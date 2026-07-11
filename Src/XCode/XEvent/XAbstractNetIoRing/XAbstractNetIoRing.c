// XAbstractNetIoRing.c
//
// 基类实现：提供 class_init、9 个虚函数的默认实现（平台无关）、
// SQ/CQ 队列管理、全局单例、lwIP pcap 轮询、虚函数调度入口。
// 子类（XNetIoRingWin32 等）通过继承本类，仅重载平台特定的虚函数。
//
// 原 XNetIoRing_lwip.c 内容已合并至此（pollLwip + 默认虚函数实现）。
#include "XAbstractNetIoRing.h"
#if XAbstractNetIoRing_ON
#include "XNetwork_config.h"   /* XNETWORK_USE_LWIP 宏定义 */
#include "XMemory.h"
#include "XEvent.h"
#include "XCoreApplication.h"
#include "XFileDescriptor.h"
#include <string.h>
#include <stdlib.h>

/* ================================================================
 * lwIP pcap 轮询（通用实现，平台无关）
 *
 * 原 XNetIoRing_lwip.c 内容合并至此。
 * 仅在 XNETWORK_USE_LWIP 模式下编译。
 * ================================================================ */
#ifdef XNETWORK_USE_LWIP
#include "XNetwork_lwip_platform.h"
#include "lwip/opt.h"     /* NO_SYS 宏定义 */
#include "lwip/sys.h"     /* sys_prot_t, sys_arch_protect/unprotect */

/* 核心锁：与 XNetwork_lwip.c 的 XNET_LWIP_LOCK 保持一致
 *   NO_SYS=1 + SYS_LIGHTWEIGHT_PROT=1: sys_arch_protect（递归锁）
 *   NO_SYS=1 + SYS_LIGHTWEIGHT_PROT=0: 单线程，锁为空操作（零开销）
 *   NO_SYS=0: tcpip_thread 处理锁，pollLwip 无需额外锁 */
#if NO_SYS && SYS_LIGHTWEIGHT_PROT
extern sys_prot_t sys_arch_protect(void);
extern void sys_arch_unprotect(sys_prot_t pval);
typedef sys_prot_t XNetIoRingLwipLock;
#define LWIP_LOCK()        sys_arch_protect()
#define LWIP_UNLOCK(l)     sys_arch_unprotect(l)
#elif NO_SYS
typedef int XNetIoRingLwipLock;
#define LWIP_LOCK()        0
#define LWIP_UNLOCK(l)     (void)(l)
#else
typedef int XNetIoRingLwipLock;
#define LWIP_LOCK()        0
#define LWIP_UNLOCK(l)     (void)(l)
#endif

/**
 * @brief 轮询 lwIP pcap 网卡数据包
 *
 * 由 XAbstractEventDispatcher_processEvents 在每次事件循环迭代中调用，
 * 在 processReady 之前执行。
 *
 * 仅负责 pcap 数据包轮询，将网卡数据喂入 lwIP 协议栈。
 * lwIP 处理数据包后触发 Raw API 回调，回调中直接通过
 * push_socket_cq() 将 Socket 事件推入 IoRing CQ，
 * 随后由 processReady 的 drainCQ 统一分发。
 *
 * 注意：Socket 事件不再通过轮询获取，而是由回调直接投递，
 * 大幅提高响应速度（O(1) per event vs O(n) per iteration）。
 */
void XAbstractNetIoRing_pollLwip(void) {
    XNetIoRingLwipLock p = LWIP_LOCK();
    XNetworkLwip_pollPcap();
    LWIP_UNLOCK(p);
}
#endif /* XNETWORK_USE_LWIP */

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

/* 分发一条 CQ 完成事件到应用层。
 *
 * 从 CQ 条目提取 fd 和事件掩码，通过 XFd_get(fd)->ctx 获取 Socket 的
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
    if (!desc || !desc->ctx) return;
    owner = (XObject*)desc->ctx;

    /* 原生 I/O 对端关闭检测：0 字节 + Read + NativeIO 来源 => 关闭事件 */
    if (entry->m_bytes == 0 &&
        (entry->m_events & XSocketAct_Read) &&
        entry->m_sourceType == XAbstractNetIoRing_Source_NativeIO) {
        XEventSockClose* closeEvent = XEventSockClose_create(entry->m_fd);
        if (closeEvent) {
            ((XEvent*)closeEvent)->posted = true;
            ((XEvent*)closeEvent)->spontaneous = true;
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
            ((XEvent*)ev)->posted = true;
            ((XEvent*)ev)->spontaneous = true;
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
    XVTABLE_CREAT_DEFAULT
#if VTABLE_ISSTACK
    XVTABLE_STACK_INIT_DEFAULT(XCLASS_VTABLE_GET_SIZE(XAbstractNetIoRing))
#else
    XVTABLE_HEAP_INIT_DEFAULT
#endif
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
        VXAbstractNetIoRing_wakeUp
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

    /* 初始化 XClass 基类（设置 m_vtable=NULL, m_free=NULL） */
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
        XLockFreeQueue_delete_base(ring->m_sq);
        ring->m_sq = NULL;
    }
    if (ring->m_cq) {
        XLockFreeQueue_delete_base(ring->m_cq);
        ring->m_cq = NULL;
    }
    ring->m_eventFd = XFD_INVALID;
    ring->m_enabled = false;
}

XAbstractNetIoRing* XAbstractNetIoRing_create(void) {
    XAbstractNetIoRing* ring = (XAbstractNetIoRing*)XMalloc_System(sizeof(XAbstractNetIoRing));
    if (!ring) return NULL;

    XAbstractNetIoRing_init(ring);
    Set_Class_MemoryFree(ring, XFree_System);
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
#if !XPLATFORM_WINDOWS
XAbstractNetIoRing* XAbstractNetIoRing_createPlatform(void) {
    return XAbstractNetIoRing_create();
}
#endif

#endif /* XAbstractNetIoRing_ON */