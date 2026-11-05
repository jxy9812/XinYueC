/**
 * @file       HsfUi.c
 * @brief      客户端主窗组装与启动入口（装配壳）。
 * @details    三段式主窗（仿 XGuiWindowDemo 布局化 chrome）：第一行标题栏
 *             （HsfTitleBar）+ 中段内容区 + 底部黑色状态栏（HsfStatusBar），
 *             根容器垂直布局。中段=框架分割条（XSplitter）：宽屏形态左
 *             停靠导航面板（HsfNavPanel，拖拽把手可改宽）+ 内容堆栈
 *             （HsfStack）；窄屏（竖屏）形态强制收起侧栏，导航改由
 *             主菜单页承载（☰ 打开）——同 XGuiWindowDemo 的形态口径。
 *             页面经 HsfPages 注册表驱动的 builders 表逐页创建挂栈
 *             （堆叠下标=页索引）；本模块只持有页序语义与形态策略。
 */
#include "HsfUi.h"


/* 前置声明：模式回调（实现见文件后部，建窗时注册）。 */
static void hsfWinModeSlot(void* userData);
static void hsfWinModeNotify(void* userData);
static void hsfWinHealthNotify(void* userData);

#include "HsfPages.h"
#include "HsfTheme.h"
#include "HsfTitleBar.h"
#include "HsfStatusBar.h"
#include "HsfStack.h"
#include "HsfNavPanel.h"
#include "HsfDemoCompat.h"
#include "HsfSettingsStore.h"
#include "HsfLogin.h"
#include "HsfLinkClient.h"
#include "XhsfLinkProto.h"
#include "HsfVfdProviderRemote.h"
#include "HsfHomePage.h"
#include "HsfMainMenu.h"
#include "HsfMonitorMenu.h"
#include "HsfMonitorDigital.h"
#include "HsfMonitorAnalog.h"
#include "HsfMonitorVfd.h"
#include "HsfMonitorFlowMeter.h"
#include "HsfMonitorValve.h"

/* 前置声明：数字量/模拟量/电磁阀页回调（实现见文件后部，建窗时注册）。
 * [必须在 include 之后] 形参用到 HsfDigitalState 等页面类型。 */
static void hsfWinValveSync(void* userData);
static void hsfWinValveWrite(void* userData, int index, bool wantOpen);
static void hsfWinCommandResult(void* userData, int op, bool ok,
                                const char* errUtf8);
static void hsfWinDigitalSync(void* userData);
static void hsfWinDigitalWrite(void* userData, int index, HsfDigitalState target);
static void hsfWinAnalogSync(void* userData);
#include "HsfDataMenu.h"
#include "HsfDataHistory.h"
#include "HsfDataTable.h" /* HsfDataTablePage_adapt（页 7/8/9 共用钩子）。 */
#include "HsfDataLog.h"
#include "HsfDataAlarm.h"
#include "HsfSettingsMenu.h"
#include "HsfSettingsLocal.h"
#include "HsfSettingsRemote.h"
#include "HsfNetworkPage.h"
#include "HsfOverlayPage.h"
#include "HsfWindowPage.h"
#include "XGuiApplication.h"
#include "XCoreApplication.h"
#include "XAbstractEventDispatcher.h"
#include "XWidget.h"
#include "XBoxLayout.h"
#include "XScrollArea.h"
#include "XScrollBar.h"          /* 切页滚动复位用。 */
#include "XFrame.h"
#include "XSplitter.h"
#include "XSplitterHandle.h"
#include "XPerformanceOverlay.h"
#include "XDateTime.h"
#include "XDeviceNetwork.h"
#include "XWindowDecoration.h" /* CSD 系统标题栏保留边距（根布局顶边让位） */
#include "XScreen.h"           /* 主屏几何（缺省窗口尺寸收敛用） */
#include "XPlatformScreen.h"   /* 平台屏幕契约（Windows 无 XScreen 时的退路） */
#include "XImage.h"
#include "XSystem.h"         /* 环境变量钩子（自测触发）。 */
#include "XWidget_Protected.h"
#include "XPrintf.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

/* ---- 页面 builders 表（堆叠下标=页索引；HsfPageIndex 同序） ---- */
static const HsfPageCreateFn kHsfBuilders[HSF_PAGE_COUNT] = {
    HsfHomePage_create,        /* 0  主界面 */
    HsfMainMenu_create,        /* 1  主菜单 */
    HsfMonitorMenu_create,     /* 2  监视 子菜单 */
    HsfMonitorDigital_create,  /* 3  监视-数字量 */
    HsfMonitorAnalog_create,   /* 4  监视-模拟量 */
    HsfMonitorVfd_create,      /* 5  监视-变频器 */
    HsfMonitorFlowMeter_create,/* 6  监视-电磁流量计（与变频器同级） */
    HsfMonitorValve_create,    /* 7  监视-电磁阀 */
    HsfDataMenu_create,        /* 7  数据 子菜单 */
    HsfDataHistory_create,     /* 8  数据-历史数据 */
    HsfDataLog_create,         /* 9  数据-日志 */
    HsfDataAlarm_create,       /* 10 数据-报警 */
    HsfSettingsMenu_create,    /* 11 设置 子菜单 */
    HsfSettingsLocal_create,   /* 12 设置-本地设置 */
    HsfSettingsRemote_create,  /* 13 设置-远端设置 */
    HsfNetworkPage_create,     /* 14 设置-网络设置（移植） */
    HsfOverlayPage_create,     /* 15 设置-悬浮窗设置（移植） */
    HsfWindowPage_create       /* 16 设置-窗口设置（移植） */
};

/* ---- 形态/导航度量（单一事实源） ---- */
#define HSF_COMPACT_MAX_W   600  /**< 紧凑断点（逻辑宽 < 600 ⇒ 竖屏形态）。 */
#define HSF_NAV_MIN_W       120  /**< 导航面板最小宽（拖拽下限）。 */
#define HSF_NAV_DEFAULT_W   176  /**< 导航面板缺省宽（拖拽记忆初值）。 */
#define HSF_SPLITTER_HANDLE_W 8  /**< 分割条把手宽（拖拽区）。 */
#define HSF_OVERLAY_MARGIN    0  /**< 悬浮层贴边余量（0=完全贴内容区边）。 */

/* ---- 主窗缺省尺寸（横屏基准） ---- */
#define HSF_WIN_DEFAULT_W  1280  /**< 主窗缺省宽（横屏基准，16:9）。 */
#define HSF_WIN_DEFAULT_H   720  /**< 主窗缺省高（横屏基准，16:9）。 */
#define HSF_WIN_SCREEN_PCT   90  /**< 屏内占比上限（余量留给边框/任务栏）。 */

/* ---- 移植页的窄宽/竖屏适配钩子（页根=build 返回值） ---- */
typedef void (*HsfAdaptFn)(XWidget* page);

/** 移植页 adapt 包装（移植页原 adapt 无 compact 参数，补齐统一契约）。 */
static void hsfAdaptNetwork(XWidget* page, bool compact)
{
    (void)compact;
    hsf_page_network_adapt(page);
}
static void hsfAdaptOverlay(XWidget* page, bool compact)
{
    (void)compact;
    hsf_page_overlay_adapt(page);
}
static void hsfAdaptWindow(XWidget* page, bool compact)
{
    (void)compact;
    hsf_page_window_adapt(page);
}

/** 页级 adapt 表（按页索引；NULL=该页无需自适应）。 */
static const HsfPageAdaptFn kHsfPageAdapt[HSF_PAGE_COUNT] = {
    HsfHomePage_adapt,   /* 0  主界面（横屏一行/竖屏一列） */
    NULL, NULL,
    HsfMonitorDigital_adapt, /* 3 监视-数字量（竖屏窄列/触控高/顶行堆叠） */
    HsfMonitorAnalog_adapt,  /* 4 监视-模拟量（竖屏窄列/触控高） */
    HsfMonitorVfd_adapt, /* 5  监视-变频器（仪表横排/纵排） */
    HsfMonitorFlowMeter_adapt, /* 6 监视-电磁流量计（仪表横排/纵排） */
    HsfMonitorValve_adapt,     /* 7 监视-电磁阀（3 列/1 列） */
    NULL,
    HsfDataTablePage_adapt, /* 8  数据-历史数据（竖屏收边距） */
    HsfDataTablePage_adapt, /* 9  数据-日志（竖屏收边距） */
    HsfDataTablePage_adapt, /* 10 数据-报警（竖屏收边距） */
    NULL, NULL,
    hsfAdaptNetwork,     /* 14 网络设置 */
    hsfAdaptOverlay,     /* 15 悬浮窗设置 */
    hsfAdaptWindow       /* 16 窗口设置 */
};

/** 移植页在页索引中的起始位置（= HSF_PAGE_SETTINGS_NETWORK）。 */
#define HSF_ADAPT_FIRST HSF_PAGE_SETTINGS_NETWORK

/* ==================== 主窗 ==================== */

/** @brief 主窗对象：m_base 顶层窗口 + 三段根布局 + 中段分割条。 */
typedef struct HsfWin
{
    XWidget      m_base;          /**< 顶层窗口；必须是第一个成员。 */
    HsfTitleBar  m_titleBar;      /**< 第一行：标题栏（☰ + 标题）。 */
    XWidget      m_midHost;       /**< 中段宿主（伸展，托管分割条）。 */
    XBoxLayout   m_midLayout;     /**< 中段布局（托管分割条）。 */
    XSplitter    m_splitter;      /**< 中段分割条（导航面板 | 内容堆栈）。 */
    HsfNavPanel  m_navPanel;      /**< 左侧停靠导航面板（宽屏侧栏）。 */
    HsfNavPanel  m_mobilePanel;   /**< 竖屏菜单页的面板（移动态树）。 */
    XBoxLayout   m_mobileBox;     /**< 菜单页容器布局（成员化：栈上局部
                                    *   布局对象会成悬垂指针→激活期崩）。 */
    HsfStack     m_stack;         /**< 内容堆栈（多页切换）。 */
    HsfStatusBar m_statusBar;     /**< 底部：黑色状态栏。 */
    XBoxLayout   m_rootLayout;    /**< 根垂直布局（三段）。 */
    XPerformanceOverlay m_overlay;/**< 性能悬浮层（悬浮窗设置页对象）。 */
    bool         m_overlayPinned;     /**< 悬浮层已被用户显式定位（预设/
                                    *   拖动）→ 挂起 resize 自动重锚（demo
                                    *   同款 m_overlayPinned 口径）。 */
    int          m_overlayAnchorW;    /**< 上次锚定时的浮层宽（autoFit 变
                                    *   化后经帧泵尺寸差检测触发重锚）。 */
    int          m_overlayAnchorH;    /**< 上次锚定时的浮层高。 */
    bool         m_navCollapsed;      /**< 导航面板收起态（当前生效）。 */
    bool         m_navCollapsedUser;  /**< 用户手动收起态（紧凑态另记）。 */
    int          m_layoutMode;    /**< 形态：0=宽屏 1=紧凑。 */
    int          m_navWidth;      /**< 导航面板记忆宽（拖拽更新，px）。 */
    XTimerId     m_clockTimer;   /**< 标题栏时钟 1s 刷新定时器。 */
    int64_t      m_homeRefreshMs;/**< 主界面数值块上次刷新时刻（帧泵节流用）。 */
    XTimerId     m_autoQuitTimer; /**< 自动退出定时器（>0 秒参数时启用）。 */
    int          m_autoSeconds;   /**< 剩余自动退出秒数（0=常驻）。 */
    XWidget*     m_pageRoot[HSF_PAGE_COUNT]; /**< 各页页根（供 adapt 用）。 */
    XWidget*     m_pageScroll[HSF_PAGE_COUNT]; /**< 各页的滚动壳（切页时复位滚动位用；可 NULL）。 */
    int          m_pageBeforeMenu; /**< 打开菜单页前的原页（紧凑态 ☰
                                    *   菜单开合用；形态切回宽屏时据此
                                    *   退出菜单页——同 demo 的
                                    *   m_pageBeforeMenu 口径）。 */
    int64_t      m_lastPaintUsecs;     /**< 上次绘制时刻（喂悬浮层帧率）。 */
    int64_t      m_lastFrameUsecs;      /**< 上拍时刻（轮询节拍）。 */
    int64_t      m_lastNetworkPollUsecs;/**< 上次网络计数采样时刻。 */
    XHandle      m_framePump;      /**< 帧泵句柄（几何刷新漏斗）。 */
    int          m_thenPage;       /**< 出图前切到的目标页（-1=不切）。 */
    bool         m_uiReady;        /**< UI 装配就绪（resize 事件的前置门：
                                    *  建窗初期的 resize 会早于侧栏/分割条
                                    *  装配，形态判定会打在零内存对象上）。 */
    const char*  m_shotPath;       /**< 离屏截图输出路径（NULL=不截图）。 */
    XTimerId     m_shotTimer;      /**< 截图定时器（渲染数帧后出图）。 */
    int          m_shotWaitTicks;  /**< 截图前等待遥测的轮询计数（上限兜底）。 */
    int          m_subTopic;       /**< 当前已订阅的数据主题（HsfLinkTopic；
                                    *   -1=无：当前页不吃遥测）。 */
    XTimerId     m_cycleTimer;     /**< 页轮转自测定时器（按需订阅验证用）。 */
    int          m_cyclePage;      /**< 轮转中的当前页。 */
} HsfWin;

