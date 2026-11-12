/**
 * @file       XSpinBoxAlignTest.c
 * @brief      XSpinBox × Qt 6.8 QSpinBox 对齐契约测试。
 * @details    对齐依据：/opt/Qt/6.8.3/gcc_64/include/QtWidgets/qspinbox.h
 *             与 Qt 6.8 源码 src/widgets/widgets/qspinbox.cpp、
 *             qabstractspinbox.cpp；关键行为用本机 Qt 6.8.0（系统 Qt，
 *             offscreen）探针实测：
 *             - 默认值：minimum=0、maximum=99、value=0、singleStep=1、
 *               displayIntegerBase=10、stepType=DefaultStepType；
 *               setRange(min>max) ⇒ maximum=minimum（SetRange 100,10 实测
 *               两者都是 100）；setValue 越界钳位；
 *             - setSingleStep 负值被忽略（qspinbox.cpp:306-314
 *               `if (value >= 0)`，实测 step(-5) 后 singleStep 仍为 1）；
 *             - specialValueText 在 value==minimum 时整段替换显示，且
 *               cleanText 亦返回特殊值文本（实测 'auto'）；非最小值时
 *               text=prefix+数值+suffix、cleanText 只留数值；
 *             - stepEnabled：到上界只剩 StepDown、到下界只剩 StepUp、
 *               wrapping 双向、readOnly 全禁；但 readOnly 下 stepUp()
 *               仍然改值（Qt 实测 stepUp 后 5→6）——stepEnabled 只门控
 *               键盘/箭头，不拦公共槽；
 *             - AdaptiveDecimalStepType 自适应十进制步长
 *               （QSpinBoxPrivate::calculateAdaptiveDecimalStep，
 *               qspinbox.cpp:400-417）：实测 99→+1、100→+10、500→+10、
 *               999→+10、1000→+100、12345→+1000、-500→+10、-1000→+10、
 *               1000 stepBy(2)→+200、500 stepDown→-10；
 *             - clear() 保留前后缀（qabstractspinbox.cpp:510-517
 *               `edit->setText(prefix + suffix)`，实测带 "$"/" ms" 的
 *               QSpinBox clear 后文本为 "$ ms"、值不变）；cleared 标志在
 *               interpret/updateEdit 期间保持（失焦后文本仍为空态），
 *               setValue 或用户键入才复位（qabstractspinbox.cpp:1773/
 *               1881/1750，实测 clear→失焦文本仍 "$ ms"、同值 setValue
 *               后恢复 "$44 ms"）；
 *             - cleanText 保留千分位分隔符（stripped 只剥匹配的前后缀 +
 *               trimmed，实测 '1,234,567'）；特殊值文本整段原样返回；
 *             - validate/valueFromText 对缺前缀的文本同样可接受
 *               （QSpinBoxValidator 先补前后缀再校验，实测 validate("3")
 *               为 Acceptable、valueFromText("3")==3）；
 *             - textFromValue 返回**裸数值**（无前后缀、不处理特殊值，
 *               实测 textFromValue(3)=="3"、specialValue 下
 *               textFromValue(0)=="0"）；
 *             - Home/End：无 Shift 时交内嵌编辑框移光标、值不变；Shift
 *               组合选到前缀后/后缀前（实测 Home→cursor 0、End→行尾、
 *               Shift+Home(2)→选中 '5'、Shift+End(2)→选中 '0'）；
 *             - 长按步进按钮：阈值 500ms 后按 150ms 连发，accelerated
 *               逐拍缩短间隔（accelerated=true 时同样时长步进数更多）。
 * @author     XinYueC 团队
 */
#include "xq_align.h"

#include "XGuiConfig.h"

#if XGUI_ON && XWIDGET_ON && XSPINBOX_ON && XLINEEDIT_ON && XABSTRACTSPINBOX_ON

