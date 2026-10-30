#include "XDeviceSerialPort.h"

#if defined(_WIN32) && defined(XSERIALPORT_USE_PLATFORM_API)

#include "XMemory.h"
#include "XVariant.h"
#include "XVarList.h"
#include "XString.h"
#include "XSerialPort.h"
#include "XAbstractNetIoRing.h" /* 全局事件环（IOCP 完成端口宿主） */
#include "XRingBuffer.h"         /* 已完成异步读数据的落地缓冲 */
#include <winsock2.h>           /* FD_READ/FD_WRITE 事件掩码（须先于 windows.h） */
#include "XNetIoRingWin32.h"    /* IOCP 事件上下文与完成端口关联 */
#include <windows.h>
#include <devpropdef.h>
#include <setupapi.h>
#include <stdint.h>
#include <stdio.h>
#include <stdio.h>
#include <string.h>
#include <wchar.h>
#include <stdlib.h>

/* ================================================================
 * IOCP 事件驱动接收（Windows 侧，2026-10-30，与 POSIX 侧同构）
 *
 * 串口句柄以 FILE_FLAG_OVERLAPPED 打开并关联到**框架的 IOCP 完成端口**：
 * 常驻一个 OVERLAPPED ReadFile ⇒ 数据到达时内核把完成包投进完成端口
 * ⇒ 主循环（MsgWaitForMultipleObjects 把 IOCP 句柄列入等待集）被唤醒
 * ⇒ 完成回调把数据搬进接收环并通知属主发射 readyRead。
 *
 * 与 POSIX 侧（XAbstractNetIoRing 被动可读 fd）等价：
 *   「数据就绪直接唤醒主循环」，不再靠毫秒级定时器轮询等待集。
 *
 * 三条与 POSIX 侧的对应关系：
 *   POSIX：fd 注册进 ring 等待集（epoll/io_uring POLL_ADD）→ 就绪回调
 *   Win32 ：HANDLE 关联进 IOCP + 常驻 OVERLAPPED ReadFile → 完成回调
 *   两者都只做「通知」，数据落地都由属主自己的缓冲承担。
 *
 * 无 IOCP 后端（网络模块裁剪 / 全局环不可用）⇒ 句柄以同步方式打开，
 * 走原 ReadFile 路径，调用方（XSerialPort）保留毫秒级轮询兜底，
 * Windows 行为与本通道引入前逐位一致（不产生功能回退）。
 * ================================================================ */

/** @brief 常驻异步读的分片大小（字节）。Modbus RTU 帧 8~256 字节，
 *  512 足以一次取走整帧又不至于让内核分片过多。 */
#define XSPW32_READ_CHUNK 512
/** @brief 关闭时取消在途异步读后回收完成包的尝试次数（每次等待一片）。 */
#define XSPW32_CANCEL_RECLAIM_TRIES 50
/** @brief 常驻读同步完成时的连续重挂上限（防止内核持续有数据时空转）。 */
#define XSPW32_SYNC_REARM_LIMIT 8

/* ================================================================
 * Windows 定时器分辨率（2026-10-30）
 *
 * Windows 系统时钟默认分辨率 15.625ms（64Hz）：**所有**等待超时都被
 * 这个节拍量化——框架时间轮的定时器（串口轮询拍、应答超时、UI 刷新）
 * 设定 2ms 也只能按 15.625ms 步进唤醒。485 联调实测：主站在途时延
 * 100% 落在 15.625ms 的整数倍上（16/31/125/187/234ms…），而同一条
 * 链路用 pyserial 直连只有 2.9ms ⇒ 差值全在「唤醒被系统节拍量化」。
 *
 * 串口打开期间把分辨率提到 1ms（进程级副作用，作用域=串口打开期），
 * 关闭时还原。用 ntdll 的 NtSetTimerResolution 动态取址，免去链接
 * winmm.lib 的依赖。
 * ================================================================ */
typedef LONG (WINAPI *PFN_NtSetTimerResolution)(ULONG DesiredResolution,
                                                BOOLEAN IncreaseResolution,
                                                PULONG CurrentResolution);
static int g_xspw32_timerResRefs = 0; /**< 抬分辨率的引用计数（多串口）。 */

static void xspw32_setTimerResolution(bool raise)
{
    HMODULE ntdll = GetModuleHandleW(L"ntdll.dll");
    PFN_NtSetTimerResolution fn;
    ULONG actual = 0;
    if (!ntdll) return;
    fn = (PFN_NtSetTimerResolution)GetProcAddress(ntdll, "NtSetTimerResolution");
    if (!fn) return;
    if (raise) {
        if (g_xspw32_timerResRefs++ == 0) {
            (void)fn(10000 /* 1ms，单位 100ns */, TRUE, &actual);
            XPrintf("[XSerial] 系统定时器分辨率: 目标 1.000ms，实际 %.3fms\n",
                    (double)actual / 10000.0);
        }
    } else if (g_xspw32_timerResRefs > 0) {
        if (--g_xspw32_timerResRefs == 0)
            (void)fn(10000, FALSE, &actual);
    }
}

