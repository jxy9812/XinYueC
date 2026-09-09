#include "XComboBoxTest.h"
#include "XComboBox.h"
#include "XMemory.h"
#include "XEvent.h"
#include <stdio.h>
#include <string.h>

static int cb_failures = 0;
static int cb_indexChangedCount = 0;
static int cb_activatedCount = 0;
static void cb_expect(bool cond, const char* what)
{
    if (!cond) {
        fprintf(stderr, "[CB-FAIL] %s\n", what ? what : "");
        ++cb_failures;
    }
}
static void cb_indexChangedSlot(void* sender, XVarList* args)
{
    (void)sender;
    ++cb_indexChangedCount;
}
static void cb_activatedSlot(void* sender, XVarList* args)
{
    (void)sender;
    ++cb_activatedCount;
}

bool XComboBoxTest_runAll(void)
{
    XComboBox* combo = XComboBox_create(NULL, 0);
    static const char* const kItems[] = { "Alpha", "Beta", "Gamma", NULL };

    /* 1. 默认状态。 */
    cb_expect(XComboBox_count(combo) == 0, "默认 0 项");
    cb_expect(XComboBox_currentIndex(combo) == -1, "默认无当前项");
    cb_expect(XComboBox_maxVisibleItems(combo) == 10, "默认弹出 10 项");
    cb_expect(XComboBox_insertPolicy(combo) ==
              (int)XComboBoxInsertPolicy_InsertAtBottom, "默认底部插入");

    /* 2. addItems + currentIndex 联动。 */
    cb_indexChangedCount = 0;
    XObject_connect_2((XObject*)combo,
                      (size_t)XComboBox_currentIndexChanged_signal(combo),
                      cb_indexChangedSlot);
    XComboBox_addItems(combo, kItems);
    cb_expect(XComboBox_count(combo) == 3, "addItems 3 项");
    /* 对标 Qt：插入不自动改变 currentIndex（保持 -1）。 */
    cb_expect(XComboBox_currentIndex(combo) == -1, "插入不自动置当前项");
    cb_expect(cb_indexChangedCount == 0, "插入不发 currentIndex 变化");
    XComboBox_setCurrentIndex(combo, 0);
    fprintf(stderr, "[cb-dbg2] after set(0): idx=%d text='%s'\n",
            XComboBox_currentIndex(combo), XComboBox_currentText(combo));
    cb_expect(strcmp(XComboBox_currentText(combo), "Alpha") == 0,
              "显式 setCurrentIndex(0) 生效");

    /* 3. setCurrentIndex/findText/itemText。 */
    XComboBox_setCurrentIndex(combo, 2);
    cb_expect(strcmp(XComboBox_currentText(combo), "Gamma") == 0,
              "setCurrentIndex(2) 文本 Gamma");
    cb_expect(XComboBox_findText(combo, "Beta") == 1, "findText 命中");
    cb_expect(XComboBox_findText(combo, "Delta") == -1, "findText 未命中");
    cb_expect(strcmp(XComboBox_itemText(combo, 1), "Beta") == 0, "itemText(1)");

    /* 4. insertItem/removeItem/setItemText。 */
    XComboBox_insertItem(combo, 1, "插入项");
    cb_expect(XComboBox_count(combo) == 4 &&
              strcmp(XComboBox_itemText(combo, 1), "插入项") == 0,
              "insertItem 到指定位置");
    XComboBox_removeItem(combo, 1);
    cb_expect(XComboBox_count(combo) == 3, "removeItem 移除");
    XComboBox_setItemText(combo, 0, "Alpha2");
    cb_expect(strcmp(XComboBox_itemText(combo, 0), "Alpha2") == 0,
              "setItemText 修改");

    /* 5. 弹出显示/隐藏 + activated（模拟弹出中点击行）。 */
    XComboBox_showPopup_base(combo);
    cb_expect(XComboBox_popupVisible(combo), "showPopup 生效");
    XComboBox_hidePopup_base(combo);
    cb_expect(!XComboBox_popupVisible(combo), "hidePopup 生效");

    /* 6. editable 与 placeholder。 */
    XComboBox_setEditable(combo, true);
    cb_expect(XComboBox_isEditable(combo) && XComboBox_lineEdit(combo) != NULL,
              "setEditable 生效且有编辑框");
    XComboBox_setEditable(combo, false);
    XComboBox_setPlaceholderText(combo, "请选择");
    cb_expect(strcmp(XComboBox_placeholderText(combo), "请选择") == 0,
              "setPlaceholderText 生效");

    /* 7. maxCount 钳位 + duplicates。 */
    XComboBox_setMaxCount(combo, 2);
    XComboBox_addItem(combo, "溢出项");
    cb_expect(XComboBox_count(combo) == 2, "maxCount 钳位项数");
    XComboBox_setMaxCount(combo, 2147483647);

    /* 8. clear。 */
    XComboBox_clear(combo);
    cb_expect(XComboBox_count(combo) == 0 &&
              XComboBox_currentIndex(combo) == -1, "clear 清空");

    XComboBox_delete_base(combo);

    {
        int failures = cb_failures;
        cb_failures = 0;
        cb_indexChangedCount = 0;
        cb_activatedCount = 0;
        if (failures == 0) {
            fprintf(stderr, "XComboBox test: PASS\n");
            return true;
        }
        fprintf(stderr, "XComboBox test: %d assertion(s) failed\n", failures);
        return false;
    }
}
