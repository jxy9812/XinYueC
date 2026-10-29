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
#include "XObject_Protected.h"
#include <stdarg.h> /* demo_log：va_list 转发 vprintf（诊断行立即落盘）。 */
#if defined(__GLIBC__) /* mallopt(内存驻留治理)——glibc 专有, MSVC 无此 API。 */
#include <malloc.h>
#endif
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
#include "XString.h"         /* applicationVersion 注入（XUPGRADE_PRODUCT_VERSION）。 */
#include "XGuiApplication.h"
#include "XClipboard.h"
#if defined(__ANDROID__)
/* 平台补扫自检（--probe-platform）所需：主题/Services 驱动钩子、无线契约、
 * 轮询睡眠与向量容器（头内部带模块开关守卫，桌面构建零影响）。 */
#include "XPlatformTheme.h"
#include "XPlatformServices.h"
#include "XDeviceWiFi.h"
#include "XNetworkWiFi.h"
#include "XVector.h"
#include "XThread.h"
#endif
#if XWIDGET_ON && XKEYBOARD_ON
#include "XVirtualKeyboard.h" /* 键盘页单例面板 win 析构前摘挂（自包含声明，防 XVIRTUALKEYBOARD_ON=0 态缺声明）。 */
#endif
#include "XWidget.h"
#include "XWidget_Protected.h"
#include "XImage.h"
#include "XIcon.h"
#include "XPixmap.h"
#include "XPainter.h"
#include "XWindow.h"
#include "XWindowEvent.h"
#include "xgui_demo_pages.h"
#include "xgui_demo_splitter.h" /* 通用分割条：导航面板/RC 控制列拖宽收展。 */
#include "xgui_demo_theme.h" /* fusion-css 缺省主题样式表（xgui_demo_theme_css）。 */
#include "XFusionStyle.h" /* XFusionStyle_installDefault：缺省 fusion 样式安装（XSTYLE_ON 样式矩阵），补声明消除 C4013 隐式声明。 */
#include "XStyle.h" /* XStyle_installStyleSheet：全局样式表装载，补声明消除 C4013 隐式声明。 */
#include "xgui_demo_page_remote_server.h" /* 远程窗口设置页 CLI 预置/autostart/shutdown。 */
#include "xgui_demo_page_remote_client.h" /* 远程客户端页 CLI 预置/autostart/shutdown(2026-10-02)。 */
#include "xgui_demo_apitest.h"
#include "XWindowDecoration.h" /* 框架级系统标题栏：布局让位边距查询 */
#include "XWindowSystemInterface.h" /* 鼠标/触摸注入 _ex 接口（防 C4013 隐式声明）。 */
#include "XFusionStyle.h" /* XFusionStyle_installDefault（防 C4013 隐式声明）。 */
#include "XStyle.h" /* XStyle_installStyleSheet（防 C4013 隐式声明）。 */
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
#if defined(__linux__) && XGUI_ON && XPLATFORM_DRM_ON
/* DRM/KMS 后端（无 /dev/fb* 的现代嵌入式板，如 RK3506 Buildroot 内核）：
 * 板级引导先试 DRM，失败回落 fbdev（下方既有块）。 */
#include "XPlatformFramebuffer_drm.h"
#endif
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
#include "XBoxLayout.h"
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

/* Demo 默认家族随库缺省配置（XFONT_DEFAULT_FAMILY）。
   [已移除 2026-10-07] 原 XFONT_BUILTIN_OUTLINE_ON 内嵌字库分叉随自研
   XFO1 字库删除——轮廓字统一走 FT 外挂文件解析，缺文件由 XFont_face
   回退位图。 */
#define XGUI_DEMO_DEFAULT_FONT_FAMILY XFONT_DEFAULT_FAMILY

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

/* chrome 高度单一事实源（布局化 chrome 2026-10-17）：品牌条/状态条
 * 高度只在此定义；内容区顶=CSD 保留边距(demo_sysbarH)+品牌条高，由根
 * 垂直布局自动分配，任何布局点不得手写坐标偏移。 */
/* 主窗类型前向声明：品牌条（下方子类）持有 owner 指针并在点击槽里置
 * 一个待处理位，需要该类型可见（完整定义在本文件中段）。 */
struct DemoWin;
#define DEMO_BRAND_H  40
#define DEMO_STATUS_H 26
/* 顶栏导航图标（☰）触控区宽：Material 48dp。品牌条（宽屏/横屏）与
 * 移动端应用栏（紧凑形态）共用——两者都在左上角画同一枚 ☰，故定义
 * 必须早于这两个子类。 */
#define DEMO_APPBAR_BTN_W  48

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

static void demo_draw_checker(XPainter* painter, int x0, int y0,
                              int cols, int rows, int cell);

/* ==================== 应用图标（品牌条棋盘格同源，程序化构造） ==== */

/** @brief 程序化构造品牌条棋盘格图标并设到顶层控件（应用图标落地）。
 *  @details 64x64 ARGB32 离屏 XImage 经 XPainter 直绘（fillRect 与
 *           VDemoBrandBar_paintEvent 同一套图元），XWidget_setWindowIcon
 *           存值后经窗口桥接由平台层按后端落地：X11=_NET_WM_ICON（任务
 *           栏/Alt-Tab 取用），Win32=exe 资源图标（rc `1 ICON`，即
 *           Test/XGuiDemo/res/xgui.ico，与此处同图源），安卓=APK
 *           mipmap 启动器图标（AndroidManifest android:icon，打包链路
 *           入包），无窗管环境平台层 no-op。配色与品牌条绘制共用字面
 *           量口径，改图需两处同步（此处 + VDemoBrandBar_paintEvent）。 */
static void demo_applyWindowIcon(XWidget* win)
{
    XImage img;
    XPixmap pm;
    XIcon icon;
    XPainter painter;
    XRect rect;
    XRect cell;
    const uint32_t kBg = 0xff1f4e79u;    /* 品牌条底色同源 */
    const uint32_t kBlue = 0xff4a7ba6u;  /* 棋盘偶格（左上/右下） */
    const uint32_t kLight = 0xffc8d8e8u; /* 棋盘奇格（右上/左下） */
    const int canvas = 64;
    const int total = 36;                /* 棋盘总边长：画布 ~56%，缩到 16px 格仍可辨 */
    const int csz = total / 2;
    const int x0 = (canvas - total) / 2;
    const int y0 = x0;
    if (!win) return;
    XImage_init_ex(&img, canvas, canvas, XImageFormat_ARGB32);
    XPainter_init(&painter, NULL);
    if (!XPainter_begin_image(&painter, &img)) {
        XPainter_deinit(&painter);
        XClassDeinit(&img);
        return;
    }
    XRect_init(&rect, 0, 0, canvas, canvas);
    XPainter_fillRect(&painter, &rect, kBg);
    XRect_init(&cell, x0, y0, csz, csz);
    XPainter_fillRect(&painter, &cell, kBlue);
    XRect_init(&cell, x0 + csz, y0, csz, csz);
    XPainter_fillRect(&painter, &cell, kLight);
    XRect_init(&cell, x0, y0 + csz, csz, csz);
    XPainter_fillRect(&painter, &cell, kLight);
    XRect_init(&cell, x0 + csz, y0 + csz, csz, csz);
    XPainter_fillRect(&painter, &cell, kBlue);
    XPainter_end(&painter);
    XPainter_deinit(&painter);
    XPixmap_fromImage(&img, 0, &pm);
    XClassDeinit(&img);
    memset(&icon, 0, sizeof(icon));
    XIcon_init(&icon);
    XIcon_init_pixmap(&icon, &pm);
    XWidget_setWindowIcon(win, &icon);
    XClassDeinit(&icon);
    XClassDeinit(&pm);
}

/* ==================== 品牌条子类（蓝底+棋盘格+标题文字一体） ==== */

XCLASS_DEFINE_BEGING(DemoBrandBar)
XCLASS_DEFINE_EXTEND_END(DemoBrandBar, XLabel)

/** @brief 品牌条：自带深蓝底与棋盘格装饰的品牌标题条（demo 自定义
 *         chrome，非框架标题条），文本由 XLabel 绘制管线接管。
 *  @note  [横屏 ☰ 2026-10-25] 宽屏/横屏形态下顶部品牌条左侧加导航
 *         图标，**替代原导航面板上的「收起」按钮**：点 ☰ 收起/展开
 *         左侧导航面板（与「收起」同一开关 demo_navCollapsed）。
 *         注意这不是"打开全屏菜单页"——那套是紧凑形态（竖屏）的
 *         独立机制，横屏仍用常驻侧栏。 */
typedef struct DemoBrandBar
{
    XLabel m_base; /**< XLabel 基类；必须是第一个成员。 */
    struct DemoWin* m_owner; /**< 归属主窗（导航图标点击 → 收起/展开侧栏）。 */
} DemoBrandBar;

/** @brief 绘制事件：树内按自身几何先铺品牌底色与棋盘格，再画导航图标
 *         与标题文字。 */
static void VDemoBrandBar_paintEvent(XWidget* self, XEvent* event)
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
    XPainter_fillRect(&painter, &rect, 0xff1f4e79u);
    demo_draw_checker(&painter,
                      XWidget_width(self) - 24 - 8, 8, 2, 2, 12);
    /* 导航图标：左端 48dp 触控区画三横条 ☰（点击收起/展开左侧导航
     * 面板，替代原面板上的「收起」按钮）。 */
    {
        int cx = 16;
        int cy = XWidget_height(self) / 2;
        int i;
        for (i = 0; i < 3; ++i) {
            XRect line;
            line.x = cx; line.y = cy - 9 + i * 8;
            line.width = 20; line.height = 2;
            XPainter_fillRect(&painter, &line, 0xffffffffu);
        }
    }
    XLabel_drawContents((XLabel*)self, &painter);
    /* [时钟正规化 2026-10-29] 时钟改独立 XLabel 控件（右端锚定由时钟
     * 定时器槽维护），品牌条自绘不再画时间。 */
    XPainter_end(&painter);
    XPainter_deinit(&painter);
}

/* 品牌条（下方子类）点击要切换导航面板收起态；该动作的实现在文件后段
 * （需要 DemoWin 完整定义），故此处只对不完整类型做前置声明。 */
static void demo_brandNavToggle(struct DemoWin* self);

/** @brief 鼠标按下：命中导航图标区（左侧 48dp）→ 收起/展开左侧导航面板。
 *  @details 与已删除的「收起」按钮同一开关（翻转 m_navCollapsed 并重排
 *           面板/内容）。事件在此接受后不会再上传给主窗，故必须**就地**
 *           调用开关函数——不能靠主窗消费标志位（那样永远收不到）。 */
static void VDemoBrandBar_mousePressEvent(XWidget* self, XEvent* event)
{
    DemoBrandBar* bar = (DemoBrandBar*)self;
    if (!self || !event) return;
    if (XEvent_type(event) == XEVENT_TYPE_MOUSE_BUTTON_PRESS && bar &&
        bar->m_owner) {
        XMouseEvent* mouse = (XMouseEvent*)event;
        XPoint pos = XMouseEvent_position(mouse);
        if (pos.x < DEMO_APPBAR_BTN_W) {
            demo_brandNavToggle(bar->m_owner);
            XEvent_accept(event);
            return;
        }
    }
    XClass_Parent(XWidget, EXWidget_MousePressEvent,
                  void (*)(XWidget*, XEvent*))(self, event);
}

/** @brief 初始化品牌条类虚函数表。 */
XVtable* DemoBrandBar_class_init(void)
{
    XVTABLE_INIT_DEFAULT(DemoBrandBar)
    XVTABLE_INHERIT_XCLASS(XLabel);
    XVTABLE_OVERLOAD_DEFAULT(EXWidget_PaintEvent,
                             VDemoBrandBar_paintEvent);
    XVTABLE_OVERLOAD_DEFAULT(EXWidget_MousePressEvent,
                             VDemoBrandBar_mousePressEvent);
    return XVTABLE_DEFAULT;
}

/** @brief 初始化品牌条（文本/字体沿用 XLabel 接口；高度由布局固定）。 */
void DemoBrandBar_init(DemoBrandBar* self, XWidget* parent,
                       XWidgetFlags flags)
{
    if (!self) return;
    XLabel_init(&self->m_base, parent, flags);
    XClassSetVtable(self, DemoBrandBar);
    self->m_owner = NULL;
}

#endif /* XWIDGET_ON && XFRAME_ON && XLABEL_ON */

/* ==================== 移动端应用栏（☰ + 当前页标题） ====================
 * 2026-10-25 手机竖横屏适配：紧凑形态（逻辑宽 < 600dp）的顶栏。
 * 参照 Material 3 应用栏：56dp 高、导航图标触控区 48dp、标题左对齐
 * 紧跟导航图标右侧、同一色面承载（沿用品牌条的深蓝，与桌面形态视觉
 * 连贯）。导航图标（☰ / 返回箭头）由 paint 直接画三道白条/折线——
 * 不走字库字形（轮廓字体未必含 U+2630/HAMBURGER，缺字会渲染成豆腐块）。
 */

/* 主窗类型此处尚未定义（DemoWin 的完整定义在下方），故用不完整类型
 * 前向引用——不重复 typedef（C99 下重定义 typedef 是约束违规）。 */
struct DemoWin;

/* ---- 移动端形态常量（Material 3 窗口尺寸类口径）----
 * compact 断点：逻辑宽 < 600dp（Material 3 的 compact 窗口上限）——
 * 手机竖屏必落此档；横屏手机逻辑宽 ≈ 869dp → 落宽屏档，侧栏形态在
 * 横屏下是更好的信息架构（导航项多时侧栏优于底部条）。 */
#define DEMO_COMPACT_MAX_W 600   /**< compact 断点（逻辑 px，≈dp）。 */
#define DEMO_APPBAR_H      56    /**< 应用栏高（Material 应用栏 56dp）。 */
/* DEMO_APPBAR_BTN_W 已于上方（品牌条之前）定义——两处共用同一图标区宽。 */
#define DEMO_MENU_ROW_H    44    /**< 菜单行高（触控目标 ≥ 44dp）。 */
#define DEMO_MENU_CAT_H    40    /**< 菜单分区标题高。 */
#define DEMO_MENU_PAGE_IDX 21    /**< 移动端菜单页在内容堆叠中的下标（页面 0..20 之后）。 */

/** @brief 菜单开合（应用栏 ☰ 点击 / Esc 返回键）。 */
static void demo_menuToggle(struct DemoWin* self);

#if XWIDGET_ON && XFRAME_ON && XLABEL_ON

XCLASS_DEFINE_BEGING(DemoAppBar)
XCLASS_DEFINE_EXTEND_END(DemoAppBar, XLabel)

/** @brief 移动端应用栏：深蓝底 + 导航图标 + 当前页标题（一体自绘）。 */
typedef struct DemoAppBar
{
    XLabel    m_base;   /**< XLabel 基类；标题文本/对齐/缩进沿用其接口。 */
    struct DemoWin* m_owner; /**< 归属主窗（导航图标点击 → 主窗切菜单页）。 */
    bool      m_menuOpen; /**< 菜单页是否展开（决定图标形态：☰ / ←）。 */
} DemoAppBar;

/** @brief 绘制：底色 + 导航图标（菜单态=三横条，菜单开=返回箭头）+ 标题。 */
static void VDemoAppBar_paintEvent(XWidget* self, XEvent* event)
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
    XPainter_fillRect(&painter, &rect, 0xff1f4e79u);
    {
        /* 导航图标：左端 48dp 触控区内画图形（不依赖字库字形）。
         * [回左侧 2026-10-25 用户裁定] ☰ 回到标题左侧（Material 标准
         * 顶栏形：导航图标在文字左），此前右置一版已回退。 */
        int cx = 16;
        int cy = XWidget_height(self) / 2;
        int i;
        DemoAppBar* bar = (DemoAppBar*)self;
        int menuOpen = bar ? bar->m_menuOpen : 0;
        if (menuOpen) {
            /* 返回箭头 ←（菜单开）：主干 + 上/下两笔斜折（线段画出真斜
             * 线；此前用横向矩形拼，出来是四道杠的怪图形——桌面实测）。 */
            XPainter_setPen(&painter, 0xffffffffu);
            XPainter_setPenWidth(&painter, 2);
            XPainter_drawLine(&painter, cx + 3, cy, cx + 21, cy);
            XPainter_drawLine(&painter, cx + 3, cy, cx + 11, cy - 8);
            XPainter_drawLine(&painter, cx + 3, cy, cx + 11, cy + 8);
        } else {
            for (i = 0; i < 3; ++i) {
                XRect line;
                line.x = cx; line.y = cy - 9 + i * 8;
                line.width = 20; line.height = 2;
                XPainter_fillRect(&painter, &line, 0xffffffffu);
            }
        }
    }
    XLabel_drawContents((XLabel*)self, &painter);
    /* [时钟正规化 2026-10-29] 同品牌条：时钟独立控件化，应用栏自绘
     * 不再画时间。 */
    XPainter_end(&painter);
    XPainter_deinit(&painter);
}

/** @brief 鼠标按下：命中导航图标区（左侧 48dp）→ 主窗切菜单页。 */
static void VDemoAppBar_mousePressEvent(XWidget* self, XEvent* event)
{
    DemoAppBar* bar = (DemoAppBar*)self;
    if (!self || !event) return;
    if (XEvent_type(event) == XEVENT_TYPE_MOUSE_BUTTON_PRESS && bar &&
        bar->m_owner) {
        XMouseEvent* mouse = (XMouseEvent*)event;
        XPoint pos = XMouseEvent_position(mouse);
        if (pos.x < DEMO_APPBAR_BTN_W) {
            demo_menuToggle(bar->m_owner);
            XEvent_accept(event);
            return;
        }
    }
    XClass_Parent(XWidget, EXWidget_MousePressEvent,
                  void (*)(XWidget*, XEvent*))(self, event);
}

/** @brief 初始化应用栏类虚函数表。 */
XVtable* DemoAppBar_class_init(void)
{
    XVTABLE_INIT_DEFAULT(DemoAppBar)
    XVTABLE_INHERIT_XCLASS(XLabel);
    XVTABLE_OVERLOAD_DEFAULT(EXWidget_PaintEvent,
                             VDemoAppBar_paintEvent);
    XVTABLE_OVERLOAD_DEFAULT(EXWidget_MousePressEvent,
                             VDemoAppBar_mousePressEvent);
    return XVTABLE_DEFAULT;
}

/** @brief 初始化移动端应用栏（标题文本/对齐沿用 XLabel 接口）。 */
void DemoAppBar_init(DemoAppBar* self, XWidget* parent,
                     XWidgetFlags flags)
{
    if (!self) return;
    XLabel_init(&self->m_base, parent, flags);
    XClassSetVtable(self, DemoAppBar);
    self->m_owner = NULL;
    self->m_menuOpen = false;
}

#endif /* XWIDGET_ON && XFRAME_ON && XLABEL_ON */

/* 紧凑形态依赖内容堆叠（菜单页是堆叠里的一页）：堆叠关闭时应用栏的
 * 导航图标无菜单可切，给一个空实现保住链接完整性。 */
#if !(XWIDGET_ON && XABSTRACTBUTTON_ON && XPUSHBUTTON_ON && XFRAME_ON && \
      XLABEL_ON && XLAYOUT_ON && XLAYOUT_STACKED_ON)
static void demo_menuToggle(struct DemoWin* self) { (void)self; }
#endif

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
    XTimerId        m_clockTimer;   /**< 顶栏系统时钟 1s 刷新定时器（驱动时钟 XLabel setText）。 */
    XLabel*         m_clockLabel;   /**< 品牌条时钟控件（[时钟入布局 2026-10-29] 品牌条
                                      *   水平布局右端项：宽度由布局按 sizeHint 自动分配，
                                      *   setText 经 updateGeometry 驱动重排；白字蓝底与
                                      *   顶栏一体，鼠标穿透不挡顶栏点击。 */
    XLabel*         m_clockLabelApp;/**< 应用栏时钟控件（紧凑形态，同品牌条；显隐随父栏）。 */
    XTimerId        m_overlayTimer; /**< 空闲闸门下悬浮层降频自刷新定时器（XGUI_DEMO_IDLE_OVERLAY_MS，4Hz）。 */
    int             m_lcdValue;     /**< LCD 字符序列索引（循环段码表 30 字符）。 */
    bool            m_closed; /**< CloseEvent 被接受或自动退出后置真。 */
    const char*     m_screenshotPath; /**< 非空时渲染数帧后保存一帧截图并退出（借用指针）。 */
    int             m_screenshotFrames; /**< 截图模式已渲染帧数。 */
    bool            m_autoTest;         /**< 自动交互测试模式（第 4 页注入事件断言联动）。 */
    int             m_autoTestFrames;   /**< 自动测试已渲染帧数。 */
    int             m_orientOpt;        /**< --orient：首帧切到的方向（-1=不切）。 */
    bool            m_orientApplied;    /**< --orient 是否已应用（只跑一次）。 */
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
    DemoBrandBar    m_brandBar; /**< 顶部品牌条（蓝底+棋盘格+标题文字一体；
                                  *   布局化 chrome：根垂直布局首段，高度
                                  *   DEMO_BRAND_H 固定）。 */
#if XWIDGET_ON && XFRAME_ON && XLABEL_ON
    DemoAppBar      m_appBar;    /**< 移动端应用栏（☰ + 当前页标题）：紧凑
                                  *   形态的顶栏，宽屏形态隐藏。 */
#endif
    int             m_layoutMode; /**< 布局形态：0=宽屏（桌面/横屏，侧栏导航）
                                   *   1=紧凑（手机竖屏，应用栏 + 全屏菜单页）。
                                   *   按逻辑宽与 DEMO_COMPACT_MAX_W 判定。 */
    int             m_menuPageIdx; /**< 移动端菜单页在内容堆叠中的实际下标
                                   *   （随特性裁剪而变，注册时实测写入——
                                   *   写死常量会在页数被裁的构建里指向别的页）。 */
    bool            m_menuOpen;  /**< 紧凑形态：菜单页是否展开。 */
    int             m_pageBeforeMenu; /**< 打开菜单前的页索引（菜单关闭时回退目标）。 */
    bool            m_navCollapsedUser; /**< 用户手动收起态（紧凑形态强制收起，
                                   *   回宽屏形态时还原用户原值）。 */
    XWidget         m_mobileMenu; /**< 紧凑形态的全屏菜单页（内容堆叠第
                                   *   DEMO_MENU_PAGE_IDX 页）：导航树平铺，
                                   *   Material 抽屉在紧凑窗口的"全屏变体"。 */
    XWidget         m_contentHost; /**< 内容区宿主（根布局中段伸展；页面
                                  *   堆叠/浮动导航面板/悬浮层的父）。 */
    XBoxLayout      m_rootLayout; /**< 根垂直布局（品牌条/中段宿主/状态栏
                                  *   三段；contentsMargins.top=CSD 保留
                                  *   边距，边框开关只更新这一处）。 */
    XBoxLayout      m_brandBarLayout; /**< 品牌条水平布局（[时钟入布局
                                  *   2026-10-29] stretch + 时钟右端项；
                                  *   右缘 40 让位棋盘格）。 */
    XBoxLayout      m_appBarLayout;   /**< 应用栏水平布局（同品牌条，
                                  *   右缘 12 留白）。 */
    XWidget         m_midHost;    /**< 中段宿主（根布局伸展段）。 */
    XBoxLayout      m_midHostLayout; /**< 中段容器布局（单项：框架分割条
                                  *   stretch 铺满）。 */
#if XSPLITTER_ON
    XSplitter       m_navSplitter; /**< [XSplitter 迁移 2026-10-17] 中段
                                  *   框架分割条（Qt QSplitter 语义）：
                                  *   [导航面板|内容区]，换边=setOrientation
                                  *   +条目顺序翻转，拖拽/手柄/方向光标/
                                  *   手柄点击信号由框架接管，替代原四方向
                                  *   盒式布局器+DemoSplitter 组合。 */
#endif
    bool            m_statusBarVisible; /**< 状态栏可见（窗口设置页开关；
                                          *   隐藏时布局自动收回其空间）。 */
    int             m_lastContentW; /**< 帧泵兜底门：上次页内排布时内容区
                                  *   宽（-1=未排布）。 */
    int             m_lastContentH; /**< 上次页内排布时内容区高。 */
    DemoStatusLabel m_statusLabel; /**< 底部状态栏（自带深色底，白字）。 */
#endif
#if XWIDGET_ON && XPUSHBUTTON_ON
    XPushButton     m_pageNav[24]; /**< 页面切换按钮（下标=页索引；挂在浮动导航面板活动分类下，装配见 DemoWin_create；页 15=二维码、页 16=软件更新、页 17/18=Modbus 主站/从站、页 19=窗口设置、页 20=时钟与 DDNS、页 22=测试菜单 2026-10-29 合并扩列 23）。 */
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
    XPushButton     m_navCatBtns[4]; /**< 一级分类钮（控件/系统设置/远程/测试；手风琴）。 */
    XLabel          m_navHeadLabels[5]; /**< 子分组标题标签池（控件类内「按钮/输入/…」，竖版展开时启用）。 */
#if XSCROLLAREA_ON && XABSTRACTSCROLLAREA_ON
    XScrollArea     m_navScroll;     /**< 竖版内容滚动容器（AsNeeded：内容超高才出垂直滚动条）。 */
    XWidget*        m_navContent;    /**< 滚动内容载体（分类钮/标题/页面钮的公共父，竖版挂它、横版回挂面板）。 */
#endif
    int             m_navDock;       /**< 停靠边 0=左 1=右 2=上 3=下（默认左）。 */
    bool            m_navCollapsed;  /**< 收起贴边态（细条=分割条本身）。 */
    int             m_navCategory;   /**< 当前展开分类（kNavGroups 下标）。 */
    int             m_navWidth;      /**< 面板宽记忆值（左右停靠；拖拽后
                                      *   navUpdatePanel 起始从分割器回读
                                      *   同步，钳位 140..380）。 */
    int             m_navTBH;        /**< 面板高记忆值（上下停靠；同步口径
                                      *   92..320）。 */
