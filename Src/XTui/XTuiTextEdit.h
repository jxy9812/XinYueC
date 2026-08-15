/**
 * @file       XTuiTextEdit.h
 * @brief      XTui 单行文本编辑控件公开 API。
 * @details    继承 XTuiWidget，重载 Render 和 KeyPress。支持普通字符、UTF-8、
 *             退格、Delete、方向键、Home/End。可选密码模式（显示掩码字符）。
 *             用于演示通用 TUI 控件如何扩展虚函数表。
 */

#ifndef XTUI_TEXTEDIT_H
#define XTUI_TEXTEDIT_H

#include "XTuiConfig.h"

#ifdef __cplusplus
extern "C" {
#endif

#if XTUI_ON && XTUI_WIDGET_ON

#include "XTuiWidget.h"
#include "XClass.h"
#include "XMemory.h"
#include <stdbool.h>

/**
 * @brief XTuiTextEdit 类虚函数表枚举（仅继承 XTuiWidget，不新增虚函数）。
 */
XCLASS_DEFINE_BEGING(XTuiTextEdit)
XCLASS_DEFINE_EXTEND_END(XTuiTextEdit, XTuiWidget)

/**
 * @brief 单行文本编辑控件对象。
 * @details m_text 为动态分配 UTF-8 缓冲区；m_cursor 是字节偏移，始终指向
 *          UTF-8 字符边界。m_password 为 true 时渲染为 '*'。
 */
typedef struct XTuiTextEdit
{
    XTuiWidget m_widget;    /**< 基类控件，第一个成员，由 XTuiWidget 管理。 */
    char*      m_text;      /**< UTF-8 文本缓冲；动态分配，由本对象释放。 */
    size_t     m_length;    /**< 当前文本字节数。 */
    size_t     m_capacity;  /**< 缓冲容量（含结尾 NUL）。 */
    int        m_cursor;    /**< 光标字节偏移。 */
    bool       m_password;  /**< 密码模式开关。 */
} XTuiTextEdit;

/** @brief 初始化 XTuiTextEdit 类虚函数表。 */
XVtable* XTuiTextEdit_class_init(void);

/** @brief 在栈上初始化控件对象。 */
void XTuiTextEdit_init(XTuiTextEdit* edit);

/** @brief 在堆上创建控件对象。 */
XTuiTextEdit* XTuiTextEdit_create_ex(XMemoryType memory);

#define XTuiTextEdit_delete_base XClass_delete_base /**< 释放堆对象。 */
#define XTuiTextEdit_deinit_base XClass_deinit_base /**< 反初始化栈对象。 */
#define XTuiTextEdit_copy_base   XClass_copy_base   /**< 拷贝。 */
#define XTuiTextEdit_move_base   XClass_move_base   /**< 移动。 */

/** @brief 设置文本（深拷贝 UTF-8）。 */
void XTuiTextEdit_setText(XTuiTextEdit* edit, const char* text);

/** @brief 获取文本（借用指针）。 */
const char* XTuiTextEdit_text(const XTuiTextEdit* edit);

/** @brief 清空文本并复位光标。 */
void XTuiTextEdit_clear(XTuiTextEdit* edit);

/** @brief 设置密码模式。 */
void XTuiTextEdit_setPassword(XTuiTextEdit* edit, bool password);

/** @brief 查询密码模式。 */
bool XTuiTextEdit_isPassword(const XTuiTextEdit* edit);

/** @brief 获取当前文本字节长度。 */
size_t XTuiTextEdit_length(const XTuiTextEdit* edit);

/** @brief 获取光标字节偏移。 */
int XTuiTextEdit_cursor(const XTuiTextEdit* edit);

/** @brief 在光标处插入一段 UTF-8 文本，光标移动到插入内容之后。 */
bool XTuiTextEdit_insertUtf8(XTuiTextEdit* edit, const char* utf8, int length);

/** @brief 删除光标前一个 UTF-8 字符；返回是否删除了字符。 */
bool XTuiTextEdit_backspace(XTuiTextEdit* edit);

/** @brief 删除光标处的一个 UTF-8 字符；返回是否删除了字符。 */
bool XTuiTextEdit_deleteChar(XTuiTextEdit* edit);

#endif /* XTUI_ON && XTUI_WIDGET_ON */

#ifdef __cplusplus
}
#endif

/* XClass create API default-memory wrappers. */
#undef XTuiTextEdit_create
#define XTuiTextEdit_create(...) XTuiTextEdit_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, ##__VA_ARGS__)

#endif /* XTUI_TEXTEDIT_H */
