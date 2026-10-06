/******************************************************************************
 * @file       XPlatformNativeWindow_unsupported.c
 * @brief      未提供平台原生窗口后端的平台存根（XWindow/…
 *             XPlatformNativeWindow 回落路径）。
 * @details    本文件严格遵循 Drive 平台存根惯例（见
 *             XPlatformBackingStore_unsupported.c / XSystem_unsupported.c）：
 *             在既非 Linux X11（未检出 X11 或子开关关闭）也非 Windows
 *             Win32（子开关关闭）的平台/配置下，保持 XPlatformNativeWindow
 *             契约可链接。所有操作退化为空值：isAvailable 恒 false，
 *             create/属性同步/事件泵/上屏均安全无操作，winId 恒 0，
 *             windowForWinId 恒 NULL，nativeConnection 恒 NULL。
 *             这样 XWindow 首次显示时稳定回落嵌入式自增虚拟 WId 行为，
 *             不连接任何窗口系统，不影响嵌入式构建。
 * @note       模块总开关 XPLATFORMNATIVEWINDOW_ON 与平台子开关
 *             XPLATFORMNATIVEWINDOW_X11_ON / XPLATFORMNATIVEWINDOW_WIN32_ON
 *             定义于 XGuiConfig.h；本文件的哨兵守卫与 Drive/Posix 与
 *             Drive/windows 两个真实实现严格互斥，保证任意平台恰好编译
 *             一份原生窗口后端。
 * @author     XinYueC 团队
 ******************************************************************************/
#include "XPlatformNativeWindow.h"
#include <stddef.h>

#if XPLATFORMNATIVEWINDOW_ON

/* 安卓（Drive/Android/Graphics/XPlatformWindowAndroid.c）提供真实实现，
 * 本哨兵不参与，避免同符号双定义由链接器随机择一。 */
#if !XWINDOW_ON || !((defined(__linux__) && defined(XINYUE_C_HAS_X11) && \
       XPLATFORMNATIVEWINDOW_X11_ON) || \
      (defined(_WIN32) && XPLATFORMNATIVEWINDOW_WIN32_ON) || \
      defined(__ANDROID__))

/* ==================== 可用性与生命周期（全部空值/无操作） ==================== */

bool XPlatformNativeWindow_isAvailable(void)
{
    return false;
}

bool XPlatformNativeWindow_create(XWindow* window)
{
    (void)window;
    return false;
}

bool XPlatformNativeWindow_attachForeign(XWindow* window, XWindowId nativeId)
{
    (void)window; (void)nativeId;
    return false;
}

void XPlatformNativeWindow_destroy(XWindow* window)
{
    (void)window;
}

/* ==================== 属性同步（全部无操作） ==================== */

bool XPlatformNativeWindow_setVisible(XWindow* window, bool visible)
{
    (void)window; (void)visible;
    return false;
}

bool XPlatformNativeWindow_setWindowState(XWindow* window, uint32_t state)
{
    (void)window; (void)state;
    return false;
}

bool XPlatformNativeWindow_setWindowFlags(XWindow* window, uint32_t flags)
{
    /* 能力不足的后端默认 no-op（对标 Qt 无窗口系统插件时的行为）：
     * flags 仅存于 XWindow 内部，无平台效果。 */
    (void)window; (void)flags;
    return false;
}

bool XPlatformNativeWindow_setGeometry(XWindow* window, const XRect* geometry)
{
    (void)window; (void)geometry;
    return false;
}

bool XPlatformNativeWindow_deferGeometry(XWindow* window, bool deferred)
{
    (void)window; (void)deferred;
    return false;
}

bool XPlatformNativeWindow_setTitle(XWindow* window, const XString* title)
{
    (void)window; (void)title;
    return false;
}

bool XPlatformNativeWindow_setSizeHints(XWindow* window)
{
    (void)window; /* 无真实窗口系统：no-op。 */
    return false;
}

bool XPlatformNativeWindow_setKeyboardGrabEnabled(XWindow* window, bool grab)
{
    (void)window; (void)grab;
    return false;
}

bool XPlatformNativeWindow_setMouseGrabEnabled(XWindow* window, bool grab)
{
    (void)window; (void)grab;
    return false;
}

bool XPlatformNativeWindow_requestActivate(XWindow* window)
{
    (void)window;
    return false;
}

