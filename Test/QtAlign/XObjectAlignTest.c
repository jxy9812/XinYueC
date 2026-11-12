/**
 * @file       XObjectAlignTest.c
 * @brief      XObject 与 Qt 6.8 QObject 对齐契约测试。
 * @details    覆盖本批次对齐/根修的行为契约，逐条对应 Qt 6.8 实测结论
 *             （探针源码见提交说明，Qt 头文件
 *             /usr/include/x86_64-linux-gnu/qt6/QtCore/qobject.h）：
 *             - 对象树：setParent / `new QObject(parent)` / 父析构带走子对象 /
 *               析构顺序（Qt 实测：父先发 destroyed，再按创建顺序删子）；
 *             - deleteLater：最小事件循环里兑现、重复调用只删一次；
 *             - blockSignals：阻塞期间槽不触发、destroyed 例外、返回调用前状态；
 *             - 事件派发：User 以上事件交 customEvent、Child 系列交 childEvent；
 *             - 事件过滤器：安装/命中/返回 true 吞事件/反注册/过滤器先亡；
 *             - 定时器：startTimer(0) 合法、启停生效、跨线程之外的常规路径；
 *             - 动态属性：替换、传 NULL 即删除、名称列表拷贝与借用；
 *             - objectNameChanged：同值不发、改为空串要发；
 *             - findChild/findChildren：浅层优先、递归、空名匹配首个子对象；
 *             - connect/disconnect：一次断开同一 (receiver,slot) 的全部重复连接、
 *               三个过滤条件可为空的批量断开；
 *             - sender/senderSignalIndex：槽内有值、槽外 -1；
 *             - 不可拷贝：XClassCopy 不改动目标对象（对标 Q_DISABLE_COPY）。
 *
 *             泄漏由运行器自动判定：用例只创建、不手工删子对象（父析构带走），
 *             名称列表拷贝等"调用方拥有"的对象必须显式释放，否则判 `[LEAK]`。
 * @note       C99；断言失败不中断用例（XQ_CHECK），前置条件用 XQ_REQUIRE。
 * @author     XinYueC 团队
 */
#include "xq_align.h"

#include "XObject.h"
#include "XString.h"
#include "XVector.h"
#include "XVariant.h"
#include "XEvent.h"
#include "XEventLoop.h"
#include "XCoreApplication.h"
#include "XMemory.h"
#include "XThreadData.h"
#include "XAbstractEventDispatcher.h"   /* timersForObject / 定时器迁移断言 */
#include "XFileDescriptor.h"            /* XFd 槽位状态（id 未归还池子） */
#include "XRegularExpression.h"

/* ==================== 信号/槽探针 ==================== */

/** @brief 测试用信号（首个参数为对象指针，NULL 时返回自身地址作为信号标识）。 */
static void* xqAlignProbeSignal(XObject* self)
{
    XEmitSignal(self, xqAlignProbeSignal, NULL, NULL, NULL, XEVENT_PRIORITY_NORMAL);
}

static int g_slotHits;                  /**< 普通槽命中次数 */
static XObject* g_lastSender;           /**< 槽内看到的发送者 */
static int g_lastSignalIndex;           /**< 槽内看到的信号索引 */
static int g_destroyCount;              /**< destroyed 槽命中次数 */
static XObject* g_destroyOrder[8];      /**< destroyed 顺序快照 */
static int g_nameHits;                  /**< objectNameChanged 命中次数 */
static XEventLoop* g_destroyQuitLoop;   /**< 非 NULL：destroyed 命中后退出该循环 */

/** @brief 计数槽：记录命中、发送者与信号索引。 */
static void xqSlot_count(XObject* receiver, XVarList* args)
{
    (void)args;
    ++g_slotHits;
    g_lastSender = XObject_sender(receiver);
    g_lastSignalIndex = XObject_senderSignalIndex(receiver);
}

/** @brief destroyed 槽：记录被销毁对象（sender）与顺序。 */
static void xqSlot_destroyed(XObject* receiver, XVarList* args)
{
    (void)args;
    ++g_destroyCount;
    if (g_destroyCount <= (int)(sizeof(g_destroyOrder) / sizeof(g_destroyOrder[0])))
        g_destroyOrder[g_destroyCount - 1] = XObject_sender(receiver);
    /* deleteLater 用例：销毁一发生就退出循环，不必等兜底定时器。 */
    if (g_destroyQuitLoop)
        XEventLoop_exit(g_destroyQuitLoop, 0);
}

/** @brief objectNameChanged 槽。 */
static void xqSlot_nameChanged(XObject* receiver, XVarList* args)
{
    (void)receiver;
    (void)args;
    ++g_nameHits;
}

/** @brief 重置本文件用到的全部计数器。 */
static void xqResetCounters(void)
{
    int i;
    g_slotHits = 0;
    g_lastSender = NULL;
    g_lastSignalIndex = 0;
    g_destroyCount = 0;
    g_nameHits = 0;
    g_destroyQuitLoop = NULL;
    for (i = 0; i < (int)(sizeof(g_destroyOrder) / sizeof(g_destroyOrder[0])); ++i)
        g_destroyOrder[i] = NULL;
}

/* ==================== 可观察子类（虚函数槽位探针） ==================== */

XCLASS_DEFINE_BEGING(XQAlignProbe)
XCLASS_DEFINE_EXTEND_END(XQAlignProbe, XObject);

typedef struct XQAlignProbe
{
    XObject m_base;          /**< 必须为首成员（多态布局） */
    int m_customEvents;      /**< customEvent 命中次数 */
    int m_childEvents;       /**< childEvent 命中次数 */
    int m_timerEvents;       /**< **被跟踪定时器**的 timerEvent 命中次数 */
    int m_anyTimerHits;      /**< 任意定时器命中次数（用于退出循环） */
    int m_quitAfterHits;     /**< 任意定时器命中达到该次数后退出 m_loop */
    int m_filterCalls;       /**< eventFilter 命中次数 */
    bool m_filterBlock;      /**< true 时过滤器吞掉事件（返回 true） */
    XTimerId m_trackedId;    /**< 要单独统计的定时器 ID */
    XEventLoop* m_loop;      /**< 非 NULL：达到命中次数后退出该循环 */
    int m_connectNotifies;   /**< connectNotify 命中次数（应命中**发送者**） */
    int m_disconnectNotifies;/**< disconnectNotify 命中次数（应命中**发送者**） */
    int m_dynPropEvents;     /**< DynamicPropertyChange 事件命中次数 */
    int m_parentAboutToChange; /**< ParentAboutToChange 事件命中次数（setParent 事件序） */
    int m_parentChange;        /**< ParentChange 事件命中次数（setParent 事件序） */
    char m_lastDynProp[64];  /**< 最近一次变更事件里的属性名（拷贝） */
    XObject* m_deleteLaterOnHit; /**< 非 NULL：定时器命中时对它 deleteLater
                                      （用于在**事件循环内**制造延迟删除） */
} XQAlignProbe;

static void VXQAlignProbe_customEvent(XObject* self, XEvent* event)
{
    ++((XQAlignProbe*)self)->m_customEvents;
    XEvent_accept(event);
}

static void VXQAlignProbe_childEvent(XObject* self, XChildEvent* event)
{
    ++((XQAlignProbe*)self)->m_childEvents;
    XEvent_accept((XEvent*)event);
}

static void VXQAlignProbe_timerEvent(XObject* self, XTimerEvent* event)
{
    XQAlignProbe* probe = (XQAlignProbe*)self;
    ++probe->m_anyTimerHits;
    if (probe->m_trackedId != XTIMER_INVALID_ID
        && XTimerEvent_timerId(event) == probe->m_trackedId)
        ++probe->m_timerEvents;
    XEvent_accept((XEvent*)event);
    /* 在事件循环**内部**（loopLevel=1）发起延迟删除：用于锁定
     * 「普通事件循环退出不冲刷、exec cleanup 拍才冲刷」的 Qt 口径。 */
    if (probe->m_deleteLaterOnHit) {
        XObject* target = probe->m_deleteLaterOnHit;
        probe->m_deleteLaterOnHit = NULL;
        XObject_deleteLater(target);
    }
    if (probe->m_loop && probe->m_quitAfterHits > 0
        && probe->m_anyTimerHits >= probe->m_quitAfterHits)
        XEventLoop_exit(probe->m_loop, 0);
}

static bool VXQAlignProbe_eventFilter(XObject* self, XObject* watched, XEvent* event)
{
    XQAlignProbe* probe = (XQAlignProbe*)self;
    (void)watched;
    (void)event;
    ++probe->m_filterCalls;
    return probe->m_filterBlock;
}

/** @brief connectNotify：Qt 6.8 口径下命中**发送者**（信号所属对象）1 次。 */
static void VXQAlignProbe_connectNotify(XObject* self, size_t signal)
{
    (void)signal;
    ++((XQAlignProbe*)self)->m_connectNotifies;
}

/** @brief disconnectNotify：同 connectNotify，命中发送者。 */
static void VXQAlignProbe_disconnectNotify(XObject* self, size_t signal)
{
    (void)signal;
    ++((XQAlignProbe*)self)->m_disconnectNotifies;
}

/** @brief event：统计 DynamicPropertyChange / Parent*，其余交给 XObject 默认实现。 */
static bool VXQAlignProbe_event(XObject* self, XEvent* e)
{
    XQAlignProbe* probe = (XQAlignProbe*)self;
    if (e && e->m_type == XEVENT_TYPE_DYNAMIC_PROPERTY_CHANGE) {
        const char* name = XDynamicPropertyChangeEvent_propertyName(e);
        size_t i = 0;
        ++probe->m_dynPropEvents;
        if (name) {
            for (; name[i] && i + 1 < sizeof(probe->m_lastDynProp); ++i)
                probe->m_lastDynProp[i] = name[i];
        }
        probe->m_lastDynProp[i] = '\0';
        return true;
    }
    if (e && e->m_type == XEVENT_TYPE_PARENT_ABOUT_TO_CHANGE)
        ++probe->m_parentAboutToChange;
    if (e && e->m_type == XEVENT_TYPE_PARENT_CHANGE)
        ++probe->m_parentChange;
    return XClass_Parent(XObject, EXObject_Event,
                         bool(*)(XObject*, XEvent*))(self, e);
}

static XVtable* XQAlignProbe_class_init(void)
{
    XVTABLE_INIT_DEFAULT(XQAlignProbe)
    XVTABLE_INHERIT_XCLASS(XObject);
    XVTABLE_OVERLOAD_DEFAULT(EXObject_Event, VXQAlignProbe_event);
    XVTABLE_OVERLOAD_DEFAULT(EXObject_CustomEvent, VXQAlignProbe_customEvent);
    XVTABLE_OVERLOAD_DEFAULT(EXObject_ChildEvent, VXQAlignProbe_childEvent);
    XVTABLE_OVERLOAD_DEFAULT(EXObject_TimerEvent, VXQAlignProbe_timerEvent);
    XVTABLE_OVERLOAD_DEFAULT(EXObject_EventFilter, VXQAlignProbe_eventFilter);
    XVTABLE_OVERLOAD_DEFAULT(EXObject_ConnectNotify, VXQAlignProbe_connectNotify);
    XVTABLE_OVERLOAD_DEFAULT(EXObject_DisconnectNotify, VXQAlignProbe_disconnectNotify);
    return XVTABLE_DEFAULT;
}

