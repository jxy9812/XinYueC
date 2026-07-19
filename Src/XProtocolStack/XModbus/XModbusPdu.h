#ifndef XMODBUSPDU_H
#define XMODBUSPDU_H

#include <stdint.h>
#include <stdbool.h>
#include "XClass.h"
#include "XTypes.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @file XModbusPdu.h
 * @brief Modbus协议数据单元（对齐Qt6 QModbusPdu）
 * @details 实现Modbus PDU的核心功能，包括功能码、异常码和数据管理
 *
 * @par 功能特性
 * - 支持所有标准Modbus功能码
 * - 支持异常响应处理
 * - PDU数据自动管理
 * - 支持继承扩展（Request/Response）
 *
 * @par 类层次结构
 * @code
 * XClass (基类)
 *   └── XModbusPdu (Modbus PDU基类)
 *         ├── XModbusRequest (请求PDU)
 *         └── XModbusResponse (响应PDU)
 *               └── XModbusExceptionResponse (异常响应PDU)
 * @endcode
 *
 * @par 使用示例
 * @code
 * // 创建请求PDU
 * XModbusRequest* request = XModbusRequest_create_with_code(XModbusPdu_ReadHoldingRegisters);
 * uint8_t data[] = {0x00, 0x01, 0x00, 0x0A}; // 起始地址1，数量10
 * XModbusPdu_setData((XModbusPdu*)request, data, 4);
 *
 * // 检查有效性
 * if (XModbusPdu_isValid((XModbusPdu*)request)) {
 *     // 发送请求...
 * }
 *
 * // 清理
 * XModbusRequest_delete_base(request);
 * @endcode
 */

/******************************************************************************************
* 常量定义
******************************************************************************************/

/**
 * @brief Modbus异常响应标志位
 * @details 当响应功能码的最高位被设置时，表示这是一个异常响应
 */
#define XMODBUS_PDU_EXCEPTION_BYTE 0x80

/******************************************************************************************
* 枚举类型定义
******************************************************************************************/

/**
 * @brief Modbus异常码枚举
 * @details 对齐Qt6 QModbusPdu::ExceptionCode
 * @par 异常码说明
 * | 异常码 | 名称 | 说明 |
 * |--------|------|------|
 * | 0x01 | IllegalFunction | 设备不支持该功能码 |
 * | 0x02 | IllegalDataAddress | 数据地址无效 |
 * | 0x03 | IllegalDataValue | 数据值无效 |
 * | 0x04 | ServerDeviceFailure | 服务器设备故障 |
 * | 0x05 | Acknowledge | 已接受，正在处理 |
 * | 0x06 | ServerDeviceBusy | 服务器设备忙 |
 * | 0x08 | MemoryParityError | 内存奇偶校验错误 |
 * | 0x0A | GatewayPathUnavailable | 网关路径不可用 |
 * | 0x0B | GatewayTargetDeviceFailedToRespond | 网关目标设备无响应 |
 */
typedef enum {
    XModbusPdu_IllegalFunction = 0x01,                          ///< 非法功能码
    XModbusPdu_IllegalDataAddress = 0x02,                       ///< 非法数据地址
    XModbusPdu_IllegalDataValue = 0x03,                         ///< 非法数据值
    XModbusPdu_ServerDeviceFailure = 0x04,                      ///< 服务器设备故障
    XModbusPdu_Acknowledge = 0x05,                              ///< 确认
    XModbusPdu_ServerDeviceBusy = 0x06,                         ///< 服务器设备忙
    XModbusPdu_NegativeAcknowledge = 0x07,                      ///< 否定确认
    XModbusPdu_MemoryParityError = 0x08,                        ///< 内存奇偶校验错误
    XModbusPdu_GatewayPathUnavailable = 0x0A,                   ///< 网关路径不可用
    XModbusPdu_GatewayTargetDeviceFailedToRespond = 0x0B,       ///< 网关目标设备无响应
    XModbusPdu_ExtendedException = 0xFF                         ///< 扩展异常码
} XModbusPdu_ExceptionCode;

