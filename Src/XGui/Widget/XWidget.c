/******************************************************************************
 * @file       XWidget.c
 * @brief      XWidget 控件基类实现（对标 Qt 6.8 QWidget 的嵌入式 API）。
 * @details    本文件实现 XWidget 已覆盖行为的 Qt 6.8 语义，逐一与 Qt 6.8
 *             qwidget.cpp / qwidget_p.h / qwidgetwindow.cpp 对齐：
 *              - 生命周期与属性：XObject 继承、控件属性位集（XWidgetAttribute
 *                与 Qt 数值一致）、窗口标志/类型（复用 XWindowFlags/XWindowType）；
 *              - 几何体系：pos/size/rect/geometry/frameGeometry/normalGeometry、
 *                move/resize/setGeometry/setFixedSize，尺寸按最小/最大约束
 *                钳位（上限 XWIDGET_MAX_SIZE=16777215，与 Qt QWINDOWSIZE_MAX
 *                一致），变化时派发 move/resize 事件；
 *              - 尺寸约束与提示：minimumSize/maximumSize/baseSize/
 *                sizeIncrement/sizeHint/minimumSizeHint 与 XWidgetSizePolicy
 *                （对标 QSizePolicy：4 位策略、5 位控件类型编码、双向拉伸、
 *                heightForWidth/widthForHeight/retainSizeWhenHidden）；
 *              - 控件树：setParent 走 XObject 父子登记，childAt 按子控件
 *                矩形逆序命中测试递归查找，mapToGlobal/mapFromGlobal 沿
 *                父链累加并在顶层桥接窗口存在时以 XWindow_mapToGlobal 为准；
 *              - 可见性与窗口状态：setVisible/show/hide 维护
 *                m_explicitShow/m_visible（生效可见状态含父链），顶层控件
 *                惰性创建内部 XWidgetWindow（XWindow 子类）并同步标题/图标/
 *                文件路径/透明度/模态/几何/光标，首次显示发送 SHOW 事件，
 *                隐藏发送 HIDE 事件并向子控件传播；
 *              - 焦点：模块静态 g_focusWidget 保存焦点控件，setFocusReason
 *                先发 FOCUS_OUT 再登记应用焦点（XGuiApplication_setFocusWindow
 *                与 XApplication_setFocusWidget 联动）后发 FOCUS_IN；
 *              - 绘制闭环：update/updateRect/updateRegion 把区域折算到顶层
 *                控件脏区（XWidget_paintOffset 平移量）并投递 PAINT，
 *                repaint 同步驱动 XWidget_flushBackingStore；flushBackingStore
 *                确保 XBackingStore 存在后 beginPaint(region) -> 顶层 paintEvent
 *                递归子控件（区域逐层裁剪）-> endPaint -> flush 上屏 ->
 *                完成区域确认并保留绘制期间新增脏区，默认 paintEvent 按
 *                autoFillBackground 填充 XPaletteColorRole_Window 底色；
 *              - 事件分派：XWidget_event_base 继承自 XObject（宏复用
 *                XObject_event_base），经虚表 EXObject_Event 进入
 *                VXWidget_event；VXWidget_event 按 XEventType 分派到 25 个
 *                事件虚函数槽（paintEvent/resizeEvent/moveEvent/closeEvent/
 *                focusInEvent/focusOutEvent/enterEvent/leaveEvent/keyPressEvent/
 *                keyReleaseEvent/inputMethodEvent/dragEnterEvent/dragMoveEvent/
 *                dragLeaveEvent/dropEvent/mousePressEvent/mouseReleaseEvent/
 *                mouseDoubleClickEvent/mouseMoveEvent/wheelEvent/showEvent/
 *                hideEvent/changeEvent），未识别事件回退 XObject 默认
 *                Event 实现；
 *                鼠标/滚轮/进入事件经 XWidget_childAt 命中测试改写局部坐标
 *                后投递，未接受事件可沿父链冒泡（位置逐层换算）。
 *             本模块不依赖任何平台 API；窗口/后备存储/平台差异全部由
 *             XWindow/XBackingStore 与 Drive 后端隔离，嵌入式可用。
 * @note       模块总开关 XWIDGET_ON 定义于 XGuiConfig.h；置 0 时本文件
 *             实现体整体裁剪。依赖子开关 XWINDOW_ON / XGUIAPPLICATION_ON /
 *             XAPPLICATION_ON / XCURSOR_ON / XBACKINGSTORE_ON，关闭时对应
 *             接口按头文件回退语义退化为空实现或空指针，保证可裁剪编译。
 * @author     XinYueC 团队
 ******************************************************************************/
#include "XWidget.h"
#include "XMemory.h"

#include "XAlgorithm.h"
#include "XWidget_Protected.h"
#include "XShortcut.h"
#include "XVarList.h"
/* ShortcutOverride 询问接收侧（对标 QLineEdit/QPlainTextEdit::event 的
 * ShortcutOverride 分支）：行编辑/多行编辑壳头文件仅供下方取编辑控制器与
 * 转交判定用，不扩任何契约头（XEVENT_TYPE_SHORTCUT_OVERRIDE 见
 * XEventType.h:49，早已定义）。 */
#if XLINEEDIT_ON && XLINECONTROL_ON
#include "XLineEdit.h"
#endif /* XLINEEDIT_ON && XLINECONTROL_ON */
#if XTEXTCONTROL_ON && XPLAINTEXTEDIT_ON
#include "XPlainTextEdit.h"
#endif /* XTEXTCONTROL_ON && XPLAINTEXTEDIT_ON */
#if XByteArray_ON
#include "XByteArray.h"
#endif /* XByteArray_ON */
#include "XStringUtils.h"
#if XWINDOWEVENT_ON
#include "XWindowEvent.h"
#if XWINDOWSYSTEMINTERFACE_ON && XWINDOW_ON && XWINDOWEVENT_ON
#include "XWindowSystemInterface.h"
#endif
#endif /* XWINDOWEVENT_ON */
#include "XGuiApplication.h"
#include "XImage.h"
#include "XPainter.h"
#include "XPaintDevice.h"
#include "XCoreApplication.h"
#include "XBackingStore.h"
#include "XPlatformBackingStore.h"
/* XSystem_environment 原型（本文件 GPU present 模式与脏区回退两处调用）；
   缺原型时 MSVC 隐式声明 int 返回，Win64 下指针截断（C4047）。 */
#include "XSystem.h"
#include "XDateTime.h"        /* present 限频计时：单调毫秒（约束文档时间源规则） */
#include <stdlib.h>

/* TEMP：paintTree 派发 paintEvent 时的上屏目标图像（表面裁剪限定用）。 */
static XImage* g_paintTargetImage;
/* 静态内容保留层前向声明：失效联动（update/几何/可见性路径调用）、
 * 生命周期挂钩与 paintTree 绘制钩子，实现体在效果钩子之后的保留层小节。 */
static void xwidget_retainedDropCache(XWidget* self);
static void xwidget_retainedDisable(XWidget* self);
static void xwidget_retainedInvalidateForRect(XWidget* origin);
static void xwidget_retainedInvalidateChain(XWidget* origin);
static bool xwidget_paintRetainedLayer(XWidget* widget, const XRegion* paintRegion);

/** @brief 保留层 LRU 登记节点（双向链表；g_retainedHead=最近使用，
 *         g_retainedTail=最久未用，两端操作均 O(1)）。定义前置：失效
 *         联动与移动构造在文件前段即访问登记表。 */
struct XWidgetRetainedNode {
    struct XWidgetRetainedNode* m_prev;
    struct XWidgetRetainedNode* m_next;
    XWidget* m_widget;            /**< 借用；控件析构/移动时反向改绑或摘除。 */
};

static XWidgetRetainedNode* g_retainedHead;   /**< MRU 端。 */
static XWidgetRetainedNode* g_retainedTail;   /**< LRU 端。 */
static int g_retainedRegistered;              /**< 登记数；保留层全局总开关判断。 */
static size_t g_retainedBytes;                /**< 当前缓存字节总量（含失效未释放）。 */
static uint64_t g_retainedStampClock;         /**< 单调访问时间戳时钟。 */
#if XPLATFORMINTEGRATION_ON && XGPU_ON
#include "XGpuRenderBackend.h"
#endif /* XPLATFORMINTEGRATION_ON && XGPU_ON */
#if XLAYOUT_ON
#include "XLayout.h"
#include "XLayout_Internal.h"
#endif /* XLAYOUT_ON */
#if XAPPLICATION_ON && XGUIAPPLICATION_ON
#include "XApplication.h"
#endif /* XAPPLICATION_ON */
#if XWINDOW_ON && XACCESSIBLE_ON
#include "XPlatformAccessibility.h"
#endif /* XWINDOW_ON && XACCESSIBLE_ON */
#if XCURSOR_ON
#include "XCursor.h"
#endif /* XCURSOR_ON */
#include "XGraphicsEffect.h"


#if XWIDGET_ON

/* ==================== 内部常量与私有结构 ==================== */

/** @brief 控件尺寸上限（与 Qt QWINDOWSIZE_MAX / XWindow 保持一致）。 */
#ifndef XWIDGET_MAX_SIZE
#define XWIDGET_MAX_SIZE 16777215
#endif /* XWIDGET_MAX_SIZE */

/** @brief 控件属性位偏移：属性值可达 129，32 位不够容纳，改用 192 位位段。 */
#define XWIDGET_ATTR_WORD(Attr) ((unsigned int)(Attr) >> 6)
#define XWIDGET_ATTR_MASK(Attr) (1ull << ((unsigned int)(Attr) & 63))

/** @brief 内部桥接窗口虚表枚举（XWidgetWindow 仅重载 Event 总入口）。 */
XCLASS_DEFINE_BEGING(XWidgetWindow)
XCLASS_DEFINE_EXTEND_END(XWidgetWindow, XWindow)

/**
 * @brief 顶层控件内嵌桥接窗口（内部类，XWindow 子类）。
 * @details XWidgetWindow 把平台/事件环投递到 XWindow 的事件转译为控件
 *          事件：鼠标/滚轮/进入先命中测试子控件并改写局部坐标，RESIZE
 *          先同步几何字段，PAINT 驱动顶层后备存储刷新；不回调 XWindow
 *          默认事件槽，避免双重分派。
 */
typedef struct XWidgetWindow
{
    XWindow m_base;      /**< 基类 XWindow；必须是第一个成员。 */
    XWidget* m_widget;   /**< 归属的顶层控件（借用指针，控件拥有窗口）。 */
} XWidgetWindow;

/** @brief 模块静态焦点控件；控件域内全局唯一（对标 QApplication::focusWidget）。 */
static XWidget* g_focusWidget = NULL;

/** @brief 模块静态鼠标抓取控件（对标 QApplication::mouseGrabber；控件抓取期间事件直投）。 */
static XWidget* g_mouseGrabWidget = NULL;
/** @brief 模块静态触摸隐式抓取表（对标 Qt 6.8 per-point 隐式抓取契约：
 *         qapplication.cpp:3791 activateImplicitTouchGrab 把 target 记于
 *         触点、:3824 非 Pressed 逐点取各自 target——同一序列各触点可各
 *         自抓取不同控件；取代旧单指针 g_touchGrabWidget「id=1 接受后
 *         id=2 全序列误投」的模型）。 */
#define XWIDGET_TOUCH_GRAB_CAPACITY 8 /**< per-id 抓取表容量（上限 8，超出
                                           防御性弃新不动旧表项）。 */
/** @brief 无触点列表的旧单点负载（XTouchEvent_init 形态）的主点哨兵 id：
 *         该形态全序列共用一个键位，行为等价旧单指针模型（保留 touch→
 *         mouse 仿真与既有清理语义）。 */
#define XWIDGET_TOUCH_PRIMARY_ID ((int32_t)0)

/** @brief 触点隐式抓取条目（对标 QMutableEventPoint::target 的 per-point
 *         记录；生命周期同触点序列，控件销毁/隐藏时全表摘除）。 */
typedef struct XTouchGrabEntry
{
    int32_t  m_id;      /**< 触点 id（平台 XI2 detail 透传）。 */
    XWidget* m_widget;  /**< 被隐式抓取的控件（借用指针）。 */
} XTouchGrabEntry;

/** @brief per-id 抓取表（槽位数组 + 计数；紧凑存储，删除尾部补位）。 */
static XTouchGrabEntry g_touchGrabTable[XWIDGET_TOUCH_GRAB_CAPACITY];
/** @brief 抓取表当前条目数（0=非抓取期）。 */
static int g_touchGrabCount = 0;
/** @brief touch→mouse 仿真开关（对标 Qt AA_SynthesizeMouseForUnhandledTouch-
 *         Events；Qt 6 对未处理触摸序列的鼠标仿真默认开启，应用可显式关闭）。 */
static bool g_touchMouseSynthEnabled = true;
/** @brief 当前触摸序列的 touch→mouse 仿真状态：BEGIN 未被任何控件接受时
 *         置位，END/CANCEL 清除（对标 Qt per-point synthesized-mouse 生命
 *         周期；单点模型简化为序列级标志）。同一 BEGIN 只走 touch 或仿真
 *         鼠标一条路。 */
static bool g_touchMouseSynthActive = false;
/* 应用模态控件（对标 QApplication 模态登记；经 XWidget_Protected.h
 * 供 XDialog/XApplication 读写，VXWidgetWindow_event 做输入拦截）。 */
static XWidget* g_applicationModalWidget = NULL;
/** @brief 模块静态键盘抓取控件（对标 QApplication::keyboardGrabber）。 */
static XWidget* g_keyboardGrabWidget = NULL;

/** @brief 焦点代理注册条目：owner 的 m_focusProxy 指向 proxy（借用）。
 * @details 用于代理销毁时自动摘除所有持有该代理的控件，避免悬空指针；
 *          生命周期与 XWidget 实例一致，不随模块注册表驱动线程变化。 */
typedef struct XFocusProxyEntry
{
    XWidget*                owner;   /**< 持有代理的控件。 */
    XWidget*                proxy;   /**< 被持有为代理的控件。 */
    struct XFocusProxyEntry* next;   /**< 下一项。 */
    struct XFocusProxyEntry* prev;   /**< 上一项。 */
} XFocusProxyEntry;

/** @brief 焦点代理注册表头（单链表；条目随 owner/proxy 生命周期登记清理）。 */
static XFocusProxyEntry* g_focusProxyEntries = NULL;




/* ==================== 静态函数前向声明 ==================== */

/** @brief 属性位置位/清位（对标 QWidget::setAttribute 内部实现）。 */
static void XWidget_attrSet(XWidgetAttributes* bits, XWidgetAttribute attr, bool on);
/** @brief 查询属性位（对标 QWidget::testAttribute 内部实现）。 */
static bool XWidget_attrTest(const XWidgetAttributes* bits, XWidgetAttribute attr);

#if XPAINTDEVICE_ON
/** @brief 控件绘制设备度量回调（定义见文件后部）。 */
static int xwidget_paintDeviceMetric(void* userData, int metric);
#endif /* XPAINTDEVICE_ON */

static void VXWidget_deinit(XWidget* self);
static void VXWidget_copy(XWidget* self, const XWidget* other);
static void VXWidget_move(XWidget* self, XWidget* other);
static bool VXWidget_event(XWidget* self, XEvent* event);
static bool VXWidgetWindow_event(XWidgetWindow* self, XEvent* event);
/** @brief XWidgetWindow 类虚函数表初始化（内部类，堆窗口对象使用）。 */
XVtable* XWidgetWindow_class_init(void);
static void XWidget_recomputeGeometry(XWidget* self, const XRect* old);
static void XWidget_updateContentsRect(XWidget* self);
static void XWidget_propagateVisibility(XWidget* self, bool parentVisible);
static XWidget* XWidget_topLevel(const XWidget* self);
static XWidgetWindow* XWidget_createWindow(XWidget* top);
static void XWidget_destroyWindow(XWidget* top);
static void XWidget_sendEvent(XWidget* self, XEvent* event);
static void XWidget_sendShowHide(XWidget* self, bool visible);
static void XWidget_clearFocusBase(XWidget* self, XFocusReason reason);
static XWidget* XWidget_deepestFocusProxy(const XWidget* self);
/** @brief 触摸事件槽入口（对标 QWidget::touchEvent 虚函数调用形态）。 */
void XWidget_touchEvent_base(XWidget* self, XEvent* event);
/** @brief 数位板事件槽入口（对标 QWidget::tabletEvent 虚函数调用形态）。 */
void XWidget_tabletEvent_base(XWidget* self, XEvent* event);
static void XFocusProxy_register(XWidget* owner);
static void XFocusProxy_cleanupFor(XWidget* self);
static void XWidget_propagateEnabled(XWidget* self, bool enabled);
static XEvent* XWidget_createPaintEvent(const XWidget* source);
static void XWidget_propagateLayoutDirection(XWidget* self,
                                             XWidgetLayoutDirection direction);
static void XWidget_clearUnderMouseRecursive(XWidget* self);
static void XWidget_addDirty(XWidget* self, const XRect* rect);
static void XWidget_addDirtyRegion(XWidget* self, const XRegion* region);
static void XWidget_paintTree(XWidget* top, const XRegion* topRegion);
static bool xwidget_drawWithGraphicsEffect(XWidget* widget,
                                           const XRegion* paintRegion);
static bool xwidget_focusNextPrevChild(XWidget* self, bool next);
static void XRegion_translateInline(XRegion* region, int dx, int dy);
/* ---- per-id 触点隐式抓取表（触摸派发域；定义紧随本节之后） ---- */
static XWidget* xwidget_touchGrabFind(int32_t id);
static void xwidget_touchGrabSet(int32_t id, XWidget* widget);
static void xwidget_touchGrabRemoveId(int32_t id);
static void xwidget_touchGrabClear(void);
static void xwidget_touchGrabRemoveWidget(XWidget* widget);
static int32_t xwidget_touchPrimaryId(const XTouchEvent* event);
static XPoint XWidget_accumulateOffset(const XWidget* self);
static void XWidget_eventSetPosition(XEvent* event, const XPoint* pos);
static XWidget* XWidget_dispatchInputAt(XWidget* top, XEvent* event);
static bool XWidget_dispatchTouchGroup(XWidget* top, const XEvent* source,
                                       XEventType type,
                                       const XTouchPoint* group, int count,
                                       XWidget* target, bool direct);

/** ==================== per-id 触点隐式抓取表（触摸派发域） ==================== */

/** @brief 按触点 id 查隐式抓取控件（对标 Qt 非 Pressed 点逐点取 target）。
 * @return 抓取控件；该 id 无表项返回 NULL。 */
static XWidget* xwidget_touchGrabFind(int32_t id)
{
    int i;
    for (i = 0; i < g_touchGrabCount; ++i) {
        if (g_touchGrabTable[i].m_id == id)
            return g_touchGrabTable[i].m_widget;
    }
    return NULL;
}

/** @brief 登记触点 id 的隐式抓取控件（对标 activateImplicitTouchGrab 记于
 *         触点；已有同 id 表项则改写，表满时防御性弃新）。 */
static void xwidget_touchGrabSet(int32_t id, XWidget* widget)
{
    int i;
    if (!widget) return;
    for (i = 0; i < g_touchGrabCount; ++i) {
        if (g_touchGrabTable[i].m_id == id) {
            g_touchGrabTable[i].m_widget = widget;
            return;
        }
    }
    if (g_touchGrabCount >= XWIDGET_TOUCH_GRAB_CAPACITY) return;
    g_touchGrabTable[g_touchGrabCount].m_id = id;
    g_touchGrabTable[g_touchGrabCount].m_widget = widget;
    ++g_touchGrabCount;
}

/** @brief 摘除指定触点 id 的抓取表项（TOUCH_END 按事件携带 id 逐 id 摘表）。 */
static void xwidget_touchGrabRemoveId(int32_t id)
{
    int i;
    for (i = 0; i < g_touchGrabCount; ++i) {
        if (g_touchGrabTable[i].m_id == id) {
            g_touchGrabTable[i] = g_touchGrabTable[g_touchGrabCount - 1];
            --g_touchGrabCount;
            return;
        }
    }
}

/** @brief 清空抓取表（TOUCH_CANCEL 全清；对标触点序列异常终止回收）。 */
static void xwidget_touchGrabClear(void)
{
    g_touchGrabCount = 0;
}

/** @brief 摘除某控件的全部抓取表项（控件销毁/隐藏路径；对标 Qt 隐式
 *         grab 随 target 失效。旧单指针模型只比较单槽，会漏多触点抓取
 *         同一控件的其余表项）。 */
static void xwidget_touchGrabRemoveWidget(XWidget* widget)
{
    int i;
    if (!widget) return;
    for (i = 0; i < g_touchGrabCount;) {
        if (g_touchGrabTable[i].m_widget == widget) {
            g_touchGrabTable[i] = g_touchGrabTable[g_touchGrabCount - 1];
            --g_touchGrabCount;
        } else {
            ++i;
        }
    }
}

/** @brief 触摸事件主点归一 id：有触点列表取 points[0].m_id；无列表的旧
 *         单点负载取主点哨兵（行为等价旧单指针模型）。 */
static int32_t xwidget_touchPrimaryId(const XTouchEvent* event)
{
    if (event && event->m_points && event->m_pointCount > 0)
        return event->m_points[0].m_id;
    return XWIDGET_TOUCH_PRIMARY_ID;
}

/** @brief 派发一个按靶分组的触点子事件（对标 qapplication.cpp:3840-3842
 *         widgetsNeedingEvents 按靶发送）。
 * @details direct=false 走命中路径（childAt + 父链传播；TouchBegin 被接受
 *          后逐点登记隐式抓取，对标 activateImplicitTouchGrab）；direct=
 *          true 抓取直达，跨顶层按该靶各自 topLevel 经全局坐标换算转投
 *          （对标鼠标抓取的同型兜底路由）。子事件只携带该靶触点，
 *          spontaneous 镜像源事件。
 * @return 子事件被接受返回 true；构造失败/无人接受返回 false。 */
static bool XWidget_dispatchTouchGroup(XWidget* top, const XEvent* source,
                                       XEventType type,
                                       const XTouchPoint* group, int count,
                                       XWidget* target, bool direct)
{
    XTouchEvent* sub;
    bool accepted = false;
    if (!top || !source || !group || count <= 0 || !target) return false;
    sub = XTouchEvent_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, type, NULL, NULL,
                                count);
    if (!sub) return false;
    XTouchEvent_setPoints(sub, group, count);
    if (!sub->m_points) {
        /* 触点列表分配失败：该组本批不派发（防御，等价丢点）。 */
        XEvent_delete_base((XEvent*)sub);
        return false;
    }
    XEvent_setAccepted_base((XEvent*)sub, false);
    sub->m_class.spontaneous = source->spontaneous;
    if (!direct) {
        /* 命中路径：按组首点位置 childAt + 父链传播；TouchBegin 被接受
           → 逐点 setGrab(id, receiver)（对标 qapplication.cpp:3791；
           activateImplicitTouchGrab 对非 Begin 早退——UPDATE/END 即使
           被接受也不新设抓取）。 */
        XWidget* recv = XWidget_dispatchInputAt(top, (XEvent*)sub);
        if (type == XEVENT_TYPE_TOUCH_BEGIN &&
            recv && XEvent_isAccepted((XEvent*)sub)) {
            const XTouchPoint* pts = XTouchEvent_points(sub);
            int k;
            for (k = 0; pts && k < count; ++k)
                xwidget_touchGrabSet(pts[k].m_id, recv);
        }
    } else {
        /* 抓取直达：组内各点按其 grab 靶的顶层换算坐标后直投，不再按
           命中测试分派（对标旧直达分支；多点时仅主点字段换算为靶局部
           坐标，触点列表保持顶层局部/全局原值——与旧单点模型同一坐标
           契约）。 */
        XWidget* grabTop = XWidget_topLevel(target);
        XPoint off = XWidget_accumulateOffset(target);
        XPoint pos = XTouchEvent_position(sub);
        XPoint local;
        if (grabTop && grabTop != top) {
            XPoint global = XWidget_mapToGlobal(top, &pos);
            pos = XWidget_mapFromGlobal(grabTop, &global);
        }
        local.x = pos.x - off.x;
        local.y = pos.y - off.y;
        XWidget_eventSetPosition((XEvent*)sub, &local);
        XWidget_sendEvent(target, (XEvent*)sub);
    }
    accepted = XEvent_isAccepted((XEvent*)sub);
    XEvent_delete_base((XEvent*)sub);
    return accepted;
}

/* ==================== 通用辅助函数 ==================== */

/** @brief 深拷贝字符串；源为 NULL 时返回 NULL。 */
static XString* XWidget_copyString(const XString* source)
{
    return source ? XString_create_copy(source) : NULL;
}

/** @brief 释放拥有字符串并置空。 */
static void XWidget_freeString(XString** slot)
{
    if (slot && *slot) {
        XString_delete_base((XClass*)*slot);
        *slot = NULL;
    }
}

/** @brief 判断控件是否真正可见（对标 QWidget::isVisible 的生效语义）。 */
static bool XWidget_effectiveVisible(const XWidget* self)
{
    if (!self) return false;
    if (!self->m_explicitShow) return false;
    if (self->m_isWindow) return self->m_visible;
    {
        const XWidget* parent = (const XWidget*)XObject_parent((XObject*)self);
        if (parent && !parent->m_visible) return false;
    }
    return self->m_explicitShow;
}

/** @brief 沿父链向上查找顶层控件（m_isWindow 或无父控件的控件）。 */
static XWidget* XWidget_topLevel(const XWidget* self)
{
    const XWidget* w = self;
    if (!w) return NULL;
    /* 经 XWidget_parentWidget 取父：非控件父（绕过控件 API 挂链）视同
       无父控件，终止上溯，避免按 XWidget* 解引用非控件结构体。 */
    while (w && !w->m_isWindow) {
        w = XWidget_parentWidget(w);
    }
    return (XWidget*)w;
}

/** @brief 创建与 XWindowEvent 解析约定一致的绘制事件。 */
static XEvent* XWidget_createPaintEvent(const XWidget* source)
{
    XRect rect;
    XWidget* top;
    XEvent* event;
    if (!source) return NULL;
    top = XWidget_topLevel(source);
#if XWINDOWEVENT_ON
    if (top && top->m_dirty.count > 0) {
        /* 构造函数会复制区域；直接传入脏区，避免先复制到临时区域再
           由事件对象复制一次。复用单槽把事件本体与区域数组的每帧
           malloc/free 归零。 */
        return (XEvent*)XPaintEvent_createRecycled(
            XCLASS_DEFAULT_MEMORY_TYPE, XEVENT_TYPE_PAINT, &top->m_dirty);
    }
    {
        rect = source->m_contentsRect;
        {
            XRegion region = { &rect, XRect_isEmpty(&rect) ? 0 : 1, 1 };
            event = (XEvent*)XPaintEvent_createRecycled(
                XCLASS_DEFAULT_MEMORY_TYPE, XEVENT_TYPE_PAINT, &region);
        }
    }
#else
    (void)top;
    event = XEvent_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, XEVENT_TYPE_PAINT);
#endif /* XWINDOWEVENT_ON */
    return event;
}

/** @brief 投递顶层控件绘制事件，并报告事件是否确实进入线程队列。 */
static bool XWidget_postPaintEvent(XWidget* top)
{
    XEvent* event;
    XObject* receiver;
    int32_t expected;
    if (!top || !top->m_windowHandle || !top->m_visible)
        return false;
    /* PendingUpdate 是脏区状态，不足以防止并发 update() 重复入队；这个
       独立占位位只代表一个 PAINT 事件。已有事件时请求已经被覆盖，
       直接视为成功，避免调用方错误清掉仍有效的 PendingUpdate。 */
    expected = 0;
    if (!XAtomic_compare_exchange_strong_int32(
            &top->m_paintEventPosted, &expected, 1,
            XAtomic_MemoryOrder_AcqRel, XAtomic_MemoryOrder_Acquire))
        return true;
    receiver = (XObject*)top->m_windowHandle;
    event = XWidget_createPaintEvent(top);
    if (!event) {
        XAtomic_store_int32(&top->m_paintEventPosted, 0,
                            XAtomic_MemoryOrder_Release);
        return false;
    }
    if (!XCoreApplication_tryPostEvent(receiver, event, 0)) {
        /* tryPostEvent 失败时不接管 event；由本调用方释放，并保留 dirty
           与 PendingUpdate，使后续 update/show 能再次尝试投递。 */
        XEvent_delete_base((XClass*)event);
        XAtomic_store_int32(&top->m_paintEventPosted, 0,
                            XAtomic_MemoryOrder_Release);
        return false;
    }
    return true;
}

/** @brief 判断 self 是否 child 的祖先控件。 */
static bool XWidget_isAncestor(const XWidget* self, const XWidget* child)
{
    const XWidget* widget;
    if (!self || !child) return false;
    widget = child;
    while (widget) {
        if (widget == self) return true;
        /* QWidget::isAncestorOf() 不跨越另一个顶层窗口的边界。 */
        if (widget != child && widget->m_isWindow) return false;
        widget = (const XWidget*)XObject_parent((XObject*)widget);
    }
    return false;
}

/** @brief 递归设置显式可见状态并沿树传播 SHOW/HIDE 事件。 */
static void XWidget_setExplicitVisibleRecursive(XWidget* self, bool visible,
                                                bool topLevel)
{
    bool oldVisible;
    const XWidget* parent;
    bool newVisible;
    if (!self) return;
    oldVisible = (self->m_visible != 0);
    self->m_explicitShow = visible ? 1 : 0;
    /* 显式 show/hide 同步 Qt::WA_WState_Hidden 位（对标 QWidget::setVisible）。 */
    XWidget_attrSet(&self->m_attributes, XWidgetAttribute_WState_Hidden,
                    !visible);
    if (topLevel) {
        newVisible = visible;
    } else {
        parent = (const XWidget*)XObject_parent((XObject*)self);
        newVisible = visible && (!parent || (parent->m_visible != 0));
    }
    self->m_visible = newVisible ? 1 : 0;
    if (newVisible != oldVisible) {
        /* 可见性变化联动保留层：该矩形区域的目标像素内容改变（显示
           前/隐藏后由父级背景与邻居填充），命中链上与交叠保留层全部
           作废。 */
        if (g_retainedRegistered > 0)
            xwidget_retainedInvalidateForRect(self);
        if (newVisible) {
            XWidget_sendShowHide(self, true);
        } else {
            if (g_focusWidget == self)
                XWidget_clearFocusBase(self, XFocusReason_Other);
            if (g_mouseGrabWidget == self)
                g_mouseGrabWidget = NULL;
            /* 隐藏路径全表遍历摘除该控件全部触点抓取表项（per-id 表）。 */
            xwidget_touchGrabRemoveWidget(self);
            if (g_keyboardGrabWidget == self)
                g_keyboardGrabWidget = NULL;
            XWidget_sendShowHide(self, false);
        }
    }
    XWidget_propagateVisibility(self, oldVisible);
}

/** @brief 可见变化时向子树传播 SHOW/HIDE 事件并修正焦点/脏区。 */
static void XWidget_propagateVisibility(XWidget* self, bool changedFromVisible)
{
    const XVector* children;
    size_t n;
    size_t i;
    if (!self) return;
    if (self->m_visible) {
        /* 显示：若窗口句柄存在且 PendingUpdate 则投递 PAINT。 */
        if (self->m_windowHandle &&
            (XWidget_attrTest(&self->m_attributes, XWidgetAttribute_PendingUpdate))) {
            if (!XWidget_postPaintEvent(self))
                XWidget_attrSet(&self->m_attributes,
                                XWidgetAttribute_PendingUpdate, false);
        }
    } else {
        /* 隐藏：清空焦点并派发 HIDE 事件。 */
        if (g_focusWidget == self)
            XWidget_clearFocusBase(self, XFocusReason_Other);
    }
    children = XObject_children((XObject*)self);
    n = children ? XVector_size_base((const XContainer*)children) : 0;
    for (i = 0; i < n; ++i) {
        XObject* child = *(XObject**)XVector_at_base(children, (int64_t)i);
        XWidget* widget;
        bool oldVisible;
        bool newVisible;
        if (!child || !child->is_widget) continue;
        widget = (XWidget*)child;
        /* 顶层窗口子控件不随父控件可见性传播（对标 showChildren 跳过 isWindow）。 */
        if (widget->m_isWindow) continue;
        oldVisible = (widget->m_visible != 0);
        if (self->m_visible) {
            /* 父显示：自动显示“从未显式隐藏”的子控件（对标 QWidgetPrivate::showChildren）。 */
            if (XWidget_attrTest(&widget->m_attributes,
                                 XWidgetAttribute_WState_Hidden))
                continue;
            widget->m_explicitShow = 1;
            newVisible = true;
        } else {
            /* 父隐藏：仅取消生效可见，保留显式状态（isHidden 仍为 false）。 */
            newVisible = false;
        }
        if (newVisible == oldVisible) continue;
        widget->m_visible = newVisible ? 1 : 0;
        /* 子树可见性传播联动保留层：生效可见翻转即像素内容改变。 */
        if (g_retainedRegistered > 0)
            xwidget_retainedInvalidateForRect(widget);
        if (newVisible) {
            if (widget->m_windowHandle) {
                if (!XWidget_postPaintEvent(widget))
                    XWidget_attrSet(&widget->m_attributes,
                                    XWidgetAttribute_PendingUpdate, false);
            }
            XWidget_sendShowHide(widget, true);
        } else {
            if (g_focusWidget == widget)
                XWidget_clearFocusBase(widget, XFocusReason_Other);
            XWidget_sendShowHide(widget, false);
        }
        XWidget_propagateVisibility(widget, oldVisible);
    }
    (void)changedFromVisible;
}

/** @brief 投递 SHOW/HIDE 事件到控件事件入口。 */
static void XWidget_sendShowHide(XWidget* self, bool visible)
{
#if XWINDOWEVENT_ON
    if (!self) return;
    if (visible) {
        XShowEvent event;
        XShowEvent_init(&event, XEVENT_TYPE_SHOW);
        XWidget_event_base(self, (XEvent*)&event);
        XShowEvent_deinit_base(&event);
    } else {
        XHideEvent event;
        XHideEvent_init(&event, XEVENT_TYPE_HIDE);
        XWidget_event_base(self, (XEvent*)&event);
        XHideEvent_deinit_base(&event);
    }
#else /* !XWINDOWEVENT_ON */
    (void)self;
    (void)visible;
#endif /* XWINDOWEVENT_ON */
}

/** @brief 发送事件到控件事件入口（事件由调用方持有并在派发后原地复用）。 */
static void XWidget_sendEvent(XWidget* self, XEvent* event)
{
    if (!self || !event) return;
    XWidget_event_base(self, event);
}

/** @brief 沿父链累加控件原点，得出 self 在顶层后备存储中的偏移量。 */
static XPoint XWidget_accumulateOffset(const XWidget* self)
{
    XPoint out;
    XPoint_init(&out, 0, 0);
    if (!self) return out;
    {
        const XWidget* w = self;
        while (w && !w->m_isWindow) {
            out.x += w->m_windowRect.x;
            out.y += w->m_windowRect.y;
            w = (const XWidget*)XObject_parent((XObject*)w);
        }
    }
    return out;
}

/**
 * @brief 沿父链查找生效的离屏重定向控件（XWidget_render/XWidget_grab 期间）。
 * @details XWidget_render/XWidget_grab 在绘制子树前把临时目标图像登记到
 *          重定向控件的 m_offscreenTarget，调用结束即复位；绘制过程中子树
 *          内任意控件经 XWidget_paintImage/XWidget_paintOffset 取绘制目标
 *          与偏移时，沿父链找到最近的登记控件即可完成重定向。取最近者
 *          保证快照流程内的嵌套 render/grab（对更深的子控件）语义正确。
 * @param      self 起始控件；可为 NULL。
 * @return     最近的离屏重定向控件；无重定向返回 NULL。
 */
static const XWidget* xwidget_redirectRoot(const XWidget* self)
{
    const XWidget* node = self;
    while (node) {
        if (node->m_offscreenTarget) return node;
        node = (const XWidget*)XObject_parent((XObject*)node);
    }
    return NULL;
}

/**
 * @brief 经典后备存储绘制偏移（accumulateOffset 减去后备存储绘制原点）。
 * @details XWidget_paintOffset 的历史语义。XWidget_render/XWidget_grab
 *          引入离屏重定向后，绘制期取偏移须走重定向分支；而 update() 折算
 *          顶层脏区（XWidget_addDirtyRegion）在重定向期间也必须继续按
 *          顶层后备存储坐标累计，因此脏区路径改用本函数固定旧行为。
 * @param      self 目标控件；可为 NULL。
 * @return     self 局部坐标到顶层后备存储坐标的平移量。
 */
static XPoint xwidget_backingPaintOffset(const XWidget* self)
{
    XPoint offset = XWidget_accumulateOffset(self);
#if XBACKINGSTORE_ON && XPLATFORMBACKINGSTORE_ON && XPLATFORMINTEGRATION_ON
    XBackingStore* store = XWidget_backingStore(self);
    if (store) {
        XPoint origin = XBackingStore_paintOrigin(store);
        offset.x -= origin.x;
        offset.y -= origin.y;
    }
#endif /* XBACKINGSTORE_ON && XPLATFORMBACKINGSTORE_ON && XPLATFORMINTEGRATION_ON */
    return offset;
}

/** @brief XRegion 内全部矩形平移（原地）。 */
static void XRegion_translateInline(XRegion* region, int dx, int dy)
{
    int i;
    if (!region || (dx == 0 && dy == 0)) return;
    for (i = 0; i < region->count; ++i) {
        region->rects[i].x += dx;
        region->rects[i].y += dy;
    }
}

/** @brief 区域与矩形求交并写入已有输出区域。 */
static void XRegion_intersectRectInto(const XRegion* region, const XRect* rect,
                                      XRegion* out)
{
    int i;
    if (!out) return;
    XRegion_clear(out);
    if (!region || !rect || XRect_isEmpty(rect)) return;
    for (i = 0; i < region->count; ++i) {
        XRect r = XRect_intersected(&region->rects[i], rect);
        if (!XRect_isEmpty(&r)) XRegion_addRect(out, &r);
    }
}

/** @brief 两个区域求交并写入已有输出区域；调用方保证 out 不与输入别名。 */
static void XRegion_intersectInto(const XRegion* lhs, const XRegion* rhs,
                                  XRegion* out)
{
    int i;
    int j;
    if (!out) return;
    XRegion_clear(out);
    if (!lhs || !rhs) return;
    for (i = 0; i < lhs->count; ++i)
        for (j = 0; j < rhs->count; ++j) {
            XRect r = XRect_intersected(&lhs->rects[i], &rhs->rects[j]);
            if (!XRect_isEmpty(&r)) XRegion_addRect(out, &r);
        }
}

