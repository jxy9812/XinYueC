/**
 * @file       XLineEditAlignTest.c
 * @brief      XLineEdit 与 Qt 6.8 QLineEdit 的对齐契约测试。
 * @details    覆盖本轮对齐项与既有契约：
 *             - 构造默认值（qlineedit_p.h:125 的 alignment=0x81/帧/回显/
 *               只读/modified/撤销可用性/选区哨兵；maxLength 缺省 32767；
 *               acceptDrops true）；
 *             - setText/insert 的信号分流（程序化 setText 只发 textChanged，
 *               insert 视为用户编辑发 textEdited+textChanged；
 *               对照 qwidgetlinecontrol.cpp finishChange 的 edited 形参）；
 *             - maxLength：setText 截断、满长插入整体拒绝并发
 *               inputRejected、setMaxLength(0) 清空文本并禁止输入、负值原样
 *               保存（qlineedit.cpp:347-380；本机探针）；
 *             - **控制器层负 maxLength 的输入路径**（本轮补齐）：
 *               XLineControl.negativeMaxLengthInputPath——负值不截断、插入
 *               整串落盘并补发 inputRejected（qwidgetlinecontrol_p.h:259-265
 *               与 qwidgetlinecontrol.cpp:836-851；本机探针逐值对齐）；
 *             - 程序化 setText 置 edited（QLineEditPrivate::setText）⇒ 失焦发
 *               editingFinished（本机探针 1 次）；同值 setText 早退为项目
 *               有意偏差（HSF 周期回填，见用例注释）；
 *             - 拖放：acceptDrops + dragEnter/Move/Leave/drop 覆写（落点插入、
 *               只读拒绝）；
 *             - 密码掩码字形取 SH_LineEdit_PasswordCharacter（探针 U+25CF）；
 *             - 选区族 setSelection/hasSelectedText/selectedText/
 *               selectionStart/End/Length/deselect/selectAll；
 *             - 撤销/重做与 setText 清史；
 *             - **回显模式切换不动光标与选区**（qwidgetlinecontrol_p.h:243-256
 *               只取消密码定时器/复位 passwordEchoEditing/刷新显示文本；
 *               本机 Qt 6.8 探针：cursor 2、选区 [1,3) 切 Password 后
 *               cursor 仍 2、hasSelectedText 仍 1）；
 *             - 只读只拦键盘/IME，不拦程序化 insert（qlineedit.cpp:1279-1284
 *               直通控制器；Qt 6.8 探针 readOnly 下 insert 仍生效）；
 *             - XLineEdit_initStyleOption（qlineedit.cpp:73-92：rect 取
 *               contentsRect、State_Sunken、只读补 State_ReadOnly）；
 *             - 补全器挂接（setCompleter 即 completer->setWidget(this)）；
 *             - 父子析构（父析构带走子控件，泄漏门由运行器判定）。
 * @author     XinYueC 团队
 */
#include "xq_align.h"

#include "XLineEdit.h"
#include "XLineControl.h"
#include "XCompleter.h"
#include "XAbstractItemModel.h"
#include "XObject.h"
#include "XClass.h"
#include "XWidget.h"
#include "XMemory.h"
#include "XVarList.h"
#include "XWindowEvent.h"
#include "XCoreApplication.h"
#include "XVariant.h"
#include "XString.h"

#include <stdio.h>
#include <string.h>

#if XWIDGET_ON && XLINEEDIT_ON

/* ==================== 信号计数槽 ==================== */

static int s_changed;  /**< textChanged 次数。 */
static int s_edited;   /**< textEdited 次数。 */
static int s_rejected; /**< inputRejected 次数。 */
static int s_finished; /**< editingFinished 次数。 */

static void xle_slotFinished(XObject* receiver, XVarList* args)
{
    (void)receiver;
    (void)args;
    ++s_finished;
}

static void xle_slotChanged(XObject* receiver, XVarList* args)
{
    (void)receiver;
    (void)args;
    ++s_changed;
}

static void xle_slotEdited(XObject* receiver, XVarList* args)
{
    (void)receiver;
    (void)args;
    ++s_edited;
}

static void xle_slotRejected(XObject* receiver, XVarList* args)
{
    (void)receiver;
    (void)args;
    ++s_rejected;
}

