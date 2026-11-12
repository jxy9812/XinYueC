/**
 * @file       XWidgetAlignTest.c
 * @brief      XWidget × Qt 6.8 QWidget 对齐契约测试。
 * @details    覆盖本批重点（几何/内容矩形、sizeHint 链条、布局接管与解除、
 *             父子所有权、visible/enabled、焦点策略与焦点链、事件分发与
 *             事件过滤器、mapTo/mapFrom、屏幕归属、信号集成、move 赋值的
 *             对象身份契约）。每条断言的 Qt 依据写在断言旁的注释里：
 *             - 头文件：/opt/Qt/6.8.3/gcc_64/include/QtWidgets/qwidget.h
 *             - 行为实测：本机 Qt 6.8.0 + offscreen 平台探针（结论随断言注明）
 *
 *             泄漏由运行器统一判定：用例只创建、父对象析构后不手动删除
 *             子对象，子对象若未随父释放即判 `[LEAK]`。
 * @author     XinYueC 团队
 */
#include "xq_align.h"

#include "XGuiConfig.h"

#if XGUI_ON && XWIDGET_ON

#include "XWidget.h"
#include "XWidget_Protected.h" /* XWidget_setSizeHint（保护接口，模拟子类覆写）。 */
#include "XObject.h"
#include "XString.h"
#include "XGeometry.h"
#include "XBoxLayout.h"
#include "XVector.h"
#include "XEvent.h"
#include "XVarList.h"
#include "XCoreApplication.h"
#include "XScreen.h"
#include "XImage.h"    /* grab 快照像素/dpr 标签断言。 */
#include "XColor.h"    /* XColor_init_rgb / 分量读取。 */
#include "XPalette.h"  /* 背景色装配（autoFillBackground 回填验证）。 */

/* ==================== 事件探针控件 ==================== */

/**
 * @brief 事件计数探针：覆写 EXObject_Event 统计 WINDOW_TITLE_CHANGE 到达次数。
 * @details 事件过滤器测试需要「被过滤者是否真的收到事件」的可观测点；
 *          Qt 的对应观察点是子类覆写 `QWidget::event()`。
 */
XCLASS_DEFINE_BEGING(XqProbeWidget)
XCLASS_DEFINE_EXTEND_END(XqProbeWidget, XWidget)

typedef struct XqProbeWidget
{
    XWidget m_base;      /**< 基类成员；必须是第一个。 */
    int m_titleChanges;  /**< WINDOW_TITLE_CHANGE 到达次数。 */
    int m_totalEvents;   /**< 全部事件到达次数。 */
} XqProbeWidget;

static XVtable* XqProbeWidget_class_init(void);

static bool XqProbeWidget_event(XObject* receiver, XEvent* event)
{
    XqProbeWidget* self = (XqProbeWidget*)receiver;
    if (!self || !event) return false;
    ++self->m_totalEvents;
    if (XEvent_type(event) == XEVENT_TYPE_WINDOW_TITLE_CHANGE)
        ++self->m_titleChanges;
    /* 继续走 XWidget 基类分派（其余事件语义不变）。 */
    return XClass_Parent(XWidget, EXObject_Event,
                         bool (*)(XObject*, XEvent*))((XObject*)receiver,
                                                      event);
}

static XVtable* XqProbeWidget_class_init(void)
{
    XVTABLE_INIT_DEFAULT(XqProbeWidget);
    XVTABLE_INHERIT_XCLASS(XWidget);
    XVTABLE_OVERLOAD_DEFAULT(EXObject_Event, XqProbeWidget_event);
    return XVTABLE_DEFAULT;
}

static XqProbeWidget* XqProbe_create(XWidget* parent)
{
    XqProbeWidget* self =
        (XqProbeWidget*)XMemory_malloc(sizeof(XqProbeWidget),
                                       XCLASS_DEFAULT_MEMORY_TYPE);
    if (!self) return NULL;
    XMemset(self, 0, sizeof(*self));
    XWidget_init(&self->m_base, parent, 0);
    XClassSetVtable(self, XqProbeWidget);
    Set_Class_IsHeap(self, true);
    return self;
}

/* ==================== 事件过滤器对象 ==================== */

/**
 * @brief 事件过滤器对象：记录调用次数并按需吞事件。
 * @details 对标 Qt 里「新建 QObject 子类覆写 eventFilter」的用法
 *          （qobject.h `virtual bool eventFilter(QObject*, QEvent*)`）——
 *          Qt 的 QObject 本身就能当过滤器，故本探针直接派生 XObject。
 */
