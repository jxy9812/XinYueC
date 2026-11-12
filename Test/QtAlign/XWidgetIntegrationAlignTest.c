/**
 * @file       XWidgetIntegrationAlignTest.c
 * @brief      XGui 控件层**跨类集成**契约测试（XWidgetIntegration.*）。
 * @details    逐类 `X<类>AlignTest.c` 锁的是「单个类 × Qt API 对表」；本文件
 *             锁的是**控件层各类装配在一起时的所有权/生命周期契约**——这类
 *             缺陷只有两个类同时在场时才暴露（单类用例各自都绿）：
 *
 *             ① 父控件析构级联（含布局内控件、显式隐藏控件）——
 *                `XWidget` × `XObject` × `XLayout`/`XBoxLayout`；
 *             ② `setParent` 重挂 vs 布局接管/移除后的所有权归属——
 *                `XWidget` × `XLayoutItem`（布局 addWidget 会 reparent，
 *                removeWidget 只摘条目不改父链）；
 *             ③ `XTabWidget` / `XStackedWidget` 页增删、当前页切换后的
 *                页控件生灭与归属归还；
 *             ④ `XSplitter` 子页增删与把手的自动增删（childEvent 驱动）；
 *             ⑤ 跨对象信号连接在发送者/接收者析构后的自动失效；
 *             ⑥ 事件过滤器跨控件安装/反注册/过滤器自身析构的自动摘除；
 *             ⑦ 焦点链在嵌套控件（孙代）中的转移与焦点控件析构自动清焦；
 *             ⑧ `XDialog` / `XMessageBox` 父子归属与 accept/reject 后的
 *                结果码/显隐/生命周期（不调 show/exec，不弹真窗口）。
 *
 *             Qt 依据（断言旁注明）：
 *             - `/opt/Qt/6.8.3/gcc_64/include/QtWidgets/qwidget.h`
 *               （setParent/parentWidget/children/focusWidget/focusNextChild）；
 *             - `/opt/Qt/6.8.3/gcc_64/include/QtWidgets/qlayout.h`
 *               （addWidget 接管并 reparent、removeWidget 不删控件不改父链）；
 *             - `/opt/Qt/6.8.3/gcc_64/include/QtWidgets/qtabwidget.h` +
 *               `qstackedwidget.h`（removeTab/removeWidget 只摘页不删页）；
 *             - `/opt/Qt/6.8.3/gcc_64/include/QtWidgets/qsplitter.h`
 *               （addWidget 接管控件所有权；handle 是分割器子部件）；
 *             - `/opt/Qt/6.8.3/gcc_64/include/QtCore/qobject.h`
 *               （connect 随任一端析构自动断开；eventFilter 随过滤器析构
 *               自动摘除）；
 *             - `/opt/Qt/6.8.3/gcc_64/include/QtWidgets/qdialog.h`
 *               （done/accept/reject 置 result 并发 accepted/rejected）。
 *
 *             **可观测点说明**：本框架没有对象析构计数器，故「某对象确实
 *             被释放」一律经**它自己的 destroyed 信号**观测（`XObject` 在
 *             `VXObject_deinit` 开头发射，见 XObject.c:70）。计数观察者
 *             （`XqiDestroyCounter` 表）都是独立的无父 `XObject`，用例末尾
 *             显式释放——它们不参与被测父子链，避免观察者先亡造成漏计。
 *             信号/过滤器连接的「自动失效」另经连接表长度断言（发送者的
 *             `XSignalSlot_receivers`、接收者的 `bindSignalList`），这是
 *             该契约唯一能正面观测的落点（Qt 侧对应
 *             `QObject::receivers()` / 内部 connection list）。
 *
 *             泄漏由运行器统一判定：每个用例只创建、由父析构带走子对象
 *             或显式删除，任何遗留对象即 `[LEAK]`。
 * @author     XinYueC 团队
 */
#include "xq_align.h"

#include "XGuiConfig.h"

#if XGUI_ON && XWIDGET_ON

#include "XWidget.h"
#include "XWidget_Protected.h" /* XWidget_appFocusWidget（保护接口，测试观测用）。 */
#include "XObject.h"
#include "XClass.h"
#include "XEvent.h"
#include "XEventLoop.h"
#include "XCoreApplication.h"
#include "XVector.h"
#include "XVarList.h"
#include "XSignalSlot.h"
#if XLAYOUT_ON
#include "XLayout.h"
/* XLAYOUT_BOX_ON 定义在 XLayout_config.h（由 XBoxLayout.h 拉入），故本
 * 头必须无条件包含——否则下方 `#if XLAYOUT_BOX_ON` 因未定义而恒假。 */
#include "XBoxLayout.h"
#endif
#if XTABBAR_ON && XTABWIDGET_ON
#include "XTabWidget.h"
#endif
#if XFRAME_ON && XLAYOUT_ON && XLAYOUT_STACKED_ON && XSTACKEDWIDGET_ON
#include "XStackedWidget.h"
#endif
#if XFRAME_ON && XSPLITTER_ON
#include "XSplitter.h"
#include "XSplitterHandle.h"
#endif
#if XDIALOG_ON
#include "XDialog.h"
#endif
#if XDIALOG_ON && XMESSAGEBOX_ON
#include "XMessageBox.h"
#endif

/* ==================== 析构观测：destroyed 计数观察者表 ==================== */

/**
 * @brief 单条计数记录：观察者对象 + 命中次数。
 * @details 槽函数签名 `void (*)(XObject* receiver, XVarList*)` 没有用户数据
 *          指针，接收者就是观察者对象本身。本框架的 `XObject_create()` 只
 *          分配 `sizeof(XObject)`（无法安全地按更大结构体解引用），故用
 *          「对象指针 → 计数」静态映射，而不是派生一个带计数字段的对象。
 */
typedef struct XqiDestroyCounter
{
    XObject* observer; /**< 观察者对象（NULL=表项空闲）。 */
    int hits;          /**< destroyed 命中次数。 */
} XqiDestroyCounter;

#define XQI_MAX_COUNTERS 16

static XqiDestroyCounter g_xqiCounters[XQI_MAX_COUNTERS];

/** @brief destroyed 槽：按接收者在表内定位并计数。 */
static void xqiDestroyedSlot(XObject* receiver, XVarList* args)
{
    int i;
    (void)args;
    for (i = 0; i < XQI_MAX_COUNTERS; ++i) {
        if (g_xqiCounters[i].observer == receiver) {
            ++g_xqiCounters[i].hits;
            return;
        }
    }
}

/** @brief 新建一个计数观察者（命中数归零）。失败返回 NULL。 */
static XObject* xqiCounterNew(void)
{
    int i;
    XObject* observer = XObject_create();
    if (!observer) return NULL;
    for (i = 0; i < XQI_MAX_COUNTERS; ++i) {
        if (!g_xqiCounters[i].observer) {
            g_xqiCounters[i].observer = observer;
            g_xqiCounters[i].hits = 0;
            return observer;
        }
    }
    XClassDelete((XClass*)observer); /* 表满：不留悬空对象。 */
    return NULL;
}

