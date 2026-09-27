/******************************************************************************
 * @file       XPlatformBackingStore_posix.c
 * @brief      Linux XPlatformBackingStore 平台提交驱动。
 * @details    Linux 后端的全部软件缓冲逻辑（双缓冲 XImage、resize/滚动/
 *             静态内容/tile 遍历/外部缓冲）已上提到公共层
 *             Src/XGui/Platform/XPlatformBackingStore.c；本文件只提供
 *             「把脏区提交到真实显示目标」的 Driver 钩子，提交优先级：
 *             - 显示驱动直写（XPLATFORM_FBDEV_ON=1 且注册了活动驱动，
 *               见 XPlatformDisplayDriver.h 消费链）：后备表面格式与
 *               面板扫描格式一致（formatNegotiate 可直写）时，把脏区
 *               经 memcpy 写入驱动帧缓冲映射（行距用驱动 stride——
 *               硬件对齐可能大于 width*bpp），随后 cacheSync（DMA
 *               scanout 前 clean）+ pan（FBIOPAN_DISPLAY，FB_ACTIVATE_
 *               VBL 在垂直消隐生效防撕裂）；X11 路径完全跳过；
 *             - 窗口已挂接真实原生窗口（WId 非 0）时，经
 *               XPlatformNativeWindow_present（XPutImage）上屏；
 *             - 否则 no-op（公共层统一触发的 present 回调交给显示驱动）。
 *             直写不可行（无活动驱动/格式不符/probe 失败）时逐条回落，
 *             桌面默认（未注册驱动）行为与既有实现逐位一致。
 *             本文件不包含任何 Linux API 头，因此同一实现也可在需要时
 *             迁往其它软件光栅化平台。
 * @note       文件同时受 XBACKINGSTORE_ON 与 XPLATFORMBACKINGSTORE_ON 总
 *             开关约束（Drive 平台后端惯例）。
 ******************************************************************************/

#include "XPlatformBackingStore.h"

#if XBACKINGSTORE_ON && XPLATFORMBACKINGSTORE_ON

#if defined(__linux__)

#include "XMemory.h" /* XMemcpy（fbdev 直写行拷贝；对标全库 XMem* 纪律 */
#include <stdio.h>
#include <string.h>

#include "XImageFormat.h"
#include "XPlatformDisplayDriver.h"
#include "XPlatformNativeWindow.h"
#include "XWindow.h"

#if XGUI_ON && XPLATFORM_FBDEV_ON

/* ==================== 显示驱动直写（消费点 2：present 提交） ==================== */

/** @brief cacheSync 失败诊断已打印标志（一次性，防逐帧刷屏）。 */
static bool g_xpbsCacheSyncWarned;

/** @brief fbdev 双缓冲写入缓冲号（0/1 轮换；pan 成功后翻转，见
 *  xpbs_presentToDisplayDriver——单缓冲面板恒写 0 号）。 */
static int g_xpbsFbWriteIndex;
/** @brief fbdev 首帧标记：首帧 present 前整段帧缓冲映射清零一次，
 *  同屏前任应用的残留帧不外泄（见 xpbs_presentToDisplayDriver 同步段）。 */
static bool g_xpbsFbFirstPresent = true;

/**
 * @brief      显示驱动 cache 同步 + 一次性失败诊断。
 * @details    cacheSync 返回 false 表示驱动未真正完成 CPU cache 同步
 *             （fbdev 模板占位即如此——msync 不等于 DCache clean）：
 *             一致性/uncached 内存无需同步（无害），非一致性 cached
 *             映射则会显示旧数据。只打一次诊断指明修复路径，不逐帧
 *             刷屏；返回值透传给调用方留决策空间（当前策略：继续
 *             present——画面仍会更新，最坏是旧数据，不因同步缺失
 *             拒绝上屏）。
 */
static bool xpbs_cacheSyncWarnOnce(const XPlatformDisplayDriverOps* ops)
{
    bool ok = ops->cacheSync(XPlatformDisplayCache_Clean, NULL, 0);
    if (!ok && !g_xpbsCacheSyncWarned)
    {
        g_xpbsCacheSyncWarned = true;
        fprintf(stderr,
                "[fbdev] WARNING: display driver cacheSync reported "
                "NOT-synced (template placeholder does not clean CPU "
                "DCache). On cache-coherent/uncached memory this is "
                "harmless; on non-coherent cached mappings the display "
                "may show stale lines. Fix: register a board-level "
                "cacheSync via XPlatformDisplayDriver_register "
                "(cacheflush/dma_sync).\n");
    }
    return ok;
}

