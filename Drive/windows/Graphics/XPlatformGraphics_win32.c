/****************************************************************************
 * @file       XPlatformGraphics_win32.c
 * @brief      Windows WGL 与 Vulkan 平台图形后端。
 ****************************************************************************/
#include "XPlatformGraphics.h"

#if XPLATFORMINTEGRATION_ON && defined(_WIN32)

#include "XWindow.h"
#include "XPlatformNativeWindow.h"
#include "XMemory.h"
#include <windows.h>
#include <GL/gl.h>
#include <stdint.h>
#include <string.h>

typedef struct XWin32OpenGLState
{
    HWND m_window;
    HDC m_dc;
    HGLRC m_context;
} XWin32OpenGLState;

#if defined(XINYUE_C_HAS_VULKAN)
#include <vulkan/vulkan.h>

typedef struct XWin32VulkanState
{
    VkInstance m_instance;
} XWin32VulkanState;
#endif /* XINYUE_C_HAS_VULKAN */

typedef struct XWin32OffscreenState
{
    HWND m_window;
    HDC m_dc;
    HGLRC m_context;
} XWin32OffscreenState;

bool XPlatformGraphicsDriver_openGLAvailable(void)
{
    return GetModuleHandleW(L"opengl32.dll") != NULL ||
           LoadLibraryW(L"opengl32.dll") != NULL;
}

bool XPlatformGraphicsDriver_createOpenGL(XWindow* window, void** nativeState)
{
    XWin32OpenGLState* state;
    PIXELFORMATDESCRIPTOR descriptor;
    int format;
    if (nativeState) *nativeState = NULL;
    if (!window || !nativeState) return false;
    state = (XWin32OpenGLState*)XMalloc_System(sizeof(*state));
    if (!state) return false;
    memset(state, 0, sizeof(*state));
    state->m_window = (HWND)(uintptr_t)XWindow_winId(window);
    if (!state->m_window || !IsWindow(state->m_window)) goto failed;
    state->m_dc = GetDC(state->m_window);
    if (!state->m_dc) goto failed;
    memset(&descriptor, 0, sizeof(descriptor));
    descriptor.nSize = sizeof(descriptor);
    descriptor.nVersion = 1;
    descriptor.dwFlags = PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER;
    descriptor.iPixelType = PFD_TYPE_RGBA;
    descriptor.cColorBits = 32;
    descriptor.cDepthBits = 24;
    descriptor.cStencilBits = 8;
    descriptor.iLayerType = PFD_MAIN_PLANE;
    format = ChoosePixelFormat(state->m_dc, &descriptor);
    if (!format || !SetPixelFormat(state->m_dc, format, &descriptor)) goto failed;
    state->m_context = wglCreateContext(state->m_dc);
    if (!state->m_context) goto failed;
    *nativeState = state;
    return true;
failed:
    if (state->m_dc && state->m_window) ReleaseDC(state->m_window, state->m_dc);
    XFree_System(state);
    return false;
}

void XPlatformGraphicsDriver_destroyOpenGL(void* nativeState)
{
    XWin32OpenGLState* state = (XWin32OpenGLState*)nativeState;
    if (!state) return;
    if (wglGetCurrentContext() == state->m_context) (void)wglMakeCurrent(NULL, NULL);
    if (state->m_context) wglDeleteContext(state->m_context);
    if (state->m_dc && state->m_window) ReleaseDC(state->m_window, state->m_dc);
    XFree_System(state);
}

bool XPlatformGraphicsDriver_makeCurrentOpenGL(void* nativeState)
{
    XWin32OpenGLState* state = (XWin32OpenGLState*)nativeState;
    return state && state->m_dc && state->m_context &&
           wglMakeCurrent(state->m_dc, state->m_context);
}

void XPlatformGraphicsDriver_doneCurrentOpenGL(void* nativeState)
{
    (void)nativeState;
    (void)wglMakeCurrent(NULL, NULL);
}

bool XPlatformGraphicsDriver_swapBuffersOpenGL(void* nativeState)
{
    XWin32OpenGLState* state = (XWin32OpenGLState*)nativeState;
    return state && state->m_dc && SwapBuffers(state->m_dc);
}

void* XPlatformGraphicsDriver_openGLProcAddress(void* nativeState,
                                                 const char* name)
{
    PROC procedure;
    uintptr_t raw;
    (void)nativeState;
    if (!name || !*name) return NULL;
    procedure = wglGetProcAddress(name);
    /* WGL uses small sentinel values for an unsupported entry point.  Treat
       them as missing before the caller copies the address into a function
       pointer; invoking one would crash the automatic GPU fallback path. */
    raw = (uintptr_t)procedure;
    if (raw == 1u || raw == 2u || raw == 3u || raw == (uintptr_t)-1)
        procedure = NULL;
    if (!procedure) {
        HMODULE module = GetModuleHandleW(L"opengl32.dll");
        if (module) procedure = GetProcAddress(module, name);
    }
    return (void*)procedure;
}

