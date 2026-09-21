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
 *             - PARTIAL 模式的相邻 tile 攒批合并 flush（远端 §23.4 规划
 *               4）：flushTileBatched 把相邻/重叠 tile 内容拷入攒批缓冲，
 *               超 1/4 屏预算或 16ms 帧界才真正 present，帧末由
 *               flushPendingTiles 收批兜底；编译开关
 *               XGUI_BACKINGSTORE_TILE_BATCHING_ON（默认开）关闭后退化为
 *               逐片即时上屏。
 *             本文件不包含任何平台 API 头。
 * @note       模块总开关 XBACKINGSTORE_ON 与 XPLATFORMBACKINGSTORE_ON 定义
 *             于 XGuiConfig.h；任一处 0 时本实现整体裁剪。
 * @author     XinYueC 团队
 ******************************************************************************/
#include "XPlatformBackingStore.h"

#include "XAlgorithm.h"
#if XBACKINGSTORE_ON && XPLATFORMBACKINGSTORE_ON

#include "XImage.h"
#include "XImageFormat.h"
#include "XMemory.h"
/* 显示驱动契约（消费点见下方 xpbs_surfaceFormat / 平台 Driver_present）：
 * 契约头不含任何平台 API 头，公共层只经 ops 表消费；XPLATFORM_FBDEV_ON=0
 * 时本头整体为空、新消费代码同步裁剪（零新增 ABI 面，桌面零回归）。 */
#include "XPlatformDisplayDriver.h"

/** @brief tile 攒批总闸：仅 PARTIAL 模式有意义（DIRECT/FULL 没有逐片
 *  提交路径，攒批层整体裁剪为零开销）。XGUI_BACKINGSTORE_TILE_BATCHING_ON
 *  是面向调用方的编译开关（见契约头），此处收敛为文件内单一判定。 */
#if XGUI_BACKINGSTORE_RENDER_MODE == XGUI_BACKINGSTORE_RENDER_MODE_PARTIAL && \
    XGUI_BACKINGSTORE_TILE_BATCHING_ON
#define XPBS_TILE_BATCHING_ON 1
#else
#define XPBS_TILE_BATCHING_ON 0
#endif

#if XPBS_TILE_BATCHING_ON
#include "XDateTime.h"
/** @brief 攒批 16ms 帧界（60Hz 一帧，对标 Qt 高频局部更新按 vsync 合帧；
 *  批次首片入批起超过该时长即强制先上屏，防大脏区 repaint 首片延迟）。 */
#define XPBS_TILE_BATCH_FRAME_MS 16
#endif

/** @brief 后备缓冲像素格式（对标 Qt 栅格后备存储的 ARGB32 预乘）。
 *  @note  由 XGuiConfig.h 的 XGUI_BACKINGSTORE_IMAGE_FORMAT_RGB16 编译期
 *         选择（对标 QBackingStore 按目标窗口/屏幕格式协商缓冲，嵌入式
 *         面板格式出厂固定，故以编译期开关表达）：默认 ARGB32 预乘，
 *         与既有行为逐位一致；置 1 切换为 RGB16（RGB565，每像素 2 字
 *         节），供无 Alpha 的 16 位面板目标省一半表面内存与带宽。选择
 *         器是 0/1 布尔（XGuiConfig.h 不反向包含 XImageFormat.h），枚举
 *         映射收敛在本文件。 */
#if XGUI_BACKINGSTORE_IMAGE_FORMAT_RGB16
#define XPBS_IMAGE_FORMAT XImageFormat_RGB16
#else
#define XPBS_IMAGE_FORMAT XImageFormat_ARGB32_Premultiplied
#endif

/** @brief 后备缓冲每像素字节数（与 XPBS_IMAGE_FORMAT 同源的条件编译
 *  常量）。XImageFormat 体系只提供运行时访问器（XImageFormat_bitDepth
 *  返回位数，格式表为 XImageFormat.c 私有），stride 校验等需要「每像
 *  素字节」的场合直接用本常量，保持编译期常量、零运行时开销。 */
#if XGUI_BACKINGSTORE_IMAGE_FORMAT_RGB16
#define XPBS_PIXEL_BYTES 2u
#else
#define XPBS_PIXEL_BYTES 4u
#endif

