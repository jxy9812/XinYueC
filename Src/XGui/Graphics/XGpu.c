/****************************************************************************
 * @file       XGpu.c
 * @brief      统一 GPU 运行时实现；仅组合平台无关后端对象。
 * @details    平台上下文创建对齐 Qt 的 QPA 链路：
 *             QOpenGLContext/QVulkanInstance 经 QGuiApplication 的平台集成层
 *             （QPlatformIntegration::createPlatformOpenGLContext /
 *             createPlatformVulkanInstance）创建平台上下文；XGpu 同样经
 *             XPlatformIntegration 的对应工厂转发到 XPlatformGraphics 的
 *             XPlatformOpenGLContext / XPlatformVulkanInstance，再落到 Drive
 *             的 GLX/WGL/Vulkan 驱动。无 GUI 应用实例（集成层）时创建失败，
 *             与 Qt 的「无 QGuiApplication 时 QOpenGLContext::create 失败」
 *             语义一致。
 ****************************************************************************/
#include "XGpu.h"
#include "XPlatformGraphics.h"
#include "XPlatformIntegration.h"
#if XGUIAPPLICATION_ON && XPLATFORMNATIVEINTERFACE_ON
#include "XGuiApplication.h"
#include "XPlatformNativeInterface.h"
#endif /* XGUIAPPLICATION_ON && XPLATFORMNATIVEINTERFACE_ON */
#include <string.h>

#if XPLATFORMINTEGRATION_ON && XGPU_ON

struct XGpu
{
    XMemoryType m_memory;
    XGpuBackend m_backend;
    XGpuAdapterInfo m_adapterInfo;
    union {
        XPlatformOpenGLContext* m_openGL;
        XPlatformVulkanInstance* m_vulkan;
        void* m_opaque;
    } m_handle;
};

/** @brief 获取平台集成层（对齐 Qt QOpenGLContext 内部拿
 *         QGuiApplicationPrivate::platformIntegration）。 */
static XPlatformIntegration* xgpu_integration(void)
{
#if XGUIAPPLICATION_ON && XPLATFORMNATIVEINTERFACE_ON
    XPlatformNativeInterface* ni = XGuiApplication_platformNativeInterface();
    return ni ? XPlatformNativeInterface_integration(ni) : NULL;
#else
    return NULL;
#endif /* XGUIAPPLICATION_ON && XPLATFORMNATIVEINTERFACE_ON */
}

static XGpu* xgpu_allocate(XMemoryType memory)
{
    XGpu* self = (XGpu*)XMemory_malloc(sizeof(*self), memory);
    if (!self) return NULL;
    memset(self, 0, sizeof(*self));
    self->m_memory = memory;
    return self;
}

XGpu* XGpu_create_ex(XMemoryType memory, XGpuBackend preferredBackend,
                     XWindow* surfaceWindow)
{
    XGpu* self;
    XPlatformIntegration* integration;
    XGpuBackend selected = preferredBackend;
    if (selected == XGpuBackend_Auto) {
        selected = surfaceWindow ? XGpuBackend_OpenGL : XGpuBackend_Vulkan;
    }
    /* 对齐 Qt：无 GUI 应用（集成层）时创建失败。 */
    integration = xgpu_integration();
    if (!integration) return NULL;
    self = xgpu_allocate(memory);
    if (!self) return NULL;
    if (selected == XGpuBackend_OpenGL) {
        self->m_handle.m_openGL = (XPlatformOpenGLContext*)
            XPlatformIntegration_createPlatformOpenGLContext(
                integration, (void*)surfaceWindow);
        if (!self->m_handle.m_openGL) goto failed;
        self->m_backend = XGpuBackend_OpenGL;
        self->m_adapterInfo.m_backend = XGpuBackend_OpenGL;
        self->m_adapterInfo.m_adapterCount = 1;
        self->m_adapterInfo.m_supportsPresentation = true;
    } else if (selected == XGpuBackend_Vulkan) {
        self->m_handle.m_vulkan = (XPlatformVulkanInstance*)
            XPlatformIntegration_createPlatformVulkanInstance(integration, NULL);
        if (!self->m_handle.m_vulkan) goto failed;
        self->m_backend = XGpuBackend_Vulkan;
        self->m_adapterInfo.m_backend = XGpuBackend_Vulkan;
        self->m_adapterInfo.m_apiVersion =
            XPlatformVulkanInstance_apiVersion(self->m_handle.m_vulkan);
        self->m_adapterInfo.m_adapterCount =
            XPlatformVulkanInstance_physicalDeviceCount(self->m_handle.m_vulkan);
        self->m_adapterInfo.m_supportsExplicitCommands = true;
    } else {
        goto failed;
    }
    return self;
failed:
    XMemory_free(self, memory);
    return NULL;
}

void XGpu_destroy(XGpu* self)
{
    if (!self) return;
    if (self->m_backend == XGpuBackend_OpenGL)
        XPlatformOpenGLContext_destroy(self->m_handle.m_openGL);
    else if (self->m_backend == XGpuBackend_Vulkan)
        XPlatformVulkanInstance_destroy(self->m_handle.m_vulkan);
    XMemory_free(self, self->m_memory);
}

bool XGpu_isValid(const XGpu* self)
{
    if (!self) return false;
    if (self->m_backend == XGpuBackend_OpenGL)
        return XPlatformOpenGLContext_isValid(self->m_handle.m_openGL);
    if (self->m_backend == XGpuBackend_Vulkan)
        return XPlatformVulkanInstance_isValid(self->m_handle.m_vulkan);
    return false;
}

XGpuBackend XGpu_backend(const XGpu* self)
{ return XGpu_isValid(self) ? self->m_backend : XGpuBackend_None; }

XGpuAdapterInfo XGpu_adapterInfo(const XGpu* self)
{
    XGpuAdapterInfo empty;
    memset(&empty, 0, sizeof(empty));
    return XGpu_isValid(self) ? self->m_adapterInfo : empty;
}

bool XGpu_makeCurrent(XGpu* self)
{
    return XGpu_isValid(self) && self->m_backend == XGpuBackend_OpenGL &&
           XPlatformOpenGLContext_makeCurrent(self->m_handle.m_openGL);
}

void XGpu_doneCurrent(XGpu* self)
{
    if (XGpu_isValid(self) && self->m_backend == XGpuBackend_OpenGL)
        XPlatformOpenGLContext_doneCurrent(self->m_handle.m_openGL);
}

bool XGpu_present(XGpu* self)
{
    return XGpu_isValid(self) && self->m_backend == XGpuBackend_OpenGL &&
           XPlatformOpenGLContext_swapBuffers(self->m_handle.m_openGL);
}

void* XGpu_getProcAddress(const XGpu* self, const char* name)
{
    return XGpu_isValid(self) && self->m_backend == XGpuBackend_OpenGL ?
           XPlatformOpenGLContext_getProcAddress(self->m_handle.m_openGL, name) : NULL;
}

#endif /* XPLATFORMINTEGRATION_ON && XGPU_ON */
