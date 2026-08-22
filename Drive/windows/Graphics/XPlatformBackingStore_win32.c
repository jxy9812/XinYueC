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
    XSize m_size;                             /**< 当前缓冲尺寸。 */
    XRegion m_staticContents;                 /**< 静态内容区域集合。 */
    XRegion m_paintRegion;                    /**< beginPaint 登记的绘制区。 */
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

/** @brief 深拷贝图像（XImage_copy_base 为共享引用，不能用）。 */
static bool xpbs_win32_deepCopy(const XImage* src, XImage* dst)
{
    const uint8_t* sbuf;
    uint8_t* dbuf;
    int w, h, bpl, row;
    if (!src || !dst || !src->m_data) return false;
    w = XImage_width(src);
    h = XImage_height(src);
    if (!XClassIsVtableNull(dst))
        XImage_deinit_base(dst);
    XImage_init(dst);
    XImage_init_ex(dst, w, h, XImage_format(src));
    if (!dst->m_data) return false;
    sbuf = XImage_constBits(src);
    dbuf = XImage_bits(dst);
    bpl = XImage_bytesPerLine(src);
    if (!sbuf || !dbuf || bpl <= 0 || XImage_bytesPerLine(dst) != bpl) return false;
    for (row = 0; row < h; ++row)
        memcpy(dbuf + (int64_t)row * bpl, sbuf + (int64_t)row * bpl, (size_t)bpl);
    return true;
}

/** @brief 把集合裁剪到图像范围。 */
static void xpbs_win32_clipRegion(const XRegion* region, int w, int h,
                                  XRegion* out)
{
    int i;
    XRect clip;
    XRegion_init(out);
    if (!region) return;
    for (i = 0; i < region->count; ++i)
    {
        if (xpbs_win32_clipRect(&region->rects[i], w, h, &clip))
            XRegion_addRect(out, &clip);
    }
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
                                     const XRect* rect)
{
    const uint8_t* sbuf;
    uint8_t* dbuf;
    int bpl;
    int row;
    if (!store || !store->m_dibBits || !rect || !store->m_image.m_data) return;
    sbuf = XImage_constBits(&store->m_image);
    dbuf = store->m_dibBits;
    bpl = XImage_bytesPerLine(&store->m_image);
    if (!sbuf || !dbuf || bpl <= 0) return;
    for (row = 0; row < rect->height; ++row)
        memcpy(dbuf + (int64_t)(rect->y + row) * bpl + (int64_t)rect->x * 4,
               sbuf + (int64_t)(rect->y + row) * bpl + (int64_t)rect->x * 4,
               (size_t)rect->width * 4u);
}

/** @brief 从内存 DC 把脏区 BitBlt 到目标窗口 DC。 */
static void xpbs_win32_presentDirtyRect(struct XPlatformBackingStore* store,
                                        const XRect* rect)
{
    HWND hwnd;
    HDC winDC;
    if (!store || !store->m_memDC || !rect) return;
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
    BitBlt(winDC, rect->x, rect->y, rect->width, rect->height,
           store->m_memDC, rect->x, rect->y, SRCCOPY);
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
    XRegion_init(&store->m_staticContents);
    XRegion_init(&store->m_paintRegion);
    XSize_init(&store->m_size, 0, 0);
    return store;
}

void XPlatformBackingStore_delete(XPlatformBackingStore* self)
{
    if (!self) return;
    xpbs_win32_releaseSurface(self);
    if (self->m_image.m_data)
    {
        XImage_deinit_base(&self->m_image);
        XImage_init(&self->m_image);
    }
    XRegion_deinit(&self->m_staticContents);
    XRegion_init(&self->m_staticContents);
    XRegion_deinit(&self->m_paintRegion);
    XRegion_init(&self->m_paintRegion);
    XFree_System(self);
}

/* ==================== 访问器 ==================== */

XWindow* XPlatformBackingStore_window(const XPlatformBackingStore* self)
{
    return self ? self->m_window : NULL;
}

XImage* XPlatformBackingStore_paintDevice(XPlatformBackingStore* self)
{
    return (self && self->m_image.m_data) ? &self->m_image : NULL;
}

bool XPlatformBackingStore_toImage(XPlatformBackingStore* self, XImage* out)
{
    if (!self || !out)
        return false;
    return xpbs_win32_deepCopy(&self->m_image, out);
}

/* ==================== 绘制流程 ==================== */

void XPlatformBackingStore_flush(XPlatformBackingStore* self, XWindow* window,
                                 const XRegion* region, const XPoint* offset)
{
    XRegion effective;
    XRect full;
    XPoint zero;
    const XPoint* off = offset;
    int i;
    if (!self || !self->m_image.m_data) return;
    if (!off)
    {
        XPoint_init(&zero, 0, 0);
        off = &zero;
    }
    if (region && !XRegion_isEmpty(region))
    {
        xpbs_win32_clipRegion(region, XImage_width(&self->m_image),
                              XImage_height(&self->m_image), &effective);
    }
    else
    {
        XRegion_init(&effective);
        full.x = 0; full.y = 0;
        full.width = XImage_width(&self->m_image);
        full.height = XImage_height(&self->m_image);
        if (full.width > 0 && full.height > 0)
            XRegion_addRect(&effective, &full);
    }
    /* 平台差异提交点：同步 DIB 并 BitBlt 到目标窗口，再触发 present。 */
    for (i = 0; i < effective.count; ++i)
    {
        xpbs_win32_syncDirtyRect(self, &effective.rects[i]);
        xpbs_win32_presentDirtyRect(self, &effective.rects[i]);
    }
    if (!XRegion_isEmpty(&effective) && self->m_present)
        self->m_present(self->m_userData, self, &effective, off);
    (void)window; /* 目标窗口由 setNativeTargetWindow 登记，参数保持 Qt 签名。 */
    XRegion_deinit(&effective);
}