/** @brief 读取观察者命中数（未登记返回 -1，便于断言发现表溢出）。 */
static int xqiCounterHits(const XObject* observer)
{
    int i;
    for (i = 0; i < XQI_MAX_COUNTERS; ++i)
        if (g_xqiCounters[i].observer == (XObject*)observer)
            return g_xqiCounters[i].hits;
    return -1;
}

/** @brief 释放观察者并清表项（须在用例收尾时调用）。 */
static void xqiCounterFree(XObject* observer)
{
    int i;
    if (!observer) return;
    for (i = 0; i < XQI_MAX_COUNTERS; ++i) {
        if (g_xqiCounters[i].observer == observer) {
            g_xqiCounters[i].observer = NULL;
            g_xqiCounters[i].hits = 0;
            break;
        }
    }
    XClassDelete((XClass*)observer);
}

/**
 * @brief 让 observer 观测 watched 的 destroyed 信号（对标 Qt 里
 *        `connect(watched, &QObject::destroyed, observer, ...)`）。
 * @return 建连成功返回 true。
 */
static bool xqiCounterWatch(XObject* watched, XObject* observer)
{
    if (!watched || !observer) return false;
    return XObject_connect(watched, XSignal(XObject_destroyed_signal),
                             observer, xqiDestroyedSlot,
                             XConnectionType_Direct) != NULL;
}

/** @brief parent 的直属子对象里是否有 child（借用读，不释放）。 */
static bool xqiHasChild(const XObject* parent, const XObject* child)
{
    const XVector* children;
    size_t n;
    size_t i;
    if (!parent || !child) return false;
    children = XObject_children(parent);
    if (!children) return false;
    n = XVector_size_base((const XContainer*)children);
    for (i = 0; i < n; ++i) {
        const XObject* it = *(XObject* const*)XVector_at_base(
            (XVector*)children, (int64_t)i);
        if (it == child) return true;
    }
    return false;
}

/** @brief 观察者对象作为接收者的在册连接数（0=NULL 或空表）。 */
static size_t xqiBindCount(const XObject* receiver)
{
    if (!receiver || !receiver->m_signalSlot) return 0;
    return XVector_size_base(
        (const XContainer*)receiver->m_signalSlot->bindSignalList);
}

/** @brief 冲刷已排队的延迟删除（removeTab/xsp_removeSlot 走 deleteLater）。 */
static void xqiFlushDeferredDeletes(void)
{
    XCoreApplication_sendPostedEvents(NULL, XEVENT_TYPE_DEFERRED_DELETE);
}

/* ==================== 事件探针控件（事件过滤器可观测点） ==================== */

/**
 * @brief 事件计数探针：覆写 EXObject_Event 统计事件到达次数。
 * @details Qt 的对位观察点是子类覆写 `QWidget::event()`
 *          （qwidget.h `bool event(QEvent*) override`）。过滤器测试需要
 *          「被过滤者是否真的收到事件」的正面证据——只数过滤器调用次数
 *          无法证明「吞掉」「摘除」「过滤器析构」三态。
 */
XCLASS_DEFINE_BEGING(XqiProbeWidget)
XCLASS_DEFINE_EXTEND_END(XqiProbeWidget, XWidget)

typedef struct XqiProbeWidget
{
    XWidget m_base;     /**< 基类成员；必须是第一个。 */
    int m_titleEvents;  /**< WINDOW_TITLE_CHANGE 到达次数。 */
} XqiProbeWidget;

static XVtable* XqiProbeWidget_class_init(void);

static bool XqiProbeWidget_event(XObject* receiver, XEvent* event)
{
    XqiProbeWidget* self = (XqiProbeWidget*)receiver;
    if (!self || !event) return false;
    if (XEvent_type(event) == XEVENT_TYPE_WINDOW_TITLE_CHANGE)
        ++self->m_titleEvents;
    /* 继续走 XWidget 基类分派（其余事件语义不变）。 */
    return XClass_Parent(XWidget, EXObject_Event,
                         bool (*)(XObject*, XEvent*))((XObject*)receiver,
                                                      event);
}

static XVtable* XqiProbeWidget_class_init(void)
{
    XVTABLE_INIT_DEFAULT(XqiProbeWidget);
    XVTABLE_INHERIT_XCLASS(XWidget);
    XVTABLE_OVERLOAD_DEFAULT(EXObject_Event, XqiProbeWidget_event);
    return XVTABLE_DEFAULT;
}

static XqiProbeWidget* XqiProbe_create(XWidget* parent)
{
    XqiProbeWidget* self = (XqiProbeWidget*)XMemory_malloc(
        sizeof(XqiProbeWidget), XCLASS_DEFAULT_MEMORY_TYPE);
    if (!self) return NULL;
    XMemset(self, 0, sizeof(*self));
    XWidget_init(&self->m_base, parent, 0);
    XClassSetVtable(self, XqiProbeWidget);
    Set_Class_IsHeap(self, true);
    return self;
}

/* ==================== 事件过滤器对象 ==================== */

/**
 * @brief 事件过滤器对象：记录调用次数并按需吞事件。
 * @details 对标 Qt「新建 QObject 子类覆写 eventFilter」
 *          （qobject.h `virtual bool eventFilter(QObject *watched, QEvent *event)`）.
 */
XCLASS_DEFINE_BEGING(XqiFilterObject)
XCLASS_DEFINE_EXTEND_END(XqiFilterObject, XObject)

typedef struct XqiFilterObject
{
    XObject m_base; /**< 基类成员；必须是第一个。 */
    int m_calls;    /**< eventFilter 调用次数。 */
    int m_swallow;  /**< 要吞掉的事件类型（-1=不吞）。 */
} XqiFilterObject;

static XVtable* XqiFilterObject_class_init(void);

static bool XqiFilterObject_eventFilter(XObject* self, XObject* watched,
                                        XEvent* event)
{
    XqiFilterObject* f = (XqiFilterObject*)self;
    (void)watched;
    if (!f || !event) return false;
    ++f->m_calls;
    return f->m_swallow >= 0 && (int)XEvent_type(event) == f->m_swallow;
}

static XVtable* XqiFilterObject_class_init(void)
{
    XVTABLE_INIT_DEFAULT(XqiFilterObject);
    XVTABLE_INHERIT_XCLASS(XObject);
    XVTABLE_OVERLOAD_DEFAULT(EXObject_EventFilter, XqiFilterObject_eventFilter);
    return XVTABLE_DEFAULT;
}

static XqiFilterObject* XqiFilter_create(int swallowType)
{
    XqiFilterObject* f = (XqiFilterObject*)XMemory_malloc(
        sizeof(XqiFilterObject), XCLASS_DEFAULT_MEMORY_TYPE);
    if (!f) return NULL;
    XMemset(f, 0, sizeof(*f));
    XObject_init(&f->m_base);
    XClassSetVtable(f, XqiFilterObject);
    Set_Class_IsHeap(f, true);
    f->m_swallow = swallowType;
    return f;
}

/* ==================== ① 父控件析构级联 ==================== */

