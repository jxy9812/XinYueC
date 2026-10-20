/****************************************************************************
 * @file       XPlatformGraphics.h
 * @brief      平台图形上下文与 Vulkan 实例公共契约。
 * @details    本文件只定义平台无关的对象生命周期和操作接口；X11/GLX、
 *             Win32/WGL、Vulkan SDK 等系统类型及调用均只能位于 Drive。
 *             OpenGL 上下文依附现有 XWindow 的原生表面，Vulkan 实例可
 *             枚举可用物理设备，二者在嵌入式裁剪或无图形驱动时安全回退。
 ****************************************************************************/
#ifndef XPLATFORMGRAPHICS_H
#define XPLATFORMGRAPHICS_H

#if XGUI_ON
#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stdint.h>
#include "XGuiConfig.h"
#include "XMemory.h"

#if XWINDOW_ON
typedef struct XWindow XWindow;
#endif

#if XPLATFORMINTEGRATION_ON

/** @brief 平台 OpenGL 上下文；实现细节保持私有，不能包含系统句柄。 */
typedef struct XPlatformOpenGLContext XPlatformOpenGLContext;
/** @brief 平台 Vulkan 实例；实现细节保持私有，不能包含系统句柄。 */
typedef struct XPlatformVulkanInstance XPlatformVulkanInstance;
/** @brief 无窗口 GPU 离屏表面；内部句柄由 Drive 保存。 */
typedef struct XPlatformOffscreenSurface XPlatformOffscreenSurface;

/** @brief 查询当前构建与运行环境是否可创建窗口 OpenGL 上下文。 */
bool XPlatformGraphics_isOpenGLAvailable(void);
/** @brief 查询当前构建与运行环境是否可创建 Vulkan 实例。 */
bool XPlatformGraphics_isVulkanAvailable(void);

/**
 * @brief      为已创建原生表面的窗口创建 OpenGL 上下文。
 * @details    window 必须已显示或已显式创建原生句柄；创建失败返回 NULL，
 *             不会改变窗口状态。返回对象归调用方所有。
 */
XPlatformOpenGLContext* XPlatformOpenGLContext_create_ex(XMemoryType memory,
                                                          XWindow* window);
#define XPlatformOpenGLContext_create(window) \
    XPlatformOpenGLContext_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, (window))
/**
 * @brief      释放 OpenGL 上下文及其平台原生状态。
 * @param      self 目标上下文；可为 NULL（不执行任何操作）。
 * @return     无。释放后句柄不可再用。
 */
void XPlatformOpenGLContext_destroy(XPlatformOpenGLContext* self);
/**
 * @brief      查询 OpenGL 上下文是否有效。
 * @param      self 目标上下文；可为 NULL。
 * @return     上下文有效且持有平台原生状态返回 true。
 */
bool XPlatformOpenGLContext_isValid(const XPlatformOpenGLContext* self);
/**
 * @brief      把上下文绑定为当前线程的当前上下文。
 * @param      self 目标上下文；可为 NULL。
 * @return     绑定成功返回 true；上下文无效或平台绑定失败返回 false。
 */
bool XPlatformOpenGLContext_makeCurrent(XPlatformOpenGLContext* self);
/**
 * @brief      解除当前线程的上下文绑定。
 * @param      self 目标上下文；可为 NULL。
 * @return     无。上下文无效时不执行任何操作。
 */
void XPlatformOpenGLContext_doneCurrent(XPlatformOpenGLContext* self);
/**
 * @brief      交换窗口表面缓冲（对标 QOpenGLContext::swapBuffers）。
 * @param      self 目标上下文；可为 NULL。
 * @return     交换成功返回 true；上下文无效或平台交换失败返回 false。
 */
bool XPlatformOpenGLContext_swapBuffers(XPlatformOpenGLContext* self);
/**
 * @brief      查询 OpenGL 扩展函数指针。
 * @param      self 目标上下文；可为 NULL。
 * @param      name GL 函数名（如 "glGenFramebuffers"）；不能为 NULL 或
 *             空字符串。
 * @return     函数指针（进程级借用，无需释放）；上下文无效或名称为空
 *             返回 NULL。
 */
