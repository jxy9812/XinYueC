#ifndef XEVENTTYPE_H
#define XEVENTTYPE_H
#ifdef __cplusplus
extern "C" {
#endif
typedef enum
{
    XEVENT_ALL,//全部事件
    XEVENT_READY=100,                   /*!< 启动完成事件 */
    XEVENT_FRAME_RECEIVED,              /*!< 接收到完整帧事件 */
    XEVENT_RX_BUFFER_OVERFLOW,          /*!< 接收缓冲区溢出 >*/
    XEVENT_RX_FRAME_ERROR,              /*!< 接收帧错误 >*/
    XEVENT_EXECUTE,                     /*!< 执行功能码处理事件 */
    XEVENT_FRAME_SENT,                  /*!< 帧发送完成事件 */

    /*套接字事件*/
    XEVENT_SOCKET_CONNECTED =   1001,
    XEVENT_SOCKET_DISCONNECTED= 1002,
    XEVENT_SOCKET_DATA_READY =  1003,
    XEVENT_SOCKET_ERROR=        1004,
    XEVENT_TIMER,          // 定时器事件
    XEVENT_SOCKET,
    XEVENT_KEY,            // 键盘事件
    XEVENT_MOUSE,          // 鼠标事件
    XEVENT_FUNC_RUN,       // 函数运行事件
    XEVENT_SLOT_RUN,       // 槽函数运行事件
    XEVENT_USER = 1000     // 用户自定义事件起始值
}XEventType;
// 事件优先级
typedef enum {
    XEVENT_PRIORITY_LOWEST=0,
    XEVENT_PRIORITY_LOW ,
    XEVENT_PRIORITY_NORMAL,
    XEVENT_PRIORITY_HIGH,
    XEVENT_PRIORITY_HIGHEST,
    XEVENT_PRIORITY_COUNT // 优先级数量
} XEventPriority;

#ifdef __cplusplus
}
#endif	
#endif