#endif
#if XWIDGET_ON && XLAYOUT_ON && XLAYOUT_STACKED_ON
    XStackedLayout  m_stackLayout; /**< 主内容堆叠布局（15 个演示页面）。 */
    XWidget         m_pageButtons; /**< 页面 0：按钮演示容器。 */
    XWidget         m_pageChoices; /**< 页面 1：选择演示容器。 */
    XWidget         m_pageStacked; /**< 页面 2：堆叠演示容器。 */
    XWidget         m_pageInputs;  /**< 页面 3：输入控件演示容器。 */
    XWidget         m_pageTabs;    /**< 页面 4：容器与窗口演示容器。 */
    XWidget         m_pageChart;   /**< 页面 12：图表演示容器（自选项卡页打散独立）。 */
    XWidget*        m_extPages[19]; /**< 页面 5~22：扩展页根（xgui_demo_pages.h 契约，5+下标=页索引，下标 7=内置图表页占位恒 NULL；堆对象随父链级联析构；下标 12/13=Modbus 主站/从站页 2026-10-13 双页拆分，下标 14=窗口设置页 2026-10-17，下标 17=测试菜单+Shell 页 2026-10-29）。[懒分配 2026-10-19] 语义更新：注册期=空占位壳（XWidget_create 堆对象，仅保「堆叠下标=页索引」与几何分支口径），首显经 demo_ext_ensureBuilt 同位换入真页根后槽位才指真根；构建回落（裁剪）时槽位清空=旧「跳过注册」口径。 */
    bool            m_extBuilt[19]; /**< [懒分配 2026-10-19] 扩展页已构建/已尝试标记（demo_ext_ensureBuilt 幂等守卫）：false=槽位仍是注册期占位壳；true=已换真页根或已裁剪回落。全量懒分配策略=常驻不销毁，置位后无复位路径（页活到进程退出，与现状销毁语义一致）。 */
    /* [页级滚动壳 2026-10-29] 页索引 → 包裹滚动区（DemoScrollHost 内嵌
     * 壳；NULL=该页未包裹——已自带滚动承载的豁免页/滚动模块裁剪）。
     * adapt 漏斗收尾按当前页刷新一次内容排版（页内档位重排改内容提示
     * 后滚动范围即时跟上，network/modbus 页各自 adapt 显式调用的统一
     * 化收口）。指针皆借用（滚动区随宿主父子链存活，页常驻不销毁）。 */
    XScrollArea*    m_pageScrolls[26]; /**< 堆叠页索引 0~23 + 菜单页（容量含余量）。 */
    /* 页 3/4/12 布局容器化（2026-10-17）：页内排布改由盒式布局管理，
     * demo_layout_content 对应手写坐标段删除。嵌套行/列布局器按借用
     * 指针挂入父布局（XBoxLayout_addLayout 不取所有权），故全部作为
     * DemoWin 成员持有，生命周期与页面控件同随窗口。 */
    XBoxLayout      m_inputsLayout;   /**< 页 3 根纵向布局（顶行铺满+状态行贴底）。 */
    XBoxLayout      m_inputsTopRow;   /**< 页 3 顶行横向布局（左分组框列+右输入列）。 */
    XBoxLayout      m_inputsLeftCol;  /**< 页 3 左列纵向布局（分组框+多行编辑余高）。 */
    XBoxLayout      m_inputsRightCol; /**< 页 3 右列纵向布局（下拉/旋钮/数码管/日期时间族）。 */
    XBoxLayout      m_groupBoxLayout; /**< 页 3 分组框内纵向布局（顶边距 24 近似标题区）。 */
    XBoxLayout      m_comboRow;       /**< 页 3 下拉/字体行横向布局。 */
    XBoxLayout      m_dialRow;        /**< 页 3 旋钮/联动进度行横向布局。 */
    XBoxLayout      m_lcdRow;         /**< 页 3 数码管/滚动条行横向布局。 */
    XBoxLayout      m_tabsLayout;     /**< 页 4 根纵向布局（页签容器 stretch 铺满+状态行贴底）。 */
    XBoxLayout      m_framePageLayout;/**< 页 4「框架」页签纵向布局（菜单栏/工具栏/状态栏三段）。 */
    XBoxLayout      m_wePageLayout;   /**< 页 4「向导错误」页签启动器钮行横向布局。 */
    XBoxLayout      m_scrollTabPageLayout; /**< 页 4「滚动」包装页填充布局（内容件随包装页铺满，撤手写 (10,10,300,150)）。 */
    XBoxLayout      m_calTabPageLayout;    /**< 页 4「日历」包装页填充布局（撤手写 (10,10,280,200)）。 */
    XBoxLayout      m_stackWTabPageLayout; /**< 页 4「堆叠组」包装页填充布局（撤手写 (10,10,200,100)）。 */
    XBoxLayout      m_chartLayout;    /**< 页 12 根纵向布局（钮行顶置+图表视图铺满）。 */
    XBoxLayout      m_chartBtnRow;    /**< 页 12 图表切换钮行横向布局。 */
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

/** @brief 将 demo 使用的 XFont 默认家族设置为当前可用的轮廓字库。
 * @details 默认家族 "XFontOutlineCommon" 是普通名——FT 为唯一轮廓字
 *          实现（[已移除 2026-10-07] 原 XFO1 外挂链），引擎按
 *          XFONT_EXTERNAL_FT_FONT_DIR（默认 "../Library/XFont"，相对
 *          进程 cwd）补探 .ttf/.otf/.ttc（XFontFt.c 候选迭代）。demo
 *          常从 bin-release/bin-* 目录起跑，cwd 恰为仓库子目录时
 *          "../Library/XFont" 失配 → 全链探测落空。此处改经
 *          XCoreApplication_applicationDirPath 拼 exe 相对绝对路径
 *          "<exeDir>/../Library/XFont/XFontOutlineCommon"——绝对路径
 *          走候选枚举 direct 分支（无后缀补探三后缀），与起跑 cwd
 *          无关；文件缺失时由 XFont_face 回退链兜底（FT 探测失败发
 *          一行 stderr 警告 → 注册链首位位图 provider = XFont8x16），
 *          不崩溃。 */
static void demo_apply_default_font(XFont* font)
{
    if (!font)
        return;
    {
        const XString* exeDir = XCoreApplication_applicationDirPath();
        char fontPath[XFONT_EXTERNAL_FONT_PATH_MAX];
        /* 契约: applicationDirPath 返回堆串调用者释放(ASan 实测每控件
         * 一次泄漏, 47 控件/轮)。setFamily 深拷贝路径串, 两路都先释放。
         * 家族串取无后缀形态：<exeDir>/../Library/XFont/XFontOutlineCommon
         * ——FT 链直连无后缀补 .ttf/.otf/.ttc 命中同目录对应文件
         * （[已移除] 原 XFO1 链同串补 .xfo），与起跑 cwd 无关；FT 探测
         * 失配时由 XFont_face 回退链兜底（一行 stderr 警告 → 注册链
         * 首位位图 XFont8x16）。
         * [安卓 2026-10-24] 上面的 <exeDir>/../Library/XFont 是**桌面**
         * 布局的相对假设：安卓的 exeDir 是进程伪路径（app_process），
         * 该路径不存在 → 直连路径失配 → 回退位图字库（仅 ASCII）→ 中文
         * 整片缺失。安卓的字库由 android_main.c 从 APK assets 释放到应用
         * 私有目录 fonts/，编译期目录宏 XFONT_EXTERNAL_FT_FONT_DIR 指向
         * 该目录（CMake 安卓分支注入）。故安卓上**不**用 exeDir 直连，
         * 改用家族名走编译期目录链（FT 按 <dir>/<family>.{ttf,otf,ttc} 找）。 */
#if defined(__ANDROID__)
        /* 安卓：家族名 + 编译期目录（CMake 注入 XFONT_EXTERNAL_FT_FONT_DIR） */
        XFont_setFamily(font, XGUI_DEMO_DEFAULT_FONT_FAMILY);
        if (exeDir)
            XClassDelete((XClass*)exeDir);
        return;
#else
        if (exeDir &&
            snprintf(fontPath, sizeof(fontPath),
                     "%s/../Library/XFont/XFontOutlineCommon",
                     XString_toUtf8(exeDir)) > 0 &&
            strlen(fontPath) < sizeof(fontPath)) {
            XFont_setFamily(font, fontPath); /* setFamily 深拷贝（XFont.c XFont_setFamily）。 */
            XClassDelete((XClass*)exeDir);
            return;
        }
        if (exeDir)
            XClassDelete((XClass*)exeDir);
        XFont_setFamily(font, XGUI_DEMO_DEFAULT_FONT_FAMILY);
#endif
    }
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

/** @brief 当前时间 → "YYYY-MM-DD HH:MM:SS"（时钟控件初始化与 1s 定时器共用）。 */
static void demo_clockFormatNow(char* buf, size_t size)
{
    XDateTime now = XDateTime_currentDateTime();
    snprintf(buf, size, "%04d-%02d-%02d %02d:%02d:%02d",
             XDate_year(&now.m_date), XDate_month(&now.m_date),
             XDate_day(&now.m_date), XTime_hour(&now.m_time),
             XTime_minute(&now.m_time), XTime_second(&now.m_time));
}

/** @brief 创建顶栏时钟控件：XLabel + 顶栏同款蓝底白字 + 鼠标穿透。
 *  @details [时钟入布局 2026-10-29] 控件由父栏的盒式布局托管：宽度取
 *           sizeHint（文本自然步进宽），setText 经 updateGeometry 触发
 *           布局重排，不再手算锚点/宽度。创建即写当前时间，首帧就有
 *           正确文本与宽度，不依赖 1s 定时器首拍。 */
static XLabel* demo_clockLabelCreate(XWidget* bar)
{
    XLabel* label;
    XFont font;
    XPalette pal;
    XColor color;
    if (!bar) return NULL;
    label = XLabel_create(bar, 0);
    if (!label) return NULL;
    font = XWidget_font(bar);
    XWidget_setFont((XWidget*)label, &font);
    XClassDeinit(&font);
    XPalette_init_default(&pal);
    XColor_init_rgb(&color, 0x1f, 0x4e, 0x79, 0xff);
    XPalette_setColor(&pal, XPaletteColorGroup_Active,
                      XPaletteColorRole_Window, color);
    XColor_init_rgb(&color, 0xff, 0xff, 0xff, 0xff);
    XPalette_setColor(&pal, XPaletteColorGroup_Active,
                      XPaletteColorRole_WindowText, color);
    XWidget_setPalette((XWidget*)label, &pal);
    XWidget_setForegroundRole((XWidget*)label, XPaletteColorRole_WindowText);
    XWidget_setAutoFillBackground((XWidget*)label, true);
    XLabel_setAlignment(label, XAlignment_Right | XAlignment_VCenter);
    XWidget_setAttribute((XWidget*)label,
                         XWidgetAttribute_TransparentForMouseEvents, true);
    {
        char buf[32];
        demo_clockFormatNow(buf, sizeof(buf));
        XLabel_setText_2(label, buf);
    }
    return label;
}

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
/* [死码清理] demo_draw_label 已删除：全仓无调用点（审计清单）。
 */
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
    y = XWidget_height(base) - geo.height -
        (self->m_statusBarVisible ? DEMO_STATUS_H : 0);
    if (x < 0) x = 0;
    if (y < 0) y = 0;
    if (x != geo.x || y != geo.y) {
        /* 悬浮层文字自适应收框只改尺寸不动位置：右下锚定随行宽变化
           （如网速位数增减）整体位移。位置变更后旧矩形不再属于悬浮层
           但残着上一帧像素——真机竖屏实测出界残影（边缘残留半个字形）。
           新旧矩形各投一次更新，旧位残迹清除、新位随即绘上。 */
        XPerformanceOverlay_setPosition(&self->m_performanceOverlay, x, y);
        XWidget_updateRect(base, &geo);
        {
            XRect moved;
            moved = XPerformanceOverlay_geometry(&self->m_performanceOverlay);
            XWidget_updateRect(base, &moved);
        }
    }
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

/* [下移取消 2026-10-25 用户裁定] 状态栏让位（demo_topInset，整体下移
 * 48dp）已回退：安卓改走沉浸式（XGuiNativeActivity 隐藏状态栏 +
 * LAYOUT_FULLSCREEN 铺满），顶部没有需要让位的系统栏，应用栏直接落
 * y=0。菜单图标同步移到标题文字右侧（避免顶栏左缘与未来可能的系统
 * 时钟位打架）。 */

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

    {
        /* [皮肤深浅联动 2026-10-18] 窗口背景随颜色方案翻深浅：深色皮肤
         * 下取应用调色板 Window 角色（对标 qt_fusionPalette 深底），
         * 浅色维持品牌底 #F4F6F8。原硬编码浅底在深色皮肤下=全窗浅底，
         * 而文字已随调色板翻浅 → 标题/行标/勾选框/下拉框文字与浅底
         * 同色不可见（10-18 真机截图实证；导航/状态栏/按钮因自绘深底
         * 幸免）。 */
        uint32_t bg = 0xfff4f6f8u;
#if XPALETTE_ON
        XStyleHints* hints = XGuiApplication_styleHints();
        if (hints && XStyleHints_colorScheme(hints) ==
                         XStyleHintsColorScheme_Dark) {
            XPalette pal = XGuiApplication_palette();
            XColor winBg = XPalette_color(&pal, XPaletteColorGroup_Current,
                                          XPaletteColorRole_Window);
            bg = 0xff000000u | ((uint32_t)(winBg.m_comp1 & 0xff) << 16) |
                 ((uint32_t)(winBg.m_comp2 & 0xff) << 8) |
                 (uint32_t)(winBg.m_comp3 & 0xff);
        }
#endif
        demo_fill_rect(painter, 0, 0, w, h, bg);       /* 窗口背景 */
    }
    /* [下移取消 2026-10-25] 状态栏让位带已随 demo_topInset 一并回退：
     * 沉浸式下状态栏隐藏、应用栏落 y=0，无需染色带。 */
    /* 标题带/棋盘格由 DemoBrandBar 子控件自绘（布局化 chrome），静态
     * 基底只剩窗口背景。导航为浮动面板（子控件自绘）。 */
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
/** @brief 启动即全屏（--fullscreen，2026-10-25 用户需求：开机自启铺满面板）。
 * @details 与 --maximized 的区别是窗口状态不同：Maximized 保留 CSD 标题栏
 *          （内容区让位条带高），FullScreen 按框架语义隐藏标题栏、内容铺满
 *          整个窗口——设备上即整块 1024x600 面板。fbdev 无 WM，两者都只是
 *          框架内的软件窗口状态，无原生窗交互差异。 */
static bool g_startFullScreen;
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
        /* 配对 RELEASE 收口：滑块按下即 grabMouse（拖动语义），缺释放
         * 会让抓取与 m_sliderDown 双滞留，吞掉演示窗后续首击。 */
        XMouseEvent_init(&me, XEVENT_TYPE_MOUSE_BUTTON_RELEASE,
                         XMouseButton_LeftButton, 0, p);
        XObject_event_base((XObject*)slider, (XEvent*)&me);
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
        /* [坐标口径 2026-10-27] XWidget_geometry 返回**父坐标系**矩形
         * （对标 QWidget::geometry）。窗口级注入的 position 期望**窗口
         * 客户区坐标**——直接把父相对矩形当窗口坐标会漏掉祖先链偏移
         * （本页导航面板在内容区容器内，偏移 98px），点击落到别的页签
         * 上（实测命中 nav[9] 而非 nav[5]）。须经 XWidget_mapTo 换算。 */
        {
            XPoint localCenter;
            XPoint_init(&localCenter, navRect.width / 2, navRect.height / 2);
            tpos = XWidget_mapTo((XWidget*)&self->m_pageNav[5], (XWidget*)self,
                                 &localCenter);
        }
        tglobal = XWidget_mapToGlobal((XWidget*)&self->m_pageNav[5],
                                      &(XPoint){ navRect.width / 2, navRect.height / 2 });
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
        /* [坐标口径 2026-10-27] XWidget_geometry 返回**父坐标系**矩形
         * （对标 QWidget::geometry）。窗口级注入的 position 期望**窗口
         * 客户区坐标**——直接把父相对矩形当窗口坐标会漏掉祖先链偏移
         * （本页导航面板在内容区容器内，偏移 98px），点击落到别的页签
         * 上（实测命中 nav[9] 而非 nav[5]）。须经 XWidget_mapTo 换算。 */
        {
            XPoint localCenter;
            XPoint_init(&localCenter, navRect.width / 2, navRect.height / 2);
            tpos = XWidget_mapTo((XWidget*)&self->m_pageNav[5], (XWidget*)self,
                                 &localCenter);
        }
        tglobal = XWidget_mapToGlobal((XWidget*)&self->m_pageNav[5],
                                      &(XPoint){ navRect.width / 2, navRect.height / 2 });
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

    /* 页 19 导航点击回归锁（2026-10-16）：kNavSlots 表漏扩页时症状为
     * 「导航钮高亮但内容区不切换」（连接读越界槽位为空）。用编程式
     * click() 直击按钮的信号→槽链路（不依赖窗口级合成鼠标的投放环境；
     * 窗口级合成点击对导航钮的可达性另见上方页 5 对照组的既有口径）。 */
    {
        demo_switchPage(self, 19);
        demo_switchPage(self, 13);
        XAbstractButton_click((XAbstractButton*)&self->m_pageNav[19]);
        XGuiApplication_processEvents(XEventLoop_AllEvents);
        DEMO_EXPECT(XStackedLayout_currentIndex(&self->m_stackLayout) == 19,
                    "导航钮编程式 click 切到时钟与 DDNS 页（kNavSlots 链路锁）");
        demo_switchPage(self, 3);
    }

#undef DEMO_EXPECT
    XPrintf("XGuiAutoTest: %s\n",
            failures == 0 ? "PASS" : "FAIL");
    if (failures)
        XGuiApplication_quit();
}

#if XWIDGET_ON && XSCROLLAREA_ON && XABSTRACTSCROLLAREA_ON
/* ==================== 页级滚动壳（2026-10-29 整页竖滚统一收口） ========
 * [所有页面可滚] 未自带滚动承载的页面（条目视图/高级控件/图形效果/
 * 虚拟键盘/远程服务端/远程客户端/悬浮窗设置/二维码/软件更新/窗口设置/
 * 测试菜单 + 内建 0~4/12/菜单页）统一在堆叠条目层包一层：
 *   DemoScrollHost（堆叠条目，XWidget 子类）
 *     └ XScrollArea（竖条 AsNeeded、横条 AlwaysOff、widgetResizable(true)）
 *         └ 真页根
 * 不直接把 XScrollArea 挂进 XStackedLayout：堆叠给 XScrollArea 条目的
 * 几何分配异常（仅 184x150 且不再重摆，network/modbus 页同款实证），
 * 宿主内嵌定式与 net_createScroll/mb_slaveCreateScroll 同口径。
 * 竖条 AsNeeded：内容一屏装得下时完全不出条（桌面现状不变），超高才
 * 出；触摸滑动由框架 XAbstractScrollArea 触摸跟手+惯性承载（手指方向=
 * 内容移动方向，移动端操作习惯）。
 * 身份契约：m_extPages[]/m_page* 成员仍指真页根（逐页 adapt/autotest/
 * 大表格收编钩子/远程 isWindow 门按真根消费，零感知）；包裹只发生在
 * 堆叠条目层。已自带整页滚动的页面（dialogs/timeddns/atv61/modbus
 * 从站真根=滚动区，network/modbus 主站自带页宿主内嵌壳）经豁免表跳
 * 过，防双层滚动条。 */
XCLASS_DEFINE_BEGING(DemoScrollHost)
XCLASS_DEFINE_EXTEND_END(DemoScrollHost, XWidget)

typedef struct DemoScrollHost
{
    XWidget m_base;        /**< XWidget 基类；必须是第一个成员。 */
    XScrollArea* m_scroll; /**< 内嵌滚动区（拥有；随父子链析构）。 */
} DemoScrollHost;

/** @brief 页宿主 resizeEvent：内嵌滚动区铺满宿主矩形（堆叠分配几何的
 *         唯一消费点，NetPageHost 定式）。 */
static void VDemoScrollHost_resizeEvent(XWidget* self, XEvent* event)
{
    DemoScrollHost* host;
    (void)event;
    if (!self) return;
    host = (DemoScrollHost*)self;
    if (host->m_scroll)
        XWidget_setGeometry((XWidget*)host->m_scroll, 0, 0,
                            XWidget_width(self), XWidget_height(self));
}

XVtable* DemoScrollHost_class_init(void)
{
    XVTABLE_INIT_DEFAULT(DemoScrollHost)
    XVTABLE_INHERIT_XCLASS(XWidget);
    XVTABLE_OVERLOAD_DEFAULT(EXWidget_ResizeEvent,
                             VDemoScrollHost_resizeEvent);
    return XVTABLE_DEFAULT;
}

/** @brief 把真页根包进滚动壳，返回堆叠条目。
 * @details 失败（分配失败）原样返回真根：页面退化为「超高截断」历史
 *         行为，功能不受损；调用方无需判别包裹与否。真根所有权移交
 *         滚动区（XScrollArea_setWidget 接管语义）。
 * @param scrollOut 非空时回填内嵌滚动区（借用；NULL 传可不关心）。 */
static XWidget* demo_wrap_page_scroll_ex(XWidget* page,
                                         XScrollArea** scrollOut)
{
    DemoScrollHost* host;
    if (scrollOut) *scrollOut = NULL;
    if (!page) return NULL;
    host = (DemoScrollHost*)XMemory_malloc(sizeof(DemoScrollHost),
                                           XCLASS_DEFAULT_MEMORY_TYPE);
    if (!host) return page;
    XMemset(host, 0, sizeof(*host));
    XWidget_init(&host->m_base, NULL, 0);
    XClassSetVtable(host, DemoScrollHost);
    Set_Class_Memory(host, XCLASS_DEFAULT_MEMORY_TYPE);
    Set_Class_IsHeap(host, true);
    host->m_scroll = XScrollArea_create((XWidget*)host, 0);
    if (!host->m_scroll) {
        XObject_deleteLater((XObject*)host);
        return page;
    }
    XAbstractScrollArea_setHorizontalScrollBarPolicy(
        (XAbstractScrollArea*)host->m_scroll,
        (int)XScrollBarPolicy_AlwaysOff);
    XScrollArea_setWidgetResizable(host->m_scroll, true);
    XScrollArea_setWidget(host->m_scroll, page);
    if (scrollOut) *scrollOut = host->m_scroll;
    return &host->m_base;
}
#endif /* XWIDGET_ON && XSCROLLAREA_ON && XABSTRACTSCROLLAREA_ON */

/** @brief 内建页/菜单页包裹 + 映射登记（包裹失败/模块裁剪原样返回）。
 * @param pageIdx 页索引（>=0 时登记 m_pageScrolls[pageIdx]）。 */
static XWidget* demo_win_wrapBuiltinPage(DemoWin* self, XWidget* page,
                                         int pageIdx)
{
#if XWIDGET_ON && XSCROLLAREA_ON && XABSTRACTSCROLLAREA_ON
    XScrollArea* sb = NULL;
    XWidget* entry = demo_wrap_page_scroll_ex(page, &sb);
    if (entry != page && sb && pageIdx >= 0 && pageIdx < 26)
        self->m_pageScrolls[pageIdx] = sb;
    return entry;
#else
    (void)self; (void)pageIdx;
    return page;
#endif
}

/** @brief 扩展页滚动壳豁免表（下标=exti=页索引-5）：true=真根已自带
 *         整页滚动承载（真根本身是滚动区，或页宿主内嵌滚动壳），堆叠
 *         换入时不再包裹防双层滚动条。 */
static const bool g_extPageScrollReady[19] = {
    false, /* exti0 条目视图 */
    true,  /* exti1 对话框（DlgPgScroll=XScrollArea 子类，自管排版） */
    false, /* exti2 高级控件 */
    false, /* exti3 图形效果 */
    false, /* exti4 虚拟键盘 */
    false, /* exti5 远程服务端 */
    false, /* exti6 远程客户端（远程视图自管触摸，壳只覆盖页边缘空白） */
    true,  /* exti7 占位（内置图表页，恒不查表） */
    true,  /* exti8 网络设置（NetPageHost 内嵌壳） */
    false, /* exti9 悬浮窗设置 */
    false, /* exti10 二维码 */
    false, /* exti11 软件更新 */
    true,  /* exti12 Modbus 主站（页宿主内嵌壳） */
    true,  /* exti13 Modbus 从站（真根=XScrollArea） */
    false, /* exti14 窗口设置 */
    true,  /* exti15 时钟与 DDNS（真根=XScrollArea） */
    true,  /* exti16 ATV61（真根=XScrollArea） */
    false, /* exti17 测试菜单（页内滚动区只管菜单树，页级另包） */
    true   /* exti18 Shell（真根=XScrollArea，页宿主=scroll 定式） */
};

/** @brief 扩展页真根 → 堆叠条目（按豁免表决定是否包滚动壳）。 */
static XWidget* demo_ext_stackEntry(DemoWin* self, int exti, XWidget* real)
{
#if XWIDGET_ON && XSCROLLAREA_ON && XABSTRACTSCROLLAREA_ON
    if (real && exti >= 0 && exti < 19 && !g_extPageScrollReady[exti]) {
        XScrollArea* wrapScroll = NULL;
        XWidget* entry = demo_wrap_page_scroll_ex(real, &wrapScroll);
        if (entry != real && self && 5 + exti < 26)
            self->m_pageScrolls[5 + exti] = wrapScroll; /* 漏斗刷新登记。 */
        return entry;
    }
#else
    (void)self; (void)exti;
#endif
    return real;
}

#if XWIDGET_ON && XLAYOUT_ON && XLAYOUT_STACKED_ON
/* 前向声明：本函数定义在 demo_page_name 之前。 */
static const char* demo_page_name(int index);

/* ==================== 扩展页全量懒分配（2026-10-19） ====================
 * 策略=全量懒分配（评审定版）：14 个扩展页统一「首显构建 + 常驻不销
 * 毁」——Top-6 重页折算收益 ≈6.10MB 已超全懒一半，多省 ≈4.54MB，且免
 * 「哪些页懒」清单维护；常驻策略销毁风险零触发（与现状同：页活到进
 * 程退出，demo 无页级析构钩子）。注册期只建空占位壳写入 m_extPages
 * 槽位本身（main 尾部几何分支查 m_extPages[0..11] 任一非空才取
 * 1000x700，槽位空会落 520x360 污染量化对照），真页根推迟到首显由
 * demo_ext_ensureBuilt 同位换入。工程要点落位：
 *  ① CLI autostart 豁免（红线）：--remote-server/--remote-client 预置
 *     时对应远程页在事件循环前 ensureBuilt（main 内，否则 autostart 守
 *     卫 s_rs.m_ready/s_rc.m_ready 恒假=被控端静默不监听）；
 *  ② 大表格收编（阻断项）：主文件大表格构建+挂接移入 exti0 真建后钩
 *     子 demo_ext_viewsBigTableHook（views 页 CHILD_ADDED 拦截收编机制
 *     不变），否则懒建后页 5 大表格消失；
 *  ③ 内建 autotest 效果页留证段先建再测（防证据静默缺失）；
 *  ⑤ 切页契约：demo_switchPage 在 setCurrentIndex 前确保目标页已建；
 *     autotest 调度先建再测。销毁语义与现状一致（既有 deleteLater/
 *     断链定式不触碰），仅占位壳退役走 XObject_deleteLater 仓库定式。
 */

/** @brief 扩展页构建注册表（[懒分配 2026-10-19] 自 DemoWin_create 局部
 *         kExtBuilders 提升为文件级：注册循环查表建占位壳，
 *         demo_ext_ensureBuilt 查表真建，autotest 调度查表甄别裁剪槽）。
 *         下标=页索引-5，下标 7=页 12（图表演示，内置页）恒 NULL 占位。 */
