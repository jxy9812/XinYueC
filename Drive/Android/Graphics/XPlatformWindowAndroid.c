/******************************************************************************
 * @file       XPlatformWindowAndroid.c
 * @brief      XGui 安卓平台层：NativeActivity 单窗直写 + 触摸输入注入。
 * @details    模型对齐嵌入式 fbdev 直写路径（XPlatformFramebuffer_posix
 *             的单屏注册表模型），差异仅在"帧缓冲"换成了
 *             ANativeWindow（NativeActivity 的应用窗口 surface）：
 *             - 显示：经 XPlatformDisplayDriverOps 注册表向框架报告
 *               surface 尺寸/格式/行距；present 由 ANativeWindow_lock
 *               直写 + unlock 提交（软件渲染路径，对标 linuxfb memcpy）。
 *               GPU（EGL/Vulkan）路径待后续批次接入。
 *             - 输入：NativeActivity 主线程回调（AInputQueue）经
 *               XWindowSystemInterface_handleMouseEvent 注入触摸（单点）
 *               —— 映射为左键 PRESS/RELEASE/MOVE。
 *             - 生命周期：ANativeActivityCallbacks onResume/onPause/
 *               onDestroy 与 surface Created/Changed/Destroyed 全部落到
 *               本文件的静态状态机；XGui 侧零感知。
 *             线程模型（NativeActivity 官方约定）：生命周期回调全部在
 *             ANativeActivity 的主 UI 线程；渲染/事件泵在独立的
 *             android_app->thread。XGui exec 跑在渲染线程，通过
 *             xpad_waitForEvents 阻塞在条件变量上；UI 线程投递事件后
 *             signal 唤醒。与 X11 的 "poll fd + processPendingEvents"
 *             同构：这里的事件源是 android_app 消息队列。
 * @note       仅在 __ANDROID__ && XGUI_ON && XPLATFORMINTEGRATION_ON 下
 *             编译出内容；其他平台整文件裁剪为空（与 fbdev 同款守卫）。
 *             平台 API（android/native_activity.h 等）只允许出现在
 *             Drive 目录——本文件是安卓平台 API 的唯一入口。
 * @author     XinYueC 团队
 ******************************************************************************/
#include "XPlatformNativeWindow.h"

#if defined(__ANDROID__) && XGUI_ON

#include "XPlatformDisplayDriver.h"
#include "XWindowSystemInterface.h"
#include "XWindow.h"
#include "XImage.h"
#include "XImageFormat.h"
#include "XGeometry.h"
#include "XString.h"
#include "XPrintf.h"
#include "XRingBuffer.h"

#include <android/native_activity.h>
#include <android/native_window.h>
#include <android/looper.h>
#include <android/log.h>
#include <string.h>
#include <stdlib.h>
#include <pthread.h>
#include <sched.h>
#include <time.h>

#define XPAD_LOGI(...) ((void)__android_log_print(ANDROID_LOG_INFO, "XPad", __VA_ARGS__))

/* 由 android_main.c / Drive/Android/Core/XSystemAndroid.c 提供（含 JNI
   查询与最小化 moveTaskToBack）；前置声明——setWindowState 的实现位置
   在使用点之前，C99 隐式声明会把 bool 返回值截断成 int 引发冲突。 */
void XAndroid_setMainActivity(jobject activity);
bool XAndroid_moveTaskToBack(void);
bool XAndroid_getWindowScreenOrigin(int* outX, int* outY);
bool XAndroid_getWindowFrameSize(int* outWidth, int* outHeight);

/* ==================== 编译期配置 ==================== */

#ifndef XPLATFORM_ANDROID_ON
#define XPLATFORM_ANDROID_ON 1
#endif

#if XPLATFORMINTEGRATION_ON && XPLATFORM_ANDROID_ON

/* ==================== 模块状态（单屏单窗模型） ==================== */

/** @brief 待注入触摸事件（动作 + 窗口本地坐标）。 */
typedef struct XPadTouchEvent
{
    int32_t action;
    int32_t x;
    int32_t y;
} XPadTouchEvent;

/** @brief 生命周期/surface 状态机（UI 线程写、渲染线程读，mutex 保护）。 */
typedef struct XPadState
{
    ANativeActivity* m_activity;      /**< NativeActivity 句柄。 */
    ANativeWindow* m_window;          /**< 当前 surface；NULL=无表面。 */
    pthread_mutex_t m_mutex;          /**< 保护以下全部状态。 */
    pthread_cond_t m_cond;            /**< 渲染线程等待事件泵的睡点。 */
    bool m_running;                   /**< APP_CMD_START..STOP 之间为真。 */
    bool m_destroyed;                 /**< onDestroy 已到，请求退出。 */
    bool m_focus;                     /**< 有窗口焦点。 */
    bool m_surfaceDirty;              /**< surface 新建/变尺寸，待重绘。 */

    /* 触摸事件队列：库内 XRingBuffer（字节环形缓冲，动态扩容），以
       XPadTouchEvent 为单位读写。input tap 的 DOWN/UP 同毫秒到达，
       单槽互相覆盖丢事件（实测 RELEASE 被 DOWN 覆盖，工具按钮的
       释放弹菜单永不触发），故必须排队；队列懒初始化。 */
    XRingBuffer m_touchQueue;         /**< 事件队列。 */
    bool m_queueReady;                /**< 队列已初始化。 */
    bool m_pointerDown;               /**< 当前有手指按下。 */

    /* 双击检测（平台合成 DBL_CLICK：桌面由 WM_LBUTTONDBLCLK/徐 X11
       按钮计数生成，触屏/注入路径必须自己合成）。 */
    int64_t m_lastUpMs;               /**< 上次 UP 时刻（单调毫秒）。 */
    int32_t m_lastUpX, m_lastUpY;     /**< 上次 UP 位置。 */
    bool m_haveLastUp;                /**< 已有有效上次 UP。 */
    bool m_nextPressIsDouble;         /**< 下个 PRESS 按双击合成。 */
} XPadState;

