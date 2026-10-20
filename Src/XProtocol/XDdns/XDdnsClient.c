/** @file       XDdnsClient.c
 * @brief      XDdns 客户端实现——探测公网 IP → 比对 → 增/改/跳过的编排。
 * @details    HTTP 同步等待照抄 XUpgradeEngine 的 xup_http_get_bytes 口径：
 *             deadline + processEvents 自旋 + 超时 abort + 等
 *             activeReplyCount 归零后再释放 reply（管理器把事务清理投递到
 *             事件队列，直接删是悬垂借用）。
 * @author     XinYueC 团队
 */
#include "XDdnsClient.h"

#if XDDNS_ON && XDDNS_CLIENT_ON

#include "XDdnsClient_Protected.h"
#include "XDdnsProvider.h"
#include "XDdnsHttp.h"
#include "XHttpRequest.h"
#include "XHttpReply.h"
#include "XNetworkAccessManager.h"
#include "XHostAddress.h"
#include "XDateTime.h"
#include "XThread.h"
#include "XVarList.h"
#include "XCoreApplication.h"
#include "XMemory.h"
#include <string.h>

/* ========================================================================== */
/*                        内部辅助前置声明                                   */
/* ========================================================================== */

static void VXDdnsClient_deinit(XClass* object);
static void VXDdnsClient_timerEvent(XObject* obj, XTimerEvent* event);
static void xdns_fail(XDdnsClient* self, XDdnsError error);
static void xdns_captureApiError(XDdnsClient* self, const char* body);
static bool xdns_sync_get(XDdnsClient* self, const char* urlUtf8,
                          char* bodyOut, size_t cap);
static void xdns_emit_ip(XDdnsClient* self, void* signal, const char* ipUtf8);

/* ========================================================================== */
/*                        静态虚函数实现                                     */
/* ========================================================================== */

static void VXDdnsClient_deinit(XClass* object)
{
    XDdnsClient* self;
    XDdnsClientPrivate* d;
    int i;
    if (!object)
        return;
    self = (XDdnsClient*)object;
    d = self->m_d;
    if (!d)
        return;
    if (d->m_checkTimer) {
        XObject_killTimer((XObject*)self, d->m_checkTimer);
        d->m_checkTimer = 0;
    }
    if (d->m_provider) { /* 静态存储期，仅清引用 */ d->m_provider = NULL; }
#define XDNS_FREE_STR(field)                     \
    do {                                         \
        if (d->field) {                          \
            XClassDelete(d->field);              \
            d->field = NULL;                     \
        }                                        \
    } while (0)
    XDNS_FREE_STR(m_endpoint);
    XDNS_FREE_STR(m_keyId);
    XDNS_FREE_STR(m_keySecret);
    XDNS_FREE_STR(m_domain);
    XDNS_FREE_STR(m_rr);
    XDNS_FREE_STR(m_manualIp);
    XDNS_FREE_STR(m_currentIp);
    XDNS_FREE_STR(m_pushedIp);
    for (i = 0; i < (int)XDDNS_MAX_PROBE_URLS; i++)
        XDNS_FREE_STR(m_probeUrls[i]);
#undef XDNS_FREE_STR
    XFree_System(d);
    self->m_d = NULL;
}

static void VXDdnsClient_timerEvent(XObject* obj, XTimerEvent* event)
{
    XDdnsClient* self = (XDdnsClient*)obj;
    if (!obj || !event)
        return;
    XEvent_accept(event);
    if (self->m_d && XTimerEvent_timerId(event) == self->m_d->m_checkTimer)
        XDdnsClient_checkNow(self);
    XClass_Parent(XObject, EXObject_TimerEvent,
                  void (*)(XObject*, XTimerEvent*))((XObject*)obj, event);
}

