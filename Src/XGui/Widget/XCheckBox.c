/******************************************************************************
 * @file       XCheckBox.c
 * @brief      XCheckBox 复选框控件实现（对标 Qt 6.8 QCheckBox，继承 XAbstractButton）。
 * @details    实现要点：
 *             - 继承 XAbstractButton（对齐 Qt QCheckBox : QAbstractButton），
 *               QAbstractButton 的公共状态/激活/自动重复/自动互斥/输入事件
 *               与 pressed/released/clicked/toggled 信号全部由基类提供；
 *             - 本类只实现 QCheckBox 特有部分：
 *               - 三态状态模型：基类 m_checked 是真值源，PartiallyChecked
 *                 通过 tristate+noChange 表达，checkState() 派生计算；
 *               - 重载 nextCheckState（三态 (state+1)%3 循环，非三态交基类
 *                 反转）与 checkStateSet（基类 setChecked 后同步并发出
 *                 checkStateChanged）；setCheckState 期间用 m_blockRefresh
 *                 抑制基类路径重复发信号（对标 Qt 6.8 QCheckBoxPrivate
 *                 blockRefresh）；
 *               - 重载 hitButton（indicator 矩形命中，对标 Qt
 *                 SE_CheckBoxClickRect）与 PaintEvent/ContentChanged；
 *               - drawContents 绘制 Window 背景 + indicator 方块边框 +
 *                 勾/横线 + 图标 + 文本；
 *             - 生命周期：copy/move 经 XClass_Parent 先调 XAbstractButton
 *               基类槽位再处理三态字段；无拥有资源故不重载 deinit；
 *             - 不依赖任何平台 API；嵌入式由 XCHECKBOX_ON 裁剪，且依赖
 *               XABSTRACTBUTTON_ON。
 * @note       近似边界：快捷键、样式表 bevel、悬停效果与 Qt 6.9 起废弃的
 *             stateChanged(int) 信号未实现；hitButton 按 indicator 矩形。
 * @author     XinYueC 团队
 ******************************************************************************/
#include "XCheckBox.h"
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
#include "XVarList.h"
#include <string.h>

#if XWIDGET_ON && XABSTRACTBUTTON_ON && XCHECKBOX_ON

/* ==================== 内部工具 ==================== */

/** @brief indicator 边长（对标 QCommonStyle PM_IndicatorWidth/PM_IndicatorHeight=13）。 */
#define CHECKBOX_INDICATOR_SIZE 13
/** @brief indicator 与文本间距（对标 PM_CheckBoxLabelSpacing=6）。 */
#define CHECKBOX_LABEL_SPACING 6