static XPadState g_xpad = {
    NULL, NULL,
    PTHREAD_MUTEX_INITIALIZER,
    PTHREAD_COND_INITIALIZER,
    false, false, false, false,
    { 0 }, false,
    false,
    0, 0, 0, false, false
};

/* 显示驱动注册句柄与几何缓存。 */
static bool g_xpadRegistered = false;
static XWindow* g_xpadPrimaryWindow = NULL;   /**< 状态控制作用的主窗。 */
static int g_xpadWidth = 0;
static int g_xpadHeight = 0;
/* 多顶层登记表：demo 会建不止一个顶层窗（主窗+菜单/弹层等 Popup）。
   弹层在单 surface 模型下按"图层"合成：记录每层的窗口位置（框架
   setGeometry 下发）与最后提交的图像引用，present 时按登记顺序
   画家算法叠加（主窗先、弹层后）。 */
#define XPAD_MAX_WINDOWS 8
typedef struct XPadLayer
{
    XWindow* win;              /**< 登记的顶层窗口。 */
    XPoint pos;                /**< 窗口位置（setGeometry 下发，主窗 (0,0)）。 */
    bool visible;              /**< setVisible 状态。 */
    const XImage* lastImage;   /**< 最近一次 present 的图像（借用，弹层叠加用）。 */
} XPadLayer;
static XPadLayer g_xpadWindows[XPAD_MAX_WINDOWS];
static XPadLayer* xpad_findLayer(XWindow* window);
static int g_xpadWindowCount = 0;

/* ==================== 显示驱动（XPlatformDisplayDriverOps 契约） ==================== */

/** @brief 把 ANativeWindow 的硬件格式映射为 XImageFormat。 */
static XImageFormat xpad_formatFromWindow(int32_t androidFormat)
{
    switch (androidFormat) {
    /* Windows 系 ABI 格式常量在 NDK 头中兼容非 Windows 平台定义。 */
    case WINDOW_FORMAT_RGBA_8888:
        return XImageFormat_ARGB32;   /* 预乘语义由框架统一处理 */
    case WINDOW_FORMAT_RGBX_8888:
        return XImageFormat_ARGB32;   /* X 通道忽略 */
    case WINDOW_FORMAT_RGB_565:
        return XImageFormat_RGB16;
    default:
        return XImageFormat_Invalid;
    }
}

static bool xpad_probe(XPlatformDisplayInfo* outInfo)
{
    bool ok;
    pthread_mutex_lock(&g_xpad.m_mutex);
    ok = g_xpad.m_window != NULL;
    if (ok) {
        g_xpadWidth = ANativeWindow_getWidth(g_xpad.m_window);
        g_xpadHeight = ANativeWindow_getHeight(g_xpad.m_window);
        if (outInfo) {
            outInfo->m_width = g_xpadWidth;
            outInfo->m_height = g_xpadHeight;
            outInfo->m_format = xpad_formatFromWindow(
                ANativeWindow_getFormat(g_xpad.m_window));
            outInfo->m_bitsPerPixel = 32;
            outInfo->m_stride = (size_t)ANativeWindow_getStride(g_xpad.m_window) * 4u;
            outInfo->m_frameBuffer = NULL;      /* 直写地址由 present 时锁出 */
            outInfo->m_frameBufferSize = 0;
            outInfo->m_doubleBuffered = false;  /* lock/unlock 单缓冲语义 */
        }
    }
    pthread_mutex_unlock(&g_xpad.m_mutex);
    return ok;
}

static bool xpad_formatNegotiate(XImageFormat preferred, XImageFormat* outFormat)
{
    XImageFormat panel;
    if (!xpad_probe(NULL)) return false;
    pthread_mutex_lock(&g_xpad.m_mutex);
    panel = xpad_formatFromWindow(
        g_xpad.m_window ? ANativeWindow_getFormat(g_xpad.m_window) : 0);
    pthread_mutex_unlock(&g_xpad.m_mutex);
    if (panel == XImageFormat_Invalid) return false;
    if (outFormat) *outFormat = panel;
    /* RGBA8888 面板可直写 ARGB32 预乘缓冲（Android 预乘约定一致）；
       RGB565 面板与 RGB16 缓冲直写。 */
    return preferred == XImageFormat_Invalid || preferred == panel;
}

static bool xpad_pan(int bufferIndex)
{
    /* ANativeWindow 无显式翻页：unlockAndPost 即提交扫描。 */
    (void)bufferIndex;
    return true;
}

static bool xpad_cacheSync(XPlatformDisplayCacheMode mode, void* address,
                           size_t length)
{
    /* ANativeWindow lock 的 CPU 写入经 GPU 合成器读取，一致性由图形栈
       保证，无需板级 cache 管理。 */
    (void)mode; (void)address; (void)length;
    return true;
}

static bool xpad_waitVsync(int timeoutMilliseconds)
{
    /* Choreographer vsync 回调接入待后续批次；软件路径 present 频率受
       XGUI_PRESENT_MAX_FPS 闸门约束，撕裂风险由 Android 合成器兜底。 */
    (void)timeoutMilliseconds;
    return false;
}

static size_t xpad_stride(int width, XImageFormat format)
{
    int32_t s;
    (void)format;
    pthread_mutex_lock(&g_xpad.m_mutex);
    s = g_xpad.m_window ? ANativeWindow_getStride(g_xpad.m_window) : 0;
    pthread_mutex_unlock(&g_xpad.m_mutex);
    if (s <= 0) s = width;   /* 无 surface 时按紧排兜底 */
    return (size_t)s * 4u;
}

