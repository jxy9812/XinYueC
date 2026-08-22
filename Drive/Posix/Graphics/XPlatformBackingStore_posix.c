/******************************************************************************
 * @file       XPlatformBackingStore_posix.c
 * @brief      Linux XPlatformBackingStore 软件后备存储后端。
 * @details    本文件是 XBackingStore 公共类的 Linux 平台后端，采用纯软件
 *             XImage 缓冲：paintDevice 返回内部 ARGB32 预乘 Alpha 图像，
 *             resize/滚动/静态内容全部在进程内完成，不连接 X11/Wayland。
 *             对嵌入式/无窗口系统环境，flush 通过可选的 present 回调把
 *             脏矩形交给显示驱动或窗口侧缓冲；回调由
 *             XPlatformBackingStore_setPresentCallback 登记。本文件不包含
 *             任何 Linux API 头，因此同一实现也可在需要时迁往其它
 *             软件光栅化平台。文件同时受 XBACKINGSTORE_ON 与
 *             XPLATFORMBACKINGSTORE_ON 总开关约束（Drive 平台后端惯例）。
 */

#include "XPlatformBackingStore.h"

#if XBACKINGSTORE_ON && XPLATFORMBACKINGSTORE_ON

#if defined(__linux__)

#include "XImage.h"
#include "XMemory.h"
#if XPLATFORMNATIVEWINDOW_ON
#include "XPlatformNativeWindow.h"
#include "XWindow.h"
#endif /* XPLATFORMNATIVEWINDOW_ON */
#include <string.h>

/** @brief 后备缓冲默认像素格式（对标 Qt 栅格后备存储的 ARGB32 预乘）。 */
#define XPBS_POSIX_IMAGE_FORMAT XImageFormat_ARGB32_Premultiplied

/** @brief Linux 软件后端私有句柄。 */
struct XPlatformBackingStore
{
    XWindow* m_window;                        /**< 绑定窗口（借用，不持有）。 */
    XImage m_image;                           /**< 内部软件帧缓冲（拥有）。 */
    XSize m_size;                             /**< 当前缓冲尺寸。 */
    XRegion m_staticContents;                 /**< 静态内容区域集合。 */
    XRegion m_paintRegion;                    /**< beginPaint 登记的绘制区。 */
    XPlatformBackingStorePresentFn m_present; /**< present 回调（借用）。 */
    void* m_userData;                         /**< present 回调用户数据（借用）。 */
    void* m_nativeTarget;                     /**< 原生目标窗口句柄（Windows HWND，其它平台记录）。 */
    unsigned m_beginPaintActive;              /**< beginPaint/endPaint 区间标志。 */
};

/* ==================== 内部工具 ==================== */

/** @brief 把矩形裁剪到图像范围；空矩形返回 false。 */
static bool xpbs_posix_clipRect(const XRect* rect, int w, int h, XRect* out)
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
static bool xpbs_posix_intersect(const XRect* a, const XRect* b, XRect* out)
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

/** @brief 按行复制像素矩形：把 src 的 (sx,sy,w,h) 复制到 dst 的 (dx,dy)。
 *  本实现假定 4 字节/像素格式（XPBS 默认 ARGB32_Premultiplied）。 */
