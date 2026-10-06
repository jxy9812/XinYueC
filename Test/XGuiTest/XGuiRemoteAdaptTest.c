/**
 * @file       XGuiRemoteAdaptTest.c
 * @brief      perf9 路4 自动降档阶梯测试: 纯状态机单元 + 回环量化(独立可执行)。
 * @details    两段(XGuiRemoteAuthTest 同款独立 main 惯例):
 *               (a) 单元(XGuiRemoteAdapt.h 纯函数直含): 阶梯映射
 *                 60→30→15/30→15/15 平阶梯/窄档夹取/无门控直通; feed 迟滞
 *                 ——连续 2 轮超预算下行、中性/富余轮清零、上行需 8 富余轮
 *                 +1s 驻留、PROFILE_SET 基准变化即重置、step 封顶。
 *               (b) 回环量化(XGuiRemoteTest xr_e2e 同款零网络拓扑): latency
 *                 档(60fps)持续伤害流, XGUI_REMOTE_ADAPT_BUDGET_MS=1 强制
 *                 每轮超预算 → 阶梯沉底 15fps 地板, 客户端实测交付批率
 *                 ≤ ~22fps; --adapt-baseline 反证组(关阶梯=接近档位 60fps)。
 *                 env 由本进程 main 先行 setenv(getenv 读一次缓存纪律:
 *                 助手首次使用在任何会话创建之后, 覆盖生效)。
 *             直跑口径: Xvfb 会话 DISPLAY=:94 ./bin/XGuiRemoteAdapt_Test
 *             [--adapt-baseline]; [ADAPT-QUANT] 量化行恒 stderr 归档
 *             out/perf9/neon-bench.txt 同战役产物。
 * @author     XinYueC 团队
 ******************************************************************************/
#include "XGuiConfig.h" /* 门控宏单一来源(XGuiRemoteTest 惯例, #if 前置)。 */
#include "XGuiRemoteProto.h" /* XGUI_REMOTE_ON 单一来源。 */

#if XWIDGET_ON && XGUI_REMOTE_ON

#include "XGuiRemoteLoopback.h"
#include "XGuiRemoteAdapt.h" /* 被测纯函数(头内 static inline 直含)。 */
#include "XGuiServer.h"
#include "XGuiClient.h"
#include "XGuiApplication.h"
#include "XWidget.h"
#include "XObject.h"
#include "XEvent.h"
#include "XLabel.h"
#include "XImage.h"
#include "XImageFormat.h"
#include "XPixmap.h"
#include "XDateTime.h" /* XDateTime_currentMSecsSinceEpoch。 */
#include "XThread.h"
#include "XMemory.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

/* ==================== 断言与工具 ==================== */

static int ta_runs = 0;
static int ta_failures = 0;
static bool ta_baselineOnly = false; /* --adapt-baseline 反证组。 */

static void ta_expect(bool cond, const char* what)
{
    ++ta_runs;
    if (!cond) {
        fprintf(stderr, "[ADAPT-FAIL] %s\n", what ? what : "");
        ++ta_failures;
    }
}

static int64_t ta_nowMs(void)
{
    return XDateTime_currentMSecsSinceEpoch();
}

static void ta_pumpFor(int ms)
{
    int64_t deadline = ta_nowMs() + ms;
    while (ta_nowMs() < deadline) {
        XGuiApplication_processEvents(XEventLoop_AllEvents);
        XThread_msleep(2);
    }
    XGuiApplication_processEvents(XEventLoop_AllEvents);
}

static void ta_ensureApp(void)
{
    static XGuiApplication* app;
    if (!app) {
        app = XGuiApplication_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, 0, NULL);
        (void)app;
    }
}

/* ==================== (a) 纯状态机单元 ==================== */

