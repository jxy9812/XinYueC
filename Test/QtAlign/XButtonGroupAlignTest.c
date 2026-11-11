/**
 * @file       XButtonGroupAlignTest.c
 * @brief      XButtonGroup × Qt 6.8 QButtonGroup 对齐契约测试。
 * @details    对齐依据：/opt/Qt/6.8.3/gcc_64/include/QtWidgets/qbuttongroup.h
 *             与本机 Qt 6.8.3 源码
 *             /home/xinyue/Qt-src/6.8.3/Src/qtbase/src/widgets/widgets/qbuttongroup.cpp
 *             （私有实现见 qbuttongroup_p.cpp 的 detectCheckedButton）。
 *             关键语义：
 *             - 默认 exclusive=true（Q_PROPERTY 缺省）；
 *             - addButton 指定 id；id<0 时自动分配负序 id（首个 -2、其后
 *               取映射最小值 -1，qbuttongroup.cpp:206-225）；按钮换组时先从
 *               原组 removeButton；
 *             - setId 的 id==-1 是保留哨兵，写入被忽略
 *               （qbuttongroup.cpp:288-293）；
 *             - removeButton 若移的是 tracked 选中按钮，先
 *               detectCheckedButton（qbuttongroup.cpp:8-21、232-243）：
 *               互斥组直接落空，非互斥组回落到其他选中按钮；
 *             - 组析构把全体成员的 group 承载清空
 *               （qbuttongroup.cpp:99-103）。
 * @author     XinYueC 团队
 */
#include "xq_align.h"

#include "XGuiConfig.h"

#if XGUI_ON && XABSTRACTBUTTON_ON && XBUTTONGROUP_ON

#include "XButtonGroup.h"
#include "XAbstractButton.h"
#include "XPushButton.h"
#include "XWidget.h"
#include "XObject.h"
#include "XVector.h"
#include "XClass.h"

/** @brief 组成员数（借用）。 */
static int64_t xbg_count(const XButtonGroup* group)
{
    const XVector* list = XButtonGroup_buttons(group);
    return list ? (int64_t)XVector_size_base((const XContainer*)list) : 0;
}

/** @brief 造一个可选中按钮（父为 NULL，用例自行释放）。 */
static XPushButton* xbg_makeButton(const char* text)
{
    XPushButton* button = XPushButton_create(NULL, 0);
    if (!button) return NULL;
    XAbstractButton_setText_2((XAbstractButton*)button, text);
    XAbstractButton_setCheckable((XAbstractButton*)button, true);
    return button;
}

/* ==================== ① 默认值 + id 映射 + 选中跟踪 ==================== */