/** @brief 创建并登记顶层桥接窗口（惰性；窗口对象由本控件拥有）。 */
static XWidgetWindow* XWidget_createWindow(XWidget* top)
{
    XWidgetWindow* win;
    XWindow* window;
    if (!top || top->m_windowHandle) return top ? top->m_windowHandle : NULL;
    win = (XWidgetWindow*)XMemory_malloc(sizeof(XWidgetWindow),
                                         XCLASS_DEFAULT_MEMORY_TYPE);
    if (!win) return NULL;
    XMemset(win, 0, sizeof(XWidgetWindow));
    XWindow_init(&win->m_base);
    XClassSetVtable(win, XWidgetWindow);
    Set_Class_Memory(win, XCLASS_DEFAULT_MEMORY_TYPE);
    Set_Class_IsHeap(win, true);
    win->m_widget = top;
    top->m_windowHandle = win;

    window = (XWindow*)win;
    XWindow_setFlags(window, (XWindowFlags)top->m_windowFlags);
    if (top->m_windowTitle)
        XWindow_setTitle(window, top->m_windowTitle);
    XWindow_setIcon(window, &top->m_icon);
    if (top->m_windowFilePath)
        XWindow_setFilePath(window, top->m_windowFilePath);
    XWindow_setOpacity(window, top->m_windowOpacity);
    if (top->m_mask.count > 0)
        XWindow_setMask(window, &top->m_mask);
    XWindow_setModality(window, top->m_windowModality);
    XWindow_setWindowStates(window, top->m_windowState);
    XWindow_setMinimumSize(window, &top->m_minimumSize);
    XWindow_setMaximumSize(window, &top->m_maximumSize);
    XWindow_setGeometry(window, top->m_windowRect.x, top->m_windowRect.y,
                        top->m_windowRect.width, top->m_windowRect.height);
#if XCURSOR_ON
    if (top->m_cursor && XWindow_setCursor)
        XWindow_setCursor(window, top->m_cursor);
    /* 惰性建窗补生效：setCursor 先于窗口创建时（平台后端尚未注册或
       winId 未就绪而静默），窗口建立后补做一次原生应用。 */
    if (top->m_cursor)
        (void)XCursor_applyToWindow((uintptr_t)XWindow_winId(window),
                                    top->m_cursor);
#endif /* XCURSOR_ON */
#if XAPPLICATION_ON && XGUIAPPLICATION_ON
    XApplication_registerTopLevelWidget(top);
#endif /* XAPPLICATION_ON */
    return win;
}

/** @brief 销毁顶层桥接窗口并归还注册表项。 */
static void XWidget_destroyWindow(XWidget* top)
{
    if (!top || !top->m_windowHandle) return;
#if XAPPLICATION_ON && XGUIAPPLICATION_ON
    XApplication_unregisterTopLevelWidget(top);
#endif /* XAPPLICATION_ON */
    XWindow_destroy((XWindow*)top->m_windowHandle);
    /* 桥接窗口由 create_ex 堆分配，销毁后必须同时归还结构体。 */
    XClass_delete_base((XClass*)top->m_windowHandle);
    top->m_windowHandle = NULL;
}

/** @brief 焦点清空基础实现（发 FOCUS_OUT 并联动应用登记）。 */
static void XWidget_clearFocusBase(XWidget* self, XFocusReason reason)
{
#if XWINDOWEVENT_ON
    XFocusEvent event;
    if (!self || g_focusWidget != self) return;
    XFocusEvent_init(&event, XEVENT_TYPE_FOCUS_OUT, reason);
    XWidget_event_base(self, (XEvent*)&event);
    XFocusEvent_deinit_base(&event);
#endif /* XWINDOWEVENT_ON */
    g_focusWidget = NULL;
#if XAPPLICATION_ON && XGUIAPPLICATION_ON
    XApplication_setFocusWidget(NULL);
#endif /* XAPPLICATION_ON */
}

/** @brief 向子控件传播 Qt WA_Disabled 语义，保留子控件的显式禁用状态。 */
static void XWidget_propagateEnabled(XWidget* self, bool enabled)
{
    const XVector* children;
    size_t n;
    size_t i;
    if (!self) return;
    children = XObject_children((XObject*)self);
    n = children ? XVector_size_base((const XContainer*)children) : 0;
    for (i = 0; i < n; ++i) {
        XObject* child = *(XObject**)XVector_at_base(children, (int64_t)i);
        XWidget* widget;
        bool oldEnabled;
        bool newEnabled;
        XEvent event;
        if (!child || !child->is_widget) continue;
        widget = (XWidget*)child;
        /* 启用父控件时，显式禁用的子控件及其子树保持禁用；父控件
         * 禁用时仍需给所有子控件置 WA_Disabled，保持属性可观察性。 */
        if (enabled && XWidget_attrTest(&widget->m_attributes,
                                        XWidgetAttribute_ForceDisabled))
            continue;
        oldEnabled = XWidget_isEnabled(widget);
        widget->m_enabled = enabled ? 1 : 0;
        XWidget_attrSet(&widget->m_attributes, XWidgetAttribute_Disabled,
                        !enabled);
        newEnabled = XWidget_isEnabled(widget);
        if (oldEnabled != newEnabled) {
            XEvent_init(&event, XEVENT_TYPE_ENABLED_CHANGE);
            XWidget_event_base(widget, &event);
            XWidget_update(widget);
        }
        XWidget_propagateEnabled(widget, enabled);
    }
}

/** @brief 清除控件及其子树的 WA_UnderMouse 状态。 */
static void XWidget_clearUnderMouseRecursive(XWidget* self)
{
    const XVector* children;
    size_t n;
    size_t i;
    if (!self) return;
    XWidget_attrSet(&self->m_attributes, XWidgetAttribute_UnderMouse, false);
    children = XObject_children((XObject*)self);
    n = children ? XVector_size_base((const XContainer*)children) : 0;
    for (i = 0; i < n; ++i) {
        XObject* child = *(XObject**)XVector_at_base(children, (int64_t)i);
        if (child && child->is_widget)
            XWidget_clearUnderMouseRecursive((XWidget*)child);
    }
}

/** @brief 把局部区域折算到顶层坐标后并入顶层脏区。 */
static void XWidget_addDirtyRegion(XWidget* self, const XRegion* region)
{
    XWidget* top;
    int i;
    XPoint offset;
    XRect contents;
    const XRegion* source;
    XRegion sourceCopy;
    bool copiedSource;
    if (!self || !region || !self->m_updatesEnabled) return;
    self->m_contentCacheDirty = true;
    /* 保留层失效联动：update/updateRect/updateRegion/repaint 全部经此
       入口，沿父链命中保留层祖先、并与脏区相交的其他保留层一并作废。
       未启用保留层时仅一次全局计数布尔判断（零回归红线）。 */
    if (g_retainedRegistered > 0)
        xwidget_retainedInvalidateForRect(self);
    top = XWidget_topLevel(self);
    if (!top) return;
    /* 脏区恒按顶层后备存储坐标折算：render/grab 重定向期间 paintEvent
       内触发的 update() 也必须落到顶层真实脏区，而非临时快照坐标系。 */
    offset = xwidget_backingPaintOffset(self);
    contents = self->m_contentsRect;
    source = region;
    copiedSource = false;
    XRegion_init(&sourceCopy);
    /* 正常 update 路径直接逐矩形裁剪，避免每次刷新先创建临时区域。
       只有调用方把顶层脏区本身作为输入时才需要快照，避免边遍历边合并
       修改输入数组。 */
    if (region == &top->m_dirty) {
        XRegion_copy(region, &sourceCopy);
        source = &sourceCopy;
        copiedSource = true;
    }
    for (i = 0; i < source->count; ++i) {
        XRect clipped = XRect_intersected(&source->rects[i], &contents);
        if (!XRect_isEmpty(&clipped)) {
            XRect translated = XRect_translated(&clipped, offset.x, offset.y);
            XRegion_addRect(&top->m_dirty, &translated);
        }
    }
    if (copiedSource)
        XRegion_deinit(&sourceCopy);
    XWidget_attrSet(&top->m_attributes, XWidgetAttribute_PendingUpdate, true);
    if (top->m_windowHandle && top->m_visible)
        (void)XWidget_postPaintEvent(top);
}

/** @brief 把局部矩形折算到顶层坐标后并入顶层脏区。 */
static void XWidget_addDirty(XWidget* self, const XRect* rect)
{
    XRegion region;
    XRect r;
    if (!self || !rect) return;
    r = *rect;
    /* XWidget_addDirtyRegion 只读输入区域；用栈上单矩形视图避免每次
       updateRect() 都申请并释放一个临时 XRegion。 */
    region.rects = &r;
    region.count = 1;
    region.capacity = 1;
    XWidget_addDirtyRegion(self, &region);
}

/* ==================== 尺寸策略（对标 QSizePolicy） ==================== */

/** @brief 计算 32 位值的尾随零个数（控制类型位索引）。 */
static int XWidgetSizePolicy_ctz(uint32_t value)
{
    int count = 0;
    if (!value) return 0;
    while (!(value & 1u)) {
        value >>= 1;
        ++count;
    }
    return count;
}

XWidgetSizePolicy XWidgetSizePolicy_create(void)
{
    XWidgetSizePolicy policy;
    XMemset(&policy, 0, sizeof(policy));
    policy.m_horizontalPolicy = XWidgetSizePolicy_Preferred;
    policy.m_verticalPolicy = XWidgetSizePolicy_Preferred;
    policy.m_controlType = XWidgetSizePolicy_ctz(
        (uint32_t)XWidgetSizePolicyControl_DefaultType);
    return policy;
}

XWidgetSizePolicy XWidgetSizePolicy_create_ex(
    XWidgetSizePolicyPolicy horizontal, XWidgetSizePolicyPolicy vertical,
    XWidgetSizePolicyControlType controlType)
{
    XWidgetSizePolicy policy = XWidgetSizePolicy_create();
    policy.m_horizontalPolicy = (uint8_t)horizontal;
    policy.m_verticalPolicy = (uint8_t)vertical;
    if (controlType)
        policy.m_controlType = (uint8_t)XWidgetSizePolicy_ctz(
            (uint32_t)controlType);
    return policy;
}

XWidgetSizePolicyPolicy XWidgetSizePolicy_horizontalPolicy(
    const XWidgetSizePolicy* self)
{
    return self ? (XWidgetSizePolicyPolicy)self->m_horizontalPolicy
                : XWidgetSizePolicy_Preferred;
}

XWidgetSizePolicyPolicy XWidgetSizePolicy_verticalPolicy(
    const XWidgetSizePolicy* self)
{
    return self ? (XWidgetSizePolicyPolicy)self->m_verticalPolicy
                : XWidgetSizePolicy_Preferred;
}

void XWidgetSizePolicy_setHorizontalPolicy(XWidgetSizePolicy* self,
                                           XWidgetSizePolicyPolicy policy)
{
    if (self) self->m_horizontalPolicy = (uint8_t)policy;
}

void XWidgetSizePolicy_setVerticalPolicy(XWidgetSizePolicy* self,
                                         XWidgetSizePolicyPolicy policy)
{
    if (self) self->m_verticalPolicy = (uint8_t)policy;
}

XWidgetSizePolicyControlType XWidgetSizePolicy_controlType(
    const XWidgetSizePolicy* self)
{
    if (!self) return XWidgetSizePolicyControl_DefaultType;
    return (XWidgetSizePolicyControlType)(1u << self->m_controlType);
}

void XWidgetSizePolicy_setControlType(XWidgetSizePolicy* self,
                                      XWidgetSizePolicyControlType type)
{
    if (self)
        self->m_controlType = (uint8_t)XWidgetSizePolicy_ctz((uint32_t)type);
}

/** @brief 策略是否可扩展（对标 Qt 6.8 QSizePolicy 的 ExpandFlag=0x02 位）。
 *  @details 与 qsizepolicy.h 一致：只有带 ExpandFlag 位的策略
 *  （MinimumExpanding=3 / Expanding=7）会向布局声明可扩展；Minimum、
 *  Preferred、Maximum、Fixed、Ignored 均不向布局要求多余空间。 */
static bool XWidgetSizePolicy_expands(XWidgetSizePolicyPolicy policy)
{
    return (policy & 0x02u) != 0;
}

int XWidgetSizePolicy_expandingDirections(const XWidgetSizePolicy* self)
{
    int directions = 0;
    if (!self) return 0;
    if (XWidgetSizePolicy_expands(
            (XWidgetSizePolicyPolicy)self->m_horizontalPolicy))
        directions |= 1;
    if (XWidgetSizePolicy_expands(
            (XWidgetSizePolicyPolicy)self->m_verticalPolicy))
        directions |= 2;
    return directions;
}

bool XWidgetSizePolicy_hasHeightForWidth(const XWidgetSizePolicy* self)
{
    return self ? self->m_hasHeightForWidth : false;
}

void XWidgetSizePolicy_setHeightForWidth(XWidgetSizePolicy* self, bool enabled)
{
    if (self) self->m_hasHeightForWidth = enabled;
}

bool XWidgetSizePolicy_hasWidthForHeight(const XWidgetSizePolicy* self)
{
    return self ? self->m_hasWidthForHeight : false;
}

void XWidgetSizePolicy_setWidthForHeight(XWidgetSizePolicy* self, bool enabled)
{
    if (self) self->m_hasWidthForHeight = enabled;
}

int XWidgetSizePolicy_horizontalStretch(const XWidgetSizePolicy* self)
{
    return self ? self->m_horizontalStretch : 0;
}

int XWidgetSizePolicy_verticalStretch(const XWidgetSizePolicy* self)
{
    return self ? self->m_verticalStretch : 0;
}

void XWidgetSizePolicy_setHorizontalStretch(XWidgetSizePolicy* self, int stretch)
{
    if (self) {
        if (stretch < 0) stretch = 0;
        if (stretch > 255) stretch = 255;
        self->m_horizontalStretch = (uint8_t)stretch;
    }
}

void XWidgetSizePolicy_setVerticalStretch(XWidgetSizePolicy* self, int stretch)
{
    if (self) {
        if (stretch < 0) stretch = 0;
        if (stretch > 255) stretch = 255;
        self->m_verticalStretch = (uint8_t)stretch;
    }
}

bool XWidgetSizePolicy_retainSizeWhenHidden(const XWidgetSizePolicy* self)
{
    return self ? self->m_retainSizeWhenHidden : false;
}

void XWidgetSizePolicy_setRetainSizeWhenHidden(XWidgetSizePolicy* self,
                                               bool retain)
{
    if (self) self->m_retainSizeWhenHidden = retain;
}

XWidgetSizePolicy XWidgetSizePolicy_transposed(const XWidgetSizePolicy* self)
{
    XWidgetSizePolicy out = *self;
    uint8_t tmp;
    if (!self) return XWidgetSizePolicy_create();
    tmp = out.m_horizontalPolicy;
    out.m_horizontalPolicy = out.m_verticalPolicy;
    out.m_verticalPolicy = tmp;
    tmp = out.m_horizontalStretch;
    out.m_horizontalStretch = out.m_verticalStretch;
    out.m_verticalStretch = tmp;
    return out;
}

bool XWidgetSizePolicy_isEqual(const XWidgetSizePolicy* a,
                               const XWidgetSizePolicy* b)
{
    if (a == b) return true;
    if (!a || !b) return false;
    return a->m_horizontalPolicy == b->m_horizontalPolicy &&
           a->m_verticalPolicy == b->m_verticalPolicy &&
           a->m_controlType == b->m_controlType &&
           a->m_horizontalStretch == b->m_horizontalStretch &&
           a->m_verticalStretch == b->m_verticalStretch &&
           a->m_hasHeightForWidth == b->m_hasHeightForWidth &&
           a->m_hasWidthForHeight == b->m_hasWidthForHeight &&
           a->m_retainSizeWhenHidden == b->m_retainSizeWhenHidden;
}

/* ==================== 属性位集（对标 QWidget::setAttribute 内部实现） ==================== */


/** @brief 属性位置位/清位：按 192 位位段（3×uint64_t）寻址。 */
static void XWidget_attrSet(XWidgetAttributes* bits, XWidgetAttribute attr, bool on)
{
    unsigned int word;
    uint64_t mask;
    if (!bits || attr < 0 || attr >= XWidgetAttribute_AttributeCount) return;
    word = XWIDGET_ATTR_WORD(attr);
    if (word >= 3) return;
    mask = XWIDGET_ATTR_MASK(attr);
    if (on)
        bits->m_bits[word] |= mask;
    else
        bits->m_bits[word] &= ~mask;
}

/** @brief 查询属性位：越界属性一律视为未置位。 */
static bool XWidget_attrTest(const XWidgetAttributes* bits, XWidgetAttribute attr)
{
    unsigned int word;
    if (!bits || attr < 0 || attr >= XWidgetAttribute_AttributeCount) return false;
    word = XWIDGET_ATTR_WORD(attr);
    if (word >= 3) return false;
    return (bits->m_bits[word] & XWIDGET_ATTR_MASK(attr)) != 0;
}

/* ==================== 通用几何辅助 ==================== */

/** @brief 把尺寸值钳位到 [lo, hi]；hi<lo 时按 hi 优先（Qt 语义）。 */
static int XWidget_clampSize(int value, int lo, int hi)
{
    if (value < lo) value = lo;
    if (hi >= lo && value > hi) value = hi;
    return value;
}

/** @brief 事件中携带的指针位置（鼠标/滚轮/进入事件；其余返回零点）。 */
static XPoint XWidget_eventPosition(const XEvent* event)
{
    XPoint out;
    XPoint_init(&out, 0, 0);
    if (!event) return out;
    switch (XEvent_type(event)) {
    case XEVENT_TYPE_MOUSE_BUTTON_PRESS:
    case XEVENT_TYPE_MOUSE_BUTTON_RELEASE:
    case XEVENT_TYPE_MOUSE_BUTTON_DBL_CLICK:
    case XEVENT_TYPE_MOUSE_MOVE:
        out = ((const XMouseEvent*)event)->m_position;
        break;
#if XWINDOWEVENT_ON
    case XEVENT_TYPE_WHEEL:
        out = ((const XWheelEvent*)event)->m_position;
        break;
    case XEVENT_TYPE_ENTER:
        out = ((const XEnterEvent*)event)->m_position;
        break;
    case XEVENT_TYPE_CONTEXT_MENU:
        out = ((const XContextMenuEvent*)event)->m_position;
        break;
    /* 触摸/数位板按主点坐标参与命中测试（对标 QWidgetWindow::handleTouchEvent
       的按触点位置 childAt 形态）。 */
    case XEVENT_TYPE_TOUCH_BEGIN:
    case XEVENT_TYPE_TOUCH_UPDATE:
    case XEVENT_TYPE_TOUCH_END:
    case XEVENT_TYPE_TOUCH_CANCEL:
        out = ((const XTouchEvent*)event)->m_position;
        break;
    case XEVENT_TYPE_TABLET_PRESS:
    case XEVENT_TYPE_TABLET_RELEASE:
    case XEVENT_TYPE_TABLET_MOVE:
        out = ((const XTabletEvent*)event)->m_position;
        break;
#endif /* XWINDOWEVENT_ON */
    default:
        break;
    }
    return out;
}

/** @brief 改写事件中携带的指针位置（命中测试后换算为接收控件局部坐标）。 */
static void XWidget_eventSetPosition(XEvent* event, const XPoint* pos)
{
    if (!event || !pos) return;
    switch (XEvent_type(event)) {
    case XEVENT_TYPE_MOUSE_BUTTON_PRESS:
    case XEVENT_TYPE_MOUSE_BUTTON_RELEASE:
    case XEVENT_TYPE_MOUSE_BUTTON_DBL_CLICK:
    case XEVENT_TYPE_MOUSE_MOVE:
        ((XMouseEvent*)event)->m_position = *pos;
        break;
#if XWINDOWEVENT_ON
    case XEVENT_TYPE_WHEEL:
        ((XWheelEvent*)event)->m_position = *pos;
        break;
    case XEVENT_TYPE_ENTER:
        ((XEnterEvent*)event)->m_position = *pos;
        break;
    case XEVENT_TYPE_CONTEXT_MENU:
        ((XContextMenuEvent*)event)->m_position = *pos;
        break;
    /* 命中后换算为接收控件局部坐标（与鼠标事件同一坐标契约）。 */
    case XEVENT_TYPE_TOUCH_BEGIN:
    case XEVENT_TYPE_TOUCH_UPDATE:
    case XEVENT_TYPE_TOUCH_END:
    case XEVENT_TYPE_TOUCH_CANCEL:
        ((XTouchEvent*)event)->m_position = *pos;
        break;
    case XEVENT_TYPE_TABLET_PRESS:
    case XEVENT_TYPE_TABLET_RELEASE:
    case XEVENT_TYPE_TABLET_MOVE:
        ((XTabletEvent*)event)->m_position = *pos;
        break;
#endif /* XWINDOWEVENT_ON */
    default:
        break;
    }
}

/** @brief 从目标控件开始沿父链向顶层投递指针事件（位置逐级换算）。 */
static bool XWidget_dispatchPointerEvent(XWidget* top, XEvent* event)
{
    XWidget* target;
    XPoint pos;
    XWidget* w;
    if (!top || !event) return false;
    if (XWidget_attrTest(&top->m_attributes, XWidgetAttribute_TransparentForMouseEvents))
        return false;
    pos = XWidget_eventPosition(event);
    if (g_mouseGrabWidget) {
        XWidget* grabTop = XWidget_topLevel(g_mouseGrabWidget);
        if (grabTop && grabTop != top) {
            /* 跨顶层窗口的全局鼠标抓取（如弹出菜单的模态关闭）：把事件
             * 坐标换算到抓取窗口的坐标系后转投，使点击其它窗口也能送达
             * 抓取控件（对标 QWidget::grabMouse 的全局语义；平台
             * XGrabPointer 可能因映射时序未生效，此路由作可靠兜底）。 */
            XPoint global = XWidget_mapToGlobal(top, &pos);
            XPoint local = XWidget_mapFromGlobal(grabTop, &global);
            XWidget_eventSetPosition(event, &local);
            return XWidget_dispatchPointerEvent(grabTop, event);
        }
    }
    if (g_mouseGrabWidget &&
        XWidget_topLevel(g_mouseGrabWidget) == top) {
        /* 鼠标抓取：直接投递抓取控件，不再按命中测试分派（对标 QWidget grabMouse）。 */
        target = g_mouseGrabWidget;
    } else {
        target = XWidget_childAt(top, &pos);
        if (!target) {
            const XRegion* topMask = top ? &top->m_mask : NULL;
            /* 顶层自身有遮罩且点不在遮罩内时不再回退到顶层（对该点不派发）。 */
            if (!topMask || topMask->count <= 0 ||
                XRegion_contains(topMask, pos.x, pos.y))
                target = top;
        }
    }
    w = target;
    while (w) {
        XPoint off = XWidget_accumulateOffset(w);
        XPoint local;
        local.x = pos.x - off.x;
        local.y = pos.y - off.y;
        XWidget_eventSetPosition(event, &local);
        if (!XWidget_attrTest(&w->m_attributes, XWidgetAttribute_TransparentForMouseEvents)) {
            XWidget_sendEvent(w, event);
#if XWINDOWSYSTEMINTERFACE_ON && XWINDOW_ON && XWINDOWEVENT_ON
            /* 悬停合成器回报钩子（第八轮 R1 悬停收口，见
               XWindowSystemInterface.c xwsi_hoverTopForWindow 回退④注）：
               原生窗边界 ENTER 经本桥命中实投时，把首个非透明接收者
               （w==target，与 xwsi_hoverPickTarget 同口径）回报给合成器
               登记——纯悬停冷会话三锚全空时，合成器据此以登记靶为回退锚
               解析窗→顶层并换靶。合成器自派的链上 ENTER 走
               XCoreApplication_sendEvent 直投控件事件槽、不经本桥，无重
               入；传播链后续接收者不回报（悬停靶=命中靶，非上抛靶）。 */
            if (XEvent_type(event) == XEVENT_TYPE_ENTER && w == target)
                XWindowSystemInterface_setHoverTarget(w);
#endif
            if (XEvent_isAccepted(event)) return true;
            if (XWidget_attrTest(&w->m_attributes, XWidgetAttribute_NoMousePropagation)) break;
        }
        if (w == top) break;
        w = XWidget_parentWidget(w);
    }
#if XWINDOWEVENT_ON
    /* 对标 Qt：右键按下未被接受时合成上下文菜单事件，发给命中控件
       （QGuiApplicationPrivate::processMouseEvent 在 press 未接受且
       button==RightButton 时向窗口合成 QContextMenuEvent，最终由
       QWidgetWindow 转发到鼠标下控件）。
       坐标契约：local 必须逐接收者换算（pos − 累计偏移），global 以
       顶层本地坐标统一换算——此前 local 取传播循环改写后的最后一个
       接收者坐标、global 又按 target 本地坐标换算，双重偏移导致菜单
       弹出位置错误（14.124 补充四 ①）。
       传递契约：接收者未显式接受时沿父链继续投递——滚动区内部
       viewport 等无上下文菜单槽的控件会静默吞掉事件，导致多行编辑
       右键无菜单（14.124 补充四 ②）；Qt 中被忽略的 QContextMenuEvent
       同样交由父级处理。 */
    if (!XEvent_isAccepted(event) &&
        XEvent_type(event) == XEVENT_TYPE_MOUSE_BUTTON_PRESS &&
        ((const XMouseEvent*)event)->m_button == XMouseButton_RightButton) {
        XWidget* w = target;
        while (w) {
            XContextMenuEvent* ctx;
            XPoint off = XWidget_accumulateOffset(w);
            XPoint local;
            XPoint global;
            local.x = pos.x - off.x;
            local.y = pos.y - off.y;
            global = XWidget_mapToGlobal(top, &pos);
            ctx = XContextMenuEvent_create_ex(
                XCLASS_DEFAULT_MEMORY_TYPE, XEVENT_TYPE_CONTEXT_MENU, &local,
                &global, XContextMenuReason_Mouse,
                ((const XMouseEvent*)event)->m_modifiers);
            if (ctx) {
                XEvent_ignore((XEvent*)ctx); /* 默认忽略：处理者显式接受 */
                XWidget_sendEvent(w, (XEvent*)ctx);
                if (XEvent_isAccepted((XEvent*)ctx)) {
                    XEvent_delete_base((XEvent*)ctx);
                    return true;
                }
                XEvent_delete_base((XEvent*)ctx);
            }
            if (w == top) break;
            w = XWidget_parentWidget(w);
        }
    }
#endif /* XWINDOWEVENT_ON */
    return XEvent_isAccepted(event);
}

/* ==================== ShortcutOverride 询问（对标 QEvent::ShortcutOverride） ==================== */

/** @brief 虚表槽位指针比较用的通用函数指针型（仅作恒等比较，不调用）。 */
typedef void (*XWidgetVtableSlot)(void);

#if XLINEEDIT_ON && XLINECONTROL_ON
/**
 * @brief      行编辑族判定并取编辑控制器（ShortcutOverride 接收侧，内部）。
 * @details    判据=虚表 Copy 槽与 XLineEdit_class_init() 共享表同函数指针：
 *             XVTABLE_INHERIT_XCLASS(XLineEdit) 把基类槽位复制进子类表，
 *             而 XComboEdit（XComboBox.c 内嵌可编辑下拉框行编辑）与
 *             XItemLineEditor（XItemDelegate.c 视图单元格编辑器）均只覆写
 *             按键/失焦槽，结构上等价 is-a-XLineEdit，并自动覆盖未来未
 *             覆写 Copy 槽的子类；两个子类结构体均以 XLineEdit m_base 为
 *             首成员（XComboEdit 见 XComboBox.c xcomboEdit_create），故
 *             (XLineEdit*) 向下转换地址不变。壳→控制器转交对标
 *             QLineEdit::event 的 ShortcutOverride 分支
 *             （qlineedit.cpp:1451 → control->processShortcutOverrideEvent）。
 * @param      self 待判定控件；可为 NULL。
 * @return     行编辑族的编辑控制器；非行编辑族返回 NULL。
 */
static XLineControl* xwidget_shortcutOverrideLineControl(XWidget* self)
{
    XVtable* vt;
    XVtable* le;
    if (!self) return NULL;
    vt = XClassGetVtable(self);
    le = XLineEdit_class_init();
    if (!vt || !le) return NULL;
    if (XVtableGetFunc(vt, EXClass_Copy, XWidgetVtableSlot) ==
        XVtableGetFunc(le, EXClass_Copy, XWidgetVtableSlot))
        return ((XLineEdit*)self)->m_control;
    return NULL;
}
#endif /* XLINEEDIT_ON && XLINECONTROL_ON */

#if XTEXTCONTROL_ON && XPLAINTEXTEDIT_ON
/**
 * @brief      多行编辑族 ShortcutOverride 转交（接收侧，内部）。
 * @details    判据=虚表 Deinit 槽与 XPlainTextEdit_class_init() 同函数指针
 *             （XPlainTextEdit 仅覆写 Deinit 槽；XTextEdit 的键入经聚焦的
 *             内嵌 XPlainTextEdit 承载，同落本判据）。命中后转交
 *             XTextControl_processEvent——其 XEVENT_TYPE_SHORTCUT_OVERRIDE
 *             分支按既有口径决定 accept/ignore；非本族控件不转交，事件
 *             保持忽略（快捷键照常激活）。
 * @param      self 接收到询问事件的控件；可为 NULL。
 * @param      ke   ShortcutOverride 询问事件。
 */
static void xwidget_shortcutOverrideDelegate(XWidget* self, XKeyEvent* ke)
{
    XVtable* vt;
    XVtable* pte;
    if (!self || !ke) return;
    vt = XClassGetVtable(self);
    pte = XPlainTextEdit_class_init();
    if (!vt || !pte) return;
    if (XVtableGetFunc(vt, EXClass_Deinit, XWidgetVtableSlot) ==
        XVtableGetFunc(pte, EXClass_Deinit, XWidgetVtableSlot)) {
        XPlainTextEdit* edit = (XPlainTextEdit*)self;
        if (edit->m_control)
            XTextControl_processEvent(edit->m_control, (XEvent*)ke);
    }
}
#endif /* XTEXTCONTROL_ON && XPLAINTEXTEDIT_ON */

/**
 * @brief      快捷键激活前的 ShortcutOverride 询问（内部）。
 * @details    对标 qt_sendShortcutOverrideEvent
 *             （qwindowsysteminterface.cpp:1175-1199）与
 *             qapplication.cpp:2665-2675：命中快捷键后、激活前，向焦点
 *             对象发送 QEvent::ShortcutOverride；控件 accept（文本编辑
 *             要吃这颗键，如行编辑的裸字母/Shift+字母）则调用方跳过
 *             快捷键激活，按键随后按正常路径送达焦点控件；ignore 才
 *             真正激活快捷键。询问范围与下方按键投递同口径：仅同顶层
 *             的当前焦点控件；禁用控件不询问（对标 QWidget::event 丢弃
 *             禁用控件的键盘输入）。
 * @param      top  正在派发按键的顶层控件。
 * @param      key  原始 KEY_PRESS 事件。
 * @return     true=焦点控件接受覆盖（跳过 XShortcut_activate）。
 */
static bool xwidget_shortcutOverrideAsk(const XWidget* top, const XKeyEvent* key)
{
    XWidget* focus = g_focusWidget;
    XKeyEvent ask;
    if (!focus || !key) return false;
    if (XWidget_topLevel(focus) != top) return false;
    if (!XWidget_isEnabled(focus)) return false;
    XKeyEvent_init(&ask, XEVENT_TYPE_SHORTCUT_OVERRIDE,
                   key->m_key, key->m_modifiers);
    XEvent_ignore((XEvent*)&ask); /* 默认忽略：接收侧显式 accept 才覆盖 */
    XWidget_sendEvent(focus, (XEvent*)&ask);
    XClass_deinit_base((XClass*)&ask); /* 栈上询问事件：无堆资源，走基类清理 */
    return XEvent_isAccepted((XEvent*)&ask);
}

/** @brief 键盘事件投递：优先焦点控件，其次顶层控件；未接受沿父链上抛。 */
static bool XWidget_dispatchKeyEvent(const XWidget* top, XEvent* event)
{
    XWidget* target;
    XEventType type;
    bool bubble;
    if (!top || !event) return false;
    type = XEvent_type(event);
    /* 快捷键优先（对标 QShortcutMap：按键先过快捷键表，命中即消费）。
     * 命中后、激活前先向焦点控件发 ShortcutOverride 询问（对标
     * qt_sendShortcutOverrideEvent，qwindowsysteminterface.cpp:1175）：
     * 焦点是文本编辑控件且这颗键要进编辑框（如裸字母 t 对 #32b）时
     * accept → 跳过快捷键，按键继续走下方焦点控件正常投递；否则照旧
     * 激活快捷键。 */
    if (type == XEVENT_TYPE_KEY_PRESS) {
        XShortcut* sc = XShortcut_match(
            (int)((XKeyEvent*)event)->m_key, (XShortcutContext)0,
            g_focusWidget);
        if (sc) {
            if (!xwidget_shortcutOverrideAsk(top, (const XKeyEvent*)event)) {
                XShortcut_activate(sc);
                return true;
            }
        }
    }
    target = g_keyboardGrabWidget;
    if (!target || XWidget_topLevel(target) != top)
        target = g_focusWidget;
    if (!target || XWidget_topLevel(target) != top) target = (XWidget*)top;
    /* 仅按键按下/释放参与父链上抛；输入法事件仍单点投递（对标 Qt：
       QInputMethodEvent 只发焦点控件，不做父链传播）。键盘抓取
       （g_keyboardGrabWidget）仅决定起点，沿用既有目标选择逻辑。 */
    bubble = (type == XEVENT_TYPE_KEY_PRESS ||
              type == XEVENT_TYPE_KEY_RELEASE);
    if (target) {
        XWidget* w = target;
        while (w) {
            XWidget_sendEvent(w, event);
            /* 对标 Qt：焦点控件 ignore 的按键沿父链逐级重投（QKeyEvent
               在 QApplication::notify 的父链传播），接受即止；到达顶层
               或链中窗口型控件不再外抛。QDialog 依赖该传播实现"行编辑
               有焦点时 Esc 也 reject"：子行编辑 ignore → 对话框
               keyPress 收到 Esc → reject。此前仅投单个 target，ignore
               后事件直接丢弃（复扫 P1-1；上抛范式与
               XWidget_dispatchPointerEvent 的父链传播同口径）。 */
            if (XEvent_isAccepted(event)) return true;
            if (w == (XWidget*)top || w->m_isWindow) break;
            w = XWidget_parentWidget(w);
        }
        return XEvent_isAccepted(event);
    }
    return XWidget_event_base((XWidget*)top, event);
}

/** @brief 按命中测试 + 父链传播投递定位类输入事件（触摸/数位板共用）。
 * @details 参照 XWidget_dispatchPointerEvent 的命中与坐标平移方式，但不含
 *          鼠标抓取与右键上下文菜单合成（两者是 QMouseEvent 专属语义）。
 *          命中：childAt 按主点位置（对标 QWidgetWindow::handleTouchEvent
 *          / handleTabletEvent 的 childAt 形态）；传播：接收者未接受时沿
 *          父链继续（与鼠标同一 accept 语义）。
 * @return 接受事件的控件（含沿父链上溯后接受者）；无人接受返回 NULL。 */
static XWidget* XWidget_dispatchInputAt(XWidget* top, XEvent* event)
{
    XWidget* target;
    XPoint pos;
    XWidget* w;
    if (!top || !event) return NULL;
    pos = XWidget_eventPosition(event);
    target = XWidget_childAt(top, &pos);
    if (!target) {
        const XRegion* topMask = &top->m_mask;
        /* 与指针派发一致：顶层有遮罩且点不在遮罩内时不派发。 */
        if (topMask->count > 0 && !XRegion_contains(topMask, pos.x, pos.y))
            return NULL;
        target = top;
    }
    w = target;
    while (w) {
        XPoint off = XWidget_accumulateOffset(w);
        XPoint local;
        local.x = pos.x - off.x;
        local.y = pos.y - off.y;
        XWidget_eventSetPosition(event, &local);
        if (!XWidget_attrTest(&w->m_attributes, XWidgetAttribute_TransparentForMouseEvents)) {
            XWidget_sendEvent(w, event);
            if (XEvent_isAccepted(event)) return w;
            if (XWidget_attrTest(&w->m_attributes, XWidgetAttribute_NoMousePropagation)) break;
        }
        if (w == top) break;
        w = XWidget_parentWidget(w);
    }
    return NULL;
}

/** @brief 合成鼠标事件并复用鼠标命中/派发管线（对标 QGuiApplicationPrivate::
 *         synthesizeMouseFromTouchEvents 的 QMouseEvent 语义）。
 * @details topLocal 为触摸主点的顶层局部坐标（与鼠标事件同一坐标系契约），
 *          经 XWidget_dispatchPointerEvent 做命中测试 + 父链传播 + 逐接收者
 *          坐标换算；来源标志置 m_synthesized=1（对标 Qt 的
 *          MouseEventSynthesizedBySystem，经 XMouseEvent_isSynthesized
 *          可与真实鼠标区分）。
 * @return 合成鼠标事件被接受返回 true；分配失败/无人接受返回 false。 */
static bool XWidget_synthesizeMouseFromTouch(XWidget* top, XEventType type,
                                             XMouseButton button,
                                             XMouseButton buttons,
                                             const XPoint* topLocal)
{
    XMouseEvent* mouse;
    bool accepted;
    if (!top || !topLocal) return false;
    mouse = XMouseEvent_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, type, button,
                                  (XKeyboardModifiers)XKeyboardModifier_NoModifier,
                                  *topLocal);
    if (!mouse) return false;
    XMouseEvent_setButtons(mouse, buttons);
    XMouseEvent_setSynthesized(mouse, true); /* 合成来源标志。 */
    /* 触摸时间戳透传：合成鼠标事件继承源触摸序列时间（对标 Qt 合成
       QMouseEvent 继承触摸 timestamp 语义）；无同步触摸派发时为 0。 */
#if XWINDOWSYSTEMINTERFACE_ON && XWINDOW_ON && XWINDOWEVENT_ON
    XMouseEvent_setTimestamp(mouse, XWindowSystemInterface_touchTimestamp());
#endif
    accepted = XWidget_dispatchPointerEvent(top, (XEvent*)mouse);
    XEvent_delete_base((XEvent*)mouse);
    return accepted;
}

/** @brief 触摸事件命中派发：per-id 触点隐式抓取 + 按靶分组派发 +
 *         touch→mouse 仿真（对标 QWidgetWindow::handleTouchEvent /
 *         QApplicationPrivate::translateRawTouchEvent，Qt 6.8
 *         qapplication.cpp:3791-3842 per-point 契约）。
 * @details Qt 语义：BEGIN（Pressed）逐点 childAt 命中；被接受的触点按 id
 *          记入隐式抓取表（activateImplicitTouchGrab 记于触点）；非
 *          Pressed 逐点取各自 target，抓取期查无该 id 的点丢弃
 *          （:3824-3826 if(!target) continue）；连续同靶点合并为一次
 *          投递（:3840-3842 按靶分组）。跨顶层按各点各自 grab 的
 *          topLevel 换算转投。END 按事件携带 id 逐 id 摘表，CANCEL 全清。
 *          TouchBegin 未被任何控件接受的序列进入鼠标仿真（对标
 *          AA_SynthesizeMouseForUnhandledTouchEvents，默认开启）：合成
 *          MOUSE_BUTTON_PRESS（坐标同触摸点）、UPDATE→MOUSE_MOVE、
 *          END→MOUSE_BUTTON_RELEASE，复用鼠标命中/派发管线；同一 BEGIN
 *          只走 touch 或仿真鼠标一条路。多点模型下仿真门控仅由主点 id
 *          驱动（主点被抓取即整批不合成；非主点不单独合成）。无触点
 *          列表的旧单点负载以主点字段合成单点、id 取主点哨兵，行为
 *          等价旧单指针模型。 */
