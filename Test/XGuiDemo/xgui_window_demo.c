/******************************************************************************
 * @file       xgui_window_demo.c
 * @brief      XGui GUI 控件统一可视化测试程序（Linux X11 / Windows Win32）。
 * @details    本程序是 GUI 控件的人工可视化验收入口，演示 XGui 完整窗口链路：
 *             - XGuiApplication_create_ex 初始化应用单例；
 *             - 顶层 XWidget 的 showNormal() 惰性创建内部 XWidgetWindow，
 *               再由该桥接窗口创建平台窗口（X11 Window / Win32 HWND）；
 *             - XWidget 首次绘制时创建离屏缓冲，并由 XPainter 软件光栅化；
 *             - XWindowSystemInterface 事件注入（Expose/Resize/Close）；
 *             - XGuiApplication_exec 标准事件循环；常驻刷新注册在事件
 *               分发器的轮询链上，不受 1ms 定时器粒度限制；
 *             - WM 删除（标题栏 X）触发 CloseEvent -> 接受后关闭退出。
 *             窗口绘制完全走 XImage/XPainter 软件路径，不依赖任何平台
 *             图形 API；平台差异全部隔离在 Drive 后端。
 *             新增控件时在本文件追加可见场景；自动断言仍统一放在
 *             xgui_regression_test.c，避免菜单式测试程序重复。
 *             运行：./bin/XGuiWindowDemo_Test [自动退出秒数]
 *                   - 不带参数：常驻，等待窗口标题栏关闭；
 *                   - 带参数：运行指定秒数后自动退出（供无窗口管理器的
 *                     Xvfb/CI 环境截图验收）。
 * @author     XinYueC 团队
 ******************************************************************************/
#include <stdio.h>
#include <stdarg.h> /* demo_log：va_list 转发 vprintf（诊断行立即落盘）。 */
#include <malloc.h> /* mallopt(内存驻留治理) */
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include "CXinYueConfig.h"
#include "XPrintf.h"
#include "XObject.h"
#include "XEvent.h"
#include "XAbstractEventDispatcher.h"
#include "XDateTime.h"
#include "XSystem.h" /* XSystem_environment：环境变量唯一入口。 */
#include "XMemory.h" /* [perf8r3 临时内存探针] XMemory_statistics_2。 */
#include "XCoreApplication.h" /* demo 缺省字库经 exe 目录解析外挂轮廓字库。 */
#include "XGuiApplication.h"
#if XWIDGET_ON && XKEYBOARD_ON
#include "XVirtualKeyboard.h" /* 键盘页单例面板 win 析构前摘挂（自包含声明，防 XVIRTUALKEYBOARD_ON=0 态缺声明）。 */
#endif
#include "XWidget.h"
#include "XWidget_Protected.h"
#include "XImage.h"
#include "XWindow.h"
#include "XWindowEvent.h"
#include "xgui_demo_pages.h"
#include "xgui_demo_splitter.h" /* 通用分割条：导航面板/RC 控制列拖宽收展。 */
#include "xgui_demo_theme.h" /* fusion-css 缺省主题样式表（xgui_demo_theme_css）。 */
#include "xgui_demo_page_remote_server.h" /* 远程窗口设置页 CLI 预置/autostart/shutdown。 */
#include "xgui_demo_page_remote_client.h" /* 远程客户端页 CLI 预置/autostart/shutdown(2026-10-02)。 */
#include "xgui_demo_apitest.h"
#include "XWindowDecoration.h" /* 框架级系统标题栏：布局让位边距查询 */
#if XPLATFORMINTEGRATION_ON && XGPU_ON
#include "XGpuRenderBackend.h"
#endif
#if XPLATFORMINTEGRATION_ON && XPLATFORMNATIVEWINDOW_ON
#include "XPlatformNativeWindow.h"
#endif
#if defined(__linux__) && XGUI_ON && XPLATFORM_FBDEV_ON
/* 嵌入式 fbdev 板级引导头：显示驱动注册入口 + 触摸输入驱动注册入口
 * （两文件经门控宏自行裁剪，XPLATFORM_FBINPUT_ON=0 时后者为空头）。 */
#include "XPlatformFramebuffer_posix.h"
#if XPLATFORM_FBINPUT_ON
#include "XPlatformFbInput_posix.h"
#endif
#include "XPlatformBackingStore.h" /* requestPanelClear：合成 □ 最大化切换清屏。 */
#include "XPlatformDisplayDriver.h" /* 面板尺寸探测（□ 最大化目标几何）。 */
#endif
#include "XPixmap.h"
#include "XImage.h"
#include "XPainter.h"
#include "XLabel.h"
#include "XDeviceNetwork.h"
#if XGUI_PERFORMANCE_OVERLAY_ON && XWIDGET_ON && XFRAME_ON && XLABEL_ON
#include "XPerformanceOverlay.h"
#endif
#if XWIDGET_ON && XPUSHBUTTON_ON
#include "XPushButton.h"
#endif
#if XWIDGET_ON && XABSTRACTBUTTON_ON && XCHECKBOX_ON
#include "XCheckBox.h"
#endif
#if XWIDGET_ON && XABSTRACTBUTTON_ON && XRADIOBUTTON_ON
#include "XRadioButton.h"
#endif
#if XWIDGET_ON && XABSTRACTBUTTON_ON && XPUSHBUTTON_ON && XCOMMANDLINKBUTTON_ON
#include "XCommandLinkButton.h"
#endif
#if XWIDGET_ON && XABSTRACTBUTTON_ON && XTOOLBUTTON_ON && XMENU_ON
#include "XToolButton.h"
#endif
#if XWIDGET_ON && XGROUPBOX_ON
#include "XGroupBox.h"
#endif
#if XWIDGET_ON && XLINEEDIT_ON
#include "XLineEdit.h"
#endif
#if XWIDGET_ON && XSPINBOX_ON && XABSTRACTSPINBOX_ON && XLINEEDIT_ON
#include "XSpinBox.h"
#endif
#if XWIDGET_ON && XSLIDER_ON && XABSTRACTSLIDER_ON
#include "XSlider.h"
#endif
#if XWIDGET_ON && XPROGRESSBAR_ON
#include "XProgressBar.h"
#endif
#if XWIDGET_ON && XTABWIDGET_ON && XTABBAR_ON
#include "XTabWidget.h"
#if XLCDNUMBER_ON
#include "XLcdNumber.h"
#endif
#if XSCROLLBAR_ON
#include "XScrollBar.h"
#endif
#if XSCROLLAREA_ON && XABSTRACTSCROLLAREA_ON
#include "XScrollArea.h"
#endif
#if XSPLITTER_ON
#include "XSplitter.h"
#endif
#if XTOOLBOX_ON
#include "XToolBox.h"
#endif
#if XDIALOGBUTTONBOX_ON
#include "XDialogButtonBox.h"
#endif
#if XMENUBAR_ON
#include "XMenuBar.h"
#endif
#if XTOOLBAR_ON
#include "XToolBar.h"
#endif
#if XPLAINTEXTEDIT_ON
#include "XPlainTextEdit.h"
#endif
#if XDATETIMEEDIT_ON
#include "XDateTimeEdit.h"
#endif
#if XDATEEDIT_ON
#include "XDateEdit.h"
#endif
#if XTIMEEDIT_ON
#include "XTimeEdit.h"
#endif
#if XFONTCOMBOBOX_ON
#include "XFontComboBox.h"
#endif
#if XCALENDARWIDGET_ON
#include "XCalendarWidget.h"
#endif
#if XTEXTBROWSER_ON
#include "XTextBrowser.h"
#include "XTableWidget.h"
#include "XChartView.h"
#include "XValueAxis.h"
#include "XLineSeries.h"
#include "XBarSeries.h"
#include "XBarSet.h"
#include "XScatterSeries.h"
#include "XAreaSeries.h"
#include "XSplineSeries.h"
#include "XPieSeries.h"
#endif
#if XMDIAREA_ON
#include "XMdiArea.h"
#endif
#if XSTATUSBAR_ON
#include "XStatusBar.h"
#endif
#if XSTACKEDWIDGET_ON && XLAYOUT_STACKED_ON
#include "XStackedWidget.h"
#endif
#if XBUTTONGROUP_ON
#include "XButtonGroup.h"
#endif
#if XCHECKBOX_ON
#include "XCheckBox.h"
#endif
#endif
#if XWIDGET_ON && XCOMBOBOX_ON
#include "XComboBox.h"
#endif
#if XWIDGET_ON && XABSTRACTSLIDER_ON && XDIAL_ON
#include "XDial.h"
#endif
#include "XVarList.h"
#if XWIDGET_ON && XFRAME_ON && XLABEL_ON && XLAYOUT_ON && XLAYOUT_STACKED_ON
#include "XStackedLayout.h"
#endif

#if XLCDNUMBER_ON
#include "XLcdNumber.h"
#endif
#if XSCROLLBAR_ON
#include "XScrollBar.h"
#endif
#if XSCROLLAREA_ON && XABSTRACTSCROLLAREA_ON
#include "XScrollArea.h"
#endif
#if XSPLITTER_ON
#include "XSplitter.h"
#endif
#if XTOOLBOX_ON
#include "XToolBox.h"
#endif
#if XDIALOGBUTTONBOX_ON
#include "XDialogButtonBox.h"
#endif
#if XMENUBAR_ON
#include "XMenuBar.h"
#endif
#if XTOOLBAR_ON
#include "XToolBar.h"
#endif
#if XSTACKEDWIDGET_ON && XLAYOUT_STACKED_ON
#include "XStackedWidget.h"
#endif
#if XBUTTONGROUP_ON
#include "XButtonGroup.h"
#endif
#if XSTATUSBAR_ON
#include "XStatusBar.h"
#if XWIZARD_ON
#include "XWizard.h"
#endif
#if XERRORMESSAGE_ON
#include "XErrorMessage.h"
#endif
#endif
#if XDATETIMEEDIT_ON
#include "XDateTimeEdit.h"
#endif
#if XDATEEDIT_ON
#include "XDateEdit.h"
#endif
#if XTIMEEDIT_ON
#include "XTimeEdit.h"
#endif
#if XFONTCOMBOBOX_ON
#include "XFontComboBox.h"
#endif
#if XPLAINTEXTEDIT_ON
#include "XPlainTextEdit.h"
#endif
#if XMDIAREA_ON
#include "XMdiArea.h"
#endif
#if XCALENDARWIDGET_ON
#include "XCalendarWidget.h"
#endif
#if XTEXTBROWSER_ON
#include "XTextBrowser.h"
#include "XTableWidget.h"
#include "XChartView.h"
#include "XValueAxis.h"
#include "XLineSeries.h"
#include "XBarSeries.h"
#include "XBarSet.h"
#include "XScatterSeries.h"
#include "XAreaSeries.h"
#include "XSplineSeries.h"
#include "XPieSeries.h"
#endif

#if XGUIAPPLICATION_ON && XWIDGET_ON && XWINDOW_ON && XBACKINGSTORE_ON && \
    XPLATFORMINTEGRATION_ON && XPLATFORMNATIVEWINDOW_ON

/* The demo follows the scalable built-in face whenever it is compiled in.
   A clipped embedded build keeps the existing bitmap fallback. */
#if XFONT_BUILTIN_OUTLINE_ON
#define XGUI_DEMO_DEFAULT_FONT_FAMILY "XFontOutlineCommon"
#else
#define XGUI_DEMO_DEFAULT_FONT_FAMILY XFONT_DEFAULT_FAMILY
#endif

/* Desktop demo 默认缓存静态控件场景；资源受限目标可显式设为 0。
 * 【昆仑通态 A33 fbdev 定版 = 0（09-28）】：静态场景缓存依赖
 * 「影子缓冲持久且增量正确」的前提；fbdev 双缓冲 pan + 差带同步下
 * 缓存命中会把过期瓦片搬上屏，交互后大块白屏/内容缺失（真机实测）。
 * 关闭后每次 PAINT 现绘整页，内容恒正确；成本由 HUD 1Hz 限频兜底。 */
#ifndef XGUI_DEMO_STATIC_SCENE_CACHE_ON
#define XGUI_DEMO_STATIC_SCENE_CACHE_ON 0
#endif

/* 交互空闲治理（2026-09-25）：空闲闸门开启时帧泵不再逐轮强制重绘
   （对标 Qt「无脏区不重绘」），性能悬浮层指标自刷新降频为独立定时器
   驱动。降频周期取 250ms（4Hz，落在 2~4Hz 治理区间），与
   XGUI_PERFORMANCE_OVERLAY_UPDATE_MS 的文本统计窗口（250ms）对齐，
   指标更新节奏与重绘节奏一致；XGUI_DEMO_IDLE_GATE=0 一并回退两种
   行为（回退旧「帧泵每轮 processEvents 全速重绘」口径）。 */
#ifndef XGUI_DEMO_IDLE_OVERLAY_MS
/* 1000ms（A33 真机 09-28）：悬浮层每次自刷新都会走一遍「全页 paintTree
   + present（行带 cacheflush + 阻塞式 pan 等回扫）」链，实测单次 ~55ms、
   占 CPU ~7%；250ms 时仅此一项即占 ~38%。状态读数 1Hz 完全够用。 */
#define XGUI_DEMO_IDLE_OVERLAY_MS 1000
#endif

/* ==================== 状态栏标签子类 ==================== */

#if XWIDGET_ON && XFRAME_ON && XLABEL_ON

XCLASS_DEFINE_BEGING(DemoStatusLabel)
XCLASS_DEFINE_EXTEND_END(DemoStatusLabel, XLabel)

/** @brief 状态栏标签：先铺不透明深色底再绘制状态文本。
 * @details 窗口缩小后固定高度的内容控件会向下越界伸进状态栏区域，
 *          状态栏底色若只靠根背景（静态场景）着色，会被后画的内容
 *          控件盖住；改为自带底色的子控件并在创建完毕后 raise 到
 *          内容控件之后，状态栏就永远可见。 */
typedef struct DemoStatusLabel
{
    XLabel m_base; /**< XLabel 基类；必须是第一个成员。 */
} DemoStatusLabel;

/** @brief 绘制事件：树内按自身几何先填底色，再走标签文本绘制。 */
static void VDemoStatusLabel_paintEvent(XWidget* self, XEvent* event)
{
    XImage* image;
    XPoint offset;
    XPainter painter;
    XRect rect;
    if (!self || !event || XEvent_type(event) != XEVENT_TYPE_PAINT) return;
    image = XWidget_paintImage(self);
    if (!image) return;
    XPainter_init(&painter, NULL);
    if (!XPainter_begin_image(&painter, image)) {
        XPainter_deinit(&painter);
        return;
    }
    offset = XWidget_paintOffset(self);
    if (offset.x != 0 || offset.y != 0)
        XPainter_translate(&painter, (float)offset.x, (float)offset.y);
    XRect_init(&rect, 0, 0, XWidget_width(self), XWidget_height(self));
    XPainter_fillRect(&painter, &rect, 0xff3a3a3au);
    XLabel_drawContents((XLabel*)self, &painter);
    XPainter_end(&painter);
    XPainter_deinit(&painter);
}

/** @brief 初始化 DemoStatusLabel 类虚函数表。 */
XVtable* DemoStatusLabel_class_init(void)
{
    XVTABLE_INIT_DEFAULT(DemoStatusLabel)
    XVTABLE_INHERIT_XCLASS(XLabel);
    XVTABLE_OVERLOAD_DEFAULT(EXWidget_PaintEvent,
                             VDemoStatusLabel_paintEvent);
    return XVTABLE_DEFAULT;
}

/** @brief 初始化状态栏标签（几何/文本沿用 XLabel 接口）。 */
void DemoStatusLabel_init(DemoStatusLabel* self, XWidget* parent,
                          XWidgetFlags flags)
{
    if (!self) return;
    XLabel_init(&self->m_base, parent, flags);
    XClassSetVtable(self, DemoStatusLabel);
}

#endif /* XWIDGET_ON && XFRAME_ON && XLABEL_ON */

/* ==================== 演示窗口子类 ==================== */

XCLASS_DEFINE_BEGING(DemoWin)
XCLASS_DEFINE_EXTEND_END(DemoWin, XWidget)

/** @brief 演示顶层控件：XWidget 负责窗口桥接、后备存储和控件树。 */
typedef struct DemoWin
{
    XWidget         m_base;  /**< 顶层控件基类；必须是第一个成员。 */
    XImage          m_staticScene; /**< 不含性能悬浮层的静态场景缓存。 */
    bool            m_staticSceneDirty; /**< 静态场景需重新生成。 */
    XHandle         m_framePump; /**< 事件循环轮询回调句柄（刷新不受定时器限制）。 */
    XTimerId        m_autoQuitTimer; /**< 自动退出定时器。 */
    XTimerId        m_lcdTimer;     /**< LCD 数码管自动更新定时器。 */
    XTimerId        m_overlayTimer; /**< 空闲闸门下悬浮层降频自刷新定时器（XGUI_DEMO_IDLE_OVERLAY_MS，4Hz）。 */
    int             m_lcdValue;     /**< LCD 字符序列索引（循环段码表 30 字符）。 */
    bool            m_closed; /**< CloseEvent 被接受或自动退出后置真。 */
    const char*     m_screenshotPath; /**< 非空时渲染数帧后保存一帧截图并退出（借用指针）。 */
    int             m_screenshotFrames; /**< 截图模式已渲染帧数。 */
    bool            m_autoTest;         /**< 自动交互测试模式（第 4 页注入事件断言联动）。 */
    int             m_autoTestFrames;   /**< 自动测试已渲染帧数。 */
#if XGUI_PERFORMANCE_OVERLAY_ON && XWIDGET_ON && XFRAME_ON && XLABEL_ON
    XPerformanceOverlay m_performanceOverlay; /**< 性能悬浮层控件。 */
    int             m_overlayAnchorW; /**< 上次锚定时的悬浮层宽度（尺寸自适应变化后触发重新锚定贴右边）。 */
    int             m_overlayAnchorH; /**< 上次锚定时的悬浮层高度（尺寸自适应变化后触发重新锚定贴状态栏）。 */
    bool            m_overlayPinned; /**< 用户显式预设位置（悬浮窗设置页）：挂起 resize 自动重锚右下角，预设回右下即恢复。 */
#if XGUI_PERFORMANCE_OVERLAY_NETWORK_ON
    int64_t m_lastNetworkPollUsecs; /**< 最近一次主机网络计数采样时刻。 */
#endif
#endif
#if XWIDGET_ON && XFRAME_ON && XLABEL_ON
    XLabel          m_titleLabel; /**< 顶部标题栏文本（深蓝背景，白字）。 */
    DemoStatusLabel m_statusLabel; /**< 底部状态栏（自带深色底，白字）。 */
#endif
#if XWIDGET_ON && XPUSHBUTTON_ON
    XPushButton     m_pageNav[15]; /**< 页面切换按钮（下标=页索引；挂在浮动导航面板活动分类下，装配见 DemoWin_create）。 */
#endif
#if XBUTTONGROUP_ON
    XButtonGroup    m_navGroup;    /**< 页面钮互斥组：当前页按钮保持选中高亮（主题 :checked 态）。 */
    XButtonGroup    m_navCatGroup; /**< 分类钮互斥组：面板手风琴当前展开分类。 */
#endif
#if XWIDGET_ON && XABSTRACTBUTTON_ON && XPUSHBUTTON_ON && \
    XFRAME_ON && XLABEL_ON
    XWidget         m_navPanel;      /**< 浮动导航面板（四边停靠/收起贴边，见 demo_navUpdatePanel）。 */
    XLabel          m_navTitle;      /**< 面板标题「导航」。 */
    XPushButton     m_navDockBtn;    /**< 换边钮：左→右→上→下循环。 */
    XPushButton     m_navCollapseBtn;/**< 收起/展开钮：收起后贴边成细条。 */
    XPushButton     m_navCatBtns[3]; /**< 一级分类钮（控件/系统设置/远程；手风琴）。 */
    XLabel          m_navHeadLabels[5]; /**< 子分组标题标签池（控件类内「按钮/输入/…」，竖版展开时启用）。 */
    int             m_navDock;       /**< 停靠边 0=左 1=右 2=上 3=下（默认左）。 */
    bool            m_navCollapsed;  /**< 收起贴边态（细条=分割条本身）。 */
    int             m_navCategory;   /**< 当前展开分类（kNavGroups 下标）。 */
    int             m_navWidth;      /**< 面板宽（左右停靠；分割条可拖 140..380）。 */
    int             m_navTBH;        /**< 面板高（上下停靠；分割条可拖 92..320）。 */
    XWidget*        m_navSplit;      /**< 分割条：拖动调尺寸/双击收起/收起单击展开。 */
#endif
#if XWIDGET_ON && XLAYOUT_ON && XLAYOUT_STACKED_ON
    XStackedLayout  m_stackLayout; /**< 主内容堆叠布局（15 个演示页面）。 */
    XWidget         m_pageButtons; /**< 页面 0：按钮演示容器。 */
    XWidget         m_pageChoices; /**< 页面 1：选择演示容器。 */
    XWidget         m_pageStacked; /**< 页面 2：堆叠演示容器。 */
    XWidget         m_pageInputs;  /**< 页面 3：输入控件演示容器。 */
    XWidget         m_pageTabs;    /**< 页面 4：容器与窗口演示容器。 */
    XWidget         m_pageChart;   /**< 页面 12：图表演示容器（自选项卡页打散独立）。 */
    XWidget*        m_extPages[10]; /**< 页面 5~14：扩展页根（xgui_demo_pages.h 契约，5+下标=页索引，下标 7=内置图表页占位恒 NULL；堆对象随父链级联析构）。 */
#endif
#if XWIDGET_ON && XGROUPBOX_ON && XLINEEDIT_ON && XSPINBOX_ON && \
    XABSTRACTSLIDER_ON && XSLIDER_ON && XPROGRESSBAR_ON
    XGroupBox       m_groupBox;     /**< 页面 3：分组框（标题含子控件）。 */
    XLineEdit       m_lineEdit;     /**< 页面 3：单行输入。 */
    XSpinBox        m_spinBox;      /**< 页面 3：数值微调框。 */
    XSlider         m_slider;       /**< 页面 3：滑块。 */
    XProgressBar    m_progressBar;  /**< 页面 3：进度条（随滑块联动）。 */
    XLabel          m_inputStatus;  /**< 页面 3：输入联动状态行。 */
#endif
#if XWIDGET_ON && XTABWIDGET_ON && XTABBAR_ON && XCOMBOBOX_ON && \
    XABSTRACTSLIDER_ON && XDIAL_ON && XPROGRESSBAR_ON && XFRAME_ON && XLABEL_ON
    XTabWidget      m_tabWidget;    /**< 页面 4：选项卡容器。 */
    XComboBox       m_comboBox;     /**< 页面 4：下拉框（页一内容）。 */
    XDial           m_dial;         /**< 页面 4：旋钮（页二内容）。 */
    XProgressBar    m_dialProgress; /**< 页面 4：旋钮联动进度条。 */
    XLabel          m_tabStatus;    /**< 页面 4：联动状态行。 */
#if XLCDNUMBER_ON
    XLcdNumber      m_lcd;          /**< LCD 数码管。 */
#endif
#if XSCROLLBAR_ON
    XScrollBar      m_scrollBar;    /**< 滚动条。 */
#endif
#if XSCROLLAREA_ON && XABSTRACTSCROLLAREA_ON
    XScrollArea     m_scrollArea;   /**< 滚动区域。 */
#endif
#if XSPLITTER_ON
    XSplitter       m_splitter;     /**< 分割器。 */
#endif
#if XTOOLBOX_ON
    XToolBox        m_toolBox;      /**< 工具箱。 */
#endif
#if XDIALOGBUTTONBOX_ON
    XDialogButtonBox m_buttonBox;   /**< 标准按钮盒。 */
#endif
#if XMENUBAR_ON && XMENU_ON
    XMenuBar        m_menuBar;      /**< 菜单栏。 */
    XMenu*          m_fileMenu;     /**< 文件菜单。 */
    XMenu*          m_editMenu;     /**< 编辑菜单。 */
#endif
#if XTOOLBAR_ON
    XToolBar        m_toolBar;      /**< 工具栏。 */
#endif
#if XPLAINTEXTEDIT_ON
    XPlainTextEdit  m_plainEdit;    /**< 多行编辑。 */
#endif
#if XDATETIMEEDIT_ON
    XDateTimeEdit   m_dtEdit;       /**< 日期时间编辑。 */
#endif
#if XDATEEDIT_ON
    XDateEdit       m_dateEdit;     /**< 日期编辑（对标 QDateEdit）。 */
#endif
#if XTIMEEDIT_ON
    XTimeEdit       m_timeEdit;     /**< 时间编辑（对标 QTimeEdit）。 */
#endif
#if XFONTCOMBOBOX_ON
    XFontComboBox   m_fontCombo;    /**< 字体下拉框。 */
#endif
#if XCALENDARWIDGET_ON
    XCalendarWidget m_calendar;     /**< 日历。 */
#endif
#if XTEXTBROWSER_ON
    XTextBrowser    m_textBrowser;  /**< 文本浏览器。 */
#endif
#if XTABLEWIDGET_ON
    XTableWidget    m_tableWidget; /**< 表格控件。 */
#endif
#if XCHARTS_ON
    XChartView      m_chartView;   /**< 图表视图。 */
    XPushButton     m_btnLegend;   /**< 图表：图例开关。 */
    XPushButton     m_btnGrid;     /**< 图表：网格开关。 */
    XPushButton     m_btnTitle;    /**< 图表：标题开关。 */
    XPushButton     m_btnSeries;   /**< 图表：序列循环。 */
    XPushButton     m_btnRange;    /**< 图表：Y 轴范围切换。 */
    int             m_chartSeriesMode; /**< 图表序列显示模式 0=全部。 */
    int             m_chartRange;  /**< 图表 Y 轴范围 0=0..60 1=0..30。 */
#endif
#if XMDIAREA_ON
    XMdiArea        m_mdiArea;      /**< MDI 区域。 */
#endif
#if XSTATUSBAR_ON
    XStatusBar      m_sb;           /**< 状态栏。 */
    XLabel          m_sbLabel;      /**< 状态栏标签。 */
#if XWIDGET_ON && XSTATUSBAR_ON && XABSTRACTBUTTON_ON && XPUSHBUTTON_ON && \
    XWIZARD_ON && XERRORMESSAGE_ON
    XPushButton     m_weOpenWizardBtn; /**< 页签 8 启动器：打开向导。 */
    XPushButton     m_weShowErrBtn;    /**< 页签 8 启动器：显示错误提示。 */
#endif
#if XWIZARD_ON
    XWizard         m_wizard;       /**< 向导。 */
    XWizardPage     m_wizPage0;     /**< 向导页 0。 */
    XWizardPage     m_wizPage1;     /**< 向导页 1。 */
    XWizardPage     m_wizPage2;     /**< 向导页 2。 */
#endif
#if XERRORMESSAGE_ON
    XErrorMessage   m_errMsg;       /**< 错误消息。 */
#endif
#endif
#if XSTACKEDWIDGET_ON && XLAYOUT_STACKED_ON
    XStackedWidget  m_stackedW;     /**< 堆叠容器。 */
#endif
#if XBUTTONGROUP_ON
    XButtonGroup    m_btnGroup;     /**< 按钮组。 */
    XCheckBox       m_bgBtn0;       /**< 按钮组成员 0。 */
    XCheckBox       m_bgBtn1;       /**< 按钮组成员 1。 */
#endif
#endif
#if XWIDGET_ON && XPUSHBUTTON_ON
    XPushButton     m_button; /**< 页面 0：常驻按钮（点击/信号演示）。 */
#endif
#if XWIDGET_ON && XFRAME_ON && XLABEL_ON
    XLabel          m_linkLabel; /**< 页面 0：按钮信号联动标签。 */
#endif
#if XWIDGET_ON && XABSTRACTBUTTON_ON && XPUSHBUTTON_ON && XCOMMANDLINKBUTTON_ON
    XCommandLinkButton m_commandLink; /**< 页面 0：命令链接按钮（双行描述）。 */
#endif
#if XWIDGET_ON && XABSTRACTBUTTON_ON && XTOOLBUTTON_ON && XMENU_ON
    XToolButton     m_toolButton; /**< 页面 0：工具按钮（默认动作 + 弹出菜单）。 */
    XAction         m_toolAction; /**< 工具按钮默认动作（嵌入）。 */
    XMenu           m_toolMenu; /**< 工具按钮弹出菜单（嵌入）。 */
#endif
#if XWIDGET_ON && XABSTRACTBUTTON_ON && XCHECKBOX_ON
    XCheckBox       m_checkBox; /**< 页面 1：三态复选框。 */
#endif
#if XWIDGET_ON && XABSTRACTBUTTON_ON && XRADIOBUTTON_ON
    XRadioButton    m_radioA; /**< 页面 1：单选按钮 A（互斥组）。 */
    XRadioButton    m_radioB; /**< 页面 1：单选按钮 B。 */
#endif
#if XWIDGET_ON && XFRAME_ON && XLABEL_ON
    XLabel          m_choiceLabel; /**< 页面 1：选择状态联动标签。 */
#endif
#if XWIDGET_ON && XFRAME_ON && XLABEL_ON && XLAYOUT_ON && XLAYOUT_STACKED_ON
    XLabel          m_stackPageOne; /**< 页面 2：内层堆叠第一个页面。 */
    XLabel          m_stackPageTwo; /**< 页面 2：内层堆叠第二个页面。 */
    XStackedLayout  m_stackLayoutInner; /**< 页面 2：内层堆叠演示。 */
#if XPUSHBUTTON_ON
    XPushButton     m_stackPrevButton; /**< 页面 2：内层上一页按钮。 */
    XPushButton     m_stackNextButton; /**< 页面 2：内层下一页按钮。 */
#endif
#endif
} DemoWin;

#if XGUI_PERFORMANCE_OVERLAY_ON && XWIDGET_ON && XFRAME_ON && XLABEL_ON
static int64_t demo_monotonicUsecs(void);
#endif

/* ---------------- 绘制 ----------------
 * 全部使用 ARGB32 预乘颜色：软件光栅化由 XWidget 后备存储统一上屏，
 * 与平台（X11/Win32）无关。 */

/** @brief 填充一个矩形（坐标自动裁剪到窗口内）。 */
static void demo_fill_rect(XPainter* painter, int x, int y, int w, int h,
                           uint32_t argb)
{
    XRect rect;
    if (w <= 0 || h <= 0) return;
    XRect_init(&rect, x, y, w, h);
    XPainter_fillRect(painter, &rect, argb);
}

/** @brief 统计 UTF-8 串的字符数（导航钮宽/分组标题宽按字符数推导）。 */
static int demo_utf8_chars(const char* text)
{
    int count = 0;
    const unsigned char* p;
    if (!text) return 0;
    for (p = (const unsigned char*)text; *p; ++p)
        if ((*p & 0xC0) != 0x80) ++count; /* 跳过续字节 */
    return count;
}

/** @brief 将 demo 使用的 XFont 默认家族设置为当前可用的内置字库。
 * @details 桌面（XFONT_BUILTIN_OUTLINE_ON=0）默认家族 "XFontOutlineCommon"
 *          是普通名——引擎按 XFONT_EXTERNAL_OUTLINE_FONT_DIR（默认
 *          "../Library/XFont"，相对进程 cwd）枚举外挂 .xfo/.inc（XFont.c
 *          XFont_outlinePathBuild）。demo 常从 bin-release/bin-* 目录起跑，
 *          cwd 恰为仓库子目录时 "../Library/XFont" 失配 → 负缓存 → 全链
 *          回落 XFont8x16 点阵，GB2312 常用字大面积豆腐（2026-10-01 美学
 *          评审第 1 轮实证）。此处改经 XCoreApplication_applicationDirPath
 *          拼 exe 相对绝对路径 "<exeDir>/../Library/XFont/XFontOutlineCommon.xfo"
 *          ——绝对路径走候选枚举 idx 0（XFont_outlinePathBuild direct 分支），
 *          与起跑 cwd 无关；文件缺失时由 XFont_face 回落链兜底
 *          （XFontFace.c XFont_face：外挂轮廓/点阵皆失配 → 注册链首位
 *          位图 provider = XFont8x16），语义同旧默认链不劣化。
 *          内嵌轮廓字库构建（Android）仍走 provider 家族名直配。 */
static void demo_apply_default_font(XFont* font)
{
    if (!font)
        return;
#if XFONT_BUILTIN_OUTLINE_ON
    XFont_setFamily(font, XGUI_DEMO_DEFAULT_FONT_FAMILY);
#else
    {
        const XString* exeDir = XCoreApplication_applicationDirPath();
        char fontPath[XFONT_EXTERNAL_FONT_PATH_MAX];
        /* 契约: applicationDirPath 返回堆串调用者释放(ASan 实测每控件
         * 一次泄漏, 47 控件/轮)。setFamily 深拷贝路径串, 两路都先释放。 */
        if (exeDir &&
            snprintf(fontPath, sizeof(fontPath),
                     "%s/../Library/XFont/XFontOutlineCommon.xfo",
                     XString_toUtf8(exeDir)) > 0 &&
            strlen(fontPath) < sizeof(fontPath)) {
            XFont_setFamily(font, fontPath); /* setFamily 深拷贝（XFont.c XFont_setFamily）。 */
            XClassDelete((XClass*)exeDir);
            return;
        }
        if (exeDir)
            XClassDelete((XClass*)exeDir);
        XFont_setFamily(font, XGUI_DEMO_DEFAULT_FONT_FAMILY);
    }
#endif /* XFONT_BUILTIN_OUTLINE_ON */
}

#if XWIDGET_ON
/** @brief 将 demo 默认字体应用到一个控件，供控件内部文字绘制使用。 */
static void demo_set_widget_default_font(XWidget* widget)
{
    XFont font;
    if (!widget)
        return;
    XFont_init(&font);
    demo_apply_default_font(&font);
    XWidget_setFont(widget, &font);
    XClassDeinit(&font);
}
#endif /* XWIDGET_ON */

/** @brief 画棋盘格纹理：验证亚像素级脏区提交正确性。
 * @details 调试遗留工具函数：当前调用点已全部摘除（曾绘于静态场景
 *          标题栏右端验证脏区提交，在部分窗口宽度下落进键盘页黄色
 *          说明行，观感为多余圆角图标）。函数体与调用定式保留，后续
 *          需要逐块验证脏区提交/混合正确性时按
 *          demo_draw_checker(painter, x, y, cols, rows, cell) 复用。 */
static void demo_draw_checker(XPainter* painter, int x0, int y0,
                              int cols, int rows, int cell)
{
    int xi;
    int yi;
    for (yi = 0; yi < rows; ++yi) {
        for (xi = 0; xi < cols; ++xi) {
            /* 棋盘奇偶格交替填色，保证每个 8x8 像素块都可区分。 */
            if (((xi + yi) & 1) != 0)
                demo_fill_rect(painter, x0 + xi * cell, y0 + yi * cell,
                               cell, cell, 0xffc8d8e8u);
            else
                demo_fill_rect(painter, x0 + xi * cell, y0 + yi * cell,
                               cell, cell, 0xff4a7ba6u);
        }
    }
}