/* ==================== 显示驱动格式协商（消费点 1：缓冲格式决定） ====================
 * 嵌入式单屏（XPlatformDisplayDriver 契约，驱动注册见
 * Drive/Posix/Graphics/XPlatformFramebuffer_posix.c）：后备存储创建/resize
 * 分配缓冲时，以编译期选择器格式（XPBS_IMAGE_FORMAT）为 preferred 向活动
 * 驱动 formatNegotiate，按面板扫描格式分配缓冲——present 侧无需任何转换：
 * - 面板 565 + 选择器 RGB16=1：协商为真即"直写零拷贝"（present 经平台
 *   Driver 的 fbdev 直写路径 memcpy 上屏，X11 路径跳过）；
 * - 面板非 565（RGB888/RGB32 等）：同样按面板格式分配，避免逐帧转换；
 * - 面板格式不可识别（Invalid）：保持编译期格式，present 不直写（回落
 *   平台既有提交路径）。
 * 无活动驱动（桌面默认未注册）或 XPLATFORM_FBDEV_ON=0 时折叠为编译期
 * 常量，缓冲格式与既有行为逐位一致（零回归）。 */
#if XGUI_ON && XPLATFORM_FBDEV_ON
/** @brief 上次协商时的活动驱动（注销/换驱动后惰性重协商的失效判据）。 */
static const XPlatformDisplayDriverOps* g_xpbsDriverOps = NULL;
/** @brief 协商出的后备表面格式（无驱动时恒为编译期选择器格式）。 */
static XImageFormat g_xpbsDriverFormat = XPBS_IMAGE_FORMAT;
#endif

/**
 * @brief 后备表面像素格式（运行期：优先驱动协商结果；否则编译期选择器）。
 * @note  调用点为 create/resize/requiredBufferSize 等缓冲决策路径（非逐
 *        像素热路径）；协商结果按活动驱动指针缓存，板级启动代码在 GUI
 *        初始化前注册驱动的常规时序下首次查询即定案。
 */
static XImageFormat xpbs_surfaceFormat(void)
{
#if XGUI_ON && XPLATFORM_FBDEV_ON
    const XPlatformDisplayDriverOps* ops = XPlatformDisplayDriver_active();
    if (ops != g_xpbsDriverOps)
    {
        XImageFormat panel = XImageFormat_Invalid;
        g_xpbsDriverOps = ops;
        g_xpbsDriverFormat = XPBS_IMAGE_FORMAT;
        /* preferred 传编译期选择器格式；返回值只区分"可直写/需转换"，
           两种情况下只要面板报告了可识别扫描格式就按它分配（避免
           present 逐帧转换），直写可行性由 present 侧对照
           "后备格式==面板格式"再判定。面板格式不可识别（Invalid）时
           保持编译期格式，present 不直写。 */
        if (ops)
        {
            (void)ops->formatNegotiate(XPBS_IMAGE_FORMAT, &panel);
            if (panel != XImageFormat_Invalid)
                g_xpbsDriverFormat = panel;
        }
    }
    return g_xpbsDriverFormat;
#else
    return XPBS_IMAGE_FORMAT;
#endif
}

/** @brief 每像素字节数（按格式位深推导，支持运行期协商格式；未知格式
 *         返回 0，调用方据此拒绝分配/拷贝）。 */
static size_t xpbs_pixelBytes(XImageFormat format)
{
    int depth = XImageFormat_bitDepth(format);
    return depth > 0 ? (size_t)((depth + 7) / 8) : 0u;
}

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
    bool m_nativeBufferMode;                  /**< 绘制缓冲 = 平台共享内存（DIB），单缓冲。 */
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
#if XPBS_TILE_BATCHING_ON
    /* tile 攒批（PARTIAL 专属，见文件内 XPBS_TILE_BATCHING_ON）：把相邻
     * tile 内容先拷入攒批缓冲，凑满一片连续区域后一次 present，消除逐片
     * 上屏的 present 次数放大（对标 Qt 高频局部更新按帧合批）。 */
    XImage m_batchImage;                      /**< 攒批缓冲（拥有）：容纳当前预算批次的外接矩形。 */
    XRegion m_batchRegion;                    /**< 已攒 tile 的窗口坐标集合（present 脏区）。 */
    XRect m_batchBounds;                      /**< 已攒集合的外接矩形（窗口坐标）。 */
    int64_t m_batchStartMs;                   /**< 批次首片入批时刻（16ms 帧界判定基准）。 */
    bool m_batchOpen;                         /**< 攒批中是否存在待上屏内容。 */
