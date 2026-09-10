/**
 * @file       XLineEdit.h
 * @brief      XLineEdit 单行文本编辑控件（对标 Qt 6.8 QLineEdit 全部公共 API）。
 * @details    功能范围：
 *             - 文本编辑：可打印 ASCII 字符插入、Backspace/Delete、
 *               Left/Right/Home/End 光标移动（光标按 UTF-8 字符边界
 *               移动，内部以字节偏移存储）；中文 IME 输入为后续扩展；
 *             - 回显模式：Normal / NoEcho / Password / PasswordEchoOnEdit
 *               （数值与 Qt 6.8 QLineEdit::EchoMode 完全一致）；
 *             - maxLength 钳位、readOnly 只读、placeholder 占位提示
 *               （空文本时灰显）、alignment 对齐、frame 边框开关；
 *             - 选区：Shift+方向键扩展、setSelection、绘制反色高亮；
 *             - 撤销/重做：每次用户编辑前快照文本入栈（栈深 20）；
 *             - 剪贴板：cut/copy/paste 经 XGuiApplication_clipboard 的
 *               XClipboard（XCLIPBOARD_ON 且 XGUIAPPLICATION_ON 且应用
 *               存在时）；不可用时回退到控件内部缓冲；
 *             - 校验与掩码：validator 函数指针、inputMask 字符串
 *               （绘制时按掩码过滤显示、输入按掩码逐字符校验）；
 *             - 光标竖线（焦点内常显，闪烁为后续扩展）；文本超宽时
 *               水平滚动跟随光标（简化估算度量）；
 *             - 信号：textChanged(const char*)、textEdited(const char*)、
 *               cursorPositionChanged(int,int)、returnPressed()、
 *               editingFinished()、selectionChanged()、inputRejected()。
 * @note       模块总开关 XLINEEDIT_ON 定义于 XGuiConfig.h；=0 时裁剪
 *             全部公共 API。依赖 XWIDGET_ON、XPALETTE_ON、XPAINTER_ON；
 *             剪贴板能力依赖 XCLIPBOARD_ON/XGUIAPPLICATION_ON（关闭时
 *             自动回退内部缓冲）。
 * @author     XinYueC 团队
 ******************************************************************************/
#ifndef XLINEEDIT_H
#define XLINEEDIT_H
#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "XGuiConfig.h"
#include "XWidget.h"
#include "XPainter.h"
#include "XAlignment.h"
#include "XAction.h"
#if XMENU_ON
#include "XMenu.h"
#endif /* XMENU_ON */

#if XWIDGET_ON && XLINEEDIT_ON

/** @brief 撤销/重做栈深度（对齐 Qt QLineEdit 内部 20 步上限）。 */
#define XLINEEDIT_UNDO_DEPTH 20

/**
 * @brief      内置 action 位置（对标 QLineEdit::ActionPosition，数值一致）。
 */
typedef enum XLineEditActionPosition
{
    XLineEditActionPosition_Leading = 0,   /**< 文本起始侧（左侧）。 */
    XLineEditActionPosition_Trailing = 1   /**< 文本末尾侧（右侧）。 */
} XLineEditActionPosition;

/** @brief 内置 action 最大数量（超出忽略）。 */
#ifndef XLINEEDIT_MAX_ACTIONS
#define XLINEEDIT_MAX_ACTIONS 8
#endif

/**
 * @brief      回显模式（对标 QLineEdit::EchoMode，数值完全一致）。
 * @details    Normal 正常回显；NoEcho 完全不显示；Password 全部显示
 *             '*'；PasswordEchoOnEdit 在编辑（获得焦点）时正常回显、
 *             失焦后按 Password 显示。
 */
typedef enum XLineEditEchoMode
{
    XLineEditEchoMode_Normal = 0,           /**< 正常回显。 */
    XLineEditEchoMode_NoEcho = 1,           /**< 不回显（空显示）。 */
    XLineEditEchoMode_Password = 2,         /**< 密码回显（全部 '*'）。 */
    XLineEditEchoMode_PasswordEchoOnEdit = 3 /**< 编辑中回显、失焦后密码回显。 */
} XLineEditEchoMode;

/**
 * @brief      光标移动风格（对标 Qt::CursorMoveStyle，数值一致）。
 * @details    LogicalMoveStyle 按文本逻辑顺序移动；VisualMoveStyle 按
 *             视觉方向移动。本实现当前仅存储字段，两种风格行为一致。
 */
typedef enum XLineEditCursorMoveStyle
{
    XLineEditCursorMoveStyle_LogicalMoveStyle = 0, /**< 逻辑移动风格。 */
    XLineEditCursorMoveStyle_VisualMoveStyle = 1   /**< 视觉移动风格。 */
} XLineEditCursorMoveStyle;

/**
 * @brief      校验结果状态（对标 QValidator::State，数值一致）。
 */
typedef enum XLineEditValidatorState
{
    XLineEditValidatorState_Invalid = 0,       /**< 无效：输入被拒绝。 */
    XLineEditValidatorState_Intermediate = 1,  /**< 中间态：编辑期间允许。 */
    XLineEditValidatorState_Acceptable = 2     /**< 可接受。 */
} XLineEditValidatorState;

