/**
 * @file       XTuiConfig.h
 * @brief      XTui 通用 TUI 模块编译期开关与固定容量配置。
 * @details    本文件由 CXinYueConfig.h 统一包含，也可由产品配置直接包含。
 *             默认开启整个模块；所有容量为编译期常量，运行时达到上限返回
 *             资源错误。本文件不包含平台头文件，不调用平台 API。
 */

#ifndef XTUI_CONFIG_H
#define XTUI_CONFIG_H

/* XRegularExpression_ON 等依赖开关由容器配置统一提供。 */
#include "XContainerConfig.h"

/* ==================================================================== */
/* 1. 总开关与核心模块                                                       */
/* ==================================================================== */

/** @brief XTui 总开关；置 0 时裁剪整个 TUI 公共 API 和所有子功能。 */
#ifndef XTUI_ON
#define XTUI_ON 1
#endif

/** @brief 屏幕缓冲模块开关；关闭后 XTuiScreen 相关 API 不编译。 */
#ifndef XTUI_SCREEN_ON
#define XTUI_SCREEN_ON 1
#endif

/** @brief ANSI 终端控制模块开关；关闭后 XTuiTerminal 相关 API 不编译。 */
#ifndef XTUI_TERMINAL_ON
#define XTUI_TERMINAL_ON 1
#endif

/** @brief 控件基类模块开关；关闭后 XTuiWidget 相关 API 不编译。 */
#ifndef XTUI_WIDGET_ON
#define XTUI_WIDGET_ON 1
#endif

/** @brief 会话模块开关；关闭后 XTui 主会话与输入解析不编译。 */
#ifndef XTUI_SESSION_ON
#define XTUI_SESSION_ON 1
#endif

/* ==================================================================== */
/* 2. 固定容量配置                                                          */
/* ==================================================================== */

/** @brief 屏幕最大列数；用于滚动缓冲与越界检查，可被编译期覆盖。 */
#ifndef XTUI_SCREEN_MAX_COLUMNS
#define XTUI_SCREEN_MAX_COLUMNS 256
#endif

/** @brief 屏幕最大行数；用于滚动缓冲与越界检查，可被编译期覆盖。 */
#ifndef XTUI_SCREEN_MAX_ROWS
#define XTUI_SCREEN_MAX_ROWS 64
#endif

/** @brief 单个屏幕单元格最多保存的 UTF-8 字节数（不含结尾 NUL）。 */
#ifndef XTUI_CELL_UTF8_MAX
#define XTUI_CELL_UTF8_MAX 4
#endif

/** @brief 输入解析器接收单个 ESC 序列的最大字节数。 */
#ifndef XTUI_INPUT_BUFFER_SIZE
#define XTUI_INPUT_BUFFER_SIZE 64
#endif

/** @brief 文本编辑控件单行最大字符数（按 UTF-8 字节计）。 */
#ifndef XTUI_TEXTEDIT_MAX_BYTES
#define XTUI_TEXTEDIT_MAX_BYTES 256
#endif

/** @brief 启用 XTuiVim 全屏编辑器控件。 */
#ifndef XTUI_VIM_ON
#define XTUI_VIM_ON 1
#endif

/** @brief vim 单行最大字节数（含结尾 NUL 的容量上限）。 */
#ifndef XTUI_VIM_LINE_MAX
#define XTUI_VIM_LINE_MAX 256
#endif

/** @brief vim 冒号命令缓冲最大长度。 */
#ifndef XTUI_VIM_CMD_MAX
#define XTUI_VIM_CMD_MAX 64
#endif

/** @brief vim 状态栏文本缓冲长度。 */
#ifndef XTUI_VIM_STATUS_MAX
#define XTUI_VIM_STATUS_MAX 128
#endif

/* ==================================================================== */
/* 3. XTuiVim 子功能开关与容量                                              */
/* ==================================================================== */

/** @brief 启用 vim 多缓冲和 :edit/:bnext/:buffers 命令；关闭后仅保留当前文件。 */
#ifndef XTUI_VIM_MULTIBUFFER_ON
#define XTUI_VIM_MULTIBUFFER_ON 1
#endif

/** @brief XTui vim 最多保留的打开缓冲数；仅多缓冲开启时生效。 */
#ifndef XTUI_VIM_MAX_BUFFERS
#define XTUI_VIM_MAX_BUFFERS 16
#endif

/** @brief 启用 vim 扩展 Ex 命令；基础 :w/:q/:wq/:x/:q! 始终保留。 */
#ifndef XTUI_VIM_EX_ON
#define XTUI_VIM_EX_ON 1
#endif

/** @brief 启用 : 命令和 /? 搜索的上下方向键历史。 */
#ifndef XTUI_VIM_HISTORY_ON
#define XTUI_VIM_HISTORY_ON 1
#endif

/** @brief 每类 vim 输入历史最多保留的条目数。 */
#ifndef XTUI_VIM_HISTORY_MAX
#define XTUI_VIM_HISTORY_MAX 16
#endif

/** @brief 启用 :s、:%s 等带行范围的替换命令（依赖扩展 Ex）。 */
#ifndef XTUI_VIM_SUBSTITUTE_ON
#define XTUI_VIM_SUBSTITUTE_ON 1
#endif

