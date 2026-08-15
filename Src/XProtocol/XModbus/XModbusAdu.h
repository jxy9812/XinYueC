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
 *          支持RTU和ASCII两种传输模式，对齐Qt QModbusSerialAdu。
 *
 * @par 内存优化说明
 * - 结构体大小：64位24字节，32位12字节（原版约48字节）
 * - 使用位域合并 m_type(1bit) + m_serverAddress(8bit) + m_checksumValid(1bit)
 * - 去除冗余的 m_pdu 指针字段（按需从 m_data 解析，对齐Qt）
 * - m_data 语义对齐Qt：地址 + PDU + 校验码
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
 * // 解析接收到的ADU帧（堆分配）
 * XModbusAdu* adu = XModbusAdu_parseRtu(frameData, frameLen);
 * if (adu && XModbusAdu_matchingChecksum(adu)) {
 *     int serverAddr = XModbusAdu_serverAddress(adu);
 *     XModbusPdu pdu;
 *     XModbusAdu_pdu(adu, &pdu);
 *     // ...
 *     XModbusPdu_deinit_base(&pdu);
 * }
 * XModbusAdu_delete(adu);
 *
 * // 栈分配示例
 * XModbusAdu adu;
 * XModbusAdu_init(&adu);
 * // ... 使用 adu ...
 * XModbusAdu_deinit(&adu);
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
 * 结构体定义（内存优化版）
 ******************************************************************************************/

/**
 * @brief Modbus ADU结构体
 * @details 封装了一个完整的ADU帧，对齐Qt QModbusSerialAdu语义。
 *
 * @par m_data 存储布局（对齐Qt）
 * @code
 * RTU:  [地址(1)] [PDU(n)] [CRC_Lo(1)] [CRC_Hi(1)]
 *        |________ size() _________|
 *        |____ data() ____|         <- 校验范围
 * ASCII:[地址(1)] [PDU(n)] [LRC(1)]
 *        |____ size() ____|
 *        |____ data() ____|         <- 校验范围
 * @endcode
 *
 * @par 内存布局（64位）
 * offset 0: m_rawData  (XByteArray*, 8B)
 * offset 8: m_data     (XByteArray*, 8B)
 * offset 16: m_type(1bit) + m_serverAddress(8bit) + m_checksumValid(1bit)  (uint16_t, 2B)
 * 总计: 18B 有效数据，对齐到 24B
 *
 * @par 内存布局（32位嵌入式）
 * offset 0: m_rawData  (XByteArray*, 4B)
 * offset 4: m_data     (XByteArray*, 4B)
 * offset 8: m_type(1bit) + m_serverAddress(8bit) + m_checksumValid(1bit)  (uint16_t, 2B)
 * 总计: 10B 有效数据，对齐到 12B
 */
typedef struct XModbusAdu {
    XByteArray* m_rawData;          ///< 原始帧数据（含帧头尾，如ASCII的":"和"\r\n"）
    XByteArray* m_data;             ///< 地址 + PDU + 校验码（对齐Qt语义）
    uint16_t m_type : 1;            ///< 传输模式：0=RTU, 1=ASCII
    uint16_t m_serverAddress : 8;   ///< 从站地址（0-247，0xFF=无效）
    uint16_t m_checksumValid : 1;   ///< 校验和是否匹配
} XModbusAdu;

/******************************************************************************************
 * 生命周期管理（对齐项目风格：init/delete/deinit）
 ******************************************************************************************/

/**
 * @brief 初始化已分配的ADU实例（栈或嵌入使用）
 * @param adu XModbusAdu指针（非NULL）
 */
void XModbusAdu_init(XModbusAdu* adu);

/**
 * @brief 释放ADU对象（堆分配对象的完整释放）
 * @param adu 待释放的XModbusAdu指针
 */
void XModbusAdu_delete(XModbusAdu* adu);

/**
 * @brief 析构ADU内部资源（不释放结构体本身，用于栈/嵌入对象）
 * @param adu XModbusAdu指针
 */
void XModbusAdu_deinit(XModbusAdu* adu);

/******************************************************************************************
 * ADU帧创建接口
 ******************************************************************************************/

/**
 * @brief 创建RTU模式的ADU帧
 * @param serverAddress 从站地址
 * @param pdu Modbus PDU指针
 * @return 组装好的RTU帧数据（地址 + PDU + CRC16），调用者负责释放
 * @note CRC16使用XCrc16_calculate计算，以小端序附加
 */
XByteArray* XModbusAdu_createRtuFrame(int serverAddress, const XModbusPdu* pdu);

/**
 * @brief 创建ASCII模式的ADU帧
 * @param serverAddress 从站地址
 * @param pdu Modbus PDU指针
 * @param delimiter 结束分隔符（默认为'\n'，传入0使用默认值）
 * @return 组装好的ASCII帧数据（":" + 十六进制 + "\r" + delimiter），调用者负责释放
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
 * @return 解析后的XModbusAdu对象，调用者负责XModbusAdu_delete释放
 * @note 返回NULL表示数据不足以解析
 */
XModbusAdu* XModbusAdu_parseRtu(const uint8_t* data, size_t size);

/**
 * @brief 解析ASCII格式的ADU帧
 * @param data 原始帧数据（含":"前缀和"\r\n"后缀）
 * @param size 数据大小
 * @return 解析后的XModbusAdu对象，调用者负责XModbusAdu_delete释放
 * @note 返回NULL表示数据不足以解析
 */
XModbusAdu* XModbusAdu_parseAscii(const uint8_t* data, size_t size);

/******************************************************************************************
 * ADU查询接口（对齐Qt QModbusSerialAdu）
 ******************************************************************************************/

/**
 * @brief 获取ADU数据部分大小（地址 + PDU，不含校验码）
 * @param adu ADU指针
 * @return 数据部分大小，-1表示无效
 * @note 对齐Qt：m_data.size() - checksumBytes
 *       RTU减去2字节CRC，ASCII减去1字节LRC
 */
int XModbusAdu_size(const XModbusAdu* adu);

/**
 * @brief 获取ADU数据（地址 + PDU，不含校验码）
 * @param adu ADU指针
 * @return 数据部分的拷贝（地址 + PDU），调用者负责释放，NULL表示无效
 * @note 对齐Qt：m_data.left(size())
 */
XByteArray* XModbusAdu_data(const XModbusAdu* adu);

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
 * @return 从站地址，-1表示无效
 */
int XModbusAdu_serverAddress(const XModbusAdu* adu);

/**
 * @brief 获取解析出的PDU（对齐Qt：零堆分配，填充调用者提供的栈对象）
 * @param adu ADU指针
 * @param out [out] 输出参数，调用者提供的XModbusPdu对象
 * @return true表示成功，false表示参数无效
 * @note 对齐Qt QModbusSerialAdu::pdu()，返回栈上构造的QModbusPdu
 *       调用者需在不再使用时调用 XModbusPdu_deinit_base(out)
 */
bool XModbusAdu_pdu(const XModbusAdu* adu, XModbusPdu* out);

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
 *       此函数与XCrc16_calculate功能相同，提供便捷封装
 */
uint16_t XModbusAdu_calculateCRC(const uint8_t* data, int len);

#ifdef __cplusplus
}
#endif

#endif // XMODBUSADU_H