static const XPlatformDisplayDriverOps g_xpadDisplayOps = {
    "android-anativewindow",
    XPLATFORM_DISPLAY_DRIVER_ABI_VERSION,
    xpad_probe,
    xpad_formatNegotiate,
    xpad_pan,
    xpad_cacheSync,
    xpad_waitVsync,
    xpad_stride
};

/* ==================== present（直写 surface + 图层合成） ==================== */

/** @brief 把 XImage 的一行（ARGB32 字节序 B,G,R,A）重排进 RGBA8888 行。 */
static void xpad_blitRowSwap(uint8_t* dst, const uint8_t* src,
                             int width)
{
    int32_t col;
    for (col = 0; col < width; ++col) {
        dst[col * 4 + 0] = src[col * 4 + 2];
        dst[col * 4 + 1] = src[col * 4 + 1];
        dst[col * 4 + 2] = src[col * 4 + 0];
        dst[col * 4 + 3] = src[col * 4 + 3];
    }
}

/** @brief 把图像按图层位置画进已锁定的 surface 缓冲（越界裁剪）。
 *  @note  srcBpl/width/height 为图像口径；pos 为图层在 surface 内的位置。 */
static void xpad_drawImage(ANativeWindow_Buffer* buf, const XImage* image,
                           const XPoint* pos, int32_t winFmt)
{
    const uint8_t* src;
    size_t srcBpl;
    int w, h, x, y, row;

    if (!buf || !buf->bits || !image) return;
    src = (const uint8_t*)XImage_constBits(image);
    if (!src) return;
    w = XImage_width(image);
    h = XImage_height(image);
    srcBpl = (size_t)XImage_bytesPerLine(image);
    if (w <= 0 || h <= 0) return;

    x = pos->x; y = pos->y;
    /* 左侧/上方越界裁剪（弹层可被拖出屏外）。 */
    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (x + w > buf->width) w = buf->width - x;
    if (y + h > buf->height) h = buf->height - y;
    if (w <= 0 || h <= 0) return;

    if (winFmt == WINDOW_FORMAT_RGB_565) {
        /* 565 窗口：图像 ARGB32(预乘,B,G,R,A 内存序)→RGB565 位截断；
           RGB16 图像按 2 字节行直拷。行距 2 字节/像素。 */
        size_t dstBpl = (size_t)buf->stride * 2u;
        int srcBpp = (XImage_format(image) == XImageFormat_RGB16) ? 2 : 4;
        if (srcBpl < (size_t)XImage_width(image) * (size_t)srcBpp) return;
        for (row = 0; row < h; ++row) {
            const uint8_t* s8 = src + (size_t)row * srcBpl;
            uint8_t* d8 = (uint8_t*)buf->bits +
                          (size_t)(y + row) * dstBpl + (size_t)x * 2u;
            int col;
            if (srcBpp == 2) {
                memcpy(d8, s8, (size_t)w * 2u);
            }
            else {
                for (col = 0; col < w; ++col) {
                    uint8_t b8 = s8[col * 4 + 0];
                    uint8_t g8 = s8[col * 4 + 1];
                    uint8_t r8 = s8[col * 4 + 2];
                    uint16_t px = (uint16_t)(((r8 >> 3) << 11) |
                                             ((g8 >> 2) << 5) |
                                             (b8 >> 3));
                    d8[col * 2 + 0] = (uint8_t)(px & 0xFF);
                    d8[col * 2 + 1] = (uint8_t)(px >> 8);
                }
            }
        }
    }
    else {
        /* 8888 窗口（RGBA8888/RGBX8888）：ARGB32 内存序 B,G,R,A →
           R,G,B,A 通道重排，行距 4 字节/像素。 */
        if (srcBpl < (size_t)XImage_width(image) * 4u) return;
        for (row = 0; row < h; ++row) {
            xpad_blitRowSwap((uint8_t*)buf->bits +
                                 (size_t)(y + row) * (size_t)buf->stride * 4u +
                                 (size_t)x * 4u,
                             src + (size_t)row * srcBpl, w);
        }
    }
}

/**
 * @brief  XPlatformNativeWindow_present 的安卓实现：锁 surface、按脏区
 *         重排行、解锁提交。
 * @note   ANativeWindow 的 stride 是像素数且可能大于宽度，必须逐行拷。
 */
