/******************************************************************************
 * @file       XPlatformBackingStore_win32.c
 * @brief      Windows XPlatformBackingStore GDI 后备存储后端。
 * @details    本文件是 XBackingStore 公共类的 Windows 平台后端：绘制设备
 *             仍是进程内 XImage 软件缓冲（ARGB32 预乘 Alpha，与
 *             Linux 后端一致），平台差异集中在 flush 提交点：
 *             - resize 时在系统堆创建与该缓冲等宽的 32 位自顶向下 DIB
 *               (CreateDIBSection)；DIB 的 BGRA 字节序与 XImage ARGB32
 *               小端内存布局一致，可逐行直接拷贝，无需像素转换；
 *             - flush 时先把脏矩形对应的 XImage 行同步进 DIB，再把每块
 *               脏区经 BitBlt(SRCCOPY) 从内存 DC 合成到目标窗口 DC；
 *             - 目标窗口句柄（HWND）经由平台扩展
 *               XPlatformBackingStore_setNativeTargetWindow 登记；未登记
 *               时与软件后端一致，只做 DIB 同步并触发 present 回调，便于
 *               离屏/测试环境使用。
 *             resize/滚动/静态内容语义与 Linux 后端完全一致，全部不依赖
 *             Win32 API。本文件只在 _WIN32 下编译，公共层不包含本头外的
 *             任何 Windows 类型。
 */

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

/** @brief 后备缓冲默认像素格式（对标 Qt 栅格后备存储的 ARGB32 预乘）。 */
#define XPBS_WIN32_IMAGE_FORMAT XImageFormat_ARGB32_Premultiplied

/** @brief Windows 后端私有句柄。 */
struct XPlatformBackingStore
{
    XWindow* m_window;                        /**< 绑定窗口（借用，不持有）。 */
    XImage m_image;                           /**< 内部软件帧缓冲（拥有）。 */
#if XGUI_BACKINGSTORE_BUFFER_COUNT > 1
    XImage m_image2;                          /**< 双缓冲的第二帧缓冲。 */
#endif
    unsigned m_activeIndex;                   /**< 当前绘制/提交缓冲索引。 */
    void* m_buffer1;                           /**< 外部第一块缓冲（借用）。 */
    void* m_buffer2;                           /**< 外部第二块缓冲（借用）。 */
    size_t m_bufferSize;                       /**< 外部每块缓冲容量。 */
    bool m_externalBuffers;                    /**< 是否使用外部缓冲。 */
    bool m_buffersInitialized;                 /**< 外部绑定是否已完成一次。 */
    XSize m_size;                             /**< 当前缓冲尺寸。 */
    XRegion m_staticContents;                 /**< 静态内容区域集合。 */
    XRegion m_paintRegion;                    /**< beginPaint 登记的绘制区。 */
    XRegion m_flushRegion;                    /**< flush 使用的可复用提交区域。 */
    int m_tileCursorX;                        /**< 下一个 tile 的 X 网格坐标。 */
    int m_tileCursorY;                        /**< 下一个 tile 的 Y 网格坐标。 */
    XRect m_currentTile;                      /**< 当前 tile 的窗口矩形。 */
    XPoint m_paintOrigin;                     /**< 当前 tile 原点。 */
    bool m_tileActive;                        /**< 当前是否存在可提交 tile。 */
    XPlatformBackingStorePresentFn m_present; /**< present 回调（借用）。 */
    void* m_userData;                         /**< present 回调用户数据（借用）。 */
    void* m_nativeTarget;                     /**< 原生目标窗口（HWND，借用）。 */
    unsigned m_beginPaintActive;              /**< beginPaint/endPaint 区间标志。 */
    HDC m_memDC;                              /**< 内存 DC（拥有）。 */
    HBITMAP m_dib;                            /**< DIB section 位图（拥有）。 */
    HBITMAP m_oldBitmap;                      /**< memDC 中原位图（还原用）。 */
    uint8_t* m_dibBits;                       /**< DIB section 系统堆指针（借用自 m_dib）。 */
};

/* ==================== 内部工具 ==================== */

/** @brief 把矩形裁剪到图像范围；空矩形返回 false。 */
static bool xpbs_win32_clipRect(const XRect* rect, int w, int h, XRect* out)
{
    int x0, y0, x1, y1;
    if (!rect || !out) return false;
    if (rect->width <= 0 || rect->height <= 0) return false;
    x0 = rect->x;                    y0 = rect->y;
    x1 = rect->x + rect->width;      y1 = rect->y + rect->height;
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 > w) x1 = w;
    if (y1 > h) y1 = h;
    if (x1 <= x0 || y1 <= y0) return false;
    out->x = x0; out->y = y0;
    out->width = x1 - x0; out->height = y1 - y0;
    return true;
}

/** @brief 求两个矩形交集；为空时返回 false。 */
static bool xpbs_win32_intersect(const XRect* a, const XRect* b, XRect* out)
{
    int x0, y0, x1, y1;
    if (!a || !b || !out) return false;
    x0 = a->x > b->x ? a->x : b->x;
    y0 = a->y > b->y ? a->y : b->y;
    x1 = (a->x + a->width)  < (b->x + b->width)  ? (a->x + a->width)  : (b->x + b->width);
    y1 = (a->y + a->height) < (b->y + b->height) ? (a->y + a->height) : (b->y + b->height);
    if (x1 <= x0 || y1 <= y0) return false;
    out->x = x0; out->y = y0;
    out->width = x1 - x0; out->height = y1 - y0;
    return true;
}

