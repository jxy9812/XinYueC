/**
 * @file       XWidgetShortcutAlignTest.c
 * @brief      XWidget × Qt 6.8 QWidget 的 grabShortcut 族对齐契约测试。
 * @details    对齐依据：Qt 6.8.3 原文
 *             /home/xinyue/Qt-src/6.8.3/Src/qtbase/src/widgets/kernel/qwidget.h
 *             （450-453 四条声明）与 qwidget.cpp（11896-11903
 *             grabShortcut、11920-11925 releaseShortcut、11939-11944
 *             setShortcutEnabled、11954-11959 setShortcutAutoRepeat），
 *             以及 gui/kernel/qshortcutmap.cpp 的
 *             addShortcut/removeShortcut/setShortcutEnabled/
 *             setShortcutAutoRepeat/dispatchEvent。
 *
 *             本库的实现口径（见 XWidget.h 的 grabShortcut 族头注释）：
 *             - 登记进既有 XShortcut 全局注册表（不另建表），返回
 *               XShortcut_id()；Qt 的 QShortcutMap 同样按 `--currentId`
 *               单调递减分配 ⇒ 成功 id **非零（通常为负）**、0 = 失败；
 *             - 激活时向所属控件投递 **type == XEVENT_TYPE_SHORTCUT 的
 *               XKeyEvent**（本库没有 QShortcutEvent 载荷类型；差异已在
 *               XWidget_grabShortcut 头注释逐条登记）；
 *             - enabled / autoRepeat 的消费点分别是
 *               XShortcut_match_2 的 enabled 闸与 XWidget_dispatchKeyEvent
 *               的 autoRepeat 守卫（qshortcutmap.cpp:588-591 同口径）；
 *             - **同键多命中（歧义）的派发是本库与 Qt 的真实差异**：Qt
 *               每次按键只向 next->owner 发一条 QShortcutEvent 并按
 *               ambigCount 轮转（qshortcutmap.cpp:560-607、575-587、
 *               605-606）；本库 XShortcut_activate_2 对每个命中登记件发
 *               activatedAmbiguously（Src/XGui/Widget/XShortcut.c:695-723）
 *               ⇒ 每次按键给每个所属控件各投一条。该口径由本文件末的
 *               grabbedShortcutAmbiguityBroadcast 用例锁定（两控件同键，
 *               每按一次两边各 +1），而不是按 Qt 的轮转单发断言。
 * @note       按键一律经**桥接窗口**投递（XWidget_nativeWindow(top)）：
 *             快捷键表只在 VXWidgetWindow_event → XWidget_dispatchKeyEvent
 *             这一段参与，直投控件（XObject_event_base）不经过它
 *             （与 XShortcutAlignTest.autoRepeatGuard 同一取证路径）。
 *             上下文用 ApplicationShortcut，不依赖焦点控件。
 * @author     XinYueC 团队
 */

#include "xq_align.h"

#include "XGuiConfig.h"

#if XGUI_ON && XWIDGET_ON

#include "XWidget.h"
#include "XShortcut.h"
#include "XKeySequence.h"
#include "XObject.h"
#include "XEvent.h"
#include "XClass.h"
#include "XCoreApplication.h"
#include "XVarList.h"
#include "XVector.h"

/* ==================== 事件过滤器探针（计快捷键事件） ==================== */

XCLASS_DEFINE_BEGING(XWidgetShortcutProbe)
XCLASS_DEFINE_EXTEND_END(XWidgetShortcutProbe, XObject)

/** @brief 挂在目标控件上的探针：统计 XEVENT_TYPE_SHORTCUT 投递次数与载荷。 */
typedef struct XWidgetShortcutProbe
{
    XObject m_base;      /**< 基类；必须是第一个成员。 */
    int     m_events;    /**< 收到的快捷键事件数。 */
    int     m_lastKey;   /**< 最近一次快捷键事件的键码。 */
    int     m_lastMods;  /**< 最近一次快捷键事件的修饰位。 */
    int     m_keyPresses;/**< 顺带统计的普通按键事件数（对照用）。 */
} XWidgetShortcutProbe;