static bool xpad_present(XWindow* window, const XImage* image,
                         const XRegion* region, const XPoint* offset)
{
    ANativeWindow_Buffer buf;
    ANativeWindow* win;
    int32_t winFmt;
    XPadLayer* selfLayer;
    int i;
    bool ok = false;

    (void)region; /* 图层合成模型：每帧按登记顺序整层重画 */
    (void)offset;
    if (!window || !image) return false;

    /* 短锁①：登记本层图像 + 取 surface 引用（锁外使用，先持引用）。 */
    pthread_mutex_lock(&g_xpad.m_mutex);
    win = g_xpad.m_window;
    selfLayer = xpad_findLayer(window);
    if (selfLayer) selfLayer->lastImage = image;
    if (win) ANativeWindow_acquire(win);
    pthread_mutex_unlock(&g_xpad.m_mutex);
    /* surface 未就绪：干净退出，等 onNativeWindowCreated 之后的
       surfaceDirty 全窗 expose 再画。严禁持有互斥量早退——上一版在
       无 surface 分支把锁留在自己手里，渲染线程下一次加锁与 UI 线程
       生命周期回调全体挂死（黑屏+卡死根因）。 */
    if (!win) return false;

    if (ANativeWindow_lock(win, &buf, NULL) != 0) {
        ANativeWindow_release(win);
        return false;
    }
    if (!buf.bits || buf.width <= 0 || buf.height <= 0 || buf.stride <= 0) {
        ANativeWindow_unlockAndPost(win);
        ANativeWindow_release(win);
        return false;
    }
    g_xpadWidth = buf.width;
    g_xpadHeight = buf.height;
    winFmt = ANativeWindow_getFormat(win);

    /* 仅支持 565（直拷/转换）与 8888（R/B 重排）两类窗口格式。 */
    if (winFmt != WINDOW_FORMAT_RGB_565 &&
        winFmt != WINDOW_FORMAT_RGBA_8888 &&
        winFmt != WINDOW_FORMAT_RGBX_8888) {
        ANativeWindow_unlockAndPost(win);
        ANativeWindow_release(win);
        return false;
    }

    /* 整屏清黑：图层合成模型下窗口外的 surface 区域不属于任何图层，
       不清则保留上一帧残影（最大化→还原后旧全宽标题栏/浮窗滞留）。
       lock 缓冲每帧都可能是新分配的（需初始化）或复用的（含旧帧），
       统一清零最稳妥。 */
    {
        size_t bpp = (winFmt == WINDOW_FORMAT_RGB_565) ? 2u : 4u;
        uint8_t* row8 = (uint8_t*)buf.bits;
        size_t rowBytes = (size_t)buf.stride * bpp;
        int r;
        for (r = 0; r < buf.height; ++r)
            memset(row8 + (size_t)r * rowBytes, 0, rowBytes);
    }

    /*
     * 图层合成（画家算法）：按登记顺序把每个可见图层的最后提交图像
     * 画到 surface 的窗口位置上（主窗先画，弹层按 setGeometry 位置
     * 后画覆盖其上）。本次提交的图像已在上一步记入图层表，统一走
     * lastImage 管线；逐层绘制在锁外进行（图像生命周期由后备存储
     * 持有，与 present 调用方同线程，无竞态）。
     */
    pthread_mutex_lock(&g_xpad.m_mutex);
    for (i = 0; i < g_xpadWindowCount; ++i) {
        XPadLayer* layer = &g_xpadWindows[i];
        if (layer->visible && layer->lastImage) {
            XPoint pos = layer->pos;
            const XImage* img = layer->lastImage;
            pthread_mutex_unlock(&g_xpad.m_mutex);
            xpad_drawImage(&buf, img, &pos, winFmt);
            pthread_mutex_lock(&g_xpad.m_mutex);
        }
    }
    pthread_mutex_unlock(&g_xpad.m_mutex);

    ANativeWindow_unlockAndPost(win);
    ANativeWindow_release(win);
    ok = true;
    return ok;
}

/* ==================== 原生窗口接口（XPlatformNativeWindow.h 契约） ==================== */

/* 哨兵实现约定：本文件与 Unsupported 哨兵同符号，链接期以本文件优先——
   构建系统须保证安卓构建时本目录先于 Unsupported 参与归档。 */
bool XPlatformNativeWindow_isAvailable(void)
{
    return true;
}

static XPadLayer* xpad_findLayer(XWindow* window)
{
    int i;
    for (i = 0; i < g_xpadWindowCount; ++i)
        if (g_xpadWindows[i].win == window) return &g_xpadWindows[i];
    return NULL;
}

static void xpad_registerWindow(XWindow* window)
{
    int i;
    for (i = 0; i < g_xpadWindowCount; ++i)
        if (g_xpadWindows[i].win == window) return;   /* 幂等 */
    if (g_xpadWindowCount < XPAD_MAX_WINDOWS) {
        XRect geom = XWindow_geometry(window);
        if (g_xpadPrimaryWindow == NULL)
            g_xpadPrimaryWindow = window;   /* 首个登记为主窗 */
        g_xpadWindows[g_xpadWindowCount].win = window;
        g_xpadWindows[g_xpadWindowCount].pos.x = geom.x;
        g_xpadWindows[g_xpadWindowCount].pos.y = geom.y;
        g_xpadWindows[g_xpadWindowCount].visible = true;
        g_xpadWindows[g_xpadWindowCount].lastImage = NULL;
        ++g_xpadWindowCount;
        XPAD_LOGI("register: win=%p pos=(%d,%d) size=%dx%d",
                  (void*)window, geom.x, geom.y, geom.width, geom.height);
    }
}

static void xpad_unregisterWindow(XWindow* window)
{
    int i;
    for (i = 0; i < g_xpadWindowCount; ++i) {
        if (g_xpadWindows[i].win == window) {
            g_xpadWindows[i] = g_xpadWindows[--g_xpadWindowCount];
            return;
        }
    }
}

bool XPlatformNativeWindow_create(XWindow* window)
{
    /* 安卓无独立创建步骤：NativeActivity 的 surface 就是窗口。
       surface 事件（onNativeWindowCreated）通常早于 demo 线程的窗口
       登记——dirty 若在那次已被消费（当时尚无窗口登记），这里必须
       补挂，否则初始 expose 丢失、首帧永不绘制（Win32 靠系统重发
       WM_PAINT 无此问题，安卓 surface 事件一次性）。 */
    if (!window) return false;
    pthread_mutex_lock(&g_xpad.m_mutex);
    xpad_registerWindow(window);
    if (g_xpad.m_window) g_xpad.m_surfaceDirty = true;
    pthread_cond_broadcast(&g_xpad.m_cond);
    pthread_mutex_unlock(&g_xpad.m_mutex);
    XPAD_LOGI("create: window registered (%d total), surface=%s",
              g_xpadWindowCount, g_xpad.m_window ? "yes" : "no");
    return true;
}

