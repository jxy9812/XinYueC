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
/**
 * @brief 水质块数据桥：浊度仪推送 → 灌页面。
 * @note  NTU 是线上定点整数 ×100（协议层口径），显示前 ÷100。
 */
static void hsfWinTurbSync(void* userData)
{
    const HsfLinkTurbiditySet* t = HsfLinkClient_turbidity();
    int i;
    (void)userData;
    if (!t) return;
    for (i = 0; i < t->m_count; ++i) {
        HsfMonitorFlowMeter_setTurbidity(i, t->m_items[i].m_name,
                                         (double)t->m_items[i].m_ntu100 / 100.0,
                                         t->m_items[i].m_online,
                                         t->m_items[i].m_tempValid,
                                         (double)t->m_items[i].m_temp100 / 100.0);
    }
}

/**
 * @brief 水质块数据桥：泵推送 → 灌页面。
 * @note  定义在文件后段（HsfWin 完整定义之后）：本函数要调
 *        hsfWinApplyPumpGate(HsfWin*)，而此处 HsfWin 还是不完整类型，
 *        直接引用会让 MSVC 把前置声明与定义的形参视为不同基类型
 *        （error C2371）。
 */
static void hsfWinPumpSync(void* userData);

/**
 * @brief 泵控制出口（启动/停止）。
 * @note  泵无反馈：命令已发出即显示命令状态（页上有常驻说明）。
 */
static void hsfWinPumpWrite(void* userData, bool on)
{
    (void)userData;
    HsfLinkClient_pumpControl(0, on);
}

static void hsfWinValveSync(void* userData);
static void hsfWinTurbSync(void* userData);
static void hsfWinPumpSync(void* userData);
static void hsfWinPumpWrite(void* userData, bool on);
static void hsfWinValveWrite(void* userData, int index, bool wantOpen);
static void hsfWinValveAlarm(void* userData, const HsfLinkValveAlarm* alarm);
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
#include "XMemory.h"         /* XMemory_statistics_2（XHSF_HEAP_TRACE 压测）。 */
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
    HsfDataMenu_create,        /* 8  数据 子菜单 */
    HsfDataHistory_create,     /* 9  数据-历史数据 */
    HsfDataLog_create,         /* 10 数据-日志 */
    HsfDataAlarm_create,       /* 11 数据-报警 */
    HsfSettingsMenu_create,    /* 12 设置 子菜单 */
    HsfSettingsLocal_create,   /* 13 设置-本地设置 */
    HsfSettingsRemote_create,  /* 14 设置-远端设置 */
    HsfNetworkPage_create,     /* 15 设置-网络设置（移植） */
    HsfOverlayPage_create,     /* 16 设置-悬浮窗设置（移植） */
    HsfWindowPage_create       /* 17 设置-窗口设置（移植） */
};

/* ---- 形态/导航度量（单一事实源） ----
 * [2026-11-09 竖屏三档化] 窗口宽 <860 ⇒ 窄档（NARROW：侧栏收起+页级
 * compact 全集），覆盖 720×1280 手机竖屏 / 768 平板竖屏 / ~800 半屏——
 * 旧断点 600 下 720×1280 落宽屏档，导航侧栏挤压内容区只剩 ~530px，
 * 页面横排爆宽（勘察台账 HsfMonitorVfd.c:2565 实锅）。520 以下为手机
 * PHONE 档：窗级不再单列，由各页按**页根宽**细档承接（VFD 620/数据条
 * 1016/窗口页 520 等既有页宽档全部保留——页宽口径本来就是正确的）。
 * 横屏 ≥860 零变化（1280×720 基线不受影响）。 */
#define HSF_COMPACT_MAX_W   860  /**< 窄档断点（逻辑宽 < 860 ⇒ 侧栏收起+页级 compact）。 */
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
    NULL, NULL,          /* 1 主菜单 / 2 监视子菜单（菜单页无分档） */
    HsfMonitorDigital_adapt, /* 3 监视-数字量（竖屏窄列/触控高/顶行堆叠） */
    HsfMonitorAnalog_adapt,  /* 4 监视-模拟量（竖屏窄列/触控高） */
    HsfMonitorVfd_adapt, /* 5  监视-变频器（仪表横排/纵排） */
    HsfMonitorFlowMeter_adapt, /* 6  监视-电磁流量计（仪表横排/纵排） */
    HsfMonitorValve_adapt,     /* 7  监视-电磁阀（3 列/1 列） */
    NULL,                /* 8  数据子菜单 */
    HsfDataHistory_adapt,/* 9  数据-历史数据（自建根结构，竖屏收边距） */
    HsfDataTablePage_adapt, /* 10 数据-日志（HsfDataTablePage） */
    HsfDataAlarm_adapt,  /* 11 数据-报警（自建根结构，竖屏收边距） */
    NULL,                /* 12 设置子菜单 */
    HsfSettingsLocal_adapt, /* 13 设置-本地设置（自建根结构，竖屏收边距） */
    HsfSettingsRemote_adapt, /* 14 设置-远端设置（2026-11-09 补窄档：收边距+store 行翻排） */
    hsfAdaptNetwork,     /* 15 设置-网络设置（移植页） */
    hsfAdaptOverlay,     /* 16 设置-悬浮窗设置（移植页） */
    hsfAdaptWindow       /* 17 设置-窗口设置（移植页） */
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
    int          m_adaptWidth[HSF_PAGE_COUNT]; /**< 上次 adapt 的页宽（-1=未适配）。 */
    int          m_adaptCompact;  /**< 上次 adapt 的形态档（-1=未适配）。 */
    int          m_navSigW;       /**< 上次导航重排时的窗宽（-1=未排）。 */
    int          m_navSigH;       /**< 上次导航重排时的窗高。 */
    int          m_navSigPage;    /**< 上次导航重排时的当前页。 */
    int          m_navSigMode;    /**< 上次导航重排时的形态档。 */
    int64_t      m_pumpLastMs;    /**< 帧泵上一拍时刻（节流用）。 */
    XWidget*     m_pageScroll[HSF_PAGE_COUNT]; /**< 各页的滚动壳（切页时复位滚动位用；可 NULL）。 */
    int          m_pageBeforeMenu; /**< 打开菜单页前的原页（紧凑态 ☰
                                    *   菜单开合用；形态切回宽屏时据此
                                    *   退出菜单页——同 demo 的
                                    *   m_pageBeforeMenu 口径）。 */
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
    uint32_t     m_subTopics;      /**< 当前已订阅的数据主题**集合**（位掩码；
                                    *   -1=无：当前页不吃遥测）。 */
    XTimerId     m_cycleTimer;     /**< 页轮转自测定时器（按需订阅验证用）。 */
    XTimerId     m_valveSelfTestTimer; /**< 阀自测定时器（XHSF_VALVE_SELFTEST）。 */
    int          m_cyclePage;      /**< 轮转中的当前页。 */
} HsfWin;

XCLASS_DEFINE_BEGING(HsfWin)
XCLASS_DEFINE_EXTEND_END(HsfWin, XWidget)

/* 泵控制闸重算（定义在模式/设备回调区；切页、泵推送、模式变更都要用）。
 * 放在 HsfWin 完整定义之后：前置声明处若 HsfWin 仅是不完整类型，MSVC 会把
 * 声明与定义的形参类型视为不同基类型（error C2371）。 */
static void hsfWinApplyPumpGate(HsfWin* win);

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
static uint32_t hsf_pageTopicMask(int pageIndex)
{
    switch (pageIndex) {
    /* [主界面补订阅 2026-11-06] 主界面消费四主题：搅拌泵卡读变频器遥测、
     * 压差卡读 AI 通道 0（压差计）、流量计卡读流量计（B 侧）、电磁阀卡读
     * 六路阀状态。VFD 主题原先只在变频器页订阅 ⇒ 首次启动停在主界面时
     * 卡片永远"无数据"（首启实测）。Python 客户端是连接期全局订阅全部
     * 主题（bridge.py TOPIC_KIND 全集），故无此问题。
     *
     * [2026-11-08 温湿度不再进本表] 主界面左上「电箱温湿度」卡读 TempHum
     * 主题。此前把 TempHum 加进本表"补订"，但温湿度已是**常驻订阅**
     * （HsfLinkClient 连接期恒订，见 hsfLcSubscribeTopics），造成重复登记：
     * 离开首页时引用归零 → 真的发退订帧 → 紧接着常驻补订又发订阅帧，
     * 每次切页产生一对多余帧，并短暂摘掉顶栏温湿度依赖的订阅。
     * 卡片与顶栏读同一份 model，常驻订阅已足够覆盖，故这里移除。 */
    case HSF_PAGE_HOME:
        return (1u << (int)HsfLinkTopic_Vfd) |
               (1u << (int)HsfLinkTopic_Ai) |
               (1u << (int)HsfLinkTopic_FlowMeter) |
               (1u << (int)HsfLinkTopic_Valve) |
               /* [2026-11-09 首页新增两卡] 浊度仪卡读 TURBIDITY 主题、
                * 按钮组卡读 DIGITAL 主题（DI12~15 触点 + DO7~9 三灯）。
                * 不订则两卡恒 "--"（与当初 VFD 主题漏订同一个坑）。 */
               (1u << (int)HsfLinkTopic_Turbidity) |
               (1u << (int)HsfLinkTopic_Digital);
    case HSF_PAGE_MONITOR_VFD:       return 1u << (int)HsfLinkTopic_Vfd;
    case HSF_PAGE_MONITOR_ANALOG:    return 1u << (int)HsfLinkTopic_Ai;
    /* [水质监测页三主题] 流量计 + 浊度仪 + 泵同属一条工艺链，放一页看
     *  （对齐 Python 客户端的「水质监测」页）。位掩码模型支持一页多主题。 */
    case HSF_PAGE_MONITOR_FLOWMETER:
        return (1u << (int)HsfLinkTopic_FlowMeter) |
               (1u << (int)HsfLinkTopic_Turbidity) |
               (1u << (int)HsfLinkTopic_Pump);
    case HSF_PAGE_MONITOR_VALVE:     return 1u << (int)HsfLinkTopic_Valve;
    /* [数字量页补订阅] 原先该页只在连接期拉一次快照 + 控制应答时刷新，
     * 页面停留期间看不到实时变化（DO 被现场按钮改动时界面不动）。订阅
     * 数字量主题后随推送实时刷新（服务端 200ms 一拍）。 */
    case HSF_PAGE_MONITOR_DIGITAL:   return 1u << (int)HsfLinkTopic_Digital;
    default:                         return 0;
    }
}

