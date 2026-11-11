/**
 * @file       XGroupBox.c
 * @brief      XGroupBox 分组框控件实现（对标 Qt 6.8 QGroupBox）。
 * @details    绘制分层：
 *             - 标题区高度 = 文本行高 + 上下各 2px 间距（无标题时
 *               为 0）；
 *             - 边框：1px 凹陷线（上/左 Dark，下/右 Light），从标题
 *               区中部水平线起环绕左、右、下三边；标题文本以 Base
 *               底色矩形遮挡边框线（对标 QGroupBox 的标题挖空）；
 *             - flat 模式：只在标题文本左右两侧各画一段短边框线，
 *               不画完整环绕；
 *             - 可勾选（checkable）：标题左侧自绘 12px 小勾选框
 *               （Base 底 + 凹陷描边 + 勾选标记），标题文本右移；
 *               点击标题区切换勾选状态并发射 clicked/toggled，未勾选
 *               时递归禁用子控件（对标 _q_setChildrenEnabled）。
 *             标题对齐仅水平分量（Left/HCenter/Right）。
 * @author     XinYueC 团队
 ******************************************************************************/
#if XGUI_ON

#include "XPainter.h"
#include "XGroupBox.h"

#include "CXinYueConfig.h"
#include "XStringUtils.h"
#include "XAlgorithm.h"
#include "XStyle.h"
#include "XStyleOption.h"
#include "XString.h"
#include "XWidget_Protected.h"
#include "XMemory.h"
#include "XEvent.h"
#include "XCoreApplication.h"
#include "XColor.h"
#include "XVector.h"
#include "XContainer.h"
#if XPALETTE_ON
#include "XPalette.h"
#endif /* XPALETTE_ON */

#if XWIDGET_ON && XGROUPBOX_ON

/* 标题区上下间距（像素） */
#define XGROUPBOX_TITLE_PAD 2
/* 自绘勾选框尺寸（像素，正方形） */
#define XGROUPBOX_CHECKBOX_SIZE 12
/* 勾选框与标题文本间距（像素） */
#define XGROUPBOX_CHECKBOX_GAP 4

/* ==================== 内部辅助 ==================== */

/** @brief 取指定角色颜色为 ARGB32；无调色板能力时回退纯黑。 */
static uint32_t xgroupbox_color(const XGroupBox* self, XPaletteColorRole role)
{
#if XPALETTE_ON
    XPalette palette = XWidget_palette((XWidget*)self);
    XColor c = XPalette_color(&palette, XPaletteColorGroup_Current, role);
    return XColor_rgba(&c);
#else
    (void)self;
    (void)role;
    return 0xFF000000u;
#endif /* XPALETTE_ON */
}

/** @brief 读取标题文本（空串表示无标题）。 */
static const char* xgroupbox_titleText(const XGroupBox* self)
{
    const char* t;
    if (!self || !self->m_title) return "";
    t = XString_toUtf8(self->m_title);
    return (t && t[0]) ? t : "";
}

/** @brief 计算标题区高度（无标题返回 0）。 */
static int xgroupbox_titleHeight(const XGroupBox* self)
{
    return (self->m_title && xgroupbox_titleText(self)[0] != '\0')
        ? 14 + XGROUPBOX_TITLE_PAD * 2 : 0;
}

/** @brief 标题内容（勾选框/文本）的起始 X 坐标；可勾选时勾选框居前，
 *         文本顺延到勾选框之后。 */
static int xgroupbox_contentX(const XGroupBox* self, int leftMargin)
{
    if (self->m_checkable)
        return leftMargin + XGROUPBOX_CHECKBOX_SIZE + XGROUPBOX_CHECKBOX_GAP;
    return leftMargin;
}

/** @brief 标题绘制的水平对齐分量（Qt 把 setAlignment 入参**原样**存储，
 *         绘制期只消费水平位——QStyle::alignedRect 同样只读
 *         AlignHorizontal_Mask，其余位不影响单行标题）。
 *  @details 判定顺序按 QStyle::alignedRect（qstyle.cpp）：先 AlignRight、
 *           再 AlignHCenter，都没有才按左对齐（0x00FF 这类冲突位组合在
 *           Qt 下同样落到 Right）。 */
static int xgroupbox_horizAlign(const XGroupBox* self)
{
    int h = self ? (self->m_alignment & XAlignment_HorizontalMask) : 0;
    if (h & XAlignment_Right) return XAlignment_Right;
    if (h & XAlignment_HCenter) return XAlignment_HCenter;
    return XAlignment_Left;
}