#if XWIDGET_ON && XFRAME_ON && XLABEL_ON
/**
 * @brief 在窗口后备缓冲中绘制一个可见的 XLabel 测试场景。
 * @param painter 已绑定后备缓冲的绘制器。
 * @param x 标签在窗口客户区中的横坐标。
 * @param y 标签在窗口客户区中的纵坐标。
 * @param width 标签可用宽度。
 * @param height 标签可用高度。
 * @param text 要显示的 UTF-8 文本。
 * @param pixelSize 标签文字像素高度，16 为原始点阵字号，32 为两倍放大。
 * @param family 字库 family；NULL 使用 demo 当前默认字库。
 */
static void demo_draw_label(XPainter* painter, int x, int y, int width,
                            int height, const char* text, int pixelSize,
                            const char* family)
{
    XLabel label;
    if (!painter || width <= 0 || height <= 0) return;
    memset(&label, 0, sizeof(label));
    XLabel_init(&label, NULL, 0);
    {
        XFont labelFont = XWidget_font((XWidget*)&label);
        XFont_setFamily(&labelFont,
                        family ? family : XGUI_DEMO_DEFAULT_FONT_FAMILY);
        XWidget_setFont((XWidget*)&label, &labelFont);
        XClassDeinit(&labelFont);
    }
    XLabel_setText_2(&label, text);
    XLabel_setTextPixelSize(&label, pixelSize);
    XLabel_setAlignment(&label, XAlignment_Left | XAlignment_Top);
    XWidget_resize((XWidget*)&label, width, height);
    if (XPainter_save(painter)) {
        XPainter_translate(painter, (float)x, (float)y);
        XLabel_drawContents(&label, painter);
        XPainter_restore(painter);
    }
    XClassDeinit(&label);
}
#endif /* XWIDGET_ON && XFRAME_ON && XLABEL_ON */

/* ==================== 性能悬浮层 ==================== */

#if XGUI_PERFORMANCE_OVERLAY_ON && XWIDGET_ON && XFRAME_ON && XLABEL_ON

/** @brief 性能悬浮层右下锚定：右缘贴齐窗口（右边不留空），底部避让
 *         状态栏 26px（状态栏固定占窗口底部 26px，悬浮层压上去会被
 *         状态文本遮挡/互相重绘）。setPresetPosition 的 margin 是 x/y
 *         单值，无法表达“右贴齐、底留距”，故这里直接算坐标。 */
static void demo_performance_anchorBottomRight(DemoWin* self)
{
    XWidget* base;
    XRect geo;
    int x;
    int y;
    if (!self) return;
    /* [2026-10-06] 最小化卷起态(Shade: 窗口只剩标题条)悬浮层随内容
       隐藏——卷起后窗口高=条高, 右下重锚钳到 y=0 会盖住标题条(真机
       用户实测「最小化后性能悬浮窗盖住标题栏」); 展开即恢复。 */
    if (XWindowDecoration_isShaded((XWidget*)&self->m_base)) {
        if (XWidget_isVisible((XWidget*)&self->m_performanceOverlay))
            XWidget_hide((XWidget*)&self->m_performanceOverlay);
        return;
    }
    if (!XWidget_isVisible((XWidget*)&self->m_performanceOverlay))
        XWidget_show((XWidget*)&self->m_performanceOverlay);
    /* 悬浮窗设置页已显式预设位置：用户定位优先，挂起自动重锚
       （预设回右下/复位经 demo_main_overlay_applyPreset 恢复）。 */
    if (self->m_overlayPinned) return;
    base = (XWidget*)&self->m_base;
    geo = XPerformanceOverlay_geometry(&self->m_performanceOverlay);
    x = XWidget_width(base) - geo.width;
    y = XWidget_height(base) - geo.height - 26;
    if (x < 0) x = 0;
    if (y < 0) y = 0;
    XPerformanceOverlay_setPosition(&self->m_performanceOverlay, x, y);
    self->m_overlayAnchorW = geo.width;  /* 记录锚定时尺寸，供每帧检测 */
    self->m_overlayAnchorH = geo.height;
}

/** @brief 初始化性能悬浮层；作为顶层子控件参与控件树绘制。 */
static void demo_performance_init(DemoWin* self)
{
    if (!self) return;
    XPerformanceOverlay_init(&self->m_performanceOverlay, &self->m_base, 0);
    /* 悬浮窗自身不接收鼠标：按下/拖动命中测试由 demo 根控件统一处理
       （见 VDemoWin_mousePressEvent），避免子控件抢先消费事件。 */
    XWidget_setAttribute((XWidget*)&self->m_performanceOverlay,
                         XWidgetAttribute_TransparentForMouseEvents, true);
    /* 状态栏固定占用窗口底部 26px。把浮层缩至四行文字所需高度
       （FPS/帧耗时/CPU·GPU/网络），并在状态栏上方保留同样的 26px
       间距，避免每帧脏区同时重绘/遮挡状态标签。 */
    XPerformanceOverlay_setSize(&self->m_performanceOverlay, 210, 70);
    XPerformanceOverlay_setAutoFitSize(&self->m_performanceOverlay, true);
    XPerformanceOverlay_setFontFamily(&self->m_performanceOverlay,
                                      XGUI_DEMO_DEFAULT_FONT_FAMILY);
    XPerformanceOverlay_setTextPixelSize(&self->m_performanceOverlay, 12);
    demo_performance_anchorBottomRight(self); /* 右下角：右贴齐、底避状态栏 */
    /* 默认固定右下（2026-10-03 用户口径）：fixed 态下 resize/内容自适
       应尺寸变化自动重锚（resizeEvent 与 paintEvent 的锚定时尺寸差检
       测双路）。右键可解 fixed 转自由拖动，再右键切回（切换分支保留）。
       （2026-10-03 早间曾裁自由拖动为默认，用户复裁：默认仍固定。） */
    XPerformanceOverlay_setFixed(&self->m_performanceOverlay, true);
    /* 自由拖动（2026-10-03 用户裁定）：不再 setFixed(true)。框架确认
       XPerformanceOverlay.c:937 beginDrag / :954 dragTo 入口条件均为
       m_movable && !m_fixed（init 默认 m_movable=true、m_fixed=false，
       :583/:532），非 fixed 态即启用 VDemoWin_mousePressEvent 的
       beginDrag 与 VDemoWin_mouseMoveEvent 的 dragTo 拖动链；resize/
       重锚路径以 isFixed 分支自动短路。初始仍右下锚定，重叠由用户拖
       避（图表页让位已撤销，见 demo_layout_content）。右键仍可切回
       fixed（VDemoWin_mousePressEvent 切换分支保留）。 */
}

static void demo_performance_deinit(DemoWin* self)
{
    if (!self) return;
    XClassDeinit(&self->m_performanceOverlay);
}

/** @brief 判断窗口客户区坐标是否命中性能悬浮层。 */
static bool demo_performance_contains(DemoWin* self, XPoint position)
{
    XRect geo;
    if (!self) return false;
    geo = XPerformanceOverlay_geometry(&self->m_performanceOverlay);
    return position.x >= geo.x && position.x < geo.x + geo.width &&
           position.y >= geo.y && position.y < geo.y + geo.height;
}

#endif /* XGUI_PERFORMANCE_OVERLAY_ON && XWIDGET_ON && XFRAME_ON && XLABEL_ON */

/* ==================== 系统标题栏（框架级 XWindowDecoration） ====================
 * 桌面与 fbdev 共用同一框架能力：无窗口管理器环境（fbdev 直驱、X11 后
 * 端不可用回落）由 XWindowDecoration 自绘系统标题栏并接管输入——窗口
 * 标题（超宽省略）+ 窗口图标/系统菜单钮（还原/最小化/最大化/关闭）+
 * —/□(还原)/✕ 三键 + 双击空白区切换最大化 + 空白区拖拽移动 + 边缘 8
 * 向拖拽改尺寸 + 活动窗口配色；绘制走样式 CC_TitleBar（调色板/标准图
 * 标/PM 度量），换风格自动跟随。桌面由 WM 绘制（flags 走
 * _MOTIF_WM_HINTS），本 demo 不感知差异。demo 只按
 * XWindow_frameMargins 的保留边距让位布局，不再自绘/自命中（历史
 * demo 层合成实现已整体移除，含其真机教训——全部迁移进装饰模块）。 */
/** @brief 当前框架系统标题栏高度（框架未装饰=0，布局据此让位）。 */
static int demo_sysbarH(DemoWin* self)
{
    /* 布局期（窗口句柄未建）也能取到预测边距：装饰判定支持无句柄预
     * 测，子控件排布与 show 后的真实边距一致（否则按 0 排布后不重
     * 排，CSD 下页签点击落空）。 */
    return XWindowDecoration_marginsFor((XWidget*)self).top;
}

/* demo_drawStaticScene 无条件编译：静态场景缓存关闭（昆仑通态 fbdev
 * 定版=0）时，demo_paintScene 的 #else 分支仍需现绘整页静态基底。 */
/** @brief 绘制不随性能采样变化的 Demo 场景：窗口背景、标题栏与状态栏基底。
 * @details 标题/状态文本与导航按钮由真实子控件接管；此处只画静态底色。
 *          标题栏右端的棋盘格脏区验证贴片（demo_draw_checker）已摘除：
 *          在当前窗口宽度下该贴片落进键盘页黄色说明行，观感为多余的
 *          圆角图标（调试遗留清理）；函数本身保留待脏区验证复用。 */
static void demo_drawStaticScene(DemoWin* self, XPainter* painter, int w, int h)
{
    XFont painterFont;
    if (!self || !painter || w <= 0 || h <= 0) return;
    XFont_init(&painterFont);
    demo_apply_default_font(&painterFont);
    XPainter_setFont(painter, &painterFont);
    XClassDeinit(&painterFont);

    demo_fill_rect(painter, 0, 0, w, h, 0xfff4f6f8u);       /* 窗口背景 */
    demo_fill_rect(painter, 0, demo_sysbarH(self), w, 40, 0xff1f4e79u); /* 标题栏基底 */
    /* 棋盘格装饰（恢复，用户裁定保留）：贴右对齐（块宽 24 + 右缘 8），
     * y 随系统栏高度动态（demo_sysbarH）。 */
    demo_draw_checker(painter, w - 24 - 8, demo_sysbarH(self) + 8, 2, 2, 12);
    /* 导航为浮动面板（子控件自绘），静态基底不再画导航带分隔线。 */
    /* 状态栏底色由 DemoStatusLabel 子控件自带（要盖在越界内容之上，
     * 不能画在根背景里）。 */
    /* 标题文本由 m_titleLabel 子控件绘制（深蓝底白字），静态场景不再重复画。 */
}

#if XGUI_DEMO_STATIC_SCENE_CACHE_ON
/** @brief 尺寸或控件状态变化后重建静态场景缓存。 */
static bool demo_updateStaticScene(DemoWin* self, int w, int h)
{
    XPainter painter;
    if (!self || w <= 0 || h <= 0) return false;
    if (!self->m_staticSceneDirty &&
        XImage_width(&self->m_staticScene) == w &&
        XImage_height(&self->m_staticScene) == h)
        return true;
    if (self->m_staticScene.m_data)
        XClassDeinit(&self->m_staticScene);
    XImage_init_ex(&self->m_staticScene, w, h, XImageFormat_ARGB32);
    if (XImage_isNull(&self->m_staticScene)) return false;
    XPainter_init(&painter, NULL);
    if (!XPainter_begin_image(&painter, &self->m_staticScene)) {
        XPainter_deinit(&painter);
        return false;
    }
    demo_drawStaticScene(self, &painter, w, h);
    XPainter_end(&painter);
    XPainter_deinit(&painter);
    self->m_staticSceneDirty = false;
    return true;
}

/** @brief 将静态场景中对应 tile 的 32 位像素直接复制到后备绘制设备。 */
static bool demo_copyStaticTile(const XImage* scene, XImage* tile,
                                const XRect* tileRect, const XPoint* offset)
{
    const uint8_t* source;
    uint8_t* target;
    size_t rowBytes;
    int targetX;
    int targetY;
    int row;
    if (!scene || !tile || !tileRect || tileRect->x < 0 || tileRect->y < 0 ||
        tileRect->width <= 0 || tileRect->height <= 0 ||
        tileRect->x + tileRect->width > XImage_width(scene) ||
        tileRect->y + tileRect->height > XImage_height(scene) ||
        XImage_depth(scene) != 32 || XImage_depth(tile) != 32)
        return false;
    targetX = tileRect->x + (offset ? offset->x : 0);
    targetY = tileRect->y + (offset ? offset->y : 0);
    if (targetX < 0 || targetY < 0 ||
        targetX + tileRect->width > XImage_width(tile) ||
        targetY + tileRect->height > XImage_height(tile))
        return false;
    source = XImage_constBits(scene);
    target = XImage_bits(tile);
    if (!source || !target) return false;
    target += (size_t)targetY * (size_t)XImage_bytesPerLine(tile) +
              (size_t)targetX * 4u;
    rowBytes = (size_t)tileRect->width * 4u;
    for (row = 0; row < tileRect->height; ++row) {
        memcpy(target + (size_t)row * (size_t)XImage_bytesPerLine(tile),
               source + (size_t)(tileRect->y + row) *
                            (size_t)XImage_bytesPerLine(scene) +
                            (size_t)tileRect->x * 4u,
               rowBytes);
    }
    return true;
}
#endif /* XGUI_DEMO_STATIC_SCENE_CACHE_ON */

/** @brief 重绘整个窗口：resize 后备存储 -> 绘制 -> flush 提交原生窗口。 */
static void demo_paintScene(DemoWin* self, XEvent* event)
{
    XImage* device;
    XPainter painter;
    XPoint offset;
    XRect dirty;
    XRect tile;
    int width;
    int height;
    if (!self) return;
    width = XWidget_width(&self->m_base);
    height = XWidget_height(&self->m_base);
    if (width <= 0 || height <= 0) return;
    device = XWidget_paintImage(&self->m_base);
    if (!device) return;
    offset = XWidget_paintOffset(&self->m_base);
    if (event && XEvent_type(event) == XEVENT_TYPE_PAINT)
        dirty = XPaintEvent_rect((const XPaintEvent*)event);
    else
        XRect_init(&dirty, 0, 0, width, height);
    /* 事件脏区是控件本地坐标；转成静态场景坐标并裁剪到窗口内，
       这样常态刷新只复制悬浮层所在小块，而不是整帧 memcpy。 */
    dirty.x += offset.x;
    dirty.y += offset.y;
    if (dirty.x < 0) {
        dirty.width += dirty.x;
        dirty.x = 0;
    }
    if (dirty.y < 0) {
        dirty.height += dirty.y;
        dirty.y = 0;
    }
    if (dirty.x + dirty.width > width)
        dirty.width = width - dirty.x;
    if (dirty.y + dirty.height > height)
        dirty.height = height - dirty.y;
    if (dirty.width <= 0 || dirty.height <= 0) return;
    XPainter_init(&painter, NULL);
    if (!XPainter_begin_image(&painter, device)) {
        XPainter_deinit(&painter);
        return;
    }
    if (offset.x != 0 || offset.y != 0)
        XPainter_translate(&painter, (float)offset.x, (float)offset.y);
    tile = dirty;
#if XGUI_DEMO_STATIC_SCENE_CACHE_ON
    if (XPainter_rasterBackend(&painter) == XPainterRasterBackend_Gpu) {
        /* GPU 直通：绘制目标是窗口 GL 帧缓冲而非 XImage，CPU memcpy
           的静态场景拷贝不生效；直接用 GPU 原语重画静态场景。 */
        demo_drawStaticScene(self, &painter, width, height);
        /* 整帧重绘完成后清静态脏标记（对标软件路径 demo_updateStaticScene
           的清位语义）：GPU 分支没有 CPU 缓存图像可更新，若不清位，
           m_staticSceneDirty 在 GPU 模式永远为真，空闲帧泵的每次
           demo_repaint 都退化为整窗重绘（提交链/读回/BitBlt 全开）。
           仅当本次脏区已覆盖整窗时才清——FBO 基底随本次整帧刷新完毕；
           部分脏区不改变基底判定，保留标记让后续补整帧。 */
        if (dirty.x <= 0 && dirty.y <= 0 &&
            dirty.x + dirty.width >= width &&
            dirty.y + dirty.height >= height)
            self->m_staticSceneDirty = false;
    }
    else if (!demo_updateStaticScene(self, width, height) ||
             !demo_copyStaticTile(&self->m_staticScene, device, &tile,
                                  &offset))
        demo_drawStaticScene(self, &painter, width, height);
#else
    demo_drawStaticScene(self, &painter, width, height);
#endif /* XGUI_DEMO_STATIC_SCENE_CACHE_ON */
    /* 性能悬浮层自改为顶层子控件后由 paintTree 最后绘制（见
       VXPerformanceOverlay_paintEvent），根背景阶段不再手动叠加，避免
       业务页面控件（如第 4 页 GroupBox）反向盖住浮层顶部。 */
    XPainter_end(&painter);
    XPainter_deinit(&painter);
}

/** @brief 基准模式是否强制整帧重绘（--benchmark-full）。默认基准走
 *  「静态场景缓存 + 仅重绘性能浮层小块」路径，反映小区域增量刷新；
 *  强制整帧才能得到每页的真实全屏绘制成本。 */
static bool g_benchmarkFullRedraw;
/** @brief 交互空闲闸门（环境变量 XGUI_DEMO_IDLE_GATE，默认开）。开启时
 *  帧泵空闲不再逐轮强制重绘（无脏区不重绘，对标 Qt），悬浮层指标改为
 *  独立降频定时器（XGUI_DEMO_IDLE_OVERLAY_MS，4Hz）自刷新；设 0 回退
 *  旧「帧泵每轮 processEvents 强制重绘」口径。--benchmark/--benchmark-full
 *  走 demo_runFrameBenchmark 独立循环不注册帧泵，基准口径不受本开关
 *  影响；--autotest/--screenshot 按帧数推进，在闸门内显式旁路。 */
static bool g_idleGate = true;
/** @brief 按 Qt QWidget::update() 语义合并待绘区域，不同步强制整树重绘。 */
static void demo_input_autotest(DemoWin* self);
#if XWIDGET_ON && XFRAME_ON && XLABEL_ON
static void demo_layout_chrome(DemoWin* self);
#endif
#if XWIDGET_ON && XLAYOUT_ON && XLAYOUT_STACKED_ON
static void demo_layout_content(DemoWin* self);
static void demo_switchPage(DemoWin* self, int index);
#endif

#if XGUI_DEMO_STATIC_SCENE_CACHE_ON
/** @brief 静态场景缓存是否可直接复用（决定 demo_repaint 的重绘范围）。
 * @details 软件模式以 CPU 缓存图像的尺寸与显式脏标记共同判定；GPU 直通
 *          没有 CPU 缓存图像（demo_paintScene 走 GPU 原语重画基底），
 *          若沿用尺寸比较，空缓存图像恒不匹配窗口尺寸，会把每次重绘
 *          放大成整窗——这是 GPU 空闲高频全窗重绘的根因之一。GPU 口径
 *          只认显式脏标记 m_staticSceneDirty（demo_paintScene 在整帧
 *          绘制后清位，部分脏区不动基底判定）。 */
static bool demo_staticSceneCacheUsable(const DemoWin* self)
{
    if (!self || self->m_staticSceneDirty) return false;
    if (XImage_width(&self->m_staticScene) == XWidget_width(&self->m_base) &&
        XImage_height(&self->m_staticScene) == XWidget_height(&self->m_base))
        return true;
#if XPLATFORMINTEGRATION_ON && XGPU_ON
    /* GPU 请求口径：FBO 基底持久，无需 CPU 缓存图像（会话创建失败回退
       软件时，软件分支会重建缓存图像，尺寸比较自然恢复主导判定）。 */
    if (XGpuRenderBackend_requested())
        return true;
#endif
    return false;
}
#endif /* XGUI_DEMO_STATIC_SCENE_CACHE_ON */
static void demo_repaint(DemoWin* self)
{
    XRect dirty;
    if (!self) return;
    if (g_benchmarkFullRedraw) {
        dirty = XWidget_rect(&self->m_base);
        XWidget_updateRect(&self->m_base, &dirty);
        return;
    }
#if XGUI_DEMO_STATIC_SCENE_CACHE_ON
    if (!demo_staticSceneCacheUsable(self)) {
        dirty = XWidget_rect(&self->m_base);
    }
    else
#endif /* XGUI_DEMO_STATIC_SCENE_CACHE_ON */
    {
#if XGUI_PERFORMANCE_OVERLAY_ON && XFRAME_ON && XLABEL_ON
        dirty = XPerformanceOverlay_geometry(&self->m_performanceOverlay);
#else
        dirty = XWidget_rect(&self->m_base);
#endif
    }
    XWidget_updateRect(&self->m_base, &dirty);
}
/** @brief 返回单调微秒计时，用于真实窗口绘制基准。 */
static int64_t demo_monotonicUsecs(void)
{
    /* 统一走 XDateTime 提供的跨平台时钟，避免在 demo 里散落平台时钟 API。 */
    return XDateTime_currentNSecsSinceEpoch() / 1000LL;
}

/**
 * @brief 持续运行真实窗口重绘，统计图形路径的吞吐与最长帧。
 * @details 固定尺寸模式每帧直接完整绘制 demo；尺寸切换模式则交替调用
 *          Win32 SetWindowPos，WM_SIZE 同步进入 resizeEvent 并完成一次
 *          完整绘制。两种模式都经过 XWidget 的后备存储、XPainter 和平台提交。
 */
static void demo_runFrameBenchmark(DemoWin* self, int durationSeconds,
                                   bool resizeWindow)
{
    const int normalWidth = 520;
    const int normalHeight = 360;
    const int largeWidth = 960;
    const int largeHeight = 720;
    int64_t start;
    int64_t now;
    int64_t frameStart;
    int64_t frameUsecs;
    int64_t longestUsecs = 0;
    int64_t elapsedUsecs;
    unsigned frameCount = 0;
    bool large = false;
    if (!self || durationSeconds <= 0)
        return;

    /* 排除窗口首次 show/expose 和首个 DIB 创建成本。 */
    XGuiApplication_processEvents(XEventLoop_AllEvents);
    demo_repaint(self);
    XGuiApplication_processEvents(XEventLoop_AllEvents);

    start = demo_monotonicUsecs();
    do {
        frameStart = demo_monotonicUsecs();
        if (resizeWindow) {
            large = !large;
            XWidget_setGeometry(&self->m_base, 60, 60,
                                large ? largeWidth : normalWidth,
                                large ? largeHeight : normalHeight);
        }
        else {
            demo_repaint(self);
        }
        XGuiApplication_processEvents(XEventLoop_AllEvents);
        frameUsecs = demo_monotonicUsecs() - frameStart;
        if (frameUsecs > longestUsecs)
            longestUsecs = frameUsecs;
        ++frameCount;
        now = demo_monotonicUsecs();
    } while (!self->m_closed &&
             now - start < (int64_t)durationSeconds * 1000000LL);

    elapsedUsecs = now - start;
    if (elapsedUsecs <= 0)
        elapsedUsecs = 1;
    XPrintf("XGuiWindowDemo: benchmark mode=%s size=%dx%d frames=%u "
            "elapsed=%.3fs fps=%.1f avg=%.3fms longest=%.3fms\n",
            resizeWindow ? "resize" : "repaint",
            XWidget_width(&self->m_base), XWidget_height(&self->m_base),
            frameCount,
            (double)elapsedUsecs / 1000000.0,
            (double)frameCount * 1000000.0 / (double)elapsedUsecs,
            (double)elapsedUsecs / (double)frameCount / 1000.0,
            (double)longestUsecs / 1000.0);
}

static void demo_stopTimers(DemoWin* self);

/** @brief 事件循环轮询回调：每轮 processEvents 请求一次重绘，代替 1ms
 *         帧定时器，刷新频率只受事件循环调度速度限制。 */
/**
 * @brief      第 4 页输入控件的图形界面自动化验证。
 * @details    程序化注入鼠标/键盘事件（经控件事件入口分发，与真实
 *             输入同路径），断言联动结果：SpinBox 上箭头步进并同步
 *             滑块/进度条、滑块凹槽点击跳转并回写微调框、单行输入
 *             键入更新状态行、微调框内字母被数字校验器拒绝。结果以
 *             XGuiAutoTest: PASS/FAIL 输出，任一断言失败退出码 1。
 * @param      self 演示窗口。
 * @return     无返回值。
 */
static void demo_input_autotest(DemoWin* self)
{
    int failures = 0;
    XSpinBox* spin = &self->m_spinBox;
    XSlider* slider = &self->m_slider;
    XProgressBar* bar = &self->m_progressBar;
    XLineEdit* edit = &self->m_lineEdit;
    int spinW = XWidget_width((XWidget*)spin);
    int spinH = XWidget_height((XWidget*)spin);
    int sliderW = XWidget_width((XWidget*)slider);

#define DEMO_EXPECT(cond, what) \
    do { \
        if (cond) XPrintf("XGuiAutoTest: [PASS] %s\n", what); \
        else { XPrintf("XGuiAutoTest: [FAIL] %s\n", what); ++failures; } \
    } while (0)

    /* 0. 文本控件键盘注入：键入/光标移动/选区（实机渲染验证，
     *    画面呈现于 autotest 交互后截图）。 */
    {
        int ki;
        XWidget* editW = (XWidget*)edit;
        XWidget_setFocus(editW);
        XWidget_update(editW);
        /* 键入 "hello"。 */
        for (ki = 0; ki < 5; ++ki) {
            XKeyEvent ke;
            XKeyEvent_init(&ke, XEVENT_TYPE_KEY_PRESS, 'a' + ki, 0);
            XObject_event_base((XObject*)editW, (XEvent*)&ke);
        }
        /* Left → Left → 键入 "XY"（居中插入）。 */
        {
            XKeyEvent left;
            XKeyEvent_init(&left, XEVENT_TYPE_KEY_PRESS, XKey_Left, 0);
            XObject_event_base((XObject*)editW, (XEvent*)&left);
            XObject_event_base((XObject*)editW, (XEvent*)&left);
            for (ki = 0; ki < 2; ++ki) {
                XKeyEvent ke;
                /* 大写字母按平台大写归一契约携带 Shift 修饰位。 */
                XKeyEvent_init(&ke, XEVENT_TYPE_KEY_PRESS, 'X' + ki,
                               (int)XKeyboardModifier_ShiftModifier);
                XObject_event_base((XObject*)editW, (XEvent*)&ke);
            }
        }
        DEMO_EXPECT(strcmp(XLineEdit_text(edit), "abcXYde") == 0 &&
                    XLineEdit_cursorPosition(edit) == 5,
                    "实机键入 abcXYde 光标 5");
        /* 程序化选区：前 4 字符高亮可见。 */
        XLineEdit_setSelection(edit, 0, 4);
        XWidget_update(editW);
    }

    /* 1. 点击微调框上箭头：值 0 -> 1（按钮区右 16px 上半）。 */
    {
        XMouseEvent me;
        XPoint pos;
        XPoint_init(&pos, spinW - 8, spinH / 4);
        XMouseEvent_init(&me, XEVENT_TYPE_MOUSE_BUTTON_PRESS,
                         XMouseButton_LeftButton, 0, pos);
        XObject_event_base((XObject*)spin, (XEvent*)&me);
        /* 输入页初值已统一为 30（P2 外观批次）：上箭头步进 30→31。 */
        DEMO_EXPECT(XSpinBox_value(spin) == 31, "SpinBox 上箭头点击步进到 31");
        DEMO_EXPECT(XProgressBar_value(bar) == 31, "进度条同步到 31");
        DEMO_EXPECT(XAbstractSlider_value((XAbstractSlider*)slider) == 31,
                    "滑块同步到 31");
    }

    /* 2. 点击滑块凹槽中点：handle 跳转约中值并回写微调框。 */
    {
        XMouseEvent me;
        XPoint p;
        XPoint_init(&p, sliderW / 2, XWidget_height((XWidget*)slider) / 2);
        XMouseEvent_init(&me, XEVENT_TYPE_MOUSE_BUTTON_PRESS,
                         XMouseButton_LeftButton, 0, p);
        XObject_event_base((XObject*)slider, (XEvent*)&me);
        {
            int v = XAbstractSlider_value((XAbstractSlider*)slider);
            DEMO_EXPECT(v >= 45 && v <= 55, "滑块凹槽中点点击跳转中值");
            DEMO_EXPECT(XSpinBox_value(spin) == v, "微调框与滑块联动");
            DEMO_EXPECT(XProgressBar_value(bar) == v, "进度条与滑块联动");
        }
    }

    /* 3. 单行输入：中文（IME 提交路径）+ 西文键入，保持焦点看光标。 */
    {
        XKeyEvent ke;
        XLineEdit_setText(edit, "");
        XLineEdit_insert(edit, "\xE4\xB8\xAD"); /* 中（模拟 IME 提交） */
        XKeyEvent_init(&ke, XEVENT_TYPE_KEY_PRESS, 'a', 0);
        XObject_event_base((XObject*)edit, (XEvent*)&ke);
        XKeyEvent_init(&ke, XEVENT_TYPE_KEY_PRESS, 'b', 0);
        XObject_event_base((XObject*)edit, (XEvent*)&ke);
        {
            const char* got = XLineEdit_text(edit);
            XPrintf("XGuiAutoTest: [dbg] got='%s' len=%d bytes:", got,
                    (int)strlen(got));
            {
                const unsigned char* q = (const unsigned char*)got;
                while (*q) XPrintf(" %02X", *q++);
                XPrintf("\n");
            }
            DEMO_EXPECT(strcmp(got, "\xE4\xB8\xAD" "ab") == 0, /* 拼接避免 \xADa 贪婪解析 */
                        "中文+西文混合输入");
        }
        XWidget_setFocus((XWidget*)edit); /* 保持焦点：截图看光标位置 */
    }

    /* 4. 微调框内键入字母 'x'：被数字校验器拒绝（文本不变）。 */
    {
        XKeyEvent ke;
        char before[32];
        XLineEdit* le = XSpinBox_lineEdit((XAbstractSpinBox*)spin);
        XSpinBox_setValue(spin, 42);
        snprintf(before, sizeof(before), "%s", XLineEdit_text(le));
        XKeyEvent_init(&ke, XEVENT_TYPE_KEY_PRESS, 'x', 0);
        XObject_event_base((XObject*)le, (XEvent*)&ke);
        DEMO_EXPECT(strcmp(XLineEdit_text(le), before) == 0,
                    "微调框键入字母被数字校验器拒绝");
    }

    /* XI2 方案 A 回归锁：窗口级触摸按下/抬起 → touch→mouse 仿真点击
       顶部页签「条目视图」→ 页切换到 5（触摸派发在 XWidgetWindow；
       抓取语义修复后，未被接受的触摸按 Qt 语义合成鼠标）。测试后
       恢复输入页（扩展页自动化自行切页）。 */
    {
        /* XI2 方案 A 回归锁：经 XWindowSystemInterface（与平台 XI2 分派
           完全同层）注入触摸按下/抬起 → touch→mouse 仿真点击顶部页签
           「条目视图」→ 页切换到 5。对照组用 handleMouseEvent_ex 验证
           投递链。 */
        XWindow* xwin = (XWindow*)self->m_base.m_windowHandle;
        XPoint tpos;
        XPoint tglobal;
        XTouchEvent te;
        XRect navRect;
        /* 手风琴面板下页面钮仅在其分类展开时可见：先切到页 5 展开
         * 「视图」分类（m_pageNav[5] 随之显形并获得几何），再验证
         * 窗口级合成点击/触摸点页签。 */
        demo_switchPage(self, 5);
        /* 页签坐标随导航几何走：从「条目视图」导航钮（m_pageNav[5]）矩形
         * 取中心（面板停靠/收起/展开任意状态下都从几何推导，硬编码
         * 像素坐标会漂）。 */
        navRect = XWidget_geometry((XWidget*)&self->m_pageNav[5]);
        XPoint_init(&tpos, navRect.x + navRect.width / 2,
                    navRect.y + navRect.height / 2);
        tglobal = tpos;
        /* 对照组：窗口级合成鼠标按下/抬起点页签。 */
        XWindowSystemInterface_handleMouseEvent_ex(
            xwin, XEVENT_TYPE_MOUSE_BUTTON_PRESS, XMouseButton_LeftButton,
            XMouseButton_LeftButton, XKeyboardModifier_NoModifier, tpos,
            &tglobal, 0);
        XWindowSystemInterface_handleMouseEvent_ex(
            xwin, XEVENT_TYPE_MOUSE_BUTTON_RELEASE, XMouseButton_LeftButton,
            0, XKeyboardModifier_NoModifier, tpos, &tglobal, 0);
        XGuiApplication_processEvents(XEventLoop_AllEvents);
        DEMO_EXPECT(XStackedLayout_currentIndex(&self->m_stackLayout) == 5,
                    "对照：窗口级合成鼠标点击页签切换到条目视图");
        /* 触摸组：TOUCH_BEGIN/END → touch→mouse 仿真点同一页签。
           （先回页 3 再重进页 5：验证分类展开态重复进入后按钮仍可命中。） */
        demo_switchPage(self, 3);
        demo_switchPage(self, 5);
        navRect = XWidget_geometry((XWidget*)&self->m_pageNav[5]);
        XPoint_init(&tpos, navRect.x + navRect.width / 2,
                    navRect.y + navRect.height / 2);
        tglobal = tpos;
        XTouchEvent_init(&te, XEVENT_TYPE_TOUCH_BEGIN, &tpos, &tglobal, 1);
        XWindowSystemInterface_handleTouchEvent_ex(
            xwin, XEVENT_TYPE_TOUCH_BEGIN, tpos, &tglobal, 1, 0);
        XTouchEvent_init(&te, XEVENT_TYPE_TOUCH_END, &tpos, &tglobal, 1);
        XWindowSystemInterface_handleTouchEvent_ex(
            xwin, XEVENT_TYPE_TOUCH_END, tpos, &tglobal, 1, 0);
        XGuiApplication_processEvents(XEventLoop_AllEvents);
        DEMO_EXPECT(XStackedLayout_currentIndex(&self->m_stackLayout) == 5,
                    "窗口级触摸按下/抬起经 touch→mouse 仿真切换页签（XI2-A 接线回归锁）");
        demo_switchPage(self, 3);
    }

#undef DEMO_EXPECT
    XPrintf("XGuiAutoTest: %s\n",
            failures == 0 ? "PASS" : "FAIL");
    if (failures)
        XGuiApplication_quit();
}

#if XWIDGET_ON && XLAYOUT_ON && XLAYOUT_STACKED_ON
/* 前向声明：本函数定义在 demo_page_name 之前。 */
static const char* demo_page_name(int index);

