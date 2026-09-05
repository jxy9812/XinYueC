/******************************************************************************
 * @file       XPushButton.c
 * @brief      XPushButton 按钮控件实现（对标 Qt 6.8 QPushButton，继承 XAbstractButton）。
 * @details    实现要点：
 *             - 继承 XAbstractButton（对齐 Qt QPushButton : QAbstractButton），
 *               QAbstractButton 的公共状态、激活流程、自动重复/动画定时器、
 *               自动互斥、输入事件与 pressed/released/clicked/toggled 信号
 *               全部由基类提供，本文件不再重复实现；
 *             - 本类只实现 QPushButton 特有部分：
 *               - autoDefault/default/flat 三态与父对话框链自动解析；
 *               - setMenu/menu/showMenu 菜单关联（平台弹层未接入）；
 *               - sizeHint/minimumSizeHint 与 drawContents 绘制入口；
 *               - 键盘 Return/Enter 在默认或 autoDefault 生效时触发 click，
 *                 Space/Escape 等按键交基类处理；
 *               - 重载 XAbstractButton 的 contentChanged 保护槽，在
 *                 文本/图标/图标尺寸变化后刷新自身 sizeHint 存储位；
 *             - 生命周期走 XClass 体系：XPushButton_init/create_ex 挂
 *               XPushButton 虚表，copy/move/deinit 经 XClass_Parent 先调
 *               XAbstractButton 基类槽位再处理本类特有字段；
 *             - 绘制：XPainter 输出 raised/sunken/flat 外观，文本经当前
 *               XFont（内置 8x16 点阵字库回退），图标走 XIcon_paint；
 *             - 不依赖任何平台 API；嵌入式由 XPUSHBUTTON_ON 裁剪，且
 *               依赖 XABSTRACTBUTTON_ON（基类裁剪时本类一并裁剪）。
 * @note       近似边界：显式 QButtonGroup 登记未实现；同一父控件的
 *             autoExclusive 互斥已由基类按 Qt 规则处理；样式 bevel、快捷键
 *             与真实平台菜单弹层未实现；菜单关联接口按 Qt 借用语义提供，
 *             showMenu 仅同步按下状态并重绘，不进入阻塞式弹层循环。
 * @author     XinYueC 团队
 ******************************************************************************/
#include "XPushButton.h"
#include "XAbstractButton_Protected.h"
#include "XWidget_Protected.h"
#include "XPainter.h"
#include "XPalette.h"
#include "XColor.h"
#include "XFont.h"
#include "XWindowEvent.h"
#include "XMemory.h"
#include "XString.h"
#include "XAlignment.h"
#include <string.h>

#if XWIDGET_ON && XABSTRACTBUTTON_ON && XPUSHBUTTON_ON

/* ==================== 内部工具 ==================== */

/** @brief 从控件调色板读取颜色；调色板裁剪时回退黑色。 */
static uint32_t pushbutton_color(const XPushButton* self,
                                 XPaletteColorGroup group,
                                 XPaletteColorRole role)
{
#if XPALETTE_ON
    XPalette palette;
    XColor color;
    if (!self) return 0xFF000000u;
    palette = XWidget_palette((const XWidget*)self);
    color = XPalette_color(&palette, group, role);
    return XColor_rgba(&color);
#else
    (void)self;
    (void)group;
    (void)role;
    return 0xFF000000u;
#endif /* XPALETTE_ON */
}

/** @brief 是否三态 Auto 且当前上下文判定为对话框默认按钮。
 * @details 对标 Qt 6.8 QPushButtonPrivate::dialogParent/autoDefault：
 *          沿父链向上遍历到第一个窗口前，若命中窗口类型为 Dialog 的
 *          父控件则返回 true；按钮自身为窗口或父链无对话框返回 false。 */
