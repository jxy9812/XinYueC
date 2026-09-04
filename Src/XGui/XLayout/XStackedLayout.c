/******************************************************************************
 * @file       XStackedLayout.c
 * @brief      XStackedLayout 堆叠布局实现（对标 Qt 6.8 QStackedLayout）。
 * @details    页面条目统一使用 XLayout 的控件条目工厂和数组助手；当前页
 *             切换、StackOne/StackAll 可见性、尺寸协商和 heightForWidth
 *             均按 Qt 6.8 源码实现。该布局不引入平台 API。
 ******************************************************************************/
#include "XStackedLayout.h"
#include "XLayout_Internal.h"
#include "XLayoutItem_Protected.h"
#include "XMemory.h"
#include <string.h>

#if XLAYOUT_ON && XLAYOUT_STACKED_ON

/** @brief 设置单页模式下的可见状态。 */
static void XStackedLayout_applyStackOne(XStackedLayout* self)
{
    int i;
    if (!self) return;
    for (i = 0; i < self->m_base.m_itemCount; ++i) {
        XWidget* widget = XStackedLayout_widget(self, i);
        if (widget)
            XWidget_setVisible(widget, i == self->m_currentIndex);
    }
}

/** @brief 堆叠布局默认 addItem：只接受承载控件的条目。 */
static void VXStackedLayout_addItem(XLayout* layout, XLayoutItem* item)
{
    XStackedLayout* self = (XStackedLayout*)layout;
    XWidget* widget;
    int index;
    if (!self || !item) return;
    widget = XLayoutItem_widget_base(item);
    if (!widget) return;
    index = XLayout_insertItemAt(&self->m_base, self->m_base.m_itemCount,
                                 item, false);
    if (index < 0) return;
    if (self->m_currentIndex < 0) {
        XStackedLayout_setCurrentIndex(self, index);
    } else {
        if (self->m_stackingMode == XStackedLayoutStackOne)
            XWidget_setVisible(widget, false);
    }
}

/** @brief 堆叠布局 itemAt 继承 XLayout 的数组语义，无需重载。 */

/** @brief 取出页面并维护 currentIndex。 */
static XLayoutItem* VXStackedLayout_takeAt(XLayout* layout, int index)
{
    XStackedLayout* self = (XStackedLayout*)layout;
    XLayoutItem* item;
    int oldCurrent;
    int count;
    XWidget* widget;
    if (!self || index < 0 || index >= self->m_base.m_itemCount)
        return NULL;
    oldCurrent = self->m_currentIndex;
    item = XClass_Parent(XLayout, EXLayout_TakeAt,
                         XLayoutItem*(*)(XLayout*, int))(layout, index);
    if (!item) return NULL;
    count = self->m_base.m_itemCount;
    if (index == oldCurrent) {
        self->m_currentIndex = -1;
        if (count > 0) {
            XStackedLayout_setCurrentIndex(
                self, (index == count) ? count - 1 : index);
        } else {
            XStackedLayout_currentChanged_signal(self, -1);
        }
    } else if (index < oldCurrent) {
        self->m_currentIndex = oldCurrent - 1;
    }
    widget = XLayoutItem_widget_base(item);
    if (widget)
        XWidget_setVisible(widget, false);
    XStackedLayout_widgetRemoved_signal(self, index);
    return item;
}

/** @brief 堆叠布局尺寸提示：取所有页面首选尺寸的逐项最大值。 */
static XSize VXStackedLayout_sizeHint(const XLayoutItem* item)
{
    const XStackedLayout* self = (const XStackedLayout*)item;
    XSize out;
    int i;
    XSize_init(&out, 0, 0);
    if (!self) return out;
    for (i = 0; i < self->m_base.m_itemCount; ++i) {
        XWidget* widget = XStackedLayout_widget(self, i);
        XSize size;
        XWidgetSizePolicy policy;
        if (!widget) continue;
        size = XWidget_sizeHint(widget);
        policy = XWidget_sizePolicy(widget);
        if (XWidgetSizePolicy_horizontalPolicy(&policy) ==
            XWidgetSizePolicy_Ignored)
            size.width = 0;
        if (XWidgetSizePolicy_verticalPolicy(&policy) ==
            XWidgetSizePolicy_Ignored)
            size.height = 0;
        out = XSize_expandedTo(&out, &size);
    }
    return out;
}