#endif
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

/** @brief 按行复制像素矩形（每像素字节数按源/目标格式推导：默认 ARGB32
 *         4 字节小端与 DIB BGRA 一致，RGB16 为 2 字节）。
 *  @note  源与目标各自使用自己的 bytesPerLine，因此支持跨尺寸复制
 *         （resize 保留左上重叠区时源缓冲与目标缓冲行距可能不同）。
 *         源/目标格式不一致时拒绝拷贝（驱动运行中注册换代的理论时序，
 *         宁丢迁移内容不可错位污染，见 xpbs_surfaceFormat 注）。 */
static void xpbs_copyRectPixels(const XImage* src, int sx, int sy,
                                XImage* dst, int dx, int dy,
                                int w, int h)
{
    const uint8_t* sbuf;
    uint8_t* dbuf;
    size_t pixelBytes;
    int srcBpl;
    int dstBpl;
    int row;
    if (!src || !dst || w <= 0 || h <= 0) return;
    if (XImage_format(src) != XImage_format(dst)) return;
    pixelBytes = xpbs_pixelBytes(XImage_format(src));
    if (pixelBytes == 0) return;
    sbuf = XImage_constBits(src);
    dbuf = XImage_bits(dst);
    srcBpl = XImage_bytesPerLine(src);
    dstBpl = XImage_bytesPerLine(dst);
    if (!sbuf || !dbuf || srcBpl <= 0 || dstBpl <= 0) return;
    for (row = 0; row < h; ++row)
        XMemmove(dbuf + (int64_t)(dy + row) * dstBpl +
                     (int64_t)dx * (int64_t)pixelBytes,
                sbuf + (int64_t)(sy + row) * srcBpl +
                     (int64_t)sx * (int64_t)pixelBytes,
                (size_t)w * pixelBytes);
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
        XMemcpy(dbuf + (int64_t)row * bpl, sbuf + (int64_t)row * bpl, (size_t)bpl);
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
        stride = (size_t)XImageFormat_bytesPerLine(bw, xpbs_surfaceFormat());
        if (stride == 0 || (size_t)bh > SIZE_MAX / stride) return 0;
        return stride * (size_t)bh;
    }
#else
    stride = (size_t)XImageFormat_bytesPerLine(size->width,
                                                xpbs_surfaceFormat());
    if (stride == 0 || (size_t)size->height > SIZE_MAX / stride) return 0;
    return stride * (size_t)size->height;
#endif
}

static void xpbs_initConfiguredImage(XImage* image, int width, int height,
                                     void* buffer, size_t bufferSize)
{
    int stride;
    XImageFormat format = xpbs_surfaceFormat();
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
        stride = XImageFormat_bytesPerLine(width, format);
        XImage_init_ex_2(image, width, height, format,
                         stride, (uint8_t*)buffer, NULL, NULL);
        (void)bufferSize;
    }
    else
        XImage_init_ex(image, width, height, format);
}

#if XPBS_TILE_BATCHING_ON
/* ==================== tile 攒批内部工具（PARTIAL 专属） ==================== */

/** @brief 当前毫秒时钟。为什么经 XDateTime 平台适配而不直接触平台 API：
 *  本文件契约不含任何平台头（与 Drive 时钟后端解耦），共同层既有惯例是
 *  经 XDateTime_*SecsSinceEpoch 取时间（XGpuRenderDriver_gl.c 计时同款）。 */
static int64_t xpbs_batchNowMs(void)
{
    return XDateTime_currentMSecsSinceEpoch();
}

/** @brief 攒批预算：半宽×半高（≈1/4 屏面积），下限抬到一片 tile。
 *  为什么按外接矩形宽高而不是累计面积判定：present 源是攒批缓冲上的矩形
 *  区域，外接矩形尺寸直接决定缓冲容量上限（对标 LVGL partial 对单次
 *  flush 区域的封顶，避免攒批缓冲随窗口无界增长）。 */
