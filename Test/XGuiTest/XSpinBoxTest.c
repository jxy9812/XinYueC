/**
 * @file       XSpinBoxTest.c
 * @brief      XSpinBox 微调框控件回归测试（对齐 Qt 6.8 QSpinBox 语义）。
 * @details    覆盖：默认状态、setValue/setRange/setMinimum/setMaximum
 *             钳位、StepBy/stepUp/stepDown 步进、文本输入解析与提交修正
 *             （CorrectToPreviousValue/CorrectToNearestValue）、按钮符号
 *             枚举数值、基类属性（keyboardTracking/readOnly/frame/
 *             alignment/specialValueText/wrapping/accelerated/
 *             groupSeparatorShown）、键盘步进（Up/Down/PageUp/PageDown/
 *             Home/End）、滚轮步进、prefix/suffix、displayIntegerBase
 *             进制、特殊值文本、千分位、wrapping 循环、validate 状态、
 *             clear/selectAll、valueChanged/textChanged 信号。
 * @author     XinYueC 团队
 ******************************************************************************/
#include "XSpinBoxTest.h"
#include "XSpinBox.h"
#include "XAbstractSpinBox.h"
#include "XLineEdit.h"
#include "XMemory.h"
#include "XEvent.h"
#include "XWindowEvent.h"
#include <stdio.h>
#include <string.h>

static int sb_failures = 0;
static void sb_expect(bool cond, const char* what)
{
    if (!cond) {
        fprintf(stderr, "[SB-FAIL] %s\n", what ? what : "");
        ++sb_failures;
    }
}

/** @brief 向微调框注入一次键盘按下（经事件入口分派）。 */
static void sb_key(XAbstractSpinBox* spin, int key)
{
    XKeyEvent ke;
    XKeyEvent_init(&ke, XEVENT_TYPE_KEY_PRESS, key, 0);
    XObject_event_base((XObject*)spin, (XEvent*)&ke);
}

/** @brief 向微调框注入一次滚轮（垂直角度增量 dy，120=1 步）。 */
static void sb_wheel(XAbstractSpinBox* spin, int dy)
{
    XWheelEvent we;
    XPoint pos = { 5, 5 };
    XPoint gpos = { 50, 50 };
    XPoint delta = { 0, dy };
    XWheelEvent_init(&we, XEVENT_TYPE_WHEEL, &pos, &gpos, &delta,
                     XMouseButton_NoButton, 0);
    XObject_event_base((XObject*)spin, (XEvent*)&we);
}

/** @brief 编辑框文本（快捷读取）。 */
static const char* sb_text(XAbstractSpinBox* spin)
{
    return XAbstractSpinBox_text(spin);
}

/* valueChanged / textChanged 信号计数 */
static int sb_valueChangedCount = 0;
static int sb_textChangedCount = 0;
static void sb_onValueChanged(void* sender, XVarList* args)
{
    (void)sender;
    (void)args;
    ++sb_valueChangedCount;
}
static void sb_onTextChanged(void* sender, XVarList* args)
{
    (void)sender;
    (void)args;
    ++sb_textChangedCount;
}

