/**
 * @file       XAbstractSpinBoxAlignTest.c
 * @brief      XAbstractSpinBox × Qt 6.8 QAbstractSpinBox 对齐契约测试。
 * @details    对齐依据：/opt/Qt/6.8.3/gcc_64/include/QtWidgets/
 *             qabstractspinbox.h 与 Qt 6.8 源码
 *             src/widgets/widgets/qabstractspinbox.cpp，关键行为用本机
 *             Qt 6.8.0（系统 Qt，offscreen）探针实测：
 *             - 构造默认值：buttonSymbols=UpDownArrows(0)、wrapping=false、
 *               keyboardTracking=true、frame=true、
 *               correctionMode=CorrectToPreviousValue(0)、accelerated=false、
 *               groupSeparatorShown=false、readOnly=false、
 *               specialValueText 为空；焦点策略 WheelFocus
 *               （qabstractspinbox.cpp:1605 setFocusPolicy(Qt::WheelFocus)）；
 *             - 基类 stepEnabled（QMetaType::UnknownType）恒 StepNone
 *               （qabstractspinbox.cpp:537-552）；
 *             - 公共槽 clear()：清成「前缀+后缀」并置内部 cleared
 *               （qabstractspinbox.cpp:510-517，子类保留前后缀/分段）；
 *             - editingFinished 信号：内嵌编辑框 editingFinished 的转发
 *               （qabstractspinbox.cpp focusOutEvent 的 emit editingFinished）；
 *             - Return/Enter：interpret + selectAll + 忽略事件 +
 *               editingFinished + 转发内嵌编辑框 returnPressed
 *               （qabstractspinbox.cpp:1048-1052，探针实测两者各 1 次）；
 *             - Up/Down/PageUp/PageDown 事件先 accept 再判步进使能
 *               （qabstractspinbox.cpp:1006-1010），禁用时也吞键、不转投
 *               内嵌编辑框；
 *             - 步进按钮长按：spinButtonPress 在基类（无值域）下不启动
 *               连发（stepEnabled 恒 StepNone），状态位保持 None。
 * @note       本文件只测「基类」行为；QSpinBox 专有量程/文本转换见
 *             XSpinBoxAlignTest.c，日期时间子类见各自文件。
 * @author     XinYueC 团队
 */
#include "xq_align.h"

#include "XGuiConfig.h"

#if XGUI_ON && XWIDGET_ON && XABSTRACTSPINBOX_ON && XLINEEDIT_ON

#include "XAbstractSpinBox.h"
#include "XLineEdit.h"
#include "XWidget.h"
#include "XObject.h"
#include "XObject_Protected.h"
#include "XEvent.h"
#include "XVarList.h"

/** @brief editingFinished 转发计数（静态单槽，用例内清零）。 */
static int xq_asb_editingFinished = 0;
/** @brief 内嵌编辑框 returnPressed 计数（Return 转发口径，用例内清零）。 */
static int xq_asb_editReturnPressed = 0;

static void xq_asb_onEditingFinished(XObject* receiver, XVarList* args)
{
    (void)receiver;
    (void)args;
    ++xq_asb_editingFinished;
}

static void xq_asb_onEditReturnPressed(XObject* receiver, XVarList* args)
{
    (void)receiver;
    (void)args;
    ++xq_asb_editReturnPressed;
}

