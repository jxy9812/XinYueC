/**
 * @file       XStatusBar.c
 * @brief      状态栏控件实现（对标 Qt 6.8 QStatusBar 全部公共 API）。
 * @details    与同名头文件的公共 API 一一对应；内部实现细节见
 *             头文件 @note 与函数级 Doxygen 注释。
 * @author     XinYueC 团队
 */

#include "XStatusBar.h"
#include "XMemory.h"
#include "XEvent.h"
#include "XPainter.h"
#include "XVarList.h"
#include "XGuiConfig.h"

#include "XAlgorithm.h"
#include "XWidget_Protected.h"
#include <stdio.h>

#if XWIDGET_ON && XSTATUSBAR_ON

/* ==================== 内部条目与工具 ==================== */

typedef struct XStatusBarItem
{
    XWidget* widget;   /**< 承载控件（借用，归调用方）。 */
    int stretch;       /**< 拉伸因子（第一版仅存储）。 */
    bool hiddenByMessage; /**< 因临时消息致隐（hideOrShow 记账，
                               仅恢复此类隐藏，不动调用方显式隐藏）。 */
} XStatusBarItem;

static XStatusBarItem* xsb_itemCreate(XWidget* widget, int stretch)
{
    XStatusBarItem* item =
        (XStatusBarItem*)XMalloc_System(sizeof(XStatusBarItem));
    if (!item) return NULL;
    item->widget = widget;
    item->stretch = stretch;
    item->hiddenByMessage = false;
    return item;
}

static void xsb_itemDestroy(XStatusBarItem* item)
{
    if (item) XFree_System(item);
}

/** @brief 从指定数组移除控件条目；返回是否移除。 */
static bool xsb_removeFrom(XVector* vec, XWidget* widget)
{
    int64_t i;
    int64_t n;
    if (!vec || !widget) return false;
    n = XVector_size_base((const XContainer*)vec);
    for (i = 0; i < n; ++i) {
        XStatusBarItem** item =
            (XStatusBarItem**)XVector_at_base(vec, i);
        if (item && *item && (*item)->widget == widget) {
            xsb_itemDestroy(*item);
            XVector_remove_base(vec, i, 1);
            return true;
        }
    }
    return false;
}

static void xsb_destroyVector(XVector* vec)
{
    int64_t i;
    int64_t n;
    if (!vec) return;
    n = XVector_size_base((const XContainer*)vec);
    for (i = 0; i < n; ++i) {
        XStatusBarItem** item =
            (XStatusBarItem**)XVector_at_base(vec, i);
        xsb_itemDestroy(item ? *item : NULL);
    }
    XVector_delete_base(vec);
}

/** @brief sizegrip 角位条带宽度（对标 QStyle::PM_SizeGripSize 的 16px
 *         简化；QStatusBar::reformat 把 d->resizer 以 addWidget 压入
 *         行尾角位，qstatusbar.cpp:465-466）。 */
#define XSTATUSBAR_GRIP_W 16

static void xsb_layoutItems(XStatusBar* self);

/** @brief 常驻控件几何排版（二次复扫 #68 伴随问题根修）。
 *  @details 此前 addWidget/insertWidget 只登记借用记录从不给子控件
 *  几何：标签停在构造默认矩形上，页签容器把状态栏拉伸成整页后该
 *  矩形既不随容器更新也不再被任何布局触达——实测「普通区标签」整页
 *  只余 (25,123) 两个残迹像素（rescan p4_t15 像素表）；面板底
 *  fillRect 在 paintTree「自先子后」的次序下并不会覆盖子控件
 *  （XWidget.c paintEvent 派发先于 children 递归），病根在子控件
 *  从未获得随容器的几何。对标 Qt：qstatusbar.cpp:420-472 reformat()
 *  把普通区/永久区控件装入 QBoxLayout（普通区左起、永久区右置、
 *  sizegrip 压角），resizeEvent 经布局自动重排——本函数即该排版的
 *  简化实现：stretch>0 的条目按拉伸因子分摊弹性宽，stretch<=0 保持
 *  自身现宽，条目占满条高（Preferred 竖直策略控件在行内拉满，与
 *  Qt 状态栏文本垂直居中的经典形态一致）。 */
