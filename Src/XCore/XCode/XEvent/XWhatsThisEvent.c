/**
 * @file       XWhatsThisEvent.c
 * @brief      QHelpEvent / QWhatsThisClickedEvent 的对等实现（§B13 事件类型）。
 * @details    两个类的构造/Copy/Clone/析构与既有派生事件
 *             （XKeyEvent / XMouseEvent / XDynamicPropertyChangeEvent）
 *             逐项同构：
 *             ① 类身份经 XCLASS_DEFINE_BEGING/EXTEND_END 生成的槽位枚举 +
 *                XVTABLE_INHERIT_XCLASS(XEvent) 继承基类表；
 *             ② Copy 走 XClass_Parent(XEvent, EXClass_Copy, ...)（**不得**
 *                直调 `_base`，那会无限递归——风格文档 §497 条款 7）；
 *             ③ Clone 沿源事件自身的内存分配器族分配（Class_Memory），
 *                与 VXEvent_default_clone 同口径；
 *             ④ 带拥有型载荷的类（XWhatsThisClickedEvent 的 XString*）
 *                必须自带 Deinit/Clone，否则基类 Clone 只按 sizeof(XEvent)
 *                分配、deinit 按派生类读载荷 ⇒ 越界读 + 误释放
 *                （XDynamicPropertyChangeEvent 的历史教训，见其注释）。
 * @author     XinYueC 团队
 */

#include "XWhatsThisEvent.h"
#include "XMemory.h"
#include <string.h>

/* ==================== XHelpEvent ==================== */

static void VXHelpEvent_copy(XHelpEvent* dest, const XHelpEvent* src);
static XEvent* VXHelpEvent_clone(const XHelpEvent* event);

static XVtable* XHelpEvent_class_init(void)
{
    XVTABLE_INIT_DEFAULT(XHelpEvent)
    XVTABLE_INHERIT_XCLASS(XEvent);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Copy, VXHelpEvent_copy);
    XVTABLE_OVERLOAD_DEFAULT(EXEvent_Clone, VXHelpEvent_clone);
    return XVTABLE_DEFAULT;
}

void XHelpEvent_init(XHelpEvent* event, XEventType type,
                     XPoint pos, XPoint globalPos)
{
    if (!event) return;
    XEvent_init(&event->m_base, type);
    XClassGetVtable(event) = XHelpEvent_class_init();
    /* 帮助事件对标 Qt 的 QHelpEvent：**不是**输入事件（Qt 的 QHelpEvent
     * 构造只设 type/pos/globalPos，未置 m_inputEvent）。故基类的
     * m_input_event/m_pointer_event/m_single_point_event 保持 XEvent_init
     * 的零值——不复制 XMouseEvent 的置位口径。 */
    event->m_pos = pos;
    event->m_globalPos = globalPos;
}

XHelpEvent* XHelpEvent_create_ex(XMemoryType memory, XEventType type,
                                 XPoint pos, XPoint globalPos)
{
    XHelpEvent* event = (XHelpEvent*)XMemory_malloc(sizeof(XHelpEvent), memory);
    if (!event) return NULL;
    XHelpEvent_init(event, type, pos, globalPos);
    Set_Class_Memory(event, memory); Set_Class_IsHeap(event, true);
    return event;
}

XPoint XHelpEvent_pos(const XHelpEvent* event)
{
    XPoint out;
    out.x = 0;
    out.y = 0;
    if (event) out = event->m_pos;
    return out;
}

XPoint XHelpEvent_globalPos(const XHelpEvent* event)
{
    XPoint out;
    out.x = 0;
    out.y = 0;
    if (event) out = event->m_globalPos;
    return out;
}

int XHelpEvent_x(const XHelpEvent* event)
{
    return event ? event->m_pos.x : 0;
}

int XHelpEvent_y(const XHelpEvent* event)
{
    return event ? event->m_pos.y : 0;
}

int XHelpEvent_globalX(const XHelpEvent* event)
{
    return event ? event->m_globalPos.x : 0;
}

int XHelpEvent_globalY(const XHelpEvent* event)
{
    return event ? event->m_globalPos.y : 0;
}

bool XHelpEvent_isEvent(const XEvent* event)
{
    XVtable* vt;
    XVtable* target;
    int guard = 0;
    if (!event) return false;
    vt = XClassGetVtable(event);
    if (!vt) return false;
    target = XHelpEvent_class_init();
    /* 沿 m_parent 链上溯比对（派生类的表以 XHelpEvent 表为直接父）。
     * guard 防畸形表成环——链长上限远大于实际继承深度。 */
    while (vt && guard < 32) {
        if (vt == target) return true;
        vt = XVtable_parent(vt);
        ++guard;
    }
    return false;
}

const XHelpEvent* XHelpEvent_from(const XEvent* event)
{
    if (!XHelpEvent_isEvent(event)) return NULL;
    return (const XHelpEvent*)event;
}

