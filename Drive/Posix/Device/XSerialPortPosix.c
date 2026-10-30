#include "XDeviceSerialPort.h"
#include "XAbstractNetIoRing.h"  /* 串口 fd 注册进框架异步后端等待集（可读事件源）。 */

#if defined(XSERIALPORT_USE_PLATFORM_API) && (defined(__linux__) || defined(__unix__) || defined(__APPLE__) || \
    defined(__FreeBSD__) || defined(__OpenBSD__) || defined(__NetBSD__) || \
    defined(__DragonFly__) || defined(__sun))

#include "XMemory.h"
#include "XVariant.h"
#include "XVarList.h"
#include "XString.h"
#include "XSerialPort.h"
#include "XRingBuffer.h"
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <termios.h>
#include <unistd.h>
#include <sys/ioctl.h>

#ifndef PATH_MAX
#define PATH_MAX 4096
#endif

#ifndef CRTSCTS
#define CRTSCTS 0
#endif
#ifndef CMSPAR
#define CMSPAR 0
#endif

typedef struct XDeviceSerialPortPosixContext
{
    XDeviceSerialPortContext m_base;
    int m_nativeFd;
    struct termios m_originalTermios;
    bool m_haveOriginalTermios;
} XDeviceSerialPortPosixContext;

static XDeviceSerialPortPosixContext* posixContext(XFd fd)
{
    return (XDeviceSerialPortPosixContext*)XDevice_handle(fd);
}

/* ==================== [波特率编码 2026-10-30] ====================
 * 交叉编译命中的头文件是**宿主 glibc**（gcc 内建 /usr/aarch64-linux-gnu/
 * include 优先于 -I sysroot/usr/include，见工具链文件的老坑注），而运行
 * 目标是**板端 glibc 2.31**——两者的 B* 常量取值不同：
 *     宿主 glibc 2.39：B115200 = 115200
 *     板端 glibc 2.31：B115200 = 0x1002 = 4098
 * 传错编码值 ⇒ 板端 cfsetispeed 判非法返回 -1（EINVAL），而原实现
 * **忽略返回值** ⇒ termios 保持旧速率 ⇒ 表现「选什么波特率都停在 9600」。
 * 真机实证（RK3568 + Debian 11，2026-10-30）：
 *     BAUDDIAG posix req=115200 speed_t=115200 ispeed=13 ospeed=13 set_rc=0
 * —— 传入的是宿主编码值，cfsetispeed 静默失败，termios 仍是 13(B9600)，
 * 而 tcsetattr 因「内容没变」返回 0。
 * 修法（两层保险，不依赖编译期 B* 宏）：
 *   ① 首选**内核传统编码**自备常量——各 glibc 版本一致、也是内核 termios
 *      期望的编码（板端 glibc 2.31 的 B115200 正是这个值，stty 115200 在
 *      板端可生效即为此证）；
 *   ② 首选被目标 libc 拒绝（cfsetispeed 返回非 0）时，回落用编译期 B*
 *      宏——覆盖「目标 libc 用新编码」的平台（宿主 glibc 2.39 一类）。
 * 二者都以 cfsetispeed 的返回值为准，语义确定、无静默失败。
 */
#define XSP_BAUD_9600    13          /* 传统编码 0010005 */
#define XSP_BAUD_19200   14          /* 0010006 */
#define XSP_BAUD_38400   15          /* 0010007 */
#define XSP_BAUD_57600   0010001     /* 4097 */
#define XSP_BAUD_115200  0010002     /* 4098 */
#define XSP_BAUD_230400  0010003     /* 4099 */
#define XSP_BAUD_460800  0010004     /* 4100 */
#define XSP_BAUD_921600  0010010     /* 4104 */

/** @brief 内核传统编码的波特率常量（不依赖编译期 B* 宏）。
 *  @return 0=不支持该速率 */
static speed_t posixBaudTraditional(int32_t baudRate)
{
    switch (baudRate) {
    case 9600:  return XSP_BAUD_9600;
    case 19200: return XSP_BAUD_19200;
    case 38400: return XSP_BAUD_38400;
    case 57600: return XSP_BAUD_57600;
    case 115200: return XSP_BAUD_115200;
    case 230400: return XSP_BAUD_230400;
    case 460800: return XSP_BAUD_460800;
    case 921600: return XSP_BAUD_921600;
    default: return 0;
    }
}