XVtable* XDdnsClient_class_init(void)
{
    XVTABLE_INIT_DEFAULT(XDdnsClient)
    XVTABLE_INHERIT_XCLASS(XObject);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Deinit, VXDdnsClient_deinit);
    XVTABLE_OVERLOAD_DEFAULT(EXObject_TimerEvent, VXDdnsClient_timerEvent);
    return XVTABLE_DEFAULT;
}

void XDdnsClient_init(XDdnsClient* obj)
{
    if (!obj)
        return;
    memset(obj, 0, sizeof(XDdnsClient));
    XObject_init((XObject*)obj);
    XClassSetVtable(obj, XDdnsClient);
    obj->m_d = (XDdnsClientPrivate*)XCalloc_System(
        1, sizeof(XDdnsClientPrivate));
    if (obj->m_d) {
        XDdnsClientPrivate* d = obj->m_d;
#if XDDNS_ALIYUN_ON
        d->m_provider = XDdnsProvider_aliyun(); /* 默认阿里云 */
#endif
        d->m_type = XDdnsRecordType_A;
        d->m_timeoutMs = XDDNS_HTTP_TIMEOUT_MS;
        d->m_checkIntervalMs = XDDNS_DEFAULT_CHECK_INTERVAL_MS;
        d->m_state = (uint32_t)XDdnsClientState_Idle;
    }
}

XDdnsClient* XDdnsClient_create(void)
{
    XMemoryType memory = XCLASS_DEFAULT_MEMORY_TYPE;
    XDdnsClient* obj = (XDdnsClient*)XMemory_malloc(sizeof(XDdnsClient), memory);
    if (!obj)
        return NULL;
    XDdnsClient_init(obj);
    Set_Class_Memory(obj, memory);
    Set_Class_IsHeap(obj, true);
    return obj;
}

/* ========================================================================== */
/*                        HTTP 同步 GET（XUpgrade 口径）                     */
/* ========================================================================== */

static bool xdns_sync_get(XDdnsClient* self, const char* urlUtf8,
                          char* bodyOut, size_t cap)
{
    XDdnsClientPrivate* d = self ? self->m_d : NULL;
    int statusCode = 0;

    if (!d || !urlUtf8 || !bodyOut || cap == 0)
        return false;
    bodyOut[0] = '\0';
    /* 2026-10-16 切换为裸 TCP HTTP GET：XNM 同步 GET 对真实互联网地址
     * 存在 settle 崩溃/超时问题（实测），裸 XTcpSocket 实测收发正常；
     * DDNS 请求均为无状态签名 GET，HTTP/1.0 + Connection: close 足够。 */
    d->m_lastHttpStatus = 0;
    if (XDdnsHttp_get(urlUtf8, d->m_timeoutMs, bodyOut, cap, &statusCode)) {
        d->m_lastHttpStatus = statusCode;
        return true;
    }
    if (statusCode > 0) {
        /* 云端应答到达但非 2xx（签名错误/权限不足等），如实记录状态码 */
        d->m_lastHttpStatus = statusCode;
        return false;
    }
    return false;
}

/* ========================================================================== */
/*                        探测与校验                                         */
/* ========================================================================== */

/**
 * @brief 从应答体中扫描第一个 IPv4 形态的 token（x.x.x.x，每段 0..255，
 *        前后不得紧邻字母/数字/点——避免把版本号 "1.2" 或时间戳误当 IP）。
 * @param  body 应答体（可为 JSON/多行文本等非纯 IP 格式）。
 * @param  outIp 输出缓冲区（调用方提供存储空间）。
 * @param  cap 输出缓冲区容量。
 * @return 找到合法 IPv4 返回 true。
 */
