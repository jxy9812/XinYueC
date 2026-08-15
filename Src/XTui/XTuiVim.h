/**
 * @file       XTuiVim.h
 * @brief      XTui 全屏 vim 风格编辑器控件公开 API。
 * @details    继承 XTuiWidget，重载渲染和按键处理。支持命令模式、插入模式、
 *             替换模式、可视模式、搜索模式；冒号命令（:w/:q/:wq/:x/:q!、
 *             :s/regex/replacement/[g]、:%s/...、:set nu/nonu、:nohlsearch）；词移动
 *             w/e/b/W/E/B、跳转 gg/G、字符查找 f/F/t/T/;/,、行操作 dd/cc/yy、
 *             复合操作 dw/cw/y$ 等、复制粘贴 y/p/P、撤销/重做 u/Ctrl-R、
 *             重复上次操作 .，行为对齐 Linux vim 的常用编辑操作。本控件只
 *             维护内存中的文本缓冲，文件读写由调用方通过 XTuiVim 的缓冲
 *             访问接口完成。
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
 * @brief 单个文本缓冲快照（撤销/重做栈元素）。
 * @details lines 为行数组的所有权指针，count 为有效行数，capacity 为容量；
 *          新增/恢复快照时整体转移所有权，不逐行深拷贝，避免大文件开销。
 */
typedef struct XVimSnapshot
{
    char** lines;     /**< 行数组；所有权由快照持有。 */
    int    count;     /**< 有效行数。 */
    int    capacity;  /**< 行数组容量。 */
} XVimSnapshot;

/** @brief vim 寄存器内容。 */
typedef struct XVimRegister
{
    char** lines;     /**< 寄存器文本行；动态分配。 */
    int    count;     /**< 有效文本行数。 */
    bool   lineWise;  /**< 是否为行方式寄存器。 */
} XVimRegister;

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
    bool       m_replaceMode;   /**< 是否处于替换模式（R）。 */
    bool       m_commandMode;   /**< 是否正在输入冒号命令（: 后内容）。 */
#if XTUI_VIM_SEARCH_ON
    bool       m_searchMode;    /**< 是否正在输入搜索（/ 或 ? 后内容）。 */
    bool       m_searchBackward;/**< 当前搜索是否为反向（?）。 */
    bool       m_searchHighlight;/**< 是否显示搜索匹配高亮（:set hlsearch）。 */
#endif
#if XTUI_VIM_SUBSTITUTE_CONFIRM_ON
    bool       m_substituteConfirm; /**< 是否处于 :s///c 逐项确认状态。 */
    bool       m_substituteGlobal;  /**< 确认替换是否处理同一行的全部匹配。 */
    bool       m_substituteIgnoreCase; /**< 确认替换是否忽略大小写。 */
    int        m_substituteFirstLine; /**< 确认替换范围首行。 */
    int        m_substituteLastLine;  /**< 确认替换范围末行。 */
    int        m_substituteLine;      /**< 当前确认候选所在行。 */
    int        m_substituteColumn;    /**< 当前确认候选起始列。 */
    int        m_substituteEndColumn; /**< 当前确认候选结束列。 */
    int        m_substituteNextColumn;/**< 当前行下次匹配的起始列。 */
    char       m_substitutePattern[XTUI_VIM_CMD_MAX + 1]; /**< 确认替换模式。 */
    char       m_substituteReplacement[XTUI_VIM_CMD_MAX + 1]; /**< 确认替换文本。 */
#endif
    bool       m_modified;      /**< 缓冲相对磁盘是否已修改。 */
    char       m_command[XTUI_VIM_CMD_MAX + 1];   /**< 冒号命令缓冲（不含 ':'）。 */
    int        m_commandLen;    /**< 冒号命令缓冲字节长度。 */
#if XTUI_VIM_HISTORY_ON
    char       m_commandHistory[XTUI_VIM_HISTORY_MAX][XTUI_VIM_CMD_MAX + 1]; /**< 冒号命令历史。 */
    int        m_commandHistoryLen; /**< 冒号命令历史有效条目数。 */
    int        m_commandHistoryIndex; /**< 冒号命令当前浏览位置。 */
#endif
#if XTUI_VIM_SEARCH_ON
    char       m_search[XTUI_VIM_CMD_MAX + 1];    /**< 搜索缓冲（不含 '/' 或 '?'）。 */
    int        m_searchLen;     /**< 搜索缓冲字节长度。 */
    char       m_lastSearch[XTUI_VIM_CMD_MAX + 1];/**< 上次执行的搜索串。 */
    int        m_lastSearchLen; /**< 上次搜索串长度。 */