/** @brief 判断局部坐标是否落在标题区（可勾选时点击该区域切换状态）。 */
static bool xgroupbox_titleRowHit(const XGroupBox* self, const XPoint* pos)
{
    int titleH;
    if (!self || !pos || !xgroupbox_titleText(self)[0]) return false;
    titleH = xgroupbox_titleHeight(self);
    return pos->y >= 0 && pos->y < titleH;
}

/** @brief 把全部子控件的启用状态同步为 enabled（递归由 XWidget_setEnabled
 *         传播；对标 QGroupBoxPrivate::_q_setChildrenEnabled）。 */
static void xgroupbox_setChildrenEnabled(XGroupBox* self, bool enabled)
{
    const XVector* children;
    size_t n;
    size_t i;
    if (!self) return;
    children = XObject_children((XObject*)self);
    n = children ? XVector_size_base((const XContainer*)children) : 0;
    for (i = 0; i < n; ++i) {
        XObject* child = *(XObject**)XVector_at_base(children, (int64_t)i);
        XWidget* widget;
        if (!child || !child->m_is_widget) continue;
        widget = (XWidget*)child;
        /* 对标 Qt：顶层子窗口不受分组框勾选状态影响。 */
        if (widget->m_isWindow) continue;
        if (XWidget_isEnabled(widget) != enabled)
            XWidget_setEnabled(widget, enabled);
    }
}

/** @brief 发射 bool 参数信号（对标 abstractbutton_emitBool 模式）。 */
static void xgroupbox_emitBool(XGroupBox* self, size_t signal, bool value)
{
    XVarList* arguments = XVarList_Create(XVar(bool, value));
    if (!arguments) return;
    if (self && ((XObject*)self)->m_signalSlot) {
        XObject_emitSignal((XObject*)self, signal, arguments, NULL, NULL,
                           XEVENT_PRIORITY_NORMAL);
    } else {
        XVarList_delete(arguments);
    }
}

/* ==================== 绘制 ==================== */

/**
 * @brief      绘制边框 + 标题（标题以 Base 底色挖空边框线）。
 * @param      self    分组框对象。
 * @param      painter 画笔（已绑定绘制图像并应用平移/裁剪）。
 * @return     无返回值。
 */