static void xsb_layoutSection(XVector* vec, int xStart, int xEnd, int h,
                              bool fromRight)
{
    int64_t i;
    int64_t n;
    int flexible = 0;
    int avail;
    int x;
    if (!vec) return;
    n = XVector_size_base((const XContainer*)vec);
    for (i = 0; i < n; ++i) {
        XStatusBarItem** it =
            (XStatusBarItem**)XVector_at_base((const XContainer*)vec, i);
        if (it && *it && (*it)->widget && (*it)->stretch > 0)
            flexible += (*it)->stretch;
    }
    avail = xEnd - xStart;
    if (avail < 0) avail = 0;
    x = fromRight ? xEnd : xStart;
    for (i = 0; i < n; ++i) {
        XStatusBarItem** it =
            (XStatusBarItem**)XVector_at_base((const XContainer*)vec, i);
        XWidget* w;
        int iw;
        XRect r;
        if (!it || !*it) continue;
        w = (*it)->widget;
        if (!w) continue;
        if ((*it)->stretch > 0 && flexible > 0)
            iw = avail * (*it)->stretch / flexible;
        else
            iw = XWidget_width(w);
        if (fromRight) {
            if (iw > x - xStart) iw = x - xStart;
            XRect_init(&r, x - iw, 0, iw > 0 ? iw : 0, h);
        } else {
            if (iw > xEnd - x) iw = xEnd - x;
            XRect_init(&r, x, 0, iw > 0 ? iw : 0, h);
        }
        XWidget_setGeometryRect(w, &r);
        if (fromRight) x -= iw;
        else x += iw;
    }
}

/** @brief 全量排版：永久区占右端（登记序自左向右），普通区占其余
 *         自左向右，sizegrip 角位预留。 */
static void xsb_layoutItems(XStatusBar* self)
{
    int w;
    int h;
    int grip;
    int rightEnd;
    int pw = 0;
    int64_t i;
    int64_t n;
    if (!self) return;
    w = XWidget_width((XWidget*)self);
    h = XWidget_height((XWidget*)self);
    if (w <= 0 || h <= 0) return;
    grip = self->m_sizeGripEnabled ? XSTATUSBAR_GRIP_W : 0;
    if (grip > w) grip = 0;
    rightEnd = w - grip;
    /* 永久区宽度先测（各条目现宽，按登记序紧贴右端）。 */
    n = self->m_permanents
            ? XVector_size_base((const XContainer*)self->m_permanents) : 0;
    for (i = 0; i < n; ++i) {
        XStatusBarItem** it =
            (XStatusBarItem**)XVector_at_base(
                (const XContainer*)self->m_permanents, i);
        if (it && *it && (*it)->widget)
            pw += XWidget_width((*it)->widget);
    }
    if (pw > rightEnd) pw = rightEnd;
    xsb_layoutSection(self->m_permanents, rightEnd - pw, rightEnd, h, true);
    xsb_layoutSection(self->m_items, 0, rightEnd - pw, h, false);
}

/** @brief 取调色板角色颜色（无调色板时回退黑/白）。 */
static uint32_t xsb_color(const XStatusBar* self, XPaletteColorRole role)
{
#if XPALETTE_ON
    XPalette palette = XWidget_palette((XWidget*)self);
    XColor c = XPalette_color(&palette, XPaletteColorGroup_Current, role);
    return XColor_rgba(&c);
#else
    (void)self;
    (void)role;
    return 0xFF000000u;
#endif /* XPALETTE_ON */
}

/* ==================== 事件处理 ==================== */

/** @brief paintEvent：状态条底色 + 顶部 1px 凹槽分隔线 + 消息文本
 *         （普通区被临时消息隐藏期间显示消息，永久区控件保持可见）。 */