static bool pushbutton_autoDefaultActive(const XPushButton* self)
{
    const XWidget* p = (const XWidget*)self;

    while (p && !XWidget_isWindow(p)) {
        p = XWidget_parentWidget(p);
        if (p && XWidget_windowType(p) == XWindowType_Dialog)
            return true;
    }
    return false;
}

/** @brief 是否把当前尺寸视为显式图标尺寸（字段位于 XAbstractButton 基类）。 */
static bool pushbutton_hasIconSize(const XPushButton* self)
{
    const XAbstractButton* ab = (const XAbstractButton*)self;

    return ab && XSize_isValid(&ab->m_iconSize) &&
           ab->m_iconSize.width > 0 && ab->m_iconSize.height > 0;
}

/** @brief 返回按钮图标用于尺寸提示的渲染尺寸（未显式设置按 PM_ButtonIconSize=16）。 */
static XSize pushbutton_hintIconSize(const XPushButton* self)
{
    const XAbstractButton* ab = (const XAbstractButton*)self;
    XSize out;

    XSize_init(&out, 16, 16);
    if (pushbutton_hasIconSize(self))
        out = ab->m_iconSize;
    return out;
}

/** @brief 按 Qt 6.8 QPushButton::sizeHint 内容语义计算建议尺寸。 */
static XSize pushbutton_computeSizeHint(const XPushButton* self)
{
    const XAbstractButton* ab = (const XAbstractButton*)self;
    XSize out;
    XSize iconSize;
    const char* text;
    bool hasIcon;
    bool empty;
    int w = 0;
    int h = 0;
    XFont font;
    if (!self) { XSize_init(&out, -1, -1); return out; }
    hasIcon = !XIcon_isNull(&ab->m_icon);
    if (hasIcon) {
        iconSize = pushbutton_hintIconSize(self);
        w += iconSize.width + 4;
        if (h < iconSize.height) h = iconSize.height;
    }
    font = XWidget_font((XWidget*)self);
    text = ab->m_text ? XString_toUtf8(ab->m_text) : NULL;
    if (!text) text = "";
    empty = text[0] == '\0';
    if (empty) {
        int placeholder;
        placeholder = XPainter_textWidth(&font, "XXXX");
        if (!w) w += placeholder;
        if (!h) h = XPainter_textHeight(&font);
    } else {
        w += XPainter_textWidth(&font, text);
        if (h < XPainter_textHeight(&font))
            h = XPainter_textHeight(&font);
    }
    XFont_deinit_base(&font);
    if (self->m_menu)
        w += 12; /* PM_MenuButtonIndicator */
    w += 6 + 4; /* PM_ButtonMargin + PM_DefaultFrameWidth * 2 */
    h += 6 + 4;
    XSize_init(&out, w, h);
    return out;
}

/** @brief 刷新基类 XWidget 的 sizeHint/minimumSizeHint 存储位。 */
static void pushbutton_refreshSizeHint(XPushButton* self)
{
    XSize hint;
    if (!self) return;
    hint = pushbutton_computeSizeHint(self);
    XWidget_setSizeHint((XWidget*)self, &hint);
    XWidget_setMinimumSizeHint((XWidget*)self, &hint);
}

/* ==================== 生命周期（对标 QPushButton 构造/析构） ==================== */

static void VXPushButton_copy(XPushButton* self, const XPushButton* other);
static void VXPushButton_move(XPushButton* self, XPushButton* other);
static void VXPushButton_deinit(XPushButton* self);

void XPushButton_init(XPushButton* self, XWidget* parent, XWidgetFlags flags)
{
    XWidgetSizePolicy policy;
    if (!self) return;
    memset(self, 0, sizeof(XPushButton));
    /* 基类初始化：文本/图标/状态位/定时器/调色板角色/焦点策略全部由
       XAbstractButton_init 设置 Qt 默认值。 */
    XAbstractButton_init(&self->m_base, parent, flags);
    XClassSetVtable(self, XPushButton);
    self->m_flat = false;
    self->m_defaultButton = false;
    self->m_autoDefault = XPushButtonAutoDefault_Auto;
    self->m_menu = NULL;
    policy = XWidgetSizePolicy_create_ex(XWidgetSizePolicy_Minimum,
                                         XWidgetSizePolicy_Fixed,
                                         XWidgetSizePolicyControl_PushButton);
    XWidget_setSizePolicyFull((XWidget*)self, &policy);
    pushbutton_refreshSizeHint(self);
}

