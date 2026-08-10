/**
 * @file       XTuiTypes.c
 * @brief      XTui 基础类型实现。
 * @details    几何类型纯函数由 XGui 模块提供（XGuiTypes.c），颜色类型复用
 *             XColor，按键事件继承 XEvent；本文件实现 TUI 键盘事件与颜色
 *             调色板工具函数。
 */

#include "XTuiTypes.h"

#if XTUI_ON

#include <string.h>

/* ==================== XTuiKeyEvent ==================== */

XVtable* XTuiKeyEvent_class_init(void)
{
    XVTABLE_INIT_DEFAULT(XTuiKeyEvent)
    XVTABLE_INHERIT_XCLASS(XEvent);
    XCLASS_SHOW_SIZE_DEFAULT(XTuiKeyEvent);
    return XVTABLE_DEFAULT;
}

XTuiKeyEvent* XTuiKeyEvent_create(XEventType type, XTuiKeyType key, XKeyboardModifiers modifiers)
{
    XTuiKeyEvent* event = (XTuiKeyEvent*)XMalloc_System(sizeof(XTuiKeyEvent));
    if (!event)
        return NULL;
    XTuiKeyEvent_init(event, type, key, modifiers);
    Set_Class_MemoryFree(event, XFree_System);
    return event;
}

void XTuiKeyEvent_init(XTuiKeyEvent* event, XEventType type, XTuiKeyType key, XKeyboardModifiers modifiers)
{
    if (!event)
        return;
    XEvent_init((XEvent*)event, type);
    XClassGetVtable(event) = XTuiKeyEvent_class_init();
    event->m_class.input_event = true;
    event->m_keyType = key;
    event->m_modifiers = modifiers;
    event->m_utf8[0] = '\0';
    event->m_code = 0;
}

XTuiKeyType XTuiKeyEvent_keyType(const XTuiKeyEvent* event)
{
    return event ? event->m_keyType : XTuiKey_None;
}

XKeyboardModifiers XTuiKeyEvent_modifiers(const XTuiKeyEvent* event)
{
    return event ? event->m_modifiers : (XKeyboardModifiers)XKeyboardModifier_NoModifier;
}

/* ==================== 颜色调色板 ==================== */

/* 16 色调色板，与 ANSI 颜色编号 0-15 对应；无效 XColor 表示终端默认色。
 * 分量使用 0-65535 的 XColor 内部表示。 */
static const XColor g_xtuiPalette[XTUI_COLOR_COUNT] = {
    { XColor_Rgb, 65535, 0,      0,      0,      0      }, /* 0  Black */
    { XColor_Rgb, 65535, 65535,  0,      0,      0      }, /* 1  Red */
    { XColor_Rgb, 65535, 0,      65535,  0,      0      }, /* 2  Green */
    { XColor_Rgb, 65535, 65535,  65535,  0,      0      }, /* 3  Yellow */
    { XColor_Rgb, 65535, 0,      0,      65535,  0      }, /* 4  Blue */
    { XColor_Rgb, 65535, 65535,  0,      65535,  0      }, /* 5  Magenta */
    { XColor_Rgb, 65535, 0,      65535,  65535,  0      }, /* 6  Cyan */
    { XColor_Rgb, 65535, 65535,  65535,  65535,  0      }, /* 7  White */
    { XColor_Rgb, 65535, 32896,  32896,  32896,  0      }, /* 8  BrightBlack/Gray */
    { XColor_Rgb, 65535, 65535,  21845,  21845,  0      }, /* 9  BrightRed */
    { XColor_Rgb, 65535, 21845,  65535,  21845,  0      }, /* 10 BrightGreen */
    { XColor_Rgb, 65535, 65535,  65535,  21845,  0      }, /* 11 BrightYellow */
    { XColor_Rgb, 65535, 21845,  21845,  65535,  0      }, /* 12 BrightBlue */
    { XColor_Rgb, 65535, 65535,  21845,  65535,  0      }, /* 13 BrightMagenta */
    { XColor_Rgb, 65535, 21845,  65535,  65535,  0      }, /* 14 BrightCyan */
    { XColor_Rgb, 65535, 49344,  49344,  49344,  0      }  /* 15 BrightWhite/LightGray */
};

uint8_t XTuiColor_toIndex(const XColor* color)
{
    if (!color || color->m_spec == XColor_Invalid)
        return XTUI_COLOR_DEFAULT_INDEX;
    for (uint8_t i = 0; i < XTUI_COLOR_COUNT; ++i) {
        if (XColor_equals(color, &g_xtuiPalette[i]))
            return i;
    }
    return XTUI_COLOR_DEFAULT_INDEX;
}

XColor XTuiColor_fromIndex(uint8_t index)
{
    if (index >= XTUI_COLOR_COUNT)
        return XColor_create();
    return g_xtuiPalette[index];
}

#endif /* XTUI_ON */
