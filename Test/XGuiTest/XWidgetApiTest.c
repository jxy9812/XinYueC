/******************************************************************************
 * @file       XWidgetApiTest.c
 * @brief      XWidget 公开 API 契约回归（本轮 Qt 6.8 对齐改动的护栏）。
 * @details    Test/XGuiTest 下此前无 XWidget 专属测试文件——XWidget 公开
 *             声明 255 个、Test/ 已引用 191 个唯一名，本轮 A 级改动（几何
 *             /焦点/命中/动作/窗口状态）全无护栏。本文件按契约维度覆盖：
 *               ① API 存在性与 NULL 入参不崩（公开 API 全量冒烟）；
 *               ② 几何不变式（rect 全幅 / contentsRect 扣边距）；
 *               ③ focusNextPrevChild / setVisible 虚槽覆写点（子类可接管）；
 *               ④ ensurePolished 链（POLISH → 子控件 → CHILD_POLISHED）；
 *               ⑤ winId / effectiveWinId 转发；
 *               ⑥ 父链守卫（isEnabledTo 非控件父挂链冒烟，missed#4 护栏）。
 *             断言按**修后语义**书写（对齐 Qt 6.8.3 源码口径）。
 * @note       自带 main 的独立可执行（XWidgetApi_Test 目标，接入 ctest）。
 *             头文件只依赖 XWidget 公开 API + XObject 信号框架，不依赖
 *             平台窗口（无头可跑）。
 * @author     XinYueC 团队
 ******************************************************************************/
#if XGUI_ON

#include "XWidget.h"
#include "XWidget_Protected.h"
#include "XObject.h"
#include "XVarList.h"
#include "XString.h"
#include "XGeometry.h"

#include <stdio.h>
#include <string.h>

/* ==================== 断言框架 ==================== */

static int g_failures = 0;
static int g_checks = 0;

static void wapi_expect(bool cond, const char* what)
{
    ++g_checks;
    if (!cond) {
        fprintf(stderr, "[WAPI-FAIL] %s\n", what ? what : "");
        ++g_failures;
    }
}

/* ==================== 测试用控件派生 ==================== */

/**
 * @brief 覆写 FocusNextPrevChild / SetVisible 两个虚槽的测试控件。
 * @details 验证本轮新增的两个 protected 虚槽可被子类接管：
 *          FocusNextPrevChild 被调用时记录 next 参数并返回 true；
 *          SetVisible 被调用时记录目标可见态。
 *          虚表枚举（XCLASS_DEFINE_BEGING/END）在此文件内就地声明——
 *          本测试控件不入公开头（仅本套件内部使用）。
 */
XCLASS_DEFINE_BEGING(WapiProbeWidget)
XCLASS_DEFINE_EXTEND_END(WapiProbeWidget, XWidget)

typedef struct WapiProbeWidget
{
    XWidget m_base;        /**< 基类成员；必须是第一个。 */
    int m_focusNextCalls;  /**< focusNextPrevChild 被调次数。 */
    bool m_focusNextValue; /**< focusNextPrevChild 收到的 next 参数。 */
    int m_setVisibleCalls;  /**< setVisible 被调次数。 */
    bool m_setVisibleValue;/**< setVisible 收到的目标可见态。 */
    bool m_callBase;       /**< 覆写槽是否转调基类实现。 */
    int m_polishCount;     /**< 收到的 POLISH 事件数。 */
    int m_childPolishedCount; /**< 收到的 CHILD_POLISHED 事件数。 */
} WapiProbeWidget;

static XVtable* WapiProbeWidget_class_init(void);

static bool WapiProbeWidget_focusNextPrevChild(XWidget* self, bool next)
{
    WapiProbeWidget* probe = (WapiProbeWidget*)self;
    ++probe->m_focusNextCalls;
    probe->m_focusNextValue = next;
    if (probe->m_callBase) {
        /* 基类实现直取（XClass_Parent 走 XWidget 自己的虚表槽，不回灌本
         * 覆写槽——否则经 XWidget_focusNextPrevChild_base 的虚表分派会
         * 再次回到本覆写 → 无限递归）。 */
        return XClass_Parent(XWidget, EXWidget_FocusNextPrevChild,
                             bool(*)(XWidget*, bool))(self, next);
    }
    return true; /* 接管：自行消费，不让基类继续遍历。 */
}