XCLASS_DEFINE_BEGING(XqFilterObject)
XCLASS_DEFINE_EXTEND_END(XqFilterObject, XObject)

typedef struct XqFilterObject
{
    XObject m_base;   /**< 基类成员；必须是第一个。 */
    int m_calls;      /**< eventFilter 调用次数。 */
    int m_swallow;    /**< 要吞掉的事件类型（-1=不吞）。 */
} XqFilterObject;

static XVtable* XqFilterObject_class_init(void);

static bool XqFilterObject_eventFilter(XObject* self, XObject* watched,
                                       XEvent* event)
{
    XqFilterObject* f = (XqFilterObject*)self;
    (void)watched;
    if (!f || !event) return false;
    ++f->m_calls;
    return f->m_swallow >= 0 && (int)XEvent_type(event) == f->m_swallow;
}

static XVtable* XqFilterObject_class_init(void)
{
    XVTABLE_INIT_DEFAULT(XqFilterObject);
    XVTABLE_INHERIT_XCLASS(XObject);
    XVTABLE_OVERLOAD_DEFAULT(EXObject_EventFilter, XqFilterObject_eventFilter);
    return XVTABLE_DEFAULT;
}

static XqFilterObject* XqFilter_create(int swallowType)
{
    XqFilterObject* f = (XqFilterObject*)XMemory_malloc(
        sizeof(XqFilterObject), XCLASS_DEFAULT_MEMORY_TYPE);
    if (!f) return NULL;
    XMemset(f, 0, sizeof(*f));
    XObject_init(&f->m_base);
    XClassSetVtable(f, XqFilterObject);
    Set_Class_IsHeap(f, true);
    f->m_swallow = swallowType;
    return f;
}

/* ==================== 信号槽探针 ==================== */

static int s_titleSignalCount = 0;

/** @brief windowTitleChanged 槽：只计发射次数（参数不参与断言）。 */
static void xq_titleChangedSlot(XObject* receiver, XVarList* args)
{
    (void)receiver;
    (void)args;
    ++s_titleSignalCount;
}

/* ==================== 用例 ==================== */

/** @brief 几何与内容矩形（qwidget.h:876-896 rect/geometry/contentsRect）。 */
XQ_TEST(XWidget, geometryAndContentsMargins)
{
    XWidget* w = XWidget_create(NULL, 0);
    XRect r;
    XRect cr;
    XPoint p;
    XQ_REQUIRE(w != NULL);

    /* Qt 6.8 实测：无父控件构造后 geometry() 由平台/缺省尺寸决定，
     * 本项目缺省 640x480（XWidget_init 的 QWidgetPrivate::init 等价）。 */
    XQ_EQ_I64(XWidget_width(w), 640);
    XQ_EQ_I64(XWidget_height(w), 480);

    XWidget_setGeometry(w, 10, 20, 200, 100);
    XQ_EQ_I64(XWidget_x(w), 10);
    XQ_EQ_I64(XWidget_y(w), 20);
    p = XWidget_pos(w);
    XQ_CHECK(p.x == 10 && p.y == 20);

    /* Qt 语义（qwidget.h:890-891）：rect() 恒为 (0,0,w,h)，与内容边距无关。 */
    r = XWidget_rect(w);
    XQ_CHECK(r.x == 0 && r.y == 0 && r.width == 200 && r.height == 100);

    /* Qt 语义（qwidget.cpp:7705-7708）：contentsRect() = rect() 扣四边边距。 */
    XWidget_setContentsMargins(w, 1, 2, 3, 4);
    cr = XWidget_contentsRect(w);
    XQ_CHECK(cr.x == 1 && cr.y == 2);
    XQ_EQ_I64(cr.width, 196);
    XQ_EQ_I64(cr.height, 94);
    r = XWidget_rect(w); /* 边距不改 rect()。 */
    XQ_CHECK(r.x == 0 && r.y == 0 && r.width == 200 && r.height == 100);

    /* Qt 语义（qwidget.cpp:setMinimumSize）：当前尺寸小于新下限时自动增大。 */
    XWidget_setMinimumSize(w, 300, 260);
    XQ_EQ_I64(XWidget_width(w), 300);
    XQ_EQ_I64(XWidget_height(w), 260);

    /* setFixedSize ⇒ min=max（qwidget.h:901-904）。 */
    XWidget_setFixedSize(w, 120, 80);
    XQ_EQ_I64(XWidget_minimumWidth(w), 120);
    XQ_EQ_I64(XWidget_maximumWidth(w), 120);
    XQ_EQ_I64(XWidget_width(w), 120);

    XClassDelete((XClass*)w);
}