/** @brief 编译期 glibc 的 B* 宏取值（宿主头口径，作为回落候选）。 */
static speed_t posixBaudLibc(int32_t baudRate)
{
    switch (baudRate) {
    case 0: return B0;
    case 50: return B50;
    case 75: return B75;
    case 110: return B110;
    case 134: return B134;
    case 150: return B150;
    case 200: return B200;
    case 300: return B300;
    case 600: return B600;
    case 1200: return B1200;
    case 1800: return B1800;
    case 2400: return B2400;
    case 4800: return B4800;
    case 9600: return B9600;
    case 19200: return B19200;
    case 38400: return B38400;
    case 57600: return B57600;
    case 115200: return B115200;
#ifdef B230400
    case 230400: return B230400;
#endif
#ifdef B460800
    case 460800: return B460800;
#endif
#ifdef B921600
    case 921600: return B921600;
#endif
    default: return 0;
    }
}

/* ==================== [mcgs 5.2.9 内核串口探测原语全坏的能力门 2026-10-29] ====================
 * 真机两案（dmesg 实证）：
 *   ① FIONREAD → PC at tty_ioctl+0x20（RX 轮询每 2ms 探测，十几秒必触发）；
 *   ② poll()   → PC at tty_poll+0x1c ← LR pserial_poll+0x20（sun8i 串口驱
 *      动的 poll 方法自身调坏指针，秒级触发）。
 * 即该平台 tty 的可用性探测两条标准原语（ioctl FIONREAD / poll-select）
 * 全灭；唯一健壮原语只剩 read()。规避 = 「fd 恒 O_NONBLOCK + 可用性探测
 * 改 read 泵」（泵进 m_readBuffer，platformRead 本就优先读 ring，语义对
 * 消费方等效——BytesAvailable 全部消费方只用 >0 判据）。
 * 能力门（对齐 XNET 引擎「内核版本门」先例，**健康平台零改动**）：
 *   - /proc/device-tree/compatible 含 sunxi/sun8i（mcgs 这类全志板）→ 泵路径；
 *   - 其余一律维持原 FIONREAD 路径**一字不改**：桌面 x86 无设备树、
 *     RK3568 = rockchip compatible，全部走原路；
 *   - XSERIAL_FORCE_READPUMP=1 强制走泵路径（跨平台测试与现场逃生口），
 *     XSERIAL_FORCE_FIONREAD=1 反向强制（对照取证）。 */

static int g_xserialReadPump = -1; /**< -1=未探测；0=FIONREAD；1=read 泵。 */

/** @brief 判定本机串口是否必须走 read 泵（结果缓存，首探一次）。 */
static bool xserialReadPumpNeeded(void)
{
    char compat[512];
    int fd;
    ssize_t n;
    const char* force;
    if (g_xserialReadPump >= 0) return g_xserialReadPump == 1;
    g_xserialReadPump = 0;
    force = getenv("XSERIAL_FORCE_READPUMP");
    if (force && force[0] == '1') g_xserialReadPump = 1;
    if (g_xserialReadPump == 0) {
        force = getenv("XSERIAL_FORCE_FIONREAD");
        if (force && force[0] == '1') { g_xserialReadPump = 0; return false; }
        fd = open("/proc/device-tree/compatible", O_RDONLY);
        if (fd >= 0) {
            n = read(fd, compat, sizeof(compat) - 1);
            close(fd);
            if (n > 0) {
                /* compatible 是 NUL 分隔的多段字符串表，逐段扫。 */
                char* p = compat;
                char* end = compat + n;
                while (p < end) {
                    size_t seg = strlen(p);
                    if (seg == 0) { ++p; continue; }
                    if (strstr(p, "sunxi") || strstr(p, "sun8i")) {
                        g_xserialReadPump = 1;
                        break;
                    }
                    p += seg + 1;
                }
            }
        }
    }
    return g_xserialReadPump == 1;
}

