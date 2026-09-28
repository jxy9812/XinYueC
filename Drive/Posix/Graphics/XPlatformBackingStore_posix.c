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
#include "XGuiApplication.h" /* topLevelWindows：fbdev 遮挡剔除取 Z 序。 */

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

/** @brief 行带同步收窄（弹层 30Hz 重绘 CPU 飙升根修 2026-09-28）：
 *  present 原先每次 cacheSync(Clean, NULL, 0)=整幅 1.2MB 映射 msync
 *  （A33 实测 ~30ms/次）。独立弹层窗（菜单）打开期间以 ~30Hz 自续
 *  重绘，30×30ms ≈ 76% 单核全部烧在全帧 cacheflush 上。本 helper 按
 *  实际写入范围收窄同步；一次性失败诊断语义与整幅版一致。 */
static bool xpbs_cacheSyncRangeWarnOnce(const XPlatformDisplayDriverOps* ops,
                                        void* address, size_t length)
{
    bool ok = ops->cacheSync(XPlatformDisplayCache_Clean, address, length);
    if (!ok && !g_xpbsCacheSyncWarned)
    {
        g_xpbsCacheSyncWarned = true;
        fprintf(stderr,
                "[fbdev] WARNING: display driver cacheSync reported "
                "NOT-synced (ranged call). On cache-coherent/uncached "
                "memory this is harmless; see board-level cacheSync note "
                "in xpbs_cacheSyncWarnOnce.\n");
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
/** @brief 弹层遮挡剔除收集上限（超出降级为不剔除，不丢帧）。 */
#define XPBS_OCCLUDER_MAX 8
/** @brief 单脏矩形遮挡切分工作集上限（超出降级为按原矩形上屏）。 */
#define XPBS_OCCL_WORK_MAX 32

/** @brief 把一个已按图像/面板范围裁剪的窗口局部矩形按行 memcpy 进 fb
 *  映射（原 xpbs_presentToDisplayDriver 脏区循环体的矩形部分，抽出
 *  供遮挡剔除后的多条带复用）。调用方保证 wx0>=off->x、wy0>=off->y、
 *  fb 坐标已入面板界。 */
static void xpbs_presentRectRows(const XImage* image, const XPoint* off,
                                 const XPoint* origin,
                                 const XPlatformDisplayInfo* info,
                                 int writeIndex, const uint8_t* srcBase,
                                 size_t pixelBytes, int imgBpl,
                                 int wx0, int wy0, int wx1, int wy1,
                                 size_t* outSyncLo, size_t* outSyncHi,
                                 bool* outSyncAny)
{
    const uint8_t* src;
    uint8_t* dst;
    uint8_t* dstLast;
    size_t rowBytes;
    int fx0 = wx0 + origin->x;
    int fy0 = wy0 + origin->y;
    int y;
    (void)image;
    src = srcBase + (int64_t)(wy0 - off->y) * imgBpl +
          (int64_t)(wx0 - off->x) * (int64_t)pixelBytes;
    dst = (uint8_t*)info->m_frameBuffer +
          (size_t)(writeIndex * info->m_height + fy0) * info->m_stride +
          (size_t)fx0 * pixelBytes;
    dstLast = dst + (size_t)(wy1 - 1 - wy0) * info->m_stride +
              (size_t)(wx1 - wx0) * pixelBytes;
    rowBytes = (size_t)(wx1 - wx0) * pixelBytes;
    for (y = wy0; y < wy1; ++y)
    {
        XMemcpy(dst, src, rowBytes);
        src += imgBpl;
        dst += info->m_stride;
    }
    /* 记录本矩形实际写入的 fb 字节范围（含行距间隙），供收窄同步。 */
    if (outSyncLo && outSyncHi && outSyncAny)
    {
        if (!*outSyncAny || (size_t)(dst - rowBytes) < *outSyncLo)
            *outSyncLo = (size_t)(dst - rowBytes);
        if (!*outSyncAny || (size_t)dstLast > *outSyncHi)
            *outSyncHi = (size_t)dstLast;
        *outSyncAny = true;
    }
}

static bool xpbs_presentToDisplayDriver(const XImage* image,
                                        const XRegion* region,
                                        const XPoint* offset,
                                        const XPoint* fbOrigin,
                                        XWindow* window)
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
    XRect occluders[XPBS_OCCLUDER_MAX];
    int occluderCount;
    size_t syncLo = 0;
    size_t syncHi = 0;
    bool syncAny = false;
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
    /* fbdev 遮挡剔除（弹层低频闪烁根修 2026-09-28）：fbdev 无合成器，
     * 同屏各顶层窗口共用可见 fb，后提交者覆盖先者。主窗口整窗 present
     * （FULL 模式每次 PAINT 恒整窗重绘+整窗提交）会把打开中的更高层
     * 瞬态弹层（菜单/下拉）区域一并覆盖——弹层直到自身下次重绘才浮现，
     * 随主窗重绘节拍明灭（HUD 1Hz tick ⇒ 菜单 1Hz 闪烁，昆仑通态 A33
     * 真机实测）。按「登记序在本窗之后的可见顶层=更高层」定序（与
     * XWindow_setVisible 弹层暴露恢复同约定）收集其全局几何矩形，
     * 下方脏区逐矩形剔除这些区域后再落笔。 */
    {
        occluderCount = 0;
        if (window)
        {
            XVector* tops = XGuiApplication_topLevelWindows();
            if (tops)
            {
                int selfIndex = -1;
                size_t topCount = XVector_size_base(tops);
                size_t ti;
                for (ti = 0; ti < topCount; ++ti)
                {
                    if (*(XWindow* const*)XVector_at_base(tops,
                                                          (int64_t)ti) == window)
                    {
                        selfIndex = (int)ti;
                        break;
                    }
                }
                for (ti = (size_t)(selfIndex + 1);
                     selfIndex >= 0 && ti < topCount; ++ti)
                {
                    XWindow* above =
                        *(XWindow**)XVector_at_base(tops, (int64_t)ti);
                    XRect g;
                    int gx1;
                    int gy1;
                    if (!above || !XWindow_isVisible(above)) continue;
                    g = XWindow_geometry(above);
                    gx1 = g.x + g.width;
                    gy1 = g.y + g.height;
                    if (g.x < 0) { g.width += g.x; g.x = 0; }
                    if (g.y < 0) { g.height += g.y; g.y = 0; }
                    if (gx1 > info.m_width) gx1 = info.m_width;
                    if (gy1 > info.m_height) gy1 = info.m_height;
                    if (gx1 <= g.x || gy1 <= g.y) continue;
                    if (occluderCount < XPBS_OCCLUDER_MAX)
                    {
                        occluders[occluderCount].x = g.x;
                        occluders[occluderCount].y = g.y;
                        occluders[occluderCount].width = gx1 - g.x;
                        occluders[occluderCount].height = gy1 - g.y;
                        ++occluderCount;
                    }
                }
                XVector_delete_base(tops);
            }
        }
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
            /* 清零也是写：行带同步范围扩到整段映射。 */
            syncLo = 0;
            syncHi = info.m_frameBufferSize;
            syncAny = true;
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
            /* 遮挡剔除打开时强制整搬：FULL 整窗脏区本可跳过整搬
             * （coversPanel 优化），但剔除后弹层矩形不再被主窗内容
             * 覆写，两缓冲的弹层矩形只能靠整搬保持一致——跳过会退化
             * 成「两缓冲各留一份旧内容、随翻页交替」的弹层闪烁。 */
            if (!coversPanel || occluderCount > 0)
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
                /* 整搬写满写入缓冲：行带同步范围扩到整个写入缓冲。 */
                syncLo = (size_t)g_xpbsFbWriteIndex * info.m_height * info.m_stride;
                syncHi = syncLo + (size_t)info.m_height * info.m_stride;
                syncAny = true;
            }
        }
    }
    for (i = 0; i < region->count; ++i)
    {
        const XRect* rect = &region->rects[i];
        int wx0, wy0, wx1, wy1;
        if (!rect || rect->width <= 0 || rect->height <= 0) continue;
        /* 图像范围约束（图像坐标 = 窗口坐标 - offset，见 X11 present）。 */
        wx0 = rect->x;          wy0 = rect->y;
        wx1 = rect->x + rect->width; wy1 = rect->y + rect->height;
        if (wx0 < off->x) wx0 = off->x;
        if (wy0 < off->y) wy0 = off->y;
        if (wx1 > off->x + XImage_width(image))  wx1 = off->x + XImage_width(image);
        if (wy1 > off->x + XImage_height(image)) wy1 = off->y + XImage_height(image);
        /* 面板范围约束：fb 像素坐标 = 窗口全局位置(origin) + 窗口坐标
         * （主窗口恒在原点；弹层等非原点顶层窗口落到其实际屏幕位置）。
         * 双缓冲面板的可见高度被驱动钳到 yres——后台缓冲在
         * [yres, yres_virtual) 区段，写入行 y 需加 writeIndex*yres 偏移
         * 进入对应缓冲（0 号缓冲偏移 0，逐帧交替写入互不覆盖）。 */
        {
            int fx0 = wx0 + origin->x;
            int fy0 = wy0 + origin->y;
            int fx1 = wx1 + origin->x;
            int fy1 = wy1 + origin->y;
            if (fx0 < 0) { wx0 -= fx0; fx0 = 0; }
            if (fy0 < 0) { wy0 -= fy0; fy0 = 0; }
            if (fx1 > info.m_width)  fx1 = info.m_width;
            if (fy1 > info.m_height) fy1 = info.m_height;
            wx1 = fx1 - origin->x;
            wy1 = fy1 - origin->y;
        }
        if (wx1 <= wx0 || wy1 <= wy0) continue;
        if (occluderCount > 0)
        {
            /* 遮挡剔除：在全局/fb 坐标系按各高层弹层矩形把本脏矩形切
             * 分为周边条带，仅上屏未被遮挡的部分（工作集溢出则降级为
             * 按原矩形整块上屏——退回旧行为，绝不丢帧）。 */
            XRect work[XPBS_OCCL_WORK_MAX];
            int wn = 1;
            int oi;
            bool truncated = false;
            work[0].x = wx0 + origin->x;
            work[0].y = wy0 + origin->y;
            work[0].width = wx1 - wx0;
            work[0].height = wy1 - wy0;
            for (oi = 0; oi < occluderCount && !truncated; ++oi)
            {
                XRect next[XPBS_OCCL_WORK_MAX];
                int nn = 0;
                int wi2;
                const XRect* o = &occluders[oi];
                for (wi2 = 0; wi2 < wn; ++wi2)
                {
                    const XRect* r = &work[wi2];
                    int rx1 = r->x + r->width;
                    int ry1 = r->y + r->height;
                    int ix0 = r->x > o->x ? r->x : o->x;
                    int iy0 = r->y > o->y ? r->y : o->y;
                    int ix1 = rx1 < o->x + o->width ? rx1 : o->x + o->width;
                    int iy1 = ry1 < o->y + o->height ? ry1 : o->y + o->height;
                    if (ix1 <= ix0 || iy1 <= iy0)
                    {
                        /* 不相交：整段保留。 */
                        if (nn < XPBS_OCCL_WORK_MAX) next[nn++] = *r;
                        else truncated = true;
                        continue;
                    }
                    /* 相交：保留上下左右四条带（均不含遮挡矩形）。 */
                    if (iy0 > r->y)
                    {
                        if (nn < XPBS_OCCL_WORK_MAX)
                        {
                            next[nn].x = r->x; next[nn].y = r->y;
                            next[nn].width = r->width;
                            next[nn].height = iy0 - r->y;
                            ++nn;
                        }
                        else truncated = true;
                    }
                    if (iy1 < ry1)
                    {
                        if (nn < XPBS_OCCL_WORK_MAX)
                        {
                            next[nn].x = r->x; next[nn].y = iy1;
                            next[nn].width = r->width;
                            next[nn].height = ry1 - iy1;
                            ++nn;
                        }
                        else truncated = true;
                    }
                    if (ix0 > r->x)
                    {
                        if (nn < XPBS_OCCL_WORK_MAX)
                        {
                            next[nn].x = r->x; next[nn].y = iy0;
                            next[nn].width = ix0 - r->x;
                            next[nn].height = iy1 - iy0;
                            ++nn;
                        }
                        else truncated = true;
                    }
                    if (ix1 < rx1)
                    {
                        if (nn < XPBS_OCCL_WORK_MAX)
                        {
                            next[nn].x = ix1; next[nn].y = iy0;
                            next[nn].width = rx1 - ix1;
                            next[nn].height = iy1 - iy0;
                            ++nn;
                        }
                        else truncated = true;
                    }
                }
                if (!truncated)
                {
                    int ci;
                    for (ci = 0; ci < nn; ++ci) work[ci] = next[ci];
                    wn = nn;
                }
            }
            if (!truncated)
            {
                int wi2;
                for (wi2 = 0; wi2 < wn; ++wi2)
                {
                    const XRect* r = &work[wi2];
                    if (r->width <= 0 || r->height <= 0) continue;
                    xpbs_presentRectRows(image, off, origin, &info,
                                         writeIndex, srcBase, pixelBytes,
                                         imgBpl,
                                         r->x - origin->x,
                                         r->y - origin->y,
                                         r->x - origin->x + r->width,
                                         r->y - origin->y + r->height,
                                         &syncLo, &syncHi, &syncAny);
                }
                continue;
            }
            /* 截断降级：落到底部按未剔除原矩形整块上屏。 */
        }
        xpbs_presentRectRows(image, off, origin, &info, writeIndex,
                             srcBase, pixelBytes, imgBpl,
                             wx0, wy0, wx1, wy1,
                             &syncLo, &syncHi, &syncAny);
    }
    /* DMA scanout 前 cache clean——行带收窄版：只同步本次实际写入的
     * fb 字节范围（独立弹层窗 ~30Hz 自续重绘下，整幅 msync 每次 ~30ms
     * 是 CPU 飙升的主项；收窄后单次亚毫秒）。无写入则跳过。 */
    if (syncAny)
        xpbs_cacheSyncRangeWarnOnce(ops,
                                    (uint8_t*)info.m_frameBuffer + syncLo,
                                    syncHi - syncLo);
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