XCLASS_DEFINE_BEGING(HsfWin)
XCLASS_DEFINE_EXTEND_END(HsfWin, XWidget)

/* ---- 重绘辅助（脏区残留治理） ---- */

/** @brief 整窗刷新：标脏全窗，强制重绘。
 *  @details 布局变更（侧栏显隐/分割条改宽/切页）后，新增暴露的区域
 *           不在旧脏区内 → 留旧像素（实测：侧栏展开后内容区顶部残留
 *           标题栏蓝带）。整窗标脏一次即可覆盖（同 XGuiWindowDemo
 *           切页后的整页刷新口径）。 */
static void hsf_fullRepaint(HsfWin* self)
{
    XRect full;
    if (!self) return;
    XRect_init(&full, 0, 0, XWidget_width(&self->m_base),
               XWidget_height(&self->m_base));
    XWidget_updateRect(&self->m_base, &full);
}

/** @brief 触发全部已建移植页的 adapt（前置声明；定义在本文件后段）。 */
static void hsf_adaptPages(HsfWin* self);

/* ---- 悬浮层锚定（右下，避开标题栏与状态栏） ---- */

/** @brief 把性能悬浮层锚到内容区右下角（状态栏之上）。
 *  @details 悬浮层是主窗子控件，默认位置在左上会压住标题栏；这里按
 *           「内容区右下、距边 8px」定位（对齐 demo 的右下重锚口径）。 */
static void hsf_overlayAnchor(HsfWin* self)
{
    XRect geo;
    int bottomLimit;
    int targetX;
    int targetY;
    if (!self) return;
    /* 用户显式定位（预设/拖动）后不再自动重锚——否则下拉切换的位置会被
     * 立刻拽回右下（用户反馈「切换位置都是偏的」）。 */
    if (self->m_overlayPinned) return;
    geo = XPerformanceOverlay_geometry(&self->m_overlay);
    if (geo.width <= 0 || geo.height <= 0) return;
    /* 内容区（中段宿主）右下角：**贴边**（余量 0）——底部对齐内容区底、
     * 右侧对齐内容区右。 */
    bottomLimit = XWidget_y(&self->m_midHost)
                  + XWidget_height(&self->m_midHost) - HSF_OVERLAY_MARGIN;
    targetX = XWidget_x(&self->m_midHost) + XWidget_width(&self->m_midHost)
              - geo.width - HSF_OVERLAY_MARGIN;
    targetY = bottomLimit - geo.height;
    if (targetX != geo.x || targetY != geo.y) {
        /* autoFit 收框只改尺寸不动位置 → 位置变更后旧矩形残着上一帧
         * 像素（边缘残留半个字形，demo 真机同款账）。新旧矩形各投一次
         * 更新：旧位残迹清除、新位随即绘上。 */
        XPerformanceOverlay_setPosition(&self->m_overlay, targetX, targetY);
        XWidget_updateRect(&self->m_base, &geo);
        {
            XRect moved = XPerformanceOverlay_geometry(&self->m_overlay);
            XWidget_updateRect(&self->m_base, &moved);
        }
    }
    /* 记录锚定时尺寸，供帧泵按尺寸差检测（autoFit 变宽/变高触发重锚）。 */
    self->m_overlayAnchorW = geo.width;
    self->m_overlayAnchorH = geo.height;
}

/* ---- 页序语义（标题栏/状态栏/导航高亮随页同步） ---- */

/**
 * @brief 页索引 → 该页需要的遥测主题。
 * @return 主题枚举；页面不吃遥测返回 -1。
 * @note  只有**显示中的页**才订阅其主题：隐藏页不占带宽、不解码、不
 *        刷新（服务端不再投递）。顶栏/状态栏用的温湿度与设备健康是
 *        常驻主题（不在此表，由 HsfLinkClient 连接期恒订阅）。
 */
static int hsf_pageTopicOf(int pageIndex)
{
    switch (pageIndex) {
    case HSF_PAGE_MONITOR_VFD:       return (int)HsfLinkTopic_Vfd;
    case HSF_PAGE_MONITOR_ANALOG:    return (int)HsfLinkTopic_Ai;
    case HSF_PAGE_MONITOR_FLOWMETER: return (int)HsfLinkTopic_FlowMeter;
    case HSF_PAGE_MONITOR_VALVE:     return (int)HsfLinkTopic_Valve;
    default:                         return -1;
    }
}

/**
 * @brief 页数据订阅随切页迁移（旧页退订 → 新页订阅）。
 * @note  服务端对**新订阅**立即定向补发一拍，故切到某页立即可见数据，
 *        不必等推送周期。同一主题重复进入不重复加减（引用计数）。
 */
static void hsf_pageTopicSwitch(HsfWin* self, int index)
{
    const int next = hsf_pageTopicOf(index);
    if (!self) return;
    if (next == self->m_subTopic) return;
    if (self->m_subTopic >= 0)
        HsfLinkClient_unsubscribe((HsfLinkTopic)self->m_subTopic);
    if (next >= 0) {
        if (!HsfLinkClient_subscribe((HsfLinkTopic)next))
            XPrintf("HsfClient: 页面主题订阅未生效（未登录？page=%d）\n", index);
    }
    self->m_subTopic = next;
}

/** @brief 切页：堆叠当前页 + 标题栏标题 + 状态栏文本 + 导航高亮。 */
static void hsf_goPage(HsfWin* self, int index)
{
    char buf[96];
    if (!self) return;
    if (index < 0) index = 0;
    if (index >= HSF_PAGE_COUNT) index = HSF_PAGE_COUNT - 1;
    HsfStack_go(&self->m_stack, index);
    snprintf(buf, sizeof(buf), "当前页: %s", HsfPages_name(index));
    HsfStatusBar_setText(&self->m_statusBar, buf);
    HsfNavPanel_setActivePage(&self->m_navPanel, index);
    /* [2026-11-05 切页滚动复位] 滚动壳会记住上次停在哪。实测：窄屏电磁阀页滞留在
     * 底部，切过去只看得到「电磁阀6」，前面 5 张卡片全在视口上方。
     * 切页就应从头看。 */
    if (index >= 0 && index < HSF_PAGE_COUNT && self->m_pageScroll[index]) {
        /* [用滚动条 value 而不是 ensureVisible] ensureVisible 在切页当拍
         * 不生效：那时新页刚被 show、视口/内容几何尚未定，
         * 可见区计算出来是空的。直接把竖向滚动条置 0 最确定。 */
        XScrollBar* vsb = XAbstractScrollArea_verticalScrollBar(
            (XAbstractScrollArea*)self->m_pageScroll[index]);
        if (vsb) XScrollBar_setValue(vsb, 0);
    }
    /* 按需订阅迁移（隐藏页退订，新页订阅即补发首帧）。 */
    hsf_pageTopicSwitch(self, index);
    /* 移植页窄宽分档：切页后页根宽度更新 → 重新分档。 */
    hsf_adaptPages(self);
    /* 新页控件未覆盖处会露出旧页像素（叠印）→ 切页后整窗刷新。 */
    hsf_fullRepaint(self);
    XPrintf("HsfClient: switch page=%d (%s)\n", index, HsfPages_name(index));}
/** @brief 页面导航回调（HsfMenuNavFn 契约：透传页索引）。 */static void hsf_navToPage(void* userData, int index){    hsf_goPage((HsfWin*)userData, index);}
/** @brief 全局导航入口（HsfPages_gotoPage 契约；移植页返回钮用）。 */void HsfPages_gotoPage(void* user, int index){    hsf_navToPage(user, index);}
/** @brief 状态栏文本反馈（HsfPages_setStatus 契约；移植页状态行用）。 */void HsfPages_setStatus(void* user, const char* text){    HsfWin* win = (HsfWin*)user;
    if (!win || !text) return;    HsfStatusBar_setText(&win->m_statusBar, text);}
/* ---- 移植页自适应（窄宽分档/竖屏排布） ---- *//** @brief 触发全部已建移植页的 adapt（窄宽分档即时生效）。 *  @details 移植页的窄宽档按**页根宽度**判定；页根几何在切页/形态 *           切换后才会更新，故每次布局变化后统一调一遍（同 *           XGuiWindowDemo demo_layout_content 的 adapt 漏斗口径）。 *           未建/几何未定（宽 0）的页由 adapt 自身跳过。 */static void hsf_adaptPages(HsfWin* self){
    int i;
    bool compact;
    if (!self) return;    compact = (self->m_layoutMode == 1);
    for (i = 0; i < HSF_PAGE_COUNT; ++i) {    
    if (self->m_pageRoot[i] && kHsfPageAdapt[i])            kHsfPageAdapt[i](self->m_pageRoot[i], compact);
    }}
/* ---- 形态策略（宽屏/竖屏） ---- *//** @brief 当前形态：0=宽屏（侧栏停靠）1=紧凑（竖屏，强制收侧栏）。 */static int hsf_layoutModeOf(HsfWin* self){
    if (!self) return 0;    return (XWidget_width(&self->m_base) < HSF_COMPACT_MAX_W) ? 1 : 0;}
/** @brief 导航面板尺寸意图重申（setSizes 为 px 意图；每轮重申防漂移）。 */static void hsf_navApplySizes(HsfWin* self){
    int total;
    int navSize;
    int sizes[2];
    if (!self) return;    total = XWidget_width((XWidget*)&self->m_splitter);    total -= HSF_SPLITTER_HANDLE_W * (XSplitter_count(&self->m_splitter) - 1);
    if (total < 128) total = 128;    navSize = self->m_navCollapsed ? 0 : self->m_navWidth;
    if (navSize > total - 72) navSize = total - 72;
    if (navSize < 0) navSize = 0;    sizes[0] = navSize;    sizes[1] = total - navSize;
    /* 非收起态两页意图均不得 ≤0（0 宽会被读成收起签名）。 */
    if (!self->m_navCollapsed) {    
    if (sizes[0] < 1) sizes[0] = 1;    
    if (sizes[1] < 1) sizes[1] = 1;
    }    XSplitter_setSizes(&self->m_splitter, sizes, 2);
    /* 面板宽度随分割条变化 → 条目几何重排（demo 侧栏同款口径）。 */    HsfNavPanel_update(&self->m_navPanel);}
