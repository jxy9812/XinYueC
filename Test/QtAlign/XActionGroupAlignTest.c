/**
 * @file       XActionGroupAlignTest.c
 * @brief      XActionGroup × Qt 6.8 QActionGroup 对齐契约测试。
 * @details    对齐依据：/opt/Qt/6.8.3/gcc_64/include/QtGui/qactiongroup.h
 *             与本机 Qt 6.8.3 源码
 *             /home/xinyue/Qt-src/6.8.3/Src/qtbase/src/gui/kernel/qactiongroup.cpp。
 *             关键语义：
 *             - 构造默认 exclusionPolicy=Exclusive、enabled/visible=true
 *               （qactiongroup.cpp:14-17、77-81）；
 *             - isExclusive() 判据是「策略非 None」
 *               （qactiongroup.cpp:258-262）；
 *             - checkedAction() 返回**缓存** current，不是成员扫描
 *               （qactiongroup.cpp:296-301）；缓存只在策略非 None 时由
 *               `_q_actionChanged` 维护（qactiongroup.cpp:25-42），
 *               addAction 一个已选中的动作则无条件记它
 *               （qactiongroup.cpp:136-137）；
 *             - setDisabled(b) 是 setEnabled(!b) 的内联便捷形态
 *               （qactiongroup.h:76）；
 *             - setEnabled/setVisible 把状态转发给全部成员
 *               （qactiongroup.cpp:277-300）；成员侧按 QAction 的
 *               「显式启用位」裁决（qaction.cpp:940-951）：组禁用一律
 *               生效，组启用时显式设置过启用态的成员按显式值裁决；
 *             - 动作持有组回指（QActionPrivate::group 的 QPointer，
 *               qaction_p.h:55/77），removeAction/组析构清空回指；
 *               removeAction 另恢复被组强制的启用态（XGui 扩展）。
 *             本机 Qt 6.8 探针（QGuiApplication，offscreen）实测：
 *             - 默认 policy=1、exclusive=1、enabled/visible=1、无选中；
 *             - 非互斥组同时选中 A/B 后 checkedAction()==NULL；
 *             - setEnabled(false) 成员同禁用，恢复后同启用；
 *             - 显式启用/禁用的成员：组禁用一律禁用；组恢复启用时显式
 *               禁用者仍禁用、显式启用者恢复启用、未显式设置者默认启用；
 *             - 组禁用期间成员 setEnabled(true) 不立即生效，组恢复启用后
 *               才生效；组启用期间动作隐藏 → 强制禁用，恢复可见后启用；
 *             - 组禁用时成员 trigger() 不发射 triggered；
 *             - addAction 到禁用组：成员禁用；组恢复启用后启用；
 *             - 动作从禁用组 addAction 到启用组：旧组失去成员、新组启用；
 *             - removeAction 后 Qt 保持成员被组写入的禁用态（本库有意
 *               扩展为恢复，见 XAction_bindGroup @details）；
 *             - setExclusionPolicy(ExclusiveOptional) 后 isExclusive()==true。
 * @author     XinYueC 团队
 */
#include "xq_align.h"

#include "XGuiConfig.h"

#if XGUI_ON && XWIDGET_ON && XACTION_ON

#include "XActionGroup.h"
#include "XAction.h"
#include "XObject.h"
#include "XVector.h"
#include "XVarList.h"
#include "XString.h"
#include "XClass.h"

/** @brief 组成员数（借用）。 */
static int64_t xag_count(const XActionGroup* group)
{
    XVector* list = XActionGroup_actions(group);
    int64_t n = list ? (int64_t)XVector_size_base((const XContainer*)list) : 0;
    if (list) XClassDelete((XClass*)list);
    return n;
}

/** @brief 组 triggered/hovered 计数。 */
static int g_xagTriggered;
static int g_xagHovered;

static void xag_triggeredSlot(XObject* receiver, XVarList* args)
{
    (void)receiver;
    (void)args;
    ++g_xagTriggered;
}

