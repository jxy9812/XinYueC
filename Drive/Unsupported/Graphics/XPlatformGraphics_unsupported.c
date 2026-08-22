/****************************************************************************
 * @file       XPlatformGraphics_unsupported.c
 * @brief      无桌面图形后端时的 XPlatformGraphics 安全回退。
 ****************************************************************************/
#include "XPlatformGraphics.h"

#if XPLATFORMINTEGRATION_ON && \
    !((defined(__linux__) && defined(XINYUE_C_HAS_X11) && \
       defined(XINYUE_C_HAS_OPENGL)) || \
      (defined(__linux__) && defined(XINYUE_C_HAS_VULKAN))) && !defined(_WIN32)

bool XPlatformGraphicsDriver_openGLAvailable(void) { return false; }
bool XPlatformGraphicsDriver_vulkanAvailable(void) { return false; }
bool XPlatformGraphicsDriver_createOpenGL(XWindow* window, void** nativeState)
{ (void)window; if (nativeState) *nativeState = NULL; return false; }
void XPlatformGraphicsDriver_destroyOpenGL(void* nativeState) { (void)nativeState; }
bool XPlatformGraphicsDriver_makeCurrentOpenGL(void* nativeState)
{ (void)nativeState; return false; }
void XPlatformGraphicsDriver_doneCurrentOpenGL(void* nativeState) { (void)nativeState; }
bool XPlatformGraphicsDriver_swapBuffersOpenGL(void* nativeState)
{ (void)nativeState; return false; }
void* XPlatformGraphicsDriver_openGLProcAddress(void* nativeState, const char* name)
{ (void)nativeState; (void)name; return NULL; }
bool XPlatformGraphicsDriver_createVulkan(void** nativeState,
                                          uint32_t* physicalDeviceCount,
                                          uint32_t* apiVersion)
{
    if (nativeState) *nativeState = NULL;
    if (physicalDeviceCount) *physicalDeviceCount = 0;
    if (apiVersion) *apiVersion = 0;
    return false;
}
void XPlatformGraphicsDriver_destroyVulkan(void* nativeState) { (void)nativeState; }
bool XPlatformGraphicsDriver_createOffscreen(uint32_t width, uint32_t height,
                                             void** nativeState)
{ (void)width; (void)height; if (nativeState) *nativeState = NULL; return false; }
void XPlatformGraphicsDriver_destroyOffscreen(void* nativeState) { (void)nativeState; }
bool XPlatformGraphicsDriver_makeCurrentOffscreen(void* nativeState)
{ (void)nativeState; return false; }
void XPlatformGraphicsDriver_doneCurrentOffscreen(void* nativeState) { (void)nativeState; }

#endif
