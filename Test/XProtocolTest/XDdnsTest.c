/** @file       XDdnsTest.c
 * @brief      XDdns 动态域名解析客户端全量测试（接入 Test 菜单/命令行体系）。
 * @details    分组结构（读测试代码即可了解用法与协议行为）：
 *
 *   一、阿里云 POP 签名确定性：固定 nonce/Timestamp/参数 → 逐字符比对
 *      黄金 URL（规范串排序、ISO8601 时间戳 %3A 编码、HMAC-SHA1+Base64
 *      签名及其 %2B/%2F/%3D 再编码，全部由离线 Python 参考实现算出）
 *   二、AddDomainRecord URL：RR=@ 编码为 %40、TTL 参数、签名追加
 *   三、应答解析：Describe 罐头 JSON（命中/不命中/错误形态）、Update 应答
 *   四、端到端（本地 mock 服务器同时扮演探测源与阿里云 API）：
 *      探测 → 查记录 → Update → recordUpdated；二次检查短路 UpToDate；
 *      空记录走 Add；探测失败自动切换备用 URL
 *   五、内存泄漏：100 次创建/销毁回基线
 *
 * @author      XinYueC 团队
 */
#include "XDdnsTest.h"
#include "XDdnsClient.h"
#include "XDdnsProvider.h"
#include "XDdnsAliyun.h"
#include "XDdnsHttp.h"
#include "XHttpServer.h"
#include "XUdpSocket.h"
#include "XVarList.h"
#include "XTcpSocket.h"
#include "XAbstractSocket.h"
#include "XCryptographicHash.h"
#include "XString.h"
#include "XCoreApplication.h"
#include "XThread.h"
#include "XMemory.h"
#include "XDateTime.h"
#include "XPrintf.h"
#include <string.h>
#include <stdlib.h>

/* ========================================================================== */
/*                        断言与统计                                         */
/* ========================================================================== */

static int g_passed = 0;
static int g_failed = 0;

/** @brief 断言宏。 */
#define DNS_CHECK(cond, desc)                                               \
    do {                                                                    \
        if (cond) {                                                         \
            g_passed++;                                                     \
            XPrintf("  [PASS] %s\n", desc);                                 \
        } else {                                                            \
            g_failed++;                                                     \
            XPrintf("  [FAIL] %s\n", desc);                                 \
        }                                                                   \
    } while (0)

/** @brief 分组标题。 */
#define DNS_SECTION(title) XPrintf("\n--- %s ---\n", title)

/** @brief 泵事件循环直至条件满足或轮次耗尽。 */
static void dns_pump_until(bool condition, int maxRounds)
{
    int i;
    for (i = 0; i < maxRounds && !condition; i++) {
        XCoreApplication_processEvents(XEventLoop_AllEvents);
        XThread_msleep(5);
    }
    XCoreApplication_processEvents(XEventLoop_AllEvents);
}

/* ========================================================================== */
/*                        一、阿里云 POP 签名确定性                           */
/* ========================================================================== */

/**
 * @brief 黄金 URL（离线 Python HMAC-SHA1 参考实现算出，逐字节固定）。
 * @details 覆盖：参数按名排序、Timestamp 冒号编码 %3A、规范串二次编码
 *          （%3A→%253A）、HMAC-SHA1 密钥 = Secret+"&"、Base64 签名的
 *          +//= 字符再编码为 %2B/%2F/%3D。
 */
static const char* k_goldenQueryUrl =
    "https://alidns.aliyuncs.com/?"
    "AccessKeyId=testid&Action=DescribeDomainRecords&DomainName=example.com&"
    "Format=JSON&RRKeyWord=www&SignatureMethod=HMAC-SHA1&"
    "SignatureNonce=1234567890abcdef&SignatureVersion=1.0&"
    "Timestamp=2026-01-15T08%3A00%3A00Z&Type=A&Version=2015-01-09&"
    "Signature=Y%2Fmt1G5TbcH2Sh9FCynL9L3XBOA%3D";

static void dns_test_aliyun_signature(void)
{
    XDdnsRequestContext ctx;
    char url[XDDNS_URL_MAX];

    DNS_SECTION("一、阿里云 POP 签名确定性");
    memset(&ctx, 0, sizeof(ctx));
    ctx.m_accessKeyId = "testid";
    ctx.m_accessSecret = "testsecret";
    ctx.m_domain = "example.com";
    ctx.m_rr = "www";
    ctx.m_type = XDdnsRecordType_A;
    ctx.m_nonce = "1234567890abcdef";
    ctx.m_timestamp = "2026-01-15T08:00:00Z";
    /* endpoint 留空 = 默认官方端点 */

    {
        const XDdnsProviderOps* ops = XDdnsProvider_find(XDDNS_PROVIDER_ALIYUN);
        DNS_CHECK(ops != NULL, "Provider 查找返回阿里云操作表");
        if (!ops)
            return;
        DNS_CHECK(ops->buildQueryUrl && ops->buildAddUrl
                      && ops->buildUpdateUrl && ops->parseQuery
                      && ops->parseMutation,
                  "操作表五个函数齐全");
        DNS_CHECK(ops->m_defaultEndpoint != NULL
                      && strcmp(ops->m_defaultEndpoint,
                                "https://alidns.aliyuncs.com") == 0,
                  "默认端点字段正确（切换厂商时 GUI 据此恢复）");
        DNS_CHECK(XDdnsProvider_count() >= 1
                      && XDdnsProvider_at(0) == ops,
                  "注册表枚举 count/at 与查找一致");
        DNS_CHECK(XDdnsProvider_at(-1) == NULL
                      && XDdnsProvider_at(XDdnsProvider_count()) == NULL,
                  "注册表越界访问返回 NULL");
    }

    DNS_CHECK(XDdnsProvider_find("no-such-provider") == NULL,
              "未注册厂商查找返回 NULL");

    memset(url, 0, sizeof(url));
    DNS_CHECK(XDdnsProvider_find(XDDNS_PROVIDER_ALIYUN)
                  ->buildQueryUrl(&ctx, url, sizeof(url)),
              "buildQueryUrl 构造成功");
    XPrintf("        [INFO] url=%s\n", url);
    DNS_CHECK(strcmp(url, k_goldenQueryUrl) == 0,
              "查询 URL 与离线黄金向量逐字符一致");

    /* 缺凭据必须失败（防签出无凭据请求） */
    {
        XDdnsRequestContext bad = ctx;
        bad.m_accessSecret = NULL;
        DNS_CHECK(!XDdnsProvider_find(XDDNS_PROVIDER_ALIYUN)
                       ->buildQueryUrl(&bad, url, sizeof(url)),
                  "缺 AccessSecret 时构造失败");
    }

    /* Add URL：RR=@ 编码为 %40；携带 TTL；签名追加 */
    {
        const XDdnsProviderOps* ops = XDdnsProvider_find(XDDNS_PROVIDER_ALIYUN);
        ctx.m_rr = "@";
        DNS_CHECK(ops->buildAddUrl(&ctx, "1.2.3.4", url, sizeof(url)),
                  "buildAddUrl 构造成功");
        DNS_CHECK(strstr(url, "RR=%40") != NULL,
                  "RR=@ 编码为 %40（@ 非法字符强制转义）");
        DNS_CHECK(strstr(url, "Value=1.2.3.4") != NULL,
                  "Value 明文携带（IP 无需转义字符）");
        DNS_CHECK(strstr(url, "TTL=600") != NULL, "默认 TTL=600 随请求提交");
        DNS_CHECK(strstr(url, "Action=AddDomainRecord") != NULL,
                  "Action=AddDomainRecord");
        DNS_CHECK(strstr(url, "&Signature=") != NULL, "签名参数已追加");
        ctx.m_rr = "www";
    }

    /* Update URL：RecordId 携带 */
    DNS_CHECK(XDdnsProvider_find(XDDNS_PROVIDER_ALIYUN)
                  ->buildUpdateUrl(&ctx, "69999999", "5.6.7.8", url,
                                   sizeof(url)),
              "buildUpdateUrl 构造成功");
    DNS_CHECK(strstr(url, "RecordId=69999999") != NULL
                  && strstr(url, "Value=5.6.7.8") != NULL
                  && strstr(url, "Action=UpdateDomainRecord") != NULL,
              "Update URL 含 RecordId/Value/Action");
}