/** @brief 父子所有权、重挂父级与祖先判定（qwidget.h:childAt/isAncestorOf）。 */
XQ_TEST(XWidget, parentOwnership)
{
    XWidget* host = XWidget_create(NULL, 0);
    XWidget* a = NULL;
    XWidget* b = NULL;
    const XVector* children;
    XQ_REQUIRE(host != NULL);

    a = XWidget_create(host, 0);
    b = XWidget_create(host, 0);
    XQ_REQUIRE(a != NULL && b != NULL);

    XQ_CHECK(XWidget_parentWidget(a) == host);
    children = XObject_children((XObject*)host);
    XQ_CHECK_PTR(children);
    if (children)
        XQ_EQ_SIZE(XContainer_size_base((const XContainer*)children), 2);

    /* Qt：isAncestorOf 对自身返回 true（qwidget.cpp 循环首轮即比较 this）。 */
    XQ_CHECK(XWidget_isAncestorOf(host, a));
    XQ_CHECK(XWidget_isAncestorOf(host, host));
    XQ_CHECK(!XWidget_isAncestorOf(a, host));

    /* 重挂父级：a 改挂到 b 之下。 */
    XWidget_setParent_2(a, b);
    XQ_CHECK(XWidget_parentWidget(a) == b);
    XQ_CHECK(XWidget_isAncestorOf(b, a));
    children = XObject_children((XObject*)host);
    if (children)
        XQ_EQ_SIZE(XContainer_size_base((const XContainer*)children), 1);

    /* 父析构带走整棵子树（a 在 b 之下，b 在 host 之下）：全部由 host
     * 析构级联释放，用例不手动删 a/b——漏删即运行器判 [LEAK]。 */
    XClassDelete((XClass*)host);
}

/* ==================== move 赋值：目标对象身份不随 move 拆除 ==================== */

/** @brief 目标 destroyed 信号命中计数（move 前后连接是否存活的可观测证据）。 */
static int g_xqMoveDestroyedHits = 0;

/** @brief destroyed 槽：只计数（连接存活即命中）。 */
static void xqMove_destroyedSlot(XObject* receiver, XVarList* args)
{
    (void)receiver;
    (void)args;
    ++g_xqMoveDestroyedHits;
}

/**
 * @brief move 赋值后目标仍是活对象：XObject 身份/信号槽/线程亲和全部保留。
 * @details 本用例锁的是 2026-11-11 实测根因：VXWidget_move 原先对目标调
 *          `VXWidget_deinit`（走完整 XObject 基类析构），留下三个目标侧
 *          缺陷：
 *          ① `m_was_deleted` 置位 ⇒ move 之后重建的对象级资源（如
 *             XLabel_move 里 label_connectTextControl 重新接出的
 *             XSignalSlot）在最终析构时被 VXObject_deinit 的早退闸整段
 *             跳过 ⇒ 每次 move 泄漏 448 字节
 *             （XPerformanceOverlay.defaultsAndValueSemantics 实测）；
 *          ② `m_deiniting` 永久置位 ⇒ XWidget_postPaintEvent 对目标永久
 *             早退，移入的控件再也画不出来；
 *          ③ `m_threadData` 清空 ⇒ 目标收不到任何 posted 事件
 *             （deleteLater/定时器/元调用静默丢弃）。
 *          本源既有 move 契约见 XBarSet.c:221「XObject 的信号槽与
 *          m_was_deleted 状态须保持存活」：move 只替换控件级状态。
 *          Qt 无 QWidget 移动构造（Q_DISABLE_COPY(QObject)），故本用例
 *          锁的是本仓库自己的值语义契约，不是 Qt API 对表。
 * @note  连接用 destroyed 信号（信号函数只取 XObject* 形参）：
 *        XSignal 宏以 fn(NULL) 取标识，形参多于 object 的信号在 NULL
 *        调用下会读未初始化实参。
 */
