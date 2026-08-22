/****************************************************************************
 * @file       XPlatformGraphics.c
 * @brief      平台图形公共对象实现；不包含任何平台图形 API。
 ****************************************************************************/
#include "XPlatformGraphics.h"

#if XPLATFORMINTEGRATION_ON

struct XPlatformOpenGLContext
{
    void* m_nativeState;
    XMemoryType m_memory;
    bool m_valid;
};

struct XPlatformVulkanInstance
{
    void* m_nativeState;
    XMemoryType m_memory;
    uint32_t m_physicalDeviceCount;
    uint32_t m_apiVersion;
    bool m_valid;
};

struct XPlatformOffscreenSurface
{
    void* m_nativeState;
    XMemoryType m_memory;
    uint32_t m_width;
    uint32_t m_height;
    bool m_valid;
};

bool XPlatformGraphics_isOpenGLAvailable(void)
{
    return XPlatformGraphicsDriver_openGLAvailable();
}

bool XPlatformGraphics_isVulkanAvailable(void)
{
    return XPlatformGraphicsDriver_vulkanAvailable();
}

XPlatformOpenGLContext* XPlatformOpenGLContext_create_ex(XMemoryType memory,
                                                          XWindow* window)
{
    XPlatformOpenGLContext* self;
    void* nativeState = NULL;
    if (!window || !XPlatformGraphicsDriver_createOpenGL(window, &nativeState) ||
        !nativeState)
        return NULL;
    self = (XPlatformOpenGLContext*)XMemory_malloc(sizeof(*self), memory);
    if (!self) {
        XPlatformGraphicsDriver_destroyOpenGL(nativeState);
        return NULL;
    }
    self->m_nativeState = nativeState;
    self->m_memory = memory;
    self->m_valid = true;
    return self;
}

void XPlatformOpenGLContext_destroy(XPlatformOpenGLContext* self)
{
    if (!self) return;
    if (self->m_nativeState)
        XPlatformGraphicsDriver_destroyOpenGL(self->m_nativeState);
    XMemory_free(self, self->m_memory);
}

bool XPlatformOpenGLContext_isValid(const XPlatformOpenGLContext* self)
{
    return self && self->m_valid && self->m_nativeState;
}

bool XPlatformOpenGLContext_makeCurrent(XPlatformOpenGLContext* self)
{
    return XPlatformOpenGLContext_isValid(self) &&
           XPlatformGraphicsDriver_makeCurrentOpenGL(self->m_nativeState);
}

void XPlatformOpenGLContext_doneCurrent(XPlatformOpenGLContext* self)
{
    if (XPlatformOpenGLContext_isValid(self))
        XPlatformGraphicsDriver_doneCurrentOpenGL(self->m_nativeState);
}

bool XPlatformOpenGLContext_swapBuffers(XPlatformOpenGLContext* self)
{
    return XPlatformOpenGLContext_isValid(self) &&
           XPlatformGraphicsDriver_swapBuffersOpenGL(self->m_nativeState);
}

void* XPlatformOpenGLContext_getProcAddress(const XPlatformOpenGLContext* self,
                                            const char* name)
{
    if (!XPlatformOpenGLContext_isValid(self) || !name || !*name) return NULL;
    return XPlatformGraphicsDriver_openGLProcAddress(self->m_nativeState, name);
}

XPlatformVulkanInstance* XPlatformVulkanInstance_create_ex(XMemoryType memory)
{
    XPlatformVulkanInstance* self;
    void* nativeState = NULL;
    uint32_t deviceCount = 0;
    uint32_t apiVersion = 0;
    if (!XPlatformGraphicsDriver_createVulkan(&nativeState, &deviceCount,
                                               &apiVersion) || !nativeState)
        return NULL;
    self = (XPlatformVulkanInstance*)XMemory_malloc(sizeof(*self), memory);
    if (!self) {
        XPlatformGraphicsDriver_destroyVulkan(nativeState);
        return NULL;
    }
    self->m_nativeState = nativeState;
    self->m_memory = memory;
    self->m_physicalDeviceCount = deviceCount;
    self->m_apiVersion = apiVersion;
    self->m_valid = true;
    return self;
}

void XPlatformVulkanInstance_destroy(XPlatformVulkanInstance* self)
{
    if (!self) return;
    if (self->m_nativeState)
        XPlatformGraphicsDriver_destroyVulkan(self->m_nativeState);
    XMemory_free(self, self->m_memory);
}

bool XPlatformVulkanInstance_isValid(const XPlatformVulkanInstance* self)
{
    return self && self->m_valid && self->m_nativeState;
}

uint32_t XPlatformVulkanInstance_physicalDeviceCount(
        const XPlatformVulkanInstance* self)
{
    return XPlatformVulkanInstance_isValid(self) ?
           self->m_physicalDeviceCount : 0;
}

uint32_t XPlatformVulkanInstance_apiVersion(const XPlatformVulkanInstance* self)
{
    return XPlatformVulkanInstance_isValid(self) ? self->m_apiVersion : 0;
}

XPlatformOffscreenSurface* XPlatformOffscreenSurface_create_ex(
        XMemoryType memory, uint32_t width, uint32_t height)
{
    XPlatformOffscreenSurface* self;
    void* nativeState = NULL;
    if (width == 0) width = 1;
    if (height == 0) height = 1;
    if (!XPlatformGraphicsDriver_createOffscreen(width, height, &nativeState) ||
        !nativeState)
        return NULL;
    self = (XPlatformOffscreenSurface*)XMemory_malloc(sizeof(*self), memory);
    if (!self) {
        XPlatformGraphicsDriver_destroyOffscreen(nativeState);
        return NULL;
    }
    self->m_nativeState = nativeState;
    self->m_memory = memory;
    self->m_width = width;
    self->m_height = height;
    self->m_valid = true;
    return self;
}

void XPlatformOffscreenSurface_destroy(XPlatformOffscreenSurface* self)
{
    if (!self) return;
    if (self->m_nativeState)
        XPlatformGraphicsDriver_destroyOffscreen(self->m_nativeState);
    XMemory_free(self, self->m_memory);
}

bool XPlatformOffscreenSurface_isValid(const XPlatformOffscreenSurface* self)
{
    return self && self->m_valid && self->m_nativeState;
}

bool XPlatformOffscreenSurface_makeCurrent(XPlatformOffscreenSurface* self)
{
    return XPlatformOffscreenSurface_isValid(self) &&
           XPlatformGraphicsDriver_makeCurrentOffscreen(self->m_nativeState);
}

void XPlatformOffscreenSurface_doneCurrent(XPlatformOffscreenSurface* self)
{
    if (XPlatformOffscreenSurface_isValid(self))
        XPlatformGraphicsDriver_doneCurrentOffscreen(self->m_nativeState);
}

uint32_t XPlatformOffscreenSurface_width(const XPlatformOffscreenSurface* self)
{
    return self ? self->m_width : 0;
}

uint32_t XPlatformOffscreenSurface_height(const XPlatformOffscreenSurface* self)
{
    return self ? self->m_height : 0;
}

#endif /* XPLATFORMINTEGRATION_ON */
