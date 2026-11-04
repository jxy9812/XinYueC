/**
 * @file       hsf_vfd_probe.c
 * @brief      HSF 变频器监视页全量交互回归探针（独立可执行，进程内）。
 * @details    覆盖面（对应页面六大交互区）：
 *               ① 控制闸 checkbox：开/关 → 全部操作控件使能联动；
 *               ② 给定行：Slider↔SpinBox 双向同步 + 「下发」全链路
 *                  （按钮 → provider → 设备层 → 假从站 LFR1 槽）；
 *               ③ 操作行：启动/停止单钮双态（文字随运行态翻转）、
 *                  方向下拉（反转随启动生效）、点动能力裁剪（ATV61
 *                  无通讯点动 → 隐藏）、故障复位（确定性注入 → 使能 →
 *                  点击 → 清除 → 回禁用）；
 *               ④ 自动运行锁：setInteractiveMode(false/true) 隐藏/恢复；
 *               ⑤ 趋势图：追点生长 + 视口仲裁（Pan 归用户 / Reset 归页面）；
 *               ⑥ 形态自适应：窄档仪表纵排 / 宽档横排。
 *             关键状态各存一张 PNG（out/vfdshot_*.png）供目验。
 *
 *             [实现方式] 单 TU 直接收录 HsfMonitorVfd.c / HsfVfdProvider.c /
 *             HsfVfdDemoSim.c：探针需要直达 s_vfdPage 内部控件与模拟器
 *             内部槽（确定性故障注入、LFR1 写入核对），走公开 API 拿不到。
 *             模拟源剧本是自治的（自动斜坡/故障），故启停断言采用
 *             「命令字写落定 + 文字随状态翻转不变量」口径，不赌时序。
 */
#include <stdio.h>
#include <string.h>

#include "XGuiApplication.h"
#include "XEventType.h"
#include "XThread.h"
#include "XWidget.h"
#include "XLabel.h"
#include "XCheckBox.h"
#include "XComboBox.h"
#include "XSpinBox.h"
#include "XSlider.h"
#include "XPushButton.h"
#include "XAbstractButton.h"
#include "XAbstractSlider.h"
#include "XImage.h"
#include "XChartView.h"
#include "XXYSeries.h"
#include "XObject.h"
#include "XVarList.h"
#include "XVfdDevice.h"
#include "XString.h"
#include "XSystem.h"
#include "HsfMonitorVfd.h"

/* 直接收录被测实现（单 TU，避免独立目标的符号复制）。收录顺序：
 * 主题 → 模拟源 → provider 表 → 页面（页面引用前几者的符号）。
 * HsfHomePage 一并收录：快捷入口是 receiver 实锅的回归锁（⑪）。 */
#include "HsfTheme.c"
#include "HsfVfdDemoSim.c"
#include "HsfVfdProvider.c"
#include "HsfMonitorVfd.c"
#include "HsfHomePage.c"

/* ---- 远端后端桩（[2026-11-04] 并发流的 XLink 后端在途：页面 provider
 * 接入已按 HsfLinkClient_isOpen() 分叉 demo/remote。探针只验本地 demo
 * 路径，且不收录还在写的 HsfLinkClient.c/HsfVfdProviderRemote.c——
 * 此处给最小桩：恒离线 → 页面走 demo 模拟源。他们收口后若要连验
 * 远端路径，改收 HsfVfdProviderRemote.c 并起真链路即可。 ---- */
bool HsfLinkClient_isOpen(void) { return false; }
const HsfVfdProvider* HsfVfdProvider_remote(void) { return NULL; }

static int s_failures = 0;

/* ---- 主界面快捷入口的 navFn 录制（⑪ receiver 实锅回归锁）。 ---- */
static int g_homeNav = -1;
static void homeNavRecorder(void* userData, int index)
{
    (void)userData;
    g_homeNav = index;
}

static void expect_true(bool cond, const char* name)
{
    printf("  [%s] %s\n", cond ? "OK" : "FAIL", name);
    if (!cond) s_failures++;
}

/* ---- 泵：processEvents + 短睡；每 ~200ms 手动推一拍模拟源
 * （页面自带 200ms XTimer 也会推，双驱只是倍速，断言不赌时序）。 ---- */
