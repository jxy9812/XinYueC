/******************************************************************************
 * @file       XWindowEvent.c
 * @brief      窗口事件类合集实现（对标 Qt 6.8 qevent.h）。
 * @details    实现 XResizeEvent / XExposeEvent / XPaintEvent / XCloseEvent /
 *             XShowEvent / XHideEvent / XFocusEvent / XWheelEvent /
 *             XEnterEvent / XInputMethodEvent 的创建、初始化、访问器与虚表：
 *             - 无动态成员的事件（Resize/Close/Show/Hide/Focus）沿用 XEvent
 *               的释放路径，仅重载 Clone 克隆基类字段；
 *             - 带 XRegion 的事件（Expose/Paint）重载 Deinit 释放内部区域，
 *               重载 Clone 深拷贝区域，保证事件队列/副本生命周期独立。
 *             本文件不依赖任何平台 API，嵌入式可用。
 * @note       模块总开关 XWINDOWEVENT_ON 定义于 XGuiConfig.h；置 0 时
 *             本文件实现体整体裁剪。
 * @author     XinYueC 团队
 ******************************************************************************/
#include "XWindowEvent.h"

#include "XAlgorithm.h"
#include "XMemory.h"
#include "XPrintf.h"

#if XWINDOWEVENT_ON

/* ==================== 虚函数实现（Clone / Deinit） ==================== */

/** @brief 复制 XEvent 基类值字段（克隆辅助；不含 XClass 管理字段）。 */
static void xevent_clone_base(XEvent* dst, const XEvent* src)
{
    dst->type = src->type;
    dst->reserved = src->reserved;
    dst->accepted = src->accepted;
    dst->spontaneous = src->spontaneous;
    dst->posted = src->posted;
    dst->input_event = src->input_event;
    dst->pointer_event = src->pointer_event;
    dst->single_point_event = src->single_point_event;
}

static XEvent* VXResizeEvent_clone(const XResizeEvent* event)
{
    XResizeEvent* copy = XClass_Malloc(XResizeEvent);
    if (!copy) return NULL;
    XClassSetVtable(copy, XResizeEvent);
    xevent_clone_base((XEvent*)copy, (const XEvent*)event);
    copy->m_size = event->m_size;
    copy->m_oldSize = event->m_oldSize;
    copy->m_normalSize = event->m_normalSize;
    copy->m_normalOldSize = event->m_normalOldSize;
    Set_Class_Memory(copy, XCLASS_DEFAULT_MEMORY_TYPE);
    Set_Class_IsHeap(copy, true);
    return (XEvent*)copy;
}

static void VXExposeEvent_deinit(XExposeEvent* self)
{
    if (!self) return;
    XRegion_deinit(&self->m_region);
    /* 父类 XEvent 无动态资源；显式走父表保证派生链完整。 */
    XClass_Deinit_Parent(XEvent, (XEvent*)self);
}

static XEvent* VXExposeEvent_clone(const XExposeEvent* event)
{
    XExposeEvent* copy = XClass_Malloc(XExposeEvent);
    if (!copy) return NULL;
    XClassSetVtable(copy, XExposeEvent);
    xevent_clone_base((XEvent*)copy, (const XEvent*)event);
    /* 区域为动态资源：初始化后深拷贝，保证副本 deinit 不误释放源。 */
    XRegion_init(&copy->m_region);
    XRegion_copy(&event->m_region, &copy->m_region);
    Set_Class_Memory(copy, XCLASS_DEFAULT_MEMORY_TYPE);
    Set_Class_IsHeap(copy, true);
    return (XEvent*)copy;
}

static void VXPaintEvent_deinit(XPaintEvent* self)
{
    if (!self) return;
    XRegion_deinit(&self->m_region);
    XClass_Deinit_Parent(XEvent, (XEvent*)self);
}

static XEvent* VXPaintEvent_clone(const XPaintEvent* event)
{
    XPaintEvent* copy = XClass_Malloc(XPaintEvent);
    if (!copy) return NULL;
    XClassSetVtable(copy, XPaintEvent);
    xevent_clone_base((XEvent*)copy, (const XEvent*)event);
    XRegion_init(&copy->m_region);
    XRegion_copy(&event->m_region, &copy->m_region);
    copy->m_rect = event->m_rect; /* 值类型，显式复制。 */
    Set_Class_Memory(copy, XCLASS_DEFAULT_MEMORY_TYPE);
    Set_Class_IsHeap(copy, true);
    return (XEvent*)copy;
}

static XEvent* VXCloseEvent_clone(const XCloseEvent* event)
{
    XCloseEvent* copy = XClass_Malloc(XCloseEvent);
    if (!copy) return NULL;
    XClassSetVtable(copy, XCloseEvent);
    xevent_clone_base((XEvent*)copy, (const XEvent*)event);
    Set_Class_Memory(copy, XCLASS_DEFAULT_MEMORY_TYPE);
    Set_Class_IsHeap(copy, true);
    return (XEvent*)copy;
}