/** @brief 按行复制像素矩形（4 字节/像素，ARGB32 小端与 DIB BGRA 一致）。 */
static void xpbs_win32_copyRectPixels(const XImage* src, int sx, int sy,
                                      XImage* dst, int dx, int dy,
                                      int w, int h)
{
    const uint8_t* sbuf;
    uint8_t* dbuf;
    int bpl;
    int row;
    if (!src || !dst || w <= 0 || h <= 0) return;
    sbuf = XImage_constBits(src);
    dbuf = XImage_bits(dst);
    bpl = XImage_bytesPerLine(src);
    if (!sbuf || !dbuf || bpl <= 0) return;
    if (XImage_bytesPerLine(dst) != bpl) return;
    for (row = 0; row < h; ++row)
        memmove(dbuf + (int64_t)(dy + row) * bpl + (int64_t)dx * 4,
                sbuf + (int64_t)(sy + row) * bpl + (int64_t)sx * 4,
                (size_t)w * 4u);
}

/** @brief 深拷贝图像（XCopy 为共享引用，不能用）。 */
static bool xpbs_win32_deepCopy(const XImage* src, XImage* dst)
{
    const uint8_t* sbuf;
    uint8_t* dbuf;
    int w, h, bpl, row;
    if (!src || !dst || !src->m_data) return false;
    w = XImage_width(src);
    h = XImage_height(src);
    /* dst 可能已经持有图像数据（toImage 可反复复用同一输出）；init_ex
       只用于首次初始化，不能覆盖已有对象。reinit_ex 先构造临时图像再
       移动替换，释放旧像素并保留对象的内存方法/堆所有权标记。 */
    if (!XImage_reinit_ex(dst, w, h, XImage_format(src)))
        return false;
    sbuf = XImage_constBits(src);
    dbuf = XImage_bits(dst);
    bpl = XImage_bytesPerLine(src);
    if (!sbuf || !dbuf || bpl <= 0 || XImage_bytesPerLine(dst) != bpl) return false;
    for (row = 0; row < h; ++row)
        memcpy(dbuf + (int64_t)row * bpl, sbuf + (int64_t)row * bpl, (size_t)bpl);
    return true;
}

/**
 * @brief 把集合裁剪到图像范围。
 * @note out 必须已经通过 XRegion_init() 初始化；函数只清空元素并复用
 *       其已有容量，适合 beginPaint() 的高频调用。
 */
static void xpbs_win32_clipRegion(const XRegion* region, int w, int h,
                                  XRegion* out)
{
    int i;
    XRect clip;
    XRegion_clear(out);
    if (!region) return;
    for (i = 0; i < region->count; ++i)
    {
        if (xpbs_win32_clipRect(&region->rects[i], w, h, &clip))
            XRegion_addRect(out, &clip);
    }
}

/**
 * @brief 调用 present 回调并隔离回调期间的区域生命周期。
 * @details m_flushRegion 只属于当前 flush 调用，回调可能重入同一个
 *          后备存储并复用该区域。因此回调收到的是本次提交的深拷贝，
 *          仅保证在回调返回前有效，回调不得保存该指针。
 */
static void xpbs_win32_invokePresent(XPlatformBackingStore* self,
                                     const XRegion* region,
                                     const XPoint* offset)
{
    XRegion callbackRegion;
    if (!self || !self->m_present || !region || XRegion_isEmpty(region))
        return;
    XRegion_init(&callbackRegion);
    XRegion_copy(region, &callbackRegion);
    if (callbackRegion.count == region->count)
        self->m_present(self->m_userData, self, &callbackRegion, offset);
    XRegion_deinit(&callbackRegion);
}

static XImage* xpbs_win32_activeImage(XPlatformBackingStore* self)
{
    if (!self) return NULL;
#if XGUI_BACKINGSTORE_BUFFER_COUNT > 1
    return self->m_activeIndex == 0u ? &self->m_image : &self->m_image2;
#else
    return &self->m_image;
#endif
}

static XImage* xpbs_win32_inactiveImage(XPlatformBackingStore* self)
{
#if XGUI_BACKINGSTORE_BUFFER_COUNT > 1
    if (!self) return NULL;
    return self->m_activeIndex == 0u ? &self->m_image2 : &self->m_image;
#else
    (void)self;
    return NULL;
#endif
}

static void xpbs_win32_syncImage(const XImage* source, XImage* target)
{
    int width;
    int height;
    const uint8_t* sbuf;
    uint8_t* dbuf;
    int bpl;
    int row;
    if (!source || !target || !source->m_data || !target->m_data) return;
    width = XImage_width(source);
    height = XImage_height(source);
    if (width <= 0 || height <= 0 || width != XImage_width(target) ||
        height != XImage_height(target)) return;
    sbuf = XImage_constBits(source);
    dbuf = XImage_bits(target);
    bpl = XImage_bytesPerLine(source);
    if (!sbuf || !dbuf || bpl <= 0 || XImage_bytesPerLine(target) != bpl)
        return;
    for (row = 0; row < height; ++row)
        memcpy(dbuf + (int64_t)row * bpl, sbuf + (int64_t)row * bpl,
               (size_t)bpl);
}

