/******************************************************************************
 * @file       XPlatformWindow.h
 * @brief      XPlatformWindow 平台窗口句柄类（对标 Qt 6.8 QPlatformWindow 轻量子集）。
 * @details    XPlatformWindow 继承 XObject，是平台集成层（XPlatformIntegration）
 *             提供给每个 XWindow 的轻量平台句柄：
 *             - 与 XWindow 一一对应：m_window 为借用指针，几何/可见性/激活
 *               直接转发 XWindow 快照与 XGuiApplication 焦点系统；
 *             - 自带 64 位自增原生句柄 ID（handle()，充当 WId 等价物）与
 *               窗口 id（winId()，转发 XWindow_winId）；
 *             - 原生属性表（对标 QPlatformNativeInterface 的
 *               windowProperties 系列）：XString→XVariant 深拷贝存储，
 *               供上层/平台交换窗口私有属性，变更由
 *               XPlatformNativeInterface_windowPropertyChanged 信号通知。
 *             本模块不依赖任何平台 API：嵌入式内置单后端直接使用对象内存
 *             承载属性，不连接系统窗口栈。
 * @note       模块开关 XPLATFORMWINDOW_ON 定义于 XGuiConfig.h；置 0 时
 *             裁剪整个 XPlatformWindow 公共 API，XPlatformIntegration 的
 *             createPlatformWindow() 返回 NULL。
 * @author     XinYueC 团队
 ******************************************************************************/
#ifndef XPLATFORMWINDOW_H
#define XPLATFORMWINDOW_H
#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>
#include "XGuiConfig.h"
#include "XClass.h"
#include "XObject.h"
#include "XMemory.h"
#include "XTypes.h"
#include "XGeometry.h"
#include "XString.h"
#include "XVariant.h"
#include "XHashMap.h"
#if XWINDOW_ON
#include "XWindow.h"
#else /* !XWINDOW_ON */
/** @brief XWINDOW_ON=0 时的 XWindow 前向声明，保持指针 API 可编译。 */
typedef struct XWindow XWindow;
/** @brief 窗口 id 类型回退定义；保证结构体成员可编译。 */
typedef uintptr_t XWindowId;
#endif /* XWINDOW_ON */

#if XPLATFORMWINDOW_ON

/** @brief 私有实现前向声明；仅供实现访问。 */
typedef struct XPlatformWindowPrivate XPlatformWindowPrivate;/** @brief 声明 XPlatformWindow 虚函数枚举：继承 XObject（无新增槽位）。 */
XCLASS_DEFINE_BEGING(XPlatformWindow)
XCLASS_DEFINE_EXTEND_END(XPlatformWindow, XObject)



/**
 * @brief      XPlatformWindow 平台窗口句柄对象；m_class 必须为第一个成员。
 * @details    所有平台句柄状态保存在 m_data 私有块中，调用方不得直接访问。
 */
typedef struct XPlatformWindow
{
    XObject                   m_class; /**< 第一个成员，由 XObject 管理。 */
    XPlatformWindowPrivate*   m_data;  /**< 私有数据块，由 XPlatformWindow 拥有。 */
} XPlatformWindow;

/**
 * @brief      初始化 XPlatformWindow 类虚函数表并返回共享表指针。
 * @return     XPlatformWindow 类的共享 XVtable 指针。
 */
XVtable* XPlatformWindow_class_init(void);

/**
 * @brief      初始化绑定指定 XWindow 的轻量平台句柄。
 * @details    为窗口分配自增原生句柄 ID 并创建空原生属性表；窗口借用不持有。
 * @param      self 待初始化对象；必须与 XPlatformWindow_deinit_base 成对调用。
 * @param      window 目标 XWindow 借用指针；可为 NULL（纯属性承载）。
 */
void XPlatformWindow_init(XPlatformWindow* self, XWindow* window);

/**
 * @brief      使用默认内存类型创建绑定指定 XWindow 的平台句柄。
 * @param      window 目标 XWindow 借用指针；可为 NULL。
 * @return     新对象指针；失败返回 NULL，调用方用 XPlatformWindow_delete_base 释放。
 */
#define XPlatformWindow_create(window) \
    XPlatformWindow_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, (window))

/**
 * @brief      使用指定内存类型创建绑定指定 XWindow 的平台句柄。
 * @param      memory 对象内存类型。
 * @param      window 目标 XWindow 借用指针；可为 NULL。
 * @return     新对象指针；失败返回 NULL。
 */
XPlatformWindow* XPlatformWindow_create_ex(XMemoryType memory, XWindow* window);

/** @brief 通过 XClass 虚表释放 XPlatformWindow 资源（栈/外部存储对象使用）。 */
#define XPlatformWindow_deinit_base(self) XClass_deinit_base((XClass*)(self))
/** @brief 删除堆上的 XPlatformWindow 对象。 */
#define XPlatformWindow_delete_base(self) XClass_delete_base((XClass*)(self))