static bool XWidget_dispatchTouchEvent(XWidget* top, XEvent* event)
{
    XTouchEvent* te = (XTouchEvent*)event;
    XEventType type;
    XPoint topLocal;
    int32_t primaryId;
    bool anyAccepted = false;
    bool primaryAccepted = false;
    XTouchPoint group[XWIDGET_TOUCH_GRAB_CAPACITY];
    XTouchPoint pt;
    XWidget* groupTarget = NULL;
    bool groupDirect = false;
    bool groupHasPrimary = false;
    int groupCount = 0;
    int total;
    int i;
    if (!top || !event) return false;
    if (XWidget_attrTest(&top->m_attributes, XWidgetAttribute_TransparentForMouseEvents))
        return false;
    type = XEvent_type(event);
    /* 顶层局部坐标快照：后续命中派发会把事件位置改写为接收者局部坐标，
       仿真鼠标必须以同一触摸主点坐标进入鼠标管线。 */
    topLocal = XWidget_eventPosition(event);
    /* 新序列开始：防御性清理上一序列可能残留的仿真状态（平台漏发 END）。 */
    if (type == XEVENT_TYPE_TOUCH_BEGIN)
        g_touchMouseSynthActive = false;
    /* 逐点路由（对标 translateRawTouchEvent 逐点循环）：BEGIN 逐点
       childAt 命中；非 BEGIN 逐点取各自抓取靶，抓取期查无该 id 丢点。
       抓取表为空（非抓取期）时保持既有主点命中形态——touch→mouse
       仿真序列从不抓取，语义与旧单指针模型逐位一致。 */
    total = te->m_points ? te->m_pointCount : 1;
    primaryId = xwidget_touchPrimaryId(te);
    for (i = 0; i < total; ++i) {
        XWidget* target;
        bool direct;
        if (te->m_points) {
            pt = te->m_points[i];
        } else {
            /* 旧单点负载（无触点列表）：主点字段合成单触点，id 取主点
               哨兵。 */
            pt.m_id = primaryId;
            pt.m_state = (type == XEVENT_TYPE_TOUCH_BEGIN)
                             ? XTOUCHPOINT_STATE_PRESSED
                             : ((type == XEVENT_TYPE_TOUCH_END)
                                    ? XTOUCHPOINT_STATE_RELEASED
                                    : XTOUCHPOINT_STATE_UPDATED);
            pt.m_position = te->m_position;
            pt.m_globalPosition = te->m_globalPosition;
            pt.m_pressure = 1.0f;
        }
        if (type == XEVENT_TYPE_TOUCH_BEGIN || g_touchGrabCount == 0) {
            /* Pressed 点（及非抓取期）：逐点 childAt 命中，含顶层遮罩
               回退（与 dispatchInputAt 同口径）。 */
            target = XWidget_childAt(top, &pt.m_position);
            if (!target) {
                const XRegion* topMask = &top->m_mask;
                if (topMask->count > 0 &&
                    !XRegion_contains(topMask, pt.m_position.x, pt.m_position.y))
                    target = NULL; /* 遮罩外：该点不派发。 */
                else
                    target = top;
            }
            direct = false;
        } else {
            /* 非 Pressed：逐点取各自抓取靶；抓取期查无该 id 的点丢弃
               （对标 qapplication.cpp:3824-3826 if(!target) continue）。 */
            target = xwidget_touchGrabFind(pt.m_id);
            if (!target) continue;
            direct = true;
        }
        if (groupCount > 0 &&
            (target != groupTarget || direct != groupDirect ||
             groupCount >= XWIDGET_TOUCH_GRAB_CAPACITY)) {
            /* 靶/路径切换或组满：先派发已积组。 */
            if (XWidget_dispatchTouchGroup(top, event, type, group, groupCount,
                                           groupTarget, groupDirect)) {
                anyAccepted = true;
                if (groupHasPrimary) primaryAccepted = true;
            }
            groupCount = 0;
            groupHasPrimary = false;
        }
        if (!target) continue; /* 遮罩外点：不派发、不分组。 */
        if (groupCount == 0) {
            groupTarget = target;
            groupDirect = direct;
        }
        group[groupCount++] = pt;
        if (i == 0) groupHasPrimary = true;
    }
    if (groupCount > 0) {
        if (XWidget_dispatchTouchGroup(top, event, type, group, groupCount,
                                       groupTarget, groupDirect)) {
            anyAccepted = true;
            if (groupHasPrimary) primaryAccepted = true;
        }
    }
    /* touch→mouse 仿真：仅主点 id 驱动——主点已被触点隐式抓取时整批
       不合成；BEGIN 未被接受（主点无抓取且命中组未被接受）时开启，
       之后整条序列持续合成，END 合成释放后复位（对标 Qt per-point
       状态机的单点收敛；回归锁「touch→mouse 仿真 itemClicked」实证
       TouchBegin 被接受才抓取、不合成）。 */
    if (g_touchMouseSynthEnabled && !xwidget_touchGrabFind(primaryId)) {
        if (type == XEVENT_TYPE_TOUCH_BEGIN && !primaryAccepted) {
            g_touchMouseSynthActive = true;
            /* 对标 Qt：合成 press 携带 LeftButton（button 与 buttons 一致）。 */
            XWidget_synthesizeMouseFromTouch(top,
                XEVENT_TYPE_MOUSE_BUTTON_PRESS, XMouseButton_LeftButton,
                XMouseButton_LeftButton, &topLocal);
        } else if (type == XEVENT_TYPE_TOUCH_UPDATE && g_touchMouseSynthActive) {
            /* move：button 为 NoButton，buttons 保持按压态。 */
            XWidget_synthesizeMouseFromTouch(top, XEVENT_TYPE_MOUSE_MOVE,
                XMouseButton_NoButton, XMouseButton_LeftButton, &topLocal);
        } else if (type == XEVENT_TYPE_TOUCH_END && g_touchMouseSynthActive) {
            /* release：button 为 LeftButton，buttons 为剩余按压（空）。 */
            XWidget_synthesizeMouseFromTouch(top,
                XEVENT_TYPE_MOUSE_BUTTON_RELEASE, XMouseButton_LeftButton,
                XMouseButton_NoButton, &topLocal);
        }
    }
    /* Qt 语义：序列结束（END）按事件携带 id 逐 id 摘表；CANCEL 全清；
       两态均复位仿真状态。 */
    if (type == XEVENT_TYPE_TOUCH_END) {
        if (te->m_points) {
            int k;
            for (k = 0; k < te->m_pointCount; ++k)
                xwidget_touchGrabRemoveId(te->m_points[k].m_id);
        } else {
            xwidget_touchGrabRemoveId(primaryId);
        }
        g_touchMouseSynthActive = false;
    } else if (type == XEVENT_TYPE_TOUCH_CANCEL) {
        xwidget_touchGrabClear();
        g_touchMouseSynthActive = false;
    }
    return anyAccepted;
}

/** @brief 数位板事件命中派发：与鼠标一致的按压命中路径（对标 QWidgetWindow::
 *         handleTabletEvent——数位板事件按鼠标同型 childAt 命中 + 父链传播，
 *         只是负载多压力/指针类型；不参与触摸抓取）。 */
static bool XWidget_dispatchTabletEvent(XWidget* top, XEvent* event)
{
    if (!top || !event) return false;
    if (XWidget_attrTest(&top->m_attributes, XWidgetAttribute_TransparentForMouseEvents))
        return false;
    (void)XWidget_dispatchInputAt(top, event);
    return XEvent_isAccepted(event);
}

/* ==================== 25 个默认事件槽（对标 QWidget 默认实现） ==================== */

/** @brief 默认绘制槽：autoFillBackground 时用活动组 Window 色填充绘制矩形。 */
static void XWidget_paintEvent_default(XWidget* self, XEvent* event)
{
#if !XPALETTE_ON || !XWINDOWEVENT_ON
    (void)self; (void)event;
    return;
#else
    XPaintEvent* pe;
    XRect rect;
    XPoint offset;
    XImage* image;
    XPalette palette;
    XColor color;
    if (!self || !event || XEvent_type(event) != XEVENT_TYPE_PAINT) return;
    if (!self->m_autoFillBackground) return;
    pe = (XPaintEvent*)event;
    palette = XWidget_palette(self);
    color = XPalette_color(&palette, XPaletteColorGroup_Active,
                           XPaletteColorRole_Window);
    rect = XPaintEvent_rect(pe);
    offset = XWidget_paintOffset(self);
    rect.x += offset.x;
    rect.y += offset.y;
    /* 只填充事件脏区：容器整页背景在最大化下约 250 万像素/帧，
       而小区域刷新（性能浮层/光标闪烁）的常见脏区远小于控件矩形。
       Qt 的 fillRegion 语义同样是按事件区域回填背景。 */
    {
        /* rect 已折算为顶层后备存储坐标：裁剪须按控件在顶层的实际
         * 矩形 (offset, w, h)。此前按局部 (0,0,w,h) 裁剪，非窗口原点
         * 的子控件（offset 非 0）填充矩形被整体裁掉（填充缺失根因）。
         * 顶层窗口 offset=(0,0)，行为与旧实现逐位一致。 */
        int w = XWidget_width(self);
        int h = XWidget_height(self);
        int wx = offset.x;
        int wy = offset.y;
        if (rect.x < wx) { rect.width -= wx - rect.x; rect.x = wx; }
        if (rect.y < wy) { rect.height -= wy - rect.y; rect.y = wy; }
        if (rect.x + rect.width > wx + w) rect.width = wx + w - rect.x;
        if (rect.y + rect.height > wy + h) rect.height = wy + h - rect.y;
        if (rect.width <= 0 || rect.height <= 0) return;
    }
    image = XWidget_paintImage(self);
    if (image)
        XImage_fillRect(image, &rect, XColor_rgba(&color));
#endif /* XPALETTE_ON */
}

/** @brief 默认关闭槽：接受关闭（对标 QWidget::closeEvent 默认接受）。 */
static void XWidget_closeEvent_default(XWidget* self, XEvent* event)
{
    (void)self;
    if (event) XEvent_accept(event);
}

/** @brief 默认输入忽略槽：键盘/鼠标/滚轮/进入/离开默认忽略以允许父链冒泡。 */
static void XWidget_ignoreEvent_default(XWidget* self, XEvent* event)
{
    (void)self;
    if (event) XEvent_ignore(event);
}

/** @brief 默认拖放槽：仅在 setAcceptDrops(true) 时接受。 */
static void XWidget_dropEvent_default(XWidget* self, XEvent* event)
{
    if (!event) return;
    if (self && self->m_acceptDrops) XEvent_accept(event);
    else XEvent_ignore(event);
}

/** @brief 默认空实现槽：resize/move/focus/show/hide 默认无操作。 */
static void XWidget_noopEvent_default(XWidget* self, XEvent* event)
{
    (void)self;
    (void)event;
}

/** @brief 焦点变化默认槽：聚焦/失焦即重绘（对标 QWidget::focusInEvent /
 *         focusOutEvent 默认 update()，qwidget.cpp:9714/:9740）。
 * @details 样式层 HasFocus 焦点框（dotted frame）只在重绘时呈现——
 *          此前默认槽为空操作，Tab 移动焦点后新旧焦点控件都不重绘，
 *          焦点框要等无关重绘才出现甚至永不出现（问题 #13 键盘焦点
 *          完全不可见的另一半根因）。 */
static void XWidget_focusEvent_default(XWidget* self, XEvent* event)
{
    (void)event;
    if (self) XWidget_update(self);
}

/* ==================== XWidgetWindow 桥接窗口类 ==================== */

XVtable* XWidgetWindow_class_init(void)
{
    XVTABLE_INIT_DEFAULT(XWidgetWindow)
    XVTABLE_INHERIT_XCLASS(XWindow);
    XVTABLE_OVERLOAD_DEFAULT(EXObject_Event, VXWidgetWindow_event);
    return XVTABLE_DEFAULT;
}

/** @brief 顶层控件是否为 Popup 型原生窗（模态门豁免判定，见
 *  VXWidgetWindow_event 应用模态拦截注释）。无桥接窗按非 Popup 处理。 */
static bool XWidget_topIsPopup(const XWidget* top)
{
    return top && top->m_windowHandle &&
           XWindow_type((XWindow*)top->m_windowHandle) ==
               XWindowType_Popup;
}

/** @brief 顶层桥接窗口事件总入口：把窗口事件转译为控件事件。 */
static bool VXWidgetWindow_event(XWidgetWindow* self, XEvent* event)
{
    XWidget* top;
    XEventType type;
    if (!self || !event) return false;
    top = self->m_widget;
    if (!top) {
        return XClass_Parent(XWindow, EXObject_Event,
                             bool(*)(XObject*, XEvent*))((XObject*)self, event);
    }
    type = XEvent_type(event);
    /* 应用模态输入拦截（对标 QApplication 模态语义）：存在活动模态
       控件时，非模态顶层窗口的输入事件一律吞掉（对标 Qt
       QGuiApplicationPrivate::isWindowBlocked）。
       Popup 豁免（页6 文件对话框组合框弹层不可选根修，2026-09-25）：
       Qt::Popup 是模态面板的瞬态输入延伸——Qt 模态对话框内的
       QComboBox 下拉/QMenu 弹层可正常交互，isWindowBlocked 不阻塞
       Popup。本框架子控件形态对话框（XFileDialog/XInputDialog/
       QColorDialog，flags 无 Window 位）的 modalTop=主窗顶层，而组合
       框弹层（XComboPopupView）是独立顶层 Popup 窗：不加豁免时，平台
       XGrabPointer/XGrabKeyboard 把用户输入全部重定向进弹层原生窗，
       却在本门被 modalTop!=top 吞掉——弹层行点击/Return/Esc 全部失效
       且点击外部无法收层（弹层僵尸化，实测 :120 页6 复现）。弹层关闭
       经 hidePopup 焦点回交宿主（XComboBox_hidePopup_base），模态语义
       不受损。 */
    if (g_applicationModalWidget) {
        static const XEventType inputTypes[] = {
            XEVENT_TYPE_MOUSE_BUTTON_PRESS, XEVENT_TYPE_MOUSE_BUTTON_RELEASE,
            XEVENT_TYPE_MOUSE_BUTTON_DBL_CLICK, XEVENT_TYPE_MOUSE_MOVE,
            XEVENT_TYPE_WHEEL, XEVENT_TYPE_KEY_PRESS,
            XEVENT_TYPE_KEY_RELEASE, XEVENT_TYPE_CONTEXT_MENU,
            /* 触摸/数位板同属输入事件：模态阻塞窗口一并吞掉（对标
               QGuiApplicationPrivate::isWindowBlocked 对全部输入生效）。 */
            XEVENT_TYPE_TOUCH_BEGIN, XEVENT_TYPE_TOUCH_UPDATE,
            XEVENT_TYPE_TOUCH_END, XEVENT_TYPE_TOUCH_CANCEL,
            XEVENT_TYPE_TABLET_PRESS, XEVENT_TYPE_TABLET_RELEASE,
            XEVENT_TYPE_TABLET_MOVE
        };
        size_t ti;
        for (ti = 0; ti < sizeof(inputTypes) / sizeof(inputTypes[0]); ++ti) {
            if (type == inputTypes[ti]) {
                XWidget* modalTop =
                    XWidget_topLevel(g_applicationModalWidget);
                if (modalTop != top && !XWidget_topIsPopup(top)) {
                    XEvent_accept(event);
                    return true;
                }
                break;
            }
        }
    }
    switch (type) {
    case XEVENT_TYPE_RESIZE: {
        XResizeEvent* re = (XResizeEvent*)event;
        XSize oldSize = XResizeEvent_oldSize(re);
        XRect geometry = XWindow_geometry((XWindow*)self);
        XWidget_applyWindowGeometry(top, &geometry, &oldSize);
        return true;
    }
    case XEVENT_TYPE_EXPOSE: {
        XRegion region;
        XRect rect;
        /* 原生窗口首次映射或重新暴露后，服务器端像素不再可靠。即使
         * X11 只报告最后一块 Expose，也必须把顶层后备存储完整合成并
         * 提交；否则高频局部 update 只能补回悬浮层等脏区，留下黑底。
         * 这对应 QWidgetWindow 收到 expose 后重建可见窗口内容的边界。 */
        XRegion_init(&region);
        rect = XWidget_rect(top);
        XRegion_addRect(&region, &rect);
        XWidget_flushBackingStore(top, &region);
        XRegion_deinit(&region);
        XEvent_accept(event);
        return true;
    }
    case XEVENT_TYPE_PAINT: {
        /* PAINT 事件已被事件循环取出并开始处理，清掉投递占位位，
           允许本次绘制期间或之后产生的 update() 再次入队。必须先于
           flush 清零：flush 内部 paintEvent 触发的 update 依赖该位为 0
           才能重新投递，否则绘制中产生的脏区会被永久吞掉。 */
        XAtomic_store_int32(&top->m_paintEventPosted, 0,
                            XAtomic_MemoryOrder_Release);
#if XWINDOWEVENT_ON
        /* 借用事件内部区域直接交给 flush（flush 对 region 只读：copy 进
           whole、与 m_dirty 求差都不写入），省掉 XPaintEvent_region 的
           深拷贝 + XRegion_deinit —— 每帧固定 1 次 malloc/free。事件本体
           由投递方在派发返回后统一释放，借用生命周期覆盖整个同步 flush。 */
        XWidget_flushBackingStore(top,
                                  &((XPaintEvent*)event)->m_region);
#else
        {
            XRegion region;
            XRect rect = XWidget_rect(top);
            XRegion_init(&region);
            XRegion_addRect(&region, &rect);
            XWidget_flushBackingStore(top, &region);
            XRegion_deinit(&region);
        }
#endif /* XWINDOWEVENT_ON */
        XEvent_accept(event);
        return true;
    }
    case XEVENT_TYPE_MOUSE_BUTTON_PRESS:
    case XEVENT_TYPE_MOUSE_BUTTON_RELEASE:
    case XEVENT_TYPE_MOUSE_BUTTON_DBL_CLICK:
    case XEVENT_TYPE_MOUSE_MOVE:
    case XEVENT_TYPE_WHEEL:
    case XEVENT_TYPE_ENTER:
        return XWidget_dispatchPointerEvent(top, event);
    case XEVENT_TYPE_TOUCH_BEGIN:
    case XEVENT_TYPE_TOUCH_UPDATE:
    case XEVENT_TYPE_TOUCH_END:
    case XEVENT_TYPE_TOUCH_CANCEL:
        /* 对标 QWidgetWindow::handleTouchEvent：桥接窗口把窗口级触摸事件
           按主点命中转发控件 touchEvent 虚槽。 */
        return XWidget_dispatchTouchEvent(top, event);
    case XEVENT_TYPE_TABLET_PRESS:
    case XEVENT_TYPE_TABLET_RELEASE:
    case XEVENT_TYPE_TABLET_MOVE:
        /* 对标 QWidgetWindow::handleTabletEvent：数位板事件按鼠标一致
           的命中路径转发控件 tabletEvent 虚槽。 */
        return XWidget_dispatchTabletEvent(top, event);
    case XEVENT_TYPE_LEAVE:
        XWidget_clearUnderMouseRecursive(top);
        XWidget_sendEvent(top, event);
        return XEvent_isAccepted(event);
    case XEVENT_TYPE_KEY_PRESS:
    case XEVENT_TYPE_KEY_RELEASE:
    case XEVENT_TYPE_INPUT_METHOD:
        return XWidget_dispatchKeyEvent(top, event);
    case XEVENT_TYPE_CONTEXT_MENU:
        /* 对标 Qt：上下文菜单事件发给鼠标下控件（QWidgetWindow::
           handleContextMenuEvent 命中转发）。 */
        return XWidget_dispatchPointerEvent(top, event);
    case XEVENT_TYPE_DRAG_ENTER:
    case XEVENT_TYPE_DRAG_MOVE:
    case XEVENT_TYPE_DRAG_LEAVE:
    case XEVENT_TYPE_DROP:
        XWidget_sendEvent(top, event);
        return XEvent_isAccepted(event);
    case XEVENT_TYPE_CLOSE:
        XWidget_sendEvent(top, event);
        return XEvent_isAccepted(event);
    case XEVENT_TYPE_SHOW:
    case XEVENT_TYPE_HIDE:
        /* 对标 Qt：showEvent/hideEvent 由 QWidget::setVisible 经
           XWidget_sendShowHide 按可见性翻转恰好发送一次；桥接窗口
           自身的映射/取消映射事件（XWindow_setVisible 合成）不再
           转发控件，避免同一翻转双次发射 visibilityChanged 等信号。 */
        XEvent_accept(event);
        return true;
    default:
        return XWidget_event_base(top, event);
    }
}

/* ==================== XWidget 类初始化与生命周期 ==================== */

#if XINPUTMETHOD_ON
/** @brief XWidget_inputMethodQuery 基类默认实现（对标 QWidget::inputMethodQuery
 *         默认实现；各查询项返回值见 XWidget.h 声明处文档）。 */
static XVariant* XWidget_inputMethodQuery_default(const XWidget* self,
                                                  XInputMethodQuery query)
{
    if (!self) return NULL;
    switch (query) {
    case XInputMethodQuery_ImCursorRectangle:
        /* 对标 Qt：QRectF(width() / 2.0, 0, 1, height())。 */
        {
            XRectF rect;
            rect.x = (float)XWidget_width(self) / 2.0f;
            rect.y = 0.0f;
            rect.width = 1.0f;
            rect.height = (float)XWidget_height(self);
            return XVariant_create(&rect, sizeof(rect), XVariantType_User);
        }
    case XInputMethodQuery_ImInputItemClipRectangle:
        /* 对标 Qt：QRectF(rect())，控件矩形的浮点副本。 */
        {
            XRect rect = XWidget_rect(self);
            XRectF rectF;
            rectF.x = (float)rect.x;
            rectF.y = (float)rect.y;
            rectF.width = (float)rect.width;
            rectF.height = (float)rect.height;
            return XVariant_create(&rectF, sizeof(rectF), XVariantType_User);
        }
    case XInputMethodQuery_ImHints:
        /* 对标 Qt：(int)inputMethodHints()。 */
        {
            int32_t value = (int32_t)XWidget_inputMethodHints(self);
            return XVariant_create(&value, sizeof(value), XVariantType_Int32);
        }
    case XInputMethodQuery_ImEnabled:
        /* 对标 Qt：QVariant(true)。 */
        {
            bool enabled = true;
            return XVariant_create(&enabled, sizeof(enabled), XVariantType_Bool);
        }
    default:
        /* 对标 Qt：其余查询项返回无效 QVariant（NULL 等价）。 */
        return NULL;
    }
}
#endif /* XINPUTMETHOD_ON */

XVtable* XWidget_class_init(void)
{
    XVTABLE_INIT_DEFAULT(XWidget)
    XVTABLE_INHERIT_XCLASS(XObject);
    void* table[] = {
        XWidget_paintEvent_default,        /* PaintEvent */
        XWidget_noopEvent_default,         /* ResizeEvent */
        XWidget_noopEvent_default,         /* MoveEvent */
        XWidget_closeEvent_default,        /* CloseEvent */
        XWidget_focusEvent_default,        /* FocusInEvent（对标 QWidget::focusInEvent 默认 update()） */
        XWidget_focusEvent_default,        /* FocusOutEvent（对标 QWidget::focusOutEvent 默认 update()） */
        XWidget_ignoreEvent_default,       /* EnterEvent */
        XWidget_ignoreEvent_default,       /* LeaveEvent */
        XWidget_ignoreEvent_default,       /* KeyPressEvent */
        XWidget_ignoreEvent_default,       /* KeyReleaseEvent */
        XWidget_ignoreEvent_default,       /* InputMethodEvent */
        XWidget_dropEvent_default,         /* DragEnterEvent */
        XWidget_dropEvent_default,         /* DragMoveEvent */
        XWidget_dropEvent_default,         /* DragLeaveEvent */
        XWidget_dropEvent_default,         /* DropEvent */
        XWidget_ignoreEvent_default,       /* MousePressEvent */
        XWidget_ignoreEvent_default,       /* MouseReleaseEvent */
        XWidget_ignoreEvent_default,       /* MouseDoubleClickEvent */
        XWidget_ignoreEvent_default,       /* MouseMoveEvent */
        XWidget_ignoreEvent_default,       /* WheelEvent */
        XWidget_noopEvent_default,         /* ShowEvent */
        XWidget_noopEvent_default,         /* HideEvent */
        XWidget_noopEvent_default,         /* ChangeEvent */
        XWidget_ignoreEvent_default,       /* ContextMenuEvent */
        XWidget_ignoreEvent_default,       /* TouchEvent（默认忽略→沿父链传播；对标 QWidget 默认 touchEvent） */
        XWidget_ignoreEvent_default        /* TabletEvent（默认忽略→沿父链传播；对标 QWidget 默认 tabletEvent） */
#if XINPUTMETHOD_ON
        ,XWidget_inputMethodQuery_default /* InputMethodQuery（对标 QWidget::inputMethodQuery） */
#endif /* XINPUTMETHOD_ON */
    };
    XVTABLE_ADD_FUNC_LIST_DEFAULT(table);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Deinit, VXWidget_deinit);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Copy, VXWidget_copy);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Move, VXWidget_move);
    XVTABLE_OVERLOAD_DEFAULT(EXObject_Event, VXWidget_event);
    return XVTABLE_DEFAULT;
}

void XWidget_init(XWidget* self, XWidget* parent, XWidgetFlags flags)
{
    if (!self) return;
    /* 根因防护：父指针必须是已初始化的 XWidget（XObject::is_widget=1）。
       裸 XWindow 等非控件对象被调用方强转 XWidget* 当父传入时（如
       XLineEdit_create((XWidget*)XWindow_create(), 0)），其结构体远小于
       XWidget，挂父后 XLineEdit_init 尾部 xlineedit_updateSizeHints →
       XWidget_updateGeometry 沿父链读 parent->m_layout 会越界读到堆残留，
       非零垃圾被当 XLayout* 传入 XLayout_activate 解引用段错误。
       此处把非控件父一律按无父（顶层窗口）处理，杜绝垃圾强转；真实
       控件父 is_widget 恒为 1（XWidget_init 统一置位），正常路径零变化。 */
    if (parent && !((const XObject*)parent)->is_widget)
        parent = NULL;
    XMemset(self, 0, sizeof(XWidget));
    XObject_init(&self->m_class);
    ((XObject*)self)->is_widget = 1;
    XClassSetVtable(self, XWidget);
    self->m_windowFlags = flags;
    if (!parent && !(self->m_windowFlags & (XWindowFlags)XWindowType_Window))
        self->m_windowFlags |= (XWindowFlags)XWindowType_Window;
    /* 对标 Qt：Qt::Popup 即窗口（见 init 有父分支同规则注释）。 */
    self->m_isWindow = (!parent ||
                        (self->m_windowFlags & (XWindowFlags)XWindowType_Window) ||
                        (self->m_windowFlags & (XWindowFlags)XWindowType_Popup)) ? 1 : 0;
    self->m_focusPolicy = XWidgetFocusPolicy_NoFocus;
    /* Qt QWidget 的默认值是 DefaultContextMenu，不是 NoContextMenu。 */
    self->m_contextMenuPolicy = XWidgetContextMenuPolicy_DefaultContextMenu;
    self->m_layoutDirection = XWidgetLayoutDirection_LeftToRight;
    self->m_enabled = 1;
    self->m_updatesEnabled = 1;
    /* QWidgetPrivate::init 设置 WA_WState_Hidden：新控件在显式
     * show() 前保持隐藏，父控件首次显示时不会误显示该子控件。 */
    /* 对标 Qt：顶层控件初始 Hidden（需显式 show），子控件不 Hidden
     * （随父控件 show 自动显示）。 */
    XWidget_attrSet(&self->m_attributes, XWidgetAttribute_WState_Hidden,
                    parent == NULL);
    /* QWidgetPrivate::init 在 create() 前为控件预置几何：顶层窗口
     * 640x480，子控件 100x30。该尺寸不是布局结果，而是首个显式
     * setGeometry/布局激活前 QWidget::geometry() 的默认值。 */
    XRect_init(&self->m_windowRect, 0, 0, parent ? 100 : 640,
               parent ? 30 : 480);
    XRect_init(&self->m_contentsRect, 0, 0, self->m_windowRect.width,
               self->m_windowRect.height);
    XMargins_init(&self->m_contentsMargins, 0, 0, 0, 0);
    XFont_init(&self->m_font);
    self->m_backgroundRole = XPaletteColorRole_Window;
    self->m_foregroundRole = XPaletteColorRole_NoRole;
    XSize_init(&self->m_minimumSize, 0, 0);
    XSize_init(&self->m_maximumSize, XWIDGET_MAX_SIZE, XWIDGET_MAX_SIZE);
    XSize_init(&self->m_baseSize, -1, -1);
    XSize_init(&self->m_sizeIncrement, 0, 0);
    XSize_init(&self->m_sizeHint, -1, -1);
    XSize_init(&self->m_minimumSizeHint, -1, -1);
    self->m_windowOpacity = 1.0f;
    self->m_heightForWidthHandler = NULL;
    self->m_heightForWidthUserData = NULL;
    self->m_focusNext = NULL;
    self->m_focusPrev = NULL;
    self->m_focusProxy = NULL;
    self->m_windowIconText = NULL;
    self->m_statusTip = NULL;
    self->m_whatsThis = NULL;
    self->m_accessibleName = NULL;
    self->m_accessibleDescription = NULL;
    self->m_windowRole = NULL;
    self->m_styleSheet = NULL;
    self->m_inputMethodHints = 0;
    self->m_windowState = XWindowState_NoState;
    self->m_windowModality = XWindowModality_NonModal;
    self->m_sizePolicy = XWidgetSizePolicy_create();
#if XWINDOW_ON && XACCESSIBLE_ON
    self->m_accessible = XAccessible_createForWidget(self);
#endif
#if XPALETTE_ON
    XPalette_init_default(&self->m_palette);
#else
    self->m_palette.m_disabled = 0;
#endif
    XIcon_init(&self->m_icon);
    XRegion_init(&self->m_dirty);
    XRegion_init(&self->m_staticContents);
    self->m_contentCache = NULL;
    self->m_contentCacheDirty = true;
    /* 保留层默认全关（显式选择加入；零回归红线）。 */
    self->m_retainedCache = NULL;
    self->m_retainedNode = NULL;
    self->m_retainedStamp = 0;
    self->m_retainedEnabled = false;
    self->m_retainedValid = false;
    XRegion_init(&self->m_mask);
#if XPAINTDEVICE_ON
    XPaintDevice_init(&self->m_paintDevice, XPaintDeviceType_Widget, self,
                      xwidget_paintDeviceMetric, XPaintEngineType_Raster,
                      (uint32_t)XPaintEngineFeature_AllFeatures);
#endif
    if (parent) {
        XObject_setParent(&self->m_class, (XObject*)parent);
        /* 对标 Qt：Qt::Popup 即窗口（isWindow() 为真）——弹出层须以
         * 自身窗口承载后备存储，否则 flushBackingStore 顶层回溯落到
         * 宿主窗，弹层永久透明（14.111 combo 弹层实测）。 */
        self->m_isWindow =
            (self->m_windowFlags &
             ((XWindowFlags)XWindowType_Window |
              (XWindowFlags)XWindowType_Popup)) ? 1 : 0;
    }
#if XAPPLICATION_ON && XGUIAPPLICATION_ON
    if (self->m_isWindow)
        XApplication_registerTopLevelWidget(self);
#endif
#if XWINDOW_ON && XACCESSIBLE_ON
    XPlatformAccessibility_notifyWidget(XAccessibleEvent_ObjectCreated, self);
#endif
}

XWidget* XWidget_create_ex(XMemoryType memory, XWidget* parent, XWidgetFlags flags)
{
    XWidget* self = (XWidget*)XMemory_malloc(sizeof(XWidget), memory);
    if (!self) return NULL;
    XMemset(self, 0, sizeof(XWidget));
    XWidget_init(self, parent, flags);
    Set_Class_Memory(self, memory);
    Set_Class_IsHeap(self, true);
    return self;
}

/** @brief 释放控件内容离屏缓存并把脏标记复位为待重建。 */
static void XWidget_freeContentCache(XWidget* self)
{
    if (!self) return;
    if (self->m_contentCache) {
        XImage_delete_base(self->m_contentCache);
        self->m_contentCache = NULL;
    }
    self->m_contentCacheDirty = true;
}

/** @brief 把 self 从显式 Tab 链中摘除，避免析构后留下悬空引用。 */
static void XWidget_unlinkFocusChain(XWidget* self)
{
    if (!self) return;
    if (self->m_focusPrev && self->m_focusPrev->m_focusNext == self)
        self->m_focusPrev->m_focusNext = self->m_focusNext;
    if (self->m_focusNext && self->m_focusNext->m_focusPrev == self)
        self->m_focusNext->m_focusPrev = self->m_focusPrev;
    self->m_focusNext = NULL;
    self->m_focusPrev = NULL;
}

/** @brief 返回控件焦点链最深层的代理；无代理返回 NULL。 */
static XWidget* XWidget_deepestFocusProxy(const XWidget* self)
{
    XWidget* cur;
    uint32_t guard;
    if (!self) return NULL;
    cur = self->m_focusProxy;
    guard = 0;
    while (cur && cur->m_focusProxy && guard < 64u) {
        cur = cur->m_focusProxy;
        ++guard;
    }
    return cur;
}

/** @brief 判断 owner 是否已有焦点代理注册条目。 */
static bool XFocusProxy_hasEntry(const XWidget* owner)
{
    XFocusProxyEntry* e;
    for (e = g_focusProxyEntries; e; e = e->next)
        if (e->owner == owner) return true;
    return false;
}

/** @brief 为 owner 的 m_focusProxy（非 NULL）登记注册表条目；重复登记忽略。 */
static void XFocusProxy_register(XWidget* owner)
{
    XFocusProxyEntry* e;
    if (!owner || !owner->m_focusProxy || XFocusProxy_hasEntry(owner)) return;
    e = (XFocusProxyEntry*)XMemory_malloc(sizeof(XFocusProxyEntry),
                                          XCLASS_DEFAULT_MEMORY_TYPE);
    if (!e) return;
    XMemset(e, 0, sizeof(XFocusProxyEntry));
    e->owner = owner;
    e->proxy = owner->m_focusProxy;
    e->next = g_focusProxyEntries;
    if (g_focusProxyEntries) g_focusProxyEntries->prev = e;
    g_focusProxyEntries = e;
}

/** @brief 移除 owner 的注册条目（不改写 m_focusProxy）。 */
static void XFocusProxy_unregisterOwner(XWidget* owner)
{
    XFocusProxyEntry* e;
    if (!owner) return;
    e = g_focusProxyEntries;
    while (e) {
        XFocusProxyEntry* next;
        XFocusProxyEntry* prev;
        if (e->owner == owner) {
            prev = e->prev;
            next = e->next;
            if (prev) prev->next = next; else g_focusProxyEntries = next;
            if (next) next->prev = prev;
            XMemory_free(e, XCLASS_DEFAULT_MEMORY_TYPE);
            return;
        }
        e = e->next;
    }
}

/** @brief 控件销毁清理：移除 owner==self 的条目，并把仍指向 self 的代理字段清空后摘除。 */
static void XFocusProxy_cleanupFor(XWidget* self)
{
    XFocusProxyEntry* e;
    if (!self) return;
    e = g_focusProxyEntries;
    while (e) {
        XFocusProxyEntry* next = e->next;
        if (e->owner == self || e->proxy == self) {
            if (e->proxy == self && e->owner != self)
                e->owner->m_focusProxy = NULL;
            if (e->prev) e->prev->next = e->next; else g_focusProxyEntries = e->next;
            if (e->next) e->next->prev = e->prev;
            XMemory_free(e, XCLASS_DEFAULT_MEMORY_TYPE);
            e = next;
            continue;
        }
        e = next;
    }
 }

/** @brief 释放控件自身资源（不释放子控件；XObject 基类 Deinit 负责子树与父登记）。 */
static void VXWidget_deinit(XWidget* self)
{
    if (!self) return;
    XWidget_unlinkFocusChain(self);
    XFocusProxy_cleanupFor(self);
#if XLAYOUT_ON
    if (self->m_layout) {
        XLayout_detachWidget(self->m_layout);
        self->m_layout = NULL;
    }
#endif /* XLAYOUT_ON */
    XWidget_destroyWindow(self);
#if XAPPLICATION_ON && XGUIAPPLICATION_ON
    if (self->m_isWindow)
        XApplication_unregisterTopLevelWidget(self);
#endif
#if XWINDOW_ON && XACCESSIBLE_ON
    XPlatformAccessibility_notifyWidget(XAccessibleEvent_ObjectDestroyed, self);
    if (self->m_accessible) {
        XAccessible_delete_base(self->m_accessible);
        self->m_accessible = NULL;
    }
#endif
    if (XObject_parent((XObject*)self))
        XObject_setParent((XObject*)self, NULL);
    if (g_focusWidget == self)
        XWidget_clearFocusBase(self, XFocusReason_Other);
    if (g_mouseGrabWidget == self)
        g_mouseGrabWidget = NULL;
    /* 销毁路径全表遍历摘除该控件全部触点抓取表项（per-id 表；多触点
       抓取同一控件时旧单指针比较会漏摘其余表项）。 */
    xwidget_touchGrabRemoveWidget(self);
    if (g_keyboardGrabWidget == self)
        g_keyboardGrabWidget = NULL;
#if XWINDOWSYSTEMINTERFACE_ON && XWINDOW_ON && XWINDOWEVENT_ON
    /* 悬停合成器登记靶析构自清位（第八轮 R1 悬停收口；与上方焦点/抓取
       清位同纪律）：本控件若是合成器登记靶，失放前同步置空，保证
       WSI 侧「登记非空即活对象」不变量（XWindowSystemInterface.c
       xwsi_hoverTargetAlive 冷会话回退据此信任登记）。 */
    XWindowSystemInterface_clearHoverTarget(self);
#endif
    XWidget_freeString(&self->m_toolTip);
    XWidget_freeString(&self->m_windowTitle);
    XWidget_freeString(&self->m_windowIconText);
    XWidget_freeString(&self->m_windowFilePath);
    XWidget_freeString(&self->m_statusTip);
    XWidget_freeString(&self->m_whatsThis);
    XWidget_freeString(&self->m_accessibleName);
    XWidget_freeString(&self->m_accessibleDescription);
    XWidget_freeString(&self->m_windowRole);
    XWidget_freeString(&self->m_styleSheet);
#if XCURSOR_ON
    if (self->m_cursor) {
        XCursor_delete_base((XClass*)self->m_cursor);
        self->m_cursor = NULL;
    }
#endif /* XCURSOR_ON */
    XFont_deinit_base(&self->m_font);
    XIcon_deinit_base(&self->m_icon);
    XRegion_deinit(&self->m_dirty);
    XRegion_deinit(&self->m_staticContents);
    XWidget_freeContentCache(self);
    /* 保留层登记节点持有本控件借用指针，析构必须先摘除再释放缓存。 */
    xwidget_retainedDisable(self);
    XRegion_deinit(&self->m_mask);
#if XBACKINGSTORE_ON && XPLATFORMBACKINGSTORE_ON && XPLATFORMINTEGRATION_ON
    if (self->m_backingStore) {
        XBackingStore_delete_base(self->m_backingStore);
        self->m_backingStore = NULL;
    }
#endif /* XBACKINGSTORE_ON && XPLATFORMBACKINGSTORE_ON && XPLATFORMINTEGRATION_ON */
    if (self->m_graphicsEffect) {
        XGraphicsEffect_delete_base(self->m_graphicsEffect);
        self->m_graphicsEffect = NULL;
    }
    XClass_Deinit_Parent(XObject, (XObject*)self);
}

