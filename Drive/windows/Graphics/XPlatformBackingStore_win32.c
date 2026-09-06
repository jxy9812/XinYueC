/******************************************************************************
 * @file       XPlatformBackingStore_win32.c
 * @brief      Windows XPlatformBackingStore GDI 提交驱动。
 * @details    Windows 后端的全部软件缓冲逻辑（双缓冲 XImage、resize/滚动/
 *             静态内容/tile 遍历/外部缓冲）已上提到公共层
 *             Src/XGui/Platform/XPlatformBackingStore.c；本文件只提供
 *             「把脏区提交到真实窗口」的 Driver 钩子：
 *             - nativeState 持有与缓冲等宽的 32 位自顶向下 DIB section
 *               (CreateDIBSection)；DIB 的 BGRA 字节序与 XImage ARGB32
 *               小端内存布局一致，可逐行直接拷贝，无需像素转换；
 *             - present：先把脏矩形对应的 XImage 行同步进 DIB，再把每块
 *               脏区经 BitBlt(SRCCOPY) 从内存 DC 合成到目标窗口 DC
 *               （FULL 模式经 SetDIBitsToDevice 整屏上传）；
 *             - presentTile：窗口已挂接真实 HWND 时经
 *               XPlatformNativeWindow_present 上屏；
 *             - 目标窗口句柄（HWND）经
 *               XPlatformBackingStoreDriver_setNativeTarget 登记；未登记时
 *               回退到 XWindow 平台注册表的真实 HWND。
 *             本文件只在 _WIN32 下编译，公共层不包含本头外的任何 Windows
 *             类型。
 * @note       文件同时受 XBACKINGSTORE_ON 与 XPLATFORMBACKINGSTORE_ON 总
 *             开关约束（Drive 平台后端惯例）。
 ******************************************************************************/

#include "XPlatformBackingStore.h"

#if XBACKINGSTORE_ON && XPLATFORMBACKINGSTORE_ON

#if defined(_WIN32)

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0600
#endif

#include "XImage.h"
#include "XMemory.h"
#if XPLATFORMNATIVEWINDOW_ON
#include "XPlatformNativeWindow.h"
#include "XWindow.h"
#endif /* XPLATFORMNATIVEWINDOW_ON */
#include <string.h>
#include <windows.h>

/** @brief Windows 平台提交状态（GDI DIB/内存 DC）。 */
struct XWin32BackingStoreNative
{
    HWND m_nativeTarget;                       /**< 显式登记的目标 HWND（借用）。 */
    HDC m_memDC;                               /**< 内存 DC（拥有）。 */
    HBITMAP m_dib;                             /**< DIB section 位图（拥有）。 */
    HBITMAP m_oldBitmap;                       /**< memDC 中原位图（还原用）。 */
    uint8_t* m_dibBits;                        /**< DIB section 系统堆指针（借用自 m_dib）。 */
    int m_width;                               /**< DIB 宽度（与缓冲一致）。 */
    int m_height;                              /**< DIB 高度（与缓冲一致）。 */
};

/** @brief 释放内存 DC 与 DIB section（失败路径也保持安全，可重复调用）。 */
static void xpbs_win32_releaseSurface(struct XWin32BackingStoreNative* state)
{
    if (!state) return;
    if (state->m_memDC && state->m_oldBitmap)
    {
        SelectObject(state->m_memDC, state->m_oldBitmap);
        state->m_oldBitmap = NULL;
    }
    if (state->m_dib)
    {
        DeleteObject(state->m_dib);
        state->m_dib = NULL;
    }
    if (state->m_memDC)
    {
        DeleteDC(state->m_memDC);
        state->m_memDC = NULL;
    }
    state->m_dibBits = NULL;
    state->m_width = 0;
    state->m_height = 0;
}

