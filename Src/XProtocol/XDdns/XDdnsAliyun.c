/** @file       XDdnsAliyun.c
 * @brief      阿里云解析（Alibaba Cloud DNS）厂商实现——POP 签名 + alidns API。
 * @details    签名流程（阿里云 POP RPC 风格，官方文档《签名机制》）：
 * @code
 *   1. 组装公共参数 + Action 参数：
 *        AccessKeyId / Action / Format=JSON / SignatureMethod=HMAC-SHA1 /
 *        SignatureNonce(随机) / SignatureVersion=1.0 /
 *        Timestamp(UTC ISO8601) / Version=2015-01-09 + 业务参数
 *   2. 按参数名升序排序，k=v 用 & 连接，k 与 v 均做 POP 百分号编码
 *      （RFC3986 unreserved = A-Za-z0-9-_.，但 ~ 和 * 也强制编码），
 *      得到规范化查询串 canonicalizedQueryString。
 *   3. StringToSign = "GET" + "&" + percentEncode("/") + "&"
 *                    + percentEncode(canonicalizedQueryString)
 *   4. Signature = Base64( HMAC-SHA1( StringToSign, AccessSecret + "&" ) )
 *   5. 最终 URL = endpoint + "/?" + canonical + "&Signature=" + percentEncode(sig)
 * @endcode
 *             秘钥只参与本地 HMAC 计算，永不过网；SignatureNonce + Timestamp
 *             由云端防重放。实现只依赖 XinYueC 抽象层，禁止调用 Win32、
 *             POSIX 或其他平台 API。
 * @author     XinYueC 团队
 */
#include "XDdnsAliyun.h"

#if XDDNS_ON && XDDNS_ALIYUN_ON

#include "XDdnsProvider.h"
#include "XUrl.h"
#include "XString.h"
#include "XByteArray.h"
#include "XDateTime.h"
#include "XRandomGenerator.h"
#include "XCryptographicHash.h"
#include "XBase64.h"
#include "XJsonDocument.h"
#include "XJsonValue.h"
#include "XJsonObject.h"
#include "XPrintf.h"
#include "XStringUtils.h"
#include <string.h>

/* ========================================================================== */
/*                        内部常量                                           */
/* ========================================================================== */

/** @brief 参数表上限（公共 7 个 + Action 1 个 + 业务最多 4 个，余量充足）。 */
#define XDDNS_ALIYUN_MAX_PARAMS 16u
/** @brief 单个参数值缓冲上限。 */
#define XDDNS_ALIYUN_VALUE_MAX 512u

/** @brief 参数键值对（排序用）。 */
typedef struct XDdnsAliyunParam {
    char m_name[64];
    char m_value[XDDNS_ALIYUN_VALUE_MAX];
} XDdnsAliyunParam;

/* ========================================================================== */
/*                        编码与签名原语                                     */
/* ========================================================================== */

/**
 * @brief POP 百分号编码：RFC3986 unreserved 集合，但 ~ 与 * 强制编码。
 * @param input 输入字符串（UTF-8）。
 * @param out 输出缓冲区（调用方提供存储空间）。
 * @param cap 输出缓冲区容量。
 * @return 成功返回 true；缓冲不足返回 false。
 */
static bool xdns_aliyun_percent_encode(const char* input, char* out, size_t cap)
{
    XString* in = NULL;
    XString* include = NULL;
    XString* encoded = NULL;
    const char* utf8;
    bool ok = false;

    if (!input || !out || cap == 0)
        return false;
    out[0] = '\0';
    in = XString_create_utf8(input);
    include = XString_create_utf8("~*");
    if (!in || !include) {
        goto cleanup;
    }
    encoded = XUrl_toPercentEncoding(in, NULL, include);
    if (!encoded)
        goto cleanup;
    utf8 = XString_toUtf8(encoded);
    if (!utf8 || XStrlen(utf8) >= cap)
        goto cleanup;
    memcpy(out, utf8, XStrlen(utf8) + 1); /* 长度已校验 < cap */
    ok = true;
cleanup:
    if (in) XClassDelete(in);
    if (include) XClassDelete(include);
    if (encoded) XClassDelete(encoded);
    return ok;
}

