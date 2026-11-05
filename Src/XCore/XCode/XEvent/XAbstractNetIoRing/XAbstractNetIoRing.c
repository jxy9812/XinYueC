// XAbstractNetIoRing.c
//
// 基类实现：提供 class_init、9 个虚函数的默认实现（平台无关）、
// SQ/CQ 队列管理、全局单例、虚函数调度入口。
// 子类（XNetIoRingWin32 等）通过继承本类，仅重载平台特定的虚函数。
//
// 虚函数调度入口 + 平台默认实现。
#include "XAbstractNetIoRing.h"
/* 以下三个头供守卫之外的唤醒延迟探针使用（见文件末的说明），必须留在环开关
   门控之外；否则 XAbstractNetIoRing_ON=0 时探针仍在编译却看不到声明。 */
#include "XFileDescriptor.h"
#include "XDateTime.h"
#include <stdlib.h>
#if XAbstractNetIoRing_ON
#include "XMemory.h"
#include "XEvent.h"
#include "XCoreApplication.h"
#include "XDevice.h"
#include <string.h>
#include <stdio.h>
#include <errno.h>
#if !defined(_WIN32) && !defined(__ESP8266__)
/* POSIX 专用：MSG_DONTWAIT/MSG_PEEK 取自 sys/socket.h；
   Windows 侧走 winsock2.h（仅 MSG_PEEK），假 EOF 规避段另有平台闸。
   ESP8266（NONOS SDK）无 BSD socket 头，同样排除。 */
#include <sys/socket.h>
#endif
/* 单调时长统一经 XDateTime（约束文档「外部依赖约束」时间源规则：
 * Src/ 禁止直呼 QueryPerformanceCounter/clock_gettime 等平台时间源）。 */
#include "XDateTime.h"




static bool xabstractnetioring_is_socket_fd(const XAbstractNetIoRing_CQEntry* entry)
{
    XDevice* device;
    if (!entry) return false;
    if (entry->m_fdType == XFD_TYPE_SOCKET) return true;
    if (entry->m_fdType != XFD_TYPE_CLASS) return false;
    device = XDevice_class(entry->m_fd);
    return device && device->m_type == XDeviceType_Socket;
}

/* ================================================================
 * 全局单例
 * ================================================================ */
static XAbstractNetIoRing* g_globalRing = NULL;

XAbstractNetIoRing* XAbstractNetIoRing_global(void) {
    return g_globalRing;
}

void XAbstractNetIoRing_setGlobal(XAbstractNetIoRing* ring) {
    g_globalRing = ring;
}

/* ================================================================
 * 默认虚函数实现（平台无关）
 *
 * 子类只需重载平台特定的虚函数，其余沿用此处默认实现。
 * ================================================================ */

/* 前向声明 */
static XFd   VXAbstractNetIoRing_getEventFd(XAbstractNetIoRing* self);
static void  VXAbstractNetIoRing_processSource(XAbstractNetIoRing* self,
                                                const XAbstractNetIoRing_SQEntry* entry);
static void  VXAbstractNetIoRing_pollPlatform(XAbstractNetIoRing* self);
static bool  VXAbstractNetIoRing_hasPendingInput(const XAbstractNetIoRing* self);
static bool  VXAbstractNetIoRing_registerEvent(XAbstractNetIoRing* self, XFd fd);
static bool  VXAbstractNetIoRing_unregisterEvent(XAbstractNetIoRing* self, XFd fd);
static void  VXAbstractNetIoRing_dispatchCQEntry(XAbstractNetIoRing* self,
                                                  const XAbstractNetIoRing_CQEntry* entry);
static void  VXAbstractNetIoRing_waitForEvents(XAbstractNetIoRing* self, int timeoutMs);
static void  VXAbstractNetIoRing_wakeUp(XAbstractNetIoRing* self);
static void  VXAbstractNetIoRing_deinit(XAbstractNetIoRing* obj);

/* 获取平台事件 fd：默认无单一事件源，返回 XFD_INVALID。
 * 子类（如 XNetIoRingWin32）重载为返回 IOCP 端口句柄。 */
static XFd VXAbstractNetIoRing_getEventFd(XAbstractNetIoRing* self) {
    (void)self;
    return XFD_INVALID;
}

/* 处理一条 SQ 条目：默认空操作。
 * lwIP 模式下事件由协议栈回调驱动，不使用 SQ；
 * Win32 模式下完成已在 pollPlatform 中直接推入 CQ。 */
