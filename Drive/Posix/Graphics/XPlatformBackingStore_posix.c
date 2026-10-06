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
/** @brief 差带同步账本（CPU 根修 2026-09-28）：上一帧实际落笔的面板
 *  坐标矩形集——即「当前写入缓冲相对可见缓冲仍缺失的内容」。轮换写
 *  语义下，写入缓冲平日只收脏区新像素，其余区域停留在更早帧；翻页
 *  前只需把这份差额从可见缓冲搬入写入缓冲（成本 ∝ 上一帧脏区，替代
 *  原每帧整搬 1.2MB+cacheflush——小交互 CPU 与整页重绘同级的根因）。
 *  记录取剔除前的脏矩形（含被弹层遮挡剔除的部分）：这些行带从可见
 *  缓冲同步时会把弹层像素一并带入两缓冲，保持遮挡剔除批次的防闪烁
 *  语义不变。pan 失败不回滚（下帧同缓冲重写，账本仍成立）。 */
static XRegion g_xpbsFbBackDirty;
static bool g_xpbsFbBackDirtyInit = false;

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

/** @brief 差带同步：把可见缓冲中 rect 行带搬入写入缓冲（fb→fb 行拷贝，
 *  供轮换写前补齐写入缓冲缺失内容；坐标为面板像素坐标，调用方保证
 *  已入面板界）。同步范围计入行带 cacheflush 账本。 */
