/******************************************************************************
 * @file       XCommandLinkButton.c
 * @brief      XCommandLinkButton 命令链接按钮实现（对标 Qt 6.8 QCommandLinkButton，继承 XPushButton）。
 * @details    实现要点：
 *             - 继承 XPushButton（对齐 Qt QCommandLinkButton : QPushButton），
 *               QAbstractButton/QPushButton 的公共状态/激活/自动重复/自动互斥/
 *               输入事件/信号与 autoDefault/default/flat/menu 全部由基类提供；
 *             - 本类只实现 QCommandLinkButton 特有部分：
 *               - description 描述文本属性（拥有 XString，copy/move/deinit
 *                 经 XClass_Parent 先调 XPushButton 基类槽位再处理本字段）；
 *               - 构造默认图标尺寸 20x20（对标 qcommandlinkbutton.cpp:189）、
 *                 尺寸策略 Preferred/Preferred + PushButton（对标 :185）；
 *               - drawContents 绘制 Window 背景 + 左侧大图标 + 标题/描述
 *                 双行文本（双行块垂直居中，描述使用次要角色色）+
 *                 右侧大号右向箭头（对标 SP_CommandLink→SP_ArrowRight）；
 *               - 重载 ContentChanged 刷新双行 sizeHint；
 *             - 不依赖任何平台 API；嵌入式由 XCOMMANDLINKBUTTON_ON 裁剪，
 *               且依赖 XPUSHBUTTON_ON。
 * @note       近似边界：heightForWidth（描述按宽度换行计算高度）未实现，
 *             按固定双行高度返回 sizeHint；SP_CommandLink 标准图标未以
 *             图标对象接入（drawContents 以 14x14 实心右向三角直接绘制）。
 * @author     XinYueC 团队
 ******************************************************************************/
#include "XCommandLinkButton.h"

#include "XAlgorithm.h"
#include "XStyle.h"
#include "XStyleOption.h"
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

#if XWIDGET_ON && XABSTRACTBUTTON_ON && XPUSHBUTTON_ON && XCOMMANDLINKBUTTON_ON

/* ==================== 内部工具 ==================== */

/** @brief 左侧图标与文本间距。 */
#define COMMANDLINK_ICON_SPACING 6
/** @brief 内容左右边距。 */
#define COMMANDLINK_MARGIN 8
/** @brief 标题与描述行间距。 */
#define COMMANDLINK_LINE_SPACING 2
/** @brief 右侧箭头预留宽度。 */
#define COMMANDLINK_ARROW_WIDTH 14