/** @brief 深拷贝控件布局与外观字段；不复制 XObject 基类/父链/窗口句柄/后备存储。 */
static void VXWidget_copy(XWidget* self, const XWidget* other)
{
    if (!self || !other || self == other) return;
    if (XClassIsVtableNull(self)) XWidget_init(self, NULL, 0);
    /* 释放目标已有字符串、字体和缓存；区域对象由 XRegion_copy 复用容量。 */
    XFocusProxy_unregisterOwner(self);
    self->m_focusProxy = NULL;
    /* 拷贝不继承目标对象的平台资源；目标若已创建过资源，必须先销毁，
       否则下面置 NULL 会泄漏原生窗口、后备存储和无障碍对象。 */
    XWidget_destroyWindow(self);
#if XBACKINGSTORE_ON && XPLATFORMBACKINGSTORE_ON && XPLATFORMINTEGRATION_ON
    if (self->m_backingStore) {
        XBackingStore_delete_base(self->m_backingStore);
        self->m_backingStore = NULL;
    }
#endif /* XBACKINGSTORE_ON && XPLATFORMBACKINGSTORE_ON && XPLATFORMINTEGRATION_ON */
#if XWINDOW_ON && XACCESSIBLE_ON
    if (self->m_accessible) {
        XAccessible_delete_base(self->m_accessible);
        self->m_accessible = NULL;
    }
#endif /* XWINDOW_ON && XACCESSIBLE_ON */
#if XLAYOUT_ON
    if (self->m_layout) {
        XLayout_detachWidget(self->m_layout);
        self->m_layout = NULL;
    }
#endif /* XLAYOUT_ON */
    XWidget_freeString(&self->m_toolTip);
    XWidget_freeString(&self->m_windowTitle);
    XWidget_freeString(&self->m_windowIconText);
    XWidget_freeString(&self->m_windowFilePath);
    XWidget_freeString(&self->m_statusTip);
    XWidget_freeString(&self->m_whatsThis);
    XWidget_freeString(&self->m_accessibleName);
    XWidget_freeString(&self->m_accessibleDescription);
    XWidget_freeString(&self->m_windowRole);
    XWidget_freeString(&self->m_styleSheet);
#if XCURSOR_ON
    if (self->m_cursor) {
        XCursor_delete_base((XClass*)self->m_cursor);
        self->m_cursor = NULL;
    }
#endif /* XCURSOR_ON */
    self->m_cursor = NULL;
    /* 图形效果为控件独占资源：拷贝前释放自身效果，拷贝不继承效果
       （对标 Qt：QWidget 拷贝不复制 graphicsEffect）。 */
    if (self->m_graphicsEffect) {
        XGraphicsEffect_delete_base(self->m_graphicsEffect);
        self->m_graphicsEffect = NULL;
    }
    XWidget_freeContentCache(self);
    /* 保留层为运行期显式选择加入状态：拷贝前释放自身登记，且不继承
       目标保留状态（与图形效果同一“不随拷贝复制”家族）。 */
    xwidget_retainedDisable(self);
    /* 复制字段（m_class 基类、m_windowHandle、m_backingStore 不复制）。 */
    self->m_windowFlags = other->m_windowFlags;
    self->m_attributes = other->m_attributes;
    self->m_focusPolicy = other->m_focusPolicy;
    self->m_contextMenuPolicy = other->m_contextMenuPolicy;
    self->m_layoutDirection = other->m_layoutDirection;
    self->m_isWindow = other->m_isWindow;
    self->m_isClosing = other->m_isClosing;
    self->m_inShow = other->m_inShow;
    self->m_inPaintEvent = other->m_inPaintEvent;
    self->m_visible = other->m_visible;
    self->m_explicitShow = other->m_explicitShow;
    self->m_enabled = other->m_enabled;
    self->m_updatesEnabled = other->m_updatesEnabled;
    self->m_autoFillBackground = other->m_autoFillBackground;
    self->m_paletteSet = other->m_paletteSet;
    self->m_mouseTracking = other->m_mouseTracking;
    self->m_tabletTracking = other->m_tabletTracking;
    self->m_acceptDrops = other->m_acceptDrops;
    self->m_windowRect = other->m_windowRect;
    self->m_contentsRect = other->m_contentsRect;
    self->m_contentsMargins = other->m_contentsMargins;
    self->m_backgroundRole = other->m_backgroundRole;
    self->m_foregroundRole = other->m_foregroundRole;
    self->m_minimumSize = other->m_minimumSize;
    self->m_maximumSize = other->m_maximumSize;
    self->m_baseSize = other->m_baseSize;
    self->m_sizeIncrement = other->m_sizeIncrement;
    self->m_sizeHint = other->m_sizeHint;
    self->m_minimumSizeHint = other->m_minimumSizeHint;
    self->m_heightForWidthHandler = other->m_heightForWidthHandler;
    self->m_heightForWidthUserData = other->m_heightForWidthUserData;
    self->m_focusNext = NULL;
    self->m_focusPrev = NULL;
    self->m_sizePolicy = other->m_sizePolicy;
    self->m_normalGeometry = other->m_normalGeometry;
    self->m_windowState = other->m_windowState;
    self->m_windowModality = other->m_windowModality;
    self->m_toolTipDuration = other->m_toolTipDuration;
    self->m_windowOpacity = other->m_windowOpacity;
    self->m_toolTip = XWidget_copyString(other->m_toolTip);
    self->m_windowTitle = XWidget_copyString(other->m_windowTitle);
    self->m_windowIconText = XWidget_copyString(other->m_windowIconText);
    self->m_windowFilePath = XWidget_copyString(other->m_windowFilePath);
    self->m_statusTip = XWidget_copyString(other->m_statusTip);
    self->m_whatsThis = XWidget_copyString(other->m_whatsThis);
    self->m_accessibleName = XWidget_copyString(other->m_accessibleName);
    self->m_accessibleDescription = XWidget_copyString(other->m_accessibleDescription);
    self->m_windowRole = XWidget_copyString(other->m_windowRole);
    self->m_styleSheet = XWidget_copyString(other->m_styleSheet);
    self->m_inputMethodHints = other->m_inputMethodHints;
#if XPALETTE_ON
    XPalette_copy(&self->m_palette, &other->m_palette);
#else
    self->m_palette = other->m_palette;
#endif
    XCopy(&self->m_font, &other->m_font);
    XCopy(&self->m_icon, &other->m_icon);
#if XCURSOR_ON
    if (other->m_cursor) {
        self->m_cursor = XCursor_create_ex(XCLASS_DEFAULT_MEMORY_TYPE);
        if (self->m_cursor)
            XCopy(self->m_cursor, other->m_cursor);
    }
#endif /* XCURSOR_ON */
    XRegion_copy(&other->m_dirty, &self->m_dirty);
    XRegion_copy(&other->m_staticContents, &self->m_staticContents);
    XRegion_copy(&other->m_mask, &self->m_mask);
    /* 窗口句柄与后备存储一律置空（拷贝构造不清平台资源）。 */
    self->m_windowHandle = NULL;
    self->m_backingStore = NULL;
    self->m_contentCache = NULL;
    self->m_contentCacheDirty = true;
#if XWINDOW_ON && XACCESSIBLE_ON
    if (!self->m_accessible)
        self->m_accessible = XAccessible_createForWidget(self);
#endif
#if XLAYOUT_ON
    /* 布局为借用指针，不随控件拷贝（对标 Qt：布局对象独立拥有）。 */
    self->m_layout = NULL;
#endif /* XLAYOUT_ON */
}

/** @brief 移动语义：先释放目标资源，再转移源拥有指针（源归零）。 */
static void VXWidget_move(XWidget* self, XWidget* other)
{
    if (!self || !other || self == other) return;
    if (XClassIsVtableNull(self)) XWidget_init(self, NULL, 0);
    VXWidget_deinit(self);
    /* 拥有指针转移 */
    self->m_toolTip = other->m_toolTip;         other->m_toolTip = NULL;
    self->m_windowTitle = other->m_windowTitle; other->m_windowTitle = NULL;
    self->m_windowIconText = other->m_windowIconText; other->m_windowIconText = NULL;
    self->m_windowFilePath = other->m_windowFilePath; other->m_windowFilePath = NULL;
    self->m_statusTip = other->m_statusTip;       other->m_statusTip = NULL;
    self->m_whatsThis = other->m_whatsThis;       other->m_whatsThis = NULL;
    self->m_accessibleName = other->m_accessibleName; other->m_accessibleName = NULL;
    self->m_accessibleDescription = other->m_accessibleDescription; other->m_accessibleDescription = NULL;
    self->m_windowRole = other->m_windowRole;     other->m_windowRole = NULL;
    self->m_styleSheet = other->m_styleSheet;     other->m_styleSheet = NULL;
    self->m_inputMethodHints = other->m_inputMethodHints; other->m_inputMethodHints = 0;
    XFocusProxy_unregisterOwner(other);
    self->m_focusProxy = other->m_focusProxy; other->m_focusProxy = NULL;
    XFocusProxy_register(self);
#if XCURSOR_ON
    self->m_cursor = other->m_cursor;           other->m_cursor = NULL;
#endif /* XCURSOR_ON */
    self->m_graphicsEffect = other->m_graphicsEffect;
    other->m_graphicsEffect = NULL;
    self->m_windowHandle = other->m_windowHandle; other->m_windowHandle = NULL;
    self->m_backingStore = other->m_backingStore; other->m_backingStore = NULL;
    self->m_contentCache = other->m_contentCache;
    other->m_contentCache = NULL;
    self->m_contentCacheDirty = other->m_contentCacheDirty;
    other->m_contentCacheDirty = true;
    /* 保留层状态随移动转移：缓存/开关/时间戳平移，登记节点保留在
       LRU 链上但反指改绑目标控件（源控件归零，防双重登记）。 */
    self->m_retainedCache = other->m_retainedCache;
    other->m_retainedCache = NULL;
    self->m_retainedValid = other->m_retainedValid;
    other->m_retainedValid = false;
    self->m_retainedEnabled = other->m_retainedEnabled;
    other->m_retainedEnabled = false;
    self->m_retainedStamp = other->m_retainedStamp;
    other->m_retainedStamp = 0;
    self->m_retainedNode = other->m_retainedNode;
    if (self->m_retainedNode)
        self->m_retainedNode->m_widget = self;
    other->m_retainedNode = NULL;
#if XWINDOW_ON && XACCESSIBLE_ON
    self->m_accessible = XAccessible_createForWidget(self);
    if (other->m_accessible) {
        XAccessible_delete_base(other->m_accessible);
        other->m_accessible = NULL;
    }
#endif
    XMove(&self->m_icon, &other->m_icon);
    XMove(&self->m_font, &other->m_font);
#if XLAYOUT_ON
    /* 布局为借用指针：转移挂接并把布局反向引用改指目标控件。 */
    self->m_layout = other->m_layout;
    if (self->m_layout) {
        self->m_layout->m_parentWidget = self;
        other->m_layout = NULL;
    }
#endif /* XLAYOUT_ON */
    self->m_dirty = other->m_dirty;
    XRegion_init(&other->m_dirty);
    self->m_staticContents = other->m_staticContents;
    XRegion_init(&other->m_staticContents);
    self->m_mask = other->m_mask;
    XRegion_init(&other->m_mask);
    /* 普通值字段整体转移 */
    self->m_windowFlags = other->m_windowFlags;
    self->m_attributes = other->m_attributes;
    self->m_focusPolicy = other->m_focusPolicy;
    self->m_focusNext = NULL;
    self->m_focusPrev = NULL;
    self->m_contextMenuPolicy = other->m_contextMenuPolicy;
    self->m_layoutDirection = other->m_layoutDirection;
    self->m_isWindow = other->m_isWindow;
    self->m_isClosing = other->m_isClosing;
    self->m_inShow = other->m_inShow;
    self->m_inPaintEvent = other->m_inPaintEvent;
    self->m_visible = other->m_visible;
    self->m_explicitShow = other->m_explicitShow;
    self->m_enabled = other->m_enabled;
    self->m_updatesEnabled = other->m_updatesEnabled;
    self->m_autoFillBackground = other->m_autoFillBackground;
    self->m_paletteSet = other->m_paletteSet;
    self->m_mouseTracking = other->m_mouseTracking;
    self->m_tabletTracking = other->m_tabletTracking;
    self->m_acceptDrops = other->m_acceptDrops;
    self->m_windowRect = other->m_windowRect;
    self->m_contentsRect = other->m_contentsRect;
    self->m_contentsMargins = other->m_contentsMargins;
    self->m_backgroundRole = other->m_backgroundRole;
    self->m_foregroundRole = other->m_foregroundRole;
    self->m_minimumSize = other->m_minimumSize;
    self->m_maximumSize = other->m_maximumSize;
    self->m_baseSize = other->m_baseSize;
    self->m_sizeIncrement = other->m_sizeIncrement;
    self->m_sizeHint = other->m_sizeHint;
    self->m_minimumSizeHint = other->m_minimumSizeHint;
    self->m_heightForWidthHandler = other->m_heightForWidthHandler;
    self->m_heightForWidthUserData = other->m_heightForWidthUserData;
    self->m_sizePolicy = other->m_sizePolicy;
    self->m_normalGeometry = other->m_normalGeometry;
    self->m_windowState = other->m_windowState;
    self->m_windowModality = other->m_windowModality;
    self->m_toolTipDuration = other->m_toolTipDuration;
    self->m_windowOpacity = other->m_windowOpacity;
#if XPALETTE_ON
    XPalette_copy(&self->m_palette, &other->m_palette);
#else
    self->m_palette = other->m_palette;
#endif
    /* 基类/父链沿用目标；源对象归零字段 */
    XMemset((char*)other + sizeof(XObject), 0, sizeof(XWidget) - sizeof(XObject));
    /* 上面的整体清零不能破坏嵌入式 XClass 对象的析构前提。移动后的
       源控件不再拥有资源，但仍必须能被 XWidget_delete_base 安全销毁。 */
    XFont_init(&other->m_font);
    XIcon_init(&other->m_icon);
    other->m_windowFlags = 0;
}

/** @brief 控件事件总入口（EXObject_Event 重载槽，按事件类型分派）。 */
static bool VXWidget_event(XWidget* self, XEvent* event)
{
    XEventType type;
    if (!self || !event) return false;
    type = XEvent_type(event);
    /* QWidget::event 丢弃禁用控件的数位板、触摸、鼠标、键盘、滚轮及
     * 上下文菜单输入；返回 false 让上层派发器按现有传播规则继续处理。 */
    if (!XWidget_isEnabled(self)) {
        switch (type) {
        case XEVENT_TYPE_TABLET_PRESS:
        case XEVENT_TYPE_TABLET_RELEASE:
        case XEVENT_TYPE_TABLET_MOVE:
        case XEVENT_TYPE_MOUSE_BUTTON_PRESS:
        case XEVENT_TYPE_MOUSE_BUTTON_RELEASE:
        case XEVENT_TYPE_MOUSE_BUTTON_DBL_CLICK:
        case XEVENT_TYPE_MOUSE_MOVE:
        case XEVENT_TYPE_TOUCH_BEGIN:
        case XEVENT_TYPE_TOUCH_UPDATE:
        case XEVENT_TYPE_TOUCH_END:
        case XEVENT_TYPE_TOUCH_CANCEL:
        case XEVENT_TYPE_WHEEL:
        case XEVENT_TYPE_KEY_PRESS:
        case XEVENT_TYPE_KEY_RELEASE:
        case XEVENT_TYPE_CONTEXT_MENU:
            return false;
        default:
            break;
        }
    }
    switch (type) {
    case XEVENT_TYPE_PAINT:
        XWidget_paintEvent_base(self, event);
        return true;
    case XEVENT_TYPE_RESIZE:
        XWidget_resizeEvent_base(self, event);
        return true;
    case XEVENT_TYPE_MOVE:
        XWidget_moveEvent_base(self, event);
        return true;
    case XEVENT_TYPE_CLOSE:
        XWidget_closeEvent_base(self, event);
        return true;
    case XEVENT_TYPE_FOCUS_IN:
        XWidget_focusInEvent_base(self, event);
        return true;
    case XEVENT_TYPE_FOCUS_OUT:
        XWidget_focusOutEvent_base(self, event);
        return true;
    case XEVENT_TYPE_ENTER:
        XWidget_attrSet(&self->m_attributes, XWidgetAttribute_UnderMouse,
                        true);
        XWidget_enterEvent_base(self, event);
        /* 悬停状态翻转触发重绘（对标 Qt hover 样式刷新）。 */
        XWidget_update(self);
        return true;
    case XEVENT_TYPE_LEAVE:
        XWidget_attrSet(&self->m_attributes, XWidgetAttribute_UnderMouse,
                        false);
        XWidget_leaveEvent_base(self, event);
        XWidget_update(self);
        return true;
    case XEVENT_TYPE_KEY_PRESS:
        XWidget_keyPressEvent_base(self, event);
        /* 对标 QWidget::event 的 Tab 焦点遍历：控件未处理的 Tab/
         * Shift+Tab（Backtab）交窗口级 focusNextPrevChild 移动焦点并
         * 消费。不以接收控件自身 focusPolicy 为门槛——Qt 中 Tab 遍历
         * 由所在顶层窗口决定去向（qwidget.cpp:6816 focusNextPrevChild
         * 沿父链上交），候选资格只看候选控件自身的 TabFocus 位；初始
         * 无任何焦点控件时由窗口级回退起点启动遍历（问题 #3）。
         * 派发顺序保持「keyPressEvent 优先、未接受才遍历」：编辑器
         * （XItemDelegate）与富文本链接导航在 keyPressEvent 消费 Tab
         * 提交/锚点移动，等价 Qt 的 focusNextPrevChild 重载/事件过滤器
         * 拦截位，先遍历会抢走其按键。 */
        if (!XEvent_isAccepted(event)) {
            XKeyEvent* ke = (XKeyEvent*)event;
            int key = (int)ke->m_key;
            int mods = (int)ke->m_modifiers;
            if ((key == (int)XKey_Tab || key == (int)XKey_Backtab) &&
                (mods & ~(int)XKeyboardModifier_ShiftModifier) == 0) {
                bool next = (key == (int)XKey_Tab) ==
                            ((mods & (int)XKeyboardModifier_ShiftModifier) == 0);
                if (xwidget_focusNextPrevChild(self, next)) {
                    XEvent_accept(event);
                    return true;
                }
            }
        }
        /* 对标 QWidget::event：键盘虚槽返回后保留事件接受状态，由上层
           派发（XWidget_dispatchKeyEvent）按 accept/ignore 决定是否沿
           父链上抛。此前无条件 return true 抹平接受状态，ignore 的
           按键被当作已消费，父链传播无从发起（复扫 P1-1）。 */
        return XEvent_isAccepted(event);
    case XEVENT_TYPE_KEY_RELEASE:
        XWidget_keyReleaseEvent_base(self, event);
        /* 同 KEY_PRESS：保留接受状态供父链传播判定（复扫 P1-1）。 */
        return XEvent_isAccepted(event);
    case XEVENT_TYPE_INPUT_METHOD:
        XWidget_inputMethodEvent_base(self, event);
        return true;
    case XEVENT_TYPE_CONTEXT_MENU:
        XWidget_contextMenuEvent_base(self, event);
        return true;
    case XEVENT_TYPE_DRAG_ENTER:
        XWidget_dragEnterEvent_base(self, event);
        return true;
    case XEVENT_TYPE_DRAG_MOVE:
        XWidget_dragMoveEvent_base(self, event);
        return true;
    case XEVENT_TYPE_DRAG_LEAVE:
        XWidget_dragLeaveEvent_base(self, event);
        return true;
    case XEVENT_TYPE_DROP:
        XWidget_dropEvent_base(self, event);
        return true;
    case XEVENT_TYPE_MOUSE_BUTTON_PRESS:
        XWidget_mousePressEvent_base(self, event);
        return true;
    case XEVENT_TYPE_MOUSE_BUTTON_RELEASE:
        XWidget_mouseReleaseEvent_base(self, event);
        return true;
    case XEVENT_TYPE_MOUSE_BUTTON_DBL_CLICK:
        XWidget_mouseDoubleClickEvent_base(self, event);
        return true;
    case XEVENT_TYPE_MOUSE_MOVE:
        XWidget_mouseMoveEvent_base(self, event);
        return true;
    case XEVENT_TYPE_WHEEL:
        XWidget_wheelEvent_base(self, event);
        return true;
    case XEVENT_TYPE_TOUCH_BEGIN:
    case XEVENT_TYPE_TOUCH_UPDATE:
    case XEVENT_TYPE_TOUCH_END:
    case XEVENT_TYPE_TOUCH_CANCEL:
        /* 对标 QWidget::event 的 TouchBegin/Update/End → touchEvent 虚函数
           分派；accept 状态由槽设置，默认忽略以沿父链传播。 */
        XWidget_touchEvent_base(self, event);
        return true;
    case XEVENT_TYPE_TABLET_PRESS:
    case XEVENT_TYPE_TABLET_RELEASE:
    case XEVENT_TYPE_TABLET_MOVE:
        /* 对标 QWidget::event 的 TabletPress/Release/Move → tabletEvent
           虚函数分派。 */
        XWidget_tabletEvent_base(self, event);
        return true;
    case XEVENT_TYPE_SHOW:
        XWidget_showEvent_base(self, event);
        return true;
    case XEVENT_TYPE_HIDE:
        XWidget_hideEvent_base(self, event);
        return true;
    case XEVENT_TYPE_LOCALE_CHANGE:
    case XEVENT_TYPE_LANGUAGE_CHANGE:
    case XEVENT_TYPE_LAYOUT_DIRECTION_CHANGE:
    case XEVENT_TYPE_STYLE_CHANGE:
    case XEVENT_TYPE_FONT_CHANGE:
    case XEVENT_TYPE_ENABLED_CHANGE:
    case XEVENT_TYPE_WINDOW_STATE_CHANGE:
    case XEVENT_TYPE_CONTENTS_RECT_CHANGE:
        XWidget_changeEvent_base(self, event);
        return true;
    case XEVENT_TYPE_SHORTCUT_OVERRIDE:
        /* 对标 QLineEdit::event 的 ShortcutOverride 分支
         * （qlineedit.cpp:1451）：询问事件由认识它的文本编辑控件定夺——
         * 行编辑族转交控制器 processShortcutOverrideEvent（XLineControl.c，
         * Qt 条件全集：复制/撤销族、可打印及编辑键），多行编辑族转交
         * XTextControl_processEvent（其 ShortcutOverride 分支）；其余
         * 控件不识别该事件 → 保持忽略，快捷键照常激活。 */
#if XLINEEDIT_ON && XLINECONTROL_ON
        {
            XLineControl* overrideControl =
                xwidget_shortcutOverrideLineControl(self);
            if (overrideControl) {
                XLineControl_processShortcutOverrideEvent(
                    overrideControl, (XKeyEvent*)event);
                return XEvent_isAccepted(event);
            }
        }
#endif /* XLINEEDIT_ON && XLINECONTROL_ON */
#if XTEXTCONTROL_ON && XPLAINTEXTEDIT_ON
        xwidget_shortcutOverrideDelegate(self, (XKeyEvent*)event);
#endif /* XTEXTCONTROL_ON && XPLAINTEXTEDIT_ON */
        return XEvent_isAccepted(event);
    default:
        /* 未识别事件回退 XObject 默认 Event 实现（对标 QWidget::event 尾部）。 */
        return XClass_Parent(XObject, EXObject_Event,
                             bool(*)(XObject*, XEvent*))((XObject*)self, event);
    }
}

/* ==================== 属性与窗口标志 ==================== */

void XWidget_setAttribute(XWidget* self, XWidgetAttribute attribute, bool on)
{
    if (!self) return;
    XWidget_attrSet(&self->m_attributes, attribute, on);
    /* 这些属性在 QWidget 中同时驱动对应的便捷查询接口；保持字段与
     * 位集一致，避免通过 setAttribute() 设置后读到旧状态。 */
    switch (attribute) {
    case XWidgetAttribute_MouseTracking:
        self->m_mouseTracking = on ? 1 : 0;
        break;
    case XWidgetAttribute_TabletTracking:
        self->m_tabletTracking = on ? 1 : 0;
        break;
    case XWidgetAttribute_AcceptDrops:
        self->m_acceptDrops = on ? 1 : 0;
        break;
    case XWidgetAttribute_UpdatesDisabled:
        self->m_updatesEnabled = on ? 0 : 1;
        break;
    default:
        break;
    }
}

bool XWidget_testAttribute(const XWidget* self, XWidgetAttribute attribute)
{
    return self ? XWidget_attrTest(&self->m_attributes, attribute) : false;
}

bool XWidget_isWindow(const XWidget* self)
{
    return self ? (self->m_isWindow != 0) : false;
}

bool XWidget_isTopLevel(const XWidget* self)
{
    /* isTopLevel() 是 Qt 中 isWindow() 的历史兼容拼写。 */
    return XWidget_isWindow(self);
}

XWindowType XWidget_windowType(const XWidget* self)
{
    if (!self) return XWindowType_Widget;
    return (XWindowType)(self->m_windowFlags & 0xffu);
}

XWidgetFlags XWidget_windowFlags(const XWidget* self)
{
    return self ? self->m_windowFlags : 0;
}

void XWidget_setWindowFlags(XWidget* self, XWidgetFlags flags)
{
    bool wasWindow;
    if (!self) return;
    wasWindow = self->m_isWindow != 0;
    self->m_windowFlags = flags;
    /* 对标 Qt：Qt::Popup 即窗口（isWindow() 为真）——有父弹层同样以
     * 自身窗口承载后备存储，见 XWidget_init 内同规则注释。 */
    self->m_isWindow = (!(XObject_parent((XObject*)self)) ||
                        (flags & (XWidgetFlags)XWindowType_Window) ||
                        (flags & (XWidgetFlags)XWindowType_Popup)) ? 1 : 0;
    if (wasWindow && !self->m_isWindow) XWidget_destroyWindow(self);
#if XAPPLICATION_ON && XGUIAPPLICATION_ON
    if (!wasWindow && self->m_isWindow)
        XApplication_registerTopLevelWidget(self);
    else if (wasWindow && !self->m_isWindow)
        XApplication_unregisterTopLevelWidget(self);
#endif
#if XWINDOW_ON && XACCESSIBLE_ON
    XPlatformAccessibility_notifyWidget(XAccessibleEvent_StateChanged, self);
#endif
}

void XWidget_overrideWindowFlags(XWidget* self, XWidgetFlags flags)
{
    if (!self) return;
    self->m_windowFlags = flags;
}

void XWidget_setWindowFlag(XWidget* self, XWidgetFlags flag, bool on)
{
    XWidgetFlags flags;
    if (!self) return;
    flags = self->m_windowFlags;
    if (on)
        flags |= flag;
    else
        flags &= ~flag;
    XWidget_setWindowFlags(self, flags);
}

/* ==================== 几何体系 ==================== */

int XWidget_x(const XWidget* self)
{ return self ? self->m_windowRect.x : 0; }

int XWidget_y(const XWidget* self)
{ return self ? self->m_windowRect.y : 0; }

XPoint XWidget_pos(const XWidget* self)
{
    XPoint out;
    if (!self) { XPoint_init(&out, 0, 0); return out; }
    XPoint_init(&out, self->m_windowRect.x, self->m_windowRect.y);
    return out;
}

int XWidget_width(const XWidget* self)
{ return self ? self->m_windowRect.width : 0; }

int XWidget_height(const XWidget* self)
{ return self ? self->m_windowRect.height : 0; }

XSize XWidget_size(const XWidget* self)
{
    XSize out;
    if (!self) { XSize_init(&out, 0, 0); return out; }
    XSize_init(&out, self->m_windowRect.width, self->m_windowRect.height);
    return out;
}

XRect XWidget_rect(const XWidget* self)
{
    XRect out;
    if (!self) { XRect_init(&out, 0, 0, 0, 0); return out; }
    out = self->m_contentsRect;
    return out;
}

XRect XWidget_contentsRect(const XWidget* self)
{
    XRect out;
    if (!self) { XRect_init(&out, 0, 0, 0, 0); return out; }
    out = self->m_contentsRect;
    return out;
}

XMargins XWidget_contentsMargins(const XWidget* self)
{
    XMargins out;
    if (!self) { XMargins_init(&out, 0, 0, 0, 0); return out; }
    out = self->m_contentsMargins;
    return out;
}

void XWidget_setContentsMargins(XWidget* self, int left, int top,
                                int right, int bottom)
{
    if (!self) return;
    if (self->m_contentsMargins.left == left &&
        self->m_contentsMargins.top == top &&
        self->m_contentsMargins.right == right &&
        self->m_contentsMargins.bottom == bottom)
        return;
    XMargins_init(&self->m_contentsMargins, left, top, right, bottom);
    XWidget_updateContentsRect(self);
    XWidget_updateGeometry(self);
    XWidget_update(self);
}

void XWidget_unsetContentsMargins(XWidget* self)
{
    if (!self) return;
    if (self->m_contentsMargins.left == 0 && self->m_contentsMargins.top == 0 &&
        self->m_contentsMargins.right == 0 && self->m_contentsMargins.bottom == 0)
        return;
    XMargins_init(&self->m_contentsMargins, 0, 0, 0, 0);
    XWidget_updateContentsRect(self);
    XWidget_updateGeometry(self);
    XWidget_update(self);
}

XRect XWidget_geometry(const XWidget* self)
{
    XRect out;
    if (!self) { XRect_init(&out, 0, 0, 0, 0); return out; }
    out = self->m_windowRect;
    return out;
}

XRect XWidget_frameGeometry(const XWidget* self)
{
    XRect out;
    /* 无系统装饰的嵌入式语义：frameGeometry == geometry。 */
    if (!self) { XRect_init(&out, 0, 0, 0, 0); return out; }
    out = self->m_windowRect;
    return out;
}

XSize XWidget_frameSize(const XWidget* self)
{
    XSize out;
    XRect g = XWidget_frameGeometry(self);
    XSize_init(&out, g.width, g.height);
    return out;
}

XPoint XWidget_framePos(const XWidget* self)
{
    XPoint out;
    XRect g = XWidget_frameGeometry(self);
    XPoint_init(&out, g.x, g.y);
    return out;
}

XRect XWidget_normalGeometry(const XWidget* self)
{
    XRect out;
    if (!self) { XRect_init(&out, 0, 0, 0, 0); return out; }
    out = self->m_normalGeometry;
    if (out.width <= 0 && out.height <= 0) {
        /* 从未进入特殊状态：与当前几何一致。 */
        out = self->m_windowRect;
    }
    return out;
}

/** @brief 根据窗口几何与内容边距刷新内容矩形（对标 Qt QWidgetPrivate::setContentsRect 后的 crect 语义）。 */
static void XWidget_updateContentsRect(XWidget* self)
{
    int width, height;
    if (!self) return;
    width = self->m_windowRect.width - self->m_contentsMargins.left -
            self->m_contentsMargins.right;
    height = self->m_windowRect.height - self->m_contentsMargins.top -
             self->m_contentsMargins.bottom;
    if (width < 0) width = 0;
    if (height < 0) height = 0;
    self->m_contentsRect.x = self->m_contentsMargins.left;
    self->m_contentsRect.y = self->m_contentsMargins.top;
    self->m_contentsRect.width = width;
    self->m_contentsRect.height = height;
}

/** @brief 几何字段刷新：位置/尺寸变化时派发 MOVE/RESIZE 事件并同步客户区。 */
static void XWidget_recomputeGeometry(XWidget* self, const XRect* oldRect)
{
    XRect old;
    bool posChanged;
    bool sizeChanged;
    if (!self) return;
    old = oldRect ? *oldRect : self->m_windowRect;
    posChanged = (old.x != self->m_windowRect.x) ||
                 (old.y != self->m_windowRect.y);
    sizeChanged = (old.width != self->m_windowRect.width) ||
                  (old.height != self->m_windowRect.height);
    if (!posChanged && !sizeChanged) return;
    XWidget_updateContentsRect(self);
    if (posChanged) {
        XEvent event;
        XEvent_init(&event, XEVENT_TYPE_MOVE);
        XWidget_sendEvent(self, &event);
    }
    if (sizeChanged) {
        XResizeEvent event;
        XSize size;
        XSize oldSize;
        XSize_init(&size, self->m_windowRect.width, self->m_windowRect.height);
        XSize_init(&oldSize, old.width, old.height);
        XResizeEvent_init(&event, XEVENT_TYPE_RESIZE, &size, &oldSize);
        XWidget_sendEvent(self, (XEvent*)&event);
        XResizeEvent_deinit_base(&event);
#if XLAYOUT_ON
        if (self->m_layout)
            XLayout_activate(self->m_layout);
#endif /* XLAYOUT_ON */
    }
    /* 新旧矩形一并失效（对标 Qt setGeometry_sys 的双失效语义）：
       控件移动/缩放后，旧位置像素由父层背景重绘，不再留下残影。
       此前只发事件不失效旧区域——按钮重排后旧位置文字长期残留。 */
    {
        XWidget* parent = XWidget_parentWidget(self);
        if (parent) {
            XRect dirty = XRect_united(&old, &self->m_windowRect);
            /* 经 addDirtyRegion 联动父链与兄弟交叠保留层。 */
            XWidget_updateRect(parent, &dirty);
        }
    }
    /* 几何变化联动自身与祖先保留层（缓存尺寸/内容随几何失效；父级
       一侧已由上方 updateRect → addDirtyRegion 的保留层联动覆盖）。 */
    if (g_retainedRegistered > 0)
        xwidget_retainedInvalidateChain(self);
}

/** @brief 若为顶层且桥接窗口存在，同步平台窗口几何。 */
static void XWidget_syncWindowGeometry(XWidget* self)
{
    if (!self) return;
    if (self->m_isWindow && self->m_windowHandle) {
        XWindow_setGeometry((XWindow*)self->m_windowHandle,
                            self->m_windowRect.x, self->m_windowRect.y,
                            self->m_windowRect.width, self->m_windowRect.height);
    }
}

void XWidget_setGeometry(XWidget* self, int x, int y, int w, int h)
{
    int minW, minH, maxW, maxH;
    XRect old;
    if (!self) return;
    minW = self->m_minimumSize.width;
    minH = self->m_minimumSize.height;
    maxW = self->m_maximumSize.width;
    maxH = self->m_maximumSize.height;
    w = XWidget_clampSize(w, minW, maxW);
    h = XWidget_clampSize(h, minH, maxH);
    /* Qt::setGeometry() 明确置位两类历史状态，即使新旧矩形相同。 */
    XWidget_attrSet(&self->m_attributes, XWidgetAttribute_Resized, true);
    XWidget_attrSet(&self->m_attributes, XWidgetAttribute_Moved, true);
    if (x == self->m_windowRect.x && y == self->m_windowRect.y &&
        w == self->m_windowRect.width && h == self->m_windowRect.height)
        return;
    old = self->m_windowRect;
    self->m_windowRect.x = x;
    self->m_windowRect.y = y;
    self->m_windowRect.width = w;
    self->m_windowRect.height = h;
    XWidget_recomputeGeometry(self, &old);
    XWidget_syncWindowGeometry(self);
#if XWINDOW_ON && XACCESSIBLE_ON
    XPlatformAccessibility_notifyWidget(XAccessibleEvent_LocationChanged, self);
#endif
}

void XWidget_setGeometryRect(XWidget* self, const XRect* rect)
{
    if (!self || !rect) return;
    XWidget_setGeometry(self, rect->x, rect->y, rect->width, rect->height);
}

void XWidget_updateGeometry(XWidget* self)
{
    XWidget* parent;
    if (!self) return;
#if XLAYOUT_ON
    if (self->m_layout) {
        XLayout_activate(self->m_layout);
        return;
    }
    parent = XWidget_parentWidget(self);
    if (parent && parent->m_layout)
        XLayout_activate(parent->m_layout);
#else
    parent = XWidget_parentWidget(self);
    (void)parent;
#endif /* XLAYOUT_ON */
}

void XWidget_move(XWidget* self, int x, int y)
{
    bool resized;
    if (!self) return;
    resized = XWidget_testAttribute(self, XWidgetAttribute_Resized);
    XWidget_setGeometry(self, x, y, self->m_windowRect.width,
                        self->m_windowRect.height);
    /* move() 只置 WA_Moved；setGeometry 的共享实现产生的新增
     * WA_Resized 状态在尺寸未变时恢复。 */
    if (!resized)
        XWidget_attrSet(&self->m_attributes, XWidgetAttribute_Resized, false);
}

void XWidget_movePoint(XWidget* self, const XPoint* pos)
{
    if (!self || !pos) return;
    XWidget_move(self, pos->x, pos->y);
}

void XWidget_resize(XWidget* self, int w, int h)
{
    bool moved;
    if (!self) return;
    moved = XWidget_testAttribute(self, XWidgetAttribute_Moved);
    XWidget_setGeometry(self, self->m_windowRect.x, self->m_windowRect.y, w, h);
    /* resize() 只置 WA_Resized；保留此前已经置位的 WA_Moved。 */
    if (!moved)
        XWidget_attrSet(&self->m_attributes, XWidgetAttribute_Moved, false);
}

void XWidget_resizeSize(XWidget* self, const XSize* size)
{
    if (!self || !size) return;
    XWidget_resize(self, size->width, size->height);
}

void XWidget_setFixedSize(XWidget* self, int w, int h)
{
    XSize oldMinimum;
    XSize oldMaximum;
    int fixedW;
    int fixedH;
    bool changed;
    if (!self) return;
    oldMinimum = self->m_minimumSize;
    oldMaximum = self->m_maximumSize;
    XWidget_setMinimumSize(self, w, h);
    XWidget_setMaximumSize(self, w, h);
    fixedW = XWidget_clampSize(w, 0, XWIDGET_MAX_SIZE);
    fixedH = XWidget_clampSize(h, 0, XWIDGET_MAX_SIZE);
    changed = oldMinimum.width != self->m_minimumSize.width ||
              oldMinimum.height != self->m_minimumSize.height ||
              oldMaximum.width != self->m_maximumSize.width ||
              oldMaximum.height != self->m_maximumSize.height;
    if (changed && (fixedW != XWIDGET_MAX_SIZE || fixedH != XWIDGET_MAX_SIZE))
        XWidget_resize(self, fixedW, fixedH);
}

void XWidget_setFixedSizeSize(XWidget* self, const XSize* size)
{
    if (!self || !size) return;
    XWidget_setFixedSize(self, size->width, size->height);
}