/** @brief 挂接三个计数槽（receiver 取控件自身，槽不解引用载荷）。 */
static void xle_connect(XLineEdit* le)
{
    s_changed = s_edited = s_rejected = 0;
    XObject_connect_1((XObject*)le,
                      (size_t)XLineEdit_textChanged_signal(NULL),
                      (XObject*)le, xle_slotChanged,
                      XConnectionType_Direct);
    XObject_connect_1((XObject*)le,
                      (size_t)XLineEdit_textEdited_signal(NULL),
                      (XObject*)le, xle_slotEdited,
                      XConnectionType_Direct);
    XObject_connect_1((XObject*)le,
                      (size_t)XLineEdit_inputRejected_signal(NULL),
                      (XObject*)le, xle_slotRejected,
                      XConnectionType_Direct);
}

/** @brief 建 3 行单列词条模型（补全器挂接用）。 */
static XAbstractItemModel* xle_makeModel(void)
{
    static const char* const kWords[3] = {"alpha", "beta", "gamma"};
    XAbstractItemModel* m = XAbstractItemModel_create();
    int i;
    if (!m) return NULL;
    XAbstractItemModel_setDimension(m, 3, 1);
    for (i = 0; i < 3; ++i)
        XAbstractItemModel_setData_2(m, i, 0, kWords[i]);
    return m;
}

/* ==================== 用例 ==================== */

XQ_TEST(XLineEdit, defaultsAndTextSignals)
{
    XLineEdit* le = XLineEdit_create(NULL, 0);
    XQ_REQUIRE(le != NULL);

    /* Qt 6.8 探针：默认 text 空、echo Normal、readOnly false、modified
       false、撤销/重做均不可用、无可接受性失败、frame true、
       clearButton false、光标 0、selectionStart/End 均为 -1。 */
    XQ_CHECK_MSG(XLineEdit_text(le)[0] == '\0', "默认文本为空");
    XQ_EQ_I64(XLineEdit_echoMode(le), XLineEditEchoMode_Normal);
    XQ_CHECK(!XLineEdit_isReadOnly(le));
    XQ_CHECK(!XLineEdit_isModified(le));
    XQ_CHECK(!XLineEdit_isUndoAvailable(le));
    XQ_CHECK(!XLineEdit_isRedoAvailable(le));
    XQ_CHECK(XLineEdit_hasFrame(le));
    XQ_CHECK(!XLineEdit_isClearButtonEnabled(le));
    XQ_EQ_I64(XLineEdit_cursorPosition(le), 0);
    XQ_EQ_I64(XLineEdit_selectionStart(le), -1);
    XQ_EQ_I64(XLineEdit_selectionEnd(le), -1);
    XQ_EQ_I64(XLineEdit_selectionLength(le), 0);
    XQ_CHECK(XLineEdit_hasAcceptableInput(le));
    /* Qt 6.8 探针：maxLength 缺省 32767（qlineedit.cpp:347-375）、
       alignment 缺省 0x81=AlignLeading|AlignVCenter（qlineedit_p.h:125）、
       acceptDrops true（qlineedit_p.cpp:239）。 */
    XQ_EQ_I64(XLineEdit_maxLength(le), 32767);
    XQ_EQ_I64(XLineEdit_alignment(le),
              XAlignment_Left | XAlignment_VCenter);
    XQ_CHECK(XWidget_acceptDrops((XWidget*)le));

    xle_connect(le);
    /* 程序化 setText：只发 textChanged（Qt finishChange(edited=false)），
       光标移到末尾（Qt setText 文档语义）。 */
    XLineEdit_setText(le, "abc");
    XQ_EQ_STR(XLineEdit_text(le), "abc");
    XQ_EQ_I64(XLineEdit_cursorPosition(le), 3);
    XQ_EQ_I64(s_changed, 1);
    XQ_EQ_I64(s_edited, 0);
    XQ_CHECK(!XLineEdit_isModified(le));

    /* insert 视为用户编辑：textEdited + textChanged，置 modified。 */
    XLineEdit_insert(le, "d");
    XQ_EQ_STR(XLineEdit_text(le), "abcd");
    XQ_EQ_I64(s_changed, 2);
    XQ_EQ_I64(s_edited, 1);
    XQ_CHECK(XLineEdit_isModified(le));

    /* maxLength：setText 截断；满长 insert 整体拒绝并 inputRejected。 */
    XLineEdit_setMaxLength(le, 3);
    XQ_EQ_I64(XLineEdit_maxLength(le), 3);
    XLineEdit_setText(le, "abcdef");
    XQ_EQ_STR(XLineEdit_text(le), "abc");
    XLineEdit_insert(le, "z");
    XQ_EQ_STR(XLineEdit_text(le), "abc");
    XQ_EQ_I64(s_rejected, 1);

    /* 只读不拦程序化编辑（Qt 6.8 探针：readOnly 下 insert 仍写入）。 */
    XLineEdit_setReadOnly(le, true);
    XQ_CHECK(XLineEdit_isReadOnly(le));
    XLineEdit_setMaxLength(le, 5);
    XLineEdit_insert(le, "Z");
    XQ_EQ_STR(XLineEdit_text(le), "abcZ");

    /* setMaxLength(0)：Qt 探针文本被截断为空串且后续输入整体拒绝
       （qwidgetlinecontrol_p.h:259-265 ⇒ setText 走 left(0)）。 */
    XLineEdit_setMaxLength(le, 0);
    XQ_EQ_I64(XLineEdit_maxLength(le), 0);
    XQ_EQ_STR(XLineEdit_text(le), "");
    XLineEdit_insert(le, "Y");
    XQ_EQ_STR(XLineEdit_text(le), "");
    XQ_EQ_I64(s_rejected, 2);

    /* 负值原样保存（Qt 探针 setMaxLength(-3) 后返回 -3、文本不被截断）；
       本库壳层用壳字段承载负值、控制器按不限运行 ⇒ 插入不再受长度上限
       限制。**控制器层**对负 maxLength 的口径见下方
       XLineControl.negativeMaxLengthInputPath（整串插入 + 补发拒绝）。 */
    XLineEdit_setMaxLength(le, -3);
    XQ_EQ_I64(XLineEdit_maxLength(le), -3);
    XLineEdit_insert(le, "x");
    XQ_EQ_STR(XLineEdit_text(le), "x");

    XClassDelete((XClass*)le);
}