static bool xdns_scan_first_ipv4(const char* body, char* outIp, size_t cap)
{
    const char* p = body;
    if (!body)
        return false;
    while (*p) {
        if (*p >= '0' && *p <= '9') {
            unsigned o[4];
            int seg;
            const char* q = p;
            int matched = 1;
            for (seg = 0; seg < 4 && matched; ++seg) {
                unsigned v = 0;
                int digits = 0;
                if (seg)
                    ++q; /* 跳过 '.' */
                while (*q >= '0' && *q <= '9' && digits < 3) {
                    v = v * 10u + (unsigned)(*q - '0');
                    ++q;
                    ++digits;
                }
                o[seg] = v;
                matched = (digits > 0)
                          && (*q == '.' || *q == '\0' || *q < '0' || *q > '9')
                          && v <= 255u;
                /* 段后不得紧跟字母（排除 "1.2.3.4abc"/十六进制等） */
                if (matched && digits > 0
                    && ((*q >= 'a' && *q <= 'f') || (*q >= 'A' && *q <= 'F')))
                    matched = 0;
            }
            if (matched && o[0] != 0u) {
                size_t len = (size_t)(q - p);
                if (len < cap) {
                    memcpy(outIp, p, len);
                    outIp[len] = '\0';
                    return true;
                }
                return false;
            }
            p = q > p ? q : p + 1;
        } else {
            ++p;
        }
    }
    return false;
}

/**
 * @brief 从探测应答体提取 IP 文本（去首尾空白），并按记录类型校验合法性。
 * @return 合法返回 true 并回写 outIp（截断至 cap-1）。
 */
static bool xdns_extract_ip(const char* body, XDdnsRecordType type,
                            char* outIp, size_t cap)
{
    XHostAddress addr;
    const char* begin;
    const char* end;
    size_t len;
    size_t i;

    if (!body)
        return false;
    begin = body;
    while (*begin == ' ' || *begin == '\r' || *begin == '\n' || *begin == '\t')
        begin++;
    end = begin + XStrlen(begin);
    while (end > begin
           && (end[-1] == ' ' || end[-1] == '\r' || end[-1] == '\n'
               || end[-1] == '\t'))
        end--;
    len = (size_t)(end - begin);
    if (len == 0 || len >= cap)
        return false;
    memcpy(outIp, begin, len);
    outIp[len] = '\0';

    /* 形态校验：必须解析为 IP，且与记录类型同族（A↔IPv4，AAAA↔IPv6）。 */
    XHostAddress_init(&addr);
    XHostAddress_setAddress(&addr, outIp);
    i = (size_t)XHostAddress_protocol(&addr);
    if (i == (size_t)XHostAddress_UnknownNetworkLayerProtocol) {
        /* 非纯 IP 应答（JSON/多行文本等）：对 A 记录回退到「首个 IPv4
         * token 扫描」，兼容 ipip/qifu 等国内探测源的应答格式。 */
        if (type == XDdnsRecordType_A
            && xdns_scan_first_ipv4(begin, outIp, cap))
            return true;
        return false;
    }
    if (type == XDdnsRecordType_A
        && i != (size_t)XHostAddress_IPv4Protocol)
        return false;
    if (type == XDdnsRecordType_AAAA
        && i != (size_t)XHostAddress_IPv6Protocol)
        return false;
    return true;
}

/**
 * @brief 探测公网 IP：手动注入优先，否则按槽位顺序尝试探测列表。
 * @note  自定义列表为空时按记录类型回退到内置列表（config 宏
 *        XDDNS_PROBE_V4/V6_DEFAULT：国际 HTTPS 源 + 国内 HTTP 源混合，
 *        提升国内网络可达性）。
 */