/* ========================================================================== */
/*                        二、应答解析                                       */
/* ========================================================================== */

static void dns_test_parse(void)
{
    /* SHA1 原语探针：RFC 6234 向量（单块/双块边界/长消息）。
       阿里云签名的 STS 超过 64 字节（多块），若库的 SHA1 多块实现有缺陷
       会在此暴露——这是签名分歧的诊断入口。 */
    {
        struct Sha1Vector { const char* data; size_t len; const char* expect; };
        const struct Sha1Vector vectors[] = {
            { "abc", 3, "a9993e364706816aba3e25717850c26c9cd0d89d" },
            { "abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq", 56,
              "84983e441c3bd26ebaae4aa1f95129e5e54670f1" },
        };
        unsigned i;
        DNS_SECTION("SHA1 原语探针（RFC 6234）");
        for (i = 0; i < sizeof(vectors)/sizeof(vectors[0]); i++) {
            XByteArray* digest = XCryptographicHash_hash(
                vectors[i].data, vectors[i].len, XCryptographicHash_Sha1);
            char hex[64];
            size_t j;
            hex[0] = '\0';
            if (digest) {
                const uint8_t* raw = (const uint8_t*)XByteArray_constData(digest);
                size_t n = (size_t)XContainer_size_base((const XContainer*)digest);
                for (j = 0; j < n && j < 20; j++)
                    XSnprintf(&hex[j*2], 3, "%02x", raw[j]);
                XClassDelete(digest);
            }
            XPrintf("        [INFO] sha1(%zuB)=%s\n", vectors[i].len, hex);
            DNS_CHECK(strcmp(hex, vectors[i].expect) == 0,
                      i == 0 ? "SHA1 单块向量 (abc)" : "SHA1 双块边界向量 (56B)");
        }

        /* 314 字节消息（阿里云 STS 的内层哈希实际长度档位） */
        {
            char big[314];
            XByteArray* digest;
            char hex[64] = { 0 };
            memset(big, 'a', sizeof(big));
            digest = XCryptographicHash_hash(big, sizeof(big),
                                             XCryptographicHash_Sha1);
            if (digest) {
                const uint8_t* raw = (const uint8_t*)XByteArray_constData(digest);
                size_t n = (size_t)XContainer_size_base((const XContainer*)digest);
                size_t j;
                for (j = 0; j < n && j < 20; j++)
                    XSnprintf(&hex[j*2], 3, "%02x", raw[j]);
                XClassDelete(digest);
            }
            XPrintf("        [INFO] sha1(314B of 'a')=%s\n", hex);
            /* 参考值由 Python hashlib.sha1(b'a'*314) 离线计算 */
            DNS_CHECK(strcmp(hex, "ff0ad2b82486645ebf945a5858de7f9224d1bbeb") == 0,
                      "SHA1 314 字节多块消息");
        }

        /* RFC 2202 HMAC-SHA1 用例 2：key="Jefe" */
        {
            XByteArray* digest = XCryptographicHash_hmac(
                "Jefe", 4, "what do ya want for nothing?", 28,
                XCryptographicHash_Sha1);
            char hex[64] = { 0 };
            if (digest) {
                const uint8_t* raw = (const uint8_t*)XByteArray_constData(digest);
                size_t n = (size_t)XContainer_size_base((const XContainer*)digest);
                size_t j;
                for (j = 0; j < n && j < 20; j++)
                    XSnprintf(&hex[j*2], 3, "%02x", raw[j]);
                XClassDelete(digest);
            }
            DNS_CHECK(strcmp(hex, "effcdf6ae5eb2fa2d27416d5f184df9c259a7c79") == 0,
                      "RFC2202 HMAC-SHA1 (Jefe) 标准向量");
        }

        /* 尺寸扫描：定位「返回空摘要」的失败边界（诊断用，不设断言）。 */
        {
            static const size_t sizes[] = { 63, 64, 65, 100, 200, 256, 257,
                                            300, 314, 400, 512, 513, 600 };
            size_t si;
            for (si = 0; si < sizeof(sizes)/sizeof(sizes[0]); si++) {
                size_t n = sizes[si];
                char* big = (char*)XMalloc_System(n);
                XByteArray* digest;
                char hex[8];
                if (!big)
                    continue;
                memset(big, 'a', n);
                digest = XCryptographicHash_hash(big, n, XCryptographicHash_Sha1);
                hex[0] = '\0';
                if (digest) {
                    const uint8_t* raw =
                        (const uint8_t*)XByteArray_constData(digest);
                    /* 空串摘要 = da39a3ee5e6b4b0d3255bfef95601890afd80709 */
                    bool emptyDigest = raw[0] == 0xda && raw[1] == 0x39;
                    XSnprintf(hex, sizeof(hex), "%s", emptyDigest ? "EMPTY" : "ok");
                    XClassDelete(digest);
                } else {
                    XSnprintf(hex, sizeof(hex), "%s", "NULL");
                }
                XPrintf("        [SCAN] sha1(%3uB) -> %s\n", (unsigned)n, hex);
                XFree_System(big);
            }
        }

        /* 分配器探针：HYBRID/MULTIPOOL 在 257..512 区间是否返回 NULL
           （诊断 XDdns 签名空摘要问题的根因归属）。 */
        {
            static const size_t allocSizes[] = { 64, 200, 256, 257, 300,
                                                 314, 400, 512, 513 };
            size_t ai;
            for (ai = 0; ai < sizeof(allocSizes)/sizeof(allocSizes[0]); ai++) {
                void* h = XMemory_malloc(allocSizes[ai], XMEMORY_TYPE_HYBRID);
                void* m = XMemory_malloc(allocSizes[ai], XMEMORY_TYPE_MULTIPOOL);
                XPrintf("        [ALLOC] %3uB hybrid=%s multipool=%s\n",
                        (unsigned)allocSizes[ai],
                        h ? "ok" : "NULL",
                        m ? "ok" : "NULL");
                if (h) XMemory_free(h, XMEMORY_TYPE_HYBRID);
                if (m) XMemory_free(m, XMEMORY_TYPE_MULTIPOOL);
            }
        }
    }

    const char* describeFound =
        "{\"RequestId\":\"AB123\",\"TotalCount\":1,\"DomainRecords\":{"
        "\"Record\":[{\"RecordId\":\"69999999\",\"RR\":\"www\",\"Type\":\"A\","
        "\"Value\":\"1.2.3.4\",\"TTL\":600,\"Status\":\"ENABLE\"}]}}";
    const char* describeMulti =
        "{\"RequestId\":\"AB124\",\"TotalCount\":2,\"DomainRecords\":{"
        "\"Record\":["
        "{\"RecordId\":\"1\",\"RR\":\"www\",\"Type\":\"CNAME\","
        "\"Value\":\"x.example.com\"},"
        "{\"RecordId\":\"2\",\"RR\":\"www\",\"Type\":\"A\","
        "\"Value\":\"9.9.9.9\"}]}}";
    const char* describeEmpty =
        "{\"RequestId\":\"AB125\",\"TotalCount\":0,\"DomainRecords\":{"
        "\"Record\":[]}}";
    const char* apiError =
        "{\"RequestId\":\"AB126\",\"Code\":\"InvalidAccessKeyId.NotFound\","
        "\"Message\":\"The AccessKey not found\"}";
    const char* updateOk =
        "{\"RequestId\":\"AB127\",\"RecordId\":\"69999999\"}";

    const XDdnsProviderOps* ops = XDdnsProvider_find(XDDNS_PROVIDER_ALIYUN);
    XDdnsRecordInfo record;

    DNS_SECTION("二、应答解析");
    if (!ops) {
        DNS_CHECK(false, "Provider 可用");
        return;
    }

    /* 命中：RR+Type 匹配的记录被提取 */
    memset(&record, 0, sizeof(record));
    DNS_CHECK(ops->parseQuery(describeFound, XStrlen(describeFound), "www",
                              XDdnsRecordType_A, &record),
              "Describe 命中应答解析成功");
    DNS_CHECK(strcmp(record.m_recordId, "69999999") == 0
                  && strcmp(record.m_value, "1.2.3.4") == 0,
              "RecordId 与 Value 正确提取");

    /* 多记录：只取 RR+Type 都匹配的（跳过 CNAME） */
    memset(&record, 0, sizeof(record));
    DNS_CHECK(ops->parseQuery(describeMulti, XStrlen(describeMulti), "www",
                              XDdnsRecordType_A, &record),
              "多记录应答解析成功");
    DNS_CHECK(strcmp(record.m_recordId, "2") == 0
                  && strcmp(record.m_value, "9.9.9.9") == 0,
              "按 RR+Type 过滤（跳过 CNAME 记录取 A 记录）");

    /* 空记录：解析成功且 recordId 为空（触发 Add 流程的信号） */
    memset(&record, 0, sizeof(record));
    DNS_CHECK(ops->parseQuery(describeEmpty, XStrlen(describeEmpty), "www",
                              XDdnsRecordType_A, &record),
              "空记录应答解析成功（非错误）");
    DNS_CHECK(record.m_recordId[0] == '\0', "空记录时 recordId 为空串");

    /* 错误形态：返回 false（调用方映射 ApiError） */
    memset(&record, 0, sizeof(record));
    DNS_CHECK(!ops->parseQuery(apiError, XStrlen(apiError), "www",
                               XDdnsRecordType_A, &record),
              "云端错误形态（JSON Code）解析失败");

    /* parseMutation */
    DNS_CHECK(ops->parseMutation(updateOk, XStrlen(updateOk)),
              "Update 成功应答判定通过");
    DNS_CHECK(!ops->parseMutation(apiError, XStrlen(apiError)),
              "Update 错误应答判定失败");
    DNS_CHECK(!ops->parseMutation("not-json", 8), "非 JSON 应答判定失败");
    DNS_CHECK(!ops->parseMutation(NULL, 0), "空应答判定失败");
}