/**
 * @brief 生成 UTC ISO8601 时间戳（yyyy-MM-ddTHH:mm:ssZ，零填充）。
 * @note  不用 XDateTime_toString_format：其数字段无零填充，7 月会输出 "7"
 *        而非 "07"，不符合阿里云要求。
 */
static void xdns_aliyun_timestamp(char* out, size_t cap)
{
    XDateTime now = XDateTime_currentDateTimeUtc();
    XSnprintf(out, cap, "%04d-%02d-%02dT%02d:%02d:%02dZ",
              (int)XDate_year(&now.m_date), (int)XDate_month(&now.m_date),
              (int)XDate_day(&now.m_date), (int)XTime_hour(&now.m_time),
              (int)XTime_minute(&now.m_time), (int)XTime_second(&now.m_time));
}

/**
 * @brief 生成随机 SignatureNonce（8 字节安全随机 → 16 字符 hex）。
 */
static void xdns_aliyun_nonce(char* out, size_t cap)
{
    uint8_t raw[8];
    if (cap < 17)
        return;
    if (!XRandomGenerator_fillSecure(raw, sizeof(raw))) {
        XRandomGenerator_random();
        memcpy(raw, &raw, 0); /* 无害占位：fillSecure 失败极罕见 */
    }
    XSnprintf(out, cap, "%02x%02x%02x%02x%02x%02x%02x%02x",
              raw[0], raw[1], raw[2], raw[3],
              raw[4], raw[5], raw[6], raw[7]);
}

/* ========================================================================== */
/*                        参数表与规范化                                     */
/* ========================================================================== */

/** @brief 追加一个参数；容量不足静默丢弃（参数个数受控，不会发生）。
 *  @note  值为纯字符串（调用点需格式化的先 XSnprintf 到临时缓冲）。 */
static void xdns_aliyun_param_add(XDdnsAliyunParam* params, int* count,
                                  const char* name, const char* value)
{
    XDdnsAliyunParam* p;
    if (!params || !count || *count >= (int)XDDNS_ALIYUN_MAX_PARAMS)
        return;
    p = &params[(*count)++];
    XStrncpy(p->m_name, name, sizeof(p->m_name) - 1);
    XStrncpy(p->m_value, value, sizeof(p->m_value) - 1);
}

/** @brief 按参数名升序插入排序（数量 <=16，简单排序足够）。 */
static void xdns_aliyun_param_sort(XDdnsAliyunParam* params, int count)
{
    int i;
    int j;
    for (i = 1; i < count; i++) {
        XDdnsAliyunParam tmp = params[i];
        j = i - 1;
        while (j >= 0 && strcmp(params[j].m_name, tmp.m_name) > 0) {
            params[j + 1] = params[j];
            j--;
        }
        params[j + 1] = tmp;
    }
}

/**
 * @brief 组装规范化查询串：enc(k)=enc(v) 用 & 连接。
 * @param params 已排序参数表。
 * @param count 参数个数。
 * @param out 输出缓冲区。
 * @param cap 输出缓冲区容量。
 * @return 成功返回 true；编码失败或缓冲不足返回 false。
 */
static bool xdns_aliyun_canonical(const XDdnsAliyunParam* params, int count,
                                  char* out, size_t cap)
{
    size_t used = 0;
    int i;
    out[0] = '\0';
    for (i = 0; i < count; i++) {
        char encName[XDDNS_ALIYUN_VALUE_MAX];
        char encValue[XDDNS_ALIYUN_VALUE_MAX];
        size_t len;

        if (!xdns_aliyun_percent_encode(params[i].m_name, encName, sizeof(encName)))
            return false;
        if (!xdns_aliyun_percent_encode(params[i].m_value, encValue, sizeof(encValue)))
            return false;
        len = XStrlen(encName) + 1 + XStrlen(encValue) + (i ? 1 : 0);
        if (used + len + 1 > cap)
            return false;
        if (i)
            out[used++] = '&';
        /* 长度已预先校验（len 计入分隔符与结尾 NUL），memcpy 安全 */
        memcpy(out + used, encName, XStrlen(encName));
        used += XStrlen(encName);
        out[used++] = '=';
        memcpy(out + used, encValue, XStrlen(encValue));
        used += XStrlen(encValue);
    }
    out[used] = '\0';
    return true;
}