void* XPlatformOpenGLContext_getProcAddress(const XPlatformOpenGLContext* self,
                                            const char* name);

/**
 * @brief      创建 Vulkan 实例并枚举物理设备。
 * @details    此对象仅管理 VkInstance；窗口表面和逻辑设备由后续 RHI/渲染
 *             层按相同 Drive 边界扩展。返回对象归调用方所有。
 */
XPlatformVulkanInstance* XPlatformVulkanInstance_create_ex(XMemoryType memory);
#define XPlatformVulkanInstance_create() \
    XPlatformVulkanInstance_create_ex(XCLASS_DEFAULT_MEMORY_TYPE)
/**
 * @brief      释放 Vulkan 实例及其平台原生状态。
 * @param      self 目标实例；可为 NULL（不执行任何操作）。
 * @return     无。释放后句柄不可再用。
 */
void XPlatformVulkanInstance_destroy(XPlatformVulkanInstance* self);
/**
 * @brief      查询 Vulkan 实例是否有效。
 * @param      self 目标实例；可为 NULL。
 * @return     实例有效且持有平台原生状态返回 true。
 */
bool XPlatformVulkanInstance_isValid(const XPlatformVulkanInstance* self);
/**
 * @brief      返回实例枚举到的物理设备数量。
 * @param      self 目标实例；可为 NULL。
 * @return     物理设备数；实例无效返回 0。
 */
uint32_t XPlatformVulkanInstance_physicalDeviceCount(
        const XPlatformVulkanInstance* self);
/**
 * @brief      返回实例的 Vulkan API 版本（Vulkan 标准打包格式）。
 * @param      self 目标实例；可为 NULL。
 * @return     API 版本打包值；实例无效返回 0。
 */
uint32_t XPlatformVulkanInstance_apiVersion(const XPlatformVulkanInstance* self);

/* Drive 后端入口：只传递不透明状态，不暴露任何系统 API。 */
/**
 * @brief      查询当前构建与运行环境是否可创建窗口 OpenGL 上下文
 *             （Drive 后端实现）。
 * @return     平台后端可用返回 true；不支持或后端加载失败返回 false。
 */
bool XPlatformGraphicsDriver_openGLAvailable(void);
/**
 * @brief      查询当前构建与运行环境是否可创建 Vulkan 实例
 *             （Drive 后端实现）。
 * @return     平台后端可用返回 true；不支持或后端加载失败返回 false。
 */
bool XPlatformGraphicsDriver_vulkanAvailable(void);
/**
 * @brief      为窗口创建平台 OpenGL 原生状态（GLX/WGL，Drive 实现）。
 * @param      window 已持有原生句柄的目标窗口借用指针。
 * @param      outState 输出平台原生状态（由 Driver 拥有，经 destroyOpenGL
 *             释放）；失败时置 NULL。
 * @return     创建成功返回 true；平台不支持或创建失败返回 false。
 */
bool XPlatformGraphicsDriver_createOpenGL(XWindow* window, void** nativeState);
/**
 * @brief      释放 createOpenGL 输出的平台原生状态。
 * @param      nativeState 平台原生状态；可为 NULL（不执行任何操作）。
 * @return     无。
 */
void XPlatformGraphicsDriver_destroyOpenGL(void* nativeState);
/**
 * @brief      把 OpenGL 原生状态绑定为当前线程上下文。
 * @param      nativeState 平台原生状态。
 * @return     绑定成功返回 true；状态无效或平台绑定失败返回 false。
 */
bool XPlatformGraphicsDriver_makeCurrentOpenGL(void* nativeState);
/**
 * @brief      解除当前线程的 OpenGL 上下文绑定。
 * @param      nativeState 平台原生状态。
 * @return     无。
 */
void XPlatformGraphicsDriver_doneCurrentOpenGL(void* nativeState);
/**
 * @brief      交换窗口表面缓冲。
 * @param      nativeState 平台原生状态。
 * @return     交换成功返回 true；状态无效或平台交换失败返回 false。
 */