/** @brief 扩展页（5~14）图形界面自动化验证调度。
 * @details 逐页切换并调用页面自带的 autotest（xgui_demo_pages.h 契约：
 *          事件注入 + getter 断言，全程非阻塞），汇总失败数；任一失败
 *          退出非零。结束恢复第 4 页，保持交互后截图口径不变。下标 7
 *          =页 12（图表演示，内置页无 autotest 契约）恒 NULL 跳过。 */
static void demo_ext_pages_autotest(DemoWin* demo)
{
    int (*const kTests[10])(XWidget*) = {
        demo_page_views_autotest, demo_page_dialogs_autotest,
        demo_page_advanced_autotest, demo_page_effects_autotest,
        demo_page_keyboard_autotest, demo_page_remote_server_autotest,
        demo_page_remote_client_autotest, /* 2026-10-02 追加第 7 扩展页。 */
        NULL, /* 页 12（图表演示）内置页占位。 */
        demo_page_network_autotest, /* 2026-10-06 网络设置页。 */
        demo_page_overlay_settings_autotest /* 2026-10-06 悬浮窗设置页。 */
    };
    int total = 0;
    int exti;
    for (exti = 0; exti < 10; ++exti) {
        int failures;
        if (!demo->m_extPages[exti] || !kTests[exti])
            continue; /* 页面模块被裁剪/无契约，跳过 */
        demo_switchPage(demo, 5 + exti);
        failures = kTests[exti](demo->m_extPages[exti]);
        XPrintf("XGuiAutoTest: 扩展页%d(%s) %s\n", 5 + exti,
                demo_page_name(5 + exti),
                failures == 0 ? "PASS" : "FAIL");
        /* 负值=页面自检错页（契约 -1），按失败计。 */
        total += failures > 0 ? failures : (failures < 0 ? 1 : 0);
    }
    demo_switchPage(demo, 3);
    if (total)
        XGuiApplication_quit();
}
#endif /* XWIDGET_ON && XLAYOUT_ON && XLAYOUT_STACKED_ON */

static bool demo_framePumpBody(void* userData);

/** @brief autotest 截图落盘目录：POSIX/安卓 /tmp；Windows 用 %TEMP%/%TMP%
 *  （"/tmp/..." 在 Windows 解析为当前盘根 \tmp，CI 换盘符即失效——walk 轮
 *  实测落 D:\tmp）。取自框架环境入口 XSystem_environment；两端都无时回退
 *  当前目录 "."。 */
static const char* demo_autotest_tmpdir(void)
{
#ifdef _WIN32
    const char* dir = XSystem_environment("TEMP");
    if (!dir || !dir[0]) dir = XSystem_environment("TMP");
    return (dir && dir[0]) ? dir : ".";
#else
    return "/tmp";
#endif
}

/** @brief 帧泵（重入守卫）：autotest 内 XGuiApplication_processEvents
 *         会重入本帧定时器，守卫位防递归（触摸接线断言引入事件泵）。 */
static bool demo_framePump(void* userData)
{
    static bool inTick = false;
    bool result;
    /* [perf8r3 临时内存探针] XGUI_REMOTE_BENCH_TRACE=1 时每 5s 打一行
     * SYSTEM 存活/峰值字节(区分真泄漏 vs 分配器碎片), 收尾前回退。 */
    {
        static int s_memCnt = 0;
        if (XSystem_environment("XGUI_REMOTE_BENCH_TRACE") &&
            ++s_memCnt % 250 == 0) {
            XMemoryStatistics st = XMemory_statistics_2(XMEMORY_TYPE_SYSTEM);
            XPrintf("MEMPROF t=%ds sys=%zu peak=%zu\n", s_memCnt / 50,
                    st.systemBytes, st.systemPeakBytes);
        }
    }
    if (inTick) return false;
    inTick = true;
    result = demo_framePumpBody(userData);
    inTick = false;
    return result;
}

static bool demo_framePumpBody(void* userData)
{
    DemoWin* demo = (DemoWin*)userData;
    if (!demo || demo->m_closed)
        return false;
    /* --remote-server CLI 预置的首帧自动启动（须在空闲闸门前——闸门
       开启时本泵早退；内部自带终态标记, 完成后零开销）。 */
    demo_page_remote_server_autostart();
    /* --remote-client CLI 预置的首帧自动连接（2026-10-02 追加; 同闸门
       口径, 内部自带终态标记）。 */
    demo_page_remote_client_autostart();
    /* 交互空闲闸门（XGUI_DEMO_IDLE_GATE，默认开）：常态下帧泵不再逐轮
       强制重绘——对标 Qt「无脏区不重绘」。事件驱动的局部更新本就经
       XWidget_addDirtyRegion -> XWidget_postPaintEvent 异步闭环自足，
       无需帧泵推动；悬浮层指标改由降频定时器（XGUI_DEMO_IDLE_OVERLAY_MS，
       见 VDemoWin_timerEvent / main）驱动，每次只投递悬浮层小区域。
       GPU 模式每次重绘都是完整提交链（FBO 绘制+读回+BitBlt），空闲
       全速重绘成本远高于软件模式，此闸门是 GPU 空闲治理的主开关。
       截图/自动测试按帧数推进逻辑，仍需逐帧泵动（显式旁路）；
       --benchmark 系列走 demo_runFrameBenchmark 不注册帧泵，不受影响。
       设 0 回退旧「每轮 processEvents 强制重绘」口径。 */
    if (g_idleGate && !demo->m_autoTest && !demo->m_screenshotPath)
        return true;
    demo_repaint(demo);
    /* 截图模式：渲染几帧待控件树绘制完成，保存一帧后退出。
       GPU 直通模式窗口内容在 GL 帧缓冲（GDI 抓窗读不到），直接读回
       FBO 保存；软件模式抓真实窗口。 */
    /* 自动交互测试：帧 3 注入事件序列并断言联动（updateRect 异步投
       递 PAINT），帧 5 重绘完成后截图留证并退出（第 4 页输入控件的
       图形界面验证）。 */
    if (demo->m_autoTest) {
        if (demo->m_autoTestFrames == 3) {
            demo_input_autotest(demo);
#if XWIDGET_ON && XLAYOUT_ON && XLAYOUT_STACKED_ON
            demo_ext_pages_autotest(demo);
#endif
            demo->m_staticSceneDirty = true;
            demo_repaint(demo);
        }
        else if (demo->m_autoTestFrames == 5) {
            {
                XImage* device = XWidget_paintImage(&demo->m_base);
                char shotPath[260];
                snprintf(shotPath, sizeof(shotPath), "%s/demo_autotest_after.png",
                         demo_autotest_tmpdir());
                if (device && XImage_save_2(device, shotPath, "PNG", 95))
                    XPrintf("XGuiAutoTest: 交互后截图 %s\n", shotPath);
            }
#if XWIDGET_ON && XLAYOUT_ON && XLAYOUT_STACKED_ON
            /* 切到效果页：repaint 异步投递，本帧只切页，隔两帧再截图。 */
            if (demo->m_extPages[3]) {
                demo_switchPage(demo, 8);
                demo->m_staticSceneDirty = true;
                demo_repaint(demo);
            }
#endif
        }
        else if (demo->m_autoTestFrames >= 7) {
#if XWIDGET_ON && XLAYOUT_ON && XLAYOUT_STACKED_ON
            /* 效果页留证：启用态效果与无效果基线同屏，供集成阶段像素对照。 */
            if (demo->m_extPages[3]) {
                XImage* fxDevice = XWidget_paintImage(&demo->m_base);
                char fxPath[260];
                snprintf(fxPath, sizeof(fxPath), "%s/demo_page8_effects.png",
                         demo_autotest_tmpdir());
                if (fxDevice && XImage_save_2(fxDevice, fxPath, "PNG", 95))
                    XPrintf("XGuiAutoTest: 效果页截图 %s\n", fxPath);
                demo_switchPage(demo, 3);
            }
#endif
            demo_stopTimers(demo);
            demo->m_closed = true;
            XGuiApplication_quit();
            return false;
        }
        ++demo->m_autoTestFrames;
    }
    if (demo->m_screenshotPath) {
        if (++demo->m_screenshotFrames >= 3) {
#if XPLATFORMINTEGRATION_ON && XGPU_ON
            /* GPU 直通帧画面在 FBO（读回保存）；降级/软件帧画面在 XImage。 */
            if (XGpuRenderBackend_requested() &&
                XGpuRenderBackend_framePresented())
            {
                XGpuRenderBackend* session = XGpuRenderBackend_acquireForWindow(
                    (XWindow*)demo->m_base.m_windowHandle,
                    XWidget_width(&demo->m_base),
                    XWidget_height(&demo->m_base));
                if (session)
                {
                    XImage shotImage;
                    bool readOk;
                    XImage_init(&shotImage);
                    XImage_init_ex(&shotImage, XWidget_width(&demo->m_base),
                                   XWidget_height(&demo->m_base),
                                   XImageFormat_ARGB32);
                    /* 建立 GL 上下文后再读回 FBO（窗口模式首帧后不清除）。 */
                    XGpuRenderBackend_beginFrame(session);
                    /* 读回结果必须检查：VK 窗口态 readback 修复前恒返
                       false，未检查会把未填充的全零 XImage 存成全黑
                       PNG（1974 字节根因）。失败落 paintImage 兜底。 */
                    readOk = XGpuRenderBackend_readback(session, &shotImage);
                    XGpuRenderBackend_endWindowFrame();
                    if (readOk)
                    {
                        XPrintf("XGuiWindowDemo: 保存截图到 %s\n",
                                demo->m_screenshotPath);
                        if (!XImage_save_2(&shotImage, demo->m_screenshotPath,
                                           "PNG", 95))
                            XPrintf("XGuiWindowDemo: 截图保存失败\n");
                        XClassDeinit(&shotImage);
                        demo_stopTimers(demo);
                        demo->m_closed = true;
                        XGuiApplication_quit();
                        return false;
                    }
                    XClassDeinit(&shotImage);
                    /* readback 失败：不保存未填充图像，落到下方
                       XWidget_paintImage 分支兜底保存。 */
                    XPrintf("XGuiWindowDemo: GPU 读回失败，回退后备图像\n");
                }
            }
#endif /* XPLATFORMINTEGRATION_ON && XGPU_ON */
            {
                /* 无头截图覆盖目标（键盘页紧凑悬浮态：独立顶层 Popup
                   不在主窗 paintImage 内——截键盘自身背后图像）。 */
                XWidget* shotTarget = demo_page_keyboard_screenshot_target();
                XImage* device = shotTarget ? XWidget_paintImage(shotTarget)
                                            : XWidget_paintImage(&demo->m_base);
                if (device) {
                    XPrintf("XGuiWindowDemo: 保存截图到 %s\n",
                            demo->m_screenshotPath);
                    if (!XImage_save_2(device, demo->m_screenshotPath,
                                       "PNG", 95))
                        XPrintf("XGuiWindowDemo: 截图保存失败\n");
                }
            }
            demo_stopTimers(demo);
            demo->m_closed = true;
            XGuiApplication_quit();
            return false;
        }
    }
    return true;
}

/** @brief 注销轮询回调并停止自动退出定时器，避免对象销毁后继续派发刷新事件。 */
static void demo_stopTimers(DemoWin* self)
{
    if (!self) return;
    if (self->m_framePump) {
        XAbstractEventDispatcher_removePollCallback(self->m_framePump);
        self->m_framePump = NULL;
    }
    if (self->m_autoQuitTimer != XTIMER_INVALID_ID) {
        XObject_killTimer((XObject*)self, self->m_autoQuitTimer);
        self->m_autoQuitTimer = XTIMER_INVALID_ID;
    }
    if (self->m_lcdTimer != XTIMER_INVALID_ID) {
        XObject_killTimer((XObject*)self, self->m_lcdTimer);
        self->m_lcdTimer = XTIMER_INVALID_ID;
    }
    if (self->m_overlayTimer != XTIMER_INVALID_ID) {
        XObject_killTimer((XObject*)self, self->m_overlayTimer);
        self->m_overlayTimer = XTIMER_INVALID_ID;
    }
}

/** @brief 标准事件循环中的自动退出定时器处理。 */
static void VDemoWin_timerEvent(XObject* object, XTimerEvent* event)
{
    DemoWin* self = (DemoWin*)object;
    XTimerId timerId;
    if (!self || !event) return;
    timerId = XTimerEvent_timerId(event);
    if (timerId == self->m_autoQuitTimer) {
        demo_stopTimers(self);
        self->m_closed = true;
        XGuiApplication_quit();
        XEvent_accept((XEvent*)event);
        return;
    }
    if (timerId == self->m_overlayTimer) {
        {
            extern volatile unsigned long g_overlayTimerFires;
            ++g_overlayTimerFires;
        }
        /* 空闲闸门下的悬浮层自刷新（XGUI_DEMO_IDLE_OVERLAY_MS=250ms，
           4Hz，对标 Qt 指标浮层低频心跳）：demo_repaint 在静态场景
           干净时只投递悬浮层自身 210x70 小区域；文本统计窗口同为
           250ms（XGUI_PERFORMANCE_OVERLAY_UPDATE_MS），指标更新与
           重绘节奏一致。XGUI_DEMO_IDLE_GATE=0 时本定时器不启动。 */
        demo_repaint(self);
        XEvent_accept((XEvent*)event);
        return;
    }
    if (timerId == self->m_lcdTimer) {
        /* 循环展示段码表全部 30 行字符（0-9/A-F/a-f 大小写同形/-/.
         * /O/g/h/L/o/P/r/u/U/Y/:，对标 QLCDNumber::display 支持集）。
         * m_lcdValue 复用为字符序列索引。 */
        static const char* const lcdSeq[30] = {
            "0", "1", "2", "3", "4", "5", "6", "7", "8", "9",
            "A", "B", "C", "D", "E", "F",
            "a", "b", "c", "d", "e", "f",
            "-", ".", ":", "O", "g", "h", "L", " "
        };
        /* 数码管停摆门（XGUI_DEMO_IDLE_GATE，默认开）：页签不可见时
           不推字符序列也不触发重绘——定时器驱动的动画在无可见变化时
           停摆（对标 Qt 隐藏控件零绘制）；切回「数码管」页签后下个
           1s 周期自动续播。设 0 回退无条件推进的旧行为。 */
        if (g_idleGate && !XWidget_isVisible((XWidget*)&self->m_lcd)) {
            XEvent_accept((XEvent*)event);
            return;
        }
        self->m_lcdValue = (self->m_lcdValue + 1) % 30;
        XLcdNumber_display(&self->m_lcd, lcdSeq[self->m_lcdValue]);
        XEvent_accept((XEvent*)event);
        return;
    }
    XClass_Parent(XObject, EXObject_TimerEvent,
                  void(*)(XObject*, XTimerEvent*))(object, event);
}

/* ==================== 信号槽 ==================== */

/** @brief 更新底部状态栏文本并触发重绘。 */
static void demo_set_status(DemoWin* self, const char* text)
{
    if (!self || !text) return;
#if XWIDGET_ON && XFRAME_ON && XLABEL_ON
    XLabel_setText_2((XLabel*)&self->m_statusLabel, text);
#endif
    self->m_staticSceneDirty = true;
    demo_repaint(self);
}

#if XWIDGET_ON && XLAYOUT_ON && XLAYOUT_STACKED_ON
/** @brief 页面名称表（与导航按钮一一对应，中文）。 */
static const char* demo_page_name(int index)
{
    static const char* const kNames[15] = {
        "\xE6\x8C\x89\xE9\x92\xAE\xE6\xBC\x94\xE7\xA4\xBA", /* 按钮演示 */
        "\xE9\x80\x89\xE6\x8B\xA9\xE6\xBC\x94\xE7\xA4\xBA", /* 选择演示 */
        "\xE5\xA0\x86\xE5\x8F\xA0\xE6\xBC\x94\xE7\xA4\xBA", /* 堆叠演示 */
        "\xE8\xBE\x93\xE5\x85\xA5\xE6\xBC\x94\xE7\xA4\xBA", /* 输入演示 */
        "\xE5\xAE\xB9\xE5\x99\xA8\xE4\xB8\x8E\xE7\xAA\x97\xE5\x8F\xA3", /* 容器与窗口 */
        "\xE6\x9D\xA1\xE7\x9B\xAE\xE8\xA7\x86\xE5\x9B\xBE", /* 条目视图 */
        "\xE5\xAF\xB9\xE8\xAF\x9D\xE6\xA1\x86",             /* 对话框 */
        "\xE9\xAB\x98\xE7\xBA\xA7\xE6\x8E\xA7\xE4\xBB\xB6", /* 高级控件 */
        "\xE5\x9B\xBE\xE5\xBD\xA2\xE6\x95\x88\xE6\x9E\x9C", /* 图形效果 */
        "\xE9\x94\xAE\xE7\x9B\x98\xE6\xBC\x94\xE7\xA4\xBA", /* 键盘演示 */
        "\xE8\xBF\x9C\xE7\xA8\x8B\xE7\xAA\x97\xE5\x8F\xA3", /* 远程窗口 */
        "\xE8\xBF\x9C\xE7\xA8\x8B\xE5\xAE\xA2\xE6\x88\xB7\xE7\xAB\xAF",  /* 远程客户端 */
        "\xE5\x9B\xBE\xE8\xA1\xA8\xE6\xBC\x94\xE7\xA4\xBA",  /* 图表演示(2026-10-03 打散独立) */
        "\xE7\xBD\x91\xE7\xBB\x9C\xE8\xAE\xBE\xE7\xBD\xAE",  /* 网络设置(2026-10-06 系统设置) */
        "\xE6\x82\xAC\xE6\xB5\xAE\xE7\xAA\x97\xE8\xAE\xBE\xE7\xBD\xAE" /* 悬浮窗设置(2026-10-06 系统设置) */
    };
    if (index < 0 || index > 14)
        return kNames[0];
    return kNames[index];
}

/* ==================== 浮动导航面板 ====================
 * 「先切一级分类、再选页面」两级导航（2026-10-06 一级菜单整改：收拢
 * 为「控件 / 系统设置 / 远程」三大类——原七个平铺分类降为控件类内的
 * 子分组标题行，分类语义与用户操作面一致）。面板默认贴左停靠，「换
 * 边」钮循环 左→右→上→下，「收起」钮贴边成 26px 细条（点细条复原）。
 * 面板内 3 个一级分类钮常驻，活动分类的条目在手风琴下展开——分类即
 * 菜单第一级、页面钮即第二级（子分组标题为不可点的分组提示行）。
 * 内容区几何（demo_layout_content）按面板占位自动避让；resize/停靠/
 * 收起/切页统一走 demo_navUpdatePanel。 */
#define DEMO_NAV_PANEL_W 176   /**< 左右停靠面板默认宽（分割条可拖 140..380）。 */
#define DEMO_NAV_TB_H    100   /**< 上下停靠面板默认高（分割条可拖 92..320）。 */
#define DEMO_NAV_TB_MAX  320   /**< 上下停靠面板高上限（换行页面钮自动抬高至此）。 */
#define DEMO_NAV_STRIP_W 8     /**< 收起贴边分割条细条厚。 */
#define DEMO_NAV_CAT_N   3     /**< 一级分类数（=kNavGroups 项数）。 */
#define DEMO_NAV_PAGE_N  15    /**< 页面数。 */
#define DEMO_NAV_HEAD_N  5     /**< 单分类最多子分组标题数（标签池容量）。 */
#define DEMO_NAV_PAGE_H    24  /**< 页面钮高（竖版）。 */
#define DEMO_NAV_PAGE_PITCH 26 /**< 页面钮行距（竖版）。 */
#define DEMO_NAV_HEAD_PITCH 18 /**< 子分组标题行距（竖版）。 */

/** @brief 导航条目（分类内列表项；page=-1 为子分组标题行，不可点）。 */
typedef struct {
    const char* caption; /**< 条目文本（页面名或子分组标题）。 */
    int         page;    /**< 页索引；-1=子分组标题行。 */
} DemoNavItem;

/** @brief 控件类条目（原七分类收拢为五组：按钮/输入/容器与视图/对话框与高级/图形与图表）。 */
static const DemoNavItem kNavControlsItems[] = {
    { "\xE6\x8C\x89\xE9\x92\xAE", -1 },                                    /* 按钮 */
    { "\xE6\x8C\x89\xE9\x92\xAE\xE6\xBC\x94\xE7\xA4\xBA", 0 },             /* 按钮演示 */
    { "\xE9\x80\x89\xE6\x8B\xA9\xE6\xBC\x94\xE7\xA4\xBA", 1 },             /* 选择演示 */
    { "\xE8\xBE\x93\xE5\x85\xA5", -1 },                                    /* 输入 */
    { "\xE8\xBE\x93\xE5\x85\xA5\xE6\xBC\x94\xE7\xA4\xBA", 3 },             /* 输入演示 */
    { "\xE9\x94\xAE\xE7\x9B\x98\xE6\xBC\x94\xE7\xA4\xBA", 9 },             /* 键盘演示 */
    { "\xE5\xAE\xB9\xE5\x99\xA8\xE4\xB8\x8E\xE8\xA7\x86\xE5\x9B\xBE", -1 },/* 容器与视图 */
    { "\xE5\xA0\x86\xE5\x8F\xA0\xE6\xBC\x94\xE7\xA4\xBA", 2 },             /* 堆叠演示 */
    { "\xE5\xAE\xB9\xE5\x99\xA8\xE4\xB8\x8E\xE7\xAA\x97\xE5\x8F\xA3", 4 }, /* 容器与窗口 */
    { "\xE6\x9D\xA1\xE7\x9B\xAE\xE8\xA7\x86\xE5\x9B\xBE", 5 },             /* 条目视图 */
    { "\xE5\xAF\xB9\xE8\xAF\x9D\xE6\xA1\x86\xE4\xB8\x8E\xE9\xAB\x98\xE7\xBA\xA7", -1 }, /* 对话框与高级 */
    { "\xE5\xAF\xB9\xE8\xAF\x9D\xE6\xA1\x86", 6 },                         /* 对话框 */
    { "\xE9\xAB\x98\xE7\xBA\xA7\xE6\x8E\xA7\xE4\xBB\xB6", 7 },             /* 高级控件 */
    { "\xE5\x9B\xBE\xE5\xBD\xA2\xE4\xB8\x8E\xE5\x9B\xBE\xE8\xA1\xA8", -1 },/* 图形与图表 */
    { "\xE5\x9B\xBE\xE5\xBD\xA2\xE6\x95\x88\xE6\x9E\x9C", 8 },             /* 图形效果 */
    { "\xE5\x9B\xBE\xE8\xA1\xA8\xE6\xBC\x94\xE7\xA4\xBA", 12 }             /* 图表演示 */
};

/** @brief 系统设置类条目（2026-10-06 新增二级设置页）。 */
static const DemoNavItem kNavSystemItems[] = {
    { "\xE7\xBD\x91\xE7\xBB\x9C\xE8\xAE\xBE\xE7\xBD\xAE", 13 },             /* 网络设置 */
    { "\xE6\x82\xAC\xE6\xB5\xAE\xE7\xAA\x97\xE8\xAE\xBE\xE7\xBD\xAE", 14 }  /* 悬浮窗设置 */
};

/** @brief 远程类条目。 */
static const DemoNavItem kNavRemoteItems[] = {
    { "\xE8\xBF\x9C\xE7\xA8\x8B\xE7\xAA\x97\xE5\x8F\xA3", 10 },             /* 远程窗口 */
    { "\xE8\xBF\x9C\xE7\xA8\x8B\xE5\xAE\xA2\xE6\x88\xB7\xE7\xAB\xAF", 11 }  /* 远程客户端 */
};

/** @brief 一级分类表（手风琴数据源；条目内 page=-1 为子分组标题）。 */
static const struct {
    const char*        caption; /**< 分类名（一级分类钮文本）。 */
    const DemoNavItem* items;   /**< 组内条目（页面钮/子分组标题）。 */
    int                count;   /**< 组内条目数。 */
} kNavGroups[DEMO_NAV_CAT_N] = {
    { "\xE6\x8E\xA7\xE4\xBB\xB6",                                     /* 控件 */
      kNavControlsItems,
      (int)(sizeof(kNavControlsItems) / sizeof(kNavControlsItems[0])) },
    { "\xE7\xB3\xBB\xE7\xBB\x9F\xE8\xAE\xBE\xE7\xBD\xAE",             /* 系统设置 */
      kNavSystemItems,
      (int)(sizeof(kNavSystemItems) / sizeof(kNavSystemItems[0])) },
    { "\xE8\xBF\x9C\xE7\xA8\x8B",                                     /* 远程 */
      kNavRemoteItems,
      (int)(sizeof(kNavRemoteItems) / sizeof(kNavRemoteItems[0])) }
};

/** @brief 页索引 → 分类下标（切页时手风琴展开所在分类）。 */
static int demo_navCategoryForPage(int page)
{
    int c;
    int i;
    for (c = 0; c < DEMO_NAV_CAT_N; ++c)
        for (i = 0; i < kNavGroups[c].count; ++i)
            if (kNavGroups[c].items[i].page == page)
                return c;
    return 0;
}

#if XWIDGET_ON && XABSTRACTBUTTON_ON && XPUSHBUTTON_ON && XFRAME_ON && \
    XLABEL_ON && XLAYOUT_ON && XLAYOUT_STACKED_ON
/** @brief 按停靠边/收起态/展开分类重排导航面板几何与子钮。
 * @details 面板为 m_base 子控件（z 顶）；占位条带尺寸与
 *          demo_layout_content 的避让口径同源——左右停靠占
 *          m_navWidth（分割条可拖）、上下占 m_navTBH，收起仅留 8px
 *          分割条细条（单击展开）。竖版（左/右）：一级分类钮纵列，
 *          活动分类条目展开（页面钮缩进 + 子分组标题行）；横版
 *          （上/下）：分类钮一行，活动分类页面钮换行铺放并按行数
 *          自动抬高面板高（下限钳位）。 */
static void demo_navUpdatePanel(DemoWin* self)
{
    int top;
    int w;
    int h;
    int pw;
    int ph;
    int px;
    int py;
    int i;
    int y;
    int rowsNeeded;
    if (!self) return;
    top = 40 + demo_sysbarH(self);
    w = XWidget_width(&self->m_base);
    h = XWidget_height(&self->m_base);
    if (w < 1) w = 1;
    if (h < 1) h = 1;
    if (self->m_navDock < 0 || self->m_navDock > 3)
        self->m_navDock = 0;
    if (self->m_navCategory < 0 || self->m_navCategory >= DEMO_NAV_CAT_N)
        self->m_navCategory = 0;
    /* 横版展开时按页面钮换行数自动抬高面板高（下限钳位；须在面板
     * 几何落位前定版，ph/py 随之取新值）。 */
    rowsNeeded = 1;
    if (self->m_navDock >= 2) {
        int x = 24;
        int p;
        int needed;
        for (p = 0; p < kNavGroups[self->m_navCategory].count; ++p) {
            const DemoNavItem* item =
                &kNavGroups[self->m_navCategory].items[p];
            int bwid;
            if (item->page < 0)
                continue; /* 横版不显示子分组标题。 */
            bwid = demo_utf8_chars(item->caption) * 17 + 12;
            if (bwid < 64) bwid = 64;
            if (x + bwid > w - 16 && x > 24) {
                x = 24;
                ++rowsNeeded;
            }
            x += bwid + 6;
        }
        needed = 62 + rowsNeeded * 28 + 8;
        if (needed > DEMO_NAV_TB_MAX) needed = DEMO_NAV_TB_MAX;
        if (self->m_navTBH < needed)
            self->m_navTBH = needed;
    }
    if (self->m_navDock <= 1) {           /* 左/右：竖版，占满标题栏以下 */
        pw = self->m_navWidth;
        ph = h - top - 26;
        px = (self->m_navDock == 0) ? 0 : w - pw;
        py = top;
    } else {                              /* 上/下：横版条带 */
        pw = w;
        ph = self->m_navTBH;
        px = 0;
        py = (self->m_navDock == 2) ? top : h - 26 - ph;
    }
    if (self->m_navCollapsed) {
        /* 收起：面板整隐，贴边 8px 分割条细条=展开把手（单击复原）。 */
        XWidget_hide(&self->m_navPanel);
        if (self->m_navDock == 0)
            XWidget_setGeometry(self->m_navSplit, 0, top, 8, h - top - 26);
        else if (self->m_navDock == 1)
            XWidget_setGeometry(self->m_navSplit, w - 8, top, 8, h - top - 26);
        else if (self->m_navDock == 2)
            XWidget_setGeometry(self->m_navSplit, 0, top, w, 8);
        else
            XWidget_setGeometry(self->m_navSplit, 0, h - 26 - 8, w, 8);
        XWidget_raise(self->m_navSplit);
        XWidget_show(self->m_navSplit);
    } else {
        XWidget_setGeometry(&self->m_navPanel, px, py, pw, ph);
        XWidget_raise(&self->m_navPanel);
        XWidget_show(&self->m_navPanel);
        if (self->m_navDock <= 1) {
            /* 竖版展开：标题行 + 纵列一级分类钮 + 活动分类条目展开
             * （页面钮缩进 16px；子分组标题行更窄更淡，仅分组提示）。 */
            int head;
            XWidget_setGeometry(&self->m_navTitle, 8, 5, 60, 22);
            XWidget_show(&self->m_navTitle);
            XPushButton_setText_2(&self->m_navDockBtn,
                                  "\xE6\x8D\xA2\xE8\xBE\xB9"); /* 换边 */
            XWidget_setGeometry(&self->m_navDockBtn, pw - 100, 5, 46, 22);
            XWidget_show(&self->m_navDockBtn);
            XPushButton_setText_2(&self->m_navCollapseBtn,
                                  "\xE6\x94\xB6\xE8\xB5\xB7"); /* 收起 */
            XWidget_setGeometry(&self->m_navCollapseBtn, pw - 52, 5, 44, 22);
            XWidget_show(&self->m_navCollapseBtn);
            y = 32;
            head = 0;
            for (i = 0; i < DEMO_NAV_CAT_N; ++i) {
                int p;
                XWidget_setGeometry(&self->m_navCatBtns[i], 8, y, pw - 16, 26);
                XWidget_show(&self->m_navCatBtns[i]);
                y += 28;
                if (i != self->m_navCategory)
                    continue;
                for (p = 0; p < kNavGroups[i].count; ++p) {
                    const DemoNavItem* item = &kNavGroups[i].items[p];
                    if (item->page < 0) {
                        /* 子分组标题行：标签池轮转，超出池容量按页面行。 */
                        if (head < DEMO_NAV_HEAD_N) {
                            XLabel* label = &self->m_navHeadLabels[head++];
                            XLabel_setText_2(label, item->caption);
                            XLabel_setTextPixelSize(label, 12);
                            XWidget_setGeometry((XWidget*)label, 10, y + 3,
                                                pw - 20, 14);
                            XWidget_show((XWidget*)label);
                        }
                        y += DEMO_NAV_HEAD_PITCH;
                        continue;
                    }
                    XWidget_setGeometry(&self->m_pageNav[item->page], 24, y,
                                        pw - 40, DEMO_NAV_PAGE_H);
                    XWidget_show(&self->m_pageNav[item->page]);
                    y += DEMO_NAV_PAGE_PITCH;
                }
            }
            /* 池内未用到的标题标签隐藏（分类切换后残留防串行）。 */
            for (i = head; i < DEMO_NAV_HEAD_N; ++i)
                XWidget_hide((XWidget*)&self->m_navHeadLabels[i]);
            for (i = 0; i < DEMO_NAV_PAGE_N; ++i) {
                if (demo_navCategoryForPage(i) != self->m_navCategory)
                    XWidget_hide(&self->m_pageNav[i]);
            }
        } else {
            /* 横版展开：标题行 + 一级分类钮一行 + 活动分类页面钮换行
             * 铺放（子分组标题省略——横版条带纵向空间有限；换行数已在
             * 函数头部预算并抬高面板高）。 */
            int bw = (pw - 16) / DEMO_NAV_CAT_N;
            int p;
            int x;
            int row;
            if (bw > 120) bw = 120;
            if (bw < 56) bw = 56;
            XWidget_setGeometry(&self->m_navTitle, 8, 4, 60, 22);
            XWidget_show(&self->m_navTitle);
            XPushButton_setText_2(&self->m_navDockBtn,
                                  "\xE6\x8D\xA2\xE8\xBE\xB9"); /* 换边 */
            XWidget_setGeometry(&self->m_navDockBtn, pw - 100, 4, 46, 22);
            XWidget_show(&self->m_navDockBtn);
            XPushButton_setText_2(&self->m_navCollapseBtn,
                                  "\xE6\x94\xB6\xE8\xB5\xB7"); /* 收起 */
            XWidget_setGeometry(&self->m_navCollapseBtn, pw - 52, 4, 44, 22);
            XWidget_show(&self->m_navCollapseBtn);
            for (i = 0; i < DEMO_NAV_CAT_N; ++i) {
                XWidget_setGeometry(&self->m_navCatBtns[i], 8 + i * bw, 30,
                                    bw - 4, 26);
                XWidget_show(&self->m_navCatBtns[i]);
            }
            for (i = 0; i < DEMO_NAV_HEAD_N; ++i)
                XWidget_hide((XWidget*)&self->m_navHeadLabels[i]);
            x = 24;
            row = 0;
            for (p = 0; p < kNavGroups[self->m_navCategory].count; ++p) {
                const DemoNavItem* item =
                    &kNavGroups[self->m_navCategory].items[p];
                int bwid;
                if (item->page < 0)
                    continue; /* 横版不显示子分组标题。 */
                bwid = demo_utf8_chars(item->caption) * 17 + 12;
                if (bwid < 64) bwid = 64;
                if (x + bwid > pw - 16 && x > 24) {
                    x = 24;
                    ++row;
                }
                XWidget_setGeometry(&self->m_pageNav[item->page], x,
                                    62 + row * 28, bwid, 26);
                XWidget_show(&self->m_pageNav[item->page]);
                x += bwid + 6;
            }
            for (i = 0; i < DEMO_NAV_PAGE_N; ++i) {
                if (demo_navCategoryForPage(i) != self->m_navCategory)
                    XWidget_hide(&self->m_pageNav[i]);
            }
        }
        /* 分割条贴面板内缘（拖动调尺寸/双击收起）。 */
        if (self->m_navDock == 0)
            XWidget_setGeometry(self->m_navSplit, px + pw, py, 5, ph);
        else if (self->m_navDock == 1)
            XWidget_setGeometry(self->m_navSplit, px - 5, py, 5, ph);
        else if (self->m_navDock == 2)
            XWidget_setGeometry(self->m_navSplit, 0, py + ph, w, 5);
        else
            XWidget_setGeometry(self->m_navSplit, 0, py - 5, w, 5);
        XWidget_raise(self->m_navSplit);
        XWidget_show(self->m_navSplit);
    }
    DemoSplitter_setState(self->m_navSplit, self->m_navDock,
                          self->m_navCollapsed);
    /* 分类钮选中态随展开分类同步（互斥组反选其余）。 */
    XAbstractButton_setChecked(
        (XAbstractButton*)&self->m_navCatBtns[self->m_navCategory], true);
    self->m_staticSceneDirty = true;
    demo_repaint(self);
    /* 导航 chrome 的 raise（resize 拖拽经 resizeEvent 逐帧进入本函数）
       会把弹出的屏幕键盘压回自己之下——子控件浮层形态的 Z 序只有弹出
       时刻一次 raise，绘制按子控件向量序，异步守护 tick（200ms）只救
       得了拖动结束、帧内必被下一次 chrome raise 再压回。收尾对已弹出
       面板同步抬回（帧内最后一步），使「chrome 低于输入面板」在每一
       帧成立。peek 不惰性创建单例：键盘从未启用的会话零开销。 */
    {
        XVirtualKeyboard* kb;
#if XGUI_PERFORMANCE_OVERLAY_ON && XFRAME_ON && XLABEL_ON
        /* 性能浮层随 chrome 同步抬层（2026-10-06 用户口径：悬浮窗层级
           高于导航面板/分割条）：本函数每次 chrome raise 都会把浮层压
           回之下，左上/左中等与导航重叠的预设位被按钮盖字（拖动起点
           的单次 raise 只保拖动期）。先抬浮层再抬已弹出键盘，帧内
           收尾恒成立「主窗内容 < 性能浮层 < 屏幕键盘」。 */
        XWidget_raise((XWidget*)&self->m_performanceOverlay);
#endif
        kb = XGuiApplication_virtualKeyboardPeek();
        if (kb && XVirtualKeyboard_popupVisible(kb))
            XWidget_raise((XWidget*)kb);
    }
}

