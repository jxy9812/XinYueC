/**
 * @file       XMdiArea.c
 * @brief      多文档接口区域及子窗口控件实现（对标 Qt 6.8 QMdiArea/QMdiSubWindow 全部公共 API）。
 * @details    与同名头文件的公共 API 一一对应；内部实现细节见
 *             头文件 @note 与函数级 Doxygen 注释。
 * @author     XinYueC 团队
 */

#if XGUI_ON

#include "XMdiArea.h"
#include "XMemory.h"
#include "XEvent.h"
#include "XVarList.h"
#include "XPainter.h"
#include "XGuiConfig.h"

#include "XAlgorithm.h"
#include "XStringUtils.h"
#include "XWidget_Protected.h"
#if XMENU_ON
#include "XMenu.h"
#endif
#if XABSTRACTSCROLLAREA_ON
#include "XAbstractScrollArea.h"
#endif

#if XWIDGET_ON && XABSTRACTSCROLLAREA_ON && XMDIAREA_ON

/* ==================== 动态类型判定（子窗口识别） ==================== */

/** @brief 子窗口激活信号发射体（定义见下方 XMdiArea 段；此处前置声明
 *         便于活动窗收口助手复用）。 */
static void xmdi_emitActivated(XMdiArea* self, XMdiSubWindow* window);
/** @brief 子窗口状态变化信号发射体（定义见下方 XMdiArea 段；折叠/
 *         最小化联动助手在定义点之前使用）。 */
static void xmdi_emitStateChanged(XMdiSubWindow* sw, int oldState,
                                  int newState);

/**
 * @brief      判断控件是否为 XMdiSubWindow（或其派生类）实例。
 * @details    XMdiArea::addSubWindow / removeSubWindow 需要区分「传入的是
 *             子窗口本身」还是「子窗口的内容件」——对标 Qt 的
 *             `qobject_cast<QMdiSubWindow*>(widget)` 分支
 *             （qmdiarea.cpp:1949-1957 / 1984-1999）。C 侧无 RTTI，识别
 *             机制=虚表槽位指纹：比对 EXWidget_PaintEvent 与 EXClass_Deinit
 *             两槽与 XMdiSubWindow_class_init 注册的静态实现——任何经
 *             XVTABLE_INHERIT_XCLASS(XMdiSubWindow) 派生的类都原样继承
 *             这两个槽，未继承本类虚表的控件不可能持有同槽函数指针。
 *             全程经 XVtable_at 越界安全读取，异类控件恒返回 false。
 * @param      candidate 待判定控件借用指针；可为 NULL。
 * @return     candidate 为 XMdiSubWindow 或其派生实例返回 true。
 */
static bool xmdi_isSubWindow(const XWidget* candidate)
{
    XVtable* vt;
    XVtable* self_vt;
    if (!candidate) return false;
    /* XWidget 嵌 XObject、XObject 嵌 XClass（m_class 恒为第一成员），
     * 任意控件对象首字节即 XClass 头，读虚表指针无越界。 */
    vt = ((const XClass*)candidate)->m_vtable;
    if (!vt) return false;
    self_vt = XMdiSubWindow_class_init();
    return XVtable_at(vt, (size_t)EXWidget_PaintEvent) ==
               XVtable_at(self_vt, (size_t)EXWidget_PaintEvent) &&
           XVtable_at(vt, (size_t)EXClass_Deinit) ==
               XVtable_at(self_vt, (size_t)EXClass_Deinit);
}

/**
 * @brief      在子窗口数组中的下标。
 * @param      self 目标区域；可为 NULL。
 * @param      window 子窗口借用指针；可为 NULL。
 * @return     找到返回下标；未登记/参数无效返回 -1。
 */
static int64_t xmdi_subWindowIndex(const XMdiArea* self,
                                   const XMdiSubWindow* window)
{
    int64_t i;
    int64_t n;
    if (!self || !self->m_subWindows || !window) return -1;
    n = XVector_size_base((const XContainer*)self->m_subWindows);
    for (i = 0; i < n; ++i) {
        XMdiSubWindow** slot =
            (XMdiSubWindow**)XVector_at_base(self->m_subWindows, i);
        if (slot && *slot == window) return i;
    }
    return -1;
}

/**
 * @brief      激活历史账本：登记新子窗口到最前。
 * @details    对标 QMdiAreaPrivate::appendChild 的
 *             `indicesToActivatedChildren.prepend(childWindows.size()-1)`
 *             （qmdiarea.cpp:812）。本实现按指针记录（Qt 按 childWindows
 *             下标记录），二者语义等价且免去删窗后的下标平移维护。
 * @param      self 目标区域；可为 NULL。
 * @param      window 子窗口借用指针；可为 NULL。
 * @return     无返回值。
 */
static void xmdi_historyAdd(XMdiArea* self, XMdiSubWindow* window)
{
    XMdiSubWindow* item;
    if (!self || !self->m_activationHistory || !window) return;
    item = window;
    XVector_insert_1_base(self->m_activationHistory, 0, &item, 1);
}

/**
 * @brief      激活历史账本：摘除已移除的子窗口。
 * @details    对标 updateActiveWindow 的
 *             `indicesToActivatedChildren.removeAll(index)` 与后续下标
 *             递减（qmdiarea.cpp:1105-1113）；按指针记账时只需删除该
 *             指针一项。
 * @param      self 目标区域；可为 NULL。
 * @param      window 子窗口借用指针；可为 NULL。
 * @return     无返回值。
 */
static void xmdi_historyRemove(XMdiArea* self, const XMdiSubWindow* window)
{
    int64_t i;
    int64_t n;
    if (!self || !self->m_activationHistory || !window) return;
    n = XVector_size_base((const XContainer*)self->m_activationHistory);
    for (i = 0; i < n; ++i) {
        XMdiSubWindow** slot =
            (XMdiSubWindow**)XVector_at_base(self->m_activationHistory, i);
        if (slot && *slot == window) {
            XVector_remove_base(self->m_activationHistory, i, 1);
            return;
        }
    }
}

/**
 * @brief      激活历史账本：把刚激活的子窗口移到最前。
 * @details    对标 activateWindow 的
 *             `indicesToActivatedChildren.move(index, 0)`
 *             （qmdiarea.cpp:1018-1024）：最近激活在前。
 * @param      self 目标区域；可为 NULL。
 * @param      window 子窗口借用指针；可为 NULL。
 * @return     无返回值。
 */
static void xmdi_historyRaise(XMdiArea* self, XMdiSubWindow* window)
{
    int64_t i;
    int64_t n;
    if (!self || !self->m_activationHistory || !window) return;
    n = XVector_size_base((const XContainer*)self->m_activationHistory);
    for (i = 0; i < n; ++i) {
        XMdiSubWindow** slot =
            (XMdiSubWindow**)XVector_at_base(self->m_activationHistory, i);
        if (slot && *slot == window) {
            if (i > 0) XVector_move((XVector*)self->m_activationHistory, i, 0);
            return;
        }
    }
}

/**
 * @brief      活动子窗被移出后的激活收口（对标 QMdiAreaPrivate::
 *             updateActiveWindow / resetActiveWindow）。
 * @details    Qt 在移除活动窗后挑下一个可见子窗激活；无候选则
 *             resetActiveWindow 并发射 subWindowActivated(nullptr)
 *             （qmdiarea.cpp:1049-1126）。隐藏子窗不参与激活——对齐
 *             `activateWindow` 的 `if (child->isHidden()) return;`
 *             （qmdiarea.cpp:941-947）。
 * @param      self 目标区域；可为 NULL。
 * @param      removed 刚被移出的子窗口借用指针（仅用于排除自身）。
 */
static void xmdi_reactivateAfterRemove(XMdiArea* self,
                                       XMdiSubWindow* removed)
{
    int64_t i;
    int64_t n;
    if (!self) return;
    if (self->m_active != removed) return;
    self->m_active = NULL;
    n = self->m_subWindows
            ? XVector_size_base((const XContainer*)self->m_subWindows) : 0;
    for (i = 0; i < n; ++i) {
        XMdiSubWindow** slot =
            (XMdiSubWindow**)XVector_at_base(self->m_subWindows, i);
        if (!slot || !*slot || *slot == removed) continue;
        if (XWidget_isHidden((XWidget*)*slot)) continue;
        XMdiArea_setActiveSubWindow(self, *slot);
        return;
    }
    xmdi_emitActivated(self, NULL);
}

/* ==================== XMdiSubWindow ==================== */

/* ==================== 子窗几何常量与标题带按钮 ==================== */

/** @brief 子窗标题带高度（像素；paintEvent 头带、内容件挂载与拖拽
 *         命中域共用同一常量口径）。 */
#define XMDI_SUBWINDOW_TITLEBAR_HEIGHT 20
/* 有父窗口时子窗 chrome 的最小宽度（标题带系统菜单图标位 + 最小化/关闭
 * 按钮位合计的经验下界；对标 Qt 由 CC_TitleBar 各子控件矩形求和得到的
 * minWidth，qmdisubwindow.cpp:1719-1745）。 */
#define XMDI_SUBWINDOW_CHROME_MIN_WIDTH 50
/* 标题带按钮命中矩形（与 paintEvent 的绘制位置同源）：关闭 "×" 画在
 * w-18 附近、最小化 8x1 横线画在 w-34..w-26，故命中域取两段互不重叠的
 * 区域。 */
#define XMDI_SUBWINDOW_BTN_H 17

/** @brief 标题带是否显示/响应关闭按钮。
 * @details Qt 的 CloseAction 可见条件是 `windowFlags &
 *          Qt::WindowSystemMenuHint` 且非 FramelessWindowHint
 *          （QMdiSubWindowPrivate::updateActions，qmdisubwindow.cpp:
 *          1930-1960）；按钮命中与绘制共用本判定。
 * @param sw 目标子窗口；可为 NULL。
 * @return 可关闭返回 true。 */
static bool xmdi_subWindowHasCloseButton(const XMdiSubWindow* sw)
{
    XWidgetFlags flags;
    if (!sw) return false;
    flags = XWidget_windowFlags((XWidget*)sw);
    if (flags & (XWidgetFlags)XWindowType_FramelessWindowHint) return false;
    return (flags & (XWidgetFlags)XWindowType_WindowSystemMenuHint) != 0;
}

/** @brief 标题带是否显示/响应最小化按钮（Qt：WindowMinimizeButtonHint，
 *         同上 updateActions）。
 * @param sw 目标子窗口；可为 NULL。
 * @return 可最小化返回 true。 */
static bool xmdi_subWindowHasMinButton(const XMdiSubWindow* sw)
{
    XWidgetFlags flags;
    if (!sw) return false;
    flags = XWidget_windowFlags((XWidget*)sw);
    if (flags & (XWidgetFlags)XWindowType_FramelessWindowHint) return false;
    return (flags & (XWidgetFlags)XWindowType_WindowMinimizeButtonHint) != 0;
}

/** @brief 取标题带按钮的命中矩形（局部坐标）。
 * @param sw 目标子窗口。
 * @param close true=关闭按钮，false=最小化按钮。
 * @param out 输出矩形（可为 NULL）。
 * @return 该按钮有绘制/命中位返回 true（窗口过窄返回 false）。 */
static bool xmdi_subWindowButtonRect(const XMdiSubWindow* sw, bool close,
                                     XRect* out)
{
    int w;
    if (!sw) return false;
    w = XWidget_width((const XWidget*)sw);
    if (w <= 40) return false;
    if (close) {
        /* "×" 画在 w-18 附近：命中域 [w-26, w-10)。 */
        if (w - 26 < 18) return false; /* 让开系统菜单图标位 */
        if (out) XRect_init(out, w - 26, 2, 16, XMDI_SUBWINDOW_BTN_H);
    } else {
        /* 最小化 8x1 横线画在 w-34..w-26：命中域 [w-40, w-26)。 */
        if (w - 40 < 18) return false;
        if (out) XRect_init(out, w - 40, 2, 14, XMDI_SUBWINDOW_BTN_H);
    }
    return true;
}

/** @brief 标题带关闭按钮的关闭链：close() + DeleteOnClose 收口。
 * @details 对标 ControllerWidget 的 `_q_close` → `mdiChild->close()`
 *          （qmdisubwindow.cpp:701/617），随后按 QWidget 的
 *          WA_DeleteOnClose 语义回收内部创建的子窗（qmdiarea.cpp:1962）。
 *          关闭被 closeEvent 拒绝时保持原状（Qt 同）。
 * @param sw 目标子窗口。
 * @return 无返回值。 */
static void xmdi_closeFromTitleBar(XMdiSubWindow* sw)
{
    XMdiArea* area;
    bool deleteOnClose;
    int64_t i;
    if (!sw) return;
    area = XMdiSubWindow_mdiArea(sw);
    deleteOnClose = XWidget_testAttribute((XWidget*)sw,
                                          XWidgetAttribute_DeleteOnClose);
    if (!XWidget_close((XWidget*)sw)) return; /* 关闭被拒绝 */
    if (!area) return;
    if (deleteOnClose) {
        i = xmdi_subWindowIndex(area, sw);
        if (i >= 0) XVector_remove_base(area->m_subWindows, i, 1);
        xmdi_historyRemove(area, sw);
    }
    xmdi_reactivateAfterRemove(area, sw);
    if (deleteOnClose) XObject_deleteLater((XObject*)sw);
}

