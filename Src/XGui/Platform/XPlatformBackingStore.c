/******************************************************************************
 * @file       XPlatformBackingStore.c
 * @brief      XPlatformBackingStore 平台后备存储「共享软件核心」实现。
 * @details    本文件把 QPlatformBackingStore 的软件语义全部收敛到公共层：
 *             - 双/单 XImage 软件缓冲（ARGB32 预乘）、外部调用方缓冲、
 *               尺寸/静态内容/绘制区/tile 遍历/scroll/resize 保留左上；
 *             - 平台差异（提交到真实显示目标）收敛为 6 个
 *               XPlatformBackingStoreDriver_* 钩子（对标
 *               XPlatformGraphicsDriver_* 的 opaque nativeState 惯例）；
 *             - 平台后端只需提供这些钩子，无需再维护缓冲逻辑；
 *               Unsupported 存根 Driver_create 返回 false 时，本层 create
 *               返回 NULL，XBackingStore 保持「空后端」语义。
 *             本文件不包含任何平台 API 头。
 * @note       模块总开关 XBACKINGSTORE_ON 与 XPLATFORMBACKINGSTORE_ON 定义
 *             于 XGuiConfig.h；任一处 0 时本实现整体裁剪。
 * @author     XinYueC 团队
 ******************************************************************************/
#include "XPlatformBackingStore.h"

#if XBACKINGSTORE_ON && XPLATFORMBACKINGSTORE_ON

#include "XImage.h"
#include "XImageFormat.h"
#include "XMemory.h"
#include <string.h>

/** @brief 后备缓冲默认像素格式（对标 Qt 栅格后备存储的 ARGB32 预乘）。 */
#define XPBS_IMAGE_FORMAT XImageFormat_ARGB32_Premultiplied

/** @brief 共享软件核心 + 平台提交状态。 */
struct XPlatformBackingStore
{
    XWindow* m_window;                        /**< 绑定窗口（借用，不持有）。 */
    XImage m_image;                           /**< 内部软件帧缓冲（拥有）。 */
#if XGUI_BACKINGSTORE_BUFFER_COUNT > 1
    XImage m_image2;                          /**< 双缓冲的第二帧缓冲。 */
#endif
    unsigned m_activeIndex;                   /**< 当前绘制/提交缓冲索引。 */
    void* m_buffer1;                          /**< 外部第一块缓冲（借用）。 */
    void* m_buffer2;                          /**< 外部第二块缓冲（借用）。 */
    size_t m_bufferSize;                      /**< 外部每块缓冲容量。 */
    bool m_externalBuffers;                   /**< 是否使用外部缓冲。 */
    bool m_buffersInitialized;                /**< 外部绑定是否已完成一次。 */
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
    void* m_nativeTarget;                     /**< 原生目标窗口句柄（Windows HWND，其它平台记录）。 */
    unsigned m_beginPaintActive;              /**< beginPaint/endPaint 区间标志。 */
    void* m_nativeState;                      /**< 平台提交状态（Driver 拥有）。 */
};

/* ==================== 内部工具 ==================== */

/** @brief 把矩形裁剪到图像范围；空矩形返回 false。 */
static bool xpbs_clipRect(const XRect* rect, int w, int h, XRect* out)
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
static bool xpbs_intersect(const XRect* a, const XRect* b, XRect* out)
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

/** @brief 按行复制像素矩形（4 字节/像素，ARGB32 小端与 DIB BGRA 一致）。
 *  @note  源与目标各自使用自己的 bytesPerLine，因此支持跨尺寸复制
 *         （resize 保留左上重叠区时源缓冲与目标缓冲行距可能不同）。 */
static void xpbs_copyRectPixels(const XImage* src, int sx, int sy,
                                XImage* dst, int dx, int dy,
                                int w, int h)
{
    const uint8_t* sbuf;
    uint8_t* dbuf;
    int srcBpl;
    int dstBpl;
    int row;
    if (!src || !dst || w <= 0 || h <= 0) return;
    sbuf = XImage_constBits(src);
    dbuf = XImage_bits(dst);
    srcBpl = XImage_bytesPerLine(src);
    dstBpl = XImage_bytesPerLine(dst);
    if (!sbuf || !dbuf || srcBpl <= 0 || dstBpl <= 0) return;
    for (row = 0; row < h; ++row)
        memmove(dbuf + (int64_t)(dy + row) * dstBpl + (int64_t)dx * 4,
                sbuf + (int64_t)(sy + row) * srcBpl + (int64_t)sx * 4,
                (size_t)w * 4u);
}

