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
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include "CXinYueConfig.h"
#include "XPrintf.h"
#include "XObject.h"
#include "XEvent.h"
#include "XAbstractEventDispatcher.h"
#include "XDateTime.h"
#include "XGuiApplication.h"
#include "XWidget.h"
#include "XWidget_Protected.h"
#include "XImage.h"
#include "XWindow.h"
#include "XWindowEvent.h"
#if XPLATFORMINTEGRATION_ON && XGPU_ON
#include "XGpuRenderBackend.h"
#endif
#if XPLATFORMINTEGRATION_ON && XPLATFORMNATIVEWINDOW_ON
#include "XPlatformNativeWindow.h"
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

#if XGUIAPPLICATION_ON && XWIDGET_ON && XWINDOW_ON && XBACKINGSTORE_ON && \
    XPLATFORMINTEGRATION_ON && XPLATFORMNATIVEWINDOW_ON

/* The demo follows the scalable built-in face whenever it is compiled in.
   A clipped embedded build keeps the existing bitmap fallback. */
#if XFONT_BUILTIN_OUTLINE_ON
#define XGUI_DEMO_DEFAULT_FONT_FAMILY "XFontOutlineCommon"
#else
#define XGUI_DEMO_DEFAULT_FONT_FAMILY XFONT_DEFAULT_FAMILY
#endif

/* Desktop demo 默认缓存静态控件场景；资源受限目标可显式设为 0。 */
#ifndef XGUI_DEMO_STATIC_SCENE_CACHE_ON
#define XGUI_DEMO_STATIC_SCENE_CACHE_ON 1
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
    image = XWidget_paintDevice(self);
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
    bool            m_closed; /**< CloseEvent 被接受或自动退出后置真。 */
    const char*     m_screenshotPath; /**< 非空时渲染数帧后保存一帧截图并退出（借用指针）。 */
    int             m_screenshotFrames; /**< 截图模式已渲染帧数。 */
    bool            m_autoTest;         /**< 自动交互测试模式（第 4 页注入事件断言联动）。 */
    int             m_autoTestFrames;   /**< 自动测试已渲染帧数。 */
#if XGUI_PERFORMANCE_OVERLAY_ON && XWIDGET_ON && XFRAME_ON && XLABEL_ON
    XPerformanceOverlay m_performanceOverlay; /**< 性能悬浮层控件。 */
#if XGUI_PERFORMANCE_OVERLAY_NETWORK_ON
    int64_t m_lastNetworkPollUsecs; /**< 最近一次主机网络计数采样时刻。 */
#endif
#endif
#if XWIDGET_ON && XFRAME_ON && XLABEL_ON
    XLabel          m_titleLabel; /**< 顶部标题栏文本（深蓝背景，白字）。 */
    DemoStatusLabel m_statusLabel; /**< 底部状态栏（自带深色底，白字）。 */
#endif
#if XWIDGET_ON && XPUSHBUTTON_ON
    XPushButton     m_pageNav[5]; /**< 页面切换按钮：按钮/选择/堆叠/输入/选项卡演示。 */
#endif
#if XWIDGET_ON && XLAYOUT_ON && XLAYOUT_STACKED_ON
    XStackedLayout  m_stackLayout; /**< 主内容堆叠布局（4 个演示页面）。 */
    XWidget         m_pageButtons; /**< 页面 0：按钮演示容器。 */
    XWidget         m_pageChoices; /**< 页面 1：选择演示容器。 */
    XWidget         m_pageStacked; /**< 页面 2：堆叠演示容器。 */
    XWidget         m_pageInputs;  /**< 页面 3：输入控件演示容器。 */
    XWidget         m_pageTabs;    /**< 页面 4：选项卡演示容器。 */
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

/** @brief 将 demo 使用的 XFont 默认家族设置为当前可用的内置字库。 */
static void demo_apply_default_font(XFont* font)
{
    if (font)
        XFont_setFamily(font, XGUI_DEMO_DEFAULT_FONT_FAMILY);
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
    XFont_deinit_base(&font);
}
#endif /* XWIDGET_ON */

/** @brief 画棋盘格纹理：验证亚像素级脏区提交正确性。 */
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
        XFont_deinit_base(&labelFont);
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
    XLabel_deinit_base(&label);
}
#endif /* XWIDGET_ON && XFRAME_ON && XLABEL_ON */

/* ==================== 性能悬浮层 ==================== */

#if XGUI_PERFORMANCE_OVERLAY_ON && XWIDGET_ON && XFRAME_ON && XLABEL_ON