static void xpbs_batchBudget(const XPlatformBackingStore* self,
                             int* outW, int* outH)
{
    int tileW = XGUI_BACKINGSTORE_PARTIAL_BUFFER_WIDTH;
    int tileH = XGUI_BACKINGSTORE_PARTIAL_BUFFER_HEIGHT;
    int w = self->m_size.width;
    int h = self->m_size.height;
    int bw = (w + 1) / 2;
    int bh = (h + 1) / 2;
    /* 下限 = 一片 tile（裁到窗口内）：小窗口半宽/半高不足单片时抬到
       单片，保证首片总能入批、攒批不至于整体不可用；上限天然 ≤ 窗口。 */
    if (bw < tileW) bw = tileW;
    if (bh < tileH) bh = tileH;
    if (bw > w && w > 0) bw = w;
    if (bh > h && h > 0) bh = h;
    *outW = bw;
    *outH = bh;
}

/** @brief 确保攒批缓冲按当前预算就绪（懒分配；窗口变大后按需重建）。
 *  @return false 表示低内存等分配失败，调用方退化为逐片即时上屏。 */
static bool xpbs_batchEnsureBuffer(XPlatformBackingStore* self)
{
    int bw, bh;
    xpbs_batchBudget(self, &bw, &bh);
    if (bw <= 0 || bh <= 0) return false;
    if (self->m_batchImage.m_data &&
        XImage_width(&self->m_batchImage) >= bw &&
        XImage_height(&self->m_batchImage) >= bh)
        return true;
    XImage_deinit_base(&self->m_batchImage);
    XImage_init(&self->m_batchImage);
    XImage_init_ex(&self->m_batchImage, bw, bh, xpbs_surfaceFormat());
    return self->m_batchImage.m_data != NULL;
}

/** @brief 16ms 帧界是否已到。墙钟回拨（now < startMs）按未到期处理：
 *  攒批只是延迟策略，帧末 flushPendingTiles 兜底最终一致性，不依赖时钟
 *  单调性（对标 QElapsedTimer 用途而共同层仅有墙钟毫秒的现实约束）。 */
static bool xpbs_batchDeadlinePassed(const XPlatformBackingStore* self)
{
    int64_t now;
    if (!self->m_batchOpen) return false;
    now = xpbs_batchNowMs();
    if (now < self->m_batchStartMs) return false;
    return (now - self->m_batchStartMs) >= XPBS_TILE_BATCH_FRAME_MS;
}

/** @brief 把已攒批次整体上屏并复位批次状态（缓冲保留复用）。
 *  @note  present 只送 m_batchRegion 登记过的矩形：region 之外的缓冲像素
 *         从未被写入，按集合上屏保证不把未攒批内容带出（平台 Driver 以
 *         (region, offset) 逐矩形搬运，天然支持多矩形一次提交）。 */
static void xpbs_batchPresent(XPlatformBackingStore* self, XWindow* window)
{
    XPoint origin;
    if (!self->m_batchOpen || XRegion_isEmpty(&self->m_batchRegion))
    {
        self->m_batchOpen = false;
        return;
    }
    origin.x = self->m_batchBounds.x;
    origin.y = self->m_batchBounds.y;
    XPlatformBackingStoreDriver_presentTile(self->m_nativeState, window,
                                            &self->m_batchImage,
                                            &self->m_batchRegion, &origin);
    xpbs_invokePresent(self, &self->m_batchRegion, &origin);
    self->m_batchOpen = false;
    XRegion_clear(&self->m_batchRegion);
}

/** @brief 丢弃攒批中的待上屏内容并释放缓冲（resize 换代时使用）。
 *  @note  resize 无 window 入参无法上屏；PARTIAL tile 缓冲本就不保留旧
 *         内容，resize 后由控件层整帧 repaint 覆盖，故此处直接丢弃而不
 *         是丢失一致性（旧内容随窗口尺寸失效，上屏反而错位）。 */