static XEvent* VXShowEvent_clone(const XShowEvent* event)
{
    XShowEvent* copy = XClass_Malloc(XShowEvent);
    if (!copy) return NULL;
    XClassSetVtable(copy, XShowEvent);
    xevent_clone_base((XEvent*)copy, (const XEvent*)event);
    Set_Class_Memory(copy, XCLASS_DEFAULT_MEMORY_TYPE);
    Set_Class_IsHeap(copy, true);
    return (XEvent*)copy;
}

static XEvent* VXHideEvent_clone(const XHideEvent* event)
{
    XHideEvent* copy = XClass_Malloc(XHideEvent);
    if (!copy) return NULL;
    XClassSetVtable(copy, XHideEvent);
    xevent_clone_base((XEvent*)copy, (const XEvent*)event);
    Set_Class_Memory(copy, XCLASS_DEFAULT_MEMORY_TYPE);
    Set_Class_IsHeap(copy, true);
    return (XEvent*)copy;
}

static XEvent* VXFocusEvent_clone(const XFocusEvent* event)
{
    XFocusEvent* copy = XClass_Malloc(XFocusEvent);
    if (!copy) return NULL;
    XClassSetVtable(copy, XFocusEvent);
    xevent_clone_base((XEvent*)copy, (const XEvent*)event);
    copy->m_reason = event->m_reason;
    Set_Class_Memory(copy, XCLASS_DEFAULT_MEMORY_TYPE);
    Set_Class_IsHeap(copy, true);
    return (XEvent*)copy;
}

static void VXInputMethodEvent_deinit(XInputMethodEvent* self)
{
    if (!self) return;
    if (self->m_preeditString) XString_delete_base((XClass*)self->m_preeditString);
    if (self->m_commitString) XString_delete_base((XClass*)self->m_commitString);
    self->m_preeditString = NULL;
    self->m_commitString = NULL;
    XClass_Deinit_Parent(XEvent, (XEvent*)self);
}

static XEvent* VXInputMethodEvent_clone(const XInputMethodEvent* event)
{
    XInputMethodEvent* copy;
    if (!event) return NULL;
    copy = XClass_Malloc(XInputMethodEvent);
    if (!copy) return NULL;
    XInputMethodEvent_init(copy, event->m_preeditString, event->m_commitString,
                           event->m_replacementStart,
                           event->m_replacementLength,
                           event->m_cursorPosition,
                           event->m_anchorPosition);
    Set_Class_Memory(copy, XCLASS_DEFAULT_MEMORY_TYPE);
    Set_Class_IsHeap(copy, true);
    return (XEvent*)copy;
}

static void VXDropEvent_deinit(XDropEvent* self)
{
    if (!self) return;
    if (self->m_mimeType) XString_delete_base((XClass*)self->m_mimeType);
    if (self->m_data) XString_delete_base((XClass*)self->m_data);
    self->m_mimeType = NULL;
    self->m_data = NULL;
    XClass_Deinit_Parent(XEvent, (XEvent*)self);
}

static XEvent* VXDropEvent_clone(const XDropEvent* event)
{
    XDropEvent* copy;
    if (!event) return NULL;
    copy = XClass_Malloc(XDropEvent);
    if (!copy) return NULL;
    XDropEvent_init(copy, event->m_class.type, &event->m_position,
                    &event->m_globalPosition, event->m_mimeType,
                    event->m_data);
    copy->m_dropAction = event->m_dropAction;
    copy->m_possibleActions = event->m_possibleActions;
    Set_Class_Memory(copy, XCLASS_DEFAULT_MEMORY_TYPE);
    Set_Class_IsHeap(copy, true);
    return (XEvent*)copy;
}

/* ==================== 类虚函数表初始化 ==================== */

XVtable* XResizeEvent_class_init(void)
{
    XVTABLE_INIT_DEFAULT(XResizeEvent)
    XVTABLE_INHERIT_XCLASS(XEvent);
    XVTABLE_OVERLOAD_DEFAULT(EXEvent_Clone, VXResizeEvent_clone);
    return XVTABLE_DEFAULT;
}

XVtable* XExposeEvent_class_init(void)
{
    XVTABLE_INIT_DEFAULT(XExposeEvent)
    XVTABLE_INHERIT_XCLASS(XEvent);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Deinit, VXExposeEvent_deinit);
    XVTABLE_OVERLOAD_DEFAULT(EXEvent_Clone, VXExposeEvent_clone);
    return XVTABLE_DEFAULT;
}

XVtable* XPaintEvent_class_init(void)
{
    XVTABLE_INIT_DEFAULT(XPaintEvent)
    XVTABLE_INHERIT_XCLASS(XEvent);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Deinit, VXPaintEvent_deinit);
    XVTABLE_OVERLOAD_DEFAULT(EXEvent_Clone, VXPaintEvent_clone);
    return XVTABLE_DEFAULT;
}

XVtable* XCloseEvent_class_init(void)
{
    XVTABLE_INIT_DEFAULT(XCloseEvent)
    XVTABLE_INHERIT_XCLASS(XEvent);
    XVTABLE_OVERLOAD_DEFAULT(EXEvent_Clone, VXCloseEvent_clone);
    return XVTABLE_DEFAULT;
}