/** @brief 初始化性能悬浮层；作为顶层子控件参与控件树绘制。 */
static void demo_performance_init(DemoWin* self)
{
    if (!self) return;
    XPerformanceOverlay_init(&self->m_performanceOverlay, &self->m_base, 0);
    /* 悬浮窗自身不接收鼠标：按下/拖动命中测试由 demo 根控件统一处理
       （见 VDemoWin_mousePressEvent），避免子控件抢先消费事件。 */
    XWidget_setAttribute((XWidget*)&self->m_performanceOverlay,
                         XWidgetAttribute_TransparentForMouseEvents, true);
    /* 状态栏固定占用窗口底部 26px。把浮层缩至三行文字所需高度，
       并在状态栏上方保留同样的 26px 间距，避免每帧脏区同时重绘/遮挡
       状态标签。 */
    XPerformanceOverlay_setSize(&self->m_performanceOverlay, 180, 54);
    XPerformanceOverlay_setFontFamily(&self->m_performanceOverlay,
                                      XGUI_DEMO_DEFAULT_FONT_FAMILY);
    XPerformanceOverlay_setTextPixelSize(&self->m_performanceOverlay, 14);
    XPerformanceOverlay_setPresetPosition(
        &self->m_performanceOverlay, XPerformanceOverlayPosition_BottomRight,
        520, 360, 26);
    XPerformanceOverlay_setFixed(&self->m_performanceOverlay, true);
}

