#ifndef XMODBUSENUM_H
#define XMODBUSENUM_H
#ifdef __cplusplus
extern "C" {
#endif
#include"XModbusConfig.h"
    /*! \brief Modbus异常码枚举（对应功能码异常响应） */
    typedef enum
    {
        MB_EX_NONE = 0x00,                 /*!< 无异常 */
        MB_EX_ILLEGAL_FUNCTION = 0x01,     /*!< 非法功能码（功能码未实现） */
        MB_EX_ILLEGAL_DATA_ADDRESS = 0x02, /*!< 非法数据地址（寄存器/线圈地址超出范围） */
        MB_EX_ILLEGAL_DATA_VALUE = 0x03,   /*!< 非法数据值（写入数据无效） */
        MB_EX_SLAVE_DEVICE_FAILURE = 0x04, /*!< 从机设备故障（硬件或初始化错误） */
        MB_EX_ACKNOWLEDGE = 0x05,          /*!< 接收确认（保留功能） */
        MB_EX_SLAVE_BUSY = 0x06,           /*!< 从机繁忙（请求处理中） */
        MB_EX_MEMORY_PARITY_ERROR = 0x08,  /*!< 内存奇偶校验错误（存储错误） */
        MB_EX_GATEWAY_PATH_FAILED = 0x0A,  /*!< 网关路径失败（Modbus TCP网关错误） */
        MB_EX_GATEWAY_TGT_FAILED = 0x0B    /*!< 网关目标设备失败（目标从机无响应） */
    } XModbusException;
    /*! \ingroup modbus
     * \brief Modbus串行传输模式（RTU/ASCII/TCP）
     *
     * Modbus串行支持两种传输模式：ASCII或RTU。RTU速度更快但对硬件要求更高，需要低抖动的网络；
     * ASCII速度较慢但在低速链路（如调制解调器）上更可靠。TCP模式用于以太网通信。
     */
    typedef enum
    {
        MB_NOT_MODE = 0xFF,                     /*无协议*/
        MB_RTU_MASTER=0,                     /*!< RTU传输模式 主站*/
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
    /*! \brief 串口传输校验位类型 */
    typedef enum
    {
        MB_PAR_NONE,                /*!< 无校验 */
        MB_PAR_ODD,                 /*!< 奇校验 */
        MB_PAR_EVEN                 /*!< 偶校验 */
    } XModbusParity;
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




#if MB_ENUM_TO_STRING
   //modbus协议栈事件类型转string字符串常量输出
   const char* XModbusEventType_toString(XModbusEventType type);

#endif // MB_ENUM_TO_STRING




#ifdef __cplusplus
}
#endif
#endif