#include "XSpinBox.h"
#include "XAbstractSpinBox.h"
#include "XLineEdit.h"
#include "XWidget.h"
#include "XObject.h"
#include "XObject_Protected.h"
#include "XEvent.h"
#include "XEventLoop.h"
#include "XCoreApplication.h"
#include "XMemory.h"

/** @brief valueChanged / textChanged 计数（用例内清零）。 */
static int xq_sb_valueChanged = 0;
static int xq_sb_textChanged = 0;

static void xq_sb_onValueChanged(XObject* receiver, XVarList* args)
{
    (void)receiver;
    (void)args;
    ++xq_sb_valueChanged;
}

static void xq_sb_onTextChanged(XObject* receiver, XVarList* args)
{
    (void)receiver;
    (void)args;
    ++xq_sb_textChanged;
}

/** @brief 构造默认值、范围钳位、负单步忽略、stepEnabled 四态。 */
XQ_TEST(XSpinBox, defaultsRangeAndStepEnabled)
{
    XSpinBox* spin = XSpinBox_create(NULL, 0);
    XAbstractSpinBox* base;
    XQ_REQUIRE(spin != NULL);
    base = (XAbstractSpinBox*)spin;

    /* qspinbox.h + QSpinBoxPrivate::init：0/99/0/1/10/DefaultStepType。 */
    XQ_EQ_I64(XSpinBox_minimum(spin), 0);
    XQ_EQ_I64(XSpinBox_maximum(spin), 99);
    XQ_EQ_I64(XSpinBox_value(spin), 0);
    XQ_EQ_I64(XSpinBox_singleStep(spin), 1);
    XQ_EQ_I64(XSpinBox_displayIntegerBase(spin), 10);
    XQ_EQ_I64(XSpinBox_stepType(spin),
              XAbstractSpinBoxStepType_DefaultStepType);
    XQ_EQ_STR(XSpinBox_prefix(spin), "");
    XQ_EQ_STR(XSpinBox_suffix(spin), "");
    /* 构造后显示当前值（Qt 首次 show 时才回填文本，本库 init 即回填；
     * 数值语义一致，仅"首帧前 text()"这一实现细节不同）。 */
    XQ_EQ_STR(XAbstractSpinBox_text(base), "0");

    /* setRange(min>max) ⇒ 上限收敛为 min（qabstractspinbox.cpp:1806-1827
     * 的 setRange：`maximum = qMax(min, max)`），实测 min=max=100。 */
    XSpinBox_setRange(spin, 100, 10);
    XQ_EQ_I64(XSpinBox_minimum(spin), 100);
    XQ_EQ_I64(XSpinBox_maximum(spin), 100);
    XQ_EQ_I64(XSpinBox_value(spin), 100);

    /* setValue 越界钳位（实测 1000→100 于 10..100；-1→10）。 */
    XSpinBox_setRange(spin, 10, 100);
    XSpinBox_setValue(spin, 1000);
    XQ_EQ_I64(XSpinBox_value(spin), 100);
    XSpinBox_setValue(spin, -1);
    XQ_EQ_I64(XSpinBox_value(spin), 10);

    /* 负单步被忽略（qspinbox.cpp:306-314），实测 singleStep 仍 1。 */
    XSpinBox_setSingleStep(spin, -5);
    XQ_EQ_I64(XSpinBox_singleStep(spin), 1);

    /* stepEnabled 四态（实测：at max=2、at min=1、wrapping=3、readOnly=0）。 */
    XSpinBox_setValue(spin, 100);
    XQ_EQ_I64(XAbstractSpinBox_stepEnabled_base(base),
              XAbstractSpinBoxStepEnabledFlag_StepDownEnabled);
    XSpinBox_setValue(spin, 10);
    XQ_EQ_I64(XAbstractSpinBox_stepEnabled_base(base),
              XAbstractSpinBoxStepEnabledFlag_StepUpEnabled);
    XAbstractSpinBox_setWrapping(base, true);
    XQ_EQ_I64(XAbstractSpinBox_stepEnabled_base(base),
              XAbstractSpinBoxStepEnabledFlag_StepUpEnabled |
              XAbstractSpinBoxStepEnabledFlag_StepDownEnabled);
    XAbstractSpinBox_setReadOnly(base, true);
    XQ_EQ_I64(XAbstractSpinBox_stepEnabled_base(base),
              XAbstractSpinBoxStepEnabledFlag_StepNone);
    /* readOnly 只门控键盘/箭头（Qt 实测 stepUp 仍 5→6）。 */
    XSpinBox_setValue(spin, 50);
    XSpinBox_stepUp(spin);
    XQ_EQ_I64(XSpinBox_value(spin), 51);

    XQ_EQ_I64(XAbstractSpinBox_correctionMode(base),
              XAbstractSpinBoxCorrectionMode_CorrectToPreviousValue);
    XClassDelete((XClass*)spin);
}