typedef struct XDeviceSerialPortWin32Context
{
    XDeviceSerialPortContext m_base;
    HANDLE m_handle;
    COMMTIMEOUTS m_originalTimeouts;
    bool m_haveOriginalTimeouts;
    /* ---- IOCP 事件驱动接收（2026-10-30，与 POSIX 侧 ring 等待集同构）----
     * 串口以 FILE_FLAG_OVERLAPPED 打开，常驻一个 OVERLAPPED ReadFile：
     * 数据到达 ⇒ 内核把完成包投进**框架的 IOCP 完成端口** ⇒ 事件循环被
     * 唤醒 ⇒ 完成回调取走数据并通知属主发射 readyRead。
     * 与 POSIX 侧（XAbstractNetIoRing 被动可读 fd）的语义一致：
     * 「数据就绪直接唤醒主循环」，不再靠毫秒级定时器轮询等待集。
     * 无 IOCP 后端（如裁剪掉网络模块）⇒ 回落 2ms 轮询（行为不变）。 */
    bool m_overlapped;                /**< 句柄以 FILE_FLAG_OVERLAPPED 打开（IOCP 模式）。 */
    bool m_iocpAssociated;            /**< 句柄是否已关联框架 IOCP。 */
    XEventContext_IOCP m_readContext; /**< 常驻异步读事件上下文（OVERLAPPED 为首成员）。 */
    XEventContext_IOCP m_writeContext;/**< 写完成上下文（写也须带 OVERLAPPED）。 */
    char* m_readBuffer;               /**< 常驻异步读缓冲（由上下文拥有）。 */
    HANDLE m_writeEvent;              /**< 写完成事件（同步等待写完成用）。 */
    XFd m_fd;                         /**< 本上下文的统一描述符（完成包回填用）。 */
    bool m_readPending;               /**< 是否有在途异步读。 */
    bool m_writePending;              /**< 是否有在途异步写（完成包待回收）。 */
} XDeviceSerialPortWin32Context;

static XDeviceSerialPortWin32Context* win32Context(XFd fd)
{
    return (XDeviceSerialPortWin32Context*)XDevice_handle(fd);
}

/* 把一次异步读得到的字节交给属主（落地到接收环 + 通知可读）。 */
static void xspw32_deliver(XDeviceSerialPortWin32Context* owner, size_t bytes)
{
    if (!owner || bytes == 0) return;
    if (owner->m_base.m_readBuffer)
        XRingBuffer_write(owner->m_base.m_readBuffer, owner->m_readBuffer, bytes);
    if (owner->m_base.m_readableNotify)
        owner->m_base.m_readableNotify(owner->m_base.m_readableCtx);
}

/* 挂起常驻异步读（定义见下，完成回调需要先挂起续读）。 */
static void xspw32_armRead(XDeviceSerialPortWin32Context* owner);

/* 异步读完成回调：框架从完成端口出队本读完成包后调用，
   context->finishedBytes 已由事件环写入实际传输字节数。
   返回 false = 不再把 CQ 条目分发给应用层（串口走 readyRead 信号路径，
   与文件设备共享内存通道同款：内部接收不产生应用层 socket 事件）。 */
static bool xspw32_readCompletion(XEventContext_IOCP* context, void* userData)
{
    XDeviceSerialPortWin32Context* owner = (XDeviceSerialPortWin32Context*)userData;
    size_t bytes;
    if (!owner) return false;
    bytes = context->finishedBytes;
    owner->m_readPending = false;
    xspw32_deliver(owner, bytes);
    /* 常驻读续挂：读始终在途 ⇒ 下一批数据到达即产生完成包。 */
    xspw32_armRead(owner);
    return false;
}

/* 写完成回调：句柄以 FILE_FLAG_OVERLAPPED 打开 ⇒ 写也走 OVERLAPPED，
   完成包同样进框架完成端口。本回调只消费完成包（清在途标志），
   不生成应用层事件。 */
static bool xspw32_writeCompletion(XEventContext_IOCP* context, void* userData)
{
    XDeviceSerialPortWin32Context* owner = (XDeviceSerialPortWin32Context*)userData;
    (void)context;
    if (!owner) return false;
    owner->m_writePending = false;
    return false;
}

/* 挂起常驻异步读。
   关键约定：**不使用** FILE_SKIP_COMPLETION_PORT_ON_SUCCESS —— 该标志会
   让「成功完成」的 overlapped 读/写**不投完成包**，而串口驱动的读完成
   恰好以 STATUS_SUCCESS 回报 ⇒ 完成包被吞 ⇒ 常驻读永远等不到通知
   （真机实测：应答延迟劣化到 4s 级）。故本路径依赖「每次 overlapped
   操作必投一个完成包」这一确定语义：
     同步完成 ⇒ 完成包照投（只是晚一点出队），
     异步完成（ERROR_IO_PENDING）⇒ 数据到达时投包。
   两种情形统一由 xspw32_readCompletion 搬运数据并续挂下一次读。 */
static void xspw32_armRead(XDeviceSerialPortWin32Context* owner)
{
    BOOL ok;
    if (!owner || owner->m_readPending) return;
    if (!owner->m_handle || owner->m_handle == INVALID_HANDLE_VALUE) return;
    if (!owner->m_readBuffer) return;
    memset(&owner->m_readContext, 0, sizeof(owner->m_readContext));
    owner->m_readContext.base.type = XEventContextType_Type_File;
    owner->m_readContext.base.fd = owner->m_fd;
    owner->m_readContext.socket =
        XSocketDescriptor_fromIntptr((intptr_t)owner->m_handle);
    owner->m_readContext.eventMask = FD_READ;
    owner->m_readContext.buffer = owner->m_readBuffer;
    owner->m_readContext.bufferSize = XSPW32_READ_CHUNK;
    owner->m_readContext.completionCallback = xspw32_readCompletion;
    owner->m_readContext.completionUserData = owner;
    ok = ReadFile(owner->m_handle, owner->m_readBuffer,
                  (DWORD)XSPW32_READ_CHUNK, NULL,
                  &owner->m_readContext.base.overlapped);
    if (!ok && GetLastError() != ERROR_IO_PENDING) return; /* 读失败 */
    owner->m_readPending = true;                            /* 等完成包 */
}