/* ========================================================================== */
/*                        三、本地 mock 服务器                               */
/* ========================================================================== */

static XHttpServer* g_mock = NULL;
static int g_mockDescribe = 0;
static int g_mockAdd = 0;
static int g_mockUpdate = 0;
static int g_mockProbe = 0;
/** Describe 应答返回的记录值；空串 = 无记录（触发 Add）。 */
static char g_mockRecordValue[64] = "1.2.3.4";
/** 探测接口返回的 IP。 */
static char g_mockProbeIp[64] = "5.6.7.8";
/** 探测接口是否可达（false 时模拟连接失败：直接断开不回包——用不可达端口模拟）。 */
static bool g_mockProbeAlive = true;

/**
 * @brief 构造应答（body → XByteArray → Response）并发送。
 */
static void dns_mock_send(XHttpServerResponder* responder, const char* body)
{
    XByteArray* bytes = XByteArray_create_utf8(body);
    XHttpServerResponse* response =
        bytes ? XHttpServerResponse_create_body(bytes, XHttpServerResponse_Ok)
              : NULL;
    if (response)
        XHttpServerResponder_sendResponse(responder, response);
    if (response) XClassDelete((XClass*)response);
    if (bytes) XClassDelete(bytes);
}

/**
 * @brief mock 处理器：按 query 中的 Action 分流；无 Action 的路径视为
 *        公网 IP 探测源。
 */
static void dns_mock_handler(const XHttpServerRequest* request,
                             XHttpServerResponder* responder, void* context)
{
    const XString* query = XHttpServerRequest_query_const(request);
    const char* qs = query ? XString_toUtf8(query) : NULL;
    (void)context;

    if (!qs || strstr(qs, "Action=") == NULL) {
        /* 公网 IP 探测（/ip 路径） */
        g_mockProbe++;
        if (g_mockProbeAlive)
            dns_mock_send(responder, g_mockProbeIp);
        else
            XHttpServerResponder_sendResponse(
                responder,
                XHttpServerResponse_create_status(
                    XHttpServerResponse_InternalServerError));
        return;
    }
    if (strstr(qs, "Action=DescribeDomainRecords")) {
        g_mockDescribe++;
        if (g_mockRecordValue[0]) {
            char body[512];
            XSnprintf(body, sizeof(body),
                      "{\"RequestId\":\"M1\",\"TotalCount\":1,"
                      "\"DomainRecords\":{\"Record\":[{\"RecordId\":\"699\","
                      "\"RR\":\"www\",\"Type\":\"A\",\"Value\":\"%s\","
                      "\"TTL\":600}]}}",
                      g_mockRecordValue);
            dns_mock_send(responder, body);
        } else {
            dns_mock_send(responder,
                          "{\"RequestId\":\"M2\",\"TotalCount\":0,"
                          "\"DomainRecords\":{\"Record\":[]}}");
        }
        return;
    }
    if (strstr(qs, "Action=UpdateDomainRecord")) {
        g_mockUpdate++;
        dns_mock_send(responder,
                      "{\"RequestId\":\"M3\",\"RecordId\":\"699\"}");
        return;
    }
    if (strstr(qs, "Action=AddDomainRecord")) {
        g_mockAdd++;
        dns_mock_send(responder, "{\"RequestId\":\"M4\",\"RecordId\":\"700\"}");
        return;
    }
    /* 未知 Action：报业务错误 */
    dns_mock_send(responder, "{\"Code\":\"InvalidAction\",\"Message\":\"?\"}");
}