/**
 * @brief Modbus功能码枚举
 * @details 对齐Qt6 QModbusPdu::FunctionCode
 * @par 功能码分类
 * | 范围 | 类型 | 说明 |
 * |------|------|------|
 * | 0x01-0x06 | 位/寄存器访问 | 读写线圈和寄存器 |
 * | 0x07-0x0B | 诊断 | 诊断和状态查询 |
 * | 0x0F-0x10 | 批量写入 | 多线圈/寄存器写入 |
 * | 0x14-0x18 | 文件/队列 | 文件记录和FIFO |
 * | 0x2B | 其他 | 封装接口传输 |
 */
typedef enum {
    XModbusPdu_Invalid = 0x00,                                  ///< 无效功能码
    XModbusPdu_ReadCoils = 0x01,                                ///< 读线圈(FC01)
    XModbusPdu_ReadDiscreteInputs = 0x02,                      ///< 读离散输入(FC02)
    XModbusPdu_ReadHoldingRegisters = 0x03,                    ///< 读保持寄存器(FC03)
    XModbusPdu_ReadInputRegisters = 0x04,                      ///< 读输入寄存器(FC04)
    XModbusPdu_WriteSingleCoil = 0x05,                          ///< 写单个线圈(FC05)
    XModbusPdu_WriteSingleRegister = 0x06,                      ///< 写单个寄存器(FC06)
    XModbusPdu_ReadExceptionStatus = 0x07,                      ///< 读异常状态(FC07)
    XModbusPdu_Diagnostics = 0x08,                              ///< 诊断(FC08)
    XModbusPdu_GetCommEventCounter = 0x0B,                      ///< 获取通信事件计数(FC11)
    XModbusPdu_GetCommEventLog = 0x0C,                          ///< 获取通信事件日志(FC12)
    XModbusPdu_WriteMultipleCoils = 0x0F,                       ///< 写多个线圈(FC15)
    XModbusPdu_WriteMultipleRegisters = 0x10,                   ///< 写多个寄存器(FC16)
    XModbusPdu_ReportServerId = 0x11,                           ///< 报告服务器ID(FC17)
    XModbusPdu_ReadFileRecord = 0x14,                           ///< 读文件记录(FC20)
    XModbusPdu_WriteFileRecord = 0x15,                          ///< 写文件记录(FC21)
    XModbusPdu_MaskWriteRegister = 0x16,                        ///< 掩码写寄存器(FC22)
    XModbusPdu_ReadWriteMultipleRegisters = 0x17,               ///< 读写多个寄存器(FC23)
    XModbusPdu_ReadFifoQueue = 0x18,                            ///< 读FIFO队列(FC24)
    XModbusPdu_EncapsulatedInterfaceTransport = 0x2B,           ///< 封装接口传输(FC43)
    XModbusPdu_UndefinedFunctionCode = 0x100                    ///< 未定义功能码
} XModbusPdu_FunctionCode;

/******************************************************************************************
 * 结构体定义
 ******************************************************************************************/

 /**
  * @brief Modbus PDU基类结构体
  * @details 继承自XClass，封装功能码和数据
  *
  * @par 成员说明
  * | 成员 | 类型 | 说明 |
  * |------|------|------|
  * | m_class | XClass | 基类 |
  * | m_code | XModbusPdu_FunctionCode | 功能码（可能包含异常标志） |
  * | m_data | XByteArray* | PDU数据部分 |
  *
  * @par PDU结构
  * @code
  * +----------+------------------+
  * | 功能码   | 数据             |
  * | 1字节    | 0-252字节        |
  * +----------+------------------+
  * @endcode
  */
typedef struct XModbusPdu {
    XClass m_class;                             ///< 继承自XClass
    XModbusPdu_FunctionCode m_code;             ///< 功能码（可能包含异常标志）
    XByteArray* m_data;                         ///< PDU数据部分（不包含功能码）
} XModbusPdu;

/**
 * @brief Modbus请求PDU结构体
 * @details 继承自XModbusPdu，用于封装Modbus请求
 *
 * @par 使用示例
 * @code
 * // 创建读保持寄存器请求
 * XModbusRequest* req = XModbusRequest_create_with_code(XModbusPdu_ReadHoldingRegisters);
 * uint8_t data[] = {0x00, 0x00, 0x00, 0x0A}; // 地址0，数量10
 * XModbusPdu_setData((XModbusPdu*)req, data, 4);
 * @endcode
 */