/* 把串口句柄关联到框架的 IOCP 完成端口（幂等）。 */
static bool xspw32_associate(XDeviceSerialPortWin32Context* owner)
{
    XAbstractNetIoRing* ring;
    if (!owner) return false;
    if (owner->m_iocpAssociated) return true;
    if (!owner->m_handle || owner->m_handle == INVALID_HANDLE_VALUE) return false;
    ring = XAbstractNetIoRing_global();
    if (!ring || !XAbstractNetIoRing_isEnabled(ring)) return false;
    if (!XNetIoRingWin32_assocHandle((XNetIoRingWin32*)ring, owner->m_handle,
                                     (ULONG_PTR)&owner->m_base))
        return false;
    owner->m_iocpAssociated = true;
    return true;
}

/* ================================================================
 * 可读事件源（跨平台统一的等待集接入，2026-10-30）
 *
 * Windows 侧与 POSIX 侧同构：句柄已关联框架 IOCP 且常驻异步读在途
 * ⇒ 数据到达即产生完成包 ⇒ 事件循环被唤醒 ⇒ 完成回调通知属主。
 * 无 IOCP 后端（句柄以同步方式打开）⇒ 返回 false，调用方（XSerialPort）
 * 保留毫秒级轮询兜底，行为与本通道引入前逐位一致。
 * ================================================================ */
bool XDeviceSerialPort_platformArmReadable(XFd fd,
                                           void (*notify)(void* ctx),
                                           void* ctx)
{
    XDeviceSerialPortWin32Context* context = win32Context(fd);
    if (!context) return false;
    context->m_fd = fd;
    context->m_base.m_readableNotify = notify;
    context->m_base.m_readableCtx = ctx;
    if (!context->m_overlapped) return false;
    if (!xspw32_associate(context)) return false;
    if (!context->m_readPending) xspw32_armRead(context);
    return true;
}

void XDeviceSerialPort_platformDisarmReadable(XFd fd)
{
    XDeviceSerialPortWin32Context* context = win32Context(fd);
    if (!context) return;
    /* 与 POSIX 侧同款语义：只是「不再通知」，**不拆常驻读**——
     * 常驻读继续把数据搬进接收环（否则数据会滞留在驱动缓冲里丢失，
     * 而 POSIX 侧数据是留在内核缓冲里等 platformRead 取走的）。 */
    context->m_base.m_readableNotify = NULL;
    context->m_base.m_readableCtx = NULL;
}

static BYTE win32DataBits(XSerialPort_DataBits value)
{
    return value >= XSerialPort_Data5 && value <= XSerialPort_Data8 ? (BYTE)value : 8;
}

static BYTE win32Parity(XSerialPort_Parity value)
{
    switch (value) {
    case XSerialPort_EvenParity: return EVENPARITY;
    case XSerialPort_OddParity: return ODDPARITY;
    case XSerialPort_MarkParity: return MARKPARITY;
    case XSerialPort_SpaceParity: return SPACEPARITY;
    default: return NOPARITY;
    }
}

static BYTE win32StopBits(XSerialPort_StopBits value)
{
    return value == XSerialPort_TwoStop ? TWOSTOPBITS : ONESTOPBIT;
}

static bool win32ApplyConfig(XDeviceSerialPortWin32Context* context)
{
    DCB dcb;
    if (!context || context->m_handle == INVALID_HANDLE_VALUE) return false;
    memset(&dcb, 0, sizeof(dcb));
    dcb.DCBlength = sizeof(dcb);
    if (!GetCommState(context->m_handle, &dcb)) return false;
    dcb.BaudRate = context->m_base.m_baudRate;
    dcb.ByteSize = win32DataBits(context->m_base.m_dataBits);
    dcb.Parity = win32Parity(context->m_base.m_parity);
    dcb.StopBits = win32StopBits(context->m_base.m_stopBits);
    dcb.fParity = context->m_base.m_parity != XSerialPort_NoParity;
    dcb.fOutxCtsFlow = FALSE;
    dcb.fRtsControl = RTS_CONTROL_DISABLE;
    dcb.fInX = FALSE;
    dcb.fOutX = FALSE;
    if (context->m_base.m_flowControl == XSerialPort_HardwareControl ||
        context->m_base.m_flowControl == XSerialPort_BothControl) {
        dcb.fOutxCtsFlow = TRUE;
        dcb.fRtsControl = RTS_CONTROL_HANDSHAKE;
    }
    if (context->m_base.m_flowControl == XSerialPort_SoftwareControl ||
        context->m_base.m_flowControl == XSerialPort_BothControl) {
        dcb.fInX = TRUE;
        dcb.fOutX = TRUE;
    }
    return SetCommState(context->m_handle, &dcb) != FALSE;
}

XDeviceSerialPortContext* XDeviceSerialPort_platformCreateContext(void)
{
    XDeviceSerialPortWin32Context* context =
        (XDeviceSerialPortWin32Context*)XCalloc_System(1, sizeof(*context));
    if (context) context->m_handle = INVALID_HANDLE_VALUE;
    return context ? &context->m_base : NULL;
}

void XDeviceSerialPort_platformDeleteContext(XDeviceSerialPortContext* base)
{
    XFree_System(base);
}

