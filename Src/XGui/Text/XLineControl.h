/**
 * @file       XLineControl.h
 * @brief      XLineControl 私有文本控制器（完整对齐 Qt 6.8
 *             QWidgetLineControl 的 API/功能/行为三层）。
 * @details    本类是 Qt 私有类 QWidgetLineControl（qwidgetlinecontrol_p.h
 *             /qwidgetlinecontrol.cpp，QLineEdit 的编辑引擎）的 C99 完整
 *             移植：控件无关、XObject 派生，供 XLineEdit 等
 *             单行编辑控件复用。与 Qt 基准逐方法对照，覆盖：
 *             - 文本模型：text/setText/insert/clear/backspace/del/
 *               internalInsert/internalDelete/internalRemove/internalSetText；
 *             - 撤销栈：undo/redo/internalUndo/internalRedo/Command/
 *               addCommand/finishChange/undoText/redoText（按字分组、
 *               按删除段分组、Separator 分隔、越权回滚）；
 *             - 选区族：setSelection/selectionStart/End/hasSelectedText/
 *               allSelected/inSelection/removeSelection/removeSelectedText/
 *               selectAll/selectWordAtPos/textBeforeSelection/
 *               textAfterSelection/deselect；
 *             - 光标族：cursor/cursorPosition/setCursorPosition/moveCursor/
 *               cursorForward/cursorWordForward/cursorWordBackward/home/end/
 *               xToPos/cursorToX/cursorWidth/rectForPos/cursorRect/
 *               anchorRect/blink 族；
 *             - 回显状态机：setEchoMode/displayText/passwordCharacter/
 *               passwordEchoEditing/updatePasswordEchoEditing/
 *               cancelPasswordEchoTimer/setPasswordMaskDelay/
 *               passwordMaskDelay（PasswordEchoOnEdit 三态）；
 *             - 校验与输入掩码：setValidator/hasAcceptableInput/fixup/
 *               inputMask/setInputMask/parseInputMask/isValidInput/
 *               maskString/clearString/stripString/findInMask/
 *               nextMaskBlank/prevMaskBlank；
 *             - IME：processInputMethodEvent/composeMode/setPreeditArea/
 *               preeditAreaText/commitPreedit/cancelText/setCancelText；
 *             - 键盘：processKeyEvent/processShortcutOverrideEvent
 *               （快捷键分派与 QKeySequence 逐一映射）；
 *             - 绘制数据：draw/redoTextLayout/setFont/setLayoutDirection/
 *               ascent/width/height/naturalTextWidth/textLayout；
 *             - 信号：cursorPositionChanged/selectionChanged/
 *               displayTextChanged/textChanged/textEdited/accepted/
 *               editingFinished/updateNeeded/inputRejected/
 *               editFocusChange/resetInputContext/updateMicroFocus。
 *             位置约定（重要）：Qt 内部以 UTF-16 码元偏移定位；本实现以
 *             UTF-8 字节偏移定位（恒落在字符边界），一处 Qt 代码单元 ↔
 *             一处本实现字符（1~4 字节），Qt 代理对（surrogate）成对处理
 *             的分支一一映射为 UTF-8 续字节成对处理；所有光标/选区/掩码/
 *             撤销命令的 pos 均为 UTF-8 字节偏移，语义与 Qt 逐点对齐。
 * @note       模块开关 XLINECONTROL_ON 由主会话/构建配置定义；关闭时
 *             本头文件全部声明被裁剪。校验器为不透明指针 + validate/fixup
 *             回调钩子（XValidator 体系尚未建立，头文件注明；体系建成后
 *             可无改动替换承载）。依赖 XObject、XEvent（XKeyEvent/
 *             XInputMethodEvent）、XInputMethod 提交形态、XFont 度量与
 *             XClipboard/XCompleter/XStyleHints（均经 XGuiApplication
 *             获取，不可用时按 Qt 侧空对象语义退化）。
 * @author     XinYueC 团队
 ******************************************************************************/
#ifndef XLINECONTROL_H
#define XLINECONTROL_H
#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "XGuiConfig.h"
#include "XObject.h"
#include "XFont.h"

#if XLINECONTROL_ON

/** @brief XCompleter 前向声明（补全器见 XCompleter.h；借用，不拥有）。 */
typedef struct XCompleter XCompleter;
/** @brief XPainter 前向声明（绘制器见 Graphics/XPainter.h）。 */
typedef struct XPainter XPainter;
/** @brief XInputMethodEvent 前向声明（输入法事件见 Window/XWindowEvent.h）。 */
typedef struct XInputMethodEvent XInputMethodEvent;

/* ==================== 枚举（对标 Qt 6.8，数值逐项一致） ==================== */

/**
 * @brief      回显模式（对标 QLineEdit::EchoMode，数值完全一致）。
 */
typedef enum XLineControlEchoMode
{
    XLineControlEchoMode_Normal = 0,            /**< 正常回显。 */
    XLineControlEchoMode_NoEcho = 1,            /**< 不回显（显示空）。 */
    XLineControlEchoMode_Password = 2,          /**< 密码回显（全部掩码字符）。 */
    XLineControlEchoMode_PasswordEchoOnEdit = 3 /**< 编辑中回显、非编辑密码回显。 */
} XLineControlEchoMode;

/**
 * @brief      布局方向（对标 Qt::LayoutDirection，数值完全一致）。
 */
typedef enum XLineControlLayoutDirection
{
    XLineControlLayoutDirection_LeftToRight = 0,  /**< 从左到右。 */
    XLineControlLayoutDirection_RightToLeft = 1,  /**< 从右到左。 */
    XLineControlLayoutDirection_Auto = 2          /**< 按文本内容自动判定。 */
} XLineControlLayoutDirection;

/**
 * @brief      光标移动风格（对标 Qt::CursorMoveStyle，数值完全一致）。
 */
typedef enum XLineControlCursorMoveStyle
{
    XLineControlCursorMoveStyle_LogicalMoveStyle = 0, /**< 按文本逻辑顺序移动。 */
    XLineControlCursorMoveStyle_VisualMoveStyle = 1   /**< 按视觉方向移动。 */
} XLineControlCursorMoveStyle;

/**
 * @brief      xToPos 的定位口径（对标 QTextLine::CursorPosition，数值一致）。
 */
typedef enum XLineControlCursorPosition
{
    XLineControlCursorPosition_BetweenCharacters = 0, /**< 取字符间最近边界。 */
    XLineControlCursorPosition_OnCharacter = 1        /**< 取 x 所在字符自身。 */
} XLineControlCursorPosition;

/**
 * @brief      校验状态（对标 QValidator::State，数值完全一致）。
 * @note       XValidator 体系尚未建立，本枚举随校验回调钩子先行对齐取值。
 */
typedef enum XLineControlValidatorState
{
    XLineControlValidatorState_Invalid = 0,      /**< 无效：变更将被回滚。 */
    XLineControlValidatorState_Intermediate = 1, /**< 中间态：允许编辑。 */
    XLineControlValidatorState_Acceptable = 2    /**< 可接受。 */
} XLineControlValidatorState;

/**
 * @brief      校验回调（对标 QValidator::validate 的 C 适配钩子）。
 * @param      validator setValidator 传入的不透明校验器对象（借用）。
 * @param      text 输入/输出：指向 UTF-8 堆缓冲（NUL 结尾，XMemory_strdup
 *             族分配）的指针；回调可就地修改，或释放旧缓冲并以新堆缓冲
 *             替换（XFree_System 释放旧值）；函数返回后缓冲所有权归控制器。
 * @param      cursor 输入/输出：光标 UTF-8 字节偏移；回调可修正。
 * @param      userData setValidator 传入的上下文（借用）。
 * @return     XLineControlValidatorState 状态值。
 */