static void xag_hoveredSlot(XObject* receiver, XVarList* args)
{
    (void)receiver;
    (void)args;
    ++g_xagHovered;
}

/* ==================== ① 默认值 + 成员管理 + 状态转发 ==================== */

XQ_TEST(XActionGroup, defaultsAndMembership)
{
    XActionGroup* group = XActionGroup_create(NULL);
    XAction* a1;
    XAction* a2;

    XQ_REQUIRE(group != NULL);

    /* qactiongroup.cpp:14-17 + 77-81：默认互斥、启用、可见、无选中。 */
    XQ_CHECK(XActionGroup_isExclusive(group));
    XQ_CHECK(XActionGroup_isEnabled(group));
    XQ_CHECK(XActionGroup_isVisible(group));
    XQ_CHECK(XActionGroup_checkedAction(group) == NULL);
    XQ_EQ_I64((int)XActionGroup_exclusionPolicy(group),
              (int)XActionGroupExclusionPolicy_Exclusive);
    XQ_EQ_I64(xag_count(group), 0);

    /* addAction(text) 新建成员（以组为父，随组级联释放）。 */
    a1 = XActionGroup_addAction_2(group, "左对齐");
    a2 = XActionGroup_addAction_3(group, NULL, "右对齐");
    XQ_REQUIRE(a1 != NULL && a2 != NULL);
    XQ_EQ_I64(xag_count(group), 2);
    XQ_CHECK(XObject_parent((XObject*)a1) == (XObject*)group);
    XQ_EQ_STR(XString_toUtf8((XString*)XAction_text_const(a1)), "左对齐");
    XQ_EQ_STR(XString_toUtf8((XString*)XAction_text_const(a2)), "右对齐");

    /* 重复加入同一动作被忽略（qactiongroup.cpp:126-128 contains 判定）。 */
    XQ_CHECK(XActionGroup_addAction(group, a1) == a1);
    XQ_EQ_I64(xag_count(group), 2);
    XQ_CHECK(XActionGroup_addAction(group, NULL) == NULL);
    XQ_CHECK(XActionGroup_addAction(NULL, a1) == NULL);
    /* 新建动作的两个重载在 self==NULL 时也必须返回 NULL 且不留下
     * 无主动作（头文件契约；Qt 的成员函数无法表达 NULL this）。 */
    XQ_CHECK(XActionGroup_addAction_2(NULL, "孤儿") == NULL);
    XQ_CHECK(XActionGroup_addAction_3(NULL, NULL, "孤儿") == NULL);

    /* setEnabled/setDisabled 转发到全部成员（qactiongroup.cpp:277-283）。 */
    XActionGroup_setDisabled(group, true);
    XQ_CHECK(!XActionGroup_isEnabled(group));
    XQ_CHECK(!XAction_isEnabled(a1));
    XQ_CHECK(!XAction_isEnabled(a2));
    XActionGroup_setDisabled(group, false);
    XQ_CHECK(XActionGroup_isEnabled(group));
    XQ_CHECK(XAction_isEnabled(a1));
    XQ_CHECK(XAction_isEnabled(a2));

    /* setVisible 转发到全部成员（qactiongroup.cpp:288-300）。 */
    XActionGroup_setVisible(group, false);
    XQ_CHECK(!XActionGroup_isVisible(group));
    XQ_CHECK(!XAction_isVisible(a1));
    XQ_CHECK(!XAction_isVisible(a2));
    XActionGroup_setVisible(group, true);
    XQ_CHECK(XAction_isVisible(a1));
    XQ_CHECK(XAction_isVisible(a2));

    /* 策略往返：isExclusive 判据「非 None」。 */
    XActionGroup_setExclusionPolicy(
        group, XActionGroupExclusionPolicy_ExclusiveOptional);
    XQ_EQ_I64((int)XActionGroup_exclusionPolicy(group),
              (int)XActionGroupExclusionPolicy_ExclusiveOptional);
    XQ_CHECK(XActionGroup_isExclusive(group));
    XActionGroup_setExclusive(group, false);
    XQ_EQ_I64((int)XActionGroup_exclusionPolicy(group),
              (int)XActionGroupExclusionPolicy_None);
    XQ_CHECK(!XActionGroup_isExclusive(group));
    XActionGroup_setExclusive(group, true);
    XQ_CHECK(XActionGroup_isExclusive(group));

    /* removeAction：借用语义，对象不销毁、成员表同步。 */
    XActionGroup_removeAction(group, a2);
    XQ_EQ_I64(xag_count(group), 1);
    XAction_setText_2(a2, "仍可用");
    XQ_EQ_STR(XString_toUtf8((XString*)XAction_text_const(a2)), "仍可用");
    XActionGroup_removeAction(group, a2); /* 非成员：无操作 */
    XActionGroup_removeAction(group, NULL);

    XClassDelete((XClass*)group); /* 成员随组级联释放 */
}