static bool xpbs_win32_bufferSizeValid(const XSize* size, size_t bufferSize)
{
    size_t required = XPlatformBackingStore_requiredBufferSize(size);
    return required == 0 ? (!size || size->width <= 0 || size->height <= 0)
                         : bufferSize >= required;
}

size_t XPlatformBackingStore_requiredBufferSize(const XSize* size)
{
    size_t stride;
    if (!size || size->width <= 0 || size->height <= 0) return 0;
#if XGUI_BACKINGSTORE_RENDER_MODE == XGUI_BACKINGSTORE_RENDER_MODE_PARTIAL
    {
        int bw = size->width < XGUI_BACKINGSTORE_PARTIAL_BUFFER_WIDTH ?
                 size->width : XGUI_BACKINGSTORE_PARTIAL_BUFFER_WIDTH;
        int bh = size->height < XGUI_BACKINGSTORE_PARTIAL_BUFFER_HEIGHT ?
                 size->height : XGUI_BACKINGSTORE_PARTIAL_BUFFER_HEIGHT;
        stride = (size_t)XImageFormat_bytesPerLine(bw, XPBS_WIN32_IMAGE_FORMAT);
        if (stride == 0 || (size_t)bh > SIZE_MAX / stride) return 0;
        return stride * (size_t)bh;
    }
#else
    stride = (size_t)XImageFormat_bytesPerLine(size->width,
                                                XPBS_WIN32_IMAGE_FORMAT);
    if (stride == 0 || (size_t)size->height > SIZE_MAX / stride) return 0;
    return stride * (size_t)size->height;
#endif
}

static void xpbs_win32_initConfiguredImage(XImage* image, int width, int height,
                                             void* buffer, size_t bufferSize)
{
    int stride;
    if (!image) return;
    if (width <= 0 || height <= 0)
    {
        /* deinit 释放旧像素但保留对象的虚表与堆所有权标记；不能让
           resize(0, 0) 把旧数据留在对象里。 */
        XImage_deinit_base(image);
        return;
    }
    if (buffer)
    {
        stride = XImageFormat_bytesPerLine(width, XPBS_WIN32_IMAGE_FORMAT);
        XImage_init_ex_2(image, width, height, XPBS_WIN32_IMAGE_FORMAT,
                         stride, (uint8_t*)buffer, NULL, NULL);
        (void)bufferSize;
    }
    else
        XImage_init_ex(image, width, height, XPBS_WIN32_IMAGE_FORMAT);
}

/** @brief 用指定颜色填充集合内的全部矩形。 */
static void xpbs_win32_fillRegion(XImage* image, const XRegion* region,
                                  uint32_t color)
{
    int i;
    if (!image || !region) return;
    for (i = 0; i < region->count; ++i)
        XImage_fillRect(image, &region->rects[i], color);
}

/** @brief 释放内存 DC 与 DIB section（失败路径也保持安全，可重复调用）。 */
static void xpbs_win32_releaseSurface(struct XPlatformBackingStore* store)
{
    if (!store) return;
    if (store->m_memDC && store->m_oldBitmap)
    {
        SelectObject(store->m_memDC, store->m_oldBitmap);
        store->m_oldBitmap = NULL;
    }
    if (store->m_dib)
    {
        DeleteObject(store->m_dib);
        store->m_dib = NULL;
    }
    if (store->m_memDC)
    {
        DeleteDC(store->m_memDC);
        store->m_memDC = NULL;
    }
    store->m_dibBits = NULL;
}

/** @brief 按当前缓冲尺寸创建 DIB section 与内存 DC。 */
static bool xpbs_win32_createSurface(struct XPlatformBackingStore* store)
{
    BITMAPINFO bmi;
    void* bits = NULL;
    int w, h;
    if (!store || !store->m_image.m_data) return false;
    w = XImage_width(&store->m_image);
    h = XImage_height(&store->m_image);
    if (w <= 0 || h <= 0) return false;
    memset(&bmi, 0, sizeof(bmi));
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = w;
    bmi.bmiHeader.biHeight = -h; /* 自顶向下：行 0 与 XImage 顶部一致。 */
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;
    store->m_dib = CreateDIBSection(NULL, &bmi, DIB_RGB_COLORS, &bits, NULL, 0);
    if (!store->m_dib || !bits) goto fail;
    store->m_memDC = CreateCompatibleDC(NULL);
    if (!store->m_memDC) goto fail;
    store->m_oldBitmap = (HBITMAP)SelectObject(store->m_memDC, store->m_dib);
    if (!store->m_oldBitmap) goto fail;
    store->m_dibBits = (uint8_t*)bits;
    return true;
fail:
    xpbs_win32_releaseSurface(store);
    return false;
}