/** @brief 形态切换：宽屏↔紧凑（侧栏显隐/把手宽度/用户收起态还原）。 */static void hsf_layoutModeUpdate(HsfWin* self){
    int mode;
    if (!self) return;    mode = hsf_layoutModeOf(self);
    if (mode == self->m_layoutMode) return;    self->m_layoutMode = mode;
    if (mode == 1) {    
    /* 紧凑（竖屏）：侧栏强制收起且把手归零（8px 细条占比过大、         * 拖拽会把侧栏拽出来）；导航改由主菜单页承载。 */        self->m_navCollapsedUser = self->m_navCollapsed;        self->m_navCollapsed = true;        XSplitter_setHandleWidth(&self->m_splitter, 0);        XWidget_hide((XWidget*)&self->m_navPanel.m_base);
    } else {    
    /* 宽屏：还原（含用户手动收起态——紧凑态强制收起属形态策略，         * 不该覆盖用户意图，故单独记一份用户值）。 */        self->m_navCollapsed = self->m_navCollapsedUser;        XSplitter_setHandleWidth(&self->m_splitter, HSF_SPLITTER_HANDLE_W);    
    if (self->m_navCollapsed)            XWidget_hide((XWidget*)&self->m_navPanel.m_base);        else            XWidget_show((XWidget*)&self->m_navPanel.m_base);
    }
    /* 紧凑→宽屏：若当前停在竖屏菜单页，退出它回原页——否则宽屏下       竖屏菜单（移动态树）会与左侧树/内容区同屏残留（同 demo       demo_layoutModeUpdate 的菜单页复位口径）。 */
    if (mode == 0 && HsfStack_current(&self->m_stack) == HSF_PAGE_MENU) {    
    int back = self->m_pageBeforeMenu;    
    if (back < 0 || back >= HSF_PAGE_COUNT || back == HSF_PAGE_MENU)            back = HSF_PAGE_HOME;        hsf_goPage(self, back);
    }    hsf_navApplySizes(self);
    /* 形态切换 → 移植页宽度跨档 → 重新分档（竖屏适配）。 */    hsf_adaptPages(self);    hsf_fullRepaint(self);    XPrintf("HsfClient: layout mode=%d (%s)\n", mode,            mode == 1 ? "紧凑/竖屏" : "宽屏\n");
}

/** @brief ☰ 回调：宽屏=折叠/展开左侧导航面板；紧凑=开合主菜单页。 */
static void hsf_menuToggleCb(void* userData)
{
    HsfWin* win = (HsfWin*)userData;
    if (!win) return;
    if (hsf_layoutModeOf(win) == 1) {
        /* 紧凑形态：☰ 开合主菜单页（导航由菜单页承载）。
           开菜单前记住原页，关闭时回原页（同 demo m_pageBeforeMenu）。 */
        if (HsfStack_current(&win->m_stack) == HSF_PAGE_MENU) {
            int back = win->m_pageBeforeMenu;
            if (back < 0 || back >= HSF_PAGE_COUNT) back = HSF_PAGE_HOME;
            hsf_goPage(win, back);
        } else {
            win->m_pageBeforeMenu = HsfStack_current(&win->m_stack);
            hsf_goPage(win, HSF_PAGE_MENU);
        }
        return;
    }
    /* 宽屏形态：☰ 折叠/展开左侧导航面板（内容区随之伸展）。 */
    win->m_navCollapsed = !win->m_navCollapsed;
    win->m_navCollapsedUser = win->m_navCollapsed;
    if (win->m_navCollapsed)
        XWidget_hide((XWidget*)&win->m_navPanel.m_base);
    else
        XWidget_show((XWidget*)&win->m_navPanel.m_base);
    hsf_navApplySizes(win);
    hsf_fullRepaint(win); /* 侧栏显隐后内容区尺寸变化 → 整窗刷新防残留。 */
}

/** @brief 分割条拖拽：更新导航面板宽度记忆（仅真实拖拽触发）。 */
static void hsf_splitterMovedSlot(XObject* receiver, XVarList* args)
{
    HsfWin* win = (HsfWin*)receiver;
    int pi;
    int sizes[2];
    (void)args;
    if (!win) return;
    pi = XSplitter_indexOf(&win->m_splitter,
                           (XWidget*)&win->m_navPanel.m_base);
    if (pi < 0) return;
    XSplitter_sizes(&win->m_splitter, sizes, 2); /* 出参=子控件实际 px 宽。 */
    if (sizes[pi] < HSF_NAV_MIN_W) {
        win->m_navWidth = HSF_NAV_MIN_W;
        return;
    }
    win->m_navWidth = sizes[pi];
}

/** @brief 截图执行（前置声明；定义在本文件后段）。 */
static void hsfShotCapture(HsfWin* self);

/**
 * @brief 把远端温湿度遥测同步到标题栏。
 * @param self 主窗。
 * @details 数据来源 = HsfLinkClient 订阅的 HSF_TOPIC_TEMPHUM_STATUS
 *          （服务端按 HSF_TEMPHUM_PUSH_MS 周期发布，A/B 两侧各一份）；
 *          标题栏显示 **A 侧**（主用侧）。未登录/未收到遥测时显示"--"。
 *          单位换算在此集中一次：快照是 0.1 定点（219=21.9℃、-250=-25.0℃），
 *          界面按摄氏度/百分比显示，故统一除 10。
 * @note  两个触发点：①温湿度推送回调（到达即刷，零延迟）；②时钟定时器
 *        同拍（兜底——推送停摆时也能把"无数据"状态刷出来）。
 */
/**
 * @brief 把远端温湿度遥测同步到标题栏。
 * @param self 主窗。
 * @details 数据来源 = HsfLinkClient 订阅的 HSF_TOPIC_TEMPHUM_STATUS
 *          （服务端按 HSF_TEMPHUM_PUSH_MS 周期发布，A/B 两侧各一份）；
 *          标题栏显示 **A 侧**（主用侧）。未登录/未收到遥测时显示"--"。
 *          单位换算在此集中一次：快照是 0.1 定点（219=21.9℃、-250=-25.0℃），
 *          界面按摄氏度/百分比显示，故统一除 10。
 * @note  [新鲜度] 用 HsfLinkClient_tempHumFresh 判实时性，不直读
 *        m_received——断线后推送停止但 m_received 仍是 true，只判它会
 *        把最后一拍的旧值当实时数据显示（2026-11-04 实锅）。
 *        两个触发点：①推送回调（到达即刷）；②时钟定时器同拍（兜底，
 *        让"断流成 --"能在没有推送时也刷出来）。
 */
static void hsf_titleSyncClimate(HsfWin* self)
{
    const HsfLinkTempHum* th;
    if (!self) return;
    th = HsfLinkClient_tempHum(HSF_SIDE_A);
    if (!th || !HsfLinkClient_tempHumFresh(HSF_SIDE_A)) {
        HsfTitleBar_setClimate(&self->m_titleBar, 0.0, 0.0, false, false);
        return;
    }
    HsfTitleBar_setClimate(&self->m_titleBar,
                           (double)th->m_tempC10 / 10.0,
                           (double)th->m_humPct10 / 10.0,
                           th->m_tempValid, th->m_humValid);
}

/** @brief 温湿度推送回调（HsfLinkClient）：到达即刷新标题栏。 */
static void hsf_onTempHumPushed(void* userData)
{
    hsf_titleSyncClimate((HsfWin*)userData);
}

/** @brief 截图等待遥测的轮询步长（毫秒）。 */
#define HSF_SHOT_WAIT_STEP_MS   250
/** @brief 截图等待遥测的最大轮询次数（250ms × 40 = 10s 上限）。 */
#define HSF_SHOT_WAIT_MAX_TICKS 40

/**
 * @brief 截图前的数据就绪判定（按当前页关心的数据源）。
 * @return 当前页关心的数据已到达返回 true；等待超时上限由调用方控制。
 * @note 登录态窗口在链路打开**之前**就已创建（无头登录的窗口在登录成功后
 *       才装配），故不能用 isOpen 判定"要不要等"——统一按"有没有数据"
 *       判断，未登录（离线演示）永远无数据，靠调用方的轮询上限兜底出图。
 * @note [2026-11-04 流量计页实锅] 原来只判 A 侧温湿度，截流量计页时
 *       A 侧温湿度先到就立刻出图，流量计（B 侧）数据还没来 —— 页面截成
 *       "无数据"。改为按当前页取对应数据源：流量计页等 B 侧流量计，
 *       其余页维持原判据（A 侧温湿度）。
 */
static bool hsfShotDataReady(HsfWin* self)
{
    if (!self) return false;
    if (self && HsfStack_current(&self->m_stack) == HSF_PAGE_MONITOR_FLOWMETER)
        return HsfLinkClient_flowMeterFresh(HSF_SIDE_B);
    return HsfLinkClient_tempHumFresh(HSF_SIDE_A);
}

/* ---- 窗口事件（自动退出 / 关闭退出 / 尺寸变化） ---- */

/** @brief TimerEvent：自动退出倒计时（autoExitSeconds>0 时启用）。 */
static void VHsfWin_timerEvent(XObject* object, XTimerEvent* event)
{
    HsfWin* self = (HsfWin*)object;
    if (!self || !event) return;
    if (self->m_clockTimer != XTIMER_INVALID_ID &&
        XTimerEvent_timerId(event) == self->m_clockTimer) {
        HsfTitleBar_refreshClock(&self->m_titleBar);
        hsf_titleSyncClimate(self);   /* 顺带刷新标题栏温湿度（远端推送驱动）。 */
        XEvent_accept((XEvent*)event);
        return;
    }
    if (self->m_shotTimer != XTIMER_INVALID_ID &&
        XTimerEvent_timerId(event) == self->m_shotTimer) {
        XObject_killTimer((XObject*)self, self->m_shotTimer);
        self->m_shotTimer = XTIMER_INVALID_ID;
        if (self->m_thenPage >= 0) {
            /* 先切页（真实路径：旧页像素残留风险点），下一拍再出图。 */
            int target = self->m_thenPage;
            self->m_thenPage = -1;
            hsf_goPage(self, target);
            self->m_shotTimer = XObject_startTimer_ms(
                (XObject*)self, 600u, XTimerType_CoarseTimer);
            XEvent_accept((XEvent*)event);
            return;
        }
        /* 登录态截图：等真实遥测到达再出图（否则截到的是"--"占位态）。
         * 数据链有连接/认证/首推延迟，固定延迟不可靠——改轮询等待，
         * 上限 HSF_SHOT_WAIT_MAX_TICKS 兜底（离线演示无数据也照样出图）。 */
        if (!hsfShotDataReady(self) &&
            ++self->m_shotWaitTicks <= HSF_SHOT_WAIT_MAX_TICKS) {
            self->m_shotTimer = XObject_startTimer_ms(
                (XObject*)self, HSF_SHOT_WAIT_STEP_MS, XTimerType_CoarseTimer);
            XEvent_accept((XEvent*)event);
            return;
        }
        hsfShotCapture(self);
        XEvent_accept((XEvent*)event);
        return;
    }
    if (XTimerEvent_timerId(event) == self->m_autoQuitTimer) {
        if (self->m_autoQuitTimer != XTIMER_INVALID_ID) {
            XObject_killTimer((XObject*)self, self->m_autoQuitTimer);
            self->m_autoQuitTimer = XTIMER_INVALID_ID;
        }
        XGuiApplication_quit();
        XEvent_accept((XEvent*)event);
        return;
    }
    if (self->m_cycleTimer != XTIMER_INVALID_ID &&
        XTimerEvent_timerId(event) == self->m_cycleTimer) {
        /* 轮转：变频器 → 模拟量 → 流量计 → 变频器（每拍切一页）。 */
        static const int kCycle[3] = {
            HSF_PAGE_MONITOR_VFD, HSF_PAGE_MONITOR_ANALOG,
            HSF_PAGE_MONITOR_FLOWMETER
        };
        int i;
        for (i = 0; i < 3; ++i) {
            if (kCycle[i] == self->m_cyclePage) {
                self->m_cyclePage = kCycle[(i + 1) % 3];
                break;
            }
        }
        hsf_goPage(self, self->m_cyclePage);
        XEvent_accept((XEvent*)event);
        return;
    }
    XClass_Parent(XObject, EXObject_TimerEvent,
                  void (*)(XObject*, XTimerEvent*))(object, event);
}