XQ_TEST(XWidget, moveKeepsObjectIdentity)
{
    XqProbeWidget* dst = NULL;
    XWidget* src = NULL;
    XObject* observer = NULL;
    XThread* threadBefore = NULL;

    dst = XqProbe_create(NULL);
    src = XWidget_create(NULL, 0);
    observer = XObject_create();
    XQ_REQUIRE(dst != NULL && src != NULL && observer != NULL);

    /* 目标先建立一条 XObject 级信号连接，并记录线程亲和。 */
    XQ_CHECK(XObject_connect((XObject*)dst,
                               XSignal(XObject_destroyed_signal),
                               observer, xqMove_destroyedSlot,
                               XConnectionType_Direct) != NULL);
    threadBefore = XObject_thread((XObject*)dst);
    XQ_CHECK_PTR(threadBefore);

    XWidget_resize(src, 123, 45);
    XClassMove(dst, src);

    /* ① 目标未被标记「已析构」：否则最终 XClassDelete 跳过 XObject 级
     *    释放（泄漏），且 setParent/moveToThread/postEvent 一律拒收。 */
    XQ_CHECK(!((XObject*)dst)->m_was_deleted);
    /* ② 析构入口闸复位：否则 XWidget_postPaintEvent 对目标永久早退。 */
    XQ_EQ_I64(dst->m_base.m_deiniting, 0);
    /* ③ 线程亲和保留（Qt：线程亲和属于对象身份，不随值转移）。 */
    XQ_CHECK(XObject_thread((XObject*)dst) == threadBefore);
    /* ④ move 本身兑现：源控件几何转移到目标。 */
    XQ_EQ_I64(XWidget_width((XWidget*)dst), 123);
    XQ_EQ_I64(XWidget_height((XWidget*)dst), 45);
    /* ⑤ 目标既有信号连接存活：目标真析构时 destroyed 必须命中一次
     *    （move 拆掉 XObject 基类时该连接会随基类析构一起消失）。 */
    g_xqMoveDestroyedHits = 0;
    XClassDelete((XClass*)dst); /* 目标真析构：对象级资源必须全部归还。 */
    XQ_EQ_I64(g_xqMoveDestroyedHits, 1);

    XClassDelete((XClass*)src);
    XClassDelete((XClass*)observer);
}

/** @brief 布局接管与解除（qwidget.h:537-538 layout/setLayout）。 */
XQ_TEST(XWidget, layoutAttachDetach)
{
    XWidget* host = XWidget_create(NULL, 0);
    XBoxLayout* layout = NULL;
    XQ_REQUIRE(host != NULL);

    layout = XBoxLayout_create(XBoxLayoutDirection_TopToBottom, NULL);
    XQ_REQUIRE(layout != NULL);

    /* 借用版挂接（Qt 的 setLayout 接管所有权；本项目按 C 值语义拆成
     * 借用版 + `_ref` 接管版，见 XWidget.h 的已登记偏差）。 */
    XWidget_setLayout(host, (XLayout*)layout);
    XQ_CHECK(XWidget_layout(host) == (XLayout*)layout);
    XQ_CHECK(XWidget_layout_const(host) == (const XLayout*)layout);

    /* 布局进入控件后，sizeHint 链条改由布局合计给出
     * （qwidget.cpp QWidget::sizeHint → layout()->totalSizeHint()）。 */
    {
        XSize hint;
        XWidget* kid = XWidget_create(host, 0);
        XQ_REQUIRE(kid != NULL);
        XWidget_setMinimumSize(kid, 40, 24);
        {
            XSize kidHint;
            kidHint.width = 40;
            kidHint.height = 24;
            XWidget_setSizeHint(kid, &kidHint);
        }
        XBoxLayout_addWidget(layout, kid);
        hint = XWidget_sizeHint_base(host);
        XQ_CHECK_MSG(hint.width >= 40 && hint.height >= 24,
                     "布局合计 sizeHint 应涵盖子项（实际 %dx%d）",
                     hint.width, hint.height);
    }

    /* setLayout(NULL)：Qt 6.8 实测打印 `QWidget::setLayout: Cannot set
     * layout to 0` 并**保持原布局**（qwidget.cpp:10262-10287；takeLayout
     * 在 qwidget.h:738 是 private，Qt 没有公开摘挂入口）。 */
    XWidget_setLayout(host, NULL);
    XQ_CHECK(XWidget_layout_const(host) == (const XLayout*)layout);
    /* 同一布局重复设置 ⇒ 幂等（qwidget.cpp:10270-10275 `if (l != layout())
     * 告警; return;`），不 detach/重挂。 */
    XWidget_setLayout(host, (XLayout*)layout);
    XQ_CHECK(XWidget_layout_const(host) == (const XLayout*)layout);

    /* 借用布局归调用方：宿主析构会先摘挂（VXWidget_deinit 的
     * XLayout_detachWidget），之后才可安全释放布局。 */
    XClassDelete((XClass*)host); /* 子控件随 host 析构。 */
    XClassDelete((XClass*)layout);
}

/** @brief 换父可见性：Qt setParent 后须显式 show 才重新可见
 *         （qwidget.cpp:10694-10740；本机 Qt 6.8.0 + offscreen 实测：
 *         可见子件换挂到可见新父 → isVisible=0、isHidden=1）。 */