void XWidget_setFixedWidth(XWidget* self, int width)
{
    if (!self) return;
    XWidget_setMinimumWidth(self, width);
    XWidget_setMaximumWidth(self, width);
}

void XWidget_setFixedHeight(XWidget* self, int height)
{
    if (!self) return;
    XWidget_setMinimumHeight(self, height);
    XWidget_setMaximumHeight(self, height);
}

void XWidget_adjustSize(XWidget* self)
{
    XSize hint;
    XRect children;
    if (!self) return;
    hint = XWidget_sizeHint(self);
    if (!XSize_isValid(&hint)) {
        /* QWidgetPrivate::adjustedSize() falls back to childrenRect when
         * sizeHint() is invalid; an empty child union leaves the size alone. */
        children = XWidget_childrenRect(self);
        if (children.width == 0 && children.height == 0) return;
        hint.width = children.width + 2 * children.x;
        hint.height = children.height + 2 * children.y;
    }
    XWidget_resize(self, hint.width, hint.height);
}

/* ==================== 窗口几何序列化（对标 QWidget::saveGeometry/restoreGeometry） ==================== */

#if XByteArray_ON

/**
 * @brief      从序列化文本游标处解析一个带符号十进制字段。
 * @details    跳过前导空格/制表符后接受可选正负号与十进制数字；数值溢出
 *             int、无数字或数字后紧跟其他字符（如 "12ab"）一律按损坏数据
 *             处理返回 false。解析后游标停在字段末尾（不含分隔空白）。
 * @param      data 序列化缓冲区首地址。
 * @param      length 缓冲区字节长度。
 * @param      cursor 入参/出参：当前解析游标（字节偏移）。
 * @param      out 解析成功时写回字段值。
 * @return     解析成功返回 true；损坏数据返回 false。
 */
static bool xwg_parseField(const char* data, int length, int* cursor, int* out)
{
    int64_t value = 0;
    bool negative = false;
    bool hasDigit = false;
    int i;
    if (!data || !cursor || !out) return false;
    i = *cursor;
    while (i < length && (data[i] == ' ' || data[i] == '\t')) ++i;
    if (i < length && (data[i] == '-' || data[i] == '+')) {
        negative = (data[i] == '-');
        ++i;
    }
    while (i < length && data[i] >= '0' && data[i] <= '9') {
        value = value * 10 + (data[i] - '0');
        if (value > 2147483647) return false; /* 超出 int 范围按损坏数据拒绝 */
        hasDigit = true;
        ++i;
    }
    if (!hasDigit) return false;
    /* 数值后必须是分隔空白或串尾，防止 "12ab" 被部分解析。 */
    if (i < length && data[i] != ' ' && data[i] != '\t' &&
        data[i] != '\r' && data[i] != '\n')
        return false;
    *out = (int)(negative ? -value : value);
    *cursor = i;
    return true;
}

XByteArray* XWidget_saveGeometry(const XWidget* self)
{
    XByteArray* out;
    XRect frame;
    XRect normal;
    char buf[160];
    if (!self || !self->m_isWindow)
        return NULL; /* 仅顶层窗口有效（Qt 同样把窗口几何保存在顶层上）。 */
    out = XByteArray_create();
    if (!out) return NULL;
    /* 顶层控件 frameGeometry == geometry（仓库既有语义），故直接保存
       m_windowRect；normalGeometry 在未进入特殊状态时回退当前几何。 */
    frame = self->m_windowRect;
    normal = XWidget_normalGeometry(self);
    /* 序列化格式沿用仓库惯例（参照 XSplitter_saveState 的 XByteArray 文本
       承载）：魔数 "XWG1" + 9 个空格分隔的十进制字段——
       [0..3] 当前 frameGeometry x/y/w/h；[4..7] 正常态几何 x/y/w/h；
       [8] 窗口状态标志（仅 Maximized|FullScreen，对标 Qt savedState，
       最小化是瞬态不保存）。x/y 允许负值，故用带符号十进制而非定宽数字。 */
    XSnprintf(buf, sizeof(buf),
              "XWG1 %d %d %d %d %d %d %d %d %u",
              frame.x, frame.y, frame.width, frame.height,
              normal.x, normal.y, normal.width, normal.height,
              (unsigned)(self->m_windowState &
                         (XWindowStates)(XWindowState_Maximized |
                                         XWindowState_FullScreen)));
    XByteArray_append_utf8(out, buf);
    return out;
}

bool XWidget_restoreGeometry(XWidget* self, const XByteArray* geometry)
{
    static const char magic[] = "XWG1";
    const char* data;
    int64_t length;
    int fields[9];
    int cursor;
    int i;
    XRect frame;
    XRect normal;
    unsigned savedState;
    if (!self || !geometry) return false;
    if (!self->m_isWindow)
        return false; /* 仅顶层窗口有效；非顶层返回失败。 */
    data = (const char*)XByteArray_constData(geometry);
    length = XByteArray_size_base((const XContainer*)geometry);
    if (!data || length < (int64_t)(sizeof(magic) - 1)) return false;
    if (XStrncmp(data, magic, sizeof(magic) - 1) != 0)
        return false; /* 魔数/版本不符按损坏数据拒绝。 */
    cursor = (int)(sizeof(magic) - 1);
    for (i = 0; i < 9; ++i) {
        if (!xwg_parseField(data, (int)length, &cursor, &fields[i]))
            return false;
    }
    /* 尾部只允许空白；截断或追加垃圾数据一律拒绝。 */
    while (cursor < (int)length &&
           (data[cursor] == ' ' || data[cursor] == '\t' ||
            data[cursor] == '\r' || data[cursor] == '\n'))
        ++cursor;
    if (cursor != (int)length) return false;
    savedState = (unsigned)fields[8];
    /* 只接受 Maximized|FullScreen 组合（保存侧就不会写入其他位）。 */
    if ((savedState & ~((unsigned)XWindowState_Maximized |
                        (unsigned)XWindowState_FullScreen)) != 0)
        return false;
    /* 尺寸必须为正且交给 setGeometry 按最小/最大约束钳位；x/y 允许任意值。 */
    if (fields[2] <= 0 || fields[3] <= 0 || fields[6] <= 0 || fields[7] <= 0)
        return false;
    XRect_init(&frame, fields[0], fields[1], fields[2], fields[3]);
    XRect_init(&normal, fields[4], fields[5], fields[6], fields[7]);
    /* 对标 QWidget::restoreGeometry 的恢复次序：先恢复窗口状态标志
       （触发本仓库 setWindowState 的 normalGeometry 记账与桥接窗口同步），
       再恢复当前几何，最后写回保存时的正常态几何——顺序保证最大化窗口
       恢复后 normalGeometry 与保存值一致。简化项：不做 Qt 的屏幕可用
       区域夹取（无多屏/任务栏概念），尺寸钳位由 setGeometry 完成。 */
    XWidget_setWindowState(self, (XWindowStates)savedState);
    XWidget_setGeometry(self, frame.x, frame.y, frame.width, frame.height);
    self->m_normalGeometry = normal;
    return true;
}

#endif /* XByteArray_ON */

/* ==================== 尺寸约束 ==================== */

XSize XWidget_minimumSize(const XWidget* self)
{
    XSize out;
    if (!self) { XSize_init(&out, 0, 0); return out; }
    out = self->m_minimumSize;
    return out;
}

void XWidget_setMinimumSize(XWidget* self, int w, int h)
{
    int minW, minH;
    bool resized;
    if (!self) return;
    /* Qt 使用 QWIDGETSIZE_MAX 表示未设置的最小尺寸。 */
    if (w == XWIDGET_MAX_SIZE) w = 0;
    if (h == XWIDGET_MAX_SIZE) h = 0;
    minW = XWidget_clampSize(w, 0, XWIDGET_MAX_SIZE);
    minH = XWidget_clampSize(h, 0, XWIDGET_MAX_SIZE);
    if (self->m_minimumSize.width == minW &&
        self->m_minimumSize.height == minH)
        return;
    self->m_minimumSize.width = minW;
    self->m_minimumSize.height = minH;
#if XWINDOW_ON
    if (self->m_isWindow && self->m_windowHandle) {
        XWindow_setMinimumSize((XWindow*)self->m_windowHandle,
                               &self->m_minimumSize);
    }
#endif /* XWINDOW_ON */
    if (self->m_windowRect.width < minW || self->m_windowRect.height < minH) {
        resized = XWidget_testAttribute(self, XWidgetAttribute_Resized);
        XWidget_resize(self,
                       self->m_windowRect.width < minW ? minW : self->m_windowRect.width,
                       self->m_windowRect.height < minH ? minH : self->m_windowRect.height);
        XWidget_attrSet(&self->m_attributes, XWidgetAttribute_Resized, resized);
    }
}

void XWidget_setMinimumSizeSize(XWidget* self, const XSize* size)
{
    if (!self || !size) return;
    XWidget_setMinimumSize(self, size->width, size->height);
}

int XWidget_minimumWidth(const XWidget* self)
{ return XWidget_minimumSize(self).width; }

int XWidget_minimumHeight(const XWidget* self)
{ return XWidget_minimumSize(self).height; }

void XWidget_setMinimumWidth(XWidget* self, int w)
{ if (self) XWidget_setMinimumSize(self, w, self->m_minimumSize.height); }

void XWidget_setMinimumHeight(XWidget* self, int h)
{ if (self) XWidget_setMinimumSize(self, self->m_minimumSize.width, h); }

XSize XWidget_maximumSize(const XWidget* self)
{
    XSize out;
    if (!self) { XSize_init(&out, XWIDGET_MAX_SIZE, XWIDGET_MAX_SIZE); return out; }
    out = self->m_maximumSize;
    return out;
}

void XWidget_setMaximumSize(XWidget* self, int w, int h)
{
    int maxW, maxH;
    bool resized;
    if (!self) return;
    maxW = XWidget_clampSize(w, 0, XWIDGET_MAX_SIZE);
    maxH = XWidget_clampSize(h, 0, XWIDGET_MAX_SIZE);
    if (self->m_maximumSize.width == maxW &&
        self->m_maximumSize.height == maxH)
        return;
    self->m_maximumSize.width = maxW;
    self->m_maximumSize.height = maxH;
#if XWINDOW_ON
    if (self->m_isWindow && self->m_windowHandle) {
        XWindow_setMaximumSize((XWindow*)self->m_windowHandle,
                               &self->m_maximumSize);
    }
#endif /* XWINDOW_ON */
    if (self->m_windowRect.width > maxW || self->m_windowRect.height > maxH) {
        resized = XWidget_testAttribute(self, XWidgetAttribute_Resized);
        XWidget_resize(self,
                       self->m_windowRect.width > maxW ? maxW : self->m_windowRect.width,
                       self->m_windowRect.height > maxH ? maxH : self->m_windowRect.height);
        XWidget_attrSet(&self->m_attributes, XWidgetAttribute_Resized, resized);
    }
}

void XWidget_setMaximumSizeSize(XWidget* self, const XSize* size)
{
    if (!self || !size) return;
    XWidget_setMaximumSize(self, size->width, size->height);
}

int XWidget_maximumWidth(const XWidget* self)
{ return XWidget_maximumSize(self).width; }

int XWidget_maximumHeight(const XWidget* self)
{ return XWidget_maximumSize(self).height; }

void XWidget_setMaximumWidth(XWidget* self, int w)
{ if (self) XWidget_setMaximumSize(self, w, self->m_maximumSize.height); }

void XWidget_setMaximumHeight(XWidget* self, int h)
{ if (self) XWidget_setMaximumSize(self, self->m_maximumSize.width, h); }

XSize XWidget_baseSize(const XWidget* self)
{
    XSize out;
    if (!self) { XSize_init(&out, -1, -1); return out; }
    out = self->m_baseSize;
    return out;
}

void XWidget_setBaseSize(XWidget* self, int w, int h)
{
    if (!self) return;
    self->m_baseSize.width = w;
    self->m_baseSize.height = h;
}

void XWidget_setBaseSizeSize(XWidget* self, const XSize* size)
{
    if (!self || !size) return;
    XWidget_setBaseSize(self, size->width, size->height);
}

XSize XWidget_sizeIncrement(const XWidget* self)
{
    XSize out;
    if (!self) { XSize_init(&out, 0, 0); return out; }
    out = self->m_sizeIncrement;
    return out;
}

void XWidget_setSizeIncrement(XWidget* self, int w, int h)
{
    if (!self) return;
    self->m_sizeIncrement.width = w;
    self->m_sizeIncrement.height = h;
}

void XWidget_setSizeIncrementSize(XWidget* self, const XSize* size)
{
    if (!self || !size) return;
    XWidget_setSizeIncrement(self, size->width, size->height);
}

XSize XWidget_sizeHint(const XWidget* self)
{
    XSize out;
    if (!self) { XSize_init(&out, -1, -1); return out; }
#if XLAYOUT_ON
    if (self->m_layout)
        return XLayout_totalSizeHint(self->m_layout);
#endif /* XLAYOUT_ON */
    out = self->m_sizeHint;
    return out;
}

XSize XWidget_minimumSizeHint(const XWidget* self)
{
    XSize out;
    if (!self) { XSize_init(&out, -1, -1); return out; }
#if XLAYOUT_ON
    if (self->m_layout)
        return XLayout_totalMinimumSize(self->m_layout);
#endif /* XLAYOUT_ON */
    out = self->m_minimumSizeHint;
    return out;
}

void XWidget_setSizeHint(XWidget* self, const XSize* hint)
{
    if (!self || !hint) return;
    self->m_sizeHint = *hint;
}

void XWidget_setMinimumSizeHint(XWidget* self, const XSize* hint)
{
    if (!self || !hint) return;
    self->m_minimumSizeHint = *hint;
}

int XWidget_heightForWidth(const XWidget* self, int width)
{
    int height;
    XSize hint;
    if (!self) return -1;
#if XLAYOUT_ON
    if (self->m_layout &&
        XLayoutItem_hasHeightForWidth_base((const XLayoutItem*)self->m_layout))
        return XLayout_totalHeightForWidth(self->m_layout, width);
#endif /* XLAYOUT_ON */
    if (!XWidget_hasHeightForWidth(self))
        return -1;
    if (self->m_heightForWidthHandler)
        height = self->m_heightForWidthHandler((XWidget*)self, width,
                                               self->m_heightForWidthUserData);
    else {
        hint = self->m_sizeHint;
        height = hint.height;
    }
    if (height < 0) return -1;
    if (height > self->m_maximumSize.height) height = self->m_maximumSize.height;
    if (height < self->m_minimumSize.height) height = self->m_minimumSize.height;
    return height;
}

bool XWidget_hasHeightForWidth(const XWidget* self)
{
    if (!self) return false;
#if XLAYOUT_ON
    if (self->m_layout)
        return XLayoutItem_hasHeightForWidth_base(
            (const XLayoutItem*)self->m_layout);
#endif /* XLAYOUT_ON */
    return self->m_heightForWidthHandler != NULL ||
           XWidgetSizePolicy_hasHeightForWidth(&self->m_sizePolicy);
}

void XWidget_setHeightForWidthHandler(XWidget* self,
                                      XWidgetHeightForWidthHandler handler,
                                      void* userData)
{
    if (!self) return;
    self->m_heightForWidthHandler = handler;
    self->m_heightForWidthUserData = userData;
}

XWidgetSizePolicy XWidget_sizePolicy(const XWidget* self)
{
    return self ? self->m_sizePolicy : XWidgetSizePolicy_create();
}

void XWidget_setSizePolicy(XWidget* self,
                           XWidgetSizePolicyPolicy horizontal,
                           XWidgetSizePolicyPolicy vertical)
{
    if (!self) return;
    /* 显式调用 setSizePolicy 标记 OwnSizePolicy（对标 Qt WA_WState_OwnSizePolicy）；
     * XFrame::setFrameStyle 据此判断是否需要按形状接管尺寸策略。 */
    XWidget_setAttribute(self, XWidgetAttribute_WState_OwnSizePolicy, true);
    self->m_sizePolicy.m_horizontalPolicy = (uint8_t)horizontal;
    self->m_sizePolicy.m_verticalPolicy = (uint8_t)vertical;
}

void XWidget_setSizePolicyFull(XWidget* self, const XWidgetSizePolicy* policy)
{
    if (!self || !policy) return;
    XWidget_setAttribute(self, XWidgetAttribute_WState_OwnSizePolicy, true);
    self->m_sizePolicy = *policy;
}

/* ==================== 控件树与命中测试 ==================== */

XWidget* XWidget_parentWidget(const XWidget* self)
{
    XObject* parent;
    if (!self) return NULL;
    /* 根因防护（批次七方向 b 的收口）：绕过控件 API 直接
       XObject_setParent((XObject*)widget, 非控件) 后，父链上的
       m_parent 不是 XWidget 布局；此处盲转会按 XWidget* 读
       m_layout/m_visible 等越界字段（悬挂风险）。读前校验
       is_widget，非控件父一律视同无父控件返回 NULL；正常路径
       （XWidget_init/XWidget_setParent 已归一化）行为零变化。 */
    parent = XObject_parent((XObject*)self);
    return (parent && parent->is_widget) ? (XWidget*)parent : NULL;
}

void XWidget_setParent(XWidget* self, XWidget* parent, XWidgetFlags flags)
{
    XWidget* oldParent;
    bool parentChanged;
    bool wasWindow;
    if (!self) return;
    /* 根因防护（同 XWidget_init）：创建后重挂父也必须校验 is_widget，
       否则裸 XWindow 等非控件对象经此挂父后，父链遍历仍会把非控件
       结构体按 XWidget* 解引用（越界读 m_layout/m_visible 等）。 */
    if (parent && !((const XObject*)parent)->is_widget)
        parent = NULL;
    /* QWidget 禁止把控件设置为自身或其后代；C 接口采用安全返回，
     * 避免破坏 XObject 的父子链。 */
    if (self == parent) return;
    oldParent = XWidget_parentWidget(self);
    parentChanged = oldParent != parent;
    if (parentChanged) {
        /* Qt setParent() 会让已生效可见的控件先变为不可见，调用方需
         * 显式 show()；尚未生效但已显式 show 的控件保留该状态，
         * 以便挂入可见父控件后按 Qt 的 showChildren 规则恢复。 */
        if (self->m_visible)
            XWidget_setVisible(self, false);
    }
    wasWindow = (self->m_isWindow != 0);
    self->m_windowFlags = flags;
    if (!parent && !(flags & (XWidgetFlags)XWindowType_Window))
        self->m_windowFlags |= (XWidgetFlags)XWindowType_Window;
    /* 对标 Qt：Qt::Popup 即窗口（见 XWidget_init 同规则注释）。 */
    self->m_isWindow = (!parent ||
                        (flags & (XWidgetFlags)XWindowType_Window) ||
                        (flags & (XWidgetFlags)XWindowType_Popup)) ? 1 : 0;
    /* 从顶层降级为子控件时释放其桥接窗口。 */
    if (wasWindow && !self->m_isWindow)
        XWidget_destroyWindow(self);
#if XAPPLICATION_ON && XGUIAPPLICATION_ON
    if (wasWindow && !self->m_isWindow)
        XApplication_unregisterTopLevelWidget(self);
    else if (!wasWindow && self->m_isWindow)
        XApplication_registerTopLevelWidget(self);
#endif
    XObject_setParent((XObject*)self, parent ? (XObject*)parent : NULL);
    if (parentChanged && parent) {
        /* 对标 Qt QWidgetPrivate::setParent_sys（qwidget.cpp:10925-11035，
         * 全程不写 data.crect）：换父后位置与尺寸完整保留。原「移到
         * (0,0)」系误标（Qt 源码/文档均无此行为），曾把 wrapTabPage
         * 重挂的滚动条几何打回原点（第一轮 #47 根因）。本修复曾在
         * stash pop 合并中被远端批次覆盖丢失，此次重新摘除。 */
    }
    /* 父链变化后重算生效可见状态。 */
    {
        bool newVisible = self->m_explicitShow;
        if (!self->m_isWindow) {
            XWidget* p = (XWidget*)XObject_parent((XObject*)self);
            if (p && !p->m_visible) newVisible = false;
        }
        self->m_visible = newVisible ? 1 : 0;
    }
#if XAPPLICATION_ON && XGUIAPPLICATION_ON
    if (self->m_isWindow)
        XApplication_registerTopLevelWidget(self);
#endif
#if XWINDOW_ON && XACCESSIBLE_ON
    XPlatformAccessibility_notifyWidget(XAccessibleEvent_StateChanged, self);
#endif
}

void XWidget_setParentPlain(XWidget* self, XWidget* parent)
{
    XWidgetFlags flags;
    if (!self) return;
    /* QWidget::setParent(QWidget*) 对同一父控件直接返回。 */
    if (XWidget_parentWidget(self) == parent) return;
    /* QWidget::setParent(QWidget*) 传递 windowFlags() 去除
     * Qt::WindowType_Mask；否则顶层控件重新挂接后仍会被识别为窗口。 */
    flags = self->m_windowFlags & (XWidgetFlags)~0xffu;
    XWidget_setParent(self, parent, flags);
}

XWidget* XWidget_childAt(const XWidget* self, const XPoint* point)
{
    const XVector* children;
    size_t n;
    size_t i;
    if (!self || !point) return NULL;
    if (self->m_mask.count > 0 && self->m_mask.rects &&
        !XRegion_contains(&self->m_mask, point->x, point->y))
        return NULL;
    children = XObject_children((XObject*)self);
    n = children ? XVector_size_base((const XContainer*)children) : 0;
    /* 逆序命中：后入栈的子控件绘制在上层（对标 Qt 子控件 Z 序）。 */
    for (i = n; i > 0; --i) {
        XObject* child = *(XObject**)XVector_at_base(children, (int64_t)(i - 1));
        XWidget* widget;
        XPoint local;
        XWidget* deep;
        if (!child || !child->is_widget) continue;
        widget = (XWidget*)child;
        /* 对标 Qt 6.8 QWidgetPrivate::childAtRecursiveHelper：只跳过
         * isHidden()（显式隐藏位 WA_WState_Hidden）与窗口型子控件，
         * 不要求生效可见（isVisible 含父链）——父窗口未 show 时命中
         * 测试仍按几何进行，事件投递路径（可见窗口内）等价。 */
        if (XWidget_testAttribute(widget, XWidgetAttribute_WState_Hidden))
            continue;
        if (!XRect_contains(&widget->m_windowRect, point->x, point->y)) continue;
        local.x = point->x - widget->m_windowRect.x;
        local.y = point->y - widget->m_windowRect.y;
        deep = XWidget_childAt(widget, &local);
        if (deep) return deep;
        return widget;
    }
    return NULL;
}

XWidget* XWidget_childAt_2(const XWidget* self, int x, int y)
{
    XPoint point;
    XPoint_init(&point, x, y);
    return XWidget_childAt(self, &point);
}

XWidget* XWidget_childAtGlobal(const XWidget* self, const XPoint* globalPoint)
{
    XPoint local;
    if (!self || !globalPoint) return NULL;
    local = XWidget_mapFromGlobal(self, globalPoint);
    return XWidget_childAt(self, &local);
}


/* ==================== 遮罩（对标 QWidget::mask/setMask/clearMask） ==================== */

XRegion XWidget_mask(const XWidget* self)
{
    XRegion out;
    XRegion_init(&out);
    if (self)
        XRegion_copy(&self->m_mask, &out);
    return out;
}

bool XWidget_hasMask(const XWidget* self)
{
    return self && self->m_mask.count > 0;
}

void XWidget_setMask(XWidget* self, const XRegion* region)
{
    bool nowEmpty;
    bool wasEmpty;
    if (!self) return;
    wasEmpty = self->m_mask.count <= 0;
    nowEmpty = (!region || region->count <= 0);
    if (!nowEmpty)
        XRegion_copy(region, &self->m_mask);
    else
        XRegion_clear(&self->m_mask);
    /* 空态变化或两次均为非空（可能内容不同）都触发刷新并同步原生窗口。 */
    if ((wasEmpty != nowEmpty) || (!wasEmpty && !nowEmpty)) {
        if (self->m_isWindow && self->m_windowHandle)
            XWindow_setMask((XWindow*)self->m_windowHandle,
                            nowEmpty ? NULL : &self->m_mask);
        XWidget_update(self);
    }
}

void XWidget_clearMask(XWidget* self)
{
    if (!self) return;
    XWidget_setMask(self, NULL);
}

/* ==================== Z 序（对标 QWidget::raise/lower/stackUnder） ==================== */

/** @brief 返回 children 列表中 target 控件（XWidget）的索引；找不到或非控件返回 -1。 */
static int64_t XWidget_widgetVectorIndex(const XVector* children,
                                         const XWidget* target)
{
    size_t n;
    size_t i;
    if (!children || !target) return -1;
    n = XVector_size_base((const XContainer*)children);
    for (i = 0; i < n; ++i) {
        XObject* child = *(XObject**)XVector_at_base(children, (int64_t)i);
        if (child && child->is_widget && (XWidget*)child == target)
            return (int64_t)i;
    }
    return -1;
}

/** @brief 返回 children 列表中第一个 XWidget 子控件的索引；无则返回 -1。 */
static int64_t XWidget_firstWidgetVectorIndex(const XVector* children)
{
    size_t n;
    size_t i;
    if (!children) return -1;
    n = XVector_size_base((const XContainer*)children);
    for (i = 0; i < n; ++i) {
        XObject* child = *(XObject**)XVector_at_base(children, (int64_t)i);
        if (child && child->is_widget) return (int64_t)i;
    }
    return -1;
}

/** @brief 返回 children 列表中最后一个 XWidget 子控件的索引；无则返回 -1。 */
static int64_t XWidget_lastWidgetVectorIndex(const XVector* children)
{
    size_t n;
    size_t i;
    if (!children) return -1;
    n = XVector_size_base((const XContainer*)children);
    for (i = n; i > 0; --i) {
        XObject* child = *(XObject**)XVector_at_base(children, (int64_t)(i - 1));
        if (child && child->is_widget) return (int64_t)(i - 1);
    }
    return -1;
}

/** @brief 顶层控件 Z 序变更：请求原生窗口提升/降低并发送事件。 */
static void XWidget_emitZOrderChange(XWidget* self)
{
    XEvent event;
    if (!self) return;
    XEvent_init(&event, XEVENT_TYPE_Z_ORDER_CHANGE);
    XWidget_event_base(self, &event);
}

void XWidget_raise(XWidget* self)
{
    const XVector* children;
    XWidget* parent;
    int64_t from;
    int64_t last;
    if (!self) return;
    if (self->m_isWindow) {
#if XWINDOW_ON
        if (self->m_windowHandle)
            XWindow_raise((XWindow*)self->m_windowHandle);
#endif /* XWINDOW_ON */
        XWidget_emitZOrderChange(self);
        return;
    }
    parent = XWidget_parentWidget(self);
    if (!parent) return;
    children = XObject_children((XObject*)parent);
    from = XWidget_widgetVectorIndex(children, self);
    last = XWidget_lastWidgetVectorIndex(children);
    if (from < 0 || last < 0 || from == last) return;
    XVector_move((XVector*)children, from, last);
    XWidget_updateRect(parent, &self->m_windowRect);
    XWidget_emitZOrderChange(self);
}

void XWidget_lower(XWidget* self)
{
    const XVector* children;
    XWidget* parent;
    int64_t from;
    int64_t first;
    if (!self) return;
    if (self->m_isWindow) {
#if XWINDOW_ON
        if (self->m_windowHandle)
            XWindow_lower((XWindow*)self->m_windowHandle);
#endif /* XWINDOW_ON */
        XWidget_emitZOrderChange(self);
        return;
    }
    parent = XWidget_parentWidget(self);
    if (!parent) return;
    children = XObject_children((XObject*)parent);
    from = XWidget_widgetVectorIndex(children, self);
    first = XWidget_firstWidgetVectorIndex(children);
    if (from < 0 || first < 0 || from == first) return;
    XVector_move((XVector*)children, from, first);
    XWidget_updateRect(parent, &self->m_windowRect);
    XWidget_emitZOrderChange(self);
}

void XWidget_stackUnder(XWidget* self, XWidget* other)
{
    const XVector* children;
    XWidget* parent;
    int64_t from;
    int64_t to;
    if (!self || !other || self == other) return;
    if (self->m_isWindow || other->m_isWindow) return;
    parent = XWidget_parentWidget(self);
    if (!parent || XWidget_parentWidget(other) != parent) return;
    children = XObject_children((XObject*)parent);
    from = XWidget_widgetVectorIndex(children, self);
    to = XWidget_widgetVectorIndex(children, other);
    if (from < 0 || to < 0) return;
    /* 对齐 QWidget::stackUnder：目标在其上方时，把 self 插到目标之前。 */
    if (from < to)
        --to;
    if (from == to) return;
    XVector_move((XVector*)children, from, to);
    XWidget_updateRect(parent, &self->m_windowRect);
    XWidget_emitZOrderChange(self);
}

XRect XWidget_childrenRect(const XWidget* self)
{
    const XVector* children;
    XRect out;
    size_t n;
    size_t i;
    bool first = true;
    XRect_init(&out, 0, 0, 0, 0);
    if (!self) return out;
    children = XObject_children((XObject*)self);
    n = children ? XVector_size_base((const XContainer*)children) : 0;
    for (i = 0; i < n; ++i) {
        XObject* child = *(XObject**)XVector_at_base(children, (int64_t)i);
        XWidget* widget;
        if (!child || !child->is_widget) continue;
        widget = (XWidget*)child;
        if (widget->m_isWindow || !widget->m_visible) continue;
        if (first) {
            out = widget->m_windowRect;
            first = false;
        } else {
            out = XRect_united(&out, &widget->m_windowRect);
        }
    }
    return out;
}

XRegion XWidget_childrenRegion(const XWidget* self)
{
    const XVector* children;
    XRegion out;
    size_t n;
    size_t i;
    XRegion_init(&out);
    if (!self) return out;
    children = XObject_children((XObject*)self);
    n = children ? XVector_size_base((const XContainer*)children) : 0;
    for (i = 0; i < n; ++i) {
        XObject* child = *(XObject**)XVector_at_base(children, (int64_t)i);
        XWidget* widget;
        if (!child || !child->is_widget) continue;
        widget = (XWidget*)child;
        if (widget->m_isWindow || !widget->m_visible) continue;
        XRegion_addRect(&out, &widget->m_windowRect);
    }
    return out;
}

bool XWidget_isAncestorOf(const XWidget* self, const XWidget* child)
{
    return XWidget_isAncestor(self, child);
}

XWidget* XWidget_window(const XWidget* self)
{
    return XWidget_topLevel(self);
}

XWidget* XWidget_nativeParentWidget(const XWidget* self)
{
    XWidget* parent;
    if (!self) return NULL;
    /* 同 parentWidget 防护：非控件父视同无父，不做控件级上溯。 */
    parent = XWidget_parentWidget(self);
    while (parent && !parent->m_windowHandle)
        parent = XWidget_parentWidget(parent);
    return parent;
}

XWidget* XWidget_topLevelWidget(const XWidget* self)
{
    return XWidget_topLevel(self);
}

XWindow* XWidget_nativeWindow(const XWidget* self)
{
    XWidget* top = XWidget_topLevel(self);
    return top ? (XWindow*)top->m_windowHandle : NULL;
}

XWindow* XWidget_windowHandle(const XWidget* self)
{
    return XWidget_nativeWindow(self);
}

/* ==================== 坐标映射 ==================== */

XPoint XWidget_mapToGlobal(const XWidget* self, const XPoint* local)
{
    XPoint out;
    XWidget* top;
    XPoint_init(&out, 0, 0);
    if (!self || !local) return out;
    {
        XPoint offset = XWidget_accumulateOffset(self);
        out.x = local->x + offset.x;
        out.y = local->y + offset.y;
    }
    top = XWidget_topLevel(self);
    if (top) {
        if (top->m_windowHandle)
            out = XWindow_mapToGlobal((XWindow*)top->m_windowHandle, &out);
        else {
            out.x += top->m_windowRect.x;
            out.y += top->m_windowRect.y;
        }
    }
    return out;
}

XPoint XWidget_mapFromGlobal(const XWidget* self, const XPoint* global)
{
    XPoint out;
    XWidget* top;
    XPoint_init(&out, 0, 0);
    if (!self || !global) return out;
    out = *global;
    top = XWidget_topLevel(self);
    if (top) {
        if (top->m_windowHandle)
            out = XWindow_mapFromGlobal((XWindow*)top->m_windowHandle, &out);
        else {
            out.x -= top->m_windowRect.x;
            out.y -= top->m_windowRect.y;
        }
    }
    {
        XPoint offset = XWidget_accumulateOffset(self);
        out.x -= offset.x;
        out.y -= offset.y;
    }
    return out;
}

XPoint XWidget_mapToParent(const XWidget* self, const XPoint* local)
{
    XPoint out;
    XPoint_init(&out, 0, 0);
    if (!self || !local) return out;
    /* 无父控件时 Qt 定义为 mapToGlobal。 */
    if (!XObject_parent((XObject*)self))
        return XWidget_mapToGlobal(self, local);
    /* QWidget 使用 geometry/crect 的 topLeft()；本项目对应的父坐标
     * 原点是 m_windowRect，而 m_contentsRect 只表示客户区内边距。 */
    out.x = local->x + self->m_windowRect.x;
    out.y = local->y + self->m_windowRect.y;
    return out;
}

XPoint XWidget_mapFromParent(const XWidget* self, const XPoint* parent)
{
    XPoint out;
    XPoint_init(&out, 0, 0);
    if (!self || !parent) return out;
    /* 无父控件时 Qt 定义为 mapFromGlobal。 */
    if (!XObject_parent((XObject*)self))
        return XWidget_mapFromGlobal(self, parent);
    out.x = parent->x - self->m_windowRect.x;
    out.y = parent->y - self->m_windowRect.y;
    return out;
}

XPoint XWidget_mapTo(const XWidget* self, const XWidget* target, const XPoint* local)
{
    XPoint global;
    if (!self || !local) { XPoint_init(&global, 0, 0); return global; }
    global = XWidget_mapToGlobal(self, local);
    if (target)
        return XWidget_mapFromGlobal(target, &global);
    return global;
}

XPoint XWidget_mapFrom(const XWidget* self, const XWidget* source, const XPoint* local)
{
    XPoint global;
    if (!self || !source || !local) {
        XPoint_init(&global, 0, 0);
        return global;
    }
    global = XWidget_mapToGlobal(source, local);
    return XWidget_mapFromGlobal(self, &global);
}

/* ==================== 可见性与窗口状态（对标 QWidget） ==================== */

bool XWidget_isVisible(const XWidget* self)
{
    return XWidget_effectiveVisible(self);
}

bool XWidget_isHidden(const XWidget* self)
{
    /* 对标 Qt 6.8（qwidget.h：isHidden()==testAttribute(WA_WState_Hidden)）：
     * “隐藏”指显式隐藏位，与“从未显式 show”解耦——新建子控件（父未
     * 显示）不算隐藏，随父链首次 show 自动显示。构造期置位规则见
     * XWidget_init（顶层=Hidden、有父=不 Hidden），hide()/show() 经
     * XWidget_setExplicitVisibleRecursive 翻转同一属性位。 */
    return self ? XWidget_testAttribute(self, XWidgetAttribute_WState_Hidden)
                : false;
}

bool XWidget_isVisibleTo(const XWidget* self, const XWidget* ancestor)
{
    const XWidget* w;
    if (!self) return false;
    if (!ancestor)
        return XWidget_isVisible(self);
    /* Qt 6.8 的实现不会验证 ancestor 是否确为祖先，也不会检查
     * ancestor 自身的显式隐藏状态：循环只检查 self 到 ancestor
     * 之前的父链，遇到窗口或父链末端即返回当前节点的 isHidden 结果。
     * isHidden 读显式隐藏位（WA_WState_Hidden），见 XWidget_isHidden。 */
    w = self;
    while (w &&
           !XWidget_testAttribute(w, XWidgetAttribute_WState_Hidden) &&
           !w->m_isWindow &&
           XObject_parent((XObject*)w) &&
           XObject_parent((XObject*)w) != (const XObject*)ancestor)
        w = (const XWidget*)XObject_parent((XObject*)w);
    return w && !XWidget_testAttribute(w, XWidgetAttribute_WState_Hidden);
}

void XWidget_setVisible(XWidget* self, bool visible)
{
    bool wasVisible;
    if (!self) return;
    if (self->m_inShow) return;
    wasVisible = self->m_visible != 0;
    if (self->m_isWindow) {
        /* 顶层控件：惰性创建桥接窗口后再映射/取消映射。 */
        if (visible && !self->m_windowHandle)
            XWidget_createWindow(self);
        /* QWidget 首次映射前必须有一帧完整的初始内容。此前子控件构造时
         * 留下的局部 update 不能代替首帧：双缓冲 DIRECT 模式会正确保留
         * 它们，却只提交那些局部矩形，导致新 X11 窗口的其余像素未定义。
         * 此处仅在从隐藏转为可见时合并根客户区；随后仍由正常事件循环
         * 异步提交，后续高频 update 保持局部刷新。 */
        if (visible && !wasVisible)
            XWidget_update(self);
        if (self->m_windowHandle && !self->m_inShow) {
            self->m_inShow = 1;
            XWindow_setVisible((XWindow*)self->m_windowHandle, visible);
            self->m_inShow = 0;
        }
    }
    XWidget_setExplicitVisibleRecursive(self, visible, self->m_isWindow);
    if (!self->m_isWindow && wasVisible != visible) {
        /* 子控件显隐同样驱动顶层脏区合成（对标 Qt show/hide 后的重绘
         * 语义）：parent+flags=0 的子控件形态对话框 show/hide 不标脏时，
         * 脏区合成器只重画已有脏矩形——show 后内容永不出现（"弹不出"）、
         * hide 后残影永不消失。XWidget_update 按子控件矩形折算进顶层
         * 脏区，show 与 hide 两个方向都需要。 */
        XWidget_update(self);
    }
#if XWINDOW_ON && XACCESSIBLE_ON
    XPlatformAccessibility_notifyWidget(XAccessibleEvent_StateChanged, self);
#endif
#if XLAYOUT_ON
    if (visible && self->m_layout)
        XLayout_activate(self->m_layout);
#endif /* XLAYOUT_ON */
}

void XWidget_show(XWidget* self)
{
    XWidget_setVisible(self, true);
}

void XWidget_hide(XWidget* self)
{
    XWidget_setVisible(self, false);
}

void XWidget_setHidden(XWidget* self, bool hidden)
{
    XWidget_setVisible(self, !hidden);
}

void XWidget_showNormal(XWidget* self)
{
    if (!self) return;
    XWidget_setWindowState(self, XWindowState_NoState);
    XWidget_setVisible(self, true);
}

void XWidget_showMinimized(XWidget* self)
{
    if (!self) return;
    XWidget_setWindowState(self, XWindowState_Minimized);
    XWidget_setVisible(self, true);
}

void XWidget_showMaximized(XWidget* self)
{
    if (!self) return;
    XWidget_setWindowState(self, XWindowState_Maximized);
    XWidget_setVisible(self, true);
}

void XWidget_showFullScreen(XWidget* self)
{
    if (!self) return;
    XWidget_setWindowState(self, XWindowState_FullScreen);
    XWidget_setVisible(self, true);
}

