/**
 * @file       hsf_server_io_valve_fresh_test.c
 * @brief      电磁阀组「反馈模块离线」语义单测（HsfServerIoValve 隔离）。
 * @details    [为什么必须有这组用例] 2026-11-05 实锅：点级 m_valid 是单向的
 *             （只置 1、从不清零），加上电磁阀把「整站 DI 有数据」当成在线
 *             判据，导致扩展模块 .13 掉线时：
 *               · 六路阀仍显示 valid，读到恒 0 的 DI 被当成"反馈正常"；
 *               · 提示退化成"无到位反馈（检查行程开关接线）"——
 *                 真正的问题是模块离线，提示却把人引向接线；
 *               · setOutput 仍然受理，界面先"动作中"、后"动作超时未到位"，
 *                 **编造出"阀卡住"的假象**。
 *             修复引入了三层：IXoLink::RoleFresh 虚槽、XIoChannelGroup_isFresh、
 *             以及阀组的按通道新鲜度位图。本文件锁定这三层的对外行为。
 *
 *             [为什么是纯业务层单测] HsfServerIoValve 不持有传输对象（DO/DI
 *             由调用方喂进来），故不需要 TCP 从站：直接给 diMask/diFreshMask
 *             就能精确构造"主模块在线、扩展模块掉线"这类组合——真从站反而
 *             做不到（不能只让一台模块的某半区失效）。
 */
#include "HsfServerIoValve.h"

#include "XCoreApplication.h"
#include "XThread.h"
#include "XPrintf.h"
#include <string.h>

#if XINDUSTRY_ON && XINDUSTRY_CHANNEL_ON

static int g_pass = 0;
static int g_fail = 0;

#define CHECK(desc, cond) \
    do { \
        if (cond) { g_pass++; XPrintf("[PASS] %s\n", desc); } \
        else      { g_fail++; XPrintf("[FAIL] %s\n", desc); } \
    } while (0)

/** @brief 事件泵小睡（信号派发驱动）。 */
static void spinMs(int64_t ms)
{
    int64_t t0 = XDateTime_currentMSecsSinceEpoch();
    while (XDateTime_currentMSecsSinceEpoch() - t0 < ms) {
        XCoreApplication_processEvents(0);
        XThread_msleep(2);
    }
}

/* ==================== 夹具 ==================== */

/**
 * @brief 六路阀全部落在聚合上半区（逻辑点 16..31）。
 * @details 与现场一致：阀 DO 16..21、开到位 DI 17/19/21/23/25/27、
 *          关到位 DI 16/18/20/22/24/26。
 */
static const int kDo[6]     = { 16, 17, 18, 19, 20, 21 };
static const int kOpenDi[6] = { 17, 19, 21, 23, 25, 27 };
static const int kCloseDi[6]= { 16, 18, 20, 22, 24, 26 };

typedef struct ValveFx {
    HsfServerIoValve m_bank;
} ValveFx;

static void fxInit(ValveFx* fx, bool enabled)
{
    HsfServerIoValveCfg cfg[HSF_VALVE_COUNT];
    int i;

    memset(fx, 0, sizeof(*fx));
    for (i = 0; i < HSF_VALVE_COUNT; ++i) {
        cfg[i].m_name = NULL;          /* 用缺省名。 */
        cfg[i].m_do = (i < 6) ? kDo[i] : -1;
        cfg[i].m_openDi = (i < 6) ? kOpenDi[i] : -1;
        cfg[i].m_closeDi = (i < 6) ? kCloseDi[i] : -1;
    }
    HsfServerIoValve_init(&fx->m_bank);
    HsfServerIoValve_configure(&fx->m_bank, cfg, 6, 20000, enabled);
}

static void fxDeinit(ValveFx* fx)
{
    if (!fx) return;
    HsfServerIoValve_clear(&fx->m_bank);
}

/** @brief 构造「上半区（16..31）全部新鲜」的位图。 */
static uint32_t freshUpperOnly(void)
{
    return 0xFFFF0000u;
}

/** @brief 构造「只有主模块 0..15 新鲜」的位图（= .13 掉线）。 */
static uint32_t freshLowerOnly(void)
{
    return 0x0000FFFFu;
}

/* ==================== 断言辅助 ==================== */

static bool itemTextIs(const HsfServerIoValve* bank, int i, const char* needle)
{
    const HsfServerIoValveItem* it = HsfServerIoValve_item(bank, i);
    if (!it) return false;
    return strstr(it->m_text, needle) != NULL;
}