#if XTUI_VIM_HISTORY_ON
    char       m_searchHistory[XTUI_VIM_HISTORY_MAX][XTUI_VIM_CMD_MAX + 1]; /**< 搜索历史。 */
    int        m_searchHistoryLen; /**< 搜索历史有效条目数。 */
    int        m_searchHistoryIndex; /**< 搜索历史当前浏览位置。 */
#endif
#endif
    char       m_insertBuf[XTUI_VIM_LINE_MAX + 1];/**< 插入模式当前待回车行缓冲。 */
    int        m_insertLen;     /**< 插入缓冲字节长度。 */
    int        m_insertCursor;  /**< 插入缓冲光标列（UTF-8 字符数）。 */
    char       m_status[XTUI_VIM_STATUS_MAX + 1]; /**< 状态栏文本。 */
    char*      m_path;          /**< 目标文件路径；动态分配，由本对象释放。 */
#if XTUI_VIM_UNDO_REDO_ON
    XVimSnapshot* m_undoStack;  /**< 撤销快照栈；动态分配。 */
    int        m_undoLen;       /**< 撤销栈有效快照数。 */
    int        m_undoCap;       /**< 撤销栈容量。 */
    XVimSnapshot* m_redoStack;  /**< 重做快照栈；动态分配。 */
    int        m_redoLen;       /**< 重做栈有效快照数。 */
    int        m_redoCap;       /**< 重做栈容量。 */
#endif
#if XTUI_VIM_YANK_PASTE_ON
    char**     m_regLines;      /**< 无名寄存器文本行；动态分配。 */
    int        m_regCount;      /**< 寄存器有效行数。 */
    bool       m_regLineWise;   /**< 寄存器内容是否为行方式（粘贴在行间）。 */
#endif
#if XTUI_VIM_REGISTER_ON
    XVimRegister m_namedRegisters[26]; /**< a-z 命名寄存器。 */
    XVimRegister m_numberedRegisters[10]; /**< 0-9 编号寄存器。 */
    char       m_activeRegister; /**< 下一条操作指定的寄存器；0 为无名。 */
    bool       m_appendRegister; /**< 大写寄存器名表示追加写入。 */
#endif
#if XTUI_VIM_MACRO_ON
    char       m_macros[26][XTUI_VIM_CMD_MAX + 1]; /**< a-z 宏内容。 */
    int        m_macroLengths[26]; /**< 宏内容字节长度。 */
    bool       m_macroRecording; /**< 是否正在录制宏。 */
    bool       m_macroPlaying; /**< 是否正在回放宏。 */
    char       m_macroRegister; /**< 当前录制寄存器。 */
    char       m_lastMacro; /**< `@@` 使用的寄存器。 */
#endif
    bool       m_wantSave;      /**< 请求保存（:w）。 */
    bool       m_wantQuit;      /**< 请求退出（:q/:q!）。 */
    bool       m_wantSaveQuit;  /**< 请求保存并退出（:wq/:x）。 */
#if XTUI_VIM_MULTIBUFFER_ON
    bool       m_wantEdit;      /**< 请求打开另一个缓冲（:edit/:e）。 */
    bool       m_wantBufferNext;/**< 请求切换至下一缓冲（:bnext/:bn）。 */
    bool       m_wantBufferPrev;/**< 请求切换至上一缓冲（:bprevious/:bp）。 */
    bool       m_wantBufferList;/**< 请求列出当前缓冲（:buffers/:ls）。 */
    int        m_wantBufferIndex; /**< 请求切换的 1 起始缓冲号；0 表示未指定。 */
    bool       m_wantForce;     /**< 当前 edit 请求是否带 !。 */
    bool       m_wantWritePath; /**< 请求按 actionPath 写出当前缓冲（:w path）。 */
    bool       m_wantSaveAs;    /**< 请求写出并将当前缓冲路径改为 actionPath（:saveas）。 */
    bool       m_wantWriteAll;  /**< 请求写出所有已修改缓冲（:wa/:wall）。 */
    bool       m_wantQuitAll;   /**< 请求关闭全部缓冲（:qa/:qall）。 */
    bool       m_wantBufferClose; /**< 请求关闭当前缓冲（:bd）。 */
    char       m_actionPath[XTUI_VIM_LINE_MAX + 1]; /**< :edit 的目标路径。 */
#endif
    char       m_pendingOperator; /**< 命令模式待组合操作符（'d'/'c'/'y' 或 '\0'）。 */
    char       m_pendingNormal;   /**< 命令模式待组合键（dd 的第二个 d、f/t 的等待字符）。 */
    int        m_count;         /**< 普通模式计数前缀（0 表示无）。 */
    int        m_opStartLine;   /**< 操作符起始行（d/c/y 的起点）。 */
    int        m_opStartColumn; /**< 操作符起始列。 */
