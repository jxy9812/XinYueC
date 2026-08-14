/**
 * @file       XTuiTypes.h
 * @brief      XTui 通用 TUI 模块的基础类型定义。
 * @details    几何类型（XPoint/XSize/XRect）复用 XGeometry.h，颜色类型复用
 *             XData/XColor/XColor.h 的 XColor，按键事件继承 XEvent，避免重复
 *             定义；本文件只包含 TUI 特有的逻辑按键类型与文本属性，不分配内存，
 *             适合嵌入式裁剪。所有坐标使用列(x)、行(y)约定，原点在左上角。
 */

#ifndef XTUI_TYPES_H
#define XTUI_TYPES_H

#include "XTuiConfig.h"
#include "XData/XGeometry.h"
#include "XData/XColor/XColor.h"
#include "XEvent.h"

#ifdef __cplusplus
extern "C" {
#endif

#if XTUI_ON

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

/** @brief 终端调色板颜色数（16 色 ANSI 标准，含 8 个亮色）。 */
#define XTUI_COLOR_COUNT 16

/** @brief 屏幕单元格中表示“终端默认色”的调色板索引。 */
#define XTUI_COLOR_DEFAULT_INDEX 0xFF

/** @brief 终端默认色；等价于无效 XColor。 */
#define XTUI_COLOR_DEFAULT XColor_create()

/** @brief 文本属性位标志，可组合使用。 */
typedef enum XTuiAttribute
{
    XTuiAttribute_None       = 0,      /**< 无特殊属性。 */
    XTuiAttribute_Bold       = 1 << 0, /**< 加粗。 */
    XTuiAttribute_Underline  = 1 << 1, /**< 下划线。 */
    XTuiAttribute_Blink      = 1 << 2, /**< 闪烁。 */
    XTuiAttribute_Reverse    = 1 << 3, /**< 反显。 */
    XTuiAttribute_Hidden     = 1 << 4  /**< 隐藏。 */
} XTuiAttribute;

/** @brief 按键类型。 */
typedef enum XTuiKeyType
{
    XTuiKey_None = 0,       /**< 空事件。 */
    XTuiKey_Char,           /**< 普通可见字符（UTF-8 字节序列）。 */
    XTuiKey_Enter,          /**< 回车。 */
    XTuiKey_Backspace,      /**< 退格。 */
    XTuiKey_Tab,            /**< Tab。 */
    XTuiKey_Escape,         /**< ESC。 */
    XTuiKey_ArrowUp,        /**< 上方向键。 */
    XTuiKey_ArrowDown,      /**< 下方向键。 */
    XTuiKey_ArrowLeft,      /**< 左方向键。 */
    XTuiKey_ArrowRight,     /**< 右方向键。 */
    XTuiKey_Home,           /**< Home。 */
    XTuiKey_End,            /**< End。 */
    XTuiKey_PageUp,         /**< PageUp。 */
    XTuiKey_PageDown,       /**< PageDown。 */
    XTuiKey_Insert,         /**< Insert。 */
    XTuiKey_Delete,         /**< Delete。 */
    XTuiKey_F1,             /**< F1。 */
    XTuiKey_F2,             /**< F2。 */
    XTuiKey_F3,             /**< F3。 */
    XTuiKey_F4,             /**< F4。 */
    XTuiKey_F5,             /**< F5。 */
    XTuiKey_F6,             /**< F6。 */
    XTuiKey_F7,             /**< F7。 */
    XTuiKey_F8,             /**< F8。 */
    XTuiKey_F9,             /**< F9。 */
    XTuiKey_F10,            /**< F10。 */
    XTuiKey_F11,            /**< F11。 */
    XTuiKey_F12             /**< F12。 */
} XTuiKeyType;

/** @brief 携带按键数据的 TUI 键盘事件，继承 XEvent。 */
XCLASS_DEFINE_BEGING(XTuiKeyEvent)
XCLASS_DEFINE_EXTEND_END(XTuiKeyEvent, XEvent)

typedef struct XTuiKeyEvent
{
    XEvent             m_class;    /**< 继承 XEvent。 */
    XTuiKeyType        m_keyType;  /**< TUI 逻辑按键类型。 */
    XKeyboardModifiers m_modifiers;/**< 事件发生时按下的修饰键。 */
    char               m_utf8[XTUI_CELL_UTF8_MAX + 1]; /**< 当 key 为 Char 时的 UTF-8 字符。 */
    int                m_code;     /**< 扩展码；当前保留为 0。 */
} XTuiKeyEvent;

/** @brief 初始化 XTuiKeyEvent 类虚函数表。 */
XVtable* XTuiKeyEvent_class_init(void);

/**
 * @brief 创建 TUI 键盘事件。
 * @param type 事件类型，通常为 XEVENT_TYPE_KEY_PRESS。
 * @param key  TUI 逻辑按键类型。
 * @param modifiers 修饰键组合（XKeyboardModifier_*）。
 * @return 新事件；内存分配失败返回 NULL。
 */
XTuiKeyEvent* XTuiKeyEvent_create(XEventType type, XTuiKeyType key, XKeyboardModifiers modifiers);

/**
 * @brief 初始化调用者提供的 TUI 键盘事件存储。
 * @param event 目标事件存储。
 * @param type 事件类型。
 * @param key  TUI 逻辑按键类型。
 * @param modifiers 修饰键组合。
 */
void XTuiKeyEvent_init(XTuiKeyEvent* event, XEventType type, XTuiKeyType key, XKeyboardModifiers modifiers);

/**
 * @brief 获取 TUI 逻辑按键类型。
 * @param event 目标事件。
 * @return 按键类型；event 为 NULL 时返回 XTuiKey_None。
 */
XTuiKeyType XTuiKeyEvent_keyType(const XTuiKeyEvent* event);

/**
 * @brief 获取修饰键位掩码。
 * @param event 目标事件。
 * @return 修饰键组合；event 为 NULL 时返回 NoModifier。
 */
XKeyboardModifiers XTuiKeyEvent_modifiers(const XTuiKeyEvent* event);

/** @brief 释放堆上创建的 TUI 键盘事件。 */
#define XTuiKeyEvent_delete_base XEvent_delete_base
/** @brief 反初始化栈上创建的 TUI 键盘事件。 */
#define XTuiKeyEvent_deinit_base XEvent_deinit_base

/* ========== 颜色工具 ========== */

/**
 * @brief 把 XColor 映射为终端调色板索引（0-15）。
 * @param color 颜色；NULL 或无效颜色视为默认色。
 * @return 调色板索引；默认色返回 XTUI_COLOR_DEFAULT_INDEX。
 */
uint8_t XTuiColor_toIndex(const XColor* color);

/**
 * @brief 把终端调色板索引转换回 XColor。
 * @param index 调色板索引（0-15）或 XTUI_COLOR_DEFAULT_INDEX。
 * @return 对应的 XColor；默认色返回无效颜色。
 */
XColor XTuiColor_fromIndex(uint8_t index);

#endif /* XTUI_ON */

#ifdef __cplusplus
}
#endif
#endif /* XTUI_TYPES_H */