static bool posixApplyConfig(XDeviceSerialPortPosixContext* context)
{
    struct termios termiosValue;
    speed_t baud;
    speed_t cand[2];
    int candCount = 0;
    int ci;
    bool applied = false;
    if (!context || context->m_nativeFd < 0 || !context->m_haveOriginalTermios) return false;
    if (context->m_base.m_dataBits == XSerialPort_Data9 ||
        context->m_base.m_stopBits == XSerialPort_OneAndHalfStop) return false;
    /* [波特率编码 2026-10-30] 候选序列：内核传统编码优先、编译期 B* 宏回落。
     * 两者都被目标 libc 的 cfsetispeed 校验（返回值非 0 即该编码不被接受），
     * 杜绝「静默失败 ⇒ 速率停在旧值」。详见文件上方编码说明。 */
    baud = posixBaudTraditional(context->m_base.m_baudRate);
    if (baud != 0) cand[candCount++] = baud;
    {
        speed_t libcBaud = posixBaudLibc(context->m_base.m_baudRate);
        if (libcBaud != 0 && libcBaud != baud) cand[candCount++] = libcBaud;
    }
    if (candCount == 0) return false;

    termiosValue = context->m_originalTermios;
    cfmakeraw(&termiosValue);
    termiosValue.c_cflag |= CLOCAL | CREAD;
    termiosValue.c_cflag &= ~(CSIZE | PARENB | PARODD | CSTOPB | CRTSCTS);
    termiosValue.c_iflag &= ~(IXON | IXOFF | IXANY);

    switch (context->m_base.m_dataBits) {
    case XSerialPort_Data5: termiosValue.c_cflag |= CS5; break;
    case XSerialPort_Data6: termiosValue.c_cflag |= CS6; break;
    case XSerialPort_Data7: termiosValue.c_cflag |= CS7; break;
    default: termiosValue.c_cflag |= CS8; break;
    }
    switch (context->m_base.m_parity) {
    case XSerialPort_EvenParity: termiosValue.c_cflag |= PARENB; break;
    case XSerialPort_OddParity: termiosValue.c_cflag |= PARENB | PARODD; break;
    case XSerialPort_SpaceParity:
        if (!CMSPAR) return false;
        termiosValue.c_cflag |= PARENB | CMSPAR;
        break;
    case XSerialPort_MarkParity:
        if (!CMSPAR) return false;
        termiosValue.c_cflag |= PARENB | PARODD | CMSPAR;
        break;
    default: break;
    }
    if (context->m_base.m_stopBits == XSerialPort_TwoStop) termiosValue.c_cflag |= CSTOPB;
    if (context->m_base.m_flowControl == XSerialPort_HardwareControl ||
        context->m_base.m_flowControl == XSerialPort_BothControl) {
        if (!CRTSCTS) return false;
        termiosValue.c_cflag |= CRTSCTS;
    }
    if (context->m_base.m_flowControl == XSerialPort_SoftwareControl ||
        context->m_base.m_flowControl == XSerialPort_BothControl)
        termiosValue.c_iflag |= IXON | IXOFF | IXANY;
    /* [波特率编码 2026-10-30] 逐候选尝试：以 cfsetispeed/cfsetospeed 的
     * 返回值判定该编码是否被目标 libc 接受（不接受时返回 -1/EINVAL 且不改
     * 字段——原实现忽略返回值，故「选什么波特率都停在 9600」）。首个被接受
     * 的候选即采用；全部被拒则保持原速率（open 仍成功，速率停在旧值）。 */
    for (ci = 0; ci < candCount && !applied; ++ci) {
        struct termios trial = termiosValue;
        if (cfsetispeed(&trial, cand[ci]) != 0) continue;
        if (cfsetospeed(&trial, cand[ci]) != 0) continue;
        if (tcsetattr(context->m_nativeFd, TCSANOW, &trial) != 0) continue;
        baud = cand[ci];
        applied = true;
    }
    if (!applied) return false;
    return true;
}

static bool readBool(const XVariant* value)
{
    return XVariant_toBool(value);
}

XDeviceSerialPortContext* XDeviceSerialPort_platformCreateContext(void)
{
    XDeviceSerialPortPosixContext* context =
        (XDeviceSerialPortPosixContext*)XCalloc_System(1, sizeof(*context));
    if (context) context->m_nativeFd = -1;
    return context ? &context->m_base : NULL;
}

void XDeviceSerialPort_platformDeleteContext(XDeviceSerialPortContext* base)
{
    XFree_System(base);
}