XQ_TEST(XWidget, setParentHidesVisibleChild)
{
    XWidget* top = XWidget_create(NULL, 0);
    XWidget* other = XWidget_create(NULL, 0);
    XWidget* child = NULL;
    XQ_REQUIRE(top != NULL && other != NULL);
    child = XWidget_create(top, 0);
    XQ_REQUIRE(child != NULL);

    XWidget_resize(top, 100, 100);
    XWidget_resize(other, 100, 100);
    XWidget_show(top);
    XWidget_show(other);
    XWidget_show(child);
    XQ_CHECK(XWidget_isVisible(child));

    /* 换挂到另一个**可见**父：Qt 先 hide 并清 ExplicitShowHide，新父可见
     * 也不会自动恢复。 */
    XWidget_setParent_2(child, other);
    XQ_CHECK(XWidget_parentWidget(child) == other);
    XQ_CHECK(!XWidget_isVisible(child));
    XQ_CHECK(XWidget_isHidden(child));

    /* 显式 show 后重新可见（Qt 实测同一序列）。 */
    XWidget_show(child);
    XQ_CHECK(XWidget_isVisible(child));
    XQ_CHECK(!XWidget_isHidden(child));

    XClassDelete((XClass*)top);
    XClassDelete((XClass*)other); /* child 在 other 之下，随之释放。 */
}

/** @brief scroll(dx,dy) 平移全部非窗口子件（含显式隐藏件）；
 *         scrollRect(dx,dy,rect) 不平移子件；更新禁用但有子件时仍平移
 *         （qwidget.cpp:11089-11132、198-218；本机 Qt 6.8.0 实测
 *         子件 (10,10) 在 scroll(5,7) 后 (15,17)）。 */
XQ_TEST(XWidget, scrollMovesChildren)
{
    XWidget* top = XWidget_create(NULL, 0);
    XWidget* a = NULL;
    XWidget* b = NULL;
    XQ_REQUIRE(top != NULL);
    XWidget_resize(top, 200, 100);
    a = XWidget_create(top, 0);
    b = XWidget_create(top, 0);
    XQ_REQUIRE(a != NULL && b != NULL);
    XWidget_setGeometry(a, 10, 10, 30, 20);
    XWidget_setGeometry(b, 40, 10, 30, 20);
    XWidget_show(top); /* 顶层可见；未显式隐藏的子件随 showChildren 可见。 */
    XQ_REQUIRE(XWidget_isVisible(top));
    XQ_CHECK(XWidget_isVisible(a));
    XWidget_hide(b); /* 显式隐藏的子件在 Qt scrollChildren 中同样平移。 */

    XWidget_scroll(top, 5, 7);
    XQ_EQ_I64(XWidget_x(a), 15);
    XQ_EQ_I64(XWidget_y(a), 17);
    XQ_EQ_I64(XWidget_x(b), 45);
    XQ_EQ_I64(XWidget_y(b), 17);

    /* 三参区域版：只滚区域，不移动子控件（qwidget.cpp:11116-11120
     * "does not move the children"）。 */
    XWidget_scrollRect(top, 5, 7, NULL);
    XQ_EQ_I64(XWidget_x(a), 15);
    XQ_EQ_I64(XWidget_y(a), 17);
    XQ_EQ_I64(XWidget_x(b), 45);

    /* 更新禁用但**有子件**：守卫 `(!updatesEnabled && children==0)` 不成立，
     * scrollChildren 仍执行（qwidget.cpp:11089-11093）。 */
    XWidget_setUpdatesEnabled(top, false);
    XWidget_scroll(top, 1, 2);
    XQ_EQ_I64(XWidget_x(a), 16);
    XQ_EQ_I64(XWidget_y(a), 19);
    XWidget_setUpdatesEnabled(top, true);

    XClassDelete((XClass*)top); /* a/b 随父释放。 */
}

