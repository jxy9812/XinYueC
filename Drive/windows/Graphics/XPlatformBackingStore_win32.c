/******************************************************************************
 * @file       XPlatformBackingStore_win32.c
 * @brief      Windows XPlatformBackingStore GDI 提交驱动。
 * @details    Windows 后端的全部软件缓冲逻辑（双缓冲 XImage、resize/滚动/
 *             静态内容/tile 遍历/外部缓冲）已上提到公共层
 *             Src/XGui/Platform/XPlatformBackingStore.c；本文件只提供
 *             「把脏区提交到真实窗口」的 Driver 钩子：
 *             - nativeState 持有与缓冲等宽的自顶向下 DIB section
 *               (CreateDIBSection)；表面格式随
 *               XGUI_BACKINGSTORE_IMAGE_FORMAT_RGB16（XGuiConfig 定义，
 *               默认 0）：0=ARGB32（BI_RGB，BGRA 字节序与 XImage 小端
 *               内存布局一致）、1=RGB16 565（BI_BITFIELDS，行内存即 565
 *               编码）；两种模式均与 XImage 行内存逐行直接拷贝，无需
 *               像素转换；
 *             - present：先把脏矩形对应的 XImage 行同步进 DIB，再把每块
 *               脏区经 BitBlt(SRCCOPY) 从内存 DC 合成到目标窗口 DC
 *               （FULL 模式经 SetDIBitsToDevice 整屏上传）；dpr>1 时
 *               同位点改用拉伸原语 StretchBlt/StretchDIBits（目标矩形
 *               ×dpr 放大——BitBlt/SetDIBitsToDevice 仅 1:1，present
 *               唯一放大点，四 blit 位点由 xpbs_win32_blitRectScaled
 *               收拢，scale==1.0 原调用逐位透传），scale 平台内部读
 *               XWindow_devicePixelRatio——
 *               共享契约 present/presentTile/getNativeBuffer 签名零改动；
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
#include "XSystem.h"
#if XPLATFORMNATIVEWINDOW_ON
#include "XPlatformNativeWindow.h"
#include "XWindow.h"
#endif /* XPLATFORMNATIVEWINDOW_ON */
#include <string.h>
#include <windows.h>

#ifndef XGUI_BACKINGSTORE_IMAGE_FORMAT_RGB16
/* XGuiConfig 统一定义，此处兜底供独立编译。 */
#define XGUI_BACKINGSTORE_IMAGE_FORMAT_RGB16 0
#endif

/* 后备缓冲表面每像素字节数：ARGB32=4、RGB16(565)=2。缓冲内像素编码由
   绘制内核负责写入，本层只把字节按对应 bpp 的 DIB 语义交给 GDI，不做
   任何颜色转换（对标 QWindowsBackingStore 的图像格式跟随策略）。 */
#if XGUI_BACKINGSTORE_IMAGE_FORMAT_RGB16
#define XPBS_WIN32_PIXEL_BYTES 2u
/* GDI 对 16bpp DIB 同样按 DWORD 对齐取行；此宏与公共层
   XImageFormat_bytesPerLine(width, XImageFormat_RGB16) 的 4 字节行
   对齐规则一致，保证零拷贝路径两侧行距逐字节吻合。 */
#define XPBS_WIN32_DIB_ROW_STRIDE(pixelWidth) \
    ((((size_t)(pixelWidth) * XPBS_WIN32_PIXEL_BYTES) + 3u) & ~(size_t)3u)
#else
#define XPBS_WIN32_PIXEL_BYTES 4u
#define XPBS_WIN32_DIB_ROW_STRIDE(pixelWidth) \
    ((size_t)(pixelWidth) * XPBS_WIN32_PIXEL_BYTES)
