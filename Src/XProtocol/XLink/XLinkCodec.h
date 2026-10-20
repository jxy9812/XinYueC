/** @file       XLinkCodec.h
 * @brief      XLink 帧编解码——小端序列化原语与帧头编解码公开 API。
 * @details    对齐 MQTT 报文打包/解包的职责划分（字节序转换 + 帧头打包/校验）。
 *             实现只依赖 XinYueC 抽象层，禁止调用 Win32、POSIX 或其他平台 API。
 *             本层无状态、无堆分配；载荷上限校验是唯一的协议违例入口，
 *             由会话层据此判线。
 */
#ifndef XLINKCODEC_H
#define XLINKCODEC_H

#ifdef __cplusplus
extern "C" {
#endif

/* 引入本协议配置，确保开关已定义（必须早于下方条件编译判定） */
#include "XLink_config.h"

#if XLINK_ON && XLINK_CODEC_ON

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include "XLinkProto.h"
#include "XClass.h"

/* ========================================================================== */
/*                        小端序列化原语                                     */
/* ========================================================================== */

/**
 * @brief      将 16 位无符号整数按小端线序写入缓冲区。
 * @param      buf 输出缓冲区（调用方提供存储空间），至少 2 字节。
 * @param      value 待写入的数值。
 * @return     无。buf 为 NULL 时函数不执行任何操作。
 * @note       线上格式一律小端（fbdev/网络栈惯例，ARM 与 x86 一致）。
 */
void XLinkCodec_putU16(uint8_t* buf, uint16_t value);

/**
 * @brief      从缓冲区按小端线序读出 16 位无符号整数。
 * @param      buf 输入缓冲区（借用，不取得所有权），至少 2 字节。
 * @return     读出的数值；buf 为 NULL 时返回 0。
 */
uint16_t XLinkCodec_getU16(const uint8_t* buf);

/**
 * @brief      将 32 位无符号整数按小端线序写入缓冲区。
 * @param      buf 输出缓冲区（调用方提供存储空间），至少 4 字节。
 * @param      value 待写入的数值。
 * @return     无。buf 为 NULL 时函数不执行任何操作。
 */
void XLinkCodec_putU32(uint8_t* buf, uint32_t value);

/**
 * @brief      从缓冲区按小端线序读出 32 位无符号整数。
 * @param      buf 输入缓冲区（借用，不取得所有权），至少 4 字节。
 * @return     读出的数值；buf 为 NULL 时返回 0。
 */
uint32_t XLinkCodec_getU32(const uint8_t* buf);

/* ========================================================================== */
/*                        会话载荷                                           */
/* ========================================================================== */

/**
 * @brief      把 Hello/HelloAck 载荷结构序列化为 4 字节小端线序。
 * @param      payload 待序列化的载荷结构（借用，不取得所有权）。
 * @param      buf 输出缓冲区（调用方提供存储空间），至少 4 字节。
 * @param      capacity 输出缓冲区容量（字节）。
 * @param      outLen 实际写入字节数（调用方提供存储空间）；可为 NULL。
 * @return     成功返回 true；参数为 NULL 或 capacity 不足 4 时返回 false，
 *             此时 buf 与 outLen 保持不变。
 */
bool XLinkCodec_encodeHello(const XLinkHelloPayload* payload, uint8_t* buf,
                            size_t capacity, size_t* outLen);

/**
 * @brief      从小端线序字节反序列化 Hello/HelloAck 载荷。
 * @param      buf 输入缓冲区（借用，不取得所有权），至少 4 字节有效数据。
 * @param      length 输入缓冲区有效长度（字节），须 >= 4。
 * @param      out 输出载荷结构（调用方提供存储空间）。
 * @return     成功返回 true；参数为 NULL 或 length 不足 4 时返回 false，
 *             此时 out 保持不变。
 */
bool XLinkCodec_decodeHello(const uint8_t* buf, size_t length,
                            XLinkHelloPayload* out);

/* ========================================================================== */
/*                        帧头                                               */
/* ========================================================================== */

/**
 * @brief      把帧头结构打包为 12 字节小端线序。
 * @param      header 待打包的帧头（借用，不取得所有权）。
 * @param      buf 输出缓冲区（调用方提供存储空间），至少 XLINK_HEADER_SIZE 字节。
 * @return     成功返回 true；参数为 NULL 时返回 false，buf 保持不变。
 * @note       本函数不校验 m_payloadLen 是否越界（越界由解码侧把关，
 *             保证发帧路径无分支开销）；发送方应自行保证载荷长度合法。
 */
bool XLinkCodec_encodeHeader(const XLinkFrameHeader* header, uint8_t* buf);

/**
 * @brief      从 12 字节小端线序解出帧头，并做载荷上限校验。
 * @param      buf 输入缓冲区（借用，不取得所有权），至少 XLINK_HEADER_SIZE 字节。
 * @param      length 输入缓冲区有效长度（字节），须 >= XLINK_HEADER_SIZE。
 * @param      out 输出帧头（调用方提供存储空间）。
 * @return     成功返回 true；参数为 NULL、length 不足 12 字节，或
 *             payloadLen 越界（> XLINK_MAX_PAYLOAD）时返回 false，
 *             此时 out 保持不变。
 * @warning    载荷上限是防畸形帧撑爆接收缓冲的安全阀，返回 false 时调用方
 *             应判定为协议违例并断开链路，不得继续按该帧头读载荷。
 */
bool XLinkCodec_decodeHeader(const uint8_t* buf, size_t length,
                             XLinkFrameHeader* out);

#endif /* XLINK_ON && XLINK_CODEC_ON */

#ifdef __cplusplus
}
#endif
#endif /* XLINKCODEC_H */
