#include "XWindowsStyle.h"

#include "XAlgorithm.h"
#include "XMemory.h"
#include "XClass.h"
#include "XPalette.h"
#include "XWidget.h"

#if XSTYLE_ON

static void VXWindowsStyle_deinit(XWindowsStyle* self);
static int VXWindowsStyle_styleHint(XStyle* self, int hint,
                                    const XStyleOption* option,
                                    const XWidget* widget);
static int VXWindowsStyle_pixelMetric(XStyle* self, int pm,
                                      const XStyleOption* option);

XVtable* XWindowsStyle_class_init(void)
{
    XVTABLE_INIT_DEFAULT(XWindowsStyle)
    XVTABLE_INHERIT_XCLASS(XCommonStyle);
    XVTABLE_OVERLOAD_DEFAULT(EXStyle_StyleHint, VXWindowsStyle_styleHint);
    XVTABLE_OVERLOAD_DEFAULT(EXStyle_PixelMetric, VXWindowsStyle_pixelMetric);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Deinit, VXWindowsStyle_deinit);
    return XVTABLE_DEFAULT;
}

void XWindowsStyle_init(XWindowsStyle* self)
{
    if (!self) return;
    XMemset(self, 0, sizeof(*self));
    XCommonStyle_init(&self->m_base);
    XClassSetVtable(self, XWindowsStyle);
}

XWindowsStyle* XWindowsStyle_create_ex(XMemoryType memory)
{
    XWindowsStyle* self = (XWindowsStyle*)XMemory_malloc(sizeof(*self), memory);
    if (!self) return NULL;
    XWindowsStyle_init(self);
    Set_Class_Memory(self, memory);
    Set_Class_IsHeap(self, true);
    return self;
}

static void VXWindowsStyle_deinit(XWindowsStyle* self)
{
    if (!self) return;
    XClass_Deinit_Parent(XCommonStyle, (XCommonStyle*)self);
}

int XWindowsStyle_styleHint(XWindowsStyle* self, int hint,
                            const XStyleOption* option,
                            const XWidget* widget)
{
    return VXWindowsStyle_styleHint((XStyle*)self, hint, option, widget);
}

/** @brief 样式提示（对标 QWindowsStyle::styleHint 中不依赖平台主题的
 *         子集；其余回落 XCommonStyle）。 */
static int VXWindowsStyle_styleHint(XStyle* self, int hint,
                                    const XStyleOption* option,
                                    const XWidget* widget)
{
    switch (hint) {
    case XStyleSH_EtchDisabledText: {
        /* pal.window().lightness() > pal.text().lightness()。 */
        uint32_t win;
        uint32_t text;
        if (option) {
            XColor cw = XPalette_color((XPalette*)&option->m_palette,
                                       XPaletteColorGroup_Current,
                                       XPaletteColorRole_Window);
            XColor ct = XPalette_color((XPalette*)&option->m_palette,
                                       XPaletteColorGroup_Current,
                                       XPaletteColorRole_Text);
            win = XColor_rgba(&cw);
            text = XColor_rgba(&ct);
        } else if (widget) {
            /* 无 option 时使用标准调色板近似。 */
            XPalette pal = XStyle_standardPalette(self);
            XColor cw = XPalette_color(&pal, XPaletteColorGroup_Current,
                                       XPaletteColorRole_Window);
            XColor ct = XPalette_color(&pal, XPaletteColorGroup_Current,
                                       XPaletteColorRole_Text);
            win = XColor_rgba(&cw);
            text = XColor_rgba(&ct);
        } else {
            return 0;
        }
        return (((win >> 16) & 0xFF) + ((win >> 8) & 0xFF) + (win & 0xFF)) / 3
               > (((text >> 16) & 0xFF) + ((text >> 8) & 0xFF) +
                  (text & 0xFF)) / 3
            ? 1 : 0;
    }
    case XStyleSH_Slider_SnapToValue:
    case XStyleSH_PrintDialog_RightAlignButtons:
    case XStyleSH_FontDialog_SelectAssociatedText:
    case XStyleSH_Menu_AllowActiveAndDisabled:
    case XStyleSH_MenuBar_AltKeyNavigation:
    case XStyleSH_MenuBar_MouseTracking:
    case XStyleSH_Menu_MouseTracking:
    case XStyleSH_ComboBox_ListMouseTracking:
    case XStyleSH_Slider_StopMouseOverSlider:
    case XStyleSH_MainWindow_SpaceBelowMenuBar:
    case XStyleSH_ItemView_ChangeHighlightOnFocus:
    case XStyleSH_ItemView_ArrowKeysNavigateIntoChildren:
        return 1;
    case XStyleSH_ToolBox_SelectedPageTitleBold:
    case XStyleSH_DialogButtonBox_ButtonsHaveIcons:
        return 0;
    case XStyleSH_Menu_SubMenuSloppyCloseTimeout:
    case XStyleSH_Menu_SubMenuPopupDelay:
        return 400;
    default:
        return XClass_Parent(XCommonStyle, EXStyle_StyleHint,
                             int(*)(XStyle*, int, const XStyleOption*,
                                    const XWidget*))(
            (XStyle*)self, hint, option, widget);
    }
}

/** @brief 像素度量（对标 QWindowsStyle::pixelMetric：SplitterWidth=4；
 *         其余回落 XCommonStyle）。 */
static int VXWindowsStyle_pixelMetric(XStyle* self, int pm,
                                      const XStyleOption* option)
{
    switch (pm) {
    case XStylePM_SplitterWidth:
        return 4;
    default:
        return XClass_Parent(XCommonStyle, EXStyle_PixelMetric,
                             int(*)(XStyle*, int, const XStyleOption*))(
            (XStyle*)self, pm, option);
    }
}

#endif /* XSTYLE_ON */