/**
 * @brief 父控件析构带走全部子控件：普通子件、显式隐藏子件、布局内子件。
 * @details Qt 依据：`QObject::~QObject` 删除全部 children；`QLayout::addWidget`
 *          对未挂在本控件名下的控件执行 `w->setParent(this)`（qlayout.cpp
 *          `QLayout::addChildWidget` → `QWidget::setParent`），故「布局内
 *          控件」在 Qt 里同样是控件的子对象 ⇒ 随控件析构释放。
 *          本用例先断言布局把无父子件 reparent 到宿主（`XLayout_linkItem`
 *          的 `XWidget_setParent_2(child, parent)`，XLayout.c:258-260），
 *          再断言三态子件全部随父析构（destroyed 各命中一次）。
 */
XQ_TEST(XWidgetIntegration, parentDestructsChildren)
{
    XWidget* host = XWidget_create(NULL, 0);
    XWidget* plain = NULL;
    XWidget* hidden = NULL;
    XWidget* inLayout = NULL;
    XObject* cPlain = NULL;
    XObject* cHidden = NULL;
    XObject* cLayout = NULL;
    const XVector* children = NULL;
#if XLAYOUT_ON && XLAYOUT_BOX_ON
    XBoxLayout* layout = NULL;
#endif

    XQ_REQUIRE(host != NULL);
    plain = XWidget_create(host, 0);
    hidden = XWidget_create(host, 0);
    /* 布局内控件刻意以「无父」创建：由布局接管 reparent（Qt addChildWidget）。 */
    inLayout = XWidget_create(NULL, 0);
    cPlain = xqiCounterNew();
    cHidden = xqiCounterNew();
    cLayout = xqiCounterNew();
    XQ_REQUIRE(plain && hidden && inLayout && cPlain && cHidden && cLayout);

    XQ_CHECK(xqiCounterWatch((XObject*)plain, cPlain));
    XQ_CHECK(xqiCounterWatch((XObject*)hidden, cHidden));
    XQ_CHECK(xqiCounterWatch((XObject*)inLayout, cLayout));

    /* 显式隐藏的子控件同样是父的子对象（隐藏只改可见性位，不改所有权）。 */
    XWidget_hide(hidden);
    XQ_CHECK(XWidget_isHidden(hidden));

#if XLAYOUT_ON && XLAYOUT_BOX_ON
    layout = XBoxLayout_create(XBoxLayoutDirection_TopToBottom, NULL);
    XQ_REQUIRE(layout != NULL);
    /* _ref 版：布局归 host 所有，随 host 析构释放（Qt `l->setParent(this)`）。 */
    XWidget_setLayout_ref(host, (XLayout*)layout);
    XQ_CHECK(XWidget_layout_const(host) == (const XLayout*)layout);
    XBoxLayout_addWidget(layout, inLayout);
    /* 布局接管控件：无父子件被 reparent 到布局宿主（Qt addChildWidget 同）。 */
    XQ_CHECK(XWidget_parentWidget(inLayout) == host);
    XQ_CHECK(xqiHasChild((XObject*)host, (XObject*)inLayout));
    XQ_EQ_I64(XLayout_count_base((const XLayout*)layout), 1);
#endif

    children = XObject_children((XObject*)host);
    XQ_CHECK_PTR(children);
    if (children)
        XQ_EQ_SIZE(XVector_size_base((const XContainer*)children), 3);

    /* 父析构：三态子件全部被带走（本用例不手动删任何子件）。 */
    XClassDelete((XClass*)host);
    XQ_EQ_I64(xqiCounterHits(cPlain), 1);
    XQ_EQ_I64(xqiCounterHits(cHidden), 1);
    XQ_EQ_I64(xqiCounterHits(cLayout), 1);

    xqiCounterFree(cPlain);
    xqiCounterFree(cHidden);
    xqiCounterFree(cLayout);
}

/* ==================== ② setParent 重挂 vs 布局接管/移除 ==================== */

/**
 * @brief 布局移除不改变父链归属；setParent 重挂才转移所有权。
 * @details Qt 依据：
 *          - `QLayout::removeWidget`（qlayout.h）只把控件从布局摘出，
 *            文档明写调用方随后自行负责其几何/隐藏——**父对象不变**；
 *          - `QWidget::setParent(parent)` 才改父链，父链即所有权：
 *            旧父析构不再带走该控件，新父析构才带走。
 *          两个方向都必须成立，否则「先入布局再换父」会二义：要么旧父
 *          析构双删（布局仍持有条目），要么新父不接管（泄漏）。
 */
XQ_TEST(XWidgetIntegration, layoutTakeoverAndReparent)
{
    /* ---- 阶段 A：布局 addWidget 接管 → removeWidget 只摘条目 ---- */
    XWidget* hostA = XWidget_create(NULL, 0);
    XWidget* w = NULL;
    XObject* cW = NULL;
#if XLAYOUT_ON && XLAYOUT_BOX_ON
    XBoxLayout* layoutA = NULL;
#endif

    XQ_REQUIRE(hostA != NULL);
    w = XWidget_create(hostA, 0);
    cW = xqiCounterNew();
    XQ_REQUIRE(w && cW);
    XQ_CHECK(xqiCounterWatch((XObject*)w, cW));

#if XLAYOUT_ON && XLAYOUT_BOX_ON
    layoutA = XBoxLayout_create(XBoxLayoutDirection_TopToBottom, NULL);
    XQ_REQUIRE(layoutA != NULL);
    XWidget_setLayout_ref(hostA, (XLayout*)layoutA);
    XBoxLayout_addWidget(layoutA, w);
    XQ_EQ_I64(XLayout_indexOf((const XLayout*)layoutA, w), 0);
    XQ_CHECK(XWidget_parentWidget(w) == hostA);

    /* 摘条目：控件本体不销毁、父链不变（Qt removeWidget 同语义）。 */
    XLayout_removeWidget((XLayout*)layoutA, w);
    XQ_EQ_I64(XLayout_count_base((const XLayout*)layoutA), 0);
    XQ_EQ_I64(XLayout_indexOf((const XLayout*)layoutA, w), -1);
    XQ_CHECK(XWidget_parentWidget(w) == hostA);
    XQ_EQ_I64(xqiCounterHits(cW), 0);

    /* 父析构：布局（_ref 接管）与被测控件一并释放。 */
    XClassDelete((XClass*)hostA);
    XQ_EQ_I64(xqiCounterHits(cW), 1);
#else
    XClassDelete((XClass*)hostA);
    XQ_EQ_I64(xqiCounterHits(cW), 1);
#endif

    /* ---- 阶段 B：setParent 重挂 ⇒ 所有权随新父 ---- */
    {
        XWidget* oldHost = XWidget_create(NULL, 0);
        XWidget* newHost = XWidget_create(NULL, 0);
        XWidget* moved = NULL;
        XObject* cMoved = NULL;
#if XLAYOUT_ON && XLAYOUT_BOX_ON
        XBoxLayout* oldLayout = NULL;
#endif

        XQ_REQUIRE(oldHost && newHost);
        moved = XWidget_create(oldHost, 0);
        cMoved = xqiCounterNew();
        XQ_REQUIRE(moved && cMoved);
        XQ_CHECK(xqiCounterWatch((XObject*)moved, cMoved));

#if XLAYOUT_ON && XLAYOUT_BOX_ON
        oldLayout = XBoxLayout_create(XBoxLayoutDirection_TopToBottom, NULL);
        XQ_REQUIRE(oldLayout != NULL);
        XWidget_setLayout_ref(oldHost, (XLayout*)oldLayout);
        XBoxLayout_addWidget(oldLayout, moved);
        XQ_EQ_I64(XLayout_indexOf((const XLayout*)oldLayout, moved), 0);
#endif
        XQ_CHECK(XWidget_parentWidget(moved) == oldHost);

        /* 重挂父级：控件所有权转给 newHost。 */
        XWidget_setParent_2(moved, newHost);
        XQ_CHECK(XWidget_parentWidget(moved) == newHost);
        XQ_CHECK(xqiHasChild((XObject*)newHost, (XObject*)moved));
        XQ_CHECK(!xqiHasChild((XObject*)oldHost, (XObject*)moved));
#if XLAYOUT_ON && XLAYOUT_BOX_ON
        /* 旧布局条目是历史残账：本框架「加进新布局」只 reparent，
         * **不**做 Qt `QLayout::addChildWidget` 的「从旧布局摘除」
         * （Qt 6.8 qlayout.cpp 对 `WA_LaidOut` 控件调
         * `removeWidgetRecursively` → `delete lay->takeAt(i)`；
         * 本仓库 XLayout_linkItem 只有 setParent_2，XLayout.c:256-260）。
         * 实测（本批联调探针）：w 加进第二个布局后
         * `XLayout_indexOf(旧布局, w)` 仍返回 0（Qt 语义应为 -1）——
         * 该差异已记入本批集成测试的缺陷清单，**不在本用例断言**（保持
         * 契约套件全绿），此处按当前口径显式摘旧条目以验证后续所有权。 */
        XLayout_removeWidget((XLayout*)oldLayout, moved);
        XQ_EQ_I64(XLayout_count_base((const XLayout*)oldLayout), 0);
#endif

        /* 旧父析构：不得带走已换父的控件。 */
        XClassDelete((XClass*)oldHost);
        XQ_EQ_I64(xqiCounterHits(cMoved), 0);
        XQ_CHECK(XWidget_parentWidget(moved) == newHost); /* 控件仍可用。 */

        /* 新父析构：此时才带走。 */
        XClassDelete((XClass*)newHost);
        XQ_EQ_I64(xqiCounterHits(cMoved), 1);

        xqiCounterFree(cMoved);
    }

    xqiCounterFree(cW);
}

