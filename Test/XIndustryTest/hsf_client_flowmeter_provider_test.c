/**
 * @file       hsf_client_flowmeter_provider_test.c
 * @brief      客户端电磁流量计 provider（HsfFlowMeterProvider）隔离单测。
 * @details    [测什么] provider 的核心价值是**两层质量叠加**：
 *             数据"有效" = 服务端说 valid && 本地推送没断流（新鲜度）。
 *             只信服务端 valid 会把断线后的旧值当实时数据显示（温湿度页
 *             踩过这个坑）。本文件把这条叠加逻辑钉死。
 *
 *             [怎么注入数据] 走 HsfLinkClient_flowMeterInject —— 它不经网络，
 *             但走**与真实推送完全相同的解析路径**（hsfLcParseFlowMeter），
 *             所以测到的是生产代码而不是复制品。
 *
 *             [可测/不可测边界]
 *               - BSON 解析（double/int32/bool/缺键/非法字节）→ 可测；
 *               - 新鲜度叠加（valid && fresh）→ 可测（含超时翻 false）；
 *               - 回调转发（注入 → provider → 页面令牌）→ 可测；
 *               - 真实网络推送 → 不在本文件（端到端见
 *                 verify/verify_hsf_flowmeter.py）。
 */
#include "HsfFlowMeterProvider.h"
#include "HsfLinkClient.h"

#include "XBsonDocument.h"
#include "XByteArray.h"
#include "XDateTime.h"
#include "XCoreApplication.h"
#include "XThread.h"
#include "XPrintf.h"
#include <string.h>

#if XINDUSTRY_ON || XVFD_ON

/* ==================== 测试辅助 ==================== */

static int g_pass = 0;
static int g_fail = 0;

#define CHECK(desc, cond) \
    do { \
        if (cond) { g_pass++; XPrintf("[PASS] %s\n", desc); } \
        else      { g_fail++; XPrintf("[FAIL] %s\n", desc); } \
    } while (0)

static void spinMs(int64_t ms)
{
    int64_t t0 = XDateTime_currentMSecsSinceEpoch();
    while (XDateTime_currentMSecsSinceEpoch() - t0 < ms) {
        XCoreApplication_processEvents(0);
        XThread_msleep(2);
    }
}

/**
 * @brief 构造一帧流量计遥测 BSON（键名同服务端 hsfAppFillFlowMeter）。
 * @param side 侧号。
 * @param flow 瞬时流量；speed 流速；fwd 正向累积。
 * @param valid 服务端质量位。
 * @param outSize 输出字节数。
 * @return 新建 XByteArray（调用方 XClassDelete）；失败 NULL。
 */
static XByteArray* makeFrame(int side, double flow, double speed, double fwd,
                             int flowUnit, int totalUnit, int alarms,
                             bool valid, uint32_t* outSize)
{
    XBsonDocument* doc = XBsonDocument_create();
    XByteArray* bytes;
    if (!doc) return NULL;
    XBsonDocument_insert_keyUtf8_int32(doc, "side", (int32_t)side);
    XBsonDocument_insert_keyUtf8_bool(doc, "valid", valid);
    if (valid) {
        XBsonDocument_insert_keyUtf8_double(doc, "flow", flow);
        XBsonDocument_insert_keyUtf8_double(doc, "speed", speed);
        XBsonDocument_insert_keyUtf8_double(doc, "forwardTotal", fwd);
        XBsonDocument_insert_keyUtf8_double(doc, "reverseTotal", 0.0);
        XBsonDocument_insert_keyUtf8_int32(doc, "flowUnit", (int32_t)flowUnit);
        XBsonDocument_insert_keyUtf8_int32(doc, "totalUnit", (int32_t)totalUnit);
        XBsonDocument_insert_keyUtf8_int32(doc, "alarms", (int32_t)alarms);
    }
    bytes = XBsonDocument_toBson(doc);
    XClassDelete((XClass*)doc);
    if (bytes && outSize)
        *outSize = (uint32_t)XByteArray_size_base(bytes);
    return bytes;
}

