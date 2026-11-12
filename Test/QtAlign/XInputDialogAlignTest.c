/**
 * @file       XInputDialogAlignTest.c
 * @brief      XInputDialog × Qt 6.8 QInputDialog 对齐契约测试。
 * @details    对齐依据：/opt/Qt/6.8.3/gcc_64/include/QtWidgets/qinputdialog.h
 *             与 Qt 6.8 源码 src/widgets/dialogs/qinputdialog.cpp（jsdelivr
 *             v6.8.0 原文），关键行为用本机 Qt 6.8.0（QT_QPA_PLATFORM=
 *             offscreen）探针实测：
 *             - 值 setter 会切换 inputMode（setIntValue→IntInput、
 *               setDoubleValue→DoubleInput、setTextValue→TextInput，
 *               qinputdialog.cpp:690-707/768-773/884-889；实测 I1~I3）；
 *             - 值按当前范围钳位（QSpinBox/QDoubleSpinBox::setValue
 *               语义；实测默认范围 0..99 / 0..99.99 下
 *               setIntValue(500)→99、setDoubleValue(1234.5)→99.99）；
 *             - done(result!=0) 按当前模式发 textValueSelected/
 *               intValueSelected/doubleValueSelected，done(0) 不发
 *               （qinputdialog.cpp:1101-1121；实测 I4 顺序
 *               accepted→finished→textValueSelected、I5 无确认信号）。
 *
 *             **Qt 6.8 对齐项**（2026-11-11 由已登记偏差转为实现）：
 *             - 默认 int 范围 0..99、double 0..99.99（qinputdialog.cpp:
 *               786-830/894-940 的 `d->intSpinBox ? ... : 0` 分支；
 *               本机探针 I0 实测）；
 *             - labelText 缺省 "Enter a value:"（qinputdialog.cpp:
 *               203-207 ensureLayout 建 QLabel；探针 Z0）；
 *             - setVisible(true) 聚焦输入控件并全选（qinputdialog.cpp:
 *               1060-1080；探针 Z1/Z2：文本模式 QLineEdit 全选、整数/
 *               浮点模式自旋框聚焦）。静态便捷路径保留现场「点击输入框
 *               才弹虚拟键盘」裁定（m_suppressAutoFocus，见 XInputDialog.c
 *               xid_execDialog 注），实例 show() 路径按 Qt 语义。
 * @author     XinYueC 团队
 */
#include "xq_align.h"

#include "XGuiConfig.h"

#if XGUI_ON && XWIDGET_ON && XDIALOG_ON

#include "XInputDialog.h"
#include "XDialog.h"
#include "XWidget.h"
#include "XObject.h"
#include "XString.h"
#include "XStringList.h"
#include "XLineEdit.h"
#include "XSpinBox.h"
#include "XComboBox.h"
#include "XPlainTextEdit.h"
#include "XLabel.h"
#include "XBoxLayout.h"
#include "XLayout.h"
#include "XVarList.h"
#include "XClass.h"
#include "XMemory.h"

static int g_xidTextSel;    /**< textValueSelected 次数。 */
static int g_xidIntSel;     /**< intValueSelected 次数。 */
static int g_xidDoubleSel;  /**< doubleValueSelected 次数。 */
static int g_xidIntChg;     /**< intValueChanged 次数。 */
static int g_xidLastInt;    /**< 最近 int 载荷。 */
static char g_xidText[64];  /**< 最近文本载荷（UTF-8）。 */

static void xidTextSelSlot(XObject* receiver, XVarList* args)
{
    (void)receiver;
    XVarList_args_1(args, XString*, text);
    ++g_xidTextSel;
    {
        const char* utf8 = text ? XString_toUtf8(text) : "";
        snprintf(g_xidText, sizeof(g_xidText), "%s", utf8 ? utf8 : "");
    }
}

static void xidIntSelSlot(XObject* receiver, XVarList* args)
{
    (void)receiver;
    XVarList_args_1(args, int, value);
    ++g_xidIntSel;
    g_xidLastInt = value;
}

static void xidDoubleSelSlot(XObject* receiver, XVarList* args)
{
    (void)receiver; (void)args;
    ++g_xidDoubleSel;
}

static void xidIntChgSlot(XObject* receiver, XVarList* args)
{
    (void)receiver;
    XVarList_args_1(args, int, value);
    ++g_xidIntChg;
    g_xidLastInt = value;
}