/** @brief 分割条回调：查询受控尺寸（左右=面板宽，上下=面板高）。 */
static int demo_navSplitSizeFor(void* owner)
{
    DemoWin* self = (DemoWin*)owner;
    if (!self) return 0;
    return (self->m_navDock <= 1) ? self->m_navWidth : self->m_navTBH;
}

/** @brief 分割条回调：应用拖出的新尺寸（钳位后重排面板与内容区）。 */
static void demo_navSplitApplySize(void* owner, int size)
{
    DemoWin* self = (DemoWin*)owner;
    if (!self) return;
    if (self->m_navDock <= 1) {
        if (size < 140) size = 140;
        if (size > 380) size = 380;
        if (size == self->m_navWidth) return;
        self->m_navWidth = size;
    } else {
        if (size < 92) size = 92;
        if (size > 320) size = 320;
        if (size == self->m_navTBH) return;
        self->m_navTBH = size;
    }
    demo_navUpdatePanel(self);
    demo_layout_content(self);
}

/** @brief 分割条回调：双击收起 / 收起态单击展开。 */
static void demo_navSplitToggle(void* owner)
{
    DemoWin* self = (DemoWin*)owner;
    if (!self) return;
    self->m_navCollapsed = !self->m_navCollapsed;
    demo_navUpdatePanel(self);
    demo_layout_content(self);
}

/** @brief 分类钮点击（m_navCatGroup idClicked）：切换手风琴展开分类。 */
static void demo_navCatSlot(XObject* receiver, XVarList* args)
{
    DemoWin* self = (DemoWin*)receiver;
    if (!self || !args) return;
    XVarList_args_1(args, int, catId);
    if (catId < 0 || catId >= DEMO_NAV_CAT_N) return;
    self->m_navCategory = catId;
    demo_navUpdatePanel(self);
    demo_layout_content(self);
}

/** @brief 换边钮：停靠边循环 左→右→上→下。 */
static void demo_navDockSlot(XObject* receiver, XVarList* args)
{
    DemoWin* self = (DemoWin*)receiver;
    (void)args;
    if (!self) return;
    self->m_navDock = (self->m_navDock + 1) % 4;
    demo_navUpdatePanel(self);
    demo_layout_content(self);
}

/** @brief 收起/展开钮：完整面板 ⇔ 贴边细条。 */
static void demo_navCollapseSlot(XObject* receiver, XVarList* args)
{
    DemoWin* self = (DemoWin*)receiver;
    (void)args;
    if (!self) return;
    self->m_navCollapsed = !self->m_navCollapsed;
    demo_navUpdatePanel(self);
    demo_layout_content(self);
}
#endif /* 浮动导航面板门 */

#if XWIDGET_ON && XFRAME_ON && XLABEL_ON
/** @brief 按当前窗口尺寸更新标题栏/状态栏标签几何（resize 时调用）。
 * @details 标题栏基底位于 (0,0,w,40)、状态栏基底位于 (0,h-26,w,26)，
 *          文本标签必须跟随窗口尺寸重新定位，否则改变窗口大小后状态栏
 *          文本会与底部黑色矩形错位或超出可视区域。 */
static void demo_layout_chrome(DemoWin* self)
{
    int width;
    int height;
    int labelWidth;

    if (!self) return;
    width = XWidget_width(&self->m_base);
    height = XWidget_height(&self->m_base);
    if (width < 0) width = 0;
    if (height < 0) height = 0;
    labelWidth = width > 16 ? width - 16 : 0;
    XWidget_setGeometry((XWidget*)&self->m_titleLabel, 16,
                        demo_sysbarH(self),
                        labelWidth, 40);
    /* 状态栏是自带底色的整条子控件，几何必须覆盖整个状态条区域。 */
    if (height >= 26)
        XWidget_setGeometry((XWidget*)&self->m_statusLabel, 0,
                            height - 26, width, 26);
}
#endif /* XWIDGET_ON && XFRAME_ON && XLABEL_ON */

/** @brief 按当前窗口尺寸重新分配主内容区几何（切换页面/resize 时调用）。
 * @details 内容区=标题栏(40+系统栏)与状态栏(26)之间的完整区域再留 8px
 *          呼吸边，按浮动导航面板占位避让（左右停靠让宽、上下停靠让
 *          高、收起让细条，口径与 demo_navUpdatePanel 同源）——
 *          800x600 无面板时内容区 776x494，贴左展开时 600x494。
 *          各页面内部布局全部按本函数给定的根几何自适应摆位。 */
static void demo_layout_content(DemoWin* self)
{
    XRect content;
    int width;
    int height;
    int top;
    int left = 12;
    int right = 12;
    int bottom = 28;
    int contentWidth;
    int contentHeight;
    if (!self) return;
    width = XWidget_width(&self->m_base);
    height = XWidget_height(&self->m_base);
    top = 40 + demo_sysbarH(self);
#if XWIDGET_ON && XABSTRACTBUTTON_ON && XPUSHBUTTON_ON && XFRAME_ON && \
    XLABEL_ON && XLAYOUT_ON && XLAYOUT_STACKED_ON
    {
        /* 浮动导航面板占位避让（三态：展开/收起细条/停靠边）。 */
        int navW = self->m_navCollapsed
                       ? DEMO_NAV_STRIP_W
                       : (self->m_navDock <= 1 ? self->m_navWidth : 0);
        int navH = self->m_navCollapsed
                       ? DEMO_NAV_STRIP_W
                       : (self->m_navDock >= 2 ? self->m_navTBH : 0);
        if (self->m_navDock == 0) left += navW;
        if (self->m_navDock == 1) right += navW;
        if (self->m_navDock == 2) top += navH;
        if (self->m_navDock == 3) bottom += navH;
    }
#endif
    contentWidth = width - left - right;
    contentHeight = height - top - 8 - bottom;
    if (contentWidth < 0) contentWidth = 0;
    if (contentHeight < 0) contentHeight = 0;
    XRect_init(&content, left, top + 8, contentWidth, contentHeight);
    XLayoutItem_setGeometry_base((XLayoutItem*)&self->m_stackLayout,
                                 &content);
#if XWIDGET_ON && XGROUPBOX_ON && XLINEEDIT_ON && XSPINBOX_ON && \
    XABSTRACTSLIDER_ON && XSLIDER_ON && XPROGRESSBAR_ON
    /* 页面 3：输入控件族（2026-10-03 选项卡打散归位）——左列 GroupBox
     * 四件（单行/微调/滑块/进度）+ 多行编辑，右列下拉/字体/旋钮/数码管/
     * 滚动条/日期时间族；两列宽随内容区自适应，小窗口不溢出。 */
    {
        int colW = contentWidth / 2 - 16;
        int rx;
        int rw;
        if (colW < 240) colW = 240;
        if (colW > 320) colW = 320;
        rx = 12 + colW + 16;
        /* 右列宽以页面根（contentWidth）右缘反推：页面几何是页面局部
           坐标，基准必须与根宽同源——首轮收口误用窗口宽 width（含导航
           面板占位），贴左面板时右列溢出根 188px（目验二轮 page3 字体
           框截断根因）。 */
        rw = contentWidth - rx - 4;
        if (rw < 200) rw = 200;
        XWidget_setGeometry((XWidget*)&self->m_groupBox,
                            12, 8, colW, 210);
        {
            XRect inner = XGroupBox_contentsRect(&self->m_groupBox);
            int innerW = inner.width - 24;
            if (innerW < 80) innerW = 80;
            XWidget_setGeometry((XWidget*)&self->m_lineEdit,
                                inner.x + 12, inner.y + 8, innerW, 26);
            XWidget_setGeometry((XWidget*)&self->m_spinBox,
                                inner.x + 12, inner.y + 48, innerW, 26);
            XWidget_setGeometry((XWidget*)&self->m_slider,
                                inner.x + 12, inner.y + 92, innerW, 28);
            XWidget_setGeometry((XWidget*)&self->m_progressBar,
                                inner.x + 12, inner.y + 138, innerW, 24);
        }
#if XWIDGET_ON && XPLAINTEXTEDIT_ON
        /* 多行编辑填充左列余下高度。 */
        XWidget_setGeometry((XWidget*)&self->m_plainEdit,
                            12, 226, colW,
                            contentHeight - 226 - 34 > 80
                                ? contentHeight - 226 - 34 : 80);
#endif
#if XWIDGET_ON && XCOMBOBOX_ON && XABSTRACTSLIDER_ON && XDIAL_ON && \
    XPROGRESSBAR_ON && XLCDNUMBER_ON && XSCROLLBAR_ON
        /* 右列：下拉/字体行、旋钮联动行、数码管/滚动条行。 */
        XWidget_setGeometry((XWidget*)&self->m_comboBox, rx, 8, 150, 26);
        XWidget_setGeometry((XWidget*)&self->m_fontCombo, rx + 158, 8,
                            rw - 158, 28);
        XWidget_setGeometry((XWidget*)&self->m_dial, rx, 44, 60, 60);
        XWidget_setGeometry((XWidget*)&self->m_dialProgress,
                            rx + 70, 62, rw - 70, 20);
        XWidget_setGeometry((XWidget*)&self->m_lcd, rx, 112, 160, 60);
        XWidget_setGeometry((XWidget*)&self->m_scrollBar,
                            rx + 170, 112, 24, 64);
#endif
#if XWIDGET_ON && XDATETIMEEDIT_ON && XDATEEDIT_ON && XTIMEEDIT_ON
        /* 右列：日期时间三件套。 */
        XWidget_setGeometry((XWidget*)&self->m_dtEdit, rx, 184, rw, 28);
        XWidget_setGeometry((XWidget*)&self->m_dateEdit, rx, 220, rw, 28);
        XWidget_setGeometry((XWidget*)&self->m_timeEdit, rx, 256, rw, 28);
#endif
        /* 状态行贴本页底部（宽度随内容区）。 */
        XWidget_setGeometry((XWidget*)&self->m_inputStatus,
                            12, contentHeight - 30, contentWidth - 12, 24);
    }
#endif
#if XWIDGET_ON && XTABWIDGET_ON && XTABBAR_ON && XCOMBOBOX_ON && \
    XABSTRACTSLIDER_ON && XDIAL_ON && XPROGRESSBAR_ON
    {
        /* 页面 4：容器与窗口（选项卡瘦身后 9 签）——页签容器填满页。 */
        int w4 = contentWidth - 24;
        if (w4 < 120) w4 = 120;
        XWidget_setGeometry((XWidget*)&self->m_tabWidget,
                            12, 8, w4, contentHeight - 40);
        XWidget_setGeometry((XWidget*)&self->m_tabStatus,
                            12, 8 + contentHeight - 40 + 8,
                            contentWidth, 24);
    }
#endif
#if XWIDGET_ON && XCHARTS_ON && XPUSHBUTTON_ON
    /* 页面 12：图表演示（2026-10-03 自选项卡页独立）——切换钮行顶置，
     * 图表视图填充其余全部区域，随窗口缩放。 */
    {
        int bw = (contentWidth - 24 - 4 * 8) / 5;
        int i;
        if (bw > 84) bw = 84;
        if (bw < 48) bw = 48;
        for (i = 0; i < 5; ++i) {
            XPushButton* b = (i == 0) ? &self->m_btnLegend
                          : (i == 1) ? &self->m_btnGrid
                          : (i == 2) ? &self->m_btnTitle
                          : (i == 3) ? &self->m_btnSeries
                                     : &self->m_btnRange;
            XWidget_setGeometry((XWidget*)b, 12 + i * (bw + 8), 8, bw, 24);
        }
        /* 图表铺满；FPS 悬浮层已改自由拖动（见 demo_performance_init），
           与图表重叠由用户拖避（2026-10-03 用户质问「这边为啥要空着」：
           撤销右缘 218px HUD 让位带，图表视图恢复铺满内容区）。 */
        XWidget_setGeometry((XWidget*)&self->m_chartView,
                            12, 38, contentWidth - 24, contentHeight - 46);
    }
#endif
#if XWIDGET_ON && XTABLEWIDGET_ON && XLAYOUT_ON && XLAYOUT_STACKED_ON
    /* 条目视图页补充大表格（自选项卡页打散归位）：贴小表格右侧，
       宽随页面根自适应且右缘收口在 rootW-8（目验 page5 挂项：类型列
       在窗口右缘被裁的根因=宽度公式溢出收口）；行高让底部横滚条。 */
    if (self->m_extPages[0]) {
        int rootW = XWidget_width(self->m_extPages[0]);
        int rootH = XWidget_height(self->m_extPages[0]);
        /* 双档位摆位：宽根（rootW≥652=404+4 列最小 240+边 8，面板收
           起/无面板）贴小表格右侧同行；窄根（面板展开 rootW≈600，
           404 起只剩 ~196 宽装不下 4 列）下移到小表格下方占满整行
           （小表格底 306 → 大表格 314..，状态行之上）——四轮目验：
           窄根挤列只够名称列，正解是换行铺满而非挤列。 */
        int tx;
        int tyy;
        int tw;
        int th = 124;
        if (rootW >= 652) {
            tx = 404;
            tyy = 182;
            tw = rootW - 404 - 8;
            if (tw < 120) tw = 120;
        } else {
            tx = 8;
            tyy = 314;
            tw = rootW - 16;
            th = rootH - 314 - 40;
            if (th < 100) th = 100;
            if (tyy + th > rootH - 36) th = rootH - 36 - tyy;
        }
        XWidget_setGeometry((XWidget*)&self->m_tableWidget,
                            tx, tyy, tw, th);
        /* 列宽随可视宽（表格宽−纵条带 12）分配：宽根全 4 列恰满，
           窄根保「名称+类型」主列、其余列横滚可达。 */
        {
            int view = tw - 12;
            if (view >= 372) {
                XTableView_setColumnWidth((XTableView*)&self->m_tableWidget,
                                          0, 128);
                XTableView_setColumnWidth((XTableView*)&self->m_tableWidget,
                                          1, 72);
                XTableView_setColumnWidth((XTableView*)&self->m_tableWidget,
                                          2, 60);
                XTableView_setColumnWidth((XTableView*)&self->m_tableWidget,
                                          3, 112);
            } else if (view >= 200) {
                int c0 = view * 62 / 100;
                XTableView_setColumnWidth((XTableView*)&self->m_tableWidget,
                                          0, c0);
                XTableView_setColumnWidth((XTableView*)&self->m_tableWidget,
                                          1, view - c0);
                XTableView_setColumnWidth((XTableView*)&self->m_tableWidget,
                                          2, 60);
                XTableView_setColumnWidth((XTableView*)&self->m_tableWidget,
                                          3, 112);
            }
        }
    }
#endif
    /* 扩展页自适应重排（xgui_demo_pages.h 契约）：内容区几何随 CSD
     * 系统栏/窗口尺寸/导航面板占位变化，各页按当前根几何重排——本函数
     * 在切页/resize/startup/停靠切换全路径执行；登记表下标=页索引-5
     * （0=条目视图 2=高级 3=效果 6=远程客户端 8=网络设置 9=悬浮窗
     * 设置，见 kExtBuilders 顺序）。 */
    if (self->m_extPages[0])
        demo_page_views_adapt(self->m_extPages[0]);
    if (self->m_extPages[2])
        demo_page_advanced_adapt(self->m_extPages[2]);
    if (self->m_extPages[3])
        demo_page_effects_adapt(self->m_extPages[3]);
#if defined(XGUI_REMOTE_ON) && XGUI_REMOTE_ON
    if (self->m_extPages[6])
        demo_page_remote_client_adapt(self->m_extPages[6]);
#endif
    if (self->m_extPages[8])
        demo_page_network_adapt(self->m_extPages[8]);
    if (self->m_extPages[9])
        demo_page_overlay_settings_adapt(self->m_extPages[9]);
    /* 远程服务器页（2026-10-06 补入统一漏斗）：此前只在 resizeEvent
     * 单独调用，切页路径漏跑——XStackedLayout StackOne 只给当前页
     * 分配几何，resize 落在别的页时本页根保持陈旧尺寸，adapt 算出
     * 全页错位（用户实测：服务地址行 95px 窄条悬页中）。本函数先
     * 重贴堆叠几何（上方 setGeometry_base）再跑 adapt，切页/resize/
     * startup/停靠全路径根尺寸都新鲜。 */
    demo_page_remote_server_adaptWidth();
}

/** @brief 切换主内容页面：更新堆叠布局当前页、重新分配几何并更新状态栏。 */
static void demo_switchPage(DemoWin* self, int index)
{
    if (!self) return;
#if XWIDGET_ON && XKEYBOARD_ON
    /* 虚拟键盘 autoPopup 随页切换（2026-10-03 用户反馈键盘页点不弹的
       根因收口）：RC 页曾把单例 autoPopup 全局关断，离开 RC 页后键盘
       页等其余页面点击不弹。改为进入远程客户端页（index==11）时关断
       （RC 页触摸点击不被屏幕键盘打断，唯一键盘入口=悬浮会话工具条
       「键盘」钮），离开时恢复。关断/恢复取 oldIndex 在 setCurrentIndex
       之前执行（XStackedLayout_currentIndex 读旧值）。单例惰性创建，
       virtualKeyboard() 判空（裁剪配置=0 单例返回 NULL 自然空操作）。 */
    {
        int oldIndex = XStackedLayout_currentIndex(&self->m_stackLayout);
        if (oldIndex != 11 && index == 11) {
            XVirtualKeyboard* kb = XGuiApplication_virtualKeyboard();
            if (kb) XVirtualKeyboard_setAutoPopup(kb, false);
        } else if (oldIndex == 11 && index != 11) {
            XVirtualKeyboard* kb = XGuiApplication_virtualKeyboard();
            if (kb) XVirtualKeyboard_setAutoPopup(kb, true);
        }
    }
#endif
    if (index < 0) index = 0;
    if (index > 14) index = 14; /* 2026-10-06: 第 13/14 页(网络/悬浮窗设置)入列。 */
#if XWIDGET_ON && XABSTRACTBUTTON_ON && XPUSHBUTTON_ON && XFRAME_ON && \
    XLABEL_ON && XLAYOUT_ON && XLAYOUT_STACKED_ON
    /* 导航面板手风琴随页展开所在分类（程序化切页 --page/autotest 与
       点击切页同口径；面板重排先于内容区避让重算）。 */
    if (self->m_navCategory != demo_navCategoryForPage(index)) {
        self->m_navCategory = demo_navCategoryForPage(index);
        demo_navUpdatePanel(self);
    }
#endif
    XStackedLayout_setCurrentIndex(&self->m_stackLayout, index);
#if XBUTTONGROUP_ON && XWIDGET_ON && XABSTRACTBUTTON_ON && XPUSHBUTTON_ON
    /* 导航钮选中态随页同步：点击切页（clicked 槽走此处，重设同值幂等）
       与程序化切页（--page / autotest 调度 / 截图门）共用同一口径；
       互斥组自动反选其余成员。 */
    XAbstractButton_setChecked((XAbstractButton*)&self->m_pageNav[index], true);
#endif
    /* XStackedLayout 的 setGeometry 只给当前页面分配几何；切换后必须
       重新分配，否则新页面容器保持 0x0 导致页面内容不可见。 */
    demo_layout_content(self);
    /* 切页后标脏静态场景缓存并整页重绘：切页只投悬浮层小脏区时，旧页
       内容留在后备缓冲（新页控件未覆盖处露出旧像素=叠印，2026-10-03
       目验 page7 多组文字重影根因）；静态场景缓存关（默认 0）时同样
       需要整页刷新。 */
    self->m_staticSceneDirty = true;
    {
        XRect full;
        XRect_init(&full, 0, 0, XWidget_width(&self->m_base),
                   XWidget_height(&self->m_base));
        XWidget_updateRect(&self->m_base, &full);
    }
    XPrintf("XGuiWindowDemo: switch page=%d (%s)\n", index,
            demo_page_name(index));
    demo_set_status(self, demo_page_name(index));
}
#endif /* XWIDGET_ON && XLAYOUT_ON && XLAYOUT_STACKED_ON */

#if XCHARTS_ON
/** @brief 图表：图例开关槽。 */
static void demo_chartLegendSlot(XObject* receiver, XVarList* args)
{
    DemoWin* self = (DemoWin*)receiver;
    XChart* chart;
    (void)args;
    if (!self) return;
    chart = XChartView_chart(&self->m_chartView);
    if (!chart) return;
    XChart_setLegendVisible(chart, !XChart_isLegendVisible(chart));
    XChartView_updateChart(&self->m_chartView);
    demo_set_status(self, "图表: 图例切换");
}

/** @brief 图表：网格开关槽。 */
static void demo_chartGridSlot(XObject* receiver, XVarList* args)
{
    DemoWin* self = (DemoWin*)receiver;
    XChart* chart;
    (void)args;
    if (!self) return;
    chart = XChartView_chart(&self->m_chartView);
    if (!chart) return;
    /* 网格开关双轴联动：只切 axisY 会让从未被切过的 axisX 竖线在前后对比中
       像「网格自行恢复」（第一轮台账 #52 复核结论）。 */
    if (chart->m_axisX) {
        XValueAxis_setGridVisible(chart->m_axisX,
                                  !XValueAxis_isGridVisible(chart->m_axisX));
    }
    if (chart->m_axisY) {
        XValueAxis_setGridVisible(chart->m_axisY,
                                  !XValueAxis_isGridVisible(chart->m_axisY));
    }
    XChartView_updateChart(&self->m_chartView);
    demo_set_status(self, "图表: 网格切换");
}

/** @brief 图表：标题开关槽。 */
static void demo_chartTitleSlot(XObject* receiver, XVarList* args)
{
    DemoWin* self = (DemoWin*)receiver;
    XChart* chart;
    (void)args;
    if (!self) return;
    chart = XChartView_chart(&self->m_chartView);
    if (!chart) return;
    XChart_setTitleVisible(chart, !XChart_isTitleVisible(chart));
    XChartView_updateChart(&self->m_chartView);
    demo_set_status(self, "图表: 标题切换");
}

/** @brief 图表：序列显示模式循环（全部/折线/柱状/散点/面积/样条）。 */
static void demo_chartSeriesSlot(XObject* receiver, XVarList* args)
{
    DemoWin* self = (DemoWin*)receiver;
    XChart* chart;
    int i;
    (void)args;
    if (!self) return;
    chart = XChartView_chart(&self->m_chartView);
    if (!chart) return;
    self->m_chartSeriesMode = (self->m_chartSeriesMode + 1) % 6;
    for (i = 0; i < chart->m_lineCount; ++i)
        XAbstractSeries_setVisible((XAbstractSeries*)&chart->m_lineSeries[i]->m_base,
            (self->m_chartSeriesMode == 0 || self->m_chartSeriesMode == 1));
    for (i = 0; i < chart->m_barCount; ++i)
        XAbstractSeries_setVisible((XAbstractSeries*)&chart->m_barSeries[i]->m_base,
            (self->m_chartSeriesMode == 0 || self->m_chartSeriesMode == 2));
    for (i = 0; i < chart->m_scatterCount; ++i)
        XAbstractSeries_setVisible((XAbstractSeries*)&chart->m_scatterSeries[i]->m_base,
            (self->m_chartSeriesMode == 0 || self->m_chartSeriesMode == 3));
    for (i = 0; i < chart->m_areaCount; ++i)
        XAbstractSeries_setVisible((XAbstractSeries*)&chart->m_areaSeries[i]->m_base,
            (self->m_chartSeriesMode == 0 || self->m_chartSeriesMode == 4));
    for (i = 0; i < chart->m_splineCount; ++i)
        XAbstractSeries_setVisible((XAbstractSeries*)&chart->m_splineSeries[i]->m_base,
            (self->m_chartSeriesMode == 0 || self->m_chartSeriesMode == 5));
    XChartView_updateChart(&self->m_chartView);
    demo_set_status(self, "图表: 序列模式切换");
}

/** @brief 图表：Y 轴范围切换槽。 */
static void demo_chartRangeSlot(XObject* receiver, XVarList* args)
{
    DemoWin* self = (DemoWin*)receiver;
    XChart* chart;
    (void)args;
    if (!self) return;
    chart = XChartView_chart(&self->m_chartView);
    if (!chart || !chart->m_axisY) return;
    self->m_chartRange = (self->m_chartRange + 1) % 2;
    if (self->m_chartRange == 0)
        XValueAxis_setRange(chart->m_axisY, 0, 60);
    else
        XValueAxis_setRange(chart->m_axisY, 0, 30);
    XChartView_updateChart(&self->m_chartView);
    demo_set_status(self, "图表: 范围切换");
}
#endif /* XCHARTS_ON */

/** @brief 把紧凑内容控件包进页容器（页容器铺满 tab 页，内容保持
 *         自身几何；对标 Qt 页容器+布局的分层语义）。返回页容器。 */
static XWidget* demo_wrapTabPage(DemoWin* self, XWidget* content)
{
    XWidget* page;
    if (!self || !content) return content;
    page = (XWidget*)XMemory_malloc(sizeof(XWidget),
                                    XCLASS_DEFAULT_MEMORY_TYPE);
    if (!page) return content;
    XWidget_init(page, (XWidget*)&self->m_tabWidget, 0);
    Set_Class_Memory(page, XCLASS_DEFAULT_MEMORY_TYPE);
    Set_Class_IsHeap(page, true);
    XWidget_setParent(content, page, 0);
    XWidget_show(content);
    XWidget_show(page);
    return page;
}


#if XWIDGET_ON && XPUSHBUTTON_ON && XLAYOUT_ON && XLAYOUT_STACKED_ON
/** @brief 页面 1（按钮演示）导航按钮 clicked 槽。 */
static void demo_nav0Slot(XObject* receiver, XVarList* args)
{
    (void)args;
    demo_switchPage((DemoWin*)receiver, 0);
}
/** @brief 页面 2（选择演示）导航按钮 clicked 槽。 */
static void demo_nav1Slot(XObject* receiver, XVarList* args)
{
    (void)args;
    demo_switchPage((DemoWin*)receiver, 1);
}
/** @brief 页面 3（堆叠演示）导航按钮 clicked 槽。 */
static void demo_nav2Slot(XObject* receiver, XVarList* args)
{
    (void)args;
    demo_switchPage((DemoWin*)receiver, 2);
}
/** @brief 页面 4（输入演示）导航按钮 clicked 槽。 */
static void demo_nav3Slot(XObject* receiver, XVarList* args)
{
    (void)args;
    demo_switchPage((DemoWin*)receiver, 3);
}
/** @brief 页面 5（选项卡演示）导航按钮 clicked 槽。 */
static void demo_nav4Slot(XObject* receiver, XVarList* args)
{
    (void)args;
    demo_switchPage((DemoWin*)receiver, 4);
}
/** @brief 页面 6（条目视图）导航按钮 clicked 槽。 */
static void demo_nav5Slot(XObject* receiver, XVarList* args)
{
    (void)args;
    demo_switchPage((DemoWin*)receiver, 5);
}
/** @brief 页面 7（对话框）导航按钮 clicked 槽。 */
static void demo_nav6Slot(XObject* receiver, XVarList* args)
{
    (void)args;
    demo_switchPage((DemoWin*)receiver, 6);
}
/** @brief 页面 8（高级控件）导航按钮 clicked 槽。 */
static void demo_nav7Slot(XObject* receiver, XVarList* args)
{
    (void)args;
    demo_switchPage((DemoWin*)receiver, 7);
}

/** @brief 页面 9（图形效果）导航按钮 clicked 槽。 */
static void demo_nav8Slot(XObject* receiver, XVarList* args)
{
    (void)args;
    demo_switchPage((DemoWin*)receiver, 8);
}
/** @brief 页面 10（屏幕键盘）导航按钮 clicked 槽。 */
static void demo_nav9Slot(XObject* receiver, XVarList* args)
{
    (void)args;
    demo_switchPage((DemoWin*)receiver, 9);
}
/** @brief 页面 11（远程窗口）导航按钮 clicked 槽。 */
static void demo_nav10Slot(XObject* receiver, XVarList* args)
{
    (void)args;
    demo_switchPage((DemoWin*)receiver, 10);
}
/** @brief 页面 12（远程客户端）导航按钮 clicked 槽（2026-10-02 追加）。 */
static void demo_nav11Slot(XObject* receiver, XVarList* args)
{
    (void)args;
    demo_switchPage((DemoWin*)receiver, 11);
}

/** @brief 页面 12（图表演示）导航槽。 */
static void demo_nav12Slot(XObject* receiver, XVarList* args)
{
    (void)args;
    demo_switchPage((DemoWin*)receiver, 12);
}

/** @brief 页面 13（网络设置）导航槽（2026-10-06 系统设置入列）。 */
static void demo_nav13Slot(XObject* receiver, XVarList* args)
{
    (void)args;
    demo_switchPage((DemoWin*)receiver, 13);
}

/** @brief 页面 14（悬浮窗设置）导航槽（2026-10-06 系统设置入列）。 */
static void demo_nav14Slot(XObject* receiver, XVarList* args)
{
    (void)args;
    demo_switchPage((DemoWin*)receiver, 14);
}

/** @brief 扩展页状态回调：转发到主窗状态栏（xgui_demo_pages.h 契约适配）。 */
static void demo_ext_page_status(void* user, const char* text)
{
    demo_set_status((DemoWin*)user, text);
}
#endif /* XWIDGET_ON && XPUSHBUTTON_ON && XLAYOUT_ON && XLAYOUT_STACKED_ON */

/* ==================== 悬浮窗设置页访问器（xgui_demo_pages.h 契约） ====
 * 文件级符号（页 TU 无条件链接），任何配置下都提供定义：悬浮层编译
 * 开关关闭或控件段被裁剪时走空桩（设置页自身会因 build 返回 NULL 而
 * 被主文件跳过注册，两道防线语义一致）。 */

#if XGUI_PERFORMANCE_OVERLAY_ON && XWIDGET_ON && XFRAME_ON && XLABEL_ON
void* demo_main_overlay(void* user)
{
    DemoWin* win = (DemoWin*)user;
    if (!win) return NULL;
    return (void*)&win->m_performanceOverlay;
}

void demo_main_overlay_applyPreset(void* user, int preset)
{
    DemoWin* win = (DemoWin*)user;
    if (!win) return;
    /* 用户显式定位：非右下预设挂起 resize 自动重锚右下角；预设回右下
       （或复位）即恢复自动重锚。锚定尺寸随之刷新，避免 paint 差检测
       立刻抢回旧锚。 */
    win->m_overlayPinned =
        (preset != (int)XPerformanceOverlayPosition_BottomRight);
    XPerformanceOverlay_setPresetPosition(&win->m_performanceOverlay,
                                          (XPerformanceOverlayPosition)preset,
                                          XWidget_width(&win->m_base),
                                          XWidget_height(&win->m_base), 8);
    /* 预设收进内容区（2026-10-06 用户目验：左上预设整块压在标题栏上）。
       setPresetPosition 是纯窗口相对九宫格，不感知 demo 框架 chrome——
       顶部系统栏带（demo_sysbarH）+标题栏 40、底部状态栏 26。这里按
       右下自动重锚（demo_performance_anchorBottomRight）的同一避让
       口径把结果钳回内容区，九宫格角语义不变（左上=内容区左上…）；
       右下项走自动重锚，本钳制对其无感。 */
    {
        XRect geo;
        int top;
        int bottomLimit;
        int x;
        int y;
        geo = XPerformanceOverlay_geometry(&win->m_performanceOverlay);
        top = demo_sysbarH(win) + 40 + 8;
        bottomLimit = XWidget_height(&win->m_base) - 26 - 8;
        x = geo.x;
        y = geo.y;
        if (x < 8) x = 8;
        if (x > XWidget_width(&win->m_base) - 8 - geo.width)
            x = XWidget_width(&win->m_base) - 8 - geo.width;
        if (x < 0) x = 0;
        if (y > bottomLimit - geo.height) y = bottomLimit - geo.height;
        if (y < top) y = top; /* 内容区过矮时保顶不保底 */
        if (x != geo.x || y != geo.y)
            XPerformanceOverlay_setPosition(&win->m_performanceOverlay, x, y);
    }
    win->m_overlayAnchorW =
        XPerformanceOverlay_geometry(&win->m_performanceOverlay).width;
    win->m_overlayAnchorH =
        XPerformanceOverlay_geometry(&win->m_performanceOverlay).height;
    demo_repaint(win);
}
#else
void* demo_main_overlay(void* user)
{
    (void)user;
    return NULL;
}

void demo_main_overlay_applyPreset(void* user, int preset)
{
    (void)user; (void)preset;
}
#endif /* XGUI_PERFORMANCE_OVERLAY_ON && ... */

#if XWIDGET_ON && XGROUPBOX_ON && XLINEEDIT_ON && XSPINBOX_ON && \
    XABSTRACTSLIDER_ON && XSLIDER_ON && XPROGRESSBAR_ON