static void VXAbstractNetIoRing_processSource(XAbstractNetIoRing* self,
                                                const XAbstractNetIoRing_SQEntry* entry) {
    (void)self;
    (void)entry;
}

/* 轮询平台 I/O：默认空操作。
 * 子类（如 XNetIoRingWin32）重载为轮询 IOCP 完成端口。
 * lwIP 模式下 pcap 轮询由 XAbstractNetIoRing_pollLwip() 单独完成。 */
static void VXAbstractNetIoRing_pollPlatform(XAbstractNetIoRing* self) {
    (void)self;
}

/* 检查是否有待处理输入：检查 SQ + CQ 是否有待处理条目。 */
static bool VXAbstractNetIoRing_hasPendingInput(const XAbstractNetIoRing* self) {
    if (!self) return false;
    if (self->m_sq && !XLockFreeQueue_isEmpty_base(self->m_sq))
        return true;
    if (self->m_cq && !XLockFreeQueue_isEmpty_base(self->m_cq))
        return true;
    return false;
}

/* 注册事件源：默认允许（返回 true）。
 * 子类（如 XNetIoRingWin32）重载为关联 HANDLE 到 IOCP 端口。 */
static bool VXAbstractNetIoRing_registerEvent(XAbstractNetIoRing* self, XFd fd) {
    (void)self;
    (void)fd;
    return true;
}

/* 注销事件源：默认允许（返回 true）。
 * 子类（如 XNetIoRingWin32）重载时 IOCP 不支持显式注销，关闭 HANDLE 即可。 */
static bool VXAbstractNetIoRing_unregisterEvent(XAbstractNetIoRing* self, XFd fd) {
    (void)self;
    (void)fd;
    return true;
}

/* 注册裸 OS fd 为被动可读事件源：默认不支持（返回 false）。
 *
 * 基类无从得知后端等待集的实现方式（IOCP 无「被动 fd」等价物、epoll 可
 * epoll_ctl(ADD)、io_uring 需 POLL_ADD），故默认不实现——调用方（fbdev
 * 触摸泵等）据此判定「注册失败 → 保留原有轮询回调链兜底」，行为退化为
 * 本次改造前的口径，不产生功能回退。 */
static bool VXAbstractNetIoRing_registerPassiveOsFd(XAbstractNetIoRing* self,
                                                     int osFd,
                                                     void (*notify)(int osFd)) {
    (void)self;
    (void)osFd;
    (void)notify;
    return false;
}

/* 注销裸 OS fd 被动事件源：默认幂等成功（本就未注册亦视为成功）。
 * 属主关闭 fd 前调用，确保等待集里不留失效 fd。 */
static bool VXAbstractNetIoRing_unregisterPassiveOsFd(XAbstractNetIoRing* self,
                                                       int osFd) {
    (void)self;
    (void)osFd;
    return true;
}

/* 分发一条 CQ 完成事件到应用层。
 *
 * 从 CQ 条目提取 fd 和事件掩码，通过 XFd_Get(fd)->object 获取 Socket 的
 * owner（XObject*），创建 XEventSockAct / XEventSockClose 并通过
 * XCoreApplication_postEvent 投递到应用层。
 *
 * 对端关闭检测（原生 I/O）：
 *   0 字节 + Read 事件 + 来源为 NativeIO => XEventSockClose（对端 FIN）。
 *   lwIP 来源的事件不触发此检测（来源为 Source_Netif，带明确事件掩码）。 */