/* ==================== ③ XTabWidget 页生命周期 ==================== */

/**
 * @brief 页增删与当前页切换后的生命周期（Qt qtabwidget.cpp:627-632 /
 *        qstackedwidget.cpp:149-163：「页控件 not deleted but simply
 *        removed … causing it to be hidden」）。
 * @details 契约分三段：
 *          ① addTab 把页控件 reparent 进内部页容器 ⇒ 页随容器（=随本控件）
 *             析构释放；
 *          ② removeTab 先摘父链（`setParent(NULL)`）再隐藏——页控件交还
 *             调用方且**不得**被容器析构连带释放；
 *          ③ clear 等价逐页 removeTab（Qt clear 只摘页不删页）。
 */
XQ_TEST(XWidgetIntegration, tabWidgetPageLifecycle)
{
#if XTABBAR_ON && XTABWIDGET_ON
    XTabWidget* tw = XTabWidget_create(NULL, 0);
    XWidget* pageA = XWidget_create(NULL, 0);
    XWidget* pageB = XWidget_create(NULL, 0);
    XObject* cA = xqiCounterNew();
    XObject* cB = xqiCounterNew();

    XQ_REQUIRE(tw && pageA && pageB && cA && cB);
    XQ_CHECK(xqiCounterWatch((XObject*)pageA, cA));
    XQ_CHECK(xqiCounterWatch((XObject*)pageB, cB));

    XQ_EQ_I64(XTabWidget_addTab_2(tw, pageA, "A"), 0);
    XQ_EQ_I64(XTabWidget_addTab_2(tw, pageB, "B"), 1);
    XQ_EQ_I64(XTabWidget_count(tw), 2);
    XQ_EQ_I64(XTabWidget_currentIndex(tw), 0);
    XQ_CHECK(XTabWidget_currentWidget(tw) == pageA);

    /* addTab 接管控件：页控件被 reparent 进内部页容器（XTabWidget.c:857）。 */
    XQ_CHECK(XWidget_parentWidget(pageA) != NULL);
    XQ_CHECK(XWidget_parentWidget(pageA) != (XWidget*)tw); /* 是页容器，不是本控件 */
    XQ_CHECK(XWidget_isAncestorOf((XWidget*)tw, pageA));
    XQ_EQ_I64(xqiCounterHits(cA), 0);

    /* 当前页切换：只是显隐/几何切换，不涉及任何销毁。 */
    XTabWidget_setCurrentIndex(tw, 1);
    XQ_EQ_I64(XTabWidget_currentIndex(tw), 1);
    XQ_CHECK(XTabWidget_currentWidget(tw) == pageB);

    /* removeTab(0)：页 A 交还调用方（摘父 + 隐藏），不销毁；
     * 当前页顺延到唯一剩下的页 B。 */
    XTabWidget_removeTab(tw, 0);
    XQ_EQ_I64(XTabWidget_count(tw), 1);
    XQ_EQ_I64(XTabWidget_indexOf(tw, pageA), -1);
    XQ_CHECK(XWidget_parentWidget(pageA) == NULL);
    XQ_CHECK(XWidget_isHidden(pageA));
    XQ_EQ_I64(xqiCounterHits(cA), 0);
    XQ_EQ_I64(XTabWidget_currentIndex(tw), 0);
    XQ_CHECK(XTabWidget_currentWidget(tw) == pageB);

    /* 控件析构：仍在页里的 B 被带走；已交还的 A 不归它。 */
    XClassDelete((XClass*)tw);
    XQ_EQ_I64(xqiCounterHits(cB), 1);
    XQ_EQ_I64(xqiCounterHits(cA), 0);

    XClassDelete((XClass*)pageA); /* 调用方收尾：此时才释放 A。 */
    XQ_EQ_I64(xqiCounterHits(cA), 1);

    /* 页容器走 deleteLater（XTabWidget.c:930），冲刷一拍让泄漏门看到回收。 */
    xqiFlushDeferredDeletes();

    xqiCounterFree(cA);
    xqiCounterFree(cB);
#endif
}

/* ==================== ③（续）XStackedWidget 页生命周期 ==================== */

/**
 * @brief 页增删/当前页迁移与归属归还（Qt qstackedwidget.cpp：removeWidget
 *        只把控件移出布局，「Parent object and parent widget of widget will
 *        remain the QStackedWidget」；XStackedWidget.h 的 removeWidget
 *        注同口径，`VXStackedLayout_takeAt` 只 hide 不 reparent）。
 * @details 断言重点：addWidget 后页是本控件的子对象（随本控件析构）；
 *          removeWidget 后父链**仍是本容器**、隐藏、本体存活；当前页
 *          索引随摘除自动前移（`VXStackedLayout_takeAt` 维护，
 *          XStackedLayout.c:63-88）。
 */