/** @brief 把源图像深拷贝到目标图像（XCopy 为共享引用，不能用）。 */
static bool xpbs_deepCopy(const XImage* src, XImage* dst)
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
static void xpbs_clipRegion(const XRegion* region, int w, int h,
                            XRegion* out)
{
    int i;
    XRect clip;
    XRegion_clear(out);
    if (!region) return;
    for (i = 0; i < region->count; ++i)
    {
        if (xpbs_clipRect(&region->rects[i], w, h, &clip))
            XRegion_addRect(out, &clip);
    }
}

/** @brief 用指定颜色填充集合内的全部矩形。 */
static void xpbs_fillRegion(XImage* image, const XRegion* region,
                            uint32_t color)
{
    int i;
    if (!image || !region) return;
    for (i = 0; i < region->count; ++i)
        XImage_fillRect(image, &region->rects[i], color);
}

/** @brief 从快照把 (srcRect) 平移 (dx,dy) 后的内容拷回 image。 */
static void xpbs_blitFromSnapshot(const XImage* snapshot, int srcX, int srcY,
                                  XImage* image, int dstX, int dstY,
                                  int copyW, int copyH)
{
    xpbs_copyRectPixels(snapshot, srcX, srcY, image, dstX, dstY,
                        copyW, copyH);
}

static XImage* xpbs_activeImage(XPlatformBackingStore* self)
{
    if (!self) return NULL;
#if XGUI_BACKINGSTORE_BUFFER_COUNT > 1
    return self->m_activeIndex == 0u ? &self->m_image : &self->m_image2;
#else
    return &self->m_image;
#endif
}

static XImage* xpbs_inactiveImage(XPlatformBackingStore* self)
{
#if XGUI_BACKINGSTORE_BUFFER_COUNT > 1
    if (!self) return NULL;
    return self->m_activeIndex == 0u ? &self->m_image2 : &self->m_image;
#else
    (void)self;
    return NULL;
#endif
}

static void xpbs_syncImage(const XImage* source, XImage* target)
{
    int width;
    int height;
    if (!source || !target || !source->m_data || !target->m_data) return;
    width = XImage_width(source);
    height = XImage_height(source);
    if (width <= 0 || height <= 0 || width != XImage_width(target) ||
        height != XImage_height(target)) return;
    xpbs_copyRectPixels(source, 0, 0, target, 0, 0, width, height);
}

static bool xpbs_bufferSizeValid(const XSize* size, size_t bufferSize)
{
    size_t required = XPlatformBackingStore_requiredBufferSize(size);
    return required == 0 ? (!size || size->width <= 0 || size->height <= 0)
                         : bufferSize >= required;
}

/** @brief 调用 present 回调并隔离回调期间的区域生命周期（防重入）。 */
static void xpbs_invokePresent(XPlatformBackingStore* self,
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
        stride = (size_t)XImageFormat_bytesPerLine(bw, XPBS_IMAGE_FORMAT);
        if (stride == 0 || (size_t)bh > SIZE_MAX / stride) return 0;
        return stride * (size_t)bh;
    }
#else
    stride = (size_t)XImageFormat_bytesPerLine(size->width,
                                                XPBS_IMAGE_FORMAT);
    if (stride == 0 || (size_t)size->height > SIZE_MAX / stride) return 0;
    return stride * (size_t)size->height;
#endif
}

static void xpbs_initConfiguredImage(XImage* image, int width, int height,
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
        stride = XImageFormat_bytesPerLine(width, XPBS_IMAGE_FORMAT);
        XImage_init_ex_2(image, width, height, XPBS_IMAGE_FORMAT,
                         stride, (uint8_t*)buffer, NULL, NULL);
        (void)bufferSize;
    }
    else
        XImage_init_ex(image, width, height, XPBS_IMAGE_FORMAT);
}

/* ==================== 生命周期（共享实现 + 平台驱动） ==================== */

