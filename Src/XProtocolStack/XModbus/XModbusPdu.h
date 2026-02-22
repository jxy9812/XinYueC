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
     * @brief Modbus协议数据单元(PDU)核心头文件（纯C风格，对齐Qt6 QModbusPdu）
     * @details 封装了Modbus PDU的核心功能，包括功能码、异常码和数据。
     */

     /**
      * @brief Modbus异常码枚举 (对齐 QModbusPdu::ExceptionCode)
      */
    typedef enum {
        XModbusPdu_IllegalFunction = 0x01,
        XModbusPdu_IllegalDataAddress = 0x02,
        XModbusPdu_IllegalDataValue = 0x03,
        XModbusPdu_ServerDeviceFailure = 0x04,
        XModbusPdu_Acknowledge = 0x05,
        XModbusPdu_ServerDeviceBusy = 0x06,
        XModbusPdu_NegativeAcknowledge = 0x07,
        XModbusPdu_MemoryParityError = 0x08,
        XModbusPdu_GatewayPathUnavailable = 0x0A,
        XModbusPdu_GatewayTargetDeviceFailedToRespond = 0x0B,
        XModbusPdu_ExtendedException = 0xFF
    } XModbusPdu_ExceptionCode;

    /**
     * @brief Modbus功能码枚举 (对齐 QModbusPdu::FunctionCode)
     */
    typedef enum {
        XModbusPdu_Invalid = 0x00,
        XModbusPdu_ReadCoils = 0x01,
        XModbusPdu_ReadDiscreteInputs = 0x02,
        XModbusPdu_ReadHoldingRegisters = 0x03,
        XModbusPdu_ReadInputRegisters = 0x04,
        XModbusPdu_WriteSingleCoil = 0x05,
        XModbusPdu_WriteSingleRegister = 0x06,
        XModbusPdu_ReadExceptionStatus = 0x07,
        XModbusPdu_Diagnostics = 0x08,
        XModbusPdu_GetCommEventCounter = 0x0B,
        XModbusPdu_GetCommEventLog = 0x0C,
        XModbusPdu_WriteMultipleCoils = 0x0F,
        XModbusPdu_WriteMultipleRegisters = 0x10,
        XModbusPdu_ReportServerId = 0x11,
        XModbusPdu_ReadFileRecord = 0x14,
        XModbusPdu_WriteFileRecord = 0x15,
        XModbusPdu_MaskWriteRegister = 0x16,
        XModbusPdu_ReadWriteMultipleRegisters = 0x17,
        XModbusPdu_ReadFifoQueue = 0x18,
        XModbusPdu_EncapsulatedInterfaceTransport = 0x2B,
        XModbusPdu_UndefinedFunctionCode = 0x100
    } XModbusPdu_FunctionCode;

    // 常量定义
#define XMODBUS_PDU_EXCEPTION_BYTE 0x80

/**
 * @struct XModbusPdu
 * @brief Modbus PDU 核心结构体 (继承自 XClass)
 */
    typedef struct XModbusPdu {
        XClass m_class;                     ///< 继承自 XClass
        XModbusPdu_FunctionCode m_code;     ///< 功能码（可能包含异常标志）
        XByteArray* m_data;                 ///< PDU 数据部分 (不包含功能码)
    } XModbusPdu;


    // =============== 派生类：XModbusRequest ===============
    typedef struct XModbusRequest {
        XModbusPdu m_base; // 必须是第一个成员！
    } XModbusRequest;

    // =============== 派生类：XModbusResponse ===============
    typedef struct XModbusResponse {
        XModbusPdu m_base; // 必须是第一个成员！
    } XModbusResponse;

    // =============== 派生类：XModbusExceptionResponse ===============
    typedef struct XModbusExceptionResponse {
        XModbusResponse m_base; // 继承自 Response
    } XModbusExceptionResponse;

    // =============== 虚表声明 ===============
    XVtable* XModbusPdu_class_init(void);
    XVtable* XModbusRequest_class_init(void);
    XVtable* XModbusResponse_class_init(void);
    XVtable* XModbusExceptionResponse_class_init(void);
    // =============== 创建/初始化函数 ===============