static XQAlignProbe* XQAlignProbe_create(void)
{
    XQAlignProbe* probe = (XQAlignProbe*)XMemory_malloc(sizeof(XQAlignProbe),
                                                       XCLASS_DEFAULT_MEMORY_TYPE);
    if (!probe)
        return NULL;
    XObject_init(&probe->m_base);
    XClassGetVtable(probe) = XQAlignProbe_class_init();
    /* 对齐 Qt 的子类 opt-in（qobject.h:87 receiveParentEvents 是
       protected 位，Qt 缺省 false 且 qtbase 内零写点）：探针要锁定
       setParent 的 ParentAboutToChange/ParentChange 事件序，须自行置位
       ——框架侧按 XWidget.c:7703 定裁不代写该标志（非控件对象不受影响）。 */
    probe->m_base.m_receive_parent_events = true;
    probe->m_customEvents = 0;
    probe->m_childEvents = 0;
    probe->m_timerEvents = 0;
    probe->m_anyTimerHits = 0;
    probe->m_quitAfterHits = 0;
    probe->m_filterCalls = 0;
    probe->m_filterBlock = false;
    probe->m_trackedId = XTIMER_INVALID_ID;
    probe->m_loop = NULL;
    probe->m_connectNotifies = 0;
    probe->m_disconnectNotifies = 0;
    probe->m_dynPropEvents = 0;
    probe->m_parentAboutToChange = 0;
    probe->m_parentChange = 0;
    probe->m_lastDynProp[0] = '\0';
    probe->m_deleteLaterOnHit = NULL;
    Set_Class_Memory(probe, XCLASS_DEFAULT_MEMORY_TYPE);
    Set_Class_IsHeap(probe, true);
    return probe;
}

/* ==================== 用例 ==================== */

/** @brief 构造/析构基本所有权：create 成功且 delete 后无残留。 */
XQ_TEST(XObject, createDestroy)
{
    XObject* obj = XObject_create();
    XQ_CHECK_PTR(obj);
    if (obj) {
        XQ_CHECK(XObject_parent(obj) == NULL);
        /* 创建即带线程归属（对标 Qt 构造期 setThreadData_helper(currentThreadData)） */
        XQ_CHECK(XObject_threadData(obj) != NULL);
        XQ_CHECK(!XObject_signalsBlocked(obj));
        XClassDelete((XClass*)obj);
    }
}

/** @brief 对标 `new QObject(parent)`：带父创建即挂到父对象下。 */
XQ_TEST(XObject, createWithParent)
{
    XObject* parent = XObject_create();
    XQ_REQUIRE(parent != NULL);

    /* 等价 Qt 的 QObject(QObject *parent = nullptr) */
    {
        XObject* child = XObject_createWithParent(parent);
        const XVector* children = NULL;
        XQ_REQUIRE(child != NULL);
        XQ_CHECK(XObject_parent(child) == parent);
        children = XObject_children(parent);
        XQ_CHECK_PTR(children);
        if (children)
            XQ_EQ_SIZE(XContainer_size_base((const XContainer*)children), 1);
        /* 子对象随父析构释放（故意不删 child）。 */
    }
    XClassDelete((XClass*)parent);
}

/** @brief 父子关系 + 父析构带走子对象 + Qt 实测的析构顺序。 */
XQ_TEST(XObject, parentChildDelete)
{
    XObject* parent = XObject_create();
    XObject* observer = XObject_create();
    XObject* c1 = XObject_create();
    XObject* c2 = NULL;
    const XVector* children = NULL;

    xqResetCounters();
    XQ_REQUIRE(parent != NULL && observer != NULL && c1 != NULL);

    XObject_setParent(c1, parent);
    c2 = XObject_createWithParent(parent);  /* 子表顺序 = c1, c2 */
    XQ_REQUIRE(c2 != NULL);

    XQ_CHECK(XObject_parent(c1) == parent);
    XQ_CHECK(XObject_parent(c2) == parent);
    children = XObject_children(parent);
    XQ_CHECK_PTR(children);
    if (children) XQ_EQ_SIZE(XContainer_size_base((const XContainer*)children), 2);

    /* Qt 实测：destroyed(parent) 先于 destroyed(children)，子对象按创建顺序 */
    XQ_CHECK(XObject_connect(parent, XSignal(XObject_destroyed_signal), observer,
                               xqSlot_destroyed, XConnectionType_Auto) != NULL);
    XQ_CHECK(XObject_connect(c1, XSignal(XObject_destroyed_signal), observer,
                               xqSlot_destroyed, XConnectionType_Auto) != NULL);
    XQ_CHECK(XObject_connect(c2, XSignal(XObject_destroyed_signal), observer,
                               xqSlot_destroyed, XConnectionType_Auto) != NULL);

    /* Qt 语义：父对象析构自动删除子对象（此处故意不删 c1/c2）。 */
    XClassDelete((XClass*)parent);

    XQ_EQ_I64(g_destroyCount, 3);
    XQ_CHECK(g_destroyOrder[0] == parent);
    XQ_CHECK(g_destroyOrder[1] == c1);
    XQ_CHECK(g_destroyOrder[2] == c2);

    XClassDelete((XClass*)observer);
}

/** @brief reparent：从旧父移除并挂到新父，旧父 children 计数回落。 */
XQ_TEST(XObject, reparent)
{
    XObject* a = XObject_create();
    XObject* b = XObject_create();
    XObject* child = XObject_create();
    XQ_REQUIRE(a != NULL && b != NULL && child != NULL);

    XObject_setParent(child, a);
    XObject_setParent(child, b);
    XQ_CHECK(XObject_parent(child) == b);
    {
        const XVector* ca = XObject_children(a);
        const XVector* cb = XObject_children(b);
        if (ca) XQ_EQ_SIZE(XContainer_size_base((const XContainer*)ca), 0);
        if (cb) XQ_EQ_SIZE(XContainer_size_base((const XContainer*)cb), 1);
    }

    /* 自挂：Qt 6.8 实测**没有**环守卫（qobject.cpp 的 setParent_helper 只查
     * 同父/跨线程）——setParent(self) 后 parent()==self、children()==1，
     * 析构时 double free（探针进程 exit 134）。本框架的祖先链守卫
     * （XObject.c:408-413）是**故意与 Qt 分叉的安全扩展**：拒绝后 parent()
     * 保持 NULL（否则析构必崩，等于把 Qt 的坑照搬进来）。 */
    XObject_setParent(a, a);
    XQ_CHECK(XObject_parent(a) == NULL);
    /* 真后代环：把 child 重新挂回 a，再把 a 挂到 child 下 ⇒ 祖先链能查到 a
     * ⇒ 拒绝（Qt 探针同场景 parent()==child、两侧 children 各 1，析构即崩；
     * 这里宁愿拒绝也不造环）。 */
    XObject_setParent(child, a);
    XObject_setParent(a, child);
    XQ_CHECK(XObject_parent(a) == NULL);

    XClassDelete((XClass*)a); /* child 是 a 的子对象，随 a 释放 */
    XClassDelete((XClass*)b);
}

/** @brief objectName：UTF-8 读回与设置值一致。 */
XQ_TEST(XObject, objectName)
{
    XObject* obj = XObject_create();
    XString* name = XString_create_utf8("align-probe");
    XQ_REQUIRE(obj != NULL && name != NULL);

    XQ_CHECK(XObject_objectName(obj) == NULL);   /* 未设置过 → 无名字 */
    XObject_setObjectName(obj, name);
    {
        const XString* got = XObject_objectName(obj);
        XQ_CHECK_PTR(got);
        if (got) XQ_EQ_STR(XString_toUtf8(got), "align-probe");
    }

    XClassDelete((XClass*)name);
    XClassDelete((XClass*)obj);
}

/** @brief objectNameChanged：同值不发；非空 → 空要发（Qt 实测 2 次）。 */
XQ_TEST(XObject, objectNameChangedSignal)
{
    XObject* obj = XObject_create();
    XObject* observer = XObject_create();
    XString* n1 = XString_create_utf8("name-1");
    XString* empty = XString_create_utf8("");
    XQ_REQUIRE(obj != NULL && observer != NULL && n1 != NULL && empty != NULL);

    xqResetCounters();
    XQ_CHECK(XObject_connect(obj, XSignal(XObject_objectNameChanged_signal),
                               observer, xqSlot_nameChanged,
                               XConnectionType_Auto) != NULL);

    XObject_setObjectName(obj, n1);
    XQ_EQ_I64(g_nameHits, 1);
    XObject_setObjectName(obj, n1);          /* 同值：不发射 */
    XQ_EQ_I64(g_nameHits, 1);
    XObject_setObjectName(obj, empty);       /* 非空 → 空：发射（Qt 实测） */
    XQ_EQ_I64(g_nameHits, 2);
    XObject_setObjectName(obj, empty);       /* 同值：不发射 */
    XQ_EQ_I64(g_nameHits, 2);
    /* 改成空串后对象名长度归零（XString 区分不了 Qt 的 null/empty，
     * 见 gaps；XString_toUtf8 对空串返回 NULL，故用长度判定） */
    if (XObject_objectName(obj))
        XQ_EQ_SIZE(XString_length_base(XObject_objectName(obj)), 0);

    XClassDelete((XClass*)obj);
    XClassDelete((XClass*)observer);
    XClassDelete((XClass*)n1);
    XClassDelete((XClass*)empty);
}

/** @brief deleteLater：最小事件循环里兑现，重复调用只删一次。 */
XQ_TEST(XObject, deleteLaterInLoop)
{
    XEventLoop* loop = NULL;
    XQAlignProbe* driver = NULL;
    XObject* victim = NULL;
    XObject* observer = NULL;
    XTimerId backstop = XTIMER_INVALID_ID;

    xqResetCounters();
    /* 先建循环：它负责给当前线程装上事件调度器，后续对象才能注册定时器。 */
    loop = XEventLoop_create();
    XQ_REQUIRE(loop != NULL);
    driver = XQAlignProbe_create();
    observer = XObject_create();
    victim = XObject_create();
    XQ_REQUIRE(driver != NULL && observer != NULL && victim != NULL);

    driver->m_loop = loop;
    /* 兜底：100ms 后强退循环，deleteLater 未兑现时用例报失败而不是挂死。 */
    driver->m_quitAfterHits = 1;
    backstop = XObject_startTimer((XObject*)driver, 100, XTimerType_CoarseTimer);
    XQ_CHECK(backstop != XTIMER_INVALID_ID);

    XQ_CHECK(XObject_connect(victim, XSignal(XObject_destroyed_signal),
                               observer, xqSlot_destroyed,
                               XConnectionType_Auto) != NULL);
    g_destroyQuitLoop = loop;      /* destroyed 一命中就退出循环 */
    XObject_deleteLater(victim);
    XObject_deleteLater(victim);   /* Qt 实测：重复 deleteLater 只删一次 */

    if (backstop != XTIMER_INVALID_ID) {
        (void)XEventLoop_exec(loop, XEventLoop_AllEvents);
        XQ_EQ_I64(g_destroyCount, 1);
        XQ_CHECK(g_destroyOrder[0] == victim);
    }
    g_destroyQuitLoop = NULL;

    XObject_killTimer((XObject*)driver, backstop);
    XClassDelete((XClass*)driver);
    XClassDelete((XClass*)observer);
    XClassDelete((XClass*)loop);
}