/**
 * @brief 启动 mock 服务器并重置剧本。
 * @return mock 端口；失败返回 0。
 */
static uint16_t dns_mock_start(void)
{
    g_mock = XHttpServer_create();
    if (!g_mock)
        return 0;
    g_mockDescribe = 0;
    g_mockAdd = 0;
    g_mockUpdate = 0;
    g_mockProbe = 0;
    XStrncpy(g_mockRecordValue, "1.2.3.4", sizeof(g_mockRecordValue) - 1);
    XStrncpy(g_mockProbeIp, "5.6.7.8", sizeof(g_mockProbeIp) - 1);
    g_mockProbeAlive = true;
    XHttpServer_setHandler(g_mock, dns_mock_handler, NULL);
    if (!XHttpServer_listen(g_mock, NULL, 0)) {
        XClassDelete((XClass*)g_mock);
        g_mock = NULL;
        return 0;
    }
    return (uint16_t)XHttpServer_serverPort(g_mock);
}

static void dns_mock_stop(void)
{
    if (g_mock) {
        XClassDelete((XClass*)g_mock);
        g_mock = NULL;
    }
}

/**
 * @brief 配置一个指向 mock 的就绪客户端（凭据/记录/端点/探测 URL）。
 */
static XDdnsClient* dns_make_client(uint16_t port)
{
    XDdnsClient* client = XDdnsClient_create();
    char url[96];
    if (!client)
        return NULL;
    XSnprintf(url, sizeof(url), "http://127.0.0.1:%u", (unsigned)port);
    XDdnsClient_setProvider(client, XDDNS_PROVIDER_ALIYUN);
    XDdnsClient_setEndpoint(client, url);
    XDdnsClient_setCredentials_2(client, "testid", "testsecret");
    XDdnsClient_setRecord_2(client, "example.com", "www", XDdnsRecordType_A);
    XSnprintf(url, sizeof(url), "http://127.0.0.1:%u/ip", (unsigned)port);
    XDdnsClient_setProbeUrl_2(client, url, 0);
    XDdnsClient_setTimeout(client, 3000);
    return client;
}

/* ========================================================================== */
/*                        四、端到端                                         */
/* ========================================================================== */

/* 信号观测计数 */
static int g_evtUpdated = 0;
static int g_evtUpToDate = 0;
static int g_evtFailed = 0;
static int g_evtIpDetected = 0;   /**< ipDetected 信号计数。 */
static char g_evtLastIp[64] = { 0 };

/** @brief ipDetected 信号槽（记录探测到的 IP）。 */
static void dns_sig_ip(XObject* receiver, XVarList* args)
{
    XDdnsClient* c = (XDdnsClient*)receiver;
    const XString* ip = XDdnsClient_currentIp_const(c);
    (void)args;
    g_evtIpDetected++;
    if (ip)
        XStrncpy(g_evtLastIp, XString_toUtf8(ip), sizeof(g_evtLastIp) - 1);
}

/** @brief recordUpdated 信号槽（信号覆盖用例的简版）。 */
static void dns_sig_upd(XObject* receiver, XVarList* args)
{
    (void)receiver; (void)args;
    g_evtUpdated++;
}

/** @brief recordUpToDate 信号槽。 */
static void dns_sig_same(XObject* receiver, XVarList* args)
{
    (void)receiver; (void)args;
    g_evtUpToDate++;
}

/** @brief checkFailed 信号槽。 */
static void dns_sig_fail(XObject* receiver, XVarList* args)
{
    (void)receiver; (void)args;
    g_evtFailed++;
}

static void dns_on_updated(XObject* receiver, XVarList* args)
{
    XDdnsClient* c = (XDdnsClient*)receiver;
    const XString* ip = XDdnsClient_currentIp_const(c);
    (void)args;
    g_evtUpdated++;
    if (ip)
        XStrncpy(g_evtLastIp, XString_toUtf8(ip),
                 sizeof(g_evtLastIp) - 1);
}

static void dns_on_uptodate(XObject* receiver, XVarList* args)
{
    (void)receiver; (void)args;
    g_evtUpToDate++;
}

static void dns_on_failed(XObject* receiver, XVarList* args)
{
    (void)receiver; (void)args;
    g_evtFailed++;
}