XQ_TEST(XButtonGroup, defaultsIdsAndChecked)
{
    XButtonGroup* group = XButtonGroup_create(NULL);
    XPushButton* b1;
    XPushButton* b2;
    XPushButton* b3;

    XQ_REQUIRE(group != NULL);
    b1 = xbg_makeButton("一");
    b2 = xbg_makeButton("二");
    b3 = xbg_makeButton("三");
    XQ_REQUIRE(b1 != NULL && b2 != NULL && b3 != NULL);

    /* 构造默认：互斥、无成员、无选中、checkedId=-1。 */
    XQ_CHECK(XButtonGroup_exclusive(group));
    XQ_CHECK(XButtonGroup_isExclusive(group));
    XQ_EQ_I64(xbg_count(group), 0);
    XQ_CHECK(XButtonGroup_checkedButton(group) == NULL);
    XQ_EQ_I64(XButtonGroup_checkedId(group), -1);
    XQ_CHECK(XButtonGroup_button(group, 0) == NULL);

    /* 指定 id 登记 + group() 回写（qbuttongroup.cpp:206-225）。 */
    XButtonGroup_addButton(group, (XAbstractButton*)b1, 10);
    XQ_EQ_I64(xbg_count(group), 1);
    XQ_CHECK(XButtonGroup_button(group, 10) == (XAbstractButton*)b1);
    XQ_EQ_I64(XButtonGroup_id(group, (XAbstractButton*)b1), 10);
    XQ_CHECK(XAbstractButton_group((XAbstractButton*)b1) == (void*)group);
    /* 重复登记：对标 QButtonGroup::addButton（qbuttongroup.cpp:206-225）
     * `if (d->mapping.contains(button)) removeButton(button);` ——
     * 先摘除旧映射再按新参数追加，自动 id 重新分配（本机 Qt 6.8 探针：
     * addButton(p,5) 后 addButton(p) → id(p)==-2、button(5)==nullptr）。 */
    XButtonGroup_addButton(group, (XAbstractButton*)b1, -1);
    XQ_EQ_I64(xbg_count(group), 1);
    XQ_EQ_I64(XButtonGroup_id(group, (XAbstractButton*)b1), -2);
    XQ_CHECK(XButtonGroup_button(group, 10) == NULL);
    XQ_CHECK(XButtonGroup_button(group, -2) == (XAbstractButton*)b1);
    /* 恢复为 10，便于后续 checkedId 断言。 */
    XButtonGroup_setId(group, (XAbstractButton*)b1, 10);
    XQ_EQ_I64(XButtonGroup_id(group, (XAbstractButton*)b1), 10);

    /* 自动 id：首个 -2，其后 -3（Qt 取映射最小值再 -1）。 */
    XButtonGroup_addButton(group, (XAbstractButton*)b2, -1);
    XQ_EQ_I64(XButtonGroup_id(group, (XAbstractButton*)b2), -2);
    XButtonGroup_addButton(group, (XAbstractButton*)b3, -1);
    XQ_EQ_I64(XButtonGroup_id(group, (XAbstractButton*)b3), -3);
    XQ_EQ_I64(xbg_count(group), 3);

    /* setId 往返；id==-1 是保留哨兵，写入被忽略。 */
    XButtonGroup_setId(group, (XAbstractButton*)b2, 20);
    XQ_EQ_I64(XButtonGroup_id(group, (XAbstractButton*)b2), 20);
    XButtonGroup_setId(group, (XAbstractButton*)b2, -1);
    XQ_EQ_I64(XButtonGroup_id(group, (XAbstractButton*)b2), 20);
    /* 非成员查询：-1。 */
    {
        XPushButton* outsider = xbg_makeButton("外");
        XQ_REQUIRE(outsider != NULL);
        XQ_EQ_I64(XButtonGroup_id(group, (XAbstractButton*)outsider), -1);
        XClassDelete((XClass*)outsider);
    }

    /* 选中跟踪：最近选中者成为 checkedButton，checkedId 跟随。 */
    XAbstractButton_setChecked((XAbstractButton*)b1, true);
    XQ_CHECK(XButtonGroup_checkedButton(group) == (XAbstractButton*)b1);
    XQ_EQ_I64(XButtonGroup_checkedId(group), 10);
    XAbstractButton_setChecked((XAbstractButton*)b2, true);
    XQ_CHECK(XButtonGroup_checkedButton(group) == (XAbstractButton*)b2);
    XQ_EQ_I64(XButtonGroup_checkedId(group), 20);
    /* 互斥组：b1 被取消。 */
    XQ_CHECK(!XAbstractButton_isChecked((XAbstractButton*)b1));

    /* 非互斥：允许同时选中；tracked 仍为最近选中者（Qt notifyChecked）。 */
    XButtonGroup_setExclusive(group, false);
    XAbstractButton_setChecked((XAbstractButton*)b1, true);
    XQ_CHECK(XAbstractButton_isChecked((XAbstractButton*)b1) &&
             XAbstractButton_isChecked((XAbstractButton*)b2));
    XQ_CHECK(XButtonGroup_checkedButton(group) == (XAbstractButton*)b1);

    XButtonGroup_removeButton(group, (XAbstractButton*)b1);
    XButtonGroup_removeButton(group, (XAbstractButton*)b2);
    XButtonGroup_removeButton(group, (XAbstractButton*)b3);
    XQ_EQ_I64(xbg_count(group), 0);
    XClassDelete((XClass*)b1);
    XClassDelete((XClass*)b2);
    XClassDelete((XClass*)b3);
    XClassDelete((XClass*)group);
}

