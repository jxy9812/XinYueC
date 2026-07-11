/**
 * @file XNetIoRing_lwip.c
 * @brief XAbstractNetIoRing 的 lwIP 后端实现（平台无关）
 *
 * 本文件在 XNETWORK_USE_LWIP 模式下编译，提供两部分功能：
 *
 * 1. XAbstractNetIoRing_pollLwip() — 通用辅助函数
 *    由 XAbstractEventDispatcher_processEvents 在每次事件循环迭代中调用，
 *    完成：
 *      a) pcap 数据包轮询（XNetworkLwip_pollPcap）- 将网卡数据喂入 lwIP 协议栈
 *      b) Socket 事件由 lwIP 回调直接投递 CQ（push_socket_cq）- 无需轮询
 *      c) CQ 条目由 processReady 的 drainCQ 统一分发
 *
 *    在 Windows 平台上，后端为 XNetIoRingWin32（IOCP），
 *    pollLwip() 负责 lwIP 事件，IOCP 负责 socket/file I/O 完成。
 *    两者的 CQ 条目均由 XNetIoRingWin32::dispatchCQEntry 统一分发。
 *
 * 2. XNetIoRingLwip 类 — 完整后端实现（用于无 IOCP/epoll 的平台）
 *    在裸机 / 嵌入式平台（无 IOCP/epoll）上，本类作为
 *    XAbstractNetIoRing 的完整后端，实现全部 9 个虚函数：
 *      - pollPlatform:      pcap 轮询 + Socket 事件轮询 -> CQ
 *      - dispatchCQEntry:   从 CQ 条目创建 XEventSockAct -> 投递到应用层
 *      - waitForEvents:     平台无关休眠（Sleep / 空操作）
 *      - wakeUp:             空操作（裸机单线程，无需唤醒）
 *      - 其余函数见下方注释
 *
 * 设计要点：
 *   - 平台无关：仅依赖 XNetworkLwip_pollPcap 抽象接口 + 回调直接投递 CQ
 *   - 与平台后端解耦：不涉及 IOCP/epoll，仅处理 lwIP 协议栈事件
 *   - CQ 分发统一：无论事件来源（IOCP 或 lwIP），均通过
 *     XFd_get(fd)->ctx 获取 owner，创建 XEventSockAct 投递到应用层
 */

#include "CXinYueConfig.h"
#include "XNetwork_config.h"    /* XNETWORK_USE_LWIP 宏定义 */
#if XAbstractNetIoRing_ON

#ifdef XNETWORK_USE_LWIP

#include "XAbstractNetIoRing.h"
#include "XNetwork_lwip_platform.h"
#include "XMemory.h"
#include "XEvent.h"
#include "XCoreApplication.h"
#include "XFileDescriptor.h"
#include "XTypes.h"
#include <string.h>
#include <stdlib.h>
#include "lwip/opt.h"     /* NO_SYS 宏定义 */
#include "lwip/sys.h"     /* sys_prot_t, sys_arch_protect/unprotect */

/* NO_SYS 模式下的核心锁前向声明（由 sys_arch.c 提供） */
#if NO_SYS
typedef sys_prot_t XNetLwipCoreLock;
extern sys_prot_t sys_arch_protect(void);
extern void sys_arch_unprotect(sys_prot_t pval);
#define LWIP_LOCK()        sys_arch_protect()
#define LWIP_UNLOCK(l)     sys_arch_unprotect(l)
#else
typedef int XNetLwipCoreLock;
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
    /* pcap 数据包轮询（NO_SYS=1 需核心锁保护） */
#if NO_SYS
    {
        XNetLwipCoreLock prot = LWIP_LOCK();
        XNetworkLwip_pollPcap();
        LWIP_UNLOCK(prot);
    }
#else
    XNetworkLwip_pollPcap();