XVtable* XShowEvent_class_init(void)
{
    XVTABLE_INIT_DEFAULT(XShowEvent)
    XVTABLE_INHERIT_XCLASS(XEvent);
    XVTABLE_OVERLOAD_DEFAULT(EXEvent_Clone, VXShowEvent_clone);
    return XVTABLE_DEFAULT;
}

XVtable* XHideEvent_class_init(void)
{
    XVTABLE_INIT_DEFAULT(XHideEvent)
    XVTABLE_INHERIT_XCLASS(XEvent);
    XVTABLE_OVERLOAD_DEFAULT(EXEvent_Clone, VXHideEvent_clone);
    return XVTABLE_DEFAULT;
}

XVtable* XFocusEvent_class_init(void)
{
    XVTABLE_INIT_DEFAULT(XFocusEvent)
    XVTABLE_INHERIT_XCLASS(XEvent);
    XVTABLE_OVERLOAD_DEFAULT(EXEvent_Clone, VXFocusEvent_clone);
    return XVTABLE_DEFAULT;
}

XVtable* XInputMethodEvent_class_init(void)
{
    XVTABLE_INIT_DEFAULT(XInputMethodEvent)
    XVTABLE_INHERIT_XCLASS(XEvent);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Deinit, VXInputMethodEvent_deinit);
    XVTABLE_OVERLOAD_DEFAULT(EXEvent_Clone, VXInputMethodEvent_clone);
    return XVTABLE_DEFAULT;
}

XVtable* XDropEvent_class_init(void)
{
    XVTABLE_INIT_DEFAULT(XDropEvent)
    XVTABLE_INHERIT_XCLASS(XEvent);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Deinit, VXDropEvent_deinit);
    XVTABLE_OVERLOAD_DEFAULT(EXEvent_Clone, VXDropEvent_clone);
    return XVTABLE_DEFAULT;
}

/* ==================== XResizeEvent ==================== */

XResizeEvent* XResizeEvent_create_ex(XMemoryType memory, XEventType type,
                                     const XSize* size, const XSize* oldSize)
{
    XResizeEvent* event = XMemory_malloc(sizeof(XResizeEvent), memory);
    if (!event) return NULL;
    XResizeEvent_init(event, type, size, oldSize);
    Set_Class_Memory(event, memory);
    Set_Class_IsHeap(event, true);
    return event;
}

void XResizeEvent_init(XResizeEvent* event, XEventType type,
                       const XSize* size, const XSize* oldSize)
{
    if (!event) return;
    XEvent_init((XEvent*)event, type);
    XClassGetVtable(event) = XResizeEvent_class_init();
    event->m_size = size ? *size : (XSize){ 0, 0 };
    event->m_oldSize = oldSize ? *oldSize : (XSize){ 0, 0 };
    /* 无平台边框信息时，正常尺寸默认等于实际尺寸（Qt 语义）。 */
    event->m_normalSize = event->m_size;
    event->m_normalOldSize = event->m_oldSize;
}

XSize XResizeEvent_size(const XResizeEvent* event)
{
    XSize out = { 0, 0 };
    return event ? event->m_size : out;
}

XSize XResizeEvent_oldSize(const XResizeEvent* event)
{
    XSize out = { 0, 0 };
    return event ? event->m_oldSize : out;
}

XSize XResizeEvent_normalSize(const XResizeEvent* event)
{
    XSize out = { 0, 0 };
    return event ? event->m_normalSize : out;
}

XSize XResizeEvent_normalOldSize(const XResizeEvent* event)
{
    XSize out = { 0, 0 };
    return event ? event->m_normalOldSize : out;
}

/* ==================== XExposeEvent ==================== */

XExposeEvent* XExposeEvent_create_ex(XMemoryType memory, XEventType type,
                                     const XRegion* region)
{
    XExposeEvent* event = XMemory_malloc(sizeof(XExposeEvent), memory);
    if (!event) return NULL;
    XExposeEvent_init(event, type, region);
    Set_Class_Memory(event, memory);
    Set_Class_IsHeap(event, true);
    return event;
}

void XExposeEvent_init(XExposeEvent* event, XEventType type, const XRegion* region)
{
    if (!event) return;
    XEvent_init((XEvent*)event, type);
    XClassGetVtable(event) = XExposeEvent_class_init();
    XRegion_init(&event->m_region);
    if (region) XRegion_copy(region, &event->m_region);
}

XRegion XExposeEvent_region(const XExposeEvent* event)
{
    XRegion out;
    XRegion_init(&out);
    if (event) XRegion_copy(&event->m_region, &out);
    return out;
}

/* ==================== XPaintEvent ==================== */

XPaintEvent* XPaintEvent_create_ex(XMemoryType memory, XEventType type,
                                   const XRegion* region)
{
    XPaintEvent* event = XMemory_malloc(sizeof(XPaintEvent), memory);
    if (!event) return NULL;
    XPaintEvent_init(event, type, region);
    Set_Class_Memory(event, memory);
    Set_Class_IsHeap(event, true);
    return event;
}