/** @brief 堆叠布局最小尺寸：取所有页面智能最小尺寸的逐项最大值。 */
static XSize VXStackedLayout_minimumSize(const XLayoutItem* item)
{
    const XStackedLayout* self = (const XStackedLayout*)item;
    XSize out;
    int i;
    XSize_init(&out, 0, 0);
    if (!self) return out;
    for (i = 0; i < self->m_base.m_itemCount; ++i) {
        XWidget* widget = XStackedLayout_widget(self, i);
        XSize size;
        XSize hint;
        XSize max;
        if (!widget) continue;
        size = XWidget_minimumSize(widget);
        hint = XWidget_minimumSizeHint(widget);
        if (size.width < hint.width) size.width = hint.width;
        if (size.height < hint.height) size.height = hint.height;
        max = XWidget_maximumSize(widget);
        if (max.width >= 0 && size.width > max.width) size.width = max.width;
        if (max.height >= 0 && size.height > max.height) size.height = max.height;
        if (size.width < 0) size.width = 0;
        if (size.height < 0) size.height = 0;
        out = XSize_expandedTo(&out, &size);
    }
    return out;
}

/** @brief 按当前堆叠模式把几何矩形分配给页面。 */
static void VXStackedLayout_setGeometry(XLayout* layout, const XRect* rect)
{
    XStackedLayout* self = (XStackedLayout*)layout;
    int i;
    if (!self || !rect) return;
    self->m_base.m_base.m_geometry = *rect;
    if (self->m_stackingMode == XStackedLayoutStackOne) {
        XWidget* widget = XStackedLayout_currentWidget(self);
        if (widget)
            XWidget_setGeometryRect(widget, rect);
    } else {
        for (i = 0; i < self->m_base.m_itemCount; ++i) {
            XWidget* widget = XStackedLayout_widget(self, i);
            if (widget)
                XWidget_setGeometryRect(widget, rect);
        }
    }
    self->m_base.m_isDirty = 0;
    self->m_base.m_activated = 1;
}

/** @brief 检查任一页面是否支持 heightForWidth。 */
static bool VXStackedLayout_hasHeightForWidth(const XLayoutItem* item)
{
    const XStackedLayout* self = (const XStackedLayout*)item;
    int i;
    if (!self) return false;
    for (i = 0; i < self->m_base.m_itemCount; ++i) {
        XLayoutItem* child = self->m_base.m_items[i];
        if (child && XLayoutItem_hasHeightForWidth_base(child))
            return true;
    }
    return false;
}

/** @brief 直接询问每个页面控件并返回最大 heightForWidth。 */
static int VXStackedLayout_heightForWidth(const XLayoutItem* item, int width)
{
    const XStackedLayout* self = (const XStackedLayout*)item;
    int result = 0;
    int i;
    XSize minimum;
    if (!self) return 0;
    for (i = 0; i < self->m_base.m_itemCount; ++i) {
        XWidget* widget = XStackedLayout_widget(self, i);
        int height;
        if (!widget) continue;
        height = XWidget_heightForWidth(widget, width);
        if (height > result) result = height;
    }
    minimum = VXStackedLayout_minimumSize(item);
    if (result < minimum.height) result = minimum.height;
    return result;
}

/** @brief 释放堆叠布局资源；页面数组由 XLayout 父类负责。 */
static void VXStackedLayout_deinit(XStackedLayout* self)
{
    if (!self) return;
    XClass_Deinit_Parent(XLayout, (XLayout*)self);
}

