/** @file       XDdnsProto.h
 * @brief      XDdns 动态域名解析——公共类型定义（记录类型/状态/错误码）。
 * @details    实现只依赖 XinYueC 抽象层，禁止调用 Win32、POSIX 或其他平台 API。
 */
#ifndef XDDNSPROTO_H
#define XDDNSPROTO_H

#ifdef __cplusplus
extern "C" {
#endif

/* include config before #if */
#include "XDdns_config.h"

#if XDDNS_ON

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "XDdns_config.h"

/* ========================================================================== */
/*                        记录类型                                           */
/* ========================================================================== */

/**
 * @brief DNS 记录类型（当前支持 A/AAAA；枚举值同时作为线上 Type 参数语义）。
 */
typedef enum XDdnsRecordType {
    XDdnsRecordType_A    = 0, /**< IPv4 地址记录。 */
    XDdnsRecordType_AAAA = 1  /**< IPv6 地址记录。 */
} XDdnsRecordType;

/**
 * @brief 记录类型 → 线上类型字符串（"A"/"AAAA"）。
 * @param type 记录类型。
 * @return 类型字符串常量；未知类型返回 "A"。
 */
const char* XDdnsRecordType_name(XDdnsRecordType type);

/* ========================================================================== */
/*                        客户端状态与错误                                   */
/* ========================================================================== */

/**
 * @brief 客户端状态。
 */
typedef enum XDdnsClientState {
    XDdnsClientState_Idle         = 0, /**< 未配置或从未执行。 */
    XDdnsClientState_Detecting    = 1, /**< 正在探测公网 IP。 */
    XDdnsClientState_Updating     = 2, /**< 正在查询/更新 DNS 记录。 */
    XDdnsClientState_Synchronized = 3, /**< 最近一次检查发现记录已与 IP 一致。 */
    XDdnsClientState_Failed       = 4  /**< 最近一次检查失败（原因见 lastError）。 */
} XDdnsClientState;

/**
 * @brief 检查失败原因。
 */
typedef enum XDdnsError {
    XDdnsError_None          = 0, /**< 无错误。 */
    XDdnsError_NoConfig      = 1, /**< 缺少必要配置（AccessKey/域名等）。 */
    XDdnsError_ProbeExhausted= 2, /**< 所有公网探测接口均不可达。 */
    XDdnsError_BadIp         = 3, /**< 探测内容不是合法的对应类型 IP。 */
    XDdnsError_Socket        = 4, /**< 连接/发送失败。 */
    XDdnsError_Timeout       = 5, /**< 请求超时。 */
    XDdnsError_HttpStatus    = 6, /**< HTTP 非 2xx。 */
    XDdnsError_ApiError      = 7, /**< 云端返回业务错误（JSON Code）。 */
    XDdnsError_BadResponse   = 8  /**< 应答体无法解析。 */
} XDdnsError;

/**
 * @brief 一条 DNS 记录的关键信息（查询应答解析产物）。
 */
typedef struct XDdnsRecordInfo {
    char m_recordId[XDDNS_NAME_MAX]; /**< 厂商记录 ID（更新时必需）。 */
    char m_rr[XDDNS_NAME_MAX];       /**< 主机记录（子域名前缀）。 */
    char m_type[16];                 /**< 记录类型字符串（"A"/"AAAA"）。 */
    char m_value[XDDNS_VALUE_MAX];   /**< 记录值（IP 地址字符串）。 */
} XDdnsRecordInfo;

#endif /* XDDNS_ON */

#ifdef __cplusplus
}
#endif
#endif /* XDDNSPROTO_H */
