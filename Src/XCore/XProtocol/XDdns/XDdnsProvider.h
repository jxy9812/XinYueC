/** @file       XDdnsProvider.h
 * @brief      XDdns 厂商抽象层——DNS 服务商接入接口定义与查找。
 * @details    每个云厂商实现一组 ops（构造签名请求 URL + 解析应答），
 *             客户端编排逻辑与厂商解耦：新增 DNSPod/Cloudflare 等只需
 *             新增一个实现文件并登记进查找表。
 *             实现只依赖 XinYueC 抽象层，禁止调用 Win32、POSIX 或其他平台 API。
 */
#ifndef XDDNSPROVIDER_H
#define XDDNSPROVIDER_H

#ifdef __cplusplus
extern "C" {
#endif

/* include config before #if */
#include "XDdns_config.h"

#if XDDNS_ON

#include "XDdnsProto.h"

/* ========================================================================== */
/*                        请求上下文                                         */
/* ========================================================================== */

/**
 * @brief 单次 API 请求的上下文（凭据 + 记录定位 + 测试注入点）。
 * @note  m_nonce/m_timestamp 供签名测试注入固定值（确定性向量）；运行期为
 *        NULL 表示由厂商实现自行生成。全部字符串为借用指针，调用期间有效。
 */
typedef struct XDdnsRequestContext {
    const char* m_accessKeyId;     /**< AccessKey ID。 */
    const char* m_accessSecret;    /**< AccessKey Secret（只参与本地签名，不过网）。 */
    const char* m_domain;          /**< 主域名（如 "example.com"）。 */
    const char* m_rr;              /**< 主机记录（如 "www"；"@") 表示根域）。 */
    XDdnsRecordType m_type;        /**< 记录类型（A/AAAA）。 */
    const char* m_endpoint;        /**< API 端点（含 scheme，如官方端点或测试 mock）。 */
    const char* m_nonce;           /**< 测试注入 nonce；NULL=运行期生成。 */
    const char* m_timestamp;       /**< 测试注入 UTC 时间戳；NULL=运行期生成。 */
} XDdnsRequestContext;

/* ========================================================================== */
/*                        厂商操作表                                         */
/* ========================================================================== */

/**
 * @brief 厂商操作表：构造签名请求 + 解析应答。
 * @note  所有 build* 输出完整的请求 URL（含签名参数），客户端直接 GET；
 *        outUrl 容量不足或上下文缺凭据时返回 false。
 */
typedef struct XDdnsProviderOps {
    const char* m_name;            /**< 厂商名（"aliyun"），供 setProvider 查找。 */
    const char* m_defaultEndpoint; /**< 厂商默认 API 端点（含 scheme）；客户端
                                        切换厂商时据此恢复默认端点。 */

    /**
     * @brief  构造「查询记录」请求 URL。
     * @param  ctx 请求上下文（借用）。
     * @param  outUrl 输出缓冲区（调用方提供存储空间）。
     * @param  cap 输出缓冲区容量。
     * @return 成功返回 true。
     */
    bool (*buildQueryUrl)(const XDdnsRequestContext* ctx, char* outUrl, size_t cap);

    /**
     * @brief  构造「新增记录」请求 URL（记录不存在时调用）。
     * @param  ctx 请求上下文（借用）。
     * @param  ip 要写入的 IP 地址字符串。
     * @param  outUrl 输出缓冲区（调用方提供存储空间）。
     * @param  cap 输出缓冲区容量。
     * @return 成功返回 true。
     */
    bool (*buildAddUrl)(const XDdnsRequestContext* ctx, const char* ip,
                        char* outUrl, size_t cap);

    /**
     * @brief  构造「更新记录」请求 URL（记录存在且值不同时调用）。
     * @param  ctx 请求上下文（借用）。
     * @param  recordId 厂商记录 ID（来自 parseQuery）。
     * @param  ip 要写入的 IP 地址字符串。
     * @param  outUrl 输出缓冲区（调用方提供存储空间）。
     * @param  cap 输出缓冲区容量。
     * @return 成功返回 true。
     */
    bool (*buildUpdateUrl)(const XDdnsRequestContext* ctx, const char* recordId,
                           const char* ip, char* outUrl, size_t cap);

    /**
     * @brief  解析「查询记录」应答，定位 rr+type 匹配的记录。
     * @param  body 应答体（借用）。
     * @param  len 应答体长度。
     * @param  rr 主机记录。
     * @param  type 记录类型。
     * @param  outRecord 输出记录信息（调用方提供存储空间）；记录不存在时
     *         m_recordId[0] 置 0。
     * @return 应答体本身可解析（含"无记录"）返回 true；应答体损坏返回 false。
     */
    bool (*parseQuery)(const char* body, size_t len, const char* rr,
                       XDdnsRecordType type, XDdnsRecordInfo* outRecord);

    /**
     * @brief  判定「新增/更新」应答是否成功。
     * @param  body 应答体（借用）。
     * @param  len 应答体长度。
     * @return 成功返回 true（含 HTTP 200 下的 JSON 确认）；业务错误返回 false。
     */
    bool (*parseMutation)(const char* body, size_t len);
} XDdnsProviderOps;

/**
 * @brief  按名称查找厂商操作表。
 * @param  name 厂商名（如 "aliyun"）。
 * @return 操作表指针（静态存储期，不释放）；未注册返回 NULL。
 */
const XDdnsProviderOps* XDdnsProvider_find(const char* name);

/**
 * @brief  已注册厂商数量。
 * @return 厂商数（>=0）；模块裁剪后可能为 0。
 */
int XDdnsProvider_count(void);

/**
 * @brief  按下标取厂商操作表（供 GUI 下拉框枚举填充）。
 * @param  index 下标（0..count-1）。
 * @return 操作表指针（静态存储期，不释放）；越界返回 NULL。
 */
const XDdnsProviderOps* XDdnsProvider_at(int index);

#endif /* XDDNS_ON */

#ifdef __cplusplus
}
#endif
#endif /* XDDNSPROVIDER_H */
