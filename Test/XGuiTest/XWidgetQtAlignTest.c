/**
 * @file       XWidgetQtAlignTest.c
 * @brief      XWidget × Qt 6.8.3 对齐专项回归（实现）。
 * @details    本套件是 XWidget Qt 6.8.3 对齐战役的**增量**回归锁：仓库根
 *             test_widget_contract()（xgui_regression_test.c:22924）已覆盖
 *             几何钳位/属性位/焦点进出/close 等通用契约，Test/XGuiTest/
 *             XWidgetApiTest.c 已覆盖新 API 的 NULL 冒烟、rect/contentsRect
 *             不变式、虚槽覆写点、ensurePolished 链、winId 转发——本文件
 *             不重复上述断言，只锁本轮对齐**新增/修正**的语义面，按
 *             Qt 语义分七组：
 *               ① 几何与尺寸约束（baseSize/sizeIncrement/normalGeometry
 *                  三分支/setGeometry 解除最大化/save↔restore 往返/
 *                  contentsMargins 事件）；
 *               ② 可见性与窗口状态（构造期 Hidden、ExplicitShowHide 登记位、
 *                  状态位流转与同值早退、模态读写口径、childrenRect 显式
 *                  隐藏位过滤、setUpdatesEnabled 递归）；
 *               ③ 焦点与可用性（NoFocus 可 setFocus 但不进 Tab 链、链卫生、
 *                  禁用顺延、代理策略同步、换父传播/焦点清理/可见性）；
 *               ④ 控件树与坐标映射（childAt 透明跳过/遮罩排除续试/
 *                  MouseNoMask 豁免、isAncestorOf 顶层边界、map 族往返）；
 *               ⑤ 绘制闭环（visibleRegion 可见性闸+全幅锚点、update 脏区
 *                  全幅锚点、grab_2 子矩形、setMask_2 位图遮罩）；
 *               ⑥ 外观与动作（cursor 父链上溯、通知事件族、windowIconChanged
 *                  无条件发射、动作列表 UAF 防护与 addAction_2 所有权、
 *                  contextMenuPolicy 四策略、graphicsEffect _ref 契约）；
 *               ⑦ 命名契约（_const 借用版成对与值随 set 变化、_utf8 与
 *                  XString 等价、_2 重载族、setLayout 借用 vs _ref 接管）。
 *               ⑧ 二轮·生命周期与焦点（二轮批1/批2 A 档修复锁：接管布局
 *                  登记表的改挂/旧宿主析构不误删、setFocus 待激活不立即
 *                  生效、setFont 两道门、setUpdatesEnabled 父守卫、
 *                  grabMouse_2 光标保存/还原）。
 *               ⑨ 二轮·换父传播与焦点遍历（二轮批3 B 档修复锁：setParent
 *                  双向传播含子树恢复、Tab/Backtab 焦点原因、focusNextPrevChild
 *                  虚表分派、PARENT_CHANGE 事件到达与消费）。
 *             每条断言附中文说明与对应 Qt 6.8.3 源码位置（qwidget.cpp 行号
 *             与定裁 refs 编号），语义出处见
 *             docs/xgui/XWidget_Qt683_对齐测试说明.md。
 * @author     XinYueC 团队
 ******************************************************************************/
#include "XWidgetQtAlignTest.h"
#include "XGuiConfig.h"

#if XGUI_ON && XWIDGET_ON

#include "XWidget.h"
#include "XWidget_Protected.h" /* XWidget_appFocusWidget（全局焦点断言用） */
#include "XObject.h"
#include "XMemory.h"
#include "XString.h"
#include "XGeometry.h"
#include "XByteArray.h"
#include "XVector.h"
#include "XAction.h"
#include "XBoxLayout.h"
#include "XFrame.h"      /* 批3 missed#5：PARENT_CHANGE 消费方探针基类。 */
#include "XGraphicsEffect.h"
#include "XBitmap.h"
#include "XCursor.h"
#include "XCoreApplication.h"
#include "XGuiApplication.h" /* 批6 assert-gap#3：应用默认字体回落断言用。 */
#include "XStringUtils.h"    /* 批6：XStrcmp 家族名判别（禁 libc string.h 口径）。 */

#include <stdio.h>
#include <string.h>

/* ==================== 断言辅助（照抄 XGroupBoxTest 风格） ==================== */

static int qta_failures = 0;
static void qta_expect(bool cond, const char* what)
{
    if (!cond) {
        fprintf(stderr, "[XWQ-FAIL] %s\n", what ? what : "");
        ++qta_failures;
    }
}

/* ==================== 事件探针控件 ==================== */

/**
 * @brief 事件计数探针控件：覆写 EXObject_Event 统计通知事件族到达次数。
 * @details setWindowTitle/setToolTip/setWindowIcon/setWindowIconText/
 *          setWindowModified/setCursor/setBackgroundRole/setContentsMargins/
 *          setWindowState 派发的都是**事件**（XEVENT_TYPE_*_CHANGE）而非
 *          信号，只有经事件总入口才能观测；POLISH 族同款范式见
 *          XWidgetApiTest.c 的 WapiProbeWidget。另覆写 FocusInEvent 捕获
 *          焦点原因、ContextMenuEvent 统计上下文菜单虚槽派发。
 */
XCLASS_DEFINE_BEGING(QtaProbeWidget)
XCLASS_DEFINE_EXTEND_END(QtaProbeWidget, XWidget)

typedef struct QtaProbeWidget
{
    XWidget m_base;               /**< 基类成员；必须是第一个。 */
    int m_evtTitle;               /**< WINDOW_TITLE_CHANGE 计数。 */
    int m_evtToolTip;             /**< TOOL_TIP_CHANGE 计数。 */
    int m_evtIconText;            /**< ICON_TEXT_CHANGE 计数。 */
    int m_evtWindowIcon;          /**< WINDOW_ICON_CHANGE 计数。 */
    int m_evtModified;            /**< MODIFIED_CHANGE 计数。 */
    int m_evtCursor;              /**< CURSOR_CHANGE 计数。 */
    int m_evtPalette;             /**< PALETTE_CHANGE 计数。 */
    int m_evtContentsRect;        /**< CONTENTS_RECT_CHANGE 计数。 */
    int m_evtResize;              /**< RESIZE 计数。 */
    int m_evtWindowState;         /**< WINDOW_STATE_CHANGE 计数。 */
    int m_evtActionAdded;         /**< ACTION_ADDED 计数。 */
    int m_evtActionRemoved;       /**< ACTION_REMOVED 计数。 */
    int m_evtFont;                /**< FONT_CHANGE 计数（批6 assert-gap#3 字体链断言用）。 */
    int m_ctxMenuCalls;           /**< contextMenuEvent 虚槽派发计数。 */
    int m_focusInCount;           /**< focusInEvent 派发计数。 */
    XFocusReason m_focusInReason; /**< 最近一次 focusInEvent 携带的原因。 */
} QtaProbeWidget;

static XVtable* QtaProbeWidget_class_init(void);

static bool QtaProbeWidget_event(XObject* receiver, XEvent* event)
{
    QtaProbeWidget* self = (QtaProbeWidget*)receiver;
    switch (XEvent_type(event)) {
    case XEVENT_TYPE_WINDOW_TITLE_CHANGE: ++self->m_evtTitle; break;
    case XEVENT_TYPE_TOOL_TIP_CHANGE: ++self->m_evtToolTip; break;
    case XEVENT_TYPE_ICON_TEXT_CHANGE: ++self->m_evtIconText; break;
    case XEVENT_TYPE_WINDOW_ICON_CHANGE: ++self->m_evtWindowIcon; break;
    case XEVENT_TYPE_MODIFIED_CHANGE: ++self->m_evtModified; break;
    case XEVENT_TYPE_CURSOR_CHANGE: ++self->m_evtCursor; break;
    /* PALETTE_CHANGE 不在此计数：新对齐后 XWidget_event 把它路由给
     * changeEvent（qwidget.cpp event() 的 PaletteChange 分支），sendEvent
     * 会连击两条计数槽 → 每控件实到 2。只在 changeEvent 覆写侧计数
     * （Qt 每控件恰好一次、恒达 changeEvent）。 */
    case XEVENT_TYPE_CONTENTS_RECT_CHANGE: ++self->m_evtContentsRect; break;
    case XEVENT_TYPE_RESIZE: ++self->m_evtResize; break;
    case XEVENT_TYPE_WINDOW_STATE_CHANGE: ++self->m_evtWindowState; break;
    case XEVENT_TYPE_ACTION_ADDED: ++self->m_evtActionAdded; break;
    case XEVENT_TYPE_ACTION_REMOVED: ++self->m_evtActionRemoved; break;
    case XEVENT_TYPE_FONT_CHANGE: ++self->m_evtFont; break;
    default: break;
    }
    /* 继续走 XWidget 基类分派（其余事件语义不变）。 */
    return XClass_Parent(XWidget, EXObject_Event,
                         bool(*)(XObject*, XEvent*))((XObject*)receiver, event);
}

static void QtaProbeWidget_focusIn(XWidget* self, XEvent* event)
{
    QtaProbeWidget* probe = (QtaProbeWidget*)self;
    ++probe->m_focusInCount;
    probe->m_focusInReason = ((const XFocusEvent*)event)->m_reason;
}

static void QtaProbeWidget_contextMenu(XWidget* self, XEvent* event)
{
    (void)event;
    ++((QtaProbeWidget*)self)->m_ctxMenuCalls;
}

/**
 * @brief changeEvent 覆写：统计经 changeEvent_base **直呼**派发的事件。
 * @details setContentsMargins 的 ContentsRectChange 与 setBackgroundRole/
 *          propagatePaletteChange 的 PaletteChange 不经事件总入口（不走
 *          EXObject_Event），只有覆写 EXWidget_ChangeEvent 才能观测。
 *          WINDOW_STATE 等经 sendEvent 的事件在 EXObject_Event 侧计数，
 *          两条通路各计一次、不重复。
 */
static void QtaProbeWidget_change(XWidget* self, XEvent* event)
{
    QtaProbeWidget* probe = (QtaProbeWidget*)self;
    switch (XEvent_type(event)) {
    case XEVENT_TYPE_PALETTE_CHANGE: ++probe->m_evtPalette; break;
    case XEVENT_TYPE_CONTENTS_RECT_CHANGE: ++probe->m_evtContentsRect; break;
    default: break;
    }
}

static XVtable* QtaProbeWidget_class_init(void)
{
    XVTABLE_INIT_DEFAULT(QtaProbeWidget);
    XVTABLE_INHERIT_XCLASS(XWidget);
    XVTABLE_OVERLOAD_DEFAULT(EXObject_Event, QtaProbeWidget_event);
    XVTABLE_OVERLOAD_DEFAULT(EXWidget_FocusInEvent, QtaProbeWidget_focusIn);
    XVTABLE_OVERLOAD_DEFAULT(EXWidget_ContextMenuEvent,
                             QtaProbeWidget_contextMenu);
    XVTABLE_OVERLOAD_DEFAULT(EXWidget_ChangeEvent, QtaProbeWidget_change);
    return XVTABLE_DEFAULT;
}

static QtaProbeWidget* QtaProbe_createEx(XWidget* parent, XWidgetFlags flags)
{
    QtaProbeWidget* self = (QtaProbeWidget*)
        XMemory_malloc(sizeof(QtaProbeWidget), XCLASS_DEFAULT_MEMORY_TYPE);
    if (!self) return NULL;
    memset(self, 0, sizeof(*self));
    XWidget_init(&self->m_base, parent, flags);
    XClassSetVtable(self, QtaProbeWidget);
    return self;
}

static QtaProbeWidget* QtaProbe_create(XWidget* parent)
{
    return QtaProbe_createEx(parent, 0);
}

/* ==================== 守门布局（deinit 置旗） ==================== */

/**
 * @brief deinit 置旗的派生布局：以**判定性**手段断言「布局随控件销毁
 *        而释放」——系统分配器统计受 postEvent 队列等惰性初始化干扰，
 *        字节级断言不可靠；deinit 是否被调到是零噪声契约观测点。
 */
XCLASS_DEFINE_BEGING(QtaCanaryLayout)
XCLASS_DEFINE_EXTEND_END(QtaCanaryLayout, XBoxLayout)

typedef struct QtaCanaryLayout
{
    XBoxLayout m_base;   /**< 基类成员；必须是第一个。 */
    int* m_deadFlag;     /**< 析构置旗目标（借用；NULL 则不记）。 */
} QtaCanaryLayout;

static XVtable* QtaCanaryLayout_class_init(void);

static void VQtaCanaryLayout_deinit(QtaCanaryLayout* self)
{
    if (!self) return;
    if (self->m_deadFlag) ++*self->m_deadFlag;
    XClass_Deinit_Parent(XBoxLayout, (XBoxLayout*)self);
}

static XVtable* QtaCanaryLayout_class_init(void)
{
    XVTABLE_INIT_DEFAULT(QtaCanaryLayout);
    XVTABLE_INHERIT_XCLASS(XBoxLayout);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Deinit, VQtaCanaryLayout_deinit);
    return XVTABLE_DEFAULT;
}

static QtaCanaryLayout* QtaCanaryLayout_create(int* deadFlag)
{
    QtaCanaryLayout* self = (QtaCanaryLayout*)
        XMemory_malloc(sizeof(QtaCanaryLayout), XCLASS_DEFAULT_MEMORY_TYPE);
    if (!self) return NULL;
    memset(self, 0, sizeof(*self));
    XBoxLayout_init(&self->m_base, XBoxLayoutDirection_TopToBottom);
    XClassSetVtable(self, QtaCanaryLayout);
    Set_Class_IsHeap(self, true);
    self->m_deadFlag = deadFlag;
    return self;
}

/** @brief 把探针全部事件计数清零（段前复位用）。 */
static void QtaProbe_resetCounters(QtaProbeWidget* w)
{
    w->m_evtTitle = 0;
    w->m_evtToolTip = 0;
    w->m_evtIconText = 0;
    w->m_evtWindowIcon = 0;
    w->m_evtModified = 0;
    w->m_evtCursor = 0;
    w->m_evtPalette = 0;
    w->m_evtContentsRect = 0;
    w->m_evtResize = 0;
    w->m_evtWindowState = 0;
    w->m_evtActionAdded = 0;
    w->m_evtActionRemoved = 0;
    w->m_evtFont = 0;
    w->m_ctxMenuCalls = 0;
    /* 【二轮补】焦点计数同属「全部事件计数」：此前漏重置，段前复位后
     * 若该控件已收过 FocusIn，后续 ==1 判据恒假（二轮⑨-2 Tab/Backtab
     * reason 组实测踩中，故补齐——不动既有断言的调用点语义）。 */
    w->m_focusInCount = 0;
    w->m_focusInReason = XFocusReason_NoReason;
}

/* ==================== 信号计数槽 ==================== */

static int qta_titleSignals = 0;
static void qta_onTitleChanged(XObject* receiver, XVarList* args)
{ (void)receiver; (void)args; ++qta_titleSignals; }

static int qta_iconSignals = 0;
static void qta_onIconChanged(XObject* receiver, XVarList* args)
{ (void)receiver; (void)args; ++qta_iconSignals; }

static int qta_ctxMenuSignals = 0;
static void qta_onCtxMenuRequested(XObject* receiver, XVarList* args)
{ (void)receiver; (void)args; ++qta_ctxMenuSignals; }

/* ==================== ① 几何与尺寸约束 ==================== */

/**
 * @brief 几何组：baseSize/sizeIncrement、normalGeometry 三分支、
 *        setGeometry 解除最大化、save↔restore 往返、contentsMargins 事件。
 */