typedef int (*XLineControlValidateFunc)(void* validator, char** text,
                                        int* cursor, void* userData);

/**
 * @brief      修复回调（对标 QValidator::fixup 的 C 适配钩子）。
 * @param      validator setValidator 传入的不透明校验器对象（借用）。
 * @param      text 输入/输出：契约同 XLineControlValidateFunc 的 text。
 * @param      userData setValidator 传入的上下文（借用）。
 */
typedef void (*XLineControlFixupFunc)(void* validator, char** text,
                                      void* userData);

/**
 * @brief      键盘方案（对标 QPlatformTheme::KeyboardScheme，数值一致）。
 * @details    Qt 由平台主题在 init 时注入；XGui 无主题层，默认 0
 *             （Windows），并提供 setKeyboardScheme 供接入层注入。
 */
typedef enum XLineControlKeyboardScheme
{
    XLineControlKeyboardScheme_Windows = 0, /**< Windows 方案。 */
    XLineControlKeyboardScheme_Mac = 1,     /**< macOS 方案。 */
    XLineControlKeyboardScheme_X11 = 2,     /**< X11 方案。 */
    XLineControlKeyboardScheme_Kde = 3,     /**< KDE 方案（init 时泛化为 X11）。 */
    XLineControlKeyboardScheme_Gnome = 4,   /**< GNOME 方案（init 时泛化为 X11）。 */
    XLineControlKeyboardScheme_Cde = 5      /**< CDE 方案（init 时泛化为 X11）。 */
} XLineControlKeyboardScheme;

/**
 * @brief      draw() 的绘制内容位标志（对标 QWidgetLineControl::DrawFlags，
 *             数值完全一致）。
 */
typedef enum XLineControlDrawFlag
{
    XLineControlDrawFlag_Text = 0x01,       /**< 绘制显示文本。 */
    XLineControlDrawFlag_Selections = 0x02, /**< 绘制选区/掩码反选背景。 */
    XLineControlDrawFlag_Cursor = 0x04,     /**< 绘制光标竖线。 */
    XLineControlDrawFlag_All = 0x07         /**< 全部内容（Text|Selections|Cursor）。 */
} XLineControlDrawFlag;

/**
 * @brief      撤销命令类型（对标 QWidgetLineControl::CommandType，数值一致）。
 * @note       Qt 私有枚举；此处随撤销栈字段一并暴露为只读数据结构。
 */
typedef enum XLineControlCommandType
{
    XLineControlCommandType_Separator = 0,        /**< 分组分隔（无操作）。 */
    XLineControlCommandType_Insert = 1,           /**< 插入一字符。 */
    XLineControlCommandType_Remove = 2,           /**< 退格删除一字符。 */
    XLineControlCommandType_Delete = 3,           /**< 向前删除一字符。 */
    XLineControlCommandType_RemoveSelection = 4,  /**< 选区删除（光标在选区外）。 */
    XLineControlCommandType_DeleteSelection = 5,  /**< 选区删除（光标在选区内）。 */
    XLineControlCommandType_SetSelection = 6      /**< 选区快照（恢复锚点用）。 */
} XLineControlCommandType;

/**
 * @brief      掩码大小写模式（对标 MaskInputData::Casemode，数值一致）。
 */
typedef enum XLineControlCaseMode
{
    XLineControlCaseMode_NoCaseMode = 0, /**< 不转换。 */
    XLineControlCaseMode_Upper = 1,      /**< 转大写（'>' 之后）。 */
    XLineControlCaseMode_Lower = 2       /**< 转小写（'<' 之后）。 */
} XLineControlCaseMode;

/**
 * @brief      单条撤销/重做命令（对标 QWidgetLineControl::Command）。
 * @note       Qt 以 QChar 承载单码元；本实现以 m_uc/m_ucLen 承载单个
 *             UTF-8 字符（1~4 字节），undo/redo 语义逐点对齐。
 */
typedef struct XLineControlCommand
{
    int      type;      /**< 命令类型（XLineControlCommandType）。 */
    uint8_t  m_uc[5];   /**< 承载的 UTF-8 字符原样字节（4 字节满额 + NUL，
                           *   与 m_maskChar/m_passwordCharacter 同一 5 字节
                           *   契约；消费方按 m_ucLen 读取，NUL 仅为经
                           *   maskCharSet 写入时的边界保证）。 */
    uint8_t  m_ucLen;   /**< 字符字节长（1~4；Separator/SetSelection 为 0）。 */
    int      pos;       /**< 作用位置（UTF-8 字节偏移）。 */
    int      selStart;  /**< 选区起点快照。 */
    int      selEnd;    /**< 选区终点快照。 */
} XLineControlCommand;

/**
 * @brief      输入掩码槽位数据（对标 QWidgetLineControl::MaskInputData）。
 * @note       Qt 每槽存一个 QChar；本实现以 UTF-8 定容数组承载（掩码
 *             分隔符可为多字节字符）。
 */
typedef struct XLineControlMaskInputData
{
    char m_maskChar[5];      /**< 分隔符字符或掩码字符（UTF-8，NUL 结尾）。 */
    bool m_separator;        /**< true 表示字面分隔符（不可编辑）。 */
    int  m_caseMode;         /**< 大小写模式（XLineControlCaseMode）。 */
} XLineControlMaskInputData;

/**
 * @brief      绘制调色板快照（对标 QWidgetLineControl::palette 中 draw()
 *             实际消费的四色；ARGB32）。
 * @note       XGui 的 XPalette 为值对象且按控件承载，控制器侧仅保存
 *             draw() 所需四色；接入层（XLineEdit）在绘制前从控件调色板
 *             刷新即可。
 */
typedef struct XLineControlPalette
{
    uint32_t m_highlight;       /**< 选中区背景色（对标 Highlight）。 */
    uint32_t m_highlightedText; /**< 选中区前景色（对标 HighlightedText）。 */
    uint32_t m_text;            /**< 普通文本前景/掩码反选背景（对标 Text）。 */
    uint32_t m_window;          /**< 掩码反选前景（对标 Window）。 */
} XLineControlPalette;

/**
 * @brief      文本布局数据视图（对标 QWidgetLineControl::textLayout()
 *             返回的 QTextLayout 只读投影）。
 * @details    m_text 为回显处理后的显示文本（对标 QTextLayout::text，
 *             不含 preedit 区）；m_preeditText/preeditPosition 为 preedit
 *             区（对标 setPreeditArea）；m_layoutText 为把 preedit 插入
 *             m_text 后的完整布局串，xToPos/cursorToX/draw 均在该串上
 *             定位；度量均为整数像素（Qt 为 qreal，XGui 度量口径统一
 *             取整）。字段由 updateDisplayText/redoTextLayout 维护，
 *             调用方只读。
 */
typedef struct XLineControlTextLayout
{
    const char* m_text;             /**< 显示文本（借用；不含 preedit）。 */
    const char* m_preeditText;      /**< preedit 区文本（借用；可为空串）。 */
    const char* m_layoutText;       /**< 完整布局文本（借用；preedit 已插入）。 */
    int   m_preeditPosition;        /**< preedit 插入点（m_text 字节偏移）。 */
    int   m_preeditCursor;          /**< preedit 内光标字节偏移；-1 表示无效。 */
    bool  m_hideCursor;             /**< true 隐藏光标（IME 无光标属性时）。 */
    int   m_cursorMoveStyle;        /**< 光标移动风格（XLineControlCursorMoveStyle）。 */
    const XFont* m_font;            /**< 布局字体（借用；NULL=默认字库）。 */
    int   m_ascent;                 /**< 首行 ascent（像素）。 */
    int   m_lineWidth;              /**< 首行宽度（像素，含 preedit；对标 lineAt(0).width()）。 */
    int   m_lineHeight;             /**< 首行高度（像素）。 */
} XLineControlTextLayout;

