#include "XButtonGroupTest.h"
#include "XButtonGroup.h"
#if XABSTRACTBUTTON_ON && XBUTTONGROUP_ON
#include "XCheckBox.h"
#include "XMemory.h"
#include "XEvent.h"
#include <stdio.h>
#include <string.h>

static int bg_failures = 0;
static int bg_clicked = 0;
static int bg_idClicked = 0;
static int bg_lastId = -1;
static int bg_toggled = 0;

static void bg_expect(bool cond, const char* what)
{
    if (!cond) {
        fprintf(stderr, "[BG-FAIL] %s\n", what ? what : "");
        ++bg_failures;
    }
}

static void bg_clickedSlot(XObject* receiver, XVarList* args)
{
    (void)receiver;
    (void)args;
    ++bg_clicked;
}

static void bg_idClickedSlot(XObject* receiver, XVarList* args)
{
    (void)receiver;
    if (!args) return;
    XVarList_args_1(args, int, id);
    bg_lastId = id;
    ++bg_idClicked;
}

static void bg_toggledSlot(XObject* receiver, XVarList* args)
{
    (void)receiver;
    ++bg_toggled;
}

bool XButtonGroupTest_runAll(void)
{
    XButtonGroup* group = XButtonGroup_create(NULL);
    XCheckBox* b0 = XCheckBox_create(NULL, 0);
    XCheckBox* b1 = XCheckBox_create(NULL, 0);
    XCheckBox* b2 = XCheckBox_create(NULL, 0);

    bg_expect(group != NULL, "XButtonGroup 创建");
    bg_expect(XButtonGroup_exclusive(group), "默认互斥");
    bg_expect(XButtonGroup_checkedId(group) == -1, "初始 checkedId -1");
    bg_expect(XButtonGroup_checkedButton(group) == NULL, "初始无选中");

    /* 1. addButton 与自动 id 分配。 */
    XButtonGroup_addButton(group, (XAbstractButton*)b0, -1);
    XButtonGroup_addButton(group, (XAbstractButton*)b1, -1);
    bg_expect(XButtonGroup_id(group, (XAbstractButton*)b0) == -2,
              "首个自动 id -2（对标 Qt）");
    bg_expect(XButtonGroup_id(group, (XAbstractButton*)b1) == -3,
              "次个自动 id -3（对标 Qt）");
    XButtonGroup_addButton(group, (XAbstractButton*)b2, 5);
    bg_expect(XButtonGroup_id(group, (XAbstractButton*)b2) == 5,
              "显式 id 5");
    bg_expect(XButtonGroup_buttons(group) != NULL &&
              XVector_size_base(XButtonGroup_buttons(group)) == 3,
              "成员数 3");
    bg_expect(XButtonGroup_button(group, 5) == (XAbstractButton*)b2,
              "button(5) 反查 b2");
    bg_expect(XButtonGroup_button(group, 99) == NULL, "未知 id NULL");

    /* 2. 选中与互斥（exclusive）。 */
    XAbstractButton_setChecked((XAbstractButton*)b0, true);
    fprintf(stderr, "[bg-dbg] checkedId=%d checkedB=%p b0=%p\n",
            XButtonGroup_checkedId(group),
            (void*)XButtonGroup_checkedButton(group), (void*)b0);
    bg_expect(XButtonGroup_checkedButton(group) == (XAbstractButton*)b0,
              "checkedButton 为 b0");
    bg_expect(XButtonGroup_checkedId(group) == -2, "b0 自动 id -2");
    XAbstractButton_setChecked((XAbstractButton*)b1, true);
    bg_expect(XButtonGroup_checkedButton(group) == (XAbstractButton*)b1,
              "互斥后选中 b1");
    bg_expect(!XAbstractButton_isChecked((XAbstractButton*)b0),
              "互斥取消 b0");

    /* 3. setId 改写与信号。 */
    XButtonGroup_setId(group, (XAbstractButton*)b1, 7);
    bg_expect(XButtonGroup_id(group, (XAbstractButton*)b1) == 7, "setId 7");

    bg_clicked = 0;
    bg_idClicked = 0;
    bg_lastId = -99;
    bg_toggled = 0;
    XObject_connect_2((XObject*)group,
        XSignal(XButtonGroup_buttonClicked_signal), bg_clickedSlot);
    XObject_connect_2((XObject*)group,
        XSignal(XButtonGroup_idClicked_signal), bg_idClickedSlot);
    XObject_connect_2((XObject*)group,
        XSignal(XButtonGroup_buttonToggled_signal), bg_toggledSlot);
    XAbstractButton_click((XAbstractButton*)b2);
    bg_expect(bg_clicked == 1, "click 转发 buttonClicked 一次");
    bg_expect(bg_idClicked == 1 && bg_lastId == 5,
              "idClicked 携带 id 5 一次");
    bg_expect(bg_toggled >= 1, "click 选中 b2 转发 buttonToggled");
    bg_expect(XButtonGroup_checkedButton(group) == (XAbstractButton*)b2,
              "click 后选中 b2");

    /* 4. removeButton 与 destroyed 语义。 */
    XButtonGroup_removeButton(group, (XAbstractButton*)b2);
    bg_expect(XButtonGroup_buttons(group) != NULL &&
              XVector_size_base(XButtonGroup_buttons(group)) == 2,
              "移除后成员数 2");
    bg_expect(XButtonGroup_checkedId(group) == -1, "移除选中项后 checkedId -1");

    XButtonGroup_delete_base(group);
    XCheckBox_delete_base(b0);
    XCheckBox_delete_base(b1);
    XCheckBox_delete_base(b2);

    {
        int failures = bg_failures;
        bg_failures = 0;
        if (failures == 0) {
            fprintf(stderr, "XButtonGroup test: PASS\n");
            return true;
        }
        fprintf(stderr, "XButtonGroup test: %d assertion(s) failed\n",
                failures);
        return false;
    }
}
#else
bool XButtonGroupTest_runAll(void)
{
    fprintf(stderr, "XButtonGroupTest: skipped (module off)\n");
    return true;
}
#endif /* XABSTRACTBUTTON_ON && XBUTTONGROUP_ON */
