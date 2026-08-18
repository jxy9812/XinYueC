#ifndef XATCOMM_H
#define XATCOMM_H

/**
 * @file XATComm.h
 * @brief 通用 AT 指令通信引擎接口。
 * @details 该组件基于 XObject 和 XIODevice，供蜂窝、WiFi 等 AT 模块复用；
 *          底层 I/O 对象始终由调用方持有，XATComm 只负责协议交互和响应缓存。
 */

#ifdef __cplusplus
extern "C" {
#endif

#include "XObject.h"
#include "XIODevice.h"
#include "XByteArray.h"
#include "XEvent.h"

/**
 * @brief 通用 AT 指令通信引擎
 * @details 继承自 XObject，提供 AT 指令的发送、响应接收、超时处理。
 * 子类（如 XESP8266Wifi、SIM800 等）继承后可直接使用基类的 AT 通信能力。
 * 使用信号与槽机制通知响应结果。
 */
XCLASS_DEFINE_BEGING(XATComm)
XCLASS_DEFINE_ENUM(XATComm, ProcessResponse) = XCLASS_VTABLE_GET_SIZE(XObject), /**< 处理 AT 指令响应。 */
XCLASS_DEFINE_END(XATComm)

/**
 * @brief AT 通信引擎结构体
 */
typedef struct XATComm
{
    XObject m_base;                   /**< XObject 基类，必须是第一个成员。 */
    XIODevice* m_io;                  /**< 底层 I/O 设备；外部提供，函数只借用。 */

    XByteArray* m_responseBuffer;     /**< 响应缓冲区；由对象拥有并动态扩容。 */
    XTimerId m_timeoutId;             /**< 超时定时器 ID，由 XObject_startTimer_ms 创建。 */
    int32_t m_operationResult:1;      /**< 当前操作是否收到成功结果。 */
    int32_t m_currentOp:31;           /**< 当前操作类型；由 sendCommand 传入，供子类解释。 */
    /* 与 m_operationResult 分开保存：旧位域仅有一位，只能表示未完成/成功。 */
    bool m_operationError;            /**< 当前操作是否收到错误响应。 */
} XATComm;

// ========== 构造与析构 ==========

/**
 * @brief 虚函数表初始化
 * @return 指向共享 XVtable 的指针；初始化失败返回 NULL。
 */
XVtable* XATComm_class_init(void);

/**
 * @brief 初始化 XATComm 实例
 * @param comm XATComm 对象指针；不能为 NULL。
 * @param io 底层 I/O 设备指针；外部提供且必须已初始化，函数只借用不释放。
 */
void XATComm_init(XATComm* comm, XIODevice* io);

/**
 * @brief 创建 XATComm 实例
 * @param memory 对象分配使用的内存类型。
 * @param io 底层 I/O 设备指针；外部提供且必须已初始化，函数只借用不释放。
 * @return 成功返回新对象指针；失败返回 NULL，调用方负责释放对象。
 */
XATComm* XATComm_create_ex(XMemoryType memory,  XIODevice* io);

// ========== AT 指令操作 ==========

/**
 * @brief 发送 AT 指令并同步等待结果
 * @details 发送 "cmd\r\n"，启动超时定时器，阻塞等待响应或超时。
 * @param comm XATComm 对象指针；不能为 NULL。
 * @param cmd AT 指令（不含 \r\n，函数自动追加）；为 NULL 时仅等待已有操作的响应。
 * @param opType 操作类型（由调用方自定义，用于超时/响应处理中的区分）
 * @param msecs 超时等待时间（毫秒），-1 无限等待，0 立即返回。
 * @return true 操作成功（收到 OK），false 操作失败、超时或参数非法。
 */
bool XATComm_sendCommand(XATComm* comm, const char* cmd, int opType, int msecs);

// ========== 信号 ==========

/**
 * @brief 信号：收到 AT 响应数据
 * @param comm XATComm 对象指针。
 * @param data 响应数据；指向内部缓冲区，只读且仅在本次信号处理期间有效。
 * @return 信号函数指针（供 XSignal 宏使用）。
 */
void* XATComm_response_signal(XATComm* comm, const char* data);

/**
 * @brief 信号：收到 "OK" 响应
 * @param comm XATComm 对象指针。
 * @return 信号函数指针（供 XSignal 宏使用）。
 */
void* XATComm_ok_signal(XATComm* comm);

/**
 * @brief 信号：收到 "ERROR" 响应
 * @param comm XATComm 对象指针。
 * @param errorMsg 错误信息；只读借用，可能为 NULL。
 * @return 信号函数指针（供 XSignal 宏使用）。
 */
void* XATComm_error_signal(XATComm* comm, const char* errorMsg);

/**
 * @brief 信号：操作超时
 * @param comm XATComm 对象指针。
 * @param opType 超时时的操作类型，对应 sendCommand 的操作类型。
 * @return 信号函数指针（供 XSignal 宏使用）。
 */
void* XATComm_timeout_signal(XATComm* comm, int opType);

/** @brief 延迟删除 XATComm 实例；参数和生命周期契约继承 XObject_deleteLater。 */
#define XATComm_deleteLater    XObject_deleteLater

/** @brief 清空响应缓冲区；comm 只借用，缓冲区仍由对象拥有。 */
#define XATComm_clearResponse(comm)  XByteArray_clear_base((comm)->m_responseBuffer)

/** @brief 获取响应数据指针（只读）；指针在下一次写入或清空响应前有效。 */
#define XATComm_responseData(comm)   ((const char*)XByteArray_data((comm)->m_responseBuffer))

/** @brief 获取响应数据大小（字节）；返回值不包含额外的 NUL 终止字节。 */
#define XATComm_responseSize(comm)   XByteArray_size_base((comm)->m_responseBuffer)

#ifdef __cplusplus
}
#endif


/* XClass create API default-memory wrappers. */
#undef XATComm_create
#define XATComm_create(...) XATComm_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, __VA_ARGS__)

#endif // XATCOMM_H