/* ==================== 类定义 ==================== */
XCLASS_DEFINE_BEGING(XLineControl)
XCLASS_DEFINE_EXTEND_END(XLineControl, XObject)

/**
 * @brief      XLineControl 私有文本控制器对象；m_base 必须是第一个成员。
 * @details    字段与 Qt 6.8 QWidgetLineControl 私有成员一一对应（名字去
 *             m_ 前缀差异除外），含 Qt 位域布局；调用者不得手工修改字段，
 *             一律走公开 API。默认值对齐 Qt 构造函数：maxLength=32767、
 *             layoutDirection=Auto、echoMode=Normal、validInput=true、
 *             lastCursorPos=-1、passwordMaskDelay=-1、
 *             keyboardScheme=Windows；定时器字段以 XTIMER_INVALID_ID 表
 *             示无定时器（Qt 用 0，XGui 定时器句柄体系不同，语义一致）。
 */
typedef struct XLineControl
{
    XObject m_base;                    /**< 基类成员；必须是第一个。 */

    /* ---- 文本模型（对标 m_text QString：定容缓冲 + NUL 终止 + 扩容） ---- */
    char* m_text;                      /**< 编辑文本定容缓冲（拥有，NUL 结尾）。 */
    int   m_textLen;                   /**< 缓冲有效字节长（不含 NUL）。 */
    int   m_textCap;                   /**< 缓冲容量（含 NUL 位；对标 reserve）。 */

    /* ---- 显示/布局缓存（对标 m_textLayout 的文本与度量） ---- */
    char* m_displayText;               /**< 回显处理后的显示文本（拥有）。 */
    int   m_displayLen;                /**< 显示文本字节长。 */
    int   m_displayCap;                /**< 显示文本容量。 */
    char* m_preeditText;               /**< IME preedit 区文本（拥有）。 */
    int   m_preeditLen;                /**< preedit 字节长。 */
    int   m_preeditCap;                /**< preedit 容量。 */
    int   m_preeditPos;                /**< preedit 插入点（文本坐标字节偏移；<0 清除）。 */
    int   m_preeditLayoutPos;          /**< preedit 插入点（显示坐标；redoTextLayout 维护）。 */
    char* m_layoutText;                /**< 布局全文（display+preedit 插入；拥有）。 */
    int   m_layoutLen;                 /**< 布局全文字节长。 */
    int   m_layoutCap;                 /**< 布局全文容量。 */
    int   m_layoutAscent;              /**< 首行 ascent（像素；对标 m_ascent）。 */
    int   m_layoutLineWidth;           /**< 首行宽（像素）。 */
    int   m_layoutLineHeight;          /**< 首行高（像素）。 */
    XFont* m_font;                     /**< 布局字体（拥有深拷贝；NULL=默认字库）。 */
    char* m_textReturn;                /**< text()/inputMask() 返回缓存（拥有）。 */
    int   m_textReturnLen;             /**< 返回缓存字节长。 */
    int   m_textReturnCap;             /**< 返回缓存容量。 */
    char* m_maskReturn;                /**< inputMask() 组装缓存（拥有）。 */
    int   m_maskReturnLen;             /**< 组装缓存字节长。 */
    int   m_maskReturnCap;             /**< 组装缓存容量。 */

    /* ---- 光标与选区 ---- */
    int   m_cursor;                    /**< 光标位置（UTF-8 字节偏移）。 */
    int   m_preeditCursor;             /**< preedit 内光标字节偏移；-1 表示无效。 */
    int   m_cursorWidth;               /**< 光标宽（像素，默认 0）。 */
    int   m_layoutDirection;           /**< 布局方向（XLineControlLayoutDirection）。 */
    int   m_cursorMoveStyle;           /**< 光标移动风格（XLineControlCursorMoveStyle）。 */
    int   m_selstart;                  /**< 选区起点（含）。 */
    int   m_selend;                    /**< 选区终点（不含）。 */
    int   m_lastCursorPos;             /**< 上次已发射的光标位置（初始 -1）。 */
    XPoint m_tripleClick;              /**< 三击坐标快照（对标 m_tripleClick）。 */

    /* ---- 状态位（对标 Qt 位域，逐位一致） ---- */
    uint32_t m_hideCursor : 1;         /**< preedit 内隐藏光标（IME 驱动）。 */
    uint32_t m_separator : 1;          /**< 挂起分组标志（separate/pending）。 */
    uint32_t m_readOnly : 1;           /**< 只读。 */
    uint32_t m_dragEnabled : 1;        /**< 拖拽启用（仅存储）。 */
    uint32_t m_echoMode : 2;           /**< 回显模式（XLineControlEchoMode）。 */
    uint32_t m_textDirty : 1;          /**< 文本未结算标志。 */
    uint32_t m_selDirty : 1;           /**< 选区未发射标志。 */
    uint32_t m_pendingInputRejected : 1; /**< 拒绝发射挂起（finishChange
                          *   在 text 系信号后统一补发，对齐 Qt 顺序）。 */
    uint32_t m_validInput : 1;         /**< 当前文本通过校验。 */
    uint32_t m_blinkStatus : 1;        /**< 光标闪烁相位。 */
    uint32_t m_blinkEnabled : 1;       /**< 光标闪烁使能。 */

    /* ---- 定时器（对标 m_blinkTimer/m_deleteAllTimer/m_tripleClickTimer/
     *      m_passwordEchoTimer；XTIMER_INVALID_ID 表示未启动） ---- */
    XTimerId m_blinkTimer;             /**< 光标闪烁定时器。 */
    XTimerId m_deleteAllTimer;         /**< 键盘导航长按清空定时器。 */
    XTimerId m_tripleClickTimer;       /**< 三击窗口定时器。 */
    XTimerId m_passwordEchoTimer;      /**< 密码回显延迟定时器。 */

    /* ---- 长度与校验 ---- */
    int   m_maxLength;                 /**< 最大字符数（默认 32767；对标 m_maxLength）。 */
    void* m_validator;                 /**< 不透明校验器（借用；XValidator 未建）。 */
    XLineControlValidateFunc m_validateFunc; /**< validate 委托（可 NULL）。 */
    XLineControlFixupFunc    m_fixupFunc;    /**< fixup 委托（可 NULL）。 */
    void* m_validatorUserData;         /**< 校验回调上下文（借用）。 */

    /* ---- 输入掩码（对标 m_inputMask/m_blank/m_maskData） ---- */
    char* m_inputMask;                 /**< 原始掩码串（拥有；NULL=无掩码）。 */
    char  m_blank[5];                  /**< 掩码占位字符（UTF-8，默认空格）。 */
    XLineControlMaskInputData* m_maskData; /**< 掩码槽位表（拥有；NULL=无掩码）。 */
    int   m_maskDataCount;             /**< 槽位表长度（= 掩码后 maxLength）。 */

    /* ---- 撤销栈（对标 m_history/m_undoState/m_modifiedState） ---- */
    XLineControlCommand* m_history;    /**< 命令历史动态数组（拥有）。 */
    int   m_historySize;               /**< 历史长度。 */
    int   m_historyCap;                /**< 历史容量。 */
    int   m_undoState;                 /**< 撤销游标（0..historySize）。 */
    int   m_modifiedState;             /**< 修改基准态（isModified 判定）。 */
    int*  m_transactions;              /**< IME 事务表（Qt 恒空，保留判定）。 */
    int   m_transactionCount;          /**< 事务表长度。 */
    int   m_transactionCap;            /**< 事务表容量。 */

    /* ---- 回显状态机 ---- */
    bool  m_passwordEchoEditing;       /**< PasswordEchoOnEdit 的编辑中标志。 */
    char  m_passwordCharacter[5];      /**< 密码掩码字符（UTF-8，默认 '*'）。 */
    int   m_passwordMaskDelay;         /**< 密码回显延迟 ms（-1=禁用；对标 m_passwordMaskDelay）。 */

    /* ---- 键盘与外部对象 ---- */
    int   m_keyboardScheme;            /**< 键盘方案（XLineControlKeyboardScheme）。 */
    bool  m_keypadNavigationEnabled;   /**< 键盘导航开关（项目扩展；默认 false）。 */
    XObject* m_accessibleObject;       /**< 无障碍事件目标（借用；NULL 时用 m_parent）。 */
    XCompleter* m_completer;           /**< 补全器（借用，不拥有）。 */
    XLineControlPalette m_palette;     /**< 绘制四色快照（对标 m_palette 消费面）。 */

    /* ---- 取消文本（对标 m_cancelText；键盘导航 Back 恢复用） ---- */
    char* m_cancelText;                /**< 取消恢复文本（拥有）。 */
    int   m_cancelTextCap;             /**< 取消文本容量。 */
} XLineControl;