XWidget* (*const g_extBuilders[19])(XWidget*, DemoPageStatusFn, void*) = {
#if XINYUE_EMBEDDED
    /* 嵌入式（F407 外扩堆 1008KB）：全部扩展页暂不预建——键盘页的
     * 拼音引擎+虚拟键盘控件树为最大内存户；性能悬浮窗演示优先，
     * 键盘页待后备存储静态绑定（extbuf 640KB 方案）后接回。 */
    NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL,
    NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL
#else
    demo_page_views_build, demo_page_dialogs_build,
    demo_page_advanced_build, demo_page_effects_build,
    demo_page_keyboard_build, demo_page_remote_server_build,
    demo_page_remote_client_build, /* 2026-10-02 追加第 7 扩展页。 */
    NULL, /* 页 12（图表演示）内置页占位。 */
    demo_page_network_build, /* 2026-10-06 网络设置页（系统设置）。 */
    demo_page_overlay_settings_build, /* 2026-10-06 悬浮窗设置页。 */
    demo_page_qrcode_build, /* 2026-10-08 二维码演示页（控件类）。 */
    demo_page_update_build, /* 2026-10-07 软件更新页（系统设置）。 */
    demo_page_modbus_master_build, /* 2026-10-13 Modbus 主站页。 */
    demo_page_modbus_slave_build, /* 2026-10-13 Modbus 从站页。 */
    demo_page_window_settings_build, /* 2026-10-17 窗口设置页（系统设置）。 */
    demo_page_timeddns_build, /* 2026-10-16 时钟与 DDNS 页（系统设置）。 */
    demo_page_atv61_build, /* 2026-10-26 ATV61 变频器页（Modbus RTU 主站）。 */
    demo_page_testmenu_build, /* 2026-10-29 测试菜单页（控制台 test 菜单 GUI 化）。 */
    demo_page_shell_build /* 2026-10-29 Shell 页（页面内控制台，按需开关）。 */
#endif
};
/* 前向声明：扩展页状态回调定义在导航槽段（demo_nav19Slot 之后）。 */
static void demo_ext_page_status(void* user, const char* text);

#if XTABLEWIDGET_ON && XLAYOUT_ON && XLAYOUT_STACKED_ON
/** @brief exti0（条目视图页）真建后钩子：大表格构建+挂接收编。
 * @details [懒分配 2026-10-19·工程要点②] 原段在 DemoWin_create 内「if
 *          (m_extPages[0]) 组装大表格挂条目视图页根」——懒分配下注册
 *          期槽位是空占位壳且 if 无回落分支，真建推迟后大表格将凭空消
 *          失（views autotest 仅断言页内小表格不会红，但页 5 功能回归）。
 *          本钩子在 demo_ext_ensureBuilt 真建 exti0 后调用：以真页根为
 *          父 XTableWidget_init，views 页根控件子类的 CHILD_ADDED 拦截
 *          （xgui_demo_page_views.c views_adoptBigTable，buildDone 置位
 *          后新增子即大表格）自动收编进小表格行——机制与原「build 完成
 *          后主文件后挂」完全同路径。摆位/列宽逻辑逐式照搬原段。 */
static void demo_ext_viewsBigTableHook(DemoWin* self)
{
    if (!self || !self->m_extPages[0])
        return; /* 真页根缺席（裁剪回落）：零操作。 */
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
       二轮挂项「头文件」末字半切）。可视宽=表格宽−纵向条带 12，列合
       计须 ≤ 可视宽；宽根（面板收起 776→表格 372）用内容定宽
       128/72/60/112=372 恰满，窄根（面板展开表格 ~196）按比例收
       缩保「名称+类型」两主列可读，时间列让横向滚动。定版由 views 页
       根 resizeEvent（views_bigTableRefit）随堆叠几何分配承接。 */
    XWidget_show((XWidget*)&self->m_tableWidget);
}
#endif /* XTABLEWIDGET_ON && XLAYOUT_ON && XLAYOUT_STACKED_ON */

/** @brief 确保扩展页 exti 已真实构建（[懒分配 2026-10-19] 首显构建入口，
 *         幂等；全量懒分配唯一建页路径）。
 * @details 首调把注册期占位壳同位换入真页根：insertWidget 推位 +
 *          removeWidget 的 VXStackedLayout_takeAt 对 oldCurrent 补偿
 *          （index<oldCurrent → oldCurrent-1），堆叠下标=页索引与当前
 *          页全程稳定；槽位改指真根（adapt/autotest 契约口径）。构建
 *          返回 NULL（裁剪回落，如键盘页 XKEYBOARD_ON=0）时槽位清空，
 *          与旧注册期「NULL 跳过注册」口径一致，占位壳留堆叠保下标。
 *          常驻不销毁：已尝试（成功或回落）即短路，无二次构建。占位
 *          壳退役走 XObject_deleteLater 仓库定式（堆对象，removeWidget
 *          已隐藏并置 parent=NULL）。调用方：demo_switchPage（首显）、
 *          内建 autotest 效果页留证段、main 的 CLI autostart 豁免与键
 *          盘无头钩子豁免。 */
static void demo_ext_ensureBuilt(DemoWin* self, int exti)
{
    XWidget* placeholder;
    XWidget* real;
    if (!self || exti < 0 || exti >= 19)
        return;
    if (!g_extBuilders[exti])
        return; /* 裁剪注册表/内置图表页占位槽：零操作。 */
    if (self->m_extBuilt[exti])
        return; /* 已建/已尝试：常驻不销毁，零开销短路。 */
    placeholder = self->m_extPages[exti];
    self->m_extBuilt[exti] = true; /* 先置位：build 内布局激活/回调重入防递归。 */
    real = g_extBuilders[exti]((XWidget*)&self->m_base,
                               demo_ext_page_status, self);
    if (!real) {
        /* 裁剪回落：槽位清空=旧「跳过注册」口径（adapt/autotest 判空
           跳过）；占位壳留在堆叠，保「堆叠下标=页索引」不漂移。 */
        self->m_extPages[exti] = NULL;
        return;
    }
    self->m_extPages[exti] = real;
#if XTABLEWIDGET_ON && XLAYOUT_ON && XLAYOUT_STACKED_ON
    /* [页级滚动壳 2026-10-29 顺序约束] 大表格收编必须先于包裹：收编把
     * mid-init（vtable 未装）的表格插入 views 页根布局——root 尚无几何
     * 时布局激活惰性（历史行为，sizeHint 不被查询）；若先包后收编，
     * 包裹壳 setWidget 已按视口立即排版 root 并激活布局，mid-init 表格
     * 入布局即被 sizeHint 虚调用 → 崩溃（真机 420x640 --page 5 实证）。 */
    if (exti == 0)
        demo_ext_viewsBigTableHook(self); /* 工程要点②：大表格收编。 */
#endif
    if (placeholder &&
        XStackedLayout_indexOf(&self->m_stackLayout, placeholder) >= 0) {
        /* [页级滚动壳 2026-10-29] 堆叠条目=滚动壳包真根（豁免表甄别，
         * 见 DemoScrollHost 段）；m_extPages 仍指真根——adapt/autotest/
         * 大表格收编/远程门身份契约不变。 */
        XStackedLayout_insertWidget(&self->m_stackLayout, 5 + exti,
                                    demo_ext_stackEntry(self, exti, real));
        XStackedLayout_removeWidget(&self->m_stackLayout, placeholder);
        /* 占位壳已隐藏并摘出容器（removeWidget 置 parent=NULL）：
           XObject_deleteLater 延迟释放（仓库定式；XObject 析构清挂起
           事件，进程退出前未泵两序皆安全）。 */
        XObject_deleteLater((XObject*)placeholder);
    } else {
        /* 占位壳缺席（理论不可达，防御分支）：末尾追加兜底，页功能
           不受损（堆叠下标可能漂移，仅注册链异常时可达）。 */
        XStackedLayout_addWidget(&self->m_stackLayout,
                                 demo_ext_stackEntry(self, exti, real));
    }
}

/** @brief 扩展页（5~15）图形界面自动化验证调度。
 * @details 逐页切换并调用页面自带的 autotest（xgui_demo_pages.h 契约：
 *          事件注入 + getter 断言，全程非阻塞），汇总失败数；任一失败
 *          退出非零。结束恢复第 4 页，保持交互后截图口径不变。下标 7
 *          =页 12（图表演示，内置页无 autotest 契约）恒 NULL 跳过。 */
static int g_autotestPageOnly = -1; /**< --autotest-page 指定页号；-1=全量。 */

/** @brief 扩展页自测调度。
 * @details pageOnly 为 -1 时逐页全跑（历史口径）；>=0 时只跑该主页面号
 *          （5..20 的扩展页；其它页号不跑并报一行说明）。专项页号存在的
 *          原因：全量调度会在前置页（如 Modbus 从站）以存量崩溃中止，其
 *          后的页永远测不到，卡死类缺陷因此长期无门禁。 */
