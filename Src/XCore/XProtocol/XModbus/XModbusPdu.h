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
 * @par 释放约定
 * - 本文件四个类统一经 XClass 多态入口释放：栈对象 XClassDeinit、
 *   堆对象 XClassDelete。
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
 * XClassDelete(request);
 * @endcode
 */

#ifndef XMODBUSPDU_H
#define XMODBUSPDU_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "XClass.h"
#include "XTypes.h"

#ifdef __cplusplus
extern "C" {
#endif

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
 * | 0x07 | NegativeAcknowledge | 否定确认（无 dedicated 处理动作） |
 * | 0x08 | MemoryParityError | 内存奇偶校验错误 |
 * | 0x0A | GatewayPathUnavailable | 网关路径不可用 |
 * | 0x0B | GatewayTargetDeviceFailedToRespond | 网关目标设备无响应 |
 * | 0xFF | ExtendedException | 扩展异常码（本库哨兵：无法解析异常码时的返回值） |
 *
 * @note 枚举值即异常响应数据首字节的协议取值，直接上/下线传输，无重编码。
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
 *
 * @note XModbusPdu_Invalid(0x00) 是初始/无效值；XModbusPdu_UndefinedFunctionCode
 *       (0x100) 是哨兵值（isValid 的功能码上界），两者都不是协议功能码。
 *       功能码存储在 m_code 中原样保存；异常响应在该值上再置最高位（OR 0x80）。
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

/**
 * @brief XModbusPdu_encodeData/decodeData 支持的字段类型
 * @note 对齐 QModbusPdu::encodeData/decodeData 支持的 quint8 与 quint16。
 */
typedef enum {
    XModbusPdu_DataType_Uint8,                  /**< 8 位字段：PDU 中 1 字节，原样拷贝。 */
    XModbusPdu_DataType_Uint16                  /**< 16 位字段：PDU 中 2 字节，Modbus 大端序。 */
} XModbusPdu_DataType;

/**
 * @brief PDU 编码字段（encodeData 的单个输入描述）。
 * @note uint16_t 输入使用主机字节序表示，编码到 PDU 时转换为 Modbus 大端序。
 */
typedef struct {
    XModbusPdu_DataType type;   /**< 字段元素类型（Uint8/Uint16；其他值整组字段被拒绝）。 */
    const void* data;           /**< 输入缓冲区（uint8_t/uint16_t 标量或数组的地址，
                                     借用、不取得所有权；count>0 时不能为 NULL，
                                     count==0 时可为 NULL 且被跳过）。 */
    size_t count;               /**< 元素数量（单位=个，非字节）；标量传 1。 */
} XModbusPdu_EncodeField;

/**
 * @brief PDU 解码字段（decodeData 的单个输出描述）。
 * @note uint16_t 输出使用主机字节序表示，输入 PDU 按 Modbus 大端序解析。
 */
typedef struct {
    XModbusPdu_DataType type;   /**< 字段元素类型（Uint8/Uint16；其他值整组字段被拒绝）。 */
    void* data;                 /**< 输出缓冲区（调用方提供、可写；函数写入
                                     count 个元素；count>0 时不能为 NULL）。 */
    size_t count;               /**< 元素数量（单位=个，非字节）；标量传 1。 */
} XModbusPdu_DecodeField;

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
XCLASS_DEFINE_BEGING(XModbusPdu)
XCLASS_DEFINE_EXTEND_END(XModbusPdu, XClass)

typedef struct XModbusPdu {
    XClass m_class;                             /**< 基类（第一成员），由 XClass 管理，禁止手工修改。 */
    XModbusPdu_FunctionCode m_code;             /**< 原始功能码（异常响应时最高位被置位，见 functionCodeRaw）。 */
    XByteArray* m_data;                         /**< PDU 数据部分（不含功能码字节），对象拥有、deinit 时释放。 */
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
/**
 * @brief 请求优先级（任意整数值；XinYueC 扩展，Qt 的 QModbusRequest 无对应概念）。
 * @details 对齐 Qt 事件队列语义（QEvent::priority + QCoreApplication::postEvent）：
 *          值越大越先出队；**支持任意等级**，不限于两档。相同优先级的请求保持
 *          FIFO（先入队者先发出）。供多优先队列的传输实现排序出队——Modbus RTU
 *          主站（485 半双工单总线）与单事务模式下的 Modbus/TCP 主站据此降低
 *          控制命令的排队延迟（已发出的帧不可抢占，优先级只作用于等待中的请求）。
 */
typedef int XModbusRequestPriority;

/** @brief 普通优先级（默认；状态轮询/参数读写等） */
#define XModbusRequestPriority_Normal 0
/** @brief 高优先级（控制命令等，需快速响应） */
#define XModbusRequestPriority_High 1

typedef struct XModbusRequest {
    XModbusPdu m_base;                          /**< 基类（第一成员），由 XModbusPdu 管理，禁止手工修改。 */
    int m_priority;                             ///< 请求优先级（XModbusRequestPriority；默认 Normal=0；
                                              ///< 任意整数值，值越大越先出队）。
                                              ///< 供支持多优先队列的传输（如 Modbus RTU 主站）排序出队用；
                                              ///< Qt 的 QModbusRequest 无对应概念，属 XinYueC 扩展字段。
} XModbusRequest;

/**
 * @brief Modbus响应PDU结构体
 * @details 继承自XModbusPdu，用于封装Modbus响应
 */
typedef struct XModbusResponse {
    XModbusPdu m_base;                          /**< 基类（第一成员），由 XModbusPdu 管理，禁止手工修改。 */
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
    XModbusResponse m_base;                     /**< 基类（第一成员），由 XModbusResponse 管理，禁止手工修改。 */
} XModbusExceptionResponse;

/******************************************************************************************
 * 类初始化接口
 ******************************************************************************************/

 /**
  * @brief 初始化XModbusPdu的虚函数表（单例，进程内一次）。
  * @return 类虚函数表指针；不可释放。
  * @note 重载了 XClass 的 copy/move/deinit：copy 深拷贝数据，move 转移
  *       m_data 所有权并清空源对象，deinit 释放 m_data。
  */
XVtable* XModbusPdu_class_init(void);

/**
 * @brief 初始化XModbusRequest的虚函数表。
 * @return 类虚函数表指针；不可释放。
 * @note Request 未新增虚函数，实现直接返回 XModbusPdu_class_init() 的同一
 *       虚函数表（与 Pdu 共享 copy/move/deinit）。
 */
XVtable* XModbusRequest_class_init(void);

/**
 * @brief 初始化XModbusResponse的虚函数表。
 * @return 类虚函数表指针；不可释放。
 * @note Response 未新增虚函数，实现直接返回 XModbusPdu_class_init() 的同一
 *       虚函数表（与 Pdu 共享 copy/move/deinit）。
 */
XVtable* XModbusResponse_class_init(void);

/**
 * @brief 初始化XModbusExceptionResponse的虚函数表。
 * @return 类虚函数表指针；不可释放。
 * @note ExceptionResponse 未新增虚函数，实现经 XModbusResponse_class_init()
 *       返回 XModbusPdu_class_init() 的同一虚函数表（与 Pdu 共享 copy/move/deinit）。
 */
XVtable* XModbusExceptionResponse_class_init(void);

/******************************************************************************************
 * XModbusPdu 创建/初始化接口
 ******************************************************************************************/

 /**
  * @brief 在堆上创建一个空 PDU（功能码 Invalid、数据为空）。
  * @param memory 内存分配器类型（XMemoryType 枚举；分配与所有权标记按
  *               同一类型登记）。
  * @return 新对象（调用方 XClassDelete 释放）；内存分配失败返回 NULL。
  */
XModbusPdu* XModbusPdu_create_ex(XMemoryType memory);

/**
 * @brief 创建 XModbusPdu 的深拷贝。
 * @param pdu 源对象（借用，不取得所有权）；NULL 返回 NULL。
 * @return 新对象（调用方 XClassDelete 释放）；创建或拷贝失败返回 NULL。
 * @note 深拷贝功能码与数据，源对象保持不变；拷贝经 XClassCopy 多态分派
 *       （目标未初始化时内部先 init）。
 */
XModbusPdu* XModbusPdu_create_copy(const XModbusPdu* pdu);
/**
 * @brief 通过移动语义创建 XModbusPdu（转移源对象的数据所有权）。
 * @param pdu 源对象；NULL 返回 NULL。
 * @return 新对象（调用方 XClassDelete 释放）；创建失败返回 NULL。
 * @warning 移动成功后源对象被清空（m_code 复位为 XModbusPdu_Invalid、
 *          m_data 置 NULL），源对象仍由其创建者按原分配方式释放
 *          （栈对象 XClassDeinit、堆对象 XClassDelete）。
 */
XModbusPdu* XModbusPdu_create_move(XModbusPdu* pdu);
/**
 * @brief 创建带功能码的XModbusPdu。
 * @param code 功能码（原样存储，不做范围校验）。
 * @return 新对象（调用方 XClassDelete 释放）；内存分配失败返回 NULL。
 * @note 使用默认内存分配器（XCLASS_DEFAULT_MEMORY_TYPE），与 create_ex
 *       的差异仅在于分配器来源。
 */
XModbusPdu* XModbusPdu_create_with_code(XModbusPdu_FunctionCode code);

/**
 * @brief 栈上/就地初始化 XModbusPdu（功能码 Invalid、数据为空）。
 * @param pdu 目标对象；NULL 时不执行任何操作；可为未初始化内存
 *        （派生字段内部清零）。
 * @return 无。
 * @note 仅用于未初始化内存（对已初始化对象重复调用会覆盖 m_data 属主
 *       指针导致泄漏）；完成后与 XClassDeinit/XClassDelete 成对使用。
 */
void XModbusPdu_init(XModbusPdu* pdu);

/**
 * @brief 初始化已分配的XModbusPdu实例（带功能码）。
 * @param pdu 目标对象；NULL 时仅按 init 语义安全返回（不执行任何操作）。
 * @param code 功能码（原样存储，不做范围校验）。
 * @return 无。
 * @note 前置条件与注意事项同 XModbusPdu_init。
 */
void XModbusPdu_init_with_code(XModbusPdu* pdu, XModbusPdu_FunctionCode code);

/******************************************************************************************
 * XModbusRequest 创建/初始化接口
 ******************************************************************************************/

 /**
  * @brief 在堆上创建一个空请求 PDU（功能码 Invalid、优先级 Normal）。
  * @param memory 内存分配器类型（XMemoryType 枚举；分配与所有权标记按
  *               同一类型登记）。
  * @return 新对象（调用方 XClassDelete 释放）；内存分配失败返回 NULL。
  */
XModbusRequest* XModbusRequest_create_ex(XMemoryType memory);
/**
 * @brief 创建 XModbusRequest 的深拷贝。
 * @param req 源对象（借用，不取得所有权）；NULL 返回 NULL。
 * @return 新对象（调用方 XClassDelete 释放）；创建或拷贝失败返回 NULL。
 * @note 深拷贝功能码、数据与 m_priority，源对象保持不变。
 */
XModbusRequest* XModbusRequest_create_copy(const XModbusRequest* req);
/**
 * @brief 通过移动语义创建 XModbusRequest（转移源对象的数据所有权）。
 * @param req 源对象；NULL 返回 NULL。
 * @return 新对象（调用方 XClassDelete 释放）；创建失败返回 NULL。
 * @warning 移动成功后源对象被清空（功能码复位、m_data 置 NULL），源对象
 *          仍由其创建者按原分配方式释放（栈对象 XClassDeinit、堆对象
 *          XClassDelete）。
 */
XModbusRequest* XModbusRequest_create_move(XModbusRequest* req);
/**
 * @brief 创建带功能码的XModbusRequest（优先级 Normal）。
 * @param code 功能码（原样存储，不做范围校验）。
 * @return 新对象（调用方 XClassDelete 释放）；内存分配失败返回 NULL。
 */
XModbusRequest* XModbusRequest_create_with_code(XModbusPdu_FunctionCode code);

/**
 * @brief 栈上/就地初始化 XModbusRequest（功能码 Invalid、优先级 Normal）。
 * @param req 目标对象；NULL 时不执行任何操作；可为未初始化内存。
 * @return 无。
 * @note 仅用于未初始化内存；内部完成 Pdu 基类初始化并把 m_priority 置为
 *       XModbusRequestPriority_Normal；完成后与 XClassDeinit/XClassDelete
 *       成对使用。
 */
void XModbusRequest_init(XModbusRequest* req);

/**
 * @brief 初始化已分配的XModbusRequest实例（带功能码）。
 * @param req 目标对象；NULL 时安全返回（不执行任何操作）。
 * @param code 功能码（原样存储，不做范围校验）。
 * @return 无。
 * @note 前置条件与注意事项同 XModbusRequest_init。
 */
void XModbusRequest_init_with_code(XModbusRequest* req, XModbusPdu_FunctionCode code);

/******************************************************************************************
 * XModbusResponse 创建/初始化接口
 ******************************************************************************************/

 /**
  * @brief 在堆上创建一个空响应 PDU（功能码 Invalid、数据为空）。
  * @param memory 内存分配器类型（XMemoryType 枚举；分配与所有权标记按
  *               同一类型登记）。
  * @return 新对象（调用方 XClassDelete 释放）；内存分配失败返回 NULL。
  */
XModbusResponse* XModbusResponse_create_ex(XMemoryType memory);
/**
 * @brief 创建 XModbusResponse 的深拷贝。
 * @param response 源对象（借用，不取得所有权）；NULL 时不拷贝，返回内容
 *        为空（功能码 Invalid）的新对象。
 * @return 新对象（调用方 XClassDelete 释放）；创建失败返回 NULL。
 * @note 深拷贝功能码与数据，源对象保持不变。
 */
XModbusResponse* XModbusResponse_create_copy(XModbusResponse* response);
/**
 * @brief 通过移动语义创建 XModbusResponse（转移源对象的数据所有权）。
 * @param response 源对象；NULL 时转移被跳过，返回内容为空的新对象。
 * @return 新对象（调用方 XClassDelete 释放）；创建失败返回 NULL。
 * @warning 移动成功后源对象被清空（功能码复位、m_data 置 NULL），源对象
 *          仍由其创建者按原分配方式释放（栈对象 XClassDeinit、堆对象
 *          XClassDelete）。
 */
XModbusResponse* XModbusResponse_create_move(XModbusResponse* response);
/**
 * @brief 创建带功能码的XModbusResponse。
 * @param code 功能码（原样存储，不做范围校验）。
 * @return 新对象（调用方 XClassDelete 释放）；内存分配失败返回 NULL。
 */
XModbusResponse* XModbusResponse_create_with_code(XModbusPdu_FunctionCode code);

/**
 * @brief 栈上/就地初始化 XModbusResponse（功能码 Invalid、数据为空）。
 * @param resp 目标对象；NULL 时不执行任何操作；可为未初始化内存。
 * @return 无。
 * @note 仅用于未初始化内存；完成后与 XClassDeinit/XClassDelete 成对使用。
 */
void XModbusResponse_init(XModbusResponse* resp);

/**
 * @brief 初始化已分配的XModbusResponse实例（带功能码）。
 * @param resp 目标对象；NULL 时安全返回（不执行任何操作）。
 * @param code 功能码（原样存储，不做范围校验）。
 * @return 无。
 * @note 前置条件与注意事项同 XModbusResponse_init。
 */
void XModbusResponse_init_with_code(XModbusResponse* resp, XModbusPdu_FunctionCode code);


/******************************************************************************************
 * XModbusExceptionResponse 创建/初始化接口
 ******************************************************************************************/

 /**
  * @brief 在堆上创建一个空异常响应 PDU（功能码 Invalid、数据为空）。
  * @param memory 内存分配器类型（XMemoryType 枚举；分配与所有权标记按
  *               同一类型登记）。
  * @return 新对象（调用方 XClassDelete 释放）；内存分配失败返回 NULL。
  */
XModbusExceptionResponse* XModbusExceptionResponse_create_ex(XMemoryType memory);
/**
 * @brief 创建 XModbusExceptionResponse 的深拷贝。
 * @param res 源对象（借用，不取得所有权）；NULL 时不拷贝，返回内容为空
 *        的新对象。
 * @return 新对象（调用方 XClassDelete 释放）；创建失败返回 NULL。
 * @note 深拷贝原始功能码（含异常位）与异常码数据，源对象保持不变。
 */
XModbusExceptionResponse* XModbusExceptionResponse_create_copy(const XModbusExceptionResponse* res);
/**
 * @brief 通过移动语义创建 XModbusExceptionResponse（转移源对象的数据所有权）。
 * @param res 源对象；NULL 时转移被跳过，返回内容为空的新对象。
 * @return 新对象（调用方 XClassDelete 释放）；创建失败返回 NULL。
 * @warning 移动成功后源对象被清空（功能码复位、m_data 置 NULL），源对象
 *          仍由其创建者按原分配方式释放（栈对象 XClassDeinit、堆对象
 *          XClassDelete）。
 */
XModbusExceptionResponse* XModbusExceptionResponse_create_move(XModbusExceptionResponse* res);
/**
 * @brief 创建带功能码和异常码的XModbusExceptionResponse。
 * @param functionCode 原始请求的功能码（内部自动 OR 0x80 异常位后存储）。
 * @param exceptionCode 异常码（作为 1 字节数据写入 PDU）。
 * @return 新对象（调用方 XClassDelete 释放）；内存分配失败返回 NULL。
 * @note 等价于 create() 后调用 init_with_function_and_exception 的语义
 *       （实现只补设功能码与数据，避免重复初始化泄漏内部容器）。
 */
XModbusExceptionResponse* XModbusExceptionResponse_create_with_function_and_exception(
    XModbusPdu_FunctionCode functionCode, XModbusPdu_ExceptionCode exceptionCode);

/**
 * @brief 栈上/就地初始化 XModbusExceptionResponse（功能码 Invalid、数据为空）。
 * @param exc 目标对象；NULL 时不执行任何操作；可为未初始化内存。
 * @return 无。
 * @note 仅用于未初始化内存；完成后与 XClassDeinit/XClassDelete 成对使用。
 */
void XModbusExceptionResponse_init(XModbusExceptionResponse* exc);

/**
 * @brief 初始化异常响应（带功能码和异常码）。
 * @param exc 目标对象；NULL 时安全返回（不执行任何操作）。
 * @param functionCode 原始请求的功能码（内部自动 OR 0x80 异常位后存储）。
 * @param exceptionCode 异常码（作为 1 字节数据写入 PDU，替换原有数据）。
 * @return 无。
 * @note 前置条件与注意事项同 XModbusExceptionResponse_init。
 */
void XModbusExceptionResponse_init_with_function_and_exception(
    XModbusExceptionResponse* exc, XModbusPdu_FunctionCode functionCode, XModbusPdu_ExceptionCode exceptionCode);

/**
 * @brief 替换异常响应的异常码（数据变为 1 字节，功能码不变）。
 * @param exc 目标对象；NULL 时不执行任何操作。
 * @param ec 新异常码（截断为 1 字节写入 PDU 数据）。
 * @return 无。
 */
void XModbusExceptionResponse_setExceptionCode(XModbusExceptionResponse* exc, XModbusPdu_ExceptionCode ec);

/******************************************************************************************
 * 核心查询与操作接口 (对齐 QModbusPdu)
 ******************************************************************************************/

/**
 * @brief 检查PDU是否有效。
 * @param pdu 目标对象；NULL 返回 false。
 * @return 功能码 ∈ [XModbusPdu_ReadCoils, XModbusPdu_UndefinedFunctionCode)
 *         且数据长度 < 253 字节时返回 true；否则返回 false。
 * @note 异常响应（原始功能码带 0x80 位）同样落在功能码范围内，因此
 *       isValid 对异常响应也返回 true；未初始化（Invalid 功能码）的
 *       PDU 返回 false。
 */
bool XModbusPdu_isValid(const XModbusPdu* pdu);

/**
 * @brief 检查是否为异常响应。
 * @param pdu 目标对象；NULL 返回 false。
 * @return 原始功能码最高位（0x80）被设置返回 true；否则返回 false。
 */
bool XModbusPdu_isException(const XModbusPdu* pdu);

/**
 * @brief 获取异常码（异常响应数据的首字节）。
 * @param pdu 目标对象。
 * @return 异常响应且数据非空时返回数据首字节；pdu 为 NULL、非异常响应
 *         或数据为空时返回哨兵值 XModbusPdu_ExtendedException (0xFF)。
 * @note 与 Qt 不同（Qt 返回 0），本实现用 ExtendedException 表示
 *       「无异常码可取」。
 */
XModbusPdu_ExceptionCode XModbusPdu_exceptionCode(const XModbusPdu* pdu);

/**
 * @brief 获取PDU总大小。
 * @param pdu 目标对象；NULL 返回 0。
 * @return PDU 总字节数（功能码 1 字节 + 数据部分）。
 */
int16_t XModbusPdu_size(const XModbusPdu* pdu);

/**
 * @brief 获取PDU数据部分大小。
 * @param pdu 目标对象；NULL 返回 0。
 * @return 数据部分字节数（不含功能码）。
 */
int16_t XModbusPdu_dataSize(const XModbusPdu* pdu);

/**
 * @brief 获取原始功能码（包含异常位）。
 * @param pdu 目标对象；NULL 返回 XModbusPdu_Invalid。
 * @return m_code 原值（异常响应时最高位被置位）。
 */
XModbusPdu_FunctionCode XModbusPdu_functionCodeRaw(const XModbusPdu* pdu);

/**
 * @brief 获取实际功能码（剥离异常位）。
 * @param pdu 目标对象；NULL 返回 XModbusPdu_Invalid。
 * @return m_code 清除最高位（0x80）后的功能码。
 */
XModbusPdu_FunctionCode XModbusPdu_functionCode(const XModbusPdu* pdu);

/**
 * @brief 设置功能码。
 * @param pdu 目标对象；NULL 时不执行任何操作。
 * @param code 新功能码（原样存储，不做范围校验；异常响应需调用方自行
 *        OR 0x80）。
 * @return 无。
 */
void XModbusPdu_setFunctionCode(XModbusPdu* pdu, XModbusPdu_FunctionCode code);

/**
 * @brief 获取数据的拷贝。
 * @param pdu 目标对象；NULL 返回 NULL。
 * @return 新 XByteArray 对象（PDU 数据的深拷贝，调用方 XClassDelete
 *         释放）；数据为空时返回空数组对象（与 Qt 返回引用不同）。
 */
XByteArray* XModbusPdu_data(const XModbusPdu* pdu);

/**
 * @brief 替换 PDU 数据（深拷贝）。
 * @param pdu 目标对象；NULL 或内部容器缺失时不执行任何操作。
 * @param newData 源数据（借用，函数内部复制，不取得所有权）；
 *        NULL 或 size==0 时清空 PDU 数据。
 * @param size 数据字节数。
 * @return 无。
 * @note 拷贝失败（内存分配）时数据可能不完整，无错误返回值。
 */
void XModbusPdu_setData(XModbusPdu* pdu, const uint8_t* newData, size_t size);

/**
 * @brief 按 Modbus 大端序编码 uint8_t/uint16_t 字段序列并替换 PDU 数据。
 * @details 等价于 QModbusPdu::encodeData()。先整体校验字段类型与总长度
 *          （≤252 字节），全部通过后才清空并写入。
 * @param pdu 目标对象；NULL 或内部容器缺失返回 false。
 * @param fields 字段数组（借用，仅调用期间使用；fieldCount>0 时不能为
 *        NULL）；每个字段的 data 借用、不取得所有权，count==0 的字段
 *        跳过（data 可为 NULL）。
 * @param fieldCount 字段数量；0 表示清空 PDU 数据并返回 true。
 * @return 全部字段编码成功返回 true（替换原数据）；字段类型非法、
 *         fields 为 NULL 而 fieldCount>0、或累计长度超 252 字节返回
 *         false 且原数据保持不变；写入中途内存分配失败返回 false 且
 *         PDU 数据被清空。
 * @note uint16_t 字段按主机字节序读入、Modbus 大端序写出。
 */
bool XModbusPdu_encodeData(XModbusPdu* pdu,
    const XModbusPdu_EncodeField* fields, size_t fieldCount);

/**
 * @brief 按 Modbus 大端序把 PDU 数据解码为 uint8_t/uint16_t 字段序列。
 * @details 等价于 QModbusPdu::decodeData()。逐字段校验后立即写入该字段，
 *          不要求 PDU 数据被完整消费（允许剩余尾部字节）。
 * @param pdu 源对象（只读，本函数不修改 PDU）；NULL 或内部容器缺失
 *        返回 false。
 * @param fields 输出字段数组（函数向每个字段的 data 写入 count 个元素；
 *        fieldCount>0 时不能为 NULL）。
 * @param fieldCount 字段数量。
 * @return 全部字段解码成功返回 true；pdu/fields 为 NULL、字段类型非法
 *         或 PDU 剩余数据不足返回 false——失败时先前已解码的字段保持
 *         已写入状态，后续字段未被触碰。
 * @note uint16_t 字段按 Modbus 大端序读入、主机字节序写出。
 */
bool XModbusPdu_decodeData(const XModbusPdu* pdu,
    const XModbusPdu_DecodeField* fields, size_t fieldCount);

/******************************************************************************************
 * 数据大小计算器（对齐Qt6 QModbusRequest/QModbusResponse）
 * @details 用于注册和查询特定功能码的PDU最小数据大小和实际数据大小计算方法。
 *          支持标准Modbus功能码的默认实现，也允许用户注册自定义计算器。
 */

/**
 * @brief 请求数据大小计算函数指针类型（用户自定义计算器）。
 * @param pdu 请求PDU指针（只读借用；可能与注册表遍历中的任意请求实例无关联）。
 * @return 数据字节数（不含功能码）；无法确定返回 -1。
 */
typedef int16_t (*XModbusRequest_CalcFuncPtr)(const XModbusRequest* pdu);

/**
 * @brief 响应数据大小计算函数指针类型（用户自定义计算器）。
 * @param pdu 响应PDU指针（只读借用）。
 * @return 数据字节数（不含功能码）；无法确定返回 -1。
 */
typedef int16_t (*XModbusResponse_CalcFuncPtr)(const XModbusResponse* pdu);

/**
 * @brief 获取请求PDU的最小数据大小（不含功能码字节）。
 * @param pdu 请求PDU指针；NULL 返回 -1。
 * @return 按剥离异常位后的功能码查内置最小大小表（覆盖全部标准功能码；
 *         固定长度请求该值即实际大小），单位字节；未知功能码（Invalid/
 *         未定义）返回 -1。
 * @note 不查询用户注册表——本函数恒为内置表。
 */
int16_t XModbusRequest_minimumDataSize(const XModbusRequest* pdu);

/**
 * @brief 计算请求PDU的实际数据大小（不含功能码字节）。
 * @param pdu 请求PDU指针；NULL 返回 -1。
 * @return 数据字节数：固定长度请求返回最小大小；变长请求（写多线圈/
 *         写多寄存器/读写寄存器/文件记录/封装传输等）解析数据内的字节
 *         计数字段推算。自定义计算器（若已注册）优先于内置实现。
 *         无法确定（数据缺失、计数字段不可用、未知功能码）返回 -1。
 */
int16_t XModbusRequest_calculateDataSize(const XModbusRequest* pdu);

/**
 * @brief 注册请求PDU数据大小计算器（按功能码覆盖内置实现）。
 * @param fc 功能码。
 * @param func 计算函数指针（进程级常驻，调用方保持其可执行）；传入 NULL
 *        注销该功能码的注册（恢复内置实现）；func 为 NULL 且该功能码本就
 *        未注册时不执行任何操作。
 * @return 无。
 * @note 注册表为进程级静态数组，容量 32 条（请求/响应各自独立计）；
 *       同一功能码重复注册为覆盖；表满时新增注册被静默忽略；无锁，
 *       非线程安全，需在多线程启动前完成注册。
 */
void XModbusRequest_registerDataSizeCalculator(XModbusPdu_FunctionCode fc, XModbusRequest_CalcFuncPtr func);

/**
 * @brief 获取响应PDU的最小数据大小（不含功能码字节）。
 * @param pdu 响应PDU指针；NULL 返回 -1。
 * @return 异常响应固定返回 1（异常码字节）；否则按剥离异常位后的功能码
 *         查内置最小大小表，单位字节；未知功能码返回 -1。
 */
int16_t XModbusResponse_minimumDataSize(const XModbusResponse* pdu);

/**
 * @brief 计算响应PDU的实际数据大小（不含功能码字节）。
 * @param pdu 响应PDU指针；NULL 返回 -1。
 * @return 数据字节数：异常响应固定返回 1；固定长度响应查内置表；变长
 *         响应（诊断/事件计数/事件日志/文件记录/FIFO/封装传输等）返回
 *         实际数据字节数（FIFO 按前导 2 字节计数字段推算）。
 *         自定义计算器（若已注册）优先于内置实现。
 *         无法确定返回 -1。
 */
int16_t XModbusResponse_calculateDataSize(const XModbusResponse* pdu);

/**
 * @brief 注册响应PDU数据大小计算器（按功能码覆盖内置实现）。
 * @param fc 功能码。
 * @param func 计算函数指针（进程级常驻，调用方保持其可执行）；传入 NULL
 *        注销该功能码的注册（恢复内置实现）；func 为 NULL 且该功能码本就
 *        未注册时不执行任何操作。
 * @return 无。
 * @note 注册表为进程级静态数组，容量 32 条（与请求注册表相互独立）；
 *       同一功能码重复注册为覆盖；表满时新增注册被静默忽略；无锁，
 *       非线程安全，需在多线程启动前完成注册。
 */
void XModbusResponse_registerDataSizeCalculator(XModbusPdu_FunctionCode fc, XModbusResponse_CalcFuncPtr func);


/* ==================== 释放约定 ====================
 * XModbusPdu/XModbusRequest/XModbusResponse/XModbusExceptionResponse 四个类
 * 均不提供独立的 *_deinit/*_delete 宏或函数：栈/就地对象用 XClassDeinit
 * （经虚表分派，释放内部 m_data 并复位字段），堆对象用 XClassDelete
 * （反初始化并释放结构体内存）。
 */


#ifdef __cplusplus
}
#endif


/* XClass create API default-memory wrappers. */
#undef XModbusExceptionResponse_create
#define XModbusExceptionResponse_create() XModbusExceptionResponse_create_ex(XCLASS_DEFAULT_MEMORY_TYPE)
#undef XModbusPdu_create
#define XModbusPdu_create() XModbusPdu_create_ex(XCLASS_DEFAULT_MEMORY_TYPE)
#undef XModbusRequest_create
#define XModbusRequest_create() XModbusRequest_create_ex(XCLASS_DEFAULT_MEMORY_TYPE)
#undef XModbusResponse_create
#define XModbusResponse_create() XModbusResponse_create_ex(XCLASS_DEFAULT_MEMORY_TYPE)

#endif // XMODBUSPDU_H