/* ==================== 生命周期（对标构造/析构/init） ==================== */

XVtable* XLineControl_class_init(void);
/**
 * @brief      初始化文本控制器（对标 QWidgetLineControl(const QString&)）。
 * @param      self 目标对象；不可为 NULL（栈对象请先 XMemset 清零）。
 * @param      txt 初始文本（UTF-8）；NULL 等价空串。
 * @return     无返回值。
 */
void XLineControl_init(XLineControl* self, const char* txt);
/**
 * @brief      在堆上创建并初始化控制器。
 * @param      memory 内存类型。
 * @param      txt 初始文本（UTF-8）；NULL 等价空串。
 * @return     新建对象；分配失败返回 NULL。
 */
XLineControl* XLineControl_create_ex(XMemoryType memory, const char* txt);
#define XLineControl_create(txt) \
    XLineControl_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, (txt))
/** @brief 析构基调用宏（对标 ~QWidgetLineControl；密码内存清零）。 */
#define XLineControl_deinit_base(self) XClass_deinit_base((XClass*)(self))
/** @brief 删除基调用宏（堆对象；析构 + 释放）。 */
#define XLineControl_delete_base(self) XClass_delete_base((XClass*)(self))

/* ==================== 文本模型（对标 text/setText/insert/…） ==================== */

/**
 * @brief      查询编辑文本（对标 text()）。
 * @details    设置了输入掩码时按 stripString 剥离未填占位
 *             （"127.0__.0__.1__" → "127.0.0.1"，分隔符保留）；无掩码
 *             时返回编辑文本本身。
 * @param      self 目标对象借用指针；可为 NULL。
 * @return     文本借用指针（UTF-8，NUL 结尾）；生存期同 self 至下一次
 *             任意接口调用；不得释放或修改。NULL 入参返回 ""。
 */
const char* XLineControl_text(const XLineControl* self);
/**
 * @brief      查询回显处理后的显示文本（对标 displayText()）。
 * @details    回显模式与掩码过滤后的文本（NoEcho 为空串、Password 为
 *             密码符重复、掩码含占位符）；借用指针，生存期同 text()。
 *             NULL 入参返回 ""。
 * @param      self 目标对象借用指针；可为 NULL。
 * @return     显示文本借用指针（UTF-8，NUL 结尾）；不得释放或修改。
 */
const char* XLineControl_displayText(const XLineControl* self);
/**
 * @brief      设置文本（对标 setText()）。
 * @details    组合态先 reset 输入法；经 internalSetText：清选区/撤销栈、
 *             光标落尾（pos=-1）、掩码场景经 maskString 适配并发射
 *             inputRejected（内容被掩码改写时）；发射 textChanged（非
 *             edited，不发射 textEdited）。
 * @param      self 目标对象；可为 NULL。
 * @param      txt 新文本（UTF-8）；NULL 等价空串。
 * @return     无返回值。
 */
void XLineControl_setText(XLineControl* self, const char* txt);
/**
 * @brief      在光标处插入文本（对标 insert()）。
 * @details    先删除选区；受 maxLength/inputMask 过滤，被拒绝时发射
 *             inputRejected；作为一次用户编辑结算（textEdited+textChanged）。
 * @param      self 目标对象；可为 NULL。
 * @param      newText 待插入文本（UTF-8）；NULL 等价空串。
 * @return     无返回值。
 */
void XLineControl_insert(XLineControl* self, const char* newText);
/**
 * @brief      清空文本（对标 clear()）。
 * @details    以全选+删除路径执行（撤销栈保留清空记录），非 edited 结算。
 * @param      self 目标对象；可为 NULL。
 * @return     无返回值。
 */
void XLineControl_clear(XLineControl* self);
/**
 * @brief      退格（对标 backspace()）。
 * @details    有选区删选区；否则删光标前一字符（掩码场景先回退到前一
 *             可编辑位并置占位符而非删除）；UTF-8 续字节与首字节成对
 *             删除（对标 Qt 代理对成对删除）。
 * @param      self 目标对象；可为 NULL。
 * @return     无返回值。
 */
void XLineControl_backspace(XLineControl* self);
/**
 * @brief      向前删除（对标 del()）。
 * @details    有选区删选区；否则删光标后一字符（按 UTF-8 字符边界）。
 * @param      self 目标对象；可为 NULL。
 * @return     无返回值。
 */
void XLineControl_del(XLineControl* self);
/**
 * @brief      查询编辑缓冲原文（对标 surroundingText()；IME 查询用）。
 * @param      self 目标对象借用指针；可为 NULL。
 * @return     编辑缓冲借用指针（不剥离掩码；NULL 入参返回 ""）。
 */
const char* XLineControl_surroundingText(const XLineControl* self);

/* ==================== 撤销栈（对标 undo/redo/…） ==================== */

/**
 * @brief      查询是否可撤销（对标 isUndoAvailable()）。
 * @details    密码类回显模式下出于安全仅允许撤销到清空（末命令为
 *             Insert 型才可用），与 Qt 一致。
 */
bool XLineControl_isUndoAvailable(const XLineControl* self);
/**
 * @brief      查询是否可重做（对标 isRedoAvailable()）。
 * @details    密码类回显模式下一律不可重做（安全约束，与 Qt 一致）。
 */
bool XLineControl_isRedoAvailable(const XLineControl* self);
/** @brief 清空撤销历史并复位修改态（对标 clearUndo()）。 */
void XLineControl_clearUndo(XLineControl* self);
/** @brief 查询自 setModified 基准后是否被修改（对标 isModified()）。 */
bool XLineControl_isModified(const XLineControl* self);
/** @brief 设置修改基准（对标 setModified()；true 置 -1 基准）。 */
void XLineControl_setModified(XLineControl* self, bool modified);
/**
 * @brief      撤销（对标 undo()）。
 * @details    Normal 回显下按命令组回退；密码类回显下等价 clear()
 *             （安全约束）。
 */
void XLineControl_undo(XLineControl* self);
/** @brief 重做（对标 redo()；按命令组前进）。 */
void XLineControl_redo(XLineControl* self);
/**
 * @brief      查询撤销组描述文本（undoText；项目扩展承载）。
 * @details    Qt 的 QWidgetLineControl 无该方法（QLineEdit 借助
 *             QUndoStack 体系提供 undoText/redoText 语义）；此处按撤销
 *             游标前一条命令返回 "撤销" 类别描述供接入层菜单使用：
 *             Remove/RemoveSelection/Delete/DeleteSelection 组返回
 *             "重做删除"的逆操作语义名，Insert 组返回 "输入"，其余
 *             返回 "撤销"。接入层可直接用于编辑菜单 undo 项标题。
 * @param      self 目标对象借用指针；可为 NULL。
 * @return     静态描述串（借用，恒有效）；无可撤销返回 NULL。
 */