/** @brief 按指定尺寸创建 DIB section 与内存 DC。 */
static bool xpbs_win32_createSurface(struct XWin32BackingStoreNative* state,
                                     int w, int h)
{
    BITMAPINFO bmi;
    void* bits = NULL;
    if (!state || w <= 0 || h <= 0) return false;
    memset(&bmi, 0, sizeof(bmi));
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = w;
    bmi.bmiHeader.biHeight = -h; /* 自顶向下：行 0 与 XImage 顶部一致。 */
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;
    state->m_dib = CreateDIBSection(NULL, &bmi, DIB_RGB_COLORS, &bits, NULL, 0);
    if (!state->m_dib || !bits) goto fail;
    state->m_memDC = CreateCompatibleDC(NULL);
    if (!state->m_memDC) goto fail;
    state->m_oldBitmap = (HBITMAP)SelectObject(state->m_memDC, state->m_dib);
    if (!state->m_oldBitmap) goto fail;
    state->m_dibBits = (uint8_t*)bits;
    state->m_width = w;
    state->m_height = h;
    return true;
fail:
    xpbs_win32_releaseSurface(state);
    return false;
}

/** @brief 解析提交目标 HWND：显式登记的 nativeTarget 优先，其次窗口 WId。 */
static HWND xpbs_win32_targetHwnd(struct XWin32BackingStoreNative* state,
                                  XWindow* window)
{
    HWND hwnd;
    if (!state) return NULL;
    hwnd = state->m_nativeTarget;
#if XPLATFORMNATIVEWINDOW_ON
    if (!hwnd && window)
        hwnd = (HWND)(uintptr_t)XPlatformNativeWindow_winId(window);
#endif /* XPLATFORMNATIVEWINDOW_ON */
    if (!hwnd || !IsWindow(hwnd)) return NULL;
    return hwnd;
}

/** @brief 把脏矩形的 XImage 行同步进 DIB（两缓冲字节序一致，逐行拷贝）。 */
static void xpbs_win32_syncDirtyRect(struct XWin32BackingStoreNative* state,
                                     const XImage* image, const XRect* rect)
{
    const uint8_t* sbuf;
    uint8_t* dbuf;
    int bpl;
    int row;
    if (!state || !state->m_dibBits || !rect || !image || !image->m_data) return;
    sbuf = XImage_constBits(image);
    dbuf = state->m_dibBits;
    bpl = XImage_bytesPerLine(image);
    if (!sbuf || !dbuf || bpl <= 0) return;
    for (row = 0; row < rect->height; ++row)
        memcpy(dbuf + (int64_t)(rect->y + row) * bpl + (int64_t)rect->x * 4,
               sbuf + (int64_t)(rect->y + row) * bpl + (int64_t)rect->x * 4,
               (size_t)rect->width * 4u);
}

/** @brief 从内存 DC 把一帧脏区提交到目标窗口 DC。
 * @details 取得/释放目标 DC 是一次刷新中最昂贵的部分。DIRECT/FULL
 *          模式先同步所有脏矩形，再复用同一个 DC 完成全部 BitBlt，
 *          对齐 LVGL Windows 在最后一次 flush 中一次提交 framebuffer 的
 *          行为。 */
static void xpbs_win32_presentRegion(struct XWin32BackingStoreNative* state,
                                     HWND hwnd, const XRegion* region, bool full)
{
    HDC winDC;
#if XGUI_BACKINGSTORE_RENDER_MODE != XGUI_BACKINGSTORE_RENDER_MODE_FULL
    int i;
#endif
    (void)full;
    if (!state || !state->m_memDC || !region || region->count <= 0) return;
    if (!hwnd || !IsWindow(hwnd)) return;
    winDC = GetDC(hwnd);
    if (!winDC) return;
#if XGUI_BACKINGSTORE_RENDER_MODE != XGUI_BACKINGSTORE_RENDER_MODE_FULL
    /* PARTIAL/DIRECT 都按脏矩形提交：同步进 DIB 的只有变化区域，
       因此上屏也只 BitBlt 这些矩形。DIRECT 虽然持有整帧双缓冲，
       但每帧只把变化的小块上传到窗口，帧成本与窗口面积无关。 */
    (void)full;
    for (i = 0; i < region->count; ++i) {
        const XRect* rect = &region->rects[i];
        if (rect->width > 0 && rect->height > 0)
            BitBlt(winDC, rect->x, rect->y, rect->width, rect->height,
                   state->m_memDC, rect->x, rect->y, SRCCOPY);
    }
#else
    /* FULL 的语义是每次提交整屏：即使脏区很小也整帧上传。 */
    {
        BITMAPINFO bmi;
        int width = state->m_width;
        int height = state->m_height;
        if (width > 0 && height > 0 && state->m_dibBits) {
            memset(&bmi, 0, sizeof(bmi));
            bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
            bmi.bmiHeader.biWidth = width;
            bmi.bmiHeader.biHeight = -height;
            bmi.bmiHeader.biPlanes = 1;
            bmi.bmiHeader.biBitCount = 32;
            bmi.bmiHeader.biCompression = BI_RGB;
            SetDIBitsToDevice(winDC, 0, 0, (DWORD)width, (DWORD)height,
                              0, 0, 0, (UINT)height, state->m_dibBits,
                              &bmi, DIB_RGB_COLORS);
        }
    }
#endif
    ReleaseDC(hwnd, winDC);
}

