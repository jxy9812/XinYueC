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
 * 虚函数表定义（纯虚类，无 class_init，子类自行创建虚函数表）
 *
 * 平台后端（XNetIoRingWin32 / XNetIoRingLinux 等）通过继承本类，
 * 在各自的 class_init 中从 XClass 继承并添加以下虚函数，
 * 实现平台特定的 I/O 事件源注册、数据包处理、IOCP/epoll 轮询等。
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
    XAbstractNetIoRing_Source_None   = 0,   /**< 无 */
    XAbstractNetIoRing_Source_Netif  = 1,   /**< 网卡数据包就绪（pcap / TAP / 硬件 ISR） */
    XAbstractNetIoRing_Source_IOCP   = 2,   /**< IOCP 完成通知（Windows socket/file I/O） */
    XAbstractNetIoRing_Source_Timer  = 3,   /**< 定时器到期（PostQueuedCompletionStatus） */
    XAbstractNetIoRing_Source_Custom = 4    /**< 自定义事件 */
} XAbstractNetIoRing_SourceType;

/* ================================================================
 * SQ 条目 - 提交队列（平台 -> 核心）
 * ================================================================ */
typedef struct {
    XFd      m_fd;          /**< 关联的文件描述符 */
    uint32_t m_sourceType;  /**< 事件来源类型（XAbstractNetIoRing_SourceType） */
    uint32_t m_sourceData;  /**< 来源特定数据（网卡索引 / IOCP key 等） */
    uint32_t m_events;      /**< 事件掩码（XSocketActType 位掩码） */
    uint32_t m_bytes;       /**< 传输字节数（IOCP 完成时有效） */
    int      m_error;       /**< 错误码（0=成功） */
} XAbstractNetIoRing_SQEntry;

/* ================================================================
 * CQ 条目 - 完成队列（核心 -> 应用）
 * ================================================================ */
typedef struct {
    XFd      m_fd;          /**< 目标 Socket 的文件描述符 */
    uint32_t m_events;      /**< 事件掩码（XSocketActType 位掩码） */
    uint32_t m_bytes;       /**< 传输字节数 */
    int      m_error;       /**< 错误码（0=成功） */
    uint32_t m_sourceType;  /**< 来源类型（用于区分 socket/file/timer） */
} XAbstractNetIoRing_CQEntry;

/* ================================================================
 * XAbstractNetIoRing 类结构体（纯虚基类）
 *
 * 继承 XClass，内含 SQ/CQ 双无锁环形队列。
 * 本类不提供 class_init 和默认虚函数实现，子类必须：
 *   1. 在自己的 class_init 中从 XClass 继承并添加全部 9 个虚函数
 *   2. 在 init 中调用 XAbstractNetIoRing_init 后设置自己的虚函数表
 *   3. 在 deinit 中调用 XAbstractNetIoRing_cleanupQueues 清理 SQ/CQ
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
 * @brief 初始化基类部分（创建 SQ/CQ 无锁队列）
 * @param ring 待初始化的 XAbstractNetIoRing 实例指针
 * @note 不设置虚函数表，子类必须在调用本函数后自行 XClassSetVtable。
 *       子类 init 应先 memset 清零整个子类结构体。
 */
void XAbstractNetIoRing_init(XAbstractNetIoRing* ring);

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
 * @return 平台后端实例（Win32->XNetIoRingWin32, Linux->XNetIoRingLinux, 裸机->XNetIoRingLwip）
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
 * @brief 轮询 lwIP pcap 网卡数据包（通用实现，由 XNetIoRing_lwip.c 提供）
 * @note 仅在 XNETWORK_USE_LWIP 模式下编译，由事件调度器在 processEvents 中调用。
 *       仅负责 pcap 数据包轮询，Socket 事件由 lwIP 回调直接投递 CQ（无需轮询）。
 */
void XAbstractNetIoRing_pollLwip(void);
#endif

#endif /* XAbstractNetIoRing_ON */

#ifdef __cplusplus
}
#endif

#endif /* XABSTRACTNETIORING_H */
