#include"XDataStructConfig.h"
#if !defined(XCRC_H)&& XCrc_ON
#define XCRC_H
#ifdef __cplusplus
extern "C" {
#endif
#include"XTypes.h"
#include<stdint.h>
#include<stdbool.h>
/**
 * CRC16字节序枚举
 * 用于指定CRC校验值的字节存储顺序
 */
typedef enum
{
    XCRC_BYTE_ORDER_LITTLE_ENDIAN = 0,  /**< 小端序：低字节在前，高字节在后 (LE) */
    XCRC_BYTE_ORDER_BIG_ENDIAN,        /**< 大端序：高字节在前，低字节在后 (BE) */
    XCRC_BYTE_ORDER_NATIVE            /**< 本机字节序：使用当前系统的默认字节序 */
}XCRCByteOrder;
 
 uint16_t   XCrc_get16( uint8_t * pucFrame, uint16_t usLen );
 //设置CRC16  mode为1是大端,0小端
 void XCrc_set16Data(uint8_t* dst, uint16_t crc16, XCRCByteOrder order);
#if XVector_ON
 bool XVector_append_crc16(XVector* data, XCRCByteOrder order);
#endif

#if XCrc32_ON
 /**
 * CRC32多项式类型枚举
 */
 typedef enum {
     XCRC32_IEEE_802_3 = 0,      /**< 标准IEEE 802.3 (以太网)多项式：0x04C11DB7 */
     XCRC32_CASTAGNOLI,         /**< Castagnoli多项式：0x1EDC6F41 */
     XCRC32_KOOPMAN,            /**< Koopman多项式：0x741B8CD7 */
     XCRC32_ISO_HDLC,           /**< ISO HDLC多项式：0x04C11DB7 (与IEEE相同) */
     XCRC32_MPEG2,              /**< MPEG-2多项式：0x04C11DB7 (与IEEE相同) */
     XCRC32_POSIX               /**< POSIX多项式：0x04C11DB7 (与IEEE相同) */
 } XCRC32Polynomial;
 /**
 * 初始化CRC32查找表
 * @param polynomial 多项式类型
 */
 void XCrc32_init_table(XCRC32Polynomial polynomial);

 /**
  * 计算数据的CRC32校验值
  * @param data 输入数据指针
  * @param length 数据长度（字节）
  * @param order 字节序
  * @return CRC32校验值
  */
 uint32_t XCrc32_calculate(const uint8_t* data, size_t length, XCRCByteOrder order);

 /**
  * 更新CRC32校验值（用于流式计算）
  * @param crc 当前CRC值
  * @param data 新数据指针
  * @param length 新数据长度
  * @return 更新后的CRC值
  */
 uint32_t XCrc32_update(uint32_t crc, const uint8_t* data, size_t length);

 /**
  * 获取CRC32校验值（根据字节序调整）
  * @param crc CRC值
  * @param order 字节序
  * @return 调整后的CRC值
  */
 uint32_t XCrc32_finalize(uint32_t crc, XCRCByteOrder order);
#endif // XCrc32_ON

#ifdef __cplusplus
}
#endif
#endif