static void WapiProbeWidget_setVisible(XWidget* self, bool visible)
{
    WapiProbeWidget* probe = (WapiProbeWidget*)self;
    ++probe->m_setVisibleCalls;
    probe->m_setVisibleValue = visible;
    if (probe->m_callBase) {
        /* 基类实现直取（不走 XWidget_setVisible_base 的虚表分派，见上）。 */
        XClass_Parent(XWidget, EXWidget_SetVisible,
                      void(*)(XWidget*, bool))(self, visible);
    }
}

/**
 * @brief 事件总入口覆写：统计 POLISH / CHILD_POLISHED 事件到达次数。
 * @details 本轮 ensurePolished 链以**事件**（XEVENT_TYPE_POLISH /
 *          XEVENT_TYPE_CHILD_POLISHED）而非信号派发，故用事件覆写计数。
 */
static bool WapiProbeWidget_event(XObject* receiver, XEvent* event)
{
    WapiProbeWidget* probe = (WapiProbeWidget*)receiver;
    XEventType type = XEvent_type(event);
    if (type == XEVENT_TYPE_POLISH) ++probe->m_polishCount;
    else if (type == XEVENT_TYPE_CHILD_POLISHED) ++probe->m_childPolishedCount;
    /* 继续走基类分派（其余事件语义不变）。 */
    return XClass_Parent(XWidget, EXObject_Event,
                         bool(*)(XObject*, XEvent*))((XObject*)receiver, event);
}

static XVtable* WapiProbeWidget_class_init(void)
{
    XVTABLE_INIT_DEFAULT(WapiProbeWidget);
    XVTABLE_INHERIT_XCLASS(XWidget);
    XVTABLE_OVERLOAD_DEFAULT(EXWidget_FocusNextPrevChild,
                             WapiProbeWidget_focusNextPrevChild);
    XVTABLE_OVERLOAD_DEFAULT(EXWidget_SetVisible, WapiProbeWidget_setVisible);
    XVTABLE_OVERLOAD_DEFAULT(EXObject_Event, WapiProbeWidget_event);
    return XVTABLE_DEFAULT;
}

static void WapiProbeWidget_init(WapiProbeWidget* self, XWidget* parent)
{
    if (!self) return;
    XWidget_init(&self->m_base, parent, 0);
    XClassSetVtable(self, WapiProbeWidget);
    self->m_focusNextCalls = 0;
    self->m_focusNextValue = false;
    self->m_setVisibleCalls = 0;
    self->m_setVisibleValue = false;
    self->m_callBase = false;
    self->m_polishCount = 0;
    self->m_childPolishedCount = 0;
}

static WapiProbeWidget* WapiProbeWidget_create(XWidget* parent)
{
    WapiProbeWidget* self = (WapiProbeWidget*)
        XMemory_malloc(sizeof(WapiProbeWidget), XCLASS_DEFAULT_MEMORY_TYPE);
    if (!self) return NULL;
    WapiProbeWidget_init(self, parent);
    return self;
}

/* ==================== 用例 ==================== */

/**
 * @brief ① API 存在性 + NULL 入参不崩（公开 API 全量冒烟）。
 */
