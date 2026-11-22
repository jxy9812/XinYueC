/**
 * @file       hsf_vfd_probe.c
 * @brief      HSF 客户端全界面真实点击回归探针（独立可执行，进程内）。
 * @details    覆盖面分两段：
 *               [A..G] 主界面点击流（2026-11-10 新增）：离线主窗装配
 *                  （hsfUiMakeWin，--no-login 同路径）→ 导航树逐页真实
 *                  点击切页 → 标题栏 ☰ 收起/恢复侧栏 → 主界面三张可点
 *                  卡片（健康表/泵详情/阀详情浮层开合）→ 历史页（翻页
 *                  守卫/生成折线图/表格↔图表切换/列显隐下拉）→ 日志页
 *                  （类别下拉换档/重新加载/表头双击排序三态）→ 本地设置
 *                  页（SpinBox 箭头真实点击 → 设置落盘键变化）。
 *                  [A4 2026-11-14] 主界面**模式下拉**的 op=5 接线：改选
 *                  （activated）触发选择路径、程序化回显（currentIndex-
 *                  Changed）不触发（无回环）。
 *               ①..⑩ 变频器页全量交互回归（既有断言全部保留）：
 *                 控制闸联动 / Slider↔SpinBox 同步 / 下发全链路 / 启停
 *                 双态 / 方向下拉 / 故障复位 / 自动运行锁 / 趋势视口 /
 *                 形态自适应。
 *             关键状态各存一张 PNG（out/vfdshot_*.png）供目验。
 *
 *             [实现方式] 单 TU 直接收录客户端全量 UI（HsfUi.c + 全部页面
 *             + HsfLinkClient 真实现，恒离线不 open）：主窗装配 hsfUiMakeWin
 *             与各页内部控件（s_homePage、HsfDataHistory/HsfDataLogPage/
 *             HsfSettingsLocal 结构体）走公开 API 拿不到，收录后直达。
 *             [真实点击口径] 新增断言一律经
 *             XWindowSystemInterface_handleMouseEvent 注入 PRESS/RELEASE
 *             ——与平台后端同一条分发链（XWidgetWindow 桥 → 命中测试 →
 *             隐式抓取 → 控件虚槽），不是直接调槽函数；每次点击后泵事件
 *             循环再断言。既有 ①..⑩ 段保留原 XAbstractButton_click 口径
 *             不动（同类：框架 click() = 按下+抬起合成）。
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
#include "XVector.h"
#include "XVfdDevice.h"
#include "XString.h"
#include "XSystem.h"
#include "XDateTime.h"
#include "XHeaderView.h"
#include "XTableView.h"
#include "XListView.h"
#include "XWindowSystemInterface.h"   /* 真实鼠标事件注入入口 */
#include "XPerformanceOverlay.h"       /* 悬浮层显隐回读（合并页分区②抽样） */
#include "HsfMonitorVfd.h"
#include "HsfUi.h"

/* 直接收录被测实现（单 TU，避免独立目标的符号复制）。收录顺序：
 * 主题 → 模拟源 → provider 表 → 变频器页 → 设置落盘/链路客户端（真实现，
 * 恒离线）→ 页面骨架件 → 各页面 → 主窗装配（引用前述全部符号）。 */
#include "HsfTheme.c"
#include "HsfVfdDemoSim.c"
#include "HsfVfdProvider.c"
#include "HsfMonitorVfd.c"

/* ---- 远端 provider 桩：探针恒离线（HsfLinkClient 不 open），页面建页
 * 走 demo 模拟源分支，远端 provider 永不被取用；真实现
 * （HsfVfdProviderRemote.c）不收录，此处给最小桩补符号。 ---- */
const HsfVfdProvider* HsfVfdProvider_remote(void) { return NULL; }

/* ---- HSF 客户端 UI 全量（2026-11-10 主界面点击流）：真 HsfLinkClient
 * 收录后恒离线（不调 open ⇒ isOpen/isReady=false，与 --no-login 生产
 * 路径同口径）。各 .c 间 static 同名零冲突（收编前已普查）。 ---- */
#include "HsfSettingsStore.c"
#include "HsfLinkHistory.c"
#include "HsfLinkConfig.c"
#include "HsfLinkClient.c"
#include "HsfPages.c"
#include "HsfPlaceHolder.c"
#include "HsfStack.c"
#include "HsfNavPanel.c"
#include "HsfTitleBar.c"
#include "HsfStatusBar.c"
#include "HsfTableSort.c"
#include "HsfDataBar.c"
#include "HsfExport.c"    /* 历史/日志/报警页导出共用件（HsfDataHistory.c 调
                           * HsfExport_*，该 .c 只随 HsfClient 编译、不在任何
                           * 库里 ⇒ 单 TU 不收录则本目标链接 undefined
                           * reference）。static 同名已与收录集普查无冲突。 */
#include "HsfFlowMeterProvider.c"
#include "HsfDataTable.c"
#include "HsfLogin.c"
#include "pages/HsfChartZoom.c"
#include "pages/HsfHomePage.c"
/* 四个菜单页各自定义 static kEntries[]（同名不同表）——单 TU 收录按文件
 * 作用域改名（宏改名只影响本 TU 内的符号拼写，产品代码零改动）。 */
#define kEntries kEntries_mainMenu
#include "pages/HsfMainMenu.c"
#undef kEntries
#include "HsfMenuPage.c"          /* 首页字号重放引用的通用菜单页构件 */
#define kEntries kEntries_monitorMenu
#include "pages/HsfMonitorMenu.c"
#undef kEntries
#define kEntries kEntries_dataMenu
#include "pages/HsfDataMenu.c"
#undef kEntries
#include "pages/HsfMonitorDigital.c"
#include "pages/HsfMonitorAnalog.c"
#include "pages/HsfMonitorFlowMeter.c"
#include "pages/HsfMonitorValve.c"
#include "pages/HsfDataHistory.c"
#include "pages/HsfDataLog.c"
#include "pages/HsfDataAlarm.c"
#define kEntries kEntries_settingsMenu
#include "pages/HsfSettingsMenu.c"
#undef kEntries
#include "pages/HsfSettingsLocal.c"
#include "pages/HsfSettingsRemote.c"
#include "pages/HsfNetworkPage.c"
#include "pages/HsfOverlayPage.c"
#include "pages/HsfWindowPage.c"
#include "HsfUi.c"

static int s_failures = 0;

/* [切页性能门 2026-11-18] 导航切页两段耗时上限（毫秒）。
 * 同步段 = 点导航 → hsf_goPage 返回（布局解算 + 逐页 adapt + 导航重排），
 * 这段**卡住 UI 线程**，是用户「点下去没反应一会儿」的直接来源。
 * 首帧 = 新页那一帧的整树绘制耗时（仪表静态层/进度带、趋势图都在其中）。
 * [口径] 离屏渲染下的绝对值；真机更慢，但**门禁锁的是结构性问题**：
 *        老路 = 同步段多轮整树重解算（实测 600ms）、绘制走径向线链
 *        （水质/流量计页单帧 229ms）。阈值取「修复后实测 + 余量」。
 */
#define HSF_PROBE_SWITCH_SYNC_MS 150
#define HSF_PROBE_SWITCH_FRAME_MS 80

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
static bool pred_notRunning(void)
{
    const XVfdDevice* dev = s_vfdPage ? s_vfdPage->m_provider
        ? s_vfdPage->m_provider->device(s_vfdPage->m_provUser) : NULL : NULL;
    const XVfdStatus* st = XVfdDevice_status_const(dev);
    return st && st->m_valid && !st->m_running;
}

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

/* ==================== [A..G] 主界面点击流基建（2026-11-10 新增） ==================== */

/** @brief 发射 combo 的 activated(index)（用户改选口径）。
 *  @details 信号函数只是 id 取址器，真发射走 XObject_emitSignal+XVarList
 *           （照 emitViewport 同款口径）。 */
static void emitComboActivated(XComboBox* combo, int index)
{
    XVarList* args;
    if (!combo) return;
    args = XVarList_Create(XVar(int, index));
    if (!args) return;
    XObject_emitSignal((XObject*)combo,
                       (size_t)XComboBox_activated_signal(combo, index),
                       args, NULL, NULL, XEVENT_PRIORITY_NORMAL);
}

static HsfWin* g_win = NULL;        /**< 活动主窗（hsfUiMakeWin 产物）。 */
static HsfNavPanel* g_nav = NULL;   /**< 左侧导航面板（g_win 内嵌成员）。 */
static int s_synMiss = 0;           /**< 合成点击缺窗口句柄计数（诊断）。 */

/* ---- 真实鼠标事件合成：把「左键按下+抬起」经 XWindowSystemInterface
 * 注入目标所在顶层窗口——与平台后端完全同一条分发链（XWidgetWindow 桥
 * → XWidget_dispatchPointerEvent 命中测试 → 隐式抓取 → 控件虚槽），
 * 不是直接调槽函数。坐标换算：target 局部 → 全局 → 窗口局部。 ---- */
static void synClickIn(XWidget* target, int lx, int ly)
{
    XWidget* top;
    XWindow* winHandle;
    XPoint local;
    XPoint global;
    XPoint winPos;
    if (!target) return;
    local.x = lx;
    local.y = ly;
    top = XWidget_topLevelWidget(target);
    if (!top) { ++s_synMiss; return; }
    winHandle = (XWindow*)XWidget_windowHandle(top);
    if (!winHandle) { ++s_synMiss; return; }
    global = XWidget_mapToGlobal(target, &local);
    winPos = XWidget_mapFromGlobal(top, &global);
    XWindowSystemInterface_handleMouseEvent(
        winHandle, XEVENT_TYPE_MOUSE_BUTTON_PRESS,
        XMouseButton_LeftButton, XMouseButton_LeftButton, 0, winPos);
    XWindowSystemInterface_handleMouseEvent(
        winHandle, XEVENT_TYPE_MOUSE_BUTTON_RELEASE,
        XMouseButton_LeftButton, 0, 0, winPos);
}