/** @brief 单行输入文本变化：状态行反馈（可视化行为验证）。 */
static void demo_input_textChangedSlot(XObject* receiver, XVarList* args)
{
    DemoWin* self = (DemoWin*)receiver;
    const char* text;
    char buf[160];
    XVarList_args_1(args, const char*, t);
    if (!self) return;
    text = t ? t : "";
    snprintf(buf, sizeof(buf), "\xE6\x96\x87\xE6\x9C\xAC: %s", text); /* 文本: */
    XLabel_setText_2(&self->m_inputStatus, buf);
    /* 标签文本更新走 XLabel 自身的整块 update:paintTree 逐矩形拆分后,
       根静态 tile 只覆盖标签脏区并恢复背景,标签自绘新文本(14.122
       根修后不再需要全窗标脏的临时缓解)。 */

    demo_set_status(self, buf);
}
/** @brief 微调框数值变化：同步滑块与进度条。 */
static void demo_input_spinChangedSlot(XObject* receiver, XVarList* args)
{
    DemoWin* self = (DemoWin*)receiver;
    int value;
    XVarList_args_1(args, int, v);
    if (!self) return;
    value = v;
    XAbstractSlider_setValue((XAbstractSlider*)&self->m_slider, value);
    XProgressBar_setValue(&self->m_progressBar, value);
}
/** @brief 滑块数值变化：同步微调框与进度条 + 状态行反馈。 */
static void demo_input_sliderChangedSlot(XObject* receiver, XVarList* args)
{
    DemoWin* self = (DemoWin*)receiver;
    int value;
    char buf[64];
    XVarList_args_1(args, int, v);
    if (!self) return;
    value = v;
    XSpinBox_setValue(&self->m_spinBox, value);
    XProgressBar_setValue(&self->m_progressBar, value);
    snprintf(buf, sizeof(buf), "\xE6\xBB\x91\xE5\x9D\x97: %d", value); /* 滑块: */
    XLabel_setText_2(&self->m_inputStatus, buf);
    /* 同 textChanged 槽:静态场景根修(14.122/14.123 ④)后标签更新
       走自身整块 update,无需全窗标脏。 */
}
#endif /* 输入演示联动槽 */

#if XWIDGET_ON && XTABWIDGET_ON && XTABBAR_ON && XCOMBOBOX_ON && \
    XABSTRACTSLIDER_ON && XDIAL_ON && XPROGRESSBAR_ON && XFRAME_ON && XLABEL_ON
/** @brief 下拉框选择：状态行反馈。 */
static void demo_tab_comboSlot(XObject* receiver, XVarList* args)
{
    DemoWin* self = (DemoWin*)receiver;
    const char* text = "";
    char buf[160];
    XVarList_args_1(args, const char*, t);
    if (!self) return;
    text = t ? t : "";
    snprintf(buf, sizeof(buf), "\xE4\xB8\x8B\xE6\x8B\x89: %s", text); /* 下拉: */
    XLabel_setText_2(&self->m_tabStatus, buf);
}
/** @brief 旋钮值变化：同步进度条 + 状态行。 */
static void demo_tab_dialSlot(XObject* receiver, XVarList* args)
{
    DemoWin* self = (DemoWin*)receiver;
    int value;
    char buf[64];
    XVarList_args_1(args, int, v);
    if (!self) return;
    value = v;
    XProgressBar_setValue(&self->m_dialProgress, value);
    snprintf(buf, sizeof(buf), "\xE6\x97\x8B\xE9\x92\xAE: %d", value); /* 旋钮: */
    XLabel_setText_2(&self->m_tabStatus, buf);
}
/** @brief 选项卡切换：状态行反馈。 */
static void demo_tab_changedSlot(XObject* receiver, XVarList* args)
{
    DemoWin* self = (DemoWin*)receiver;
    int index;
    char buf[64];
    XVarList_args_1(args, int, i);
    if (!self) return;
    index = i;
    snprintf(buf, sizeof(buf), "\xE9\xA1\xB5\xE7\xAD\xBE: %d", index); /* 页签: */
    XLabel_setText_2(&self->m_tabStatus, buf);
}
#endif /* 选项卡演示联动槽 */

#if XWIDGET_ON && XPUSHBUTTON_ON
/** @brief 页面 1 常驻按钮 pressed 信号槽：更新联动标签与状态栏。 */
static void demo_button_pressedSlot(XObject* receiver, XVarList* args)
{
    DemoWin* self = (DemoWin*)receiver;
    (void)args;
    if (!self) return;
#if XWIDGET_ON && XFRAME_ON && XLABEL_ON
    XLabel_setText_2(&self->m_linkLabel, "按钮：按下");
#endif
    demo_set_status(self, "按钮：按下");
}

/** @brief 页面 1 常驻按钮 released 信号槽：更新联动标签与状态栏。 */
static void demo_button_releasedSlot(XObject* receiver, XVarList* args)
{
    DemoWin* self = (DemoWin*)receiver;
    (void)args;
    if (!self) return;
#if XWIDGET_ON && XFRAME_ON && XLABEL_ON
    XLabel_setText_2(&self->m_linkLabel, "按钮：释放");
#endif
    demo_set_status(self, "按钮：释放");
}

#if XWIDGET_ON && XSTATUSBAR_ON && XABSTRACTBUTTON_ON && XPUSHBUTTON_ON && \
    XWIZARD_ON && XERRORMESSAGE_ON
/** @brief 页签 8「向导错误」启动器：点击弹出向导（弹窗化，见页签 8
 *         装配段——XWizard 是 XDialog 派生顶层窗，不再启动即显）。
 * @note  对标 xgui_demo_page_advanced.c adv_btnMainWindowSlot 的居中
 *        公式：x = 父x + (父w - 480) / 2、y = 父y + (父h - 320) / 2
 *        （480x320 = XWizard_init 内置 resize，见 XWizard.c），
 *        负值钳 0；show 后 activateWindow 抢焦点。 */
static void demo_wizOpenSlot(XObject* receiver, XVarList* args)
{
    DemoWin* self = (DemoWin*)receiver;
    XWidget* parent;
    int x;
    int y;
    (void)args;
    if (!self) return;
    parent = XWidget_topLevelWidget((XWidget*)self); /* 演示主窗（自引用安全）。 */
    if (parent) {
        x = XWidget_x(parent) + (XWidget_width(parent) - 480) / 2;
        y = XWidget_y(parent) + (XWidget_height(parent) - 320) / 2;
        if (x < 0) x = 0;
        if (y < 0) y = 0;
        XWidget_move((XWidget*)&self->m_wizard, x, y);
    }
    XWidget_show((XWidget*)&self->m_wizard);
    XWidget_activateWindow((XWidget*)&self->m_wizard);
    demo_set_status(self, "向导: 已弹出（480x320 居中父窗口）");
}
#endif /* 向导弹出槽 */

#if XWIDGET_ON && XSTATUSBAR_ON && XABSTRACTBUTTON_ON && XPUSHBUTTON_ON && \
    XWIZARD_ON && XERRORMESSAGE_ON
/** @brief 页签 8「向导错误」启动器：点击显示错误提示条（弹窗化，见
 *         页签 8 装配段——XErrorMessage 不再启动即显，showMessage 本身
 *         即「置文本 + show」的瞬时提示条入口）。
 * @note  居中同向导槽，move 在 showMessage 之前（先移后显避免顶层窗
 *        在默认位闪现；xerr_updateSize 尺寸锚 300x40 下限，"Test error
 *        message" 实测不超锚，按 300x120 兜底居中公式，负值钳 0）。 */
static void demo_errShowSlot(XObject* receiver, XVarList* args)
{
    DemoWin* self = (DemoWin*)receiver;
    XWidget* parent;
    int x;
    int y;
    (void)args;
    if (!self) return;
    /* 先按当前固定尺寸居中（init 时 xerr_updateSize 已定尺；move 在
       showMessage 之前，对标 adv_btnMainWindowSlot 先移后显，避免
       顶层窗在默认位闪现后再跳居中位）。 */
    parent = XWidget_topLevelWidget((XWidget*)self);
    if (parent) {
        x = XWidget_x(parent) + (XWidget_width(parent) - 300) / 2;
        y = XWidget_y(parent) + (XWidget_height(parent) - 120) / 2;
        if (x < 0) x = 0;
        if (y < 0) y = 0;
        XWidget_move((XWidget*)&self->m_errMsg, x, y);
    }
    XErrorMessage_showMessage(&self->m_errMsg, "Test error message");
    demo_set_status(self, "错误提示: 已显示（瞬时提示条居中）");
}
#endif /* 错误提示槽 */


#if XWIDGET_ON && XABSTRACTBUTTON_ON && XPUSHBUTTON_ON && XCOMMANDLINKBUTTON_ON
/** @brief 页面 1 命令链接按钮 clicked 槽：更新联动标签与状态栏。 */
static void demo_commandlink_clickedSlot(XObject* receiver, XVarList* args)
{
    DemoWin* self = (DemoWin*)receiver;
    (void)args;
    if (!self) return;
#if XWIDGET_ON && XFRAME_ON && XLABEL_ON
    XLabel_setText_2(&self->m_linkLabel, "命令链接：点击");
#endif
    demo_set_status(self, "命令链接：点击");
}
#endif /* XWIDGET_ON && XABSTRACTBUTTON_ON && XPUSHBUTTON_ON && XCOMMANDLINKBUTTON_ON */
#endif /* XWIDGET_ON && XPUSHBUTTON_ON */

#if XWIDGET_ON && XABSTRACTBUTTON_ON && XTOOLBUTTON_ON && XMENU_ON
/** @brief 页面 1 工具按钮 clicked 槽：弹出关联菜单并更新状态栏。 */
static void demo_toolbutton_clickedSlot(XObject* receiver, XVarList* args)
{
    DemoWin* self = (DemoWin*)receiver;
    (void)args;
    if (!self) return;
    XToolButton_showMenu(&self->m_toolButton);
    demo_set_status(self, "工具按钮：弹出菜单");
}

/** @brief 页面 1 工具按钮 triggered(XAction*) 槽：显示默认动作被触发。 */
static void demo_toolbutton_triggeredSlot(XObject* receiver, XVarList* args)
{
    DemoWin* self = (DemoWin*)receiver;
    (void)args;
    if (!self) return;
#if XWIDGET_ON && XFRAME_ON && XLABEL_ON
    XLabel_setText_2(&self->m_linkLabel, "工具按钮：触发默认动作");
#endif
    demo_set_status(self, "工具按钮：触发默认动作");
}

/** @brief 页面 1 工具按钮菜单 triggered(XAction*) 槽：显示选中的菜单项。 */
static void demo_toolmenu_triggeredSlot(XObject* receiver, XVarList* args)
{
    DemoWin* self = (DemoWin*)receiver;
    XVarList_args_1(args, XAction*, action);
    const char* text = NULL;

    if (!self) return;
    if (action)
        text = XString_toUtf8(XAction_text_const(action));
    if (!text)
        text = "";
#if XWIDGET_ON && XFRAME_ON && XLABEL_ON
    {
        char buf[128];
        snprintf(buf, sizeof(buf), "工具按钮菜单：%s", text);
        XLabel_setText_2(&self->m_linkLabel, buf);
    }
#endif
    demo_set_status(self, text);
}
#endif /* XWIDGET_ON && XABSTRACTBUTTON_ON && XTOOLBUTTON_ON && XMENU_ON */

#if XWIDGET_ON && XFRAME_ON && XLABEL_ON && XLAYOUT_ON && XLAYOUT_STACKED_ON
/** @brief 页面 3 内层上一页按钮 clicked 槽：循环切换内层堆叠。 */
static void demo_stack_prev_clickedSlot(XObject* receiver, XVarList* args)
{
    DemoWin* self = (DemoWin*)receiver;
    int index;
    int count;
    (void)args;
    if (!self) return;
    index = XStackedLayout_currentIndex(&self->m_stackLayoutInner);
    count = XStackedLayout_count(&self->m_stackLayoutInner);
    if (count <= 0) return;
    if (index <= 0) index = count - 1;
    else --index;
    XStackedLayout_setCurrentIndex(&self->m_stackLayoutInner, index);
    XPrintf("XGuiWindowDemo: inner stacked page=%d (prev)\n", index);
    demo_set_status(self, index == 0 ? "内层页面 1" : "内层页面 2");
}

/** @brief 页面 3 内层下一页按钮 clicked 槽：循环切换内层堆叠。 */
static void demo_stack_next_clickedSlot(XObject* receiver, XVarList* args)
{
    DemoWin* self = (DemoWin*)receiver;
    int index;
    int count;
    (void)args;
    if (!self) return;
    index = XStackedLayout_currentIndex(&self->m_stackLayoutInner);
    count = XStackedLayout_count(&self->m_stackLayoutInner);
    if (count <= 0) return;
    index = (index + 1) % count;
    XStackedLayout_setCurrentIndex(&self->m_stackLayoutInner, index);
    XPrintf("XGuiWindowDemo: inner stacked page=%d (next)\n", index);
    demo_set_status(self, index == 0 ? "内层页面 1" : "内层页面 2");
}
#endif /* XWIDGET_ON && XFRAME_ON && XLABEL_ON && XLAYOUT_ON && XLAYOUT_STACKED_ON */

#if XWIDGET_ON && XABSTRACTBUTTON_ON && XCHECKBOX_ON
/** @brief 页面 2 复选框 checkStateChanged 槽：更新选择状态标签与状态栏。 */
static void demo_checkbox_stateSlot(XObject* receiver, XVarList* args)
{
    DemoWin* self = (DemoWin*)receiver;
    const char* text = "复选框：未选中";
    if (!self || !args) return;
    XVarList_args_1(args, int, state);
    switch (state) {
    case 1: text = "复选框：部分选中"; break;
    case 2: text = "复选框：已选中"; break;
    default: text = "复选框：未选中"; break;
    }
#if XWIDGET_ON && XFRAME_ON && XLABEL_ON
    XLabel_setText_2(&self->m_choiceLabel, text);
#endif
    demo_set_status(self, text);
}
#endif /* XWIDGET_ON && XABSTRACTBUTTON_ON && XCHECKBOX_ON */

#if XWIDGET_ON && XABSTRACTBUTTON_ON && XRADIOBUTTON_ON
/** @brief 页面 2 单选按钮 toggled 槽：更新选择状态标签与状态栏。 */
static void demo_radio_toggledSlot(XObject* receiver, XVarList* args)
{
    DemoWin* self = (DemoWin*)receiver;
    const char* text;
    (void)args;
    if (!self) return;
    text = XRadioButton_isChecked(&self->m_radioA) ? "单选：A"
                                                   : "单选：B";
#if XWIDGET_ON && XFRAME_ON && XLABEL_ON
    XLabel_setText_2(&self->m_choiceLabel, text);
#endif
    demo_set_status(self, text);
}
#endif /* XWIDGET_ON && XABSTRACTBUTTON_ON && XRADIOBUTTON_ON */

/* ==================== 事件槽重载 ==================== */

/** @brief CloseEvent：接受关闭，停止刷新并退出应用事件循环。 */
static void VDemoWin_closeEvent(XWidget* self, XEvent* event)
{
    DemoWin* demo = (DemoWin*)self;
    XClass_Parent(XWidget, EXWidget_CloseEvent,
                  void(*)(XWidget*, XEvent*))(self, event);
    demo_stopTimers(demo);
    demo->m_closed = true;
    XGuiApplication_quit();
    if (event) XEvent_accept(event);
}

/** @brief ResizeEvent：更新固定悬浮层锚点、主内容区几何，并让控件树提交新尺寸画面。 */
static void VDemoWin_resizeEvent(XWidget* self, XEvent* event)
{
    DemoWin* demo = (DemoWin*)self;
    XClass_Parent(XWidget, EXWidget_ResizeEvent,
                  void(*)(XWidget*, XEvent*))(self, event);
    demo->m_staticSceneDirty = true;
#if XWIDGET_ON && XFRAME_ON && XLABEL_ON
    demo_layout_chrome(self);
#endif
#if XWIDGET_ON && XABSTRACTBUTTON_ON && XPUSHBUTTON_ON && XFRAME_ON && \
    XLABEL_ON && XLAYOUT_ON && XLAYOUT_STACKED_ON
    /* 浮动导航面板随窗重排（贴边坐标/横竖版切换），先于内容区避让。 */
    demo_navUpdatePanel(demo);
#endif
#if XWIDGET_ON && XLAYOUT_ON && XLAYOUT_STACKED_ON
    demo_layout_content(self);
    /* 远程窗口页 adapt 已并入 demo_layout_content 尾部统一漏斗
       （2026-10-06: 切页路径也要跑, resizeEvent 不再单独补调）。 */
#endif
#if XGUI_PERFORMANCE_OVERLAY_ON && XFRAME_ON && XLABEL_ON
    if (XPerformanceOverlay_isFixed(&demo->m_performanceOverlay)) {
        demo_performance_anchorBottomRight(demo); /* 与 init 同口径：
            右贴齐窗口、底部避让状态栏 26px；按压穿透见 childAt 修复 */
    }
#endif
    /* 尺寸变化强制整窗重绘：demo_repaint 的静态缓存快路径只脏悬浮层
       小矩形，而 resize 后框架补漆=差带+子控件区，布局留白沟槽（左右
       边距带等无孩子覆盖区）不在任何更新内——fbdev 侧 requestPanelClear
       （toggle 清屏防残影）把表面整清后，这些沟槽以黑带面世（真机
       2026-09-28：双击最大化/还原与边缘改尺寸后左右 12/13px 黑边，
       用户实测「改变窗口大小需要重绘时没有全部重绘」）。resize 是
       低频路径，整窗更新由静态缓存 tile 拷贝一次承担（~1.2MB memcpy）。 */
    {
        XRect full = XWidget_rect(self);
        XWidget_updateRect(self, &full);
    }
}

/** @brief PaintEvent：根控件背景与性能悬浮层；子控件由 XWidget 自动递归绘制。 */
static void VDemoWin_paintEvent(XWidget* self, XEvent* event)
{
#if XGUI_PERFORMANCE_OVERLAY_ON && XFRAME_ON && XLABEL_ON
    DemoWin* demo = (DemoWin*)self;
    int64_t frameStartUsecs = demo_monotonicUsecs();
#endif
    /* 临时探针已拆除（绘制率/整窗-局部分类取证完毕，2026-09-29）：结论
       =改尺寸拖拽期每落地步 ~2 个局部 paintEvent（差带条带+HUD 标签
       微绘），presents=落地步数，无整窗帧无幻绘。 */
    demo_paintScene((DemoWin*)self, event);
#if XGUI_PERFORMANCE_OVERLAY_ON && XFRAME_ON && XLABEL_ON
    /* 悬浮层尺寸自适应（文字贴边收框）后，位置需同步右移/下移保持
       右贴窗口边、底贴状态栏——尺寸与上次锚定值不同才重锚。 */
    if (XPerformanceOverlay_isFixed(&demo->m_performanceOverlay)) {
        XRect ovlGeo = XPerformanceOverlay_geometry(
            &demo->m_performanceOverlay);
        if (ovlGeo.width != demo->m_overlayAnchorW ||
            ovlGeo.height != demo->m_overlayAnchorH)
            demo_performance_anchorBottomRight(demo);
    }
    {
        int64_t frameEndUsecs = demo_monotonicUsecs();
        /* FPS 口径=绘制活动率（每个 paintEvent 计 1 帧，含空闲 tick——
           用户裁定 2026-09-29：悬浮窗自身也在刷新，静止不为 0）。
           【HUD 手势期冻结（方案 C，2026-09-29）】装饰手势（拖拽移动/
           改尺寸）进行中跳过 updateFrame/updateNetwork：省掉 250ms 统计
           窗口落在手势步时的 HUD setText 全文重排+autoFit 度量+标签微
           绘（独立 flush 周期，受击步 1-3ms 抖动尖峰）。读数语义随之
           变化：手势帧不入样本，统计窗口跨度含冻结期——松手后首个
           paintEvent 一次补齐，该窗口读数=「含手势冻结跨度的真实活动
           率」（分母拉长、读数偏低一窗），不再是限帧闸下 ≈2x 落地步率
           的虚高口径；限帧是否生效仍以 CPU 差分/CPU% 为准。手势中窗口
           几何跟随不受影响（resizeEvent 已重锚
           悬浮层，本处重锚只在悬浮层自身尺寸变化时触发）。 */
        if (!XWindowDecoration_gestureActive()) {
            XPerformanceOverlay_updateFrame(&demo->m_performanceOverlay,
                                            frameStartUsecs, frameEndUsecs);
#if XGUI_PERFORMANCE_OVERLAY_NETWORK_ON
            if (demo->m_lastNetworkPollUsecs <= 0 ||
                frameEndUsecs - demo->m_lastNetworkPollUsecs >=
                    (int64_t)XGUI_PERFORMANCE_OVERLAY_UPDATE_MS * 1000LL) {
                uint64_t rxBytes = 0;
                uint64_t txBytes = 0;
                bool available = XDeviceNetwork_getNetworkCounters(&rxBytes, &txBytes);
                XPerformanceOverlay_updateNetwork(&demo->m_performanceOverlay,
                                                  available, rxBytes, txBytes,
                                                  frameEndUsecs);
                demo->m_lastNetworkPollUsecs = frameEndUsecs;
            }
#endif /* XGUI_PERFORMANCE_OVERLAY_NETWORK_ON */
        }
    }
#endif /* XGUI_PERFORMANCE_OVERLAY_ON && XFRAME_ON && XLABEL_ON */
}

/* 事件处理器诊断日志（定义在 main 前紧邻 setvbuf）：重定向下 UCRT 忽略
 * _IOLBF 行缓冲语义（实测仍按全缓冲，1KB 边界腰斩——见 main 内 setvbuf
 * 注释），裸 printf 行只在缓冲满/进程退出时落盘，挂起时末行只剩半行。 */
static int demo_log(const char* fmt, ...);

/** @brief 诊断日志熔断开关（wave6 管道挂起缓解①）。demo_log 一旦观测到
 *  底层写失败（vprintf 返回负 / fflush 返回 EOF——读端断开、共享流错误
 *  态）即置位，后续调用直接短路返回，不再对失效句柄逐次阻塞重试。 */
static bool g_logDisabled;

/** @brief KeyPressEvent：打印键码/修饰键/自动重复（真实输入闭环验证）。 */
static void VDemoWin_keyPressEvent(XWidget* self, XEvent* event)
{
    XKeyEvent* key = (XKeyEvent*)event;
    (void)self;
    if (!key) return;
    demo_log("XGuiWindowDemo: keyPress key=%d modifiers=0x%x autoRepeat=%d\n",
             XKeyEvent_key(key), (unsigned)XKeyEvent_modifiers(key),
             (int)XKeyEvent_autoRepeat(key));
}

/** @brief KeyReleaseEvent：打印释放键码。 */
static void VDemoWin_keyReleaseEvent(XWidget* self, XEvent* event)
{
    XKeyEvent* key = (XKeyEvent*)event;
    (void)self;
    if (!key) return;
    demo_log("XGuiWindowDemo: keyRelease key=%d modifiers=0x%x\n",
             XKeyEvent_key(key), (unsigned)XKeyEvent_modifiers(key));
}

/** @brief MousePressEvent：标题栏 ✕ 武装按压；背景区域处理性能悬浮层；
 *         子控件由自身接收事件。 */
static void VDemoWin_mousePressEvent(XWidget* self, XEvent* event)
{
    DemoWin* demo = (DemoWin*)self;
    XMouseEvent* mouse = (XMouseEvent*)event;
    if (!mouse) return;
    if (!mouse) return;
    demo_log("XGuiWindowDemo: mousePress button=%d buttons=0x%x pos=(%d,%d)\n",
             (int)XMouseEvent_button(mouse), (unsigned)XMouseEvent_buttons(mouse),
             (int)XMouseEvent_position(mouse).x, (int)XMouseEvent_position(mouse).y);
#if defined(__ANDROID__)
    { /* 安卓验证通道：stdout 不可见，鼠标事件到达即打 logcat。 */
        extern void XGuiDemo_debugLog(const char* text);
        char dbg[128];
        snprintf(dbg, sizeof(dbg),
                 "mousePress btn=%d pos=(%d,%d)",
                 (int)XMouseEvent_button(mouse),
                 (int)XMouseEvent_position(mouse).x,
                 (int)XMouseEvent_position(mouse).y);
        XGuiDemo_debugLog(dbg);
    }
#endif
    /* 交互按下即整帧重绘：子控件形态对话框（消息框/进度条等覆盖层）
       局部脏区重绘在其内部布局微移后会留下旧帧残影（实测消息框点击
       后图标/文字双影，两份内容纵向错位 ~45px）。按下频率低，整帧
       成本可接受；随后 paintTree 全量重画覆盖层新帧，旧帧残影清除。 */
    {
        XRect fullDirty;
        XRect_init(&fullDirty, 0, 0,
                   XWidget_width(&demo->m_base), XWidget_height(&demo->m_base));
        XWidget_updateRect(&demo->m_base, &fullDirty);
    }
#if XGUI_PERFORMANCE_OVERLAY_ON && XFRAME_ON && XLABEL_ON
    {
        XPoint position = XMouseEvent_position(mouse);
        if (demo_performance_contains(demo, position)) {
            if (XMouseEvent_button(mouse) == XMouseButton_RightButton) {
                XPerformanceOverlay_setFixed(
                    &demo->m_performanceOverlay,
                    !XPerformanceOverlay_isFixed(&demo->m_performanceOverlay));
                XPrintf("XGuiWindowDemo: performance overlay fixed=%s\n",
                        XPerformanceOverlay_isFixed(&demo->m_performanceOverlay)
                            ? "true" : "false");
                demo_repaint(demo);
            } else if (XMouseEvent_button(mouse) == XMouseButton_LeftButton) {
                /* 拖动起点提层（2026-10-05 用户口径：拖到导航区被分割
                 * 条/菜单按钮遮挡）：性能浮层升至内容兄弟末位，盖过分
                 * 隔条/导航面板等布局期自抬层。屏幕键盘为独立顶层
                 * Popup，天然恒在所有子控件（含浮层）之上——层级口径
                 * 「主窗内容 < 性能浮层 < 屏幕键盘」自动成立。 */
                XWidget_raise((XWidget*)&demo->m_performanceOverlay);
                (void)XPerformanceOverlay_beginDrag(
                    &demo->m_performanceOverlay, position.x, position.y);
            }
            XEvent_accept(event);
        }
    }
#endif
}

/** @brief MouseReleaseEvent：✕ 松开触发窗口关闭；结束性能悬浮层拖动。 */
static void VDemoWin_mouseReleaseEvent(XWidget* self, XEvent* event)
{
    DemoWin* demo = (DemoWin*)self;
    XMouseEvent* mouse = (XMouseEvent*)event;
    if (!mouse) return;
#if XGUI_PERFORMANCE_OVERLAY_ON && XFRAME_ON && XLABEL_ON
    if (XMouseEvent_button(mouse) == XMouseButton_LeftButton &&
        XPerformanceOverlay_isDragging(&demo->m_performanceOverlay)) {
        XPerformanceOverlay_endDrag(&demo->m_performanceOverlay);
        demo_repaint(demo);
        XEvent_accept(event);
    }
#endif
}

/** @brief MouseDoubleClickEvent：打印双击键/坐标。 */
static void VDemoWin_mouseDoubleClickEvent(XWidget* self, XEvent* event)
{
    XMouseEvent* mouse = (XMouseEvent*)event;
    DemoWin* demo = (DemoWin*)self;
    if (!mouse) return;
    demo_log("XGuiWindowDemo: mouseDoubleClick button=%d pos=(%d,%d)\n",
             (int)XMouseEvent_button(mouse),
             (int)XMouseEvent_position(mouse).x, (int)XMouseEvent_position(mouse).y);
}

/** @brief MouseMoveEvent：边缘改尺寸/标题栏拖拽/悬停热跟踪/悬浮层拖动。 */
static void VDemoWin_mouseMoveEvent(XWidget* self, XEvent* event)
{
    DemoWin* demo = (DemoWin*)self;
    XMouseEvent* mouse = (XMouseEvent*)event;
    if (!mouse) return;
#if XGUI_PERFORMANCE_OVERLAY_ON && XFRAME_ON && XLABEL_ON
    if (XPerformanceOverlay_isDragging(&demo->m_performanceOverlay)) {
        XMouseButton buttons = XMouseEvent_buttons(mouse);
        if ((buttons & XMouseButton_LeftButton) != 0) {
            if (XPerformanceOverlay_dragTo(
                    &demo->m_performanceOverlay,
                    XMouseEvent_position(mouse).x,
                    XMouseEvent_position(mouse).y,
                    XWidget_width(self), XWidget_height(self)))
                demo_repaint(demo);
            XEvent_accept(event);
        } else {
            XPerformanceOverlay_endDrag(&demo->m_performanceOverlay);
        }
    }
#endif
}

/** @brief WheelEvent：打印滚轮角度增量与坐标。 */
static void VDemoWin_wheelEvent(XWidget* self, XEvent* event)
{
    XWheelEvent* wheel = (XWheelEvent*)event;
    XPoint delta;
    (void)self;
    if (!wheel) return;
    delta = XWheelEvent_angleDelta(wheel);
    demo_log("XGuiWindowDemo: wheel delta=(%d,%d) pos=(%d,%d)\n",
             (int)delta.x, (int)delta.y,
             (int)XWheelEvent_position(wheel).x,
             (int)XWheelEvent_position(wheel).y);
}

/** @brief EnterEvent：打印进入坐标（局部+全局）。 */
static void VDemoWin_enterEvent(XWidget* self, XEvent* event)
{
    XEnterEvent* enter = (XEnterEvent*)event;
    XPoint global;
    (void)self;
    if (!enter) return;
    global = XEnterEvent_globalPosition(enter);
    demo_log("XGuiWindowDemo: enter pos=(%d,%d) global=(%d,%d)\n",
             (int)XEnterEvent_position(enter).x, (int)XEnterEvent_position(enter).y,
             (int)global.x, (int)global.y);
}

/** @brief LeaveEvent：打印离开通知。 */
static void VDemoWin_leaveEvent(XWidget* self, XEvent* event)
{
    (void)self;
    (void)event;
    demo_log("XGuiWindowDemo: leave\n");
}
/** @brief 演示窗口类虚表初始化。 */
static XVtable* DemoWin_class_init(void)
{
    XVTABLE_INIT_DEFAULT(DemoWin)
    XVTABLE_INHERIT_XCLASS(XWidget);
    XVTABLE_OVERLOAD_DEFAULT(EXObject_TimerEvent, VDemoWin_timerEvent);
    XVTABLE_OVERLOAD_DEFAULT(EXWidget_CloseEvent, VDemoWin_closeEvent);
    XVTABLE_OVERLOAD_DEFAULT(EXWidget_ResizeEvent, VDemoWin_resizeEvent);
    XVTABLE_OVERLOAD_DEFAULT(EXWidget_PaintEvent, VDemoWin_paintEvent);
    XVTABLE_OVERLOAD_DEFAULT(EXWidget_KeyPressEvent, VDemoWin_keyPressEvent);
    XVTABLE_OVERLOAD_DEFAULT(EXWidget_KeyReleaseEvent, VDemoWin_keyReleaseEvent);
    XVTABLE_OVERLOAD_DEFAULT(EXWidget_MousePressEvent, VDemoWin_mousePressEvent);
    XVTABLE_OVERLOAD_DEFAULT(EXWidget_MouseReleaseEvent, VDemoWin_mouseReleaseEvent);
    XVTABLE_OVERLOAD_DEFAULT(EXWidget_MouseDoubleClickEvent,
                             VDemoWin_mouseDoubleClickEvent);
    XVTABLE_OVERLOAD_DEFAULT(EXWidget_MouseMoveEvent, VDemoWin_mouseMoveEvent);
    XVTABLE_OVERLOAD_DEFAULT(EXWidget_WheelEvent, VDemoWin_wheelEvent);
    XVTABLE_OVERLOAD_DEFAULT(EXWidget_EnterEvent, VDemoWin_enterEvent);
    XVTABLE_OVERLOAD_DEFAULT(EXWidget_LeaveEvent, VDemoWin_leaveEvent);
    return XVTABLE_DEFAULT;
}