/** @brief specialValueText / prefix / suffix / cleanText / 文本↔值转换。 */
XQ_TEST(XSpinBox, specialValueAndTextConversion)
{
    XSpinBox* spin = XSpinBox_create(NULL, 0);
    XAbstractSpinBox* base;
    char* text;
    char* clean;
    XQ_REQUIRE(spin != NULL);
    base = (XAbstractSpinBox*)spin;
    XSpinBox_setRange(spin, 0, 10);
    XAbstractSpinBox_setSpecialValueText(base, "auto");
    XSpinBox_setPrefix(spin, "$");
    XSpinBox_setSuffix(spin, " ms");

    /* value==minimum ⇒ 整段特殊值文本，无前后缀（Qt 实测 'auto'）。 */
    XSpinBox_setValue(spin, 0);
    XQ_EQ_STR(XAbstractSpinBox_text(base), "auto");
    clean = XSpinBox_cleanText(spin);
    XQ_CHECK(clean != NULL);
    if (clean) {
        XQ_EQ_STR(clean, "auto");
        XFree_System(clean);
    }

    /* 非最小值 ⇒ prefix + 数值 + suffix（Qt 实测 '$3 ms'），
     * cleanText 只保留数值（'3'）。 */
    XSpinBox_setValue(spin, 3);
    XQ_EQ_STR(XAbstractSpinBox_text(base), "$3 ms");
    clean = XSpinBox_cleanText(spin);
    XQ_CHECK(clean != NULL);
    if (clean) {
        XQ_EQ_STR(clean, "3");
        XFree_System(clean);
    }

    /* 文本↔值虚槽：textFromValue 给**裸数值**（Qt QSpinBox::textFromValue
     * 不含前后缀、不处理特殊值，实测 "3"），valueFromText 反向解析；
     * 特殊值文本解析回 minimum（qspinbox.cpp:511-522）。 */
    XQ_EQ_I64(XSpinBox_valueFromText_base(spin, "$3 ms"), 3);
    XQ_EQ_I64(XSpinBox_valueFromText_base(spin, "auto"), 0);
    /* 缺前缀同样可解析/可接受（QSpinBoxValidator 先补前后缀再校验，
     * 本机 Qt 实测 validate("3")==Acceptable、valueFromText("3")==3）。 */
    XQ_EQ_I64(XSpinBox_valueFromText_base(spin, "3"), 3);
    {
        int pos = 1;
        XQ_EQ_I64(XSpinBox_validate_base(spin, "3", &pos),
                  XValidatorState_Acceptable);
        XQ_EQ_I64(XSpinBox_validate_base(spin, "$3 ms", &pos),
                  XValidatorState_Acceptable);
    }
    text = XSpinBox_textFromValue_base(spin, 3);
    XQ_CHECK(text != NULL);
    if (text) {
        XQ_EQ_STR(text, "3");
        XFree_System(text);
    }
    /* 特殊值文本只影响显示：textFromValue(0) 仍是裸 "0"（实测）。 */
    text = XSpinBox_textFromValue_base(spin, 0);
    XQ_CHECK(text != NULL);
    if (text) {
        XQ_EQ_STR(text, "0");
        XFree_System(text);
    }

    /* 千分位分隔显示（Qt 实测 1234567 → '1,234,567'）。 */
    XSpinBox_setRange(spin, 0, 1234567);
    XAbstractSpinBox_setGroupSeparatorShown(base, true);
    XSpinBox_setValue(spin, 1234567);
    XQ_EQ_STR(XAbstractSpinBox_text(base), "$1,234,567 ms");
    /* cleanText 保留千分位（QAbstractSpinBoxPrivate::stripped 只剥前后缀
     * 与首尾空白，实测 '1,234,567'；gap 94）。 */
    clean = XSpinBox_cleanText(spin);
    XQ_CHECK(clean != NULL);
    if (clean) {
        XQ_EQ_STR(clean, "1,234,567");
        XFree_System(clean);
    }
    XAbstractSpinBox_setGroupSeparatorShown(base, false);
    XQ_EQ_STR(XAbstractSpinBox_text(base), "$1234567 ms");

    /* 显示进制（qspinbox.cpp:445-459）：16 进制小写、非法进制回落 10。 */
    XSpinBox_setRange(spin, 0, 255);
    XSpinBox_setPrefix(spin, "");
    XSpinBox_setSuffix(spin, "");
    XSpinBox_setDisplayIntegerBase(spin, 16);
    XSpinBox_setValue(spin, 255);
    XQ_EQ_STR(XAbstractSpinBox_text(base), "ff");
    XSpinBox_setDisplayIntegerBase(spin, 1);
    XQ_EQ_I64(XSpinBox_displayIntegerBase(spin), 10);

    XClassDelete((XClass*)spin);
}