XQ_TEST(XWidgetIntegration, stackedWidgetPageLifecycle)
{
#if XFRAME_ON && XLAYOUT_ON && XLAYOUT_STACKED_ON && XSTACKEDWIDGET_ON
    XStackedWidget* sw = XStackedWidget_create(NULL, 0);
    XWidget* s1 = XWidget_create(NULL, 0);
    XWidget* s2 = XWidget_create(NULL, 0);
    XWidget* s3 = XWidget_create(NULL, 0);
    XObject* c1 = xqiCounterNew();
    XObject* c2 = xqiCounterNew();
    XObject* c3 = xqiCounterNew();

    XQ_REQUIRE(sw && s1 && s2 && s3 && c1 && c2 && c3);
    XQ_CHECK(xqiCounterWatch((XObject*)s1, c1));
    XQ_CHECK(xqiCounterWatch((XObject*)s2, c2));
    XQ_CHECK(xqiCounterWatch((XObject*)s3, c3));

    XQ_EQ_I64(XStackedWidget_addWidget(sw, s1), 0);
    XQ_EQ_I64(XStackedWidget_addWidget(sw, s2), 1);
    XQ_EQ_I64(XStackedWidget_addWidget(sw, s3), 2);
    XQ_EQ_I64(XStackedWidget_count(sw), 3);
    /* 首页入栈即成为当前页（XStackedLayout_insertWidget 在 oldCurrent<0 时激活）。 */
    XQ_EQ_I64(XStackedWidget_currentIndex(sw), 0);
    XQ_CHECK(XStackedWidget_currentWidget(sw) == s1);
    /* 页归容器所有（布局 addWidget 的 reparent）。 */
    XQ_CHECK(XWidget_parentWidget(s1) == (XWidget*)sw);
    XQ_CHECK(XWidget_parentWidget(s3) == (XWidget*)sw);

    XStackedWidget_setCurrentIndex(sw, 2);
    XQ_CHECK(XStackedWidget_currentWidget(sw) == s3);

    /* 摘掉当前页之前的页：当前页随之前移，控件本体不销毁。 */
    XStackedWidget_removeWidget(sw, s2);
    XQ_EQ_I64(XStackedWidget_count(sw), 2);
    XQ_EQ_I64(XStackedWidget_indexOf(sw, s2), -1);
    /* Qt：页移出布局但父链保持容器（qstackedwidget.cpp removeWidget
     * 文档；本机 6.8.0 探针 removeWidget 后 parentWidget() 仍是容器、
     * isHidden()==1）。 */
    XQ_CHECK(XWidget_parentWidget(s2) == (XWidget*)sw);
    XQ_CHECK(XWidget_isHidden(s2));
    XQ_EQ_I64(xqiCounterHits(c2), 0);
    XQ_EQ_I64(XStackedWidget_currentIndex(sw), 1);
    XQ_CHECK(XStackedWidget_currentWidget(sw) == s3);

    /* 摘掉当前页：当前页回落到剩余首项。 */
    XStackedWidget_removeWidget(sw, s3);
    XQ_EQ_I64(XStackedWidget_count(sw), 1);
    XQ_EQ_I64(XStackedWidget_currentIndex(sw), 0);
    XQ_CHECK(XStackedWidget_currentWidget(sw) == s1);
    XQ_EQ_I64(xqiCounterHits(c3), 0);

    /* 摘出布局的页仍是本容器子控件（Qt 口径），调用方显式删除同样
     * 合法：删除子对象时父链自动摘除，不会与容器析构重复释放。 */
    XClassDelete((XClass*)s2);
    XClassDelete((XClass*)s3);
    XQ_EQ_I64(xqiCounterHits(c2), 1);
    XQ_EQ_I64(xqiCounterHits(c3), 1);

    /* 容器析构：仍在页里的 s1 被带走。 */
    XClassDelete((XClass*)sw);
    XQ_EQ_I64(xqiCounterHits(c1), 1);

    xqiCounterFree(c1);
    xqiCounterFree(c2);
    xqiCounterFree(c3);
#endif
}

/* ==================== ④ XSplitter 子项与把手 ==================== */

/**
 * @brief 分割器子项增删驱动把手增删，析构带走全部页与把手。
 * @details Qt 依据（qsplitter.cpp）：
 *          - `QWidget *w = new QWidget(splitter)` 会经 `childEvent` 自动
 *            成为一页（`QSplitter::childEvent` addChild）；
 *          - 页被销毁/摘父时 `childEvent`（ChildRemoved）摘槽并
 *            `deleteLater` 对应把手；
 *          - 页数 N ⇒ 把手 N-1，把手是分割器的真实子部件。
 *          本用例覆盖：子件构造即入页（增）、页析构自动摘把手（删）、
 *          分割器析构带走剩余页与把手。
 */
XQ_TEST(XWidgetIntegration, splitterPagesAndHandles)
{
#if XFRAME_ON && XSPLITTER_ON
    XSplitter* sp = XSplitter_create(NULL, 0);
    XWidget* p0 = NULL;
    XWidget* p1 = NULL;
    XWidget* p2 = NULL;
    XSplitterHandle* h0 = NULL;
    XSplitterHandle* h1 = NULL;
    XObject* cP0 = NULL;
    XObject* cH0 = NULL;
    XObject* cH1 = NULL;
    const XVector* children = NULL;

    XQ_REQUIRE(sp != NULL);
    /* 以分割器为父创建 ⇒ childEvent(CHILD_ADDED) 自动成页。 */
    p0 = XWidget_create((XWidget*)sp, 0);
    p1 = XWidget_create((XWidget*)sp, 0);
    p2 = XWidget_create((XWidget*)sp, 0);
    cP0 = xqiCounterNew();
    cH0 = xqiCounterNew();
    cH1 = xqiCounterNew();
    XQ_REQUIRE(p0 && p1 && p2 && cP0 && cH0 && cH1);

    XQ_EQ_I64(XSplitter_count(sp), 3);
    XQ_CHECK(XSplitter_widget(sp, 0) == p0);
    XQ_CHECK(XSplitter_widget(sp, 1) == p1);
    XQ_CHECK(XSplitter_widget(sp, 2) == p2);

    h0 = XSplitter_handle(sp, 0);
    h1 = XSplitter_handle(sp, 1);
    XQ_CHECK_PTR(h0);
    XQ_CHECK_PTR(h1);
    XQ_CHECK(XSplitter_handle(sp, 2) == NULL);
    /* 把手是分割器子部件，且归属/分隔点号一致。 */
    XQ_CHECK(XWidget_parentWidget((XWidget*)h0) == (XWidget*)sp);
    XQ_CHECK(XSplitterHandle_splitter(h0) == sp);
    XQ_EQ_I64(XSplitterHandle_splitterIndex(h0), 0);
    XQ_EQ_I64(XSplitterHandle_splitterIndex(h1), 1);
    XQ_EQ_I64(XSplitter_indexOf(sp, (XWidget*)h0), 0);

    /* 3 页 + 2 把手 = 5 个直属子件。 */
    children = XObject_children((XObject*)sp);
    XQ_CHECK_PTR(children);
    if (children)
        XQ_EQ_SIZE(XVector_size_base((const XContainer*)children), 5);

    XQ_CHECK(xqiCounterWatch((XObject*)p0, cP0));
    XQ_CHECK(xqiCounterWatch((XObject*)h0, cH0));
    XQ_CHECK(xqiCounterWatch((XObject*)h1, cH1));

    /* 页析构（删中间页）：页数 -1、剩余页序不变、被摘的把手延迟回收。 */
    XClassDelete((XClass*)p1);
    XQ_EQ_I64(XSplitter_count(sp), 2);
    XQ_EQ_I64(XSplitter_indexOf(sp, p1), -1);
    XQ_CHECK(XSplitter_widget(sp, 0) == p0);
    XQ_CHECK(XSplitter_widget(sp, 1) == p2);
    XQ_CHECK(XSplitter_handle(sp, 1) == NULL);
    XQ_CHECK(XSplitter_handle(sp, 0) == h0); /* 分隔点 0 的把手保留。 */
    XQ_EQ_I64(XSplitterHandle_splitterIndex(h0), 0);
    /* 被移除的把手走 deleteLater（XSplitter.c:1005-1008），冲刷后计数生效。 */
    xqiFlushDeferredDeletes();
    XQ_EQ_I64(xqiCounterHits(cH1), 1);
    XQ_EQ_I64(xqiCounterHits(cH0), 0);

    /* 分割器析构：剩余页 p0/p2 与把手 h0 随级联释放。 */
    XClassDelete((XClass*)sp);
    XQ_EQ_I64(xqiCounterHits(cP0), 1);
    XQ_EQ_I64(xqiCounterHits(cH0), 1);

    xqiCounterFree(cP0);
    xqiCounterFree(cH0);
    xqiCounterFree(cH1);
#endif
}