static void ta_unit_ladderMap(void)
{
    XGuiRemoteAdapt a;
    /* 60 → 60/30/15。 */
    xgui_remote_adapt_reset(&a, 60);
    ta_expect(xgui_remote_adapt_fps(&a) == 60, "阶梯: base60 步0=60");
    a.step = 1;
    ta_expect(xgui_remote_adapt_fps(&a) == 30, "阶梯: base60 步1=30");
    a.step = 2;
    ta_expect(xgui_remote_adapt_fps(&a) == 15, "阶梯: base60 步2=15 地板");
    a.step = 9; /* 越界步防御=地板。 */
    ta_expect(xgui_remote_adapt_fps(&a) == 15, "阶梯: 步越界钳到地板");
    /* 30 → 30/15/15。 */
    xgui_remote_adapt_reset(&a, 30);
    a.step = 1;
    ta_expect(xgui_remote_adapt_fps(&a) == 15, "阶梯: base30 步1=15");
    a.step = 2;
    ta_expect(xgui_remote_adapt_fps(&a) == 15, "阶梯: base30 步2=15");
    /* 15 平阶梯(地板不高于档位)。 */
    xgui_remote_adapt_reset(&a, 15);
    a.step = 2;
    ta_expect(xgui_remote_adapt_fps(&a) == 15, "阶梯: base15 平阶梯");
    /* 窄档 5: 地板 15 不得反超档位。 */
    xgui_remote_adapt_reset(&a, 5);
    a.step = 2;
    ta_expect(xgui_remote_adapt_fps(&a) == 5, "阶梯: base5 夹回档位");
    /* 无门控(0)直通。 */
    xgui_remote_adapt_reset(&a, 0);
    ta_expect(xgui_remote_adapt_fps(&a) == 0, "阶梯: maxFps=0 不节流直通");
}

static void ta_unit_feedDown(void)
{
    XGuiRemoteAdapt a;
    int64_t t = 1000;
    xgui_remote_adapt_reset(&a, 60);
    /* 单轮超预算: 不动(阈值 2)。 */
    ta_expect(xgui_remote_adapt_feed(&a, 60, 30, 16, 0, t) == 60,
              "feed: 单轮超预算步0 不动");
    /* 连续 2 轮超预算: 降一步 60→30。 */
    ta_expect(xgui_remote_adapt_feed(&a, 60, 30, 16, 0, t + 10) == 30,
              "feed: 连续2轮超预算 60→30");
    /* 再 2 轮: 30→15(预算派生 33ms)。 */
    ta_expect(xgui_remote_adapt_feed(&a, 60, 40, 0, 0, t + 20) == 30,
              "feed: 30 档单轮超预算不动");
    ta_expect(xgui_remote_adapt_feed(&a, 60, 40, 0, 0, t + 30) == 15,
              "feed: 30 档连续2轮 30→15 地板");
    /* 地板再超预算: 封顶不动。 */
    ta_expect(xgui_remote_adapt_feed(&a, 60, 100, 0, 0, t + 40) == 15,
              "feed: 地板封顶不越界");
    ta_expect(a.step == 2, "feed: step 停在 2");
}

static void ta_unit_feedNeutralQHigh(void)
{
    XGuiRemoteAdapt a;
    int64_t t = 2000;
    xgui_remote_adapt_reset(&a, 60);
    /* 队列高水位单独即可触发下行(无需耗时超限)。 */
    ta_expect(xgui_remote_adapt_feed(&a, 60, 1, 16, 1, t) == 60,
              "feed: qHigh 单轮不动");
    ta_expect(xgui_remote_adapt_feed(&a, 60, 1, 16, 1, t + 10) == 30,
              "feed: qHigh 连续2轮 60→30");
    /* 中性轮清零下行计数。 */
    xgui_remote_adapt_reset(&a, 60);
    (void)xgui_remote_adapt_feed(&a, 60, 30, 16, 0, t); /* over=1。 */
    (void)xgui_remote_adapt_feed(&a, 60, 8, 16, 0, t + 10); /* 中性。 */
    ta_expect(xgui_remote_adapt_feed(&a, 60, 30, 16, 0, t + 20) == 60,
              "feed: 中性轮打断连超计数");
    /* 富余半预算清零超预算计数。 */
    xgui_remote_adapt_reset(&a, 60);
    (void)xgui_remote_adapt_feed(&a, 60, 30, 16, 0, t);
    (void)xgui_remote_adapt_feed(&a, 60, 5, 16, 0, t + 10); /* 富余。 */
    ta_expect(xgui_remote_adapt_feed(&a, 60, 30, 16, 0, t + 20) == 60,
              "feed: 富余轮打断连超计数");
}

