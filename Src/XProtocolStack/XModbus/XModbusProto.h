/* 
 * 自由Modbus库：一个用于Modbus ASCII/RTU协议的可移植实现。
 * 版权所有 (c) 2006-2018 克里斯蒂安·沃尔特 <cwalter@embedded-solutions.at>
 * 保留所有权利。
 * 
 * 允许以源代码和二进制形式进行再分发和使用，无论是否进行修改，但需满足以下条件：
 * 1. 源代码的再分发必须保留上述版权声明、此条件列表和以下免责声明。
 * 2. 二进制形式的再分发必须在分发时提供的文档和/或其他材料中复制上述版权声明、此条件列表和以下免责声明。
 * 3. 未经作者事先明确书面许可，不得使用作者的姓名来认可或推广基于此软件衍生的产品。
 * 
 * 本软件由作者“按原样”提供，任何明示或暗示的保证，包括但不限于适销性和特定用途适用性的暗示保证，均不予以保证。
 * 在任何情况下，作者均不对因使用本软件而产生的任何直接、间接、偶然、特殊、惩戒性或后果性损害（包括但不限于替代商品或服务的采购、使用损失、数据或利润损失、或业务中断）承担责任，无论这种损害是如何引起的，也无论责任理论是合同责任、严格责任还是侵权责任（包括疏忽或其他原因），即使已被告知可能发生此类损害。
 */

 #ifndef _MB_PROTO_H
 #define _MB_PROTO_H
 
 #ifdef __cplusplus
 PR_BEGIN_EXTERN_C
 #endif

     /* ----------------------- 宏定义 ------------------------------------------*/
#define MB_ADDRESS_BROADCAST    ( 0 )   /*! Modbus广播地址（地址0表示广播） */
#define MB_ADDRESS_MIN          ( 1 )   /*! 最小从机地址（有效范围1-247） */
#define MB_ADDRESS_MAX          ( 247 ) /*! 最大从机地址（有效范围1-247） */

// 标准Modbus功能码定义（0x01-0x17及扩展功能码）
#define MB_FUNC_NONE                          (  0 )            /*! 无效功能码 */
#define MB_FUNC_READ_COILS                    (  1 )            /*! 读取线圈（功能码0x01） */
#define MB_FUNC_READ_DISCRETE_INPUTS          (  2 )            /*! 读取离散输入（功能码0x02） */
#define MB_FUNC_WRITE_SINGLE_COIL             (  5 )            /*! 写入单个线圈（功能码0x05） */
#define MB_FUNC_WRITE_MULTIPLE_COILS          ( 15 )            /*! 写入多个线圈（功能码0x0F） */
#define MB_FUNC_READ_HOLDING_REGISTER         (  3 )            /*! 读取保持寄存器（功能码0x03） */
#define MB_FUNC_READ_INPUT_REGISTER           (  4 )            /*! 读取输入寄存器（功能码0x04） */
#define MB_FUNC_WRITE_REGISTER                (  6 )            /*! 写入单个寄存器（功能码0x06） */
#define MB_FUNC_WRITE_MULTIPLE_REGISTERS      ( 16 )            /*! 写入多个寄存器（功能码0x10） */
#define MB_FUNC_READWRITE_MULTIPLE_REGISTERS  ( 23 )            /*! 读写多个寄存器（功能码0x17） */
#define MB_FUNC_DIAG_READ_EXCEPTION           (  7 )            /*! 诊断：读取异常状态（功能码0x07） */
#define MB_FUNC_DIAG_DIAGNOSTIC               (  8 )            /*! 诊断：执行诊断测试（功能码0x08） */
#define MB_FUNC_DIAG_GET_COM_EVENT_CNT        ( 11 )            /*! 诊断：获取通信事件计数（功能码0x0B） */
#define MB_FUNC_DIAG_GET_COM_EVENT_LOG        ( 12 )            /*! 诊断：获取通信事件日志（功能码0x0C） */
#define MB_FUNC_OTHER_REPORT_SLAVEID          ( 17 )            /*! 其他：报告从机ID（功能码0x11） */
#define MB_FUNC_ERROR                         ( 128 )           /*! 异常功能码基值（0x80+原功能码） */

/* ----------------------- 类型定义 ---------------------------------------*/
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

 /*! \brief 功能码处理函数指针类型 */
 //typedef XModbusException(*pXModbusFunctionHandler) (UCHAR* pucFrame, USHORT* pusLength);

 ///*! \brief 功能码处理函数表结构体 */
 //typedef struct
 //{
 //    UCHAR           ucFunctionCode;       /*!< Modbus功能码（1-127） */
 //    pxMBFunctionHandler pxHandler;        /*!< 对应的处理函数指针 */
 //} xMBFunctionHandler;
 
 #ifdef __cplusplus
 PR_END_EXTERN_C
 #endif
 
 #endif