/******************************************************************************
 * @file       XPlatformNativeInterface.h
 * @brief      XPlatformNativeInterface 平台原生接口类（对标 Qt 6.8
 *             QPlatformNativeInterface 全部公共 API）。
 * @details    XPlatformNativeInterface 继承 XObject，是平台集成层对外暴露
 *             「原生资源」的窗口：
 *             - 资源查询：nativeResourceFor{Integration,Window,Screen,BackingStore,
 *               Cursor} 按内置资源表返回 void* 句柄，未知资源返回 NULL；
 *             - 平台函数：platformFunction(name) 从进程内注册表解析函数指针，
 *               支持注册、覆盖和注销，未找到时返回 NULL；
 *             - 窗口原生属性：windowProperties / windowProperty /
 *               setWindowProperty 委托 XPlatformWindow 原生属性表
 *               （XString→XVariant 深拷贝），写入成功发射
 *               windowPropertyChanged 信号（对标 Qt 同名信号）。
 *             内置资源表（大小写敏感，UTF-8）：
 *             集成层（nativeResourceForIntegration）：
 *               "integration" / "integration-handle" → XPlatformIntegration*
 *               "display"    → X11 Display*（仅 Linux X11 接管真实窗口时）
 *               "hinstance"  → Win32 HINSTANCE（仅 Windows Win32 接管时）
 *               "native-connection" → 原生连接句柄 + 类型见平台契约
 *             窗口（nativeResourceForWindow）：
 *               "window"                 → XWindow*
 *               "window-handle"          → XPlatformWindow*（平台句柄）
 *               "native-window-id"       → (void*)XWindowId
 *             屏幕（nativeResourceForScreen）：
 *               "screen" / "screen-handle" → XScreen*（参数为 NULL 时返回主屏）
 *             本模块不依赖任何平台 API，未连接任何系统原生窗口栈，
 *             返回的句柄均为进程内借用指针，调用方不得释放。
 * @note       模块开关 XPLATFORMNATIVEINTERFACE_ON 定义于 XGuiConfig.h；
 *             置 0 时裁剪整个公共 API，XPlatformIntegration 的
 *             nativeInterface() 返回 NULL。
 * @author     XinYueC 团队
 ******************************************************************************/
#ifndef XPLATFORMNATIVEINTERFACE_H
#define XPLATFORMNATIVEINTERFACE_H
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
#include "XString.h"
#include "XVariant.h"
#include "XHashMap.h"
#include "XPlatformWindow.h"
/** @brief 平台集成层前向声明；避免循环包含 XPlatformIntegration.h。 */
typedef struct XPlatformIntegration XPlatformIntegration;

/** @brief XScreen 前向声明；XGUIAPPLICATION_ON 下由 XGuiApplication.h 提供。 */
typedef struct XScreen XScreen;
/** @brief XCursor 前向声明；XCURSOR_ON 下由 XGui/XCursor.h 提供。 */
typedef struct XCursor XCursor;

#if XPLATFORMNATIVEINTERFACE_ON

/** @brief 私有实现前向声明；仅供实现访问。 */
typedef struct XPlatformNativeInterfacePrivate XPlatformNativeInterfacePrivate;/** @brief 声明 XPlatformNativeInterface 虚函数枚举：继承 XObject（无新增槽位）。 */
XCLASS_DEFINE_BEGING(XPlatformNativeInterface)
XCLASS_DEFINE_EXTEND_END(XPlatformNativeInterface, XObject)



/**
 * @brief      XPlatformNativeInterface 平台原生接口对象；m_class 必须为第一个成员。
 * @details    内部仅保存所属集成层的借用指针；资源表为静态只读，无平台依赖。
 */
typedef struct XPlatformNativeInterface
{
    XObject                             m_class; /**< 第一个成员，由 XObject 管理。 */
    XPlatformNativeInterfacePrivate*    m_data;  /**< 私有数据块，由 XPlatformNativeInterface 拥有。 */
} XPlatformNativeInterface;

/**
 * @brief      初始化 XPlatformNativeInterface 类虚函数表并返回共享表指针。
 * @return     XPlatformNativeInterface 类的共享 XVtable 指针。
 */
XVtable* XPlatformNativeInterface_class_init(void);

/**
 * @brief      初始化空 XPlatformNativeInterface。
 * @param      self 待初始化对象；必须与 XPlatformNativeInterface_deinit_base 成对调用。
 */
void XPlatformNativeInterface_init(XPlatformNativeInterface* self);

/**
 * @brief      使用默认内存类型在堆上创建 XPlatformNativeInterface。
 * @return     新对象指针；失败返回 NULL，调用方用
 *             XPlatformNativeInterface_delete_base 释放。
 */
#define XPlatformNativeInterface_create() \
    XPlatformNativeInterface_create_ex(XCLASS_DEFAULT_MEMORY_TYPE)