static bool xdns_detect_ip(XDdnsClient* self, char* outIp, size_t cap)
{
    XDdnsClientPrivate* d = self->m_d;
    /* 内置兜底列表（与 config 宏保持一致）：国内可达源优先（ip.3322、
     * 百度 qifu JSON），国际 HTTPS 源兜底。明文 http 源排在 https 之前，
     * 因其链路最简、国内直连可达性最好；探测内容仅为公开 IP，无敏感信息。 */
    static const char* const kV4[] = {
        "http://ip.3322.net/",
        "http://qifu-api.baidubce.com/ip/local/geo/v1/dip",
        "https://api4.ipify.org/",
        "https://ipv4.icanhazip.com/"
    };
    static const char* const kV6[] = {
        "https://api6.ipify.org/",
        "https://ipv6.icanhazip.com/"
    };
    const char* const* builtin = (d->m_type == XDdnsRecordType_AAAA)
                                     ? kV6 : kV4;
    int builtinCount = (d->m_type == XDdnsRecordType_AAAA)
                           ? (int)(sizeof(kV6) / sizeof(kV6[0]))
                           : (int)(sizeof(kV4) / sizeof(kV4[0]));
    int i;

    if (d->m_manualIp) {
        const char* ip = XString_toUtf8(d->m_manualIp);
        if (ip && xdns_extract_ip(ip, d->m_type, outIp, cap))
            return true;
        xdns_fail(self, XDdnsError_BadIp);
        return false;
    }

    /* 先尝试用户自定义槽位，再回退内置列表 */
    for (i = 0; i < (int)XDDNS_MAX_PROBE_URLS + builtinCount; i++) {
        const char* url = NULL;
        char body[128];

        if (i < (int)XDDNS_MAX_PROBE_URLS) {
            if (d->m_probeUrls[i]) {
                const char* probeUtf8 = XString_toUtf8(d->m_probeUrls[i]);
                if (probeUtf8 && probeUtf8[0] != '\0')
                    url = probeUtf8;
            }
        } else {
            int bi = i - (int)XDDNS_MAX_PROBE_URLS;
            if (bi >= builtinCount)
                break;
            url = builtin[bi];
        }
        if (!url)
            continue;
        if (xdns_sync_get(self, url, body, sizeof(body))
            && xdns_extract_ip(body, d->m_type, outIp, cap))
            return true;
        /* 单个探测源失败：继续下一个 */
    }
    xdns_fail(self, XDdnsError_ProbeExhausted);
    return false;
}

/* ========================================================================== */
/*                        失败出口与信号                                     */
/* ========================================================================== */

/**
 * @brief 从云端错误应答体提取 Code（阿里云等厂商的机器可读错误码）。
 * @param  self 客户端实例。
 * @param  body 应答体（可能为空串：非 2xx 时仍会带回正文）。
 * @note   形如 {"Code":"SignatureDoesNotMatch","Message":"..."}；
 *         解析失败时保持空串，调用方回退到 HTTP 状态码提示。
 */
static void xdns_captureApiError(XDdnsClient* self, const char* body)
{
    XDdnsClientPrivate* d = self ? self->m_d : NULL;
    const char* val;
    size_t n;

    if (!d || !body || !body[0])
        return;
    d->m_lastApiCode[0] = '\0';
    if (!strstr(body, "\"Code\""))
        return;
    val = strchr(strstr(body, "\"Code\"") + 6, '"'); /* 值起始引号 */
    if (!val)
        return;
    ++val;
    n = 0;
    while (val[n] && val[n] != '"' && n + 1 < sizeof(d->m_lastApiCode)) {
        d->m_lastApiCode[n] = val[n];
        ++n;
    }
    d->m_lastApiCode[n] = '\0';
}

static void xdns_fail(XDdnsClient* self, XDdnsError error)
{
    XDdnsClientPrivate* d = self ? self->m_d : NULL;
    int errArg;
    if (!d)
        return;
    d->m_lastError = (uint32_t)error;
    d->m_state = (uint32_t)XDdnsClientState_Failed;
    errArg = (int)error;
    XEmitSignal(self, XDdnsClient_checkFailed_signal,
                XVarList_Create(XVar(int, errArg)),
                NULL, NULL, XEVENT_PRIORITY_NORMAL);
}

/* xdns_emit_ip 定义在信号小节（带 XString 参数删除器，见文件尾部）。 */

/* ========================================================================== */
/*                        主流程                                             */
/* ========================================================================== */

