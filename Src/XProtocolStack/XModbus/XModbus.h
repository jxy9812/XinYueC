#ifndef XMODBUS_H
#define XMODBUS_H
#ifdef __cplusplus
extern "C" {
#endif

#include"XVector.h"
#include"XQueue.h"
#include"XEventQueue.h"
#include"XModbusFrame.h"
#include"XModbusFrameQueue.h"
#include"XTimer.h"
    /* ----------------------- 宏定义 ------------------------------------------*/

//使用默认的Modbus TCP端口（502）
#define MB_TCP_PORT_USE_DEFAULT 0   

  /* ----------------------- 类型定义 ---------------------------------------*/

  /*! \ingroup modbus
   * \brief Modbus串行传输模式（RTU/ASCII/TCP）
   *
   * Modbus串行支持两种传输模式：ASCII或RTU。RTU速度更快但对硬件要求更高，需要低抖动的网络；
   * ASCII速度较慢但在低速链路（如调制解调器）上更可靠。TCP模式用于以太网通信。
   */
    typedef enum
    {
        MB_RTU_MASTER,                     /*!< RTU传输模式 主站*/
        MB_RTU_SLAVE,                     /*!< RTU传输模式 从站*/
        MB_ASCII_MASTER,                   /*!< ASCII传输模式 主站*/
        MB_ASCII_SLAVE,                   /*!< ASCII传输模式 从站*/
        MB_TCP_MASTER,                      /*!< TCP传输模式 主站*/
        MB_TCP_SLAVE                      /*!< TCP传输模式 从站*/
    } XModbusMode;

    /*! \ingroup modbus
     * \brief 寄存器读写模式
     *
     * 该值传递给支持读写寄存器值的回调函数。写操作表示应更新应用寄存器，
     * 读操作表示协议栈需要获取当前寄存器值。
     *
     * \see eMBRegHoldingCB( ), eMBRegCoilsCB( ), eMBRegDiscreteCB( ) 和 eMBRegInputCB( )
     */
    typedef enum
    {
        MB_REG_READ,                /*!< 读取寄存器值并传递给协议栈 */
        MB_REG_WRITE                /*!< 更新寄存器值 */
    } XModbusRegisterMode;

    /*! \ingroup modbus
     * \brief 协议栈函数错误码
     */
    typedef enum
    {
        MB_ENOERR,                  /*!< 无错误 */
        MB_ENOREG,                  /*!< 非法寄存器地址 */
        MB_EINVAL,                  /*!< 非法参数 */
        MB_EPORTERR,                /*!< 移植层错误 */
        MB_ENORES,                  /*!< 资源不足 */
        MB_EIO,                     /*!< I/O错误 */
        MB_EILLSTATE,               /*!< 协议栈状态非法 */
        MB_ETIMEDOUT                /*!< 超时错误 */
    } XModbusErrorCode;
    // 协议栈状态机（未初始化/禁用/启用）
    typedef enum {
        STATE_ENABLED,       // 协议栈已启用，正在处理通信（调用 eMBEnable 后）
        STATE_DISABLED,      // 协议栈已禁用，资源未释放（可通过 eMBEnable 重新激活）
        STATE_NOT_INITIALIZED// 协议栈未初始化（初始状态，需调用 eMBInit 初始化）
    } XModbusState;
    /*! \brief Modbus协议栈事件类型枚举 */
    typedef enum
    {
        EV_READY,                   /*!< 启动完成事件 */
        EV_FRAME_RECEIVED,          /*!< 接收到完整帧事件 */
        EV_EXECUTE,                 /*!< 执行功能码处理事件 */
        EV_FRAME_SENT               /*!< 帧发送完成事件 */
    } XModbusEventType;

    /* ----------------------- 接收状态机枚举 -----------------------------*/
    typedef enum {
        STATE_RX_INIT,    // 接收初始状态（等待总线空闲）
        STATE_RX_IDLE,    // 接收空闲状态（无数据接收）
        STATE_RX_RCV,     // 接收中状态（正在接收数据帧）
        STATE_RX_ERROR    // 接收错误状态（帧无效）
    } XModbusRcvState;

    /* ----------------------- 发送状态机枚举 -----------------------------*/
    typedef enum {
        STATE_TX_IDLE,    // 发送空闲状态（无数据发送）
        STATE_TX_XMIT,     // 发送中状态（正在发送数据帧）
        STATE_TX_END     // 发送结束（刚发完一帧数据）
    } XModbusSndState;
typedef struct XModbus XModbus;
/* ----------------------- 函数指针类型定义  0-------------------------------------*/
// 开始接收Modbus帧的函数指针类型
typedef void    (*XModbusFrameStart) (XModbus* modbus);
// 停止接收Modbus帧的函数指针类型
typedef void    (*XModbusFrameStop) (XModbus* modbus);
// 接收Modbus帧的函数指针类型
typedef XModbusErrorCode(*XModbusFrameReceive) (XModbus* modbus, XModbusFrameData* dataFrame);
// 发送Modbus帧的函数指针类型
typedef XModbusErrorCode(*XModbusFrameSend) (XModbus* modbus, XModbusFrameData* dataFrame);
// 关闭Modbus帧处理的函数指针类型
typedef void(*XModbusFrameClose) (XModbus* modbus);
typedef bool (*XModbusGetByte)(XModbus* modbus, uint8_t* Byte);//获取一个字符
typedef bool (*XModbusPutByte)(XModbus* modbus, uint8_t Byte);//发送一个字符
/*!
 * @brief 启用/禁用串口收发
 * @param xRxEnable TRUE=启用接收，FALSE=禁用接收
 * @param xTxEnable TRUE=启用发送，FALSE=禁用发送
 */
typedef void(*XModbusSerialEnable)(XModbus* modbus, bool xRxEnable, bool xTxEnable);
/* 端口层回调函数（需平台实现，处理外部事件驱动协议栈） */
typedef bool(*XModbusFrameCBByteReceived)(XModbus* modbus);   // 接收到单个字节时调用（触发接收状态机）
typedef bool(*XModbusFrameCBTransmitterEmpty)(XModbus* modbus); // 发送缓冲区空时调用（触发发送状态机）
typedef bool(*XModbusPortCBTimerExpired)(XModbus* modbus);    // 定时器超时回调（如 RTU 的 T35 超时、ASCII 的 T1S 超时）

typedef struct XModbus
{
    UCHAR    address;         // Modbus 主机地址（1-247，0 为广播地址，255 保留）
    XModbusMode mode;//模式
    XModbusErrorCode errorCode;//错误代码
    XModbusState     state;//状态
    XVector* recvBuffer;//接收缓冲区
    XQueue* sendQueue;//发送队列(XQueue<XVector>)
    XModbusFrameQueue* recvFrameQueue;//接收帧队列 后面处理执行
    XEventQueue* eventQueue;//事件队列
    int sendRemaining;//当前发送的进度
    /* ----------------------- 函数指针类型定义  0-------------------------------------*/
    XModbusGetByte xGetByte;//获取一个字符数据
    XModbusPutByte xPutByte;//发送一个字符数据
    XModbusFrameSend     peMBFrameSendCur;     // 帧发送函数指针（发送完整 Modbus 帧）
    XModbusFrameStart    pvMBFrameStartCur;    // 协议栈启动函数指针（初始化端口资源，如串口、定时器）
    XModbusFrameStop     pvMBFrameStopCur;     // 协议栈停止函数指针（停止接收/发送，释放临时资源）
    XModbusFrameReceive  peMBFrameReceiveCur;  // 帧接收函数指针（接收完整 Modbus 帧，返回帧数据）
    XModbusFrameClose    pvMBFrameCloseCur;    // 端口关闭函数指针（可选，用于释放端口资源，如关闭串口）
    /* ----------------------- RTU-------------------------------------*/
    bool xRxEnable;//接收
    bool xTxEnable;//发送
    XTimer* timer;//定时器     平台初始化
    XModbusSndState eSndState;    // 发送状态机（volatile确保多线程可见）
    XModbusRcvState eRcvState;    // 接收状态机
    //函数
    XModbusSerialEnable SerialEnable;//控制串口收发状态
    XModbusFrameCBByteReceived pxMBFrameCBByteReceived;// 接收到单个字节时调用（触发接收状态机）
    XModbusFrameCBTransmitterEmpty pxMBFrameCBTransmitterEmpty;// 发送缓冲区空时调用（触发发送状态机）
    XModbusPortCBTimerExpired pxMBPortCBTimerExpired; // 定时器超时回调（如 RTU 的 T35 超时、ASCII 的 T1S 超时）

}XModbus;

void XModbus_init(XModbus* modbus,size_t bufferSize, XModbusMode mode, XModbusGetByte xGetByte, XModbusPutByte xPutByte);
XModbus* XModbus_new(size_t bufferSize, XModbusMode mode, XModbusGetByte xGetByte, XModbusPutByte xPutByte);
/*! \ingroup modbus
 * \brief 启用Modbus协议栈
 *
 * 启用Modbus帧处理，仅在协议栈禁用状态下有效。
 *
 * \return 成功启用返回MB_ENOERR，非禁用状态返回MB_EILLSTATE
 */
XModbusErrorCode    XModbus_enable(XModbus* modbus);
/*! \ingroup modbus
 * \brief 禁用Modbus协议栈
 *
 * 停止Modbus帧处理。
 *
 * \return 成功禁用返回MB_ENOERR，非启用状态返回MB_EILLSTATE
 */
XModbusErrorCode   XModbus_disable(XModbus* modbus);
/*! \ingroup modbus
 * \brief 协议栈主轮询函数
 *
 * 需周期性调用，轮询间隔由Modbus从机超时配置决定。
 * 内部调用xMBPortEventGet()等待接收/发送状态机事件。
 *
 * \return 协议栈未启用时返回MB_EILLSTATE，正常返回MB_ENOERR
 */
XModbusErrorCode  XModbus_poll(XModbus* modbus);
#ifdef __cplusplus
}
#endif
#endif // !XModbus_H