/** @brief 清空计数。 */
static void xidSigReset(void)
{
    g_xidTextSel = 0;
    g_xidIntSel = 0;
    g_xidDoubleSel = 0;
    g_xidIntChg = 0;
    g_xidLastInt = -1;
    g_xidText[0] = '\0';
}

/** @brief 空串安全转换：本库 XString_toUtf8 对空串返回 NULL。 */
static const char* xidCStr(const char* s)
{
    return s ? s : "";
}

/** @brief 按 Qt 私有子对象名取对话框直接子控件（XObject::findChild 的
 *  测试侧封装；实例懒建与静态便捷路径共用同一批名字）。 */
static XWidget* xidTestChild(XInputDialog* dlg, const char* name)
{
    XString tmp;
    XObject* obj;
    XString_init(&tmp);
    XString_assign_utf8(&tmp, name);
    obj = XObject_findChild((XObject*)dlg, &tmp,
                            XFindChildOption_DirectChildrenOnly);
    XClassDeinit((XClass*)&tmp);
    return (XWidget*)obj;
}

/* ==================== ① 构造默认值 ==================== */

XQ_TEST(XInputDialog, constructorDefaults)
{
    XInputDialog* dlg = XInputDialog_create(NULL, 0);
    XString* text;
    XQ_REQUIRE(dlg != NULL);

    /* qinputdialog.cpp:509-520：inputMode 缺省 TextInput。 */
    XQ_EQ_I64(XInputDialog_inputMode(dlg), XInputDialog_TextInput);
    XQ_EQ_I64(XInputDialog_options(dlg), 0);
    XQ_EQ_I64(XInputDialog_intValue(dlg), 0);
    XQ_CHECK(XInputDialog_doubleValue(dlg) == 0.0);
    XQ_EQ_I64(XInputDialog_intStep(dlg), 1);
    XQ_EQ_I64(XInputDialog_doubleDecimals(dlg), 2);
    XQ_CHECK(XInputDialog_doubleStep(dlg) == 1.0);
    XQ_CHECK(!XInputDialog_isComboBoxEditable(dlg));
    XQ_EQ_I64(XInputDialog_textEchoMode(dlg),
              XInputDialogEchoMode_Normal);
    /* Qt 6.8 缺省范围：QSpinBox 0..99 / QDoubleSpinBox 0..99.99
     * （qinputdialog.cpp:786-830/894-940；本机探针 I0）。 */
    XQ_EQ_I64(XInputDialog_intMinimum(dlg), 0);
    XQ_EQ_I64(XInputDialog_intMaximum(dlg), 99);
    XQ_CHECK(XInputDialog_doubleMinimum(dlg) == 0.0);
    XQ_CHECK(XInputDialog_doubleMaximum(dlg) == 99.99);
    /* labelText 缺省 "Enter a value:"（qinputdialog.cpp:203-207；探针 Z0）。 */
    {
        XString* got = XInputDialog_labelText(dlg);
        XQ_REQUIRE(got != NULL);
        XQ_EQ_STR(xidCStr(XString_toUtf8(got)), "Enter a value:");
        XClassDelete((XClass*)got);
    }

    text = XInputDialog_textValue(dlg);
    XQ_REQUIRE(text != NULL);
    XQ_EQ_STR(xidCStr(XString_toUtf8(text)), "");
    XClassDelete((XClass*)text);

    /* 选项位往返（qinputdialog.h:43-47 位值）。 */
    XInputDialog_setOption(dlg, XInputDialog_NoButtons, true);
    XQ_CHECK(XInputDialog_testOption(dlg, XInputDialog_NoButtons));
    XInputDialog_setOptions(dlg,
                            (XInputDialogOptions)XInputDialog_NoButtons |
                                (XInputDialogOptions)
                                    XInputDialog_UsePlainTextEditForTextInput);
    XQ_EQ_I64(XInputDialog_options(dlg), 0x5);

    XClassDelete(dlg);
}

/* ==================== ② 值 setter 切模式 + 范围钳位 ==================== */