void XPlatformBackingStore_requestPanelClear(void)
{
#if XGUI_ON && XPLATFORM_FBDEV_ON
    /* 无 WM 的 fbdev：顶层窗口几何变化（最大化切换/移动）后，旧几何
       区域没有任何窗口重绘覆盖，残留像素会长期留在屏上。置回首帧
       标记，下次 present 先整段映射清零（两缓冲同清无失步），窗口
       内容随后正常绘制——等价 WM 换桌面的整屏重铺。注意：本实现会
       把「正在显示的缓冲」一并打黑（memset 即见），逐帧调用表现为
       整屏频闪（昆仑通态 A33 拖动实测）——拖动等高频路径请改用
       XPlatformBackingStore_fillPanelRects 只清暴露条带。 */
    g_xpbsFbFirstPresent = true;
#else
    /* 非 fbdev 平台由窗口系统重铺：no-op。 */
#endif
}

void XPlatformBackingStore_fillPanelRects(const XRect* rects, int count,
                                          uint32_t nativePixel)
{
#if XGUI_ON && XPLATFORM_FBDEV_ON
    /* 无 WM 的 fbdev 高频几何变化路径（标题栏拖拽移动）：只把窗口
       移开后暴露出来的条带立即填色（两缓冲同填，双缓冲不失步），
       不触碰其余显示内容——对比 requestPanelClear 的整屏 memset，
       无黑屏中间态，无频闪。nativePixel 为面板原生像素值（按面板
       格式逐像素写入；RGB565 黑=0x0000）。 */
    const XPlatformDisplayDriverOps* ops = XPlatformDisplayDriver_active();
    XPlatformDisplayInfo info;
    XImageFormat panel = XImageFormat_Invalid;
    size_t pixelBytes;
    int i;
    if (!ops || !ops->probe || !ops->probe(&info) || !info.m_frameBuffer ||
        info.m_stride == 0 || info.m_width < 1 || info.m_height < 1)
        return;
    if (!ops->formatNegotiate(XImageFormat_RGB16, &panel) ||
        panel != XImageFormat_RGB16)
        return; /* 仅承诺与直写同款 RGB565 面板；其余格式维持旧行为。 */
    pixelBytes = 2;
    for (i = 0; i < count; ++i)
    {
        const XRect* r = &rects[i];
        int x0;
        int y0;
        int x1;
        int y1;
        int x;
        int y;
        if (!r || r->width <= 0 || r->height <= 0) continue;
        x0 = r->x < 0 ? 0 : r->x;
        y0 = r->y < 0 ? 0 : r->y;
        x1 = r->x + r->width > info.m_width ? info.m_width : r->x + r->width;
        y1 = r->y + r->height > info.m_height ? info.m_height
                                              : r->y + r->height;
        if (x1 <= x0 || y1 <= y0) continue;
        for (y = y0; y < y1; ++y)
        {
            uint8_t* row = (uint8_t*)info.m_frameBuffer +
                           (size_t)y * info.m_stride;
            for (x = x0; x < x1; ++x)
            {
                uint8_t* px = row + (size_t)x * pixelBytes;
                px[0] = (uint8_t)(nativePixel & 0xFF);
                px[1] = (uint8_t)((nativePixel >> 8) & 0xFF);
            }
        }
        /* 双缓冲面板：同步填后台缓冲，翻页不失步。 */
        if (info.m_doubleBuffered)
        {
            for (y = y0; y < y1; ++y)
            {
                uint8_t* row = (uint8_t*)info.m_frameBuffer +
                               (size_t)(info.m_height + y) * info.m_stride;
                for (x = x0; x < x1; ++x)
                {
                    uint8_t* px = row + (size_t)x * pixelBytes;
                    px[0] = (uint8_t)(nativePixel & 0xFF);
                    px[1] = (uint8_t)((nativePixel >> 8) & 0xFF);
                }
            }
        }
    }
#else
    (void)rects; (void)count; (void)nativePixel;
#endif
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
    if (xpbs_presentToDisplayDriver(image, region, offset, &fbOrigin, window))
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
    if (xpbs_presentToDisplayDriver(image, region, offset, &fbOrigin, window))
        return;
#endif
    if (window && XPlatformNativeWindow_winId(window) != 0)
        XPlatformNativeWindow_present(window, image, region, offset);
}

#endif /* XBACKINGSTORE_ON && XPLATFORMBACKINGSTORE_ON && defined(__linux__) */
#endif /* XBACKINGSTORE_ON && XPLATFORMBACKINGSTORE_ON */