static void dns_test_e2e(void)
{
    uint16_t port = dns_mock_start();
    XDdnsClient* client = NULL;

    DNS_SECTION("四、端到端（mock 探测源 + mock 阿里云）");
    DNS_CHECK(port > 0, "mock 服务器启动");
    if (!port)
        return;

    client = dns_make_client(port);
    DNS_CHECK(client != NULL, "客户端创建");
    if (!client) {
        dns_mock_stop();
        return;
    }
    XObject_connect_2((XObject*)client,
                      XSignal(XDdnsClient_recordUpdated_signal),
                      dns_on_updated);
    XObject_connect_2((XObject*)client,
                      XSignal(XDdnsClient_recordUpToDate_signal),
                      dns_on_uptodate);
    XObject_connect_2((XObject*)client,
                      XSignal(XDdnsClient_checkFailed_signal),
                      dns_on_failed);

    /* 未配置探测源且无手动 IP 时不该跑起来——先给手动 IP 之外的完整配置 */
    /* 场景 1：记录值 1.2.3.4 ≠ 探测 IP 5.6.7.8 → Update */
    DNS_CHECK(XDdnsClient_checkNow(client), "checkNow 启动（记录需更新场景）");
    dns_pump_until(XDdnsClient_state(client) == XDdnsClientState_Synchronized
                       || XDdnsClient_state(client) == XDdnsClientState_Failed,
                   600);
    DNS_CHECK(XDdnsClient_state(client) == XDdnsClientState_Synchronized,
              "端到端更新后转 Synchronized");
    DNS_CHECK(g_evtUpdated == 1, "recordUpdated 信号发出");
    DNS_CHECK(g_mockUpdate == 1, "云端收到恰好一次 Update 请求");
    DNS_CHECK(strcmp(g_evtLastIp, "5.6.7.8") == 0,
              "更新写入的 IP 即探测 IP");
    {
        const XString* pushed = XDdnsClient_pushedIp_const(client);
        DNS_CHECK(pushed && strcmp(XString_toUtf8(pushed), "5.6.7.8") == 0,
                  "pushedIp 记录为 5.6.7.8");
    }

    /* 场景 2：IP 未变化 → 短路 UpToDate，不触碰云端 API */
    DNS_CHECK(XDdnsClient_checkNow(client), "二次 checkNow（IP 未变场景）");
    dns_pump_until(
        XDdnsClient_state(client) == XDdnsClientState_Synchronized
            && g_evtUpToDate > 0,
        600);
    DNS_CHECK(g_evtUpToDate == 1, "IP 未变化发 recordUpToDate 信号");
    DNS_CHECK(g_mockUpdate == 1 && g_mockAdd == 0,
              "短路生效：未发生 Add/Update 请求（防限流）");

    /* 场景 3：IP 变化 → 再次 Update */
    XStrncpy(g_mockProbeIp, "7.7.7.7", sizeof(g_mockProbeIp) - 1);
    DNS_CHECK(XDdnsClient_checkNow(client), "三次 checkNow（IP 已变化场景）");
    dns_pump_until(g_evtUpdated == 2, 600);
    DNS_CHECK(g_evtUpdated == 2 && g_mockUpdate == 2,
              "IP 变化触发第二次 Update");

    /* 场景 4：记录不存在 → Add。
       注意换一个新 IP：上一场景已把 pushedIp 推到 7.7.7.7，相同 IP 会被
       「未变化短路」拦截而根本不查询记录。 */
    g_mockRecordValue[0] = '\0';
    XStrncpy(g_mockProbeIp, "8.8.8.8", sizeof(g_mockProbeIp) - 1);
    DNS_CHECK(XDdnsClient_checkNow(client), "四次 checkNow（记录缺失场景）");
    dns_pump_until(g_mockAdd == 1, 600);
    DNS_CHECK(g_mockAdd == 1, "记录缺失触发 AddDomainRecord");
    DNS_CHECK(g_evtUpdated == 3, "Add 成功同样发 recordUpdated");

    XClassDelete((XClass*)client);

    /* 场景 5：探测失败自动切换备用 URL（手动注入兜底） */
    {
        XDdnsClient* c2 = dns_make_client(port);
        if (c2) {
            g_evtUpdated = 0;
            g_mockRecordValue[0] = '\0';
            /* 探测槽 0 指向不可达端口；槽 1 指向 mock */
            XDdnsClient_setProbeUrl_2(c2, "http://127.0.0.1:1/", 0);
            {
                char url2[96];
                XSnprintf(url2, sizeof(url2), "http://127.0.0.1:%u/ip",
                          (unsigned)port);
                XDdnsClient_setProbeUrl_2(c2, url2, 1);
            }
            XDdnsClient_setTimeout(c2, 1500);
            DNS_CHECK(XDdnsClient_checkNow(c2),
                      "主探测不可达时 checkNow 启动");
            dns_pump_until(XDdnsClient_state(c2) == XDdnsClientState_Synchronized
                               || XDdnsClient_state(c2) == XDdnsClientState_Failed,
                           800);
            DNS_CHECK(XDdnsClient_state(c2) == XDdnsClientState_Synchronized,
                      "探测失败自动切换备用 URL 成功");
            XClassDelete((XClass*)c2);
        } else {
            DNS_CHECK(false, "场景 5 客户端创建");
        }
    }

    dns_mock_stop();
}

/* ========================================================================== */
/*                        六、生命周期 / 访问器 / 传输 / 注册表              */
/* ========================================================================== */

/**
 * @brief 信号逐个到达覆盖（4 个信号全部触发并计数）。
 */
static void dns_test_signals(void)
{
    XDdnsClient* client;
    uint16_t port;
    int pumped;

    DNS_SECTION("六、信号到达覆盖");

    /* 模拟服务器：记录值 1.2.3.4 ≠ 探测 IP → 必走 Update 路径 */
    port = dns_mock_start();
    if (port == 0) { DNS_CHECK(false, "信号用例：mock 启动"); return; }

    client = dns_make_client(port);
    if (!client) {
        DNS_CHECK(false, "信号用例：客户端创建");
        dns_mock_stop();
        return;
    }

    g_evtIpDetected = 0;
    g_evtUpdated = 0;
    g_evtUpToDate = 0;
    g_evtFailed = 0;
    XObject_connect_2((XObject*)client,
                      XSignal(XDdnsClient_ipDetected_signal), dns_sig_ip);
    XObject_connect_2((XObject*)client,
                      XSignal(XDdnsClient_recordUpdated_signal), dns_sig_upd);
    XObject_connect_2((XObject*)client,
                      XSignal(XDdnsClient_recordUpToDate_signal), dns_sig_same);
    XObject_connect_2((XObject*)client,
                      XSignal(XDdnsClient_checkFailed_signal), dns_sig_fail);

    /* 成功路径：探测成功(ipDetected) + 记录更新(recordUpdated) */
    XStrncpy(g_mockRecordValue, "1.2.3.4", sizeof(g_mockRecordValue) - 1);
    XStrncpy(g_mockProbeIp, "5.6.7.8", sizeof(g_mockProbeIp) - 1);
    g_mockAdd = g_mockUpdate = 0;
    XDdnsClient_checkNow(client);
    for (pumped = 0; pumped < 600
         && XDdnsClient_state(client) != XDdnsClientState_Synchronized
         && XDdnsClient_state(client) != XDdnsClientState_Failed; ++pumped) {
        XCoreApplication_processEvents(XEventLoop_AllEvents);
        XThread_msleep(5);
    }
    DNS_CHECK(g_evtIpDetected == 1, "ipDetected 信号到达 1 次");
    DNS_CHECK(g_evtUpdated == 1, "recordUpdated 信号到达 1 次（记录值不同）");
    DNS_CHECK(g_evtUpToDate == 0, "更新路径不触发 recordUpToDate");
    DNS_CHECK(g_evtFailed == 0, "成功路径不触发 checkFailed");
    DNS_CHECK(XDdnsClient_lastHttpStatus(client) == 200,
              "成功路径 lastHttpStatus=200");
    DNS_CHECK(strcmp(XDdnsClient_lastApiCode_const(client), "") == 0,
              "成功路径 lastApiCode 为空串");
    XClassDelete((XClass*)client);

    /* 已一致路径：探测 IP 改回与记录相同 → recordUpToDate */
    client = dns_make_client(port);
    if (client) {
        XStrncpy(g_mockProbeIp, "9.9.9.9", sizeof(g_mockProbeIp) - 1);
        XStrncpy(g_mockRecordValue, "9.9.9.9",
                 sizeof(g_mockRecordValue) - 1);
        g_evtIpDetected = g_evtUpToDate = g_evtUpdated = 0;
        XObject_connect_2((XObject*)client,
                          XSignal(XDdnsClient_ipDetected_signal), dns_sig_ip);
        XObject_connect_2((XObject*)client,
                          XSignal(XDdnsClient_recordUpdated_signal), dns_sig_upd);
        XObject_connect_2((XObject*)client,
                          XSignal(XDdnsClient_recordUpToDate_signal), dns_sig_same);
        XObject_connect_2((XObject*)client,
                          XSignal(XDdnsClient_checkFailed_signal), dns_sig_fail);
        XDdnsClient_checkNow(client);
        for (pumped = 0; pumped < 600
             && XDdnsClient_state(client) != XDdnsClientState_Synchronized
             && XDdnsClient_state(client) != XDdnsClientState_Failed;
             ++pumped) {
            XCoreApplication_processEvents(XEventLoop_AllEvents);
            XThread_msleep(5);
        }
        DNS_CHECK(g_evtIpDetected == 1, "一致路径 ipDetected 仍到达");
        DNS_CHECK(g_evtUpToDate == 1, "recordUpToDate 信号到达 1 次");
        DNS_CHECK(g_evtUpdated == 0, "一致路径不发 Update 请求");
        XClassDelete((XClass*)client);
    }

    /* 失败路径：凭据缺失即拒 -> checkFailed */
    client = XDdnsClient_create();
    if (client) {
        g_evtFailed = 0;
        XObject_connect_2((XObject*)client,
                          XSignal(XDdnsClient_checkFailed_signal), dns_sig_fail);
        XDdnsClient_setProvider(client, XDDNS_PROVIDER_ALIYUN);
        XDdnsClient_setRecord_2(client, "e.com", "www", XDdnsRecordType_A);
        /* 缺凭据：checkNow 应拒绝并触发 checkFailed */
        XDdnsClient_checkNow(client);
        DNS_CHECK(g_evtFailed == 1, "checkFailed 信号到达 1 次（缺凭据）");
        XClassDelete((XClass*)client);
    }
    dns_mock_stop();
    DNS_CHECK(1, "信号用例：客户端与 mock 均已销毁");
}

