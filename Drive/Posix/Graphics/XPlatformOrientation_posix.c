/******************************************************************************
 * @file       XPlatformOrientation_posix.c
 * @brief      XPlatformOrientation 契约的 POSIX 实现（X11 Xrandr 屏幕旋转）。
 * @details    POSIX 侧有两类显示后端，本契约对它们的结论不同，如实分开：
 *
 *             - **X11 桌面**：显示方向由 Xrandr 的屏幕配置（screen
 *               config）持有，可用 XRRSetScreenConfigAndSize 改。本实现
 *               自开一条 X 连接（不复用窗口后端的单连接，避免跨文件
 *               耦合，纪律同 XPlatformPower_posix.c 的 DPMS 段）。
 *             - **fbdev 直写面板**：面板朝向是**硬件/驱动属性**，fbdev
 *               没有通用旋转原语（fb_var_screeninfo.rotate 需驱动
 *               显式支持，工业屏驱动绝大多数不支持，且写入失败的语义
 *               依驱动而异——有的是静默忽略、有的是整块黑屏）。故
 *               fbdev 侧**不声明任何能力**（0=不支持），界面据此把
 *               「窗口方向」置灰，而不是让用户点了屏幕不动。需要转屏
 *               的嵌入式面板请走驱动/系统侧手段，或改用框架的「画布
 *               旋转」（应用内逐像素转置，与本契约正交）。
 *
 *             RandR 调用纪律：旋转配置不被服务端接受时，X11 走的是
 *             **协议错误**（BadValue/BadRRConfig），Xlib 默认错误处理
 *             器会直接终止进程——故本实现在调用窗口内挂一个吞错误的
 *             临时错误处理器，并在返回前 XSync 冲刷，调用完立即还原
 *             原处理器（错误处理器是进程全局的，不还原会污染后续所有
 *             X 调用）。
 * @note       平台 API 只出现在 Drive（见 Drive/README.md）。
 * @author     XinYueC 团队
 ******************************************************************************/
#include "XPlatformOrientation.h"

#if XGUI_ON && defined(__linux__) && !defined(__ANDROID__)

/* ---- X11 面编译开关 ---- */
#if defined(XINYUE_C_HAS_X11)
#include <X11/Xlib.h>
#define XORIENT_X11_ON 1
#else
#define XORIENT_X11_ON 0
#endif

#if XORIENT_X11_ON

/* RandR 最小原型（开发头缺失时的内联 ABI 声明，纪律同
 * XPlatformNativeWindow_posix.c 的 RandR 段 / XPlatformPower_posix.c 的
 * DPMS 段）：本处只用参数全为标量的两个入口，不触碰任何需要结构体
 * 布局的接口（XRRScreenResources/XRROutputInfo 的手写布局风险高，
 * 宁可不查——故本实现不声明 Query 能力，见文件头说明）。
 * 时间戳参数按 X11 的 Time（unsigned long）口径手写：新版 Xlib.h 已
 * 不再提供 XTime 这个旧别名，只留 Time。
 * 入口选 XRRSetScreenConfig（sizeID=XRRConfigCurrent）而非
 * XRRSetScreenConfigAndSize：后者在 libXrandr 运行库里并不存在（实测
 * 本机 libXrandr.so.2 只导出 XRRSetScreenConfig/XRRSetScreenConfigAndRate
 * /XRRSetCrtcConfig），引用它即断链。 */
extern Bool XRRQueryExtension(Display* dpy, int* eventBase, int* errorBase);
extern Status XRRSetScreenConfig(Display* dpy, Drawable drawable,
                                  int rotation, unsigned long sizeID,
                                  unsigned long timestamp);

/* RandR 旋转档（XRandR 规范的 Rotate_0/90/180/270 数值）。 */
#define XORIENT_RR_ROTATE_0   0
#define XORIENT_RR_ROTATE_90  1
#define XORIENT_RR_ROTATE_180 2
#define XORIENT_RR_ROTATE_270 3

/* 「沿用当前尺寸」档（XRandR 规范的 XRRConfigCurrent）。 */
#define XORIENT_RR_CONFIG_CURRENT 0

static Display* g_xoriDisplay = NULL; /**< 惰性自开的 X 连接。 */

/** @brief 临时错误处理器：吞掉 X 协议错误（否则 Xlib 默认处理器会
 *         直接终止进程）。 */