#endif
}
/* ================================================================
 * 第二部分：XNetIoRingLwip 类（完整后端，用于无 IOCP/epoll 的平台）
 *
 * 在裸机 / 嵌入式平台上，本类作为 XAbstractNetIoRing 的完整后端。
 * 在 Windows 平台上，后端为 XNetIoRingWin32（IOCP），
 * 本类不被创建，仅 pollLwip() 函数生效。
 * ================================================================ */

/* 虚函数表枚举：继承 XAbstractNetIoRing，不添加新虚函数 */
XCLASS_DEFINE_BEGING(XNetIoRingLwip)
XCLASS_DEFINE_EXTEND_END(XNetIoRingLwip, XAbstractNetIoRing)

/**
 * @brief XNetIoRingLwip 类结构体
 *
 * 继承 XAbstractNetIoRing 基类，无额外成员。
 * 所有状态由 lwIP 协议栈内部维护，
 * 本类仅作为虚函数分发的载体。
 */
typedef struct XNetIoRingLwip {
    XAbstractNetIoRing m_class;   /**< 基类（必须位于第一位） */
} XNetIoRingLwip;

/* ==================== 前向声明 ==================== */
static XFd    VXNetIoRingLwip_getEventFd(XAbstractNetIoRing* self);
static void   VXNetIoRingLwip_processSource(XAbstractNetIoRing* self,
                                             const XAbstractNetIoRing_SQEntry* entry);
static void   VXNetIoRingLwip_pollPlatform(XAbstractNetIoRing* self);
static bool   VXNetIoRingLwip_hasPendingInput(const XAbstractNetIoRing* self);
static bool   VXNetIoRingLwip_registerEvent(XAbstractNetIoRing* self, XFd fd);
static bool   VXNetIoRingLwip_unregisterEvent(XAbstractNetIoRing* self, XFd fd);
static void   VXNetIoRingLwip_dispatchCQEntry(XAbstractNetIoRing* self,
                                                const XAbstractNetIoRing_CQEntry* entry);
static void   VXNetIoRingLwip_waitForEvents(XAbstractNetIoRing* self, int timeoutMs);
static void   VXNetIoRingLwip_wakeUp(XAbstractNetIoRing* self);
static void   VXNetIoRingLwip_deinit(XAbstractNetIoRing* obj);

/* ================================================================
 * 虚函数实现
 * ================================================================ */

/**
 * @brief 获取平台事件 fd
 * @return XFD_INVALID（lwIP 无单一事件源 fd）
 *
 * lwIP 的事件由协议栈回调驱动，不依赖单一的
 * 事件 fd（如 IOCP 端口或 epoll fd）。返回 XFD_INVALID
 * 表示无事件源需要注册到事件调度器。
 */
static XFd VXNetIoRingLwip_getEventFd(XAbstractNetIoRing* self) {
    (void)self;
    return XFD_INVALID;
}

/**
 * @brief 处理一条 SQ 条目
 *
 * lwIP 模式下事件由协议栈回调驱动，不使用 SQ。
 * 本函数为空实现，仅满足接口契约。
 */
static void VXNetIoRingLwip_processSource(XAbstractNetIoRing* self,
                                             const XAbstractNetIoRing_SQEntry* entry) {
    (void)self;
    (void)entry;
}

/**
 * @brief 轮询平台 I/O（pcap 数据包 + Socket 事件），将完成事件推入 CQ
 *
 * 调用 XAbstractNetIoRing_pollLwip() 完成全部工作：
 *   1. 轮询 pcap 数据包，喂入 lwIP 协议栈
 *   2. 检查所有 Socket 的状态标志位
 *   3. 将事件推入 CQ 队列
 *
 * @note 当后端为 XNetIoRingLwip 时，processEvents 中的 pollLwip() 调用
 *       会与本函数重复。为避免双重轮询，当
 *       后端为 XNetIoRingLwip 时，pollLwip() 应跳过。
 *       通过检查全局单例类型实现（见下方）。
 */