static bool VXWidgetShortcutProbe_eventFilter(XObject* self, XObject* watched,
                                              XEvent* event)
{
    XWidgetShortcutProbe* probe = (XWidgetShortcutProbe*)self;
    (void)watched;
    if (!probe || !event) return false;
    if (XEvent_type(event) == XEVENT_TYPE_SHORTCUT) {
        ++probe->m_events;
        probe->m_lastKey = XKeyEvent_key((const XKeyEvent*)event);
        probe->m_lastMods =
            (int)XKeyEvent_modifiers((const XKeyEvent*)event);
    } else if (XEvent_type(event) == XEVENT_TYPE_KEY_PRESS) {
        ++probe->m_keyPresses;
    }
    return false; /* 不消费。 */
}

static XVtable* XWidgetShortcutProbe_class_init(void)
{
    XVTABLE_INIT_DEFAULT(XWidgetShortcutProbe)
    XVTABLE_INHERIT_XCLASS(XObject);
    XVTABLE_OVERLOAD_DEFAULT(EXObject_EventFilter,
                             VXWidgetShortcutProbe_eventFilter);
    return XVTABLE_DEFAULT;
}

static XWidgetShortcutProbe* XWidgetShortcutProbe_create(void)
{
    XWidgetShortcutProbe* probe = (XWidgetShortcutProbe*)XMemory_malloc(
        sizeof(XWidgetShortcutProbe), XCLASS_DEFAULT_MEMORY_TYPE);
    if (!probe) return NULL;
    XMemset(probe, 0, sizeof(*probe));
    XObject_init(&probe->m_base);
    XClassSetVtable(probe, XWidgetShortcutProbe);
    Set_Class_Memory(probe, XCLASS_DEFAULT_MEMORY_TYPE);
    Set_Class_IsHeap(probe, true);
    return probe;
}

/* ==================== 公共夹具 ==================== */

/** @brief 建顶层控件并 show（惰性建桥接窗，按键要经它进快捷键派发）。 */
static XWidget* ws_createTop(void)
{
    XWidget* top = XWidget_create(NULL, 0);
    if (!top) return NULL;
    XWidget_resize(top, 200, 120);
    XWidget_show(top);
    return top;
}

/**
 * @brief 合成一次 KEY_PRESS 并经桥接窗口投递（走 XWidget_dispatchKeyEvent）。
 * @return XCoreApplication_sendEvent 的返回值（快捷键命中被消费 ⇒ true）。
 */
static bool ws_sendKey(XWidget* top, int key, XKeyboardModifiers modifiers,
                       bool autoRepeat)
{
    XKeyEvent event;
    bool consumed;
    if (!top || !XWidget_nativeWindow(top)) return false;
    XKeyEvent_init(&event, XEVENT_TYPE_KEY_PRESS, key, modifiers);
    XKeyEvent_setAutoRepeat(&event, autoRepeat);
    consumed = XCoreApplication_sendEvent((XObject*)XWidget_nativeWindow(top),
                                          (XEvent*)&event);
    XClassDeinit((XClass*)&event);
    return consumed;
}

/** @brief 在 top 的直接子对象里按 id 找 grabShortcut 登记件（白盒断言用）。 */
static XShortcut* ws_findShortcutById(const XWidget* top, int id)
{
    const XVector* children;
    size_t i;
    size_t n;
    if (!top || id == 0) return NULL;
    children = XObject_children((const XObject*)top);
    if (!children) return NULL;
    n = XVector_size_base((const XContainer*)children);
    for (i = 0; i < n; ++i) {
        XObject* child = *(XObject**)XVector_at_base(children, (int64_t)i);
        if (!child) continue;
        if (!XObject_isA_class(child, XShortcut_class_init())) continue;
        if (XShortcut_id((const XShortcut*)child) == id)
            return (XShortcut*)child;
    }
    return NULL;
}

/** @brief 合并修饰位（读起来比长名字拼接清楚）。 */
static XKeyboardModifiers ws_mods(XKeyboardModifiers a, XKeyboardModifiers b)
{
    return (XKeyboardModifiers)((unsigned int)a | (unsigned int)b);
}