static void wapi_test_api_surface(void)
{
    /* 几何族：NULL 入参必须安全返回零值/空操作。 */
    wapi_expect(XWidget_rect(NULL).width == 0, "rect(NULL) 宽度为 0");
    wapi_expect(XWidget_contentsRect(NULL).width == 0,
                "contentsRect(NULL) 宽度为 0");
    wapi_expect(XWidget_contentsMargins(NULL).left == 0,
                "contentsMargins(NULL) 左边距为 0");
    wapi_expect(XWidget_normalGeometry(NULL).width == 0,
                "normalGeometry(NULL) 宽度为 0");
    wapi_expect(XWidget_size(NULL).width == 0, "size(NULL) 宽度为 0");
    wapi_expect(XWidget_geometry(NULL).width == 0,
                "geometry(NULL) 宽度为 0");
    XWidget_setContentsMargins(NULL, 1, 2, 3, 4);
    XWidget_setGeometry(NULL, 0, 0, 10, 10);
    XWidget_setVisible(NULL, true);
    XWidget_show(NULL);
    XWidget_hide(NULL);
    XWidget_setHidden(NULL, true);
    XWidget_adjustSize(NULL);
    XWidget_setAttribute(NULL, XWidgetAttribute_MouseTracking, true);
    wapi_expect(!XWidget_testAttribute(NULL, XWidgetAttribute_MouseTracking),
                "testAttribute(NULL) 为 false");
    wapi_expect(!XWidget_isVisible(NULL), "isVisible(NULL) 为 false");
    wapi_expect(!XWidget_isHidden(NULL), "isHidden(NULL) 为 false");
    wapi_expect(!XWidget_isWindow(NULL), "isWindow(NULL) 为 false");
    wapi_expect(!XWidget_isModal(NULL), "isModal(NULL) 为 false");
    wapi_expect(!XWidget_isWindowModal(NULL), "isWindowModal(NULL) 为 false");
    wapi_expect(!XWidget_isMinimized(NULL), "isMinimized(NULL) 为 false");
    wapi_expect(!XWidget_isActiveWindow(NULL), "isActiveWindow(NULL) 为 false");
    wapi_expect(!XWidget_underMouse(NULL), "underMouse(NULL) 为 false");
    wapi_expect(XWidget_windowState(NULL) == XWindowState_NoState,
                "windowState(NULL) 为 NoState");
    wapi_expect(XWidget_windowModality(NULL) == XWindowModality_NonModal,
                "windowModality(NULL) 为 NonModal");
    wapi_expect(XWidget_contextMenuPolicy(NULL) ==
                    XWidgetContextMenuPolicy_NoContextMenu,
                "contextMenuPolicy(NULL) 为 NoContextMenu");
    wapi_expect(XWidget_windowOpacity(NULL) == 1.0,
                "windowOpacity(NULL) 为 1.0");
    wapi_expect(XWidget_baseSize(NULL).width == 0,
                "baseSize(NULL) 宽度为 0");
    wapi_expect(XWidget_winId(NULL) == 0, "winId(NULL) 为 0");
    wapi_expect(XWidget_effectiveWinId(NULL) == 0,
                "effectiveWinId(NULL) 为 0");

    /* 焦点族。 */
    XWidget_setFocus(NULL);
    XWidget_setFocus_2(NULL, XFocusReason_Other);
    XWidget_clearFocus(NULL);
    XWidget_setFocusPolicy(NULL, XWidgetFocusPolicy_StrongFocus);
    wapi_expect(XWidget_focusPolicy(NULL) == XWidgetFocusPolicy_NoFocus,
                "focusPolicy(NULL) 为 NoFocus");
    wapi_expect(!XWidget_hasFocus(NULL), "hasFocus(NULL) 为 false");
    wapi_expect(XWidget_focusWidget(NULL) == NULL,
                "focusWidget(NULL) 为 NULL");
    wapi_expect(!XWidget_focusNextChild(NULL), "focusNextChild(NULL) 为 false");
    wapi_expect(!XWidget_focusPreviousChild(NULL),
                "focusPreviousChild(NULL) 为 false");
    wapi_expect(!XWidget_focusNextPrevChild_base(NULL, true),
                "focusNextPrevChild_base(NULL) 为 false");

    /* 命中/区域族：NULL 入参返回空区域。 */
    {
        XRegion r;
        XRegion_init(&r);
        wapi_expect(XWidget_childrenRegion(NULL).count == 0,
                    "childrenRegion(NULL) 为空区域");
        wapi_expect(XWidget_visibleRegion(NULL).count == 0,
                    "visibleRegion(NULL) 为空区域");
        wapi_expect(XWidget_visibleRegion_const(NULL) == NULL,
                    "visibleRegion_const(NULL) 为 NULL");
        wapi_expect(XWidget_childrenRegion_const(NULL) == NULL,
                    "childrenRegion_const(NULL) 为 NULL");
        wapi_expect(XWidget_mask_const(NULL) == NULL,
                    "mask_const(NULL) 为 NULL");
        XWidget_childAt(NULL, NULL);
        XWidget_setMask(NULL, &r);
        XWidget_setMask_2(NULL, NULL);
        XWidget_clearMask(NULL);
        XRegion_deinit(&r);
    }

    /* 窗口状态/图标/文本族。 */
    XWidget_setWindowState(NULL, XWindowState_Maximized);
    XWidget_overrideWindowState(NULL, XWindowState_Maximized);
    XWidget_setWindowModality(NULL, XWindowModality_ApplicationModal);
    XWidget_setWindowOpacity(NULL, 0.5);
    XWidget_setWindowIconText(NULL, NULL);
    XWidget_setWindowIcon(NULL, NULL);
    XWidget_setWindowFilePath(NULL, NULL);
    XWidget_setTitleBarWidget(NULL, NULL);
    wapi_expect(XWidget_titleBarWidget(NULL) == NULL,
                "titleBarWidget(NULL) 为 NULL");
    wapi_expect(XWidget_windowIconText(NULL) == NULL,
                "windowIconText(NULL) 为 NULL");
    wapi_expect(XWidget_windowFilePath(NULL) == NULL,
                "windowFilePath(NULL) 为 NULL");

    /* 外观/输入族。 */
    XWidget_setCursor(NULL, NULL);
    XWidget_unsetCursor(NULL);
    XWidget_setStyleSheet(NULL, NULL);
    XWidget_setAccessibleName(NULL, NULL);
    XWidget_setContextMenuPolicy(NULL,
                                XWidgetContextMenuPolicy_DefaultContextMenu);
    XWidget_setUpdatesEnabled(NULL, true);
    XWidget_setToolTip(NULL, NULL);
    XWidget_setWhatsThis(NULL, NULL);
    wapi_expect(XWidget_styleSheet(NULL) == NULL,
                "styleSheet(NULL) 为 NULL");
    wapi_expect(XWidget_accessibleName(NULL) == NULL,
                "accessibleName(NULL) 为 NULL");

    /* polish / 布局族。 */
    XWidget_ensurePolished(NULL);
    XWidget_setLayout(NULL, NULL);
    XWidget_setLayout_ref(NULL, NULL);
    wapi_expect(XWidget_layout(NULL) == NULL, "layout(NULL) 为 NULL");
    wapi_expect(XWidget_layout_const(NULL) == NULL,
                "layout_const(NULL) 为 NULL");
    wapi_expect(XWidget_saveGeometry(NULL) == NULL,
                "saveGeometry(NULL) 为 NULL");
    wapi_expect(!XWidget_restoreGeometry(NULL, NULL),
                "restoreGeometry(NULL) 为 false");

    /* 动作族。 */
    XWidget_addAction(NULL, NULL);
    XWidget_insertAction(NULL, NULL, NULL);
    XWidget_removeAction(NULL, NULL);
    wapi_expect(XWidget_actions(NULL) == NULL, "actions(NULL) 为 NULL");
}