static void VXAbstractNetIoRing_dispatchCQEntry(XAbstractNetIoRing* self,
                                                  const XAbstractNetIoRing_CQEntry* entry) {
    XFileDescriptor* desc;
    XObject* owner;

    (void)self;
    if (!entry) return;

    /* 通过 fd 查找 owner。
     * entry->m_fdType 可用于类型感知的分发（如区分 Socket/File/Timer），
     * 当前统一走 Socket 事件路径。 */
    desc = XFd_get(entry->m_fd);
    if (!desc || !desc->object) return;
    if ((XFdType)entry->m_fdType != (XFdType)desc->m_type) return;
    owner = (XObject*)desc->object;

    /* 原生套接字对端关闭检测：0 字节 + Read + NativeIO 来源 => 关闭事件。
     * 串口的异步读可以在超时或取消时以 0 字节完成，不表示连接关闭。 */
    if (entry->m_bytes == 0 &&
        (entry->m_events & XSocketActType_Read) &&
        entry->m_sourceType == XAbstractNetIoRing_Source_NativeIO &&
        xabstractnetioring_is_socket_fd(entry)) {
        /* [2026-10-16 假 EOF 复核] 昆仑通态 A33(5.2.9 定制内核)实测:
         * 新建 connect 型 socket 上读到 0 字节完成(连接后 0~350ms 内),
         * 但对端健在(同机裸 socket 连接+闲置 2s+收发全通), 按对端 FIN
         * 处理即误断——Modbus 主站「连上即断线重连」循环、XUpgrade 引擎
         * HTTP 超时(wget 却通)同征。MSG_PEEK 复核: EAGAIN=连接活着
         * (内核边缘事件语义误报, 丢弃 CLOSE、完成事件照常下发, 由设备
         * 层重挂读); pr==0 才是真 FIN(照旧发关闭)。与桌面/其他平台
         * 真关闭语义零变化(真关闭时 PEEK 恒返 0)。
         *
         * [2026-11-04 Windows 同征] 原实现只在 POSIX 生效、Windows 保持
         * "0 字节即关闭"。本机 IOCP 环回实测推翻该前提：服务端 socket
         * 在链路健在时被判 Unconnected（客户端全程未断），导致会话被
         * 复位、订阅全丢、推送积压。现改为**平台钩子统一裁决**：
         * `XAbstractNetIoRing_platformPeerReallyClosed(osFd)` —— 各平台
         * 在自己的 Drive 层用本平台可为的 PEEK 手段实现同一判定口径，
         * Src/ 不再出现平台分支（平台差异只允许下沉到 Drive）。 */
        /* PEEK 必须用引擎随 CQ 条目带来的真实 OS fd（m_osFd），不能用
         * entry->m_fd（XFd 槽位号）——两者编号空间不同，历史上因此把一切
         * 真 FIN 误判成假 EOF（epoll 引擎 xmqtt-tcp-api 断开三例实证）。
         * m_osFd<=0（数据报 0 长报文/引擎未填/上下文缺失）＝不适用关闭
         * 判定：按普通 0 字节读完成下发，由设备层既有复核路径决定重挂读。
         * [2026-11-04] 判据从 `<0` 收紧为 `<=0`：Win32 后端历史上从不填
         * 该字段（memset 恒 0），用 `<0` 会让 PEEK 打到 **fd 0（stdin）**
         * ——stdin 读到 0 ⇒ 一切 0 字节完成被当成真 FIN，健康链路被误断。
         * 合法套接字句柄在两端都恒为正（POSIX fd>2，Win32 SOCKET 非 0）。 */
        if (entry->m_osFd <= 0)
            return;
        if (!XAbstractNetIoRing_platformPeerReallyClosed((int)entry->m_osFd))
            return; /* 假 EOF：丢弃 CLOSE，完成事件走常规路径下发。 */
        XEventSockClose* closeEvent = XEventSockClose_create(entry->m_fd);
        if (closeEvent) {
            ((XEvent*)closeEvent)->m_posted = true;
            ((XEvent*)closeEvent)->m_spontaneous = true;
            XCoreApplication_postEvent(owner, (XEvent*)closeEvent,
                                       XEVENT_PRIORITY_NORMAL);
        }
        return;
    }

    /* 常规事件：创建 XEventSockAct 投递到应用层 */
    if (entry->m_events != 0) {
        XEventSockAct* ev = XEventSockAct_create(entry->m_fd,
                                                   (XSocketActType)entry->m_events);
        if (ev) {
            ((XEvent*)ev)->m_posted = true;
            ((XEvent*)ev)->m_spontaneous = true;
            XCoreApplication_postEvent(owner, (XEvent*)ev,
                                       XEVENT_PRIORITY_NORMAL);
        }
    }
}

/* 阻塞等待事件：默认空操作（裸机 / 单线程不阻塞）。
 * 子类（如 XNetIoRingWin32）重载为 GetQueuedCompletionStatus(timeout)。 */
static void VXAbstractNetIoRing_waitForEvents(XAbstractNetIoRing* self, int timeoutMs) {
    (void)self;
    (void)timeoutMs;
}