bool XDeviceSerialPort_platformOpen(XFd fd, const XString* portName)
{
    XDeviceSerialPortPosixContext* context = posixContext(fd);
    int flags = O_NOCTTY;
    int nativeFd;
    const char* path;
    if (!context || !portName) return false;
    switch (context->m_base.m_openMode & (XIODevice_ReadOnly | XIODevice_WriteOnly)) {
    case XIODevice_ReadOnly: flags |= O_RDONLY; break;
    case XIODevice_WriteOnly: flags |= O_WRONLY; break;
    default: flags |= O_RDWR; break;
    }
    if (context->m_base.m_flags & XDeviceOpenFlag_NonBlocking) flags |= O_NONBLOCK;
    /* [read 泵能力门] sunxi 老内核探测原语全坏，泵路径需要非阻塞 fd；
     * 其余平台保持原有 open 旗标一字不改。 */
    if (xserialReadPumpNeeded()) flags |= O_NONBLOCK;
    path = XString_toUtf8(portName);
    nativeFd = open(path ? path : "", flags);
    if (nativeFd < 0) return false;
    context->m_nativeFd = nativeFd;
    if (tcgetattr(nativeFd, &context->m_originalTermios) == 0) {
        context->m_haveOriginalTermios = true;
        if (!posixApplyConfig(context)) {
            close(nativeFd);
            context->m_nativeFd = -1;
            return false;
        }
    } else {
        close(nativeFd);
        context->m_nativeFd = -1;
        return false;
    }
    return true;
}

void XDeviceSerialPort_platformClose(XFd fd)
{
    XDeviceSerialPortPosixContext* context = posixContext(fd);
    if (!context) return;
    /* [可读事件源 2026-10-30] 先从框架等待集注销，再关 fd：等待集里留着
     * 一个即将失效的 fd，事件循环在其他 fd 就绪时读到它会拿到 EBADF/
     * 复用的 fd 号（与 fbdev 触摸通道同款纪律）。 */
    XDeviceSerialPort_platformDisarmReadable(fd);
    if (context->m_nativeFd >= 0) {
        if (context->m_haveOriginalTermios)
            tcsetattr(context->m_nativeFd, TCSANOW, &context->m_originalTermios);
        close(context->m_nativeFd);
        context->m_nativeFd = -1;
    }
}

/* ================================================================
 * 可读事件源（跨平台统一的等待集接入，2026-10-30）
 *
 * 串口 fd 注册进框架的异步后端等待集（XAbstractNetIoRing）：数据到达
 * ⇒ 内核唤醒阻塞中的事件循环 ⇒ 回调属主发射 readyRead。**不再靠上层
 * 毫秒级定时器轮询**（轮询拍只能在下一次「事件循环圈边界」才被发现，
 * 板端主循环一圈 100+ms 时，RX 感知延迟同量级）。
 *
 * POSIX 侧复用框架既有的「被动可读 fd」通道（与 fbdev 触摸唤醒通道
 * 同一套机制）：epoll 引擎直接 epoll_ctl(ADD)；io_uring 引擎走 POLL_ADD
 * （≥5.13 multishot / 5.10~5.12 一次性+重挂，见 XNetIoRingPosix.c）。
 * 两个引擎语义一致 ⇒ 跨设备（RK3568 / mcgs / 桌面 Linux）行为统一。
 *
 * 属主登记：框架的 XFd 表带 owner 字段（XFileDevice 的异步注册同款），
 * 被动 fd 回调只带**裸 OS fd**（与 XFd 编号空间不同）⇒ 回调按裸 fd
 * 反查 XFd 表里的 owner 上下文。
 * ================================================================ */
static void xserialPosixReadableNotify(int osFd)
{
    /* 被动 fd 回调只带裸 OS fd（与框架 XFd 是两套编号空间）⇒ 经框架 fd
     * 表反查属主：XFd 的 m_deviceCtx 存本平台上下文，其裸 OS fd 字段即
     * 回调参数 ⇒ 命中即取该上下文的可读回调（与 socket notifier 用
     * XFd.object 找 owner 同款思路，只是这里认的是平台 fd 字段）。 */
    int fd;
    for (fd = 0; fd < XFD_TABLE_SIZE; ++fd) {
        XDeviceSerialPortPosixContext* context;
        if (XFd_type(fd) != XFD_TYPE_CLASS) continue;
        context = (XDeviceSerialPortPosixContext*)XDevice_handle(fd);
        if (!context || context->m_nativeFd != osFd) continue;
        if (context->m_base.m_readableNotify)
            context->m_base.m_readableNotify(context->m_base.m_readableCtx);
        return;
    }
}