bool XWidget_isMinimized(const XWidget* self)
{
    return self && self->m_isWindow &&
           (self->m_windowState & XWindowState_Minimized) != 0;
}

bool XWidget_isMaximized(const XWidget* self)
{
    return self && self->m_isWindow &&
           (self->m_windowState & XWindowState_Maximized) != 0;
}

bool XWidget_isFullScreen(const XWidget* self)
{
    return self && self->m_isWindow &&
           (self->m_windowState & XWindowState_FullScreen) != 0;
}

bool XWidget_isActiveWindow(const XWidget* self)
{
    XWidget* top;
    if (!self) return false;
    top = self->m_isWindow ? (XWidget*)self : XWidget_topLevel(self);
    if (!top) return false;
    if (top->m_windowHandle)
        return XWindow_isActive((XWindow*)top->m_windowHandle);
    return (top->m_windowState & XWindowState_Active) != 0;
}

bool XWidget_isModal(const XWidget* self)
{
    XWidget* top;
    if (!self) return false;
    top = self->m_isWindow ? (XWidget*)self : XWidget_topLevel(self);
    if (!top) return false;
    return top->m_windowModality != XWindowModality_NonModal ||
           XWidget_testAttribute(top, XWidgetAttribute_ShowModal);
}

XWindowStates XWidget_windowState(const XWidget* self)
{
    return self ? self->m_windowState : XWindowState_NoState;
}

void XWidget_setWindowState(XWidget* self, XWindowStates state)
{
    XWindowStates old;
    XWindowStates special;
    if (!self) return;
    old = self->m_windowState;
    special = (XWindowStates)(XWindowState_Minimized |
                              XWindowState_Maximized |
                              XWindowState_FullScreen);
    if ((old & special) == 0 && (state & special) != 0) {
        /* 首次进入最小化/最大化/全屏：保存正常几何（对标 normalGeometry）。 */
        self->m_normalGeometry = self->m_windowRect;
    }
    self->m_windowState = state;
    if (self->m_isWindow && self->m_windowHandle)
        XWindow_setWindowStates((XWindow*)self->m_windowHandle, state);
}

void XWidget_overrideWindowState(XWidget* self, XWindowStates state)
{
    if (!self) return;
    /* 对标 QWidget::overrideWindowState：只覆盖逻辑状态，不请求平台窗口，
     * 也不维护 normalGeometry。当前事件系统没有 isOverride 标志，故不伪造
     * 状态变更事件。 */
    self->m_windowState = state;
}

XWindowModality XWidget_windowModality(const XWidget* self)
{
    return self ? self->m_windowModality : XWindowModality_NonModal;
}

void XWidget_setWindowModality(XWidget* self, XWindowModality modality)
{
    if (!self) return;
    self->m_windowModality = modality;
    if (self->m_isWindow && self->m_windowHandle)
        XWindow_setModality((XWindow*)self->m_windowHandle, modality);
}

void XWidget_activateWindow(XWidget* self)
{
    XWidget* top;
    if (!self) return;
    top = self->m_isWindow ? (XWidget*)self : XWidget_topLevel(self);
    if (!top) return;
    if (top->m_windowHandle)
        XWindow_requestActivate((XWindow*)top->m_windowHandle);
    top->m_windowState |= (XWindowStates)XWindowState_Active;
}

bool XWidget_close(XWidget* self)
{
    XCloseEvent event;
    bool accepted;
    if (!self) return true;
    if (self->m_isClosing) return true;
    self->m_isClosing = 1;
    XCloseEvent_init(&event, XEVENT_TYPE_CLOSE);
    XWidget_event_base(self, (XEvent*)&event);
    accepted = XEvent_isAccepted((XEvent*)&event);
    XCloseEvent_deinit_base(&event);
    self->m_isClosing = 0;
    if (accepted)
        XWidget_setVisible(self, false);
    return accepted;
}

/* ==================== 标题/图标/文件路径/透明度（对标 QWidget） ==================== */

const XString* XWidget_windowTitle(const XWidget* self)
{
    return self ? self->m_windowTitle : NULL;
}

void XWidget_setWindowTitle(XWidget* self, const XString* title)
{
    XString* old;
    XString* copy;
    bool changed;
    if (!self) return;
    copy = XWidget_copyString(title);
    old = self->m_windowTitle;
    if (old && copy)
        changed = !XString_equals(old, copy, XChar_CaseSensitive);
    else
        changed = (old != copy); /* 一个为 NULL、一个非 NULL 视为变化。 */
    self->m_windowTitle = copy;
    XWidget_freeString(&old); /* 替换旧值后释放原字符串，避免 setter 重复赋值泄漏。 */
    if (!changed) return;
    if (self->m_isWindow && self->m_windowHandle)
        XWindow_setTitle((XWindow*)self->m_windowHandle, self->m_windowTitle);
    XWidget_windowTitleChanged_signal(self, self->m_windowTitle);
#if XWINDOW_ON && XACCESSIBLE_ON
    XPlatformAccessibility_notifyWidget(XAccessibleEvent_NameChanged, self);
#endif
}

const XString* XWidget_windowIconText(const XWidget* self)
{
    return self ? self->m_windowIconText : NULL;
}

void XWidget_setWindowIconText(XWidget* self, const XString* text)
{
    XString* old;
    XString* copy;
    bool changed;
    if (!self) return;
    copy = XWidget_copyString(text);
    old = self->m_windowIconText;
    if (old && copy)
        changed = !XString_equals(old, copy, XChar_CaseSensitive);
    else
        changed = (old != copy); /* 一个为 NULL、一个非 NULL 视为变化。 */
    self->m_windowIconText = copy;
    XWidget_freeString(&old); /* 替换旧值后释放原字符串，避免 setter 重复赋值泄漏。 */
    if (changed)
        XWidget_windowIconTextChanged_signal(self, self->m_windowIconText);
}

XIcon XWidget_windowIcon(const XWidget* self)
{
    XIcon out;
    const XWidget* w;
    XIcon_init(&out);
    if (!self) return out;
    /* 对标 QWidget::windowIcon()：本控件未显式设置图标（图标为空）时沿
       父链向顶层传播解析，命中最近的非空图标；链上全部为空时回落应用
       图标（QGuiApplication::windowIcon），应用也未设置则返回空图标。 */
    w = self;
    while (w && ((const XObject*)w)->is_widget && XIcon_isNull(&w->m_icon))
        w = (const XWidget*)XObject_parent((XObject*)w);
    if (w && ((const XObject*)w)->is_widget && !XIcon_isNull(&w->m_icon)) {
        /* XCopy 对 XIcon 是共享私有数据（refcount 增量）的浅拷贝；调用方
           用 XIcon_deinit_base 释放本副本即可（契约同 XWidget_font）。 */
        XCopy(&out, &w->m_icon);
        return out;
    }
#if XGUIAPPLICATION_ON
    {
        XIcon* appIcon = XGuiApplication_windowIcon();
        if (appIcon) {
            XCopy(&out, appIcon);
            XIcon_delete_base((XClass*)appIcon);
        }
    }
#endif /* XGUIAPPLICATION_ON */
    return out;
}

void XWidget_setWindowIcon(XWidget* self, const XIcon* icon)
{
    XIcon empty;
    int64_t oldKey;
    int64_t newKey;
    if (!self) return;
    oldKey = XIcon_cacheKey(&self->m_icon);
    if (icon) {
        /* 共享式浅拷贝（XIconPrivate refcount 增量），旧引用自动释放。 */
        XCopy(&self->m_icon, icon);
    } else {
        /* setWindowIcon(空图标) 清除图标（对标 setWindowIcon(QIcon())）。 */
        XIcon_init(&empty);
        XCopy(&self->m_icon, &empty);
        XIcon_deinit_base(&empty);
    }
    newKey = XIcon_cacheKey(&self->m_icon);
    /* 对标 QWidget::setWindowIcon_sys：仅顶层控件且桥接窗口已创建时刷新
       平台窗口图标；窗口未创建时仅存值，惰性创建时由 XWidget_createWindow
       携带 m_icon。子控件设置只存自身值，不改顶层平台图标（Qt 6 行为）。 */
    if (self->m_isWindow && self->m_windowHandle)
        XWindow_setIcon((XWindow*)self->m_windowHandle, &self->m_icon);
    /* 图标内容键变化才发射 windowIconChanged（Qt 仅在图标变化时通知）。 */
    if (newKey != oldKey)
        XWidget_windowIconChanged_signal(self, &self->m_icon);
}

const XString* XWidget_windowFilePath(const XWidget* self)
{
    return self ? self->m_windowFilePath : NULL;
}

void XWidget_setWindowFilePath(XWidget* self, const XString* path)
{
    XString* copy;
    if (!self) return;
    copy = XWidget_copyString(path);
    XWidget_freeString(&self->m_windowFilePath);
    self->m_windowFilePath = copy;
    if (self->m_isWindow && self->m_windowHandle)
        XWindow_setFilePath((XWindow*)self->m_windowHandle,
                            self->m_windowFilePath);
}

double XWidget_windowOpacity(const XWidget* self)
{
    return self ? (double)self->m_windowOpacity : 1.0;
}

void XWidget_setWindowOpacity(XWidget* self, double opacity)
{
    float clamped;
    if (!self) return;
    if (opacity < 0.0) opacity = 0.0;
    if (opacity > 1.0) opacity = 1.0;
    clamped = (float)opacity;
    if (self->m_windowOpacity == clamped) return;
    self->m_windowOpacity = clamped;
    if (self->m_isWindow && self->m_windowHandle)
        XWindow_setOpacity((XWindow*)self->m_windowHandle, clamped);
}

bool XWidget_isWindowModified(const XWidget* self)
{
    return self ? XWidget_testAttribute(self, XWidgetAttribute_WindowModified)
                : false;
}

void XWidget_setWindowModified(XWidget* self, bool modified)
{
    if (!self) return;
    XWidget_setAttribute(self, XWidgetAttribute_WindowModified, modified);
}

/* ==================== 可用性与焦点（对标 QWidget） ==================== */

bool XWidget_isEnabled(const XWidget* self)
{
    if (!self) return false;
    /* QWidget::isEnabled() 的实现只观察 WA_Disabled；WA_ForceDisabled
     * 仅用于 setEnabled() 的递归传播筛选，不能单独改变查询结果。 */
    return !XWidget_attrTest(&self->m_attributes, XWidgetAttribute_Disabled);
}

void XWidget_setEnabled(XWidget* self, bool enabled)
{
    bool oldEnabled;
    XEvent event;
    XWidget* parent;
    if (!self) return;
    /* 先读取当前有效状态，再写入显式 ForceDisabled 请求；否则禁用请求
     * 会先改变 isEnabled() 的观察值，导致 Qt 的状态转换被短路。 */
    oldEnabled = XWidget_isEnabled(self);
    /* Qt 先记录显式请求。因此在禁用父控件下调用 setEnabled(true) 时，
     * 会清除 WA_ForceDisabled，但保留继承得到的 WA_Disabled，直到父控件
     * 再次启用后才恢复有效状态。 */
    XWidget_attrSet(&self->m_attributes, XWidgetAttribute_ForceDisabled,
                    !enabled);
    /* QWidget 不允许在禁用的非窗口父控件下显式重新启用子控件；
     * 父控件重新启用时由传播逻辑统一恢复未显式禁用的子控件。 */
    parent = XWidget_parentWidget(self);
    if (enabled && !self->m_isWindow && parent && !XWidget_isEnabled(parent))
        return;
    if (oldEnabled == enabled) return;
    self->m_enabled = enabled ? 1 : 0;
    XWidget_attrSet(&self->m_attributes, XWidgetAttribute_Disabled,
                    !enabled);
    if (!enabled && g_focusWidget == self)
        XWidget_clearFocusBase(self, XFocusReason_Other);
    if (oldEnabled != enabled) {
        XEvent_init(&event, XEVENT_TYPE_ENABLED_CHANGE);
        XWidget_event_base(self, &event);
    }
    XWidget_propagateEnabled(self, enabled);
    XWidget_update(self);
#if XWINDOW_ON && XACCESSIBLE_ON
    XPlatformAccessibility_notifyWidget(XAccessibleEvent_StateChanged, self);
#endif
}

void XWidget_setDisabled(XWidget* self, bool disabled)
{
    XWidget_setEnabled(self, !disabled);
}

bool XWidget_isEnabledTo(const XWidget* self, const XWidget* ancestor)
{
    const XWidget* w;
    if (!self) return false;
    w = self;
    /* 对齐 QWidget::isEnabledTo：只检查显式禁用标志，并在当前窗口
     * 边界停止；传播得到的 WA_Disabled 不应被重复解释。 */
    while (!XWidget_attrTest(&w->m_attributes,
                             XWidgetAttribute_ForceDisabled) &&
           !w->m_isWindow && XObject_parent((XObject*)w) &&
           XObject_parent((XObject*)w) != (const XObject*)ancestor)
        w = (const XWidget*)XObject_parent((XObject*)w);
    return !XWidget_attrTest(&w->m_attributes,
                             XWidgetAttribute_ForceDisabled);
}

XWidgetFocusPolicy XWidget_focusPolicy(const XWidget* self)
{
    return self ? self->m_focusPolicy : XWidgetFocusPolicy_NoFocus;
}

void XWidget_setFocusPolicy(XWidget* self, XWidgetFocusPolicy policy)
{
    if (!self) return;
    self->m_focusPolicy = policy;
}

bool XWidget_hasFocus(const XWidget* self)
{
    XWidget* proxy;
    XWidget* target;
    if (!self) return false;
    proxy = XWidget_deepestFocusProxy(self);
    target = proxy ? proxy : (XWidget*)self;
    return g_focusWidget == target;
}

XWidget* XWidget_focusWidget(const XWidget* self)
{
    XWidget* top;
    if (!self) return NULL;
    top = self->m_isWindow ? (XWidget*)self : XWidget_topLevel(self);
    if (g_focusWidget && XWidget_topLevel(g_focusWidget) == top)
        return g_focusWidget;
    return NULL;
}

/** @brief 返回应用当前持有键盘焦点的控件（全局；对标 QApplication::focusWidget()）。 */
XWidget* XWidget_appFocusWidget(void)
{
    return g_focusWidget;
}

void XWidget_setFocus(XWidget* self)
{
    /* QWidget::setFocus() 内联实现使用 Qt::OtherFocusReason。 */
    XWidget_setFocusReason(self, XFocusReason_Other);
}

void XWidget_setFocusReason(XWidget* self, XFocusReason reason)
{
#if XWINDOWEVENT_ON
    XFocusEvent event;
#endif /* XWINDOWEVENT_ON */
    XWidget* top;
    XWidget* focusTarget;
    if (!self) return;
    if (!self->m_enabled ||
        XWidget_attrTest(&self->m_attributes, XWidgetAttribute_Disabled) ||
        XWidget_attrTest(&self->m_attributes, XWidgetAttribute_ForceDisabled))
        return;
    /* 对标 QWidget::setFocus：焦点真正落在最深层焦点代理上。 */
    focusTarget = XWidget_deepestFocusProxy(self);
    if (!focusTarget) focusTarget = self;
    if (!focusTarget->m_enabled ||
        XWidget_attrTest(&focusTarget->m_attributes, XWidgetAttribute_Disabled) ||
        XWidget_attrTest(&focusTarget->m_attributes, XWidgetAttribute_ForceDisabled))
        return;
    top = self->m_isWindow ? (XWidget*)self : XWidget_topLevel(self);
    if (!top) return;
    if (top->m_windowHandle && !XWindow_isActive((XWindow*)top->m_windowHandle))
        XWindow_requestActivate((XWindow*)top->m_windowHandle);
    if (g_focusWidget == focusTarget) return;
    if (g_focusWidget)
        XWidget_clearFocusBase(g_focusWidget, reason);
    g_focusWidget = focusTarget;
#if XAPPLICATION_ON && XGUIAPPLICATION_ON
    XApplication_setFocusWidget(focusTarget);
#endif /* XAPPLICATION_ON */
#if XWINDOWEVENT_ON
    XFocusEvent_init(&event, XEVENT_TYPE_FOCUS_IN, reason);
    XWidget_event_base(focusTarget, (XEvent*)&event);
    XFocusEvent_deinit_base(&event);
#endif /* XWINDOWEVENT_ON */
}

void XWidget_clearFocus(XWidget* self)
{
    XWidget* focus;
    if (!self || !XWidget_hasFocus(self)) return;
    focus = XWidget_focusWidget(self);
    if (focus)
        XWidget_clearFocusBase(focus, XFocusReason_Other);
}

XWidget* XWidget_focusProxy(const XWidget* self)
{
    return self ? self->m_focusProxy : NULL;
}

void XWidget_setFocusProxy(XWidget* self, XWidget* proxy)
{
    XWidget* cur;
    bool hadFocus;
    uint32_t guard;
    if (!self) return;
    hadFocus = XWidget_hasFocus(self);
    if (proxy) {
        /* 拒绝形成焦点代理环（对标 QWidget::setFocusProxy 的链检查）。 */
        guard = 0;
        for (cur = proxy; cur && guard < 64u; cur = cur->m_focusProxy, ++guard) {
            if (cur == self)
                return;
        }
    }
    XFocusProxy_unregisterOwner(self);
    self->m_focusProxy = proxy;
    if (proxy)
        XFocusProxy_register(self);
    /* Qt 在“焦点控件已经是本控件（或其代理）”时会把焦点移到新代理。 */
    if (hadFocus)
        XWidget_setFocus(self);
}

void XWidget_setTabOrder(XWidget* first, XWidget* second)
{
    XWidget* firstTop;
    XWidget* secondTop;
    if (!first || !second || first == second) return;
    /* 对标 QWidget::setTabOrder：不接受 NoFocus 控件，且两控件必须同窗。 */
    if (first->m_focusPolicy == XWidgetFocusPolicy_NoFocus ||
        second->m_focusPolicy == XWidgetFocusPolicy_NoFocus)
        return;
    firstTop = XWidget_topLevel(first);
    secondTop = XWidget_topLevel(second);
    if (!firstTop || !secondTop || firstTop != secondTop) return;
    /* C 适配只维护显式 next/prev 单跳链接；未设置的控件仍按文档序导航。 */
    first->m_focusNext = second;
    second->m_focusPrev = first;
}

/** @brief 判断控件是否可作为 Tab 链中的显式/文档序焦点候选（与收集规则一致）。 */
static bool XWidget_focusChainCandidate(const XWidget* self)
{
    const XObject* parent;
    /* 对标 Qt 焦点遍历按生效可见性过滤（qwidget.cpp focusNextPrevChild
     * 的候选走 isVisible()）：StackOne 隐藏页的子控件虽 explicitShow 但
     * 生效不可见，不得成为候选——否则 Tab 落到不可见控件上（复扫-3
     * #40 根因：页3 Tab 落到隐藏「按钮演示」页的 m_button）。 */
    if (!(self && self->m_enabled &&
          (self->m_focusPolicy & XWidgetFocusPolicy_TabFocus) != 0 &&
          self->m_visible && !self->m_isWindow))
        return false;
    /* 复合控件候选资格二选一，停靠点归容器（对标 Qt 单控件语义：
     * QAbstractSpinBoxPrivate::init 的 d->edit->setFocusProxy(q)
     * （qabstractspinbox.cpp:691）令内嵌行编辑与容器合成单停靠点，
     * qapplication.cpp:1996-1998 的 composites 过滤保证 Tab 不在父子
     * 间打转）。XGui 复合容器（XAbstractSpinBox 等）尚未调用
     * XWidget_setFocusProxy，此处在收集端等效收口：直接父控件自身为
     * Tab 候选时子控件不重复入链——XLineEdit_init 补 StrongFocus（复
     * 扫-5 #40）后，页3 SpinBox 的内嵌编辑框不再与容器双停靠，链序
     * [LE,SpinBox,Slider,nav0..nav8] 维持（复扫-5 路0 项4）；NoFocus
     * 容器（页面/groupBox/XChartView）下的独立控件不受影响。 */
    parent = XObject_parent((XObject*)self);
    if (parent && parent->is_widget) {
        const XWidget* pw = (const XWidget*)parent;
        if (pw->m_enabled &&
            (pw->m_focusPolicy & XWidgetFocusPolicy_TabFocus) != 0 &&
            pw->m_visible && !pw->m_isWindow)
            return false;
    }
    return true;
}

/** @brief 深度优先收集可 Tab 聚焦子控件（不含顶层自身；顺序即绘制顺序）。 */

static void XWidget_collectTabFocusable(const XWidget* self, XVector* out)
{
    const XVector* children;
    size_t n;
    size_t i;
    if (!self || !out) return;
    if (XWidget_focusChainCandidate(self)) {
        XWidget* w = (XWidget*)self;
        XVector_push_back_1_base(out, &w);
    }
    children = XObject_children((XObject*)self);
    n = children ? XVector_size_base((const XContainer*)children) : 0;
    for (i = 0; i < n; ++i) {
        XObject* child = *(XObject**)XVector_at_base(children, (int64_t)i);
        if (child && child->is_widget)
            XWidget_collectTabFocusable((const XWidget*)child, out);
    }
}

/** @brief w 是否位于 root 子树内（沿父链上溯判定）。 */
static bool XWidget_focusInSubtree(const XWidget* root, const XWidget* w)
{
    const XObject* cur;
    if (!root || !w) return false;
    for (cur = (const XObject*)w; cur;
         cur = XObject_parent((XObject*)cur)) {
        if (cur == (const XObject*)root)
            return true;
    }
    return false;
}

/** @brief 焦点链候选收集根：模态围栏（对标 Qt 模态期间的候选裁剪）。
 * @details Qt 中模态对话框是独立原生顶层（QApplication::activeModalWidget
 *          / QGuiApplicationPrivate::isWindowBlocked），focusNextPrevChild
 *          以所在顶层为收集界，候选天然不越出对话框。本框架子控件形态
 *          对话框（XFileDialog/XInputDialog/QColorDialog，flags 无 Window
 *          位）与背景页同住主窗顶层——窗口级遍历从主窗顶层收集候选，
 *          Tab 链把背景页签一并收进来：模态 exec 期间 Tab×N 焦点逃逸出
 *          对话框落到背景页签上（stab2_tab10），Esc/Return 随焦点丢失而
 *          失效（键盘关闭通道丢失）。围栏：应用模态控件在册且其顶层就是
 *          本次遍历的顶层时，候选集收缩到模态控件子树内；模态宿主窗以
 *          外的顶层（如组合框 Popup 弹层，模态门既有豁免口径）不裁剪，
 *          弹层自身 Tab 行为不变；无模态时返回 top，与既有行为逐项一致。 */
static XWidget* XWidget_focusChainRoot(XWidget* top)
{
    XWidget* modal = XWidget_applicationModalWidget();
    XWidget* modalTop;
    if (!modal || !top)
        return top;
    modalTop = modal->m_isWindow ? modal : XWidget_topLevel(modal);
    if (modalTop != top)
        return top;
    return modal;
}

/** @brief 计算焦点链下一个/上一个目标（不改焦点；优先显式 Tab 链，其次文档序）。 */
static XWidget* XWidget_focusChainTarget(XWidget* self, bool forward)
{
    XWidget* top;
    XWidget* root;
    XWidget* linked;
    XVector* list;
    size_t n;
    size_t i;
    size_t cur;
    XWidget* target;
    if (!self) return NULL;
    top = self->m_isWindow ? (XWidget*)self : XWidget_topLevel(self);
    if (!top) return NULL;
    /* 模态围栏：候选收集与显式链资格都以 root 为界（top=无模态时的退化）。 */
    root = XWidget_focusChainRoot(top);
    /* 1. 显式 setTabOrder 链：同窗、仍在围栏子树内且可聚焦候选时优先。 */
    linked = forward ? self->m_focusNext : self->m_focusPrev;
    if (linked && linked != self &&
        XWidget_topLevel(linked) == top &&
        (root == top || XWidget_focusInSubtree(root, linked)) &&
        XWidget_focusChainCandidate(linked))
        return linked;
    /* 2. 未设置显式链（或显式链失效）时退回文档顺序。起点控件在围栏外
     *    （背景页残留焦点收到 Tab）时不入候选集，走下方退化起点直接落进
     *    围栏子树首个候选——焦点被拉回对话框，等价 Qt 模态期间遍历不出
     *    activeModalWidget 的语义。 */
    list = XVector_create(sizeof(XWidget*));
    if (!list) return NULL;
    XWidget_collectTabFocusable((const XWidget*)root, list);
    n = XVector_size_base((const XContainer*)list);
    if (n == 0) {
        XVector_delete_base((XClass*)list);
        return NULL;
    }
    cur = n; /* 未找到当前控件时从头/尾开始。 */
    for (i = 0; i < n; ++i) {
        if (XVector_At_Base(list, (int64_t)i, XWidget*) == self) {
            cur = i;
            break;
        }
    }
    if (cur == n) {
        /* 对标 Qt 焦点锚定：接收 Tab 的控件自身不是候选（如行编辑的
         * 内嵌编辑器）时，以最近的可聚焦祖先为锚续链——此前直接从头
         * 取候选，Tab 从文本控件出发会落回文档序首控件（复扫-3 #40），
         * 焦点链在文本控件处断岛。 */
        XWidget* anc = XWidget_parentWidget(self);
        while (anc && cur == n) {
            for (i = 0; i < n; ++i) {
                if (XVector_At_Base(list, (int64_t)i, XWidget*) == anc) {
                    cur = i;
                    break;
                }
            }
            if (cur == n)
                anc = XWidget_parentWidget(anc);
        }
    }
    target = NULL;
    for (i = 0; i < n; ++i) {
        size_t idx;
        /* 当前接收控件不在候选集（如 NoFocus 顶层接收首个 Tab）时的
         * 退化起点：forward 从 0、backward 从 n-1（对标 Qt
         * focusNextPrevChild_helper 的 f=toplevel 回退：从起点控件沿链
         * 推进，首个候选即目标）。此前 (cur+1+i)%n 在 cur=n 时从 1
         * 起跳，文档序首个可聚焦控件被跳过。 */
        if (cur == n)
            idx = forward ? i : (n - 1 - i);
        else
            idx = forward ? (cur + 1 + i) % n : (cur + n - 1 - i) % n;
        {
            XWidget* candidate = XVector_At_Base(list, (int64_t)idx, XWidget*);
            if (candidate == self) continue;
            target = candidate;
            break;
        }
    }
    XVector_delete_base((XClass*)list);
    return target;
}

/** @brief 焦点前进/后退公共实现：优先显式 Tab 链，其次按文档序取候选。 */
static bool XWidget_focusStep(XWidget* self, bool forward)
{
    XWidget* target = XWidget_focusChainTarget(self, forward);
    if (target)
        XWidget_setFocus(target);
    return target != NULL;
}

/** @brief 窗口级 Tab 焦点遍历入口（对标 QWidget::focusNextPrevChild，
 *         qwidget.cpp:6816）。
 * @details Qt 语义：子控件把遍历决定权沿父链上交所在顶层窗口——"only
 *          the window that contains the child widgets decides where to
 *          redirect focus"；窗口级以当前焦点控件为起点、无焦点控件时
 *          回退以顶层自身为起点（focusNextPrevChild_helper：
 *          f = toplevel->focusWidget() 不到时 f = toplevel），因此初始
 *          无任何子控件持有焦点时首个 Tab 也能落入文档序首个候选。
 *          此前 Tab 遍历以「接收键事件的控件自身 focusPolicy & TabFocus」
 *          为门槛（问题 #3：初始无焦点且顶层为 NoFocus，遍历永不启动）。
 *          文件内静态实现：公共头无此入口，XWidget.h 不在本批改动面。 */
static bool xwidget_focusNextPrevChild(XWidget* self, bool next)
{
    XWidget* top;
    if (!self) return false;
    if (!self->m_isWindow) {
        top = XWidget_topLevel(self);
        if (top && top != self)
            return xwidget_focusNextPrevChild(top, next);
    }
    /* 优先从当前焦点控件推进；g_focusWidget 不在本窗口时以顶层为起点。 */
    if (g_focusWidget && XWidget_topLevel(g_focusWidget) == self)
        return XWidget_focusStep(g_focusWidget, next);
    return XWidget_focusStep(self, next);
}

XWidget* XWidget_nextInFocusChain(const XWidget* self)
{
    return XWidget_focusChainTarget((XWidget*)self, true);
}

XWidget* XWidget_previousInFocusChain(const XWidget* self)
{
    return XWidget_focusChainTarget((XWidget*)self, false);
}

bool XWidget_focusNextChild(XWidget* self)
{
    return XWidget_focusStep(self, true);
}

bool XWidget_focusPreviousChild(XWidget* self)
{
    return XWidget_focusStep(self, false);
}

/* ==================== 跟踪/拖放/菜单/方向（对标 QWidget） ==================== */

bool XWidget_hasMouseTracking(const XWidget* self)
{
    return self ? (self->m_mouseTracking ||
                   XWidget_attrTest(&self->m_attributes,
                                    XWidgetAttribute_MouseTracking)) : false;
}

bool XWidget_underMouse(const XWidget* self)
{
    return self ? XWidget_attrTest(&self->m_attributes,
                                   XWidgetAttribute_UnderMouse) : false;
}

void XWidget_setMouseTracking(XWidget* self, bool enable)
{
    if (!self) return;
    self->m_mouseTracking = enable ? 1 : 0;
    XWidget_attrSet(&self->m_attributes, XWidgetAttribute_MouseTracking, enable);
}

bool XWidget_hasTabletTracking(const XWidget* self)
{
    return self ? (self->m_tabletTracking != 0) : false;
}

void XWidget_setTabletTracking(XWidget* self, bool enable)
{
    if (!self) return;
    self->m_tabletTracking = enable ? 1 : 0;
}

bool XWidget_acceptDrops(const XWidget* self)
{
    /* 对标 Qt 6.8：acceptDrops()==testAttribute(WA_AcceptDrops)，
     * 与属性位同源（XWidget_setAttribute 同步便捷字段）。 */
    return XWidget_testAttribute(self, XWidgetAttribute_AcceptDrops);
}

void XWidget_setAcceptDrops(XWidget* self, bool enable)
{
    /* 对标 Qt 6.8（qwidget.cpp：setAcceptDrops 即
     * setAttribute(WA_AcceptDrops)）：统一走属性位入口，
     * 经 XWidget_setAttribute 同步 m_acceptDrops 便捷字段。 */
    XWidget_setAttribute(self, XWidgetAttribute_AcceptDrops, enable);
}

void XWidget_setApplicationModalWidget(XWidget* widget)
{
    g_applicationModalWidget = widget;
}

XWidget* XWidget_applicationModalWidget(void)
{
    return g_applicationModalWidget;
}

void XWidget_grabMouse(XWidget* self)
{
    XWidget* top;
    if (!self || !self->m_visible) return;
    top = XWidget_topLevel(self);
    if (!top) return;
    g_mouseGrabWidget = self;
}

void XWidget_releaseMouse(XWidget* self)
{
    if (self && g_mouseGrabWidget == self)
        g_mouseGrabWidget = NULL;
}

XWidget* XWidget_mouseGrabber(void)
{
    return g_mouseGrabWidget;
}

void XWidget_setTouchMouseSynthesisEnabled(bool on)
{
    /* 对标 Qt AA_SynthesizeMouseForUnhandledTouchEvents（Qt 6 默认开启）：
       框架级开关，切换只影响其后开始的触摸序列，正在进行的序列不受影响。 */
    g_touchMouseSynthEnabled = on;
}

bool XWidget_touchMouseSynthesisEnabled(void)
{
    return g_touchMouseSynthEnabled;
}

void XWidget_grabKeyboard(XWidget* self)
{
    if (!self || !self->m_visible) return;
    g_keyboardGrabWidget = self;
}

void XWidget_releaseKeyboard(XWidget* self)
{
    if (self && g_keyboardGrabWidget == self)
        g_keyboardGrabWidget = NULL;
}

XWidget* XWidget_keyboardGrabber(void)
{
    return g_keyboardGrabWidget;
}

XWidgetContextMenuPolicy XWidget_contextMenuPolicy(const XWidget* self)
{
    return self ? (XWidgetContextMenuPolicy)self->m_contextMenuPolicy
                : XWidgetContextMenuPolicy_NoContextMenu;
}

void XWidget_setContextMenuPolicy(XWidget* self,
                                  XWidgetContextMenuPolicy policy)
{
    if (!self) return;
    self->m_contextMenuPolicy = (uint32_t)policy;
}

/** @brief 向未显式指定方向的非窗口子控件传播布局方向。 */
static void XWidget_propagateLayoutDirection(XWidget* self,
                                             XWidgetLayoutDirection direction)
{
    const XVector* children;
    size_t n;
    size_t i;
    if (!self) return;
    children = XObject_children((XObject*)self);
    n = children ? XVector_size_base((const XContainer*)children) : 0;
    for (i = 0; i < n; ++i) {
        XObject* child = *(XObject**)XVector_at_base(children, (int64_t)i);
        XWidget* widget;
        XEvent event;
        if (!child || !child->is_widget) continue;
        widget = (XWidget*)child;
        if (widget->m_isWindow ||
            XWidget_attrTest(&widget->m_attributes,
                             XWidgetAttribute_SetLayoutDirection))
            continue;
        if (widget->m_layoutDirection != (uint32_t)direction) {
            widget->m_layoutDirection = (uint32_t)direction;
            XWidget_attrSet(&widget->m_attributes,
                            XWidgetAttribute_RightToLeft,
                            direction == XWidgetLayoutDirection_RightToLeft);
            XEvent_init(&event, XEVENT_TYPE_LAYOUT_DIRECTION_CHANGE);
            XWidget_event_base(widget, &event);
            XWidget_update(widget);
        }
        XWidget_propagateLayoutDirection(widget, direction);
    }
}

XLayout* XWidget_layout(const XWidget* self)
{
#if XLAYOUT_ON
    return self ? self->m_layout : NULL;
#else
    (void)self;
    return NULL;
#endif /* XLAYOUT_ON */
}

void XWidget_setLayout(XWidget* self, XLayout* layout)
{
#if XLAYOUT_ON
    if (!self) return;
    if (self->m_layout) {
        XLayout_detachWidget(self->m_layout);
        self->m_layout = NULL;
    }
    if (layout) {
        self->m_layout = layout;
        XLayout_attachWidget(layout, self);
    }
#else
    (void)self;
    (void)layout;
#endif /* XLAYOUT_ON */
}

XWidgetLayoutDirection XWidget_layoutDirection(const XWidget* self)
{
    return self ? (XWidgetLayoutDirection)self->m_layoutDirection
                : XWidgetLayoutDirection_LeftToRight;
}

void XWidget_setLayoutDirection(XWidget* self, XWidgetLayoutDirection direction)
{
    bool changed;
    XEvent event;
    if (!self) return;
    changed = self->m_layoutDirection != (uint32_t)direction;
    self->m_layoutDirection = (uint32_t)direction;
    XWidget_attrSet(&self->m_attributes, XWidgetAttribute_SetLayoutDirection,
                    true);
    XWidget_attrSet(&self->m_attributes, XWidgetAttribute_RightToLeft,
                    direction == XWidgetLayoutDirection_RightToLeft);
    if (changed) {
        XEvent_init(&event, XEVENT_TYPE_LAYOUT_DIRECTION_CHANGE);
        XWidget_event_base(self, &event);
    }
    XWidget_propagateLayoutDirection(self, direction);
    XWidget_update(self);
}

void XWidget_unsetLayoutDirection(XWidget* self)
{
    XWidgetLayoutDirection direction;
    XWidget* parent;
    bool changed;
    XEvent event;
    if (!self) return;
    XWidget_attrSet(&self->m_attributes, XWidgetAttribute_SetLayoutDirection,
                    false);
    /* 优先继承父控件方向；无父控件时按 Qt 默认 LTR。 */
    parent = XWidget_parentWidget(self);
    direction = parent ? (XWidgetLayoutDirection)parent->m_layoutDirection
                       : XWidgetLayoutDirection_LeftToRight;
    changed = self->m_layoutDirection != (uint32_t)direction;
    self->m_layoutDirection = (uint32_t)direction;
    XWidget_attrSet(&self->m_attributes, XWidgetAttribute_RightToLeft,
                    direction == XWidgetLayoutDirection_RightToLeft);
    if (changed) {
        XEvent_init(&event, XEVENT_TYPE_LAYOUT_DIRECTION_CHANGE);
        XWidget_event_base(self, &event);
    }
    XWidget_propagateLayoutDirection(self, direction);
    XWidget_update(self);
}

bool XWidget_isRightToLeft(const XWidget* self)
{
    return self && self->m_layoutDirection ==
                       (uint32_t)XWidgetLayoutDirection_RightToLeft;
}

bool XWidget_isLeftToRight(const XWidget* self)
{
    return !XWidget_isRightToLeft(self);
}

/* ==================== 光标/提示/调色板（对标 QWidget） ==================== */

XCursor XWidget_cursor(const XWidget* self)
{
#if XCURSOR_ON
    XCursor out;
    XCursor_init(&out); /* 默认 ArrowCursor；与 Qt 未设置时语义一致。 */
    if (self && self->m_cursor)
        XCopy(&out, self->m_cursor);
    return out;
#else
    XCursor out;
    XMemset(&out, 0, sizeof(out));
    (void)self;
    return out;
#endif /* XCURSOR_ON */
}

void XWidget_setCursor(XWidget* self, const XCursor* cursor)
{
#if XCURSOR_ON
    XWidget* top;
    if (!self || !cursor) return;
    if (!self->m_cursor) {
        self->m_cursor = XCursor_create_ex(XCLASS_DEFAULT_MEMORY_TYPE);
        if (!self->m_cursor) return;
    }
    XCopy(self->m_cursor, cursor);
    XWidget_attrSet(&self->m_attributes, XWidgetAttribute_SetCursor, true);
    top = self->m_isWindow ? (XWidget*)self : XWidget_topLevel(self);
    if (top && top->m_windowHandle) {
        XWindow* window = (XWindow*)top->m_windowHandle;
        XWindow_setCursor(window, self->m_cursor);
        /* 平台生效路径：形状/自定义光标映射为原生光标并 XDefineCursor
           到窗口（XCursor 平台后端；无后端/窗口未映射时静默，存储已
           完成）。形状变化时重复调用即更新生效光标。 */
        (void)XCursor_applyToWindow((uintptr_t)XWindow_winId(window),
                                    self->m_cursor);
    }
#else
    (void)self;
    (void)cursor;
#endif /* XCURSOR_ON */
}

void XWidget_unsetCursor(XWidget* self)
{
#if XCURSOR_ON
    XWidget* top;
    if (!self) return;
    if (self->m_cursor) {
        XCursor_delete_base((XClass*)self->m_cursor);
        self->m_cursor = NULL;
    }
    XWidget_attrSet(&self->m_attributes, XWidgetAttribute_SetCursor, false);
    top = self->m_isWindow ? (XWidget*)self : XWidget_topLevel(self);
    if (top && top->m_windowHandle) {
        XWindow* window = (XWindow*)top->m_windowHandle;
        XWindow_unsetCursor(window);
        /* 平台清除路径：XUndefineCursor 恢复窗口默认光标（静默语义同上）。 */
        (void)XCursor_clearForWindow((uintptr_t)XWindow_winId(window));
    }
#else
    (void)self;
#endif /* XCURSOR_ON */
}