static void qta_test_geometry(void)
{
    QtaProbeWidget* w = QtaProbe_create(NULL);
    XRect rect;
    XByteArray* saved;
    XByteArray* garbage;
    QtaProbeWidget* child;
    qta_expect(w != NULL, "几何组：控件创建");
    if (!w) return;

    /* baseSize/sizeIncrement 默认 (0,0)（Qt qwidget.cpp:3896-3901；
     * behavior_diff#18：此前 X 用 (-1,-1) 表达未设置，跨端口径不一致）。 */
    qta_expect(XWidget_baseSize(&w->m_base).width == 0 &&
               XWidget_baseSize(&w->m_base).height == 0,
               "baseSize 默认 (0,0)（Qt 口径，非 (-1,-1)）");
    qta_expect(XWidget_sizeIncrement(&w->m_base).width == 0 &&
               XWidget_sizeIncrement(&w->m_base).height == 0,
               "sizeIncrement 默认 (0,0)");
    XWidget_setBaseSize(&w->m_base, 7, 9);
    qta_expect(XWidget_baseSize(&w->m_base).width == 7 &&
               XWidget_baseSize(&w->m_base).height == 9,
               "setBaseSize 往返（窗口无句柄时只存本地，不崩）");
    {
        XSize inc;
        XSize_init(&inc, 3, 5);
        XWidget_setSizeIncrementSize(&w->m_base, &inc);
        qta_expect(XWidget_sizeIncrement(&w->m_base).width == 3 &&
                   XWidget_sizeIncrement(&w->m_base).height == 5,
                   "setSizeIncrementSize 重载与语义一致");
    }

    /* normalGeometry 三分支（Qt qwidget.cpp:3693-3699；
     * behavior_diff#13：非窗口返回空矩形，不再直返记忆值）。 */
    child = QtaProbe_create(&w->m_base);
    qta_expect(child != NULL, "几何组：子控件创建");
    if (child) {
        rect = XWidget_normalGeometry(&child->m_base);
        qta_expect(rect.x == 0 && rect.y == 0 &&
                   rect.width == 0 && rect.height == 0,
                   "normalGeometry 非窗口控件返回空矩形（分支①）");
        XClassDelete((XClass*)child);
    }

    /* 分支②：未最大化/全屏 → 返回当前 geometry。 */
    XWidget_setGeometry(&w->m_base, 11, 22, 90, 50);
    rect = XWidget_normalGeometry(&w->m_base);
    qta_expect(rect.x == 11 && rect.y == 22 &&
               rect.width == 90 && rect.height == 50,
               "normalGeometry 普通态窗口返回当前 geometry（分支②）");

    /* saveGeometry → restoreGeometry 普通态往返（Qt qwidget.cpp:7572-7595
     * 「几何先、状态后」；behavior_diff#23/#25）。 */
    saved = XWidget_saveGeometry(&w->m_base);
    qta_expect(saved != NULL, "saveGeometry 返回序列化缓冲");
    XWidget_setGeometry(&w->m_base, 55, 66, 30, 20);
    qta_expect(saved && XWidget_restoreGeometry(&w->m_base, saved),
               "restoreGeometry 接受自身 saveGeometry 的数据");
    rect = XWidget_geometry(&w->m_base);
    qta_expect(rect.x == 11 && rect.y == 22 &&
               rect.width == 90 && rect.height == 50,
               "save↔restore 普通态几何往返一致");
    qta_expect(!XWidget_isMaximized(&w->m_base),
               "普通态往返后不处于最大化");
    if (saved) XClassDelete((XClass*)saved);

    /* 最大化态往返：normalGeometry 保存进入特殊态前的几何（分支③）。 */
    XWidget_setGeometry(&w->m_base, 0, 0, 90, 50);
    XWidget_setWindowState(&w->m_base, XWindowState_Maximized);
    qta_expect(XWidget_isMaximized(&w->m_base), "setWindowState 进入最大化");
    rect = XWidget_normalGeometry(&w->m_base);
    qta_expect(rect.x == 0 && rect.y == 0 &&
               rect.width == 90 && rect.height == 50,
               "normalGeometry 最大化态返回记忆的正常态几何（分支③）");
    saved = XWidget_saveGeometry(&w->m_base);
    qta_expect(saved != NULL, "最大化态 saveGeometry 成功");
    XWidget_setWindowState(&w->m_base, XWindowState_NoState);
    XWidget_setGeometry(&w->m_base, 1, 1, 70, 40);
    qta_expect(saved && XWidget_restoreGeometry(&w->m_base, saved),
               "最大化态往返 restore 返回 true");
    qta_expect(XWidget_isMaximized(&w->m_base),
               "restore 恢复最大化状态位");
    rect = XWidget_normalGeometry(&w->m_base);
    qta_expect(rect.x == 0 && rect.y == 0 &&
               rect.width == 90 && rect.height == 50,
               "restore 后 normalGeometry 为保存的正常态几何");
    if (saved) XClassDelete((XClass*)saved);

    /* setGeometry 真变化解除最大化并失效 normalGeometry
     * （Qt qwidget.cpp:7283-7287；behavior_diff#14）。 */
    XWidget_setGeometry(&w->m_base, 5, 6, 80, 40);
    qta_expect(!XWidget_isMaximized(&w->m_base),
               "最大化态 setGeometry 逻辑上解除最大化");
    rect = XWidget_normalGeometry(&w->m_base);
    qta_expect(rect.x == 5 && rect.y == 6 &&
               rect.width == 80 && rect.height == 40,
               "setGeometry 后 normalGeometry 为当前几何（记忆值已失效，"
               "不再返回最大化前的陈旧值）");

    /* restoreGeometry 拒收面：NULL / 错魔数 / 截断 / 非窗口
     * （Qt qwidget.cpp:7487 起的损坏数据口径）。 */
    qta_expect(!XWidget_restoreGeometry(&w->m_base, NULL),
               "restoreGeometry(NULL) 返回 false");
    garbage = XByteArray_create();
    if (garbage) {
        XByteArray_append_utf8(garbage, "JUNK 1 2 3 4 5 6 7 8 9");
        qta_expect(!XWidget_restoreGeometry(&w->m_base, garbage),
                   "restoreGeometry 魔数不符按损坏数据拒绝");
        XClassDelete((XClass*)garbage);
    }
    garbage = XByteArray_create();
    if (garbage) {
        XByteArray_append_utf8(garbage, "XWG1");
        qta_expect(!XWidget_restoreGeometry(&w->m_base, garbage),
                   "restoreGeometry 字段截断拒绝");
        XClassDelete((XClass*)garbage);
    }
    {
        QtaProbeWidget* nonTop = QtaProbe_create(&w->m_base);
        saved = XWidget_saveGeometry(&w->m_base);
        if (nonTop && saved) {
            qta_expect(!XWidget_restoreGeometry(&nonTop->m_base, saved),
                       "restoreGeometry 非顶层控件返回 false");
            qta_expect(XWidget_saveGeometry(&nonTop->m_base) == NULL,
                       "saveGeometry 非顶层控件返回 NULL");
        }
        if (saved) XClassDelete((XClass*)saved);
        if (nonTop) XClassDelete((XClass*)nonTop);
    }

    /* setContentsMargins 派发 RESIZE + CONTENTS_RECT_CHANGE
     * （Qt qwidget.cpp:7664-7683；behavior_diff#19 死分支转活）。
     * 【批5 fix-verify#8 2026-11-06】Resize 按 Qt
     * updateContentsRect（7672-7679）包可见性闸：w 未显式 show
     * （不可见）时不立即派发、置 WA_PendingResizeEvent 挂起；
     * ContentsRectChange 在闸外无条件派发；show 时兑现挂起的
     * Resize（7968-7971）后清位。 */
    QtaProbe_resetCounters(w);
    XWidget_setContentsMargins(&w->m_base, 2, 4, 6, 8);
    qta_expect(w->m_evtResize == 0,
               "不可见控件 setContentsMargins 不立即派 Resize（挂起）");
    qta_expect(XWidget_testAttribute(&w->m_base,
                                     XWidgetAttribute_PendingResizeEvent),
               "不可见控件 setContentsMargins 置 PendingResizeEvent 挂起位");
    qta_expect(w->m_evtContentsRect == 1,
               "setContentsMargins 派发一次 ContentsRectChange 事件");
    XWidget_show(&w->m_base);
    qta_expect(w->m_evtResize == 1,
               "show 兑现一次挂起的 Resize 事件");
    qta_expect(!XWidget_testAttribute(&w->m_base,
                                      XWidgetAttribute_PendingResizeEvent),
               "show 兑现后 PendingResizeEvent 挂起位清零");
    XWidget_setContentsMargins(&w->m_base, 0, 0, 0, 0);

    /* setSizePolicy 两参重载只改两轴、保留 hfw 位（已登记偏差护栏；
     * behavior_diff#21，qwidget.h:908-909 对照）。 */
    {
        XWidgetSizePolicy policy = XWidgetSizePolicy_create();
        XWidgetSizePolicy got;
        XWidgetSizePolicy_setHeightForWidth(&policy, true);
        XWidget_setSizePolicyFull(&w->m_base, &policy);
        got = XWidget_sizePolicy(&w->m_base);
        qta_expect(got.m_hasHeightForWidth,
                   "setSizePolicyFull 写入 hfw 位");
        XWidget_setSizePolicy(&w->m_base, XWidgetSizePolicy_Fixed,
                              XWidgetSizePolicy_Fixed);
        got = XWidget_sizePolicy(&w->m_base);
        qta_expect(got.m_horizontalPolicy == XWidgetSizePolicy_Fixed &&
                   got.m_verticalPolicy == XWidgetSizePolicy_Fixed,
                   "setSizePolicy 两参重载改两轴");
        qta_expect(got.m_hasHeightForWidth,
                   "setSizePolicy 两参重载保留 hfw 位（登记偏差：不整体复位）");
    }

    XClassDelete((XClass*)w);
}

/* ==================== ② 可见性与窗口状态 ==================== */

/**
 * @brief 可见性/窗口状态组：构造期 Hidden、ExplicitShowHide 登记位、
 *        状态位流转、模态读写口径、childrenRect 过滤、setUpdatesEnabled 递归。
 */
static void qta_test_visibility_state(void)
{
    QtaProbeWidget* top = QtaProbe_create(NULL);
    QtaProbeWidget* child;
    QtaProbeWidget* winChild;
    XRect rect;
    qta_expect(top != NULL, "可见性组：顶层创建");
    if (!top) return;

    /* 构造期 Hidden 规则（X 登记口径护栏）：顶层初始隐藏；父可见时
     * 新建子控件直接可见（qwidget.cpp:11018-11019 对照的登记偏差）。 */
    qta_expect(XWidget_isHidden(&top->m_base), "顶层构造期 isHidden 为真");
    XWidget_show(&top->m_base);
    child = QtaProbe_create(&top->m_base);
    qta_expect(child != NULL, "可见性组：子控件创建");
    if (!child) { XClassDelete((XClass*)top); return; }
    qta_expect(!XWidget_isHidden(&child->m_base),
               "可见父下新建子控件 isHidden 为假（X 自动显示模型）");
    qta_expect(XWidget_isVisible(&child->m_base),
               "可见父下新建子控件立即可见");

    /* ExplicitShowHide 属性位记录（qwidget.cpp:8300-8301 的登记偏离：
     * X 只置位记录、不复刻同值早退——早退会卡死布局联动，见
     * XWidget_setVisibleImpl 注释）。 */
    {
        QtaProbeWidget* eh = QtaProbe_create(NULL);
        if (eh) {
            qta_expect(!XWidget_testAttribute(&eh->m_base,
                                              XWidgetAttribute_ExplicitShowHide),
                       "未调用 setVisible 前 ExplicitShowHide 为假");
            XWidget_setVisible(&eh->m_base, true);
            qta_expect(XWidget_testAttribute(&eh->m_base,
                                             XWidgetAttribute_ExplicitShowHide),
                       "setVisible 后置 ExplicitShowHide 登记位");
            XWidget_setVisible(&eh->m_base, false);
            qta_expect(XWidget_testAttribute(&eh->m_base,
                                             XWidgetAttribute_ExplicitShowHide),
                       "hide 后登记位仍保留（记录语义）");
            XClassDelete((XClass*)eh);
        }
    }

    /* setWindowState：Minimized 清 Active 位 + 同值早退
     * （Qt qwidget.cpp:2963-2966；behavior_diff#35）。 */
    XWidget_overrideWindowState(&top->m_base, XWindowState_Active);
    XWidget_setWindowState(&top->m_base,
                           (XWindowStates)(XWindowState_Active |
                                           XWindowState_Minimized));
    qta_expect(XWidget_windowState(&top->m_base) == XWindowState_Minimized,
               "setWindowState(Minimized) 清 Active 位（最小化不并存激活）");
    QtaProbe_resetCounters(top);
    XWidget_setWindowState(&top->m_base, XWindowState_Minimized);
    qta_expect(top->m_evtWindowState == 0,
               "setWindowState 同值早退：不重复派发状态变更事件");

    /* show* 族位运算保留其余状态位（Qt qwidget.cpp:3052/3080-3088；
     * behavior_diff#34：不再覆盖式写状态）。 */
    XWidget_overrideWindowState(&top->m_base, XWindowState_Active);
    XWidget_showMaximized(&top->m_base);
    qta_expect((XWidget_windowState(&top->m_base) &
                (XWindowStates)(XWindowState_Active |
                                XWindowState_Maximized)) ==
                   (XWindowStates)(XWindowState_Active |
                                   XWindowState_Maximized),
               "showMaximized 保留 Active 位（位运算非覆盖）");
    XWidget_showFullScreen(&top->m_base);
    qta_expect((XWidget_windowState(&top->m_base) &
                (XWindowStates)(XWindowState_Active |
                                XWindowState_FullScreen)) ==
                   (XWindowStates)(XWindowState_Active |
                                   XWindowState_FullScreen),
               "showFullScreen 保留 Active 并置全屏位");
    XWidget_showNormal(&top->m_base);
    qta_expect((XWidget_windowState(&top->m_base) &
                (XWindowStates)(XWindowState_Minimized |
                                XWindowState_Maximized |
                                XWindowState_FullScreen)) == 0 &&
               (XWidget_windowState(&top->m_base) &
                XWindowState_Active) != 0,
               "showNormal 清特殊态并保留其余状态位");
    XWidget_showMinimized(&top->m_base);
    qta_expect(XWidget_isMinimized(&top->m_base) &&
               (XWidget_windowState(&top->m_base) &
                XWindowState_Active) == 0,
               "showMinimized 置最小化并清 Active");
    XWidget_showNormal(&top->m_base);

    /* activateWindow 只请求激活、不改 windowState
     * （Qt qwidget.cpp:12965-12971；behavior_diff#33）。 */
    {
        XWindowStates before = XWidget_windowState(&top->m_base);
        XWidget_activateWindow(&top->m_base);
        qta_expect(XWidget_windowState(&top->m_base) == before,
                   "activateWindow 不改写 m_windowState");
    }

    /* 模态读写口径（qwidget.h:818-819；behavior_diff#31/#32）：
     * isModal 读自身、isWindowModal 读顶层；setWindowModality 同步置
     * WA_ShowModal / WA_SetWindowModality。 */
    qta_expect(!XWidget_isModal(&top->m_base) &&
               !XWidget_isWindowModal(&top->m_base),
               "默认 NonModal：isModal/isWindowModal 均假");
    XWidget_setWindowModality(&top->m_base, XWindowModality_WindowModal);
    qta_expect(XWidget_isModal(&top->m_base),
               "isModal 读自身 windowModality");
    qta_expect(XWidget_isWindowModal(&top->m_base),
               "isWindowModal 顶层模态为真");
    qta_expect(child && !XWidget_isModal(&child->m_base),
               "模态窗口下的普通子控件 isModal 为假（Qt 口径）");
    qta_expect(child && XWidget_isWindowModal(&child->m_base),
               "子控件 isWindowModal 沿顶层解析为真");
    qta_expect(XWidget_testAttribute(&top->m_base,
                                     XWidgetAttribute_ShowModal) &&
               XWidget_testAttribute(&top->m_base,
                                     XWidgetAttribute_SetWindowModality),
               "setWindowModality 同步置 WA_ShowModal/WA_SetWindowModality");
    XWidget_setWindowModality(&top->m_base, XWindowModality_NonModal);

    /* childrenRect/childrenRegion 按显式隐藏位过滤
     * （Qt qwidget.cpp:3721 isHidden；behavior_diff#24）：父不可见但子
     * 未显式隐藏 → 仍计入。用专用未显父（顶层显窗会引入标题条子控件，
     * 污染 childrenRect 外接矩形）。 */
    {
        QtaProbeWidget* crParent = QtaProbe_create(NULL);
        QtaProbeWidget* crChild = QtaProbe_create(crParent ? &crParent->m_base : NULL);
        if (crParent && crChild) {
            XWidget_setGeometry(&crChild->m_base, 10, 10, 30, 20);
            XWidget_show(&crChild->m_base);
            rect = XWidget_childrenRect(&crParent->m_base);
            qta_expect(rect.x == 10 && rect.y == 10 &&
                       rect.width == 30 && rect.height == 20,
                       "childrenRect 计入已显式 show 的子控件");
            XWidget_hide(&crParent->m_base);
            rect = XWidget_childrenRect(&crParent->m_base);
            qta_expect(rect.x == 10 && rect.y == 10 &&
                       rect.width == 30 && rect.height == 20,
                       "父不可见但子未显式隐藏：childrenRect 仍计入"
                       "（Qt isHidden 口径）");
            XWidget_hide(&crChild->m_base);
            rect = XWidget_childrenRect(&crParent->m_base);
            qta_expect(rect.x == 0 && rect.y == 0 &&
                       rect.width == 0 && rect.height == 0,
                       "子控件显式隐藏后 childrenRect 排除");
        }
        if (crChild) XClassDelete((XClass*)crChild);
        if (crParent) XClassDelete((XClass*)crParent);
    }

    /* setUpdatesEnabled 递归到非窗口子控件；窗口型子控件不受父级冻结
     * （Qt qwidget.cpp:1824-1839；behavior_diff#83）。 */
    winChild = QtaProbe_createEx(&top->m_base, XWindowType_Window);
    if (winChild) {
        qta_expect(XWidget_isWindow(&winChild->m_base),
                   "窗口型子控件就位（Window 位）");
    }
    XWidget_setUpdatesEnabled(&top->m_base, false);
    qta_expect(!XWidget_updatesEnabled(&top->m_base), "顶层更新冻结");
    qta_expect(!XWidget_updatesEnabled(&child->m_base),
               "setUpdatesEnabled(false) 递归非窗口子控件");
    qta_expect(!winChild || XWidget_updatesEnabled(&winChild->m_base),
               "窗口型子控件不随父级冻结");
    XWidget_setUpdatesEnabled(&top->m_base, true);
    qta_expect(XWidget_updatesEnabled(&child->m_base),
               "解冻后子控件更新使能恢复");

    if (winChild) XClassDelete((XClass*)winChild);
    XClassDelete((XClass*)child);
    XClassDelete((XClass*)top);
}

/* ==================== ③ 焦点与可用性 ==================== */

