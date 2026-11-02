/******************************************************************************
 * @file       XPlatformPowerTest.c
 * @brief      平台电源契约（熄屏/唤醒/外部唤醒收敛）的框架层回归测试。
 * @details    本测试**白盒编译框架实现**：把 Src/XGui/Platform/
 *             XPlatformPower.c 直接编入本目标，并配一组桩驱动（替掉
 *             平台层的 6 个 Driver_ 钩子），从而在宿主（无屏、无 X11）
 *             上精确驱动「影子态 ↔ 硬件实况」的状态机。
 *
 *             覆盖的断言（逐条对应框架契约）：
 *               T1 空闲超时 ⇒ 驱动收到熄屏（Off）指令；
 *               T2 **核心锁**：熄屏后硬件被**外部点亮**（VNC/x11vnc、
 *                  `xset dpms force on`、面板电源管理；不经应用输入
 *                  通道）⇒ 影子必须跟着硬件归位为「亮」，且**之后能
 *                  再次正常熄屏**。
 *                  旧口径对「影子说熄、硬件说亮」刻意不动作 ⇒ 影子永久
 *                  卡在「熄」⇒ 自动熄屏分支被 !m_screenOff 永久门死
 *                  ⇒ 应用之后再也无法休眠（板端实证）。本断言在旧码上
 *                  必红，是该缺陷的回归锁；
 *               T3 唤醒来源掩码：未被允许的输入不唤醒，允许的输入唤醒；
 *               T4 自愈方向：影子说亮、硬件已黑 ⇒ 点亮自愈；
 *               T5 读不回来（Unknown）⇒ 什么都不做（不得把「读不到」
 *                  当「熄屏」误点亮）。
 *
 *             桩驱动同时记录驱动收到的动作次数（Off/On），用于断言
 *             「平台侧动作被真实下发」。
 * @note       本测试自带 main()，是独立可执行目标，不得卷入主程序
 *             （见 Test/CMakeLists.txt 的 REMOVE_ITEM 纪律）。
 * @author     XinYueC 团队
 ******************************************************************************/
#include "XPlatformPower.h"

#include "XThread.h"
#include "XPrintf.h"
#include "XObject.h"
#include "XScreen.h"
#include "XGuiApplication.h"

#include <stdio.h>
#include <string.h>

/* ==================== 桩驱动（替掉平台层 6 个钩子） ==================== */

/** @brief 桩的硬件实况（框架读回钩子的返回值）。 */
static XPlatformPowerScreenActual s_stubActual =
    XPlatformPowerScreenActual_On;
/** @brief 桩收到的熄屏（Off）动作次数。 */
static int s_blankCalls = 0;
/** @brief 桩收到的点亮（On）动作次数。 */
static int s_onCalls = 0;

unsigned XPlatformPowerDriver_capabilities(void)
{
    return (unsigned)XPlatformPowerCap_ScreenControl;
}

bool XPlatformPowerDriver_setScreenState(XPlatformPowerScreenState state)
{
    if (state == XPlatformPowerScreenState_Off) {
        ++s_blankCalls;
        s_stubActual = XPlatformPowerScreenActual_Off;
    } else {
        ++s_onCalls;
        s_stubActual = XPlatformPowerScreenActual_On;
    }
    return true;
}

XPlatformPowerScreenActual XPlatformPowerDriver_queryScreenState(void)
{
    return s_stubActual;
}

bool XPlatformPowerDriver_setSystemState(XPlatformPowerSystemState state)
{
    (void)state;
    return false;
}

bool XPlatformPowerDriver_setScreenTimeout(int64_t millis)
{
    (void)millis;
    return false;
}

bool XPlatformPowerDriver_requestScreenTimeoutPermission(void)
{
    return false;
}

/* ==================== 断言小工具 ==================== */

static int s_failures = 0;

#define XPOW_ASSERT(cond, msg)                                            \
    do {                                                                  \
        if (cond) {                                                       \
            XPrintf("  [PASS] %s\n", (msg));                              \
        } else {                                                          \
            XPrintf("  [FAIL] %s\n", (msg));                              \
            ++s_failures;                                                 \
        }                                                                 \
    } while (0)

/** @brief 设定策略（屏幕超时 + 唤醒来源），其余字段保持影子值。 */
static void xpow_setPolicy(int64_t screenOffMs, unsigned wakeInputs)
{
    XPlatformPowerPolicy policy;
    XPlatformPower_policy(&policy);
    policy.m_screenOffMs = screenOffMs;
    policy.m_systemSuspendMs = 0;
    policy.m_wakeInputs = wakeInputs;
    XPlatformPower_setPolicy(&policy);
}