const XString* XWidget_toolTip(const XWidget* self)
{
    return self ? self->m_toolTip : NULL;
}

void XWidget_setToolTip(XWidget* self, const XString* tip)
{
    XString* copy;
    if (!self) return;
    copy = XWidget_copyString(tip);
    XWidget_freeString(&self->m_toolTip);
    self->m_toolTip = copy;
#if XWINDOW_ON && XACCESSIBLE_ON
    XPlatformAccessibility_notifyWidget(XAccessibleEvent_DescriptionChanged, self);
#endif
}

int XWidget_toolTipDuration(const XWidget* self)
{
    return self ? self->m_toolTipDuration : 0;
}

void XWidget_setToolTipDuration(XWidget* self, int msec)
{
    if (!self) return;
    self->m_toolTipDuration = msec;
}

/* ==================== 状态提示/What's This/无障碍/窗口角色/输入法/样式表 ==== */

const XString* XWidget_statusTip(const XWidget* self)
{
    return self ? self->m_statusTip : NULL;
}

void XWidget_setStatusTip(XWidget* self, const XString* tip)
{
    XString* copy;
    if (!self) return;
    copy = XWidget_copyString(tip);
    XWidget_freeString(&self->m_statusTip);
    self->m_statusTip = copy;
}

const XString* XWidget_whatsThis(const XWidget* self)
{
    return self ? self->m_whatsThis : NULL;
}

void XWidget_setWhatsThis(XWidget* self, const XString* text)
{
    XString* copy;
    if (!self) return;
    copy = XWidget_copyString(text);
    XWidget_freeString(&self->m_whatsThis);
    self->m_whatsThis = copy;
}

const XString* XWidget_accessibleName(const XWidget* self)
{
    return self ? self->m_accessibleName : NULL;
}

void XWidget_setAccessibleName(XWidget* self, const XString* name)
{
    XString* copy;
    if (!self) return;
    copy = XWidget_copyString(name);
    XWidget_freeString(&self->m_accessibleName);
    self->m_accessibleName = copy;
#if XWINDOW_ON && XACCESSIBLE_ON
    XPlatformAccessibility_notifyWidget(XAccessibleEvent_NameChanged, self);
#endif
}

const XString* XWidget_accessibleDescription(const XWidget* self)
{
    return self ? self->m_accessibleDescription : NULL;
}

void XWidget_setAccessibleDescription(XWidget* self, const XString* description)
{
    XString* copy;
    if (!self) return;
    copy = XWidget_copyString(description);
    XWidget_freeString(&self->m_accessibleDescription);
    self->m_accessibleDescription = copy;
#if XWINDOW_ON && XACCESSIBLE_ON
    XPlatformAccessibility_notifyWidget(XAccessibleEvent_DescriptionChanged, self);
#endif
}

const XString* XWidget_windowRole(const XWidget* self)
{
    return self ? self->m_windowRole : NULL;
}

void XWidget_setWindowRole(XWidget* self, const XString* role)
{
    XString* copy;
    if (!self) return;
    copy = XWidget_copyString(role);
    XWidget_freeString(&self->m_windowRole);
    self->m_windowRole = copy;
}

XInputMethodHints XWidget_inputMethodHints(const XWidget* self)
{
    return self ? self->m_inputMethodHints : 0;
}

void XWidget_setInputMethodHints(XWidget* self, XInputMethodHints hints)
{
    if (!self) return;
    self->m_inputMethodHints = hints;
}

#if XINPUTMETHOD_ON
XVariant* XWidget_inputMethodQuery(const XWidget* self, XInputMethodQuery query)
{
    if (!self) return NULL;
    /* 对标 Qt：QWidget::inputMethodQuery 为虚函数。这里经对象虚表分派到
       EXWidget_InputMethodQuery 槽位（控件子类可重载），未绑定虚表或槽位
       为空时回落基类默认实现；查询本身不修改控件状态。 */
    if (((const XClass*)self)->m_vtable) {
        XWidgetInputMethodQuerySlot slot = XClassGetVirtualFunc(
            (XWidget*)self, EXWidget_InputMethodQuery, XWidgetInputMethodQuerySlot);
        if (slot) return slot(self, query);
    }
    return XWidget_inputMethodQuery_default(self, query);
}
#endif /* XINPUTMETHOD_ON */

const XString* XWidget_styleSheet(const XWidget* self)
{
    return self ? self->m_styleSheet : NULL;
}

void XWidget_setStyleSheet(XWidget* self, const XString* styleSheet)
{
    XString* copy;
    if (!self) return;
    copy = XWidget_copyString(styleSheet);
    XWidget_freeString(&self->m_styleSheet);
    self->m_styleSheet = copy;
}

XFont XWidget_font(const XWidget* self)
{
    /* 深拷贝语义（Phase 3.2 裁定）：XFont 值拷贝共享 XString 指针且
       无引用计数，浅拷贝会在任一持有方 deinit 后留下悬空指针（UAF）。
       返回独立副本，调用方使用后必须 XFont_deinit_base。 */
    XFont out;
    XFont_init(&out);
    if (!self)
        return out;
    XCopy(&out, &self->m_font);
    return out;
}

void XWidget_setFont(XWidget* self, const XFont* font)
{
    XFont temp;
    if (!self || !font) return;
    XFont_init(&temp);
    XCopy(&temp, font);
    XMove(&self->m_font, &temp);
    XWidget_updateGeometry(self);
    XWidget_update(self);
}

XFont XWidget_fontMetrics(const XWidget* self)
{
    /* 对标 QWidget::fontMetrics：Qt 返回以 widget->font() 构造的
       QFontMetrics 只读度量对象；本仓库未建立度量类，XPainter_textWidth/
       XPainter_textHeight 等测量接口直接以 XFont 为入参，故按值返回
       字体拷贝作为测量凭据，调用方将其传入上述测量函数完成度量。
       控件未显式 setFont 时即为 XWidget_font 的默认构造字体结果；
       深拷贝契约与 XWidget_font 一致（使用完毕必须 XFont_deinit_base）。 */
    return XWidget_font(self);
}

XFont XWidget_fontInfo(const XWidget* self)
{
    /* 对标 QWidget::fontInfo：Qt 返回以 widget->font() 交由字体子系统
       解析后的 QFontInfo（反映实际匹配到的家族/样式）；本仓库未建立
       字体替换/匹配引擎，XFont 即光栅化最终使用的字体描述，解析结果
       与控件字体一致，故按 XWidget_fontMetrics 相同的 XFont 值拷贝
       方案返回。深拷贝契约与 XWidget_font 一致（使用完毕必须
       XFont_deinit_base）。 */
    return XWidget_font(self);
}

XPalette XWidget_palette(const XWidget* self)
{
    XPalette out;
#if !XPALETTE_ON
    (void)self;
    out.m_disabled = 0;
    return out;
#else
    if (!self) {
        XPalette_init_default(&out);
        return out;
    }
    if (self->m_paletteSet) {
        XPalette_init_default(&out);
        XPalette_copy(&out, &self->m_palette);
    }
#if XGUIAPPLICATION_ON
    else {
        out = XGuiApplication_palette();
    }
#else
    else {
        XPalette_init_default(&out);
    }
#endif /* XGUIAPPLICATION_ON */
    return out;
#endif /* XPALETTE_ON */
}

void XWidget_setPalette(XWidget* self, const XPalette* palette)
{
#if !XPALETTE_ON
    (void)self; (void)palette;
    return;
#else
    if (!self || !palette) return;
    if (!self->m_paletteSet) {
        XPalette_init_default(&self->m_palette);
        self->m_paletteSet = 1;
    }
    XPalette_copy(&self->m_palette, palette);
    XWidget_update(self);
#endif /* XPALETTE_ON */
}

XPaletteColorRole XWidget_backgroundRole(const XWidget* self)
{
    return self ? self->m_backgroundRole : XPaletteColorRole_Window;
}

void XWidget_setBackgroundRole(XWidget* self, XPaletteColorRole role)
{
    if (!self) return;
    if (self->m_backgroundRole == role) return;
    self->m_backgroundRole = role;
    XWidget_update(self);
}

XPaletteColorRole XWidget_foregroundRole(const XWidget* self)
{
    return self ? self->m_foregroundRole : XPaletteColorRole_NoRole;
}

void XWidget_setForegroundRole(XWidget* self, XPaletteColorRole role)
{
    if (!self) return;
    if (self->m_foregroundRole == role) return;
    self->m_foregroundRole = role;
    XWidget_update(self);
}

/* ==================== 更新使能与背景（对标 QWidget） ==================== */

bool XWidget_updatesEnabled(const XWidget* self)
{
    return self ? (self->m_updatesEnabled != 0) : false;
}

void XWidget_setUpdatesEnabled(XWidget* self, bool enable)
{
    if (!self || self->m_updatesEnabled == (enable ? 1 : 0)) return;
    self->m_updatesEnabled = enable ? 1 : 0;
    XWidget_attrSet(&self->m_attributes, XWidgetAttribute_UpdatesDisabled,
                    !enable);
    if (enable)
        XWidget_update(self);
}

bool XWidget_autoFillBackground(const XWidget* self)
{
    return self ? (self->m_autoFillBackground != 0) : false;
}

void XWidget_setAutoFillBackground(XWidget* self, bool enable)
{
    if (!self) return;
    self->m_autoFillBackground = enable ? 1 : 0;
    XWidget_update(self);
}

/* ==================== 绘制闭环（对标 QWidget update/repaint） ==================== */

void XWidget_update(XWidget* self)
{
    XRect rect;
    if (!self) return;
    rect = self->m_contentsRect;
    XWidget_updateRect(self, &rect);
}

void XWidget_updateRect(XWidget* self, const XRect* rect)
{
    if (!self || !rect) return;
    XWidget_addDirty(self, rect);
}

void XWidget_updateRegion(XWidget* self, const XRegion* region)
{
    if (!self || !region) return;
    XWidget_addDirtyRegion(self, region);
}

void XWidget_repaint(XWidget* self)
{
    XRect rect;
    if (!self) return;
    rect = self->m_contentsRect;
    XWidget_repaintRect(self, &rect);
}

void XWidget_repaintRect(XWidget* self, const XRect* rect)
{
    XRegion region;
    XRect value;
    if (!rect) return;
    value = *rect;
    /* repaintRegion 只读取区域；使用栈上单矩形视图，避免同步重绘也
       产生一次短命的矩形数组分配。 */
    region.rects = &value;
    region.count = XRect_isEmpty(&value) ? 0 : 1;
    region.capacity = 1;
    XWidget_repaintRegion(self, &region);
}

void XWidget_repaintRegion(XWidget* self, const XRegion* region)
{
    XWidget* top;
    if (!self || !region) return;
    XWidget_addDirtyRegion(self, region);
    top = XWidget_topLevel(self);
    if (!top) return;
    /* 无原生窗口的控件树同样需要满足 repaint() 的同步 paintEvent
       语义。此时没有可提交的后备存储，直接在本地坐标递归分派即可。 */
    if (!top->m_isWindow) {
        XWidget_paintTree(self, region);
        return;
    }
    if (top->m_dirty.count <= 0) return;
    XWidget_flushBackingStore(top, &top->m_dirty);
}

XRegion XWidget_visibleRegion(const XWidget* self)
{
    XRegion out;
    XRegion next;
    XRegion maskClip;
    const XWidget* node;
    XPoint offset;
    XRegion_init(&out);
    XRegion_init(&next);
    XRegion_init(&maskClip);
    if (!self) {
        XRegion_deinit(&next);
        XRegion_deinit(&maskClip);
        return out;
    }
    XRegion_addRect(&out, &self->m_contentsRect);
    if (self->m_mask.count > 0 && self->m_mask.rects) {
        XRegion_intersectInto(&out, &self->m_mask, &maskClip);
        {
            XRegion temp = out;
            out = maskClip;
            maskClip = temp;
        }
    }
    XPoint_init(&offset, 0, 0);
    node = (const XWidget*)XObject_parent((XObject*)self);
    if (node) {
        XRegion_translateInline(&out, self->m_windowRect.x, self->m_windowRect.y);
        offset.x += self->m_windowRect.x;
        offset.y += self->m_windowRect.y;
    }
    while (node) {
        XRegion_intersectRectInto(&out, &node->m_contentsRect, &next);
        {
            XRegion temp = out;
            out = next;
            next = temp;
        }
        if (node->m_mask.count > 0 && node->m_mask.rects) {
            XRegion_intersectInto(&out, &node->m_mask, &maskClip);
            {
                XRegion temp = out;
                out = maskClip;
                maskClip = temp;
            }
        }
        if (node->m_isWindow)
            break;
        XRegion_translateInline(&out, node->m_windowRect.x, node->m_windowRect.y);
        offset.x += node->m_windowRect.x;
        offset.y += node->m_windowRect.y;
        node = (const XWidget*)XObject_parent((XObject*)node);
    }
    XRegion_translateInline(&out, -offset.x, -offset.y);
    XRegion_deinit(&next);
    XRegion_deinit(&maskClip);
    return out;
}

void XWidget_scroll(XWidget* self, int dx, int dy)
{
    XRect view;       /* 滚动漫裁剪（控件局部，对标 Qt 的 q->rect()）。 */
    XRect dest;       /* 平移后的裁剪区域：接收滚动内容的目标带。 */
    XRect keep;       /* 视口随内容平移后仍留在视口内的矩形。 */
    XRegion full;     /* 视口区域（求差集的被减数）。 */
    XRegion kept;     /* 存留区域（差集的减数）。 */
    XRegion exposed;  /* 滚动露出、必须重绘的区域。 */
    XRegion dirty;    /* 最终并入脏区的区域：目标带 ∪ 露出带。 */
    int i;

    /* 对标 Qt 前置守卫：(!updatesEnabled && 无子控件) || !isVisible 时
       直接返回；简化为更新禁用或不可见即返回（区域调度本就依赖更新
       使能，Qt 的子控件例外来自 scrollChildren 仍需执行）。 */
    if (!self || !self->m_updatesEnabled || !XWidget_isVisible(self))
        return;
    if (dx == 0 && dy == 0) return;
    view = self->m_contentsRect;
    if (XRect_isEmpty(&view)) return;
    /* 对标 QWidgetPrivate::scroll_sys：Qt 经平台后备存储做像素 blit，
       平移脏区并只重绘露出带；本适配按任务裁定简化为"平移裁剪区域 +
       调度重绘"——XGui 绘制闭环由 paintEvent 从头重建、无像素 blit
       通道，故把目标带与露出带一并并入脏区，由下一次 PAINT 事件重绘。
       子控件不平移（Qt scrollChildren 语义未提供，见头文件 @note）。 */
    dest = XRect_translated(&view, dx, dy);
    dest = XRect_intersected(&dest, &view);
    keep = XRect_translated(&view, -dx, -dy);
    keep = XRect_intersected(&keep, &view);
    XRegion_init(&full);
    XRegion_init(&kept);
    XRegion_init(&exposed);
    XRegion_init(&dirty);
    XRegion_addRect(&full, &view);
    if (!XRect_isEmpty(&keep)) XRegion_addRect(&kept, &keep);
    XRegion_subtracted(&full, &kept, &exposed);
    if (!XRect_isEmpty(&dest)) XRegion_addRect(&dirty, &dest);
    for (i = 0; i < exposed.count; ++i)
        XRegion_addRect(&dirty, &exposed.rects[i]);
    /* 已挂起在本控件的脏区随内容平移（对标 scrollRect 的脏区平移；
       正常 update 路径脏区即时折算到顶层，此区域通常为空，仅保持
       语义完整）。 */
    XRegion_translateInline(&self->m_dirty, dx, dy);
    XWidget_updateRegion(self, &dirty);
    XRegion_deinit(&full);
    XRegion_deinit(&kept);
    XRegion_deinit(&exposed);
    XRegion_deinit(&dirty);
}

XBackingStore* XWidget_backingStore(const XWidget* self)
{
#if XBACKINGSTORE_ON && XPLATFORMBACKINGSTORE_ON && XPLATFORMINTEGRATION_ON
    XWidget* top;
    if (!self) return NULL;
    top = self->m_isWindow ? (XWidget*)self : XWidget_topLevel(self);
    return top ? top->m_backingStore : NULL;
#else
    (void)self;
    return NULL;
#endif /* XBACKINGSTORE_ON && XPLATFORMBACKINGSTORE_ON && XPLATFORMINTEGRATION_ON */
}

int XWidget_devType(const XWidget* self)
{
    /* 对标 QPaintDevice::devType 经 QWidget 的继承入口。任务裁定简化：
       有效控件返回 1=Widget（与 XPixmap_devType"有效设备返回 1"的既有
       约定一致）；精确的 XPaintDeviceType 码（内嵌设备为
       XPaintDeviceType_Widget）可经 XWidget_paintDevice() 取设备后调
       XPaintDevice_devType 查询。 */
    return self ? 1 : 0;
}

void* XWidget_paintEngine(const XWidget* self)
{
    /* 对标 QPaintDevice::paintEngine：返回控件内嵌 XPaintDevice 的引擎
       描述借用指针（初始化为 Raster 软件光栅 + 全能力位，见
       XWidget_init）。XGui 的绘制命令由 XPainter 承担，该描述仅提供
       type/isActive/hasFeature 查询；返回 void* 与
       XPixmap_paintEngine/XPicture_paintEngine 的既有约定一致，
       调用方按 XPaintEngine* 解释且不得释放。 */
#if XPAINTDEVICE_ON
    if (!self) return NULL;
    return &((XWidget*)self)->m_paintDevice.m_engine;
#else
    (void)self;
    return NULL;
#endif /* XPAINTDEVICE_ON */
}

XImage* XWidget_paintImage(const XWidget* self)
{
    /* 离屏重定向优先：XWidget_render/XWidget_grab 绘制子树期间，重定向
       控件的 m_offscreenTarget 即绘制目标，命中后不再回落后备存储。 */
    {
        const XWidget* redirect = xwidget_redirectRoot(self);
        if (redirect) return redirect->m_offscreenTarget;
    }
#if XBACKINGSTORE_ON && XPLATFORMBACKINGSTORE_ON && XPLATFORMINTEGRATION_ON
    XWidget* top;
    XBackingStore* store;
    if (!self) return NULL;
    top = self->m_isWindow ? (XWidget*)self : XWidget_topLevel(self);
    if (!top) return NULL;
    store = top->m_backingStore;
    return store ? XBackingStore_paintImage(store) : NULL;
#else
    (void)self;
    return NULL;
#endif /* XBACKINGSTORE_ON && XPLATFORMBACKINGSTORE_ON && XPLATFORMINTEGRATION_ON */
}

#if XPAINTDEVICE_ON
/** @brief XWidget 绘制设备度量回调（实时读取控件几何）。 */
static int xwidget_paintDeviceMetric(void* userData, int metric)
{
    XWidget* w = (XWidget*)userData;
    if (!w) return 0;
    switch (metric)
    {
        case XPaintDeviceMetric_PdmWidth: return XWidget_width(w);
        case XPaintDeviceMetric_PdmHeight: return XWidget_height(w);
        case XPaintDeviceMetric_PdmWidthMM: return XWidget_width(w) * 25 / 96;
        case XPaintDeviceMetric_PdmHeightMM: return XWidget_height(w) * 25 / 96;
        case XPaintDeviceMetric_PdmNumColors: return 0;
        case XPaintDeviceMetric_PdmDepth: return 32;
        case XPaintDeviceMetric_PdmDpiX:
        case XPaintDeviceMetric_PdmDpiY:
        case XPaintDeviceMetric_PdmPhysicalDpiX:
        case XPaintDeviceMetric_PdmPhysicalDpiY:
            return 96;
        case XPaintDeviceMetric_PdmDevicePixelRatio: return 1;
        case XPaintDeviceMetric_PdmDevicePixelRatioScaled: return 256;
        default: return 0;
    }
}

XPaintDevice* XWidget_paintDevice(const XWidget* self)
{
    if (!self) return NULL;
    return (XPaintDevice*)&((XWidget*)self)->m_paintDevice;
}
#endif /* XPAINTDEVICE_ON */

XPoint XWidget_paintOffset(const XWidget* self)
{
    XPoint offset = XWidget_accumulateOffset(self);
    /* 离屏重定向：绘制偏移换算为"相对重定向控件局部原点 + m_offscreenOrigin"。
       render 的原点为目标绘制偏移 targetOffset；grab 的源矩形取控件全幅，
       原点为 -rectangle.topLeft()（全幅快照时恒为 (0,0)）。命中重定向后
       不再叠加后备存储 paintOrigin——临时快照图像没有绘制原点概念。 */
    {
        const XWidget* redirect = xwidget_redirectRoot(self);
        if (redirect) {
            XPoint base = XWidget_accumulateOffset(redirect);
            offset.x += redirect->m_offscreenOrigin.x - base.x;
            offset.y += redirect->m_offscreenOrigin.y - base.y;
            return offset;
        }
    }
#if XBACKINGSTORE_ON && XPLATFORMBACKINGSTORE_ON && XPLATFORMINTEGRATION_ON
    {
        XBackingStore* store = XWidget_backingStore(self);
        if (store) {
            XPoint origin = XBackingStore_paintOrigin(store);
            offset.x -= origin.x;
            offset.y -= origin.y;
        }
    }
#endif /* XBACKINGSTORE_ON && XPLATFORMBACKINGSTORE_ON && XPLATFORMINTEGRATION_ON */
    return offset;
}

void XWidget_invalidateContentCache(XWidget* self)
{
    if (self) self->m_contentCacheDirty = true;
}

bool XWidget_contentCacheUsable(const XWidget* self, int width, int height)
{
    if (!self || !self->m_contentCache || self->m_contentCacheDirty)
        return false;
    return XImage_width(self->m_contentCache) == width &&
           XImage_height(self->m_contentCache) == height;
}

XImage* XWidget_contentCacheImage(const XWidget* self)
{
    return self ? self->m_contentCache : NULL;
}

XImage* XWidget_beginContentCacheFormat(XWidget* self, int width, int height,
                                        XImageFormat format)
{
    if (!self || width <= 0 || height <= 0) return NULL;
    if (self->m_contentCache &&
        XImage_width(self->m_contentCache) == width &&
        XImage_height(self->m_contentCache) == height &&
        XImage_format(self->m_contentCache) == format)
        return self->m_contentCache;
    /* 尺寸变化时复用已有离屏缓存对象：XImage_reinit_ex 先构造临时图像
       再移动替换，失败时保留旧内容，且不会重置堆对象的内存方法/所有权
       标记；不要对已持有数据的对象直接调用 XImage_init_ex（会泄漏旧像素
       并清掉堆标记）。 */
    if (self->m_contentCache) {
        self->m_contentCacheDirty = true;
        if (!XImage_reinit_ex(self->m_contentCache, width, height, format))
            return NULL;
        self->m_contentCacheDirty = true;
        return self->m_contentCache;
    }
    self->m_contentCache = XImage_create_ex(XCLASS_DEFAULT_MEMORY_TYPE);
    if (!self->m_contentCache)
        return NULL;
    if (!XImage_reinit_ex(self->m_contentCache, width, height, format)) {
        XImage_delete_base(self->m_contentCache);
        self->m_contentCache = NULL;
        return NULL;
    }
    self->m_contentCacheDirty = true;
    return self->m_contentCache;
}

void XWidget_markContentCacheReady(XWidget* self)
{
    if (self) self->m_contentCacheDirty = false;
}

bool XWidget_drawContentCached(XWidget* self, XPainter* target,
                               int x, int y, int width, int height,
                               XWidgetContentDrawProc drawContent,
                               void* userData)
{
    XImage* cache;
    XPainter cachePainter;
    bool drawn;
    if (!self || !target || width <= 0 || height <= 0 || !drawContent)
        return false;
    /* 缓存像素格式必须与目标设备一致：不一致时 drawImage 走逐像素
       转换+混合慢路径（180x54 实测约 2ms/帧），同格式才命中按行
       memcpy 快速路径。 */
    {
        XImageFormat targetFormat = XImageFormat_ARGB32;
        if (target->m_image) targetFormat = XImage_format(target->m_image);
        cache = XWidget_contentCacheImage(self);
        if (XWidget_contentCacheUsable(self, width, height) && cache &&
            !XImage_isNull(cache) &&
            XImage_format(cache) == targetFormat)
            return XPainter_drawImage(target, cache, x, y);
        cache = XWidget_beginContentCacheFormat(self, width, height,
                                                targetFormat);
    }
    if (cache)
    {
        XPainter cachePainter;
        /* 重渲染语义是"重画整个内容"：必须从全透明画布开始。缓存在
           同尺寸失效重渲染时保留着上一次内容，而内容绘制器的第一笔
           往往是半透明外观（如性能浮层的暗色底板），不清理会让旧内容
           透过半透明像素逐帧残留（性能浮层文字重影的根因）。 */
        XImage_fillRect(cache, NULL, 0u);
        XPainter_init(&cachePainter, NULL);
        if (XPainter_begin_image(&cachePainter, cache))
        {
            drawn = drawContent(self, &cachePainter, userData);
            XPainter_end(&cachePainter);
            XPainter_deinit(&cachePainter);
            if (drawn)
            {
                XWidget_markContentCacheReady(self);
                return XPainter_drawImage(target, cache, x, y);
            }
            return false;
        }
        XPainter_deinit(&cachePainter);
    }
    if (XPainter_save(target))
    {
        XPainter_translate(target, (float)x, (float)y);
        drawn = drawContent(self, target, userData);
        XPainter_restore(target);
        return drawn;
    }
    return false;
}

/** @brief 递归绘制控件树：region 为控件本地坐标脏区，裁剪后平移递归子控件。
 * @details Qt 语义根修（14.122）：PAINT 事件携带的矩形 R 是脏区外接框
 *          （XPaintEvent_init 取 boundingRect），凡按自身 paint rect 涂写
 *          的父级（样式面板、autoFillBackground、demo 静态 tile memcpy）
 *          都会把 R 内后代旧像素一并抹掉；因此与 R 相交的后代必须以
 *          R∩自身 为裁剪完整重绘，否则被抹像素残缺到其后代下次自我更新。
 *          多矩形区域先逐矩形拆分派发：每棵子树的 PAINT 只携带单一矩形，
 *          外接框==矩形本身，父级可涂写范围与后代重绘范围严格闭合。 */
static void XWidget_paintTree(XWidget* widget, const XRegion* region)
{
    const XVector* children;
    XRegion maskClipped;
    XRegion clipped;
    const XRegion* paintRegion;
    size_t n;
    size_t i;
    if (!widget || !region || region->count <= 0) return;
    /* 控件自身遮罩参与所有绘制裁剪：遮罩外像素不进入 paintEvent 与后代。 */
    paintRegion = region;
    XRegion_init(&maskClipped);
    XRegion_init(&clipped);
    if (widget->m_mask.count > 0 && widget->m_mask.rects) {
        XRegion_intersectInto(region, &widget->m_mask, &maskClipped);
        if (maskClipped.count <= 0) {
            XRegion_deinit(&maskClipped);
            XRegion_deinit(&clipped);
            return;
        }
        paintRegion = &maskClipped;
    }
    if (paintRegion->count > 1) {
        XRegion single;
        int r;
        /* 遮罩求交可能重新引入多矩形：此处再拆，保证进入本控件的
           PAINT 恒携带单一矩形（递归重入遮罩裁剪为幂等交集）。 */
        XRegion_init(&single);
        for (r = 0; r < paintRegion->count; ++r) {
            XRegion_clear(&single);
            XRegion_addRect(&single, &paintRegion->rects[r]);
            XWidget_paintTree(widget, &single);
        }
        XRegion_deinit(&single);
        XRegion_deinit(&clipped);
        XRegion_deinit(&maskClipped);
        return;
    }
    /* 静态内容保留层：先于效果钩子检查（保留的是未施效输出；同控件
       效果与保留同开时效果优先——setContentRetained 与 setGraphicsEffect
       已强制互斥，此处效果位测试为防御兜底）。仅显式开启保留层的控件
       进入本路径（m_retainedEnabled 门）；未启用保留层的运行态在此仅剩
       一次 g_retainedRegistered 全局计数判断（短路），热点路径零额外
       开销（零回归红线）。 */
    if (g_retainedRegistered > 0 &&
        widget->m_retainedEnabled &&
        widget->m_updatesEnabled && !widget->m_inPaintEvent &&
        !(widget->m_graphicsEffect &&
          XGraphicsEffect_isEnabled(widget->m_graphicsEffect)) &&
        xwidget_paintRetainedLayer(widget, paintRegion)) {
        XRegion_deinit(&clipped);
        XRegion_deinit(&maskClipped);
        return;
    }
    /* 效果挂接渲染段（对标 Qt drawWidget 的 graphics effect 分支）：控件
       携带启用中的效果时，本控件+可见子树改走离屏效果管线（source 快照
       →效果处理→回贴），成功即完成本子树绘制并跳过常规派发；失败（无
       绘制目标/快照失败等）回退常规路径。 */
    if (widget->m_updatesEnabled && !widget->m_inPaintEvent &&
        widget->m_graphicsEffect &&
        xwidget_drawWithGraphicsEffect(widget, paintRegion)) {
        XRegion_deinit(&clipped);
        XRegion_deinit(&maskClipped);
        return;
    }
    if (widget->m_updatesEnabled && !widget->m_inPaintEvent) {
        XPaintEvent event;
        widget->m_inPaintEvent = 1;
        /* 借用 paintRegion 初始化栈上事件（不深拷贝区域）：paintRegion
           在整个 paintTree 递归期间稳定，控件 paintEvent 同步返回，事件
           随即析构——借用生命周期严格覆盖使用期。每个控件一次深拷贝是
           软件增量帧的固定分配开销（N 控件 = N 次 malloc/free），实测
           借用化是本轮帧率提升的最大单项之一（默认页 4430→9063 FPS）。 */
        XPaintEvent_initBorrow(&event, XEVENT_TYPE_PAINT, paintRegion);
        XWidget_paintEvent_base(widget, (XEvent*)&event);
        XPaintEvent_deinit_base(&event);
        widget->m_inPaintEvent = 0;
    }
    children = XObject_children((XObject*)widget);
    n = children ? XVector_size_base((const XContainer*)children) : 0;
    for (i = 0; i < n; ++i) {
        XObject* child = *(XObject**)XVector_at_base(children, (int64_t)i);
        XWidget* w;
        XRect childRect;
        if (!child || !child->is_widget) continue;
        w = (XWidget*)child;
        if (!w->m_visible || !w->m_updatesEnabled) continue;
        /* 原生/弹出子窗口（isWindow）不随父级 paintTree 绘制：它们
         * 拥有独立后备存储并经自身 repaint/flush 上屏（对标 Qt 跳过
         * 原生子窗口）；混入父级遍历会以父链平移重写其自存储，导致
         * Popup 弹层被覆盖为纯背景色（14.114 弹层文字消失根因）。 */
        if (w->m_isWindow) continue;
        childRect = w->m_windowRect;
        XRegion_intersectRectInto(paintRegion, &childRect, &clipped);
        if (clipped.count > 0) {
            XRegion_translateInline(&clipped, -childRect.x, -childRect.y);
            XWidget_paintTree(w, &clipped);
        }
    }
    XRegion_deinit(&clipped);
    XRegion_deinit(&maskClipped);
}

/* ==================== 控件快照与离屏渲染（对标 QWidget grab/render） ==================== */

/**
 * @brief 创建指定尺寸的全透明快照画布（ARGB32_Premultiplied）。
 * @details grab/render 的临时目标图像统一走此入口：ARGB32_Premultiplied
 *          与后备存储内部缓冲同格式，软件光栅与 XPainter_drawImage 混合
 *          无需转换。画布从全透明开始——只含控件自身与可见子树内容，
 *          未覆盖像素保持 0（与 Qt grab 的透明底语义一致，控件背景由
 *          autoFillBackground/paintEvent 自行负责）。
 * @param      width 画布宽度；须大于 0。
 * @param      height 画布高度；须大于 0。
 * @return     新建 XImage（堆对象，调用方 XImage_delete_base 释放）；
 *             失败返回 NULL。
 */
static XImage* xwidget_createSnapshotImage(int width, int height)
{
    XImage* image = XImage_create_ex(XCLASS_DEFAULT_MEMORY_TYPE);
    if (!image) return NULL;
    if (!XImage_reinit_ex(image, width, height,
                          XImageFormat_ARGB32_Premultiplied)) {
        XImage_delete_base(image);
        return NULL;
    }
    /* 与 XWidget_drawContentCached 相同的语义：重渲染从全透明画布开始，
       防止复用/实现变化时残留旧像素。 */
    XImage_fillRect(image, NULL, 0u);
    return image;
}

/**
 * @brief 把 self 子树同步绘制到 target 图像（离屏重定向核心）。
 * @details 登记重定向后走 XWidget_paintTree 正常绘制闭环：paintEvent
 *          经虚表分派到各控件实现，其内部经 XWidget_paintImage 取到
 *          target、经 XWidget_paintOffset 取到"相对 self 局部原点 +
 *          (originX,originY)"的平移量，从而把整棵子树画进 target。
 *          登记与复位严格成对；绘制复用 paintTree 的遮罩/裁剪/可见性
 *          语义（隐藏子控件不绘制，updatesEnabled 关闭的控件跳过）。
 * @param      self 重定向控件（子树根）；不可为 NULL。
 * @param      target 目标图像；须为已初始化的有效图像。
 * @param      originX self 局部 (0,0) 在 target 中的映射横坐标。
 * @param      originY self 局部 (0,0) 在 target 中的映射纵坐标。
 * @return     派发成功返回 true；参数非法返回 false。
 */
static bool xwidget_renderSubtree(XWidget* self, XImage* target,
                                  int originX, int originY)
{
    XRegion region;
    XRect rect;
    if (!self || !target || XImage_isNull(target)) return false;
    rect.x = 0;
    rect.y = 0;
    rect.width = XImage_width(target);
    rect.height = XImage_height(target);
    if (rect.width <= 0 || rect.height <= 0) return false;
    XRegion_init(&region);
    XRegion_addRect(&region, &rect);
    self->m_offscreenTarget = target;
    self->m_offscreenOrigin.x = originX;
    self->m_offscreenOrigin.y = originY;
    XWidget_paintTree(self, &region);
    self->m_offscreenTarget = NULL;
    self->m_offscreenOrigin.x = 0;
    self->m_offscreenOrigin.y = 0;
    XRegion_deinit(&region);
    return true;
}

double g_xgui_paintTreeMs = 0.0;
double g_xgui_flushMs = 0.0;
double g_xgui_rootPaintMs = 0.0;
long g_xgui_flushCount = 0;