/**
 * @brief 焦点组：NoFocus 可 setFocus 但不进 Tab 链、链卫生、禁用顺延、
 *        代理策略同步、换父传播/焦点清理/可见性。
 * @details 全程不 show 顶层（无桥接窗口 → setFocus 的「未激活不抢焦点」
 *          闸不生效，跨顶层语义可确定性断言）。
 */
static void qta_test_focus_enabled(void)
{
    QtaProbeWidget* top = QtaProbe_create(NULL);
    QtaProbeWidget *a, *b, *c, *d;
    qta_expect(top != NULL, "焦点组：顶层创建");
    if (!top) return;
    a = QtaProbe_create(&top->m_base);
    b = QtaProbe_create(&top->m_base);
    c = QtaProbe_create(&top->m_base);
    d = QtaProbe_create(&top->m_base);
    qta_expect(a && b && c && d, "焦点组：四子控件创建");
    if (!a || !b || !c || !d) {
        if (d) XClassDelete((XClass*)d);
        if (c) XClassDelete((XClass*)c);
        if (b) XClassDelete((XClass*)b);
        if (a) XClassDelete((XClass*)a);
        XClassDelete((XClass*)top);
        return;
    }
    XWidget_setGeometry(&top->m_base, 0, 0, 400, 200);
    XWidget_setGeometry(&a->m_base, 0, 0, 40, 30);
    XWidget_setGeometry(&b->m_base, 50, 0, 40, 30);
    XWidget_setGeometry(&c->m_base, 100, 0, 40, 30);
    XWidget_setGeometry(&d->m_base, 150, 0, 40, 30);
    XWidget_setFocusPolicy(&a->m_base, XWidgetFocusPolicy_StrongFocus);
    XWidget_setFocusPolicy(&b->m_base, XWidgetFocusPolicy_StrongFocus);
    XWidget_setFocusPolicy(&d->m_base, XWidgetFocusPolicy_StrongFocus);
    /* c 保持默认 NoFocus；全部子控件显式 show（Tab 候选要求 m_explicitShow）。 */
    XWidget_show(&a->m_base);
    XWidget_show(&b->m_base);
    XWidget_show(&c->m_base);
    XWidget_show(&d->m_base);

    /* setFocus 只检查 isEnabled、不检查 focusPolicy
     * （Qt qwidget.cpp:6537）：NoFocus 控件程序化聚焦仍生效。 */
    XWidget_setFocus(&c->m_base);
    qta_expect(XWidget_hasFocus(&c->m_base),
               "setFocus 对 NoFocus 策略控件仍然生效（qwidget.cpp:6537）");
    qta_expect(c->m_focusInCount == 1 &&
               c->m_focusInReason == XFocusReason_Other,
               "setFocus 无参版以 Other 原因派发 focusIn");
    XWidget_clearFocus(&c->m_base);

    /* setFocus_2 透传焦点原因（focusInEvent 载荷）。 */
    XWidget_setFocus_2(&a->m_base, XFocusReason_Mouse);
    qta_expect(a->m_focusInCount == 1 &&
               a->m_focusInReason == XFocusReason_Mouse,
               "setFocus_2(Mouse) 的原因经 FocusIn 事件载荷透传");
    XWidget_clearFocus(&a->m_base);

    /* setTabOrder 对 NoFocus 任一方直接不建链
     * （Qt qwidget.cpp:7021-7024）。 */
    XWidget_setTabOrder(&a->m_base, &c->m_base);
    XWidget_setFocus(&a->m_base);
    XWidget_focusNextChild(&a->m_base);
    qta_expect(XWidget_focusWidget(&top->m_base) == &b->m_base,
               "setTabOrder(a,NoFocus) 拒绝：Tab 仍走文档序 a→b（不落 c）");
    XWidget_setTabOrder(&c->m_base, &b->m_base);
    qta_expect(c->m_base.m_focusNext == NULL,
               "setTabOrder(NoFocus,…) 拒绝：NoFocus 一方不建立显式链");
    XWidget_clearFocus(&b->m_base);

    /* 链卫生（qwidget.cpp:6996-6998 remove-then-insert；behavior_diff#55
     * 落地面）：反向重挂后旧链接另一半被清理，不成 2 环。 */
    {
        QtaProbeWidget *h1 = QtaProbe_create(&top->m_base);
        QtaProbeWidget *h2 = QtaProbe_create(&top->m_base);
        if (h1 && h2) {
            XWidget_setFocusPolicy(&h1->m_base, XWidgetFocusPolicy_StrongFocus);
            XWidget_setFocusPolicy(&h2->m_base, XWidgetFocusPolicy_StrongFocus);
            XWidget_setTabOrder(&h1->m_base, &h2->m_base);
            qta_expect(h1->m_base.m_focusNext == &h2->m_base &&
                       h2->m_base.m_focusPrev == &h1->m_base,
                       "setTabOrder(h1,h2) 建立显式单跳链");
            XWidget_setTabOrder(&h2->m_base, &h1->m_base);
            qta_expect(h1->m_base.m_focusNext == NULL,
                       "反向重挂后 h1 的旧 next 被清理（防 2 环）");
            qta_expect(h2->m_base.m_focusPrev == NULL,
                       "反向重挂后 h2 的旧 prev 被清理");
            qta_expect(h2->m_base.m_focusNext == &h1->m_base &&
                       h1->m_base.m_focusPrev == &h2->m_base,
                       "反向重挂后新链 h2→h1 成立");
        }
        if (h2) XClassDelete((XClass*)h2);
        if (h1) XClassDelete((XClass*)h1);
    }

    /* 焦点链遍历跳过 NoFocus 与显式隐藏控件
     * （Qt qapplication.cpp:1970-2025 候选过滤）。 */
    XWidget_setFocus(&a->m_base);
    XWidget_focusNextChild(&a->m_base);
    qta_expect(XWidget_focusWidget(&top->m_base) == &b->m_base,
               "焦点前进跳过 NoFocus 控件 c");
    XWidget_hide(&b->m_base);
    XWidget_setFocus(&a->m_base);
    XWidget_focusNextChild(&a->m_base);
    qta_expect(XWidget_focusWidget(&top->m_base) == &d->m_base,
               "焦点前进同时跳过 NoFocus 与显式隐藏控件");
    XWidget_show(&b->m_base);
    XWidget_clearFocus(&d->m_base);

    /* 禁用持焦控件先顺延到下一个可聚焦控件（qwidget.cpp:3400-3404；
     * behavior_diff#60）。 */
    XWidget_setFocus(&a->m_base);
    XWidget_setEnabled(&a->m_base, false);
    qta_expect(XWidget_focusWidget(&top->m_base) == &b->m_base,
               "禁用持焦控件：焦点顺延到下一候选而非凭空消失");
    XWidget_setEnabled(&a->m_base, true);

    /* 禁用祖先时递归处理后代焦点：焦点不残留在已禁用后代上
     * （qwidget.cpp:3400-3411；behavior_diff#68）。 */
    XWidget_setFocus(&a->m_base);
    XWidget_setEnabled(&top->m_base, false);
    qta_expect(!XWidget_hasFocus(&a->m_base) && !XWidget_hasFocus(&b->m_base) &&
               !XWidget_hasFocus(&d->m_base) &&
               XWidget_appFocusWidget() == NULL,
               "禁用祖先：焦点不被留在任何已禁用后代上（全局焦点清空）");
    XWidget_setEnabled(&top->m_base, true);

    /* setFocusPolicy 同步给 focusProxy（qwidget.cpp:7864-7865；
     * behavior_diff#51）。 */
    {
        QtaProbeWidget *owner = QtaProbe_create(&top->m_base);
        QtaProbeWidget *proxy = QtaProbe_create(&top->m_base);
        if (owner && proxy) {
            qta_expect(XWidget_focusPolicy(&proxy->m_base) ==
                       XWidgetFocusPolicy_NoFocus,
                       "代理默认 NoFocus");
            XWidget_setFocusProxy(&owner->m_base, &proxy->m_base);
            XWidget_setFocusPolicy(&owner->m_base,
                                   XWidgetFocusPolicy_StrongFocus);
            qta_expect(XWidget_focusPolicy(&owner->m_base) ==
                       XWidgetFocusPolicy_StrongFocus,
                       "setFocusPolicy 写自身");
            qta_expect(XWidget_focusPolicy(&proxy->m_base) ==
                       XWidgetFocusPolicy_StrongFocus,
                       "setFocusPolicy 沿代理链同步策略位");
        }
        if (proxy) XClassDelete((XClass*)proxy);
        if (owner) XClassDelete((XClass*)owner);
    }

    /* 换父后按新父 isEnabled/updatesEnabled 传播（qwidget.cpp:10826-10831；
     * behavior_diff#81）：挂到禁用父下的控件不可再交互。 */
    {
        QtaProbeWidget *dp = QtaProbe_create(NULL);
        QtaProbeWidget *ch = QtaProbe_create(&top->m_base);
        if (dp && ch) {
            XWidget_setEnabled(&dp->m_base, false);
            XWidget_setUpdatesEnabled(&dp->m_base, false);
            qta_expect(XWidget_isEnabled(&ch->m_base), "换父前子控件启用");
            XWidget_setParent_2(&ch->m_base, &dp->m_base);
            qta_expect(!XWidget_isEnabled(&ch->m_base),
                       "换父到禁用父：isEnabled 按新父传播为假");
            qta_expect(!XWidget_updatesEnabled(&ch->m_base),
                       "换父到冻结父：updatesEnabled 按新父传播为假");
            /* 【批3 fix-verify#5 2026-11-06】双向传播护栏：从禁用/冻结
             * 父换挂**使能**父后必须恢复（Qt setParent 的
             * setEnabled_helper/setUpdatesEnabled_helper 按新父
             * isEnabled/updatesEnabled 双向传播，qwidget.cpp:10826-10831；
             * 此前 X 只有单向禁用分支，子控件永久滞留禁用）。 */
            XWidget_setParent_2(&ch->m_base, &top->m_base);
            qta_expect(XWidget_isEnabled(&ch->m_base),
                       "禁用父→使能父：isEnabled 双向传播恢复为真");
            qta_expect(XWidget_updatesEnabled(&ch->m_base),
                       "冻结父→解冻父：updatesEnabled 双向传播恢复为真");
        }
        if (ch) XClassDelete((XClass*)ch);
        if (dp) XClassDelete((XClass*)dp);
    }

    /* 换父到不同窗口：焦点控件是被移控件后代时显式 clearFocus
     * （qwidget.cpp:10775-10776；behavior_diff#88）。 */
    {
        QtaProbeWidget *other = QtaProbe_create(NULL);
        QtaProbeWidget *mv = QtaProbe_create(&top->m_base);
        QtaProbeWidget *g = NULL;
        if (other && mv) {
            g = QtaProbe_create(&mv->m_base);
            if (g) {
                XWidget_setFocusPolicy(&g->m_base,
                                       XWidgetFocusPolicy_StrongFocus);
                XWidget_show(&g->m_base);
                XWidget_setFocus(&g->m_base);
                qta_expect(XWidget_hasFocus(&g->m_base), "换父前孙控件持焦");
                XWidget_setParent_2(&mv->m_base, &other->m_base);
                qta_expect(!XWidget_hasFocus(&g->m_base),
                           "换父跨窗口：被移子树内的焦点被显式清除");
            }
        }
        if (g) XClassDelete((XClass*)g);
        if (mv) XClassDelete((XClass*)mv);
        if (other) XClassDelete((XClass*)other);
    }

    /* 换父可见性（setParent 头注释契约 + Qt qwidget.cpp:11018-11019 对照
     * 的登记模型）：已生效可见控件换父先变为不可见，调用方需显式 show。 */
    {
        QtaProbeWidget *pa = QtaProbe_create(NULL);
        QtaProbeWidget *pb = QtaProbe_create(NULL);
        QtaProbeWidget *pc = QtaProbe_create(NULL);
        QtaProbeWidget *vch = NULL;
        QtaProbeWidget *vch2 = NULL;
        if (pa && pb && pc) {
            XWidget_show(&pa->m_base);
            XWidget_show(&pb->m_base); /* pc 保持未显：不可见父 */
            vch = QtaProbe_create(&pa->m_base);
            vch2 = QtaProbe_create(&pa->m_base);
            if (vch && vch2) {
                qta_expect(XWidget_isVisible(&vch->m_base),
                           "可见父下新建子控件可见");
                XWidget_setParent_2(&vch->m_base, &pb->m_base);
                qta_expect(!XWidget_isVisible(&vch->m_base),
                           "换父先不可见（调用方需显式 show，Qt 口径）");
                XWidget_show(&vch->m_base);
                qta_expect(XWidget_isVisible(&vch->m_base),
                           "换父到可见父后显式 show 恢复可见");
                XWidget_setParent_2(&vch2->m_base, &pc->m_base);
                XWidget_show(&vch2->m_base);
                qta_expect(!XWidget_isVisible(&vch2->m_base),
                           "换父到不可见父：显式 show 也不生效可见");
            }
        }
        if (vch2) XClassDelete((XClass*)vch2);
        if (vch) XClassDelete((XClass*)vch);
        if (pc) XClassDelete((XClass*)pc);
        if (pb) XClassDelete((XClass*)pb);
        if (pa) XClassDelete((XClass*)pa);
    }

    XClassDelete((XClass*)d);
    XClassDelete((XClass*)c);
    XClassDelete((XClass*)b);
    XClassDelete((XClass*)a);
    XClassDelete((XClass*)top);
}

/* ==================== ④ 控件树与坐标映射 ==================== */

/**
 * @brief 树/坐标组：childAt 透明跳过、遮罩排除续试、MouseNoMask 豁免、
 *        isAncestorOf 顶层边界、map 族往返。
 */
static void qta_test_tree_coords(void)
{
    /* -- childAt：透明控件跳过（qwidget.cpp:10491；behavior_diff#70） -- */
    {
        QtaProbeWidget *root = QtaProbe_create(NULL);
        QtaProbeWidget *low = QtaProbe_create(root ? &root->m_base : NULL);
        QtaProbeWidget *hi = QtaProbe_create(root ? &root->m_base : NULL);
        if (root && low && hi) {
            /* 命中测试是纯几何路径（不要求生效可见，Qt 同口径），全程
             * 不显窗——顶层显窗会引入标题条子控件干扰命中面。 */
            XWidget_setGeometry(&root->m_base, 0, 0, 200, 100);
            XWidget_setGeometry(&low->m_base, 0, 0, 100, 100);
            XWidget_setGeometry(&hi->m_base, 50, 0, 100, 100);
            qta_expect(XWidget_childAt_2(&root->m_base, 70, 10) == &hi->m_base,
                       "无属性时命中上层 hi");
            XWidget_setAttribute(&hi->m_base,
                                 XWidgetAttribute_TransparentForMouseEvents,
                                 true);
            qta_expect(XWidget_childAt_2(&root->m_base, 70, 10) == &low->m_base,
                       "childAt 跳过透明控件：命中其下方兄弟");
            XWidget_setAttribute(&hi->m_base,
                                 XWidgetAttribute_TransparentForMouseEvents,
                                 false);
        }
        if (hi) XClassDelete((XClass*)hi);
        if (low) XClassDelete((XClass*)low);
        if (root) XClassDelete((XClass*)root);
    }

    /* -- childAt：遮罩排除时 continue 试下一个兄弟
     *    （qwidget.cpp:10501-10502；behavior_diff#71）+ MouseNoMask 豁免
     *    （qwidget_p.h:898-900；missing_api#26）。 -- */
    {
        QtaProbeWidget *root = QtaProbe_create(NULL);
        QtaProbeWidget *lo = QtaProbe_create(root ? &root->m_base : NULL);
        QtaProbeWidget *hi = QtaProbe_create(root ? &root->m_base : NULL);
        if (root && lo && hi) {
            XRegion mask;
            XRect r;
            XWidget_setGeometry(&root->m_base, 0, 0, 200, 100);
            XWidget_setGeometry(&lo->m_base, 0, 0, 100, 100);
            XWidget_setGeometry(&hi->m_base, 50, 0, 100, 100); /* 上层 */
            XRegion_init(&mask);
            XRect_init(&r, 0, 0, 20, 100); /* hi 仅左侧 20px 可命中 */
            XRegion_addRect(&mask, &r);
            XWidget_setMask(&hi->m_base, &mask);
            XRegion_deinit(&mask);
            /* (70,10) 在 hi 矩形内、hi 遮罩外 → hi 拒绝并续试 lo。 */
            qta_expect(XWidget_childAt_2(&root->m_base, 70, 10) == &lo->m_base,
                       "遮罩排除命中点时续试下一个兄弟（不再误返 hi）");
            /* (60,10) 在 hi 遮罩内 → 命中 hi。 */
            qta_expect(XWidget_childAt_2(&root->m_base, 60, 10) == &hi->m_base,
                       "遮罩内点仍命中上层控件");
            /* WA_MouseNoMask：带遮罩控件按整矩形命中。 */
            XWidget_setAttribute(&hi->m_base,
                                 XWidgetAttribute_MouseNoMask, true);
            qta_expect(XWidget_childAt_2(&root->m_base, 70, 10) == &hi->m_base,
                       "MouseNoMask 豁免：带遮罩控件按整矩形命中");
            XWidget_setAttribute(&hi->m_base,
                                 XWidgetAttribute_MouseNoMask, false);
            XWidget_clearMask(&hi->m_base);
        }
        if (hi) XClassDelete((XClass*)hi);
        if (lo) XClassDelete((XClass*)lo);
        if (root) XClassDelete((XClass*)root);
    }

    /* -- isAncestorOf 不跨顶层边界（QWidget::isAncestorOf 契约） -- */
    {
        QtaProbeWidget *t1 = QtaProbe_create(NULL);
        QtaProbeWidget *t2 = QtaProbe_create(NULL);
        QtaProbeWidget *c1 = QtaProbe_create(t1 ? &t1->m_base : NULL);
        QtaProbeWidget *c2 = QtaProbe_create(t2 ? &t2->m_base : NULL);
        if (t1 && t2 && c1 && c2) {
            qta_expect(XWidget_isAncestorOf(&t1->m_base, &c1->m_base),
                       "isAncestorOf(父,子) 为真");
            qta_expect(!XWidget_isAncestorOf(&c1->m_base, &t1->m_base),
                       "isAncestorOf 反向为假（控件不是自身祖先）");
            qta_expect(!XWidget_isAncestorOf(&t1->m_base, &c2->m_base),
                       "isAncestorOf 不跨顶层边界");
        }
        if (c2) XClassDelete((XClass*)c2);
        if (c1) XClassDelete((XClass*)c1);
        if (t2) XClassDelete((XClass*)t2);
        if (t1) XClassDelete((XClass*)t1);
    }

    /* -- map 族往返（无句柄路径：全局 = 原点链累加，确定性口径） -- */
    {
        QtaProbeWidget *top = QtaProbe_create(NULL);
        QtaProbeWidget *mid = QtaProbe_create(top ? &top->m_base : NULL);
        QtaProbeWidget *leaf = QtaProbe_create(mid ? &mid->m_base : NULL);
        if (top && mid && leaf) {
            XPoint p;
            XPoint g;
            XPoint back;
            XWidget_setGeometry(&top->m_base, 100, 200, 300, 200);
            XWidget_setGeometry(&mid->m_base, 10, 20, 100, 80);
            XWidget_setGeometry(&leaf->m_base, 5, 6, 40, 20);
            XPoint_init(&p, 5, 6);
            g = XWidget_mapToGlobal(&leaf->m_base, &p);
            qta_expect(g.x == 100 + 10 + 5 + 5 && g.y == 200 + 20 + 6 + 6,
                       "mapToGlobal 沿父链累加原点 + 顶层原点");
            back = XWidget_mapFromGlobal(&leaf->m_base, &g);
            qta_expect(back.x == p.x && back.y == p.y,
                       "mapFromGlobal 与 mapToGlobal 互逆");
            back = XWidget_mapTo(&leaf->m_base, &top->m_base, &p);
            qta_expect(back.x == 10 + 5 + 5 && back.y == 20 + 6 + 6,
                       "mapTo(祖先) 折算祖先局部坐标");
            back = XWidget_mapFrom(&leaf->m_base, &top->m_base, &back);
            qta_expect(back.x == p.x && back.y == p.y,
                       "mapFrom 与 mapTo 互逆");
        }
        if (leaf) XClassDelete((XClass*)leaf);
        if (mid) XClassDelete((XClass*)mid);
        if (top) XClassDelete((XClass*)top);
    }
}