/**
 * @brief      把脏区从后备表面直写进活动显示驱动的帧缓冲映射。
 * @details    前置条件（任一不满足即返回 false 回落平台既有路径）：
 *             活动驱动存在、后备格式==面板扫描格式（preferred 对照经
 *             formatNegotiate，直写不做任何像素转换）、probe 给出映射
 *             地址与行距。坐标语义与 X11 present 一致：region 为窗口
 *             坐标，图像坐标 = 窗口坐标 - offset；fb 像素坐标 = 窗口
 *             全局位置(fbOrigin) + 窗口坐标——主窗口恒在面板原点，
 *             弹层等非原点顶层窗口经 fbOrigin 落到正确屏幕位置。逐矩形
 *             裁剪到「图像范围 ∩ 面板范围」后按行 memcpy（目标行距用驱动
 *             stride），全部写完后 cacheSync(Clean) + pan(0)。
 * @note       pan 失败不回滚（单缓冲直写本已可见；双缓冲驱动 pan 失败
 *             属驱动故障，下帧重试）。cacheSync 按契约对 NULL 地址同步
 *             整幅帧缓冲（模板实现为尽力 msync，一致性内存即时返回）。
 * @return     true 已直写并提交；false 未触碰 fb（调用方回落）。
 */
static bool xpbs_presentToDisplayDriver(const XImage* image,
                                        const XRegion* region,
                                        const XPoint* offset,
                                        const XPoint* fbOrigin)
{
    const XPlatformDisplayDriverOps* ops = XPlatformDisplayDriver_active();
    XPlatformDisplayInfo info;
    XImageFormat panel = XImageFormat_Invalid;
    XPoint zero;
    const XPoint* off;
    const XPoint* origin;
    const uint8_t* srcBase;
    size_t pixelBytes;
    int imgBpl;
    int i;
    int writeIndex;
    if (!ops || !image || !image->m_data || !region) return false;
    /* 直写仅当后备格式即面板扫描格式（preferred==面板 → true）。 */
    if (!ops->formatNegotiate(XImage_format(image), &panel) ||
        panel != XImage_format(image))
        return false;
    if (!ops->probe(&info) || !info.m_frameBuffer ||
        info.m_stride == 0 || info.m_width < 1 || info.m_height < 1)
        return false;
    pixelBytes = (size_t)((XImageFormat_bitDepth(panel) + 7) / 8);
    if (pixelBytes == 0) return false;
    srcBase = XImage_constBits(image);
    imgBpl = XImage_bytesPerLine(image);
    if (!srcBase || imgBpl <= 0) return false;
    /* 真零拷贝判定：native 模式下公共层传入的 image 本就架在 fb 映射上
       （getNativeBuffer 返回了按窗口位置偏移的映射视图），painter 写入
       即已落在该窗口的屏幕区域，无需也无法再 memcpy——只做 cacheSync
       提交。区分依据：数据指针命中映射区间。 */
    if (srcBase >= (const uint8_t*)info.m_frameBuffer &&
        srcBase < (const uint8_t*)info.m_frameBuffer + info.m_frameBufferSize)
    {
        /* 双缓冲轮换状态未用（native 恒单缓冲 0 号）；pan(0) 收敛。 */
        xpbs_cacheSyncWarnOnce(ops);
        ops->pan(0);
        return true;
    }
    /* 双缓冲轮换：写入与上次呈现不同的后台缓冲，pan 到该缓冲——对未
     * 变化的 yoffset 发 pan 多数驱动直接返回、不等待，恒 pan(0) 的
     * "名义翻页"不产生防撕裂效果；轮换 + FB_ACTIVATE_VBL 才是真正的
     * 双缓冲提交（对标 LVGL linux fbdev 驱动 flush 后交换绘制缓冲）。
     * 单缓冲面板恒写 0 号（直写即可见）。 */
    writeIndex = g_xpbsFbWriteIndex;
    if (!info.m_doubleBuffered)
        writeIndex = 0; /* 单缓冲直写：恒写可见 0 号，绝不轮换——否则
                         * pan(1) 失败后写入索引停驻，帧会永远写进
                         * 不可见的后台区段。 */
    if (offset)
        off = offset;
    else
    {
        XPoint_init(&zero, 0, 0);
        off = &zero;
    }
    {
        XPoint originZero;
        XPoint_init(&originZero, 0, 0);
        origin = fbOrigin ? fbOrigin : &originZero;
    }
    /* 双缓冲内容同步（闪烁根修）：轮换写意味着写入缓冲平日只收脏区新
     * 像素，其余区域停留在更早帧甚至初始垃圾——直接翻页，可见画面会在
     * 「上一帧」与「新脏区+陈旧内容」间交替（真机表现为整窗闪烁）。
     * 故翻页前先把当前可见缓冲整帧搬入写入缓冲，下方脏区循环随后原地
     * 覆盖，保证两缓冲除脏区外逐位一致。脏区已覆盖整屏时整搬可跳过；
     * 首帧先把整段映射清零，同屏前任应用的帧缓冲残留不会从任一缓冲
     * 漏出。整搬成本一次约一帧面（1024x600x2 ≈ 1.2MB），只写脏区的
     * 收益保留在绘制侧，提交侧换来任意时刻两缓冲皆完整可显。 */
    if (info.m_doubleBuffered)
    {
        if (g_xpbsFbFirstPresent)
        {
            memset((uint8_t*)info.m_frameBuffer, 0, info.m_frameBufferSize);
            g_xpbsFbFirstPresent = false;
        }
        else
        {
            bool coversPanel = false;
            for (i = 0; i < region->count; ++i)
            {
                const XRect* rect = &region->rects[i];
                if (!rect || rect->width <= 0 || rect->height <= 0) continue;
                if (rect->x <= off->x && rect->y <= off->y &&
                    rect->x + rect->width >= off->x + XImage_width(image) &&
                    rect->y + rect->height >= off->y + XImage_height(image) &&
                    off->x <= 0 && off->y <= 0 &&
                    XImage_width(image) >= info.m_width &&
                    XImage_height(image) >= info.m_height)
                {
                    coversPanel = true;
                    break;
                }
            }
            if (!coversPanel)
            {
                const uint8_t* srcRows = (const uint8_t*)info.m_frameBuffer +
                    (size_t)(g_xpbsFbWriteIndex ^ 1) * info.m_height * info.m_stride;
                uint8_t* dstRows = (uint8_t*)info.m_frameBuffer +
                    (size_t)g_xpbsFbWriteIndex * info.m_height * info.m_stride;
                int sy;
                for (sy = 0; sy < info.m_height; ++sy)
                    XMemcpy(dstRows + (size_t)sy * info.m_stride,
                            srcRows + (size_t)sy * info.m_stride,
                            info.m_stride);
            }
        }
    }
    for (i = 0; i < region->count; ++i)
    {
        const XRect* rect = &region->rects[i];
        const uint8_t* src;
        uint8_t* dst;
        size_t rowBytes;
        int wx0, wy0, wx1, wy1, y;
        int fx0, fy0, fx1, fy1;
        if (!rect || rect->width <= 0 || rect->height <= 0) continue;
        /* 图像范围约束（图像坐标 = 窗口坐标 - offset，见 X11 present）。 */
        wx0 = rect->x;          wy0 = rect->y;
        wx1 = rect->x + rect->width; wy1 = rect->y + rect->height;
        if (wx0 < off->x) wx0 = off->x;
        if (wy0 < off->y) wy0 = off->y;
        if (wx1 > off->x + XImage_width(image))  wx1 = off->x + XImage_width(image);
        if (wy1 > off->y + XImage_height(image)) wy1 = off->y + XImage_height(image);
        /* 面板范围约束：fb 像素坐标 = 窗口全局位置(origin) + 窗口坐标
         * （主窗口恒在原点；弹层等非原点顶层窗口落到其实际屏幕位置）。
         * 双缓冲面板的可见高度被驱动钳到 yres——后台缓冲在
         * [yres, yres_virtual) 区段，写入行 y 需加 writeIndex*yres 偏移
         * 进入对应缓冲（0 号缓冲偏移 0，逐帧交替写入互不覆盖）。 */
        fx0 = wx0 + origin->x;
        fy0 = wy0 + origin->y;
        fx1 = wx1 + origin->x;
        fy1 = wy1 + origin->y;
        if (fx0 < 0) { wx0 -= fx0; fx0 = 0; }
        if (fy0 < 0) { wy0 -= fy0; fy0 = 0; }
        if (fx1 > info.m_width)  fx1 = info.m_width;
        if (fy1 > info.m_height) fy1 = info.m_height;
        wx1 = fx1 - origin->x;
        wy1 = fy1 - origin->y;
        if (wx1 <= wx0 || wy1 <= wy0) continue;
        src = srcBase + (int64_t)(wy0 - off->y) * imgBpl +
              (int64_t)(wx0 - off->x) * (int64_t)pixelBytes;
        dst = (uint8_t*)info.m_frameBuffer +
              (size_t)(writeIndex * info.m_height + fy0) * info.m_stride +
              (size_t)fx0 * pixelBytes;
        rowBytes = (size_t)(wx1 - wx0) * pixelBytes;
        for (y = wy0; y < wy1; ++y)
        {
            XMemcpy(dst, src, rowBytes);
            src += imgBpl;
            dst += info.m_stride;
        }
    }
    /* DMA scanout 前 cache clean（一次性失败诊断，见函数注释；
     * 非一致性 cached 板子须注册板级覆盖，见驱动模板 cacheSync 注释）。 */
    xpbs_cacheSyncWarnOnce(ops);
    /* 翻页提交：pan 到刚写入的缓冲（单缓冲 no-op）；成功后轮换写索引。
     * pan 失败不回滚（内容已在目标缓冲，下帧重试同号缓冲覆盖写）。 */
    if (ops->pan(writeIndex))
        g_xpbsFbWriteIndex ^= 1;
    return true;
}

