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
void XPlatformOpenGLContext_destroy(XPlatformOpenGLContext* self);
bool XPlatformOpenGLContext_isValid(const XPlatformOpenGLContext* self);
bool XPlatformOpenGLContext_makeCurrent(XPlatformOpenGLContext* self);
void XPlatformOpenGLContext_doneCurrent(XPlatformOpenGLContext* self);
bool XPlatformOpenGLContext_swapBuffers(XPlatformOpenGLContext* self);
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
void XPlatformVulkanInstance_destroy(XPlatformVulkanInstance* self);
bool XPlatformVulkanInstance_isValid(const XPlatformVulkanInstance* self);
uint32_t XPlatformVulkanInstance_physicalDeviceCount(
        const XPlatformVulkanInstance* self);
uint32_t XPlatformVulkanInstance_apiVersion(const XPlatformVulkanInstance* self);

/* Drive 后端入口：只传递不透明状态，不暴露任何系统 API。 */
bool XPlatformGraphicsDriver_openGLAvailable(void);
bool XPlatformGraphicsDriver_vulkanAvailable(void);
bool XPlatformGraphicsDriver_createOpenGL(XWindow* window, void** nativeState);
void XPlatformGraphicsDriver_destroyOpenGL(void* nativeState);
bool XPlatformGraphicsDriver_makeCurrentOpenGL(void* nativeState);
void XPlatformGraphicsDriver_doneCurrentOpenGL(void* nativeState);
bool XPlatformGraphicsDriver_swapBuffersOpenGL(void* nativeState);
void* XPlatformGraphicsDriver_openGLProcAddress(void* nativeState,
                                                 const char* name);
bool XPlatformGraphicsDriver_createVulkan(void** nativeState,
                                          uint32_t* physicalDeviceCount,
                                          uint32_t* apiVersion);
void XPlatformGraphicsDriver_destroyVulkan(void* nativeState);

/** @brief 创建可绑定 OpenGL 的离屏表面（默认 1x1，Drive 决定实现）。 */
bool XPlatformGraphicsDriver_createOffscreen(uint32_t width, uint32_t height,
                                             void** nativeState);
void XPlatformGraphicsDriver_destroyOffscreen(void* nativeState);
bool XPlatformGraphicsDriver_makeCurrentOffscreen(void* nativeState);
void XPlatformGraphicsDriver_doneCurrentOffscreen(void* nativeState);

XPlatformOffscreenSurface* XPlatformOffscreenSurface_create_ex(
        XMemoryType memory, uint32_t width, uint32_t height);
#define XPlatformOffscreenSurface_create(width, height) \
    XPlatformOffscreenSurface_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, (width), (height))
void XPlatformOffscreenSurface_destroy(XPlatformOffscreenSurface* self);
bool XPlatformOffscreenSurface_isValid(const XPlatformOffscreenSurface* self);
bool XPlatformOffscreenSurface_makeCurrent(XPlatformOffscreenSurface* self);
void XPlatformOffscreenSurface_doneCurrent(XPlatformOffscreenSurface* self);
uint32_t XPlatformOffscreenSurface_width(const XPlatformOffscreenSurface* self);
uint32_t XPlatformOffscreenSurface_height(const XPlatformOffscreenSurface* self);

#endif /* XPLATFORMINTEGRATION_ON */

#ifdef __cplusplus
}
#endif
#endif /* XPLATFORMGRAPHICS_H */
