#ifndef XMODBUSADU_H
#define XMODBUSADU_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "XModbusPdu.h"
#include "XByteArray.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @file XModbusAdu.h
 * @brief Modbus ADU（应用数据单元）工具
 * @details 提供Modbus串行链路ADU帧的封装、解析和校验功能。
 *          支持RTU和ASCII两种传输模式，对齐Qt6 QModbusSerialAdu。
 *
 * @par 功能特性
 * - RTU模式：CRC校验、帧组装/解析
 * - ASCII模式：LRC校验、帧组装/解析
 * - 帧验证：校验和检查、地址提取
 *
 * @par 使用示例
 * @code
 * // 创建RTU ADU帧
 * XModbusRequest* request = XModbusRequest_create_with_code(XModbusPdu_ReadHoldingRegisters);
 * uint8_t data[] = {0x00, 0x00, 0x00, 0x0A};
 * XModbusPdu_setData((XModbusPdu*)request, data, 4);
 * XByteArray* aduFrame = XModbusAdu_createRtuFrame(1, (const XModbusPdu*)request);
 *
 * // 解析接收到的ADU帧
 * XModbusAdu* adu = XModbusAdu_parseRtu(frameData, frameLen);
 * if (adu && XModbusAdu_matchingChecksum(adu)) {
 *     int serverAddr = XModbusAdu_serverAddress(adu);
 *     XModbusResponse* pdu = XModbusAdu_pdu(adu);
 * }
 * XModbusAdu_free(adu);
 * @endcode
 */

/******************************************************************************************
 * 枚举类型定义
 ******************************************************************************************/

/**
 * @brief ADU传输模式枚举
 */
typedef enum {
    XModbusAdu_Rtu,     ///< RTU模式（二进制，CRC校验）
    XModbusAdu_Ascii    ///< ASCII模式（文本，LRC校验）
} XModbusAdu_Type;

/******************************************************************************************
 * 结构体定义
 ******************************************************************************************/

/**
 * @brief Modbus ADU结构体
 * @details 封装了一个完整的ADU帧，包含原始数据和解析后的字段
 */
typedef struct XModbusAdu {
    XModbusAdu_Type m_type;         ///< 传输模式
    XByteArray* m_rawData;          ///< 原始帧数据（含校验码）
    XByteArray* m_data;             ///< 解析后的数据（不含帧头尾，RTU模式下含地址+PDU+校验）
    int m_serverAddress;            ///< 从站地址
    XModbusPdu* m_pdu;              ///< 解析出的PDU
    bool m_checksumValid;           ///< 校验和是否匹配
} XModbusAdu;

/******************************************************************************************
 * ADU创建接口
 ******************************************************************************************/

/**
 * @brief 创建RTU模式的ADU帧
 * @param serverAddress 从站地址
 * @param pdu Modbus PDU指针
 * @return 组装好的RTU帧数据（地址 + PDU + CRC16），调用者负责释放
 * @note CRC16使用XCrc_get16计算，以小端序附加
 */
XByteArray* XModbusAdu_createRtuFrame(int serverAddress, const XModbusPdu* pdu);

/**
 * @brief 创建ASCII模式的ADU帧
 * @param serverAddress 从站地址
 * @param pdu Modbus PDU指针
 * @param delimiter 结束分隔符（默认为'\\n'，传入0使用默认值）
 * @return 组装好的ASCII帧数据（":" + 十六进制 + "\\r" + delimiter），调用者负责释放
 * @note LRC校验码会被自动计算并附加
 */
XByteArray* XModbusAdu_createAsciiFrame(int serverAddress, const XModbusPdu* pdu, char delimiter);

/******************************************************************************************
 * ADU解析接口
 ******************************************************************************************/

/**
 * @brief 解析RTU格式的ADU帧
 * @param data 原始帧数据（含CRC）
 * @param size 数据大小
 * @return 解析后的XModbusAdu对象，调用者负责释放
 * @note 返回NULL表示数据不足以解析
 */
XModbusAdu* XModbusAdu_parseRtu(const uint8_t* data, size_t size);

/**
 * @brief 解析ASCII格式的ADU帧
 * @param data 原始帧数据（含":"前缀和"\\r\\n"后缀）
 * @param size 数据大小
 * @return 解析后的XModbusAdu对象，调用者负责释放
 * @note 返回NULL表示数据不足以解析
 */
XModbusAdu* XModbusAdu_parseAscii(const uint8_t* data, size_t size);

/**
 * @brief 释放ADU对象
 * @param adu 待释放的XModbusAdu对象
 */
void XModbusAdu_free(XModbusAdu* adu);

/******************************************************************************************
 * ADU查询接口
 ******************************************************************************************/

/**
 * @brief 获取ADU帧大小（不含PDU功能码，但含地址+校验）
 * @param adu ADU指针
 * @return ADU数据部分大小，-1表示无效
 */
int XModbusAdu_size(const XModbusAdu* adu);

/**
 * @brief 获取原始帧大小
 * @param adu ADU指针
 * @return 原始帧大小
 */
int XModbusAdu_rawSize(const XModbusAdu* adu);

/**
 * @brief 获取原始帧数据
 * @param adu ADU指针
 * @return 原始帧数据的拷贝，调用者负责释放
 */
XByteArray* XModbusAdu_rawData(const XModbusAdu* adu);

/**
 * @brief 获取从站地址
 * @param adu ADU指针
 * @return 从站地址
 */
int XModbusAdu_serverAddress(const XModbusAdu* adu);

/**
 * @brief 获取解析出的PDU
 * @param adu ADU指针
 * @return PDU对象的拷贝，调用者负责释放
 */
XModbusPdu* XModbusAdu_pdu(const XModbusAdu* adu);

/**
 * @brief 检查校验和是否匹配
 * @param adu ADU指针
 * @return true表示校验和匹配
 */
bool XModbusAdu_matchingChecksum(const XModbusAdu* adu);

/******************************************************************************************
 * 校验和计算接口（静态工具函数）
 ******************************************************************************************/

/**
 * @brief 计算LRC校验码
 * @param data 数据指针
 * @param len 数据长度
 * @return LRC校验码
 * @note LRC = -(累加和)，取低8位
 */
uint8_t XModbusAdu_calculateLRC(const uint8_t* data, int len);

/**
 * @brief 计算Modbus RTU CRC16校验码
 * @param data 数据指针
 * @param len 数据长度
 * @return CRC16校验码
 * @note 使用标准Modbus CRC16算法（多项式0x8005）
 *       此函数与XCrc_get16功能相同，提供便捷封装
 */
uint16_t XModbusAdu_calculateCRC(const uint8_t* data, int len);

#ifdef __cplusplus
}
#endif

#endif // XMODBUSADU_H