#if XTUI_VIM_VISUAL_ON
    bool       m_visualMode;    /**< 是否处于可视模式。 */
    bool       m_visualLineWise;/**< 可视模式是否为行方式（V）。 */
    bool       m_visualBlock;   /**< 可视模式是否为块方式（Ctrl-V）。 */
    int        m_visualStartLine;   /**< 可视起始行。 */
    int        m_visualStartColumn; /**< 可视起始列。 */
    bool       m_blockInsertMode; /**< 可视块 I/A/c 结束后是否向所有选中行插入。 */
    int        m_blockInsertFirstLine; /**< 块插入首行。 */
    int        m_blockInsertLastLine; /**< 块插入末行。 */
    int        m_blockInsertColumn; /**< 块插入原始列。 */
#endif
#if XTUI_VIM_MARK_ON
    int        m_markLines[26]; /**< a-z 本地标记行；-1 表示未设置。 */
    int        m_markColumns[26]; /**< a-z 本地标记列；-1 表示未设置。 */
#endif
#if XTUI_VIM_JUMPLIST_ON
    int        m_jumpLines[XTUI_VIM_JUMPLIST_MAX]; /**< 跳转列表行。 */
    int        m_jumpColumns[XTUI_VIM_JUMPLIST_MAX]; /**< 跳转列表列。 */
    int        m_jumpLength;    /**< 跳转列表有效位置数。 */
    int        m_jumpIndex;     /**< 当前浏览位置；等于长度表示最新位置。 */
#endif
#if XTUI_VIM_ADVANCED_MOTION_ON
    char       m_findChar;      /**< 上次 f/F/t/T 查找的字符。 */
    bool       m_findBackward;  /**< 上次查找是否为反向（F/T）。 */
    bool       m_findTill;      /**< 上次查找是否为止于字符前（t/T）。 */
    char       m_lastRepeat[XTUI_VIM_CMD_MAX + 1]; /**< 上次可重复操作的编码。 */
    int        m_lastRepeatLen; /**< 上次可重复操作长度。 */
    char       m_insertRepeatPrefix[8]; /**< 插入前置操作编码（如 s:/cw:）。 */
#endif
    bool       m_showLineNumbers; /**< 是否显示行号（:set nu/nonu）。 */
} XTuiVim;

/** @brief 初始化 XTuiVim 类虚函数表。 */
XVtable* XTuiVim_class_init(void);

/** @brief 在栈上初始化控件对象。 */
void XTuiVim_init(XTuiVim* vim);

/** @brief 在堆上创建控件对象。 */
XTuiVim* XTuiVim_create_ex(XMemoryType memory);

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

/** @brief 查询是否请求由宿主打开指定文件（:edit/:e）。 */
bool XTuiVim_wantEdit(const XTuiVim* vim);

/** @brief 获取当前宿主动作的目标路径；未指定时返回空串。 */
const char* XTuiVim_actionPath(const XTuiVim* vim);

/** @brief 查询是否请求切换到下一缓冲（:bnext/:bn）。 */
bool XTuiVim_wantBufferNext(const XTuiVim* vim);

/** @brief 查询是否请求切换到上一缓冲（:bprevious/:bp）。 */
bool XTuiVim_wantBufferPrev(const XTuiVim* vim);

/** @brief 查询是否请求列出缓冲（:buffers/:ls）。 */
bool XTuiVim_wantBufferList(const XTuiVim* vim);

/** @brief 获取请求的 1 起始缓冲号；0 表示未指定。 */
int XTuiVim_wantBufferIndex(const XTuiVim* vim);

/** @brief 查询当前宿主动作是否带强制标记 !。 */
bool XTuiVim_wantForce(const XTuiVim* vim);

/** @brief 查询是否请求按 actionPath 写出当前缓冲（:w path）。 */
bool XTuiVim_wantWritePath(const XTuiVim* vim);
/** @brief 查询是否请求保存并改名当前缓冲（:saveas）。 */
bool XTuiVim_wantSaveAs(const XTuiVim* vim);
/** @brief 查询是否请求写出所有缓冲（:wa/:wall）。 */
bool XTuiVim_wantWriteAll(const XTuiVim* vim);
/** @brief 查询是否请求关闭全部缓冲（:qa/:qall）。 */
bool XTuiVim_wantQuitAll(const XTuiVim* vim);
/** @brief 查询是否请求关闭当前缓冲（:bd）。 */
bool XTuiVim_wantBufferClose(const XTuiVim* vim);

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

/* XClass create API default-memory wrappers. */
#undef XTuiVim_create
#define XTuiVim_create(...) XTuiVim_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, ##__VA_ARGS__)

#endif /* XTUI_VIM_H */
