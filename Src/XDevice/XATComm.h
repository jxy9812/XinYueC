#ifndef XATCOMM_H
#define XATCOMM_H

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
XCLASS_DEFINE_ENUM(XATComm, ProcessResponse) = XCLASS_VTABLE_GET_SIZE(XObject), // 处理AT指令响应
XCLASS_DEFINE_END(XATComm)

/**
 * @brief AT 通信引擎结构体
 */
typedef struct XATComm
{
    XObject m_base;                   // 继承 XObject
    XIODevice* m_io;                  // 底层 IO 设备（外部传入）
    
    XByteArray* m_responseBuffer;     // 响应缓冲区（动态扩容）
    XTimerId m_timeoutId;             // 超时定时器 ID（XObject_startTimer_ms）
    int32_t m_operationResult:1;      // 当前操作结果
    int32_t m_currentOp:31;           // 当前操作类型（子类自定义，由 sendCommand 传入）
    /* 与 m_operationResult 分开保存：旧位域仅有一位，只能表示未完成/成功。 */
    bool m_operationError;
} XATComm;

// ========== 构造与析构 ==========

/**
 * @brief 虚函数表初始化
 * @return 指向 XVtable 的指针
 */
XVtable* XATComm_class_init(void);

/**
 * @brief 初始化 XATComm 实例
 * @param comm XATComm 对象指针
 * @param io 底层 IO 设备指针（外部提供，必须已初始化）
 */
void XATComm_init(XATComm* comm, XIODevice* io);

/**
 * @brief 创建 XATComm 实例
 * @param io 底层 IO 设备指针
 * @return 成功返回 XATComm 对象指针，失败返回 NULL
 */
XATComm* XATComm_create_ex(XMemoryType memory,  XIODevice* io);

// ========== AT 指令操作 ==========

/**
 * @brief 发送 AT 指令并同步等待结果
 * @details 发送 "cmd\r\n"，启动超时定时器，阻塞等待响应或超时。
 * @param comm XATComm 对象指针
 * @param cmd AT 指令（不含 \r\n，自动追加），为 NULL 时仅等待已有操作的响应
 * @param opType 操作类型（由调用方自定义，用于超时/响应处理中的区分）
 * @param msecs 超时等待时间（毫秒），-1 无限等待，0 立即返回
 * @return true 操作成功（收到 OK），false 操作失败或超时
 */
bool XATComm_sendCommand(XATComm* comm, const char* cmd, int opType, int msecs);

// ========== 信号 ==========

/**
 * @brief 信号：收到 AT 响应数据
 * @param comm XATComm 对象指针
 * @param data 响应数据（指向 m_responseBuffer 内部，只读）
 * @return 信号函数指针（供 XSignal 宏使用）
 */
void* XATComm_response_signal(XATComm* comm, const char* data);

/**
 * @brief 信号：收到 "OK" 响应
 * @param comm XATComm 对象指针
 * @return 信号函数指针
 */
void* XATComm_ok_signal(XATComm* comm);

/**
 * @brief 信号：收到 "ERROR" 响应
 * @param comm XATComm 对象指针
 * @param errorMsg 错误信息
 * @return 信号函数指针
 */
void* XATComm_error_signal(XATComm* comm, const char* errorMsg);

/**
 * @brief 信号：操作超时
 * @param comm XATComm 对象指针
 * @param opType 超时时的操作类型
 * @return 信号函数指针
 */
void* XATComm_timeout_signal(XATComm* comm, int opType);

/** @brief 删除 XATComm 实例 */
#define XATComm_deleteLater    XObject_deleteLater

/** @brief 清空响应缓冲区 */
#define XATComm_clearResponse(comm)  XByteArray_clear_base((comm)->m_responseBuffer)

/** @brief 获取响应数据指针（只读） */
#define XATComm_responseData(comm)   ((const char*)XByteArray_data((comm)->m_responseBuffer))

/** @brief 获取响应数据大小（字节） */
#define XATComm_responseSize(comm)   XByteArray_size_base((comm)->m_responseBuffer)

#ifdef __cplusplus
}
#endif


/* XClass create API default-memory wrappers. */
#undef XATComm_create
#define XATComm_create(...) XATComm_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, __VA_ARGS__)

#endif // XATCOMM_H
