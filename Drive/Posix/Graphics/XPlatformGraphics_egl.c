/****************************************************************************
 * @file       XPlatformGraphics_egl.c
 * @brief      Linux EGL/GLES 平台图形后端（X11 窗口直通 + pbuffer 离屏）。
 * @details    与 GLX 后端（XPlatformGraphics_posix.c）等价并列，由
 *             XPlatformGraphics_posix.c 的分派层按运行期开关选择。
 *
 *             存在理由（RK3568 实测）：厂商 BSP 只提供 EGL/GLES 用户态库
 *             （Mali-G52），不提供 GLX DRI 驱动 ⇒ Xorg 的 AIGLX 初始化失败，
 *             客户端 glXCreateContext 落到 llvmpipe 软件光栅；而
 *             eglGetDisplay(X11) + eglCreateWindowSurface(X11 Window)
 *             可直接拿到厂商 Mali 上下文（实测 GL_RENDERER=Mali-G52，
 *             eglSwapBuffers 后从 X server 读回的窗口像素为正确渐变）。
 *             ⇒ EGL 是这块板子上唯一能真正用上 GPU 的路。
 *
 *             【EGL 与 GLX 的坐标系】EGL 的窗口 surface 原点在左下，
 *             与 GLX/GL 相同；框架绘制坐标按设备坐标（左上原点）给出，
 *             由 XGpuRenderDriver_gl.c 的投影矩阵统一处理，本文件不参与。
 *
 *             【GLES 与桌面 GL 的差异】本后端建的是 GLES2 上下文。
 *             XGpuRenderDriver_gl.c 的纹理上传快速路径使用桌面 GL 专有的
 *             GL_BGRA + GL_UNSIGNED_INT_8_8_8_8_REV 组合（GLES 非法），
 *             该路径在 GLES 上下文中须走 RGBA + UNSIGNED_BYTE 慢路径
 *             （见驱动的 isGles 分支）。
 ****************************************************************************/
#include "XPlatformGraphics.h"

#if XPLATFORMINTEGRATION_ON && defined(__linux__) && \
    defined(XINYUE_C_HAS_OPENGL) && defined(XINYUE_C_HAS_X11) && \
    defined(XINYUE_C_HAS_EGL)

#include "XPlatformNativeWindow.h"
#include "XPlatformGraphicsBackend.h"
#include "XWindow.h"
#include "XMemory.h"
#include <string.h>

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
#undef XImage
#undef XPoint
#undef XEvent
#undef XColor
#undef XKeyEvent
#undef XExposeEvent
#define XFree XMemory_free

#include <EGL/egl.h>
#include <EGL/eglext.h>

/** @brief EGL 窗口会话状态（首字段与 GLX 后端同构，供分派层识别归属）。 */
typedef struct XPosixEglState
{
    XPlatformGlStateHead m_head; /**< 公共首部（display + backend tag）。 */
    Window      m_drawable;      /**< X11 窗口句柄（EGL window surface 目标）。 */
    EGLDisplay  m_eglDisplay;    /**< EGL display。 */
    EGLContext  m_context;       /**< EGL 上下文（GLES2）。 */
    EGLSurface  m_surface;       /**< EGL window surface。 */
    bool        m_ownsDisplay;   /**< true=本后端创建并拥有 EGL display。 */
} XPosixEglState;

/** @brief EGL pbuffer 离屏会话状态。 */
typedef struct XPosixEglOffscreenState
{
    XPlatformGlStateHead m_head;
    EGLDisplay  m_eglDisplay;
    EGLContext  m_context;
    EGLSurface  m_surface;       /**< pbuffer surface。 */
    int         m_width;
    int         m_height;
    bool        m_ownsDisplay;
} XPosixEglOffscreenState;

/**
 * @brief 取 X11 display（平台层唯一入口）。
 */
static Display* xpgraphics_egl_xdisplay(void)
{
    XPlatformNativeWindowConnectionType type;
    return (Display*)XPlatformNativeWindow_nativeConnection(&type);
}