static void demo_ext_pages_autotest(DemoWin* demo)
{
    int (*const kTests[19])(XWidget*) = {
        demo_page_views_autotest, demo_page_dialogs_autotest,
        demo_page_advanced_autotest, demo_page_effects_autotest,
        demo_page_keyboard_autotest, demo_page_remote_server_autotest,
        demo_page_remote_client_autotest, /* 2026-10-02 追加第 7 扩展页。 */
        NULL, /* 页 12（图表演示）内置页占位。 */
        demo_page_network_autotest, /* 2026-10-06 网络设置页。 */
        demo_page_overlay_settings_autotest, /* 2026-10-06 悬浮窗设置页。 */
        demo_page_qrcode_autotest, /* 2026-10-08 二维码演示页。 */
        demo_page_update_autotest, /* 2026-10-07 软件更新页。 */
        demo_page_modbus_master_autotest, /* 2026-10-13 Modbus 主站页。 */
        demo_page_modbus_slave_autotest, /* 2026-10-13 Modbus 从站页。 */
        demo_page_window_settings_autotest, /* 2026-10-17 窗口设置页。 */
        demo_page_timeddns_autotest, /* 2026-10-16 时钟与 DDNS 页。 */
        demo_page_atv61_autotest, /* 2026-10-26 ATV61 变频器页。 */
        demo_page_testmenu_autotest, /* 2026-10-29 测试菜单页。 */
        demo_page_shell_autotest /* 2026-10-29 Shell 页。 */
    };
    int total = 0;
    int exti;
    for (exti = 0; exti < 19; ++exti) {
        int failures;
        if (!kTests[exti] || !g_extBuilders[exti])
            continue; /* 无契约/裁剪占位槽（内置图表页等），跳过 */
        if (g_autotestPageOnly >= 0 && 5 + exti != g_autotestPageOnly)
            continue; /* 专项页号：只跑点名的那一页。 */
        /* [懒分配 2026-10-19·工程要点⑤] 先建再测：懒分配下页根首显才
           真建，demo_switchPage 内部先 ensureBuilt 同位换入真页根再
           setCurrentIndex，此处槽位已指真根。 */
        demo_switchPage(demo, 5 + exti);
        if (!demo->m_extPages[exti])
            continue; /* 构建回落（裁剪配置返回 NULL）：同旧「槽位 NULL 跳过」。 */
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
#if XWIDGET_ON && XABSTRACTBUTTON_ON && XPUSHBUTTON_ON && XFRAME_ON && \
    XLABEL_ON && XLAYOUT_ON && XLAYOUT_STACKED_ON
static void demo_navUpdatePanel(DemoWin* self);
static void demo_layout_content(DemoWin* self);
static void demo_layoutModeUpdate(DemoWin* self);
#endif

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
    /* --orient N：首帧把顶层窗口切到指定方向（0/90/180/270）。
       自动化验证入口——靠鼠标点下拉框的像素坐标太脆弱，换机器/换 DPI
       就点空。只跑一次（m_orientApplied 标记），此后帧泵行为不变。
       几何按 0° 基准对调（与窗口设置页 wn_orientApply 同口径），
       旋转角先设、几何后设（平台层 footprint 对调读执行时刻的角）。 */
    if (demo->m_orientOpt >= 0 && !demo->m_orientApplied) {
        demo->m_orientApplied = true;
        {
            int deg = demo->m_orientOpt;
            XWidget* top = (XWidget*)&demo->m_base;
            int bw = XWidget_width(top);
            int bh = XWidget_height(top);
            if (bw < bh) { int t = bw; bw = bh; bh = t; }
            if (XWidget_setContentRotation(top, deg)) {
                int w = (deg == 90 || deg == 270) ? bh : bw;
                int h = (deg == 90 || deg == 270) ? bw : bh;
                XWidget_setGeometry(top, XWidget_x(top), XWidget_y(top), w, h);
                XWidget_setContentRotation(top, deg);
                demo->m_staticSceneDirty = true;
                XPrintf("XGuiWindowDemo: --orient %d -> %dx%d\n", deg, w, h);
            }
            else {
                XPrintf("XGuiWindowDemo: --orient %d 平台不支持\n", deg);
            }
        }
    }
    /* --remote-server CLI 预置的首帧自动启动（须在空闲闸门前——闸门
       开启时本泵早退；内部自带终态标记, 完成后零开销）。 */
    demo_page_remote_server_autostart();
    /* --remote-client CLI 预置的首帧自动连接（2026-10-02 追加; 同闸门
       口径, 内部自带终态标记）。 */
    demo_page_remote_client_autostart();
    /* [布局化 chrome 兜底门 2026-10-17] contentHost 几何由根布局
     * activation 异步落位（resize/边框开关/状态栏显隐/切停靠），页内
     * 排布与导航面板须跟随——每帧尺寸差检查（同值零成本）兜住一切
     * 来源，防 autotest 注入点击落在陈旧几何上。 */
    {
        int cw = XWidget_width(&demo->m_contentHost);
        int ch = XWidget_height(&demo->m_contentHost);
        if (cw != demo->m_lastContentW || ch != demo->m_lastContentH) {
            demo->m_lastContentW = cw;
            demo->m_lastContentH = ch;
#if XWIDGET_ON && XABSTRACTBUTTON_ON && XPUSHBUTTON_ON && XFRAME_ON && \
    XLABEL_ON && XLAYOUT_ON && XLAYOUT_STACKED_ON
            /* 形态判定基于**窗口**宽（内容区宽在紧凑态已占满，跨断点
               时内容区宽可能不变）——每轮兜底一次，代价=一次比较。 */
            demo_layoutModeUpdate(demo);
            demo_navUpdatePanel(demo);
            demo_layout_content(demo);
#endif
        }
    }
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
            /* [专项直达 2026-10-29] --autotest-page 指向扩展页（>=5）时
               跳过内置页自测：其一，直达语义本该只跑指定页；其二，内置
               自测含跨页导航（如输入页切页 5），会误触无关扩展页构建，
               令指定页之外的在途问题拦住专项回归。 */
            if (g_autotestPageOnly < 0 || g_autotestPageOnly < 5)
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
            /* 切到效果页：repaint 异步投递，本帧只切页，隔两帧再截图。
               [懒分配 2026-10-19·工程要点③] 留证前先建：懒分配下效果
               页未显不建，只查槽位会让证据静默缺失——「autotest 先建
               再测」覆盖内建交互 autotest 本段（ensureBuilt 幂等，
               demo_ext_pages_autotest 已建过则零开销）。 */
            demo_ext_ensureBuilt(demo, 3);
            if (demo->m_extPages[3]) {
                demo_switchPage(demo, 8);
                demo->m_staticSceneDirty = true;
                demo_repaint(demo);
            }
#endif
        }
        else if (demo->m_autoTestFrames >= 7) {
#if XWIDGET_ON && XLAYOUT_ON && XLAYOUT_STACKED_ON
            /* 效果页留证：启用态效果与无效果基线同屏，供集成阶段像素对照。
               [懒分配 2026-10-19·工程要点③] 同帧 5 口径：留证截图前确保
               效果页已真实构建（幂等短路，防证据静默缺失）。 */
            demo_ext_ensureBuilt(demo, 3);
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
    if (self->m_clockTimer != XTIMER_INVALID_ID) {
        XObject_killTimer((XObject*)self, self->m_clockTimer);
        self->m_clockTimer = XTIMER_INVALID_ID;
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
    if (timerId == self->m_clockTimer) {
        /* [时钟入布局 2026-10-29] 只改文本：宽度由顶栏布局按 sizeHint
         * 自动分配（setText → updateGeometry → 布局重排），APP 不再手算
         * 锚点/宽度。原浮层方案写死 180px，18px 真 Bold 下文本步进
         * 184px，右对齐把首个数字左缘裁掉。品牌条/应用栏各挂一个时钟，
         * 显隐随父栏，紧凑/宽屏切换无需额外代码。 */
        char buf[32];
        demo_clockFormatNow(buf, sizeof(buf));
        if (self->m_clockLabel)
            XLabel_setText_2(self->m_clockLabel, buf);
        if (self->m_clockLabelApp)
            XLabel_setText_2(self->m_clockLabelApp, buf);
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
    static const char* const kNames[24] = {
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
        "\xE6\x82\xAC\xE6\xB5\xAE\xE7\xAA\x97\xE8\xAE\xBE\xE7\xBD\xAE", /* 悬浮窗设置(2026-10-06 系统设置) */
        "\xE4\xBA\x8C\xE7\xBB\xB4\xE7\xA0\x81\xE6\xBC\x94\xE7\xA4\xBA",  /* 二维码演示(2026-10-08 控件类) */
        "\xE8\xBD\xAF\xE4\xBB\xB6\xE6\x9B\xB4\xE6\x96\xB0", /* 软件更新(2026-10-07 系统设置) */
        "Modbus \xE4\xB8\xBB\xE7\xAB\x99", /* Modbus 主站(2026-10-13) */
        "Modbus \xE4\xBB\x8E\xE7\xAB\x99", /* Modbus 从站(2026-10-13) */
        "\xE7\xAA\x97\xE5\x8F\xA3\xE8\xAE\xBE\xE7\xBD\xAE", /* 窗口设置(2026-10-17 系统设置) */
        "\xE6\x97\xB6\xE9\x92\x9F\xE4\xB8\x8E DDNS", /* 时钟与 DDNS(2026-10-16) */
        "ATV61 \xE5\x8F\x98\xE9\xA2\x91\xE5\x99\xA8", /* ATV61 变频器(2026-10-26) */
        "\xE6\xB5\x8B\xE8\xAF\x95\xE8\x8F\x9C\xE5\x8D\x95", /* 测试菜单(2026-10-29) */
        "Shell" /* Shell 控制台(2026-10-29) */
    };
    if (index < 0 || index > 23)
        return kNames[0];
    return kNames[index];
}

/* ==================== 浮动导航面板 ====================
 * 「先切一级分类、再选页面」两级导航（2026-10-06 一级菜单整改：收拢
 * 为「控件 / 系统设置 / 远程 / 测试」四大类——原七个平铺分类降为控件类内的
 * 子分组标题行，分类语义与用户操作面一致）。面板默认贴左停靠，「换
 * 边」钮循环 左→右→上→下，「收起」钮贴边成 26px 细条（点细条复原）。
 * 面板内 3 个一级分类钮常驻，活动分类的条目在手风琴下展开——分类即
 * 菜单第一级、页面钮即第二级（子分组标题为不可点的分组提示行）。
 * 内容区几何（demo_layout_content）按面板占位自动避让；resize/停靠/
 * 收起/切页统一走 demo_navUpdatePanel。 */
#define DEMO_NAV_PANEL_W 176   /**< 左右停靠面板默认宽（可拖范围由框架
                                *   getRange 按 smartMin 应用前钳位，2026-10-18
                                *   原 140..380 静态带已废——带外记忆冻结曾致
                                *   帧泵重申掰回=拖拽闪烁）。 */
#define DEMO_NAV_TB_H    100   /**< 上下停靠面板默认高（分割条可拖 92..320）。 */
#define DEMO_NAV_TB_MAX  320   /**< 上下停靠面板高上限（换行页面钮自动抬高至此）。 */
#define DEMO_NAV_STRIP_W 8     /**< 收起贴边分割条细条厚。 */
#define DEMO_NAV_CAT_N   4     /**< 一级分类数（=kNavGroups 项数；2026-10-29 +测试）。 */
#define DEMO_NAV_PAGE_N  24    /**< 页面数（2026-10-29：+测试菜单/Shell 页 22）。 */
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
    { "\xE5\x9B\xBE\xE8\xA1\xA8\xE6\xBC\x94\xE7\xA4\xBA", 12 },            /* 图表演示 */
    { "\xE4\xBA\x8C\xE7\xBB\xB4\xE7\xA0\x81\xE6\xBC\x94\xE7\xA4\xBA", 15 } /* 二维码演示(2026-10-08) */
};

/** @brief 系统设置类条目（2026-10-06 新增二级设置页）。 */
static const DemoNavItem kNavSystemItems[] = {
    { "\xE7\xBD\x91\xE7\xBB\x9C\xE8\xAE\xBE\xE7\xBD\xAE", 13 },             /* 网络设置 */
    { "\xE6\x82\xAC\xE6\xB5\xAE\xE7\xAA\x97\xE8\xAE\xBE\xE7\xBD\xAE", 14 }, /* 悬浮窗设置 */
    { "\xE8\xBD\xAF\xE4\xBB\xB6\xE6\x9B\xB4\xE6\x96\xB0", 16 },             /* 软件更新（2026-10-07 关于→软件更新；2026-10-11 双页并入移至页 16） */
    { "\xE7\xAA\x97\xE5\x8F\xA3\xE8\xAE\xBE\xE7\xBD\xAE", 19 },             /* 窗口设置（2026-10-17） */
    { "\xE6\x97\xB6\xE9\x92\x9F\xE4\xB8\x8E DDNS", 20 }                    /* 时钟与 DDNS(2026-10-16) */
};

/** @brief 远程类条目。 */
static const DemoNavItem kNavRemoteItems[] = {
    { "\xE8\xBF\x9C\xE7\xA8\x8B\xE7\xAA\x97\xE5\x8F\xA3", 10 },             /* 远程窗口 */
    { "\xE8\xBF\x9C\xE7\xA8\x8B\xE5\xAE\xA2\xE6\x88\xB7\xE7\xAB\xAF", 11 }, /* 远程客户端 */
    { "Modbus \xE4\xB8\xBB\xE7\xAB\x99", 17 },  /* Modbus 主站(2026-10-13) */
    { "Modbus \xE4\xBB\x8E\xE7\xAB\x99", 18 },   /* Modbus 从站(2026-10-13) */
    { "ATV61 \xE5\x8F\x98\xE9\xA2\x91\xE5\x99\xA8", 21 } /* ATV61 变频器(2026-10-26) */
};

/** @brief 测试类条目（2026-10-29 自远程类拆出：测试/调试工具自成一类，
 *          与远程设备页语义解耦；后续测试工具页统一入此分类）。 */
static const DemoNavItem kNavTestItems[] = {
    { "\xE6\xB5\x8B\xE8\xAF\x95\xE8\x8F\x9C\xE5\x8D\x95", 22 }, /* 测试菜单(2026-10-29) */
    { "Shell", 23 } /* Shell 控制台(2026-10-29) */
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
      (int)(sizeof(kNavRemoteItems) / sizeof(kNavRemoteItems[0])) },
    { "\xE6\xB5\x8B\xE8\xAF\x95",                                     /* 测试（2026-10-29 新增分类） */
      kNavTestItems,
      (int)(sizeof(kNavTestItems) / sizeof(kNavTestItems[0])) }
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

/** @brief 当前布局形态判定：逻辑宽 < DEMO_COMPACT_MAX_W → 紧凑（手机）。
 * @details 只看宽度不看高度：横屏手机逻辑宽 ≈869dp 落宽屏档（侧栏在
 *          横屏下信息密度更优）；竖屏手机 ≈400dp 落紧凑档。宽高都不看
 *          会让"竖屏平板"误判，故此处按宽度分档，与 Material 3 的
 *          compact/medium/expanded 窗口尺寸类同口径。 */
static int demo_layoutModeOf(DemoWin* self)
{
    int w;
    if (!self) return 0;
    w = XWidget_width(&self->m_base);
    if (w <= 0) return self->m_layoutMode;  /* 未排布：沿用当前形态。 */
    return (w < DEMO_COMPACT_MAX_W) ? 1 : 0;
}

/** @brief 应用栏标题同步：当前页名（菜单页显示「菜单」）。 */
static void demo_appBarTitleUpdate(DemoWin* self)
{
    int cur;
    if (!self) return;
    cur = XStackedLayout_currentIndex(&self->m_stackLayout);
#if XWIDGET_ON && XFRAME_ON && XLABEL_ON
    if (cur == self->m_menuPageIdx)
        XLabel_setText_2((XLabel*)&self->m_appBar, "\xE8\x8F\x9C\xE5\x8D\x95");
    else if (cur >= 0 && cur < DEMO_NAV_PAGE_N)
        XLabel_setText_2((XLabel*)&self->m_appBar, demo_page_name(cur));
    else
        XLabel_setText_2((XLabel*)&self->m_appBar, "XGui \xE6\x8E\xA7\xE4\xBB\xB6\xE6\xBC\x94\xE7\xA4\xBA");
#endif
    self->m_appBar.m_menuOpen = (cur == self->m_menuPageIdx);
}

/** @brief 紧凑形态：菜单页内排布（三大分区平铺 + 44dp 触控行高）。
 * @details 菜单页=内容堆叠的第 DEMO_MENU_PAGE_IDX 页，占满内容区；
 *          导航滚动容器从桌面导航面板搬进本页（控件搬家而非复制，
 *          页面按钮与分类按钮实体唯一，选中态互斥组不分裂）。条目平铺
 *          全部三个分区（不折叠）：手机上"折叠手风琴"要多点一次，
 *          平铺让 21 个目的地一屏可达（Material 抽屉在紧凑窗口的
 *          全屏变体）。子分组标题保留为轻量分隔标签。 */
static void demo_compactMenuLayout(DemoWin* self)
{
    int i;
    int y = 0;
    int mw;
    int mh;
    if (!self) return;
    mw = XWidget_width(&self->m_mobileMenu);
    mh = XWidget_height(&self->m_mobileMenu);
    if (mw < 1) mw = 1;
    if (mh < 1) mh = 1;
#if XSCROLLAREA_ON && XABSTRACTSCROLLAREA_ON
    XWidget_setGeometry((XWidget*)&self->m_navScroll, 0, 0, mw, mh);
    XWidget_show((XWidget*)&self->m_navScroll);
    for (i = 0; i < DEMO_NAV_CAT_N; ++i)
        XWidget_setParentPlain((XWidget*)&self->m_navCatBtns[i],
                               self->m_navContent);
    for (i = 0; i < DEMO_NAV_PAGE_N; ++i)
        XWidget_setParentPlain((XWidget*)&self->m_pageNav[i],
                               self->m_navContent);
    for (i = 0; i < DEMO_NAV_HEAD_N; ++i)
        XWidget_setParentPlain((XWidget*)&self->m_navHeadLabels[i],
                               self->m_navContent);
#endif
    {
        int head = 0;
        for (i = 0; i < DEMO_NAV_CAT_N; ++i) {
            int p;
            /* 分区标题（分类钮兼作分区头，样式与桌面分类钮一致）。 */
            XWidget_setGeometry((XWidget*)&self->m_navCatBtns[i], 8, y,
                                mw - 16, DEMO_MENU_CAT_H);
            XWidget_show((XWidget*)&self->m_navCatBtns[i]);
            y += DEMO_MENU_CAT_H + 2;
            for (p = 0; p < kNavGroups[i].count; ++p) {
                const DemoNavItem* item = &kNavGroups[i].items[p];
                if (item->page < 0) {
                    if (head < DEMO_NAV_HEAD_N) {
                        XLabel* label = &self->m_navHeadLabels[head++];
                        XLabel_setText_2(label, item->caption);
                        XLabel_setTextPixelSize(label, 12);
                        XWidget_setGeometry((XWidget*)label, 16, y + 2,
                                            mw - 32, 16);
                        XWidget_show((XWidget*)label);
                    }
                    y += 22;
                    continue;
                }
                XWidget_setGeometry((XWidget*)&self->m_pageNav[item->page],
                                    16, y, mw - 32, DEMO_MENU_ROW_H);
                XWidget_show((XWidget*)&self->m_pageNav[item->page]);
                y += DEMO_MENU_ROW_H;
            }
            y += 10; /* 分区间距。 */
        }
#if XSCROLLAREA_ON && XABSTRACTSCROLLAREA_ON
        XWidget_resize(self->m_navContent, mw, y > 1 ? y : 1);
#endif
        for (i = head; i < DEMO_NAV_HEAD_N; ++i)
            XWidget_hide((XWidget*)&self->m_navHeadLabels[i]);
    }
}

/** @brief 菜单页开合（应用栏导航图标 / Esc 返回键）。 */
static void demo_menuToggle(DemoWin* self)
{
    int cur;
    if (!self) return;
    if (self->m_layoutMode != 1) return;   /* 仅紧凑形态有菜单页。 */
    cur = XStackedLayout_currentIndex(&self->m_stackLayout);
    if (cur == self->m_menuPageIdx) {
        int back = self->m_pageBeforeMenu;
        if (back < 0 || back >= DEMO_NAV_PAGE_N) back = 0;
        XStackedLayout_setCurrentIndex(&self->m_stackLayout, back);
    } else {
        self->m_pageBeforeMenu = cur;
        XStackedLayout_setCurrentIndex(&self->m_stackLayout,
                                       self->m_menuPageIdx);
    }
    self->m_menuOpen = (XStackedLayout_currentIndex(&self->m_stackLayout) ==
                        self->m_menuPageIdx);
    demo_appBarTitleUpdate(self);
    /* [运行时开合重排 2026-10-25] XStackedLayout 只给**当前页**分配几何：
       菜单页在非当前态几何恒 0，切到当前才拿到 contentHost 的矩形——
       此刻必须紧跟一次导航面板重排（紧凑分支=compactMenuLayout，按新
       几何铺菜单项）。缺这一步时菜单项停在 0 尺寸几何上：真机/桌面
       运行时点 ☰ 打开菜单只剩一行"控件"挤在左上角（用户桌面实测）；
       --menu 启动路径看不见此账，因 main 里 toggle 之后本来就各有一次
       navUpdatePanel/layout_content。contentHost 尺寸未变，帧泵兜底门
       也不会代跑。 */
    demo_navUpdatePanel(self);
    demo_layout_content(self);
    XWidget_update(&self->m_base);
}

/** @brief 形态切换：紧凑（手机竖屏）↔ 宽屏（桌面/横屏）。
 * @details 紧凑态：品牌条让位应用栏；桌面导航面板收起（内容独占整宽）
 *          且手柄归零（8px 细条在 400dp 宽屏上占比过大、拖拽会把桌面
 *          侧栏拽出来）；导航滚动容器搬进移动端菜单页。宽屏态：全部
 *          还原（含用户手动收起态——紧凑态强制收起属形态策略，不该
 *          覆盖用户意图，故单独记一份用户值）。 */
static void demo_layoutModeUpdate(DemoWin* self)
{
    int mode;
    if (!self) return;
    mode = demo_layoutModeOf(self);
    if (mode == self->m_layoutMode) return;
    self->m_layoutMode = mode;
    if (mode == 1) {
#if XWIDGET_ON && XFRAME_ON && XLABEL_ON
        XWidget_hide((XWidget*)&self->m_brandBar);
        XWidget_show((XWidget*)&self->m_appBar);
#endif
        self->m_navCollapsedUser = self->m_navCollapsed;
        self->m_navCollapsed = true;
#if XSPLITTER_ON
        XSplitter_setHandleWidth(&self->m_navSplitter, 0);
#endif
#if XSCROLLAREA_ON && XABSTRACTSCROLLAREA_ON
        XWidget_setParentPlain((XWidget*)&self->m_navScroll,
                               &self->m_mobileMenu);
#endif
    } else {
#if XWIDGET_ON && XFRAME_ON && XLABEL_ON
        XWidget_show((XWidget*)&self->m_brandBar);
        XWidget_hide((XWidget*)&self->m_appBar);
#endif
        self->m_navCollapsed = self->m_navCollapsedUser;
#if XSPLITTER_ON
        XSplitter_setHandleWidth(&self->m_navSplitter, 8);
#endif
#if XSCROLLAREA_ON && XABSTRACTSCROLLAREA_ON
        XWidget_setParentPlain((XWidget*)&self->m_navScroll,
                               &self->m_navPanel);
#endif
    }
    /* 形态切换后菜单页状态复位：紧凑→宽屏时若停在菜单页，回到原页。 */
    if (mode == 0 &&
        XStackedLayout_currentIndex(&self->m_stackLayout) ==
            self->m_menuPageIdx) {
        int back = self->m_pageBeforeMenu;
        if (back < 0 || back >= DEMO_NAV_PAGE_N) back = 0;
        XStackedLayout_setCurrentIndex(&self->m_stackLayout, back);
    }
    self->m_menuOpen = (XStackedLayout_currentIndex(&self->m_stackLayout) ==
                        self->m_menuPageIdx);
    demo_appBarTitleUpdate(self);
    demo_navUpdatePanel(self);
    demo_layout_content(self);
}

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
    int w;
    int h;
    int bottomH;
    int i;
    int y;
    int head;
    int rowsNeeded;
    int pw;
    int ph;
    XLayout* mid = NULL;
    (void)mid; /* [XSplitter 迁移] 中段布局器已退役，占位防未用告警。 */
    if (!self) return;
    w = XWidget_width(&self->m_base);
    h = XWidget_height(&self->m_base);
    if (w < 1) w = 1;
    if (h < 1) h = 1;
    if (self->m_navDock < 0 || self->m_navDock > 3)
        self->m_navDock = 0;
    if (self->m_navCategory < 0 || self->m_navCategory >= DEMO_NAV_CAT_N)
        self->m_navCategory = 0;
    /* 横版展开时按页面钮换行数自动抬高面板高（下限钳位）。 */
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
    bottomH = self->m_statusBarVisible ? DEMO_STATUS_H : 0;
    /* [XSplitter 迁移 2026-10-17] 中段=框架分割条（替代四方向盒式布局
     * +DemoSplitter 组合）：方向随停靠（左右=水平 1/上下=垂直 2），条目
     * 顺序=停靠侧（面板左/上=索引 0，右/下=索引 1），面板尺寸=setSizes
     * 比例意图（拖拽由框架手柄原生接管并记忆，随窗缩放保比例，对标
     * Qt）；收起=面板零宽、把手贴窗口缘。 */
#if XSPLITTER_ON
    {
        int pi = XSplitter_indexOf(&self->m_navSplitter,
                                   (XWidget*)&self->m_navPanel);
        bool wantFirst;
        int total;
        int navSize;
        int sizes[2];
        /* 记忆值（m_navWidth/m_navTBH）只由 splitterMoved 信号（真实
         * 拖拽）更新，此处每次 resize 重申 px 定宽意图——setSizes 是
         * 比例语义，若不重申，窗口变宽时面板会按旧比例涨宽。 */
        {
            int orient = (self->m_navDock <= 1) ? 1 : 2;
            if (XSplitter_orientation(&self->m_navSplitter) != orient)
                XSplitter_setOrientation(&self->m_navSplitter, orient);
        }
        wantFirst = (self->m_navDock == 0 || self->m_navDock == 2);
        pi = XSplitter_indexOf(&self->m_navSplitter,
                               (XWidget*)&self->m_navPanel);
        if (pi >= 0 && ((pi == 0) != wantFirst)) {
            /* 换侧：insertWidget 已按 Qt 语义支持「已在内则移到新位」
             * （qsplitter.cpp insertWidget 的 list.move），直接按位插入
             * 即可，无需摘出/重挂。 */
            XSplitter_insertWidget(&self->m_navSplitter,
                                   wantFirst ? 0 : 1,
                                   (XWidget*)&self->m_navPanel);
        }
        if (self->m_navCollapsed)
            XWidget_hide(&self->m_navPanel);
        else
            XWidget_show(&self->m_navPanel);
        /* 尺寸意图：收起=[0,1]（setSizes 比例语义下 0 意图=面板归零、
         * 把手贴缘；0,0 会被当全未设定均分故取 1 兜底）；展开=记忆值，
         * 内容侧余量。 */
        pi = XSplitter_indexOf(&self->m_navSplitter,
                               (XWidget*)&self->m_navPanel);
        total = (self->m_navDock <= 1)
                    ? XWidget_width(&self->m_navSplitter)
                    : XWidget_height(&self->m_navSplitter);
        /* [无损尺寸 2026-10-17] setSizes 内部按 contentLen（总宽-手柄
         * 宽×（页数-1)）归一落位——传入总和必须与之同口径，否则每轮
         * 回读-重写都损失 ~3px（面板 176 一路衰到 139，按钮位移把
         * 固定坐标的换边点击顶进收起钮=「右停靠面板消失」假象）。 */
        total -= 8 * (XSplitter_count(&self->m_navSplitter) - 1);
        if (total < 128) total = 128;
        navSize = self->m_navCollapsed
                      ? 0
                      : (self->m_navDock <= 1 ? self->m_navWidth
                                              : self->m_navTBH);
        if (navSize > total - 72) navSize = total - 72;
        if (navSize < 0) navSize = 0;
        if (self->m_navCollapsed) {
            /* [⑤右修复 2026-10-18] 收起零宽放面板本位（原硬编码 {0,1}
             * 无视位次：右停靠（pi=1）点收起钮把内容页归零=页面内容整
             * 体消失+带落左缘+贴缘把手被 W 带吞=永久死态）。 */
            if (pi < 0) pi = 0;
            sizes[pi] = 0;
            sizes[1 - pi] = 1;
        } else if (pi == 0) {
            sizes[0] = navSize;
            sizes[1] = total - navSize;
        } else {
            sizes[0] = total - navSize;
            sizes[1] = navSize;
        }
        /* [⑥修复 2026-10-18] 重申钳底：非收起态双页意图均不得 ≤0——
         * 缩窗瞬间重申/布局与陈旧 total 交错曾算出 0 宽意图（setSizes
         * [0,1] 收起签名）被 movedSlot 固化为收起态且持续劣化。 */
        if (!self->m_navCollapsed) {
            if (sizes[0] < 1) sizes[0] = 1;
            if (sizes[1] < 1) sizes[1] = 1;
        }
        XSplitter_setSizes(&self->m_navSplitter, sizes, 2);
        /* [⑤左修复 2026-10-18] HitEdgeBand 让位例外按最深命中件自身
         * 属性判（XWindowDecoration.c :1879-1885），部件模型下贴缘命中
         * 件=把手子件（属性声明在 navSplitter 上够不着）——此处把属性
         * 补挂到把手（navUpdatePanel 每轮覆盖把手创建时机），收起态贴
         * 缘把手 [0,8) 不再被 CSD W 向改尺寸带吞（唯一展开入口）。 */
        {
            XSplitterHandle* h = XSplitter_handle(&self->m_navSplitter, 0);
            if (h)
                XWidget_setAttribute((XWidget*)h,
                                     XWidgetAttribute_HitEdgeBand, true);
        }
    }
#endif
    pw = XWidget_width(&self->m_navPanel);
    ph = XWidget_height(&self->m_navPanel);
    if (self->m_layoutMode == 1) {
        /* 紧凑形态（手机竖屏）：桌面导航面板强制收起且隐藏——内容区独
           占整宽，导航改由移动端菜单页承载（☰ 打开）。面板自身几何
           无意义，条目排布交给 demo_compactMenuLayout。 */
        demo_compactMenuLayout(self);
        return;
    }
    if (self->m_navDock <= 1) {
        int head;
        int scrollTop = 30; /* 标题行高（title 5..27 + 间隙）。 */
        int scrollH = ph - scrollTop;
        XWidget_setGeometry(&self->m_navTitle, 8, 5, 60, 22);
        XWidget_show(&self->m_navTitle);
        XPushButton_setText_2(&self->m_navDockBtn,
                              "\xE6\x8D\xA2\xE8\xBE\xB9"); /* 换边 */
        /* [删「收起」2026-10-25] 用户裁定去掉收起钮（横屏改用顶栏 ☰
         * 打开菜单页，与竖屏同一套逻辑）。「换边」独占标题行右端。 */
        XWidget_setGeometry(&self->m_navDockBtn, pw - 54, 5, 46, 22);
        XWidget_show(&self->m_navDockBtn);
#if XSCROLLAREA_ON && XABSTRACTSCROLLAREA_ON
        if (scrollH < 1) scrollH = 1;
        XWidget_setGeometry((XWidget*)&self->m_navScroll, 0, scrollTop,
                            pw, scrollH);
        XWidget_show((XWidget*)&self->m_navScroll);
        XWidget_raise((XWidget*)&self->m_navScroll);
        for (i = 0; i < DEMO_NAV_CAT_N; ++i)
            XWidget_setParentPlain((XWidget*)&self->m_navCatBtns[i],
                                   self->m_navContent);
        for (i = 0; i < DEMO_NAV_PAGE_N; ++i) {
            if (XWidget_parentWidget((XWidget*)&self->m_pageNav[i]) !=
                self->m_navContent)
                XWidget_setParentPlain((XWidget*)&self->m_pageNav[i],
                                       self->m_navContent);
        }
        for (i = 0; i < DEMO_NAV_HEAD_N; ++i) {
            XWidget_setParentPlain((XWidget*)&self->m_navHeadLabels[i],
                                   self->m_navContent);
        }
#endif
        y = 0; /* y 从此为 content 域坐标（滚动容器内容高）。 */
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
#if XSCROLLAREA_ON && XABSTRACTSCROLLAREA_ON
        XWidget_resize(self->m_navContent, pw, y > 1 ? y : 1);
#endif
        for (i = head; i < DEMO_NAV_HEAD_N; ++i)
            XWidget_hide((XWidget*)&self->m_navHeadLabels[i]);
        for (i = 0; i < DEMO_NAV_PAGE_N; ++i) {
            if (demo_navCategoryForPage(i) != self->m_navCategory)
                XWidget_hide(&self->m_pageNav[i]);
        }
    } else {
        /* 横版展开：标题行+一级分类钮一行+活动分类页面钮换行铺放。 */
        int bw = (pw - 16) / DEMO_NAV_CAT_N;
        int p;
        int x;
        int row;
#if XSCROLLAREA_ON && XABSTRACTSCROLLAREA_ON
        XWidget_hide((XWidget*)&self->m_navScroll);
        for (i = 0; i < DEMO_NAV_CAT_N; ++i)
            XWidget_setParentPlain((XWidget*)&self->m_navCatBtns[i],
                                   &self->m_navPanel);
        for (i = 0; i < DEMO_NAV_PAGE_N; ++i)
            XWidget_setParentPlain((XWidget*)&self->m_pageNav[i],
                                   &self->m_navPanel);
        for (i = 0; i < DEMO_NAV_HEAD_N; ++i)
            XWidget_setParentPlain((XWidget*)&self->m_navHeadLabels[i],
                                   &self->m_navPanel);
#endif
        if (bw > 120) bw = 120;
        if (bw < 56) bw = 56;
        XWidget_setGeometry(&self->m_navTitle, 8, 4, 60, 22);
        XWidget_show(&self->m_navTitle);
        XPushButton_setText_2(&self->m_navDockBtn,
                              "\xE6\x8D\xA2\xE8\xBE\xB9"); /* 换边 */
        /* [删「收起」2026-10-25] 同竖版分支：收起钮不再参与布局。 */
        XWidget_setGeometry(&self->m_navDockBtn, pw - 54, 4, 46, 22);
        XWidget_show(&self->m_navDockBtn);
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
}

/** @brief 分割条手柄点击槽（XSplitter handleClicked 信号）：双击=收起/
 *  展开切换；空闲点击=仅收起态展开（Qt 双击折叠交互位 + 保留 demo
 *  「收起态单击细条展开」习惯；XSplitter 只发信号不内置折叠策略，
 *  面板零宽几何由 navUpdatePanel 的 setSizes 意图落位）。 */
static void demo_navSplitHandleSlot(XObject* receiver, XVarList* args)
{
    DemoWin* self = (DemoWin*)receiver;
    if (!self || !args) return;
    XVarList_args_2(args, int, index, int, doubleClick);
    (void)index;
    if (doubleClick) {
        self->m_navCollapsed = !self->m_navCollapsed;
        demo_navUpdatePanel(self);
        demo_layout_content(self);
    } else if (self->m_navCollapsed) {
        self->m_navCollapsed = false;
        demo_navUpdatePanel(self);
        demo_layout_content(self);
    }
}

/** @brief 分割条拖拽落位槽（splitterMoved 信号，仅真实拖拽触发）：把
 *  拖后尺寸写入记忆值（拖合法值全采纳），供换边恢复与 px 定宽重申。
 *  [记忆带放开 2026-10-18] 原 140..380/92..320 带内才采纳=带外记忆
 *  冻结，帧泵重申按旧记忆把拖拽结果掰回=第二处闪烁源（与框架 getRange
 *  缺 smartMin 钳位同病）；拖拽合法区间已由框架 getRange 应用前钳定，
 *  此处如实记忆即可（0=折叠仍拒绝，见下）。 */
static void demo_navSplitMovedSlot(XObject* receiver, XVarList* args)
{
    DemoWin* self = (DemoWin*)receiver;
    int cs[2] = {0, 0};
    int pi;
    if (!self || !args) return;
    XVarList_args_2(args, int, pos, int, index);
    (void)index;
    pi = XSplitter_indexOf(&self->m_navSplitter,
                           (XWidget*)&self->m_navPanel);
    if (self->m_navCollapsed) {
        /* [④修复 2026-10-18] 收起态把手拖拽=回拖展开通道（配合装饰层
         * HitEdgeBand 沿父链让位：贴缘把手在收起态可按可拖）。把位揭示
         * 的面板空间过迟滞阈值（40，对标 adjustPos Threshold）即展开并
         * 以揭示宽度为新记忆；未过阈回推重申（死区，免内容被隐性吸走
         * ——拖拽先按光标落位、此处逐事件顶回）。 */
        int total = (self->m_navDock <= 1)
                        ? XWidget_width(&self->m_navSplitter)
                        : XWidget_height(&self->m_navSplitter);
        int space;
        if (pi < 0) return;
        space = (pi == 0) ? pos
                          : total - XSplitter_handleWidth(&self->m_navSplitter) - pos;
        if (space >= 40) {
            if (self->m_navDock <= 1)
                self->m_navWidth = space;
            else
                self->m_navTBH = space;
            self->m_navCollapsed = false;
        }
        demo_navUpdatePanel(self);
        return;
    }
    if (pi < 0) return;
    XSplitter_sizes(&self->m_navSplitter, cs, 2);
    if (cs[pi] <= 0) {
        /* 拖到零宽=转折叠态（Qt farMin 折叠的简化，免迟滞）：否则拖到
         0 后重申仍按旧记忆落位=0↔记忆宽逐帧掰回（左缘同款闪烁）。
         展开走既有 handleClicked/收起钮路径，恢复记忆宽度。 */
        if (!self->m_navCollapsed) {
            self->m_navCollapsed = true;
            demo_navUpdatePanel(self);
        }
        return;
    }
    if (self->m_navDock <= 1)
        self->m_navWidth = cs[pi];
    else
        self->m_navTBH = cs[pi];
    demo_layout_content(self); /* 内容区自适应随面板新尺寸重排。 */
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

/** @brief 品牌条 ☰ 点击：收起/展开左侧导航面板。
 *  @details 与已删除的「收起」按钮同一行为——翻转 m_navCollapsed（并
 *           同步用户记忆值，避免形态切换时被旧值覆盖）后重排面板与
 *           内容区。品牌条子类在文件前段，故经前置声明调用本函数。 */
static void demo_brandNavToggle(struct DemoWin* self_)
{
    DemoWin* self = (DemoWin*)self_;
    if (!self) return;
    self->m_navCollapsed = !self->m_navCollapsed;
    self->m_navCollapsedUser = self->m_navCollapsed;
    demo_navUpdatePanel(self);
    demo_layout_content(self);
    XWidget_update((XWidget*)&self->m_base);
}

/** @brief 收起/展开钮：完整面板 ⇔ 贴边细条。 */
static void demo_navCollapseSlot(XObject* receiver, XVarList* args)
{
    DemoWin* self = (DemoWin*)receiver;
    (void)args;
    if (!self) return;
    self->m_navCollapsed = !self->m_navCollapsed;
    self->m_navCollapsedUser = self->m_navCollapsed;
    /* 品牌条图标绘制不变（☰ 恒为三横条），但收起后内容区宽度变化需要
     * 重排；此处只做重排，图标本身无状态。
     * [横屏 ☰ 2026-10-25] 本槽现由品牌条导航图标调用（原「收起」按钮
     * 已删），行为不变：切换 m_navCollapsed 并重排面板/内容。 */
    demo_navUpdatePanel(self);
    demo_layout_content(self);
}
#endif /* 浮动导航面板门 */

/** @brief 内建页（0/1/2/3/12）紧凑形态自适应重排（demo_layout_content
 *         统一漏斗逐路径调用，与扩展页 demo_page_*_adapt 同口径）。
 * @details 真机竖屏 400dp 体检暴露的内建页三类问题，均为构造期按桌面
 *          口径定值、不随窗宽重排所致：
 *          - 页 0/1/2 控件行高 22-36px、页 12 切换钮行高 24px，低于
 *            手机触控目标下限（约 40 逻辑像素），拇指难以准确点按；
 *          - 页 3 顶行=左列分组框定宽 288+右列 stretch，右列起点
 *            x≈316，整列（下拉框/拨盘行/数码管行/日期三件套）越出
 *            400dp 窗口右缘成列裁切；
 *          - 页 12 五钮固定 84 宽，5×84+4×8=452px 超出内容宽，第 5 钮
 *            整体出界不可点。
 *          处理口径：紧凑档（m_layoutMode=1，窗宽<600dp）页 0/1/2 可
 *          点行高提到 ≥44 并单列纵排，页 3 顶行翻纵向单列流式，页 12
 *          五钮按内容宽均分且高 40；宽屏档逐一还原构造期定值，桌面
 *          1000x700 观感零变化。 */
static void demo_corePages_adapt(DemoWin* self)
{
    bool compact;
    int pw;
    if (!self) return;
    compact = (self->m_layoutMode == 1);
    /* 页内可用宽：页根几何由堆叠随 contentHost 分配，取不到时按窗口
     * 宽扣内容区左右边距兜底；仅用于钳宽防右缘出窗，不影响分档。 */
    pw = XWidget_width(&self->m_pageButtons);
    if (pw < 1) pw = XWidget_width(&self->m_base) - 24;
    if (pw < 1) pw = 1;
    /* ---- 页 0：按钮/命令链接/工具按钮/状态行/按钮盒（构造期绝对坐
     *       标，无布局承载，直接按档回写几何）。 ---- */
    XWidget_setGeometry((XWidget*)&self->m_button, 40, 48, 180,
                        compact ? 44 : 36);
#if XWIDGET_ON && XABSTRACTBUTTON_ON && XPUSHBUTTON_ON && XCOMMANDLINKBUTTON_ON
    /* 命令链接 54 高已达标，窄宽只钳右缘。 */
    XWidget_setGeometry((XWidget*)&self->m_commandLink, 40, 104,
                        compact ? (pw - 48 < 320 ? pw - 48 : 320) : 320, 54);
#endif
#if XWIDGET_ON && XABSTRACTBUTTON_ON && XTOOLBUTTON_ON && XMENU_ON
    XWidget_setGeometry((XWidget*)&self->m_toolButton, 40, 168, 150,
                        compact ? 44 : 32);
#endif
    XWidget_setGeometry((XWidget*)&self->m_linkLabel, 40,
                        compact ? 222 : 212,
                        compact ? (pw - 48 < 420 ? pw - 48 : 420) : 420, 24);
#if XDIALOGBUTTONBOX_ON
    /* 盒内钮高 26 由 XDialogButtonBox 的 relayout 定死（框架层），demo
     * 侧加高盒体只徒增空边；紧凑档仅钳盒宽防右缘出窗（340+40>376）。 */
    XWidget_setGeometry((XWidget*)&self->m_buttonBox, 40,
                        compact ? 256 : 250,
                        compact ? (pw - 80 < 340 ? pw - 80 : 340) : 340, 40);
#endif
#if XWIDGET_ON && XABSTRACTBUTTON_ON && XCHECKBOX_ON
    /* ---- 页 1：复选框 26→44；单选 22→44 并由桌面并排改紧凑纵排
     *       （40dp 触控行=单列流式）。 ---- */
    XWidget_setGeometry((XWidget*)&self->m_checkBox, 40, 48,
                        compact ? 240 : 220, compact ? 44 : 26);
#endif
#if XWIDGET_ON && XABSTRACTBUTTON_ON && XRADIOBUTTON_ON
    if (compact) {
        XWidget_setGeometry((XWidget*)&self->m_radioA, 40, 104, 140, 44);
        XWidget_setGeometry((XWidget*)&self->m_radioB, 40, 158, 140, 44);
    } else {
        XWidget_setGeometry((XWidget*)&self->m_radioA, 40, 88, 120, 22);
        XWidget_setGeometry((XWidget*)&self->m_radioB, 180, 88, 120, 22);
    }
#endif
    XWidget_setGeometry((XWidget*)&self->m_choiceLabel, 40,
                        compact ? 214 : 124,
                        compact ? (pw - 48 < 420 ? pw - 48 : 420) : 420, 24);
#if XPUSHBUTTON_ON
    /* ---- 页 2：上一页/下一页 28→44（触控高度），横向余宽让给钮宽。 */
    if (compact) {
        XWidget_setGeometry((XWidget*)&self->m_stackPrevButton,
                            40, 170, 130, 44);
        XWidget_setGeometry((XWidget*)&self->m_stackNextButton,
                            180, 170, 130, 44);
    } else {
        XWidget_setGeometry((XWidget*)&self->m_stackPrevButton,
                            40, 170, 90, 28);
        XWidget_setGeometry((XWidget*)&self->m_stackNextButton,
                            136, 170, 90, 28);
    }
#endif
#if XGROUPBOX_ON && XLINEEDIT_ON && XSPINBOX_ON && XABSTRACTSLIDER_ON && \
    XSLIDER_ON && XPROGRESSBAR_ON
    /* ---- 页 3：窄宽单列。顶行翻纵向（左列[分组框+多行编辑]在上、
     *       右列输入族流到其下），余高交左列多行编辑吸收；右列各行
     *       本就满宽铺放，不再被 288 定宽左列挤出窗口。宽屏还原横向
     *       双列（余高仍归右列尾部伸缩空白，对标构造期口径）。 ---- */
    XBoxLayout_setDirection(&self->m_inputsTopRow,
                            compact ? XBoxLayoutDirection_TopToBottom
                                    : XBoxLayoutDirection_LeftToRight);
    XBoxLayout_setStretch(&self->m_inputsTopRow, 0, compact ? 1 : 0);
    XBoxLayout_setStretch(&self->m_inputsTopRow, 1, compact ? 0 : 1);
#endif
#if XCHARTS_ON
    /* ---- 页 12：切换钮行。紧凑档五钮按内容宽均分（钳 48..84）、行高
     *       40 达触控标准，总宽不再超出 400dp；宽屏还原 84x24 构造期
     *       定值。 ---- */
    {
        int cw = XWidget_width(&self->m_pageChart);
        int bw;
        int bh = compact ? 40 : 24;
        int i;
        XPushButton* const btns[5] = {
            &self->m_btnLegend, &self->m_btnGrid, &self->m_btnTitle,
            &self->m_btnSeries, &self->m_btnRange
        };
        if (cw < 1) cw = pw;
        bw = 84;
        if (compact) {
            bw = (cw - 24 - 4 * 8) / 5; /* 扣页边距 12x2 与段距 8x4。 */
            if (bw < 48) bw = 48;
            if (bw > 84) bw = 84;
        }
        for (i = 0; i < 5; ++i)
            XWidget_setFixedSize((XWidget*)btns[i], bw, bh);
    }
#endif
}

/** @brief 扩展页自适应重排统一漏斗（切页/resize/startup/停靠切换全路径调用）。
 * @details [布局化 chrome 三段 2026-10-17] 主内容堆叠已托管到 contentHost
 *          （装配段 XWidget_setLayout，页面 addWidget 时自动重挂为
 *          contentHost 子）：页面矩形由框架随 contentHost 尺寸变化自动
 *          分配（宿主自身坐标 + contentsMargins 12/8/12/0 呼吸边），
 *          品牌条/状态栏/CSD/导航面板/分割条让位全部由根布局链分段完成。
 *          原手动 setGeometry（矩形按窗口坐标解释，页面压盖品牌条——
 *          DDE 探针实测错位 y=68=CSD 28+品牌 40）与导航避让块（面板已
 *          是中段布局条目、contentHost 尺寸已扣其占位，再避=双重计入；
 *          收起 8px 细条与底部状态栏 26px 同理）整体删除。堆叠几何随
 *          contentHost resize 自动重激活（XWidget_recomputeGeometry），
 *          切页由 setCurrentIndex 回贴既有几何——本函数只分发扩展页
 *          adapt：各页按当前根几何重排（登记表下标=页索引-5）。 */
static void demo_layout_content(DemoWin* self)
{
    /* [页 3 窄档翻排 2026-10-27 视觉走查 P2-5] 输入页顶行（左列 288+右
     * 列）在 400 级竖屏放不下：右列被压到 ~90px，表盘左缘出负裁、字体
     * 下拉被压扁。窄档顶行翻竖排（左列/右列各占全宽），宽档复原横排
     * （幂等，布局器内部失效重排）。 */
#if XWIDGET_ON && XLAYOUT_ON && XLAYOUT_BOX_ON && XCOMBOBOX_ON
    if (self && XWidget_width(&self->m_pageInputs) > 0) {
        bool narrow =
            XWidget_width(&self->m_pageInputs) < 600;
        XBoxLayout_setDirection(&self->m_inputsTopRow,
            narrow ? XBoxLayoutDirection_TopToBottom
                   : XBoxLayoutDirection_LeftToRight);
    }
#endif
    if (!self) return;
    /* 内建页（0/1/2/3/12）紧凑形态重排：与扩展页 adapt 同走本漏斗，
     * 切页/resize/形态切换全路径生效（见函数头注）。 */
    demo_corePages_adapt(self);
    /* 扩展页自适应重排（xgui_demo_pages.h 契约）：内容区几何随 CSD
     * 系统栏/窗口尺寸/导航面板占位变化，各页按当前根几何重排——本函数
     * 在切页/resize/startup/停靠切换全路径执行；登记表下标=页索引-5
     * （0=条目视图 2=高级 3=效果 6=远程客户端 8=网络设置 9=悬浮窗
     * 设置 10=二维码 11=软件更新，见 kExtBuilders 顺序）。 */
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
    if (self->m_extPages[10])
        demo_page_qrcode_adapt(self->m_extPages[10]); /* 2026-10-08 二维码页。 */
    if (self->m_extPages[11])
        demo_page_update_adapt(self->m_extPages[11]); /* 2026-10-07 软件更新页。 */
    /* [性能 2026-10-17] 摘除 Modbus 调试打印 layout ext12/ext13——本
     * 漏斗每 resize 步必跑，控制台写在拖拽期是纯开销。 */
    if (self->m_extPages[12])
        demo_page_modbus_master_adapt(self->m_extPages[12]); /* 2026-10-14 Modbus 主站页滚动承载。 */
    if (self->m_extPages[13])
        demo_page_modbus_slave_adapt(self->m_extPages[13]); /* 2026-10-14 Modbus 从站页。 */
    if (self->m_extPages[14])
        demo_page_window_settings_adapt(self->m_extPages[14]); /* 2026-10-17 窗口设置页。 */
    if (self->m_extPages[15])
        demo_page_timeddns_adapt(self->m_extPages[15]); /* 2026-10-16 时钟与 DDNS 页。 */
    if (self->m_extPages[16])
        demo_page_atv61_adapt(self->m_extPages[16]); /* 2026-10-26 ATV61 变频器页。 */
    if (self->m_extPages[17])
        demo_page_testmenu_adapt(self->m_extPages[17]); /* 2026-10-29 测试菜单页（横竖屏双栏翻转）。 */
    if (self->m_extPages[18])
        demo_page_shell_adapt(self->m_extPages[18]); /* 2026-10-29 Shell 页（契约空操作）。 */
    /* 远程服务器页（2026-10-06 补入统一漏斗）：此前只在 resizeEvent
     * 单独调用，切页路径漏跑——XStackedLayout StackOne 只给当前页
     * 分配几何，resize 落在别的页时本页根保持陈旧尺寸，adapt 算出
     * 全页错位（用户实测：服务地址行 95px 窄条悬页中）。切页路径
     * setCurrentIndex 已回贴堆叠既有几何（三段托管后=contentHost
     * 局部），本漏斗随后跑 adapt，切页/resize/startup/停靠全路径
     * 根尺寸都新鲜。 */
    demo_page_remote_server_adaptWidth();
#if XWIDGET_ON && XSCROLLAREA_ON && XABSTRACTSCROLLAREA_ON
    /* [页级滚动壳 2026-10-29] 漏斗收尾：当前页若在滚动壳内，页内档位
     * 重排（adapt 改内部控件约束）后内容提示已变，重排版一次让滚动
     * 范围即时跟上（幂等；几何未变时 setContentSize 短路）——统一收
     * 口 network/modbus 页各自 adapt 内显式调用的同款刷新。 */
    {
        int cur = XStackedLayout_currentIndex(&self->m_stackLayout);
        if (cur >= 0 && cur < 26 && self->m_pageScrolls[cur])
            XScrollArea_updateWidgetGeometry(self->m_pageScrolls[cur]);
    }
#endif
}

/** @brief 切换主内容页面：更新堆叠布局当前页、重新分配几何并更新状态栏。 */
static void demo_switchPage(DemoWin* self, int index)
{
    if (!self) return;
#if XWIDGET_ON && XKEYBOARD_ON
    /* 虚拟键盘 autoPopup 随页切换（2026-10-03 用户反馈键盘页点不弹的
       根因收口）：RC 页曾把单例 autoPopup 全局关断，离开 RC 页后键盘
       页等其余页面点击不弹。改为进入远程客户端页（index==11）时关断
       （RC 页触摸点击不被屏幕键盘打断），离开时恢复。关断/恢复取 oldIndex
       在 setCurrentIndex 之前执行（XStackedLayout_currentIndex 读旧值）。
       单例惰性创建，virtualKeyboard() 判空（裁剪配置=0 单例返回 NULL 自然
       空操作）。
       [2026-10-25 用户反馈「RC 页点 IP/端口 输入框不弹屏幕键盘」]：
       页级全关把**输入框**也一起关了（autoPopup=false=应用全权接管，按下
       路径与守护一并停），但该页的 IP/端口/口令正是要键盘的输入位。改用
       框架的「仅编辑控件」降级模式：autoPopup 仍关（按钮/列表/空白点按
       不弹键盘，2026-10-03 的诉求保住），但**受支持编辑控件**落点仍弹
       面板（编辑控件判据与 accept 全等）。 */
    {
        int oldIndex = XStackedLayout_currentIndex(&self->m_stackLayout);
        if (oldIndex != 11 && index == 11) {
            XVirtualKeyboard* kb = XGuiApplication_virtualKeyboard();
            if (kb) {
                XVirtualKeyboard_setAutoPopupEditableOnly(kb, true);
                XVirtualKeyboard_setAutoPopup(kb, false);
            }
        } else if (oldIndex == 11 && index != 11) {
            XVirtualKeyboard* kb = XGuiApplication_virtualKeyboard();
            if (kb) {
                XVirtualKeyboard_setAutoPopupEditableOnly(kb, false);
                XVirtualKeyboard_setAutoPopup(kb, true);
            }
        }
    }
#endif
    if (index < 0) index = 0;
    if (index > 23) index = 23; /* 2026-10-29 页 22=测试菜单、页 23=Shell 入列。 */
#if XWIDGET_ON && XABSTRACTBUTTON_ON && XPUSHBUTTON_ON && XFRAME_ON && \
    XLABEL_ON && XLAYOUT_ON && XLAYOUT_STACKED_ON
    /* 导航面板手风琴随页展开所在分类（程序化切页 --page/autotest 与
       点击切页同口径；面板重排先于内容区避让重算）。 */
    if (self->m_navCategory != demo_navCategoryForPage(index)) {
        self->m_navCategory = demo_navCategoryForPage(index);
        demo_navUpdatePanel(self);
    }
#endif
    /* [懒分配 2026-10-19·工程要点⑤] 切页契约：目标扩展页在 setCurrentIndex
       前确保真实构建（首显构建唯一常规入口；exti=index-5，负值/越界/裁
       剪占位槽由 demo_ext_ensureBuilt 自守卫零操作；已建幂等短路=常驻
       不销毁）。堆叠下标=页索引由注册期占位壳保持，换入真页根后本函数
       既有几何回贴/adapt/整页刷新链路照旧。 */
    demo_ext_ensureBuilt(self, index - 5);
    /* [2026-10-20 退出段 UAF 根修] 远程客户端页（页 11）的统计/基准
       定时器回调会写页内控件；页面切离后其控件可能已失效，而定时器
       仍被事件循环周期派发 → 回调写已释放控件 → 读取访问权限冲突
       （实测：进入本页后切到别的页，约 1s 后必崩）。口径：定时器只
       在本页为当前页时运行——切离即停、切回即启。 */
    {
        int prevIndex = XStackedLayout_currentIndex(&self->m_stackLayout);
        if (prevIndex == 11 && index != 11)
            demo_page_remote_client_pause();
        else if (prevIndex != 11 && index == 11)
            demo_page_remote_client_resume();
        else if (prevIndex != 13 && index == 13)
            demo_page_network_resume(); /* 无线构建期瞬态失败的恢复口 */
    }
    XStackedLayout_setCurrentIndex(&self->m_stackLayout, index);
#if XBUTTONGROUP_ON && XWIDGET_ON && XABSTRACTBUTTON_ON && XPUSHBUTTON_ON
    /* 导航钮选中态随页同步：点击切页（clicked 槽走此处，重设同值幂等）
       与程序化切页（--page / autotest 调度 / 截图门）共用同一口径；
       互斥组自动反选其余成员。 */
    XAbstractButton_setChecked((XAbstractButton*)&self->m_pageNav[index], true);
#endif
#if XWIDGET_ON && XABSTRACTBUTTON_ON && XPUSHBUTTON_ON && XFRAME_ON && \
    XLABEL_ON && XLAYOUT_ON && XLAYOUT_STACKED_ON
    /* 紧凑形态：离开菜单页即收起菜单态（应用栏图标回到 ☰、标题回到
       页名）。切页本身即隐含"用户已从菜单里选了目的地"。 */
    self->m_menuOpen = false;
    demo_appBarTitleUpdate(self);
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

/** @brief 把内容控件包进页容器（页容器挂填充布局随 tab 页铺满，
 *         内容件由布局接管几何——对标 Qt 页容器+布局分层语义；
 *         [布局容器化 2026-10-17] 原「包装页无布局+内容件手写
 *         (10,10,...) 固定几何」会使内容件停留在包装页被
 *         XTabWidget 摆位前的旧坐标（目验：滚动区盖住页签条）。
 *         layout 由调用方传入成员（一控件一布局），NULL 时不挂。
 *         返回页容器。 */
static XWidget* demo_wrapTabPage(DemoWin* self, XWidget* content,
                                 XBoxLayout* layout)
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
    if (layout) {
        XBoxLayout_init(layout, XBoxLayoutDirection_TopToBottom);
        XBoxLayout_addWidget(layout, content);
        XWidget_setLayout(page, (XLayout*)layout);
    }
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

/** @brief 页面 15（二维码演示）导航槽（2026-10-08 控件类入列）。 */
static void demo_nav15Slot(XObject* receiver, XVarList* args)
{
    (void)args;
    demo_switchPage((DemoWin*)receiver, 15);
}

/** @brief 页面 16（软件更新）导航槽（2026-10-07 系统设置入列；2026-10-11 双页并入移至页 16）。 */
static void demo_nav16Slot(XObject* receiver, XVarList* args)
{
    (void)args;
    demo_switchPage((DemoWin*)receiver, 16);
}

static void demo_nav17Slot(XObject* receiver, XVarList* args)
{
    (void)args;
    demo_switchPage((DemoWin*)receiver, 17);
}

static void demo_nav18Slot(XObject* receiver, XVarList* args)
{
    (void)args;
    demo_switchPage((DemoWin*)receiver, 18);
}

/** @brief 页面 19（窗口设置）导航槽（2026-10-17 系统设置入列）。 */
static void demo_nav19Slot(XObject* receiver, XVarList* args)
{
    (void)args;
    demo_switchPage((DemoWin*)receiver, 19);
}

/** @brief 页面 20（时钟与 DDNS）导航槽（2026-10-16 系统设置入列）。 */
static void demo_nav20Slot(XObject* receiver, XVarList* args)
{
    (void)args;
    demo_switchPage((DemoWin*)receiver, 20);
}

/** @brief 页面 21（ATV61 变频器）导航槽（2026-10-26 远程类入列）。 */
static void demo_nav21Slot(XObject* receiver, XVarList* args)
{
    (void)args;
    demo_switchPage((DemoWin*)receiver, 21);
}

/** @brief 页面 22（测试菜单）导航槽（2026-10-29 远程类入列）。 */
static void demo_nav22Slot(XObject* receiver, XVarList* args)
{
    (void)args;
    demo_switchPage((DemoWin*)receiver, 22);
}

/** @brief 页面 23（Shell 控制台）导航槽（2026-10-29 远程类入列）。 */
static void demo_nav23Slot(XObject* receiver, XVarList* args)
{
    (void)args;
    demo_switchPage((DemoWin*)receiver, 23);
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
        top = demo_sysbarH(win) + DEMO_BRAND_H + 8;
        bottomLimit = XWidget_height(&win->m_base) -
                      (win->m_statusBarVisible ? DEMO_STATUS_H : 0) - 8;
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

/* ==================== 窗口设置页访问器（xgui_demo_pages.h 契约） ==== */

void* demo_main_toplevel(void* user)
{
    DemoWin* win = (DemoWin*)user;
    if (!win) return NULL;
    return (void*)&win->m_base;
}

bool demo_main_setStatusBarVisible(void* user, bool visible)
{
    DemoWin* win = (DemoWin*)user;
    if (!win || win->m_statusBarVisible == visible) return visible;
    win->m_statusBarVisible = visible;
    /* 布局化 chrome：显隐即布局隐藏条目，空间自动收放。 */
    XWidget_setVisible((XWidget*)&win->m_statusLabel, visible);
    demo_main_refreshChrome(user);
    return visible;
}

bool demo_main_statusBarVisible(void* user)
{
    DemoWin* win = (DemoWin*)user;
    if (!win) return false;
    return win->m_statusBarVisible;
}

void demo_main_sceneInvalidate(void* user)
{
    DemoWin* win = (DemoWin*)user;
    if (!win) return;
    win->m_staticSceneDirty = true;
    demo_repaint(win);
}

void demo_main_refreshChrome(void* user)
{
    DemoWin* win = (DemoWin*)user;
    if (!win) return;
#if XWIDGET_ON && XFRAME_ON && XLABEL_ON && XLAYOUT_ON && XLAYOUT_ON
    /* CSD 保留边距=根布局 contentsMargins.top（单一出处）：边框开关
     * 收放边距后更新之，品牌条/内容区/状态栏三段由布局自动重排。
     * [下移取消 2026-10-25] 安卓沉浸式后无状态栏让位，只余 CSD。 */
    XLayout_setContentsMargins(&win->m_rootLayout,
                               0, demo_sysbarH(win), 0, 0);
    XLayout_activate((XLayout*)&win->m_rootLayout);
#endif
#if XGUI_PERFORMANCE_OVERLAY_ON && XWIDGET_ON && XFRAME_ON && XLABEL_ON
    /* 右下自动悬浮层锚定随内容区段收放（用户预设定位时不抢位）。 */
    demo_performance_anchorBottomRight(win);
#endif
#if XWIDGET_ON && XABSTRACTBUTTON_ON && XPUSHBUTTON_ON && XFRAME_ON && \
    XLABEL_ON && XLAYOUT_ON && XLAYOUT_STACKED_ON
    /* 导航面板/页内排布随内容区段重排（面板 py 相对 contentHost=0）。 */
    demo_navUpdatePanel(win);
    demo_layout_content(win);
#endif
}

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
#if XWIDGET_ON && XLAYOUT_ON
    /* [跟随延迟兜底 2026-10-18] 显式重解算后再做 demo 侧排布：RESIZE 派
       发前平台路径已激活根布局（XWidget_applyWindowGeometry 平台路径
       布局激活，与 recomputeGeometry 对称），此处 updateGeometry 使
       demo_navUpdatePanel/demo_layout_content 读到的 contentHost 几何
       恒为本帧激活值，不再依赖帧泵下一轮兜底（晚一拍）。沿父链
       invalidate+activate 幂等（XLayout.c:1140-1144），与平台激活双调
       用无害；失效先于激活顺序由 updateGeometry 内部保证。 */
    XWidget_updateGeometry(self);
#endif
#if XWIDGET_ON && XABSTRACTBUTTON_ON && XPUSHBUTTON_ON && XFRAME_ON && \
    XLABEL_ON && XLAYOUT_ON && XLAYOUT_STACKED_ON
    /* [移动端形态 2026-10-25] 窗宽跨过 compact 断点（600dp）时切骨架：
       手机竖屏（宽 400dp）→ 应用栏 + 全屏菜单页；横屏/桌面（宽 ≥600）
       → 品牌条 + 侧栏导航。切换须先于面板重排（形态决定面板语义）。 */
    demo_layoutModeUpdate(demo);
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

/** @brief ChangeEvent：窗口状态（最小化/最大化/全屏）变化时的 demo 侧重排。
 * @details 对标 Qt QWidget::changeEvent(QEvent::WindowStateChange)。
 *          【职责边界 2026-10-25 真机纠正】全屏只去掉**窗口装饰**——CSD 标题
 *          条由框架装饰层在 notifyWindowState 里隐藏并把保留边距归零，本槽
 *          不碰它。应用自己的 chrome（蓝底品牌条 DEMO_BRAND_H、底部状态栏
 *          DEMO_STATUS_H）是全屏内容的一部分，必须照常显示：上一版在此把两
 *          条一并隐藏是错的（用户指正「蓝色状态栏和底部黑色状态栏都没了」）。
 *          本槽只做状态无关的重排兜底：状态翻转会改变装饰保留边距（内容区
 *          起点），布局与内容页需按新边距重解算一次。 */
static void VDemoWin_changeEvent(XWidget* self, XEvent* event)
{
    XClass_Parent(XWidget, EXWidget_ChangeEvent,
                  void(*)(XWidget*, XEvent*))(self, event);
    if (XEvent_type(event) != XEVENT_TYPE_WINDOW_STATE_CHANGE) return;
    {
        DemoWin* demo = (DemoWin*)self;
        demo->m_staticSceneDirty = true;
    }
    /* 【关键】根布局 contentsMargins.top 是装饰保留边距的**快照**（见
     * demo_main_refreshChrome 注释）：框架全屏时把保留边距归零，但 demo
     * 根布局不会自动跟随——必须经 refreshChrome 把新值重取进布局，否则
     * 内容区顶仍停在 28px CSD 条高（真机实证：品牌条从 y=28 起，上方
     * 整幅 28px 灰带；桌面窗口尺寸变化恰好触发 RESIZE→refreshChrome 才
     * 侥幸看不出）。refreshChrome 内含根布局激活 + 悬浮层重锚 + 导航面板
     * 与页内排布，一次覆盖全部 chrome 重排。 */
    demo_main_refreshChrome(self);
#if XWIDGET_ON && XLAYOUT_ON
    XWidget_updateGeometry(self);
#endif
    /* 【窗口设置页状态回读推入 2026-10-28】窗口状态全路径变化（软件
     * 请求/平台回写经 XWidgetWindow 桥转发）都以本事件收口——窗口
     * 设置页据此自动修正「全屏显示」等复选框（Qt：widget 的
     * changeEvent 驱动 UI 回读；安卓沉浸式/fbdev 自启全屏的启动期
     * 推入晚于页面 build，无此推入则勾选框永久漏勾）。 */
    demo_page_window_settings_syncFromWindow();
    /* 整窗重绘：装饰边距翻转会改变内容区起点，原区域需要重新上漆。 */
    {
        XRect full;
        XRect_init(&full, 0, 0, XWidget_width(self), XWidget_height(self));
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

/* 诊断时间戳基准（2026-10-11 启动卡顿分段定位）：main 入口置位，此后
 * 每行 demo_log 前缀 [秒.毫秒]=距进程入口耗时，慢段在行间直接现形。 */
static int64_t s_logBaseMs;
static bool s_logBaseInit;
static void demo_log_markBase(void)
{
    s_logBaseMs = XDateTime_currentMSecsSinceEpoch();
    s_logBaseInit = true;
}

/** @brief 带时间戳的启动里程碑打点（[秒.毫秒] 同基准；独立于 demo_log
 *  ——XPrintf 直呼的既有阶段行不经 demo_log，卡顿分段须在 main 各阶段
 *  主动打点，慢段在相邻行差值中直接现形）。 */
static void demo_diag(const char* fmt, ...)
{
    va_list args;
    int64_t now;
    if (!s_logBaseInit)
        return;
    now = XDateTime_currentMSecsSinceEpoch();
    printf("[%d.%03d] ", (int)((now - s_logBaseMs) / 1000),
           (int)((now - s_logBaseMs) % 1000));
    va_start(args, fmt);
    vprintf(fmt, args);
    va_end(args);
    fflush(stdout);
}

/** @brief KeyPressEvent：打印键码/修饰键/自动重复（真实输入闭环验证）。 */
static void VDemoWin_keyPressEvent(XWidget* self, XEvent* event)
{
    XKeyEvent* key = (XKeyEvent*)event;
    if (!key) return;
    /* [移动端返回键 2026-10-25] 安卓「返回」被平台层映射为 XKey_Escape
       （XPlatformWindowAndroid.c 的 AKEYCODE_BACK 映射）：紧凑形态下
       Esc = 关闭菜单页（与 ☰ 图标同语义），符合手机返回键直觉。 */
#if XWIDGET_ON && XABSTRACTBUTTON_ON && XPUSHBUTTON_ON && XFRAME_ON && \
    XLABEL_ON && XLAYOUT_ON && XLAYOUT_STACKED_ON
    if (XKeyEvent_key(key) == XKey_Escape) {
        DemoWin* demo = (DemoWin*)self;
        if (demo->m_layoutMode == 1 &&
            XStackedLayout_currentIndex(&demo->m_stackLayout) ==
                demo->m_menuPageIdx) {
            demo_menuToggle(demo);
            XEvent_accept(event);
            return;
        }
    }
#endif
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
    /* [命中探针 2026-10-19] XGUI_DEMO_HITPROBE=1：根收到按下时打印
       childAt 结果与扩展页槽位/占位壳矩形——XTEST 黑盒点击不可达的
       现场归因（评审目验失败项①），常态零开销。 */
    if (XSystem_environment("XGUI_DEMO_HITPROBE")) {
        XPoint probe;
        XWidget* hit;
        int exti;
        XPoint_init(&probe, XMouseEvent_position(mouse).x,
                    XMouseEvent_position(mouse).y);
        hit = XWidget_childAt(&demo->m_base, &probe);
        demo_log("HITPROBE childAt=%p isWidget=%d\n", (void*)hit,
                 hit ? (int)XObject_isWidgetType((XObject*)hit) : -1);
        { /* 命中链回溯：逐层打印 rect/visible，定位错位层。 */
            XObject* it = (XObject*)hit;
            int depth = 0;
            while (it && it->m_is_widget && depth < 8) {
                XWidget* w = (XWidget*)it;
                demo_log("HITPROBE chain[%d] %p rect=(%d,%d %dx%d) "
                         "hidden=%d vis=%d\n", depth, (void*)w,
                         w->m_windowRect.x, w->m_windowRect.y,
                         w->m_windowRect.width, w->m_windowRect.height,
                         XWidget_testAttribute(w,
                             XWidgetAttribute_WState_Hidden) ? 1 : 0,
                         XWidget_isVisible(w) ? 1 : 0);
                it = XObject_parent(it);
                ++depth;
            }
        }
        for (exti = 0; exti < 19; ++exti) {
            XWidget* page = demo->m_extPages[exti];
            if (page)
                demo_log("HITPROBE ext[%d] rect=(%d,%d %dx%d) vis=%d built=%d\n",
                         exti, page->m_windowRect.x, page->m_windowRect.y,
                         page->m_windowRect.width, page->m_windowRect.height,
                         XWidget_isVisible(page) ? 1 : 0,
                         demo->m_extBuilt[exti] ? 1 : 0);
        }
        demo_log("HITPROBE navPanel rect=(%d,%d %dx%d) vis=%d\n",
                 demo->m_navPanel.m_windowRect.x,
                 demo->m_navPanel.m_windowRect.y,
                 demo->m_navPanel.m_windowRect.width,
                 demo->m_navPanel.m_windowRect.height,
                 XWidget_isVisible((XWidget*)&demo->m_navPanel) ? 1 : 0);
    }
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
    /* 【按压整帧重绘已拆除（脏区架构战役 2026-10-15）】原「交互按下即
       整帧重绘」是子件形态对话框布局微移残影的粗放兜底：任意一次按压
       （含触屏点按空白区）都把整窗拖成全窗重绘+提交，真机 RGB565
       1024x600 上=1.2MB/帧 memcpy+cacheflush，触摸阵发 sys 100% 的
       直接元凶。残影根因框架已收口——XWidget_recomputeGeometry 对
       move/resize 的旧+新矩形双失效（生效可见门控），show/hide 走
       setVisible 双向 update；按压局部脏区已足够消除双影（消息框/
       进度条覆盖层 Xvfb 目验回归）。 */
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
                if (XPerformanceOverlay_beginDrag(
                        &demo->m_performanceOverlay, position.x, position.y)) {
                    /* 拖动期抓取（对标 QWidget::grabMouse，XSlider/XDial
                     * 同款根修）：触摸合成事件不参与 buttonDown 隐式抓取，
                     * DragBegin 认领后的 MOVE 按命中派发，手指瞬时越出
                     * 悬浮层矩形（低帧率必现）即断链脱手；抓取后
                     * MOVE/RELEASE 直投本窗口，拖移与真实鼠标同轨。 */
                    XWidget_grabMouse(self);
                }
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
    if (XMouseEvent_button(mouse) == XMouseButton_LeftButton) {
        if (XPerformanceOverlay_isDragging(&demo->m_performanceOverlay)) {
            XPerformanceOverlay_endDrag(&demo->m_performanceOverlay);
            demo_repaint(demo);
            XEvent_accept(event);
        }
        /* 抓取配对解除（对齐按下侧 beginDrag 即抓）：拖动若被长按远点
         * 收口/movable 翻转等旁路提前终结，仍须摘抓取防事件滞留本窗。 */
        if (XWidget_mouseGrabber() == self) XWidget_releaseMouse(self);
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
            /* 按键中途丢失收口：同步摘抓取（拖动态与抓取态一起归零）。 */
            XPerformanceOverlay_endDrag(&demo->m_performanceOverlay);
            if (XWidget_mouseGrabber() == self) XWidget_releaseMouse(self);
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
    XVTABLE_OVERLOAD_DEFAULT(EXWidget_ChangeEvent, VDemoWin_changeEvent);
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
    self->m_clockTimer = XTIMER_INVALID_ID;
    self->m_clockLabel = NULL;
    self->m_clockLabelApp = NULL;
    self->m_overlayTimer = XTIMER_INVALID_ID;
#if XWIDGET_ON && XFRAME_ON && XLABEL_ON
    /* 底部状态栏文本（深灰背景由静态场景绘制，白字覆盖其上）。
       【提前到扩展页 build 之前（2026-10-11 XError 根修）】：扩展页
       build（kExtBuilders 循环，QR 页尾部 qr_generate→qr_set_status→
       demo_set_status）会对 m_statusLabel 调 XLabel_setText_2——此前
       init 在数千行之后，写入的是 memset 全零的未初始化对象，
       label_clearContents 里 XClassDeinit(&m_pixmap) 读 vtable NULL
       触发 XClass_deinit_base 参数告警（每次启动一行，2026-10-08
       二维码页引入）。几何/缩进/对齐字段与原处一致；稍后的可见性
       调整与 show 不受提前影响（show 原处保留）。 */
    DemoStatusLabel_init(&self->m_statusLabel, &self->m_base, 0);
    self->m_statusBarVisible = true; /* 窗口设置页开关的初值（默认显示）。 */
    self->m_lastContentW = -1; /* 帧泵兜底门：强制首轮页内排布。 */
    self->m_lastContentH = -1;
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
    XWidget_setFixedHeight((XWidget*)&self->m_statusLabel, DEMO_STATUS_H);
#endif
#if XGUI_PERFORMANCE_OVERLAY_ON && XWIDGET_ON && XFRAME_ON && XLABEL_ON
    demo_performance_init(self);
#endif
#if XWIDGET_ON && XFRAME_ON && XLABEL_ON && XLAYOUT_ON && XLAYOUT_ON
    /* ---- 布局化 chrome（2026-10-17）：根垂直布局三段----
     * [品牌条 40 固定][内容区伸展][状态栏 26 固定]；CSD 保留边距=
     * contentsMargins.top（边框开关只更新这一处）。Bold：合成粗体
     * 样张——标题是界面里最合适加粗的层级。 */
    DemoBrandBar_init(&self->m_brandBar, &self->m_base, 0);
    self->m_brandBar.m_owner = self;
    demo_set_widget_default_font((XWidget*)&self->m_brandBar);
    {
        XFont titleFont = XWidget_font((XWidget*)&self->m_brandBar);
        XFont_setBold(&titleFont, true);
        XWidget_setFont((XWidget*)&self->m_brandBar, &titleFont);
        XClassDeinit(&titleFont);
    }
    XLabel_setText_2((XLabel*)&self->m_brandBar, "XGui 控件演示");
    XLabel_setTextPixelSize((XLabel*)&self->m_brandBar, 18);
    XLabel_setAlignment((XLabel*)&self->m_brandBar,
                        XAlignment_Left | XAlignment_VCenter);
    XLabel_setIndent((XLabel*)&self->m_brandBar, 56); /* 让开左侧 ☰ 图标区
                                                       （与紧凑形态应用栏同口径）。 */
    XWidget_setForegroundRole((XWidget*)&self->m_brandBar,
                              XPaletteColorRole_HighlightedText);
    XWidget_setFixedHeight((XWidget*)&self->m_brandBar, DEMO_BRAND_H);
    XWidget_show((XWidget*)&self->m_brandBar);

    /* ---- 移动端应用栏（紧凑形态顶栏；宽屏形态隐藏）----
     * 与品牌条同色（深蓝）保证两形态视觉连贯；标题随当前页更新，
     * 左侧 48dp 触控区是导航图标（☰/←），点击切菜单页。 */
    DemoAppBar_init(&self->m_appBar, &self->m_base, 0);
    self->m_appBar.m_owner = self;
    demo_set_widget_default_font((XWidget*)&self->m_appBar);
    {
        XFont titleFont = XWidget_font((XWidget*)&self->m_appBar);
        XFont_setBold(&titleFont, true);
        XWidget_setFont((XWidget*)&self->m_appBar, &titleFont);
        XClassDeinit(&titleFont);
    }
    XLabel_setText_2((XLabel*)&self->m_appBar,
                     "XGui \xE6\x8E\xA7\xE4\xBB\xB6\xE6\xBC\x94\xE7\xA4\xBA");
    XLabel_setTextPixelSize((XLabel*)&self->m_appBar, 18);
    XLabel_setAlignment((XLabel*)&self->m_appBar,
                        XAlignment_Left | XAlignment_VCenter);
    XLabel_setIndent((XLabel*)&self->m_appBar, 56); /* 让开左侧导航图标区。 */
    XWidget_setForegroundRole((XWidget*)&self->m_appBar,
                              XPaletteColorRole_HighlightedText);
    XWidget_setFixedHeight((XWidget*)&self->m_appBar, DEMO_APPBAR_H);
    XWidget_hide((XWidget*)&self->m_appBar);

    XBoxLayout_init(&self->m_rootLayout, XBoxLayoutDirection_TopToBottom);
    XWidget_init(&self->m_midHost, &self->m_base, 0);
    /* [Qt 对齐 2026-10-18] 中段宿主=根布局伸展段，缺省 sizePolicy 改双向
     * Expanding：XSplitter 引入 sizeHint/minimumSizeHint 后 midHost 的
     * 布局 hint 首次有了有效值（分割条页 hint 合计），缺省 Preferred 使
     * 根布局按 hint 落尺寸、余量整段留白——放大/最大化后中段（导航+
     * 内容）停在旧尺寸不跟随（10-18 真机实证；品牌/状态横平移不受影响）。 */
    XWidget_setSizePolicy(&self->m_midHost,
                          XWidgetSizePolicy_Expanding,
                          XWidgetSizePolicy_Expanding);
    XWidget_init(&self->m_contentHost, &self->m_midHost, 0);
    /* [XSplitter 迁移 2026-10-17] 中段=框架分割条：midHost 单项容器布
     * 局托管分割条；面板/内容区在导航装配段作为分割条子页挂入。 */
    XBoxLayout_init(&self->m_midHostLayout, XBoxLayoutDirection_TopToBottom);
#if XSPLITTER_ON
    XSplitter_init(&self->m_navSplitter, &self->m_midHost, 0);
    XSplitter_setHandleWidth(&self->m_navSplitter, 8);
    XWidget_setAttribute((XWidget*)&self->m_navSplitter,
                         XWidgetAttribute_HitEdgeBand, true);
    XWidget_show((XWidget*)&self->m_navSplitter);
    XBoxLayout_addWidget(&self->m_midHostLayout,
                         (XWidget*)&self->m_navSplitter);
    XBoxLayout_setStretch(&self->m_midHostLayout, 0, 1);
#endif
    XBoxLayout_addWidget(&self->m_rootLayout, (XWidget*)&self->m_brandBar);
    XBoxLayout_addWidget(&self->m_rootLayout, (XWidget*)&self->m_appBar);
    XBoxLayout_addWidget(&self->m_rootLayout, &self->m_midHost);
    XBoxLayout_addWidget(&self->m_rootLayout,
                         (XWidget*)&self->m_statusLabel);
    XBoxLayout_setStretch(&self->m_rootLayout, 1, 1);
    XLayout_setContentsMargins((XLayout*)&self->m_rootLayout,
                               0, demo_sysbarH(self), 0, 0);
    XWidget_setLayout(&self->m_base, (XLayout*)&self->m_rootLayout);
    XWidget_setLayout(&self->m_midHost, (XLayout*)&self->m_midHostLayout);
#endif
#if XWIDGET_ON && XLAYOUT_ON && XLAYOUT_STACKED_ON
    /* 主内容堆叠：三个演示页面容器（可见性由 XStackedLayout 管理）。 */
    XStackedLayout_init(&self->m_stackLayout);
    /* [布局化 chrome 三段 2026-10-17] 堆叠托管到 contentHost（上次
     * 尝试回退的「不消费避让边距」已由框架侧 VXStackedLayout_setGeometry
     * 消费 contentsMargins 根修）：下方 addWidget 经 XLayout_linkItem
     * 自动把页面重挂为 contentHost 子，矩形由宿主以自身坐标 (0,0,w,h)
     * 激活分配——原手动 setGeometry 按窗口坐标解释（页面压盖品牌条，
     * DDE 探针实测错位 y=68=CSD 28+品牌 40）连同避让双重计入一并消除。
     * 呼吸边 12/8/12/0（上 8px 呼吸，底=0 因 contentHost 底已贴状态栏）。 */
    XWidget_setLayout(&self->m_contentHost, (XLayout*)&self->m_stackLayout);
    XLayout_setContentsMargins((XLayout*)&self->m_stackLayout, 12, 8, 12, 0);
    XWidget_init(&self->m_pageButtons, &self->m_contentHost, 0);
    XWidget_init(&self->m_pageChoices, &self->m_contentHost, 0);
    XWidget_init(&self->m_pageStacked, &self->m_contentHost, 0);
    XWidget_init(&self->m_pageInputs, &self->m_contentHost, 0);
    /* [页级滚动壳 2026-10-29] 内建页堆叠条目=滚动壳包成员页根（成员
     * 身份不变，槽/adapt 按成员消费；内容超高才出竖条，桌面满屏不变）。
     * 包裹壳登记 m_pageScrolls[页索引]（adapt 漏斗收尾刷新用）。 */
    XStackedLayout_addWidget(&self->m_stackLayout,
                             demo_win_wrapBuiltinPage(
                                 self, (XWidget*)&self->m_pageButtons, 0));
    XStackedLayout_addWidget(&self->m_stackLayout,
                             demo_win_wrapBuiltinPage(
                                 self, (XWidget*)&self->m_pageChoices, 1));
    XStackedLayout_addWidget(&self->m_stackLayout,
                             demo_win_wrapBuiltinPage(
                                 self, (XWidget*)&self->m_pageStacked, 2));
    XStackedLayout_addWidget(&self->m_stackLayout,
                             demo_win_wrapBuiltinPage(
                                 self, (XWidget*)&self->m_pageInputs, 3));
    XWidget_init(&self->m_pageTabs, &self->m_contentHost, 0);
    XStackedLayout_addWidget(&self->m_stackLayout,
                             demo_win_wrapBuiltinPage(
                                 self, (XWidget*)&self->m_pageTabs, 4));
    /* ---- 页面 5~16：扩展页注册（xgui_demo_pages.h 契约）。[懒分配
     * 2026-10-19] 改惰性：注册期只建空占位壳入栈（真页根推迟到首显，
     * 由 demo_ext_ensureBuilt 同位换入，见文件级 g_extBuilders 注）。
     * 下标=页索引-5，下标 7=页 12（图表演示，内置页）恒 NULL 占位。 ---- */
    {
        int exti;
        for (exti = 0; exti < 19; ++exti) {
#if XCHARTS_ON
            if (exti == 7) {
                /* 页 12（图表演示）内置页在此占位入栈：保证「堆叠下标=
                   页索引」连续——后随扩展页（网络设置/悬浮窗设置）恰落
                   堆叠序 13/14；曾把图表页挪到扩展页之后入栈，两新页
                   顶掉了它的序号（--page 13/14 截图错页实证）。 */
                XWidget_init(&self->m_pageChart, &self->m_contentHost, 0);
                XStackedLayout_addWidget(&self->m_stackLayout,
                                         demo_win_wrapBuiltinPage(
                                             self, &self->m_pageChart, 12));
                continue;
            }
#endif
            if (!g_extBuilders[exti])
                continue;
            /* [懒分配 2026-10-19·工程要点④] 占位壳必须写入 m_extPages
             * 槽位本身，非仅 XStackedLayout.addWidget：main 尾部窗口几
             * 何分支查 m_extPages[0..11] 任一非空才取 1000x700，槽位空
             * 会落 520x360——无头一致性采集（XGUI_DEMO_WINDOW_SIZE）与
             * 量化对照的后备存储 RSS 全面漂移（评审实验已以临时 OR 门
             * +PNG 1000x700 解析复核）。占位壳=堆上空 XWidget（零子树，
             * 常驻成本可忽略；懒分配省的是页内容重树）。
             * 注册父必须 m_base（真窗口），勿改挂 contentHost：与真页
             * 根同口径（真根 build 亦以 m_base 为构造父，随后由
             * insertWidget 的 XLayout_linkItem 重挂 contentHost）；部分
             * 页面把 parent 当「主窗顶层」用（远程页 m_topLevel→
             * XGuiServer_host 的 isWindow 门；挂 contentHost 即监听静
             * 默失败=autotest 3 FAIL 回归实证 2026-10-17）。 */
            self->m_extPages[exti] = XWidget_create((XWidget*)&self->m_base, 0);
            if (self->m_extPages[exti])
                XStackedLayout_addWidget(&self->m_stackLayout,
                                         self->m_extPages[exti]);
        }
    }
    /* ---- 移动端菜单页（紧凑形态的导航载体，堆叠末页）----
     * 下标 = 21（页面 0..20 之后）：Material 抽屉在紧凑窗口的"全屏
     * 变体"——导航树平铺在内容区，应用栏 ☰ 打开、选中条目即切页并
     * 关闭。导航条目控件实体唯一（从桌面面板搬入本页），选中态互斥
     * 组与页切换逻辑不分叉。 */
    XWidget_init(&self->m_mobileMenu, &self->m_contentHost, 0);
    /* [页级滚动壳 2026-10-29 豁免] 菜单页不入壳：页内容即导航滚动容器
     * （m_navScroll 铺满整页，见 demo_compactMenuLayout），再包一层成
     * 嵌套滚动；其触摸滑动由框架 XAbstractScrollArea 通道直接生效。 */
    XStackedLayout_addWidget(&self->m_stackLayout, &self->m_mobileMenu);
    /* 菜单页下标实测写入（页数随特性裁剪而变，写死常量会指错页）。 */
    self->m_menuPageIdx =
        XStackedLayout_indexOf(&self->m_stackLayout, &self->m_mobileMenu);
#endif
#if XWIDGET_ON && XABSTRACTBUTTON_ON && XPUSHBUTTON_ON && XFRAME_ON && \
    XLABEL_ON && XLAYOUT_ON && XLAYOUT_STACKED_ON
    /* 浮动导航面板装配（「先切一级分类、再选页面」，见文件域
     * 「浮动导航面板」段）：面板/标题/换边/收起 + 4 一级分类钮
     * （控件/系统设置/远程，互斥组 idClicked 切手风琴）+ 5 子分组
     * 标题标签池（竖版展开时按活动分类轮转显隐）+ 16 页面钮（挂活动
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
#if XSCROLLAREA_ON && XABSTRACTSCROLLAREA_ON
    /* 竖版导航内容滚动容器（2026-10-11）：标题行以下整体装进
       XScrollArea，垂直条 AsNeeded——分类展开导致内容超高（矮窗/
       拖窄）时自动出现，全部可见时隐藏。横版（上下停靠）不参与
       滚动：条带已有换行+面板抬高逻辑，滚动容器届时隐藏、子控件
       回挂面板。content 容器无背景无边框，仅作几何锚定父。 */
    XScrollArea_init(&self->m_navScroll, &self->m_navPanel, 0);
    XAbstractScrollArea_setVerticalScrollBarPolicy(
        (XAbstractScrollArea*)&self->m_navScroll,
        (int)XScrollBarPolicy_AsNeeded);
    XAbstractScrollArea_setHorizontalScrollBarPolicy(
        (XAbstractScrollArea*)&self->m_navScroll,
        (int)XScrollBarPolicy_AlwaysOff);
    self->m_navContent = XWidget_create((XWidget*)&self->m_navScroll, 0);
    XScrollArea_setWidget(&self->m_navScroll, self->m_navContent);
    XScrollArea_setWidgetResizable(&self->m_navScroll, false);
    XFrame_setFrameStyle((XFrame*)&self->m_navScroll,
                         (int)(XFrameShape_NoFrame | XFrameShadow_Plain));
#endif
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
    /* [删「收起」2026-10-25] 收起/展开改由顶部品牌条左侧 ☰ 承担
     * （横屏/桌面形态），导航面板上不再有收起钮。 */
    {
        static void (*const kNavSlots[24])(XObject*, XVarList*) = {
            demo_nav0Slot, demo_nav1Slot, demo_nav2Slot, demo_nav3Slot,
            demo_nav4Slot, demo_nav5Slot, demo_nav6Slot, demo_nav7Slot,
            demo_nav8Slot, demo_nav9Slot, demo_nav10Slot, demo_nav11Slot,
            demo_nav12Slot, /* 2026-10-03 图表演示入列。 */
            demo_nav13Slot, /* 2026-10-06 网络设置入列。 */
            demo_nav14Slot, /* 2026-10-06 悬浮窗设置入列。 */
            demo_nav15Slot, /* 2026-10-08 二维码演示入列。 */
            demo_nav16Slot, /* 2026-10-07 软件更新入列（2026-10-11 双页并入移至页 16）。 */
            demo_nav17Slot, /* 2026-10-13 Modbus 主站入列。 */
            demo_nav18Slot, /* 2026-10-13 Modbus 从站入列。 */
            demo_nav19Slot, /* 2026-10-17 窗口设置入列。 */
            demo_nav20Slot,  /* 2026-10-16 时钟与 DDNS 入列。 */
            demo_nav21Slot,  /* 2026-10-26 ATV61 变频器入列。 */
            demo_nav22Slot,  /* 2026-10-29 测试菜单入列。 */
            demo_nav23Slot   /* 2026-10-29 Shell 控制台入列。 */
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
    /* 移动端形态状态（2026-10-25）：初值=宽屏形态，紧凑形态由窗口
     * 宽度（< 600dp）触发切换，见 demo_layoutModeUpdate。 */
    self->m_layoutMode = 0;
    self->m_menuOpen = false;
    self->m_pageBeforeMenu = 0;
    self->m_navCollapsedUser = false;
#if XGUI_PERFORMANCE_OVERLAY_ON && XWIDGET_ON && XFRAME_ON && XLABEL_ON
    /* [编译健全性 2026-10-18] 成员声明在 overlay 宏域内（DemoWin :462），
       使用处须同宏域护栏——此前 overlay=0 裁剪构建编不过
       （m_overlayPinned 未声明），阻塞嵌入式裁剪与无 HUD 回归验证。 */
    self->m_overlayPinned = false; /* 悬浮层默认自动锚右下（用户预设后挂起）。 */
#endif
#if XSPLITTER_ON
    /* [XSplitter 迁移 2026-10-17] 面板+内容挂入分割条（默认顺序=左停靠
     * [面板|内容]；换侧顺序由 navUpdatePanel 经 insertWidget 翻转）；
     * content 侧 stretch=1 兜底吃满余下；手柄点击信号接双击收起/展开
     * 与收起态空闲点击展开（见 demo_navSplitHandleSlot）。 */
    XSplitter_addWidget(&self->m_navSplitter, (XWidget*)&self->m_navPanel);
    XSplitter_addWidget(&self->m_navSplitter, &self->m_contentHost);
    XSplitter_setStretchFactor(&self->m_navSplitter, 1, 1);
    XObject_connect_1((XObject*)&self->m_navSplitter,
                      (size_t)XSplitter_handleClicked_signal(NULL, 0, 0),
                      (XObject*)self, demo_navSplitHandleSlot,
                      XConnectionType_Direct);
    XObject_connect_1((XObject*)&self->m_navSplitter,
                      (size_t)XSplitter_splitterMoved_signal(NULL, 0, 0),
                      (XObject*)self, demo_navSplitMovedSlot,
                      XConnectionType_Direct);
#endif
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
    /* [2026-10-20 启动即报错根修] 状态标签必须先于选项卡初始化：选项卡
     * 插入页签会发 currentChanged 信号，槽 demo_tab_changedSlot 随即写
     * 本标签；标签未初始化时其内嵌 pixmap 虚表为空 → XClassDeinit 走
     * 「虚表为 NULL」报告分支（启动即刷 XError），且写未初始化对象有
     * 内存破坏风险（实测进程随后以 -1 退出）。故标签先建，页签后插。 */
    XLabel_init(&self->m_tabStatus, (XWidget*)&self->m_pageTabs, 0);
    demo_set_widget_default_font((XWidget*)&self->m_tabStatus);
    XLabel_setText_2(&self->m_tabStatus, "\xE5\xB0\xB1\xE7\xBB\xAA"); /* 就绪 */
    XTabWidget_init(&self->m_tabWidget, (XWidget*)&self->m_pageTabs, 0);
    demo_set_widget_default_font((XWidget*)&self->m_tabWidget);
    /* 页签切换联动状态行（demo_tab_changedSlot 此前从未接线，状态行恒「就绪」）。 */
    XObject_connect_1((XObject*)&self->m_tabWidget,
                      (size_t)XTabWidget_currentChanged_signal(&self->m_tabWidget, 0),
                      (XObject*)self, demo_tab_changedSlot,
                      XConnectionType_Direct);
#if XSCROLLAREA_ON && XABSTRACTSCROLLAREA_ON && XFRAME_ON && XLABEL_ON
    /* 页签 0：XScrollArea（包装页挂 m_scrollTabPageLayout 填充布局，
       2026-10-17 撤手写 (10,10,300,150)——原手写使滚动区停留在包装页
       未摆位前的坐标，目验盖住页签条）。内容加高到 30 行保证填充后
       视口仍可滚动（滚动演示语义保持）。 */
    XScrollArea_init(&self->m_scrollArea, (XWidget*)&self->m_tabWidget, 0);
    {
        /* 行距改逐行标签承载：框架 XLabel 的多行行距=字高定尺（无行
           间隙可调），30 行文本相邻行笔画相触（真机 4x 放大可见串行）。
           拆为单行标签+盒距 8，行间留出真实空隙；行高逐标签按当前字
           体实测（heightForWidth 单行值），内容总高随之计算，滚动
           语义与 30 行文案保持不变。 */
        static const char* const kScrollLines[30] = {
            "滚动内容", "第二行", "第三行", "第四行", "第五行",
            "第六行", "第七行", "第八行", "第九行", "第十行",
            "第十一行", "第十二行", "第十三行", "第十四行", "第十五行",
            "第十六行", "第十七行", "第十八行", "第十九行", "第二十行",
            "第二一行", "第二二行", "第二三行", "第二四行", "第二五行",
            "第二六行", "第二七行", "第二八行", "第二九行", "第三十行"
        };
        XWidget* content = XWidget_create((XWidget*)&self->m_scrollArea, 0);
        XBoxLayout* lines =
            XBoxLayout_create(XBoxLayoutDirection_TopToBottom, content);
        int lineH;
        int i;
        XLayout_setContentsMargins((XLayout*)lines, 0, 0, 0, 0);
        XLayout_setSpacing((XLayout*)lines, 8);
        /* SetNoConstraint：容器尺寸由下方显式 resize 定版（滚动区内
           不随布局最小尺寸回写，口径同页 3 分组框）。 */
        XLayout_setSizeConstraint((XLayout*)lines,
                                  XLayoutSizeConstraint_SetNoConstraint);
        lineH = 16; /* 字体查询失败时的兜底行高（位图字库定尺）。 */
        for (i = 0; i < 30; ++i) {
            XLabel* line = XLabel_create(content, 0);
            XLabel_setText_2(line, kScrollLines[i]);
            if (i == 0)
                lineH = XLabel_heightForWidth(line, 260);
            if (lineH < 1) lineH = 16;
            XWidget_setFixedHeight((XWidget*)line, lineH);
            XBoxLayout_addWidget(lines, (XWidget*)line);
            XWidget_show((XWidget*)line);
        }
        XWidget_resize(content, 260, 30 * lineH + 29 * 8);
        XScrollArea_setWidget(&self->m_scrollArea, content);
        XScrollArea_setWidgetResizable(&self->m_scrollArea, false);
        XFrame_setFrameStyle((XFrame*)&self->m_scrollArea,
                             (int)(XFrameShape_NoFrame | XFrameShadow_Plain));
    }
    (void)XTabWidget_insertTab_2(&self->m_tabWidget, 0,
                               demo_wrapTabPage(self, (XWidget*)&self->m_scrollArea,
                                                &self->m_scrollTabPageLayout), "滚动");
#endif
#if XSPLITTER_ON && XFRAME_ON && XLABEL_ON
    /* 页签 1：XSplitter（几何由 XTabWidget 分配）。 */
    XSplitter_init(&self->m_splitter, (XWidget*)&self->m_tabWidget, 0);
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
    /* 页签 2：XToolBox（几何由 XTabWidget 分配）。 */
    XToolBox_init(&self->m_toolBox, (XWidget*)&self->m_tabWidget, 0);
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
            XWidget_show((XWidget*)&self->m_menuBar);
            XToolBar_init(&self->m_toolBar, mbPage, 0);
            XToolBar_addAction_2(&self->m_toolBar, "新建");
            XToolBar_addAction_2(&self->m_toolBar, "保存");
            XWidget_show((XWidget*)&self->m_toolBar);
            /* 框架页布局容器化（2026-10-17）：菜单栏/工具栏/状态栏三段
               纵向布局接管（行高对标改造前手写值 26/34/24，段间距 4）；
               页容器几何由 XTabWidget 统一分配（原 setGeometry 撤销）。 */
            XBoxLayout_init(&self->m_framePageLayout,
                            XBoxLayoutDirection_TopToBottom);
            XBoxLayout_addWidget(&self->m_framePageLayout,
                                 (XWidget*)&self->m_menuBar);
            XWidget_setFixedHeight((XWidget*)&self->m_menuBar, 26);
            XBoxLayout_addWidget(&self->m_framePageLayout,
                                 (XWidget*)&self->m_toolBar);
            XWidget_setFixedHeight((XWidget*)&self->m_toolBar, 34);
#if XSTATUSBAR_ON && XLABEL_ON
            /* 状态栏并入框架页（页底）。 */
            XStatusBar_init(&self->m_sb, mbPage, 0);
            XLabel_init(&self->m_sbLabel, (XWidget*)&self->m_sb, 0);
            XLabel_setText_2(&self->m_sbLabel, "普通区标签");
            XStatusBar_addWidget(&self->m_sb, (XWidget*)&self->m_sbLabel, 1);
            XBoxLayout_addWidget(&self->m_framePageLayout,
                                 (XWidget*)&self->m_sb);
            XWidget_setFixedHeight((XWidget*)&self->m_sb, 24);
            XWidget_show((XWidget*)&self->m_sb);
#endif
            XLayout_setSpacing((XLayout*)&self->m_framePageLayout, 4);
            XBoxLayout_addStretch(&self->m_framePageLayout, 1); /* 余高留底部（三段固定高）。 */
            XWidget_setLayout(mbPage, (XLayout*)&self->m_framePageLayout);
            (void)XTabWidget_insertTab_2(&self->m_tabWidget, 3, mbPage, "框架");
        }
    }
#endif
#if XCALENDARWIDGET_ON
    /* 页签 4：XCalendarWidget（包装页挂 m_calTabPageLayout 填充布局，
       2026-10-17 撤手写 (10,10,280,200)，见页签 0 注）。 */
    XCalendarWidget_init(&self->m_calendar, (XWidget*)&self->m_tabWidget, 0);
    (void)XTabWidget_insertTab_2(&self->m_tabWidget, 4,
                               demo_wrapTabPage(self, (XWidget*)&self->m_calendar,
                                                &self->m_calTabPageLayout), "日历");
#endif
#if XTEXTBROWSER_ON
    /* 页签 5：XTextBrowser（几何由 XTabWidget 分配）。 */
    XTextBrowser_init(&self->m_textBrowser, (XWidget*)&self->m_tabWidget, 0);
    XPlainTextEdit_setPlainText(self->m_textBrowser.m_base.m_editor,
        "帮助内容\n第二段\n第三段");
    (void)XTabWidget_insertTab_2(&self->m_tabWidget, 5,
                               (XWidget*)&self->m_textBrowser, "浏览器");
    /* 直插型页签须显式 show（经 demo_wrapTabPage 的页签已带 show；
       页签内容随当前页显示对标 QTabWidget::insertTab 后页面可见语义）。 */
    XWidget_show((XWidget*)&self->m_textBrowser);
#endif
#if XMDIAREA_ON && XFRAME_ON && XLABEL_ON
    /* 页签 6：XMdiArea（几何由 XTabWidget 分配）。 */
    XMdiArea_init(&self->m_mdiArea, (XWidget*)&self->m_tabWidget, 0);
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
    /* 页签 7：XStackedWidget + XButtonGroup（包装页挂
       m_stackWTabPageLayout 填充布局，2026-10-17 撤容器手写
       (10,10,200,100)，见页签 0 注）。 */
    XStackedWidget_init(&self->m_stackedW, (XWidget*)&self->m_tabWidget, 0);
    XButtonGroup_init(&self->m_btnGroup, NULL);
    XCheckBox_init(&self->m_bgBtn0, (XWidget*)&self->m_stackedW, 0);
    XCheckBox_init(&self->m_bgBtn1, (XWidget*)&self->m_stackedW, 0);
    XAbstractButton_setText_2((XAbstractButton*)&self->m_bgBtn0, "选项 A");
    XAbstractButton_setText_2((XAbstractButton*)&self->m_bgBtn1, "选项 B");
    /* 两勾选框保留手写摆位：XStackedWidget 自挂内部 XStackedLayout，
       外层不能再装盒式布局（一控件一布局）；且勾选框是普通子控件而非
       堆叠页（XStackedWidget_addWidget 会使其变成互斥页签，语义不合），
       无布局承载可迁移（外层容器几何已由布局接管，此处为容器内局部）。 */
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
                               demo_wrapTabPage(self, (XWidget*)&self->m_stackedW,
                                                &self->m_stackWTabPageLayout), "堆叠组");
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
            /* 启动器按钮（各固定 150x28）：点「打开向导」弹向导、点
               「显示错误提示」弹提示条（槽见 demo_wizOpenSlot/
               demo_errShowSlot）。门控与结构体字段/槽函数三处同款，
               裁剪配置下整组一齐缺席。 */
            /* 页签内排布布局容器化（2026-10-17）：钮行横向布局接管
               （边距 8,8,0,0+段间距 10 对标改造前手写 (8,8)/(168,8)）。 */
            XBoxLayout_init(&self->m_wePageLayout,
                            XBoxLayoutDirection_LeftToRight);
            XPushButton_init(&self->m_weOpenWizardBtn, wePage, 0);
            demo_set_widget_default_font((XWidget*)&self->m_weOpenWizardBtn);
            XAbstractButton_setText_2((XAbstractButton*)&self->m_weOpenWizardBtn,
                                      "打开向导");
            XWidget_setFixedWidth((XWidget*)&self->m_weOpenWizardBtn, 150);
            XWidget_setFixedHeight((XWidget*)&self->m_weOpenWizardBtn, 28);
            XBoxLayout_addWidget(&self->m_wePageLayout,
                                 (XWidget*)&self->m_weOpenWizardBtn);
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
            XWidget_setFixedWidth((XWidget*)&self->m_weShowErrBtn, 150);
            XWidget_setFixedHeight((XWidget*)&self->m_weShowErrBtn, 28);
            XBoxLayout_addWidget(&self->m_wePageLayout,
                                 (XWidget*)&self->m_weShowErrBtn);
            XLayout_setContentsMargins((XLayout*)&self->m_wePageLayout,
                                       8, 8, 0, 0);
            XLayout_setSpacing((XLayout*)&self->m_wePageLayout, 10);
            XBoxLayout_addStretch(&self->m_wePageLayout, 1); /* 余宽留行尾。 */
            XWidget_setLayout(wePage, (XLayout*)&self->m_wePageLayout);
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
    /* 大表格（m_tableWidget）：[懒分配 2026-10-19·工程要点②] 原地组装
       段已收编进 exti0（条目视图页）真建后钩子 demo_ext_viewsBigTable
       Hook（demo_ext_ensureBuilt 内调用）——懒分配下注册期槽位是空占
       位壳且原 if 无回落分支，留在此处会让页 5 大表格在懒建后凭空消失
       （views 页 CHILD_ADDED 拦截收编机制不变，摆位/列宽逐式照搬）。 */
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
        /* 钮行布局容器化（2026-10-17）：固定 84x24 对标改造前
           demo_layout_content 均布上限值（bw=clamp(...,48,84)），
           行内排布由 m_chartBtnRow 接管（装配见本函数尾段）。 */
        XWidget_setFixedWidth((XWidget*)&self->m_btnLegend, 84);
        XWidget_setFixedHeight((XWidget*)&self->m_btnLegend, 24);
        XWidget_setFixedWidth((XWidget*)&self->m_btnGrid, 84);
        XWidget_setFixedHeight((XWidget*)&self->m_btnGrid, 24);
        XWidget_setFixedWidth((XWidget*)&self->m_btnTitle, 84);
        XWidget_setFixedHeight((XWidget*)&self->m_btnTitle, 24);
        XWidget_setFixedWidth((XWidget*)&self->m_btnSeries, 84);
        XWidget_setFixedHeight((XWidget*)&self->m_btnSeries, 24);
        XWidget_setFixedWidth((XWidget*)&self->m_btnRange, 84);
        XWidget_setFixedHeight((XWidget*)&self->m_btnRange, 24);
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

    XWidget_show((XWidget*)&self->m_tabWidget);
    XWidget_show((XWidget*)&self->m_inputStatus);
    /* 放到最低层避免挡住 tab 按钮 */
    XWidget_lower((XWidget*)&self->m_inputStatus);
#endif /* 容器与窗口页签节（XTabWidget 门） */
#if XWIDGET_ON && XFRAME_ON && XLABEL_ON
    /* 底部状态栏 init 已提前到 DemoWin_create 头部（扩展页 build 之前，
       2026-10-11 XError 根修）；此处只保留 show 语义（原处）。 */
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
    /* ---- 页 3/4/12 布局容器化装配（2026-10-17）：页内排布由各页根
     * 容器挂盒式布局自管，demo_layout_content 对应手写坐标段删除。
     * 行高/边距/间距均对标改造前手写值；固定尺寸经 setFixedHeight/
     * Width 声明，伸缩语义经 setStretch 表达；嵌套行/列布局器按借用
     * 指针挂父（对象为 DemoWin 成员，见结构体注释）。 ---- */
#if XWIDGET_ON && XGROUPBOX_ON && XLINEEDIT_ON && XSPINBOX_ON && \
    XABSTRACTSLIDER_ON && XSLIDER_ON && XPROGRESSBAR_ON
    /* 页 3 装配：分组框内纵向布局（XGroupBox 无布局感知，VBox+顶边距
       24 近似标题区；行高 26/26/28/24、边距 12,24,12,8、段距 16 对标
       改造前 contentsRect 手写摆位）。 */
    XBoxLayout_init(&self->m_groupBoxLayout, XBoxLayoutDirection_TopToBottom);
    XBoxLayout_addWidget(&self->m_groupBoxLayout, (XWidget*)&self->m_lineEdit);
    XWidget_setFixedHeight((XWidget*)&self->m_lineEdit, 26);
    XBoxLayout_addWidget(&self->m_groupBoxLayout, (XWidget*)&self->m_spinBox);
    XWidget_setFixedHeight((XWidget*)&self->m_spinBox, 26);
    XBoxLayout_addWidget(&self->m_groupBoxLayout, (XWidget*)&self->m_slider);
    XWidget_setFixedHeight((XWidget*)&self->m_slider, 28);
    XBoxLayout_addWidget(&self->m_groupBoxLayout,
                         (XWidget*)&self->m_progressBar);
    XWidget_setFixedHeight((XWidget*)&self->m_progressBar, 24);
    XBoxLayout_addStretch(&self->m_groupBoxLayout, 1); /* 余高留底部（子件固定高防均摊推移）。 */
    XLayout_setContentsMargins((XLayout*)&self->m_groupBoxLayout,
                               12, 24, 12, 8);
    XLayout_setSpacing((XLayout*)&self->m_groupBoxLayout, 16);
    /* SetNoConstraint：阻止 activate 把布局最小尺寸写回宿主（否则覆盖
       下方 setFixed 尺寸，分组框塌回首选宽 240）。 */
    XLayout_setSizeConstraint((XLayout*)&self->m_groupBoxLayout,
                              XLayoutSizeConstraint_SetNoConstraint);
    XWidget_setLayout((XWidget*)&self->m_groupBox,
                      (XLayout*)&self->m_groupBoxLayout);
    /* 分组框定宽定高：对标改造前 colW=clamp(宽/2-16,240,320)、高 210。 */
    XWidget_setFixedWidth((XWidget*)&self->m_groupBox, 288);
    XWidget_setFixedHeight((XWidget*)&self->m_groupBox, 210);
    /* 左列：分组框 + 多行编辑（stretch 填左列余高，段距 8 对标原
       分组框底 218 → 多行编辑顶 226）。 */
    XBoxLayout_init(&self->m_inputsLeftCol, XBoxLayoutDirection_TopToBottom);
    XBoxLayout_addWidget(&self->m_inputsLeftCol, (XWidget*)&self->m_groupBox);
    XLayout_setSpacing((XLayout*)&self->m_inputsLeftCol, 8);
#if XWIDGET_ON && XPLAINTEXTEDIT_ON
    XBoxLayout_addWidget(&self->m_inputsLeftCol, (XWidget*)&self->m_plainEdit);
    XBoxLayout_setStretch(&self->m_inputsLeftCol, 1, 1);
#endif
    /* 右列：下拉/字体行、旋钮联动行、数码管/滚动条行、日期时间族
       （段距 8 对标原 36→44→104→112→176→184 行距）。 */
    XBoxLayout_init(&self->m_inputsRightCol, XBoxLayoutDirection_TopToBottom);
    XLayout_setSpacing((XLayout*)&self->m_inputsRightCol, 8);
#if XWIDGET_ON && XCOMBOBOX_ON && XABSTRACTSLIDER_ON && XDIAL_ON && \
    XPROGRESSBAR_ON && XLCDNUMBER_ON && XSCROLLBAR_ON
    XBoxLayout_init(&self->m_comboRow, XBoxLayoutDirection_LeftToRight);
    XBoxLayout_addWidget(&self->m_comboRow, (XWidget*)&self->m_comboBox);
    XWidget_setFixedWidth((XWidget*)&self->m_comboBox, 150);
    XWidget_setFixedHeight((XWidget*)&self->m_comboBox, 26);
    XBoxLayout_addWidgetEx(&self->m_comboRow, (XWidget*)&self->m_fontCombo,
                           1, 0); /* 字体下拉吃行内余宽。 */
    XWidget_setFixedHeight((XWidget*)&self->m_fontCombo, 28);
    XLayout_setSpacing((XLayout*)&self->m_comboRow, 8);
    XBoxLayout_addLayout(&self->m_inputsRightCol, (XLayout*)&self->m_comboRow);
    XBoxLayout_init(&self->m_dialRow, XBoxLayoutDirection_LeftToRight);
    XBoxLayout_addWidget(&self->m_dialRow, (XWidget*)&self->m_dial);
    XWidget_setFixedWidth((XWidget*)&self->m_dial, 60);
    XWidget_setFixedHeight((XWidget*)&self->m_dial, 60);
    XBoxLayout_addWidgetEx(&self->m_dialRow, (XWidget*)&self->m_dialProgress,
                           1, XAlignment_VCenter); /* 进度条行内垂直居中。 */
    XWidget_setFixedHeight((XWidget*)&self->m_dialProgress, 20);
    XLayout_setSpacing((XLayout*)&self->m_dialRow, 10);
    XBoxLayout_addLayout(&self->m_inputsRightCol, (XLayout*)&self->m_dialRow);
    XBoxLayout_init(&self->m_lcdRow, XBoxLayoutDirection_LeftToRight);
    XBoxLayout_addWidgetEx(&self->m_lcdRow, (XWidget*)&self->m_lcd, 0,
                           XAlignment_Top); /* 顶对齐对标原同行 y=112。 */
    XWidget_setFixedWidth((XWidget*)&self->m_lcd, 160);
    XWidget_setFixedHeight((XWidget*)&self->m_lcd, 60);
    XBoxLayout_addWidget(&self->m_lcdRow, (XWidget*)&self->m_scrollBar);
    XWidget_setFixedWidth((XWidget*)&self->m_scrollBar, 24);
    XWidget_setFixedHeight((XWidget*)&self->m_scrollBar, 64);
    XBoxLayout_addStretch(&self->m_lcdRow, 1); /* 余宽留行尾（两件固定宽）。 */
    XLayout_setSpacing((XLayout*)&self->m_lcdRow, 10);
    XBoxLayout_addLayout(&self->m_inputsRightCol, (XLayout*)&self->m_lcdRow);
#endif
#if XWIDGET_ON && XDATETIMEEDIT_ON && XDATEEDIT_ON && XTIMEEDIT_ON
    /* 日期时间三件套：固定高 28，宽度随右列铺满。 */
    XBoxLayout_addWidget(&self->m_inputsRightCol, (XWidget*)&self->m_dtEdit);
    XWidget_setFixedHeight((XWidget*)&self->m_dtEdit, 28);
    XBoxLayout_addWidget(&self->m_inputsRightCol, (XWidget*)&self->m_dateEdit);
    XWidget_setFixedHeight((XWidget*)&self->m_dateEdit, 28);
    XBoxLayout_addWidget(&self->m_inputsRightCol, (XWidget*)&self->m_timeEdit);
    XWidget_setFixedHeight((XWidget*)&self->m_timeEdit, 28);
#endif
    /* 余高留右列底部（各行固定高，防 qGeomCalc 把余量均摊进行距）。 */
    XBoxLayout_addStretch(&self->m_inputsRightCol, 1);
    /* 顶行：左列固定宽（随分组框 288）+ 右列 stretch 吃余宽（段距 16
       对标原左列右缘 12+colW → 右列起点 rx=12+colW+16）。 */
    XBoxLayout_init(&self->m_inputsTopRow, XBoxLayoutDirection_LeftToRight);
    XBoxLayout_addLayout(&self->m_inputsTopRow,
                         (XLayout*)&self->m_inputsLeftCol);
    XBoxLayout_addLayoutEx(&self->m_inputsTopRow,
                           (XLayout*)&self->m_inputsRightCol, 1);
    XLayout_setSpacing((XLayout*)&self->m_inputsTopRow, 16);
    /* 页根：顶行 stretch 铺满 + 状态行固定高贴底（边距 12,8,4,6/段距 4
       对标原 y8 起、右缘收 4、状态行 y=高-30 h24、上缝 4）。 */
    XBoxLayout_init(&self->m_inputsLayout, XBoxLayoutDirection_TopToBottom);
    XBoxLayout_addLayout(&self->m_inputsLayout,
                         (XLayout*)&self->m_inputsTopRow);
    XBoxLayout_setStretch(&self->m_inputsLayout, 0, 1);
    XBoxLayout_addWidget(&self->m_inputsLayout,
                         (XWidget*)&self->m_inputStatus);
    XWidget_setFixedHeight((XWidget*)&self->m_inputStatus, 24);
    XLayout_setContentsMargins((XLayout*)&self->m_inputsLayout, 12, 8, 4, 6);
    XLayout_setSpacing((XLayout*)&self->m_inputsLayout, 4);
    XWidget_setLayout(&self->m_pageInputs, (XLayout*)&self->m_inputsLayout);
#endif
#if XWIDGET_ON && XTABWIDGET_ON && XTABBAR_ON && XCOMBOBOX_ON && \
    XABSTRACTSLIDER_ON && XDIAL_ON && XPROGRESSBAR_ON && XFRAME_ON && \
    XLABEL_ON
    /* 页 4 装配：页签容器 stretch 铺满 + 状态行固定高贴底（边距
       12,8,12,0/段距 8 对标原页签 12,8,宽-24 与状态行 y=高-24 h24）。 */
    XBoxLayout_init(&self->m_tabsLayout, XBoxLayoutDirection_TopToBottom);
    XBoxLayout_addWidget(&self->m_tabsLayout, (XWidget*)&self->m_tabWidget);
    XBoxLayout_setStretch(&self->m_tabsLayout, 0, 1);
    XBoxLayout_addWidget(&self->m_tabsLayout, (XWidget*)&self->m_tabStatus);
    XWidget_setFixedHeight((XWidget*)&self->m_tabStatus, 24);
    XLayout_setContentsMargins((XLayout*)&self->m_tabsLayout, 12, 8, 12, 0);
    XLayout_setSpacing((XLayout*)&self->m_tabsLayout, 8);
    XWidget_setLayout(&self->m_pageTabs, (XLayout*)&self->m_tabsLayout);
#endif
#if XWIDGET_ON && XCHARTS_ON && XPUSHBUTTON_ON
    /* 页 12 装配：切换钮行顶置（钮固定 84x24、段距 8）+ 图表视图
       stretch 铺满余区（边距 12,8,12,8/段距 6 对标原钮行 y8 h24、
       图表 y38 铺到高-8）。84x24 为宽屏定值：竖屏 400dp 下五钮总宽
       超窗且 24px 行高低于手机触控标准，紧凑档由 demo_corePages_adapt
       按内容宽均分并提行高。 */
    XBoxLayout_init(&self->m_chartBtnRow, XBoxLayoutDirection_LeftToRight);
    XBoxLayout_addWidget(&self->m_chartBtnRow, (XWidget*)&self->m_btnLegend);
    XBoxLayout_addWidget(&self->m_chartBtnRow, (XWidget*)&self->m_btnGrid);
    XBoxLayout_addWidget(&self->m_chartBtnRow, (XWidget*)&self->m_btnTitle);
    XBoxLayout_addWidget(&self->m_chartBtnRow, (XWidget*)&self->m_btnSeries);
    XBoxLayout_addWidget(&self->m_chartBtnRow, (XWidget*)&self->m_btnRange);
    XBoxLayout_addStretch(&self->m_chartBtnRow, 1); /* 余宽留行尾（五钮固定宽）。 */
    XLayout_setSpacing((XLayout*)&self->m_chartBtnRow, 8);
    XBoxLayout_init(&self->m_chartLayout, XBoxLayoutDirection_TopToBottom);
    XBoxLayout_addLayout(&self->m_chartLayout, (XLayout*)&self->m_chartBtnRow);
    XBoxLayout_addWidget(&self->m_chartLayout, (XWidget*)&self->m_chartView);
    XBoxLayout_setStretch(&self->m_chartLayout, 1, 1);
    XLayout_setContentsMargins((XLayout*)&self->m_chartLayout, 12, 8, 12, 8);
    XLayout_setSpacing((XLayout*)&self->m_chartLayout, 6);
    XWidget_setLayout(&self->m_pageChart, (XLayout*)&self->m_chartLayout);
#endif
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
    if (s_logBaseInit)
    {
        /* 每行前缀 [秒.毫秒]：距 main 入口耗时（XDateTime 单调毫秒）。
         * 静态期调用（markBase 之前）不打前缀，恒等价旧行为。 */
        int64_t now = XDateTime_currentMSecsSinceEpoch();
        printf("[%d.%03d] ", (int)((now - s_logBaseMs) / 1000),
               (int)((now - s_logBaseMs) % 1000));
    }
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
    /* 三处 main 兜底之三（评审⑤）：XKB_METRICS 未设时置 legacy——桌面
       会话/Xvfb 可能读到 RandR 毫米标定而进 calibrated 轨，既有截图基
       线与风格检查按 legacy 几何锁；档位目验脚本显式 env（XKB_METRICS
       /XKB_FORCE_DEVICE/XKB_FORCE_DPI）优先（覆写 0）。 */
    if (!XSystem_environment("XKB_METRICS") ||
        !XSystem_environment("XKB_METRICS")[0])
    {
#ifdef _WIN32
        _putenv_s("XKB_METRICS", "legacy");
#else
        setenv("XKB_METRICS", "legacy", 0);
#endif
    }
    demo_log_markBase(); /* 诊断时间戳基准：此后每行日志前缀 [秒.毫秒]。 */
    demo_log_markBase(); /* 诊断时间戳基准：此后每行日志前缀 [秒.毫秒]。 */
    const char* styleOpt;
    int screenshotPage;
    bool openMenuOpt;
    int screenshotTab;
    int orientOpt;      /* --orient：截图前切到的窗口方向（0/90/180/270）。 */
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
#if defined(__linux__) || defined(__GLIBC__) || defined(__BIONIC__)
    /* 【内存驻留治理 2026-10-06】glibc 阈值调优: 换页/拖动/编码每步 churn
     * 1-3MB 分配释放(静态场景重建+tile 差带+镜像编码), 缺省阈值下 glibc
     * 把释放块滞留主堆不还系统——A33(56MB 无 swap)实测导航点击 RSS
     * +125MB 不回落直至 OOM(用户实测吃满)。mmap 阈值 128KB=大块走
     * mmap/munmap 即时归还; trim 阈值同步收紧。仅影响本进程。 */
#if defined(__GLIBC__)
    mallopt(M_TRIM_THRESHOLD, 128 * 1024);
    mallopt(M_MMAP_THRESHOLD, 128 * 1024);
#endif
#endif /* __linux__ || __GLIBC__ || __BIONIC__ */
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
    openMenuOpt = false;
    screenshotTab = -1;
    orientOpt = -1;   /* 默认不切方向。 */
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
        else if (strcmp(argv[argi], "--fullscreen") == 0) {
            /* 启动即全屏（对标 QWidget::showFullScreen）：与 --maximized
             * 的状态语义不同——全屏隐藏 CSD 标题栏、内容铺满整窗。 */
            g_startFullScreen = true;
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
        else if (strcmp(argv[argi], "--menu") == 0) {
            /* 启动即打开移动端菜单页（紧凑形态的导航页）：手机端无头/
             * 自动化验证用（无输入注入权限的设备上无法点汉堡钮）。 */
            openMenuOpt = true;
        }
        else if (strcmp(argv[argi], "--hr-stress") == 0) {
            /* [两孩子删除句柄身份压测 2026-10-25] 独立入口：不建窗，
             * 直接跑高精度定时器组的随机增删/句柄回收压力测试后退出。
             * 返回码=失败断言数。仅桌面目标（XHrTimerStressTest 所在
             * 源文件未编入安卓 xguidemo 目标）。 */
#if !defined(__ANDROID__)
            extern int XHrTimerStressTest(void);
            return XHrTimerStressTest();
#else
            XPrintf("XGuiWindowDemo: --hr-stress is desktop-only\n");
#endif
        }
        else if (strcmp(argv[argi], "--tab") == 0) {
            screenshotTab = atoi(argv[++argi]);
        }
        else if (strcmp(argv[argi], "--orient") == 0 && argi + 1 < argc) {
            /* 窗口方向（0/90/180/270）：截图前先切到该角度。
             * 自动化验证用——靠鼠标点下拉框的像素坐标太脆弱，
             * 换台机器/换个 DPI 就点空。 */
            orientOpt = atoi(argv[++argi]);
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
        else if (strcmp(argv[argi], "--autotest-page") == 0 &&
                 argi + 1 < argc) {
            /* 专项页号：只跑该页的自测（全量调度会在前置页存量崩溃处
               中止，其后各页永远测不到）。仍隐含 --autotest。 */
            g_autotestPageOnly = atoi(argv[++argi]);
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
    demo_diag("phase: app created\n");
#if defined(__ANDROID__)
    /* --clipboard-test：剪贴板后端自检（写→回读→logcat 出证）。写在
     * 应用创建后：installClipboardBackend 在 XGuiApplication 初始化内
     * 完成，此刻 JNI 后端已就位。结果经 XGuiDemo_debugLog 进 logcat
     * （tag=XGuiDemo），系统侧再以 `service call clipboard 2` 交叉验证。 */
    for (argi = 1; argi < argc; ++argi) {
        if (strcmp(argv[argi], "--clipboard-test") == 0) {
            extern void XGuiDemo_debugLog(const char* text);
            XClipboard* clip = XGuiApplication_clipboard();
            XString* payload;
            XString* back;
            char msg[192];
            if (!clip) {
                XGuiDemo_debugLog("[CLIPTEST] FAIL no clipboard instance");
                break;
            }
            payload = XString_create_utf8("xgui-clip-e2e-20261024");
            XClipboard_setText(clip, payload, XClipboardMode_Clipboard);
            back = XClipboard_text(clip, XClipboardMode_Clipboard);
            snprintf(msg, sizeof(msg), "[CLIPTEST] readback=%s",
                     back ? XString_toUtf8(back) : "(null)");
            XGuiDemo_debugLog(msg);
            if (payload) XClassDelete((XClass*)payload);
            if (back) XClassDelete((XClass*)back);
            break;
        }
    }
#endif
#if defined(__ANDROID__)
    /* --probe-platform：平台补扫自检（主题/Services/WiFi），logcat 出证
     * （tag=XGuiDemo，前缀 [PROBE]）。只读探针：openUrl 不实发（会跳出
     * 应用），只报 isAvailable；WiFi 只扫描+查询连接。 */
    for (argi = 1; argi < argc; ++argi) {
        if (strcmp(argv[argi], "--probe-platform") == 0) {
            extern void XGuiDemo_debugLog(const char* text);
            bool probeDark = false;
            char probeName[64];
            XFd probeWifiFd;
            char msg[256];
            if (XPlatformThemeDriver_detect(&probeDark, probeName,
                                            sizeof(probeName)))
                snprintf(msg, sizeof(msg), "[PROBE] theme: dark=%d name=%s",
                         (int)probeDark, probeName);
            else
                snprintf(msg, sizeof(msg),
                         "[PROBE] theme: detect failed (fallback)");
            XGuiDemo_debugLog(msg);
            snprintf(msg, sizeof(msg), "[PROBE] services: url=%d",
                     (int)XPlatformServicesDriver_isAvailable());
            XGuiDemo_debugLog(msg);
            probeWifiFd = XDeviceWiFi_open(NULL, NULL);
            if (probeWifiFd != XFD_INVALID) {
                uint32_t probeCaps = XDeviceWiFi_capabilities(probeWifiFd);
                XVector* probeAdapters = NULL;
                snprintf(msg, sizeof(msg),
                         "[PROBE] wifi: open ok caps=0x%04x",
                         (unsigned)probeCaps);
                XGuiDemo_debugLog(msg);
                if (XDeviceWiFi_enumAdapters(probeWifiFd, &probeAdapters) &&
                    probeAdapters) {
                    size_t ai;
                    for (ai = 0; ai < XVector_size_base(probeAdapters); ++ai) {
                        XDeviceWiFiAdapterInfo** slot =
                            (XDeviceWiFiAdapterInfo**)
                                XVector_at_base(probeAdapters, (int64_t)ai);
                        if (slot && *slot && (*slot)->id) {
                            const char* id = XString_toUtf8((*slot)->id);
                            int waits = 0;
                            XVector* networks = NULL;
                            XDeviceWiFi_startScan(probeWifiFd, id);
                            while (waits++ < 25 &&
                                   XDeviceWiFi_scanState(probeWifiFd, id) ==
                                       XDeviceWiFi_ScanRunning)
                                XThread_msleep(200);
                            snprintf(msg, sizeof(msg),
                                     "[PROBE] wifi adapter=%s scanState=%d",
                                     id,
                                     (int)XDeviceWiFi_scanState(probeWifiFd,
                                                                id));
                            XGuiDemo_debugLog(msg);
                            if (XDeviceWiFi_takeScanResults(probeWifiFd, id,
                                                            &networks) &&
                                networks) {
                                size_t ni;
                                snprintf(msg, sizeof(msg),
                                         "[PROBE] wifi scan: %u nets",
                                         (unsigned)XVector_size_base(networks));
                                XGuiDemo_debugLog(msg);
                                for (ni = 0;
                                     ni < XVector_size_base(networks) &&
                                     ni < 8;
                                     ++ni) {
                                    XDeviceWiFiNetworkInfo** nslot =
                                        (XDeviceWiFiNetworkInfo**)
                                            XVector_at_base(networks,
                                                            (int64_t)ni);
                                    if (nslot && *nslot) {
                                        snprintf(msg, sizeof(msg),
                                                 "[PROBE]  net[%u] ssid=%s"
                                                 " sig=%d ch=%d sec=%d",
                                                 (unsigned)ni,
                                                 (*nslot)->ssid
                                                     ? XString_toUtf8(
                                                           (*nslot)->ssid)
                                                     : "(hidden)",
                                                 (*nslot)->signalStrength,
                                                 (*nslot)->channel,
                                                 (int)(*nslot)->security);
                                        XGuiDemo_debugLog(msg);
                                    }
                                }
                                XDeviceWiFi_freeNetworks(networks);
                            } else {
                                XGuiDemo_debugLog(
                                    "[PROBE] wifi scan: no results"
                                    " (定位权限缺失或无 AP)");
                            }
                        }
                    }
                    XDeviceWiFi_freeAdapters(probeAdapters);
                }
                {
                    XDeviceWiFiConnectionInfo conn;
                    memset(&conn, 0, sizeof(conn));
                    if (XDeviceWiFi_queryConnection(probeWifiFd, "wlan0",
                                                    &conn)) {
                        snprintf(msg, sizeof(msg),
                                 "[PROBE] wifi conn: ssid=%s sig=%d",
                                 conn.ssid ? XString_toUtf8(conn.ssid)
                                           : "(null)",
                                 conn.signalStrength);
                        XDeviceWiFi_freeConnectionInfo(&conn);
                    } else {
                        snprintf(msg, sizeof(msg),
                                 "[PROBE] wifi conn: none");
                    }
                    XGuiDemo_debugLog(msg);
                }
                /* 连接/断开能力探测：走 L0 契约（与 UI 页同一设备同一层）。
                 * 安卓的连接走 WifiNetworkSpecifier（系统弹窗确认），故
                 * 这里只报"发起是否被受理"与"操作状态机"，不代用户确认。 */
                {
                    uint32_t probeCaps2 = XDeviceWiFi_capabilities(probeWifiFd);
                    snprintf(msg, sizeof(msg),
                             "[PROBE] wifi caps2=0x%04x (scan=%d connect=%d "
                             "disconnect=%d profile=%d)",
                             (unsigned)probeCaps2,
                             (probeCaps2 & XDeviceWiFi_CapScan) ? 1 : 0,
                             (probeCaps2 & XDeviceWiFi_CapConnect) ? 1 : 0,
                             (probeCaps2 & XDeviceWiFi_CapDisconnect) ? 1 : 0,
                             (probeCaps2 & XDeviceWiFi_CapProfile) ? 1 : 0);
                    XGuiDemo_debugLog(msg);
                    {
                        /* 探针连**开放网络** XinYueAP（系统已保存、当前未连）：
                         * 验证"查已存列表 → enableNetwork(netId, true) 立即
                         * 切换"的经典路径（连接归系统持有）。选开放网络是为
                         * 了免口令即可全链路走通。 */
                        bool started = XDeviceWiFi_startConnect(
                            probeWifiFd, "wlan0", "XinYueAP", NULL,
                            XDeviceWiFi_SecurityOpen);
                        snprintf(msg, sizeof(msg),
                                 "[PROBE] wifi startConnect(受理=%d) state=%d",
                                 started ? 1 : 0,
                                 (int)XDeviceWiFi_connectState(probeWifiFd,
                                                              "wlan0"));
                        XGuiDemo_debugLog(msg);
                        if (started) {
                            int tw = 0;
                            int st = (int)XDeviceWiFi_connectState(
                                probeWifiFd, "wlan0");
                            while (tw++ < 20 &&
                                   st == (int)XDeviceWiFi_ConnectConnecting) {
                                XThread_msleep(250);
                                st = (int)XDeviceWiFi_connectState(
                                    probeWifiFd, "wlan0");
                            }
                            snprintf(msg, sizeof(msg),
                                     "[PROBE] wifi connect 终态=%d", st);
                            XGuiDemo_debugLog(msg);
                        }
                        snprintf(msg, sizeof(msg),
                                 "[PROBE] wifi disconnect(受理=%d)",
                                 XDeviceWiFi_disconnect(probeWifiFd, "wlan0")
                                     ? 1 : 0);
                        XGuiDemo_debugLog(msg);
                    }
                }
                XDeviceWiFi_close(probeWifiFd);
            } else {
                XGuiDemo_debugLog("[PROBE] wifi: open failed");
            }
            break;
        }
    }
#endif
    /* 应用元数据走 XCoreApplication 库标准口（对标 Qt）：名称+出厂版本。
     * 版本此处仅为进程启动缺省——软件更新页构建时会把 applicationVersion
     * 同步为引擎激活版本（模拟真实产品"升级换二进制即换版本"），使标题行
     * 「程序版本」与「当前版本」两处口径一致。 */
    {
        XString* appName = XString_create_utf8("XGuiWindowDemo");
        XString* appVer = XString_create_utf8(XUPGRADE_PRODUCT_VERSION);
        if (appName) {
            XCoreApplication_setApplicationName(appName);
            XClassDelete((XClass*)appName);
        }
        if (appVer) {
            XCoreApplication_setApplicationVersion(appVer);
            XClassDelete((XClass*)appVer);
        }
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
#if defined(__linux__) && XGUI_ON && XPLATFORM_DRM_ON
    /* 1.6.0) DRM 优先：现代嵌入式 BSP（RK3506 等）默认不开 fbdev 仿真，
       只有 /dev/dri/card0；注册成功则 fbdev 探测恒败、自动让位。 */
    if (XPlatformNativeWindow_useDrmDriver(NULL))
        XPrintf("XGuiWindowDemo: DRM 显示驱动已注册（KMS dumb 直写）\n");
    else
        XPrintf("XGuiWindowDemo: DRM 显示驱动不可用（无 /dev/dri 或无已连接面板）\n");
#endif
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
    demo_diag("phase: DemoWin_create done\n");
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
#if XWIDGET_ON && XWINDOW_ON
    /* 应用图标：品牌条棋盘格同源（三平台落地路径见函数注释）；show 前
       存值，窗口句柄创建时经 XWidget→XWindow 桥接 + 平台 setIcon 落地。 */
    demo_applyWindowIcon(&win->m_base);
#endif
#if XWIDGET_ON && XWINDOW_ON && defined(__ANDROID__)
    /* [安卓无边框 2026-10-25] 安卓没有窗口管理器，框架的 CSD 自绘条件
       命中（无原生窗口回落路径）→ 会画出一条桌面式系统标题栏（最小化/
       最大化/关闭）。手机上没有窗口可最小化，那条标题栏既无意义又挤
       占顶部空间：置 FramelessWindowHint 抑制装饰，顶部改由 demo 自绘
       的应用栏/品牌条承担（桌面形态保留 CSD 行为不变）。 */
    XWidget_setWindowFlags(&win->m_base,
                           XWidget_windowFlags(&win->m_base) |
                               (XWidgetFlags)XWindowType_FramelessWindowHint);
#if XLAYOUT_ON
    /* 根布局上边距 = 框架系统标题栏高（桌面 CSD）。[下移取消 2026-10-25]
     * 安卓改沉浸式（状态栏隐藏、surface 铺满），应用栏直接落 y=0，
     * 不再做状态栏让位下移。 */
    XLayout_setContentsMargins((XLayout*)&win->m_rootLayout, 0,
                               demo_sysbarH(win), 0, 0);
    XWidget_updateGeometry((XWidget*)&win->m_base);
#endif
#endif
#if XWIDGET_ON && XLAYOUT_ON && XLAYOUT_STACKED_ON
    /* [懒分配 2026-10-19·工程要点①·红线] CLI autostart 豁免：
     * --remote-server/--remote-client 预置时对应远程页在事件循环前
     * 真建。链路：--remote-server→remoteCli.enabled（:5081 附近解析）→
     * cliDefaults 存 s_rsCli/s_rcCli（:5137 附近）→framePump 每帧调
     * autostart（remote_server.c 守卫 if(!s_rs.m_ready||!s_rsCli.enabled)
     * return，m_ready 在 build 尾置位；客户端同构）——懒分配下页面未显
     * 不建则 m_ready 恒假，autostart 先于「不静默报告」返回，被控端将
     * 静默不监听。此处 ensureBuilt 后 m_ready 就绪，首帧 autostart 走
     * 既有 UI 启动路径（host 需后备存储就绪，内部自带重试/终态标记）。
     * 裁剪配置下 builder 契约桩返回 NULL，槽位清空，autostart 桩自身
     * 零操作，两道防线语义一致。 */
    if (remoteCli.enabled)
        demo_ext_ensureBuilt(win, 5); /* 页 10 远程窗口设置（exti 5）。 */
    if (remoteClientCli.enabled)
        demo_ext_ensureBuilt(win, 6); /* 页 11 远程客户端设置（exti 6）。 */
#endif

    win->m_screenshotPath = screenshotPath;
    win->m_screenshotFrames = 0;
    win->m_autoTest = autoTest;
    win->m_autoTestFrames = 0;
    win->m_orientOpt = orientOpt;
    win->m_orientApplied = false;
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
#if defined(__ANDROID__)
    /* [安卓全屏 2026-10-25] 安卓无窗口管理器：surface 即全部画布，窗口
       几何必须跟随 surface 的逻辑尺寸，否则框架按桌面尺寸（1000x700）
       排布、present 再按 presentScale(=dpr=3) 放大进画布 → 内容被裁
       （真机实证：竖屏下右侧面板整片裁掉）。平台层在 surface 创建/旋转
       时回推全屏几何（XPlatformWindowAndroid 的 m_geomPending 通道），
       此处只给一个手机量级的初值，避免首帧出现桌面尺寸的布局。 */
    XWidget_setGeometry(&win->m_base, 0, 0, 400, 800);
#else
    if (win->m_extPages[0] || win->m_extPages[1] ||
        win->m_extPages[2] || win->m_extPages[3] || win->m_extPages[4] ||
        win->m_extPages[5] || win->m_extPages[6] || win->m_extPages[7] ||
        win->m_extPages[8] || win->m_extPages[9] || win->m_extPages[10] ||
        win->m_extPages[11])
        {
            /* 16 页导航/扩展页（含二维码/软件更新双页）与网络设置双列
               布局需要 1000x700；无头一致性采集（XGUI_DEMO_WINDOW_SIZE=
               1000x600）可对齐面板尺寸，使桌面参考与设备窗口同几何、
               逐像素可比。 */
            int baseW = 1000;
            int baseH = 700;
            const char* winSz = XSystem_environment("XGUI_DEMO_WINDOW_SIZE");
            if (winSz && winSz[0])
            {
                int envW = 0;
                int envH = 0;
                if (sscanf(winSz, "%dx%d", &envW, &envH) == 2 &&
                    envW >= 320 && envH >= 240 && envW <= 4096 && envH <= 4096)
                {
                    baseW = envW;
                    baseH = envH;
                }
            }
            XWidget_setGeometry(&win->m_base, 40, 40, baseW, baseH);
        }
    else
        XWidget_setGeometry(&win->m_base, 60, 60, 520, 360);
#endif /* !__ANDROID__ */
#else
    XWidget_setGeometry(&win->m_base, 60, 60, 520, 360);
#endif /* XWIDGET_ON && XLAYOUT_ON && XLAYOUT_STACKED_ON */
#if defined(XINYUE_PANEL_WIDTH) && defined(XINYUE_PANEL_HEIGHT)
    /* 嵌入式单屏：窗口全屏铺满面板（板级 present 回调直写 GRAM，
     * 面板尺寸经编译定义注入，见 Drive/STM32/ShenzhouF407）。 */
    XWidget_setGeometry(&win->m_base, 0, 0, XINYUE_PANEL_WIDTH,
                        XINYUE_PANEL_HEIGHT);
#endif
    /* 键盘页无头截图钩子（Tools/VirtualKeyboard/style_check.py；环境变量缺省时
       零操作）：几何定版后、事件循环前同步弹出——守护轮询 200ms 在
       --screenshot 3 帧内到不了，钩子不依赖定时器边沿。
       [懒分配 2026-10-19] 无头自动化豁免：XGUI_KB_* 环境族在位时先真
       建键盘页（exti 4，页 9）再调钩子——懒分配下页 9 未显不建，钩子
       首行 s_kbd.page 判空整体零操作，风格检查动态层会全空；环境族缺
       省时 requested() 恒假，维持全量懒分配口径零开销。 */
#if XWIDGET_ON && XLAYOUT_ON && XLAYOUT_STACKED_ON
    if (demo_page_keyboard_headless_requested())
        demo_ext_ensureBuilt(win, 4);
#endif
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
    demo_main_refreshChrome(win);
#endif
#if XWIDGET_ON && XABSTRACTBUTTON_ON && XPUSHBUTTON_ON && XFRAME_ON && \
    XLABEL_ON && XLAYOUT_ON && XLAYOUT_STACKED_ON
    /* --menu：启动即展开移动端菜单页（紧凑形态的导航页）。真机若没有
       输入注入权限（adb shell input 被 ROM 拒绝）就点不到汉堡钮，自动化
       验证走此入口。必须在窗口几何定版之后执行——形态判定读的是窗口宽
       （紧凑断点 600dp），几何未定时宽为 0、形态判定会悬空。 */
    if (openMenuOpt) {
        demo_layoutModeUpdate(win);
        demo_menuToggle(win);
    }
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
        else if (g_startFullScreen)
            XWidget_showFullScreen(&win->m_base);
        else
            XWidget_showNormal(&win->m_base);
    } else
#endif
    if (benchmarkMaximized)
        XWidget_showMaximized(&win->m_base);
    else if (g_startFullScreen)
        XWidget_showFullScreen(&win->m_base);
    else
        XWidget_showNormal(&win->m_base);
#if defined(__ANDROID__) && XWIDGET_ON
    /* [状态对齐 2026-10-25 用户裁定] 安卓=沉浸式全屏（Java 侧隐藏状态
     * 栏 + 挖孔区铺满），但框架窗口态此前仍是 Normal——窗口设置页的
     * 「全屏显示」勾选经 wn_syncFromWindow 回读 XWidget_isFullScreen
     * 时谎报未勾（用户真机指正：全屏显示没勾，当前是全屏显示）。show
     * 之后置 FullScreen 只翻状态位，几何本就全屏（几何回推通道管）；
     * 平台层「还原」路径同步改为回落全屏几何（手机无窗口化语义，
     * 见 XPlatformNativeWindow_setWindowState），取消勾选不会缩窗。 */
    XWidget_showFullScreen(&win->m_base);
#endif
#if defined(__linux__) && XGUI_ON && XPLATFORM_FBDEV_ON
    if (fbPanelW > 0 && fbPanelH > 0 && !benchmarkMaximized &&
        !g_startFullScreen)
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
    demo_diag("phase: postShow done\n");
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
        demo_diag("phase: first full paint done\n");
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
        /* 顶栏系统时钟刷新定时器（1 秒：驱动时钟 XLabel setText）。 */
        win->m_clockTimer = XObject_startTimer_ms(
            (XObject*)win, 1000u, XTimerType_CoarseTimer);
        if (win->m_clockTimer == XTIMER_INVALID_ID) {
            XPrintf("XGuiWindowDemo: 顶栏时钟定时器创建失败\n");
        }
        /* [时钟入布局 2026-10-29] 时钟本体=XLabel，作为顶栏布局的右端
         * 条目（不用 m_base 上的浮层 + 定时器手算锚点）：宽度由布局按
         * sizeHint 自动分配，setText 经 updateGeometry 驱动重排，文本
         * 变长/换字体都自动跟随；品牌条右缘 40 让位棋盘格，应用栏留
         * 12。两栏各挂一个，显隐随父栏（紧凑/宽屏切换零代码）。 */
        win->m_clockLabel =
            demo_clockLabelCreate((XWidget*)&win->m_brandBar);
        if (win->m_clockLabel) {
            XBoxLayout_init(&win->m_brandBarLayout,
                            XBoxLayoutDirection_LeftToRight);
            XLayout_setContentsMargins((XLayout*)&win->m_brandBarLayout,
                                       0, 0, 40, 0);
            XBoxLayout_addStretch(&win->m_brandBarLayout, 1);
            XBoxLayout_addWidget(&win->m_brandBarLayout,
                                 (XWidget*)win->m_clockLabel);
            XWidget_setLayout((XWidget*)&win->m_brandBar,
                              (XLayout*)&win->m_brandBarLayout);
            /* 品牌条创建早于窗口显示，其后新建的子控件不会自动可见
             * （Qt 同语义：父已显示后创建的子控件须显式 show）。 */
            XWidget_show((XWidget*)win->m_clockLabel);
        } else {
            XPrintf("XGuiWindowDemo: 品牌条时钟控件创建失败\n");
        }
        win->m_clockLabelApp =
            demo_clockLabelCreate((XWidget*)&win->m_appBar);
        if (win->m_clockLabelApp) {
            XBoxLayout_init(&win->m_appBarLayout,
                            XBoxLayoutDirection_LeftToRight);
            XLayout_setContentsMargins((XLayout*)&win->m_appBarLayout,
                                       0, 0, 12, 0);
            XBoxLayout_addStretch(&win->m_appBarLayout, 1);
            XBoxLayout_addWidget(&win->m_appBarLayout,
                                 (XWidget*)win->m_clockLabelApp);
            XWidget_setLayout((XWidget*)&win->m_appBar,
                              (XLayout*)&win->m_appBarLayout);
            XWidget_show((XWidget*)win->m_clockLabelApp);
        } else {
            XPrintf("XGuiWindowDemo: 应用栏时钟控件创建失败\n");
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
    /* [退出段 UAF 根修 2026-10-19·目验失败项②] 页面收尾三连必须先于
     * 全部成员 XClassDeinit（尤其 :5755 附近 XClassDeinit(&m_navSplitter)
     * ——[XSplitter 迁移 2026-10-17] 后导航分割器是内容区祖先，其
     * VX_splitter_deinit 会级联 delete 整个内容子树，含远程客户端页的
     * XGuiClient 与会话条）。原顺序：分割器级联先释放 client（堆子），
     * 随后同轮级联里的会话条 VX_xrb_deinit 仍持 d->m_client 悬垂指针
     * 走 XObject_disconnect_1（XObject.c:649 读已释放连接向量）——
     * ASan heap-use-after-free 实证（--page 10/11/18 与 --autotest 退
     * 出段稳定复现，改动前后 A/B 同源，非懒分配引入）。提前到成员
     * deinit 之前：client 尚存活时 setClient(NULL) 解绑四信号 + 断链 +
     * deleteLater（deferred 事件由级联 XObject_deinit 清挂起，两序皆
     * 安全，见 XObject.c:583），会话条 deinit 见 m_client==NULL 整段
     * 跳过；server unhost+close 解除 present 登记同理前移。 */
    demo_page_remote_server_shutdown();
    demo_page_remote_client_shutdown();
    demo_page_update_shutdown();
#if XWIDGET_ON && XFRAME_ON && XLABEL_ON
    XClassDeinit((XLabel*)&win->m_statusLabel);
    XClassDeinit(&win->m_brandBar);
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
        for (nav = 0; nav < DEMO_NAV_PAGE_N; ++nav) /* 全部页面钮随当前页数回收（2026-10-19 修正陈旧的 17 硬编码）。 */
            XClassDeinit(&win->m_pageNav[nav]);
    }
#endif
#if XWIDGET_ON && XABSTRACTBUTTON_ON && XPUSHBUTTON_ON && XFRAME_ON && \
    XLABEL_ON && XLAYOUT_ON && XLAYOUT_STACKED_ON
    /* 浮动导航面板成员（面板/标题/两钮/4 一级分类钮 + 5 子分组标题
       标签池；页面钮已在其上回收，面板容器随 m_base 级联，成员结构体
       须显式 deinit）。 */
    {
        int cat;
        for (cat = 0; cat < DEMO_NAV_CAT_N; ++cat)
            XClassDeinit(&win->m_navCatBtns[cat]);
        for (cat = 0; cat < DEMO_NAV_HEAD_N; ++cat)
            XClassDeinit(&win->m_navHeadLabels[cat]);
        XClassDeinit(&win->m_navDockBtn);
        XClassDeinit(&win->m_navTitle);
#if XSPLITTER_ON
        /* 分割器内部 sizes/stretch/collapsible 数组为堆分配，须显式
           deinit 回收（vx 分割器 deinit 释放）。 */
        XClassDeinit((XWidget*)&win->m_navSplitter);
#endif
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
    /* 远程双页与软件更新页收尾已前移至本段开头（[退出段 UAF 根修
       2026-10-19]，须先于 m_navSplitter 等成员级联 deinit，见 :5669
       附近注）。 */
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
    /* 三处 main 兜底之三（裁剪壳同口径；见上方完整分支注）。 */
    if (!XSystem_environment("XKB_METRICS") ||
        !XSystem_environment("XKB_METRICS")[0])
    {
#ifdef _WIN32
        _putenv_s("XKB_METRICS", "legacy");
#else
        setenv("XKB_METRICS", "legacy", 0);
#endif
    }
    XPrintf("XGuiWindowDemo: 本演示需要 XGUIAPPLICATION_ON / XWINDOW_ON / "
                    "XBACKINGSTORE_ON / XPLATFORMINTEGRATION_ON / "
                    "XPLATFORMNATIVEWINDOW_ON。\n");
    return 2;
}
#endif /* 开关 */
/* 注：文末历史「补齐未闭合 #if」的补位 #endif 已随 2026-10-03 页签节
 * 结构整顿（页 3/容器节门控重排）恢复平衡而移除，全文件 #if/#endif
 * 严格配对。 */