XQ_TEST(XInputDialog, valueSetterModeAndClamp)
{
    XInputDialog* dlg = XInputDialog_create(NULL, 0);
    XQ_REQUIRE(dlg != NULL);
    xidSigReset();
    XObject_connect((XObject*)dlg,
                      XSignal(XInputDialog_intValueChanged_signal),
                      (XObject*)dlg, xidIntChgSlot,
                      XConnectionType_Direct);

    /* qinputdialog.cpp:768-773：setIntValue → setInputMode(IntInput)。 */
    XInputDialog_setIntRange(dlg, 0, 10);
    XInputDialog_setIntValue(dlg, 20);
    XQ_EQ_I64(XInputDialog_intValue(dlg), 10);
    XQ_EQ_I64(XInputDialog_inputMode(dlg), XInputDialog_IntInput);
    XQ_EQ_I64(g_xidIntChg, 1);
    XQ_EQ_I64(g_xidLastInt, 10);

    /* qinputdialog.cpp:884-889：setDoubleValue → DoubleInput + 钳位。 */
    XInputDialog_setDoubleRange(dlg, -1.0, 1.0);
    XInputDialog_setDoubleValue(dlg, 2.0);
    XQ_CHECK(XInputDialog_doubleValue(dlg) == 1.0);
    XQ_EQ_I64(XInputDialog_inputMode(dlg), XInputDialog_DoubleInput);

    /* qinputdialog.cpp:690-707：setTextValue → 切回 TextInput。 */
    {
        XString* t = XString_create_utf8("abc");
        XQ_REQUIRE(t != NULL);
        XInputDialog_setTextValue(dlg, t);
        XClassDelete((XClass*)t);
    }
    XQ_EQ_I64(XInputDialog_inputMode(dlg), XInputDialog_TextInput);

    XClassDelete(dlg);
}

/* ==================== ②b 范围边界收敛与值钳位（QSpinBox 同口径） ====== */

XQ_TEST(XInputDialog, rangeBoundaryConvergence)
{
    XInputDialog* dlg = XInputDialog_create(NULL, 0);
    XQ_REQUIRE(dlg != NULL);

    /* qspinbox.cpp setRange：当前值钳入新范围（探针 I6：
     * setIntRange(10,20) 后 value=10）。 */
    XInputDialog_setIntRange(dlg, 10, 20);
    XQ_EQ_I64(XInputDialog_intValue(dlg), 10);

    /* setMinimum(min>max) → max 收敛 min、value=min
     * （探针 I6：10..20 上 setMinimum(30) → 30..30、value=30）。 */
    XInputDialog_setIntMinimum(dlg, 30);
    XQ_EQ_I64(XInputDialog_intMinimum(dlg), 30);
    XQ_EQ_I64(XInputDialog_intMaximum(dlg), 30);
    XQ_EQ_I64(XInputDialog_intValue(dlg), 30);

    /* setMaximum(max<value) → value 钳到 max（探针 I6：10..20 上
     * setIntValue(15)/setMaximum(12) → value=12）。 */
    XInputDialog_setIntRange(dlg, 10, 20); /* 自上一步 30..30 复位。 */
    XInputDialog_setIntValue(dlg, 15);
    XInputDialog_setIntMaximum(dlg, 12);
    XQ_EQ_I64(XInputDialog_intValue(dlg), 12);

    /* double 同口径（探针 I6：setDoubleRange(1.5,4.5)→value=1.5；
     * setDoubleRange(10.0,5.0)→10.0..10.0、value=10.0）。 */
    XInputDialog_setDoubleRange(dlg, 1.5, 4.5);
    XQ_CHECK(XInputDialog_doubleValue(dlg) == 1.5);
    XInputDialog_setDoubleRange(dlg, 10.0, 5.0);
    XQ_CHECK(XInputDialog_doubleMinimum(dlg) == 10.0);
    XQ_CHECK(XInputDialog_doubleMaximum(dlg) == 10.0);
    XQ_CHECK(XInputDialog_doubleValue(dlg) == 10.0);

    XClassDelete(dlg);
}

/* ==================== ②c setVisible(true) 聚焦输入控件并全选 ========== */

