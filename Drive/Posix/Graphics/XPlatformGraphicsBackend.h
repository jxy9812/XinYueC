/****************************************************************************
 * @file       XPlatformGraphicsBackend.h
 * @brief      POSIX 图形后端内部契约（GLX / EGL 双后端分派）。
 * @details    两个后端各自实现同一组入口（xpgraphics_glx_* / xpgraphics_egl_*），
 *             由本文件声明；公共导出名（XPlatformGraphicsDriver_*）由
 *             XPlatformGraphics_posix.c 的分派层按运行期开关选择。
 *
 *             【为什么需要 EGL 后端】RK3568 实测：厂商 BSP 只提供
 *             EGL/GLES 用户态库（Mali-G52），**不提供 GLX DRI 驱动** ⇒
 *             Xorg 的 AIGLX 初始化失败，客户端 glXCreateContext 落到
 *             llvmpipe 软件光栅；而 eglGetDisplay(X11) +
 *             eglCreateWindowSurface(X11 Window) 可直接拿到厂商 Mali
 *             上下文（实测 GL_RENDERER=Mali-G52）。故 POSIX 侧需要
 *             EGL 后端作为 GLX 的等价替代。
 *
 *             【nativeState 不透明性】各后端的原生状态首字段统一为
 *             XPlatformGlStateHead（含 backend tag），分派层据此在
 *             destroy/makeCurrent 等回调里知道该交给哪个后端处理，
 *             而无需在公共层感知具体图形 API。
 *
 *             本文件仅在 Drive 内部使用，不属于公共 API。
 ****************************************************************************/
#ifndef XPLATFORMGRAPHICSBACKEND_H
#define XPLATFORMGRAPHICSBACKEND_H

/* 【不 include X11/Xlib.h】本头会被 XPlatformGraphics_posix.c 在「Xlib
   类型改名宏」生效期间包含；此处直接引入 Xlib 会让 XImage/XPoint/XEvent/
   XColor/XKeyEvent/XExposeEvent 与框架公共类型撞名（conflicting types）。
   故只用前向声明的 Display*（Xlib 的 Display 是不透明指针，前向声明足够）。 */
typedef struct _XDisplay Display;

/** @brief 框架窗口类型（前向声明，避免本头引入 XWindow.h）。 */
typedef struct XWindow XWindow;

#include <stdbool.h>
#include <stdint.h>

/** @brief GLX 后端标记（'G''L''X'）。 */
#define XPLATFORM_GL_BACKEND_TAG_GLX 0x474C5801u
/** @brief EGL 后端标记（'E''G''L'）。 */
#define XPLATFORM_GL_BACKEND_TAG_EGL 0x45474C01u

/**
 * @brief 原生图形状态的公共首部（各后端状态结构体的第一个成员）。
 * @details 分派层据此识别状态归属；m_display 复用 X11 display，
 *          由窗口后端持有（离屏后端同样需要 display 建 EGL/GLX 会话）。
 */
typedef struct XPlatformGlStateHead
{
    Display* m_display; /**< X11 display。 */
    uint32_t m_backendTag; /**< XPLATFORM_GL_BACKEND_TAG_* 之一。 */
} XPlatformGlStateHead;

/**
 * @brief 当前是否选用 EGL 后端。
 * @details 运行期开关 XGUI_GL_BACKEND=egl|glx 显式指定；未指定时
 *          EGL 优先（能拿到厂商 GPU 驱动），EGL 不可用则回落 GLX。
 */
bool xpgraphics_useEgl(void);

/* ==================== GLX 后端入口（XPlatformGraphics_posix.c） ==================== */

bool xpgraphics_glx_openGLAvailable(void);
bool xpgraphics_glx_createOpenGL(XWindow* window, void** nativeState);
void xpgraphics_glx_destroyOpenGL(void* nativeState);
bool xpgraphics_glx_makeCurrentOpenGL(void* nativeState);
void xpgraphics_glx_doneCurrentOpenGL(void* nativeState);
bool xpgraphics_glx_swapBuffersOpenGL(void* nativeState);
void* xpgraphics_glx_openGLProcAddress(void* nativeState, const char* name);
bool xpgraphics_glx_createOffscreen(uint32_t width, uint32_t height,
                                   void** nativeState);
void xpgraphics_glx_destroyOffscreen(void* nativeState);
bool xpgraphics_glx_makeCurrentOffscreen(void* nativeState);
void xpgraphics_glx_doneCurrentOffscreen(void* nativeState);

/* ==================== EGL 后端入口（XPlatformGraphics_egl.c） ==================== */

bool xpgraphics_egl_openGLAvailable(void);
bool xpgraphics_egl_createOpenGL(XWindow* window, void** nativeState);
void xpgraphics_egl_destroyOpenGL(void* nativeState);
bool xpgraphics_egl_makeCurrentOpenGL(void* nativeState);
void xpgraphics_egl_doneCurrentOpenGL(void* nativeState);
bool xpgraphics_egl_swapBuffersOpenGL(void* nativeState);
void* xpgraphics_egl_openGLProcAddress(void* nativeState, const char* name);
bool xpgraphics_egl_createOffscreen(uint32_t width, uint32_t height,
                                   void** nativeState);
void xpgraphics_egl_destroyOffscreen(void* nativeState);
bool xpgraphics_egl_makeCurrentOffscreen(void* nativeState);
void xpgraphics_egl_doneCurrentOffscreen(void* nativeState);

#endif /* XPLATFORMGRAPHICSBACKEND_H */