/**
 * @brief ② 几何不变式：rect() 恒为全幅、contentsRect() 扣除边距。
 * @details 对标 Qt 6.8（qwidget.h:890-891 rect() 恒 (0,0,w,h)；
 *          qwidget.cpp:7705-7708 contentsRect() = rect() - margins）。
 *          本轮 behavior_diff#12 修正了 rect() 曾直返 m_contentsRect 的
 *          缺陷——此用例是那道改动的护栏。
 */
static void wapi_test_geometry_invariant(void)
{
    XWidget* w = XWidget_create(NULL, 0);
    XRect rect;
    XRect contents;
    XMargins margins;
    wapi_expect(w != NULL, "几何用例：控件创建");
    if (!w) return;

    XWidget_setGeometry(w, 10, 20, 120, 50);
    rect = XWidget_rect(w);
    wapi_expect(rect.x == 0 && rect.y == 0 && rect.width == 120 &&
                    rect.height == 50,
                "rect() 恒为 (0,0,w,h) 全幅");
    contents = XWidget_contentsRect(w);
    wapi_expect(contents.x == 0 && contents.y == 0 &&
                    contents.width == 120 && contents.height == 50,
                "零边距时 contentsRect() == rect()");

    /* 设边距后：rect() 不变（仍是全幅），contentsRect() 扣边距。 */
    XWidget_setContentsMargins(w, 2, 4, 6, 8);
    rect = XWidget_rect(w);
    wapi_expect(rect.x == 0 && rect.y == 0 && rect.width == 120 &&
                    rect.height == 50,
                "设边距后 rect() 仍为全幅");
    contents = XWidget_contentsRect(w);
    wapi_expect(contents.x == 2 && contents.y == 4 &&
                    contents.width == 120 - 2 - 6 &&
                    contents.height == 50 - 4 - 8,
                "contentsRect() 扣除四边边距");
    margins = XWidget_contentsMargins(w);
    wapi_expect(margins.left == 2 && margins.top == 4 &&
                    margins.right == 6 && margins.bottom == 8,
                "contentsMargins() 往返");

    /* 边距对象重载与四参版等价。 */
    {
        XMargins m;
        XMargins_init(&m, 1, 1, 1, 1);
        XWidget_setContentsMargins_2(w, &m);
        contents = XWidget_contentsRect(w);
        wapi_expect(contents.x == 1 && contents.y == 1 &&
                        contents.width == 120 - 2 &&
                        contents.height == 50 - 2,
                    "setContentsMargins_2 与四参版等价");
    }

    XWidget_setContentsMargins(w, 0, 0, 0, 0);
    contents = XWidget_contentsRect(w);
    wapi_expect(contents.x == 0 && contents.y == 0 &&
                    contents.width == 120 && contents.height == 50,
                "清零边距后 contentsRect() 复原");

    XClassDelete((XClass*)w);
}

