/******************************************************************************
 * @file       XHoverEvent.c
 * @brief      悬停事件类实现（对标 Qt 6.8 QHoverEvent）。
 * @details    与同名头文件的公共 API 一一对应。实现要点：
 *             - 虚表只重载 Copy / Clone（事件无动态成员，Deinit 沿用
 *               XEvent）：克隆件与本体各自独立、可安全入队与跨线程投递；
 *             - 内存分配器口径与其它事件类一致：本体与克隆件一律沿对象
 *               自身保存的分配器族（Class_Memory）分配与记账，不硬编码
 *               系统分配函数；
 *             - 事件标志位按 Qt 的指针事件口径置 input/pointer/
 *               single_point 三位（`QSinglePointEvent` 家族共同特征），
 *               便于上层按「指针事件」统一分派。
 * @note       本文件不依赖任何平台 API，亦不依赖 XWINDOWEVENT_ON——
 *             悬停事件是控件级事件，窗口事件模块裁剪时仍可单独使用。
 *             模块总开关 XGUI_ON 定义于 XGuiConfig.h。
 * @author     XinYueC 团队
 ******************************************************************************/
#if XGUI_ON

#include "XHoverEvent.h"

#include "XAlgorithm.h"

/* ==================== 虚函数实现（Copy / Clone） ==================== */

/** @brief XHoverEvent 的 Copy 实现：先复制基类部分，再复制负载字段。 */
static void VXHoverEvent_copy(XHoverEvent* dest, const XHoverEvent* src)
{
    if (!dest || !src || dest == src) return;
    /* 目标未 init（虚表为空）时先按本类型完整初始化兜底，再复制。 */
    if (XClassIsVtableNull(dest))
        XHoverEvent_init(dest, src->m_class.m_type, &src->m_position,
                         &src->m_globalPosition, &src->m_oldPosition,
                         src->m_modifiers);
    XClass_Parent(XEvent, EXClass_Copy, void(*)(XEvent*, const XEvent*))(
        (XEvent*)dest, (const XEvent*)src);
    dest->m_position = src->m_position;
    dest->m_globalPosition = src->m_globalPosition;
    dest->m_oldPosition = src->m_oldPosition;
    dest->m_modifiers = src->m_modifiers;
}

/** @brief XHoverEvent 的 Clone 实现：分配 + 继承虚表 + 经 Copy 虚槽深拷贝。
 *  @note 克隆件沿源事件自身保存的内存分配器族分配与记账（同其它事件类）。 */
static XEvent* VXHoverEvent_clone(const XHoverEvent* event)
{
    XHoverEvent* copy;
    if (!event) return NULL;
    copy = (XHoverEvent*)Class_Memory(event)->malloc(sizeof(XHoverEvent));
    if (!copy) return NULL;
    XClassGetVtable(copy) = XClassGetVtable(event);
    Class_Memory(copy) = Class_Memory(event);
    Set_Class_IsHeap(copy, true);
    XClassCopy((XClass*)copy, (const XClass*)event);
    return (XEvent*)copy;
}

XVtable* XHoverEvent_class_init(void)
{
    XVTABLE_INIT_DEFAULT(XHoverEvent)
    XVTABLE_INHERIT_XCLASS(XEvent);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Copy, VXHoverEvent_copy);
    XVTABLE_OVERLOAD_DEFAULT(EXEvent_Clone, VXHoverEvent_clone);
    return XVTABLE_DEFAULT;
}

/* ==================== 构造 / 初始化 ==================== */

XHoverEvent* XHoverEvent_create_ex(XMemoryType memory, XEventType type,
                                   const XPoint* position,
                                   const XPoint* globalPosition,
                                   const XPoint* oldPosition,
                                   XKeyboardModifiers modifiers)
{
    XHoverEvent* event = (XHoverEvent*)XMemory_malloc(sizeof(XHoverEvent),
                                                     memory);
    if (!event) return NULL;
    XHoverEvent_init(event, type, position, globalPosition, oldPosition,
                     modifiers);
    Set_Class_Memory(event, memory);
    Set_Class_IsHeap(event, true);
    return event;
}

void XHoverEvent_init(XHoverEvent* event, XEventType type,
                      const XPoint* position, const XPoint* globalPosition,
                      const XPoint* oldPosition, XKeyboardModifiers modifiers)
{
    if (!event) return;
    XEvent_init((XEvent*)event, type);
    XClassGetVtable(event) = XHoverEvent_class_init();
    /* 指针事件三位（QSinglePointEvent 家族共同特征）：上层按「指针事件」
     * 统一分派/合并时据此识别。 */
    event->m_class.m_input_event = true;
    event->m_class.m_pointer_event = true;
    event->m_class.m_single_point_event = true;
    XPoint_init(&event->m_position, 0, 0);
    XPoint_init(&event->m_globalPosition, 0, 0);
    XPoint_init(&event->m_oldPosition, 0, 0);
    if (position) event->m_position = *position;
    if (globalPosition) event->m_globalPosition = *globalPosition;
    if (oldPosition) event->m_oldPosition = *oldPosition;
    event->m_modifiers = modifiers;
}

/* ==================== 访问器 ==================== */

XPoint XHoverEvent_position(const XHoverEvent* event)
{
    XPoint zero;
    XPoint_init(&zero, 0, 0);
    return event ? event->m_position : zero;
}

XPoint XHoverEvent_globalPosition(const XHoverEvent* event)
{
    XPoint zero;
    XPoint_init(&zero, 0, 0);
    return event ? event->m_globalPosition : zero;
}

XPoint XHoverEvent_oldPosition(const XHoverEvent* event)
{
    XPoint zero;
    XPoint_init(&zero, 0, 0);
    return event ? event->m_oldPosition : zero;
}

XKeyboardModifiers XHoverEvent_modifiers(const XHoverEvent* event)
{
    return event ? event->m_modifiers : (XKeyboardModifiers)0;
}

void XHoverEvent_setPosition(XHoverEvent* event, const XPoint* pos)
{
    if (event && pos) event->m_position = *pos;
}

#endif /* XGUI_ON */