/** @brief 注入一帧（便捷包装）。 */
static bool injectFrame(int side, double flow, bool valid, int alarms)
{
    uint32_t n = 0;
    XByteArray* b = makeFrame(side, flow, -22.0625, 19088743.0,
                              5 /*M3/H*/, 1 /*M3*/, alarms, valid, &n);
    bool ok;
    if (!b) return false;
    ok = HsfLinkClient_flowMeterInject(
        (const uint8_t*)XByteArray_constData(b), n);
    XClassDelete((XClass*)b);
    return ok;
}

/* ==================== 用例 ==================== */

/**
 * @brief BSON 解析：字段逐一落位，缺键取默认。
 */
static void test_parse(void)
{
    const HsfLinkFlowMeter* s;

    XPrintf("---- BSON 解析 ----\n");
    CHECK("注入 B 侧帧成功", injectFrame(1, -625.5, true, 0x04));

    s = HsfLinkClient_flowMeter(1);
    CHECK("B 侧快照可取", s != NULL);
    if (!s) return;
    CHECK("m_received = true", s->m_received);
    CHECK("m_valid = true（服务端口径）", s->m_valid);
    CHECK("flow = -625.5", s->m_flow < -625.49 && s->m_flow > -625.51);
    CHECK("speed = -22.0625", s->m_speed < -22.06 && s->m_speed > -22.07);
    CHECK("forwardTotal = 19088743",
          s->m_forwardTotal > 19088742.9 && s->m_forwardTotal < 19088743.1);
    CHECK("flowUnit = 5 (M3/H)", s->m_flowUnit == 5);
    CHECK("totalUnit = 1 (M3)", s->m_totalUnit == 1);
    CHECK("alarms = 0x04（空管）", s->m_alarms == 0x04);

    /* A 侧未注入：保持未收到。 */
    {
        const HsfLinkFlowMeter* a = HsfLinkClient_flowMeter(0);
        CHECK("A 侧未注入时 m_received = false", a && !a->m_received);
    }

    /* 侧号越界。 */
    CHECK("侧号 -1 返回 NULL", HsfLinkClient_flowMeter(-1) == NULL);
    CHECK("侧号 2 返回 NULL", HsfLinkClient_flowMeter(2) == NULL);
}

/**
 * @brief 非法载荷拒绝（不破坏已有快照）。
 */
static void test_badPayload(void)
{
    const HsfLinkFlowMeter* s;

    XPrintf("---- 非法载荷 ----\n");
    injectFrame(1, -625.5, true, 0x04);   /* 先建好一份有效快照 */

    CHECK("NULL 载荷拒绝", !HsfLinkClient_flowMeterInject(NULL, 0));
    CHECK("空载荷拒绝", !HsfLinkClient_flowMeterInject((const uint8_t*)"", 0));
    CHECK("非 BSON 字节拒绝",
          !HsfLinkClient_flowMeterInject((const uint8_t*)"not bson", 8));

    /* 侧号越界的帧（side=9）应被拒。 */
    {
        uint32_t n = 0;
        XByteArray* b = makeFrame(9, 1.0, 1.0, 1.0, 5, 1, 0, true, &n);
        bool ok = false;
        if (b) {
            ok = HsfLinkClient_flowMeterInject(
                (const uint8_t*)XByteArray_constData(b), n);
            XClassDelete((XClass*)b);
        }
        CHECK("侧号越界帧被拒", !ok);
    }

    /* 已有快照应保持原值（非法帧不应清零）。 */
    s = HsfLinkClient_flowMeter(1);
    CHECK("非法帧后原有快照未被破坏",
          s && s->m_flow < -625.49 && s->m_flow > -625.51);
}

/**
 * @brief 新鲜度：超时后 Fresh 翻 false，但 m_received 保持 true。
 * @details 这正是「不能直读 m_received」的原因——断线后它仍是 true。
 */