/* ==================== ② 选中缓存 + 互斥联动 + 信号 ==================== */

XQ_TEST(XActionGroup, checkedActionCacheAndExclusive)
{
    XActionGroup* group = XActionGroup_create(NULL);
    XAction* a1;
    XAction* a2;
    XAction* foreign;

    XQ_REQUIRE(group != NULL);
    a1 = XActionGroup_addAction_2(group, "A");
    a2 = XActionGroup_addAction_2(group, "B");
    XQ_REQUIRE(a1 != NULL && a2 != NULL);
    XAction_setCheckable(a1, true);
    XAction_setCheckable(a2, true);

    g_xagTriggered = 0;
    g_xagHovered = 0;
    XObject_connect((XObject*)group,
                      (size_t)XActionGroup_triggered_signal(NULL, NULL),
                      (XObject*)group, xag_triggeredSlot,
                      XConnectionType_Direct);
    XObject_connect((XObject*)group,
                      (size_t)XActionGroup_hovered_signal(NULL, NULL),
                      (XObject*)group, xag_hoveredSlot,
                      XConnectionType_Direct);

    /* 程序化 setChecked 也进缓存（Qt 挂在 QAction::changed 上，
       qactiongroup.cpp:129）——不是只有 triggered 才跟踪。 */
    XAction_setChecked(a1, true);
    XQ_CHECK(XActionGroup_checkedAction(group) == a1);
    XQ_EQ_I64(g_xagTriggered, 0); /* setChecked 不产生组 triggered */

    /* 互斥：选中另一成员 → 旧成员取消、缓存跟随。 */
    XAction_setChecked(a2, true);
    XQ_CHECK(XActionGroup_checkedAction(group) == a2);
    XQ_CHECK(!XAction_isChecked(a1));
    /* 取消当前选中 → 缓存清空。 */
    XAction_setChecked(a2, false);
    XQ_CHECK(XActionGroup_checkedAction(group) == NULL);

    /* 非互斥（None）：不跟踪缓存（qactiongroup.cpp:30-31 的策略闸）。
       Qt 探针：同时选中 A/B 后 checkedAction() 仍为 NULL。 */
    XActionGroup_setExclusive(group, false);
    XAction_setChecked(a1, true);
    XAction_setChecked(a2, true);
    XQ_CHECK(XAction_isChecked(a1) && XAction_isChecked(a2));
    XQ_CHECK(XActionGroup_checkedAction(group) == NULL);

    /* 切回 Exclusive 不回溯重排（qactiongroup.cpp:239-245 只记策略）。 */
    XActionGroup_setExclusive(group, true);
    XQ_CHECK(XActionGroup_checkedAction(group) == NULL);
    XAction_setChecked(a1, false);
    XAction_setChecked(a2, false);

    /* addAction 一个"加入前已选中"的外部动作：无论策略都记为当前
       （qactiongroup.cpp:136-137）；**不取消其它已选中成员**（Qt 只在
       `_q_actionChanged` 里收敛互斥，本机探针：a/b 均 checked、
       checkedAction=b；gaps 205）。 */
    XAction_setChecked(a2, true);
    XQ_CHECK(XActionGroup_checkedAction(group) == a2);
    foreign = XAction_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, NULL, "C");
    XQ_REQUIRE(foreign != NULL);
    XAction_setCheckable(foreign, true);
    XAction_setChecked(foreign, true);
    XQ_CHECK(XActionGroup_addAction(group, foreign) == foreign);
    XQ_CHECK(XActionGroup_checkedAction(group) == foreign);
    XQ_CHECK(XAction_isChecked(a2));      /* 既有成员保持选中（不取消） */
    XQ_CHECK(XAction_isChecked(foreign));
    XActionGroup_removeAction(group, foreign);
    XQ_CHECK(XActionGroup_checkedAction(group) == NULL);
    XAction_setChecked(a2, false);
    XClassDelete((XClass*)foreign);

    /* 信号转发：成员触发 → 组 triggered；成员悬停 → 组 hovered。 */
    XAction_trigger(a1);
    XQ_EQ_I64(g_xagTriggered, 1);
    XQ_CHECK(XActionGroup_checkedAction(group) == a1);
    XAction_hover(a2);
    XQ_EQ_I64(g_xagHovered, 1);

    /* XGui 扩展 setCheckedAction（Qt 无此 API）：同样维护缓存并发
       triggered（XActionGroup.h @details 口径）。 */
    XActionGroup_setCheckedAction(group, a2);
    XQ_CHECK(XActionGroup_checkedAction(group) == a2);
    XQ_CHECK(!XAction_isChecked(a1));
    XQ_EQ_I64(g_xagTriggered, 2);
    /* 非成员/不可选中动作：无操作。 */
    XActionGroup_setCheckedAction(group, NULL);
    XQ_CHECK(XActionGroup_checkedAction(group) == a2);

    XClassDelete((XClass*)group);
}