bool XDeviceSerialPort_platformOpen(XFd fd, const XString* portName)
{
    XDeviceSerialPortWin32Context* context = win32Context(fd);
    const wchar_t* path;
    wchar_t fullPath[256];
    DWORD access = 0;
    DWORD flags = FILE_ATTRIBUTE_NORMAL;
    COMMTIMEOUTS timeouts;
    XAbstractNetIoRing* ring;
    bool wantIocp;
    if (!context || !portName) return false;
    context->m_fd = fd;
    path = (const wchar_t*)XString_toUtf16(portName);
    if (!path) return false;
    if (context->m_base.m_openMode & XIODevice_ReadOnly) access |= GENERIC_READ;
    if (context->m_base.m_openMode & XIODevice_WriteOnly) access |= GENERIC_WRITE;
    if (wcsncmp(path, L"\\\\.\\", 4) == 0)
        wcsncpy(fullPath, path, sizeof(fullPath) / sizeof(fullPath[0]) - 1);
    else {
        fullPath[0] = L'\\'; fullPath[1] = L'\\'; fullPath[2] = L'.'; fullPath[3] = L'\\';
        wcsncpy(fullPath + 4, path, sizeof(fullPath) / sizeof(fullPath[0]) - 5);
    }
    fullPath[sizeof(fullPath) / sizeof(fullPath[0]) - 1] = L'\0';
    /* [IOCP 2026-10-30] 有框架 IOCP 后端 ⇒ 句柄以 FILE_FLAG_OVERLAPPED
     * 打开并关联完成端口，走「常驻异步读 + 完成包唤醒主循环」；
     * 无后端 ⇒ 同步打开（保留原 ReadFile 路径 + 调用方毫秒轮询兜底）。 */
    ring = XAbstractNetIoRing_global();
    wantIocp = (ring && XAbstractNetIoRing_isEnabled(ring));
    {   /* 现场对照开关：XSERIAL_WIN32_SYNC=1 强制走同步读 + 毫秒轮询兜底
         *（绕开 overlapped 完成包路径），用于判定 CH343 驱动对 overlapped
         * 完成包的投递节奏 vs 同步读的数据可见时机。 */
        const char* syncEnv = getenv("XSERIAL_WIN32_SYNC");
        if (syncEnv && syncEnv[0] == '1') wantIocp = false;
    }
    if (wantIocp) flags |= FILE_FLAG_OVERLAPPED;
    context->m_handle = CreateFileW(fullPath, access, 0, NULL, OPEN_EXISTING,
                                    flags, NULL);
    if (context->m_handle == INVALID_HANDLE_VALUE) return false;
    context->m_overlapped = wantIocp;
    if (wantIocp) {
        /* 注意：这里**不**设 FILE_SKIP_COMPLETION_PORT_ON_SUCCESS ——
         * 该标志会吞掉「成功完成」的完成包，串口读完成恰以 STATUS_SUCCESS
         * 回报 ⇒ 常驻读永远收不到通知（真机实测：应答延迟劣化到 4s 级）。 */
        context->m_readBuffer = (char*)XCalloc_System(1, XSPW32_READ_CHUNK);
        context->m_writeEvent = CreateEventW(NULL, TRUE, FALSE, NULL);
        if (!context->m_readBuffer || !context->m_writeEvent) {
            CloseHandle(context->m_handle);
            context->m_handle = INVALID_HANDLE_VALUE;
            context->m_overlapped = false;
            if (context->m_readBuffer) {
                XFree_System(context->m_readBuffer);
                context->m_readBuffer = NULL;
            }
            if (context->m_writeEvent) {
                CloseHandle(context->m_writeEvent);
                context->m_writeEvent = NULL;
            }
            return false;
        }
        if (!xspw32_associate(context)) {
            /* 关联失败 ⇒ 退回同步形态（重开一次）。 */
            CloseHandle(context->m_handle);
            context->m_handle = INVALID_HANDLE_VALUE;
            XFree_System(context->m_readBuffer);
            context->m_readBuffer = NULL;
            CloseHandle(context->m_writeEvent);
            context->m_writeEvent = NULL;
            context->m_overlapped = false;
            context->m_handle = CreateFileW(fullPath, access, 0, NULL,
                                            OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL,
                                            NULL);
            if (context->m_handle == INVALID_HANDLE_VALUE) return false;
        }
    }
    memset(&context->m_originalTimeouts, 0, sizeof(context->m_originalTimeouts));
    context->m_haveOriginalTimeouts = GetCommTimeouts(context->m_handle,
                                                       &context->m_originalTimeouts) != FALSE;
    memset(&timeouts, 0, sizeof(timeouts));
    timeouts.ReadIntervalTimeout = MAXDWORD;
    if (!SetCommTimeouts(context->m_handle, &timeouts) ||
        !SetupComm(context->m_handle, (DWORD)context->m_base.m_readBufferSize,
                   (DWORD)context->m_base.m_readBufferSize) || !win32ApplyConfig(context)) {
        CloseHandle(context->m_handle);
        context->m_handle = INVALID_HANDLE_VALUE;
        return false;
    }
    PurgeComm(context->m_handle, PURGE_RXCLEAR | PURGE_TXCLEAR);
    /* 打开期间把系统定时器分辨率提到 1ms：框架时间轮的毫秒设定值
     * 不再被 15.625ms 系统节拍量化（否则应答在途时延整体抬高一个
     * 节拍，485 轮询周期也跟着漂）。 */
    xspw32_setTimerResolution(true);
    if (context->m_overlapped) xspw32_armRead(context);
    return true;
}