/* ==================== XLineControl 负 maxLength 输入路径 ==================== */

/**
 * @brief 负 maxLength 的插入路径（控制器层，对标 Qt 6.8 逐值）。
 * @details 参考实现：
 *          - `QWidgetLineControl::setMaxLength` 只赋值并 `setText(m_text)`
 *            （qwidgetlinecontrol_p.h:259-265），而 `QString::left(负数)`
 *            返回整串（qstring.cpp:5270-5280）⇒ 文本不被截断；
 *          - `internalInsert` 的 `remaining = m_maxLength - m_text.size()`，
 *            remaining<0 时 `s.left(remaining)` 仍是整串 ⇒ **整串插入**，
 *            且 `s.size() > remaining` 恒真 ⇒ **补发 inputRejected**
 *            （qwidgetlinecontrol.cpp:836-851）。
 *          本机 Qt 6.8 探针（offscreen）：
 *            setMaxLength(-3) 于 "abcdef" ⇒ 文本仍 "abcdef"；
 *            insert("xy") ⇒ "abcdefxy" + 1 次 inputRejected；
 *            setMaxLength(0); insert("A") ⇒ "" + 1 次 inputRejected；
 *            setMaxLength(2) 后 setText("hello") ⇒ "he"。
 *          修复前本库把 `maxChars<=0` 一律当「取 0 字节」⇒ 负值下 setText
 *          清空文本、insert 一个字都落不下（只剩拒绝），与 Qt 相反。
 */
