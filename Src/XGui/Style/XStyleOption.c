#include "XStyleOption.h"
#include "XMemory.h"

#include "XAlgorithm.h"
#if XSTYLE_ON

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
}

void XStyleOptionComplex_init(XStyleOptionComplex* option, int type)
{
    if (!option) return;
    XStyleOption_init(&option->m_base, type);
    option->m_subControls = XStyleSC_All;
    option->m_activeSubControls = XStyleSC_None;
}

void XStyleOptionButton_init(XStyleOptionButton* option, int type)
{
    if (!option) return;
    XStyleOption_init(&option->m_base, type);
    option->m_features = 0;
    option->m_icon = NULL;
    XSize_init(&option->m_iconSize, -1, -1);
}

void XStyleOptionProgressBar_init(XStyleOptionProgressBar* option, int type)
{
    if (!option) return;
    XStyleOption_init(&option->m_base, type);
    option->m_orientation = 0; /* Horizontal。 */
    option->m_invertedAppearance = false;
    option->m_base.m_progressMin = 0;
    option->m_base.m_progressMax = 100;
    option->m_base.m_progressValue = -1;
    option->m_base.m_progressTextVisible = false;
}

void XStyleOptionSlider_init(XStyleOptionSlider* option, int type)
{
    if (!option) return;
    XStyleOption_init(&option->m_base, type);
    option->m_orientation = 0; /* Horizontal。 */
    option->m_subControls = XStyleSC_All;
    option->m_activeSubControls = XStyleSC_None;
    option->m_upsideDown = false;
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
    option->m_base.m_base.m_spinFrame = true;
    option->m_base.m_base.m_spinStepEnabled = 0;
    option->m_base.m_base.m_spinSymbols = 0; /* UpDownArrows。 */
    option->m_base.m_base.m_spinActiveUp = false;
    option->m_base.m_base.m_spinActiveDown = false;
}

void XStyleOptionComboBox_init(XStyleOptionComboBox* option, int type)
{
    if (!option) return;
    XStyleOptionComplex_init(&option->m_base, type);
    option->m_editable = false;
    option->m_popupOpen = false;
    option->m_currentIcon = NULL;
    XSize_init(&option->m_iconSize, -1, -1);
    option->m_base.m_base.m_spinFrame = true; /* frame 复用。 */
}

void XStyleOptionToolButton_init(XStyleOptionToolButton* option, int type)
{
    if (!option) return;
    XStyleOptionComplex_init(&option->m_base, type);
    option->m_features = 0;
    option->m_toolButtonStyle = 0; /* ToolButtonIconOnly。 */
    option->m_arrowType = 2;       /* DownArrow。 */
    option->m_defaultButton = false;
    option->m_icon = NULL;
    XSize_init(&option->m_iconSize, 0, 0);
}

#endif /* XSTYLE_ON */
