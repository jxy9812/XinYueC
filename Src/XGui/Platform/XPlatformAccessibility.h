/**
 * @file       XPlatformAccessibility.h
 * @brief      辅助功能平台桥接契约（公共层无平台 API）。
 * @details    该对象拥有进程级 XAccessible 应用根，并把对象树变化交给
 *              Drive 的平台实现。Linux 后端可映射为 AT-SPI/D-Bus，Windows
 *              后端可映射为 UI Automation；嵌入式后端是零副作用存根。
 */
#ifndef XPLATFORMACCESSIBILITY_H
#define XPLATFORMACCESSIBILITY_H
#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include "XGuiConfig.h"
#include "XClass.h"
#include "XObject.h"
#include "XMemory.h"
#include "XAccessible.h"

#if XWINDOW_ON && XACCESSIBLE_ON

typedef enum XAccessibleEvent
{
    XAccessibleEvent_ObjectCreated = 1,
    XAccessibleEvent_ObjectDestroyed,
    XAccessibleEvent_NameChanged,
    XAccessibleEvent_DescriptionChanged,
    XAccessibleEvent_LocationChanged,
    XAccessibleEvent_StateChanged
} XAccessibleEvent;

typedef struct XPlatformAccessibility XPlatformAccessibility;

XCLASS_DEFINE_BEGING(XPlatformAccessibility)
XCLASS_DEFINE_EXTEND_END(XPlatformAccessibility, XObject)

struct XPlatformAccessibility
{
    XObject m_class;
    XAccessible* m_root;     /**< 应用根（拥有）。 */
    void* m_nativeState;     /**< Drive 平台状态（拥有，不透明）。 */
    bool m_active;           /**< 系统辅助功能服务是否已实际接入。 */
};

XVtable* XPlatformAccessibility_class_init(void);
XPlatformAccessibility* XPlatformAccessibility_create_ex(XMemoryType memory);
#define XPlatformAccessibility_create() \
    XPlatformAccessibility_create_ex(XCLASS_DEFAULT_MEMORY_TYPE)

/**
 * @brief      返回进程级辅助功能应用根（对标 QAccessible::rootObject）。
 * @param      self 目标桥接对象；可为 NULL。
 * @return     应用根借用指针；对象拥有该根，调用方不得释放；入参非法或
 *             未创建时返回 NULL。
 */
XAccessible* XPlatformAccessibility_root(const XPlatformAccessibility* self);
/**
 * @brief      查询系统辅助功能服务是否已实际接入。
 * @param      self 目标桥接对象；可为 NULL。
 * @return     桥接有效且平台后端回报已接入返回 true；否则返回 false。
 */
bool XPlatformAccessibility_isActive(const XPlatformAccessibility* self);
/**
 * @brief      启用/禁用辅助功能（对标 setActive）。
 * @note       公共层当前为占位实现；接入状态由平台后端回报维护。
 * @param      self 目标桥接对象；可为 NULL。
 * @param      active true 请求启用；false 请求禁用。
 * @return     无。
 */
void XPlatformAccessibility_setActive(const XPlatformAccessibility* self,
                                      bool active);
/**
 * @brief      初始化辅助功能后端（对标 initialize；幂等）。
 * @note       公共层当前为占位实现；后端起用在 create 时完成。
 * @param      self 目标桥接对象；可为 NULL。
 * @return     无。
 */
void XPlatformAccessibility_initialize(const XPlatformAccessibility* self);
/**
 * @brief      清理辅助功能后端（对标 cleanup；幂等）。
 * @note       公共层当前为占位实现；资源随对象 deinit 释放。
 * @param      self 目标桥接对象；可为 NULL。
 * @return     无。
 */
void XPlatformAccessibility_cleanup(const XPlatformAccessibility* self);
/**
 * @brief      向平台后端通知辅助功能对象树变化（对标 QAccessible
 *             updateAccessibility 的平台通知）。
 * @param      self 目标桥接对象；可为 NULL（不执行）。
 * @param      event 对象树事件类型。
 * @param      accessible 发生变化的辅助功能对象借用指针；不能为 NULL。
 * @return     无。通知后按后端回报刷新接入状态。
 */
void XPlatformAccessibility_notify(XPlatformAccessibility* self,
                                   XAccessibleEvent event,
                                   XAccessible* accessible);
/**
 * @brief      处理平台辅助功能事件队列（对标平台桥接轮询）。
 * @param      self 目标桥接对象；可为 NULL（不执行）。
 * @return     无。处理后按后端回报刷新接入状态。
 */
void XPlatformAccessibility_processEvents(XPlatformAccessibility* self);

/**
 * @brief      供 XWindow 属性与生命周期使用的全局桥接通知入口。
 * @details    无全局桥接、窗口无效或通知重入时静默忽略。
 * @param      event 对象树事件类型。
 * @param      window 目标窗口借用指针；可为 NULL（不执行）。
 * @return     无。
 */
void XPlatformAccessibility_notifyWindow(XAccessibleEvent event, XWindow* window);
/**
 * @brief      供 XWidget 控件生命周期使用的全局桥接通知入口。
 * @details    无全局桥接、控件无效或通知重入时静默忽略。
 * @param      event 对象树事件类型。
 * @param      widget 目标控件借用指针；可为 NULL（不执行）。
 * @return     无。
 */
void XPlatformAccessibility_notifyWidget(XAccessibleEvent event, XWidget* widget);

/* Drive 实现契约。公共层仅调用这些纯 C 函数，绝不包含平台头。 */
bool XPlatformAccessibilityDriver_start(XPlatformAccessibility* bridge,
                                        void** nativeState);
void XPlatformAccessibilityDriver_stop(void* nativeState);
bool XPlatformAccessibilityDriver_isActive(void* nativeState);
void XPlatformAccessibilityDriver_notify(void* nativeState,
                                         XAccessibleEvent event,
                                         XAccessible* accessible);
void XPlatformAccessibilityDriver_processEvents(void* nativeState);

#endif /* XWINDOW_ON && XACCESSIBLE_ON */

#ifdef __cplusplus
}
#endif
#endif /* XPLATFORMACCESSIBILITY_H */