const char* XLineControl_undoText(const XLineControl* self);
/**
 * @brief      查询重做组描述文本（redoText；项目扩展承载，同 undoText）。
 * @param      self 目标对象借用指针；可为 NULL。
 * @return     静态描述串（借用，恒有效）；无可重做返回 NULL。
 */
const char* XLineControl_redoText(const XLineControl* self);

/* ==================== 选区族（对标 setSelection/…） ==================== */

/**
 * @brief      设置选区（对标 setSelection()）。
 * @details    先提交 preedit；start 非法（<0 或 >len）告警并忽略；
 *             length>0 向右选（光标落选区尾）、<0 向左选（光标落选区
 *             首）、==0 清选区（光标落 start）；发射 selectionChanged 与
 *             cursorPositionChanged。
 * @param      self 目标对象；可为 NULL。
 * @param      start 选区基准（UTF-8 字节偏移，须落在字符边界）。
 * @param      length 选区字符数（可负；按字符计，对标 Qt 语义）。
 * @return     无返回值。
 */
void XLineControl_setSelection(XLineControl* self, int start, int length);
/** @brief 查询选区起点（无选区返回 -1；对标 selectionStart()）。 */
int XLineControl_selectionStart(const XLineControl* self);
/** @brief 查询选区终点（无选区返回 -1；对标 selectionEnd()）。 */
int XLineControl_selectionEnd(const XLineControl* self);
/** @brief 查询是否存在选区（对标 hasSelectedText()）。 */
bool XLineControl_hasSelectedText(const XLineControl* self);
/** @brief 查询是否全选（文本非空且选区覆盖全文；对标 allSelected()）。 */
bool XLineControl_allSelected(const XLineControl* self);
/**
 * @brief      查询像素 x 是否落在选区内（对标 inSelection()）。
 * @details    以 CursorOnCharacter 口径把 x 映射为布局位置后判定。
 */
bool XLineControl_inSelection(const XLineControl* self, int x);
/**
 * @brief      删除选区并结算（对标 removeSelection()）。
 * @details    等价 removeSelectedText + finishChange(priorState)。
 */
void XLineControl_removeSelection(XLineControl* self);
/**
 * @brief      全选（对标 selectAll()）。
 * @details    等价 Qt：selstart=selend=cursor=0 后 moveCursor(len, mark)。
 */
void XLineControl_selectAll(XLineControl* self);
/** @brief 清除选区（锚点归位；对标 deselect()）。 */
void XLineControl_deselect(XLineControl* self);
/**
 * @brief      选中 pos 处的词（对标 selectWordAtPos()）。
 * @details    词边界由按词移动口径（SkipWords 等价实现）定义，词尾
 *             空白不入选。
 * @param      self 目标对象；可为 NULL。
 * @param      cursor 词内位置（UTF-8 字节偏移）。
 * @return     无返回值。
 */
void XLineControl_selectWordAtPos(XLineControl* self, int cursor);
/**
 * @brief      查询选中文本（对标 selectedText()）。
 * @return     新建堆拷贝（UTF-8，NUL 结尾），调用方以 XFree_System 释放；
 *             无选区或分配失败返回 NULL。
 */
char* XLineControl_selectedText(const XLineControl* self);
/**
 * @brief      查询选区前文本（对标 textBeforeSelection()）。
 * @return     新建堆拷贝；契约同 selectedText；无选区返回 NULL。
 */
char* XLineControl_textBeforeSelection(const XLineControl* self);
/**
 * @brief      查询选区后文本（对标 textAfterSelection()）。
 * @return     新建堆拷贝；契约同 selectedText；无选区返回 NULL。
 */
char* XLineControl_textAfterSelection(const XLineControl* self);
/** @brief 查询文本起点（恒 0；对标 start()；C 无重载，与 end(mark) 区名）。 */
int XLineControl_textStart(const XLineControl* self);
/** @brief 查询文本终点（字节长；对标 end()；C 无重载，与 end(mark) 区名）。 */
int XLineControl_textEnd(const XLineControl* self);
/**
 * @brief      pos 起的下一个可编辑掩码位（对标 nextMaskBlank()）。
 * @details    无可编辑位返回文本末尾；顺带置分组挂起标志（位置移动时）。
 * @param      self 目标对象；可为 NULL。
 * @param      pos 基准位置（UTF-8 字节偏移）。
 * @return     下一个可编辑位（字节偏移）；无掩码时语义退化为 pos。
 */
int XLineControl_nextMaskBlank(XLineControl* self, int pos);
/** @brief pos 起的上一个可编辑掩码位（无则 0；对标 prevMaskBlank()）。 */
int XLineControl_prevMaskBlank(XLineControl* self, int pos);

/* ==================== 剪贴板（对标 copy/paste） ==================== */

/**
 * @brief      复制选区文本到剪贴板（对标 copy()）。
 * @details    仅 Normal 回显下生效（密码防泄漏，与 Qt 一致）；mode 为
 *             XClipboardMode 取值（0=Clipboard）；剪贴板服务不可用时
 *             无操作（Qt 空剪贴板语义）。
 * @param      self 目标对象借用指针；可为 NULL。
 * @param      mode 剪贴板模式（XClipboardMode 数值）。
 * @return     无返回值。
 */
void XLineControl_copy(const XLineControl* self, int mode);
/**
 * @brief      粘贴剪贴板文本到光标处（对标 paste()）。
 * @details    剪贴板非空或存在选区时执行 separate→insert→separate
 *             （独立撤销组，与 Qt 一致）；readOnly 由调用方（键盘路径）
 *             把关，本入口与 Qt 同为不设防。
 * @param      self 目标对象；可为 NULL。
 * @param      mode 剪贴板模式（XClipboardMode 数值）。
 * @return     无返回值。
 */
void XLineControl_paste(XLineControl* self, int mode);
/**
 * @brief      私有槽转发：删除当前选区（对标 _q_deleteSelected()）。
 * @details    Qt 中由 QLineEdit 侧连接触发；本实现暴露为可连接入口，
 *             语义为无选区直接返回，否则 resetInputContext + 删选区 +
 *             分组 + 结算。
 */
void XLineControl_deleteSelected(XLineControl* self);

/* ==================== 光标族（对标 cursor/moveCursor/…） ==================== */

/** @brief 查询光标位置（UTF-8 字节偏移；对标 cursor()）。 */
int XLineControl_cursor(const XLineControl* self);
/** @brief 查询 preedit 内光标偏移（对标 preeditCursor()）。 */
int XLineControl_preeditCursor(const XLineControl* self);
/** @brief 查询光标位置（同 cursor；对标 cursorPosition()）。 */
int XLineControl_cursorPosition(const XLineControl* self);
/**
 * @brief      设置光标位置（对标 setCursorPosition()）。
 * @details    pos<=len 时按 qMax(0,pos) 调 moveCursor（不带选区标记）。
 */
void XLineControl_setCursorPosition(XLineControl* self, int pos);
/** @brief 查询光标竖线宽（像素；对标 cursorWidth()）。 */
int XLineControl_cursorWidth(const XLineControl* self);
/** @brief 设置光标竖线宽（像素；对标 setCursorWidth()）。 */
void XLineControl_setCursorWidth(XLineControl* self, int value);
/** @brief 查询光标移动风格（对标 cursorMoveStyle()）。 */
int XLineControl_cursorMoveStyle(const XLineControl* self);
/** @brief 设置光标移动风格（对标 setCursorMoveStyle()）。 */
void XLineControl_setCursorMoveStyle(XLineControl* self, int style);
/**
 * @brief      移动光标（对标 moveCursor()）。
 * @details    位置变化先分组（separate）；掩码场景跳到最近可编辑位；
 *             mark=true 以原锚点扩展选区并刷新显示，否则清选区；随后
 *             发射 selectionChanged（有变化时）与 cursorPositionChanged。
 * @param      self 目标对象；可为 NULL。
 * @param      pos 目标位置（UTF-8 字节偏移）。
 * @param      mark true 扩展选区（Shift 语义）。
 * @return     无返回值。
 */