/** @brief blockSignals：阻塞期间不派发、destroyed 例外、返回调用前状态。 */
XQ_TEST(XObject, blockSignals)
{
    XObject* emitter = XObject_create();
    XObject* observer = XObject_create();
    bool prev = false;
    bool prev2 = false;

    xqResetCounters();
    XQ_REQUIRE(emitter != NULL && observer != NULL);
    XQ_CHECK(XObject_connect(emitter, XSignal(xqAlignProbeSignal), observer,
                               xqSlot_count, XConnectionType_Direct) != NULL);

    XQ_CHECK(!XObject_signalsBlocked(emitter));
    xqAlignProbeSignal(emitter);
    XQ_EQ_I64(g_slotHits, 1);

    prev = XObject_blockSignals(emitter, true);
    XQ_CHECK(!prev);                       /* 调用前未被阻塞 */
    XQ_CHECK(XObject_signalsBlocked(emitter));
    xqAlignProbeSignal(emitter);           /* 阻塞：槽不触发（Qt 实测 0 次） */
    XQ_EQ_I64(g_slotHits, 1);

    prev2 = XObject_blockSignals(emitter, true);  /* 布尔位而非嵌套计数 */
    XQ_CHECK(prev2);
    XQ_EQ_I64(g_slotHits, 1);
    /* Qt 6.8 探针（blockSignals 是**布尔位**，不嵌套）逐档实测：
     *   block(true)  → prev=0，signalsBlocked=1
     *   block(true)  → prev=1，signalsBlocked=1
     *   block(false) → prev=1，signalsBlocked=0   ← 一次 unblock 即解开
     *   block(false) → prev=0，signalsBlocked=0
     * 依据 qobject.cpp `d->blockSig = block;`（直接赋值，没有嵌套计数器）。 */
    XQ_CHECK(XObject_blockSignals(emitter, false));  /* 返回调用前状态 true */
    XQ_CHECK(!XObject_signalsBlocked(emitter));      /* 一次 unblock 即解开 */
    XQ_CHECK(!XObject_blockSignals(emitter, false)); /* 已解开：前态 false */
    XQ_CHECK(!XObject_signalsBlocked(emitter));

    xqAlignProbeSignal(emitter);
    XQ_EQ_I64(g_slotHits, 2);

    /* destroyed() 是唯一不受阻塞影响的信号（Qt 文档 + 实测） */
    XQ_CHECK(XObject_connect(emitter, XSignal(XObject_destroyed_signal),
                               observer, xqSlot_destroyed,
                               XConnectionType_Auto) != NULL);
    XObject_blockSignals(emitter, true);
    XClassDelete((XClass*)emitter);
    XQ_EQ_I64(g_destroyCount, 1);

    XClassDelete((XClass*)observer);
}

/** @brief 事件派发：User 以上交 customEvent，Child 系列交 childEvent。 */
XQ_TEST(XObject, eventDispatch)
{
    XQAlignProbe* probe = XQAlignProbe_create();
    XObject* child = XObject_create();
    XEvent custom;
    bool handled = false;

    XQ_REQUIRE(probe != NULL && child != NULL);

    /* Qt 实测：User+7 事件由 QObject::event 的 default 分支交 customEvent，
     * 且 event() 返回 true。修复前 XObject_customEvent_base 全仓 0 处引用。 */
    XEvent_init(&custom, (XEventType)(XEVENT_TYPE_USER + 7));
    handled = XCoreApplication_sendEvent((XObject*)probe, &custom);
    XQ_CHECK(handled);
    XQ_EQ_I64(probe->m_customEvents, 1);

    /* Child 系列：Qt 实测 new QObject(&parent) 触发 1 次 ChildAdded、
     * delete 触发 1 次 ChildRemoved。 */
    XObject_setParent(child, (XObject*)probe);
    XQ_EQ_I64(probe->m_childEvents, 1);
    XObject_setParent(child, NULL);
    XQ_EQ_I64(probe->m_childEvents, 2);

    /* 非 User 的普通事件不交 customEvent（Qt 同口径） */
    XEvent_init(&custom, XEVENT_TYPE_NONE);
    (void)XCoreApplication_sendEvent((XObject*)probe, &custom);
    XQ_EQ_I64(probe->m_customEvents, 1);

    XClassDelete((XClass*)child);
    XClassDelete((XClass*)probe);
}

/** @brief 事件过滤器：安装/命中/吞事件/反注册/过滤器先亡自动反注册。 */
XQ_TEST(XObject, eventFilter)
{
    XQAlignProbe* filter = XQAlignProbe_create();
    XObject* watched = XObject_create();
    XEvent ev;

    XQ_REQUIRE(filter != NULL && watched != NULL);
    XEvent_init(&ev, (XEventType)(XEVENT_TYPE_USER + 3));

    XObject_installEventFilter(watched, (XObject*)filter);
    (void)XCoreApplication_sendEvent(watched, &ev);
    XQ_EQ_I64(filter->m_filterCalls, 1);

    /* 过滤器返回 true ⇒ 事件被吞，sendEvent 视为已处理（对标 Qt 的
     * notify 短路）。事件的 accepted 位不受过滤器影响：Qt 6.8 的 QEvent
     * 缺省 accepted=true（qcoreevent.h:344 `bool m_accept = true;`），
     * 过滤器吞掉事件也不会把它改成 false——只有 `ignore()` 才会。 */
    filter->m_filterBlock = true;
    XQ_CHECK(XCoreApplication_sendEvent(watched, &ev));
    XQ_EQ_I64(filter->m_filterCalls, 2);
    XQ_CHECK(ev.m_accepted);

    XObject_removeEventFilter(watched, (XObject*)filter);
    filter->m_filterBlock = false;
    (void)XCoreApplication_sendEvent(watched, &ev);
    XQ_EQ_I64(filter->m_filterCalls, 2);   /* 反注册后不再命中 */

    /* 过滤器先亡：被观察对象的分发表必须已清空（否则下次派发即 UAF） */
    XObject_installEventFilter(watched, (XObject*)filter);
    XClassDelete((XClass*)filter);
    XQ_CHECK(watched->m_filters == NULL
             || XContainer_size_base((const XContainer*)watched->m_filters) == 0);
    (void)XCoreApplication_sendEvent(watched, &ev);   /* 不得崩溃 */

    XClassDelete((XClass*)watched);
}

/** @brief 定时器：startTimer(0) 合法、定时触发、killTimer 生效。 */
XQ_TEST(XObject, timerStartStop)
{
    XEventLoop* loop = XEventLoop_create();
    XQAlignProbe* probe = XQAlignProbe_create();
    XTimerId zeroId = XTIMER_INVALID_ID;
    XTimerId liveId = XTIMER_INVALID_ID;

    XQ_REQUIRE(loop != NULL && probe != NULL);

    /* Qt 实测 startTimer(0) 返回合法 ID（id=1）——修复前被调度器的
     * `interval == 0` 分支拒绝。 */
    zeroId = XObject_startTimer((XObject*)probe, 0, XTimerType_CoarseTimer);
    XQ_CHECK(zeroId != XTIMER_INVALID_ID);
    XObject_killTimer((XObject*)probe, zeroId);

    /* 停止的定时器不得触发。
     * 定时器 ID **会复用**：Qt 6.8 探针连续 stop/start 每次都拿到 id=1；
     * 本框架的 id 是 XFd 表下标，同样复用。所以必须先起"活着"的 20ms 定时器、
     * 再起"被 kill"的 100s 定时器，且此后不再新建定时器——否则被 kill 的 ID
     * 会被下一个注册复用，`m_trackedId` 就会误命中活定时器（旧写法正是如此：
     * 先 kill 再 start ⇒ 两者同 ID ⇒ 断言恒失败）。 */
    probe->m_anyTimerHits = 0;
    probe->m_loop = loop;
    probe->m_quitAfterHits = 1;
    liveId = XObject_startTimer((XObject*)probe, 20, XTimerType_CoarseTimer);
    XQ_CHECK(liveId != XTIMER_INVALID_ID);
    {
        XTimerId killed = XObject_startTimer((XObject*)probe, 100000,
                                                 XTimerType_CoarseTimer);
        XQ_CHECK(killed != XTIMER_INVALID_ID);
        XQ_CHECK(killed != liveId);  /* 活定时器在世 ⇒ 两个 ID 必须不同 */
        /* 100s 的定时器立刻取消；20ms 的活定时器负责退出循环。 */
        probe->m_trackedId = killed;
        XObject_killTimer((XObject*)probe, killed);
    }
    if (liveId != XTIMER_INVALID_ID)
        (void)XEventLoop_exec(loop, XEventLoop_AllEvents);
    XQ_EQ_I64(probe->m_timerEvents, 0);      /* 被 kill 的定时器一次都没触发 */
    XQ_CHECK(probe->m_anyTimerHits >= 1);    /* 未 kill 的定时器确实触发过 */

    XObject_killTimer((XObject*)probe, liveId);
    XClassDelete((XClass*)probe);
    XClassDelete((XClass*)loop);
}

/** @brief 动态属性：替换、NULL 即删除、名称列表拷贝/借用所有权。 */
XQ_TEST(XObject, dynamicProperties)
{
    XObject* obj = XObject_create();
    XString* key = XString_create_utf8("alignKey");
    XVariant* v1 = NULL;
    XVariant* v2 = NULL;
    XVariant* got = NULL;
    XVector* names = NULL;

    XQ_REQUIRE(obj != NULL && key != NULL);
    XQ_CHECK(XObject_property_const(obj, key) == NULL);

    v1 = XVariant_create_int(7);
    XQ_REQUIRE(v1 != NULL);
    /* 2026-11-11：返回值是 Qt 语义「是否命中静态属性」——动态写入恒 false；
     * 所有权**无条件**被对象接管（下面断言同一指针即可证明）。 */
    XQ_CHECK(!XObject_setProperty(obj, key, v1));
    got = XObject_property_const(obj, key);
    XQ_CHECK_PTR(got);
    XQ_CHECK(got == v1);                            /* 所有权已转移 */
    if (got) XQ_EQ_I64(XVariant_toInt64(got), 7);
    /* 借用语义：同一属性重复读取返回同一内部指针（不新建拷贝）。
     * 2026-11-11 API 收敛后唯一读取入口是 _const 版（旧的同实同义入口
     * XObject_property 已删除），故断言改为锁定借用指针的稳定性。 */
    XQ_CHECK(XObject_property_const(obj, key) == got);

    /* 替换：旧值由对象释放 */
    v2 = XVariant_create_int(9);
    XQ_REQUIRE(v2 != NULL);
    XQ_CHECK(!XObject_setProperty(obj, key, v2));
    got = XObject_property_const(obj, key);
    XQ_CHECK_PTR(got);
    if (got) XQ_EQ_I64(XVariant_toInt64(got), 9);

    /* 名称列表：按值返回拷贝（Qt 语义），调用方拥有 */
    names = XObject_dynamicPropertyNames(obj);
    XQ_CHECK_PTR(names);
    if (names) {
        XQ_EQ_SIZE(XContainer_size_base((const XContainer*)names), 1);
        if (XContainer_size_base((const XContainer*)names) == 1) {
            XString** namePtr = (XString**)XContainerDataAddr(names);
            XQ_CHECK(namePtr[0] != NULL);
            if (namePtr[0])
                XQ_EQ_STR(XString_toUtf8(namePtr[0]), "alignKey");
        }
        XClassDelete((XClass*)names);   /* 元素随容器一起释放 */
    }
    /* 借用版：内部指针，不得释放 */
    XQ_CHECK(XObject_dynamicPropertyNames_const(obj) != NULL);
    XQ_EQ_I64((long long)XContainer_size_base(
                  (const XContainer*)XObject_dynamicPropertyNames_const(obj)), 1);

    /* Qt 实测：写无效值 == 删除该属性，返回 false */
    XQ_CHECK(!XObject_setProperty(obj, key, NULL));
    XQ_CHECK(XObject_property_const(obj, key) == NULL);
    XQ_CHECK(XObject_dynamicPropertyNames_const(obj) == NULL
             || XContainer_size_base(
                    (const XContainer*)XObject_dynamicPropertyNames_const(obj)) == 0);

    XClassDelete((XClass*)obj);
    XClassDelete((XClass*)key);
}