#endif

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
    HWND m_winDcHwnd;                          /**< m_winDC 所属窗口（拥有 DC）。 */
    HDC m_winDC;                               /**< 缓存的目标窗口 DC（拥有）：
                                                    每帧 GetDC/ReleaseDC 实测约
                                                    11µs/帧纯开销，与目标 HWND
                                                    绑定复用，窗口变化时重建。 */
};

/** @brief 释放缓存的目标窗口 DC（可重复调用）。 */
static void xpbs_win32_releaseWinDC(struct XWin32BackingStoreNative* state)
{
    if (!state || !state->m_winDC) return;
    if (state->m_winDcHwnd)
        ReleaseDC(state->m_winDcHwnd, state->m_winDC);
    state->m_winDC = NULL;
    state->m_winDcHwnd = NULL;
}

/** @brief 取（或复用）目标窗口 DC：同 HWND 复用，变化时先释放旧的。 */
static HDC xpbs_win32_windowDC(struct XWin32BackingStoreNative* state,
                               HWND hwnd)
{
    if (!state || !hwnd) return NULL;
    if (state->m_winDC && state->m_winDcHwnd == hwnd)
        return state->m_winDC;
    xpbs_win32_releaseWinDC(state);
    state->m_winDC = GetDC(hwnd);
    state->m_winDcHwnd = state->m_winDC ? hwnd : NULL;
    return state->m_winDC;
}

/** @brief present 段微优化开关（XGPU_PRESENT_MICRO，缺省=开；"0"=逐位
 *         回退）：FULL 模式 presentRegion 与非 native 兼容路径的每帧
 *         GetDC/ReleaseDC 改走按 HWND 缓存 DC（契约同零拷贝路径：随
 *         Driver_destroy 释放，失效 DC 上 GDI 失败不崩溃）。 */
static bool xpbs_present_micro_requested(void)
{
    static int requested = -1;
    if (requested < 0)
    {
        const char* value = XSystem_environment("XGPU_PRESENT_MICRO");
        requested = !(value && *value && value[0] == '0' && value[1] == 0);
    }
    return requested != 0;
}

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
#if XGUI_BACKINGSTORE_IMAGE_FORMAT_RGB16
    /* RGB16(565)：BI_BITFIELDS 要求在 BITMAPINFOHEADER 之后跟随 3 个
       DWORD 掩码，而 BITMAPINFO 自身只有 1 个颜色表槽，故用扩展布局
       装配（成员名与 BITMAPINFO 保持一致，共用下方装配代码）。 */
    struct
    {
        BITMAPINFOHEADER bmiHeader;
        DWORD bmiMasks[3];
    } bmi;
#else
    BITMAPINFO bmi;
#endif
    void* bits = NULL;
    if (!state || w <= 0 || h <= 0) return false;
    memset(&bmi, 0, sizeof(bmi));
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = w;
    bmi.bmiHeader.biHeight = -h; /* 自顶向下：行 0 与 XImage 顶部一致。 */
    bmi.bmiHeader.biPlanes = 1;
#if XGUI_BACKINGSTORE_IMAGE_FORMAT_RGB16
    /* GDI 对 biBitCount=16 缺省按 555 解释，须以 BI_BITFIELDS 显式
       声明 565 掩码；DIB 行内存即 565 字节序，与缓冲逐行直拷。 */
    bmi.bmiHeader.biBitCount = 16;
    bmi.bmiHeader.biCompression = BI_BITFIELDS;
    bmi.bmiMasks[0] = 0xF800u; /* R：高 5 位。 */
    bmi.bmiMasks[1] = 0x07E0u; /* G：中 6 位。 */
    bmi.bmiMasks[2] = 0x001Fu; /* B：低 5 位。 */
    bmi.bmiHeader.biSizeImage =
        (DWORD)XPBS_WIN32_DIB_ROW_STRIDE(w) * (DWORD)h;