void XGroupBox_drawControl(const XGroupBox* self, XPainter* painter)
{
    XRect r = XWidget_rect((XWidget*)self);
    uint32_t base;
    uint32_t dark;
    uint32_t light;
    uint32_t windowText;
    int titleH;
    char text[64];
    r.x = 0;
    r.y = 0;
    if (r.width <= 2 || r.height <= 2) return;

    base       = xgroupbox_color(self, XPaletteColorRole_Base);
    dark       = xgroupbox_color(self, XPaletteColorRole_Dark);
    light      = xgroupbox_color(self, XPaletteColorRole_Light);
    windowText = xgroupbox_color(self, XPaletteColorRole_WindowText);
    titleH = xgroupbox_titleHeight(self);
    (void)text;

#if XSTYLE_ON
    if (XWidget_style((const XWidget*)self) != NULL) {
        /* Fusion/公共风格接管：分组框整体走 CC_GroupBox（镂空边框 +
         * 标题 + 勾选框完整复刻）。 */
        XStyle* style = XWidget_style((const XWidget*)self);
        XStyleOption opt;
        XStyleOption_init(&opt, XStyleCC_GroupBox);
        opt.m_rect = r;
        opt.m_state = XWidget_isEnabled((XWidget*)self)
            ? XStyleState_Enabled : 0;
        if (XWidget_hasFocus((XWidget*)self))
            opt.m_state |= XStyleState_HasFocus;
        if (XWidget_underMouse((XWidget*)self) &&
            XWidget_isEnabled((XWidget*)self))
            opt.m_state |= XStyleState_MouseOver;
        opt.m_text = xgroupbox_titleText(self);
        opt.m_flat = self->m_flat;
        opt.m_checkable = self->m_checkable;
        opt.m_checked = self->m_checked;
#if XPALETTE_ON
        opt.m_palette = XWidget_palette((XWidget*)self);
#endif
        XStyle_drawComplexControl(style, XStyleCC_GroupBox, &opt,
                                  painter, (XWidget*)self);
        return;
    }
#endif /* XSTYLE_ON */
    if (!xgroupbox_titleText(self)[0]) {
        /* 无标题：四边完整凹陷边框。 */
        XRect e = r;
        e.height = 1;
        XPainter_fillRect(painter, &e, dark);
        e = r; e.width = 1;
        XPainter_fillRect(painter, &e, dark);
        e = r; e.x = r.x + r.width - 1; e.width = 1;
        XPainter_fillRect(painter, &e, light);
        e = r; e.y = r.y + r.height - 1; e.height = 1;
        XPainter_fillRect(painter, &e, light);
        return;
    }

    XSnprintf(text, sizeof(text), "%s", xgroupbox_titleText(self));

    if (self->m_flat) {
        /* 扁平：标题两侧各一段短边框线（上边线 y=titleH/2）。 */
        int midY = r.y + titleH / 2;
        int textW = (int)XStrlen(text) * 8;
        int textX = xgroupbox_contentX(self, r.x + 6);
        int leftEnd;
        if (xgroupbox_horizAlign(self) == XAlignment_HCenter)
            textX = r.x + (r.width - textW) / 2;
        else if (xgroupbox_horizAlign(self) == XAlignment_Right)
            textX = r.x + r.width - 6 - textW;
        /* 左侧短线止于勾选框（可勾选时）或文本前；右侧从文本后开始。 */
        leftEnd = self->m_checkable ? (r.x + 6 - 2) : (textX - 2);
        {
            XRect line = { r.x, midY,
                           (leftEnd > r.x) ? (leftEnd - r.x) : 0, 1 };
            XPainter_fillRect(painter, &line, dark);
        }
        {
            int rightStart = textX + textW + 2;
            XRect line = { rightStart, midY,
                           (rightStart < r.x + r.width)
                               ? (r.x + r.width - rightStart) : 0, 1 };
            XPainter_fillRect(painter, &line, dark);
        }
    } else {
        /* 常规（对标 Fusion PE_FrameGroupBox 的 1px 中性边框）：
         * 左/右/下三边整段 1px Dark；上边线在标题两侧（起于标题区中部）。 */
        int midY = r.y + titleH / 2;
        XRect left = { r.x, midY, 1, r.y + r.height - midY };
        XPainter_fillRect(painter, &left, dark);
        {
            XRect right = { r.x + r.width - 1, midY, 1,
                            r.y + r.height - midY };
            XPainter_fillRect(painter, &right, dark);
        }
        {
            XRect bottom = { r.x + 1, r.y + r.height - 1, r.width - 2, 1 };
            XPainter_fillRect(painter, &bottom, dark);
        }
        {
            XRect innerL = { r.x + 1, midY + 1, 1, r.y + r.height - midY - 1 };
            XPainter_fillRect(painter, &innerL, light);
        }
        {
            XRect topSeg = { r.x, midY, r.width, 1 };
            XPainter_fillRect(painter, &topSeg, dark);
        }
    }

    /* 勾选框（可勾选时）：标题左侧自绘 12px 小勾选框。 */
    if (self->m_checkable) {
        XRect cb = { r.x + 6, r.y + (titleH - XGROUPBOX_CHECKBOX_SIZE) / 2,
                     XGROUPBOX_CHECKBOX_SIZE, XGROUPBOX_CHECKBOX_SIZE };
        XRect edge = cb;
        XPainter_fillRect(painter, &cb, base);          /* 底（覆盖边框线） */
        edge = cb; edge.height = 1;                     /* 上 Dark */
        XPainter_fillRect(painter, &edge, dark);
        edge = cb; edge.width = 1;                      /* 左 Dark */
        XPainter_fillRect(painter, &edge, dark);
        edge = cb; edge.x = cb.x + cb.width - 1; edge.width = 1; /* 右 Light */
        XPainter_fillRect(painter, &edge, light);
        edge = cb; edge.y = cb.y + cb.height - 1; edge.height = 1; /* 下 Light */
        XPainter_fillRect(painter, &edge, light);
        if (self->m_checked) {
            /* 勾选标记：两段斜线（近似对勾）。 */
            XPainter_setPen(painter, windowText);
            XPainter_drawLine(painter, cb.x + 2, cb.y + 6,
                              cb.x + 5, cb.y + 9);
            XPainter_drawLine(painter, cb.x + 5, cb.y + 9,
                              cb.x + 10, cb.y + 3);
        }
    }

    /* 标题文本：Base 底色矩形挖空边框线 + WindowText 文本。 */
    {
        int textW = (int)XStrlen(text) * 8;
        int textX = xgroupbox_contentX(self, r.x + 6);
        int backX;
        int backW;
        if (xgroupbox_horizAlign(self) == XAlignment_HCenter)
            textX = r.x + (r.width - textW) / 2;
        else if (xgroupbox_horizAlign(self) == XAlignment_Right)
            textX = r.x + r.width - 6 - textW;
        /* 挖空矩形：可勾选时从勾选框右缘起（覆盖间隔），否则文本左 2px。 */
        backX = self->m_checkable
            ? (r.x + 6 + XGROUPBOX_CHECKBOX_SIZE)
            : (textX - 2);
        backW = textX + textW + 2 - backX;
        if (backW < 0) backW = 0;
        {
            XRect back = { backX, r.y + (titleH - 14) / 2, backW, 14 };
            XPainter_fillRect(painter, &back, base);
            XPainter_drawText(painter, textX,
                              back.y + 14 - 2, text, windowText);
        }
    }
}

