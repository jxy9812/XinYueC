#if XGUI_ON

#include "XStyleOption.h"
#include "XMemory.h"

#include "XAlgorithm.h"
#include "XClass.h"
#include "XFont.h"
#include "XPainter.h"
#include "XWidget.h"
#if XSTYLE_ON

/** @brief 字体度量清零（值结构，无析构义务）。 */
static void XStyleFontMetrics_init(XStyleFontMetrics* metrics)
{
    if (!metrics) return;
    metrics->m_height = 0;
    metrics->m_ascent = 0;
    metrics->m_descent = 0;
    metrics->m_lineSpacing = 0;
    metrics->m_averageCharWidth = 0;
    metrics->m_spaceWidth = 0;
}

void XStyleOption_init(XStyleOption* option, int type)
{
    if (!option) return;
    XMemset(option, 0, sizeof(*option));
    option->m_version = 1;
    option->m_type = type;
    option->m_direction = 0;
    option->m_checkState = 0;
    option->m_progressMin = 0;
    option->m_progressMax = 100;
    option->m_progressValue = -1;
    option->m_sliderMin = 0;
    option->m_sliderMax = 100;
    option->m_sliderSingleStep = 1;
    option->m_sliderPageStep = 10;
    option->m_sliderTickInterval = 0;
    option->m_sliderTickPosition = 0;
    option->m_spinFrame = true;
    option->m_spinSymbols = 0; /* UpDownArrows。 */
    option->m_pageStep = 1;
    /* Qt 的 OptionType 语义（qstyleoption.h:41-52）：基类默认
       SO_Default；派生类构造各自改写（见下方各 *_init）。 */
    option->m_optionType = XStyleOptionType_Default;
    XStyleFontMetrics_init(&option->m_fontMetrics);
}

void XStyleOptionComplex_init(XStyleOptionComplex* option, int type)
{
    if (!option) return;
    XStyleOption_init(&option->m_base, type);
    option->m_base.m_optionType = XStyleOptionType_Complex;
    option->m_subControls = XStyleSC_All;
    option->m_activeSubControls = XStyleSC_None;
}

void XStyleOptionButton_init(XStyleOptionButton* option, int type)
{
    if (!option) return;
    XStyleOption_init(&option->m_base, type);
    option->m_base.m_optionType = XStyleOptionType_Button;
    option->m_features = 0;
    option->m_icon = NULL;
    XSize_init(&option->m_iconSize, -1, -1);
}

void XStyleOptionProgressBar_init(XStyleOptionProgressBar* option, int type)
{
    if (!option) return;
    XStyleOption_init(&option->m_base, type);
    option->m_base.m_optionType = XStyleOptionType_ProgressBar;
    option->m_orientation = 0; /* Horizontal。 */
    option->m_invertedAppearance = false;
    option->m_bottomToTop = false;
    option->m_text = NULL;
    option->m_textAlignment = 0;
    option->m_base.m_progressMin = 0;
    option->m_base.m_progressMax = 100;
    option->m_base.m_progressValue = -1;
    option->m_base.m_progressTextVisible = false;
}

void XStyleOptionSlider_init(XStyleOptionSlider* option, int type)
{
    if (!option) return;
    XStyleOption_init(&option->m_base, type);
    option->m_base.m_optionType = XStyleOptionType_Slider;
    option->m_orientation = 0; /* Horizontal。 */
    option->m_subControls = XStyleSC_All;
    option->m_activeSubControls = XStyleSC_None;
    option->m_upsideDown = false;
    option->m_sliderPosition = 0;
    option->m_notchTarget = 0.0f;
    option->m_keyboardModifiers = 0;
    option->m_base.m_horizontal = true;
    option->m_base.m_sliderMin = 0;
    option->m_base.m_sliderMax = 100;
    option->m_base.m_sliderValue = 0;
    option->m_base.m_sliderSingleStep = 1;
    option->m_base.m_sliderPageStep = 10;
    option->m_base.m_sliderTickInterval = 0;
    option->m_base.m_sliderTickPosition = 0;
    option->m_base.m_notchSize = 0;
}

void XStyleOptionSpinBox_init(XStyleOptionSpinBox* option, int type)
{
    if (!option) return;
    XStyleOptionComplex_init(&option->m_base, type);
    option->m_base.m_base.m_optionType = XStyleOptionType_SpinBox;
    option->m_base.m_base.m_spinFrame = true;
    option->m_base.m_base.m_spinStepEnabled = 0;
    option->m_base.m_base.m_spinSymbols = 0; /* UpDownArrows. */
    option->m_base.m_base.m_spinActiveUp = false;
    option->m_base.m_base.m_spinActiveDown = false;
}

