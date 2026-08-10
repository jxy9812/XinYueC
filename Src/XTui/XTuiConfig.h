/**
 * @file       XTuiConfig.h
 * @brief      XTui 通用 TUI 模块编译期开关与固定容量配置。
 * @details    本文件由 CXinYueConfig.h 统一包含，也可由产品配置直接包含。
 *             默认开启整个模块；所有容量为编译期常量，运行时达到上限返回
 *             资源错误。本文件不包含平台头文件，不调用平台 API。
 */

#ifndef XTUI_CONFIG_H
#define XTUI_CONFIG_H

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

#endif /* XTUI_CONFIG_H */