/* ==================== ① grab 返回非零 id 并登记进快捷键表 ==================== */

/**
 * @brief grabShortcut 返回非零 id、两条登记 id 互异、序列可被按键表命中；
 *        releaseShortcut 后命中消失；控件析构连带注销未释放的登记件。
 * @details 对齐 qwidget.cpp:11896-11903（`return shortcutMap.addShortcut(
 *          this, key, context, ...)`）与 qshortcutmap.cpp:123-140
 *          （id = `--(d->currentId)`，故 0 保留给失败）。
 */
XQ_TEST(XWidget, grabShortcutRegisterAndId)
{
    XWidget* top = ws_createTop();
    XKeyboardModifiers mods =
        ws_mods(ws_mods(XKeyboardModifier_ControlModifier,
                        XKeyboardModifier_AltModifier),
                XKeyboardModifier_ShiftModifier);
    XKeySequence first = XKeySequence_fromKey(mods, XKey_F10);
    XKeySequence second = XKeySequence_fromKey(mods, XKey_F11);
    int firstId;
    int secondId;
    int orphanId;

    XQ_REQUIRE(top != NULL);
    firstId = XWidget_grabShortcut(top, &first,
                                   XShortcutContext_ApplicationShortcut);
    secondId = XWidget_grabShortcut(top, &second,
                                    XShortcutContext_ApplicationShortcut);
    XQ_CHECK_MSG(firstId != 0,
                 "grabShortcut 成功必须返回非零 id（Qt: --currentId）");
    XQ_CHECK_MSG(secondId != 0, "第二条 grabShortcut 也必须返回非零 id");
    XQ_CHECK_MSG(firstId != secondId, "两条快捷键的 id 必须互不相同");

    /* 登记表就是既有 XShortcut 全局注册表：应用级上下文不依赖焦点。 */
    XQ_CHECK_PTR(XShortcut_match_2(XKey_F10, mods,
                                   XShortcutContext_ApplicationShortcut,
                                   NULL));
    XQ_CHECK_PTR(XShortcut_match_2(XKey_F11, mods,
                                   XShortcutContext_ApplicationShortcut,
                                   NULL));
    /* 未登记的键不命中。 */
    XQ_CHECK(XShortcut_match_2(XKey_F12, mods,
                               XShortcutContext_ApplicationShortcut,
                               NULL) == NULL);

    /* releaseShortcut：序列不再命中。未知 id 忽略、不误删别的登记件。 */
    XWidget_releaseShortcut(top, firstId);
    XQ_CHECK(XShortcut_match_2(XKey_F10, mods,
                               XShortcutContext_ApplicationShortcut,
                               NULL) == NULL);
    XQ_CHECK_PTR(XShortcut_match_2(XKey_F11, mods,
                                   XShortcutContext_ApplicationShortcut,
                                   NULL));
    XWidget_releaseShortcut(top, firstId);      /* 重复释放：幂等 */
    /* 未知 id：忽略。注意 id 是**负向**单调递减的（--currentId），
     * 故用远离已分配区间的负值，绝不能写成 firstId-1（那正是第二条）。 */
    XWidget_releaseShortcut(top, firstId - 1000000);
    XQ_CHECK_PTR(XShortcut_match_2(XKey_F11, mods,
                                   XShortcutContext_ApplicationShortcut,
                                   NULL));
    XWidget_releaseShortcut(top, secondId);
    XQ_CHECK(XShortcut_match_2(XKey_F11, mods,
                               XShortcutContext_ApplicationShortcut,
                               NULL) == NULL);

    /* 未显式释放的登记件随控件（对象树）析构自动注销
     * （对标 qwidget.cpp:11920-11925 告警里 Qt 自动移除的口径）。 */
    orphanId = XWidget_grabShortcut(top, &first,
                                    XShortcutContext_ApplicationShortcut);
    XQ_CHECK(orphanId != 0);
    XClassDelete((XClass*)top);
    XQ_CHECK(XShortcut_match_2(XKey_F10, mods,
                               XShortcutContext_ApplicationShortcut,
                               NULL) == NULL);
}