XPlatformBackingStore* XPlatformBackingStore_create(XWindow* window)
{
    XPlatformBackingStore* store;
    void* nativeState = NULL;
    /* 平台可提交能力探测：Unsupported 存根返回 false → 保持空后端
       （公共 create 返回 NULL，XBackingStore 安全退化）。 */
    if (!XPlatformBackingStoreDriver_create(&nativeState, window))
        return NULL;
    store = (XPlatformBackingStore*)XCalloc_System(1u, sizeof(XPlatformBackingStore));
    if (!store)
    {
        XPlatformBackingStoreDriver_destroy(nativeState);
        return NULL;
    }
    store->m_window = window;
    store->m_nativeState = nativeState;
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
    if (self->m_nativeState)
    {
        XPlatformBackingStoreDriver_destroy(self->m_nativeState);
        self->m_nativeState = NULL;
    }
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
    XImage* image = xpbs_activeImage(self);
    return (image && image->m_data) ? image : NULL;
}

bool XPlatformBackingStore_nextTile(XPlatformBackingStore* self,
                                    XRect* tileRect)
{
    XImage* image;
    if (!self || !tileRect || !self->m_beginPaintActive) return false;
    image = xpbs_activeImage(self);
    if (!image || !image->m_data) return false;
#if XGUI_BACKINGSTORE_RENDER_MODE == XGUI_BACKINGSTORE_RENDER_MODE_PARTIAL
    int tileW, tileH, x, y, i;
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
            if (xpbs_intersect(tileRect, &self->m_paintRegion.rects[i], &hit)) {
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
    return xpbs_deepCopy(xpbs_activeImage(self), out);
}

/* ==================== 绘制流程 ==================== */

void XPlatformBackingStore_flush(XPlatformBackingStore* self, XWindow* window,
                                 const XRegion* region, const XPoint* offset)
{
    XRect full;
    XPoint zero;
    const XPoint* off = offset;
    XImage* image;
    if (!self || !(image = xpbs_activeImage(self)) || !image->m_data)
        return;
    XRegion_clear(&self->m_flushRegion);
    if (!off)
    {
        XPoint_init(&zero, 0, 0);
        off = &zero;
    }
    if (region && !XRegion_isEmpty(region))
    {
        xpbs_clipRegion(region, XImage_width(image),
                        XImage_height(image), &self->m_flushRegion);
    }
    else
    {
        full.x = 0;
        full.y = 0;
        full.width = XImage_width(image);
        full.height = XImage_height(image);
        if (full.width > 0 && full.height > 0)
            XRegion_addRect(&self->m_flushRegion, &full);
    }
    /* FULL 始终提交整屏。 */
#if XGUI_BACKINGSTORE_RENDER_MODE == XGUI_BACKINGSTORE_RENDER_MODE_FULL
    XRegion_clear(&self->m_flushRegion);
    full.x = 0; full.y = 0;
    full.width = XImage_width(image); full.height = XImage_height(image);
    if (full.width > 0 && full.height > 0)
        XRegion_addRect(&self->m_flushRegion, &full);
#endif
    if (!XRegion_isEmpty(&self->m_flushRegion)) {
        /* DIRECT/PARTIAL：提交前把脏区同步到另一帧缓冲，保证下一帧只重绘
           变化区域即可（FULL 恒整屏，无需备份）。 */
#if XGUI_BACKINGSTORE_RENDER_MODE != XGUI_BACKINGSTORE_RENDER_MODE_FULL
        {
        XImage* inactive = xpbs_inactiveImage(self);
        if (inactive)
        {
            int i;
            for (i = 0; i < self->m_flushRegion.count; ++i)
                xpbs_copyRectPixels(image, self->m_flushRegion.rects[i].x,
                                    self->m_flushRegion.rects[i].y, inactive,
                                    self->m_flushRegion.rects[i].x,
                                    self->m_flushRegion.rects[i].y,
                                    self->m_flushRegion.rects[i].width,
                                    self->m_flushRegion.rects[i].height);
        }
        }
#endif
        /* 平台差异提交点：由 Driver 上屏（XPutImage / BitBlt / 显示驱动）。 */
        XPlatformBackingStoreDriver_present(self->m_nativeState, window, image,
                                            &self->m_flushRegion, off,
#if XGUI_BACKINGSTORE_RENDER_MODE == XGUI_BACKINGSTORE_RENDER_MODE_FULL
                                            true);
#else
                                            false);
#endif
        xpbs_invokePresent(self, &self->m_flushRegion, off);
    }
#if XGUI_BACKINGSTORE_BUFFER_COUNT > 1
    self->m_activeIndex ^= 1u;
#endif
}