#else
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;
#endif
    state->m_dib = CreateDIBSection(NULL, (BITMAPINFO*)&bmi, DIB_RGB_COLORS,
                                    &bits, NULL, 0);
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
        memcpy(dbuf + (int64_t)(rect->y + row) * bpl +
                   (int64_t)rect->x * XPBS_WIN32_PIXEL_BYTES,
               sbuf + (int64_t)(rect->y + row) * bpl +
                   (int64_t)rect->x * XPBS_WIN32_PIXEL_BYTES,
               (size_t)rect->width * XPBS_WIN32_PIXEL_BYTES);
}

/** @brief 换算取整（与 xpwn_dprRound 同式；本文件私有）。 */
static int xpbs_win32_roundScale(float value)
{
    return (int)(value >= 0.0f ? value + 0.5f : value - 0.5f);
}

/** @brief      四 blit 位点收拢（R15）：把一块逻辑脏区提交到目标窗口 DC。
 *  @details    scale==1.0f 时 GDI 调用与既往逐位一致（BitBlt/
 *              SetDIBitsToDevice 1:1 参数原样透传）；dpr>1 时目标原点/
 *              尺寸 ×scale 放大（源矩形保持逻辑坐标）——GDI 的 BitBlt/
 *              SetDIBitsToDevice 仅做同尺寸块拷贝，放大必须走拉伸原语，
 *              故 memDC 路径改 StretchBlt、缓冲路径改 StretchDIBits，
 *              拉伸模式 COLORONCOLOR（最近邻取样，present 唯一放大点
 *              口径；SetStretchBltMode 仅作用于 Stretch 系）。
 *              src 语义随 fromMemDC：true=源内存 DC（零拷贝路径，DIB 与
 *              缓冲同内存）；false=顶下行主序紧凑缓冲（biWidth=rect.width，
 *              行距按 DWORD 对齐公式，与 DIB 行距声明逐字节吻合——整屏
 *              FULL 提交与逐矩形兼容路径共用此声明）。 */
static void xpbs_win32_blitRectScaled(HDC hdc, const XRect* rect, void* src,
                                      bool fromMemDC, float scale)
{
    int dx;
    int dy;
    int dw;
    int dh;
#if XGUI_BACKINGSTORE_IMAGE_FORMAT_RGB16
    struct
    {
        BITMAPINFOHEADER bmiHeader;
        DWORD bmiMasks[3];
    } bmi;
#else
    BITMAPINFO bmi;
#endif
    if (!hdc || !rect || !src || rect->width <= 0 || rect->height <= 0)
        return;
    if (scale == 1.0f) {
        dx = rect->x;
        dy = rect->y;
        dw = rect->width;
        dh = rect->height;
    } else {
        dx = xpbs_win32_roundScale((float)rect->x * scale);
        dy = xpbs_win32_roundScale((float)rect->y * scale);
        dw = xpbs_win32_roundScale((float)rect->width * scale);
        dh = xpbs_win32_roundScale((float)rect->height * scale);
        SetStretchBltMode(hdc, COLORONCOLOR);
    }
    if (fromMemDC) {
        if (scale == 1.0f)
            BitBlt(hdc, dx, dy, dw, dh, (HDC)src, rect->x, rect->y, SRCCOPY);
        else
            StretchBlt(hdc, dx, dy, dw, dh, (HDC)src,
                       rect->x, rect->y, rect->width, rect->height, SRCCOPY);
        return;
    }
    memset(&bmi, 0, sizeof(bmi));
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = rect->width;
    bmi.bmiHeader.biHeight = -rect->height; /* 自顶向下：行 0 为矩形首行。 */
    bmi.bmiHeader.biPlanes = 1;
#if XGUI_BACKINGSTORE_IMAGE_FORMAT_RGB16
    bmi.bmiHeader.biBitCount = 16;
    bmi.bmiHeader.biCompression = BI_BITFIELDS;
    bmi.bmiMasks[0] = 0xF800u; /* R：高 5 位。 */
    bmi.bmiMasks[1] = 0x07E0u; /* G：中 6 位。 */
    bmi.bmiMasks[2] = 0x001Fu; /* B：低 5 位。 */
    bmi.bmiHeader.biSizeImage =
        (DWORD)XPBS_WIN32_DIB_ROW_STRIDE(rect->width) * (DWORD)rect->height;
#else
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;
#endif
    if (scale == 1.0f)
        SetDIBitsToDevice(hdc, dx, dy, (DWORD)dw, (DWORD)dh,
                          0, 0, 0, (UINT)rect->height, src,
                          (BITMAPINFO*)&bmi, DIB_RGB_COLORS);
    else
        StretchDIBits(hdc, dx, dy, dw, dh,
                      0, 0, rect->width, rect->height, src,
                      (BITMAPINFO*)&bmi, DIB_RGB_COLORS, SRCCOPY);
}