/* ==================== ② 命中序列 ⇒ 向所属控件投递快捷键事件 ==================== */

/**
 * @brief 按键命中后控件经事件过滤器收到一条 XEVENT_TYPE_SHORTCUT 事件，
 *        载荷 = 命中序列首组组合；键码/修饰位不同的按键不触发。
 * @details 对齐 qshortcutmap.cpp:560-607 的 dispatchEvent（向 owner 发
 *          QShortcutEvent）；本库按 XWidget_grabShortcut 头注释登记的
 *          差异以 XKeyEvent(type=SHORTCUT) 承载同一投递点。
 */
XQ_TEST(XWidget, grabbedShortcutDeliversEvent)
{
    XWidget* top = ws_createTop();
    XWidgetShortcutProbe* probe;
    XKeyboardModifiers mods = ws_mods(XKeyboardModifier_ControlModifier,
                                      XKeyboardModifier_ShiftModifier);
    XKeySequence sequence = XKeySequence_fromKey(mods, XKey_F11);
    int id;
    bool consumed;

    XQ_REQUIRE(top != NULL);
    XQ_REQUIRE(XWidget_nativeWindow(top) != NULL);
    probe = XWidgetShortcutProbe_create();
    XQ_REQUIRE(probe != NULL);
    XObject_installEventFilter((XObject*)top, (XObject*)probe);

    id = XWidget_grabShortcut(top, &sequence,
                              XShortcutContext_ApplicationShortcut);
    XQ_REQUIRE(id != 0);

    /* 命中：快捷键被消费（dispatchKeyEvent 命中即 return true）且投递一次。 */
    consumed = ws_sendKey(top, XKey_F11, mods, false);
    XQ_CHECK_MSG(consumed, "命中快捷键的按键必须被按键派发消费");
    XQ_EQ_I64(probe->m_events, 1);
    XQ_EQ_I64(probe->m_lastKey, (int)XKey_F11);
    XQ_EQ_I64(probe->m_lastMods, (int)mods);

    /* 键码不同：不触发（按键落到普通投递路径）。 */
    ws_sendKey(top, XKey_F12, mods, false);
    XQ_EQ_I64(probe->m_events, 1);

    /* 修饰位不同：不触发（Ctrl+Shift+F11 与 Ctrl+F11 是两条序列）。 */
    ws_sendKey(top, XKey_F11, XKeyboardModifier_ControlModifier, false);
    XQ_EQ_I64(probe->m_events, 1);

    /* 再命中一次：每次都投递。 */
    ws_sendKey(top, XKey_F11, mods, false);
    XQ_EQ_I64(probe->m_events, 2);

    XWidget_releaseShortcut(top, id);
    XObject_removeEventFilter((XObject*)top, (XObject*)probe);
    XClassDelete((XClass*)probe);
    XClassDelete((XClass*)top);
}

/* ==================== ③ release 后不再触发 ==================== */

/**
 * @brief releaseShortcut 之后同一序列不再投递快捷键事件；重复释放与未知
 *        id 均无副作用（qwidget.cpp:11923 `if (id)` + 表内无项的忽略语义）。
 */
XQ_TEST(XWidget, releaseShortcutStopsDelivery)
{
    XWidget* top = ws_createTop();
    XWidgetShortcutProbe* probe;
    XKeyboardModifiers mods = ws_mods(XKeyboardModifier_ControlModifier,
                                      XKeyboardModifier_AltModifier);
    XKeySequence sequence = XKeySequence_fromKey(mods, XKey_F12);
    int id;

    XQ_REQUIRE(top != NULL);
    XQ_REQUIRE(XWidget_nativeWindow(top) != NULL);
    probe = XWidgetShortcutProbe_create();
    XQ_REQUIRE(probe != NULL);
    XObject_installEventFilter((XObject*)top, (XObject*)probe);

    id = XWidget_grabShortcut(top, &sequence,
                              XShortcutContext_ApplicationShortcut);
    XQ_REQUIRE(id != 0);
    ws_sendKey(top, XKey_F12, mods, false);
    XQ_EQ_I64(probe->m_events, 1);

    XWidget_releaseShortcut(top, id);
    ws_sendKey(top, XKey_F12, mods, false);
    XQ_EQ_I64(probe->m_events, 1); /* 已释放：不再触发 */

    /* 重复释放 / 未知 id：无操作、不崩，且不影响其它状态。 */
    XWidget_releaseShortcut(top, id);
    XWidget_releaseShortcut(top, id - 1000000);
    ws_sendKey(top, XKey_F12, mods, false);
    XQ_EQ_I64(probe->m_events, 1);

    XObject_removeEventFilter((XObject*)top, (XObject*)probe);
    XClassDelete((XClass*)probe);
    XClassDelete((XClass*)top);
}