/** @brief 子窗 chrome 绘制（对标 QMdiSubWindow::paintEvent，
 *         qmdisubwindow.cpp:3086-3138 的标准条构造）。
 * @details 第八轮总验收 FAIL② 根修：此前仅涂「裸蓝标题条 + 白体」，
 *          无窗框无图标无按钮位（r3e_mdi_zoom.png）。补齐：
 *          - 窗框 1px（Mid；对标 PE_FrameWindow，宽度按本项目设计
 *            语言取 1px 而非 Fusion 的 PM_MdiSubWindowFrameWidth=4）；
 *          - 标题带：激活=Highlight 底（qmdisubwindow.cpp:1637-1644
 *            State_Active 分支；激活判定=mdiArea()->activeSubWindow
 *            ==self），非激活=Window 底；
 *          - 带左系统菜单图标位（迷你窗图形，对标 CC_TitleBar 的
 *            SC_TitleBarSysMenu）、右最小化/关闭按钮（SC_TitleBar
 *            MinButton/CloseButton：命中域见
 *            xmdi_subWindowButtonRect，关闭走 close 链、最小化走
 *            showMinimized；按钮显隐受 windowFlags 提示位与
 *            FramelessWindowHint 约束，同 QMdiSubWindowPrivate::
 *            updateActions）；
 *          - 标题文本自基类 windowTitle()（对标 QWidget 属性；Qt 的
 *            QMdiSubWindow 没有独立标题字段）；setWidget 时为空则采纳
 *            内容件 windowTitle（qmdisubwindow.cpp:994 标题同步链的
 *            静态版）。
 *          脏区口径不变：只绘事件脏区∩各铬件（night #50 修复保持）。
 *          内容件挂载已内收 1px（XMdiSubWindow_setWidget/resizeEvent），
 *          侧框线不再被子控件覆盖。 */
static void VX_mdiSubWindow_paintEvent(XWidget* self, XEvent* event)
{
    XMdiSubWindow* sw = (XMdiSubWindow*)self;
    XPainter painter;
    XImage* image;
    XPoint offset;
    XRect head;
    XRect body;
    XRect exposed;
    XRect piece;
    XRect clipHead;
    const char* titleText;
    uint32_t bandColor;
    uint32_t textColor;
    uint32_t frameColor;
    uint32_t base;
    bool active;
    int w;
    int h;
    XWidget* parent;
    XRect visible;
    int pw;
    int ph;
    if (!sw || !event) return;
    w = XWidget_width(self);
    h = XWidget_height(self);
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
#if XPALETTE_ON
    {
        XPalette palette = XWidget_palette(self);
        XColor c = XPalette_color(&palette, XPaletteColorGroup_Current,
                                  XPaletteColorRole_Base);
        base = XColor_rgba(&c);
        c = XPalette_color(&palette, XPaletteColorGroup_Current,
                           XPaletteColorRole_Mid);
        frameColor = XColor_rgba(&c);
        c = XPalette_color(&palette, XPaletteColorGroup_Current,
                           XPaletteColorRole_Window);
        /* 非激活标题带底色默认（对标 titleBarOptions 的 Inactive 色组，
         * qmdisubwindow.cpp:1640-1643）。 */
        bandColor = XColor_rgba(&c);
        c = XPalette_color(&palette, XPaletteColorGroup_Current,
                           XPaletteColorRole_WindowText);
        textColor = XColor_rgba(&c);
    }
#else
    bandColor = 0xFFD8D8D8u;
    textColor = 0xFF000000u;
    base = 0xFFFFFFFFu;
    frameColor = 0xFF808080u;
#endif /* XPALETTE_ON */
    /* 激活判定：所属 MDI 区当前活动子窗（脱离区域的独立子窗按非激
       活呈现，同 Qt 无 parent 时无活动态）；激活带=Highlight 底 +
       HighlightedText 前景（qmdisubwindow.cpp:1637-1639）。 */
    {
        XMdiArea* area = XMdiSubWindow_mdiArea(sw);
        active = area && XMdiArea_activeSubWindow(area) ==
                            (XMdiSubWindow*)sw;
    }
    if (active) {
#if XPALETTE_ON
        XPalette palette = XWidget_palette(self);
        XColor c = XPalette_color(&palette, XPaletteColorGroup_Current,
                                  XPaletteColorRole_Highlight);
        bandColor = XColor_rgba(&c);
        c = XPalette_color(&palette, XPaletteColorGroup_Current,
                           XPaletteColorRole_HighlightedText);
        textColor = XColor_rgba(&c);
#else
        bandColor = 0xFF3080C0u;
        textColor = 0xFFFFFFFFu;
#endif /* XPALETTE_ON */
    }
    /* 只绘事件脏区（本地坐标，收进控件矩形）：此前恒整幅涂写标题条
     * +白底，多矩形脏区逐矩形派发时本窗的整幅白底会抹掉先派矩形刚
     * 恢复的内容标签像素，且抹写不受 present 区域保护（night #50
     * 交互擦除链的子窗一级；对标 Qt paintEvent 经裁剪只绘暴露区）。 */
    exposed = ((const XPaintEvent*)event)->m_rect;
    if (exposed.x < 0) { exposed.width += exposed.x; exposed.x = 0; }
    if (exposed.y < 0) { exposed.height += exposed.y; exposed.y = 0; }
    if (exposed.x + exposed.width > w) exposed.width = w - exposed.x;
    if (exposed.y + exposed.height > h) exposed.height = h - exposed.y;
    /* 父界裁剪（对标 Qt 通用语义：子控件绘制恒被 parentWidget() 矩形
     * 裁剪）。本框架 paintTree 不按父矩形裁剪子件绘制，子窗被拖到视
     * 口边缘后带/体像素会泼到视口外的页签标签、状态栏等区域（夜间 D
     * 猎捕 d1_i4 实锚）。此处子窗一级自裁剪：绘制域收进「父件矩形平
     * 移到本窗局部坐标」的交集，拖出视口的部分不再落笔，观感与 Qt
     * 一致（体/带在视口边缘截断）。父区缺失（独立子窗）或退化时不裁
     * 剪。无效暴露区（完全出界）直接跳过绘制。 */
    parent = XWidget_parentWidget(self);
    if (parent) {
        pw = XWidget_width(parent);
        ph = XWidget_height(parent);
        if (pw > 0 && ph > 0) {
            XRect geoSelf = XWidget_geometry(self);
            XRect_init(&visible, -geoSelf.x, -geoSelf.y, pw, ph);
            exposed = XRect_intersected(&exposed, &visible);
        }
    }
    if (exposed.width <= 0 || exposed.height <= 0) {
        XPainter_deinit(&painter);
        return;
    }
    /* 体不透明填充（行 20 起、内收侧/底框线）：此前仅涂标题条，标题条
     * 以下透明透出视口背景。 */
    if (h > 21 && w > 2) {
        XRect_init(&body, 1, 20, w - 2, h - 21);
        body = XRect_intersected(&body, &exposed);
        if (body.width > 0 && body.height > 0)
            XPainter_fillRect(&painter, &body, base);
    }
    /* 标题带（框线内 1..19 行；第 19 行兼作带下分隔）。 */
    XRect_init(&head, 1, 1, w > 2 ? w - 2 : 0, 19);
    clipHead = head;
    /* 图标/文本绘制用 clipHead 作画家裁剪，须同受父界约束（head 填充
     * 已随 exposed 收进父界；此处保持两者口径一致）。 */
    if (parent && pw > 0 && ph > 0)
        clipHead = XRect_intersected(&clipHead, &visible);
    head = XRect_intersected(&head, &exposed);
    if (head.width > 0 && head.height > 0)
        XPainter_fillRect(&painter, &head, bandColor);
    /* 窗框 1px：顶/左/右/底（最后绘制，保证框线压住体填充）。 */
    if (w > 0 && h > 0) {
        if (0 >= exposed.y && 0 < exposed.y + exposed.height) {
            XRect_init(&piece, exposed.x, 0, exposed.width, 1);
            XPainter_fillRect(&painter, &piece, frameColor);
        }
        if (h - 1 >= exposed.y && h - 1 < exposed.y + exposed.height &&
            exposed.width > 0) {
            XRect_init(&piece, exposed.x, h - 1, exposed.width, 1);
            XPainter_fillRect(&painter, &piece, frameColor);
        }
        if (0 >= exposed.x && 0 < exposed.x + exposed.width) {
            XRect_init(&piece, 0, exposed.y, 1, exposed.height);
            XPainter_fillRect(&painter, &piece, frameColor);
        }
        if (w - 1 >= exposed.x && w - 1 < exposed.x + exposed.width &&
            exposed.height > 0) {
            XRect_init(&piece, w - 1, exposed.y, 1, exposed.height);
            XPainter_fillRect(&painter, &piece, frameColor);
        }
    }
    if (head.width > 0 && head.height > 0) {
        XPainter_setClipRect(&painter, &clipHead,
                             XPainterClipOperation_ReplaceClip);
        /* 左：系统菜单图标（迷你窗图形，对标 CC_TitleBar 的
         * SC_TitleBarSysMenu；点击即弹出系统菜单，同 Qt 的
         * ControlLabel::_q_showSystemMenu，qmdisubwindow.cpp:416-448）。 */
        XPainter_setPen(&painter, textColor);
        XPainter_drawRect_2(&painter, 4.0f, 4.0f, 10.0f, 10.0f);
        XRect_init(&piece, 5, 5, 8, 3);
        XPainter_fillRect(&painter, &piece, textColor);
        /* 标题文本：与 QMdiSubWindow 同源读基类 windowTitle()
         * （qmdisubwindow.cpp:3086-3138 的 titleBarOptions()->text），
         * 不再维护独立标题字段。 */
        titleText = "";
        {
            const XString* title = XWidget_windowTitle(self);
            if (title && XString_size((const XContainer*)title) > 0)
                titleText = XString_toUtf8(title);
        }
        if (titleText && *titleText)
            /* x=18 让出图标位；基线 14 同 XDockWidget 标题条口径。 */
            XPainter_drawText(&painter, 18, 14, titleText, textColor);
        /* 右：最小化（8x1 横线）+ 关闭（"×" 字形）按钮，位置与命中矩形
         * 同源（xmdi_subWindowButtonRect）；显隐按窗口标志提示位
         * （QMdiSubWindowPrivate::updateActions 同判据）。 */
        if (w > 40) {
            if (xmdi_subWindowHasMinButton(sw) &&
                xmdi_subWindowButtonRect(sw, false, &piece)) {
                XRect lineRect;
                XRect_init(&lineRect, w - 34, 10, 8, 1);
                XPainter_fillRect(&painter, &lineRect, textColor);
            }
            if (xmdi_subWindowHasCloseButton(sw))
                XPainter_drawText(&painter, w - 18, 14, "\xc3\x97",
                                  textColor);
        }
        XPainter_setClipping(&painter, false);
    }
    XPainter_deinit(&painter);
}

/* ==================== 子窗标题条拖拽会话 ==================== */
/* 对标 Qt 6.8.3 qmdisubwindow.cpp 的标题条拖动链：mousePressEvent:
 * 3160 记录 mousePressPosition/oldGeometry → mouseMoveEvent:3302 以
 * 全局坐标映射父区驱动 setNewGeometry → setNewGeometry:1163-1175 按
 * 父矩形钳位（BoundaryMargin=5，:169）→ mouseReleaseEvent:3234 收尾。
 * XMdiArea.h 为公共契约头（结构体不可扩字段），拖拽会话状态按模块级
 * 单例收拢在实现文件内——鼠标单点语义下同一时刻至多一路拖拽，口径同
 * XSizeGrip.c 的 g_sizeGripDragActive 模块级登记。头带高度 20 与
 * paintEvent 头带及内容件 y=20 挂载（XMdiSubWindow_setWidget）共用
 * XMDI_SUBWINDOW_TITLEBAR_HEIGHT（定义于本文件子窗 chrome 常量段）。 */
/* 拖拽视口钳位边距（对标 Qt 6.8.3 qmdisubwindow.cpp 文件级
 * static const int BoundaryMargin = 5）。 */
#define XMDI_SUBWINDOW_BOUNDARY_MARGIN 5
static bool g_mdiDragActive = false;      /**< 按下头带后进入拖拽会话。 */
static XMdiSubWindow* g_mdiDragWindow = NULL; /**< 会话归属子窗（防串扰）。 */
static XPoint g_mdiDragPressGlobal;       /**< 按下时屏幕全局坐标。 */
static XRect g_mdiDragStartGeometry;      /**< 按下时子窗几何（尺寸锚定）。 */

/** @brief 内容件在子窗内的挂载矩形（标题带下方、框线内收 1px）。
 * @details chrome 窗框（paintEvent）绘制占用的 1px 边缘不归内容件：
 *          x=1 起、宽 w-2（避侧框线），y=20（标题带 0..19 行）起、
 *          高 h-21（底框线 1 行）；尺寸不足时钳 0（折叠态 h=20 时
 *          内容件 0 高不绘，同 Qt 折叠仅标题条）。setWidget 挂载与
 *          resizeEvent 随框布局共用同一口径。 */
static void xmdi_subWindowContentRect(const XMdiSubWindow* self, XRect* out)
{
    int w;
    int h;
    if (!out) return;
    w = self ? XWidget_width((const XWidget*)self) : 0;
    h = self ? XWidget_height((const XWidget*)self) : 0;
    XRect_init(out, 1, XMDI_SUBWINDOW_TITLEBAR_HEIGHT,
               w > 2 ? w - 2 : 0, h > 21 ? h - 21 : 0);
}

static int xmdi_dragClamp(int value, int low, int high)
{
    if (value < low) return low;
    if (value > high) return high;
    return value;
}

/** @brief 拖动一步的脏区全覆盖（对标 QMdiAreaPrivate 移动 update 系统）。
 * @details 第八轮总验收 FAIL① 根修：拖后标题栏左端「台阶状缺角」
 *          （r3e_mdi_drag2_zoom.png）= 旧位部分行列从未进入任何已派发
 *          脏区快照。XWidget_setGeometry 内联的 old∪new 包围盒失效
 *          （XWidget.c recomputeGeometry）与显式 update 共用同一异步
 *          管线（顶层脏区 → PAINT 快照 → 求差派发），单次快照竞态下
 *          可能漏行。本助手在每步拖动后追加一次显式 update：
 *          旧位 ∪ 新位 ∪ 平移扫掠 delta 带（横/纵四条带）逐矩形入队，
 *          同区域多次入队幂等，任一次快照漏区都被其余入队补齐——
 *          脏区覆盖从「单来源单快照」变为「双来源多快照」全闭合。
 * @param      parent 子窗父容器（视口；矩形同子窗局部坐标系）。
 * @param      oldRect 本步拖动前子窗矩形。
 * @param      newRect 本步拖动后子窗矩形。
 * @return     无返回值。
 */
