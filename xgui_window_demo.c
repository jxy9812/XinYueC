/******************************************************************************
 * @file       xgui_window_demo.c
 * @brief      XGui GUI 控件统一可视化测试程序（Linux X11 / Windows Win32）。
 * @details    本程序是 GUI 控件的人工可视化验收入口，演示 XGui 完整窗口链路：
 *             - XGuiApplication_create_ex 初始化应用单例；
 *             - 经平台原生接口拿到 XPlatformIntegration；
 *             - XPlatformIntegration_createPlatformWindow 挂接原生窗口
 *               （X11 Window / Win32 HWND，XWindow_createHandle 惰性创建）；
 *             - XBackingStore 离屏缓冲 + XPainter 软件光栅化绘制；
 *             - XWindowSystemInterface 事件注入（Expose/Resize/Close）；
 *             - XGuiApplication_waitForEvents + processEvents 事件泵；
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
#include <string.h>

#include "CXinYueConfig.h"
#include "XPrintf.h"
#include "XObject.h"
#include "XEvent.h"
#include "XDateTime.h"
#include "XGuiApplication.h"
#include "XPlatformNativeInterface.h"
#include "XPlatformIntegration.h"
#include "XWindow.h"
#include "XWindowEvent.h"
#include "XBackingStore.h"
#include "XImage.h"
#include "XPainter.h"
#include "XLabel.h"
#if XWIDGET_ON && XPUSHBUTTON_ON
#include "XPushButton.h"
#endif
#if XWIDGET_ON && XFRAME_ON && XLABEL_ON && XLAYOUT_ON && XLAYOUT_STACKED_ON
#include "XStackedLayout.h"
#endif

#if XGUIAPPLICATION_ON && XWINDOW_ON && XBACKINGSTORE_ON && \
    XPLATFORMINTEGRATION_ON && XPLATFORMNATIVEWINDOW_ON

/* ==================== 演示窗口子类 ==================== */

XCLASS_DEFINE_BEGING(DemoWin)
XCLASS_DEFINE_EXTEND_END(DemoWin, XWindow)

/** @brief 演示窗口：持有后备存储引用与关闭标志。 */
typedef struct DemoWin
{
    XWindow         m_base;  /**< 基类；必须是第一个成员。 */
    XBackingStore*  m_store; /**< 后备存储借用指针（随窗口生命周期）。 */
    bool            m_closed; /**< CloseEvent 被接受后置真，驱动主循环退出。 */
    bool            m_buttonDown; /**< XPushButton 当前是否处于按下状态。 */
#if XWIDGET_ON && XPUSHBUTTON_ON
    XPushButton     m_button; /**< 常驻按钮控件：输入与信号都走控件自身。 */
#endif
#if XWIDGET_ON && XFRAME_ON && XLABEL_ON
    XLabel          m_linkLabel; /**< 常驻联动标签：按下/松开文本由按钮信号槽更新。 */
#endif
#if XWIDGET_ON && XFRAME_ON && XLABEL_ON && XLAYOUT_ON && XLAYOUT_STACKED_ON
    XStackedLayout  m_stackLayout; /**< 堆叠页面布局：展示 StackOne 页面切换策略。 */
    XLabel          m_stackPageOne; /**< 堆叠布局第一个页面控件。 */
    XLabel          m_stackPageTwo; /**< 堆叠布局第二个页面控件。 */
#if XPUSHBUTTON_ON
    XPushButton     m_stackPrevButton; /**< 堆叠布局上一页按钮。 */
    XPushButton     m_stackNextButton; /**< 堆叠布局下一页按钮。 */
    bool            m_stackPrevDown; /**< 上一页按钮当前是否处于按下状态。 */
    bool            m_stackNextDown; /**< 下一页按钮当前是否处于按下状态。 */
#endif
#endif
} DemoWin;

/* ---------------- 绘制 ----------------
 * 全部使用 ARGB32 预乘颜色：软件光栅化 + XBackingStore 上屏，
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
 * @param family 点阵字库 family；NULL 使用当前默认字库。
 */