#endif /* XGUI_ON && XPLATFORM_FBDEV_ON */

/* ==================== 平台提交驱动（对标 XPlatformGraphicsDriver_*） ==================== */

bool XPlatformBackingStoreDriver_create(void** outState, XWindow* window)
{
    /* 记住所属窗口：getNativeBuffer 需要窗口全局位置来计算其 fb
     * 视图落点（弹层等非原点窗口按实际屏幕位置开偏移视图）。 */
    if (outState) *outState = window;
    /* 软件缓冲始终可用；真实提交按运行期窗口挂接情况决定。 */
    return true;
}

void XPlatformBackingStoreDriver_destroy(void* nativeState)
{
    (void)nativeState;
}

void XPlatformBackingStoreDriver_setNativeTarget(void* nativeState,
                                                 void* nativeWindow)
{
    (void)nativeState; (void)nativeWindow;
}

void* XPlatformBackingStoreDriver_getNativeBuffer(void* nativeState,
                                                  int width, int height,
                                                  size_t* outStride)
{
    (void)nativeState; (void)width; (void)height; (void)outStride;
    /* fbdev 不提供 native 直写缓冲（影子缓冲架构）：painter 若直接写可见
     * fb，"背景填充→逐控件重绘"的中间态会被扫描输出实时看到——真机表现
     * 为点击白屏一闪、悬浮层高频闪烁（昆仑通态 A7 2026-09-27 实测）。
     * 统一走自分配影子缓冲：离屏合成完整帧后由 present 按脏区+窗口位置
     * 搬运上屏，屏幕只见到最终结果。 */
    return NULL;
}

