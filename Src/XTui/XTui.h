/**
 * @file       XTui.h
 * @brief      XTui 通用 TUI 会话类公开 API。
 * @details    只继承 XClass。XTui 负责把终端、屏幕、根控件和输入解析串起来：
 *             应用调用 start() 进入全屏模式，回调 feedInput() 灌入原始字节，
 *             refresh() 把根控件渲染到屏幕并绘制到终端。输入解析状态机支持
 *             ESC [ / ESC O / 功能键 / 方向键 / Ctrl 组合以及 UTF-8 字符。
 */

#ifndef XTUI_H
#define XTUI_H

#include "XTuiConfig.h"

#ifdef __cplusplus
extern "C" {
#endif

#if XTUI_ON && XTUI_SESSION_ON

#include "XTuiTypes.h"
#include "XTuiScreen.h"
#include "XTuiTerminal.h"
#include "XTuiWidget.h"
#include "XClass.h"
#include "XMemory.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/** @brief 输入解析器状态。 */
typedef enum XTuiParseState
{
    XTuiParse_Normal = 0, /**< 普通字符状态。 */
    XTuiParse_Esc,        /**< 已收到 ESC。 */
    XTuiParse_Csi,        /**< 已收到 ESC [，正在收集 CSI 参数。 */
    XTuiParse_O           /**< 已收到 ESC O。 */
} XTuiParseState;

/**
 * @brief XTui 会话类虚函数表枚举（仅继承 XClass，不新增虚函数）。
 */
XCLASS_DEFINE_BEGING(XTui)
XCLASS_DEFINE_EXTEND_END(XTui, XClass)

/**
 * @brief TUI 会话对象。
 * @details m_screen/m_terminal/m_root 均为借用指针，由调用方创建和管理；
 *          XTui 不释放它们。m_previousScreen 是内部快照，用于差异绘制。
 */
typedef struct XTui
{
    XClass         m_class;           /**< 基类，第一个成员，由 XClass 管理。 */
    XTuiScreen*    m_screen;          /**< 当前屏幕缓冲；借用指针。 */
    XTuiTerminal*  m_terminal;        /**< 当前终端；借用指针。 */
    XTuiWidget*    m_root;            /**< 根控件；借用指针。 */
    XTuiWidget*    m_focus;           /**< 焦点控件；借用指针。 */
    XTuiScreen*    m_previousScreen;  /**< 上次绘制快照；内部动态分配。 */
    size_t         m_csiLength;       /**< CSI 参数缓冲长度。 */
    size_t         m_utf8Pos;         /**< UTF-8 已累积字节数。 */
    size_t         m_utf8Expected;    /**< UTF-8 期望总字节数。 */
    int64_t        m_escapeStartedMsecs; /**< 单独 ESC 等待开始时间；0 表示无等待。 */
    XTuiParseState m_parseState;      /**< 输入解析状态。 */
    char           m_csiBuffer[XTUI_INPUT_BUFFER_SIZE]; /**< CSI 参数缓冲。 */
    char           m_utf8Buf[XTUI_CELL_UTF8_MAX + 1];   /**< UTF-8 字符累积缓冲。 */
    bool           m_running;         /**< 是否已进入全屏模式。 */
    bool           m_useAlternateScreen; /**< 是否使用备用屏幕；默认 true。 */
    bool           m_lastByteCR;      /**< 上次输入为 CR，用于合并 CRLF 回车。 */
} XTui;

/** @brief 初始化 XTui 类虚函数表。 */
XVtable* XTui_class_init(void);

/** @brief 在栈上初始化 TUI 会话对象。 */
void XTui_init(XTui* tui);

/** @brief 在堆上创建 TUI 会话对象。 */
XTui* XTui_create_ex(XMemoryType memory);

#define XTui_delete_base XClass_delete_base /**< 释放堆对象。 */

/* ==================== 装配 ==================== */

/** @brief 设置屏幕缓冲；借用指针，不由 XTui 释放。 */
void XTui_setScreen(XTui* self, XTuiScreen* screen);

/** @brief 获取当前屏幕缓冲。 */
XTuiScreen* XTui_screen(const XTui* self);

/** @brief 设置终端；借用指针，不由 XTui 释放。 */
void XTui_setTerminal(XTui* self, XTuiTerminal* terminal);

/** @brief 获取当前终端。 */
XTuiTerminal* XTui_terminal(const XTui* self);

/** @brief 设置根控件；借用指针，不由 XTui 释放。 */
void XTui_setRootWidget(XTui* self, XTuiWidget* widget);

/** @brief 获取根控件。 */
XTuiWidget* XTui_rootWidget(const XTui* self);

/** @brief 设置焦点控件；若控件不可用则忽略。 */
void XTui_setFocusWidget(XTui* self, XTuiWidget* widget);

/** @brief 获取焦点控件。 */
XTuiWidget* XTui_focusWidget(const XTui* self);

/** @brief 设置是否使用备用屏幕。 */
void XTui_setUseAlternateScreen(XTui* self, bool enabled);

/* ==================== 生命周期 ==================== */

/**
 * @brief 进入全屏 TUI 模式。
 * @details 若启用备用屏幕则先进入备用屏幕，然后清屏、同步屏幕尺寸并刷新。
 * @return true 表示成功。
 */
bool XTui_start(XTui* self);

/**
 * @brief 退出全屏 TUI 模式。
 * @details 退出备用屏幕并复位终端属性。
 */
void XTui_stop(XTui* self);

/* ==================== 渲染与输入 ==================== */

/**
 * @brief 把根控件渲染到屏幕并绘制到终端。
 * @details 先清屏缓冲，再调用根控件 render，最后差异绘制到终端。
 * @return true 表示绘制完成。
 */
bool XTui_refresh(XTui* self);

/**
 * @brief 使下一次绘制忽略旧屏幕快照。
 * @details 用于终端尺寸变化或外部终端状态被清除后，强制完整重绘当前屏幕。
 * @param self 目标 TUI 会话；可为 NULL。
 */
void XTui_invalidate(XTui* self);

/**
 * @brief 把当前屏幕与上次快照做差异并绘制到终端。
 * @return true 表示完成；无终端或屏幕时返回 false。
 */
bool XTui_paint(XTui* self);

/**
 * @brief 向会话灌入原始输入字节。
 * @details 解析普通字符、UTF-8、ESC 序列并派发给焦点控件。
 * @param self 目标会话。
 * @param data 输入字节。
 * @param length 字节数。
 * @return 已消费字节数。
 */
int XTui_feedInput(XTui* self, const char* data, int length);

/**
 * @brief 提交已等待超时的单独 ESC 按键。
 * @details
 * 输入解析器需要等待极短时间来区分单独 ESC 与方向键的 ESC [ / ESC O
 * 前缀。事件循环在没有新输入时应周期调用本函数；未达到判定窗口时不
 * 产生事件，已超时则派发 Escape 并恢复普通解析状态。
 * @param self 目标会话；可为 NULL。
 * @return 本次是否提交了一个 Escape 按键。
 */
bool XTui_flushPendingInput(XTui* self);

/**
 * @brief 把单个按键事件派发给焦点控件。
 * @param self 目标会话。
 * @param event 按键事件。
 * @return true 表示事件已处理。
 */
bool XTui_handleKey(XTui* self, const XTuiKeyEvent* event);

#endif /* XTUI_ON && XTUI_SESSION_ON */

#ifdef __cplusplus
}
#endif

/* XClass create API default-memory wrappers. */
#undef XTui_create
#define XTui_create() XTui_create_ex(XCLASS_DEFAULT_MEMORY_TYPE)

#endif /* XTUI_H */
