/**
 * @file       XHoverEvent.h
 * @brief      悬停事件类（对标 Qt 6.8 QHoverEvent / QEvent::HoverEnter /
 *             HoverLeave / HoverMove，`qevent.h:246-278`）。
 * @details    悬停事件是 Qt 在 `WA_Hover` 控件上单独投递的一类指针事件：
 *             与 ENTER/LEAVE 的区别在于它**携带坐标**（当前局部/全局坐标
 *             与上一次局部坐标），因此「指针在同一个控件内部从一个子控件
 *             移到另一个子控件」可以被消费方感知——QScrollBar 正是靠它做
 *             子控件悬停追踪（`qscrollbar.cpp:441-446` 的
 *             `d_func()->updateHoverControl(he->position().toPoint())`）。
 *
 *             Qt 的生成点（qapplication.cpp）：
 *             - `QEvent::HoverEnter`：`he(HoverEnter, windowPos, QPointF(-1,-1),
 *               globalPos, mods)` 后 `QMutableEventPoint::setPosition(he.point(0),
 *               localPos)`（:2114-2117）；
 *             - `QEvent::HoverLeave`：`he(HoverLeave, QPointF(-1,-1), globalPosF,
 *               w->mapFromGlobal(globalPosF), mods)`（:2094-2095）——
 *               **当前位置是 (-1,-1)**，真实位置在 oldPos；
 *             - `QEvent::HoverMove`：`he(HoverMove, scenePos, globalPos,
 *               relpos - diff, mods)` 后同样以 `setPosition` 覆盖为局部坐标
 *               （:2798-2800）。
 *
 *             本类逐字段承载上述语义：m_position（当前局部坐标，Leave 时
 *             为 (-1,-1)）、m_globalPosition、m_oldPosition（上一次局部
 *             坐标）与 m_modifiers。
 * @note       只依赖 XEvent / XGeometry，不依赖窗口域与平台 API：
 *             本文件**不**受 XWINDOWEVENT_ON 约束（悬停事件是控件级事件，
 *             控件域在无窗口事件模块时仍可构造它做单元测试）。
 *             模块总开关 XGUI_ON（XGuiConfig.h）。
 * @author     XinYueC 团队
 */
#ifndef XHOVEREVENT_H
#define XHOVEREVENT_H

#if XGUI_ON
#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>
#include "XGuiConfig.h"
#include "XClass.h"
#include "XMemory.h"
#include "XEvent.h"
#include "XGeometry.h"

/** @brief 声明 XHoverEvent 虚函数枚举：继承 XEvent（新增 Copy/Clone）。 */
XCLASS_DEFINE_BEGING(XHoverEvent)
XCLASS_DEFINE_EXTEND_END(XHoverEvent, XEvent)

/** @brief 初始化 XHoverEvent 类虚函数表。 @return 共享虚函数表指针。 */
XVtable* XHoverEvent_class_init(void);

/**
 * @brief 悬停事件对象；m_class 必须为第一个成员。
 * @note  m_position 在 HoverLeave 上按 Qt 口径为 (-1,-1)（当前位置未知/
 *        已不在控件内），真实位置见 m_oldPosition。
 */
typedef struct XHoverEvent
{
    XEvent m_class;          /**< 继承 XEvent；必须为第一个成员。 */
    XPoint m_position;       /**< 当前局部坐标（对标 QHoverEvent::position；
                                  HoverLeave 时为 (-1,-1)）。 */
    XPoint m_globalPosition; /**< 当前屏幕全局坐标（对标
                                  QHoverEvent::globalPosition）。 */
    XPoint m_oldPosition;    /**< 上一次局部坐标（对标 QHoverEvent::oldPosF）。 */
    XKeyboardModifiers m_modifiers; /**< 事件发生时按下的修饰键（对标
                                         QInputEvent::modifiers）。 */
} XHoverEvent;

/**
 * @brief      创建悬停事件（对标 QHoverEvent(type, scenePos, globalPos,
 *             oldPos, modifiers)）。
 * @param      memory         内存类型；通常传 XCLASS_DEFAULT_MEMORY_TYPE。
 * @param      type           事件类型：XEVENT_TYPE_HOVER_ENTER /
 *                            XEVENT_TYPE_HOVER_LEAVE / XEVENT_TYPE_HOVER_MOVE。
 * @param      position       当前局部坐标；可为 NULL（按 (0,0)）。
 * @param      globalPosition 当前屏幕全局坐标；可为 NULL（按 (0,0)）。
 * @param      oldPosition    上一次局部坐标；可为 NULL（按 (0,0)）。
 * @param      modifiers      修饰键位掩码。
 * @return     新事件对象；内存分配失败返回 NULL（所有权归调用方）。
 */
XHoverEvent* XHoverEvent_create_ex(XMemoryType memory, XEventType type,
                                   const XPoint* position,
                                   const XPoint* globalPosition,
                                   const XPoint* oldPosition,
                                   XKeyboardModifiers modifiers);

/**
 * @brief      初始化调用者提供的悬停事件存储（参数语义同 create_ex）。
 * @param      event          待初始化的事件；NULL 时无操作。
 */
void XHoverEvent_init(XHoverEvent* event, XEventType type,
                      const XPoint* position, const XPoint* globalPosition,
                      const XPoint* oldPosition, XKeyboardModifiers modifiers);

/** @brief 获取当前局部坐标（对标 QHoverEvent::position）。 */
XPoint XHoverEvent_position(const XHoverEvent* event);
/** @brief 获取当前屏幕全局坐标（对标 QHoverEvent::globalPosition）。 */
XPoint XHoverEvent_globalPosition(const XHoverEvent* event);
/** @brief 获取上一次局部坐标（对标 QHoverEvent::oldPosF）。 */
XPoint XHoverEvent_oldPosition(const XHoverEvent* event);
/** @brief 获取修饰键（对标 QInputEvent::modifiers）。 */
XKeyboardModifiers XHoverEvent_modifiers(const XHoverEvent* event);

/**
 * @brief      覆写当前局部坐标（对标 QMutableEventPoint::setPosition）。
 * @details    悬停事件的局部坐标在 Qt 侧是**事后覆盖**的：构造时先给场景
 *             坐标，再由派发点换算成接收控件局部坐标写回
 *             （`qapplication.cpp:2116-2117`、`:2799-2800`）。X 侧没有
 *             QMutableEventPoint，直接提供本 setter 承担同一语义。
 * @param      event 悬停事件；可为 NULL。
 * @param      pos   目标局部坐标；NULL 时无操作。
 */
void XHoverEvent_setPosition(XHoverEvent* event, const XPoint* pos);

/** @brief 创建悬停事件（默认内存类型；参数语义同 XHoverEvent_create_ex）。 */
#define XHoverEvent_create(...) \
    XHoverEvent_create_ex(XMEMORY_TYPE_MULTIPOOL, __VA_ARGS__)

#ifdef __cplusplus
}
#endif
#endif /* XGUI_ON */

#endif /* XHOVEREVENT_H */