XQ_TEST(XLineControl, negativeMaxLengthInputPath)
{
    XLineControl ctl;
    XLineControl_init(&ctl, "abcdef");
    s_rejected = 0;
    XObject_connect_1((XObject*)&ctl,
                      (size_t)XLineControl_inputRejected_signal(NULL),
                      (XObject*)&ctl, xle_slotRejected,
                      XConnectionType_Direct);

    /* 负值原样保存且**不截断**现有文本（Qt 探针 setMaxLength(-3)）。 */
    XQ_EQ_I64(XLineControl_maxLength(&ctl), 32767);
    XLineControl_setMaxLength(&ctl, -3);
    XQ_EQ_I64(XLineControl_maxLength(&ctl), -3);
    XQ_EQ_STR(XLineControl_text(&ctl), "abcdef");

    /* 负值下插入：整串落盘（不是取 0 字节），并补发一次 inputRejected。 */
    XLineControl_insert(&ctl, "xy");
    XQ_EQ_STR(XLineControl_text(&ctl), "abcdefxy");
    XQ_EQ_I64(s_rejected, 1);

    /* 0 仍表示禁止输入：left(0) 截成空串，插入整体拒绝。 */
    XLineControl_setMaxLength(&ctl, 0);
    XQ_EQ_STR(XLineControl_text(&ctl), "");
    XLineControl_insert(&ctl, "Z");
    XQ_EQ_STR(XLineControl_text(&ctl), "");
    XQ_EQ_I64(s_rejected, 2);

    /* 再回负值：从空串整串插入 UTF-8 多字节串，不劈半个字符。 */
    XLineControl_setMaxLength(&ctl, -1);
    XLineControl_insert(&ctl, "中文");
    XQ_EQ_STR(XLineControl_text(&ctl), "中文");
    XQ_EQ_I64(s_rejected, 3);
    XQ_EQ_I64((int)strlen(XLineControl_text(&ctl)), 6);

    /* 正值上限回归：负值修复不得影响正向截断（Qt 探针 "hello" ⇒ "he"）。 */
    XLineControl_setMaxLength(&ctl, 2);
    XQ_EQ_STR(XLineControl_text(&ctl), "中文");  /* 恰好 2 字符，不截断 */
    XLineControl_setText(&ctl, "hello");
    XQ_EQ_STR(XLineControl_text(&ctl), "he");
    XQ_EQ_I64(s_rejected, 3); /* 程序化 setText 不算用户输入，不补发拒绝 */

    XClassDeinit((XClass*)&ctl);
}

XQ_TEST(XLineEdit, selectionCursorAndUndo)
{
    XLineEdit* le = XLineEdit_create(NULL, 0);
    char* sel;
    XQ_REQUIRE(le != NULL);

    XLineEdit_setText(le, "abcdef");
    /* 选区：start/length 均为字符索引口径（含 UTF-8 字符边界）。 */
    XLineEdit_setSelection(le, 1, 2);
    XQ_CHECK(XLineEdit_hasSelectedText(le));
    XQ_EQ_I64(XLineEdit_selectionStart(le), 1);
    XQ_EQ_I64(XLineEdit_selectionEnd(le), 3);
    XQ_EQ_I64(XLineEdit_selectionLength(le), 2);
    sel = XLineEdit_selectedText(le);
    XQ_REQUIRE(sel != NULL);
    XQ_EQ_STR(sel, "bc");
    XFree_System(sel);

    /* 非法起点整体忽略（qlineedit.cpp:990-995 的 Q_UNLIKELY 分支，不钳位）。 */
    XLineEdit_setSelection(le, -1, 2);
    XQ_EQ_I64(XLineEdit_selectionStart(le), 1);

    XLineEdit_deselect(le);
    XQ_CHECK(!XLineEdit_hasSelectedText(le));
    XQ_EQ_I64(XLineEdit_selectionStart(le), -1);

    XLineEdit_selectAll(le);
    XQ_EQ_I64(XLineEdit_selectionStart(le), 0);
    XQ_EQ_I64(XLineEdit_selectionLength(le), 6);

    /* 光标：越界钳位、按 UTF-8 字符计（不是字节）。 */
    XLineEdit_setText(le, "中文");
    XLineEdit_setCursorPosition(le, 99);
    XQ_EQ_I64(XLineEdit_cursorPosition(le), 2);
    XLineEdit_setCursorPosition(le, 1);
    XQ_EQ_I64(XLineEdit_cursorPosition(le), 1);

    /* 撤销/重做：setText 清史，insert 一次可撤销一步。 */
    XLineEdit_setText(le, "abc");
    XQ_CHECK(!XLineEdit_isUndoAvailable(le));
    XLineEdit_insert(le, "d");
    XQ_CHECK(XLineEdit_isUndoAvailable(le));
    XLineEdit_undo(le);
    XQ_EQ_STR(XLineEdit_text(le), "abc");
    XQ_CHECK(XLineEdit_isRedoAvailable(le));
    XLineEdit_redo(le);
    XQ_EQ_STR(XLineEdit_text(le), "abcd");

    XClassDelete((XClass*)le);
}