void XWidget_flushBackingStore(XWidget* self, const XRegion* region)
{
#if XBACKINGSTORE_ON && XPLATFORMBACKINGSTORE_ON && XPLATFORMINTEGRATION_ON
    XWidget* top;
    XBackingStore* store;
    XRegion whole;
    XRect contents;
    XSize size;
    if (!self) return;
    top = self->m_isWindow ? (XWidget*)self : XWidget_topLevel(self);
    if (!top || !top->m_isWindow) return;
    if (!top->m_windowHandle)
        XWidget_createWindow(top);
    store = top->m_backingStore;
    if (!store) {
        store = XBackingStore_create_ex(XCLASS_DEFAULT_MEMORY_TYPE,
                                        (XWindow*)top->m_windowHandle);
        if (!store) return;
        top->m_backingStore = store;
    }
    XSize_init(&size, top->m_windowRect.width, top->m_windowRect.height);
    XBackingStore_resize(store, &size);
    XRegion_init(&whole);
 #if XGUI_BACKINGSTORE_RENDER_MODE == XGUI_BACKINGSTORE_RENDER_MODE_FULL
    {
        contents = top->m_contentsRect;
        XRegion_addRect(&whole, &contents);
    }
 #else
    if (region && region->count > 0)
        XRegion_copy(region, &whole);
    else if (top->m_dirty.count > 0)
    {
        /* P0-4（2026-09-25）脏区限绘：region 未携带时优先用当前脏区
           回退（弹窗/菜单 flush(NULL) 只刷真实脏区），不再整窗
           contentsRect 退化——整窗重绘在 GPU 直通持久 FBO 上表现为
           全页帧对闪变与覆盖层（FPS 面板）被整帧重排的可见抖动。
           回退链尾部仍保留整窗（m_dirty 空且无 region=首显/resize）。
           XGUI_FLUSH_FULLFALLBACK=1 恢复整窗退化（诊断用）。 */
        static int fullFallback = -1;
        if (fullFallback < 0)
        {
            const char* ff = XSystem_environment("XGUI_FLUSH_FULLFALLBACK");
            fullFallback = ff && *ff && !(ff[0] == '0' && ff[1] == 0) ? 1 : 0;
        }
        if (fullFallback)
        {
            contents = top->m_contentsRect;
            XRegion_addRect(&whole, &contents);
        }
        else
        {
            /* 包围盒单矩形而非多矩形区域：paintTree 对区域逐矩形求交，
               多矩形脏区会把每控件裁剪推入 O(控件×矩形) 慢路（实测曾有
               benchmark-full 掉帧至 1/3），包围盒保持快路径且仍限绘。 */
            XRect bb;
            XRegion_boundingRect(&top->m_dirty, &bb);
            XRegion_clear(&whole);
            XRegion_addRect(&whole, &bb);
        }
    }
    else {
        contents = top->m_contentsRect;
        XRegion_addRect(&whole, &contents);
    }
 #endif
    /*
     * paint 事件携带的是入队时的脏区快照。先从当前脏区移除本次将要
     * 绘制的部分，避免把事件入队后新增的区域一并清掉。repaint() 直接
     * 传入 top->m_dirty 时无需计算差集；绘制期间产生的新 update 会重新
     * 进入 top->m_dirty，并在函数末尾继续排队。
     */
    XWidget_attrSet(&top->m_attributes, XWidgetAttribute_PendingUpdate, false);
    if (whole.count > 0)
    {
        if (region == &top->m_dirty || top->m_dirty.count <= 0)
        {
            XRegion_clear(&top->m_dirty);
        }
        else
        {
            XRegion_subtracted(&top->m_dirty, &whole, &top->m_dirty);
        }
    }
    if (whole.count > 0) {
        /* 后端或平台集成被裁剪/尚未建立时，repaint 仍必须同步派发
           paintEvent；区别仅是没有 XImage 可供绘制、也不会执行上屏。 */
        if (!XBackingStore_paintImage(store)) {
            XWidget_paintTree(top, &whole);
        }
        else {
#if XPLATFORMINTEGRATION_ON && XGPU_ON && \
    XGUI_BACKINGSTORE_RENDER_MODE != XGUI_BACKINGSTORE_RENDER_MODE_PARTIAL
        /* GPU 直通（阶段 2）：请求 GPU 时获取窗口直通会话；失败自动回退
           软件/阶段 1。PARTIAL 模式保持离屏 readback（tile 缓冲语义）。 */
        XGpuRenderBackend* gpuWindow = NULL;
        if (XGpuRenderBackend_requested())
            gpuWindow = XGpuRenderBackend_acquireForWindow(
                (XWindow*)top->m_windowHandle,
                top->m_windowRect.width, top->m_windowRect.height);
        if (gpuWindow)
        {
            /* 会话首帧补绘（2026-09-25）：激活会话实测启动首次全绘的
               上屏不完整（此后任何一次全窗重绘内容即完整），GPU 会话
               首次挂接后强制再排一次全窗更新，由下一帧补齐。
               静态单次：多窗口场景仅首窗受益（demo/测试口径足够）。 */
            static int s_firstSessionReupdate = 0;
            if (!s_firstSessionReupdate)
            {
                s_firstSessionReupdate = 1;
                XWidget_update(top);
            }
        }
#endif /* GPU && !PARTIAL */
        g_paintTargetImage = XBackingStore_paintImage(store);
        /* 表面裁剪：按刷区域外接矩形设置（对标 Qt drawWidget →
           setSystemClip(toBePainted)，设备坐标）。paintTree 递归期间
           所有像素写入限幅在脏区内，控件 setClipRect 不可绕过。 */
        {
            /* 表面裁剪：按刷区域外接矩形设置（对标 Qt drawWidget →
               setSystemClip(toBePainted)，设备坐标）。两遍求 bbox——
               单遍增量版在后续矩形把 sc.x/sc.y 左/上拉走时 x+width
               随之左移，右/下边界被静默收窄（多矩形脏区表面裁剪
               错误根因，远端第五轮修复，2026-09-25 合并重放）。 */
            XRect sc = { 0, 0, 0, 0 };
            int r;
            int maxRx1 = 0;
            int maxRy1 = 0;
            for (r = 0; r < whole.count; ++r) {
                const XRect* rc = &whole.rects[r];
                int rx1 = rc->x + rc->width;
                int ry1 = rc->y + rc->height;
                if (r == 0) {
                    sc = *rc;
                    maxRx1 = rx1;
                    maxRy1 = ry1;
                    continue;
                }
                if (rc->x < sc.x) sc.x = rc->x;
                if (rc->y < sc.y) sc.y = rc->y;
                if (rx1 > maxRx1) maxRx1 = rx1;
                if (ry1 > maxRy1) maxRy1 = ry1;
            }
            sc.width = maxRx1 - sc.x;
            sc.height = maxRy1 - sc.y;
            XPainter_setSurfaceClipRect(&sc, XBackingStore_paintImage(store));
        }
        XBackingStore_beginPaint(store, &whole);
#if XGUI_BACKINGSTORE_RENDER_MODE == XGUI_BACKINGSTORE_RENDER_MODE_PARTIAL
        {
            XRect tile;
            XRegion tileRegion;
            /* 攒批入口经平台句柄直达（XBackingStore 公共包装不在本模块
               所有权内；present 决策全部由平台层攒批策略触发）。 */
            XPlatformBackingStore* platform = XBackingStore_handle(store);
            XRegion_init(&tileRegion);
            while (XBackingStore_nextTile(store, &tile)) {
                /* XWidget_paintTree 接收的是区域集合。不能把 XRect 直接
                 * 强转传入，否则第二个 tile 的 x/y 会被解释为 count/指针。 */
                XRegion_clear(&tileRegion);
                XRegion_addRect(&tileRegion, &tile);
                XWidget_paintTree(top, &tileRegion);
                /* 请求攒批（对标 Qt 高频局部更新按帧合批）：本调用只把
                   tile 内容并入攒批缓冲，present 由攒批决策触发（相邻
                   合并/超 1/4 屏预算/16ms 帧界）；攒批关闭时退化为逐片
                   即时上屏。 */
                XPlatformBackingStore_flushTileBatched(
                    platform, (XWindow*)top->m_windowHandle, &tile, NULL);
            }
            XRegion_deinit(&tileRegion);
            /* 帧末显式边界强制收批：所有已请求攒批的 tile 在本帧内终究
               上屏（最终一致性，不丢帧）。 */
            XPlatformBackingStore_flushPendingTiles(
                platform, (XWindow*)top->m_windowHandle);
            XBackingStore_endPaint(store);
        }
#else
        XWidget_paintTree(top, &whole);
        XBackingStore_endPaint(store);
#if XPLATFORMINTEGRATION_ON && XGPU_ON && \
    XGUI_BACKINGSTORE_RENDER_MODE != XGUI_BACKINGSTORE_RENDER_MODE_PARTIAL
        if (gpuWindow && !XGpuRenderBackend_frameDegraded())
        {
            /* GPU 上屏双通道。默认=回读+BitBlt：SwapBuffers 换链在
               RDP/远程显示栈下帧不可达——FBO 内容经逐点探针证实正确
               稳定，但屏幕停帧/闪烁（2026-09-24 实测，远端作者登记的
               「GPU 直通默认开交互白屏」同源）。回读 +0.5ms 换取任何
               显示环境下的确定性可见；XGPU_PRESENT=swap 切回换链
               （物理显示/真机驱动栈零拷贝更快）。
               framePresented 诚实上报（XGUI_PRESENT_HONEST 默认 1）：
               按后端真实返回值置位，供截图选择内容来源（谎报会让截图
               读到错误通道）；置 0 回退旧「无条件置 true」谎报行为。 */
            const char* presentMode = NULL;
            int presentThisFrame = 1;
#if !defined(XGUI_PRESENT_HONEST)
#define XGUI_PRESENT_HONEST 1
#endif
#if XGUI_PRESENT_HONEST
            bool presented;
#endif
            /* 呈现限频（2026-09-25）：重绘循环 ~250Hz 时每帧全窗读回
               +BitBlt 上传（800x600≈1.9MB/帧）在远程/虚拟显示栈
               （RDP/OrayIdd）上泛滥成持续闪烁；FBO 内容持久，跳帧
               无损失（下一周期整帧补上），故缺省限 60Hz——与物理
               显示刷新一致。XGPU_PRESENT_MAX_FPS=0 禁用限频，
               =N 自定义；绘制帧率不受影响（仅限上屏）。 */
            {
                static int throttleInit = -1;
                static double throttleMinMs = 16.6;
                if (throttleInit < 0)
                {
                    const char* tv =
                        XSystem_environment("XGPU_PRESENT_MAX_FPS");
                    int fps = 60;
                    if (tv && *tv) fps = atoi(tv);
                    throttleMinMs = fps > 0 ? 1000.0 / (double)fps : 0.0;
                    throttleInit = 1;
                }
                if (throttleMinMs > 0.0)
                {
                    static int64_t s_lastPresent = 0;
                    int64_t nowMs = XDateTime_currentMSecsSinceEpoch();
                    double elapseMs = (double)(nowMs - s_lastPresent);
                    /* 全窗帧（首绘/resize/全窗失效）永不跳过；局部帧在
                     * 限频窗口内跳过时，把区域并回脏区——绘制结果已在
                     * FBO/后备存储中持久，下一呈现周期整帧补上。 */
                    int coversFull = 0;
                    {
                        XRect sc = { 0, 0, 0, 0 };
                        int r;
                        int maxRx1 = 0;
                        int maxRy1 = 0;
                        for (r = 0; r < whole.count; ++r) {
                            const XRect* rc = &whole.rects[r];
                            int rx1 = rc->x + rc->width;
                            int ry1 = rc->y + rc->height;
                            if (r == 0) {
                                sc = *rc;
                                maxRx1 = rx1;
                                maxRy1 = ry1;
                                continue;
                            }
                            if (rc->x < sc.x) sc.x = rc->x;
                            if (rc->y < sc.y) sc.y = rc->y;
                            if (rx1 > maxRx1) maxRx1 = rx1;
                            if (ry1 > maxRy1) maxRy1 = ry1;
                        }
                        coversFull = (sc.x <= 0 && sc.y <= 0 &&
                                      sc.width >= top->m_windowRect.width &&
                                      sc.height >= top->m_windowRect.height);
                    }
                    if (s_lastPresent != 0 &&
                        elapseMs < throttleMinMs && !coversFull)
                    {
                        int r;
                        presentThisFrame = 0;
                        for (r = 0; r < whole.count; ++r)
                            XRegion_addRect(&top->m_dirty, &whole.rects[r]);
                    }
                    else
                        s_lastPresent = nowMs;
                }
            }
            if (presentThisFrame)
                presentMode = XSystem_environment("XGPU_PRESENT");
            if (presentThisFrame && presentMode &&
                presentMode[0] == 's' &&
                presentMode[1] == 'w' && presentMode[2] == 'a' &&
                presentMode[3] == 'p' && presentMode[4] == '\0')
            {
#if XGUI_PRESENT_HONEST
                presented = XGpuRenderBackend_presentToWindow(gpuWindow);
                if (!presented)
                {
                    /* swap 失败回退 BitBlt flush 通道（保底可见性）：窗口
                       直通模式帧末不读回（painterGpuEndFrame），backing
                       image 没有本帧内容，先 readback 合并才可提交。 */
                    presented = XGpuRenderBackend_readback(
                        gpuWindow, XBackingStore_paintImage(store));
                }
                /* XBackingStore_flush 为 void 无结果可查：平台后备存储
                   不在位时 flush 是静默空操作，此时按实不置位。 */
                if (presented && !XBackingStore_handle(store))
                    presented = false;
                XBackingStore_flush(store, &whole,
                                    (XWindow*)top->m_windowHandle, NULL);
                XGpuRenderBackend_setFramePresented(presented);
#else /* XGUI_PRESENT_HONEST=0：旧谎报行为，忽略后端结果。 */
                XGpuRenderBackend_presentToWindow(gpuWindow);
                XGpuRenderBackend_setFramePresented(true);
#endif
            }
            else if (presentThisFrame)
            {
#if XGUI_PRESENT_HONEST
                /* readback 失败即 FBO→image 未发生，flush 只能提交陈旧
                   帧，按实置 false；flush 照常执行，保底可见性不回退。
                   限频跳帧时整分支跳过：FBO 内容持久，屏幕保留上一
                   呈现帧，下一周期整帧补上。 */
                presented = XGpuRenderBackend_readback(
                    gpuWindow, XBackingStore_paintImage(store));
                if (presented && !XBackingStore_handle(store))
                    presented = false;
                XBackingStore_flush(store, &whole,
                                    (XWindow*)top->m_windowHandle, NULL);
                XGpuRenderBackend_setFramePresented(presented);
#else
                XGpuRenderBackend_readback(
                    gpuWindow, XBackingStore_paintImage(store));
                XBackingStore_flush(store, &whole,
                                    (XWindow*)top->m_windowHandle, NULL);
                XGpuRenderBackend_setFramePresented(true);
#endif
            }
        }
        else
        {
            /* 软件模式（gpuWindow=NULL）与降级帧的提交腿（2026-09-25
               修复：present 诚实化重构时 flush 被收进 gpuWindow 分支，
               软件模式从此零提交=整窗白屏，激活会话实测复现）。
               降级帧额外先补读回：窗口直通模式帧末无读回，backing
               image 停在陈旧内容，直接 flush 会把陈旧/空白帧糊上屏
               ——这正是 GPU 模式闪烁的来源（降级标志逐帧翻转时，
               新鲜帧与陈旧帧交替上屏）。 */
            if (gpuWindow)
                XGpuRenderBackend_readback(
                    gpuWindow, XBackingStore_paintImage(store));
            XBackingStore_flush(store, &whole,
                                (XWindow*)top->m_windowHandle, NULL);
            XGpuRenderBackend_setFramePresented(false);
        }
        if (gpuWindow)
            XGpuRenderBackend_endWindowFrame();
#else
        XBackingStore_flush(store, &whole, (XWindow*)top->m_windowHandle, NULL);
#endif /* GPU && !PARTIAL */
#endif
        }
    }
        g_paintTargetImage = NULL;
    XRegion_deinit(&whole);
    XPainter_clearSurfaceClipRect();
    /* 保留绘制期间或本次快照未覆盖的脏区，并确保它最终会再次派发。 */
    if (top->m_dirty.count > 0)
    {
        bool alreadyPending = XWidget_attrTest(
            &top->m_attributes, XWidgetAttribute_PendingUpdate);
        XWidget_attrSet(&top->m_attributes,
                        XWidgetAttribute_PendingUpdate, true);
        if (!alreadyPending && top->m_windowHandle && top->m_visible)
        {
            if (!XWidget_postPaintEvent(top))
                XWidget_attrSet(&top->m_attributes,
                                XWidgetAttribute_PendingUpdate, false);
        }
    }
    else
    {
        XWidget_attrSet(&top->m_attributes,
                        XWidgetAttribute_PendingUpdate, false);
    }
#else
    (void)self;
    (void)region;
#endif /* XBACKINGSTORE_ON && XPLATFORMBACKINGSTORE_ON && XPLATFORMINTEGRATION_ON */
}

/* ==================== 快照与离屏渲染公开入口（对标 QWidget::grab/render） ==================== */

bool XWidget_render(XWidget* self, XPainter* painter, const XRect* targetRect)
{
    XRect target;
    XImage* image;
    bool drawn;
    int width;
    int height;
    if (!self || !painter) return false;
    width = XWidget_width(self);
    height = XWidget_height(self);
    if (width <= 0 || height <= 0) return false;
    if (targetRect) {
        target = *targetRect;
        /* 简化：仅支持与控件等尺寸的目标矩形（无平移缩放矩阵推导）。
           尺寸不一致直接拒绝，由调用方先行 XImage_scaled 等缩放。 */
        if (target.width != width || target.height != height) return false;
    }
    else
    {
        XRect_init(&target, 0, 0, width, height);
    }
    image = xwidget_createSnapshotImage(width, height);
    if (!image) return false;
    if (!xwidget_renderSubtree(self, image, 0, 0)) {
        XImage_delete_base(image);
        return false;
    }
    /* 经调用方绘制器输出：目标平移/裁剪/合成属性全部由 painter 现有
       状态接管（源完全等尺寸，目标只需一个绘制原点）。 */
    drawn = XPainter_drawImage(painter, image, target.x, target.y);
    XImage_delete_base(image);
    return drawn;
}

XImage* XWidget_grab(const XWidget* self)
{
    XWidget* widget = (XWidget*)self;
    XImage* image;
    int width;
    int height;
    if (!self) return NULL;
    width = XWidget_width(widget);
    height = XWidget_height(widget);
    if (width <= 0 || height <= 0) return NULL;

    /* 路径 1（已绘制顶层）：后备存储持有与本控件同尺寸的有效像素时
       直接深拷贝，忠实还原最近一次上屏内容（含 PARTIAL 瓦片合成结果），
       不触发任何重绘。仅对顶层控件生效——后备存储属于顶层控件，缓冲
       覆盖整个窗口；子控件走路径 2 独立重绘。快照流程重定向期间跳过，
       避免读到正在合成的中间状态。 */
    if (widget->m_isWindow && !xwidget_redirectRoot(widget))
    {
#if XBACKINGSTORE_ON && XPLATFORMBACKINGSTORE_ON && XPLATFORMINTEGRATION_ON
        XBackingStore* store = XWidget_backingStore(widget);
        XImage* paintImage = store ? XBackingStore_paintImage(store) : NULL;
        if (paintImage && !XImage_isNull(paintImage) &&
            XImage_width(paintImage) == width &&
            XImage_height(paintImage) == height)
        {
            image = XImage_create_ex(XCLASS_DEFAULT_MEMORY_TYPE);
            if (image)
            {
                if (XBackingStore_toImage(store, image))
                    return image;
                XImage_delete_base(image);
            }
        }
#endif /* XBACKINGSTORE_ON && XPLATFORMBACKINGSTORE_ON && XPLATFORMINTEGRATION_ON */
    }

    /* 路径 2（同步重绘）：从未上屏、后备数据缺失/尺寸不匹配或子控件
       场景下，创建临时画布并同步派发一次完整 paintEvent 子树绘制。 */
    image = xwidget_createSnapshotImage(width, height);
    if (!image) return NULL;
    if (!xwidget_renderSubtree(widget, image, 0, 0)) {
        XImage_delete_base(image);
        return NULL;
    }
    return image;
}

/* ==================== 事件分派（对标 QWidget::event） ==================== */

/*
 * XWidget_event_base 继承自 XObject（对标 QObject::event），按文档约定在
 * XWidget.h 中直接宏复用：XWidget_event_base(self, event) 展开为
 * XObject_event_base((XObject*)(self), (event))，经虚表 EXObject_Event 槽位
 * 分派到 VXWidget_event（按事件类型分发到 25 个控件事件虚函数）。
 */

/* ==================== 25 个公开事件槽入口（XWidget_*_base） ==================== */

/** @brief 生成公开事件槽入口：经对象虚表安全调用对应槽位。 */
#define XWIDGET_VT_DISPATCH(FuncName, SlotEnum) \
void XWidget_##FuncName##_base(XWidget* self, XEvent* event) \
{ \
    if (ISNULL(self, "") || ISNULL(XClassGetVtable(self), "")) return; \
    if (!XClassGetVirtualFunc(self, SlotEnum, bool)) return; \
    XClassGetVirtualFunc(self, SlotEnum, void(*)(XWidget*, XEvent*))(self, event); \
}

XWIDGET_VT_DISPATCH(paintEvent, EXWidget_PaintEvent)
XWIDGET_VT_DISPATCH(resizeEvent, EXWidget_ResizeEvent)
XWIDGET_VT_DISPATCH(moveEvent, EXWidget_MoveEvent)
XWIDGET_VT_DISPATCH(closeEvent, EXWidget_CloseEvent)
XWIDGET_VT_DISPATCH(focusInEvent, EXWidget_FocusInEvent)
XWIDGET_VT_DISPATCH(focusOutEvent, EXWidget_FocusOutEvent)
XWIDGET_VT_DISPATCH(enterEvent, EXWidget_EnterEvent)
XWIDGET_VT_DISPATCH(leaveEvent, EXWidget_LeaveEvent)
XWIDGET_VT_DISPATCH(keyPressEvent, EXWidget_KeyPressEvent)
XWIDGET_VT_DISPATCH(keyReleaseEvent, EXWidget_KeyReleaseEvent)
XWIDGET_VT_DISPATCH(inputMethodEvent, EXWidget_InputMethodEvent)
XWIDGET_VT_DISPATCH(contextMenuEvent, EXWidget_ContextMenuEvent)
XWIDGET_VT_DISPATCH(dragEnterEvent, EXWidget_DragEnterEvent)
XWIDGET_VT_DISPATCH(dragMoveEvent, EXWidget_DragMoveEvent)
XWIDGET_VT_DISPATCH(dragLeaveEvent, EXWidget_DragLeaveEvent)
XWIDGET_VT_DISPATCH(dropEvent, EXWidget_DropEvent)
XWIDGET_VT_DISPATCH(mousePressEvent, EXWidget_MousePressEvent)
XWIDGET_VT_DISPATCH(mouseReleaseEvent, EXWidget_MouseReleaseEvent)
XWIDGET_VT_DISPATCH(mouseDoubleClickEvent, EXWidget_MouseDoubleClickEvent)
XWIDGET_VT_DISPATCH(mouseMoveEvent, EXWidget_MouseMoveEvent)
XWIDGET_VT_DISPATCH(wheelEvent, EXWidget_WheelEvent)
XWIDGET_VT_DISPATCH(showEvent, EXWidget_ShowEvent)
XWIDGET_VT_DISPATCH(hideEvent, EXWidget_HideEvent)
XWIDGET_VT_DISPATCH(changeEvent, EXWidget_ChangeEvent)
XWIDGET_VT_DISPATCH(touchEvent, EXWidget_TouchEvent)
XWIDGET_VT_DISPATCH(tabletEvent, EXWidget_TabletEvent)

/* ==================== 图形效果（对标 QWidget::graphicsEffect） ==================== */

XGraphicsEffect* XWidget_graphicsEffect(const XWidget* self)
{
    return self ? self->m_graphicsEffect : NULL;
}

void XWidget_setGraphicsEffect(XWidget* self, XGraphicsEffect* effect)
{
    if (!self) return;
    if (self->m_graphicsEffect == effect) return;
    /* Qt：已有效果先删除再安装新效果，控件取得新效果所有权。 */
    if (self->m_graphicsEffect) {
        XGraphicsEffect_setSource(self->m_graphicsEffect, NULL);
        XGraphicsEffect_delete_base(self->m_graphicsEffect);
        self->m_graphicsEffect = NULL;
    }
    self->m_graphicsEffect = effect;
    if (effect) {
        /* 图形效果与保留层互斥（效果优先）：保留层缓存的是未施效输出，
           同控件二者不可同开——挂接效果即放弃保留（释放缓存与 LRU 登记，
           控件退回常规绘制）。 */
        if (self->m_retainedEnabled)
            xwidget_retainedDisable(self);
        /* 对标 Qt setGraphicsEffect：挂接即建立 source() 关联，并经
           sourceChanged（基类默认转 update()，覆盖效果外扩包围盒）请求
           一次重绘，保证效果安装即呈现。 */
        XGraphicsEffect_setSource(effect, self);
        XGraphicsEffect_sourceChanged(
            effect, XGraphicsEffectChange_ContentsChanged |
                        XGraphicsEffectChange_GeometryChanged);
    }
}

/**
 * @brief 效果挂接绘制入口（XWidget 效果段私有；对标 Qt
 *        QWidgetPrivate::drawWidget 的 graphics effect 分支）。
 * @details paintTree 常规派发前调用：控件带启用效果时，先把本控件+可见
 *          子树同步渲染到控件全幅的未施效离屏快照（对标 Qt source 绘制
 *          不含效果自身；期间临时摘除本控件效果以防 paintTree 递归重入
 *          效果分支），再交 XGraphicsEffect_drawWidget 完成"脏区∩控件
 *          区域"的效果处理与回贴。快照恒走 createSnapshotImage+
 *          renderSubtree 同步重绘路径，不读后备存储旧帧——旧帧可能已
 *          含上一轮效果输出，重复施效会逐帧加深。
 * @param      widget 目标控件；不可为 NULL。
 * @param      paintRegion 本控件局部坐标绘制区域；不可为 NULL。
 * @return true=效果路径已完成本子树绘制；false=回退常规绘制。
 */
static bool xwidget_drawWithGraphicsEffect(XWidget* widget,
                                           const XRegion* paintRegion)
{
    XGraphicsEffect* effect;
    XImage* snapshot;
    bool rendered;
    bool drawn;
    if (!widget || !paintRegion || !widget->m_graphicsEffect) return false;
    effect = widget->m_graphicsEffect;
    if (!XGraphicsEffect_isEnabled(effect)) return false;
    snapshot = xwidget_createSnapshotImage(XWidget_width(widget),
                                           XWidget_height(widget));
    if (!snapshot) return false;
    /* 临时摘除效果：source 快照绘制不得再次进入效果分支（防递归）。 */
    widget->m_graphicsEffect = NULL;
    rendered = xwidget_renderSubtree(widget, snapshot, 0, 0);
    widget->m_graphicsEffect = effect;
    if (!rendered) {
        XImage_delete_base(snapshot);
        return false;
    }
    drawn = XGraphicsEffect_drawWidget(effect, widget, snapshot, paintRegion);
    XImage_delete_base(snapshot);
    return drawn;
}

/* ==================== 静态内容保留层（对标 LVGL 静态内容缓存思想） ==================== */
/* 登记节点结构与全局表定义前置在文件头部（前向声明块之后）：
 * 失效联动（addDirtyRegion）、几何/可见性钩子与移动构造在其之前使用。 */

/** @brief 保留层字节预算（XGuiConfig.h #ifndef 默认 2MB；0=禁止建立缓存）。 */
static size_t xwidget_retainedBudget(void)
{
    return (size_t)(XGUI_RETAINED_LAYER_BUDGET_BYTES);
}

/** @brief 缓存图像字节量（ARGB32_Premultiplied 恒 4 字节/像素）。 */
static size_t xwidget_retainedImageBytes(int width, int height)
{
    return (size_t)width * (size_t)height * 4u;
}

/** @brief 访问即刷新：登记节点移到表头（MRU）并更新访问时间戳。 */
static void xwidget_retainedTouch(XWidget* self)
{
    XWidgetRetainedNode* node = self ? self->m_retainedNode : NULL;
    if (!node || g_retainedHead == node) {
        if (self) self->m_retainedStamp = ++g_retainedStampClock;
        return;
    }
    self->m_retainedStamp = ++g_retainedStampClock;
    if (node->m_prev) node->m_prev->m_next = node->m_next;
    if (node->m_next) node->m_next->m_prev = node->m_prev;
    if (g_retainedTail == node) g_retainedTail = node->m_prev;
    node->m_prev = NULL;
    node->m_next = g_retainedHead;
    if (g_retainedHead) g_retainedHead->m_prev = node;
    g_retainedHead = node;
    if (!g_retainedTail) g_retainedTail = node;
}

/** @brief 释放缓存图像并扣减预算字节；登记与开关不动（重建时复用）。 */
static void xwidget_retainedDropCache(XWidget* self)
{
    if (!self || !self->m_retainedCache) return;
    g_retainedBytes -= xwidget_retainedImageBytes(
        XImage_width(self->m_retainedCache),
        XImage_height(self->m_retainedCache));
    XImage_delete_base(self->m_retainedCache);
    self->m_retainedCache = NULL;
    self->m_retainedValid = false;
}

/** @brief 彻底关闭保留层：摘除登记、释放缓存、清开关（LRU 淘汰终点）。 */
static void xwidget_retainedDisable(XWidget* self)
{
    XWidgetRetainedNode* node;
    if (!self) return;
    node = self->m_retainedNode;
    if (node) {
        if (node->m_prev) node->m_prev->m_next = node->m_next;
        else g_retainedHead = node->m_next;
        if (node->m_next) node->m_next->m_prev = node->m_prev;
        else g_retainedTail = node->m_prev;
        XMemory_free(node, XCLASS_DEFAULT_MEMORY_TYPE);
        self->m_retainedNode = NULL;
        --g_retainedRegistered;
    }
    xwidget_retainedDropCache(self);
    self->m_retainedEnabled = false;
    self->m_retainedStamp = 0;
}

/** @brief 单点失效（保持缓存内存，下次绘制重渲染并更新缓存）。 */
static void xwidget_retainedInvalidateOne(XWidget* widget)
{
    if (widget && widget->m_retainedEnabled && widget->m_retainedValid)
        widget->m_retainedValid = false;
}

/** @brief 失效 origin 自身及其保留层祖先链（子树在祖先缓存内）。 */
static void xwidget_retainedInvalidateChain(XWidget* origin)
{
    XWidget* node = origin;
    while (node) {
        xwidget_retainedInvalidateOne(node);
        node = (XWidget*)XObject_parent((XObject*)node);
    }
}

/**
 * @brief 失效联动主入口：origin 子树内容/几何/可见性变化命中的保留层作废。
 * @details 只沿父链失效（origin 自身 + 保留层祖先）：缓存画布从全透明
 *          开始、只含子树自身渲染输出（不含背景与邻居），预乘 SourceOver
 *          的 Porter-Duff Over 结合律保证回贴恒等于"子树输出 over 目标
 *          既有内容"——父级背景重绘、兄弟交叠内容变化都不改变缓存语义，
 *          无需相交扫描；这正是全窗口脏区下保留层不被误杀的关键。
 */
static void xwidget_retainedInvalidateForRect(XWidget* origin)
{
    if (!origin || g_retainedRegistered <= 0) return;
    xwidget_retainedInvalidateChain(origin);
}

/**
 * @brief 预算内确保 w×h 保留层缓存图像；失败/超预算返回 NULL。
 * @details 同尺寸旧缓存直接复用；尺寸变化先扣旧账，再按 LRU 从表尾
 *          淘汰（被淘汰控件退回常规绘制）腾位；单缓存超预算或分配
 *          失败时返回 NULL，调用方退回常规绘制（不阻塞）。
 */
static XImage* xwidget_retainedEnsureCache(XWidget* self, int width, int height)
{
    XImage* image;
    size_t need;
    size_t budget;
    if (width <= 0 || height <= 0) return NULL;
    budget = xwidget_retainedBudget();
    need = xwidget_retainedImageBytes(width, height);
    if (self->m_retainedCache &&
        XImage_width(self->m_retainedCache) == width &&
        XImage_height(self->m_retainedCache) == height)
        return self->m_retainedCache;
    if (need > budget) return NULL;
    xwidget_retainedDropCache(self);
    /* 按访问时间戳从 LRU 端淘汰"其他"保留层腾位（本帧内 blit/重建会
       把同树其他保留层陆续刷新为 MRU，新启用控件的节点可能恰好居于
       LRU 端——因此只能跳过 self 挑受害者，不能一见 self 就放弃）。
       淘汰的保留层开关一并清除（不自动重建，防抖动）。 */
    while (g_retainedBytes + need > budget) {
        XWidgetRetainedNode* victim;
        victim = g_retainedTail;
        while (victim && victim->m_widget == self)
            victim = victim->m_prev;
        if (!victim) break;
        xwidget_retainedDisable(victim->m_widget);
    }
    if (g_retainedBytes + need > budget) return NULL;
    image = XImage_create_ex(XCLASS_DEFAULT_MEMORY_TYPE);
    if (!image) return NULL;
    if (!XImage_reinit_ex(image, width, height,
                          XImageFormat_ARGB32_Premultiplied)) {
        XImage_delete_base(image);
        return NULL;
    }
    /* 与 grab 快照同语义：缓存画布从全透明开始，未覆盖像素保持 0。 */
    XImage_fillRect(image, NULL, 0u);
    self->m_retainedCache = image;
    g_retainedBytes += need;
    return image;
}

/**
 * @brief 保留层绘制入口（paintTree 效果钩子之前调用）。
 * @details 缓存有效且脏区与控件相交：直接整幅 blit（表面裁剪限幅），
 *          跳过自身与子树的 paintEvent 派发；缓存无效：先经
 *          renderSubtree 同步重渲染进缓存（paintEvent 恰好派发一次，
 *          复用效果钩子的离屏重定向闭环），再 blit。
 *          回贴与效果回贴同一"paintImage+paintOffset+表面裁剪"闭环；
 *          缓存未覆盖像素为全透明，预乘 SourceOver 混合对 alpha=0
 *          幂等，不越界改写目标既有像素——blit 区域与常规渲染逐像素
 *          一致（Porter-Duff Over 结合律：缓存内容=渲染输出 over 透明，
 *          回贴=渲染输出 over 目标既有内容，与直接绘制等价）。
 * @param      widget 保留层控件；不可为 NULL。
 * @param      paintRegion 本控件局部坐标绘制区域；不可为 NULL。
 * @return     true=本子树绘制已完成；false=回退常规绘制路径。
 */
static bool xwidget_paintRetainedLayer(XWidget* widget, const XRegion* paintRegion)
{
    int width = XWidget_width(widget);
    int height = XWidget_height(widget);
    XImage* target;
    if (width <= 0 || height <= 0 || !paintRegion || paintRegion->count <= 0)
        return false;
    /* render/grab 离屏重定向期间不参与：快照恒实时渲染（含自身缓存
       建立的重入——renderSubtree 登记后 paintTree 再次到达本控件时
       经此短路，防自递归）。 */
    if (xwidget_redirectRoot(widget)) return false;
    if (!widget->m_retainedValid || !widget->m_retainedCache ||
        XImage_width(widget->m_retainedCache) != width ||
        XImage_height(widget->m_retainedCache) != height) {
        XImage* cache = xwidget_retainedEnsureCache(widget, width, height);
        if (!cache) {
            /* 分配失败/预算不足：整层放弃保留，退回常规绘制（不阻塞）。 */
            xwidget_retainedDisable(widget);
            return false;
        }
        xwidget_renderSubtree(widget, cache, 0, 0);
        widget->m_retainedValid = true;
    }
    xwidget_retainedTouch(widget);
    target = XWidget_paintImage(widget);
    if (!target) return true; /* 无上屏目标（纯同步派发）：内容已进缓存。 */
    {
        XPainter painter;
        XPoint offset;
        XPainter_init(&painter, NULL);
        if (XPainter_begin_image(&painter, target)) {
            offset = XWidget_paintOffset(widget);
            XPainter_drawImage(&painter, widget->m_retainedCache,
                               offset.x, offset.y);
            XPainter_end(&painter);
        }
        XPainter_deinit(&painter);
    }
    return true;
}

bool XWidget_setContentRetained(XWidget* self, bool on)
{
    XWidgetRetainedNode* node;
    if (!self) return false;
    if (!on) {
        xwidget_retainedDisable(self);
        return false;
    }
    if (self->m_retainedEnabled) {
        xwidget_retainedTouch(self);
        return true;
    }
    /* 与图形效果互斥：保留层缓存的是未施效输出，效果优先（报告取舍：
       开效果前必须先关保留；此处直接拒绝开启请求）。 */
    if (self->m_graphicsEffect &&
        XGraphicsEffect_isEnabled(self->m_graphicsEffect))
        return false;
    if (xwidget_retainedBudget() <= 0) return false;
    /* 顶层窗口暂不参与：其自带后备存储与 DIRECT 整帧缓冲已具备持久
       语义，面板级（子控件）保留才是 HMI 痛点的目标形态。 */
    if (self->m_isWindow) return false;
    node = (XWidgetRetainedNode*)XMemory_malloc(sizeof(XWidgetRetainedNode),
                                                XCLASS_DEFAULT_MEMORY_TYPE);
    if (!node) return false;
    node->m_prev = NULL;
    node->m_next = g_retainedHead;
    if (g_retainedHead) g_retainedHead->m_prev = node;
    g_retainedHead = node;
    if (!g_retainedTail) g_retainedTail = node;
    node->m_widget = self;
    self->m_retainedNode = node;
    ++g_retainedRegistered;
    self->m_retainedEnabled = true;
    self->m_retainedValid = false;
    self->m_retainedStamp = ++g_retainedStampClock;
    return true;
}

void XWidget_invalidateAllRetainedLayers(void)
{
    /* 应用级批量失效（调色板广播等"自上而下"场景）：沿保留层链表
       逐个 update，经 addDirtyRegion→chain 失效使缓存作废并重渲染。 */
    XWidgetRetainedNode* node = g_retainedHead;
    while (node) {
        if (node->m_widget) XWidget_update(node->m_widget);
        node = node->m_next;
    }
}

bool XWidget_contentRetained(const XWidget* self)
{
    return self ? self->m_retainedEnabled : false;
}

void XWidget_retainedLayerStats(size_t* usedBytes, int* count)
{
    if (usedBytes) *usedBytes = g_retainedBytes;
    if (count) *count = g_retainedRegistered;
}

/* ==================== 通知信号（对标 QWidget 信号） ==================== */

/** @brief 信号发射助手：未连接槽时释放参数列表，防止泄漏。 */
static void XWidget_emit(XWidget* self, size_t signal, XVarList* args)
{
    if (self && ((XObject*)self)->m_signalSlot)
        XObject_emitSignal((XObject*)self, signal, args, NULL, NULL,
                           XEVENT_PRIORITY_NORMAL);
    else if (args)
        XVarList_delete(args);
}

void* XWidget_windowTitleChanged_signal(XWidget* self, const XString* title)
{
    XString* value;
    if (!self) return (void*)(size_t)XWidget_windowTitleChanged_signal;
    value = (XString*)title;
    XWidget_emit(self, (size_t)XWidget_windowTitleChanged_signal,
                 XVarList_Create(XVar(XString*, value)));
    return (void*)(size_t)XWidget_windowTitleChanged_signal;
}

void* XWidget_windowIconChanged_signal(XWidget* self, XIcon* icon)
{
    if (!self) return (void*)(size_t)XWidget_windowIconChanged_signal;
    XWidget_emit(self, (size_t)XWidget_windowIconChanged_signal,
                 XVarList_Create(XVar(XIcon*, icon)));
    return (void*)(size_t)XWidget_windowIconChanged_signal;
}

void* XWidget_windowIconTextChanged_signal(XWidget* self, const XString* text)
{
    XString* value;
    if (!self) return (void*)(size_t)XWidget_windowIconTextChanged_signal;
    value = (XString*)text;
    XWidget_emit(self, (size_t)XWidget_windowIconTextChanged_signal,
                 XVarList_Create(XVar(XString*, value)));
    return (void*)(size_t)XWidget_windowIconTextChanged_signal;
}

void* XWidget_customContextMenuRequested_signal(XWidget* self, const XPoint* pos)
{
    XPoint value;
    if (!self) return (void*)(size_t)XWidget_customContextMenuRequested_signal;
    if (pos)
        value = *pos;
    else
        XPoint_init(&value, 0, 0);
    XWidget_emit(self, (size_t)XWidget_customContextMenuRequested_signal,
                 XVarList_Create(XVar(XPoint, value)));
    return (void*)(size_t)XWidget_customContextMenuRequested_signal;
}

/* ==================== 平台/测试接入钩子 ==================== */

void XWidget_applyWindowGeometry(XWidget* self, const XRect* geometry,
                                 const XSize* oldSize)
{
    XRect old;
    bool posChanged;
    bool sizeChanged;
    if (!self || !geometry || !self->m_isWindow) return;
    old = self->m_windowRect;
    self->m_windowRect = *geometry;
    if (self->m_contentsRect.width != geometry->width ||
        self->m_contentsRect.height != geometry->height) {
        self->m_contentsRect.x = 0;
        self->m_contentsRect.y = 0;
        self->m_contentsRect.width = geometry->width;
        self->m_contentsRect.height = geometry->height;
    }
    posChanged = (old.x != geometry->x) || (old.y != geometry->y);
    sizeChanged = (old.width != geometry->width) ||
                  (old.height != geometry->height);
    /* 平台同步同样对应 Qt 的 WA_Moved/WA_Resized 历史状态位；只置位，
     * 不因后续仅改变另一维而丢失已有状态。 */
    if (posChanged)
        XWidget_attrSet(&self->m_attributes, XWidgetAttribute_Moved, true);
    if (sizeChanged)
        XWidget_attrSet(&self->m_attributes, XWidgetAttribute_Resized, true);
    if (posChanged) {
        XEvent event;
        XEvent_init(&event, XEVENT_TYPE_MOVE);
        XWidget_sendEvent(self, &event);
    }
    if (sizeChanged) {
        XResizeEvent event;
        XSize size;
        XSize_init(&size, geometry->width, geometry->height);
        XResizeEvent_init(&event, XEVENT_TYPE_RESIZE, &size, oldSize);
        XWidget_sendEvent(self, (XEvent*)&event);
        XResizeEvent_deinit_base(&event);
    }
}

void XWidget_applyWindowVisibility(XWidget* self, bool visible)
{
    bool oldVisible;
    if (!self || !self->m_isWindow) return;
    oldVisible = (self->m_visible != 0);
    self->m_explicitShow = visible ? 1 : 0;
    self->m_visible = visible ? 1 : 0;
    XWidget_attrSet(&self->m_attributes, XWidgetAttribute_WState_Hidden,
                    !visible);
    if ((self->m_visible != 0) == oldVisible) return;
    if (self->m_visible) {
        XWidget_sendShowHide(self, true);
    } else {
        if (g_focusWidget == self)
            XWidget_clearFocusBase(self, XFocusReason_Other);
        XWidget_sendShowHide(self, false);
    }
    XWidget_propagateVisibility(self, oldVisible);
}



void XWidget_setWindowTitle_2(XWidget* self, const char* utf8)
{
    XString* tmp = NULL;
    if (utf8) {
        tmp = XString_create_utf8(utf8);
        if (!tmp) return;
    }
    XWidget_setWindowTitle(self, tmp);
    if (tmp) XString_delete_base(tmp);
}






































#endif /* XWIDGET_ON */

