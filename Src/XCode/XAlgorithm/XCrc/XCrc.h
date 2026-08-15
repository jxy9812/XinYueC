#include "XCrc_config.h"
#if !defined(XCRC_H)&& XCrc_ON
#define XCRC_H

/**
 * @file XCrc.h
 * @brief CRC16/CRC32 校验和计算 API。
 */

#ifdef __cplusplus
extern "C" {
#endif

#include <stddef.h>
#include <stdint.h>

#if XCrc16_ON
/**
 * @brief CRC-16 标准参数组。
 * @details 每个枚举值的实现由 XCrc_config.h 中对应的宏控制；已关闭的
 *          算法传入 XCrc16_calculate 时返回 0。
 */
typedef enum XCrc16_Algorithm {
    XCrc16_Algorithm_Arc,         /**< CRC-16/ARC（CRC-16/IBM）。 */
    XCrc16_Algorithm_Modbus,      /**< CRC-16/MODBUS。 */
    XCrc16_Algorithm_CcittFalse,  /**< CRC-16/CCITT-FALSE。 */
    XCrc16_Algorithm_Xmodem,      /**< CRC-16/XMODEM。 */
    XCrc16_Algorithm_X25,         /**< CRC-16/X25。 */
    XCrc16_Algorithm_Kermit,      /**< CRC-16/KERMIT，IEEE 802.15.4 FCS。 */
    XCrc16_Algorithm_Usb,         /**< CRC-16/USB。 */
    XCrc16_Algorithm_Dnp          /**< CRC-16/DNP。 */
} XCrc16_Algorithm;

/**
 * @brief 使用指定标准参数组计算一段数据的 CRC-16 校验值。
 * @param algorithm CRC-16 算法枚举；对应实现必须已在 XCrc_config.h 启用。
 * @param data 输入数据；length 为 0 时可为空，否则不能为 NULL。
 * @param length 输入数据长度，单位为字节。
 * @return 计算得到的 CRC-16 数值；参数无效、算法枚举无效或算法未启用时
 *         返回 0。
 */
uint16_t XCrc16_calculate(XCrc16_Algorithm algorithm,
                          const uint8_t* data, size_t length);
#endif /* XCrc16_ON */

#if XCrc32_ON
/**
 * @brief CRC-32 标准参数组。
 * @details 每个枚举值的实现由 XCrc_config.h 中对应的宏控制；已关闭的
 *          算法传入 CRC-32 API 时返回 0。
 */
typedef enum XCrc32_Algorithm {
    XCrc32_Algorithm_IsoHdlc,    /**< CRC-32/ISO-HDLC（IEEE 802.3、PKZIP、zlib）。 */
    XCrc32_Algorithm_Castagnoli, /**< CRC-32C（Castagnoli）。 */
    XCrc32_Algorithm_Mpeg2,      /**< CRC-32/MPEG-2。 */
    XCrc32_Algorithm_Bzip2,      /**< CRC-32/BZIP2。 */
    XCrc32_Algorithm_Posix,      /**< CRC-32/POSIX（CKSUM 参数组）。 */
    XCrc32_Algorithm_Jamcrc,     /**< CRC-32/JAMCRC。 */
    XCrc32_Algorithm_Q,          /**< CRC-32Q。 */
    XCrc32_Algorithm_Xfer        /**< CRC-32/XFER。 */
} XCrc32_Algorithm;

/**
 * @brief 获取指定 CRC-32 算法的流式计算初始值。
 * @param algorithm CRC-32 算法枚举；对应实现必须已在 XCrc_config.h 启用。
 * @return 传入 XCrc32_update 进行首段计算的初始 CRC 值，同时也是空数据的
 *         CRC-32 结果；算法枚举无效或算法未启用时返回 0。
 */
uint32_t XCrc32_initialize(XCrc32_Algorithm algorithm);

/**
 * @brief 使用指定标准参数组计算一段数据的 CRC-32 校验值。
 * @param algorithm CRC-32 算法枚举；对应实现必须已在 XCrc_config.h 启用。
 * @param data 输入数据；length 为 0 时可为空，否则不能为 NULL。
 * @param length 输入数据长度，单位为字节。
 * @return 计算得到的 CRC-32 数值；参数无效、算法枚举无效或算法未启用时
 *         返回 0。
 */
uint32_t XCrc32_calculate(XCrc32_Algorithm algorithm,
                          const uint8_t* data, size_t length);

/**
 * @brief 使用一段新数据更新指定 CRC-32 算法的校验值。
 * @param algorithm CRC-32 算法枚举；必须与 crc 的来源保持一致。
 * @param crc 上一次返回的 CRC-32 值；首次调用传入
 *            XCrc32_initialize(algorithm) 的返回值。
 * @param data 新数据；length 为 0 时可为空，否则不能为 NULL。
 * @param length 新数据长度，单位为字节。
 * @return 更新后的 CRC-32 数值；参数无效、算法枚举无效或算法未启用时返回 0。
 */
uint32_t XCrc32_update(XCrc32_Algorithm algorithm, uint32_t crc,
                        const uint8_t* data, size_t length);

/**
 * @brief 合并同一 CRC-32 算法下两段连续数据的校验值。
 * @param algorithm CRC-32 算法枚举；必须与两个 CRC 的来源保持一致。
 * @param crc1 第一段数据的 CRC32 结果。
 * @param crc2 第二段数据的 CRC32 结果。
 * @param len2 第二段数据长度，单位为字节。
 * @return 拼接后数据的 CRC32 结果。
 * @note 两个 CRC 必须使用相同算法独立计算得到。
 */
uint32_t XCrc32_combine(XCrc32_Algorithm algorithm, uint32_t crc1,
                        uint32_t crc2, uint64_t len2);
#endif /* XCrc32_ON */

#ifdef __cplusplus
}
#endif

#endif /* XCRC_H */