/** @brief 从控件调色板读取颜色；调色板裁剪时回退黑色。 */
static uint32_t commandlink_color(const XCommandLinkButton* self,
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

/** @brief 返回用于尺寸/绘制的图标渲染尺寸（显式未设置时按 20x20）。 */
static XSize commandlink_iconSize(const XCommandLinkButton* self)
{
    const XAbstractButton* ab = (const XAbstractButton*)self;
    XSize out;

    XSize_init(&out, 20, 20);
    if (ab && XSize_isValid(&ab->m_iconSize) &&
        ab->m_iconSize.width > 0 && ab->m_iconSize.height > 0) {
        out = ab->m_iconSize;
    }
    return out;
}

/** @brief 按标题+描述双行与左侧图标计算建议尺寸。 */
static XSize commandlink_computeSizeHint(const XCommandLinkButton* self)
{
    const XAbstractButton* ab = (const XAbstractButton*)self;
    XSize out;
    XSize iconSize;
    XFont font;
    const char* title;
    const char* desc;
    int lineHeight;
    int titleWidth;
    int descWidth;
    int w;
    int h;

    if (!self) { XSize_init(&out, -1, -1); return out; }
    iconSize = commandlink_iconSize(self);
    font = XWidget_font((XWidget*)self);
    title = ab->m_text ? XString_toUtf8(ab->m_text) : NULL;
    if (!title) title = "";
    desc = self->m_description ? XString_toUtf8(self->m_description) : NULL;
    if (!desc) desc = "";
    lineHeight = XPainter_textHeight(&font);
    titleWidth = XPainter_textWidth(&font, title[0] ? title : "XXXX");
    descWidth = XPainter_textWidth(&font, desc);
    XFont_deinit_base(&font);

    w = COMMANDLINK_MARGIN * 2 + iconSize.width + COMMANDLINK_ICON_SPACING;
    if (titleWidth > w - COMMANDLINK_MARGIN * 2 - COMMANDLINK_ARROW_WIDTH)
        w = COMMANDLINK_MARGIN * 2 + titleWidth + COMMANDLINK_ARROW_WIDTH;
    if (descWidth > w - COMMANDLINK_MARGIN * 2 - COMMANDLINK_ARROW_WIDTH)
        w = COMMANDLINK_MARGIN * 2 + descWidth + COMMANDLINK_ARROW_WIDTH;

    h = COMMANDLINK_MARGIN + lineHeight;
    if (desc[0] != '\0')
        h += COMMANDLINK_LINE_SPACING + lineHeight;
    h += COMMANDLINK_MARGIN;
    if (h < iconSize.height + COMMANDLINK_MARGIN * 2)
        h = iconSize.height + COMMANDLINK_MARGIN * 2;

    XSize_init(&out, w, h);
    return out;
}

/** @brief 刷新基类 XWidget 的 sizeHint/minimumSizeHint 存储位。 */
static void commandlink_refreshSizeHint(XCommandLinkButton* self)
{
    XSize hint;
    if (!self) return;
    hint = commandlink_computeSizeHint(self);
    XWidget_setSizeHint((XWidget*)self, &hint);
    XWidget_setMinimumSizeHint((XWidget*)self, &hint);
}

/* ==================== 描述文本（对标 QCommandLinkButton description） ==================== */

const XString* XCommandLinkButton_description(const XCommandLinkButton* self)
{
    return self ? self->m_description : NULL;
}

void XCommandLinkButton_setDescription(XCommandLinkButton* self,
                                       const XString* description)
{
    XString* copy;

    if (!self)
        return;
    if (self->m_description && description &&
        XString_equals(self->m_description, description,
                       XChar_CaseSensitive)) {
        return;
    }
    copy = description ? XString_create_copy(description) : XString_create();
    if (!copy)
        return;
    if (self->m_description)
        XString_delete_base((XClass*)self->m_description);
    self->m_description = copy;
    commandlink_refreshSizeHint(self);
    XWidget_updateGeometry((XWidget*)self);
    XWidget_update((XWidget*)self);
}

void XCommandLinkButton_setDescription_2(XCommandLinkButton* self,
                                         const char* utf8)
{
    XString* text;

    if (!self)
        return;
    text = XString_create_utf8(utf8 ? utf8 : "");
    if (!text)
        return;
    XCommandLinkButton_setDescription(self, text);
    XString_delete_base((XClass*)text);
}

/* ==================== 尺寸（对标 QCommandLinkButton sizeHint/minimumSizeHint） ==================== */

XSize XCommandLinkButton_sizeHint(const XCommandLinkButton* self)
{
    return commandlink_computeSizeHint(self);
}

XSize XCommandLinkButton_minimumSizeHint(const XCommandLinkButton* self)
{
    return commandlink_computeSizeHint(self);
}

/* ==================== 绘制（对标 QCommandLinkButton::paintEvent 内容） ==================== */

void XCommandLinkButton_drawContents(XCommandLinkButton* self,
                                     XPainter* painter)
{
    XAbstractButton* ab = (XAbstractButton*)self;
    XRect rect;
    XPaletteColorGroup group;
    uint32_t window, titleColor, descColor, arrowColor;
    XSize iconSize;
    XFont font;
    const char* title;
    const char* desc;
    int lineHeight;
    int textX;
    int titleBaseline;
    int descBaseline;

    if (!self || !painter) return;
    if (!XPainter_save(painter)) return;
    rect = XWidget_rect((XWidget*)self);
    group = XWidget_isEnabled((XWidget*)self) ? XPaletteColorGroup_Current
                                              : XPaletteColorGroup_Disabled;
#if XPALETTE_ON
    window = commandlink_color(self, group, XPaletteColorRole_Window);
    titleColor = commandlink_color(self, group, XPaletteColorRole_ButtonText);
    descColor = commandlink_color(self, group, XPaletteColorRole_PlaceholderText);
    /* 箭头取 ButtonText（对标 Qt 6.8：QCommandLinkButton 默认图标为
     * standardIcon(SP_CommandLink)，QCommonStyle 将其映射为
     * SP_ArrowRight 右向箭头标准图标，以文本类深色渲染）。 */
    arrowColor = commandlink_color(self, group, XPaletteColorRole_ButtonText);
#else
    window = 0xFFCFCFCFu;
    titleColor = 0xFF000000u;
    descColor = 0xFF808080u;
    arrowColor = 0xFF000000u;
#endif /* XPALETTE_ON */
    if (window == 0u)
        window = 0xFFCFCFCFu;
    if (titleColor == 0u)
        titleColor = 0xFF000000u;
    if (descColor == 0u)
        descColor = 0xFF808080u;
    if (arrowColor == 0u)
        arrowColor = 0xFF000000u;

#if XSTYLE_ON
    if (XStyle_defaultStyle() != NULL) {
        /* Fusion/公共风格接管：面板走 PE_PanelButtonCommand（悬停渐变/
         * 按下反转/焦点环），标题/描述/图标保留本控件绘制。 */
        XStyle* style = XStyle_defaultStyle();
        XStyleOption opt;
        XStyleOption_init(&opt, XStylePE_PanelButtonCommand);
        opt.m_rect = rect;
        opt.m_state = XWidget_isEnabled((XWidget*)self)
            ? XStyleState_Enabled | XStyleState_Raised : 0;
        if (ab->m_down || ab->m_checked)
            opt.m_state |= XStyleState_Sunken | XStyleState_On;
        if (XWidget_underMouse((XWidget*)self) &&
            XWidget_isEnabled((XWidget*)self))
            opt.m_state |= XStyleState_MouseOver;
        if (XWidget_hasFocus((XWidget*)self))
            opt.m_state |= XStyleState_HasFocus;
#if XPALETTE_ON
        opt.m_palette = XWidget_palette((XWidget*)self);
#endif
        XStyle_drawPrimitive(style, XStylePE_PanelButtonCommand, &opt,
                             painter, (XWidget*)self);
        if (XWidget_hasFocus((XWidget*)self)) {
            XStyleOption foc = opt;
            XRect fr = rect;
            fr.x += 3;
            fr.y += 3;
            fr.width -= 6;
            fr.height -= 6;
            foc.m_type = XStylePE_FrameFocusRect;
            foc.m_rect = fr;
            XStyle_drawPrimitive(style, XStylePE_FrameFocusRect, &foc,
                                 painter, (XWidget*)self);
        }
        goto xcl_style_label;
    }
#endif /* XSTYLE_ON */
    XPainter_fillRect(painter, &rect, window);

xcl_style_label:
    iconSize = commandlink_iconSize(self);
    font = XWidget_font((XWidget*)self);
    XPainter_setFont(painter, &font);
    title = XString_toUtf8(ab->m_text ? ab->m_text : NULL);
    if (!title) title = "";
    desc = XString_toUtf8(self->m_description ? self->m_description : NULL);
    if (!desc) desc = "";
    lineHeight = XPainter_textHeight(&font);
    textX = rect.x + COMMANDLINK_MARGIN + iconSize.width +
            COMMANDLINK_ICON_SPACING;

    /* 左侧大图标（垂直居中于整体）。 */
    if (!XIcon_isNull(&ab->m_icon)) {
        int iconX = rect.x + COMMANDLINK_MARGIN;
        int iconY = rect.y + (rect.height - iconSize.height) / 2;
        XIcon_paint(&ab->m_icon, painter, iconX, iconY,
                    iconSize.width, iconSize.height, XAlignment_Center,
                    XWidget_isEnabled((XWidget*)self) ? XIconMode_Normal
                                                      : XIconMode_Disabled,
                    ab->m_checked ? XIconState_On : XIconState_Off);
    }

    /* 标题（第一行）+ 描述（第二行）双行块整体垂直居中排布。
     * 结构对标 Qt 6.8 QCommandLinkButtonPrivate::titleRect()/
     * descriptionRect()（标题行在上、描述行紧随，行距
     * COMMANDLINK_LINE_SPACING）；但 Qt 固定 topMargin/bottomMargin=10
     * （qcommandlinkbutton.cpp:81-84），在 320x54 固定几何与本库字体
     * 度量（行高 22px 量级）下描述行会越过按钮下缘被削掉半截，故改为
     * 按双行块实际高度在按钮高度内垂直居中（整块高于按钮的极端情况
     * 退化为 2px 顶边距顶对齐，标题行优先保持可见）。 */
    if (title[0] != '\0' || desc[0] != '\0') {
        int ascent = XPainter_textAscent(&font);
        int blockH = lineHeight;
        int top;
        if (title[0] != '\0' && desc[0] != '\0')
            blockH += COMMANDLINK_LINE_SPACING + lineHeight;
        top = rect.y + (rect.height - blockH) / 2;
        if (top < rect.y + 2)
            top = rect.y + 2;
        if (title[0] != '\0') {
            titleBaseline = top + ascent;
            XPainter_drawText(painter, textX, titleBaseline, title,
                              titleColor);
            if (desc[0] != '\0') {
                descBaseline = titleBaseline + lineHeight +
                               COMMANDLINK_LINE_SPACING;
                XPainter_drawText(painter, textX, descBaseline, desc,
                                  descColor);
            }
        } else {
            descBaseline = top + ascent;
            XPainter_drawText(painter, textX, descBaseline, desc, descColor);
        }
    }

    /* 右侧大号右向箭头（14x14 实心三角，垂直居中）。
     * 对标 Qt 6.8 QCommandLinkButton：构造时以
     * style()->standardIcon(SP_CommandLink) 为默认图标（图标尺寸
     * 20x20），而 QCommonStyle::standardIcon 将 SP_CommandLink 映射为
     * SP_ArrowRight 右向箭头（qcommonstyle.cpp case SP_CommandLink）；
     * 旧实现仅 5x5 小三角，在按钮右缘视觉上只是一个针尖大的"+"状
     * 斑点，与 Qt 的大号箭头图标观感不符。 */
    if (rect.width >= COMMANDLINK_MARGIN * 2 + COMMANDLINK_ARROW_WIDTH) {
        int arrowX = rect.x + rect.width - COMMANDLINK_MARGIN -
                     COMMANDLINK_ARROW_WIDTH;
        int arrowY = rect.y + (rect.height - COMMANDLINK_ARROW_WIDTH) / 2;
        int k;
        for (k = 0; k < COMMANDLINK_ARROW_WIDTH; ++k) {
            int t = (k < COMMANDLINK_ARROW_WIDTH / 2)
                        ? k
                        : (COMMANDLINK_ARROW_WIDTH - 1 - k);
            int rowW = 2 * (t + 1);
            if (rowW > COMMANDLINK_ARROW_WIDTH)
                rowW = COMMANDLINK_ARROW_WIDTH;
            /* 左对齐行：左缘竖直、右缘向中线收尖，成右向三角（▶）。
             * 右对齐会得到镜像的左向三角（◀，方向 bug）。 */
            XPainter_fillRect(painter,
                &(XRect){arrowX, arrowY + k, rowW, 1}, arrowColor);
        }
    }
    XFont_deinit_base(&font);
    XPainter_restore(painter);
}

/* ==================== 虚槽实现（对标 QCommandLinkButton 事件与生命周期） ==================== */

/** @brief 重载 ContentChanged 保护槽：文本/图标变化后刷新本类双行 sizeHint。 */
static void VXCommandLinkButton_contentChanged(XAbstractButton* base)
{
    XCommandLinkButton* self = (XCommandLinkButton*)base;

    if (self)
        commandlink_refreshSizeHint(self);
}

static void VXCommandLinkButton_paintEvent(XWidget* self, XEvent* event)
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
#if XPAINTER_CLIP_ON
    clip = XPaintEvent_rect(pe);
    XPainter_setClipRect(&painter, &clip, XPainterClipOperation_ReplaceClip);
#endif
    XCommandLinkButton_drawContents((XCommandLinkButton*)self, &painter);
    XPainter_end(&painter);
    XPainter_deinit(&painter);
#endif /* XPALETTE_ON && XWINDOWEVENT_ON */
}