/* 取消在途异步读并回收完成包（关闭前调用）：
   提交 CancelIoEx 后经事件环有界等待该读的完成包出队（完成回调清除
   m_readPending），确保释放上下文前不存在悬垂 OVERLAPPED 完成包
   （与文件设备共享内存通道的回收语义一致）。 */
static void xspw32_cancelRead(XDeviceSerialPortWin32Context* context)
{
    XAbstractNetIoRing* ring;
    int tries = 0;
    if (!context || !context->m_readPending) return;
    ring = XAbstractNetIoRing_global();
    if (!ring) { context->m_readPending = false; return; }
    if (context->m_handle && context->m_handle != INVALID_HANDLE_VALUE)
        CancelIoEx(context->m_handle, &context->m_readContext.base.overlapped);
    while (context->m_readPending && tries < XSPW32_CANCEL_RECLAIM_TRIES) {
        XAbstractNetIoRing_waitForEvents_base(ring, 1);
        ++tries;
    }
    context->m_readPending = false;
}

/* 回收残留的写完成包（写也走 OVERLAPPED，完成包指向写上下文；
   关闭前必须等它出队，否则释放上下文后完成包会指向已释放内存）。 */
static void xspw32_drainWrite(XDeviceSerialPortWin32Context* context)
{
    XAbstractNetIoRing* ring;
    int tries = 0;
    if (!context || !context->m_writePending) return;
    ring = XAbstractNetIoRing_global();
    if (!ring) { context->m_writePending = false; return; }
    while (context->m_writePending && tries < XSPW32_CANCEL_RECLAIM_TRIES) {
        XAbstractNetIoRing_waitForEvents_base(ring, 1);
        ++tries;
    }
    context->m_writePending = false;
}

void XDeviceSerialPort_platformClose(XFd fd)
{
    XDeviceSerialPortWin32Context* context = win32Context(fd);
    if (!context) return;
    if (context->m_overlapped) {
        xspw32_cancelRead(context);
        xspw32_drainWrite(context);
    }
    if (context->m_readBuffer) {
        XFree_System(context->m_readBuffer);
        context->m_readBuffer = NULL;
    }
    if (context->m_writeEvent) {
        CloseHandle(context->m_writeEvent);
        context->m_writeEvent = NULL;
    }
    if (context->m_handle != INVALID_HANDLE_VALUE) {
        if (context->m_haveOriginalTimeouts)
            SetCommTimeouts(context->m_handle, &context->m_originalTimeouts);
        CloseHandle(context->m_handle);
        context->m_handle = INVALID_HANDLE_VALUE;
    }
    context->m_overlapped = false;
    context->m_iocpAssociated = false;
    xspw32_setTimerResolution(false);
}

int64_t XDeviceSerialPort_platformRead(XFd fd, void* buffer, int64_t size)
{
    XDeviceSerialPortWin32Context* context = win32Context(fd);
    DWORD done = 0;
    if (!context || context->m_handle == INVALID_HANDLE_VALUE || !buffer || size <= 0) return -1;
    if (context->m_overlapped) {
        /* 常驻异步读已把数据搬进接收环（与 POSIX 侧 platformRead 优先读
           m_readBuffer 同款语义）。 */
        if (context->m_base.m_readBuffer &&
            XRingBuffer_available(context->m_base.m_readBuffer) > 0)
            return (int64_t)XRingBuffer_read(context->m_base.m_readBuffer, buffer,
                                              (size_t)size);
        return 0;
    }
    if (!ReadFile(context->m_handle, buffer, (DWORD)size, &done, NULL)) return -1;
    return (int64_t)done;
}

int64_t XDeviceSerialPort_platformWrite(XFd fd, const void* data, int64_t size)
{
    XDeviceSerialPortWin32Context* context = win32Context(fd);
    DWORD done = 0;
    if (!context || context->m_handle == INVALID_HANDLE_VALUE || !data || size <= 0) return -1;
    if (context->m_overlapped) {
        /* OVERLAPPED 句柄：写必须带 OVERLAPPED。提交后等完成，保持改造前
         * 「写完才返回」的同步语义（写完成包由 xspw32_writeCompletion 消费）。 */
        BOOL ok;
        memset(&context->m_writeContext.base.overlapped, 0, sizeof(OVERLAPPED));
        context->m_writeContext.base.overlapped.hEvent = context->m_writeEvent;
        context->m_writeContext.base.type = XEventContextType_Type_File;
        context->m_writeContext.base.fd = fd;
        context->m_writeContext.socket =
            XSocketDescriptor_fromIntptr((intptr_t)context->m_handle);
        context->m_writeContext.eventMask = FD_WRITE;
        context->m_writeContext.completionCallback = xspw32_writeCompletion;
        context->m_writeContext.completionUserData = context;
        ResetEvent(context->m_writeEvent);
        context->m_writePending = true;
        ok = WriteFile(context->m_handle, data, (DWORD)size, &done,
                       &context->m_writeContext.base.overlapped);
        if (!ok) {
            if (GetLastError() != ERROR_IO_PENDING) {
                context->m_writePending = false;
                return -1;
            }
            if (!GetOverlappedResult(context->m_handle,
                                     &context->m_writeContext.base.overlapped,
                                     &done, TRUE)) {
                context->m_writePending = false;
                return -1;
            }
            /* 异步写：完成包已排入完成端口，由 xspw32_writeCompletion 消费。 */
        }
        /* 同步完成的写同样会投完成包（未设 SKIP_COMPLETION_PORT_ON_SUCCESS）
         * ⇒ 在途标志由回调清除，关闭时统一回收。 */
        return (int64_t)done;
    }
    if (!WriteFile(context->m_handle, data, (DWORD)size, &done, NULL)) return -1;
    return (int64_t)done;
}