static void VX_statusBar_paintEvent(XWidget* self, XEvent* event)
{
    XStatusBar* sb = (XStatusBar*)self;
    XPainter painter;
    XImage* image;
    XPoint offset;
    XRect panel;
    XRect line;
    XRect msgRect;
    uint32_t dark;
    uint32_t button;
    uint32_t text;
    int w;
    int h;
    if (!sb || !event) return;
    w = XWidget_width(self);
    h = XWidget_height(self);
    if (w <= 0 || h <= 0) return;
    image = XWidget_paintImage(self);
    if (!image) return;
    XPainter_init(&painter, NULL);
    if (!XPainter_begin_image(&painter, image)) {
        XPainter_deinit(&painter);
        return;
    }
    offset = XWidget_paintOffset(self);
    if (offset.x != 0 || offset.y != 0)
        XPainter_translate(&painter, (float)offset.x, (float)offset.y);
    dark = xsb_color(sb, XPaletteColorRole_Dark);
    text = xsb_color(sb, XPaletteColorRole_WindowText);
    button = xsb_color(sb, XPaletteColorRole_Button);
    /* 状态条底色（对标 QStatusBar 的 PE_PanelStatusBarSunk 面板底：
     * Button 色铺底；此前仅画 1px 分隔线，被页签容器拉伸后整片透底，
     * 条形感全无——台账 #68）。 */
    XRect_init(&panel, 0, 0, w, h);
    XPainter_fillRect(&painter, &panel, button);
    XRect_init(&line, 0, 0, w, 1);
    XPainter_fillRect(&painter, &line, dark);
    if (sb->m_currentMessage && XString_toUtf8(sb->m_currentMessage) &&
        XString_toUtf8(sb->m_currentMessage)[0] != '\0') {
        XFont font = XWidget_font(self);
        XPainter_setFont(&painter, &font);
        XRect_init(&msgRect, 4, 1, w - 8, h - 2);
        XPainter_drawText(&painter, 4, h - 6,
                          XString_toUtf8(sb->m_currentMessage), text);
        XFont_deinit_base(&font);
    }
    (void)msgRect;
    XPainter_deinit(&painter);
}

/** @brief 消息超时定时器：自动清除临时消息。 */
static void VX_statusBar_timerEvent(XObject* object, XTimerEvent* event)
{
    XStatusBar* sb = (XStatusBar*)object;
    if (!sb || !event) return;
    if (event->timerId == sb->m_messageTimer) {
        XObject_killTimer(object, sb->m_messageTimer);
        sb->m_messageTimer = XTIMER_INVALID_ID;
        XStatusBar_clearMessage(sb);
    }
}

/* ==================== 生命周期与虚表 ==================== */

static void xstatusbar_freeItems(XVector* v)
{
    int64_t i;
    int64_t n;
    if (!v) return;
    n = XVector_size_base((const XContainer*)v);
    for (i = 0; i < n; ++i) {
        XStatusBarItem** it = (XStatusBarItem**)XVector_at_base(
            (const XContainer*)v, i);
        if (it && *it)
            XFree_System(*it);
    }
    XVector_delete_base(v);
}

static void VXStatusBar_deinit(XStatusBar* self)
{
    if (!self) return;
    if (self->m_currentMessage) {
        XString_delete_base(self->m_currentMessage);
        self->m_currentMessage = NULL;
    }
    xstatusbar_freeItems(self->m_items);
    xstatusbar_freeItems(self->m_permanents);
    self->m_items = NULL;
    self->m_permanents = NULL;
    XClass_Deinit_Parent(XWidget, (XWidget*)self);
}

/** @brief resizeEvent：容器尺寸变化后重排普通区/永久区条目（对标
 *         QStatusBar 的 QBoxLayout 随 resizeEvent 自动重排）。 */
static void VX_statusBar_resizeEvent(XWidget* self, XEvent* event)
{
    if (!self) return;
    XClass_Parent(XWidget, EXWidget_ResizeEvent,
                  XWidgetEventSlot)(self, event);
    xsb_layoutItems((XStatusBar*)self);
}

XVtable* XStatusBar_class_init(void)
{
    XVTABLE_INIT_DEFAULT(XStatusBar)
    XVTABLE_INHERIT_XCLASS(XWidget);
    XVTABLE_OVERLOAD_DEFAULT(EXWidget_PaintEvent, VX_statusBar_paintEvent);
    XVTABLE_OVERLOAD_DEFAULT(EXWidget_ResizeEvent, VX_statusBar_resizeEvent);
    XVTABLE_OVERLOAD_DEFAULT(EXObject_TimerEvent, VX_statusBar_timerEvent);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Deinit, VXStatusBar_deinit);
    return XVTABLE_DEFAULT;
}

void XStatusBar_init(XStatusBar* self, XWidget* parent, XWidgetFlags flags)
{
    if (!self) return;
    XMemset(self, 0, sizeof(*self));
    XWidget_init(&self->m_base, parent, flags);
    XClassSetVtable(self, XStatusBar);
    Set_Class_Memory(self, XCLASS_DEFAULT_MEMORY_TYPE);
    Set_Class_IsHeap(self, false);
    self->m_items = XVector_Create(XStatusBarItem*);
    self->m_permanents = XVector_Create(XStatusBarItem*);
    self->m_sizeGripEnabled = true;
    self->m_messageTimer = XTIMER_INVALID_ID;
    /* 对标 QStatusBar 默认固定高度策略的简化：高度 24。 */
    XWidget_resize(self, 200, 24);
}