/* ==================== 虚槽实现 ==================== */

/** @brief 控件事件入口（对标 QGroupBox::event）。
 *  @details 本框架的子对象事件（CHILD_ADDED 等）经事件入口分派而不走
 *           EXObject_ChildEvent 槽位，故在此拦截（对标 QGroupBox::
 *           childEvent，qgroupbox.cpp:362-391）：可勾选分组框新增子控件
 *           时按当前勾选态同步其可用性——已勾选恢复可用、未勾选立即禁用
 *           （Qt `if (d->checkable) { if (d->checked) w->setEnabled(true);
 *           else w->setEnabled(false); }`）。 */
static bool VXGroupBox_event(XWidget* self, XEvent* event)
{
    XEventType type;
    bool result;
    if (!self || !event) return false;
    type = XEvent_type(event);
    result = XClass_Parent(XWidget, EXObject_Event,
                           bool(*)(XObject*, XEvent*))((XObject*)self, event);
    if (type == XEVENT_TYPE_CHILD_ADDED) {
        XObject* child = XChildEvent_child((const XChildEvent*)event);
        if (child && child->m_is_widget) {
            XGroupBox* box = (XGroupBox*)self;
            /* 顶层子窗口不受分组框勾选状态影响（Qt `w->isWindow()` 早退）。 */
            if (box->m_checkable && !((XWidget*)child)->m_isWindow) {
                bool enable = box->m_checked;
                if (XWidget_isEnabled((XWidget*)child) != enable)
                    XWidget_setEnabled((XWidget*)child, enable);
            }
        }
    }
    return result;
}

/** @brief 调整大小事件：交父类后重绘（对标 QGroupBox::resizeEvent）。 */
static void VXGroupBox_resizeEvent(XWidget* self, XEvent* event)
{
    XClass_Parent(XWidget, EXWidget_ResizeEvent,
                  void(*)(XWidget*, XEvent*))((XWidget*)self, event);
    XWidget_update(self);
}

/** @brief 焦点进入事件：交父类后重绘（对标 QGroupBox::focusInEvent）。 */
static void VXGroupBox_focusInEvent(XWidget* self, XEvent* event)
{
    XClass_Parent(XWidget, EXWidget_FocusInEvent,
                  void(*)(XWidget*, XEvent*))((XWidget*)self, event);
    XWidget_update(self);
}

/** @brief 变更事件：交父类后按启用状态变化同步子控件可用性并重绘
 *         （对标 QGroupBox::changeEvent 的 EnabledChange 分支，
 *         qgroupbox.cpp:647-664）。 */
static void VXGroupBox_changeEvent(XWidget* self, XEvent* event)
{
    XGroupBox* box = (XGroupBox*)self;
    XEventType type;
    XClass_Parent(XWidget, EXWidget_ChangeEvent,
                  void(*)(XWidget*, XEvent*))((XWidget*)self, event);
    if (!box || !event) return;
    type = XEvent_type(event);
    /* Qt 只在「被启用」且未勾选时把子控件禁用；禁用分组框本身不再改
     * 子控件状态（父链禁用自然生效）。此前任何启用变化都按勾选态重写
     * 一次子控件启用位（禁用分组框时反而把子控件置回启用）。 */
    if (type == XEVENT_TYPE_ENABLED_CHANGE && box->m_checkable &&
        XWidget_isEnabled((XWidget*)box) && !box->m_checked)
        xgroupbox_setChildrenEnabled(box, false);
    XWidget_update((XWidget*)box);
}