bool XDeviceSerialPort_platformFlush(XFd fd)
{
    XDeviceSerialPortWin32Context* context = win32Context(fd);
    return context && context->m_handle != INVALID_HANDLE_VALUE &&
           FlushFileBuffers(context->m_handle) != FALSE;
}

bool XDeviceSerialPort_platformSetProperty(XFd fd, uint32_t property, const XVariant* value)
{
    XDeviceSerialPortWin32Context* context = win32Context(fd);
    bool enabled;
    if (!context || !value || context->m_handle == INVALID_HANDLE_VALUE) return false;
    switch (property) {
    case XDeviceSerialPortProperty_BaudRate:
    case XDeviceSerialPortProperty_DataBits:
    case XDeviceSerialPortProperty_Parity:
    case XDeviceSerialPortProperty_StopBits:
    case XDeviceSerialPortProperty_FlowControl:
        return win32ApplyConfig(context);
    case XDeviceSerialPortProperty_ReadBufferSize:
        return SetupComm(context->m_handle, (DWORD)XVariant_toInt64(value),
                         (DWORD)XVariant_toInt64(value)) != FALSE;
    case XDeviceSerialPortProperty_DataTerminalReady:
        enabled = XVariant_toBool(value);
        return EscapeCommFunction(context->m_handle, enabled ? SETDTR : CLRDTR) != FALSE;
    case XDeviceSerialPortProperty_RequestToSend:
        enabled = XVariant_toBool(value);
        return EscapeCommFunction(context->m_handle, enabled ? SETRTS : CLRRTS) != FALSE;
    case XDeviceSerialPortProperty_BreakEnabled:
        enabled = XVariant_toBool(value);
        return EscapeCommFunction(context->m_handle, enabled ? SETBREAK : CLRBREAK) != FALSE;
    default:
        return false;
    }
}

bool XDeviceSerialPort_platformGetProperty(XFd fd, uint32_t property, XVariant* value)
{
    XDeviceSerialPortWin32Context* context = win32Context(fd);
    COMSTAT status;
    DWORD errors = 0;
    DWORD modem = 0;
    int signals;
    if (!context || !value || context->m_handle == INVALID_HANDLE_VALUE) return false;
    switch (property) {
    case XDeviceProperty_NativeHandle:
        XVariant_setValue_ptr(value, context->m_handle); return true;
    case XDeviceSerialPortProperty_BytesAvailable:
        if (!ClearCommError(context->m_handle, &errors, &status)) return false;
        XVariant_setValue_int64(value, status.cbInQue); return true;
    case XDeviceSerialPortProperty_PinoutSignals:
        if (!GetCommModemStatus(context->m_handle, &modem)) return false;
        signals = XSerialPort_NoSignal;
        if (modem & MS_CTS_ON) signals |= XSerialPort_ClearToSendSignal;
        if (modem & MS_DSR_ON) signals |= XSerialPort_DataSetReadySignal;
        if (modem & MS_RING_ON) signals |= XSerialPort_RingIndicatorSignal;
        if (modem & MS_RLSD_ON) signals |= XSerialPort_DataCarrierDetectSignal;
        XVariant_setValue_int(value, signals); return true;
    default:
        return false;
    }
}

bool XDeviceSerialPort_platformControl(XFd fd, uint32_t command,
                                       const XVarList* input, XVarList* output)
{
    XDeviceSerialPortWin32Context* context = win32Context(fd);
    XSerialPort_Direction directions;
    XVarList* arguments = (XVarList*)input;
    DWORD flags = 0;
    (void)output;
    if (!context || context->m_handle == INVALID_HANDLE_VALUE) return false;
    if (command == XDeviceSerialPortCommand_Clear) {
        if (!arguments || arguments->m_size != sizeof(directions)) return false;
        XVarList_start(arguments);
        directions = XVarList_arg(arguments, XSerialPort_Direction);
        if (directions & XSerialPort_Input) flags |= PURGE_RXCLEAR;
        if (directions & XSerialPort_Output) flags |= PURGE_TXCLEAR;
        return PurgeComm(context->m_handle, flags) != FALSE;
    }
    return command == XDeviceSerialPortCommand_HandleEvent ||
           command == XDeviceCommand_Cancel;
}

/* ==================================================================== */
/* 串口枚举：SetupAPI 枚举串口设备接口                                    */
/* ==================================================================== */

/*
 * GUID 与 DEVPROPKEY 在此以文件局部常量给出，不引用 SDK 的 <initguid.h> /
 * <devguid.h> / <devpkey.h> 宏：
 *   - <initguid.h> 会把 DEFINE_GUID 展开为带外部链接的定义，本工程另有翻译
 *     单元（Drive/windows/Device/Usb/XDeviceUsb_win32.c）同样实例化了
 *     GUID_DEVINTERFACE_COMPORT，两处定义会撞 LNK2005 重复符号；
 *   - DEVPROPKEY 走 DECLSPEC_SELECTANY，多翻译单元重复定义本可合并，但一并
 *     局部化可省去 initguid 的宏开关 juggling。
 * 取值均来自 Windows SDK：
 *   GUID_DEVCLASS_PORTS                          {4D36E978-E325-11CE-BFC1-08002BE10318}
 *   DEVPKEY_Device_Manufacturer                  {A45C254E-DF1C-4EFD-8020-67D146A850E0},13
 */