/**
 * @brief      使用指定内存类型在堆上创建 XPlatformNativeInterface。
 * @param      memory 对象内存类型。
 * @return     新对象指针；失败返回 NULL。
 */
XPlatformNativeInterface* XPlatformNativeInterface_create_ex(XMemoryType memory);

/** @brief 通过 XClass 虚表释放 XPlatformNativeInterface 资源（栈/外部存储对象使用）。 */
#define XPlatformNativeInterface_deinit_base(self) XClass_deinit_base((XClass*)(self))
/** @brief 深拷贝 XPlatformNativeInterface 资源。 */
#define XPlatformNativeInterface_copy_base(self, other) \
    XClass_copy_base((XClass*)(self), (const XClass*)(other))
/** @brief 移动 XPlatformNativeInterface 资源。 */
#define XPlatformNativeInterface_move_base(self, other) \
    XClass_move_base((XClass*)(self), (XClass*)(other))
/** @brief 删除堆上的 XPlatformNativeInterface 对象。 */
#define XPlatformNativeInterface_delete_base(self) XClass_delete_base((XClass*)(self))

/* ==================== 所属集成层（内部关联） ==================== */

/**
 * @brief      绑定所属集成层（供 XPlatformIntegration 构造时自动调用；借用）。
 * @param      self 目标对象；可为 NULL。
 * @param      integration 所属集成层借用指针；可为 NULL 清除。
 */
void XPlatformNativeInterface_setIntegration(XPlatformNativeInterface* self,
                                             XPlatformIntegration* integration);

/**
 * @brief      返回所属集成层（对标 nativeResourceForIntegration("integration") 等价）。
 * @return     借用指针；未绑定返回 NULL。
 */
XPlatformIntegration* XPlatformNativeInterface_integration(const XPlatformNativeInterface* self);

/* ==================== 原生资源查询（对标 QPlatformNativeInterface） ==================== */

/**
 * @brief      查询集成层原生资源。
 * @param      resource UTF-8 资源名；未知返回 NULL。见文件头资源表。
 * @return     进程内借用句柄；未知资源/入参非法返回 NULL。
 */
void* XPlatformNativeInterface_nativeResourceForIntegration(
        const XPlatformNativeInterface* self, const char* resource);

/**
 * @brief      查询窗口原生资源。
 * @param      resource UTF-8 资源名；未知返回 NULL。见文件头资源表。
 * @param      window 目标窗口借用指针；可为 NULL。
 * @return     进程内借用句柄；未知资源/入参非法返回 NULL。
 */
void* XPlatformNativeInterface_nativeResourceForWindow(
        const XPlatformNativeInterface* self, const char* resource, XWindow* window);

/**
 * @brief      查询屏幕原生资源。
 * @param      resource UTF-8 资源名；未知返回 NULL。见文件头资源表。
 * @param      screen 目标屏幕借用指针；NULL 时回退主屏幕。
 * @return     进程内借用句柄；未知资源返回 NULL。
 */
void* XPlatformNativeInterface_nativeResourceForScreen(
        const XPlatformNativeInterface* self, const char* resource, XScreen* screen);

/**
 * @brief      查询后备存储（BackingStore）原生资源。
 * @details    资源名 "paintdevice" 返回后端内部 XImage 绘制设备
 *             （对标 Qt 栅格后备存储的相同资源键）；后端未实现/开关
 *             关闭时恒返回 NULL。
 * @param      backingStore 平台后备存储句柄借用指针；可为 NULL。
 * @return     进程内借用句柄；未知资源或不可用返回 NULL。
 */
void* XPlatformNativeInterface_nativeResourceForBackingStore(
        const XPlatformNativeInterface* self, const char* resource, void* backingStore);

/**
 * @brief      查询光标原生资源。
 * @details    嵌入式无系统光标句柄，恒返回 NULL。
 * @return     恒 NULL。
 */
void* XPlatformNativeInterface_nativeResourceForCursor(
        const XPlatformNativeInterface* self, const char* resource, XCursor* cursor);

/**
 * @brief      查询集成层原生资源函数（当前未注册时返回 NULL）。
 * @return     已注册函数指针或 NULL。
 */
void* XPlatformNativeInterface_nativeResourceFunctionForIntegration(
        const XPlatformNativeInterface* self, const char* resource);

/**
 * @brief      查询屏幕原生资源函数（当前未注册时返回 NULL）。
 * @return     已注册函数指针或 NULL。
 */
void* XPlatformNativeInterface_nativeResourceFunctionForScreen(
        const XPlatformNativeInterface* self, const char* resource);

/**
 * @brief      查询窗口原生资源函数（当前未注册时返回 NULL）。
 * @return     已注册函数指针或 NULL。
 */
void* XPlatformNativeInterface_nativeResourceFunctionForWindow(
        const XPlatformNativeInterface* self, const char* resource);

/**
 * @brief      查询后备存储原生资源函数（当前未注册时返回 NULL）。
 * @return     已注册函数指针或 NULL。
 */