/** @brief 鼠标按下：标题区仅记录按压态，不立即切换（对标
 *  QGroupBox::mousePressEvent——设置 pressed 并重绘，勾选切换发生在
 *  标题区内的释放，对齐复选框点击语义）。非左键或非标题区交父类。 */
static void VXGroupBox_mousePressEvent(XWidget* self, XEvent* event)
{
    XGroupBox* box = (XGroupBox*)self;
    XMouseEvent* me;
    XPoint pos;
    if (!box || !event ||
        XEvent_type(event) != XEVENT_TYPE_MOUSE_BUTTON_PRESS)
        return;
    me = (XMouseEvent*)event;
    if (XMouseEvent_button(me) != XMouseButton_LeftButton) {
        XEvent_ignore(event);
        return;
    }
    pos = XMouseEvent_position(me);
    box->m_pressed = box->m_checkable && xgroupbox_titleRowHit(box, &pos);
    if (box->m_pressed) {
        XEvent_accept(event);
        XWidget_update((XWidget*)self);
        return;
    }
    XClass_Parent(XWidget, EXWidget_MousePressEvent,
                  void(*)(XWidget*, XEvent*))((XWidget*)self, event);
}

/** @brief 鼠标移动：第一版无拖选语义，交父类处理（后续扩展拖选）。 */
static void VXGroupBox_mouseMoveEvent(XWidget* self, XEvent* event)
{
    XClass_Parent(XWidget, EXWidget_MouseMoveEvent,
                  void(*)(XWidget*, XEvent*))((XWidget*)self, event);
}

/** @brief 鼠标释放：按压源于标题区且在标题区内释放时切换勾选并发射
 *  clicked（对标 QGroupBox::mouseReleaseEvent 的指示器点击语义）。 */
static void VXGroupBox_mouseReleaseEvent(XWidget* self, XEvent* event)
{
    XGroupBox* box = (XGroupBox*)self;
    XMouseEvent* me;
    XPoint pos;
    bool wasPressed;
    if (!box || !event ||
        XEvent_type(event) != XEVENT_TYPE_MOUSE_BUTTON_RELEASE) {
        XClass_Parent(XWidget, EXWidget_MouseReleaseEvent,
                      void(*)(XWidget*, XEvent*))((XWidget*)self, event);
        return;
    }
    me = (XMouseEvent*)event;
    wasPressed = box->m_pressed;
    box->m_pressed = false;
    if (wasPressed && XMouseEvent_button(me) == XMouseButton_LeftButton) {
        pos = XMouseEvent_position(me);
        if (xgroupbox_titleRowHit(box, &pos)) {
            XGroupBox_setChecked(box, !box->m_checked); /* 变化时发射 toggled */
            XGroupBox_clicked_signal(box, box->m_checked);
            XEvent_accept(event);
            return;
        }
    }
    XClass_Parent(XWidget, EXWidget_MouseReleaseEvent,
                  void(*)(XWidget*, XEvent*))((XWidget*)self, event);
}

/** @brief 绘制事件：在 paintDevice 上平移裁剪后调用 drawControl。 */
static void VXGroupBox_paintEvent(XWidget* self, XEvent* event)
{
    XImage* image;
    XPoint offset;
    XPainter painter;
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
    XGroupBox_drawControl((XGroupBox*)self, &painter);
    XPainter_end(&painter);
    XPainter_deinit(&painter);
}

/** @brief 深拷贝：基类 XWidget 深拷贝后复制全部分组框字段。 */
static void VXGroupBox_copy(XGroupBox* self, const XGroupBox* other)
{
    if (!self || !other || self == other) return;
    if (XClassIsVtableNull(self)) XGroupBox_init(self, NULL, 0);
    XClass_Parent(XWidget, EXClass_Copy,
                  void(*)(XWidget*, const XWidget*))((XWidget*)self,
                                                     (const XWidget*)other);
    if (self->m_title && other->m_title)
        XString_assign(self->m_title, other->m_title);
    self->m_alignment = other->m_alignment;
    self->m_flat = other->m_flat;
    self->m_checkable = other->m_checkable;
    self->m_checked = other->m_checked;
}