/** @brief 焦点策略、Tab 链与焦点归属（qwidget.h:426-447）。 */
XQ_TEST(XWidget, focusChain)
{
    XWidget* host = XWidget_create(NULL, 0);
    XWidget* a = NULL;
    XWidget* b = NULL;
    XWidget* c = NULL;
    XQ_REQUIRE(host != NULL);

    a = XWidget_create(host, 0);
    b = XWidget_create(host, 0);
    c = XWidget_create(host, 0);
    XQ_REQUIRE(a != NULL && b != NULL && c != NULL);

    /* Qt 缺省 focusPolicy = NoFocus（qwidget.h:setFocusPolicy 默认值）。 */
    XQ_EQ_I64(XWidget_focusPolicy(a), XWidgetFocusPolicy_NoFocus);

    XWidget_setFocusPolicy(a, XWidgetFocusPolicy_TabFocus);
    XWidget_setFocusPolicy(b, XWidgetFocusPolicy_TabFocus);
    XWidget_setFocusPolicy(c, XWidgetFocusPolicy_TabFocus);
    /* 显式 show 让三者在「窗口内可达」判据里成为 Tab 候选。 */
    XWidget_show(a);
    XWidget_show(b);
    XWidget_show(c);

    /* Qt 的 setTabOrder 把 second 从原位置摘出并插到 first 之后
     * （qwidget.cpp:6996-6998），故 a→b→c 后反向链同样成立。 */
    XWidget_setTabOrder(a, b);
    XWidget_setTabOrder(b, c);
    XQ_CHECK(XWidget_nextInFocusChain(a) == b);
    XQ_CHECK(XWidget_nextInFocusChain(b) == c);
    XQ_CHECK(XWidget_previousInFocusChain(c) == b);
    XQ_CHECK(XWidget_previousInFocusChain(b) == a);

    /* 焦点落到最深层焦点代理；无代理时就是自身（qwidget.cpp:6576）。 */
    XWidget_setFocus(a);
    XQ_CHECK(XWidget_hasFocus(a));
    XQ_CHECK(XWidget_focusWidget(host) == a);
    XQ_CHECK(!XWidget_hasFocus(b));

    XWidget_setFocus_2(b, XFocusReason_Tab);
    XQ_CHECK(XWidget_hasFocus(b));
    XQ_CHECK(!XWidget_hasFocus(a));

    /* clearFocus 后该顶层无焦点控件。 */
    XWidget_clearFocus(b);
    XQ_CHECK(!XWidget_hasFocus(b));
    XQ_CHECK(XWidget_focusWidget(host) == NULL);

    XClassDelete((XClass*)host);
}

/** @brief 事件分发、事件过滤器与信号集成（qobject.h:eventFilter）。 */
XQ_TEST(XWidget, eventFilterAndSignal)
{
    XWidget* host = XWidget_create(NULL, 0);
    XqProbeWidget* probe = NULL;
    XqFilterObject* filter = NULL;
    XEvent event;
    XString* title = NULL;
    XQ_REQUIRE(host != NULL);

    probe = XqProbe_create(host);
    filter = XqFilter_create(XEVENT_TYPE_WINDOW_TITLE_CHANGE);
    title = XString_create_utf8("align-title");
    XQ_REQUIRE(probe != NULL && filter != NULL && title != NULL);

    /* 无过滤器：事件直达控件的 event()。 */
    XEvent_init(&event, XEVENT_TYPE_WINDOW_TITLE_CHANGE);
    XCoreApplication_sendEvent((XObject*)probe, &event);
    XQ_EQ_I64(probe->m_titleChanges, 1);

    /* 装上吞该类型的过滤器（Qt：installEventFilter 后通知先经过滤器，
     * 返回 true 即止步，接收者收不到事件）。 */
    XObject_installEventFilter((XObject*)probe, (XObject*)filter);
    XCoreApplication_sendEvent((XObject*)probe, &event);
    XQ_EQ_I64(probe->m_titleChanges, 1); /* 未再增长。 */
    XQ_CHECK(filter->m_calls >= 1);

    /* 摘下过滤器后恢复直达。 */
    XObject_removeEventFilter((XObject*)probe, (XObject*)filter);
    XCoreApplication_sendEvent((XObject*)probe, &event);
    XQ_EQ_I64(probe->m_titleChanges, 2);

    /* 信号集成：setWindowTitle 发 windowTitleChanged（XWidget.c:9209）。 */
    {
        XConnection* conn = XObject_connect(
            (XObject*)probe,
            (size_t)XWidget_windowTitleChanged_signal(NULL, NULL),
            (XObject*)probe, xq_titleChangedSlot, XConnectionType_Direct);
        XQ_CHECK_PTR(conn);
        s_titleSignalCount = 0;
        XWidget_setWindowTitle((XWidget*)probe, title);
        XQ_EQ_I64(s_titleSignalCount, 1);
    }

    XClassDelete((XClass*)title);
    XClassDelete((XClass*)filter);
    XClassDelete((XClass*)host); /* probe 随 host 析构。 */
}

