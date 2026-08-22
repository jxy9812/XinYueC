/****************************************************************************
 * @file       XPlatformGraphics_posix.c
 * @brief      Linux GLX 与 Vulkan 平台图形后端。
 * @details    所有系统图形 API 仅位于本文件。公共 Src 层只通过
 *             XPlatformGraphicsDriver_* 的不透明状态调用这里的实现。
 ****************************************************************************/
#include "XPlatformGraphics.h"

#if XPLATFORMINTEGRATION_ON && defined(__linux__) && \
    (defined(XINYUE_C_HAS_OPENGL) || defined(XINYUE_C_HAS_VULKAN))

#include "XPlatformNativeWindow.h"
#include "XWindow.h"
#include "XMemory.h"
#include <string.h>

#if defined(XINYUE_C_HAS_OPENGL) && defined(XINYUE_C_HAS_X11)
/* 与 XGui 公共类型重名的 Xlib 声明仅在 Drive 内做局部改名。 */
#undef XFree
#define XImage X11_XImage
#define XPoint X11_XPoint
#define XEvent X11_XEvent
#define XColor X11_XColor
#define XKeyEvent X11_XKeyEvent
#define XExposeEvent X11_XExposeEvent
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <GL/glx.h>
#undef XImage
#undef XPoint
#undef XEvent
#undef XColor
#undef XKeyEvent
#undef XExposeEvent
#define XFree XMemory_free

typedef struct XPosixOpenGLState
{
    Display* m_display;
    GLXContext m_context;
    GLXDrawable m_drawable;
} XPosixOpenGLState;

typedef struct XPosixOffscreenState
{
    Display* m_display;
    GLXPbuffer m_pbuffer;
    GLXContext m_context;
} XPosixOffscreenState;

static void xpgraphics_xFree(void* pointer)
{
    if (!pointer) return;
#undef XFree
    XFree(pointer);
#define XFree XMemory_free
}

bool XPlatformGraphicsDriver_openGLAvailable(void)
{
    XPlatformNativeWindowConnectionType type;
    Display* display;
    int major = 0;
    int minor = 0;
    display = (Display*)XPlatformNativeWindow_nativeConnection(&type);
    return display && type == XPlatformNativeWindowConnection_X11 &&
           glXQueryVersion(display, &major, &minor) &&
           (major > 1 || (major == 1 && minor >= 2));
}

bool XPlatformGraphicsDriver_createOpenGL(XWindow* window, void** nativeState)
{
    XPlatformNativeWindowConnectionType type;
    XPosixOpenGLState* state;
    Display* display;
    Window drawable;
    XWindowAttributes attributes;
    XVisualInfo templateInfo;
    XVisualInfo* visualInfo;
    int count = 0;
    if (nativeState) *nativeState = NULL;
    if (!window || !nativeState) return false;
    display = (Display*)XPlatformNativeWindow_nativeConnection(&type);
    drawable = (Window)XWindow_winId(window);
    if (!display || type != XPlatformNativeWindowConnection_X11 || !drawable)
        return false;
    if (!XGetWindowAttributes(display, drawable, &attributes)) return false;
    memset(&templateInfo, 0, sizeof(templateInfo));
    templateInfo.visualid = XVisualIDFromVisual(attributes.visual);
    visualInfo = XGetVisualInfo(display, VisualIDMask, &templateInfo, &count);
    if (!visualInfo || count < 1) {
        xpgraphics_xFree(visualInfo);
        return false;
    }
    state = (XPosixOpenGLState*)XMalloc_System(sizeof(*state));
    if (!state) {
        xpgraphics_xFree(visualInfo);
        return false;
    }
    memset(state, 0, sizeof(*state));
    state->m_display = display;
    state->m_drawable = drawable;
    state->m_context = glXCreateContext(display, visualInfo, NULL, True);
    xpgraphics_xFree(visualInfo);
    if (!state->m_context) {
        XFree_System(state);
        return false;
    }
    *nativeState = state;
    return true;
}

void XPlatformGraphicsDriver_destroyOpenGL(void* nativeState)
{
    XPosixOpenGLState* state = (XPosixOpenGLState*)nativeState;
    if (!state) return;
    if (state->m_display && state->m_context)
        glXDestroyContext(state->m_display, state->m_context);
    XFree_System(state);
}

bool XPlatformGraphicsDriver_makeCurrentOpenGL(void* nativeState)
{
    XPosixOpenGLState* state = (XPosixOpenGLState*)nativeState;
    return state && state->m_display && state->m_context && state->m_drawable &&
           glXMakeCurrent(state->m_display, state->m_drawable, state->m_context);
}

void XPlatformGraphicsDriver_doneCurrentOpenGL(void* nativeState)
{
    XPosixOpenGLState* state = (XPosixOpenGLState*)nativeState;
    if (state && state->m_display)
        (void)glXMakeCurrent(state->m_display, None, NULL);
}

bool XPlatformGraphicsDriver_swapBuffersOpenGL(void* nativeState)
{
    XPosixOpenGLState* state = (XPosixOpenGLState*)nativeState;
    if (!state || !state->m_display || !state->m_drawable) return false;
    glXSwapBuffers(state->m_display, state->m_drawable);
    XFlush(state->m_display);
    return true;
}

void* XPlatformGraphicsDriver_openGLProcAddress(void* nativeState,
                                                 const char* name)
{
    (void)nativeState;
    return name ? (void*)glXGetProcAddressARB((const GLubyte*)name) : NULL;
}
#else
bool XPlatformGraphicsDriver_openGLAvailable(void) { return false; }
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
#endif