bool XPlatformNativeWindow_attachForeign(XWindow* window, XWindowId nativeId)
{
    if (!window) return false;
    (void)nativeId;
    pthread_mutex_lock(&g_xpad.m_mutex);
    xpad_registerWindow(window);
    pthread_mutex_unlock(&g_xpad.m_mutex);
    return true;
}

void XPlatformNativeWindow_destroy(XWindow* window)
{
    pthread_mutex_lock(&g_xpad.m_mutex);
    xpad_unregisterWindow(window);
    pthread_mutex_unlock(&g_xpad.m_mutex);
}

bool XPlatformNativeWindow_setVisible(XWindow* window, bool visible)
{
    pthread_mutex_lock(&g_xpad.m_mutex);
    {
        XPadLayer* layer = xpad_findLayer(window);
        if (layer) layer->visible = visible;
    }
    pthread_mutex_unlock(&g_xpad.m_mutex);
    return true;
}

bool XPlatformNativeWindow_setGeometry(XWindow* window, const XRect* geometry)
{
    /* 安卓主窗几何由系统决定；弹层（菜单/Popup）的位置经此处下发——
       记录到图层表供 present 按位叠加。 */
    if (!window || !geometry) return false;
    pthread_mutex_lock(&g_xpad.m_mutex);
    {
        XPadLayer* layer = xpad_findLayer(window);
        if (layer) { layer->pos.x = geometry->x; layer->pos.y = geometry->y; }
    }
    pthread_mutex_unlock(&g_xpad.m_mutex);
    XPAD_LOGI("setGeometry: win=%p pos=(%d,%d) size=%dx%d",
              (void*)window, geometry->x, geometry->y,
              geometry->width, geometry->height);
    return true;
}

bool XPlatformNativeWindow_setWindowState(XWindow* window,
                                          uint32_t state)
{
    /* 最大化：向框架回推整屏几何使控件树重排（surface 本身始终是
       全屏画布，窗口尺寸是图层属性，不动缓冲几何）。最小化：经 JNI
       moveTaskToBack 退到安卓后台（保持进程存活），回前台由 Activity
       生命周期回调自然恢复。还原（NoState）：回落常规 800x600 几何。
       本入口由框架事件处理调用，调用方不持本模块互斥量——严禁在
       入口处 unlock 不属于自己的锁（UB），也严禁持有互斥量跨框架
       调用（handleGeometryChange 会同步重排并回 present 抢锁）。 */
    XRect target;

    if (!window) return false;

    if (state & 0x00000001u /* XWindowState_Minimized */) {
        XAndroid_moveTaskToBack();
        return true;
    }

    if (state & 0x00000006u /* Maximized | FullScreen */) {
        int fw = 0;
        int fh = 0;
        if (!XAndroid_getWindowFrameSize(&fw, &fh) || fw <= 0 || fh <= 0)
            return false;
        XRect_init(&target, 0, 0, fw, fh);
        XWindow_setGeometry_rect(window, &target);
        XWindowSystemInterface_handleGeometryChange(window, &target);
        pthread_mutex_lock(&g_xpad.m_mutex);
        g_xpad.m_surfaceDirty = true;
        pthread_cond_broadcast(&g_xpad.m_cond);
        pthread_mutex_unlock(&g_xpad.m_mutex);
        XPAD_LOGI("maximize: geometry set to %dx%d", fw, fh);
        return true;
    }

    /* 还原（NoState）：回落 800x600 常规几何（与 demo 初始对齐）。 */
    XRect_init(&target, 40, 40, 800, 600);
    XWindow_setGeometry_rect(window, &target);
    XWindowSystemInterface_handleGeometryChange(window, &target);
    pthread_mutex_lock(&g_xpad.m_mutex);
    g_xpad.m_surfaceDirty = true;
    pthread_cond_broadcast(&g_xpad.m_cond);
    pthread_mutex_unlock(&g_xpad.m_mutex);
    return true;
}

bool XPlatformNativeWindow_deferGeometry(XWindow* window, bool deferred)
{
    /* 安卓单 surface：无平台级“延迟几何”通道，直接确认即可。 */
    (void)window; (void)deferred;
    return false;
}

bool XPlatformNativeWindow_setWindowFlags(XWindow* window, uint32_t flags)
{
    (void)window; (void)flags;
    return true;
}

bool XPlatformNativeWindow_setTitle(XWindow* window, const XString* title)
{
    (void)window; (void)title;
    return true;
}

bool XPlatformNativeWindow_setKeyboardGrabEnabled(XWindow* window, bool grab)
{ (void)window; (void)grab; return true; }

bool XPlatformNativeWindow_setMouseGrabEnabled(XWindow* window, bool grab)
{ (void)window; (void)grab; return true; }

bool XPlatformNativeWindow_requestActivate(XWindow* window)
{ (void)window; return true; }

bool XPlatformNativeWindow_raise(XWindow* window)
{ (void)window; return true; }

bool XPlatformNativeWindow_lower(XWindow* window)
{ (void)window; return true; }

XPixmap* XPlatformNativeWindow_grabWindow(XWindowId window,
                                          int x, int y, int w, int h)
{ (void)window; (void)x; (void)y; (void)w; (void)h; return NULL; }

XWindowId XPlatformNativeWindow_winId(const XWindow* window)
{
    (void)window;
    return 1u;   /* 单窗模型固定哨兵 ID */
}

XWindow* XPlatformNativeWindow_windowForWinId(XWindowId id)
{
    XWindow* w;
    if (id != 1u) return NULL;
    pthread_mutex_lock(&g_xpad.m_mutex);
    w = (g_xpadWindowCount > 0) ? g_xpadWindows[g_xpadWindowCount - 1].win : NULL;
    pthread_mutex_unlock(&g_xpad.m_mutex);
    return w;
}