static void xmdi_dragUpdateCoverage(XWidget* parent,
                                    const XRect* oldRect,
                                    const XRect* newRect)
{
    XRegion region;
    XRect band;
    int dx = newRect->x - oldRect->x;
    int dy = newRect->y - oldRect->y;
    int minY = oldRect->y < newRect->y ? oldRect->y : newRect->y;
    int minX = oldRect->x < newRect->x ? oldRect->x : newRect->x;
    int spanH = oldRect->height > newRect->height
                    ? oldRect->height : newRect->height;
    int spanW = oldRect->width > newRect->width
                    ? oldRect->width : newRect->width;
    if (!parent) return;
    XRegion_init(&region);
    XRegion_addRect(&region, oldRect);
    XRegion_addRect(&region, newRect);
    /* 横向扫掠带：水平位移在新旧位之间扫过的竖条（含对角移动的
       斜向耦合段，覆盖域=平移 Minkowski 和，宽 |dx| 高 h+|dy|）。 */
    if (dx > 0) {
        XRect_init(&band, oldRect->x + oldRect->width, minY, dx,
                   spanH + (dy < 0 ? -dy : dy));
        XRegion_addRect(&region, &band);
    } else if (dx < 0) {
        XRect_init(&band, newRect->x + newRect->width, minY, -dx,
                   spanH + (dy < 0 ? -dy : dy));
        XRegion_addRect(&region, &band);
    }
    /* 纵向扫掠带：垂直位移扫过的横条（宽 w+|dx| 高 |dy|）。 */
    if (dy > 0) {
        XRect_init(&band, minX, oldRect->y + oldRect->height,
                   spanW + (dx < 0 ? -dx : dx), dy);
        XRegion_addRect(&region, &band);
    } else if (dy < 0) {
        XRect_init(&band, minX, newRect->y + newRect->height,
                   spanW + (dx < 0 ? -dx : dx), -dy);
        XRegion_addRect(&region, &band);
    }
    XWidget_updateRegion(parent, &region);
    XRegion_deinit(&region);
}

static void VX_mdiSubWindow_mousePressEvent(XWidget* self, XEvent* event)
{
    XMdiSubWindow* sw = (XMdiSubWindow*)self;
    XMouseEvent* me = (XMouseEvent*)event;
    XPoint pos;
    if (!sw || !event ||
        XEvent_type(event) != XEVENT_TYPE_MOUSE_BUTTON_PRESS) return;
    if (XMouseEvent_button(me) != XMouseButton_LeftButton ||
        XMouseEvent_position(me).y >= XMDI_SUBWINDOW_TITLEBAR_HEIGHT) {
        /* 非左键或非头带（头带以下为内容件命中域）：交基类默认链
         * （对标 qmdisubwindow.cpp:3153 非左键 ignore 沿父链传播）。
         * 注意须用 XClass_Parent 直呼父类实现——XWidget_mousePressEvent_base
         * 是 vtable 派发助手，会重入本重载造成无限递归（复扫-5 实证）。 */
        XClass_Parent(XWidget, EXWidget_MousePressEvent,
                      void (*)(XWidget*, XEvent*))((XWidget*)self, event);
        return;
    }
    /* 头带按下即激活本子窗（对标 qmdisubwindow.cpp:958 按压激活链，
     * qmdiarea.cpp 视口事件过滤器的 setActiveSubWindow 同义）：激活态
     * 驱动标题带 Highlight/Window 配色（paintEvent），拖动非活动窗时
     * 配色即时翻转，不再出现「灰带被拖」。区域缺失（独立子窗）跳过。 */
    {
        XMdiArea* area = XMdiSubWindow_mdiArea(sw);
        if (area) XMdiArea_setActiveSubWindow(area, sw);
    }
    /* 标题带按钮命中优先于拖拽（对标 CC_MdiControls 的
     * hitTestComplexControl + _q_close/_q_minimize 连接，
     * qmdisubwindow.cpp:519-529、617-627、701-703）。命中矩形与
     * paintEvent 的绘制位置同源（xmdi_subWindowButtonRect）。 */
    {
        XPoint p = XMouseEvent_position(me);
        XRect btn;
        /* 左侧系统菜单图标（同 paintEvent 的 4..14 方框）：弹出系统菜单
         * （对标 SC_TitleBarSysMenu 的 _q_showSystemMenu）。 */
        if (p.x >= 4 && p.x < 14 && p.y >= 4 && p.y < 14 &&
            XMdiSubWindow_systemMenu(sw)) {
            XMdiSubWindow_showSystemMenu(sw);
            XEvent_accept(event);
            return;
        }
        if (xmdi_subWindowHasCloseButton(sw) &&
            xmdi_subWindowButtonRect(sw, true, &btn) &&
            p.x >= btn.x && p.x < btn.x + btn.width &&
            p.y >= btn.y && p.y < btn.y + btn.height) {
            xmdi_closeFromTitleBar(sw);
            XEvent_accept(event);
            return;
        }
        if (xmdi_subWindowHasMinButton(sw) &&
            xmdi_subWindowButtonRect(sw, false, &btn) &&
            p.x >= btn.x && p.x < btn.x + btn.width &&
            p.y >= btn.y && p.y < btn.y + btn.height) {
            XWidget_showMinimized((XWidget*)sw);
            XEvent_accept(event);
            return;
        }
    }
    /* 记录拖拽基线（对标 qmdisubwindow.cpp:3160-3162：按下记全局
     * 基准与 oldGeometry；移动事件以全局坐标差值算增量——局部坐标
     * 随命中接收者改写不可靠，同 XSizeGrip 口径）。 */
    g_mdiDragActive = true;
    g_mdiDragWindow = sw;
    g_mdiDragPressGlobal = XMouseEvent_globalPosition(me);
    g_mdiDragStartGeometry = XWidget_geometry(self);
    /* 对标 Qt 按压建立的隐式抓取（qt_button_down 后续 move/release
     * 直达按下控件）：拖拽中光标必然越出头带，显式抓取保证后续
     * 移动/释放仍送达本子窗（同 XSizeGrip.c:133 / XSplitter.c:456）。 */
    XWidget_grabMouse(self);
    XEvent_accept(event);
}

static void VX_mdiSubWindow_mouseMoveEvent(XWidget* self, XEvent* event)
{
    XMdiSubWindow* sw = (XMdiSubWindow*)self;
    XMouseEvent* me = (XMouseEvent*)event;
    XWidget* parent;
    XPoint np;
    int dx;
    int dy;
    int newX;
    int newY;
    int vw;
    int vh;
    if (!sw || !event || XEvent_type(event) != XEVENT_TYPE_MOUSE_MOVE)
        return;
    if (!g_mdiDragActive || g_mdiDragWindow != sw ||
        XMouseEvent_buttons(me) != XMouseButton_LeftButton) {
        /* 无按压基线/左键已释放/会话不归本窗：不动作（防御会话状态
         * 残留时误改几何，口径同 XSizeGrip.c:152-156）。基类链同 Press：
         * XClass_Parent 直呼（_base 助手会重入本重载，复扫-5 悬停栈
         * 溢出实证）。 */
        XClass_Parent(XWidget, EXWidget_MouseMoveEvent,
                      void (*)(XWidget*, XEvent*))((XWidget*)self, event);
        return;
    }
    parent = XWidget_parentWidget(self);
    np = XMouseEvent_globalPosition(me);
    dx = np.x - g_mdiDragPressGlobal.x;
    dy = np.y - g_mdiDragPressGlobal.y;
    newX = g_mdiDragStartGeometry.x + dx;
    newY = g_mdiDragStartGeometry.y + dy;
    /* 视口钳位（对标 qmdisubwindow.cpp:1163-1175 setNewGeometry 的
     * Move 分支：AllowOutsideAreaHorizontally/Vertically 默认关时
     * posX = qMin(qMax(BoundaryMargin, posX), parentWidth-BoundaryMargin)、
     * posY ≤ parentHeight-BoundaryMargin，BoundaryMargin=5）。
     * 本框架取两处有意偏差（均有实测依据，见夜间 D 猎捕 MDI 分片）：
     *   横向下界取 Qt 精确语义 [5, vw-5]——子窗左缘恒留视口内 5px，
     *   标题带左段（系统菜单图标/标题一侧）恒可见，抓取锚点稳定；
     *   旧实现的 -(w-5) 下界允许左缘带整段越界、仅剩右端 5px 残带，
     *   系对 Qt 该分支的误读，修正；
     *   纵向上界取严为 vh-头带高——头带整段恒在视口内（Qt 的 vh-5
     *   允许头带沉到只剩 5px 条；Qt 有 QMdiArea 滚动条回捞路径而本
     *   框架视口无滚动回捞，取严保「随时可抓回」）。
     * 父区缺失（独立子窗）时不钳位。 */
    if (parent) {
        vw = XWidget_width(parent);
        vh = XWidget_height(parent);
        newX = xmdi_dragClamp(newX,
                              XMDI_SUBWINDOW_BOUNDARY_MARGIN,
                              vw - XMDI_SUBWINDOW_BOUNDARY_MARGIN);
        newY = xmdi_dragClamp(newY, 0,
                              vh - XMDI_SUBWINDOW_TITLEBAR_HEIGHT);
    }
    /* 尺寸取按下时锚定值：拖动不缩放（对标 oldGeometry 锚定，仅
     * Move 操作改 top-left）。步内旧位→新位+扫掠 delta 显式全覆盖
     * 失效（xmdi_dragUpdateCoverage；setGeometry 内联的包围盒失效
     * 之外的双保险，第八轮 FAIL① 台阶缺角根修）。 */
    {
        XRect oldRect = XWidget_geometry(self);
        XRect newRect;
        XWidget_setGeometry(self, newX, newY,
                            g_mdiDragStartGeometry.width,
                            g_mdiDragStartGeometry.height);
        newRect = XWidget_geometry(self);
        if (parent && (newRect.x != oldRect.x || newRect.y != oldRect.y))
            xmdi_dragUpdateCoverage(parent, &oldRect, &newRect);
    }
    XEvent_accept(event);
}

static void VX_mdiSubWindow_mouseReleaseEvent(XWidget* self, XEvent* event)
{
    XMdiSubWindow* sw = (XMdiSubWindow*)self;
    XMouseEvent* me = (XMouseEvent*)event;
    if (!sw || !event ||
        XEvent_type(event) != XEVENT_TYPE_MOUSE_BUTTON_RELEASE) return;
    if (XMouseEvent_button(me) != XMouseButton_LeftButton) {
        /* 对标 qmdisubwindow.cpp:3241 非左键交基类默认链（XClass_Parent
         * 直呼，同 Press/Move 防重入口径）。 */
        XClass_Parent(XWidget, EXWidget_MouseReleaseEvent,
                      void (*)(XWidget*, XEvent*))((XWidget*)self, event);
        return;
    }
    /* 结束拖拽会话并解除抓取（对标 mouseReleaseEvent:3247 收尾 +
     * d->gotMousePress 复位；releaseMouse 仅当 self 为当前抓取者时
     * 生效，同 XSizeGrip.c:201-205）。 */
    if (g_mdiDragWindow == sw) g_mdiDragWindow = NULL;
    g_mdiDragActive = false;
    XWidget_releaseMouse(self);
    XEvent_accept(event);
}

/** @brief 尺寸变化时内容件随框布局（对标 QMdiSubWindow::resizeEvent，
 *         qmdisubwindow.cpp:3026：缩放后内容几何与框保持一致）。
 * @details 此前子窗 resize 后内容件保持 creation 尺寸——chrome 窗框
 *          绘制后表现为框大内容小、框线内残白。拖动只改位置不发
 *          RESIZE（recomputeGeometry 仅 sizeChanged 派发），跟手链
 *          不受本槽影响。链式口径同 XSplitter resizeEvent（不链基
 *          类，XWidget 默认 Resize 槽为空实现）。 */
static void VX_mdiSubWindow_resizeEvent(XWidget* self, XEvent* event)
{
    XMdiSubWindow* sw = (XMdiSubWindow*)self;
    XRect r;
    if (!sw || !event || XEvent_type(event) != XEVENT_TYPE_RESIZE) return;
    if (sw->m_widget) {
        xmdi_subWindowContentRect(sw, &r);
        XWidget_setGeometryRect(sw->m_widget, &r);
    }
}

/** @brief 折叠/最小化进入前记录恢复几何（对标 restoreSize + oldGeometry，
 *         qmdisubwindow.cpp:2586-2589/1246-1288）。
 * @details 只在首次进入「非正常态」时记录：折叠与最小化互斥切换时
 *          （例如缩小态直接最小化）不覆盖最初的正常几何。
 * @param sw 目标子窗口；可为 NULL。
 * @return 无返回值。 */
static void xmdi_saveRestoreGeometry(XMdiSubWindow* sw)
{
    if (!sw || sw->m_restoreValid) return;
    sw->m_restoreGeometry = XWidget_geometry((XWidget*)sw);
    sw->m_restoreValid = 1;
}

/** @brief 按记录的正常几何恢复（对标 setNormalMode，qmdisubwindow.cpp:
 *         1246-1288）。
 * @param sw 目标子窗口；可为 NULL。
 * @return 无返回值。 */
static void xmdi_restoreNormalGeometry(XMdiSubWindow* sw)
{
    if (!sw) return;
    if (sw->m_restoreValid) {
        XWidget_setGeometryRect((XWidget*)sw, &sw->m_restoreGeometry);
        sw->m_restoreValid = 0;
    }
}

/** @brief 折叠/最小化时隐藏内容件（对标 showShaded 的
 *         `baseWidget->hide()` 分支，qmdisubwindow.cpp:2610-2616：
 *         只隐藏「未被用户隐藏」的内容件，并记 isWidgetHiddenByUs）。
 * @param sw 目标子窗口；可为 NULL。
 * @return 无返回值。 */