/** @brief 截图执行：把主窗后备图像存 PNG 后退出（静态函数，由定时器槽调）。 */
static void hsfShotCapture(HsfWin* self)
{
    XImage* image;
    if (!self) return;
    image = XWidget_paintImage(&self->m_base);
    if (image) {
        XPrintf("HsfClient: 保存截图到 %s\n", self->m_shotPath);
        if (!XImage_save_2(image, self->m_shotPath, "PNG", 95))
            XPrintf("HsfClient: 截图保存失败\n");
    } else {
        XPrintf("HsfClient: 截图失败（无后备图像）\n");
    }
    XGuiApplication_quit();
}

/** @brief CloseEvent：接受关闭并退出事件循环（窗口标题栏 X 同口径）。 */
static void VHsfWin_closeEvent(XWidget* self, XEvent* event)
{
    HsfWin* win = (HsfWin*)self;
    XClass_Parent(XWidget, EXWidget_CloseEvent,
                  void (*)(XWidget*, XEvent*))(self, event);
    if (win && win->m_autoQuitTimer != XTIMER_INVALID_ID) {
        XObject_killTimer((XObject*)win, win->m_autoQuitTimer);
        win->m_autoQuitTimer = XTIMER_INVALID_ID;
    }
    XGuiApplication_quit();
    if (event) XEvent_accept(event);
}

/** @brief PaintEvent：按**绘制事件**喂悬浮层帧率/帧耗时。
 *  @details 口径同 XGuiWindowDemo（每个 paintEvent 计 1 帧）——用帧泵
 *           轮询间隔算 FPS 会得到「轮询率」（数千 FPS，无意义）。 */
static void VHsfWin_paintEvent(XWidget* self, XEvent* event)
{
    HsfWin* win = (HsfWin*)self;
    int64_t nowUsecs;
    XClass_Parent(XWidget, EXWidget_PaintEvent,
                  void (*)(XWidget*, XEvent*))(self, event);
    if (!win) return;
    nowUsecs = XDateTime_currentNSecsSinceEpoch() / 1000LL;
    if (win->m_lastPaintUsecs > 0 && nowUsecs > win->m_lastPaintUsecs) {
        XPerformanceOverlay_updateFrame(&win->m_overlay,
                                        win->m_lastPaintUsecs, nowUsecs);
    }
    win->m_lastPaintUsecs = nowUsecs;
}

/** @brief 点是否落在悬浮层矩形内（浮层对鼠标穿透，命中由根窗判定）。 */
static bool hsf_overlayContains(HsfWin* self, XPoint position)
{
    XRect geo;
    if (!self) return false;
    geo = XPerformanceOverlay_geometry(&self->m_overlay);
    return position.x >= geo.x && position.x < geo.x + geo.width &&
           position.y >= geo.y && position.y < geo.y + geo.height;
}

/** @brief 鼠标按下：落在悬浮层上 → 左键开始拖动、右键切换固定/可拖。 */
static void VHsfWin_mousePressEvent(XWidget* self, XEvent* event)
{
    HsfWin* win = (HsfWin*)self;
    XMouseEvent* mouse = (XMouseEvent*)event;
    XPoint position;
    if (!self || !event || !mouse) {
        XClass_Parent(XWidget, EXWidget_MousePressEvent,
                      void (*)(XWidget*, XEvent*))(self, event);
        return;
    }
    position = XMouseEvent_position(mouse);
    if (hsf_overlayContains(win, position)) {
        if (XMouseEvent_button(mouse) == XMouseButton_RightButton) {
            /* 右键：固定 ⇄ 允许拖动切换。 */
            XPerformanceOverlay_setFixed(
                &win->m_overlay, !XPerformanceOverlay_isFixed(&win->m_overlay));
            XPerformanceOverlay_setMovable(
                &win->m_overlay, !XPerformanceOverlay_isMovable(&win->m_overlay));
            win->m_overlayPinned = true; /* 手动定位 → 挂起自动重锚 */
            hsf_fullRepaint(win);
        } else if (XMouseEvent_button(mouse) == XMouseButton_LeftButton) {
            /* 拖动起点提层（拖到导航区时被分割条/菜单钮遮挡）。 */
            XWidget_raise((XWidget*)&win->m_overlay);
            if (XPerformanceOverlay_beginDrag(&win->m_overlay,
                                               position.x, position.y)) {
                /* 抓取：MOVE/RELEASE 直投本窗口（触摸合成事件不参与
                   buttonDown 隐式抓取，越出浮层矩形即断链脱手）。 */
                XWidget_grabMouse(self);
            }
        }
        XEvent_accept(event);
        return;
    }
    XClass_Parent(XWidget, EXWidget_MousePressEvent,
                  void (*)(XWidget*, XEvent*))(self, event);
}

/** @brief 鼠标移动：拖动态 → 更新悬浮层位置。 */
static void VHsfWin_mouseMoveEvent(XWidget* self, XEvent* event)
{
    HsfWin* win = (HsfWin*)self;
    XMouseEvent* mouse = (XMouseEvent*)event;
    if (!self || !event || !mouse) {
        XClass_Parent(XWidget, EXWidget_MouseMoveEvent,
                      void (*)(XWidget*, XEvent*))(self, event);
        return;
    }
    if (XPerformanceOverlay_isDragging(&win->m_overlay)) {
        XMouseButton buttons = XMouseEvent_buttons(mouse);
        if ((buttons & XMouseButton_LeftButton) != 0) {
            XPoint p = XMouseEvent_position(mouse);
            if (XPerformanceOverlay_dragTo(&win->m_overlay, p.x, p.y,
                                           XWidget_width(self),
                                           XWidget_height(self))) {
                /* 手动拖过 → 钉住（不再被 resize 自动重锚拽回）。 */
                win->m_overlayPinned = true;
                hsf_fullRepaint(win);
            }
            XEvent_accept(event);
            return;
        }
        /* 按键中途丢失收口：拖动态与抓取态一起归零。 */
        XPerformanceOverlay_endDrag(&win->m_overlay);
        if (XWidget_mouseGrabber() == self) XWidget_releaseMouse(self);
    }
    XClass_Parent(XWidget, EXWidget_MouseMoveEvent,
                  void (*)(XWidget*, XEvent*))(self, event);
}

/** @brief ResizeEvent：形态切换判定 + 导航尺寸重申。 */
static void VHsfWin_resizeEvent(XWidget* self, XEvent* event)
{
    HsfWin* win = (HsfWin*)self;
    XClass_Parent(XWidget, EXWidget_ResizeEvent,
                  void (*)(XWidget*, XEvent*))(self, event);
    if (!win || !win->m_uiReady) return;
    hsf_layoutModeUpdate(win);
    hsf_navApplySizes(win);
    hsf_overlayAnchor(win); /* 悬浮层随窗口尺寸重锚右下。 */
}

/** @brief 主窗类虚表初始化。 */
static XVtable* HsfWin_class_init(void)
{
    XVTABLE_INIT_DEFAULT(HsfWin)
    XVTABLE_INHERIT_XCLASS(XWidget);
    XVTABLE_OVERLOAD_DEFAULT(EXObject_TimerEvent, VHsfWin_timerEvent);
    XVTABLE_OVERLOAD_DEFAULT(EXWidget_CloseEvent, VHsfWin_closeEvent);
    XVTABLE_OVERLOAD_DEFAULT(EXWidget_ResizeEvent, VHsfWin_resizeEvent);
    XVTABLE_OVERLOAD_DEFAULT(EXWidget_PaintEvent, VHsfWin_paintEvent);
    XVTABLE_OVERLOAD_DEFAULT(EXWidget_MousePressEvent, VHsfWin_mousePressEvent);
    XVTABLE_OVERLOAD_DEFAULT(EXWidget_MouseMoveEvent, VHsfWin_mouseMoveEvent);
    return XVTABLE_DEFAULT;
}

/* ==================== 移植页兼容层访问器（XGuiDemo 页面依赖） ==================== */

/** @brief 主窗顶层控件借用指针。 */
void* demo_main_toplevel(void* user)
{
    HsfWin* win = (HsfWin*)user;
    if (!win) return NULL;
    return (void*)&win->m_base;
}

/** @brief 主窗性能悬浮层实例借用指针。 */
void* demo_main_overlay(void* user)
{
    HsfWin* win = (HsfWin*)user;
    if (!win) return NULL;
    return (void*)&win->m_overlay;
}

/** @brief 悬浮层九宫格预设位置（用户显式定位）。
 *  @details 非右下预设 → 钉住（挂起 resize 自动重锚）；预设回右下 → 恢复
 *           自动重锚。setPresetPosition 是纯窗口相对九九宫格，不感知
 *           框架 chrome（标题栏/状态栏）→ 钳进内容区（demo 同款口径，
 *           避免左上预设整块压在标题栏上）。 */
void demo_main_overlay_applyPreset(void* user, int preset)
{
    HsfWin* win = (HsfWin*)user;
    XRect geo;
    int top;
    int bottomLimit;
    int x;
    int y;
    if (!win) return;
    win->m_overlayPinned =
        (preset != (int)XPerformanceOverlayPosition_BottomRight);
    XPerformanceOverlay_setPresetPosition(&win->m_overlay,
                                          (XPerformanceOverlayPosition)preset,
                                          XWidget_width(&win->m_base),
                                          XWidget_height(&win->m_base),
                                          HSF_OVERLAY_MARGIN);
    /* 预设收进内容区：用**中段宿主实际矩形**（已含 CSD 系统栏让位，
       顶部 = 标题栏下沿、底部 = 状态栏上沿）——按标题栏高度硬算会漏
       掉 CSD 让位，预设仍会压在标题栏上。 */
    geo = XPerformanceOverlay_geometry(&win->m_overlay);
    top = XWidget_y(&win->m_midHost);
    bottomLimit = XWidget_y(&win->m_midHost)
                  + XWidget_height(&win->m_midHost);
    x = geo.x;
    y = geo.y;
    if (x < 0) x = 0;
    if (x > XWidget_width(&win->m_base) - geo.width)
        x = XWidget_width(&win->m_base) - geo.width;
    if (y > bottomLimit - geo.height) y = bottomLimit - geo.height;
    if (y < top) y = top;
    if (x != geo.x || y != geo.y)
        XPerformanceOverlay_setPosition(&win->m_overlay, x, y);
}

/** @brief 底部状态栏显隐切换（布局化：显隐即布局隐藏条目）。 */
bool demo_main_setStatusBarVisible(void* user, bool visible)
{
    HsfWin* win = (HsfWin*)user;
    if (!win) return false;
    XWidget_setVisible((XWidget*)&win->m_statusBar.m_base, visible);
    demo_main_refreshChrome(user);
    return visible;
}

/** @brief 查询底部状态栏当前可见性。 */
bool demo_main_statusBarVisible(void* user)
{
    HsfWin* win = (HsfWin*)user;
    if (!win) return false;
    return XWidget_isVisible((XWidget*)&win->m_statusBar.m_base);
}

/** @brief 静态场景失效并整窗重绘（皮肤切换用）。 */
void demo_main_sceneInvalidate(void* user)
{
    HsfWin* win = (HsfWin*)user;
    XRect full;
    if (!win) return;
    XRect_init(&full, 0, 0, XWidget_width(&win->m_base),
               XWidget_height(&win->m_base));
    XWidget_updateRect(&win->m_base, &full);
}