static int xori_swallowError(Display* dpy, XErrorEvent* err)
{
    (void)dpy;
    (void)err;
    return 0;
}

/** @brief 惰性取得 X 连接（连接失败不缓存，下次再试）。 */
static Display* xoriX11Display(void)
{
    if (!g_xoriDisplay) {
        g_xoriDisplay = XOpenDisplay(NULL);
    }
    return g_xoriDisplay;
}

/** @brief RandR 扩展是否可用（连接可用时结果缓存）。 */
static bool xoriX11Usable(void)
{
    static bool s_probed = false;
    static bool s_usable = false;
    Display* dpy;
    int eventBase = 0;
    int errorBase = 0;
    if (s_probed) return s_usable;
    dpy = xoriX11Display();
    if (!dpy) return false; /* 连接失败不缓存：X 起来得晚时可再试。 */
    s_probed = true;
    if (!XRRQueryExtension(dpy, &eventBase, &errorBase)) return false;
    if (errorBase != 0) return false; /* 服务端报告扩展错误=不可用。 */
    s_usable = true;
    return true;
}

/** @brief 把本契约的度数换算为 RandR 旋转档。
 *  @return RandR 档值；角度非法返回 -1。
 *  @note   角度口径（本契约统一）：0=横屏、90=竖屏、180=倒置横屏、
 *          270=倒置竖屏；与 RandR 的 Rotate_0/90/180/270 一一对应
 *          （顺时针口径一致）。 */
static int xoriDegreesToRandR(int degrees)
{
    switch (degrees) {
        case 0:   return XORIENT_RR_ROTATE_0;
        case 90:  return XORIENT_RR_ROTATE_90;
        case 180: return XORIENT_RR_ROTATE_180;
        case 270: return XORIENT_RR_ROTATE_270;
        default:  return -1;
    }
}

#endif /* XORIENT_X11_ON */

/* ==================== 契约实现 ==================== */

unsigned XPlatformOrientationDriver_capabilities(void)
{
    unsigned caps = 0u;
#if XORIENT_X11_ON
    if (xoriX11Usable()) {
        /* 可申请指定方向 + 可交回默认朝向（RandR Rotate_0）。
           **不声明 Query**：查当前旋转需要 XRRScreenResources/
           XRROutputInfo 的结构体布局，开发头缺失时手写布局风险过高，
           宁可不查——界面回显改用公共层的影子值。 */
        caps |= (unsigned)XPlatformOrientationCap_Request;
        caps |= (unsigned)XPlatformOrientationCap_Auto;
    }
#endif
    /* fbdev 直写面板：无通用旋转原语 → 不声明任何能力（见文件头）。 */
    return caps;
}

bool XPlatformOrientationDriver_setOrientation(int degrees)
{
#if XORIENT_X11_ON
    Display* dpy;
    int screen;
    int rot;
    Status st;
    XErrorHandler oldHandler;

    if (!xoriX11Usable()) return false;
    dpy = xoriX11Display();
    if (!dpy) return false;

    if (degrees == XPLATFORM_ORIENTATION_AUTO) {
        /* 「跟随系统」在 X11 侧的落点是回到 RandR 的默认档（Rotate_0，
         * 即屏幕不旋转）——X11 没有「交回传感器」的概念。 */
        rot = XORIENT_RR_ROTATE_0;
    } else {
        rot = xoriDegreesToRandR(degrees);
        if (rot < 0) return false;
    }
    screen = DefaultScreen(dpy);
    /* 旋转配置不被服务端接受时走 X 协议错误 → 默认错误处理器会终止
       进程，故调用窗口内挂临时吞错处理器，调用完立即还原。 */
    oldHandler = XSetErrorHandler(xori_swallowError);
    st = XRRSetScreenConfig(dpy, RootWindow(dpy, screen), rot,
                             XORIENT_RR_CONFIG_CURRENT, 0);
    XSync(dpy, False);
    XSetErrorHandler(oldHandler);
    return (st != 0);
#else
    /* fbdev/无 X11：面板朝向是硬件属性，无通用原语 → 如实失败。 */
    (void)degrees;
    return false;
#endif
}

int XPlatformOrientationDriver_currentOrientation(void)
{
    /* 本平台不回读实际方向（见能力位说明）：查 RandR 旋转需结构体
       布局，开发头缺失时不冒 ABI 风险；未知即 -1。 */
    return -1;
}

#endif /* XGUI_ON && defined(__linux__) && !defined(__ANDROID__) */
