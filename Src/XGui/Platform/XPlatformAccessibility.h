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
#define XPlatformAccessibility_delete_base(self) XClass_delete_base((XClass*)(self))
#define XPlatformAccessibility_deinit_base(self) XClass_deinit_base((XClass*)(self))
#define XPlatformAccessibility_copy_base(self, other) \
    XClass_copy_base((XClass*)(self), (const XClass*)(other))
#define XPlatformAccessibility_move_base(self, other) \
    XClass_move_base((XClass*)(self), (XClass*)(other))

XAccessible* XPlatformAccessibility_root(const XPlatformAccessibility* self);
bool XPlatformAccessibility_isActive(const XPlatformAccessibility* self);
void XPlatformAccessibility_notify(XPlatformAccessibility* self,
                                   XAccessibleEvent event,
                                   XAccessible* accessible);
void XPlatformAccessibility_processEvents(XPlatformAccessibility* self);

/** @brief 供 XWindow 属性与生命周期使用的全局桥接通知入口。 */
void XPlatformAccessibility_notifyWindow(XAccessibleEvent event, XWindow* window);
/** @brief 供 XWidget 控件生命周期使用的全局桥接通知入口。 */
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