/** @brief 构造默认值（逐项对齐 Qt 6.8 探针与源码构造口径）。 */
XQ_TEST(XAbstractSpinBox, constructorDefaults)
{
    XAbstractSpinBox* spin = XAbstractSpinBox_create_ex(
        XCLASS_DEFAULT_MEMORY_TYPE, NULL, 0);
    XLineEdit* edit;
    XQ_REQUIRE(spin != NULL);

    /* qabstractspinbox.cpp 构造：buttonSymbols 默认 UpDownArrows(0)，
     * correctionMode 默认 CorrectToPreviousValue(0)，wrapping 关，
     * keyboardTracking 开，frame 开（Qt 探针 hasFrame=1），
     * accelerated/groupSeparator 关，readOnly 关。 */
    XQ_EQ_I64(XAbstractSpinBox_buttonSymbols(spin),
              XAbstractSpinBoxButtonSymbols_UpDownArrows);
    XQ_EQ_I64(XAbstractSpinBox_correctionMode(spin),
              XAbstractSpinBoxCorrectionMode_CorrectToPreviousValue);
    XQ_CHECK(!XAbstractSpinBox_wrapping(spin));
    XQ_CHECK(XAbstractSpinBox_keyboardTracking(spin));
    XQ_CHECK(XAbstractSpinBox_hasFrame(spin));
    XQ_CHECK(!XAbstractSpinBox_isAccelerated(spin));
    XQ_CHECK(!XAbstractSpinBox_isGroupSeparatorShown(spin));
    XQ_CHECK(!XAbstractSpinBox_isReadOnly(spin));
    XQ_EQ_STR(XAbstractSpinBox_specialValueText(spin), "");

    /* 内嵌编辑框：Qt 构造即建（QAbstractSpinBoxPrivate::init 的
     * d->edit = new QLineEdit），本库同一口径；编辑框父控件=微调框本体。 */
    edit = XAbstractSpinBox_lineEdit(spin);
    XQ_CHECK_PTR(edit);
    XQ_CHECK(XWidget_parentWidget((XWidget*)edit) == (XWidget*)spin);
    /* 基类（type=Unknown）无值域 ⇒ 构造期文本为空、无步进方向。 */
    XQ_EQ_STR(XAbstractSpinBox_text(spin), "");
    XQ_EQ_I64(XAbstractSpinBox_stepEnabled_base(spin),
              XAbstractSpinBoxStepEnabledFlag_StepNone);
    /* Qt 构造 setFocusPolicy(Qt::WheelFocus)（qabstractspinbox.cpp:1605：
     * Tab+点击+滚轮可聚焦）。 */
    XQ_EQ_I64(XWidget_focusPolicy((XWidget*)spin),
              XWidgetFocusPolicy_WheelFocus);
    /* Qt 构造 setSizePolicy(Minimum, Fixed)（qabstractspinbox.cpp:1606）：
     * 水平可增长、垂直锁在 sizeHint。 */
    {
        XWidgetSizePolicy sp = XWidget_sizePolicy((XWidget*)spin);
        XQ_EQ_I64(XWidgetSizePolicy_horizontalPolicy(&sp),
                  XWidgetSizePolicy_Minimum);
        XQ_EQ_I64(XWidgetSizePolicy_verticalPolicy(&sp),
                  XWidgetSizePolicy_Fixed);
    }

    XClassDelete((XClass*)spin);
}

/** @brief 公共 clear() 槽 + editingFinished 转发（Qt 6.8 实测口径）。 */
XQ_TEST(XAbstractSpinBox, clearAndEditingFinished)
{
    XAbstractSpinBox* spin = XAbstractSpinBox_create_ex(
        XCLASS_DEFAULT_MEMORY_TYPE, NULL, 0);
    XLineEdit* edit;
    XConnection* conn;
    XQ_REQUIRE(spin != NULL);
    edit = XAbstractSpinBox_lineEdit(spin);
    XQ_REQUIRE(edit != NULL);

    /* 属性 setter/getter 往返（Qt 公共 API 一一对应）。 */
    XAbstractSpinBox_setWrapping(spin, true);
    XQ_CHECK(XAbstractSpinBox_wrapping(spin));
    XAbstractSpinBox_setKeyboardTracking(spin, false);
    XQ_CHECK(!XAbstractSpinBox_keyboardTracking(spin));
    XAbstractSpinBox_setSpecialValueText(spin, "none");
    XQ_EQ_STR(XAbstractSpinBox_specialValueText(spin), "none");
    XAbstractSpinBox_setButtonSymbols(spin,
                                      XAbstractSpinBoxButtonSymbols_PlusMinus);
    XQ_EQ_I64(XAbstractSpinBox_buttonSymbols(spin),
              XAbstractSpinBoxButtonSymbols_PlusMinus);
    XAbstractSpinBox_setReadOnly(spin, true);
    XQ_CHECK(XAbstractSpinBox_isReadOnly(spin));

    /* clear() 公共槽（qabstractspinbox.cpp:510-517 基类形态：清空文本）。
     * 基类无前后缀概念 ⇒ 清完就是空串。 */
    XLineEdit_setText(edit, "abc");
    XQ_EQ_STR(XAbstractSpinBox_text(spin), "abc");
    XAbstractSpinBox_clear(spin);
    XQ_EQ_STR(XAbstractSpinBox_text(spin), "");

    /* editingFinished 转发：内嵌编辑框的 editingFinished → 本控件同名
     * 信号（XAbstractSpinBox_editingFinished_signal）。真发一次编辑框
     * 信号，断言恰好转发 1 次（不多发）。 */
    xq_asb_editingFinished = 0;
    conn = XObject_connect_1(
        (XObject*)spin,
        (size_t)XAbstractSpinBox_editingFinished_signal(spin),
        (XObject*)spin, xq_asb_onEditingFinished, XConnectionType_Direct);
    XQ_CHECK_PTR(conn);
    {
        XVarList* args = XVarList_create(0);
        XQ_REQUIRE(args != NULL);
        XObject_emitSignal((XObject*)edit,
                           (size_t)XLineEdit_editingFinished_signal(edit),
                           args, NULL, NULL, XEVENT_PRIORITY_NORMAL);
    }
    XQ_EQ_I64(xq_asb_editingFinished, 1);

    XClassDelete((XClass*)spin);
}

/** @brief Up/Down 键在步进禁用时仍被接受（qabstractspinbox.cpp:1006-1010
 *         的 `event->accept(); ... if (!(stepEnabled() & ...)) return;`）。 */