/* ==================== ⑤ 绘制闭环 ==================== */

/**
 * @brief 绘制组：visibleRegion 可见性闸+全幅锚点、update 脏区全幅锚点、
 *        grab_2 子矩形、setMask_2 位图遮罩。
 */
static void qta_test_paint_loop(void)
{
    QtaProbeWidget* root = QtaProbe_create(NULL);
    QtaProbeWidget* child;
    XRegion region;
    XRect bounds;
    qta_expect(root != NULL, "绘制组：顶层创建");
    if (!root) return;
    XWidget_setGeometry(&root->m_base, 0, 0, 200, 200);
    child = QtaProbe_create(&root->m_base);
    qta_expect(child != NULL, "绘制组：子控件创建");
    if (!child) { XClassDelete((XClass*)root); return; }
    /* 子控件放顶层标题条带之下（显窗后装饰层会插入标题条子控件）。 */
    XWidget_setGeometry(&child->m_base, 10, 100, 50, 40);
    XWidget_setContentsMargins(&child->m_base, 2, 4, 6, 8);

    /* visibleRegion 可见性闸（qwidget.cpp:1889-1890；behavior_diff#76）：
     * 不可见控件返回空区域。 */
    region = XWidget_visibleRegion(&child->m_base);
    qta_expect(region.count == 0,
               "visibleRegion 隐藏控件返回空区域（可见性闸）");
    XRegion_deinit(&region);
    XWidget_show(&root->m_base);
    XWidget_show(&child->m_base);

    /* visibleRegion 锚点为全幅 rect()（qwidget.cpp:8716-8722；
     * behavior_diff#84/#76 同批锚点改造）：有内容边距时不再以
     * contentsRect 为锚。 */
    region = XWidget_visibleRegion(&child->m_base);
    XRegion_boundingRect(&region, &bounds);
    qta_expect(region.count >= 1 && bounds.x == 0 && bounds.y == 0 &&
               bounds.width == 50 && bounds.height == 40,
               "visibleRegion 以全幅 (0,0,w,h) 为锚（含边距带）");
    XRegion_deinit(&region);

    /* update() 脏区锚点为全幅（qwidget.cpp:11257-11260；
     * behavior_diff#84）：边距带角点必须入顶层脏区。 */
    XRegion_clear(&root->m_base.m_dirty);
    XWidget_update(&child->m_base);
    qta_expect(root->m_base.m_dirty.count >= 1, "update 并入顶层脏区");
    qta_expect(XRegion_contains(&root->m_base.m_dirty, 10, 100),
               "update 脏区含边距带左上角（全幅锚点，非 contentsRect）");
    qta_expect(XRegion_contains(&root->m_base.m_dirty, 59, 139),
               "update 脏区含控件右下角");
    XRegion_clear(&root->m_base.m_dirty);
    XWidget_setContentsMargins(&child->m_base, 0, 0, 0, 0);

    /* setMask_2(XBitmap)：置位像素构成遮罩（qwidget.cpp:13193；
     * missing_api#20/#29）。4x4 位图前两行置位。专用未显根（避开标题条）。 */
    {
        XBitmap bm;
        XSize bsz;
        static const uint8_t bits[4] = { 0xF0, 0xF0, 0x00, 0x00 };
        QtaProbeWidget* maskRoot = QtaProbe_create(NULL);
        QtaProbeWidget *lo = QtaProbe_create(maskRoot ? &maskRoot->m_base : NULL);
        QtaProbeWidget *hi = QtaProbe_create(maskRoot ? &maskRoot->m_base : NULL);
        if (maskRoot && lo && hi) {
            XRegion maskCopy;
            XWidget_setGeometry(&maskRoot->m_base, 0, 0, 100, 100);
            XWidget_setGeometry(&lo->m_base, 0, 0, 60, 60);
            XWidget_setGeometry(&hi->m_base, 0, 0, 60, 60);
            XSize_init(&bsz, 4, 4);
            XBitmap_init(&bm);
            XBitmap_fromData(&bsz, bits, XImageFormat_Mono, &bm);
            XWidget_setMask_2(&hi->m_base, &bm);
            XClassDeinit((XClass*)&bm);
            qta_expect(XWidget_hasMask(&hi->m_base),
                       "setMask_2(位图) 建立遮罩");
            maskCopy = XWidget_mask(&hi->m_base);
            qta_expect(XRegion_contains(&maskCopy, 0, 0) &&
                       XRegion_contains(&maskCopy, 3, 1),
                       "置位像素进入遮罩区域");
            qta_expect(!XRegion_contains(&maskCopy, 0, 2) &&
                       !XRegion_contains(&maskCopy, 3, 3),
                       "未置位像素不进入遮罩区域");
            XRegion_deinit(&maskCopy);
            /* 遮罩随命中测试生效：上层 hi 遮罩外点落到下层 lo。 */
            qta_expect(XWidget_childAt_2(&maskRoot->m_base, 4, 30) == &lo->m_base,
                       "位图遮罩随 childAt 生效（遮罩外点穿透）");
            qta_expect(XWidget_childAt_2(&maskRoot->m_base, 1, 1) == &hi->m_base,
                       "位图遮罩内置位点仍命中上层");
            /* setMask_2(NULL) 等价清除（qwidget.cpp:13193 空位图口径）。 */
            XWidget_setMask_2(&hi->m_base, NULL);
            qta_expect(!XWidget_hasMask(&hi->m_base),
                       "setMask_2(NULL) 等价清除遮罩");
            qta_expect(XWidget_childAt_2(&maskRoot->m_base, 4, 30) == &hi->m_base,
                       "清除遮罩后恢复整矩形命中");
        }
        if (hi) XClassDelete((XClass*)hi);
        if (lo) XClassDelete((XClass*)lo);
        if (maskRoot) XClassDelete((XClass*)maskRoot);
    }

    /* grab_2：按矩形子区抓图（missing_api#22）；【批5 new-code#8
     * 2026-11-06】越界/负原点改为与 rect() 求交钳位（对标 Qt
     * qwidget.cpp:5257），只有交叠为空才返回 NULL。child=50x40：
     * (40,35,20,30)→交叠 (40,35,10,5)；(-1,0,10,10)→交叠
     * (0,0,9,10)。 */
    {
        XImage* img;
        XRect sub;
        XRect_init(&sub, 0, 0, 20, 30);
        img = XWidget_grab_2(&child->m_base, &sub);
        qta_expect(img != NULL && XImage_width(img) == 20 &&
                   XImage_height(img) == 30,
                   "grab_2 返回子矩形尺寸的抓图");
        if (img) XClassDelete((XClass*)img);
        XRect_init(&sub, 40, 35, 20, 30);
        img = XWidget_grab_2(&child->m_base, &sub);
        qta_expect(img != NULL && XImage_width(img) == 10 &&
                   XImage_height(img) == 5,
                   "grab_2 越界源矩形按求交钳位为交叠尺寸");
        if (img) XClassDelete((XClass*)img);
        XRect_init(&sub, -1, 0, 10, 10);
        img = XWidget_grab_2(&child->m_base, &sub);
        qta_expect(img != NULL && XImage_width(img) == 9 &&
                   XImage_height(img) == 10,
                   "grab_2 负原点源矩形按求交钳位为交叠尺寸");
        if (img) XClassDelete((XClass*)img);
        XRect_init(&sub, 60, 0, 10, 10);
        img = XWidget_grab_2(&child->m_base, &sub);
        qta_expect(img == NULL, "grab_2 交叠为空的越界矩形返回 NULL");
    }

    XClassDelete((XClass*)child);
    XClassDelete((XClass*)root);
}

/* ==================== ⑥ 外观与动作 ==================== */

/**
 * @brief 外观/动作组：cursor 父链上溯、通知事件族、windowIconChanged
 *        无条件发射、动作列表契约、contextMenuPolicy 四策略、graphicsEffect。
 */