/** @brief 拷贝布局配置；Qt 堆叠布局不可复制页面树，索引回到 -1。 */
static void VXStackedLayout_copy(XStackedLayout* self,
                                 const XStackedLayout* other)
{
    if (!self || !other || self == other) return;
    if (XClassIsVtableNull(self)) XStackedLayout_init(self);
    XClass_Parent(XLayout, EXClass_Copy,
                  void(*)(XLayout*, const XLayout*))((XLayout*)self,
                                                     (const XLayout*)other);
    self->m_currentIndex = -1;
    self->m_stackingMode = other->m_stackingMode;
}

/** @brief 移动布局配置和页面条目。 */
static void VXStackedLayout_move(XStackedLayout* self,
                                 XStackedLayout* other)
{
    if (!self || !other || self == other) return;
    if (XClassIsVtableNull(self)) XStackedLayout_init(self);
    XClass_Parent(XLayout, EXClass_Move,
                  void(*)(XLayout*, XLayout*))((XLayout*)self,
                                                (XLayout*)other);
    self->m_currentIndex = other->m_currentIndex;
    self->m_stackingMode = other->m_stackingMode;
    other->m_currentIndex = -1;
    other->m_stackingMode = XStackedLayoutStackOne;
}

XVtable* XStackedLayout_class_init(void)
{
    XVTABLE_INIT_DEFAULT(XStackedLayout)
    XVTABLE_INHERIT_XCLASS(XLayout);
    XVTABLE_OVERLOAD_DEFAULT(EXLayout_AddItem, VXStackedLayout_addItem);
    XVTABLE_OVERLOAD_DEFAULT(EXLayoutItem_SizeHint, VXStackedLayout_sizeHint);
    XVTABLE_OVERLOAD_DEFAULT(EXLayoutItem_MinimumSize,
                             VXStackedLayout_minimumSize);
    XVTABLE_OVERLOAD_DEFAULT(EXLayoutItem_SetGeometry,
                             VXStackedLayout_setGeometry);
    XVTABLE_OVERLOAD_DEFAULT(EXLayoutItem_HasHeightForWidth,
                             VXStackedLayout_hasHeightForWidth);
    XVTABLE_OVERLOAD_DEFAULT(EXLayoutItem_HeightForWidth,
                             VXStackedLayout_heightForWidth);
    XVTABLE_OVERLOAD_DEFAULT(EXLayout_TakeAt, VXStackedLayout_takeAt);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Deinit, VXStackedLayout_deinit);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Copy, VXStackedLayout_copy);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Move, VXStackedLayout_move);
    return XVTABLE_DEFAULT;
}

void XStackedLayout_init(XStackedLayout* self)
{
    if (!self) return;
    memset(self, 0, sizeof(XStackedLayout));
    XLayout_init(&self->m_base);
    XClassSetVtable(self, XStackedLayout);
    self->m_currentIndex = -1;
    self->m_stackingMode = XStackedLayoutStackOne;
}

XStackedLayout* XStackedLayout_create(XWidget* parent)
{
    XStackedLayout* self = (XStackedLayout*)XMalloc_System(
        sizeof(XStackedLayout));
    if (!self) return NULL;
    memset(self, 0, sizeof(XStackedLayout));
    XStackedLayout_init(self);
    Set_Class_IsHeap(self, true);
    if (parent)
        XWidget_setLayout(parent, (XLayout*)self);
    return self;
}

int XStackedLayout_addWidget(XStackedLayout* self, XWidget* widget)
{
    if (!self || !widget) return -1;
    return XStackedLayout_insertWidget(self, self->m_base.m_itemCount,
                                       widget);
}

int XStackedLayout_insertWidget(XStackedLayout* self, int index,
                                XWidget* widget)
{
    XLayoutItem* item;
    int actual;
    int oldCurrent;
    if (!self || !widget) return -1;
    oldCurrent = self->m_currentIndex;
    item = XLayoutItem_createWidgetItem(widget);
    if (!item) return -1;
    actual = XLayout_insertItemAt(&self->m_base, index, item, true);
    if (actual < 0) {
        XLayoutItem_delete_base(item);
        return -1;
    }
    if (oldCurrent < 0) {
        XStackedLayout_setCurrentIndex(self, actual);
    } else {
        if (actual <= oldCurrent)
            self->m_currentIndex = oldCurrent + 1;
        if (self->m_stackingMode == XStackedLayoutStackOne)
            XWidget_setVisible(widget, false);
    }
    return actual;
}