/* ==================== 平台提交驱动（对标 XPlatformGraphicsDriver_*） ==================== */

bool XPlatformBackingStoreDriver_create(void** outState, XWindow* window)
{
    struct XWin32BackingStoreNative* state;
    (void)window;
    if (!outState) return false;
    state = (struct XWin32BackingStoreNative*)XCalloc_System(
        1u, sizeof(struct XWin32BackingStoreNative));
    if (!state) { *outState = NULL; return false; }
    *outState = state;
    return true;
}

void XPlatformBackingStoreDriver_destroy(void* nativeState)
{
    struct XWin32BackingStoreNative* state =
        (struct XWin32BackingStoreNative*)nativeState;
    if (!state) return;
    xpbs_win32_releaseSurface(state);
    XFree_System(state);
}

void XPlatformBackingStoreDriver_setNativeTarget(void* nativeState,
                                                 void* nativeWindow)
{
    struct XWin32BackingStoreNative* state =
        (struct XWin32BackingStoreNative*)nativeState;
    if (!state) return;
    state->m_nativeTarget = (HWND)nativeWindow;
}

void XPlatformBackingStoreDriver_surfaceResized(void* nativeState,
                                                int width, int height)
{
    struct XWin32BackingStoreNative* state =
        (struct XWin32BackingStoreNative*)nativeState;
    if (!state) return;
    xpbs_win32_releaseSurface(state);
    if (width > 0 && height > 0)
        xpbs_win32_createSurface(state, width, height);
}

void XPlatformBackingStoreDriver_present(void* nativeState, XWindow* window,
                                         const XImage* image,
                                         const XRegion* region,
                                         const XPoint* offset, bool full)
{
    struct XWin32BackingStoreNative* state =
        (struct XWin32BackingStoreNative*)nativeState;
    HWND hwnd;
    int i;
    if (!state || !image || !image->m_data || !region || region->count <= 0)
        return;
    /* 目标 DC 只能在真实窗口上取得；无 HWND（离屏/测试）时提交交给
       公共层统一触发的 present 回调。 */
    hwnd = xpbs_win32_targetHwnd(state, window);
    if (!hwnd) return;
    /* 懒保护：create 后直接 flush（未经 resize）时 DIB 尚未建立，用当前
       缓冲尺寸补建；正常路径由 surfaceResized 在 resize 时重建。 */
    if (!state->m_memDC)
        xpbs_win32_createSurface(state, XImage_width(image),
                                 XImage_height(image));
    if (!state->m_memDC) return;
    for (i = 0; i < region->count; ++i)
        xpbs_win32_syncDirtyRect(state, image, &region->rects[i]);
    xpbs_win32_presentRegion(state, hwnd, region,
#if XGUI_BACKINGSTORE_RENDER_MODE == XGUI_BACKINGSTORE_RENDER_MODE_FULL
                             true);
#else
                             false);
#endif
    (void)offset; /* 窗口客户区坐标 BitBlt，偏移由提交目标窗口决定。 */
}

void XPlatformBackingStoreDriver_presentTile(void* nativeState,
                                             XWindow* window,
                                             const XImage* image,
                                             const XRegion* region,
                                             const XPoint* offset)
{
    (void)nativeState;
#if XPLATFORMNATIVEWINDOW_ON
    if (window && XPlatformNativeWindow_winId(window) != 0)
        XPlatformNativeWindow_present(window, image, region, offset);
#else
    (void)window; (void)image; (void)region; (void)offset;
#endif
}

#endif /* XBACKINGSTORE_ON && XPLATFORMBACKINGSTORE_ON && defined(_WIN32) */
#endif /* XBACKINGSTORE_ON && XPLATFORMBACKINGSTORE_ON */