XQ_TEST(XLineEdit, echoModeKeepsCursorAndSelection)
{
    XLineEdit* le = XLineEdit_create(NULL, 0);
    const char* display;
    XQ_REQUIRE(le != NULL);

    XLineEdit_setText(le, "abcdef");
    XLineEdit_setSelection(le, 1, 2); /* 光标落 3、选区 [1,3)。 */
    XQ_EQ_I64(XLineEdit_cursorPosition(le), 3);

    /* 本机 Qt 6.8 探针：切 Password 后 cursor 仍 3、hasSelectedText 仍 1
       （qwidgetlinecontrol_p.h:243-256 的 setEchoMode 不触碰选区/光标）。 */
    XLineEdit_setEchoMode(le, XLineEditEchoMode_Password);
    XQ_EQ_I64(XLineEdit_echoMode(le), XLineEditEchoMode_Password);
    XQ_EQ_I64(XLineEdit_cursorPosition(le), 3);
    XQ_CHECK(XLineEdit_hasSelectedText(le));
    display = XLineEdit_displayText(le);
#if XSTYLE_ON
    /* 密码掩码字形取 SH_LineEdit_PasswordCharacter（qlineedit_p.cpp:226）。
     * 本机 Qt 6.8 探针该 hint 返回 U+25CF「●」，displayText 为 6 个 ●
     * （每个 3 字节 UTF-8，共 18 字节）。 */
    XQ_EQ_STR(display, "\xE2\x97\x8F\xE2\x97\x8F\xE2\x97\x8F"
                       "\xE2\x97\x8F\xE2\x97\x8F\xE2\x97\x8F");
#else
    /* 无样式子系统时保留控制器缺省掩码 '*'。 */
    XQ_EQ_STR(display, "******");
#endif

    /* NoEcho：显示空串，选区与光标保持（Qt 同）。 */
    XLineEdit_setEchoMode(le, XLineEditEchoMode_NoEcho);
    XQ_EQ_STR(XLineEdit_displayText(le), "");
    XQ_EQ_I64(XLineEdit_cursorPosition(le), 3);
    XQ_CHECK(XLineEdit_hasSelectedText(le));

    /* 非法回显值忽略（Qt 枚举只接受 0..3；本库同口径按 0..3 门禁）。 */
    XLineEdit_setEchoMode(le, 99);
    XQ_EQ_I64(XLineEdit_echoMode(le), XLineEditEchoMode_NoEcho);

    XLineEdit_setEchoMode(le, XLineEditEchoMode_Normal);
    XQ_EQ_STR(XLineEdit_displayText(le), "abcdef");

    XClassDelete((XClass*)le);
}

XQ_TEST(XLineEdit, styleOptionAndCompleterAndParent)
{
    XLineEdit* le = XLineEdit_create(NULL, 0);
    XWidget* parent;
    XLineEdit* child;
    XAbstractItemModel* model;
    XCompleter* completer;
    XQ_REQUIRE(le != NULL);

#if XSTYLE_ON
    {
        XStyleOption opt;
        XLineEdit_setPlaceholderText(le, "请输入");
        XLineEdit_setReadOnly(le, true);
        XLineEdit_initStyleOption(le, &opt);
        /* qlineedit.cpp:73-92：rect=contentsRect、State_Sunken、
           只读补 State_ReadOnly、text 承载 placeholder。 */
        XQ_EQ_I64(opt.m_type, XStylePE_PanelLineEdit);
        XQ_EQ_I64(opt.m_optionType, XStyleOptionType_Frame);
        XQ_CHECK((opt.m_state & XStyleState_Sunken) != 0);
        XQ_CHECK((opt.m_state & XStyleState_ReadOnly) != 0);
        XQ_EQ_STR(opt.m_text, "请输入");
        XQ_CHECK(opt.m_rect.width == XWidget_contentsRect((XWidget*)le).width);
        XQ_CHECK(opt.m_rect.height ==
                 XWidget_contentsRect((XWidget*)le).height);
    }
#endif /* XSTYLE_ON */

    /* 补全器挂接：setCompleter 即 completer->setWidget(this)，
       解绑时清空关联位（避免悬挂借用）。 */
    model = xle_makeModel();
    completer = XCompleter_create_2(model, NULL);
    XQ_REQUIRE(model != NULL && completer != NULL);
    XLineEdit_setCompleter(le, completer);
    XQ_CHECK(XLineEdit_completer(le) == completer);
    XQ_CHECK(XCompleter_widget(completer) == (XWidget*)le);
    XLineEdit_setCompleter(le, NULL);
    XQ_CHECK(XLineEdit_completer(le) == NULL);
    XQ_CHECK(XCompleter_widget(completer) == NULL);
    XClassDelete((XClass*)completer);
    XClassDelete((XClass*)model);

    /* 父子析构：父析构带走子控件（泄漏门由运行器判定）。 */
    parent = XWidget_create(NULL, 0);
    child = XLineEdit_create(parent, 0);
    XQ_REQUIRE(parent != NULL && child != NULL);
    XQ_CHECK(XWidget_parentWidget((XWidget*)child) == parent);
    XClassDelete((XClass*)parent);

    XClassDelete((XClass*)le);
}