XPushButton* XPushButton_create_ex(XMemoryType memory, XWidget* parent,
                                   XWidgetFlags flags)
{
    XPushButton* self = (XPushButton*)XMemory_malloc(sizeof(XPushButton), memory);
    if (!self) return NULL;
    memset(self, 0, sizeof(XPushButton));
    XPushButton_init(self, parent, flags);
    Set_Class_Memory(self, memory);
    Set_Class_IsHeap(self, true);
    return self;
}

static void VXPushButton_copy(XPushButton* self, const XPushButton* other)
{
    if (!self || !other || self == other) return;
    if (XClassIsVtableNull(self)) XPushButton_init(self, NULL, 0);
    XClass_Parent(XAbstractButton, EXClass_Copy,
                  void(*)(XAbstractButton*, const XAbstractButton*))(
                      (XAbstractButton*)self, (const XAbstractButton*)other);
    self->m_flat = other->m_flat;
    self->m_defaultButton = other->m_defaultButton;
    self->m_autoDefault = other->m_autoDefault;
    self->m_menu = other->m_menu;
}

static void VXPushButton_move(XPushButton* self, XPushButton* other)
{
    if (!self || !other || self == other) return;
    if (XClassIsVtableNull(self)) XPushButton_init(self, NULL, 0);
    XClass_Parent(XAbstractButton, EXClass_Move,
                  void(*)(XAbstractButton*, XAbstractButton*))(
                      (XAbstractButton*)self, (XAbstractButton*)other);
    self->m_flat = other->m_flat;
    self->m_defaultButton = other->m_defaultButton;
    self->m_autoDefault = other->m_autoDefault;
    self->m_menu = other->m_menu;
    other->m_menu = NULL;
    other->m_flat = false;
    other->m_defaultButton = false;
    other->m_autoDefault = XPushButtonAutoDefault_Auto;
}

static void VXPushButton_deinit(XPushButton* self)
{
    if (!self) return;
    self->m_menu = NULL;
    XClass_Deinit_Parent(XAbstractButton, (XAbstractButton*)self);
}

/* ==================== 命中（QAbstractButton protected，经基类虚表分派） ==================== */

bool XPushButton_hitButton(const XPushButton* self, const XPoint* pos)
{
    return XAbstractButton_hitButton_base((const XAbstractButton*)self, pos);
}

/* ==================== 默认/扁平（对标 QPushButton） ==================== */

bool XPushButton_autoDefault(const XPushButton* self)
{
    if (!self) return false;
    if (self->m_autoDefault == XPushButtonAutoDefault_On)
        return true;
    if (self->m_autoDefault == XPushButtonAutoDefault_Off)
        return false;
    return pushbutton_autoDefaultActive(self);
}

void XPushButton_setAutoDefault(XPushButton* self, bool enable)
{
    XPushButtonAutoDefault value;
    if (!self) return;
    value = enable ? XPushButtonAutoDefault_On : XPushButtonAutoDefault_Off;
    if (self->m_autoDefault == value) return;
    self->m_autoDefault = value;
    pushbutton_refreshSizeHint(self);
    XWidget_updateGeometry((XWidget*)self);
    XWidget_update((XWidget*)self);
}

bool XPushButton_isDefault(const XPushButton* self)
{
    return self ? self->m_defaultButton : false;
}

void XPushButton_setDefault(XPushButton* self, bool enable)
{
    if (!self || self->m_defaultButton == enable) return;
    self->m_defaultButton = enable;
    XWidget_update((XWidget*)self);
}