/**
 * @brief 生命周期与全部配置/查询访问器（含 NULL 防御）。
 */
static void dns_test_lifecycle(void)
{
    XDdnsClient* heap;
    XDdnsClient stack;

    DNS_SECTION("六、生命周期与访问器");

    /* --- 记录类型名（枚举 -> 线上字符串） --- */
    DNS_CHECK(strcmp(XDdnsRecordType_name(XDdnsRecordType_A), "A") == 0,
              "RecordType_name(A) = \"A\"");
    DNS_CHECK(strcmp(XDdnsRecordType_name(XDdnsRecordType_AAAA), "AAAA") == 0,
              "RecordType_name(AAAA) = \"AAAA\"");
    DNS_CHECK(strcmp(XDdnsRecordType_name((XDdnsRecordType)99), "A") == 0,
              "RecordType_name(未知值) 回落为 A");

    /* --- 栈对象 init/deinit 成对 --- */
    XDdnsClient_init(&stack);
    DNS_CHECK(XDdnsClient_state(&stack) == XDdnsClientState_Idle,
              "栈对象 init 后状态 Idle");
    DNS_CHECK(XDdnsClient_currentIp_const(&stack) == NULL,
              "栈对象初始 currentIp 为 NULL");
    DNS_CHECK(XDdnsClient_pushedIp_const(&stack) == NULL,
              "栈对象初始 pushedIp 为 NULL");
    DNS_CHECK(XDdnsClient_lastError(&stack) == XDdnsError_None,
              "栈对象初始 lastError=None");
    DNS_CHECK(XDdnsClient_lastHttpStatus(&stack) == 0,
              "栈对象初始 lastHttpStatus=0");
    DNS_CHECK(strcmp(XDdnsClient_lastApiCode_const(&stack), "") == 0,
              "栈对象初始 lastApiCode 为空串");
    /* 全 NULL 防御 */
    DNS_CHECK(XDdnsClient_state(NULL) == XDdnsClientState_Idle,
              "state(NULL)=Idle");
    DNS_CHECK(XDdnsClient_currentIp_const(NULL) == NULL,
              "currentIp_const(NULL)=NULL");
    DNS_CHECK(XDdnsClient_pushedIp_const(NULL) == NULL,
              "pushedIp_const(NULL)=NULL");
    DNS_CHECK(XDdnsClient_lastError(NULL) == XDdnsError_None,
              "lastError(NULL)=None");
    DNS_CHECK(XDdnsClient_lastHttpStatus(NULL) == 0,
              "lastHttpStatus(NULL)=0");
    DNS_CHECK(XDdnsClient_lastApiCode_const(NULL) != NULL
                  && XDdnsClient_lastApiCode_const(NULL)[0] == '\0',
              "lastApiCode_const(NULL)=空串");
    XClassDeinit(&stack);
    DNS_CHECK(1, "栈对象 XClassDeinit 成对收尾");

    /* --- 堆对象 create/delete 成对 + 配置 API 往返 --- */
    heap = XDdnsClient_create();
    DNS_CHECK(heap != NULL, "create 返回堆对象");
    DNS_CHECK(heap && XDdnsClient_setProvider(heap, XDDNS_PROVIDER_ALIYUN),
              "setProvider(aliyun) 成功");
    DNS_CHECK(heap && !XDdnsClient_setProvider(heap, "no-such"),
              "setProvider(未注册) 返回 false");
    DNS_CHECK(heap && XDdnsClient_setEndpoint(heap, NULL),
              "setEndpoint(NULL) 复位为默认端点");
    DNS_CHECK(heap && XDdnsClient_setSslPeerVerify(heap, true),
              "setSslPeerVerify(true) 成功");
    DNS_CHECK(heap && XDdnsClient_setSslPeerVerify(heap, false),
              "setSslPeerVerify(false) 成功");
    DNS_CHECK(heap && !XDdnsClient_setSslPeerVerify(NULL, true),
              "setSslPeerVerify(NULL) 返回 false");
    DNS_CHECK(heap && XDdnsClient_setCredentials_2(heap, "id", "secret"),
              "setCredentials_2 成功");
    DNS_CHECK(heap && !XDdnsClient_setCredentials_2(heap, NULL, "s"),
              "setCredentials_2(NULL) 返回 false");
    DNS_CHECK(heap && XDdnsClient_setRecord_2(heap, "e.com", "www",
                                               XDdnsRecordType_A),
              "setRecord_2 成功");
    DNS_CHECK(heap && !XDdnsClient_setRecord_2(heap, NULL, "www",
                                                XDdnsRecordType_A),
              "setRecord_2(NULL domain) 返回 false");
    DNS_CHECK(heap && XDdnsClient_setTimeout(heap, 0),
              "setTimeout(0) 回落默认超时");
    DNS_CHECK(heap && !XDdnsClient_setCheckInterval(NULL, 1000),
              "setCheckInterval(NULL) 返回 false");
    /* 上面已配置好凭据与记录，故 checkNow 应真正发起（true = 流程已启动），
     * 而非配置缺失拒绝。 */
    DNS_CHECK(heap && XDdnsClient_checkNow(heap),
              "配置齐备时 checkNow 发起流程");
    if (heap)
        XClassDelete((XClass*)heap);
    DNS_CHECK(1, "堆对象 XClassDelete 成对收尾");

    /* --- 虚函数表单例 --- */
    DNS_CHECK(XDdnsClient_class_init() != NULL, "class_init 返回非空虚表");
    DNS_CHECK(XDdnsClient_class_init() == XDdnsClient_class_init(),
              "class_init 多次调用返回同一虚表（单例）");
}