/** @brief AdaptiveDecimalStepType 自适应十进制步长（Qt 探针实测值）。 */
XQ_TEST(XSpinBox, adaptiveDecimalStep)
{
    static const int kFrom[] = {99, 100, 500, 999, 1000, 12345, -500, -1000};
    static const int kTo[]   = {100, 110, 510, 1009, 1100, 13345, -490, -990};
    XSpinBox* spin = XSpinBox_create(NULL, 0);
    XAbstractSpinBox* base;
    int i;
    XQ_REQUIRE(spin != NULL);
    base = (XAbstractSpinBox*)spin;
    XSpinBox_setRange(spin, -100000, 100000);
    XSpinBox_setStepType(spin, XAbstractSpinBoxStepType_AdaptiveDecimalStepType);
    XQ_EQ_I64(XSpinBox_stepType(spin),
              XAbstractSpinBoxStepType_AdaptiveDecimalStepType);

    for (i = 0; i < (int)(sizeof(kFrom) / sizeof(kFrom[0])); ++i) {
        XSpinBox_setValue(spin, kFrom[i]);
        XSpinBox_stepUp(spin);
        XQ_EQ_I64(XSpinBox_value(spin), kTo[i]);
    }

    /* stepBy 多步：1000 起点步长 100，stepBy(2)=+200（实测 1200）。 */
    XSpinBox_setValue(spin, 1000);
    XAbstractSpinBox_stepBy_base(base, 2);
    XQ_EQ_I64(XSpinBox_value(spin), 1200);
    /* 向下步长对称（500 stepDown → 490）。 */
    XSpinBox_setValue(spin, 500);
    XSpinBox_stepDown(spin);
    XQ_EQ_I64(XSpinBox_value(spin), 490);

    /* 回到 DefaultStepType：固定 singleStep（500→501）。 */
    XSpinBox_setStepType(spin, XAbstractSpinBoxStepType_DefaultStepType);
    XSpinBox_setValue(spin, 500);
    XSpinBox_stepUp(spin);
    XQ_EQ_I64(XSpinBox_value(spin), 501);

    XClassDelete((XClass*)spin);
}