bool XPushButton_isFlat(const XPushButton* self)
{
    return self ? self->m_flat : false;
}

void XPushButton_setFlat(XPushButton* self, bool flat)
{
    if (!self || self->m_flat == flat) return;
    self->m_flat = flat;
    pushbutton_refreshSizeHint(self);
    XWidget_updateGeometry((XWidget*)self);
    XWidget_update((XWidget*)self);
}

/* ==================== 菜单（对标 QPushButton setMenu/menu/showMenu） ==================== */

void XPushButton_setMenu(XPushButton* self, XMenu* menu)
{
    if (!self || self->m_menu == menu) return;
    self->m_menu = menu;
    pushbutton_refreshSizeHint(self);
    XWidget_updateGeometry((XWidget*)self);
    XWidget_update((XWidget*)self);
}

XMenu* XPushButton_menu(const XPushButton* self)
{
    return self ? self->m_menu : NULL;
}

void XPushButton_showMenu(XPushButton* self)
{
    if (!self || !self->m_menu) return;
    if (!XWidget_isEnabled((XWidget*)self)) return;
    /* 对标 Qt QPushButton::showMenu 的 setDown(true)：只进入按下状态并
       重绘，不发 pressed 信号；平台弹层接入后应在此打开菜单并在关闭时
       恢复 down。 */
    XAbstractButton_setDown((XAbstractButton*)self, true);
}

/* ==================== 尺寸（对标 QPushButton sizeHint/minimumSizeHint） ==================== */

XSize XPushButton_sizeHint(const XPushButton* self)
{
    return pushbutton_computeSizeHint(self);
}

XSize XPushButton_minimumSizeHint(const XPushButton* self)
{
    return pushbutton_computeSizeHint(self);
}

/* ==================== 绘制（对标 QPushButton::paintEvent 内容） ==================== */

