/**
 * @file       XTuiTerminal.h
 * @brief      XTui ANSI 终端控制类公开 API。
 * @details    只继承 XClass。终端类不直接读写文件描述符，而是通过调用方注册
 *             的写回调输出 ANSI 转义序列，因此可以接入 SSH、Telnet、串口或
 *             本地控制台。所有方法都是同步调用，不占用事件循环。
 */

#ifndef XTUI_TERMINAL_H
#define XTUI_TERMINAL_H

#include "XTuiConfig.h"

#ifdef __cplusplus
extern "C" {
#endif

#if XTUI_ON && XTUI_TERMINAL_ON

#include "XTuiTypes.h"
#include "XClass.h"
#include "XMemory.h"
#include <stdbool.h>
#include <stddef.h>

/** @brief 终端写回调；返回 false 表示设备已断开或写入失败。 */
typedef bool (*XTuiTerminalWriteFunc)(void* userData, const char* data, size_t length);

/**
 * @brief XTuiTerminal 类虚函数表枚举（仅继承 XClass，不新增虚函数）。
 */
XCLASS_DEFINE_BEGING(XTuiTerminal)
XCLASS_DEFINE_EXTEND_END(XTuiTerminal, XClass)

/**
 * @brief ANSI 终端控制对象。
 * @details m_write/m_userData 组合指定输出目标；调用方保证回调在对象存续期
 *          内有效。
 */
typedef struct XTuiTerminal
{
    XClass               m_class;   /**< 基类，第一个成员，由 XClass 管理。 */
    XTuiTerminalWriteFunc m_write;  /**< 输出回调；可以为 NULL。 */
    void*                m_userData;/**< 回调上下文，借用指针，不由对象释放。 */
    int                  m_width;   /**< 终端宽度（列数），由调用方设置。 */
    int                  m_height;  /**< 终端高度（行数），由调用方设置。 */
    bool                 m_hasColor;/**< 是否启用颜色输出；默认开启。 */
} XTuiTerminal;

/** @brief 初始化 XTuiTerminal 类虚函数表。 */
XVtable* XTuiTerminal_class_init(void);

/** @brief 在栈上初始化终端对象。 */
void XTuiTerminal_init(XTuiTerminal* terminal);

/** @brief 在堆上创建终端对象。 */
XTuiTerminal* XTuiTerminal_create_ex(XMemoryType memory);

#define XTuiTerminal_delete_base XClass_delete_base /**< 释放堆对象。 */
#define XTuiTerminal_deinit_base XClass_deinit_base /**< 反初始化栈对象。 */
#define XTuiTerminal_copy_base   XClass_copy_base   /**< 拷贝。 */
#define XTuiTerminal_move_base   XClass_move_base   /**< 移动。 */

/**
 * @brief 注册输出回调。
 * @param terminal 目标终端。
 * @param write 写回调；NULL 表示清除回调。
 * @param userData 回调上下文；借用指针，不由终端释放。
 */
void XTuiTerminal_setWriteCallback(XTuiTerminal* terminal,
                                   XTuiTerminalWriteFunc write,
                                   void* userData);

/** @brief 设置终端尺寸。 */
void XTuiTerminal_setSize(XTuiTerminal* terminal, int width, int height);

/** @brief 获取终端宽度（列数）。 */
int XTuiTerminal_width(const XTuiTerminal* terminal);

/** @brief 获取终端高度（行数）。 */
int XTuiTerminal_height(const XTuiTerminal* terminal);

/** @brief 设置是否输出 ANSI 转义颜色序列。 */
void XTuiTerminal_setColorEnabled(XTuiTerminal* terminal, bool enabled);

/** @brief 查询是否输出颜色序列。 */
bool XTuiTerminal_colorEnabled(const XTuiTerminal* terminal);

/**
 * @brief 直接写入一段字节。
 * @param terminal 目标终端。
 * @param data 数据；NULL 不执行。
 * @param length 字节数；小于 0 时按字符串长度。
 * @return true 表示回调存在且返回成功。
 */
bool XTuiTerminal_write(XTuiTerminal* terminal, const char* data, int length);

/** @brief 清屏并复位光标到左上角。 */
void XTuiTerminal_clearScreen(XTuiTerminal* terminal);

/** @brief 清除从当前光标到行尾的内容。 */
void XTuiTerminal_clearLine(XTuiTerminal* terminal);

/** @brief 把光标移动到指定位置（1 基坐标会被转换为 0 基写入）。 */
void XTuiTerminal_moveTo(XTuiTerminal* terminal, int x, int y);

/** @brief 光标向上移动 n 行。 */
void XTuiTerminal_moveUp(XTuiTerminal* terminal, int n);

/** @brief 光标向下移动 n 行。 */
void XTuiTerminal_moveDown(XTuiTerminal* terminal, int n);

/** @brief 光标向右移动 n 列。 */
void XTuiTerminal_moveRight(XTuiTerminal* terminal, int n);

/** @brief 光标向左移动 n 列。 */
void XTuiTerminal_moveLeft(XTuiTerminal* terminal, int n);

/** @brief 保存光标位置。 */
void XTuiTerminal_saveCursor(XTuiTerminal* terminal);

/** @brief 恢复光标位置。 */
void XTuiTerminal_restoreCursor(XTuiTerminal* terminal);

/** @brief 显示光标。 */
void XTuiTerminal_showCursor(XTuiTerminal* terminal);

/** @brief 隐藏光标。 */
void XTuiTerminal_hideCursor(XTuiTerminal* terminal);

/** @brief 设置前景色；无效颜色（XTUI_COLOR_DEFAULT）表示终端默认色。 */
void XTuiTerminal_setForeground(XTuiTerminal* terminal, XColor color);

/** @brief 设置背景色；无效颜色（XTUI_COLOR_DEFAULT）表示终端默认色。 */
void XTuiTerminal_setBackground(XTuiTerminal* terminal, XColor color);

/** @brief 设置文本属性（可组合）。 */
void XTuiTerminal_setAttributes(XTuiTerminal* terminal, int attrs);

/** @brief 重置前景/背景/属性为终端默认值。 */
void XTuiTerminal_resetAttributes(XTuiTerminal* terminal);

/** @brief 进入备用屏幕（全屏应用）。 */
void XTuiTerminal_enterAlternateScreen(XTuiTerminal* terminal);

/** @brief 退出备用屏幕。 */
void XTuiTerminal_leaveAlternateScreen(XTuiTerminal* terminal);

#endif /* XTUI_ON && XTUI_TERMINAL_ON */

#ifdef __cplusplus
}
#endif

/* XClass create API default-memory wrappers. */
#undef XTuiTerminal_create
#define XTuiTerminal_create(...) XTuiTerminal_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, ##__VA_ARGS__)

#endif /* XTUI_TERMINAL_H */