static void demo_performance_deinit(DemoWin* self)
{
    if (!self) return;
    XPerformanceOverlay_deinit_base(&self->m_performanceOverlay);
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

#if XGUI_DEMO_STATIC_SCENE_CACHE_ON
/** @brief 绘制不随性能采样变化的 Demo 场景：窗口背景、标题栏与状态栏基底。
 * @details 标题/状态文本与导航按钮由真实子控件接管；此处只画静态底色，
 *          右上角保留一小块棋盘格用于验证脏区提交。 */
static void demo_drawStaticScene(DemoWin* self, XPainter* painter, int w, int h)
{
    XFont painterFont;
    if (!self || !painter || w <= 0 || h <= 0) return;
    XFont_init(&painterFont);
    demo_apply_default_font(&painterFont);
    XPainter_setFont(painter, &painterFont);
    XFont_deinit_base(&painterFont);

    demo_fill_rect(painter, 0, 0, w, h, 0xfff4f6f8u);       /* 窗口背景 */
    demo_fill_rect(painter, 0, 0, w, 40, 0xff1f4e79u);      /* 标题栏基底 */
    /* 状态栏底色由 DemoStatusLabel 子控件自带（要盖在越界内容之上，
     * 不能画在根背景里）。 */
    demo_draw_checker(painter, w - 116, 84, 2, 2, 24);      /* 右上角装饰 */
    /* 标题文本由 m_titleLabel 子控件绘制（深蓝底白字），静态场景不再重复画。 */
}

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
        XImage_deinit_base(&self->m_staticScene);
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
    device = XWidget_paintDevice(&self->m_base);
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

/** @brief 按 Qt QWidget::update() 语义合并待绘区域，不同步强制整树重绘。 */
static void demo_input_autotest(DemoWin* self);
static void demo_repaint(DemoWin* self)
{
    XRect dirty;
    if (!self) return;
#if XGUI_DEMO_STATIC_SCENE_CACHE_ON
    if (self->m_staticSceneDirty ||
        XImage_width(&self->m_staticScene) != XWidget_width(&self->m_base) ||
        XImage_height(&self->m_staticScene) != XWidget_height(&self->m_base)) {
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
    XPrintf("XGuiWindowDemo: benchmark mode=%s frames=%u elapsed=%.3fs "
            "fps=%.1f avg=%.3fms longest=%.3fms\n",
            resizeWindow ? "resize" : "repaint", frameCount,
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

    /* 1. 点击微调框上箭头：值 0 -> 1（按钮区右 16px 上半）。 */
    {
        XMouseEvent me;
        XPoint pos;
        XPoint_init(&pos, spinW - 8, spinH / 4);
        XMouseEvent_init(&me, XEVENT_TYPE_MOUSE_BUTTON_PRESS,
                         XMouseButton_LeftButton, 0, pos);
        XObject_event_base((XObject*)spin, (XEvent*)&me);
        DEMO_EXPECT(XSpinBox_value(spin) == 1, "SpinBox 上箭头点击步进到 1");
        DEMO_EXPECT(XProgressBar_value(bar) == 1, "进度条同步到 1");
        DEMO_EXPECT(XAbstractSlider_value((XAbstractSlider*)slider) == 1,
                    "滑块同步到 1");
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

#undef DEMO_EXPECT
    XPrintf("XGuiAutoTest: %s\n",
            failures == 0 ? "PASS" : "FAIL");
    if (failures)
        XGuiApplication_quit();
}

static bool demo_framePump(void* userData)
{
    DemoWin* demo = (DemoWin*)userData;
    if (!demo || demo->m_closed)
        return false;
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
            demo->m_staticSceneDirty = true;
            demo_repaint(demo);
        }
        else if (demo->m_autoTestFrames >= 5) {
            {
                XImage* device = XWidget_paintDevice(&demo->m_base);
                if (device && XImage_save_2(device, "/tmp/demo_autotest_after.png",
                                            "PNG", 95))
                    XPrintf("XGuiAutoTest: 交互后截图 /tmp/demo_autotest_after.png\n");
            }
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
                    XImage_init(&shotImage);
                    XImage_init_ex(&shotImage, XWidget_width(&demo->m_base),
                                   XWidget_height(&demo->m_base),
                                   XImageFormat_ARGB32);
                    /* 建立 GL 上下文后再读回 FBO（窗口模式首帧后不清除）。 */
                    XGpuRenderBackend_beginFrame(session);
                    XGpuRenderBackend_readback(session, &shotImage);
                    XGpuRenderBackend_endWindowFrame();
                    XPrintf("XGuiWindowDemo: 保存截图到 %s\n",
                            demo->m_screenshotPath);
                    if (!XImage_save_2(&shotImage, demo->m_screenshotPath,
                                       "PNG", 95))
                        XPrintf("XGuiWindowDemo: 截图保存失败\n");
                    XImage_deinit_base(&shotImage);
                    demo_stopTimers(demo);
                    demo->m_closed = true;
                    XGuiApplication_quit();
                    return false;
                }
            }
#endif /* XPLATFORMINTEGRATION_ON && XGPU_ON */
            {
                XImage* device = XWidget_paintDevice(&demo->m_base);
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
    static const char* const kNames[5] = {
        "\xE6\x8C\x89\xE9\x92\xAE\xE6\xBC\x94\xE7\xA4\xBA", /* 按钮演示 */
        "\xE9\x80\x89\xE6\x8B\xA9\xE6\xBC\x94\xE7\xA4\xBA", /* 选择演示 */
        "\xE5\xA0\x86\xE5\x8F\xA0\xE6\xBC\x94\xE7\xA4\xBA", /* 堆叠演示 */
        "\xE8\xBE\x93\xE5\x85\xA5\xE6\xBC\x94\xE7\xA4\xBA", /* 输入演示 */
        "\xE9\x80\x89\xE9\xA1\xB9\xE5\x8D\xA1\xE6\xBC\x94\xE7\xA4\xBA"  /* 选项卡演示 */
    };
    if (index < 0 || index > 4)
        return kNames[0];
    return kNames[index];
}

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
    XWidget_setGeometry((XWidget*)&self->m_titleLabel, 16, 0,
                        labelWidth, 40);
    /* 状态栏是自带底色的整条子控件，几何必须覆盖整个状态条区域。 */
    if (height >= 26)
        XWidget_setGeometry((XWidget*)&self->m_statusLabel, 0,
                            height - 26, width, 26);
}
#endif /* XWIDGET_ON && XFRAME_ON && XLABEL_ON */

/** @brief 按当前窗口尺寸重新分配主内容区几何（切换页面/resize 时调用）。 */
static void demo_layout_content(DemoWin* self)
{
    XRect content;
    int width;
    int height;
    int contentWidth;
    int contentHeight;
    if (!self) return;
    width = XWidget_width(&self->m_base);
    height = XWidget_height(&self->m_base);
    contentWidth = width - 24;
    contentHeight = height - 78 - 28;
    if (contentWidth < 0) contentWidth = 0;
    if (contentHeight < 0) contentHeight = 0;
    XRect_init(&content, 12, 78, contentWidth, contentHeight);
    XLayoutItem_setGeometry_base((XLayoutItem*)&self->m_stackLayout,
                                 &content);
#if XWIDGET_ON && XGROUPBOX_ON && XLINEEDIT_ON && XSPINBOX_ON && \
    XABSTRACTSLIDER_ON && XSLIDER_ON && XPROGRESSBAR_ON
    /* 页面 4：输入控件按内容区宽度自适应摆位（控件纵向流式排列，
       宽度跟随内容区，小窗口不溢出）。 */
    {
        int w = contentWidth - 24;
        int innerW;
        XRect inner;
        if (w < 120) w = 120;
        XWidget_setGeometry((XWidget*)&self->m_groupBox,
                            12, 8, w, 210);
        inner = XGroupBox_contentsRect(&self->m_groupBox);
        innerW = inner.width - 24;
        if (innerW < 80) innerW = 80;
        XWidget_setGeometry((XWidget*)&self->m_lineEdit,
                            inner.x + 12, inner.y + 8, innerW, 26);
        XWidget_setGeometry((XWidget*)&self->m_spinBox,
                            inner.x + 12, inner.y + 48, innerW, 26);
        XWidget_setGeometry((XWidget*)&self->m_slider,
                            inner.x + 12, inner.y + 92, innerW, 28);
        XWidget_setGeometry((XWidget*)&self->m_progressBar,
                            inner.x + 12, inner.y + 138, innerW, 24);
        XWidget_setGeometry((XWidget*)&self->m_inputStatus,
                            12, 8 + 210 + 8, contentWidth - 12, 24);
    }
#if XWIDGET_ON && XTABWIDGET_ON && XTABBAR_ON && XCOMBOBOX_ON && \
    XABSTRACTSLIDER_ON && XDIAL_ON && XPROGRESSBAR_ON
    {
        int w5 = contentWidth - 24;
        if (w5 < 120) w5 = 120;
        XWidget_setGeometry((XWidget*)&self->m_tabWidget,
                            12, 8, w5, contentHeight - 40);
        XWidget_setGeometry((XWidget*)&self->m_inputStatus,
                            12, 8 + contentHeight - 40 + 8,
                            contentWidth, 24);
    }
#endif
}

/** @brief 切换主内容页面：更新堆叠布局当前页、重新分配几何并更新状态栏。 */
static void demo_switchPage(DemoWin* self, int index)
{
    if (!self) return;
    if (index < 0) index = 0;
    if (index > 4) index = 4;
    XStackedLayout_setCurrentIndex(&self->m_stackLayout, index);
    /* XStackedLayout 的 setGeometry 只给当前页面分配几何；切换后必须
       重新分配，否则新页面容器保持 0x0 导致页面内容不可见。 */
    demo_layout_content(self);
    XPrintf("XGuiWindowDemo: switch page=%d (%s)\n", index,
            demo_page_name(index));
    demo_set_status(self, demo_page_name(index));
}
#endif /* XWIDGET_ON && XLAYOUT_ON && XLAYOUT_STACKED_ON */

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
#endif /* XWIDGET_ON && XPUSHBUTTON_ON && XLAYOUT_ON && XLAYOUT_STACKED_ON */

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
    demo_layout_chrome(demo);
#endif
#if XWIDGET_ON && XLAYOUT_ON && XLAYOUT_STACKED_ON
    demo_layout_content(demo);
#endif
#if XGUI_PERFORMANCE_OVERLAY_ON && XFRAME_ON && XLABEL_ON
    if (XPerformanceOverlay_isFixed(&demo->m_performanceOverlay)) {
        XPerformanceOverlay_setPresetPosition(
            &demo->m_performanceOverlay, XPerformanceOverlayPosition_BottomRight,
            XWidget_width(self), XWidget_height(self), 26);
    }
#endif
    demo_repaint(demo);
}

/** @brief PaintEvent：根控件背景与性能悬浮层；子控件由 XWidget 自动递归绘制。 */
static void VDemoWin_paintEvent(XWidget* self, XEvent* event)
{
#if XGUI_PERFORMANCE_OVERLAY_ON && XFRAME_ON && XLABEL_ON
    DemoWin* demo = (DemoWin*)self;
    int64_t frameStartUsecs = demo_monotonicUsecs();
#endif
    demo_paintScene((DemoWin*)self, event);
#if XGUI_PERFORMANCE_OVERLAY_ON && XFRAME_ON && XLABEL_ON
    {
        int64_t frameEndUsecs = demo_monotonicUsecs();
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
#endif /* XGUI_PERFORMANCE_OVERLAY_ON && XFRAME_ON && XLABEL_ON */
}

/** @brief KeyPressEvent：打印键码/修饰键/自动重复（真实输入闭环验证）。 */
static void VDemoWin_keyPressEvent(XWidget* self, XEvent* event)
{
    XKeyEvent* key = (XKeyEvent*)event;
    (void)self;
    if (!key) return;
    printf("XGuiWindowDemo: keyPress key=%d modifiers=0x%x autoRepeat=%d\n",
           XKeyEvent_key(key), (unsigned)XKeyEvent_modifiers(key),
           (int)XKeyEvent_autoRepeat(key));
}

/** @brief KeyReleaseEvent：打印释放键码。 */
static void VDemoWin_keyReleaseEvent(XWidget* self, XEvent* event)
{
    XKeyEvent* key = (XKeyEvent*)event;
    (void)self;
    if (!key) return;
    printf("XGuiWindowDemo: keyRelease key=%d modifiers=0x%x\n",
           XKeyEvent_key(key), (unsigned)XKeyEvent_modifiers(key));
}

/** @brief MousePressEvent：背景区域处理性能悬浮层；子控件由自身接收事件。 */
static void VDemoWin_mousePressEvent(XWidget* self, XEvent* event)
{
    DemoWin* demo = (DemoWin*)self;
    XMouseEvent* mouse = (XMouseEvent*)event;
    if (!mouse) return;
    printf("XGuiWindowDemo: mousePress button=%d buttons=0x%x pos=(%d,%d)\n",
           (int)XMouseEvent_button(mouse), (unsigned)XMouseEvent_buttons(mouse),
           (int)XMouseEvent_position(mouse).x, (int)XMouseEvent_position(mouse).y);
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
                (void)XPerformanceOverlay_beginDrag(
                    &demo->m_performanceOverlay, position.x, position.y);
            }
            XEvent_accept(event);
        }
    }