void XPushButton_drawContents(XPushButton* self, XPainter* painter)
{
    XAbstractButton* ab = (XAbstractButton*)self;
    XRect rect, content;
    uint32_t bg, textColor, light, dark, mid;
    XPaletteColorGroup group;
    bool pressed;
    XFont font;
    const char* text;
    bool drawIcon;
    int iconW, iconH, iconX, iconY;

    if (!self || !painter) return;
    if (!XPainter_save(painter)) return;
    rect = XWidget_rect((XWidget*)self);
    group = XWidget_isEnabled((XWidget*)self) ? XPaletteColorGroup_Current
                                              : XPaletteColorGroup_Disabled;
#if XPALETTE_ON
    light = pushbutton_color(self, group, XPaletteColorRole_Light);
    dark = pushbutton_color(self, group, XPaletteColorRole_Dark);
    mid = pushbutton_color(self, group, XPaletteColorRole_Mid);
#else
    light = 0xFFE0E0E0u;
    dark = 0xFF808080u;
    mid = 0xFFA0A0A0u;
#endif
    bg = pushbutton_color(self, group, XPaletteColorRole_Button);
    textColor = pushbutton_color(self, group, XPaletteColorRole_ButtonText);
    if (bg == 0u)
        bg = 0xFFCFCFCFu;
    pressed = ab->m_down || ab->m_checked;

    XPainter_fillRect(painter, &rect, bg);
    if (!self->m_flat && rect.width > 2 && rect.height > 2) {
        XRect edge;
        if (!pressed) {
            edge.x = rect.x; edge.y = rect.y;
            edge.width = rect.width; edge.height = 1;
            XPainter_fillRect(painter, &edge, light);
            edge.x = rect.x; edge.y = rect.y;
            edge.width = 1; edge.height = rect.height;
            XPainter_fillRect(painter, &edge, light);
            edge.x = rect.x; edge.y = rect.y + rect.height - 1;
            edge.width = rect.width; edge.height = 1;
            XPainter_fillRect(painter, &edge, dark);
            edge.x = rect.x + rect.width - 1; edge.y = rect.y;
            edge.width = 1; edge.height = rect.height;
            XPainter_fillRect(painter, &edge, mid);
        } else {
            edge.x = rect.x; edge.y = rect.y;
            edge.width = rect.width; edge.height = 1;
            XPainter_fillRect(painter, &edge, dark);
            edge.x = rect.x; edge.y = rect.y;
            edge.width = 1; edge.height = rect.height;
            XPainter_fillRect(painter, &edge, mid);
            edge.x = rect.x; edge.y = rect.y + rect.height - 1;
            edge.width = rect.width; edge.height = 1;
            XPainter_fillRect(painter, &edge, light);
            edge.x = rect.x + rect.width - 1; edge.y = rect.y;
            edge.width = 1; edge.height = rect.height;
            XPainter_fillRect(painter, &edge, light);
        }
    }

    font = XWidget_font((XWidget*)self);
    XPainter_setFont(painter, &font);
    text = XString_toUtf8(ab->m_text ? ab->m_text : NULL);
    drawIcon = !XIcon_isNull(&ab->m_icon);
    iconW = 16;
    iconH = 16;
    if (pushbutton_hasIconSize(self)) {
        iconW = ab->m_iconSize.width;
        iconH = ab->m_iconSize.height;
    }
    content = rect;
    if (drawIcon) {
        iconX = rect.x + (rect.width - iconW) / 2;
        iconY = rect.y + (rect.height - iconH) / 2;
        if (text && text[0] != '\0') {
            iconX = rect.x + 4;
            iconY = rect.y + (rect.height - iconH) / 2;
            content.x += iconW + 4;
            content.width -= iconW + 4;
        }
        XIcon_paint(&ab->m_icon, painter, iconX, iconY, iconW, iconH,
                    XAlignment_Center,
                    XWidget_isEnabled((XWidget*)self) ? XIconMode_Normal
                                                      : XIconMode_Disabled,
                    ab->m_checked ? XIconState_On : XIconState_Off);
    }
    if (text && text[0] != '\0') {
        if (content.width < 4) content.width = 4;
#if XPAINTER_TEXTLAYOUT_ON
        XPainter_drawTextRect(painter, &content,
                              XPAINTER_TEXT_ALIGN_CENTER | XPAINTER_TEXT_SINGLE_LINE,
                              text, textColor);
#else
        /* 文本布局被裁剪时保留按钮可用性：用点阵字体度量做单行居中。 */
        {
            int textW = XPainter_textWidth(&font, text);
            int textH = XPainter_textHeight(&font);
            int ascent = XPainter_textAscent(&font);
            int textX = content.x + (content.width - textW) / 2;
            int baseline = content.y + (content.height - textH) / 2 + ascent;
            XPainter_drawText(painter, textX, baseline, text, textColor);
        }
#endif /* XPAINTER_TEXTLAYOUT_ON */
    }
    XFont_deinit_base(&font);
    if (self->m_menu && rect.width >= 12 && rect.height >= 7) {
        int arrowX = rect.x + rect.width - 9;
        int arrowY = rect.y + (rect.height / 2) - 2;
        XRect tri;
        tri.x = arrowX + 2; tri.y = arrowY; tri.width = 1; tri.height = 1;
        XPainter_fillRect(painter, &tri, textColor);
        tri.x = arrowX + 1; tri.y = arrowY + 1; tri.width = 3; tri.height = 1;
        XPainter_fillRect(painter, &tri, textColor);
        tri.x = arrowX; tri.y = arrowY + 2; tri.width = 5; tri.height = 1;
        XPainter_fillRect(painter, &tri, textColor);
        tri.x = arrowX + 1; tri.y = arrowY + 3; tri.width = 3; tri.height = 1;
        XPainter_fillRect(painter, &tri, textColor);
        tri.x = arrowX + 2; tri.y = arrowY + 4; tri.width = 1; tri.height = 1;
        XPainter_fillRect(painter, &tri, textColor);
    }
    XPainter_restore(painter);
}