typedef struct XModbusRequest {
    XModbusPdu m_base;                          ///< 继承自XModbusPdu（必须是第一个成员）
} XModbusRequest;

/**
 * @brief Modbus响应PDU结构体
 * @details 继承自XModbusPdu，用于封装Modbus响应
 */
typedef struct XModbusResponse {
    XModbusPdu m_base;                          ///< 继承自XModbusPdu（必须是第一个成员）
} XModbusResponse;

/**
 * @brief Modbus异常响应PDU结构体
 * @details 继承自XModbusResponse，用于封装Modbus异常响应
 *
 * @par 异常响应格式
 * @code
 * +----------+----------+
 * | 功能码   | 异常码   |
 * | 1字节    | 1字节    |
 * +----------+----------+
 * 注：功能码最高位被设置(OR 0x80)
 * @endcode
 */
typedef struct XModbusExceptionResponse {
    XModbusResponse m_base;                     ///< 继承自XModbusResponse（必须是第一个成员）
} XModbusExceptionResponse;

/******************************************************************************************
 * 类初始化接口
 ******************************************************************************************/

 /**
  * @brief 初始化XModbusPdu的虚函数表
  * @return 初始化完成的虚函数表指针
  */
XVtable* XModbusPdu_class_init(void);

/**
 * @brief 初始化XModbusRequest的虚函数表
 * @return 初始化完成的虚函数表指针
 */
XVtable* XModbusRequest_class_init(void);

/**
 * @brief 初始化XModbusResponse的虚函数表
 * @return 初始化完成的虚函数表指针
 */
XVtable* XModbusResponse_class_init(void);

/**
 * @brief 初始化XModbusExceptionResponse的虚函数表
 * @return 初始化完成的虚函数表指针
 */
XVtable* XModbusExceptionResponse_class_init(void);

/******************************************************************************************
 * XModbusPdu 创建/初始化接口
 ******************************************************************************************/

 /**
  * @brief 在堆上创建并初始化一个XModbusPdu实例
  * @return 成功返回XModbusPdu指针，失败返回NULL
  */
XModbusPdu* XModbusPdu_create(void);

/**
 * @brief 创建XModbusPdu的深拷贝
 * @param pdu 源XModbusPdu指针
 * @return 成功返回新XModbusPdu指针，失败返回NULL
 */
XModbusPdu* XModbusPdu_create_copy(const XModbusPdu* pdu);
XModbusPdu* XModbusPdu_create_move(XModbusPdu* pdu);
/**
 * @brief 创建带功能码的XModbusPdu
 * @param code 功能码
 * @return 成功返回XModbusPdu指针，失败返回NULL
 */
XModbusPdu* XModbusPdu_create_with_code(XModbusPdu_FunctionCode code);

/**
 * @brief 初始化已分配的XModbusPdu实例
 * @param pdu XModbusPdu指针（非NULL）
 */
void XModbusPdu_init(XModbusPdu* pdu);

/**
 * @brief 初始化已分配的XModbusPdu实例（带功能码）
 * @param pdu XModbusPdu指针（非NULL）
 * @param code 功能码
 */
void XModbusPdu_init_with_code(XModbusPdu* pdu, XModbusPdu_FunctionCode code);

/******************************************************************************************
 * XModbusRequest 创建/初始化接口
 ******************************************************************************************/

 /**
  * @brief 在堆上创建并初始化一个XModbusRequest实例
  * @return 成功返回XModbusRequest指针，失败返回NULL
  */
XModbusRequest* XModbusRequest_create(void);
XModbusRequest* XModbusRequest_create_copy(const XModbusRequest* req);
XModbusRequest* XModbusRequest_create_move(XModbusRequest* req);
/**
 * @brief 创建带功能码的XModbusRequest
 * @param code 功能码
 * @return 成功返回XModbusRequest指针，失败返回NULL
 */