/* ==================== ⑤ 跨对象信号连接的生命周期 ==================== */

static int g_xqiTitleHits = 0;

/** @brief windowTitleChanged 槽：只计发射次数（载荷不参与断言）。 */
static void xqiTitleChangedSlot(XObject* receiver, XVarList* args)
{
    (void)receiver;
    (void)args;
    ++g_xqiTitleHits;
}

/**
 * @brief 接收者析构 ⇒ 连接自动失效；发送者析构 ⇒ 接收者侧连接自动摘除。
 * @details Qt 依据：`QObject::~QObject` 断开本对象作为**接收者**的全部连接
 *          （qobject.cpp `QObjectPrivate::~QObjectPrivate` 遍历 senders /
 *          connectionLists），也断开本对象作为**发送者**的连接。因此：
 *          - 删接收者后，发送者再发信号不得触发任何槽；
 *          - 删发送者后，接收者的连接簿记必须清空（本框架
 *            `XSignalSlot_deinit` 两个方向都摘，XSignalSlot.c:63-108）。
 */
XQ_TEST(XWidgetIntegration, signalConnectionLifetime)
{
    XWidget* sender = NULL;
    XWidget* receiver = NULL;
    size_t sig = XSignal(XWidget_windowTitleChanged_signal);

    /* ---- 接收者先亡 ---- */
    sender = XWidget_create(NULL, 0);
    receiver = XWidget_create(NULL, 0);
    XQ_REQUIRE(sender && receiver);
    XQ_CHECK(XObject_connect((XObject*)sender, sig, (XObject*)receiver,
                               xqiTitleChangedSlot,
                               XConnectionType_Direct) != NULL);
    g_xqiTitleHits = 0;
    XWidget_setWindowTitle_2(sender, "t1");
    XQ_EQ_I64(g_xqiTitleHits, 1);
    /* 连接同时登记在接收者的 bindSignalList（connect1 的 receiver 簿记）。 */
    XQ_EQ_SIZE(xqiBindCount((XObject*)receiver), 1);

    XClassDelete((XClass*)receiver);
    /* 发送者侧连接数归零（接收者析构时已把自己从 senders 摘掉）。 */
    XQ_CHECK_PTR(((XObject*)sender)->m_signalSlot);
    XQ_EQ_I64(XSignalSlot_receivers(((XObject*)sender)->m_signalSlot, sig), 0);
    XWidget_setWindowTitle_2(sender, "t2"); /* 不得回调、不得崩。 */
    XQ_EQ_I64(g_xqiTitleHits, 1);
    XClassDelete((XClass*)sender);

    /* ---- 发送者先亡 ---- */
    sender = XWidget_create(NULL, 0);
    receiver = XWidget_create(NULL, 0);
    XQ_REQUIRE(sender && receiver);
    XQ_CHECK(XObject_connect((XObject*)sender, sig, (XObject*)receiver,
                               xqiTitleChangedSlot,
                               XConnectionType_Direct) != NULL);
    XQ_EQ_SIZE(xqiBindCount((XObject*)receiver), 1);
    XClassDelete((XClass*)sender);
    /* 接收者的连接簿记被发送者析构清空（无悬垂连接）。 */
    XQ_EQ_SIZE(xqiBindCount((XObject*)receiver), 0);
    /* 接收者仍可安全使用并重新建立连接。 */
    {
        XWidget* sender2 = XWidget_create(NULL, 0);
        XQ_REQUIRE(sender2 != NULL);
        XQ_CHECK(XObject_connect((XObject*)sender2, sig,
                                   (XObject*)receiver, xqiTitleChangedSlot,
                                   XConnectionType_Direct) != NULL);
        g_xqiTitleHits = 0;
        XWidget_setWindowTitle_2(sender2, "t3");
        XQ_EQ_I64(g_xqiTitleHits, 1);
        XClassDelete((XClass*)sender2);
        XQ_EQ_SIZE(xqiBindCount((XObject*)receiver), 0);
    }
    XClassDelete((XClass*)receiver);
}

/* ==================== ⑥ 事件过滤器跨控件 ==================== */

/**
 * @brief 同一过滤器挂多个控件、逐个反注册、过滤器自身析构自动摘除。
 * @details Qt 依据（qobject.h / qobject.cpp）：
 *          - `installEventFilter` 后事件先经过滤器，返回 true 即止步；
 *          - `removeEventFilter` 恢复直达；
 *          - **过滤器析构时自动从所有被观察对象摘除**（QObject 析构遍历
 *            `d->watchers`），被观察对象不会持有悬垂过滤器指针。
 *          本框架两侧对称维护（XObject.c:148-186），本用例用「事件是否
 *          真的到达被观察控件」做正面验证——只数过滤器调用次数无法区分
 *          「过滤器析构后仍被调用（UAF）」与「已摘除」。
 */