bool XPlatformNativeWindow_processPendingEvents(void)
{
    bool had = false;
    pthread_mutex_lock(&g_xpad.m_mutex);
    /* Activity 已销毁后不再向框架注入任何事件（窗口树可能正在析构）。 */
    if (g_xpad.m_destroyed) {
        pthread_mutex_unlock(&g_xpad.m_mutex);
        return false;
    }

    /* ---- 1. surface 变化：广播 Expose（不注入几何——resize 重排会清
     *        手工布局位置，见 expose 注释） ---- */
    if (g_xpad.m_surfaceDirty && g_xpadWindowCount > 0) {
        int i;
        int count = g_xpadWindowCount;
        XWindow* targets[XPAD_MAX_WINDOWS];
        for (i = 0; i < count; ++i) targets[i] = g_xpadWindows[i].win;
        g_xpad.m_surfaceDirty = false;
        had = true;
        pthread_mutex_unlock(&g_xpad.m_mutex);
        {
            XRegion region;
            XRect full;
            full.x = 0; full.y = 0;
            full.width = g_xpadWidth; full.height = g_xpadHeight;
            XRegion_init(&region);
            XRegion_addRect(&region, &full);
            for (i = 0; i < count; ++i)
                XWindowSystemInterface_handleExposeEvent(targets[i], &region);
            XRegion_deinit(&region);
        }
        pthread_mutex_lock(&g_xpad.m_mutex);
    }

    /* ---- 2. 触摸注入（目标 = 最后一个可见图层；注入在锁外） ---- */
    {
        XPadLayer* targetLayer = NULL;
        int ti;
        for (ti = g_xpadWindowCount - 1; ti >= 0; --ti)
            if (g_xpadWindows[ti].visible) { targetLayer = &g_xpadWindows[ti]; break; }
        if (targetLayer) {
            XWindow* target = targetLayer->win;
            XPadTouchEvent evt;
            size_t got;
            while ((got = XRingBuffer_read(&g_xpad.m_touchQueue, &evt,
                                           sizeof(evt))) == sizeof(evt)) {
                XEventType type = XEVENT_TYPE_NONE;
                XMouseButton button = XMouseButton_NoButton;
                XMouseButton buttons = XMouseButton_NoButton;
                XPoint pos;
                int32_t action = evt.action;
                had = true;
                /* BlueStacks 等报屏幕坐标，WSA 报窗口本地坐标——按图层
                   登记的窗口原点换算成控件树坐标（原点 0 平台不受影响）。 */
                pos.x = evt.x - targetLayer->pos.x;
                pos.y = evt.y - targetLayer->pos.y;
                if (action == AMOTION_EVENT_ACTION_DOWN) {
                    buttons = XMouseButton_LeftButton;
                    g_xpad.m_pointerDown = true;
                    if (g_xpad.m_nextPressIsDouble) {
                        /* 双击合成（对标桌面 WM_LBUTTONDBLCLK/X11 按钮
                           计数）：快速二次按下直接发 DBL_CLICK，消费后
                           复位——标题栏双击最大化/还原依赖本类型。 */
                        type = XEVENT_TYPE_MOUSE_BUTTON_DBL_CLICK;
                        g_xpad.m_nextPressIsDouble = false;
                    }
                    else {
                        type = XEVENT_TYPE_MOUSE_BUTTON_PRESS;
                    }
                    button = XMouseButton_LeftButton;
                }
                else if (action == AMOTION_EVENT_ACTION_UP) {
                    type = XEVENT_TYPE_MOUSE_BUTTON_RELEASE;
                    button = XMouseButton_LeftButton;
                    g_xpad.m_pointerDown = false;
                }
                else if (action == AMOTION_EVENT_ACTION_MOVE ||
                         action == AMOTION_EVENT_ACTION_HOVER_MOVE) {
                    type = XEVENT_TYPE_MOUSE_MOVE;
                    if (g_xpad.m_pointerDown) buttons = XMouseButton_LeftButton;
                }
                pthread_mutex_unlock(&g_xpad.m_mutex);
                if (type != XEVENT_TYPE_NONE) {
                    /* 全局坐标必须下发：标题栏 CSD 拖拽锚用
                       XMouseEvent_globalPosition 算增量（本地系随窗口
                       自移而自指），恒 (0,0) 会让拖动纹丝不动。单
                       surface 模型下 surface 坐标即全局坐标。 */
                    XPoint global;
                    global.x = evt.x;
                    global.y = evt.y;
                    XWindowSystemInterface_handleMouseEvent_ex(target,
                                                            type,
                                                            button, buttons,
                                                            XKeyboardModifier_NoModifier,
                                                            pos, &global, 0);
                    if (type != XEVENT_TYPE_MOUSE_MOVE)
                        XPAD_LOGI("inject: type=%d local=(%d,%d) delivered=1",
                                  (int)type, pos.x, pos.y);
                }
                pthread_mutex_lock(&g_xpad.m_mutex);
            }
        }
    }
    pthread_mutex_unlock(&g_xpad.m_mutex);
    return had;
}

bool XPlatformNativeWindow_refreshScreenLogicalDpi(void)
{
    /* WSA/手机默认 160 dpi 逻辑密度；精确 DPI 待接入 AConfiguration。 */
    return true;
}

void XPlatformNativeWindow_installClipboardBackend(void)
{
    /* 安卓剪贴板（JNI ClipboardManager）待后续批次。 */
}