/**
 * @brief      文本校验回调（对标 QValidator::validate 的 C 适配）。
 * @param      self 被校验的编辑框对象（借用）。
 * @param      text 待校验的完整文本（UTF-8，借用，NUL 结尾）。
 * @param      userData setValidator 时传入的上下文（借用）。
 * @return     校验结果状态；返回 Invalid 的编辑被拒绝并发射 inputRejected。
 */
typedef struct XLineEdit XLineEdit;
typedef XLineEditValidatorState (*XLineEditValidatorFunc)(
    XLineEdit* self, const char* text, void* userData);

/* ==================== 类定义 ==================== */
XCLASS_DEFINE_BEGING(XLineEdit)
XCLASS_DEFINE_EXTEND_END(XLineEdit, XWidget)

/**
 * @brief      XLineEdit 单行编辑控件对象；m_base 必须是第一个成员。
 * @details    字段含义：
 *             - m_text：动态缓冲的 UTF-8 文本（NUL 结尾，堆分配）；
 *             - m_placeholder：占位提示文本（固定缓冲，空文本时绘制）；
 *             - m_cursor：光标的 UTF-8 字节偏移（0..strlen）；
 *             - m_anchor：选区锚点的 UTF-8 字节偏移；与 m_cursor 不等时
 *               表示存在选区（[min,max) 为选中区间）；
 *             - m_viewOffset：水平滚动偏移（文本超宽时跟随光标）；
 *             - m_maxLength：最大字符数（0 = 不限制）；
 *             - m_echoMode：回显模式（XLineEditEchoMode）；
 *             - m_readOnly：只读（拒绝编辑，允许移动与选择）；
 *             - m_frame：是否绘制凹陷边框（默认 true）；
 *             - m_alignment：文本水平对齐（默认 Left）；
 *             - m_displayBuf：显示文本缓存（回显+掩码过滤后，堆分配）；
 *             - m_inputMask：输入掩码字符串（堆分配，NULL=无掩码）；
 *             - m_validator/m_validatorUserData：校验回调与上下文（借用）；
 *             - m_clearButtonEnabled：清除按钮开关（点击清空文本）；
 *             - m_dragEnabled：拖拽开关（后续扩展，仅存储）；
 *             - m_cursorMoveStyle：光标移动风格（仅存储）；
 *             - m_textMargins：文本边距（像素，绘制与尺寸提示使用）；
 *             - m_modified：用户是否修改过文本（isModified）；
 *             - m_undoStack/m_redoStack/m_undoCount/m_redoCount：
 *               撤销/重做栈（每项为文本快照，栈深 XLINEEDIT_UNDO_DEPTH）；
 *             - m_clipboardText：剪贴板回退缓冲（系统剪贴板不可用时用）；
 *             - m_clearButtonRect：清除按钮命中矩形（绘制时记录）。
 *             调用者不得手工修改字段；一律走公开 API。
 */
typedef struct XLineEdit
{
    XWidget m_base;                  /**< 基类成员；必须是第一个。 */
    char*   m_text;                  /**< 文本动态缓冲（NUL 结尾）。 */
    char    m_placeholder[64];       /**< 占位提示（固定缓冲）。 */
    size_t  m_cursor;                /**< 光标字节偏移。 */
    size_t  m_anchor;                /**< 选区锚点字节偏移（无选区时==m_cursor）。 */
    int     m_viewOffset;            /**< 水平滚动偏移（像素，简化估算）。 */
    int     m_maxLength;             /**< 最大字符数；0 = 不限制。 */
    int     m_echoMode;              /**< 回显模式（XLineEditEchoMode）。 */
    bool    m_readOnly;              /**< 只读。 */
    bool    m_frame;                 /**< 是否绘制边框。 */
    int     m_alignment;             /**< 文本对齐（XAlignment 组合）。 */
    char*   m_displayBuf;            /**< 显示文本缓存（回显+掩码过滤；拥有）。 */
    char*   m_inputMask;             /**< 输入掩码（拥有；NULL=无）。 */
    XLineEditValidatorFunc m_validator;  /**< 校验回调（借用；NULL=无）。 */
    void*   m_validatorUserData;     /**< 校验回调上下文（借用）。 */
    bool    m_clearButtonEnabled;    /**< 清除按钮开关（点击清空文本）。 */
    bool    m_dragEnabled;           /**< 拖拽开关（后续扩展，仅存储）。 */
    int     m_cursorMoveStyle;       /**< 光标移动风格（XLineEditCursorMoveStyle）。 */
    XMargins m_textMargins;          /**< 文本边距（像素）。 */
    bool    m_modified;              /**< 用户是否修改过文本。 */
    bool    m_finishedPending;       /**< 自上次 editingFinished 后用户是否编辑过。 */
    char*   m_undoStack[XLINEEDIT_UNDO_DEPTH];  /**< 撤销栈（拥有）。 */
    int     m_undoCount;             /**< 撤销栈深度。 */
    char*   m_redoStack[XLINEEDIT_UNDO_DEPTH];  /**< 重做栈（拥有）。 */
    int     m_redoCount;             /**< 重做栈深度。 */
    char*   m_clipboardText;         /**< 剪贴板回退缓冲（拥有）。 */
    XRect   m_clearButtonRect;       /**< 清除按钮命中矩形（绘制时记录）。 */
    XAction* m_actions[XLINEEDIT_MAX_ACTIONS]; /**< 内置 action 槽（借用指针）。 */
    uint8_t  m_actionPositions[XLINEEDIT_MAX_ACTIONS]; /**< 各 action 位置。 */
    uint8_t  m_actionCount;          /**< 已注册 action 数。 */
} XLineEdit;

