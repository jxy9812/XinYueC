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
 *                VXWidget_event；VXWidget_event 按 XEventType 分派到 23 个
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
#include "XWidget_Protected.h"
#include "XVarList.h"
#include <string.h>
#if XWINDOWEVENT_ON
#include "XWindowEvent.h"
#endif /* XWINDOWEVENT_ON */
#include "XGuiApplication.h"
#include "XImage.h"
#include "XPainter.h"
#include "XCoreApplication.h"
#include "XBackingStore.h"
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
static void XRegion_translateInline(XRegion* region, int dx, int dy);

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
    while (w && !w->m_isWindow) {
        w = (const XWidget*)XObject_parent((XObject*)w);
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
           由事件对象复制一次。 */
        return (XEvent*)XPaintEvent_create_ex(
            XCLASS_DEFAULT_MEMORY_TYPE, XEVENT_TYPE_PAINT, &top->m_dirty);
    }
    {
        rect = source->m_contentsRect;
        {
            XRegion region = { &rect, XRect_isEmpty(&rect) ? 0 : 1, 1 };
            event = (XEvent*)XPaintEvent_create_ex(
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
        if (newVisible) {
            XWidget_sendShowHide(self, true);
        } else {
            if (g_focusWidget == self)
                XWidget_clearFocusBase(self, XFocusReason_Other);
            if (g_mouseGrabWidget == self)
                g_mouseGrabWidget = NULL;
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
    memset(win, 0, sizeof(XWidgetWindow));
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
    top = XWidget_topLevel(self);
    if (!top) return;
    offset = XWidget_paintOffset(self);
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
    memset(&policy, 0, sizeof(policy));
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
            if (XEvent_isAccepted(event)) return true;
            if (XWidget_attrTest(&w->m_attributes, XWidgetAttribute_NoMousePropagation)) break;
        }
        if (w == top) break;
        w = (XWidget*)XObject_parent((XObject*)w);
    }
    return XEvent_isAccepted(event);
}

/** @brief 键盘事件投递：优先焦点控件，其次顶层控件。 */
static bool XWidget_dispatchKeyEvent(const XWidget* top, XEvent* event)
{
    XWidget* target;
    if (!top || !event) return false;
    target = g_keyboardGrabWidget;
    if (!target || XWidget_topLevel(target) != top)
        target = g_focusWidget;
    if (!target || XWidget_topLevel(target) != top) target = (XWidget*)top;
    if (target) {
        XWidget_sendEvent(target, event);
        return XEvent_isAccepted(event);
    }
    return XWidget_event_base((XWidget*)top, event);
}

/* ==================== 23 个默认事件槽（对标 QWidget 默认实现） ==================== */

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
    image = XWidget_paintDevice(self);
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

/* ==================== XWidgetWindow 桥接窗口类 ==================== */

XVtable* XWidgetWindow_class_init(void)
{
    XVTABLE_INIT_DEFAULT(XWidgetWindow)
    XVTABLE_INHERIT_XCLASS(XWindow);
    XVTABLE_OVERLOAD_DEFAULT(EXObject_Event, VXWidgetWindow_event);
    return XVTABLE_DEFAULT;
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
        XRegion region;
        /* PAINT 事件已被事件循环取出并开始处理，清掉投递占位位，
           允许本次绘制期间或之后产生的 update() 再次入队。必须先于
           flush 清零：flush 内部 paintEvent 触发的 update 依赖该位为 0
           才能重新投递，否则绘制中产生的脏区会被永久吞掉。 */
        XAtomic_store_int32(&top->m_paintEventPosted, 0,
                            XAtomic_MemoryOrder_Release);
#if XWINDOWEVENT_ON
        /* XPaintEvent_region 已返回拥有的深拷贝，直接交给 flush，避免
           再复制一次区域数组。 */
        region = XPaintEvent_region((XPaintEvent*)event);
#else
        XRegion_init(&region);
        {
            XRect rect = XWidget_rect(top);
            XRegion_addRect(&region, &rect);
        }
#endif /* XWINDOWEVENT_ON */
        XWidget_flushBackingStore(top, &region);
        XRegion_deinit(&region);
        return true;
    }
    case XEVENT_TYPE_MOUSE_BUTTON_PRESS:
    case XEVENT_TYPE_MOUSE_BUTTON_RELEASE:
    case XEVENT_TYPE_MOUSE_BUTTON_DBL_CLICK:
    case XEVENT_TYPE_MOUSE_MOVE:
    case XEVENT_TYPE_WHEEL:
    case XEVENT_TYPE_ENTER:
        return XWidget_dispatchPointerEvent(top, event);
    case XEVENT_TYPE_LEAVE:
        XWidget_clearUnderMouseRecursive(top);
        XWidget_sendEvent(top, event);
        return XEvent_isAccepted(event);
    case XEVENT_TYPE_KEY_PRESS:
    case XEVENT_TYPE_KEY_RELEASE:
    case XEVENT_TYPE_INPUT_METHOD:
        return XWidget_dispatchKeyEvent(top, event);
    case XEVENT_TYPE_DRAG_ENTER:
    case XEVENT_TYPE_DRAG_MOVE:
    case XEVENT_TYPE_DRAG_LEAVE:
    case XEVENT_TYPE_DROP:
        XWidget_sendEvent(top, event);
        return XEvent_isAccepted(event);
    case XEVENT_TYPE_CLOSE:
        XWidget_sendEvent(top, event);
        return XEvent_isAccepted(event);
    default:
        return XWidget_event_base(top, event);
    }
}

