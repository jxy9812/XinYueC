#ifndef XDATAFRAMECOMMENUM_H
#define XDATAFRAMECOMMENUM_H
#ifdef __cplusplus
extern "C" {
#endif
#include"XDataFrameCommConfig.h"
#include"XEventType.h"
/*! \ingroup m_modbus
 * \brief 协议栈函数错误码
 */
typedef enum
{
    XDFC_ENOERR,                  /*!< 无错误 */
    XDFC_EINVAL,                  /*!< 非法参数 */
    XDFC_EPORTERR,                /*!< 移植层错误 */
    XDFC_ENORES,                  /*!< 资源不足 */
    XDFC_EIO,                     /*!< I/O错误 */
    XDFC_EILLSTATE,               /*!< 协议栈状态非法 */
    XDFC_ETIMEDOUT                /*!< 超时错误 */
} XDFC_ErrorCode;
typedef enum
{
    XDFC_READY,                               /*!< 启动完成事件 */
    XDFC_FRAME_RECEIVED,             /*!< 接收到完整帧事件 */
    XDFC_RX_BUFFER_OVERFLOW,      /*!<接收缓冲区溢出 >*/
    XDFC_RX_FRAME_ERROR,             /*!<接收帧错误>*/
    XDFC_EXECUTE,                           /*!< 执行功能码处理事件 */
    XDFC_FRAME_SENT                      /*!< 帧发送完成事件 */
}XDFC_EventType;
// 协议栈状态机（未初始化/禁用/启用）
typedef enum
{
    XDFC_STATE_ENABLED,       // 协议栈已启用，正在处理通信（调用 eMBEnable 后）
    XDFC_STATE_DISABLED,      // 协议栈已禁用，资源未释放（可通过 eMBEnable 重新激活）
    XDFC_STATE_NOT_INITIALIZED// 协议栈未初始化（初始状态，需调用 eMBInit 初始化）
} XDFC_State;
/* ----------------------- 接收状态机枚举 -----------------------------*/
typedef enum {
    XDFC_STATE_RX_INIT,    // 接收初始状态（等待总线空闲）
    XDFC_STATE_RX_IDLE,    // 接收空闲状态（无数据接收）
    XDFC_STATE_RX_HEAD,    // 接收帧头中
    XDFC_STATE_RX_RCV,     // 接收中状态（正在接收数据帧）
    XDFC_STATE_RX_ERROR    // 接收错误状态（帧无效）
}XDFC_RcvState;

/* ----------------------- 发送状态机枚举 -----------------------------*/
typedef enum {
    XDFC_STATE_TX_IDLE,    // 发送空闲状态（无数据发送）
    XDFC_STATE_TX_XMIT,     // 发送中状态（正在发送数据帧）
    XDFC_STATE_TX_END     // 发送结束（刚发完一帧数据）
} XDFC_SndState;
typedef enum {
    XDFC_COMM_MODE_HALF_DUPLEX,  // 半双工通信模式
    XDFC_COMM_MODE_FULL_DUPLEX   // 全双工通信模式
} XDFC_CommMode;
typedef enum {
    XDFC_SEND_MODE_BYTE,    // 逐字节发送模式
    XDFC_SEND_MODE_WHOLE    // 整体发送模式
} XDFC_SendMode;
typedef enum {
    XDFC_FRAME_END_TIMEOUT,  // 帧超时结束
    XDFC_FRAME_END_MARKER    // 帧标志结束
} XDFC_FrameEndType;
#if XDFC_ENUM_TO_STRING
//XDataFrameComm协议栈事件类型转string字符串常量输出
const char* XDataFrameComm_EventType_toString(XDFC_EventType type);
#endif // XMB_ENUM_TO_STRING

#ifdef __cplusplus
}
#endif
#endif