#if defined(XINYUE_C_HAS_VULKAN)
#include <vulkan/vulkan.h>
#include <vulkan/vulkan_win32.h>

bool XPlatformGraphicsDriver_vulkanWindowSurfaceExtensions(
        const char* const** outNames, uint32_t* outCount)
{
    /* Win32 窗口表面：VK_KHR_surface 基础扩展 + 平台扩展。 */
    static const char* const win32SurfaceExtensions[] = {
        "VK_KHR_surface", "VK_KHR_win32_surface"
    };
    if (!outNames || !outCount) return false;
    *outNames = win32SurfaceExtensions;
    *outCount = (uint32_t)(sizeof(win32SurfaceExtensions) /
                           sizeof(win32SurfaceExtensions[0]));
    return true;
}

bool XPlatformGraphicsDriver_createVulkanWindowSurface(void* instance,
                                                       XWindow* window,
                                                       void** outSurface)
{
    VkWin32SurfaceCreateInfoKHR createInfo;
    VkSurfaceKHR surface = VK_NULL_HANDLE;
    XPlatformNativeWindowConnectionType connectionType;
    HINSTANCE instanceHandle;
    HWND windowHandle;
    if (!instance || !window || !outSurface) return false;
    *outSurface = NULL;
    instanceHandle =
        (HINSTANCE)XPlatformNativeWindow_nativeConnection(&connectionType);
    windowHandle = (HWND)(uintptr_t)XWindow_winId(window);
    if (connectionType != XPlatformNativeWindowConnection_Win32 ||
        !instanceHandle || !windowHandle || !IsWindow(windowHandle))
        return false;
    memset(&createInfo, 0, sizeof(createInfo));
    createInfo.sType = VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR;
    createInfo.hinstance = instanceHandle;
    createInfo.hwnd = windowHandle;
    if (vkCreateWin32SurfaceKHR((VkInstance)instance, &createInfo, NULL,
                                &surface) != VK_SUCCESS)
        return false;
    *outSurface = (void*)surface;
    return true;
}

void XPlatformGraphicsDriver_destroyVulkanWindowSurface(void* instance,
                                                        void* surface)
{
    if (!instance || !surface) return;
    vkDestroySurfaceKHR((VkInstance)instance, (VkSurfaceKHR)surface, NULL);
}

bool XPlatformGraphicsDriver_vulkanAvailable(void)
{
    return GetModuleHandleW(L"vulkan-1.dll") != NULL ||
           LoadLibraryW(L"vulkan-1.dll") != NULL;
}

bool XPlatformGraphicsDriver_createVulkan(void** nativeState,
                                          uint32_t* physicalDeviceCount,
                                          uint32_t* apiVersion)
{
    VkApplicationInfo applicationInfo;
    VkInstanceCreateInfo createInfo;
    XWin32VulkanState* state;
    uint32_t count = 0;
    uint32_t version = VK_API_VERSION_1_0;
    PFN_vkEnumerateInstanceVersion enumerateVersion;
    VkResult result;
    if (nativeState) *nativeState = NULL;
    if (physicalDeviceCount) *physicalDeviceCount = 0;
    if (apiVersion) *apiVersion = 0;
    if (!nativeState) return false;
    enumerateVersion = (PFN_vkEnumerateInstanceVersion)vkGetInstanceProcAddr(
        VK_NULL_HANDLE, "vkEnumerateInstanceVersion");
    if (enumerateVersion) (void)enumerateVersion(&version);
    memset(&applicationInfo, 0, sizeof(applicationInfo));
    applicationInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    applicationInfo.pApplicationName = "XinYueC";
    applicationInfo.applicationVersion = 1;
    applicationInfo.pEngineName = "XinYueC";
    applicationInfo.engineVersion = 1;
    applicationInfo.apiVersion = version;
    memset(&createInfo, 0, sizeof(createInfo));
    createInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    createInfo.pApplicationInfo = &applicationInfo;
    state = (XWin32VulkanState*)XMalloc_System(sizeof(*state));
    if (!state) return false;
    memset(state, 0, sizeof(*state));
    result = vkCreateInstance(&createInfo, NULL, &state->m_instance);
    if (result != VK_SUCCESS) {
        XFree_System(state);
        return false;
    }
    result = vkEnumeratePhysicalDevices(state->m_instance, &count, NULL);
    if (result != VK_SUCCESS && result != VK_INCOMPLETE) count = 0;
    *nativeState = state;
    if (physicalDeviceCount) *physicalDeviceCount = count;
    if (apiVersion) *apiVersion = version;
    return true;
}