bool XDdnsClient_checkNow(XDdnsClient* self)
{
    XDdnsClientPrivate* d = self ? self->m_d : NULL;
    char ip[XDDNS_VALUE_MAX];
    char url[XDDNS_URL_MAX];
    char body[2048];
    XDdnsRequestContext ctx;
    XDdnsRecordInfo record;
    const char* keyId;
    const char* keySecret;
    const char* domain;
    const char* rr;
    const char* endpoint;

    if (!d)
        return false;
    if (d->m_state == (uint32_t)XDdnsClientState_Detecting
        || d->m_state == (uint32_t)XDdnsClientState_Updating)
        return false; /* 单飞：上一轮未结束 */

    keyId = d->m_keyId ? XString_toUtf8(d->m_keyId) : NULL;
    keySecret = d->m_keySecret ? XString_toUtf8(d->m_keySecret) : NULL;
    domain = d->m_domain ? XString_toUtf8(d->m_domain) : NULL;
    rr = d->m_rr ? XString_toUtf8(d->m_rr) : NULL;
    endpoint = d->m_endpoint ? XString_toUtf8(d->m_endpoint) : NULL;
    if (!d->m_provider || !keyId || !keySecret || !domain || !rr) {
        xdns_fail(self, XDdnsError_NoConfig);
        return false;
    }

    /* HTTP 传输由 XDdnsHttp（裸 XTcpSocket）承载，XNM 已不参与；
     * m_verifyPeer 留作 XDdnsHttp 扩展 XSslSocket 后的开关。 */

    d->m_state = (uint32_t)XDdnsClientState_Detecting;
    if (!xdns_detect_ip(self, ip, sizeof(ip)))
        return true; /* 失败路径已发信号 */

    if (!d->m_currentIp)
        d->m_currentIp = XString_create_utf8(ip);
    else
        XString_assign_utf8(d->m_currentIp, ip);
    xdns_emit_ip(self, (void*)(size_t)XDdnsClient_ipDetected_signal, ip);

    /* 短路：IP 未变化则不触碰云端 API（防限流）。 */
    if (d->m_pushedIp
        && XString_equals_utf8(d->m_pushedIp, ip, XChar_CaseSensitive)) {
        d->m_state = (uint32_t)XDdnsClientState_Synchronized;
        d->m_lastError = (uint32_t)XDdnsError_None;
        xdns_emit_ip(self, (void*)(size_t)XDdnsClient_recordUpToDate_signal, ip);
        return true;
    }

    d->m_state = (uint32_t)XDdnsClientState_Updating;
    memset(&ctx, 0, sizeof(ctx));
    ctx.m_accessKeyId = keyId;
    ctx.m_accessSecret = keySecret;
    ctx.m_domain = domain;
    ctx.m_rr = rr;
    ctx.m_type = d->m_type;
    ctx.m_endpoint = endpoint;
    memset(&record, 0, sizeof(record));

    /* 1) 查询现有记录 */
    if (!d->m_provider->buildQueryUrl(&ctx, url, sizeof(url))
        || !xdns_sync_get(self, url, body, sizeof(body))
        || !d->m_provider->parseQuery(body, XStrlen(body), rr, d->m_type,
                                      &record)) {
        /* 云端已应答但非 2xx 时，正文带 Code/Message（如
           SignatureDoesNotMatch / InvalidAccessKeyId / Forbidden.RAM），
           取出来供 GUI 直接显示，避免用户只看到一个无信息的 err=8。 */
        xdns_captureApiError(self, body);
        xdns_fail(self, d->m_lastHttpStatus > 0 ? XDdnsError_HttpStatus
                                                : XDdnsError_BadResponse);
        return true;
    }

    /* 2) 不存在 → 新增；存在且值不同 → 更新；相同 → 一致 */
    if (record.m_recordId[0] == '\0') {
        if (!d->m_provider->buildAddUrl(&ctx, ip, url, sizeof(url))
            || !xdns_sync_get(self, url, body, sizeof(body))
            || !d->m_provider->parseMutation(body, XStrlen(body))) {
            xdns_fail(self, XDdnsError_ApiError);
            return true;
        }
    } else if (strcmp(record.m_value, ip) != 0) {
        if (!d->m_provider->buildUpdateUrl(&ctx, record.m_recordId, ip,
                                           url, sizeof(url))
            || !xdns_sync_get(self, url, body, sizeof(body))
            || !d->m_provider->parseMutation(body, XStrlen(body))) {
            xdns_fail(self, XDdnsError_ApiError);
            return true;
        }
    } else {
        d->m_state = (uint32_t)XDdnsClientState_Synchronized;
        d->m_lastError = (uint32_t)XDdnsError_None;
        xdns_emit_ip(self, (void*)(size_t)XDdnsClient_recordUpToDate_signal, ip);
        return true;
    }

    /* 3) 推送成功：记录已推送 IP 并发更新信号 */
    if (!d->m_pushedIp)
        d->m_pushedIp = XString_create_utf8(ip);
    else
        XString_assign_utf8(d->m_pushedIp, ip);
    d->m_state = (uint32_t)XDdnsClientState_Synchronized;
    d->m_lastError = (uint32_t)XDdnsError_None;
    xdns_emit_ip(self, (void*)(size_t)XDdnsClient_recordUpdated_signal, ip);
    return true;
}