void XPaintEvent_init(XPaintEvent* event, XEventType type, const XRegion* region)
{
    if (!event) return;
    XEvent_init((XEvent*)event, type);
    XClassGetVtable(event) = XPaintEvent_class_init();
    XRegion_init(&event->m_region);
    if (region) XRegion_copy(region, &event->m_region);
    XRegion_boundingRect(&event->m_region, &event->m_rect);
}

XRegion XPaintEvent_region(const XPaintEvent* event)
{
    XRegion out;
    XRegion_init(&out);
    if (event) XRegion_copy(&event->m_region, &out);
    return out;
}

XRect XPaintEvent_rect(const XPaintEvent* event)
{
    XRect out;
    XRect_init(&out, 0, 0, 0, 0);
    return event ? event->m_rect : out;
}

/* ==================== XCloseEvent ==================== */

XCloseEvent* XCloseEvent_create_ex(XMemoryType memory, XEventType type)
{
    XCloseEvent* event = XMemory_malloc(sizeof(XCloseEvent), memory);
    if (!event) return NULL;
    XCloseEvent_init(event, type);
    Set_Class_Memory(event, memory);
    Set_Class_IsHeap(event, true);
    return event;
}

void XCloseEvent_init(XCloseEvent* event, XEventType type)
{
    if (!event) return;
    XEvent_init((XEvent*)event, type);
    XClassGetVtable(event) = XCloseEvent_class_init();
    /* 对齐 Qt：QEvent 默认 accepted=true，未重载 close 槽时默认允许关闭；
       应用在 close 槽调用 XEvent_ignore 可拒绝关闭。 */
    event->m_class.accepted = true;
}

/* ==================== XShowEvent ==================== */

XShowEvent* XShowEvent_create_ex(XMemoryType memory, XEventType type)
{
    XShowEvent* event = XMemory_malloc(sizeof(XShowEvent), memory);
    if (!event) return NULL;
    XShowEvent_init(event, type);
    Set_Class_Memory(event, memory);
    Set_Class_IsHeap(event, true);
    return event;
}

void XShowEvent_init(XShowEvent* event, XEventType type)
{
    if (!event) return;
    XEvent_init((XEvent*)event, type);
    XClassGetVtable(event) = XShowEvent_class_init();
}

/* ==================== XHideEvent ==================== */

XHideEvent* XHideEvent_create_ex(XMemoryType memory, XEventType type)
{
    XHideEvent* event = XMemory_malloc(sizeof(XHideEvent), memory);
    if (!event) return NULL;
    XHideEvent_init(event, type);
    Set_Class_Memory(event, memory);
    Set_Class_IsHeap(event, true);
    return event;
}

void XHideEvent_init(XHideEvent* event, XEventType type)
{
    if (!event) return;
    XEvent_init((XEvent*)event, type);
    XClassGetVtable(event) = XHideEvent_class_init();
}

/* ==================== XFocusEvent ==================== */

XFocusEvent* XFocusEvent_create_ex(XMemoryType memory, XEventType type,
                                   XFocusReason reason)
{
    XFocusEvent* event = XMemory_malloc(sizeof(XFocusEvent), memory);
    if (!event) return NULL;
    XFocusEvent_init(event, type, reason);
    Set_Class_Memory(event, memory);
    Set_Class_IsHeap(event, true);
    return event;
}

void XFocusEvent_init(XFocusEvent* event, XEventType type, XFocusReason reason)
{
    if (!event) return;
    XEvent_init((XEvent*)event, type);
    XClassGetVtable(event) = XFocusEvent_class_init();
    event->m_class.input_event = true;
    event->m_reason = reason;
}

bool XFocusEvent_gotFocus(const XFocusEvent* event)
{
    return event && event->m_class.type == XEVENT_TYPE_FOCUS_IN;
}

bool XFocusEvent_lostFocus(const XFocusEvent* event)
{
    return event && event->m_class.type == XEVENT_TYPE_FOCUS_OUT;
}

XFocusReason XFocusEvent_reason(const XFocusEvent* event)
{
    return event ? event->m_reason : XFocusReason_NoReason;
}

/* ==================== XInputMethodEvent ==================== */

XInputMethodEvent* XInputMethodEvent_create_ex(
        XMemoryType memory, const XString* preeditString,
        const XString* commitString, int replacementStart,
        int replacementLength, int cursorPosition, int anchorPosition)
{
    XInputMethodEvent* event = XMemory_malloc(sizeof(*event), memory);
    if (!event) return NULL;
    XInputMethodEvent_init(event, preeditString, commitString,
                           replacementStart, replacementLength,
                           cursorPosition, anchorPosition);
    Set_Class_Memory(event, memory);
    Set_Class_IsHeap(event, true);
    return event;
}

void XInputMethodEvent_init(XInputMethodEvent* event,
                            const XString* preeditString,
                            const XString* commitString,
                            int replacementStart, int replacementLength,
                            int cursorPosition, int anchorPosition)
{
    if (!event) return;
    XEvent_init((XEvent*)event, XEVENT_TYPE_INPUT_METHOD);
    XClassGetVtable(event) = XInputMethodEvent_class_init();
    event->m_class.input_event = true;
    event->m_preeditString = preeditString ? XString_create_copy(preeditString) : NULL;
    event->m_commitString = commitString ? XString_create_copy(commitString) : NULL;
    event->m_replacementStart = replacementStart;
    event->m_replacementLength = replacementLength;
    event->m_cursorPosition = cursorPosition;
    event->m_anchorPosition = anchorPosition;
}