XQ_TEST(XInputDialog, showFocusesInputAndSelectsAll)
{
    XInputDialog* dlg = XInputDialog_create(NULL, 0);
    XLineEdit* edit;
    XQ_REQUIRE(dlg != NULL);
    /* 实例路径不再手搓子控件：show 时按 inputMode 懒建行编辑
     * （qinputdialog.cpp:190-240 的 ensureLayout/ensureLineEdit）。 */
    {
        XString* t = XString_create_utf8("hello");
        XQ_REQUIRE(t != NULL);
        XInputDialog_setTextValue(dlg, t);
        XClassDelete((XClass*)t);
    }
    XInputDialog_setInputMode(dlg, XInputDialog_TextInput);

    /* qinputdialog.cpp:1060-1080：show → inputWidget->setFocus() +
     * QLineEdit::selectAll()（探针 Z1/Z2）。 */
    XWidget_show((XWidget*)dlg);
    edit = (XLineEdit*)xidTestChild(dlg, "qt_input_dialog_edit");
    XQ_REQUIRE(edit != NULL);
    XQ_EQ_STR(xidCStr(XLineEdit_text(edit)), "hello");
    XQ_CHECK(XWidget_hasFocus((XWidget*)edit));
    XQ_CHECK(XLineEdit_hasSelectedText(edit));
    XQ_EQ_I64(XLineEdit_selectionStart(edit), 0);

    XWidget_hide((XWidget*)dlg);
    XClassDelete(dlg); /* 懒建子控件随父析构释放。 */
}

/* ==================== ②d 实例懒建布局：三段装配 ====================
 * 对标 QInputDialogPrivate::ensureLayout（qinputdialog.cpp:196-240）：
 * label → inputWidget → buttonBox 竖排；控件按 inputMode 懒建。 */

XQ_TEST(XInputDialog, instanceBuildsLayoutOnShow)
{
    XInputDialog* dlg = XInputDialog_create(NULL, 0);
    XLabel* label;
    XSpinBox* spin;
    XQ_REQUIRE(dlg != NULL);
    {
        XString* t = XString_create_utf8("端口(1-5)");
        XQ_REQUIRE(t != NULL);
        XInputDialog_setLabelText(dlg, t);
        XClassDelete((XClass*)t);
    }
    XInputDialog_setIntRange(dlg, 1, 5);
    XInputDialog_setIntStep(dlg, 1);
    XInputDialog_setIntValue(dlg, 3);

    /* show 前**不建任何子控件**（懒建；对标 Qt 在 ensureLayout 前
     * d->label/inputWidget/buttonBox 均为空）。 */
    XQ_CHECK(XWidget_layout((XWidget*)dlg) == NULL);
    XQ_CHECK(xidTestChild(dlg, "qt_input_dialog_spin") == NULL);
    XQ_CHECK(xidTestChild(dlg, "qt_input_dialog_label") == NULL);

    XWidget_show((XWidget*)dlg);
    /* 根布局三段：标签 / 输入控件 / 按钮行（qinputdialog.cpp:215-221
     * addWidget 顺序；无按钮盒构建时按钮直挂根布局、条目数不同）。 */
    XQ_REQUIRE(XWidget_layout((XWidget*)dlg) != NULL);
#if XDIALOGBUTTONBOX_ON
    XQ_EQ_I64(XLayout_count_base(XWidget_layout((XWidget*)dlg)), 3);
#else
    XQ_EQ_I64(XLayout_count_base(XWidget_layout((XWidget*)dlg)), 5);
#endif
    /* 标签：懒建且文本来自 labelText（qinputdialog.cpp:203-207）。 */
    label = (XLabel*)xidTestChild(dlg, "qt_input_dialog_label");
    XQ_REQUIRE(label != NULL);
    XQ_EQ_STR(xidCStr(XString_toUtf8(XLabel_text(label))), "端口(1-5)");
    /* 输入控件：IntInput ⇒ 自旋框，范围/步进/值来自实例存储。 */
    spin = (XSpinBox*)xidTestChild(dlg, "qt_input_dialog_spin");
    XQ_REQUIRE(spin != NULL);
    XQ_EQ_I64(XSpinBox_minimum(spin), 1);
    XQ_EQ_I64(XSpinBox_maximum(spin), 5);
    XQ_EQ_I64(XSpinBox_singleStep(spin), 1);
    XQ_EQ_I64(XSpinBox_value(spin), 3);
    XQ_CHECK(XWidget_isVisible((XWidget*)spin));
    XQ_CHECK(XWidget_hasFocus((XWidget*)spin));
#if XDIALOGBUTTONBOX_ON
    /* 按钮行：标准 Ok/Cancel 盒（qinputdialog.cpp:208-215）。 */
    XQ_CHECK(xidTestChild(dlg, "qt_input_dialog_buttons") != NULL);
#endif

    XWidget_hide((XWidget*)dlg);
    XClassDelete(dlg);
}