/** @brief 从控件调色板读取颜色；调色板裁剪时回退黑色。 */
static uint32_t checkbox_color(const XCheckBox* self,
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

/** @brief 计算 indicator 矩形（控件左缘、垂直居中，边长 13）。 */
static XRect checkbox_indicatorRect(const XCheckBox* self)
{
    XRect rect = XWidget_rect((const XWidget*)self);
    XRect ind;

    ind.x = rect.x;
    ind.y = rect.y + (rect.height - CHECKBOX_INDICATOR_SIZE) / 2;
    ind.width = CHECKBOX_INDICATOR_SIZE;
    ind.height = CHECKBOX_INDICATOR_SIZE;
    return ind;
}

/** @brief 是否把当前尺寸视为显式图标尺寸（字段位于 XAbstractButton 基类）。 */
static bool checkbox_hasIconSize(const XCheckBox* self)
{
    const XAbstractButton* ab = (const XAbstractButton*)self;

    return ab && XSize_isValid(&ab->m_iconSize) &&
           ab->m_iconSize.width > 0 && ab->m_iconSize.height > 0;
}

/** @brief 发射携带选中状态的信号；无接收者时立即释放参数列表。 */
static void checkbox_emitState(XCheckBox* self, XCheckState state)
{
    XVarList* args = XVarList_Create(XVar(int, (int)state));

    if (!args)
        return;
    if (self && ((XObject*)self)->m_signalSlot) {
        XObject_emitSignal((XObject*)self,
                           (size_t)XCheckBox_checkStateChanged_signal,
                           args, NULL, NULL, XEVENT_PRIORITY_NORMAL);
    } else {
        XVarList_delete(args);
    }
}

/** @brief 按 QCommonStyle 基础数值计算建议尺寸。 */
static XSize checkbox_computeSizeHint(const XCheckBox* self)
{
    const XAbstractButton* ab = (const XAbstractButton*)self;
    XSize out;
    XFont font;
    const char* text;
    bool hasIcon;
    XSize iconSize;
    int w = 0;
    int h = 0;

    if (!self) { XSize_init(&out, -1, -1); return out; }
    w = CHECKBOX_INDICATOR_SIZE + CHECKBOX_LABEL_SPACING;
    h = CHECKBOX_INDICATOR_SIZE;
    hasIcon = !XIcon_isNull(&ab->m_icon);
    font = XWidget_font((XWidget*)self);
    text = ab->m_text ? XString_toUtf8(ab->m_text) : NULL;
    if (!text) text = "";
    if (text[0] == '\0') {
        w += XPainter_textWidth(&font, "XXXX");
        if (h < XPainter_textHeight(&font))
            h = XPainter_textHeight(&font);
    } else {
        w += XPainter_textWidth(&font, text);
        if (h < XPainter_textHeight(&font))
            h = XPainter_textHeight(&font);
    }
    XFont_deinit_base(&font);
    if (hasIcon) {
        XSize_init(&iconSize, 16, 16);
        if (checkbox_hasIconSize(self))
            iconSize = ab->m_iconSize;
        w += iconSize.width + 4;
        if (h < iconSize.height)
            h = iconSize.height;
    }
    w += 4; /* 左右边距 */
    h += 4;
    XSize_init(&out, w, h);
    return out;
}

/** @brief 刷新基类 XWidget 的 sizeHint/minimumSizeHint 存储位。 */
static void checkbox_refreshSizeHint(XCheckBox* self)
{
    XSize hint;
    if (!self) return;
    hint = checkbox_computeSizeHint(self);
    XWidget_setSizeHint((XWidget*)self, &hint);
    XWidget_setMinimumSizeHint((XWidget*)self, &hint);
}

/* ==================== 三态（对标 QCheckBox tristate/checkState） ==================== */

void XCheckBox_setTristate(XCheckBox* self, bool tristate)
{
    if (!self || self->m_tristate == tristate)
        return;
    self->m_tristate = tristate;
    if (!tristate) {
        /* 关闭三态时回到二态：清除 noChange，PartiallyChecked 落到 Unchecked。 */
        self->m_noChange = false;
    }
    XWidget_update((XWidget*)self);
}

bool XCheckBox_isTristate(const XCheckBox* self)
{
    return self ? self->m_tristate : false;
}

XCheckState XCheckBox_checkState(const XCheckBox* self)
{
    if (!self)
        return XCheckState_Unchecked;
    if (self->m_tristate && self->m_noChange)
        return XCheckState_PartiallyChecked;
    return ((const XAbstractButton*)self)->m_checked
               ? XCheckState_Checked
               : XCheckState_Unchecked;
}

void XCheckBox_setCheckState(XCheckBox* self, XCheckState state)
{
    XCheckState old;

    if (!self)
        return;
    if (state == XCheckState_PartiallyChecked) {
        self->m_tristate = true;
        self->m_noChange = true;
    } else {
        self->m_noChange = false;
    }
    /* 经基类 setChecked 写入真值源；blockRefresh 抑制 checkStateSet 槽
       在本次写入期间发信号，由下面按 publishedState 统一去重后发出。 */
    self->m_blockRefresh = true;
    XAbstractButton_setChecked((XAbstractButton*)self,
                               state != XCheckState_Unchecked);
    self->m_blockRefresh = false;
    XWidget_update((XWidget*)self);
    old = self->m_publishedState;
    if (state != old) {
        self->m_publishedState = state;
        checkbox_emitState(self, state);
    }
}

void* XCheckBox_checkStateChanged_signal(XCheckBox* self, XCheckState state)
{
    if (!self)
        return (void*)(size_t)XCheckBox_checkStateChanged_signal;
    checkbox_emitState(self, state);
    return (void*)(size_t)XCheckBox_checkStateChanged_signal;
}

/* ==================== 命中（对标 QCheckBox::hitButton） ==================== */

bool XCheckBox_hitButton(const XCheckBox* self, const XPoint* pos)
{
    return XAbstractButton_hitButton_base((const XAbstractButton*)self, pos);
}

/* ==================== 尺寸（对标 QCheckBox sizeHint/minimumSizeHint） ==================== */

XSize XCheckBox_sizeHint(const XCheckBox* self)
{
    return checkbox_computeSizeHint(self);
}

XSize XCheckBox_minimumSizeHint(const XCheckBox* self)
{
    return checkbox_computeSizeHint(self);
}

/* ==================== 绘制（对标 QCheckBox::paintEvent 内容） ==================== */

void XCheckBox_drawContents(XCheckBox* self, XPainter* painter)
{
    XAbstractButton* ab = (XAbstractButton*)self;
    XRect rect, ind, textRect;
    XPaletteColorGroup group;
    uint32_t window, textColor, base, light, dark, mid;
    XFont font;
    const char* text;
    XCheckState state;
    bool drawIcon;
    int iconW, iconH, iconX, iconY;

    if (!self || !painter) return;
    if (!XPainter_save(painter)) return;
    rect = XWidget_rect((XWidget*)self);
    group = XWidget_isEnabled((XWidget*)self) ? XPaletteColorGroup_Current
                                              : XPaletteColorGroup_Disabled;
#if XPALETTE_ON
    light = checkbox_color(self, group, XPaletteColorRole_Light);
    dark = checkbox_color(self, group, XPaletteColorRole_Dark);
    mid = checkbox_color(self, group, XPaletteColorRole_Mid);
    base = checkbox_color(self, group, XPaletteColorRole_Base);
    window = checkbox_color(self, group, XPaletteColorRole_Window);
    textColor = checkbox_color(self, group, XPaletteColorRole_WindowText);
#else
    light = 0xFFE0E0E0u;
    dark = 0xFF808080u;
    mid = 0xFFA0A0A0u;
    base = 0xFFFFFFFFu;
    window = 0xFFCFCFCFu;
    textColor = 0xFF000000u;
#endif
    if (window == 0u)
        window = 0xFFCFCFCFu;
    if (base == 0u)
        base = 0xFFFFFFFFu;
    if (textColor == 0u)
        textColor = 0xFF000000u;

    XPainter_fillRect(painter, &rect, window);

    /* indicator 方块：Base 背景 + 凸起边框。 */
    ind = checkbox_indicatorRect(self);
    XPainter_fillRect(painter, &ind, base);
    if (ind.width > 2 && ind.height > 2) {
        XRect edge;
        edge.x = ind.x; edge.y = ind.y;
        edge.width = ind.width; edge.height = 1;
        XPainter_fillRect(painter, &edge, light);
        edge.x = ind.x; edge.y = ind.y;
        edge.width = 1; edge.height = ind.height;
        XPainter_fillRect(painter, &edge, light);
        edge.x = ind.x; edge.y = ind.y + ind.height - 1;
        edge.width = ind.width; edge.height = 1;
        XPainter_fillRect(painter, &edge, dark);
        edge.x = ind.x + ind.width - 1; edge.y = ind.y;
        edge.width = 1; edge.height = ind.height;
        XPainter_fillRect(painter, &edge, mid);
    }

    /* 勾 / 横线。 */
    state = XCheckBox_checkState(self);
    XPainter_setPen(painter, textColor);
    if (state == XCheckState_Checked) {
        XPainter_drawLine(painter, ind.x + 3, ind.y + 7,
                          ind.x + 6, ind.y + 10);
        XPainter_drawLine(painter, ind.x + 6, ind.y + 10,
                          ind.x + 10, ind.y + 3);
    } else if (state == XCheckState_PartiallyChecked) {
        XPainter_drawLine(painter, ind.x + 3, ind.y + 7,
                          ind.x + 10, ind.y + 7);
    }

    /* 图标（indicator 与文本之间）。 */
    font = XWidget_font((XWidget*)self);
    XPainter_setFont(painter, &font);
    text = XString_toUtf8(ab->m_text ? ab->m_text : NULL);
    drawIcon = !XIcon_isNull(&ab->m_icon);
    iconW = 16;
    iconH = 16;
    if (checkbox_hasIconSize(self)) {
        iconW = ab->m_iconSize.width;
        iconH = ab->m_iconSize.height;
    }
    textRect.x = ind.x + ind.width + CHECKBOX_LABEL_SPACING;
    textRect.y = rect.y;
    textRect.width = rect.width - (textRect.x - rect.x);
    textRect.height = rect.height;
    if (drawIcon) {
        iconX = textRect.x;
        iconY = rect.y + (rect.height - iconH) / 2;
        textRect.x += iconW + 4;
        textRect.width -= iconW + 4;
        XIcon_paint(&ab->m_icon, painter, iconX, iconY, iconW, iconH,
                    XAlignment_Center,
                    XWidget_isEnabled((XWidget*)self) ? XIconMode_Normal
                                                      : XIconMode_Disabled,
                    ab->m_checked ? XIconState_On : XIconState_Off);
    }
    if (text && text[0] != '\0') {
        if (textRect.width < 4) textRect.width = 4;
#if XPAINTER_TEXTLAYOUT_ON
        XPainter_drawTextRect(painter, &textRect,
                              XPAINTER_TEXT_ALIGN_VCENTER |
                                  XPAINTER_TEXT_ALIGN_LEFT |
                                  XPAINTER_TEXT_SINGLE_LINE,
                              text, textColor);
#else
        {
            int textW = XPainter_textWidth(&font, text);
            int textH = XPainter_textHeight(&font);
            int ascent = XPainter_textAscent(&font);
            int textX = textRect.x;
            int baseline = textRect.y +
                           (textRect.height - textH) / 2 + ascent;
            XPainter_drawText(painter, textX, baseline, text, textColor);
        }
#endif /* XPAINTER_TEXTLAYOUT_ON */
    }
    XFont_deinit_base(&font);
    XPainter_restore(painter);
}

/* ==================== 虚槽实现（对标 QCheckBox 事件与保护钩子） ==================== */

/**
 * @brief      重载 checkStateSet 保护槽：基类 setChecked 后同步三态并发信号。
 * @details    清除 noChange（对标 Qt 6.8 qcheckbox.cpp:328-342），按
 *             publishedState 去重发出 checkStateChanged；setCheckState 期间
 *             （m_blockRefresh）跳过，避免与 setCheckState 自己的信号重复。
 */
static void VXCheckBox_checkStateSet(XAbstractButton* base)
{
    XCheckBox* self = (XCheckBox*)base;
    XCheckState state;

    if (!self || self->m_blockRefresh)
        return;
    self->m_noChange = false;
    state = XCheckBox_checkState(self);
    if (state != self->m_publishedState) {
        self->m_publishedState = state;
        checkbox_emitState(self, state);
    }
}

/**
 * @brief      重载 nextCheckState 保护槽（对标 Qt 6.8 qcheckbox.cpp:347-356）。
 * @details    三态时按 (state+1)%3 循环；非三态交基类反转 checked，信号
 *             经 checkStateSet 槽发出。
 */
static void VXCheckBox_nextCheckState(XAbstractButton* base)
{
    XCheckBox* self = (XCheckBox*)base;

    if (!self)
        return;
    if (self->m_tristate) {
        XCheckState s = XCheckBox_checkState(self);
        XCheckBox_setCheckState(self, (XCheckState)((s + 1) % 3));
    } else {
        XClass_Parent(XAbstractButton, EXAbstractButton_NextCheckState,
                      void(*)(XAbstractButton*))((XAbstractButton*)self);
    }
}

/** @brief 重载 hitButton 保护槽：indicator 矩形命中。 */
static bool VXCheckBox_hitButton(const XAbstractButton* base,
                                 const XPoint* pos)
{
    const XCheckBox* self = (const XCheckBox*)base;
    XRect ind;

    if (!self || !pos)
        return false;
    ind = checkbox_indicatorRect(self);
    return XRect_contains(&ind, pos->x, pos->y);
}

/** @brief 重载 ContentChanged 保护槽：文本/图标变化后刷新本类 sizeHint。 */
static void VXCheckBox_contentChanged(XAbstractButton* base)
{
    XCheckBox* self = (XCheckBox*)base;

    if (self)
        checkbox_refreshSizeHint(self);
}

static void VXCheckBox_paintEvent(XWidget* self, XEvent* event)
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
    XCheckBox_drawContents((XCheckBox*)self, &painter);
    XPainter_end(&painter);
    XPainter_deinit(&painter);
#endif /* XPALETTE_ON && XWINDOWEVENT_ON */
}