bool XPlatformGraphicsDriver_swapBuffersOpenGL(void* nativeState);
/**
 * @brief      查询 OpenGL 扩展函数指针（glX/wgl 全局查询）。
 * @param      nativeState 平台原生状态。
 * @param      name GL 函数名。
 * @return     函数指针（进程级借用，无需释放）；无效返回 NULL。
 */
void* XPlatformGraphicsDriver_openGLProcAddress(void* nativeState,
                                                 const char* name);
/**
 * @brief      创建 Vulkan 实例并枚举物理设备（Drive 实现）。
 * @param      outState 输出平台原生状态（由 Driver 拥有，经 destroyVulkan
 *             释放）；失败时置 NULL。
 * @param      physicalDeviceCount 输出物理设备数；不能为 NULL。
 * @param      apiVersion 输出 API 版本（Vulkan 标准打包格式）；不能为 NULL。
 * @return     创建成功返回 true；平台不支持或创建失败返回 false。
 */
bool XPlatformGraphicsDriver_createVulkan(void** nativeState,
                                          uint32_t* physicalDeviceCount,
                                          uint32_t* apiVersion);
/**
 * @brief      释放 createVulkan 输出的平台原生状态。
 * @param      nativeState 平台原生状态；可为 NULL（不执行任何操作）。
 * @return     无。
 */
void XPlatformGraphicsDriver_destroyVulkan(void* nativeState);

/**
 * @brief      查询窗口 Vulkan 表面所需的 instance 扩展名（平台相关，
 *             如 X11 的 VK_KHR_xlib_surface / Win32 的 VK_KHR_win32_surface，
 *             均含 VK_KHR_surface 基础扩展）。
 * @param      outNames 返回扩展名字符串数组（静态存储期，借用指针）。
 * @param      outCount 返回扩展名数量。
 * @return     平台支持窗口 Vulkan 表面时返回 true；不支持时两个出参
 *             均置 0/NULL 并返回 false。
 */
bool XPlatformGraphicsDriver_vulkanWindowSurfaceExtensions(
        const char* const** outNames, uint32_t* outCount);

/**
 * @brief      为窗口创建平台 Vulkan 表面（X11 Xlib / Win32 surface，
 *             系统 API 实现仅位于 Drive）。
 * @param      instance Vulkan VkInstance（不透明传递）。
 * @param      window 已持有原生句柄的目标窗口。
 * @param      outSurface 返回 VkSurfaceKHR 句柄（不透明传递）。
 * @return     创建成功返回 true；平台不支持或创建失败返回 false。
 */
bool XPlatformGraphicsDriver_createVulkanWindowSurface(void* instance,
                                                       XWindow* window,
                                                       void** outSurface);

/**
 * @brief      销毁由 createVulkanWindowSurface 创建的平台 Vulkan 表面。
 * @param      instance Vulkan VkInstance（不透明传递）；可为 NULL（不执行
 *             任何操作）。
 * @param      surface createVulkanWindowSurface 输出的 VkSurfaceKHR 句柄
 *             （不透明传递）；可为 NULL（不执行任何操作）。
 * @return     无。销毁后句柄不可再用。
 */
void XPlatformGraphicsDriver_destroyVulkanWindowSurface(void* instance,
                                                        void* surface);

/**
 * @brief      创建可绑定 OpenGL 的离屏表面（默认 1x1，Drive 决定实现）。
 * @param      width 期望宽度（像素）；0 按 1 处理。
 * @param      height 期望高度（像素）；0 按 1 处理。
 * @param      outState 输出平台原生状态（由 Driver 拥有，经
 *             destroyOffscreen 释放）；失败时置 NULL。
 * @return     创建成功返回 true；平台不支持或创建失败返回 false。
 */
bool XPlatformGraphicsDriver_createOffscreen(uint32_t width, uint32_t height,
                                             void** nativeState);
/**
 * @brief      释放 createOffscreen 输出的平台原生状态。
 * @param      nativeState 平台原生状态；可为 NULL（不执行任何操作）。
 * @return     无。
 */
void XPlatformGraphicsDriver_destroyOffscreen(void* nativeState);
/**
 * @brief      把离屏 GL 原生状态绑定为当前线程上下文。
 * @param      nativeState 平台原生状态。
 * @return     绑定成功返回 true；状态无效或平台绑定失败返回 false。
 */