#endif
}

/** @brief MouseReleaseEvent：结束性能悬浮层拖动。 */
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
    (void)self;
    if (!mouse) return;
    printf("XGuiWindowDemo: mouseDoubleClick button=%d pos=(%d,%d)\n",
           (int)XMouseEvent_button(mouse),
           (int)XMouseEvent_position(mouse).x, (int)XMouseEvent_position(mouse).y);
}

/** @brief MouseMoveEvent：拖动性能悬浮层时更新其位置。 */
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
    printf("XGuiWindowDemo: wheel delta=(%d,%d) pos=(%d,%d)\n",
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
    printf("XGuiWindowDemo: enter pos=(%d,%d) global=(%d,%d)\n",
           (int)XEnterEvent_position(enter).x, (int)XEnterEvent_position(enter).y,
           (int)global.x, (int)global.y);
}

/** @brief LeaveEvent：打印离开通知。 */
static void VDemoWin_leaveEvent(XWidget* self, XEvent* event)
{
    (void)self;
    (void)event;
    printf("XGuiWindowDemo: leave\n");
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
#if XGUI_PERFORMANCE_OVERLAY_ON && XWIDGET_ON && XFRAME_ON && XLABEL_ON
    demo_performance_init(self);
#endif
#if XWIDGET_ON && XFRAME_ON && XLABEL_ON
    /* 顶部标题栏文本（深蓝背景由静态场景绘制，白字覆盖其上）。 */
    XLabel_init(&self->m_titleLabel, &self->m_base, 0);
    demo_set_widget_default_font((XWidget*)&self->m_titleLabel);
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
#endif
#if XWIDGET_ON && XPUSHBUTTON_ON && XLAYOUT_ON && XLAYOUT_STACKED_ON
    /* 页面切换导航按钮（标题栏下方一行）。 */
    {
        static const char* const kNavTexts[5] = {
            "\xE6\x8C\x89\xE9\x92\xAE\xE6\xBC\x94\xE7\xA4\xBA", /* 按钮演示 */
            "\xE9\x80\x89\xE6\x8B\xA9\xE6\xBC\x94\xE7\xA4\xBA", /* 选择演示 */
            "\xE5\xA0\x86\xE5\x8F\xA0\xE6\xBC\x94\xE7\xA4\xBA", /* 堆叠演示 */
            "\xE8\xBE\x93\xE5\x85\xA5\xE6\xBC\x94\xE7\xA4\xBA", /* 输入演示 */
            "\xE9\x80\x89\xE9\xA1\xB9\xE5\x8D\xA1\xE6\xBC\x94\xE7\xA4\xBA"  /* 选项卡演示 */
        };
        static void (*const kNavSlots[5])(XObject*, XVarList*) = {
            demo_nav0Slot, demo_nav1Slot, demo_nav2Slot, demo_nav3Slot,
            demo_nav4Slot
        };
        int nav;
        for (nav = 0; nav < 5; ++nav) {
            XPushButton* button = &self->m_pageNav[nav];
            XPushButton_init(button, &self->m_base, 0);
            demo_set_widget_default_font((XWidget*)button);
            XPushButton_setText_2(button, kNavTexts[nav]);
            XWidget_setGeometry((XWidget*)button, 12 + nav * 104, 44, 96, 26);
            XObject_connect_1((XObject*)button,
                              (size_t)XPushButton_clicked_signal(NULL, false),
                              (XObject*)self, kNavSlots[nav],
                              XConnectionType_Direct);
            XWidget_show((XWidget*)button);
        }
    }
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
    XObject_connect_1((XObject*)&self->m_spinBox,
                      (size_t)XSpinBox_valueChanged_signal(&self->m_spinBox),
                      (XObject*)self, demo_input_spinChangedSlot,
                      XConnectionType_Direct);

    XSlider_init(&self->m_slider, (XWidget*)&self->m_groupBox, 0);
    demo_set_widget_default_font((XWidget*)&self->m_slider);
    XAbstractSlider_setRange((XAbstractSlider*)&self->m_slider, 0, 100);
    XObject_connect_1((XObject*)&self->m_slider,
                      (size_t)XSlider_valueChanged_signal(&self->m_slider),
                      (XObject*)self, demo_input_sliderChangedSlot,
                      XConnectionType_Direct);

    XProgressBar_init(&self->m_progressBar, (XWidget*)&self->m_groupBox, 0);
    demo_set_widget_default_font((XWidget*)&self->m_progressBar);
    XProgressBar_setRange(&self->m_progressBar, 0, 100);
    XProgressBar_setValue(&self->m_progressBar, 30);

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
#endif
#if XWIDGET_ON && XTABWIDGET_ON && XTABBAR_ON && XCOMBOBOX_ON && \
    XABSTRACTSLIDER_ON && XDIAL_ON && XPROGRESSBAR_ON && XFRAME_ON && XLABEL_ON
    /* ---- 页面 5：选项卡演示（TabWidget 内嵌 ComboBox/Dial/Progress） ---- */
    XTabWidget_init(&self->m_tabWidget, (XWidget*)&self->m_pageTabs, 0);
    demo_set_widget_default_font((XWidget*)&self->m_tabWidget);
    {
        /* 页一：下拉框。 */
        XComboBox_init(&self->m_comboBox, (XWidget*)&self->m_tabWidget, 0);
        demo_set_widget_default_font((XWidget*)&self->m_comboBox);
        XComboBox_addItem(&self->m_comboBox, "Option 1");
        XComboBox_addItem(&self->m_comboBox, "Option 2");
        XComboBox_addItem(&self->m_comboBox, "Option 3");
        XComboBox_setCurrentIndex(&self->m_comboBox, 0);
        XObject_connect_1((XObject*)&self->m_comboBox,
                          (size_t)XComboBox_currentTextChanged_signal(
                              &self->m_comboBox),
                          (XObject*)self, demo_tab_comboSlot,
                          XConnectionType_Direct);
        (void)XTabWidget_insertTab(&self->m_tabWidget, 0,
                                   (XWidget*)&self->m_comboBox,
                                   "\xE4\xB8\x8B\xE6\x8B\x89"); /* 下拉 */
    }
    {
        /* 页二：旋钮 + 联动进度条。 */
        XWidget* page = (XWidget*)XMemory_malloc(sizeof(XWidget),
                                                 XCLASS_DEFAULT_MEMORY_TYPE);
        if (page) {
            XWidget_init(page, (XWidget*)&self->m_tabWidget, 0);
            Set_Class_Memory(page, XCLASS_DEFAULT_MEMORY_TYPE);
            Set_Class_IsHeap(page, true);
            XDial_init(&self->m_dial, page, 0);
            demo_set_widget_default_font((XWidget*)&self->m_dial);
            XAbstractSlider_setRange((XAbstractSlider*)&self->m_dial, 0, 100);
            XAbstractSlider_setValue((XAbstractSlider*)&self->m_dial, 40);
            XObject_connect_1((XObject*)&self->m_dial,
                              (size_t)XDial_valueChanged_signal(&self->m_dial),
                              (XObject*)self, demo_tab_dialSlot,
                              XConnectionType_Direct);
            XProgressBar_init(&self->m_dialProgress, page, 0);
            demo_set_widget_default_font((XWidget*)&self->m_dialProgress);
            XProgressBar_setRange(&self->m_dialProgress, 0, 100);
            XProgressBar_setValue(&self->m_dialProgress, 40);
            XWidget_show(page);
            (void)XTabWidget_insertTab(&self->m_tabWidget, 1, page,
                                       "\xE6\x97\x8B\xE9\x92\xAE"); /* 旋钮 */
        }
    }
    XLabel_init(&self->m_tabStatus, (XWidget*)&self->m_pageTabs, 0);
    demo_set_widget_default_font((XWidget*)&self->m_tabStatus);
    XLabel_setText_2(&self->m_tabStatus, "\xE5\xB0\xB1\xE7\xBB\xAA"); /* 就绪 */
    XWidget_show((XWidget*)&self->m_tabWidget);
    XWidget_show((XWidget*)&self->m_inputStatus);
#endif
#endif
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
    /* 主内容区几何（标题栏 40 + 导航按钮 34 之后）与内层堆叠几何。 */
    {
        XRect content;
        XRect inner;
        XRect_init(&content, 12, 78, 496, 252);
        XLayoutItem_setGeometry_base((XLayoutItem*)&self->m_stackLayout,
                                     &content);
        XRect_init(&inner, 40, 44, 320, 110);
        XLayoutItem_setGeometry_base((XLayoutItem*)&self->m_stackLayoutInner,
                                     &inner);
    }
    XStackedLayout_setCurrentIndex(&self->m_stackLayout, 0);
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

/* ==================== 主函数 ==================== */

int main(int argc, char* argv[])
{
    XGuiApplication* app;
    DemoWin* win;
    int autoSeconds;
    int benchmarkSeconds;
    bool benchmarkResize;
    const char* screenshotPath;
    int screenshotPage;
    bool autoTest;
    int argi;
    int eventLoopResult;

    autoSeconds = 0;
    benchmarkSeconds = 0;
    benchmarkResize = false;
    screenshotPath = NULL;
    autoTest = false;
    screenshotPage = 0;
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
        else if (strcmp(argv[argi], "--screenshot") == 0 &&
                 argi + 1 < argc) {
            screenshotPath = argv[++argi];
        }
        else if (strcmp(argv[argi], "--page") == 0 && argi + 1 < argc) {
            screenshotPage = atoi(argv[++argi]);
        }
        else if (strcmp(argv[argi], "--autotest") == 0) {
            autoTest = true;
        }
        else {
            autoSeconds = atoi(argv[argi]);
        }
    }
    if (autoSeconds < 0) autoSeconds = 0;
    if (benchmarkSeconds < 0) benchmarkSeconds = 0;

    /* 1) 初始化 XGuiApplication（进程内单例）。 */
    app = XGuiApplication_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, argc, argv);
    if (!app) {
        XPrintf("XGuiWindowDemo: XGuiApplication_create_ex 失败\n");
        return 1;
    }

    /* 2) 创建演示窗口并设置标题/几何。show() 会在框架内部惰性创建平台窗口。 */
    win = DemoWin_create();
    if (!win) {
        XPrintf("XGuiWindowDemo: DemoWin_create 失败\n");
        XGuiApplication_delete_base(app);
        return 1;
    }
    win->m_screenshotPath = screenshotPath;
    win->m_screenshotFrames = 0;
    win->m_autoTest = autoTest;
    win->m_autoTestFrames = 0;
    if (autoTest)
        demo_switchPage(win, 3); /* 自动测试固定切第 4 页 */