/** @brief 可见性/启用传播、坐标映射与屏幕归属（qwidget.h:472-520）。 */
XQ_TEST(XWidget, visibleEnabledMappingAndScreen)
{
    XWidget* host = XWidget_create(NULL, 0);
    XWidget* a = NULL;
    XWidget* b = NULL;
    XPoint local;
    XPoint got;
    XQ_REQUIRE(host != NULL);

    a = XWidget_create(host, 0);
    b = XWidget_create(host, 0);
    XQ_REQUIRE(a != NULL && b != NULL);

    /* Qt：新建子控件不置 WA_WState_Hidden（随父显示自动可见）；
     * 顶层控件初始 Hidden（qwidget.cpp:QWidgetPrivate::init）。 */
    XQ_CHECK(!XWidget_isHidden(a));
    XQ_CHECK(XWidget_isHidden(host));
    XQ_CHECK(!XWidget_isVisible(a)); /* 父未显示 ⇒ 生效可见为假。 */
    XQ_CHECK(XWidget_isVisibleTo(a, host));

    XWidget_hide(a);
    XQ_CHECK(XWidget_isHidden(a));
    XQ_CHECK(!XWidget_isVisibleTo(a, host));
    XWidget_show(a);
    XQ_CHECK(!XWidget_isHidden(a));

    /* 禁用沿父链传播（qwidget.cpp:3400-3411 setEnabled_helper 族）。 */
    XQ_CHECK(XWidget_isEnabled(a));
    XWidget_setEnabled(host, false);
    XQ_CHECK(!XWidget_isEnabled(host));
    XQ_CHECK(!XWidget_isEnabled(a));
    XQ_CHECK(!XWidget_isEnabled(b));
    XWidget_setEnabled(host, true);
    XQ_CHECK(XWidget_isEnabled(a));

    /* 坐标映射：子控件本地 → 父 → 兄弟控件。 */
    XWidget_setGeometry(a, 10, 20, 50, 40);
    XWidget_setGeometry(b, 100, 200, 50, 40);
    local.x = 0;
    local.y = 0;
    got = XWidget_mapToParent(a, &local);
    XQ_CHECK(got.x == 10 && got.y == 20);
    got = XWidget_mapFromParent(a, &local);
    XQ_CHECK(got.x == -10 && got.y == -20);
    got = XWidget_mapTo(a, b, &local);
    XQ_CHECK(got.x == -90 && got.y == -180);
    /* 方向语义（对齐 Qt `QWidget::mapFrom(const QWidget *parent, pos)`：
     * 第一个参数是**源**、self 是**目标**，即 `b.mapFrom(a, p)` 与
     * `a.mapTo(b, p)` 同向，都是 a→b；逆向必须 `a.mapFrom(b, p)`）。
     * 本机实测同框架口径：a→b 的位移是 (a.pos-b.pos)=(-90,-180)，
     *   XWidget_mapTo(a,b,{0,0})        = (-90,-180)
     *   XWidget_mapFrom(b,a,(-90,-180)) = (-180,-360)  ← 同向再平移一次
     *   XWidget_mapFrom(a,b,(-90,-180)) = (0,0)        ← 逆向才回原点
     * 旧断言把 b.mapFrom(a,·) 当逆向，方向写反 ⇒ 恒失败。 */
    {
        XPoint there = XWidget_mapTo(a, b, &local);
        XPoint same = XWidget_mapFrom(b, a, &there);
        XPoint back = XWidget_mapFrom(a, b, &there);
        XQ_CHECK_MSG(same.x == -180 && same.y == -360,
                     "mapFrom(目标=b, 源=a) 应与 mapTo(a→b) 同向：(%d,%d)",
                     same.x, same.y);
        XQ_CHECK_MSG(back.x == local.x && back.y == local.y,
                     "mapFrom(目标=a, 源=b) 才是逆向：(%d,%d) 期望 (%d,%d)",
                     back.x, back.y, local.x, local.y);
    }

    /* 屏幕归属（qwidget.h `screen/setScreen`）：无显式设置时回落到主屏幕
     * （Qt 同：QWidget::screen() 末段回 QGuiApplication::primaryScreen()）。 */
#if XSCREEN_ON
    {
        XScreen* primary = XScreen_primaryScreen();
        XScreen* got2 = XWidget_screen(a);
        XQ_CHECK(got2 == primary);
        XWidget_setScreen(host, primary);
        XQ_CHECK(XWidget_screen(host) == primary);
        XQ_CHECK(XWidget_screen(a) == primary); /* 子控件继承顶层归属。 */
        XWidget_setScreen(host, NULL);
        XQ_CHECK(XWidget_screen(host) == primary); /* 清空后回主屏幕。 */
    }
#endif /* XSCREEN_ON */

    XClassDelete((XClass*)host);
}

/* ==================== grab 快照 DPR 契约（Qt QWidget::grab 对齐） ==================== */

