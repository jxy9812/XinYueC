#include "XStyleOption.h"
#include <string.h>

#if XSTYLE_ON

void XStyleOption_init(XStyleOption* option, int type)
{
    if (!option) return;
    memset(option, 0, sizeof(*option));
    option->m_version = 1;
    option->m_type = type;
    option->m_checkState = 0;
    option->m_progressMin = 0;
    option->m_progressMax = 100;
    option->m_progressValue = 0;
    option->m_sliderMin = 0;
    option->m_sliderMax = 100;
    option->m_sliderSingleStep = 1;
    option->m_sliderPageStep = 10;
}

#endif /* XSTYLE_ON */
