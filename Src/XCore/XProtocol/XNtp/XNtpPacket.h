/** @file       XNtpPacket.h
 * @brief      XNtp 报文编解码——48 字节大端线序与 XNtpPacket 内存形态互转。
 * @details    NTP 线上字节序为大端（RFC 5905），与仓库其他协议（XLink 小端、
 *             XModbus 大端）互不相干，编解码在本层完成端序隔离。
 *             实现只依赖 XinYueC 抽象层，禁止调用 Win32、POSIX 或其他平台 API。
 */
#ifndef XNTPPACKET_H
#define XNTPPACKET_H

#ifdef __cplusplus
extern "C" {
#endif

/* 引入本协议配置，确保 XNTP_ON / XNTP_CLIENT_ON 已定义
   （必须早于下方条件编译判定，否则直接 include 本头文件时开关未定义、
   整段声明会被跳过） */
#include "XNtp_config.h"

#if XNTP_ON

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "XNtpProto.h"

/* ========================================================================== */
/*                        报文编解码                                         */
/* ========================================================================== */

/**
 * @brief      把内存形态报文编码为 48 字节大端线序。
 * @param      packet 报文结构（借用，不取得所有权）。
 * @param      out 输出缓冲区（调用方提供存储空间），至少 XNTP_PACKET_SIZE 字节。
 * @return     成功返回 true；参数为 NULL 时返回 false，out 保持不变。
 */
bool XNtpPacket_encode(const XNtpPacket* packet, uint8_t* out);

/**
 * @brief      从线序字节解码出内存形态报文。
 * @param      buf 输入缓冲区（借用，不取得所有权）。
 * @param      length 输入有效长度（字节），须 >= XNTP_PACKET_SIZE。
 * @param      out 输出报文（调用方提供存储空间）。
 * @return     成功返回 true；参数为 NULL 或长度不足 48 字节时返回 false，
 *             此时 out 保持不变。
 */
bool XNtpPacket_decode(const uint8_t* buf, size_t length, XNtpPacket* out);

/* ========================================================================== */
/*                        时间戳换算                                         */
/* ========================================================================== */

/**
 * @brief      判断时间戳是否为零值（NTP 约定零 = 字段未填充）。
 * @param      ts 时间戳（借用）。
 * @return     秒与小数部分全为 0 返回 true；ts 为 NULL 也返回 true。
 */
bool XNtpTimestamp_isZero(const XNtpTimestamp* ts);

/**
 * @brief      NTP 时间戳转 Unix 毫秒。
 * @param      ts 时间戳（借用）。
 * @return     Unix 纪元毫秒数（减去 1900 纪元偏移）；溢出与 NULL 返回 0。
 */
uint64_t XNtpTimestamp_toUnixMs(const XNtpTimestamp* ts);

/**
 * @brief      Unix 毫秒转 NTP 时间戳。
 * @param      ts 输出时间戳（调用方提供存储空间）。
 * @param      unixMs Unix 纪元毫秒数。
 * @return     无。早于 1900 纪元的值会被钳制到 0。
 */
void XNtpTimestamp_fromUnixMs(XNtpTimestamp* ts, uint64_t unixMs);

/**
 * @brief      构造客户端请求报文（LI=0、VN=XNTP_VERSION、Mode=Client）。
 * @param      packet 输出报文（调用方提供存储空间）。
 * @param      t1 发出时刻（填入 Transmit 字段；服务器须在应答的 Originate
 *             字段原样回显该值）。
 * @return     无。packet 为 NULL 时不执行任何操作。
 * @note       RFC 5905：客户端请求中 T1 位于 Transmit 字段；其余时间戳按
 *             约定填零。
 */
void XNtpPacket_makeClientRequest(XNtpPacket* packet,
                                  const XNtpTimestamp* t1);

/**
 * @brief      校验一个应答报文的协议合法性（Mode/Stratum/LI/T1 回显）。
 * @param      packet 已解码的应答报文（借用）。
 * @param      expectedOrigin 本端发出的 T1（用于回显比对，防重放/错包）。
 * @return     合法返回 true；不合法返回 false（具体原因由调用方按字段自查，
 *             本函数只做整体快速闸门）。
 */
bool XNtpPacket_isValidServerReply(const XNtpPacket* packet,
                                   const XNtpTimestamp* expectedOrigin);

#endif /* XNTP_ON */

#ifdef __cplusplus
}
#endif
#endif /* XNTPPACKET_H */