#if XWIDGET_ON && XLAYOUT_ON && XLAYOUT_STACKED_ON
    /* 截图模式可指定初始页面（配合 --screenshot <file> --page <N>）。 */
    if (screenshotPage > 0)
        demo_switchPage(win, screenshotPage); /* 无需截图模式也能切页。 */
#endif
    {
        XString* title = XString_create_utf8(
            "XinYueC 控件可视化测试");
        XWidget_setWindowTitle(&win->m_base, title);
        XString_delete_base((XClass*)title);
    }
    XWidget_setGeometry(&win->m_base, 60, 60, 520, 360);
#if XWIDGET_ON && XFRAME_ON && XLABEL_ON
    demo_layout_chrome(win);
#endif

    /* 3) 显示窗口：触发框架内部的惰性平台窗口创建并进入事件循环。 */
    XWidget_showNormal(&win->m_base);
    XPrintf("XGuiWindowDemo: 屏幕尺寸=%.0fx%.0f\n",
           (double)XWidget_width(&win->m_base),
           (double)XWidget_height(&win->m_base));

    eventLoopResult = 0;
    if (benchmarkSeconds > 0) {
        demo_runFrameBenchmark(win, benchmarkSeconds, benchmarkResize);
    } else {
        /* 把刷新注册到事件分发器轮询链：每次 processEvents 回调一次，
           不再受 1ms 精确定时器粒度限制；事件循环因此也始终有事件可
           处理，不会进入无事件休眠。 */
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
        if (eventLoopResult == 0)
            eventLoopResult = XGuiApplication_exec();
    }

    XPrintf("XGuiWindowDemo: 退出事件循环（关闭=%s 自动退出=%d 返回=%d）\n",
           win->m_closed ? "是" : "否", autoSeconds, eventLoopResult);

    /* 4) 清理：窗口销毁自动拆除原生窗口；应用单例回收集成层。 */
    demo_stopTimers(win);