XString* XInputMethodEvent_preeditString(const XInputMethodEvent* event)
{
    return event && event->m_preeditString ?
           XString_create_copy(event->m_preeditString) : XString_create_utf8("");
}

XString* XInputMethodEvent_commitString(const XInputMethodEvent* event)
{
    return event && event->m_commitString ?
           XString_create_copy(event->m_commitString) : XString_create_utf8("");
}

int XInputMethodEvent_replacementStart(const XInputMethodEvent* event)
{ return event ? event->m_replacementStart : 0; }
int XInputMethodEvent_replacementLength(const XInputMethodEvent* event)
{ return event ? event->m_replacementLength : 0; }
int XInputMethodEvent_cursorPosition(const XInputMethodEvent* event)
{ return event ? event->m_cursorPosition : -1; }
int XInputMethodEvent_anchorPosition(const XInputMethodEvent* event)
{ return event ? event->m_anchorPosition : -1; }

/* ==================== XDropEvent ==================== */

XDropEvent* XDropEvent_create_ex(XMemoryType memory, XEventType type,
                                 const XPoint* position,
                                 const XPoint* globalPosition,
                                 const XString* mimeType,
                                 const XString* data)
{
    XDropEvent* event = XMemory_malloc(sizeof(*event), memory);
    if (!event) return NULL;
    XDropEvent_init(event, type, position, globalPosition, mimeType, data);
    Set_Class_Memory(event, memory);
    Set_Class_IsHeap(event, true);
    return event;
}

void XDropEvent_init(XDropEvent* event, XEventType type,
                     const XPoint* position, const XPoint* globalPosition,
                     const XString* mimeType, const XString* data)
{
    if (!event) return;
    XEvent_init((XEvent*)event, type);
    XClassGetVtable(event) = XDropEvent_class_init();
    event->m_class.input_event = true;
    event->m_class.pointer_event = true;
    event->m_class.single_point_event = true;
    event->m_position = position ? *position : (XPoint){0, 0};
    event->m_globalPosition = globalPosition ? *globalPosition :
                              event->m_position;
    event->m_mimeType = mimeType ? XString_create_copy(mimeType) :
                        XString_create_utf8("");
    event->m_data = data ? XString_create_copy(data) : XString_create_utf8("");
    event->m_dropAction = XDropAction_IgnoreAction;
    event->m_possibleActions = 0;
}

XPoint XDropEvent_position(const XDropEvent* event)
{ return event ? event->m_position : (XPoint){0, 0}; }
XPoint XDropEvent_globalPosition(const XDropEvent* event)
{ return event ? event->m_globalPosition : (XPoint){0, 0}; }
XString* XDropEvent_mimeType(const XDropEvent* event)
{ return event && event->m_mimeType ? XString_create_copy(event->m_mimeType) : XString_create_utf8(""); }
XString* XDropEvent_data(const XDropEvent* event)
{ return event && event->m_data ? XString_create_copy(event->m_data) : XString_create_utf8(""); }

int XDropEvent_dropAction(const XDropEvent* event)
{
    return event ? event->m_dropAction : XDropAction_IgnoreAction;
}

void XDropEvent_setDropAction(XDropEvent* event, int action)
{
    if (event) event->m_dropAction = action;
}

int XDropEvent_possibleActions(const XDropEvent* event)
{
    return event ? event->m_possibleActions : 0;
}

void XDropEvent_setPossibleActions(XDropEvent* event, int actions)
{
    if (event) event->m_possibleActions = actions;
}

void XFocusEvent_setReason(XFocusEvent* event, XFocusReason reason)
{
    if (!event) return;
    event->m_reason = reason;
}

/* ==================== XWheelEvent ==================== */

/** @brief XWheelEvent 的 Copy 实现：先复制基类部分，再复制滚轮字段。 */
static void VXWheelEvent_copy(XWheelEvent* dest, const XWheelEvent* src)
{
    if (!dest || !src || dest == src) return;
    XClass_Parent(XEvent, EXClass_Copy, void(*)(XEvent*, const XEvent*))(
        (XEvent*)dest, (const XEvent*)src);
    dest->m_position = src->m_position;
    dest->m_globalPosition = src->m_globalPosition;
    dest->m_angleDelta = src->m_angleDelta;
    dest->m_pixelDelta = src->m_pixelDelta;
    dest->m_phase = src->m_phase;
    dest->m_inverted = src->m_inverted;
    dest->m_source = src->m_source;
    dest->m_buttons = src->m_buttons;
    dest->m_modifiers = src->m_modifiers;
}