/* ==================== ④ setShortcutEnabled 开关 ==================== */

/**
 * @brief setShortcutEnabled(false) 后不触发、true 恢复；
 *        对齐 qshortcutmap.cpp:194-222（只改 entry.enabled）与
 *        xshortcut_accepts 的 enabled 闸。
 */
XQ_TEST(XWidget, shortcutEnabledToggle)
{
    XWidget* top = ws_createTop();
    XWidgetShortcutProbe* probe;
    XKeyboardModifiers mods = ws_mods(XKeyboardModifier_ShiftModifier,
                                      XKeyboardModifier_AltModifier);
    XKeySequence sequence = XKeySequence_fromKey(mods, XKey_F10);
    int id;

    XQ_REQUIRE(top != NULL);
    XQ_REQUIRE(XWidget_nativeWindow(top) != NULL);
    probe = XWidgetShortcutProbe_create();
    XQ_REQUIRE(probe != NULL);
    XObject_installEventFilter((XObject*)top, (XObject*)probe);

    id = XWidget_grabShortcut(top, &sequence,
                              XShortcutContext_ApplicationShortcut);
    XQ_REQUIRE(id != 0);
    ws_sendKey(top, XKey_F10, mods, false);
    XQ_EQ_I64(probe->m_events, 1);

    XWidget_setShortcutEnabled(top, id, false);
    ws_sendKey(top, XKey_F10, mods, false);
    XQ_EQ_I64(probe->m_events, 1); /* 禁用：不触发 */
    XQ_CHECK(XShortcut_match_2(XKey_F10, mods,
                               XShortcutContext_ApplicationShortcut,
                               NULL) == NULL);

    XWidget_setShortcutEnabled(top, id, true);
    ws_sendKey(top, XKey_F10, mods, false);
    XQ_EQ_I64(probe->m_events, 2); /* 恢复 */
    XQ_CHECK_PTR(XShortcut_match_2(XKey_F10, mods,
                                   XShortcutContext_ApplicationShortcut,
                                   NULL));

    /* id 0 / 未知 id：忽略（qwidget.cpp:11942 `if (id)`）。 */
    XWidget_setShortcutEnabled(top, 0, false);
    XWidget_setShortcutEnabled(top, id - 1000000, false);
    ws_sendKey(top, XKey_F10, mods, false);
    XQ_EQ_I64(probe->m_events, 3);

    XWidget_releaseShortcut(top, id);
    XObject_removeEventFilter((XObject*)top, (XObject*)probe);
    XClassDelete((XClass*)probe);
    XClassDelete((XClass*)top);
}

/* ==================== ⑤ setShortcutAutoRepeat 开关 ==================== */

/**
 * @brief autoRepeat 缺省 true：自动重复键照常触发；关闭后自动重复键不
 *        触发、首次按下照常触发；再打开恢复。
 * @details 对齐 qshortcutmap.cpp:588-591
 *          `if (!next || (e->isAutoRepeat() && !next->autorepeat)) return;`
 *          的消费点在 XWidget_dispatchKeyEvent。
 */