/* ==================== ③ 组启用裁决：显式启用位（qaction.cpp:940-951） ==================== */

static int g_xagMemberTriggered;

static void xag_memberTriggeredSlot(XObject* receiver, XVarList* args)
{
    (void)receiver;
    (void)args;
    ++g_xagMemberTriggered;
}

XQ_TEST(XActionGroup, enabledRespectsExplicit)
{
    XActionGroup* group = XActionGroup_create(NULL);
    XAction* explicitOn;
    XAction* explicitOff;
    XAction* plain;

    XQ_REQUIRE(group != NULL);
    explicitOn = XAction_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, NULL, "显式启用");
    explicitOff = XAction_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, NULL, "显式禁用");
    plain = XAction_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, NULL, "未显式");
    XQ_REQUIRE(explicitOn != NULL && explicitOff != NULL && plain != NULL);

    /* 加入前记录显式位：一个显式启用、一个显式禁用、一个从未设置。 */
    XAction_setEnabled(explicitOn, true);
    XAction_setEnabled(explicitOff, false);
    XActionGroup_addAction(group, explicitOn);
    XActionGroup_addAction(group, explicitOff);
    XActionGroup_addAction(group, plain);

    /* 组启用时：显式禁用保持禁用，其余启用（Qt 探针 3 after add）。 */
    XQ_CHECK(XAction_isEnabled(explicitOn));
    XQ_CHECK(!XAction_isEnabled(explicitOff));
    XQ_CHECK(XAction_isEnabled(plain));

    /* 组禁用：显式启用不豁免组禁用（qaction.cpp:940-951 组禁用优先）。 */
    XActionGroup_setEnabled(group, false);
    XQ_CHECK(!XAction_isEnabled(explicitOn));
    XQ_CHECK(!XAction_isEnabled(explicitOff));
    XQ_CHECK(!XAction_isEnabled(plain));

    /* 组恢复启用：显式值裁决（探针 2 / 3）。 */
    XActionGroup_setEnabled(group, true);
    XQ_CHECK(XAction_isEnabled(explicitOn));
    XQ_CHECK(!XAction_isEnabled(explicitOff));
    XQ_CHECK(XAction_isEnabled(plain));

    /* 组禁用期间显式启用 → 不立即生效；组恢复启用后按显式值恢复
       （探针 4：setEn(true) 后 enabled=0，group(true) 后 enabled=1）。 */
    XActionGroup_setEnabled(group, false);
    XAction_setEnabled(plain, true);
    XQ_CHECK(!XAction_isEnabled(plain));
    XActionGroup_setEnabled(group, true);
    XQ_CHECK(XAction_isEnabled(plain));

    /* 组启用期间显式禁用 → 组禁用/启用往返后仍禁用（显式位未被组改写）。 */
    XAction_setEnabled(plain, false);
    XQ_CHECK(!XAction_isEnabled(plain));
    XActionGroup_setEnabled(group, false);
    XActionGroup_setEnabled(group, true);
    XQ_CHECK(!XAction_isEnabled(plain));

    /* resetEnabled 清显式位 → 随组启用恢复默认（qaction.cpp:1015-1020）。 */
    XAction_resetEnabled(plain);
    XQ_CHECK(XAction_isEnabled(plain));

    /* 可见性优先：组启用也不能让隐藏动作生效（探针 6）。 */
    XAction_setVisible(explicitOn, false);
    XActionGroup_setEnabled(group, false);
    XActionGroup_setEnabled(group, true);
    XQ_CHECK(!XAction_isEnabled(explicitOn));
    XAction_setVisible(explicitOn, true);
    XQ_CHECK(XAction_isEnabled(explicitOn));

    /* 触发闸：组禁用时成员 trigger 不发射 triggered（探针 8）。 */
    g_xagMemberTriggered = 0;
    XObject_connect((XObject*)explicitOn,
                      (size_t)XAction_triggered_signal(NULL, false),
                      (XObject*)explicitOn, xag_memberTriggeredSlot,
                      XConnectionType_Direct);
    XActionGroup_setEnabled(group, false);
    XAction_trigger(explicitOn);
    XQ_EQ_I64(g_xagMemberTriggered, 0);
    XActionGroup_setEnabled(group, true);
    XAction_trigger(explicitOn);
    XQ_EQ_I64(g_xagMemberTriggered, 1);

    /* 外部创建的动作不归组所有：先删动作（组桥随 destroyed 退订），
       再删组。 */
    XClassDelete((XClass*)explicitOn);
    XClassDelete((XClass*)explicitOff);
    XClassDelete((XClass*)plain);
    XClassDelete((XClass*)group);
}