/* 目标控件中心点的真实点击（既有「点中心」口径）。 */
static void synClick(XWidget* w)
{
    if (!w) return;
    synClickIn(w, XWidget_width(w) / 2, XWidget_height(w) / 2);
}

/* 双击第二拍：DBL_CLICK + 配对 RELEASE（与平台后端同一注入链）。
 * 真实双击的第二次按压以 DBL 事件类型到达——桌面 CS_DBLCLKS 的
 * WM_LBUTTONDBLCLK、触摸 DoubleTap 的合成序列都如此，不存在第二次
 * 独立 PRESS（旧版「两连击」假路径走按压链 400ms 窗口、绕开 DBL
 * 处理器，2026-11-10 已废弃）。 */
static void synDblSecond(XWidget* target, int lx, int ly)
{
    XWidget* top;
    XWindow* winHandle;
    XPoint local;
    XPoint global;
    XPoint winPos;
    if (!target) return;
    local.x = lx;
    local.y = ly;
    top = XWidget_topLevelWidget(target);
    if (!top) { ++s_synMiss; return; }
    winHandle = (XWindow*)XWidget_windowHandle(top);
    if (!winHandle) { ++s_synMiss; return; }
    global = XWidget_mapToGlobal(target, &local);
    winPos = XWidget_mapFromGlobal(top, &global);
    XWindowSystemInterface_handleMouseEvent(
        winHandle, XEVENT_TYPE_MOUSE_BUTTON_DBL_CLICK,
        XMouseButton_LeftButton, XMouseButton_LeftButton, 0, winPos);
    XWindowSystemInterface_handleMouseEvent(
        winHandle, XEVENT_TYPE_MOUSE_BUTTON_RELEASE,
        XMouseButton_LeftButton, 0, 0, winPos);
}

/* 表头排序用：真实平台双击序列 = 单击（PRESS+RELEASE）+ 第二拍
 * （DBL_CLICK+RELEASE），同点同段。 */
static void synDblClick(XWidget* w)
{
    if (!w) return;
    synClickIn(w, XWidget_width(w) / 2, XWidget_height(w) / 2);
    synDblSecond(w, XWidget_width(w) / 2, XWidget_height(w) / 2);
}

/* SpinBox 箭头列真实点击：右列中点，上 1/4=步进加、下 3/4=步进减
 * （命中区与绘制同源：无样式 16px 右列、有样式样式列，中点两档都落在
 * 列内）。 */
static void synSpinStep(XSpinBox* spin, bool up)
{
    XWidget* w = (XWidget*)spin;
    if (!w) return;
    synClickIn(w, XWidget_width(w) - 8,
               up ? XWidget_height(w) / 4 : XWidget_height(w) * 3 / 4);
}

/* ---- 主窗状态栏文本（HsfStatusBar 是 XLabel 子类）。 ---- */
static const char* winStatusText(void)
{
    if (!g_win) return NULL;
    return xstrCStr(XLabel_text(&g_win->m_statusBar.m_base));
}

/* ---- 导航树条目真实点击：必要时先真实点击展开父组（手风琴）。
 * 条目下标 = HsfPages_navTree 节点下标（面板按树序建条目）。 ---- */
static void navClickNode(int nodeIndex)
{
    const HsfNavNode* tree = HsfPages_navTree(NULL);
    if (!g_nav || !tree) return;
    if (nodeIndex < 0 || nodeIndex >= g_nav->m_count) return;
    if (tree[nodeIndex].childOf >= 0 &&
        g_nav->m_expanded != tree[nodeIndex].childOf) {
        synClick((XWidget*)&g_nav->m_items[tree[nodeIndex].childOf].m_btn);
        pumpMs(150);
    }
    synClick((XWidget*)&g_nav->m_items[nodeIndex].m_btn);
    pumpMs(150);
}

/* ---- 主界面卡片按标题查找：卡片 = 页根直接子件中类虚表为
 * HsfHomeCard 的容器（详情浮层也是 HsfHomeCard，显式排除）。 ---- */
static HsfHomeCard* homeCardByTitle(const char* titleUtf8)
{
    const XVector* kids;
    size_t n;
    size_t i;
    if (!s_homePage || !titleUtf8) return NULL;
    kids = XObject_children((XObject*)&s_homePage->m_base);
    n = kids ? XVector_size_base((const XContainer*)kids) : 0;
    for (i = 0; i < n; ++i) {
        XObject* o = *(XObject**)XVector_at_base(kids, (int64_t)i);
        XWidget* w;
        HsfHomeCard* card;
        const char* t;
        if (!o || !o->m_is_widget) continue;
        w = (XWidget*)o;
        if (XClassGetVtable(w) != HsfHomeCard_class_init()) continue;
        if (w == (XWidget*)s_homePage->m_detailFloat) continue;
        card = (HsfHomeCard*)w;
        t = xstrCStr(XLabel_text(&card->m_title));
        if (t && strcmp(t, titleUtf8) == 0) return card;
    }
    return NULL;
}

/* ---- 分区内直接子件按复选框文本查找（合并页「悬浮窗」「窗口与屏幕」
 * 分区的控件核对用；只认 XCheckBox 虚表，避免误抓标签）。 ---- */
static XCheckBox* findCheckByText(XWidget* root, const char* textUtf8)
{
    XVector* kids;
    size_t n;
    size_t i;
    if (!root || !textUtf8) return NULL;
    kids = XObject_children((XObject*)root);
    n = kids ? XVector_size_base((const XContainer*)kids) : 0;
    for (i = 0; i < n; ++i) {
        XObject* o = *(XObject**)XVector_at_base(kids, (int64_t)i);
        XWidget* w;
        const char* t;
        if (!o || !o->m_is_widget) continue;
        w = (XWidget*)o;
        if (XClassGetVtable(w) != XCheckBox_class_init()) continue;
        t = xstrCStr(XAbstractButton_text((XAbstractButton*)w));
        if (t && strcmp(t, textUtf8) == 0) return (XCheckBox*)w;
    }
    return NULL;
}

/* ---- 组合框弹层第 row 行真实点击（行激活于 RELEASE；弹层行高
 * XCOMBOBOX_ITEM_H=20，与 xcomboPopupView_rowAtY 同口径）。 ---- */
static void synComboSelectRow(XComboBox* combo, int row)
{
    XListView* view = combo ? XComboBox_view(combo) : NULL;
    int slot;
    if (!view) return;
    slot = XListView_rowHeight(view) + XListView_spacing(view);
    if (slot <= 0) slot = 20;
    synClickIn((XWidget*)view, XWidget_width((XWidget*)view) / 2,
               row * slot + slot / 2);
}