/* ==================== ②e 建布局后按模式换装输入控件 ==================
 * 对标 setInputMode → ensure*SpinBox/chooseRightTextInputWidget →
 * setInputWidget（qinputdialog.cpp:509-533 + :319-354）。 */

XQ_TEST(XInputDialog, instanceSwitchesWidgetOnModeChange)
{
    XInputDialog* dlg = XInputDialog_create(NULL, 0);
    XLineEdit* edit;
    XSpinBox* spin;
    XQ_REQUIRE(dlg != NULL);

    XWidget_show((XWidget*)dlg);   /* 默认 TextInput ⇒ 行编辑上场 */
    edit = (XLineEdit*)xidTestChild(dlg, "qt_input_dialog_edit");
    XQ_REQUIRE(edit != NULL);
    XQ_CHECK(XWidget_isVisible((XWidget*)edit));

    /* 切 IntInput：自旋框就位、行编辑退场，布局仍是三段。 */
    XInputDialog_setInputMode(dlg, XInputDialog_IntInput);
    spin = (XSpinBox*)xidTestChild(dlg, "qt_input_dialog_spin");
    XQ_REQUIRE(spin != NULL);
    XQ_CHECK(XWidget_isVisible((XWidget*)spin));
    XQ_CHECK(!XWidget_isVisible((XWidget*)edit));
#if XDIALOGBUTTONBOX_ON
    XQ_EQ_I64(XLayout_count_base(XWidget_layout((XWidget*)dlg)), 3);
#else
    XQ_EQ_I64(XLayout_count_base(XWidget_layout((XWidget*)dlg)), 5);
#endif

    /* setTextValue 切回 TextInput：行编辑重新上场且文本落位。 */
    {
        XString* t = XString_create_utf8("abc");
        XQ_REQUIRE(t != NULL);
        XInputDialog_setTextValue(dlg, t);
        XClassDelete((XClass*)t);
    }
    XQ_CHECK(XWidget_isVisible((XWidget*)edit));
    XQ_CHECK(!XWidget_isVisible((XWidget*)spin));
    XQ_EQ_STR(xidCStr(XLineEdit_text(edit)), "abc");

    XWidget_hide((XWidget*)dlg);
    XClassDelete(dlg);
}

/* ==================== ②f UsePlainTextEditForTextInput ⇒ 纯文本编辑 ===== */

XQ_TEST(XInputDialog, instancePlainTextOption)
{
    XInputDialog* dlg = XInputDialog_create(NULL, 0);
    XPlainTextEdit* plain;
    char* buf;
    XQ_REQUIRE(dlg != NULL);
    XInputDialog_setOption(dlg, XInputDialog_UsePlainTextEditForTextInput,
                           true);
    {
        XString* t = XString_create_utf8("line1");
        XQ_REQUIRE(t != NULL);
        XInputDialog_setTextValue(dlg, t);
        XClassDelete((XClass*)t);
    }

    XWidget_show((XWidget*)dlg);
    /* 对标 chooseRightTextInputWidget（qinputdialog.cpp:381-384）：
     * 无下拉条目 + UsePlainTextEdit ⇒ 纯文本编辑（行编辑不建）。 */
    plain = (XPlainTextEdit*)xidTestChild(dlg, "qt_input_dialog_plain");
    XQ_REQUIRE(plain != NULL);
    XQ_CHECK(xidTestChild(dlg, "qt_input_dialog_edit") == NULL);
    buf = XPlainTextEdit_toPlainText(plain);
    XQ_EQ_STR(xidCStr(buf), "line1");
    if (buf) XFree_System(buf);
    XQ_CHECK(XWidget_hasFocus((XWidget*)plain));
    XQ_CHECK(XPlainTextEdit_hasSelectedText(plain));

    XWidget_hide((XWidget*)dlg);
    XClassDelete(dlg);
}

/* ==================== ②g 有下拉条目 ⇒ 下拉框承载文本 ================= */