/** @brief 启用 :substitute 的逐匹配确认（c flag）。 */
#ifndef XTUI_VIM_SUBSTITUTE_CONFIRM_ON
#define XTUI_VIM_SUBSTITUTE_CONFIRM_ON 1
#endif

/** @brief 启用 /、?、n/N、*、# 正则搜索与匹配高亮。 */
#ifndef XTUI_VIM_SEARCH_ON
#define XTUI_VIM_SEARCH_ON 1
#endif

/** @brief 启用 y、p/P 及删除操作使用的无名寄存器。 */
#ifndef XTUI_VIM_YANK_PASTE_ON
#define XTUI_VIM_YANK_PASTE_ON 1
#endif

/** @brief 启用命名、编号和黑洞寄存器（依赖 yank/paste）。 */
#ifndef XTUI_VIM_REGISTER_ON
#define XTUI_VIM_REGISTER_ON 1
#endif

/** @brief 启用多级 u 撤销和 Ctrl-R 重做；关闭后编辑操作不保存快照。 */
#ifndef XTUI_VIM_UNDO_REDO_ON
#define XTUI_VIM_UNDO_REDO_ON 1
#endif

/** @brief 启用 R、r、s、S、C 等替换和 change 快捷操作。 */
#ifndef XTUI_VIM_REPLACE_ON
#define XTUI_VIM_REPLACE_ON 1
#endif

/** @brief 启用 q/@ 宏录制和回放。 */
#ifndef XTUI_VIM_MACRO_ON
#define XTUI_VIM_MACRO_ON 1
#endif

/** @brief 启用 m{mark} 与 '{mark} 标记跳转。 */
#ifndef XTUI_VIM_MARK_ON
#define XTUI_VIM_MARK_ON 1
#endif

/** @brief 启用跳转列表及 Ctrl-O/Ctrl-I 导航。 */
#ifndef XTUI_VIM_JUMPLIST_ON
#define XTUI_VIM_JUMPLIST_ON 1
#endif

/** @brief vim 跳转列表最多保存的位置数；仅跳转列表开启时生效。 */
#ifndef XTUI_VIM_JUMPLIST_MAX
#define XTUI_VIM_JUMPLIST_MAX 32
#endif

/** @brief 启用可视字符、行和块模式。 */
#ifndef XTUI_VIM_VISUAL_ON
#define XTUI_VIM_VISUAL_ON 1
#endif

/** @brief 启用扩展动作、文本对象、操作符计数和重复命令。 */
#ifndef XTUI_VIM_ADVANCED_MOTION_ON
#define XTUI_VIM_ADVANCED_MOTION_ON 1
#endif

/* 模块依赖检查。 */
#if XTUI_WIDGET_ON && !XTUI_SCREEN_ON
#error "XTui: XTUI_WIDGET_ON 依赖 XTUI_SCREEN_ON"
#endif
#if XTUI_SESSION_ON && !XTUI_TERMINAL_ON
#error "XTui: XTUI_SESSION_ON 依赖 XTUI_TERMINAL_ON"
#endif
#if XTUI_SESSION_ON && !XTUI_WIDGET_ON
#error "XTui: XTUI_SESSION_ON 依赖 XTUI_WIDGET_ON"
#endif

/* 配置值合法性检查。 */
#if (XTUI_SCREEN_MAX_COLUMNS < 1) || (XTUI_SCREEN_MAX_ROWS < 1)
#error "XTui: 屏幕容量必须大于等于 1"
#endif
#if (XTUI_CELL_UTF8_MAX < 1) || (XTUI_CELL_UTF8_MAX > 8)
#error "XTui: 单元格 UTF-8 长度必须在 1 到 8 之间"
#endif

/* XTuiVim 子功能依赖检查。 */
#if !XTUI_VIM_EX_ON
#undef XTUI_VIM_SUBSTITUTE_ON
#define XTUI_VIM_SUBSTITUTE_ON 0
#endif

#if !XTUI_VIM_SUBSTITUTE_ON || !XRegularExpression_ON
#undef XTUI_VIM_SUBSTITUTE_CONFIRM_ON
#define XTUI_VIM_SUBSTITUTE_CONFIRM_ON 0
#endif

#if !XTUI_VIM_YANK_PASTE_ON
#undef XTUI_VIM_REGISTER_ON
#define XTUI_VIM_REGISTER_ON 0
#endif

#if !XTUI_VIM_MULTIBUFFER_ON
#undef XTUI_VIM_MAX_BUFFERS
#define XTUI_VIM_MAX_BUFFERS 1
#endif

#if XTUI_VIM_MULTIBUFFER_ON && XTUI_VIM_MAX_BUFFERS < 1
#error "XTui: VIM_MAX_BUFFERS must be positive"
#endif
#if XTUI_VIM_JUMPLIST_ON && XTUI_VIM_JUMPLIST_MAX < 1
#error "XTui: VIM_JUMPLIST_MAX must be positive"
#endif
#if XTUI_VIM_HISTORY_ON && XTUI_VIM_HISTORY_MAX < 1
#error "XTui: VIM_HISTORY_MAX must be positive"
#endif

#endif /* XTUI_CONFIG_H */
