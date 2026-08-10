/**
 * @file       XTuiVim.h
 * @brief      XTui 全屏 vim 风格编辑器控件公开 API。
 * @details    继承 XTuiWidget，重载渲染和按键处理。支持命令模式、插入模式、
 *             冒号命令（:w/:q/:wq/:x/:q!）、方向键移动、行删除/插入等，
 *             行为对齐 Linux vim 的常用编辑操作。本控件只维护内存中的文本
 *             缓冲，文件读写由调用方通过 XTuiVim 的缓冲访问接口完成。
 */

#ifndef XTUI_VIM_H
#define XTUI_VIM_H

#include "XTuiConfig.h"

#ifdef __cplusplus
extern "C" {
#endif

#if XTUI_ON && XTUI_WIDGET_ON && XTUI_VIM_ON

#include "XTuiWidget.h"
#include "XClass.h"
#include "XMemory.h"
#include <stdbool.h>
#include <stddef.h>

/**
 * @brief XTuiVim 类虚函数表枚举（仅继承 XTuiWidget，不新增虚函数）。
 */
XCLASS_DEFINE_BEGING(XTuiVim)
XCLASS_DEFINE_EXTEND_END(XTuiVim, XTuiWidget)

/**
 * @brief 全屏 vim 编辑器控件对象。
 * @details m_lines 为动态行数组，每行是 NUL 结尾的 UTF-8 字符串；
 *          m_cursorLine/m_cursorColumn 分别表示光标行（0 起）和光标所在
 *          显示列（按 UTF-8 字符数计）。m_wantSave/m_wantQuit 等标志由
 *          控件在冒号命令执行后设置，调用方在处理完保存/退出后调用
 *          XTuiVim_ackAction 清除。
 */
typedef struct XTuiVim
{
    XTuiWidget m_widget;        /**< 基类控件，第一个成员，由 XTuiWidget 管理。 */
    char**     m_lines;         /**< 文本行数组；动态分配，由本对象释放。 */
    int        m_lineCount;     /**< 有效行数。 */
    int        m_linesCapacity; /**< 行数组容量。 */
    int        m_cursorLine;    /**< 光标行（0 起）。 */
    int        m_cursorColumn;  /**< 光标列（UTF-8 字符数）。 */
    int        m_topLine;       /**< 垂直滚动偏移（首个可见行的行号）。 */
    bool       m_insertMode;    /**< 是否处于插入模式。 */
    bool       m_commandMode;   /**< 是否正在输入冒号命令（: 后内容）。 */
    bool       m_modified;      /**< 缓冲相对磁盘是否已修改。 */
    char       m_command[XTUI_VIM_CMD_MAX + 1];   /**< 冒号命令缓冲（不含 ':'）。 */
    int        m_commandLen;    /**< 冒号命令缓冲字节长度。 */
    char       m_insertBuf[XTUI_VIM_LINE_MAX + 1];/**< 插入模式当前待回车行缓冲。 */
    int        m_insertLen;     /**< 插入缓冲字节长度。 */
    int        m_insertCursor;  /**< 插入缓冲光标列（UTF-8 字符数）。 */
    char       m_status[XTUI_VIM_STATUS_MAX + 1]; /**< 状态栏文本。 */
    char*      m_path;          /**< 目标文件路径；动态分配，由本对象释放。 */
    char**     m_undoLines;     /**< 撤销快照行数组；动态分配。 */
    int        m_undoCount;     /**< 撤销快照有效行数。 */
    int        m_undoCapacity;  /**< 撤销快照行数组容量。 */
    bool       m_wantSave;      /**< 请求保存（:w）。 */
    bool       m_wantQuit;      /**< 请求退出（:q/:q!）。 */
    bool       m_wantSaveQuit;  /**< 请求保存并退出（:wq/:x）。 */
    char       m_pendingNormal;  /**< 命令模式待组合键（如 dd 的第一个 d）。 */
} XTuiVim;

/** @brief 初始化 XTuiVim 类虚函数表。 */
XVtable* XTuiVim_class_init(void);

/** @brief 在栈上初始化控件对象。 */
void XTuiVim_init(XTuiVim* vim);

/** @brief 在堆上创建控件对象。 */
XTuiVim* XTuiVim_create(void);

#define XTuiVim_delete_base XClass_delete_base /**< 释放堆对象。 */
#define XTuiVim_deinit_base XClass_deinit_base /**< 反初始化栈对象。 */
#define XTuiVim_copy_base   XClass_copy_base   /**< 拷贝。 */
#define XTuiVim_move_base   XClass_move_base   /**< 移动。 */

/* ==================== 缓冲访问 ==================== */

/**
 * @brief 用已有行数组替换编辑器缓冲（深拷贝）。
 * @param vim 目标编辑器。
 * @param lines 行数组；每行是 NUL 结尾 UTF-8 字符串。
 * @param count 行数；大于等于 0。
 */
void XTuiVim_setLines(XTuiVim* vim, const char* const* lines, int count);

/** @brief 获取当前行数。 */
int XTuiVim_lineCount(const XTuiVim* vim);

/** @brief 获取指定行的文本（借用指针）；越界返回空串。 */
const char* XTuiVim_line(const XTuiVim* vim, int index);

/** @brief 设置目标文件路径（深拷贝）。 */
void XTuiVim_setPath(XTuiVim* vim, const char* path);

/** @brief 获取目标文件路径（借用指针）；未设置返回空串。 */
const char* XTuiVim_path(const XTuiVim* vim);

/** @brief 查询缓冲是否已修改。 */
bool XTuiVim_isModified(const XTuiVim* vim);

/** @brief 保存成功后清除修改标记。 */
void XTuiVim_clearModified(XTuiVim* vim);

/* ==================== 动作标志 ==================== */

/** @brief 查询是否请求保存（:w）。 */
bool XTuiVim_wantSave(const XTuiVim* vim);

/** @brief 查询是否请求退出（:q/:q!）。 */
bool XTuiVim_wantQuit(const XTuiVim* vim);

/** @brief 查询是否请求保存并退出（:wq/:x）。 */
bool XTuiVim_wantSaveQuit(const XTuiVim* vim);

/** @brief 调用方处理完保存/退出后清除全部动作标志。 */
void XTuiVim_ackAction(XTuiVim* vim);

/** @brief 设置插入模式。 */
void XTuiVim_setInsertMode(XTuiVim* vim, bool insert);

/** @brief 查询是否为插入模式。 */
bool XTuiVim_isInsertMode(const XTuiVim* vim);

#endif /* XTUI_ON && XTUI_WIDGET_ON && XTUI_VIM_ON */

#ifdef __cplusplus
}
#endif
#endif /* XTUI_VIM_H */