static void xmdi_hideContent(XMdiSubWindow* sw)
{
    if (!sw || !sw->m_widget) return;
    if (XWidget_isHidden(sw->m_widget)) return;
    XWidget_hide(sw->m_widget);
    sw->m_contentHiddenByUs = 1;
}

/** @brief 恢复正常态时复显被我们隐藏的内容件（对标 setNormalMode 的
 *         `if (baseWidget && isWidgetHiddenByUs) baseWidget->show();`）。
 * @param sw 目标子窗口；可为 NULL。
 * @return 无返回值。 */
static void xmdi_showContent(XMdiSubWindow* sw)
{
    XRect r;
    if (!sw || !sw->m_widget) return;
    if (!sw->m_contentHiddenByUs) return;
    XWidget_show(sw->m_widget);
    sw->m_contentHiddenByUs = 0;
    xmdi_subWindowContentRect(sw, &r);
    XWidget_setGeometryRect(sw->m_widget, &r);
}

/**
 * @brief      进入折叠态（showShaded 的主体；对标 qmdisubwindow.cpp:
 *             2563-2646）。
 * @details    记录恢复几何 → 隐藏内容件 → 高度缩到标题带、宽度保持
 *             （Qt 为 `resize(d->internalMinimumSize)`，宽度也是 internal
 *             minimum；本框架宽度保持隐藏前值，标题条观感一致）→ 把
 *             基类窗口状态置为 WindowMinimized（Qt ensureWindowState
 *             (WindowMinimized)，本机探针 showShaded 后 windowState=0x9
 *             = WindowActive|WindowMinimized）→ 发本类 windowStateChanged
 *             （旧/新 = 本类状态位编码，Shaded 位见
 *             XMdiSubWindowState_Shaded）。
 * @param sw 目标子窗口；可为 NULL。
 * @return 无返回值。
 */
static void xmdi_enterShaded(XMdiSubWindow* sw)
{
    int oldState;
    int w;
    if (!sw) return;
    oldState = sw->m_state;
    xmdi_saveRestoreGeometry(sw);
    sw->m_shaded = 1;
    sw->m_state |= XMdiSubWindowState_Shaded;
    xmdi_hideContent(sw);
    w = XWidget_width((XWidget*)sw);
    XWidget_resize((XWidget*)sw, w, XMDI_SUBWINDOW_TITLEBAR_HEIGHT);
    /* Qt：showShaded 里 ensureWindowState(WindowMinimized)。基类状态变化
     * 会派发 WindowStateChange → 本类 changeEvent 见 m_shaded 已置位，
     * 不再进入最小化呈现。 */
    XWidget_setWindowState((XWidget*)sw,
                           (XWindowStates)((XWidget_windowState((XWidget*)sw) &
                                            ~(XWindowStates)XWindowState_Active) |
                                           (XWindowStates)XWindowState_Minimized));
    XWidget_update((XWidget*)sw);
    if (oldState != sw->m_state)
        xmdi_emitStateChanged(sw, oldState, sw->m_state);
}

/**
 * @brief      退出折叠态（对标 setNormalMode 的取消折叠分支，
 *             qmdisubwindow.cpp:1246-1288）。
 * @details    清除 Shaded 位、按记录几何恢复、复显内容件，并广播
 *             windowStateChanged。
 * @param sw 目标子窗口；可为 NULL。
 * @return 无返回值。
 */
static void xmdi_exitShaded(XMdiSubWindow* sw)
{
    int oldState;
    if (!sw || !sw->m_shaded) return;
    oldState = sw->m_state;
    sw->m_shaded = 0;
    sw->m_state &= (uint8_t)~XMdiSubWindowState_Shaded;
    xmdi_restoreNormalGeometry(sw);
    xmdi_showContent(sw);
    XWidget_update((XWidget*)sw);
    if (oldState != sw->m_state)
        xmdi_emitStateChanged(sw, oldState, sw->m_state);
}

/**
 * @brief      进入最小化呈现（对标 QMdiSubWindowPrivate::setMinimizeMode，
 *             qmdisubwindow.cpp:1218-1246）。
 * @details    Qt 把最小化子窗缩成标题带一条并摆到区域左下（本机探针：
 *             200x150 @ (50,50) → 196x24 @ (0,276)，仍可见、仍可激活）。
 *             本实现记录恢复几何、隐藏内容件、保持宽度并把窗口贴到父区
 *             底部（高度=标题带）。与折叠的区别是 isShaded() 保持 false
 *             （Qt 的 isShadeRequestFromMinimizeMode）。
 * @param sw 目标子窗口；可为 NULL。
 * @return 无返回值。
 */
static void xmdi_enterMinimized(XMdiSubWindow* sw)
{
    XWidget* parent;
    int w;
    int ph;
    if (!sw || sw->m_minimized) return;
    parent = XWidget_parentWidget((XWidget*)sw);
    if (!parent) return;
    xmdi_saveRestoreGeometry(sw);
    sw->m_minimized = 1;
    xmdi_hideContent(sw);
    w = XWidget_width((XWidget*)sw);
    ph = XWidget_height(parent);
    XWidget_setGeometry((XWidget*)sw, 0,
                        ph > XMDI_SUBWINDOW_TITLEBAR_HEIGHT
                            ? ph - XMDI_SUBWINDOW_TITLEBAR_HEIGHT : 0,
                        w, XMDI_SUBWINDOW_TITLEBAR_HEIGHT);
    XWidget_update((XWidget*)sw);
}

/**
 * @brief      退出最小化呈现（恢复几何并复显内容）。
 * @param sw 目标子窗口；可为 NULL。
 * @return 无返回值。 */
static void xmdi_exitMinimized(XMdiSubWindow* sw)
{
    if (!sw || !sw->m_minimized) return;
    sw->m_minimized = 0;
    xmdi_restoreNormalGeometry(sw);
    xmdi_showContent(sw);
    XWidget_update((XWidget*)sw);
}

/**
 * @brief      changeEvent：窗口状态变化驱动的折叠/最小化联动
 *             （对标 QMdiSubWindow::event 的 WindowStateChange 处理，
 *             qmdisubwindow.cpp:2700-2800；Qt 的 setWindowState(NoState)
 *             经 QWidget 状态机进入 setNormalMode 复原几何）。
 * @details    WindowStateChange 由 XWidget_setWindowState / showNormal 等
 *             入口直派。判据：
 *             - 折叠态 + Minimized 位被清除 ⇒ 退出折叠（恢复几何）；
 *             - 非折叠 + Minimized 位置位 ⇒ 进入最小化呈现；
 *             - 非折叠 + Minimized 位清除 + 处于最小化呈现 ⇒ 复原。
 *             其余事件交父类 changeEvent（不吞）。
 * @param self 目标控件。
 * @param event 事件。
 * @return 无返回值。
 */
static void VX_mdiSubWindow_changeEvent(XWidget* self, XEvent* event)
{
    XMdiSubWindow* sw = (XMdiSubWindow*)self;
    if (!sw || !event) return;
    if (XEvent_type(event) == XEVENT_TYPE_WINDOW_STATE_CHANGE) {
        XWindowStates state = XWidget_windowState(self);
        bool minimized =
            (state & (XWindowStates)XWindowState_Minimized) != 0;
        if (sw->m_shaded) {
            if (!minimized) xmdi_exitShaded(sw);
        } else if (minimized) {
            xmdi_enterMinimized(sw);
        } else if (sw->m_minimized) {
            xmdi_exitMinimized(sw);
        }
    }
    XClass_Parent(XWidget, EXWidget_ChangeEvent,
                  void(*)(XWidget*, XEvent*))((XWidget*)self, event);
}

/**
 * @brief      内容件标题跟随（对标 QMdiSubWindowPrivate::updateWindowTitle，
 *             qmdisubwindow.cpp:1849-1868 + eventFilter 的 WindowTitleChange
 *             分支:2704-2711）。
 * @details    跟随规则：① 内容件标题为空 ⇒ 不动作（Qt 的
 *             `if (!titleWidget || titleWidget->windowTitle().isEmpty())
 *             return;`）；② 子窗标题为空、或仍等于上一次内容件标题
 *             （lastChildWindowTitle）⇒ 采纳内容件新标题；③ 子窗标题
 *             已被改成别的值（用户自定义）⇒ 保持不动。无论是否采纳，
 *             更新 lastChildWindowTitle 为内容件当前标题（Qt 在
 *             eventFilter 里无条件更新 :2710-2711）。
 * @param sw 目标子窗口；可为 NULL。
 * @return 无返回值。
 */
static void xmdi_syncTitleFromWidget(XMdiSubWindow* sw)
{
    const XString* childTitle;
    const XString* ownTitle;
    bool follow;
    if (!sw || !sw->m_widget) return;
    childTitle = XWidget_windowTitle(sw->m_widget);
    if (!childTitle || XString_size((const XContainer*)childTitle) == 0) {
        if (!sw->m_lastChildTitle)
            sw->m_lastChildTitle = XString_create();
        if (sw->m_lastChildTitle)
            XString_assign_utf8(sw->m_lastChildTitle, "");
        return;
    }
    ownTitle = XWidget_windowTitle((XWidget*)sw);
    if (ownTitle && XString_size((const XContainer*)ownTitle) > 0) {
        follow = !sw->m_lastChildTitle ||
                 XString_size((const XContainer*)sw->m_lastChildTitle) == 0 ||
                 XString_equals(ownTitle, sw->m_lastChildTitle,
                                XChar_CaseSensitive);
    } else {
        follow = true; /* 子窗标题为空 ⇒ 始终跟随内容件 */
    }
    if (follow) XWidget_setWindowTitle((XWidget*)sw, childTitle);
    if (!sw->m_lastChildTitle) sw->m_lastChildTitle = XString_create();
    if (sw->m_lastChildTitle)
        XString_assign(sw->m_lastChildTitle, childTitle);
}

/**
 * @brief      事件过滤器：监视内容件的标题变化（对标 QMdiSubWindow 在
 *             setWidget 里 `baseWidget->installEventFilter(this)` 与
 *             eventFilter 的 WindowTitleChange 分支，qmdisubwindow.cpp:
 *             2293-2295、2704-2711）。
 * @param self 过滤器对象（子窗口）。
 * @param watched 被观察对象。
 * @param event 事件。
 * @return 恒 false（不过滤；Qt 末尾返回 QWidget::eventFilter 的结果，
 *         对本分支同为 false）。
 */
static bool VX_mdiSubWindow_eventFilter(XObject* self, XObject* watched,
                                        XEvent* event)
{
    XMdiSubWindow* sw = (XMdiSubWindow*)self;
    if (!sw || !event) return false;
    if (sw->m_widget && watched == (XObject*)sw->m_widget &&
        XEvent_type(event) == XEVENT_TYPE_WINDOW_TITLE_CHANGE)
        xmdi_syncTitleFromWidget(sw);
    return false;
}

#if XMENU_ON
/* ---- 缺省系统菜单条目槽（对标 createSystemMenu 的 SLOT 连接，
 *      qmdisubwindow.cpp:2321-2350） ---- */

/** @brief 系统菜单 Restore：showNormal。 */
static void xmdi_sysMenuRestore(XObject* receiver, XVarList* args)
{ (void)args; if (receiver) XWidget_showNormal((XWidget*)receiver); }

/** @brief 系统菜单 Minimize：showMinimized。 */
static void xmdi_sysMenuMinimize(XObject* receiver, XVarList* args)
{ (void)args; if (receiver) XWidget_showMinimized((XWidget*)receiver); }

/** @brief 系统菜单 Maximize：showMaximized。 */
static void xmdi_sysMenuMaximize(XObject* receiver, XVarList* args)
{ (void)args; if (receiver) XWidget_showMaximized((XWidget*)receiver); }

/** @brief 系统菜单 Stay on Top：翻转 WindowStaysOnTopHint。 */
static void xmdi_sysMenuStayOnTop(XObject* receiver, XVarList* args)
{
    XVarList_args_1(args, bool, checked);
    if (receiver)
        XWidget_setWindowFlag((XWidget*)receiver,
                              (XWidgetFlags)XWindowType_WindowStaysOnTopHint,
                              checked);
}

/** @brief 系统菜单 Close：close。 */
static void xmdi_sysMenuClose(XObject* receiver, XVarList* args)
{ (void)args; if (receiver) XWidget_close((XWidget*)receiver); }

/** @brief 建一条菜单项并接到子窗口槽（对齐 Qt addToSystemMenu）。
 * @return 新建动作借用指针（归菜单所有）；失败返回 NULL。 */
static XAction* xmdi_sysMenuAdd(XMdiSubWindow* sw, XMenu* menu,
                                const char* utf8,
                                void (*slot)(XObject*, XVarList*))
{
    XAction* action;
    if (!sw || !menu || !slot) return NULL;
    action = XMenu_addAction_2(menu, utf8);
    if (!action) return NULL;
    XObject_connect_1((XObject*)action, XSignal(XAction_triggered_signal),
                      (XObject*)sw, slot, XConnectionType_Direct);
    return action;
}

/**
 * @brief      构造标准系统菜单（对标 QMdiSubWindowPrivate::createSystemMenu，
 *             qmdisubwindow.cpp:2321-2350）。
 * @details    条目与 Qt 同序：&Restore / Mi&nimize / Ma&ximize /
 *             Stay on &Top（可选中）/ 分隔线 / &Close。Qt 另有
 *             &Move / &Size（交互式移动/缩放模式）；本框架标题带拖动
 *             即移动、无独立交互模式，故不列出（不留点了不应的占位项）。
 * @param sw 目标子窗口。
 * @return 新建菜单（归子窗口所有）；分配失败返回 NULL。
 */