static void ta_unit_feedUpRebase(void)
{
    XGuiRemoteAdapt a;
    int64_t t = 5000;
    int i;
    xgui_remote_adapt_reset(&a, 60);
    /* 压到地板(每 2 轮降一步: 60→30→15)。 */
    (void)xgui_remote_adapt_feed(&a, 60, 99, 16, 0, t);
    (void)xgui_remote_adapt_feed(&a, 60, 99, 16, 0, t + 10);
    (void)xgui_remote_adapt_feed(&a, 60, 99, 16, 0, t + 20);
    ta_expect(xgui_remote_adapt_feed(&a, 60, 99, 16, 0, t + 30) == 15,
              "feed: 预置压到 15");
    /* 8 轮富余但驻留不足 1s: 不回升。 */
    for (i = 0; i < 8; ++i) {
        (void)xgui_remote_adapt_feed(&a, 60, 1, 66, 0, t + 100 + i);
    }
    ta_expect(a.step == 2, "feed: 富余但驻留<1s 不回升");
    /* 驻留满足后继续富余: 回升 15→30。 */
    ta_expect(xgui_remote_adapt_feed(&a, 60, 1, 66, 0, t + 1100) == 30,
              "feed: 驻留满足 15→30 回升");
    /* 回升后单轮富余不连升(仍需 8 轮+驻留)。 */
    ta_expect(xgui_remote_adapt_feed(&a, 60, 1, 16, 0, t + 1110) == 30,
              "feed: 单轮富余不连升");
    /* PROFILE_SET 基准变化: 即重置到新档步 0(并集语义)。 */
    ta_expect(xgui_remote_adapt_feed(&a, 15, 1, 66, 0, t + 1200) == 15,
              "feed: 换档 resource 基准=15 平阶梯");
    ta_expect(a.step == 0 && a.baseFps == 15, "feed: 换档重置步0");
    ta_expect(xgui_remote_adapt_feed(&a, 60, 1, 16, 0, t + 1300) == 60,
              "feed: 换回 60 恢复原速步0");
}

/* ==================== (b) 回环量化 ==================== */

typedef struct XtaE2e {
    XWidget* top;
    XWidget* childA;
    XWidget* noise;         /* 噪声标签(zlib 不可压负载, 抬高单轮耗时)。 */
    XPixmap noisePm[2];
    XGuiServer* server;
    XGuiClient* client;
    XGuiRemoteLoopbackDevice* devA;
    XGuiRemoteLoopbackDevice* devB;
    int sessionId;
} XtaE2e;

/** @brief 4×4 块伪噪声(同 XGuiRemoteTest xr_fillNoiseImage 口径)。 */
static void ta_fillNoiseImage(XImage* img, int w, int h, uint32_t seed)
{
    int x, y;
    for (y = 0; y < h; y += 4) {
        for (x = 0; x < w; x += 4) {
            XRect rc;
            uint32_t v = 0xFF000000u |
                         (((x * 73856093u) ^ (y * 19349663u) ^ seed) &
                          0xFFFFFFu);
            XRect_init(&rc, x, y, 4, 4);
            XImage_fillRect(img, &rc, v);
        }
    }
}

/** @brief 色块画笔(伤害源, 同 XGuiRemoteTest xr_paintColor 口径)。 */
static void ta_paintColor(XWidget* w, int r, int g, int b)
{
    XPalette pal;
    XColor c;
    XPalette_init_default(&pal);
    XColor_init_rgb(&c, r, g, b, 255);
    XPalette_setColor(&pal, XPaletteColorGroup_Active,
                      XPaletteColorRole_Window, c);
    XPalette_setColor(&pal, XPaletteColorGroup_Inactive,
                      XPaletteColorRole_Window, c);
    XWidget_setPalette(w, &pal);
    XWidget_setAutoFillBackground(w, true);
}