XStatusBar* XStatusBar_create_ex(XMemoryType memory, XWidget* parent,
                                 XWidgetFlags flags)
{
    XStatusBar* self = (XStatusBar*)XMemory_malloc(sizeof(*self), memory);
    if (!self) return NULL;
    XStatusBar_init(self, parent, flags);
    Set_Class_Memory(self, memory);
    Set_Class_IsHeap(self, true);
    return self;
}

/* ==================== 常驻控件 ==================== */

void XStatusBar_addWidget(XStatusBar* self, XWidget* widget, int stretch)
{
    XStatusBarItem* item;
    if (!self || !widget) return;
    item = xsb_itemCreate(widget, stretch);
    if (!item) return;
    XVector_push_back_1_base(self->m_items, &item);
    XWidget_setParent(widget, (XWidget*)self, 0);
    XWidget_setVisible(widget, !self->m_currentMessage ||
                        !XString_toUtf8(self->m_currentMessage) ||
                        XString_toUtf8(self->m_currentMessage)[0] == '\0');
    xsb_layoutItems(self); /* 对标 Qt：加入即入布局并获得几何。 */
}

int XStatusBar_insertWidget(XStatusBar* self, int index, XWidget* widget,
                            int stretch)
{
    XStatusBarItem* item;
    int64_t n;
    if (!self || !widget) return -1;
    item = xsb_itemCreate(widget, stretch);
    if (!item) return -1;
    n = XVector_size_base((const XContainer*)self->m_items);
    if (index < 0 || (int64_t)index > n) index = (int)n;
    XVector_insert_1_base(self->m_items, index, &item, 1);
    XWidget_setParent(widget, (XWidget*)self, 0);
    /* 对标 Qt 6.8 qstatusbar.cpp QStatusBar::insertWidget：临时消息显示
     * 期间插入的普通区控件需隐藏（与 addWidget 同款消息在场判定；
     * 此前插入路径漏做，控件会叠在消息文本上）。 */
    XWidget_setVisible(widget, !self->m_currentMessage ||
                        !XString_toUtf8(self->m_currentMessage) ||
                        XString_toUtf8(self->m_currentMessage)[0] == '\0');
    xsb_layoutItems(self); /* 对标 Qt：插入即入布局并获得几何。 */
    return index;
}

void XStatusBar_addPermanentWidget(XStatusBar* self, XWidget* widget,
                                   int stretch)
{
    XStatusBarItem* item;
    if (!self || !widget) return;
    item = xsb_itemCreate(widget, stretch);
    if (!item) return;
    XVector_push_back_1_base(self->m_permanents, &item);
    XWidget_setParent(widget, (XWidget*)self, 0);
    /* 对标 Qt 6.8 qstatusbar.cpp insertPermanentWidget：加入永久区即
     * widget->show()（常驻语义，不被临时消息遮挡；hideOrShow 只遍历
     * 普通区，永久区此后不受消息显隐影响）。 */
    XWidget_show(widget);
    xsb_layoutItems(self); /* 对标 Qt：加入即入布局并获得几何。 */
}

int XStatusBar_insertPermanentWidget(XStatusBar* self, int index,
                                     XWidget* widget, int stretch)
{
    XStatusBarItem* item;
    int64_t n;
    if (!self || !widget) return -1;
    item = xsb_itemCreate(widget, stretch);
    if (!item) return -1;
    n = XVector_size_base((const XContainer*)self->m_permanents);
    if (index < 0 || (int64_t)index > n) index = (int)n;
    XVector_insert_1_base(self->m_permanents, index, &item, 1);
    XWidget_setParent(widget, (XWidget*)self, 0);
    /* 同 addPermanentWidget：对标 Qt 6.8 insertPermanentWidget 的
     * widget->show() 常驻语义。 */
    XWidget_show(widget);
    xsb_layoutItems(self); /* 对标 Qt：插入即入布局并获得几何。 */
    return index;
}

void XStatusBar_removeWidget(XStatusBar* self, XWidget* widget)
{
    bool removed;
    if (!self) return;
    removed = xsb_removeFrom(self->m_items, widget);
    if (!removed)
        removed = xsb_removeFrom(self->m_permanents, widget);
    if (removed) {
        /* 对标 Qt：移除后控件归调用方所有，解除父子关系。 */
        XWidget_setParentPlain(widget, NULL);
    }
}

/* ==================== 尺寸手柄 ==================== */

bool XStatusBar_isSizeGripEnabled(const XStatusBar* self)
{
    return self ? self->m_sizeGripEnabled : false;
}