void XPlatformBackingStore_flushTile(XPlatformBackingStore* self, XWindow* window,
                                      const XRect* tileRect, const XPoint* offset)
{
    XPoint origin;
    XRect presentedRect;
    XImage* image;
    if (!self || !tileRect || !(image = xpbs_activeImage(self)) ||
        !image->m_data || tileRect->width <= 0 || tileRect->height <= 0)
        return;
    origin.x = tileRect->x;
    origin.y = tileRect->y;
    if (offset) { origin.x += offset->x; origin.y += offset->y; }
    /* tile image 的绘制坐标始终从 (0, 0) 起，而 presentedRect 用目标
       窗口坐标表达。Driver_presentTile 通过 offset(origin) 将二者映射。 */
    presentedRect.x = origin.x;
    presentedRect.y = origin.y;
    presentedRect.width = tileRect->width;
    presentedRect.height = tileRect->height;
    XRegion_clear(&self->m_flushRegion);
    XRegion_addRect(&self->m_flushRegion, &presentedRect);
    XPlatformBackingStoreDriver_presentTile(self->m_nativeState, window, image,
                                            &self->m_flushRegion, &origin);
    xpbs_invokePresent(self, &self->m_flushRegion, &origin);
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
        !xpbs_bufferSizeValid(size, self->m_bufferSize))
        return;
    if (w == self->m_size.width && h == self->m_size.height) return;
    active = xpbs_activeImage(self);
    /* 先快照旧内容（共享引用），重建缓冲，最后回拷左上重叠区。 */
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
        xpbs_initConfiguredImage(&newImage,
            w < XGUI_BACKINGSTORE_PARTIAL_BUFFER_WIDTH ? w : XGUI_BACKINGSTORE_PARTIAL_BUFFER_WIDTH,
            h < XGUI_BACKINGSTORE_PARTIAL_BUFFER_HEIGHT ? h : XGUI_BACKINGSTORE_PARTIAL_BUFFER_HEIGHT,
            self->m_externalBuffers ? self->m_buffer1 : NULL, self->m_bufferSize);
#else
        xpbs_initConfiguredImage(&newImage, w, h,
            self->m_externalBuffers ? self->m_buffer1 : NULL, self->m_bufferSize);
#endif
#if XGUI_BACKINGSTORE_BUFFER_COUNT > 1
    if (w > 0 && h > 0)
#if XGUI_BACKINGSTORE_RENDER_MODE == XGUI_BACKINGSTORE_RENDER_MODE_PARTIAL
        xpbs_initConfiguredImage(&newImage2,
            w < XGUI_BACKINGSTORE_PARTIAL_BUFFER_WIDTH ? w : XGUI_BACKINGSTORE_PARTIAL_BUFFER_WIDTH,
            h < XGUI_BACKINGSTORE_PARTIAL_BUFFER_HEIGHT ? h : XGUI_BACKINGSTORE_PARTIAL_BUFFER_HEIGHT,
            self->m_externalBuffers ? self->m_buffer2 : NULL, self->m_bufferSize);
#else
        xpbs_initConfiguredImage(&newImage2, w, h,
            self->m_externalBuffers ? self->m_buffer2 : NULL, self->m_bufferSize);