static void test_freshness(void)
{
    const HsfLinkFlowMeter* s;

    XPrintf("---- 新鲜度 ----\n");
    injectFrame(1, 0.0, true, 0x00);

    CHECK("刚注入时 Fresh = true", HsfLinkClient_flowMeterFresh(1));
    s = HsfLinkClient_flowMeter(1);
    CHECK("刚注入时 m_received = true", s && s->m_received);

    /* 等过陈旧阈值（3500ms），再多留 600ms 余量。 */
    XPrintf("       等待 %u ms 判定陈旧…\n",
            (unsigned)(HSF_LINK_FLOWMETER_STALE_MS + 600u));
    spinMs((int64_t)HSF_LINK_FLOWMETER_STALE_MS + 600);

    CHECK("超时后 Fresh = false", !HsfLinkClient_flowMeterFresh(1));
    s = HsfLinkClient_flowMeter(1);
    CHECK("超时后 m_received 仍为 true（历史事实不复位）", s && s->m_received);

    /* 再注入一帧应恢复新鲜。 */
    injectFrame(1, 0.0, true, 0x00);
    CHECK("重新注入后 Fresh = true", HsfLinkClient_flowMeterFresh(1));

    CHECK("未注入过的 A 侧 Fresh = false", !HsfLinkClient_flowMeterFresh(0));
    CHECK("侧号越界 Fresh = false", !HsfLinkClient_flowMeterFresh(5));
}

/**
 * @brief provider 翻译层：把 HsfLinkFlowMeter 映射为本表口径，并叠加双层质量。
 */
static void test_providerSnapshot(void)
{
    const HsfFlowMeterProvider* prov;
    const HsfFlowMeterSnapshot* snap;

    XPrintf("---- provider 翻译与双层质量 ----\n");
    prov = HsfFlowMeterProvider_remote();
    CHECK("取 provider（不按侧寻址，2026-11-10）", prov != NULL);
    CHECK("snapshot 槽非空", prov && prov->snapshot != NULL);
    if (!prov || !prov->snapshot) return;

    /* ① 服务端 valid + 本地新鲜 → 有效 */
    injectFrame(1, -625.5, true, 0x04);
    snap = prov->snapshot(prov->m_userData);
    CHECK("两层都满足时 m_valid = true", snap && snap->m_valid);
    CHECK("flow 透传 = -625.5",
          snap && snap->m_flow < -625.49 && snap->m_flow > -625.51);
    CHECK("alarms 透传 = 0x04", snap && snap->m_alarms == 0x04);
    CHECK("flowUnit 透传 = 5", snap && snap->m_flowUnit == 5);
    CHECK("totalUnit 透传 = 1", snap && snap->m_totalUnit == 1);

    /* ② 服务端 valid=false → 无效（即使本地刚收到） */
    injectFrame(1, 0.0, false, 0);
    snap = prov->snapshot(prov->m_userData);
    CHECK("服务端 valid=false 时 provider m_valid = false",
          snap && !snap->m_valid);

    /* ③ 服务端 valid=true 但本地陈旧 → 无效（关键：不能只信服务端） */
    injectFrame(1, -625.5, true, 0x00);
    spinMs((int64_t)HSF_LINK_FLOWMETER_STALE_MS + 600);
    snap = prov->snapshot(prov->m_userData);
    CHECK("本地陈旧时 provider m_valid = false（双层质量生效）",
          snap && !snap->m_valid);

    /* ④ 活动侧由服务端帧告知：注入 side=1 的帧后，客户端知道流量计在
     *    1 侧，provider 取的就是该侧的数据（客户端自己不点名侧号）。 */
    CHECK("活动侧取自服务端帧（注入 side=1 后为 1）",
          HsfLinkClient_flowMeterSide() == 1);
    CHECK("provider 快照走活动侧（与 HsfLinkClient_flowMeterActive 同源）",
          HsfLinkClient_flowMeterActive() == HsfLinkClient_flowMeter(1));
    injectFrame(1, -625.5, true, 0x00);
    snap = prov->snapshot(prov->m_userData);
    CHECK("活动侧数据可取到（flow=-625.5）",
          snap && snap->m_flow < -625.49 && snap->m_flow > -625.51);

    /* ⑤ provider 不按侧寻址：多次取回同一个表对象。 */
    CHECK("provider 不按侧寻址（同一表对象）",
          HsfFlowMeterProvider_remote() == prov);
}