static void VXNetIoRingLwip_pollPlatform(XAbstractNetIoRing* self) {
    /*
     * pcap 轮询 + Socket 事件轮询已由 XAbstractNetIoRing_pollLwip() 完成
     * （在 processEvents 中于 processReady 之前调用）。
     * 此处无需重复轮询，直接返回，由 drainCQ 分发 CQ 中的完成事件。
     */
    (void)self;
}

/**
 * @brief 检查是否有待处理输入
 * @return true=有 CQ 条目待处理（由回调直接投递）
 *
 * 由事件调度器在判断是否需要继续轮询时调用。
 */
static bool VXNetIoRingLwip_hasPendingInput(const XAbstractNetIoRing* self) {

    return XAbstractNetIoRing_hasCQPending(self);
}

/**
 * @brief 注册事件源
 *
 * lwIP 的 Socket 在创建时自动注册到内部列表
 * （XNetwork_createSocketPrivate -> socketList_add），无需 IoRing 介入。
 */
static bool VXNetIoRingLwip_registerEvent(XAbstractNetIoRing* self, XFd fd) {
    (void)self;
    (void)fd;
    return true;
}

/**
 * @brief 注销事件源
 *
 * Socket 在销毁时自动从内部列表移除
 * （XNetwork_deleteSocketPrivate -> socketList_remove），无需 IoRing 介入。
 */
static bool VXNetIoRingLwip_unregisterEvent(XAbstractNetIoRing* self, XFd fd) {
    (void)self;
    (void)fd;
    return true;
}

/**
 * @brief 分发一条 CQ 完成事件到应用层
 *
 * 从 CQ 条目提取 fd 和事件掩码，通过 XFd_get(fd)->ctx
 * 获取 Socket 的 owner（XObject*），创建 XEventSockAct
 * 并通过 XCoreApplication_postEvent 投递到应用层。
 *
 * 与 XNetIoRingWin32::dispatchCQEntry 逻辑一致，仅事件来源不同：
 *   - Win32: 事件来自 IOCP 完成通知
 *   - lwIP: 事件来自 Raw API 回调直接投递 CQ
 *
 * @param self  XAbstractNetIoRing 实例
 * @param entry CQ 条目（包含 fd、事件掩码、错误码）
 */
static void VXNetIoRingLwip_dispatchCQEntry(XAbstractNetIoRing* self,
                                                const XAbstractNetIoRing_CQEntry* entry) {
    XFileDescriptor* desc;
    XObject* owner;

    (void)self;
    if (!entry) return;

    /* 通过 fd 查找 owner（与 Win32 实现一致） */
    desc = XFd_get(entry->m_fd);
    if (!desc || !desc->ctx) return;
    owner = (XObject*)desc->ctx;

    /* 创建 Socket 活动事件并投递到应用层 */
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

/**
 * @brief 阻塞等待事件（超时返回）
 *
 * 在裸机 / 单线程环境下，无需真正阻塞。
 * 返回后由事件循环继续调用 processEvents，
 * 轮询定时器和 I/O 事件。
 *
 * @param self      XAbstractNetIoRing 实例
 * @param timeoutMs 超时毫秒（-1=无限等待，0=立即返回）
 */
static void VXNetIoRingLwip_waitForEvents(XAbstractNetIoRing* self, int timeoutMs) {
    (void)self;
    (void)timeoutMs;
    /* 裸机单线程：不阻塞，直接返回让事件循环继续轮询 */
}

/**
 * @brief 唤醒阻塞中的 waitForEvents
 *
 * 裸机单线程下 waitForEvents 不阻塞，无需唤醒。
 */
static void VXNetIoRingLwip_wakeUp(XAbstractNetIoRing* self) {
    (void)self;
}

/**
 * @brief 析构函数：清理 SQ/CQ 队列 + 调用基类 deinit
 */
static void VXNetIoRingLwip_deinit(XAbstractNetIoRing* obj) {
    if (!obj) return;

    /* 清理 SQ/CQ 队列（基类纯虚，无 deinit，需手动清理） */
    XAbstractNetIoRing_cleanupQueues(obj);

    /* 调用 XClass 基类 deinit */
    XClass_Deinit_Parent(XClass, (XClass*)obj);
}

/* ================================================================
 * 虚函数表初始化
 *
 * 纯虚基类无 class_init，本类直接从 XClass 继承并添加全部 9 个虚函数。
 * ================================================================ */
XVtable* XNetIoRingLwip_class_init(void) {
    XVTABLE_CREAT_DEFAULT
#if VTABLE_ISSTACK
    XVTABLE_STACK_INIT_DEFAULT(XCLASS_VTABLE_GET_SIZE(XNetIoRingLwip))
#else
    XVTABLE_HEAP_INIT_DEFAULT
#endif
    /* 继承 XClass 虚函数表（Copy/Move/Deinit） */
    XVTABLE_INHERIT_XCLASS(XClass);

    /* 添加全部 9 个虚函数（XAbstractNetIoRing 的虚函数） */
    void* table[] = {
        VXNetIoRingLwip_getEventFd,
        VXNetIoRingLwip_processSource,
        VXNetIoRingLwip_pollPlatform,
        VXNetIoRingLwip_hasPendingInput,
        VXNetIoRingLwip_registerEvent,
        VXNetIoRingLwip_unregisterEvent,
        VXNetIoRingLwip_dispatchCQEntry,
        VXNetIoRingLwip_waitForEvents,
        VXNetIoRingLwip_wakeUp
    };
    XVTABLE_ADD_FUNC_LIST_DEFAULT(table);

    /* 重载 deinit */
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Deinit, VXNetIoRingLwip_deinit);

    return XVTABLE_DEFAULT;
}

