#include"CXinYueConfig.h"
#if !defined(XCRC_H)&& XCrc_ON
#define XCRC_H

/**
 * @file XCrc.h
 * @brief CRC16/CRC32 校验和计算 API。
 */

#ifdef __cplusplus
extern "C" {
#endif

#include "XTypes.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/**
 * @brief CRC 结果的字节序。
 * @details 用于指定写入缓冲区或返回值中的 CRC 数值采用的字节顺序。
 */
typedef enum XCRCByteOrder {
    XCRC_BYTE_ORDER_LITTLE_ENDIAN = 0, /**< 小端序，低字节在前。 */
    XCRC_BYTE_ORDER_BIG_ENDIAN,        /**< 大端序，高字节在前。 */
    XCRC_BYTE_ORDER_NATIVE             /**< 使用当前平台本机字节序。 */
} XCRCByteOrder;

/**
 * @brief 计算一段数据的 CRC16/MODBUS 校验值。
 * @param pucFrame 输入数据；usLen 为 0 时可为空，否则不能为 NULL。
 * @param usLen 输入数据长度，单位为字节。
 * @return 计算得到的 CRC16 数值，默认初始值为 0xFFFF。
 */
uint16_t XCrc_get16(uint8_t* pucFrame, uint16_t usLen);

/**
 * @brief 按指定字节序写入 CRC16 校验值。
 * @param dst 至少可写入 2 字节的目标地址。
 * @param crc16 要写入的 CRC16 数值。
 * @param order 目标字节序。
 */
void XCrc_set16Data(uint8_t* dst, uint16_t crc16, XCRCByteOrder order);

#if XVector_ON
/**
 * @brief 计算向量数据的 CRC16 并将结果追加到向量末尾。
 * @param data 待校验的非空向量；向量必须有可扩展容量或支持 resize。
 * @param order 追加的 CRC16 字节序。
 * @return 成功追加返回 true；data 为空或为空向量时返回 false。
 */
 bool XVector_append_crc16(XVector* data, XCRCByteOrder order);
#endif

#if XCrc32_ON
/**
 * @brief CRC32 查找表使用的多项式类型。
 */
typedef enum XCRC32Polynomial {
    XCRC32_IEEE_802_3 = 0, /**< IEEE 802.3，多项式 0x04C11DB7。 */
    XCRC32_CASTAGNOLI,      /**< Castagnoli，多项式 0x1EDC6F41。 */
    XCRC32_KOOPMAN,         /**< Koopman，多项式 0x741B8CD7。 */
    XCRC32_ISO_HDLC,        /**< ISO HDLC，多项式 0x04C11DB7。 */
    XCRC32_MPEG2,           /**< MPEG-2，多项式 0x04C11DB7。 */
    XCRC32_POSIX            /**< POSIX，多项式 0x04C11DB7。 */
} XCRC32Polynomial;

/**
 * @brief 获取当前 CRC32 查找表。
 * @return 8 个 256 项表组成的表指针；表由模块内部维护，调用者不得释放或修改。
 */
uint32_t (*XCrc32_get_crc_table())[256];

/**
 * @brief 按指定多项式初始化 CRC32 查找表。
 * @param polynomial 要使用的 CRC32 多项式类型。
 * @note 后续 CRC32 计算会使用最近一次初始化的多项式。
 */
void XCrc32_init_table(XCRC32Polynomial polynomial);

/**
 * @brief 计算一段数据的 CRC32 校验值。
 * @param data 输入数据；length 为 0 时可为空，否则不能为 NULL。
 * @param length 输入数据长度，单位为字节。
 * @param order 返回值的字节序调整方式。
 * @return CRC32 校验值。
 */
uint32_t XCrc32_calculate(const uint8_t* data, size_t length, XCRCByteOrder order);

/**
 * @brief 使用一段新数据更新 CRC32 状态。
 * @param crc 当前 CRC32 状态值，通常为前一次计算的中间结果。
 * @param data 新数据；length 为 0 时可为空，否则不能为 NULL。
 * @param length 新数据长度，单位为字节。
 * @return 更新后的 CRC32 状态值。
 */
uint32_t XCrc32_update(uint32_t crc, const uint8_t* data, size_t length);

/**
 * @brief 按指定字节序调整 CRC32 数值。
 * @param crc 待调整的 CRC32 数值。
 * @param order 目标字节序；当前实现对大端序执行字节交换。
 * @return 调整后的 CRC32 数值。
 */
uint32_t XCrc32_finalize(uint32_t crc, XCRCByteOrder order);

/**
 * @brief 合并两段连续数据的 CRC32 结果。
 * @param crc1 第一段数据的 CRC32 结果。
 * @param crc2 第二段数据的 CRC32 结果。
 * @param len2 第二段数据长度，单位为字节。
 * @return 拼接后数据的 CRC32 结果。
 * @note 两个 CRC 必须使用同一多项式和同一计算约定。
 */
uint32_t XCrc32_combine(uint32_t crc1, uint32_t crc2, size_t len2);
#endif // XCrc32_ON

#ifdef __cplusplus
}
#endif

#endif /* XCRC_H */
