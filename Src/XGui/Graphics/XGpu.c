/****************************************************************************
 * @file       XGpu.c
 * @brief      统一 GPU 运行时实现；仅组合平台无关后端对象。
 ****************************************************************************/
#include "XGpu.h"
#include "XPlatformGraphics.h"
#include <string.h>

#if XPLATFORMINTEGRATION_ON

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
    XGpuBackend selected = preferredBackend;
    if (selected == XGpuBackend_Auto) {
        selected = surfaceWindow ? XGpuBackend_OpenGL : XGpuBackend_Vulkan;
    }
    self = xgpu_allocate(memory);
    if (!self) return NULL;
    if (selected == XGpuBackend_OpenGL) {
        self->m_handle.m_openGL = XPlatformOpenGLContext_create(surfaceWindow);
        if (!self->m_handle.m_openGL) goto failed;
        self->m_backend = XGpuBackend_OpenGL;
        self->m_adapterInfo.m_backend = XGpuBackend_OpenGL;
        self->m_adapterInfo.m_adapterCount = 1;
        self->m_adapterInfo.m_supportsPresentation = true;
    } else if (selected == XGpuBackend_Vulkan) {
        self->m_handle.m_vulkan = XPlatformVulkanInstance_create();
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

#endif /* XPLATFORMINTEGRATION_ON */