/** @brief 回绕步进、clear() 前后缀保留、valueChanged/textChanged 次数。 */
XQ_TEST(XSpinBox, wrappingClearAndSignals)
{
    XSpinBox* spin = XSpinBox_create(NULL, 0);
    XAbstractSpinBox* base;
    XLineEdit* edit;
    XConnection* connValue;
    XConnection* connText;
    XQ_REQUIRE(spin != NULL);
    base = (XAbstractSpinBox*)spin;
    edit = XAbstractSpinBox_lineEdit(base);
    XQ_REQUIRE(edit != NULL);

    /* wrapping：到顶 stepUp 回 minimum、到底 stepDown 回 maximum
     * （Qt 实测 range 0..2 时 2→stepUp→0）。 */
    XSpinBox_setRange(spin, 0, 2);
    XAbstractSpinBox_setWrapping(base, true);
    XSpinBox_setValue(spin, 2);
    XSpinBox_stepUp(spin);
    XQ_EQ_I64(XSpinBox_value(spin), 0);
    XSpinBox_stepDown(spin);
    XQ_EQ_I64(XSpinBox_value(spin), 2);
    XAbstractSpinBox_setWrapping(base, false);

    /* 信号：valueChanged 只在值实际变化时发（EmitIfChanged）。 */
    XSpinBox_setRange(spin, 0, 100);
    XQ_EQ_I64(XSpinBox_value(spin), 2);
    connValue = XObject_connect((XObject*)spin,
                                  (size_t)XSpinBox_valueChanged_signal(spin),
                                  (XObject*)spin, xq_sb_onValueChanged,
                                  XConnectionType_Direct);
    connText = XObject_connect((XObject*)spin,
                                 (size_t)XSpinBox_textChanged_signal(spin),
                                 (XObject*)spin, xq_sb_onTextChanged,
                                 XConnectionType_Direct);
    XQ_CHECK_PTR(connValue);
    XQ_CHECK_PTR(connText);
    xq_sb_valueChanged = 0;
    xq_sb_textChanged = 0;
    XSpinBox_setValue(spin, 42);
    XQ_EQ_I64(xq_sb_valueChanged, 1);
    XQ_CHECK(xq_sb_textChanged >= 1);
    XSpinBox_setValue(spin, 42);          /* 同值：不发 valueChanged */
    XQ_EQ_I64(xq_sb_valueChanged, 1);

    /* clear() 保留前后缀（qabstractspinbox.cpp:510-517 实测 '$ ms'），
     * 值不变、不发射 valueChanged。 */
    XSpinBox_setPrefix(spin, "$");
    XSpinBox_setSuffix(spin, " ms");
    XSpinBox_setValue(spin, 44);
    xq_sb_valueChanged = 0;
    XSpinBox_clear(spin);
    XQ_EQ_STR(XAbstractSpinBox_text(base), "$ ms");
    XQ_EQ_I64(XSpinBox_value(spin), 44);
    XQ_EQ_I64(xq_sb_valueChanged, 0);
    /* cleared 期间 interpret 早退（qabstractspinbox.cpp:1881），失焦刷新
     * （updateEdit）同样早退（:1773）——实测失焦后文本仍为 '$ ms'
     * （gap 97）。 */
    XAbstractSpinBox_interpretText(base);
    XQ_EQ_I64(XSpinBox_value(spin), 44);
    XAbstractSpinBox_updateEdit_base(base);
    XQ_EQ_STR(XAbstractSpinBox_text(base), "$ ms");
    /* 同值 setValue 复位 cleared 并回填文本（setValue 首段
     * `cleared = false; updateEdit();`，实测 '$44 ms'）。 */
    XSpinBox_setValue(spin, 44);
    XQ_EQ_STR(XAbstractSpinBox_text(base), "$44 ms");
    /* 清空态下键入合法文本 ⇒ 值更新（键盘跟踪缺省开，Qt editorTextChanged
     * → setValue；探针实测程序性 setText 同样提交）。 */
    XSpinBox_clear(spin);
    XQ_EQ_STR(XAbstractSpinBox_text(base), "$ ms");
    XLineEdit_setText(edit, "$7 ms");
    XQ_EQ_I64(XSpinBox_value(spin), 7);

    XClassDelete((XClass*)spin);
}