#if XWIDGET_ON && XFRAME_ON && XLABEL_ON
    XLabel_deinit_base((XLabel*)&win->m_statusLabel);
    XLabel_deinit_base(&win->m_titleLabel);
#endif
#if XWIDGET_ON && XPUSHBUTTON_ON
    XPushButton_deinit_base(&win->m_button);
#endif
#if XWIDGET_ON && XABSTRACTBUTTON_ON && XPUSHBUTTON_ON && XCOMMANDLINKBUTTON_ON
    XCommandLinkButton_deinit_base(&win->m_commandLink);
#endif
#if XWIDGET_ON && XABSTRACTBUTTON_ON && XTOOLBUTTON_ON && XMENU_ON
    /* 先解绑工具按钮（断开与默认动作/菜单的连接），再释放菜单与动作。 */
    XToolButton_deinit_base(&win->m_toolButton);
    XMenu_deinit_base(&win->m_toolMenu);
    XAction_deinit_base(&win->m_toolAction);
#endif
#if XWIDGET_ON && XFRAME_ON && XLABEL_ON
    XLabel_deinit_base(&win->m_linkLabel);
#endif
#if XWIDGET_ON && XABSTRACTBUTTON_ON && XCHECKBOX_ON
    XCheckBox_deinit_base(&win->m_checkBox);