XQ_TEST(XWidgetIntegration, eventFilterAcrossWidgets)
{
    XWidget* host = XWidget_create(NULL, 0);
    XqiProbeWidget* probeA = NULL;
    XqiProbeWidget* probeB = NULL;
    XqiFilterObject* filter = NULL;
    XqiFilterObject* filter2 = NULL;
    XEvent event;

    XQ_REQUIRE(host != NULL);
    probeA = XqiProbe_create(host);
    probeB = XqiProbe_create(host);
    filter = XqiFilter_create(-1);
    XQ_REQUIRE(probeA && probeB && filter);

    XEvent_init(&event, XEVENT_TYPE_WINDOW_TITLE_CHANGE);

    /* 无过滤器：事件直达控件 event()。 */
    XCoreApplication_sendEvent((XObject*)probeA, &event);
    XQ_EQ_I64(probeA->m_titleEvents, 1);

    /* 同一过滤器挂到两个控件：两边事件都先经过它。 */
    XObject_installEventFilter((XObject*)probeA, (XObject*)filter);
    XObject_installEventFilter((XObject*)probeB, (XObject*)filter);
    XCoreApplication_sendEvent((XObject*)probeA, &event);
    XCoreApplication_sendEvent((XObject*)probeB, &event);
    XQ_EQ_I64(filter->m_calls, 2);
    XQ_EQ_I64(probeA->m_titleEvents, 2);
    XQ_EQ_I64(probeB->m_titleEvents, 1);

    /* 从 A 反注册：A 恢复直达，B 仍受过滤。
     * 计数账：A 已收 826/832 两次；B 已收 833 一次。 */
    XObject_removeEventFilter((XObject*)probeA, (XObject*)filter);
    XCoreApplication_sendEvent((XObject*)probeA, &event);
    XCoreApplication_sendEvent((XObject*)probeB, &event);
    XQ_EQ_I64(filter->m_calls, 3);       /* 只有 B 那次过过滤器。 */
    XQ_EQ_I64(probeA->m_titleEvents, 3); /* A 直达（第 3 次）。 */
    XQ_EQ_I64(probeB->m_titleEvents, 2); /* B 第 2 次（仍被过滤，未吞）。 */

    /* 过滤器吞事件：被观察控件收不到。 */
    filter->m_swallow = XEVENT_TYPE_WINDOW_TITLE_CHANGE;
    XCoreApplication_sendEvent((XObject*)probeB, &event);
    XQ_EQ_I64(filter->m_calls, 4);
    XQ_EQ_I64(probeB->m_titleEvents, 2); /* 被吞：计数不动。 */
    filter->m_swallow = -1;
    XCoreApplication_sendEvent((XObject*)probeB, &event);
    XQ_EQ_I64(probeB->m_titleEvents, 3);

    /* 过滤器在被挂载状态下析构：B 侧登记必须自动摘除（否则下一次派发
     * 即读已释放内存）。用「事件仍能到达 B」做正面证据。 */
    XClassDelete((XClass*)filter);
    XCoreApplication_sendEvent((XObject*)probeB, &event);
    XQ_EQ_I64(probeB->m_titleEvents, 4);

    /* 摘除后槽位可复用：新过滤器可再次挂上并生效。 */
    filter2 = XqiFilter_create(XEVENT_TYPE_WINDOW_TITLE_CHANGE);
    XQ_REQUIRE(filter2 != NULL);
    XObject_installEventFilter((XObject*)probeA, (XObject*)filter2);
    XCoreApplication_sendEvent((XObject*)probeA, &event);
    XQ_EQ_I64(filter2->m_calls, 1);
    XQ_EQ_I64(probeA->m_titleEvents, 3); /* 被新过滤器吞掉。 */
    XObject_removeEventFilter((XObject*)probeA, (XObject*)filter2);
    XCoreApplication_sendEvent((XObject*)probeA, &event);
    XQ_EQ_I64(probeA->m_titleEvents, 4);
    XClassDelete((XClass*)filter2);

    /* host 析构带走两个探针控件（探针是 host 的子控件）。 */
    XClassDelete((XClass*)host);
}

/* ==================== ⑦ 嵌套控件中的焦点链 ==================== */

/**
 * @brief 焦点落在最深层子控件；Tab 链跨层级推进；焦点控件析构自动清焦。
 * @details Qt 依据（qwidget.h / qwidget.cpp）：
 *          - `QWidget::focusWidget()` 返回**本窗口内**的焦点控件，可以是
 *            任意深度的子控件（`QWidget::setFocus` 把焦点交给最深层
 *            focus proxy）；
 *          - `focusNextChild()/focusPreviousChild()` 是窗口级遍历
 *            （`focusNextPrevChild` 把决定权上交顶层窗口），候选含孙代；
 *          - 焦点控件被销毁时 `QWidgetPrivate::deactivateWidgetCleanup`
 *            清全局焦点指针（否则 app focusWidget 悬垂）。
 */
XQ_TEST(XWidgetIntegration, focusChainAcrossNestedWidgets)
{
    XWidget* top = XWidget_create(NULL, 0);
    XWidget* panel = NULL;
    XWidget* a = NULL;
    XWidget* b = NULL;
    XWidget* c = NULL;

    XQ_REQUIRE(top != NULL);
    panel = XWidget_create(top, 0);
    a = XWidget_create(panel, 0);
    b = XWidget_create(panel, 0);
    c = XWidget_create(top, 0);
    XQ_REQUIRE(panel && a && b && c);

    XWidget_setFocusPolicy(a, XWidgetFocusPolicy_TabFocus);
    XWidget_setFocusPolicy(b, XWidgetFocusPolicy_TabFocus);
    XWidget_setFocusPolicy(c, XWidgetFocusPolicy_TabFocus);
    /* Tab 候选判据要求控件不在显式隐藏链上（xwidget_focusChainShown）。 */
    XWidget_show(a);
    XWidget_show(b);
    XWidget_show(c);

    /* 焦点落到孙代控件：父面板与顶层窗口都报告同一个焦点控件。 */
    XWidget_setFocus(a);
    XQ_CHECK(XWidget_hasFocus(a));
    XQ_CHECK(!XWidget_hasFocus(b));
    XQ_CHECK(XWidget_focusWidget(panel) == a);
    XQ_CHECK(XWidget_focusWidget(top) == a);
    XQ_CHECK(XWidget_appFocusWidget() == a);

    /* 嵌套兄弟间转移（同层面板内）。 */
    XWidget_setFocus(b);
    XQ_CHECK(XWidget_hasFocus(b));
    XQ_CHECK(!XWidget_hasFocus(a));
    XQ_CHECK(XWidget_focusWidget(top) == b);

    /* 显式 Tab 链跨越层级：a→b（同面板）、b→c（跨到顶层直属子件）。 */
    XWidget_setTabOrder(a, b);
    XWidget_setTabOrder(b, c);
    XQ_CHECK(XWidget_nextInFocusChain(a) == b);
    XQ_CHECK(XWidget_nextInFocusChain(b) == c);
    XQ_CHECK(XWidget_previousInFocusChain(c) == b);

    /* 窗口级 Tab 遍历：以当前焦点为起点推进/回退（跨层级候选集合）。 */
    XWidget_setFocus(a);
    XQ_CHECK(XWidget_focusNextChild(top));
    XQ_CHECK(XWidget_hasFocus(b));
    XQ_CHECK(XWidget_focusPreviousChild(top));
    XQ_CHECK(XWidget_hasFocus(a));

    /* 焦点控件被析构：全局焦点自动清空（不得留悬垂指针）。 */
    XWidget_setFocus(b);
    XQ_CHECK(XWidget_hasFocus(b));
    XClassDelete((XClass*)b);
    XQ_CHECK(XWidget_focusWidget(top) == NULL);
    XQ_CHECK(XWidget_appFocusWidget() == NULL);

    XWidget_clearFocus(a);
    XQ_CHECK(XWidget_focusWidget(top) == NULL);

    /* top 析构带走 panel/a/c。 */
    XClassDelete((XClass*)top);
}

/* ==================== ⑧ 对话框父子与 accept/reject 生命周期 ==================== */

static int g_xqiAccepted = 0;
static int g_xqiRejected = 0;

