/**
 * @file       XWhatsThisEvent.h
 * @brief      「What's This?」帮助事件的载荷类型（对标 Qt 6.8
 *             QHelpEvent / QWhatsThisClickedEvent）。
 * @details    为什么单独成文件：事件**类型码**早就在 XEventType.h 里
 *             （XEVENT_TYPE_WHATS_THIS=111 / XEVENT_TYPE_QUERY_WHATS_THIS=123 /
 *             XEVENT_TYPE_WHATS_THIS_CLICKED=118，数值与 Qt
 *             qcoreevent.h:147-169 逐项一致），缺的是**载荷**——
 *             Qt 的 `QHelpEvent` 携带 `pos/globalPos`，`QWidget::event`
 *             显示帮助时取的就是 `static_cast<QHelpEvent*>(event)->
 *             globalPos()`（qwidget.cpp:9332-9337）；`QTabBar::event`
 *             同样按 `QHelpEvent::pos()` 命中页签（qtabbar.cpp:1715-1730）。
 *             没有载荷就只能传「有个帮助事件」，接收方不知道在哪、
 *             也就无法选中区域/页签，故按框架既有派生事件套路
 *             （XCLASS_DEFINE_BEGING/EXTEND_END + XVTABLE_INHERIT_XCLASS +
 *             Copy/Clone 槽，与 XKeyEvent/XMouseEvent/XDynamicPropertyChangeEvent
 *             同一套原语）新建两个类型，不新造注册机制。
 * @note       事件类型码**不重定义**：XHelpEvent 的 type 由构造入参给出
 *             （ToolTip / WhatsThis / QueryWhatsThis 三者共用同一载荷，
 *             与 Qt 一致）。
 * @author     XinYueC 团队
 */
#ifndef XWHATSTHISEVENT_H
#define XWHATSTHISEVENT_H

#include <stdint.h>
#include <stdbool.h>

#include "XClass.h"
#include "XTypes.h"
#include "XEvent.h"
#include "XString.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ==================== XHelpEvent（对标 QHelpEvent） ==================== */

XCLASS_DEFINE_BEGING(XHelpEvent)
XCLASS_DEFINE_EXTEND_END(XHelpEvent, XEvent)

/**
 * @brief 携带「帮助查询位置」的事件（对标 QHelpEvent，qevent.h:788-805）。
 * @details 与 Qt 逐项同构：`m_pos` = 事件源控件局部坐标，
 *          `m_globalPos` = 屏幕全局坐标。三者（ToolTip / WhatsThis /
 *          QueryWhatsThis）共用本载荷，由 `m_base.m_type` 区分。
 */
typedef struct XHelpEvent
{
    XEvent m_base;      /**< 基类；必须是第一个成员。 */
    XPoint m_pos;       /**< 事件源控件局部坐标（对标 QHelpEvent::pos）。 */
    XPoint m_globalPos; /**< 屏幕全局坐标（对标 QHelpEvent::globalPos）。 */
} XHelpEvent;

/**
 * @brief 创建帮助事件（对标 QHelpEvent(Type, pos, globalPos)）。
 * @param memory 对象内存类型。
 * @param type 事件类型：XEVENT_TYPE_TOOL_TIP / XEVENT_TYPE_WHATS_THIS /
 *        XEVENT_TYPE_QUERY_WHATS_THIS（Qt 同三者）。
 * @param pos 事件源控件局部坐标。
 * @param globalPos 屏幕全局坐标。
 * @return 新事件，调用方拥有（投递后由事件队列接管）；失败返回 NULL。
 */
XHelpEvent* XHelpEvent_create_ex(XMemoryType memory, XEventType type,
                                 XPoint pos, XPoint globalPos);
/**
 * @brief 初始化调用方提供的帮助事件存储（对标 QHelpEvent 构造）。
 * @param event 待初始化存储；不可为 NULL。
 * @param type 事件类型（同上）。
 * @param pos 局部坐标。
 * @param globalPos 全局坐标。
 * @return 无返回值。
 */
void XHelpEvent_init(XHelpEvent* event, XEventType type,
                     XPoint pos, XPoint globalPos);
/**
 * @brief 查询帮助事件的局部坐标（对标 QHelpEvent::pos）。
 * @param event 帮助事件；可为 NULL。
 * @return 局部坐标；NULL 返回 (0,0)。
 */
XPoint XHelpEvent_pos(const XHelpEvent* event);
/**
 * @brief 查询帮助事件的全局坐标（对标 QHelpEvent::globalPos）。
 * @param event 帮助事件；可为 NULL。
 * @return 全局坐标；NULL 返回 (0,0)。
 */