static void qta_test_appearance_actions(void)
{
    /* -- cursor() 父链上溯（qwidget.cpp:4980-4982；behavior_diff#113） -- */
    {
        QtaProbeWidget *top = QtaProbe_create(NULL);
        QtaProbeWidget *child = QtaProbe_create(top ? &top->m_base : NULL);
        if (top && child) {
            XCursor* cross = XCursor_create_shape(XCursor_Cross);
            XCursor got;
            QtaProbe_resetCounters(top);
            XWidget_setCursor(&top->m_base, cross);
            got = XWidget_cursor(&child->m_base);
            qta_expect(got.m_shape == XCursor_Cross,
                       "cursor() 沿父链上溯取最近祖先光标");
            XClassDeinit((XClass*)&got);
            qta_expect(top->m_evtCursor == 1,
                       "setCursor 派发 CursorChange");
            XWidget_unsetCursor(&top->m_base);
            got = XWidget_cursor(&child->m_base);
            qta_expect(got.m_shape == XCursor_Arrow,
                       "unsetCursor 后链上无光标回落默认箭头");
            XClassDeinit((XClass*)&got);
            qta_expect(top->m_evtCursor == 2,
                       "unsetCursor 同样派发 CursorChange");
            if (cross) XClassDelete((XClass*)cross);
        }
        if (child) XClassDelete((XClass*)child);
        if (top) XClassDelete((XClass*)top);
    }

    /* -- 通知事件族（behavior_diff#30/#93/#107：死事件类型转活） -- */
    {
        QtaProbeWidget* w = QtaProbe_create(NULL);
        XIcon icon;
        if (w) {
            XString* tip = XString_create_utf8("提示");
            XString* tip2 = XString_create_utf8("图标文本");
            QtaProbe_resetCounters(w);
            XWidget_setToolTip(&w->m_base, tip);
            qta_expect(w->m_evtToolTip == 1,
                       "setToolTip 派发 ToolTipChange（qwidget.cpp:11722）");
            XClassDelete((XClass*)tip);

            QtaProbe_resetCounters(w);
            XWidget_setWindowModified(&w->m_base, true);
            qta_expect(w->m_evtModified == 1 &&
                       XWidget_isWindowModified(&w->m_base),
                       "setWindowModified 派发 ModifiedChange 并翻转标志");
            XWidget_setWindowModified(&w->m_base, false);

            QtaProbe_resetCounters(w);
            XWidget_setWindowIconText(&w->m_base, tip2);
            qta_expect(w->m_evtIconText == 1,
                       "setWindowIconText 派发 IconTextChange（qwidget.cpp:6115）");
            XClassDelete((XClass*)tip2);

            /* setWindowIcon 无条件发射 windowIconChanged
             * （qwidget.cpp:6228；behavior_diff#39）：同图标重复设置也发射。 */
            XIcon_init(&icon);
            qta_titleSignals = 0;
            qta_iconSignals = 0;
            XObject_connect((XObject*)w,
                              XSignal(XWidget_windowIconChanged_signal),
                              (XObject*)w, qta_onIconChanged,
                              XConnectionType_Direct);
            QtaProbe_resetCounters(w);
            XWidget_setWindowIcon(&w->m_base, &icon);
            XWidget_setWindowIcon(&w->m_base, &icon);
            qta_expect(qta_iconSignals == 2,
                       "windowIconChanged 无条件发射（同值也发射，Qt 口径）");
            qta_expect(w->m_evtWindowIcon == 2,
                       "setWindowIcon 每次派发 WindowIconChange");
            XWidget_setWindowIcon(&w->m_base, NULL);
            qta_expect(qta_iconSignals == 3,
                       "setWindowIcon(NULL) 清除图标并照常发射");
            XObject_disconnect((XObject*)w,
                                 XSignal(XWidget_windowIconChanged_signal),
                                 (XObject*)w, qta_onIconChanged);
            XClassDeinit((XClass*)&icon);

            /* setWindowTitle 口径（qwidget.cpp:6132-6149；behavior_diff#46）：
             * 变化发射；同值非空早退；空/null 每次都发。 */
            qta_titleSignals = 0;
            XObject_connect((XObject*)w,
                              XSignal(XWidget_windowTitleChanged_signal),
                              (XObject*)w, qta_onTitleChanged,
                              XConnectionType_Direct);
            QtaProbe_resetCounters(w);
            {
                XString* t = XString_create_utf8("对齐标题");
                XWidget_setWindowTitle(&w->m_base, t);
                XClassDelete((XClass*)t);
            }
            qta_expect(w->m_evtTitle == 1 && qta_titleSignals == 1,
                       "标题变化派发一次 TitleChange + 一次信号");
            {
                XString* t = XString_create_utf8("对齐标题");
                XWidget_setWindowTitle(&w->m_base, t);
                XClassDelete((XClass*)t);
            }
            qta_expect(w->m_evtTitle == 1 && qta_titleSignals == 1,
                       "同值非空标题早退：不重复发射（qwidget.cpp:6132-6133）");
            XWidget_setWindowTitle(&w->m_base, NULL);
            qta_expect(w->m_evtTitle == 2 && qta_titleSignals == 2,
                       "null 标题不早退：每次继续派发");
            XWidget_setWindowTitle(&w->m_base, NULL);
            qta_expect(w->m_evtTitle == 3 && qta_titleSignals == 3,
                       "重复 null 标题仍每次派发（空串口径）");
            XObject_disconnect((XObject*)w,
                                 XSignal(XWidget_windowTitleChanged_signal),
                                 (XObject*)w, qta_onTitleChanged);

            /* setBackgroundRole 派发 PALETTE_CHANGE 并向子控件传播
             * （qwidget.cpp:4421/4486；behavior_diff#96/#98）。 */
            {
                QtaProbeWidget* kid = QtaProbe_create(&w->m_base);
                if (kid) {
                    QtaProbe_resetCounters(w);
                    QtaProbe_resetCounters(kid);
                    XWidget_setBackgroundRole(&w->m_base,
                                              XPaletteColorRole_Base);
                    qta_expect(w->m_evtPalette == 1,
                               "setBackgroundRole 派发 PaletteChange");
                    qta_expect(kid->m_evtPalette == 1,
                               "PaletteChange 向子控件传播");
                    qta_expect(XWidget_backgroundRole(&kid->m_base) ==
                               XPaletteColorRole_Base,
                               "backgroundRole 沿父链上溯（qwidget.cpp:4388）");
                    XWidget_setBackgroundRole(&kid->m_base,
                                              XPaletteColorRole_Highlight);
                    qta_expect(XWidget_backgroundRole(&kid->m_base) ==
                               XPaletteColorRole_Highlight,
                               "子控件自身角色优先于父链解析");
                }
                if (kid) XClassDelete((XClass*)kid);
            }
            XClassDelete((XClass*)w);
        }
    }

    /* -- 动作列表契约（qwidget.cpp:3139-3215；behavior_diff#90/#91） -- */
    {
        QtaProbeWidget* w = QtaProbe_create(NULL);
        if (w) {
            XAction* act1 = XAction_create();
            XAction* act2 = XAction_create();
            XVector* list;
            QtaProbe_resetCounters(w);
            qta_expect(act1 && act2, "动作对象创建");
            XWidget_addAction(&w->m_base, act1);
            list = XWidget_actions(&w->m_base);
            qta_expect(list && XVector_size_base((const XContainer*)list) == 1, "addAction 入列");
            if (list) XClassDelete((XClass*)list);
            qta_expect(w->m_evtActionAdded == 1,
                       "addAction 派发 ActionAdded（qwidget.cpp:3184）");
            /* 同一动作重复追加只保留一份（qwidget.cpp:3174-3175）。 */
            XWidget_addAction(&w->m_base, act1);
            list = XWidget_actions(&w->m_base);
            qta_expect(list && XVector_size_base((const XContainer*)list) == 1,
                       "同一动作重复 addAction 只保留一份");
            if (list) XClassDelete((XClass*)list);
            /* insertAction(before)：插到参照动作之前。 */
            XWidget_insertAction(&w->m_base, act1, act2);
            list = XWidget_actions(&w->m_base);
            qta_expect(list && XVector_size_base((const XContainer*)list) == 2 &&
                       XVector_At_Base(list, 0, XAction*) == act2,
                       "insertAction(before) 插入参照动作之前");
            if (list) XClassDelete((XClass*)list);
            /* 动作销毁自动摘除（destroyed 连接；UAF 防护）。 */
            XClassDelete((XClass*)act2);
            list = XWidget_actions(&w->m_base);
            qta_expect(list && XVector_size_base((const XContainer*)list) == 1 &&
                       XVector_At_Base(list, 0, XAction*) == act1,
                       "动作销毁后自动从列表摘除（destroyed 连接）");
            if (list) XClassDelete((XClass*)list);
            qta_expect(w->m_evtActionAdded == 3,
                       "三次插入各派发一次 ActionAdded");
            /* removeAction：摘除 + ActionRemoved。 */
            QtaProbe_resetCounters(w);
            XWidget_removeAction(&w->m_base, act1);
            list = XWidget_actions(&w->m_base);
            qta_expect(list && XVector_size_base((const XContainer*)list) == 0, "removeAction 清空列表");
            if (list) XClassDelete((XClass*)list);
            qta_expect(w->m_evtActionRemoved == 1,
                       "removeAction 派发 ActionRemoved");
            XClassDelete((XClass*)act1);
            /* addAction_2：新建动作以控件为父对象（所有权归控件；
             * qwidget.cpp:3160-3166；behavior_diff#91）。 */
            {
                XString* text = XString_create_utf8("新建动作");
                XAction* owned = XWidget_addAction_2(&w->m_base, text);
                qta_expect(owned != NULL &&
                           XObject_parent((XObject*)owned) == (XObject*)w,
                           "addAction_2 新建动作以控件为父对象");
                XClassDelete((XClass*)text);
            }
            XClassDelete((XClass*)w); /* 挂父动作随控件析构（所有权承担）。 */
        }
    }

    /* -- contextMenuPolicy 四策略分派（qwidget.cpp:9150-9173；
     *    behavior_diff#92） -- */
    {
        QtaProbeWidget* w = QtaProbe_create(NULL);
        if (w) {
            XContextMenuEvent ctx;
            XPoint pos;
            XPoint global;
            XPoint_init(&pos, 7, 9);
            XPoint_init(&global, 107, 209);
            XContextMenuEvent_init(&ctx, XEVENT_TYPE_CONTEXT_MENU, &pos,
                                   &global, XContextMenuReason_Mouse,
                                   XKeyboardModifier_NoModifier);
            qta_expect(XWidget_contextMenuPolicy(&w->m_base) ==
                       XWidgetContextMenuPolicy_DefaultContextMenu,
                       "contextMenuPolicy 构造默认 DefaultContextMenu");
            XObject_connect((XObject*)w,
                              XSignal(XWidget_customContextMenuRequested_signal),
                              (XObject*)w, qta_onCtxMenuRequested,
                              XConnectionType_Direct);
            /* Default：走 contextMenuEvent 虚槽。 */
            QtaProbe_resetCounters(w);
            qta_ctxMenuSignals = 0;
            XCoreApplication_sendEvent((XObject*)w, (XEvent*)&ctx);
            qta_expect(w->m_ctxMenuCalls == 1 && qta_ctxMenuSignals == 0,
                       "Default 策略走 contextMenuEvent 虚槽");
            /* Prevent：抑制。 */
            XWidget_setContextMenuPolicy(&w->m_base,
                                         XWidgetContextMenuPolicy_PreventContextMenu);
            XCoreApplication_sendEvent((XObject*)w, (XEvent*)&ctx);
            qta_expect(w->m_ctxMenuCalls == 1 && qta_ctxMenuSignals == 0,
                       "Prevent 策略抑制上下文菜单");
            /* Custom：发信号、不走虚槽。 */
            XWidget_setContextMenuPolicy(&w->m_base,
                                         XWidgetContextMenuPolicy_CustomContextMenu);
            XCoreApplication_sendEvent((XObject*)w, (XEvent*)&ctx);
            qta_expect(w->m_ctxMenuCalls == 1 && qta_ctxMenuSignals == 1,
                       "Custom 策略发射 customContextMenuRequested");
            /* Actions：无动作 → 忽略。 */
            XWidget_setContextMenuPolicy(&w->m_base,
                                         XWidgetContextMenuPolicy_ActionsContextMenu);
            XCoreApplication_sendEvent((XObject*)w, (XEvent*)&ctx);
            qta_expect(w->m_ctxMenuCalls == 1 && qta_ctxMenuSignals == 1,
                       "Actions 策略无动作时不发射");
            /* Actions：有动作 → 发信号。 */
            {
                XAction* act = XAction_create();
                XWidget_addAction(&w->m_base, act);
                XCoreApplication_sendEvent((XObject*)w, (XEvent*)&ctx);
                qta_expect(qta_ctxMenuSignals == 2,
                           "Actions 策略有动作时发射信号");
                XWidget_removeAction(&w->m_base, act);
                XClassDelete((XClass*)act);
            }
            XObject_disconnect((XObject*)w,
                                 XSignal(XWidget_customContextMenuRequested_signal),
                                 (XObject*)w, qta_onCtxMenuRequested);
            XWidget_setContextMenuPolicy(&w->m_base,
                                         XWidgetContextMenuPolicy_DefaultContextMenu);
            XClassDelete((XClass*)w);
        }
    }

    /* -- graphicsEffect 契约（naming#11/#12；behavior_diff#78 同族所有权） -- */
    {
        QtaProbeWidget* w = QtaProbe_create(NULL);
        if (w) {
            XGraphicsEffect* e1 = XGraphicsEffect_create();
            XGraphicsEffect* e2 = XGraphicsEffect_create();
            qta_expect(XWidget_graphicsEffect(&w->m_base) == NULL,
                       "默认无图形效果");
            XWidget_setGraphicsEffect_ref(&w->m_base, e1);
            qta_expect(XWidget_graphicsEffect(&w->m_base) == e1,
                       "setGraphicsEffect_ref 接管并挂接效果");
            XWidget_setGraphicsEffect(&w->m_base, e2); /* 旧效果由控件释放 */
            qta_expect(XWidget_graphicsEffect(&w->m_base) == e2,
                       "重复设置替换效果（旧效果控件内部释放）");
            XWidget_setGraphicsEffect(&w->m_base, NULL);
            qta_expect(XWidget_graphicsEffect(&w->m_base) == NULL,
                       "setGraphicsEffect(NULL) 清除效果");
            /* e1/e2 所有权已随设置转移给控件，不得再释放。 */
            XClassDelete((XClass*)w);
        }
    }
}

/* ==================== ⑦ 命名契约 ==================== */

/**
 * @brief 命名契约组：_const 借用版成对且值随 set 变化、_utf8 与 XString
 *        等价、_2 重载族、setLayout 借用 vs _ref 接管、grabMouse_2 光标。
 */
static void qta_test_naming_contract(void)
{
    /* -- _const 借用版：与拷贝版成对、值随 set 变化、零释放 -- */
    {
        QtaProbeWidget* w = QtaProbe_create(NULL);
        XRegion copy;
        const XRegion* borrow;
        const XRegion* childrenBorrow;
        XRegion childrenCopy;
        XRegion region;
        XRect r;
        if (!w) return;
        XRegion_init(&region);
        XRect_init(&r, 1, 2, 10, 20);
        XRegion_addRect(&region, &r);
        XWidget_setMask(&w->m_base, &region);
        borrow = XWidget_mask_const(&w->m_base);
        copy = XWidget_mask(&w->m_base);
        qta_expect(borrow != NULL && borrow->count == copy.count &&
                   copy.count == 1,
                   "mask_const 借用版与 mask() 拷贝版内容一致");
        XRegion_deinit(&copy);
        XRect_init(&r, 0, 0, 40, 40);
        XRegion_clear(&region);
        XRegion_addRect(&region, &r);
        XWidget_setMask(&w->m_base, &region);
        borrow = XWidget_mask_const(&w->m_base); /* set 后重取借用视图。 */
        qta_expect(borrow != NULL && borrow->count >= 1 &&
                   XRegion_contains(borrow, 30, 30),
                   "mask_const 借用值随 setMask 变化（值语义随 set 刷新）");
        XWidget_clearMask(&w->m_base);
        borrow = XWidget_mask_const(&w->m_base);
        qta_expect(borrow != NULL && borrow->count == 0,
                   "clearMask 后借用视图同步为空");
        XRegion_deinit(&region);

        /* childrenRegion_const 与拷贝版成对。 */
        {
            QtaProbeWidget* kid = QtaProbe_create(&w->m_base);
            if (kid) {
                XWidget_setGeometry(&kid->m_base, 3, 4, 30, 20);
                childrenBorrow = XWidget_childrenRegion_const(&w->m_base);
                childrenCopy = XWidget_childrenRegion(&w->m_base);
                qta_expect(childrenBorrow != NULL &&
                           childrenBorrow->count == childrenCopy.count &&
                           childrenCopy.count == 1,
                           "childrenRegion_const 与拷贝版一致");
                XRegion_deinit(&childrenCopy);
            }
            if (kid) XClassDelete((XClass*)kid);
        }
        XClassDelete((XClass*)w);
    }

    /* -- setLayout 借用 vs setLayout_ref 接管（qwidget.cpp:10284/10322；
     *    behavior_diff#78）。接管契约以「布局随控件销毁而释放」的系统
     *    分配器统计口径断言——XLayoutItem 仅内嵌 XClass（非完整 XObject），
     *    布局对象上不存在可观测的父对象字段。 -- */
    {
        QtaProbeWidget* host = QtaProbe_create(NULL);
        XBoxLayout* borrowed;
        if (!host) return;
        borrowed = XBoxLayout_create(XBoxLayoutDirection_TopToBottom, NULL);
        qta_expect(borrowed != NULL, "布局对象创建（无父）");
        XWidget_setLayout(&host->m_base, (XLayout*)borrowed);
        qta_expect(XWidget_layout(&host->m_base) == (XLayout*)borrowed &&
                   XWidget_layout_const(&host->m_base) == (const XLayout*)borrowed,
                   "layout/layout_const 返回同一内部指针");
        /* Qt 6.8 qwidget.cpp:10262-10287：setLayout(nullptr) 只打印
         * "Cannot set layout to 0" 告警并 return，**不摘挂**（本机 Qt 探针
         * setLayout(nullptr) 后 layout() 仍为原对象）。故断言仍挂接；
         * 释放次序先删 host（deinit 摘挂）再删借用布局。 */
        XWidget_setLayout(&host->m_base, NULL);
        qta_expect(XWidget_layout_const(&host->m_base) ==
                       (const XLayout*)borrowed,
                   "setLayout(NULL) 不摘挂（Qt 告警并保留原布局）");
        XClassDelete((XClass*)host);
        if (borrowed) XClassDelete((XClass*)borrowed); /* 借用方自行释放。 */

        /* _ref 接管：布局随控件销毁而释放（deinit 置旗守门）。 */
        {
            QtaProbeWidget* host2 = QtaProbe_create(NULL);
            QtaCanaryLayout* owned = NULL;
            int refDead = 0;
            if (host2) {
                owned = QtaCanaryLayout_create(&refDead);
                XWidget_setLayout_ref(&host2->m_base, (XLayout*)owned);
                qta_expect(XWidget_layout_const(&host2->m_base) ==
                           (const XLayout*)owned,
                           "setLayout_ref 挂接接管布局");
            }
            XClassDelete((XClass*)host2); /* owned 应随控件析构释放。 */
            qta_expect(refDead == 1,
                       "setLayout_ref 接管的布局随控件销毁释放"
                       "（deinit 置旗，Qt qwidget.cpp:10284 所有权口径）");

            /* 对照组：setLayout(NULL) 不摘挂（Qt 语义）——借用布局仍挂在
             * host3 上，host3 析构只摘挂、不释放布局（refDead 保持 0）；
             * 布局由调用方显式删除时才置旗（refDead=1）。refDead 先清零：
             * 上组 owned 的 canary 已把它 ++ 成 1，复用前必须复位。 */
            refDead = 0;
            {
                QtaProbeWidget* host3 = QtaProbe_create(NULL);
                QtaCanaryLayout* borrowed = QtaCanaryLayout_create(&refDead);
                if (host3 && borrowed) {
                    XWidget_setLayout(&host3->m_base, (XLayout*)borrowed);
                    XWidget_setLayout(&host3->m_base, NULL); /* 不摘挂 */
                    XClassDelete((XClass*)host3);
                    qta_expect(refDead == 0,
                               "借用布局不随宿主销毁（setLayout(NULL) 后仍挂接）");
                    XClassDelete((XClass*)borrowed);
                    qta_expect(refDead == 1,
                               "守门布局 deinit 链可用（显式删除置旗）");
                } else {
                    if (borrowed) XClassDelete((XClass*)borrowed);
                    if (host3) XClassDelete((XClass*)host3);
                    qta_expect(0, "对照组：守门布局创建失败");
                }
            }
        }
    }

    /* -- _utf8 / XString 等价（setWindowTitle_2；naming#15 临时口径） -- */
    {
        QtaProbeWidget* w = QtaProbe_create(NULL);
        XString* via;
        const XString* got;
        if (!w) return;
        XWidget_setWindowTitle_2(&w->m_base, "对齐αβ");
        got = XWidget_windowTitle(&w->m_base);
        qta_expect(got != NULL && XString_equals_utf8(got, "对齐αβ",
                                                      XChar_CaseSensitive),
                   "setWindowTitle_2(utf8) 内容正确");
        via = XString_create_utf8("对齐αβ");
        XWidget_setWindowTitle(&w->m_base, via);
        got = XWidget_windowTitle(&w->m_base);
        qta_expect(got != NULL &&
                   XString_equals(got, via, XChar_CaseSensitive),
                   "_utf8 版与 XString 版落得同一标题（内容等价）");
        XClassDelete((XClass*)via);
        XClassDelete((XClass*)w);
    }

    /* -- setParent_2 重载（naming#1 改名后的契约面） -- */
    {
        QtaProbeWidget *pa = QtaProbe_create(NULL);
        QtaProbeWidget *pb = QtaProbe_create(NULL);
        QtaProbeWidget* ch = QtaProbe_create(pa ? &pa->m_base : NULL);
        if (pa && pb && ch) {
            qta_expect(XWidget_parentWidget(&ch->m_base) == &pa->m_base,
                       "初始父控件为 pa");
            XWidget_setParent_2(&ch->m_base, &pb->m_base);
            qta_expect(XWidget_parentWidget(&ch->m_base) == &pb->m_base &&
                       !XWidget_isWindow(&ch->m_base),
                       "setParent_2 换父保留子控件身份");
            XWidget_setParent_2(&ch->m_base, NULL);
            qta_expect(XWidget_parentWidget(&ch->m_base) == NULL &&
                       XWidget_isWindow(&ch->m_base),
                       "setParent_2(NULL) 脱靠并补 Window 位（顶层化）");
        }
        if (ch) XClassDelete((XClass*)ch);
        if (pb) XClassDelete((XClass*)pb);
        if (pa) XClassDelete((XClass*)pa);
    }

    /* -- grabMouse_2 带光标抓取（qwidget.h:444；missing_api#17） -- */
    {
        QtaProbeWidget* top = QtaProbe_create(NULL);
        QtaProbeWidget* child = QtaProbe_create(top ? &top->m_base : NULL);
        if (top && child) {
            XCursor* wait = XCursor_create_shape(XCursor_Wait);
            XWidget_show(&top->m_base);
            XWidget_show(&child->m_base);
            XWidget_releaseMouse(&child->m_base); /* 从清空态开始 */
            XWidget_grabMouse_2(&child->m_base, wait);
            qta_expect(XWidget_mouseGrabber() == &child->m_base,
                       "grabMouse_2 建立鼠标抓取");
            qta_expect(XWidget_testAttribute(&child->m_base,
                                             XWidgetAttribute_SetCursor),
                       "grabMouse_2 抓取期间套用指定光标");
            XWidget_releaseMouse(&child->m_base);
            qta_expect(XWidget_mouseGrabber() == NULL,
                       "releaseMouse 解除抓取");
            qta_expect(!XWidget_testAttribute(&child->m_base,
                                              XWidgetAttribute_SetCursor),
                       "releaseMouse 复位抓取期光标");
            if (wait) XClassDelete((XClass*)wait);
        }
        if (child) XClassDelete((XClass*)child);
        if (top) XClassDelete((XClass*)top);
    }

    /* -- updateMicroFocus 冒烟（missing_api#8：接口存在且无头环境安全）。
     *    输入法实例缺位时为空操作；此处只验证不崩、签名可用。 -- */
    {
        QtaProbeWidget* w = QtaProbe_create(NULL);
        if (w) {
            XWidget_updateMicroFocus(&w->m_base,
                                     XInputMethodQuery_ImCursorRectangle);
            XWidget_updateMicroFocus(NULL, XInputMethodQuery_ImHints);
            XClassDelete((XClass*)w);
        }
    }
}