/** @brief XWheelEvent 的 Clone 实现：分配 + 继承虚表 + 经 Copy 虚槽深拷贝。 */
static XEvent* VXWheelEvent_clone(const XWheelEvent* event)
{
    XWheelEvent* copy = XClass_Malloc(XWheelEvent);
    if (!copy) return NULL;
    XClassGetVtable(copy) = XClassGetVtable(event);
    Set_Class_Memory(copy, XCLASS_DEFAULT_MEMORY_TYPE);
    Set_Class_IsHeap(copy, true);
    XCopy((XClass*)copy, (const XClass*)event);
    return (XEvent*)copy;
}

XVtable* XWheelEvent_class_init(void)
{
    XVTABLE_INIT_DEFAULT(XWheelEvent)
    XVTABLE_INHERIT_XCLASS(XEvent);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Copy, VXWheelEvent_copy);
    XVTABLE_OVERLOAD_DEFAULT(EXEvent_Clone, VXWheelEvent_clone);
    return XVTABLE_DEFAULT;
}

XWheelEvent* XWheelEvent_create_ex(XMemoryType memory, XEventType type,
                                   const XPoint* position,
                                   const XPoint* globalPosition,
                                   const XPoint* angleDelta,
                                   XMouseButton buttons,
                                   XKeyboardModifiers modifiers)
{
    XWheelEvent* event = XMemory_malloc(sizeof(XWheelEvent), memory);
    if (!event) return NULL;
    XWheelEvent_init(event, type, position, globalPosition, angleDelta,
                     buttons, modifiers);
    Set_Class_Memory(event, memory);
    Set_Class_IsHeap(event, true);
    return event;
}

void XWheelEvent_init(XWheelEvent* event, XEventType type,
                      const XPoint* position, const XPoint* globalPosition,
                      const XPoint* angleDelta, XMouseButton buttons,
                      XKeyboardModifiers modifiers)
{
    if (!event) return;
    XEvent_init((XEvent*)event, type);
    XClassGetVtable(event) = XWheelEvent_class_init();
    event->m_class.input_event = true;
    event->m_class.pointer_event = true;
    event->m_class.single_point_event = true;
    if (position) event->m_position = *position;
    if (globalPosition) event->m_globalPosition = *globalPosition;
    if (angleDelta) event->m_angleDelta = *angleDelta;
    event->m_buttons = buttons;
    event->m_modifiers = modifiers;
    /* 根因(R-99)：平台无像素级（高分辨率）滚轮源时 pixelDelta 必须恒
       (0,0)（对标 Qt：pixelDelta 仅在高精度增量平台有效，非平滑滚轮
       上为 (0,0)，消费方以「非零才优先」判别回退 angleDelta）。此前
       伪造为 angleDelta/120 会使「pixelDelta 优先」的惯用代码按每刻度
       1 像素错误步进。仓库现无 pixelDelta 消费者，置 (0,0) 无回归。 */
    event->m_pixelDelta.x = 0;
    event->m_pixelDelta.y = 0;
    event->m_phase = XWheelEventPhase_NoScrollPhase;
    event->m_inverted = false;
    event->m_source = XWheelEventSource_NotSynthesized;
}

XPoint XWheelEvent_position(const XWheelEvent* event)
{
    return event ? event->m_position : (XPoint){0, 0};
}

XPoint XWheelEvent_globalPosition(const XWheelEvent* event)
{
    return event ? event->m_globalPosition : (XPoint){0, 0};
}

XPoint XWheelEvent_angleDelta(const XWheelEvent* event)
{
    return event ? event->m_angleDelta : (XPoint){0, 0};
}

XMouseButton XWheelEvent_buttons(const XWheelEvent* event)
{
    return event ? event->m_buttons : XMouseButton_NoButton;
}

XKeyboardModifiers XWheelEvent_modifiers(const XWheelEvent* event)
{
    return event ? event->m_modifiers : XKeyboardModifier_NoModifier;
}

XPoint XWheelEvent_pixelDelta(const XWheelEvent* event)
{
    return event ? event->m_pixelDelta : (XPoint){0, 0};
}

void XWheelEvent_setPixelDelta(XWheelEvent* event, const XPoint* delta)
{
    if (event && delta) event->m_pixelDelta = *delta;
}

int XWheelEvent_phase(const XWheelEvent* event)
{
    return event ? event->m_phase : XWheelEventPhase_NoScrollPhase;
}

void XWheelEvent_setPhase(XWheelEvent* event, int phase)
{
    if (event) event->m_phase = phase;
}

bool XWheelEvent_inverted(const XWheelEvent* event)
{
    return event ? event->m_inverted : false;
}

void XWheelEvent_setInverted(XWheelEvent* event, bool inverted)
{
    if (event) event->m_inverted = inverted;
}

int XWheelEvent_source(const XWheelEvent* event)
{
    return event ? event->m_source : XWheelEventSource_NotSynthesized;
}

void XWheelEvent_setSource(XWheelEvent* event, int source)
{
    if (event) event->m_source = source;
}

/* ==================== XEnterEvent ==================== */

/** @brief XEnterEvent 的 Copy 实现：先复制基类部分，再复制坐标字段。 */
static void VXEnterEvent_copy(XEnterEvent* dest, const XEnterEvent* src)
{
    if (!dest || !src || dest == src) return;
    XClass_Parent(XEvent, EXClass_Copy, void(*)(XEvent*, const XEvent*))(
        (XEvent*)dest, (const XEvent*)src);
    dest->m_position = src->m_position;
    dest->m_globalPosition = src->m_globalPosition;
    dest->m_scenePosition = src->m_scenePosition;
}