void XStyleOptionComboBox_init(XStyleOptionComboBox* option, int type)
{
    if (!option) return;
    XStyleOptionComplex_init(&option->m_base, type);
    option->m_base.m_base.m_optionType = XStyleOptionType_ComboBox;
    option->m_editable = false;
    option->m_popupOpen = false;
    option->m_currentIcon = NULL;
    XSize_init(&option->m_iconSize, -1, -1);
    XRect_init(&option->m_popupRect, 0, 0, 0, 0);
    option->m_frame = true;
    option->m_currentText = NULL;
    option->m_textAlignment = 0;
    option->m_base.m_base.m_spinFrame = true; /* frame 复用。 */
}

void XStyleOptionToolButton_init(XStyleOptionToolButton* option, int type)
{
    if (!option) return;
    XStyleOptionComplex_init(&option->m_base, type);
    option->m_base.m_base.m_optionType = XStyleOptionType_ToolButton;
    option->m_features = 0;
    option->m_toolButtonStyle = 0; /* ToolButtonIconOnly. */
    option->m_arrowType = 2;       /* DownArrow. */
    option->m_defaultButton = false;
    option->m_icon = NULL;
    XSize_init(&option->m_iconSize, 0, 0);
    option->m_text = NULL;
    XPoint_init(&option->m_pos, 0, 0);
    option->m_font = NULL;
}

void XStyleOptionTitleBar_init(XStyleOptionTitleBar* option, int type)
{
    if (!option) return;
    /* XStyleOptionComplex_init 内部 XStyleOption_init 全量清零并填
     * version/type；子控件位由组装方（窗口装饰层）按窗口 flags 收窄，
     * 不沿用 SC_All 缺省。 */
    XStyleOptionComplex_init(&option->m_base, type);
    option->m_base.m_base.m_optionType = XStyleOptionType_TitleBar;
    option->m_base.m_subControls = 0;
    option->m_titleBarState = 0;
    option->m_titleBarFlags = 0;
}

/* ==================== QStyleOption 族补齐：14 个新增选项类 ==================== */

/** @brief 派生选项类通用初始化：整体清零 + 基类初始化 + OptionType。 */
static void xso_derivedInit(XStyleOption* base, int type, int optionType)
{
    XStyleOption_init(base, type);
    base->m_optionType = optionType;
}

void XStyleOptionFocusRect_init(XStyleOptionFocusRect* option, int type)
{
    if (!option) return;
    XMemset(option, 0, sizeof(*option));
    xso_derivedInit(&option->m_base, type, XStyleOptionType_FocusRect);
    option->m_backgroundColor = 0;
}

void XStyleOptionFrame_init(XStyleOptionFrame* option, int type)
{
    if (!option) return;
    XMemset(option, 0, sizeof(*option));
    xso_derivedInit(&option->m_base, type, XStyleOptionType_Frame);
    option->m_lineWidth = 0;
    option->m_midLineWidth = 0;
    option->m_features = XStyleOptionFrameFeature_None;
    option->m_frameShape = XStyleOptionFrameShape_NoFrame;
}

void XStyleOptionTabWidgetFrame_init(XStyleOptionTabWidgetFrame* option, int type)
{
    if (!option) return;
    XMemset(option, 0, sizeof(*option));
    xso_derivedInit(&option->m_base, type, XStyleOptionType_TabWidgetFrame);
    option->m_lineWidth = 0;
    option->m_midLineWidth = 0;
    option->m_shape = XStyleOptionTabBarShape_RoundedNorth;
    XSize_init(&option->m_tabBarSize, 0, 0);
    XSize_init(&option->m_rightCornerWidgetSize, 0, 0);
    XSize_init(&option->m_leftCornerWidgetSize, 0, 0);
    XRect_init(&option->m_tabBarRect, 0, 0, 0, 0);
    XRect_init(&option->m_selectedTabRect, 0, 0, 0, 0);
}

void XStyleOptionTabBarBase_init(XStyleOptionTabBarBase* option, int type)
{
    if (!option) return;
    XMemset(option, 0, sizeof(*option));
    xso_derivedInit(&option->m_base, type, XStyleOptionType_TabBarBase);
    option->m_shape = XStyleOptionTabBarShape_RoundedNorth;
    XRect_init(&option->m_tabBarRect, 0, 0, 0, 0);
    XRect_init(&option->m_selectedTabRect, 0, 0, 0, 0);
    option->m_documentMode = false;
}

void XStyleOptionHeader_init(XStyleOptionHeader* option, int type)
{
    if (!option) return;
    XMemset(option, 0, sizeof(*option));
    xso_derivedInit(&option->m_base, type, XStyleOptionType_Header);
    option->m_section = -1;
    option->m_text = NULL;
    option->m_textAlignment = 0;
    option->m_icon = NULL;
    option->m_iconAlignment = 0;
    option->m_position = XStyleOptionHeaderSectionPosition_Beginning;
    option->m_selectedPosition = XStyleOptionHeaderSelectedPosition_NotAdjacent;
    option->m_sortIndicator = XStyleOptionHeaderSortIndicator_None;
    option->m_orientation = 0;
}

