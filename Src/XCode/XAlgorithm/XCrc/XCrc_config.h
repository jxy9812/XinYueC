/**
 * @file XCrc_config.h
 * @brief XCrc 模块及各 CRC-16、CRC-32 标准变体的编译开关。
 */

#ifndef XCRC_CONFIG_H
#define XCRC_CONFIG_H

/** @brief 是否编译整个 XCrc 模块。 */
#ifndef XCrc_ON
#define XCrc_ON 1
#endif

/** @brief 是否编译 CRC-16 公共 API。 */
#ifndef XCrc16_ON
#define XCrc16_ON 1
#endif

/** @brief 是否启用 CRC-16/ARC（又称 CRC-16/IBM）。 */
#ifndef XCrc16_Arc_ON
#define XCrc16_Arc_ON 1
#endif

/** @brief 是否启用 CRC-16/MODBUS。 */
#ifndef XCrc16_Modbus_ON
#define XCrc16_Modbus_ON 1
#endif

/** @brief 是否启用 CRC-16/CCITT-FALSE。 */
#ifndef XCrc16_CcittFalse_ON
#define XCrc16_CcittFalse_ON 1
#endif

/** @brief 是否启用 CRC-16/XMODEM。 */
#ifndef XCrc16_Xmodem_ON
#define XCrc16_Xmodem_ON 1
#endif

/** @brief 是否启用 CRC-16/X25。 */
#ifndef XCrc16_X25_ON
#define XCrc16_X25_ON 1
#endif

/** @brief 是否启用 CRC-16/KERMIT（IEEE 802.15.4 FCS）。 */
#ifndef XCrc16_Kermit_ON
#define XCrc16_Kermit_ON 1
#endif

/** @brief 是否启用 CRC-16/USB。 */
#ifndef XCrc16_Usb_ON
#define XCrc16_Usb_ON 1
#endif

/** @brief 是否启用 CRC-16/DNP。 */
#ifndef XCrc16_Dnp_ON
#define XCrc16_Dnp_ON 1
#endif

/** @brief 是否编译 CRC-32 公共 API。 */
#ifndef XCrc32_ON
#define XCrc32_ON 1
#endif

/** @brief 是否启用 CRC-32/ISO-HDLC（IEEE 802.3、PKZIP、zlib）。 */
#ifndef XCrc32_IsoHdlc_ON
#define XCrc32_IsoHdlc_ON 1
#endif

/** @brief 是否启用 CRC-32C（Castagnoli）。 */
#ifndef XCrc32_Castagnoli_ON
#define XCrc32_Castagnoli_ON 1
#endif

/** @brief 是否启用 CRC-32/MPEG-2。 */
#ifndef XCrc32_Mpeg2_ON
#define XCrc32_Mpeg2_ON 1
#endif

/** @brief 是否启用 CRC-32/BZIP2。 */
#ifndef XCrc32_Bzip2_ON
#define XCrc32_Bzip2_ON 1
#endif

/** @brief 是否启用 CRC-32/POSIX（CKSUM 参数组）。 */
#ifndef XCrc32_Posix_ON
#define XCrc32_Posix_ON 1
#endif

/** @brief 是否启用 CRC-32/JAMCRC。 */
#ifndef XCrc32_Jamcrc_ON
#define XCrc32_Jamcrc_ON 1
#endif

/** @brief 是否启用 CRC-32Q。 */
#ifndef XCrc32_Q_ON
#define XCrc32_Q_ON 1
#endif

/** @brief 是否启用 CRC-32/XFER。 */
#ifndef XCrc32_Xfer_ON
#define XCrc32_Xfer_ON 1
#endif

#endif /* XCRC_CONFIG_H */