/**
 * XWidget_grab 路径 2 / XWidget_grab_2 的输出规格契约（2026-11-12 DPR
 * 统一后，a4d9af05 合规扫描 P1#12 整改）：
 * - 输出恒带 dpr 标签：路径 1（顶层后备深拷贝）= 后备图像标签；
 *   路径 2/grab_2 = round(逻辑×dpr) 物理画布 + 同标签
 *   （Qt qwidget.cpp:5260-5262 `res(r.size()*dpr)` +
 *   `setDevicePixelRatio(dpr)`）。
 * - dpr=1（本宿主无平台窗口，paintImage 为 NULL→1.0）下：像素尺寸 ==
 *   控件逻辑尺寸、标签 1.0 —— 与统一前逐位一致（本用例锁该旧链）。
 * dpr>1 规格需带后备存储的顶层窗口，测试宿主无平台集成窗口不可达；
 * 由桌面/安卓 DPR 管线在真机覆盖。此处锁三个可离屏观测的落点：
 * 标签恒存在、dpr=1 逐位不变、grab_2 钳位/空交叠语义不变。
 */
XQ_TEST(XWidget, grabSnapshotDprContract)
{
    XWidget* host = XWidget_create(NULL, 0);
    XWidget* child;
    XPalette pal;
    XColor red;
    XImage* shot;
    XRect sub;
    XColor c;

    XQ_REQUIRE(host != NULL);
    XWidget_setGeometry(host, 0, 0, 40, 30);
    XColor_init_rgb(&red, 255, 0, 0, 255);
    pal = XWidget_palette(host);
    XPalette_setColor(&pal, XPaletteColorGroup_Active,
                      XPaletteColorRole_Window, red);
    XWidget_setPalette(host, &pal);
    XWidget_setAutoFillBackground(host, true);

    /* 路径 2（非窗口控件必走同步重绘）：像素尺寸=逻辑尺寸、dpr 标签
       1.0、autoFillBackground 背景回填到快照（Qt grab 的
       DrawWindowBackground 等效语义，同 XGraphicsBlurEffectAlignTest ⑤
       的 alpha 断言口径）。 */
    shot = XWidget_grab((const XWidget*)host);
    XQ_REQUIRE(shot != NULL);
    XQ_EQ_I64(XImage_width(shot), 40);
    XQ_EQ_I64(XImage_height(shot), 30);
    XQ_CHECK_MSG(XImage_devicePixelRatio(shot) == 1.0f,
                 "dpr=1 宿主下 grab 输出 dpr 标签恒 1.0（实得 %f）",
                 (double)XImage_devicePixelRatio(shot));
    c = XImage_pixelColor(shot, 10, 10);
    XQ_CHECK_MSG(XColor_alpha(&c) == 255 && XColor_red(&c) == 255 &&
                     XColor_green(&c) == 0 && XColor_blue(&c) == 0,
                 "autoFillBackground 背景色应回填进快照（实得 a=%d r=%d "
                 "g=%d b=%d）",
                 XColor_alpha(&c), XColor_red(&c), XColor_green(&c),
                 XColor_blue(&c));
    XClassDelete((XClass*)shot);

    /* grab_2 子矩形：与 rect() 求交钳位（子控件局部坐标），输出尺寸=
       交叠矩形（Qt res 尺寸=r 交叠尺寸×dpr；dpr=1 即交叠尺寸）、标签
       同源 1.0。child 20x10 @ (5,4)：请求 (10,6,100,100) → 钳位
       (10,6,10,4)。 */
    child = XWidget_create(host, 0);
    XQ_REQUIRE(child != NULL);
    XWidget_setGeometry(child, 5, 4, 20, 10);
    XRect_init(&sub, 10, 6, 100, 100);
    shot = XWidget_grab_2((const XWidget*)child, &sub);
    XQ_REQUIRE(shot != NULL);
    XQ_EQ_I64(XImage_width(shot), 10);
    XQ_EQ_I64(XImage_height(shot), 4);
    XQ_CHECK_MSG(XImage_devicePixelRatio(shot) == 1.0f,
                 "grab_2 输出 dpr 标签恒 1.0（dpr=1 宿主）");
    XClassDelete((XClass*)shot);

    /* 交叠为空 → NULL（Qt `!r.intersects(rect())` 拒绝口径）。 */
    XRect_init(&sub, 100, 100, 5, 5);
    shot = XWidget_grab_2((const XWidget*)child, &sub);
    XQ_CHECK(shot == NULL);

    XClassDelete((XClass*)child); /* host 析构亦可带走；显式释放成对。 */
    XClassDelete((XClass*)host);
}

#endif /* XGUI_ON && XWIDGET_ON */