/** @brief 创建演示窗口对象并套用子类虚表。 */
static DemoWin* DemoWin_create(void)
{
    DemoWin* self = (DemoWin*)XMemory_malloc(
        sizeof(DemoWin), XCLASS_DEFAULT_MEMORY_TYPE);
    if (!self) return NULL;
    memset(self, 0, sizeof(DemoWin));
    XWidget_init(&self->m_base, NULL, 0);
    XClassSetVtable(self, DemoWin);
    Set_Class_Memory(self, XCLASS_DEFAULT_MEMORY_TYPE);
    Set_Class_IsHeap(self, true);
    XImage_init(&self->m_staticScene);
    self->m_staticSceneDirty = true;
    self->m_framePump = NULL;
    self->m_autoQuitTimer = XTIMER_INVALID_ID;
    self->m_lcdTimer = XTIMER_INVALID_ID;
    self->m_overlayTimer = XTIMER_INVALID_ID;
#if XGUI_PERFORMANCE_OVERLAY_ON && XWIDGET_ON && XFRAME_ON && XLABEL_ON
    demo_performance_init(self);
#endif
#if XWIDGET_ON && XFRAME_ON && XLABEL_ON
    /* 顶部标题栏文本（深蓝背景由静态场景绘制，白字覆盖其上）。
       Bold：合成粗体样张——标题是界面里最合适加粗的层级。 */
    XLabel_init(&self->m_titleLabel, &self->m_base, 0);
    demo_set_widget_default_font((XWidget*)&self->m_titleLabel);
    {
        XFont titleFont = XWidget_font((XWidget*)&self->m_titleLabel);
        XFont_setBold(&titleFont, true);
        XWidget_setFont((XWidget*)&self->m_titleLabel, &titleFont);
        XClassDeinit(&titleFont);
    }
    XLabel_setText_2(&self->m_titleLabel, "XGui 控件演示");
    XLabel_setTextPixelSize(&self->m_titleLabel, 18);
    XLabel_setAlignment(&self->m_titleLabel,
                        XAlignment_Left | XAlignment_VCenter);
    XWidget_setForegroundRole((XWidget*)&self->m_titleLabel,
                              XPaletteColorRole_HighlightedText);
    XWidget_setGeometry((XWidget*)&self->m_titleLabel, 16, 0, 420, 40);
    XWidget_show((XWidget*)&self->m_titleLabel);
#endif
#if XWIDGET_ON && XLAYOUT_ON && XLAYOUT_STACKED_ON
    /* 主内容堆叠：三个演示页面容器（可见性由 XStackedLayout 管理）。 */
    XStackedLayout_init(&self->m_stackLayout);
    XWidget_init(&self->m_pageButtons, &self->m_base, 0);
    XWidget_init(&self->m_pageChoices, &self->m_base, 0);
    XWidget_init(&self->m_pageStacked, &self->m_base, 0);
    XWidget_init(&self->m_pageInputs, &self->m_base, 0);
    XStackedLayout_addWidget(&self->m_stackLayout,
                             (XWidget*)&self->m_pageButtons);
    XStackedLayout_addWidget(&self->m_stackLayout,
                             (XWidget*)&self->m_pageChoices);
    XStackedLayout_addWidget(&self->m_stackLayout,
                             (XWidget*)&self->m_pageStacked);
    XStackedLayout_addWidget(&self->m_stackLayout,
                             (XWidget*)&self->m_pageInputs);
    XWidget_init(&self->m_pageTabs, &self->m_base, 0);
    XStackedLayout_addWidget(&self->m_stackLayout,
                             (XWidget*)&self->m_pageTabs);
    /* ---- 页面 5~14：扩展页注册（xgui_demo_pages.h 契约，堆根随父级联
     * 析构）；裁剪配置下 build 返回 NULL 则跳过注册。下标=页索引-5，
     * 下标 7=页 12（图表演示，内置页）恒 NULL 占位。 ---- */
    {
        int exti;
#if XINYUE_EMBEDDED
        /* 嵌入式（F407 外扩堆 1008KB）：全部扩展页暂不预建——键盘页的
         * 拼音引擎+虚拟键盘控件树为最大内存户；性能悬浮窗演示优先，
         * 键盘页待后备存储静态绑定（extbuf 640KB 方案）后接回。 */
        XWidget* (*const kExtBuilders[10])(XWidget*, DemoPageStatusFn, void*) = {
            NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL
        };
#else
        XWidget* (*const kExtBuilders[10])(XWidget*, DemoPageStatusFn, void*) = {
            demo_page_views_build, demo_page_dialogs_build,
            demo_page_advanced_build, demo_page_effects_build,
            demo_page_keyboard_build, demo_page_remote_server_build,
            demo_page_remote_client_build, /* 2026-10-02 追加第 7 扩展页。 */
            NULL, /* 页 12（图表演示）内置页占位。 */
            demo_page_network_build, /* 2026-10-06 网络设置页（系统设置）。 */
            demo_page_overlay_settings_build /* 2026-10-06 悬浮窗设置页（系统设置）。 */
        };
#endif
        for (exti = 0; exti < 10; ++exti) {
#if XCHARTS_ON
            if (exti == 7) {
                /* 页 12（图表演示）内置页在此占位入栈：保证「堆叠下标=
                   页索引」连续——后随扩展页（网络设置/悬浮窗设置）恰落
                   堆叠序 13/14；曾把图表页挪到扩展页之后入栈，两新页
                   顶掉了它的序号（--page 13/14 截图错页实证）。 */
                XWidget_init(&self->m_pageChart, &self->m_base, 0);
                XStackedLayout_addWidget(&self->m_stackLayout,
                                         &self->m_pageChart);
                continue;
            }
#endif
            if (!kExtBuilders[exti])
                continue;
            self->m_extPages[exti] =
                kExtBuilders[exti]((XWidget*)&self->m_base,
                                   demo_ext_page_status, self);
            if (self->m_extPages[exti])
                XStackedLayout_addWidget(&self->m_stackLayout,
                                         self->m_extPages[exti]);
        }
    }
#endif
#if XWIDGET_ON && XABSTRACTBUTTON_ON && XPUSHBUTTON_ON && XFRAME_ON && \
    XLABEL_ON && XLAYOUT_ON && XLAYOUT_STACKED_ON
    /* 浮动导航面板装配（「先切一级分类、再选页面」，见文件域
     * 「浮动导航面板」段）：面板/标题/换边/收起 + 3 一级分类钮
     * （控件/系统设置/远程，互斥组 idClicked 切手风琴）+ 5 子分组
     * 标题标签池（竖版展开时按活动分类轮转显隐）+ 15 页面钮（挂活动
     * 分类下，互斥组高亮当前页）。m_pageNav 下标=页索引不变，CLI
     * --page、扩展页 autotest 调度（5+exti）、XI2 回归锁（m_pageNav[5]
     * 几何取中心）不受影响。几何全部由 demo_navUpdatePanel 统一分配
     * （停靠/收起/展开三态）。 */
#if XBUTTONGROUP_ON
    XButtonGroup_init(&self->m_navGroup, NULL);
    XButtonGroup_setExclusive(&self->m_navGroup, true);
    XButtonGroup_init(&self->m_navCatGroup, NULL);
    XButtonGroup_setExclusive(&self->m_navCatGroup, true);
#endif
    XWidget_init(&self->m_navPanel, &self->m_base, 0);
    XLabel_init(&self->m_navTitle, &self->m_navPanel, 0);
    demo_set_widget_default_font((XWidget*)&self->m_navTitle);
    XLabel_setText_2(&self->m_navTitle, "\xE5\xAF\xBC\xE8\x88\xAA"); /* 导航 */
    XLabel_setTextPixelSize(&self->m_navTitle, 14);
    XLabel_setAlignment(&self->m_navTitle,
                        XAlignment_Left | XAlignment_VCenter);
    XWidget_show(&self->m_navTitle);
    XPushButton_init(&self->m_navDockBtn, &self->m_navPanel, 0);
    demo_set_widget_default_font((XWidget*)&self->m_navDockBtn);
    XObject_connect_1((XObject*)&self->m_navDockBtn,
                      (size_t)XPushButton_clicked_signal(NULL, false),
                      (XObject*)self, demo_navDockSlot,
                      XConnectionType_Direct);
    XWidget_show(&self->m_navDockBtn);
    XPushButton_init(&self->m_navCollapseBtn, &self->m_navPanel, 0);
    demo_set_widget_default_font((XWidget*)&self->m_navCollapseBtn);
    XObject_connect_1((XObject*)&self->m_navCollapseBtn,
                      (size_t)XPushButton_clicked_signal(NULL, false),
                      (XObject*)self, demo_navCollapseSlot,
                      XConnectionType_Direct);
    XWidget_show(&self->m_navCollapseBtn);
    {
        static void (*const kNavSlots[15])(XObject*, XVarList*) = {
            demo_nav0Slot, demo_nav1Slot, demo_nav2Slot, demo_nav3Slot,
            demo_nav4Slot, demo_nav5Slot, demo_nav6Slot, demo_nav7Slot,
            demo_nav8Slot, demo_nav9Slot, demo_nav10Slot, demo_nav11Slot,
            demo_nav12Slot, /* 2026-10-03 图表演示入列。 */
            demo_nav13Slot, /* 2026-10-06 网络设置入列。 */
            demo_nav14Slot  /* 2026-10-06 悬浮窗设置入列。 */
        };
        int i;
#if XBUTTONGROUP_ON
        /* 分类互斥组 idClicked → 单槽切手风琴（连接一次，勿入循环重复连）。 */
        XObject_connect_1((XObject*)&self->m_navCatGroup,
                          (size_t)XButtonGroup_idClicked_signal(NULL, 0),
                          (XObject*)self, demo_navCatSlot,
                          XConnectionType_Direct);
#endif
        for (i = 0; i < DEMO_NAV_CAT_N; ++i) {
            XPushButton* cat = &self->m_navCatBtns[i];
            XPushButton_init(cat, &self->m_navPanel, 0);
            demo_set_widget_default_font((XWidget*)cat);
            XPushButton_setText_2(cat, kNavGroups[i].caption);
            XAbstractButton_setCheckable((XAbstractButton*)cat, true);
#if XBUTTONGROUP_ON
            XButtonGroup_addButton(&self->m_navCatGroup,
                                   (XAbstractButton*)cat, i);
#endif
            XWidget_show((XWidget*)cat);
        }
        for (i = 0; i < DEMO_NAV_HEAD_N; ++i) {
            XLabel* head = &self->m_navHeadLabels[i];
            XLabel_init(head, &self->m_navPanel, 0);
            demo_set_widget_default_font((XWidget*)head);
            XLabel_setTextPixelSize(head, 12);
            XWidget_setForegroundRole((XWidget*)head,
                                      XPaletteColorRole_Mid);
            XLabel_setAlignment(head,
                                XAlignment_Left | XAlignment_VCenter);
            XWidget_hide((XWidget*)head); /* 竖版展开时按分类轮转显隐。 */
        }
        for (i = 0; i < DEMO_NAV_PAGE_N; ++i) {
            XPushButton* button = &self->m_pageNav[i];
            XPushButton_init(button, &self->m_navPanel, 0);
            demo_set_widget_default_font((XWidget*)button);
            XPushButton_setText_2(button, demo_page_name(i));
            XObject_connect_1((XObject*)button,
                              (size_t)XPushButton_clicked_signal(NULL, false),
                              (XObject*)self, kNavSlots[i],
                              XConnectionType_Direct);
#if XBUTTONGROUP_ON
            /* 可选中 + 入互斥组：当前页钮常亮（:checked），点击已选中钮
               被 toggle 掉后由 demo_switchPage 复位。 */
            XAbstractButton_setCheckable((XAbstractButton*)button, true);
            XButtonGroup_addButton(&self->m_navGroup,
                                   (XAbstractButton*)button, i);
#endif
            XWidget_show((XWidget*)button);
        }
    }
    self->m_navDock = 0;       /* 默认贴左 */
    self->m_navCollapsed = false;
    self->m_navCategory = 0;   /* 默认展开「控件」 */
    self->m_navWidth = DEMO_NAV_PANEL_W;
    self->m_navTBH = DEMO_NAV_TB_H;
    self->m_overlayPinned = false; /* 悬浮层默认自动锚右下（用户预设后挂起）。 */
    {
        /* 分割条：贴面板内缘拖动调宽/高，双击收起，收起态单击展开。 */
        static const DemoSplitterCallbacks kNavSplitCbs = {
            demo_navSplitSizeFor, demo_navSplitApplySize, demo_navSplitToggle
        };
        self->m_navSplit = DemoSplitter_create_ex(XCLASS_DEFAULT_MEMORY_TYPE,
                                                  &self->m_base, 0,
                                                  &kNavSplitCbs, self);
    }
    demo_navUpdatePanel(self);
#endif
#if XWIDGET_ON && XPUSHBUTTON_ON
    /* ---- 页面 1：按钮演示 ---- */
    XPushButton_init(&self->m_button, (XWidget*)&self->m_pageButtons, 0);
    demo_set_widget_default_font((XWidget*)&self->m_button);
    XPushButton_setText_2(&self->m_button, "按钮");
    XWidget_setGeometry((XWidget*)&self->m_button, 40, 48, 180, 36);
    XObject_connect_1((XObject*)&self->m_button,
                      (size_t)XPushButton_pressed_signal(NULL),
                      (XObject*)self, demo_button_pressedSlot,
                      XConnectionType_Direct);
    XObject_connect_1((XObject*)&self->m_button,
                      (size_t)XPushButton_released_signal(NULL),
                      (XObject*)self, demo_button_releasedSlot,
                      XConnectionType_Direct);
    XWidget_show((XWidget*)&self->m_button);
#if XWIDGET_ON && XFRAME_ON && XLABEL_ON
    XLabel_init(&self->m_linkLabel, (XWidget*)&self->m_pageButtons, 0);
    demo_set_widget_default_font((XWidget*)&self->m_linkLabel);
    XLabel_setText_2(&self->m_linkLabel, "就绪");
    XLabel_setTextPixelSize(&self->m_linkLabel, 16);
    XLabel_setAlignment(&self->m_linkLabel, XAlignment_Left | XAlignment_Top);
    XWidget_setGeometry((XWidget*)&self->m_linkLabel, 40, 212, 420, 24);
    XWidget_show((XWidget*)&self->m_linkLabel);
#endif
#endif
#if XWIDGET_ON && XABSTRACTBUTTON_ON && XPUSHBUTTON_ON && XCOMMANDLINKBUTTON_ON
    XCommandLinkButton_init(&self->m_commandLink,
                            (XWidget*)&self->m_pageButtons, 0);
    demo_set_widget_default_font((XWidget*)&self->m_commandLink);
    /* Bold 样张：命令链接主标题加粗、描述保持常规——同屏粗/常规对照。 */
    {
        XFont linkFont = XWidget_font((XWidget*)&self->m_commandLink);
        XFont_setBold(&linkFont, true);
        XWidget_setFont((XWidget*)&self->m_commandLink, &linkFont);
        XClassDeinit(&linkFont);
    }
    XCommandLinkButton_setText_2(&self->m_commandLink,
                                 "命令链接按钮");
    XCommandLinkButton_setDescription_2(&self->m_commandLink,
                                        "带标题与描述的按钮");
    XWidget_setGeometry((XWidget*)&self->m_commandLink, 40, 104, 320, 54);
    XObject_connect_1((XObject*)&self->m_commandLink,
                      (size_t)XCommandLinkButton_clicked_signal(NULL, false),
                      (XObject*)self, demo_commandlink_clickedSlot,
                      XConnectionType_Direct);
    XWidget_show((XWidget*)&self->m_commandLink);
#endif
#if XWIDGET_ON && XABSTRACTBUTTON_ON && XTOOLBUTTON_ON && XMENU_ON
    /* ---- 页面 0：工具按钮（默认动作 + 弹出菜单） ---- */
    XAction_init(&self->m_toolAction);
    XAction_setText_2(&self->m_toolAction, "工具按钮");
    XToolButton_init(&self->m_toolButton, (XWidget*)&self->m_pageButtons, 0);
    demo_set_widget_default_font((XWidget*)&self->m_toolButton);
    XAbstractButton_setText_2((XAbstractButton*)&self->m_toolButton,
                              "工具按钮");
    XToolButton_setToolButtonStyle(
        &self->m_toolButton, XToolButtonStyle_TextBesideIcon);
    XToolButton_setDefaultAction(&self->m_toolButton, &self->m_toolAction);
    XWidget_setGeometry((XWidget*)&self->m_toolButton, 40, 168, 150, 32);
    XObject_connect_1((XObject*)&self->m_toolButton,
                      (size_t)XToolButton_triggered_signal(NULL, NULL),
                      (XObject*)self, demo_toolbutton_triggeredSlot,
                      XConnectionType_Direct);
    XObject_connect_1((XObject*)&self->m_toolButton,
                      (size_t)XAbstractButton_clicked_signal(NULL, false),
                      (XObject*)self, demo_toolbutton_clickedSlot,
                      XConnectionType_Direct);
    XWidget_show((XWidget*)&self->m_toolButton);
    /* 弹出菜单：打开 / 另存为 / 分隔 / 退出。 */
    XMenu_init(&self->m_toolMenu, NULL);
    /* 菜单默认字体改为轮廓字库（含 GB2312 汉字），否则中文条目无法渲染。 */
    demo_set_widget_default_font((XWidget*)&self->m_toolMenu);
    XMenu_addAction_2(&self->m_toolMenu, "打开");
    XMenu_addAction_2(&self->m_toolMenu, "另存为");
    XMenu_addSeparator(&self->m_toolMenu);
    XMenu_addAction_2(&self->m_toolMenu, "退出");
    XToolButton_setMenu(&self->m_toolButton, &self->m_toolMenu);
    XObject_connect_1((XObject*)&self->m_toolMenu,
                      (size_t)XMenu_triggered_signal(NULL, NULL),
                      (XObject*)self, demo_toolmenu_triggeredSlot,
                      XConnectionType_Direct);
#endif
#if XWIDGET_ON && XABSTRACTBUTTON_ON && XCHECKBOX_ON
    /* ---- 页面 2：选择演示 ---- */
    XCheckBox_init(&self->m_checkBox, (XWidget*)&self->m_pageChoices, 0);
    demo_set_widget_default_font((XWidget*)&self->m_checkBox);
    XCheckBox_setText_2(&self->m_checkBox, "复选框（三态）");
    XCheckBox_setTristate(&self->m_checkBox, true);
    XCheckBox_setCheckState(&self->m_checkBox, XCheckState_PartiallyChecked);
    XWidget_setGeometry((XWidget*)&self->m_checkBox, 40, 48, 220, 26);
    XObject_connect_1((XObject*)&self->m_checkBox,
                      (size_t)XCheckBox_checkStateChanged_signal(
                          NULL, XCheckState_Unchecked),
                      (XObject*)self, demo_checkbox_stateSlot,
                      XConnectionType_Direct);
    XWidget_show((XWidget*)&self->m_checkBox);
#endif
#if XWIDGET_ON && XABSTRACTBUTTON_ON && XRADIOBUTTON_ON
    XRadioButton_init(&self->m_radioA, (XWidget*)&self->m_pageChoices, 0);
    demo_set_widget_default_font((XWidget*)&self->m_radioA);
    XRadioButton_setText_2(&self->m_radioA, "选项 A");
    XWidget_setGeometry((XWidget*)&self->m_radioA, 40, 88, 120, 22);
    XRadioButton_setChecked(&self->m_radioA, true);
    XRadioButton_init(&self->m_radioB, (XWidget*)&self->m_pageChoices, 0);
    demo_set_widget_default_font((XWidget*)&self->m_radioB);
    XRadioButton_setText_2(&self->m_radioB, "选项 B");
    XWidget_setGeometry((XWidget*)&self->m_radioB, 180, 88, 120, 22);
    XObject_connect_1((XObject*)&self->m_radioA,
                      (size_t)XRadioButton_toggled_signal(NULL, false),
                      (XObject*)self, demo_radio_toggledSlot,
                      XConnectionType_Direct);
    XObject_connect_1((XObject*)&self->m_radioB,
                      (size_t)XRadioButton_toggled_signal(NULL, false),
                      (XObject*)self, demo_radio_toggledSlot,
                      XConnectionType_Direct);
    XWidget_show((XWidget*)&self->m_radioA);
    XWidget_show((XWidget*)&self->m_radioB);
#endif
#if XWIDGET_ON && XFRAME_ON && XLABEL_ON
    XLabel_init(&self->m_choiceLabel, (XWidget*)&self->m_pageChoices, 0);
    demo_set_widget_default_font((XWidget*)&self->m_choiceLabel);
    XLabel_setText_2(&self->m_choiceLabel, "就绪");
    XLabel_setTextPixelSize(&self->m_choiceLabel, 16);
    XLabel_setAlignment(&self->m_choiceLabel, XAlignment_Left | XAlignment_Top);
    XWidget_setGeometry((XWidget*)&self->m_choiceLabel, 40, 124, 420, 24);
    XWidget_show((XWidget*)&self->m_choiceLabel);
#endif
#if XWIDGET_ON && XFRAME_ON && XLABEL_ON && XLAYOUT_ON && XLAYOUT_STACKED_ON
    /* ---- 页面 3：堆叠演示（内层堆叠 + 上一页/下一页按钮） ---- */
    XStackedLayout_init(&self->m_stackLayoutInner);
    XLabel_init(&self->m_stackPageOne, (XWidget*)&self->m_pageStacked, 0);
    demo_set_widget_default_font((XWidget*)&self->m_stackPageOne);
    XLabel_setText_2(&self->m_stackPageOne, "内层页面 1");
    XLabel_setTextPixelSize(&self->m_stackPageOne, 16);
    XLabel_setAlignment(&self->m_stackPageOne,
                        XAlignment_HCenter | XAlignment_VCenter);
    XLabel_init(&self->m_stackPageTwo, (XWidget*)&self->m_pageStacked, 0);
    demo_set_widget_default_font((XWidget*)&self->m_stackPageTwo);
    XLabel_setText_2(&self->m_stackPageTwo, "内层页面 2");
    XLabel_setTextPixelSize(&self->m_stackPageTwo, 16);
    XLabel_setAlignment(&self->m_stackPageTwo,
                        XAlignment_HCenter | XAlignment_VCenter);
    XStackedLayout_addWidget(&self->m_stackLayoutInner,
                             (XWidget*)&self->m_stackPageOne);
    XStackedLayout_addWidget(&self->m_stackLayoutInner,
                             (XWidget*)&self->m_stackPageTwo);
#if XPUSHBUTTON_ON
    XPushButton_init(&self->m_stackPrevButton,
                     (XWidget*)&self->m_pageStacked, 0);
    demo_set_widget_default_font((XWidget*)&self->m_stackPrevButton);
    XPushButton_setText_2(&self->m_stackPrevButton, "上一页");
    XWidget_setGeometry((XWidget*)&self->m_stackPrevButton, 40, 170, 90, 28);
    XPushButton_init(&self->m_stackNextButton,
                     (XWidget*)&self->m_pageStacked, 0);
    demo_set_widget_default_font((XWidget*)&self->m_stackNextButton);
    XPushButton_setText_2(&self->m_stackNextButton, "下一页");
    XWidget_setGeometry((XWidget*)&self->m_stackNextButton, 136, 170, 90, 28);
    XObject_connect_1((XObject*)&self->m_stackPrevButton,
                      (size_t)XPushButton_clicked_signal(NULL, false),
                      (XObject*)self, demo_stack_prev_clickedSlot,
                      XConnectionType_Direct);
    XObject_connect_1((XObject*)&self->m_stackNextButton,
                      (size_t)XPushButton_clicked_signal(NULL, false),
                      (XObject*)self, demo_stack_next_clickedSlot,
                      XConnectionType_Direct);
    XWidget_show((XWidget*)&self->m_stackPrevButton);
    XWidget_show((XWidget*)&self->m_stackNextButton);
#endif /* XPUSHBUTTON_ON */

#if XWIDGET_ON && XGROUPBOX_ON && XLINEEDIT_ON && XSPINBOX_ON && \
    XABSTRACTSLIDER_ON && XSLIDER_ON && XPROGRESSBAR_ON
    /* ---- 页面 4：输入控件演示（GroupBox 内输入/微调/滑块/进度联动） ---- */
    XGroupBox_init(&self->m_groupBox, (XWidget*)&self->m_pageInputs, 0);
    demo_set_widget_default_font((XWidget*)&self->m_groupBox);
    XGroupBox_setTitle(&self->m_groupBox,
                         "\xE8\xBE\x93\xE5\x85\xA5\xE6\x8E\xA7\xE4\xBB\xB6"); /* 输入控件 */

    XLineEdit_init(&self->m_lineEdit, (XWidget*)&self->m_groupBox, 0);
    demo_set_widget_default_font((XWidget*)&self->m_lineEdit);
    XLineEdit_setPlaceholderText(&self->m_lineEdit,
        "\xE8\xBE\x93\xE5\x85\xA5\xE6\x96\x87\xE6\x9C\xAC"); /* 输入文本 */
    XObject_connect_1((XObject*)&self->m_lineEdit,
                      (size_t)XLineEdit_textChanged_signal(&self->m_lineEdit),
                      (XObject*)self, demo_input_textChangedSlot,
                      XConnectionType_Direct);

    XSpinBox_init(&self->m_spinBox, (XWidget*)&self->m_groupBox, 0);
    demo_set_widget_default_font((XWidget*)&self->m_spinBox);
    XSpinBox_setRange(&self->m_spinBox, 0, 100);
    /* 初值三联动控件统一为 30：setValue 置于 connect 之前（对标 Qt
       先设初值再 connect 的惯用法），初始化不触发 valueChanged 联动槽，
       状态行保持"就绪"。此前仅进度条设 30，滑块/微调框停在 0 不同步。 */
    XSpinBox_setValue(&self->m_spinBox, 30);
    XObject_connect_1((XObject*)&self->m_spinBox,
                      (size_t)XSpinBox_valueChanged_signal(&self->m_spinBox),
                      (XObject*)self, demo_input_spinChangedSlot,
                      XConnectionType_Direct);

    XSlider_init(&self->m_slider, (XWidget*)&self->m_groupBox, 0);
    demo_set_widget_default_font((XWidget*)&self->m_slider);
    XAbstractSlider_setRange((XAbstractSlider*)&self->m_slider, 0, 100);
    XAbstractSlider_setValue((XAbstractSlider*)&self->m_slider, 30); /* 初值同步，见上 */
    XObject_connect_1((XObject*)&self->m_slider,
                      (size_t)XSlider_valueChanged_signal(&self->m_slider, 0),
                      (XObject*)self, demo_input_sliderChangedSlot,
                      XConnectionType_Direct);

    XProgressBar_init(&self->m_progressBar, (XWidget*)&self->m_groupBox, 0);
    demo_set_widget_default_font((XWidget*)&self->m_progressBar);
    XProgressBar_setRange(&self->m_progressBar, 0, 100);
    XProgressBar_setValue(&self->m_progressBar, 30); /* 初值 30（与滑块/微调框同步，见上） */

    XLabel_init(&self->m_inputStatus, (XWidget*)&self->m_pageInputs, 0);
    demo_set_widget_default_font((XWidget*)&self->m_inputStatus);
    XLabel_setText_2(&self->m_inputStatus, "\xE5\xB0\xB1\xE7\xBB\xAA"); /* 就绪 */
    XLabel_setAlignment(&self->m_inputStatus,
                        XAlignment_Left | XAlignment_Top);

    /* 布局摆位在 demo_layout_content 中按窗口尺寸自适应（避免固定
       宽度在小窗口下溢出）。 */
    XWidget_show((XWidget*)&self->m_groupBox);
    XWidget_show((XWidget*)&self->m_lineEdit);
    XWidget_show((XWidget*)&self->m_spinBox);
    XWidget_show((XWidget*)&self->m_slider);
    XWidget_show((XWidget*)&self->m_progressBar);
    XWidget_show((XWidget*)&self->m_inputStatus);
    /* 放到最低层避免挡住 tab 按钮 */
    XWidget_lower((XWidget*)&self->m_inputStatus);
#endif
#if XWIDGET_ON && XCOMBOBOX_ON
    /* 下拉框：2026-10-03 自选项卡页打散归位输入页（几何在
       demo_layout_content 自适应摆位）。 */
    XComboBox_init(&self->m_comboBox, (XWidget*)&self->m_pageInputs, 0);
    demo_set_widget_default_font((XWidget*)&self->m_comboBox);
    XComboBox_addItem_2(&self->m_comboBox, "Option 1");
    XComboBox_addItem_2(&self->m_comboBox, "Option 2");
    XComboBox_addItem_2(&self->m_comboBox, "Option 3");
    XComboBox_setCurrentIndex(&self->m_comboBox, 0);
    XObject_connect_1((XObject*)&self->m_comboBox,
                      (size_t)XComboBox_currentTextChanged_signal(
                          &self->m_comboBox, 0),
                      (XObject*)self, demo_tab_comboSlot,
                      XConnectionType_Direct);
    XWidget_show((XWidget*)&self->m_comboBox);
#endif
#if XWIDGET_ON && XABSTRACTSLIDER_ON && XDIAL_ON && XPROGRESSBAR_ON
    /* 旋钮 + 联动进度条：打散归位输入页（几何在 demo_layout_content）。 */
    XDial_init(&self->m_dial, (XWidget*)&self->m_pageInputs, 0);
    demo_set_widget_default_font((XWidget*)&self->m_dial);
    XAbstractSlider_setRange((XAbstractSlider*)&self->m_dial, 0, 100);
    XAbstractSlider_setValue((XAbstractSlider*)&self->m_dial, 40);
    XObject_connect_1((XObject*)&self->m_dial,
                      (size_t)XDial_valueChanged_signal(&self->m_dial, 0),
                      (XObject*)self, demo_tab_dialSlot,
                      XConnectionType_Direct);
    XProgressBar_init(&self->m_dialProgress, (XWidget*)&self->m_pageInputs, 0);
    demo_set_widget_default_font((XWidget*)&self->m_dialProgress);
    XProgressBar_setRange(&self->m_dialProgress, 0, 100);
    XProgressBar_setValue(&self->m_dialProgress, 40);
    XWidget_show((XWidget*)&self->m_dial);
    XWidget_show((XWidget*)&self->m_dialProgress);
#endif
#if XLCDNUMBER_ON && XSCROLLBAR_ON
    /* 数码管 + 滚动条联动：打散归位输入页（几何在 demo_layout_content；
       LCD 定时器停摆门按可见性工作，页隐藏即暂停，语义不变）。 */
    XLcdNumber_init_2(&self->m_lcd, 4u, (XWidget*)&self->m_pageInputs, 0);
    XLcdNumber_display(&self->m_lcd, "0");
    XScrollBar_init(&self->m_scrollBar, (XWidget*)&self->m_pageInputs, 0);
    XAbstractSlider_setRange((XAbstractSlider*)&self->m_scrollBar, 0, 9999);
    XAbstractSlider_setValue((XAbstractSlider*)&self->m_scrollBar, 1888);
    XWidget_show((XWidget*)&self->m_lcd);
    XWidget_show((XWidget*)&self->m_scrollBar);
#endif
#if XDIALOGBUTTONBOX_ON && XPUSHBUTTON_ON
    /* 按钮盒：打散归位按钮演示页（与按钮/命令链接/工具按钮同页）。 */
    XDialogButtonBox_init(&self->m_buttonBox, (XWidget*)&self->m_pageButtons, 0);
    XWidget_setGeometry((XWidget*)&self->m_buttonBox, 40, 250, 340, 40);
    XDialogButtonBox_setStandardButtons(&self->m_buttonBox,
        (int)XDialogButtonBoxStandard_Ok | (int)XDialogButtonBoxStandard_Cancel);
    XWidget_show((XWidget*)&self->m_buttonBox);
#endif
#if XPLAINTEXTEDIT_ON
    /* 多行编辑：打散归位输入页（几何在 demo_layout_content，填左列余高）。 */
    XPlainTextEdit_init(&self->m_plainEdit, (XWidget*)&self->m_pageInputs, 0);
    XPlainTextEdit_setPlainText(&self->m_plainEdit, "多行编辑\n第二行\n第三行");
    XWidget_show((XWidget*)&self->m_plainEdit);
#endif
#if XDATETIMEEDIT_ON && XFONTCOMBOBOX_ON
    /* 日期时间三件套 + 字体下拉：打散归位输入页（三控件同列可对比；
     * 弹层内容按各自 displayFormat 分段构成自动三态：日期时间=日历+
     * 时间行、纯日期=纯日历、纯时间=纯时间设定行）。 */
    XDateTimeEdit_init(&self->m_dtEdit, (XWidget*)&self->m_pageInputs, 0);
    /* 对标 QDateTimeEdit::setCalendarPopup(true)：点下拉箭头弹出日历
     * 弹层（弹层机器见 XDateTimeEdit.c）。 */
    XDateTimeEdit_setCalendarPopup(&self->m_dtEdit, true);
#if XDATEEDIT_ON
    /* 对标 QDateEdit：构造即设 "yyyy/MM/dd"（仅日期段）→弹层=纯日历。 */
    XDateEdit_init(&self->m_dateEdit, (XWidget*)&self->m_pageInputs, 0);
    XDateTimeEdit_setCalendarPopup((XDateTimeEdit*)&self->m_dateEdit, true);
    XWidget_show((XWidget*)&self->m_dateEdit);
#endif
#if XTIMEEDIT_ON
    /* 对标 QTimeEdit：构造即设 "HH:mm:ss"（仅时间段）→弹层=纯时间行。 */
    XTimeEdit_init(&self->m_timeEdit, (XWidget*)&self->m_pageInputs, 0);
    XDateTimeEdit_setCalendarPopup((XDateTimeEdit*)&self->m_timeEdit, true);
    XWidget_show((XWidget*)&self->m_timeEdit);
#endif
    XFontComboBox_init(&self->m_fontCombo, (XWidget*)&self->m_pageInputs, 0);
    XWidget_show((XWidget*)&self->m_dtEdit);
    XWidget_show((XWidget*)&self->m_fontCombo);
#endif
#if XWIDGET_ON && XTABWIDGET_ON && XTABBAR_ON && XCOMBOBOX_ON && \
    XABSTRACTSLIDER_ON && XDIAL_ON && XPROGRESSBAR_ON && XFRAME_ON && XLABEL_ON
    /* ---- 页面 4：容器与窗口（XTabWidget 承载容器/窗口框架族页签；
     * 2026-10-03 打散后仅留 9 签：滚动/分割/工具箱/日历/浏览器/
     * 框架(菜单栏+工具栏+状态栏)/MDI/堆叠组/向导错误） ---- */
    XTabWidget_init(&self->m_tabWidget, (XWidget*)&self->m_pageTabs, 0);
    demo_set_widget_default_font((XWidget*)&self->m_tabWidget);
    /* 页签切换联动状态行（demo_tab_changedSlot 此前从未接线，状态行恒「就绪」）。 */
    XObject_connect_1((XObject*)&self->m_tabWidget,
                      (size_t)XTabWidget_currentChanged_signal(&self->m_tabWidget, 0),
                      (XObject*)self, demo_tab_changedSlot,
                      XConnectionType_Direct);
#if XSCROLLAREA_ON && XABSTRACTSCROLLAREA_ON && XFRAME_ON && XLABEL_ON
    /* 页签 0：XScrollArea。 */
    XScrollArea_init(&self->m_scrollArea, (XWidget*)&self->m_tabWidget, 0);
    XWidget_setGeometry((XWidget*)&self->m_scrollArea, 10, 10, 300, 150);
    {
        XLabel* big = XLabel_create((XWidget*)&self->m_scrollArea, 0);
        XLabel_setText_2(big, "滚动内容\n第二行\n第三行\n第四行\n第五行\n第六行");
        XWidget_resize(big, 260, 200);
        XScrollArea_setWidget(&self->m_scrollArea, (XWidget*)big);
    }
    (void)XTabWidget_insertTab_2(&self->m_tabWidget, 0,
                               demo_wrapTabPage(self, (XWidget*)&self->m_scrollArea), "滚动");
#endif
#if XSPLITTER_ON && XFRAME_ON && XLABEL_ON
    /* 页签 1：XSplitter。 */
    XSplitter_init(&self->m_splitter, (XWidget*)&self->m_tabWidget, 0);
    XWidget_setGeometry((XWidget*)&self->m_splitter, 10, 10, 300, 150);
    {
        XLabel* left = XLabel_create((XWidget*)&self->m_splitter, 0);
        XLabel* right = XLabel_create((XWidget*)&self->m_splitter, 0);
        XLabel_setText_2(left, "左");
        XLabel_setText_2(right, "右");
        XSplitter_addWidget(&self->m_splitter, (XWidget*)left);
        XSplitter_addWidget(&self->m_splitter, (XWidget*)right);
        XWidget_show((XWidget*)left);
        XWidget_show((XWidget*)right);
    }
    (void)XTabWidget_insertTab_2(&self->m_tabWidget, 1,
                               (XWidget*)&self->m_splitter, "分割");
#endif
#if XTOOLBOX_ON && XFRAME_ON && XLABEL_ON
    /* 页签 2：XToolBox。 */
    XToolBox_init(&self->m_toolBox, (XWidget*)&self->m_tabWidget, 0);
    XWidget_setGeometry((XWidget*)&self->m_toolBox, 10, 10, 200, 150);
    {
        XLabel* a = XLabel_create((XWidget*)&self->m_toolBox, 0);
        XLabel* b = XLabel_create((XWidget*)&self->m_toolBox, 0);
        XLabel_setText_2(a, "工具箱页一");
        XLabel_setText_2(b, "工具箱页二");
        XToolBox_addItem(&self->m_toolBox, (XWidget*)a, "页一");
        XToolBox_addItem(&self->m_toolBox, (XWidget*)b, "页二");
        /* 页面显隐由 XToolBox 统一管理（对齐 QToolBox：非当前页隐藏），
           外部不再 show 非当前页。 */
        XWidget_show((XWidget*)a);
    }
    (void)XTabWidget_insertTab_2(&self->m_tabWidget, 2,
                               (XWidget*)&self->m_toolBox, "工具箱");
#endif
#if XMENUBAR_ON && XMENU_ON && XTOOLBAR_ON && XACTION_ON
    /* 页签 3「框架」：XMenuBar + XToolBar + XStatusBar 同页（窗口框架
     * 三件套合并，2026-10-03 打散归位）。 */
    {
        XWidget* mbPage = (XWidget*)XMemory_malloc(sizeof(XWidget), XCLASS_DEFAULT_MEMORY_TYPE);
        if (mbPage) {
            XWidget_init(mbPage, (XWidget*)&self->m_tabWidget, 0);
            Set_Class_Memory(mbPage, XCLASS_DEFAULT_MEMORY_TYPE);
            Set_Class_IsHeap(mbPage, true);
            XMenuBar_init(&self->m_menuBar, mbPage, 0);
            self->m_fileMenu = XMenuBar_addMenu_2(&self->m_menuBar, "文件");
            self->m_editMenu = XMenuBar_addMenu_2(&self->m_menuBar, "编辑");
            if (self->m_fileMenu) XMenu_addAction_2(self->m_fileMenu, "退出");
            if (self->m_editMenu) XMenu_addAction_2(self->m_editMenu, "全选");
            XWidget_setGeometry((XWidget*)&self->m_menuBar, 0, 0, 300, 26);
            XWidget_show((XWidget*)&self->m_menuBar);
            XToolBar_init(&self->m_toolBar, mbPage, 0);
            XToolBar_addAction_2(&self->m_toolBar, "新建");
            XToolBar_addAction_2(&self->m_toolBar, "保存");
            XWidget_setGeometry((XWidget*)&self->m_toolBar, 0, 30, 300, 34);
            XWidget_show((XWidget*)&self->m_toolBar);
#if XSTATUSBAR_ON && XLABEL_ON
            /* 状态栏并入框架页（页底）。 */
            XStatusBar_init(&self->m_sb, mbPage, 0);
            XWidget_setGeometry((XWidget*)&self->m_sb, 10, 74, 350, 24);
            XLabel_init(&self->m_sbLabel, (XWidget*)&self->m_sb, 0);
            XLabel_setText_2(&self->m_sbLabel, "普通区标签");
            XStatusBar_addWidget(&self->m_sb, (XWidget*)&self->m_sbLabel, 1);
            XWidget_show((XWidget*)&self->m_sb);
#endif
            XWidget_setGeometry(mbPage, 0, 0, 400, 290);
            (void)XTabWidget_insertTab_2(&self->m_tabWidget, 3, mbPage, "框架");
        }
    }
#endif
#if XCALENDARWIDGET_ON
    /* 页签 4：XCalendarWidget。 */
    XCalendarWidget_init(&self->m_calendar, (XWidget*)&self->m_tabWidget, 0);
    XWidget_setGeometry((XWidget*)&self->m_calendar, 10, 10, 280, 200);
    (void)XTabWidget_insertTab_2(&self->m_tabWidget, 4,
                               demo_wrapTabPage(self, (XWidget*)&self->m_calendar), "日历");
#endif
#if XTEXTBROWSER_ON
    /* 页签 5：XTextBrowser。 */
    XTextBrowser_init(&self->m_textBrowser, (XWidget*)&self->m_tabWidget, 0);
    XWidget_setGeometry((XWidget*)&self->m_textBrowser, 10, 10, 300, 150);
    XPlainTextEdit_setPlainText(self->m_textBrowser.m_base.m_editor,
        "帮助内容\n第二段\n第三段");
    (void)XTabWidget_insertTab_2(&self->m_tabWidget, 5,
                               (XWidget*)&self->m_textBrowser, "浏览器");
    /* 直插型页签须显式 show（经 demo_wrapTabPage 的页签已带 show；
       页签内容随当前页显示对标 QTabWidget::insertTab 后页面可见语义）。 */
    XWidget_show((XWidget*)&self->m_textBrowser);
#endif
#if XMDIAREA_ON && XFRAME_ON && XLABEL_ON
    /* 页签 6：XMdiArea。 */
    XMdiArea_init(&self->m_mdiArea, (XWidget*)&self->m_tabWidget, 0);
    XWidget_setGeometry((XWidget*)&self->m_mdiArea, 10, 10, 350, 200);
    {
        XLabel* m0 = XLabel_create((XWidget*)&self->m_mdiArea, 0);
        XLabel* m1 = XLabel_create((XWidget*)&self->m_mdiArea, 0);
        XLabel_setText_2(m0, "文档 1");
        XLabel_setText_2(m1, "文档 2");
        XMdiArea_addSubWindow(&self->m_mdiArea, (XWidget*)m0);
        XMdiArea_addSubWindow(&self->m_mdiArea, (XWidget*)m1);
        XWidget_show((XWidget*)m0);
        XWidget_show((XWidget*)m1);
    }
    (void)XTabWidget_insertTab_2(&self->m_tabWidget, 6,
                               (XWidget*)&self->m_mdiArea, "MDI");
    XWidget_show((XWidget*)&self->m_mdiArea); /* 直插型页签显式 show */
#endif
#if XSTACKEDWIDGET_ON && XBUTTONGROUP_ON && XCHECKBOX_ON && XLAYOUT_STACKED_ON
    /* 页签 7：XStackedWidget + XButtonGroup。 */
    XStackedWidget_init(&self->m_stackedW, (XWidget*)&self->m_tabWidget, 0);
    XWidget_setGeometry((XWidget*)&self->m_stackedW, 10, 10, 200, 100);
    XButtonGroup_init(&self->m_btnGroup, NULL);
    XCheckBox_init(&self->m_bgBtn0, (XWidget*)&self->m_stackedW, 0);
    XCheckBox_init(&self->m_bgBtn1, (XWidget*)&self->m_stackedW, 0);
    XAbstractButton_setText_2((XAbstractButton*)&self->m_bgBtn0, "选项 A");
    XAbstractButton_setText_2((XAbstractButton*)&self->m_bgBtn1, "选项 B");
    XWidget_setGeometry((XWidget*)&self->m_bgBtn0, 10, 10, 120, 24);
    XWidget_setGeometry((XWidget*)&self->m_bgBtn1, 10, 40, 120, 24);
    XButtonGroup_addButton(&self->m_btnGroup, (XAbstractButton*)&self->m_bgBtn0, 0);
    XButtonGroup_addButton(&self->m_btnGroup, (XAbstractButton*)&self->m_bgBtn1, 1);
    XWidget_show((XWidget*)&self->m_bgBtn0);
    XWidget_show((XWidget*)&self->m_bgBtn1);
    /* 尾段页签按 index 递增顺序插入（7 堆叠组/8 向导错误）：对标
       QTabWidget::insertTab——index 超过当前页签数时按 Qt 语义收缩为
       "追加"，乱序调用会使实际位次与字面 index 对调。 */
    (void)XTabWidget_insertTab_2(&self->m_tabWidget, 7,
                               demo_wrapTabPage(self, (XWidget*)&self->m_stackedW), "堆叠组");
#endif
#if (defined(XWIZARD_ON) && XWIZARD_ON) || (defined(XERRORMESSAGE_ON) && XERRORMESSAGE_ON)
    /* 页签 8「向导错误」：XWizard + XErrorMessage 同页（流程/提示窗口
       族合并，2026-10-03 打散归位）。弹窗化（2026-10-03 用户裁定「你
       没嵌入进父窗口，找个合适的地方放进去」+ 两窗叠开截图）：二者均
       为 XDialog 派生顶层窗，不再启动即显——XWidget_init 对顶层控件
       预置 WState_Hidden（XWidget.c:2519），构造后本就不可见，此处只
       移除原 show/showMessage 启动即弹调用；点击页签 8 的启动器按钮才
       show（槽 demo_wizOpenSlot/demo_errShowSlot，居中公式见槽注释）。 */
    {
        XWidget* wePage = (XWidget*)XMemory_malloc(sizeof(XWidget),
                                                   XCLASS_DEFAULT_MEMORY_TYPE);
        if (wePage) {
            XWidget_init(wePage, (XWidget*)&self->m_tabWidget, 0);
            Set_Class_Memory(wePage, XCLASS_DEFAULT_MEMORY_TYPE);
            Set_Class_IsHeap(wePage, true);
#if XWIDGET_ON && XSTATUSBAR_ON && XABSTRACTBUTTON_ON && XPUSHBUTTON_ON && \
    XWIZARD_ON && XERRORMESSAGE_ON
            /* 启动器按钮（各 150x28，页签左上 (8,8)/(168,8)）：点「打
               开向导」弹向导、点「显示错误提示」弹提示条（槽见
               demo_wizOpenSlot/demo_errShowSlot）。门控与结构体字段/
               槽函数三处同款，裁剪配置下整组一齐缺席。 */
            XPushButton_init(&self->m_weOpenWizardBtn, wePage, 0);
            demo_set_widget_default_font((XWidget*)&self->m_weOpenWizardBtn);
            XAbstractButton_setText_2((XAbstractButton*)&self->m_weOpenWizardBtn,
                                      "打开向导");
            XWidget_setGeometry((XWidget*)&self->m_weOpenWizardBtn,
                                8, 8, 150, 28);
            XObject_connect_1((XObject*)&self->m_weOpenWizardBtn,
                              (size_t)XAbstractButton_clicked_signal(
                                  (XAbstractButton*)&self->m_weOpenWizardBtn,
                                  false),
                              (XObject*)self, demo_wizOpenSlot,
                              XConnectionType_Direct);
            XWidget_show((XWidget*)&self->m_weOpenWizardBtn);
            XPushButton_init(&self->m_weShowErrBtn, wePage, 0);
            demo_set_widget_default_font((XWidget*)&self->m_weShowErrBtn);
            XAbstractButton_setText_2((XAbstractButton*)&self->m_weShowErrBtn,
                                      "显示错误提示");
            XWidget_setGeometry((XWidget*)&self->m_weShowErrBtn,
                                168, 8, 150, 28);
            XObject_connect_1((XObject*)&self->m_weShowErrBtn,
                              (size_t)XAbstractButton_clicked_signal(
                                  (XAbstractButton*)&self->m_weShowErrBtn,
                                  false),
                              (XObject*)self, demo_errShowSlot,
                              XConnectionType_Direct);
            XWidget_show((XWidget*)&self->m_weShowErrBtn);
#endif
#if XWIZARD_ON && XLABEL_ON
            /* 向导装配保留（构造挂 wePage 父下，仅不再启动即显：无
               XWidget_show——顶层窗 WState_Hidden 待点击弹出）。 */
            XWizard_init(&self->m_wizard, wePage, 0);
            XWizardPage_init(&self->m_wizPage0, (XWidget*)&self->m_wizard, 0);
            XWizardPage_init(&self->m_wizPage1, (XWidget*)&self->m_wizard, 0);
            XWizardPage_init(&self->m_wizPage2, (XWidget*)&self->m_wizard, 0);
            {
                XLabel* w0 = XLabel_create((XWidget*)&self->m_wizPage0, 0);
                XLabel_setText_2(w0, "Step 1");
                XLabel* w1 = XLabel_create((XWidget*)&self->m_wizPage1, 0);
                XLabel_setText_2(w1, "Step 2");
                XLabel* w2 = XLabel_create((XWidget*)&self->m_wizPage2, 0);
                XLabel_setText_2(w2, "Done");
            }
            XWizardPage_setTitle(&self->m_wizPage0, "Step 1");
            XWizardPage_setTitle(&self->m_wizPage1, "Step 2");
            XWizardPage_setTitle(&self->m_wizPage2, "Finish");
            XWizardPage_setSubTitle(&self->m_wizPage0, "基本信息");
            XWizardPage_setSubTitle(&self->m_wizPage1, "高级选项");
            XWizardPage_setSubTitle(&self->m_wizPage2, "完成向导");
            XWizard_addPage(&self->m_wizard, &self->m_wizPage0);
            XWizard_addPage(&self->m_wizard, &self->m_wizPage1);
            XWizard_addPage(&self->m_wizard, &self->m_wizPage2);
            /* 最小尺寸兜底：按钮行五槽推导需约 470px 宽（bw=80×5+
               gap=6×4+边距 8×2），页几何需横幅+按钮带以上可容内容；
               窗口被拖小于下限时按钮/页内容不再互相叠裁。 */
            XWidget_setMinimumSize((XWidget*)&self->m_wizard, 480, 320);
            /* 原 XWidget_setGeometry(wizard, 8, 8, 440, 220) 撤销：向导
               是独立顶层窗，弹出几何由 demo_wizOpenSlot 居中公式给出
               （内置尺寸 480x320）；440x220 是误按子控件摆的。 */
#endif
#if XERRORMESSAGE_ON
            /* 错误提示条装配保留（构造挂 wePage 父下，仅不再启动即
               显：原 showMessage+show 撤销——showMessage 本身瞬时提示
               条入口，点击「显示错误提示」才置文本并弹出）。 */
            XErrorMessage_init(&self->m_errMsg, wePage, 0);
            /* 原 XWidget_setGeometry(errMsg, 8, 240, 300, 120) 撤销：
               xerr_updateSize 在 init 时已按内容定固定尺寸（300x40 下
               锚），弹出几何由 demo_errShowSlot 居中公式给出。 */
#endif
            (void)XTabWidget_insertTab_2(&self->m_tabWidget, 8, wePage,
                                       "\xE5\x90\x91\xE5\xAF\xBC\xE9\x94\x99\xE8\xAF\xAF"); /* 向导错误 */
        }
    }
#endif
#if XTABLEWIDGET_ON && XLAYOUT_ON && XLAYOUT_STACKED_ON
    /* 大表格：打散归位条目视图页（页签 20 表格撤销）——挂条目视图页
       根，贴其小表格右侧（几何在 demo_layout_content 随根自适应）；
       条目视图页被裁剪时回落主窗（不挂 tab）。 */
    if (self->m_extPages[0]) {
        XTableWidget_init(&self->m_tableWidget, self->m_extPages[0], 0);
        demo_set_widget_default_font((XWidget*)&self->m_tableWidget);
        XTableWidget_setRowCount(&self->m_tableWidget, 5);
        XTableWidget_setColumnCount(&self->m_tableWidget, 4);
        {
            static const char* const th[] = {"名称", "类型", "大小", "修改时间"};
            static const char* const thv[] = {"1", "2", "3", "4", "5"};
            XTableWidget_setHorizontalHeaderLabels(&self->m_tableWidget, th, 4);
            XTableWidget_setVerticalHeaderLabels(&self->m_tableWidget, thv, 5);
        }
        {
            static const char* const cells[5][4] = {
                {"XWidget.h", "头文件", "48 KB", "2026-09-12"},
                {"XPainter.c", "源文件", "210 KB", "2026-09-11"},
                {"XGuiDemo", "可执行", "1.2 MB", "2026-09-12"},
                {"XGui.md", "文档", "88 KB", "2026-09-10"},
                {"assets", "目录", "--", "2026-09-01"} };
            int r;
            int c;
            for (r = 0; r < 5; ++r)
                for (c = 0; c < 4; ++c)
                    XTableWidget_setText(&self->m_tableWidget, r, c, cells[r][c]);
        }
        XTableWidget_setCurrentCell(&self->m_tableWidget, 0, 0);
        /* 列宽随根宽定版：默认均分把「类型/大小」挤到滚动条带下（目验
           二轮挂项「头文件」末字半切）。可视宽=表格宽−纵向条带 12，
           列合计须 ≤ 可视宽；宽根（面板收起 776→表格 372）用内容定宽
           128/72/60/112=372 恰满，窄根（面板展开表格 ~196）按比例收
           缩保「名称+类型」两主列可读，时间列让横向滚动。 */
        XWidget_show((XWidget*)&self->m_tableWidget);
    }
#endif
#if XCHARTS_ON
    /* ---- 页面 12：图表演示（2026-10-03 自选项卡页独立成页；
     * 折线 + 柱状 + 散点 + 面积 + 平滑线，对标 QChartView）。
     * 页容器已在扩展页注册循环的下标 7 占位入栈（堆叠下标=页索引 12），
     * 此处只装配图表内容。 ---- */
    XChartView_init(&self->m_chartView, &self->m_pageChart, 0);
    demo_set_widget_default_font((XWidget*)&self->m_chartView);
    {
        XChart* chart = XChartView_chart(&self->m_chartView);
        XLineSeries* line = XLineSeries_create();
        XPieSeries* pie = XPieSeries_create();
        int i;
        XChart_setTitle_2(chart, "XinYueC Charts");
        if (line) {
            XAbstractSeries_setName_2(&line->m_base.m_base, "销量");
            for (i = 0; i < 7; ++i)
                XXYSeries_append(line, i, (i * 37) % 50 + 10);
            XChart_addLineSeries(chart, line);
        }
        if (pie) {
            XAbstractSeries_setName_2((XAbstractSeries*)&pie->m_base, "占比");
            XPieSeries_append_2(pie, "A", 30);
            XPieSeries_append_2(pie, "B", 20);
            XPieSeries_append_2(pie, "C", 50);
            /* 饼图与折线共用坐标系会互相遮挡：饼图保留但默认从 demo
               主视图分离（第一版只演示折线/柱状/散点/面积/样条）。 */
            XClassDelete(pie);
        }
        {
            XBarSeries* bar = XBarSeries_create();
            XScatterSeries* sc = XScatterSeries_create();
            XAreaSeries* area = XAreaSeries_create();
            XSplineSeries* sp = XSplineSeries_create();
            if (bar) {
                XAbstractSeries_setName_2(&bar->m_base.m_base, "月销");
                {
                    XBarSet* bset = XBarSet_create_ex_2(
                        XCLASS_DEFAULT_MEMORY_TYPE, "月销");
                    if (bset) {
                        XBarSet_append(bset, 20);
                        XBarSet_append(bset, 45);
                        XBarSet_append(bset, 30);
                        XAbstractBarSeries_append(bar, bset);
                    }
                }
                XChart_addBarSeries(chart, bar);
            }
            if (sc) {
                XAbstractSeries_setName_2(&sc->m_base.m_base, "离散点");
                XXYSeries_append(sc, 0.5, 45);
                XXYSeries_append(sc, 2.5, 25);
                XXYSeries_append(sc, 4.5, 55);
                XXYSeries_setColor(sc, 0xFFD1294Bu);
                XChart_addScatterSeries(chart, sc);
            }
            if (area) {
                XAreaSeries_setName_2(area, "面积");
                XAreaSeries_setBaseValue(area, 0);
                XXYSeries_append(XAreaSeries_upperSeries(area), 3, 15);
                XXYSeries_append(XAreaSeries_upperSeries(area), 4, 28);
                XXYSeries_append(XAreaSeries_upperSeries(area), 5, 20);
                XAreaSeries_setColor(area, 0x5516AFA9u);
                XChart_addAreaSeries(chart, area);
            }
            if (sp) {
                XAbstractSeries_setName_2(&sp->m_base.m_base, "平滑线");
                XXYSeries_append(sp, 1, 40);
                XXYSeries_append(sp, 2, 22);
                XXYSeries_append(sp, 3, 48);
                XXYSeries_setColor(sp, 0xFF8B5AC7u);
                XChart_addSplineSeries(chart, sp);
            }
        }
        XValueAxis_setRange(XChart_axisY(chart), 0, 60);
        }
    XWidget_show((XWidget*)&self->m_chartView);
    {
        /* 切换钮行挂页容器顶部（几何在 demo_layout_content 随窗宽均布）。 */
        const char* texts[5] = {"图例", "网格", "标题", "序列", "范围"};
        XPushButton_init(&self->m_btnLegend, &self->m_pageChart, 0);
        XPushButton_init(&self->m_btnGrid, &self->m_pageChart, 0);
        XPushButton_init(&self->m_btnTitle, &self->m_pageChart, 0);
        XPushButton_init(&self->m_btnSeries, &self->m_pageChart, 0);
        XPushButton_init(&self->m_btnRange, &self->m_pageChart, 0);
        demo_set_widget_default_font((XWidget*)&self->m_btnLegend);
        demo_set_widget_default_font((XWidget*)&self->m_btnGrid);
        demo_set_widget_default_font((XWidget*)&self->m_btnTitle);
        demo_set_widget_default_font((XWidget*)&self->m_btnSeries);
        demo_set_widget_default_font((XWidget*)&self->m_btnRange);
        XAbstractButton_setText_2((XAbstractButton*)&self->m_btnLegend, texts[0]);
        XAbstractButton_setText_2((XAbstractButton*)&self->m_btnGrid, texts[1]);
        XAbstractButton_setText_2((XAbstractButton*)&self->m_btnTitle, texts[2]);
        XAbstractButton_setText_2((XAbstractButton*)&self->m_btnSeries, texts[3]);
        XAbstractButton_setText_2((XAbstractButton*)&self->m_btnRange, texts[4]);
        XWidget_setGeometry((XWidget*)&self->m_btnLegend, 5, 5, 52, 22);
        XWidget_setGeometry((XWidget*)&self->m_btnGrid, 62, 5, 52, 22);
        XWidget_setGeometry((XWidget*)&self->m_btnTitle, 119, 5, 52, 22);
        XWidget_setGeometry((XWidget*)&self->m_btnSeries, 176, 5, 52, 22);
        XWidget_setGeometry((XWidget*)&self->m_btnRange, 233, 5, 52, 22);
        XObject_connect_1((XObject*)&self->m_btnLegend,
                          (size_t)XAbstractButton_clicked_signal((XAbstractButton*)&self->m_btnLegend, false),
                          (XObject*)self, demo_chartLegendSlot, XConnectionType_Direct);
        XObject_connect_1((XObject*)&self->m_btnGrid,
                          (size_t)XAbstractButton_clicked_signal((XAbstractButton*)&self->m_btnGrid, false),
                          (XObject*)self, demo_chartGridSlot, XConnectionType_Direct);
        XObject_connect_1((XObject*)&self->m_btnTitle,
                          (size_t)XAbstractButton_clicked_signal((XAbstractButton*)&self->m_btnTitle, false),
                          (XObject*)self, demo_chartTitleSlot, XConnectionType_Direct);
        XObject_connect_1((XObject*)&self->m_btnSeries,
                          (size_t)XAbstractButton_clicked_signal((XAbstractButton*)&self->m_btnSeries, false),
                          (XObject*)self, demo_chartSeriesSlot, XConnectionType_Direct);
        XObject_connect_1((XObject*)&self->m_btnRange,
                          (size_t)XAbstractButton_clicked_signal((XAbstractButton*)&self->m_btnRange, false),
                          (XObject*)self, demo_chartRangeSlot, XConnectionType_Direct);
        XWidget_show((XWidget*)&self->m_btnLegend);
        XWidget_show((XWidget*)&self->m_btnGrid);
        XWidget_show((XWidget*)&self->m_btnTitle);
        XWidget_show((XWidget*)&self->m_btnSeries);
        XWidget_show((XWidget*)&self->m_btnRange);
    }
#endif
#endif

    XLabel_init(&self->m_tabStatus, (XWidget*)&self->m_pageTabs, 0);
    demo_set_widget_default_font((XWidget*)&self->m_tabStatus);
    XLabel_setText_2(&self->m_tabStatus, "\xE5\xB0\xB1\xE7\xBB\xAA"); /* 就绪 */
    XWidget_show((XWidget*)&self->m_tabWidget);
    XWidget_show((XWidget*)&self->m_inputStatus);
    /* 放到最低层避免挡住 tab 按钮 */
    XWidget_lower((XWidget*)&self->m_inputStatus);
#endif /* 容器与窗口页签节（XTabWidget 门） */
#if XWIDGET_ON && XFRAME_ON && XLABEL_ON
    /* 底部状态栏文本（深灰背景由静态场景绘制，白字覆盖其上）。 */
    DemoStatusLabel_init(&self->m_statusLabel, &self->m_base, 0);
    demo_set_widget_default_font((XWidget*)&self->m_statusLabel);
    XLabel_setText_2((XLabel*)&self->m_statusLabel, "就绪");
    XLabel_setTextPixelSize((XLabel*)&self->m_statusLabel, 14);
    XLabel_setAlignment((XLabel*)&self->m_statusLabel,
                        XAlignment_Left | XAlignment_VCenter);
    /* 水平缩进用 indent（16px）：margin 是四边统一的内边距，26px 高的
     * 状态条减去上下各 16 后内容区高度为负，VCenter 会把文字压出底边。 */
    XLabel_setIndent((XLabel*)&self->m_statusLabel, 16);
    XWidget_setForegroundRole((XWidget*)&self->m_statusLabel,
                              XPaletteColorRole_HighlightedText);
    XWidget_setGeometry((XWidget*)&self->m_statusLabel, 0, 334, 520, 26);
    XWidget_show((XWidget*)&self->m_statusLabel);
#endif
#if XWIDGET_ON && XLAYOUT_ON && XLAYOUT_STACKED_ON
    /* 内层堆叠（页面 2 堆叠演示）初始几何；主内容区几何由 startup 尾部
       demo_layout_content(win) 统一定版。 */
    {
        XRect inner;
        XRect_init(&inner, 40, 44, 320, 110);
        XLayoutItem_setGeometry_base((XLayoutItem*)&self->m_stackLayoutInner,
                                     &inner);
    }
    /* 经 demo_switchPage 落初始页：导航钮 checked 高亮/状态栏/内容区
       几何三件同步（直呼 setCurrentIndex 会漏高亮，2026-10-03 目验
       page0 挂项根因）。 */
    demo_switchPage(self, 0);
#endif
#if XGUI_PERFORMANCE_OVERLAY_ON && XWIDGET_ON && XFRAME_ON && XLABEL_ON
    /* 压轴层级：先提升状态栏（盖住越界伸入的内容控件），再提升悬浮窗
       （悬浮窗保持最顶层）。 */
    XWidget_raise((XWidget*)&self->m_statusLabel);
    XWidget_raise((XWidget*)&self->m_performanceOverlay);
    XWidget_show((XWidget*)&self->m_performanceOverlay);
#endif
    return self;
}