bool XDeviceSerialPort_platformArmReadable(XFd fd,
                                           void (*notify)(void* ctx),
                                           void* ctx)
{
    XDeviceSerialPortPosixContext* context = posixContext(fd);
    XAbstractNetIoRing* ring;
    if (!context || context->m_nativeFd < 0 || !notify) return false;
    ring = XAbstractNetIoRing_global();
    if (!ring) return false;
    if (!XAbstractNetIoRing_registerPassiveOsFd_base(
            ring, context->m_nativeFd, xserialPosixReadableNotify))
        return false;   /* 后端不支持被动 fd（io_uring 无 multishot 且一次性
                         * 形态也失败）⇒ 调用方保留定时器轮询兜底。 */
    context->m_base.m_readableNotify = notify;
    context->m_base.m_readableCtx = ctx;
    return true;
}

void XDeviceSerialPort_platformDisarmReadable(XFd fd)
{
    XDeviceSerialPortPosixContext* context = posixContext(fd);
    XAbstractNetIoRing* ring;
    if (!context || !context->m_base.m_readableNotify) return;
    ring = XAbstractNetIoRing_global();
    if (ring && context->m_nativeFd >= 0)
        XAbstractNetIoRing_unregisterPassiveOsFd_base(ring, context->m_nativeFd);
    context->m_base.m_readableNotify = NULL;
    context->m_base.m_readableCtx = NULL;
}

int64_t XDeviceSerialPort_platformRead(XFd fd, void* buffer, int64_t size)
{
    XDeviceSerialPortPosixContext* context = posixContext(fd);
    ssize_t result;
    if (!context || context->m_nativeFd < 0 || !buffer || size <= 0) return -1;
    if (context->m_base.m_readBuffer && XRingBuffer_available(context->m_base.m_readBuffer) > 0)
        return (int64_t)XRingBuffer_read(context->m_base.m_readBuffer, buffer, (size_t)size);
    result = read(context->m_nativeFd, buffer, (size_t)size);
    if (result >= 0) return (int64_t)result;
    if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR) return 0;
    return -1;
}

int64_t XDeviceSerialPort_platformWrite(XFd fd, const void* data, int64_t size)
{
    XDeviceSerialPortPosixContext* context = posixContext(fd);
    ssize_t result;
    if (!context || context->m_nativeFd < 0 || !data || size <= 0) return -1;
    result = write(context->m_nativeFd, data, (size_t)size);
    if (result >= 0) return (int64_t)result;
    if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR) return 0;
    return -1;
}

bool XDeviceSerialPort_platformFlush(XFd fd)
{
    XDeviceSerialPortPosixContext* context = posixContext(fd);
    return context && context->m_nativeFd >= 0 && tcdrain(context->m_nativeFd) == 0;
}

bool XDeviceSerialPort_platformSetProperty(XFd fd, uint32_t property, const XVariant* value)
{
    XDeviceSerialPortPosixContext* context = posixContext(fd);
    int modem = 0;
    bool enabled;
    if (!context || !value || context->m_nativeFd < 0) return false;
    switch (property) {
    case XDeviceSerialPortProperty_BaudRate:
    case XDeviceSerialPortProperty_DataBits:
    case XDeviceSerialPortProperty_Parity:
    case XDeviceSerialPortProperty_StopBits:
    case XDeviceSerialPortProperty_FlowControl:
        return posixApplyConfig(context);
    case XDeviceSerialPortProperty_ReadBufferSize:
        return XVariant_toInt64(value) > 0;
    case XDeviceSerialPortProperty_DataTerminalReady:
    case XDeviceSerialPortProperty_RequestToSend:
        enabled = readBool(value);
        if (ioctl(context->m_nativeFd, TIOCMGET, &modem) != 0) return false;
        if (property == XDeviceSerialPortProperty_DataTerminalReady) {
            if (enabled) modem |= TIOCM_DTR; else modem &= ~TIOCM_DTR;
        } else {
            if (enabled) modem |= TIOCM_RTS; else modem &= ~TIOCM_RTS;
        }
        return ioctl(context->m_nativeFd, TIOCMSET, &modem) == 0;
    case XDeviceSerialPortProperty_BreakEnabled:
        enabled = readBool(value);
        return ioctl(context->m_nativeFd, enabled ? TIOCSBRK : TIOCCBRK) == 0;
    default:
        return false;
    }
}