/* ==================== 用例 ==================== */

/** @brief T1：空闲超时 ⇒ 熄屏。 */
static void testIdleBlank(void)
{
    int blankBefore;
    XPrintf("T1 空闲超时熄屏\n");
    s_stubActual = XPlatformPowerScreenActual_On;
    xpow_setPolicy(150, (unsigned)XPlatformPowerInputAll);
    XPlatformPower_tick();              /* 建立空闲基线 */
    blankBefore = s_blankCalls;
    XThread_msleep(300);
    XPlatformPower_tick();              /* 空闲超时 ⇒ 熄屏 */
    XPOW_ASSERT(s_blankCalls > blankBefore,
                "空闲超时必须向驱动下发熄屏指令");
    XPOW_ASSERT(XPlatformPower_isScreenOff(), "熄屏后影子态必须是「熄」");
}

/** @brief T2（核心锁）：外部唤醒 ⇒ 影子归位为亮，且之后能再次熄屏。 */
static void testExternalWake(void)
{
    int blankBefore;
    int onBefore;
    XPrintf("T2 外部唤醒后影子归位 + 仍可再次熄屏\n");
    /* 前提：处于熄屏态（T1 已建立）。 */
    XPOW_ASSERT(XPlatformPower_isScreenOff(), "前提：影子应为「熄」");

    /* 外部把屏点亮：硬件实况变亮，但**不经应用输入通道**（模拟
       VNC/x11vnc 或 xset dpms force on 直接改服务端状态）。 */
    s_stubActual = XPlatformPowerScreenActual_On;
    onBefore = s_onCalls;
    XPlatformPower_tick();

    XPOW_ASSERT(!XPlatformPower_isScreenOff(),
                "硬件已亮时影子必须跟着归位为「亮」（不得永久卡在「熄」）");
    XPOW_ASSERT(s_onCalls == onBefore,
                "硬件本已亮，不应再下发多余的点亮指令");

    /* 关键后果：之后必须能再次正常熄屏（旧码此处永不再熄）。 */
    blankBefore = s_blankCalls;
    XThread_msleep(300);
    XPlatformPower_tick();
    XPOW_ASSERT(s_blankCalls > blankBefore,
                "外部唤醒之后应用必须能再次正常熄屏（旧码此处必红）");
}

/** @brief T3：唤醒来源掩码。 */
static void testWakeInputMask(void)
{
    int onBefore;
    XPrintf("T3 唤醒来源掩码\n");
    xpow_setPolicy(0, (unsigned)XPlatformPowerInputTouch);
    XPlatformPower_setScreenState(XPlatformPowerScreenState_Off);
    XPOW_ASSERT(XPlatformPower_isScreenOff(), "前提：影子应为「熄」");
    onBefore = s_onCalls;
    /* 未被允许的输入源：只记活动，不唤醒。 */
    XPlatformPower_noteActivity((unsigned)XPlatformPowerInputMouse);
    XPOW_ASSERT(s_onCalls == onBefore,
                "未被允许的输入源不得唤醒屏幕");
    XPOW_ASSERT(XPlatformPower_isScreenOff(),
                "未被允许的输入源之后影子仍应为「熄」");
    /* 被允许的输入源：唤醒。 */
    XPlatformPower_noteActivity((unsigned)XPlatformPowerInputTouch);
    XPOW_ASSERT(s_onCalls == onBefore + 1,
                "被允许的输入源必须唤醒屏幕");
    XPOW_ASSERT(!XPlatformPower_isScreenOff(), "唤醒后影子应为「亮」");
}

/** @brief T4：自愈方向（影子说亮、硬件已黑 ⇒ 点亮自愈）。 */
static void testSelfHeal(void)
{
    int onBefore;
    XPrintf("T4 硬件已黑时的点亮自愈\n");
    xpow_setPolicy(0, (unsigned)XPlatformPowerInputAll);
    XPlatformPower_setScreenState(XPlatformPowerScreenState_On);
    /* 外部把屏写黑（前任遗留/验证脚本/面板电源管理）。 */
    s_stubActual = XPlatformPowerScreenActual_Off;
    onBefore = s_onCalls;
    XPlatformPower_tick();
    XPOW_ASSERT(s_onCalls == onBefore + 1,
                "影子说亮而硬件已黑时必须点亮自愈");
    XPOW_ASSERT(!XPlatformPower_isScreenOff(), "自愈后影子应为「亮」");
}