/* ========================================================================== */
/*                        配置                                               */
/* ========================================================================== */

bool XDdnsClient_setProvider(XDdnsClient* self, const char* name)
{
    const XDdnsProviderOps* ops = XDdnsProvider_find(name);
    if (!self || !self->m_d || !ops)
        return false;
    self->m_d->m_provider = ops;
    return true;
}

bool XDdnsClient_setEndpoint(XDdnsClient* self, const char* urlUtf8)
{
    XDdnsClientPrivate* d = self ? self->m_d : NULL;
    if (!d)
        return false;
    if (d->m_endpoint) {
        XClassDelete(d->m_endpoint);
        d->m_endpoint = NULL;
    }
    if (urlUtf8 && urlUtf8[0])
        d->m_endpoint = XString_create_utf8(urlUtf8);
    return true;
}

bool XDdnsClient_setCredentials(XDdnsClient* self, const XString* keyId,
                                const XString* keySecret)
{
    XDdnsClientPrivate* d = self ? self->m_d : NULL;
    if (!d || !keyId || !keySecret)
        return false;
    if (d->m_keyId) XClassDelete(d->m_keyId);
    if (d->m_keySecret) XClassDelete(d->m_keySecret);
    d->m_keyId = XString_create_copy(keyId);
    d->m_keySecret = XString_create_copy(keySecret);
    return d->m_keyId && d->m_keySecret;
}

bool XDdnsClient_setCredentials_2(XDdnsClient* self, const char* keyId,
                                  const char* keySecret)
{
    XString* id;
    XString* secret;
    bool ok;
    if (!self || !keyId || !keySecret)
        return false;
    id = XString_create_utf8(keyId);
    secret = XString_create_utf8(keySecret);
    ok = id && secret && XDdnsClient_setCredentials(self, id, secret);
    if (id) XClassDelete(id);
    if (secret) XClassDelete(secret);
    return ok;
}

bool XDdnsClient_setRecord(XDdnsClient* self, const XString* domain,
                           const XString* rr, XDdnsRecordType type)
{
    XDdnsClientPrivate* d = self ? self->m_d : NULL;
    if (!d || !domain || !rr)
        return false;
    if (d->m_domain) XClassDelete(d->m_domain);
    if (d->m_rr) XClassDelete(d->m_rr);
    d->m_domain = XString_create_copy(domain);
    d->m_rr = XString_create_copy(rr);
    d->m_type = type;
    return d->m_domain && d->m_rr;
}