/* ==================== 生命周期 ==================== */

XVtable* XLineEdit_class_init(void);
void XLineEdit_init(XLineEdit* self, XWidget* parent, XWidgetFlags flags);
#define XLineEdit_create(parent, flags) XLineEdit_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, (parent), (flags))
XLineEdit* XLineEdit_create_ex(XMemoryType memory, XWidget* parent, XWidgetFlags flags);
#define XLineEdit_deinit_base(self) XWidget_deinit_base((XWidget*)(self))
#define XLineEdit_delete_base(self) XWidget_delete_base((XWidget*)(self))

/* ==================== 父类 XWidget API 宏转发（对齐库内 XRadioButton 惯例） ==================== */

#define XLineEdit_setEnabled(self, enabled) XWidget_setEnabled((XWidget*)(self), (enabled))
#define XLineEdit_isEnabled(self) XWidget_isEnabled((const XWidget*)(self))
#define XLineEdit_setVisible(self, visible) XWidget_setVisible((XWidget*)(self), (visible))
#define XLineEdit_isVisible(self) XWidget_isVisible((const XWidget*)(self))
#define XLineEdit_show(self) XWidget_show((XWidget*)(self))
#define XLineEdit_hide(self) XWidget_hide((XWidget*)(self))
#define XLineEdit_raise(self) XWidget_raise((XWidget*)(self))
#define XLineEdit_lower(self) XWidget_lower((XWidget*)(self))
#define XLineEdit_setGeometry(self, x, y, w, h) XWidget_setGeometry((XWidget*)(self), (x), (y), (w), (h))
#define XLineEdit_setGeometryRect(self, rect) XWidget_setGeometryRect((XWidget*)(self), (rect))
#define XLineEdit_x(self) XWidget_x((const XWidget*)(self))
#define XLineEdit_y(self) XWidget_y((const XWidget*)(self))
#define XLineEdit_width(self) XWidget_width((const XWidget*)(self))
#define XLineEdit_height(self) XWidget_height((const XWidget*)(self))
#define XLineEdit_resize(self, w, h) XWidget_resize((XWidget*)(self), (w), (h))
#define XLineEdit_move(self, x, y) XWidget_move((XWidget*)(self), (x), (y))
#define XLineEdit_update(self) XWidget_update((XWidget*)(self))
#define XLineEdit_updateRect(self, rect) XWidget_updateRect((XWidget*)(self), (rect))
#define XLineEdit_setParent(self, parent, flags) XWidget_setParent((XWidget*)(self), (parent), (flags))
#define XLineEdit_parentWidget(self) XWidget_parentWidget((const XWidget*)(self))
#define XLineEdit_setFocus(self) XWidget_setFocus((XWidget*)(self))
#define XLineEdit_clearFocus(self) XWidget_clearFocus((XWidget*)(self))
#define XLineEdit_hasFocus(self) XWidget_hasFocus((const XWidget*)(self))
#define XLineEdit_setFocusPolicy(self, policy) XWidget_setFocusPolicy((XWidget*)(self), (policy))
#define XLineEdit_setAttribute(self, attribute, on) XWidget_setAttribute((XWidget*)(self), (attribute), (on))
#define XLineEdit_testAttribute(self, attribute) XWidget_testAttribute((const XWidget*)(self), (attribute))
#define XLineEdit_setContentsMargins(self, l, t, r, b) XWidget_setContentsMargins((XWidget*)(self), (l), (t), (r), (b))
#define XLineEdit_contentsMargins(self) XWidget_contentsMargins((const XWidget*)(self))
#define XLineEdit_setWindowFlags(self, flags) XWidget_setWindowFlags((XWidget*)(self), (flags))
#define XLineEdit_updateGeometry(self) XWidget_updateGeometry((XWidget*)(self))


/* ==================== 文本（对标 QLineEdit public API） ==================== */

/**
 * @brief      查询文本（对标 QLineEdit::text）。
 * @param      self 编辑框对象借用指针；可为 NULL。
 * @return     内部文本的借用指针（UTF-8，NUL 结尾）；self 为 NULL 时
 *             返回空字符串。返回指针不得释放或修改，生命周期同 self
 *             及下一次文本修改前的状态。
 */
const char* XLineEdit_text(const XLineEdit* self);
/**
 * @brief      查询按回显模式处理的显示文本（对标 QLineEdit::displayText）。
 * @details    返回内部显示缓存借用指针：NoEcho 为空串；Password /
 *             PasswordEchoOnEdit（失焦）为逐字符 '*'；设置了 inputMask
 *             时按掩码过滤（不匹配的字符以占位符显示）。显示缓存随
 *             文本、回显模式、掩码与焦点变化自动刷新。
 * @param      self 编辑框对象借用指针；可为 NULL。
 * @return     显示文本借用指针（UTF-8）；self 为 NULL 时返回空字符串。
 *             返回指针不得释放或修改。
 */
const char* XLineEdit_displayText(const XLineEdit* self);
/**
 * @brief      设置文本（对标 QLineEdit::setText）。
 * @details    光标移到末尾、清除选区；发射 textChanged；内容相同则忽略。
 *             按 Qt 语义清空撤销/重做历史，且不改变 isModified。
 * @param      self 目标编辑框；可为 NULL。
 * @param      text 新文本（UTF-8）；可为 NULL 等价空串；函数不取得所有权。
 * @return     无返回值；self 为 NULL 或分配失败时保持原状态。
 */