/* ==================== ④ 组回指 + 移除恢复启用态 ==================== */

XQ_TEST(XActionGroup, groupBackPointerAndRemoveRestores)
{
    XActionGroup* group = XActionGroup_create(NULL);
    XActionGroup* other = XActionGroup_create(NULL);
    XAction* borrowed;
    XAction* explicitOff;
    XAction* late;
    XVector* members;

    XQ_REQUIRE(group != NULL && other != NULL);
    borrowed = XAction_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, NULL, "借用");
    explicitOff = XAction_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, NULL, "显式禁用");
    late = XAction_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, NULL, "后加入");
    XQ_REQUIRE(borrowed != NULL && explicitOff != NULL && late != NULL);

    /* 回指：未入组 NULL，入组后指向组（QAction::actionGroup 同口径）。 */
    XQ_CHECK(XAction_actionGroup(borrowed) == NULL);
    XQ_CHECK(XAction_actionGroup(NULL) == NULL);
    XActionGroup_addAction(group, borrowed);
    XQ_CHECK(XAction_actionGroup(borrowed) == group);

    /* 组禁用 → 成员被强制禁用；removeAction：清回指并恢复启用态。 */
    XActionGroup_setEnabled(group, false);
    XQ_CHECK(!XAction_isEnabled(borrowed));
    XActionGroup_removeAction(group, borrowed);
    XQ_CHECK(XAction_actionGroup(borrowed) == NULL);
    XQ_CHECK(XAction_isEnabled(borrowed)); /* XGui 扩展：恢复默认启用 */
    /* 重复移除/非成员移除是无操作，不破坏已清空的回指。 */
    XActionGroup_removeAction(group, borrowed);
    XQ_CHECK(XAction_actionGroup(borrowed) == NULL);

    /* 显式禁用的成员移除后仍禁用（显式值优先于「恢复默认」）。 */
    XAction_setEnabled(explicitOff, false);
    XActionGroup_addAction(group, explicitOff);
    XActionGroup_setEnabled(group, false);
    XActionGroup_setEnabled(group, true);
    XQ_CHECK(!XAction_isEnabled(explicitOff));
    XActionGroup_removeAction(group, explicitOff);
    XQ_CHECK(XAction_actionGroup(explicitOff) == NULL);
    XQ_CHECK(!XAction_isEnabled(explicitOff));

    /* 组禁用期间加入的成员：先禁用，组恢复启用后启用（探针 5）。 */
    XActionGroup_setEnabled(group, false);
    XAction_setEnabled(late, true);
    XActionGroup_addAction(group, late);
    XQ_CHECK(XAction_actionGroup(late) == group);
    XQ_CHECK(!XAction_isEnabled(late));
    XActionGroup_setEnabled(group, true);
    XQ_CHECK(XAction_isEnabled(late));

    /* 动作从禁用组转投启用组：旧组失去成员、回指改挂、启用态恢复
       （qactiongroup.cpp:139-146；探针 7）。 */
    XActionGroup_addAction(group, borrowed);
    XQ_CHECK(XAction_actionGroup(borrowed) == group);
    XActionGroup_setEnabled(group, false);
    XQ_CHECK(!XAction_isEnabled(borrowed));
    XActionGroup_addAction(other, borrowed);
    XQ_CHECK(XAction_actionGroup(borrowed) == other);
    XQ_CHECK(XAction_isEnabled(borrowed));
    members = XActionGroup_actions(group);
    XQ_REQUIRE(members != NULL);
    /* group 此时只剩 late（explicitOff 已移除、borrowed 已转组）。 */
    XQ_EQ_I64((int64_t)XVector_size_base((const XContainer*)members), 1);
    XClassDelete((XClass*)members);
    /* 删除启用组 other：外部借用动作 borrowed 的回指清空、启用态静默
       恢复（组析构不清显式位）。 */
    XClassDelete((XClass*)other);
    XQ_CHECK(XAction_actionGroup(borrowed) == NULL);
    XQ_CHECK(XAction_isEnabled(borrowed));

    /* 组析构（QPointer 语义，qaction_p.h:55/77）：组禁用期间经由组强制
       禁用的 late，组删除后回指清空并静默恢复启用态；动作对象仍可用。 */
    XActionGroup_setEnabled(group, false);
    XQ_CHECK(!XAction_isEnabled(late));
    XClassDelete((XClass*)group);
    XQ_CHECK(XAction_actionGroup(borrowed) == NULL);
    XQ_CHECK(XAction_actionGroup(explicitOff) == NULL);
    XQ_CHECK(XAction_actionGroup(late) == NULL);
    XQ_CHECK(XAction_isEnabled(borrowed));
    XQ_CHECK(!XAction_isEnabled(explicitOff));
    XQ_CHECK(XAction_isEnabled(late));

    XClassDelete((XClass*)borrowed);
    XClassDelete((XClass*)explicitOff);
    XClassDelete((XClass*)late);
}