/**
 * @brief 计算签名并追加到 URL。
 * @param canonical 已排序编码的规范化查询串。
 * @param secret AccessKey Secret。
 * @param outUrl 输入输出：已含 "endpoint/?canonical"，追加 "&Signature=..."。
 * @param cap outUrl 容量。
 * @return 成功返回 true。
 */
static bool xdns_aliyun_append_signature(const char* canonical,
                                         const char* secret,
                                         char* outUrl, size_t cap)
{
    char stringToSign[2 * XDDNS_URL_MAX];
    char encSign[XDDNS_URL_MAX];
    char encPath[] = "%2F";
    char keyBuf[XDDNS_NAME_MAX + 2];
    XByteArray* digest = NULL;
    char sigB64[64];
    size_t sigLen = sizeof(sigB64);
    size_t keyLen;
    bool ok = false;

    /* StringToSign = "GET&%2F&" + percentEncode(canonical) */
    if (!xdns_aliyun_percent_encode(canonical,
                                    stringToSign + strlen("GET&%2F&"),
                                    sizeof(stringToSign) - strlen("GET&%2F&")))
        return false;
    memcpy(stringToSign, "GET&", 4);
    memcpy(stringToSign + 4, encPath, strlen(encPath));
    stringToSign[4 + strlen(encPath)] = '&';

    /* HMAC-SHA1(StringToSign, AccessSecret + "&") → Base64 */
    keyLen = XStrlen(secret);
    if (keyLen + 2 > sizeof(keyBuf))
        return false;
    memcpy(keyBuf, secret, keyLen);
    keyBuf[keyLen] = '&';
    keyBuf[keyLen + 1] = '\0';
    digest = XCryptographicHash_hmac(keyBuf, keyLen + 1, stringToSign,
                                     XStrlen(stringToSign),
                                     XCryptographicHash_Sha1);
    if (!digest)
        return false;
    if (XBase64_encode(
            (const uint8_t*)XByteArray_constData(digest),
            (size_t)XContainer_size_base((const XContainer*)digest),
            sigB64, &sigLen) == 0) {
        if (xdns_aliyun_percent_encode(sigB64, encSign, sizeof(encSign))) {
            size_t used = XStrlen(outUrl);
            if (used + 1 + 10 + XStrlen(encSign) + 1 <= cap) {
                outUrl[used++] = '&';
                memcpy(outUrl + used, "Signature=", 10);
                used += 10;
                memcpy(outUrl + used, encSign, XStrlen(encSign) + 1);
                ok = true;
            }
        }
    }
    XClassDelete(digest);
    return ok;
}

/* ========================================================================== */
/*                        公共参数与 URL 构造                                */
/* ========================================================================== */

/**
 * @brief 组装公共参数（不含 Signature）。
 * @return 参数个数；ctx 缺凭据返回 -1。
 */
static int xdns_aliyun_common_params(const XDdnsRequestContext* ctx,
                                     XDdnsAliyunParam* params)
{
    char nonce[24];
    char timestamp[32];
    int count = 0;

    if (!ctx->m_accessKeyId || !ctx->m_accessSecret || !ctx->m_domain || !ctx->m_rr)
        return -1;

    if (ctx->m_nonce)
        XStrncpy(nonce, ctx->m_nonce, sizeof(nonce) - 1);
    else
        xdns_aliyun_nonce(nonce, sizeof(nonce));
    if (ctx->m_timestamp)
        XStrncpy(timestamp, ctx->m_timestamp, sizeof(timestamp) - 1);
    else
        xdns_aliyun_timestamp(timestamp, sizeof(timestamp));

    xdns_aliyun_param_add(params, &count, "AccessKeyId", ctx->m_accessKeyId);
    xdns_aliyun_param_add(params, &count, "Format", "JSON");
    xdns_aliyun_param_add(params, &count, "SignatureMethod", "HMAC-SHA1");
    xdns_aliyun_param_add(params, &count, "SignatureNonce", nonce);
    xdns_aliyun_param_add(params, &count, "SignatureVersion", "1.0");
    xdns_aliyun_param_add(params, &count, "Timestamp", timestamp);
    xdns_aliyun_param_add(params, &count, "Version", XDDNS_ALIYUN_VERSION);
    return count;
}