#endif
#if XWIDGET_ON && XABSTRACTBUTTON_ON && XRADIOBUTTON_ON
    XRadioButton_deinit_base(&win->m_radioA);
    XRadioButton_deinit_base(&win->m_radioB);
#endif
#if XWIDGET_ON && XFRAME_ON && XLABEL_ON
    XLabel_deinit_base(&win->m_choiceLabel);
#endif
#if XWIDGET_ON && XFRAME_ON && XLABEL_ON && XLAYOUT_ON && XLAYOUT_STACKED_ON
#if XPUSHBUTTON_ON
    XPushButton_deinit_base(&win->m_stackPrevButton);
    XPushButton_deinit_base(&win->m_stackNextButton);
#endif
    XLabel_deinit_base(&win->m_stackPageOne);
    XLabel_deinit_base(&win->m_stackPageTwo);
    XStackedLayout_deinit_base(&win->m_stackLayoutInner);
#endif
#if XWIDGET_ON && XLAYOUT_ON && XLAYOUT_STACKED_ON
    XWidget_deinit_base(&win->m_pageStacked);
    XWidget_deinit_base(&win->m_pageChoices);
#if XWIDGET_ON && XGROUPBOX_ON && XLINEEDIT_ON && XSPINBOX_ON && \
    XABSTRACTSLIDER_ON && XSLIDER_ON && XPROGRESSBAR_ON
    XWidget_deinit_base(&win->m_inputStatus);
    XWidget_deinit_base((XWidget*)&win->m_progressBar);
    XWidget_deinit_base((XWidget*)&win->m_slider);
    XWidget_deinit_base((XWidget*)&win->m_spinBox);
    XWidget_deinit_base((XWidget*)&win->m_lineEdit);
    XWidget_deinit_base((XWidget*)&win->m_groupBox);