bool XPlatformNativeWindow_raise(XWindow* window)
{
    /* 能力不足的后端默认 no-op：无窗口系统即无 Z 序，XWindow 保持
     * 虚拟窗口行为（对标 Qt 无平台插件时 raise 为空操作）。 */
    (void)window;
    return false;
}

bool XPlatformNativeWindow_setTransientParent(XWindow* window,
                                              XWindow* parent)
{
    /* 能力不足的后端默认 no-op：无窗口系统即无 owner/Z 序语义；返回
     * true 让公共层视为已落地（虚拟窗口天然无遮挡问题）。 */
    (void)window;
    (void)parent;
    return true;
}

bool XPlatformNativeWindow_lower(XWindow* window)
{
    (void)window;
    return false;
}

XPixmap* XPlatformNativeWindow_grabWindow(XWindowId window,
                                          int x, int y, int w, int h)
{
    (void)window; (void)x; (void)y; (void)w; (void)h;
    return NULL;
}

/* ==================== 原生句柄与反查（恒空值） ==================== */

XWindowId XPlatformNativeWindow_winId(const XWindow* window)
{
    (void)window;
    return 0;
}

XWindow* XPlatformNativeWindow_windowForWinId(XWindowId id)
{
    (void)id;
    return NULL;
}

/* ==================== 原生事件泵（恒 false） ==================== */

bool XPlatformNativeWindow_processPendingEvents(void)
{
    return false;
}

bool XPlatformNativeWindow_waitForEvents(int maxMilliseconds)
{
    (void)maxMilliseconds;
    return false;
}

bool XPlatformNativeWindow_queryKeyboardModifiers(
        XKeyboardModifiers* outModifiers)
{
    (void)outModifiers;
    return false;
}

/* ==================== 上屏（恒 false，无窗口可提交） ==================== */

bool XPlatformNativeWindow_present(XWindow* window, const XImage* image,
                                   const XRegion* region,
                                   const XPoint* offset)
{
    (void)window; (void)image; (void)region; (void)offset;
    return false;
}

/* ==================== 原生连接（恒 NULL） ==================== */

void* XPlatformNativeWindow_nativeConnection(
        XPlatformNativeWindowConnectionType* outType)
{
    if (outType) *outType = XPlatformNativeWindowConnection_None;
    return NULL;
}

/* ==================== 系统移动交接/拖动显示约定（16306d93 兜底补齐） ====
 * XWindowDecoration.c / XDockWidget.c 的拖动入口无平台守卫地引用这两个
 * 契约点（fbdev 嵌入式构建同样编入），X11/Win32 真实实现分别位于
 * Drive/Posix 与 Drive/windows 的 XPlatformNativeWindow_*.c；本桩若不
 * 提供回退定义，无 X11 的 armel fbdev 构建在链接期报 undefined
 * reference。契约口径与 posix X11 段一致：
 * startSystemMove——无系统模态移动循环可交接，返回 false 让调用方回退
 * 应用层拖拽循环；
 * dragFullWindows——无 SPI_GETDRAGFULLWINDOWS「轮廓拖动」系统约定，恒
 * true 保持整窗内容跟随语义（fbdev 远程与直接展示场景本就要求实况跟随）。 */
bool XPlatformNativeWindow_startSystemMove(XWindow* window)
{
    (void)window;
    return false;
}

bool XPlatformNativeWindow_dragFullWindows(void)
{
    return true;
}

/* ==================== 剪贴板后端安装（无平台后端：no-op） ====================
 * 无窗口系统即无跨进程 Selection/Clipboard 互通：安装退化为空操作，
 * XClipboard 保持纯进程内行为。契约声明见 XPlatformNativeWindow.h，
 * 调用点 XGuiApplication_clipboard 仅受 XCLIPBOARD_ON 守卫，平台无关；
 * X11/Win32 真实实现分别在 Drive/Posix 与 Drive/windows 的
 * XPlatformNativeWindow_*.c，本桩与彼二者哨兵互斥，保证无后端平台
 * （如 Linux 未检出 X11 的 fbdev 嵌入式构建）链接完整。 */
void XPlatformNativeWindow_installClipboardBackend(void)
{
}

#endif /* 未提供真实平台实现的哨兵守卫 */
#endif /* XPLATFORMNATIVEWINDOW_ON */