XQ_TEST(XInputDialog, instanceComboItemsBuildComboBox)
{
    XInputDialog* dlg = XInputDialog_create(NULL, 0);
    XStringList* items;
    XComboBox* combo;
    XQ_REQUIRE(dlg != NULL);
    items = XStringList_create_ex(XCLASS_DEFAULT_MEMORY_TYPE);
    XQ_REQUIRE(items != NULL);
    {
        XString* a = XString_create_utf8("HTTP");
        XString* b = XString_create_utf8("SOCKS");
        XQ_REQUIRE(a != NULL && b != NULL);
        XStringList_push_back_move_base((XVector*)items, a);
        XStringList_push_back_move_base((XVector*)items, b);
        XClassDelete((XClass*)a);
        XClassDelete((XClass*)b);
    }
    XInputDialog_setComboBoxItems(dlg, items);
    XClassDelete((XClass*)items);
    {
        XString* t = XString_create_utf8("SOCKS");
        XQ_REQUIRE(t != NULL);
        XInputDialog_setTextValue(dlg, t);
        XClassDelete((XClass*)t);
    }

    XWidget_show((XWidget*)dlg);
    /* 对标 useComboBoxOrListView（qinputdialog.cpp:167-170）：有条目即
     * 下拉框承载，文本模式不再建行编辑。 */
    combo = (XComboBox*)xidTestChild(dlg, "qt_input_dialog_combo");
    XQ_REQUIRE(combo != NULL);
    XQ_EQ_I64(XComboBox_count(combo), 2);
    XQ_EQ_STR(xidCStr(XComboBox_currentText_2(combo)), "SOCKS");
    XQ_CHECK(xidTestChild(dlg, "qt_input_dialog_edit") == NULL);
    XQ_CHECK(XWidget_hasFocus((XWidget*)combo));

    XWidget_hide((XWidget*)dlg);
    XClassDelete(dlg);
}

/* ==================== ②h NoButtons ⇒ 按钮行隐藏，取消选项后复现 ====== */

#if XDIALOGBUTTONBOX_ON
XQ_TEST(XInputDialog, instanceNoButtonsHidesButtonBox)
{
    XInputDialog* dlg = XInputDialog_create(NULL, 0);
    XWidget* box;
    XQ_REQUIRE(dlg != NULL);
    XInputDialog_setOption(dlg, XInputDialog_NoButtons, true);
    XWidget_show((XWidget*)dlg);
    box = xidTestChild(dlg, "qt_input_dialog_buttons");
    XQ_REQUIRE(box != NULL);
    /* 对标 ensureLayout + setOptions 的 buttonBox->setVisible(!(opts & NoButtons))。 */
    XQ_CHECK(!XWidget_isVisible(box));

    XInputDialog_setOption(dlg, XInputDialog_NoButtons, false);
    XQ_CHECK(XWidget_isVisible(box));

    XWidget_hide((XWidget*)dlg);
    XClassDelete(dlg);
}
#endif /* XDIALOGBUTTONBOX_ON */

/* ==================== ②i 程序化 done 结算控件当前值 =================== */

XQ_TEST(XInputDialog, instanceSettlesWidgetValueOnDone)
{
    XInputDialog* dlg = XInputDialog_create(NULL, 0);
    XLineEdit* edit;
    XSpinBox* spin;
    XQ_REQUIRE(dlg != NULL);
    xidSigReset();
    XObject_connect((XObject*)dlg,
                      XSignal(XInputDialog_textValueSelected_signal),
                      (XObject*)dlg, xidTextSelSlot,
                      XConnectionType_Direct);
    XObject_connect((XObject*)dlg,
                      XSignal(XInputDialog_intValueSelected_signal),
                      (XObject*)dlg, xidIntSelSlot,
                      XConnectionType_Direct);

    /* 文本：控件被改写后 done(Accepted) 结算成 textValueSelected。 */
    XWidget_show((XWidget*)dlg);
    edit = (XLineEdit*)xidTestChild(dlg, "qt_input_dialog_edit");
    XQ_REQUIRE(edit != NULL);
    XLineEdit_setText(edit, "typed");
    XInputDialog_done(dlg, (int)XDialogCode_Accepted);
    XQ_EQ_I64(g_xidTextSel, 1);
    XQ_EQ_STR(g_xidText, "typed");

    /* 整数：切 IntInput 后直接改自旋框值，再 done。 */
    xidSigReset();
    XWidget_show((XWidget*)dlg);
    XInputDialog_setInputMode(dlg, XInputDialog_IntInput);
    spin = (XSpinBox*)xidTestChild(dlg, "qt_input_dialog_spin");
    XQ_REQUIRE(spin != NULL);
    XSpinBox_setValue(spin, 42);
    XInputDialog_done(dlg, (int)XDialogCode_Accepted);
    XQ_EQ_I64(g_xidIntSel, 1);
    XQ_EQ_I64(g_xidLastInt, 42);
    XQ_EQ_I64(XInputDialog_intValue(dlg), 42);

    XWidget_hide((XWidget*)dlg);
    XClassDelete(dlg);
}

