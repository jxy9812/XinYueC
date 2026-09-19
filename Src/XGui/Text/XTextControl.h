/**
 * @file       XTextControl.h
 * @brief      XTextControl 私有文本控制器（对标 Qt 6.8 QWidgetTextControl
 *             的 API/功能/行为三层）。
 * @details    QWidgetTextControl 是 Qt 内部承载 QTextEdit/QTextBrowser/
 *             QLabel(富文本) 编辑行为的 QObject 私有控制器：文本/段落模型
 *             与编辑、光标与多选区、撤销/重做、拖选与双击/三击选段、
 *             IME（preedit 生命周期）、命中测试与链接（anchorAt/hitTest/
 *             linkActivated/linkHovered）、drawContents（选区高亮/光标/
 *             IME 下划线）、processEvent 键盘鼠标路由、滚动联动与完整
 *             信号族。本类以 XObject 为基类（对标 QObject 继承），完整
 *             映射其公共 API/槽/信号/保护虚函数面。
 *             内部承载采用平铺行模型：行数组（每行 char* 定容缓冲 +
 *             行长），对标 QTextDocument 的可见行为；位置一律使用文档
 *             绝对 UTF-8 字节偏移（行内容 + 行间 '\n' 各占 1 字节），
 *             对标 QTextCursor 的绝对位置。
 *             与 Qt 的已知差异（平铺行 vs 文档模型的边界）：
 *             - 无富文本格式：QTextCharFormat 以 int 位值承载，无逐字符
 *               格式存储；HTML 走"剥标签 + 提取 <a href> 锚点"的子集；
 *             - 无自动换行：一个 '\n' 段落即一行，QTextLayout 的
 *               wordWrap/行内 wrap 不存在（documentSize 宽度取 textWidth）；
 *             - 无表格/图片/对象（QTextTable/QTextFrame 路径整族裁剪）；
 *             - 撤销为命令差量栈 + 撤销组（beginEditBlock/endEditBlock
 *               + 连续键入合并），对标 QTextDocument 的 edit block 语义；
 *             - preedit 不入文档（与 Qt 一致），以 m_preedit 旁路承载并
 *               参与命中测试/绘制；无逐属性 IME 下划线（整段下划线）。
 * @note       模块总开关 XTEXTCONTROL_ON：默认回退定义于本头文件（待
 *             并入 XGuiConfig.h 统一管理）；实现文件以同一开关裁剪。
 * @author     XinYueC 团队
 */