/**
 * @brief 页数据订阅随切页迁移（旧页退订 → 新页订阅）。
 * @note  服务端对**新订阅**立即定向补发一拍，故切到某页立即可见数据，
 *        不必等推送周期。同一主题重复进入不重复加减（引用计数）。
 */
static void hsf_pageTopicSwitch(HsfWin* self, int index)
{
    const uint32_t next = hsf_pageTopicMask(index);
    const uint32_t cur = self->m_subTopics;
    int i;
    if (!self) return;
    if (next == cur) {
        /* [集合没变也要补订] 快路径原本直接 return：若上一次订阅帧因链路
         * 抖动/未就绪而发送失败，这里的 m_topicRef>0 会永久悬空——服务端
         * 收不到 Subscribe 就不推，界面表现为切回主界面后某几张卡永久
         * "--"（实测：page 7 → 主界面，流量计卡停在「-- 无数据」）。
         * ensureSubscriptions 只补发不计数（引用仍由 subscribe/unsubscribe
         * 记账），已订阅主题由会话层幂等短路，故可无条件调用。 */
        (void)HsfLinkClient_ensureSubscriptions();
        return;
    }
    /* 位掩码模型：一页可同时订阅多个主题（退订旧集合 → 订阅新集合；
     * 逐主题走引用计数，重复进入同一页不重复加减）。 */
    for (i = 0; i < (int)HsfLinkTopic_Count; ++i) {
        const uint32_t bit = 1u << i;
        if ((cur & bit) && !(next & bit))
            HsfLinkClient_unsubscribe((HsfLinkTopic)i);
        else if (!(cur & bit) && (next & bit)) {
            if (!HsfLinkClient_subscribe((HsfLinkTopic)i))
                XPrintf("HsfClient: 页面主题订阅未生效（未登录？page=%d topic=%d）\n",
                        index, i);
        }
    }
    self->m_subTopics = next;
    /* 订阅集合已迁移：把上一轮可能发送失败的引用再兜一次（同上）。 */
    (void)HsfLinkClient_ensureSubscriptions();
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
    /* [切到/离开水质页都重算泵闸] 首次进入时按钮使能必须已反映当前
     * 运行模式（否则自动模式下进去一看就是可点，点完才被服务端拒）。 */
    hsfWinApplyPumpGate(self);
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
    /* [只在页宽/形态档真变时重跑] 帧泵每轮都调本函数；各页 adapt 会改布局
     * 方向/边距并 activate，即使值没变也会清缓存+重排+重绘（实测帧率数千、
     * CPU 恒满）。按“页宽 + 形态档”去重。 */
    for (i = 0; i < HSF_PAGE_COUNT; ++i) {
        int w;
        if (!self->m_pageRoot[i] || !kHsfPageAdapt[i]) continue;
        w = XWidget_width(self->m_pageRoot[i]);
        if (w <= 0) continue;
        if (self->m_adaptWidth[i] == w &&
            self->m_adaptCompact == (compact ? 1 : 0))
            continue;
        if (self->m_adaptCompact != (compact ? 1 : 0)) {
            int k;
            for (k = 0; k < HSF_PAGE_COUNT; ++k) self->m_adaptWidth[k] = -1;
            self->m_adaptCompact = compact ? 1 : 0;
        }
        self->m_adaptWidth[i] = w;
        kHsfPageAdapt[i](self->m_pageRoot[i], compact);
    }
}
/* ---- 形态策略（宽屏/竖屏） ---- */
/* [本地设置·强制紧凑档] -1=跟随窗口宽度自动判定（缺省）；1=强制窄档。
 * 本地设置页「紧凑模式」开关写 1/解除写 -1（Display/Compact 键持久化，
 * 主窗装配时恢复）。0 无语义（不提供「强制宽屏」）。 */
static int s_forceCompact = -1;
/** @brief 活动主窗（本地设置页翻转形态档后即时生效用；单窗应用）。 */
static HsfWin* s_activeWin = NULL;

/** @brief 当前形态：0=宽屏（侧栏停靠）1=紧凑（竖屏，强制收侧栏）。 */
static int hsf_layoutModeOf(HsfWin* self){
    if (!self) return 0;
    if (s_forceCompact >= 0) return s_forceCompact;
    return (XWidget_width(&self->m_base) < HSF_COMPACT_MAX_W) ? 1 : 0;}
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

void HsfUi_setForceCompact(bool on)
{
    s_forceCompact = on ? 1 : -1;
    if (s_activeWin && s_activeWin->m_uiReady)
        hsf_layoutModeUpdate(s_activeWin); /* 内含重分档/导航/整窗重绘。 */
}

/* ---- 页面字体缩放（本地设置 FontScale/Page<N>，50–200%，100=默认） ----
 * [2026-11-07] 各页把写死的字号（及与字号联动的高度）过 hsfUi_pageFontPx
 * 折算，本地设置页「字体大小」区即可按页调；首次调用惰性读键，改动在
 * 下次建页生效（与刷新周期同口径——页实例不换则维持建页时折算）。
 *
 * [2026-11-09 安卓] 缺省按**平台**取值，不按窗口形态取值：页面在建页期
 * 就要读字号折算行高，而那时主窗尚未登记到 s_activeWin（形态未定），
 * 按形态取会拿到宽屏缺省。手机屏逻辑宽约 400（dpr 3），桌面口径的
 * 11~13px 折算后物理尺寸偏小 → 安卓缺省抬到 130%；桌面维持 100%
 * （行为零变化）。用户仍可在本地设置页逐页调，覆盖缺省。 */
#if defined(__ANDROID__)
#define HSF_FONT_PCT_DEFAULT 130
#else
#define HSF_FONT_PCT_DEFAULT 100
#endif

static int  s_pageFontPct[HSF_PAGE_COUNT];
static bool s_pageFontLoaded = false;

int hsfUi_pageFontPx(int page, int px)
{
    if (page < 0 || page >= HSF_PAGE_COUNT) return px;
    if (!s_pageFontLoaded) {
        char key[32];
        int i;
        for (i = 0; i < HSF_PAGE_COUNT; ++i) {
            snprintf(key, sizeof(key), "FontScale/Page%d", i);
            s_pageFontPct[i] = HsfSettingsStore_getInt(key,
                                                       HSF_FONT_PCT_DEFAULT);
            if (s_pageFontPct[i] < 50)  s_pageFontPct[i] = 50;
            if (s_pageFontPct[i] > 200) s_pageFontPct[i] = 200;
        }
        s_pageFontLoaded = true;
    }
    return (px * s_pageFontPct[page] + 50) / 100;
}

/* ---- 实时字号：每页自查重放（不遍历控件树） ---- */
static void (*s_pageFontApply[HSF_PAGE_COUNT])(XWidget* page, int pct);
static XWidget* s_pageFontRoot[HSF_PAGE_COUNT];

int hsfUi_pageFontPct(int page)
{
    char key[32];
    int pct;
    if (page < 0 || page >= HSF_PAGE_COUNT) return HSF_FONT_PCT_DEFAULT;
    snprintf(key, sizeof(key), "FontScale/Page%d", page);
    pct = HsfSettingsStore_getInt(key, HSF_FONT_PCT_DEFAULT);
    if (pct < 50)  pct = 50;
    if (pct > 200) pct = 200;
    s_pageFontPct[page] = pct;
    s_pageFontLoaded = true;
    return pct;
}

void hsfUi_registerPageFontApplier(int page, XWidget* root,
                                   void (*fn)(XWidget* page, int pct))
{
    if (page < 0 || page >= HSF_PAGE_COUNT) return;
    s_pageFontRoot[page] = root;
    s_pageFontApply[page] = fn;
}