/** @brief findChild/findChildren：浅层优先、递归开关、空名匹配首个子对象。 */
XQ_TEST(XObject, findChild)
{
    XObject* root = XObject_create();
    XObject* a = XObject_create();
    XObject* b = XObject_create();
    XObject* deep = XObject_create();
    XString* nameA = XString_create_utf8("a");
    XString* nameB = XString_create_utf8("b");
    XString* nameDeep = XString_create_utf8("deep");
    XString* empty = XString_create_utf8("");
    XObjectList* list = NULL;

    XQ_REQUIRE(root != NULL && a != NULL && b != NULL && deep != NULL);
    XQ_REQUIRE(nameA != NULL && nameB != NULL && nameDeep != NULL && empty != NULL);

    XObject_setObjectName(a, nameA);
    XObject_setObjectName(b, nameB);
    XObject_setObjectName(deep, nameDeep);
    XObject_setParent(a, root);
    XObject_setParent(b, root);
    XObject_setParent(deep, b);

    /* 只查直接子对象：深层匹配不到 */
    XQ_CHECK(XObject_findChild(root, nameDeep, XFindChildOption_DirectChildrenOnly) == NULL);
    /* 递归：能找到孙子 */
    XQ_CHECK(XObject_findChild(root, nameDeep, XFindChildOption_ChildrenRecursively) == deep);
    /* 同层命中 */
    XQ_CHECK(XObject_findChild(root, nameB, XFindChildOption_DirectChildrenOnly) == b);
    /* Qt 实测：空名字取**第一个**子对象（浅层） */
    XQ_CHECK(XObject_findChild(root, empty, XFindChildOption_DirectChildrenOnly) == a);
    XQ_CHECK(XObject_findChild(root, NULL, XFindChildOption_DirectChildrenOnly) == a);

    list = XObject_findChildren(root, NULL, XFindChildOption_ChildrenRecursively);
    XQ_CHECK_PTR(list);
    if (list) {
        XQ_EQ_SIZE(XContainer_size_base((const XContainer*)list), 3);
        XClassDelete((XClass*)list);
    }
    list = XObject_findChildren(root, nameB, XFindChildOption_ChildrenRecursively);
    XQ_CHECK_PTR(list);
    if (list) {
        XQ_EQ_SIZE(XContainer_size_base((const XContainer*)list), 1);
        XClassDelete((XClass*)list);
    }

    XClassDelete((XClass*)root);   /* a/b/deep 随 root 释放 */
    XClassDelete((XClass*)nameA);
    XClassDelete((XClass*)nameB);
    XClassDelete((XClass*)nameDeep);
    XClassDelete((XClass*)empty);
}

#if XRegularExpression_ON
/** @brief 正则版 findChild/findChildren（Qt 6.8 qobject.h:186-198 的
 *         `findChildren<T>(const QRegularExpression&, options)`）。
 *
 *  Qt 探针实测口径（本机 Qt 6.8.0 offscreen，/tmp/probe_qobject_regex.cpp）：
 *    - 主题是 objectName()，未设名字按空串 ⇒ 空模式 "" 与 "a*" 都命中无名对象，
 *      ".+" 不命中；
 *    - match() 非锚定："e" 命中 "deep"；"^d" 只命中以 d 开头的；
 *    - 无效模式（"("）不命中任何对象；
 *    - 列表顺序是深度优先前序：根下 a/b（b 下挂 deep）+ 无名对象时，
 *      re("") 的顺序是 [a, b, deep, 无名]、re("e") 是 [deep]。 */
XQ_TEST(XObject, findChildByRegex)
{
    XObject* root = XObject_create();
    XObject* a = XObject_create();
    XObject* b = XObject_create();
    XObject* deep = XObject_create();
    XObject* noname = XObject_create();
    XString* nameA = XString_create_utf8("a");
    XString* nameB = XString_create_utf8("b");
    XString* nameDeep = XString_create_utf8("deep");
    XRegularExpression* reE = NULL;
    XRegularExpression* reEmpty = NULL;
    XRegularExpression* reAnchored = NULL;
    XRegularExpression* reInvalid = NULL;
    XRegularExpression* reAlt = NULL;
    XObjectList* list = NULL;

    XQ_REQUIRE(root && a && b && deep && noname);
    XQ_REQUIRE(nameA && nameB && nameDeep);

    XObject_setObjectName(a, nameA);
    XObject_setObjectName(b, nameB);
    XObject_setObjectName(deep, nameDeep);
    /* noname 保持未设名字（objectName 为空） */
    XObject_setParent(a, root);
    XObject_setParent(b, root);
    XObject_setParent(deep, b);
    XObject_setParent(noname, root);

    reE = XRegularExpression_create_utf8("e", XRegularExpression_NoPatternOption);
    reEmpty = XRegularExpression_create_utf8("", XRegularExpression_NoPatternOption);
    reAnchored = XRegularExpression_create_utf8("^d", XRegularExpression_NoPatternOption);
    reInvalid = XRegularExpression_create_utf8("(", XRegularExpression_NoPatternOption);
    /* 同时命中孙子 deep 与"无名直接子对象"（^$ 匹配空名字） */
    reAlt = XRegularExpression_create_utf8("^deep$|^$", XRegularExpression_NoPatternOption);
    XQ_REQUIRE(reE && reEmpty && reAnchored && reInvalid && reAlt);
    XQ_CHECK(XRegularExpression_isValid(reE));
    XQ_CHECK(XRegularExpression_isValid(reEmpty));
    XQ_CHECK(!XRegularExpression_isValid(reInvalid));

    /* 非锚定匹配 + 深度优先前序：只有孙子 "deep" 命中 "e" */
    list = XObject_findChildren_regex(root, reE, XFindChildOption_ChildrenRecursively);
    XQ_CHECK_PTR(list);
    if (list) {
        XQ_EQ_SIZE(XContainer_size_base((const XContainer*)list), 1);
        if (XContainer_size_base((const XContainer*)list) == 1)
            XQ_CHECK(*(XObject**)XVector_at_base(list, 0) == deep);
        XClassDelete((XClass*)list);
    }
    /* DirectChildrenOnly 看不到孙子层（Qt 实测同口径） */
    list = XObject_findChildren_regex(root, reE, XFindChildOption_DirectChildrenOnly);
    XQ_CHECK_PTR(list);
    if (list) {
        XQ_EQ_SIZE(XContainer_size_base((const XContainer*)list), 0);
        XClassDelete((XClass*)list);
    }

    /* 空模式命中全部（含未设名字的对象），顺序 = 深度优先前序 */
    list = XObject_findChildren_regex(root, reEmpty, XFindChildOption_ChildrenRecursively);
    XQ_CHECK_PTR(list);
    if (list) {
        XQ_EQ_SIZE(XContainer_size_base((const XContainer*)list), 4);
        if (XContainer_size_base((const XContainer*)list) == 4) {
            XQ_CHECK(*(XObject**)XVector_at_base(list, 0) == a);
            XQ_CHECK(*(XObject**)XVector_at_base(list, 1) == b);
            XQ_CHECK(*(XObject**)XVector_at_base(list, 2) == deep);
            XQ_CHECK(*(XObject**)XVector_at_base(list, 3) == noname);
        }
        XClassDelete((XClass*)list);
    }

    /* 锚定首字符 */
    list = XObject_findChildren_regex(root, reAnchored, XFindChildOption_ChildrenRecursively);
    XQ_CHECK_PTR(list);
    if (list) {
        XQ_EQ_SIZE(XContainer_size_base((const XContainer*)list), 1);
        XClassDelete((XClass*)list);
    }

    /* 无效模式不命中任何对象（Qt 打印 invalid QRegularExpression 警告后返回空表） */
    list = XObject_findChildren_regex(root, reInvalid, XFindChildOption_ChildrenRecursively);
    XQ_CHECK_PTR(list);
    if (list) {
        XQ_EQ_SIZE(XContainer_size_base((const XContainer*)list), 0);
        XClassDelete((XClass*)list);
    }

    /* 单个对象入口（X 扩展，Qt 无单对象正则重载）：浅层优先 */
    XQ_CHECK(XObject_findChild_regex(root, reEmpty, XFindChildOption_DirectChildrenOnly) == a);
    XQ_CHECK(XObject_findChild_regex(root, reE, XFindChildOption_DirectChildrenOnly) == NULL);
    XQ_CHECK(XObject_findChild_regex(root, reE, XFindChildOption_ChildrenRecursively) == deep);
    /* 浅层优先 vs 深度优先前序的**区分用例**：reAlt 同时命中孙子 deep 和
     * 后面的无名直接子对象 noname。
     *   - findChild（本函数）先扫完所有直接子对象，再递归 ⇒ noname；
     *   - findChildren 深度优先前序 ⇒ [deep, noname]（deep 先出现）。 */
    XQ_CHECK(XObject_findChild_regex(root, reAlt,
                                     XFindChildOption_ChildrenRecursively) == noname);
    list = XObject_findChildren_regex(root, reAlt, XFindChildOption_ChildrenRecursively);
    XQ_CHECK_PTR(list);
    if (list) {
        XQ_EQ_SIZE(XContainer_size_base((const XContainer*)list), 2);
        if (XContainer_size_base((const XContainer*)list) == 2) {
            XQ_CHECK(*(XObject**)XVector_at_base(list, 0) == deep);
            XQ_CHECK(*(XObject**)XVector_at_base(list, 1) == noname);
        }
        XClassDelete((XClass*)list);
    }
    /* classVtable == NULL 的 class 版等价于不按类型过滤 */
    XQ_CHECK(XObject_findChild_class_regex(root, NULL, reE,
                                           XFindChildOption_ChildrenRecursively) == deep);

    /* 参数守卫：NULL 表达式按"无效模式"不命中；NULL self 返回 NULL/空 */
    XQ_CHECK(XObject_findChild_regex(root, NULL, XFindChildOption_ChildrenRecursively) == NULL);
    XQ_CHECK(XObject_findChild_regex(NULL, reEmpty, XFindChildOption_ChildrenRecursively) == NULL);
    XQ_CHECK(XObject_findChildren_regex(NULL, reEmpty, XFindChildOption_ChildrenRecursively) == NULL);
    list = XObject_findChildren_regex(root, NULL, XFindChildOption_ChildrenRecursively);
    XQ_CHECK_PTR(list);
    if (list) {
        XQ_EQ_SIZE(XContainer_size_base((const XContainer*)list), 0);
        XClassDelete((XClass*)list);
    }

    XClassDelete((XClass*)root);   /* a/b/deep/noname 随 root 释放 */
    XClassDelete((XClass*)nameA);
    XClassDelete((XClass*)nameB);
    XClassDelete((XClass*)nameDeep);
    XClassDelete((XClass*)reE);
    XClassDelete((XClass*)reEmpty);
    XClassDelete((XClass*)reAnchored);
    XClassDelete((XClass*)reInvalid);
    XClassDelete((XClass*)reAlt);
}
#endif /* XRegularExpression_ON */