static void VXCommandLinkButton_copy(XCommandLinkButton* self,
                                     const XCommandLinkButton* other)
{
    if (!self || !other || self == other) return;
    if (XClassIsVtableNull(self)) XCommandLinkButton_init(self, NULL, 0);
    XClass_Parent(XPushButton, EXClass_Copy,
                  void(*)(XPushButton*, const XPushButton*))(
                      (XPushButton*)self, (const XPushButton*)other);
    if (self->m_description) {
        XString_delete_base((XClass*)self->m_description);
        self->m_description = NULL;
    }
    self->m_description =
        other->m_description ? XString_create_copy(other->m_description)
                             : XString_create();
}

static void VXCommandLinkButton_move(XCommandLinkButton* self,
                                     XCommandLinkButton* other)
{
    if (!self || !other || self == other) return;
    if (XClassIsVtableNull(self)) XCommandLinkButton_init(self, NULL, 0);
    XClass_Parent(XPushButton, EXClass_Move,
                  void(*)(XPushButton*, XPushButton*))(
                      (XPushButton*)self, (XPushButton*)other);
    if (self->m_description) {
        XString_delete_base((XClass*)self->m_description);
        self->m_description = NULL;
    }
    self->m_description = other->m_description;
    other->m_description = XString_create();
}