void XLineEdit_setText(XLineEdit* self, const char* text);
/**
 * @brief      清空文本（对标 QLineEdit::clear；等价 setText("")）。
 * @param      self 目标编辑框；可为 NULL。
 * @return     无返回值。
 */
void XLineEdit_clear(XLineEdit* self);
/**
 * @brief      在光标处插入文本（对标 QLineEdit::insert）。
 * @details    替换当前选区；按 maxLength 与 inputMask 过滤；插入成功
 *             视为用户编辑（置 modified、压入撤销栈、发射 textChanged
 *             与 textEdited）。readOnly 时忽略。
 * @param      self 目标编辑框；可为 NULL。
 * @param      utf8 待插入文本（UTF-8）；可为 NULL。
 * @return     无返回值；全部字符被掩码/长度拒绝时不改变文本。
 */
void XLineEdit_insert(XLineEdit* self, const char* utf8);
/** @brief 查询占位提示文本（对标 QLineEdit::placeholderText；借用指针）。 */
const char* XLineEdit_placeholderText(const XLineEdit* self);
/** @brief 设置占位提示文本（对标 QLineEdit::setPlaceholderText；超长截断到 63 字符）。 */
void XLineEdit_setPlaceholderText(XLineEdit* self, const char* placeholder);

/* ==================== 编辑属性（对标 QLineEdit public API） ==================== */

/** @brief 查询只读状态（对标 QLineEdit::isReadOnly）。 */
bool XLineEdit_isReadOnly(const XLineEdit* self);
/** @brief 设置只读（对标 QLineEdit::setReadOnly；true 拒绝编辑键与输入）。 */
void XLineEdit_setReadOnly(XLineEdit* self, bool readOnly);
/** @brief 查询回显模式（对标 QLineEdit::echoMode）。 */
int XLineEdit_echoMode(const XLineEdit* self);
/**
 * @brief      设置回显模式并重绘（对标 QLineEdit::setEchoMode）。
 * @details    仅接受 XLineEditEchoMode 的 4 个合法值；切换时清除选区
 *             并把光标移到末尾（Qt 语义）。
 * @param      self 目标编辑框；可为 NULL。
 * @param      echoMode 目标回显模式（0..3）。
 * @return     无返回值；非法模式或未变化时保持原状态。
 */
void XLineEdit_setEchoMode(XLineEdit* self, int echoMode);
/** @brief 查询最大字符数（对标 QLineEdit::maxLength；0 = 不限制）。 */
int XLineEdit_maxLength(const XLineEdit* self);
/**
 * @brief      设置最大字符数（对标 QLineEdit::setMaxLength）。
 * @details    超长现有文本截断并发射 textChanged；不改变 isModified。
 * @param      self 目标编辑框；可为 NULL。
 * @param      maxLength 新上限；负数按 0（不限制）处理。
 * @return     无返回值。
 */
void XLineEdit_setMaxLength(XLineEdit* self, int maxLength);
/** @brief 查询文本对齐（对标 QLineEdit::alignment；XAlignment 水平组合；默认 Left）。 */
int XLineEdit_alignment(const XLineEdit* self);
/** @brief 设置文本对齐并重绘（对标 QLineEdit::setAlignment）。 */
void XLineEdit_setAlignment(XLineEdit* self, int alignment);
/** @brief 查询边框开关（对标 QLineEdit::hasFrame；默认 true 绘制凹陷边框）。 */
bool XLineEdit_hasFrame(const XLineEdit* self);
/** @brief 设置边框开关并重绘（对标 QLineEdit::setFrame）。 */
void XLineEdit_setFrame(XLineEdit* self, bool on);
/**
 * @brief      查询是否启用清除按钮（对标 QLineEdit::isClearButtonEnabled）。
 * @param      self 编辑框对象借用指针；可为 NULL。
 * @return     启用返回 true；self 为 NULL 返回 false。
 */
bool XLineEdit_isClearButtonEnabled(const XLineEdit* self);
/**
 * @brief      设置清除按钮开关（对标 QLineEdit::setClearButtonEnabled）。
 * @details    启用后文本非空时在右侧绘制小叉提示，点击清除文本（视为
 *             用户编辑：置 modified、压入撤销栈）。图标为内置简笔绘制，
 *             完整图标资源为后续扩展。
 * @param      self 目标编辑框；可为 NULL。
 * @param      enable true 启用，false 关闭。
 * @return     无返回值；变化后刷新绘制与尺寸提示。
 */
void XLineEdit_setClearButtonEnabled(XLineEdit* self, bool enable);
/**
 * @brief      设置文本校验回调（对标 QLineEdit::setValidator 的 C 适配）。
 * @details    校验回调为借用指针，函数不取得所有权；编辑产生的文本若
 *             校验为 Invalid 将被拒绝并发射 inputRejected；
 *             hasAcceptableInput 要求校验为 Acceptable。
 * @param      self 目标编辑框；可为 NULL。
 * @param      validator 校验回调；可为 NULL 清除校验。
 * @param      userData 回调上下文（借用）；随 validator 一起保存。
 * @return     无返回值。
 */
void XLineEdit_setValidator(XLineEdit* self, XLineEditValidatorFunc validator,
                            void* userData);