/** @brief 把脏矩形的 XImage 行同步进 DIB（两缓冲字节序一致，逐行拷贝）。 */
static void xpbs_win32_syncDirtyRect(struct XPlatformBackingStore* store,
                                     const XImage* image, const XRect* rect)
{
    const uint8_t* sbuf;
    uint8_t* dbuf;
    int bpl;
    int row;
    if (!store || !store->m_dibBits || !rect || !image || !image->m_data) return;
    sbuf = XImage_constBits(image);
    dbuf = store->m_dibBits;
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
static void xpbs_win32_presentRegion(struct XPlatformBackingStore* store,
                                     const XRegion* region)
{
    HWND hwnd;
    HDC winDC;
#if XGUI_BACKINGSTORE_RENDER_MODE != XGUI_BACKINGSTORE_RENDER_MODE_FULL
    int i;
#endif
    if (!store || !store->m_memDC || !region || region->count <= 0) return;
    hwnd = (HWND)store->m_nativeTarget;
#if XPLATFORMNATIVEWINDOW_ON
    /* 兜底：未显式登记原生目标（setNativeTargetWindow）时，若窗口已挂接
       XPlatformNativeWindow 真实 HWND，则从平台注册表取回提交目标。 */
    if (!hwnd && store->m_window)
        hwnd = (HWND)(uintptr_t)XPlatformNativeWindow_winId(
                  (XWindow*)store->m_window);
#endif /* XPLATFORMNATIVEWINDOW_ON */
    if (!hwnd || !IsWindow(hwnd)) return;
    winDC = GetDC(hwnd);
    if (!winDC) return;
#if XGUI_BACKINGSTORE_RENDER_MODE != XGUI_BACKINGSTORE_RENDER_MODE_FULL
    /* PARTIAL/DIRECT 都按脏矩形提交：同步进 DIB 的只有变化区域，
       因此上屏也只 BitBlt 这些矩形。DIRECT 虽然持有整帧双缓冲，
       但每帧只把变化的小块上传到窗口，帧成本与窗口面积无关。 */
    for (i = 0; i < region->count; ++i) {
        const XRect* rect = &region->rects[i];
        if (rect->width > 0 && rect->height > 0)
            BitBlt(winDC, rect->x, rect->y, rect->width, rect->height,
                   store->m_memDC, rect->x, rect->y, SRCCOPY);
    }
#else
    /* FULL 的语义是每次提交整屏：即使脏区很小也整帧上传。 */
    {
        BITMAPINFO bmi;
        int width = store->m_size.width;
        int height = store->m_size.height;
        if (width > 0 && height > 0 && store->m_dibBits) {
            memset(&bmi, 0, sizeof(bmi));
            bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
            bmi.bmiHeader.biWidth = width;
            bmi.bmiHeader.biHeight = -height;
            bmi.bmiHeader.biPlanes = 1;
            bmi.bmiHeader.biBitCount = 32;
            bmi.bmiHeader.biCompression = BI_RGB;
            SetDIBitsToDevice(winDC, 0, 0, (DWORD)width, (DWORD)height,
                              0, 0, 0, (UINT)height, store->m_dibBits,
                              &bmi, DIB_RGB_COLORS);
        }
    }
#endif
    ReleaseDC(hwnd, winDC);
}

/* ==================== 生命周期（平台后端提供） ==================== */

XPlatformBackingStore* XPlatformBackingStore_create(XWindow* window)
{
    XPlatformBackingStore* store;
    store = (XPlatformBackingStore*)XCalloc_System(1u, sizeof(XPlatformBackingStore));
    if (!store) return NULL;
    store->m_window = window;
    XImage_init(&store->m_image);
#if XGUI_BACKINGSTORE_BUFFER_COUNT > 1
    XImage_init(&store->m_image2);
#endif
    XRegion_init(&store->m_staticContents);
    XRegion_init(&store->m_paintRegion);
    XRegion_init(&store->m_flushRegion);
    XSize_init(&store->m_size, 0, 0);
#if XGUI_BACKINGSTORE_BUFFER_SIZE > 0
    if (!XPlatformBackingStore_setBuffers(
            store, (void*)XGUI_BACKINGSTORE_BUFFER1,
            (void*)XGUI_BACKINGSTORE_BUFFER2,
            (size_t)XGUI_BACKINGSTORE_BUFFER_SIZE))
    {
        XPlatformBackingStore_delete(store);
        return NULL;
    }
#endif
    return store;
}

void XPlatformBackingStore_delete(XPlatformBackingStore* self)
{
    if (!self) return;
    xpbs_win32_releaseSurface(self);
    if (self->m_image.m_data)
        XImage_deinit_base(&self->m_image);
#if XGUI_BACKINGSTORE_BUFFER_COUNT > 1
    if (self->m_image2.m_data)
        XImage_deinit_base(&self->m_image2);
#endif
    XRegion_deinit(&self->m_staticContents);
    XRegion_deinit(&self->m_paintRegion);
    XRegion_deinit(&self->m_flushRegion);
    XFree_System(self);
}

/* ==================== 访问器 ==================== */

XWindow* XPlatformBackingStore_window(const XPlatformBackingStore* self)
{
    return self ? self->m_window : NULL;
}

XImage* XPlatformBackingStore_paintDevice(XPlatformBackingStore* self)
{
    XImage* image = xpbs_win32_activeImage(self);
    return (image && image->m_data) ? image : NULL;
}

bool XPlatformBackingStore_nextTile(XPlatformBackingStore* self,
                                    XRect* tileRect)
{
    XImage* image;
#if XGUI_BACKINGSTORE_RENDER_MODE == XGUI_BACKINGSTORE_RENDER_MODE_PARTIAL
    int tileW, tileH, x, y, i;
#endif
    if (!self || !tileRect || !self->m_beginPaintActive) return false;
    image = xpbs_win32_activeImage(self);
    if (!image || !image->m_data) return false;
#if XGUI_BACKINGSTORE_RENDER_MODE == XGUI_BACKINGSTORE_RENDER_MODE_PARTIAL
    tileW = XGUI_BACKINGSTORE_PARTIAL_BUFFER_WIDTH;
    tileH = XGUI_BACKINGSTORE_PARTIAL_BUFFER_HEIGHT;
    while (self->m_tileCursorY < self->m_size.height) {
        x = self->m_tileCursorX;
        y = self->m_tileCursorY;
        self->m_tileCursorX += tileW;
        if (self->m_tileCursorX >= self->m_size.width) {
            self->m_tileCursorX = 0;
            self->m_tileCursorY += tileH;
        }
        tileRect->x = x; tileRect->y = y;
        tileRect->width = (x + tileW <= self->m_size.width) ? tileW : self->m_size.width - x;
        tileRect->height = (y + tileH <= self->m_size.height) ? tileH : self->m_size.height - y;
        for (i = 0; i < self->m_paintRegion.count; ++i) {
            XRect hit;
            if (xpbs_win32_intersect(tileRect, &self->m_paintRegion.rects[i], &hit)) {
                XImage_fill(image, 0u);
                self->m_currentTile = *tileRect;
                self->m_paintOrigin.x = x; self->m_paintOrigin.y = y;
                self->m_tileActive = true;
                return true;
            }
        }
    }
    return false;
#else
    if (self->m_tileActive || self->m_size.width <= 0 || self->m_size.height <= 0)
        return false;
    tileRect->x = 0; tileRect->y = 0;
    tileRect->width = self->m_size.width; tileRect->height = self->m_size.height;
    self->m_currentTile = *tileRect;
    self->m_paintOrigin.x = 0; self->m_paintOrigin.y = 0;
    self->m_tileActive = true;
    return true;
#endif
}

XPoint XPlatformBackingStore_paintOrigin(const XPlatformBackingStore* self)
{
    XPoint out;
    XPoint_init(&out, 0, 0);
    if (self) out = self->m_paintOrigin;
    return out;
}

bool XPlatformBackingStore_toImage(XPlatformBackingStore* self, XImage* out)
{
    if (!self || !out)
        return false;
    return xpbs_win32_deepCopy(xpbs_win32_activeImage(self), out);
}

bool XPlatformBackingStore_setBuffers(XPlatformBackingStore* self,
                                       void* buffer1, void* buffer2,
                                       size_t bufferSize)
{
    XSize size;
    bool clear;
    if (!self) return false;
    if (self->m_buffersInitialized)
        return self->m_buffer1 == buffer1 && self->m_buffer2 == buffer2 &&
               self->m_bufferSize == bufferSize;
    clear = !buffer1 && !buffer2 && bufferSize == 0;
    if (clear)
        return true;
    if (!clear)
    {
        if (!buffer1 || bufferSize == 0 ||
#if XGUI_BACKINGSTORE_BUFFER_COUNT > 1
            !buffer2 ||
#else
            buffer2 ||
#endif
            !xpbs_win32_bufferSizeValid(&self->m_size, bufferSize))
            return false;
    }
    self->m_buffer1 = clear ? NULL : buffer1;
    self->m_buffer2 = clear ? NULL : buffer2;
    self->m_bufferSize = clear ? 0 : bufferSize;
    self->m_externalBuffers = !clear;
    self->m_buffersInitialized = true;
    size = self->m_size;
    self->m_size.width = -1;
    XPlatformBackingStore_resize(self, &size);
    return true;
}

/* ==================== 绘制流程 ==================== */

void XPlatformBackingStore_flush(XPlatformBackingStore* self, XWindow* window,
                                 const XRegion* region, const XPoint* offset)
{
    XRect full;
    XPoint zero;
    const XPoint* off = offset;
    XImage* image;
    XImage* inactive;
    int i;
    if (!self || !(image = xpbs_win32_activeImage(self)) || !image->m_data)
        return;
    XRegion_clear(&self->m_flushRegion);
    if (!off)
    {
        XPoint_init(&zero, 0, 0);
        off = &zero;
    }
    if (region && !XRegion_isEmpty(region))
    {
        xpbs_win32_clipRegion(region, XImage_width(image),
                              XImage_height(image), &self->m_flushRegion);
    }
    else
    {
        full.x = 0; full.y = 0;
        full.width = XImage_width(image);
        full.height = XImage_height(image);
        if (full.width > 0 && full.height > 0)
            XRegion_addRect(&self->m_flushRegion, &full);
    }
    /* FULL 始终提交整屏；DIRECT 在提交后同步另一帧缓冲。 */
#if XGUI_BACKINGSTORE_RENDER_MODE == XGUI_BACKINGSTORE_RENDER_MODE_FULL
    XRegion_clear(&self->m_flushRegion);
    full.x = 0; full.y = 0;
    full.width = XImage_width(image); full.height = XImage_height(image);
    if (full.width > 0 && full.height > 0)
        XRegion_addRect(&self->m_flushRegion, &full);
#endif
    inactive = xpbs_win32_inactiveImage(self);
    for (i = 0; i < self->m_flushRegion.count; ++i)
    {
        xpbs_win32_syncDirtyRect(self, image, &self->m_flushRegion.rects[i]);
#if XGUI_BACKINGSTORE_RENDER_MODE == XGUI_BACKINGSTORE_RENDER_MODE_DIRECT || \
    XGUI_BACKINGSTORE_RENDER_MODE == XGUI_BACKINGSTORE_RENDER_MODE_PARTIAL
        if (inactive)
            xpbs_win32_copyRectPixels(image, self->m_flushRegion.rects[i].x,
                                      self->m_flushRegion.rects[i].y, inactive,
                                      self->m_flushRegion.rects[i].x,
                                      self->m_flushRegion.rects[i].y,
                                      self->m_flushRegion.rects[i].width,
                                      self->m_flushRegion.rects[i].height);
#endif
    }
    /* Win32 uses the persistent DIB/memory DC for the native target. Keep the
     * submit outside the rectangle loop so a frame acquires one target DC. */
    xpbs_win32_presentRegion(self, &self->m_flushRegion);
    xpbs_win32_invokePresent(self, &self->m_flushRegion, off);
 #if XGUI_BACKINGSTORE_BUFFER_COUNT > 1
    self->m_activeIndex ^= 1u;
 #endif
    (void)window; /* 目标窗口由 setNativeTargetWindow 登记，参数保持 Qt 签名。 */
}

void XPlatformBackingStore_flushTile(XPlatformBackingStore* self, XWindow* window,
                                      const XRect* tileRect, const XPoint* offset)
{
    XPoint origin;
    XRect presentedRect;
    XImage* image;
    if (!self || !tileRect || !(image = xpbs_win32_activeImage(self)) ||
        !image->m_data || tileRect->width <= 0 || tileRect->height <= 0)
        return;
    origin.x = tileRect->x;
    origin.y = tileRect->y;
    if (offset) { origin.x += offset->x; origin.y += offset->y; }
    /* tile image 的绘制坐标始终从 (0, 0) 起，而 presentedRect 用目标
       窗口坐标表达。XPlatformNativeWindow_present 通过 origin 将二者映射，
       因此不能把 tileRect 直接作为源图像区域传入。 */
    presentedRect.x = origin.x;
    presentedRect.y = origin.y;
    presentedRect.width = tileRect->width;
    presentedRect.height = tileRect->height;
    XRegion_clear(&self->m_flushRegion);
    XRegion_addRect(&self->m_flushRegion, &presentedRect);
#if XPLATFORMNATIVEWINDOW_ON
    if (window && XPlatformNativeWindow_winId(window) != 0)
        XPlatformNativeWindow_present(window, image, &self->m_flushRegion, &origin);
    else
#endif
    xpbs_win32_invokePresent(self, &self->m_flushRegion, &origin);
    self->m_tileActive = false;
#if XGUI_BACKINGSTORE_BUFFER_COUNT > 1
    self->m_activeIndex ^= 1u;
#endif
}

void XPlatformBackingStore_resize(XPlatformBackingStore* self, const XSize* size)
{
    XImage oldImage;
    XImage newImage;
    XImage* active;
#if XGUI_BACKINGSTORE_RENDER_MODE != XGUI_BACKINGSTORE_RENDER_MODE_PARTIAL
    XRect oldRect;
    XRect newRect;
    XRect overlap;
#endif
#if XGUI_BACKINGSTORE_BUFFER_COUNT > 1
    XImage newImage2;
#endif
    XRegion cropped;
    int ow, oh, w, h;
    if (!self || !size) return;
    w = size->width;
    h = size->height;
    if (w < 0 || h < 0) return;
    if (self->m_externalBuffers &&
        !xpbs_win32_bufferSizeValid(size, self->m_bufferSize))
        return;
    if (w == self->m_size.width && h == self->m_size.height) return;
    active = xpbs_win32_activeImage(self);
    /* 先快照旧内容，重建 XImage 缓冲，再同步重建 GDI 表面。 */
    XImage_init(&oldImage);
    if (active && active->m_data)
        XCopy(&oldImage, active);
    ow = XImage_width(&oldImage);
    oh = XImage_height(&oldImage);
    XImage_init(&newImage);
#if XGUI_BACKINGSTORE_BUFFER_COUNT > 1
    XImage_init(&newImage2);
#endif
    if (w > 0 && h > 0)
#if XGUI_BACKINGSTORE_RENDER_MODE == XGUI_BACKINGSTORE_RENDER_MODE_PARTIAL
        xpbs_win32_initConfiguredImage(&newImage,
            w < XGUI_BACKINGSTORE_PARTIAL_BUFFER_WIDTH ? w : XGUI_BACKINGSTORE_PARTIAL_BUFFER_WIDTH,
            h < XGUI_BACKINGSTORE_PARTIAL_BUFFER_HEIGHT ? h : XGUI_BACKINGSTORE_PARTIAL_BUFFER_HEIGHT,
            self->m_externalBuffers ? self->m_buffer1 : NULL, self->m_bufferSize);
#else
        xpbs_win32_initConfiguredImage(&newImage, w, h,
            self->m_externalBuffers ? self->m_buffer1 : NULL, self->m_bufferSize);
#endif
#if XGUI_BACKINGSTORE_BUFFER_COUNT > 1
    if (w > 0 && h > 0)
#if XGUI_BACKINGSTORE_RENDER_MODE == XGUI_BACKINGSTORE_RENDER_MODE_PARTIAL
        xpbs_win32_initConfiguredImage(&newImage2,
            w < XGUI_BACKINGSTORE_PARTIAL_BUFFER_WIDTH ? w : XGUI_BACKINGSTORE_PARTIAL_BUFFER_WIDTH,
            h < XGUI_BACKINGSTORE_PARTIAL_BUFFER_HEIGHT ? h : XGUI_BACKINGSTORE_PARTIAL_BUFFER_HEIGHT,
            self->m_externalBuffers ? self->m_buffer2 : NULL, self->m_bufferSize);
#else
        xpbs_win32_initConfiguredImage(&newImage2, w, h,
            self->m_externalBuffers ? self->m_buffer2 : NULL, self->m_bufferSize);
#endif
#endif
    if ((w > 0 && h > 0 && !newImage.m_data)
#if XGUI_BACKINGSTORE_BUFFER_COUNT > 1
        || (w > 0 && h > 0 && !newImage2.m_data)
#endif
       )
    {
        XImage_deinit_base(&newImage);
#if XGUI_BACKINGSTORE_BUFFER_COUNT > 1
        XImage_deinit_base(&newImage2);
#endif
        XImage_deinit_base(&oldImage);
        return;
    }
#if XGUI_BACKINGSTORE_RENDER_MODE != XGUI_BACKINGSTORE_RENDER_MODE_PARTIAL
    if (oldImage.m_data && newImage.m_data)
    {
        oldRect.x = 0; oldRect.y = 0; oldRect.width = ow; oldRect.height = oh;
        newRect.x = 0; newRect.y = 0; newRect.width = w;  newRect.height = h;
        if (xpbs_win32_intersect(&oldRect, &newRect, &overlap))
        {
            xpbs_win32_copyRectPixels(&oldImage, 0, 0, &newImage,
                                      0, 0, overlap.width, overlap.height);
#if XGUI_BACKINGSTORE_BUFFER_COUNT > 1
            xpbs_win32_copyRectPixels(&oldImage, 0, 0, &newImage2,
                                      0, 0, overlap.width, overlap.height);
#endif
        }
    }
#endif
    XImage_deinit_base(&oldImage);
    XImage_deinit_base(&self->m_image);
    XMove(&self->m_image, &newImage);
#if XGUI_BACKINGSTORE_BUFFER_COUNT > 1
    XImage_deinit_base(&self->m_image2);
    XMove(&self->m_image2, &newImage2);
#endif
    self->m_size.width = w;
    self->m_size.height = h;
    self->m_activeIndex = 0u;
    self->m_tileCursorX = 0;
    self->m_tileCursorY = 0;
    self->m_tileActive = false;
    XRegion_clear(&self->m_flushRegion);
    xpbs_win32_releaseSurface(self);
    if (w > 0 && h > 0)
        xpbs_win32_createSurface(self);
    XRegion_init(&cropped);
    xpbs_win32_clipRegion(&self->m_staticContents, w, h, &cropped);
    XRegion_copy(&cropped, &self->m_staticContents);
    XRegion_deinit(&cropped);
}

bool XPlatformBackingStore_scroll(XPlatformBackingStore* self,
                                  const XRegion* area, int dx, int dy)
{
    XImage* image;
    XImage* inactive;
    XRegion clip;
    XRegion dest;
    XRegion vacated;
    XImage snapshot;
    int w, h, i;
    XRect dstRect;
    if (!self || !(image = xpbs_win32_activeImage(self)) || !image->m_data)
        return false;
#if XGUI_BACKINGSTORE_RENDER_MODE == XGUI_BACKINGSTORE_RENDER_MODE_PARTIAL
    if (self->m_size.width > XImage_width(image) ||
        self->m_size.height > XImage_height(image))
        return false;
#endif
    inactive = xpbs_win32_inactiveImage(self);
    if (!area || XRegion_isEmpty(area)) return false;
    if (dx == 0 && dy == 0) return true;
    w = XImage_width(image);
    h = XImage_height(image);
    if (w <= 0 || h <= 0) return false;
    XRegion_init(&clip);
    xpbs_win32_clipRegion(area, w, h, &clip);
    if (XRegion_isEmpty(&clip))
    {
        XRegion_deinit(&clip);
        return true;
    }
    XRegion_init(&dest);
    for (i = 0; i < clip.count; ++i)
    {
        dstRect.x = clip.rects[i].x + dx;
        dstRect.y = clip.rects[i].y + dy;
        dstRect.width = clip.rects[i].width;
        dstRect.height = clip.rects[i].height;
        if (xpbs_win32_clipRect(&dstRect, w, h, &dstRect))
            XRegion_addRect(&dest, &dstRect);
    }
    XRegion_init(&vacated);
    XRegion_subtracted(&clip, &dest, &vacated);
    XImage_init(&snapshot);
    if (xpbs_win32_deepCopy(image, &snapshot))
    {
        for (i = 0; i < dest.count; ++i)
            xpbs_win32_copyRectPixels(&snapshot,
                                      dest.rects[i].x - dx,
                                      dest.rects[i].y - dy,
                                      image,
                                      dest.rects[i].x,
                                      dest.rects[i].y,
                                      dest.rects[i].width,
                                      dest.rects[i].height);
            xpbs_win32_fillRegion(image, &vacated, 0u);
    }
    XImage_deinit_base(&snapshot);
    XRegion_deinit(&dest);
    XRegion_deinit(&vacated);
    XRegion_deinit(&clip);
    if (inactive) xpbs_win32_syncImage(image, inactive);
    return true;
}

void XPlatformBackingStore_beginPaint(XPlatformBackingStore* self,
                                      const XRegion* region)
{
    int w, h;
    if (!self) return;
    /* xpbs_win32_clipRegion() reuses the initialized region capacity. */
    XRegion_clear(&self->m_paintRegion);
    {
        XImage* image = xpbs_win32_activeImage(self);
        if (image && image->m_data)
    {
        XRect full;
#if XGUI_BACKINGSTORE_RENDER_MODE == XGUI_BACKINGSTORE_RENDER_MODE_PARTIAL
        w = self->m_size.width;
        h = self->m_size.height;
#else
        w = XImage_width(image);
        h = XImage_height(image);
#endif
#if XGUI_BACKINGSTORE_RENDER_MODE == XGUI_BACKINGSTORE_RENDER_MODE_FULL
        region = NULL;
#endif
 #if XGUI_BACKINGSTORE_RENDER_MODE == XGUI_BACKINGSTORE_RENDER_MODE_PARTIAL
        xpbs_win32_clipRegion(region, self->m_size.width, self->m_size.height,
                              &self->m_paintRegion);
 #else
        xpbs_win32_clipRegion(region, w, h, &self->m_paintRegion);
 #endif
        if (XRegion_isEmpty(&self->m_paintRegion))
        {
            full.x = 0; full.y = 0;
 #if XGUI_BACKINGSTORE_RENDER_MODE == XGUI_BACKINGSTORE_RENDER_MODE_PARTIAL
            full.width = self->m_size.width; full.height = self->m_size.height;
 #else
            full.width = w; full.height = h;
 #endif
            if (w > 0 && h > 0)
                XRegion_addRect(&self->m_paintRegion, &full);
        }
    }
    }
    self->m_beginPaintActive = 1u;
    self->m_tileCursorX = 0;
    self->m_tileCursorY = 0;
    self->m_tileActive = false;
}

void XPlatformBackingStore_endPaint(XPlatformBackingStore* self)
{
    if (!self) return;
    self->m_beginPaintActive = 0u;
    XRegion_clear(&self->m_paintRegion);
}

/* ==================== 静态内容 ==================== */

void XPlatformBackingStore_setStaticContents(XPlatformBackingStore* self,
                                             const XRegion* region)
{
    int w, h;
    if (!self) return;
    /* xpbs_win32_clipRegion() reuses the initialized region capacity. */
    XRegion_clear(&self->m_staticContents);
    if (!region || XRegion_isEmpty(region)) return;
    {
        XImage* image = xpbs_win32_activeImage((XPlatformBackingStore*)self);
        if (image && image->m_data)
        {
 #if XGUI_BACKINGSTORE_RENDER_MODE == XGUI_BACKINGSTORE_RENDER_MODE_PARTIAL
            w = self->m_size.width;
            h = self->m_size.height;
 #else
            w = XImage_width(image);
            h = XImage_height(image);
 #endif
            xpbs_win32_clipRegion(region, w, h, &self->m_staticContents);
        }
        else
        {
            XRegion_copy(region, &self->m_staticContents);
        }
    }
}

XRegion XPlatformBackingStore_staticContents(const XPlatformBackingStore* self)
{
    XRegion out;
    XRegion_init(&out);
    if (self)
        XRegion_copy(&self->m_staticContents, &out);
    return out;
}

bool XPlatformBackingStore_hasStaticContents(const XPlatformBackingStore* self)
{
    return self && !XRegion_isEmpty(&self->m_staticContents);
}

/* ==================== 平台扩展 ==================== */

void XPlatformBackingStore_setPresentCallback(
        XPlatformBackingStore* self,
        XPlatformBackingStorePresentFn callback, void* userData)
{
    if (!self) return;
    self->m_present = callback;
    self->m_userData = userData;
}

void XPlatformBackingStore_setNativeTargetWindow(
        XPlatformBackingStore* self, void* nativeWindow)
{
    if (!self) return;
    self->m_nativeTarget = nativeWindow;
}

#endif /* XBACKINGSTORE_ON && XPLATFORMBACKINGSTORE_ON && defined(_WIN32) */
#endif /* XBACKINGSTORE_ON && XPLATFORMBACKINGSTORE_ON */