/**
 * @brief 取得（或创建）EGL display。
 * @details 优先 eglGetDisplay(X11 display) 复用现有 X 连接（EGL/GLX 共存
 *          时这是唯一能让 EGL 拿到厂商驱动的入口）；失败则回退
 *          eglGetDisplay(EGL_DEFAULT_DISPLAY)。
 */
static bool xpgraphics_egl_acquire(Display* xdpy, EGLDisplay* outDisplay,
                                    bool* outOwns)
{
    EGLDisplay edpy;
    *outOwns = false;
    if (xdpy) {
        edpy = eglGetDisplay((EGLNativeDisplayType)xdpy);
        if (edpy != EGL_NO_DISPLAY) {
            EGLint major = 0;
            EGLint minor = 0;
            if (eglInitialize(edpy, &major, &minor)) {
                *outDisplay = edpy;
                return true;
            }
        }
    }
    edpy = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    if (edpy == EGL_NO_DISPLAY) return false;
    {
        EGLint major = 0;
        EGLint minor = 0;
        if (!eglInitialize(edpy, &major, &minor)) return false;
    }
    *outDisplay = edpy;
    *outOwns = true;
    return true;
}

/**
 * @brief 选一个支持 GLES 的 EGL config。
 * @details 同时申报 ES2/ES3 可渲染类型（EGL 规范要求按上下文版本选
 *          config；两者都申报可让 ES3 上下文总能拿到 config，ES2
 *          上下文也可用同一份）。
 */
static bool xpgraphics_egl_chooseConfig(EGLDisplay edpy, EGLint surfaceType,
                                         EGLConfig* outConfig)
{
    EGLint attr[] = {
        EGL_SURFACE_TYPE, surfaceType,
        EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT | EGL_OPENGL_ES3_BIT,
        EGL_RED_SIZE, 8,
        EGL_GREEN_SIZE, 8,
        EGL_BLUE_SIZE, 8,
        EGL_ALPHA_SIZE, 8,
        EGL_NONE
    };
    EGLConfig config = 0;
    EGLint count = 0;
    if (!eglChooseConfig(edpy, attr, &config, 1, &count)) return false;
    if (count < 1) return false;
    *outConfig = config;
    return true;
}

/**
 * @brief 创建 GLES 上下文。
 * @details 请求 ES3（EGL_CONTEXT_CLIENT_VERSION=3）：驱动用到
 *          glBlitFramebuffer / glMapBuffer / glFenceSync 等 ES3 能力
 *          （ES2 缺失，驱动会走慢路径或退化）。设备不支持 ES3 时
 *          回退 ES2（驱动按别名探测降级）。
 */
static EGLContext xpgraphics_egl_createContext(EGLDisplay edpy,
                                                EGLConfig config)
{
    EGLContext ctx;
    EGLint attr3[] = { EGL_CONTEXT_CLIENT_VERSION, 3, EGL_NONE };
    EGLint attr2[] = { EGL_CONTEXT_CLIENT_VERSION, 2, EGL_NONE };
    ctx = eglCreateContext(edpy, config, EGL_NO_CONTEXT, attr3);
    if (ctx != EGL_NO_CONTEXT) return ctx;
    return eglCreateContext(edpy, config, EGL_NO_CONTEXT, attr2);
}

/* ==================== 窗口会话 ==================== */

bool xpgraphics_egl_openGLAvailable(void)
{
    Display* xdpy = xpgraphics_egl_xdisplay();
    EGLDisplay edpy = EGL_NO_DISPLAY;
    bool owns = false;
    if (!xdpy) return false;
    if (!xpgraphics_egl_acquire(xdpy, &edpy, &owns)) return false;
    if (owns) eglTerminate(edpy);
    return true;
}