/** @brief 主窗 chrome 全量重排（边框开关/状态栏显隐用）。 */
void demo_main_refreshChrome(void* user)
{
    HsfWin* win = (HsfWin*)user;
    int sysTop;
    if (!win) return;
    sysTop = XWindowDecoration_marginsFor(&win->m_base).top;
    XLayout_setContentsMargins((XLayout*)&win->m_rootLayout,
                               0, sysTop, 0, 0);
    XLayout_activate((XLayout*)&win->m_rootLayout);
    hsf_layoutModeUpdate(win);
    hsf_navApplySizes(win);
}

/* ==================== 主窗创建与三段装配 ==================== */

/** @brief 取主屏尺寸（逻辑像素）；屏信息不可得时返回 false。
 *  @details 首选框架 XScreen（逻辑坐标，availableGeometry 已扣任务栏）；
 *           Windows 平台不登记 XScreen（见 Drive/windows），退到平台
 *           契约 XPlatformScreen_queryFrameSize（设备像素）并按 dpr
 *           折回逻辑像素（坐标空间与窗位一致）。
 */
static bool hsf_primaryScreenSize(int* outW, int* outH)
{
    XScreen* screen;
    int w = 0;
    int h = 0;
    if (!outW || !outH) return false;
    screen = XGuiApplication_primaryScreen();
    if (screen) {
        XRect g = XScreen_availableGeometry(screen);
        if (g.width <= 0 || g.height <= 0)
            g = XScreen_geometry(screen);
        if (g.width > 0 && g.height > 0) {
            *outW = g.width;
            *outH = g.height;
            return true;
        }
    }
    if (XPlatformScreen_queryFrameSize(&w, &h) && w > 0 && h > 0) {
        float dpr = 1.0f;
        XPlatformScreenDpiInfo dpi;
        if (XPlatformScreen_queryDpi(&dpi) && dpi.m_valid &&
            dpi.m_density > 0.0f)
            dpr = dpi.m_density;
        *outW = (int)((float)w / dpr + 0.5f);
        *outH = (int)((float)h / dpr + 0.5f);
        return true;
    }
    return false;
}

/** @brief 主窗缺省尺寸：横屏基准 1280x720，屏小则等比收敛。
 *  @details 缺省取宽屏形态（逻辑宽 ≥ HSF_COMPACT_MAX_W ⇒ 宽屏/横屏
 *           布局，左侧导航常驻）；屏装不下时按 16:9 等比缩到屏内
 *           （留 HSF_WIN_SCREEN_PCT 余量给边框与任务栏），绝不裁切。
 */
static void hsf_defaultWinSize(int* outW, int* outH)
{
    int w = HSF_WIN_DEFAULT_W;
    int h = HSF_WIN_DEFAULT_H;
    int sw = 0;
    int sh = 0;
    if (!outW || !outH) return;
    if (hsf_primaryScreenSize(&sw, &sh) && sw > 0 && sh > 0) {
        int availW = (sw * HSF_WIN_SCREEN_PCT) / 100;
        int availH = (sh * HSF_WIN_SCREEN_PCT) / 100;
        if (availW < 1) availW = 1;
        if (availH < 1) availH = 1;
        if (w > availW || h > availH) {
            /* 等比缩到屏内：交叉相乘比宽高约束（免浮点误差） */
            if ((long long)w * availH > (long long)availW * h) {
                h = (int)((long long)h * availW / w);
                w = availW;
            } else {
                w = (int)((long long)w * availH / h);
                h = availH;
            }
        }
    }
    *outW = w;
    *outH = h;
}

/** @brief 创建主窗：根垂直布局三段 + 分割条（导航|堆栈）+ builders 挂栈。 */
static HsfWin* hsf_win_create(int autoSeconds, const char* shotPath,
                              int shotWidth, int shotHeight, int startPage,
                              int thenPage)
{
    HsfWin* self;
    int sysTop = 0; /* CSD 系统标题栏保留边距（布局顶边让位）。 */
    int i;

    self = (HsfWin*)XMemory_malloc(sizeof(HsfWin),
                                   XCLASS_DEFAULT_MEMORY_TYPE);
    if (!self) return NULL;
    memset(self, 0, sizeof(HsfWin));
    XWidget_init(&self->m_base, NULL, 0);
    XClassSetVtable(self, HsfWin);
    Set_Class_Memory(self, XCLASS_DEFAULT_MEMORY_TYPE);
    Set_Class_IsHeap(self, true);
    self->m_autoQuitTimer = XTIMER_INVALID_ID;
    self->m_clockTimer = XTIMER_INVALID_ID;
    self->m_shotTimer = XTIMER_INVALID_ID;
    self->m_cycleTimer = XTIMER_INVALID_ID;
    self->m_autoSeconds = (autoSeconds > 0) ? autoSeconds : 0;
    self->m_shotPath = shotPath;
    self->m_navWidth = HSF_NAV_DEFAULT_W;
    self->m_layoutMode = -1; /* 首次必触发形态判定。 */
    self->m_pageBeforeMenu = HSF_PAGE_HOME;
    self->m_subTopic = -1;   /* 尚无页面订阅（0 是合法主题号，必须显式置 -1）。 */
    self->m_lastFrameUsecs = 0;
    self->m_lastPaintUsecs = 0;
    self->m_overlayAnchorW = 0;
    self->m_overlayAnchorH = 0;
    self->m_lastNetworkPollUsecs = 0;

    self->m_thenPage = thenPage;
    /* 缺省窗口尺寸：横屏基准（宽屏形态）；--size 显式指定优先（截图
     * 验收按给定尺寸出图）。 */
    {
        int defW = 0;
        int defH = 0;
        hsf_defaultWinSize(&defW, &defH);
        XWidget_resize(&self->m_base,
                       (shotWidth > 0) ? shotWidth : defW,
                       (shotHeight > 0) ? shotHeight : defH);
    }
    XWidget_setWindowTitle_2(&self->m_base, "PMMA有机玻璃高速过滤器");

    /* ---- 第一行：标题栏（菜单图标 + 标题文字，模块自理 chrome） ---- */
    HsfTitleBar_init(&self->m_titleBar, &self->m_base);
    /* 标题固定为应用名（页名由底部状态栏「当前页」承担）。 */
    HsfTitleBar_setTitle(&self->m_titleBar,
                         "PMMA有机玻璃高速过滤器");
    /* 全局运行模式：顶栏显示 + 点击切换（服务端一档管控全部设备）。 */
    HsfTitleBar_setModeHandler(&self->m_titleBar, hsfWinModeSlot, self);
    HsfLinkClient_setModeCallback(hsfWinModeNotify, self);
    HsfLinkClient_setHealthCallback(hsfWinHealthNotify, self);
    /* 电磁阀：快照到达即灌页面（含控制闸重算）；按钮出口走同一链路。 */
    HsfLinkClient_setValveCallback(hsfWinValveSync, self);
    HsfLinkClient_setCommandCallback(hsfWinCommandResult, self);
    HsfMonitorValve_setWriteHandler(hsfWinValveWrite, self);
    /* 数字量/模拟量：点数与值全部由服务端推送决定（客户端不写死）。 */
    HsfLinkClient_setDigitalCallback(hsfWinDigitalSync, self);
    HsfLinkClient_setAiCallback(hsfWinAnalogSync, self);
    HsfMonitorDigital_setWriteHandler(hsfWinDigitalWrite, self);
    HsfMonitorValve_attachStatus(HsfPages_setStatus, self);
    HsfLinkClient_queryRunMode();
    HsfTitleBar_setMenuHandler(&self->m_titleBar, hsf_menuToggleCb, self);

    /* ---- 底部：黑色状态栏（模块自理 chrome） ---- */
    HsfStatusBar_init(&self->m_statusBar, &self->m_base, 0);
    {
        char buf[96];
        snprintf(buf, sizeof(buf), "当前页: %s",
                 HsfPages_name(HSF_PAGE_HOME));
        HsfStatusBar_setText(&self->m_statusBar, buf);
    }

    /* ---- 性能悬浮层（悬浮窗设置页的操作对象） ---- */
    XPerformanceOverlay_init(&self->m_overlay, &self->m_base, 0);
    XWidget_setAttribute((XWidget*)&self->m_overlay,
                         XWidgetAttribute_TransparentForMouseEvents, true);
    XPerformanceOverlay_setSize(&self->m_overlay, 210, 70);
    XPerformanceOverlay_setAutoFitSize(&self->m_overlay, true);
    XPerformanceOverlay_setTextPixelSize(&self->m_overlay, 12);
    /* [关键] 默认 fixed=true（demo 同口径「默认固定右下」）：帧泵的
       autoFit 尺寸差重锚**只在 fixed 态生效**——不设 fixed 的话浮层
       变宽/变高后永远不重锚 → 右缘/底缘离开边框（用户实测「右边没贴
       住」的根因）。右键浮层或设置页「固定位置」开关可解固定转自由
       拖动（拖动入口条件= movable && !fixed）。 */
    XPerformanceOverlay_setFixed(&self->m_overlay, true);
    /* 悬浮层可见（右下锚定，见 hsf_overlayAnchor）——「显示悬浮窗」总
     * 开关/关闭按钮在悬浮窗设置页控制它的显隐。 */
    XWidget_show((XWidget*)&self->m_overlay);

    /* ---- 根垂直布局三段：标题栏 / 中段宿主（伸展）/ 黑色状态栏 ---- */
    XBoxLayout_init(&self->m_rootLayout, XBoxLayoutDirection_TopToBottom);
    XWidget_init(&self->m_midHost, &self->m_base, 0);
    XWidget_setSizePolicy(&self->m_midHost,
                          XWidgetSizePolicy_Expanding,
                          XWidgetSizePolicy_Expanding);
    XBoxLayout_init(&self->m_midLayout, XBoxLayoutDirection_TopToBottom);

    /* 中段=框架分割条：左停靠导航面板 | 内容堆栈（拖拽把手可改宽）。 */
    XSplitter_init(&self->m_splitter, &self->m_midHost, 0);
    XSplitter_setOrientation(&self->m_splitter, XOrientation_Horizontal);
    XSplitter_setHandleWidth(&self->m_splitter, HSF_SPLITTER_HANDLE_W);
    XWidget_setAttribute((XWidget*)&self->m_splitter,
                         XWidgetAttribute_HitEdgeBand, true);
    XWidget_show((XWidget*)&self->m_splitter);
    XBoxLayout_addWidget(&self->m_midLayout, (XWidget*)&self->m_splitter);
    XBoxLayout_setStretch(&self->m_midLayout, 0, 1);
    XWidget_setLayout(&self->m_midHost, (XLayout*)&self->m_midLayout);

    /* 左侧停靠导航面板（手风琴树）。 */
    HsfNavPanel_init(&self->m_navPanel, &self->m_base);
    HsfNavPanel_setNavHandler(&self->m_navPanel, hsf_navToPage, self);
    XSplitter_addWidget(&self->m_splitter, (XWidget*)&self->m_navPanel.m_base);

    /* 内容堆栈（分割条第二条目，伸展）。 */
    HsfStack_init(&self->m_stack, &self->m_base);
    XSplitter_addWidget(&self->m_splitter, HsfStack_host(&self->m_stack));
    XSplitter_setStretchFactor(&self->m_splitter, 1, 1);

    /* 分割条拖拽记忆导航宽度。 */
    XObject_connect_1((XObject*)&self->m_splitter,
                      (size_t)XSplitter_splitterMoved_signal(NULL, 0, 0),
                      (XObject*)self, hsf_splitterMovedSlot,
                      XConnectionType_Direct);

    XBoxLayout_addWidget(&self->m_rootLayout, (XWidget*)&self->m_titleBar);

    XBoxLayout_addWidget(&self->m_rootLayout, &self->m_midHost);

    XBoxLayout_addWidget(&self->m_rootLayout,
                         (XWidget*)&self->m_statusBar);

    XBoxLayout_setStretch(&self->m_rootLayout, 1, 1);
    sysTop = XWindowDecoration_marginsFor(&self->m_base).top;

    XLayout_setContentsMargins((XLayout*)&self->m_rootLayout,
                               0, sysTop, 0, 0);
    XWidget_setLayout(&self->m_base, (XLayout*)&self->m_rootLayout);

    /* 状态栏 raise 到内容控件之后：窗口缩小后内容越界下探时黑底永可见。 */
    XWidget_raise((XWidget*)&self->m_statusBar);

    /* ---- 中段：builders 表逐页创建挂栈（堆叠下标=页索引） ---- */
    for (i = 0; i < HSF_PAGE_COUNT; ++i) {
        XWidget* page = kHsfBuilders[i](HsfStack_host(&self->m_stack),
                                        hsf_navToPage, self);
        XWidget* entry;
        XScrollArea* scroll;
        if (!page) {
            XPrintf("HsfClient: 页 %d (%s) 创建失败\n", i,
                    HsfPages_name(i));
            continue;
        }
        self->m_pageRoot[i] = page;
        if (i == HSF_PAGE_MENU) {
            /* 菜单页：改用**移动态树面板**（竖屏菜单口径：分区标题 +
               缩进子项、44dp 触控行、手风琴就地展开）。旧页收起（它已
               建在堆叠宿主下，不藏会与新面板同屏）。 */
            XWidget* host = XWidget_create(HsfStack_host(&self->m_stack), 0);
            XWidget_hide(page);
            HsfNavPanel_initMobile(&self->m_mobilePanel, host);
            HsfNavPanel_setNavHandler(&self->m_mobilePanel,
                                      hsf_navToPage, self);
            {
                XBoxLayout_init(&self->m_mobileBox,
                                XBoxLayoutDirection_TopToBottom);
                XLayout_setContentsMargins((XLayout*)&self->m_mobileBox,
                                           0, 0, 0, 0);
                /* 先挂布局再放条目（linkItem 改挂依赖布局已有 parent）。 */
                XWidget_setLayout(host, (XLayout*)&self->m_mobileBox);
                XBoxLayout_addWidget(&self->m_mobileBox,
                                     (XWidget*)&self->m_mobilePanel.m_base);
                XBoxLayout_setStretch(&self->m_mobileBox, 0, 1);
            }
            page = host;
        }
        /* 统一滚动壳：页根作滚动内容（宽随视口、高出可滚）——界面永不被
           视口截断（窄窗口/竖屏形态尤其需要）。移植页的窄宽分档按页根
           宽度判定，滚动壳下页根宽=视口宽 → 分档照常。 */
        scroll = XScrollArea_create(HsfStack_host(&self->m_stack), 0);
        if (!scroll) {
            HsfStack_addPage(&self->m_stack, page);
            continue;
        }
        XScrollArea_setWidgetResizable(scroll, true);
        XAbstractScrollArea_setHorizontalScrollBarPolicy(
            (XAbstractScrollArea*)scroll, XScrollBarPolicy_AlwaysOff);
        XFrame_setFrameStyle((XFrame*)scroll,
                             (int)(XFrameShape_NoFrame | XFrameShadow_Plain));
        XScrollArea_setWidget(scroll, page);
        entry = (XWidget*)scroll;
        self->m_pageScroll[i] = entry;   /* 切页时复位滚动位用。 */
        HsfStack_addPage(&self->m_stack, entry);
    }
    HsfStack_go(&self->m_stack, HSF_PAGE_HOME);
    HsfNavPanel_setActivePage(&self->m_navPanel, HSF_PAGE_HOME);
    /* 指定起始页（截图/调试直达某页）。 */
    if (startPage >= 0 && startPage < HSF_PAGE_COUNT)
        hsf_goPage(self, startPage);

    /* 数字量页状态栏反馈挂接 + 兼容自测钩子（env 触发，截图闭环用）。 */
    HsfMonitorDigital_attachStatus(HsfPages_setStatus, self);
    HsfMonitorDigital_autotest(self->m_pageRoot[HSF_PAGE_MONITOR_DIGITAL]);
    /* 模拟量页同款挂接（env 触发，截图闭环用）。 */
    HsfMonitorAnalog_attachStatus(HsfPages_setStatus, self);
    HsfMonitorAnalog_autotest(self->m_pageRoot[HSF_PAGE_MONITOR_ANALOG]);

    /* ---- 【持久化恢复·显示前】不依赖窗口句柄的项：皮肤/边框位/窗口
     * 方向/光标/电源策略。皮肤必须在此落定——窗口映射后首帧即新配色，
     * 不会先闪一下默认皮肤再跳变（对标 Qt：设置恢复在界面呈现前完成）。 */
    hsf_page_window_restoreSettings();

    /* UI 装配就绪（此前 resize 事件的形态判定一律跳过——侧栏/分割条
     * 尚未建立，判定结果会落在零内存对象上）。 */
    self->m_uiReady = true;

    XWidget_show(&self->m_base); /* 顶层窗口整体最后显示（子树已就绪）。 */

    /* 形态初判（宽/窄）+ 导航尺寸初申——**必须在 show 之后**：show 递归
     * 显示子树，先前隐藏的紧凑态侧栏会被重新显示出来（竖屏侧栏不隐）。 */
    hsf_layoutModeUpdate(self);
    hsf_navApplySizes(self);
    hsf_adaptPages(self);
    /* 【持久化恢复·显示后】依赖窗口句柄的项：画布旋转（能力探测要句柄）/
     * 最大化/全屏（窗口态下发）/状态栏与边框 chrome 重排 + 页面控件回填；
     * 随后悬浮窗各项 + 预设位置（内容区钳制要内容区几何）。
     * 此时尚未进事件循环，首帧绘制还没发生，用户看不到中间态。 */
    hsf_page_window_restoreChrome();
    hsf_page_overlay_restoreSettings();
    hsf_overlayAnchor(self);
    XWidget_raise((XWidget*)&self->m_overlay);

    /* 兼容自测钩子（env XHSF_SETTINGS_AUTOTEST）：直呼槽 = 用户点击的
       等价路径，闭环验证「改动 → 落盘 → 复位默认」（自测以复位收尾，落盘
       的即出厂默认值）。 */
    if (XSystem_environment("XHSF_SETTINGS_AUTOTEST")) {
        hsf_page_overlay_autotest(
            self->m_pageRoot[HSF_PAGE_SETTINGS_OVERLAY]);
        hsf_page_window_autotest(
            self->m_pageRoot[HSF_PAGE_SETTINGS_WINDOW]);
    }


    /* 离屏截图模式：窗口开到屏外（不占用户桌面、不抢焦点），静默渲染
     * 数帧后存 PNG 自动退出（走 XWidget_paintImage 后备图像，与
     * XGuiWindowDemo --screenshot 同口径，不依赖屏幕像素）。 */
    if (self->m_shotPath) {
        int defW = 0;
        int defH = 0;
        hsf_defaultWinSize(&defW, &defH);
        XWidget_setGeometry(&self->m_base, -3000, -3000,
                            (shotWidth > 0) ? shotWidth : defW,
                            (shotHeight > 0) ? shotHeight : defH);
        /* 离屏截图延迟：已登录（--login 无头）要多等首推——服务端温湿度
         * 推送周期 HSF_TEMPHUM_PUSH_MS=1s，加连接/认证/首帧渲染；离线
         * （--no-login）路径无网络等待，维持原 1200ms。 */
        self->m_shotTimer = XObject_startTimer_ms(
            (XObject*)self,
            HsfLinkClient_isOpen() ? 3000u : 1200u,
            XTimerType_CoarseTimer);
        if (self->m_shotTimer == XTIMER_INVALID_ID) {
            XPrintf("HsfClient: 截图定时器创建失败\n");
        }
    }
    return self;
}

