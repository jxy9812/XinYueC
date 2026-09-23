/******************************************************************************
 * @file       xgui_demo_apitest_input.c
 * @brief      控件 API 测试族：input（输入族）。
 * @details    覆盖 XLineEdit / XAbstractSpinBox / XSpinBox / XDateTimeEdit /
 *             XComboBox / XFontComboBox 六个控件类的公开 API（对标
 *             Qt 6.8.3 同名控件）：
 *             - 属性 setter/getter 往返一致（text/placeholder/maxLength/
 *               readOnly/echoMode、min/max/step/wrapping/prefix/suffix、
 *               dateTime range/section/displayFormat、editable/currentIndex/
 *               条目管理、filters/writingSystem/family）；
 *             - Qt 6.8.3 对齐默认值（文档依据确定的直接断言；XGui 与 Qt
 *               存在裁剪差异处按头文件口径断言并在注释中标注差异；无依
 *               据的写注释不硬断言防误报）；
 *             - 信号发射与状态迁移（textChanged/textEdited/inputRejected/
 *               valueChanged/dateTimeChanged 三信号/userDateChanged/
 *               currentIndexChanged/popupShown 等经 XObject_event_base 直
 *               发合成键盘/滚轮事件或槽调用触发，与真实输入同路径）；
 *             - 边界（空串/NULL/0/极大值/重复 set/越界钳位/未 show 直接
 *               调 API）。
 *             全套件无头运行：所有控件栈上构造、从不 show（契约「控件不
 *             show 也可调绝大多数 API」口径）；键盘/滚轮注入坐标为控件
 *             本地坐标。
 * @note       文件所有权：仅本翻译单元，不改契约头、主文件与 Src/。
 * @author     XinYueC 团队
 ******************************************************************************/
#include <stdio.h>
#include <string.h>

#include "xgui_demo_apitest.h"

#include "XObject.h"
#include "XEvent.h"
#include "XVarList.h"
#include "XMemory.h"

#if XWIDGET_ON && XLINEEDIT_ON
#include "XLineEdit.h"
#include "XCompleter.h"
#include "XAbstractItemModel.h"
#include "XAlignment.h"
#include "XWindowEvent.h" /* XWheelEvent（滚轮步进注入）。 */
#endif

#if XWIDGET_ON && XLINEEDIT_ON && XABSTRACTSPINBOX_ON
#include "XAbstractSpinBox.h"
#endif

#if XWIDGET_ON && XSPINBOX_ON && XLINEEDIT_ON && XABSTRACTSPINBOX_ON
#include "XSpinBox.h"
#endif

#if XWIDGET_ON && XABSTRACTSPINBOX_ON && XDATETIMEEDIT_ON
#include "XDateTimeEdit.h"
#include "XDateTime.h"
#endif

#if XWIDGET_ON && XCOMBOBOX_ON && XLINEEDIT_ON
#include "XComboBox.h"
#endif

#if XWIDGET_ON && XCOMBOBOX_ON && XFONTCOMBOBOX_ON
#include "XFontComboBox.h"
#include "XFont.h"
#endif

/* ==================== 共享：合成事件注入（与真实输入同路径） ==================== */

#if XWIDGET_ON && XLINEEDIT_ON
/** @brief 向目标控件直发一次合成键盘按下（XObject_event_base，对标
 *         QTEST_KEY_CLICK 的真事件路径；键码为 XKey/ASCII 码位）。 */
static void input_injectKey(XWidget* target, int key,
                            XKeyboardModifiers modifiers)
{
    XKeyEvent ke;
    if (!target) return;
    XKeyEvent_init(&ke, XEVENT_TYPE_KEY_PRESS, key, modifiers);
    XObject_event_base((XObject*)target, (XEvent*)&ke);
}

/** @brief 向目标控件直发一次滚轮事件（垂直角度增量 dy，120=1 步）。 */
static void input_injectWheel(XWidget* target, int dy)
{
    XWheelEvent we;
    XPoint pos;
    XPoint gpos;
    XPoint delta;
    if (!target) return;
    XPoint_init(&pos, 5, 5);
    XPoint_init(&gpos, 50, 50);
    XPoint_init(&delta, 0, dy);
    XWheelEvent_init(&we, XEVENT_TYPE_WHEEL, &pos, &gpos, &delta,
                     XMouseButton_NoButton, 0);
    XObject_event_base((XObject*)target, (XEvent*)&we);
}

/** @brief 记录最近一次 const char* 信号载荷（截断拷贝，防悬垂）。 */
static void input_copyText(char* dst, size_t cap, const char* src)
{
    if (!dst || cap == 0) return;
    if (!src) src = "";
    snprintf(dst, cap, "%s", src);
}
#endif /* XWIDGET_ON && XLINEEDIT_ON */

/* ==================== 信号记录器（对标 QSignalSpy 的最小等价物） ==================== */

#if XWIDGET_ON && XLINEEDIT_ON
/** @brief XLineEdit 七信号计数与最近文本载荷。 */
typedef struct LeSigRec
{
    int textChanged;        /**< textChanged 次数。 */
    int textEdited;         /**< textEdited 次数（仅用户编辑路径）。 */
    int cursorMoved;        /**< cursorPositionChanged 次数。 */
    int returnPressed;      /**< returnPressed 次数。 */
    int editingFinished;    /**< editingFinished 次数。 */
    int selectionChanged;   /**< selectionChanged 次数。 */
    int inputRejected;      /**< inputRejected 次数（掩码/长度/校验拒绝）。 */
    char lastText[64];      /**< 最近一次 textChanged 载荷拷贝。 */
} LeSigRec;

static LeSigRec g_leSig;

static void input_leReset(void) { memset(&g_leSig, 0, sizeof(g_leSig)); }

static void input_leTextChangedSlot(XObject* receiver, XVarList* args)
{
    (void)receiver;
    XVarList_args_1(args, const char*, t);
    ++g_leSig.textChanged;
    input_copyText(g_leSig.lastText, sizeof(g_leSig.lastText), t);
}

static void input_leTextEditedSlot(XObject* receiver, XVarList* args)
{
    (void)receiver; (void)args;
    ++g_leSig.textEdited;
}

static void input_leCursorMovedSlot(XObject* receiver, XVarList* args)
{
    (void)receiver;
    XVarList_args_2(args, int, oldPos, int, newPos);
    (void)oldPos; (void)newPos;
    ++g_leSig.cursorMoved;
}

static void input_leReturnPressedSlot(XObject* receiver, XVarList* args)
{
    (void)receiver; (void)args;
    ++g_leSig.returnPressed;
}

static void input_leEditingFinishedSlot(XObject* receiver, XVarList* args)
{
    (void)receiver; (void)args;
    ++g_leSig.editingFinished;
}

static void input_leSelectionChangedSlot(XObject* receiver, XVarList* args)
{
    (void)receiver; (void)args;
    ++g_leSig.selectionChanged;
}

static void input_leInputRejectedSlot(XObject* receiver, XVarList* args)
{
    (void)receiver; (void)args;
    ++g_leSig.inputRejected;
}

/** @brief XLineEdit 七信号统一装配（sender==receiver 自连定式）。 */
static void input_leConnect(XLineEdit* le)
{
    XObject* obj = (XObject*)le;
    XObject_connect_1(obj, (size_t)XLineEdit_textChanged_signal(NULL),
                      obj, input_leTextChangedSlot, XConnectionType_Direct);
    XObject_connect_1(obj, (size_t)XLineEdit_textEdited_signal(NULL),
                      obj, input_leTextEditedSlot, XConnectionType_Direct);
    XObject_connect_1(obj, (size_t)XLineEdit_cursorPositionChanged_signal(
                                NULL, 0, 0),
                      obj, input_leCursorMovedSlot, XConnectionType_Direct);
    XObject_connect_1(obj, (size_t)XLineEdit_returnPressed_signal(NULL),
                      obj, input_leReturnPressedSlot, XConnectionType_Direct);
    XObject_connect_1(obj, (size_t)XLineEdit_editingFinished_signal(NULL),
                      obj, input_leEditingFinishedSlot,
                      XConnectionType_Direct);
    XObject_connect_1(obj, (size_t)XLineEdit_selectionChanged_signal(NULL),
                      obj, input_leSelectionChangedSlot,
                      XConnectionType_Direct);
    XObject_connect_1(obj, (size_t)XLineEdit_inputRejected_signal(NULL),
                      obj, input_leInputRejectedSlot, XConnectionType_Direct);
}

/** @brief 校验器样例：首字符为 '9' 的文本判 Invalid（对标
 *         QValidator::validate 返回 Invalid 的拒绝语义）。 */
static XLineEditValidatorState input_rejectNineValidator(
    XLineEdit* self, const char* text, void* userData)
{
    (void)self; (void)userData;
    if (text && text[0] == '9') return XLineEditValidatorState_Invalid;
    return XLineEditValidatorState_Acceptable;
}
#endif /* XWIDGET_ON && XLINEEDIT_ON */

#if XWIDGET_ON && XSPINBOX_ON && XLINEEDIT_ON && XABSTRACTSPINBOX_ON
/** @brief XSpinBox valueChanged/textChanged 计数与最近载荷。 */
typedef struct SpinSigRec
{
    int  valueChanged;      /**< valueChanged(int) 次数。 */
    int  textChanged;       /**< textChanged(const char*) 次数。 */
    int  lastValue;         /**< 最近一次 valueChanged 载荷。 */
    char lastText[64];      /**< 最近一次 textChanged 载荷拷贝。 */
} SpinSigRec;

static SpinSigRec g_spinSig;

static void input_spinReset(void)
{
    memset(&g_spinSig, 0, sizeof(g_spinSig));
    g_spinSig.lastValue = -1;
}

static void input_spinValueChangedSlot(XObject* receiver, XVarList* args)
{
    (void)receiver;
    XVarList_args_1(args, int, v);
    ++g_spinSig.valueChanged;
    g_spinSig.lastValue = v;
}

static void input_spinTextChangedSlot(XObject* receiver, XVarList* args)
{
    (void)receiver;
    XVarList_args_1(args, const char*, t);
    ++g_spinSig.textChanged;
    input_copyText(g_spinSig.lastText, sizeof(g_spinSig.lastText), t);
}

static void input_spinConnect(XSpinBox* spin)
{
    XObject* obj = (XObject*)spin;
    XObject_connect_1(obj, (size_t)XSpinBox_valueChanged_signal(NULL),
                      obj, input_spinValueChangedSlot,
                      XConnectionType_Direct);
    XObject_connect_1(obj, (size_t)XSpinBox_textChanged_signal(NULL),
                      obj, input_spinTextChangedSlot, XConnectionType_Direct);
}
#endif /* XWIDGET_ON && XSPINBOX_ON && XLINEEDIT_ON && XABSTRACTSPINBOX_ON */

#if XWIDGET_ON && XABSTRACTSPINBOX_ON && XDATETIMEEDIT_ON
/** @brief XDateTimeEdit 五信号计数。 */
typedef struct DtSigRec
{
    int dateTimeChanged;    /**< dateTimeChanged 次数。 */
    int dateChanged;        /**< dateChanged 次数（日期部分实际变化）。 */
    int timeChanged;        /**< timeChanged 次数（时间部分实际变化）。 */
    int userDateChanged;    /**< userDateChanged 次数（仅用户步进路径）。 */
    int userTimeChanged;    /**< userTimeChanged 次数（仅用户步进路径）。 */
} DtSigRec;

static DtSigRec g_dtSig;

static void input_dtReset(void) { memset(&g_dtSig, 0, sizeof(g_dtSig)); }

static void input_dtDateTimeChangedSlot(XObject* receiver, XVarList* args)
{
    (void)receiver;
    XVarList_args_1(args, XDateTime*, dt);
    (void)dt;
    ++g_dtSig.dateTimeChanged;
}

static void input_dtDateChangedSlot(XObject* receiver, XVarList* args)
{
    (void)receiver;
    XVarList_args_1(args, XDate*, d);
    (void)d;
    ++g_dtSig.dateChanged;
}

static void input_dtTimeChangedSlot(XObject* receiver, XVarList* args)
{
    (void)receiver;
    XVarList_args_1(args, XTime*, t);
    (void)t;
    ++g_dtSig.timeChanged;
}

static void input_dtUserDateChangedSlot(XObject* receiver, XVarList* args)
{
    (void)receiver;
    XVarList_args_1(args, XDate*, d);
    (void)d;
    ++g_dtSig.userDateChanged;
}

static void input_dtUserTimeChangedSlot(XObject* receiver, XVarList* args)
{
    (void)receiver;
    XVarList_args_1(args, XTime*, t);
    (void)t;
    ++g_dtSig.userTimeChanged;
}

static void input_dtConnect(XDateTimeEdit* dt)
{
    XObject* obj = (XObject*)dt;
    XObject_connect_1(obj, (size_t)XDateTimeEdit_dateTimeChanged_signal(
                                NULL, NULL),
                      obj, input_dtDateTimeChangedSlot,
                      XConnectionType_Direct);
    XObject_connect_1(obj, (size_t)XDateTimeEdit_dateChanged_signal(NULL,
                                                                    NULL),
                      obj, input_dtDateChangedSlot, XConnectionType_Direct);
    XObject_connect_1(obj, (size_t)XDateTimeEdit_timeChanged_signal(NULL,
                                                                    NULL),
                      obj, input_dtTimeChangedSlot, XConnectionType_Direct);
    XObject_connect_1(obj, (size_t)XDateTimeEdit_userDateChanged_signal(
                                NULL, NULL),
                      obj, input_dtUserDateChangedSlot,
                      XConnectionType_Direct);
    XObject_connect_1(obj, (size_t)XDateTimeEdit_userTimeChanged_signal(
                                NULL, NULL),
                      obj, input_dtUserTimeChangedSlot,
                      XConnectionType_Direct);
}
#endif /* XWIDGET_ON && XABSTRACTSPINBOX_ON && XDATETIMEEDIT_ON */

#if XWIDGET_ON && XCOMBOBOX_ON && XLINEEDIT_ON
/** @brief XComboBox 信号计数与最近载荷。 */
typedef struct CbSigRec
{
    int  indexChanged;      /**< currentIndexChanged 次数。 */
    int  lastIndex;         /**< 最近一次 currentIndexChanged 载荷。 */
    int  textChangedCount;  /**< currentTextChanged 次数。 */
    char lastText[64];      /**< 最近一次 currentTextChanged 载荷拷贝。 */
    int  editChanged;       /**< editTextChanged 次数。 */
    char lastEdit[64];      /**< 最近一次 editTextChanged 载荷拷贝。 */
    int  popupShown;        /**< popupShown 次数。 */
    int  popupHidden;       /**< popupHidden 次数。 */
} CbSigRec;

static CbSigRec g_cbSig;

static void input_cbReset(void)
{
    memset(&g_cbSig, 0, sizeof(g_cbSig));
    g_cbSig.lastIndex = -1;
}