bool XDdnsClient_setRecord_2(XDdnsClient* self, const char* domain,
                             const char* rr, XDdnsRecordType type)
{
    XString* dom;
    XString* rrStr;
    bool ok;
    if (!self || !domain || !rr)
        return false;
    dom = XString_create_utf8(domain);
    rrStr = XString_create_utf8(rr);
    ok = dom && rrStr && XDdnsClient_setRecord(self, dom, rrStr, type);
    if (dom) XClassDelete(dom);
    if (rrStr) XClassDelete(rrStr);
    return ok;
}

bool XDdnsClient_setProbeUrl_2(XDdnsClient* self, const char* urlUtf8,
                               uint32_t index)
{
    XDdnsClientPrivate* d = self ? self->m_d : NULL;
    if (!d || index >= XDDNS_MAX_PROBE_URLS)
        return false;
    if (d->m_probeUrls[index]) {
        XClassDelete(d->m_probeUrls[index]);
        d->m_probeUrls[index] = NULL;
    }
    if (urlUtf8 && urlUtf8[0])
        d->m_probeUrls[index] = XString_create_utf8(urlUtf8);
    return true;
}

bool XDdnsClient_setLocalIp_2(XDdnsClient* self, const char* ipUtf8)
{
    XDdnsClientPrivate* d = self ? self->m_d : NULL;
    XHostAddress check;
    if (!d)
        return false;
    if (d->m_manualIp) {
        XClassDelete(d->m_manualIp);
        d->m_manualIp = NULL;
    }
    if (!ipUtf8 || ipUtf8[0] == '\0')
        return true; /* 清除手动模式 */
    /* 预校验：非法 IP 拒绝注入（保持原状态） */
    XHostAddress_init(&check);
    XHostAddress_setAddress(&check, ipUtf8);
    if (XHostAddress_protocol(&check)
        == XHostAddress_UnknownNetworkLayerProtocol)
        return false;
    d->m_manualIp = XString_create_utf8(ipUtf8);
    return d->m_manualIp != NULL;
}

bool XDdnsClient_setCheckInterval(XDdnsClient* self, uint32_t intervalMs)
{
    XDdnsClientPrivate* d = self ? self->m_d : NULL;
    if (!d)
        return false;
    d->m_checkIntervalMs = intervalMs;
    if (d->m_checkTimer) {
        XObject_killTimer((XObject*)self, d->m_checkTimer);
        d->m_checkTimer = 0;
    }
    if (intervalMs) {
        d->m_checkTimer = XObject_startTimer_ms((XObject*)self, intervalMs,
                                                XTimerType_CoarseTimer);
    }
    return true;
}

bool XDdnsClient_setSslPeerVerify(XDdnsClient* self, bool on)
{
    XDdnsClientPrivate* d = self ? self->m_d : NULL;
    if (!d)
        return false;
    /* HTTP 传输已切至裸 XTcpSocket（明文），此开关留作 XDdnsHttp 扩展
     * XSslSocket 后的 TLS 校验入口；当前仅记录偏好。 */
    d->m_verifyPeer = on;
    return true;
}

bool XDdnsClient_setTimeout(XDdnsClient* self, uint32_t timeoutMs)
{
    XDdnsClientPrivate* d = self ? self->m_d : NULL;
    if (!d)
        return false;
    d->m_timeoutMs = timeoutMs ? timeoutMs : (uint32_t)XDDNS_HTTP_TIMEOUT_MS;
    if (d->m_http)
        XNetworkAccessManager_setTransferTimeout(d->m_http,
                                                 (int)d->m_timeoutMs);
    return true;
}

/* ========================================================================== */
/*                        查询                                               */
/* ========================================================================== */

XDdnsClientState XDdnsClient_state(const XDdnsClient* self)
{
    if (!self || !self->m_d)
        return XDdnsClientState_Idle;
    return (XDdnsClientState)self->m_d->m_state;
}