void XStyleOptionTab_init(XStyleOptionTab* option, int type)
{
    if (!option) return;
    XMemset(option, 0, sizeof(*option));
    xso_derivedInit(&option->m_base, type, XStyleOptionType_Tab);
    option->m_shape = XStyleOptionTabBarShape_RoundedNorth;
    option->m_text = NULL;
    option->m_icon = NULL;
    option->m_row = 0;
    option->m_position = XStyleOptionTabPosition_Beginning;
    option->m_selectedPosition = XStyleOptionTabSelectedPosition_NotAdjacent;
    option->m_cornerWidgets = XStyleOptionTabCornerWidget_None;
    XSize_init(&option->m_iconSize, -1, -1);
    option->m_documentMode = false;
    XSize_init(&option->m_leftButtonSize, 0, 0);
    XSize_init(&option->m_rightButtonSize, 0, 0);
    option->m_features = XStyleOptionTabFeature_None;
    option->m_tabIndex = -1;
}

void XStyleOptionToolBar_init(XStyleOptionToolBar* option, int type)
{
    if (!option) return;
    XMemset(option, 0, sizeof(*option));
    xso_derivedInit(&option->m_base, type, XStyleOptionType_ToolBar);
    option->m_positionOfLine = XStyleOptionToolBarPosition_Beginning;
    option->m_positionWithinLine = XStyleOptionToolBarPosition_Beginning;
    option->m_toolBarArea = 0;
    option->m_features = XStyleOptionToolBarFeature_None;
    option->m_lineWidth = 0;
    option->m_midLineWidth = 0;
}

void XStyleOptionMenuItem_init(XStyleOptionMenuItem* option, int type)
{
    if (!option) return;
    XMemset(option, 0, sizeof(*option));
    xso_derivedInit(&option->m_base, type, XStyleOptionType_MenuItem);
    option->m_menuItemType = XStyleOptionMenuItemType_Normal;
    option->m_checkType = XStyleOptionMenuItemCheckType_NotCheckable;
    option->m_checked = false;
    option->m_menuHasCheckableItems = false;
    XRect_init(&option->m_menuRect, 0, 0, 0, 0);
    option->m_text = NULL;
    option->m_icon = NULL;
    option->m_maxIconWidth = 16;
    option->m_reservedShortcutWidth = 0;
    option->m_font = NULL;
}

void XStyleOptionDockWidget_init(XStyleOptionDockWidget* option, int type)
{
    if (!option) return;
    XMemset(option, 0, sizeof(*option));
    xso_derivedInit(&option->m_base, type, XStyleOptionType_DockWidget);
    option->m_title = NULL;
    option->m_closable = false;
    option->m_movable = false;
    option->m_floatable = false;
    option->m_verticalTitleBar = false;
}

void XStyleOptionViewItem_init(XStyleOptionViewItem* option, int type)
{
    if (!option) return;
    XMemset(option, 0, sizeof(*option));
    xso_derivedInit(&option->m_base, type, XStyleOptionType_ViewItem);
    option->m_displayAlignment = 0;
    option->m_decorationAlignment = 0;
    option->m_textElideMode = 0;
    option->m_decorationPosition = XStyleOptionViewItemPosition_Left;
    XSize_init(&option->m_decorationSize, 0, 0);
    option->m_font = NULL;
    option->m_showDecorationSelected = false;
    option->m_features = XStyleOptionViewItemFeature_None;
    option->m_widget = NULL;
    option->m_checkState = 0;
    option->m_icon = NULL;
    option->m_text = NULL;
    option->m_viewItemPosition = XStyleOptionViewItemViewItemPosition_Invalid;
    option->m_backgroundColor = 0;
}

void XStyleOptionToolBox_init(XStyleOptionToolBox* option, int type)
{
    if (!option) return;
    XMemset(option, 0, sizeof(*option));
    xso_derivedInit(&option->m_base, type, XStyleOptionType_ToolBox);
    option->m_text = NULL;
    option->m_icon = NULL;
    option->m_position = XStyleOptionToolBoxTabPosition_Beginning;
    option->m_selectedPosition = XStyleOptionToolBoxSelectedPosition_NotAdjacent;
}

void XStyleOptionRubberBand_init(XStyleOptionRubberBand* option, int type)
{
    if (!option) return;
    XMemset(option, 0, sizeof(*option));
    xso_derivedInit(&option->m_base, type, XStyleOptionType_RubberBand);
    option->m_shape = 1; /* XRubberBandShape_Rectangle。 */
    option->m_opaque = false;
}