/** @brief disconnect：一次断光重复连接；三个过滤条件可为空的批量断开。 */
XQ_TEST(XObject, disconnectSemantics)
{
    XObject* emitter = XObject_create();
    XObject* receiver = XObject_create();
    XObject* other = XObject_create();

    xqResetCounters();
    XQ_REQUIRE(emitter != NULL && receiver != NULL && other != NULL);

    /* 同一 (signal,receiver,slot) 连三次 */
    XQ_CHECK(XObject_connect(emitter, XSignal(xqAlignProbeSignal), receiver,
                               xqSlot_count, XConnectionType_Direct) != NULL);
    XQ_CHECK(XObject_connect(emitter, XSignal(xqAlignProbeSignal), receiver,
                               xqSlot_count, XConnectionType_Direct) != NULL);
    XQ_CHECK(XObject_connect(emitter, XSignal(xqAlignProbeSignal), receiver,
                               xqSlot_count, XConnectionType_Direct) != NULL);
    XQ_EQ_I64(XObject_receivers(emitter, XSignal(xqAlignProbeSignal)), 3);
    XQ_CHECK(XObject_isSignalConnected(emitter, XSignal(xqAlignProbeSignal)));

    /* Qt 实测：一次 disconnect 断掉全部匹配（旧实现只断第一条） */
    XQ_CHECK(XObject_disconnect(emitter, XSignal(xqAlignProbeSignal), receiver,
                                  xqSlot_count));
    XQ_EQ_I64(XObject_receivers(emitter, XSignal(xqAlignProbeSignal)), 0);
    xqAlignProbeSignal(emitter);
    XQ_EQ_I64(g_slotHits, 0);

    /* 没有匹配时返回 false（Qt 同口径） */
    XQ_CHECK(!XObject_disconnect(emitter, XSignal(xqAlignProbeSignal), receiver,
                                   xqSlot_count));

    /* 批量形态：receiver 指定、槽为 NULL —— 断掉该接收者的全部槽 */
    XQ_CHECK(XObject_connect(emitter, XSignal(xqAlignProbeSignal), receiver,
                               xqSlot_count, XConnectionType_Direct) != NULL);
    XQ_CHECK(XObject_connect(emitter, XSignal(xqAlignProbeSignal), other,
                               xqSlot_count, XConnectionType_Direct) != NULL);
    XQ_EQ_I64(XObject_receivers(emitter, XSignal(xqAlignProbeSignal)), 2);
    XQ_CHECK(XObject_disconnect(emitter, XSignal(xqAlignProbeSignal), receiver, NULL));
    XQ_EQ_I64(XObject_receivers(emitter, XSignal(xqAlignProbeSignal)), 1);
    xqAlignProbeSignal(emitter);
    XQ_EQ_I64(g_slotHits, 1);   /* 只剩 other 的槽 */

    /* 实例版 disconnect()：signal=0 且 receiver/slot 均空 ⇒ 断开本对象全部 */
    XQ_CHECK(XObject_connect(emitter, XSignal(XObject_destroyed_signal), receiver,
                               xqSlot_count, XConnectionType_Direct) != NULL);
    XQ_CHECK(XObject_disconnect(emitter, 0, NULL, NULL));
    XQ_EQ_I64(XObject_receivers(emitter, XSignal(xqAlignProbeSignal)), 0);
    XQ_EQ_I64(XObject_receivers(emitter, XSignal(XObject_destroyed_signal)), 0);

    /* disconnect_2 走连接句柄，语义不变 */
    {
        XConnection* conn = XObject_connect(emitter, XSignal(xqAlignProbeSignal),
                                              receiver, xqSlot_count,
                                              XConnectionType_Direct);
        XQ_CHECK_PTR(conn);
        XQ_CHECK(XObject_disconnect_2(conn));
        XQ_EQ_I64(XObject_receivers(emitter, XSignal(xqAlignProbeSignal)), 0);
    }

    XClassDelete((XClass*)emitter);
    XClassDelete((XClass*)receiver);
    XClassDelete((XClass*)other);
}

/** @brief sender/senderSignalIndex：槽内有值、槽外 -1（Qt 实测）。 */
XQ_TEST(XObject, senderContext)
{
    XObject* emitter = XObject_create();
    XObject* receiver = XObject_create();

    xqResetCounters();
    XQ_REQUIRE(emitter != NULL && receiver != NULL);

    /* 槽外调用返回 -1（旧头文件写"返回 0"，与实现/Qt 都不符） */
    XQ_EQ_I64(XObject_senderSignalIndex(receiver), -1);
    XQ_CHECK(XObject_sender(receiver) == NULL);

    XQ_CHECK(XObject_connect(emitter, XSignal(xqAlignProbeSignal), receiver,
                               xqSlot_count, XConnectionType_Direct) != NULL);
    xqAlignProbeSignal(emitter);

    XQ_EQ_I64(g_slotHits, 1);
    XQ_CHECK(g_lastSender == emitter);
    XQ_CHECK(g_lastSignalIndex != -1);
    /* 离开槽上下文后恢复 -1 */
    XQ_EQ_I64(XObject_senderSignalIndex(receiver), -1);

    XClassDelete((XClass*)emitter);
    XClassDelete((XClass*)receiver);
}

/** @brief 对标 Q_DISABLE_COPY(QObject)：XClassCopy 不得改动目标对象。 */
XQ_TEST(XObject, copyForbidden)
{
    XObject* src = XObject_create();
    XObject* dst = XObject_create();
    XString* srcName = XString_create_utf8("src-name");
    XString* dstName = XString_create_utf8("dst-name");

    XQ_REQUIRE(src != NULL && dst != NULL && srcName != NULL && dstName != NULL);
    XObject_setObjectName(src, srcName);
    XObject_setObjectName(dst, dstName);

    XClassCopy(dst, src);   /* Qt：QObject 拷贝被删除；此处为空操作 + 报错 */

    {
        const XString* got = XObject_objectName(dst);
        XQ_CHECK_PTR(got);
        if (got) XQ_EQ_STR(XString_toUtf8(got), "dst-name");
    }
    XQ_CHECK(XObject_parent(dst) == NULL);

    XClassDelete((XClass*)src);
    XClassDelete((XClass*)dst);
    XClassDelete((XClass*)srcName);
    XClassDelete((XClass*)dstName);
}

/** @brief dumpObjectTree/dumpObjectInfo：Qt 6.8 格式（调用不崩溃）。 */
XQ_TEST(XObject, dumpSmoke)
{
    XObject* root = XObject_create();
    XObject* child = NULL;
    XQ_REQUIRE(root != NULL);
    {
        XString* rootName = XString_create_utf8("align-root");
        XString* childName = XString_create_utf8("align-child");
        XQ_REQUIRE(rootName != NULL && childName != NULL);
        XObject_setObjectName(root, rootName);
        child = XObject_createWithParent(root);
        XQ_REQUIRE(child != NULL);
        XObject_setObjectName(child, childName);
        XClassDelete((XClass*)rootName);
        XClassDelete((XClass*)childName);
    }

    XObject_dumpObjectTree(root);   /* 期望：`XObject::align-root ` + 4 空格缩进 */
    XObject_dumpObjectInfo(root);   /* 期望：OBJECT/SIGNALS OUT/SIGNALS IN */
    XQ_CHECK(XObject_parent(child) == root);
    XQ_EQ_I64(XObject_receivers(root, XSignal(XObject_destroyed_signal)), 0);

    XClassDelete((XClass*)root);    /* child 随 root 释放 */
}

/* ==================== 2026-11-11 批次：类元信息 / 句柄 / 事件载荷语义 ==================== */

/* ---- 带上下文重载的槽（首参必须是发送者，不是 context） ---- */
static int g_ctxHits;
static XObject* g_ctxFirstArg;
/** @brief `connect(sender, signal, context, functor)` 的 functor：首参=发送者。 */
static void xqSlot_ctxFunctor(XObject* sender, XVarList* args)
{
    (void)args;
    ++g_ctxHits;
    g_ctxFirstArg = sender;
}

/** @brief destroyed 载荷槽：把参数里的对象记下来（对标 Qt destroyed(QObject*)）。 */
static XObject* g_destroyedPayload;
static void xqSlot_destroyedPayload(XObject* receiver, XVarList* args)
{
    XObject* obj = NULL;
    ++g_destroyCount;
    if (args) {
        XVarList_start(args);
        obj = XVarList_arg(args, XObject*);
    }
    g_destroyedPayload = obj;
    if (g_destroyQuitLoop)
        XEventLoop_exit(g_destroyQuitLoop, 0);
    (void)receiver;
}

/** @brief 对标 Qt 6.8 QMetaObject 的最小类身份集：类名 / inherits / isA / 父链。 */
XQ_TEST(XObject, classMetadata)
{
    XQAlignProbe* probe = XQAlignProbe_create();
    XObject* plain = XObject_create();

    XQ_REQUIRE(probe != NULL && plain != NULL);

    /* className()：类名来自 XVTABLE_INIT_DEFAULT(<类型名>) 的 #Type */
    XQ_EQ_STR(XObject_className((XObject*)probe), "XQAlignProbe");
    XQ_EQ_STR(XObject_className(plain), "XObject");

    /* inherits()：沿 superClass() 链比对类名（含自身，含后代） */
    XQ_CHECK(XObject_inherits((XObject*)probe, "XQAlignProbe"));
    XQ_CHECK(XObject_inherits((XObject*)probe, "XObject"));
    XQ_CHECK(XObject_inherits((XObject*)probe, "XClass"));
    XQ_CHECK(!XObject_inherits((XObject*)probe, "XWidget"));
    XQ_CHECK(!XObject_inherits(plain, "XQAlignProbe"));
    XQ_CHECK(XObject_inherits(plain, "XObject"));
    /* 名字大小写敏感（Qt 同口径） */
    XQ_CHECK(!XObject_inherits(plain, "xobject"));

    /* qobject_cast 等价：按**类表指针**沿父链比对 */
    XQ_CHECK(XObject_isA_class((XObject*)probe, XQAlignProbe_class_init()));
    XQ_CHECK(XObject_isA_class((XObject*)probe, XObject_class_init()));
    XQ_CHECK(!XObject_isA_class(plain, XQAlignProbe_class_init()));
    XQ_CHECK(XObject_isA_class(plain, XObject_class_init()));

    /* 虚函数表的父链登记（对标 QMetaObject::superClass()） */
    XQ_CHECK(XVtable_parent(XQAlignProbe_class_init()) == XObject_class_init());
    XQ_CHECK(XVtable_parent(XObject_class_init()) == XClass_class_init());
    XQ_CHECK(XVtable_parent(XClass_class_init()) == NULL);   /* XClass 是根 */

    XClassDelete((XClass*)plain);
    XClassDelete((XClass*)probe);
}

/** @brief 按类型的 findChild/findChildren（对标 findChild<T>，inherits 语义）。 */
XQ_TEST(XObject, findChildByClass)
{
    XObject* root = XObject_create();
    XQAlignProbe* probeA = XQAlignProbe_create();
    XObject* objB = XObject_create();
    XQAlignProbe* probeDeep = XQAlignProbe_create();
    XObjectList* list = NULL;

    XQ_REQUIRE(root != NULL && probeA != NULL && objB != NULL && probeDeep != NULL);
    XObject_setParent((XObject*)probeA, root);
    XObject_setParent(objB, root);
    XObject_setParent((XObject*)probeDeep, objB);       /* 孙子层 */

    /* 只要直接子对象里的探针类型：跳过 objB */
    XQ_CHECK(XObject_findChild_class(root, NULL, XQAlignProbe_class_init(),
                                     XFindChildOption_DirectChildrenOnly)
             == (XObject*)probeA);
    /* 递归才能找到孙子层的探针 */
    XQ_CHECK(XObject_findChild_class(root, NULL, XQAlignProbe_class_init(),
                                     XFindChildOption_ChildrenRecursively)
             == (XObject*)probeA);   /* 浅层优先：先命中 probeA */
    XQ_CHECK(XObject_findChild_class(objB, NULL, XQAlignProbe_class_init(),
                                     XFindChildOption_DirectChildrenOnly)
             == (XObject*)probeDeep);
    /* 反过来：用 XObject 过滤在 objB 下也应命中 probeDeep（它是 XObject 后代） */
    XQ_CHECK(XObject_findChild_class(objB, NULL, XObject_class_init(),
                                     XFindChildOption_DirectChildrenOnly)
             == (XObject*)probeDeep);
    /* classVtable==NULL ⇒ 不按类型过滤（findChild<QObject*>） */
    XQ_CHECK(XObject_findChild_class(root, NULL, NULL,
                                     XFindChildOption_DirectChildrenOnly)
             == (XObject*)probeA);
    /* 任何对象 isA(XObject)：XObject 过滤命中全部 */
    XQ_CHECK(XObject_findChild_class(root, NULL, XObject_class_init(),
                                     XFindChildOption_DirectChildrenOnly)
             == (XObject*)probeA);

    list = XObject_findChildren_class(root, NULL, XQAlignProbe_class_init(),
                                      XFindChildOption_ChildrenRecursively);
    XQ_CHECK_PTR(list);
    if (list) {
        XQ_EQ_SIZE(XContainer_size_base((const XContainer*)list), 2);
        XClassDelete((XClass*)list);
    }
    list = XObject_findChildren_class(root, NULL, XObject_class_init(),
                                      XFindChildOption_ChildrenRecursively);
    XQ_CHECK_PTR(list);
    if (list) {
        XQ_EQ_SIZE(XContainer_size_base((const XContainer*)list), 3);
        XClassDelete((XClass*)list);
    }

    XClassDelete((XClass*)root);   /* probeA/objB/probeDeep 随 root 释放 */
}