/* ==================== 全局运行模式（顶栏） ==================== */

/**
 * @brief 顶栏模式钮点击：手动 ⇄ 自动 切换（服务端执行，需 root）。
 * @param userData 归属主窗（HsfWin*）。
 */
static void hsfWinModeSlot(void* userData)
{
    HsfTitleBar* bar;
    int autoMode = 0;
    bool known;
    if (!userData) return;
    bar = &((HsfWin*)userData)->m_titleBar;
    if (!HsfLinkClient_isReady()) return;
    known = HsfLinkClient_runMode(&autoMode);
    HsfLinkClient_setRunMode(!(known && autoMode));
}

/**
 * @brief 模式应答到达：刷新顶栏显示。
 * @param userData 归属主窗（HsfWin*）。
 */
static void hsfWinModeNotify(void* userData)
{
    HsfWin* win = (HsfWin*)userData;
    int autoMode = 0;
    bool known;
    if (!win) return;
    known = HsfLinkClient_runMode(&autoMode);
    HsfTitleBar_setRunMode(&win->m_titleBar, known, autoMode == 1);
}

/**
 * @brief 设备健康更新：刷新顶栏健康指示。
 * @param userData 归属主窗（HsfWin*）。
 * @note  服务端事件驱动推送（状态变化才发），客户端首连/首订阅会立即
 *        收到一份，因此这里无需轮询。
 */
static void hsfWinHealthNotify(void* userData)
{
    HsfWin* win = (HsfWin*)userData;
    int fault = 0;
    int offline = 0;
    int n;
    if (!win) return;
    n = HsfLinkClient_deviceHealth(NULL, 0, NULL);
    if (n <= 0) {
        HsfTitleBar_setHealth(&win->m_titleBar, false, 0, 0);
        return;
    }
    (void)HsfLinkClient_healthHasProblem(&fault, &offline);
    HsfTitleBar_setHealth(&win->m_titleBar, true, fault, offline);
}

/* ==================== 电磁阀页数据桥 ==================== */

/**
 * @brief 把服务端阀快照灌进电磁阀页并同步控制闸。
 * @param userData 归属主窗（HsfWin*；当前未用，保留接缝）。
 * @note  [双保险] 服务端始终强制闸（自动模式拒绝全部远程控制 + 需 root）；
 *        客户端这道闸只负责让"点不动"有可见原因——否则用户会以为界面卡了。
 */
static void hsfWinValveSync(void* userData)
{
    const HsfLinkValves* v = HsfLinkClient_valves();
    const char* reason = NULL;
    int autoMode = 0;
    int i;

    (void)userData;
    if (!v) return;
    if (v->m_received && HsfLinkClient_valvesFresh()) {
        HsfMonitorValve_setCount(v->m_count);
        for (i = 0; i < v->m_count; ++i) {
            HsfMonitorValve_setName(i, v->m_items[i].m_name);
            HsfMonitorValve_setFeedback(i, v->m_items[i].m_openFb,
                                        v->m_items[i].m_closeFb);
            HsfMonitorValve_setState(i, v->m_items[i].m_state);
            HsfMonitorValve_setReady(i, v->m_items[i].m_ready);
            HsfMonitorValve_setValid(i, v->m_items[i].m_valid);
            HsfMonitorValve_setText(i, v->m_items[i].m_text);
        }
    }

    /* 闸门原因优先级：未连接 → 自动模式 → 权限不足。 */
    if (!HsfLinkClient_isReady()) {
        reason = "\xE6\x9C\xAA\xE8\xBF\x9E\xE6\x8E\xA5\xE6\x9C\x8D\xE5\x8A\xA1\xE7\xAB\xAF"; /* 未连接服务端 */
    } else if (HsfLinkClient_runMode(&autoMode) && autoMode) {
        reason = "\xE8\x87\xAA\xE5\x8A\xA8\xE8\xBF\x90\xE8\xA1\x8C\xE4\xB8\xAD"
                 "\xEF\xBC\x9A\xE8\xBF\x9C\xE7\xA8\x8B\xE6\x8E\xA7\xE5\x88\xB6"
                 "\xE5\xB7\xB2\xE8\xA2\xAB\xE7\xA6\x81\xE7\x94\xA8"; /* 自动运行中：远程控制已被禁用 */
    } else if (HsfLinkClient_role() < (int)HSF_ROLE_CONTROL) {
        reason = "\xE6\x9D\x83\xE9\x99\x90\xE4\xB8\x8D\xE8\xB6\xB3"
                 "\xEF\xBC\x9A\xE6\x8E\xA7\xE5\x88\xB6\xE9\x9C\x80\xE8\xA6\x81"
                 " root \xE8\xB4\xA6\xE6\x88\xB7"; /* 权限不足：控制需要 root 账户 */
    }
    HsfMonitorValve_setControlEnabled(reason == NULL, reason);
    HsfMonitorValve_refresh();
}