void XLineControl_moveCursor(XLineControl* self, int pos, bool mark);
/**
 * @brief      光标按字符前后移动（对标 cursorForward()）。
 * @details    steps>0 前进 |steps| 个字符；<0 后退；Visual 风格与逻辑
 *             风格在单行 LTR 布局下等价（布局无重排）。
 */
void XLineControl_cursorForward(XLineControl* self, bool mark, int steps);
/** @brief 光标按词后移（对标 cursorWordForward()；SkipWords 等价口径）。 */
void XLineControl_cursorWordForward(XLineControl* self, bool mark);
/** @brief 光标按词前移（对标 cursorWordBackward()）。 */
void XLineControl_cursorWordBackward(XLineControl* self, bool mark);
/** @brief 光标到行首（对标 home()）。 */
void XLineControl_home(XLineControl* self, bool mark);
/** @brief 光标到行尾（对标 end()）。 */
void XLineControl_end(XLineControl* self, bool mark);
/**
 * @brief      像素 x → 布局位置（对标 xToPos()）。
 * @details    在完整布局串（含 preedit）上按字符宽度累计定位。
 * @param      self 目标对象借用指针；可为 NULL。
 * @param      x 相对文本起点的像素 X。
 * @param      betweenOrOn 定位口径（XLineControlCursorPosition）。
 * @return     UTF-8 字节偏移（落在字符边界）；NULL 入参返回 0。
 */
int XLineControl_xToPos(const XLineControl* self, int x, int betweenOrOn);
/**
 * @brief      查询字符包围盒（对标 rectForPos()）。
 * @details    返回以 cursorToX 为中心、宽 cursorWidth+9、高行高+1 的
 *             矩形（与 Qt 的 cix-5/+9 包络一致），用于拖拽/IME 定位。
 */
XRect XLineControl_rectForPos(const XLineControl* self, int pos);
/** @brief 查询光标矩形（对标 cursorRect()；= rectForPos(cursor)）。 */
XRect XLineControl_cursorRect(const XLineControl* self);
/** @brief 查询锚点矩形（对标 anchorRect()；无选区时=光标矩形）。 */
XRect XLineControl_anchorRect(const XLineControl* self);
/**
 * @brief      布局位置 → 像素 X（对标 cursorToX(cursor)）。
 * @param      self 目标对象借用指针；可为 NULL。
 * @param      cursor 布局坐标中的位置（UTF-8 字节偏移，含 preedit 段）。
 * @return     相对文本起点的像素 X；NULL 入参返回 0。
 */
int XLineControl_cursorToX(const XLineControl* self, int cursor);
/**
 * @brief      当前光标 → 像素 X（对标 cursorToX()）。
 * @details    组合中且光标在插入点时为「组合区起点 + 组合内偏移」，
 *             否则为 mapTextToLayout(cursor) 的映射位置。
 */
int XLineControl_cursorToXCurrent(const XLineControl* self);

/* ==================== 只读/长度/拖拽 ==================== */

/** @brief 查询只读（对标 isReadOnly()）。 */
bool XLineControl_isReadOnly(const XLineControl* self);
/**
 * @brief      设置只读（对标 setReadOnly()）。
 * @details    状态变化后联动光标闪烁（updateCursorBlinking）。
 */
void XLineControl_setReadOnly(XLineControl* self, bool enable);
/** @brief 查询最大字符数（对标 maxLength()）。 */
int XLineControl_maxLength(const XLineControl* self);
/**
 * @brief      设置最大字符数（对标 setMaxLength()）。
 * @details    掩码激活时忽略（掩码长度优先，与 Qt 一致）；否则以当前
 *             文本重走 setText 截断。
 */
void XLineControl_setMaxLength(XLineControl* self, int maxLength);
/** @brief 查询拖拽启用（对标 dragEnabled()；仅存储）。 */
bool XLineControl_dragEnabled(const XLineControl* self);
/** @brief 设置拖拽启用（对标 setDragEnabled()；仅存储）。 */
void XLineControl_setDragEnabled(XLineControl* self, bool enable);

/* ==================== 回显状态机（对标 echoMode/…） ==================== */

/** @brief 查询回显模式（对标 echoMode()；XLineControlEchoMode 值）。 */
uint32_t XLineControl_echoMode(const XLineControl* self);
/**
 * @brief      设置回显模式（对标 setEchoMode()）。
 * @details    取消密码回显定时器、复位 passwordEchoEditing；非 Normal
 *             模式把文本缓冲扩容预留至 30 字符（对标 reserve(30)，降低
 *             密码场景内存搬移暴露面）；刷新显示文本。
 */
void XLineControl_setEchoMode(XLineControl* self, uint32_t mode);
/** @brief 查询密码掩码字符（对标 passwordCharacter()；UTF-8 借用串）。 */
const char* XLineControl_passwordCharacter(const XLineControl* self);
/** @brief 设置密码掩码字符（对标 setPasswordCharacter()；取首字符）。 */
void XLineControl_setPasswordCharacter(XLineControl* self, const char* character);
/** @brief 查询密码回显延迟 ms（-1=禁用；对标 passwordMaskDelay()）。 */
int XLineControl_passwordMaskDelay(const XLineControl* self);
/** @brief 设置密码回显延迟 ms（对标 setPasswordMaskDelay()）。 */
void XLineControl_setPasswordMaskDelay(XLineControl* self, int delay);
/**
 * @brief      查询密码编辑中状态（对标 passwordEchoEditing()）。
 * @details    回显延迟定时器活跃期间恒为 true。
 */
bool XLineControl_passwordEchoEditing(const XLineControl* self);
/**
 * @brief      设置密码编辑中状态（对标 updatePasswordEchoEditing()）。
 * @details    取消回显延迟定时器并刷新显示。
 */
void XLineControl_updatePasswordEchoEditing(XLineControl* self, bool editing);

/* ==================== 校验与输入掩码（对标 validator/inputMask/…） ==================== */

/**
 * @brief      设置校验器钩子（对标 setValidator()）。
 * @details    XValidator 体系尚未建立：validator 为不透明借用指针，
 *             校验/修复经 validateFunc/fixupFunc 委托（契约见回调
 *             typedef 注释）；体系建成后可整体替换。
 * @param      self 目标对象；可为 NULL。
 * @param      validator 不透明校验器（借用；NULL 仅清除钩子函数时同清）。
 * @param      validateFunc validate 委托；可 NULL（视为无校验）。
 * @param      fixupFunc fixup 委托；可 NULL。
 * @param      userData 回调上下文（借用）。
 * @return     无返回值。
 */
void XLineControl_setValidator(XLineControl* self, void* validator,
                               XLineControlValidateFunc validateFunc,
                               XLineControlFixupFunc fixupFunc,
                               void* userData);
/** @brief 查询不透明校验器（对标 validator()；未设置返回 NULL）。 */
void* XLineControl_validator(const XLineControl* self);
/**
 * @brief      查询输入是否可接受（对标 hasAcceptableInput()）。
 * @details    校验钩子须返回 Acceptable；掩码场景还要求长度等于掩码
 *             长度且逐槽位匹配（分隔符精确匹配、可编辑位 isValidInput）。
 */