/**
 * @brief ③ focusNextPrevChild / setVisible 虚槽覆写点。
 * @details 本轮 behavior_diff#0/#2 新增两个 protected 虚槽：子类覆写后
 *          对应路径必须走子类实现（不被基类吞掉）。
 */
static void wapi_test_virtual_slots(void)
{
    WapiProbeWidget* probe = WapiProbeWidget_create(NULL);
    wapi_expect(probe != NULL, "虚槽用例：控件创建");
    if (!probe) return;

    /* focusNextPrevChild：子类覆写被调用。 */
    wapi_expect(XWidget_focusNextPrevChild_base(&probe->m_base, true),
                "focusNextPrevChild_base 走子类覆写并返回 true");
    wapi_expect(probe->m_focusNextCalls == 1,
                "focusNextPrevChild 覆写被调用 1 次");
    wapi_expect(probe->m_focusNextValue,
                "focusNextPrevChild 覆写收到 next=true");
    XWidget_focusNextPrevChild_base(&probe->m_base, false);
    wapi_expect(probe->m_focusNextCalls == 2,
                "focusNextPrevChild 覆写累计 2 次");
    wapi_expect(!probe->m_focusNextValue,
                "focusNextPrevChild 覆写收到 next=false");

    /* focusNextChild / focusPreviousChild 必须转发到该虚槽（Qt 口径：
     * focusNextChild() 是 focusNextPrevChild(true) 的 inline 包装）。 */
    probe->m_focusNextCalls = 0;
    XWidget_focusNextChild(&probe->m_base);
    wapi_expect(probe->m_focusNextCalls == 1,
                "focusNextChild 转发到 focusNextPrevChild 虚槽");
    wapi_expect(probe->m_focusNextValue,
                "focusNextChild 以 next=true 转发");
    probe->m_focusNextCalls = 0;
    XWidget_focusPreviousChild(&probe->m_base);
    wapi_expect(probe->m_focusNextCalls == 1,
                "focusPreviousChild 转发到 focusNextPrevChild 虚槽");
    wapi_expect(!probe->m_focusNextValue,
                "focusPreviousChild 以 next=false 转发");

    /* setVisible：子类覆写被调用，且 show/hide/setHidden 都走该虚槽。 */
    probe->m_setVisibleCalls = 0;
    XWidget_setVisible(&probe->m_base, true);
    wapi_expect(probe->m_setVisibleCalls == 1,
                "setVisible 覆写被调用 1 次");
    wapi_expect(probe->m_setVisibleValue,
                "setVisible 覆写收到 visible=true");
    probe->m_setVisibleCalls = 0;
    XWidget_show(&probe->m_base);
    wapi_expect(probe->m_setVisibleCalls == 1, "show 走 setVisible 虚槽");
    wapi_expect(probe->m_setVisibleValue, "show 以 visible=true 转发");
    probe->m_setVisibleCalls = 0;
    XWidget_hide(&probe->m_base);
    wapi_expect(probe->m_setVisibleCalls == 1, "hide 走 setVisible 虚槽");
    wapi_expect(!probe->m_setVisibleValue, "hide 以 visible=false 转发");
    probe->m_setVisibleCalls = 0;
    XWidget_setHidden(&probe->m_base, true);
    wapi_expect(probe->m_setVisibleCalls == 1,
                "setHidden 走 setVisible 虚槽");
    wapi_expect(!probe->m_setVisibleValue,
                "setHidden(true) 以 visible=false 转发");

    /* 覆写转调基类时，基类语义仍然生效（可见态真的翻转）。 */
    probe->m_callBase = true;
    probe->m_setVisibleCalls = 0;
    XWidget_setVisible(&probe->m_base, true);
    wapi_expect(probe->m_setVisibleCalls == 1,
                "覆写转调基类：setVisible 覆写被调用");
    wapi_expect(XWidget_isVisible(&probe->m_base),
                "覆写转调基类：基类可见态生效");
    XWidget_setVisible(&probe->m_base, false);
    wapi_expect(!XWidget_isVisible(&probe->m_base),
                "覆写转调基类：基类隐藏态生效");

    XClassDelete((XClass*)probe);
}