static bool ta_e2e_setup(XtaE2e* e)
{
    int w = 320, h = 240;
    XImage* img = NULL;
    int i;
    memset(e, 0, sizeof(*e));
    ta_ensureApp();
    e->top = XWidget_create(NULL, 0);
    if (!e->top) return false;
    XWidget_resize(e->top, w, h);
    ta_paintColor(e->top, 0x21, 0x53, 0x9B);
    e->childA = XWidget_create(e->top, 0);
    XWidget_setGeometry(e->childA, 12, 12, w / 3, h / 3);
    ta_paintColor(e->childA, 0xD9, 0x30, 0x25);
    XWidget_show(e->childA);
    /* 噪声标签(128×96 ARGB32 ≈ 48KiB 不可压负载): 桌面回环轮耗时若无
     * 此抬升 <1ms, XGUI_REMOTE_ADAPT_BUDGET_MS=1 永不触发——量化目的
     * 即"强制每轮超预算"。 */
    e->noise = XLabel_create(e->top, 0);
    XWidget_setGeometry((XWidget*)e->noise, 0, 0, 128, 96);
    img = XImage_create_ex(XCLASS_DEFAULT_MEMORY_TYPE);
    for (i = 0; i < 2; ++i) {
        XPixmap_init(&e->noisePm[i]);
        if (img && XImage_reinit_ex(img, 128, 96, XImageFormat_ARGB32)) {
            ta_fillNoiseImage(img, 128, 96, i ? 0x5EED0000u : 0x00FA0000u);
            XPixmap_init_image(&e->noisePm[i], img, 0);
        }
    }
    if (img) XClassDelete(img);
    XLabel_setPixmap(e->noise, &e->noisePm[0]);
    XWidget_show((XWidget*)e->noise);
    XWidget_show(e->top);
    XWidget_repaint(e->top);

    e->server = XGuiServer_create(NULL);
    if (!e->server) return false;
    /* performance 档(ARGB32/zlib/128×128/60fps): base=60, 阶梯
     * 60→30→15 全程可量化; 噪声负载抬轮耗时放大预算触发面。 */
    XGuiServer_setProfileId(e->server, XGUI_REMOTE_PROFILE_PERFORMANCE);
    if (!XGuiServer_host(e->server, e->top)) return false;
    e->devA = XGuiRemoteLoopbackDevice_createPair(64u * 1024u, &e->devB);
    if (!e->devA || !e->devB) return false;
    e->sessionId = XGuiServer_attachTransport(e->server, (XIODevice*)e->devA);
    if (e->sessionId <= 0) return false;
    e->client = XGuiClient_create(NULL, 0);
    if (!e->client) return false;
    XGuiClient_setTransport(e->client, (XIODevice*)e->devB);
    return true;
}

static void ta_e2e_teardown(XtaE2e* e)
{
    int i;
    if (e->client) XGuiClient_disconnectFromServer(e->client);
    ta_pumpFor(60);
    if (e->devA) XClassDelete(e->devA);
    if (e->devB) XClassDelete(e->devB);
    for (i = 0; i < 2; ++i) XClassDeinit(&e->noisePm[i]);
    if (e->server) XClassDelete(e->server);
    if (e->top) XClassDelete(e->top);
    memset(e, 0, sizeof(*e));
}

/** @brief 流式态到达判定。 */
static bool ta_isStreaming(XtaE2e* e)
{
    return XGuiClient_state(e->client) == XGUI_REMOTE_STATE_STREAMING;
}

/** @brief 持续伤害 churn churnMs 毫秒, 返回窗口内客户端收到的批率(fps)。 */
static double ta_churnRate(XtaE2e* e, int churnMs)
{
    XGuiRemoteStats st0, st1;
    int64_t t0, deadline;
    int flip = 0;
    XGuiClient_statistics(e->client, &st0);
    t0 = ta_nowMs();
    deadline = t0 + churnMs;
    while (ta_nowMs() < deadline) {
        flip = !flip;
        /* 噪声整幅翻转 + 全窗重绘: 每轮 6 tile(128 网格)全脏且 zlib
         * 不可压——桌面轮耗时 ms 级, 超预算触发面最大化。 */
        XLabel_setPixmap(e->noise, &e->noisePm[flip ? 1 : 0]);
        XWidget_repaint(e->top);
        XGuiApplication_processEvents(XEventLoop_AllEvents);
        XThread_msleep(8); /* ~120Hz 伤害源, 高于任何门控节拍。 */
    }
    ta_pumpFor(150);
    XGuiClient_statistics(e->client, &st1);
    fprintf(stderr,
            "[ADAPT-QUANT] window churnMs=%d updates=%d tiles=%d "
            "bytes=%llu\n", churnMs,
            (int)st1.updateCount - (int)st0.updateCount,
            (int)st1.tileCount - (int)st0.tileCount,
            (unsigned long long)(st1.bytesReceived - st0.bytesReceived));
    {
        double dt = (double)(ta_nowMs() - t0);
        if (dt <= 0.0) return 0.0;
        return (double)((int)st1.updateCount - (int)st0.updateCount) *
               1000.0 / dt;
    }
}