void XPlatformGraphicsDriver_destroyVulkan(void* nativeState)
{
    XWin32VulkanState* state = (XWin32VulkanState*)nativeState;
    if (!state) return;
    if (state->m_instance) vkDestroyInstance(state->m_instance, NULL);
    XFree_System(state);
}
#else
bool XPlatformGraphicsDriver_vulkanAvailable(void) { return false; }
bool XPlatformGraphicsDriver_vulkanWindowSurfaceExtensions(
        const char* const** outNames, uint32_t* outCount)
{
    if (outNames) *outNames = NULL;
    if (outCount) *outCount = 0;
    return false;
}
bool XPlatformGraphicsDriver_createVulkanWindowSurface(void* instance,
                                                       XWindow* window,
                                                       void** outSurface)
{
    (void)instance; (void)window;
    if (outSurface) *outSurface = NULL;
    return false;
}
void XPlatformGraphicsDriver_destroyVulkanWindowSurface(void* instance,
                                                        void* surface)
{ (void)instance; (void)surface; }
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
#endif /* XINYUE_C_HAS_VULKAN */

bool XPlatformGraphicsDriver_createOffscreen(uint32_t width, uint32_t height,
                                             void** nativeState)
{
    XWin32OffscreenState* state;
    PIXELFORMATDESCRIPTOR descriptor;
    int format;
    int w;
    int h;
    if (nativeState) *nativeState = NULL;
    if (!nativeState) return false;
    /* drawable（窗口）必须与会话渲染尺寸一致：glReadPixels 越界读会破坏
       驱动内部缓冲；0 尺寸按 1x1 兜底。 */
    w = width > 0 ? (int)width : 1;
    h = height > 0 ? (int)height : 1;
    state = (XWin32OffscreenState*)XMalloc_System(sizeof(*state));
    if (!state) return false;
    memset(state, 0, sizeof(*state));
    state->m_window = CreateWindowExW(0, L"STATIC", L"XinYueC Offscreen",
                                      WS_POPUP, 0, 0, w, h, NULL, NULL,
                                      GetModuleHandleW(NULL), NULL);
    if (!state->m_window) goto failed;
    state->m_dc = GetDC(state->m_window);
    if (!state->m_dc) goto failed;
    memset(&descriptor, 0, sizeof(descriptor));
    descriptor.nSize = sizeof(descriptor);
    descriptor.nVersion = 1;
    descriptor.dwFlags = PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER;
    descriptor.iPixelType = PFD_TYPE_RGBA;
    descriptor.cColorBits = 32;
    descriptor.cDepthBits = 24;
    descriptor.cStencilBits = 8;
    descriptor.iLayerType = PFD_MAIN_PLANE;
    format = ChoosePixelFormat(state->m_dc, &descriptor);
    if (!format || !SetPixelFormat(state->m_dc, format, &descriptor)) goto failed;
    state->m_context = wglCreateContext(state->m_dc);
    if (!state->m_context) goto failed;
    *nativeState = state;
    return true;
failed:
    if (state->m_context) wglDeleteContext(state->m_context);
    if (state->m_dc && state->m_window) ReleaseDC(state->m_window, state->m_dc);
    if (state->m_window) DestroyWindow(state->m_window);
    XFree_System(state);
    return false;
}

void XPlatformGraphicsDriver_destroyOffscreen(void* nativeState)
{
    XWin32OffscreenState* state = (XWin32OffscreenState*)nativeState;
    if (!state) return;
    if (wglGetCurrentContext() == state->m_context) (void)wglMakeCurrent(NULL, NULL);
    if (state->m_context) wglDeleteContext(state->m_context);
    if (state->m_dc && state->m_window) ReleaseDC(state->m_window, state->m_dc);
    if (state->m_window) DestroyWindow(state->m_window);
    XFree_System(state);
}

bool XPlatformGraphicsDriver_makeCurrentOffscreen(void* nativeState)
{
    XWin32OffscreenState* state = (XWin32OffscreenState*)nativeState;
    return state && state->m_dc && state->m_context &&
           wglMakeCurrent(state->m_dc, state->m_context);
}

void XPlatformGraphicsDriver_doneCurrentOffscreen(void* nativeState)
{
    (void)nativeState;
    (void)wglMakeCurrent(NULL, NULL);
}

#endif /* XPLATFORMINTEGRATION_ON && _WIN32 */