#endif
#if XWIDGET_ON && XLAYOUT_ON && XLAYOUT_STACKED_ON
    XWidget_deinit_base(&win->m_pageInputs);
    XWidget_deinit_base(&win->m_pageTabs);
#endif
    XWidget_deinit_base(&win->m_pageButtons);
    XStackedLayout_deinit_base(&win->m_stackLayout);
#endif
#if XWIDGET_ON && XPUSHBUTTON_ON && XLAYOUT_ON && XLAYOUT_STACKED_ON
    {
        int nav;
        for (nav = 0; nav < 3; ++nav)
            XPushButton_deinit_base(&win->m_pageNav[nav]);
    }
#endif
#if XGUI_PERFORMANCE_OVERLAY_ON && XWIDGET_ON && XFRAME_ON && XLABEL_ON
    demo_performance_deinit(win);
#endif
#if XGUI_DEMO_STATIC_SCENE_CACHE_ON
    XImage_deinit_base(&win->m_staticScene);
#endif
    XWidget_delete_base((XClass*)win);
    XGuiApplication_delete_base(app);
    XPrintf("XGuiWindowDemo: 已退出\n");
    return eventLoopResult;
}

#else /* 开关裁剪 */
int main(int argc, char* argv[])
{
    (void)argc;
    (void)argv;
    XPrintf("XGuiWindowDemo: 本演示需要 XGUIAPPLICATION_ON / XWINDOW_ON / "
                    "XBACKINGSTORE_ON / XPLATFORMINTEGRATION_ON / "
                    "XPLATFORMNATIVEWINDOW_ON。\n");
    return 2;
}
#endif /* 开关 */
#endif /* 补齐编译器报告的未闭合 #if（嵌套层级审计确认差 1） */