#ifndef XTEXTCONTROL_H
#define XTEXTCONTROL_H
#ifdef __cplusplus
extern "C" {
#endif

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include "XGuiConfig.h"

#ifndef XTEXTCONTROL_ON
#define XTEXTCONTROL_ON 1
#endif

#if XTEXTCONTROL_ON

#include "XClass.h"
#include "XObject.h"
#include "XGeometry.h"
#include "XEvent.h"
#include "XWindowEvent.h"
#include "XPalette.h"
#include "XFont.h"

/* XVariant 前置声明（loadResource 返回载体；定义见 XData/XVariant）。 */
typedef struct XVariant XVariant;
/* XTextDocument 前置声明（文档镜像承载；定义见 Widget/XTextDocument）。 */
typedef struct XTextDocument XTextDocument;
/* XPainter/XMenu 前置声明。 */
typedef struct XPainter XPainter;
typedef struct XMenu XMenu;

/* ========================================================================== */
/*                    枚举族（数值与 Qt 6.8 逐一对应）                        */
/* ========================================================================== */

/**
 * @brief      文本交互标志（对标 Qt::TextInteractionFlag，数值一致，可位或）。
 */
typedef enum XTextControlInteractionFlag
{
    XTextControlInteraction_NoTextInteraction = 0x00000000, /**< 无交互。 */
    XTextControlInteraction_TextSelectableByMouse = 0x00000001, /**< 鼠标可选。 */
    XTextControlInteraction_TextSelectableByKeyboard = 0x00000002, /**< 键盘可选。 */
    XTextControlInteraction_LinksAccessibleByMouse = 0x00000004, /**< 鼠标可开链接。 */
    XTextControlInteraction_LinksAccessibleByKeyboard = 0x00000008, /**< 键盘可开链接。 */
    XTextControlInteraction_TextEditable = 0x00000010, /**< 可编辑。 */
    /**< 编辑器默认组合（Qt::TextEditorInteraction）。 */
    XTextControlInteraction_TextEditorInteraction =
        XTextControlInteraction_TextSelectableByMouse |
        XTextControlInteraction_TextSelectableByKeyboard |
        XTextControlInteraction_LinksAccessibleByKeyboard,
    /**< 浏览器默认组合（Qt::TextBrowserInteraction）。 */
    XTextControlInteraction_TextBrowserInteraction =
        XTextControlInteraction_TextSelectableByMouse |
        XTextControlInteraction_LinksAccessibleByMouse |
        XTextControlInteraction_LinksAccessibleByKeyboard
} XTextControlInteractionFlag;

/**
 * @brief      光标移动操作（对标 QTextCursor::MoveOperation，数值一致）。
 * @note       平铺模型下 Block 与 Line 同义（一个 '\n' 段落即一行）；
 *             表格单元操作（NextCell/PreviousCell/NextRow/PreviousRow）
 *             保留枚举位但无表格语义，行为退化为 NoMove。
 */
typedef enum XTextControlMoveOperation
{
    XTextControlMove_NoMove = 0,            /**< 不移动。 */
    XTextControlMove_Start = 1,             /**< 文档起始。 */
    XTextControlMove_Up = 2,                /**< 上一行（保持列目标）。 */
    XTextControlMove_StartOfLine = 3,       /**< 行首。 */
    XTextControlMove_StartOfBlock = 4,      /**< 段首（平铺：同 StartOfLine）。 */
    XTextControlMove_StartOfWord = 5,       /**< 词首。 */
    XTextControlMove_PreviousBlock = 6,     /**< 上一段首。 */
    XTextControlMove_PreviousCharacter = 7, /**< 前一码点。 */
    XTextControlMove_PreviousWord = 8,      /**< 前一词。 */
    XTextControlMove_Left = 9,              /**< 左一码点（可跨行）。 */
    XTextControlMove_WordLeft = 10,         /**< 左一词。 */
    XTextControlMove_End = 11,              /**< 文档末尾。 */
    XTextControlMove_Down = 12,             /**< 下一行（保持列目标）。 */
    XTextControlMove_EndOfLine = 13,        /**< 行尾。 */
    XTextControlMove_EndOfWord = 14,        /**< 词尾。 */
    XTextControlMove_EndOfBlock = 15,       /**< 段尾（平铺：同 EndOfLine）。 */
    XTextControlMove_NextBlock = 16,        /**< 下一段首。 */
    XTextControlMove_NextCharacter = 17,    /**< 后一码点。 */
    XTextControlMove_NextWord = 18,         /**< 后一词。 */
    XTextControlMove_Right = 19,            /**< 右一码点（可跨行）。 */
    XTextControlMove_WordRight = 20,        /**< 右一词。 */
    XTextControlMove_NextCell = 21,         /**< 下一表格单元（无表格语义）。 */
    XTextControlMove_PreviousCell = 22,     /**< 上一表格单元（无表格语义）。 */
    XTextControlMove_NextRow = 23,          /**< 下一表格行（无表格语义）。 */
    XTextControlMove_PreviousRow = 24       /**< 上一表格行（无表格语义）。 */
} XTextControlMoveOperation;

/**
 * @brief      光标移动模式（对标 QTextCursor::MoveMode，数值一致）。
 */
typedef enum XTextControlMoveMode
{
    XTextControlMoveMode_MoveAnchor = 0, /**< 移动光标并收起选区。 */
    XTextControlMoveMode_KeepAnchor = 1  /**< 保持锚点扩展选区。 */
} XTextControlMoveMode;

/**
 * @brief      查找标志（对标 QTextDocument::FindFlag，数值一致，可位或）。
 */
typedef enum XTextControlFindFlag
{
    XTextControlFindFlag_FindBackward = 0x00000001,      /**< 向后查找。 */
    XTextControlFindFlag_FindCaseSensitively = 0x00000002, /**< 区分大小写。 */
    XTextControlFindFlag_FindWholeWords = 0x00000004     /**< 全词匹配。 */
} XTextControlFindFlag;

/**
 * @brief      命中测试精度（对标 Qt::HitTestAccuracy，数值一致）。
 */
typedef enum XTextControlHitTestAccuracy
{
    XTextControlHitTestAccuracy_ExactHit = 0, /**< 精确命中（未落在字形上返回 -1）。 */
    XTextControlHitTestAccuracy_FuzzyHit = 1  /**< 模糊命中（钳位到最近边界）。 */
} XTextControlHitTestAccuracy;

/* ========================================================================== */
/*                          平铺行模型数据结构                                */
/* ========================================================================== */

/**
 * @brief      平铺行模型的单行承载（行数组元素）。
 * @details    定容缓冲 + 行长：data 容量 cap 字节（含 NUL 冗余位），有效
 *             内容 len 字节（不含 NUL）；len 增长时按 cap 倍增扩容。
 *             对标 QTextDocument 的一个文本块（block）。
 */
typedef struct XTextControlLine
{
    char* data; /**< 行缓冲（UTF-8，NUL 结尾冗余存储；可 NULL 当 len==cap==0）。 */
    int len;    /**< 行长（UTF-8 字节数，不含 NUL）。 */
    int cap;    /**< 缓冲容量（字节，含 NUL 位）。 */
} XTextControlLine;

/**
 * @brief      撤销/重做命令（差量承载，对标 QTextDocument 的 undo 命令）。
 * @details    一条命令表达一次原位替换：在绝对位置 pos 处删除 removed
 *             再插入 inserted；同 group 的命令在撤销/重做时整体回滚
 *             （对标 QTextCursor 的 edit block）。group==0 表示自动分组
 *             （连续键入/连续删除按相邻位置合并，对标 Qt 的键入分组）。
 */
typedef struct XTextControlUndoCommand
{
    int pos;     /**< 作用起点（文档绝对 UTF-8 字节位置）。 */
    char* removed;  /**< 被删除文本（堆拷贝；无删除为 NULL）。 */
    char* inserted; /**< 插入文本（堆拷贝；无插入为 NULL）。 */
    int group;      /**< 撤销组号；0 = 自动分组。 */
} XTextControlUndoCommand;

/**
 * @brief      超链接锚点登记项（对标 QTextCharFormat::anchorHref 承载）。
 * @details    平铺模型无逐字符格式，锚点以独立注册表承载：文档绝对
 *             字节区间 [start, end) 与 href。由 insertHtml/setHtml/
 *             insertFromMimeData 的 HTML 子集解析登记，文本重置时清空。
 */
typedef struct XTextControlAnchor
{
    int start;   /**< 起始文档绝对字节位置（含）。 */
    int end;     /**< 结束文档绝对字节位置（排他）。 */
    char* href;  /**< 链接目标（堆拷贝，对标 anchorHref）。 */
} XTextControlAnchor;

/**
 * @brief      额外选择集条目（对标 QTextEdit::ExtraSelection 的平铺承载）。
 * @note       对标差异：Qt 条目携带 QTextCursor + QTextCharFormat（含
 *             FullWidthSelection 属性），此处简化为绝对字节区间 + 单一
 *             ARGB32 高亮色；FullWidthSelection 语义由绘制端按行整行
 *             高亮近似。
 */
typedef struct XTextControlExtraSelection
{
    int start;      /**< 起始文档绝对字节位置（含）。 */
    int end;        /**< 结束文档绝对字节位置（排他）。 */
    uint32_t color; /**< 高亮颜色（ARGB32）。 */
} XTextControlExtraSelection;

/**
 * @brief      输入法查询结果承载（对标 QVariant 返回的 C 形态）。
 * @details    inputMethodQuery 按查询项写入对应成员：type 指明有效类别
 *             （0 无效 / 1 整数 / 2 布尔 / 3 文本 / 4 矩形）；文本为堆
 *             拷贝，调用方以 XFree_System 释放。
 */
typedef struct XTextControlImValue
{
    int type;       /**< 结果类别（0 无效 / 1 整数 / 2 布尔 / 3 文本 / 4 矩形）。 */
    int i;          /**< 类别 1 时的整数值。 */
    bool b;         /**< 类别 2 时的布尔值。 */
    char* text;     /**< 类别 3 时的 UTF-8 文本（堆拷贝，调用方释放）。 */
    XRect rect;     /**< 类别 4 时的矩形（内容坐标）。 */
} XTextControlImValue;

/** @brief 声明 XTextControl 类虚函数枚举：继承 XObject（无新增虚槽）。 */
XCLASS_DEFINE_BEGING(XTextControl)
XCLASS_DEFINE_EXTEND_END(XTextControl, XObject)

/**
 * @brief      XTextControl 类结构体（对标 QWidgetTextControl + Private）。
 * @note       成员即 Qt Private 的平铺承载；位置字段均为文档绝对 UTF-8
 *             字节偏移。m_lines 行数组由实现内部维护（容量 m_lineCap）。
 */
typedef struct XTextControl
{
    XObject m_base;              /**< 继承 XObject（对标 QObject 基类）。 */

    /* ---- 平铺行模型（对标 QTextDocument 内容） ---- */
    XTextControlLine* m_lines;   /**< 行数组（定容缓冲 + 行长，拥有）。 */
    int m_lineCount;             /**< 当前行数（对标 blockCount）。 */
    int m_lineCap;               /**< 行数组容量。 */

    /* ---- 光标与选区（对标 QTextCursor anchor/position） ---- */
    int m_cursorPosition;        /**< 光标位置（绝对字节偏移）。 */
    int m_cursorAnchor;          /**< 选区锚点（绝对字节偏移；== 位置即无选区）。 */
    int m_lastSelPosition;       /**< 上次通知选区的位置（信号去重）。 */
    int m_lastSelAnchor;         /**< 上次通知选区的锚点（信号去重）。 */
    int m_goalCol;               /**< 垂直移动列目标（对标 cursor x 保持）。 */

    /* ---- 格式（对标 QTextCharFormat 位值承载） ---- */
    int m_charFormat;            /**< 当前插入字符格式（位值）。 */
    int m_lastCharFormat;        /**< 上次通知的字符格式（currentCharFormatChanged 去重）。 */

    /* ---- 光标显示与焦点 ---- */
    bool m_cursorOn;             /**< 光标闪灯当前态。 */
    bool m_cursorVisible;        /**< 光标可见开关（setCursorVisible 承载）。 */
    bool m_cursorIsFocusIndicator; /**< 光标仅为焦点指示（键盘链接导航）。 */
    bool m_hasFocus;             /**< 焦点态（FocusIn/FocusOut 事件维护）。 */
    bool m_isEnabled;            /**< 启用态（EnabledChange 事件维护）。 */
    XTimerId m_cursorBlinkTimer; /**< 光标闪烁定时器（XTIMER_INVALID_ID = 未启动）。 */
    XTimerId m_tripleClickTimer; /**< 三击判定定时器（XTIMER_INVALID_ID = 未启动）。 */
    XPoint m_tripleClickPoint;   /**< 三击判定基准点。 */

    /* ---- 属性族（对标同名 Q_PROPERTY） ---- */
    int m_interactionFlags;      /**< 文本交互标志集（默认 TextEditorInteraction）。 */
    bool m_overwriteMode;        /**< 覆盖模式。 */
    bool m_acceptRichText;       /**< 接受富文本粘贴（默认 true）。 */
    int m_cursorWidth;           /**< 光标宽度（px，-1 已解析为风格默认 1）。 */
    bool m_dragEnabled;          /**< 选区可拖拽（默认 true）。 */
    bool m_wordSelectionEnabled; /**< 拖选按词选（默认 false）。 */
    bool m_openExternalLinks;    /**< 链接交由外部打开（默认 false）。 */
    bool m_ignoreUnusedNavigationEvents; /**< 未消费的导航事件是否忽略。 */
    int m_textWidth;             /**< 首选文本宽度（<= 0 = 未设置）。 */

    /* ---- 鼠标状态机 ---- */
    bool m_mousePressed;         /**< 按下拖选中（TextSelectableByMouse）。 */
    bool m_mightStartDrag;       /**< 按在选区内、可能起拖。 */
    bool m_hadSelectionOnMousePress; /**< 按下时已有选区（链接激活判定）。 */
    bool m_dragInProgress;       /**< 自发起拖拽进行中（Move 放置删源）。 */
    XPoint m_mousePressPos;      /**< 按下点（起拖距离判定）。 */
    int m_wordSelStart;          /**< 双击词选区起点（-1 = 无）。 */
    int m_wordSelEnd;            /**< 双击词选区终点（-1 = 无）。 */
    int m_blockSelStart;         /**< 三击段选区起点（-1 = 无）。 */
    int m_blockSelEnd;           /**< 三击段选区终点（-1 = 无）。 */
    char* m_anchorOnMousePress;  /**< 按下点锚点 href（堆拷贝；激活判定）。 */
    char* m_highlightedAnchor;   /**< 当前悬停锚点 href（linkHovered 去重）。 */
    char* m_linkToCopy;          /**< 右键菜单"复制链接"目标。 */

    /* ---- IME preedit（不入文档，对标 layout preeditArea） ---- */
    char* m_preedit;             /**< 组中文本（堆拷贝；空/NULL = 非 preediting）。 */
    int m_preeditPos;            /**< preedit 插入点（文档绝对位置）。 */
    int m_preeditCursor;         /**< preedit 内光标偏移（码点内字节）。 */
    bool m_hideCursor;           /**< IME 要求隐藏光标。 */

    /* ---- 撤销/重做（命令差量栈 + 组） ---- */
    XTextControlUndoCommand* m_undoStack; /**< 撤销栈（拥有，堆数组）。 */
    int m_undoCount;             /**< 撤销栈深度。 */
    int m_undoCap;               /**< 撤销栈容量。 */
    XTextControlUndoCommand* m_redoStack; /**< 重做栈（拥有，堆数组）。 */
    int m_redoCount;             /**< 重做栈深度。 */
    int m_redoCap;               /**< 重做栈容量。 */
    bool m_undoEnabled;          /**< 撤销开关（默认 = 可编辑，对标 init）。 */
    int m_editBlockDepth;        /**< 编辑块嵌套深度（begin/endEditBlock）。 */
    int m_groupCounter;          /**< 组号发生器（自 1 递增）。 */
    bool m_undoAvailable;        /**< 上次通知的 undoAvailable 态。 */
    bool m_redoAvailable;        /**< 上次通知的 redoAvailable 态。 */
    bool m_modified;             /**< 修改标志（对标 QTextDocument::modified）。 */

    /* ---- 额外选择集 / 锚点注册表 ---- */
    XTextControlExtraSelection* m_extraSelections; /**< 额外选择集数组（拥有）。 */
    int m_extraSelectionCount;   /**< 额外选择集条目数。 */
    int m_extraSelectionCap;     /**< 额外选择集容量。 */
    XTextControlAnchor* m_anchors; /**< 锚点注册表（拥有）。 */
    int m_anchorCount;           /**< 锚点条目数。 */
    int m_anchorCap;             /**< 锚点容量。 */

    /* ---- 绘制上下文 ---- */
    int m_dndFeedbackPos;        /**< 拖放反馈光标位置（-1 = 无）。 */
    XPalette m_palette;          /**< 调色板（对标 QWidgetTextControl::palette）。 */
    XFont m_font;                /**< 绘制字体（Qt 由文档默认字体承载）。 */
    int m_lineHeight;            /**< 行高（像素，setFont 时刷新；默认 16）。 */
    int m_lineAscent;            /**< 行内基线偏移（像素；默认 13）。 */

#if XTEXTDOCUMENT_ON
    XTextDocument* m_textDoc;    /**< 内部文档镜像（拥有；document()/setDocument 承载）。 */
#endif
} XTextControl;

/* ========================================================================== */
/*                       生命周期（对标构造/析构）                            */
/* ========================================================================== */

XVtable* XTextControl_class_init(void);

/**
 * @brief      初始化控件（对标 QWidgetTextControl(QObject *parent)）。
 * @details    默认交互标志 TextEditorInteraction、可编辑、拖选开启、
 *             光标宽 1、单空行文档；undoRedoEnabled = 可编辑（对标
 *             QWidgetTextControlPrivate::init）。
 * @param      self 调用方提供的未初始化存储；不可为 NULL。
 */
void XTextControl_init(XTextControl* self);

/**
 * @brief      带文本初始化（对标 QWidgetTextControl(const QString&, parent)；
 *             富文本解析退化为 HTML 子集剥标签）。
 * @param      self 未初始化存储；不可为 NULL。
 * @param      text 初始文本（UTF-8；NULL 视为空）。
 */
void XTextControl_init_2(XTextControl* self, const char* text);

#if XTEXTDOCUMENT_ON
/**
 * @brief      带文档初始化（对标 QWidgetTextControl(QTextDocument*, parent)；
 *             文档纯文本被镜像进平铺行模型）。
 * @param      self 未初始化存储；不可为 NULL。
 * @param      doc 初始文档；NULL 时回退空文档（对标 Qt 行为）。
 */
void XTextControl_init_3(XTextControl* self, XTextDocument* doc);
#endif

#define XTextControl_create() XTextControl_create_ex(XCLASS_DEFAULT_MEMORY_TYPE)
XTextControl* XTextControl_create_ex(XMemoryType memory);
#define XTextControl_deinit_base(self) XObject_deinit_base((XObject*)(self))
#define XTextControl_delete_base(self) XClass_delete_base((XClass*)(self))

/* ========================================================================== */
/*                     文档与光标（对标 public API）                          */
/* ========================================================================== */

#if XTEXTDOCUMENT_ON
/**
 * @brief      设置文档（对标 setDocument）：接管所有权并把纯文本镜像进
 *             平铺行模型；传 NULL 回退新建空文档（对标 Qt）。
 */
void XTextControl_setDocument(XTextControl* self, XTextDocument* doc);
/**
 * @brief      返回内部文档镜像（借用指针，所有权仍归控件；对标 document()）。
 */
XTextDocument* XTextControl_document(const XTextControl* self);
#endif

/**
 * @brief      设置文本光标（对标 setTextCursor(cursor, selectionClipboard)）。
 * @param      self 目标控件；NULL 时无操作。
 * @param      position 光标位置（钳位到 [0, 文档长]）。
 * @param      anchor 选区锚点（钳位同上；== position 即无选区）。
 * @param      selectionClipboard true 时同步 selection 剪贴板
 *             （XGui 无主选择区，退化为不复制，见差异清单）。
 */
void XTextControl_setTextCursor(XTextControl* self, int position, int anchor,
                                bool selectionClipboard);
/**
 * @brief      读取文本光标（对标 textCursor()）。
 * @param      self 目标控件；可为 NULL。
 * @param      position 输出光标位置；可为 NULL。
 * @param      anchor 输出锚点；可为 NULL。
 */
void XTextControl_textCursor(const XTextControl* self, int* position, int* anchor);

void XTextControl_setTextInteractionFlags(XTextControl* self, int flags);
int XTextControl_textInteractionFlags(const XTextControl* self);
void XTextControl_mergeCurrentCharFormat(XTextControl* self, int modifier);
void XTextControl_setCurrentCharFormat(XTextControl* self, int format);
int XTextControl_currentCharFormat(const XTextControl* self);

/**
 * @brief      从光标处查找（对标 find(exp, options)）。
 * @details    支持 FindBackward/FindCaseSensitively/FindWholeWords；命中后
 *             选区即命中串并移动光标（Qt 语义）。正则版 find(QRegularExpression)
 *             无 XGui 对应类型，见差异清单。
 * @return     命中返回 true（选区已置为命中串）。
 */
bool XTextControl_find(XTextControl* self, const char* exp, int options);

char* XTextControl_toPlainText(const XTextControl* self);
char* XTextControl_toHtml(const XTextControl* self);
char* XTextControl_toMarkdown(const XTextControl* self);

/* ========================================================================== */
/*                     命中测试 / 几何 / 链接（public）                       */
/* ========================================================================== */

void XTextControl_ensureCursorVisible(XTextControl* self);
XVariant* XTextControl_loadResource(XTextControl* self, int type, const char* name);
#if XMENU_ON
/**
 * @brief      创建标准右键菜单（对标 createStandardContextMenu）。
 * @return     新建 XMenu*，调用方拥有（DeleteOnClose + popup 用法参照
 *             XPlainTextEdit）；无可菜单化内容时返回 NULL。
 */
XMenu* XTextControl_createStandardContextMenu(XTextControl* self);
#endif
int XTextControl_cursorForPosition(const XTextControl* self, const XPoint* pos);
/**
 * @brief      命中测试（对标 hitTest 虚函数）。
 * @details    行 = y / 行高（钳位），列按字形宽逐码点累加定位到落点前
 *             码点边界（UTF-8 字节偏移），返回文档绝对字节位置；组合行
 *             考虑 preedit 占位（组合区间归并到插入点）。ExactHit 时落
 *             点须在文本区 [0, 行宽] 内（空行仅 x<=0），否则返回 -1。
 * @param      self 目标控件；NULL 或 point 为 NULL 时返回 -1。
 * @param      point 内容坐标点。
 * @param      accuracy 命中精度（XTextControlHitTestAccuracy）。
 * @return     命中的文档绝对字节位置；未命中返回 -1。
 */
int XTextControl_hitTest(const XTextControl* self, const XPoint* point,
                         int accuracy);
/**
 * @brief      指定位置光标矩形（对标 cursorRect(const QTextCursor&)；内容坐标）。
 */
XRect XTextControl_cursorRectAt(const XTextControl* self, int position);
/**
 * @brief      当前光标矩形（对标 cursorRect()；内容坐标）。
 */
XRect XTextControl_cursorRect(const XTextControl* self);
/**
 * @brief      指定选区矩形（对标 selectionRect(const QTextCursor&)；内容坐标）。
 */
XRect XTextControl_selectionRectAt(const XTextControl* self, int position, int anchor);
/**
 * @brief      当前选区矩形（对标 selectionRect()；内容坐标）。
 */
XRect XTextControl_selectionRect(const XTextControl* self);
/**
 * @brief      返回坐标 pos 处锚点 href（对标 anchorAt；空串 = 无链接）。
 * @return     堆上新建 XString*，调用方 XString_delete_base 释放。
 */
XString* XTextControl_anchorAt(const XTextControl* self, const XPoint* pos);
/**
 * @brief      按名称查找命名锚点位置（对标 anchorPosition；返回 (0, y)）。
 */
XPoint XTextControl_anchorPosition(const XTextControl* self, const char* name);
/**
 * @brief      当前光标处锚点 href（对标 anchorAtCursor；空串 = 无）。
 * @return     堆上新建 XString*，调用方释放。
 */
XString* XTextControl_anchorAtCursor(const XTextControl* self);
/**
 * @brief      块标记命中（对标 blockWithMarkerAt；平铺模型无块标记，恒 -1）。
 */
int XTextControl_blockWithMarkerAt(const XTextControl* self, const XPoint* pos);

/* ========================================================================== */
/*                        属性族（对标 Q_PROPERTY）                           */
/* ========================================================================== */

bool XTextControl_overwriteMode(const XTextControl* self);
void XTextControl_setOverwriteMode(XTextControl* self, bool overwrite);
int XTextControl_cursorWidth(const XTextControl* self);
void XTextControl_setCursorWidth(XTextControl* self, int width);
bool XTextControl_acceptRichText(const XTextControl* self);
void XTextControl_setAcceptRichText(XTextControl* self, bool accept);
/**
 * @brief      设置额外选择集（对标 setExtraSelections；整体替换）。
 */
void XTextControl_setExtraSelections(XTextControl* self,
                                     const XTextControlExtraSelection* selections,
                                     int count);
/**
 * @brief      读取额外选择集（对标 extraSelections；返回条目数并输出借用数组）。
 */
int XTextControl_extraSelections(const XTextControl* self,
                                 const XTextControlExtraSelection** selections);
void XTextControl_setTextWidth(XTextControl* self, int width);
int XTextControl_textWidth(const XTextControl* self);
/**
 * @brief      文档尺寸（对标 size()）：宽 = textWidth（未设置为内容宽），
 *             高 = 行数 x 行高。
 */
XSize XTextControl_size(const XTextControl* self);
void XTextControl_setOpenExternalLinks(XTextControl* self, bool open);
bool XTextControl_openExternalLinks(const XTextControl* self);
void XTextControl_setIgnoreUnusedNavigationEvents(XTextControl* self, bool ignore);
bool XTextControl_ignoreUnusedNavigationEvents(const XTextControl* self);
void XTextControl_moveCursor(XTextControl* self, int operation, int mode);
bool XTextControl_canPaste(const XTextControl* self);
void XTextControl_setCursorIsFocusIndicator(XTextControl* self, bool b);
bool XTextControl_cursorIsFocusIndicator(const XTextControl* self);
void XTextControl_setDragEnabled(XTextControl* self, bool enabled);
bool XTextControl_isDragEnabled(const XTextControl* self);
void XTextControl_setWordSelectionEnabled(XTextControl* self, bool enabled);
bool XTextControl_isWordSelectionEnabled(const XTextControl* self);
/**
 * @brief      是否正在输入法组合（对标 isPreediting()）。
 */
bool XTextControl_isPreediting(XTextControl* self);
/**
 * @brief      平铺块矩形（对标 blockBoundingRect 虚函数；内容坐标整行条带）。
 */
XRect XTextControl_blockBoundingRect(const XTextControl* self, int line);

/**
 * @brief      读取调色板（对标 palette()）。
 */
void XTextControl_palette(const XTextControl* self, XPalette* out);
/**
 * @brief      设置调色板（对标 setPalette()）。
 */
void XTextControl_setPalette(XTextControl* self, const XPalette* pal);
/**
 * @brief      设置绘制字体（平铺承载扩展：Qt 由文档默认字体承担；
 *             刷新行高/基线度量并请求重绘）。
 */
void XTextControl_setFont(XTextControl* self, const XFont* font);
/**
 * @brief      读取绘制字体（深拷贝到 *out；配合 XFont_deinit_base 释放）。
 */
void XTextControl_font(const XTextControl* self, XFont* out);

/* ========================================================================== */
/*                          编辑槽（对标 public slots）                       */
/* ========================================================================== */

void XTextControl_setPlainText(XTextControl* self, const char* text);
void XTextControl_setHtml(XTextControl* self, const char* text);
void XTextControl_setMarkdown(XTextControl* self, const char* text);
void XTextControl_cut(XTextControl* self);
void XTextControl_copy(XTextControl* self);
void XTextControl_paste(XTextControl* self);
void XTextControl_undo(XTextControl* self);
void XTextControl_redo(XTextControl* self);
void XTextControl_clear(XTextControl* self);
void XTextControl_selectAll(XTextControl* self);
void XTextControl_insertPlainText(XTextControl* self, const char* text);
void XTextControl_insertHtml(XTextControl* self, const char* text);
void XTextControl_append(XTextControl* self, const char* text);
void XTextControl_appendHtml(XTextControl* self, const char* html);
void XTextControl_appendPlainText(XTextControl* self, const char* text);
/**
 * @brief      调整尺寸（对标 adjustSize()；平铺尺寸派生自行数，行为为
 *             发射 documentSizeChanged，无布局可请求）。
 */
void XTextControl_adjustSize(XTextControl* self);

/**
 * @brief      修改标志查询（对标 QTextDocument::isModified 承载）。
 */
bool XTextControl_isModified(const XTextControl* self);
/**
 * @brief      复位修改标志（对标 QTextDocument::setModified(false) 承载；
 *             状态翻转时发射 modificationChanged）。
 */
void XTextControl_setModified(XTextControl* self, bool modified);
bool XTextControl_isUndoRedoEnabled(const XTextControl* self);
void XTextControl_setUndoRedoEnabled(XTextControl* self, bool enable);

/* ========================================================================== */
/*                        控制方法（对标 public 控制面）                      */
/* ========================================================================== */

/**
 * @brief      键盘/鼠标/IME/拖放事件路由入口（对标 processEvent）。
 * @details    分派 KeyPress/MouseButtonPress/MouseMove/MouseButtonRelease/
 *             MouseButtonDblClick/InputMethod/ContextMenu/FocusIn/
 *             FocusOut/EnabledChange/ShortcutOverride/DragEnter/DragMove/
 *             DragLeave/Drop；交互标志为 NoTextInteraction 时忽略事件。
 *             事件坐标为内容坐标（宿主控件负责滚动偏移，对标 Qt 的
 *             QTransform 平移）。
 * @param      control 目标控件；NULL 时直接返回。
 * @param      event 事件；NULL 时直接返回。
 */
void XTextControl_processEvent(XTextControl* control, XEvent* event);

/**
 * @brief      绘制入口（对标 drawContents(painter, rect, widget)）。
 * @details    按内容坐标绘制：选区高亮（Highlight 背景 + HighlightedText
 *             前景两段式文本）、额外选择集、锚点下划线（Link 色）、IME
 *             preedit 下划线与组合光标、闪烁光标（m_cursorOn 时）。rect
 *             非 NULL 时先做交集裁剪。
 * @param      self 目标控件；NULL 时无操作。
 * @param      painter 绘制器（XGui XPainter 体系）；NULL 时无操作。
 * @param      rect 裁剪/绘制区域（内容坐标）；NULL 绘制全部。
 */
void XTextControl_draw(XTextControl* self, XPainter* painter, const XRect* rect);

/**
 * @brief      合成焦点事件（对标 setFocus(focus, reason)）：经
 *             processEvent 投递 FocusIn/FocusOut。
 */
void XTextControl_setFocus(XTextControl* self, bool focus, XFocusReason reason);

/**
 * @brief      输入法查询（对标 inputMethodQuery(property, argument)）。
 * @param      self 目标控件；NULL 或 out 为 NULL 时返回 false。
 * @param      property 查询项（XInputMethodQuery 位值，取单项）。
 * @param      argument 备用参数：Qt 传入 QPointF 的
 *             ImCursorPosition/ImAbsolutePosition 此处传文档绝对字节
 *             位置（<0 表示用当前光标）；其余查询忽略。
 * @param      out 结果承载；成功时写入（文本成员由调用方释放）。
 * @return     支持该查询返回 true；不支持的查询返回 false（对标无效
 *             QVariant）。
 */
bool XTextControl_inputMethodQuery(const XTextControl* self, int property,
                                   int argument, XTextControlImValue* out);

/**
 * @brief      从选区创建 MIME 数据（虚入口，对标 createMimeDataFromSelection；
 *             平铺承载为选中文本 UTF-8 堆拷贝，调用方 XFree_System）。
 */
char* XTextControl_createMimeDataFromSelection(const XTextControl* self);
/**
 * @brief      能否从 MIME 数据插入（虚入口，对标 canInsertFromMimeData；
 *             平铺模型只认非空 text/plain）。
 */
bool XTextControl_canInsertFromMimeData(const XTextControl* self, const char* source);
/**
 * @brief      从 MIME 数据插入（虚入口，对标 insertFromMimeData；富文本
 *             acceptRichText 开启时尝试 HTML 子集，否则按纯文本插入）。
 */
void XTextControl_insertFromMimeData(XTextControl* self, const char* source);

/**
 * @brief      键盘焦点置入指定锚点（对标 setFocusToAnchor(cursor)）。
 * @param      position 锚点所在文档绝对位置；非锚点返回 false。
 * @return     置入成功返回 true（光标选区 = 锚点区间，焦点指示态）。
 */
bool XTextControl_setFocusToAnchor(XTextControl* self, int position);
/**
 * @brief      焦点移到下/上一个锚点（对标 setFocusToNextOrPreviousAnchor）。
 */
bool XTextControl_setFocusToNextOrPreviousAnchor(XTextControl* self, bool next);
/**
 * @brief      从 fromPos 起查找下/上一个锚点（对标 findNextPrevAnchor）。
 * @param      newStart 输出命中锚点区间起点；可为 NULL。
 * @param      newEnd 输出命中锚点区间终点；可为 NULL。
 * @return     命中返回 true。
 */
bool XTextControl_findNextPrevAnchor(const XTextControl* self, int fromPos,
                                     bool next, int* newStart, int* newEnd);

/* ========================================================================== */
/*                        信号族（对标 Q_SIGNALS，真发射）                    */
/* ========================================================================== */

void* XTextControl_textChanged_signal(XTextControl* self);
void* XTextControl_undoAvailable_signal(XTextControl* self, bool b);
void* XTextControl_redoAvailable_signal(XTextControl* self, bool b);
void* XTextControl_currentCharFormatChanged_signal(XTextControl* self, int format);
void* XTextControl_copyAvailable_signal(XTextControl* self, bool b);
void* XTextControl_selectionChanged_signal(XTextControl* self);
void* XTextControl_cursorPositionChanged_signal(XTextControl* self);
void* XTextControl_updateRequest_signal(XTextControl* self, const XRect* rect);
void* XTextControl_documentSizeChanged_signal(XTextControl* self, const XSize* size);
void* XTextControl_blockCountChanged_signal(XTextControl* self, int newBlockCount);
void* XTextControl_visibilityRequest_signal(XTextControl* self, const XRect* rect);
void* XTextControl_microFocusChanged_signal(XTextControl* self);
void* XTextControl_linkActivated_signal(XTextControl* self, const char* link);
void* XTextControl_linkHovered_signal(XTextControl* self, const char* link);
void* XTextControl_blockMarkerHovered_signal(XTextControl* self, int block);
void* XTextControl_modificationChanged_signal(XTextControl* self, bool m);

#endif /* XTEXTCONTROL_ON */

#ifdef __cplusplus
}
#endif
#endif /* XTEXTCONTROL_H */