/** @brief 移动语义：基类移动后转移字段，源对象归构造默认值。 */
static void VXGroupBox_move(XGroupBox* self, XGroupBox* other)
{
    if (!self || !other || self == other) return;
    if (XClassIsVtableNull(self)) XGroupBox_init(self, NULL, 0);
    XClass_Parent(XWidget, EXClass_Move,
                  void(*)(XWidget*, XWidget*))((XWidget*)self,
                                               (XWidget*)other);
    if (self->m_title) XClassDelete(self->m_title);
    self->m_title = other->m_title;
    other->m_title = XString_create();
    self->m_alignment = other->m_alignment;
    self->m_flat = other->m_flat;
    self->m_checkable = other->m_checkable;
    self->m_checked = other->m_checked;
    /* m_title 已转移并重建。 */
    other->m_alignment = XAlignment_Left;
    other->m_flat = false;
    other->m_checkable = false;
    other->m_checked = false;
}

/* ==================== 生命周期 ==================== */

static void VXGroupBox_deinit(XGroupBox* self)
{
    if (!self) return;
    if (self->m_title) {
        XClassDelete(self->m_title);
        self->m_title = NULL;
    }
    XClass_Deinit_Parent(XWidget, (XWidget*)self);
}

XVtable* XGroupBox_class_init(void)
{
    XVTABLE_INIT_DEFAULT(XGroupBox)
    XVTABLE_INHERIT_XCLASS(XWidget);

    XVTABLE_OVERLOAD_DEFAULT(EXObject_Event, VXGroupBox_event);
    XVTABLE_OVERLOAD_DEFAULT(EXWidget_PaintEvent, VXGroupBox_paintEvent);
    XVTABLE_OVERLOAD_DEFAULT(EXWidget_ResizeEvent, VXGroupBox_resizeEvent);
    XVTABLE_OVERLOAD_DEFAULT(EXWidget_FocusInEvent, VXGroupBox_focusInEvent);
    XVTABLE_OVERLOAD_DEFAULT(EXWidget_ChangeEvent, VXGroupBox_changeEvent);
    XVTABLE_OVERLOAD_DEFAULT(EXWidget_MousePressEvent,
                             VXGroupBox_mousePressEvent);
    XVTABLE_OVERLOAD_DEFAULT(EXWidget_MouseMoveEvent,
                             VXGroupBox_mouseMoveEvent);
    XVTABLE_OVERLOAD_DEFAULT(EXWidget_MouseReleaseEvent,
                             VXGroupBox_mouseReleaseEvent);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Copy, VXGroupBox_copy);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Move, VXGroupBox_move);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Deinit, VXGroupBox_deinit);

    return XVTABLE_DEFAULT;
}

/** @brief 按当前标题计算最小尺寸提示（对标 QGroupBox::minimumSizeHint：
 *         标题文本宽 + 一个空格，可勾选时另加指示器宽与标签间距；高取
 *         字体高与指示器高的较大者）。纯函数，不写缓存。 */
static XSize xgroupbox_computeMinHint(const XGroupBox* self)
{
    XSize out;
    XFont font;
    const char* title;
    int baseW = 0;
    int baseH = 0;
    if (!self) { XSize_init(&out, 0, 0); return out; }
    font = XWidget_font((XWidget*)self);
    title = self->m_title ? XString_toUtf8(self->m_title) : NULL;
    if (title && title[0]) {
        baseW = XPainter_textWidth(&font, title) +
                XPainter_textWidth(&font, " ");
    }
    baseH = XPainter_textHeight(&font);
    XClassDeinit(&font);
    if (baseH <= 0) baseH = 14;
    /* 对标 QGroupBox::minimumSizeHint（qgroupbox.cpp:456-480）：可勾选时
     * baseWidth += PM_IndicatorWidth + PM_CheckBoxLabelSpacing，
     * baseHeight = max(baseHeight, PM_IndicatorHeight)。本框架的指示器即
     * 自绘勾选框（XGROUPBOX_CHECKBOX_SIZE）与它的间距
     * （XGROUPBOX_CHECKBOX_GAP），与绘制路径同源。 */
    if (self->m_checkable) {
        baseW += XGROUPBOX_CHECKBOX_SIZE + XGROUPBOX_CHECKBOX_GAP;
        if (baseH < XGROUPBOX_CHECKBOX_SIZE)
            baseH = XGROUPBOX_CHECKBOX_SIZE;
    }
    XSize_init(&out, baseW, baseH);
    return out;
}

/** @brief 刷新基类尺寸缓存（init 与 setTitle 共用；Qt 里 setTitle 会
 *         updateGeometry 并重算 minimumSizeHint）。 */
static void xgroupbox_refreshSizeHints(XGroupBox* self)
{
    XSize minHint;
    if (!self) return;
    minHint = xgroupbox_computeMinHint(self);
    XWidget_setMinimumSizeHint((XWidget*)self, &minHint);
}