/**
 * @brief      查询校验回调（对标 QLineEdit::validator）。
 * @param      self 编辑框对象借用指针；可为 NULL。
 * @return     已设置的回调；未设置或 self 为 NULL 返回 NULL。
 */
XLineEditValidatorFunc XLineEdit_validator(const XLineEdit* self);
/**
 * @brief      计算尺寸提示（对标 QLineEdit::sizeHint）。
 * @details    按文本/占位字符数、边距、边框与清除按钮估算宽度；高度按
 *             行高估算。同时会同步到 XWidget 尺寸提示存储供布局使用。
 * @param      self 编辑框对象借用指针；可为 NULL。
 * @return     估算尺寸（像素）；self 为 NULL 返回 (0,0)。
 */
XSize XLineEdit_sizeHint(const XLineEdit* self);
/**
 * @brief      计算最小尺寸提示（对标 QLineEdit::minimumSizeHint）。
 * @details    按 1 个字符宽度估算；同时会同步到 XWidget 最小尺寸提示存储。
 * @param      self 编辑框对象借用指针；可为 NULL。
 * @return     估算最小尺寸（像素）；self 为 NULL 返回 (0,0)。
 */
XSize XLineEdit_minimumSizeHint(const XLineEdit* self);

/**
 * @brief      在编辑框内部注册一个 action（对标 QLineEdit::addAction）。
 * @details    action 以 16px 图标区绘制在文本区起始/末尾侧（按
 *             iconText 显示文本）；点击该区触发 XAction_trigger。
 *             action 指针为借用（生命周期由调用方管理）；数量超过
 *             XLINEEDIT_MAX_ACTIONS 时忽略。
 * @param      self     编辑框对象；可为 NULL。
 * @param      action   要注册的 action（借用）；NULL 忽略。
 * @param      position 位置（XLineEditActionPosition）。
 * @return     无返回值。
 */
void XLineEdit_addAction(XLineEdit* self, XAction* action, int position);

/**
 * @brief      查询光标竖线矩形（对标 QLineEdit::cursorRect）。
 * @details    返回控件本地坐标中光标竖线（1px 宽、文本行高）的矩形，
 *             与绘制一致（含边框/action 区偏移与水平滚动）。
 * @param      self 编辑框对象；可为 NULL。
 * @return     光标矩形；self 为 NULL 时返回零矩形。
 */
XRect XLineEdit_cursorRect(const XLineEdit* self);

/** @brief 查询光标位置（对标 QLineEdit::cursorPosition；UTF-8 字节偏移 0..strlen）。 */
int XLineEdit_cursorPosition(const XLineEdit* self);
/**
 * @brief      设置光标位置（对标 QLineEdit::setCursorPosition）。
 * @details    自动钳位并对齐 UTF-8 字符边界；清除选区（Qt 语义）。
 *             位置变化时发射 cursorPositionChanged。
 * @param      self 目标编辑框；可为 NULL。
 * @param      position 目标字节偏移；负数按 0、超出按 strlen 处理。
 * @return     无返回值。
 */
void XLineEdit_setCursorPosition(XLineEdit* self, int position);
/**
 * @brief      查询指定像素位置的字符位置（对标 QLineEdit::cursorPositionAt）。
 * @param      self 编辑框对象借用指针；可为 NULL。
 * @param      pos 编辑框局部坐标；可为 NULL（等价位置 0）。
 * @return     对应光标的 UTF-8 字节偏移（0..strlen）；self 为 NULL 返回 0。
 */
int XLineEdit_cursorPositionAt(const XLineEdit* self, const XPoint* pos);

/* ==================== 光标移动与编辑键（对标 QLineEdit public API） ==================== */

/**
 * @brief      光标向后（右）移动（对标 QLineEdit::cursorForward）。
 * @details    steps 为字符数（默认 1）；负值等价向前移动。
 * @param      self 目标编辑框；可为 NULL。
 * @param      mark true 时保留锚点扩展选区（Shift+方向键语义）；
 *             false 时清除选区。
 * @param      steps 步数；省略按 1。
 * @return     无返回值。
 */
void XLineEdit_cursorForward(XLineEdit* self, bool mark, int steps);
/**
 * @brief      光标向前（左）移动（对标 QLineEdit::cursorBackward）。
 * @param      self 目标编辑框；可为 NULL。
 * @param      mark true 时扩展选区，false 时清除选区。
 * @param      steps 步数；省略按 1；负值等价向后移动。
 * @return     无返回值。
 */
void XLineEdit_cursorBackward(XLineEdit* self, bool mark, int steps);
/**
 * @brief      光标按词向后（右）移动（对标 QLineEdit::cursorWordForward）。
 * @details    词定义为连续的非空白字符（UTF-8 字符边界）。
 * @param      self 目标编辑框；可为 NULL。
 * @param      mark true 时扩展选区，false 时清除选区。
 * @return     无返回值。
 */
void XLineEdit_cursorWordForward(XLineEdit* self, bool mark);
/**
 * @brief      光标按词向前（左）移动（对标 QLineEdit::cursorWordBackward）。
 * @param      self 目标编辑框；可为 NULL。
 * @param      mark true 时扩展选区，false 时清除选区。
 * @return     无返回值。
 */
void XLineEdit_cursorWordBackward(XLineEdit* self, bool mark);
/**
 * @brief      退格删除（对标 QLineEdit::backspace）。
 * @details    有选区时删除选区；否则删除光标前一个 UTF-8 字符。
 *             readOnly 时忽略。
 * @param      self 目标编辑框；可为 NULL。
 * @return     无返回值。
 */