/* ==================== ① 扩展模块离线：明确报错，不伪装 ==================== */

static void test_extOfflineReportsUncontrollable(void)
{
    ValveFx fx;
    const HsfServerIoValveItem* it;
    char err[256];

    XPrintf("--- ① 扩展模块离线 ---\n");
    fxInit(&fx, true);

    /* 先喂一拍"全新鲜"建立正常基线（模拟模块本来在线）。 */
    HsfServerIoValve_poll(&fx.m_bank, 0x00000000u, true, freshUpperOnly(), 0, false, 1000);
    it = HsfServerIoValve_item(&fx.m_bank, 0);
    CHECK("基线：模块在线时阀0 可用", it && it->m_online);

    /* 让它走到"已关"：只给关到位 DI16，不给开到位 DI17。
     * [不能用 0xFFFFFFFF] 那样开/关到位同时为 1，判读逻辑会走到**互斥故障**，
     * 测出来的不是"已关"（首版断言就踩了这个）。 */
    HsfServerIoValve_poll(&fx.m_bank, (1u << 16), true, freshUpperOnly(), 0, false, 1200);
    it = HsfServerIoValve_item(&fx.m_bank, 0);
    CHECK("基线：关到位反馈生效 = 已关",
          it && it->m_state == HsfValveState_Closed);

    /* ---- 现在 .13 掉线：上半区不新鲜，下半区仍新鲜 ---- */
    HsfServerIoValve_poll(&fx.m_bank, 0x00000000u, true, freshLowerOnly(), 0, false, 1500);
    it = HsfServerIoValve_item(&fx.m_bank, 0);
    CHECK("离线：阀0 标记为不在线", it && !it->m_online);
    CHECK("离线：阀0 valid=false", it && !it->m_valid);
    CHECK("离线：状态不再伪装成已关",
          it && it->m_state == HsfValveState_Unknown);
    CHECK("离线：文案明确指出模块离线，而非'无到位反馈'",
          itemTextIs(&fx.m_bank, 0, "离线"));
    CHECK("离线：文案不得出现'行程开关'（否则带偏排查方向）",
          !itemTextIs(&fx.m_bank, 0, "行程开关"));

    /* ---- 控制必须被拒，且原因可读 ---- */
    err[0] = '\0';
    CHECK("离线：开阀被拒",
          !HsfServerIoValve_setOutput(&fx.m_bank, 0, true, err, (int)sizeof(err)));
    CHECK("离线：错误信息含'离线'", strstr(err, "离线") != NULL);
    CHECK("离线：错误信息含'无法控制'", strstr(err, "无法控制") != NULL);
    CHECK("离线：错误信息明确指出反馈模块（不是阀体/接线问题）",
          strstr(err, "反馈模块") != NULL);

    /* ---- 关键：拒绝时不得留下命令意图（否则界面会演一出"阀卡住"） ---- */
    it = HsfServerIoValve_item(&fx.m_bank, 0);
    CHECK("离线：被拒后不记命令意图（否则会伪造'动作中'→'超时'）",
          it && !it->m_online && it->m_cmdAtMs == 0);

    fxDeinit(&fx);
}

/* ==================== ② 部分通道新鲜：按通道各自判定 ==================== */

static void test_perChannelFreshness(void)
{
    ValveFx fx;
    const HsfServerIoValveItem* it0;
    const HsfServerIoValveItem* it1;

    XPrintf("--- ② 按通道新鲜度（不能整组一个 bool） ---\n");
    fxInit(&fx, true);

    /* 只让阀1 的两个 DI（19/18）新鲜，其余都不新鲜：
     * 这是「只有部分点位所在的模块在线」的情形。
     * 若实现用一个整组 bool，阀0 会连带被判在线。 */
    {
        uint32_t mask = (1u << 19) | (1u << 18);
        HsfServerIoValve_poll(&fx.m_bank, 0u, true, mask, 0, false, 1000);
    }
    it0 = HsfServerIoValve_item(&fx.m_bank, 0);
    it1 = HsfServerIoValve_item(&fx.m_bank, 1);
    CHECK("阀0（通道 17/16 不新鲜）判离线", it0 && !it0->m_online);
    CHECK("阀1（通道 19/18 新鲜）判在线", it1 && it1->m_online);

    /* 单边新鲜也不够：只知道开到位、不知道关到位，就无法判阀位。 */
    {
        uint32_t mask = (1u << 17);     /* 阀0 只有开到位新鲜。 */
        HsfServerIoValve_poll(&fx.m_bank, 0u, true, mask, 0, false, 1200);
    }
    it0 = HsfServerIoValve_item(&fx.m_bank, 0);
    CHECK("单边新鲜不算可用（两个到位缺一即判离线）", it0 && !it0->m_online);

    fxDeinit(&fx);
}