/**
 * @brief ④ ensurePolished 链：POLISH → 递归子控件 → CHILD_POLISHED。
 * @details 对标 Qt 6.8（qwidget.cpp:10209-10235）：ensurePolished 先给
 *          自身发 Polish 事件，再自顶向下递归子控件，最后向**父控件**发
 *          ChildPolished 事件。
 */
static void wapi_test_ensure_polished(void)
{
    WapiProbeWidget* parent = WapiProbeWidget_create(NULL);
    WapiProbeWidget* child = WapiProbeWidget_create(&parent->m_base);
    WapiProbeWidget* grand = WapiProbeWidget_create(&child->m_base);
    wapi_expect(parent && child && grand, "polish 用例：控件创建");
    if (!parent || !child || !grand) return;

    XWidget_ensurePolished(&parent->m_base);
    /* 顺序：自身 POLISH → 自顶向下递归子控件 POLISH → 向父发 CHILD_POLISHED。
     * 计数口径：parent 收 1 次 POLISH（自身）+ 1 次 CHILD_POLISHED（child
     * 完成时派发）；child 收 1 次 POLISH（自身）+ 1 次 CHILD_POLISHED（grand
     * 完成时派发）；grand 收 1 次 POLISH（自身，无子故无 CHILD_POLISHED）。 */
    wapi_expect(parent->m_polishCount == 1 &&
                    child->m_polishCount == 1 &&
                    grand->m_polishCount == 1,
                "ensurePolished 链：自身 + 子 + 孙各收 1 次 POLISH");
    wapi_expect(parent->m_childPolishedCount == 1 &&
                    child->m_childPolishedCount == 1 &&
                    grand->m_childPolishedCount == 0,
                "ensurePolished 链：CHILD_POLISHED 派发给父控件");

    /* 幂等：重复调用不再派发（Qt 用 metaObject 缓存判重）。 */
    parent->m_polishCount = 0;
    child->m_polishCount = 0;
    grand->m_polishCount = 0;
    parent->m_childPolishedCount = 0;
    child->m_childPolishedCount = 0;
    grand->m_childPolishedCount = 0;
    XWidget_ensurePolished(&parent->m_base);
    wapi_expect(parent->m_polishCount == 0 && child->m_polishCount == 0 &&
                    grand->m_polishCount == 0 &&
                    parent->m_childPolishedCount == 0 &&
                    child->m_childPolishedCount == 0 &&
                    grand->m_childPolishedCount == 0,
                "ensurePolished 幂等：重复调用零派发");

    XClassDelete((XClass*)grand);
    XClassDelete((XClass*)child);
    XClassDelete((XClass*)parent);
}

/**
 * @brief ⑤ winId / effectiveWinId 转发。
 * @details 本轮 behavior_diff#3 新增：winId 沿 nativeWindow → XWindow
 *          两级跳转取原生窗口 id（未建窗时惰性建柄）；effectiveWinId
 *          只读变体不建窗。子控件无自己的原生窗口 → 返回 0。
 */
static void wapi_test_window_id(void)
{
    XWidget* top = XWidget_create(NULL, 0);
    XWidget* child = XWidget_create(top, 0);
    wapi_expect(top && child, "winId 用例：控件创建");
    if (!top || !child) return;

    /* 【批5 new-code#9 2026-11-06】子控件本身没有原生窗口：
     * winId/effectiveWinId 沿所在顶层句柄取值（Qt 语义，非「子控件
     * 恒返回 0」）；无头环境顶层尚未建柄 → 二者均为 0，此断言仅在
     * 无已建柄顶层的口径下成立。 */
    wapi_expect(XWidget_winId(child) == 0,
                "无已建柄顶层时子控件 winId 为 0（沿顶层取 id）");
    wapi_expect(XWidget_effectiveWinId(child) == 0,
                "无已建柄顶层时子控件 effectiveWinId 为 0");

    /* 顶层窗口：effectiveWinId 为只读（未建窗时 0）；winId 惰性建柄后
     * 两者一致（非 0 表示平台有原生窗口；无头环境可能仍为 0，只断言二者
     * 互相一致这一契约）。 */
    {
        XWindowId id = XWidget_winId(top);
        XWindowId effId = XWidget_effectiveWinId(top);
        wapi_expect(id == effId,
                    "winId 与 effectiveWinId 同源（建窗后一致）");
    }

    XClassDelete((XClass*)child);
    XClassDelete((XClass*)top);
}