/**
 * @brief 串口设备类别 GUID（GUID_DEVCLASS_PORTS，"端口 (COM 和 LPT)" 类）。
 * @details SetupDiGetClassDevsW 的首参是"设备类"而非"设备接口"，串口设备全部
 *          归属本类；类下每个设备的 COM 端口名取自其 "Device" 键的 PortName 值。
 */
static const GUID kSerialPortClassGuid =
    {0x4d36e978L, 0xe325, 0x11ce, {0xbf, 0xc1, 0x08, 0x00, 0x2b, 0xe1, 0x03, 0x18}};

/**
 * @brief 设备属性键的格式 GUID（DEVPKEY_Device_*）。
 */
static const GUID kDevicePropertyFmtId =
    {0xa45c254eL, 0xdf1c, 0x4efd, {0x80, 0x20, 0x67, 0xd1, 0x46, 0xa8, 0x50, 0xe0}};

/**
 * @brief 读取设备的注册表字符串属性。
 * @param deviceInfoSet 设备集。
 * @param deviceInfoData 设备数据。
 * @param property 注册表属性码（SPDRP_*）。
 * @param buffer 接收缓冲（宽字符）。
 * @param capacity 缓冲容量（字符数）。
 * @return 读到非空字符串返回 true。
 */
static bool win32GetRegistryString(HDEVINFO deviceInfoSet,
                                   SP_DEVINFO_DATA* deviceInfoData, DWORD property,
                                   wchar_t* buffer, DWORD capacity)
{
    DWORD required = 0;
    if (capacity == 0) return false;
    buffer[0] = L'\0';
    if (!SetupDiGetDeviceRegistryPropertyW(deviceInfoSet, deviceInfoData, property, NULL,
                                           (PBYTE)buffer, (DWORD)(capacity * sizeof(wchar_t)),
                                           &required))
        return false;
    buffer[capacity - 1] = L'\0';
    return buffer[0] != L'\0';
}

/**
 * @brief 读取设备的 DEVPKEY 字符串属性。
 * @param deviceInfoSet 设备集。
 * @param deviceInfoData 设备数据。
 * @param key 属性键。
 * @param buffer 接收缓冲（宽字符）。
 * @param capacity 缓冲容量（字符数）。
 * @return 读到非空字符串返回 true。
 */
static bool win32GetDeviceString(HDEVINFO deviceInfoSet,
                                 SP_DEVINFO_DATA* deviceInfoData, const DEVPROPKEY* key,
                                 wchar_t* buffer, DWORD capacity)
{
    DEVPROPTYPE type = 0;
    DWORD required = 0;
    if (capacity == 0) return false;
    buffer[0] = L'\0';
    if (!SetupDiGetDevicePropertyW(deviceInfoSet, deviceInfoData, key, &type,
                                   (PBYTE)buffer, (DWORD)(capacity * sizeof(wchar_t)),
                                   &required, 0))
        return false;
    if (type != DEVPROP_TYPE_STRING) return false;
    buffer[capacity - 1] = L'\0';
    return buffer[0] != L'\0';
}

/**
 * @brief 从设备的 "Device" 注册表键读取串口名（PortName）。
 * @param deviceInfoSet 设备集。
 * @param deviceInfoData 设备数据。
 * @param buffer 接收缓冲（宽字符）。
 * @param capacity 缓冲容量（字符数）。
 * @return 读到端口名返回 true。
 * @note 串口类设备的端口名（COMx）存放在设备枚举子键的 "Device" 键下的
 *       "PortName" 值里；这是把 PnP 端口实例对应到 COM 端口名的权威来源。
 */
static bool win32ReadPortName(HDEVINFO deviceInfoSet, SP_DEVINFO_DATA* deviceInfoData,
                              wchar_t* buffer, DWORD capacity)
{
    HKEY key;
    DWORD type = 0;
    DWORD size;
    bool ok = false;
    if (capacity == 0) return false;
    buffer[0] = L'\0';
    key = SetupDiOpenDevRegKey(deviceInfoSet, deviceInfoData, DICS_FLAG_GLOBAL, 0,
                               DIREG_DEV, KEY_READ);
    if (!key) return false;
    size = (DWORD)(capacity * sizeof(wchar_t));
    if (RegQueryValueExW(key, L"PortName", NULL, &type, (LPBYTE)buffer, &size) == ERROR_SUCCESS &&
        type == REG_SZ) {
        buffer[capacity - 1] = L'\0';
        ok = buffer[0] != L'\0';
    }
    RegCloseKey(key);
    return ok;
}

/**
 * @brief 解析十六进制数字串（最多 8 位）。
 * @param text 源字符串；不可为 NULL。
 * @param maxLength 最多解析的字符数。
 * @return 解析出的数值；无有效十六进制数字时返回 0。
 */
static uint32_t win32ParseHex(const wchar_t* text, size_t maxLength)
{
    uint32_t value = 0;
    size_t i;
    for (i = 0; i < maxLength; ++i) {
        wchar_t c = text[i];
        int digit;
        if (c >= L'0' && c <= L'9') digit = c - L'0';
        else if (c >= L'A' && c <= L'F') digit = c - L'A' + 10;
        else if (c >= L'a' && c <= L'f') digit = c - L'a' + 10;
        else break;
        value = value * 16 + (uint32_t)digit;
    }
    return value;
}

/**
 * @brief 从设备实例 ID 里解析 USB 厂商 ID 与产品 ID。
 * @param instanceId 设备实例 ID（如 "USB\VID_1A86&PID_7523\9&2952D8C4&0&4"）。
 * @param vendorId 接收厂商 ID。
 * @param productId 接收产品 ID。
 */