/** @brief 复制基类部分 + 两个坐标（对标 QHelpEvent 的隐式拷贝语义）。 */
static void VXHelpEvent_copy(XHelpEvent* dest, const XHelpEvent* src)
{
    if (!dest || !src || dest == src) return;
    XClass_Parent(XEvent, EXClass_Copy, void (*)(XEvent*, const XEvent*))(
        (XEvent*)dest, (const XEvent*)src);
    dest->m_pos = src->m_pos;
    dest->m_globalPos = src->m_globalPos;
}

static XEvent* VXHelpEvent_clone(const XHelpEvent* event)
{
    XHelpEvent* copy;
    if (!event) return NULL;
    copy = (XHelpEvent*)Class_Memory(event)->malloc(sizeof(XHelpEvent));
    if (!copy) return NULL;
    XClassGetVtable(copy) = XClassGetVtable(event);
    Class_Memory(copy) = Class_Memory(event);
    Set_Class_IsHeap(copy, true);
    XClassCopy((XClass*)copy, (const XClass*)event);
    return (XEvent*)copy;
}

/* ==================== XWhatsThisClickedEvent ==================== */

static void VXWhatsThisClickedEvent_deinit(XEvent* event);
static void VXWhatsThisClickedEvent_copy(XWhatsThisClickedEvent* dest,
                                         const XWhatsThisClickedEvent* src);
static XEvent* VXWhatsThisClickedEvent_clone(
    const XWhatsThisClickedEvent* event);

/** @brief 复制 UTF-8 链接目标（深拷贝；NULL 返回 NULL）。 */
static XString* xwt_makeHref(const char* utf8)
{
    if (!utf8) return NULL;
    return XString_create_utf8(utf8);
}

static XVtable* XWhatsThisClickedEvent_class_init(void)
{
    XVTABLE_INIT_DEFAULT(XWhatsThisClickedEvent)
    XVTABLE_INHERIT_XCLASS(XEvent);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Deinit, VXWhatsThisClickedEvent_deinit);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Copy, VXWhatsThisClickedEvent_copy);
    XVTABLE_OVERLOAD_DEFAULT(EXEvent_Clone, VXWhatsThisClickedEvent_clone);
    return XVTABLE_DEFAULT;
}

void XWhatsThisClickedEvent_init(XWhatsThisClickedEvent* event,
                                 const char* utf8)
{
    if (!event) return;
    XEvent_init(&event->m_base, XEVENT_TYPE_WHATS_THIS_CLICKED);
    XClassGetVtable(event) = XWhatsThisClickedEvent_class_init();
    event->m_href = xwt_makeHref(utf8);
}

XWhatsThisClickedEvent* XWhatsThisClickedEvent_create_ex(XMemoryType memory,
                                                        const char* utf8)
{
    XWhatsThisClickedEvent* event = (XWhatsThisClickedEvent*)XMemory_malloc(
        sizeof(XWhatsThisClickedEvent), memory);
    if (!event) return NULL;
    XWhatsThisClickedEvent_init(event, utf8);
    Set_Class_Memory(event, memory); Set_Class_IsHeap(event, true);
    return event;
}

const XString* XWhatsThisClickedEvent_href(
    const XWhatsThisClickedEvent* event)
{
    if (!event) return NULL;
    if (event->m_base.m_type != XEVENT_TYPE_WHATS_THIS_CLICKED) return NULL;
    return event->m_href;
}

static void VXWhatsThisClickedEvent_deinit(XEvent* event)
{
    XWhatsThisClickedEvent* e = (XWhatsThisClickedEvent*)event;
    if (!e) return;
    if (e->m_href) {
        XClassDelete((XClass*)e->m_href);
        e->m_href = NULL;
    }
}

/** @brief 只复制基类部分，链接目标保持目标对象自己的（同
 *         VXDynamicPropertyChangeEvent_copy 口径）。 */
static void VXWhatsThisClickedEvent_copy(XWhatsThisClickedEvent* dest,
                                         const XWhatsThisClickedEvent* src)
{
    if (!dest || !src || dest == src) return;
    XClass_Parent(XEvent, EXClass_Copy, void (*)(XEvent*, const XEvent*))(
        (XEvent*)dest, (const XEvent*)src);
}

/** @brief 自带 Clone：按**完整大小**分配并深拷贝链接目标（链路见文件头注④）。 */
static XEvent* VXWhatsThisClickedEvent_clone(
    const XWhatsThisClickedEvent* event)
{
    XWhatsThisClickedEvent* copy;
    const XString* href;
    if (!event) return NULL;
    href = XWhatsThisClickedEvent_href(event);
    copy = (XWhatsThisClickedEvent*)Class_Memory(event)->malloc(
        sizeof(XWhatsThisClickedEvent));
    if (!copy) return NULL;
    XEvent_init(&copy->m_base, XEVENT_TYPE_WHATS_THIS_CLICKED);
    XClassGetVtable(copy) = XWhatsThisClickedEvent_class_init();
    Class_Memory(copy) = Class_Memory(event);
    Set_Class_IsHeap(copy, true);
    copy->m_href = href ? XString_create_copy(href) : NULL;
    XClassCopy((XClass*)copy, (const XClass*)event);
    return (XEvent*)copy;
}