#endif
#endif
    /* 分配失败保持旧缓冲不变。 */
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
        if (xpbs_intersect(&oldRect, &newRect, &overlap))
        {
            xpbs_blitFromSnapshot(&oldImage, 0, 0, &newImage,
                                  0, 0, overlap.width, overlap.height);
#if XGUI_BACKINGSTORE_BUFFER_COUNT > 1
            xpbs_blitFromSnapshot(&oldImage, 0, 0, &newImage2,
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
    /* 静态内容裁到新尺寸（Qt 在 resize 后同样收敛到有效区域）。 */
    XRegion_init(&cropped);
    xpbs_clipRegion(&self->m_staticContents, w, h, &cropped);
    XRegion_copy(&cropped, &self->m_staticContents);
    XRegion_deinit(&cropped);
    /* 通知平台重建表面（Win32 DIB/DC）。 */
    XPlatformBackingStoreDriver_surfaceResized(self->m_nativeState, w, h);
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
    if (!self || !(image = xpbs_activeImage(self)) || !image->m_data)
        return false;
#if XGUI_BACKINGSTORE_RENDER_MODE == XGUI_BACKINGSTORE_RENDER_MODE_PARTIAL
    if (self->m_size.width > XImage_width(image) ||
        self->m_size.height > XImage_height(image))
        return false;
#endif
    inactive = xpbs_inactiveImage(self);
    if (!area || XRegion_isEmpty(area)) return false;
    if (dx == 0 && dy == 0) return true;
    w = XImage_width(image);
    h = XImage_height(image);
    if (w <= 0 || h <= 0) return false;
    XRegion_init(&clip);
    xpbs_clipRegion(area, w, h, &clip);
    if (XRegion_isEmpty(&clip))
    {
        XRegion_deinit(&clip);
        return true;
    }
    /* 目标区域 = 源区域平移 (dx,dy) 后裁剪到缓冲内。 */
    XRegion_init(&dest);
    for (i = 0; i < clip.count; ++i)
    {
        dstRect.x = clip.rects[i].x + dx;
        dstRect.y = clip.rects[i].y + dy;
        dstRect.width = clip.rects[i].width;
        dstRect.height = clip.rects[i].height;
        if (xpbs_clipRect(&dstRect, w, h, &dstRect))
            XRegion_addRect(&dest, &dstRect);
    }
    /* 原区域减去目标区域 = 需要清空的部分。 */
    XRegion_init(&vacated);
    XRegion_subtracted(&clip, &dest, &vacated);
    /* 整幅快照后逐目标矩形搬移，规避矩形间/行间重叠顺序问题。 */
    XImage_init(&snapshot);
    if (xpbs_deepCopy(image, &snapshot))
    {
        for (i = 0; i < dest.count; ++i)
        {
            const int copyW = dest.rects[i].width;
            const int copyH = dest.rects[i].height;
            xpbs_blitFromSnapshot(&snapshot,
                                  dest.rects[i].x - dx,
                                  dest.rects[i].y - dy,
                                  image,
                                  dest.rects[i].x,
                                  dest.rects[i].y,
                                  copyW, copyH);
        }
        xpbs_fillRegion(image, &vacated, 0u);
    }
    XImage_deinit_base(&snapshot);
    XRegion_deinit(&dest);
    XRegion_deinit(&vacated);
    XRegion_deinit(&clip);
    if (inactive) xpbs_syncImage(image, inactive);
    return true;
}

void XPlatformBackingStore_beginPaint(XPlatformBackingStore* self,
                                      const XRegion* region)
{
    int w, h;
    if (!self) return;
    /* xpbs_clipRegion() reuses the initialized output region. */
    XRegion_clear(&self->m_paintRegion);
    {
        XImage* image = xpbs_activeImage(self);
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
        xpbs_clipRegion(region, self->m_size.width, self->m_size.height,
                        &self->m_paintRegion);
 #else
        xpbs_clipRegion(region, w, h, &self->m_paintRegion);
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
    /* The clipping helper reuses the initialized output region. */
    XRegion_clear(&self->m_staticContents);
    if (!region || XRegion_isEmpty(region)) return;
    {
        XImage* image = xpbs_activeImage((XPlatformBackingStore*)self);
        if (image && image->m_data)
    {
#if XGUI_BACKINGSTORE_RENDER_MODE == XGUI_BACKINGSTORE_RENDER_MODE_PARTIAL
        w = self->m_size.width;
        h = self->m_size.height;
#else
        w = XImage_width(image);
        h = XImage_height(image);
#endif
        xpbs_clipRegion(region, w, h, &self->m_staticContents);
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

/* ==================== 缓冲与平台扩展 ==================== */

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
            !xpbs_bufferSizeValid(&self->m_size, bufferSize))
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
    XPlatformBackingStoreDriver_setNativeTarget(self->m_nativeState,
                                                nativeWindow);
}

#endif /* XBACKINGSTORE_ON && XPLATFORMBACKINGSTORE_ON */