/** @brief 电磁阀页按钮出口：下发阀控制请求。 */
static void hsfWinValveWrite(void* userData, int index, bool wantOpen)
{
    (void)userData;
    HsfLinkClient_valveControl(index, wantOpen);
}

/** @brief 控制应答回调：阀/数字量被拒时把原因打到状态栏。 */
static void hsfWinCommandResult(void* userData, int op, bool ok,
                                const char* errUtf8)
{
    HsfWin* win = (HsfWin*)userData;
    char buf[160];
    if (!win) return;
    if (op != (int)HSF_CMD_VALVE_CONTROL && op != (int)HSF_CMD_DIGITAL_CONTROL)
        return;
    if (!ok) {
        snprintf(buf, sizeof(buf),
                 "\xE6\x93\x8D\xE4\xBD\x9C\xE5\xA4\xB1\xE8\xB4\xA5\xEF\xBC\x9A%s",
                 (errUtf8 && errUtf8[0]) ? errUtf8
                                         : "\xE6\x9C\xAA\xE7\x9F\xA5\xE5\x8E\x9F\xE5\x9B\xA0");
    } else {
        snprintf(buf, sizeof(buf), "\xE5\x91\xBD\xE4\xBB\xA4\xE5\xB7\xB2\xE5\x8F\x97\xE7\x90\x86");
    }
    HsfStatusBar_setText(&win->m_statusBar, buf);
}

/* ==================== 数字量页数据桥 ==================== */

/**
 * @brief 把服务端位图推送灌进数字量页，并同步操作闸。
 * @param userData 归属主窗（HsfWin*；当前未用）。
 * @note  [点数由服务端定] 客户端不写死几个 DI/DO：一律按推送的
 *        count 建点（DI 占 [0,count)、DO 占 [count,2*count)），服务端改
 *        点数客户端自动跟随；值取 di/do 位图。
 *        [操作闸] 与电磁阀同款双保险：服务端始终强制，客户端
 *        只负责让"点不动"有可见原因。
 */
static void hsfWinDigitalSync(void* userData)
{
    const HsfLinkDigital* d = HsfLinkClient_digital();
    const char* reason = NULL;
    int autoMode = 0;
    int i;

    (void)userData;
    if (!d) return;
    if (d->m_received && HsfLinkClient_digitalFresh()) {
        int n = d->m_count;
        if (n < 0) n = 0;
        if (n > HSF_DIGITAL_CHANNELS) n = HSF_DIGITAL_CHANNELS;
        HsfMonitorDigital_setCount(n * 2);
        for (i = 0; i < n; ++i) {
            /* 通道名由服务端给（未收到时页面显示缺省 DI/DO+通道号）。 */
            const char* diName = HsfLinkClient_digitalName(i, false);
            const char* doName = HsfLinkClient_digitalName(i, true);
            if (diName) HsfMonitorDigital_setName(i, diName);
            if (doName) HsfMonitorDigital_setName(n + i, doName);
            HsfMonitorDigital_setChannel(i, i);
            HsfMonitorDigital_setKind(i, HSF_DIGITAL_INPUT);
            HsfMonitorDigital_setValue(
                i, (d->m_di & (1u << i)) ? HSF_DIGITAL_ON : HSF_DIGITAL_OFF);

            HsfMonitorDigital_setChannel(n + i, i);
            HsfMonitorDigital_setKind(n + i, HSF_DIGITAL_OUTPUT);
            HsfMonitorDigital_setValue(
                n + i, (d->m_do & (1u << i)) ? HSF_DIGITAL_ON : HSF_DIGITAL_OFF);
        }
    }

    if (!HsfLinkClient_isReady()) {
        reason = "\xE6\x9C\xAA\xE8\xBF\x9E\xE6\x8E\xA5\xE6\x9C\x8D\xE5\x8A\xA1\xE7\xAB\xAF";
    } else if (HsfLinkClient_runMode(&autoMode) && autoMode) {
        reason = "\xE8\x87\xAA\xE5\x8A\xA8\xE8\xBF\x90\xE8\xA1\x8C\xE4\xB8\xAD\xEF\xBC\x9A\xE8\xBF\x9C\xE7\xA8\x8B\xE6\x8E\xA7\xE5\x88\xB6\xE5\xB7\xB2\xE8\xA2\xAB\xE7\xA6\x81\xE7\x94\xA8";
    } else if (HsfLinkClient_role() < (int)HSF_ROLE_CONTROL) {
        reason = "\xE6\x9D\x83\xE9\x99\x90\xE4\xB8\x8D\xE8\xB6\xB3\xEF\xBC\x9A\xE6\x8E\xA7\xE5\x88\xB6\xE9\x9C\x80\xE8\xA6\x81 root \xE8\xB4\xA6\xE6\x88\xB7";
    }
    HsfMonitorDigital_setOperationEnabled(reason == NULL);
    HsfMonitorDigital_setInteractiveMode(reason == NULL);
}

/**
 * @brief 数字量页写出口：DO 按下消费者给的下标发控制。
 * @note  页内下标分两段：[0,count)=DI、[count,2*count)=DO。写出口只接收
 *        DO 段，按减去段基址后的通道号下发。
 */
static void hsfWinDigitalWrite(void* userData, int index, HsfDigitalState target)
{
    const HsfLinkDigital* d = HsfLinkClient_digital();
    int n;
    (void)userData;
    if (!d) return;
    n = d->m_count;
    if (index < n) return;            /* DI 只读，不应该走到这里。 */
    HsfLinkClient_digitalControl(index - n, target == HSF_DIGITAL_ON);
}

/* ==================== 模拟量页数据桥 ==================== */

/**
 * @brief 把服务端模拟量推送灌进模拟量页。
 * @note  [点数由服务端定] 通道数取推送的 n 键（不读客户端常量）；
 *        值取 u<i> 微安定点 ÷ 1000 得 mA，有效标志取 v 掩码。
 */
static void hsfWinAnalogSync(void* userData)
{
    int32_t ua[HSF_LINK_AI_CH_MAX];
    uint32_t mask = 0;
    int n;
    int i;

    (void)userData;
    n = HsfLinkClient_aiValues(ua, &mask);
    if (n <= 0) return;
    HsfMonitorAnalog_setCount(n);
    for (i = 0; i < n; ++i) {
        const char* name = HsfLinkClient_aiName(i);
        if (name) HsfMonitorAnalog_setName(i, name);
        HsfMonitorAnalog_setChannel(i, i);
        HsfMonitorAnalog_setRaw(i, ua[i]);
        HsfMonitorAnalog_setScaled(i, (double)ua[i] / 1000.0);
        HsfMonitorAnalog_setKind(i, HSF_ANALOG_INPUT);
    }
}

/* ==================== 帧泵（几何刷新漏斗） ==================== *//* ==================== 帧泵（几何刷新漏斗） ==================== */

/** @brief 帧泵：每轮事件循环刷新移植页分档与导航几何。
 *  @details 布局/尺寸更新是**异步**的（resize 事件排队派发），故在
 *           布局变更当拍读到的页宽可能是旧值 → 移植页的窄宽分档算错
 *           （字段不收窄 → 窄窗横向截断）。每轮补一次 adapt，拿到
 *           新鲜几何再分档（同 XGuiWindowDemo 帧泵口径）。 */
static bool hsf_framePump(void* userData)
{
    HsfWin* win = (HsfWin*)userData;
    int64_t nowUsecs;
    int64_t frameUsecs;
    if (!win || !win->m_uiReady) return true;
    hsf_adaptPages(win);
    HsfNavPanel_update(&win->m_navPanel);
    HsfNavPanel_update(&win->m_mobilePanel);
    /* 主界面数值块刷新：**只在首页可见时**、按 200ms 节流跑。
     * [为什么不在页面内开定时器] 页面生命周期长于/短于主窗不定，页内定时器
     * 要自己管注销；这里借帧泵统一节拍，页隐藏即不刷（与"隐藏页不重绘"
     * 同口径），零额外定时器。 */
    {
        int64_t nowMs = XDateTime_currentNSecsSinceEpoch() / 1000000LL;
        if (HsfStack_current(&win->m_stack) == HSF_PAGE_HOME &&
            nowMs - win->m_homeRefreshMs >= 200) {
            win->m_homeRefreshMs = nowMs;
            HsfHomePage_refresh(win->m_pageRoot[HSF_PAGE_HOME]);
        }
    }
    XWidget_raise((XWidget*)&win->m_overlay);
    /* autoFit 收框只改尺寸不动位置 → 每帧按「尺寸与上次锚定值不同」重锚
       贴边（demo 帧泵同口径；fixed 态才跟边，非 fixed 是用户自由拖动位）。 */
    if (XPerformanceOverlay_isFixed(&win->m_overlay)) {
        XRect ovlGeo = XPerformanceOverlay_geometry(&win->m_overlay);
        if (ovlGeo.width != win->m_overlayAnchorW ||
            ovlGeo.height != win->m_overlayAnchorH)
            hsf_overlayAnchor(win);
    }
    /* 喂悬浮层：FPS/帧耗时按**两拍间隔**统计，网络按 250ms 周期取
       计数（同 XGuiWindowDemo 帧泵口径）——悬浮层是被动控件，不喂数据
       就只是一块空白板。 */
    nowUsecs = XDateTime_currentNSecsSinceEpoch() / 1000LL;
    frameUsecs = nowUsecs - win->m_lastFrameUsecs;
    if (win->m_lastFrameUsecs > 0 && frameUsecs > 0) {
        /* 帧率/帧耗时由 paintEvent 喂（口径=绘制活动率）；此处只喂网络。 */
        if (nowUsecs - win->m_lastNetworkPollUsecs >= 250000LL) {
            uint64_t rx = 0;
            uint64_t tx = 0;
            bool ok = XDeviceNetwork_getNetworkCounters(&rx, &tx);
            XPerformanceOverlay_updateNetwork(&win->m_overlay, ok, rx, tx,
                                              nowUsecs);
            win->m_lastNetworkPollUsecs = nowUsecs;
        }
    }
    win->m_lastFrameUsecs = nowUsecs;
    return true;
}

/* ==================== 启动入口 ==================== */

/**
 * @brief 登录结果上下文（登录窗/无头登录 → 主窗装配的桥）。
 */
typedef struct HsfUiLoginCtx {
    const HsfUiOptions* m_opts;     /**< 启动选项（借用）。 */
    HsfWin* m_win;                  /**< 登录成功后装配的主窗（供退出清理）。 */
    struct HsfLoginWindow* m_login; /**< 登录窗（成功后延迟关闭；无头为 NULL）。 */
    int m_failed;                   /**< 登录最终失败（退出循环用）。 */
    int m_pending;                  /**< 主窗待装配（事件循环顶层延迟建，见泵）。 */
} HsfUiLoginCtx;

static HsfUiLoginCtx* s_launchCtx = NULL;   /**< 延迟装配上下文（泵消费）。 */

/**
 * @brief 装配主窗（时钟/自动退出/帧泵；截图模式窗口内处理）。
 */