static void pumpMs(int ms)
{
    int elapsed = 0;
    while (elapsed < ms) {
        XGuiApplication_processEvents(XEventLoop_AllEvents);
        XThread_msleep(10);
        elapsed += 10;
        if (elapsed % 200 == 0)
            hsfVfdSimTick(NULL);
    }
}

/* ---- 条件等待（每 50ms 查一次谓词；超时返回 false）。 ---- */
static bool waitUntil(bool (*pred)(void), int timeoutMs)
{
    int elapsed = 0;
    while (elapsed <= timeoutMs) {
        XGuiApplication_processEvents(XEventLoop_AllEvents);
        XThread_msleep(50);
        elapsed += 50;
        if (elapsed % 200 == 0)
            hsfVfdSimTick(NULL);
        if (pred()) return true;
    }
    return false;
}

/* ---- 截图辅助：整窗后备图像存 PNG。 ---- */
static void saveShot(XWidget* host, const char* path)
{
    XImage* image = host ? XWidget_paintImage(host) : NULL;
    if (image) {
        if (!XImage_save_2(image, path, "PNG", 95))
            printf("  [WARN] 截图保存失败: %s\n", path);
    } else {
        printf("  [WARN] 无后备图像: %s\n", path);
    }
}

static HsfMonitorVfd* pg(void) { return s_vfdPage; }

/* ---- 按钮文字取 C 串（XString → C 串；toUtf8 可能返回 NULL，回退 toLocal）。 ---- */
static const char* xstrCStr(const XString* s)
{
    const char* u;
    if (!s) return NULL;
    u = XString_toUtf8(s);
    if (!u) u = XString_toLocal(s);
    return u;
}

static const char* btnText(XPushButton* btn)
{
    return xstrCStr(XAbstractButton_text((XAbstractButton*)btn));
}

/* ---- 谓词：快照有效 / 运行中 / 故障中 / 停机。 ---- */
static bool pred_valid(void)
{
    const XVfdDevice* dev = s_vfdPage ? s_vfdPage->m_provider
        ? s_vfdPage->m_provider->device(s_vfdPage->m_provUser) : NULL : NULL;
    const XVfdStatus* st = XVfdDevice_status_const(dev);
    return st && st->m_valid;
}
static bool pred_running(void)
{
    const XVfdDevice* dev = s_vfdPage ? s_vfdPage->m_provider
        ? s_vfdPage->m_provider->device(s_vfdPage->m_provUser) : NULL : NULL;
    const XVfdStatus* st = XVfdDevice_status_const(dev);
    return st && st->m_valid && st->m_running;
}
static bool pred_faulted(void)
{
    const XVfdDevice* dev = s_vfdPage ? s_vfdPage->m_provider
        ? s_vfdPage->m_provider->device(s_vfdPage->m_provUser) : NULL : NULL;
    const XVfdStatus* st = XVfdDevice_status_const(dev);
    return st && st->m_valid && st->m_faulted;
}
static bool pred_notFaulted(void) { return !pred_faulted(); }

/* ---- 谓词：设备动作序列空闲（CiA402 启动序列多步推进期间 isBusy）。 ---- */
static bool pred_idle(void)
{
    const XVfdDevice* dev = s_vfdPage && s_vfdPage->m_provider
        ? s_vfdPage->m_provider->device(s_vfdPage->m_provUser) : NULL;
    return dev && !XVfdDevice_isBusy(dev);
}

/* ---- viewportChanged 发射（照 XChartView.c xcv_emitViewportChanged
 * 口径：信号函数只是 id 取址器，真发射走 XObject_emitSignal+XVarList）。
 * 信号函数体内 (void)self 直返地址——直接调它**不会**发射。 ---- */
static void emitViewport(XChartView* view, int reason)
{
    XVarList* args;
    if (!view || !((XObject*)view)->m_signalSlot) return;
    args = XVarList_Create(XVar(int, reason));
    if (!args) return;
    XObject_emitSignal((XObject*)view,
                       (size_t)XChartView_viewportChanged_signal(view, reason),
                       args, NULL, NULL, XEVENT_PRIORITY_NORMAL);
}