/**
 * @brief 最终 URL 组装：endpoint/?canonical&Signature=...（公共收尾）。
 */
static bool xdns_aliyun_finish_url(const XDdnsRequestContext* ctx,
                                   XDdnsAliyunParam* params, int count,
                                   char* outUrl, size_t cap)
{
    char canonical[XDDNS_URL_MAX];
    const char* endpoint;

    xdns_aliyun_param_sort(params, count);
    if (!xdns_aliyun_canonical(params, count, canonical, sizeof(canonical)))
        return false;

    endpoint = (ctx->m_endpoint && ctx->m_endpoint[0])
                   ? ctx->m_endpoint : XDDNS_ALIYUN_ENDPOINT;
    /* 容量已校验（endpoint + "/?" + canonical + NUL），memcpy 安全 */
    memcpy(outUrl, endpoint, XStrlen(endpoint));
    memcpy(outUrl + XStrlen(endpoint), "/?", 2);
    memcpy(outUrl + XStrlen(endpoint) + 2, canonical, XStrlen(canonical) + 1);
    return xdns_aliyun_append_signature(canonical, ctx->m_accessSecret,
                                        outUrl, cap);
}

/* ========================================================================== */
/*                        三个 Action 的 URL 构造                            */
/* ========================================================================== */

static bool xdns_aliyun_buildQueryUrl(const XDdnsRequestContext* ctx,
                                      char* outUrl, size_t cap)
{
    XDdnsAliyunParam params[XDDNS_ALIYUN_MAX_PARAMS];
    int count = xdns_aliyun_common_params(ctx, params);
    if (count < 0)
        return false;
    /* DescribeDomainRecords（关键词匹配 DomainName 下的记录），而非
     * DescribeSubDomainRecords —— 后者要求 SubDomain=<rr>.<domain> 全名参数，
     * 传 RR 会得到 MissingSubDomain。精确的 RR/Type 匹配由 parseQuery 过滤
     * （rrKeyWord 只是粗筛）。 */
    xdns_aliyun_param_add(params, &count, "Action",
                          "DescribeDomainRecords");
    xdns_aliyun_param_add(params, &count, "DomainName", ctx->m_domain);
    xdns_aliyun_param_add(params, &count, "RRKeyWord", ctx->m_rr);
    xdns_aliyun_param_add(params, &count, "Type",
                          XDdnsRecordType_name(ctx->m_type));
    return xdns_aliyun_finish_url(ctx, params, count, outUrl, cap);
}

static bool xdns_aliyun_buildAddUrl(const XDdnsRequestContext* ctx,
                                    const char* ip, char* outUrl, size_t cap)
{
    XDdnsAliyunParam params[XDDNS_ALIYUN_MAX_PARAMS];
    int count = xdns_aliyun_common_params(ctx, params);
    if (count < 0)
        return false;
    xdns_aliyun_param_add(params, &count, "Action", "AddDomainRecord");
    xdns_aliyun_param_add(params, &count, "DomainName", ctx->m_domain);
    xdns_aliyun_param_add(params, &count, "RR", ctx->m_rr);
    xdns_aliyun_param_add(params, &count, "Type",
                          XDdnsRecordType_name(ctx->m_type));
    xdns_aliyun_param_add(params, &count, "Value", ip);
    xdns_aliyun_param_add(params, &count, "TTL", "600");
    return xdns_aliyun_finish_url(ctx, params, count, outUrl, cap);
}

static bool xdns_aliyun_buildUpdateUrl(const XDdnsRequestContext* ctx,
                                       const char* recordId, const char* ip,
                                       char* outUrl, size_t cap)
{
    XDdnsAliyunParam params[XDDNS_ALIYUN_MAX_PARAMS];
    int count = xdns_aliyun_common_params(ctx, params);
    if (count < 0)
        return false;
    xdns_aliyun_param_add(params, &count, "Action", "UpdateDomainRecord");
    xdns_aliyun_param_add(params, &count, "RecordId", recordId);
    xdns_aliyun_param_add(params, &count, "RR", ctx->m_rr);
    xdns_aliyun_param_add(params, &count, "Type",
                          XDdnsRecordType_name(ctx->m_type));
    xdns_aliyun_param_add(params, &count, "Value", ip);
    return xdns_aliyun_finish_url(ctx, params, count, outUrl, cap);
}