void XGroupBox_init(XGroupBox* self, XWidget* parent, XWidgetFlags flags)
{
    if (!self) return;
    XMemset(self, 0, sizeof(XGroupBox));
    XWidget_init((XWidget*)self, parent, flags);
    XClassSetVtable(self, XGroupBox);

    self->m_title = XString_create();
    self->m_alignment = XAlignment_Left;
    self->m_flat = false;
    self->m_checkable = false;
    /* [Qt 对齐 2026-11-11] 内部 checked 初值即 true（qgroupbox.cpp:178
     * `checked = true`），与 checkable 无关：isChecked() = checkable &&
     * checked 仍为 false。这条初值决定了 setCheckable(true) 的
     * `setChecked(true)` 是同值早退 ⇒ **不发射** toggled（Qt 本机实测
     * toggled 计数 0），也决定 setCheckable(false) 不清勾选位。 */
    self->m_checked = true;
    self->m_pressed = false;
    /* 悬停外观 opt-in：面板绘制按 State_MouseOver 出悬停高亮，
     * ENTER/LEAVE 须标脏自矩形（对标 Qt polish 的 WA_Hover 收口）。 */
    XWidget_setAttribute((XWidget*)self, XWidgetAttribute_Hover, true);
    /* [minimumSizeHint 补齐 2026-10-27 框架对齐] Qt QGroupBox 不 override
     * sizeHint（继承 QWidget 缺省空尺寸），但 override minimumSizeHint
     * （qgroupbox.cpp:456-480）：标题文本宽 + 空格宽（可勾选时另加
     * 指示器宽与标签间距），高取字高（可勾选时取指示器高较大者）。
     * 补齐后布局竖排收缩有真实下界（此前 min=0，容器可被压塌）。 */
    xgroupbox_refreshSizeHints(self);
}

XGroupBox* XGroupBox_create_ex(XMemoryType memory, XWidget* parent,
                               XWidgetFlags flags)
{
    XGroupBox* self = (XGroupBox*)XMemory_malloc(sizeof(XGroupBox), memory);
    if (!self) return NULL;
    XGroupBox_init(self, parent, flags);
    Set_Class_Memory(self, memory); Set_Class_IsHeap(self, true);
    return self;
}

/* ==================== 标题与样式 ==================== */

const char* XGroupBox_title(const XGroupBox* self)
{
    return xgroupbox_titleText(self);
}

void XGroupBox_setTitle(XGroupBox* self, const char* title)
{
    if (!self) return;
    if (!title) title = "";
    if (!self->m_title) self->m_title = XString_create();
    if (self->m_title)
        XString_assign_utf8(self->m_title, title ? title : "");
    /* [Qt 对齐 2026-11-02] Qt 的 setTitle 会 updateGeometry() 并按新标题
       重算 minimumSizeHint；此前只 update() 不刷 hint → 标题变长后最小
       尺寸仍是旧值，布局按旧宽排。 */
    xgroupbox_refreshSizeHints(self);
    XWidget_updateGeometry((XWidget*)self);
    XWidget_update((XWidget*)self);
}

int XGroupBox_alignment(const XGroupBox* self)
{
    return self ? self->m_alignment : XAlignment_Left;
}

void XGroupBox_setAlignment(XGroupBox* self, int alignment)
{
    if (!self) return;
    /* 对标 QGroupBox::setAlignment（qgroupbox.cpp:259-264）：`d->align =
     * alignment;` **原样**存储（含垂直位/冲突位），alignment() 原样返回。
     * 本机 Qt 6.8 实测：setAlignment(Right|VCenter) 后 alignment()==130、
     * setAlignment(0x00FF) 后 ==255。绘制期只消费水平分量
     * （xgroupbox_horizAlign），等价 QStyle::alignedRect 的
     * AlignHorizontal_Mask 口径。此前只存水平分量并把非法值回退 Left，
     * 与 Qt 的存储语义不一致（既有回归用例按旧口径锁定，已登记迁移）。 */
    self->m_alignment = alignment;
    XWidget_updateGeometry((XWidget*)self);
    XWidget_update((XWidget*)self);
}

bool XGroupBox_isFlat(const XGroupBox* self)
{
    return self ? self->m_flat : false;
}

