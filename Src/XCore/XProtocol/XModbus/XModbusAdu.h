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
 *     XClassDeinit(&pdu);
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

#ifndef XMODBUSADU_H
#define XMODBUSADU_H
#include "XModbus_config.h"

#ifdef __cplusplus
extern "C" {
#endif
#if XMODBUS_ON
#if XMODBUS_CORE_ON

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "XModbusPdu.h"
#include "XByteArray.h"

/******************************************************************************************
 * 枚举类型定义
 ******************************************************************************************/

/**
 * @brief ADU传输模式枚举
 * @details 存储于 XModbusAdu::m_type 位域；决定校验码种类（RTU=CRC16 2 字节，
 *          ASCII=LRC 1 字节）与帧格式。
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
    XByteArray* m_rawData;          /**< 原始帧数据（含帧头尾，如ASCII的":"和"\r\n"），
                                         对象拥有、deinit 时释放；init 后为 NULL。 */
    XByteArray* m_data;             /**< 地址 + PDU + 校验码（对齐Qt语义），对象拥有、
                                         deinit 时释放；init 后为 NULL。 */
    uint16_t m_type : 1;            /**< 传输模式：0=XModbusAdu_Rtu, 1=XModbusAdu_Ascii。 */
    uint16_t m_serverAddress : 8;   /**< 从站地址原始字节：init 置 0xFF 表示无效；
                                         parse 后为帧首字节（0-255，不做 0-247 校验）。 */
    uint16_t m_checksumValid : 1;   /**< 校验和是否匹配（解析时计算并存入，
                                         1=匹配，0=不匹配或未经解析）。 */
} XModbusAdu;

/******************************************************************************************
 * 生命周期管理（对齐项目风格：init/delete/deinit）
 ******************************************************************************************/

/**
 * @brief 初始化已分配的ADU实例（栈或嵌入使用）。
 * @param adu 目标对象；NULL 时不执行任何操作；可为未初始化内存。
 * @return 无。
 * @note 整体清零后置 m_serverAddress=0xFF（无效地址）、默认 RTU 模式；
 *       两个数据容器保持 NULL（本函数不创建容器）；栈对象完成后用
 *       XModbusAdu_deinit 释放内部资源。
 */
void XModbusAdu_init(XModbusAdu* adu);

/**
 * @brief 释放ADU对象（堆分配对象的完整释放）。
 * @param adu 待释放对象；NULL 时不执行任何操作。
 * @return 无。
 * @warning 仅用于 XModbusAdu_parseRtu/XModbusAdu_parseAscii 返回的堆对象
 *          （内部按 XFree_System 配对释放）；栈/嵌入对象改用
 *          XModbusAdu_deinit。
 */
void XModbusAdu_delete(XModbusAdu* adu);

/**
 * @brief 析构ADU内部资源（不释放结构体本身，用于栈/嵌入对象）。
 * @param adu 目标对象；NULL 时不执行任何操作。
 * @return 无。
 * @note 释放并置空 m_rawData 与 m_data（对未解析过的实例是安全的空操作）；
 *       可与 XModbusAdu_init 成对重复使用。
 */
void XModbusAdu_deinit(XModbusAdu* adu);

/******************************************************************************************
 * ADU帧创建接口
 ******************************************************************************************/

/**
 * @brief 组装 RTU 模式 ADU 帧（地址 + 功能码 + PDU数据 + CRC16 小端）。
 * @param serverAddress 从站地址（截断为低 8 位写入，不做 0-247 校验）。
 * @param pdu 源 PDU（借用，只读；NULL 返回 NULL）；功能码取原始值
 *        （异常位保留），数据部分为功能码之后的数据。
 * @return 新 XByteArray 帧（[地址1][功能码1][数据0-252][CRC_Lo][CRC_Hi]，
 *         调用方 XClassDelete 释放）；pdu 为 NULL 或内存分配失败返回 NULL。
 * @note CRC16 按 XCrc16_Modbus 算法对「地址+功能码+数据」计算，以小端序
 *       （低字节在前）附加到帧尾。
 */