void XPlatformBackingStoreDriver_surfaceResized(void* nativeState,
                                                int width, int height)
{
    (void)nativeState; (void)width; (void)height;
}

/** @brief 取顶层窗口的全局位置作为 fb 落笔原点（主窗口恒 (0,0)）。
 *  fbdev 无窗口系统，弹层等非原点顶层窗口的屏幕落点需显式偏移，
 *  否则会按局部坐标画到面板左上角（弹层错位根因）。 */
static void xpbs_windowFbOrigin(XWindow* window, XPoint* out)
{
    XPoint_init(out, 0, 0);
    if (window)
    {
        XRect geometry = XWindow_geometry(window);
        out->x = geometry.x;
        out->y = geometry.y;
    }
}

void XPlatformBackingStoreDriver_present(void* nativeState, XWindow* window,
                                         const XImage* image,
                                         const XRegion* region,
                                         const XPoint* offset, bool full)
{
    XPoint fbOrigin;
    (void)nativeState; (void)full;
#if XGUI_ON && XPLATFORM_FBDEV_ON
    /* 消费点 2：活动显示驱动可直写时 memcpy+cacheSync+pan 上屏，X11
     * 路径完全跳过；格式不符或驱动不可用返回 false 回落 X11（零回归）。 */
    xpbs_windowFbOrigin(window, &fbOrigin);
    if (xpbs_presentToDisplayDriver(image, region, offset, &fbOrigin))
        return;
#endif
    if (window && XPlatformNativeWindow_winId(window) != 0)
        XPlatformNativeWindow_present(window, image, region, offset);
}

void XPlatformBackingStoreDriver_presentTile(void* nativeState,
                                             XWindow* window,
                                             const XImage* image,
                                             const XRegion* region,
                                             const XPoint* offset)
{
    XPoint fbOrigin;
    (void)nativeState;
#if XGUI_ON && XPLATFORM_FBDEV_ON
    /* tile 提交与整幅 present 同一直写路径（offset 语义一致：窗口坐标
       - offset = 图像坐标，tile 图像恒从 (0,0) 起）。 */
    xpbs_windowFbOrigin(window, &fbOrigin);
    if (xpbs_presentToDisplayDriver(image, region, offset, &fbOrigin))
        return;
#endif
    if (window && XPlatformNativeWindow_winId(window) != 0)
        XPlatformNativeWindow_present(window, image, region, offset);
}

#endif /* XBACKINGSTORE_ON && XPLATFORMBACKINGSTORE_ON && defined(__linux__) */
#endif /* XBACKINGSTORE_ON && XPLATFORMBACKINGSTORE_ON */
