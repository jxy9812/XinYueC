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
#include "XHeaderView.h"
#include "XTableView.h"
#include "XListView.h"
#include "XWindowSystemInterface.h"   /* 真实鼠标事件注入入口 */
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
        synClick(&g_nav->m_items[tree[nodeIndex].childOf].m_btn);
        pumpMs(150);
    }
    synClick(&g_nav->m_items[nodeIndex].m_btn);
    pumpMs(150);
}

/* ---- 主界面卡片按标题查找：卡片 = 页根直接子件中类虚表为
 * HsfHomeCard 的容器（详情浮层也是 HsfHomeCard，显式排除）。 ---- */
static HsfHomeCard* homeCardByTitle(const char* titleUtf8)
{
    XVector* kids;
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
    expect_true(g_nav->m_count == 17, "导航树条目齐全（17 节点）");
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

    /* ==================== [B] 导航树逐页真实点击 ==================== */
    printf("[B] 导航树逐页点击\n");
    expect_true(g_nav->m_expanded == -1, "初始导航树全收起");
    navClickNode(1);   /* 监视（分组，纯展开不导航）。 */
    expect_true(g_nav->m_expanded == 1, "点「监视」就地展开子菜单（手风琴）");
    expect_true(!XWidget_isHidden(&g_nav->m_items[4].m_btn),
                "展开后「变频器」条目可见");
    navClickNode(4);   /* 变频器。 */
    expect_true(HsfStack_current(&g_win->m_stack) == HSF_PAGE_MONITOR_VFD,
                "点「变频器」堆栈切到变频器页");
    expect_true(XWidget_isVisible(g_win->m_pageRoot[HSF_PAGE_MONITOR_VFD]),
                "变频器页根可见");
    expect_true(XAbstractButton_isChecked(&g_nav->m_items[4].m_btn),
                "导航高亮：变频器条目勾选");
    {
        const char* st = winStatusText();
        expect_true(st != NULL && strstr(st, "\xE5\x8F\x98\xE9\xA2\x91\xE5\x99\xA8") != NULL,
                    "状态栏同步「当前页: 变频器」");
    }
    navClickNode(7);   /* 数据（分组）。 */
    expect_true(g_nav->m_expanded == 7, "点「数据」展开数据组");
    expect_true(XWidget_isHidden(&g_nav->m_items[4].m_btn),
                "单展开手风琴：监视组子项收起（变频器条目隐藏）");
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
    expect_true(XAbstractButton_isChecked(&g_nav->m_items[9].m_btn) &&
                !XAbstractButton_isChecked(&g_nav->m_items[8].m_btn),
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
        synClick(health);
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
        synClick(pump);
        pumpMs(150);
        expect_true(s_homePage->m_detailMode == HOME_DETAIL_PUMP,
                    "点搅拌泵卡 → 展开泵详情浮层");
        synClick(&s_homePage->m_floatClose);
        pumpMs(150);
        expect_true(s_homePage->m_detailMode == HOME_DETAIL_NONE,
                    "点浮层 × 钮收起泵详情");
        synClick(valve);
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
        synClick(&bar->m_btnChart);
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
        synClick(&bar->m_btnTable);
        pumpMs(150);
        expect_true(hist->m_viewMode == 0, "点「表格」钮切回表格视图");
        /* 列显隐下拉：展开 → 选行 → 收起，表格列显隐联动。 */
        expect_true(XComboBox_currentIndex(&bar->m_colFilter) == 0,
                    "列筛选缺省=全部列");
        synClick((XWidget*)&bar->m_colFilter);
        pumpMs(150);
        expect_true(XComboBox_popupVisible(&bar->m_colFilter),
                    "点列显隐下拉 → 弹层展开");
        synComboSelectRow(&bar->m_colFilter, 1);
        pumpMs(150);
        expect_true(!XComboBox_popupVisible(&bar->m_colFilter),
                    "点选列行后弹层收起");
        expect_true(XComboBox_currentIndex(&bar->m_colFilter) == 1,
                    "列筛选切到「列0」（下拉第 1 行）");
        expect_true(XTableView_isColumnHidden(&hist->m_table.m_base, 1),
                    "列1 随列筛选隐藏（本地重铺显隐）");
        synClick((XWidget*)&bar->m_colFilter);
        pumpMs(120);
        synComboSelectRow(&bar->m_colFilter, 0);
        pumpMs(150);
        expect_true(XComboBox_currentIndex(&bar->m_colFilter) == 0 &&
                    !XTableView_isColumnHidden(&hist->m_table.m_base, 1),
                    "点回「全部列」恢复列显隐");
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
        synClick(&bar->m_btnReload);
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

    /* ==================== 收尾：按 HsfUi_start 同序拆卸 ==================== */
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