void XLineEdit_backspace(XLineEdit* self);
/**
 * @brief      删除光标处字符（对标 QLineEdit::del）。
 * @details    有选区时删除选区；否则删除光标处一个 UTF-8 字符。
 *             readOnly 时忽略。
 * @param      self 目标编辑框；可为 NULL。
 * @return     无返回值。
 */
void XLineEdit_del(XLineEdit* self);
/**
 * @brief      光标移到行首（对标 QLineEdit::home）。
 * @param      self 目标编辑框；可为 NULL。
 * @param      mark true 时扩展选区，false 时清除选区。
 * @return     无返回值。
 */
void XLineEdit_home(XLineEdit* self, bool mark);
/**
 * @brief      光标移到行尾（对标 QLineEdit::end）。
 * @param      self 目标编辑框；可为 NULL。
 * @param      mark true 时扩展选区，false 时清除选区。
 * @return     无返回值。
 */
void XLineEdit_end(XLineEdit* self, bool mark);

/* ==================== 修改状态（对标 QLineEdit public API） ==================== */

/**
 * @brief      查询文本是否被用户修改过（对标 QLineEdit::isModified）。
 * @param      self 编辑框对象借用指针；可为 NULL。
 * @return     用户编辑过返回 true；self 为 NULL 返回 false。
 */
bool XLineEdit_isModified(const XLineEdit* self);
/**
 * @brief      设置修改状态（对标 QLineEdit::setModified）。
 * @param      self 目标编辑框；可为 NULL。
 * @param      modified 目标状态。
 * @return     无返回值。
 */
void XLineEdit_setModified(XLineEdit* self, bool modified);

/* ==================== 选区（对标 QLineEdit public API） ==================== */

/**
 * @brief      设置选区（对标 QLineEdit::setSelection）。
 * @details    start 为 UTF-8 字节偏移；length 为字符数（可为负，负值
 *             表示向 start 左侧扩展）。选区变化发射 selectionChanged。
 * @param      self 目标编辑框；可为 NULL。
 * @param      start 选区起点字节偏移；负数按 0、超出按 strlen 处理。
 * @param      length 选区长度（字符数）；0 表示清除选区。
 * @return     无返回值。
 */
void XLineEdit_setSelection(XLineEdit* self, int start, int length);
/**
 * @brief      查询是否存在选区（对标 QLineEdit::hasSelectedText）。
 * @param      self 编辑框对象借用指针；可为 NULL。
 * @return     存在选区返回 true；self 为 NULL 返回 false。
 */
bool XLineEdit_hasSelectedText(const XLineEdit* self);
/**
 * @brief      查询选中文本（对标 QLineEdit::selectedText）。
 * @param      self 编辑框对象借用指针；可为 NULL。
 * @return     新建堆拷贝（UTF-8，NUL 结尾），调用方必须用 XFree_System
 *             释放；无选区或分配失败返回 NULL。
 */
char* XLineEdit_selectedText(const XLineEdit* self);
/**
 * @brief      查询选区起点（对标 QLineEdit::selectionStart）。
 * @param      self 编辑框对象借用指针；可为 NULL。
 * @return     起点 UTF-8 字节偏移；无选区或 self 为 NULL 返回 -1。
 */
int XLineEdit_selectionStart(const XLineEdit* self);
/**
 * @brief      查询选区终点（对标 QLineEdit::selectionEnd）。
 * @param      self 编辑框对象借用指针；可为 NULL。
 * @return     终点 UTF-8 字节偏移；无选区或 self 为 NULL 返回 -1。
 */
int XLineEdit_selectionEnd(const XLineEdit* self);
/**
 * @brief      查询选区长度（对标 QLineEdit::selectionLength）。
 * @param      self 编辑框对象借用指针；可为 NULL。
 * @return     选中字符数；无选区或 self 为 NULL 返回 0。
 */
int XLineEdit_selectionLength(const XLineEdit* self);
/**
 * @brief      清除选区（对标 QLineEdit::deselect）。
 * @details    锚点移到光标处；选区变化发射 selectionChanged。
 * @param      self 目标编辑框；可为 NULL。
 * @return     无返回值。
 */
void XLineEdit_deselect(XLineEdit* self);
/**
 * @brief      全选（对标 QLineEdit::selectAll）。
 * @details    选区覆盖整个文本；选区变化发射 selectionChanged。
 * @param      self 目标编辑框；可为 NULL。
 * @return     无返回值。
 */
void XLineEdit_selectAll(XLineEdit* self);

/* ==================== 撤销/重做（对标 QLineEdit public API） ==================== */

/**
 * @brief      查询是否可撤销（对标 QLineEdit::isUndoAvailable）。
 * @param      self 编辑框对象借用指针；可为 NULL。
 * @return     撤销栈非空返回 true；self 为 NULL 返回 false。
 */
bool XLineEdit_isUndoAvailable(const XLineEdit* self);
/**
 * @brief      查询是否可重做（对标 QLineEdit::isRedoAvailable）。
 * @param      self 编辑框对象借用指针；可为 NULL。
 * @return     重做栈非空返回 true；self 为 NULL 返回 false。
 */
