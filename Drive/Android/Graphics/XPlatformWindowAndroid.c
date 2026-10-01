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
#include "XPlatformScreen.h"
#include "XWindowSystemInterface.h"
#include "XWindow.h"
#include "XScreen.h"
#include "XGuiApplication.h"
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

/* 由 android_main.c / Drive/Android/Core/XSystemAndroid.c 提供（最小化
   moveTaskToBack；密度/原点/窗口尺寸查询已改走
   Src/XGui/Platform/XPlatformScreen.h 统一契约）；前置声明——setWindowState
   的实现位置在使用点之前，C99 隐式声明会把 bool 返回值截断成 int 引发冲突。 */
void XAndroid_setMainActivity(jobject activity);
bool XAndroid_moveTaskToBack(void);

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

/** @brief 坐标空间标定分支（设计 §4.3 探针：不预设 origin/raw 的测量
 *  口径——Android 文档称物理 px，但 WSA/模拟器实测历史分叉，运行期用
 *  getWindowFrameSize 与 ANativeWindow 尺寸比对二选一，结果打日志）。 */
typedef enum XPadViewSpace
{
    XPAD_VIEW_SPACE_PHYSICAL = 0,  /**< origin/raw 均物理 px：evt=(raw−origin)/dpr */
    XPAD_VIEW_SPACE_LOGICAL  = 1   /**< origin 为逻辑 px：evt=raw/dpr−origin */
} XPadViewSpace;

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

    /* ---- DPI/坐标空间标定（设计 §1/§4.3/§5/§6）：UI 线程（生命周期
       回调）写、框架事件线程读，一律持 m_mutex。首帧密度在
       onNativeWindowCreated 内同步采样（纯 JNI 读数不属 §1.4 信号面
       收口，可直接在 UI 线程调），探针 JNI 采样在锁外完成、结果锁内
       发布（H3/H4）。 ---- */
    float m_dpr;                      /**< 密度快照（DisplayMetrics.density；失败兜底 1.0）。 */
    int m_densityDpi;                 /**< DisplayMetrics.densityDpi（诊断日志用）。 */
    float m_xdpi;                     /**< 设备物理 DPI x（physicalSize 毫米反推用，§1.5）。 */
    float m_ydpi;                     /**< 设备物理 DPI y。 */
    bool m_densityValid;              /**< 密度 JNI 查询成功（xdpi/ydpi 才是设备真值；
                                          失败时 160 兜底是假值，禁入 mm 上报链——
                                          契约「0=未知，严禁编造」的守卫位）。 */
    XPadViewSpace m_viewSpace;        /**< 标定分支（探针结果，§4.3；Resized/Redraw 复用不重标）。 */
    float m_presentScale;             /**< 实测放大系数=surfW_actual/logicalW（§5，不盲取 dpr）。 */
    int m_logicalW;                   /**< surface 的框架逻辑口径宽（标定分支推导）。 */
    int m_logicalH;                   /**< surface 的框架逻辑口径高。 */
    bool m_screenPending;             /**< 事件线程待消费：屏幕登记/密度回填（§1.3/§1.4）。 */
} XPadState;