// --- XModbusPdu ---
    XModbusPdu* XModbusPdu_create(void);
    XModbusPdu* XModbusPdu_create_copy(XModbusPdu* pdu);
    XModbusPdu* XModbusPdu_create_with_code(XModbusPdu_FunctionCode code);
    XModbusPdu* XModbusPdu_create_with_code_and_data(XModbusPdu_FunctionCode code, const uint8_t* data, size_t dataSize);
    void XModbusPdu_init(XModbusPdu* pdu);
    void XModbusPdu_init_with_code(XModbusPdu* pdu, XModbusPdu_FunctionCode code);
    void XModbusPdu_init_with_code_and_data(XModbusPdu* pdu, XModbusPdu_FunctionCode code, const uint8_t* data, size_t dataSize);
    
    // --- XModbusRequest ---
    XModbusRequest* XModbusRequest_create(void);
    XModbusRequest* XModbusRequest_create_with_code(XModbusPdu_FunctionCode code);
    XModbusRequest* XModbusRequest_create_with_code_and_data(XModbusPdu_FunctionCode code, const uint8_t* data, size_t size);
    void XModbusRequest_init(XModbusRequest* req);
    void XModbusRequest_init_with_code(XModbusRequest* req, XModbusPdu_FunctionCode code);
    void XModbusRequest_init_with_code_and_data(XModbusRequest* req, XModbusPdu_FunctionCode code, const uint8_t* data, size_t size);

    // --- XModbusResponse ---
    XModbusResponse* XModbusResponse_create(void);
    XModbusResponse* XModbusResponse_create_with_code(XModbusPdu_FunctionCode code);
    XModbusResponse* XModbusResponse_create_with_code_and_data(XModbusPdu_FunctionCode code, const uint8_t* data, size_t size);
    void XModbusResponse_init(XModbusResponse* resp);
    void XModbusResponse_init_with_code(XModbusResponse* resp, XModbusPdu_FunctionCode code);
    void XModbusResponse_init_with_code_and_data(XModbusResponse* resp, XModbusPdu_FunctionCode code, const uint8_t* data, size_t size);

    // --- XModbusExceptionResponse ---
    XModbusExceptionResponse* XModbusExceptionResponse_create(void);
    XModbusExceptionResponse* XModbusExceptionResponse_create_with_function_and_exception(
        XModbusPdu_FunctionCode functionCode, XModbusPdu_ExceptionCode exceptionCode);
    void XModbusExceptionResponse_init(XModbusExceptionResponse* exc);
    void XModbusExceptionResponse_init_with_function_and_exception(
        XModbusExceptionResponse* exc, XModbusPdu_FunctionCode functionCode, XModbusPdu_ExceptionCode exceptionCode);
    void XModbusExceptionResponse_setExceptionCode(XModbusExceptionResponse* exc, XModbusPdu_ExceptionCode ec);
    /******************************************************************************************
     * 核心查询与操作接口 (对齐 QModbusPdu)
     ******************************************************************************************/

     /**
      * @brief 检查 PDU 是否有效
      * @note 有效条件: 功能码在有效范围内且数据长度 < 253
      */
    bool XModbusPdu_isValid(const XModbusPdu* pdu);

    /**
     * @brief 检查是否为异常响应
     */
    bool XModbusPdu_isException(const XModbusPdu* pdu);

    /**
     * @brief 获取异常码
     */
    XModbusPdu_ExceptionCode XModbusPdu_exceptionCode(const XModbusPdu* pdu);

    /**
     * @brief 获取 PDU 总大小 (功能码 + 数据)
     */
    int16_t XModbusPdu_size(const XModbusPdu* pdu);

    /**
     * @brief 获取 PDU 数据部分大小
     */
    int16_t XModbusPdu_dataSize(const XModbusPdu* pdu);

    /**
     * @brief 获取原始功能码 (包含异常位)
     */
    XModbusPdu_FunctionCode XModbusPdu_functionCodeRaw(const XModbusPdu* pdu);

    /**
     * @brief 获取实际功能码 (剥离异常位)
     */
    XModbusPdu_FunctionCode XModbusPdu_functionCode(const XModbusPdu* pdu);

    /**
     * @brief 设置功能码
     */
    void XModbusPdu_setFunctionCode(XModbusPdu* pdu, XModbusPdu_FunctionCode code);

    /**
     * @brief 获取数据的拷贝
     * @note 返回的 XByteArray 需由调用者手动释放
     */
    XByteArray* XModbusPdu_data(const XModbusPdu* pdu);

    /**
     * @brief 设置数据 (深拷贝)
     */
    void XModbusPdu_setData(XModbusPdu* pdu, const uint8_t* newData, size_t size);

    /******************************************************************************************
     * 内存管理宏
     ******************************************************************************************/
#define XModbusPdu_copy_base                XClass_copy_base
#define XModbusPdu_move_base                XClass_move_base
#define XModbusPdu_deinit_base              XClass_deinit_base
#define XModbusPdu_delete_base              XClass_delete_base

#define XModbusRequest_copy_base            XModbusPdu_copy_base            
#define XModbusRequest_move_base            XModbusPdu_move_base            
#define XModbusRequest_deinit_base          XModbusPdu_deinit_base          
#define XModbusRequest_delete_base          XModbusPdu_delete_base          

#define XModbusResponse_copy_base            XModbusPdu_copy_base            
#define XModbusResponse_move_base            XModbusPdu_move_base            
#define XModbusResponse_deinit_base          XModbusPdu_deinit_base          
#define XModbusResponse_delete_base          XModbusPdu_delete_base 


#define XModbusExceptionResponse_copy_base       XModbusResponse_copy_base         
#define XModbusExceptionResponse_move_base       XModbusResponse_move_base  
#define XModbusExceptionResponse_deinit_base     XModbusResponse_deinit_base
#define XModbusExceptionResponse_delete_base     XModbusResponse_delete_base


#ifdef __cplusplus
}
#endif

#endif // XMODBUSPDU_H