void XGroupBox_setFlat(XGroupBox* self, bool flat)
{
    if (!self || self->m_flat == flat) return;
    self->m_flat = flat;
    /* 对标 QGroupBox::setFlat（qgroupbox.cpp:499-506）：updateGeometry + update。 */
    XWidget_updateGeometry((XWidget*)self);
    XWidget_update((XWidget*)self);
}

XRect XGroupBox_contentsRect(const XGroupBox* self)
{
    XRect r;
    int titleH;
    XMemset(&r, 0, sizeof(r));
    if (!self) return r;
    r = XWidget_rect((XWidget*)self);
    titleH = xgroupbox_titleHeight(self);
    r.x += 1;
    r.width -= 2;
    r.y += titleH + 1;
    r.height -= titleH + 2;
    if (r.width < 0) r.width = 0;
    if (r.height < 0) r.height = 0;
    return r;
}

/* ==================== 勾选状态 ==================== */

bool XGroupBox_isCheckable(const XGroupBox* self)
{
    return self ? self->m_checkable : false;
}

void XGroupBox_setCheckable(XGroupBox* self, bool checkable)
{
    bool wasCheckable;
    if (!self) return;
    /* 对标 QGroupBox::setCheckable（qgroupbox.cpp:526-553）逐行口径：
     *   ① 先写 checkable（Qt 在同值调用时**也**继续走完流程）；
     *   ② checkable=true ⇒ setChecked(true)（内部 checked 初值即 true ⇒
     *      首次同值早退、**不发** toggled）；仅「不可勾选→可勾选」时取
     *      StrongFocus、恢复子控件可用并 updateGeometry；
     *   ③ checkable=false ⇒ 只把焦点策略置 NoFocus、恢复子控件可用；
     *      **不**动 checked、**不**发 toggled（Qt 本机实测 toggled 计数
     *      不变）。底层 checked 位保留 ⇒ 再 setCheckable(true) 时
     *      setChecked(true) 仍同值早退（用户此前手动取消勾选过则真迁移
     *      并随之发 toggled，Qt 同）。
     *   ④ 仅在可勾选性真正变化时 updateGeometry + update。 */
    wasCheckable = self->m_checkable;
    self->m_checkable = checkable;
    if (checkable) {
        XGroupBox_setChecked(self, true);
        if (!wasCheckable) {
            XWidget_setFocusPolicy((XWidget*)self,
                                   XWidgetFocusPolicy_StrongFocus);
            xgroupbox_setChildrenEnabled(self, true);
        }
    } else {
        if (wasCheckable) {
            XWidget_setFocusPolicy((XWidget*)self,
                                   XWidgetFocusPolicy_NoFocus);
            xgroupbox_setChildrenEnabled(self, true);
        }
        /* Qt 末尾无条件再调一次 _q_setChildrenEnabled(true)（即便本次
         * 调用前就不可勾选）。 */
        xgroupbox_setChildrenEnabled(self, true);
        self->m_pressed = false;
    }
    if (wasCheckable != checkable) {
        xgroupbox_refreshSizeHints(self);
        XWidget_updateGeometry((XWidget*)self);
        XWidget_update((XWidget*)self);
    }
}

bool XGroupBox_isChecked(const XGroupBox* self)
{
    /* 对标 Qt：isChecked() = checkable && checked。 */
    return self ? (self->m_checkable && self->m_checked) : false;
}

void XGroupBox_setChecked(XGroupBox* self, bool checked)
{
    if (!self || !self->m_checkable) return;
    if (self->m_checked == checked) return;
    self->m_checked = checked;
    /* 子控件可用性随勾选状态同步（未勾选禁用、勾选恢复，递归传播）。 */
    xgroupbox_setChildrenEnabled(self, checked);
    XGroupBox_toggled_signal(self, checked);
    XWidget_update((XWidget*)self);
}

/* ==================== 信号 ==================== */

void* XGroupBox_clicked_signal(XGroupBox* self, bool checked)
{
    if (!self)
        return (void*)(size_t)XGroupBox_clicked_signal;
    xgroupbox_emitBool(self, (size_t)XGroupBox_clicked_signal, checked);
    return (void*)(size_t)XGroupBox_clicked_signal;
}

void* XGroupBox_toggled_signal(XGroupBox* self, bool checked)
{
    if (!self)
        return (void*)(size_t)XGroupBox_toggled_signal;
    xgroupbox_emitBool(self, (size_t)XGroupBox_toggled_signal, checked);
    return (void*)(size_t)XGroupBox_toggled_signal;
}










#endif /* XWIDGET_ON && XGROUPBOX_ON */

#endif /* XGUI_ON */