static XMenu* xmdi_createSystemMenu(XMdiSubWindow* sw)
{
    XMenu* menu = XMenu_create();
    XAction* stayAction;
    if (!menu) return NULL;
    xmdi_sysMenuAdd(sw, menu, "&Restore", xmdi_sysMenuRestore);
    xmdi_sysMenuAdd(sw, menu, "Mi&nimize", xmdi_sysMenuMinimize);
    xmdi_sysMenuAdd(sw, menu, "Ma&ximize", xmdi_sysMenuMaximize);
    stayAction = xmdi_sysMenuAdd(sw, menu, "Stay on &Top",
                                 xmdi_sysMenuStayOnTop);
    /* 可选中态（对齐 actions[StayOnTopAction]->setCheckable(true)）。 */
    if (stayAction) XAction_setCheckable(stayAction, true);
    XMenu_addSeparator(menu);
    xmdi_sysMenuAdd(sw, menu, "&Close", xmdi_sysMenuClose);
    return menu;
}
#endif /* XMENU_ON */

static void VX_mdiSubWindow_deinit(XMdiSubWindow* self)
{
    if (!self) return;
    if (self->m_widget) {
        /* 摘过滤器必须先于删内容件：XObject 析构按 m_watchedObjects
         * 回落 m_filters（XObject.c deinit 反注册段），内容件先释放会
         * 留下悬垂被观察指针。 */
        XObject_removeEventFilter((XObject*)self->m_widget, (XObject*)self);
        XClassDelete(self->m_widget);
        self->m_widget = NULL;
    }
    if (self->m_lastChildTitle) {
        XClassDelete((XClass*)self->m_lastChildTitle);
        self->m_lastChildTitle = NULL;
    }
    if (self->m_systemMenu) {
        XClassDelete((XClass*)self->m_systemMenu);
        self->m_systemMenu = NULL;
    }
    XClass_Deinit_Parent(XWidget, (XWidget*)self);
}

/* SizeHint 虚槽桥接（对标 Qt 的 sizeHint 虚函数）。 */
static XSize VXMdiSubWindow_sizeHintSlot(const XWidget* self);
static XSize VXMdiSubWindow_minimumSizeHintSlot(const XWidget* self);

static XSize VXMdiSubWindow_sizeHintSlot(const XWidget* self)
{
    return XMdiSubWindow_sizeHint((const XMdiSubWindow*)self);
}

static XSize VXMdiSubWindow_minimumSizeHintSlot(const XWidget* self)
{
    return XMdiSubWindow_minimumSizeHint((const XMdiSubWindow*)self);
}

XVtable* XMdiSubWindow_class_init(void)
{
    XVTABLE_INIT_DEFAULT(XMdiSubWindow)
    XVTABLE_INHERIT_XCLASS(XWidget);
    XVTABLE_OVERLOAD_DEFAULT(EXWidget_SizeHint, VXMdiSubWindow_sizeHintSlot);
    XVTABLE_OVERLOAD_DEFAULT(EXWidget_MinimumSizeHint, VXMdiSubWindow_minimumSizeHintSlot);
    XVTABLE_OVERLOAD_DEFAULT(EXWidget_PaintEvent,
                             VX_mdiSubWindow_paintEvent);
    XVTABLE_OVERLOAD_DEFAULT(EXWidget_ResizeEvent,
                             VX_mdiSubWindow_resizeEvent);
    XVTABLE_OVERLOAD_DEFAULT(EXWidget_MousePressEvent,
                             VX_mdiSubWindow_mousePressEvent);
    XVTABLE_OVERLOAD_DEFAULT(EXWidget_MouseMoveEvent,
                             VX_mdiSubWindow_mouseMoveEvent);
    XVTABLE_OVERLOAD_DEFAULT(EXWidget_MouseReleaseEvent,
                             VX_mdiSubWindow_mouseReleaseEvent);
    XVTABLE_OVERLOAD_DEFAULT(EXWidget_ChangeEvent,
                             VX_mdiSubWindow_changeEvent);
    XVTABLE_OVERLOAD_DEFAULT(EXObject_EventFilter,
                             VX_mdiSubWindow_eventFilter);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Deinit, VX_mdiSubWindow_deinit);
    return XVTABLE_DEFAULT;
}

void XMdiSubWindow_init(XMdiSubWindow* self, XWidget* parent,
                        XWidgetFlags flags)
{
    XWidgetFlags mdiFlags;
    if (!self) return;
    XMemset(self, 0, sizeof(*self));
    /* 对标 QMdiSubWindow 构造（qmdisubwindow.cpp:2202-2210）：
     * `QWidget(parent, 0)` 经 QWidget 的窗口标志规范化后得到
     * 0x800f012（SubWindow 类型 + WindowTitleHint|WindowSystemMenuHint|
     * WindowMinimizeButtonHint|WindowMaximizeButtonHint|
     * WindowCloseButtonHint，本机 Qt 6.8.0 探针实测 addSubWindow 后
     * windowFlags==0x800f012）；无父构造实测 0x8800f001（Window 类型 +
     * 同一组提示位）。缺省窗口外观与标题带按钮的可用性都取决于这些
     * 提示位，故类型位缺省时补齐类型并并入提示位；调用方显式给了
     * FramelessWindowHint 则不加提示位（Qt 的 sizeParameters 同判据）。 */
    mdiFlags = flags;
    if (!(mdiFlags & (XWidgetFlags)XWindowType_TypeMask))
        mdiFlags |= (XWidgetFlags)(parent ? XWindowType_SubWindow
                                          : XWindowType_Window);
    if (!(mdiFlags & (XWidgetFlags)XWindowType_FramelessWindowHint))
        mdiFlags |= (XWidgetFlags)(
            (XWidgetFlags)XWindowType_WindowTitleHint |
            (XWidgetFlags)XWindowType_WindowSystemMenuHint |
            (XWidgetFlags)XWindowType_WindowMinimizeButtonHint |
            (XWidgetFlags)XWindowType_WindowMaximizeButtonHint |
            (XWidgetFlags)XWindowType_WindowCloseButtonHint);
    XWidget_init(&self->m_base, parent, mdiFlags);
    XClassSetVtable(self, XMdiSubWindow);
    Set_Class_Memory(self, XCLASS_DEFAULT_MEMORY_TYPE);
    Set_Class_IsHeap(self, false);
    XWidget_resize((XWidget*)self, 200, 150);
    /* 默认值与 Qt QMdiSubWindowPrivate 一致。 */
    self->m_keyboardSingleStep = 5;
    self->m_keyboardPageStep = 20;
    /* 构造即建标准系统菜单（qmdisubwindow.cpp:2202-2210 调
     * createSystemMenu；探针 systemMenu() 恒非空，含 Restore/Minimize/
     * Maximize/StayOnTop/Close）。 */
#if XMENU_ON
    self->m_systemMenu = xmdi_createSystemMenu(self);
#endif
}

XMdiSubWindow* XMdiSubWindow_create_ex(XMemoryType memory, XWidget* parent,
                                       XWidgetFlags flags)
{
    XMdiSubWindow* self =
        (XMdiSubWindow*)XMemory_malloc(sizeof(*self), memory);
    if (!self) return NULL;
    XMdiSubWindow_init(self, parent, flags);
    Set_Class_Memory(self, memory);
    Set_Class_IsHeap(self, true);
    return self;
}

void XMdiSubWindow_setWidget(XMdiSubWindow* self, XWidget* widget)
{
    XRect r;
    if (!self) return;
    /* 对标 QMdiSubWindow::setWidget（qmdisubwindow.cpp:2257-2287）：
       同指针重设幂等返回（Qt 同款护栏）；widget 为 NULL 等价
       removeBaseWidget——摘除旧件不销毁；不同指针先摘旧件再装新件
       （替换语义可用，此前以 `if (m_widget) return` 静默拒绝）。 */
    if (widget == self->m_widget) return;
    if (self->m_widget) {
        XWidget* old = self->m_widget;
        const XString* ownTitle = XWidget_windowTitle((XWidget*)self);
        const XString* oldTitle = XWidget_windowTitle(old);
        /* removeBaseWidget 对标（qmdisubwindow.cpp:985-1005）：
           ① 摘事件过滤器（否则旧件销毁后本对象在旧件 m_filters 里
              留悬垂：XObject 析构反注册段会回落读旧件的 m_filters）；
           ② 若子窗标题正是从旧内容件继承来的（旧件标题 == 子窗标题），
              清空子窗标题（Qt 的 `if (baseWidget->windowTitle() ==
              q->windowTitle()) { q->setWindowTitle(QString()); ... }`）；
           ③ 父子链交还调用方（不删，所有权归还）。 */
        XObject_removeEventFilter((XObject*)old, (XObject*)self);
        if (oldTitle && ownTitle &&
            XString_size((const XContainer*)ownTitle) > 0 &&
            XString_equals(oldTitle, ownTitle, XChar_CaseSensitive)) {
            XWidget_setWindowTitle((XWidget*)self, NULL);
        }
        XWidget_setParent(old, NULL, 0);
        self->m_widget = NULL;
    }
    if (!widget) {
        if (self->m_lastChildTitle) {
            XClassDelete((XClass*)self->m_lastChildTitle);
            self->m_lastChildTitle = NULL;
        }
        return;
    }
    self->m_widget = widget;
    XWidget_setParent(widget, (XWidget*)self, 0);
    /* 内容件内收 1px 挂载（x=1 起，底留框线行）：chrome 窗框绘制后
     * 侧框线不再被内容件覆盖；标题带区（0..19 行）仍为拖拽命中域。 */
    xmdi_subWindowContentRect(self, &r);
    XWidget_setGeometryRect(widget, &r);
    /* 对标 Qt（qmdisubwindow.cpp:2293-2299）：
     *   baseWidget->installEventFilter(this);
     *   if (windowTitle().isEmpty()) updateWindowTitle(true);
     *   lastChildWindowTitle = baseWidget->windowTitle();
     * ——装标题跟随过滤器；子窗标题为空时采纳内容件标题；记录上一次
     * 内容件标题供 eventFilter 的跟随判定。 */
    XObject_installEventFilter((XObject*)widget, (XObject*)self);
    {
        const XString* selfTitle = XWidget_windowTitle((XWidget*)self);
        if (!selfTitle || XString_size((const XContainer*)selfTitle) == 0) {
            const XString* wt = XWidget_windowTitle(widget);
            if (wt && XString_size((const XContainer*)wt) > 0)
                XWidget_setWindowTitle((XWidget*)self, wt);
        }
    }
    {
        const XString* wt = XWidget_windowTitle(widget);
        if (!self->m_lastChildTitle) self->m_lastChildTitle = XString_create();
        if (self->m_lastChildTitle) {
            if (wt) XString_assign(self->m_lastChildTitle, wt);
            else XString_assign_utf8(self->m_lastChildTitle, "");
        }
    }
}

XWidget* XMdiSubWindow_widget(const XMdiSubWindow* self)
{
    return self ? self->m_widget : NULL;
}

void XMdiSubWindow_setWindowTitle_2(XMdiSubWindow* self, const char* utf8)
{
    if (!self) return;
    /* 对标 QMdiSubWindow：标题即基类 QWidget::windowTitle 属性
     * （qmdisubwindow.cpp:994 的 updateWindowTitle 与标题条绘制同源），
     * 本接口只是 UTF-8 便捷重载，不再维护独立字段。 */
    XWidget_setWindowTitle_2((XWidget*)self, utf8 ? utf8 : "");
    XWidget_update((XWidget*)self);
}

const char* XMdiSubWindow_windowTitle_2(const XMdiSubWindow* self)
{
    const XString* title;
    const char* text;
    if (!self) return "";
    title = XWidget_windowTitle((const XWidget*)self);
    if (!title) return "";
    text = XString_toUtf8(title);
    return text ? text : "";
}

/* ==================== XMdiArea ==================== */

static void xmdi_emitActivated(XMdiArea* self, XMdiSubWindow* window)
{
    XVarList* args = XVarList_Create(XVar(XMdiSubWindow*, window));
    if (!args) return;
    if (self && ((XObject*)self)->m_signalSlot) {
        XObject_emitSignal((XObject*)self,
            (size_t)XMdiArea_subWindowActivated_signal, args,
            NULL, NULL, XEVENT_PRIORITY_NORMAL);
    } else {
        XVarList_delete(args);
    }
}

static void xmdi_emitAboutToActivate(XMdiSubWindow* sw)
{
    if (!sw) return;
    if (((XObject*)sw)->m_signalSlot) {
        XObject_emitSignal((XObject*)sw,
            (size_t)XMdiSubWindow_aboutToActivate_signal,
            NULL, NULL, NULL, XEVENT_PRIORITY_NORMAL);
    }
}

static void xmdi_emitStateChanged(XMdiSubWindow* sw,
                                  int oldState, int newState)
{
    XVarList* args;
    if (!sw) return;
    args = XVarList_Create(XVar(int, oldState), XVar(int, newState));
    if (!args) return;
    if (((XObject*)sw)->m_signalSlot) {
        XObject_emitSignal((XObject*)sw,
            (size_t)XMdiSubWindow_windowStateChanged_signal, args,
            NULL, NULL, XEVENT_PRIORITY_NORMAL);
    } else {
        XVarList_delete(args);
    }
}

/**
 * @brief      派生判定子窗是否仍处 tileSubWindows 铺出的网格态
 *             （对标 Qt 6.8.3 QMdiAreaPrivate::isSubWindowsTiled）。
 * @details    根因（二次复扫 R-24）：resizeEvent 曾无条件 tileSubWindows，
 *             默认 SubWindowView 下用户摆位每次区域尺寸变化即被摧毁。
 *             Qt 6.8.3 的 resizeEvent 仅在 isSubWindowsTiled 为真时重铺
 *             （qmdiarea.cpp:2288），该标志由 tileSubWindows 置位、用户
 *             移动/缩放子窗清零。本实现 XMdiArea 结构体在头文件中
 *             （本修复不可改头文件）且子窗无 move/resize 事件挂点，无
 *             字段承载该标志，故按几何派生：平铺网格（cols=2）的特征
 *             是全部子窗等宽等高、第 i 窗位于基准窗 +(列*宽, 行*高)
 *             的网格点。区域 resize 不移动子窗、相对关系保持 → 判定为
 *             平铺并随新尺寸重铺；用户挪动任一子窗即破坏网格 → 不再
 *             重铺。单子窗（n==1）时"平铺铺满整域"与"用户随意摆位"
 *             在 resize 后几何上不可区分，保守判否不重铺，保证永不毁
 *             摆位（退化：单子窗少了"铺满新域"一步）。
 */