/** @brief XEnterEvent 的 Clone 实现：分配 + 继承虚表 + 经 Copy 虚槽深拷贝。 */
static XEvent* VXEnterEvent_clone(const XEnterEvent* event)
{
    XEnterEvent* copy = XClass_Malloc(XEnterEvent);
    if (!copy) return NULL;
    XClassGetVtable(copy) = XClassGetVtable(event);
    Set_Class_Memory(copy, XCLASS_DEFAULT_MEMORY_TYPE);
    Set_Class_IsHeap(copy, true);
    XCopy((XClass*)copy, (const XClass*)event);
    return (XEvent*)copy;
}

XVtable* XEnterEvent_class_init(void)
{
    XVTABLE_INIT_DEFAULT(XEnterEvent)
    XVTABLE_INHERIT_XCLASS(XEvent);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Copy, VXEnterEvent_copy);
    XVTABLE_OVERLOAD_DEFAULT(EXEvent_Clone, VXEnterEvent_clone);
    return XVTABLE_DEFAULT;
}

XEnterEvent* XEnterEvent_create_ex(XMemoryType memory, XEventType type,
                                   const XPoint* position,
                                   const XPoint* globalPosition)
{
    XEnterEvent* event = XMemory_malloc(sizeof(XEnterEvent), memory);
    if (!event) return NULL;
    XEnterEvent_init(event, type, position, globalPosition);
    Set_Class_Memory(event, memory);
    Set_Class_IsHeap(event, true);
    return event;
}

void XEnterEvent_init(XEnterEvent* event, XEventType type,
                      const XPoint* position, const XPoint* globalPosition)
{
    if (!event) return;
    XEvent_init((XEvent*)event, type);
    XClassGetVtable(event) = XEnterEvent_class_init();
    event->m_class.input_event = true;
    event->m_class.pointer_event = true;
    event->m_class.single_point_event = true;
    if (position) event->m_position = *position;
    if (globalPosition) event->m_globalPosition = *globalPosition;
    event->m_scenePosition = event->m_globalPosition;
}

XPoint XEnterEvent_position(const XEnterEvent* event)
{
    return event ? event->m_position : (XPoint){0, 0};
}

XPoint XEnterEvent_globalPosition(const XEnterEvent* event)
{
    return event ? event->m_globalPosition : (XPoint){0, 0};
}

XPoint XEnterEvent_scenePosition(const XEnterEvent* event)
{
    return event ? event->m_scenePosition : (XPoint){0, 0};
}

void XEnterEvent_setScenePosition(XEnterEvent* event, const XPoint* pos)
{
    if (event && pos) event->m_scenePosition = *pos;
}

/* ==================== XContextMenuEvent（对标 QContextMenuEvent） ==================== */

XVtable* XContextMenuEvent_class_init(void)
{
    XVTABLE_INIT_DEFAULT(XContextMenuEvent)
    XVTABLE_INHERIT_XCLASS(XEvent);
    return XVTABLE_DEFAULT;
}

void XContextMenuEvent_init(XContextMenuEvent* event, XEventType type,
                            const XPoint* position,
                            const XPoint* globalPosition,
                            XContextMenuReason reason,
                            XKeyboardModifiers modifiers)
{
    if (!event) return;
    XEvent_init((XEvent*)event, type);
    XClassGetVtable(event) = XContextMenuEvent_class_init();
    event->m_class.input_event = true;
    event->m_position = position ? *position : (XPoint){0, 0};
    event->m_globalPosition = globalPosition ? *globalPosition :
                              event->m_position;
    event->m_reason = reason;
    event->m_modifiers = modifiers;
}

XContextMenuEvent* XContextMenuEvent_create_ex(
        XMemoryType memory, XEventType type, const XPoint* position,
        const XPoint* globalPosition, XContextMenuReason reason,
        XKeyboardModifiers modifiers)
{
    XContextMenuEvent* event = XMemory_malloc(sizeof(*event), memory);
    if (!event) return NULL;
    XContextMenuEvent_init(event, type, position, globalPosition, reason,
                           modifiers);
    Set_Class_Memory(event, memory);
    Set_Class_IsHeap(event, true);
    return event;
}

XPoint XContextMenuEvent_position(const XContextMenuEvent* event)
{
    return event ? event->m_position : (XPoint){0, 0};
}

XPoint XContextMenuEvent_globalPosition(const XContextMenuEvent* event)
{
    return event ? event->m_globalPosition : (XPoint){0, 0};
}

XContextMenuReason XContextMenuEvent_reason(const XContextMenuEvent* event)
{
    return event ? event->m_reason : XContextMenuReason_Mouse;
}

XKeyboardModifiers XContextMenuEvent_modifiers(const XContextMenuEvent* event)
{
    return event ? event->m_modifiers : XKeyboardModifier_NoModifier;
}


/* ==================== XMoveEvent（Task 2.13） ==================== */