/* ==================== 复制/移动（对标 XClass 生命周期） ==================== */

static void VXCheckBox_copy(XCheckBox* self, const XCheckBox* other)
{
    if (!self || !other || self == other) return;
    if (XClassIsVtableNull(self)) XCheckBox_init(self, NULL, 0);
    XClass_Parent(XAbstractButton, EXClass_Copy,
                  void(*)(XAbstractButton*, const XAbstractButton*))(
                      (XAbstractButton*)self, (const XAbstractButton*)other);
    self->m_tristate = other->m_tristate;
    self->m_noChange = other->m_noChange;
    self->m_publishedState = other->m_publishedState;
    self->m_blockRefresh = false;
}

static void VXCheckBox_move(XCheckBox* self, XCheckBox* other)
{
    if (!self || !other || self == other) return;
    if (XClassIsVtableNull(self)) XCheckBox_init(self, NULL, 0);
    XClass_Parent(XAbstractButton, EXClass_Move,
                  void(*)(XAbstractButton*, XAbstractButton*))(
                      (XAbstractButton*)self, (XAbstractButton*)other);
    self->m_tristate = other->m_tristate;
    self->m_noChange = other->m_noChange;
    self->m_publishedState = other->m_publishedState;
    self->m_blockRefresh = false;
    other->m_tristate = false;
    other->m_noChange = false;
    other->m_publishedState = XCheckState_Unchecked;
}