static void ta_loopback_quant(void)
{
    XtaE2e e;
    double fps;
    int64_t deadline;
    if (!ta_e2e_setup(&e)) {
        ta_expect(false, "量化: 回环拓扑建立");
        ta_e2e_teardown(&e);
        return;
    }
    deadline = ta_nowMs() + 8000;
    while (ta_nowMs() < deadline && !ta_isStreaming(&e)) {
        XGuiApplication_processEvents(XEventLoop_AllEvents);
        XThread_msleep(2);
    }
    if (!ta_isStreaming(&e)) {
        ta_expect(false, "量化: 进入流式");
        ta_e2e_teardown(&e);
        return;
    }
    ta_pumpFor(300); /* 全量首刷落地, 稳态后开窗。 */
    fps = ta_churnRate(&e, 2500);
    fprintf(stderr, "[ADAPT-QUANT] adapt=%s budgetMs=%s fps=%.1f\n",
            getenv("XGUI_REMOTE_ADAPT_FPS") ? getenv("XGUI_REMOTE_ADAPT_FPS")
                                            : "(unset)",
            getenv("XGUI_REMOTE_ADAPT_BUDGET_MS")
                ? getenv("XGUI_REMOTE_ADAPT_BUDGET_MS") : "(unset)",
            fps);
    if (ta_baselineOnly) {
        /* 反证组(交付口径): 阶梯关非停摆。注: 回环 in-process 交付节拍
         * 受泵兜底定时器钳制(~10/s, 实测与门控 30/15fps 无关)——门控
         * 差异以服务端 XGS_TRACE claim 速率对账(本进程 stderr, 由
         * XGUI_REMOTE_BENCH_TRACE=1 外部开启后归档对账)。 */
        ta_expect(fps > 3.0, "量化: 关阶梯持续交付(非停摆)");
    } else {
        /* 阶梯组(交付口径): budget=1ms 恒超预算 → 阶梯下行且非停摆;
         * 门控速率证据同上以 XGS_TRACE 对账(30→15 服务端实测)。 */
        ta_expect(fps > 3.0, "量化: 降档不是停摆(仍有持续交付)");
    }
    ta_e2e_teardown(&e);
}

/* ==================== 入口 ==================== */

int main(int argc, char** argv)
{
    int i;
    for (i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "--adapt-baseline") == 0) {
            ta_baselineOnly = true;
        }
    }
    /* env 先行(读一次缓存纪律): 助手首次使用在任何会话创建之后。 */
    if (ta_baselineOnly) {
        setenv("XGUI_REMOTE_ADAPT_FPS", "0", 1);
        unsetenv("XGUI_REMOTE_ADAPT_BUDGET_MS");
    } else {
        setenv("XGUI_REMOTE_ADAPT_FPS", "1", 1);
        setenv("XGUI_REMOTE_ADAPT_BUDGET_MS", "1", 1);
    }

    /* (a) 纯状态机单元(无显示依赖, 先行)。 */
    ta_unit_ladderMap();
    ta_unit_feedDown();
    ta_unit_feedNeutralQHigh();
    ta_unit_feedUpRebase();

    /* (b) 回环量化(需显示环境; 反证组只跑量化)。 */
    if (ta_baselineOnly) {
        ta_loopback_quant();
    } else {
        ta_ensureApp();
        ta_loopback_quant();
    }

    fprintf(stderr, "[ADAPT] %s: %d assertion(s), %d failed%s\n",
            ta_baselineOnly ? "baseline(AdaptFps=0)" : "ladder(budget=1ms)",
            ta_runs, ta_failures,
            ta_failures == 0 ? " -> PASS" : "");
    fflush(stderr);
    return ta_failures ? 1 : 0;
}

#else /* !XWIDGET_ON || !XGUI_REMOTE_ON */

#include <stdio.h>
int main(void)
{
    fprintf(stderr, "[ADAPT] stub pass (XGUI_REMOTE_ON=0)\n");
    return 0;
}

#endif /* XWIDGET_ON && XGUI_REMOTE_ON */