bool xpgraphics_egl_createOpenGL(XWindow* window, void** nativeState)
{
    XPosixEglState* state;
    Display* xdpy;
    Window drawable;
    EGLConfig config = 0;
    EGLDisplay edpy = EGL_NO_DISPLAY;
    bool ownsDisplay = false;
    if (nativeState) *nativeState = NULL;
    if (!window || !nativeState) return false;
    xdpy = xpgraphics_egl_xdisplay();
    drawable = (Window)XWindow_winId(window);
    if (!xdpy || !drawable) return false;
    if (!xpgraphics_egl_acquire(xdpy, &edpy, &ownsDisplay)) return false;
    if (!xpgraphics_egl_chooseConfig(edpy, EGL_WINDOW_BIT, &config)) {
        if (ownsDisplay) eglTerminate(edpy);
        return false;
    }
    state = (XPosixEglState*)XMalloc_System(sizeof(*state));
    if (!state) {
        if (ownsDisplay) eglTerminate(edpy);
        return false;
    }
    memset(state, 0, sizeof(*state));
    state->m_head.m_display = xdpy;
    state->m_head.m_backendTag = XPLATFORM_GL_BACKEND_TAG_EGL;
    state->m_drawable = drawable;
    state->m_eglDisplay = edpy;
    state->m_ownsDisplay = ownsDisplay;
    state->m_surface = eglCreateWindowSurface(edpy, config,
                                               (EGLNativeWindowType)drawable,
                                               NULL);
    if (state->m_surface == EGL_NO_SURFACE) {
        if (ownsDisplay) eglTerminate(edpy);
        XFree_System(state);
        return false;
    }
    state->m_context = xpgraphics_egl_createContext(edpy, config);
    if (state->m_context == EGL_NO_CONTEXT) {
        eglDestroySurface(edpy, state->m_surface);
        if (ownsDisplay) eglTerminate(edpy);
        XFree_System(state);
        return false;
    }
    *nativeState = state;
    return true;
}

void xpgraphics_egl_destroyOpenGL(void* nativeState)
{
    XPosixEglState* state = (XPosixEglState*)nativeState;
    if (!state) return;
    if (state->m_eglDisplay != EGL_NO_DISPLAY) {
        eglMakeCurrent(state->m_eglDisplay, EGL_NO_SURFACE, EGL_NO_SURFACE,
                        EGL_NO_CONTEXT);
        if (state->m_context != EGL_NO_CONTEXT)
            eglDestroyContext(state->m_eglDisplay, state->m_context);
        if (state->m_surface != EGL_NO_SURFACE)
            eglDestroySurface(state->m_eglDisplay, state->m_surface);
        /* EGL display 可能与 GLX 共享（由 X11 display 派生），只有自建才 terminate。 */
        if (state->m_ownsDisplay) eglTerminate(state->m_eglDisplay);
    }
    XFree_System(state);
}

bool xpgraphics_egl_makeCurrentOpenGL(void* nativeState)
{
    XPosixEglState* state = (XPosixEglState*)nativeState;
    if (!state || state->m_eglDisplay == EGL_NO_DISPLAY) return false;
    return eglMakeCurrent(state->m_eglDisplay, state->m_surface,
                          state->m_surface, state->m_context) == EGL_TRUE;
}

void xpgraphics_egl_doneCurrentOpenGL(void* nativeState)
{
    XPosixEglState* state = (XPosixEglState*)nativeState;
    if (!state || state->m_eglDisplay == EGL_NO_DISPLAY) return;
    (void)eglMakeCurrent(state->m_eglDisplay, EGL_NO_SURFACE, EGL_NO_SURFACE,
                         EGL_NO_CONTEXT);
}

bool xpgraphics_egl_swapBuffersOpenGL(void* nativeState)
{
    XPosixEglState* state = (XPosixEglState*)nativeState;
    if (!state || state->m_eglDisplay == EGL_NO_DISPLAY) return false;
    if (!eglSwapBuffers(state->m_eglDisplay, state->m_surface)) return false;
    if (state->m_head.m_display) XFlush(state->m_head.m_display);
    return true;
}

void* xpgraphics_egl_openGLProcAddress(void* nativeState, const char* name)
{
    XPosixEglState* state = (XPosixEglState*)nativeState;
    if (!name) return NULL;
    if (state && state->m_eglDisplay != EGL_NO_DISPLAY) {
        /* eglGetProcAddress 覆盖绝大多数 GLES2/GLES3 入口（Mali 实测
           glCreateShader/glGenFramebuffers/glBlitFramebuffer 等均可解析）。
           个别 ES3 专有入口（如 glMapBuffer）返回 NULL——那些在驱动侧
           本身是可选能力（有别名回退/缺失即降级），故此处直接返回 NULL，
           不引入 dlsym（避免给嵌入式构建增加 libdl 依赖）。 */
        return (void*)eglGetProcAddress(name);
    }
    return NULL;
}