bool XLineControl_hasAcceptableInput(const XLineControl* self);
/**
 * @brief      尝试修复文本（对标 fixup()）。
 * @details    经 fixup 委托修复后须再次校验为 Acceptable 才生效
 *             （internalSetText 提交，非 edited）；是否变化见返回值。
 * @return     文本被修复并提交返回 true；无钩子/修复无效返回 false。
 */
bool XLineControl_fixup(XLineControl* self);
/**
 * @brief      查询输入掩码（对标 inputMask()）。
 * @details    返回 "掩码[;占位符]" 形式（占位符非空格时补 ";x"）；
 *             借用指针生存期同 text() 返回缓存。无掩码返回 ""。
 */
const char* XLineControl_inputMask(const XLineControl* self);
/**
 * @brief      设置输入掩码（对标 setInputMask()）。
 * @details    经 parseInputMask 解析；成功后光标跳到首个可编辑位
 *             （nextMaskBlank(0)）。空串或以 ';' 开头清除掩码并复位
 *             maxLength=32767、清空文本（Qt 语义）。
 * @param      self 目标对象；可为 NULL。
 * @param      mask 掩码串（UTF-8）；NULL 等价清除。
 * @return     无返回值。
 */
void XLineControl_setInputMask(XLineControl* self, const char* mask);

/* ==================== IME（对标 composeMode/processInputMethodEvent/…） ==================== */

/**
 * @brief      查询是否处于组合态（对标 composeMode()）。
 * @details    preedit 区文本非空即为组合态。
 */
bool XLineControl_composeMode(const XLineControl* self);
/**
 * @brief      设置 preedit 区（对标 setPreeditArea()）。
 * @details    cursor<0 清除 preedit（Qt 以 -1 表示清除）；文本插入到
 *             布局串 cursor 处并重排。
 * @param      self 目标对象；可为 NULL。
 * @param      cursor 插入点（UTF-8 字节偏移；<0 清除）。
 * @param      text preedit 文本（UTF-8）；NULL 等价空串。
 * @return     无返回值。
 */
void XLineControl_setPreeditArea(XLineControl* self, int cursor, const char* text);
/** @brief 查询 preedit 区文本（对标 preeditAreaText()；借用指针）。 */
const char* XLineControl_preeditAreaText(const XLineControl* self);
/**
 * @brief      提交 preedit（对标 commitPreedit()）。
 * @details    经 XGuiApplication_inputMethod 的 commit 转发（可用时）；
 *             随后清 preedit、清格式并强制刷新显示。
 */
void XLineControl_commitPreedit(XLineControl* self);
/** @brief 查询取消恢复文本（对标 cancelText()；借用指针）。 */
const char* XLineControl_cancelText(const XLineControl* self);
/** @brief 设置取消恢复文本（对标 setCancelText()；键盘导航 Back 用）。 */
void XLineControl_setCancelText(XLineControl* self, const char* text);
/**
 * @brief      处理输入法事件（对标 processInputMethodEvent()）。
 * @details    完整移植 Qt 行为：PasswordEchoOnEdit 下首次输入清空并
 *             切编辑态；替换区删除 → 提交串插入 → Selection 属性定位
 *             → 按 echoMode 写 preedit（Password 掩码化、NoEcho 置空）
 *             → Cursor 属性驱动 preeditCursor/hideCursor → 强制刷新 →
 *             光标/微焦点/选区信号结算 → finishChange。
 * @param      self 目标对象；可为 NULL。
 * @param      event 输入法事件（XInputMethodEvent，借用）；NULL 忽略。
 * @return     无返回值。
 * @note       Qt 事件属性表中的 TextFormat 富文本属性在 XGui 无对应
 *             承载，仅处理 Selection/Cursor 两类（其余属性无布局效果）。
 */
void XLineControl_processInputMethodEvent(XLineControl* self,
                                          XInputMethodEvent* event);

/* ==================== 键盘（对标 processKeyEvent/processShortcutOverrideEvent） ==================== */

/**
 * @brief      处理按键事件（对标 processKeyEvent()）。
 * @details    完整移植 Qt 分派链：补全器拦截（弹窗 Escape/内联提交
 *             Enter/Return/F4）→ Return/Return 触发 accepted +
 *             editingFinished（hasAcceptableInput 或 fixup 成功，并
 *             commit/隐藏输入法）→ PasswordEchoOnEdit 首键清空 →
 *             快捷键族（undo/redo/selectAll/copy/paste/cut/删行尾/词移/
 *             字移/删词/删行…按 QKeySequence 标准绑定映射）→ 平台方案
 *             分支（Mac 上下键=Home/End；Ctrl+Backspace 删词首）→
 *             未知键为可打印输入时 insert → accept/ignore 结算；方向
 *             键 L/R 切换布局方向。
 * @param      self 目标对象；可为 NULL。
 * @param      event 键盘事件（借用）；NULL 忽略。
 * @return     无返回值。
 * @note       XKeyEvent 不携带 text()，可打印文本按键直接由键值
 *             （XEvent.h 约定：可打印字符即 ASCII 码位）推导，与
 *             平台注入契约一致；无文本场景（组合键）自然落入快捷键
 *             /忽略分支，行为与 Qt 对齐。
 */
void XLineControl_processKeyEvent(XLineControl* self, XKeyEvent* event);
/**
 * @brief      处理快捷键覆盖事件（对标 processShortcutOverrideEvent()）。
 * @details    编辑类标准快捷键（复制/粘贴/撤销/光标移动族/全选…）与
 *             无修饰可打印键 accept；只读时仅 accept 无编辑副作用键。
 *             匹配口径为 QKeySequence 标准绑定的 Ctrl/Shift 组合。
 * @param      self 目标对象；可为 NULL。
 * @param      ke 键盘事件（借用）；NULL 忽略。
 * @return     无返回值。
 */
void XLineControl_processShortcutOverrideEvent(XLineControl* self, XKeyEvent* ke);
/** @brief 查询键盘方案（对标 m_keyboardScheme；XLineControlKeyboardScheme 值）。 */
int XLineControl_keyboardScheme(const XLineControl* self);
/**
 * @brief      设置键盘方案（项目扩展：Qt 由平台主题注入，XGui 无主题层）。
 * @details    Kde/Gnome/Cde 方案在 init 中已泛化为 X11（与 Qt 一致）；
 *             本接口供接入层在构造后注入主题判定结果。
 */
void XLineControl_setKeyboardScheme(XLineControl* self, int scheme);
/** @brief 查询键盘导航开关（项目扩展：对标 keypadNavigationEnabled 查询）。 */
bool XLineControl_keypadNavigationEnabled(const XLineControl* self);
/** @brief 设置键盘导航开关（项目扩展：驱动 Key_Back 清空/editFocusChange 路径）。 */
void XLineControl_setKeypadNavigationEnabled(XLineControl* self, bool enable);

/* ==================== 补全器（对标 completer/setCompleter/complete） ==================== */

/**
 * @brief      查询已安装补全器（对标 completer()）。
 * @return     XCompleter 借用指针（以不透明指针承载）；未安装返回 NULL。
 */
void* XLineControl_completer(const XLineControl* self);
/**
 * @brief      安装补全器（对标 setCompleter()；借用，不拥有）。
 * @note       生命周期由调用方管理：销毁补全器前必须以 NULL 解绑。
 */
void XLineControl_setCompleter(XLineControl* self, void* completer);
/**
 * @brief      触发补全（对标 complete(int key)）。
 * @details    InlineCompletion 模式按 key 区分翻页/前缀刷新；弹窗模式
 *             刷新 completionPrefix 后触发 XCompleter_complete；readOnly
 *             或非 Normal 回显忽略。
 * @param      self 目标对象；可为 NULL。
 * @param      key 触发键值（XKey；Qt::Key_Backspace/Up/Down 等语义）。
 * @return     无返回值。
 * @note       Qt advanceToEnabledItem 的候选可用位（ItemIsEnabled）在
 *             XCompleter 无承载，所有候选视为可用（@note）。
 */