bool XSpinBoxTest_runAll(void)
{
    XSpinBox* spin = XSpinBox_create(NULL, 0);
    XAbstractSpinBox* base = (XAbstractSpinBox*)spin;
    XLineEdit* edit;
    XRect geom;

    if (!spin) {
        fprintf(stderr, "XSpinBox test: create failed\n");
        return false;
    }
    edit = XAbstractSpinBox_lineEdit(base);

    /* 1. 默认状态。 */
    sb_expect(XSpinBox_minimum(spin) == 0 && XSpinBox_maximum(spin) == 99,
              "默认范围 0..99");
    sb_expect(XSpinBox_value(spin) == 0, "默认值 0");
    sb_expect(XSpinBox_singleStep(spin) == 1, "默认单步 1");
    sb_expect(edit != NULL, "基类拥有内嵌编辑框");
    sb_expect(XAbstractSpinBox_buttonSymbols(base) ==
              (int)XAbstractSpinBoxButtonSymbols_UpDownArrows,
              "默认按钮符号 UpDownArrows");
    sb_expect(XAbstractSpinBox_correctionMode(base) ==
              (int)XAbstractSpinBoxCorrectionMode_CorrectToPreviousValue,
              "默认修正模式 CorrectToPreviousValue");
    sb_expect(XSpinBox_stepType(spin) ==
              (int)XAbstractSpinBoxStepType_DefaultStepType,
              "默认步进类型 DefaultStepType");
    sb_expect(XSpinBox_displayIntegerBase(spin) == 10, "默认进制 10");
    sb_expect(!XAbstractSpinBox_wrapping(base), "默认不循环");
    sb_expect(!XAbstractSpinBox_isAccelerated(base), "默认不加速");
    sb_expect(!XAbstractSpinBox_isGroupSeparatorShown(base),
              "默认不显示千分位");
    sb_expect(strcmp(XAbstractSpinBox_specialValueText(base), "") == 0,
              "默认无特殊值文本");
    sb_expect(strcmp(sb_text(base), "0") == 0, "默认文本 0");
    {
        char* clean = XSpinBox_cleanText(spin);
        sb_expect(clean && strcmp(clean, "0") == 0, "默认纯净文本 0");
        XFree_System(clean);
    }
    sb_expect(XAbstractSpinBox_hasAcceptableInput(base), "默认输入可接受");

    /* 2. setValue 钳位 + 编辑框文本同步。 */
    XSpinBox_setValue(spin, 50);
    sb_expect(XSpinBox_value(spin) == 50, "setValue(50) 生效");
    sb_expect(strcmp(sb_text(base), "50") == 0, "编辑框文本同步为 50");
    XSpinBox_setValue(spin, 200);
    sb_expect(XSpinBox_value(spin) == 99, "越界钳位到 max");
    XSpinBox_setValue(spin, -5);
    sb_expect(XSpinBox_value(spin) == 0, "负值钳位到 min");

    /* 3. setRange/setMinimum/setMaximum（Qt 语义：不交换，端收敛）。 */
    XSpinBox_setRange(spin, 10, 0);
    sb_expect(XSpinBox_minimum(spin) == 10 && XSpinBox_maximum(spin) == 10,
              "setRange(min>max) 上限收敛为 min");
    XSpinBox_setRange(spin, 0, 100);
    sb_expect(XSpinBox_minimum(spin) == 0 && XSpinBox_maximum(spin) == 100,
              "setRange 生效");
    XSpinBox_setMinimum(spin, 20);
    sb_expect(XSpinBox_minimum(spin) == 20 && XSpinBox_maximum(spin) == 100,
              "setMinimum 单独设置");
    XSpinBox_setMaximum(spin, 50);
    sb_expect(XSpinBox_minimum(spin) == 20 && XSpinBox_maximum(spin) == 50,
              "setMaximum 单独设置");
    XSpinBox_setMinimum(spin, 200);
    sb_expect(XSpinBox_minimum(spin) == 200 && XSpinBox_maximum(spin) == 200,
              "setMinimum 超过上限时上限收敛");
    XSpinBox_setMaximum(spin, 150);
    sb_expect(XSpinBox_minimum(spin) == 150 && XSpinBox_maximum(spin) == 150,
              "setMaximum 低于下限时下限收敛");
    XSpinBox_setRange(spin, 0, 100);
    sb_expect(XSpinBox_value(spin) == 100, "范围变更重钳位");

    /* 4. 步进虚槽（StepBy 经基类入口多态分派）。 */
    XSpinBox_setValue(spin, 50);
    XAbstractSpinBox_stepBy_base(base, 1);
    sb_expect(XSpinBox_value(spin) == 51, "stepBy(+1) 生效");
    XAbstractSpinBox_stepBy_base(base, -1);
    sb_expect(XSpinBox_value(spin) == 50, "stepBy(-1) 生效");
    XAbstractSpinBox_stepBy_base(base, 100);
    sb_expect(XSpinBox_value(spin) == 100, "stepBy 钳位到 max");
    XSpinBox_setSingleStep(spin, 5);
    XAbstractSpinBox_stepBy_base(base, 1);
    sb_expect(XSpinBox_value(spin) == 100, "singleStep=5 后步进仍钳位");
    XSpinBox_setValue(spin, 90);
    XAbstractSpinBox_stepUp(base);
    sb_expect(XSpinBox_value(spin) == 95, "stepUp 按 singleStep 步进");
    XAbstractSpinBox_stepDown(base);
    sb_expect(XSpinBox_value(spin) == 90, "stepDown 按 singleStep 步进");
    XSpinBox_setSingleStep(spin, 1);

    /* 4b. 键入字母被校验器拒绝（编辑框文本不含字母，对标 QSpinBox）。 */
    {
        XLineEdit* le = XSpinBox_lineEdit(spin);
        XKeyEvent ke;
        XSpinBox_setValue(spin, 7);
        XKeyEvent_init(&ke, XEVENT_TYPE_KEY_PRESS, 'a', 0);
        XObject_event_base((XObject*)le, (XEvent*)&ke);
        sb_expect(strcmp(XLineEdit_text(le), "7") == 0,
                  "键入字母被数字校验器拒绝");
        XKeyEvent_init(&ke, XEVENT_TYPE_KEY_PRESS, '5', 0);
        XObject_event_base((XObject*)le, (XEvent*)&ke);
        sb_expect(strcmp(XLineEdit_text(le), "75") == 0,
                  "键入数字正常进入");
    }

    /* 5. 文本输入解析联动（编辑框 setText → 数值更新）。 */
    XSpinBox_setValue(spin, 10);
    XLineEdit_setText(edit, "42");
    sb_expect(XSpinBox_value(spin) == 42, "文本 42 解析为值");
    XLineEdit_setText(edit, "500");
    sb_expect(XSpinBox_value(spin) == 42, "越界文本保持原值（Qt 语义）");
    XAbstractSpinBox_interpretText(base);
    sb_expect(XSpinBox_value(spin) == 42, "CorrectToPreviousValue 保持原值");
    sb_expect(strcmp(sb_text(base), "42") == 0,
              "CorrectToPreviousValue 恢复显示");
    XLineEdit_setText(edit, "abc");
    sb_expect(XSpinBox_value(spin) == 42, "非法文本保持原值");
    XAbstractSpinBox_interpretText(base);
    sb_expect(strcmp(sb_text(base), "42") == 0, "非法文本提交后恢复");

    /* 6. CorrectToNearestValue：非法文本取最近合法值。 */
    XAbstractSpinBox_setCorrectionMode(
        base, (int)XAbstractSpinBoxCorrectionMode_CorrectToNearestValue);
    XLineEdit_setText(edit, "500");
    sb_expect(XSpinBox_value(spin) == 42, "越界文本编辑中保持原值");
    XAbstractSpinBox_interpretText(base);
    sb_expect(XSpinBox_value(spin) == 100, "CorrectToNearestValue 钳位到 max");
    sb_expect(strcmp(sb_text(base), "100") == 0, "最近合法值显示同步");
    XLineEdit_setText(edit, "abc");
    XAbstractSpinBox_interpretText(base);
    sb_expect(XSpinBox_value(spin) == 100, "完全非法文本回退上次有效值");
    XAbstractSpinBox_setCorrectionMode(
        base, (int)XAbstractSpinBoxCorrectionMode_CorrectToPreviousValue);
    sb_expect(XAbstractSpinBox_correctionMode(base) ==
              (int)XAbstractSpinBoxCorrectionMode_CorrectToPreviousValue,
              "修正模式恢复");

    /* 7. 按钮符号枚举数值对齐 Qt + 设置生效。 */
    sb_expect((int)XAbstractSpinBoxButtonSymbols_UpDownArrows == 0 &&
              (int)XAbstractSpinBoxButtonSymbols_PlusMinus == 1 &&
              (int)XAbstractSpinBoxButtonSymbols_NoButtons == 2,
              "ButtonSymbols 数值对齐 Qt");
    XAbstractSpinBox_setButtonSymbols(
        base, (int)XAbstractSpinBoxButtonSymbols_PlusMinus);
    sb_expect(XAbstractSpinBox_buttonSymbols(base) ==
              (int)XAbstractSpinBoxButtonSymbols_PlusMinus,
              "buttonSymbols 设置生效");
    XAbstractSpinBox_setButtonSymbols(
        base, (int)XAbstractSpinBoxButtonSymbols_NoButtons);
    sb_expect(XAbstractSpinBox_buttonSymbols(base) ==
              (int)XAbstractSpinBoxButtonSymbols_NoButtons,
              "NoButtons 生效");
    XAbstractSpinBox_setButtonSymbols(
        base, (int)XAbstractSpinBoxButtonSymbols_UpDownArrows);

    /* 8. 基类属性转发。 */
    XAbstractSpinBox_setKeyboardTracking(base, false);
    sb_expect(!XAbstractSpinBox_keyboardTracking(base),
              "keyboardTracking(false) 生效");
    XAbstractSpinBox_setKeyboardTracking(base, true);
    XAbstractSpinBox_setReadOnly(base, true);
    sb_expect(XAbstractSpinBox_isReadOnly(base), "readOnly 转发生效");
    XAbstractSpinBox_setReadOnly(base, false);
    sb_expect(!XAbstractSpinBox_isReadOnly(base), "readOnly 恢复");
    XAbstractSpinBox_setFrame(base, false);
    sb_expect(!XAbstractSpinBox_hasFrame(base), "frame(false) 生效");
    XAbstractSpinBox_setFrame(base, true);
    XAbstractSpinBox_setAlignment(base, 2 /* XAlignment 右对齐占位 */);
    sb_expect(XAbstractSpinBox_alignment(base) == 2, "alignment 转发");
    XAbstractSpinBox_setAlignment(base, 0 /* Left */);
    XAbstractSpinBox_setAccelerated(base, true);
    sb_expect(XAbstractSpinBox_isAccelerated(base), "accelerated 生效");
    XAbstractSpinBox_setAccelerated(base, false);
    XAbstractSpinBox_setGroupSeparatorShown(base, true);
    sb_expect(XAbstractSpinBox_isGroupSeparatorShown(base),
              "groupSeparatorShown 生效");
    XAbstractSpinBox_setGroupSeparatorShown(base, false);
    XAbstractSpinBox_setWrapping(base, true);
    sb_expect(XAbstractSpinBox_wrapping(base), "wrapping 生效");
    XAbstractSpinBox_setWrapping(base, false);

    /* 9. 键盘步进（Up/Down/PageUp/PageDown/Home/End）。 */
    XSpinBox_setRange(spin, 0, 100);
    XSpinBox_setValue(spin, 50);
    sb_key(base, XKey_Up);
    sb_expect(XSpinBox_value(spin) == 51, "Up 步进 +1");
    sb_key(base, XKey_Down);
    sb_expect(XSpinBox_value(spin) == 50, "Down 步进 -1");
    sb_key(base, XKey_PageUp);
    sb_expect(XSpinBox_value(spin) == 60, "PageUp 步进 +10");
    sb_key(base, XKey_PageDown);
    sb_expect(XSpinBox_value(spin) == 50, "PageDown 步进 -10");
    sb_key(base, XKey_Home);
    sb_expect(XSpinBox_value(spin) == 0, "Home 跳转到 minimum");
    sb_key(base, XKey_End);
    sb_expect(XSpinBox_value(spin) == 100, "End 跳转到 maximum");
    sb_key(base, XKey_End); /* 已在 max：StepUp 禁用，不动作 */
    sb_expect(XSpinBox_value(spin) == 100, "边界键在边界处不动作");
    sb_key(base, XKey_Down);
    sb_expect(XSpinBox_value(spin) == 99, "键盘步进钳位");

    /* 10. 滚轮步进（120 角度=1 步）。 */
    XSpinBox_setValue(spin, 50);
    sb_wheel(base, 120);
    sb_expect(XSpinBox_value(spin) == 51, "滚轮 +120 步进 +1");
    sb_wheel(base, -240);
    sb_expect(XSpinBox_value(spin) == 49, "滚轮 -240 步进 -2");
    sb_wheel(base, 60);
    sb_wheel(base, 60);
    sb_expect(XSpinBox_value(spin) == 50, "滚轮角度累积 120 步进 +1");

    /* 11. 前缀/后缀。 */
    XSpinBox_setPrefix(spin, "$");
    XSpinBox_setSuffix(spin, "%");
    sb_expect(strcmp(sb_text(base), "$50%") == 0,
              "显示 = 前缀 + 数值 + 后缀");
    XSpinBox_setValue(spin, 60);
    sb_expect(strcmp(sb_text(base), "$60%") == 0, "setValue 同步前后缀文本");
    {
        char* clean = XSpinBox_cleanText(spin);
        sb_expect(clean && strcmp(clean, "60") == 0,
                  "cleanText 无前后缀");
        XFree_System(clean);
    }
    XLineEdit_setText(edit, "$70%");
    sb_expect(XSpinBox_value(spin) == 70, "带前后缀文本解析");
    XLineEdit_setText(edit, "$abc%");
    sb_expect(XSpinBox_value(spin) == 70, "带前后缀非法文本保持原值");
    XAbstractSpinBox_interpretText(base);
    sb_expect(strcmp(sb_text(base), "$70%") == 0, "带前后缀提交恢复");
    sb_expect(XSpinBox_valueFromText_base(spin, "$80%") == 80,
              "valueFromText 剥离前后缀");
    {
        char* t = XSpinBox_textFromValue_base(spin, 90);
        sb_expect(t && strcmp(t, "$90%") == 0, "textFromValue 含前后缀");
        XFree_System(t);
    }

    /* 12. 显示进制（displayIntegerBase）。 */
    XSpinBox_setDisplayIntegerBase(spin, 16);
    XSpinBox_setValue(spin, 26);
    sb_expect(strcmp(sb_text(base), "$1a%") == 0, "base 16 显示 1a");
    sb_expect(XSpinBox_valueFromText_base(spin, "$ff%") == 255,
              "base 16 解析 ff");
    XSpinBox_setDisplayIntegerBase(spin, 2);
    XSpinBox_setValue(spin, 5);
    sb_expect(strcmp(sb_text(base), "$101%") == 0, "base 2 显示 101");
    XSpinBox_setDisplayIntegerBase(spin, 8);
    XSpinBox_setValue(spin, 64);
    sb_expect(strcmp(sb_text(base), "$100%") == 0, "base 8 显示 100");
    XSpinBox_setDisplayIntegerBase(spin, 1);
    sb_expect(XSpinBox_displayIntegerBase(spin) == 10,
              "base 越界（<2）回退 10");
    XSpinBox_setDisplayIntegerBase(spin, 37);
    sb_expect(XSpinBox_displayIntegerBase(spin) == 10,
              "base 越界（>36）回退 10");
    XSpinBox_setDisplayIntegerBase(spin, 10);

    /* 13. 特殊值文本（value==minimum 时整段替换显示）。 */
    XSpinBox_setRange(spin, 0, 100);
    XAbstractSpinBox_setSpecialValueText(base, "Auto");
    XSpinBox_setValue(spin, 0);
    sb_expect(strcmp(sb_text(base), "Auto") == 0,
              "特殊值文本整段显示（无前后缀）");
    XSpinBox_setValue(spin, 5);
    sb_expect(strcmp(sb_text(base), "$5%") == 0, "非最小值正常显示");
    XLineEdit_setText(edit, "Auto");
    sb_expect(XSpinBox_value(spin) == 0, "输入特殊值文本归 minimum");
    {
        int pos = 0;
        sb_expect(XAbstractSpinBox_validate_base(base, "Auto", &pos) ==
                  XValidatorState_Acceptable, "特殊值文本校验可接受");
    }
    XAbstractSpinBox_setSpecialValueText(base, "");
    sb_expect(strcmp(XAbstractSpinBox_specialValueText(base), "") == 0,
              "清空特殊值文本");

    /* 14. 千分位分隔（group separator）。 */
    XSpinBox_setRange(spin, 0, 100000);
    XSpinBox_setValue(spin, 12345);
    XAbstractSpinBox_setGroupSeparatorShown(base, true);
    sb_expect(strcmp(sb_text(base), "$12,345%") == 0, "千分位显示 12,345");
    {
        char* clean = XSpinBox_cleanText(spin);
        sb_expect(clean && strcmp(clean, "12345") == 0,
                  "cleanText 无千分位");
        XFree_System(clean);
    }
    XLineEdit_setText(edit, "$98,765%");
    sb_expect(XSpinBox_value(spin) == 98765, "带千分位文本解析");
    XAbstractSpinBox_setGroupSeparatorShown(base, false);
    XSpinBox_setRange(spin, 0, 100);
    XSpinBox_setValue(spin, 50);

    /* 15. wrapping 循环步进。 */
    XSpinBox_setRange(spin, 0, 10);
    XSpinBox_setValue(spin, 10);
    XAbstractSpinBox_setWrapping(base, true);
    XAbstractSpinBox_stepBy_base(base, 1);
    sb_expect(XSpinBox_value(spin) == 0, "wrapping 越过 max 绕回 min");
    XAbstractSpinBox_stepBy_base(base, -1);
    sb_expect(XSpinBox_value(spin) == 10, "wrapping 越过 min 绕回 max");
    sb_expect(XAbstractSpinBox_stepEnabled_base(base) ==
              ((int)XAbstractSpinBoxStepEnabledFlag_StepUpEnabled |
               (int)XAbstractSpinBoxStepEnabledFlag_StepDownEnabled),
              "wrapping 时步进恒双向");
    XAbstractSpinBox_setWrapping(base, false);
    XSpinBox_setValue(spin, 0);
    sb_expect(XAbstractSpinBox_stepEnabled_base(base) ==
              (int)XAbstractSpinBoxStepEnabledFlag_StepUpEnabled,
              "min 处仅可上步进");
    XSpinBox_setValue(spin, 10);
    sb_expect(XAbstractSpinBox_stepEnabled_base(base) ==
              (int)XAbstractSpinBoxStepEnabledFlag_StepDownEnabled,
              "max 处仅可下步进");
    XSpinBox_setValue(spin, 5);
    sb_expect(XAbstractSpinBox_stepEnabled_base(base) ==
              ((int)XAbstractSpinBoxStepEnabledFlag_StepUpEnabled |
               (int)XAbstractSpinBoxStepEnabledFlag_StepDownEnabled),
              "中间值双向步进");
    XAbstractSpinBox_setReadOnly(base, true);
    sb_expect(XAbstractSpinBox_stepEnabled_base(base) ==
              (int)XAbstractSpinBoxStepEnabledFlag_StepNone,
              "只读时不可步进");
    XAbstractSpinBox_setReadOnly(base, false);

    /* 16. validate 状态。 */
    {
        int pos = 0;
        XSpinBox_setValue(spin, 5);
        sb_expect(XAbstractSpinBox_validate_base(base, "$5%", &pos) ==
                  XValidatorState_Acceptable, "合法文本 Acceptable");
        sb_expect(XAbstractSpinBox_validate_base(base, "$500%", &pos) ==
                  XValidatorState_Invalid, "正数越界 Invalid");
        sb_expect(XAbstractSpinBox_validate_base(base, "$-5%", &pos) ==
                  XValidatorState_Invalid, "负数低于下限 Invalid");
        sb_expect(XAbstractSpinBox_validate_base(base, "5%", &pos) ==
                  XValidatorState_Invalid, "缺前缀 Invalid");
        sb_expect(XAbstractSpinBox_validate_base(base, "$%", &pos) ==
                  XValidatorState_Intermediate, "空数值 Intermediate");
    }
    XSpinBox_setPrefix(spin, "");
    XSpinBox_setSuffix(spin, "");

    /* 17. clear/selectAll。 */
    XSpinBox_setRange(spin, 0, 100);
    XSpinBox_setValue(spin, 42);
    XAbstractSpinBox_clear_base(base);
    sb_expect(strcmp(sb_text(base), "") == 0, "clear 清空文本");
    sb_expect(XSpinBox_value(spin) == 42, "clear 保持值");
    XAbstractSpinBox_interpretText(base);
    sb_expect(XSpinBox_value(spin) == 42, "clear 后提交不改变值");
    XLineEdit_setText(edit, "7");
    sb_expect(XSpinBox_value(spin) == 7, "clear 后重新输入生效");
    XAbstractSpinBox_selectAll(base);
    sb_expect(XLineEdit_hasSelectedText(edit), "selectAll 建立选区");
    sb_expect(XLineEdit_selectionLength(edit) == 1, "选区覆盖全文");

    /* 18. 信号：valueChanged / textChanged。 */
    sb_valueChangedCount = 0;
    sb_textChangedCount = 0;
    XObject_connect_2((XObject*)spin, XSignal(XSpinBox_valueChanged_signal),
                      sb_onValueChanged);
    XObject_connect_2((XObject*)spin, XSignal(XSpinBox_textChanged_signal),
                      sb_onTextChanged);
    XSpinBox_setValue(spin, 10);
    sb_expect(sb_valueChangedCount == 1, "setValue 发射 valueChanged 一次");
    sb_expect(sb_textChangedCount == 1, "setValue 文本变化发射 textChanged");
    XLineEdit_setText(edit, "20");
    sb_expect(sb_valueChangedCount == 2, "键入解析发射 valueChanged");
    sb_expect(sb_textChangedCount == 2, "键入发射 textChanged");
    XSpinBox_setValue(spin, 20); /* 同值：不发信号 */
    sb_expect(sb_valueChangedCount == 2, "同值 setValue 不发射");
    sb_expect(sb_textChangedCount == 2, "同值 setValue 不改变文本");
    sb_key(base, XKey_Up);
    sb_expect(sb_valueChangedCount == 3, "键盘步进发射 valueChanged");
    sb_key(base, XKey_Return);
    sb_expect(strcmp(sb_text(base), "21") == 0, "Return 提交保持合法值");
    sb_expect(sb_valueChangedCount == 3, "合法文本 Return 不重复发射");

    /* 19. 几何布局（编辑框占满减按钮区）。 */
    geom.x = 0; geom.y = 0; geom.width = 100; geom.height = 30;
    XWidget_setGeometryRect((XWidget*)spin, &geom);
    sb_expect(XWidget_width((XWidget*)spin) == 100 &&
              XWidget_height((XWidget*)spin) == 30, "几何设置生效");

    XSpinBox_delete_base(spin);

    {
        int failures = sb_failures;
        sb_failures = 0;
        if (failures == 0) {
            fprintf(stderr, "XSpinBox test: PASS\n");
            return true;
        }
        fprintf(stderr, "XSpinBox test: %d assertion(s) failed\n", failures);
        return false;
    }
}