/* ================================================================
 * 构造与初始化
 * ================================================================ */

/**
 * @brief 初始化 XNetIoRingLwip 实例
 * @param ring 未初始化的 XNetIoRingLwip 指针
 *
 * 执行顺序：
 *   1. 清零整个结构体
 *   2. 初始化基类（创建 SQ/CQ 队列）
 *   3. 设置本类的虚函数表
 *   4. 标记启用
 */
void XNetIoRingLwip_init(XNetIoRingLwip* ring) {
    if (!ring) return;

    /* 清零整个子类结构体 */
    memset(ring, 0, sizeof(XNetIoRingLwip));

    /* 初始化基类（创建 SQ/CQ 队列，不设置虚函数表） */
    XAbstractNetIoRing_init((XAbstractNetIoRing*)ring);

    /* 设置本类的虚函数表 */
    XClassSetVtable(ring, XNetIoRingLwip);

    /* 标记启用 */
    ((XAbstractNetIoRing*)ring)->m_enabled = true;
}

/**
 * @brief 创建 XNetIoRingLwip 实例（堆分配）
 * @return 新创建的 XNetIoRingLwip 指针，失败返回 NULL
 */
XNetIoRingLwip* XNetIoRingLwip_create(void) {
    XNetIoRingLwip* ring = (XNetIoRingLwip*)XMalloc_System(sizeof(XNetIoRingLwip));
    if (!ring) return NULL;

    XNetIoRingLwip_init(ring);
    Set_Class_MemoryFree(ring, XFree_System);
    return ring;
}

/* ================================================================
 * 平台钩子：创建后端
 *
 * 在无 IOCP/epoll 的平台（裸机 / 嵌入式）上，
 * 创建 XNetIoRingLwip 作为 XAbstractNetIoRing 后端。
 *
 * 在 Windows 平台上，本函数不被编译（由 XNetIoRingWin32.c
 * 的 XAbstractNetIoRing_createPlatform 提供）。
 * ================================================================ */
#ifndef _WIN32
XAbstractNetIoRing* XAbstractNetIoRing_createPlatform(void) {
    return (XAbstractNetIoRing*)XNetIoRingLwip_create();
}
#endif

#endif /* XNETWORK_USE_LWIP */

#endif /* XAbstractNetIoRing_ON */