void XStatusBar_setSizeGripEnabled(XStatusBar* self, bool on)
{
    if (!self || self->m_sizeGripEnabled == on) return;
    self->m_sizeGripEnabled = on;
    xsb_layoutItems(self); /* 角位预留变化 → 重排条目。 */
    XWidget_update((XWidget*)self);
}

/* ==================== 消息槽 ==================== */

/** @brief 对标 Qt 6.8 qstatusbar.cpp QStatusBar::hideOrShow：消息显示
 *  期间隐藏普通区（非永久）控件，消息清除后恢复；永久区不受影响。
 *  hiddenByMessage 标记等价 Qt 的 WA_WState_ExplicitShowHide 复位技巧：
 *  只恢复"消息致隐"的控件，调用方显式隐藏（hide）过的不动。 */
static void xsb_hideOrShow(XStatusBar* self, bool haveMessage)
{
    int64_t i;
    int64_t n;
    if (!self || !self->m_items) return;
    n = XVector_size_base((const XContainer*)self->m_items);
    for (i = 0; i < n; ++i) {
        XStatusBarItem** it =
            (XStatusBarItem**)XVector_at_base(
                (const XContainer*)self->m_items, i);
        if (!it || !*it || !(*it)->widget) continue;
        if (haveMessage) {
            if (!(*it)->hiddenByMessage &&
                !XWidget_isHidden((*it)->widget)) {
                (*it)->hiddenByMessage = true;
                XWidget_hide((*it)->widget);
            }
        } else {
            if ((*it)->hiddenByMessage) {
                (*it)->hiddenByMessage = false;
                XWidget_show((*it)->widget);
            }
        }
    }
}

void XStatusBar_showMessage(XStatusBar* self, const char* utf8, int timeout)
{
    const char* s;
    if (!self) return;
    s = (utf8 && utf8[0]) ? utf8 : NULL;
    if (!s) {
        XStatusBar_clearMessage(self);
        return;
    }
    if (!self->m_currentMessage) self->m_currentMessage = XString_create();
    if (self->m_currentMessage)
        XString_assign_utf8(self->m_currentMessage, s);
    self->m_tempTimeout = timeout;
    if (timeout > 0) {
        if (self->m_messageTimer != XTIMER_INVALID_ID)
            XObject_killTimer((XObject*)self, self->m_messageTimer);
        self->m_messageTimer = XObject_startTimer_ms(
            (XObject*)self, (unsigned)timeout, XTimerType_CoarseTimer);
    }
    xsb_hideOrShow(self, true);
    XStatusBar_messageChanged_signal(self, XString_toUtf8(self->m_currentMessage));
    XWidget_update((XWidget*)self);
}

void XStatusBar_clearMessage(XStatusBar* self)
{
    if (!self) return;
    if (self->m_messageTimer != XTIMER_INVALID_ID) {
        XObject_killTimer((XObject*)self, self->m_messageTimer);
        self->m_messageTimer = XTIMER_INVALID_ID;
    }
    /* 对标 Qt 6.8 hideOrShow：messageChanged 无条件发射（空消息再清除
     * 仍发射，此前空串早退为对 Qt 的偏离）；恢复被消息致隐的普通区
     * 控件。 */
    if (self->m_currentMessage)
        XString_assign_utf8(self->m_currentMessage, "");
    xsb_hideOrShow(self, false);
    XStatusBar_messageChanged_signal(self, "");
    XWidget_update((XWidget*)self);
}

const char* XStatusBar_currentMessage(const XStatusBar* self)
{
    const char* text;
    if (!self || !self->m_currentMessage) return "";
    text = XString_toUtf8(self->m_currentMessage);
    return (text && text[0]) ? text : "";
}

/* ==================== 信号 ==================== */

void* XStatusBar_messageChanged_signal(XStatusBar* self, const char* text)
{
    XVarList* args;
    XString* value;
    (void)text;
    if (self && ((XObject*)self)->m_signalSlot) {
        value = XString_create_utf8(text ? text : "");
        args = value ? XVarList_Create(XVar(XString*, value)) : NULL;
        if (args) {
            XObject_emitSignal((XObject*)self,
                               (size_t)XStatusBar_messageChanged_signal,
                               args, NULL, NULL, XEVENT_PRIORITY_NORMAL);
        } else if (value) {
            XString_delete_base((XClass*)value);
        }
    }
    return (void*)(size_t)XStatusBar_messageChanged_signal;
}









#endif /* XWIDGET_ON && XSTATUSBAR_ON */