static void xpbs_batchReset(XPlatformBackingStore* self)
{
    self->m_batchOpen = false;
    XRegion_clear(&self->m_batchRegion);
    XRect_init(&self->m_batchBounds, 0, 0, 0, 0);
    self->m_batchStartMs = 0;
    if (self->m_batchImage.m_data)
    {
        XImage_deinit_base(&self->m_batchImage);
        XImage_init(&self->m_batchImage);
    }
}

/** @brief 开批或并批：把 tile 窗口矩形并入当前批次外接矩形。
 *  @return false 表示攒批缓冲不可用（仅开批时可能），调用方应退化为
 *          即时上屏；true 表示 batchBounds/region 已包含该 tile。 */
static bool xpbs_batchAccumulate(XPlatformBackingStore* self,
                                 const XRect* presented)
{
    if (!self->m_batchOpen)
    {
        if (!xpbs_batchEnsureBuffer(self)) return false;
        XRegion_clear(&self->m_batchRegion);
        self->m_batchBounds = *presented;
        self->m_batchStartMs = xpbs_batchNowMs();
        self->m_batchOpen = true;
        return true;
    }
    self->m_batchBounds = XRect_united(&self->m_batchBounds, presented);
    return true;
}
#endif /* XPBS_TILE_BATCHING_ON */

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
#if XPBS_TILE_BATCHING_ON
    XImage_init(&store->m_batchImage);
    XRegion_init(&store->m_batchRegion);
    XRect_init(&store->m_batchBounds, 0, 0, 0, 0);
    store->m_batchStartMs = 0;
    store->m_batchOpen = false;
#endif
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
#if XPBS_TILE_BATCHING_ON
    if (self->m_batchImage.m_data)
        XImage_deinit_base(&self->m_batchImage);
    XRegion_deinit(&self->m_batchRegion);
#endif
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
        if (!self->m_nativeBufferMode)
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
    /* native 零拷贝模式单缓冲：active 恒指向 m_image，不翻转。 */
    if (!self->m_nativeBufferMode)
        self->m_activeIndex ^= 1u;
#endif
}

/** @brief flushTile 的即时上屏主体（逐片 present；攒批的关闭/退化路径
 *  共用同一份簿记，保证两种路径的生命周期语义逐位一致）。 */
static void xpbs_flushTileImmediate(XPlatformBackingStore* self,
                                    XWindow* window,
                                    const XRect* tileRect,
                                    const XPoint* offset)
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
    if (!self->m_nativeBufferMode)
        self->m_activeIndex ^= 1u;
#endif
}

void XPlatformBackingStore_flushTile(XPlatformBackingStore* self, XWindow* window,
                                      const XRect* tileRect, const XPoint* offset)
{
    xpbs_flushTileImmediate(self, window, tileRect, offset);
}