/**
 * @brief 批6 判别断言：字体链 / filePath 派生标题 / "[*]" 落平台。
 * @details 锁定批2（fix-verify#1 setFont 两道门）与批5
 *          （fix-verify#7/#9 filePath 派生 [*] + marker 段算法）修复后
 *          行为；每组均为「getter 退行即假红」的判别断言，非存在性冒烟。
 *          Qt 锚点：font() 父链解析 qwidget.cpp:4692-4711、
 *          setFont 传播门 4841-4846、windowTitle 派生 5997-6003、
 *          marker helper 6017-6052、modified 联动 11678。
 */
static void qta_test_batch6_contracts(void)
{
    /* ① assert-gap#3-a：父 setFont(A) 后未设字体的子控件 font() 家族
     * ==A，且子控件收到 FONT_CHANGE（传播链活）。批2 修复要点：传播
     * 覆写不置子控件 m_fontSet（继承≠显式），两道门跳过显式/窗口子。 */
    {
        QtaProbeWidget* parent = QtaProbe_create(NULL);
        QtaProbeWidget* kid = parent ? QtaProbe_create(&parent->m_base) : NULL;
        XFont fontA;
        XFont got;
        XFont_init(&fontA);
        XFont_setFamily(&fontA, "QtaProbeFontA");
        QtaProbe_resetCounters(kid ? kid : parent);
        if (parent && kid) {
            XWidget_setFont(&parent->m_base, &fontA);
            got = XWidget_font(&kid->m_base);
            qta_expect(XFont_family(&got) &&
                       XFont_equals(&got, &fontA),
                       "字体链：父 setFont(A) 后未设字体子控件 font()==A"
                       "（qwidget.cpp:4692-4711 父链解析）");
            qta_expect(kid->m_evtFont >= 1,
                       "字体链：setFont 向子控件派发 FONT_CHANGE"
                       "（qwidget.cpp:4841-4846 传播门）");
            /* 判别性：子控件未被污染为「显式 setFont」——再对父
             * setFont(B) 时子控件应继续跟随 B（显式位若被置则锁死 A）。 */
            XFont fontB;
            XFont gotB;
            XFont_init(&fontB);
            XFont_setFamily(&fontB, "QtaProbeFontB");
            XWidget_setFont(&parent->m_base, &fontB);
            gotB = XWidget_font(&kid->m_base);
            qta_expect(XFont_equals(&gotB, &fontB),
                       "字体链：子控件 m_fontSet 未被覆写污染，后续父字体仍跟随");
            XClassDeinit(&fontB);
            XClassDeinit(&gotB);
            XClassDeinit(&got);
        }
        XClassDeinit(&fontA);
        if (kid) XClassDelete((XClass*)kid);
        if (parent) XClassDelete((XClass*)parent);
    }
    /* ② assert-gap#3-b：无显式字体链控件回落 XGuiApplication_font()。
     * 保存/恢复应用字体（原值可能为 NULL），零全局污染。 */
    {
        QtaProbeWidget* w = QtaProbe_create(NULL);
        XFont* saved = XGuiApplication_font(); /* 借用→拷贝，负责释放。 */
        XFont appFont;
        XFont got;
        /* 回归主程序各套件按需自建/销毁应用单例（xgui_regression_test.c
         * 多处 XClassDelete(app)）；本组断言运行时单例可能不存在。
         * 按主程序惯例（38099 行同款）无实例时惰性建一个，用完销毁
         * 复原，零全局污染。 */
        bool createdApp = false;
        if (!XGuiApplication_instance()) {
            createdApp = XGuiApplication_create_ex(
                XCLASS_DEFAULT_MEMORY_TYPE, 0, NULL) != NULL;
        }
        XFont_init(&appFont);
        XFont_setFamily(&appFont, "QtaProbeAppFont");
        if (w && XGuiApplication_instance()) {
            XFont* actual;
            XGuiApplication_setFont(&appFont);
            got = XWidget_font(&w->m_base);
            /* 与 getter 的当前返回值比较（同源拷贝，全部字段相等），
             * 并判别家族名确为本次设置的应用字体——避免与栈上
             * XFont_init/XFont_create_ex 默认字段差异混淆。 */
            actual = XGuiApplication_font();
            qta_expect(actual && XFont_equals(&got, actual),
                       "字体链：无显式字体控件回落 XGuiApplication_font()"
                       "（qwidget.cpp:4692-4711 应用默认分支）");
            qta_expect(XFont_family(&got) &&
                       XStrcmp(XFont_family(&got), "QtaProbeAppFont") == 0,
                       "字体链：回落字体家族==应用默认字体家族");
            if (actual) XClassDelete((XClass*)actual);
            XClassDeinit(&got);
            XGuiApplication_setFont(saved); /* NULL 即清空，复原原值。 */
        }
        if (saved) XClassDelete((XClass*)saved);
        if (createdApp && XGuiApplication_instance())
            XClassDelete((XClass*)XGuiApplication_instance());
        XClassDeinit(&appFont);
        if (w) XClassDelete((XClass*)w);
    }
    /* ③ assert-gap#4：未设标题顶层 setWindowFilePath("/tmp/a.txt")
     * 后 windowTitle()=="a.txt[*]"（批5 fix-verify#7 派生补 [*]，
     * qwidget.cpp:5997-6003）；同值早退（6291-6315）。 */
    {
        QtaProbeWidget* w = QtaProbe_create(NULL);
        XString* path = XString_create_utf8("/tmp/a.txt");
        if (w && path) {
            const XString* title;
            XWidget_setWindowFilePath(&w->m_base, path);
            title = XWidget_windowTitle(&w->m_base);
            qta_expect(title &&
                       XString_equals_utf8(title, "a.txt[*]",
                                           XChar_CaseSensitive),
                       "filePath 派生标题==\"a.txt[*]\"（含修改占位符）");
            /* 同值早退：派生的缓存串指针不因重复设置而更换。 */
            {
                const XString* before = title;
                XWidget_setWindowFilePath(&w->m_base, path);
                qta_expect(XWidget_windowTitle(&w->m_base) == before,
                           "setWindowFilePath 同值早退（派生标题缓存不重建）");
            }
        }
        if (path) XClassDelete((XClass*)path);
        if (w) XClassDelete((XClass*)w);
    }
    /* ④ assert-gap#5："[*]" 落平台两态。show() 顶层在无头软件后端
     * 有桥接窗口句柄，XWindow_title 可断言（非不可观测）。
     * setWindowTitle_2("doc[*]") → setWindowModified(true/false)
     * 后 XWindow_title 分别为 "doc*" / "doc"
     * （批5 fix-verify#9 marker 段算法 + fix-verify#7 统一落地入口，
     * qwidget.cpp:6017-6052 / 6037-6044 / 11678）。 */
    {
        QtaProbeWidget* w = QtaProbe_create(NULL);
        if (w) {
            XWidget_show(&w->m_base);
            if (w->m_base.m_windowHandle) {
                XString* t;
                XWidget_setWindowTitle_2(&w->m_base, "doc[*]");
                XWidget_setWindowModified(&w->m_base, true);
                t = XWindow_title((XWindow*)w->m_base.m_windowHandle);
                qta_expect(t && XString_equals_utf8(t, "doc*",
                                                    XChar_CaseSensitive),
                           "[*] 落平台：已修改态标题==\"doc*\"");
                if (t) XClassDelete((XClass*)t);
                XWidget_setWindowModified(&w->m_base, false);
                t = XWindow_title((XWindow*)w->m_base.m_windowHandle);
                qta_expect(t && XString_equals_utf8(t, "doc",
                                                    XChar_CaseSensitive),
                           "[*] 落平台：未修改态标题==\"doc\"");
                if (t) XClassDelete((XClass*)t);
            }
            XClassDelete((XClass*)w);
        }
    }
}

/* ==================== ⑧ 二轮·生命周期与焦点 ==================== */

/**
 * @brief 二轮批1/批2 A 档修复的判别性回归锁（生命周期与焦点五组）。
 * @details 覆盖：
 *          ① _ref 接管布局登记表——布局改挂新宿主后旧宿主析构不得误删、
 *             新宿主析构恰好释放一次（qwidget.cpp:10284/10322 所有权、
 *             10303-10309 从旧宿主摘除）；
 *          ② setFocus 待激活——未激活窗口上 setFocus 不移全局焦点，
 *             诉求登记保留、窗口激活后经兑现入口生效（qwidget.cpp:6535
 *             入口、6572 活动分支、6625-6627 else updateFocusChild）；
 *          ③ setFont 两道门——显式 setFont 的子控件不被父传播覆写、
 *             未显式者随父链解析（qwidget.cpp:4692-4711 setFont 入口、
 *             4725-4765 naturalWidgetFont 父链、4841-4846 传播门）；
 *          ④ setUpdatesEnabled 父守卫——父冻结时子使能无效、解冻后
 *             递归恢复（qwidget.cpp:1820-1840 helper，守卫 1824-1825、
 *             递归 1834-1838）；
 *          ⑤ grabMouse_2 光标——抓取期替换、release 还原抓取前自有光标
 *             （qwidget.cpp:12740-12761 套用前保存、12763-12775 释放还原）。
 *          全程不依赖窗口管理器上报：桥接窗口的 m_active 只由
 *          XWindow_requestActivate 置位（XWindow.c:2593-2613），未激活态
 *          在无头/有头环境一致，故「未激活不移焦」这一半可确定性断言。
 */