void hsfUi_reapplyPageFonts(int page)
{
    int pct;
    if (page < 0 || page >= HSF_PAGE_COUNT) return;
    pct = hsfUi_pageFontPct(page);
    if (s_pageFontApply[page] && s_pageFontRoot[page]) {
        s_pageFontApply[page](s_pageFontRoot[page], pct);
        XWidget_update(s_pageFontRoot[page]);
        XPrintf("HsfUi: 页%d 字号实时重放 %d%%\n", page, pct);
        return;
    }
    /* 无专用重放函数的页（数字量/模拟量/流量计）：它们的 adapt 已经用
     * hsfUi_pageFontPx 重设字号/行高。清掉该页的 adapt 去重标记再重跑。 */
    if (s_activeWin && s_pageFontRoot[page]) {
        s_activeWin->m_adaptWidth[page] = -1;
        hsf_adaptPages(s_activeWin);
        XWidget_update(s_pageFontRoot[page]);
        XPrintf("HsfUi: 页%d 字号实时重放（adapt）%d%%\n", page, pct);
    }
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

/** @brief 分割条拖拽：更新导航面板宽度记忆（仅真实拖拽触发）。
 *  @note 条目几何不在这里重排：拖拽改的是面板几何，框架随即同步派发
 *        RESIZE，HsfNavPanel 的 resizeEvent 重载负责即时重排（拖动过程
 *        逐帧跟手，同 Qt 布局激活语义，见 HsfNavPanel.c）。 */
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
 * @brief [诊断] 标题栏温湿度**显示文本**变化时打一行（env 门控）。
 * @param self 主窗。
 * @details `XHSF_TEMPHUM_TRACE=1` 打开。文本直接取自标题栏两个 XLabel
 *          （`XLabel_text` + `XString_toUtf8`），即"屏幕上真正显示的字符串"，
 *          不是拿快照值重新格式化——验收/现场取证要的是显示口径本身。
 *          服务端重启这类"数据中断→恢复"的场景用它做日志断言：
 *          正常值 → `--℃/--%` → 恢复值，三行即可。只在文本变化时输出，
 *          1Hz 时钟节拍下不刷屏。
 * @note  离屏截图（--shot）模式下 XLink 推送恒不到达，网络数据不能用截图
 *        验收；本诊断就是替代通道（与 HSF_HEAP_TRACE 同类，env 门控常驻）。
 */
static void hsf_titleTraceClimate(HsfWin* self)
{
    static int s_enabled = -1;
    static char s_lastTemp[32];
    static char s_lastHum[32];
    const XString* ts;
    const XString* hs;
    const char* tu;
    const char* hu;
    const char* env;
    if (!self || !self->m_titleBar.m_temp || !self->m_titleBar.m_humid) return;
    if (s_enabled < 0) {
        env = XSystem_environment("XHSF_TEMPHUM_TRACE");
        s_enabled = (env && env[0] && env[0] != '0') ? 1 : 0;
    }
    if (!s_enabled) return;
    ts = XLabel_text(self->m_titleBar.m_temp);
    hs = XLabel_text(self->m_titleBar.m_humid);
    tu = ts ? XString_toUtf8(ts) : NULL;
    hu = hs ? XString_toUtf8(hs) : NULL;
    if (!tu) tu = "";
    if (!hu) hu = "";
    if (strcmp(tu, s_lastTemp) == 0 && strcmp(hu, s_lastHum) == 0) return;
    snprintf(s_lastTemp, sizeof(s_lastTemp), "%s", tu);
    snprintf(s_lastHum, sizeof(s_lastHum), "%s", hu);
    XPrintf("[temphum-title] temp=%s hum=%s\n", s_lastTemp, s_lastHum);
}

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
 * @note  [自愈 2026-11-09] 本函数是**唯一**会按新鲜度把温湿度打成"--"的
 *        地方（首页卡片是事件驱动刷新，拿到最后一帧就一直显示）。而服务端
 *        周期推送一旦停摆（节拍停摆/推送失步队列满/订阅登记丢失），客户端
 *        就只剩「订阅即补发」那一帧 ⇒ 3.5s 后标题栏恒显「--℃ / --%」，
 *        而服务端数据其实一直有（history 行 temp=26.5 恒有值）。
 *        故本函数**每拍**调一次补拍通道（快照命令，请求-应答兜底）：应答
 *        到达即刷新 model，标题栏下一拍自愈。是否真发请求由
 *        HsfLinkClient_tempHumEnsureFresh 按预取阈值 + 节流判定（数据够新
 *        时立即早退），不会变成轮询风暴。
 */
static void hsf_titleSyncClimate(HsfWin* self)
{
    const HsfLinkTempHum* th;
    if (!self) return;
    th = HsfLinkClient_tempHum(HSF_SIDE_A);
    if (!th || !HsfLinkClient_tempHumFresh(HSF_SIDE_A)) {
        HsfTitleBar_setClimate(&self->m_titleBar, 0.0, 0.0, false, false);
    } else {
        HsfTitleBar_setClimate(&self->m_titleBar,
                               (double)th->m_tempC10 / 10.0,
                               (double)th->m_humPct10 / 10.0,
                               th->m_tempValid, th->m_humValid);
    }
    /* 自愈补拍：**每拍都过一遍**，不放在上面「已判无数据」的分支里。
     * [为什么必须移出来 2026-11-09 实测] 放在 stale 分支里 ⇒ 补拍请求只能
     * 等到界面**已经**落成「--℃」才发出，应答再写回真值 ⇒ 推送停摆期间
     * 标题栏每 ~4s 闪一次「--」（实测 40s 闪 10 次，age 序列 0/1.0/2.0/3.0
     * → stale 4.0s）。移出来后按预取阈值（HSF_LINK_TEMPHUM_PREFETCH_MS）
     * 在过期**之前**补，新鲜度判定就不会翻假，界面不闪。
     * 数据正常时（推送 500ms 一拍）age 恒小于预取阈值 ⇒ 函数内立即早退，
     * 一拍一次调用的开销可以忽略。 */
    (void)HsfLinkClient_tempHumEnsureFresh(HSF_SIDE_A);
    hsf_titleTraceClimate(self);
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
        /* [活动侧 2026-11-10] 流量计在哪侧由服务端告知，客户端不写死。 */
        return HsfLinkClient_flowMeterActiveFresh();
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
        /* [遥测自愈 2026-11-09] 当前页订阅的遥测按预取阈值补拍：推送停摆
         * （服务端节拍停了/推送失步被拒/订阅登记丢了）时，界面靠这条 1Hz
         * 兜底自己把数据要回来，不必等切页或重连。是否真发请求由
         * HsfLinkClient_ensureFreshTopics 逐主题判新鲜度 + 节流，数据正常时
         * 零请求（推送周期 100~500ms，新鲜度远高于预取阈值）。 */
        HsfLinkClient_ensureFreshTopics(self->m_subTopics);
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
    /* [阀自测钩子] 到点开阀一次（用于无人值守取证"卡死阀 20s 超时
     * 告警"这类需等待的现场事件）。用**对象自带定时器**（项目裁定：
     * 业务对象内部拍子走 XObject 定时器，不用独立 XTimer 实例）。 */
    if (self->m_valveSelfTestTimer != XTIMER_INVALID_ID &&
        XTimerEvent_timerId(event) == self->m_valveSelfTestTimer) {
        XObject_killTimer((XObject*)self, self->m_valveSelfTestTimer);
        self->m_valveSelfTestTimer = XTIMER_INVALID_ID;
        {
            const char* spec = XSystem_environment("XHSF_VALVE_SELFTEST");
            int index = 0;
            if (spec && strncmp(spec, "open:", 5) == 0) index = atoi(spec + 5);
            XPrintf("[valve-selftest] 开阀 路号=%d\n", index);
            HsfLinkClient_valveControl(index, true);
        }
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
 *           轮询间隔算 FPS 会得到「轮询率」（数千 FPS，无意义）。
 *           【帧耗时口径 2026-11-08·整窗】本窗是**组合控件树**：顶层
 *           paintEvent 只画顶层自身，子控件（页/表盘/状态栏/悬浮层）在
 *           paintTree 递归里随后绘制。原先在顶层 paintEvent 前后取时，
 *           读数只含顶层自身背景填充 ⇒ 恒为 0.00ms（验收实测六页均如此），
 *           无法反映整窗绘制成本。现改为读框架在 paintTree **根调用**
 *           两端记下的整树耗时（XWidget_lastPaintTreeUsecs，含全部可见
 *           子控件）：顶层 paintEvent 发生在本次树起点，读到的是**上一帧**
 *           的完整耗时，因此把样本起点回拨该耗时、终点取当前时刻——差值
 *           恒等于整窗绘制耗时；样本终点（当前时刻）仍每帧一个，FPS 口径
 *           不变。首帧无上一帧可参照，耗时报 0（单个样本，不影响窗口均值）。 */
static void VHsfWin_paintEvent(XWidget* self, XEvent* event)
{
    HsfWin* win = (HsfWin*)self;
    int64_t frameStartUsecs;
    int64_t frameEndUsecs;
    int64_t lastTreeUsecs;
    XClass_Parent(XWidget, EXWidget_PaintEvent,
                  void (*)(XWidget*, XEvent*))(self, event);
    if (!win) return;
    {
        /* [诊断] XHSF_PAINT_TRACE=1：逐 paintEvent 打出矩形/整树耗时。 */
        static int s_trace = -1;
        if (s_trace < 0)
            s_trace = XSystem_environment("XHSF_PAINT_TRACE") ? 1 : 0;
        if (s_trace && event && XEvent_type(event) == XEVENT_TYPE_PAINT) {
            XRect tr = XPaintEvent_rect((const XPaintEvent*)event);
            XPrintf("[paint] page=%d rect=%d,%d %dx%d tree=%lldus\n",
                    HsfStack_current(&win->m_stack), tr.x, tr.y,
                    tr.width, tr.height,
                    (long long)XWidget_lastPaintTreeUsecs(self));
        }
    }
    frameEndUsecs = XDateTime_currentNSecsSinceEpoch() / 1000LL;
    /* 上一帧整树绘制耗时（根调用至全部子控件绘制完成）。 */
    lastTreeUsecs = XWidget_lastPaintTreeUsecs(self);
    if (lastTreeUsecs > 0 && lastTreeUsecs < frameEndUsecs)
        frameStartUsecs = frameEndUsecs - lastTreeUsecs;
    else
        frameStartUsecs = frameEndUsecs;   /* 首帧/无样本：本帧耗时报 0。 */
    XPerformanceOverlay_updateFrame(&win->m_overlay,
                                    frameStartUsecs, frameEndUsecs);
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

/** @brief 旗舰深色主题恢复（窗口页皮肤恢复之后的覆盖步，2026-11-11）。
 *  @details Window/Skin 键的取值空间扩展到 6（旗舰深色·控制室 墨青，
 *           HSF_THEME_SKIN_DARK）：窗口设置页的规格表路径
 *           （hsf_page_window_applyStoredSkin/restoreSettings →
 *           wn_applySkin）只认 0..5、越界夹回 0（亮色），会把 6 洗成
 *           亮色——本辅助在其后重读键值，命中 6 时经
 *           HsfTheme_applyFlagshipSkin 覆盖回旗舰深色（定制深色调色板 +
 *           hsf_theme_dark_css + Dark 方案；XApplication_setStyleSheet_2
 *           安装并全顶层重绘 = 仓库既有换肤即时生效机制，不引入新脏源）。
 *           启动序在 XWidget_show 之前调用 ⇒ 首帧即深色，不闪亮；
 *           索引 0..5 时本辅助不动作（规格表皮肤照旧）。 */
static void hsfUiApplyFlagshipThemeOverride(void)
{
    int skin = HsfSettingsStore_getInt(HSF_THEME_KEY_SKIN,
                                       HSF_THEME_SKIN_LIGHT);
    if (skin == HSF_THEME_SKIN_DARK)
        HsfTheme_applyFlagshipSkin(skin);
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
 *
 *  [安卓 2026-11-09] 安卓上窗口=全屏画布（无窗口管理器），尺寸必须跟
 *  surface 的**逻辑**尺寸（竖屏手机约 400x869），不能用桌面的 1280x720
 *  横屏基准：那段等比缩放会把 16:9 的横屏比例硬塞进竖屏（400x869 的屏
 *  上算成 360x202 的扁条），页面被压扁，各页 adapt 读到的页根宽也随之
 *  失真（分档全判错）。安卓分支改为「取屏尺寸，取不到给手机量级初值」，
 *  真实全屏几何随后由平台层经 m_geomPending 通道回推
 *  （XPlatformWindowAndroid.c 的 XWindow_setGeometry_rect）。
 */
/**
 * @brief 链路状态栏提示（自动重连被瞬时拒绝等「重试中」反馈）。
 * @param userData 主窗（HsfWin*）。
 * @param textUtf8 提示文本（借用；HsfStatusBar_setText 内部拷贝）。
 * @details 2026-11-09 根修：模拟模式下客户端偶发「全页 --、换个时间重连
 *          又好」的根因之一是自动重连被服务端策略拒绝（该账户已在线）后
 *          框架永久收手。修复后框架退避重试，本回调负责让人看见"正在重试"，
 *          而不是一个静默停住的界面。
 */
static void hsfWinNotice(void* userData, const char* textUtf8)
{
    HsfWin* win = (HsfWin*)userData;
    if (!win || !textUtf8) return;
    HsfStatusBar_setText(&win->m_statusBar, textUtf8);
}

static void hsf_defaultWinSize(int* outW, int* outH)
{
    int w = HSF_WIN_DEFAULT_W;
    int h = HSF_WIN_DEFAULT_H;
    int sw = 0;
    int sh = 0;
    if (!outW || !outH) return;
#if defined(__ANDROID__)
    if (hsf_primaryScreenSize(&sw, &sh) && sw > 0 && sh > 0) {
        *outW = sw;
        *outH = sh;
    } else {
        /* surface 尚未创建（首帧前）取不到屏尺寸：给手机量级初值，避免
         * 首帧按桌面尺寸排布。 */
        *outW = 400;
        *outH = 800;
    }
    return;
#else
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
#endif
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
    self->m_valveSelfTestTimer = XTIMER_INVALID_ID;
    self->m_autoSeconds = (autoSeconds > 0) ? autoSeconds : 0;
    self->m_shotPath = shotPath;
    self->m_navWidth = HSF_NAV_DEFAULT_W;
    self->m_layoutMode = -1; /* 首次必触发形态判定。 */
    /* adapt/导航去重哨位：-1=未适配（首轮必跑）。 */
    self->m_adaptCompact = -1;
    self->m_navSigW = -1;
    self->m_navSigH = -1;
    self->m_navSigPage = -1;
    self->m_navSigMode = -1;
    {
        int k;
        for (k = 0; k < HSF_PAGE_COUNT; ++k) self->m_adaptWidth[k] = -1;
    }
    self->m_pageBeforeMenu = HSF_PAGE_HOME;
    self->m_subTopics = 0;   /* 尚无页面订阅（位掩码 0=无）。 */
    self->m_lastFrameUsecs = 0;
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
#if defined(__ANDROID__)
    /* [安卓无边框 2026-11-09] 安卓没有窗口管理器，框架的 CSD 自绘条件
     * 命中（无原生窗口回落路径）→ 会画出一条桌面式系统标题栏（最小化/
     * 最大化/关闭）。手机上无窗口可最小化，那条标题栏既无意义又挤占顶部
     * 空间：置 FramelessWindowHint 抑制装饰，顶部改由本模块自绘的标题
     * 栏承担（桌面形态保留 CSD 行为不变）。
     * 与 XGuiWindowDemo 的安卓分支同款口径（xgui_window_demo.c）。 */
    XWidget_setWindowFlags(&self->m_base,
                           XWidget_windowFlags(&self->m_base) |
                               (XWidgetFlags)XWindowType_FramelessWindowHint);
#endif
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
    /* [认证瞬时拒绝的状态栏反馈 2026-11-09] 自动重连撞上「该账户已在线」
     * 时，框架退避重试、界面**不再**静默停在 `--`：主窗状态栏显示
     * 「账户可能已在别处登录，正在重试…」，旧会话被服务端回收后自动恢复。
     * 单槽、跨连接复用，故建窗时注册一次。 */
    HsfLinkClient_setNoticeCallback(hsfWinNotice, self);
    /* 电磁阀：快照到达即灌页面（含控制闸重算）；按钮出口走同一链路。 */
    HsfLinkClient_setValveCallback(hsfWinValveSync, self);
    HsfLinkClient_setValveAlarmCallback(hsfWinValveAlarm, self);
    HsfLinkClient_setCommandCallback(hsfWinCommandResult, self);
    HsfMonitorValve_setWriteHandler(hsfWinValveWrite, self);
    /* 数字量/模拟量：点数与值全部由服务端推送决定（客户端不写死）。 */
    HsfLinkClient_setDigitalCallback(hsfWinDigitalSync, self);
    HsfLinkClient_setAiCallback(hsfWinAnalogSync, self);
    HsfMonitorDigital_setWriteHandler(hsfWinDigitalWrite, self);
    HsfMonitorValve_attachStatus(HsfPages_setStatus, self);
    /* 水质块（浊度 + 泵）：与服务端同主题推送，页面只做呈现。 */
    HsfLinkClient_setTurbidityCallback(hsfWinTurbSync, self);
    HsfLinkClient_setPumpCallback(hsfWinPumpSync, self);
    /* [2026-11-07 泵点击无响应根因] 泵的**写出口**不能在这里设：本函数此刻
     * 还没走到建页循环（下方 kHsfBuilders 逐页创建），HsfMonitorFlowMeter
     * 的 s_fmPage 仍为 NULL，setPumpWriteHandler 里的 `if (!p) return;` 会把
     * 回调静默丢弃 ⇒ 按钮可点、槽也进得来，但 m_pumpWrite 恒 NULL，
     * `if (!p->m_pumpWrite) return;` 直接返回（实测诊断日志：
     * `[pump] start slot: write=0000000000000000`）。移到建页之后
     * （见下方「泵写出口」注释处）。 */
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
        s_pageFontRoot[i] = page;   /* 字号实时重放的页根登记 */
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
    /* [泵写出口：必须建页之后设] 页面在本循环里才创建，早于此处调用
     * setPumpWriteHandler 会因 s_fmPage==NULL 被静默丢弃（详见上方注释）。
     * 其它设备的 setWriteHandler 无此问题：它们的页面单例在建页时把自己
     * 注册进模块，模块内部的写出口表与实例解耦；泵页用的是"外部注入
     * 回调"模型，必须先有实例再接回调。 */
    HsfMonitorFlowMeter_setPumpWriteHandler(hsfWinPumpWrite, self);
    HsfStack_go(&self->m_stack, HSF_PAGE_HOME);
    HsfNavPanel_setActivePage(&self->m_navPanel, HSF_PAGE_HOME);
    /* [初始页主题订阅 2026-11-06] 初始页走 HsfStack_go 直显（不经
     * hsf_goPage），页面主题订阅从未发生 ⇒ 首次启动停在主界面时搅拌泵
     * 卡片永远"无数据"（VFD 主题无人订阅，服务端按订阅制不推；首启实测
     * 服务端只收到 0x00010003/5 常驻订阅）。此处补一次与 hsf_goPage 同
     * 口径的主题切换：未登录时先累计引用，登录就绪由 hsfLcSubscribeTopics
     * 按引用补订。startPage 分支随后按需再切。 */
    hsf_pageTopicSwitch(self, HSF_PAGE_HOME);
    /* 指定起始页（截图/调试直达某页）。 */
    if (startPage >= 0 && startPage < HSF_PAGE_COUNT)
        hsf_goPage(self, startPage);
    /* [2026-11-07 --goto 非截图路径根修] `--goto N` 原先只在截图定时器分支
     * （下面的 m_shotTimer 事件里）被消费，于是**不带 --shot 的普通启动
     * 完全忽略 --goto**：命令行写 --goto 6 仍然停在主界面，联调/现场排障
     * 无法直达目标页（实测：客户端日志无任何 switch page 行）。此处与
     * startPage 同处立即切一次；m_thenPage 随后置 -1，截图路径的"出图前
     * 再切一次"语义不变（那时 m_thenPage 已为 -1，该分支自然跳过）。 */
    if (thenPage >= 0 && thenPage < HSF_PAGE_COUNT) {
        self->m_thenPage = -1;
        hsf_goPage(self, thenPage);
    }

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
    /* [旗舰深色 2026-11-11] 窗口页规格表把 Window/Skin 越界值夹回 0，
     * 这里把 6（旗舰深色·控制室）覆盖回去——仍在 show 之前，首帧即深色。 */
    hsfUiApplyFlagshipThemeOverride();
    /* [次级文本随主题重钉 2026-11-11 双主题走查] 页面在上面的装配循环里
     * 已建完（早于本覆盖步），其间 HsfTheme_applySecondaryText 按当时的
     * 亮套钉了说明小字色 ⇒ 深色恢复后那批标签仍持亮色值（#5A6572 落深底
     * ~2.8:1 偏暗）。主题落定后沿主窗子树重钉（精确识别+同值早退，
     * HsfTheme_refreshSecondaryTexts 契约见 HsfTheme.h）。 */
    HsfTheme_refreshSecondaryTexts(&self->m_base);

    /* UI 装配就绪（此前 resize 事件的形态判定一律跳过——侧栏/分割条
     * 尚未建立，判定结果会落在零内存对象上）。 */
    self->m_uiReady = true;

#if defined(__ANDROID__)
    /* [安卓沉浸式全屏 2026-11-09] 安卓无窗口管理器：窗口几何由平台层经
     * m_geomPending 通道回推为全屏（surface 逻辑尺寸），窗口态须与之
     * 对齐——showFullScreen 只翻状态位，几何本就全屏。与 XGuiWindowDemo
     * 的安卓分支同款口径（xgui_window_demo.c）。 */
    XWidget_showFullScreen(&self->m_base);
#else
    XWidget_show(&self->m_base); /* 顶层窗口整体最后显示（子树已就绪）。 */
#endif

    /* 形态初判（宽/窄）+ 导航尺寸初申——**必须在 show 之后**：show 递归
     * 显示子树，先前隐藏的紧凑态侧栏会被重新显示出来（竖屏侧栏不隐）。 */
    /* [本地设置·强制紧凑档恢复] 须在形态初判前读键（-1=跟随自动）。
     * XGuiWindowDemo 同款时机：窗口句柄/几何就绪、尚未进事件循环。 */
    /* [本地设置页已移除「紧凑模式」开关] 不再读 Display/Compact：旧值
     * 会把界面永久锁在窄档且没有 UI 可恢复；形态一律按窗口宽度自动判定。 */
    s_forceCompact = -1;
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
         * （--no-login）路径无网络等待，维持原 1200ms。
         * [XHSF_SHOT_DELAY_MS] 额外延时（毫秒）：用于等"稍后才发生"的现场
         * 事件再取证（如阀超时告警 20s 后才到）——否则截图总是抢在事件前。 */
        {
            const char* delayEnv = getenv("XHSF_SHOT_DELAY_MS");
            unsigned extraMs = 0;
            if (delayEnv && delayEnv[0]) {
                long v = strtol(delayEnv, NULL, 10);
                if (v > 0 && v < 600000L) extraMs = (unsigned)v;
            }
            self->m_shotTimer = XObject_startTimer_ms(
                (XObject*)self,
                (HsfLinkClient_isOpen() ? 3000u : 1200u) + extraMs,
                XTimerType_CoarseTimer);
        }
        if (self->m_shotTimer == XTIMER_INVALID_ID) {
            XPrintf("HsfClient: 截图定时器创建失败\n");
        }
    }
    return self;
}

/* ==================== 全局运行模式（顶栏） ==================== */

/**
 * @brief 把最近一次「设置运行模式」失败原因回显到状态栏；成功不动文案。
 * @param win 归属主窗。
 * @details [为什么两处调用] 0x0105 应答是**异步**的：点击槽里刚下发时
 *          HsfLinkClient_runModeError() 还是上一拍的结论，真正的拒绝原文
 *          （服务端 hsfAppHandleRunMode：「权限不足：切换运行模式需要 root
 *          账户」）要等应答回来才写入。故：
 *            · 点击槽（hsfWinModeSlot）：下发后立刻回显，覆盖"上一拍已
 *              拒绝、本次点击瞬间"的即时反馈；
 *            · 模式应答回调（hsfWinModeNotify）：首点被拒的可见反馈——
 *              此前 err 只存不显，现场看到的就是"点了没反应"。
 *          成功时应答已把错误清空 ⇒ 本函数早退，**不覆盖**既有状态栏
 *          文案（当前页/告警等）。
 */
static void hsfWinModeShowError(HsfWin* win)
{
    const char* err;
    if (!win) return;
    err = HsfLinkClient_runModeError();   /* 借用指针，不得释放。 */
    if (!err || !err[0]) return;
    /* HsfStatusBar_setText 经 XLabel_setText_2 内部拷贝，借用可即刻失效。 */
    HsfStatusBar_setText(&win->m_statusBar, err);
}

/**
 * @brief 顶栏模式钮点击：手动 ⇄ 自动 切换（服务端执行，需 root）。
 * @param userData 归属主窗（HsfWin*）。
 */
static void hsfWinModeSlot(void* userData)
{
    HsfWin* win;
    int autoMode = 0;
    bool known;
    if (!userData) return;
    win = (HsfWin*)userData;
    if (!HsfLinkClient_isReady()) return;
    known = HsfLinkClient_runMode(&autoMode);
    HsfLinkClient_setRunMode(!(known && autoMode));
    /* 下发后回显最近一次失败原因（无权限账户的"点了没反应"由这里兜底；
     * 本条应答到达时 hsfWinModeNotify 会再回显一次同一文案，幂等）。 */
    hsfWinModeShowError(win);
}

/**
 * @brief 统一的设备控制闸判据：算出"为什么点不动"，NULL=可操作。
 * @return 常驻文案（静态区，不需要释放）。
 * @details 判据优先级：未连接 → 自动模式 → 权限不足。服务端
 *          `hsfAppControlGate`（HsfServerApp.c:1560）按同样优先级拒绝，
 *          客户端提前置灰可避免"点了没反应/被拒"的观感。
 *
 *          [为什么收敛成一个函数] 原先阀页/数字量页/泵页各写一份判据，结果
 *          三份互相漂移：阀页文案拼成了「自动运行中：远程控制自动运行中：
 *          远程控制已被禁用」，阀页与数字量页还停在 `role >= HSF_ROLE_CONTROL`
 *          的旧口径（四档权限下角色值不单调），VFD 页则**根本没被驱动过**
 *          （setInteractiveMode 全库无调用者）⇒ 自动模式下变频器页的正转/
 *          反转/点动/给定仍可点。四处同一判据必须同源。
 */
static const char* hsfWinControlBlockReason(void)
{
    int autoMode = 0;
    /* [离线演示不算被禁] 判据的第一刀用 isOpen（链路对象是否建立），不是
     * isReady（是否已认证就绪）：
     *   · --no-login：客户端从未 HsfLinkClient_open ⇒ isOpen=false ⇒ 离线
     *     演示操作的是本地模拟源（变频器页建页时绑 HsfVfdProvider_demo），
     *     界面不该锁死；原先这里按 isReady 返回「未连接服务端」，把离线
     *     演示一并禁掉，属于回归。
     *   · 已登录后网络断开：open 过的客户端对象仍在（HsfLinkClient_close
     *     才会释放），isOpen 仍为 true、isReady 转 false ⇒ 落到下一行返回
     *     「未连接服务端」，闸正常关闭。注意此时**不会**切回本地模拟源
     *     （rebind 只在建窗与登录成功时发生，见 HsfUi.c 的
     *     HsfMonitorVfd_rebindProvider 调用点），故必须禁。
     * 远端不可用但确实该拒的操作，另有服务端 hsfAppControlGate 兜底。 */
    if (!HsfLinkClient_isOpen()) return NULL;
    if (!HsfLinkClient_isReady()) return "未连接服务端";
    if (HsfLinkClient_runMode(&autoMode) && autoMode)
        return "自动运行中：远程控制已被禁用";
    if (!HsfLinkClient_can(HSF_PERM_DEVICE_CONTROL))
        return "权限不足：控制需要操作者及以上账户";
    return NULL;
}

/**
 * @brief 泵/阀/数字量/变频器四页控制闸一次重算。
 * @param win 归属主窗（当前未用，保留接缝）。
 * @details 调用点（五处，全是"状态可能变了"的时机）：切页（hsf_goPage）、
 *          模式变更（hsfWinModeNotify）、泵推送（hsfWinPumpSync）、
 *          阀推送（hsfWinValveSync）、数字量推送（hsfWinDigitalSync）。
 *          幂等且廉价（各页只在值变化时改控件使能）。
 *
 *          [两类闸必须分开驱动] `setInteractiveMode(bool)` 在各页的语义是
 *          **「运行模式」**（true=手动可操作 / false=自动运行只读），它会改
 *          页面文案为「自动运行中：界面只读」并隐藏勾选框。绝不能把
 *          「未连接」「权限不足」也映射到它——那会让离线演示（--no-login）
 *          被锁成只读并误报「自动运行中」（2026-11-08 复审实测回归）。
 *          故这里拆成两条独立信号：
 *            · 运行模式  → setInteractiveMode（只由服务端模式位决定）
 *            · 可操作性  → setOperationEnabled / setControlEnabled
 *
 *          [四个页的可操作性分别落在哪里——不要一律按"传 reason"理解]
 *            · 泵页 / 阀页 / 数字量页：本函数把 `reason == NULL` 直接投给
 *              各页的 setControlEnabled / setOperationEnabled，即
 *              「连接 + 权限 + 模式」求交后的结果。
 *            · 变频器页：**不**由本函数传 reason，本函数只投模式位；它的
 *              连接/权限判定由 provider 闸承担——HsfMonitorVfd.c 的
 *              `gate = !m_autoMode && provider->controlEnabled(...)`，
 *              远端 provider 的 controlEnabled 为
 *              `s_vr.m_gateOpen && HsfLinkClient_can(DEVICE_CONTROL)`
 *              （HsfVfdProviderRemote.c）。未认证时 role=0 ⇒ can() 为假
 *              ⇒ 闸关闭，所以「未就绪」这条也被覆盖；离线（--no-login）
 *              时绑的是 demo provider，其 controlEnabled 读设备
 *              m_controlEnabled（缺省 true）⇒ 离线可操作。文案也由该页
 *              依 provider 闸自行渲染（横幅「界面操作已锁」/提示行）。
 *              若将来要把四页彻底统一口径，再加 setControlEnabled 入口；
 *              现在两侧判据等价，先不让第五个信号源进来。
 *
 *          [本函数是"服务端模式投影"，不是模式唯一来源] 两页的
 *          setInteractiveMode 还承接 **env 自测钩子**（XHSF_DIGITAL_AUTO /
 *          XHSF_VFD_AUTO）建立的演示态。那段态不是本函数能撤销的：钩子在位
 *          时**驱动侧让路**——下面只跳过"退手动"（manualMode==true）这一次
 *          驱动，"进自动"照常下发。否则每拍切页/推送都按服务端手动位把钩子
 *          设的自动只读翻回去（2026-11-08：钩子态一进页就失效）。
 *          让路放在**驱动侧而不是 setInteractiveMode 里**：两页头文件
 *          （HsfMonitorDigital.h:153-162 / HsfMonitorVfd.h:39-50）明写
 *          interactive=true 即退出自动，公开 setter 必须保持诚实——
 *          Test/XGuiTest/hsf_vfd_probe.c ⑨ 段正断言这个往返。
 */
static void hsfWinApplyControlGates(HsfWin* win)
{
    const char* reason;
    bool connected;
    bool manualMode;
    int autoMode = 0;
    (void)win;

    connected = HsfLinkClient_isReady();
    /* 模式未知（未连接）时按手动处理：不把「不知道」误报成「自动运行」。 */
    manualMode = !(connected && HsfLinkClient_runMode(&autoMode) && autoMode);

    reason = hsfWinControlBlockReason();

    /* [顺序有语义：先模式、后可操作闸] 数字量页 setInteractiveMode 的
     * 「退手动」分支会**无条件**把操作闸置 true（该页注释："手动模式即可
     * 操作"）。若反过来先落操作闸再落模式：自动→手动且角色无控制权限那一
     * 拍，操作闸刚被 reason 关掉，紧接着又被模式解锁分支重新置 true ⇒
     * 无权限账户拿到了可操作界面（点了才被服务端拒）。故模式先落定、操作
     * 闸后算，后者是"连接/权限/模式"的最终求交，不被任何分支反超。 */
    /* ---- 运行模式闸（只反映服务端模式位，不做连接/权限推断） ----
     * [env 自测钩子：让路只在这两行] XHSF_VFD_AUTO 与
     * XHSF_DIGITAL_AUTOTEST+XHSF_DIGITAL_AUTO 在建页/自测时把对应页置成
     * 自动只读（演示与截图闭环的前提）。本函数每拍（切页、泵/阀推送、模式
     * 应答）都会重算 manualMode；已连接+手动模式下 manualMode==true，照常
     * 下发就把钩子态翻回手动。故钩子在位时**只跳过"退手动"这一次驱动**，
     * "进自动"（manualMode==false）仍照常下发——服务端切自动必须立刻锁页。
     * 判据与两页建页处读的是同一个 env：HsfMonitorVfd.c 建页段、
     * HsfMonitorDigital.c 的 HsfMonitorDigital_autotest 尾段；改名需三处同改。 */
    {
        bool vfdHook = XSystem_environment("XHSF_VFD_AUTO") != NULL;
        bool digHook = XSystem_environment("XHSF_DIGITAL_AUTOTEST") != NULL &&
                       XSystem_environment("XHSF_DIGITAL_AUTO") != NULL;
        if (!(digHook && manualMode))
            HsfMonitorDigital_setInteractiveMode(manualMode);
        if (!(vfdHook && manualMode))
            HsfMonitorVfd_setInteractiveMode(manualMode);
    }
    /* ---- 可操作闸（连接 / 权限 / 模式三者求交） ---- */
    HsfMonitorFlowMeter_setControlEnabled(reason == NULL, reason);
    HsfMonitorValve_setControlEnabled(reason == NULL, reason);
    HsfMonitorDigital_setOperationEnabled(reason == NULL);
}

/**
 * @brief 泵控制闸重算（保留旧名，语义等于全设备闸重算）。
 * @param win 归属主窗。
 */
static void hsfWinApplyPumpGate(HsfWin* win)
{
    hsfWinApplyControlGates(win);
}

/**
 * @brief 水质块数据桥：泵推送 → 灌页面（定义见上方前置声明）。
 */
static void hsfWinPumpSync(void* userData)
{
    const HsfLinkPumps* p = HsfLinkClient_pumps();
    int i;
    if (!p) return;
    /* [闸门先于灌数据] setPump 会用 m_ctrlEnabled 与 ready/on 求交；若闸门
     * 值还是建页时的初值（false），第一拍推送就会把按钮错误置灰。这里先按
     * 当前连接/模式/权限算一次，再灌。userData 就是主窗（见装配处）。 */
    hsfWinApplyPumpGate((HsfWin*)userData);
    for (i = 0; i < p->m_count; ++i) {
        HsfMonitorFlowMeter_setPump(p->m_items[i].m_name, p->m_items[i].m_on,
                                    p->m_items[i].m_ready, p->m_items[i].m_text);
    }
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
    int running = -1;
    if (!win) return;
    known = HsfLinkClient_runMode(&autoMode);
    HsfTitleBar_setRunMode(&win->m_titleBar, known, autoMode == 1);
    /* 自动运行状态片（"自动模式"≠"控制器在跑"；服务端未上报时显示未知）。 */
    HsfLinkClient_autoRunning(&running);
    HsfTitleBar_setAutoRunning(&win->m_titleBar, running >= 0, running == 1,
                               autoMode == 1);
    /* [被拒要说出来] 无权限账户（admin=普通用户）切模式时服务端应答
     * ok=false + err 原文；此前只存进 HsfLinkClient 的 m_modeErr 无人显示，
     * 现场观感是"点了没反应"。成功时应答已清空错误 ⇒ 此处早退，不改既有
     * 状态栏文案。 */
    hsfWinModeShowError(win);
    /* [模式一变就重算设备控制闸] 服务端 hsfAppControlGate 在自动模式拒绝
     * 一切设备控制；客户端必须在**模式切换的那一刻**把泵按钮置灰，而不是
     * 等用户点了才被拒（截图实测：顶栏"模式: 自动"而泵"启动"仍可点）。
     * 阀页在自己的刷新里已带这道闸，泵页原先没有，这里统一驱动。 */
    hsfWinApplyPumpGate(win);
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
    int i;

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

    /* 闸门由 hsfWinApplyControlGates 统一驱动（本函数只管灌数据 + 重绘）。 */
    hsfWinApplyControlGates((HsfWin*)userData);
    HsfMonitorValve_refresh();
}

/**
 * @brief 阀告警回调：状态栏复述服务端原文 + 阀页常驻横幅。
 * @details 与 Python 客户端 pages_valve._drain_alarms 同口径：客户端**只
 *          复述不判读**（超没超时全在服务端算）；恢复类只走状态栏、不清
 *          横幅（横幅留最近一条非恢复告警直到下一条覆盖）。
 */
static void hsfWinValveAlarm(void* userData, const HsfLinkValveAlarm* alarm)
{
    HsfWin* win = (HsfWin*)userData;
    char buf[192];
    if (!win || !alarm) return;
    if (alarm->m_kind == (int)HSF_VALVE_ALARM_CLEARED) {
        snprintf(buf, sizeof(buf), "%s 已恢复：%s",
                 alarm->m_name, alarm->m_text);   /* «name» 已恢复：«text» */
    } else {
        char banner[192];
        snprintf(buf, sizeof(buf), "%s 告警：%s",
                 alarm->m_name, alarm->m_text);   /* «name» 告警：«text» */
        snprintf(banner, sizeof(banner),
                 "⚠ %s：%s",
                 alarm->m_name, alarm->m_text);   /* ⚠ «name»：«text» */
        HsfMonitorValve_setAlarmBanner(banner);
    }
    HsfStatusBar_setText(&win->m_statusBar, buf);
}

/** @brief 电磁阀页按钮出口：下发阀控制请求。 */
static void hsfWinValveWrite(void* userData, int index, bool wantOpen)
{
    (void)userData;
    HsfLinkClient_valveControl(index, wantOpen);
}

/**
 * @brief 控制应答回调：阀/数字量被拒时把原因打到状态栏。
 * @details [阀控制附处置结果] 服务端的幂等三分支（已到位忽略 / 在途重发 /
 *          反向）只有服务端知道；只回"命令已受理"时用户无法区分"真动了"
 *          与"本来就在那儿"，会以为按钮坏了。处置结果取自应答里的
 *          v{i}_cmdResult（对齐 Python 客户端的 VALVE_CMD_TEXT 映射）。
 */
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
                 "操作失败：%s",
                 (errUtf8 && errUtf8[0]) ? errUtf8
                                         : "未知原因");
    } else if (op == (int)HSF_CMD_VALVE_CONTROL) {
        const char* r = HsfLinkClient_lastValveCmdResult();
        const char* text = "命令已受理"; /* 命令已受理 */
        if (r) {
            if (strcmp(r, "issued") == 0)
                text = "命令已下发"; /* 命令已下发 */
            else if (strcmp(r, "repeated") == 0)
                text = "动作中，重复命令不重置计时器"; /* 动作中，重复命令不重置计时器 */
            else if (strcmp(r, "reversed") == 0)
                text = "已反向（重新计时）"; /* 已反向（重新计时） */
            else if (strcmp(r, "ignored") == 0)
                text = "已在目标位置，无需动作"; /* 已在目标位置，无需动作 */
        }
        snprintf(buf, sizeof(buf), "%s", text);
    } else {
        snprintf(buf, sizeof(buf), "%s",
                 "命令已受理"); /* 命令已受理 */
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

    /* 闸门由 hsfWinApplyControlGates 统一驱动（本函数只管灌数据）。 */
    hsfWinApplyControlGates((HsfWin*)userData);
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
        int32_t eng = 0;
        char engUnit[12];
        const char* engNote = NULL;
        if (name) HsfMonitorAnalog_setName(i, name);
        HsfMonitorAnalog_setChannel(i, i);
        HsfMonitorAnalog_setRaw(i, ua[i]);
        HsfMonitorAnalog_setScaled(i, (double)ua[i] / 1000.0);
        HsfMonitorAnalog_setKind(i, HSF_ANALOG_INPUT);
        /* 原始寄存器（毫伏）：卡片"原始"行与 Python 端脚注同口径。 */
        HsfMonitorAnalog_setRawMv(i, (int)HsfLinkClient_aiRawMv(i));
        /* 标定工程量（服务端算）：通道 0 压差计 0~1MPa。 */
        if (HsfLinkClient_aiEngineering(i, &eng, engUnit, (int)sizeof(engUnit),
                                       &engNote)) {
            HsfMonitorAnalog_setEngineering(i, (double)eng / 1000.0, engUnit,
                                            engNote);
        } else {
            HsfMonitorAnalog_setEngineering(i, 0.0, NULL, NULL);
        }
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
    int64_t nowMs;
    if (!win || !win->m_uiReady) return true;
    /* [帧泵节流] dispatcher 空转时本回调最高 50Hz；布局/导航重排 ~30Hz 足够。 */
    nowMs = XDateTime_currentNSecsSinceEpoch() / 1000000LL;
    if (win->m_pumpLastMs > 0 && nowMs - win->m_pumpLastMs < 33)
        return true;
    win->m_pumpLastMs = nowMs;
    /* [字号实时自测] XHSF_FONT_SELFTEST="page:pct"：启动 2.5s 后写键并
     * 走一次实时重放（CI/验收无需人工点数字框）。无该变量不生效。 */
    {
        static int64_t s_fontTestStart = 0;
        static int s_fontTestDone = 0;
        const char* spec = XSystem_environment("XHSF_FONT_SELFTEST");
        if (spec && spec[0] && !s_fontTestDone) {
            if (s_fontTestStart == 0) s_fontTestStart = nowMs;
            if (nowMs - s_fontTestStart >= 2500) {
                int page = atoi(spec);
                const char* colon = strchr(spec, ':');
                int pct = colon ? atoi(colon + 1) : 120;
                char key[32];
                snprintf(key, sizeof(key), "FontScale/Page%d", page);
                HsfSettingsStore_setInt(key, pct);
                HsfSettingsStore_sync();
                hsfUi_reapplyPageFonts(page);
                XPrintf("HsfClient: 字体自测 page=%d pct=%d\n", page, pct);
                s_fontTestDone = 1;
            }
        }
    }
    /* [adapt 与导航重排共用一个签名闸] adapt 只解决"页宽变了但分档还没跟上"：
     * 页宽只在**窗口尺寸/当前页/形态档**变化时才变，故按同一签名节流。
     * 原先无条件每 33ms 调一次——各页 adapt 里必然重设布局方向/拉伸/内边距/
     * 字体，只要其中任一处没做同值早退，就会变成 30Hz 空转重绘。
     * 导航收起/展开走 hsf_layoutModeUpdate → hsf_adaptPages 的独立路径，
     * 不依赖本闸。 */
    /* [adapt 必须每拍跑] 页宽在**窗口尺寸不变**时也会变（切页后堆栈首次
     * 布局、导航收起、分割条拖动），而且首次 pump 时页宽可能还是 0 ——
     * 曾把它收进签名闸，结果 adapt 拿着 0 宽度选了「窄档竖向布局」并
     * 永久停在那里（实测变频器页给定行竖着堆、控件被拉成 100x200）。
     * 各页 adapt 内部的布局调用都已是同值早退（setDirection/setStretch/
     * setContentsMargins/setFont 均有闸），所以每拍跑的实际代价只是一串
     * 比较，不产生脏区。 */
    hsf_adaptPages(win);
    {
        const int page = HsfStack_current(&win->m_stack);
        if (win->m_navSigW != XWidget_width(&win->m_base) ||
            win->m_navSigH != XWidget_height(&win->m_base) ||
            win->m_navSigPage != page ||
            win->m_navSigMode != win->m_layoutMode) {
            win->m_navSigW = XWidget_width(&win->m_base);
            win->m_navSigH = XWidget_height(&win->m_base);
            win->m_navSigPage = page;
            win->m_navSigMode = win->m_layoutMode;
            HsfNavPanel_update(&win->m_navPanel);
            HsfNavPanel_update(&win->m_mobilePanel);
            /* [悬浮层抬升随页面/几何变化一起做] 原先每 33ms 无条件调用，
             * 每次都可能搬动子对象并 XWidget_updateRect 标脏 ⇒ 30Hz 重绘
             * 来源（实测各页含无图表的主界面都有 47~93 帧/窗口）。抬升只在
             * 「可能被别人盖住」的时机需要：切页、改尺寸、形态切换。 */
            XWidget_raise((XWidget*)&win->m_overlay);
        }
    }
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
    /* [此处不再每帧抬升悬浮层] 见上方签名块的说明：抬升会搬动子对象并
     * XWidget_updateRect 标脏，每 33ms 一次就是 30Hz 空转重绘源。 */
    /* autoFit 收框只改尺寸不动位置 → 每帧按「尺寸与上次锚定值不同」重锚
       贴边（demo 帧泵同口径；fixed 态才跟边，非 fixed 是用户自由拖动位）。 */
    if (XPerformanceOverlay_isFixed(&win->m_overlay)) {
        XRect ovlGeo = XPerformanceOverlay_geometry(&win->m_overlay);
        if (ovlGeo.width != win->m_overlayAnchorW ||
            ovlGeo.height != win->m_overlayAnchorH)
            hsf_overlayAnchor(win);
    }
    /* 喂悬浮层：FPS/帧耗时由 paintEvent 喂；网络计数按**悬浮层自身统计
       周期**采样。原来固定 250ms，与 XGUI_PERFORMANCE_OVERLAY_UPDATE_MS
       的 250ms 恰好同频但各自计数；周期改 1000ms 后这里若仍 250ms，就会
       每 250ms 调一次 updateNetwork → performanceOverlay_updateText →
       文本（速率数字）变化 → 标脏 → 又画一帧，把「1s 采集」的省帧效果
       完全抵消掉。两处必须同源。 */
    nowUsecs = XDateTime_currentNSecsSinceEpoch() / 1000LL;
    frameUsecs = nowUsecs - win->m_lastFrameUsecs;
    if (win->m_lastFrameUsecs > 0 && frameUsecs > 0) {
        /* 帧率/帧耗时由 paintEvent 喂（口径=绘制活动率）；此处只喂网络。 */
        if (nowUsecs - win->m_lastNetworkPollUsecs >=
            (int64_t)XGUI_PERFORMANCE_OVERLAY_UPDATE_MS * 1000LL) {
            uint64_t rx = 0;
            uint64_t tx = 0;
            bool ok = XDeviceNetwork_getNetworkCounters(&rx, &tx);
            /* 悬浮层不可见时不采样/不刷文本（免得因此触发重绘）。 */
            if (XWidget_isVisible((XWidget*)&win->m_overlay))
                XPerformanceOverlay_updateNetwork(&win->m_overlay, ok, rx, tx,
                                                  nowUsecs);
            win->m_lastNetworkPollUsecs = nowUsecs;
        }
    }
    win->m_lastFrameUsecs = nowUsecs;
    /* [诊断] XHSF_HEAP_TRACE=1：每 5 秒打一遍系统堆存量（重连/压测判泄漏）。
     * 口径与服务器端 HSF_HEAP_TRACE 一致（AGENTS：看堆计数不看工作集）；
     * 附链路就绪位，便于区分「连接态堆涨」与「断开后堆不回落」。 */
    {
        static int heapTraceOn = -1;
        static int64_t heapLastMs = 0;
        if (heapTraceOn < 0)
            heapTraceOn = XSystem_environment("XHSF_HEAP_TRACE") ? 1 : 0;
        if (heapTraceOn) {
            int64_t heapNowMs = XDateTime_currentMSecsSinceEpoch();
            if (heapLastMs == 0 || heapNowMs - heapLastMs >= 5000) {
                heapLastMs = heapNowMs;
                XPrintf("[heap] sys=%lld link=%d t=%lld\n",
                        (long long)XMemory_statistics_2(
                            XMEMORY_TYPE_SYSTEM).systemBytes,
                        HsfLinkClient_isReady() ? 1 : 0,
                        (long long)heapNowMs);
            }
        }
    }
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
/**
 * @brief 停掉主窗挂的全部定时器（退出收口用；幂等）。
 * @param win 归属主窗。
 * @details 必须在 `XClassDelete(win)` **之前**调用：主窗析构是深层级联，
 *          期间若有本窗定时器回调被派发，会解引用已释放的页/控件（实测
 *          退出恒 AV）。kill 后把 id 复位为 XTIMER_INVALID_ID，避免重复
 *          kill 已复用的 id 误杀别人的定时器。
 */
static void hsfWinStopAllTimers(HsfWin* win)
{
    if (!win) return;
#define HSF_KILL_TIMER(field)                                                  \
    do {                                                                       \
        if (win->field != XTIMER_INVALID_ID) {                                 \
            XObject_killTimer((XObject*)win, win->field);                      \
            win->field = XTIMER_INVALID_ID;                                    \
        }                                                                      \
    } while (0)
    HSF_KILL_TIMER(m_clockTimer);
    HSF_KILL_TIMER(m_autoQuitTimer);
    HSF_KILL_TIMER(m_shotTimer);
    HSF_KILL_TIMER(m_cycleTimer);
    HSF_KILL_TIMER(m_valveSelfTestTimer);
#undef HSF_KILL_TIMER
}

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

    /* [数据源接远端] 无头登录（--login）路径是「先登录、后建主窗」，而
     * HsfMonitorVfd_rebindProvider 只在"主窗先显"那条路径里调用过 ⇒ 无头
     * 模式下变频器页仍用**本地 demo 后端**（横幅"无数据"、控制闸锁死，
     * 与真实服务端不接）。故建窗后若已登录，立即切远端后端。 */
    if (HsfLinkClient_isOpen()) {
        HsfMonitorVfd_rebindProvider();
        XPrintf("HsfClient: 变频器页数据源已切远端（已登录）\n");
    }
    /* [--no-login 离线演示：模拟量页/数字量页灌演示点] 离线态没有服务端快照，
     * 数字量/模拟量模型恒空 ⇒ 页面只有「无…点」空态，指定截图无法验收
     * 卡片字号/角标/重叠。只在显式 --no-login 路径灌（登录路径不灌——
     * 现场真实点位绝不能被假数据顶替；登录后服务端快照 setCount 会整体
     * 覆盖本批演示点）。[数字量 2026-11-10] 与模拟量页同口径收敛：数字量
     * 演示点里一半是可点的 DO，点下去会按 `index - n` 算出通道号发给
     * 服务端 ⇒ 可能误控真实通道，故登录路径一律不灌。 */
    if (opts && opts->noLogin) {
        HsfMonitorAnalog_seedDemoForOffline();
        HsfMonitorDigital_seedDemoForOffline();
    }

    /* 顶栏账号（设备健康左侧）：
     *   · --login（无头）：回调里先登录成功、再由本函数建窗 ⇒ 这里已就绪，
     *     HsfLinkClient_user() 非空；
     *   · --no-login / 主窗先显的交互登录：此刻尚未认证 ⇒ 空串 ⇒ 如实显示
     *     「未登录」；交互登录成功后由 hsfUiLoginDone 再补一次。
     * 入参都是借用指针，HsfTitleBar_setUser 内部拷贝。 */
    HsfTitleBar_setUser(&win->m_titleBar, HsfLinkClient_user(),
                        HsfLinkClient_roleName());

    /* 标题栏时钟 1s 刷新（demo 品牌条同款）。 */
    win->m_clockTimer = XObject_startTimer_ms(
        (XObject*)win, 1000u, XTimerType_CoarseTimer);
    if (win->m_clockTimer == XTIMER_INVALID_ID)
        XPrintf("HsfClient: 时钟定时器创建失败\n");

    /* 阀自测钩子（env XHSF_VALVE_SELFTEST=open:<路号>）：延时 1.2s 开阀
     * 一次（等价于点一次"开阀"按钮，走同一条控制链路）。 */
    {
        const char* vst = XSystem_environment("XHSF_VALVE_SELFTEST");
        if (vst && vst[0]) {
            win->m_valveSelfTestTimer = XObject_startTimer_ms(
                (XObject*)win, 1200u, XTimerType_CoarseTimer);
            XPrintf("HsfClient: 阀自测启用（%s）\n", vst);
        }
    }

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
    s_activeWin = win;              /* 本地设置页翻转形态档后即时生效用。 */
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
        /* 顶栏账号：主窗先显路径建窗时还没登录，这里补刷成「账号·角色」。 */
        HsfTitleBar_setUser(&ctx->m_win->m_titleBar, HsfLinkClient_user(),
                            HsfLinkClient_roleName());
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
    /* [全局重绘节流已移除 2026-11-08] 此处原有「XHSF_REPAINT_MAX_FPS 缺省
     * 2fps → XWidget_setMaxRepaintFps」的进程级节流设定。它把**所有**重绘
     * 来源合并到 2Hz，代价是任何交互（点击/切页/下拉框弹层）最坏要等
     * 500ms 才上屏，且弹层是独立顶层窗（XWindowType_Popup）同样被拦，
     * 首帧被反复推后 ⇒ 「预设位置」下拉只见灰框不见选项。Qt 没有全局帧率闸
     * （update() 只合并脏区、有脏区就画），框架侧已按 Qt 语义撤除该闸，
     * 本处设定随之删除。 */
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
            /* [旗舰深色 2026-11-11] 同主窗路径：规格表夹回 0 后补覆盖。 */
            hsfUiApplyFlagshipThemeOverride();
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
    /* [退出期定时器先停 2026-11-08] 主窗挂的时钟/自测/自动退出定时器此前
     * 从不显式 kill，靠 VXObject_deinit 的 unregisterTimers 兜底。但析构是
     * 深层级联（splitter → scrollArea → 各页 widget），期间定时器表可能
     * 已被部分清理、回调仍能排进事件队列并解引用**已释放页对象**——
     * 实测退出恒 AV（`VXObject_deinit` 内 mov eax,[rax+50h]，rax 为
     * 0x000000060000000C 这类脏值）。删窗之前先把本窗定时器全停掉，
     * 消除"析构过程中还有本窗回调在跑"这一整类时序。 */
    if (ctx.m_win)
        hsfWinStopAllTimers(ctx.m_win);
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