bool XDeviceSerialPort_platformGetProperty(XFd fd, uint32_t property, XVariant* value)
{
    XDeviceSerialPortPosixContext* context = posixContext(fd);
    int modem = 0;
    int available = 0;
    if (!context || !value || context->m_nativeFd < 0) return false;
    switch (property) {
    case XDeviceProperty_NativeHandle:
        XVariant_setValue_ptr(value, (void*)(intptr_t)context->m_nativeFd); return true;
    case XDeviceSerialPortProperty_BytesAvailable:
        if (xserialReadPumpNeeded()) {
            /* [mcgs 5.2.9 read 泵] 探测即泵：非阻塞 read 把内核 tty 缓冲
             * 搬进 m_readBuffer（platformRead 本就优先读 ring），不碰
             * FIONREAD/poll。单次泵上限 1KB×4 轮，防对端连发饿死主循环；
             * 余量留内核缓冲，下个 2ms 节拍续泵。 */
            char pumpBuf[256];
            ssize_t nRead;
            int round;
            for (round = 0; round < 4; ++round) {
                nRead = read(context->m_nativeFd, pumpBuf, sizeof(pumpBuf));
                if (nRead <= 0) break;
                if (context->m_base.m_readBuffer)
                    XRingBuffer_write(context->m_base.m_readBuffer,
                                      pumpBuf, (size_t)nRead);
            }
            available = context->m_base.m_readBuffer
                ? (int)XRingBuffer_available(context->m_base.m_readBuffer) : 0;
        } else {
            if (ioctl(context->m_nativeFd, FIONREAD, &available) != 0) available = 0;
        }
        XVariant_setValue_int64(value, available); return true;
    case XDeviceSerialPortProperty_PinoutSignals:
        if (ioctl(context->m_nativeFd, TIOCMGET, &modem) != 0) modem = 0;
        available = XSerialPort_NoSignal;
        if (modem & TIOCM_DTR) available |= XSerialPort_DataTerminalReadySignal;
        if (modem & TIOCM_CAR) available |= XSerialPort_DataCarrierDetectSignal;
        if (modem & TIOCM_DSR) available |= XSerialPort_DataSetReadySignal;
        if (modem & TIOCM_RI) available |= XSerialPort_RingIndicatorSignal;
        if (modem & TIOCM_RTS) available |= XSerialPort_RequestToSendSignal;
        if (modem & TIOCM_CTS) available |= XSerialPort_ClearToSendSignal;
        XVariant_setValue_int(value, available); return true;
    default:
        return false;
    }
}

bool XDeviceSerialPort_platformControl(XFd fd, uint32_t command,
                                       const XVarList* input, XVarList* output)
{
    XDeviceSerialPortPosixContext* context = posixContext(fd);
    XSerialPort_Direction directions;
    XVarList* arguments = (XVarList*)input;
    (void)output;
    if (!context || context->m_nativeFd < 0) return false;
    if (command == XDeviceSerialPortCommand_Clear) {
        if (!arguments || arguments->m_size != sizeof(directions)) return false;
        XVarList_start(arguments);
        directions = XVarList_arg(arguments, XSerialPort_Direction);
        return tcflush(context->m_nativeFd,
                       (directions & XSerialPort_AllDirections) == XSerialPort_AllDirections ? TCIOFLUSH :
                       (directions & XSerialPort_Input) ? TCIFLUSH : TCOFLUSH) == 0;
    }
    return command == XDeviceSerialPortCommand_HandleEvent ||
           command == XDeviceCommand_Cancel;
}

/* ==================================================================== */
/* 串口枚举：扫描 /sys/class/tty，解析 USB 设备属性                        */
/* ==================================================================== */

/** @brief 单次枚举最多上报的端口数。 */
#define POSIX_SERIAL_MAX_PORTS 64
/** @brief 端口名（/sys/class/tty 下的条目名）最大长度。 */
#define POSIX_SERIAL_MAX_NAME  64
/** @brief sysfs 属性文件的读取缓冲长度。 */
#define POSIX_SERIAL_ATTR_MAX  256