XVtable* XMoveEvent_class_init(void)
{
    XVTABLE_INIT_DEFAULT(XMoveEvent)
    XVTABLE_INHERIT_XCLASS(XEvent);
    return XVTABLE_DEFAULT;
}

XMoveEvent* XMoveEvent_create_ex(XMemoryType memory, XEventType type,
                                 const XPoint* position,
                                 const XPoint* oldPosition)
{
    XMoveEvent* event = XMemory_malloc(sizeof(XMoveEvent), memory);
    if (!event) return NULL;
    XMoveEvent_init(event, type, position, oldPosition);
    Set_Class_Memory(event, memory);
    Set_Class_IsHeap(event, true);
    return event;
}

void XMoveEvent_init(XMoveEvent* event, XEventType type,
                     const XPoint* position, const XPoint* oldPosition)
{
    if (!event) return;
    XEvent_init((XEvent*)event, type);
    XClassGetVtable(event) = XMoveEvent_class_init();
    if (position) event->m_position = *position;
    if (oldPosition) event->m_oldPosition = *oldPosition;
}

XPoint XMoveEvent_position(const XMoveEvent* event)
{
    return event ? event->m_position : (XPoint){0, 0};
}

XPoint XMoveEvent_oldPosition(const XMoveEvent* event)
{
    return event ? event->m_oldPosition : (XPoint){0, 0};
}

/* ==================== XTouchEvent（Task 2.13） ==================== */

XVtable* XTouchEvent_class_init(void)
{
    XVTABLE_INIT_DEFAULT(XTouchEvent)
    XVTABLE_INHERIT_XCLASS(XEvent);
    return XVTABLE_DEFAULT;
}

XTouchEvent* XTouchEvent_create_ex(XMemoryType memory, XEventType type,
                                   const XPoint* position,
                                   const XPoint* globalPosition,
                                   int pointCount)
{
    XTouchEvent* event = XMemory_malloc(sizeof(XTouchEvent), memory);
    if (!event) return NULL;
    XTouchEvent_init(event, type, position, globalPosition, pointCount);
    Set_Class_Memory(event, memory);
    Set_Class_IsHeap(event, true);
    return event;
}

void XTouchEvent_init(XTouchEvent* event, XEventType type,
                      const XPoint* position, const XPoint* globalPosition,
                      int pointCount)
{
    if (!event) return;
    XEvent_init((XEvent*)event, type);
    XClassGetVtable(event) = XTouchEvent_class_init();
    event->m_class.input_event = true;
    event->m_class.pointer_event = true;
    if (position) event->m_position = *position;
    if (globalPosition) event->m_globalPosition = *globalPosition;
    event->m_pointCount = pointCount > 0 ? pointCount : 1;
}

XPoint XTouchEvent_position(const XTouchEvent* event)
{
    return event ? event->m_position : (XPoint){0, 0};
}

XPoint XTouchEvent_globalPosition(const XTouchEvent* event)
{
    return event ? event->m_globalPosition : (XPoint){0, 0};
}

int XTouchEvent_pointCount(const XTouchEvent* event)
{
    return event ? event->m_pointCount : 0;
}

/* ==================== XTabletEvent（Task 2.13） ==================== */

XVtable* XTabletEvent_class_init(void)
{
    XVTABLE_INIT_DEFAULT(XTabletEvent)
    XVTABLE_INHERIT_XCLASS(XEvent);
    return XVTABLE_DEFAULT;
}

XTabletEvent* XTabletEvent_create_ex(XMemoryType memory, XEventType type,
                                     const XPoint* position,
                                     const XPoint* globalPosition,
                                     float pressure, int pointerType)
{
    XTabletEvent* event = XMemory_malloc(sizeof(XTabletEvent), memory);
    if (!event) return NULL;
    XTabletEvent_init(event, type, position, globalPosition, pressure,
                      pointerType);
    Set_Class_Memory(event, memory);
    Set_Class_IsHeap(event, true);
    return event;
}

void XTabletEvent_init(XTabletEvent* event, XEventType type,
                       const XPoint* position, const XPoint* globalPosition,
                       float pressure, int pointerType)
{
    if (!event) return;
    XEvent_init((XEvent*)event, type);
    XClassGetVtable(event) = XTabletEvent_class_init();
    event->m_class.input_event = true;
    event->m_class.pointer_event = true;
    event->m_class.single_point_event = true;
    if (position) event->m_position = *position;
    if (globalPosition) event->m_globalPosition = *globalPosition;
    event->m_pressure = pressure;
    event->m_pointerType = pointerType;
}

XPoint XTabletEvent_position(const XTabletEvent* event)
{
    return event ? event->m_position : (XPoint){0, 0};
}

XPoint XTabletEvent_globalPosition(const XTabletEvent* event)
{
    return event ? event->m_globalPosition : (XPoint){0, 0};
}

float XTabletEvent_pressure(const XTabletEvent* event)
{
    return event ? event->m_pressure : 0.0f;
}

int XTabletEvent_pointerType(const XTabletEvent* event)
{
    return event ? event->m_pointerType : XTabletPointerType_Unknown;
}

#endif /* XWINDOWEVENT_ON */