static XPadState g_xpad = {
    NULL, NULL,
    PTHREAD_MUTEX_INITIALIZER,
    PTHREAD_COND_INITIALIZER,
    false, false, false, false,
    { 0 }, false,
    false,
    0, 0, 0, false, false,
    /* DPI/标定默认：dpr=1、PHYSICAL、scale=1 —— 全部安全退化为现网
       dpr=1 行为（JNI 读数失败时的兜底链，设计 §10-13）。 */
    1.0f, 160, 160.0f, 160.0f, false,
    XPAD_VIEW_SPACE_PHYSICAL, 1.0f, 0, 0, false
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
#if XPLATFORM_DISPLAY_DRIVER_ABI_VERSION >= 2
            /* ABI 2 物理毫米（xdpi/ydpi 反推）：物理 px 口径，与 m_width/
               m_height 同源——消费方以 (m_width, mm) 派生的物理 DPI 恒等于
               设备 xdpi/ydpi 真值（对标 fbdev var.width/height 原值语义）。
               JNI 查询失败时 m_xdpi/ydpi 是 160 兜底假值，按契约上报
               0=未知，严禁编造（守卫位 m_densityValid，XSystemAndroid
               XAndroid_getDisplayDensity 的失败返回）。 */
            if (g_xpad.m_densityValid &&
                g_xpad.m_xdpi > 0.0f && g_xpad.m_ydpi > 0.0f) {
                outInfo->m_physicalWidthMm =
                    (float)g_xpadWidth * 25.4f / g_xpad.m_xdpi;
                outInfo->m_physicalHeightMm =
                    (float)g_xpadHeight * 25.4f / g_xpad.m_ydpi;
            }
            else {
                outInfo->m_physicalWidthMm = 0.0f;
                outInfo->m_physicalHeightMm = 0.0f;
            }
#endif
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

/** @brief 缩放路径：最近邻整数式重采样（scale!=1.0 时启用）。
 *  @details 全链唯一物理放大点（设计 §5，方案 B）：图层图像为逻辑口径
 *           后备存储，present 时按实测系数 g_xpad.m_presentScale 放大进
 *           物理 surface。内核 srcX = dstX x srcW / dstW 整数式（列映射
 *           用 Bresenham 累加器免逐像素除法，约 4 次整数运算/像素；
 *           visW>dw 的缩小同样成立），565/8888 双路各自内联。
 *           裁剪次序（§5-4 源侧裁剪缩放前）：左/上越界先折算成源裁剪量
 *           并平移目标原点；右/下与折算舍入残余由目标域最终钳制收口
 *           ——最近邻索引只依赖相对偏移，目标域截断与源侧截断逐一
 *           对应，源读索引可证不越界（srcCol<=sx0+visW-1<=w-1）。 */
static void xpad_drawImageScaled(ANativeWindow_Buffer* buf,
                                 const XImage* image,
                                 int dx, int dy, int32_t winFmt, float scale)
{
    const uint8_t* src;
    size_t srcBpl;
    int w, h, row, col;
    int dw, dh;
    int sx0, sy0, visW, visH;
    int x0, y0, x1, y1;
    int colStart, rowStart;

    src = (const uint8_t*)XImage_constBits(image);
    if (!src) return;
    w = XImage_width(image);
    h = XImage_height(image);
    srcBpl = (size_t)XImage_bytesPerLine(image);
    if (w <= 0 || h <= 0 || scale <= 0.0f) return;
    dw = (int)((float)w * scale + 0.5f);
    dh = (int)((float)h * scale + 0.5f);
    if (dw <= 0 || dh <= 0) return;

    /* 源侧裁剪（缩放前，逻辑坐标）：左/上越界折算成源裁剪量并平移
       目标原点。 */
    sx0 = 0; sy0 = 0; visW = w; visH = h;
    if (dx < 0) {
        sx0 = (int)((float)(-dx) / scale + 0.5f);
        if (sx0 > w) sx0 = w;
        visW = w - sx0;
        dx += (int)((float)sx0 * scale + 0.5f);
    }
    if (dy < 0) {
        sy0 = (int)((float)(-dy) / scale + 0.5f);
        if (sy0 > h) sy0 = h;
        visH = h - sy0;
        dy += (int)((float)sy0 * scale + 0.5f);
    }
    if (visW <= 0 || visH <= 0) return;
    dw = (int)((float)visW * scale + 0.5f);
    dh = (int)((float)visH * scale + 0.5f);
    if (dw <= 0 || dh <= 0) return;

    /* 目标域最终钳制：覆盖右/下溢出与源侧折算的舍入残余（理论可达：
       round(sx0*scale) 可比补偿量少 1 像素使 dx 残留 -1）。 */
    x0 = (dx < 0) ? 0 : dx;
    y0 = (dy < 0) ? 0 : dy;
    x1 = dx + dw; if (x1 > buf->width)  x1 = buf->width;
    y1 = dy + dh; if (y1 > buf->height) y1 = buf->height;
    colStart = x0 - dx;
    rowStart = y0 - dy;
    if (x1 <= x0 || y1 <= y0 || colStart >= dw || rowStart >= dh) return;

    if (winFmt == WINDOW_FORMAT_RGB_565) {
        /* 565 窗口：源可为 RGB16（2 字节近邻拷贝）或 ARGB32（位截断
           转换）。行距 2 字节/像素。 */
        size_t dstBpl = (size_t)buf->stride * 2u;
        int srcBpp = (XImage_format(image) == XImageFormat_RGB16) ? 2 : 4;
        int cols = x1 - x0;
        if (srcBpl < (size_t)w * (size_t)srcBpp) return;
        for (row = rowStart; row < rowStart + (y1 - y0); ++row) {
            int srcRow = sy0 + (row * visH) / dh;
            const uint8_t* s8 = src + (size_t)srcRow * srcBpl;
            uint8_t* d8 = (uint8_t*)buf->bits +
                          (size_t)(y0 + (row - rowStart)) * dstBpl +
                          (size_t)x0 * 2u;
            int colAcc = (colStart * visW) % dw;
            int srcCol = sx0 + (colStart * visW) / dw;
            for (col = 0; col < cols; ++col) {
                if (srcBpp == 2) {
                    d8[0] = s8[srcCol * 2 + 0];
                    d8[1] = s8[srcCol * 2 + 1];
                }
                else {
                    uint8_t b8 = s8[srcCol * 4 + 0];
                    uint8_t g8 = s8[srcCol * 4 + 1];
                    uint8_t r8 = s8[srcCol * 4 + 2];
                    uint16_t px = (uint16_t)(((r8 >> 3) << 11) |
                                             ((g8 >> 2) << 5) |
                                             (b8 >> 3));
                    d8[0] = (uint8_t)(px & 0xFF);
                    d8[1] = (uint8_t)(px >> 8);
                }
                colAcc += visW;
                while (colAcc >= dw) { ++srcCol; colAcc -= dw; }
                d8 += 2;
            }
        }
    }
    else {
        /* 8888 窗口（RGBA8888/RGBX8888）：ARGB32 内存序 B,G,R,A →
           R,G,B,A 逐像素通道重排。行距 4 字节/像素。 */
        int cols = x1 - x0;
        if (srcBpl < (size_t)w * 4u) return;
        for (row = rowStart; row < rowStart + (y1 - y0); ++row) {
            int srcRow = sy0 + (row * visH) / dh;
            const uint8_t* s8 = src + (size_t)srcRow * srcBpl;
            uint8_t* d8 = (uint8_t*)buf->bits +
                          (size_t)(y0 + (row - rowStart)) *
                              (size_t)buf->stride * 4u +
                          (size_t)x0 * 4u;
            int colAcc = (colStart * visW) % dw;
            int srcCol = sx0 + (colStart * visW) / dw;
            for (col = 0; col < cols; ++col) {
                d8[0] = s8[srcCol * 4 + 2];
                d8[1] = s8[srcCol * 4 + 1];
                d8[2] = s8[srcCol * 4 + 0];
                d8[3] = s8[srcCol * 4 + 3];
                colAcc += visW;
                while (colAcc >= dw) { ++srcCol; colAcc -= dw; }
                d8 += 4;
            }
        }
    }
}

/** @brief 把图像按图层位置画进已锁定的 surface 缓冲（越界裁剪）。
 *  @note  width/height 为图像（逻辑）口径；pos 为图层在 surface 内的
 *         目标位置、scale 为物理放大系数（均由调用方按实测系数折算）。
 *         scale==1.0 走历史 memcpy 快路径（dpr=1/WSA 逐位不变，红线；
 *         presentScale 在 dpr<=1 时被钳为恒 1.0，见各更新点守卫）。 */
static void xpad_drawImage(ANativeWindow_Buffer* buf, const XImage* image,
                           const XPoint* pos, int32_t winFmt, float scale)
{
    const uint8_t* src;
    size_t srcBpl;
    int w, h, x, y, row;

    if (!buf || !buf->bits || !image) return;
    if (scale != 1.0f) {
        /* 缩放路径：pos 已由调用方折算成目标系坐标。 */
        xpad_drawImageScaled(buf, image, pos->x, pos->y, winFmt, scale);
        return;
    }
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
    float presentScale;
    int i;
    bool ok = false;

    (void)region; /* 图层合成模型：每帧按登记顺序整层重画 */
    (void)offset;
    if (!window || !image) return false;

    /* 短锁①：登记本层图像 + 取 surface 引用 + 读实测放大系数（锁外
       使用，先持引用）。 */
    pthread_mutex_lock(&g_xpad.m_mutex);
    win = g_xpad.m_window;
    selfLayer = xpad_findLayer(window);
    if (selfLayer) selfLayer->lastImage = image;
    presentScale = g_xpad.m_presentScale;
    if (win) ANativeWindow_acquire(win);
    pthread_mutex_unlock(&g_xpad.m_mutex);
    if (!(presentScale > 0.0f)) presentScale = 1.0f; /* 未标定兜底 */
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
     * 全链唯一物理放大点（设计 §0/§5）：图层 pos 为框架逻辑口径，
     * 此处 x 实测系数折算物理像素后绘制（layerPosPx=round(logical x
     * scale)）；scale==1.0 时折算恒等、drawImage 走 memcpy 快路径，
     * dpr=1/WSA 逐位不变。后备存储保持逻辑尺寸零改动。
     */
    pthread_mutex_lock(&g_xpad.m_mutex);
    for (i = 0; i < g_xpadWindowCount; ++i) {
        XPadLayer* layer = &g_xpadWindows[i];
        if (layer->visible && layer->lastImage) {
            XPoint posPx;
            const XImage* img = layer->lastImage;
            posPx.x = (int)((float)layer->pos.x * presentScale + 0.5f);
            posPx.y = (int)((float)layer->pos.y * presentScale + 0.5f);
            pthread_mutex_unlock(&g_xpad.m_mutex);
            xpad_drawImage(&buf, img, &posPx, winFmt, presentScale);
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

/* ==================== 屏幕登记与密度回填（设计 §1.3/§1.4） ==================== */

/* XScreen 所有权归平台层：登记失败（false）不 delete、保留待重试——
   重试登记模式例外（所有权持续归平台层，成功前不视为泄漏；契约注释
   修订见公共层 PR 之 G4 义务）。 */
static XScreen* g_xpadScreen = NULL;   /**< 安卓唯一逻辑屏（平台层所有）。 */
static bool g_xpadScreenDone = false;  /**< 仅 handleScreenAdded 成功后置位（F6）。 */
static bool g_xpadScreenLogged = false;/**< 登记成功诊断日志一次性哨兵（差分
                                            回填不复打，避免 logcat 刷屏）。 */

/**
 * @brief  登记安卓屏幕并回填 dpr/几何/物理尺寸（仅框架事件线程调用）。
 * @details 线程口径（§1.4 唯一规则）：XObject 信号面（本函数全部
 *          XScreen_* setter 与 handleScreenAdded）只在 demo 事件线程
 *          （=渲染线程）执行；UI 线程回调（surface/config）只写 g_xpad
 *          快照 + m_screenPending，由 processPendingEvents 消费。
 *          回填定版（§0/H1/F8）：
 *          - dpr = DisplayMetrics.density（唯一密度载体）；
 *          - geometry = surface 物理 / dpr（round，框架恒逻辑像素）；
 *          - physicalSize mm = 逻辑px x 25.4 / 设备xdpi —— 由逻辑 px
 *            反推使派生 physicalDpi = 设备 xdpi 真值（XScreen 单一派生
 *            式下与 physicalSize 真值不可兼得，取舍登记 §10-9）；
 *          - logicalDpi 永不回填（保持默认 96；handleScreenLogical
 *            DotsPerInchChange 通道安卓封死——会把 densityDpi 灌进
 *            logicalDpi 触发 dpr²x(densityDpi/96) 复合爆炸）。
 *          重试守卫（F6）：XGuiApplication 未就绪或 handleScreenAdded
 *          失败时不置位、不删对象，下次 pending 消费重试（覆盖“UI 线程
 *          首试 app 未建→事件线程二试成功”竞态）；已登记后的密度变化
 *          走同一函数的差分回填分支（setter 内部值不变即静默，dpr 与
 *          geometry 成对下发）。
 */
static void xpad_ensureScreenRegistered(void)
{
    XRect logicalGeom;
    XSizeF mm;
    float dpr;
    float xdpi;
    float ydpi;
    int logicalW;
    int logicalH;
    int densityDpi;

    pthread_mutex_lock(&g_xpad.m_mutex);
    dpr = g_xpad.m_dpr;
    xdpi = g_xpad.m_xdpi;
    ydpi = g_xpad.m_ydpi;
    logicalW = g_xpad.m_logicalW;
    logicalH = g_xpad.m_logicalH;
    densityDpi = g_xpad.m_densityDpi;
    pthread_mutex_unlock(&g_xpad.m_mutex);

    if (!g_xpadScreenDone && !XGuiApplication_instance())
        return;                        /* 未就绪：保留待重试 */
    if (!g_xpadScreen) {
        g_xpadScreen = XScreen_create();
        if (!g_xpadScreen) return;
    }
    if (logicalW > 0 && logicalH > 0 && xdpi > 0.0f && ydpi > 0.0f) {
        XRect_init(&logicalGeom, 0, 0, logicalW, logicalH);
        mm.width = (float)logicalW * 25.4f / xdpi;   /* F8：逻辑 px 反推 */
        mm.height = (float)logicalH * 25.4f / ydpi;
    }
    else {
        /* surface 未标定（异常时序）：上报空几何保登记通道，等下次
           pending 带新标定值差分更新。 */
        XRect_init(&logicalGeom, 0, 0, 0, 0);
        mm.width = 0.0f;
        mm.height = 0.0f;
    }
    if (!g_xpadScreenDone) {
        if (!XWindowSystemInterface_handleScreenAdded(g_xpadScreen))
            return;                    /* 失败：不删（所有权在平台层）、
                                          不置守卫，下次 pending 重试 */
        XGuiApplication_setPrimaryScreen(g_xpadScreen);
        g_xpadScreenDone = true;
    }
    /* 回填（首次登记与运行期差分共用，dpr 与 geometry 成对下发）。
       时序定版=先登记后回填：XGuiApplication_screenAdded 内部挂接的
       dpr/logicalDpi 变化槽（公共层 §1.6 闭环）靠 setter 的值变化信号
       把 dpr 推进已建窗口并失效字体度量表——先回填后登记会把首帧
       dpr 推送发在挂槽之前而丢失。setter 差分静默，重复回填无抖动。
       上报统一走 WSI 推送面（对标 Qt QWindowSystemInterface::
       handleScreenDevicePixelRatioChange + handleScreenGeometryChange），
       平台层不再直调 XScreen_set*。 */
    XWindowSystemInterface_handleScreenDevicePixelRatioChange(g_xpadScreen,
                                                              dpr, &mm);
    XWindowSystemInterface_handleScreenGeometryChange(g_xpadScreen,
                                                      &logicalGeom, NULL);
    if (g_xpadScreenDone && !g_xpadScreenLogged) {
        g_xpadScreenLogged = true;
        XPAD_LOGI("screen registered: dpr=%.2f densityDpi=%d logical=%dx%d "
                  "mm=%.1fx%.1f xdpi=%.0f ydpi=%.0f",
                  dpr, densityDpi, logicalW, logicalH, mm.width, mm.height,
                  xdpi, ydpi);
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
    /* 登记点 2（demo 事件线程）：屏幕登记同样经 pending 槽由本线程
       processPendingEvents 消费（§1.3 两登记点统一收口 §1.4）。 */
    g_xpad.m_screenPending = true;
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
        float dpr;
        bool logicalFrame;
        if (!XPlatformScreen_queryFrameSize(&fw, &fh) || fw <= 0 || fh <= 0)
            return false;
        /* §4.3：frameSize 按标定口径换算成框架逻辑 px——LOGICAL 标定下
           frameSize 已是逻辑值直用；PHYSICAL 标定下为物理 px，÷dpr
           （round）后下发（handleGeometryChange 进框架逻辑坐标系）。 */
        pthread_mutex_lock(&g_xpad.m_mutex);
        dpr = g_xpad.m_dpr;
        logicalFrame = (g_xpad.m_viewSpace == XPAD_VIEW_SPACE_LOGICAL);
        pthread_mutex_unlock(&g_xpad.m_mutex);
        if (!logicalFrame && dpr > 0.0f) {
            fw = (int)((float)fw / dpr + 0.5f);
            fh = (int)((float)fh / dpr + 0.5f);
        }
        XRect_init(&target, 0, 0, fw, fh);
        XWindow_setGeometry_rect(window, &target);
        XWindowSystemInterface_handleGeometryChange(window, &target);
        pthread_mutex_lock(&g_xpad.m_mutex);
        g_xpad.m_surfaceDirty = true;
        pthread_cond_broadcast(&g_xpad.m_cond);
        pthread_mutex_unlock(&g_xpad.m_mutex);
        XPAD_LOGI("maximize: geometry set to %dx%d (frame %s)",
                  fw, fh, logicalFrame ? "logical" : "physical/dpr");
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

    /* ---- 0. 屏幕登记/密度回填（§1.3/§1.4）：XObject 信号面只在事件
     *        线程执行；UI 线程（surface/config 回调）只置 pending 槽。
     *        登记失败（app 未建）在函数内保留重试，下次 pending 再试。
     *        消费后向 surfaceDirty 传递：登记改变 dpr/几何需要全窗重绘
     *        的场景由后续 pending 的 expose 覆盖。 ---- */
    if (g_xpad.m_screenPending) {
        g_xpad.m_screenPending = false;
        pthread_mutex_unlock(&g_xpad.m_mutex);
        xpad_ensureScreenRegistered();
        pthread_mutex_lock(&g_xpad.m_mutex);
    }

    /* ---- 1. surface 变化：广播 Expose（不注入几何——resize 重排会清
     *        手工布局位置，见 expose 注释）。expose 矩形=逻辑全窗
     *        （§4.3：框架坐标恒逻辑口径；dpr=1 时与物理逐位一致）。 ---- */
    if (g_xpad.m_surfaceDirty && g_xpadWindowCount > 0) {
        int i;
        int count = g_xpadWindowCount;
        int logicalW = g_xpad.m_logicalW;
        int logicalH = g_xpad.m_logicalH;
        XWindow* targets[XPAD_MAX_WINDOWS];
        for (i = 0; i < count; ++i) targets[i] = g_xpadWindows[i].win;
        g_xpad.m_surfaceDirty = false;
        had = true;
        pthread_mutex_unlock(&g_xpad.m_mutex);
        {
            XRegion region;
            XRect full;
            full.x = 0; full.y = 0;
            full.width = logicalW > 0 ? logicalW : g_xpadWidth;
            full.height = logicalH > 0 ? logicalH : g_xpadHeight;
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
    /* 安卓 logicalDpi 恒保持默认 96、永不回填（H1 封死）：经
       handleScreenLogicalDotsPerInchChange 灌入 densityDpi 会触发
       dpr²x(densityDpi/96) 复合爆炸；密度唯一载体是 dpr（§0 标定定
       版），运行期密度变化走 XPad_onConfigurationChanged 的 pending
       槽差分回填（setDevicePixelRatio+setGeometry 成对）。通道本身
       保留给桌面（posix RR 现网 / win32 WM_DPICHANGED 未来批次）。 */
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
    if (!XPlatformScreen_queryFrameSize(&fw, &fh)) {
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
    XPlatformScreenDpiInfo dpiInfo;
    float density = 1.0f;
    int densityDpi = 160;
    int xdpi = 160;
    int ydpi = 160;
    bool densityValid = false;
    int viewW = 0;
    int viewH = 0;
    int surfW = 0;
    int surfH = 0;
    bool viewOk;
    XPadViewSpace viewSpace = XPAD_VIEW_SPACE_PHYSICAL;
    int logicalW = 0;
    int logicalH = 0;
    float presentScale = 1.0f;

    ANativeWindow_acquire(window); /* 持自身引用：窗口销毁回调返回后对象仍可用 */

    /* H3①：首帧密度同步采样——纯 JNI 读数（GetEnv/Attach 自适应任意
       线程），不属 §1.4 信号面收口，UI 线程可直接调；必须先于探针/
       门控求值，否则 dpr 落默认 1.0、门控走错分支（G1 要防的情形会经
       默认分支重新进入且持续到下次 surface 重建才自愈）。失败兜底
       160/1.0 = 安全退化（PHYSICAL + align 照跑 = 现网行为）。成功位随
       快照发布：xdpi/ydpi 只有查询成功才是设备真值，mm 上报链（probe
       的 ABI2 字段）据此守「0=未知，严禁编造」契约。 */
    densityValid = XPlatformScreen_queryDpi(&dpiInfo);
    density = dpiInfo.m_density;
    densityDpi = dpiInfo.m_densityDpi;
    xdpi = dpiInfo.m_xdpi;
    ydpi = dpiInfo.m_ydpi;
    /* density<1（0.75 档老设备）钳为 1.0：逻辑口径会反大于物理、present
       需要缩小路径（本设计未实现）——钳 1.0 即精确现网行为（安全退化，
       同 §10-13 兜底哲学）；主流设备/模拟器密度恒 >=1.0。 */
    if (density < 1.0f) density = 1.0f;
    /* 强制 DPI（XGUI_FORCE_DPI，调试/放大屏）：统一切口替换原生读数，
       后续标定/触摸÷dpr/present 放大/XScreen 上报全链自洽随动。 */
    density = XPlatformScreen_applyDpiOverride(density);

    /* H4②：探针采样在锁外完成（JNI 往返不持 g_xpad.m_mutex——该锁同
       时被事件线程 processPendingEvents 竞争，持锁 JNI 会阻塞事件循环
       一个 JNI 往返）。getWindowFrameSize=Activity 内容视图尺寸（View
       体系口径），ANativeWindow 尺寸=surface 原生物理值。 */
    viewOk = XPlatformScreen_queryFrameSize(&viewW, &viewH);
    surfW = ANativeWindow_getWidth(window);
    surfH = ANativeWindow_getHeight(window);

    /* 坐标空间标定探针（§4.3，不预设谁对，运行期二选一）：
       - viewW==surfW                → PHYSICAL（raw/origin 均物理 px）
       - |view*dpr − surf| <= 1      → LOGICAL（view 尺寸为逻辑口径）
       - 其他                        → PHYSICAL（按 Android 文档兜底）
       dpr=1 时 viewW==surfW 恒落 PHYSICAL——LOGICAL 分支蕴涵 dpr>1，
       按分支门控 align 因此覆盖全部有害情形（H2）。 */
    /* 标定块条件=仅 surf 有效即可（view 缺失不再整体跳过）：viewOk=false
       时 viewW/H 保持 0，三路判定自然落入 PHYSICAL 兜底分支（按 Android
       文档口径 surf/dpr 折算）——曾因 view 探针单点失败把标定整块跳过，
       m_dpr 与 m_presentScale/logicalW desync（输入 1.5/渲染 1.0，冷启动
       首帧 1:1、点击错位 p/3；后续 resize 重推条件 logicalW>0 永假无法
       自愈，直到 surface 重建重标）。 */
    if (surfW > 0 && surfH > 0) {
        float dW = (float)viewW * density - (float)surfW;
        float dH = (float)viewH * density - (float)surfH;
        if (dW < 0.0f) dW = -dW;
        if (dH < 0.0f) dH = -dH;
        if (viewW == surfW && viewH == surfH)
            viewSpace = XPAD_VIEW_SPACE_PHYSICAL;
        else if (dW <= 1.0f && dH <= 1.0f)
            viewSpace = XPAD_VIEW_SPACE_LOGICAL;
        else
            viewSpace = XPAD_VIEW_SPACE_PHYSICAL;
        /* 标定分支下逻辑尺寸来源：PHYSICAL→surf/dpr（round）；
           LOGICAL→view 尺寸即逻辑口径（§4.3.1-②）。 */
        if (viewSpace == XPAD_VIEW_SPACE_LOGICAL) {
            logicalW = viewW;
            logicalH = viewH;
        }
        else {
            logicalW = (int)((float)surfW / density + 0.5f);
            logicalH = (int)((float)surfH / density + 0.5f);
        }
        /* dpr<=1 钳 1.0：快路径红线（dpr=1/WSA 逐位不变），陈旧比值
           不得进入 dpr=1 链路；dpr>1 时系数=surfW/logicalW 实测推导。 */
        presentScale = (density > 1.0f) ? (float)surfW / (float)logicalW
                                        : 1.0f;
    }
    /* 采样失败：逻辑口径未知，重置为 scale=1 安全退化（不带旧 surface
       的陈旧系数进入新 surface）。 */

    /* 持锁段：发布密度快照 + 标定结果 + 屏幕登记待办（UI 写/事件线程
       同锁读；XScreen 信号面本身留给事件线程，§1.4）。 */
    pthread_mutex_lock(&g_xpad.m_mutex);
    old = g_xpad.m_window;
    g_xpad.m_window = window;
    g_xpad.m_dpr = density;
    g_xpad.m_densityDpi = densityDpi;
    g_xpad.m_xdpi = (float)xdpi;
    g_xpad.m_ydpi = (float)ydpi;
    g_xpad.m_densityValid = densityValid;
    g_xpad.m_viewSpace = viewSpace;
    g_xpad.m_logicalW = logicalW;
    g_xpad.m_logicalH = logicalH;
    g_xpad.m_presentScale = presentScale;
    g_xpad.m_screenPending = true;
    pthread_cond_broadcast(&g_xpad.m_cond);
    pthread_mutex_unlock(&g_xpad.m_mutex);

    /* H2③：align 按标定分支门控（与 RedrawNeeded 处统一）——仅
       LOGICAL 分支跳过（frameSize=逻辑口径，setBuffersGeometry 会把
       缓冲压成逻辑尺寸，破坏 §4.3 物理契约；可选强化=对齐
       round(viewW*dpr)）；PHYSICAL 分支保留（含 WSA 拖窗修复，任意
       dpr 正确；口径一致时对齐短路 no-op）。 */
    if (viewSpace != XPAD_VIEW_SPACE_LOGICAL)
        xpad_alignBufferToWindow(window);

    pthread_mutex_lock(&g_xpad.m_mutex);
    g_xpadWidth = ANativeWindow_getWidth(window);   /* 物理：buffer 契约（§4.3） */
    g_xpadHeight = ANativeWindow_getHeight(window);
    /* 实测放大系数（§4.3.1-④兜底）：align 可能改写缓冲几何，用对齐后
       的实际 surface 宽重推——不盲取 dpr，即使标定分支判错，系数仍与
       真实 surface/逻辑比自洽，blit 不越界不漏铺。登记时更新。 */
    if (g_xpad.m_logicalW > 0 && g_xpadWidth > 0 && g_xpad.m_dpr > 1.0f)
        g_xpad.m_presentScale = (float)g_xpadWidth / (float)g_xpad.m_logicalW;
    presentScale = g_xpad.m_presentScale;
    logicalW = g_xpad.m_logicalW;
    logicalH = g_xpad.m_logicalH;
    g_xpad.m_surfaceDirty = true;
    pthread_cond_broadcast(&g_xpad.m_cond);
    pthread_mutex_unlock(&g_xpad.m_mutex);
    if (old) ANativeWindow_release(old);
    /* 标定结果打日志（§4.3）：设备矩阵（BlueStacks/Pixel5/WSA）按此
       行核对分支判定与放大系数。 */
    XPAD_LOGI("surface created %dx%d fmt=%d viewSpace=%s dpr=%.2f (%ddpi) "
              "presentScale=%.3f logical=%dx%d view=%dx%d",
              g_xpadWidth, g_xpadHeight, ANativeWindow_getFormat(window),
              (viewSpace == XPAD_VIEW_SPACE_LOGICAL) ? "LOGICAL" : "PHYSICAL",
              density, densityDpi, presentScale, logicalW, logicalH,
              viewW, viewH);
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
    /* 实测放大系数随 resize 更新（§5：登记/resize 时更新，不盲取
       dpr）；标定分支复用不重标（§4.3.1-②）。logicalW 未标定（冷启动
       created 标定块曾整体跳过的残留态）时按 PHYSICAL 口径补齐并置
       pending 让屏幕差分回填——系数与真值解耦的部分只剩 dpr 一路。 */
    if (g_xpad.m_logicalW > 0 && g_xpadWidth > 0 && g_xpad.m_dpr > 1.0f)
        g_xpad.m_presentScale = (float)g_xpadWidth / (float)g_xpad.m_logicalW;
    else if (g_xpad.m_logicalW <= 0 && g_xpadWidth > 0 &&
             g_xpad.m_dpr > 1.0f) {
        g_xpad.m_logicalW = (int)((float)g_xpadWidth / g_xpad.m_dpr + 0.5f);
        g_xpad.m_logicalH = (int)((float)g_xpadHeight / g_xpad.m_dpr + 0.5f);
        g_xpad.m_viewSpace = XPAD_VIEW_SPACE_PHYSICAL;
        g_xpad.m_presentScale = (float)g_xpadWidth / (float)g_xpad.m_logicalW;
        g_xpad.m_screenPending = true;
    }
    g_xpad.m_surfaceDirty = true;
    pthread_cond_broadcast(&g_xpad.m_cond);
    pthread_mutex_unlock(&g_xpad.m_mutex);
    XPAD_LOGI("surface resized %dx%d (window detached until redrawNeeded) "
              "presentScale=%.3f",
              g_xpadWidth, g_xpadHeight, g_xpad.m_presentScale);
}

void XPad_onNativeWindowRedrawNeeded(ANativeWindow* window)
{
    XPadViewSpace viewSpace;
    pthread_mutex_lock(&g_xpad.m_mutex);
    /* resize 完成后的重绘请求：此时缓冲已稳定，重新挂回并全窗 expose。 */
    g_xpad.m_window = window;
    viewSpace = g_xpad.m_viewSpace;   /* 复用已标定分支，不重标 */
    pthread_mutex_unlock(&g_xpad.m_mutex);
    /* 拖动缩放后缓冲与窗口逻辑尺寸可能失配，先对齐再上报几何——
       H2③ 门控与 onNativeWindowCreated 统一：仅 LOGICAL 分支跳过
       （frameSize=逻辑口径会压小缓冲），PHYSICAL 保留（含 WSA 拖窗
       修复 :WSA-quirk，任意 dpr 正确）。 */
    if (viewSpace != XPAD_VIEW_SPACE_LOGICAL)
        xpad_alignBufferToWindow(window);
    pthread_mutex_lock(&g_xpad.m_mutex);
    g_xpadWidth = ANativeWindow_getWidth(window);
    g_xpadHeight = ANativeWindow_getHeight(window);
    if (g_xpad.m_logicalW > 0 && g_xpadWidth > 0 && g_xpad.m_dpr > 1.0f)
        g_xpad.m_presentScale = (float)g_xpadWidth / (float)g_xpad.m_logicalW;
    else if (g_xpad.m_logicalW <= 0 && g_xpadWidth > 0 &&
             g_xpad.m_dpr > 1.0f) {
        /* 未标定补齐（与 Resized 同款兜底）：冷启动标定缺口在此闭合，
           不得拖到下次 surface 重建才自愈。 */
        g_xpad.m_logicalW = (int)((float)g_xpadWidth / g_xpad.m_dpr + 0.5f);
        g_xpad.m_logicalH = (int)((float)g_xpadHeight / g_xpad.m_dpr + 0.5f);
        g_xpad.m_viewSpace = XPAD_VIEW_SPACE_PHYSICAL;
        g_xpad.m_presentScale = (float)g_xpadWidth / (float)g_xpad.m_logicalW;
        g_xpad.m_screenPending = true;
    }
    g_xpad.m_surfaceDirty = true;
    pthread_cond_broadcast(&g_xpad.m_cond);
    pthread_mutex_unlock(&g_xpad.m_mutex);
    XPAD_LOGI("redrawNeeded: window re-attached %dx%d presentScale=%.3f",
              g_xpadWidth, g_xpadHeight, g_xpad.m_presentScale);
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
    int originX = 0;
    int originY = 0;
    float dpr;
    XPadViewSpace viewSpace;
    float rawX;
    float rawY;
    XPadTouchEvent evt;

    if (type != AINPUT_EVENT_TYPE_MOTION) return;
    /* origin 查询（JNI 往返）在锁外：不持 g_xpad.m_mutex 做 JNI（与
       探针同纪律，H4）；查询失败按 (0,0) 处理——退化路径仍按标定分支
       换算，不退化为混算（§6.1）。 */
    XPlatformScreen_queryOrigin(&originX, &originY);
    pthread_mutex_lock(&g_xpad.m_mutex);
    if (!g_xpad.m_queueReady) {
        XRingBuffer_init(&g_xpad.m_touchQueue, 512);
        g_xpad.m_queueReady = true;
    }
    dpr = g_xpad.m_dpr;
    viewSpace = g_xpad.m_viewSpace;
    if (!(dpr > 0.0f)) dpr = 1.0f;
    /* 统一坐标口径 + 反缩放（§6.1：输入逻辑化的唯一承担者=平台入口
       归一化）。getRawX/Y（屏幕坐标）减窗口屏幕原点得窗口本地坐标，
       再按标定分支折算框架逻辑 px，两分支产物均为换算后逻辑坐标：
       - PHYSICAL（origin 为物理 px）：evt = (raw − origin) / dpr
       - LOGICAL （origin 为逻辑 px）：evt = raw / dpr − origin       */
    rawX = AMotionEvent_getRawX(event, 0);
    rawY = AMotionEvent_getRawY(event, 0);
    evt.action = AMotionEvent_getAction(event) & AMOTION_EVENT_ACTION_MASK;
    if (viewSpace == XPAD_VIEW_SPACE_LOGICAL) {
        evt.x = (int32_t)(rawX / dpr + 0.5f) - originX;
        evt.y = (int32_t)(rawY / dpr + 0.5f) - originY;
    }
    else {
        evt.x = (int32_t)((rawX - (float)originX) / dpr + 0.5f);
        evt.y = (int32_t)((rawY - (float)originY) / dpr + 0.5f);
    }
    if (evt.action == AMOTION_EVENT_ACTION_DOWN) {
        /* 双击检测（经典算法，检测必须在 DOWN 侧）：本次 DOWN 距
           上次 UP ≤400ms 且 ≤12px → 该 PRESS 按双击合成。阈值恒 12
           （G2：比较的 evt.x/y 与 m_lastUpX/Y 在两标定分支下均为换算
           后逻辑坐标，与桌面基线 12 逻辑 px 一致，不乘 dpr）。旧版
           放在 UP 侧比较"两次 UP"拖后一拍（三击才出一次双击），且
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

void XPad_onConfigurationChanged(void)
{
    XPlatformScreenDpiInfo dpiInfo;
    float density = 1.0f;
    int densityDpi = 160;
    int xdpi = 160;
    int ydpi = 160;
    bool densityValid = false;

    /* UI 线程（ANativeActivityCallbacks.onConfigurationChanged，wm
       density 运行期变化）：只做纯 JNI 读数 + 写 g_xpad 快照 + 置
       pending 槽；XScreen 差分回填（setDevicePixelRatio+setGeometry
       成对）由框架事件线程在 processPendingEvents 消费（§1.3/§1.4
       ——XObject 信号面永不出现在 UI 线程）。surface 尺寸随配置变化
       的重标定由系统随后重发的 onNativeWindowCreated 完成。成功位随
       快照发布（mm 上报链守「0=未知，严禁编造」，同 created 路径）。 */
    densityValid = XPlatformScreen_queryDpi(&dpiInfo);
    density = dpiInfo.m_density;
    densityDpi = dpiInfo.m_densityDpi;
    xdpi = dpiInfo.m_xdpi;
    ydpi = dpiInfo.m_ydpi;
    /* density<1 钳 1.0（同 onNativeWindowCreated：缩小路径未实现，安全
       退化为现网行为）。 */
    if (density < 1.0f) density = 1.0f;
    /* 强制 DPI（XGUI_FORCE_DPI）：与 surface 标定同切口（统一切口见
       XPlatformScreen.h）。 */
    density = XPlatformScreen_applyDpiOverride(density);
    pthread_mutex_lock(&g_xpad.m_mutex);
    g_xpad.m_dpr = density;
    g_xpad.m_densityDpi = densityDpi;
    g_xpad.m_xdpi = (float)xdpi;
    g_xpad.m_ydpi = (float)ydpi;
    g_xpad.m_densityValid = densityValid;
    g_xpad.m_screenPending = true;
    pthread_cond_broadcast(&g_xpad.m_cond);
    pthread_mutex_unlock(&g_xpad.m_mutex);
    XPAD_LOGI("configuration changed: dpr=%.2f densityDpi=%d xdpi=%d "
              "ydpi=%d (screen backfill pending)",
              density, densityDpi, xdpi, ydpi);
}

#endif /* XPLATFORMINTEGRATION_ON && XPLATFORM_ANDROID_ON */
#endif /* __ANDROID__ && XGUI_ON */