volatile unsigned long g_overlayTimerFires;
/* ==================== 主函数 ==================== */

/* --apitest 无头测试套件开关：宿主回归用；嵌入式固件置 0 连同 9 个
 * xgui_demo_apitest_*.c 一并裁掉（约 150KB ROM + 20KB 静态视图缓存）。 */
#ifndef XGUI_DEMO_APITEST_ON
#define XGUI_DEMO_APITEST_ON 1
#endif

#if XGUI_DEMO_APITEST_ON
#define XGUI_DEMO_APITEST_DECL(fn) fn
#else
#define XGUI_DEMO_APITEST_DECL(fn) 0
#endif

/** @brief 控件 API 全量测试调度（--apitest）：逐族运行 xgui_demo_apitest.h
 *         契约的 9 个测试族，汇总失败数（任一失败退出码 1）。无头运行，
 *         不创建演示窗口；族内事件经 XObject_event_base 直发。 */
#if XGUI_DEMO_APITEST_ON
static int demo_apitest_run(XGuiApplication* app, const char* onlyFamily)
{
    struct {
        const char* name;
        int (*run)(void);
    } const kSuites[] = {
        { "buttons",    xapi_buttons_run },
        { "labels",     xapi_labels_run },
        { "input",      xapi_input_run },
        { "text",       xapi_text_run },
        { "views",      xapi_views_run },
        { "containers", xapi_containers_run },
        { "dialogs",    xapi_dialogs_run },
        { "menus",      xapi_menus_run },
        { "core",       xapi_core_run },
    };
    int total = 0;
    int i;
    (void)app;
    for (i = 0; i < (int)(sizeof(kSuites) / sizeof(kSuites[0])); ++i) {
        int failures;
        if (onlyFamily && strcmp(kSuites[i].name, onlyFamily) != 0)
            continue;
        failures = kSuites[i].run();
        XPrintf("XGuiApiTest: 套件%-12s %s（失败=%d）\n", kSuites[i].name,
                failures == 0 ? "PASS" : "FAIL", failures);
        total += failures;
    }
    XPrintf("XGuiApiTest: 总计失败=%d %s\n", total,
            total == 0 ? "ALL PASS" : "HAS FAILURES");
    return total == 0 ? 0 : 1;
}
#endif /* XGUI_DEMO_APITEST_ON */
/* GL 驱动 PBO 滞后通道的场景自适应查询（薄包装：有鼠标抓取=交互序列
 * 中，返回非 0 让驱动回同步直读；XWidget_mouseGrabber 返回指针）。 */
static int demo_pointerGrabQuery(void)
{
    return XWidget_mouseGrabber() != NULL;
}

/**
 * @brief      事件处理器诊断日志：vprintf 后立即 fflush(stdout)。
 * @details    printf 与 XPrintf→fwrite 共锁共缓冲；现场挂起冻结点正被
 *             1KB 冲刷边界腰斩（stdout 终止于 enter 行 'global=(9'——
 *             %d 中途），证明重定向下 UCRT 忽略 _IOLBF、实际全缓冲。
 *             事件处理器诊断全部改走此处：每次调用即时落盘，下次挂起
 *             时 stdout 末行即精确指向最后完成的处理器；共享流一旦
 *             进入错误态，fflush 返回非 0 立即暴露，不再静默吞掉全部
 *             后续诊断。输出文本与原裸 printf 逐字节一致。
 * @param      fmt printf 风格格式串。
 * @return     vprintf 写出字符数；fflush 失败（共享流错误态）返回 EOF，
 *             并置 g_logDisabled 熔断，后续调用短路返回 0。
 */
static int demo_log(const char* fmt, ...)
{
    va_list args;
    int written;
    if (g_logDisabled)
        return 0; /* 已熔断：诊断静默短路（防对失效句柄逐次阻塞重试）。 */
#if defined(__ANDROID__)
    /* 安卓 NativeActivity 下 stdout FILE* 半初始化，vfprintf 直接 SEGV
       （真机实测，每次点空白区即崩）。诊断行已由各探针点直接走
       XGuiDemo_debugLog/logcat，此处整体短路不碰 stdout。 */
    return 0;
#endif
    va_start(args, fmt);
    written = vprintf(fmt, args);
    va_end(args);
    if (written < 0 || fflush(stdout) != 0)
    {
        /* wave6 管道挂起取证（diag/wave6/hang/pipe_hang_stacks.txt）：UI
         * 线程冻结于本函数 fflush→WriteFile 的满管道内核写等待。写一旦
         * 实际失败（如读端断开返回 EOF）即永久熔断，不再对死流重试。 */
        g_logDisabled = true;
        return EOF; /* 共享流损坏/错误态：显式暴露，不静默丢弃。 */
    }
    return written;
}

#ifdef _WIN32
/* 挂起缓解②所需（SetConsoleMode/GetStdHandle）。放在 main 前而非文件
 * 头：windows.h 的 IN/OUT/TRUE 等宏只波及其后代码，不污染上方 3 千余行
 * 与全部 XGui 头。 */
#include <windows.h>
#endif

/* 安卓 APK 壳（Drive/Android/android_main.c）经 xgui_window_demo_main
 * 复用同一 main 逻辑；嵌入式固件保留 xgui_demo_main 原名（由板级任务
 * 调用，main 属启动文件）；桌面仍导出标准 main。 */
#ifdef __ANDROID__
#define xgui_demo_main xgui_window_demo_main
#elif defined(XINYUE_EMBEDDED)
/* 板级固件经 xgui_demo_main(0, NULL) 调用（见 Drive/STM32/ShenzhouF407）。 */
#else
#define xgui_demo_main main
#endif

/* 平台后置钩子：窗口 show 之后、事件循环之前调用一次。嵌入式板级
 * （Drive/STM32/ShenzhouF407/app/board_xgui_glue.c）以强符号覆盖，
 * 在此挂后备存储 present 上屏回调与触摸轮询泵；宿主平台走弱空实现。 */
void xgui_demo_platformPostShow(XWidget* topLevel);
#if defined(__GNUC__)
__attribute__((weak))
#endif
void xgui_demo_platformPostShow(XWidget* topLevel)
{
    (void)topLevel;
}