/** @brief setProperty 的 Qt 返回值语义 + 所有权解耦 + 按值读取副本。 */
XQ_TEST(XObject, setPropertyQtSemantics)
{
    XObject* obj = XObject_create();
    XString* key = XString_create_utf8("qtKey");
    XVariant* v = NULL;
    XVariant* copy = NULL;
    XVector* names = NULL;

    XQ_REQUIRE(obj != NULL && key != NULL);

    /* Qt 6.8 实测：动态属性写入成功也返回 false（返回值 = 是否命中静态属性） */
    v = XVariant_create_int(7);
    XQ_REQUIRE(v != NULL);
    XQ_CHECK(!XObject_setProperty(obj, key, v));      /* false 但值已写入 */
    XQ_CHECK(XObject_property_const(obj, key) == v);  /* 同一指针：对象接管所有权 */
    XQ_EQ_I64(XVariant_toInt64(XObject_property_const(obj, key)), 7);
    names = XObject_dynamicPropertyNames(obj);
    XQ_CHECK_PTR(names);
    if (names) {
        XQ_EQ_SIZE(XContainer_size_base((const XContainer*)names), 1);
        XClassDelete((XClass*)names);
    }

    /* property_copy：按值返回独立副本（Qt 的 QVariant property() 语义） */
    copy = XObject_property_copy(obj, key);
    XQ_CHECK_PTR(copy);
    if (copy) {
        XQ_CHECK(copy != XObject_property_const(obj, key));
        XQ_EQ_I64(XVariant_toInt64(copy), 7);
        XClassDelete((XClass*)copy);
    }

    /* 替换：旧值由对象释放；调用方不再持有任何东西 */
    XQ_CHECK(!XObject_setProperty(obj, key, XVariant_create_int(9)));
    XQ_EQ_I64(XVariant_toInt64(XObject_property_const(obj, key)), 9);

    /* 写 NULL = 删除（Qt 的 setProperty(name, QVariant())） */
    XQ_CHECK(!XObject_setProperty(obj, key, NULL));
    XQ_CHECK(XObject_property_const(obj, key) == NULL);
    XQ_CHECK(XObject_property_copy(obj, key) == NULL);

    XClassDelete((XClass*)obj);
    XClassDelete((XClass*)key);
}

/** @brief 动态属性变更事件：新增/改值/删除各 1 次，同值与删空 0 次（Qt 实测）。 */
XQ_TEST(XObject, dynamicPropertyChangeEvent)
{
    XQAlignProbe* probe = XQAlignProbe_create();
    XString* key = XString_create_utf8("dynProp");
    XString* other = XString_create_utf8("absent");

    XQ_REQUIRE(probe != NULL && key != NULL && other != NULL);

    /* 新增 → 1 次，且事件带属性名 */
    (void)XObject_setProperty((XObject*)probe, key, XVariant_create_int(1));
    XQ_EQ_I64(probe->m_dynPropEvents, 1);
    XQ_EQ_STR(probe->m_lastDynProp, "dynProp");

    /* 同值再写 → 0 次（Qt 6.8 探针实测：同值不投递） */
    (void)XObject_setProperty((XObject*)probe, key, XVariant_create_int(1));
    XQ_EQ_I64(probe->m_dynPropEvents, 1);

    /* 改值 → 1 次 */
    (void)XObject_setProperty((XObject*)probe, key, XVariant_create_int(2));
    XQ_EQ_I64(probe->m_dynPropEvents, 2);

    /* 删除已有 → 1 次（唯一删除入口：setProperty(name, NULL)；
     * XObject_removeProperty 已删除——Qt 6.8 没有 QObject::removeProperty） */
    (void)XObject_setProperty((XObject*)probe, key, NULL);
    XQ_EQ_I64(probe->m_dynPropEvents, 3);
    XQ_EQ_STR(probe->m_lastDynProp, "dynProp");

    /* 删除不存在 → 0 次 */
    (void)XObject_setProperty((XObject*)probe, other, NULL);
    XQ_EQ_I64(probe->m_dynPropEvents, 3);

    /* setProperty(name, NULL) 走删除路径：属性存在则发事件 */
    (void)XObject_setProperty((XObject*)probe, key, XVariant_create_int(5));
    XQ_EQ_I64(probe->m_dynPropEvents, 4);
    (void)XObject_setProperty((XObject*)probe, key, NULL);
    XQ_EQ_I64(probe->m_dynPropEvents, 5);

    XClassDelete((XClass*)probe);
    XClassDelete((XClass*)key);
    XClassDelete((XClass*)other);
}

/** @brief destroyed 信号携带被销毁对象（对标 Qt `destroyed(QObject*)`）。 */
XQ_TEST(XObject, destroyedPayload)
{
    XObject* victim = XObject_create();
    XObject* observer = XObject_create();

    xqResetCounters();
    g_destroyedPayload = NULL;
    XQ_REQUIRE(victim != NULL && observer != NULL);

    XQ_CHECK(XObject_connect(victim, XSignal(XObject_destroyed_signal),
                               observer, xqSlot_destroyedPayload,
                               XConnectionType_Direct) != NULL);
    XClassDelete((XClass*)victim);

    XQ_EQ_I64(g_destroyCount, 1);
    /* Qt 6.8 实测：槽参数 == 被 delete 的对象指针 */
    XQ_CHECK(g_destroyedPayload == victim);

    XClassDelete((XClass*)observer);
}

/** @brief 连接句柄：引用计数、失效判定、**重复断开安全**。 */
XQ_TEST(XObject, connectionHandleLifecycle)
{
    XObject* emitter = XObject_create();
    XObject* receiver = XObject_create();
    XConnection* conn = NULL;

    xqResetCounters();
    XQ_REQUIRE(emitter != NULL && receiver != NULL);

    conn = XObject_connect(emitter, XSignal(xqAlignProbeSignal), receiver,
                             xqSlot_count, XConnectionType_Direct);
    XQ_CHECK_PTR(conn);
    XQ_REQUIRE(conn != NULL);
    XQ_CHECK(XConnection_isValid(conn));
    XQ_CHECK(!XObject_disconnect_2(NULL));       /* NULL 句柄安全 */

    /* 调用方要跨"断开"保存句柄 ⇒ 先 retain */
    XQ_CHECK(XConnection_retain(conn));
    XQ_CHECK(XObject_disconnect_2(conn));        /* 断开成功 */
    XQ_CHECK(!XConnection_isValid(conn));        /* 句柄已失效 */
    /* **重复断开**：不得崩溃，返回 false（Qt 对已断开的 Connection 同口径） */
    XQ_CHECK(!XObject_disconnect_2(conn));
    XQ_CHECK(!XConnection_disconnect(conn));
    xqAlignProbeSignal(emitter);
    XQ_EQ_I64(g_slotHits, 0);

    /* 释放调用方自己那一份引用：连接对象此时才真正回收 */
    XQ_CHECK(XConnection_release(conn));
    /* 已释放的旧句柄：任何入口都只返回 false，不解引用 */
    XQ_CHECK(!XConnection_isValid(conn));
    XQ_CHECK(!XConnection_retain(conn));
    XQ_CHECK(!XConnection_release(conn));
    XQ_CHECK(!XObject_disconnect_2(conn));

    /* 不 retain 的句柄：断开即回收（活表注销 ⇒ 旧指针被识别为失效） */
    {
        XConnection* plain = XObject_connect(emitter, XSignal(xqAlignProbeSignal),
                                               receiver, xqSlot_count,
                                               XConnectionType_Direct);
        XQ_CHECK_PTR(plain);
        XQ_CHECK(XObject_disconnect_2(plain));
        XQ_CHECK(!XObject_disconnect_2(plain));   /* 重复断开：安全 */
    }

    XClassDelete((XClass*)emitter);
    XClassDelete((XClass*)receiver);
}

/** @brief connect(sender,signal,context,functor)：首参=发送者、context 亡即断开。 */
XQ_TEST(XObject, connectWithContext)
{
    XObject* emitter = XObject_create();
    XObject* context = XObject_create();
    XConnection* conn = NULL;

    g_ctxHits = 0;
    g_ctxFirstArg = NULL;
    XQ_REQUIRE(emitter != NULL && context != NULL);

    conn = XObject_connect_context(emitter, XSignal(xqAlignProbeSignal), context,
                                   xqSlot_ctxFunctor, XConnectionType_Auto);
    XQ_CHECK_PTR(conn);
    XQ_REQUIRE(conn != NULL);

    xqAlignProbeSignal(emitter);
    XQ_EQ_I64(g_ctxHits, 1);
    /* Qt 的 functor 重载：函数拿到的首参是**发送者**，不是 context */
    XQ_CHECK(g_ctxFirstArg == emitter);

    /* context 析构 ⇒ 连接自动断开（对标 Qt 的 context 生命周期语义） */
    XClassDelete((XClass*)context);
    xqAlignProbeSignal(emitter);
    XQ_EQ_I64(g_ctxHits, 1);
    /* 句柄随 context 一起失效；重复断开安全 */
    XQ_CHECK(!XObject_disconnect_2(conn));

    /* 参数守卫：context 为 NULL 不建立连接（Qt 的 functor 重载要求非空 context） */
    XQ_CHECK(XObject_connect_context(emitter, XSignal(xqAlignProbeSignal), NULL,
                                     xqSlot_ctxFunctor, XConnectionType_Auto) == NULL);

    XClassDelete((XClass*)emitter);
}

/** @brief QSignalBlocker（C 版 ctor/dtor）：作用域内阻塞、退出恢复**前态**。 */
XQ_TEST(XObject, signalBlocker)
{
    XObject* emitter = XObject_create();
    XObject* receiver = XObject_create();
    XSignalBlocker blocker;
    XSignalBlocker nested;

    xqResetCounters();
    XQ_REQUIRE(emitter != NULL && receiver != NULL);
    XQ_CHECK(XObject_connect(emitter, XSignal(xqAlignProbeSignal), receiver,
                               xqSlot_count, XConnectionType_Direct) != NULL);

    /* 作用域内阻塞 */
    XSignalBlocker_ctor(&blocker, emitter);
    XQ_CHECK(XObject_signalsBlocked(emitter));
    xqAlignProbeSignal(emitter);
    XQ_EQ_I64(g_slotHits, 0);

    /* 嵌套守卫：内层退出仍保持阻塞（记住的是"进入前已阻塞"） */
    XSignalBlocker_ctor(&nested, emitter);
    XSignalBlocker_dtor(&nested);
    XQ_CHECK(XObject_signalsBlocked(emitter));
    xqAlignProbeSignal(emitter);
    XQ_EQ_I64(g_slotHits, 0);

    /* 外层退出：恢复到构造前的 false */
    XSignalBlocker_dtor(&blocker);
    XQ_CHECK(!XObject_signalsBlocked(emitter));
    xqAlignProbeSignal(emitter);
    XQ_EQ_I64(g_slotHits, 1);

    /* 构造前已阻塞 ⇒ 析构后仍阻塞（Qt 的 QSignalBlocker 恢复前态，不是无条件解开） */
    (void)XObject_blockSignals(emitter, true);
    XSignalBlocker_ctor(&blocker, emitter);
    XSignalBlocker_dtor(&blocker);
    XQ_CHECK(XObject_signalsBlocked(emitter));
    (void)XObject_blockSignals(emitter, false);
    /* 空对象守卫：no-op，不崩溃 */
    XSignalBlocker_ctor(&blocker, NULL);
    XSignalBlocker_dtor(&blocker);
    XSignalBlocker_dtor(&blocker);   /* 重复析构安全 */

    XClassDelete((XClass*)emitter);
    XClassDelete((XClass*)receiver);
}