void XPlatformBackingStore_flushTileBatched(XPlatformBackingStore* self,
                                            XWindow* window,
                                            const XRect* tileRect,
                                            const XPoint* offset)
{
#if XPBS_TILE_BATCHING_ON
    XImage* image;
    XPoint origin;
    XRect presented;
    XRect grown;
    XRect merged;
    XRect hit;
    int budgetW, budgetH;
    bool connected;
    if (!self || !tileRect || !(image = xpbs_activeImage(self)) ||
        !image->m_data || tileRect->width <= 0 || tileRect->height <= 0)
        return;
    origin.x = tileRect->x;
    origin.y = tileRect->y;
    if (offset) { origin.x += offset->x; origin.y += offset->y; }
    presented.x = origin.x;
    presented.y = origin.y;
    presented.width = tileRect->width;
    presented.height = tileRect->height;
    /* 邻接/重叠判定：tile 外扩 1px 后与批次外接矩形相交即视为连片
       （共边或重叠，对标 Qt 脏区合并的相邻矩形并集）；离散跳跃的新
       tile 先收旧批，避免外接矩形夹带大片未绘制区域。 */
    grown = XRect_adjusted(&presented, -1, -1, 1, 1);
    connected = self->m_batchOpen &&
                xpbs_intersect(&self->m_batchBounds, &grown, &hit);
    if (connected)
    {
        merged = XRect_united(&self->m_batchBounds, &presented);
        xpbs_batchBudget(self, &budgetW, &budgetH);
        /* 超预算（1/4 屏）或 16ms 帧界到点：先上屏旧批再开新批。 */
        if (merged.width > budgetW || merged.height > budgetH ||
            xpbs_batchDeadlinePassed(self))
            xpbs_batchPresent(self, window);
    }
    else if (self->m_batchOpen)
    {
        /* 不相邻：已攒内容到此为止，本片另起新批。 */
        xpbs_batchPresent(self, window);
    }
    if (!xpbs_batchAccumulate(self, &presented))
    {
        /* 攒批缓冲分配失败（低内存）：退化为逐片即时上屏，最终一致性
           不依赖攒批能力是否可用。 */
        xpbs_flushTileImmediate(self, window, tileRect, offset);
        return;
    }
    /* tile 内容并入攒批缓冲：源坐标恒从 (0,0) 起（flushTile 同款契约），
       目标按批次外接矩形原点平移；present 按 m_batchRegion 集合搬运，
       未登记的缓冲像素不会被读出，无脏数据外带。 */
    xpbs_copyRectPixels(image, 0, 0, &self->m_batchImage,
                        presented.x - self->m_batchBounds.x,
                        presented.y - self->m_batchBounds.y,
                        presented.width, presented.height);
    XRegion_addRect(&self->m_batchRegion, &presented);
    self->m_tileActive = false;
#if XGUI_BACKINGSTORE_BUFFER_COUNT > 1
    /* 与即时路径同款缓冲轮转（PARTIAL 无 native 零拷贝，保持逐位一致的
       双缓冲生命周期语义）。 */
    if (!self->m_nativeBufferMode)
        self->m_activeIndex ^= 1u;
#endif
#else
    /* 攒批关闭：请求攒批退化为逐片即时上屏（调用方无需感知开关）。 */
    xpbs_flushTileImmediate(self, window, tileRect, offset);
#endif
}

void XPlatformBackingStore_flushPendingTiles(XPlatformBackingStore* self,
                                             XWindow* window)
{
#if XPBS_TILE_BATCHING_ON
    if (!self) return;
    /* 显式边界（帧末/焦点变化/定时器帧界）强制收批：最终一致性的兜底。 */
    xpbs_batchPresent(self, window);
#else
    (void)self;
    (void)window;
#endif
}