XDdnsError XDdnsClient_lastError(const XDdnsClient* self)
{
    if (!self || !self->m_d)
        return XDdnsError_None;
    return (XDdnsError)self->m_d->m_lastError;
}

int XDdnsClient_lastHttpStatus(const XDdnsClient* self)
{
    if (!self || !self->m_d)
        return 0;
    return (int)self->m_d->m_lastHttpStatus;
}

const char* XDdnsClient_lastApiCode_const(const XDdnsClient* self)
{
    if (!self || !self->m_d)
        return "";
    return self->m_d->m_lastApiCode;
}

const XString* XDdnsClient_currentIp_const(const XDdnsClient* self)
{
    if (!self || !self->m_d)
        return NULL;
    return self->m_d->m_currentIp;
}

const XString* XDdnsClient_pushedIp_const(const XDdnsClient* self)
{
    if (!self || !self->m_d)
        return NULL;
    return self->m_d->m_pushedIp;
}

/* ========================================================================== */
/*                        信号                                               */
/* ========================================================================== */

/* 删除器：释放深拷贝的 XString 参数（发射方不接管时由框架调用）。
   注意 XVarList_args_1 自带局部声明（const 修饰），不可先声明同名变量。 */
static void xdns_string_arg_delete(XVarList* list)
{
    XVarList_args_1(list, XString*, value);
    if (value)
        XClassDelete(value);
}

static void xdns_emit_ip(XDdnsClient* self, void* signal, const char* ipUtf8);

static void xdns_emit_ip(XDdnsClient* self, void* signal, const char* ipUtf8)
{
    XString* copy = XString_create_utf8(ipUtf8);
    XVarList* args;
    if (!copy)
        return;
    args = XVarList_Create(XVar(XString*, copy));
    if (!args) {
        XClassDelete(copy); /* 发射方不接管参数，就地释放 */
        return;
    }
    XObject_emitSignal((XObject*)self, (size_t)signal, args,
                       xdns_string_arg_delete, NULL, XEVENT_PRIORITY_NORMAL);
}

void* XDdnsClient_ipDetected_signal(XDdnsClient* self, const XString* ip)
{
    if (!self)
        return (void*)(size_t)XDdnsClient_ipDetected_signal;
    xdns_emit_ip(self, (void*)(size_t)XDdnsClient_ipDetected_signal,
                 ip ? XString_toUtf8(ip) : "");
    return (void*)(size_t)XDdnsClient_ipDetected_signal;
}

void* XDdnsClient_recordUpdated_signal(XDdnsClient* self, const XString* ip)
{
    if (!self)
        return (void*)(size_t)XDdnsClient_recordUpdated_signal;
    xdns_emit_ip(self, (void*)(size_t)XDdnsClient_recordUpdated_signal,
                 ip ? XString_toUtf8(ip) : "");
    return (void*)(size_t)XDdnsClient_recordUpdated_signal;
}

void* XDdnsClient_recordUpToDate_signal(XDdnsClient* self, const XString* ip)
{
    if (!self)
        return (void*)(size_t)XDdnsClient_recordUpToDate_signal;
    xdns_emit_ip(self, (void*)(size_t)XDdnsClient_recordUpToDate_signal,
                 ip ? XString_toUtf8(ip) : "");
    return (void*)(size_t)XDdnsClient_recordUpToDate_signal;
}

void* XDdnsClient_checkFailed_signal(XDdnsClient* self, int error)
{
    int errArg = error;
    if (!self)
        return (void*)(size_t)XDdnsClient_checkFailed_signal;
    XEmitSignal(self, XDdnsClient_checkFailed_signal,
                XVarList_Create(XVar(int, errArg)),
                NULL, NULL, XEVENT_PRIORITY_NORMAL);
}

#endif /* XDDNS_ON && XDDNS_CLIENT_ON */