/* 唤醒阻塞中的 WaitForEvents：默认空操作（裸机单线程无需唤醒）。
 * 子类（如 XNetIoRingWin32）重载为 PostQueuedCompletionStatus。 */
static void VXAbstractNetIoRing_wakeUp(XAbstractNetIoRing* self) {
    (void)self;
}

/* 析构函数：清理 SQ/CQ 队列 + 调用 XClass 基类 deinit。
 * 子类 deinit 应先清理自身资源，再调用
 * XClass_Deinit_Parent(XAbstractNetIoRing, obj) 触发本函数。 */
static void VXAbstractNetIoRing_deinit(XAbstractNetIoRing* obj) {
    if (!obj) return;

    /* 清理 SQ/CQ 队列 */
    XAbstractNetIoRing_cleanupQueues(obj);

    /* 调用 XClass 基类 deinit */
    XClass_Deinit_Parent(XClass, (XClass*)obj);
}

/* ================================================================
 * 虚函数表初始化
 *
 * 继承 XClass 虚函数表（Copy/Move/Deinit），添加 9 个默认虚函数实现，
 * 重载 Deinit 为基类析构（清理 SQ/CQ + XClass deinit）。
 * 子类 class_init 通过 XVTABLE_INHERIT_XCLASS(XAbstractNetIoRing) 继承
 * 此表，再用 XVTABLE_OVERLOAD_DEFAULT 重载平台特定虚函数。
 * ================================================================ */
XVtable* XAbstractNetIoRing_class_init(void) {
    XVTABLE_INIT_DEFAULT(XAbstractNetIoRing)
    /* 继承 XClass 虚函数表（Copy/Move/Deinit） */
    XVTABLE_INHERIT_XCLASS(XClass);

    /* 添加 9 个默认虚函数实现 */
    void* table[] = {
        VXAbstractNetIoRing_getEventFd,
        VXAbstractNetIoRing_processSource,
        VXAbstractNetIoRing_pollPlatform,
        VXAbstractNetIoRing_hasPendingInput,
        VXAbstractNetIoRing_registerEvent,
        VXAbstractNetIoRing_unregisterEvent,
        VXAbstractNetIoRing_dispatchCQEntry,
        VXAbstractNetIoRing_waitForEvents,
        VXAbstractNetIoRing_wakeUp,
        /* 追加项（2026-10-26）：裸 OS fd 被动注册通道。追加在末尾，
         * 既有 9 项索引不变（ABI 面零破坏）。 */
        VXAbstractNetIoRing_registerPassiveOsFd,
        VXAbstractNetIoRing_unregisterPassiveOsFd
    };
    XVTABLE_ADD_FUNC_LIST_DEFAULT(table);

    /* 重载 deinit */
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Deinit, VXAbstractNetIoRing_deinit);

    return XVTABLE_DEFAULT;
}

/* ================================================================
 * 构造与析构
 * ================================================================ */

void XAbstractNetIoRing_init(XAbstractNetIoRing* ring) {
    if (!ring) return;

    /* 清零基类部分（子类应已先 memset 清零整个子类结构体） */
    memset(ring, 0, sizeof(XAbstractNetIoRing));

    /* 初始化 XClass 基类（设置 m_vtable 和默认 m_memory） */
    XClass_init(ring);

    /* 设置基类虚函数表（含默认实现），子类可在调用本函数后自行重载 */
    XClassSetVtable(ring, XAbstractNetIoRing);

    /* 创建 SQ/CQ 无锁队列 */
    ring->m_sq = XLockFreeQueue_create(sizeof(XAbstractNetIoRing_SQEntry), XNETIORING_SQ_CAPACITY);
    ring->m_cq = XLockFreeQueue_create(sizeof(XAbstractNetIoRing_CQEntry), XNETIORING_CQ_CAPACITY);
    ring->m_eventFd = XFD_INVALID;
    ring->m_enabled = false;
}

void XAbstractNetIoRing_cleanupQueues(XAbstractNetIoRing* ring) {
    if (!ring) return;

    /* 释放 SQ/CQ 无锁队列 */
    if (ring->m_sq) {
        XClassDelete(ring->m_sq);
        ring->m_sq = NULL;
    }
    if (ring->m_cq) {
        XClassDelete(ring->m_cq);
        ring->m_cq = NULL;
    }
    ring->m_eventFd = XFD_INVALID;
    ring->m_enabled = false;
}