bool XPlatformNativeWindow_waitForEvents(int maxMilliseconds)
{
    /* 渲染线程睡在条件变量上；UI 线程事件/surface 变化 signal 唤醒。 */
    struct timespec ts;
    int rc;
    if (maxMilliseconds < 0) {
        pthread_mutex_lock(&g_xpad.m_mutex);
        while (!(g_xpad.m_queueReady &&
                 XRingBuffer_available(&g_xpad.m_touchQueue) >=
                     (size_t)sizeof(XPadTouchEvent)) &&
               !g_xpad.m_surfaceDirty && !g_xpad.m_destroyed)
            pthread_cond_wait(&g_xpad.m_cond, &g_xpad.m_mutex);
        pthread_mutex_unlock(&g_xpad.m_mutex);
        return true;
    }
    clock_gettime(CLOCK_REALTIME, &ts);
    ts.tv_sec += maxMilliseconds / 1000;
    ts.tv_nsec += (long)(maxMilliseconds % 1000) * 1000000L;
    if (ts.tv_nsec >= 1000000000L) {
        ++ts.tv_sec;
        ts.tv_nsec -= 1000000000L;
    }
    pthread_mutex_lock(&g_xpad.m_mutex);
    rc = 0;
    while (!(g_xpad.m_queueReady &&
             XRingBuffer_available(&g_xpad.m_touchQueue) >=
                 (size_t)sizeof(XPadTouchEvent)) &&
           !g_xpad.m_surfaceDirty && !g_xpad.m_destroyed && rc == 0)
        rc = pthread_cond_timedwait(&g_xpad.m_cond, &g_xpad.m_mutex, &ts);
    pthread_mutex_unlock(&g_xpad.m_mutex);
    return true;
}

bool XPlatformNativeWindow_queryKeyboardModifiers(
        XKeyboardModifiers* outModifiers)
{
    (void)outModifiers;
    return false;
}

bool XPlatformNativeWindow_present(XWindow* window, const XImage* image,
                                   const XRegion* region, const XPoint* offset)
{
    return xpad_present(window, image, region, offset);
}

void* XPlatformNativeWindow_nativeConnection(
        XPlatformNativeWindowConnectionType* outType)
{
    if (outType) *outType = XPlatformNativeWindowConnection_None;
    return NULL;
}

/* ==================== NativeActivity 生命周期接线（供入口调用） ==================== */



/**
 * @brief  把渲染缓冲重设为窗口逻辑尺寸（WSA 缩放对齐）。
 * @details WSA 拖动窗口改变大小时只改窗口逻辑尺寸，surface 缓冲保持旧
 *          尺寸被拉伸显示；输入事件在逻辑空间而渲染在旧像素空间，点击
 *          全部落偏。经 JNI 查询 decorView 尺寸后 setBuffersGeometry 对
 *          齐，输入与渲染坐标 1:1。查询失败保持现状（真机无此问题）。
 */
static void xpad_alignBufferToWindow(ANativeWindow* win)
{
    int fw = 0;
    int fh = 0;
    int32_t format;
    if (!win) return;
    if (!XAndroid_getWindowFrameSize(&fw, &fh)) {
        XPAD_LOGI("align: frame size query failed, keep buffer");
        return;
    }
    if (fw <= 0 || fh <= 0) return;
    if (ANativeWindow_getWidth(win) == fw &&
        ANativeWindow_getHeight(win) == fh)
        return; /* 已对齐 */
    format = ANativeWindow_getFormat(win);
    if (ANativeWindow_setBuffersGeometry(win, fw, fh, format) == 0)
        XPAD_LOGI("align: buffer set to %dx%d", fw, fh);
    else
        XPAD_LOGI("align: setBuffersGeometry %dx%d failed", fw, fh);
}

void XPad_onNativeWindowCreated(ANativeWindow* window)
{
    ANativeWindow* old = NULL;
    ANativeWindow_acquire(window); /* 持自身引用：窗口销毁回调返回后对象仍可用 */
    pthread_mutex_lock(&g_xpad.m_mutex);
    old = g_xpad.m_window;
    g_xpad.m_window = window;
    pthread_mutex_unlock(&g_xpad.m_mutex);
    /* 缓冲与窗口逻辑尺寸对齐（WSA 缩放窗口时 surface 不随动）。 */
    xpad_alignBufferToWindow(window);
    pthread_mutex_lock(&g_xpad.m_mutex);
    g_xpadWidth = ANativeWindow_getWidth(window);
    g_xpadHeight = ANativeWindow_getHeight(window);
    g_xpad.m_surfaceDirty = true;
    pthread_cond_broadcast(&g_xpad.m_cond);
    pthread_mutex_unlock(&g_xpad.m_mutex);
    if (old) ANativeWindow_release(old);
    XPAD_LOGI("surface created %dx%d fmt=%d", g_xpadWidth, g_xpadHeight,
              ANativeWindow_getFormat(window));
}

void XPad_onNativeWindowResized(ANativeWindow* window)
{
    pthread_mutex_lock(&g_xpad.m_mutex);
    /* resize 期间系统可能重分配 surface 缓冲：摘下窗口指针，present
       立即放弃本次提交（VSADragResizing 实测拖动窗口边缘时旧映射
       写入 SEGV）。onNativeWindowRedrawNeeded 到达后再挂回。 */
    if (g_xpad.m_window == window) g_xpad.m_window = NULL;
    g_xpadWidth = ANativeWindow_getWidth(window);
    g_xpadHeight = ANativeWindow_getHeight(window);
    g_xpad.m_surfaceDirty = true;
    pthread_cond_broadcast(&g_xpad.m_cond);
    pthread_mutex_unlock(&g_xpad.m_mutex);
    XPAD_LOGI("surface resized %dx%d (window detached until redrawNeeded)",
              g_xpadWidth, g_xpadHeight);
}