#if defined(XINYUE_C_HAS_OPENGL) && defined(XINYUE_C_HAS_X11)
bool XPlatformGraphicsDriver_createOffscreen(uint32_t width, uint32_t height,
                                             void** nativeState)
{
    XPlatformNativeWindowConnectionType type;
    Display* display;
    GLXFBConfig* configs;
    GLXFBConfig config = NULL;
    GLXContext context = NULL;
    GLXPbuffer pbuffer = 0;
    XPosixOffscreenState* state;
    int count = 0;
    int attributes[] = { GLX_DRAWABLE_TYPE, GLX_PBUFFER_BIT,
                         GLX_RENDER_TYPE, GLX_RGBA_BIT,
                         GLX_RED_SIZE, 8, GLX_GREEN_SIZE, 8,
                         GLX_BLUE_SIZE, 8, GLX_ALPHA_SIZE, 8,
                         None };
    int pbufferAttributes[] = { GLX_PBUFFER_WIDTH, (int)(width ? width : 1),
                                GLX_PBUFFER_HEIGHT, (int)(height ? height : 1),
                                None };
    if (nativeState) *nativeState = NULL;
    display = (Display*)XPlatformNativeWindow_nativeConnection(&type);
    if (!nativeState || !display || type != XPlatformNativeWindowConnection_X11)
        return false;
    configs = glXChooseFBConfig(display, DefaultScreen(display), attributes,
                                &count);
    if (!configs || count < 1) {
        if (configs) xpgraphics_xFree(configs);
        return false;
    }
    config = configs[0];
    pbuffer = glXCreatePbuffer(display, config, pbufferAttributes);
    if (pbuffer) {
        context = glXCreateNewContext(display, config, GLX_RGBA_TYPE, NULL, True);
    }
    xpgraphics_xFree(configs);
    if (!pbuffer || !context) {
        if (context) glXDestroyContext(display, context);
        if (pbuffer) glXDestroyPbuffer(display, pbuffer);
        return false;
    }
    state = (XPosixOffscreenState*)XMalloc_System(sizeof(*state));
    if (!state) {
        glXDestroyContext(display, context);
        glXDestroyPbuffer(display, pbuffer);
        return false;
    }
    memset(state, 0, sizeof(*state));
    state->m_display = display;
    state->m_pbuffer = pbuffer;
    state->m_context = context;
    *nativeState = state;
    return true;
}

void XPlatformGraphicsDriver_destroyOffscreen(void* nativeState)
{
    XPosixOffscreenState* state = (XPosixOffscreenState*)nativeState;
    if (!state) return;
    if (state->m_display && state->m_context) {
        if (glXGetCurrentContext() == state->m_context)
            (void)glXMakeCurrent(state->m_display, None, NULL);
        glXDestroyContext(state->m_display, state->m_context);
    }
    if (state->m_display && state->m_pbuffer)
        glXDestroyPbuffer(state->m_display, state->m_pbuffer);
    XFree_System(state);
}

bool XPlatformGraphicsDriver_makeCurrentOffscreen(void* nativeState)
{
    XPosixOffscreenState* state = (XPosixOffscreenState*)nativeState;
    return state && state->m_display && state->m_pbuffer && state->m_context &&
           glXMakeCurrent(state->m_display, state->m_pbuffer, state->m_context);
}

void XPlatformGraphicsDriver_doneCurrentOffscreen(void* nativeState)
{
    XPosixOffscreenState* state = (XPosixOffscreenState*)nativeState;
    if (state && state->m_display)
        (void)glXMakeCurrent(state->m_display, None, NULL);
}
#else
bool XPlatformGraphicsDriver_createOffscreen(uint32_t width, uint32_t height,
                                             void** nativeState)
{ (void)width; (void)height; if (nativeState) *nativeState = NULL; return false; }
void XPlatformGraphicsDriver_destroyOffscreen(void* nativeState) { (void)nativeState; }
bool XPlatformGraphicsDriver_makeCurrentOffscreen(void* nativeState)
{ (void)nativeState; return false; }
void XPlatformGraphicsDriver_doneCurrentOffscreen(void* nativeState) { (void)nativeState; }
#endif

#if defined(XINYUE_C_HAS_VULKAN)
#include <vulkan/vulkan.h>

typedef struct XPosixVulkanState
{
    VkInstance m_instance;
} XPosixVulkanState;

bool XPlatformGraphicsDriver_vulkanAvailable(void)
{
    uint32_t version = VK_API_VERSION_1_0;
    PFN_vkEnumerateInstanceVersion enumerateVersion =
        (PFN_vkEnumerateInstanceVersion)vkGetInstanceProcAddr(
            VK_NULL_HANDLE, "vkEnumerateInstanceVersion");
    if (enumerateVersion) (void)enumerateVersion(&version);
    return version >= VK_API_VERSION_1_0;
}

bool XPlatformGraphicsDriver_createVulkan(void** nativeState,
                                          uint32_t* physicalDeviceCount,
                                          uint32_t* apiVersion)
{
    VkApplicationInfo applicationInfo;
    VkInstanceCreateInfo createInfo;
    XPosixVulkanState* state;
    VkResult result;
    uint32_t count = 0;
    uint32_t version = VK_API_VERSION_1_0;
    PFN_vkEnumerateInstanceVersion enumerateVersion;
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
    state = (XPosixVulkanState*)XMalloc_System(sizeof(*state));
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
    XPosixVulkanState* state = (XPosixVulkanState*)nativeState;
    if (!state) return;
    if (state->m_instance) vkDestroyInstance(state->m_instance, NULL);
    XFree_System(state);
}
#else
bool XPlatformGraphicsDriver_vulkanAvailable(void) { return false; }
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
#endif

#endif /* XPLATFORMINTEGRATION_ON && Linux graphics */