XModbusRequest* XModbusRequest_create_with_code(XModbusPdu_FunctionCode code);

/**
 * @brief 初始化已分配的XModbusRequest实例
 * @param req XModbusRequest指针（非NULL）
 */
void XModbusRequest_init(XModbusRequest* req);

/**
 * @brief 初始化已分配的XModbusRequest实例（带功能码）
 * @param req XModbusRequest指针（非NULL）
 * @param code 功能码
 */
void XModbusRequest_init_with_code(XModbusRequest* req, XModbusPdu_FunctionCode code);

/******************************************************************************************
 * XModbusResponse 创建/初始化接口
 ******************************************************************************************/

 /**
  * @brief 在堆上创建并初始化一个XModbusResponse实例
  * @return 成功返回XModbusResponse指针，失败返回NULL
  */
XModbusResponse* XModbusResponse_create(void);
XModbusResponse* XModbusResponse_create_copy(XModbusResponse* response);
XModbusResponse* XModbusResponse_create_move(XModbusResponse* response);
/**
 * @brief 创建带功能码的XModbusResponse
 * @param code 功能码
 * @return 成功返回XModbusResponse指针，失败返回NULL
 */
XModbusResponse* XModbusResponse_create_with_code(XModbusPdu_FunctionCode code);

/**
 * @brief 初始化已分配的XModbusResponse实例
 * @param resp XModbusResponse指针（非NULL）
 */
void XModbusResponse_init(XModbusResponse* resp);

/**
 * @brief 初始化已分配的XModbusResponse实例（带功能码）
 * @param resp XModbusResponse指针（非NULL）
 * @param code 功能码
 */
void XModbusResponse_init_with_code(XModbusResponse* resp, XModbusPdu_FunctionCode code);


/******************************************************************************************
 * XModbusExceptionResponse 创建/初始化接口
 ******************************************************************************************/

 /**
  * @brief 在堆上创建并初始化一个XModbusExceptionResponse实例
  * @return 成功返回XModbusExceptionResponse指针，失败返回NULL
  */
XModbusExceptionResponse* XModbusExceptionResponse_create(void);
XModbusExceptionResponse* XModbusExceptionResponse_create_copy(const XModbusExceptionResponse* res);
XModbusExceptionResponse* XModbusExceptionResponse_create_move(XModbusExceptionResponse* res);
/**
 * @brief 创建带功能码和异常码的XModbusExceptionResponse
 * @param functionCode 功能码
 * @param exceptionCode 异常码
 * @return 成功返回XModbusExceptionResponse指针，失败返回NULL
 */
XModbusExceptionResponse* XModbusExceptionResponse_create_with_function_and_exception(
    XModbusPdu_FunctionCode functionCode, XModbusPdu_ExceptionCode exceptionCode);

/**
 * @brief 初始化已分配的XModbusExceptionResponse实例
 * @param exc XModbusExceptionResponse指针（非NULL）
 */
void XModbusExceptionResponse_init(XModbusExceptionResponse* exc);

/**
brief 初始化已分配的XModbusExceptionResponse实例（带功能码和异常码）
param exc XModbusExceptionResponse指针（非NULL）
param functionCode 功能码
param exceptionCode 异常码
*/
/**
 * @brief 初始化异常响应（带功能码和异常码）
 * @param exc 异常响应指针（非NULL）
 * @param functionCode 原始请求的功能码
 * @param exceptionCode 异常码
 */
void XModbusExceptionResponse_init_with_function_and_exception(
    XModbusExceptionResponse* exc, XModbusPdu_FunctionCode functionCode, XModbusPdu_ExceptionCode exceptionCode);

/**
 * @brief 设置异常响应的异常码
 * @param exc XModbusExceptionResponse指针（非NULL）
 * @param ec 异常码
 */
void XModbusExceptionResponse_setExceptionCode(XModbusExceptionResponse* exc, XModbusPdu_ExceptionCode ec);

/******************************************************************************************
 * 核心查询与操作接口 (对齐 QModbusPdu)
 ******************************************************************************************/