XAbstractNetIoRing* XAbstractNetIoRing_create_ex(XMemoryType memory) {
    XAbstractNetIoRing* ring = (XAbstractNetIoRing*)XMemory_malloc(sizeof(XAbstractNetIoRing), memory);
    if (!ring) return NULL;

    XAbstractNetIoRing_init(ring);
    Set_Class_Memory(ring, memory); Set_Class_IsHeap(ring, true);
    return ring;
}

/* ================================================================
 * 核心功能（非虚函数）
 * ================================================================ */

bool XAbstractNetIoRing_pushSQ(XAbstractNetIoRing* ring, const XAbstractNetIoRing_SQEntry* entry) {
    if (ISNULL(ring, "XAbstractNetIoRing") || ISNULL(entry, "SQEntry") || !ring->m_sq)
        return false;
    return XLockFreeQueue_push_base(ring->m_sq, (void*)entry);
}

bool XAbstractNetIoRing_pushCompletion(XAbstractNetIoRing* ring, const XAbstractNetIoRing_CQEntry* entry) {
    if (ISNULL(ring, "XAbstractNetIoRing") || ISNULL(entry, "CQEntry") || !ring->m_cq)
        return false;
    return XLockFreeQueue_push_base(ring->m_cq, (void*)entry);
}

void XAbstractNetIoRing_drainCQ(XAbstractNetIoRing* ring) {
    XAbstractNetIoRing_CQEntry entry;

    if (ISNULL(ring, "XAbstractNetIoRing") || !ring->m_cq)
        return;

    /* 逐条取出 CQ 条目，通过虚函数分发到应用层 */
    while (XLockFreeQueue_receive_base(ring->m_cq, &entry)) {
        XAbstractNetIoRing_dispatchCQEntry_base(ring, &entry);
    }
}

void XAbstractNetIoRing_processReady(XAbstractNetIoRing* ring) {
    XAbstractNetIoRing_SQEntry sqEntry;

    if (ISNULL(ring, "XAbstractNetIoRing"))
        return;

    /* 1. 轮询平台 I/O（IOCP/epoll），完成事件推入 CQ */
    XAbstractNetIoRing_pollPlatform_base(ring);

    /* 2. 排空 SQ，对每条调用 ProcessSource 虚函数 */
    while (XLockFreeQueue_receive_base(ring->m_sq, &sqEntry)) {
        XAbstractNetIoRing_processSource_base(ring, &sqEntry);
    }

    /* 3. 排空 CQ，通过 DispatchCQEntry 虚函数投递完成事件到应用层 */
    XAbstractNetIoRing_drainCQ(ring);
}

bool XAbstractNetIoRing_hasSQPending(const XAbstractNetIoRing* ring) {
    if (ISNULL(ring, "XAbstractNetIoRing") || !ring->m_sq)
        return false;
    return !XLockFreeQueue_isEmpty_base(ring->m_sq);
}

bool XAbstractNetIoRing_hasCQPending(const XAbstractNetIoRing* ring) {
    if (ISNULL(ring, "XAbstractNetIoRing") || !ring->m_cq)
        return false;
    return !XLockFreeQueue_isEmpty_base(ring->m_cq);
}

bool XAbstractNetIoRing_isEnabled(const XAbstractNetIoRing* ring) {
    return ring ? ring->m_enabled : false;
}

/* ================================================================
 * 虚函数调度入口（通过 vtable 分发到平台后端实现）
 * ================================================================ */
XFd XAbstractNetIoRing_getEventFd_base(XAbstractNetIoRing* ring) {
    if (ISNULL(ring, "XAbstractNetIoRing") || ISNULL(XClassGetVtable(ring), "Vtable"))
        return XFD_INVALID;
    return XClassGetVirtualFunc(ring, EXAbstractNetIoRing_GetEventFd, XFd(*)(XAbstractNetIoRing*))(ring);
}

void XAbstractNetIoRing_processSource_base(XAbstractNetIoRing* ring, const XAbstractNetIoRing_SQEntry* entry) {
    if (ISNULL(ring, "XAbstractNetIoRing") || ISNULL(XClassGetVtable(ring), "Vtable") || !entry)
        return;
    XClassGetVirtualFunc(ring, EXAbstractNetIoRing_ProcessSource,
        void(*)(XAbstractNetIoRing*, const XAbstractNetIoRing_SQEntry*))(ring, entry);
}