static HsfWin* hsfUiMakeWin(const HsfUiOptions* opts)
{
    HsfWin* win = hsf_win_create(opts ? opts->autoSeconds : 0,
                                 opts ? opts->shotPath : NULL,
                                 opts ? opts->shotWidth : 0,
                                 opts ? opts->shotHeight : 0,
                                 opts ? opts->startPage : -1,
                                 opts ? opts->thenPage : -1);
    if (!win) {
        XPrintf("HsfClient: 主窗创建失败\n");
        return NULL;
    }
    XPrintf("HsfClient: 主窗就绪（标题栏/分割条[导航|堆栈]/黑色状态栏，"
            "%d 页挂栈）\n", HSF_PAGE_COUNT);

    /* 标题栏时钟 1s 刷新（demo 品牌条同款）。 */
    win->m_clockTimer = XObject_startTimer_ms(
        (XObject*)win, 1000u, XTimerType_CoarseTimer);
    if (win->m_clockTimer == XTIMER_INVALID_ID)
        XPrintf("HsfClient: 时钟定时器创建失败\n");

    /* 按需订阅自测钩子（env XHSF_PAGE_CYCLE）：每 2s 在"有遥测的页"之间
     * 轮转一次，供服务端日志观察订阅/退订迁移（切页订阅归属验证）。
     * 取值=起始页索引；不设则不启用。 */
    {
        const char* cycle = XSystem_environment("XHSF_PAGE_CYCLE");
        if (cycle && cycle[0]) {
            static const int kCyclePages[3] = {
                HSF_PAGE_MONITOR_VFD, HSF_PAGE_MONITOR_ANALOG,
                HSF_PAGE_MONITOR_FLOWMETER
            };
            win->m_cyclePage = atoi(cycle);
            win->m_cycleTimer = XObject_startTimer_ms(
                (XObject*)win, 2000u, XTimerType_CoarseTimer);
            (void)kCyclePages;
            XPrintf("HsfClient: 页轮转自测启用（起始 %d）\n", win->m_cyclePage);
        } else {
            win->m_cycleTimer = XTIMER_INVALID_ID;
        }
    }

    /* 自动退出定时器（CI/截图验收；0=常驻等待窗口关闭）。 */
    if (win->m_autoSeconds > 0) {
        win->m_autoQuitTimer = XObject_startTimer_ms(
            (XObject*)win, (uint64_t)win->m_autoSeconds * 1000u,
            XTimerType_CoarseTimer);
        if (win->m_autoQuitTimer == XTIMER_INVALID_ID) {
            XPrintf("HsfClient: 自动退出定时器创建失败\n");
            return NULL;
        }
    }

    /* 帧泵注册（几何刷新漏斗；移植页窄宽分档依赖新鲜页宽）。 */
    win->m_framePump = XAbstractEventDispatcher_addPollCallback(
        hsf_framePump, win);
    if (!win->m_framePump)
        XPrintf("HsfClient: 帧泵注册失败\n");
    /* 温湿度推送订阅：标题栏随推送到达即刻刷新（不等 1s 时钟拍）。
     * 注册在窗口建好之后——回调里直接用 win 指针刷标题栏。 */
    HsfLinkClient_setTempHumCallback(hsf_onTempHumPushed, win);
    hsf_titleSyncClimate(win);      /* 立即同步一次现状（未登录则为"--"）。 */
    return win;
}

/**
 * @brief 登录结果回调：成功 → 收登录窗/接远端数据；失败 → 退出循环。
 * @note  [两种启动序]
 *        - 主窗先显（交互登录，2026-11-04 起）：主窗**已存在**，本回调
 *          只需隐藏登录窗并把页面数据源切到远端后端；主窗不重建
 *          （重建会丢用户已拖好的布局、并让登录窗的 owner 悬垂）。
 *        - 无头登录（--login）：主窗尚不存在，走延迟装配泵建窗
 *          （回调栈内建顶层窗口实测卡死，泵在事件循环顶层跑）。
 */
static void hsfUiLoginDone(void* userData, bool success, const char* errUtf8)
{
    HsfUiLoginCtx* ctx = (HsfUiLoginCtx*)userData;
    if (!ctx) return;
    if (!success) {
        XPrintf("HsfClient: 登录失败（%s）\n",
                errUtf8 && errUtf8[0] ? errUtf8 : "取消");
        ctx->m_failed = 1;
        XGuiApplication_quit();
        return;
    }
    if (ctx->m_win) {
        /* 主窗先显路径：登录窗收起 + 页面数据源换远端。
         * 登录窗延迟到事件循环顶层再隐藏/删除（回调栈在 XLink 会话泵里，
         * 此处同步析控件会拆掉正在派发的事件链）。 */
        HsfMonitorVfd_rebindProvider();
        ctx->m_pending = 2;      /* 2=收登录窗（不建主窗）。 */
        s_launchCtx = ctx;
        XPrintf("[login-done] 主窗已就绪，收登录窗并接入远端后端\n");
        return;
    }
    ctx->m_pending = 1;
    s_launchCtx = ctx;
    XPrintf("[login-done] pending set\n");   /* 主窗装配延迟到事件循环顶层（泵里），
                             * 回调栈（XLink stateChanged）内建顶层窗口
                             * 实测卡死——回调不得反调重活。 */
}

/**
 * @brief 延迟收尾泵：登录成功后在此（事件循环顶层）收登录窗/建主窗。
 */
static bool hsfUiLaunchPump(void* userData)
{
    HsfUiLoginCtx* ctx = (HsfUiLoginCtx*)userData;
    if (!ctx || !ctx->m_pending) return true;
    if (ctx->m_pending == 2) {
        ctx->m_pending = 0;
        s_launchCtx = NULL;
        if (ctx->m_login) {
            XWidget_hide((XWidget*)ctx->m_login);  /* 登录窗退出时统一回收。 */
        }
        XPrintf("[launch-pump] 登录窗已收起\n");
        return true;
    }
    XPrintf("[launch-pump] enter\n");
    ctx->m_pending = 0;
    s_launchCtx = NULL;
    ctx->m_win = hsfUiMakeWin(ctx->m_opts);
    if (!ctx->m_win) {
        ctx->m_failed = 2;
        XGuiApplication_quit();
        return true;
    }
    if (ctx->m_login) {
        XWidget_hide((XWidget*)ctx->m_login);   /* 登录窗退出时统一回收。 */
    }
    return true;
}

int HsfUi_start(const HsfUiOptions* opts)
{
    HsfUiOptions defOpts;
    HsfUiLoginCtx ctx;
    HsfLoginWindow* login = NULL;
    XHandle launchPump = 0;
    XGuiApplication* app;
    int result;

    if (!opts) {
        memset(&defOpts, 0, sizeof(defOpts));
        defOpts.startPage = -1;
        defOpts.thenPage = -1;
        opts = &defOpts;
    }
    app = XGuiApplication_instance();
    if (!app) {
        XPrintf("HsfClient: XGuiApplication 未创建\n");
        return 1;
    }

    memset(&ctx, 0, sizeof(ctx));
    ctx.m_opts = opts;

    if (opts->noLogin) {
        /* 离线演示：跳过登录直进主窗（变频器页回落本地模拟源）。 */
        ctx.m_win = hsfUiMakeWin(opts);
        if (!ctx.m_win) return 1;
    } else if (opts->loginSpec) {
        /* 无头登录（CI/联调）：不建窗口，回调里装配主窗。 */
        if (!HsfLogin_startHeadless(opts->loginSpec, hsfUiLoginDone, &ctx)) {
            XPrintf("HsfClient: 无头登录发起失败（spec=%s）\n",
                    opts->loginSpec);
            return 2;
        }
        if (ctx.m_failed) {
            /* 连接失败在 open 的调用栈内同步回调（TCP 立即拒绝）：
             * quit() 早于 exec() 会丢失，这里直接快速返回。 */
            HsfLinkClient_close();
            HsfSettingsStore_close();
            return 2;
        }
    } else {
        /* [主窗先显·登录窗压上] 先建主窗（皮肤/窗口设置恢复在建窗内
         * 完成，见 hsf_win_create 的 hsf_page_window_restoreSettings）——
         * 登录窗随后以主窗为父创建，天然继承用户已选皮肤，并按其客户区
         * 居中；否则登录窗先建，恒停在默认浅色皮肤（2026-11-04 实锅）。
         * 主窗先显示也让用户看到「登录遮罩盖在真实主界面上」的形态，
         * 未登录期间由登录窗的应用模态门挡住主窗输入。
         * [截图模式] --login-shot 要截的是登录窗本身，主窗不建
         * （否则离屏主窗与登录窗同屏抢镜头，且登录窗无父可居中）。 */
        if (opts->loginShot && opts->shotPath) {
            /* 只截登录窗：先把用户皮肤装上（本条路径不建主窗，没人跑
             * hsf_page_window_restoreSettings），再建窗——截图与产品
             * 形态一致（CI 基线不因缺主窗而失真）。 */
            hsf_page_window_applyStoredSkin();
            login = HsfLoginWindow_create(hsfUiLoginDone, &ctx);
            if (!login) {
                XPrintf("HsfClient: 登录窗创建失败\n");
                return 1;
            }
            ctx.m_login = login;
            HsfLoginWindow_armShot(login, opts->shotPath,
                                   opts->shotWidth, opts->shotHeight);
        } else {
            ctx.m_win = hsfUiMakeWin(opts);
            if (!ctx.m_win) return 1;
            login = HsfLoginWindow_createFor((XWidget*)ctx.m_win,
                                             hsfUiLoginDone, &ctx);
            if (!login) {
                XPrintf("HsfClient: 登录窗创建失败\n");
                XClassDelete((XClass*)ctx.m_win);
                ctx.m_win = NULL;
                return 1;
            }
            ctx.m_login = login;
        }
    }

    /* 延迟装配泵：登录成功 → 事件循环顶层建主窗（覆盖窗口/无头两路）；
     * 离线（--no-login）无登录意图不注册（cleanup 摘泵需有效句柄）。 */
    if (!opts->noLogin)
        launchPump = XAbstractEventDispatcher_addPollCallback(hsfUiLaunchPump, &ctx);

    result = XGuiApplication_exec();
    XPrintf("HsfClient: 事件循环退出（返回=%d）\n", result);

    /* 清理次序（实测教训）：① 先摘两个 poll 泵（launch 泵持栈指针、
     * 帧泵持主窗指针）——disconnectFromServer 排空写队列时会**嵌套泵
     * 事件循环**，泵若还挂着就会踩已删对象；② 再关 XLink 链路（此
     * 时主窗仍活着，嵌套泵安全）；③ 最后删窗（页面为堆块子树随窗
     * 级联析构）与设置落盘。 */
    XAbstractEventDispatcher_removePollCallback(launchPump);
    if (ctx.m_win && ctx.m_win->m_framePump)
        XAbstractEventDispatcher_removePollCallback(ctx.m_win->m_framePump);
    HsfLinkClient_close();
    /* [父子析构序] 登录窗以主窗为父（HsfLoginWindow_createFor），删主窗
     * 会级联删登录窗——必须先单独删登录窗并断开父子关系，否则随后对
     * login 的第二次 XClassDelete 就是 UAF。这里按「先子后父」显式收口；
     * 无父路径（--login-shot）同样适用（login 无父时自行释放）。 */
    if (login) {
        XWidget* w = (XWidget*)login;
        XWidget_setParent(w, NULL, XWidget_windowFlags(w));
        XClassDelete((XClass*)login);
        login = NULL;
    }
    if (ctx.m_win)
        XClassDelete((XClass*)ctx.m_win);
    /* 设置落盘收尾（析构内含 sync；此处显式收口，覆盖非 aboutToQuit
       退出路径——如自动退出定时器/外部 quit）。 */
    HsfSettingsStore_close();
    return ctx.m_failed ? ctx.m_failed : result;
}