/**
 * @brief 检查PDU是否有效
 * @param pdu XModbusPdu指针
 * @return 有效返回true，无效返回false
 * @par 有效性判断
 * - 功能码在有效范围内
 * - 数据长度小于253字节（Modbus PDU最大数据长度）
 */
bool XModbusPdu_isValid(const XModbusPdu* pdu);

/**
 * @brief 检查是否为异常响应
 * @param pdu XModbusPdu指针
 * @return 是异常响应返回true，否则返回false
 * @note 功能码最高位被设置(0x80)时为异常响应
 */
bool XModbusPdu_isException(const XModbusPdu* pdu);

/**
 * @brief 获取异常码
 * @param pdu XModbusPdu指针
 * @return 异常码，如果不是异常响应返回0
 */
XModbusPdu_ExceptionCode XModbusPdu_exceptionCode(const XModbusPdu* pdu);

/**
 * @brief 获取PDU总大小
 * @param pdu XModbusPdu指针
 * @return PDU总大小（功能码 + 数据），单位字节
 */
int16_t XModbusPdu_size(const XModbusPdu* pdu);

/**
 * @brief 获取PDU数据部分大小
 * @param pdu XModbusPdu指针
 * @return 数据部分大小，单位字节
 */
int16_t XModbusPdu_dataSize(const XModbusPdu* pdu);

/**
 * @brief 获取原始功能码（包含异常位）
 * @param pdu XModbusPdu指针
 * @return 原始功能码
 */
XModbusPdu_FunctionCode XModbusPdu_functionCodeRaw(const XModbusPdu* pdu);

/**
 * @brief 获取实际功能码（剥离异常位）
 * @param pdu XModbusPdu指针
 * @return 实际功能码
 * @note 自动清除最高位的异常标志
 */
XModbusPdu_FunctionCode XModbusPdu_functionCode(const XModbusPdu* pdu);

/**
 * @brief 设置功能码
 * @param pdu XModbusPdu指针（非NULL）
 * @param code 功能码
 */
void XModbusPdu_setFunctionCode(XModbusPdu* pdu, XModbusPdu_FunctionCode code);

/**
 * @brief 获取数据的拷贝
 * @param pdu XModbusPdu指针
 * @return 数据的XByteArray副本，调用者负责释放
 * @note 返回NULL表示无数据
 */
XByteArray* XModbusPdu_data(const XModbusPdu* pdu);

/**
 * @brief 设置数据（深拷贝）
 * @param pdu XModbusPdu指针（非NULL）
 * @param newData 数据指针
 * @param size 数据大小
 * @note 数据会被内部复制
 */
void XModbusPdu_setData(XModbusPdu* pdu, const uint8_t* newData, size_t size);

/******************************************************************************************
 * 数据大小计算器（对齐Qt6 QModbusRequest/QModbusResponse）
 * @details 用于注册和查询特定功能码的PDU最小数据大小和实际数据大小计算方法。
 *          支持标准Modbus功能码的默认实现，也允许用户注册自定义计算器。
 */

/**
 * @brief 请求数据大小计算函数指针类型
 * @param pdu 请求PDU指针
 * @return 计算出的数据大小，-1表示未知
 */
typedef int16_t (*XModbusRequest_CalcFuncPtr)(const XModbusRequest* pdu);

/**
 * @brief 响应数据大小计算函数指针类型
 * @param pdu 响应PDU指针
 * @return 计算出的数据大小，-1表示未知
 */
typedef int16_t (*XModbusResponse_CalcFuncPtr)(const XModbusResponse* pdu);

/**
 * @brief 获取请求PDU的最小数据大小（不含功能码字节）
 * @param pdu 请求PDU指针
 * @return 最小数据大小（字节），-1表示未知功能码
 */
int16_t XModbusRequest_minimumDataSize(const XModbusRequest* pdu);

/**
 * @brief 计算请求PDU的实际数据大小（不含功能码字节）
 * @param pdu 请求PDU指针
 * @return 实际数据大小（字节），-1表示无法确定
 */
int16_t XModbusRequest_calculateDataSize(const XModbusRequest* pdu);

/**
 * @brief 注册请求PDU数据大小计算器
 * @param fc 功能码
 * @param func 计算函数指针，传入NULL恢复默认实现
 */