void XAbstractNetIoRing_pollPlatform_base(XAbstractNetIoRing* ring) {
    if (ISNULL(ring, "XAbstractNetIoRing") || ISNULL(XClassGetVtable(ring), "Vtable"))
        return;
    XClassGetVirtualFunc(ring, EXAbstractNetIoRing_PollPlatform, void(*)(XAbstractNetIoRing*))(ring);
}

bool XAbstractNetIoRing_hasPendingInput_base(const XAbstractNetIoRing* ring) {
    if (ISNULL(ring, "XAbstractNetIoRing") || ISNULL(XClassGetVtable(ring), "Vtable"))
        return false;
    return XClassGetVirtualFunc(ring, EXAbstractNetIoRing_HasPendingInput, bool(*)(const XAbstractNetIoRing*))(ring);
}

bool XAbstractNetIoRing_registerEvent_base(XAbstractNetIoRing* ring, XFd fd) {
    if (ISNULL(ring, "XAbstractNetIoRing") || ISNULL(XClassGetVtable(ring), "Vtable"))
        return false;
    return XClassGetVirtualFunc(ring, EXAbstractNetIoRing_RegisterEvent, bool(*)(XAbstractNetIoRing*, XFd))(ring, fd);
}

bool XAbstractNetIoRing_unregisterEvent_base(XAbstractNetIoRing* ring, XFd fd) {
    if (ISNULL(ring, "XAbstractNetIoRing") || ISNULL(XClassGetVtable(ring), "Vtable"))
        return false;
    return XClassGetVirtualFunc(ring, EXAbstractNetIoRing_UnregisterEvent, bool(*)(XAbstractNetIoRing*, XFd))(ring, fd);
}

void XAbstractNetIoRing_dispatchCQEntry_base(XAbstractNetIoRing* ring, const XAbstractNetIoRing_CQEntry* entry) {
    if (ISNULL(ring, "XAbstractNetIoRing") || ISNULL(XClassGetVtable(ring), "Vtable") || !entry)
        return;
    XClassGetVirtualFunc(ring, EXAbstractNetIoRing_DispatchCQEntry,
        void(*)(XAbstractNetIoRing*, const XAbstractNetIoRing_CQEntry*))(ring, entry);
}

void XAbstractNetIoRing_waitForEvents_base(XAbstractNetIoRing* ring, int timeoutMs) {
    if (ISNULL(ring, "XAbstractNetIoRing") || ISNULL(XClassGetVtable(ring), "Vtable"))
        return;
    XClassGetVirtualFunc(ring, EXAbstractNetIoRing_WaitForEvents,
        void(*)(XAbstractNetIoRing*, int))(ring, timeoutMs);
}

bool XAbstractNetIoRing_registerPassiveOsFd_base(XAbstractNetIoRing* ring,
                                                  int osFd,
                                                  void (*notify)(int osFd)) {
    if (ISNULL(ring, "XAbstractNetIoRing") || ISNULL(XClassGetVtable(ring), "Vtable"))
        return false;
    return XClassGetVirtualFunc(ring, EXAbstractNetIoRing_RegisterPassiveOsFd,
        bool(*)(XAbstractNetIoRing*, int, void(*)(int)))(ring, osFd, notify);
}

bool XAbstractNetIoRing_unregisterPassiveOsFd_base(XAbstractNetIoRing* ring,
                                                    int osFd) {
    if (ISNULL(ring, "XAbstractNetIoRing") || ISNULL(XClassGetVtable(ring), "Vtable"))
        return false;
    return XClassGetVirtualFunc(ring, EXAbstractNetIoRing_UnregisterPassiveOsFd,
        bool(*)(XAbstractNetIoRing*, int))(ring, osFd);
}

void XAbstractNetIoRing_wakeUp_base(XAbstractNetIoRing* ring) {
    if (ISNULL(ring, "XAbstractNetIoRing") || ISNULL(XClassGetVtable(ring), "Vtable"))
        return;
    XClassGetVirtualFunc(ring, EXAbstractNetIoRing_WakeUp,
        void(*)(XAbstractNetIoRing*))(ring);
}

/* ================================================================
 * 平台钩子：创建后端
 *
 * 在无 IOCP/epoll 的平台（裸机 / 嵌入式 / Linux-lwIP）上，
 * 直接创建基类实例（使用默认虚函数实现）。
 * Windows 平台由 XNetIoRingWin32.c 提供 createPlatform（IOCP 后端）。
 * ================================================================ */