/* ==================== XWidget 类初始化与生命周期 ==================== */

XVtable* XWidget_class_init(void)
{
    XVTABLE_INIT_DEFAULT(XWidget)
    XVTABLE_INHERIT_XCLASS(XObject);
    void* table[] = {
        XWidget_paintEvent_default,        /* PaintEvent */
        XWidget_noopEvent_default,         /* ResizeEvent */
        XWidget_noopEvent_default,         /* MoveEvent */
        XWidget_closeEvent_default,        /* CloseEvent */
        XWidget_noopEvent_default,         /* FocusInEvent */
        XWidget_noopEvent_default,         /* FocusOutEvent */
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
        XWidget_noopEvent_default          /* ChangeEvent */
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
    memset(self, 0, sizeof(XWidget));
    XObject_init(&self->m_class);
    ((XObject*)self)->is_widget = 1;
    XClassSetVtable(self, XWidget);
    self->m_windowFlags = flags;
    if (!parent && !(self->m_windowFlags & (XWindowFlags)XWindowType_Window))
        self->m_windowFlags |= (XWindowFlags)XWindowType_Window;
    self->m_isWindow = (!parent ||
                        (self->m_windowFlags & (XWindowFlags)XWindowType_Window)) ? 1 : 0;
    self->m_focusPolicy = XWidgetFocusPolicy_NoFocus;
    /* Qt QWidget 的默认值是 DefaultContextMenu，不是 NoContextMenu。 */
    self->m_contextMenuPolicy = XWidgetContextMenuPolicy_DefaultContextMenu;
    self->m_layoutDirection = XWidgetLayoutDirection_LeftToRight;
    self->m_enabled = 1;
    self->m_updatesEnabled = 1;
    /* QWidgetPrivate::init 设置 WA_WState_Hidden：新控件在显式
     * show() 前保持隐藏，父控件首次显示时不会误显示该子控件。 */
    XWidget_attrSet(&self->m_attributes, XWidgetAttribute_WState_Hidden, true);
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
    XRegion_init(&self->m_mask);
    if (parent) {
        XObject_setParent(&self->m_class, (XObject*)parent);
        self->m_isWindow = (self->m_windowFlags &
                            (XWindowFlags)XWindowType_Window) ? 1 : 0;
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
    memset(self, 0, sizeof(XWidget));
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
    memset(e, 0, sizeof(XFocusProxyEntry));
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
    if (g_keyboardGrabWidget == self)
        g_keyboardGrabWidget = NULL;
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
    XRegion_deinit(&self->m_mask);
#if XBACKINGSTORE_ON && XPLATFORMBACKINGSTORE_ON && XPLATFORMINTEGRATION_ON
    if (self->m_backingStore) {
        XBackingStore_delete_base(self->m_backingStore);
        self->m_backingStore = NULL;
    }
#endif /* XBACKINGSTORE_ON && XPLATFORMBACKINGSTORE_ON && XPLATFORMINTEGRATION_ON */
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
    XWidget_freeContentCache(self);
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
    self->m_windowHandle = other->m_windowHandle; other->m_windowHandle = NULL;
    self->m_backingStore = other->m_backingStore; other->m_backingStore = NULL;
    self->m_contentCache = other->m_contentCache;
    other->m_contentCache = NULL;
    self->m_contentCacheDirty = other->m_contentCacheDirty;
    other->m_contentCacheDirty = true;
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
    memset((char*)other + sizeof(XObject), 0, sizeof(XWidget) - sizeof(XObject));
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
        return true;
    case XEVENT_TYPE_LEAVE:
        XWidget_attrSet(&self->m_attributes, XWidgetAttribute_UnderMouse,
                        false);
        XWidget_leaveEvent_base(self, event);
        return true;
    case XEVENT_TYPE_KEY_PRESS:
        XWidget_keyPressEvent_base(self, event);
        return true;
    case XEVENT_TYPE_KEY_RELEASE:
        XWidget_keyReleaseEvent_base(self, event);
        return true;
    case XEVENT_TYPE_INPUT_METHOD:
        XWidget_inputMethodEvent_base(self, event);
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
    self->m_isWindow = (!(XObject_parent((XObject*)self)) ||
                        (flags & (XWidgetFlags)XWindowType_Window)) ? 1 : 0;
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
    return self ? (XWidget*)XObject_parent((XObject*)self) : NULL;
}

void XWidget_setParent(XWidget* self, XWidget* parent, XWidgetFlags flags)
{
    XWidget* oldParent;
    bool parentChanged;
    bool wasWindow;
    if (!self) return;
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
    self->m_isWindow = (!parent ||
                        (flags & (XWidgetFlags)XWindowType_Window)) ? 1 : 0;
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
        /* QWidget::setParent(QWidget*) 将子控件移到新父控件的 (0,0)，
         * 宽高保持不变；setGeometry 同时发出 MOVE 事件并刷新内容矩形。 */
        XWidget_setGeometry(self, 0, 0, self->m_windowRect.width,
                            self->m_windowRect.height);
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
        if (!widget->m_visible) continue;
        if (!widget->m_explicitShow) continue;
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
    parent = (XWidget*)XObject_parent((XObject*)self);
    while (parent && !parent->m_windowHandle)
        parent = (XWidget*)XObject_parent((XObject*)parent);
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
    return self ? (self->m_explicitShow == 0) : false;
}

bool XWidget_isVisibleTo(const XWidget* self, const XWidget* ancestor)
{
    const XWidget* w;
    if (!self) return false;
    if (!ancestor)
        return XWidget_isVisible(self);
    /* Qt 6.8 的实现不会验证 ancestor 是否确为祖先，也不会检查
     * ancestor 自身的显式隐藏状态：循环只检查 self 到 ancestor
     * 之前的父链，遇到窗口或父链末端即返回当前节点的 isHidden 结果。 */
    w = self;
    while (w && w->m_explicitShow && !w->m_isWindow &&
           XObject_parent((XObject*)w) &&
           XObject_parent((XObject*)w) != (const XObject*)ancestor)
        w = (const XWidget*)XObject_parent((XObject*)w);
    return w && w->m_explicitShow;
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
    return self && self->m_enabled &&
           (self->m_focusPolicy & XWidgetFocusPolicy_TabFocus) != 0 &&
           self->m_explicitShow && !self->m_isWindow;
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

/** @brief 计算焦点链下一个/上一个目标（不改焦点；优先显式 Tab 链，其次文档序）。 */
static XWidget* XWidget_focusChainTarget(XWidget* self, bool forward)
{
    XWidget* top;
    XWidget* linked;
    XVector* list;
    size_t n;
    size_t i;
    size_t cur;
    XWidget* target;
    if (!self) return NULL;
    top = self->m_isWindow ? (XWidget*)self : XWidget_topLevel(self);
    if (!top) return NULL;
    /* 1. 显式 setTabOrder 链：同窗且仍是可聚焦候选时优先使用。 */
    linked = forward ? self->m_focusNext : self->m_focusPrev;
    if (linked && linked != self &&
        XWidget_topLevel(linked) == top &&
        XWidget_focusChainCandidate(linked))
        return linked;
    /* 2. 未设置显式链（或显式链失效）时退回文档顺序。 */
    list = XVector_create(sizeof(XWidget*));
    if (!list) return NULL;
    XWidget_collectTabFocusable((const XWidget*)top, list);
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
    target = NULL;
    for (i = 0; i < n; ++i) {
        size_t idx = forward ? (cur + 1 + i) % n : (cur + n - 1 - i) % n;
        XWidget* candidate = XVector_At_Base(list, (int64_t)idx, XWidget*);
        if (candidate == self) continue;
        target = candidate;
        break;
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
    return self ? (self->m_acceptDrops != 0) : false;
}

void XWidget_setAcceptDrops(XWidget* self, bool enable)
{
    if (!self) return;
    self->m_acceptDrops = enable ? 1 : 0;
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
    memset(&out, 0, sizeof(out));
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
    if (top && top->m_windowHandle)
        XWindow_setCursor((XWindow*)top->m_windowHandle, self->m_cursor);
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
    if (top && top->m_windowHandle)
        XWindow_unsetCursor((XWindow*)top->m_windowHandle);
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

XImage* XWidget_paintDevice(const XWidget* self)
{
#if XBACKINGSTORE_ON && XPLATFORMBACKINGSTORE_ON && XPLATFORMINTEGRATION_ON
    XWidget* top;
    XBackingStore* store;
    if (!self) return NULL;
    top = self->m_isWindow ? (XWidget*)self : XWidget_topLevel(self);
    if (!top) return NULL;
    store = top->m_backingStore;
    return store ? XBackingStore_paintDevice(store) : NULL;
#else
    (void)self;
    return NULL;
#endif /* XBACKINGSTORE_ON && XPLATFORMBACKINGSTORE_ON && XPLATFORMINTEGRATION_ON */
}

XPoint XWidget_paintOffset(const XWidget* self)
{
    XPoint offset = XWidget_accumulateOffset(self);
#if XBACKINGSTORE_ON && XPLATFORMBACKINGSTORE_ON && XPLATFORMINTEGRATION_ON
    XBackingStore* store = XWidget_backingStore(self);
    if (store) {
        XPoint origin = XBackingStore_paintOrigin(store);
        offset.x -= origin.x;
        offset.y -= origin.y;
    }
#endif
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

XImage* XWidget_beginContentCache(XWidget* self, int width, int height)
{
    if (!self || width <= 0 || height <= 0) return NULL;
    if (self->m_contentCache &&
        XImage_width(self->m_contentCache) == width &&
        XImage_height(self->m_contentCache) == height)
        return self->m_contentCache;
    /* 尺寸变化时复用已有离屏缓存对象：XImage_reinit_ex 先构造临时图像
       再移动替换，失败时保留旧内容，且不会重置堆对象的内存方法/所有权
       标记；不要对已持有数据的对象直接调用 XImage_init_ex（会泄漏旧像素
       并清掉堆标记）。 */
    if (self->m_contentCache) {
        self->m_contentCacheDirty = true;
        if (!XImage_reinit_ex(self->m_contentCache, width, height,
                              XImageFormat_ARGB32))
            return NULL;
        self->m_contentCacheDirty = true;
        return self->m_contentCache;
    }
    self->m_contentCache = XImage_create_ex(XCLASS_DEFAULT_MEMORY_TYPE);
    if (!self->m_contentCache)
        return NULL;
    if (!XImage_reinit_ex(self->m_contentCache, width, height,
                          XImageFormat_ARGB32)) {
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
    cache = XWidget_contentCacheImage(self);
    if (XWidget_contentCacheUsable(self, width, height) && cache &&
        !XImage_isNull(cache))
        return XPainter_drawImage(target, cache, x, y);
    cache = XWidget_beginContentCache(self, width, height);
    if (cache)
    {
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

/** @brief 递归绘制控件树：region 为控件本地坐标脏区，裁剪后平移递归子控件。 */
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
    if (widget->m_updatesEnabled && !widget->m_inPaintEvent) {
        XPaintEvent event;
        widget->m_inPaintEvent = 1;
        XPaintEvent_init(&event, XEVENT_TYPE_PAINT, paintRegion);
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
        if (!XBackingStore_paintDevice(store)) {
            XWidget_paintTree(top, &whole);
        }
        else {
        XBackingStore_beginPaint(store, &whole);
#if XGUI_BACKINGSTORE_RENDER_MODE == XGUI_BACKINGSTORE_RENDER_MODE_PARTIAL
        {
            XRect tile;
            XRegion tileRegion;
            XRegion_init(&tileRegion);
            while (XBackingStore_nextTile(store, &tile)) {
                /* XWidget_paintTree 接收的是区域集合。不能把 XRect 直接
                 * 强转传入，否则第二个 tile 的 x/y 会被解释为 count/指针。 */
                XRegion_clear(&tileRegion);
                XRegion_addRect(&tileRegion, &tile);
                XWidget_paintTree(top, &tileRegion);
                XBackingStore_flushTile(store, (XWindow*)top->m_windowHandle,
                                        &tile, NULL);
            }
            XRegion_deinit(&tileRegion);
            XBackingStore_endPaint(store);
        }
#else
        XWidget_paintTree(top, &whole);
        XBackingStore_endPaint(store);
        XBackingStore_flush(store, &whole, (XWindow*)top->m_windowHandle, NULL);
#endif
        }
    }
    XRegion_deinit(&whole);
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

/* ==================== 事件分派（对标 QWidget::event） ==================== */

/*
 * XWidget_event_base 继承自 XObject（对标 QObject::event），按文档约定在
 * XWidget.h 中直接宏复用：XWidget_event_base(self, event) 展开为
 * XObject_event_base((XObject*)(self), (event))，经虚表 EXObject_Event 槽位
 * 分派到 VXWidget_event（按事件类型分发到 23 个控件事件虚函数）。
 */

/* ==================== 23 个公开事件槽入口（XWidget_*_base） ==================== */

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

#endif /* XWIDGET_ON */