/** @brief QSignalBlocker 的 reblock/unblock/dismiss（Qt 6.8 qobject.h:495-497，
 *         实现 qobject.h:548-565）。
 *
 *  本机 Qt 6.8.0 探针实测（/tmp/probe_qobject_regex.cpp）：
 *    - `unblock()` 恢复**进入前**的状态（不是无条件 false）并置 inhibit
 *      ⇒ 出作用域（析构）不再改对象；进入前已阻塞时 unblock 后仍阻塞；
 *    - `reblock()` 无条件 `blockSignals(true)` 并**清** inhibit
 *      ⇒ 出作用域按进入前状态恢复；
 *    - `dismiss()` 只清 m_o ⇒ 出作用域什么都不做，对象保持阻塞；
 *      dismiss 之后 reblock/unblock 对对象无副作用。 */
XQ_TEST(XObject, signalBlockerModes)
{
    XObject* emitter = XObject_create();
    XObject* receiver = XObject_create();
    XSignalBlocker blocker;

    xqResetCounters();
    XQ_REQUIRE(emitter != NULL && receiver != NULL);
    XQ_CHECK(XObject_connect(emitter, XSignal(xqAlignProbeSignal), receiver,
                               xqSlot_count, XConnectionType_Direct) != NULL);

    /* --- unblock：立即解除（恢复进入前 false），destroy 不再动对象 --- */
    XSignalBlocker_init(&blocker, emitter);
    XQ_CHECK(XObject_signalsBlocked(emitter));
    XSignalBlocker_unblock(&blocker);
    XQ_CHECK(!XObject_signalsBlocked(emitter));
    xqAlignProbeSignal(emitter);
    XQ_EQ_I64(g_slotHits, 1);
    XSignalBlocker_destroy(&blocker);          /* inhibit ⇒ no-op */
    XQ_CHECK(!XObject_signalsBlocked(emitter));
    xqAlignProbeSignal(emitter);
    XQ_EQ_I64(g_slotHits, 2);

    /* --- reblock：unblock 后重新阻塞，destroy 恢复进入前（false） --- */
    XSignalBlocker_init(&blocker, emitter);
    XSignalBlocker_unblock(&blocker);
    XQ_CHECK(!XObject_signalsBlocked(emitter));
    XSignalBlocker_reblock(&blocker);
    XQ_CHECK(XObject_signalsBlocked(emitter));
    XSignalBlocker_destroy(&blocker);
    XQ_CHECK(!XObject_signalsBlocked(emitter));

    /* --- 进入前已阻塞：unblock 恢复的是前态 true（不是无条件 false） --- */
    (void)XObject_blockSignals(emitter, true);
    XSignalBlocker_init(&blocker, emitter);
    XSignalBlocker_unblock(&blocker);
    XQ_CHECK(XObject_signalsBlocked(emitter));
    XSignalBlocker_destroy(&blocker);
    XQ_CHECK(XObject_signalsBlocked(emitter));
    (void)XObject_blockSignals(emitter, false);

    /* --- dismiss：放弃守卫，对象保持阻塞；destroy 为 no-op --- */
    XSignalBlocker_init(&blocker, emitter);
    XQ_CHECK(XObject_signalsBlocked(emitter));
    XSignalBlocker_dismiss(&blocker);
    XSignalBlocker_destroy(&blocker);
    XQ_CHECK(XObject_signalsBlocked(emitter));   /* Qt 实测：仍阻塞 */
    (void)XObject_blockSignals(emitter, false);
    xqAlignProbeSignal(emitter);
    XQ_EQ_I64(g_slotHits, 3);

    /* --- dismiss 之后 reblock/unblock 对对象无副作用 --- */
    XSignalBlocker_init(&blocker, emitter);      /* 对象被阻塞 */
    XSignalBlocker_dismiss(&blocker);
    XSignalBlocker_reblock(&blocker);            /* m_object 已空 ⇒ 无动作 */
    XSignalBlocker_unblock(&blocker);            /* 同上 */
    XQ_CHECK(XObject_signalsBlocked(emitter));
    XSignalBlocker_destroy(&blocker);            /* 仍 no-op */
    XQ_CHECK(XObject_signalsBlocked(emitter));
    (void)XObject_blockSignals(emitter, false);

    /* --- NULL 守卫安全 --- */
    XSignalBlocker_reblock(NULL);
    XSignalBlocker_unblock(NULL);
    XSignalBlocker_dismiss(NULL);
    XSignalBlocker_destroy(NULL);

    XClassDelete((XClass*)emitter);
    XClassDelete((XClass*)receiver);
}

/** @brief connectNotify/disconnectNotify 命中**发送者**（Qt 6.8 实测）。 */
XQ_TEST(XObject, connectNotifyOnSender)
{
    XQAlignProbe* emitter = XQAlignProbe_create();
    XQAlignProbe* receiver = XQAlignProbe_create();

    xqResetCounters();
    XQ_REQUIRE(emitter != NULL && receiver != NULL);

    XQ_CHECK(XObject_connect((XObject*)emitter, XSignal(xqAlignProbeSignal),
                               (XObject*)receiver, xqSlot_count,
                               XConnectionType_Direct) != NULL);
    /* Qt 文档 "called when something has been connected to signal in this object"：
     * this = 信号所属对象 ⇒ 发送者 1 次、接收者 0 次（本机 Qt 6.8 探针实测） */
    XQ_EQ_I64(emitter->m_connectNotifies, 1);
    XQ_EQ_I64(receiver->m_connectNotifies, 0);

    XQ_CHECK(XObject_disconnect((XObject*)emitter, XSignal(xqAlignProbeSignal),
                                  (XObject*)receiver, xqSlot_count));
    XQ_EQ_I64(emitter->m_disconnectNotifies, 1);
    XQ_EQ_I64(receiver->m_disconnectNotifies, 0);

    XClassDelete((XClass*)emitter);
    XClassDelete((XClass*)receiver);
}

/** @brief 事件 accepted 缺省 true（对标 QEvent::m_accept = true）+ event() 返回语义。 */
XQ_TEST(XObject, eventAcceptedDefault)
{
    XObject* obj = XObject_create();
    XEvent ev;
    bool handled = false;

    XQ_REQUIRE(obj != NULL);

    /* Qt 6.8：QEvent 构造即 accepted（本机实测 QEvent(User).isAccepted()==1） */
    XEvent_init(&ev, (XEventType)(XEVENT_TYPE_USER + 9));
    XQ_CHECK(XEvent_isAccepted(&ev));
    XEvent_ignore(&ev);
    XQ_CHECK(!XEvent_isAccepted(&ev));
    XEvent_accept(&ev);
    XQ_CHECK(XEvent_isAccepted(&ev));

    /* 未识别的**非 User** 类型：QObject::event 返回 false（Qt 的 default 分支），
     * 但事件自身仍是 accepted——两个概念互不干扰。 */
    XEvent_init(&ev, XEVENT_TYPE_NONE);
    handled = XCoreApplication_sendEvent(obj, &ev);
    XQ_CHECK(!handled);
    XQ_CHECK(XEvent_isAccepted(&ev));

    XClassDelete((XClass*)obj);
}

/** @brief 延迟删除的级别与「exec cleanup 冲刷」（Qt 6.8 探针逐条实测口径）。 */
XQ_TEST(XObject, deferredDeleteFlushAtExecCleanup)
{
    XEventLoop* loop = NULL;
    XQAlignProbe* driver = NULL;
    XObject* victim = NULL;
    XObject* observer = NULL;

    xqResetCounters();
    loop = XEventLoop_create();
    XQ_REQUIRE(loop != NULL);
    driver = XQAlignProbe_create();
    observer = XObject_create();
    victim = XObject_create();
    XQ_REQUIRE(driver != NULL && observer != NULL && victim != NULL);

    XQ_CHECK(XObject_connect(victim, XSignal(XObject_destroyed_signal),
                               observer, xqSlot_destroyed,
                               XConnectionType_Auto) != NULL);

    /* 场景照搬本机 Qt 6.8 探针（QEventLoop + 一个 0ms 定时器，槽里
     * `late->deleteLater(); loop.quit();`）：Qt 实测 `loop.exec()` 返回时
     * destroyed 命中 0 次，随后显式 `sendPostedEvents(nullptr, DeferredDelete)`
     * 才命中 1 次——即**普通 QEventLoop 退出不冲刷**，冲刷是
     * `QCoreApplicationPrivate::execCleanup` 那一拍（qcoreapplication.cpp:1539
     * `QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete)`）。 */
    driver->m_loop = loop;
    driver->m_quitAfterHits = 1;
    driver->m_deleteLaterOnHit = victim;      /* 定时器槽内 deleteLater */
    {
        XTimerId id = XObject_startTimer((XObject*)driver, 10, XTimerType_CoarseTimer);
        XQ_REQUIRE(id != XTIMER_INVALID_ID);
        (void)XEventLoop_exec(loop, XEventLoop_AllEvents);
        XQ_EQ_I64(g_destroyCount, 0);        /* 循环内级别不够 ⇒ 不兑现 */
        XObject_killTimer((XObject*)driver, id);
    }

    /* exec cleanup 拍：显式点名 DeferredDelete ⇒ 级别放宽到"事件级别 > 当前级别"
     * （事件在 loopLevel=1 时投递，循环退出后 currentLevel=0 ⇒ 兑现）。 */
    XCoreApplication_sendPostedEvents(NULL, XEVENT_TYPE_DEFERRED_DELETE);
    XQ_EQ_I64(g_destroyCount, 1);

    XClassDelete((XClass*)driver);
    XClassDelete((XClass*)observer);
    XClassDelete((XClass*)loop);
}

/** @brief deinitLater 之后再 deleteLater：动作**升级为释放**（不再被吞）。 */
XQ_TEST(XObject, deinitLaterUpgradedByDeleteLater)
{
    XEventLoop* loop = NULL;
    XQAlignProbe* driver = NULL;
    XQAlignProbe* victim = NULL;
    XObject* observer = NULL;

    xqResetCounters();
    loop = XEventLoop_create();
    XQ_REQUIRE(loop != NULL);
    driver = XQAlignProbe_create();
    observer = XObject_create();
    victim = XQAlignProbe_create();
    XQ_REQUIRE(driver != NULL && observer != NULL && victim != NULL);

    XQ_CHECK(XObject_connect((XObject*)victim, XSignal(XObject_destroyed_signal),
                               observer, xqSlot_destroyed,
                               XConnectionType_Auto) != NULL);
    driver->m_loop = loop;
    driver->m_quitAfterHits = 1;
    {
        XTimerId id = XObject_startTimer((XObject*)driver, 10, XTimerType_CoarseTimer);
        XQ_REQUIRE(id != XTIMER_INVALID_ID);
        XObject_deinitLater((XObject*)victim);   /* 先请求"只反初始化" */
        XObject_deleteLater((XObject*)victim);   /* 再请求"释放" ⇒ 升级 */
        g_destroyQuitLoop = loop;
        (void)XEventLoop_exec(loop, XEventLoop_AllEvents);
        XObject_killTimer((XObject*)driver, id);
    }
    g_destroyQuitLoop = NULL;
    XQ_EQ_I64(g_destroyCount, 1);
    /* 升级成功的判据：对象被**释放**（XClassDelete）而不是只反初始化。
     * 若停留在 deinitLater 语义，这块堆内存会留在活表外 ⇒ 运行器泄漏门判
     * [LEAK]（本用例不做 XClassDelete(victim)）。 */

    XClassDelete((XClass*)driver);
    XClassDelete((XClass*)observer);
    XClassDelete((XClass*)loop);
}