/* ========== ⑤ 反选守卫（P0-38）：Exclusive 拒反选 / ExclusiveOptional 允许 ========== */

static int g_xagGuardTriggered;

static void xag_guardTriggeredSlot(XObject* receiver, XVarList* args)
{
    (void)receiver;
    (void)args;
    ++g_xagGuardTriggered;
}

XQ_TEST(XActionGroup, exclusiveActivateGuard)
{
    XActionGroup* group = XActionGroup_create(NULL);
    XAction* a1;
    XAction* a2;

    XQ_REQUIRE(group != NULL);
    a1 = XActionGroup_addAction_2(group, "A");
    a2 = XActionGroup_addAction_2(group, "B");
    XQ_REQUIRE(a1 != NULL && a2 != NULL);
    XAction_setCheckable(a1, true);
    XAction_setCheckable(a2, true);
    g_xagGuardTriggered = 0;
    XObject_connect((XObject*)a1,
                      (size_t)XAction_triggered_signal(NULL, false),
                      (XObject*)a1, xag_guardTriggeredSlot,
                      XConnectionType_Direct);

    /* Exclusive（默认）：activate 选中 a1；再次 activate **不反选**、
       只发 triggered(true)——Qt 守卫原文（qaction.cpp:1092-1099）：
       `checked && group && policy==Exclusive &&
        group->checkedAction()==this → emit triggered(true); return;`。 */
    XAction_activate(a1, XActionEvent_Trigger);
    XQ_CHECK(XAction_isChecked(a1));
    XQ_CHECK(XActionGroup_checkedAction(group) == a1);
    XQ_EQ_I64(g_xagGuardTriggered, 1);
    XAction_activate(a1, XActionEvent_Trigger); /* 反选尝试 */
    XQ_CHECK(XAction_isChecked(a1));            /* 反选被拒 */
    XQ_CHECK(XActionGroup_checkedAction(group) == a1);
    XQ_EQ_I64(g_xagGuardTriggered, 2);          /* 守卫仍发 triggered(true) */

    /* 切换成员不受守卫限制：选 B → A 取消（互斥联动照常），B 成为新主；
       新主 B 的反选同样被拒（守卫跟随组选中缓存换主）。 */
    XAction_activate(a2, XActionEvent_Trigger);
    XQ_CHECK(XAction_isChecked(a2));
    XQ_CHECK(!XAction_isChecked(a1));
    XQ_CHECK(XActionGroup_checkedAction(group) == a2);
    XAction_activate(a2, XActionEvent_Trigger);
    XQ_CHECK(XAction_isChecked(a2));

    /* ExclusiveOptional（"至多一个"）：无守卫，反选允许
       （qaction.cpp:1093-1099 只对 Exclusive 生效）。 */
    XActionGroup_setExclusionPolicy(
        group, XActionGroupExclusionPolicy_ExclusiveOptional);
    XAction_activate(a2, XActionEvent_Trigger);
    XQ_CHECK(!XAction_isChecked(a2));
    XQ_CHECK(XActionGroup_checkedAction(group) == NULL);

    /* None：无守卫且不跟踪缓存——两次触发 = toggle 回原态。 */
    XActionGroup_setExclusionPolicy(group, XActionGroupExclusionPolicy_None);
    XAction_activate(a1, XActionEvent_Trigger);
    XQ_CHECK(XAction_isChecked(a1));
    XAction_activate(a1, XActionEvent_Trigger);
    XQ_CHECK(!XAction_isChecked(a1));
    XQ_CHECK(XActionGroup_checkedAction(group) == NULL);

    XObject_disconnect((XObject*)a1,
                         (size_t)XAction_triggered_signal(NULL, false),
                         (XObject*)a1, xag_guardTriggeredSlot);
    XClassDelete((XClass*)group); /* 成员随组级联释放 */
}

#endif /* XGUI_ON && XWIDGET_ON && XACTION_ON */