void XPlatformBackingStore_resize(XPlatformBackingStore* self, const XSize* size)
{
    XImage oldImage;
    XRect oldRect;
    XRect newRect;
    XRect overlap;
    XRegion cropped;
    int ow, oh, w, h;
    if (!self || !size) return;
    w = size->width;
    h = size->height;
    if (w < 0 || h < 0) return;
    if (w == self->m_size.width && h == self->m_size.height) return;
    /* 先快照旧内容，重建 XImage 缓冲，再同步重建 GDI 表面。 */
    XImage_init(&oldImage);
    if (self->m_image.m_data)
        XImage_copy_base(&oldImage, &self->m_image);
    ow = XImage_width(&oldImage);
    oh = XImage_height(&oldImage);
    XImage_deinit_base(&self->m_image);
    XImage_init(&self->m_image);
    if (w > 0 && h > 0)
        XImage_init_ex(&self->m_image, w, h, XPBS_WIN32_IMAGE_FORMAT);
    if (oldImage.m_data && self->m_image.m_data)
    {
        oldRect.x = 0; oldRect.y = 0; oldRect.width = ow; oldRect.height = oh;
        newRect.x = 0; newRect.y = 0; newRect.width = w;  newRect.height = h;
        if (xpbs_win32_intersect(&oldRect, &newRect, &overlap))
            xpbs_win32_copyRectPixels(&oldImage, 0, 0, &self->m_image,
                                      0, 0, overlap.width, overlap.height);
    }
    XImage_deinit_base(&oldImage);
    self->m_size.width = w;
    self->m_size.height = h;
    xpbs_win32_releaseSurface(self);
    if (w > 0 && h > 0)
        xpbs_win32_createSurface(self);
    xpbs_win32_clipRegion(&self->m_staticContents, w, h, &cropped);
    XRegion_deinit(&self->m_staticContents);
    XRegion_init(&self->m_staticContents);
    XRegion_copy(&cropped, &self->m_staticContents);
    XRegion_deinit(&cropped);
}

bool XPlatformBackingStore_scroll(XPlatformBackingStore* self,
                                  const XRegion* area, int dx, int dy)
{
    XRegion clip;
    XRegion dest;
    XRegion vacated;
    XImage snapshot;
    int w, h, i;
    XRect dstRect;
    if (!self || !self->m_image.m_data) return false;
    if (!area || XRegion_isEmpty(area)) return false;
    if (dx == 0 && dy == 0) return true;
    w = XImage_width(&self->m_image);
    h = XImage_height(&self->m_image);
    if (w <= 0 || h <= 0) return false;
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
    if (xpbs_win32_deepCopy(&self->m_image, &snapshot))
    {
        for (i = 0; i < dest.count; ++i)
            xpbs_win32_copyRectPixels(&snapshot,
                                      dest.rects[i].x - dx,
                                      dest.rects[i].y - dy,
                                      &self->m_image,
                                      dest.rects[i].x,
                                      dest.rects[i].y,
                                      dest.rects[i].width,
                                      dest.rects[i].height);
        xpbs_win32_fillRegion(&self->m_image, &vacated, 0u);
    }
    XImage_deinit_base(&snapshot);
    XRegion_deinit(&dest);
    XRegion_deinit(&vacated);
    XRegion_deinit(&clip);
    return true;
}

void XPlatformBackingStore_beginPaint(XPlatformBackingStore* self,
                                      const XRegion* region)
{
    int w, h;
    if (!self) return;
    XRegion_clear(&self->m_paintRegion);
    if (self->m_image.m_data)
    {
        XRect full;
        w = XImage_width(&self->m_image);
        h = XImage_height(&self->m_image);
        xpbs_win32_clipRegion(region, w, h, &self->m_paintRegion);
        if (XRegion_isEmpty(&self->m_paintRegion))
        {
            full.x = 0; full.y = 0; full.width = w; full.height = h;
            if (w > 0 && h > 0)
                XRegion_addRect(&self->m_paintRegion, &full);
        }
    }
    self->m_beginPaintActive = 1u;
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
    XRegion_clear(&self->m_staticContents);
    if (!region || XRegion_isEmpty(region)) return;
    if (self->m_image.m_data)
    {
        w = XImage_width(&self->m_image);
        h = XImage_height(&self->m_image);
        xpbs_win32_clipRegion(region, w, h, &self->m_staticContents);
    }
    else
    {
        XRegion_copy(region, &self->m_staticContents);
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