void XLineControl_complete(XLineControl* self, int key);

/* ==================== 无障碍（对标 setAccessibleObject/accessibleObject） ==================== */

/** @brief 设置无障碍事件目标对象（对标 setAccessibleObject()；借用）。 */
void XLineControl_setAccessibleObject(XLineControl* self, XObject* object);
/** @brief 查询无障碍事件目标（未设置回退 m_parent；对标 accessibleObject()）。 */
XObject* XLineControl_accessibleObject(const XLineControl* self);

/* ==================== 绘制数据（对标 draw/redoTextLayout/setFont/…） ==================== */

/**
 * @brief      设置字体（对标 setFont()）。
 * @details    深拷贝保存；NULL 恢复默认字库；随后强制刷新布局。
 */
void XLineControl_setFont(XLineControl* self, const XFont* font);
/** @brief 查询布局方向（Auto 时按文本首字符判定；对标 layoutDirection()）。 */
int XLineControl_layoutDirection(const XLineControl* self);
/** @brief 设置布局方向并刷新显示（对标 setLayoutDirection()）。 */
void XLineControl_setLayoutDirection(XLineControl* self, int direction);
/**
 * @brief      查询绘制调色板四色快照（对标 palette()）。
 * @param      self 目标对象借用指针；可为 NULL。
 * @return     快照值拷贝；NULL 入参返回全 0（透明）。
 */
XLineControlPalette XLineControl_palette(const XLineControl* self);
/** @brief 设置绘制调色板四色快照（对标 setPalette()）。 */
void XLineControl_setPalette(XLineControl* self, const XLineControlPalette* palette);
/** @brief 查询首行 ascent（像素；对标 ascent()）。 */
int XLineControl_ascent(const XLineControl* self);
/** @brief 查询布局首行宽+1（像素；对标 width()）。 */
int XLineControl_width(const XLineControl* self);
/** @brief 查询布局首行高+1（像素；对标 height()）。 */
int XLineControl_height(const XLineControl* self);
/** @brief 查询文本自然宽（无换行总宽；对标 naturalTextWidth()；单行=行宽）。 */
int XLineControl_naturalTextWidth(const XLineControl* self);
/**
 * @brief      查询布局数据只读视图（对标 textLayout()）。
 * @return     内部视图借用指针（随显示刷新同步）；NULL 入参返回 NULL。
 *             调用方只读，不得释放。
 */
const XLineControlTextLayout* XLineControl_textLayout(const XLineControl* self);
/**
 * @brief      绘制显示文本（对标 draw()）。
 * @details    flags 含 Selections 时先绘选区背景（有选区用
 *             Highlight/HighlightedText；无选区且闪烁相位亮起时按掩码
 *             反选一格：背景 Text、前景 Window）；含 Text 时按布局串
 *             逐段绘制（选区段用 HighlightedText 色、其余用 Text 色，
 *             均相对 offset 平移且按 clip 裁剪）；含 Cursor 时在
 *             cursorToX（含 preedit 偏移）处绘 cursorWidth 宽竖线
 *             （hideCursor 或闪烁熄灭相位不绘）。
 * @param      self 目标对象；可为 NULL。
 * @param      painter 绘制器（借用）；NULL 忽略。
 * @param      offset 文本绘制偏移（借用；NULL 视为 {0,0}）。
 * @param      clip 裁剪矩形（借用；NULL 视为不裁剪）。
 * @param      flags 内容位标志（XLineControlDrawFlag 组合；默认 All）。
 * @return     无返回值。
 */
void XLineControl_draw(XLineControl* self, XPainter* painter,
                       const XPoint* offset, const XRect* clip, int flags);

/* ==================== 光标闪烁（对标 setBlinkingCursorEnabled/…） ==================== */

/**
 * @brief      使能光标闪烁（对标 setBlinkingCursorEnabled()）。
 * @details    按 XStyleHints 的 cursorFlashTime 半周期启动定时器，并
 *             连接 cursorFlashTimeChanged 信号联动重排（XObject_connect_1）。
 */
void XLineControl_setBlinkingCursorEnabled(XLineControl* self, bool enable);
/** @brief 重排光标闪烁（对标 updateCursorBlinking()；复位相位并发射 updateNeeded）。 */
void XLineControl_updateCursorBlinking(XLineControl* self);
/** @brief 重置闪烁定时器相位（对标 resetCursorBlinkTimer()）。 */
void XLineControl_resetCursorBlinkTimer(XLineControl* self);
/** @brief 查询闪烁相位（对标 cursorBlinkStatus()；0/1）。 */
bool XLineControl_cursorBlinkStatus(const XLineControl* self);

/* ==================== 信号（对标 QWidgetLineControl signals） ==================== */

/**
 * @brief      cursorPositionChanged(int,int) 信号标识。
 * @param      self 目标对象借用指针；可为 NULL。
 * @param      oldPos 旧光标位置（占位参数，仅对齐 Qt 签名）。
 * @param      newPos 新光标位置（占位参数）。
 * @return     不透明信号标识；不得解引用或释放。
 */
void* XLineControl_cursorPositionChanged_signal(XLineControl* self, int oldPos, int newPos);
/** @brief selectionChanged() 信号标识（选区变化时发射）。 */
void* XLineControl_selectionChanged_signal(XLineControl* self);
/**
 * @brief      displayTextChanged(const char*) 信号标识。
 * @details    显示文本（回显处理后）变化时发射；参数为显示文本
 *             （UTF-8 借用，仅发射期间有效）。
 */
void* XLineControl_displayTextChanged_signal(XLineControl* self, const char* text);
/**
 * @brief      textChanged(const char*) 信号标识。
 * @details    编辑文本因任何原因变化时发射（程序化 + 用户编辑）。
 */
void* XLineControl_textChanged_signal(XLineControl* self, const char* text);
/**
 * @brief      textEdited(const char*) 信号标识。
 * @details    仅用户编辑路径（insert/backspace/del/removeSelection 等
 *             edited 结算）发射。
 */
void* XLineControl_textEdited_signal(XLineControl* self, const char* text);
/** @brief resetInputContext() 信号标识（请求复位输入法上下文）。 */
void* XLineControl_resetInputContext_signal(XLineControl* self);
/** @brief updateMicroFocus() 信号标识（请求刷新微焦点/IME 光标区）。 */
void* XLineControl_updateMicroFocus_signal(XLineControl* self);
/** @brief accepted() 信号标识（Return 且输入可接受/已修复）。 */
void* XLineControl_accepted_signal(XLineControl* self);
/** @brief editingFinished() 信号标识（Return 提交完成）。 */
void* XLineControl_editingFinished_signal(XLineControl* self);
/**
 * @brief      updateNeeded(const QRect&) 信号标识。
 * @details    光标闪烁等引发的局部重绘请求；参数为待重绘矩形
 *             （掩码激活时 Qt 传空矩形，此处同样传 {0,0,0,0}）。
 */
void* XLineControl_updateNeeded_signal(XLineControl* self, XRect rect);
/** @brief inputRejected() 信号标识（输入被校验/掩码/长度拒绝）。 */
void* XLineControl_inputRejected_signal(XLineControl* self);
/**
 * @brief      editFocusChange(bool) 信号标识。
 * @details    对标 QT_KEYPAD_NAVIGATION 分支（编辑焦点迁移）；XGui 下
 *             由键盘导航开关驱动（默认关闭，接口保留完整面）。
 */
void* XLineControl_editFocusChange_signal(XLineControl* self, bool moving);

#ifdef __cplusplus
}
#endif
#endif /* XLINECONTROL_ON */
#endif /* XLINECONTROL_H */