/* ==================== ② 摘除/析构的 tracked 与承载复位 ==================== */

XQ_TEST(XButtonGroup, removeAndDestroyBookkeeping)
{
    XButtonGroup* exclusive = XButtonGroup_create(NULL);
    XPushButton* e1;
    XPushButton* e2;

    XQ_REQUIRE(exclusive != NULL);
    e1 = xbg_makeButton("甲");
    e2 = xbg_makeButton("乙");
    XQ_REQUIRE(e1 != NULL && e2 != NULL);
    XButtonGroup_addButton(exclusive, (XAbstractButton*)e1, 1);
    XButtonGroup_addButton(exclusive, (XAbstractButton*)e2, 2);
    XAbstractButton_setChecked((XAbstractButton*)e1, true);
    XQ_CHECK(XButtonGroup_checkedButton(exclusive) == (XAbstractButton*)e1);

    /* 互斥组：移走 tracked 按钮 → detectCheckedButton 不回溯，落空。 */
    XButtonGroup_removeButton(exclusive, (XAbstractButton*)e1);
    XQ_CHECK(XButtonGroup_checkedButton(exclusive) == NULL);
    XQ_EQ_I64(XButtonGroup_checkedId(exclusive), -1);
    XQ_CHECK(XAbstractButton_group((XAbstractButton*)e1) == NULL);
    XQ_EQ_I64(xbg_count(exclusive), 1);

    /* 组析构：存活成员的 group 承载必须清空（qbuttongroup.cpp:99-103），
       否则按钮后续 setChecked 会解引用已释放的组。 */
    XClassDelete((XClass*)exclusive);
    XQ_CHECK(XAbstractButton_group((XAbstractButton*)e2) == NULL);
    /* 承载清空后按钮仍可独立选中（不再有互斥组语义）。 */
    XAbstractButton_setChecked((XAbstractButton*)e2, true);
    XQ_CHECK(XAbstractButton_isChecked((XAbstractButton*)e2));

    /* 非互斥组：移走 tracked 按钮 → 回落到另一个仍选中的成员。 */
    {
        XButtonGroup* loose = XButtonGroup_create(NULL);
        XPushButton* l1;
        XPushButton* l2;
        XQ_REQUIRE(loose != NULL);
        l1 = xbg_makeButton("丙");
        l2 = xbg_makeButton("丁");
        XQ_REQUIRE(l1 != NULL && l2 != NULL);
        XButtonGroup_setExclusive(loose, false);
        XButtonGroup_addButton(loose, (XAbstractButton*)l1, 7);
        XButtonGroup_addButton(loose, (XAbstractButton*)l2, 8);
        XAbstractButton_setChecked((XAbstractButton*)l1, true);
        XAbstractButton_setChecked((XAbstractButton*)l2, true);
        XQ_CHECK(XButtonGroup_checkedButton(loose) == (XAbstractButton*)l2);
        XButtonGroup_removeButton(loose, (XAbstractButton*)l2);
        XQ_CHECK(XButtonGroup_checkedButton(loose) == (XAbstractButton*)l1);
        XQ_EQ_I64(XButtonGroup_checkedId(loose), 7);
        XClassDelete((XClass*)loose);
        XClassDelete((XClass*)l1);
        XClassDelete((XClass*)l2);
    }

    XClassDelete((XClass*)e1);
    XClassDelete((XClass*)e2);
}

#endif /* XGUI_ON && XABSTRACTBUTTON_ON && XBUTTONGROUP_ON */