bool XLineEdit_isRedoAvailable(const XLineEdit* self);
/**
 * @brief      撤销上一次用户编辑（对标 QLineEdit::undo）。
 * @details    恢复编辑前文本快照（当前文本压入重做栈）；发射
 *             textChanged；不清除选区锚点以外的编辑历史。
 * @param      self 目标编辑框；可为 NULL。
 * @return     无返回值；撤销栈为空时无操作。
 */
void XLineEdit_undo(XLineEdit* self);
/**
 * @brief      重做上一次撤销（对标 QLineEdit::redo）。
 * @details    恢复被撤销的文本（当前文本压入撤销栈）；发射 textChanged。
 * @param      self 目标编辑框；可为 NULL。
 * @return     无返回值；重做栈为空时无操作。
 */
void XLineEdit_redo(XLineEdit* self);

/* ==================== 剪贴板（对标 QLineEdit public API） ==================== */

/**
 * @brief      剪切选区文本到剪贴板（对标 QLineEdit::cut）。
 * @details    剪贴板经 XGuiApplication_clipboard 的 XClipboard
 *             （XClipboardMode_Clipboard）；剪贴板不可用时回退控件内部
 *             缓冲。readOnly 或无选区时无操作。
 * @param      self 目标编辑框；可为 NULL。
 * @return     无返回值。
 */
void XLineEdit_cut(XLineEdit* self);
/**
 * @brief      复制选区文本到剪贴板（对标 QLineEdit::copy）。
 * @details    剪贴板机制同 cut；无选区时无操作。
 * @param      self 目标编辑框；可为 NULL。
 * @return     无返回值。
 */
void XLineEdit_copy(XLineEdit* self);
/**
 * @brief      从剪贴板粘贴文本到光标处（对标 QLineEdit::paste）。
 * @details    替换选区；按 maxLength 与 inputMask 过滤；粘贴视为用户
 *             编辑（置 modified、压入撤销栈）。剪贴板文本非空但全部
 *             被拒绝时发射 inputRejected。readOnly 时无操作。
 * @param      self 目标编辑框；可为 NULL。
 * @return     无返回值。
 */
void XLineEdit_paste(XLineEdit* self);

#if XMENU_ON
/**
 * @brief      创建标准右键菜单（对标 QLineEdit::createStandardContextMenu）。
 * @details    条目与启用语义完全对齐 Qt 6.8：可编辑态为
 *             撤销/重做/分隔/剪切/复制/粘贴/删除/分隔/全选；只读态为
 *             复制/分隔/全选。剪切与复制仅在 echoMode 为 Normal 且存在
 *             选区时启用；粘贴在剪贴板文本非空时启用；全选在文本非空
 *             且未全选时启用。菜单对象名为 "qt_edit_menu"。返回的菜单
 *             所有权转移给调用方（调用方负责删除）。
 * @param      self 目标编辑框；可为 NULL（返回 NULL）。
 * @return     新菜单对象；创建失败返回 NULL。
 */
XMenu* XLineEdit_createStandardContextMenu(XLineEdit* self);
#endif /* XMENU_ON */

/* ==================== 其他属性（对标 QLineEdit public API） ==================== */

/**
 * @brief      查询拖拽开关（对标 QLineEdit::dragEnabled）。
 * @note       拖拽的完整交互（拖动选择文本/放置）为后续扩展；当前仅
 *             存储字段并返回。
 * @param      self 编辑框对象借用指针；可为 NULL。
 * @return     已启用返回 true；self 为 NULL 返回 false。
 */
bool XLineEdit_dragEnabled(const XLineEdit* self);
/**
 * @brief      设置拖拽开关（对标 QLineEdit::setDragEnabled）。
 * @note       同 dragEnabled：仅存储字段，拖拽交互为后续扩展。
 * @param      self 目标编辑框；可为 NULL。
 * @param      b true 启用，false 关闭。
 * @return     无返回值。
 */
void XLineEdit_setDragEnabled(XLineEdit* self, bool b);
/**
 * @brief      查询光标移动风格（对标 QLineEdit::cursorMoveStyle）。
 * @param      self 编辑框对象借用指针；可为 NULL。
 * @return     移动风格；self 为 NULL 返回 LogicalMoveStyle。
 */
int XLineEdit_cursorMoveStyle(const XLineEdit* self);
/**
 * @brief      设置光标移动风格（对标 QLineEdit::setCursorMoveStyle）。
 * @note       当前两种风格行为一致，仅存储字段（视觉风格为后续扩展）。
 * @param      self 目标编辑框；可为 NULL。
 * @param      style XLineEditCursorMoveStyle 取值。
 * @return     无返回值。
 */
void XLineEdit_setCursorMoveStyle(XLineEdit* self, int style);
/**
 * @brief      查询输入掩码（对标 QLineEdit::inputMask）。
 * @param      self 编辑框对象借用指针；可为 NULL。
 * @return     掩码字符串借用指针（UTF-8）；未设置或 self 为 NULL 返回 ""。
 */
const char* XLineEdit_inputMask(const XLineEdit* self);
/**
 * @brief      设置输入掩码（对标 QLineEdit::setInputMask）。
 * @details    掩码字符集：0=数字必填、9=数字可选、#=数字/±/空格可选、
 *             A=字母必填、a=字母可选、N=字母数字必填、n=字母数字可选、
 *             X=任意字符必填、x=任意字符可选；其余字符为字面分隔符
 *             （不进入文本）；掩码尾部可带 ";占位符" 指定显示占位符
 *             （默认空格）。输入按掩码逐字符校验，显示按掩码过滤。
 * @param      self 目标编辑框；可为 NULL。
 * @param      inputMask 掩码字符串；可为 NULL 或空串清除掩码。
 * @return     无返回值。
 */