/* ========================================================================== */
/*                        应答解析                                           */
/* ========================================================================== */

/**
 * @brief 从应答体提取 JSON 根对象；HTTP 层已保证 2xx 才进来。
 * @return 根对象指针（借用，随 doc 失效）；解析失败返回 NULL。
 */
static const XJsonObject* xdns_aliyun_root(const char* body, size_t len,
                                           XJsonDocument** docOut)
{
    XByteArray* bytes;
    XJsonDocument* doc;
    const XJsonValue* root;
    XJsonObject* obj;

    *docOut = NULL;
    if (!body || len == 0)
        return NULL;
    bytes = XByteArray_create_with_data(body, (size_t)len);
    if (!bytes)
        return NULL;
    doc = XJsonDocument_fromJson(bytes);
    XClassDelete(bytes);
    if (!doc)
        return NULL;
    root = XJsonDocument_root_const(doc);
    obj = root ? XJsonValue_toObject(root) : NULL;
    if (!obj) {
        XClassDelete(doc);
        return NULL;
    }
    *docOut = doc;
    return obj;
}

/**
 * @brief 检查对象是否含有指定键（键缺失时 value_keyUtf8 返回堆上 Undefined
 *        值而非 NULL，须按本函数口径判别并释放）。
 */
static bool xdns_aliyun_member_exists(const XJsonObject* obj, const char* key)
{
    XJsonValue* value = XJsonObject_value_keyUtf8(obj, key);
    bool exists = false;
    if (value) {
        exists = !XJsonValue_isUndefined(value);
        XJsonValue_delete(value);
    }
    return exists;
}

/**
 * @brief 读取对象中的字符串成员到定长缓冲区。
 * @param  obj 对象（借用，须仍存活）。
 * @param  key 键名。
 * @param  out 输出缓冲区（调用方提供存储空间）。
 * @param  cap 输出缓冲区容量。
 * @return 键存在且为字符串时返回 true（含空串）；缺失/类型不符返回 false。
 * @note   value_keyUtf8 返回堆上深拷贝，本函数负责释放。
 */
static bool xdns_aliyun_member_string(const XJsonObject* obj, const char* key,
                                      char* out, size_t cap)
{
    XJsonValue* value = XJsonObject_value_keyUtf8(obj, key);
    const XString* str = NULL;
    bool ok = false;

    if (!value)
        return false;
    if (!XJsonValue_isUndefined(value)) {
        str = XJsonValue_toString(value);
        if (str && XString_toUtf8(str)) {
            XStrncpy(out, XString_toUtf8(str), cap - 1);
            ok = true;
        }
    }
    XJsonValue_delete(value);
    return ok;
}

/**
 * @brief 检查业务错误形态（JSON 顶层含 Code 字段 = 云端报错）。
 */
static bool xdns_aliyun_has_error(const char* body, size_t len)
{
    XJsonDocument* doc = NULL;
    const XJsonObject* obj = xdns_aliyun_root(body, len, &doc);
    bool hasError = false;
    if (obj) {
        hasError = xdns_aliyun_member_exists(obj, "Code");
    }
    if (doc) XClassDelete(doc);
    return hasError;
}