XQ_TEST(XLineEdit, programmaticSetTextMarksEdited)
{
    XLineEdit* le = XLineEdit_create(NULL, 0);
    XFocusEvent* ev;
    XQ_REQUIRE(le != NULL);

    s_finished = 0;
    XObject_connect_1((XObject*)le,
                      (size_t)XLineEdit_editingFinished_signal(NULL),
                      (XObject*)le, xle_slotFinished,
                      XConnectionType_Direct);
    /* Qt QLineEditPrivate::setText 先置 edited=true（qlineedit_p.cpp:273-277）
       ⇒ 程序化 setText 后失焦会发 editingFinished（本机 Qt 6.8 探针 1 次）。 */
    XLineEdit_setText(le, "abc");
    ev = XFocusEvent_create(XEVENT_TYPE_FOCUS_OUT, XFocusReason_Other);
    XQ_REQUIRE(ev != NULL);
    (void)XCoreApplication_sendEvent((XObject*)le, (XEvent*)ev);
    XClassDelete((XClass*)ev);
    XQ_EQ_I64(s_finished, 1);

    /* 发射后 edited 复位：再失焦不重复发（Qt d->edited=false）。 */
    ev = XFocusEvent_create(XEVENT_TYPE_FOCUS_OUT, XFocusReason_Other);
    XQ_REQUIRE(ev != NULL);
    (void)XCoreApplication_sendEvent((XObject*)le, (XEvent*)ev);
    XClassDelete((XClass*)ev);
    XQ_EQ_I64(s_finished, 1);

    XClassDelete((XClass*)le);
}

XQ_TEST(XLineEdit, sameTextSetTextKeepsUndoAndCursor)
{
    XLineEdit* le = XLineEdit_create(NULL, 0);
    XQ_REQUIRE(le != NULL);

    /* 项目有意偏差（核实保留）：同文本 setText 早退。Qt 会清撤销史并把
       光标移到末尾（qlineedit_p.cpp:273-277；本机探针 undo 1→0、cursor
       2→5），但 HSF 设置页在换网卡/刷新后逐字段回填
       （HighSpeedFilter/client/ui/pages/HsfNetworkPage.c:844-906），若同值
       也清史/挪光标，用户正在编辑的字段会被弹到末尾且撤销不可用。见
       XLineEdit_setText 的实现注释与 gaps 的 intentional 记录。 */
    XLineEdit_setText(le, "hello");
    XLineEdit_setCursorPosition(le, 5);
    XLineEdit_insert(le, "X"); /* 产生一步撤销，文本 helloX */
    XQ_CHECK(XLineEdit_isUndoAvailable(le));
    XLineEdit_setCursorPosition(le, 2);
    XLineEdit_setText(le, "helloX"); /* 同文本 */
    XQ_EQ_STR(XLineEdit_text(le), "helloX");
    XQ_CHECK_MSG(XLineEdit_isUndoAvailable(le),
                 "同值 setText 保留撤销史（项目有意偏差，Qt 会清史）");
    XQ_EQ_I64(XLineEdit_cursorPosition(le), 2);

    XClassDelete((XClass*)le);
}