XQ_TEST(XWidget, shortcutAutoRepeatToggle)
{
    XWidget* top = ws_createTop();
    XWidgetShortcutProbe* probe;
    XKeyboardModifiers mods = ws_mods(XKeyboardModifier_AltModifier,
                                      XKeyboardModifier_ShiftModifier);
    XKeySequence sequence = XKeySequence_fromKey(mods, XKey_F11);
    int id;

    XQ_REQUIRE(top != NULL);
    XQ_REQUIRE(XWidget_nativeWindow(top) != NULL);
    probe = XWidgetShortcutProbe_create();
    XQ_REQUIRE(probe != NULL);
    XObject_installEventFilter((XObject*)top, (XObject*)probe);

    id = XWidget_grabShortcut(top, &sequence,
                              XShortcutContext_ApplicationShortcut);
    XQ_REQUIRE(id != 0);

    /* 缺省 autoRepeat = true（Qt QShortcutEntry 的 true 初值）：重复键触发。 */
    ws_sendKey(top, XKey_F11, mods, true);
    XQ_EQ_I64(probe->m_events, 1);

    /* 关闭：重复键不触发，首次按下照常触发。 */
    XWidget_setShortcutAutoRepeat(top, id, false);
    ws_sendKey(top, XKey_F11, mods, true);
    XQ_EQ_I64(probe->m_events, 1);
    ws_sendKey(top, XKey_F11, mods, false);
    XQ_EQ_I64(probe->m_events, 2);

    /* 再打开：重复键恢复触发。 */
    XWidget_setShortcutAutoRepeat(top, id, true);
    ws_sendKey(top, XKey_F11, mods, true);
    XQ_EQ_I64(probe->m_events, 3);

    XWidget_releaseShortcut(top, id);
    XObject_removeEventFilter((XObject*)top, (XObject*)probe);
    XClassDelete((XClass*)probe);
    XClassDelete((XClass*)top);
}

/* ==================== ⑥ NULL / 空序列 / 越界上下文安全 ==================== */

/**
 * @brief NULL 控件、NULL 序列、空序列均返回 0；越界 context 按缺省
 *        WindowShortcut 夹取；四个入口对 NULL/未知 id 一律无操作。
 */
XQ_TEST(XWidget, grabShortcutNullAndEmpty)
{
    XWidget* top = ws_createTop();
    XKeySequence sequence = XKeySequence_fromKey(
        ws_mods(XKeyboardModifier_ControlModifier,
                XKeyboardModifier_ShiftModifier), XKey_F10);
    XKeySequence empty = XKeySequence_empty();
    int id;

    XQ_REQUIRE(top != NULL);
    /* Qt: `if (key.isEmpty()) return 0;`（qwidget.cpp:11899-11900）。 */
    XQ_EQ_I64(XWidget_grabShortcut(NULL, &sequence, 0), 0);
    XQ_EQ_I64(XWidget_grabShortcut(top, NULL, 0), 0);
    XQ_EQ_I64(XWidget_grabShortcut(top, &empty, 0), 0);
    XQ_EQ_I64(XWidget_grabShortcut(top, &empty,
                                   XShortcutContext_ApplicationShortcut), 0);

    /* 越界 context：按缺省 WindowShortcut 登记（Qt 缺省形参同值）。 */
    id = XWidget_grabShortcut(top, &sequence, 99);
    XQ_CHECK(id != 0);
    {
        XShortcut* shortcut = ws_findShortcutById(top, id);
        XQ_CHECK_PTR(shortcut);
        if (shortcut)
            XQ_EQ_I64((int)XShortcut_context(shortcut),
                      (int)XShortcutContext_WindowShortcut);
    }
    XWidget_releaseShortcut(top, id);

    /* 四入口的 NULL / 0 / 未知 id：无操作、不崩。 */
    XWidget_releaseShortcut(NULL, 123);
    XWidget_releaseShortcut(top, 0);
    XWidget_releaseShortcut(top, -987654);
    XWidget_setShortcutEnabled(NULL, 123, false);
    XWidget_setShortcutEnabled(top, 0, false);
    XWidget_setShortcutEnabled(top, -987654, false);
    XWidget_setShortcutAutoRepeat(NULL, 123, false);
    XWidget_setShortcutAutoRepeat(top, 0, false);
    XWidget_setShortcutAutoRepeat(top, -987654, false);

    XClassDelete((XClass*)top);
}

/* ==================== ⑦ 同键多命中（歧义）派发口径：本库集合广播 ============ */