XQ_TEST(XAbstractSpinBox, stepKeyAcceptedWhenDisabled)
{
    XAbstractSpinBox* spin = XAbstractSpinBox_create_ex(
        XCLASS_DEFAULT_MEMORY_TYPE, NULL, 0);
    XKeyEvent ke;
    XQ_REQUIRE(spin != NULL);

    /* 基类 stepEnabled 恒 StepNone ⇒ Up 不步进，但事件必须被接受
     * （Qt 先 accept 再判使能；不接受会让内嵌编辑框把 Up 当光标移动）。 */
    XKeyEvent_init(&ke, XEVENT_TYPE_KEY_PRESS, XKey_Up,
                   XKeyboardModifier_NoModifier);
    XObject_event_base((XObject*)spin, (XEvent*)&ke);
    XQ_CHECK(XEvent_isAccepted((XEvent*)&ke));

    XKeyEvent_init(&ke, XEVENT_TYPE_KEY_PRESS, XKey_PageDown,
                   XKeyboardModifier_NoModifier);
    XObject_event_base((XObject*)spin, (XEvent*)&ke);
    XQ_CHECK(XEvent_isAccepted((XEvent*)&ke));

    XClassDelete((XClass*)spin);
}

/** @brief Return 同时发 editingFinished 与内嵌编辑框 returnPressed
 *         （qabstractspinbox.cpp:1048-1052；本机 Qt 探针实测各 1 次）。 */
XQ_TEST(XAbstractSpinBox, returnKeyForwardsReturnPressed)
{
    XAbstractSpinBox* spin = XAbstractSpinBox_create_ex(
        XCLASS_DEFAULT_MEMORY_TYPE, NULL, 0);
    XLineEdit* edit;
    XKeyEvent ke;
    XConnection* cFinished;
    XConnection* cReturn;
    XQ_REQUIRE(spin != NULL);
    edit = XAbstractSpinBox_lineEdit(spin);
    XQ_REQUIRE(edit != NULL);
    cFinished = XObject_connect_1(
        (XObject*)spin,
        (size_t)XAbstractSpinBox_editingFinished_signal(spin),
        (XObject*)spin, xq_asb_onEditingFinished, XConnectionType_Direct);
    cReturn = XObject_connect_1(
        (XObject*)edit,
        (size_t)XLineEdit_returnPressed_signal(edit),
        (XObject*)edit, xq_asb_onEditReturnPressed,
        XConnectionType_Direct);
    XQ_CHECK_PTR(cFinished);
    XQ_CHECK_PTR(cReturn);

    xq_asb_editingFinished = 0;
    xq_asb_editReturnPressed = 0;
    XKeyEvent_init(&ke, XEVENT_TYPE_KEY_PRESS, XKey_Return,
                   XKeyboardModifier_NoModifier);
    XObject_event_base((XObject*)spin, (XEvent*)&ke);
    /* 两者各一次：qabstractspinbox.cpp:1051-1052 `emit editingFinished();
     * emit d->edit->returnPressed();`。 */
    XQ_EQ_I64(xq_asb_editingFinished, 1);
    XQ_EQ_I64(xq_asb_editReturnPressed, 1);
    /* Return 被本控件消费（event->ignore()：不重新投递）——取值后
     * isAccepted 为 false 即 Qt 的 ignore 语义（不派发到父控件）。 */
    XQ_CHECK(!XEvent_isAccepted((XEvent*)&ke));

    XClassDelete((XClass*)spin);
}

/** @brief 基类步进按钮连发入口在无值域时不解锁（stepEnabled 恒 StepNone
 *         ——对标 Qt QAbstractSpinBoxPrivate::updateState 的使能闸）。 */
XQ_TEST(XAbstractSpinBox, spinButtonPressNoStepWithoutRange)
{
    XAbstractSpinBox* spin = XAbstractSpinBox_create_ex(
        XCLASS_DEFAULT_MEMORY_TYPE, NULL, 0);
    XQ_REQUIRE(spin != NULL);
    XQ_EQ_I64(XAbstractSpinBox_buttonState(spin),
              XAbstractSpinBoxButtonState_None);
    XAbstractSpinBox_spinButtonPress(spin, true);
    /* 无步进方向 ⇒ 不起连发：状态位仍 None、无「按住」态。 */
    XQ_EQ_I64(XAbstractSpinBox_buttonState(spin),
              XAbstractSpinBoxButtonState_None);
    XQ_CHECK(!XAbstractSpinBox_isSpinButtonActive(spin));
    XAbstractSpinBox_spinButtonRelease(spin);
    XQ_CHECK(!XAbstractSpinBox_isSpinButtonActive(spin));
    /* 基类无前后缀：虚槽恒空串（clear 组合文本的依据）。 */
    XQ_EQ_STR(XAbstractSpinBox_prefixText_base(spin), "");
    XQ_EQ_STR(XAbstractSpinBox_suffixText_base(spin), "");
    XClassDelete((XClass*)spin);
}

#endif /* XGUI_ON && XWIDGET_ON && XABSTRACTSPINBOX_ON && XLINEEDIT_ON */