XQ_TEST(XLineEdit, dragAndDropText)
{
    XLineEdit* le = XLineEdit_create(NULL, 0);
    XString* mime;
    XString* data;
    XPoint pos;
    XPoint gpos;
    XDropEvent* drop;
    XQ_REQUIRE(le != NULL);

    XLineEdit_setText(le, "abcd");
    XLineEdit_setCursorPosition(le, 4);
    XPoint_init(&pos, 0, 0);
    XPoint_init(&gpos, 0, 0);
    mime = XString_create_utf8("text/plain");
    data = XString_create_utf8("XY");
    XQ_REQUIRE(mime != NULL && data != NULL);
    drop = XDropEvent_create(XEVENT_TYPE_DROP, &pos, &gpos, mime, data);
    XQ_REQUIRE(drop != NULL);
    XClassDelete((XClass*)mime);
    XClassDelete((XClass*)data);
    /* 落点 x=0 ⇒ 文本区起点，插入 "XY"（qlineedit.cpp:2136-2168 在落点
       moveCursor 后 insert）。 */
    (void)XCoreApplication_sendEvent((XObject*)le, (XEvent*)drop);
    XClassDelete((XClass*)drop);
    XQ_EQ_STR(XLineEdit_text(le), "XYabcd");

    /* 只读：dropEvent 忽略，文本不变（qlineedit.cpp:2141 的 isReadOnly 分支）。 */
    XLineEdit_setReadOnly(le, true);
    mime = XString_create_utf8("text/plain");
    data = XString_create_utf8("ZZ");
    XQ_REQUIRE(mime != NULL && data != NULL);
    drop = XDropEvent_create(XEVENT_TYPE_DROP, &pos, &gpos, mime, data);
    XQ_REQUIRE(drop != NULL);
    XClassDelete((XClass*)mime);
    XClassDelete((XClass*)data);
    (void)XCoreApplication_sendEvent((XObject*)le, (XEvent*)drop);
    XClassDelete((XClass*)drop);
    XQ_EQ_STR(XLineEdit_text(le), "XYabcd");

    XClassDelete((XClass*)le);
}

XQ_TEST(XLineEdit, inputMethodQueryWithArgument)
{
    XLineEdit* le = XLineEdit_create(NULL, 0);
    XVariant* arg;
    XVariant* var;
    XPointF pt;
    XQ_REQUIRE(le != NULL);

    XLineEdit_setText(le, "abcdef");
    XLineEdit_setCursorPosition(le, 4);

    /* 无参数：等价单参查询（ImCursorPosition 返回当前光标字节偏移）。 */
    var = XLineEdit_inputMethodQuery_2(le,
                                       (int)XInputMethodQuery_ImCursorPosition,
                                       NULL);
    XQ_REQUIRE(var != NULL);
    XQ_EQ_I64(XVariant_toInt32(var), 4);
    XClassDelete((XClass*)var);

    /* 带落点：ImCursorPosition 取点位置而非当前光标
       （qlineedit.cpp:1854-1865）。x 越界钳到文末 ⇒ 6。 */
    pt.x = 10000.0f;
    pt.y = 0.0f;
    arg = XVariant_create(&pt, sizeof(pt), XVariantType_User);
    XQ_REQUIRE(arg != NULL);
    var = XLineEdit_inputMethodQuery_2(le,
                                       (int)XInputMethodQuery_ImCursorPosition,
                                       arg);
    XQ_REQUIRE(var != NULL);
    XQ_EQ_I64(XVariant_toInt32(var), 6);
    XClassDelete((XClass*)var);

    /* ImTextAfterCursor 带点：点在文末 ⇒ 空（NULL 等价无效 QVariant）。 */
    var = XLineEdit_inputMethodQuery_2(le,
                                       (int)XInputMethodQuery_ImTextAfterCursor,
                                       arg);
    XQ_CHECK(var == NULL);

    /* 点在文首 ⇒ 后文为整串。 */
    pt.x = 0.0f;
    pt.y = 0.0f;
    XClassDelete((XClass*)arg);
    arg = XVariant_create(&pt, sizeof(pt), XVariantType_User);
    XQ_REQUIRE(arg != NULL);
    var = XLineEdit_inputMethodQuery_2(le,
                                       (int)XInputMethodQuery_ImTextAfterCursor,
                                       arg);
    XQ_REQUIRE(var != NULL);
    {
        const XString* text = XString_fromVariant_const(var);
        XQ_CHECK(text != NULL && strcmp(XString_toUtf8(text), "abcdef") == 0);
    }
    XClassDelete((XClass*)var);
    XClassDelete((XClass*)arg);

    XClassDelete((XClass*)le);
}

#endif /* XWIDGET_ON && XLINEEDIT_ON */