static void input_cbIndexChangedSlot(XObject* receiver, XVarList* args)
{
    (void)receiver;
    XVarList_args_1(args, int, idx);
    ++g_cbSig.indexChanged;
    g_cbSig.lastIndex = idx;
}

static void input_cbTextChangedSlot(XObject* receiver, XVarList* args)
{
    (void)receiver;
    XVarList_args_1(args, const char*, t);
    ++g_cbSig.textChangedCount;
    input_copyText(g_cbSig.lastText, sizeof(g_cbSig.lastText), t);
}

static void input_cbEditChangedSlot(XObject* receiver, XVarList* args)
{
    (void)receiver;
    XVarList_args_1(args, const char*, t);
    ++g_cbSig.editChanged;
    input_copyText(g_cbSig.lastEdit, sizeof(g_cbSig.lastEdit), t);
}

static void input_cbPopupShownSlot(XObject* receiver, XVarList* args)
{
    (void)receiver; (void)args;
    ++g_cbSig.popupShown;
}

static void input_cbPopupHiddenSlot(XObject* receiver, XVarList* args)
{
    (void)receiver; (void)args;
    ++g_cbSig.popupHidden;
}

static void input_cbConnect(XComboBox* cb)
{
    XObject* obj = (XObject*)cb;
    XObject_connect_1(obj, (size_t)XComboBox_currentIndexChanged_signal(
                                NULL, 0),
                      obj, input_cbIndexChangedSlot, XConnectionType_Direct);
    XObject_connect_1(obj, (size_t)XComboBox_currentTextChanged_signal(
                                NULL, NULL),
                      obj, input_cbTextChangedSlot, XConnectionType_Direct);
    XObject_connect_1(obj, (size_t)XComboBox_editTextChanged_signal(
                                NULL, NULL),
                      obj, input_cbEditChangedSlot, XConnectionType_Direct);
    XObject_connect_1(obj, (size_t)XComboBox_popupShown_signal(NULL),
                      obj, input_cbPopupShownSlot, XConnectionType_Direct);
    XObject_connect_1(obj, (size_t)XComboBox_popupHidden_signal(NULL),
                      obj, input_cbPopupHiddenSlot, XConnectionType_Direct);
}
#endif /* XWIDGET_ON && XCOMBOBOX_ON && XLINEEDIT_ON */

#if XWIDGET_ON && XCOMBOBOX_ON && XFONTCOMBOBOX_ON
/** @brief XFontComboBox currentFontChanged 计数与最近族名。 */
static int  g_fcbFontChanged;
static char g_fcbLastFamily[64];

static void input_fcbReset(void)
{
    g_fcbFontChanged = 0;
    g_fcbLastFamily[0] = '\0';
}

static void input_fcbFontChangedSlot(XObject* receiver, XVarList* args)
{
    (void)receiver;
    XVarList_args_1(args, const char*, family);
    ++g_fcbFontChanged;
    input_copyText(g_fcbLastFamily, sizeof(g_fcbLastFamily), family);
}
#endif /* XWIDGET_ON && XCOMBOBOX_ON && XFONTCOMBOBOX_ON */

/* ==================== 入口 ==================== */