void XModbusRequest_registerDataSizeCalculator(XModbusPdu_FunctionCode fc, XModbusRequest_CalcFuncPtr func);

/**
 * @brief 获取响应PDU的最小数据大小（不含功能码字节）
 * @param pdu 响应PDU指针
 * @return 最小数据大小（字节），-1表示未知功能码
 */
int16_t XModbusResponse_minimumDataSize(const XModbusResponse* pdu);

/**
 * @brief 计算响应PDU的实际数据大小（不含功能码字节）
 * @param pdu 响应PDU指针
 * @return 实际数据大小（字节），-1表示无法确定
 */
int16_t XModbusResponse_calculateDataSize(const XModbusResponse* pdu);

/**
 * @brief 注册响应PDU数据大小计算器
 * @param fc 功能码
 * @param func 计算函数指针，传入NULL恢复默认实现
 */
void XModbusResponse_registerDataSizeCalculator(XModbusPdu_FunctionCode fc, XModbusResponse_CalcFuncPtr func);


/******************************************************************************************
 * 内存管理宏 - XModbusPdu
 ******************************************************************************************/

/**
 * @brief 复制PDU
 * @param dest 目标PDU
 * @param src 源PDU
 */
#define XModbusPdu_copy_base        XClass_copy_base

/**
 * @brief 移动PDU
 * @param dest 目标PDU
 * @param src 源PDU
 */
#define XModbusPdu_move_base        XClass_move_base

/**
 * @brief 析构PDU
 * @param pdu PDU指针
 */
#define XModbusPdu_deinit_base      XClass_deinit_base

/**
 * @brief 删除PDU（立即释放）
 * @param pdu PDU指针
 */
#define XModbusPdu_delete_base      XClass_delete_base

/******************************************************************************************
* 内存管理宏 - XModbusRequest
******************************************************************************************/

/**
 * @brief 复制请求
 * @param dest 目标请求
 * @param src 源请求
 */
#define XModbusRequest_copy_base        XModbusPdu_copy_base

/**
 * @brief 移动请求
 * @param dest 目标请求
 * @param src 源请求
 */
#define XModbusRequest_move_base        XModbusPdu_move_base

/**
 * @brief 析构请求
 * @param req 请求指针
 */
#define XModbusRequest_deinit_base      XModbusPdu_deinit_base

/**
 * @brief 删除请求（立即释放）
 * @param req 请求指针
 */
#define XModbusRequest_delete_base      XModbusPdu_delete_base

/******************************************************************************************
* 内存管理宏 - XModbusResponse
******************************************************************************************/

/**
 * @brief 复制响应
 * @param dest 目标响应
 * @param src 源响应
 */
#define XModbusResponse_copy_base       XModbusPdu_copy_base

/**
* @brief 移动响应
* @param dest 目标响应
* @param src 源响应
*/
#define XModbusResponse_move_base       XModbusPdu_move_base

/**
* @brief 析构响应
* @param resp 响应指针
*/
#define XModbusResponse_deinit_base     XModbusPdu_deinit_base

/**
* @brief 删除响应（立即释放）
* @param resp 响应指针
*/
#define XModbusResponse_delete_base     XModbusPdu_delete_base

/******************************************************************************************
* 内存管理宏 - XModbusExceptionResponse
******************************************************************************************/

/**
 * @brief 复制异常响应
 * @param dest 目标异常响应
 * @param src 源异常响应
 */
#define XModbusExceptionResponse_copy_base      XModbusResponse_copy_base

/**
* @brief 移动异常响应
* @param dest 目标异常响应
* @param src 源异常响应
*/
#define XModbusExceptionResponse_move_base      XModbusResponse_move_base

/**
* @brief 析构异常响应
* @param exc 异常响应指针
*/
#define XModbusExceptionResponse_deinit_base    XModbusResponse_deinit_base

/**
* @brief 删除异常响应（立即释放）
* @param exc 异常响应指针
*/
#define XModbusExceptionResponse_delete_base    XModbusResponse_delete_base

#ifdef __cplusplus
}
#endif

#endif // XMODBUSPDU_H