/**
 * @brief 两个控件各 grab 同一序列 ⇒ 每次按键**两边各收一条**快捷键事件。
 * @details 锁定的是**本库口径**而非 Qt 口径：Qt 6.8.3 的
 *          QShortcutMap::dispatchEvent 每次按键只向 next->owner 发**一条**
 *          QShortcutEvent，并按 ambigCount 在同类登记件间**轮转**
 *          （qshortcutmap.cpp:560-607、575-587、605-606；本机 Qt 6.8.0
 *          offscreen 探针实测两控件同键时连续四次按下为 w1/w2/w1/w2 各
 *          一次、ambiguous=1）。本库 XShortcut_activate_2 在歧义时对
 *          registry 内每个命中登记件发 activatedAmbiguously
 *          （Src/XGui/Widget/XShortcut.c:695-723，歧义分支 709-721）⇒
 *          XWidget 的激活槽每次按键给**每个所属控件各投一条**。该差异
 *          已写进 XWidget.h 的 XWidget_grabShortcut 注释；本用例锁住
 *          现实现，防止将来无声漂移（若改为 Qt 轮转单发，本用例须按新
 *          口径重写而不是放宽断言）。
 */
XQ_TEST(XWidget, grabbedShortcutAmbiguityBroadcast)
{
    XWidget* first = ws_createTop();
    XWidget* second = ws_createTop();
    XWidgetShortcutProbe* firstProbe;
    XWidgetShortcutProbe* secondProbe;
    XKeyboardModifiers mods = ws_mods(
        ws_mods(XKeyboardModifier_ControlModifier,
                XKeyboardModifier_AltModifier),
        XKeyboardModifier_ShiftModifier);
    XKeySequence sequence = XKeySequence_fromKey(mods, XKey_F9);
    int firstId;
    int secondId;

    XQ_REQUIRE(first != NULL);
    XQ_REQUIRE(second != NULL);
    XQ_REQUIRE(XWidget_nativeWindow(first) != NULL);
    firstProbe = XWidgetShortcutProbe_create();
    secondProbe = XWidgetShortcutProbe_create();
    XQ_REQUIRE(firstProbe != NULL);
    XQ_REQUIRE(secondProbe != NULL);
    XObject_installEventFilter((XObject*)first, (XObject*)firstProbe);
    XObject_installEventFilter((XObject*)second, (XObject*)secondProbe);

    firstId = XWidget_grabShortcut(first, &sequence,
                                   XShortcutContext_ApplicationShortcut);
    secondId = XWidget_grabShortcut(second, &sequence,
                                    XShortcutContext_ApplicationShortcut);
    XQ_REQUIRE(firstId != 0);
    XQ_REQUIRE(secondId != 0);
    XQ_CHECK(firstId != secondId);

    /* 每次按下 ⇒ 两个 owner 各一条（集合广播；Qt 是轮转单发）。 */
    ws_sendKey(first, XKey_F9, mods, false);
    XQ_EQ_I64(firstProbe->m_events, 1);
    XQ_EQ_I64(secondProbe->m_events, 1);
    ws_sendKey(first, XKey_F9, mods, false);
    XQ_EQ_I64(firstProbe->m_events, 2);
    XQ_EQ_I64(secondProbe->m_events, 2);

    /* 释放一边 ⇒ 歧义消解，剩下的一边照常每按一次一条。 */
    XWidget_releaseShortcut(second, secondId);
    ws_sendKey(first, XKey_F9, mods, false);
    XQ_EQ_I64(firstProbe->m_events, 3);
    XQ_EQ_I64(secondProbe->m_events, 2);

    XWidget_releaseShortcut(first, firstId);
    XObject_removeEventFilter((XObject*)first, (XObject*)firstProbe);
    XObject_removeEventFilter((XObject*)second, (XObject*)secondProbe);
    XClassDelete((XClass*)firstProbe);
    XClassDelete((XClass*)secondProbe);
    XClassDelete((XClass*)first);
    XClassDelete((XClass*)second);
}

#endif /* XGUI_ON && XWIDGET_ON */