void XStyleOptionGroupBox_init(XStyleOptionGroupBox* option, int type)
{
    if (!option) return;
    XMemset(option, 0, sizeof(*option));
    XStyleOptionComplex_init(&option->m_base, type);
    option->m_base.m_base.m_optionType = XStyleOptionType_GroupBox;
    option->m_features = XStyleOptionFrameFeature_None;
    option->m_text = NULL;
    option->m_textAlignment = 0;
    option->m_textColor = 0;
    option->m_lineWidth = 0;
    option->m_midLineWidth = 0;
}

void XStyleOptionSizeGrip_init(XStyleOptionSizeGrip* option, int type)
{
    if (!option) return;
    XMemset(option, 0, sizeof(*option));
    XStyleOptionComplex_init(&option->m_base, type);
    option->m_base.m_base.m_optionType = XStyleOptionType_SizeGrip;
    option->m_corner = XStyleOptionCorner_BottomRight;
}

/* ==================== initFrom / 访问器 ==================== */

/** @brief 由字体计算度量（对标 QFontMetrics 的 height/ascent/descent 等）。
 *  @details 度量来源与 XWidget_fontMetrics 文档一致：XPainter_textHeight
 *           /textAscent/textDescent/textWidth。字体副本由调用方持有并
 *           释放（XFont 深拷贝契约）。 */
static void xso_fillFontMetrics(XStyleFontMetrics* metrics,
                                const XFont* font)
{
    if (!metrics) return;
    XStyleFontMetrics_init(metrics);
    if (!font) return;
    metrics->m_height = XPainter_textHeight(font);
    metrics->m_ascent = XPainter_textAscent(font);
    metrics->m_descent = XPainter_textDescent(font);
    metrics->m_lineSpacing = metrics->m_height;
    metrics->m_averageCharWidth = XPainter_textWidth(font, "0");
    metrics->m_spaceWidth = XPainter_textWidth(font, " ");
}

void XStyleOption_initFrom(XStyleOption* option, const XWidget* widget)
{
    XFont font;
    if (!option) return;
    /* Qt 的 initFrom 不重置 minimum/maximum 等——那些由构造器给出；
       X 侧以 XStyleOption_init 承担构造器职责，再按控件覆写六项。 */
    XStyleOption_init(option, 0);
    if (!widget) return;
    option->m_state = XStyleState_None;
    if (XWidget_isEnabled(widget))
        option->m_state |= XStyleState_Enabled;
    if (XWidget_hasFocus(widget))
        option->m_state |= XStyleState_HasFocus;
    if (XWidget_underMouse(widget))
        option->m_state |= XStyleState_MouseOver;
    {
        XWidget* window = XWidget_window(widget);
        if (window && XWidget_isActiveWindow(window))
            option->m_state |= XStyleState_Active;
    }
    if (XWidget_isWindow(widget))
        option->m_state |= XStyleState_Window;
    option->m_direction =
        (XWidget_layoutDirection(widget) == XWidgetLayoutDirection_RightToLeft)
            ? 1 : 0;
    option->m_rect = XWidget_rect(widget);
    option->m_palette = XWidget_palette(widget);
    option->m_styleObject = (XObject*)widget;
    /* 字体度量：XWidget_fontMetrics 返回**拥有型**副本，用完必须释放
       （XFont 深拷贝契约，见 XWidget_fontMetrics 文档）。 */
    font = XWidget_fontMetrics(widget);
    xso_fillFontMetrics(&option->m_fontMetrics, &font);
    XClassDeinit(&font);
}

int XStyleOption_optionType(const XStyleOption* option)
{
    if (!option) return XStyleOptionType_Default;
    return option->m_optionType;
}

void XStyleOption_setOptionType(XStyleOption* option, int optionType)
{
    if (!option) return;
    option->m_optionType = optionType;
}

bool XStyleOption_isComplex(const XStyleOption* option)
{
    if (!option) return false;
    return option->m_optionType == XStyleOptionType_Complex ||
           option->m_optionType > XStyleOptionType_Complex;
}

XStyleFontMetrics XStyleOption_fontMetrics(const XStyleOption* option)
{
    XStyleFontMetrics metrics;
    XStyleFontMetrics_init(&metrics);
    if (option) metrics = option->m_fontMetrics;
    return metrics;
}

void XStyleOption_setFontMetrics(XStyleOption* option,
                                  const XStyleFontMetrics* metrics)
{
    if (!option) return;
    if (metrics) option->m_fontMetrics = *metrics;
    else XStyleFontMetrics_init(&option->m_fontMetrics);
}

#endif /* XSTYLE_ON */

#endif /* XGUI_ON */