/* ==================== 类虚表 ==================== */

XVtable* XCheckBox_class_init(void)
{
    XVTABLE_INIT_DEFAULT(XCheckBox)
    XVTABLE_INHERIT_XCLASS(XAbstractButton);
    XVTABLE_OVERLOAD_DEFAULT(EXAbstractButton_CheckStateSet,
                             VXCheckBox_checkStateSet);
    XVTABLE_OVERLOAD_DEFAULT(EXAbstractButton_NextCheckState,
                             VXCheckBox_nextCheckState);
    XVTABLE_OVERLOAD_DEFAULT(EXAbstractButton_HitButton,
                             VXCheckBox_hitButton);
    XVTABLE_OVERLOAD_DEFAULT(EXAbstractButton_ContentChanged,
                             VXCheckBox_contentChanged);
    XVTABLE_OVERLOAD_DEFAULT(EXWidget_PaintEvent, VXCheckBox_paintEvent);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Copy, VXCheckBox_copy);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Move, VXCheckBox_move);
    /* 登记派生虚表：XAbstractButton 的自动互斥/实例识别依赖该登记表。 */
    XAbstractButton_registerClass(XVTABLE_DEFAULT);
    return XVTABLE_DEFAULT;
}

void XCheckBox_init(XCheckBox* self, XWidget* parent, XWidgetFlags flags)
{
    if (!self) return;
    memset(self, 0, sizeof(XCheckBox));
    XAbstractButton_init(&self->m_base, parent, flags);
    XClassSetVtable(self, XCheckBox);
    /* 对标 Qt 6.8 QCheckBoxPrivate::init：checkable 默认开启。 */
    XAbstractButton_setCheckable((XAbstractButton*)self, true);
    self->m_tristate = false;
    self->m_noChange = false;
    self->m_blockRefresh = false;
    self->m_publishedState = XCheckState_Unchecked;
    checkbox_refreshSizeHint(self);
}

XCheckBox* XCheckBox_create_ex(XMemoryType memory, XWidget* parent,
                               XWidgetFlags flags)
{
    XCheckBox* self = (XCheckBox*)XMemory_malloc(sizeof(XCheckBox), memory);
    if (!self) return NULL;
    memset(self, 0, sizeof(XCheckBox));
    XCheckBox_init(self, parent, flags);
    Set_Class_Memory(self, memory);
    Set_Class_IsHeap(self, true);
    return self;
}

#endif /* XWIDGET_ON && XABSTRACTBUTTON_ON && XCHECKBOX_ON */