int xgui_demo_main(int argc, char* argv[])
{
    XGuiApplication* app;
    DemoWin* win;
    int autoSeconds;
    int benchmarkSeconds;
    bool benchmarkResize;
    bool benchmarkMaximized;
    const char* screenshotPath;
    const char* styleOpt;
    int screenshotPage;
    int screenshotTab;
    bool autoTest;
    bool apiTestSuite;
    const char* apiTestFamily;
    int argi;
    int eventLoopResult;
    DemoRemoteServerCliOptions remoteCli; /* --remote-server 参数族初始默认值。 */
    DemoRemoteClientCliOptions remoteClientCli; /* --remote-client 参数族初始默认值(2026-10-02)。 */

    /* 交互模式可观测性（非 TTY 重定向）：stdout 缺省全缓冲（glibc 对
     * 非终端重定向挂 4~8K 块缓冲），XPrintf 底层走 fwrite(stdout)，
     * kill -9 不经 atexit/stdio 清理，缓冲区未满的启动段日志整段丢
     * 失（夜间活锁检测假阴性根因之一：日志只见 stderr 一行）。启动
     * 即切行缓冲——每个 '\n' 自动 flush，对标 C 运行库 TTY 缺省口径；
     * 仅作用于本演示进程，不影响库与其他可执行。
     * 【合并修复 2026-09-29】size 参数 0 在 MSVC UCRT 触发 setvbuf 断言
     * （2 <= size <= INT_MAX，debug CRT 弹模态框挂死 main——桌面全后端
     * bench/autotest 挂死根因）；glibc 口径 size=0 合法。改传 1024。
     * 【修复 2026-10-03】_IOLBF 在 UCRT 重定向下被忽略（见 demo_log 声
     * 明处注释：仍按全缓冲 1KB 腰斩）——F3 验收实测 kill 强杀后 XPrintf
     * 探针行整段丢失、仅 demo_log 的显式 fflush 把先行缓冲带出。切
     * _IONBF 全无缓冲：每个 XPrintf 即写即达，harness 探针行不再依赖
     * flush 运气（本行即 2165 注释所述问题的最终收口）。 */
    setvbuf(stdout, NULL, _IONBF, 1024);
    /* 【内存驻留治理 2026-10-06】glibc 阈值调优: 换页/拖动/编码每步 churn
     * 1-3MB 分配释放(静态场景重建+tile 差带+镜像编码), 缺省阈值下 glibc
     * 把释放块滞留主堆不还系统——A33(56MB 无 swap)实测导航点击 RSS
     * +125MB 不回落直至 OOM(用户实测吃满)。mmap 阈值 128KB=大块走
     * mmap/munmap 即时归还; trim 阈值同步收紧。仅影响本进程。 */
    mallopt(M_TRIM_THRESHOLD, 128 * 1024);
    mallopt(M_MMAP_THRESHOLD, 128 * 1024);
    {
        /* 虚拟键盘面板开关(联调口; XPWN_VK=none/0 关——远程注入聚焦编辑框
         * 时面板会遮挡页面, 对齐平台层 XPWN_IME 环境约定纪律)。 */
        const char* vk = XSystem_environment("XPWN_VK");
        if (vk && (strcmp(vk, "none") == 0 || strcmp(vk, "0") == 0))
            XGuiApplication_setVirtualKeyboardEnabled(false);
    }
#ifdef _WIN32
    /* 挂起缓解③（wave7 六波实锤：满管道内核级无限阻塞，冻结栈
     * NtWriteFile←WriteFile←fflush←demo_log——diag/wave7/pipe/）：
     * stdout 被重定向进命名管道且读端连而不读时，管道缓冲写满后
     * WriteFile 在内核无限等待；g_logDisabled 熔断只覆盖 EOF/错误
     * 返回路径，控制权滞留内核时熔断无从触发。改用管道非阻塞模式：
     * 写满即以 ERROR_NO_DATA 立即失败返回，fflush 得以回到用户态
     * 置熔断，后续诊断短路。只对 FILE_TYPE_PIPE 句柄做——控制台
     * （FILE_TYPE_CHAR）与文件重定向（FILE_TYPE_DISK）语义不变；
     * PIPE_NOWAIT 的读语义变化只波及本进程自己的日志写句柄。 */
    {
        HANDLE stdOut = GetStdHandle(STD_OUTPUT_HANDLE);
        if (stdOut && stdOut != INVALID_HANDLE_VALUE &&
            GetFileType(stdOut) == FILE_TYPE_PIPE)
        {
            DWORD pipeMode = PIPE_READMODE_BYTE | PIPE_NOWAIT;
            SetNamedPipeHandleState(stdOut, &pipeMode, NULL, NULL);
        }
    }
    /* 挂起缓解②（用户真实部署形态：demo 自带控制台窗口长时间挂机）：
     * 用户在控制台窗口拖选文本会让 conhost 冻结写入方（与 wave6 管道
     * 满写冻结同签名：UI 线程停在 fflush→WriteFile）。先用 STD_OUTPUT_
     * HANDLE 判定"未重定向态"——GetConsoleMode 对文件/管道句柄失败，
     * 成功即意味着 demo 有自己的控制台窗口。QuickEdit 标志位于控制台
     * 输入缓冲：SetConsoleMode 须落在 STD_INPUT_HANDLE（在输出句柄上
     * 设 QUICKEDIT 位实测 err=87，diag/wave7 配方核对器），stdin 若被
     * 重定向则回退 CONIN$ 打开本控制台的输入缓冲。清除 QUICKEDIT 位
     * 时必须保留/置上 ENABLE_EXTENDED_FLAGS（MSDN 规定）。 */
    {
        HANDLE consoleOut = GetStdHandle(STD_OUTPUT_HANDLE);
        DWORD consoleMode;
        if (consoleOut && consoleOut != INVALID_HANDLE_VALUE &&
            GetConsoleMode(consoleOut, &consoleMode))
        {
            HANDLE consoleIn = GetStdHandle(STD_INPUT_HANDLE);
            DWORD inputMode;
            if (!consoleIn || consoleIn == INVALID_HANDLE_VALUE ||
                !GetConsoleMode(consoleIn, &inputMode))
            {
                consoleIn = CreateFileW(L"CONIN$",
                                        GENERIC_READ | GENERIC_WRITE,
                                        FILE_SHARE_READ | FILE_SHARE_WRITE,
                                        NULL, OPEN_EXISTING, 0, NULL);
                if (!consoleIn || consoleIn == INVALID_HANDLE_VALUE ||
                    !GetConsoleMode(consoleIn, &inputMode))
                    inputMode = 0;
            }
            if (inputMode)
                SetConsoleMode(consoleIn,
                               (inputMode & ~ENABLE_QUICK_EDIT_MODE) |
                               ENABLE_EXTENDED_FLAGS);
        }
    }
#endif
    /* GL 驱动 PBO 滞后通道的场景自适应门控：注入 mouse-grab 查询（有
     * 抓取=交互序列中，驱动回同步直读保正确性；无抓取=非交互态允许
     * 滞后拷出换吞吐）。零抓取时行为与第三夜二分口径一致。 */
    XGpuRenderDriver_gl_setPointerGrabQuery(demo_pointerGrabQuery);
    autoSeconds = 0;
    benchmarkSeconds = 0;
    benchmarkResize = false;
    benchmarkMaximized = false;
    screenshotPath = NULL;
    styleOpt = NULL;
    autoTest = false;
    apiTestSuite = false;
    apiTestFamily = NULL;
    screenshotPage = 0;
    screenshotTab = -1;
    memset(&remoteCli, 0, sizeof(remoteCli)); /* --remote-* 全默认。 */
    memset(&remoteClientCli, 0, sizeof(remoteClientCli)); /* --remote-client 族全默认。 */
    for (argi = 1; argi < argc; ++argi) {
        if (strcmp(argv[argi], "--benchmark") == 0 && argi + 1 < argc) {
            benchmarkSeconds = atoi(argv[++argi]);
            benchmarkResize = false;
        }
        else if (strcmp(argv[argi], "--benchmark-resize") == 0 &&
                 argi + 1 < argc) {
            benchmarkSeconds = atoi(argv[++argi]);
            benchmarkResize = true;
        }
        else if (strcmp(argv[argi], "--maximized") == 0) {
            benchmarkMaximized = true;
        }
        else if (strcmp(argv[argi], "--benchmark-full") == 0) {
            g_benchmarkFullRedraw = true;
        }
        else if (strcmp(argv[argi], "--gpu") == 0) {
#if XPLATFORMINTEGRATION_ON && XGPU_ON
            /* GPU 直通显式开关：请求 GPU 渲染后端（窗口直通会话，
               会话创建失败自动回退软件——xgpu_create_ex 有序回退链）。
               对标 QtQuick 的 QSG_RHI 切换：显式选择优于静默自动。 */
            XGpuRenderBackend_addRequestedOverride(true);
#endif
        }
        else if (strcmp(argv[argi], "--software") == 0 ||
                 strcmp(argv[argi], "--sw") == 0) {
#if XPLATFORMINTEGRATION_ON && XGPU_ON
            /* 软件渲染显式开关：覆盖桌面默认 GPU 口径（XGPU_RUNTIME_
               DEFAULT_ON）与 XGUI_RENDER_BACKEND 环境变量——性能对比/
               回归软件基线用（--gpu 的对称反向）。 */
            XGpuRenderBackend_addRequestedOverride(false);
#endif
        }
        else if (strcmp(argv[argi], "--screenshot") == 0 &&
                 argi + 1 < argc) {
            screenshotPath = argv[++argi];
        }
        else if (strcmp(argv[argi], "--page") == 0 && argi + 1 < argc) {
            screenshotPage = atoi(argv[++argi]);
        }
        else if (strcmp(argv[argi], "--tab") == 0) {
            screenshotTab = atoi(argv[++argi]);
        }
        else if (strncmp(argv[argi], "--style=", 8) == 0) {
            /* 样式矩阵开关：common=框架默认样式（无 CSS 覆盖）/
             * fusion=仅 Fusion / fusion-css=Fusion+样式表（缺省，历史口径）。 */
            styleOpt = argv[argi] + 8;
        }
        else if (strcmp(argv[argi], "--apitest") == 0 ||
                 strncmp(argv[argi], "--apitest=", 10) == 0) {
            apiTestSuite = true;
            apiTestFamily = strcmp(argv[argi], "--apitest") == 0
                                ? NULL
                                : argv[argi] + 10;
        }
        else if (strcmp(argv[argi], "--autotest") == 0) {
            autoTest = true;
        }
        else if (strcmp(argv[argi], "--remote-server") == 0) {
            /* 远程窗口设置页 CLI 初始默认值族（主控路径是 UI; 便于脚本
               联调, 见 xgui_demo_page_remote_server.h）。 */
            remoteCli.enabled = true;
        }
        else if (strcmp(argv[argi], "--remote-port") == 0 &&
                 argi + 1 < argc) {
            remoteCli.port = atoi(argv[++argi]);
        }
        else if (strcmp(argv[argi], "--remote-profile") == 0 &&
                 argi + 1 < argc) {
            remoteCli.profile = argv[++argi];
        }
        else if (strcmp(argv[argi], "--remote-tls") == 0 &&
                 argi + 1 < argc) {
            remoteCli.tls = strcmp(argv[++argi], "0") != 0;
        }
        else if (strcmp(argv[argi], "--remote-cert") == 0 &&
                 argi + 1 < argc) {
            remoteCli.cert = argv[++argi];
        }
        else if (strcmp(argv[argi], "--remote-key") == 0 &&
                 argi + 1 < argc) {
            remoteCli.key = argv[++argi];
        }
        else if (strcmp(argv[argi], "--remote-password") == 0 &&
                 argi + 1 < argc) {
            remoteCli.password = argv[++argi];
        }
        else if (strcmp(argv[argi], "--remote-auth") == 0 &&
                 argi + 1 < argc) {
            remoteCli.auth = argv[++argi];
        }
        else if (strcmp(argv[argi], "--remote-client") == 0) {
            /* 远程客户端页 CLI 初始默认值族（2026-10-02 追加; 主控路径
               是页面 UI, 便于脚本联调, 见 xgui_demo_page_remote_client.h）。 */
            remoteClientCli.enabled = true;
        }
        else if (strcmp(argv[argi], "--remote-host") == 0 &&
                 argi + 1 < argc) {
            remoteClientCli.host = argv[++argi];
        }
        /* --remote-port/--remote-profile/--remote-tls/--remote-password/
         * --remote-auth 双角色共用（按是否给 --remote-client 分派到
         * 客户端页预置; 仅 --remote-server 时归服务器页）。 */
        else {
            autoSeconds = atoi(argv[argi]);
        }
    }
    /* 共用词分派：客户端预置请求存在时, 端口/档位/TLS/口令/认证 同时
       喂给客户端页（服务器页保持原语义, 未给 --remote-server 时其
       enabled=false 不会自动监听, 词被客户端角色借用无害）。 */
    if (remoteClientCli.enabled) {
        remoteClientCli.port = remoteCli.port;
        remoteClientCli.profile = remoteCli.profile;
        remoteClientCli.tls = remoteCli.tls;
        remoteClientCli.password = remoteCli.password;
        remoteClientCli.auth = remoteCli.auth;
    }
    demo_page_remote_server_cliDefaults(&remoteCli);
    demo_page_remote_client_cliDefaults(&remoteClientCli);
    if (autoSeconds < 0) autoSeconds = 0;
    if (benchmarkSeconds < 0) benchmarkSeconds = 0;

    /* 交互空闲闸门环境变量回退开关：XGUI_DEMO_IDLE_GATE=0 回退旧「帧泵
       每轮 processEvents 强制重绘 + 悬浮层随帧刷新」口径（默认开）。
       与 XGPU_PRESENT_MAX_FPS 同族：一次性读取、进程内生效。 */
    {
        const char* idleGate = XSystem_environment("XGUI_DEMO_IDLE_GATE"); /* 库内唯一环境入口（禁直呼 getenv）。 */
        g_idleGate = !(idleGate && idleGate[0] == '0');
    }

#if XSTYLE_ON
    /* 样式矩阵开关：fusion-css=Fusion+样式表（缺省，历史口径）/ fusion=仅
     * Fusion / common=框架默认样式。须在参数解析后执行（读 styleOpt）。 */
    if (!styleOpt || strcmp(styleOpt, "fusion-css") == 0) {
        XFusionStyle_installDefault();
        XStyle_installStyleSheet(xgui_demo_theme_css);
    }
    else if (strcmp(styleOpt, "fusion") == 0) {
        XFusionStyle_installDefault();
    }
    /* common：保持框架默认样式，供样式矩阵对照。 */
#endif

    /* 1) 初始化 XGuiApplication（进程内单例）。 */
    {
        /* [mem] 堆水印插桩属固件战役（__FreeRTOS__ 由 CMakeLists 嵌入式
         * 分支全局定义）：桌面目标不编 FreeRTOS 堆符号，无守卫直呼会
         * 造成 XGuiWindowDemo_Test 链接 LNK2019。 */
#if defined(__FreeRTOS__)
        extern size_t xPortGetFreeHeapSize(void);
        XPrintf("[mem] before app: free=%u\n",
                (unsigned)xPortGetFreeHeapSize());
#endif
    }
    app = XGuiApplication_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, argc, argv);
    if (!app) {
        XPrintf("XGuiWindowDemo: XGuiApplication_create_ex 失败\n");
        return 1;
    }

#if XGUI_DEMO_APITEST_ON
    /* 1.5) --apitest：控件 API 全量测试（无头，不进窗口流程）。 */
    if (apiTestSuite) {
        int rc = demo_apitest_run(app, apiTestFamily);
        XClassDelete(app);
        return rc;
    }
#else
    (void)apiTestSuite; (void)apiTestFamily;
#endif

#if defined(__linux__) && XGUI_ON && XPLATFORM_FBDEV_ON
    /* 面板几何变量（注册后 probe 赋值）；0=非 fbdev/不可用，窗口走
     * 桌面默认几何。供下方窗口全屏化使用（嵌入式单屏无合成器）。
     * 注意：必须等下方 useFramebufferDriver 注册完成后再 probe——
     * active() 在注册前返回 NULL。 */
    int fbPanelW = 0;
    int fbPanelH = 0;
    /* 1.6) 嵌入式 fbdev 板级引导（仅交叉构建注入 XPLATFORM_FBDEV_ON 时
       编译；桌面 Linux 构建开关缺省为 0，此块预处理裁剪，零影响）：
       - 显示驱动：GUI 单例就绪后、首窗显示前注册 /dev/fb0 直写驱动
         （XGUI_BACKINGSTORE_IMAGE_FORMAT_RGB16=1 时后备表面与 16 位
         面板格式一致，present 走 memcpy 直写+pan）；
       - 触摸输入：evdev 读泵挂入事件分发器轮询链（依赖分发器就绪，
         故在应用创建之后；设备节点经环境变量 XPLATFORM_FBINPUT_DEVICE
         定制）。无 /dev/fb0 或 /dev/input/event0 的环境注册失败为
         预期路径，应用继续以无输入/无上屏方式运行。 */
    if (XPlatformNativeWindow_useFramebufferDriver(NULL))
        XPrintf("XGuiWindowDemo: fbdev 显示驱动已注册\n");
    else
        XPrintf("XGuiWindowDemo: fbdev 显示驱动不可用（无帧缓冲设备）\n");
    {
        const XPlatformDisplayDriverOps* fbOps = XPlatformDisplayDriver_active();
        XPlatformDisplayInfo fbInfo;
        if (fbOps && fbOps->probe && fbOps->probe(&fbInfo))
        {
            fbPanelW = fbInfo.m_width;
            fbPanelH = fbInfo.m_height;
        }
    }
#if XPLATFORM_FBINPUT_ON
    if (XPlatformFbInput_register())
        XPrintf("XGuiWindowDemo: evdev 触摸输入已注册\n");
    else
        XPrintf("XGuiWindowDemo: 触摸输入不可用（无 evdev 设备）\n");
#endif
#endif

    /* 2) 创建演示窗口并设置标题/几何。show() 会在框架内部惰性创建平台窗口。 */
    win = DemoWin_create();
    if (!win) {
        /* 判空守卫前置：创建失败即收口退出（后续 win-> 字段写入
           不容 NULL 解引用；守卫体必须 return，否则落空继续走）。 */
        XPrintf("XGuiWindowDemo: DemoWin_create 失败\n");
        XClassDelete(app);
        return 1;
    }
#if XWIDGET_ON
    /* 顶层控件字体补设：框架标题栏文本取顶层控件字体，未设时为空字体
       （fbdev 上首帧标题缺失的嫌疑之一）；与子控件同族默认字体。 */
    demo_set_widget_default_font(&win->m_base);
#endif

    win->m_screenshotPath = screenshotPath;
    win->m_screenshotFrames = 0;
    win->m_autoTest = autoTest;
    win->m_autoTestFrames = 0;
    if (autoTest)
        demo_switchPage(win, 3); /* 自动测试固定切第 4 页 */
#if XWIDGET_ON && XLAYOUT_ON && XLAYOUT_STACKED_ON
    /* 截图模式可指定初始页面（配合 --screenshot <file> --page <N>）。 */
    if (screenshotPage > 0)
        demo_switchPage(win, screenshotPage);
    if (screenshotTab >= 0)
        XTabWidget_setCurrentIndex(&win->m_tabWidget, screenshotTab); /* 无需截图模式也能切页。 */
#endif
    {
        XString* title = XString_create_utf8(
            "XinYueC 控件可视化测试");
        XWidget_setWindowTitle(&win->m_base, title);
        XClassDelete((XClass*)title);
    }
/* fbdev 默认与桌面统一：窗口按设计尺寸浮动（不再默认全屏铺满——
 * 用户 2026-09-28 指出与桌面行为不统一；窗口外区域由平台首帧整段
 * 清零兜底，黑色桌面底两缓冲同清不残留）。--maximized 的全屏化在
 * show 后的 fbdev 块执行（还原基准几何存档后再最大化）。 */
#if XWIDGET_ON && XLAYOUT_ON && XLAYOUT_STACKED_ON
    if (win->m_extPages[0] || win->m_extPages[1] ||
        win->m_extPages[2] || win->m_extPages[3] || win->m_extPages[4] ||
        win->m_extPages[5] || win->m_extPages[6] || win->m_extPages[7] ||
        win->m_extPages[8] || win->m_extPages[9])
        XWidget_setGeometry(&win->m_base, 40, 40, 1000, 700); /* 15 页导航/扩展页与网络设置双列布局需要 */
    else
#endif
    XWidget_setGeometry(&win->m_base, 60, 60, 520, 360);
#if defined(XINYUE_PANEL_WIDTH) && defined(XINYUE_PANEL_HEIGHT)
    /* 嵌入式单屏：窗口全屏铺满面板（板级 present 回调直写 GRAM，
     * 面板尺寸经编译定义注入，见 Drive/STM32/ShenzhouF407）。 */
    XWidget_setGeometry(&win->m_base, 0, 0, XINYUE_PANEL_WIDTH,
                        XINYUE_PANEL_HEIGHT);
#endif
    /* 键盘页无头截图钩子（Tools/VirtualKeyboard/style_check.py；环境变量缺省时
       零操作）：几何定版后、事件循环前同步弹出——守护轮询 200ms 在
       --screenshot 3 帧内到不了，钩子不依赖定时器边沿。 */
    demo_page_keyboard_headless_hook();
#if defined(__linux__) && XGUI_ON && XPLATFORM_FBDEV_ON
    if (fbPanelW > 0 && fbPanelH > 0 &&
        (XWidget_x(&win->m_base) + XWidget_width(&win->m_base) > fbPanelW ||
         XWidget_y(&win->m_base) + XWidget_height(&win->m_base) > fbPanelH))
    {
        /* 面板装不下桌面默认落点（1024x600 面板 vs 800x600+40,40）：
         * 保持尺寸、位置钳回面板内（y 贴顶让自带标题栏可见）。 */
        int wx = XWidget_x(&win->m_base);
        int wy = XWidget_y(&win->m_base);
        int ww = XWidget_width(&win->m_base);
        int wh = XWidget_height(&win->m_base);
        if (ww > fbPanelW) { ww = fbPanelW; wx = 0; }
        if (wh > fbPanelH) { wh = fbPanelH; wy = 0; }
        if (wx + ww > fbPanelW) wx = fbPanelW - ww;
        if (wy + wh > fbPanelH) wy = fbPanelH - wh;
        XWidget_setGeometry(&win->m_base, wx, wy, ww, wh);
    }
#endif
#if XWIDGET_ON && XFRAME_ON && XLABEL_ON
    demo_layout_chrome(win);
#endif
#if XWIDGET_ON && XABSTRACTBUTTON_ON && XPUSHBUTTON_ON && XFRAME_ON && \
    XLABEL_ON && XLAYOUT_ON && XLAYOUT_STACKED_ON
    demo_navUpdatePanel(win); /* 面板贴边几何定版（默认左、展开）。 */
#endif
#if XWIDGET_ON && XLAYOUT_ON && XLAYOUT_STACKED_ON
    demo_layout_content(win); /* 内容区按面板占位定版（页内自适应布局随之落位）。 */
#endif

    /* 3) 显示窗口：触发框架内部的惰性平台窗口创建并进入事件循环。 */
#if defined(__linux__) && XGUI_ON && XPLATFORM_FBDEV_ON
    if (fbPanelW > 0 && fbPanelH > 0) {
        /* fbdev（无 WM）：软件窗口状态已在框架内落地（XWindow 无原生
         * 窗路径），showMaximized 与桌面同 API 同语义；几何大变的清屏
         * 由窗口装饰模块的切换路径承接（requestPanelClear 单发）。 */
        if (benchmarkMaximized)
            XWidget_showMaximized(&win->m_base);
        else
            XWidget_showNormal(&win->m_base);
    } else
#endif
    if (benchmarkMaximized)
        XWidget_showMaximized(&win->m_base);
    else
        XWidget_showNormal(&win->m_base);
#if defined(__linux__) && XGUI_ON && XPLATFORM_FBDEV_ON
    if (fbPanelW > 0 && fbPanelH > 0 && !benchmarkMaximized)
    {
        /* 窗口化启动：窗外面板区域铺桌面底色（RGB565 浅灰，与框架
         * 标题栏同源）。无 WM 环境框架即合成器，但面板底没有「桌面」
         * 概念——不铺则左右露黑带，被视作显示不全（真机用户指正
         * 2026-09-28）。与装饰模块拖拽/复原路径的 XWD_DESKTOP_PIXEL
         * 同值（那边为模块内常量，此处独立字面量）。 */
        int gx = XWidget_x(&win->m_base);
        int gy = XWidget_y(&win->m_base);
        int gw = XWidget_width(&win->m_base);
        int gh = XWidget_height(&win->m_base);
        XRect bands[4];
        XRect_init(&bands[0], 0, 0, gx, fbPanelH);              /* 左 */
        XRect_init(&bands[1], gx + gw, 0, fbPanelW - gx - gw,
                   fbPanelH);                                    /* 右 */
        XRect_init(&bands[2], gx, 0, gw, gy);                    /* 上 */
        XRect_init(&bands[3], gx, gy + gh, gw, fbPanelH - gy - gh); /* 下 */
        XPlatformBackingStore_fillPanelRects(bands, 4, 0xEF7Du);
    }
#endif
    {
        /* [mem] 同上：固件战役插桩，桌面分支裁剪。 */
#if defined(__FreeRTOS__)
        extern size_t xPortGetFreeHeapSize(void);
        XPrintf("[mem] after pages: free=%u\n",
                (unsigned)xPortGetFreeHeapSize());
#endif
    }
    xgui_demo_platformPostShow(&win->m_base);
    XPrintf("XGuiWindowDemo: 屏幕尺寸=%.0fx%.0f\n",
           (double)XWidget_width(&win->m_base),
           (double)XWidget_height(&win->m_base));

    if (g_idleGate) {
        /* 空闲闸门下帧泵不再兜底逐轮重绘：显示后主动完成一次整帧请求，
           确保静态场景脏标记（初值 true）随首帧整窗绘制收敛清位——
           否则首帧 Expose 若只覆盖部分矩形，悬浮层降频定时器会一直按
           整帧口径补画（GPU 模式即整窗提交链）。闸门关闭时帧泵本就
           逐轮强制重绘，不加这步保持旧口径逐位一致。 */
        demo_repaint(win);
        XGuiApplication_processEvents(XEventLoop_AllEvents);
    }

    eventLoopResult = 0;
    if (benchmarkSeconds > 0) {
        demo_runFrameBenchmark(win, benchmarkSeconds, benchmarkResize);
    } else {
        /* 把刷新注册到事件分发器轮询链：每次 processEvents 回调一次，
           不再受 1ms 精确定时器粒度限制；事件循环因此也始终有事件可
           处理，不会进入无事件休眠。空闲闸门（XGUI_DEMO_IDLE_GATE，
           默认开）开启时本回调空闲直接返回（无脏区不重绘），仅
           --autotest/--screenshot 旁路逐帧推进；回退开关置 0 恢复
           旧「每轮强制重绘」口径。 */
        win->m_framePump = XAbstractEventDispatcher_addPollCallback(
            demo_framePump, win);
        if (!win->m_framePump) {
            XPrintf("XGuiWindowDemo: 事件循环刷新回调注册失败\n");
            eventLoopResult = 2;
        }
        if (autoSeconds > 0) {
            win->m_autoQuitTimer = XObject_startTimer_ms(
                (XObject*)win, (uint64_t)autoSeconds * 1000u,
                XTimerType_CoarseTimer);
            if (win->m_autoQuitTimer == XTIMER_INVALID_ID) {
                XPrintf("XGuiWindowDemo: 自动退出定时器创建失败\n");
                eventLoopResult = 2;
            }
        }
        /* LCD 数码管自动更新定时器（1 秒间隔循环 0-9）。 */
        win->m_lcdTimer = XObject_startTimer_ms(
            (XObject*)win, 1000u, XTimerType_CoarseTimer);
        if (win->m_lcdTimer == XTIMER_INVALID_ID) {
            XPrintf("XGuiWindowDemo: LCD 定时器创建失败\n");
        }
#if XGUI_PERFORMANCE_OVERLAY_ON && XWIDGET_ON && XFRAME_ON && XLABEL_ON
        /* 空闲闸门开启时：悬浮层指标自刷新降频为独立 4Hz 定时器（帧泵
           已被闸门停用，见 demo_framePumpBody）。demo_repaint 在静态
           场景干净时只投递悬浮层 210x70 小区域，文本经 XLabel 自身
           update 闭环。XGUI_DEMO_IDLE_GATE=0 时不启动（旧口径随帧
           刷新，行为与回退前逐位一致）。 */
        XPrintf("XGuiWindowDemo: idleGate=%d overlay_on=%d\n",
                (int)g_idleGate, (int)XGUI_PERFORMANCE_OVERLAY_ON);
        if (g_idleGate) {
            win->m_overlayTimer = XObject_startTimer_ms(
                (XObject*)win, (uint64_t)XGUI_DEMO_IDLE_OVERLAY_MS,
                XTimerType_CoarseTimer);
            if (win->m_overlayTimer == XTIMER_INVALID_ID)
                XPrintf("XGuiWindowDemo: 悬浮层降频定时器创建失败\n");
            else
                XPrintf("XGuiWindowDemo: 悬浮层定时器已创建\n");
        }
#endif
        if (eventLoopResult == 0)
            eventLoopResult = XGuiApplication_exec();
    }

    XPrintf("XGuiWindowDemo: 退出事件循环（关闭=%s 自动退出=%d 返回=%d）\n",
           win->m_closed ? "是" : "否", autoSeconds, eventLoopResult);

    /* 4) 清理：窗口销毁自动拆除原生窗口；应用单例回收集成层。 */
    demo_stopTimers(win);
#if XWIDGET_ON && XFRAME_ON && XLABEL_ON
    XClassDeinit((XLabel*)&win->m_statusLabel);
    XClassDeinit(&win->m_titleLabel);
#endif
#if XWIDGET_ON && XPUSHBUTTON_ON
    XClassDeinit(&win->m_button);
#endif
#if XWIDGET_ON && XABSTRACTBUTTON_ON && XPUSHBUTTON_ON && XCOMMANDLINKBUTTON_ON
    XClassDeinit(&win->m_commandLink);
#endif
#if XWIDGET_ON && XABSTRACTBUTTON_ON && XTOOLBUTTON_ON && XMENU_ON
    /* 先解绑工具按钮（断开与默认动作/菜单的连接），再释放菜单与动作。 */
    XClassDeinit(&win->m_toolButton);
    XClassDeinit(&win->m_toolMenu);
    XClassDeinit(&win->m_toolAction);
#endif
#if XWIDGET_ON && XFRAME_ON && XLABEL_ON
    XClassDeinit(&win->m_linkLabel);
#endif
#if XWIDGET_ON && XABSTRACTBUTTON_ON && XCHECKBOX_ON
    XClassDeinit(&win->m_checkBox);
#endif
#if XWIDGET_ON && XABSTRACTBUTTON_ON && XRADIOBUTTON_ON
    XClassDeinit(&win->m_radioA);
    XClassDeinit(&win->m_radioB);
#endif
#if XWIDGET_ON && XFRAME_ON && XLABEL_ON
    XClassDeinit(&win->m_choiceLabel);
#endif
#if XWIDGET_ON && XFRAME_ON && XLABEL_ON && XLAYOUT_ON && XLAYOUT_STACKED_ON
#if XPUSHBUTTON_ON
    XClassDeinit(&win->m_stackPrevButton);
    XClassDeinit(&win->m_stackNextButton);
#endif
    XClassDeinit(&win->m_stackPageOne);
    XClassDeinit(&win->m_stackPageTwo);
    XClassDeinit(&win->m_stackLayoutInner);
#endif
#if XWIDGET_ON && XLAYOUT_ON && XLAYOUT_STACKED_ON
    XClassDeinit(&win->m_pageStacked);
    XClassDeinit(&win->m_pageChoices);
#if XWIDGET_ON && XGROUPBOX_ON && XLINEEDIT_ON && XSPINBOX_ON && \
    XABSTRACTSLIDER_ON && XSLIDER_ON && XPROGRESSBAR_ON
    XClassDeinit(&win->m_inputStatus);
    XClassDeinit((XWidget*)&win->m_progressBar);
    XClassDeinit((XWidget*)&win->m_slider);
    XClassDeinit((XWidget*)&win->m_spinBox);
    XClassDeinit((XWidget*)&win->m_lineEdit);
    XClassDeinit((XWidget*)&win->m_groupBox);
#endif
#if XWIDGET_ON && XLAYOUT_ON && XLAYOUT_STACKED_ON
    XClassDeinit(&win->m_pageInputs);
    XClassDeinit(&win->m_pageTabs);
#endif
    XClassDeinit(&win->m_pageButtons);
    XClassDeinit(&win->m_stackLayout);
#endif
#if XWIDGET_ON && XPUSHBUTTON_ON && XBUTTONGROUP_ON
    /* 导航互斥组为无父 XObject，不随 win 级联析构——先于成员按钮断开
       桥接并回收（同 m_btnGroup 口径：按钮侧连接先断，次序安全）。 */
    XClassDeinit(&win->m_navGroup);
    XClassDeinit(&win->m_navCatGroup);
#endif
#if XWIDGET_ON && XPUSHBUTTON_ON && XLAYOUT_ON && XLAYOUT_STACKED_ON
    {
        int nav;
        for (nav = 0; nav < 15; ++nav) /* 2026-10-06: 15 钮随系统设置两页扩列。 */
            XClassDeinit(&win->m_pageNav[nav]);
    }
#endif
#if XWIDGET_ON && XABSTRACTBUTTON_ON && XPUSHBUTTON_ON && XFRAME_ON && \
    XLABEL_ON && XLAYOUT_ON && XLAYOUT_STACKED_ON
    /* 浮动导航面板成员（面板/标题/两钮/3 一级分类钮 + 5 子分组标题
       标签池；页面钮已在其上回收，面板容器随 m_base 级联，成员结构体
       须显式 deinit）。 */
    {
        int cat;
        for (cat = 0; cat < DEMO_NAV_CAT_N; ++cat)
            XClassDeinit(&win->m_navCatBtns[cat]);
        for (cat = 0; cat < DEMO_NAV_HEAD_N; ++cat)
            XClassDeinit(&win->m_navHeadLabels[cat]);
        XClassDeinit(&win->m_navCollapseBtn);
        XClassDeinit(&win->m_navDockBtn);
        XClassDeinit(&win->m_navTitle);
    }
#endif
#if XCHARTS_ON
    XClassDeinit(&win->m_pageChart);
#endif
#if XGUI_PERFORMANCE_OVERLAY_ON && XWIDGET_ON && XFRAME_ON && XLABEL_ON
    demo_performance_deinit(win);
#endif
#if XGUI_DEMO_STATIC_SCENE_CACHE_ON
    XClassDeinit(&win->m_staticScene);
#endif
#if XWIDGET_ON && XKEYBOARD_ON
    /* 键盘页已迁移到应用默认面板单例（XGuiApplication 拥有）：面板弹
       出即挂本顶层窗为宿主。win 先于 app 析构——级联删除会把应用拥有
       的单例连带删掉，随后 XClassDelete 对
       m_virtualKeyboard 二次删除（悬垂崩溃）。win 析构前显式收层并摘
       挂，面板归还顶层交还应用（virtualKeyboard 在
       XVIRTUALKEYBOARD_ON=0 级联关闭时返回 NULL，自然空操作）。 */
    {
        XVirtualKeyboard* kb = XGuiApplication_virtualKeyboard();
        if (kb) {
            XVirtualKeyboard_closePopup(kb);
            XWidget_setParent((XWidget*)kb, NULL, 0);
        }
    }
#endif
    /* 远程窗口设置页收尾: unhost + close + deleteLater（仓库约定; 须在
       主窗口析构前, 解除 present 回调登记并停全部会话）。 */
    demo_page_remote_server_shutdown();
    /* 远程客户端页收尾: 断链 + deleteLater（2026-10-02 追加; 须在主
       窗口析构前——client 为窗口子对象, 级联析构兜底两序皆安全）。 */
    demo_page_remote_client_shutdown();
#if XMENUBAR_ON && XMENU_ON && XTOOLBAR_ON && XACTION_ON
    /* 页八：addMenu_2 返回的堆菜单归调用方（XMenuBar.h 口径），且为
       无父顶层弹窗——win 级联只回收菜单栏本体，触及不到它们；窗口
       销毁前显式释放（菜单内部动作随 VXMenu_deinit 级联回收）。 */
    if (win->m_fileMenu)
        XClassDelete(win->m_fileMenu);
    if (win->m_editMenu)
        XClassDelete(win->m_editMenu);
#endif
#if XSTACKEDWIDGET_ON && XBUTTONGROUP_ON && XCHECKBOX_ON && XLAYOUT_STACKED_ON
    /* 页十五：按钮组为无父 XObject，不随 win 级联析构——deinit 回收
       内部向量与堆桥接；按钮成员此刻仍存活，析构先断开按钮侧连接再
       同步删桥，顺序安全。 */
    XClassDeinit(&win->m_btnGroup);
#endif
    XClassDelete((XClass*)win);
    XClassDelete(app);
    XPrintf("XGuiWindowDemo: 已退出\n");
    return eventLoopResult;
}

#else /* 开关裁剪 */
int xgui_demo_main(int argc, char* argv[])
{
    (void)argc;
    (void)argv;
    XPrintf("XGuiWindowDemo: 本演示需要 XGUIAPPLICATION_ON / XWINDOW_ON / "
                    "XBACKINGSTORE_ON / XPLATFORMINTEGRATION_ON / "
                    "XPLATFORMNATIVEWINDOW_ON。\n");
    return 2;
}
#endif /* 开关 */
/* 注：文末历史「补齐未闭合 #if」的补位 #endif 已随 2026-10-03 页签节
 * 结构整顿（页 3/容器节门控重排）恢复平衡而移除，全文件 #if/#endif
 * 严格配对。 */
