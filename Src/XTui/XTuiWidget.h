/**
 * @file       XTuiWidget.h
 * @brief      XTui 控件基类公开 API。
 * @details    只继承 XClass，避免引入 XObject 的事件/线程依赖。XTuiWidget 定义
 *             渲染、按键、尺寸变化和焦点虚函数；具体控件通过重载这些虚函数实现
 *             自己的绘制和交互。控件不拥有屏幕，渲染时由调用方传入 XTuiScreen。
 */

#ifndef XTUI_WIDGET_H
#define XTUI_WIDGET_H

#include "XTuiConfig.h"

#ifdef __cplusplus
extern "C" {
#endif

#if XTUI_ON && XTUI_WIDGET_ON

#include "XTuiTypes.h"
#include "XTuiScreen.h"
#include "XClass.h"
#include "XMemory.h"
#include <stdbool.h>

/**
 * @brief XTuiWidget 类虚函数表枚举（继承 XClass，新增 5 个虚函数）。
 */
XCLASS_DEFINE_BEGING(XTuiWidget)
XCLASS_DEFINE_ENUM(XTuiWidget, Render) = XCLASS_VTABLE_GET_SIZE(XClass), /**< 渲染到屏幕。 */
XCLASS_DEFINE_ENUM(XTuiWidget, KeyPress), /**< 按键事件。 */
XCLASS_DEFINE_ENUM(XTuiWidget, Resize),   /**< 尺寸变化。 */
XCLASS_DEFINE_ENUM(XTuiWidget, FocusIn),  /**< 获得焦点。 */
XCLASS_DEFINE_ENUM(XTuiWidget, FocusOut), /**< 失去焦点。 */
XCLASS_DEFINE_END(XTuiWidget)

/**
 * @brief 控件基类对象。
 * @details 控件采用组合方式使用：m_parent 是借用指针，控件不释放父对象；
 *          m_visible/m_focused/m_enabled 控制可见性、焦点和交互使能。
 */
typedef struct XTuiWidget
{
    XClass      m_class;   /**< 基类，第一个成员，由 XClass 管理，禁止手工修改。 */
    XRect    m_rect;    /**< 控件在屏幕中的矩形区域。 */
    struct XTuiWidget* m_parent; /**< 父控件；借用指针，不由本控件释放。 */
    char*       m_name;    /**< 控件名称；动态分配，由本控件释放。 */
    bool        m_visible; /**< 是否可见；默认 true。 */
    bool        m_focused; /**< 是否获得焦点；默认 false。 */
    bool        m_enabled; /**< 是否可交互；默认 true。 */
} XTuiWidget;

/** @brief 初始化 XTuiWidget 类虚函数表。 */
XVtable* XTuiWidget_class_init(void);

/** @brief 在栈上初始化控件对象。 */
void XTuiWidget_init(XTuiWidget* widget);

/** @brief 在堆上创建控件对象。 */
XTuiWidget* XTuiWidget_create_ex(XMemoryType memory);

#define XTuiWidget_delete_base XClass_delete_base /**< 释放堆对象。 */

/* ==================== 虚函数调度入口 ==================== */

/**
 * @brief 把控件渲染到屏幕。
 * @details 基础实现返回 false；子类应重载并绘制到 self->m_rect 区域内。
 * @param self 目标控件。
 * @param screen 目标屏幕；NULL 不执行。
 * @return true 表示已绘制内容。
 */
bool XTuiWidget_render_base(XTuiWidget* self, XTuiScreen* screen);

/**
 * @brief 向控件派发按键事件。
 * @details 基础实现返回 false；子类重载后返回 true 表示事件已处理。
 * @param self 目标控件。
 * @param event 按键事件。
 * @return true 表示事件已处理，false 表示未处理。
 */
bool XTuiWidget_keyPress_base(XTuiWidget* self, const XTuiKeyEvent* event);

/**
 * @brief 通知控件尺寸变化。
 * @param self 目标控件。
 * @param width 新宽度（列数）。
 * @param height 新高度（行数）。
 */
void XTuiWidget_resize_base(XTuiWidget* self, int width, int height);

/** @brief 通知控件获得焦点。 */
void XTuiWidget_focusIn_base(XTuiWidget* self);

/** @brief 通知控件失去焦点。 */
void XTuiWidget_focusOut_base(XTuiWidget* self);

/* ==================== 属性 ==================== */

/** @brief 设置控件矩形区域。 */
void XTuiWidget_setRect(XTuiWidget* self, const XRect* rect);

/** @brief 获取控件矩形区域。 */
XRect XTuiWidget_rect(const XTuiWidget* self);

/** @brief 设置控件名称（深拷贝）。 */
void XTuiWidget_setName(XTuiWidget* self, const char* name);

/** @brief 获取控件名称（借用指针）。 */
const char* XTuiWidget_name(const XTuiWidget* self);

/** @brief 设置父控件。 */
void XTuiWidget_setParent(XTuiWidget* self, XTuiWidget* parent);

/** @brief 获取父控件。 */
XTuiWidget* XTuiWidget_parent(const XTuiWidget* self);

/** @brief 设置可见性。 */
void XTuiWidget_setVisible(XTuiWidget* self, bool visible);

/** @brief 查询可见性。 */
bool XTuiWidget_isVisible(const XTuiWidget* self);

/** @brief 设置交互使能。 */
void XTuiWidget_setEnabled(XTuiWidget* self, bool enabled);

/** @brief 查询是否可交互。 */
bool XTuiWidget_isEnabled(const XTuiWidget* self);

#endif /* XTUI_ON && XTUI_WIDGET_ON */

#ifdef __cplusplus
}
#endif

/* XClass create API default-memory wrappers. */
#undef XTuiWidget_create
#define XTuiWidget_create() XTuiWidget_create_ex(XCLASS_DEFAULT_MEMORY_TYPE)

#endif /* XTUI_WIDGET_H */