/**
 * @brief 读取整个属性文件并去掉尾部换行。
 * @param path 文件路径。
 * @param buffer 接收缓冲。
 * @param capacity 缓冲容量（字节）。
 * @return 读到的字符数（不含结尾 '\0'）；失败返回 -1。
 */
static int posixReadAttr(const char* path, char* buffer, size_t capacity)
{
    int fd;
    ssize_t count;
    if (!path || !buffer || capacity == 0) return -1;
    fd = open(path, O_RDONLY);
    if (fd < 0) return -1;
    count = read(fd, buffer, capacity - 1);
    close(fd);
    if (count < 0) return -1;
    buffer[count] = '\0';
    while (count > 0 && (buffer[count - 1] == '\n' || buffer[count - 1] == '\r'))
        buffer[--count] = '\0';
    return (int)count;
}

/**
 * @brief 从设备目录向上查找所属的 USB 设备目录（含 idVendor 的那级）。
 * @param deviceDir 设备的 device 符号链接解析后的绝对路径。
 * @param outDir 输出 USB 设备目录。
 * @param capacity 输出缓冲容量。
 * @return 找到返回 true。
 * @note USB 转串口设备的 tty 节点挂在 USB 接口之下，USB 设备属性（idVendor、
 *       manufacturer 等）位于再上一级，故需沿目录链向上回溯。
 */
static bool posixFindUsbDeviceDir(const char* deviceDir, char* outDir, size_t capacity)
{
    char current[PATH_MAX];
    int level;
    if (!deviceDir || !outDir || capacity == 0) return false;
    if (!realpath(deviceDir, current)) return false;
    for (level = 0; level < 8; ++level) {
        char probe[PATH_MAX];
        char* slash;
        snprintf(probe, sizeof(probe), "%s/idVendor", current);
        if (access(probe, R_OK) == 0) {
            strncpy(outDir, current, capacity - 1);
            outDir[capacity - 1] = '\0';
            return true;
        }
        slash = strrchr(current, '/');
        if (!slash || slash == current) break;
        *slash = '\0';
    }
    return false;
}

/**
 * @brief 探测端口是否已被占用。
 * @param devicePath 设备路径（/dev/xxx）。
 * @return 已被占用返回 true。
 * @note 探测方式是以 O_NONBLOCK 打开后立即关闭；少数 USB 转串口设备会在
 *       端口重新打开时复位，这是本实现的已知代价。
 */
static bool posixPortIsBusy(const char* devicePath)
{
    int fd = open(devicePath, O_RDWR | O_NONBLOCK | O_NOCTTY);
    bool busy = false;
    if (fd < 0) {
        busy = (errno == EBUSY || errno == EACCES);
    } else {
        close(fd);
    }
    return busy;
}

/**
 * @brief 判断某个 tty 是否为真实串口（排除虚拟控制台等纯软件设备）。
 * @param name /sys/class/tty 下的条目名。
 * @param deviceOut 设备的 device 符号链接解析结果。
 * @param capacity 输出缓冲容量。
 * @return 是真实串口返回 true。
 */
static bool posixIsRealSerial(const char* name, char* deviceOut, size_t capacity)
{
    char deviceLink[PATH_MAX];
    char resolved[PATH_MAX];
    snprintf(deviceLink, sizeof(deviceLink), "/sys/class/tty/%s/device", name);
    if (!realpath(deviceLink, resolved)) return false;
    /* 虚拟控制台、pty 等位于 /sys/devices/virtual 之下，不是串口。 */
    if (strncmp(resolved, "/sys/devices/virtual/", 21) == 0) return false;
    strncpy(deviceOut, resolved, capacity - 1);
    deviceOut[capacity - 1] = '\0';
    return true;
}