/* ==================== 窗口句柄访问（对标 QPlatformWindow 核心） ==================== */

/**
 * @brief      返回绑定的 XWindow（对标 QPlatformWindow::window()）。
 * @return     借用指针；未绑定返回 NULL。
 */
XWindow* XPlatformWindow_window(const XPlatformWindow* self);

/** @brief 更新绑定的 XWindow 借用指针（窗口 move 生命周期使用）。 */
void XPlatformWindow_setWindow(XPlatformWindow* self, XWindow* window);

/**
 * @brief      返回 64 位原生句柄 ID（对标 QPlatformWindow NativeHandle）。
 * @details    每个 XPlatformWindow 构造时从全局自增计数器分配（从 1 开始），
 *             与 XWindow_winId 相互独立；用于原生资源查询
 *             （nativeResourceForWindow 的 "window-handle"）。
 * @return     原生句柄 ID；入参非法返回 0。
 */
uint64_t XPlatformWindow_handle(const XPlatformWindow* self);
/** @brief 标记平台窗口是否代表外部原生窗口（内部集成层使用）。 */
void XPlatformWindow_setForeign(XPlatformWindow* self, bool foreign);
/** @brief 查询平台窗口是否代表外部原生窗口。 */
bool XPlatformWindow_isForeign(const XPlatformWindow* self);

/**
 * @brief      返回窗口 id（对标 QWindow::winId，转发 XWindow_winId）。
 * @details    XWINDOW_ON=0 时返回原生句柄 ID 作为回退。
 * @return     窗口 id；入参非法返回 0。
 */
XWindowId XPlatformWindow_winId(const XPlatformWindow* self);

/**
 * @brief      返回窗口几何（对标 QPlatformWindow::geometry，转发 XWindow_geometry）。
 * @return     设备无关像素的几何矩形；入参非法或未绑定时返回零矩形。
 */
XRect XPlatformWindow_geometry(const XPlatformWindow* self);

/**
 * @brief      设置窗口几何（转发 XWindow_setGeometry_rect）。
 * @param      self 目标对象；可为 NULL。
 * @param      rect 新几何；NULL 按零矩形处理。
 */
void XPlatformWindow_setGeometry(XPlatformWindow* self, const XRect* rect);

/**
 * @brief      查询窗口可见性（转发 XWindow_isVisible）。
 * @return     true 可见；false 不可见或入参非法。
 */
bool XPlatformWindow_isVisible(const XPlatformWindow* self);

/**
 * @brief      设置窗口可见性（转发 XWindow_setVisible）。
 * @param      self 目标对象；可为 NULL。
 * @param      visible true 显示、false 隐藏。
 */
void XPlatformWindow_setVisible(XPlatformWindow* self, bool visible);

/**
 * @brief      请求激活窗口并给予键盘焦点（对标 QPlatformWindow::requestActivate）。
 * @details    转发至 XGuiApplication_setFocusWindow；无 GUI 应用实例时 no-op。
 * @param      self 目标对象；可为 NULL。
 */
void XPlatformWindow_requestActivate(XPlatformWindow* self);

/* ==================== 原生属性表（对标 QPlatformNativeInterface 窗口属性） ==================== */

/**
 * @brief      返回完整原生属性表。
 * @details    对标 QPlatformNativeInterface::windowProperties 返回的映射；
 *             本实现返回内部借用指针（XString→XVariant 深拷贝存储），
 *             调用方不得释放、不得跨对象生命周期保存。
 * @return     内部 XVariantHashMap 借用指针；失败返回 NULL。
 */
XVariantHashMap* XPlatformWindow_properties(const XPlatformWindow* self);

/**
 * @brief      读取单个原生属性（对标 windowProperty(window, name)）。
 * @param      self 目标对象；可为 NULL。
 * @param      name UTF-8 属性名；可为 NULL。
 * @return     内部存储的 XVariant 借用指针；不存在或入参非法返回 NULL。
 */
XVariant* XPlatformWindow_property(const XPlatformWindow* self, const char* name);

/**
 * @brief      写入原生属性（对标 setWindowProperty）。
 * @details    深拷贝存储；value 为 NULL 时等价删除该键。
 * @param      self 目标对象；可为 NULL。
 * @param      name UTF-8 属性名；可为 NULL。
 * @param      value 属性值；可为 NULL（删除）。
 */
void XPlatformWindow_setProperty(XPlatformWindow* self, const char* name,
                                 const XVariant* value);

/**
 * @brief      删除原生属性。
 * @param      self 目标对象；可为 NULL。
 * @param      name UTF-8 属性名；可为 NULL。
 * @return     true 表示原本存在并已删除；false 表示不存在或入参非法。
 */
bool XPlatformWindow_removeProperty(XPlatformWindow* self, const char* name);

#ifdef __cplusplus
}
#endif

#endif /* XPLATFORMWINDOW_ON */
#endif /* XPLATFORMWINDOW_H */