static void xpbs_syncRectRows(const XPlatformDisplayInfo* info,
                              int writeIndex, const XRect* rect,
                              size_t pixelBytes,
                              size_t* outSyncLo, size_t* outSyncHi,
                              bool* outSyncAny)
{
    const uint8_t* src;
    uint8_t* dst;
    size_t rowBytes;
    int y;
    src = (const uint8_t*)info->m_frameBuffer +
          (size_t)((writeIndex ^ 1) * info->m_height + rect->y) *
              info->m_stride +
          (size_t)rect->x * pixelBytes;
    dst = (uint8_t*)info->m_frameBuffer +
          (size_t)(writeIndex * info->m_height + rect->y) * info->m_stride +
          (size_t)rect->x * pixelBytes;
    rowBytes = (size_t)rect->width * pixelBytes;
    for (y = 0; y < rect->height; ++y)
    {
        XMemcpy(dst, src, rowBytes);
        src += info->m_stride;
        dst += info->m_stride;
    }
    if (outSyncLo && outSyncHi && outSyncAny)
    {
        size_t lo = (size_t)((writeIndex * info->m_height + rect->y) *
                             info->m_stride) +
                    (size_t)rect->x * pixelBytes;
        size_t hi = (size_t)((writeIndex * info->m_height + rect->y +
                              rect->height - 1) * info->m_stride) +
                    (size_t)(rect->x + rect->width) * pixelBytes;
        if (!*outSyncAny || lo < *outSyncLo) *outSyncLo = lo;
        if (!*outSyncAny || hi > *outSyncHi) *outSyncHi = hi;
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
    /* 【memhunt fbdev 通道 2026-10-06】位深一致性守卫：直写语义=后备图
     * 像格式即面板扫描格式。驱动替身/缺陷驱动 negotiate 恒认可时，协
     * 商位深与 probe 报告 m_bitsPerPixel 失配会让行宽按失配像深计算、
     * 寻址按驱动行距（16bpp 口径）——行重叠+越界直写（ASan 实证：替
     * 身认 32bpp 后备，写穿 16bpp 双缓冲 RAM 面板末行，global-buffer-
     * overflow，XGuiDialogMove_Test 桌面缺省 RGB32 构建首帧即爆）。位
     * 深失配一律回落 X11/软件路径；真驱动 negotiate 只认本位深格式，
     * 守卫恒过（零回归）。 */
    if (info.m_bitsPerPixel > 0 &&
        (int)info.m_bitsPerPixel != XImageFormat_bitDepth(panel))
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
                XClassDelete(tops);
            }
        }
    }
    /* 双缓冲差带同步（闪烁根修的 CPU 版，2026-09-28）：轮换写意味着
     * 写入缓冲平日只收脏区新像素，其余区域停留在更早帧甚至初始垃圾
     * ——直接翻页，可见画面会在「上一帧」与「新脏区+陈旧内容」间
     * 交替（真机表现为整窗闪烁）。原修法每帧整搬一帧面（1024x600x2
     * ≈1.2MB memcpy + 全带 cacheflush），小交互的提交成本与整页重绘
     * 同级（昆仑通态 A33 实测交互期 CPU 42%，与 FULL 37% 无差）。
     * 现改为差带账本：只把「写入缓冲仍缺失的内容」=上一帧实际落笔
     * 矩形（g_xpbsFbBackDirty）从可见缓冲搬入，成本 ∝ 脏区面积；
     * 账本记剔除前脏矩形，弹层行带随同步进入两缓冲，遮挡剔除批次的
     * 防闪烁语义保持。脏区已盖整屏（FULL 整窗帧）时同步可跳过；首帧
     * 先把整段映射清零，同屏前任应用的残留不外泄。 */
    if (info.m_doubleBuffered)
    {
        if (g_xpbsFbFirstPresent)
        {
            memset((uint8_t*)info.m_frameBuffer, 0, info.m_frameBufferSize);
            g_xpbsFbFirstPresent = false;
            if (g_xpbsFbBackDirtyInit) XRegion_clear(&g_xpbsFbBackDirty);
            /* 清零也是写：行带同步范围扩到整段映射。 */
            syncLo = 0;
            syncHi = info.m_frameBufferSize;
            syncAny = true;
        }
        else
        {
            bool coversPanel = false;
            /* 整屏快转判定加 occluderCount==0 门槛（拖动白块战役
             * 2026-10-05 离屏测试实证第二缺陷）：遮挡剔除在位时，整帧
             * present 只写未遮挡条带、弹层矩形行带不落笔——此时跳过差
             * 带同步，等于把「写入缓冲弹层矩形=陈旧内容」直接翻上屏
             * （弹层矩形整片回跳陈旧内容，无后续补绘则长留）。快转
             * 「脏区已盖整屏→同步可跳」的前提只在真写满整屏（无剔除）
             * 时成立；有弹层在位时照走账本同步。 */
            if (occluderCount == 0)
            {
                for (i = 0; i < region->count; ++i)
                {
                    const XRect* rect = &region->rects[i];
                    if (!rect || rect->width <= 0 || rect->height <= 0)
                        continue;
                    if (rect->x <= off->x && rect->y <= off->y &&
                        rect->x + rect->width >=
                            off->x + XImage_width(image) &&
                        rect->y + rect->height >=
                            off->y + XImage_height(image) &&
                        off->x <= 0 && off->y <= 0 &&
                        XImage_width(image) >= info.m_width &&
                        XImage_height(image) >= info.m_height)
                    {
                        coversPanel = true;
                        break;
                    }
                }
            }
            /* 差带账本在位且本帧未盖整屏：把缺失矩形逐条搬入写入缓冲
             * （两缓冲弹层矩形一致性由此保持——账本含被剔除矩形，弹层
             * 行带每帧随同步传播，无「各留一份旧内容交替闪」退化）。 */
            if (!coversPanel && g_xpbsFbBackDirtyInit)
            {
                /* 账本裁冗（2026-09-30 拖拽 60 档迭代，昆仑通态 A33）：
                 * 账本=上一帧落笔行带，本帧写区即将整片覆盖两者交叠部
                 * 分——先搬后盖是双倍 fb 写带宽（拖拽移动步：账本≈旧位
                 * 整窗、写区≈新位整窗，实测差带补齐 ~0.96MB/步纯冗余，
                 * 分相 post 相 31.6~34.6ms 的主项之一）。按弹层遮挡切
                 * 分同款工作集做「账本 \ 本帧写区」矩形差分，只把真正
                 * 缺失的余料搬入写入缓冲；工作集溢出降级为按账本原矩
                 * 形整块搬（退回旧行为，绝不欠同步）。
                 * 遮挡剔除在位（occluderCount>0）时差分不安全（拖动白
                 * 块战役 2026-10-05 离屏全帧断言实证）：写区差分按剔除
                 * 前矩形扣账，而实际落笔是剔除后条带——弹层矩形被差
                 * 分扣掉却不被写，翻页即回跳陈旧内容（主窗整帧直提后
                 * 弹层矩形 2832px 回跳父窗内容实证）。有剔除在位一律
                 * 按账本原矩形整块搬（同溢出降级路径）：弹层行带必同
                 * 步，冗余只限账本行带面积（写区即将覆盖的部分白搬一
                 * 遍，上限=上一帧脏区）。 */
                XRect fbWrite[XPBS_OCCL_WORK_MAX];
                XRect subWork[XPBS_OCCL_WORK_MAX];
                XRect subNext[XPBS_OCCL_WORK_MAX];
                XRect* subFrom = subWork;
                XRect* subTo = subNext;
                int fbWriteCount = 0;
                int subCount = 0;
                int wi;
                int fi;
                bool subTruncated = occluderCount > 0;
                /* 本帧写区 → fb 像素坐标（与下方账本更新同一裁剪链：
                 * 图像范围 ∩ 面板范围），供差分求交。 */
                for (i = 0; i < region->count &&
                            fbWriteCount < XPBS_OCCL_WORK_MAX; ++i)
                {
                    const XRect* rect = &region->rects[i];
                    int wx0, wy0, wx1, wy1;
                    int fx0, fy0, fx1, fy1;
                    if (!rect || rect->width <= 0 || rect->height <= 0)
                        continue;
                    wx0 = rect->x;
                    wy0 = rect->y;
                    wx1 = rect->x + rect->width;
                    wy1 = rect->y + rect->height;
                    if (wx0 < off->x) wx0 = off->x;
                    if (wy0 < off->y) wy0 = off->y;
                    if (wx1 > off->x + XImage_width(image))
                        wx1 = off->x + XImage_width(image);
                    if (wy1 > off->y + XImage_height(image))
                        wy1 = off->y + XImage_height(image);
                    fx0 = wx0 + origin->x;
                    fy0 = wy0 + origin->y;
                    fx1 = wx1 + origin->x;
                    fy1 = wy1 + origin->y;
                    if (fx0 < 0) fx0 = 0;
                    if (fy0 < 0) fy0 = 0;
                    if (fx1 > info.m_width) fx1 = info.m_width;
                    if (fy1 > info.m_height) fy1 = info.m_height;
                    if (fx1 <= fx0 || fy1 <= fy0) continue;
                    fbWrite[fbWriteCount].x = fx0;
                    fbWrite[fbWriteCount].y = fy0;
                    fbWrite[fbWriteCount].width = fx1 - fx0;
                    fbWrite[fbWriteCount].height = fy1 - fy0;
                    ++fbWriteCount;
                }
                if (fbWriteCount >= XPBS_OCCL_WORK_MAX)
                    subTruncated = true; /* 写区超表：降级整块搬。 */
                if (!subTruncated)
                {
                    for (i = 0; i < g_xpbsFbBackDirty.count; ++i)
                    {
                        const XRect* rect = &g_xpbsFbBackDirty.rects[i];
                        if (!rect || rect->width <= 0 || rect->height <= 0)
                            continue;
                        if (subCount >= XPBS_OCCL_WORK_MAX)
                        {
                            subTruncated = true;
                            break;
                        }
                        subFrom[subCount++] = *rect;
                    }
                }
                if (!subTruncated)
                {
                    for (fi = 0; fi < fbWriteCount; ++fi)
                    {
                        const XRect* c = &fbWrite[fi];
                        int nn = 0;
                        for (wi = 0; wi < subCount; ++wi)
                        {
                            const XRect* r = &subFrom[wi];
                            int rx1 = r->x + r->width;
                            int ry1 = r->y + r->height;
                            int ix0 = r->x > c->x ? r->x : c->x;
                            int iy0 = r->y > c->y ? r->y : c->y;
                            int ix1 = rx1 < c->x + c->width
                                          ? rx1 : c->x + c->width;
                            int iy1 = ry1 < c->y + c->height
                                          ? ry1 : c->y + c->height;
                            if (ix1 <= ix0 || iy1 <= iy0)
                            {
                                /* 不相交：整段保留。 */
                                if (nn < XPBS_OCCL_WORK_MAX)
                                    subTo[nn++] = *r;
                                else
                                {
                                    subTruncated = true;
                                    break;
                                }
                                continue;
                            }
                            /* 相交：保留上下左右四条带（均不含本帧写
                             * 区），与遮挡剔除切分同款。 */
                            if (iy0 > r->y)
                            {
                                if (nn < XPBS_OCCL_WORK_MAX)
                                {
                                    subTo[nn].x = r->x;
                                    subTo[nn].y = r->y;
                                    subTo[nn].width = r->width;
                                    subTo[nn].height = iy0 - r->y;
                                    ++nn;
                                }
                                else
                                {
                                    subTruncated = true;
                                    break;
                                }
                            }
                            if (iy1 < ry1)
                            {
                                if (nn < XPBS_OCCL_WORK_MAX)
                                {
                                    subTo[nn].x = r->x;
                                    subTo[nn].y = iy1;
                                    subTo[nn].width = r->width;
                                    subTo[nn].height = ry1 - iy1;
                                    ++nn;
                                }
                                else
                                {
                                    subTruncated = true;
                                    break;
                                }
                            }
                            if (ix0 > r->x)
                            {
                                if (nn < XPBS_OCCL_WORK_MAX)
                                {
                                    subTo[nn].x = r->x;
                                    subTo[nn].y = iy0;
                                    subTo[nn].width = ix0 - r->x;
                                    subTo[nn].height = iy1 - iy0;
                                    ++nn;
                                }
                                else
                                {
                                    subTruncated = true;
                                    break;
                                }
                            }
                            if (ix1 < rx1)
                            {
                                if (nn < XPBS_OCCL_WORK_MAX)
                                {
                                    subTo[nn].x = ix1;
                                    subTo[nn].y = iy0;
                                    subTo[nn].width = rx1 - ix1;
                                    subTo[nn].height = iy1 - iy0;
                                    ++nn;
                                }
                                else
                                {
                                    subTruncated = true;
                                    break;
                                }
                            }
                        }
                        if (subTruncated) break;
                        {
                            XRect* swap = subFrom;
                            subFrom = subTo;
                            subTo = swap;
                            subCount = nn;
                        }
                    }
                }
                for (i = 0; i < (subTruncated ? g_xpbsFbBackDirty.count
                                              : subCount); ++i)
                {
                    const XRect* rect = subTruncated
                        ? &g_xpbsFbBackDirty.rects[i] : &subFrom[i];
                    if (!rect || rect->width <= 0 || rect->height <= 0)
                        continue;
                    xpbs_syncRectRows(&info, writeIndex, rect, pixelBytes,
                                      &syncLo, &syncHi, &syncAny);
                }
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
        /* 【memhunt fbdev 通道 2026-10-06】原图像底界钳制误用 off->x 作
         * y 基线（off->x>off->y 时漏钳：图像底缘脏矩形源读越界 +
         * 面板界以下落笔，与 450/452、771/773 两处同式对齐）。 */
        if (wy1 > off->y + XImage_height(image)) wy1 = off->y + XImage_height(image);
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
    /* 差带账本更新：本帧落笔的脏矩形（剔除前面板裁剪版）成为下一帧
     * 写入缓冲的缺失内容。仅双缓冲轮换路径记账；本帧写入范围同时并入
     * cacheflush 行带账（下方 range 同步一次覆盖同步+落笔两段）。 */
    if (info.m_doubleBuffered)
    {
        if (!g_xpbsFbBackDirtyInit)
        {
            XRegion_init(&g_xpbsFbBackDirty);
            g_xpbsFbBackDirtyInit = true;
        }
        XRegion_clear(&g_xpbsFbBackDirty);
        for (i = 0; i < region->count; ++i)
        {
            const XRect* rect = &region->rects[i];
            XRect fbRect;
            int fx0, fy0, fx1, fy1;
            int wx0, wy0, wx1, wy1;
            if (!rect || rect->width <= 0 || rect->height <= 0) continue;
            wx0 = rect->x;
            wy0 = rect->y;
            wx1 = rect->x + rect->width;
            wy1 = rect->y + rect->height;
            if (wx0 < off->x) wx0 = off->x;
            if (wy0 < off->y) wy0 = off->y;
            if (wx1 > off->x + XImage_width(image))
                wx1 = off->x + XImage_width(image);
            if (wy1 > off->y + XImage_height(image))
                wy1 = off->y + XImage_height(image);
            fx0 = wx0 + origin->x;
            fy0 = wy0 + origin->y;
            fx1 = wx1 + origin->x;
            fy1 = wy1 + origin->y;
            if (fx0 < 0) fx0 = 0;
            if (fy0 < 0) fy0 = 0;
            if (fx1 > info.m_width) fx1 = info.m_width;
            if (fy1 > info.m_height) fy1 = info.m_height;
            if (fx1 <= fx0 || fy1 <= fy0) continue;
            fbRect.x = fx0;
            fbRect.y = fy0;
            fbRect.width = fx1 - fx0;
            fbRect.height = fy1 - fy0;
            XRegion_addRect(&g_xpbsFbBackDirty, &fbRect);
        }
    }
    /* DMA scanout 前 cache clean——行带收窄版：只同步本次实际写入的
     * fb 字节范围（独立弹层窗 ~30Hz 自续重绘下，整幅 msync 每次 ~30ms
     * 是 CPU 飙升的主项；收窄后单次亚毫秒）。无写入则跳过。 */
    if (syncAny)
    {
        xpbs_cacheSyncRangeWarnOnce(ops,
                                    (uint8_t*)info.m_frameBuffer + syncLo,
                                    syncHi - syncLo);
    }
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

void XPlatformBackingStore_blitPanelRects(XPlatformBackingStore* src,
                                          const XRect* rects, int count,
                                          const XPoint* origin)
{
#if XGUI_ON && XPLATFORM_FBDEV_ON
    /* 让位条带归位还原（拖动对话框白块根修 2026-10-05）：fillPanelRects
     * 的「内容来自另一顶层后备缓冲」孪生版。无 WM 的 fbdev 弹层拖动
     * 让出的条带住着父窗内容，逐帧路径既不能 requestPanelClear（整屏
     * 频闪前科）也不能无条件填桌面底色（洗掉父窗=真机白块），更不能
     * 逐帧给归属顶层注入整窗 expose（EXPOSE 处理恒整窗重合成，60 档
     * 拖拽烧穿 A33 单核）——把归属顶层的既有合成结果按条带直搬两缓
     * 冲，语义=窗口系统「移开遮挡即露出下层内容」。契约与 fillPanel
     * Rects 完全同款：同步双缓冲直写（写入即见）、不碰差带账本与翻
     * 页状态、无 pan、无 cacheSync（同 fill 口径）；格式守卫与 present
     * 直写同款（源图像格式==面板扫描格式，否则 no-op 维持旧行为）。 */
    const XPlatformDisplayDriverOps* ops;
    XPlatformDisplayInfo info;
    XImageFormat panel = XImageFormat_Invalid;
    XImage* image;
    const uint8_t* srcBase;
    size_t pixelBytes;
    int imgBpl;
    int imgW;
    int imgH;
    int i;
    if (!src || !rects || count <= 0 || !origin) return;
    ops = XPlatformDisplayDriver_active();
    if (!ops || !ops->probe || !ops->probe(&info) || !info.m_frameBuffer ||
        info.m_stride == 0 || info.m_width < 1 || info.m_height < 1)
        return;
    image = XPlatformBackingStore_paintDevice(src);
    if (!image || !image->m_data) return;
    if (!ops->formatNegotiate(XImage_format(image), &panel) ||
        panel != XImage_format(image))
        return;
    pixelBytes = (size_t)((XImageFormat_bitDepth(panel) + 7) / 8);
    if (pixelBytes == 0) return;
    srcBase = XImage_constBits(image);
    imgBpl = XImage_bytesPerLine(image);
    imgW = XImage_width(image);
    imgH = XImage_height(image);
    if (!srcBase || imgBpl <= 0) return;
    for (i = 0; i < count; ++i)
    {
        const XRect* r = &rects[i];
        /* 双向钳制：先图像范围（图像坐标=矩形-origin），再面板范围；
         * 源指针按钳后坐标现算（origin 可为负=窗口悬出面板）。 */
        int sx0 = r->x - origin->x;
        int sy0 = r->y - origin->y;
        int sx1 = sx0 + r->width;
        int sy1 = sy0 + r->height;
        int x0;
        int y0;
        int x1;
        int y1;
        int y;
        size_t rowBytes;
        const uint8_t* s;
        uint8_t* d0;
        if (!r || r->width <= 0 || r->height <= 0) continue;
        if (sx0 < 0) sx0 = 0;
        if (sy0 < 0) sy0 = 0;
        if (sx1 > imgW) sx1 = imgW;
        if (sy1 > imgH) sy1 = imgH;
        x0 = sx0 + origin->x;
        y0 = sy0 + origin->y;
        x1 = sx1 + origin->x;
        y1 = sy1 + origin->y;
        if (x0 < 0) x0 = 0;
        if (y0 < 0) y0 = 0;
        if (x1 > info.m_width) x1 = info.m_width;
        if (y1 > info.m_height) y1 = info.m_height;
        if (x1 <= x0 || y1 <= y0) continue;
        s = srcBase + (int64_t)(y0 - origin->y) * imgBpl +
            (int64_t)(x0 - origin->x) * (int64_t)pixelBytes;
        rowBytes = (size_t)(x1 - x0) * pixelBytes;
        d0 = (uint8_t*)info.m_frameBuffer + (size_t)y0 * info.m_stride +
             (size_t)x0 * pixelBytes;
        for (y = y0; y < y1; ++y)
        {
            XMemcpy(d0, s, rowBytes);
            s += imgBpl;
            d0 += info.m_stride;
        }
        /* 双缓冲面板：同内容同步进后台缓冲，翻页不失步（fill 同款）。 */
        if (info.m_doubleBuffered)
        {
            s = srcBase + (int64_t)(y0 - origin->y) * imgBpl +
                (int64_t)(x0 - origin->x) * (int64_t)pixelBytes;
            d0 = (uint8_t*)info.m_frameBuffer +
                 (size_t)(info.m_height + y0) * info.m_stride +
                 (size_t)x0 * pixelBytes;
            for (y = y0; y < y1; ++y)
            {
                XMemcpy(d0, s, rowBytes);
                s += imgBpl;
                d0 += info.m_stride;
            }
        }
    }
#else
    (void)src; (void)rects; (void)count; (void)origin;
#endif
}

bool XPlatformBackingStore_blitSnapshotPanelRects(const XImage* snapshot,
                                                  const XRect* rects,
                                                  int count,
                                                  const XPoint* origin,
                                                  const XWindow* selfWindow)
{
#if XGUI_ON && XPLATFORM_FBDEV_ON
    const XPlatformDisplayDriverOps* ops;
    XPlatformDisplayInfo info;
    XImageFormat panel = XImageFormat_Invalid;
    const uint8_t* srcBase;
    size_t pixelBytes;
    int imgBpl;
    int imgW;
    int imgH;
    int i;
    int visIndex;
    int occluderCount;
    XRect occluders[XPBS_OCCLUDER_MAX];
    size_t syncLo = 0;
    size_t syncHi = 0;
    bool syncAny = false;
    if (!snapshot || !snapshot->m_data || !rects || count <= 0 || !origin)
        return false;
    ops = XPlatformDisplayDriver_active();
    if (!ops || !ops->probe || !ops->probe(&info) || !info.m_frameBuffer ||
        info.m_stride == 0 || info.m_width < 1 || info.m_height < 1)
        return false;
    /* 格式守卫与 blitPanelRects/present 直写同款：源格式==面板扫描格式。 */
    if (!ops->formatNegotiate(XImage_format(snapshot), &panel) ||
        panel != XImage_format(snapshot))
        return false;
    /* 首帧 present 前可见缓冲未定（写索引尚未经 pan 轮换确立）：
     * 拒绝直写，调用方回落既有 flush 提交路径。 */
    if (info.m_doubleBuffered && g_xpbsFbFirstPresent)
        return false;
    pixelBytes = (size_t)((XImageFormat_bitDepth(panel) + 7) / 8);
    if (pixelBytes == 0) return false;
    srcBase = XImage_constBits(snapshot);
    imgBpl = XImage_bytesPerLine(snapshot);
    imgW = XImage_width(snapshot);
    imgH = XImage_height(snapshot);
    if (!srcBase || imgBpl <= 0) return false;
    /* 目标=当前可见缓冲（与轮换写语义互补，见函数头注）：写索引恒指向
     * 下一次 present 的写入缓冲，故可见面=写索引^1（pan 成功翻转换号、
     * pan 失败不换号，两种演化下不变式均成立）；单缓冲面板恒 0 号。 */
    visIndex = info.m_doubleBuffered ? (g_xpbsFbWriteIndex ^ 1) : 0;
    /* 弹层遮挡剔除收集：与 present 直写同款（登记序在本窗之后的可见
     * 顶层=更高层）。拖动窗口扫过高层弹层时被覆盖行带不落笔，弹层像
     * 素不被窗口快照洗掉（present 遮挡剔除语义的快照版对齐）。 */
    occluderCount = 0;
    if (selfWindow)
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
                                                      (int64_t)ti) ==
                    selfWindow)
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
            XClassDelete(tops);
        }
    }
    for (i = 0; i < count; ++i)
    {
        const XRect* r = &rects[i];
        /* 双向钳制：先图像范围（图像坐标=矩形-origin），再面板范围；
         * 与 blitPanelRects 同款。 */
        int sx0 = r->x - origin->x;
        int sy0 = r->y - origin->y;
        int sx1 = sx0 + r->width;
        int sy1 = sy0 + r->height;
        int x0;
        int y0;
        int x1;
        int y1;
        size_t rowBytes;
        if (!r || r->width <= 0 || r->height <= 0) continue;
        if (sx0 < 0) sx0 = 0;
        if (sy0 < 0) sy0 = 0;
        if (sx1 > imgW) sx1 = imgW;
        if (sy1 > imgH) sy1 = imgH;
        x0 = sx0 + origin->x;
        y0 = sy0 + origin->y;
        x1 = sx1 + origin->x;
        y1 = sy1 + origin->y;
        if (x0 < 0) x0 = 0;
        if (y0 < 0) y0 = 0;
        if (x1 > info.m_width) x1 = info.m_width;
        if (y1 > info.m_height) y1 = info.m_height;
        if (x1 <= x0 || y1 <= y0) continue;
        if (occluderCount > 0)
        {
            /* 遮挡切分：在面板坐标系按各高层弹层矩形把本矩形切为周边
             * 条带，仅未被遮挡的条带落笔（与 present 同款工作集与溢出
             * 降级——溢出按原矩形整块直搬，绝不丢帧）。 */
            XRect full;
            XRect work[XPBS_OCCL_WORK_MAX];
            int wn = 1;
            int oi;
            bool truncated = false;
            int bi;
            full.x = x0;
            full.y = y0;
            full.width = x1 - x0;
            full.height = y1 - y0;
            work[0] = full;
            for (oi = 0; oi < occluderCount && !truncated; ++oi)
            {
                XRect next[XPBS_OCCL_WORK_MAX];
                int nn = 0;
                int wi;
                const XRect* o = &occluders[oi];
                for (wi = 0; wi < wn; ++wi)
                {
                    const XRect* w = &work[wi];
                    int rx1 = w->x + w->width;
                    int ry1 = w->y + w->height;
                    int ix0 = w->x > o->x ? w->x : o->x;
                    int iy0 = w->y > o->y ? w->y : o->y;
                    int ix1 = rx1 < o->x + o->width ? rx1
                                                    : o->x + o->width;
                    int iy1 = ry1 < o->y + o->height ? ry1
                                                     : o->y + o->height;
                    if (ix1 <= ix0 || iy1 <= iy0)
                    {
                        /* 不相交：整段保留。 */
                        if (nn < XPBS_OCCL_WORK_MAX) next[nn++] = *w;
                        else truncated = true;
                        continue;
                    }
                    /* 相交：保留上下左右四条带（均不含遮挡矩形）。 */
                    if (iy0 > w->y)
                    {
                        if (nn < XPBS_OCCL_WORK_MAX)
                        {
                            next[nn].x = w->x; next[nn].y = w->y;
                            next[nn].width = w->width;
                            next[nn].height = iy0 - w->y;
                            ++nn;
                        }
                        else truncated = true;
                    }
                    if (iy1 < ry1)
                    {
                        if (nn < XPBS_OCCL_WORK_MAX)
                        {
                            next[nn].x = w->x; next[nn].y = iy1;
                            next[nn].width = w->width;
                            next[nn].height = ry1 - iy1;
                            ++nn;
                        }
                        else truncated = true;
                    }
                    if (ix0 > w->x)
                    {
                        if (nn < XPBS_OCCL_WORK_MAX)
                        {
                            next[nn].x = w->x; next[nn].y = iy0;
                            next[nn].width = ix0 - w->x;
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
            /* 截断降级只搬原矩形一块（band=full）；正常路径逐条带搬。 */
            for (bi = 0; bi < (truncated ? 1 : wn); ++bi)
            {
                const XRect* band = truncated ? &full : &work[bi];
                const uint8_t* s;
                uint8_t* d;
                int y;
                if (!truncated &&
                    (band->width <= 0 || band->height <= 0))
                    continue;
                s = srcBase +
                    (int64_t)(band->y - origin->y) * imgBpl +
                    (int64_t)(band->x - origin->x) * (int64_t)pixelBytes;
                rowBytes = (size_t)band->width * pixelBytes;
                d = (uint8_t*)info.m_frameBuffer +
                    (size_t)(visIndex * info.m_height + band->y) *
                        info.m_stride +
                    (size_t)band->x * pixelBytes;
                for (y = band->y; y < band->y + band->height; ++y)
                {
                    XMemcpy(d, s, rowBytes);
                    s += imgBpl;
                    d += info.m_stride;
                }
                {
                    size_t lo = (size_t)(visIndex * info.m_height +
                                         band->y) * info.m_stride +
                                (size_t)band->x * pixelBytes;
                    size_t hi = (size_t)(visIndex * info.m_height +
                                         band->y + band->height - 1) *
                                    info.m_stride +
                                (size_t)(band->x + band->width) *
                                    pixelBytes;
                    if (!syncAny || lo < syncLo) syncLo = lo;
                    if (!syncAny || hi > syncHi) syncHi = hi;
                    syncAny = true;
                }
            }
            continue;
        }
        {
            /* 无遮挡剔除：整矩形直搬（blitPanelRects 行拷贝同款寻址）。 */
            const uint8_t* s;
            uint8_t* d;
            int y;
            s = srcBase + (int64_t)(y0 - origin->y) * imgBpl +
                (int64_t)(x0 - origin->x) * (int64_t)pixelBytes;
            rowBytes = (size_t)(x1 - x0) * pixelBytes;
            d = (uint8_t*)info.m_frameBuffer +
                (size_t)(visIndex * info.m_height + y0) * info.m_stride +
                (size_t)x0 * pixelBytes;
            for (y = y0; y < y1; ++y)
            {
                XMemcpy(d, s, rowBytes);
                s += imgBpl;
                d += info.m_stride;
            }
            {
                size_t lo = (size_t)(visIndex * info.m_height + y0) *
                                info.m_stride +
                            (size_t)x0 * pixelBytes;
                size_t hi = (size_t)(visIndex * info.m_height + y1 - 1) *
                                info.m_stride +
                            (size_t)x1 * pixelBytes;
                if (!syncAny || lo < syncLo) syncLo = lo;
                if (!syncAny || hi > syncHi) syncHi = hi;
                syncAny = true;
            }
        }
    }
    /* DMA scanout 前 cache clean——行带收窄版（只覆盖本次快照落笔范围，
     * 与 present 直写的收窄口径一致）。 */
    if (syncAny)
        xpbs_cacheSyncRangeWarnOnce(ops,
                                    (uint8_t*)info.m_frameBuffer + syncLo,
                                    syncHi - syncLo);
    /* 翻页收敛提交：pan 到刚写入的可见缓冲（同号 pan 多数驱动对未变化
     * yoffset 早退不等待）。不翻转换写索引、不触碰差带账本——另一缓冲
     * 的欠账由拖动结束的真实整窗 PAINT→flush→present 差带同步补齐。
     * pan 失败不回滚（可见性不变式不依赖本调用，见 visIndex 注）。 */
    ops->pan(visIndex);
    return true;
#else
    /* 非 fbdev 构建：no-op，调用方回落既有 flush 提交路径。 */
    (void)snapshot;
    (void)rects;
    (void)count;
    (void)origin;
    (void)selfWindow;
    return false;
#endif /* XGUI_ON && XPLATFORM_FBDEV_ON */
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