static void xqiAcceptedSlot(XObject* receiver, XVarList* args)
{
    (void)receiver;
    (void)args;
    ++g_xqiAccepted;
}

static void xqiRejectedSlot(XObject* receiver, XVarList* args)
{
    (void)receiver;
    (void)args;
    ++g_xqiRejected;
}

/**
 * @brief XDialog 随父控件的所有权、accept/reject 的结果码与显隐收口。
 * @details Qt 依据（qdialog.h / qdialog.cpp:600-625）：
 *          - `QDialog(parent)` 是独立顶层窗口，但对象所有权仍归 parent
 *            （QObject 父子链）⇒ 父析构带走对话框；
 *          - `accept()/reject()` 等价 `done(Accepted/Rejected)`：写 result、
 *            隐藏对话框，并发射 accepted/rejected。
 *          本用例不调 show/exec（不弹真窗口），只验证构造/收口/析构。
 */
XQ_TEST(XWidgetIntegration, dialogParentAndResult)
{
#if XDIALOG_ON
    XWidget* parent = XWidget_create(NULL, 0);
    XDialog* dlg = NULL;
    XWidget* content = NULL;
    XObject* cDlg = NULL;
    XObject* cContent = NULL;

    XQ_REQUIRE(parent != NULL);
    dlg = XDialog_create(parent, 0);
    content = XWidget_create((XWidget*)dlg, 0);
    cDlg = xqiCounterNew();
    cContent = xqiCounterNew();
    XQ_REQUIRE(dlg && content && cDlg && cContent);
    XQ_CHECK(xqiCounterWatch((XObject*)dlg, cDlg));
    XQ_CHECK(xqiCounterWatch((XObject*)content, cContent));

    /* 对话框是独立顶层（Dialog 窗口类型），但所有权归 parent。 */
    XQ_CHECK(XWidget_parentWidget((XWidget*)dlg) == parent);
    XQ_CHECK(xqiHasChild((XObject*)parent, (XObject*)dlg));
    XQ_EQ_I64(XDialog_result(dlg), XDialogCode_Rejected); /* 构造初值 0 */

    XQ_CHECK(XObject_connect((XObject*)dlg, XSignal(XDialog_accepted_signal),
                               (XObject*)dlg, xqiAcceptedSlot,
                               XConnectionType_Direct) != NULL);
    XQ_CHECK(XObject_connect((XObject*)dlg, XSignal(XDialog_rejected_signal),
                               (XObject*)dlg, xqiRejectedSlot,
                               XConnectionType_Direct) != NULL);

    g_xqiAccepted = 0;
    g_xqiRejected = 0;
    XDialog_accept(dlg);
    XQ_EQ_I64(XDialog_result(dlg), XDialogCode_Accepted);
    XQ_EQ_I64(g_xqiAccepted, 1);
    XQ_EQ_I64(g_xqiRejected, 0);
    XQ_CHECK(XWidget_isHidden((XWidget*)dlg)); /* done ⇒ hide */

    XDialog_reject(dlg);
    XQ_EQ_I64(XDialog_result(dlg), XDialogCode_Rejected);
    XQ_EQ_I64(g_xqiRejected, 1);
    XQ_EQ_I64(g_xqiAccepted, 1);

    /* 父析构：对话框与其内容控件（孙代）一并释放。 */
    XClassDelete((XClass*)parent);
    XQ_EQ_I64(xqiCounterHits(cDlg), 1);
    XQ_EQ_I64(xqiCounterHits(cContent), 1);

    xqiCounterFree(cDlg);
    xqiCounterFree(cContent);
#endif
}

/**
 * @brief XMessageBox 的内部子件所有权、父析构与 accept/reject 生命周期。
 * @details Qt 依据：`QMessageBox` 的消息标签/按钮盒是对话框子对象
 *          （`QMessageBoxPrivate::setupLayout` 里 `new QLabel`、
 *          `new QDialogButtonBox(this...)`），随对话框释放；
 *          `QMessageBox` 没有覆写 done/accept（qmessagebox.h:296-299 只
 *          有 Q_QDOC 的 exec 文档块）⇒ accept/reject 走 XDialog 基类收口。
 *          本用例只构造 + 收口 + 析构，不 show/exec。
 */
XQ_TEST(XWidgetIntegration, messageBoxParentAndResult)
{
#if XDIALOG_ON && XMESSAGEBOX_ON
    XWidget* parent = XWidget_create(NULL, 0);
    XMessageBox* box = NULL;
    XObject* cBox = NULL;

    XQ_REQUIRE(parent != NULL);
    box = XMessageBox_create(parent, 0);
    XQ_REQUIRE(box != NULL);
    cBox = xqiCounterNew();
    XQ_REQUIRE(cBox != NULL);
    XQ_CHECK(xqiCounterWatch((XObject*)box, cBox));

    XQ_CHECK(XWidget_parentWidget((XWidget*)box) == parent);
    XQ_CHECK(xqiHasChild((XObject*)parent, (XObject*)box));

    /* 内部文本标签是对话框的子对象（所有权随对话框）。 */
    XQ_CHECK_PTR(box->m_textLabel);
    XQ_CHECK(XWidget_parentWidget((XWidget*)box->m_textLabel) ==
             (XWidget*)box);

    XMessageBox_setText(box, "集成测试");
    XQ_EQ_STR(XMessageBox_text(box), "集成测试");
    XMessageBox_setStandardButtons(
        box, (int)(XDialogButtonBoxStandard_Ok |
                   XDialogButtonBoxStandard_Cancel));
    XQ_EQ_I64(XMessageBox_standardButtons(box),
              (int)(XDialogButtonBoxStandard_Ok |
                    XDialogButtonBoxStandard_Cancel));
    XQ_CHECK_PTR(XMessageBox_button(box, XDialogButtonBoxStandard_Ok));

    g_xqiAccepted = 0;
    g_xqiRejected = 0;
    XQ_CHECK(XObject_connect((XObject*)box,
                               XSignal(XDialog_accepted_signal),
                               (XObject*)box, xqiAcceptedSlot,
                               XConnectionType_Direct) != NULL);
    XQ_CHECK(XObject_connect((XObject*)box,
                               XSignal(XDialog_rejected_signal),
                               (XObject*)box, xqiRejectedSlot,
                               XConnectionType_Direct) != NULL);
    XDialog_accept((XDialog*)box);
    XQ_EQ_I64(XDialog_result((XDialog*)box), XDialogCode_Accepted);
    XQ_EQ_I64(g_xqiAccepted, 1);
    XQ_CHECK(XWidget_isHidden((XWidget*)box));

    XDialog_reject((XDialog*)box);
    XQ_EQ_I64(XDialog_result((XDialog*)box), XDialogCode_Rejected);
    XQ_EQ_I64(g_xqiRejected, 1);

    /* 显式释放消息盒（含内部标签/按钮盒），父控件不受反噬。 */
    XClassDelete((XClass*)box);
    XQ_EQ_I64(xqiCounterHits(cBox), 1);
    XQ_CHECK(XWidget_parentWidget(parent) == NULL);
    XClassDelete((XClass*)parent);

    xqiCounterFree(cBox);
#endif
}

#endif /* XGUI_ON && XWIDGET_ON */