XPoint XHelpEvent_globalPos(const XHelpEvent* event);
/** @brief 查询局部 X（对标 QHelpEvent::x）。NULL 返回 0。 */
int XHelpEvent_x(const XHelpEvent* event);
/** @brief 查询局部 Y（对标 QHelpEvent::y）。NULL 返回 0。 */
int XHelpEvent_y(const XHelpEvent* event);
/** @brief 查询全局 X（对标 QHelpEvent::globalX）。NULL 返回 0。 */
int XHelpEvent_globalX(const XHelpEvent* event);
/** @brief 查询全局 Y（对标 QHelpEvent::globalY）。NULL 返回 0。 */
int XHelpEvent_globalY(const XHelpEvent* event);

/**
 * @brief 判断一个 XEvent 是否确为 XHelpEvent（类身份探测）。
 * @details 用途：`XEVENT_TYPE_WHATS_THIS` 等类型码允许携带**裸 XEvent**
 *          （框架既有调用点 `XDialog.c` 的右键「What's This?」菜单就投的
 *          无载荷 XEvent），接收方若直接 `static_cast` 就会读到栈外
 *          ——本函数让「有载荷则取坐标、无载荷则回落到控件自身原点」
 *          这一降级成为可能，且不误判。
 * @param event 待探测事件；可为 NULL。
 * @return 是 XHelpEvent（含其派生类按虚表身份判定）返回 true。
 */
bool XHelpEvent_isEvent(const XEvent* event);
/**
 * @brief 把 XEvent 安全降级为 XHelpEvent 借用指针。
 * @param event 待转换事件；可为 NULL。
 * @return 是 XHelpEvent 时返回同一对象（**借用**，不得释放）；否则返回
 *         NULL（调用方据此走无载荷回落路径）。
 */
const XHelpEvent* XHelpEvent_from(const XEvent* event);

/* ==================== XWhatsThisClickedEvent（对标同名 Qt 类） ============ */

XCLASS_DEFINE_BEGING(XWhatsThisClickedEvent)
XCLASS_DEFINE_EXTEND_END(XWhatsThisClickedEvent, XEvent)

/**
 * @brief 「帮助浮层里的链接被点击」事件（对标 QWhatsThisClickedEvent，
 *        qevent.h:821-831）。
 * @details 类型恒为 `XEVENT_TYPE_WHATS_THIS_CLICKED`（118，与 Qt 同值）；
 *          载荷 `href` 由本对象**拥有**，析构时释放。
 */
typedef struct XWhatsThisClickedEvent
{
    XEvent m_base;  /**< 基类；必须是第一个成员。 */
    XString* m_href;/**< 链接目标（拥有；NULL 表示空）。 */
} XWhatsThisClickedEvent;

/**
 * @brief 创建链接点击事件（对标 QWhatsThisClickedEvent(href)）。
 * @param memory 对象内存类型。
 * @param utf8 链接目标（UTF-8，深拷贝）；NULL 视为空。
 * @return 新事件，调用方拥有；失败返回 NULL。
 */
XWhatsThisClickedEvent* XWhatsThisClickedEvent_create_ex(XMemoryType memory,
                                                        const char* utf8);
/**
 * @brief 初始化调用方提供的存储（对标同构造）。
 * @param event 待初始化存储；不可为 NULL。
 * @param utf8 链接目标（UTF-8，深拷贝）；NULL 视为空。
 * @return 无返回值。
 */
void XWhatsThisClickedEvent_init(XWhatsThisClickedEvent* event,
                                 const char* utf8);
/**
 * @brief 查询链接目标（对标 QWhatsThisClickedEvent::href；**借用**）。
 * @param event 事件；可为 NULL。
 * @return 内部字符串借用指针（随事件析构失效，禁止释放）；空返 NULL。
 */
const XString* XWhatsThisClickedEvent_href(const XWhatsThisClickedEvent* event);

/* 默认内存类型创建包装（与 XEvent.h 尾部的同名宏同一风格）。 */
#undef XHelpEvent_create
#define XHelpEvent_create(...) \
    XHelpEvent_create_ex(XMEMORY_TYPE_MULTIPOOL, __VA_ARGS__)
#undef XWhatsThisClickedEvent_create
#define XWhatsThisClickedEvent_create(...) \
    XWhatsThisClickedEvent_create_ex(XMEMORY_TYPE_MULTIPOOL, __VA_ARGS__)

#ifdef __cplusplus
}
#endif

#endif /* XWHATSTHISEVENT_H */