bool XDeviceSerialPort_platformEnumeratePorts(XDeviceSerialPortPortCollector collector, void* userData)
{
    DIR* dir;
    struct dirent* item;
    char names[POSIX_SERIAL_MAX_PORTS][POSIX_SERIAL_MAX_NAME];
    size_t nameCount = 0;
    size_t i;

    if (!collector) return false;
    dir = opendir("/sys/class/tty");
    if (!dir) return false;

    while ((item = readdir(dir)) != NULL) {
        char deviceDir[PATH_MAX];
        if (item->d_name[0] == '.') continue;
        if (nameCount >= POSIX_SERIAL_MAX_PORTS) break;
        if (strlen(item->d_name) >= POSIX_SERIAL_MAX_NAME) continue;
        /* 真串口判定前移到收集阶段：names 是固定容量的数组（上限
           POSIX_SERIAL_MAX_PORTS），而 /sys/class/tty 下除真 UART 外还有
           数十个虚拟控制台 ttyN/console/ptmx。先无差别收集再于下方
           过滤时，数组会被虚拟项占满，真正的 UART（ttySx）根本进不来
           ——真机 2026-10-26 实证：板上有 74 项（64 个 ttyN），枚举结果
           只剩 /dev/ttyFIQ0，RS485 的 ttyS3/4/5/7/8/9 全丢。此处先过
           滤再入表，数组只装真串口。下方循环内的判定保留（幂等）。 */
        if (!posixIsRealSerial(item->d_name, deviceDir, sizeof(deviceDir)))
            continue;
        strncpy(names[nameCount], item->d_name, POSIX_SERIAL_MAX_NAME - 1);
        names[nameCount][POSIX_SERIAL_MAX_NAME - 1] = '\0';
        ++nameCount;
    }
    closedir(dir);

    /* readdir 顺序不确定，按名字排序保证输出稳定。 */
    for (i = 1; i < nameCount; ++i) {
        char key[POSIX_SERIAL_MAX_NAME];
        size_t j = i;
        strncpy(key, names[i], sizeof(key));
        while (j > 0 && strcmp(names[j - 1], key) > 0) {
            strncpy(names[j], names[j - 1], POSIX_SERIAL_MAX_NAME);
            names[j][POSIX_SERIAL_MAX_NAME - 1] = '\0';
            --j;
        }
        strncpy(names[j], key, POSIX_SERIAL_MAX_NAME);
        names[j][POSIX_SERIAL_MAX_NAME - 1] = '\0';
    }

    for (i = 0; i < nameCount; ++i) {
        char devicePath[PATH_MAX];
        char deviceDir[PATH_MAX];
        char usbDir[PATH_MAX];
        char portName[PATH_MAX];
        char description[POSIX_SERIAL_ATTR_MAX];
        char manufacturer[POSIX_SERIAL_ATTR_MAX];
        char serialNumber[POSIX_SERIAL_ATTR_MAX];
        char vendorId[32];
        char productId[32];
        XDeviceSerialPortPortInfo info;

        if (!posixIsRealSerial(names[i], deviceDir, sizeof(deviceDir))) continue;

        snprintf(portName, sizeof(portName), "/dev/%s", names[i]);
        snprintf(devicePath, sizeof(devicePath), "%s", portName);

        description[0] = '\0';
        manufacturer[0] = '\0';
        serialNumber[0] = '\0';
        vendorId[0] = '\0';
        productId[0] = '\0';

        if (posixFindUsbDeviceDir(deviceDir, usbDir, sizeof(usbDir))) {
            char attributePath[PATH_MAX];
            snprintf(attributePath, sizeof(attributePath), "%s/idVendor", usbDir);
            posixReadAttr(attributePath, vendorId, sizeof(vendorId));
            snprintf(attributePath, sizeof(attributePath), "%s/idProduct", usbDir);
            posixReadAttr(attributePath, productId, sizeof(productId));
            snprintf(attributePath, sizeof(attributePath), "%s/manufacturer", usbDir);
            posixReadAttr(attributePath, manufacturer, sizeof(manufacturer));
            snprintf(attributePath, sizeof(attributePath), "%s/product", usbDir);
            posixReadAttr(attributePath, description, sizeof(description));
            snprintf(attributePath, sizeof(attributePath), "%s/serial", usbDir);
            posixReadAttr(attributePath, serialNumber, sizeof(serialNumber));
        }

        memset(&info, 0, sizeof(info));
        info.m_portName = portName;
        info.m_description = description[0] ? description : NULL;
        info.m_manufacturer = manufacturer[0] ? manufacturer : NULL;
        info.m_serialNumber = serialNumber[0] ? serialNumber : NULL;
        info.m_vendorId = (uint32_t)strtoul(vendorId, NULL, 16);
        info.m_productId = (uint32_t)strtoul(productId, NULL, 16);
        info.m_busy = posixPortIsBusy(devicePath);

        if (!collector(userData, &info)) {
            return false;
        }
    }
    return true;
}

#endif