#if !XPLATFORM_HAS_OS
XAbstractNetIoRing* XAbstractNetIoRing_createPlatform(void) {
    return XAbstractNetIoRing_create();
}
#endif


#endif /* XAbstractNetIoRing_ON */

/* ================================================================
 * 事件循环唤醒延迟探针（诊断, env XGUI_REMOTE_WAKE_PROF 门控）
 *
 * 逐 fd 登记最近一次读完成时刻；消息层经 profLastRecvUs 查询并与派发
 * 完成时刻作差, 得逐消息「数据到达→派发」响应延迟。探针未开时全程
 * 零写入零查询成本（g_profOn 缓存 getenv, 每进程仅探测一次）。
 * 置于环开关门控(XAbstractNetIoRing_ON)之外：环关闭时无打点发生,
 * 查询恒 0, 调用方(XGuiServer/XGuiClient/XGuiRemoteUdpChannel, 不随
 * 环开关裁剪)仍可安全链接。
 * ================================================================ */
/* [2026-10-18 嵌入式防呆] #ifndef：小内存目标可收缩剖析槽位。 */
#ifndef XNETIORING_PROF_FD_SLOTS
#define XNETIORING_PROF_FD_SLOTS 64
#endif
typedef struct XNetIoRingProfSlot {
    const void* owner;   /**< 套接字属主对象(XFd 表 desc->object; NULL 空槽)。 */
    uint64_t lastRecvUs; /**< 最近一次 >0 字节读完成时刻（单调 µs）。 */
} XNetIoRingProfSlot;
static XNetIoRingProfSlot g_profSlots[XNETIORING_PROF_FD_SLOTS];
static int g_profOn = -1; /* -1=未探测环境变量。 */

/** @brief 探针开关（env 探测一次并缓存）。 */
static int xnetioring_profEnabled(void) {
    if (g_profOn < 0) {
        const char* env = getenv("XGUI_REMOTE_WAKE_PROF");
        g_profOn = (env && env[0] && env[0] != '0') ? 1 : 0;
    }
    return g_profOn;
}

uint64_t XAbstractNetIoRing_profNowUs(void) {
    /* [2026-11-05] 改用纳秒轴：currentNSecsSinceEpoch 自本次改动起已由单调源
     * 推进（不再是墙钟，见 XDateTime.h 的口径注记），并且与 ms 轴同锚，
     * ns/1e6 == ms 严格成立——因此这里既得到 µs 级分辨率，又不与全库其余
     * 计时点错轴。 */
    return (uint64_t)XDateTime_currentNSecsSinceEpoch() / 1000ULL;
}

void XAbstractNetIoRing_profMarkRecv(int fd) {
    int i;
    XFileDescriptor* desc;
    const void* owner;
    if (!xnetioring_profEnabled() || fd < 0) return;
    /* 键=属主对象指针(XFd 表 desc->object): 消息层持有的是设备/套接字
     * 对象指针, XFd id 是池索引与原生 fd 无关, 不宜外泄作查询键。 */
    desc = XFd_get(fd);
    owner = desc ? desc->object : NULL;
    if (!owner) return;
    for (i = 0; i < XNETIORING_PROF_FD_SLOTS; ++i) {
        if (g_profSlots[i].owner == owner) {
            g_profSlots[i].lastRecvUs = XAbstractNetIoRing_profNowUs();
            return;
        }
    }
    /* 未登记对象：占首个空槽（槽不主动回收——探针为诊断设施, 对象销毁后
     * 残留键不再匹配新查询; 64 槽对本模块并发套接字规模充分）。 */
    for (i = 0; i < XNETIORING_PROF_FD_SLOTS; ++i) {
        if (!g_profSlots[i].owner) {
            g_profSlots[i].owner = owner;
            g_profSlots[i].lastRecvUs = XAbstractNetIoRing_profNowUs();
            return;
        }
    }
}

uint64_t XAbstractNetIoRing_profLastRecvUs(const void* ownerObj) {
    int i;
    if (!xnetioring_profEnabled() || !ownerObj) return 0;
    for (i = 0; i < XNETIORING_PROF_FD_SLOTS; ++i)
        if (g_profSlots[i].owner == ownerObj)
            return g_profSlots[i].lastRecvUs;
    return 0;
}