int xapi_input_run(void)
{
    int failures = 0;

#if XWIDGET_ON && XLINEEDIT_ON

    /* ================================================================
     * 1. XLineEdit：单行编辑（对标 Qt 6.8 QLineEdit public API）。
     * ================================================================ */
    {
        XLineEdit le;
        XMargins mg;
        const char* t0;
        XSize sz;
        XSize msz;

        XLineEdit_init(&le, NULL, 0);
        input_leConnect(&le);
        input_leReset();

        /* ---- 默认值（Qt 6.8 QLineEdit 构造默认 + XGui 裁剪口径） ---- */
        XAPI_EXPECT(xapi_cstr(XLineEdit_text(&le))[0] == '\0',
                    "LineEdit 默认文本=空串（QLineEdit::text 默认空）");
        XAPI_EXPECT(XLineEdit_maxLength(&le) == 0,
                    "LineEdit 默认 maxLength=0 不限长（XGui 以 0 承载不限，Qt 默认 32767 上限——头文件注明的裁剪口径）");
        XAPI_EXPECT(!XLineEdit_isReadOnly(&le),
                    "LineEdit 默认可编辑（QLineEdit::isReadOnly 默认 false）");
        XAPI_EXPECT(XLineEdit_echoMode(&le) ==
                    (int)XLineEditEchoMode_Normal,
                    "LineEdit 默认回显 Normal（QLineEdit::EchoMode 默认 Normal）");
        XAPI_EXPECT(XLineEdit_hasFrame(&le),
                    "LineEdit 默认绘制边框（QLineEdit::frame 默认 true）");
        XAPI_EXPECT(!XLineEdit_isClearButtonEnabled(&le),
                    "LineEdit 默认无清除按钮（clearButtonEnabled 默认 false）");
        XAPI_EXPECT(xapi_cstr(XLineEdit_placeholderText(&le))[0] == '\0',
                    "LineEdit 默认占位文本=空串");
        XAPI_EXPECT(XLineEdit_cursorMoveStyle(&le) ==
                    (int)XLineEditCursorMoveStyle_LogicalMoveStyle,
                    "LineEdit 默认逻辑光标移动风格（Qt::LogicalMoveStyle 默认）");
        XAPI_EXPECT(!XLineEdit_isModified(&le),
                    "LineEdit 默认未被修改（isModified 默认 false）");
        XAPI_EXPECT(!XLineEdit_isUndoAvailable(&le) &&
                    !XLineEdit_isRedoAvailable(&le),
                    "LineEdit 默认撤销/重做栈空（undoAvailable 默认 false）");
        XAPI_EXPECT(XLineEdit_alignment(&le) == (int)XAlignment_Left,
                    "LineEdit 默认水平左对齐（alignment 默认 AlignLeft）");
        XAPI_EXPECT(XLineEdit_selectionStart(&le) == -1 &&
                    XLineEdit_selectionEnd(&le) == -1 &&
                    XLineEdit_selectionLength(&le) == 0 &&
                    !XLineEdit_hasSelectedText(&le),
                    "LineEdit 默认无选区（selectionStart 默认 -1）");

        /* ---- text/setText 往返（QLineEdit::setText/text） ---- */
        XLineEdit_setText(&le, "hello");
        XAPI_EXPECT(strcmp(xapi_cstr(XLineEdit_text(&le)), "hello") == 0,
                    "LineEdit setText/text 往返=hello");
        t0 = XLineEdit_text(&le);
        XLineEdit_setText(&le, "hello");
        XAPI_EXPECT(XLineEdit_text(&le) == t0,
                    "LineEdit 相同文本 setText 为无操作（指针稳定）");
        XLineEdit_setText(&le, NULL);
        XAPI_EXPECT(strcmp(xapi_cstr(XLineEdit_text(&le)), "hello") == 0,
                    "LineEdit setText(NULL) 忽略保持原文（Qt 空串清空；XGui 头文件注明 NULL 忽略）");

        /* ---- displayText 回显状态机（QLineEdit::displayText） ---- */
        XLineEdit_setText(&le, "secret");
        XAPI_EXPECT(strcmp(xapi_cstr(XLineEdit_displayText(&le)), "secret") == 0,
                    "LineEdit Normal 回显=原文");
        XLineEdit_setEchoMode(&le, (int)XLineEditEchoMode_Password);
        XAPI_EXPECT(XLineEdit_echoMode(&le) ==
                    (int)XLineEditEchoMode_Password &&
                    strcmp(xapi_cstr(XLineEdit_displayText(&le)), "******") == 0,
                    "LineEdit Password 回显逐字符 '*' 且 text 不变");
        XLineEdit_setEchoMode(&le, 99);
        XAPI_EXPECT(XLineEdit_echoMode(&le) ==
                    (int)XLineEditEchoMode_Password,
                    "LineEdit 非法 echoMode(99) 忽略保持原模式");
        XLineEdit_setEchoMode(&le, (int)XLineEditEchoMode_NoEcho);
        XAPI_EXPECT(xapi_cstr(XLineEdit_displayText(&le))[0] == '\0' &&
                    strcmp(xapi_cstr(XLineEdit_text(&le)), "secret") == 0,
                    "LineEdit NoEcho 显示空但 text 保持（Qt NoEcho 语义）");
        XLineEdit_setEchoMode(&le, (int)XLineEditEchoMode_PasswordEchoOnEdit);
        XAPI_EXPECT(XLineEdit_echoMode(&le) ==
                    (int)XLineEditEchoMode_PasswordEchoOnEdit,
                    "LineEdit PasswordEchoOnEdit 往返（数值 3 对齐 Qt）");
        XLineEdit_setEchoMode(&le, (int)XLineEditEchoMode_Normal);

        /* ---- maxLength 钳位（QLineEdit::setMaxLength 截断现有文本） ---- */
        input_leReset();
        XLineEdit_setText(&le, "abcdef");
        XLineEdit_setMaxLength(&le, 3);
        XAPI_EXPECT(XLineEdit_maxLength(&le) == 3 &&
                    strcmp(xapi_cstr(XLineEdit_text(&le)), "abc") == 0,
                    "LineEdit setMaxLength(3) 截断现有文本并发射 textChanged");
        XAPI_EXPECT(g_leSig.textChanged == 2,
                    "LineEdit 截断路径 textChanged 恰发射 2 次（setText+截断）");
        XLineEdit_setMaxLength(&le, -1);
        XAPI_EXPECT(XLineEdit_maxLength(&le) == 3,
                    "LineEdit 负 maxLength 忽略保持原值");
        XLineEdit_setMaxLength(&le, 0);
        XAPI_EXPECT(XLineEdit_maxLength(&le) == 0,
                    "LineEdit maxLength=0 恢复不限长");

        /* ---- insert 用户编辑语义（QLineEdit::insert） ---- */
        input_leReset();
        XLineEdit_setText(&le, "abc");
        XLineEdit_setMaxLength(&le, 5);
        XLineEdit_insert(&le, "de");
        XAPI_EXPECT(strcmp(xapi_cstr(XLineEdit_text(&le)), "abcde") == 0,
                    "LineEdit insert 在光标处插入");
        XAPI_EXPECT(g_leSig.textEdited == 1 && g_leSig.textChanged == 1,
                    "LineEdit insert 视为用户编辑：textEdited+textChanged 各一次");
        XAPI_EXPECT(XLineEdit_isModified(&le),
                    "LineEdit insert 置 modified（Qt isModified 语义）");
        XLineEdit_insert(&le, "f");
        XAPI_EXPECT(strcmp(xapi_cstr(XLineEdit_text(&le)), "abcde") == 0 &&
                    g_leSig.inputRejected == 1,
                    "LineEdit 超 maxLength 插入被整体拒绝并发射 inputRejected");
        XLineEdit_setModified(&le, false);
        XAPI_EXPECT(!XLineEdit_isModified(&le),
                    "LineEdit setModified(false) 复位（Qt setModified）");
        XLineEdit_setText(&le, "x");
        XAPI_EXPECT(!XLineEdit_isModified(&le),
                    "LineEdit 程序化 setText 不改变 modified（Qt 文档语义）");
        /* 恢复不限长（0=XGui 不限长口径，QLineEdit 默认 32767 不限）：
         * insert 段 setMaxLength(5) 若不复位，Qt 语义下 setText 对超长
         * 文本按 maxLength 截断（QLineEdit::setMaxLength/setText 文档），
         * 后续光标/选区段的 6/11 字符断言全部失真（首版漏复位为状态
         * 泄漏，非框架缺陷——截断行为本身与 Qt 一致）。 */
        XLineEdit_setMaxLength(&le, 0);

        /* ---- 撤销/重做（QLineEdit::undo/redo） ---- */
        XLineEdit_setText(&le, "abc");
        XAPI_EXPECT(!XLineEdit_isUndoAvailable(&le),
                    "LineEdit setText 清空撤销历史（Qt setText 语义）");
        XLineEdit_insert(&le, "d");
        XAPI_EXPECT(XLineEdit_isUndoAvailable(&le),
                    "LineEdit 用户编辑后 undoAvailable=true");
        XLineEdit_undo(&le);
        XAPI_EXPECT(strcmp(xapi_cstr(XLineEdit_text(&le)), "abc") == 0,
                    "LineEdit undo 恢复编辑前快照");
        XAPI_EXPECT(XLineEdit_isRedoAvailable(&le),
                    "LineEdit undo 后 redoAvailable=true");
        XLineEdit_redo(&le);
        XAPI_EXPECT(strcmp(xapi_cstr(XLineEdit_text(&le)), "abcd") == 0,
                    "LineEdit redo 恢复被撤销文本");

        /* ---- 光标（QLineEdit::setCursorPosition/cursorForward/home/end） ---- */
        XLineEdit_setText(&le, "abcdef");
        XAPI_EXPECT(XLineEdit_cursorPosition(&le) == 6,
                    "LineEdit setText 后光标在末尾（Qt setText 语义）");
        XLineEdit_setCursorPosition(&le, 6);
        input_leReset();
        XLineEdit_setCursorPosition(&le, 2);
        XAPI_EXPECT(XLineEdit_cursorPosition(&le) == 2 &&
                    g_leSig.cursorMoved == 1,
                    "LineEdit setCursorPosition 移动发射 cursorPositionChanged");
        XLineEdit_setCursorPosition(&le, 100);
        XAPI_EXPECT(XLineEdit_cursorPosition(&le) == 6,
                    "LineEdit 光标超界钳位到字符数（Qt 钳位语义）");
        XLineEdit_setCursorPosition(&le, -3);
        XAPI_EXPECT(XLineEdit_cursorPosition(&le) == 0,
                    "LineEdit 负光标按 0（Qt 钳位语义）");
        XLineEdit_cursorForward(&le, false, 2);
        XAPI_EXPECT(XLineEdit_cursorPosition(&le) == 2,
                    "LineEdit cursorForward(2) 右移且清选区");
        XLineEdit_cursorBackward(&le, false, 1);
        XAPI_EXPECT(XLineEdit_cursorPosition(&le) == 1,
                    "LineEdit cursorBackward(1) 左移");
        XLineEdit_end(&le, false);
        XAPI_EXPECT(XLineEdit_cursorPosition(&le) == 6,
                    "LineEdit end 到行尾");
        XLineEdit_home(&le, false);
        XAPI_EXPECT(XLineEdit_cursorPosition(&le) == 0,
                    "LineEdit home 到行首");
        XLineEdit_setText(&le, "hello world");
        XLineEdit_home(&le, false);
        XLineEdit_cursorWordForward(&le, false);
        XAPI_EXPECT(XLineEdit_cursorPosition(&le) == 6,
                    "LineEdit cursorWordForward 跳到下一词首（跳过分隔符）");
        XLineEdit_end(&le, false);
        XLineEdit_cursorWordBackward(&le, false);
        XAPI_EXPECT(XLineEdit_cursorPosition(&le) == 6,
                    "LineEdit cursorWordBackward 跳到当前词首");

        /* ---- 选区（QLineEdit::setSelection/selectionStart 族） ---- */
        XLineEdit_setText(&le, "abcdef");
        XLineEdit_setSelection(&le, 1, 3);
        XAPI_EXPECT(XLineEdit_hasSelectedText(&le),
                    "LineEdit setSelection 建立选区");
        XAPI_EXPECT(XLineEdit_selectionStart(&le) == 1,
                    "LineEdit selectionStart=1");
        XAPI_EXPECT(XLineEdit_selectionLength(&le) == 3,
                    "LineEdit selectionLength=3");
        XAPI_EXPECT(XLineEdit_selectionEnd(&le) == 4,
                    "LineEdit selectionEnd=start+len（Qt exclusive 端点；ASCII 下字符索引=字节偏移）");
        {
            char* sel = XLineEdit_selectedText(&le);
            XAPI_EXPECT(sel != NULL && strcmp(sel, "bcd") == 0,
                        "LineEdit selectedText 堆拷贝=「bcd」");
            XFree_System(sel);
        }
        XLineEdit_setSelection(&le, 3, -2);
        XAPI_EXPECT(XLineEdit_selectionStart(&le) == 1 &&
                    XLineEdit_selectionLength(&le) == 2,
                    "LineEdit 负 length 向 start 左侧扩展（Qt 语义）");
        input_leReset();
        XLineEdit_deselect(&le);
        XAPI_EXPECT(!XLineEdit_hasSelectedText(&le) &&
                    XLineEdit_selectionStart(&le) == -1,
                    "LineEdit deselect 清选区且 selectionStart=-1");
        XAPI_EXPECT(g_leSig.selectionChanged == 1,
                    "LineEdit 选区清除发射 selectionChanged");
        XLineEdit_selectAll(&le);
        XAPI_EXPECT(XLineEdit_selectionLength(&le) == 6,
                    "LineEdit selectAll 覆盖全文");

        /* ---- 校验器（QLineEdit::setValidator 的 C 适配 + inputRejected） ---- */
        XLineEdit_setText(&le, "");
        input_leReset();
        XLineEdit_setValidator(&le, input_rejectNineValidator, NULL);
        XAPI_EXPECT(XLineEdit_validator(&le) == input_rejectNineValidator,
                    "LineEdit setValidator/validator 往返");
        input_injectKey((XWidget*)&le, '9', 0);
        XAPI_EXPECT(xapi_cstr(XLineEdit_text(&le))[0] == '\0' &&
                    g_leSig.inputRejected == 1,
                    "LineEdit 键入被校验器判 Invalid：文本回滚并发射 inputRejected");
        input_injectKey((XWidget*)&le, '7', 0);
        XAPI_EXPECT(strcmp(xapi_cstr(XLineEdit_text(&le)), "7") == 0 &&
                    g_leSig.inputRejected == 1,
                    "LineEdit 合法键入通过校验器进入文本");
        XLineEdit_setValidator(&le, NULL, NULL);
        XAPI_EXPECT(XLineEdit_validator(&le) == NULL,
                    "LineEdit setValidator(NULL) 清除校验器");

        /* ---- 输入掩码（QLineEdit::setInputMask） ---- */
        XLineEdit_setInputMask(&le, "00.00");
        XAPI_EXPECT(strcmp(xapi_cstr(XLineEdit_inputMask(&le)), "00.00") == 0,
                    "LineEdit inputMask 设置往返");
        XLineEdit_setInputMask(&le, "");
        XAPI_EXPECT(xapi_cstr(XLineEdit_inputMask(&le))[0] == '\0',
                    "LineEdit 空串清除掩码（Qt setInputMask(\"\") 语义）");

        /* ---- hasAcceptableInput / clear（QLineEdit 同名 API） ---- */
        XLineEdit_setText(&le, "abc");
        XAPI_EXPECT(XLineEdit_hasAcceptableInput(&le),
                    "LineEdit 非空无掩码 hasAcceptableInput=true");
        input_leReset();
        XLineEdit_clear(&le);
        XAPI_EXPECT(xapi_cstr(XLineEdit_text(&le))[0] == '\0' &&
                    XLineEdit_hasAcceptableInput(&le),
                    "LineEdit clear 清空文本；无校验器/掩码时空文本即可接受（Qt hasAcceptableInput 语义，无约束时全输入可接受——实现口径见 XLineEdit.c 注释；首版断言空文本不可接受为编写期误判）");
        XAPI_EXPECT(g_leSig.textChanged == 1 &&
                    strcmp(g_leSig.lastText, "") == 0,
                    "LineEdit clear 发射 textChanged 且载荷为空串");

        /* ---- 键盘注入全路径（textEdited/modified/Return 族信号） ---- */
        input_leReset();
        input_injectKey((XWidget*)&le, 'a', 0);
        input_injectKey((XWidget*)&le, 'b', 0);
        XAPI_EXPECT(strcmp(xapi_cstr(XLineEdit_text(&le)), "ab") == 0,
                    "LineEdit 键入注入 a/b 进入文本（XObject_event_base 真事件路径）");
        XAPI_EXPECT(g_leSig.textEdited == 2 && g_leSig.textChanged == 2,
                    "LineEdit 键入同时发射 textEdited 与 textChanged");
        XAPI_EXPECT(XLineEdit_isModified(&le),
                    "LineEdit 键入置 modified");
        input_injectKey((XWidget*)&le, XKey_Backspace, 0);
        XAPI_EXPECT(strcmp(xapi_cstr(XLineEdit_text(&le)), "a") == 0,
                    "LineEdit Backspace 删除末字符（Qt backspace 语义）");
        input_injectKey((XWidget*)&le, XKey_Return, 0);
        XAPI_EXPECT(g_leSig.returnPressed == 1 &&
                    g_leSig.editingFinished == 1,
                    "LineEdit Return 发射 returnPressed + editingFinished（Qt Return 提交语义）");

        /* ---- readOnly（QLineEdit::setReadOnly） ---- */
        XLineEdit_setText(&le, "keep");
        input_leReset();
        XLineEdit_setReadOnly(&le, true);
        XAPI_EXPECT(XLineEdit_isReadOnly(&le),
                    "LineEdit setReadOnly(true) 往返");
        XLineEdit_insert(&le, "zz");
        XAPI_EXPECT(strcmp(xapi_cstr(XLineEdit_text(&le)), "keep") == 0 &&
                    g_leSig.textChanged == 0,
                    "LineEdit readOnly 拒绝 insert 不发 textChanged");
        XLineEdit_setReadOnly(&le, false);
        XAPI_EXPECT(!XLineEdit_isReadOnly(&le),
                    "LineEdit readOnly 恢复可编辑");

        /* ---- 补全器默认弹层（§8.0g19 追加）：setCompleter 安装后
         *      直发字符键事件 -> 前缀匹配 -> 内建弹层可见。 ---- */
        {
            XCompleter* comp = XCompleter_create((XObject*)&le);
            XAbstractItemModel* cmodel = XAbstractItemModel_create();
            XKeyEvent* kev;
            XWidget* popupView;
            XAPI_EXPECT(comp != NULL, "补全器堆构造成功");
            XAPI_EXPECT(cmodel != NULL, "补全词条模型创建成功");
            if (comp && cmodel) {
                XAbstractItemModel_setDimension(cmodel, 2, 1);
                XAbstractItemModel_setData_2(cmodel, 0, 0, "Open File");
                XAbstractItemModel_setData_2(cmodel, 1, 0, "Open Project");
                XCompleter_setModel(comp, cmodel);
                XLineEdit_setCompleter(&le, comp);
                XLineEdit_clear(&le);
                XAPI_EXPECT(XCompleter_widget(comp) == (XWidget*)&le,
                            "setCompleter 安装时回填 widget 借用");
                kev = XKeyEvent_create_ex(XCLASS_DEFAULT_MEMORY_TYPE,
                                          XEVENT_TYPE_KEY_PRESS,
                                          (int)XKey_O, 0);
                XAPI_EXPECT(kev != NULL, "补全键事件构造成功");
                if (kev) {
                    XObject_event_base((XObject*)&le, (XEvent*)kev);
                    XEvent_delete_base((XEvent*)kev);
                }
                XAPI_EXPECT(strcmp(xapi_cstr(XLineEdit_text(&le)), "O") == 0,
                            "直发 O 键插入字符");
                XAPI_EXPECT(XCompleter_completionCount(comp) >= 1,
                            "键入后前缀匹配产生候选");
                popupView = XCompleter_popup(comp);
                /* apitest 环境顶层窗口未 show：有效可见性恒假，改断
                   言「弹层已创建且条目数=候选数」（创建+重建证明）。 */
                XAPI_EXPECT(popupView != NULL,
                            "键入后内建默认弹层自动创建");
                XAPI_EXPECT(XListWidget_count((XListWidget*)popupView) ==
                                XCompleter_completionCount(comp),
                            "默认弹层条目数=候选数");
                /* 键盘导航：le 顶层先 show（否则子弹层有效可见性恒
                   假），弹层随之可见，直发 Down 驱动候选移动+回填。 */
                XWidget_show((XWidget*)&le);
                XAPI_EXPECT(XWidget_isVisible(popupView),
                            "弹层 show 后有效可见");
                {
                    XKeyEvent* down = XKeyEvent_create_ex(
                        XCLASS_DEFAULT_MEMORY_TYPE, XEVENT_TYPE_KEY_PRESS,
                        (int)XKey_Down, 0);
                    if (down) {
                        XObject_event_base((XObject*)&le, (XEvent*)down);
                        XEvent_delete_base((XEvent*)down);
                    }
                }
                XAPI_EXPECT(strcmp(xapi_cstr(XLineEdit_text(&le)),
                                   "Open Project") == 0,
                            "弹层可见时 Down 回填下一候选文本");
                XAPI_EXPECT(XCompleter_currentRow(comp) == 1,
                            "Down 后 currentRow=1");
                XCompleter_setCompletionPrefix_2(comp, "");
                XCompleter_hidePopup(comp);
                XLineEdit_setCompleter(&le, NULL);
            }
            if (cmodel) XAbstractItemModel_delete_base((XClass*)cmodel);
            if (comp) XCompleter_delete_base((XClass*)comp);
        }

        /* ---- placeholder/margins/alignment/frame 等属性往返 ---- */
        XLineEdit_setPlaceholderText(&le, "请输入");
        XAPI_EXPECT(strcmp(xapi_cstr(XLineEdit_placeholderText(&le)), "请输入") == 0,
                    "LineEdit placeholderText 往返");
        XLineEdit_setPlaceholderText(&le, NULL);
        XAPI_EXPECT(xapi_cstr(XLineEdit_placeholderText(&le))[0] == '\0',
                    "LineEdit placeholder(NULL) 等价空串");
        XLineEdit_setTextMargins(&le, 1, 2, 3, 4);
        mg = XLineEdit_textMargins(&le);
        XAPI_EXPECT(mg.left == 1 && mg.top == 2 && mg.right == 3 &&
                    mg.bottom == 4,
                    "LineEdit setTextMargins 四值往返（QMargins 承载）");
        XLineEdit_setTextMargins_2(&le, NULL);
        mg = XLineEdit_textMargins(&le);
        XAPI_EXPECT(mg.left == 0 && mg.top == 0 && mg.right == 0 &&
                    mg.bottom == 0,
                    "LineEdit setTextMargins_2(NULL) 等价零边距");
        XLineEdit_setAlignment(&le,
                               (int)(XAlignment_HCenter | XAlignment_VCenter));
        XAPI_EXPECT(XLineEdit_alignment(&le) ==
                    (int)(XAlignment_HCenter | XAlignment_VCenter),
                    "LineEdit alignment 往返（居中组合）");
        XLineEdit_setFrame(&le, false);
        XAPI_EXPECT(!XLineEdit_hasFrame(&le),
                    "LineEdit setFrame(false) 往返");
        XLineEdit_setFrame(&le, true);
        XLineEdit_setDragEnabled(&le, true);
        XAPI_EXPECT(XLineEdit_dragEnabled(&le),
                    "LineEdit dragEnabled 往返（仅状态承载，交互为后续扩展）");
        XLineEdit_setDragEnabled(&le, false);
        XLineEdit_setCursorMoveStyle(
            &le, (int)XLineEditCursorMoveStyle_VisualMoveStyle);
        XAPI_EXPECT(XLineEdit_cursorMoveStyle(&le) ==
                    (int)XLineEditCursorMoveStyle_VisualMoveStyle,
                    "LineEdit cursorMoveStyle 往返（仅状态承载）");
        XLineEdit_setCursorMoveStyle(
            &le, (int)XLineEditCursorMoveStyle_LogicalMoveStyle);

        /* ---- 尺寸提示与光标矩形（几何事实断言，非渲染效果） ---- */
        sz = XLineEdit_sizeHint(&le);
        msz = XLineEdit_minimumSizeHint(&le);
        XAPI_EXPECT(sz.width > 0 && sz.height > 0,
                    "LineEdit sizeHint 非退化（对标 QLineEdit::sizeHint）");
        XAPI_EXPECT(msz.width > 0 && msz.height > 0,
                    "LineEdit minimumSizeHint 非退化");
        {
            XRect cr = XLineEdit_cursorRect(&le);
            XAPI_EXPECT(cr.width == 1 && cr.height > 0,
                        "LineEdit cursorRect 为 1px 宽竖线（本地坐标）");
        }

        /* ---- NULL 边界（头文件注明的缺省值语义） ---- */
        XAPI_EXPECT(xapi_cstr(XLineEdit_text(NULL))[0] == '\0' &&
                    XLineEdit_maxLength(NULL) == 0 &&
                    XLineEdit_echoMode(NULL) == (int)XLineEditEchoMode_Normal,
                    "LineEdit NULL 对象查询走缺省值（头文件 NULL 语义）");
        XLineEdit_setText(NULL, "noop");
        XLineEdit_deinit_base(&le);
    }
#endif /* XWIDGET_ON && XLINEEDIT_ON */

#if XWIDGET_ON && XLINEEDIT_ON && XABSTRACTSPINBOX_ON

    /* ================================================================
     * 2. XAbstractSpinBox：微调框抽象基类公共合同（直接实例化；C 无
     *    抽象类概念，基类默认虚槽行为经本对象验证）。
     * ================================================================ */
    {
        XAbstractSpinBox ab;
        XLineEdit* custom;
        int pos = 0;

        XAbstractSpinBox_init(&ab, NULL, 0);

        /* ---- 枚举数值对标 Qt（QAbstractSpinBox/QValidator 同名枚举） ---- */
        XAPI_EXPECT((int)XAbstractSpinBoxButtonSymbols_UpDownArrows == 0 &&
                    (int)XAbstractSpinBoxButtonSymbols_PlusMinus == 1 &&
                    (int)XAbstractSpinBoxButtonSymbols_NoButtons == 2,
                    "AbstractSpinBox ButtonSymbols 数值对齐 Qt");
        XAPI_EXPECT((int)XAbstractSpinBoxStepEnabledFlag_StepNone == 0x00 &&
                    (int)XAbstractSpinBoxStepEnabledFlag_StepUpEnabled ==
                        0x01 &&
                    (int)XAbstractSpinBoxStepEnabledFlag_StepDownEnabled ==
                        0x02,
                    "AbstractSpinBox StepEnabledFlag 位值对齐 Qt");
        XAPI_EXPECT((int)XAbstractSpinBoxCorrectionMode_CorrectToPreviousValue
                        == 0 &&
                    (int)XAbstractSpinBoxCorrectionMode_CorrectToNearestValue
                        == 1,
                    "AbstractSpinBox CorrectionMode 数值对齐 Qt");
        XAPI_EXPECT((int)XAbstractSpinBoxStepType_DefaultStepType == 0 &&
                    (int)XAbstractSpinBoxStepType_AdaptiveDecimalStepType == 1,
                    "AbstractSpinBox StepType 数值对齐 Qt");
        XAPI_EXPECT((int)XValidatorState_Invalid == 0 &&
                    (int)XValidatorState_Intermediate == 1 &&
                    (int)XValidatorState_Acceptable == 2,
                    "AbstractSpinBox ValidatorState 数值对齐 Qt（QValidator::State）");

        /* ---- 默认值（QAbstractSpinBox 构造默认） ---- */
        XAPI_EXPECT(XAbstractSpinBox_lineEdit(&ab) != NULL,
                    "AbstractSpinBox 默认拥有内嵌编辑框");
        XAPI_EXPECT(XAbstractSpinBox_buttonSymbols(&ab) ==
                    (int)XAbstractSpinBoxButtonSymbols_UpDownArrows,
                    "AbstractSpinBox 默认按钮符号 UpDownArrows");
        XAPI_EXPECT(XAbstractSpinBox_correctionMode(&ab) ==
                    (int)XAbstractSpinBoxCorrectionMode_CorrectToPreviousValue,
                    "AbstractSpinBox 默认修正模式 CorrectToPreviousValue");
        XAPI_EXPECT(XAbstractSpinBox_keyboardTracking(&ab),
                    "AbstractSpinBox 默认键盘跟踪开启（Qt 默认 true）");
        XAPI_EXPECT(XAbstractSpinBox_hasFrame(&ab),
                    "AbstractSpinBox 默认绘制边框（Qt frame 默认 true）");
        XAPI_EXPECT(!XAbstractSpinBox_wrapping(&ab),
                    "AbstractSpinBox 默认不循环（Qt wrapping 默认 false）");
        XAPI_EXPECT(!XAbstractSpinBox_isAccelerated(&ab),
                    "AbstractSpinBox 默认不加速（Qt accelerated 默认 false）");
        XAPI_EXPECT(!XAbstractSpinBox_isGroupSeparatorShown(&ab),
                    "AbstractSpinBox 默认不显示千分位");
        XAPI_EXPECT(strcmp(xapi_cstr(XAbstractSpinBox_specialValueText(&ab)), "") == 0,
                    "AbstractSpinBox 默认特殊值文本=空串");
        XAPI_EXPECT(!XAbstractSpinBox_isReadOnly(&ab),
                    "AbstractSpinBox 默认可编辑");
        XAPI_EXPECT(XAbstractSpinBox_alignment(&ab) == (int)XAlignment_Left,
                    "AbstractSpinBox 默认对齐转发编辑框=左对齐");

        /* ---- 属性往返 ---- */
        XAbstractSpinBox_setButtonSymbols(
            &ab, (int)XAbstractSpinBoxButtonSymbols_PlusMinus);
        XAPI_EXPECT(XAbstractSpinBox_buttonSymbols(&ab) ==
                    (int)XAbstractSpinBoxButtonSymbols_PlusMinus,
                    "AbstractSpinBox setButtonSymbols(PlusMinus) 往返");
        XAbstractSpinBox_setButtonSymbols(
            &ab, (int)XAbstractSpinBoxButtonSymbols_NoButtons);
        XAPI_EXPECT(XAbstractSpinBox_buttonSymbols(&ab) ==
                    (int)XAbstractSpinBoxButtonSymbols_NoButtons,
                    "AbstractSpinBox setButtonSymbols(NoButtons) 往返");
        XAbstractSpinBox_setButtonSymbols(
            &ab, (int)XAbstractSpinBoxButtonSymbols_UpDownArrows);
        XAbstractSpinBox_setCorrectionMode(
            &ab, (int)XAbstractSpinBoxCorrectionMode_CorrectToNearestValue);
        XAPI_EXPECT(XAbstractSpinBox_correctionMode(&ab) ==
                    (int)XAbstractSpinBoxCorrectionMode_CorrectToNearestValue,
                    "AbstractSpinBox setCorrectionMode 往返");
        XAbstractSpinBox_setCorrectionMode(
            &ab, (int)XAbstractSpinBoxCorrectionMode_CorrectToPreviousValue);
        XAbstractSpinBox_setWrapping(&ab, true);
        XAPI_EXPECT(XAbstractSpinBox_wrapping(&ab),
                    "AbstractSpinBox setWrapping 往返");
        XAbstractSpinBox_setWrapping(&ab, false);
        XAbstractSpinBox_setKeyboardTracking(&ab, false);
        XAPI_EXPECT(!XAbstractSpinBox_keyboardTracking(&ab),
                    "AbstractSpinBox setKeyboardTracking(false) 往返");
        XAbstractSpinBox_setKeyboardTracking(&ab, true);
        XAbstractSpinBox_setFrame(&ab, false);
        XAPI_EXPECT(!XAbstractSpinBox_hasFrame(&ab),
                    "AbstractSpinBox setFrame(false) 往返");
        XAbstractSpinBox_setFrame(&ab, true);
        XAbstractSpinBox_setAccelerated(&ab, true);
        XAPI_EXPECT(XAbstractSpinBox_isAccelerated(&ab),
                    "AbstractSpinBox setAccelerated 往返（字段级承载）");
        XAbstractSpinBox_setAccelerated(&ab, false);
        XAbstractSpinBox_setGroupSeparatorShown(&ab, true);
        XAPI_EXPECT(XAbstractSpinBox_isGroupSeparatorShown(&ab),
                    "AbstractSpinBox setGroupSeparatorShown 往返");
        XAbstractSpinBox_setGroupSeparatorShown(&ab, false);
        XAbstractSpinBox_setSpecialValueText(&ab, "Auto");
        XAPI_EXPECT(strcmp(XAbstractSpinBox_specialValueText(&ab),
                           "Auto") == 0,
                    "AbstractSpinBox setSpecialValueText 往返");
        XAbstractSpinBox_setSpecialValueText(&ab, "");
        XAPI_EXPECT(strcmp(xapi_cstr(XAbstractSpinBox_specialValueText(&ab)), "") == 0,
                    "AbstractSpinBox 空串关闭特殊值文本");
        XAbstractSpinBox_setAlignment(&ab, (int)XAlignment_Right);
        XAPI_EXPECT(XAbstractSpinBox_alignment(&ab) == (int)XAlignment_Right,
                    "AbstractSpinBox setAlignment 转发内嵌编辑框");
        XAbstractSpinBox_setReadOnly(&ab, true);
        XAPI_EXPECT(XAbstractSpinBox_isReadOnly(&ab) &&
                    XLineEdit_isReadOnly(XAbstractSpinBox_lineEdit(&ab)),
                    "AbstractSpinBox setReadOnly 转发内嵌编辑框同步");
        XAbstractSpinBox_setReadOnly(&ab, false);

        /* ---- 基类默认虚槽（无范围语义的抽象行为） ---- */
        XAPI_EXPECT(XAbstractSpinBox_validate_base(&ab, "x", &pos) ==
                    XValidatorState_Acceptable,
                    "AbstractSpinBox 基类 validate 默认恒 Acceptable（头文件口径）");
        XAPI_EXPECT(XAbstractSpinBox_stepEnabled_base(&ab) ==
                    (int)XAbstractSpinBoxStepEnabledFlag_StepNone,
                    "AbstractSpinBox 基类 stepEnabled 默认 StepNone（抽象基类无范围）");
        XAbstractSpinBox_stepBy_base(&ab, 3);
        XAPI_EXPECT(strcmp(xapi_cstr(XAbstractSpinBox_text(&ab)), "") == 0,
                    "AbstractSpinBox 基类 stepBy 默认空操作（文本不变）");
        XAbstractSpinBox_stepUp(&ab);
        XAbstractSpinBox_stepDown(&ab);
        XAPI_EXPECT(strcmp(xapi_cstr(XAbstractSpinBox_text(&ab)), "") == 0,
                    "AbstractSpinBox stepUp/stepDown 在基类为无操作");
        XAbstractSpinBox_clear_base(&ab);
        XAPI_EXPECT(strcmp(XLineEdit_text(XAbstractSpinBox_lineEdit(&ab)),
                           "") == 0,
                    "AbstractSpinBox clear 默认清空编辑框并置待解释标志");

        /* ---- setLineEdit 替换（QAbstractSpinBox::setLineEdit） ---- */
        custom = XLineEdit_create(NULL, 0);
        XAbstractSpinBox_setLineEdit(&ab, custom);
        XAPI_EXPECT(XAbstractSpinBox_lineEdit(&ab) == custom,
                    "AbstractSpinBox setLineEdit 接管自定义编辑框（旧对象释放）");
        XAbstractSpinBox_setLineEdit(&ab, NULL);
        XAPI_EXPECT(XAbstractSpinBox_lineEdit(&ab) != NULL &&
                    XAbstractSpinBox_lineEdit(&ab) != custom,
                    "AbstractSpinBox setLineEdit(NULL) 内部新建编辑框");

        /* ---- Return 提交发射 editingFinished（基类 keyPressEvent） ---- */
        {
            static int abFinished;
            abFinished = 0;
            XObject_connect_1(
                (XObject*)&ab,
                (size_t)XAbstractSpinBox_editingFinished_signal(NULL),
                (XObject*)&ab, input_leEditingFinishedSlot,
                XConnectionType_Direct);
            g_leSig.editingFinished = 0;
            input_injectKey((XWidget*)&ab, XKey_Return, 0);
            abFinished = g_leSig.editingFinished;
            XObject_disconnect_1(
                (XObject*)&ab,
                (size_t)XAbstractSpinBox_editingFinished_signal(NULL),
                (XObject*)&ab, input_leEditingFinishedSlot);
            XAPI_EXPECT(abFinished == 1,
                        "AbstractSpinBox Return 提交发射 editingFinished");
        }

        XAbstractSpinBox_deinit_base(&ab);
    }
#endif /* XWIDGET_ON && XLINEEDIT_ON && XABSTRACTSPINBOX_ON */

#if XWIDGET_ON && XSPINBOX_ON && XLINEEDIT_ON && XABSTRACTSPINBOX_ON

    /* ================================================================
     * 3. XSpinBox：整数微调框（对标 Qt 6.8 QSpinBox public API）。
     * ================================================================ */
    {
        XSpinBox spin;
        XAbstractSpinBox* base;
        XLineEdit* edit;
        char* clean;
        int pos = 0;

        XSpinBox_init(&spin, NULL, 0);
        base = (XAbstractSpinBox*)&spin;
        edit = XAbstractSpinBox_lineEdit(base);
        input_spinConnect(&spin);
        input_spinReset();

        /* ---- 默认值（QSpinBox 构造默认） ---- */
        XAPI_EXPECT(XSpinBox_minimum(&spin) == 0 &&
                    XSpinBox_maximum(&spin) == 99,
                    "SpinBox 默认范围 0..99（QSpinBox 默认）");
        XAPI_EXPECT(XSpinBox_value(&spin) == 0,
                    "SpinBox 默认值 0");
        XAPI_EXPECT(XSpinBox_singleStep(&spin) == 1,
                    "SpinBox 默认单步 1");
        XAPI_EXPECT(XSpinBox_displayIntegerBase(&spin) == 10,
                    "SpinBox 默认显示进制 10");
        XAPI_EXPECT(XSpinBox_stepType(&spin) ==
                    (int)XAbstractSpinBoxStepType_DefaultStepType,
                    "SpinBox 默认固定步进类型 DefaultStepType");
        XAPI_EXPECT(strcmp(xapi_cstr(XSpinBox_prefix(&spin)), "") == 0 &&
                    strcmp(xapi_cstr(XSpinBox_suffix(&spin)), "") == 0,
                    "SpinBox 默认前后缀=空串");
        XAPI_EXPECT(strcmp(xapi_cstr(XAbstractSpinBox_text(base)), "0") == 0,
                    "SpinBox 默认显示文本=0");
        clean = XSpinBox_cleanText(&spin);
        XAPI_EXPECT(clean != NULL && strcmp(clean, "0") == 0,
                    "SpinBox 默认纯净文本=0（cleanText 无前后缀）");
        XFree_System(clean);
        XAPI_EXPECT(XAbstractSpinBox_hasAcceptableInput(base),
                    "SpinBox 默认输入可接受");

        /* ---- setValue 钳位与文本同步（QSpinBox::setValue） ---- */
        XSpinBox_setValue(&spin, 50);
        XAPI_EXPECT(XSpinBox_value(&spin) == 50 &&
                    strcmp(xapi_cstr(XAbstractSpinBox_text(base)), "50") == 0,
                    "SpinBox setValue(50) 生效且编辑框同步");
        XSpinBox_setValue(&spin, 200);
        XAPI_EXPECT(XSpinBox_value(&spin) == 99,
                    "SpinBox 越界值钳位到 maximum");
        XSpinBox_setValue(&spin, -5);
        XAPI_EXPECT(XSpinBox_value(&spin) == 0,
                    "SpinBox 负值钳位到 minimum");

        /* ---- 范围 API（Qt 语义：不交换，端收敛，重钳位） ---- */
        XSpinBox_setRange(&spin, 10, 0);
        XAPI_EXPECT(XSpinBox_minimum(&spin) == 10 &&
                    XSpinBox_maximum(&spin) == 10,
                    "SpinBox setRange(min>max) 上限收敛为 min（Qt 语义）");
        XSpinBox_setRange(&spin, 0, 100);
        XSpinBox_setMinimum(&spin, 200);
        XAPI_EXPECT(XSpinBox_minimum(&spin) == 200 &&
                    XSpinBox_maximum(&spin) == 200,
                    "SpinBox setMinimum 超上限时上限收敛");
        XSpinBox_setMaximum(&spin, 150);
        XAPI_EXPECT(XSpinBox_minimum(&spin) == 150 &&
                    XSpinBox_maximum(&spin) == 150,
                    "SpinBox setMaximum 低于下限时下限收敛");
        XSpinBox_setRange(&spin, 0, 100);
        XAPI_EXPECT(XSpinBox_value(&spin) == 100,
                    "SpinBox 范围变更后当前值重钳位");

        /* ---- 步进（stepBy/stepUp/stepDown/singleStep） ---- */
        XSpinBox_setValue(&spin, 50);
        XAbstractSpinBox_stepBy_base(base, 1);
        XAPI_EXPECT(XSpinBox_value(&spin) == 51,
                    "SpinBox stepBy(+1) 步进");
        XAbstractSpinBox_stepBy_base(base, -1);
        XAPI_EXPECT(XSpinBox_value(&spin) == 50,
                    "SpinBox stepBy(-1) 反向步进");
        XSpinBox_setSingleStep(&spin, 5);
        XAPI_EXPECT(XSpinBox_singleStep(&spin) == 5,
                    "SpinBox setSingleStep 往返");
        XAbstractSpinBox_stepUp(base);
        XAPI_EXPECT(XSpinBox_value(&spin) == 55,
                    "SpinBox stepUp 按 singleStep=5 步进");
        XAbstractSpinBox_stepDown(base);
        XAPI_EXPECT(XSpinBox_value(&spin) == 50,
                    "SpinBox stepDown 按 singleStep 步进");
        XSpinBox_setSingleStep(&spin, 0);
        XAPI_EXPECT(XSpinBox_singleStep(&spin) == 0,
                    "SpinBox singleStep=0 合法（<0 才忽略）");
        XSpinBox_setSingleStep(&spin, -3);
        XAPI_EXPECT(XSpinBox_singleStep(&spin) == 0,
                    "SpinBox 负 singleStep 忽略（头文件口径）");
        XSpinBox_setSingleStep(&spin, 1);

        /* ---- 键盘步进（Up/Down/PageUp/PageDown/Home/End） ---- */
        XSpinBox_setRange(&spin, 0, 100);
        XSpinBox_setValue(&spin, 50);
        input_injectKey((XWidget*)base, XKey_Up, 0);
        XAPI_EXPECT(XSpinBox_value(&spin) == 51,
                    "SpinBox Up 键步进 +1（QAbstractSpinBox keyPress）");
        input_injectKey((XWidget*)base, XKey_Down, 0);
        XAPI_EXPECT(XSpinBox_value(&spin) == 50,
                    "SpinBox Down 键步进 -1");
        input_injectKey((XWidget*)base, XKey_PageUp, 0);
        XAPI_EXPECT(XSpinBox_value(&spin) == 60,
                    "SpinBox PageUp 步进 +10（Qt PageStep=10）");
        input_injectKey((XWidget*)base, XKey_PageDown, 0);
        XAPI_EXPECT(XSpinBox_value(&spin) == 50,
                    "SpinBox PageDown 步进 -10");
        input_injectKey((XWidget*)base, XKey_Home, 0);
        XAPI_EXPECT(XSpinBox_value(&spin) == 0,
                    "SpinBox Home 跳到 minimum（边界跳转语义）");
        input_injectKey((XWidget*)base, XKey_End, 0);
        XAPI_EXPECT(XSpinBox_value(&spin) == 100,
                    "SpinBox End 跳到 maximum");
        input_injectKey((XWidget*)base, XKey_End, 0);
        XAPI_EXPECT(XSpinBox_value(&spin) == 100,
                    "SpinBox 边界键在边界处不动作（stepEnabled 门禁）");

        /* ---- 滚轮步进（120 角度=1 步，角度累积） ---- */
        XSpinBox_setValue(&spin, 50);
        input_injectWheel((XWidget*)base, 120);
        XAPI_EXPECT(XSpinBox_value(&spin) == 51,
                    "SpinBox 滚轮 +120 角度步进 +1");
        input_injectWheel((XWidget*)base, -240);
        XAPI_EXPECT(XSpinBox_value(&spin) == 49,
                    "SpinBox 滚轮 -240 角度步进 -2");
        input_injectWheel((XWidget*)base, 60);
        input_injectWheel((XWidget*)base, 60);
        XAPI_EXPECT(XSpinBox_value(&spin) == 50,
                    "SpinBox 滚轮角度累积至 120 才步进");

        /* ---- 文本解析联动（编辑框 setText → 值解析/校验拒绝） ---- */
        XSpinBox_setValue(&spin, 10);
        XLineEdit_setText(edit, "42");
        XAPI_EXPECT(XSpinBox_value(&spin) == 42,
                    "SpinBox 文本 42 解析为值（键盘跟踪开启实时提交）");
        XLineEdit_setText(edit, "500");
        XAPI_EXPECT(XSpinBox_value(&spin) == 42,
                    "SpinBox 越界文本编辑中保持原值（Qt 语义）");
        XAbstractSpinBox_interpretText(base);
        XAPI_EXPECT(XSpinBox_value(&spin) == 42 &&
                    strcmp(xapi_cstr(XAbstractSpinBox_text(base)), "42") == 0,
                    "SpinBox interpretText 按 CorrectToPreviousValue 恢复显示");
        XLineEdit_setText(edit, "abc");
        XAPI_EXPECT(XSpinBox_value(&spin) == 42,
                    "SpinBox 非法文本保持原值");
        {
            XLineEdit* le = XSpinBox_lineEdit(&spin);
            XSpinBox_setValue(&spin, 7);
            input_injectKey((XWidget*)le, 'a', 0);
            XAPI_EXPECT(strcmp(xapi_cstr(XLineEdit_text(le)), "7") == 0,
                        "SpinBox 键入字母被数值校验拒绝（QSpinBox 校验语义）");
            input_injectKey((XWidget*)le, '5', 0);
            XAPI_EXPECT(strcmp(xapi_cstr(XLineEdit_text(le)), "75") == 0,
                        "SpinBox 键入数字正常进入");
        }

        /* ---- 前后缀（prefix/suffix/cleanText/valueFromText） ---- */
        XSpinBox_setPrefix(&spin, "$");
        XSpinBox_setSuffix(&spin, "%");
        XSpinBox_setValue(&spin, 60);
        XAPI_EXPECT(strcmp(xapi_cstr(XAbstractSpinBox_text(base)), "$60%") == 0,
                    "SpinBox 显示=前缀+数值+后缀（QSpinBox 组合语义）");
        clean = XSpinBox_cleanText(&spin);
        XAPI_EXPECT(clean != NULL && strcmp(clean, "60") == 0,
                    "SpinBox cleanText 剥离前后缀");
        XFree_System(clean);
        XLineEdit_setText(edit, "$70%");
        XAPI_EXPECT(XSpinBox_value(&spin) == 70,
                    "SpinBox 带前后缀文本可解析");
        XAPI_EXPECT(XSpinBox_valueFromText_base(&spin, "$80%") == 80,
                    "SpinBox valueFromText 剥离前后缀解析");
        {
            char* t = XSpinBox_textFromValue_base(&spin, 90);
            XAPI_EXPECT(t != NULL && strcmp(t, "$90%") == 0,
                        "SpinBox textFromValue 生成前后缀文本");
            XFree_System(t);
        }
        XSpinBox_setPrefix(&spin, "");
        XSpinBox_setSuffix(&spin, "");

        /* ---- 显示进制（displayIntegerBase，越界回退 10） ---- */
        XSpinBox_setDisplayIntegerBase(&spin, 16);
        XSpinBox_setValue(&spin, 26);
        XAPI_EXPECT(strcmp(xapi_cstr(XAbstractSpinBox_text(base)), "1a") == 0,
                    "SpinBox base 16 显示 1a（QSpinBox 进制显示）");
        XSpinBox_setDisplayIntegerBase(&spin, 1);
        XAPI_EXPECT(XSpinBox_displayIntegerBase(&spin) == 10,
                    "SpinBox base<2 越界回退 10");
        XSpinBox_setDisplayIntegerBase(&spin, 37);
        XAPI_EXPECT(XSpinBox_displayIntegerBase(&spin) == 10,
                    "SpinBox base>36 越界回退 10");
        XSpinBox_setDisplayIntegerBase(&spin, 10);

        /* ---- 特殊值文本（value==minimum 整段替换） ---- */
        XAbstractSpinBox_setSpecialValueText(base, "Auto");
        XSpinBox_setValue(&spin, 0);
        XAPI_EXPECT(strcmp(xapi_cstr(XAbstractSpinBox_text(base)), "Auto") == 0,
                    "SpinBox 最小值处整段显示特殊值文本");
        XLineEdit_setText(edit, "Auto");
        XAPI_EXPECT(XSpinBox_value(&spin) == 0,
                    "SpinBox 输入特殊值文本归 minimum（Qt 语义）");
        XAbstractSpinBox_setSpecialValueText(base, "");

        /* ---- wrapping 循环与 stepEnabled 位（QSpinBox 边界语义） ---- */
        XSpinBox_setRange(&spin, 0, 10);
        XSpinBox_setValue(&spin, 10);
        XAbstractSpinBox_setWrapping(base, true);
        XAbstractSpinBox_stepBy_base(base, 1);
        XAPI_EXPECT(XSpinBox_value(&spin) == 0,
                    "SpinBox wrapping 越过 max 绕回 min");
        XAbstractSpinBox_stepBy_base(base, -1);
        XAPI_EXPECT(XSpinBox_value(&spin) == 10,
                    "SpinBox wrapping 越过 min 绕回 max");
        XAbstractSpinBox_setWrapping(base, false);
        XSpinBox_setValue(&spin, 0);
        XAPI_EXPECT(XAbstractSpinBox_stepEnabled_base(base) ==
                    (int)XAbstractSpinBoxStepEnabledFlag_StepUpEnabled,
                    "SpinBox min 处仅可上步进（StepUpEnabled）");
        XSpinBox_setValue(&spin, 10);
        XAPI_EXPECT(XAbstractSpinBox_stepEnabled_base(base) ==
                    (int)XAbstractSpinBoxStepEnabledFlag_StepDownEnabled,
                    "SpinBox max 处仅可下步进（StepDownEnabled）");
        XSpinBox_setValue(&spin, 5);
        XAPI_EXPECT(XAbstractSpinBox_stepEnabled_base(base) ==
                    ((int)XAbstractSpinBoxStepEnabledFlag_StepUpEnabled |
                     (int)XAbstractSpinBoxStepEnabledFlag_StepDownEnabled),
                    "SpinBox 中间值双向步进使能");
        XAbstractSpinBox_setReadOnly(base, true);
        XAPI_EXPECT(XAbstractSpinBox_stepEnabled_base(base) ==
                    (int)XAbstractSpinBoxStepEnabledFlag_StepNone,
                    "SpinBox 只读时不可步进（StepNone）");
        XAbstractSpinBox_setReadOnly(base, false);

        /* ---- validate 状态（QValidator::State 三态） ---- */
        XSpinBox_setValue(&spin, 5);
        XAPI_EXPECT(XAbstractSpinBox_validate_base(base, "5", &pos) ==
                    XValidatorState_Acceptable,
                    "SpinBox 合法文本校验 Acceptable");
        XAPI_EXPECT(XAbstractSpinBox_validate_base(base, "500", &pos) ==
                    XValidatorState_Invalid,
                    "SpinBox 越界文本校验 Invalid");
        XAPI_EXPECT(XAbstractSpinBox_validate_base(base, "", &pos) ==
                    XValidatorState_Intermediate,
                    "SpinBox 空文本校验 Intermediate");

        /* ---- clear/selectAll（QSpinBox 继承语义） ---- */
        /* 注：当前范围 0..10（wrapping 段设置）；首版按 42 断言属编写期
         * 误判——Qt setValue 钳位语义下 42 收敛到 maximum=10，后续信号
         * 断言的基态随之漂移。改用范围内值 7，并在此后恢复 0..100。 */
        XSpinBox_setValue(&spin, 7);
        XAbstractSpinBox_clear_base(base);
        XAPI_EXPECT(strcmp(xapi_cstr(XAbstractSpinBox_text(base)), "") == 0 &&
                    XSpinBox_value(&spin) == 7,
                    "SpinBox clear 清文本但保持值（待解释状态；Qt clear 仅清前后缀间编辑区）");
        XAbstractSpinBox_interpretText(base);
        XAPI_EXPECT(XSpinBox_value(&spin) == 7,
                    "SpinBox clear 后提交不改变值");
        XSpinBox_setValue(&spin, 8); /* 恢复编辑框文本后再验证全选转发 */
        XAbstractSpinBox_selectAll(base);
        XAPI_EXPECT(XLineEdit_hasSelectedText(edit),
                    "SpinBox selectAll 转发内嵌编辑框全选");

        /* ---- 信号（valueChanged/textChanged + 同值不发射） ---- */
        XSpinBox_setRange(&spin, 0, 100); /* 恢复 0..100：Up 步进需上限余量 */
        input_spinReset();
        XSpinBox_setValue(&spin, 10);
        XAPI_EXPECT(g_spinSig.valueChanged == 1 &&
                    g_spinSig.lastValue == 10,
                    "SpinBox setValue 发射 valueChanged 且载荷为新值");
        XAPI_EXPECT(g_spinSig.textChanged == 1 &&
                    strcmp(g_spinSig.lastText, "10") == 0,
                    "SpinBox setValue 文本变化发射 textChanged");
        XSpinBox_setValue(&spin, 10);
        XAPI_EXPECT(g_spinSig.valueChanged == 1 && g_spinSig.textChanged == 1,
                    "SpinBox 同值 setValue 不发射信号（Qt 语义）");
        input_injectKey((XWidget*)base, XKey_Up, 0);
        XAPI_EXPECT(g_spinSig.valueChanged == 2,
                    "SpinBox 键盘步进发射 valueChanged");

        XSpinBox_deinit_base(&spin);
    }
#endif /* XWIDGET_ON && XSPINBOX_ON && XLINEEDIT_ON && XABSTRACTSPINBOX_ON */

#if XWIDGET_ON && XABSTRACTSPINBOX_ON && XDATETIMEEDIT_ON

    /* ================================================================
     * 4. XDateTimeEdit：日期时间编辑（对标 Qt 6.8 QDateTimeEdit）。
     * ================================================================ */
    {
        XDateTimeEdit dt;
        const XDateTime* got;
        XDateTime dtv;
        XDate d;
        XTime t;
        XString* sec;

        XDateTimeEdit_init(&dt, NULL, 0);
        input_dtConnect(&dt);
        input_dtReset();

        /* ---- 默认值 ---- */
        XAPI_EXPECT(strcmp(XDateTimeEdit_displayFormat(&dt),
                           "yyyy-MM-dd HH:mm:ss") == 0,
                    "DateTimeEdit 默认格式=yyyy-MM-dd HH:mm:ss（同 Qt 默认）");
        XAPI_EXPECT(XDateTimeEdit_currentSection(&dt) ==
                    (int)XDateTimeEditSection_YearSection,
                    "DateTimeEdit 默认当前分段=YearSection");
        XAPI_EXPECT(!XDateTimeEdit_calendarPopup(&dt),
                    "DateTimeEdit calendarPopup 默认 false（Qt 同）");
        XAPI_EXPECT(XDateTimeEdit_timeSpec(&dt) == 0,
                    "DateTimeEdit timeSpec 默认 LocalTime(0)（Qt 同）");
        d = XDateTimeEdit_minimumDate(&dt);
        XAPI_EXPECT(XDate_year(&d) == 1900 && XDate_month(&d) == 1 &&
                    XDate_day(&d) == 1,
                    "DateTimeEdit minimumDate 默认 1900-01-01（XGui 裁剪默认，Qt 为 1752-09-14——头文件注明差异）");
        d = XDateTimeEdit_maximumDate(&dt);
        XAPI_EXPECT(XDate_year(&d) == 2999 && XDate_month(&d) == 12 &&
                    XDate_day(&d) == 31,
                    "DateTimeEdit maximumDate 默认 2999-12-31（Qt 为 9999-12-31——裁剪差异）");
        t = XDateTimeEdit_minimumTime(&dt);
        XAPI_EXPECT(XTime_hour(&t) == 0 && XTime_minute(&t) == 0 &&
                    XTime_second(&t) == 0 && XTime_msec(&t) == 0,
                    "DateTimeEdit minimumTime 默认 00:00:00.000");
        t = XDateTimeEdit_maximumTime(&dt);
        XAPI_EXPECT(XTime_hour(&t) == 23 && XTime_minute(&t) == 59 &&
                    XTime_second(&t) == 59 && XTime_msec(&t) == 999,
                    "DateTimeEdit maximumTime 默认 23:59:59.999");

        /* ---- 分段布局（sections/sectionCount/sectionAt） ---- */
        XAPI_EXPECT(XDateTimeEdit_sections(&dt) ==
                    ((int)XDateTimeEditSection_YearSection |
                     (int)XDateTimeEditSection_MonthSection |
                     (int)XDateTimeEditSection_DaySection |
                     (int)XDateTimeEditSection_HourSection |
                     (int)XDateTimeEditSection_MinuteSection |
                     (int)XDateTimeEditSection_SecondSection),
                    "DateTimeEdit 默认格式分段掩码=6 段（displayedSections 同承载）");
        XAPI_EXPECT(XDateTimeEdit_sectionCount(&dt) == 6,
                    "DateTimeEdit sectionCount 默认格式 6 段（对标 sectionCount）");
        XAPI_EXPECT(XDateTimeEdit_sectionAt(&dt, 0) ==
                    (int)XDateTimeEditSection_YearSection &&
                    XDateTimeEdit_sectionAt(&dt, 1) ==
                        (int)XDateTimeEditSection_MonthSection &&
                    XDateTimeEdit_sectionAt(&dt, 2) ==
                        (int)XDateTimeEditSection_DaySection &&
                    XDateTimeEdit_sectionAt(&dt, 3) ==
                        (int)XDateTimeEditSection_HourSection &&
                    XDateTimeEdit_sectionAt(&dt, 4) ==
                        (int)XDateTimeEditSection_MinuteSection &&
                    XDateTimeEdit_sectionAt(&dt, 5) ==
                        (int)XDateTimeEditSection_SecondSection,
                    "DateTimeEdit sectionAt 按格式顺序映射（对标 sectionAt）");
        XAPI_EXPECT(XDateTimeEdit_sectionAt(&dt, 6) ==
                    (int)XDateTimeEditSection_NoSection,
                    "DateTimeEdit sectionAt 越界返回 NoSection");

        /* ---- setDateTime 往返 + 三信号（对标 QDateTimeEdit 三信号齐发） ---- */
        dtv = XDateTime_create_datetime(XDate_create_date(2024, 6, 15),
                                        XTime_create_time(12, 34, 56, 789));
        XDateTimeEdit_setDateTime(&dt, &dtv);
        got = XDateTimeEdit_dateTime(&dt);
        XAPI_EXPECT(XDate_year(&got->m_date) == 2024 &&
                    XTime_hour(&got->m_time) == 12,
                    "DateTimeEdit setDateTime/dateTime 往返");
        d = XDateTimeEdit_date(&dt);
        t = XDateTimeEdit_time(&dt);
        XAPI_EXPECT(XDate_month(&d) == 6 && XTime_minute(&t) == 34,
                    "DateTimeEdit date/time 部分查询");
        XAPI_EXPECT(g_dtSig.dateTimeChanged == 1 && g_dtSig.dateChanged == 1 &&
                    g_dtSig.timeChanged == 1,
                    "DateTimeEdit setDateTime 三信号齐发（dateTime 恒发，日期/时间部分按变化发射）");
        XAPI_EXPECT(g_dtSig.userDateChanged == 0 &&
                    g_dtSig.userTimeChanged == 0,
                    "DateTimeEdit 程序化 setDateTime 不发用户变体信号（user 族仅步进路径）");

        /* ---- 范围钳位（越界值收敛到边界） ---- */
        dtv = XDateTime_create_datetime(XDate_create_date(1800, 1, 1),
                                        XTime_create_time(0, 0, 0, 0));
        XDateTimeEdit_setDateTime(&dt, &dtv);
        got = XDateTimeEdit_dateTime(&dt);
        XAPI_EXPECT(XDate_year(&got->m_date) == 1900,
                    "DateTimeEdit 早于最小值钳位到 minimumDateTime");
        dtv = XDateTime_create_datetime(XDate_create_date(3000, 1, 1),
                                        XTime_create_time(0, 0, 0, 0));
        XDateTimeEdit_setDateTime(&dt, &dtv);
        got = XDateTimeEdit_dateTime(&dt);
        XAPI_EXPECT(XDate_year(&got->m_date) == 2999,
                    "DateTimeEdit 晚于最大值钳位到 maximumDateTime");

        /* ---- setDate/setTime 部分设置 ---- */
        d = XDate_create_date(2020, 2, 29);
        XDateTimeEdit_setDate(&dt, &d);
        t = XTime_create_time(6, 7, 8, 0);
        XDateTimeEdit_setTime(&dt, &t);
        d = XDateTimeEdit_date(&dt);
        t = XDateTimeEdit_time(&dt);
        XAPI_EXPECT(XDate_year(&d) == 2020 && XDate_day(&d) == 29 &&
                    XTime_hour(&t) == 6 && XTime_second(&t) == 8,
                    "DateTimeEdit setDate/setTime 部分往返（闰日合法）");

        /* ---- 范围 API 族（set/minimum/maximum Date/Time/DateTime） ---- */
        d = XDate_create_date(2000, 1, 1);
        XDateTimeEdit_setMinimumDate(&dt, &d);
        d = XDateTimeEdit_minimumDate(&dt);
        XAPI_EXPECT(XDate_year(&d) == 2000,
                    "DateTimeEdit setMinimumDate/minimumDate 往返");
        t = XTime_create_time(8, 0, 0, 0);
        XDateTimeEdit_setMinimumTime(&dt, &t);
        t = XDateTimeEdit_minimumTime(&dt);
        XAPI_EXPECT(XTime_hour(&t) == 8,
                    "DateTimeEdit setMinimumTime/minimumTime 往返");
        d = XDate_create_date(2030, 12, 31);
        XDateTimeEdit_setMaximumDate(&dt, &d);
        d = XDateTimeEdit_maximumDate(&dt);
        XAPI_EXPECT(XDate_day(&d) == 31,
                    "DateTimeEdit setMaximumDate/maximumDate 往返");
        t = XTime_create_time(20, 0, 0, 0);
        XDateTimeEdit_setMaximumTime(&dt, &t);
        t = XDateTimeEdit_maximumTime(&dt);
        XAPI_EXPECT(XTime_hour(&t) == 20,
                    "DateTimeEdit setMaximumTime/maximumTime 往返");
        {
            XDate dmin = XDate_create_date(2001, 2, 3);
            XDate dmax = XDate_create_date(2003, 4, 5);
            XDateTimeEdit_setDateRange(&dt, &dmin, &dmax);
            d = XDateTimeEdit_minimumDate(&dt);
            XAPI_EXPECT(XDate_year(&d) == 2001,
                        "DateTimeEdit setDateRange 下界生效（对标 setDateRange）");
            d = XDateTimeEdit_maximumDate(&dt);
            XAPI_EXPECT(XDate_year(&d) == 2003,
                        "DateTimeEdit setDateRange 上界生效");
        }
        {
            XTime tmin = XTime_create_time(1, 0, 0, 0);
            XTime tmax = XTime_create_time(22, 0, 0, 0);
            XDateTimeEdit_setTimeRange(&dt, &tmin, &tmax);
            t = XDateTimeEdit_minimumTime(&dt);
            XAPI_EXPECT(XTime_hour(&t) == 1,
                        "DateTimeEdit setTimeRange 下界生效");
            t = XDateTimeEdit_maximumTime(&dt);
            XAPI_EXPECT(XTime_hour(&t) == 22,
                        "DateTimeEdit setTimeRange 上界生效");
        }
        {
            XDateTime dmin = XDateTime_create_datetime(
                XDate_create_date(2005, 5, 5), XTime_create_time(5, 0, 0, 0));
            XDateTime dmax = XDateTime_create_datetime(
                XDate_create_date(2006, 6, 6), XTime_create_time(18, 0, 0, 0));
            XDateTimeEdit_setDateTimeRange(&dt, &dmin, &dmax);
            got = XDateTimeEdit_minimumDateTime(&dt);
            XAPI_EXPECT(XDate_year(&got->m_date) == 2005,
                        "DateTimeEdit setDateTimeRange 下界生效");
            got = XDateTimeEdit_maximumDateTime(&dt);
            XAPI_EXPECT(XDate_year(&got->m_date) == 2006,
                        "DateTimeEdit setDateTimeRange 上界生效");
        }

        /* ---- clear 系列（复位 init 默认边界） ---- */
        XDateTimeEdit_clearMinimumDate(&dt);
        d = XDateTimeEdit_minimumDate(&dt);
        XAPI_EXPECT(XDate_year(&d) == 1900,
                    "DateTimeEdit clearMinimumDate 复位 1900-01-01（XGui 默认下界）");
        XDateTimeEdit_clearMaximumDate(&dt);
        d = XDateTimeEdit_maximumDate(&dt);
        XAPI_EXPECT(XDate_year(&d) == 2999,
                    "DateTimeEdit clearMaximumDate 复位 2999-12-31");
        XDateTimeEdit_clearMinimumTime(&dt);
        t = XDateTimeEdit_minimumTime(&dt);
        XAPI_EXPECT(XTime_hour(&t) == 0 && XTime_msec(&t) == 0,
                    "DateTimeEdit clearMinimumTime 复位 00:00:00.000");
        XDateTimeEdit_clearMaximumTime(&dt);
        t = XDateTimeEdit_maximumTime(&dt);
        XAPI_EXPECT(XTime_hour(&t) == 23 && XTime_msec(&t) == 999,
                    "DateTimeEdit clearMaximumTime 复位 23:59:59.999");

        /* ---- displayFormat 与分段查询（sectionText/sectionCount） ---- */
        dtv = XDateTime_create_datetime(XDate_create_date(2024, 6, 15),
                                        XTime_create_time(12, 34, 56, 789));
        XDateTimeEdit_setDateTime(&dt, &dtv);
        XDateTimeEdit_setDisplayFormat(&dt, "dd/MM/yyyy");
        XAPI_EXPECT(strcmp(XDateTimeEdit_displayFormat(&dt),
                           "dd/MM/yyyy") == 0,
                    "DateTimeEdit setDisplayFormat 往返");
        XAPI_EXPECT(XDateTimeEdit_sectionCount(&dt) == 3,
                    "DateTimeEdit 新格式 sectionCount=3");
        XAPI_EXPECT(XDateTimeEdit_sections(&dt) ==
                    ((int)XDateTimeEditSection_DaySection |
                     (int)XDateTimeEditSection_MonthSection |
                     (int)XDateTimeEditSection_YearSection),
                    "DateTimeEdit 新格式分段掩码=日|月|年");
        sec = XDateTimeEdit_sectionText(&dt,
                                        (int)XDateTimeEditSection_YearSection);
        XAPI_EXPECT(sec != NULL && strcmp(xapi_u8(sec), "2024") == 0,
                    "DateTimeEdit sectionText(Year)=2024（按当前值渲染）");
        XString_delete_base((XClass*)sec);
        sec = XDateTimeEdit_sectionText(
            &dt, (int)XDateTimeEditSection_MonthSection);
        XAPI_EXPECT(sec != NULL && strcmp(xapi_u8(sec), "06") == 0,
                    "DateTimeEdit sectionText(Month)=06 两位补零");
        XString_delete_base((XClass*)sec);
        XDateTimeEdit_setDisplayFormat(&dt, "yyyy");
        sec = XDateTimeEdit_sectionText(
            &dt, (int)XDateTimeEditSection_MonthSection);
        XAPI_EXPECT(sec != NULL && xapi_u8(sec)[0] == '\0',
                    "DateTimeEdit 格式外的分段 sectionText=空文本");
        XString_delete_base((XClass*)sec);
        XDateTimeEdit_setDisplayFormat(&dt, "yyyy-MM-dd HH:mm:ss");

        /* ---- 分段定位（setCurrentSection/Index/setSelectedSection） ---- */
        XDateTimeEdit_setCurrentSection(&dt,
                                        (int)XDateTimeEditSection_MonthSection);
        XAPI_EXPECT(XDateTimeEdit_currentSection(&dt) ==
                    (int)XDateTimeEditSection_MonthSection,
                    "DateTimeEdit setCurrentSection 往返");
        XDateTimeEdit_setCurrentSectionIndex(&dt, 0);
        XAPI_EXPECT(XDateTimeEdit_currentSectionIndex(&dt) == 0 &&
                    XDateTimeEdit_currentSection(&dt) ==
                        (int)XDateTimeEditSection_YearSection,
                    "DateTimeEdit setCurrentSectionIndex(0) 映射为 YearSection（序号→分段码）");
        XDateTimeEdit_setSelectedSection(
            &dt, (int)XDateTimeEditSection_DaySection);
        XAPI_EXPECT(XDateTimeEdit_currentSection(&dt) ==
                    (int)XDateTimeEditSection_DaySection,
                    "DateTimeEdit setSelectedSection 选中格式中存在的段");
        XDateTimeEdit_setSelectedSection(
            &dt, (int)XDateTimeEditSection_MSecSection);
        XAPI_EXPECT(XDateTimeEdit_currentSection(&dt) ==
                    (int)XDateTimeEditSection_DaySection,
                    "DateTimeEdit setSelectedSection 格式外的段保持不变（Qt 有效性检查）");

        /* ---- 键盘分段导航与步进（对标 QDateTimeEdit 方向键语义） ---- */
        input_dtReset();
        XDateTimeEdit_setCurrentSectionIndex(&dt, 0);
        input_injectKey((XWidget*)&dt, XKey_Right, 0);
        XAPI_EXPECT(XDateTimeEdit_currentSectionIndex(&dt) == 1 &&
                    XDateTimeEdit_currentSection(&dt) ==
                        (int)XDateTimeEditSection_MonthSection,
                    "DateTimeEdit Right 键跨到月段（段间导航不循环）");
        input_injectKey((XWidget*)&dt, XKey_Up, 0);
        got = XDateTimeEdit_dateTime(&dt);
        XAPI_EXPECT(XDate_month(&got->m_date) == 7,
                    "DateTimeEdit Up 键步进月段 +1（2024-06→07）");
        XAPI_EXPECT(g_dtSig.userDateChanged == 1 &&
                    g_dtSig.userTimeChanged == 0,
                    "DateTimeEdit 步进改日期部分发射 userDateChanged");
        XAPI_EXPECT(g_dtSig.dateTimeChanged == 1 && g_dtSig.dateChanged == 1 &&
                    g_dtSig.timeChanged == 0,
                    "DateTimeEdit 步进按部分变化发射 dateChanged 不发 timeChanged");
        input_injectKey((XWidget*)&dt, XKey_Left, 0);
        input_injectKey((XWidget*)&dt, XKey_Up, 0);
        got = XDateTimeEdit_dateTime(&dt);
        XAPI_EXPECT(XDate_year(&got->m_date) == 2025,
                    "DateTimeEdit 回年段 Up 步进年 +1（2024→2025）");

        /* ---- calendarPopup/timeSpec/calendarWidget ---- */
        XDateTimeEdit_setCalendarPopup(&dt, true);
        XAPI_EXPECT(XDateTimeEdit_calendarPopup(&dt),
                    "DateTimeEdit setCalendarPopup(true) 往返");
        XDateTimeEdit_setCalendarPopup(&dt, false);
        XAPI_EXPECT(!XDateTimeEdit_calendarPopup(&dt),
                    "DateTimeEdit setCalendarPopup(false) 恢复");
        XDateTimeEdit_setTimeSpec(&dt, 1);
        XAPI_EXPECT(XDateTimeEdit_timeSpec(&dt) == 1,
                    "DateTimeEdit setTimeSpec(UTC=1) 往返");
        XDateTimeEdit_setTimeSpec(&dt, 0);
#if XCALENDARWIDGET_ON
        XAPI_EXPECT(XDateTimeEdit_calendarWidget(&dt) != NULL,
                    "DateTimeEdit calendarWidget 懒创建非空（对标 calendarWidget）");
#endif

        /* ---- NULL/越界边界 ---- */
        XAPI_EXPECT(XDateTimeEdit_sectionCount(NULL) == 0 &&
                    XDateTimeEdit_sectionAt(NULL, 0) ==
                        (int)XDateTimeEditSection_NoSection,
                    "DateTimeEdit NULL 对象分段查询走缺省值");
        XDateTimeEdit_setSelectedSection(NULL, 0);

        XDateTimeEdit_deinit_base(&dt);
    }
#endif /* XWIDGET_ON && XABSTRACTSPINBOX_ON && XDATETIMEEDIT_ON */

#if XWIDGET_ON && XCOMBOBOX_ON && XLINEEDIT_ON

    /* ================================================================
     * 5. XComboBox：下拉组合框（对标 Qt 6.8 QComboBox public API）。
     * ================================================================ */
    {
        XComboBox cb;
        static int s_dummyCarrier; /* validator/delegate 不透明承载探针 */

        XComboBox_init(&cb, NULL, 0);
        input_cbConnect(&cb);
        input_cbReset();

        /* ---- 默认值（QComboBox 构造默认 + XGui 载体口径） ---- */
        XAPI_EXPECT(XComboBox_count(&cb) == 0,
                    "ComboBox 默认 0 项");
        XAPI_EXPECT(XComboBox_currentIndex(&cb) == -1,
                    "ComboBox 默认无当前项（currentIndex=-1，Qt 同）");
        XAPI_EXPECT(XComboBox_maxVisibleItems(&cb) == 10,
                    "ComboBox 默认弹层最多 10 项（Qt maxVisibleItems 默认 10）");
        XAPI_EXPECT(XComboBox_insertPolicy(&cb) ==
                    (int)XComboBoxInsertPolicy_InsertAtBottom,
                    "ComboBox 默认底部插入（Qt InsertPolicy 默认 InsertAtBottom）");
        XAPI_EXPECT(XComboBox_sizeAdjustPolicy(&cb) ==
                    (int)XComboBoxSizeAdjustPolicy_AdjustToContents,
                    "ComboBox 默认尺寸策略 AdjustToContents（Qt 默认 AdjustToContentsOnFirstShow——XGui 头文件注明按此策略实现）");
        XAPI_EXPECT(XComboBox_minimumContentsLength(&cb) == 0,
                    "ComboBox 默认 minimumContentsLength=0（Qt 同）");
        XAPI_EXPECT(XComboBox_hasFrame(&cb),
                    "ComboBox 默认绘制边框（Qt frame 默认 true）");
        XAPI_EXPECT(!XComboBox_duplicatesEnabled(&cb),
                    "ComboBox 默认不允许重复项（Qt duplicatesEnabled 默认 false）");
        XAPI_EXPECT(!XComboBox_isEditable(&cb) &&
                    XComboBox_lineEdit(&cb) == NULL,
                    "ComboBox 默认不可编辑且无内嵌编辑框");
        XAPI_EXPECT(XComboBox_iconSize(&cb) == 16,
                    "ComboBox 默认图标 16px（XGui 方边简化承载；Qt 取决于样式，不按 Qt 断言）");
        XAPI_EXPECT(XComboBox_modelColumn(&cb) == 0,
                    "ComboBox 默认模型列 0（Qt modelColumn 默认 0）");
        {
            int rr = -9;
            int rc = -9;
            XComboBox_rootModelIndex(&cb, &rr, &rc);
            XAPI_EXPECT(rr == 0 && rc == 0,
                        "ComboBox 默认根索引 (0,0)（平铺承载全量根）");
        }
        XAPI_EXPECT(XComboBox_completer(&cb) == NULL &&
                    !XComboBox_isCompleterMode(&cb),
                    "ComboBox 默认无补全器且补全模式关闭");
        XAPI_EXPECT(xapi_cstr(XComboBox_placeholderText_2(&cb))[0] == '\0',
                    "ComboBox 默认占位文本=空串");
        XAPI_EXPECT(XComboBox_maxCount(&cb) == 2147483647,
                    "ComboBox 默认 maxCount=2147483647（Qt INT_MAX 默认）");
        XAPI_EXPECT(XComboBox_validator(&cb) == NULL &&
                    XComboBox_itemDelegate(&cb) == NULL,
                    "ComboBox 默认无校验器/委托");

        /* ---- 条目管理：addItem/插入不自动置当前项 ---- */
        XComboBox_addItem_2(&cb, "Alpha");
        XComboBox_addItem_2(&cb, "Beta");
        XComboBox_addItem_2(&cb, "Gamma");
        XAPI_EXPECT(XComboBox_count(&cb) == 3,
                    "ComboBox addItem_2 三项 count=3");
        XAPI_EXPECT(XComboBox_currentIndex(&cb) == -1,
                    "ComboBox 插入不自动置当前项（XGui 口径；Qt 首项自动置当前——已按回归行为断言，差异记录）");
        XAPI_EXPECT(g_cbSig.indexChanged == 0 && g_cbSig.textChangedCount == 0,
                    "ComboBox 插入不发 currentIndex/Text 变化信号");

        /* ---- setCurrentIndex 与信号 ---- */
        XComboBox_setCurrentIndex(&cb, 1);
        XAPI_EXPECT(XComboBox_currentIndex(&cb) == 1 &&
                    g_cbSig.indexChanged == 1 && g_cbSig.lastIndex == 1,
                    "ComboBox setCurrentIndex(1) 生效并发射 currentIndexChanged(1)");
        XAPI_EXPECT(strcmp(xapi_cstr(XComboBox_currentText_2(&cb)), "Beta") == 0 &&
                    g_cbSig.textChangedCount == 1 &&
                    strcmp(g_cbSig.lastText, "Beta") == 0,
                    "ComboBox 当前文本随之=Beta 并发射 currentTextChanged");
        XComboBox_setCurrentIndex(&cb, 1);
        XAPI_EXPECT(g_cbSig.indexChanged == 1,
                    "ComboBox 同索引 setCurrentIndex 无操作不发射");
        XComboBox_setCurrentIndex(&cb, 99);
        XAPI_EXPECT(XComboBox_currentIndex(&cb) == 2 &&
                    g_cbSig.lastIndex == 2,
                    "ComboBox 越界索引钳位到末项（Qt 钳位语义）");

        /* ---- findText/itemText/setItemText ---- */
        XAPI_EXPECT(XComboBox_findText_2(&cb, "Alpha") == 0 &&
                    XComboBox_findText_2(&cb, "Delta") == -1,
                    "ComboBox findText 命中返回索引/未命中 -1（Qt findText）");
        XAPI_EXPECT(strcmp(xapi_cstr(XComboBox_itemText_2(&cb, 2)), "Gamma") == 0,
                    "ComboBox itemText_2 按索引读文本");
        {
            XString* it = XComboBox_itemText(&cb, 1);
            XAPI_EXPECT(it != NULL && strcmp(xapi_u8(it), "Beta") == 0,
                        "ComboBox itemText XString 版本堆拷贝");
            XString_delete_base((XClass*)it);
        }
        XAPI_EXPECT(XComboBox_itemText(&cb, 99) == NULL &&
                    xapi_cstr(XComboBox_itemText_2(&cb, -1))[0] == '\0',
                    "ComboBox 越界 itemText 走 NULL/空串缺省");
        XComboBox_setItemText_2(&cb, 0, "Alpha2");
        XAPI_EXPECT(strcmp(xapi_cstr(XComboBox_itemText_2(&cb, 0)), "Alpha2") == 0,
                    "ComboBox setItemText_2 就地改文本");
        XComboBox_setItemText_2(&cb, 0, "Alpha2");
        XAPI_EXPECT(strcmp(xapi_cstr(XComboBox_itemText_2(&cb, 0)), "Alpha2") == 0,
                    "ComboBox 重复 setItemText 相同文本幂等（存储为重赋值，不保证指针稳定）");

        /* ---- 插入位置语义（insertItem 负数插最前/超出追加） ---- */
        XComboBox_insertItem_2(&cb, 0, "Zero");
        XAPI_EXPECT(strcmp(xapi_cstr(XComboBox_itemText_2(&cb, 0)), "Zero") == 0,
                    "ComboBox insertItem(0) 头插");
        XComboBox_insertItem_2(&cb, -5, "Neg");
        XAPI_EXPECT(strcmp(xapi_cstr(XComboBox_itemText_2(&cb, 0)), "Neg") == 0,
                    "ComboBox 负索引插最前（Qt insertItem 语义）");
        XComboBox_insertItem_2(&cb, 999, "Tail");
        XAPI_EXPECT(strcmp(XComboBox_itemText_2(&cb, XComboBox_count(&cb) - 1),
                           "Tail") == 0,
                    "ComboBox 超界索引追加到尾部");
        {
            static const char* const kBatch[] = { "B1", "B2", NULL };
            int before = XComboBox_count(&cb);
            XComboBox_insertItems_2(&cb, 2, kBatch);
            XAPI_EXPECT(XComboBox_count(&cb) == before + 2 &&
                        strcmp(xapi_cstr(XComboBox_itemText_2(&cb, 2)), "B1") == 0,
                        "ComboBox insertItems_2 从索引 2 批量插入");
        }
        {
            static const char* const kBatch[] = { "C1", "C2", NULL };
            int before = XComboBox_count(&cb);
            XComboBox_addItems_2(&cb, kBatch);
            XAPI_EXPECT(XComboBox_count(&cb) == before + 2,
                        "ComboBox addItems_2 批量追加");
        }
        {
            XString* s = XString_create_utf8("S1");
            XComboBox_insertItem(&cb, 2, s);
            XString_delete_base((XClass*)s);
            XAPI_EXPECT(strcmp(xapi_cstr(XComboBox_itemText_2(&cb, 2)), "S1") == 0,
                        "ComboBox insertItem(XString) 深拷贝插入");
        }

        /* ---- insertSeparator/removeItem/clear ---- */
        {
            int before = XComboBox_count(&cb);
            XComboBox_insertSeparator(&cb, 1);
            XAPI_EXPECT(XComboBox_count(&cb) == before + 1,
                        "ComboBox insertSeparator 增加一个分隔条目（Qt 同名语义）");
        }
        {
            int before = XComboBox_count(&cb);
            XComboBox_removeItem(&cb, 999);
            XAPI_EXPECT(XComboBox_count(&cb) == before,
                        "ComboBox removeItem 越界无操作");
            XComboBox_removeItem(&cb, 0);
            XAPI_EXPECT(XComboBox_count(&cb) == before - 1,
                        "ComboBox removeItem(0) 移除并左移");
        }
        XComboBox_clear(&cb);
        XAPI_EXPECT(XComboBox_count(&cb) == 0 &&
                    XComboBox_currentIndex(&cb) == -1,
                    "ComboBox clear 清空条目并复位当前项（Qt clear 语义）");

        /* ---- maxCount 钳位（超限丢弃，Qt 语义） ---- */
        XComboBox_setMaxCount(&cb, 4);
        XAPI_EXPECT(XComboBox_maxCount(&cb) == 4,
                    "ComboBox setMaxCount 往返");
        {
            static const char* const kSix[] = { "1", "2", "3", "4", "5", "6",
                                                NULL };
            XComboBox_addItems_2(&cb, kSix);
            XAPI_EXPECT(XComboBox_count(&cb) == 4,
                        "ComboBox 超过 maxCount 的追加被丢弃（Qt maxCount 语义）");
        }
        XComboBox_setMaxCount(&cb, 2147483647);

        /* ---- 项数据/图标/findData/currentData ---- */
        XComboBox_clear(&cb);
        XComboBox_addItem_2(&cb, "A1");
        XComboBox_addItem_2(&cb, "B1");
        XComboBox_setItemData_2(&cb, 0, "d0");
        XAPI_EXPECT(strcmp(xapi_cstr(XComboBox_itemData_2(&cb, 0)), "d0") == 0,
                    "ComboBox setItemData/itemData 字符串往返");
        XComboBox_setCurrentIndex(&cb, 0);
        XAPI_EXPECT(XComboBox_currentData(&cb) != NULL &&
                    strcmp(xapi_u8(XComboBox_currentData(&cb)),
                           "d0") == 0,
                    "ComboBox currentData=currentItem 的数据（Qt currentData）");
        XAPI_EXPECT(XComboBox_findData_2(&cb, "d0") == 0 &&
                    XComboBox_findData_2(&cb, "nope") == -1,
                    "ComboBox findData 按数据查找命中/未命中");
        XComboBox_setItemIcon_2(&cb, 1, "i.png");
        XAPI_EXPECT(strcmp(xapi_cstr(XComboBox_itemIcon_2(&cb, 1)), "i.png") == 0,
                    "ComboBox setItemIcon/itemIcon 路径往返");
        XComboBox_setIconSize(&cb, 24);
        XAPI_EXPECT(XComboBox_iconSize(&cb) == 24,
                    "ComboBox setIconSize 往返");

        /* ---- 策略/开关属性往返 ---- */
        XComboBox_setInsertPolicy(
            &cb, (int)XComboBoxInsertPolicy_InsertAlphabetically);
        XAPI_EXPECT(XComboBox_insertPolicy(&cb) ==
                    (int)XComboBoxInsertPolicy_InsertAlphabetically,
                    "ComboBox setInsertPolicy 往返");
        XComboBox_setInsertPolicy(&cb,
                                  (int)XComboBoxInsertPolicy_InsertAtBottom);
        XComboBox_setSizeAdjustPolicy(
            &cb,
            (int)XComboBoxSizeAdjustPolicy_AdjustToMinimumContentsLengthWithIcon);
        XAPI_EXPECT(XComboBox_sizeAdjustPolicy(&cb) ==
                    (int)XComboBoxSizeAdjustPolicy_AdjustToMinimumContentsLengthWithIcon,
                    "ComboBox setSizeAdjustPolicy 往返");
        XComboBox_setSizeAdjustPolicy(
            &cb, (int)XComboBoxSizeAdjustPolicy_AdjustToContents);
        XComboBox_setMinimumContentsLength(&cb, 8);
        XAPI_EXPECT(XComboBox_minimumContentsLength(&cb) == 8,
                    "ComboBox setMinimumContentsLength 往返");
        XComboBox_setMinimumContentsLength(&cb, 0);
        XComboBox_setDuplicatesEnabled(&cb, true);
        XAPI_EXPECT(XComboBox_duplicatesEnabled(&cb),
                    "ComboBox setDuplicatesEnabled 往返");
        XComboBox_setDuplicatesEnabled(&cb, false);
        XComboBox_setFrame(&cb, false);
        XAPI_EXPECT(!XComboBox_hasFrame(&cb),
                    "ComboBox setFrame(false) 往返");
        XComboBox_setFrame(&cb, true);

        /* ---- 可编辑路径（editText 分叉/currentText/editTextChanged） ---- */
        XComboBox_setEditable(&cb, true);
        XAPI_EXPECT(XComboBox_isEditable(&cb) &&
                    XComboBox_lineEdit(&cb) != NULL,
                    "ComboBox setEditable(true) 创建内嵌编辑框");
        input_cbReset();
        XComboBox_setEditText_2(&cb, "half");
        XAPI_EXPECT(strcmp(xapi_cstr(XComboBox_currentText_2(&cb)), "half") == 0,
                    "ComboBox 可编辑分叉态 currentText=编辑框文本（Qt editable 语义）");
        XAPI_EXPECT(g_cbSig.editChanged == 1 &&
                    strcmp(g_cbSig.lastEdit, "half") == 0,
                    "ComboBox setEditText 发射 editTextChanged");
        XComboBox_clearEditText(&cb);
        XAPI_EXPECT(xapi_cstr(XComboBox_currentText_2(&cb))[0] == '\0',
                    "ComboBox clearEditText 清空分叉文本（分叉态 currentText=空）");
        XComboBox_setCurrentText_2(&cb, "B1");
        XAPI_EXPECT(XComboBox_currentIndex(&cb) == 1 &&
                    strcmp(xapi_cstr(XComboBox_currentText_2(&cb)), "B1") == 0,
                    "ComboBox setCurrentText 命中已有项置当前（Qt setCurrentText）");
        {
            XComboBox_setPlaceholderText_2(&cb, "请选择");
            XAPI_EXPECT(strcmp(XComboBox_placeholderText_2(&cb),
                               "请选择") == 0,
                        "ComboBox placeholderText UTF-8 往返");
            {
                XString* ph = XComboBox_placeholderText(&cb);
                XAPI_EXPECT(ph != NULL &&
                            strcmp(xapi_u8(ph), "请选择") == 0,
                            "ComboBox placeholderText XString 版本堆拷贝");
                XString_delete_base((XClass*)ph);
            }
            XComboBox_setPlaceholderText_2(&cb, NULL);
            XAPI_EXPECT(xapi_cstr(XComboBox_placeholderText_2(&cb))[0] == '\0',
                        "ComboBox placeholder(NULL) 等价空串");
        }
        XComboBox_setEditable(&cb, false);
        XAPI_EXPECT(!XComboBox_isEditable(&cb) &&
                    XComboBox_lineEdit(&cb) == NULL,
                    "ComboBox setEditable(false) 收起内嵌编辑框（XGui 销毁承载；Qt 保留编辑框——按实现口径断言）");

        /* ---- view/model/validator/delegate 承载族 ---- */
        XAPI_EXPECT(XComboBox_view(&cb) != NULL,
                    "ComboBox view 懒创建非空（对标 QComboBox::view）");
        XAPI_EXPECT(XComboBox_model(&cb) != NULL,
                    "ComboBox model 懒创建非空并随条目同步");
        XComboBox_setValidator(&cb, &s_dummyCarrier);
        XAPI_EXPECT(XComboBox_validator(&cb) == &s_dummyCarrier,
                    "ComboBox validator 不透明指针承载往返（XValidator 未建，头文件注明）");
        XComboBox_setValidator(&cb, NULL);
        XComboBox_setItemDelegate(&cb, &s_dummyCarrier);
        XAPI_EXPECT(XComboBox_itemDelegate(&cb) == &s_dummyCarrier,
                    "ComboBox itemDelegate 不透明指针承载往返");
        XComboBox_setItemDelegate(&cb, NULL);
        XComboBox_setRootModelIndex(&cb, 0, 0);
        XComboBox_setModelColumn(&cb, 0);
        XAPI_EXPECT(XComboBox_modelColumn(&cb) == 0,
                    "ComboBox setModelColumn(0) 往返");
        {
            XString* im = XComboBox_inputMethodQuery(&cb, 0);
            XAPI_EXPECT(im != NULL &&
                        strcmp(xapi_u8(im), "B1") == 0,
                        "ComboBox inputMethodQuery 非可编辑返回当前项文本");
            XString_delete_base((XClass*)im);
        }

        /* ---- 补全模式开关 ---- */
        XComboBox_setCompleterMode(&cb, true);
        XAPI_EXPECT(XComboBox_isCompleterMode(&cb),
                    "ComboBox setCompleterMode(true) 往返（内建前缀补全过滤）");
        XComboBox_setCompleter(&cb, NULL);
        XAPI_EXPECT(XComboBox_completer(&cb) == NULL,
                    "ComboBox setCompleter(NULL) 仅解除指针");
        XComboBox_setCompleterMode(&cb, false);
        XAPI_EXPECT(!XComboBox_isCompleterMode(&cb),
                    "ComboBox setCompleterMode(false) 关闭");

        /* ---- 弹出状态与信号（showPopup/hidePopup） ---- */
        input_cbReset();
        XComboBox_showPopup_base(&cb);
        XAPI_EXPECT(XComboBox_popupVisible(&cb) && g_cbSig.popupShown == 1,
                    "ComboBox showPopup 置可见并发射 popupShown");
        XComboBox_hidePopup_base(&cb);
        XAPI_EXPECT(!XComboBox_popupVisible(&cb) && g_cbSig.popupHidden == 1,
                    "ComboBox hidePopup 收起并发射 popupHidden");

        XComboBox_deinit_base(&cb);
    }
#endif /* XWIDGET_ON && XCOMBOBOX_ON && XLINEEDIT_ON */

#if XWIDGET_ON && XCOMBOBOX_ON && XFONTCOMBOBOX_ON

    /* ================================================================
     * 6. XFontComboBox：字体族下拉（对标 Qt 6.8 QFontComboBox）。
     * ================================================================ */
    {
        XFontComboBox fcb;
        XFont font;
        XFont gotFont;
        XString* sample;

        XFontComboBox_init(&fcb, NULL, 0);
        input_fcbReset();

        /* ---- 默认值 ---- */
        XAPI_EXPECT(XFontComboBox_fontFilters(&fcb) ==
                    (int)XFontComboBoxFilter_AllFonts,
                    "FontComboBox 默认过滤器 AllFonts（Qt 同）");
        XAPI_EXPECT(XFontComboBox_writingSystem(&fcb) ==
                    (int)XFontComboBoxWritingSystem_Any,
                    "FontComboBox 默认书写系统 Any（Qt::Any 同值）");
        XAPI_EXPECT(XComboBox_count((XComboBox*)&fcb) >= 1,
                    "FontComboBox 构造后条目非空（字体数据库或回退族表）");
        XAPI_EXPECT(XComboBox_currentIndex((XComboBox*)&fcb) == 0,
                    "FontComboBox 构造后置首项为当前（populate 收尾）");
        XAPI_EXPECT(strcmp(xapi_cstr(XFontComboBox_currentFamily(&fcb)), "") != 0,
                    "FontComboBox currentFamily 非空（当前条目族名，对标 currentFont().family()）");

        /* ---- 过滤器/书写系统往返与非法值 ---- */
        XFontComboBox_setFontFilters(
            &fcb, (int)XFontComboBoxFilter_MonospacedFonts |
                  (int)XFontComboBoxFilter_ScalableFonts);
        XAPI_EXPECT(XFontComboBox_fontFilters(&fcb) ==
                    ((int)XFontComboBoxFilter_MonospacedFonts |
                     (int)XFontComboBoxFilter_ScalableFonts),
                    "FontComboBox setFontFilters 位组合往返");
        XFontComboBox_setFontFilters(&fcb, (int)XFontComboBoxFilter_AllFonts);
        XFontComboBox_setWritingSystem(
            &fcb, (int)XFontComboBoxWritingSystem_SimplifiedChinese);
        XAPI_EXPECT(XFontComboBox_writingSystem(&fcb) ==
                    (int)XFontComboBoxWritingSystem_SimplifiedChinese,
                    "FontComboBox setWritingSystem(25) 往返（Qt::WritingSystem 同值）");
        XFontComboBox_setWritingSystem(&fcb, 7);
        XAPI_EXPECT(XFontComboBox_writingSystem(&fcb) ==
                    (int)XFontComboBoxWritingSystem_SimplifiedChinese,
                    "FontComboBox 非法书写系统(7) 忽略保持原值（子集成员校验）");
        XFontComboBox_setWritingSystem(&fcb,
                                       (int)XFontComboBoxWritingSystem_Any);

        /* ---- 族名选中与 currentFontChanged 信号 ---- */
        XObject_connect_1(
            (XObject*)&fcb,
            (size_t)XFontComboBox_currentFontChanged_signal(NULL, NULL),
            (XObject*)&fcb, input_fcbFontChangedSlot, XConnectionType_Direct);
        input_fcbReset();
        XComboBox_addItem_2((XComboBox*)&fcb, "XApiTestFamily");
        XFontComboBox_setCurrentFamily(&fcb, "XApiTestFamily");
        XAPI_EXPECT(strcmp(XFontComboBox_currentFamily(&fcb),
                           "XApiTestFamily") == 0,
                    "FontComboBox setCurrentFamily 命中条目置当前");
        XAPI_EXPECT(g_fcbFontChanged == 1 &&
                    strcmp(g_fcbLastFamily, "XApiTestFamily") == 0,
                    "FontComboBox 族名选中发射 currentFontChanged 且载荷为族名");
        XAPI_EXPECT(strcmp(XFontComboBox_currentFont(&fcb),
                           "XApiTestFamily") == 0,
                    "FontComboBox currentFont 宏别名=族名（对标 currentFont().family()）");
        XFontComboBox_setCurrentFamily(&fcb, "NoSuchFamily#");
        XAPI_EXPECT(strcmp(XFontComboBox_currentFamily(&fcb),
                           "XApiTestFamily") == 0,
                    "FontComboBox setCurrentFamily 未命中条目为无操作");
        XFontComboBox_setCurrentFamily(&fcb, NULL);
        XAPI_EXPECT(strcmp(XFontComboBox_currentFamily(&fcb),
                           "XApiTestFamily") == 0,
                    "FontComboBox setCurrentFamily(NULL) 忽略");
        XFontComboBox_setCurrentFont(&fcb, "XApiTestFamily");
        XAPI_EXPECT(strcmp(XFontComboBox_currentFont(&fcb),
                           "XApiTestFamily") == 0,
                    "FontComboBox setCurrentFont 转发 setCurrentFamily");
        XObject_disconnect_1(
            (XObject*)&fcb,
            (size_t)XFontComboBox_currentFontChanged_signal(NULL, NULL),
            (XObject*)&fcb, input_fcbFontChangedSlot);

        /* ---- 显示字体状态（setDisplayFont/displayFont 值承载） ---- */
        XFont_init_ex(&font, "XApiSerif", 12, -1, false);
        XFontComboBox_setDisplayFont(&fcb, &font);
        gotFont = XFontComboBox_displayFont(&fcb);
        XAPI_EXPECT(strcmp(xapi_cstr(XFont_family(&gotFont)), "XApiSerif") == 0,
                    "FontComboBox setDisplayFont/displayFont 深拷贝往返");
        XClass_deinit_base((XClass*)&gotFont);
        XClass_deinit_base((XClass*)&font);

        /* ---- 样例文本（族名回退/自定义/移除） ---- */
        sample = XFontComboBox_sampleTextForFont(&fcb, "XApiTestFamily");
        XAPI_EXPECT(sample != NULL &&
                    strcmp(xapi_u8(sample), "XApiTestFamily") == 0,
                    "FontComboBox 未采样时 sampleTextForFont 回退族名（头文件口径）");
        XString_delete_base((XClass*)sample);
        XFontComboBox_setSampleTextForFont(&fcb, "XApiTestFamily",
                                           "ABC xyz");
        sample = XFontComboBox_sampleTextForFont(&fcb, "XApiTestFamily");
        XAPI_EXPECT(sample != NULL &&
                    strcmp(xapi_u8(sample), "ABC xyz") == 0,
                    "FontComboBox 自定义族样例文本往返");
        XString_delete_base((XClass*)sample);
        XFontComboBox_setSampleTextForFont(&fcb, "XApiTestFamily", NULL);
        sample = XFontComboBox_sampleTextForFont(&fcb, "XApiTestFamily");
        XAPI_EXPECT(sample != NULL &&
                    strcmp(xapi_u8(sample), "XApiTestFamily") == 0,
                    "FontComboBox sample=NULL 移除自定义回退默认");
        XString_delete_base((XClass*)sample);
        XAPI_EXPECT(XFontComboBox_sampleTextForFont(&fcb, NULL) == NULL,
                    "FontComboBox sampleTextForFont(NULL 族名) 返回 NULL");
        XAPI_EXPECT(XFontComboBox_sampleTextForSystem(
                        &fcb, (int)XFontComboBoxWritingSystem_Latin) != NULL,
                    "FontComboBox 合法书写系统样例查询非空（回退当前族名）");
        XAPI_EXPECT(XFontComboBox_sampleTextForSystem(&fcb, -1) == NULL,
                    "FontComboBox 非法书写系统样例查询返回 NULL");

        XComboBox_deinit_base(&fcb);
    }
#endif /* XWIDGET_ON && XCOMBOBOX_ON && XFONTCOMBOBOX_ON */

#if !XWIDGET_ON
/* 输入族整体裁剪时的非空翻译单元哨兵。 */
typedef int xgui_demo_apitest_input_disabled_sentinel;
#endif /* !XWIDGET_ON */

    return failures;
}