static void VXCommandLinkButton_deinit(XCommandLinkButton* self)
{
    if (!self) return;
    if (self->m_description) {
        XString_delete_base((XClass*)self->m_description);
        self->m_description = NULL;
    }
    XClass_Deinit_Parent(XPushButton, (XPushButton*)self);
}

/* ==================== 类虚表 ==================== */

XVtable* XCommandLinkButton_class_init(void)
{
    XVTABLE_INIT_DEFAULT(XCommandLinkButton)
    XVTABLE_INHERIT_XCLASS(XPushButton);
    XVTABLE_OVERLOAD_DEFAULT(EXAbstractButton_ContentChanged,
                             VXCommandLinkButton_contentChanged);
    XVTABLE_OVERLOAD_DEFAULT(EXWidget_PaintEvent,
                             VXCommandLinkButton_paintEvent);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Copy, VXCommandLinkButton_copy);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Move, VXCommandLinkButton_move);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Deinit, VXCommandLinkButton_deinit);
    /* 登记派生虚表：XAbstractButton 的自动互斥/实例识别依赖该登记表。 */
    XAbstractButton_registerClass(XVTABLE_DEFAULT);
    return XVTABLE_DEFAULT;
}

void XCommandLinkButton_init(XCommandLinkButton* self, XWidget* parent,
                             XWidgetFlags flags)
{
    XWidgetSizePolicy policy;
    XSize iconSize;

    if (!self) return;
    XMemset(self, 0, sizeof(XCommandLinkButton));
    XPushButton_init(&self->m_base, parent, flags);
    XClassSetVtable(self, XCommandLinkButton);
    self->m_description = XString_create();
    /* 对标 Qt 6.8 QCommandLinkButtonPrivate::init：尺寸策略
       Preferred/Preferred + PushButton，图标尺寸 20x20。 */
    policy = XWidgetSizePolicy_create_ex(XWidgetSizePolicy_Preferred,
                                         XWidgetSizePolicy_Preferred,
                                         XWidgetSizePolicyControl_PushButton);
    XWidget_setSizePolicyFull((XWidget*)self, &policy);
    XSize_init(&iconSize, 20, 20);
    XAbstractButton_setIconSize((XAbstractButton*)self, &iconSize);
    commandlink_refreshSizeHint(self);
}

XCommandLinkButton* XCommandLinkButton_create_ex(XMemoryType memory,
                                                 XWidget* parent,
                                                 XWidgetFlags flags)
{
    XCommandLinkButton* self = (XCommandLinkButton*)XMemory_malloc(
        sizeof(XCommandLinkButton), memory);
    if (!self) return NULL;
    XMemset(self, 0, sizeof(XCommandLinkButton));
    XCommandLinkButton_init(self, parent, flags);
    Set_Class_Memory(self, memory);
    Set_Class_IsHeap(self, true);
    return self;
}

#endif /* XWIDGET_ON && XABSTRACTBUTTON_ON && XPUSHBUTTON_ON && XCOMMANDLINKBUTTON_ON */