/* ==================== ②j 已有布局（静态便捷路径形态）⇒ 不懒建 ========
 * 静态便捷函数在同一个对话框上自建布局与控件；setVisible 的懒建必须
 * 早退，否则会叠出第二套控件、抢走 findChild 的结算目标。 */

XQ_TEST(XInputDialog, userLayoutSuppressesLazyBuild)
{
    XInputDialog* dlg = XInputDialog_create(NULL, 0);
    XBoxLayout* root;
    XQ_REQUIRE(dlg != NULL);
    root = XBoxLayout_create(XBoxLayoutDirection_TopToBottom, (XWidget*)dlg);
    XQ_REQUIRE(root != NULL);

    XWidget_show((XWidget*)dlg);
    XQ_CHECK(XWidget_layout((XWidget*)dlg) == (XLayout*)root);
    XQ_CHECK(xidTestChild(dlg, "qt_input_dialog_edit") == NULL);
    XQ_CHECK(xidTestChild(dlg, "qt_input_dialog_spin") == NULL);
    XQ_CHECK(xidTestChild(dlg, "qt_input_dialog_label") == NULL);
    XQ_CHECK(xidTestChild(dlg, "qt_input_dialog_buttons") == NULL);

    XWidget_hide((XWidget*)dlg);
    XClassDelete(dlg);
    /* 借用版布局归调用方（XBoxLayout_create 的 parent 形参走
     * XWidget_setLayout 借用语义），对话框析构不释放。 */
    XClassDelete((XClass*)root);
}

/* ==================== ③ done(result!=0) 发确认信号 ==================== */

XQ_TEST(XInputDialog, doneEmitsValueSelected)
{
    XInputDialog* dlg = XInputDialog_create(NULL, 0);
    XQ_REQUIRE(dlg != NULL);
    xidSigReset();
    XObject_connect((XObject*)dlg,
                      XSignal(XInputDialog_textValueSelected_signal),
                      (XObject*)dlg, xidTextSelSlot,
                      XConnectionType_Direct);
    XObject_connect((XObject*)dlg,
                      XSignal(XInputDialog_intValueSelected_signal),
                      (XObject*)dlg, xidIntSelSlot,
                      XConnectionType_Direct);
    XObject_connect((XObject*)dlg,
                      XSignal(XInputDialog_doubleValueSelected_signal),
                      (XObject*)dlg, xidDoubleSelSlot,
                      XConnectionType_Direct);

    /* 实测 I4：done(1) → textValueSelected("zz")。 */
    {
        XString* t = XString_create_utf8("zz");
        XQ_REQUIRE(t != NULL);
        XInputDialog_setTextValue(dlg, t);
        XClassDelete((XClass*)t);
    }
    XWidget_show((XWidget*)dlg);
    XInputDialog_done(dlg, (int)XDialogCode_Accepted);
    XQ_EQ_I64(g_xidTextSel, 1);
    XQ_EQ_STR(g_xidText, "zz");
    XQ_EQ_I64(XDialog_result(&dlg->m_base), XDialogCode_Accepted);
    XQ_CHECK(!XWidget_isVisible((XWidget*)dlg));

    /* 实测 I5：done(0) 不发确认信号。 */
    xidSigReset();
    XWidget_show((XWidget*)dlg);
    XInputDialog_done(dlg, (int)XDialogCode_Rejected);
    XQ_EQ_I64(g_xidTextSel, 0);
    XQ_EQ_I64(g_xidIntSel, 0);
    XQ_EQ_I64(g_xidDoubleSel, 0);

    /* 整数模式：done(1) → intValueSelected(7)。 */
    xidSigReset();
    XInputDialog_setIntValue(dlg, 7);
    XWidget_show((XWidget*)dlg);
    XInputDialog_done(dlg, (int)XDialogCode_Accepted);
    XQ_EQ_I64(g_xidIntSel, 1);
    XQ_EQ_I64(g_xidLastInt, 7);

    /* NULL 安全。 */
    XInputDialog_done(NULL, 1);

    XClassDelete(dlg);
}