/** @brief 从内存 DC 把一帧脏区提交到目标窗口 DC。
 * @details 取得/释放目标 DC 是一次刷新中最昂贵的部分。DIRECT/FULL
 *          模式先同步所有脏矩形，再复用同一个 DC 完成全部 BitBlt，
 *          对齐 LVGL Windows 在最后一次 flush 中一次提交 framebuffer 的
 *          行为。scale=dpr（present 唯一放大点；1.0 逐位透传）。 */
static void xpbs_win32_presentRegion(struct XWin32BackingStoreNative* state,
                                     HWND hwnd, const XRegion* region, bool full,
                                     float scale)
{
    HDC winDC;
#if XGUI_BACKINGSTORE_RENDER_MODE != XGUI_BACKINGSTORE_RENDER_MODE_FULL
    int i;
#endif
    (void)full;
    if (!state || !state->m_memDC || !region || region->count <= 0) return;
    if (!hwnd || !IsWindow(hwnd)) return;
    winDC = xpbs_present_micro_requested()
                ? xpbs_win32_windowDC(state, hwnd) : GetDC(hwnd);
    if (!winDC) return;
#if XGUI_BACKINGSTORE_RENDER_MODE != XGUI_BACKINGSTORE_RENDER_MODE_FULL
    /* PARTIAL/DIRECT 都按脏矩形提交：同步进 DIB 的只有变化区域，
       因此上屏也只 BitBlt 这些矩形。DIRECT 虽然持有整帧双缓冲，
       但每帧只把变化的小块上传到窗口，帧成本与窗口面积无关。 */
    for (i = 0; i < region->count; ++i) {
        const XRect* rect = &region->rects[i];
        if (rect->width > 0 && rect->height > 0)
            xpbs_win32_blitRectScaled(winDC, rect, (void*)state->m_memDC,
                                      true, scale);
    }
#else
    /* FULL 的语义是每次提交整屏：即使脏区很小也整帧上传。 */
    {
        if (state->m_width > 0 && state->m_height > 0 && state->m_dibBits) {
            XRect rect;
            rect.x = 0;
            rect.y = 0;
            rect.width = state->m_width;
            rect.height = state->m_height;
            xpbs_win32_blitRectScaled(winDC, &rect, state->m_dibBits,
                                      false, scale);
        }
    }
#endif
    if (!xpbs_present_micro_requested()) ReleaseDC(hwnd, winDC);
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
    xpbs_win32_releaseWinDC(state);
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

void* XPlatformBackingStoreDriver_getNativeBuffer(void* nativeState,
                                                  int width, int height,
                                                  size_t* outStride)
{
    struct XWin32BackingStoreNative* state =
        (struct XWin32BackingStoreNative*)nativeState;
    if (!state || width <= 0 || height <= 0) return NULL;
    /* 同尺寸直接复用现有 DIB（不重建，指针稳定）；尺寸变化时先释放
       旧面再重建（createSurface 仅在失败路径释放，入口不释放）。 */
    if (!state->m_dibBits || state->m_width != width ||
        state->m_height != height)
    {
        xpbs_win32_releaseSurface(state);
        if (!xpbs_win32_createSurface(state, width, height)) return NULL;
    }
#if XGUI_BACKINGSTORE_IMAGE_FORMAT_RGB16
    /* RGB16：GDI 与 XImageFormat_bytesPerLine(RGB16) 的行距同为 DWORD
       对齐，此处返回取齐后的行距，零拷贝架设时两侧行首才能逐字节
       吻合（ARGB32 的 w*4 天然对齐，无需此步）。 */
    if (outStride) *outStride = XPBS_WIN32_DIB_ROW_STRIDE(width);
#else
    if (outStride) *outStride = (size_t)width * 4u;
#endif
    return state->m_dibBits;
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
    float scale;
    if (!state || !image || !image->m_data || !region || region->count <= 0)
        return;
    /* 目标 DC 只能在真实窗口上取得；无 HWND（离屏/测试）时提交交给
       公共层统一触发的 present 回调。 */
    hwnd = xpbs_win32_targetHwnd(state, window);
    if (!hwnd) return;
    (void)offset; /* 窗口客户区坐标提交，偏移由提交目标窗口决定。 */
#if XPLATFORMNATIVEWINDOW_ON
    /* R1：present 放大 scale 平台内部读取（共享契约签名零改动）；dpr
       单源=XWindow_devicePixelRatio（缓冲恒逻辑分辨率，物理放大全在
       present 层）。 */
    scale = XWindow_devicePixelRatio(window);
#else
    (void)window;
    scale = 1.0f;
#endif
#if XGUI_BACKINGSTORE_RENDER_MODE != XGUI_BACKINGSTORE_RENDER_MODE_FULL
    /* 零拷贝模式：公共层在 resize 时经 getNativeBuffer 把绘制 XImage
       直接架在 DIB 内存上。此处无需任何像素拷贝——按脏矩形把 DIB
       经 memDC BitBlt 到窗口即可。 */
    if (state->m_memDC && state->m_dibBits &&
        state->m_width == XImage_width(image) &&
        state->m_height == XImage_height(image) &&
        XImage_constBits(image) == state->m_dibBits)
    {
        HDC winDC = xpbs_win32_windowDC(state, hwnd); /* 复用缓存 DC */
        int j;
        if (!winDC) return;
        for (j = 0; j < region->count; ++j)
        {
            const XRect* rect = &region->rects[j];
            if (rect->width > 0 && rect->height > 0)
                xpbs_win32_blitRectScaled(winDC, rect, (void*)state->m_memDC,
                                          true, scale);
        }
        /* 上屏只做脏矩形 BitBlt；目标窗口 DC 由 xpbs_win32_windowDC 按
           HWND 缓存复用，不再逐帧 GetDC/ReleaseDC——分段实测这两步合计
           约 11µs/帧（getDC 6.1 + relDC 5.0）的纯 GDI 调用开销，缓存后
           归零。生命周期：随 Driver_destroy 释放；目标窗口变化（重新
           绑定 HWND）时先释放旧 DC 再取新的；窗口销毁后残留的 DC 由
           destroy 兜底回收，BitBlt 对失效 DC 返回失败而不崩溃。 */
        return;
    }
    /* 非 native 缓冲（外部缓冲/兼容路径）：SetDIBitsToDevice（dpr>1 时
       StretchDIBits）直接从 XImage 行内存上屏。注意其源子矩形（XSrc/
       YSrc 与 iStartScan/cScanLines）在负高度 DIB 下语义陷阱多（曾导致
       屏幕整帧垂直错位复制），因此每矩形把脏行拷入紧凑临时缓冲
       （biWidth=rect.width，XSrc=YSrc=0），与既有 xpwn_presentRect
       同构。 */
    {
        HDC winDC = xpbs_present_micro_requested()
                        ? xpbs_win32_windowDC(state, hwnd) : GetDC(hwnd);
        int imgW;
        int imgH;
        int j;
        uint8_t* buf = NULL;
        size_t bufCap = 0;
        if (!winDC) return;
        imgW = XImage_width(image);
        imgH = XImage_height(image);
        for (j = 0; j < region->count; ++j)
        {
            const XRect* rect = &region->rects[j];
            const uint8_t* sbuf;
            int bpl;
            int row;
#if XGUI_BACKINGSTORE_IMAGE_FORMAT_RGB16
            /* RGB16：紧凑临时缓冲同为 565 掩码 DIB（扩展布局）。 */
            struct
            {
                BITMAPINFOHEADER bmiHeader;
                DWORD bmiMasks[3];
            } bmi;
#else
            BITMAPINFO bmi;
#endif
            int x0;
            if (rect->width <= 0 || rect->height <= 0) continue;
            x0 = rect->x - (offset ? offset->x : 0);
            if (rect->x < 0 || rect->y < 0 ||
                rect->x + rect->width > imgW ||
                rect->y + rect->height > imgH)
                continue;
            sbuf = XImage_constBits(image);
            bpl = XImage_bytesPerLine(image);
            if (!sbuf || bpl <= 0) break;
            if (XPBS_WIN32_DIB_ROW_STRIDE(rect->width) *
                (size_t)rect->height > bufCap)
            {
                if (buf) XFree_Hybrid(buf);
                bufCap = XPBS_WIN32_DIB_ROW_STRIDE(rect->width) *
                         (size_t)rect->height;
                buf = (uint8_t*)XMalloc_Hybrid(bufCap);
                if (!buf) break;
            }
            for (row = 0; row < rect->height; ++row)
                memcpy(buf + (size_t)row * XPBS_WIN32_DIB_ROW_STRIDE(rect->width),
                       sbuf + (size_t)(rect->y + row) * (size_t)bpl +
                              (size_t)x0 * XPBS_WIN32_PIXEL_BYTES,
                       (size_t)rect->width * XPBS_WIN32_PIXEL_BYTES);
            xpbs_win32_blitRectScaled(winDC, rect, buf, false, scale);
        }
        if (buf) XFree_Hybrid(buf);
        if (!xpbs_present_micro_requested()) ReleaseDC(hwnd, winDC);
        return;
    }
#endif
    /* FULL 模式保持原有 DIB 中转路径。 */
    {
        /* 懒保护：create 后直接 flush（未经 resize）时 DIB 尚未建立，用当前
           缓冲尺寸补建；正常路径由 surfaceResized 在 resize 时重建。 */
        if (!state->m_memDC)
            xpbs_win32_createSurface(state, XImage_width(image),
                                     XImage_height(image));
        if (!state->m_memDC) return;
        for (i = 0; i < region->count; ++i)
            xpbs_win32_syncDirtyRect(state, image, &region->rects[i]);
    }
    xpbs_win32_presentRegion(state, hwnd, region,
#if XGUI_BACKINGSTORE_RENDER_MODE == XGUI_BACKINGSTORE_RENDER_MODE_FULL
                             true,
#else
                             false,
#endif
                             scale);
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

/* Panel strip fill for the window-decoration drag path (remote 8b092c6d).
 * fbdev-only semantics: on Windows the window system / DWM re-covers
 * exposed regions and the per-rect BitBlt flush is already atomic, so the
 * panel-level fill is a no-op here (same policy as the posix
 * implementation's non-fbdev branch). Windows-side link stub added by the
 * merge integration -- the remote commit only implemented the posix side. */
void XPlatformBackingStore_fillPanelRects(const XRect* rects, int count,
                                          uint32_t nativePixel)
{
    (void)rects;
    (void)count;
    (void)nativePixel;
}

#endif /* XBACKINGSTORE_ON && XPLATFORMBACKINGSTORE_ON && defined(_WIN32) */
#endif /* XBACKINGSTORE_ON && XPLATFORMBACKINGSTORE_ON */
