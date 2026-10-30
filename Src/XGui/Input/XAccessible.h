/****************************************************************************
 * @file       XAccessible.h
 * @brief      平台无关的辅助功能对象节点。
 * @details    XAccessible 描述 UI 可访问树的节点，不包含 AT-SPI、UIA 或
 *             其它系统辅助功能 API。系统桥接只能由 Drive 消费该对象。
 ****************************************************************************/
#ifndef XACCESSIBLE_H
#define XACCESSIBLE_H

#if XGUI_ON
#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
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

/**
 * @brief 辅助功能状态位（对标 AT-SPI2 AtspiStateType 位序）。
 * @details 位序严格对齐 GNOME at-spi2-core 的 `atspi-constants.h`
 *          （ATSPI_STATE_ACTIVE = 1<<0 … ATSPI_STATE_CHECKABLE = 1<<40），
 *          使平台桥接层（AT-SPI 的 GetState 返回 uint32 掩码数组）可
 *          直接按位翻译，无需中间映射表。
 */
typedef uint64_t XAccessibleState;

/** @brief 状态位常量（位序同 AT-SPI2 AtspiStateType）。 */
#define XACCESSIBLE_STATE_ACTIVE        (1ULL << 0)  /**< 活跃。 */
#define XACCESSIBLE_STATE_ARMED         (1ULL << 1)  /**< armed（按下未释放）。 */
#define XACCESSIBLE_STATE_BUSY          (1ULL << 2)  /**< 忙碌。 */
#define XACCESSIBLE_STATE_CHECKED       (1ULL << 3)  /**< 已勾选（可勾选控件）。 */
#define XACCESSIBLE_STATE_COLLAPSED     (1ULL << 4)  /**< 折叠。 */
#define XACCESSIBLE_STATE_DEFUNCT       (1ULL << 5)  /**< 已失效。 */
#define XACCESSIBLE_STATE_EDITABLE      (1ULL << 6)  /**< 可编辑。 */
#define XACCESSIBLE_STATE_ENABLED       (1ULL << 7)  /**< 启用（未禁用）。 */
#define XACCESSIBLE_STATE_EXPANDABLE    (1ULL << 8)  /**< 可展开。 */
#define XACCESSIBLE_STATE_EXPANDED      (1ULL << 9)  /**< 已展开。 */
#define XACCESSIBLE_STATE_FOCUSABLE     (1ULL << 10) /**< 可获焦点。 */
#define XACCESSIBLE_STATE_FOCUSED       (1ULL << 11) /**< 已有焦点。 */
#define XACCESSIBLE_STATE_HAS_TOOLTIP   (1ULL << 12) /**< 有工具提示。 */
#define XACCESSIBLE_STATE_HORIZONTAL    (1ULL << 13) /**< 水平方向。 */
#define XACCESSIBLE_STATE_ICONIFIED     (1ULL << 14) /**< 最小化。 */
#define XACCESSIBLE_STATE_MODAL         (1ULL << 15) /**< 模态。 */
#define XACCESSIBLE_STATE_MULTI_LINE    (1ULL << 16) /**< 多行文本。 */
#define XACCESSIBLE_STATE_MULTISELECTABLE (1ULL << 17) /**< 可多选。 */
#define XACCESSIBLE_STATE_OPAQUE        (1ULL << 18) /**< 不透明。 */
#define XACCESSIBLE_STATE_PRESSED       (1ULL << 19) /**< 按下中。 */
#define XACCESSIBLE_STATE_RESIZABLE     (1ULL << 20) /**< 可调整大小。 */
#define XACCESSIBLE_STATE_SELECTABLE    (1ULL << 21) /**< 可选中。 */
#define XACCESSIBLE_STATE_SELECTED      (1ULL << 22) /**< 已选中。 */
#define XACCESSIBLE_STATE_SENSITIVE     (1ULL << 23) /**< 敏感（可响应）。 */
#define XACCESSIBLE_STATE_SHOWING       (1ULL << 24) /**< 正在显示。 */
#define XACCESSIBLE_STATE_SINGLE_LINE   (1ULL << 25) /**< 单行文本。 */
#define XACCESSIBLE_STATE_STALE         (1ULL << 26) /**< 过期。 */
#define XACCESSIBLE_STATE_TRANSIENT     (1ULL << 27) /**< 临时。 */
#define XACCESSIBLE_STATE_VERTICAL      (1ULL << 28) /**< 垂直方向。 */
#define XACCESSIBLE_STATE_VISIBLE       (1ULL << 29) /**< 可见。 */
#define XACCESSIBLE_STATE_MANAGES_DESCENDANTS (1ULL << 30) /**< 管理后代节点。 */
#define XACCESSIBLE_STATE_INDETERMINATE (1ULL << 31) /**< 勾选状态不定。 */
#define XACCESSIBLE_STATE_REQUIRED      (1ULL << 32) /**< 必填。 */
#define XACCESSIBLE_STATE_TRUNCATED     (1ULL << 33) /**< 文本被截断。 */
#define XACCESSIBLE_STATE_ANIMATED      (1ULL << 34) /**< 含动画。 */
#define XACCESSIBLE_STATE_INVALID_ENTRY (1ULL << 35) /**< 条目无效。 */
#define XACCESSIBLE_STATE_SUPPORTS_AUTOCOMPLETION (1ULL << 36) /**< 支持自动补全。 */
#define XACCESSIBLE_STATE_SELECTABLE_TEXT (1ULL << 37) /**< 文本可选。 */
#define XACCESSIBLE_STATE_IS_DEFAULT    (1ULL << 38) /**< 默认项。 */
#define XACCESSIBLE_STATE_VISITED       (1ULL << 39) /**< 已访问。 */
#define XACCESSIBLE_STATE_CHECKABLE     (1ULL << 40) /**< 可勾选。 */
#define XACCESSIBLE_STATE_HAS_POPUP     (1ULL << 41) /**< 有弹出层。 */
#define XACCESSIBLE_STATE_READ_ONLY     (1ULL << 42) /**< 只读。 */

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
bool XAccessible_isValid(const XAccessible* self);
XAccessibleRole XAccessible_role(const XAccessible* self);
XRect XAccessible_rect(const XAccessible* self);
bool XAccessible_isVisible(const XAccessible* self);
/**
 * @brief      查询节点当前状态位集合（对标 QAccessibleInterface::state）。
 * @details    位序见 XAccessibleState 常量（对齐 AT-SPI2 AtspiStateType）。
 *             当前实现按控件/窗口真实状态推导：可见性、启用态、焦点态、
 *             可聚焦性、按下态（按钮）、可编辑性（文本控件）、方向性。
 * @param      self 节点；可为 NULL。
 * @return     状态位集合；节点无效时返回 0。
 */
XAccessibleState XAccessible_state(const XAccessible* self);
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

#endif /* XGUI_ON */

#endif /* XACCESSIBLE_H */
