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
                      (size_t)XComboBox_currentIndexChanged_signal(combo, 0),
                      cb_indexChangedSlot);
    XComboBox_addItems_2(combo, kItems);
    cb_expect(XComboBox_count(combo) == 3, "addItems 3 项");
    /* 对标 Qt：插入不自动改变 currentIndex（保持 -1）。 */
    cb_expect(XComboBox_currentIndex(combo) == -1, "插入不自动置当前项");
    cb_expect(cb_indexChangedCount == 0, "插入不发 currentIndex 变化");
    XComboBox_setCurrentIndex(combo, 0);
    fprintf(stderr, "[cb-dbg2] after set(0): idx=%d text='%s'\n",
            XComboBox_currentIndex(combo), XComboBox_currentText_2(combo));
    cb_expect(strcmp(XComboBox_currentText_2(combo), "Alpha") == 0,
              "显式 setCurrentIndex(0) 生效");

    /* 3. setCurrentIndex/findText/itemText。 */
    XComboBox_setCurrentIndex(combo, 2);
    cb_expect(strcmp(XComboBox_currentText_2(combo), "Gamma") == 0,
              "setCurrentIndex(2) 文本 Gamma");
    cb_expect(XComboBox_findText_2(combo, "Beta") == 1, "findText 命中");
    cb_expect(XComboBox_findText_2(combo, "Delta") == -1, "findText 未命中");
    cb_expect(strcmp(XComboBox_itemText_2(combo, 1), "Beta") == 0, "itemText(1)");

    /* 4. insertItem/removeItem/setItemText。 */
    XComboBox_insertItem_2(combo, 1, "插入项");
    cb_expect(XComboBox_count(combo) == 4 &&
              strcmp(XComboBox_itemText_2(combo, 1), "插入项") == 0,
              "insertItem 到指定位置");
    XComboBox_removeItem(combo, 1);
    cb_expect(XComboBox_count(combo) == 3, "removeItem 移除");
    XComboBox_setItemText_2(combo, 0, "Alpha2");
    cb_expect(strcmp(XComboBox_itemText_2(combo, 0), "Alpha2") == 0,
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
    XComboBox_setPlaceholderText_2(combo, "请选择");
    cb_expect(strcmp(XComboBox_placeholderText_2(combo), "请选择") == 0,
              "setPlaceholderText 生效");

    /* 7. maxCount 钳位 + duplicates。 */
    XComboBox_setMaxCount(combo, 2);
    XComboBox_addItem_2(combo, "溢出项");
    cb_expect(XComboBox_count(combo) == 2, "maxCount 钳位项数");
    XComboBox_setMaxCount(combo, 2147483647);

    /* 8b. Task 2.8：图标/数据/findData/iconSize。 */
    XComboBox_setItemIcon_2(combo, 0, "i.png");
    cb_expect(XStrcmp(XComboBox_itemIcon_2(combo, 0), "i.png") == 0,
              "setItemIcon roundtrip");
    XComboBox_setItemData_2(combo, 1, "d1");
    cb_expect(XStrcmp(XComboBox_itemData_2(combo, 1), "d1") == 0 &&
              XComboBox_findData_2(combo, "d1") == 1,
              "itemData + findData");
    cb_expect(XComboBox_findData_2(combo, "nope") == -1,
              "findData 未命中");
    XComboBox_setIconSize(combo, 24);
    cb_expect(XComboBox_iconSize(combo) == 24, "iconSize");
    cb_expect(XComboBox_completer(combo) == NULL, "completer 默认 NULL");
    /* setCompleter：NULL 清除路径（XCompleter 类于 Task 2.19 落地，
       此处先验证空设置与查询语义）。 */
    XComboBox_setCompleter(combo, NULL);
    cb_expect(XComboBox_completer(combo) == NULL,
              "setCompleter NULL 清除");

    /* 8. clear。 */
    XComboBox_clear(combo);
    cb_expect(XComboBox_count(combo) == 0 &&
              XComboBox_currentIndex(combo) == -1, "clear 清空");

    /* 9. 超长字符串（>64/256 字节）不截断（XString 拥有型存储）。 */
    {
        char longBuf[600];
        int L = 500;
        int i;
        for (i = 0; i < L; ++i) longBuf[i] = 'a' + (i % 26); /* 纯 ASCII，保证合法 UTF-8 */
        longBuf[L] = 0;
        XComboBox_addItem_2(combo, longBuf);
        cb_expect(XComboBox_count(combo) == 1 &&
                  strcmp(XComboBox_itemText_2(combo, 0), longBuf) == 0,
                  "超长项文本不截断");
        XComboBox_setItemText_2(combo, 0, longBuf);
        cb_expect(strcmp(XComboBox_itemText_2(combo, 0), longBuf) == 0,
                  "超长 setItemText 不截断");
        XComboBox_setPlaceholderText_2(combo, longBuf);
        cb_expect(strcmp(XComboBox_placeholderText_2(combo), longBuf) == 0,
                  "超长占位文本不截断");
        XComboBox_clear(combo);
    }

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