/**
 * @brief 订阅转发：注入 → provider 回调 → 页面令牌原样回传。
 */
static int g_notifyCount = 0;
static void* g_notifyToken = NULL;
static const struct HsfFlowMeterProvider* g_notifyProv = NULL;

static void probeNotify(const struct HsfFlowMeterProvider* provider, void* page)
{
    ++g_notifyCount;
    g_notifyToken = page;
    g_notifyProv = provider;
}

static void test_subscribe(void)
{
    const HsfFlowMeterProvider* prov;
    int marker = 0x5A5A;

    XPrintf("---- 订阅转发 ----\n");
    prov = HsfFlowMeterProvider_remote();
    if (!prov || !prov->subscribe) { CHECK("subscribe 槽", false); return; }

    g_notifyCount = 0;
    g_notifyToken = NULL;
    g_notifyProv = NULL;

    CHECK("订阅成功", prov->subscribe(prov->m_userData, probeNotify, &marker));

    injectFrame(1, 1.0, true, 0x00);
    CHECK("注入触发 provider 回调", g_notifyCount == 1);
    CHECK("页面令牌原样回传", g_notifyToken == (void*)&marker);
    CHECK("回调收到本表指针", g_notifyProv == prov);

    injectFrame(1, 2.0, true, 0x00);
    CHECK("再次注入再次回调", g_notifyCount == 2);

    /* 注销后不再回调。 */
    if (prov->unsubscribe) {
        prov->unsubscribe(prov->m_userData, &marker);
        injectFrame(1, 3.0, true, 0x00);
        CHECK("注销后不再回调", g_notifyCount == 2);
    }

    /* 令牌不匹配的注销不生效。 */
    {
        int other = 0;
        if (prov->subscribe) prov->subscribe(prov->m_userData, probeNotify, &marker);
        if (prov->unsubscribe) prov->unsubscribe(prov->m_userData, &other);
        g_notifyCount = 0;
        injectFrame(1, 4.0, true, 0x00);
        CHECK("令牌不匹配的注销不生效", g_notifyCount == 1);
        if (prov->unsubscribe) prov->unsubscribe(prov->m_userData, &marker);
    }
}

/* ==================== 运行器 ==================== */

static void runAll(void)
{
    test_parse();
    test_badPayload();
    test_providerSnapshot();
    test_subscribe();
    test_freshness();     /* 放最后：耗时（等陈旧阈值） */
}

int XHsfClientFlowMeterProviderTest_run(void)
{
    XCoreApplication* app;
    bool owned;

    owned = (XCoreApplication_instance() == NULL);
    app = XCoreApplication_create(0, NULL);
    if (!app) {
        XPrintf("[FAIL] 事件循环创建失败\n");
        return 1;
    }

    runAll();
    XPrintf("\n===== XHsfClientFlowMeterProviderTest: %d passed, %d failed =====\n",
            g_pass, g_fail);

    if (owned) XClassDelete((XClass*)app);
    return g_fail == 0 ? 0 : 1;
}

void XHsfClientFlowMeterProviderTest(XVariant* data)
{
    const int fails = XHsfClientFlowMeterProviderTest_run();
    (void)data;
    XPrintf("=== XHsfClientFlowMeterProviderTest: %s（失败 %d 项）===\n",
            fails == 0 ? "PASS" : "FAIL", fails);
    fflush(stdout);
}

#else

#include "XPrintf.h"
int XHsfClientFlowMeterProviderTest_run(void)
{
    XPrintf("XHsfClientFlowMeterProviderTest: XINDUSTRY/XVFD 未启用，跳过\n");
    return 0;
}

#endif
