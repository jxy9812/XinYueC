/****************************************************************************
 * @file       XAccessible.h
 * @brief      平台无关的辅助功能对象节点。
 * @details    XAccessible 描述 UI 可访问树的节点，不包含 AT-SPI、UIA 或
 *             其它系统辅助功能 API。系统桥接只能由 Drive 消费该对象。
 ****************************************************************************/
#ifndef XACCESSIBLE_H
#define XACCESSIBLE_H
#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stddef.h>
#include "XGuiConfig.h"
#include "XClass.h"
#include "XObject.h"
#include "XGeometry.h"
#include "XString.h"

#if XWINDOW_ON && XACCESSIBLE_ON
typedef struct XWindow XWindow;
typedef struct XWidget XWidget;

typedef enum XAccessibleRole
{
    XAccessibleRole_Application = 1,
    XAccessibleRole_Window,
    XAccessibleRole_Client,
    XAccessibleRole_Button,
    XAccessibleRole_Text,
    XAccessibleRole_List,
    XAccessibleRole_Table,
    XAccessibleRole_Unknown
} XAccessibleRole;

typedef struct XAccessible XAccessible;

XCLASS_DEFINE_BEGING(XAccessible)
XCLASS_DEFINE_EXTEND_END(XAccessible, XObject)

struct XAccessible
{
    XObject m_class;
    XWindow* m_window;             /**< 关联窗口，借用。 */
    XWidget* m_widget;             /**< 关联控件，借用；控件节点使用。 */
    bool m_applicationRoot;        /**< true 时为进程级应用树根，无窗口。 */
    XAccessibleRole m_role;
    XString* m_name;               /**< 显式名称；NULL 时使用窗口标题。 */
    XString* m_description;        /**< 可选描述。 */
};

XVtable* XAccessible_class_init(void);
XAccessible* XAccessible_createForWindow_ex(XMemoryType memory, XWindow* window);
#define XAccessible_createForWindow(window) \
    XAccessible_createForWindow_ex(XCLASS_DEFAULT_MEMORY_TYPE, (window))
XAccessible* XAccessible_createForWidget_ex(XMemoryType memory, XWidget* widget);
#define XAccessible_createForWidget(widget) \
    XAccessible_createForWidget_ex(XCLASS_DEFAULT_MEMORY_TYPE, (widget))
/** @brief 创建进程级应用可访问根；由 XPlatformAccessibility 拥有。 */
XAccessible* XAccessible_createApplication_ex(XMemoryType memory);
#define XAccessible_createApplication() \
    XAccessible_createApplication_ex(XCLASS_DEFAULT_MEMORY_TYPE)
#define XAccessible_delete_base(self) XClass_delete_base((XClass*)(self))
#define XAccessible_deinit_base(self) XClass_deinit_base((XClass*)(self))
bool XAccessible_isValid(const XAccessible* self);
XAccessibleRole XAccessible_role(const XAccessible* self);
XRect XAccessible_rect(const XAccessible* self);
bool XAccessible_isVisible(const XAccessible* self);
XString* XAccessible_name(const XAccessible* self);
void XAccessible_setName(XAccessible* self, const XString* name);
XString* XAccessible_description(const XAccessible* self);
void XAccessible_setDescription(XAccessible* self, const XString* description);
XWindow* XAccessible_window(const XAccessible* self);
/** @brief 返回控件节点关联的 QWidget；窗口/应用节点返回 NULL。 */
XWidget* XAccessible_widget(const XAccessible* self);
/** @brief 返回父可访问节点；应用根无父节点。 */
XAccessible* XAccessible_parent(const XAccessible* self);
/** @brief 返回应用根的当前窗口子节点数；窗口节点恒为 0。 */
size_t XAccessible_childCount(const XAccessible* self);
/** @brief 返回应用根指定窗口子节点（借用）；越界或非根节点返回 NULL。 */
XAccessible* XAccessible_childAtIndex(const XAccessible* self, size_t index);

#endif /* XWINDOW_ON && XACCESSIBLE_ON */
#ifdef __cplusplus
}
#endif
#endif /* XACCESSIBLE_H */