/** @brief T5：读不回来（Unknown）⇒ 什么都不做。 */
static void testUnknownNoAction(void)
{
    int onBefore = s_onCalls;
    int blankBefore = s_blankCalls;
    XPrintf("T5 读不回来时不做任何动作\n");
    s_stubActual = XPlatformPowerScreenActual_Unknown;
    XPlatformPower_tick();
    XPOW_ASSERT(s_onCalls == onBefore && s_blankCalls == blankBefore,
                "平台读不回来时不得误点亮、也不得误熄屏");
    s_stubActual = XPlatformPowerScreenActual_On;
}

/* ==================== T6：上报 → 框架事件 → 屏幕信号 → 订阅者 ==================== */

/** @brief 屏幕信号订阅者（模拟任意 XObject 自主监听）。 */
static int s_signalHits = 0;

static void t6_screenPowerSlot(XObject* sender, XVarList* args)
{
    (void)sender;
    (void)args;
    ++s_signalHits;
}

/** @brief T6：平台上报入口 ⇒ 框架事件 ⇒ 屏幕对象信号 ⇒ 订阅者被唤醒。
 *  @details 这条链路是「平台推送 → 事件框架 → 框架事件 → 任意 XObject
 *           自主监听」的完整闭环（用户指定的形态）：平台层调
 *           XPlatformPower_reportScreenStateChanged → 公共层改影子态
 *           并把跃迁转成 XEVENT_TYPE_SCREEN_POWER_STATE_CHANGED 投递
 *           事件队列 → 应用层 notify 转成屏幕对象的 powerStateChanged
 *           信号 → 订阅者的槽被兑现。 */
static void testEventToSignal(void)
{
    XGuiApplication* app;
    XScreen* screen;
    XPrintf("T6 平台上报 → 框架事件 → 屏幕信号 → 订阅者\n");
    /* 前提：影子为「熄」，这样上报「硬件已亮」才构成状态跃迁（有事件）。 */
    XPlatformPower_setScreenState(XPlatformPowerScreenState_Off);
    XPOW_ASSERT(XPlatformPower_isScreenOff(), "前提：影子应为「熄」");

    app = XGuiApplication_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, 0, NULL);
    if (!app) {
        XPOW_ASSERT(false, "应用实例创建失败");
        return;
    }
    /* 泵一轮：让平台集成完成屏幕登记（平台后端在应用起来后建屏）。 */
    XGuiApplication_processEvents(XEventLoop_AllEvents);
    screen = XGuiApplication_primaryScreen();
    if (!screen) {
        /* 平台未建屏（无原生窗口时）⇒ 自建一块并登记为主屏。 */
        screen = XScreen_create();
        if (screen) {
            XGuiApplication_screenAdded(screen);
            XGuiApplication_setPrimaryScreen(screen);
            XGuiApplication_processEvents(XEventLoop_AllEvents);
            screen = XGuiApplication_primaryScreen();
        }
    }
    XPOW_ASSERT(screen != NULL, "应用应有一块主屏幕");
    if (!screen) return;
    /* 订阅：**应用实际的主屏幕**（应用层转发时发的也是它）。 */
    {
        XConnection* conn = XObject_connect_2(
            (XObject*)screen, XSignal(XScreen_powerStateChanged_signal),
            t6_screenPowerSlot);
        XPOW_ASSERT(conn != NULL, "订阅连接应建立");
    }
    s_signalHits = 0;

    /* 平台上报：硬件已亮（外部唤醒）。 */
    s_stubActual = XPlatformPowerScreenActual_On;
    XPlatformPower_reportScreenStateChanged(XPlatformPowerScreenActual_On);
    XPOW_ASSERT(!XPlatformPower_isScreenOff(),
                "平台上报「硬件已亮」后影子必须归位为「亮」");
    /* 事件是投递语义：泵一轮事件队列才会送到订阅者。 */
    {
        int i;
        for (i = 0; i < 3 && s_signalHits == 0; ++i)
            XGuiApplication_processEvents(XEventLoop_AllEvents);
    }
    XPOW_ASSERT(s_signalHits == 1,
                "屏幕 powerStateChanged 信号必须送达订阅者（事件→信号链路）");
}


int main(void)
{
    XPrintf("=== XPlatformPowerTest（框架电源契约回归）===\n");
    testIdleBlank();
    testExternalWake();
    testWakeInputMask();
    testSelfHeal();
    testUnknownNoAction();
    testEventToSignal();
    if (s_failures == 0) {
        XPrintf("=== 全部通过 ===\n");
        return 0;
    }
    XPrintf("=== 失败断言 %d 条 ===\n", s_failures);
    return 1;
}