static void demo_draw_label(XPainter* painter, int x, int y, int width,
                            int height, const char* text, int pixelSize,
                            const char* family)
{
    XLabel label;
    if (!painter || width <= 0 || height <= 0) return;
    memset(&label, 0, sizeof(label));
    XLabel_init(&label, NULL, 0);
    if (family)
    {
        XFont labelFont = XWidget_font((XWidget*)&label);
        XFont_setFamily(&labelFont, family);
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

/** @brief 重绘整个窗口：resize 后备存储 -> 绘制 -> flush 提交原生窗口。 */
static void demo_repaint(DemoWin* self)
{
    XBackingStore* store;
    XImage* dev;
    XPainter painter;
    XRegion region;
    XRect rc;
    XPoint offset;
    XSize size;
    int w;
    int h;
    if (!self) return;
    w = XWindow_width((XWindow*)self);
    h = XWindow_height((XWindow*)self);
    if (w <= 0 || h <= 0) return;
    store = self->m_store;
    if (!store) return;

    /* 1) 按窗口客户区尺寸调整后备缓冲（尺寸未变时内部为 no-op）。 */
    XSize_init(&size, w, h);
    XBackingStore_resize(store, &size);

    /* 2) 进入绘制区间，把 XPainter 绑定到后备缓冲的 XImage 上。 */
    XRegion_init(&region);
    XRect_init(&rc, 0, 0, w, h);
    XRegion_addRect(&region, &rc);
    XBackingStore_beginPaint(store, &region);
    XPainter_init(&painter, NULL);
    dev = XBackingStore_paintDevice(store);
    if (dev && XPainter_begin_image(&painter, dev)) {
        /* 背景。 */
        demo_fill_rect(&painter, 0, 0, w, h, 0xfff4f6f8u);
        /* 顶部标题栏条。 */
        demo_fill_rect(&painter, 0, 0, w, 40, 0xff1f4e79u);
        /* 中部棋盘格纹理。 */
        demo_draw_checker(&painter, 20, 60, 8, 5, 24);
        /* GUI 控件可视化测试面板：同一文字的原始字号与两倍字号。 */
        demo_fill_rect(&painter, 230, 60, 270, 100, 0xffdcefe2u);
#if XWIDGET_ON && XFRAME_ON && XLABEL_ON
        demo_draw_label(&painter, 246, 70, 238, 20,
                        "XLabel 1x \xE4\xB8\xAD\xE6\x96\x87\xE6\xB5\x8B\xE8\xAF\x95", 16,
                        "XFont8x16");
        demo_draw_label(&painter, 246, 106, 238, 40,
                        "XLabel 32px \xE4\xB8\xAD\xE6\x96\x87", 40,
                        "XFont32x32");
#else
        /* 组件被嵌入式配置裁剪时保留面板，便于确认裁剪后的可视状态。 */
        XPainter_drawText(&painter, 246, 92, "XLabel disabled", 0xff202020u);
#endif /* XWIDGET_ON && XFRAME_ON && XLABEL_ON */
        /* XPushButton 可视化面板：常驻按钮 + 联动标签，按下/松开通过信号槽切换。 */
        demo_fill_rect(&painter, 230, 180, 270, 100, 0xffe6eef7u);
#if XWIDGET_ON && XPUSHBUTTON_ON
        {
            XRect geo = XWidget_geometry((XWidget*)&self->m_button);
            if (XPainter_save(&painter)) {
                XPainter_translate(&painter, (float)geo.x, (float)geo.y);
                XPushButton_drawContents(&self->m_button, &painter);
                XPainter_restore(&painter);
            }
        }
#if XWIDGET_ON && XFRAME_ON && XLABEL_ON
        {
            XRect geo = XWidget_geometry((XWidget*)&self->m_linkLabel);
            if (XPainter_save(&painter)) {
                XPainter_translate(&painter, (float)geo.x, (float)geo.y);
                XLabel_drawContents(&self->m_linkLabel, &painter);
                XPainter_restore(&painter);
            }
        }
#else
        XPainter_drawText(&painter, 382, 204,
                          self->m_buttonDown ? "Pressed" : "Released",
                          0xff202020u);
#endif /* XWIDGET_ON && XFRAME_ON && XLABEL_ON */
#else
        XPainter_drawText(&painter, 246, 212, "XPushButton disabled",
                          0xff202020u);
#endif /* XWIDGET_ON && XPUSHBUTTON_ON */
#if XWIDGET_ON && XFRAME_ON && XLABEL_ON && XLAYOUT_ON && XLAYOUT_STACKED_ON
        /* 堆叠布局面板：页面控件由同一窗口父对象管理，布局只分配页面几何。 */
        demo_fill_rect(&painter, 20, 180, 190, 140, 0xffffedcfu);
        XPainter_drawText(&painter, 32, 202, "XStackedLayout", 0xff202020u);
        {
            XRect stackRect;
            XWidget* current;
            XRect geo;
            XRect_init(&stackRect, 32, 220, 166, 48);
            demo_fill_rect(&painter, stackRect.x, stackRect.y,
                           stackRect.width, stackRect.height, 0xffffffffu);
            XLayoutItem_setGeometry_base((XLayoutItem*)&self->m_stackLayout,
                                          &stackRect);
            current = XStackedLayout_currentWidget(&self->m_stackLayout);
            if (current) {
                geo = XWidget_geometry(current);
                if (XPainter_save(&painter)) {
                    XPainter_translate(&painter, (float)geo.x, (float)geo.y);
                    XLabel_drawContents((XLabel*)current, &painter);
                    XPainter_restore(&painter);
                }
            }
        }
#if XPUSHBUTTON_ON
        {
            XRect geo;
            geo = XWidget_geometry((XWidget*)&self->m_stackPrevButton);
            if (XPainter_save(&painter)) {
                XPainter_translate(&painter, (float)geo.x, (float)geo.y);
                XPushButton_drawContents(&self->m_stackPrevButton, &painter);
                XPainter_restore(&painter);
            }
            geo = XWidget_geometry((XWidget*)&self->m_stackNextButton);
            if (XPainter_save(&painter)) {
                XPainter_translate(&painter, (float)geo.x, (float)geo.y);
                XPushButton_drawContents(&self->m_stackNextButton, &painter);
                XPainter_restore(&painter);
            }
        }
#endif /* XPUSHBUTTON_ON */
#else
        /* 堆叠布局被裁剪时保留面板占位，便于截图确认裁剪结果。 */
        demo_fill_rect(&painter, 20, 180, 190, 100, 0xffffedcfu);
        XPainter_drawText(&painter, 32, 232, "XStackedLayout disabled",
                          0xff202020u);
#endif /* XWIDGET_ON && XFRAME_ON && XLABEL_ON && XLAYOUT_ON && XLAYOUT_STACKED_ON */
        /* 状态栏。 */
        demo_fill_rect(&painter, 0, h - 28, w, 28, 0xff3a3a3au);
        XPainter_end(&painter);
    }
    XBackingStore_endPaint(store);

    /* 3) 把整窗脏区提交到原生窗口（X11 XPutImage / Win32 BitBlt）。 */
    XPoint_init(&offset, 0, 0);
    XBackingStore_flush(store, &region, (XWindow*)self, &offset);
    XRegion_deinit(&region);
}

/* ==================== 信号槽 ==================== */

#if XWIDGET_ON && XPUSHBUTTON_ON
/** @brief 按钮 pressed 信号槽：置按下状态并更新联动标签。 */
static void demo_button_pressedSlot(XObject* receiver, XVarList* args)
{
    DemoWin* self = (DemoWin*)receiver;
    (void)args;
    if (!self) return;
    self->m_buttonDown = true;
#if XWIDGET_ON && XFRAME_ON && XLABEL_ON
    XLabel_setText_2(&self->m_linkLabel, "Pressed");
#endif
    demo_repaint(self);
}

/** @brief 按钮 released 信号槽：清除按下状态并更新联动标签。 */
static void demo_button_releasedSlot(XObject* receiver, XVarList* args)
{
    DemoWin* self = (DemoWin*)receiver;
    (void)args;
    if (!self) return;
    self->m_buttonDown = false;
#if XWIDGET_ON && XFRAME_ON && XLABEL_ON
    XLabel_setText_2(&self->m_linkLabel, "Released");
#endif
    demo_repaint(self);
}

#if XFRAME_ON && XLABEL_ON && XLAYOUT_ON && XLAYOUT_STACKED_ON
/** @brief 上一页按钮 clicked 槽：循环切换到堆叠布局的上一页。 */
static void demo_stack_prev_clickedSlot(XObject* receiver, XVarList* args)
{
    DemoWin* self = (DemoWin*)receiver;
    int index;
    int count;
    (void)args;
    if (!self) return;
    index = XStackedLayout_currentIndex(&self->m_stackLayout);
    count = XStackedLayout_count(&self->m_stackLayout);
    if (count <= 0) return;
    if (index <= 0) index = count - 1;
    else --index;
    XStackedLayout_setCurrentIndex(&self->m_stackLayout, index);
    XPrintf("XGuiWindowDemo: stacked page=%d (prev)\n", index);
    demo_repaint(self);
}

/** @brief 下一页按钮 clicked 槽：循环切换到堆叠布局的下一页。 */
static void demo_stack_next_clickedSlot(XObject* receiver, XVarList* args)
{
    DemoWin* self = (DemoWin*)receiver;
    int index;
    int count;
    (void)args;
    if (!self) return;
    index = XStackedLayout_currentIndex(&self->m_stackLayout);
    count = XStackedLayout_count(&self->m_stackLayout);
    if (count <= 0) return;
    index = (index + 1) % count;
    XStackedLayout_setCurrentIndex(&self->m_stackLayout, index);
    XPrintf("XGuiWindowDemo: stacked page=%d (next)\n", index);
    demo_repaint(self);
}
#endif /* XFRAME_ON && XLABEL_ON && XLAYOUT_ON && XLAYOUT_STACKED_ON */
#endif /* XWIDGET_ON && XPUSHBUTTON_ON */

/* ==================== 事件槽重载 ==================== */

/** @brief CloseEvent：接受关闭并置标志，主循环随后退出。 */
static void VDemoWin_closeEvent(XWindow* self, XEvent* event)
{
    /* 注意：XWindow_xxxEvent_base 在框架里是虚派发（会再次进入本覆写），
       必须用 XClass_Parent 显式调用父类默认实现，避免无限递归。 */
    XClass_Parent(XWindow, EXWindow_CloseEvent, XWindowEventSlot)(self, event);
    ((DemoWin*)self)->m_closed = true;
    if (event) XEvent_accept(event);
}

/** @brief ExposeEvent：区域可见，整窗重绘。 */
static void VDemoWin_exposeEvent(XWindow* self, XEvent* event)
{
    XClass_Parent(XWindow, EXWindow_ExposeEvent, XWindowEventSlot)(self, event);
    demo_repaint((DemoWin*)self);
}

/** @brief ResizeEvent：尺寸变化，整窗重绘。 */
static void VDemoWin_resizeEvent(XWindow* self, XEvent* event)
{
    XClass_Parent(XWindow, EXWindow_ResizeEvent, XWindowEventSlot)(self, event);
    demo_repaint((DemoWin*)self);
}

/** @brief PaintEvent：显式绘制请求（本演示与 Expose 同路整窗刷新）。 */
static void VDemoWin_paintEvent(XWindow* self, XEvent* event)
{
    XClass_Parent(XWindow, EXWindow_PaintEvent, XWindowEventSlot)(self, event);
    demo_repaint((DemoWin*)self);
}

/** @brief KeyPressEvent：打印键码/修饰键/自动重复（真实输入闭环验证）。 */
static void VDemoWin_keyPressEvent(XWindow* self, XEvent* event)
{
    XKeyEvent* ke;
    XClass_Parent(XWindow, EXWindow_KeyPressEvent, XWindowEventSlot)(self, event);
    ke = (XKeyEvent*)event;
    printf("XGuiWindowDemo: keyPress key=%d modifiers=0x%x autoRepeat=%d\n",
           XKeyEvent_key(ke), (unsigned)XKeyEvent_modifiers(ke),
           (int)XKeyEvent_autoRepeat(ke));
}

/** @brief KeyReleaseEvent：打印释放键码。 */
static void VDemoWin_keyReleaseEvent(XWindow* self, XEvent* event)
{
    XKeyEvent* ke;
    XClass_Parent(XWindow, EXWindow_KeyReleaseEvent, XWindowEventSlot)(self, event);
    ke = (XKeyEvent*)event;
    printf("XGuiWindowDemo: keyRelease key=%d modifiers=0x%x\n",
           XKeyEvent_key(ke), (unsigned)XKeyEvent_modifiers(ke));
}

/** @brief MousePressEvent：打印触发键/按下集合/坐标。 */
static void VDemoWin_mousePressEvent(XWindow* self, XEvent* event)
{
    XMouseEvent* me;
#if XWIDGET_ON && XPUSHBUTTON_ON
    XRect geo;
    XPoint pos;
#endif
    XClass_Parent(XWindow, EXWindow_MousePressEvent, XWindowEventSlot)(self, event);
    me = (XMouseEvent*)event;
    printf("XGuiWindowDemo: mousePress button=%d buttons=0x%x pos=(%d,%d)\n",
           (int)XMouseEvent_button(me), (unsigned)XMouseEvent_buttons(me),
           (int)XMouseEvent_position(me).x, (int)XMouseEvent_position(me).y);
#if XWIDGET_ON && XPUSHBUTTON_ON
    geo = XWidget_geometry((XWidget*)&((DemoWin*)self)->m_button);
    pos = XMouseEvent_position(me);
    if (XMouseEvent_button(me) == XMouseButton_LeftButton &&
        pos.x >= geo.x && pos.x < geo.x + geo.width &&
        pos.y >= geo.y && pos.y < geo.y + geo.height) {
        DemoWin* win = (DemoWin*)self;
        if (!win->m_buttonDown) {
            XPushButton_setDown(&win->m_button, true);
            XPushButton_pressed_signal(&win->m_button);
        }
    }
#if XFRAME_ON && XLABEL_ON && XLAYOUT_ON && XLAYOUT_STACKED_ON
    {
        DemoWin* win = (DemoWin*)self;
        XRect prevGeo = XWidget_geometry((XWidget*)&win->m_stackPrevButton);
        XRect nextGeo = XWidget_geometry((XWidget*)&win->m_stackNextButton);
        if (XMouseEvent_button(me) == XMouseButton_LeftButton &&
            pos.x >= prevGeo.x && pos.x < prevGeo.x + prevGeo.width &&
            pos.y >= prevGeo.y && pos.y < prevGeo.y + prevGeo.height) {
            if (!win->m_stackPrevDown) {
                win->m_stackPrevDown = true;
                XPushButton_setDown(&win->m_stackPrevButton, true);
                XPushButton_pressed_signal(&win->m_stackPrevButton);
            }
        } else if (XMouseEvent_button(me) == XMouseButton_LeftButton &&
                   pos.x >= nextGeo.x && pos.x < nextGeo.x + nextGeo.width &&
                   pos.y >= nextGeo.y && pos.y < nextGeo.y + nextGeo.height) {
            if (!win->m_stackNextDown) {
                win->m_stackNextDown = true;
                XPushButton_setDown(&win->m_stackNextButton, true);
                XPushButton_pressed_signal(&win->m_stackNextButton);
            }
        }
    }
#endif /* XFRAME_ON && XLABEL_ON && XLAYOUT_ON && XLAYOUT_STACKED_ON */
#endif
}

/** @brief MouseReleaseEvent：打印释放键/坐标。 */
static void VDemoWin_mouseReleaseEvent(XWindow* self, XEvent* event)
{
    XMouseEvent* me;
    XClass_Parent(XWindow, EXWindow_MouseReleaseEvent, XWindowEventSlot)(self, event);
    me = (XMouseEvent*)event;
    printf("XGuiWindowDemo: mouseRelease button=%d buttons=0x%x pos=(%d,%d)\n",
           (int)XMouseEvent_button(me), (unsigned)XMouseEvent_buttons(me),
           (int)XMouseEvent_position(me).x, (int)XMouseEvent_position(me).y);
#if XWIDGET_ON && XPUSHBUTTON_ON
    if (XMouseEvent_button(me) == XMouseButton_LeftButton &&
        ((DemoWin*)self)->m_buttonDown) {
        DemoWin* win = (DemoWin*)self;
        XPushButton_setDown(&win->m_button, false);
        XPushButton_released_signal(&win->m_button);
    }
#if XFRAME_ON && XLABEL_ON && XLAYOUT_ON && XLAYOUT_STACKED_ON
    {
        DemoWin* win = (DemoWin*)self;
        XPoint pos = XMouseEvent_position(me);
        XRect prevGeo = XWidget_geometry((XWidget*)&win->m_stackPrevButton);
        XRect nextGeo = XWidget_geometry((XWidget*)&win->m_stackNextButton);
        bool prevHit = pos.x >= prevGeo.x && pos.x < prevGeo.x + prevGeo.width &&
                       pos.y >= prevGeo.y && pos.y < prevGeo.y + prevGeo.height;
        bool nextHit = pos.x >= nextGeo.x && pos.x < nextGeo.x + nextGeo.width &&
                       pos.y >= nextGeo.y && pos.y < nextGeo.y + nextGeo.height;
        if (win->m_stackPrevDown) {
            win->m_stackPrevDown = false;
            XPushButton_setDown(&win->m_stackPrevButton, false);
            XPushButton_released_signal(&win->m_stackPrevButton);
            if (prevHit)
                XPushButton_clicked_signal(&win->m_stackPrevButton, false);
        }
        if (win->m_stackNextDown) {
            win->m_stackNextDown = false;
            XPushButton_setDown(&win->m_stackNextButton, false);
            XPushButton_released_signal(&win->m_stackNextButton);
            if (nextHit)
                XPushButton_clicked_signal(&win->m_stackNextButton, false);
        }
    }
#endif /* XFRAME_ON && XLABEL_ON && XLAYOUT_ON && XLAYOUT_STACKED_ON */
#endif
}

/** @brief MouseDoubleClickEvent：打印双击键/坐标。 */
static void VDemoWin_mouseDoubleClickEvent(XWindow* self, XEvent* event)
{
    XMouseEvent* me;
    XClass_Parent(XWindow, EXWindow_MouseDoubleClickEvent, XWindowEventSlot)(self, event);
    me = (XMouseEvent*)event;
    printf("XGuiWindowDemo: mouseDoubleClick button=%d pos=(%d,%d)\n",
           (int)XMouseEvent_button(me),
           (int)XMouseEvent_position(me).x, (int)XMouseEvent_position(me).y);
}

/** @brief MouseMoveEvent：打印坐标与按下集合。 */
static void VDemoWin_mouseMoveEvent(XWindow* self, XEvent* event)
{
    XMouseEvent* me;
    XClass_Parent(XWindow, EXWindow_MouseMoveEvent, XWindowEventSlot)(self, event);
    me = (XMouseEvent*)event;
    printf("XGuiWindowDemo: mouseMove buttons=0x%x pos=(%d,%d)\n",
           (unsigned)XMouseEvent_buttons(me),
           (int)XMouseEvent_position(me).x, (int)XMouseEvent_position(me).y);
}

/** @brief WheelEvent：打印滚轮角度增量与坐标。 */
static void VDemoWin_wheelEvent(XWindow* self, XEvent* event)
{
    XWheelEvent* we;
    XPoint delta;
    XClass_Parent(XWindow, EXWindow_WheelEvent, XWindowEventSlot)(self, event);
    we = (XWheelEvent*)event;
    delta = XWheelEvent_angleDelta(we);
    printf("XGuiWindowDemo: wheel delta=(%d,%d) pos=(%d,%d)\n",
           (int)delta.x, (int)delta.y,
           (int)XWheelEvent_position(we).x, (int)XWheelEvent_position(we).y);
}

/** @brief EnterEvent：打印进入坐标（局部+全局）。 */
static void VDemoWin_enterEvent(XWindow* self, XEvent* event)
{
    XEnterEvent* ee;
    XPoint global;
    XClass_Parent(XWindow, EXWindow_EnterEvent, XWindowEventSlot)(self, event);
    ee = (XEnterEvent*)event;
    global = XEnterEvent_globalPosition(ee);
    printf("XGuiWindowDemo: enter pos=(%d,%d) global=(%d,%d)\n",
           (int)XEnterEvent_position(ee).x, (int)XEnterEvent_position(ee).y,
           (int)global.x, (int)global.y);
}

/** @brief LeaveEvent：打印离开通知。 */
static void VDemoWin_leaveEvent(XWindow* self, XEvent* event)
{
    XClass_Parent(XWindow, EXWindow_LeaveEvent, XWindowEventSlot)(self, event);
    printf("XGuiWindowDemo: leave\n");
}

/** @brief 演示窗口类虚表初始化。 */
static XVtable* DemoWin_class_init(void)
{
    XVTABLE_INIT_DEFAULT(DemoWin)
    XVTABLE_INHERIT_XCLASS(XWindow);
    XVTABLE_OVERLOAD_DEFAULT(EXWindow_CloseEvent, VDemoWin_closeEvent);
    XVTABLE_OVERLOAD_DEFAULT(EXWindow_ExposeEvent, VDemoWin_exposeEvent);
    XVTABLE_OVERLOAD_DEFAULT(EXWindow_ResizeEvent, VDemoWin_resizeEvent);
    XVTABLE_OVERLOAD_DEFAULT(EXWindow_PaintEvent, VDemoWin_paintEvent);
    XVTABLE_OVERLOAD_DEFAULT(EXWindow_KeyPressEvent, VDemoWin_keyPressEvent);
    XVTABLE_OVERLOAD_DEFAULT(EXWindow_KeyReleaseEvent, VDemoWin_keyReleaseEvent);
    XVTABLE_OVERLOAD_DEFAULT(EXWindow_MousePressEvent, VDemoWin_mousePressEvent);
    XVTABLE_OVERLOAD_DEFAULT(EXWindow_MouseReleaseEvent, VDemoWin_mouseReleaseEvent);
    XVTABLE_OVERLOAD_DEFAULT(EXWindow_MouseDoubleClickEvent,
                             VDemoWin_mouseDoubleClickEvent);
    XVTABLE_OVERLOAD_DEFAULT(EXWindow_MouseMoveEvent, VDemoWin_mouseMoveEvent);
    XVTABLE_OVERLOAD_DEFAULT(EXWindow_WheelEvent, VDemoWin_wheelEvent);
    XVTABLE_OVERLOAD_DEFAULT(EXWindow_EnterEvent, VDemoWin_enterEvent);
    XVTABLE_OVERLOAD_DEFAULT(EXWindow_LeaveEvent, VDemoWin_leaveEvent);
    return XVTABLE_DEFAULT;
}

/** @brief 创建演示窗口对象并套用子类虚表。 */
static DemoWin* DemoWin_create(void)
{
    DemoWin* self = (DemoWin*)XMemory_malloc(
        sizeof(DemoWin), XCLASS_DEFAULT_MEMORY_TYPE);
    if (!self) return NULL;
    memset(self, 0, sizeof(DemoWin));
    XWindow_init(&self->m_base);
    XClassSetVtable(self, DemoWin);
    Set_Class_Memory(self, XCLASS_DEFAULT_MEMORY_TYPE);
    Set_Class_IsHeap(self, true);
#if XWIDGET_ON && XPUSHBUTTON_ON
    XPushButton_init(&self->m_button, NULL, 0);
    XPushButton_setText_2(&self->m_button, "XPushButton");
    XWidget_setGeometry((XWidget*)&self->m_button, 246, 190, 128, 36);
#if XWIDGET_ON && XFRAME_ON && XLABEL_ON
    XLabel_init(&self->m_linkLabel, NULL, 0);
    XLabel_setText_2(&self->m_linkLabel, "Released");
    XLabel_setTextPixelSize(&self->m_linkLabel, 16);
    XLabel_setAlignment(&self->m_linkLabel, XAlignment_Left | XAlignment_Top);
    XWidget_setGeometry((XWidget*)&self->m_linkLabel, 382, 190, 108, 36);
#endif
    XObject_connect_1((XObject*)&self->m_button,
                      (size_t)XPushButton_pressed_signal(NULL),
                      (XObject*)self, demo_button_pressedSlot,
                      XConnectionType_Direct);
    XObject_connect_1((XObject*)&self->m_button,
                      (size_t)XPushButton_released_signal(NULL),
                      (XObject*)self, demo_button_releasedSlot,
                      XConnectionType_Direct);
#endif
#if XWIDGET_ON && XFRAME_ON && XLABEL_ON && XLAYOUT_ON && XLAYOUT_STACKED_ON
    XStackedLayout_init(&self->m_stackLayout);
    /* DemoWin 的基类是 XWindow 而非 XWidget；页面由布局管理几何，
       绘制时由演示窗口显式调用，因此不能把 XWindow 强转为父控件。 */
    XLabel_init(&self->m_stackPageOne, NULL, 0);
    XLabel_setText_2(&self->m_stackPageOne, "Stacked page 1");
    XLabel_setTextPixelSize(&self->m_stackPageOne, 16);
    XLabel_setAlignment(&self->m_stackPageOne,
                        XAlignment_HCenter | XAlignment_VCenter);
    XLabel_init(&self->m_stackPageTwo, NULL, 0);
    XLabel_setText_2(&self->m_stackPageTwo, "Stacked page 2");
    XLabel_setTextPixelSize(&self->m_stackPageTwo, 16);
    XLabel_setAlignment(&self->m_stackPageTwo,
                        XAlignment_HCenter | XAlignment_VCenter);
    XStackedLayout_addWidget(&self->m_stackLayout,
                             (XWidget*)&self->m_stackPageOne);
    XStackedLayout_addWidget(&self->m_stackLayout,
                             (XWidget*)&self->m_stackPageTwo);
#if XPUSHBUTTON_ON
    XPushButton_init(&self->m_stackPrevButton, NULL, 0);
    XPushButton_setText_2(&self->m_stackPrevButton, "< Prev");
    XWidget_setGeometry((XWidget*)&self->m_stackPrevButton, 32, 274, 78, 30);
    XPushButton_init(&self->m_stackNextButton, NULL, 0);
    XPushButton_setText_2(&self->m_stackNextButton, "Next >");
    XWidget_setGeometry((XWidget*)&self->m_stackNextButton, 120, 274, 78, 30);
    XObject_connect_1((XObject*)&self->m_stackPrevButton,
                      (size_t)XPushButton_clicked_signal(NULL, false),
                      (XObject*)self, demo_stack_prev_clickedSlot,
                      XConnectionType_Direct);
    XObject_connect_1((XObject*)&self->m_stackNextButton,
                      (size_t)XPushButton_clicked_signal(NULL, false),
                      (XObject*)self, demo_stack_next_clickedSlot,
                      XConnectionType_Direct);
#endif /* XPUSHBUTTON_ON */
#endif
    return self;
}

/* ==================== 主函数 ==================== */

int main(int argc, char* argv[])
{
    XGuiApplication* app;
    XPlatformNativeInterface* gni;
    XPlatformIntegration* gpi;
    XPlatformWindow* pw;
    DemoWin* win;
    XBackingStore* store;
    int autoSeconds;
    int64_t startMsec;
    bool ok;

    autoSeconds = (argc > 1) ? atoi(argv[1]) : 0;
    if (autoSeconds < 0) autoSeconds = 0;

    /* 1) 初始化 XGuiApplication（进程内单例）。 */
    app = XGuiApplication_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, argc, argv);
    if (!app) {
        XPrintf("XGuiWindowDemo: XGuiApplication_create_ex 失败\n");
        return 1;
    }

    /* 2) 创建演示窗口并设置标题/几何（原生窗口在首次显示时惰性创建）。 */
    win = DemoWin_create();
    if (!win) {
        XPrintf("XGuiWindowDemo: DemoWin_create 失败\n");
        XGuiApplication_delete_base(app);
        return 1;
    }
    XWindow_setTitle_2((XWindow*)win, "XinYueC GUI 控件可视化测试 (X11/Win32)");
    XWindow_setGeometry((XWindow*)win, 60, 60, 520, 360);

    /* 3) 挂接平台原生接口与平台窗口句柄。 */
    gni = XGuiApplication_platformNativeInterface();
    gpi = (gni) ? XPlatformNativeInterface_integration(gni) : NULL;
    if (!gni || !gpi) {
        XPrintf("XGuiWindowDemo: 平台原生接口不可用（XPLATFORMNATIVEWINDOW_ON?）\n");
        XWindow_delete_base((XClass*)win);
        XGuiApplication_delete_base(app);
        return 1;
    }
    pw = XPlatformIntegration_createPlatformWindow(gpi, (XWindow*)win);
    if (!pw) {
        XPrintf("XGuiWindowDemo: createPlatformWindow 失败\n");
        XWindow_delete_base((XClass*)win);
        XGuiApplication_delete_base(app);
        return 1;
    }

    /* 4) 创建后备存储：paintDevice 为 XImage 软件缓冲，随后 XPainter 直绘。 */
    store = XBackingStore_create((XWindow*)win);
    if (!store) {
        XPrintf("XGuiWindowDemo: XBackingStore_create 失败\n");
        XWindow_delete_base((XClass*)win);
        XGuiApplication_delete_base(app);
        return 1;
    }
    win->m_store = store;

    /* 5) 显示窗口：触发惰性创建原生窗口并进入事件循环。 */
    XWindow_showNormal((XWindow*)win);
    startMsec = XDateTime_currentMSecsSinceEpoch();
    ok = XPlatformIntegration_isNativeWindowAvailable(gpi);
    XPrintf("XGuiWindowDemo: 原生窗口可用=%s 屏幕尺寸=%.0fx%.0f\n",
           ok ? "是" : "否",
           (double)XWindow_width((XWindow*)win),
           (double)XWindow_height((XWindow*)win));

    while (!win->m_closed) {
        int64_t nowMsec;
        if (autoSeconds > 0) {
            nowMsec = XDateTime_currentMSecsSinceEpoch();
            if ((nowMsec - startMsec) / 1000 >= (int64_t)autoSeconds) break;
        }
        XGuiApplication_waitForEvents(50); /* 平台消息泵（X11/Win32）+ 队列等待。 */
        XGuiApplication_processEvents(XEventLoop_AllEvents);
    }

    XPrintf("XGuiWindowDemo: 退出事件循环（关闭=%s 自动退出=%d）\n",
           win->m_closed ? "是" : "否", autoSeconds);

    /* 6) 清理：窗口销毁自动拆除原生窗口；应用单例回收集成层。 */
    XBackingStore_delete_base((XClass*)store);
    win->m_store = NULL;
#if XWIDGET_ON && XPUSHBUTTON_ON
    XPushButton_deinit_base(&win->m_button);
#endif
#if XWIDGET_ON && XFRAME_ON && XLABEL_ON
    XLabel_deinit_base(&win->m_linkLabel);
#endif
#if XWIDGET_ON && XFRAME_ON && XLABEL_ON && XLAYOUT_ON && XLAYOUT_STACKED_ON
#if XPUSHBUTTON_ON
    XPushButton_deinit_base(&win->m_stackPrevButton);
    XPushButton_deinit_base(&win->m_stackNextButton);
#endif
    XStackedLayout_deinit_base(&win->m_stackLayout);
#endif
    XWindow_delete_base((XClass*)win);
    XGuiApplication_delete_base(app);
    XPrintf("XGuiWindowDemo: 已退出\n");
    return 0;
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