static void win32ParseUsbIds(const wchar_t* instanceId, uint32_t* vendorId, uint32_t* productId)
{
    const wchar_t* cursor;
    *vendorId = 0;
    *productId = 0;
    if (!instanceId) return;
    cursor = wcsstr(instanceId, L"VID_");
    if (cursor) *vendorId = win32ParseHex(cursor + 4, 4);
    cursor = wcsstr(instanceId, L"PID_");
    if (cursor) *productId = win32ParseHex(cursor + 4, 4);
}

/**
 * @brief 宽字符转 UTF-8。
 * @param wide 源宽字符串；不可为 NULL。
 * @param buffer 接收缓冲。
 * @param capacity 缓冲容量（字节）。
 * @return 转换成功返回 true。
 */
static bool win32WideToUtf8(const wchar_t* wide, char* buffer, size_t capacity)
{
    int written;
    if (!wide || !buffer || capacity == 0) return false;
    written = WideCharToMultiByte(CP_UTF8, 0, wide, -1, buffer, (int)capacity, NULL, NULL);
    return written > 0;
}

/**
 * @brief 探测端口是否已被占用。
 * @param portName 端口名（如 "COM3"）。
 * @return 已被占用返回 true。
 * @note 探测方式是以独占方式打开端口后立即关闭；若别的进程已持有该端口，
 *       独占打开会因共享冲突失败。
 */
static bool win32PortIsBusy(const wchar_t* portName)
{
    wchar_t path[64];
    HANDLE handle;
    bool busy = false;
    swprintf(path, sizeof(path) / sizeof(path[0]), L"\\\\.\\%s", portName);
    handle = CreateFileW(path, GENERIC_READ | GENERIC_WRITE, 0, NULL, OPEN_EXISTING,
                         FILE_ATTRIBUTE_NORMAL, NULL);
    if (handle == INVALID_HANDLE_VALUE) {
        busy = (GetLastError() == ERROR_ACCESS_DENIED || GetLastError() == ERROR_SHARING_VIOLATION);
    } else {
        CloseHandle(handle);
    }
    return busy;
}

bool XDeviceSerialPort_platformEnumeratePorts(XDeviceSerialPortPortCollector collector, void* userData)
{
    HDEVINFO deviceInfoSet;
    SP_DEVINFO_DATA deviceInfoData;
    DWORD index = 0;
    DEVPROPKEY manufacturerKey;

    if (!collector) return false;
    deviceInfoSet = SetupDiGetClassDevsW(&kSerialPortClassGuid, NULL, NULL, DIGCF_PRESENT);
    if (deviceInfoSet == INVALID_HANDLE_VALUE) return false;

    manufacturerKey.fmtid = kDevicePropertyFmtId;
    manufacturerKey.pid = 13;

    memset(&deviceInfoData, 0, sizeof(deviceInfoData));
    deviceInfoData.cbSize = sizeof(deviceInfoData);

    while (SetupDiEnumDeviceInfo(deviceInfoSet, index++, &deviceInfoData)) {
        wchar_t portName[64];
        wchar_t friendlyName[256];
        wchar_t manufacturer[128];
        wchar_t instanceId[512];
        char portNameUtf8[128];
        char friendlyNameUtf8[512];
        char manufacturerUtf8[256];
        uint32_t vendorId = 0;
        uint32_t productId = 0;
        XDeviceSerialPortPortInfo info;

        if (!win32ReadPortName(deviceInfoSet, &deviceInfoData, portName,
                               (DWORD)(sizeof(portName) / sizeof(portName[0]))))
            continue;
        if (!win32WideToUtf8(portName, portNameUtf8, sizeof(portNameUtf8)))
            continue;

        friendlyNameUtf8[0] = '\0';
        manufacturerUtf8[0] = '\0';
        if (win32GetRegistryString(deviceInfoSet, &deviceInfoData, SPDRP_FRIENDLYNAME,
                                   friendlyName, (DWORD)(sizeof(friendlyName) / sizeof(wchar_t))))
            win32WideToUtf8(friendlyName, friendlyNameUtf8, sizeof(friendlyNameUtf8));
        if (win32GetDeviceString(deviceInfoSet, &deviceInfoData, &manufacturerKey,
                                 manufacturer, (DWORD)(sizeof(manufacturer) / sizeof(wchar_t))))
            win32WideToUtf8(manufacturer, manufacturerUtf8, sizeof(manufacturerUtf8));

        instanceId[0] = L'\0';
        if (SetupDiGetDeviceInstanceIdW(deviceInfoSet, &deviceInfoData, instanceId,
                                        (DWORD)(sizeof(instanceId) / sizeof(wchar_t)), NULL))
            win32ParseUsbIds(instanceId, &vendorId, &productId);

        memset(&info, 0, sizeof(info));
        info.m_portName = portNameUtf8;
        info.m_description = friendlyNameUtf8[0] ? friendlyNameUtf8 : NULL;
        info.m_manufacturer = manufacturerUtf8[0] ? manufacturerUtf8 : NULL;
        info.m_serialNumber = NULL;
        info.m_vendorId = vendorId;
        info.m_productId = productId;
        info.m_busy = win32PortIsBusy(portName);

        if (!collector(userData, &info)) {
            SetupDiDestroyDeviceInfoList(deviceInfoSet);
            return false;
        }
    }
    SetupDiDestroyDeviceInfoList(deviceInfoSet);
    return true;
}

#endif