static bool xdns_aliyun_parseQuery(const char* body, size_t len, const char* rr,
                                   XDdnsRecordType type, XDdnsRecordInfo* outRecord)
{
    XJsonDocument* doc = NULL;
    const XJsonObject* obj;
    XJsonValue* domainRecordsVal = NULL;
    XJsonValue* recordArrayVal = NULL;
    XJsonObject* records = NULL;
    XJsonArray* array = NULL;
    int i;
    int count;
    const char* typeText = XDdnsRecordType_name(type);
    bool ok = false;

    if (!outRecord)
        return false;
    outRecord->m_recordId[0] = '\0';
    outRecord->m_rr[0] = '\0';
    outRecord->m_type[0] = '\0';
    outRecord->m_value[0] = '\0';

    obj = xdns_aliyun_root(body, len, &doc);
    if (!obj)
        return false;
    if (xdns_aliyun_has_error(body, len)) {
        XClassDelete(doc);
        return false;
    }

    /* 嵌套取值：value_keyUtf8 返回堆上深拷贝值，全部保持存活至遍历结束，
       最后逆序释放（toObject/toArray 返回其内部指针，随值共存亡）。 */
    domainRecordsVal = XJsonObject_value_keyUtf8(obj, "DomainRecords");
    records = domainRecordsVal ? XJsonValue_toObject(domainRecordsVal) : NULL;
    recordArrayVal = records ? XJsonObject_value_keyUtf8(records, "Record")
                             : NULL;
    array = recordArrayVal ? XJsonValue_toArray(recordArrayVal) : NULL;
    if (!array) {
        /* 结构不符合预期（无 DomainRecords.Record 数组） */
        if (recordArrayVal) XJsonValue_delete(recordArrayVal);
        if (domainRecordsVal) XJsonValue_delete(domainRecordsVal);
        XClassDelete(doc);
        return false;
    }

    count = (int)XJsonArray_size_base(array);
    for (i = 0; i < count && !ok; i++) {
        XJsonValue* item = XJsonArray_at(array, i);
        XJsonObject* rec = item ? XJsonValue_toObject(item) : NULL;
        char itemRr[XDDNS_NAME_MAX];
        char itemType[16];
        char itemId[XDDNS_NAME_MAX];
        char itemValue[XDDNS_VALUE_MAX];

        if (!rec)
            continue;
        if (!xdns_aliyun_member_string(rec, "RR", itemRr, sizeof(itemRr)))
            continue;
        if (!xdns_aliyun_member_string(rec, "Type", itemType, sizeof(itemType)))
            continue;
        if (strcmp(itemRr, rr) != 0 || strcmp(itemType, typeText) != 0)
            continue; /* RR 或 Type 不匹配：跳过 */
        if (!xdns_aliyun_member_string(rec, "RecordId", itemId, sizeof(itemId)))
            continue;
        if (!xdns_aliyun_member_string(rec, "Value", itemValue,
                                       sizeof(itemValue)))
            continue;
        XStrncpy(outRecord->m_recordId, itemId, sizeof(outRecord->m_recordId) - 1);
        XStrncpy(outRecord->m_value, itemValue, sizeof(outRecord->m_value) - 1);
        XStrncpy(outRecord->m_rr, rr, sizeof(outRecord->m_rr) - 1);
        XStrncpy(outRecord->m_type, typeText, sizeof(outRecord->m_type) - 1);
        ok = true;
    }

    if (recordArrayVal) XJsonValue_delete(recordArrayVal);
    if (domainRecordsVal) XJsonValue_delete(domainRecordsVal);
    XClassDelete(doc);
    /* "无匹配记录"也是合法解析结果：recordId 为空表示需要 Add */
    return true;
}

static bool xdns_aliyun_parseMutation(const char* body, size_t len)
{
    XJsonDocument* doc = NULL;
    const XJsonObject* obj;
    bool ok = false;

    obj = xdns_aliyun_root(body, len, &doc);
    if (obj) {
        /* Add/Update 成功应答含 RecordId 或 RequestId，且无 Code 错误字段 */
        ok = !xdns_aliyun_has_error(body, len)
             && (xdns_aliyun_member_exists(obj, "RecordId")
                 || xdns_aliyun_member_exists(obj, "RequestId"));
    }
    if (doc) XClassDelete(doc);
    return ok;
}

/* ========================================================================== */
/*                        操作表                                             */
/* ========================================================================== */

static const XDdnsProviderOps g_xdnsAliyunOps = {
    "aliyun",
    XDDNS_ALIYUN_ENDPOINT, /* m_defaultEndpoint */
    xdns_aliyun_buildQueryUrl,
    xdns_aliyun_buildAddUrl,
    xdns_aliyun_buildUpdateUrl,
    xdns_aliyun_parseQuery,
    xdns_aliyun_parseMutation
};

const XDdnsProviderOps* XDdnsProvider_aliyun(void)
{
    return &g_xdnsAliyunOps;
}

#endif /* XDDNS_ON && XDDNS_ALIYUN_ON */