static bool xmdi_isTiled(const XMdiArea* self)
{
    int64_t i;
    int64_t n;
    XMdiSubWindow* base;
    int baseX;
    int baseY;
    int baseW;
    int baseH;
    if (!self || !self->m_subWindows) return false;
    n = XVector_size_base((const XContainer*)self->m_subWindows);
    if (n < 2) return false;
    base = *(XMdiSubWindow**)XVector_at_base(self->m_subWindows, 0);
    if (!base) return false;
    baseX = XWidget_x((XWidget*)base);
    baseY = XWidget_y((XWidget*)base);
    baseW = XWidget_width((XWidget*)base);
    baseH = XWidget_height((XWidget*)base);
    for (i = 1; i < n; ++i) {
        XMdiSubWindow* sw =
            *(XMdiSubWindow**)XVector_at_base(self->m_subWindows, i);
        int col = (int)(i % 2); /* 与 XMdiArea_tileSubWindows 的列布局一致。 */
        int row = (int)(i / 2);
        if (!sw) return false;
        if (XWidget_x((XWidget*)sw) != baseX + col * baseW ||
            XWidget_y((XWidget*)sw) != baseY + row * baseH ||
            XWidget_width((XWidget*)sw) != baseW ||
            XWidget_height((XWidget*)sw) != baseH)
            return false;
    }
    return true;
}

static void VX_mdiArea_resizeEvent(XWidget* self, XEvent* event)
{
    if (!self) return;
    /* 链回基类视口/滚动条重排（框架口径同 XScrollArea 的
       XClass_Parent(XAbstractScrollArea, EXWidget_ResizeEvent) 链；
       Qt 中该职责由 QAbstractScrollArea 私有排版机制承担，本框架
       落在 VX_asa_resizeEvent）。此前未链回：MDI 区被页签容器拉伸
       后视口停留 creation 尺寸 200x150——addSubWindow 的级联摆位
       按视口宽高钳位把子窗 2 钳回 (0,0)，与子窗 1 完全叠死（文档 1
       标签被盖、两条蓝标题条重合成一条），滚动条排版也永不发生
       （gdb 树遍历实证：viewport rect=(0,0 200x150) 而 ASA
       rect=(0,0 752x430)）。 */
    XClass_Parent(XAbstractScrollArea, EXWidget_ResizeEvent,
                  XWidgetEventSlot)(self, event);
    /* 对标 Qt 6.8.3 QMdiArea::resizeEvent（qmdiarea.cpp:2273-2291）：
       仅子窗仍处平铺网格态时才随区域尺寸重铺；默认 SubWindowView 下
       用户摆位不再被无条件摧毁。 */
    if (xmdi_isTiled((const XMdiArea*)self))
        XMdiArea_tileSubWindows((XMdiArea*)self);
}

/** @brief 绘制：只填充 paint 事件携带的脏区（对标 QMdiArea::paintEvent，
 *         qmdiarea.cpp:2669-2674「for (const QRect &exposedRect :
 *         paintEvent->region()) fillRect(exposedRect, background)」）。
 * @details 根因（night #50 交互擦除）：基类 VX_asa_paintEvent 恒以
 *          (0,0,w,h) 整幅涂写——XWidget_paintTree 按单矩形逐个派发且
 *          各 widget 绘制不受该矩形硬裁剪（仅表面裁剪=整区域外接框），
 *          于是 ①脏区外子窗像素在部分重绘期间即被抹白（后备帧内）；
 *          ②多矩形区域逐矩形派发时，后派矩形的整幅涂抹抹掉先派矩形
 *          刚恢复的子窗内容，整区域一次性 present 后即白屏（点击/
 *          拖拽后子窗铬架+内容全空的实锚：gdb 探针 pe=(0,0 200x150)
 *          全量子窗重绘后紧随的另一矩形派发把标题条抹白）。改为只涂
 *          事件脏区（含底边框线也按脏区裁剪），每矩形派发只改动自身
 *          矩形，多矩形序列可交换且与 present 区域严格闭合。 */
static void VX_mdiArea_paintEvent(XWidget* self, XEvent* event)
{
    XPainter painter;
    XImage* image;
    XPoint offset;
    XRect rect;
    XRect line;
    uint32_t base;
    uint32_t mid;
    int w;
    int h;
    if (!self || !event || XEvent_type(event) != XEVENT_TYPE_PAINT) return;
    w = XWidget_width(self);
    h = XWidget_height(self);
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
#if XPALETTE_ON
    {
        XPalette palette = XWidget_palette(self);
        XColor c;
        c = XPalette_color(&palette, XPaletteColorGroup_Current,
                           XPaletteColorRole_Base);
        base = XColor_rgba(&c);
        c = XPalette_color(&palette, XPaletteColorGroup_Current,
                           XPaletteColorRole_Mid);
        mid = XColor_rgba(&c);
    }
#else
    base = 0xFFFFFFFFu;
    mid = 0xFF808080u;
#endif /* XPALETTE_ON */
    /* 脏区∩控件矩形：事件矩形可能越出控件边界，先收进自身范围
       （同 XWidget_paintEvent_default 的裁剪口径）。 */
    rect = ((const XPaintEvent*)event)->m_rect;
    if (rect.x < 0) { rect.width += rect.x; rect.x = 0; }
    if (rect.y < 0) { rect.height += rect.y; rect.y = 0; }
    if (rect.x + rect.width > w) rect.width = w - rect.x;
    if (rect.y + rect.height > h) rect.height = h - rect.y;
    if (rect.width > 0 && rect.height > 0)
        XPainter_fillRect(&painter, &rect, base);
    /* 底边框线（同基类的 Mid 1px 线）：仅绘制落在脏区内的段落。 */
    if (h > 0 && (h - 1) >= rect.y &&
        (h - 1) < rect.y + rect.height && rect.width > 0) {
        XRect_init(&line, rect.x, h - 1, rect.width, 1);
        XPainter_fillRect(&painter, &line, mid);
    }
    XPainter_deinit(&painter);
}

static void VX_mdiArea_deinit(XMdiArea* self)
{
    if (!self) return;
    if (self->m_subWindows) {
        int64_t i;
        int64_t n = XVector_size_base(
            (const XContainer*)self->m_subWindows);
        for (i = 0; i < n; ++i) {
            XMdiSubWindow** sw =
                (XMdiSubWindow**)XVector_at_base(self->m_subWindows, i);
            if (sw && *sw)
                XClassDelete((XClass*)*sw);
        }
        XClassDelete(self->m_subWindows);
        self->m_subWindows = NULL;
    }
    if (self->m_activationHistory) {
        /* 借用指针账本：只释放数组（子窗口已在上方级联销毁）。 */
        XClassDelete(self->m_activationHistory);
        self->m_activationHistory = NULL;
    }
    XClass_Deinit_Parent(XAbstractScrollArea, (XAbstractScrollArea*)self);
}

XVtable* XMdiArea_class_init(void)
{
    XVTABLE_INIT_DEFAULT(XMdiArea)
    XVTABLE_INHERIT_XCLASS(XAbstractScrollArea);
    XVTABLE_OVERLOAD_DEFAULT(EXWidget_ResizeEvent, VX_mdiArea_resizeEvent);
    XVTABLE_OVERLOAD_DEFAULT(EXWidget_PaintEvent, VX_mdiArea_paintEvent);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Deinit, VX_mdiArea_deinit);
    return XVTABLE_DEFAULT;
}

void XMdiArea_init(XMdiArea* self, XWidget* parent, XWidgetFlags flags)
{
    if (!self) return;
    XMemset(self, 0, sizeof(*self));
    XAbstractScrollArea_init(&self->m_base, parent, flags);
    XClassSetVtable(self, XMdiArea);
    Set_Class_Memory(self, XCLASS_DEFAULT_MEMORY_TYPE);
    Set_Class_IsHeap(self, false);
    self->m_subWindows = XVector_Create(XMdiSubWindow*);
    /* 激活历史账本（最近激活在前；对标 QMdiAreaPrivate::
     * indicesToActivatedChildren，qmdiarea.cpp:812/1018-1024）。 */
    self->m_activationHistory = XVector_Create(XMdiSubWindow*);
    self->m_active = NULL;
    self->m_viewMode = (int)XMdiAreaViewMode_SubWindowView;
    self->m_background = 0;
    self->m_tabPosition = 0;
    self->m_tabShape = 0;
    self->m_tabsMovable = false;
    self->m_tabsClosable = false;
    self->m_activationOrder = (int)XMdiAreaWindowOrder_CreationOrder;
    self->m_options = 0;
    self->m_documentMode = false;
}

XMdiArea* XMdiArea_create_ex(XMemoryType memory, XWidget* parent,
                             XWidgetFlags flags)
{
    XMdiArea* self = (XMdiArea*)XMemory_malloc(sizeof(*self), memory);
    if (!self) return NULL;
    XMdiArea_init(self, parent, flags);
    Set_Class_Memory(self, memory);
    Set_Class_IsHeap(self, true);
    return self;
}

XMdiSubWindow* XMdiArea_addSubWindow(XMdiArea* self, XWidget* widget)
{
    XMdiSubWindow* sw;
    XWidget* viewport;
    if (!self || !widget || !self->m_subWindows) return NULL;
    /* 子窗挂到视口（对标 QMdiAreaPrivate::appendChild，qmdiarea.cpp:
     * 791-797「child->setParent(viewport, ...)」）：挂到区域本体时子窗
     * 与视口为兄弟、被视口绘制次序压住且几何/裁剪口径错位——表现为
     * 子窗不可见仅剩残迹。 */
    viewport = XAbstractScrollArea_viewport((XAbstractScrollArea*)self);
    if (xmdi_isSubWindow(widget)) {
        /* 对标 QMdiArea::addSubWindow 的 qobject_cast 分支
         * （qmdiarea.cpp:1949-1957）：widget 本身已是子窗口 ⇒ **直接接管**
         * ——只把父链挂到视口并入列，不新建外壳、不动其内容件、不置
         * WA_DeleteOnClose（Qt 只给内部新建的那些置该属性）。已在区域内的
         * 子窗 Qt 只告警并原样返回（qmdiarea.cpp:1951-1954），本实现同。 */
        sw = (XMdiSubWindow*)widget;
        if (xmdi_subWindowIndex(self, sw) >= 0) return sw;
        /* 对标 qmdiarea.cpp:1955 `child->setParent(viewport(), windowFlags
         * ? windowFlags : child->windowFlags())` + QWidget 换父的类型位
         * 规范化：独立子窗的 Window 类型位被清掉、统一成 SubWindow，
         * 非类型提示位保留（本机 Qt 6.8.0 探针：独立子窗
         * flags=0x8800f001 → addSubWindow 后 0x800f012、isWindow=0）。 */
        {
            XWidgetFlags subFlags =
                (XWidget_windowFlags(widget) &
                 ~(XWidgetFlags)XWindowType_TypeMask) |
                (XWidgetFlags)XWindowType_SubWindow;
            XWidget_setParent(widget, viewport ? viewport : (XWidget*)self,
                              subFlags);
        }
    } else {
        sw = XMdiSubWindow_create_ex(XCLASS_DEFAULT_MEMORY_TYPE,
                                     viewport ? viewport : (XWidget*)self, 0);
        if (!sw) return NULL;
        /* 对标 qmdiarea.cpp:1962 `child->setAttribute(Qt::WA_DeleteOnClose)`：
         * 内部创建的子窗关闭即回收（QMdiArea 不另持所有权账本）；
         * XMdiArea_closeAllSubWindows/closeActiveSubWindow 据此判定是否
         * 从数组摘除并延迟释放。 */
        XWidget_setAttribute((XWidget*)sw, XWidgetAttribute_DeleteOnClose,
                             true);
        XMdiSubWindow_setWidget(sw, widget);
    }
    /* 级联摆放（对标 appendChild 的 place(placer, child)：新子窗不全
       等位叠死在前一窗上）；越界时回落视口原点。 */
    {
        int64_t index = XVector_size_base(
            (const XContainer*)self->m_subWindows);
        int step = 24;
        int vw = viewport ? XWidget_width(viewport) : 0;
        int vh = viewport ? XWidget_height(viewport) : 0;
        int swx = (int)(index % 8) * step;
        int swy = (int)(index % 8) * step;
        int swW = XWidget_width((XWidget*)sw);
        int swH = XWidget_height((XWidget*)sw);
        if (vw > 0 && swx + swW > vw) swx = 0;
        if (vh > 0 && swy + swH > vh) swy = 0;
        XWidget_move((XWidget*)sw, swx, swy);
    }
    XVector_push_back_1_base(self->m_subWindows, &sw);
    xmdi_historyAdd(self, sw);
    XWidget_show((XWidget*)sw);
    XMdiArea_setActiveSubWindow(self, sw);
    return sw;
}