/**
 * @brief ⑥ 父链守卫：isEnabledTo 在非控件父挂链下的冒烟断言。
 * @details 【二轮对齐 missed#4 护栏 2026-11-06】isVisibleTo/isEnabledTo/
 *          偏移累加三处父链上溯改为经 XWidget_parentWidget（m_is_widget
 *          校验，与 XWidget_topLevel 同口径）——绕过控件 API 把控件挂到
 *          普通 XObject 下后，上溯必须把非控件父视同无父控件终止。修复
 *          前裸 XObject_parent 上溯会按 XWidget* 解引用非控件结构体
 *          （越界读 m_attributes/m_isWindow；Qt 父链遍历一律经
 *          parentWidget()，仅控件可挂父——qwidget.cpp:3112-3122
 *          isEnabledTo 同口径）。正常控件父链语义同步护栏（禁用父 →
 *          isEnabledTo 随链观测 ForceDisabled）。
 */
static void wapi_test_parent_chain_guards(void)
{
    XObject* plain = XObject_create();
    XWidget* other = XWidget_create(NULL, 0);
    XWidget* top = XWidget_create(NULL, 0);
    XWidget* child = XWidget_create(top, 0);
    XWidget* w = XWidget_create(NULL, 0);
    wapi_expect(plain && other && top && child && w,
                "父链守卫用例：对象创建");
    if (!plain || !other || !top || !child || !w) {
        if (w) XClassDelete((XClass*)w);
        if (child) XClassDelete((XClass*)child);
        if (top) XClassDelete((XClass*)top);
        if (other) XClassDelete((XClass*)other);
        if (plain) XClassDelete((XClass*)plain);
        return;
    }

    /* 正常控件父链：语义不因上溯改走防护版而变化——禁用 top 后沿链
     * 可观测 ForceDisabled（top 自身），重新启用恢复。 */
    XWidget_setEnabled(top, false);
    wapi_expect(!XWidget_isEnabledTo(child, NULL),
                "禁用 top 后 isEnabledTo(child) 随链为 false");
    XWidget_setEnabled(top, true);
    wapi_expect(XWidget_isEnabledTo(child, NULL),
                "重新启用 top 后 isEnabledTo(child) 恢复 true");

    /* 冒烟断言（本用例主靶）：非控件父挂链下 isEnabledTo 安全终止——
     * 把 w 挂到普通 XObject 下，上溯立即视同父链末端，返回 w 自身
     * 启用态（默认 true），不越界读非控件结构体。 */
    XObject_setParent((XObject*)w, plain);
    wapi_expect(XWidget_parentWidget(w) == NULL,
                "非控件父：parentWidget 视同无父控件");
    wapi_expect(XWidget_isEnabledTo(w, other),
                "isEnabledTo 经非控件父挂链安全终止（返回自身启用态）");

    XObject_setParent((XObject*)w, NULL);
    XClassDelete((XClass*)w);
    XClassDelete((XClass*)child);
    XClassDelete((XClass*)top);
    XClassDelete((XClass*)other);
    XClassDelete((XClass*)plain);
}

/* ==================== 入口 ==================== */

int main(void)
{
    wapi_test_api_surface();
    wapi_test_geometry_invariant();
    wapi_test_virtual_slots();
    wapi_test_ensure_polished();
    wapi_test_window_id();
    wapi_test_parent_chain_guards();
    fprintf(stdout, "XWidgetApiTest: %d checks, %d failure(s)\n",
            g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}

#else /* !XGUI_ON */

/* GUI 整体裁剪时的非空翻译单元哨兵。 */
typedef int xgui_widget_api_test_disabled_sentinel;

int main(void)
{
    return 0;
}

#endif /* XGUI_ON */