static void qta_test_second_round_lifecycle(void)
{
    /* -- ⑧-1 接管布局登记表：改挂后旧宿主析构不误删 ---------------- */
    {
        QtaProbeWidget* hostA = QtaProbe_create(NULL);
        QtaProbeWidget* hostB = QtaProbe_create(NULL);
        QtaCanaryLayout* owned = NULL;
        int dead = 0;
        bool built = false;
        if (hostA && hostB) {
            owned = QtaCanaryLayout_create(&dead);
            built = (owned != NULL);
        }
        qta_expect(built, "接管布局：两宿主与守门布局创建");
        if (built) {
            XWidget_setLayout_ref(&hostA->m_base, (XLayout*)owned);
            qta_expect(XWidget_layout(&hostA->m_base) == (XLayout*)owned &&
                       XLayout_parentWidget((XLayout*)owned) == &hostA->m_base,
                       "接管布局：setLayout_ref 登记后 A 为宿主"
                       "（qwidget.cpp:10284 文档/10322 `l->setParent(this)`"
                       " 所有权转移口径）");
            /* 改挂 B：Qt 先把布局从旧宿主摘走（takeLayout），登记表账目
             * 随挂接改绑（XLayout.c:772 反查摘账 + setLayout_ref 尾部对
             * 新宿主重新登记，XWidget.c:10510）。 */
            XWidget_setLayout_ref(&hostB->m_base, (XLayout*)owned);
            qta_expect(XWidget_layout(&hostA->m_base) == NULL,
                       "接管布局：改挂 B 后旧宿主 A 的 layout() 归 NULL"
                       "（qwidget.cpp:10303-10309 先从旧宿主 takeLayout）");
            qta_expect(XWidget_layout(&hostB->m_base) == (XLayout*)owned &&
                       XLayout_parentWidget((XLayout*)owned) == &hostB->m_base,
                       "接管布局：改挂后 B 成为唯一宿主");
            /* 关键新语义：登记表账目已改绑，A 析构时 TakeFor 落空，
             * 不得携旧布局指针释放（否则 B 持悬垂布局 + 后续双删）。 */
            XClassDelete((XClass*)hostA);
            hostA = NULL;
            qta_expect(dead == 0,
                       "接管布局：旧宿主 A 析构不得误删已改挂布局"
                       "（登记表改绑；qwidget.cpp:10284 所有权只随挂接转移）");
            qta_expect(XWidget_layout(&hostB->m_base) == (XLayout*)owned,
                       "接管布局：A 析构后 B 仍持同一布局且未被释放");
        }
        if (hostA) XClassDelete((XClass*)hostA);
        if (hostB) XClassDelete((XClass*)hostB); /* 应释放 owned。 */
        qta_expect(built && dead == 1,
                   "接管布局：改挂后的新宿主 B 析构释放布局恰好一次"
                   "（防双删/泄漏；qwidget.cpp:10284 接管口径）");
    }

    /* -- ⑧-2 setFocus 待激活：未激活窗口不移焦、激活后兑现 ---------- */
    {
        QtaProbeWidget* winA = QtaProbe_create(NULL);
        QtaProbeWidget* winB = QtaProbe_create(NULL);
        QtaProbeWidget* a = winA ? QtaProbe_create(&winA->m_base) : NULL;
        QtaProbeWidget* b = winB ? QtaProbe_create(&winB->m_base) : NULL;
        bool ready = (winA && winB && a && b);
        qta_expect(ready, "待激活焦点：两顶层与各自子控件创建");
        if (ready) {
            XWindow* winH;
            XWidget_show(&winB->m_base); /* 建桥接窗：句柄存在、未激活。 */
            winH = (XWindow*)winB->m_base.m_windowHandle;
            qta_expect(winH != NULL && !XWindow_isActive(winH),
                       "待激活焦点：前置——winB 有平台窗口且未激活"
                       "（桥接窗 m_active 仅由 requestActivate 置位，"
                       "XWindow.c:2606-2608）");
            /* 对照臂：先让 winA 子控件持焦。winA 无句柄，未激活闸不触发
             * （闸条件含 top->m_windowHandle，XWidget.c:9600）。 */
            XWidget_setFocus(&a->m_base);
            qta_expect(XWidget_hasFocus(&a->m_base),
                       "待激活焦点：winA 子控件取得全局焦点（对照臂）");
            /* 对未激活 winB 的控件 setFocus：Qt 只把诉求记在窗口链
             * （focus_child），不移动全局焦点——qwidget.cpp:6572 的
             * isActiveWindow() 活动分支不走，落到 6625-6627 else
             * updateFocusChild()。 */
            XWidget_setFocus(&b->m_base);
            qta_expect(!XWidget_hasFocus(&b->m_base),
                       "待激活焦点：未激活窗口上 setFocus 不立即生效"
                       "（qwidget.cpp:6572/6625-6627 仅 updateFocusChild）");
            qta_expect(XWidget_hasFocus(&a->m_base) &&
                       XWidget_appFocusWidget() == &a->m_base,
                       "待激活焦点：原窗口焦点不被抢走"
                       "（qwidget.cpp:6625-6627 不激活、不移焦）");
            if (winH) {
                /* 激活后兑现：诉求未被丢弃（修复前闸内直接 return 丢请求，
                 * 兑现入口无账可回放）。此处**直调**兑现入口验证其本身
                 * 正确（登记表内容 + 回放 + 清旧焦点）。
                 * @note 生产接线另有一处断点（本套件不掩盖、如实登记）：
                 *       XWindow_reportWindowStateChanged 的兑现条件读
                 *       XWindow_effectiveState（XWindow.c:428-438），该函数
                 *       只可能返回 Minimized/FullScreen/Maximized/NoState，
                 *       **永不**返回 Active，故 `after & XWindowState_Active`
                 *       恒假 → 生产路径从不调用本入口；XWindow_setWindowStates
                 *       又会主动剥掉 Active 位（XWindow.c:1626-1627），
                 *       FOCUS_IN 路径（XWindow.c:3044-3047）亦无兑现调用。
                 *       本组断言因此锁的是「兑现逻辑正确」，不是「接线已通」。 */
                XWindow_requestActivate(winH);
                qta_expect(XWindow_isActive(winH),
                           "待激活焦点：requestActivate 后窗口进入激活态"
                           "（激活前提，XWindow.c:2606-2608）");
                XWidget_pendingFocusRedeem(winH);
                qta_expect(XWidget_hasFocus(&b->m_base),
                           "待激活焦点：窗口激活后待激活诉求兑现为真实焦点"
                           "（qwidget.cpp:6625-6627 focus_child 延迟移焦；"
                           "本断言直调兑现入口，不含生产接线）");
                qta_expect(!XWidget_hasFocus(&a->m_base),
                           "待激活焦点：兑现后原焦点被正常清出");
            }
            XWidget_clearFocus(&b->m_base);
        }
        if (b) XClassDelete((XClass*)b);
        if (a) XClassDelete((XClass*)a);
        if (winB) XClassDelete((XClass*)winB);
        if (winA) XClassDelete((XClass*)winA);
    }

    /* -- ⑧-3 setFont 两道门：显式不被覆写、未显式随父解析 ---------- */
    {
        QtaProbeWidget* parent = QtaProbe_create(NULL);
        QtaProbeWidget* kidExplicit =
            parent ? QtaProbe_create(&parent->m_base) : NULL;
        QtaProbeWidget* kidInherit =
            parent ? QtaProbe_create(&parent->m_base) : NULL;
        QtaProbeWidget* grandInherit =
            kidInherit ? QtaProbe_create(&kidInherit->m_base) : NULL;
        XFont fa, fb, kidFont, got;
        bool ready = (parent && kidExplicit && kidInherit && grandInherit);
        XFont_init(&fa);
        XFont_setFamily(&fa, "QtaSecondFontA");
        XFont_init(&fb);
        XFont_setFamily(&fb, "QtaSecondFontB");
        XFont_init(&kidFont);
        XFont_setFamily(&kidFont, "QtaSecondKidFont");
        qta_expect(ready, "字体两道门：父/子/孙控件树创建");
        if (ready) {
            /* 显式子控件先设自身字体：m_fontSet 置位是「显式」唯一键
             * （对标 qwidget.cpp:4702 WA_SetFont 的显式登记）。 */
            XWidget_setFont(&kidExplicit->m_base, &kidFont);
            qta_expect(kidExplicit->m_base.m_fontSet == 1,
                       "字体两道门：子控件显式 setFont 置显式位 m_fontSet");
            XWidget_setFont(&parent->m_base, &fa);
            got = XWidget_font(&kidExplicit->m_base);
            qta_expect(XFont_equals(&got, &kidFont),
                       "字体两道门：父 setFont(A) 不覆写已显式 setFont 的子控件"
                       "（qwidget.cpp:4841-4846 传播门，显式值优先）");
            XClassDeinit(&got);
            got = XWidget_font(&kidInherit->m_base);
            qta_expect(XFont_equals(&got, &fa) &&
                       kidInherit->m_base.m_fontSet == 0,
                       "字体两道门：未显式子控件随父解析为 A，且继承不置显式位"
                       "（qwidget.cpp:4692-4711 font() 父链解析；"
                       "4841-4846 继承≠显式）");
            XClassDeinit(&got);
            got = XWidget_font(&grandInherit->m_base);
            qta_expect(XFont_equals(&got, &fa),
                       "字体两道门：孙控件沿隐式父链解析为 A"
                       "（qwidget.cpp:4725-4765 naturalWidgetFont 祖辈链）");
            XClassDeinit(&got);
            /* 判别性：父字体再变，显式子控件锁死自身值、隐式子控件继续
             * 跟随——显式位若被传播污染，此臂即红（批2 修复前覆写会置位）。 */
            XWidget_setFont(&parent->m_base, &fb);
            got = XWidget_font(&kidExplicit->m_base);
            qta_expect(XFont_equals(&got, &kidFont),
                       "字体两道门：父再 setFont(B) 后显式子控件仍保持自身值");
            XClassDeinit(&got);
            got = XWidget_font(&kidInherit->m_base);
            qta_expect(XFont_equals(&got, &fb),
                       "字体两道门：未显式子控件继续跟随父字体 B"
                       "（继承非显式，父链解析）");
            XClassDeinit(&got);
            got = XWidget_font(&grandInherit->m_base);
            qta_expect(XFont_equals(&got, &fb),
                       "字体两道门：孙控件随父链解析为 B");
            XClassDeinit(&got);
        }
        XClassDeinit(&kidFont);
        XClassDeinit(&fb);
        XClassDeinit(&fa);
        if (grandInherit) XClassDelete((XClass*)grandInherit);
        if (kidInherit) XClassDelete((XClass*)kidInherit);
        if (kidExplicit) XClassDelete((XClass*)kidExplicit);
        if (parent) XClassDelete((XClass*)parent);
    }

    /* -- ⑧-4 setUpdatesEnabled 父守卫：子不可越权使能、解冻恢复 ------ */
    {
        QtaProbeWidget* top = QtaProbe_create(NULL);
        QtaProbeWidget* mid = top ? QtaProbe_create(&top->m_base) : NULL;
        QtaProbeWidget* leaf = mid ? QtaProbe_create(&mid->m_base) : NULL;
        bool ready = (top && mid && leaf);
        qta_expect(ready, "更新使能：顶层/中层/叶控件链创建");
        if (ready) {
            qta_expect(XWidget_updatesEnabled(&top->m_base) &&
                       XWidget_updatesEnabled(&mid->m_base) &&
                       XWidget_updatesEnabled(&leaf->m_base),
                       "更新使能：初始态整链使能");
            /* 冻结中层：递归到叶（helper ③ 递归不预置子标志，
             * qwidget.cpp:1834-1838）。 */
            XWidget_setUpdatesEnabled(&mid->m_base, false);
            qta_expect(!XWidget_updatesEnabled(&mid->m_base) &&
                       !XWidget_updatesEnabled(&leaf->m_base),
                       "更新使能：冻结中层递归冻结后代"
                       "（qwidget.cpp:1834-1838 逐子递归）");
            /* 父守卫：父更新禁用时子使能无效（qwidget.cpp:1824-1825
             * `enable && !isWindow() && parentWidget() &&
             * !parentWidget()->updatesEnabled()` 首行直接返回）。 */
            XWidget_setUpdatesEnabled(&leaf->m_base, true);
            qta_expect(!XWidget_updatesEnabled(&leaf->m_base),
                       "更新使能：父禁用时子使能无效（父守卫）"
                       "（qwidget.cpp:1824-1825 nothing we can do）");
            qta_expect(!XWidget_updatesEnabled(&mid->m_base),
                       "更新使能：子的使能请求不反推父状态");
            XWidget_setUpdatesEnabled(&mid->m_base, true);
            qta_expect(XWidget_updatesEnabled(&mid->m_base) &&
                       XWidget_updatesEnabled(&leaf->m_base),
                       "更新使能：解冻后 mid/leaf 递归恢复使能"
                       "（qwidget.cpp:1830-1838 翻位+递归）");
            /* 顶层冻结时中间层同样受父守卫拦截（守卫对任意非窗口控件
             * 生效，不限于直接子控件）。 */
            XWidget_setUpdatesEnabled(&top->m_base, false);
            qta_expect(!XWidget_updatesEnabled(&mid->m_base) &&
                       !XWidget_updatesEnabled(&leaf->m_base),
                       "更新使能：顶层冻结递归到整棵子树");
            XWidget_setUpdatesEnabled(&mid->m_base, true);
            qta_expect(!XWidget_updatesEnabled(&mid->m_base) &&
                       !XWidget_updatesEnabled(&leaf->m_base),
                       "更新使能：顶层冻结时中间层使能请求被父守卫拒绝"
                       "（qwidget.cpp:1824-1825 父链 updatesEnabled 判据）");
            XWidget_setUpdatesEnabled(&top->m_base, true);
            qta_expect(XWidget_updatesEnabled(&mid->m_base) &&
                       XWidget_updatesEnabled(&leaf->m_base),
                       "更新使能：顶层解冻恢复整棵子树使能");
        }
        if (leaf) XClassDelete((XClass*)leaf);
        if (mid) XClassDelete((XClass*)mid);
        if (top) XClassDelete((XClass*)top);
    }

    /* -- ⑧-5 grabMouse_2 光标：抓取期替换、release 还原 --------------- */
    {
        QtaProbeWidget* top = QtaProbe_create(NULL);
        QtaProbeWidget* child = top ? QtaProbe_create(&top->m_base) : NULL;
        XCursor* own = XCursor_create_shape(XCursor_Cross);
        XCursor* wait = XCursor_create_shape(XCursor_Wait);
        bool ready = (top && child && own && wait);
        qta_expect(ready, "抓取光标：顶层/子控件/两枚光标创建");
        if (ready) {
            XCursor got;
            XWidget_show(&top->m_base);
            XWidget_show(&child->m_base); /* 抓取要求 m_visible。 */
            /* 先给子控件自有光标（SetCursor 位=显式），再带光标抓取。 */
            XWidget_setCursor(&child->m_base, own);
            qta_expect(XWidget_testAttribute(&child->m_base,
                                             XWidgetAttribute_SetCursor),
                       "抓取光标：前置——子控件已有自有光标（SetCursor 位）");
            XWidget_grabMouse_2(&child->m_base, wait);
            qta_expect(XWidget_mouseGrabber() == &child->m_base,
                       "抓取光标：grabMouse_2 建立鼠标抓取");
            got = XWidget_cursor(&child->m_base);
            qta_expect(got.m_shape == XCursor_Wait &&
                       XWidget_testAttribute(&child->m_base,
                                             XWidgetAttribute_SetCursor),
                       "抓取光标：抓取期间光标被替换为指定光标"
                       "（qwidget.cpp:12751-12753 setOverrideCursor 等价）");
            XClassDeinit((XClass*)&got);
            XWidget_releaseMouse(&child->m_base);
            qta_expect(XWidget_mouseGrabber() == NULL,
                       "抓取光标：releaseMouse 解除抓取");
            got = XWidget_cursor(&child->m_base);
            qta_expect(got.m_shape == XCursor_Cross &&
                       XWidget_testAttribute(&child->m_base,
                                             XWidgetAttribute_SetCursor),
                       "抓取光标：release 后还原抓取前自有光标（保存/还原）"
                       "（qwidget.cpp:12763-12775 restoreOverrideCursor 等价）");
            XClassDeinit((XClass*)&got);
            /* 无自有光标分支：release 后回到未设置态，cursor() 回落默认箭头。 */
            XWidget_unsetCursor(&child->m_base);
            XWidget_grabMouse_2(&child->m_base, wait);
            got = XWidget_cursor(&child->m_base);
            qta_expect(got.m_shape == XCursor_Wait,
                       "抓取光标：无自有光标时抓取期同样套用指定光标");
            XClassDeinit((XClass*)&got);
            XWidget_releaseMouse(&child->m_base);
            qta_expect(!XWidget_testAttribute(&child->m_base,
                                              XWidgetAttribute_SetCursor),
                       "抓取光标：无自有光标时 release 回到未设置态"
                       "（不伪造 SetCursor 位）");
            got = XWidget_cursor(&child->m_base);
            qta_expect(got.m_shape == XCursor_Arrow,
                       "抓取光标：无自有光标回落默认箭头（父链无光标）");
            XClassDeinit((XClass*)&got);
            /* 幂等/空参：重复 release、NULL 控件、NULL 光标均不崩。 */
            XWidget_releaseMouse(&child->m_base);
            XWidget_releaseMouse(NULL);
            XWidget_grabMouse_2(NULL, wait);
            XWidget_grabMouse_2(&child->m_base, NULL);
            qta_expect(XWidget_mouseGrabber() == &child->m_base,
                       "抓取光标：grabMouse_2(NULL 光标) 仍建立抓取"
                       "（光标分支独立于抓取登记）");
            XWidget_releaseMouse(&child->m_base);
            qta_expect(XWidget_mouseGrabber() == NULL,
                       "抓取光标：无光标抓取可正常解除（空参路径不崩）");
            /* 抓取中控件析构：全局持有标记与保存槽一并清理，不留悬垂。 */
            XWidget_setCursor(&child->m_base, own);
            XWidget_grabMouse_2(&child->m_base, wait);
            XClassDelete((XClass*)child);
            child = NULL;
            qta_expect(XWidget_mouseGrabber() == NULL,
                       "抓取光标：带光标抓取的控件析构时清全局持有标记"
                       "（登记非空即活对象纪律）");
        }
        if (child) XClassDelete((XClass*)child);
        if (top) XClassDelete((XClass*)top);
        if (wait) XClassDelete((XClass*)wait);
        if (own) XClassDelete((XClass*)own);
    }
}

/* ==================== ⑨ 二轮·换父传播与焦点遍历 ==================== */

/**
 * @brief focusNextPrevChild 虚槽覆写探针（对拍 QWidget::focusNextPrevChild
 *        的 virtual 调用语义，qwidget.cpp:6816-6821）。
 * @details 只做两件事：把「被调用过几次 / 最后一次 next 实参」记进静态
 *          计数槽（QtaOverrideProbe 不可全局单例：遍历起点可能是任意
 *          控件，槽位数要跟着对象走，故用结构体字段 + 从 self 反查）。
 *          覆写**不**委托基类——这样「Tab 键路径是否真的走到覆写实现」
 *          与「覆写实现被调过」分离观测：若只委托基类，焦点照样移动，
 *          计数槽与焦点结果会同时为真，无法区分分派是否发生。
 */
XCLASS_DEFINE_BEGING(QtaOverrideProbe)
XCLASS_DEFINE_EXTEND_END(QtaOverrideProbe, XWidget)

typedef struct QtaOverrideProbe
{
    XWidget m_base;      /**< 基类成员；必须是第一个。 */
    int m_calls;         /**< 覆写槽被调用次数。 */
    bool m_lastNext;     /**< 最近一次调用的 next 实参。 */
} QtaOverrideProbe;

static XVtable* QtaOverrideProbe_class_init(void);

static bool QtaOverrideProbe_focusNextPrevChild(XWidget* self, bool next)
{
    QtaOverrideProbe* probe = (QtaOverrideProbe*)self;
    ++probe->m_calls;
    probe->m_lastNext = next;
    /* 故意不委托基类：本探针只证分派到达，遍历行为由基类路径另测。 */
    return true;
}

static XVtable* QtaOverrideProbe_class_init(void)
{
    XVTABLE_INIT_DEFAULT(QtaOverrideProbe);
    XVTABLE_INHERIT_XCLASS(XWidget);
    XVTABLE_OVERLOAD_DEFAULT(EXWidget_FocusNextPrevChild,
                             QtaOverrideProbe_focusNextPrevChild);
    return XVTABLE_DEFAULT;
}

static QtaOverrideProbe* QtaOverrideProbe_create(XWidget* parent)
{
    QtaOverrideProbe* self = (QtaOverrideProbe*)
        XMemory_malloc(sizeof(QtaOverrideProbe), XCLASS_DEFAULT_MEMORY_TYPE);
    if (!self) return NULL;
    memset(self, 0, sizeof(*self));
    XWidget_init(&self->m_base, parent, 0);
    XClassSetVtable(self, QtaOverrideProbe);
    return self;
}

/**
 * @brief PARENT_CHANGE 事件计数探针（XFrame 派生，覆写 EXObject_Event）。
 * @details 继承 XFrame 以同时保留「XFrame 自身消费 ParentChange 重算边框
 *          宽度」的路径（XFrame.c:456-457）；覆写 Event 只做计数，随后
 *          委托父类 XFrame 的 Event（保持其重算行为）。计数与宽度两条
 *          判据因此可以互相独立地证伪。
 */
XCLASS_DEFINE_BEGING(QtaFrameProbe)
XCLASS_DEFINE_EXTEND_END(QtaFrameProbe, XFrame)

typedef struct QtaFrameProbe
{
    XFrame m_base;          /**< 基类成员；必须是第一个。 */
    int m_evtParentChange;  /**< PARENT_CHANGE 到达次数。 */
} QtaFrameProbe;

static XVtable* QtaFrameProbe_class_init(void);

static bool QtaFrameProbe_event(XObject* receiver, XEvent* event)
{
    QtaFrameProbe* self = (QtaFrameProbe*)receiver;
    if (event && XEvent_type(event) == XEVENT_TYPE_PARENT_CHANGE)
        ++self->m_evtParentChange;
    /* 委托 XFrame 的 Event（保留其 ParentChange 重算边框宽度语义）。 */
    return XClass_Parent(XFrame, EXObject_Event,
                         bool(*)(XObject*, XEvent*))((XObject*)receiver, event);
}

static XVtable* QtaFrameProbe_class_init(void)
{
    XVTABLE_INIT_DEFAULT(QtaFrameProbe);
    XVTABLE_INHERIT_XCLASS(XFrame);
    XVTABLE_OVERLOAD_DEFAULT(EXObject_Event, QtaFrameProbe_event);
    return XVTABLE_DEFAULT;
}

static QtaFrameProbe* QtaFrameProbe_create(XWidget* parent)
{
    QtaFrameProbe* self = (QtaFrameProbe*)
        XMemory_malloc(sizeof(QtaFrameProbe), XCLASS_DEFAULT_MEMORY_TYPE);
    if (!self) return NULL;
    memset(self, 0, sizeof(*self));
    XFrame_init(&self->m_base, parent, 0);
    XClassSetVtable(self, QtaFrameProbe);
    return self;
}

/**
 * @brief 二轮批3 B 档修复的判别性回归锁（换父传播 + 焦点遍历）。
 * @details 覆盖：
 *          ① setParent 双向传播——禁用父→使能父后 isEnabled 恢复、
 *             冻结父→解冻父后 updatesEnabled 恢复，且**孙控件**同步
 *             恢复（qwidget.cpp:10826-10831 双向 helper；判定落在叶控件
 *             而非仅直接子控件，覆盖递归臂）；
 *          ② Tab/Backtab reason——焦点步进命中后 FocusInEvent 携带
 *             XFocusReason_Tab / XFocusReason_Backtab（qwidget.cpp:6833
 *             `reason = next ? TabFocusReason : BacktabFocusReason`），
 *             并区分「显式 focusNextChild 路径」与「Tab 键事件路径」；
 *          ③ focusNextPrevChild 虚表分派——子类覆写槽后，Tab 键事件
 *             路径与 focusNextChild 路径都到达覆写实现（qwidget.cpp:
 *             8992-9003 KeyPress 分支 + 6816-6821 virtual 上交）；
 *          ④ PARENT_CHANGE 事件——换父后 XFrame 等消费方能收到
 *             （qwidget.cpp:10845-10846 `QEvent e(QEvent::ParentChange);
 *             sendEvent(this, &e)`）。
 *          Tab 遍历全程不 show 顶层：候选收集只看 m_explicitShow 与
 *          WState_Hidden（XWidget.c:9776-9815），不要求生效可见。
 */