/** @brief 发送者栈：接收者析构时清帧，墓碑帧不得被新查询命中（Qt 同口径）。 */
XQ_TEST(XObject, senderFrameClearedOnDestroy)
{
    XObject* receiver = XObject_create();
    XObject* sender = XObject_create();

    XQ_REQUIRE(receiver != NULL && sender != NULL);

    /* 手工压一帧：receiver 是我们的对象，sender 随便一个对象 */
    XThreadData_pushSender(receiver, sender, 42);
    XQ_CHECK(XObject_sender(receiver) == sender);
    XQ_EQ_I64(XObject_senderSignalIndex(receiver), 42);

    /* 接收者析构：帧里的 receiver 被置空（对标 Qt ~QObject 的
     * `currentSender->receiver = nullptr`），此后任何按该地址的查询都不再命中 */
    XClassDelete((XClass*)receiver);
    XQ_CHECK(XObject_sender(receiver) == NULL);
    XQ_EQ_I64(XObject_senderSignalIndex(receiver), -1);
    /* 墓碑帧不清空 sender 以外的东西，也不影响别的对象 */
    XQ_CHECK(XObject_sender(sender) == NULL);

    XThreadData_popSender();   /* 栈深必须仍然配对 */
    XClassDelete((XClass*)sender);
}

/** @brief setObjectName(NULL) = 清空（对标 Qt setObjectName(QString())）。 */
XQ_TEST(XObject, objectNameNullClears)
{
    XObject* obj = XObject_create();
    XString* name = XString_create_utf8("to-clear");

    xqResetCounters();
    XQ_REQUIRE(obj != NULL && name != NULL);

    XQ_CHECK(XObject_connect(obj, XSignal(XObject_objectNameChanged_signal),
                               obj, xqSlot_nameChanged,
                               XConnectionType_Direct) != NULL);
    XObject_setObjectName(obj, name);
    XQ_EQ_I64(g_nameHits, 1);

    /* NULL 清空：Qt 实测清空后 objectName().isNull()==true 且发一次信号 */
    XObject_setObjectName(obj, NULL);
    XQ_CHECK(XObject_objectName(obj) == NULL);
    XQ_EQ_I64(g_nameHits, 2);
    /* 已经是无名：再清空不发（Qt 同值不发） */
    XObject_setObjectName(obj, NULL);
    XQ_EQ_I64(g_nameHits, 2);

    XClassDelete((XClass*)obj);
    XClassDelete((XClass*)name);
}

/**
 * @brief 跨线程 setParent 失败路径（Qt 6.8 qobject.cpp:2269-2275）。
 * @details Qt 先摘旧父（发 ChildRemoved + ParentAboutToChange）、落地 parent，
 *          再判线程；失败时 `parent = nullptr; return;` ⇒ 对象最终**无父**、
 *          不发 ChildAdded / ParentChange。修复前校验在摘旧父之前：对象保留
 *          原父且一条事件都不发，与 Qt 相反。
 * @note 造「不同线程的父对象」用 `XObject_moveToThread(newParent, NULL)`：目标
 *       线程为 NULL 时框架创建一个无亲和性的 threadData（m_thread==NULL），其
 *       threadData 与当前线程必然不同——不需要真的起线程，也没有 XThread 析构
 *       的线程数据账要平（对象析构时 deref 收尾，零泄漏）。
 */
XQ_TEST(XObject, setParentCrossThreadFails)
{
    XObject* oldParent = XObject_create();
    XObject* newParent = XObject_create();
    XQAlignProbe* child = XQAlignProbe_create();

    XQ_REQUIRE(oldParent != NULL && newParent != NULL && child != NULL);

    /* 同线程先挂一次：锁成功路径的事件序（AboutTo + Change 各 1）。 */
    XObject_setParent((XObject*)child, oldParent);
    XQ_CHECK(XObject_parent((XObject*)child) == oldParent);
    XQ_EQ_I64(child->m_parentAboutToChange, 1);
    XQ_EQ_I64(child->m_parentChange, 1);

    /* 新父搬到无亲和性线程数据（未启动的 XThread 同款 threadData 形态）。 */
    XQ_CHECK(XObject_moveToThread(newParent, NULL));
    XQ_CHECK(XObject_threadData(newParent) != XThreadData_current());

    /* 跨线程 setParent：Qt 口径 = 摘旧父 → AboutToChange → 判线程失败 → 无父。 */
    child->m_parentAboutToChange = 0;
    child->m_parentChange = 0;
    XObject_setParent((XObject*)child, newParent);
    XQ_CHECK(XObject_parent((XObject*)child) == NULL);          /* 失败后无父 */
    XQ_EQ_I64(child->m_parentAboutToChange, 1);                 /* AboutToChange 已发 */
    XQ_EQ_I64(child->m_parentChange, 0);                        /* ParentChange 不发 */
    {
        const XVector* ch = XObject_children(oldParent);
        XQ_CHECK(ch == NULL
                 || XContainer_size_base((const XContainer*)ch) == 0); /* 旧父已摘 */
        XQ_CHECK(XObject_children(newParent) == NULL);                /* 新父没挂上 */
    }

    /* child 已从 oldParent 摘除：随 oldParent 析构不会再带走它，须自行释放。 */
    XClassDelete((XClass*)child);
    XClassDelete((XClass*)oldParent);
    XClassDelete((XClass*)newParent);
}

/**
 * @brief killTimer 的归属守卫（Qt 6.8 qobject.cpp:1926-1936）。
 * @details Qt 先查 `runningTimers.indexOf(id)`，不属于本对象的 id 告警拒绝。
 *          修复前 X 只按 id 注销：误传 id 会**静默杀掉别的对象的定时器**。
 */
XQ_TEST(XObject, killTimerOwnershipGuard)
{
    XEventLoop* loop = XEventLoop_create();
    XQAlignProbe* owner = XQAlignProbe_create();
    XObject* intruder = XObject_create();
    XAbstractEventDispatcher* disp;
    XTimerId id;
    XVector* timers;

    XQ_REQUIRE(loop != NULL && owner != NULL && intruder != NULL);
    owner->m_loop = loop;
    owner->m_quitAfterHits = 1;
    id = XObject_startTimer((XObject*)owner, 20, XTimerType_CoarseTimer);
    XQ_REQUIRE(id != XTIMER_INVALID_ID);
    disp = XObject_eventDispatcher((XObject*)owner);
    XQ_REQUIRE(disp != NULL);

    /* ① 别的对象拿这个 id 注销：必须被拒绝，定时器仍在调度器上。 */
    XObject_killTimer(intruder, id);
    timers = XAbstractEventDispatcher_timersForObject_base(disp, (XObject*)owner);
    XQ_CHECK(timers != NULL);
    if (timers) {
        XQ_EQ_SIZE(XContainer_size_base((const XContainer*)timers), 1);
        XClassDelete(timers);
    }

    /* ② 拒绝不等于吞掉：定时器依然触发（退出循环由它自己完成）。 */
    (void)XEventLoop_exec(loop, XEventLoop_AllEvents);
    XQ_CHECK(owner->m_anyTimerHits >= 1);

    /* ③ 归属正确才真正注销。 */
    XObject_killTimer((XObject*)owner, id);
    timers = XAbstractEventDispatcher_timersForObject_base(disp, (XObject*)owner);
    XQ_CHECK(timers == NULL
             || XContainer_size_base((const XContainer*)timers) == 0);
    if (timers) XClassDelete(timers);

    /* ④ 已回收的 id / XTIMER_INVALID_ID：不得崩溃，也不得误伤。 */
    XObject_killTimer(intruder, id);
    XObject_killTimer((XObject*)owner, XTIMER_INVALID_ID);

    XClassDelete((XClass*)owner);
    XClassDelete((XClass*)intruder);
    XClassDelete((XClass*)loop);
}

/**
 * @brief ThreadChange 的定时器迁移（Qt 6.8 qobject.cpp:1422-1443）。
 * @details moveToThread 先给对象发 ThreadChange（此刻定时器还登记在旧线程的
 *          调度器上）：定时器被**摘除但 id 不归还池子**，然后在新线程用显式 id
 *          重注册。本用例在单线程内模拟同一条路径——ThreadChange 交付后立即
 *          派发已投递的 FUNC_RUN（真实场景里它由目标线程的事件循环兑现）：
 *          ① 摘除后 timersForObject 为空、XFd 槽位与归属仍在；
 *          ② 重注册后 timersForObject 回到 1 条且 **id 不变**；
 *          ③ 同一 id 仍触发（m_trackedId 命中）、killTimer 同 id 仍生效；
 *          ④ 迁移态里再收一次 ThreadChange 不会把定时器弄丢/弄重。
 */
XQ_TEST(XObject, moveToThreadMigratesTimers)
{
    XEventLoop* loop = XEventLoop_create();
    XQAlignProbe* probe = XQAlignProbe_create();
    XAbstractEventDispatcher* disp;
    XTimerId id;
    XEvent* change;
    XVector* timers;

    XQ_REQUIRE(loop != NULL && probe != NULL);
    probe->m_loop = loop;
    probe->m_quitAfterHits = 1;
    id = XObject_startTimer((XObject*)probe, 20, XTimerType_CoarseTimer);
    XQ_REQUIRE(id != XTIMER_INVALID_ID);
    probe->m_trackedId = id;
    disp = XObject_eventDispatcher((XObject*)probe);
    XQ_REQUIRE(disp != NULL);

    /* 迁移前：1 条登记。 */
    timers = XAbstractEventDispatcher_timersForObject_base(disp, (XObject*)probe);
    XQ_CHECK(timers != NULL);
    if (timers) {
        XQ_EQ_SIZE(XContainer_size_base((const XContainer*)timers), 1);
        XClassDelete(timers);
    }

    /* ThreadChange：摘除 + 排队「稍后重注册」。 */
    change = XEvent_create(XEVENT_TYPE_THREAD_CHANGE);
    XQ_REQUIRE(change != NULL);
    (void)XCoreApplication_sendEvent((XObject*)probe, change);
    XClassDelete((XEvent*)change);
    timers = XAbstractEventDispatcher_timersForObject_base(disp, (XObject*)probe);
    XQ_CHECK(timers == NULL
             || XContainer_size_base((const XContainer*)timers) == 0);  /* 已摘除 */
    if (timers) XClassDelete(timers);
    XQ_EQ_I64((long long)XFd_type(id), (long long)XFD_TYPE_TIMER);      /* id 未归还池子 */
    XQ_CHECK(XFd_object(id) == (XObject*)probe);                        /* 归属随对象走 */

    /* 迁移态里再收一次 ThreadChange：快照为空 ⇒ 既不重复排队也不误伤。 */
    change = XEvent_create(XEVENT_TYPE_THREAD_CHANGE);
    XQ_REQUIRE(change != NULL);
    (void)XCoreApplication_sendEvent((XObject*)probe, change);
    XClassDelete((XEvent*)change);

    /* 目标线程兑现重注册（单线程模拟）后：回到 1 条，且 id 不变。 */
    XCoreApplication_sendPostedEvents((XObject*)probe, XEVENT_TYPE_FUNC_RUN);
    timers = XAbstractEventDispatcher_timersForObject_base(disp, (XObject*)probe);
    XQ_CHECK(timers != NULL);
    if (timers) {
        XQ_EQ_SIZE(XContainer_size_base((const XContainer*)timers), 1);
        if (XContainer_size_base((const XContainer*)timers) == 1) {
            const XAbstractEventDispatcher_TimerInfoV2* info =
                (const XAbstractEventDispatcher_TimerInfoV2*)XContainerDataAddr(timers);
            XQ_CHECK(info != NULL);
            if (info)
                XQ_EQ_I64((long long)info->timerId, (long long)id);
        }
        XClassDelete(timers);
    }

    /* 同 id 仍触发；killTimer 用同一 id 仍生效。 */
    (void)XEventLoop_exec(loop, XEventLoop_AllEvents);
    XQ_EQ_I64(probe->m_timerEvents, 1);
    XObject_killTimer((XObject*)probe, id);
    timers = XAbstractEventDispatcher_timersForObject_base(disp, (XObject*)probe);
    XQ_CHECK(timers == NULL
             || XContainer_size_base((const XContainer*)timers) == 0);
    if (timers) XClassDelete(timers);

    XClassDelete((XClass*)probe);
    XClassDelete((XClass*)loop);
}