void XMdiArea_removeSubWindow(XMdiArea* self, XWidget* widget)
{
    int64_t i;
    int64_t n;
    if (!self || !self->m_subWindows || !widget) return;
    n = XVector_size_base((const XContainer*)self->m_subWindows);
    /* ① widget 本身是子窗口：摘父链 + 移出数组，**不删除对象**
     *    （qmdiarea.cpp:1984-1999；Qt 文档「widget is never actually
     *    deleted by QMdiArea」）。活动窗被移出时按 updateActiveWindow
     *    口径改激活下一个可见子窗。 */
    if (xmdi_isSubWindow(widget)) {
        i = xmdi_subWindowIndex(self, (const XMdiSubWindow*)widget);
        if (i < 0) return; /* 未登记：Qt 告警后直接返回 */
        XVector_remove_base(self->m_subWindows, i, 1);
        xmdi_historyRemove(self, (XMdiSubWindow*)widget);
        xmdi_reactivateAfterRemove(self, (XMdiSubWindow*)widget);
        /* 摘父链（转顶层）但保留其余窗口标志（Qt setParent(nullptr) 只
         * 把类型位转成 Window，标题带按钮提示位不丢）。 */
        XWidget_setParent(widget, NULL, XWidget_windowFlags(widget));
        return;
    }
    /* ② widget 是某子窗的内容件：**只把内容件从子窗摘除**，子窗口
     *    保留在 subWindowList 里（qmdiarea.cpp:2004-2016 与官方文档
     *    「if an internal widget is passed in, the child widget is set to
     *    nullptr and the QMdiSubWindow is *not* removed」；本机 Qt 6.8.0
     *    探针：removeSubWindow(内容件) 后 subWindowList().size() 不变、
     *    child->widget()==nullptr、content->parentWidget()==nullptr、子窗
     *    仍可见）。内容件摘链后交还调用方（QMdiSubWindow::setWidget
     *    (nullptr) 即 removeBaseWidget，qmdisubwindow.cpp:985-1005）。 */
    for (i = 0; i < n; ++i) {
        XMdiSubWindow** sw =
            (XMdiSubWindow**)XVector_at_base(self->m_subWindows, i);
        if (sw && *sw && XMdiSubWindow_widget(*sw) == widget) {
            XMdiSubWindow_setWidget(*sw, NULL);
            return;
        }
    }
}

const XVector* XMdiArea_subWindowList(const XMdiArea* self)
{
    return self ? self->m_subWindows : NULL;
}

XVector* XMdiArea_subWindowList_2(const XMdiArea* self, int order)
{
    XVector* out;
    int64_t i;
    int64_t n;
    out = XVector_Create(XMdiSubWindow*);
    if (!out) return NULL;
    if (!self) return out;
    if (order == (int)XMdiAreaWindowOrder_StackingOrder) {
        /* 视口子控件顺序 = 叠加次序：XWidget_raise/lower/stackUnder 在
         * 父控件子数组中移动（XWidget.c XWidget_raise），与 Qt 的
         * viewport->children() 顺序同构（qmdiarea.cpp:1360-1371）。 */
        XWidget* viewport =
            XAbstractScrollArea_viewport((XAbstractScrollArea*)self);
        const XVector* children =
            viewport ? XObject_children((XObject*)viewport) : NULL;
        if (children) {
            n = XVector_size_base((const XContainer*)children);
            for (i = 0; i < n; ++i) {
                XObject* obj = *(XObject**)XVector_at_base(children, i);
                XMdiSubWindow* sw;
                if (!obj || !obj->m_is_widget) continue;
                sw = (XMdiSubWindow*)obj;
                if (xmdi_subWindowIndex(self, sw) < 0) continue;
                XVector_push_back_1_base(out, &sw);
            }
        }
        return out;
    }
    if (order == (int)XMdiAreaWindowOrder_ActivationHistoryOrder &&
        self->m_activationHistory) {
        /* 激活历史由旧到新：账本「最近激活在前」，逆序输出
         * （qmdiarea.cpp:1372-1385 从 indicesToActivatedChildren 尾部
         * 向前遍历）。 */
        n = XVector_size_base((const XContainer*)self->m_activationHistory);
        for (i = n; i > 0; --i) {
            XMdiSubWindow** slot = (XMdiSubWindow**)XVector_at_base(
                self->m_activationHistory, i - 1);
            if (slot && *slot)
                XVector_push_back_1_base(out, slot);
        }
        return out;
    }
    /* CreationOrder（缺省）：登记（创建）顺序。 */
    if (self->m_subWindows) {
        n = XVector_size_base((const XContainer*)self->m_subWindows);
        for (i = 0; i < n; ++i) {
            XMdiSubWindow** slot =
                (XMdiSubWindow**)XVector_at_base(self->m_subWindows, i);
            if (slot && *slot) XVector_push_back_1_base(out, slot);
        }
    }
    return out;
}

XMdiSubWindow* XMdiArea_activeSubWindow(const XMdiArea* self)
{
    return self ? self->m_active : NULL;
}

void XMdiArea_setActiveSubWindow(XMdiArea* self, XMdiSubWindow* window)
{
    if (!self) return;
    if (!window) {
        /* 对标 qmdiarea.cpp:1805-1811：setActiveSubWindow(nullptr) →
           activateWindow(nullptr) → 失活当前窗并经 resetActiveWindow
           发射 subWindowActivated(nullptr)（qmdiarea.cpp:957-963）。
           此前 NULL 直接到 return，无法触发失活。区域为空时 Qt 的
           activateWindow 亦直接返回（qmdiarea.cpp:951-955）——无活动
           窗时同样不发射。 */
        if (!self->m_active) return;
        {
            /* 失活只清「活动态」相关的位；折叠位（Shaded）是窗口自身
             * 形态，不随失活清掉（否则重新激活时 windowStateChanged
             * 载荷与 isShaded() 失同步）。 */
            int oldState = self->m_active->m_state;
            int newState = oldState & XMdiSubWindowState_Shaded;
            xmdi_emitStateChanged(self->m_active, oldState, newState);
            self->m_active->m_state = (uint8_t)newState;
        }
        XWidget_update((XWidget*)self->m_active);
        self->m_active = NULL;
        xmdi_emitActivated(self, NULL);
        return;
    }
    if (self->m_active == window) return;
    if (self->m_active) {
        int oldState = self->m_active->m_state;
        int newState = oldState & XMdiSubWindowState_Shaded;
        xmdi_emitStateChanged(self->m_active, oldState, newState);
        self->m_active->m_state = (uint8_t)newState;
        /* 活动态驱动标题带配色（paintEvent 激活分支），失活窗立即
           重绘，否则保留 Highlight 底的陈旧像素。 */
        XWidget_update((XWidget*)self->m_active);
    }
    xmdi_emitAboutToActivate(window);
    self->m_active = window;
    /* 激活历史账本（对标 activateWindow 的
     * `indicesToActivatedChildren.move(index, 0)`，qmdiarea.cpp:1018-1024）。 */
    xmdi_historyRaise(self, window);
    /* 激活即置顶（对标 activateWindow 的 internalRaise(activeWindow)，
     * qmdiarea.cpp:1025）：子控件数组次序即绘制/叠加次序
     * （XWidget_raise 把子件移到父 children 末尾），StackingOrder 因此
     * 反映最近激活的窗口在最上层。 */
    XWidget_raise((XWidget*)window);
    xmdi_emitStateChanged(window, 0, window->m_state);
    xmdi_emitActivated(self, window);
    XWidget_update((XWidget*)window);
}

void XMdiArea_closeAllSubWindows(XMdiArea* self)
{
    int64_t i;
    XMdiSubWindow* activeBefore;
    if (!self || !self->m_subWindows) return;
    activeBefore = self->m_active;
    /* 逆序遍历（删除后下标不漂移）；**不用**同步列表拷贝——close 可能
     * 重入改数组（对齐 qmdiarea.cpp:1863-1871 先拷贝 childWindows 的
     * 动机），每步按当前长度重读槽位。 */
    i = XVector_size_base((const XContainer*)self->m_subWindows) - 1;
    for (; i >= 0; --i) {
        XMdiSubWindow** slot;
        XMdiSubWindow* sw;
        bool deleteOnClose;
        if (i >= XVector_size_base((const XContainer*)self->m_subWindows))
            continue; /* 重入已缩短数组 */
        slot = (XMdiSubWindow**)XVector_at_base(self->m_subWindows, i);
        if (!slot || !*slot) continue;
        sw = *slot;
        deleteOnClose = XWidget_testAttribute(
            (XWidget*)sw, XWidgetAttribute_DeleteOnClose);
        /* 对标 qmdiarea.cpp:1857-1873（逐窗 `child->close()`）：**走关闭
         * 事件**——忽略关闭事件的子窗口保持打开（Qt 文档明写
         * 「Subwindows that ignore the close event will remain open」），
         * 旧实现无条件 deleteLater 会吞掉该否决。 */
        if (!XWidget_close((XWidget*)sw)) continue;
        /* 非内部创建（未被赋予 DeleteOnClose）的子窗仅隐藏，仍留在
         * subWindowList 里——本机 Qt 6.8.0 实测：用户自建
         * QMdiSubWindow 经 addSubWindow 后 close() 返回 1、对象存活、
         * subWindowList().size() 不变。 */
        if (!deleteOnClose) continue;
        XVector_remove_base(self->m_subWindows, i, 1);
        xmdi_historyRemove(self, sw);
        XObject_deleteLater((XObject*)sw);
    }
    /* 活动窗被关闭（或被隐藏）后收口激活：还剩可见子窗则激活之，
     * 否则发 subWindowActivated(nullptr)（qmdiarea.cpp:1049-1126）。 */
    if (activeBefore && self->m_active == activeBefore &&
        (xmdi_subWindowIndex(self, activeBefore) < 0 ||
         XWidget_isHidden((XWidget*)activeBefore)))
        xmdi_reactivateAfterRemove(self, activeBefore);
}

void XMdiArea_cascadeSubWindows(XMdiArea* self)
{
    int64_t i;
    int64_t n;
    int offset = 0;
    int w = XWidget_width((XWidget*)self);
    int h = XWidget_height((XWidget*)self);
    if (!self || !self->m_subWindows) return;
    n = XVector_size_base((const XContainer*)self->m_subWindows);
    for (i = 0; i < n; ++i) {
        XMdiSubWindow** sw =
            (XMdiSubWindow**)XVector_at_base(self->m_subWindows, i);
        XRect r;
        if (!sw || !*sw) continue;
        XRect_init(&r, offset, offset,
                   w > 60 ? w - 40 - offset : 200,
                   h > 60 ? h - 40 - offset : 150);
        XWidget_setGeometryRect((XWidget*)*sw, &r);
        offset += 24;
    }
}

void XMdiArea_tileSubWindows(XMdiArea* self)
{
    int64_t i;
    int64_t n;
    int count;
    int cols = 2;
    int rows;
    int w = XWidget_width((XWidget*)self);
    int h = XWidget_height((XWidget*)self);
    if (!self || !self->m_subWindows) return;
    n = XVector_size_base((const XContainer*)self->m_subWindows);
    count = (int)n;
    if (count <= 0) return;
    rows = (count + cols - 1) / cols;
    for (i = 0; i < n; ++i) {
        XMdiSubWindow** sw =
            (XMdiSubWindow**)XVector_at_base(self->m_subWindows, i);
        XRect r;
        int col = (int)(i % cols);
        int row = (int)(i / cols);
        if (!sw || !*sw) continue;
        XRect_init(&r, col * w / cols, row * h / rows,
                   w / cols, h / rows);
        XWidget_setGeometryRect((XWidget*)*sw, &r);
    }
}

void XMdiArea_setViewMode(XMdiArea* self, XMdiAreaViewMode mode)
{
    if (!self) return;
    self->m_viewMode = (int)mode;
}

XMdiAreaViewMode XMdiArea_viewMode(const XMdiArea* self)
{
    return self ? (XMdiAreaViewMode)self->m_viewMode
                : XMdiAreaViewMode_SubWindowView;
}

int XMdiArea_subWindowCount(const XMdiArea* self)
{
    return (self && self->m_subWindows)
               ? (int)XVector_size_base(
                     (const XContainer*)self->m_subWindows)
               : 0;
}

/* ==================== 信号 ==================== */

void* XMdiArea_subWindowActivated_signal(XMdiArea* self,
                                         XMdiSubWindow* window)
{
    (void)self; (void)window;
    return (void*)(size_t)XMdiArea_subWindowActivated_signal;
}

void* XMdiSubWindow_aboutToActivate_signal(XMdiSubWindow* self)
{
    (void)self;
    return (void*)(size_t)XMdiSubWindow_aboutToActivate_signal;
}
void* XMdiSubWindow_windowStateChanged_signal(XMdiSubWindow* self,
                                              int oldState, int newState)
{
    (void)self; (void)oldState; (void)newState;
    return (void*)(size_t)XMdiSubWindow_windowStateChanged_signal;
}

/* ==================== Task 2.7：MdiArea 补充 API ==================== */

XMdiSubWindow* XMdiArea_currentSubWindow(const XMdiArea* self)
{ return self ? self->m_active : NULL; }

void XMdiArea_setBackground(XMdiArea* self, uint32_t color)
{
    if (self) {
        self->m_background = color;
        XWidget_update((XWidget*)self);
    }
}
uint32_t XMdiArea_background(const XMdiArea* self)
{ return self ? self->m_background : 0; }

void XMdiArea_setTabPosition(XMdiArea* self, int position)
{ if (self) self->m_tabPosition = position; }
int XMdiArea_tabPosition(const XMdiArea* self)
{ return self ? self->m_tabPosition : 0; }

void XMdiArea_setTabsMovable(XMdiArea* self, bool movable)
{ if (self) self->m_tabsMovable = movable; }
bool XMdiArea_tabsMovable(const XMdiArea* self)
{ return self ? self->m_tabsMovable : false; }

void XMdiArea_setTabsClosable(XMdiArea* self, bool closable)
{ if (self) self->m_tabsClosable = closable; }
bool XMdiArea_tabsClosable(const XMdiArea* self)
{ return self ? self->m_tabsClosable : false; }

void XMdiArea_setActivationOrder(XMdiArea* self, int order)
{ if (self) self->m_activationOrder = order; }
int XMdiArea_activationOrder(const XMdiArea* self)
{ return self ? self->m_activationOrder : 0; }

/* ==================== Task 2.7：XMdiSubWindow 补充 API ==================== */

void XMdiSubWindow_setOption(XMdiSubWindow* self, int option, bool on)
{
    if (!self) return;
    if (on) self->m_options |= option;
    else self->m_options &= ~option;
}