bool XPlatformGraphicsDriver_makeCurrentOffscreen(void* nativeState);
/**
 * @brief      解除当前线程的离屏 GL 上下文绑定。
 * @param      nativeState 平台原生状态。
 * @return     无。
 */
void XPlatformGraphicsDriver_doneCurrentOffscreen(void* nativeState);

/**
 * @brief      使用指定内存类型创建无窗口 GPU 离屏表面。
 * @param      memory 对象内存类型。
 * @param      width 期望宽度（像素）；0 按 1 处理。
 * @param      height 期望高度（像素）；0 按 1 处理。
 * @return     新对象指针；平台不支持或分配失败返回 NULL，调用方用
 *             XPlatformOffscreenSurface_destroy 释放。
 */
XPlatformOffscreenSurface* XPlatformOffscreenSurface_create_ex(
        XMemoryType memory, uint32_t width, uint32_t height);
/**
 * @brief      使用默认内存类型创建无窗口 GPU 离屏表面。
 * @param      width 期望宽度（像素）；0 按 1 处理。
 * @param      height 期望高度（像素）；0 按 1 处理。
 * @return     新对象指针；失败返回 NULL，调用方用
 *             XPlatformOffscreenSurface_destroy 释放。
 */
#define XPlatformOffscreenSurface_create(width, height) \
    XPlatformOffscreenSurface_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, (width), (height))
/**
 * @brief      释放离屏表面及其平台原生状态。
 * @param      self 目标表面；可为 NULL（不执行任何操作）。
 * @return     无。释放后句柄不可再用。
 */
void XPlatformOffscreenSurface_destroy(XPlatformOffscreenSurface* self);
/**
 * @brief      查询离屏表面是否有效。
 * @param      self 目标表面；可为 NULL。
 * @return     表面有效且持有平台原生状态返回 true。
 */
bool XPlatformOffscreenSurface_isValid(const XPlatformOffscreenSurface* self);
/**
 * @brief      把离屏表面绑定为当前线程的当前上下文。
 * @param      self 目标表面；可为 NULL。
 * @return     绑定成功返回 true；表面无效或平台绑定失败返回 false。
 */
bool XPlatformOffscreenSurface_makeCurrent(XPlatformOffscreenSurface* self);
/**
 * @brief      解除当前线程的离屏表面绑定。
 * @param      self 目标表面；可为 NULL。
 * @return     无。表面无效时不执行任何操作。
 */
void XPlatformOffscreenSurface_doneCurrent(XPlatformOffscreenSurface* self);
/**
 * @brief      返回离屏表面宽度。
 * @param      self 目标表面；可为 NULL。
 * @return     宽度（像素）；入参非法返回 0。
 */
uint32_t XPlatformOffscreenSurface_width(const XPlatformOffscreenSurface* self);
/**
 * @brief      返回离屏表面高度。
 * @param      self 目标表面；可为 NULL。
 * @return     高度（像素）；入参非法返回 0。
 */
uint32_t XPlatformOffscreenSurface_height(const XPlatformOffscreenSurface* self);

/**
 * @brief      查询离屏 GL 上下文中的扩展函数指针（GPU 渲染后端用）。
 * @details    调用前需先 XPlatformOffscreenSurface_makeCurrent()；查询结果
 *             与当前上下文绑定，离屏上下文可同样解析 FBO/纹理/shader 等
 *             GL 扩展。底层复用 openGLProcAddress Driver（glX/wgl 全局查询）。
 * @param      self 目标离屏表面；可为 NULL。
 * @param      name GL 函数名（如 "glGenFramebuffers"）；可为 NULL。
 * @return     函数指针；上下文无效或名称为空返回 NULL。
 */
void* XPlatformOffscreenSurface_getProcAddress(
        const XPlatformOffscreenSurface* self, const char* name);

#endif /* XPLATFORMINTEGRATION_ON */

#ifdef __cplusplus
}
#endif

#endif /* XGUI_ON */

#endif /* XPLATFORMGRAPHICS_H */