void XLineEdit_setInputMask(XLineEdit* self, const char* inputMask);
/**
 * @brief      查询输入是否满足校验与掩码（对标 QLineEdit::hasAcceptableInput）。
 * @details    文本非空、全部掩码必填位已填且每个字符匹配其位置类别、
 *     校验回调（若有）返回 Acceptable 时返回 true。
 * @param      self 编辑框对象借用指针；可为 NULL。
 * @return     可接受返回 true；self 为 NULL 返回 false。
 */
bool XLineEdit_hasAcceptableInput(const XLineEdit* self);
/**
 * @brief      设置文本边距（对标 QLineEdit::setTextMargins(int,int,int,int)）。
 * @details    边距影响文本绘制起点与尺寸提示（像素）。
 * @param      self 目标编辑框；可为 NULL。
 * @param      left/top/right/bottom 四边边距（像素）。
 * @return     无返回值；变化后刷新绘制与尺寸提示。
 */
void XLineEdit_setTextMargins(XLineEdit* self, int left, int top,
                              int right, int bottom);
/**
 * @brief      设置文本边距（边距对象版本，对标 QLineEdit::setTextMargins(QMargins)）。
 * @param      self 目标编辑框；可为 NULL。
 * @param      margins 目标边距；可为 NULL（等价零边距）。
 * @return     无返回值。
 */
void XLineEdit_setTextMargins_2(XLineEdit* self, const XMargins* margins);
/**
 * @brief      查询文本边距（对标 QLineEdit::textMargins）。
 * @param      self 编辑框对象借用指针；可为 NULL。
 * @return     当前边距值拷贝；self 为 NULL 返回零边距。
 */
XMargins XLineEdit_textMargins(const XLineEdit* self);

/* ==================== 信号（对标 QLineEdit signals） ==================== */

/**
 * @brief      textChanged(const char*) 信号标识（对标 QLineEdit::textChanged）。
 * @details    文本因任何原因变化（程序化或用户编辑）时由控件内部发射；
 *             参数为变化后的完整文本（UTF-8，借用）。self 仅用于占位，
 *             本实现中信号函数恒为标识获取器，发射由控件内部调用
 *             XObject_emitSignal 完成。
 * @param      self 编辑框对象借用指针；可为 NULL。
 * @return     不透明的信号标识；不得解引用或释放。
 */
void* XLineEdit_textChanged_signal(XLineEdit* self);
/**
 * @brief      textEdited(const char*) 信号标识（对标 QLineEdit::textEdited）。
 * @details    仅用户编辑（键盘/粘贴/清除按钮等）改变文本时发射；参数
 *             为变化后的完整文本。
 * @param      self 编辑框对象借用指针；可为 NULL。
 * @return     不透明的信号标识。
 */
void* XLineEdit_textEdited_signal(XLineEdit* self);
/**
 * @brief      cursorPositionChanged(int,int) 信号标识（对标
 *             QLineEdit::cursorPositionChanged）。
 * @details    光标位置变化（用户移动或 setCursorPosition）时发射；参数
 *             为旧位置与新位置（UTF-8 字节偏移）。
 * @param      self 编辑框对象借用指针；可为 NULL。
 * @param      oldPos 旧光标字节偏移（仅占位，本实现不用于发射判定）。
 * @param      newPos 新光标字节偏移（仅占位）。
 * @return     不透明的信号标识。
 */
void* XLineEdit_cursorPositionChanged_signal(XLineEdit* self, int oldPos,
                                             int newPos);
/**
 * @brief      发射 returnPressed()（Return 键）。
 * @param      self 编辑框对象借用指针；可为 NULL。
 * @return     不透明的信号标识。
 */
void* XLineEdit_returnPressed_signal(XLineEdit* self);
/**
 * @brief      发射 editingFinished()（Return 键或失焦且自上次发射后
 *             有用户编辑）。
 * @param      self 编辑框对象借用指针；可为 NULL。
 * @return     不透明的信号标识。
 */
void* XLineEdit_editingFinished_signal(XLineEdit* self);
/**
 * @brief      selectionChanged() 信号标识（对标 QLineEdit::selectionChanged）。
 * @details    选区变化（含建立/清除/范围改变）时发射。
 * @param      self 编辑框对象借用指针；可为 NULL。
 * @return     不透明的信号标识。
 */
void* XLineEdit_selectionChanged_signal(XLineEdit* self);
/**
 * @brief      inputRejected() 信号标识（对标 QLineEdit::inputRejected）。
 * @details    键盘输入被掩码/长度/校验拒绝，或剪贴板文本被完全拒绝时
 *             发射。
 * @param      self 编辑框对象借用指针；可为 NULL。
 * @return     不透明的信号标识。
 */
void* XLineEdit_inputRejected_signal(XLineEdit* self);

/** @brief 返回当前聚焦的 XLineEdit（全局；供平台层 IME 直投。可 NULL）。 */
XLineEdit* XLineEdit_focusedLineEdit(void);

#ifdef __cplusplus
}
#endif
#endif /* XWIDGET_ON && XLINEEDIT_ON */

#ifdef __cplusplus
}
#endif
#endif /* XLINEEDIT_H */