static void xpbs_posix_copyRectPixels(const XImage* src, int sx, int sy,
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

/** @brief 把源图像深拷贝到目标图像（XImage_copy_base 为共享引用，不能用）。 */
static bool xpbs_posix_deepCopy(const XImage* src, XImage* dst)
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
static void xpbs_posix_clipRegion(const XRegion* region, int w, int h,
                                  XRegion* out)
{
    int i;
    XRect clip;
    XRegion_init(out);
    if (!region) return;
    for (i = 0; i < region->count; ++i)
    {
        if (xpbs_posix_clipRect(&region->rects[i], w, h, &clip))
            XRegion_addRect(out, &clip);
    }
}

/** @brief 用指定颜色填充集合内的全部矩形。 */
static void xpbs_posix_fillRegion(XImage* image, const XRegion* region,
                                  uint32_t color)
{
    int i;
    if (!image || !region) return;
    for (i = 0; i < region->count; ++i)
        XImage_fillRect(image, &region->rects[i], color);
}

/** @brief 从快照把 (srcRect) 平移 (dx,dy) 后的内容拷回 image。 */
static void xpbs_posix_blitFromSnapshot(const XImage* snapshot, int srcX, int srcY,
                                        XImage* image, int dstX, int dstY,
                                        int copyW, int copyH)
{
    xpbs_posix_copyRectPixels(snapshot, srcX, srcY, image, dstX, dstY,
                              copyW, copyH);
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
    return xpbs_posix_deepCopy(&self->m_image, out);
}

/* ==================== 绘制流程 ==================== */

void XPlatformBackingStore_flush(XPlatformBackingStore* self, XWindow* window,
                                 const XRegion* region, const XPoint* offset)
{
    XRegion effective;
    XRect full;
    XPoint zero;
    const XPoint* off = offset;
    if (!self || !self->m_image.m_data) return;
    if (!off)
    {
        XPoint_init(&zero, 0, 0);
        off = &zero;
    }
    if (region && !XRegion_isEmpty(region))
    {
        xpbs_posix_clipRegion(region, XImage_width(&self->m_image),
                              XImage_height(&self->m_image), &effective);
    }
    else
    {
        XRegion_init(&effective);
        full.x = 0;
        full.y = 0;
        full.width = XImage_width(&self->m_image);
        full.height = XImage_height(&self->m_image);
        if (full.width > 0 && full.height > 0)
            XRegion_addRect(&effective, &full);
    }
    /* 平台差异提交点：窗口已挂接真实原生窗口（WId 非 0）时直接把脏区
     * 提交到 X11 窗口（XPutImage）；否则按嵌入式显示驱动回调交给用户
     * present 回调。两种路径语义一致：脏区 + 缓冲区偏移。 */
    if (!XRegion_isEmpty(&effective)) {
#if XPLATFORMNATIVEWINDOW_ON
        if (window &&
            XPlatformNativeWindow_winId(window) != 0) {
            XPlatformNativeWindow_present(window, &self->m_image,
                                          &effective, off);
        } else
#endif /* XPLATFORMNATIVEWINDOW_ON */
        if (self->m_present)
            self->m_present(self->m_userData, self, &effective, off);
    }
    (void)window; /* 软件路径下 XWindow 为纯逻辑窗口，无窗口系统句柄可提交。 */
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
    /* 先快照旧内容（共享引用），再重建缓冲，最后回拷左上重叠区。 */
    XImage_init(&oldImage);
    if (self->m_image.m_data)
        XImage_copy_base(&oldImage, &self->m_image);
    ow = XImage_width(&oldImage);
    oh = XImage_height(&oldImage);
    XImage_deinit_base(&self->m_image);
    XImage_init(&self->m_image);
    if (w > 0 && h > 0)
        XImage_init_ex(&self->m_image, w, h, XPBS_POSIX_IMAGE_FORMAT);
    if (oldImage.m_data && self->m_image.m_data)
    {
        oldRect.x = 0; oldRect.y = 0; oldRect.width = ow; oldRect.height = oh;
        newRect.x = 0; newRect.y = 0; newRect.width = w;  newRect.height = h;
        if (xpbs_posix_intersect(&oldRect, &newRect, &overlap))
            xpbs_posix_blitFromSnapshot(&oldImage, 0, 0, &self->m_image,
                                        0, 0, overlap.width, overlap.height);
    }
    XImage_deinit_base(&oldImage);
    self->m_size.width = w;
    self->m_size.height = h;
    /* 静态内容裁到新尺寸（Qt 在 resize 后同样收敛到有效区域）。 */
    xpbs_posix_clipRegion(&self->m_staticContents, w, h, &cropped);
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
    xpbs_posix_clipRegion(area, w, h, &clip);
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
        if (xpbs_posix_clipRect(&dstRect, w, h, &dstRect))
            XRegion_addRect(&dest, &dstRect);
    }
    /* 原区域减去目标区域 = 需要清空的部分。 */
    XRegion_init(&vacated);
    XRegion_subtracted(&clip, &dest, &vacated);
    /* 整幅快照后逐目标矩形搬移，规避矩形间/行间重叠顺序问题。 */
    XImage_init(&snapshot);
    if (xpbs_posix_deepCopy(&self->m_image, &snapshot))
    {
        for (i = 0; i < dest.count; ++i)
        {
            const int copyW = dest.rects[i].width;
            const int copyH = dest.rects[i].height;
            xpbs_posix_blitFromSnapshot(&snapshot,
                                        dest.rects[i].x - dx,
                                        dest.rects[i].y - dy,
                                        &self->m_image,
                                        dest.rects[i].x,
                                        dest.rects[i].y,
                                        copyW, copyH);
        }
        xpbs_posix_fillRegion(&self->m_image, &vacated, 0u);
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
        xpbs_posix_clipRegion(region, w, h, &self->m_paintRegion);
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
        xpbs_posix_clipRegion(region, w, h, &self->m_staticContents);
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

#endif /* XBACKINGSTORE_ON && XPLATFORMBACKINGSTORE_ON && defined(__linux__) */
#endif /* XBACKINGSTORE_ON && XPLATFORMBACKINGSTORE_ON */