void XStackedLayout_addItem(XStackedLayout* self, XLayoutItem* item)
{
    XLayout_addItem_base((XLayout*)self, item);
}

XWidget* XStackedLayout_currentWidget(const XStackedLayout* self)
{
    if (!self) return NULL;
    return XStackedLayout_widget(self, self->m_currentIndex);
}

int XStackedLayout_currentIndex(const XStackedLayout* self)
{
    return self ? self->m_currentIndex : -1;
}

XWidget* XStackedLayout_widget(const XStackedLayout* self, int index)
{
    XLayoutItem* item;
    if (!self || index < 0 || index >= self->m_base.m_itemCount)
        return NULL;
    item = self->m_base.m_items[index];
    return item ? XLayoutItem_widget_base(item) : NULL;
}

int XStackedLayout_count(const XStackedLayout* self)
{
    return self ? self->m_base.m_itemCount : 0;
}

void XStackedLayout_setCurrentIndex(XStackedLayout* self, int index)
{
    XWidget* previous;
    XWidget* next;
    if (!self) return;
    next = XStackedLayout_widget(self, index);
    previous = XStackedLayout_currentWidget(self);
    if (!next || next == previous) return;
    if (previous) {
        XWidget_clearFocus(previous);
        if (self->m_stackingMode == XStackedLayoutStackOne)
            XWidget_setVisible(previous, false);
    }
    self->m_currentIndex = index;
    XWidget_setVisible(next, true);
    XStackedLayout_currentChanged_signal(self, index);
}

void XStackedLayout_setCurrentWidget(XStackedLayout* self, XWidget* widget)
{
    int index;
    if (!self || !widget) return;
    index = XLayout_indexOf((const XLayout*)self, widget);
    if (index >= 0)
        XStackedLayout_setCurrentIndex(self, index);
}

XStackedLayoutStackingMode XStackedLayout_stackingMode(
    const XStackedLayout* self)
{
    return self ? self->m_stackingMode : XStackedLayoutStackOne;
}

void XStackedLayout_setStackingMode(XStackedLayout* self,
                                    XStackedLayoutStackingMode mode)
{
    XRect geometry;
    XWidget* currentWidget;
    int i;
    int current;
    if (!self) return;
    if (mode != XStackedLayoutStackOne && mode != XStackedLayoutStackAll)
        return;
    if (self->m_stackingMode == mode) return;
    self->m_stackingMode = mode;
    if (self->m_base.m_itemCount == 0) return;
    current = self->m_currentIndex;
    if (mode == XStackedLayoutStackOne) {
        /* Qt 6.8 当前实现使用 if (const int idx = currentIndex())，
         * 因此索引 0 的分支保持其可观察行为。 */
        if (current != 0)
            XStackedLayout_applyStackOne(self);
    } else {
        currentWidget = XStackedLayout_currentWidget(self);
        if (currentWidget)
            geometry = XWidget_geometry(currentWidget);
        else
            XRect_init(&geometry, 0, 0, 0, 0);
        for (i = 0; i < self->m_base.m_itemCount; ++i) {
            XWidget* widget = XStackedLayout_widget(self, i);
            if (!widget) continue;
            if (geometry.width > 0 && geometry.height > 0)
                XWidget_setGeometryRect(widget, &geometry);
            XWidget_setVisible(widget, true);
        }
    }
}

void* XStackedLayout_currentChanged_signal(XStackedLayout* self, int index)
{
    (void)index;
    return (void*)(size_t)XStackedLayout_currentChanged_signal;
}

void* XStackedLayout_widgetRemoved_signal(XStackedLayout* self, int index)
{
    (void)self;
    (void)index;
    return (void*)(size_t)XStackedLayout_widgetRemoved_signal;
}

#endif /* XLAYOUT_ON && XLAYOUT_STACKED_ON */