/* ==================== 虚槽实现（对标 QPushButton 事件） ==================== */

static void VXPushButton_paintEvent(XWidget* self, XEvent* event)
{
#if !XPALETTE_ON || !XWINDOWEVENT_ON
    (void)self;
    (void)event;
    return;
#else
    XImage* image;
    XPoint offset;
    XPainter painter;
#if XPAINTER_CLIP_ON
    XPaintEvent* pe;
    XRect clip;
#endif
    if (!self || !event || XEvent_type(event) != XEVENT_TYPE_PAINT) return;
#if XPAINTER_CLIP_ON
    pe = (XPaintEvent*)event;
#endif
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
#if XPAINTER_CLIP_ON
    clip = XPaintEvent_rect(pe);
    XPainter_setClipRect(&painter, &clip, XPainterClipOperation_ReplaceClip);
#endif
    XPushButton_drawContents((XPushButton*)self, &painter);
    XPainter_end(&painter);
    XPainter_deinit(&painter);
#endif /* XPALETTE_ON && XWINDOWEVENT_ON */
}

static void VXPushButton_keyPressEvent(XWidget* self, XEvent* event)
{
    XPushButton* button = (XPushButton*)self;
    XKeyEvent* keyEvent;
    int key;

    if (!button || !event || XEvent_type(event) != XEVENT_TYPE_KEY_PRESS)
        return;
    keyEvent = (XKeyEvent*)event;
    key = XKeyEvent_key(keyEvent);
    /* 对标 QPushButton::keyPressEvent：Enter/Return 仅在默认或
       autoDefault 生效时触发 click，其余按键（Space/Escape 等）交给
       XAbstractButton 基类处理。 */
    if (key == XKey_Return || key == XKey_Enter) {
        if (XPushButton_autoDefault(button) || XPushButton_isDefault(button)) {
            XPushButton_click(button);
            XEvent_accept(event);
            return;
        }
    }
    XClass_Parent(XAbstractButton, EXWidget_KeyPressEvent,
                  void(*)(XWidget*, XEvent*))((XWidget*)self, event);
}

/* ==================== 内容变更虚槽（刷新本类 sizeHint 存储位） ==================== */

/**
 * @brief      重载 XAbstractButton 的 contentChanged 保护槽。
 * @details    XWidget 的 sizeHint/minimumSizeHint 是存储位而非虚函数，
 *             基类 setText/setIcon/setIconSize 不会自动重算派生尺寸；
 *             本槽在基类内容变更后、刷新几何之前重算并写回存储位。
 */
static void VXPushButton_contentChanged(XAbstractButton* base)
{
    XPushButton* self = (XPushButton*)base;

    if (self)
        pushbutton_refreshSizeHint(self);
}

/* ==================== 类虚表 ==================== */

XVtable* XPushButton_class_init(void)
{
    XVTABLE_INIT_DEFAULT(XPushButton)
    XVTABLE_INHERIT_XCLASS(XAbstractButton);
    XVTABLE_OVERLOAD_DEFAULT(EXAbstractButton_ContentChanged,
                             VXPushButton_contentChanged);
    XVTABLE_OVERLOAD_DEFAULT(EXWidget_PaintEvent, VXPushButton_paintEvent);
    XVTABLE_OVERLOAD_DEFAULT(EXWidget_KeyPressEvent,
                             VXPushButton_keyPressEvent);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Copy, VXPushButton_copy);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Move, VXPushButton_move);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Deinit, VXPushButton_deinit);
    /* 登记派生虚表：XAbstractButton 的自动互斥/实例识别（isInstance）
       依赖该登记表，必须在类虚表构建完成后调用。 */
    XAbstractButton_registerClass(XVTABLE_DEFAULT);
    return XVTABLE_DEFAULT;
}

#endif /* XWIDGET_ON && XABSTRACTBUTTON_ON && XPUSHBUTTON_ON */