void XPad_onNativeWindowRedrawNeeded(ANativeWindow* window)
{
    pthread_mutex_lock(&g_xpad.m_mutex);
    /* resize 完成后的重绘请求：此时缓冲已稳定，重新挂回并全窗 expose。 */
    g_xpad.m_window = window;
    pthread_mutex_unlock(&g_xpad.m_mutex);
    /* 拖动缩放后缓冲与窗口逻辑尺寸可能失配，先对齐再上报几何。 */
    xpad_alignBufferToWindow(window);
    pthread_mutex_lock(&g_xpad.m_mutex);
    g_xpadWidth = ANativeWindow_getWidth(window);
    g_xpadHeight = ANativeWindow_getHeight(window);
    g_xpad.m_surfaceDirty = true;
    pthread_cond_broadcast(&g_xpad.m_cond);
    pthread_mutex_unlock(&g_xpad.m_mutex);
    XPAD_LOGI("redrawNeeded: window re-attached %dx%d",
              g_xpadWidth, g_xpadHeight);
}

void XPad_onNativeWindowDestroyed(void)
{
    ANativeWindow* old = NULL;
    pthread_mutex_lock(&g_xpad.m_mutex);
    old = g_xpad.m_window;
    g_xpad.m_window = NULL;
    g_xpad.m_surfaceDirty = false;
    pthread_cond_broadcast(&g_xpad.m_cond);
    pthread_mutex_unlock(&g_xpad.m_mutex);
    if (old) ANativeWindow_release(old); /* 引用计数保证 present 中的对象存活 */
}

void XPad_onInputEvent(AInputEvent* event)
{
    int32_t type = AInputEvent_getType(event);
    if (type != AINPUT_EVENT_TYPE_MOTION) return;
    pthread_mutex_lock(&g_xpad.m_mutex);
    if (!g_xpad.m_queueReady) {
        XRingBuffer_init(&g_xpad.m_touchQueue, 512);
        g_xpad.m_queueReady = true;
    }
    {
        /* 统一坐标口径：getX/Y 在不同模拟器上空间不一致（WSA 报窗口
           本地坐标，BlueStacks 实测报屏幕坐标）。一律用 getRawX/Y
           （屏幕坐标）减窗口屏幕原点，得到与控件几何一致的窗口本地
           坐标（原点查询失败按 0 处理，退化为 raw 直用）。 */
        int originX = 0;
        int originY = 0;
        XPadTouchEvent evt;
        XAndroid_getWindowScreenOrigin(&originX, &originY);
        evt.action = AMotionEvent_getAction(event) & AMOTION_EVENT_ACTION_MASK;
        evt.x = (int32_t)AMotionEvent_getRawX(event, 0) - originX;
        evt.y = (int32_t)AMotionEvent_getRawY(event, 0) - originY;
        if (evt.action == AMOTION_EVENT_ACTION_DOWN) {
            /* 双击检测（经典算法，检测必须在 DOWN 侧）：本次 DOWN 距
               上次 UP ≤400ms 且 ≤12px → 该 PRESS 按双击合成。旧版放在
               UP 侧比较"两次 UP"拖后一拍（三击才出一次双击），且
               m_haveLastUp 从未置 true 导致永不触发。 */
            struct timespec now;
            int64_t nowMs;
            clock_gettime(CLOCK_MONOTONIC, &now);
            nowMs = (int64_t)now.tv_sec * 1000LL + now.tv_nsec / 1000000LL;
            g_xpad.m_nextPressIsDouble =
                g_xpad.m_haveLastUp &&
                nowMs - g_xpad.m_lastUpMs <= 400 &&
                abs(evt.x - g_xpad.m_lastUpX) <= 12 &&
                abs(evt.y - g_xpad.m_lastUpY) <= 12;
        }
        else if (evt.action == AMOTION_EVENT_ACTION_UP) {
            struct timespec now;
            clock_gettime(CLOCK_MONOTONIC, &now);
            g_xpad.m_lastUpMs =
                (int64_t)now.tv_sec * 1000LL + now.tv_nsec / 1000000LL;
            g_xpad.m_lastUpX = evt.x;
            g_xpad.m_lastUpY = evt.y;
            g_xpad.m_haveLastUp = true;
        }
        XRingBuffer_write(&g_xpad.m_touchQueue, &evt, sizeof(evt));
    }
    pthread_cond_broadcast(&g_xpad.m_cond);
    pthread_mutex_unlock(&g_xpad.m_mutex);
    /* 诊断：动作与窗口本地坐标（AMotionEvent 已是窗口空间）。 */
    XPAD_LOGI("touch: action=%d x=%.0f y=%.0f",
              (int)(AMotionEvent_getAction(event) & AMOTION_EVENT_ACTION_MASK),
              AMotionEvent_getX(event, 0), AMotionEvent_getY(event, 0));
}

void XPad_setRunning(bool running)
{
    pthread_mutex_lock(&g_xpad.m_mutex);
    g_xpad.m_running = running;
    pthread_cond_broadcast(&g_xpad.m_cond);
    pthread_mutex_unlock(&g_xpad.m_mutex);
}

void XPad_setDestroyed(void)
{
    pthread_mutex_lock(&g_xpad.m_mutex);
    g_xpad.m_destroyed = true;
    pthread_cond_broadcast(&g_xpad.m_cond);
    pthread_mutex_unlock(&g_xpad.m_mutex);
}

void XPad_resetDestroyed(void)
{
    pthread_mutex_lock(&g_xpad.m_mutex);
    g_xpad.m_destroyed = false;
    pthread_mutex_unlock(&g_xpad.m_mutex);
}

void XPad_setActivity(ANativeActivity* activity)
{
    pthread_mutex_lock(&g_xpad.m_mutex);
    g_xpad.m_activity = activity;
    pthread_mutex_unlock(&g_xpad.m_mutex);
}

#endif /* XPLATFORMINTEGRATION_ON && XPLATFORM_ANDROID_ON */
#endif /* __ANDROID__ && XGUI_ON */
