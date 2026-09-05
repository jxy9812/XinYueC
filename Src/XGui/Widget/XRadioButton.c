/******************************************************************************
 * @file       XRadioButton.c
 * @brief      XRadioButton 单选按钮控件实现（对标 Qt 6.8 QRadioButton，继承 XAbstractButton）。
 * @details    实现要点：
 *             - 继承 XAbstractButton（对齐 Qt QRadioButton : QAbstractButton），
 *               QAbstractButton 的公共状态/激活/自动重复/自动互斥/输入事件
 *               与 pressed/released/clicked/toggled 信号全部由基类提供；
 *             - 构造时设置 checkable=true、autoExclusive=true、前景角色
 *               WindowText（对标 Qt 6.8 QRadioButtonPrivate::init，
 *               qradiobutton.cpp:35-38）；同一父控件下的单选按钮经基类
 *               autoExclusive 自动互斥；
 *             - 本类只实现 QRadioButton 特有部分：
 *               - 重载 hitButton（indicator 外接矩形命中，对标 Qt
 *                 SE_RadioButtonClickRect）；
 *               - drawContents 绘制 Window 背景 + 圆形 indicator + 选中
 *                 圆点 + 图标 + 文本；
 *               - 无特有字段，copy/move/deinit 继承 XAbstractButton 基类；
 *             - 不依赖任何平台 API；嵌入式由 XRADIOBUTTON_ON 裁剪，且依赖
 *               XABSTRACTBUTTON_ON。
 * @note       近似边界：快捷键、样式表 bevel、悬停效果未实现；选中圆点用
 *             中心方块近似（点阵后端无实心椭圆原语）；显式 QButtonGroup
 *             登记仍是裁剪项。
 * @author     XinYueC 团队
 ******************************************************************************/
#include "XRadioButton.h"
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

#if XWIDGET_ON && XABSTRACTBUTTON_ON && XRADIOBUTTON_ON

/* ==================== 内部工具 ==================== */

/** @brief indicator 边长（对标 QCommonStyle PM_IndicatorWidth/PM_IndicatorHeight=13）。 */
#define RADIOBUTTON_INDICATOR_SIZE 13
/** @brief indicator 与文本间距（对标 PM_RadioButtonLabelSpacing=6）。 */
#define RADIOBUTTON_LABEL_SPACING 6