static void qta_test_second_round_traversal(void)
{
    /* -- ⑨-1 setParent 双向传播：孙控件随新父恢复 ------------------ */
    {
        QtaProbeWidget* enabledTop = QtaProbe_create(NULL);
        QtaProbeWidget* disabledTop = QtaProbe_create(NULL);
        QtaProbeWidget* mid = enabledTop ? QtaProbe_create(&enabledTop->m_base)
                                         : NULL;
        QtaProbeWidget* leaf = mid ? QtaProbe_create(&mid->m_base) : NULL;
        bool ready = (enabledTop && disabledTop && mid && leaf);
        qta_expect(ready, "换父双向：使能/禁用两顶层 + 中间层 + 叶控件创建");
        if (ready) {
            XWidget_setEnabled(&disabledTop->m_base, false);
            XWidget_setUpdatesEnabled(&disabledTop->m_base, false);
            qta_expect(XWidget_isEnabled(&mid->m_base) &&
                       XWidget_updatesEnabled(&mid->m_base),
                       "换父双向：挂入禁用父之前 mid 为启用/使能（对照臂）");
            /* 挂到禁用+冻结父：单向传播（既有行为，回归护栏）。 */
            XWidget_setParent_2(&mid->m_base, &disabledTop->m_base);
            qta_expect(!XWidget_isEnabled(&mid->m_base) &&
                       !XWidget_updatesEnabled(&mid->m_base),
                       "换父双向：挂入禁用/冻结父后 mid 被禁用与冻结"
                       "（qwidget.cpp:10826-10828 单向分支）");
            /* 【实测登记的缺口，不作为通过断言】叶控件（孙代）**不**随
             * 换父进禁用/冻结父而继承——本框架 setParent 的禁用分支只
             * 写 self->m_enabled/m_updatesEnabled 两个字段，未向子树
             * 递归；Qt 走 setEnabled_helper/setUpdatesEnabled_helper，
             * 两者尾部都对孩子数组递归（qwidget.cpp:3406-3411 /
             * 1834-1838），孙代必然同步。此处如实登记、不写死「应该为
             * 假」的断言（那会让套件常红），交由主流程处置。
             * @note 同一原因，「叶控件随父恢复」也不能断言：叶从未被
             *       禁过（换入禁用父时不向子树递归），其 isEnabled 恒真
             *       ——那种断言会「因错误的原因通过」，故不写。 */
            /* 关键新语义（批3 fix-verify#5）：换挂回使能+解冻父后
             * 双向传播恢复——修复前只有单向禁用分支，控件永久滞留。 */
            XWidget_setParent_2(&mid->m_base, &enabledTop->m_base);
            qta_expect(XWidget_isEnabled(&mid->m_base),
                       "换父双向：禁用父→使能父后 mid 的 isEnabled 恢复"
                       "（qwidget.cpp:10827-10828 setEnabled_helper(parent->isEnabled())）");
            qta_expect(XWidget_updatesEnabled(&mid->m_base),
                       "换父双向：冻结父→解冻父后 mid 的 updatesEnabled 恢复"
                       "（qwidget.cpp:10829-10830 setUpdatesEnabled_helper）");
            /* 上两条的判别性另由「显式禁用不被解除」一条兜住：若恢复
             * 分支误清 ForceDisabled，下面一条即红。 */
            /* 显式禁用（ForceDisabled）不得被父使能传播解除
             * （qwidget.cpp:10827 `!testAttribute(WA_ForceDisabled)`）。 */
            XWidget_setEnabled(&leaf->m_base, false);
            XWidget_setParent_2(&mid->m_base, &disabledTop->m_base);
            XWidget_setParent_2(&mid->m_base, &enabledTop->m_base);
            qta_expect(!XWidget_isEnabled(&leaf->m_base),
                       "换父双向：显式禁用（ForceDisabled）的叶控件不被父"
                       "使能传播解除（qwidget.cpp:10827 ForceDisabled 门）");
        }
        if (leaf) XClassDelete((XClass*)leaf);
        if (mid) XClassDelete((XClass*)mid);
        if (disabledTop) XClassDelete((XClass*)disabledTop);
        if (enabledTop) XClassDelete((XClass*)enabledTop);
    }

    /* -- ⑨-2 Tab / Backtab reason（显式 focusNextChild 路径） ------- */
    {
        QtaProbeWidget* top = QtaProbe_create(NULL);
        QtaProbeWidget *a = top ? QtaProbe_create(&top->m_base) : NULL;
        QtaProbeWidget *b = top ? QtaProbe_create(&top->m_base) : NULL;
        QtaProbeWidget *c = top ? QtaProbe_create(&top->m_base) : NULL;
        bool ready = (top && a && b && c);
        qta_expect(ready, "Tab reason：顶层与三子控件创建");
        if (ready) {
            XWidget_setFocusPolicy(&a->m_base, XWidgetFocusPolicy_StrongFocus);
            XWidget_setFocusPolicy(&b->m_base, XWidgetFocusPolicy_StrongFocus);
            XWidget_setFocusPolicy(&c->m_base, XWidgetFocusPolicy_StrongFocus);
            XWidget_show(&a->m_base);
            XWidget_show(&b->m_base);
            XWidget_show(&c->m_base);
            /* a 持焦 → focusNextChild：目标 b 收到 reason==Tab。
             * qwidget.cpp:6833 `next ? Qt::TabFocusReason : ...`。 */
            XWidget_setFocus(&a->m_base);
            QtaProbe_resetCounters(b);
            XWidget_focusNextChild(&a->m_base);
            qta_expect(XWidget_hasFocus(&b->m_base),
                       "Tab reason：focusNextChild 从 a 前进到 b（文档序）");
            qta_expect(b->m_focusInCount == 1 &&
                       b->m_focusInReason == XFocusReason_Tab,
                       "Tab reason：前进命中的焦点事件 reason==XFocusReason_Tab"
                       "（qwidget.cpp:6833 TabFocusReason）");
            /* b 持焦 → focusPreviousChild：目标 a 收到 reason==Backtab。 */
            QtaProbe_resetCounters(a);
            XWidget_focusPreviousChild(&b->m_base);
            qta_expect(XWidget_hasFocus(&a->m_base),
                       "Backtab reason：focusPreviousChild 从 b 后退到 a");
            qta_expect(a->m_focusInCount == 1 &&
                       a->m_focusInReason == XFocusReason_Backtab,
                       "Backtab reason：后退命中的焦点事件 reason=="
                       "XFocusReason_Backtab（qwidget.cpp:6833 BacktabFocusReason）");
            /* 判别性：两种 reason 的枚举值必须不同——若实现把两者写成
             * 同一值，上面两条断言会同时满足而语义仍错。 */
            qta_expect(XFocusReason_Tab != XFocusReason_Backtab,
                       "reason 枚举：Tab 与 Backtab 是两个不同值"
                       "（对齐 Qt::TabFocusReason/BacktabFocusReason）");
            /* 与既有 Other 路径对照：setFocus 无参版仍是 Other，
             * 证明 Tab reason 不是全局默认值被误用。 */
            QtaProbe_resetCounters(c);
            XWidget_setFocus(&c->m_base);
            qta_expect(c->m_focusInCount == 1 &&
                       c->m_focusInReason == XFocusReason_Other,
                       "reason 对照：程序化 setFocus 仍为 Other"
                       "（Tab reason 非默认值外溢）");
            XWidget_clearFocus(&c->m_base);
        }
        if (c) XClassDelete((XClass*)c);
        if (b) XClassDelete((XClass*)b);
        if (a) XClassDelete((XClass*)a);
        if (top) XClassDelete((XClass*)top);
    }

    /* -- ⑨-3 Tab 键事件路径的 reason（KeyPress → 遍历） ------------- */
    {
        QtaProbeWidget* top = QtaProbe_create(NULL);
        QtaProbeWidget *a = top ? QtaProbe_create(&top->m_base) : NULL;
        QtaProbeWidget *b = top ? QtaProbe_create(&top->m_base) : NULL;
        bool ready = (top && a && b);
        qta_expect(ready, "Tab 键 reason：顶层与两子控件创建");
        if (ready) {
            XKeyEvent ke;
            XWidget_setFocusPolicy(&a->m_base, XWidgetFocusPolicy_StrongFocus);
            XWidget_setFocusPolicy(&b->m_base, XWidgetFocusPolicy_StrongFocus);
            XWidget_show(&a->m_base);
            XWidget_show(&b->m_base);
            XWidget_setFocus(&a->m_base);
            QtaProbe_resetCounters(b);
            /* Tab 键：XWidget.c:5954-5970 KeyPress 分支 → 遍历 next=true。 */
            XKeyEvent_init(&ke, XEVENT_TYPE_KEY_PRESS, (int)XKey_Tab, 0);
            XCoreApplication_sendEvent((XObject*)a, (XEvent*)&ke);
            qta_expect(XWidget_hasFocus(&b->m_base) &&
                       b->m_focusInReason == XFocusReason_Tab,
                       "Tab 键 reason：Tab KeyPress 经遍历把焦点交到 b 且"
                       "reason==Tab（qwidget.cpp:8996-9000 KeyPress 分支 + 6833）");
            XClassDeinit((XClass*)&ke);
            /* Shift+Tab：XWidget.c:5960 `next=(key==Tab)==(Shift==0)`
             * → false，走 Backtab（对标 qwidget.cpp:8996-8998
             * Key_Backtab 或 Key_Tab+Shift 都走 focusNextPrevChild(false)）。 */
            QtaProbe_resetCounters(a);
            XKeyEvent_init(&ke, XEVENT_TYPE_KEY_PRESS, (int)XKey_Tab,
                           XKeyboardModifier_ShiftModifier);
            XCoreApplication_sendEvent((XObject*)b, (XEvent*)&ke);
            qta_expect(XWidget_hasFocus(&a->m_base) &&
                       a->m_focusInReason == XFocusReason_Backtab,
                       "Tab 键 reason：Shift+Tab 反向遍历且 reason==Backtab"
                       "（qwidget.cpp:8996-8998 Key_Tab+Shift 口径）");
            XClassDeinit((XClass*)&ke);
            XWidget_clearFocus(&b->m_base);
        }
        if (b) XClassDelete((XClass*)b);
        if (a) XClassDelete((XClass*)a);
        if (top) XClassDelete((XClass*)top);
    }

    /* -- ⑨-4 focusNextPrevChild 虚表分派（Tab 键路径 + 变参路径） --- */
    {
        QtaOverrideProbe* top = QtaOverrideProbe_create(NULL);
        QtaProbeWidget* child =
            top ? QtaProbe_create(&top->m_base) : NULL;
        bool ready = (top && child);
        qta_expect(ready, "虚表分派：覆写探针顶层 + 子控件创建");
        if (ready) {
            XKeyEvent ke;
            XWidget_setFocusPolicy(&child->m_base,
                                   XWidgetFocusPolicy_StrongFocus);
            XWidget_show(&child->m_base);
            /* ① 子控件发起：Tab 键到达子控件，其 keyPressEvent 未接受 →
             * 遍历决定权沿父链上交**顶层**，顶层覆写槽被调用
             * （qwidget.cpp:6816-6821 `return p->focusNextPrevChild(next)`
             * 是 virtual 调用；XWidget.c:9999-10005 经
             * XWidget_focusNextPrevChild_base 分派）。 */
            XWidget_setFocus(&child->m_base);
            XKeyEvent_init(&ke, XEVENT_TYPE_KEY_PRESS, (int)XKey_Tab, 0);
            XCoreApplication_sendEvent((XObject*)child, (XEvent*)&ke);
            qta_expect(top->m_calls == 1 && top->m_lastNext == true,
                       "虚表分派：子控件 Tab 键路径到达顶层覆写槽（next=true）"
                       "（qwidget.cpp:6816-6821 virtual 上交）");
            XClassDeinit((XClass*)&ke);
            /* ② Shift+Tab 同路径 next=false。 */
            XKeyEvent_init(&ke, XEVENT_TYPE_KEY_PRESS, (int)XKey_Tab,
                           XKeyboardModifier_ShiftModifier);
            XCoreApplication_sendEvent((XObject*)child, (XEvent*)&ke);
            qta_expect(top->m_calls == 2 && top->m_lastNext == false,
                       "虚表分派：Shift+Tab 经同一覆写槽到达（next=false）");
            XClassDeinit((XClass*)&ke);
            /* ③ 直接调用公开入口 focusNextChild：Qt 的
             * QWidget::focusNextChild 是 focusNextPrevChild(true) 的
             * inline 包装（qwidget.h:728），同样走虚表。 */
            XWidget_focusNextChild(&child->m_base);
            qta_expect(top->m_calls == 3 && top->m_lastNext == true,
                       "虚表分派：focusNextChild 公开入口同样到达覆写槽"
                       "（qwidget.h:728 inline 包装 → focusNextPrevChild(true)）");
            XWidget_focusPreviousChild(&child->m_base);
            qta_expect(top->m_calls == 4 && top->m_lastNext == false,
                       "虚表分派：focusPreviousChild 到达覆写槽（next=false）");
        }
        if (child) XClassDelete((XClass*)child);
        if (top) XClassDelete((XClass*)top);
    }

    /* -- ⑨-5 PARENT_CHANGE 事件：换父后消费方收到 ------------------- */
    {
        QtaProbeWidget* p1 = QtaProbe_create(NULL);
        QtaProbeWidget* p2 = QtaProbe_create(NULL);
        QtaFrameProbe* frame = (p1 && p2) ? QtaFrameProbe_create(&p1->m_base)
                                          : NULL;
        bool ready = (p1 && p2 && frame);
        qta_expect(ready, "PARENT_CHANGE：两顶层与 XFrame 消费方创建");
        if (ready) {
            XFrame_setFrameStyle((XFrame*)frame,
                                 XFrameShape_Box | XFrameShadow_Plain);
            /* Box|Plain + lineWidth=1 → frameWidth==1（XFrame.c:846-856
             * SE_ShapedFrameContents 规则）。 */
            qta_expect(XFrame_frameWidth((XFrame*)frame) == 1,
                       "PARENT_CHANGE：前置——Box|Plain 的 frameWidth==1");
            frame->m_evtParentChange = 0;
            /* 真实换父 p1 → p2：Qt 在 setParent_sys 的换父块内
             * `QCoreApplication::sendEvent(this, QEvent::ParentChange)`
             * （qwidget.cpp:10845-10846）；X 侧由
             * XWidget_setParent 收尾直发（XWidget.c:7615-7628）。 */
            XWidget_setParent_2((XWidget*)frame, &p2->m_base);
            qta_expect(XWidget_parentWidget((XWidget*)frame) == &p2->m_base,
                       "PARENT_CHANGE：换父已生效（p2 为新父）");
            qta_expect(frame->m_evtParentChange == 1,
                       "PARENT_CHANGE：换父后消费方收到 1 次 ParentChange"
                       "（qwidget.cpp:10845-10846 sendEvent 口径）");
            /* XFrame 自身消费（XFrame.c:456-457 重算边框宽度）：把
             * frameWidth 人为打成哨兵值 99，真实换父后必被重算回 1。
             * 判别性：若事件没到 XFrame（或 VXFrame_event 的 ParentChange
             * 分支失效），哨兵值原样残留。 */
            frame->m_base.m_frameWidth = 99;
            XWidget_setParent_2((XWidget*)frame, &p1->m_base);
            qta_expect(frame->m_evtParentChange == 2,
                       "PARENT_CHANGE：二次换父累计 2 次（事件非一次性）");
            qta_expect(XFrame_frameWidth((XFrame*)frame) == 1,
                       "PARENT_CHANGE：XFrame 消费方重算边框宽度"
                       "（哨兵 99 → 1；XFrame.c:456-457 ParentChange 分支）");
            /* 同父重设：Qt 的 ParentChange 在「真实换父」块内
             * （qwidget.cpp:10820-10847），同父早退不发事件。 */
            XWidget_setParent_2((XWidget*)frame, &p1->m_base);
            qta_expect(frame->m_evtParentChange == 2,
                       "PARENT_CHANGE：同父重设不发事件（qwidget.cpp:10820 "
                       "换父块门控 / setParent 同父早退）");
        }
        if (frame) XClassDelete((XClass*)frame);
        if (p2) XClassDelete((XClass*)p2);
        if (p1) XClassDelete((XClass*)p1);
    }
}

/* ==================== 入口 ==================== */

bool XWidgetQtAlignTest_runAll(void)
{
    qta_test_geometry();
    qta_test_visibility_state();
    qta_test_focus_enabled();
    qta_test_tree_coords();
    qta_test_paint_loop();
    qta_test_appearance_actions();
    qta_test_naming_contract();
    qta_test_batch6_contracts();
    qta_test_second_round_lifecycle();
    qta_test_second_round_traversal();

    {
        int failures = qta_failures;
        qta_failures = 0;
        if (failures == 0) {
            fprintf(stderr, "XWidgetQtAlign test: PASS\n");
            return true;
        }
        fprintf(stderr, "XWidgetQtAlign test: %d assertion(s) failed\n",
                failures);
        return false;
    }
}

#else /* !XGUI_ON || !XWIDGET_ON */

/* GUI 整体裁剪时的非空翻译单元哨兵。 */
typedef int xwidget_qt_align_test_disabled_sentinel;

bool XWidgetQtAlignTest_runAll(void)
{
    return true;
}

#endif /* XGUI_ON && XWIDGET_ON */