/* ==================== ④ 文本属性往返（不触发模态 exec） ==================== */

XQ_TEST(XInputDialog, textPropertiesRoundTrip)
{
    XInputDialog* dlg = XInputDialog_create(NULL, 0);
    XQ_REQUIRE(dlg != NULL);

    /* 注：static getText/getInt 等便捷函数在**有应用实例**时进入阻塞
     * exec（本测试宿主创建了 XCoreApplication），无输入源会挂起，
     * 故本用例只测属性存取，不调用静态便捷入口。 */
    {
        XString* t = XString_create_utf8("请输入端口");
        XQ_REQUIRE(t != NULL);
        XInputDialog_setLabelText(dlg, t);
        XClassDelete((XClass*)t);
    }
    {
        XString* got = XInputDialog_labelText(dlg);
        XQ_REQUIRE(got != NULL);
        XQ_EQ_STR(XString_toUtf8(got), "请输入端口");
        XClassDelete((XClass*)got);
    }
    {
        XString* t = XString_create_utf8("确定");
        XQ_REQUIRE(t != NULL);
        XInputDialog_setOkButtonText(dlg, t);
        XClassDelete((XClass*)t);
        t = XString_create_utf8("取消");
        XQ_REQUIRE(t != NULL);
        XInputDialog_setCancelButtonText(dlg, t);
        XClassDelete((XClass*)t);
    }
    {
        XString* ok = XInputDialog_okButtonText(dlg);
        XString* cancel = XInputDialog_cancelButtonText(dlg);
        XQ_REQUIRE(ok != NULL);
        XQ_REQUIRE(cancel != NULL);
        XQ_EQ_STR(XString_toUtf8(ok), "确定");
        XQ_EQ_STR(XString_toUtf8(cancel), "取消");
        XClassDelete((XClass*)ok);
        XClassDelete((XClass*)cancel);
    }
    /* 下拉条目深拷贝往返（qinputdialog.h:78-79）。 */
    {
        XStringList* items =
            XStringList_create_ex(XCLASS_DEFAULT_MEMORY_TYPE);
        XStringList* got;
        XQ_REQUIRE(items != NULL);
        {
            XString* a = XString_create_utf8("HTTP");
            XString* b = XString_create_utf8("SOCKS");
            XQ_REQUIRE(a != NULL && b != NULL);
            XStringList_push_back_move_base((XVector*)items, a);
            XStringList_push_back_move_base((XVector*)items, b);
            XClassDelete((XClass*)a);
            XClassDelete((XClass*)b);
        }
        XInputDialog_setComboBoxItems(dlg, items);
        XClassDelete((XClass*)items);
        got = XInputDialog_comboBoxItems(dlg);
        XQ_REQUIRE(got != NULL);
        XQ_EQ_I64(XStringList_size_base((const XContainer*)got), 2);
        XClassDelete((XClass*)got);
    }

    XClassDelete(dlg);
}

/* ==================== ⑤ 父子所有权 ==================== */

XQ_TEST(XInputDialog, parentOwnership)
{
    XWidget* parent = XWidget_create(NULL, 0);
    XInputDialog* dlg;
    XQ_REQUIRE(parent != NULL);
    dlg = XInputDialog_create(parent, 0);
    XQ_REQUIRE(dlg != NULL);
    XQ_CHECK(XWidget_parentWidget((XWidget*)dlg) == parent);
    /* 继承链贯通：XDialog_init 的「恒为顶层对话框窗口」对派生类生效，
     * 且虚表即本类（FRAME/XDIALOG 基类槽已继承）。 */
    XQ_CHECK(XClassGetVtable((XObject*)dlg) == XInputDialog_class_init());
    XQ_CHECK(XWidget_isWindow((XWidget*)dlg));
    XClassDelete(dlg);
    XClassDelete(parent);
}

#endif /* 模块开关 */