/** @brief 交付给控件一次键盘按键（合成事件直投事件系统）。 */
static void xq_sb_key(XSpinBox* spin, int key, int mods)
{
    XKeyEvent ke;
    XKeyEvent_init(&ke, XEVENT_TYPE_KEY_PRESS, key,
                   (XKeyboardModifiers)mods);
    XObject_event_base((XObject*)spin, (XEvent*)&ke);
}

/** @brief Home/End 与 Qt 6.8 同口径：无 Shift 交编辑框移光标（值不变），
 *         Shift 组合选到前缀后/后缀前（qabstractspinbox.cpp:1074-1093；
 *         本机 Qt 探针实测 Home→cursor 0、End→行尾、Shift+Home(2) 选
 *         '5'、Shift+End(2) 选 '0'）。 */
XQ_TEST(XSpinBox, homeEndSelection)
{
    XSpinBox* spin = XSpinBox_create(NULL, 0);
    XLineEdit* edit;
    char* sel;
    XQ_REQUIRE(spin != NULL);
    edit = XSpinBox_lineEdit(spin);
    XQ_REQUIRE(edit != NULL);
    XSpinBox_setRange(spin, 0, 100);
    XSpinBox_setValue(spin, 50);
    XSpinBox_setPrefix(spin, "$");
    XSpinBox_setSuffix(spin, " ms");
    XQ_EQ_STR(XAbstractSpinBox_text((XAbstractSpinBox*)spin), "$50 ms");

    /* 无 Shift：Home/End 交内嵌编辑框，光标到行首/行尾，**值不变**。
     * 此前本库把 Home/End 当「跳到 minimum/maximum」的大步数步进
     * （gap 93：按 Home 直接改值 = 数据破坏）。 */
    XLineEdit_setCursorPosition(edit, 2);
    xq_sb_key(spin, XKey_Home, XKeyboardModifier_NoModifier);
    XQ_EQ_I64(XSpinBox_value(spin), 50);
    XQ_EQ_I64(XLineEdit_cursorPosition(edit), 0);
    xq_sb_key(spin, XKey_End, XKeyboardModifier_NoModifier);
    XQ_EQ_I64(XSpinBox_value(spin), 50);
    XQ_EQ_I64(XLineEdit_cursorPosition(edit), 6); /* "$50 ms" 6 字符 */

    /* Shift+Home：从光标选到前缀后（前缀 1 字符 ⇒ 选 '5'）。 */
    XLineEdit_setCursorPosition(edit, 2);
    xq_sb_key(spin, XKey_Home, XKeyboardModifier_ShiftModifier);
    XQ_EQ_I64(XSpinBox_value(spin), 50);
    XQ_EQ_I64(XLineEdit_selectionStart(edit), 1);
    XQ_EQ_I64(XLineEdit_cursorPosition(edit), 1);
    sel = XLineEdit_selectedText(edit);
    XQ_CHECK(sel != NULL);
    if (sel) {
        XQ_EQ_STR(sel, "5");
        XFree_System(sel);
    }

    /* Shift+End：从光标选到后缀前（"$50 ms" 后 3 字符是后缀 ⇒ 选 '0'）。 */
    XLineEdit_setCursorPosition(edit, 2);
    xq_sb_key(spin, XKey_End, XKeyboardModifier_ShiftModifier);
    XQ_EQ_I64(XSpinBox_value(spin), 50);
    XQ_EQ_I64(XLineEdit_selectionStart(edit), 2);
    XQ_EQ_I64(XLineEdit_cursorPosition(edit), 3);
    sel = XLineEdit_selectedText(edit);
    XQ_CHECK(sel != NULL);
    if (sel) {
        XQ_EQ_STR(sel, "0");
        XFree_System(sel);
    }

    XClassDelete((XClass*)spin);
}