XByteArray* XModbusAdu_createRtuFrame(int serverAddress, const XModbusPdu* pdu);

/**
 * @brief 组装 ASCII 模式 ADU 帧（":" + 地址/功能码/数据/LRC 的十六进制 + "\r" + 结束符）。
 * @param serverAddress 从站地址（截断为低 8 位写入，不做 0-247 校验）。
 * @param pdu 源 PDU（借用，只读；NULL 返回 NULL）；功能码取原始值
 *        （异常位保留）。
 * @param delimiter 帧尾结束符；传 0 使用默认值 '\n'。
 * @return 新 XByteArray 帧（':' 前缀 + 大写十六进制 + "\r" + delimiter，
 *         调用方 XClassDelete 释放）；pdu 为 NULL 或内存分配失败返回 NULL。
 * @note LRC 对「地址+功能码+数据」的二进制字节计算（非十六进制字符），
 *       结果以 2 个十六进制字符附在 "\r" 之前。
 */
XByteArray* XModbusAdu_createAsciiFrame(int serverAddress, const XModbusPdu* pdu, char delimiter);

/******************************************************************************************
 * ADU解析接口
 ******************************************************************************************/

/**
 * @brief 解析RTU格式的ADU帧。
 * @param data 原始帧数据（含 CRC，借用；NULL 或 size < 4 返回 NULL——
 *        4 = 地址1 + 功能码1 + CRC2 的最小帧）。
 * @param size 原始帧字节数。
 * @return 新 XModbusAdu 对象（调用方 XModbusAdu_delete 释放）；数据不足
 *         或内存分配失败返回 NULL。
 * @note 只校验长度不拒绝 CRC 错误帧——校验结果存入对象（matchingChecksum
 *       查询）；m_data 保存完整帧（地址 + PDU + CRC，对齐 Qt 语义）；
 *       CRC 按「地址+功能码+数据」计算、以小端序与帧尾 2 字节比对。
 */
XModbusAdu* XModbusAdu_parseRtu(const uint8_t* data, size_t size);

/**
 * @brief 解析ASCII格式的ADU帧。
 * @param data 原始帧数据（':' 前缀 + 十六进制 + 可选 "\r"/"\n" 结尾；
 *        借用）；NULL、size < 9、首字节非 ':'、或十六进制长度 <4/为奇数
 *        返回 NULL。
 * @param size 原始帧字节数。
 * @return 新 XModbusAdu 对象（调用方 XModbusAdu_delete 释放）；格式非法
 *         或内存分配失败返回 NULL。
 * @note 在前缀之后遇到首个 '\r' 或 '\n' 即认为帧结束（无结束符则读到
 *       size）；m_rawData 保存含前缀的原始字符（结束符在缓冲区末尾时
 *       可能不含终止符）；m_data 保存十六进制解码后的二进制（地址 +
 *       PDU + LRC）；LRC 按「地址+功能码+数据」计算并与末字节比对，
 *       结果存入对象（matchingChecksum 查询）。
 */
XModbusAdu* XModbusAdu_parseAscii(const uint8_t* data, size_t size);

/******************************************************************************************
 * ADU查询接口（对齐Qt QModbusSerialAdu）
 ******************************************************************************************/

/**
 * @brief 获取ADU数据部分大小（地址 + PDU，不含校验码）。
 * @param adu 目标对象；NULL 或 m_data 为空返回 -1。
 * @return 数据部分字节数（RTU 减 2 字节 CRC，ASCII 减 1 字节 LRC）；
 *         对齐 Qt：m_data.size() - checksumBytes。
 */
int XModbusAdu_size(const XModbusAdu* adu);

/**
 * @brief 获取ADU数据（地址 + PDU，不含校验码）。
 * @param adu 目标对象；NULL、m_data 为空或数据部分为空返回 NULL。
 * @return 新 XByteArray 对象（m_data 前 size() 字节的拷贝，调用方
 *         XClassDelete 释放）；对齐 Qt：m_data.left(size())。
 */