bool XMdiSubWindow_testOption(const XMdiSubWindow* self, int option)
{
    return self ? (self->m_options & option) != 0 : false;
}

/* 对标 QMdiSubWindow::setKeyboardSingleStep/PageStep
 * （qmdisubwindow.cpp:2402-2407、2429-2434）：Qt 显式注释不做下界检查
 * ——负值表示反向步进、0 表示不动，均原样存储。此前本实现用
 * `step > 0` 门槛把 0/负值静默忽略（读回旧值），与 Qt 相悖。 */
void XMdiSubWindow_setKeyboardSingleStep(XMdiSubWindow* self, int step)
{ if (self) self->m_keyboardSingleStep = step; }

int XMdiSubWindow_keyboardSingleStep(const XMdiSubWindow* self)
{ return self ? self->m_keyboardSingleStep : 0; }

void XMdiSubWindow_setKeyboardPageStep(XMdiSubWindow* self, int step)
{ if (self) self->m_keyboardPageStep = step; }

int XMdiSubWindow_keyboardPageStep(const XMdiSubWindow* self)
{ return self ? self->m_keyboardPageStep : 0; }

bool XMdiSubWindow_isShaded(const XMdiSubWindow* self)
{ return self ? self->m_shaded : false; }

void XMdiSubWindow_showShaded(XMdiSubWindow* self)
{
    if (!self) return;
    /* 对标 QMdiSubWindow::showShaded（qmdisubwindow.cpp:2553-2561）：
     * ① 无父窗口直接返回——独立子窗不是 MDI 工作区窗口，折叠语义
     *    不适用（本机 Qt 6.8.0 探针：独立 QMdiSubWindow showShaded 后
     *    isShaded()==0、尺寸不变）；
     * ② 已是折叠态再调也返回——showShaded 不是开关，取消折叠走
     *    showNormal / setWindowState(NoState)（setNormalMode 分支，
     *    qmdisubwindow.cpp:1246-1288）。 */
    if (!XWidget_parentWidget((XWidget*)self)) return;
    if (self->m_shaded) return;
    xmdi_enterShaded(self);
}

void XMdiSubWindow_showSystemMenu(XMdiSubWindow* self)
{
    XPoint pos;
    if (!self || !self->m_systemMenu) return;
    XPoint_init(&pos, 4, XWidget_height((XWidget*)self) > 20 ? 18 : 0);
    XMenu_popup(self->m_systemMenu, &pos);
}

void XMdiSubWindow_setSystemMenu(XMdiSubWindow* self, XMenu* systemMenu)
{
    if (!self) return;
    if (self->m_systemMenu == systemMenu) return;
    if (self->m_systemMenu)
        XClassDelete((XClass*)self->m_systemMenu);
    self->m_systemMenu = systemMenu;
}

XMenu* XMdiSubWindow_systemMenu(const XMdiSubWindow* self)
{ return self ? self->m_systemMenu : NULL; }

XMdiArea* XMdiSubWindow_mdiArea(const XMdiSubWindow* self)
{
    XWidget* parent;
    XWidget* node;
    if (!self) return NULL;
    parent = XWidget_parentWidget((XWidget*)self);
    /* 对标 QMdiSubWindow::mdiArea（qmdisubwindow.cpp:2527-2538）：
     * 沿父链上溯，命中 XMdiArea 祖先且「本子窗直父=该区视口」才认
     * 归属。此前直呼 parent 强转 XMdiArea*——addSubWindow 实际把子
     * 窗挂在视口（qmdiarea.cpp:791-797 appendChild 同构），反查恒返
     * 视口指针，回归断言 "[MDI-FAIL] ext: mdiArea 反查" 即此根因。
     * 类判定经虚表类名（qobject_cast 的本框架等价，同 XStyle.c:710
     * 口径），非本框架裸指针强转。 */
    for (node = parent; node; node = XWidget_parentWidget(node)) {
        XVtable* vt = XClassGetVtable((XClass*)node);
        if (vt && XVTABLE_GET_NAME(vt) &&
            XStrcmp(XVTABLE_GET_NAME(vt), "XMdiArea") == 0) {
            XWidget* viewport = XAbstractScrollArea_viewport(
                (XAbstractScrollArea*)node);
            if (viewport == parent) return (XMdiArea*)node;
        }
    }
    return NULL;
}

XSize XMdiSubWindow_sizeHint(const XMdiSubWindow* self)
{
    XSize size;
    XSize minHint;
    XWidget* parent;
    bool decorated;
    int margin;
    int w;
    int h;
    if (!self) {
        XSize_init(&size, 0, 0);
        return size;
    }
    /* 对标 QMdiSubWindow::sizeHint（qmdisubwindow.cpp:3451-3462）：
     *   sizeParameters(&margin, &minWidth);  // 无父/无边框 → margin=0
     *   QSize size(2 * margin, titleBarHeight() + margin);
     *   if (baseWidget && baseWidget->sizeHint().isValid())
     *       size += baseWidget->sizeHint();
     *   return size.expandedTo(minimumSizeHint());
     * ——**没有** 200x150 下限（旧实现的下限已删除）。titleBarHeight()
     * 在无父/无边框时为 0（qmdisubwindow.cpp:1702-1708），本机探针：
     * 独立 QMdiSubWindow sizeHint=0x0；有父 + 内容 min(120,80) 时
     * 128x108（Fusion margin=4、titleBar=24）——本框架 chrome 取
     * margin=1（左右框线 + 底框线）、标题带 20。 */
    parent = XWidget_parentWidget((XWidget*)self);
    decorated = parent &&
                !(XWidget_windowFlags((XWidget*)self) &
                  (XWidgetFlags)XWindowType_FramelessWindowHint);
    margin = decorated ? 1 : 0;
    w = 2 * margin;
    h = decorated ? XMDI_SUBWINDOW_TITLEBAR_HEIGHT + margin : 0;
    if (self->m_widget) {
        XSize ws = XWidget_sizeHint(self->m_widget);
        if (ws.width > 0 && ws.height > 0) {
            w += ws.width;
            h += ws.height;
        }
    }
    minHint = XMdiSubWindow_minimumSizeHint(self);
    if (w < minHint.width) w = minHint.width;
    if (h < minHint.height) h = minHint.height;
    XSize_init(&size, w, h);
    return size;
}

XSize XMdiSubWindow_minimumSizeHint(const XMdiSubWindow* self)
{
    XSize size;
    bool inArea;
    int margin;
    int titleBar;
    int minW;
    int minH;
    if (!self) {
        XSize_init(&size, 0, 0);
        return size;
    }
    /* 对标 QMdiSubWindow::minimumSizeHint（qmdisubwindow.cpp:3465-3510）：
     * ① sizeParameters 在**无父窗口**时 margin/minWidth 均为 0、标题带
     *    高度也为 0 ⇒ 独立子窗的最小尺寸只由内容件决定（实测 0x0）；
     * ② 有父窗口时 = 标题带 + 内容件 minimumSizeHint（内容不可用时为
     *    标题带自身的最小宽，本实现取 chrome 的 50px 口径）；
     * ③ 折叠态只保留标题带高度、宽度取 max(最小宽, 当前宽)。
     * 此前恒返回固定 (50,20)，完全忽略内容件——内容最小尺寸大于窗口
     * 时布局会把内容压扁。 */
    inArea = XMdiSubWindow_mdiArea(self) != NULL;
    margin = inArea ? 1 : 0;               /* chrome 窗框 1px */
    titleBar = inArea ? XMDI_SUBWINDOW_TITLEBAR_HEIGHT : 0;
    if (inArea && self->m_shaded) {
        int w = XWidget_width((const XWidget*)self);
        if (w < XMDI_SUBWINDOW_CHROME_MIN_WIDTH)
            w = XMDI_SUBWINDOW_CHROME_MIN_WIDTH;
        XSize_init(&size, w, titleBar);
        return size;
    }
    minW = inArea ? XMDI_SUBWINDOW_CHROME_MIN_WIDTH : 0;
    minH = titleBar + margin;              /* 标题带 + 底框线 */
    if (self->m_widget) {
        XSize contentMin = XWidget_minimumSizeHint(self->m_widget);
        if (contentMin.width > 0)
            minW = (contentMin.width + 2 * margin > minW)
                       ? contentMin.width + 2 * margin : minW;
        if (contentMin.height > 0) minH += contentMin.height;
    }
    XSize_init(&size, minW, minH);
    return size;
}

XWidget* XMdiSubWindow_maximizedButtonsWidget(const XMdiSubWindow* self)
{ (void)self; return NULL; }

XWidget* XMdiSubWindow_maximizedSystemMenuIconWidget(
    const XMdiSubWindow* self)
{ (void)self; return NULL; }

/* ==================== Task 2.7：XMdiArea 补充 API ==================== */

void XMdiArea_setOption(XMdiArea* self, int option, bool on)
{
    if (!self) return;
    if (on) self->m_options |= option;
    else self->m_options &= ~option;
}

bool XMdiArea_testOption(const XMdiArea* self, int option)
{
    return self ? (self->m_options & option) != 0 : false;
}

void XMdiArea_setDocumentMode(XMdiArea* self, bool enabled)
{
    if (self) {
        self->m_documentMode = enabled;
        XWidget_update((XWidget*)self);
    }
}

bool XMdiArea_documentMode(const XMdiArea* self)
{ return self ? self->m_documentMode : false; }

void XMdiArea_setTabShape(XMdiArea* self, int shape)
{ if (self) self->m_tabShape = shape; }

int XMdiArea_tabShape(const XMdiArea* self)
{ return self ? self->m_tabShape : 0; }

void XMdiArea_closeActiveSubWindow(XMdiArea* self)
{
    XMdiSubWindow* sw;
    int64_t i;
    bool deleteOnClose;
    if (!self) return;
    sw = self->m_active;
    if (!sw) return; /* qmdiarea.cpp:1824-1828：无活动窗直接返回 */
    deleteOnClose = XWidget_testAttribute((XWidget*)sw,
                                          XWidgetAttribute_DeleteOnClose);
    /* 对标 Qt：closeActiveSubWindow 即 `d->active->close()`——走关闭事件，
     * **被拒绝时保持激活**（旧实现无条件置 m_active=NULL 再删）。 */
    if (!XWidget_close((XWidget*)sw)) return;
    if (!deleteOnClose) {
        /* 非内部创建的子窗仅隐藏，仍留在 subWindowList（Qt 同）。 */
        xmdi_reactivateAfterRemove(self, sw);
        return;
    }
    i = xmdi_subWindowIndex(self, sw);
    if (i >= 0) XVector_remove_base(self->m_subWindows, i, 1);
    xmdi_historyRemove(self, sw);
    /* 对标 Qt：关闭活动窗口后激活剩余的第一个可见子窗口；无剩余则发
     * subWindowActivated(nullptr)（qmdiarea.cpp:1049-1126）。 */
    xmdi_reactivateAfterRemove(self, sw);
    /* 关闭路径可能由子窗口自身事件（closeEvent/按键）触发；同步删会在其
     * 事件帧返回途中释放自身，改延迟回收（对标 Qt deleteLater）。 */
    XObject_deleteLater((XObject*)sw);
}

/**
 * @brief      按当前激活顺序找相邻可见子窗口（对标
 *             QMdiAreaPrivate::nextVisibleSubWindow，qmdiarea.cpp:1393-1440）。
 * @details    `activateNextSubWindow`/`activatePreviousSubWindow` 在 Qt 里
 *             按 `activationOrder()` 选序（不是固定创建序）。无活动窗时
 *             Qt 取列表末项为当前项再步进（本机探针：a,b,c 无活动 +
 *             activateNext ⇒ a；activatePrevious ⇒ b）；候选隐藏则继续
 *             步进，绕回当前项仍未找到则返回 NULL。
 * @param      self 目标区域。
 * @param      step +1=下一个，-1=上一个。
 * @return     下一个可见子窗口借用指针；无候选返回 NULL。
 */
static XMdiSubWindow* xmdi_nextVisibleSubWindow(XMdiArea* self, int step)
{
    XVector* list;
    int n;
    int idx = -1;
    int i;
    int candidate;
    XMdiSubWindow* result = NULL;
    if (!self) return NULL;
    list = XMdiArea_subWindowList_2(self, self->m_activationOrder);
    if (!list) return NULL;
    n = (int)XVector_size_base((const XContainer*)list);
    if (n <= 0) {
        XClassDelete((XClass*)list);
        return NULL;
    }
    if (self->m_active) {
        for (i = 0; i < n; ++i) {
            XMdiSubWindow** slot =
                (XMdiSubWindow**)XVector_at_base(list, i);
            if (slot && *slot == self->m_active) {
                idx = i;
                break;
            }
        }
    }
    if (idx < 0) idx = n - 1; /* 无活动窗/活动窗不在列表：取末项 */
    candidate = idx;
    for (i = 0; i < n; ++i) {
        XMdiSubWindow** slot;
        candidate = ((candidate + step) % n + n) % n;
        slot = (XMdiSubWindow**)XVector_at_base(list, candidate);
        if (slot && *slot && !XWidget_isHidden((XWidget*)*slot)) {
            result = *slot;
            break;
        }
        if (candidate == idx) break;
    }
    XClassDelete((XClass*)list);
    return result;
}

void XMdiArea_activateNextSubWindow(XMdiArea* self)
{
    XMdiSubWindow* next;
    if (!self) return;
    next = xmdi_nextVisibleSubWindow(self, 1);
    if (next) XMdiArea_setActiveSubWindow(self, next);
}

void XMdiArea_activatePreviousSubWindow(XMdiArea* self)
{
    XMdiSubWindow* prev;
    if (!self) return;
    prev = xmdi_nextVisibleSubWindow(self, -1);
    if (prev) XMdiArea_setActiveSubWindow(self, prev);
}

#endif /* XWIDGET_ON && XABSTRACTSCROLLAREA_ON && XMDIAREA_ON */

#endif /* XGUI_ON */