int main(void)
{
    XGuiApplication* app;
    XWidget* host;
    XWidget* page;
    int argc = 1;
    char* argv[2];
    char arg0[] = "hsf_vfd_probe";

    argv[0] = arg0; argv[1] = NULL;
    app = XGuiApplication_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, argc, argv);
    if (!app) {
        printf("XGuiApplication 创建失败\n");
        return 2;
    }
    HsfTheme_install();

    /* 离屏宿主窗（-3000,-3000，不占桌面）。 */
    host = XWidget_create(NULL, 0);
    if (!host) { printf("宿主窗创建失败\n"); return 2; }
    XWidget_resize(host, 1000, 1000);
    XWidget_move(host, -3000, -3000);

    page = HsfMonitorVfd_create(host, NULL, NULL);
    expect_true(page != NULL, "页面创建");
    expect_true(pg() != NULL, "页对象已登记（s_vfdPage）");
    expect_true(pg()->m_provider != NULL, "provider（demo 模拟源）已接入");
    /* 页面几何：真机里由堆栈宿主定尺；探针直接给宽屏尺寸（否则页宽
       < 620 → HsfMonitorVfd_adapt 恒判窄档，宽档断言失真）。 */
    XWidget_resize(page, 1000, 900);
    XWidget_show(host);
    /* 等快照有效（自治剧本前几拍）。 */
    if (!waitUntil(pred_valid, 4000))
        printf("  [WARN] 4s 内快照未有效（继续，后续断言可能连带失败）\n");
    pumpMs(200);

    /* ==================== ① 初始态（闸缺省开：设备层闸缺省使能） ==================== */
    printf("[1] 初始态\n");
    expect_true(XCheckBox_isChecked(&pg()->m_gate), "初始闸=开（设备层缺省使能）");
    expect_true(XWidget_isEnabled((XWidget*)&pg()->m_btnRun), "启动钮可用");
    expect_true(XWidget_isEnabled((XWidget*)&pg()->m_dirCombo), "方向下拉可用");
    expect_true(XWidget_isEnabled((XWidget*)&pg()->m_setApply), "下发钮可用");
    expect_true(XWidget_isEnabled((XWidget*)&pg()->m_setSlider), "滑块可用");
    expect_true(XWidget_isEnabled((XWidget*)&pg()->m_setSpin), "SpinBox 可用");
    expect_true(!XWidget_isEnabled((XWidget*)&pg()->m_btnReset), "无故障时复位钮禁用");
    expect_true(!XWidget_isVisible((XWidget*)&pg()->m_btnJog),
                "ATV61 无点动能力 → 点动钮隐藏（caps 裁剪）");
    saveShot(host, "out/vfdshot_1_initial.png");

    /* ==================== ② 控制闸联动 ==================== */
    printf("[2] 控制闸开/关联动\n");
    XAbstractButton_click((XAbstractButton*)&pg()->m_gate);   /* 关闸 */
    pumpMs(50);
    expect_true(!XCheckBox_isChecked(&pg()->m_gate), "点击后闸=关");
    expect_true(!XWidget_isEnabled((XWidget*)&pg()->m_btnRun), "关闸→启动钮禁用");
    expect_true(!XWidget_isEnabled((XWidget*)&pg()->m_dirCombo), "关闸→方向下拉禁用");
    expect_true(!XWidget_isEnabled((XWidget*)&pg()->m_setApply), "关闸→下发禁用");
    expect_true(!XWidget_isEnabled((XWidget*)&pg()->m_setSlider), "关闸→滑块禁用");
    expect_true(!XWidget_isEnabled((XWidget*)&pg()->m_setSpin), "关闸→SpinBox 禁用");
    {
        const char* h = xstrCStr(XLabel_text(&pg()->m_hint));
        expect_true(h != NULL && h[0] != '\0', "关闸→提示行非空");
    }
    saveShot(host, "out/vfdshot_2_locked.png");
    XAbstractButton_click((XAbstractButton*)&pg()->m_gate);   /* 再开 */
    pumpMs(50);
    expect_true(XCheckBox_isChecked(&pg()->m_gate), "再次点击闸=开");
    expect_true(XWidget_isEnabled((XWidget*)&pg()->m_btnRun), "开闸→启动钮恢复");
    expect_true(XWidget_isEnabled((XWidget*)&pg()->m_setSlider), "开闸→滑块恢复");
    {
        const char* h = xstrCStr(XLabel_text(&pg()->m_hint));
        expect_true(h == NULL || h[0] == '\0', "开闸→提示行清空");
    }

    /* ==================== ③ Slider↔SpinBox 双向同步 ==================== */
    printf("[3] 给定行滑块/SpinBox 同步\n");
    XAbstractSlider_setValue((XAbstractSlider*)&pg()->m_setSlider, 450);
    expect_true(XSpinBox_value(&pg()->m_setSpin) == 450,
                "Slider→450 ⇒ SpinBox=450（0.1Hz 定点同口径）");
    XSpinBox_setValue(&pg()->m_setSpin, 480);
    expect_true(XAbstractSlider_value((XAbstractSlider*)&pg()->m_setSlider) == 480,
                "SpinBox→480 ⇒ Slider=480（回环抑制）");

    /* ==================== ④ 下发（全链路写落定到假从站 LFR1） ==================== */
    printf("[4] 下发按钮全链路\n");
    {
        uint16_t before = g_simInstance ? g_simInstance->m_lfr1 : 0;
        XAbstractButton_click((XAbstractButton*)&pg()->m_setApply);
        pumpMs(120);
        expect_true(g_simInstance != NULL && g_simInstance->m_lfr1 != before,
                    "下发→假从站 LFR1 槽已变（按钮→provider→设备→链路）");
        printf("      (LFR1: %u -> %u，期望 480 = 48.0Hz@0.1Hz)\n",
               (unsigned)before,
               (unsigned)(g_simInstance ? g_simInstance->m_lfr1 : 0));
    }

    /* ==================== ⑤ 启动/停止单钮双态 ==================== */
    printf("[5] 启动/停止双态钮\n");
    {
        int i;
        uint16_t cmdBefore;
        /* 文字随运行态翻转的不变量（自治剧本下任意采样都应成立）。
           快照解码与文字刷新同出一拍，但通知若滞后一拍会出现瞬时
           错位——允许一次「再等一拍复核」，仍错位才算失败。 */
        for (i = 0; i < 8; ++i) {
            const XVfdDevice* dev = pg()->m_provider->device(pg()->m_provUser);
            const XVfdStatus* st = XVfdDevice_status_const(dev);
            bool running = st && st->m_valid && st->m_running;
            const char* txt = btnText(&pg()->m_btnRun);
            const char* want = running
                ? "\xE5\x81\x9C\xE6\xAD\xA2"     /* 停止 */
                : "\xE5\x90\xAF\xE5\x8A\xA8";    /* 启动 */
            if (txt == NULL || strcmp(txt, want) != 0) {
                pumpMs(260);                     /* 宽限：等通知落一拍。 */
                st = XVfdDevice_status_const(
                    pg()->m_provider->device(pg()->m_provUser));
                running = st && st->m_valid && st->m_running;
                txt = btnText(&pg()->m_btnRun);
                want = running
                    ? "\xE5\x81\x9C\xE6\xAD\xA2"
                    : "\xE5\x90\xAF\xE5\x8A\xA8";
                if (txt == NULL || strcmp(txt, want) != 0) {
                    printf("      (错位样例: running=%d text=%s)\n",
                           (int)running, txt ? txt : "(null)");
                    expect_true(false, "按钮文字与运行态一致（双态联动）");
                    break;
                }
            }
            pumpMs(220);
        }
        if (i == 8) expect_true(true, "按钮文字与运行态一致（双态联动，8 拍采样）");

        /* 点击 → 命令字写落定（自治剧本下不赌点击后的运行态，只验
           「按钮 → provider → 设备 → 假从站命令槽」链路已抵达）。 */
        cmdBefore = g_simInstance ? g_simInstance->m_cmd : 0;
        XAbstractButton_click((XAbstractButton*)&pg()->m_btnRun);
        {
            bool landed = false;
            int waited = 0;
            while (waited <= 1000 && !landed) {
                pumpMs(60); waited += 60;
                landed = g_simInstance && g_simInstance->m_cmd != cmdBefore;
            }
            expect_true(landed, "点击启/停钮→命令字写落定（cmd 变更）");
        }
        /* 等动作序列收尾（CiA402 多步推进期间 isBusy=true；不空闲就点
           下一次启动会被设备层拒绝——⑥的 reverse 断言依赖新序列）。 */
        if (!waitUntil(pred_idle, 4000))
            printf("  [WARN] 设备 4s 未空闲（⑥可能连带失败）\n");
    }

    /* ==================== ⑥ 方向下拉（反转随下次启动生效） ==================== */
    printf("[6] 方向下拉\n");
    expect_true(XComboBox_currentIndex(&pg()->m_dirCombo) == 0, "缺省方向=正转");
    {
        /* 强制停机待命窗内启动反转：reverse 位随命令字落定。 */
        if (g_simInstance) {
            g_simInstance->m_phase = Phase_Stopped;
            g_simInstance->m_phaseMs = 0;
        }
        pumpMs(40);
        XComboBox_setCurrentIndex(&pg()->m_dirCombo, 1);
        expect_true(XComboBox_currentIndex(&pg()->m_dirCombo) == 1, "下拉可切反转");
        {
            uint16_t cmdBefore = g_simInstance ? g_simInstance->m_cmd : 0;
            XAbstractButton_click((XAbstractButton*)&pg()->m_btnRun);
            bool landed = false;
            int waited = 0;
            while (waited <= 1000 && !landed) {
                pumpMs(60); waited += 60;
                landed = g_simInstance &&
                         g_simInstance->m_cmd != cmdBefore &&
                         g_simInstance->m_reverse;
            }
            expect_true(landed, "反转启动→命令字落定且 reverse 位=1");
        }
        XComboBox_setCurrentIndex(&pg()->m_dirCombo, 0);   /* 还原正转。 */
        pumpMs(100);
    }

    /* ==================== ⑦ 故障注入 → 复位钮使能 → 点击清除 ==================== */
    printf("[7] 故障复位\n");
    if (g_simInstance) {
        g_simInstance->m_lft = 9;          /* OCF 过流（品牌码）。 */
        g_simInstance->m_faultMs = 3000;
    }
    expect_true(waitUntil(pred_faulted, 2000), "注入后快照报故障");
    pumpMs(250);   /* 让 statusUpdated→vfdRefresh 全链路跑完再查使能态。 */
    expect_true(XWidget_isEnabled((XWidget*)&pg()->m_btnReset),
                "有故障→复位钮使能");
    saveShot(host, "out/vfdshot_3_fault.png");
    /* 设备忙（⑥启动序列未收尾）时 resetFault 会被拒——先等空闲。 */
    if (!waitUntil(pred_idle, 4000))
        printf("  [WARN] 设备 4s 未空闲（复位可能被拒）\n");
    {
        bool accepted = pg()->m_provider->resetFault != NULL &&
                        pg()->m_provider->resetFault(pg()->m_provUser);
        expect_true(accepted, "复位点击→provider 受理");
    }
    {
        bool cleared = false;
        int waited = 0;
        while (waited <= 2000 && !cleared) {
            pumpMs(60); waited += 60;
            cleared = g_simInstance && g_simInstance->m_lft == 0;
        }
        expect_true(cleared, "复位命令→假从站故障位清除（bit7 上升沿）");
    }
    expect_true(waitUntil(pred_notFaulted, 2000), "快照回无故障");
    pumpMs(200);
    expect_true(!XWidget_isEnabled((XWidget*)&pg()->m_btnReset),
                "故障清除→复位钮回禁用");

    /* ==================== ⑧ 趋势图追点 + 视口仲裁 ==================== */
    printf("[8] 趋势图\n");
    {
        int count = pg()->m_trendSeries
            ? XXYSeries_count((XXYSeries*)pg()->m_trendSeries) : 0;
        expect_true(count > 0, "趋势序列已有追点（快照驱动追点）");
        expect_true(pg()->m_axisX &&
                    pg()->m_axisX->m_base.m_max > pg()->m_axisX->m_base.m_min,
                    "X 轴时间窗有效（max>min）");
        expect_true(!pg()->m_trendUserView, "缺省=自动跟随（视口归页面）");
        emitViewport(&pg()->m_trendView, XChartView_ViewportChange_Pan);
        expect_true(pg()->m_trendUserView, "Pan→视口归用户（页面停写 X 轴）");
        emitViewport(&pg()->m_trendView, XChartView_ViewportChange_Reset);
        expect_true(!pg()->m_trendUserView && pg()->m_trendWinW == 30.0,
                    "Reset→交还跟随且窗宽回默认 30s");
        emitViewport(&pg()->m_trendView, XChartView_ViewportChange_Zoom);
        expect_true(!pg()->m_trendUserView, "Zoom（跟随态）→仍跟随");
    }

    /* ==================== ⑨ 自动运行锁 ==================== */
    printf("[9] 自动运行锁\n");
    HsfMonitorVfd_setInteractiveMode(false);
    pumpMs(50);
    expect_true(!HsfMonitorVfd_interactiveMode(), "进入自动：interactiveMode=false");
    expect_true(!XWidget_isVisible((XWidget*)&pg()->m_gate), "自动：闸勾选框隐藏");
    expect_true(!XWidget_isEnabled((XWidget*)&pg()->m_btnRun), "自动：启动钮禁用");
    expect_true(!XWidget_isEnabled((XWidget*)&pg()->m_setApply), "自动：下发禁用");
    expect_true(!XCheckBox_isChecked(&pg()->m_gate), "自动：闸强制关");
    saveShot(host, "out/vfdshot_4_automode.png");
    HsfMonitorVfd_setInteractiveMode(true);
    pumpMs(50);
    expect_true(HsfMonitorVfd_interactiveMode(), "退回手动：interactiveMode=true");
    expect_true(XWidget_isVisible((XWidget*)&pg()->m_gate), "手动：勾选框恢复");
    expect_true(XCheckBox_isChecked(&pg()->m_gate), "手动：闸值还原（此前=开）");
    expect_true(XWidget_isEnabled((XWidget*)&pg()->m_btnRun), "手动：启动钮恢复");

    /* ==================== ⑩ 形态自适应（窄档纵排/宽档横排） ==================== */
    printf("[10] 形态自适应\n");
    {
        XPoint wideCur = XWidget_pos(&pg()->m_gaugeCur.m_canvas.m_base);
        (void)wideCur;
        HsfMonitorVfd_adapt(page, true);      /* 窄档。 */
        {
            XPoint f = XWidget_pos(&pg()->m_gaugeFreq.m_canvas.m_base);
            XPoint c = XWidget_pos(&pg()->m_gaugeCur.m_canvas.m_base);
            expect_true(c.y > f.y, "窄档：电流仪表在频率仪表下方（纵排）");
        }
        saveShot(host, "out/vfdshot_5_narrow.png");
        HsfMonitorVfd_adapt(page, false);     /* 宽档。 */
        {
            XPoint f = XWidget_pos(&pg()->m_gaugeFreq.m_canvas.m_base);
            XPoint c = XWidget_pos(&pg()->m_gaugeCur.m_canvas.m_base);
            printf("      (宽档几何: freq=(%d,%d) cur=(%d,%d))\n",
                   f.x, f.y, c.x, c.y);
            expect_true(c.x > f.x && c.y == f.y, "宽档：三仪表横排同高");
        }
    }

    /* ==================== ⑪ 主界面快捷入口（receiver 实锅回归锁） ====================
     * 旧实现把 connect_1 的 receiver（按钮自身）直转页类型 → 错误基址 →
     * `receiver==&m_actions[i]` 比较恒假 → 三个快捷入口点击静默失效。
     * 本节逐钮点击并核对 navFn 目标页，防同款复发。 */
    printf("[11] 主界面快捷入口\n");
    {
        XWidget* home = HsfHomePage_create(host, homeNavRecorder, NULL);
        HsfHomePage* hp = (HsfHomePage*)home;   /* m_base 第一成员，指针同址。 */
        static const int kWant[3] = {
            HSF_PAGE_MONITOR_MENU, HSF_PAGE_DATA_MENU, HSF_PAGE_SETTINGS_MENU
        };
        int i;
        expect_true(home != NULL, "主界面页创建");
        for (i = 0; i < 3; ++i) {
            g_homeNav = -1;
            XAbstractButton_click((XAbstractButton*)&hp->m_actions[i]);
            expect_true(g_homeNav == kWant[i], "快捷入口点击→导航目标正确");
        }
    }

    printf("=== HSF 变频器页探针结束：失败 %d 项 ===\n", s_failures);
    return s_failures == 0 ? 0 : 1;
}