/* ==================== 离屏会话（pbuffer） ==================== */

bool xpgraphics_egl_createOffscreen(uint32_t width, uint32_t height,
                                    void** nativeState)
{
    XPosixEglOffscreenState* state;
    Display* xdpy;
    EGLDisplay edpy = EGL_NO_DISPLAY;
    bool ownsDisplay = false;
    EGLConfig config = 0;
    EGLint pbufferAttr[] = {
        EGL_WIDTH, (EGLint)width,
        EGL_HEIGHT, (EGLint)height,
        EGL_NONE
    };
    if (nativeState) *nativeState = NULL;
    if (!nativeState || width <= 0 || height <= 0) return false;
    xdpy = xpgraphics_egl_xdisplay();
    if (!xpgraphics_egl_acquire(xdpy, &edpy, &ownsDisplay)) return false;
    if (!xpgraphics_egl_chooseConfig(edpy, EGL_PBUFFER_BIT, &config)) {
        if (ownsDisplay) eglTerminate(edpy);
        return false;
    }
    state = (XPosixEglOffscreenState*)XMalloc_System(sizeof(*state));
    if (!state) {
        if (ownsDisplay) eglTerminate(edpy);
        return false;
    }
    memset(state, 0, sizeof(*state));
    state->m_head.m_display = xdpy;
    state->m_head.m_backendTag = XPLATFORM_GL_BACKEND_TAG_EGL;
    state->m_eglDisplay = edpy;
    state->m_ownsDisplay = ownsDisplay;
    state->m_width = (int)width;
    state->m_height = (int)height;
    state->m_surface = eglCreatePbufferSurface(edpy, config, pbufferAttr);
    if (state->m_surface == EGL_NO_SURFACE) {
        if (ownsDisplay) eglTerminate(edpy);
        XFree_System(state);
        return false;
    }
    state->m_context = xpgraphics_egl_createContext(edpy, config);
    if (state->m_context == EGL_NO_CONTEXT) {
        eglDestroySurface(edpy, state->m_surface);
        if (ownsDisplay) eglTerminate(edpy);
        XFree_System(state);
        return false;
    }
    *nativeState = state;
    return true;
}

void xpgraphics_egl_destroyOffscreen(void* nativeState)
{
    XPosixEglOffscreenState* state = (XPosixEglOffscreenState*)nativeState;
    if (!state) return;
    if (state->m_eglDisplay != EGL_NO_DISPLAY) {
        eglMakeCurrent(state->m_eglDisplay, EGL_NO_SURFACE, EGL_NO_SURFACE,
                        EGL_NO_CONTEXT);
        if (state->m_context != EGL_NO_CONTEXT)
            eglDestroyContext(state->m_eglDisplay, state->m_context);
        if (state->m_surface != EGL_NO_SURFACE)
            eglDestroySurface(state->m_eglDisplay, state->m_surface);
        if (state->m_ownsDisplay) eglTerminate(state->m_eglDisplay);
    }
    XFree_System(state);
}

bool xpgraphics_egl_makeCurrentOffscreen(void* nativeState)
{
    XPosixEglOffscreenState* state = (XPosixEglOffscreenState*)nativeState;
    if (!state || state->m_eglDisplay == EGL_NO_DISPLAY) return false;
    return eglMakeCurrent(state->m_eglDisplay, state->m_surface,
                          state->m_surface, state->m_context) == EGL_TRUE;
}

void xpgraphics_egl_doneCurrentOffscreen(void* nativeState)
{
    XPosixEglOffscreenState* state = (XPosixEglOffscreenState*)nativeState;
    if (!state || state->m_eglDisplay == EGL_NO_DISPLAY) return;
    (void)eglMakeCurrent(state->m_eglDisplay, EGL_NO_SURFACE, EGL_NO_SURFACE,
                         EGL_NO_CONTEXT);
}

#endif /* XPLATFORMINTEGRATION_ON && XINYUE_C_HAS_OPENGL && XINYUE_C_HAS_X11 && XINYUE_C_HAS_EGL */