int main(void)
{
    XGuiApplication* app;
    XWidget* page;
    HsfUiOptions opts;
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
    /* 界面设置持久化：与 main_client.c --no-login 路径同序打开（本地设置
       页/窗口页落盘依赖它；打开失败按出厂默认，不阻断）。 */
    HsfSettingsStore_open();

    /* ==================== [A] 离线主窗装配（登录/离线入口） ====================
     * 与 --no-login 生产路径同一装配函数（hsfUiMakeWin→hsf_win_create）：
     * 标题栏 + 分割条[导航|堆栈] + 黑色状态栏 + 18 页全建。 */
    printf("[A] 离线主窗装配\n");
    memset(&opts, 0, sizeof(opts));
    opts.noLogin = 1;
    opts.shotWidth = 1280;
    opts.shotHeight = 720;
    opts.startPage = -1;
    opts.thenPage = -1;
    g_win = hsfUiMakeWin(&opts);
    expect_true(g_win != NULL, "离线模式：主窗装配成功");
    if (!g_win) {
        printf("主窗装配失败，探针中止\n");
        return 2;
    }
    g_nav = &g_win->m_navPanel;
    /* 离屏（-3000,-3000，不占桌面、不抢前台）。 */
    XWidget_setGeometry(&g_win->m_base, -3000, -3000, 1280, 720);
    pumpMs(600);   /* 布局收敛：帧泵首拍 adapt + 分档。 */
    expect_true(XWidget_isVisible(&g_win->m_base), "主窗可见");
    expect_true(XWidget_isVisible(&g_nav->m_base), "导航侧栏可见（宽屏形态）");
    expect_true(g_nav->m_count == 15, "导航树条目齐全（15 节点）");
    /* [2026-11-13 三页合并] 原「悬浮窗设置」「窗口设置」两个独立页号
       （16/17）作废 ⇒ 导航树节点 17→15；页号 13「本地设置」保留不变。
       锁：树里不得再出现那两个页名，且「本地设置」仍指向页 13。 */
    {
        const HsfNavNode* tree = HsfPages_navTree(NULL);
        int i;
        bool hasLocal = false;
        bool hasOverlay = false;
        bool hasWindow = false;
        for (i = 0; tree && i < g_nav->m_count; ++i) {
            const char* cap = tree[i].caption;
            if (!cap) continue;
            if (strcmp(cap, "本地设置") == 0 &&
                tree[i].pageIndex == HSF_PAGE_SETTINGS_LOCAL)
                hasLocal = true;
            if (strcmp(cap, "悬浮窗设置") == 0) hasOverlay = true;
            if (strcmp(cap, "窗口设置") == 0) hasWindow = true;
        }
        expect_true(hasLocal, "导航树保留「本地设置」→ 页 13");
        expect_true(!hasOverlay, "导航树已无「悬浮窗设置」（并入本地设置）");
        expect_true(!hasWindow, "导航树已无「窗口设置」（并入本地设置）");
    }
    expect_true(!XWidget_isHidden(&g_nav->m_items[0].m_btn),
                "导航首条目可见");
    expect_true(strcmp(btnText(&g_nav->m_items[0].m_btn),
                       "\xE4\xB8\xBB\xE7\x95\x8C\xE9\x9D\xA2") == 0,
                "导航首条目文本=主界面");
    expect_true(HsfStack_current(&g_win->m_stack) == HSF_PAGE_HOME,
                "初始页=主界面");
    {
        /* [口径注 2026-11-10] 装配期悬浮窗设置页（builders 循环内 ov_report）
           会把状态栏覆写成悬浮层开关文案 ⇒ 首屏状态栏不再是「当前页: 主界面」
           （生产 --no-login 同样如此）；页切换的状态栏同步口径在 [B] 断言。 */
        const char* st = winStatusText();
        expect_true(st != NULL && st[0] != '\0',
                    "状态栏反馈通道非空（装配期文案）");
    }
    expect_true(s_homePage != NULL, "主界面页随装配创建（单例就位）");
    saveShot(&g_win->m_base, "out/vfdshot_m0_home.png");

    /* ==================== [A0] 状态栏账户区（退出登录/切换账户）== */
    printf("[A0] 状态栏账户区（对齐 Python 账户钮）\n");
    {
        HsfStatusBar* sb = &g_win->m_statusBar;
        char txt[160];
        int barW = XWidget_width((XWidget*)sb);
        int barH = XWidget_height((XWidget*)sb);
        HsfStatusBar_userText(sb, txt, (int)sizeof(txt));
        /* 离线（--no-login）态显示「未登录」+ 下拉箭头（对齐 Python
         * `_sync_text` 的 summary + "  ▾"）。[2026-11-12] 箭头字形断言改
         * 「∨」(U+2228)：内置字库缺「▾」(U+25BE) ⇒ 产品侧刻意换字形
         *（HsfStatusBar.c:52-54 注释），探针跟着锁现值。 */
        expect_true(strstr(txt, "\xE6\x9C\xAA\xE7\x99\xBB\xE5\xBD\x95")
                    != NULL,
                    "账户区文案含「未登录」");
        expect_true(strstr(txt, "\xE2\x88\xA8") != NULL,
                    "账户区文案含下拉箭头 ∨");
        /* 命中区在右下角：命中右缘内、不命中左缘。 */
        expect_true(HsfStatusBar_accountHitTest(sb, barW - 4, barH / 2),
                    "账户区命中：右缘");
        expect_true(!HsfStatusBar_accountHitTest(sb, 4, barH / 2),
                    "账户区不命中：左缘");
        /* 命中区宽度落在 [MIN,MAX] 档内（绘制与命中共用同一份几何）。 */
        expect_true(HsfStatusBar_accountHitTest(sb, barW - 40, barH / 2),
                    "账户区命中：右缘内 40px");
    }

    /* ==================== [A1] 账户区动作：退出登录 ====================
     * 放在 [8] 之前：探针的 [8] 段有**既存**崩溃（pg() 指向已销毁的
     * 变频器页——s_vfdPage 在页销毁时不置 NULL），与本轮改动无关；
     * 账户区断言放在它前面才能跑到。 */
    printf("[A1] 账户区动作（退出登录）\n");
    {
        HsfStatusBar* sb = &g_win->m_statusBar;
        char txt[160];
        /* 离线探针（--no-login）下本就未登录 ⇒ 账户区显示「未登录」，
         * 但**动作链路**必须可用：点账户区应能弹菜单并触发回调。
         * 这里直接调动作回调（菜单交互由目视/板端覆盖），验证：
         * ① 回调已注册（HsfUi 侧接线）；② 退出登录后连接关闭、账户
         * 显示仍为「未登录」且不崩（退出≠退进程）。 */
        HsfStatusBar_userText(sb, txt, (int)sizeof(txt));
        expect_true(strstr(txt, "\xE6\x9C\xAA\xE7\x99\xBB\xE5\xBD\x95")
                    != NULL,
                    "退出前账户区显示「未登录」");
        /* 触发退出登录动作（对齐 Python logout：断链 + 清账户显示）。 */
        /* 触发退出登录动作（对齐 Python logout：断链 + 清账户显示）。 */
        hsfUiAccountAction(g_win, HsfStatusBarAccount_Logout);
        pumpMs(200);
        expect_true(!HsfLinkClient_isOpen(),
                    "退出登录后连接已关闭（不退进程）");
        HsfStatusBar_userText(sb, txt, (int)sizeof(txt));
        expect_true(strstr(txt, "\xE6\x9C\xAA\xE7\x99\xBB\xE5\xBD\x95")
                    != NULL,
                    "退出登录后账户区回「未登录」");
        expect_true(XWidget_isVisible(&g_win->m_base),
                    "退出登录后主窗仍在（不退进程）");
    }

    /* ==================== [A2] 登录窗几何（内容驱动高度）================
     * [2026-11-11 实锅] 登录窗固定 420x560，而控件高按全局缩放放大
     * （150% ⇒ 输入框 54 / 登录钮 66）⇒ 底部「记住密码 + 登录钮 +
     * 状态行」被裁 ⇒ 用户点不到登录钮；勾选框宽度没随字号重算 ⇒ 文字
     * 溢出压到下一个勾选框上。这里按真实装配（带缩放）断言两件事：
     * ① 窗口高 ≥ 表单内容提示高（登录钮在窗内）；
     * ② 勾选框宽 ≥ 自身文字实测宽（不被压）。 */
    printf("[A2] 登录窗几何（内容驱动高度 + 勾选框宽度）\n");
    {
        HsfLoginWindow* lw;
        int formH;
        int winH;
        int cbW;
        int textW;
        XFont f;
        /* [必须在大缩放下验] 100% 时控件高不变、宽度也不变 ⇒ 两个断言
         * 恒过＝没测到。装 150% 全局缩放（现场截图的缩放档）再建窗，
         * 才能抓住「固定 560 装不下」与「勾选框宽度没跟」两个真问题。
         * [顺序] 缩放有静态缓存 ⇒ 必须**先**写设置、**再**失效缓存，
         * 最后才建窗（建窗时读到的才是 150%）。 */
        HsfSettingsStore_setInt("FontScale/Global", 150);
        hsfUi_invalidateGlobalFontPct();
        expect_true(hsfUi_globalFontPct() == 150,
                    "全局缩放已切 150%（建窗前生效）");
        lw = HsfLoginWindow_createFor(NULL, NULL, NULL);
        expect_true(lw != NULL, "登录窗建成（150% 缩放）");
        if (lw) {
            XSize fh = XLayout_totalSizeHint((const XLayout*)&lw->m_layout);
            formH = fh.height;
            winH = XWidget_height((XWidget*)&lw->m_base);
            printf("  [INFO] 登录窗高=%d 表单内容提示高=%d\n", winH, formH);
            expect_true(winH >= formH,
                        "登录窗高 ≥ 表单内容高（登录钮不被裁）");
            /* 勾选框宽：须容得下「indicator 13 + 间距 6 + 文字」——
             * 只比「文字宽」不够：文字从 x=19 起画，宽 W 的框要装下
             * 19+W 才不溢出到下一个勾选框的 indicator 上（现场截图的
             * 「记住密码」被方块压住就是这里漏算）。 */
            f = XWidget_font((XWidget*)&lw->m_rememberUser);
            textW = XPainter_textWidth(&f, "记住账号");
            XClassDeinit(&f);
            cbW = XWidget_width((XWidget*)&lw->m_rememberUser);
            printf("  [INFO] 勾选框宽=%d 文字宽=%d 需≥%d\n",
                   cbW, textW, textW + 19);
            expect_true(cbW >= textW + 19,
                        "勾选框宽 ≥ 文字宽+indicator+间距（不溢出压邻框）");
            XWidget_hide((XWidget*)&lw->m_base);
            XClassDelete((XClass*)lw); /* [2026-11-13 排障] deleteLater → 显式删：收尾 g_win 级联 UAF 二分。 */
            pumpMs(100);
        }
        /* 还原缩放（不污染后续段与本地设置）。 */
        HsfSettingsStore_setInt("FontScale/Global", 100);
        hsfUi_invalidateGlobalFontPct();
    }

    /* ==================== [A3] 登录窗关闭语义 ====================
     * [2026-11-11 实锅] 切换账户弹出的登录窗点「×」⇒ **整个应用退出**
     * （主窗一起没了）。根因：登录窗的 closeEvent 与自绘标题栏 × 都
     * **无条件** XGuiApplication_quit()（首启路径「取消即退出」语义）。
     * 修法：登录窗加 m_quitOnClose 开关——切换账户置 false（只收本窗，
     * 主窗保留；对齐 Python「退出登录 ≠ 退出进程」的失败分支口径）。
     * [2026-11-12 缺省改口径] 用户定案「登录窗是登录窗，不要关闭主窗口」
     * ⇒ createFor 缺省按 owner 定（有主窗=只收本窗，见 [A3b]）；独立窗
     * （--login-shot，无第二个窗口）缺省仍退应用。 */
    printf("[A3] 登录窗关闭语义（切换账户不退出应用）\n");
    {
        HsfLoginWindow* lw = HsfLoginWindow_createFor(NULL, NULL, NULL);
        expect_true(lw != NULL, "登录窗建成（关闭语义检查）");
        if (lw) {
            HsfLoginWindow_setQuitOnClose(lw, false);
            /* 真触发关闭（走 closeEvent ⇒ 那条无条件 quit 的路径），
             * 断言应用未退出、主窗仍在。 */
            XWidget_close((XWidget*)lw);
            pumpMs(150);
            expect_true(XWidget_isVisible(&g_win->m_base),
                        "登录窗关闭后主窗仍在（不退进程）");
            XWidget_hide((XWidget*)lw);
            XClassDelete((XClass*)lw); /* [2026-11-13 排障] deleteLater → 显式删：收尾 g_win 级联 UAF 二分。 */
            pumpMs(100);
        }
    }

    /* ==================== [A3b] 首启缺省口径（2026-11-12） ====================
     * 用户定案「登录窗是登录窗，不要关闭主窗口」：有主窗的登录窗缺省
     * 就是只收本窗（createFor 按 owner 定 quitOnClose，不需要显式置
     * false）。断言：带主窗 owner 建窗 → 真触发 close → 主窗仍在、
     * 登录窗已收起。独立窗（无 owner）缺省仍退应用——那条不能在探针里
     * 真触发（会把探针自己退出），由 [A3] 的显式 false 路径覆盖。 */
    printf("[A3b] 登录窗关闭语义（首启缺省只收本窗）\n");
    {
        HsfLoginWindow* lw =
            HsfLoginWindow_createFor((XWidget*)g_win, NULL, NULL);
        expect_true(lw != NULL, "带主窗 owner 登录窗建成");
        if (lw) {
            XWidget_show((XWidget*)lw);
            XWidget_close((XWidget*)lw);
            pumpMs(150);
            expect_true(XWidget_isVisible(&g_win->m_base),
                        "首启缺省口径：登录窗 × 后主窗仍在（不退进程）");
            expect_true(!XWidget_isVisible((XWidget*)lw),
                        "首启缺省口径：登录窗已收起");
            XWidget_hide((XWidget*)lw);
            XClassDelete((XClass*)lw); /* [2026-11-13 排障] deleteLater → 显式删：收尾 g_win 级联 UAF 二分。 */
            pumpMs(100);
        }
    }

    /* ==================== [A4] 模式下拉 op=5（只选模式）接线 ====================
     * [2026-11-14] 客户端主界面的**模式下拉**改选即发 0x0115 op=5 记服务端
     * 粘性（现场物理启动按钮按「上次选中的模式」启动；服务端只认 op=1..4
     * 时下拉与现场按钮各记各的）。接线必须挂 **activated**（用户改选才
     * 发）而不是 currentIndexChanged（程序化回显也发）⇒ 后者会与「组合
     * 循环运行时把下拉切到自动」的回显形成**回环**（每次回显发一次
     * op=5，服务端粘性被反复改写）。
     * [本探针恒离线] 探针从不连服务端（不调 open ⇒ isReady=false）⇒ 槽走
     * 「未连接」分支并写状态栏文案，正好当可观测点：接线在 ⇒ 文案出现。 */
    printf("[A4] 模式下拉 op=5 接线（改选 ⇒ 未同步文案；程序化回显不发）\n");
    {
        XComboBox* combo = &s_homePage->m_autoCombo;
        const char* st;
        /* 基线：把状态栏文本写成别的，便于判定是新写的。 */
        HsfStatusBar_setText(&g_win->m_statusBar, "\xE2\x80\x94\xE2\x80\x94");
        /* ① 程序化回显：只发 currentIndexChanged ⇒ **不得**触发选择路径。 */
        XComboBox_setCurrentIndex(combo, 1);
        pumpMs(80);
        st = winStatusText();
        expect_true(st != NULL &&
                    strstr(st, "\xE6\x9C\xAA\xE8\xBF\x9E\xE6\x8E\xA5") == NULL,
                    "程序化回显不触发 op=5（无回环）");
        /* ② 用户改选：发射 activated(index) ⇒ 走选择路径（未连接分支）。 */
        emitComboActivated(combo, 1);
        pumpMs(80);
        st = winStatusText();
        expect_true(st != NULL &&
                    strstr(st, "\xE6\x9C\xAA\xE8\xBF\x9E\xE6\x8E\xA5\xE6\x9C\x8D"
                               "\xE5\x8A\xA1\xE7\xAB\xAF") != NULL,
                    "改选触发 op=5 路径（未连接时给「未连接服务端…」文案）");
        /* 还原下拉选中项（不污染后续段）。 */
        XComboBox_setCurrentIndex(combo, 0);
        pumpMs(50);
    }

    /* ==================== [A5] 本地设置·网页服务分区 ====================
     * [2026-11-14] 客户端新增「网页服务」分区：开关（随客户端启动网页端）
     * + 端口（缺省 8085——**不占 8080**，本机 8080 常被别的程序占着）。
     * 本段只做**只读**核对：不拨开关 ⇒ 探针里绝不起网页端子进程
     * （探针是无人值守回归，不能留后台 python 进程）。 */
    printf("[A5] 网页服务分区（开关 + 端口 + 状态行；只读核对）\n");
    {
        HsfSettingsLocal* ls =
            (HsfSettingsLocal*)g_win->m_pageRoot[HSF_PAGE_SETTINGS_LOCAL];
        expect_true(ls != NULL, "本地设置页已建");
        if (ls) {
            expect_true(HSF_WEB_DEFAULT_PORT == 8085 &&
                        HSF_WEB_DEFAULT_PORT != 8080,
                        "网页端口缺省 8085（不占 8080）");
            expect_true(XSpinBox_value(&ls->m_webPort) == HsfWebServer_port() &&
                        XSpinBox_value(&ls->m_webPort) >= HSF_WEB_MIN_PORT &&
                        XSpinBox_value(&ls->m_webPort) <= HSF_WEB_MAX_PORT,
                        "端口框与生效值一致且合法");
            expect_true(XCheckBox_isChecked(&ls->m_webEnabled) ==
                        HsfWebServer_enabled(),
                        "开关与设置一致（默认开）");
            expect_true(ls->m_webStatusText[0] != '\0',
                        "状态行有文案");
            expect_true(!HsfWebServer_running(),
                        "探针内不起网页服务（无后台进程副作用）");
        }
    }

    /* ==================== [B] 导航树逐页真实点击 ==================== */
    printf("[B] 导航树逐页点击\n");
    expect_true(g_nav->m_expanded == -1, "初始导航树全收起");
    navClickNode(1);   /* 监视（分组，纯展开不导航）。 */
    expect_true(g_nav->m_expanded == 1, "点「监视」就地展开子菜单（手风琴）");
    expect_true(!XWidget_isHidden(&g_nav->m_items[4].m_btn),
                "展开后「搅拌泵」条目可见");
    navClickNode(4);   /* 搅拌泵（页名 2026-11-12 用户口径；类别仍变频器）。 */
    expect_true(HsfStack_current(&g_win->m_stack) == HSF_PAGE_MONITOR_VFD,
                "点「搅拌泵」堆栈切到搅拌泵页");
    expect_true(XWidget_isVisible(g_win->m_pageRoot[HSF_PAGE_MONITOR_VFD]),
                "搅拌泵页根可见");
    expect_true(XAbstractButton_isChecked(&g_nav->m_items[4].m_btn),
                "导航高亮：搅拌泵条目勾选");
    {
        const char* st = winStatusText();
        /* [2026-11-12] 页名改「搅拌泵」（对齐 Python pages_registry）——
         * 状态栏页名断言同步；UTF-8 = 「搅拌泵」。 */
        expect_true(st != NULL && strstr(st, "\xE6\x90\x85\xE6\x8B\x8C\xE6\xB3\xB5") != NULL,
                    "状态栏同步「当前页: 搅拌泵」");
    }
    navClickNode(7);   /* 数据（分组）。 */
    expect_true(g_nav->m_expanded == 7, "点「数据」展开数据组");
    expect_true(XWidget_isHidden(&g_nav->m_items[4].m_btn),
                "单展开手风琴：监视组子项收起（搅拌泵条目隐藏）");
    navClickNode(8);   /* 历史数据。 */
    expect_true(HsfStack_current(&g_win->m_stack) == HSF_PAGE_DATA_HISTORY,
                "点「历史数据」切到历史页");
    expect_true(XWidget_isVisible(g_win->m_pageRoot[HSF_PAGE_DATA_HISTORY]),
                "历史页根可见");
    navClickNode(9);   /* 日志。 */
    expect_true(HsfStack_current(&g_win->m_stack) == HSF_PAGE_DATA_LOG,
                "点「日志」切到日志页");
    expect_true(XWidget_isVisible(g_win->m_pageRoot[HSF_PAGE_DATA_LOG]),
                "日志页根可见");
    expect_true(XAbstractButton_isChecked(
                    (const XAbstractButton*)&g_nav->m_items[9].m_btn) &&
                !XAbstractButton_isChecked(
                    (const XAbstractButton*)&g_nav->m_items[8].m_btn),
                "导航高亮迁移：日志亮、历史灭");
    navClickNode(11);  /* 设置（分组）。 */
    expect_true(g_nav->m_expanded == 11, "点「设置」展开设置组");
    navClickNode(12);  /* 本地设置。 */
    expect_true(HsfStack_current(&g_win->m_stack) == HSF_PAGE_SETTINGS_LOCAL,
                "点「本地设置」切到设置页(本地)");
    expect_true(XWidget_isVisible(g_win->m_pageRoot[HSF_PAGE_SETTINGS_LOCAL]),
                "设置页(本地)根可见");
    navClickNode(0);   /* 主界面。 */
    expect_true(HsfStack_current(&g_win->m_stack) == HSF_PAGE_HOME,
                "再点回主界面");
    expect_true(XWidget_isVisible(g_win->m_pageRoot[HSF_PAGE_HOME]),
                "主界面页根可见");

    /* ==================== [C] 标题栏 ☰ 菜单钮 ==================== */
    printf("[C] 标题栏 ☰ 菜单钮\n");
    synClickIn(&g_win->m_titleBar.m_base, 16, HSF_TITLE_BAR_H / 2);
    pumpMs(150);
    expect_true(XWidget_isHidden(&g_nav->m_base), "点 ☰ 收起导航侧栏");
    synClickIn(&g_win->m_titleBar.m_base, 16, HSF_TITLE_BAR_H / 2);
    pumpMs(150);
    expect_true(!XWidget_isHidden(&g_nav->m_base), "再点 ☰ 恢复导航侧栏");

    /* ==================== [D] 主界面卡片点击流 ====================
     * [口径注] C 版主界面无「快捷入口三钮」（2026-11-05 用户决定移除，
     * 导航树已覆盖跳转）；可点击交互=三张总览卡（健康/搅拌泵/电磁阀）
     * 弹出居中详情浮层，点击路径=整卡 mousePress（触控口径）。 */
    printf("[D] 主界面卡片点击流\n");
    {
        HsfHomeCard* health = homeCardByTitle(
            "\xE8\xAE\xBE\xE5\xA4\x87\xE5\x81\xA5\xE5\xBA\xB7");   /* 设备健康 */
        HsfHomeCard* pump = homeCardByTitle(
            "\xE6\x90\x85\xE6\x8B\x8C\xE6\xB3\xB5");               /* 搅拌泵 */
        HsfHomeCard* valve = s_homePage->m_valveCard;
        expect_true(health != NULL && pump != NULL && valve != NULL,
                    "三张可点卡片（设备健康/搅拌泵/电磁阀）就位");
        expect_true(s_homePage->m_detailMode == HOME_DETAIL_NONE,
                    "初始详情浮层收起");
        synClick((XWidget*)health);
        pumpMs(150);
        expect_true(s_homePage->m_detailMode == HOME_DETAIL_HEALTH,
                    "点设备健康卡 → 展开设备状态表浮层");
        expect_true(XWidget_isVisible((XWidget*)&s_homePage->m_table),
                    "设备状态表随浮层可见");
        expect_true(XWidget_isVisible((XWidget*)s_homePage->m_detailFloat),
                    "详情浮层可见");
        {
            /* 设备状态表表头单击：该页无排序接线 → 指示器不出现且不崩。 */
            XHeaderView* hh =
                XTableView_horizontalHeader(&s_homePage->m_table.m_base);
            synClickIn((XWidget*)hh, 40, 8);
            pumpMs(120);
            expect_true(hh != NULL && !XHeaderView_isSortIndicatorShown(hh),
                        "点设备状态表表头：无排序指示（主界面表无排序口径）");
        }
        synClickIn((XWidget*)s_homePage->m_detailShade, 6, 6);
        pumpMs(150);
        expect_true(s_homePage->m_detailMode == HOME_DETAIL_NONE,
                    "点遮罩（浮层外）收起浮层");
        expect_true(!XWidget_isVisible((XWidget*)s_homePage->m_detailFloat),
                    "浮层随收起隐藏");
        synClick((XWidget*)pump);
        pumpMs(150);
        expect_true(s_homePage->m_detailMode == HOME_DETAIL_PUMP,
                    "点搅拌泵卡 → 展开泵详情浮层");
        synClick((XWidget*)&s_homePage->m_floatClose);
        pumpMs(150);
        expect_true(s_homePage->m_detailMode == HOME_DETAIL_NONE,
                    "点浮层 × 钮收起泵详情");
        synClick((XWidget*)valve);
        pumpMs(150);
        expect_true(s_homePage->m_detailMode == HOME_DETAIL_VALVE,
                    "点电磁阀卡 → 展开阀详情浮层");
        expect_true(XWidget_isVisible(&s_homePage->m_vTiles[0].m_base),
                    "阀详情瓷贴随浮层可见");
        synClickIn((XWidget*)s_homePage->m_detailShade, 6, 6);
        pumpMs(150);
        expect_true(s_homePage->m_detailMode == HOME_DETAIL_NONE,
                    "再点遮罩收起阀详情");
    }
    saveShot(&g_win->m_base, "out/vfdshot_m1_homecards.png");

    /* ==================== [E] 数据-历史页点击流 ==================== */
    printf("[E] 历史页点击流\n");
    navClickNode(8);   /* 历史数据（[B] 已展开数据组）。 */
    pumpMs(200);
    {
        HsfDataHistory* hist =
            (HsfDataHistory*)g_win->m_pageRoot[HSF_PAGE_DATA_HISTORY];
        HsfDataBar* bar =
            (hist && hist->m_bar) ? (HsfDataBar*)hist->m_bar : NULL;
        expect_true(hist != NULL && bar != NULL, "历史页+工具条就位");
        /* 分页守卫（离线无数据，禁用/判界两道闸都不会放行请求）。 */
        {
            int off0 = hist->m_offset;
            bool req0 = hist->m_pageReqValid;
            synClick((XWidget*)&hist->m_prev);
            pumpMs(80);
            synClick((XWidget*)&hist->m_next);
            pumpMs(80);
            expect_true(hist->m_offset == off0 && hist->m_pageReqValid == req0,
                        "离线点「上一页/下一页」无效果（判界守卫不发请求）");
        }
        /* 生成折线图（无数据 → 状态行报错，不崩溃；视图切到图表）。 */
        synClick((XWidget*)&bar->m_btnChart);
        pumpMs(200);
        expect_true(hist->m_viewMode == 1, "点「生成折线图」切到图表视图");
        expect_true(XWidget_isVisible(&hist->m_chartView.m_base),
                    "图表视图可见");
        {
            const char* st = xstrCStr(XLabel_text(&hist->m_status));
            expect_true(st != NULL &&
                        strstr(st,
                            "\xE5\x9B\xBE\xE8\xA1\xA8\xE5\xA4\xB1\xE8\xB4\xA5") != NULL,
                        "生成折线图（离线）→ 状态行报「图表失败」非崩溃");
        }
        saveShot(&g_win->m_base, "out/vfdshot_m2_histchart.png");
        synClick((XWidget*)&bar->m_btnTable);
        pumpMs(150);
        expect_true(hist->m_viewMode == 0, "点「表格」钮切回表格视图");
        /* 列显隐：**多选复选浮层**（2026-11-11 对齐 Python MultiCheckCombo；
         * 旧的单选 XComboBox 下拉已被浮层取代 ⇒ 探针这段同步改口径）。
         * 断言：缺省全勾 → 取消某列 → 该列隐藏（setColumnHidden 联动，
         * 远端 6fc0adb0 Qt68 对齐后由框架消费隐藏段）→ 勾回 → 恢复。 */
        {
            uint32_t mask0 = HsfDataBar_columnFilterMask((XWidget*)bar);
            expect_true(mask0 != 0,
                        "列筛选缺省=全部列（全勾掩码非零）");
            /* [2026-11-12] 原来这里 synClick(&bar->m_colFilterBtn) 点开
             * 单选下拉；列筛选改**多选复选浮层**后按钮封装进 HsfMultiSelect
             * （成员私有化），编程面 = setColumnFilterMask（勾选即生效，
             * 不需要"先点开"），故删掉对已删成员的点击，掩码读写断言保留。 */
            /* 取消第 1 列（掩码清位）⇒ 表格列 1 隐藏。 */
            HsfDataBar_setColumnFilterMask((XWidget*)bar,
                                           mask0 & ~(uint32_t)(1u << 1));
            pumpMs(200);
            expect_true(XTableView_isColumnHidden(&hist->m_table.m_base, 1),
                        "列1 随列筛选隐藏（本地重铺显隐）");
            /* 勾回 ⇒ 恢复显示。 */
            HsfDataBar_setColumnFilterMask((XWidget*)bar, mask0);
            pumpMs(200);
            expect_true(!XTableView_isColumnHidden(&hist->m_table.m_base, 1),
                        "勾回后列1 恢复显示");
        }
        /* [双击列标题排序 2026-11-10 回归锁] 历史页与日志页同一接线
         * （sectionDoubleClicked → HsfTableSort 三态）；真实双击序列经
         * XTableWidget 表头带转发 → XHeaderView 双击处理器 → 页面槽
         * （离线无数据，锁排序态与指示器，不发请求）。 */
        {
            XHeaderView* hh =
                XTableView_horizontalHeader(&hist->m_table.m_base);
            expect_true(hh != NULL && !XHeaderView_sectionsClickable(hh),
                        "历史页表头未开 clickable（双击排序不依赖它）");
            synDblClick((XWidget*)hh);
            pumpMs(120);
            expect_true(hist->m_sort.m_col == 0 &&
                        XHeaderView_isSortIndicatorShown(hh) &&
                        XHeaderView_sortIndicatorSection(hh) == 0 &&
                        XHeaderView_sortIndicatorOrder(hh) ==
                            (int)XHeaderViewSortOrder_Descending,
                        "双击历史页表头（真实序列）→ 列0降序 + 指示器");
            /* 后两档走程序化发射（同槽口径，日志页 F 段已锁全三态）。
               取消分支不调 updateIndicator ⇒ 指示器滞留（与日志页同款
               观感缺口），此处只锁排序态复位。 */
            XHeaderView_sectionDoubleClicked_signal(hh, 0);
            XHeaderView_sectionDoubleClicked_signal(hh, 0);
            pumpMs(80);
            expect_true(hist->m_sort.m_col == -1,
                        "历史页三态收口 → 取消排序（排序态复位）");
        }
    }

    /* ==================== [F] 数据-日志页点击流 ==================== */
    printf("[F] 日志页点击流\n");
    navClickNode(9);   /* 日志（数据组保持展开）。 */
    pumpMs(200);
    {
        HsfDataLogPage* log =
            (HsfDataLogPage*)g_win->m_pageRoot[HSF_PAGE_DATA_LOG];
        HsfDataBar* bar = (log && log->m_bar) ? (HsfDataBar*)log->m_bar : NULL;
        XHeaderView* hh =
            log ? XTableView_horizontalHeader(&log->m_table.m_base) : NULL;
        expect_true(log != NULL && bar != NULL && hh != NULL,
                    "日志页+工具条+表头就位");
        /* 类别下拉换档（挂在工具条通道口）：真实展开 → 点「控制」行。 */
        expect_true(XComboBox_currentIndex(&bar->m_channel) == 0,
                    "类别下拉缺省=全部类别");
        synClick((XWidget*)&bar->m_channel);
        pumpMs(150);
        expect_true(XComboBox_popupVisible(&bar->m_channel),
                    "点类别下拉 → 弹层展开");
        synComboSelectRow(&bar->m_channel, 2);   /* 行2=控制(ctrl)。 */
        pumpMs(150);
        expect_true(!XComboBox_popupVisible(&bar->m_channel),
                    "点选类别行后弹层收起");
        expect_true(XComboBox_currentIndex(&bar->m_channel) == 2,
                    "类别下拉换到「控制」档");
        expect_true(strcmp(log->m_category, "ctrl") == 0,
                    "页内生效类别=ctrl（换档即改查询口径）");
        {
            const char* st = xstrCStr(XLabel_text(&log->m_status));
            expect_true(st != NULL &&
                        strstr(st,
                            "\xE6\x9C\x8D\xE5\x8A\xA1\xE7\xAB\xAF\xE6\x9C\xAA\xE8\xBF\x9E\xE6\x8E\xA5") != NULL,
                        "换档触发刷新请求（离线态状态行报「服务端未连接」）");
        }
        /* 重新加载钮。 */
        synClick((XWidget*)&bar->m_btnReload);
        pumpMs(150);
        {
            const char* st = xstrCStr(XLabel_text(&log->m_status));
            expect_true(st != NULL &&
                        strstr(st,
                            "\xE9\x87\x8D\xE6\x96\xB0\xE5\x8A\xA0\xE8\xBD\xBD") != NULL,
                        "点「重新加载」→ 状态行进入重载态");
        }
        /* 表头双击排序三态：新列=降序 → 同列=升序 → 同列=取消。
         * [真实序列 2026-11-10 根修复锁] synDblClick 注入的是真实平台
         * 双击序列（PRESS→RELEASE→DBL→RELEASE，无第二次 PRESS）：
         * XTableWidget 表头带转发 → XHeaderView 双击处理器 → 页面排序槽。
         * 旧版两连击假路径（同段两按走按压链 400ms 窗口）已废弃——它
         * 绕开了 DBL 处理器上曾存在的 clickable 门控，绿灯掩盖死区
         * （页面从不 setSectionsClickable ⇒ 真实双击被吞，用户可见回归
         * 「双击列标题排序好像没了」）。 */
        expect_true(!XHeaderView_isSortIndicatorShown(hh),
                    "初始表头无排序指示");
        expect_true(!XHeaderView_sectionsClickable(hh),
                    "页面未开表头 clickable（双击排序不依赖单击口径门控，"
                    "对齐 Qt qheaderview.cpp:2759 无条件发射）");
        synDblClick((XWidget*)hh);
        pumpMs(120);
        /* 真实双击 ⇒ 时间列降序 + 指示器显示；释放处理器在双击窗口内
         * 同段让位（不翻转指示器），指示器与应用排序态一致。 */
        expect_true(XHeaderView_isSortIndicatorShown(hh) &&
                    XHeaderView_sortIndicatorSection(hh) == 0 &&
                    XHeaderView_sortIndicatorOrder(hh) ==
                        (int)XHeaderViewSortOrder_Descending &&
                    log->m_sort.m_col == 0,
                    "双击表头（真实点击转发）→ 排序指示降序（新列）");
        /* [程序化发射同槽口径] 三态后两档：同列再点 → 升序；再点 → 取消。 */
        XHeaderView_sectionDoubleClicked_signal(hh, 0);
        pumpMs(80);
        expect_true(XHeaderView_isSortIndicatorShown(hh) &&
                    XHeaderView_sortIndicatorOrder(hh) ==
                        (int)XHeaderViewSortOrder_Ascending,
                    "再双击同列 → 升序（三态中档）");
        XHeaderView_sectionDoubleClicked_signal(hh, 0);
        pumpMs(120);
        /* [取消口径 2026-11-10] 页面取消分支 = m_sort 复位 + 回取原序
           （logRequest）。注意：取消分支不调 HsfTableSort_updateIndicator
           ⇒ 表头指示器滞留上一列（离线下无应答刷新路径，线上重填表也不
           清它）——轻微观感缺口，随「表头带转发」框架修复一并收口。 */
        {
            const char* st = xstrCStr(XLabel_text(&log->m_status));
            expect_true(log->m_sort.m_col == -1 && st != NULL &&
                        strstr(st,
                            "\xE6\x9C\x8D\xE5\x8A\xA1\xE7\xAB\xAF\xE6\x9C\xAA\xE8\xBF\x9E\xE6\x8E\xA5") != NULL,
                        "第三次双击 → 取消排序（排序态复位并回取服务端原序）");
        }
        saveShot(&g_win->m_base, "out/vfdshot_m3_logsort.png");
    }

    /* ==================== [G] 设置页(本地)点击流 ==================== */
    printf("[G] 设置页(本地)点击流\n");
    navClickNode(11);  /* 设置（收起数据组）。 */
    navClickNode(12);  /* 本地设置。 */
    pumpMs(200);
    {
        HsfSettingsLocal* set =
            (HsfSettingsLocal*)g_win->m_pageRoot[HSF_PAGE_SETTINGS_LOCAL];
        XSpinBox* fontHist = set ? &set->m_fontHistory : NULL;
        XSpinBox* refresh = set ? &set->m_refresh : NULL;
        int pct0 = set ? XSpinBox_value(fontHist) : 0;
        int ms0 = set ? XSpinBox_value(refresh) : 0;
        bool up = (pct0 < 200);
        expect_true(set != NULL, "设置页(本地)就位");
        /* [2026-11-13 三页合并] 合并页三分区齐全 + 两个分区根已建
         * （内容自原悬浮窗设置页/窗口设置页搬入，一个功能项不许丢）。 */
        expect_true(set &&
                    xstrCStr(XLabel_text(&set->m_secTitle[0])) != NULL &&
                    strcmp(xstrCStr(XLabel_text(&set->m_secTitle[0])),
                           "界面设置") == 0,
                    "合并页分区①标题=界面设置");
        expect_true(set &&
                    xstrCStr(XLabel_text(&set->m_secTitle[1])) != NULL &&
                    strcmp(xstrCStr(XLabel_text(&set->m_secTitle[1])),
                           "悬浮窗") == 0,
                    "合并页分区②标题=悬浮窗");
        expect_true(set &&
                    xstrCStr(XLabel_text(&set->m_secTitle[2])) != NULL &&
                    strcmp(xstrCStr(XLabel_text(&set->m_secTitle[2])),
                           "窗口与屏幕") == 0,
                    "合并页分区③标题=窗口与屏幕");
        expect_true(set && HsfSettingsLocal_overlaySection(
                    (XWidget*)set) != NULL,
                    "合并页分区②「悬浮窗」根已建（内容搬入）");
        expect_true(set && HsfSettingsLocal_windowSection(
                    (XWidget*)set) != NULL,
                    "合并页分区③「窗口与屏幕」根已建（内容搬入）");
        /* 分区②的关键开关真的在（已真兑现项不许当占位删掉）：显示总开关
           + FPS/帧耗时/CPU·GPU/网络/内存 五个行开关 + 固定/拖动/自适应
           三开关。 */
        {
            XWidget* ovSec = set ? HsfSettingsLocal_overlaySection(
                                      (XWidget*)set) : NULL;
            const char* ovKeys[] = {
                "显示悬浮窗", "FPS", "帧耗时", "CPU/GPU", "网络", "内存",
                "固定位置", "允许拖动", "自适应尺寸"
            };
            size_t ki;
            int missing = 0;
            for (ki = 0; ki < sizeof(ovKeys) / sizeof(ovKeys[0]); ++ki)
                if (!findCheckByText(ovSec, ovKeys[ki])) ++missing;
            expect_true(ovSec != NULL && missing == 0,
                        "「悬浮窗」分区九个已兑现开关全部在位");
        }
        /* 分区③的关键开关真的在：窗口框架五开关 + 唤醒方式三开关。 */
        {
            XWidget* wnSec = set ? HsfSettingsLocal_windowSection(
                                      (XWidget*)set) : NULL;
            const char* wnKeys[] = {
                "显示边框(标题栏)", "显示状态栏", "最大化显示", "全屏显示",
                "显示鼠标光标", "鼠标唤醒", "键盘唤醒", "触摸唤醒"
            };
            size_t ki;
            int missing = 0;
            for (ki = 0; ki < sizeof(wnKeys) / sizeof(wnKeys[0]); ++ki)
                if (!findCheckByText(wnSec, wnKeys[ki])) ++missing;
            expect_true(wnSec != NULL && missing == 0,
                        "「窗口与屏幕」分区八个已兑现开关全部在位");
        }
        /* 分区②一项「点了真生效」抽样：显示总开关翻转 → 悬浮层显隐跟随
           （兑现路径 = 悬浮层 getter + INI 落盘，合并后不许断链）。 */
        {
            XWidget* ovSec = set ? HsfSettingsLocal_overlaySection(
                                      (XWidget*)set) : NULL;
            XCheckBox* master = findCheckByText(ovSec, "显示悬浮窗");
            XPerformanceOverlay* overlay =
                (XPerformanceOverlay*)demo_main_overlay(g_win);
            bool before = overlay
                ? !XWidget_isHidden((XWidget*)overlay) : false;
            if (master && overlay) {
                XAbstractButton_setChecked((XAbstractButton*)master, !before);
                pumpMs(150);
                expect_true(
                    (XWidget_isHidden((XWidget*)overlay) == before) &&
                    HsfSettingsStore_getBool("Overlay/Visible", false) ==
                        !before,
                    "「显示悬浮窗」开关 → 悬浮层显隐生效且落盘");
                /* 复原。 */
                XAbstractButton_setChecked((XAbstractButton*)master, before);
                pumpMs(120);
            } else {
                expect_true(false,
                            "「显示悬浮窗」开关 → 悬浮层显隐生效且落盘");
            }
        }
        /* 字号缩放档位切换（历史数据页行）：SpinBox 箭头真实点击 →
           值变化 + 设置落盘键变化 + 状态栏回显。 */
        synSpinStep(fontHist, up);
        pumpMs(150);
        expect_true(XSpinBox_value(fontHist) == pct0 + (up ? 10 : -10),
                    "点字号 SpinBox 箭头 → 档位 ±10%");
        expect_true(HsfSettingsStore_getInt("FontScale/Page9", 100) ==
                        XSpinBox_value(fontHist),
                    "字号档变化已落盘（FontScale/Page9 键同步）");
        {
            const char* st = winStatusText();
            expect_true(st != NULL &&
                        strstr(st,
                            "\xE5\x8E\x86\xE5\x8F\xB2\xE6\x95\xB0\xE6\x8D\xAE\xE9\xA1\xB5\xE5\xAD\x97\xE4\xBD\x93") != NULL,
                        "状态栏回显「历史数据页字体：N%」");
        }
        /* 数据刷新周期：箭头真实点击 → Data/RefreshMs 键变化。 */
        synSpinStep(refresh, ms0 < 2000);
        pumpMs(150);
        expect_true(XSpinBox_value(refresh) == ms0 + (ms0 < 2000 ? 100 : -100),
                    "点刷新周期箭头 → ±100ms");
        expect_true(HsfSettingsStore_getInt("Data/RefreshMs", 200) ==
                        XSpinBox_value(refresh),
                    "刷新周期变化已落盘（Data/RefreshMs 键同步）");
        /* 还原（程序化回设初值，槽内同一路径落盘，不留探针残值）。 */
        XSpinBox_setValue(fontHist, pct0);
        XSpinBox_setValue(refresh, ms0);
        pumpMs(120);
        expect_true(HsfSettingsStore_getInt("FontScale/Page9", 100) == pct0 &&
                    HsfSettingsStore_getInt("Data/RefreshMs", 200) == ms0,
                    "探针收尾还原设置键（不污染现场 ini）");
    }
    saveShot(&g_win->m_base, "out/vfdshot_m4_settings.png");

    /* ==================== [H] 导航真实点击进变频器页（既有全量保持） ==================== */
    printf("[H] 变频器页（导航点击进入，既有断言全量保持）\n");
    navClickNode(1);   /* 监视（展开）。 */
    navClickNode(4);   /* 变频器。 */
    pumpMs(300);
    page = g_win->m_pageRoot[HSF_PAGE_MONITOR_VFD];
    expect_true(page != NULL, "变频器页根（主窗 builders 建）");
    expect_true(pg() != NULL, "页对象已登记（s_vfdPage）");
    expect_true(pg()->m_provider != NULL, "provider（demo 模拟源）已接入");
    /* 页面几何由堆栈宿主/滚动壳定尺：主窗 1280 宽 → 页根 ~1096 ≥ 620
       ⇒ HsfMonitorVfd_adapt 宽档判定与真机一致，无需探针再手工定尺。 */
    /* 等快照有效（自治剧本前几拍）。 */
    if (!waitUntil(pred_valid, 4000))
        printf("  [WARN] 4s 内快照未有效（继续，后续断言可能连带失败）\n");
    pumpMs(200);

    /* ==================== ① 初始态（手动模式；设备层闸缺省使能） ====================
     * [口径迁移 2026-11-10] 页面已移除「控制闸勾选框/下发钮」（下发改实时
     * 防抖、闸由 provider 投影）——本段对齐现页结构：闸闸相关断言迁移到
     * ②（provider 控制闸）与 ⑨（自动运行锁）。 */
    printf("[1] 初始态\n");
    expect_true(HsfMonitorVfd_interactiveMode(), "初始=手动模式（interactiveMode）");
    expect_true(!pg()->m_autoMode, "初始自动锁位清零");
    expect_true(XWidget_isEnabled((XWidget*)&pg()->m_btnRun), "启动钮可用");
    expect_true(XWidget_isEnabled((XWidget*)&pg()->m_dirCombo), "方向下拉可用");
    expect_true(XWidget_isEnabled((XWidget*)&pg()->m_speedMode), "调速方式下拉可用");
    expect_true(XWidget_isEnabled((XWidget*)&pg()->m_setSlider), "滑块可用");
    expect_true(XWidget_isEnabled((XWidget*)&pg()->m_setSpin), "SpinBox 可用");
    expect_true(!XWidget_isEnabled((XWidget*)&pg()->m_btnReset), "无故障时复位钮禁用");
    expect_true(!XWidget_isVisible((XWidget*)&pg()->m_btnJog),
                "ATV61 无点动能力 → 点动钮隐藏（caps 裁剪）");
    saveShot(&g_win->m_base, "out/vfdshot_1_initial.png");

    /* ==================== ② 控制闸联动（provider 控制闸） ====================
     * 现页的「闸」= provider.controlEnabled 投影（远端=gateOpen&&can()，
     * demo=设备 m_controlEnabled）。关闸 → 操作控件全禁 + 提示行说明；
     * 开闸 → 恢复 + 提示清空。经 provider 表驱动（生产同一入口）。 */
    printf("[2] 控制闸开/关联动（provider 闸）\n");
    if (pg()->m_provider && pg()->m_provider->setControlEnabled) {
        pg()->m_provider->setControlEnabled(pg()->m_provUser, false);
        pumpMs(300);   /* vfdRefresh 随页节拍（200ms）刷使能。 */
        expect_true(!XWidget_isEnabled((XWidget*)&pg()->m_btnRun), "关闸→启动钮禁用");
        expect_true(!XWidget_isEnabled((XWidget*)&pg()->m_dirCombo), "关闸→方向下拉禁用");
        expect_true(!XWidget_isEnabled((XWidget*)&pg()->m_setSlider), "关闸→滑块禁用");
        expect_true(!XWidget_isEnabled((XWidget*)&pg()->m_setSpin), "关闸→SpinBox 禁用");
        {
            const char* h = xstrCStr(XLabel_text(&pg()->m_hint));
            expect_true(h != NULL && h[0] != '\0', "关闸→提示行非空");
        }
        saveShot(&g_win->m_base, "out/vfdshot_2_locked.png");
        pg()->m_provider->setControlEnabled(pg()->m_provUser, true);
        pumpMs(300);
        expect_true(XWidget_isEnabled((XWidget*)&pg()->m_btnRun), "开闸→启动钮恢复");
        expect_true(XWidget_isEnabled((XWidget*)&pg()->m_setSlider), "开闸→滑块恢复");
        {
            const char* h = xstrCStr(XLabel_text(&pg()->m_hint));
            expect_true(h == NULL || h[0] == '\0', "开闸→提示行清空");
        }
    }

    /* ==================== ③ Slider↔SpinBox 双向同步 ====================
     * 同步经量纲换算（滑块恒 0.1Hz 定点，数字框随调速方式）：本段切到
     * 频率调速（Hz 档）后两者同单位，断言 0.1Hz 定点直传。 */
    printf("[3] 给定行滑块/SpinBox 同步（Hz 档）\n");
    XComboBox_setCurrentIndex(&pg()->m_speedMode, VFD_SPEED_MODE_HZ);
    pumpMs(50);
    expect_true(XComboBox_currentIndex(&pg()->m_speedMode) == VFD_SPEED_MODE_HZ,
                "调速方式切到频率调速（Hz）");
    /* 先挪到 430 破同值早退：遥测回写可能恰好把滑块停在 450（斜坡网格值）。 */
    XAbstractSlider_setValue((XAbstractSlider*)&pg()->m_setSlider, 430);
    XAbstractSlider_setValue((XAbstractSlider*)&pg()->m_setSlider, 450);
    expect_true(XSpinBox_value(&pg()->m_setSpin) == 45,
                "Slider→450(0.1Hz) ⇒ SpinBox=45 Hz（定点→用户量纲折算）");
    XSpinBox_setValue(&pg()->m_setSpin, 48);
    expect_true(XAbstractSlider_value((XAbstractSlider*)&pg()->m_setSlider) == 480,
                "SpinBox→48 Hz ⇒ Slider=480（0.1Hz 定点回环抑制）");

    /* ==================== ④ 给定实时下发（全链路写落定到假从站 LFR1） ====================
     * [口径迁移 2026-11-10] 「下发」钮已改实时防抖下发（200ms）：
     * 界面给定 → vfdScheduleApply → 设备层写 → 假从站 LFR1。
     * 停机态给定被「停机禁给定」闸跳过（随启动捎带）⇒ 本段等模拟源
     * 运行相位再下给定（自治剧本确定性循环：停→升→恒→降→停）。 */
    printf("[4] 给定实时下发全链路（运行相位）\n");
    {
        uint16_t before;
        bool landed = false;
        int waited = 0;
        /* 锁进恒速相位（确定性：≥6s 运行窗），避免防抖 200ms 后 apply
           落在停机窗被「随启动捎带」闸丢掉。给选定 460（46.0Hz：实时
           下发从数字框读整数 Hz，456 会被取整成 46→460，故直接用 460）。 */
        if (g_simInstance) {
            g_simInstance->m_phase = Phase_Run;
            g_simInstance->m_phaseMs = 0;
        }
        if (!waitUntil(pred_running, 4000))
            printf("  [WARN] 4s 内未进运行相位（④可能连带失败）\n");
        before = g_simInstance ? g_simInstance->m_lfr1 : 0;
        XAbstractSlider_setValue((XAbstractSlider*)&pg()->m_setSlider, 460);
        while (waited <= 2400 && !landed) {
            pumpMs(60);
            waited += 60;
            landed = g_simInstance && g_simInstance->m_lfr1 == 460;
        }
        expect_true(landed,
                    "运行中给定 46.0Hz → 假从站 LFR1 写落定（实时下发全链路）");
        printf("      (LFR1: %u -> %u，期望 460 = 46.0Hz@0.1Hz)\n",
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
        /* 强制停机待命窗内启动反转：reverse 位随命令字落定。
           [等遥测跟上] 相位清零后遥测还要一拍才报停（快照 valid+!running），
           设备层仍视"运行中"时点下拉会走「运行中换向」分支（命令立即写、
           随后的启动点击变成停止）⇒ 断言口径失真。先等停机态落地。 */
        if (g_simInstance) {
            g_simInstance->m_phase = Phase_Stopped;
            g_simInstance->m_phaseMs = 0;
        }
        if (!waitUntil(pred_notRunning, 6000))
            printf("  [WARN] 6s 内未回停机态（⑥可能连带失败）\n");
        pumpMs(120);
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
    saveShot(&g_win->m_base, "out/vfdshot_3_fault.png");
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

    /* ==================== ⑨ 自动运行锁 ====================
     * [口径迁移 2026-11-10] 勾选框已移除：锁的可见反馈=操作控件禁用 +
     * 横幅/提示行只读文案；provider 闸随 setInteractiveMode 同步翻转。 */
    printf("[9] 自动运行锁\n");
    HsfMonitorVfd_setInteractiveMode(false);
    pumpMs(50);
    expect_true(!HsfMonitorVfd_interactiveMode(), "进入自动：interactiveMode=false");
    expect_true(pg()->m_autoMode, "进入自动：页内自动锁位置位");
    expect_true(!XWidget_isEnabled((XWidget*)&pg()->m_btnRun), "自动：启动钮禁用");
    expect_true(!XWidget_isEnabled((XWidget*)&pg()->m_setSlider), "自动：给定滑块禁用");
    expect_true(!XWidget_isEnabled((XWidget*)&pg()->m_dirCombo), "自动：方向下拉禁用");
    {
        const char* h = xstrCStr(XLabel_text(&pg()->m_hint));
        expect_true(h != NULL && h[0] != '\0', "自动：提示行显示只读原因");
    }
    saveShot(&g_win->m_base, "out/vfdshot_4_automode.png");
    HsfMonitorVfd_setInteractiveMode(true);
    pumpMs(50);
    expect_true(HsfMonitorVfd_interactiveMode(), "退回手动：interactiveMode=true");
    expect_true(!pg()->m_autoMode, "退回手动：页内自动锁位清零");
    expect_true(XWidget_isEnabled((XWidget*)&pg()->m_btnRun), "手动：启动钮恢复");
    expect_true(XWidget_isEnabled((XWidget*)&pg()->m_setSlider), "手动：给定滑块恢复");

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
        saveShot(&g_win->m_base, "out/vfdshot_5_narrow.png");
        HsfMonitorVfd_adapt(page, false);     /* 宽档。 */
        {
            XPoint f = XWidget_pos(&pg()->m_gaugeFreq.m_canvas.m_base);
            XPoint c = XWidget_pos(&pg()->m_gaugeCur.m_canvas.m_base);
            printf("      (宽档几何: freq=(%d,%d) cur=(%d,%d))\n",
                   f.x, f.y, c.x, c.y);
            expect_true(c.x > f.x && c.y == f.y, "宽档：三仪表横排同高");
        }
    }

    /* ==================== [P] 导航切页耗时（性能回归锁）====================
     * 现场反馈（2026-11-18）：「导航菜单切换不同界面，有些界面明显卡顿
     * 一会儿才打开」。本段把一次切页拆成两段计时（离屏渲染，绝对值 ≠ 真机，
     * 但**同机同树**的相对关系可比）：
     *   · 同步段：真实点击导航 → hsf_goPage 返回（显隐切换 + 布局解算 +
     *     逐页 adapt + 导航面板重排 + 订阅迁移）。**这一段卡住 UI 线程**，
     *     用户体感就是「点下去没反应」。
     *   · 首帧段：新页那一帧的**整树绘制耗时**（框架口径
     *     XWidget_lastPaintTreeUsecs：paintTree 根调用两端的整树耗时，
     *     含全部可见子控件）——仪表页的静态层/进度带、趋势图都在这一段。
     * [段序] 放在探针**末尾**：本段会真实点击导航（改变导航树展开态），
     * 放在 [B] 段之前会破坏「初始导航树全收起」等初始态断言。
     */
    printf("[P] 导航切页耗时（同步段=卡 UI 的那段；首帧=整树绘制）\n");
    {
        const HsfNavNode* tree = HsfPages_navTree(NULL);
        int i;
        int worstSync = 0;
        int worstFrame = 0;
        char worstName[96];
        worstName[0] = '\0';
        for (i = 0; tree && i < g_nav->m_count; ++i) {
            int64_t t0;
            int64_t t1;
            int syncMs;
            int frameMs;
            const char* cap;
            if (!tree[i].caption || tree[i].pageIndex < 0) continue;
            cap = tree[i].caption;
            /* 展开父组（真实点击）：不计入切页耗时 */
            if (tree[i].childOf >= 0 &&
                g_nav->m_expanded != tree[i].childOf) {
                synClick((XWidget*)&g_nav->m_items[tree[i].childOf].m_btn);
                pumpMs(150);
            }
            t0 = XDateTime_currentNSecsSinceEpoch();
            synClick((XWidget*)&g_nav->m_items[i].m_btn);
            t1 = XDateTime_currentNSecsSinceEpoch();
            pumpMs(40);          /* 泵几帧，让新页真的画出来 */
            syncMs = (int)((t1 - t0) / 1000000);
            frameMs = (int)(XWidget_lastPaintTreeUsecs(&g_win->m_base) / 1000);
            printf("  页 %2d %-14s 同步 %4d ms   首帧 %4d ms\n",
                   tree[i].pageIndex, cap, syncMs, frameMs);
            /* [仪表页对照截图 2026-11-18] 绘制改动（静态层缓存 / 底盘换
             * 填充椭圆）必须**像素可比**：每次进这两个页各存一张，供改前
             * 改后逐像素对比。 */
            if (tree[i].pageIndex == 5 || tree[i].pageIndex == 6) {
                char shot[128];
                snprintf(shot, sizeof(shot), "out/vfdshot_p%d.png",
                         tree[i].pageIndex);
                saveShot(&g_win->m_base, shot);
            }
            if (syncMs > worstSync) {
                worstSync = syncMs;
                snprintf(worstName, sizeof(worstName), "%s", cap);
            }
            if (frameMs > worstFrame) worstFrame = frameMs;
        }
        printf("  最慢同步段 %d ms（%s）  首帧峰值 %d ms\n",
               worstSync, worstName, worstFrame);
        /* [性能门] 上限按「离屏实测档位 + 余量」定：真机更慢，但真机的
         * 慢主要是 CPU 与字体，不是「同步段做了多轮整树重解算」。回归锁
         * 的意义是：**切页不得回到「整树重解算 ×N 轮」的老路**（老路实测
         * 同步段可达 600ms）。 */
        expect_true(worstSync <= HSF_PROBE_SWITCH_SYNC_MS,
                    "切页同步段耗时在门限内（无多轮整树重解算）");
        /* [首帧门] 仪表页的绘制成本回归锁：2026-11-18 实测水质/流量计页
         * 单帧 229ms（静态层未缓存 + 弧带用径向线链），修复后 0.14ms。
         * 门限留足余量（真机更慢），但足以拦住「回到径向线链」的老路。 */
        expect_true(worstFrame <= HSF_PROBE_SWITCH_FRAME_MS,
                    "切页首帧整树绘制耗时在门限内（无径向线链式重画）");
    }

    /* ==================== 收尾：按 HsfUi_start 同序拆卸 ==================== */
    /* [2026-11-13] 拆卸前先冲刷 stdout：探针 stdout 在重定向时是块缓冲，
     * 若拆卸期崩溃（工作树现存退出期 UAF，见台账）已缓冲的断言行会一起丢
     * 失 ⇒ 回归结论不可复现。冲刷后「断言全跑完」与「拆卸是否崩」两件事
     * 可分别判定。 */
    fflush(stdout);
    XAbstractEventDispatcher_removePollCallback(g_win->m_framePump);
    hsfWinStopAllTimers(g_win);
    XClassDelete((XClass*)g_win);
    g_win = NULL;
    HsfLinkClient_close();          /* 恒离线：安全收口（账目对齐主程序）。 */
    HsfSettingsStore_close();       /* INI 落盘收尾。 */
    printf("      (合成点击缺句柄 %d 次)\n", s_synMiss);
    printf("=== HSF 客户端全界面真实点击回归探针结束：失败 %d 项 ===\n",
           s_failures);
    return s_failures == 0 ? 0 : 1;
}