/* ==================== ③ 模块恢复后自动回到正常 ==================== */

static void test_recoveryAfterReconnect(void)
{
    ValveFx fx;
    const HsfServerIoValveItem* it;

    XPrintf("--- ③ 模块恢复 ---\n");
    fxInit(&fx, true);

    /* 离线一拍。 */
    HsfServerIoValve_poll(&fx.m_bank, 0u, true, freshLowerOnly(), 0, false, 1000);
    it = HsfServerIoValve_item(&fx.m_bank, 0);
    CHECK("离线拍：阀0 不在线", it && !it->m_online);

    /* .13 恢复：上半区重新新鲜，且**只**关到位（DI16）为 1。
     * [同上] 不能给 0xFFFFFFFF——那样开/关到位同时为 1 会判互斥故障，
     * 而不是恢复正常位置。 */
    HsfServerIoValve_poll(&fx.m_bank, (1u << 16), true, freshUpperOnly(), 0, false, 1200);
    it = HsfServerIoValve_item(&fx.m_bank, 0);
    CHECK("恢复后：阀0 重新在线", it && it->m_online);
    CHECK("恢复后：状态回到已关", it && it->m_state == HsfValveState_Closed);
    CHECK("恢复后：文案清空", it && it->m_text[0] == '\0');

    /* 恢复后控制应被受理。 */
    CHECK("恢复后：开阀被受理",
          HsfServerIoValve_setOutput(&fx.m_bank, 0, true, NULL, 0));

    fxDeinit(&fx);
}

/* ==================== ④ 从未配置 / 未启用仍走原有拒绝路径 ==================== */

static void test_existingRejectionsUnchanged(void)
{
    ValveFx fx;

    XPrintf("--- ④ 原有拒绝路径未被破坏 ---\n");

    /* 未启用。 */
    fxInit(&fx, false);
    CHECK("未启用：开阀被拒",
          !HsfServerIoValve_setOutput(&fx.m_bank, 0, true, NULL, 0));
    fxDeinit(&fx);

    /* 启用但越界。 */
    fxInit(&fx, true);
    CHECK("越界：阀号 6 被拒",
          !HsfServerIoValve_setOutput(&fx.m_bank, 6, true, NULL, 0));
    CHECK("越界：阀号 -1 被拒",
          !HsfServerIoValve_setOutput(&fx.m_bank, -1, true, NULL, 0));
    fxDeinit(&fx);
}

/* ==================== ⑤ 装配期缺省乐观（首拍前不误拒） ==================== */

static void test_optimisticBeforeFirstPoll(void)
{
    ValveFx fx;
    char err[128];

    XPrintf("--- ⑤ 首拍前不误拒 ---\n");
    fxInit(&fx, true);

    /* 装配完成到第一次 poll 之间（最多一个轮询周期）。此时没有在线信息，
     * 不该凭空判离线——否则用户刚启动就点开关会被莫名拒绝。 */
    err[0] = '\0';
    CHECK("首拍前：允许下发（不凭空判离线）",
          HsfServerIoValve_setOutput(&fx.m_bank, 0, true, err, (int)sizeof(err)));

    fxDeinit(&fx);
}

/* ==================== 入口 ==================== */

int XHsfServerIoValveFreshTest_run(void)
{
    g_pass = 0;
    g_fail = 0;

    XPrintf("===== 电磁阀反馈新鲜度语义单测 =====\n");
    test_extOfflineReportsUncontrollable();
    test_perChannelFreshness();
    test_recoveryAfterReconnect();
    test_existingRejectionsUnchanged();
    test_optimisticBeforeFirstPoll();

    XPrintf("===== 结果：PASS %d / FAIL %d =====\n", g_pass, g_fail);
    return g_fail;
}

void XHsfServerIoValveFreshTest(XVariant* data)
{
    (void)data;
    XHsfServerIoValveFreshTest_run();
}

#endif /* XINDUSTRY_ON && XINDUSTRY_CHANNEL_ON */