bool XPlatformBackingStore_hasPendingTiles(const XPlatformBackingStore* self)
{
#if XPBS_TILE_BATCHING_ON
    return self && self->m_batchOpen && !XRegion_isEmpty(&self->m_batchRegion);
#else
    (void)self;
    return false;
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
#if XPBS_TILE_BATCHING_ON
    /* 尺寸真正变化才作废攒批：resize 无 window 入参无法上屏，且旧内容
       随窗口尺寸换代失效（PARTIAL tile 缓冲本就不保留旧帧），后续整帧
       repaint 会覆盖；不丢弃会在新预算下按失效坐标 present。 */
    xpbs_batchReset(self);
#endif
    /* 零拷贝模式：平台驱动提供可直接绘制的共享内存（Win32 DIB
       section）。绘制 XImage 以它为存储，flush 只剩一次 BitBlt，
       省去 XImage→DIB 整帧 memcpy。驱动无此能力时回落自分配。 */
#if XGUI_BACKINGSTORE_RENDER_MODE != XGUI_BACKINGSTORE_RENDER_MODE_PARTIAL
    if (w > 0 && h > 0 && self->m_nativeState && !self->m_externalBuffers)
    {
        /* 关键顺序：getNativeBuffer 内部会释放旧 DIB，必须先把旧内容
           深拷贝快照（XCopy 是 COW 共享，不解决悬垂），再查询新缓冲。 */
        XImage snapshot;
        bool haveSnap = false;
        XImage_init(&snapshot);
        active = xpbs_activeImage(self);
        if (active && active->m_data)
            haveSnap = xpbs_deepCopy(active, &snapshot);
        {
            size_t stride = 0;
            void* bits = XPlatformBackingStoreDriver_getNativeBuffer(
                self->m_nativeState, w, h, &stride);
            /* stride 下限按当前表面格式的每像素字节数校验（ARGB32 为
               w*4，RGB16 为 w*2；协商格式按位深推导）；平台驱动给不出
               足够行距时视为拒绝，回落自分配路径。 */
            if (bits && stride >= (size_t)w * xpbs_pixelBytes(xpbs_surfaceFormat()))
            {
                XImage nativeImage;
                XImage_init_ex_2(&nativeImage, w, h, xpbs_surfaceFormat(),
                                 (int)stride, (uint8_t*)bits, NULL, NULL);
                if (nativeImage.m_data)
                {
                    if (haveSnap)
                    {
                        int ow2 = XImage_width(&snapshot);
                        int oh2 = XImage_height(&snapshot);
                        int cw = ow2 < w ? ow2 : w;
                        int ch = oh2 < h ? oh2 : h;
                        if (cw > 0 && ch > 0)
                            xpbs_blitFromSnapshot(&snapshot, 0, 0,
                                                  &nativeImage,
                                                  0, 0, cw, ch);
                    }
                    /* m_image 共享 native 视图（外部缓冲，m_ownsData=
                       false：unref 不释放 DIB，归 driver 管理）。 */
                    XImage_deinit_base(&self->m_image);
                    XImage_init(&self->m_image);
                    XCopy(&self->m_image, &nativeImage);
                    XImage_deinit_base(&nativeImage);
#if XGUI_BACKINGSTORE_BUFFER_COUNT > 1
                    /* native 模式单缓冲：第二缓冲置空（inactive 不用）。 */
                    XImage_deinit_base(&self->m_image2);
                    XImage_init(&self->m_image2);
#endif
                    self->m_nativeBufferMode = true;
                    self->m_size.width = w;
                    self->m_size.height = h;
                    /* 与常规路径同款的簿记（不含 surfaceResized：
                       native 缓冲已由 getNativeBuffer 保证，重建反而
                       会使 m_image 的存储悬垂）。 */
                    self->m_activeIndex = 0u;
                    self->m_tileCursorX = 0;
                    self->m_tileCursorY = 0;
                    self->m_tileActive = false;
                    XRegion_clear(&self->m_flushRegion);
                    XRegion_init(&cropped);
                    xpbs_clipRegion(&self->m_staticContents, w, h, &cropped);
                    XRegion_copy(&cropped, &self->m_staticContents);
                    XRegion_deinit(&cropped);
                    XImage_deinit_base(&snapshot);
                    return;
                }
                XImage_deinit_base(&nativeImage);
            }
        }
        XImage_deinit_base(&snapshot);
        /* 驱动拒绝该尺寸（罕见）：native 标志复位，走自分配路径。 */
        self->m_nativeBufferMode = false;
    }
    if (self->m_nativeBufferMode && w > 0 && h > 0)
    {
        /* 驱动拒绝该尺寸（罕见）：回落自分配前清标志。 */
        self->m_nativeBufferMode = false;
    }
#endif
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
    /* 通知平台重建表面（Win32 DIB/DC）。传入尺寸必须与实际缓冲一致
       （头文件契约：PARTIAL 模式为 tile 缓冲尺寸）：Win32 侧按传入
       尺寸建 DIB，PARTIAL 下若传整窗 w/h 会建出整窗 DIB 而绘制图像
       只有 tile 大，白白浪费内存。此处与上方 tile 分配（633-637 附近
       的 min(w,160)×min(h,80)）同源取 min。DIRECT/FULL 的缓冲就是
       整窗，仍传 w/h 不变。 */
#if XGUI_BACKINGSTORE_RENDER_MODE == XGUI_BACKINGSTORE_RENDER_MODE_PARTIAL
    {
        int surfaceW = w < XGUI_BACKINGSTORE_PARTIAL_BUFFER_WIDTH ?
                       w : XGUI_BACKINGSTORE_PARTIAL_BUFFER_WIDTH;
        int surfaceH = h < XGUI_BACKINGSTORE_PARTIAL_BUFFER_HEIGHT ?
                       h : XGUI_BACKINGSTORE_PARTIAL_BUFFER_HEIGHT;
        XPlatformBackingStoreDriver_surfaceResized(self->m_nativeState,
                                                   surfaceW, surfaceH);
    }
#else
    XPlatformBackingStoreDriver_surfaceResized(self->m_nativeState, w, h);
#endif
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