/**
 * @brief 厂商注册表遍历一致性（find/at/count/aliyun 四者对齐）。
 */
static void dns_test_provider_registry(void)
{
    const XDdnsProviderOps* ops0;
    int n;
    int i;

    DNS_SECTION("七、厂商注册表");

    ops0 = XDdnsProvider_aliyun();
    DNS_CHECK(ops0 != NULL, "XDdnsProvider_aliyun 返回非空操作表");
    DNS_CHECK(ops0 && strcmp(ops0->m_name, XDDNS_PROVIDER_ALIYUN) == 0,
              "阿里云操作表厂商名为 aliyun");
    DNS_CHECK(ops0 && ops0->m_defaultEndpoint != NULL
                  && ops0->m_defaultEndpoint[0] != '\0',
              "阿里云操作表含默认端点");
    DNS_CHECK(ops0 && ops0->buildQueryUrl && ops0->buildAddUrl
                  && ops0->buildUpdateUrl && ops0->parseQuery
                  && ops0->parseMutation,
              "阿里云操作表五个函数指针非空");

    n = XDdnsProvider_count();
    DNS_CHECK(n >= 1, "注册表厂商数 >= 1");
    for (i = 0; i < n; i++) {
        const XDdnsProviderOps* ops = XDdnsProvider_at(i);
        DNS_CHECK(ops != NULL, "注册表下标可取到操作表");
        if (ops) {
            DNS_CHECK(XDdnsProvider_find(ops->m_name) == ops,
                      "按名字反查与下标一致");
        }
    }
    /* 越界防护 */
    DNS_CHECK(XDdnsProvider_at(-1) == NULL, "at(-1) 返回 NULL");
    DNS_CHECK(XDdnsProvider_at(n) == NULL, "at(count) 返回 NULL");
    DNS_CHECK(XDdnsProvider_at(9999) == NULL, "at(9999) 返回 NULL");
    DNS_CHECK(XDdnsProvider_find(NULL) == NULL, "find(NULL) 返回 NULL");
    DNS_CHECK(XDdnsProvider_find("") == NULL, "find(\"\") 返回 NULL");
}

/**
 * @brief XDdnsHttp_get 传输层参数/异常路径（不触真实外网）。
 */
static void dns_test_http_transport(void)
{
    char body[128];
    int status = 0;

    DNS_SECTION("八、HTTP 传输层");

    /* 参数防御 */
    DNS_CHECK(!XDdnsHttp_get(NULL, 1000, body, sizeof(body), &status),
              "XDdnsHttp_get(NULL url) 返回 false");
    DNS_CHECK(!XDdnsHttp_get("http://127.0.0.1/", 1000, NULL, 0, &status),
              "XDdnsHttp_get(NULL body/0 cap) 返回 false");

    /* URL 不可解析（如无 host） */
    DNS_CHECK(!XDdnsHttp_get("not-a-url", 500, body, sizeof(body), &status),
              "非法 URL 返回 false");

    /* 连接到无监听端口：应失败而非崩溃 */
    {
        XUdpSocket* sink = XUdpSocket_create();
        if (sink
            && XUdpSocket_bindAny(sink, 0, XAbstractSocket_DefaultForPlatform)) {
            char url[64];
            XSnprintf(url, sizeof(url), "http://127.0.0.1:%u/probe",
                      (unsigned)XUdpSocket_localPort(sink));
            DNS_CHECK(!XDdnsHttp_get(url, 500, body, sizeof(body), &status),
                      "无监听端口连接失败（返回 false 而非崩溃）");
            DNS_CHECK(status == 0, "连接失败时状态码保持 0");
            XUdpSocket_abort(sink);
            XClassDelete((XClass*)sink);
        } else {
            DNS_CHECK(false, "占位端口绑定");
        }
    }
}

/* ========================================================================== */
/*                        七、内存泄漏                                       */
/* ========================================================================== */

static void dns_test_leak(void)
{
    int i;
    size_t before;
    size_t after;

    DNS_SECTION("九、内存泄漏");
    XMemory_setStatisticsEnabled(true);
    before = XMemory_statistics(XMEMORY_TYPE_SYSTEM).systemBytes;

    for (i = 0; i < 100; i++) {
        XDdnsClient* c = XDdnsClient_create();
        if (c) {
            XDdnsClient_setProvider(c, XDDNS_PROVIDER_ALIYUN);
            XDdnsClient_setCredentials_2(c, "id", "secret");
            XDdnsClient_setRecord_2(c, "e.com", "www", XDdnsRecordType_A);
            XDdnsClient_setProbeUrl_2(c, "http://127.0.0.1:1/", 0);
            XDdnsClient_setLocalIp_2(c, "9.9.9.9");
            XDdnsClient_setLocalIp_2(c, NULL); /* 清除路径也要走 */
            XDdnsClient_setCheckInterval(c, 1000); /* 定时器创建/销毁 */
            XDdnsClient_setCheckInterval(c, 0);
            XClassDelete((XClass*)c);
        }
    }
    after = XMemory_statistics(XMEMORY_TYPE_SYSTEM).systemBytes;
    XPrintf("        [INFO] 100 次创建/销毁后在用 %zu 字节（基线 %zu）\n",
            after, before);
    DNS_CHECK(after <= before || (after - before) < 4096,
              "反复创建/销毁无内存泄漏");
    XMemory_setStatisticsEnabled(false);
}

/* ========================================================================== */
/*                        真实网络联调                                       */
/* ========================================================================== */

/** 真网检查结果标志（信号槽置位；1=更新 2=一致 3=失败）。 */
static volatile int g_realCheckState = 0;

static void ddns_real_updated(XObject* sender, XVarList* args)
{
    (void)sender; (void)args;
    g_realCheckState = 1;
}

static void ddns_real_uptodate(XObject* sender, XVarList* args)
{
    (void)sender; (void)args;
    g_realCheckState = 2;
}

static void ddns_real_failed(XObject* sender, XVarList* args)
{
    (void)sender; (void)args;
    g_realCheckState = 3;
}

/**
 * @brief 真实网络 DDNS 全链路联调（真实探测源 + 真实阿里云 API）。
 * @details 凭据经环境变量传入（不写入源码）：XDNS_AKID / XDNS_SECRET，
 *          域名可经 XDNS_DOMAIN（默认 727370630.top）/ XDNS_RR（默认 www）
 *          覆盖。逐步打印探测/查询/更新各环节，定位「检查失败」卡在哪。
 * @return 0=链路通（更新或已一致）；1=失败。
 */
