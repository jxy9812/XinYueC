#ifndef XMODBUSENUM_H
#define XMODBUSENUM_H
#ifdef __cplusplus
extern "C" {
#endif
#include"XModbusConfig.h"
#include<stdio.h>
#include<stdint.h>
#include"XTypes.h"
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
    XMB_NOT_MODE = 0xFF,                     /*无协议*/
    XMB_RTU_MASTER=0,                     /*!< RTU传输模式 主站*/
    XMB_RTU_SLAVE,                     /*!< RTU传输模式 从站*/
    XMB_ASCII_MASTER,                   /*!< ASCII传输模式 主站*/
    XMB_ASCII_SLAVE,                   /*!< ASCII传输模式 从站*/
    XMB_TCP_MASTER,                      /*!< TCP传输模式 主站*/
    XMB_TCP_SLAVE                      /*!< TCP传输模式 从站*/
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
    XMB_REG_READ,                /*!< 读取寄存器值并传递给协议栈 */
    XMB_REG_WRITE                /*!< 更新寄存器值 */
} XModbusRegisterMode;
typedef enum {
  XMB_RecvHand_Not = 0x0000,//无设置
  XMB_RecvHand_CodeOnly= 0x0001,//仅匹配Modbus的功能码
  XMB_RecvHand_AddressOnly= 0x0002,//仅匹配Modbus的地址
  XMB_RecvHand_AddressCode= XMB_RecvHand_CodeOnly| XMB_RecvHand_AddressOnly,//同时匹配Modbus地址和功能码
} XModbusRecvHandMode;
typedef struct
{
    union {
        struct
        {
            uint8_t address;
            uint8_t code;
        };
        uint16_t addressCode;
    };
} XModbusRecvMatch;
typedef void (*XModbusRecvHandCb)(XModbusRecvMatch* math, XModbus* modbus, XModbusFrame* frame, void* userData);
#if XMB_ENUM_TO_STRING

#endif // XMB_ENUM_TO_STRING




#ifdef __cplusplus
}
#endif
#endif