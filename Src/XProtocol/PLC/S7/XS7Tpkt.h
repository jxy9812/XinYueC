#ifndef XS7TPKT_H
#define XS7TPKT_H

#include "XPlc_config.h"
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "XByteArray.h"

#ifdef __cplusplus
extern "C" {
#endif

#if XPROTOCOL_ON
#if XPLC_ON
#if XS7_ON
#if XS7_CORE_ON

/**
 * @file XS7Tpkt.h
 * @brief TPKT 层（RFC1006）组帧与半包判定（纯函数，无IO无状态）
 * @details 帧格式：03 00 | length(BE, 含头) | payload；
 *          仅由 XS7Session_feed 调用，可完全离线单测。
 */

/** @brief 单帧防失控上限（字节）。协商 PDU≤960，块传输单帧也远小于该值 */
#define XS7_MAX_FRAME 8192

/**
 * @brief 组 TPKT 帧
 * @param out 输出字节数组（追加写到尾部，非NULL）
 * @param payload 载荷（TPKT 之后的字节，如 COTP 段）
 * @param payloadLen 载荷长度
 * @return 帧总长（4+payloadLen），失败返回0
 * @note 写入头：ver=3、res=0、length=BE16(4+payloadLen)；超 XS7_MAX_FRAME 返回0
 */
size_t XS7Tpkt_wrap(XByteArray* out, const uint8_t* payload, size_t payloadLen);

/**
 * @brief 只读前4字节判定整帧长度（半包检测）
 * @param data 数据起始指针
 * @param len 可用字节数
 * @param total 输出：整帧总长（读到第4字节后有效）
 * @return 可以开始整帧处理返回true；
 *         字节数<4（半包）、ver!=3、length<4 或 length>XS7_MAX_FRAME（坏头）返回false
 * @note 返回false不区分"半包等待更多数据"与"坏头丢弃"，调用方按
 *       len<4 等待、否则丢弃推进处理（见 XS7Session_feed）
 */
bool XS7Tpkt_peekLength(const uint8_t* data, size_t len, size_t* total);

#endif /* XS7_CORE_ON */
#endif /* XS7_ON */
#endif /* XPLC_ON */
#endif /* XPROTOCOL_ON */

#ifdef __cplusplus
}
#endif

#endif // XS7TPKT_H