/** @brief 从控件调色板读取颜色；调色板裁剪时回退黑色。 */
static uint32_t radiobutton_color(const XRadioButton* self,
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

/** @brief 计算 indicator 外接矩形（控件左缘、垂直居中，边长 13）。 */
static XRect radiobutton_indicatorRect(const XRadioButton* self)
{
    XRect rect = XWidget_rect((const XWidget*)self);
    XRect ind;

    ind.x = rect.x;
    ind.y = rect.y + (rect.height - RADIOBUTTON_INDICATOR_SIZE) / 2;
    ind.width = RADIOBUTTON_INDICATOR_SIZE;
    ind.height = RADIOBUTTON_INDICATOR_SIZE;
    return ind;
}

/** @brief 是否把当前尺寸视为显式图标尺寸（字段位于 XAbstractButton 基类）。 */
static bool radiobutton_hasIconSize(const XRadioButton* self)
{
    const XAbstractButton* ab = (const XAbstractButton*)self;

    return ab && XSize_isValid(&ab->m_iconSize) &&
           ab->m_iconSize.width > 0 && ab->m_iconSize.height > 0;
}

/** @brief 按 QCommonStyle 基础数值计算建议尺寸。 */
static XSize radiobutton_computeSizeHint(const XRadioButton* self)
{
    const XAbstractButton* ab = (const XAbstractButton*)self;
    XSize out;
    XFont font;
    const char* text;
    int w = 0;
    int h = 0;

    if (!self) { XSize_init(&out, -1, -1); return out; }
    w = RADIOBUTTON_INDICATOR_SIZE + RADIOBUTTON_LABEL_SPACING;
    h = RADIOBUTTON_INDICATOR_SIZE;
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
    w += 4; /* 左右边距 */
    h += 4;
    XSize_init(&out, w, h);
    return out;
}

/** @brief 刷新基类 XWidget 的 sizeHint/minimumSizeHint 存储位。 */
static void radiobutton_refreshSizeHint(XRadioButton* self)
{
    XSize hint;
    if (!self) return;
    hint = radiobutton_computeSizeHint(self);
    XWidget_setSizeHint((XWidget*)self, &hint);
    XWidget_setMinimumSizeHint((XWidget*)self, &hint);
}

/* ==================== 命中（对标 QRadioButton::hitButton） ==================== */

bool XRadioButton_hitButton(const XRadioButton* self, const XPoint* pos)
{
    return XAbstractButton_hitButton_base((const XAbstractButton*)self, pos);
}

/* ==================== 尺寸（对标 QRadioButton sizeHint/minimumSizeHint） ==================== */

XSize XRadioButton_sizeHint(const XRadioButton* self)
{
    return radiobutton_computeSizeHint(self);
}

XSize XRadioButton_minimumSizeHint(const XRadioButton* self)
{
    return radiobutton_computeSizeHint(self);
}

/* ==================== 绘制（对标 QRadioButton::paintEvent 内容） ==================== */

void XRadioButton_drawContents(XRadioButton* self, XPainter* painter)
{
    XAbstractButton* ab = (XAbstractButton*)self;
    XRect rect, ind, textRect;
    XPaletteColorGroup group;
    uint32_t window, textColor, light, dark, mid;
    XFont font;
    const char* text;

    if (!self || !painter) return;
    if (!XPainter_save(painter)) return;
    rect = XWidget_rect((XWidget*)self);
    group = XWidget_isEnabled((XWidget*)self) ? XPaletteColorGroup_Current
                                              : XPaletteColorGroup_Disabled;
#if XPALETTE_ON
    light = radiobutton_color(self, group, XPaletteColorRole_Light);
    dark = radiobutton_color(self, group, XPaletteColorRole_Dark);
    mid = radiobutton_color(self, group, XPaletteColorRole_Mid);
    window = radiobutton_color(self, group, XPaletteColorRole_Window);
    textColor = radiobutton_color(self, group, XPaletteColorRole_WindowText);
#else
    light = 0xFFE0E0E0u;
    dark = 0xFF808080u;
    mid = 0xFFA0A0A0u;
    window = 0xFFCFCFCFu;
    textColor = 0xFF000000u;
#endif
    if (window == 0u)
        window = 0xFFCFCFCFu;
    if (textColor == 0u)
        textColor = 0xFF000000u;

    XPainter_fillRect(painter, &rect, window);

    /* 圆形 indicator：外侧深色、内侧浅色双圈近似立体感。 */
    ind = radiobutton_indicatorRect(self);
    XPainter_setPen(painter, dark);
    XPainter_drawEllipse(painter, &ind);
    if (ind.width > 2 && ind.height > 2) {
        XRect inner;
        inner.x = ind.x + 1;
        inner.y = ind.y + 1;
        inner.width = ind.width - 2;
        inner.height = ind.height - 2;
        XPainter_setPen(painter, mid);
        XPainter_drawEllipse(painter, &inner);
        inner.x = ind.x + 2;
        inner.y = ind.y + 2;
        inner.width = ind.width - 4;
        inner.height = ind.height - 4;
        XPainter_setPen(painter, light);
        XPainter_drawEllipse(painter, &inner);
    }

    /* 选中圆点：中心方块近似（点阵后端无实心椭圆原语）。 */
    if (ab->m_checked) {
        XRect dot;
        dot.width = 5;
        dot.height = 5;
        dot.x = ind.x + (ind.width - dot.width) / 2;
        dot.y = ind.y + (ind.height - dot.height) / 2;
        XPainter_fillRect(painter, &dot, textColor);
    }

    /* 文本（indicator 右侧）。 */
    font = XWidget_font((XWidget*)self);
    XPainter_setFont(painter, &font);
    text = XString_toUtf8(ab->m_text ? ab->m_text : NULL);
    textRect.x = ind.x + ind.width + RADIOBUTTON_LABEL_SPACING;
    textRect.y = rect.y;
    textRect.width = rect.width - (textRect.x - rect.x);
    textRect.height = rect.height;
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

/* ==================== 虚槽实现（对标 QRadioButton 保护钩子） ==================== */

/** @brief 重载 hitButton 保护槽：indicator 外接矩形命中。 */
static bool VXRadioButton_hitButton(const XAbstractButton* base,
                                    const XPoint* pos)
{
    const XRadioButton* self = (const XRadioButton*)base;
    XRect ind;

    if (!self || !pos)
        return false;
    ind = radiobutton_indicatorRect(self);
    return XRect_contains(&ind, pos->x, pos->y);
}

/** @brief 重载 ContentChanged 保护槽：文本/图标变化后刷新本类 sizeHint。 */
static void VXRadioButton_contentChanged(XAbstractButton* base)
{
    XRadioButton* self = (XRadioButton*)base;

    if (self)
        radiobutton_refreshSizeHint(self);
}

static void VXRadioButton_paintEvent(XWidget* self, XEvent* event)
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
    XRadioButton_drawContents((XRadioButton*)self, &painter);
    XPainter_end(&painter);
    XPainter_deinit(&painter);
#endif /* XPALETTE_ON && XWINDOWEVENT_ON */
}

/* ==================== 类虚表 ==================== */

XVtable* XRadioButton_class_init(void)
{
    XVTABLE_INIT_DEFAULT(XRadioButton)
    XVTABLE_INHERIT_XCLASS(XAbstractButton);
    XVTABLE_OVERLOAD_DEFAULT(EXAbstractButton_HitButton,
                             VXRadioButton_hitButton);
    XVTABLE_OVERLOAD_DEFAULT(EXAbstractButton_ContentChanged,
                             VXRadioButton_contentChanged);
    XVTABLE_OVERLOAD_DEFAULT(EXWidget_PaintEvent, VXRadioButton_paintEvent);
    /* 登记派生虚表：XAbstractButton 的自动互斥/实例识别依赖该登记表。 */
    XAbstractButton_registerClass(XVTABLE_DEFAULT);
    return XVTABLE_DEFAULT;
}

void XRadioButton_init(XRadioButton* self, XWidget* parent,
                       XWidgetFlags flags)
{
    if (!self) return;
    memset(self, 0, sizeof(XRadioButton));
    XAbstractButton_init(&self->m_base, parent, flags);
    XClassSetVtable(self, XRadioButton);
    /* 对标 Qt 6.8 QRadioButtonPrivate::init：checkable + autoExclusive
       默认开启，前景角色 WindowText。 */
    XAbstractButton_setCheckable((XAbstractButton*)self, true);
    XAbstractButton_setAutoExclusive((XAbstractButton*)self, true);
    XWidget_setForegroundRole((XWidget*)self, XPaletteColorRole_WindowText);
    radiobutton_refreshSizeHint(self);
}

XRadioButton* XRadioButton_create_ex(XMemoryType memory, XWidget* parent,
                                     XWidgetFlags flags)
{
    XRadioButton* self = (XRadioButton*)XMemory_malloc(sizeof(XRadioButton),
                                                       memory);
    if (!self) return NULL;
    memset(self, 0, sizeof(XRadioButton));
    XRadioButton_init(self, parent, flags);
    Set_Class_Memory(self, memory);
    Set_Class_IsHeap(self, true);
    return self;
}

#endif /* XWIDGET_ON && XABSTRACTBUTTON_ON && XRADIOBUTTON_ON */