void* XPlatformNativeInterface_nativeResourceFunctionForBackingStore(
        const XPlatformNativeInterface* self, const char* resource);

/**
 * @brief      查询光标原生资源函数（当前未注册时返回 NULL）。
 * @return     已注册函数指针或 NULL。
 */
void* XPlatformNativeInterface_nativeResourceFunctionForCursor(
        const XPlatformNativeInterface* self, const char* resource);

/**
 * @brief      按名称解析平台函数指针（对标 QPlatformNativeInterface::platformFunction）。
 * @details    名称按 UTF-8 严格区分大小写；未注册或名称非法返回 NULL。
 * @param      name UTF-8 函数名；可为 NULL。
 * @return     已注册函数指针或 NULL。
 */
void* XPlatformNativeInterface_platformFunction(
        const XPlatformNativeInterface* self, const char* name);

/**
 * @brief      注册或覆盖一个平台函数。
 * @details    function 为 NULL 时注销 name；名称由对象深拷贝，函数指针仅保存
 *             值不负责其生命周期。该入口是 C API 对平台插件注册表的显式适配。
 * @param      self 目标原生接口；可为 NULL。
 * @param      name UTF-8 函数名；不可为 NULL 或空串。
 * @param      function 函数指针；NULL 表示注销已有条目。
 * @return     true 注册/覆盖/注销成功；false 参数非法或注册表已满。
 */
bool XPlatformNativeInterface_registerPlatformFunction(
        XPlatformNativeInterface* self, const char* name, void* function);

/* ==================== 窗口原生属性（对标 QPlatformNativeInterface） ==================== */

/**
 * @brief      返回窗口完整原生属性表。
 * @param      self 目标对象；可为 NULL。
 * @param      platformWindow 平台窗口借用指针；可为 NULL。
 * @return     内部 XVariantHashMap 借用指针；无效参数返回 NULL。
 */
XVariantHashMap* XPlatformNativeInterface_windowProperties(
        const XPlatformNativeInterface* self, XPlatformWindow* platformWindow);

/**
 * @brief      读取窗口原生属性（对标 windowProperty(window, name)）。
 * @param      self 目标对象；可为 NULL。
 * @param      platformWindow 平台窗口借用指针；可为 NULL。
 * @param      name UTF-8 属性名；可为 NULL。
 * @return     内部 XVariant 借用指针；不存在或参数非法返回 NULL。
 */
XVariant* XPlatformNativeInterface_windowProperty(
        const XPlatformNativeInterface* self, XPlatformWindow* platformWindow,
        const char* name);

/**
 * @brief      读取窗口原生属性，不存在时返回默认值（对标
 *             windowProperty(window, name, defaultValue)）。
 * @details    无论命中与否都返回新建堆对象（QPlatformNativeInterface 按值
 *             返回 QVariant 的 C 等价），调用方用 XVariant_delete_base 释放。
 * @param      self 目标对象；可为 NULL。
 * @param      platformWindow 平台窗口借用指针；可为 NULL。
 * @param      name UTF-8 属性名；可为 NULL。
 * @param      defaultValue 默认值借用指针；可为 NULL（此时不存在返回 NULL）。
 * @return     新建 XVariant（命中拷贝或默认值拷贝）；参数非法返回 NULL。
 */
XVariant* XPlatformNativeInterface_windowProperty_2(
        const XPlatformNativeInterface* self, XPlatformWindow* platformWindow,
        const char* name, const XVariant* defaultValue);

/**
 * @brief      写入窗口原生属性并对标 Qt 语义发射 windowPropertyChanged。
 * @details    与 Qt 一致：写入成功后发射 windowPropertyChanged（参数为
 *             XPlatformWindow* 与属性名 XString*）；value 为 NULL 时删除属性。
 * @param      self 目标对象；可为 NULL。
 * @param      platformWindow 平台窗口借用指针；可为 NULL。
 * @param      name UTF-8 属性名；可为 NULL。
 * @param      value 属性值借用指针；可为 NULL（删除）。
 */
void XPlatformNativeInterface_setWindowProperty(
        XPlatformNativeInterface* self, XPlatformWindow* platformWindow,
        const char* name, const XVariant* value);

/**
 * @brief      窗口原生属性变化通知信号（对标 windowPropertyChanged）。
 * @param      self 目标对象；可为 NULL。
 * @param      platformWindow 变化的平台窗口借用指针。
 * @param      propertyName 变化的属性名（新建 XString，由信号系统释放）。
 * @return     信号 ID（供 XSignal 宏取用）。
 */
void* XPlatformNativeInterface_windowPropertyChanged_signal(
        XPlatformNativeInterface* self, XPlatformWindow* platformWindow,
        const XString* propertyName);

#ifdef __cplusplus
}
#endif

#endif /* XPLATFORMNATIVEINTERFACE_ON */
#endif /* XPLATFORMNATIVEINTERFACE_H */