XByteArray* XModbusAdu_data(const XModbusAdu* adu);

/**
 * @brief 获取原始帧大小。
 * @param adu 目标对象；NULL 或 m_rawData 为空返回 -1。
 * @return 原始帧字节数（RTU 为完整输入长度；ASCII 为含 ':' 前缀的
 *         原始字符长度）。
 */
int XModbusAdu_rawSize(const XModbusAdu* adu);

/**
 * @brief 获取原始帧数据。
 * @param adu 目标对象；NULL 或 m_rawData 为空返回 NULL。
 * @return 新 XByteArray 对象（原始帧的深拷贝，调用方 XClassDelete 释放）。
 */
XByteArray* XModbusAdu_rawData(const XModbusAdu* adu);

/**
 * @brief 获取从站地址。
 * @param adu 目标对象；NULL 返回 -1。
 * @return 地址原始字节（0-255；init 后为哨兵值 0xFF，解析后为帧首字节）。
 * @note 地址 0xFF 与「无效」哨兵值重合，来自帧的 0xFF 地址无法与未解析
 *       状态区分；广播地址（0）不做特殊处理，原样返回。
 */
int XModbusAdu_serverAddress(const XModbusAdu* adu);

/**
 * @brief 提取解析出的 PDU（填充调用者提供的对象，零额外 PDU 堆分配）。
 * @param adu 源 ADU（借用）；NULL、m_data 为空或数据部分不足 2 字节
 *        （至少需 地址+功能码）返回 false。
 * @param out 输出对象（调用方提供存储空间；函数对其完整初始化，覆盖
 *        既有内容——传未初始化内存或不再使用的对象）。
 * @return 成功填充返回 true（功能码=数据部分第 2 字节，数据=其余字节；
 *         无数据时 PDU 数据为空）；失败返回 false 且 out 不被触碰。
 * @note 对齐 Qt QModbusSerialAdu::pdu()（Qt 返回栈上构造的 QModbusPdu）；
 *       使用完毕后调用方须 XClassDeinit(out)。
 */
bool XModbusAdu_pdu(const XModbusAdu* adu, XModbusPdu* out);

/**
 * @brief 检查校验和是否匹配。
 * @param adu 目标对象；NULL 返回 false。
 * @return 解析时计算的校验结果：匹配返回 true；不匹配、未经解析或
 *         adu 为 NULL 返回 false。
 * @note 本函数只读解析时存入的标志，不重新计算校验。
 */
bool XModbusAdu_matchingChecksum(const XModbusAdu* adu);

/******************************************************************************************
 * 校验和计算接口（静态工具函数）
 ******************************************************************************************/

/**
 * @brief 计算LRC校验码。
 * @param data 参与校验的字节序列（借用；len>0 时不能为 NULL）。
 * @param len 参与校验的字节数；<=0 时返回 0（不读缓冲区）。
 * @return LRC 值 = (uint8_t)(-(累加和的低 8 位))，即 8 位二进制补码。
 * @note 典型校验范围：「地址+功能码+PDU数据」（不含校验码自身）。
 */
uint8_t XModbusAdu_calculateLRC(const uint8_t* data, int len);

/**
 * @brief 计算Modbus RTU CRC16校验码。
 * @param data 参与校验的字节序列（借用；len>0 时不能为 NULL）。
 * @param len 参与校验的字节数。
 * @return CRC16 值；附加到 RTU 帧时按小端序（低字节在前）写入。
 * @note 标准 Modbus CRC16 算法（多项式 0x8005，反射实现），内部委托
 *       XCrc16_calculate(XCrc16_Algorithm_Modbus)，提供便捷封装。
 */
uint16_t XModbusAdu_calculateCRC(const uint8_t* data, int len);

#endif /* XMODBUS_CORE_ON */
#endif /* XMODBUS_ON */

#ifdef __cplusplus
}
#endif

#endif // XMODBUSADU_H