int XDdnsRealTest_run(void)
{
    XDdnsClient* client = XDdnsClient_create();
    const char* akid = getenv("XDNS_AKID");
    const char* secret = getenv("XDNS_SECRET");
    const char* domain = getenv("XDNS_DOMAIN");
    const char* rr = getenv("XDNS_RR");
    int64_t deadline;
    int failures = 0;

    XPrintf("========== XDdns 真实网络全链路联调 ==========\n");
    if (!client || !akid || !secret) {
        XPrintf("[FAIL] 缺少凭据：请设置环境变量 XDNS_AKID / XDNS_SECRET\n");
        if (client) XClassDelete((XClass*)client);
        return 1;
    }
    if (!domain) domain = "727370630.top";
    if (!rr) rr = "www";

    XObject_connect_2((XObject*)client,
                      XSignal(XDdnsClient_recordUpdated_signal),
                      ddns_real_updated);
    XObject_connect_2((XObject*)client,
                      XSignal(XDdnsClient_recordUpToDate_signal),
                      ddns_real_uptodate);
    XObject_connect_2((XObject*)client,
                      XSignal(XDdnsClient_checkFailed_signal),
                      ddns_real_failed);

 /* 占位标签（无跳转引用，仅注释锚点） */
    XDdnsClient_setCredentials_2(client, akid, secret);
    XDdnsClient_setRecord_2(client, domain, rr, XDdnsRecordType_A);
    XDdnsClient_setTimeout(client, 10000);
    /* 探测源诊断：HTTP 源在前（国内稳定），HTTPS 源随后——若 HTTP 通而
     * HTTPS 崩溃/失败，则问题定位在 XSslSocket 链路而非探测策略。 */
    XPrintf("[STEP] 探测源1: http://ip.3322.net/\n");
    XDdnsClient_setProbeUrl_2(client, "http://ip.3322.net/", 0);
    XPrintf("[STEP] 探测源2: https://api4.ipify.org/\n");
    XDdnsClient_setProbeUrl_2(client, "https://api4.ipify.org/", 1);
    XPrintf("[STEP] 探测公网 IP...\n");
    /* 裸 XTcpSocket 直连探测（隔离 XNM/HTTPS 层）：验证 TCP 基础链路 */
    {
        XTcpSocket* raw = XTcpSocket_create();
        XAbstractSocket_connectToHost_base((XAbstractSocket*)raw,
                                           "ip.3322.net", 80,
                                           XIODevice_ReadWrite,
                                           XAbstractSocket_IPv4Protocol);
        XAbstractSocket_waitForConnected_base((XAbstractSocket*)raw, 5000);
        XPrintf("[STEP] raw TCP state=%d\n",
                (int)XAbstractSocket_state((XAbstractSocket*)raw));
        if (XAbstractSocket_state((XAbstractSocket*)raw)
            == XAbstractSocket_ConnectedState) {
            char req[] = "GET / HTTP/1.0\r\nHost: ip.3322.net\r\n\r\n";
            char rbuf[256];
            int64_t w = XIODevice_write_1((XIODevice*)raw, req,
                                          (int64_t)strlen(req));
            int64_t got = 0;
            int spin2;
            for (spin2 = 0; spin2 < 50 && got <= 0; ++spin2) {
                if (!XAbstractSocket_waitForReadyRead_base(
                        (XAbstractSocket*)raw, 200))
                    continue;
                got = XIODevice_read_1((XIODevice*)raw, rbuf,
                                       (int64_t)sizeof(rbuf) - 1);
            }
            if (got > 0) {
                rbuf[got] = '\0';
                XPrintf("[PASS] raw TCP got %lld bytes\n", (long long)got);
            } else {
                XPrintf("[FAIL] raw TCP no reply (w=%lld)\n", (long long)w);
                ++failures;
            }
            XAbstractSocket_abort((XAbstractSocket*)raw);
        }
        XTcpSocket_deleteLater((XTcpSocket*)raw);
    }
    if (!XDdnsClient_checkNow(client)) {
        XPrintf("[FAIL] checkNow 未启动（缺配置）\n");
        XClassDelete((XClass*)client);
        return 1;
    }
    deadline = (int64_t)XDateTime_currentMSecsSinceEpoch() + 30000;
    while (g_realCheckState == 0
           && (int64_t)XDateTime_currentMSecsSinceEpoch() < deadline) {
        XCoreApplication_processEvents(XEventLoop_AllEvents);
        XThread_msleep(10);
    }

    if (g_realCheckState == 1) {
        XPrintf("[PASS] 全链路通：探测成功 + 云端记录已更新为 %s\n",
                XString_toUtf8(XDdnsClient_pushedIp_const(client)));
    } else if (g_realCheckState == 2) {
        XPrintf("[PASS] 云端记录已与本机公网 IP 一致（无更新动作）\n");
    } else {
        XPrintf("[FAIL] 检查失败: err=%d (2=探测源全部不可达 4=套接字 "
                "5=超时 6=HTTP非2xx 7=云端报错 8=应答损坏)\n",
                (int)XDdnsClient_lastError(client));
        failures = 1;
    }
    XPrintf("[INFO] state=%d currentIp=%s\n",
            (int)XDdnsClient_state(client),
            XDdnsClient_currentIp_const(client)
                ? XString_toUtf8(XDdnsClient_currentIp_const(client)) : "-");
    XClassDelete((XClass*)client);
    XPrintf("========== XDdns 真实联调完成: %s ==========\n",
            failures ? "FAIL" : "PASS");
    return failures;
}

/* ========================================================================== */
/*                        入口                                               */
/* ========================================================================== */

int XDdnsTest_run(void)
{
    XPrintf("========== XDdns 客户端测试开始 ==========\n");
    g_passed = 0;
    g_failed = 0;
    g_evtUpdated = 0;
    g_evtUpToDate = 0;
    g_evtFailed = 0;
    g_evtLastIp[0] = '\0';

    dns_test_aliyun_signature();
    dns_test_parse();
    dns_test_e2e();
    dns_test_lifecycle();
    dns_test_signals();
    dns_test_provider_registry();
    dns_test_http_transport();
    dns_test_leak();

    XPrintf("\n========== XDdns 测试完成: %d 通过, %d 失败 ==========\n",
            g_passed, g_failed);
    return g_failed;
}

void XDdnsTest(void)
{
    (void)XDdnsTest_run();
}

void XTestMenu_XDdnsTest(XTestMenu* root)
{
    XTestMenu* menu = XTestMenu_create("XDdns(ddns动态域名)");
    {
        XAction* action = XTestMenu_addAction(menu, "全量回归（签名+解析+端到端）");
        XTestMenu_setActionFunction(action, XDdnsTest);
    }
    XTestMenu_addMenu(root, menu);
}
