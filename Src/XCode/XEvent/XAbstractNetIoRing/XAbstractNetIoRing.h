// XAbstractNetIoRing.h
#ifndef XABSTRACTNETIORING_H
#define XABSTRACTNETIORING_H

#ifdef __cplusplus
extern "C" {
#endif

#include "CXinYueConfig.h"
#if XAbstractNetIoRing_ON

#include "XClass.h"
#include "XTypes.h"
#include "XFileDescriptor.h"
#include "XLockFreeQueue.h"
#include <stdbool.h>
#include <stdint.h>

/* ================================================================
 * SQ/CQ 容量配置
 * ================================================================ */
#ifndef XNETIORING_SQ_CAPACITY
#define XNETIORING_SQ_CAPACITY  64    /**< 提交队列容量（平台->核心的通知数） */
#endif

#ifndef XNETIORING_CQ_CAPACITY
#define XNETIORING_CQ_CAPACITY  128   /**< 完成队列容量（核心->应用的完成事件数） */
#endif

/* ================================================================
 * 虚函数表定义（基类，提供 class_init 和默认虚函数实现）
 *
 * 平台后端（XNetIoRingWin32 / XNetIoRingLinux 等）通过继承本类，
 * 在各自的 class_init 中调用 XVTABLE_INHERIT_XCLASS(XAbstractNetIoRing)
 * 继承默认实现，然后仅重载平台特定的虚函数（如 PollPlatform、
 * WaitForEvents、WakeUp、GetEventFd、RegisterEvent、Deinit）。
 * ================================================================ */
XCLASS_DEFINE_BEGING(XAbstractNetIoRing)
XCLASS_DEFINE_ENUM(XAbstractNetIoRing, GetEventFd) = XCLASS_VTABLE_GET_SIZE(XClass), /**< 获取平台事件 fd */
XCLASS_DEFINE_ENUM(XAbstractNetIoRing, ProcessSource),    /**< 处理一条 SQ 条目（如 pcap_dispatch） */
XCLASS_DEFINE_ENUM(XAbstractNetIoRing, PollPlatform),     /**< 轮询平台 I/O（如 IOCP / epoll），将完成推入 CQ */
XCLASS_DEFINE_ENUM(XAbstractNetIoRing, HasPendingInput),  /**< 检查是否有待处理输入 */
XCLASS_DEFINE_ENUM(XAbstractNetIoRing, RegisterEvent),    /**< 注册事件源（如 RegisterWaitForSingleObject） */
XCLASS_DEFINE_ENUM(XAbstractNetIoRing, UnregisterEvent),  /**< 注销事件源 */
XCLASS_DEFINE_ENUM(XAbstractNetIoRing, DispatchCQEntry),  /**< 分发一条 CQ 完成事件到应用层 */
XCLASS_DEFINE_ENUM(XAbstractNetIoRing, WaitForEvents),    /**< 阻塞等待事件（超时返回），由平台后端重载 */
XCLASS_DEFINE_ENUM(XAbstractNetIoRing, WakeUp),           /**< 唤醒阻塞中的 WaitForEvents，由平台后端重载 */
XCLASS_DEFINE_END(XAbstractNetIoRing)

/* ================================================================
 * 事件来源类型
 * ================================================================ */
typedef enum {
    XAbstractNetIoRing_Source_None     = 0,   /**< 无 */
    XAbstractNetIoRing_Source_Netif    = 1,   /**< 网卡数据包就绪（pcap / TAP / 硬件 ISR） */
    XAbstractNetIoRing_Source_NativeIO = 2,   /**< 系统原生异步 I/O 完成（Windows=IOCP, Linux=epoll/io_uring） */
    XAbstractNetIoRing_Source_Timer    = 3,   /**< 定时器到期（系统定时机制） */
    XAbstractNetIoRing_Source_Custom   = 4,   /**< 自定义事件 */
    XAbstractNetIoRing_Source_ISR      = 5    /**< 裸机硬件中断（UART/SPI/Ethernet DMA ISR） */
} XAbstractNetIoRing_SourceType;

/* ================================================================
 * SQ 条目 - 提交队列（平台 -> 核心）
 * ================================================================ */
typedef struct {
    XFd      m_fd;              /**< 文件描述符（fd 表索引，O(1) 查找） */
    uint32_t m_sourceData;      /**< 来源特定数据（网卡索引 / IOCP key 等） */
    uint32_t m_bytes;           /**< 传输字节数（IOCP 完成时有效） */
    /* 位域压缩：原 3×4=12B -> 4B，结构体从 24B 降至 16B */
    uint32_t m_events     : 5;  /**< 事件掩码（XSocketActType 位掩码，0-31） */
    uint32_t m_sourceType : 3;  /**< 事件来源（XAbstractNetIoRing_SourceType，0-4） */
    uint32_t m_fdType     : 4;  /**< fd 类型（XFdType，0-4，预留扩展至 15） */
    int      m_error      : 16; /**< 错误码（-32768~32767，覆盖 Windows/lwIP 错误码） */
    uint32_t              : 4;  /**< 保留位 */
} XAbstractNetIoRing_SQEntry;

/* ================================================================
 * CQ 条目 - 完成队列（核心 -> 应用）
 * ================================================================ */
typedef struct {
    XFd      m_fd;              /**< 文件描述符（fd 表索引，O(1) 查找） */
    uint32_t m_bytes;           /**< 传输字节数 */
    /* 位域压缩：原 3×4=12B -> 4B，结构体从 20B 降至 12B */
    uint32_t m_events     : 5;  /**< 事件掩码（XSocketActType 位掩码，0-31） */
    uint32_t m_sourceType : 3;  /**< 事件来源（XAbstractNetIoRing_SourceType，0-4） */
    uint32_t m_fdType     : 4;  /**< fd 类型（XFdType，0-4，预留扩展至 15） */
    int      m_error      : 16; /**< 错误码（-32768~32767） */
    uint32_t              : 4;  /**< 保留位 */
} XAbstractNetIoRing_CQEntry;

/* ================================================================
 * XAbstractNetIoRing 类结构体（基类，提供默认虚函数实现）
 *
 * 继承 XClass，内含 SQ/CQ 双无锁环形队列。
 * 本类提供 class_init 和全部 9 个虚函数的默认实现（平台无关）：
 *   - GetEventFd:        返回 XFD_INVALID（无单一事件源）
 *   - ProcessSource:     空操作
 *   - PollPlatform:      空操作（由子类重载为 IOCP/epoll 轮询）
 *   - HasPendingInput:   检查 SQ + CQ 是否有待处理条目
 *   - RegisterEvent:     返回 true（默认允许）
 *   - UnregisterEvent:   返回 true（默认允许）
 *   - DispatchCQEntry:   从 CQ 条目创建 XEventSockAct/XEventSockClose 投递
 *   - WaitForEvents:     空操作（裸机单线程不阻塞）
 *   - WakeUp:            空操作
 * 子类只需重载平台特定的虚函数，其余沿用基类默认实现。
 * ================================================================ */
typedef struct XAbstractNetIoRing {
    XClass m_class;                /**< 基类（必须位于第一位） */
    XLockFreeQueue* m_sq;         /**< 提交队列：平台事件源 -> 核心处理 */
    XLockFreeQueue* m_cq;         /**< 完成队列：核心处理 -> 应用层 */
    XFd    m_eventFd;              /**< 平台事件 fd（XFD_INVALID=无事件源） */
    bool   m_enabled;              /**< 是否启用异步 I/O */
} XAbstractNetIoRing;

/* ==================== 构造与析构 ==================== */

/**
 * @brief 初始化基类部分（设置虚函数表 + 创建 SQ/CQ 无锁队列）
 * @param ring 待初始化的 XAbstractNetIoRing 实例指针
 * @note 设置基类虚函数表（含默认实现），子类可在调用本函数后
 *       通过 XClassSetVtable 重载为子类虚函数表。
 *       子类 init 应先 memset 清零整个子类结构体。
 */
void XAbstractNetIoRing_init(XAbstractNetIoRing* ring);

/**
 * @brief 虚函数表初始化（创建并返回基类虚函数表）
 * @return 基类虚函数表指针（含 XClass + 9 个默认虚函数实现）
 * @note 子类 class_init 中通过 XVTABLE_INHERIT_XCLASS(XAbstractNetIoRing) 继承
 */
XVtable* XAbstractNetIoRing_class_init(void);

/**
 * @brief 创建基类实例（堆分配，使用默认虚函数实现）
 * @return 新创建的 XAbstractNetIoRing 指针，失败返回 NULL
 * @note 用于无 IOCP/epoll 的平台（裸机 / 嵌入式），直接使用基类默认实现。
 *       平台后端（如 XNetIoRingWin32）使用各自的 _create 函数。
 */
XAbstractNetIoRing* XAbstractNetIoRing_create_ex(XMemoryType memory);

/**
 * @brief 清理 SQ/CQ 队列资源（非虚函数，供子类 deinit 调用）
 * @param ring XAbstractNetIoRing 实例指针
 */
void XAbstractNetIoRing_cleanupQueues(XAbstractNetIoRing* ring);

/** @brief 反初始化（宏复用基类，等价于 XClass_deinit_base） */
#define XAbstractNetIoRing_deinit_base  XClass_deinit_base
/** @brief 释放对象（宏复用基类，等价于 XClass_delete_base） */
#define XAbstractNetIoRing_delete_base  XClass_delete_base

/* ==================== 核心功能（非虚函数，无 _base 后缀） ==================== */

/**
 * @brief 向提交队列（SQ）推入一条条目
 * @param ring  XAbstractNetIoRing 实例指针
 * @param entry 待推入的 SQ 条目指针
 * @return true=推入成功；false=队列已满或参数无效
 */
bool XAbstractNetIoRing_pushSQ(XAbstractNetIoRing* ring, const XAbstractNetIoRing_SQEntry* entry);

/**
 * @brief 向完成队列（CQ）推入一条完成事件
 * @param ring  XAbstractNetIoRing 实例指针
 * @param entry 待推入的 CQ 条目指针
 * @return true=推入成功；false=队列已满或参数无效
 */
bool XAbstractNetIoRing_pushCompletion(XAbstractNetIoRing* ring, const XAbstractNetIoRing_CQEntry* entry);

/**
 * @brief 排空完成队列（CQ），逐条通过虚函数 DispatchCQEntry 分发到应用层
 * @param ring XAbstractNetIoRing 实例指针
 */
void XAbstractNetIoRing_drainCQ(XAbstractNetIoRing* ring);

/**
 * @brief 处理就绪的 I/O 事件（一次完整轮询周期）
 *
 * 执行流程：
 *   1. 调用虚函数 PollPlatform 轮询平台 I/O（IOCP/epoll），完成事件推入 CQ
 *   2. 排空 SQ，对每条调用虚函数 ProcessSource
 *   3. 排空 CQ，通过虚函数 DispatchCQEntry 投递完成事件到应用层
 *
 * @param ring XAbstractNetIoRing 实例指针
 */
void XAbstractNetIoRing_processReady(XAbstractNetIoRing* ring);

/**
 * @brief 检查提交队列（SQ）是否有待处理条目
 * @param ring XAbstractNetIoRing 实例指针
 * @return true=SQ 非空；false=SQ 为空或参数无效
 */
bool XAbstractNetIoRing_hasSQPending(const XAbstractNetIoRing* ring);

/**
 * @brief 检查完成队列（CQ）是否有待处理条目
 * @param ring XAbstractNetIoRing 实例指针
 * @return true=CQ 非空；false=CQ 为空或参数无效
 */
bool XAbstractNetIoRing_hasCQPending(const XAbstractNetIoRing* ring);

/**
 * @brief 检查 IoRing 是否已启用异步 I/O
 * @param ring XAbstractNetIoRing 实例指针
 * @return true=已启用；false=未启用或参数无效
 */
bool XAbstractNetIoRing_isEnabled(const XAbstractNetIoRing* ring);

/* ==================== 虚函数调度入口（通过 vtable 分发到平台后端） ==================== */

/**
 * @brief 获取平台事件源 fd（虚函数）
 * @param ring XAbstractNetIoRing 实例指针
 * @return 平台事件 fd（如 IOCP 端口句柄），XFD_INVALID 表示无事件源
 */
XFd XAbstractNetIoRing_getEventFd_base(XAbstractNetIoRing* ring);

/**
 * @brief 处理一条 SQ 条目（虚函数）
 * @param ring  XAbstractNetIoRing 实例指针
 * @param entry 待处理的 SQ 条目指针
 */
void XAbstractNetIoRing_processSource_base(XAbstractNetIoRing* ring, const XAbstractNetIoRing_SQEntry* entry);

/**
 * @brief 轮询平台 I/O，将完成事件推入 CQ（虚函数）
 * @param ring XAbstractNetIoRing 实例指针
 * @note 平台后端实现：Win32 调用 GetQueuedCompletionStatus，lwIP 调用 pcap 轮询
 */
void XAbstractNetIoRing_pollPlatform_base(XAbstractNetIoRing* ring);

/**
 * @brief 检查是否有待处理输入（虚函数）
 * @param ring XAbstractNetIoRing 实例指针
 * @return true=有待处理输入；false=无
 */
bool XAbstractNetIoRing_hasPendingInput_base(const XAbstractNetIoRing* ring);

/**
 * @brief 注册事件源到平台后端（虚函数）
 * @param ring XAbstractNetIoRing 实例指针
 * @param fd   待注册的文件描述符
 * @return true=注册成功；false=失败
 */
bool XAbstractNetIoRing_registerEvent_base(XAbstractNetIoRing* ring, XFd fd);

/**
 * @brief 从平台后端注销事件源（虚函数）
 * @param ring XAbstractNetIoRing 实例指针
 * @param fd   待注销的文件描述符
 * @return true=注销成功；false=失败
 */
bool XAbstractNetIoRing_unregisterEvent_base(XAbstractNetIoRing* ring, XFd fd);

/**
 * @brief 分发一条 CQ 完成事件到应用层（虚函数）
 * @param ring  XAbstractNetIoRing 实例指针
 * @param entry CQ 条目指针（包含 fd、事件掩码、错误码）
 * @note 平台后端实现：从 CQ 条目创建 XEventSockAct，通过 XCoreApplication_postEvent 投递
 */
void XAbstractNetIoRing_dispatchCQEntry_base(XAbstractNetIoRing* ring, const XAbstractNetIoRing_CQEntry* entry);

/**
 * @brief 阻塞等待事件（超时返回），由平台后端重载（虚函数）
 * @param ring      XAbstractNetIoRing 实例指针
 * @param timeoutMs 超时毫秒数（-1=无限等待，0=立即返回）
 * @note 平台后端实现：Win32 调用 GetQueuedCompletionStatus(timeout)，
 *       lwIP 裸机直接返回（单线程不阻塞）
 */
void XAbstractNetIoRing_waitForEvents_base(XAbstractNetIoRing* ring, int timeoutMs);

/**
 * @brief 唤醒阻塞中的 WaitForEvents（虚函数）
 * @param ring XAbstractNetIoRing 实例指针
 * @note 平台后端实现：Win32 调用 PostQueuedCompletionStatus，
 *       lwIP 裸机空操作（单线程无需唤醒）
 */
void XAbstractNetIoRing_wakeUp_base(XAbstractNetIoRing* ring);

/* ==================== 平台钩子 ==================== */

/**
 * @brief 平台钩子：创建平台特定的 XAbstractNetIoRing 后端
 * @return 平台后端实例（Win32->XNetIoRingWin32, Linux/裸机->基类默认实现）
 * @note 由各平台 Drive 层实现，XAbstractEventDispatcher 通过此函数创建 I/O 后端
 */
XAbstractNetIoRing* XAbstractNetIoRing_createPlatform(void);

/* ==================== 全局单例 ==================== */

/**
 * @brief 获取全局 XAbstractNetIoRing 单例
 * @return 全局 IoRing 实例指针，未设置返回 NULL
 */
XAbstractNetIoRing* XAbstractNetIoRing_global(void);

/**
 * @brief 设置全局 XAbstractNetIoRing 单例
 * @param ring 要设置为全局单例的 IoRing 实例指针
 */
void XAbstractNetIoRing_setGlobal(XAbstractNetIoRing* ring);

/* ==================== lwIP pcap 轮询（通用实现，平台无关） ==================== */

#ifdef XNETWORK_USE_LWIP
/**
 * @brief 轮询 lwIP pcap 网卡数据包（通用实现，由 XAbstractNetIoRing.c 提供）
 * @note 仅在 XNETWORK_USE_LWIP 模式下编译，由事件调度器在 processEvents 中调用。
 *       仅负责 pcap 数据包轮询，Socket 事件由 lwIP 回调直接投递 CQ（无需轮询）。
 */
void XAbstractNetIoRing_pollLwip(void);
#endif

#endif /* XAbstractNetIoRing_ON */

#ifdef __cplusplus
}
#endif


/* XClass create API default-memory wrappers. */
#undef XAbstractNetIoRing_create
#define XAbstractNetIoRing_create() XAbstractNetIoRing_create_ex(XCLASS_DEFAULT_MEMORY_TYPE)

#endif /* XABSTRACTNETIORING_H */