/** @brief 长按步进按钮：按下即步进，阈值后连发；accelerated=true 时
 *         同样时长步进数更多（对标 Qt spinClickThresholdTimer/
 *         spinClickTimer 的 accelerated 分支，qabstractspinbox.cpp:
 *         1228-1268；本机 Qt 实测 1s 按住 accel=0 走 4 步、accel=1 走 5 步）。
 * @note   定时器需要线程事件调度器：先建 XEventLoop（同 XObject 测试），
 *         再用 processEventsWithMaxTime 真实等时（该函数已按真实时间片
 *         兑现 maxtime，2026-11-01 根修）。
 */
static int xq_sb_holdSpin(XEventLoop* loop, bool accelerated, int holdMs)
{
    XSpinBox* spin = XSpinBox_create(NULL, 0);
    XMouseEvent me;
    XPoint pos;
    int value = 0;
    int w;
    (void)loop;
    if (!spin) return -1;
    XSpinBox_setRange(spin, 0, 100000);
    XSpinBox_setValue(spin, 0);
    XAbstractSpinBox_setAccelerated((XAbstractSpinBox*)spin, accelerated);
    XWidget_resize((XWidget*)spin, 120, 30);
    w = XWidget_width((XWidget*)spin);
    /* 上按钮：右上角（样式列总在右缘；无样式回退=右 16px 列上半）。 */
    pos.x = w - 4;
    pos.y = 4;
    XMouseEvent_init(&me, XEVENT_TYPE_MOUSE_BUTTON_PRESS,
                     XMouseButton_LeftButton, XKeyboardModifier_NoModifier,
                     pos);
    XObject_event_base((XObject*)spin, (XEvent*)&me);
    /* 按下即步进一拍（Qt updateState 的首步）。 */
    XQ_CHECK(XAbstractSpinBox_isSpinButtonActive((XAbstractSpinBox*)spin));
    {
        int afterPress = XSpinBox_value(spin);
        XQ_EQ_I64(afterPress, 1);
        XCoreApplication_processEventsWithMaxTime(XEventLoop_AllEvents,
                                                  holdMs);
        value = XSpinBox_value(spin);
        XQ_CHECK(value > afterPress);
    }
    XMouseEvent_init(&me, XEVENT_TYPE_MOUSE_BUTTON_RELEASE,
                     XMouseButton_LeftButton, XKeyboardModifier_NoModifier,
                     pos);
    XObject_event_base((XObject*)spin, (XEvent*)&me);
    XQ_CHECK(!XAbstractSpinBox_isSpinButtonActive((XAbstractSpinBox*)spin));
    XClassDelete((XClass*)spin);
    return value;
}

XQ_TEST(XSpinBox, spinButtonPressRepeatAndAccelerate)
{
    XEventLoop* loop = XEventLoop_create();
    int normal;
    int accel;
    XQ_REQUIRE(loop != NULL);
    normal = xq_sb_holdSpin(loop, false, 1800);
    accel = xq_sb_holdSpin(loop, true, 1800);
    /* 未加速：阈值 500ms 后按 150ms 连发 ⇒ 1.8s 内明显多于首拍。 */
    XQ_CHECK(normal > 2);
    /* 加速：每拍把间隔缩短 5%（下限 10ms）⇒ 同样时长步进更多。 */
    XQ_CHECK(accel > normal);
    XClassDelete((XClass*)loop);
}

#endif /* XGUI_ON && XWIDGET_ON && XSPINBOX_ON && XLINEEDIT_ON && XABSTRACTSPINBOX_ON */
