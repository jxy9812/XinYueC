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
#include "XWindowRotation.h" /* fbdev 内容旋转换算（footprint 覆盖判据 + 逐像素 R⁻¹ 搬运）。 */
#include "XGuiApplication.h" /* topLevelWindows：fbdev 遮挡剔除取 Z 序。 */
#if XCURSOR_ON
#include "XCursor.h" /* 软件光标精灵：激活判定/逻辑外接矩形/内建箭头位图。 */
#endif

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
/** @brief 账本所属的旋转角（2026-10-18 新增；换角帧据此重映射账本）。 */
static int g_xpbsFbRot = 0;
static bool g_xpbsFbRotInit = false;

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

/**
 * @brief 本帧整幅写区是否覆盖整块面板（整屏快转判据的尺寸项）。
 * @details 【R12 2026-10-18】旋转后窗口在屏幕上的占位是**对调后**的
 *          footprint：90/270 时覆盖判据按 height↔width 互换，否则会
 *          误判（真旋转窗口未盖满却被当盖满 → 跳过差带同步 → 翻页
 *          回跳陈旧内容）。
 */
static bool xpbs_rotationCoversPanel(int rot, const XImage* image,
                                     const XPlatformDisplayInfo* info)
{
    int w = XImage_width(image);
    int h = XImage_height(image);
    if (XWindow_rotationSwapsAxes(rot)) { int t = w; w = h; h = t; }
    return w >= info->m_width && h >= info->m_height;
}

/**
 * @brief 把「已裁剪到图像范围」的窗口坐标矩形正变换为面板坐标
 *        footprint 矩形（2026-10-21 旋转定稿配套）。
 * @details rot=0 恒等（+origin、钳面板界，与既有行为逐字节一致）；
 *          90/270 宽高对调、180 点反射（XWindow_rotationMapRect 同口
 *          径）。差带账本（g_xpbsFbBackDirty）与「本帧写区」差分集合
 *          描述的都是**实际落笔**的面板区域——旋转态必须与
 *          xpbs_presentRectRowsRot 的落笔正变换同基准，否则账本按未旋
 *          转投影记账、真实 footprint 行带漏搬/错搬（稳态 90/270 翻页
 *          残影）。矩形先经调用方裁剪（图像范围 ∩ off 界），面板钳制
 *          由本函数在映射后的面板轴上做（先钳未旋转轴再映射≠先映射
 *          再钳，以后者为准）。
 */
static void xpbs_windowRectToFootprint(int rot, int logW, int logH,
                                       const XPoint* origin,
                                       const XPlatformDisplayInfo* info,
                                       int wx0, int wy0, int wx1, int wy1,
                                       XRect* out)
{
    int rw = wx1 - wx0;
    int rh = wy1 - wy0;
    int rx, ry, rwOut, rhOut;
    int fx0, fy0, fx1, fy1;
    XWindow_rotationMapRect(rot, logW, logH, &wx0, &wy0, &rw, &rh,
                            &rx, &ry, &rwOut, &rhOut);
    fx0 = rx + origin->x;
    fy0 = ry + origin->y;
    fx1 = fx0 + rwOut;
    fy1 = fy0 + rhOut;
    if (fx0 < 0) fx0 = 0;
    if (fy0 < 0) fy0 = 0;
    if (fx1 > info->m_width) fx1 = info->m_width;
    if (fy1 > info->m_height) fy1 = info->m_height;
    XRect_init(out, fx0, fy0, fx1 > fx0 ? fx1 - fx0 : 0,
               fy1 > fy0 ? fy1 - fy0 : 0);
}

/**
 * @brief 把面板（设备全局）坐标矩形逆变换为本窗局部**逻辑**矩形。
 * @details xpbs_windowRectToFootprint 的逆：遮挡剔除要把高层弹层的设备
 *          footprint 反查到本窗逻辑域才能与本窗逻辑脏区做同域切分
 *          （2026-10-22 旋转态遮挡剔除/面板钳制域修正配套）。MapRect
 *          逆：90: x=dy,y=H-dx-dw,w=dh,h=dw；180: x=W-dx-dw,y=H-dy-dh；
 *          270: x=W-dy-dh,y=dx,w=dh,h=dw；0 恒等（减原点）。
 */
static void xpbs_footprintRectToWindowRect(int rot, int logW, int logH,
                                           const XPoint* origin,
                                           int fx0, int fy0, int fw, int fh,
                                           XRect* out)
{
    int dx = fx0 - origin->x;
    int dy = fy0 - origin->y;
    switch (rot) {
    case 90:
        XRect_init(out, dy, logH - dx - fw, fh, fw);
        break;
    case 180:
        XRect_init(out, logW - dx - fw, logH - dy - fh, fw, fh);
        break;
    case 270:
        XRect_init(out, logW - dy - fh, dx, fh, fw);
        break;
    default:
        XRect_init(out, dx, dy, fw, fh);
        break;
    }
}

/**
 * @brief 把顶层窗**逻辑全局**几何矩形经旋转正映射为**面板设备**矩形
 *        （B 域；2026-10-22 弹层面板位根修配套，fbinput 侧
 *        xpfi_topLevelDeviceRect 为同口径孪生）。
 * @details 口径：顶层窗几何 xy 是桌面**逻辑全局**位（XComboBox/XMenu 等
 *          弹层定位层 2026-10-22 起统一逻辑域喂入）；fbdev 面板是固定
 *          物理扫描域，桌面逻辑域=面板宽高换轴（90/270）或同源
 *          （0/180）。主窗全屏（原点 (0,0)、尺寸=桌面）两域在原点重合，
 *          旧「geometry.xy 直作面板原点」只对主窗成立；弹层等非原点
 *          顶层必须正映射（90° 逻辑「下方」=面板「左方」），否则落板在
 *          与 opener 无几何关系的位置（真机 90° 弹层错位 (284,336)
 *          实证）。XWindow_screenFootprint 保持 A 域旧口径（原点不变仅
 *          对调宽高）服务 hideRestore/装饰拖拽等消费方，不在此翻转。
 *          rot=0 恒等（逐字节零回归）。
 * @param      rot  窗口内容有效旋转角。
 * @param      geometry  窗口逻辑全局几何（x/y=桌面逻辑位，w/h=逻辑宽高）。
 * @param      info  面板 probe 信息（m_width/m_height=物理扫描宽高）。
 * @param      out   输出面板设备矩形（不钳面板界，钳制由调用方按域做）。
 */
static void xpbs_topLevelDeviceRect(int rot, const XRect* geometry,
                                    const XPlatformDisplayInfo* info,
                                    XRect* out)
{
    int deskW;
    int deskH;
    int rx = geometry->x;
    int ry = geometry->y;
    int rw = geometry->width;
    int rh = geometry->height;
    int ox = 0;
    int oy = 0;
    int ow = 0;
    int oh = 0;
    if (XWindow_rotationSwapsAxes(rot))
    {
        deskW = info->m_height;
        deskH = info->m_width;
    }
    else
    {
        deskW = info->m_width;
        deskH = info->m_height;
    }
    XWindow_rotationMapRect(rot, deskW, deskH, &rx, &ry, &rw, &rh,
                            &ox, &oy, &ow, &oh);
    XRect_init(out, ox, oy, ow, oh);
}

/**
 * @brief 把一个已按图像/面板范围裁剪的窗口局部矩形搬进 fb 映射
 *  （原 xpbs_presentToDisplayDriver 脏区循环体的矩形部分，抽出供遮挡
 *  剔除后的多条带复用）。调用方保证 wx0>=off->x、wy0>=off->y、fb 坐标
 *  已入面板界。
 * @param      rot  内容旋转角（0/90/180/270；2026-10-18 新增）。非 0 时
 *                  逐像素反查源（转置/翻转），写入跨度与同步账本按**实际
 *                  旋转后落笔范围**重算——直接沿用未旋转路径的
 *                  dst/dstLast 推导会在真机留下上一角度拖影（R11）。
 * @param      logW 整窗逻辑宽、logH 整窗逻辑高（旋转换算基准；rot==0 时
 *                  不用）。窗口局部矩形恒为逻辑坐标，原点不随旋转变。
 */
static void xpbs_presentRectRowsRot(const XImage* image, const XPoint* off,
                                    const XPoint* origin,
                                    const XPlatformDisplayInfo* info,
                                    int writeIndex, const uint8_t* srcBase,
                                    size_t pixelBytes, int imgBpl,
                                    int wx0, int wy0, int wx1, int wy1,
                                    int rot, int logW, int logH,
                                    size_t* outSyncLo, size_t* outSyncHi,
                                    bool* outSyncAny)
{
    int devX0, devY0, devX1, devY1;
    int dy;
    /* [跨平台编译修 2026-10-19] any/lo/hi 提到函数层作用域：原声明在
       rot!=0 的内层块，而 for 循环与收尾合并点都在块外——X11 树 gcc
       宽容通过，armel gcc-12 严格报 undeclared（部署门抓出）。 */
    size_t lo = 0, hi = 0;
    bool any = false;
    if (rot == 0)
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
        return;
    }
    /* 旋转：逻辑矩形正变换到设备 footprint 矩形，再逐设备像素 R⁻¹
       反查源。源范围由调用方保证（图像范围 ∩ 窗口范围已裁）。 */
    {
        int rw = wx1 - wx0;
        int rh = wy1 - wy0;
        int rx, ry, rwOut, rhOut;
        XWindow_rotationMapRect(rot, logW, logH, &wx0, &wy0, &rw, &rh,
                                &rx, &ry, &rwOut, &rhOut);
        devX0 = rx + origin->x;
        devY0 = ry + origin->y;
        devX1 = devX0 + rwOut;
        devY1 = devY0 + rhOut;
        if (devX0 < 0) devX0 = 0;
        if (devY0 < 0) devY0 = 0;
        if (devX1 > info->m_width) devX1 = info->m_width;
        if (devY1 > info->m_height) devY1 = info->m_height;
    }
    for (dy = devY0; dy < devY1; ++dy)
    {
        uint8_t* dstRow = (uint8_t*)info->m_frameBuffer +
            (size_t)(writeIndex * info->m_height + dy) * info->m_stride;
        int dx;
        for (dx = devX0; dx < devX1; ++dx)
        {
            int lx = 0, ly = 0;
            const uint8_t* p;
            XWindow_rotationUnmapPoint(rot, logW, logH, dx - origin->x,
                                       dy - origin->y, &lx, &ly);
            if (lx < wx0 || lx >= wx1 || ly < wy0 || ly >= wy1) continue;
            p = srcBase + (int64_t)(ly - off->y) * imgBpl +
                (int64_t)(lx - off->x) * (int64_t)pixelBytes;
            XMemcpy(dstRow + (size_t)dx * pixelBytes, p, pixelBytes);
            /* 同步账本按实际逐像素落笔范围累加（R11）。 */
            {
                size_t a = (size_t)((uint8_t*)info->m_frameBuffer +
                    (size_t)(writeIndex * info->m_height + dy) *
                        info->m_stride + (size_t)dx * pixelBytes) -
                    (size_t)(uintptr_t)info->m_frameBuffer;
                size_t b = a + pixelBytes;
                if (!any || a < lo) lo = a;
                if (!any || b > hi) hi = b;
                any = true;
            }
        }
    }
    if (any && outSyncLo && outSyncHi && outSyncAny)
    {
        if (!*outSyncAny || lo < *outSyncLo) *outSyncLo = lo;
        if (!*outSyncAny || hi > *outSyncHi) *outSyncHi = hi;
        *outSyncAny = true;
    }
}

/**
 * @brief 把源图像（窗口逻辑朝向）的一个局部逻辑矩形按旋转 R⁻¹ 写入指
 *        定 fb 缓冲（blit 旁路族旋转态共用；2026-10-21 定稿）。
 * @details 与 xpbs_presentRectRowsRot 的旋转分支同基准同口径（MapRect
 *          正变换定落笔 footprint + 逐设备像素 UnmapPoint 反查源 + 面
 *          板钳制），仅去掉差带账本记账（blit 族契约不触碰账本/翻页）。
 *          矩形口径=窗口局部逻辑坐标（面板矩形 - 窗口全局逻辑原点，
 *          与 blit 族调用方 origin 约定一致：装饰拖拽传逻辑几何、
 *          origin=几何左上）；旋转只对调落笔区宽高、原点不随转变
 *          （XWindowRotation.h 口径）。rot==0 时调用方照走原行拷贝路
 *          径（逐字节不变），本 helper 只服务旋转态。
 * @param   image      源图像（逻辑朝向；图像坐标=窗口局部逻辑坐标）。
 * @param   origin     窗口全局（面板）原点（逻辑 x/y）。
 * @param   info       显示驱动信息（fb 映射/面板尺寸/行距）。
 * @param   writeIndex 目标缓冲号（blitPanelRects 两缓冲各调一次）。
 * @param   srcBase/imgBpl/pixelBytes 源基址/行距/像深。
 * @param   lx0/ly0/lx1/ly1 窗口局部逻辑矩形（右/下界开区间）。
 * @param   rot/logW/logH 旋转角与整窗逻辑宽高（变换基准）。
 * @return  实际落笔的面板外接矩形入界与否经 outSync 带回（cacheflush
 *          收窄记账用）；完全不落笔返回 false。
 */
static bool xpbs_blitRectRowsRot(const XImage* image, const XPoint* origin,
                                 const XPlatformDisplayInfo* info,
                                 int writeIndex, const uint8_t* srcBase,
                                 size_t pixelBytes, int imgBpl,
                                 int lx0, int ly0, int lx1, int ly1,
                                 int rot, int logW, int logH,
                                 size_t* outSyncLo, size_t* outSyncHi,
                                 bool* outSyncAny)
{
    int rw = lx1 - lx0;
    int rh = ly1 - ly0;
    int rx, ry, rwOut, rhOut;
    int devX0, devY0, devX1, devY1;
    int dx, dy;
    (void)image;
    XWindow_rotationMapRect(rot, logW, logH, &lx0, &ly0, &rw, &rh,
                            &rx, &ry, &rwOut, &rhOut);
    devX0 = rx + origin->x;
    devY0 = ry + origin->y;
    devX1 = devX0 + rwOut;
    devY1 = devY0 + rhOut;
    if (devX0 < 0) devX0 = 0;
    if (devY0 < 0) devY0 = 0;
    if (devX1 > info->m_width) devX1 = info->m_width;
    if (devY1 > info->m_height) devY1 = info->m_height;
    if (devX1 <= devX0 || devY1 <= devY0) return false;
    for (dy = devY0; dy < devY1; ++dy)
    {
        uint8_t* dstRow = (uint8_t*)info->m_frameBuffer +
            (size_t)(writeIndex * info->m_height + dy) * info->m_stride;
        for (dx = devX0; dx < devX1; ++dx)
        {
            int sx = 0;
            int sy = 0;
            const uint8_t* p;
            XWindow_rotationUnmapPoint(rot, logW, logH, dx - origin->x,
                                       dy - origin->y, &sx, &sy);
            /* 反查落回源矩形界外（footprint 被面板钳制的边缘）：跳过。 */
            if (sx < lx0 || sx >= lx1 || sy < ly0 || sy >= ly1) continue;
            p = srcBase + (int64_t)sy * imgBpl +
                (int64_t)sx * (int64_t)pixelBytes;
            XMemcpy(dstRow + (size_t)dx * pixelBytes, p, pixelBytes);
        }
    }
    if (outSyncLo && outSyncHi && outSyncAny)
    {
        size_t lo = (size_t)(writeIndex * info->m_height + devY0) *
                        info->m_stride +
                    (size_t)devX0 * pixelBytes;
        size_t hi = (size_t)(writeIndex * info->m_height + devY1 - 1) *
                        info->m_stride +
                    (size_t)devX1 * pixelBytes;
        if (!*outSyncAny || lo < *outSyncLo) *outSyncLo = lo;
        if (!*outSyncAny || hi > *outSyncHi) *outSyncHi = hi;
        *outSyncAny = true;
    }
    return true;
}

/** @brief XWindow_rotationMapRect 的严格逆变换（设备矩形 → 逻辑矩形，
 *         2026-10-22 旋转遮挡切分坐标系根修配套）。
 *  @details XWindowRotation.h 只提供正矩形变换（rotationMapRect）与逐
 *           点逆变换（rotationUnmapPoint），无逆矩形变换；本 helper 按
 *           MapRect 各角分支逐式取逆（与 MapRect 复合恒等，0° 恒等，
 *           90/270 宽高对调还原）。W/H 取整窗逻辑宽高，入参为设备
 *           （footprint）域矩形，出参为逻辑域矩形；不做钳位（域外矩
 *           形合法，消费方按交集判空）。
 */
static void xpbs_unmapRectRot(int deg, int W, int H,
                              int inX, int inY, int inW, int inH,
                              int* outX, int* outY, int* outW, int* outH)
{
    switch (deg) {
    case 90:
        if (outX) *outX = inY;
        if (outY) *outY = H - inX - inW;
        if (outW) *outW = inH;
        if (outH) *outH = inW;
        return;
    case 180:
        if (outX) *outX = W - inX - inW;
        if (outY) *outY = H - inY - inH;
        if (outW) *outW = inW;
        if (outH) *outH = inH;
        return;
    case 270:
        if (outX) *outX = W - inY - inH;
        if (outY) *outY = inX;
        if (outW) *outW = inH;
        if (outH) *outH = inW;
        return;
    default:
        if (outX) *outX = inX;
        if (outY) *outY = inY;
        if (outW) *outW = inW;
        if (outH) *outH = inH;
        return;
    }
}

/** @brief 差带同步：把可见缓冲中 rect 行带搬入写入缓冲（fb→fb 行拷贝，
 *  供轮换写前补齐写入缓冲缺失内容；坐标为面板像素坐标，调用方保证
 *  已入面板界）。同步范围计入行带 cacheflush 账本。 */static void xpbs_syncRectRows(const XPlatformDisplayInfo* info,
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

/* ==================== 软件光标精灵（fbdev 无硬件光标平台触摸跟随） ===== */

#if XCURSOR_ON

/** @brief 精灵备份定容（内建箭头 12x19；旋转 90/270 外接对调 ≤19x19，
 *  每像素最多 4 字节）。 */
#define XPBS_SW_CURSOR_MAX 19

/**
 * @brief 精灵在画记录（备份像素 + 位置账，静态无堆；全 0 初始即无精灵）。
 * @details 每次画精灵前先把外接矩形内现有面板像素备份于此，下一帧
 *          呈现/旁路直写前从备份原样写回（恢复），使精灵不污染真实
 *          内容；恢复只对 m_writeIndex 记录的缓冲做（精灵只画在一个
 *          缓冲里，pan 成功后该缓冲即可见缓冲）。
 */
static struct
{
    bool m_active;     /**< 备份有效（精灵已画入 m_writeIndex 缓冲）。 */
    int m_writeIndex;  /**< 精灵所在缓冲号（0/1；恢复只对该缓冲做）。 */
    XRect m_rect;      /**< 面板坐标外接矩形（已钳入面板界）。 */
    size_t m_pixelBytes; /**< 备份时的像深（字节）；失配即作废防错位。 */
    size_t m_stride;     /**< 备份时的面板行距（字节）；失配即作废。 */
    uint8_t m_pixels[XPBS_SW_CURSOR_MAX * XPBS_SW_CURSOR_MAX * 4];
    bool m_backupValid;   /**< [备份保持 2026-10-23] m_pixels 是 m_backupRect
                               的真实底图（未被精灵像素污染）。 */
    XRect m_backupRect;   /**< m_pixels 覆盖的矩形（与 m_rect 同步更新）。 */
} g_xpbsSwCursor;

/** @brief 像深支持判定：仅 2/3/4 字节（RGB565/RGB888/32 位布局）；
 *  其余格式跳过绘制不崩（面板黑/白像素值按位深全 0/全 1 写，通道序
 *  无关——RGB565 黑=0x0000 白=0xFFFF；32bpp 布局序不影响全 0/全 1）。 */
static bool xpbs_swCursorDepthOk(size_t pixelBytes)
{
    return pixelBytes == 2 || pixelBytes == 3 || pixelBytes == 4;
}

/** @brief 单像素着色：非透明位写黑（全 0）或白（全 1）。 */
static void xpbs_swCursorPixelWrite(uint8_t* px, size_t pixelBytes, bool white)
{
    size_t i;
    for (i = 0; i < pixelBytes; ++i)
        px[i] = white ? 0xFF : 0x00;
}

/**
 * @brief      把精灵外接矩形的现有面板像素备份进静态缓冲（画前调用）。
 * @param      rect 面板坐标外接矩形（调用方保证已入面板界且 ≤19x19）。
 */
static void xpbs_swCursorBackup(const XPlatformDisplayInfo* info,
                                int writeIndex, const XRect* rect,
                                size_t pixelBytes)
{
    int y;
    for (y = 0; y < rect->height; ++y)
    {
        const uint8_t* src = (const uint8_t*)info->m_frameBuffer +
            (size_t)(writeIndex * info->m_height + rect->y + y) *
                info->m_stride +
            (size_t)rect->x * pixelBytes;
        XMemcpy(g_xpbsSwCursor.m_pixels +
                    (size_t)y * (size_t)rect->width * pixelBytes,
                src, (size_t)rect->width * pixelBytes);
    }
}

/**
 * @brief      从备份恢复精灵矩形（呈现/直写触碰 fb 之前调用）。
 * @details    只对精灵所在缓冲恢复；像深/行距与备份时失配（模式切换
 *            的防御路径）作废记录并放弃——陈旧备份写回会污染内容。
 * @return     true 实际恢复了精灵矩形（调用方可据此续画）。
 */
static bool xpbs_swCursorRestoreAt(const XPlatformDisplayInfo* info,
                                   int bufferIndex, size_t pixelBytes)
{
    const XRect* rect;
    int y;
    if (!g_xpbsSwCursor.m_active) return false;
    if (g_xpbsSwCursor.m_writeIndex != bufferIndex)
        return false; /* 精灵不在该缓冲（pan 失败帧的写缓冲侧无精灵）。 */
    if (g_xpbsSwCursor.m_pixelBytes != pixelBytes ||
        g_xpbsSwCursor.m_stride != info->m_stride)
    {
        g_xpbsSwCursor.m_active = false; /* 模式已变：备份不可信，作废。 */
        return false;
    }
    rect = &g_xpbsSwCursor.m_rect;
    for (y = 0; y < rect->height; ++y)
    {
        uint8_t* dst = (uint8_t*)info->m_frameBuffer +
            (size_t)(bufferIndex * info->m_height + rect->y + y) *
                info->m_stride +
            (size_t)rect->x * pixelBytes;
        XMemcpy(dst,
                g_xpbsSwCursor.m_pixels +
                    (size_t)y * (size_t)rect->width * pixelBytes,
                (size_t)rect->width * pixelBytes);
    }
    {
        /* [SWC 探针 2026-10-23] 恢复内容抽检：备份首行+末行首 2 像素原始
         * 值（真内容=页面底色中灰；精灵像素=全 0/全 F）——鬼影/尾行残
         * 留归因。 */
        static signed char dbg3 = -1;
        if (dbg3 < 0) dbg3 = (getenv("XGUI_SWCURSOR_DEBUG") != NULL);
        if (dbg3 && rect->width >= 2)
            fprintf(stderr,
                    "[SWC] restore buf=%d rect=(%d,%d %dx%d) px0=%02x%02x "
                    "px1=%02x%02x tail=%02x%02x\n",
                    bufferIndex, rect->x, rect->y, rect->width, rect->height,
                    g_xpbsSwCursor.m_pixels[0], g_xpbsSwCursor.m_pixels[1],
                    g_xpbsSwCursor.m_pixels[2], g_xpbsSwCursor.m_pixels[3],
                    g_xpbsSwCursor.m_pixels[(size_t)(rect->height - 1) *
                                            (size_t)rect->width * pixelBytes],
                    g_xpbsSwCursor.m_pixels[(size_t)(rect->height - 1) *
                                                (size_t)rect->width *
                                                pixelBytes +
                                            1]);
    }
    return true;
}

/** @brief 恢复精灵（无论记录在哪一号缓冲——双缓冲直写两缓冲的旁路
 *  路径用；精灵不在该缓冲时静默无事发生）。 */
static bool xpbs_swCursorRestoreAny(const XPlatformDisplayInfo* info,
                                    size_t pixelBytes)
{
    return xpbs_swCursorRestoreAt(info, g_xpbsSwCursor.m_writeIndex,
                                  pixelBytes);
}

/** @brief 精灵在画状态作废（首帧/换角整段清零把精灵一并抹掉后调用）。 */
static void xpbs_swCursorInvalidate(void)
{
    g_xpbsSwCursor.m_active = false;
}

/**
 * @brief      计算精灵按喂给参考系正变换后的面板外接矩形（纯计算零 fb 写）。
 * @details    xpbs_swCursorDraw 的矩形计算段抽出（2026-10-21 原位搬运
 *             用它先判「精灵当前可见否/面板位置变没变」，不变则免恢复
 *             免备份双写）。判定与绘制路径逐位同源：激活总条件
 *             （XCursor_swActive）→ 逻辑外接矩形 → 角归一 →
 *             rotationMapRect 正变换 + 窗口全局原点 → 面板界裁剪 →
 *             备份定容守卫。纯查询：失败**不**作废在画记录（作废纪律
 *             仍归绘制路径——恢复+作废必须成对，纯查询单方作废会把
 *             在屏精灵变成无主残留）。
 * @param      rot/logW/logH/origin 喂给参考系（XCursor_swPosFrame 口径，
 *             与三条重画路径同源）。
 * @param      outRect 输出面板坐标外接矩形（已钳入面板界且 ≤19x19）。
 * @return     true 矩形有效；false 精灵当前不可见（未激活/空矩形/出
 *             面板界），调用方无事可做。
 */
static bool xpbs_swCursorPanelRect(const XPlatformDisplayInfo* info,
                                   int rot, int logW, int logH,
                                   const XPoint* origin, XRect* outRect)
{
    XRect lrect;
    int rw;
    int rh;
    int rx;
    int ry;
    int fx0;
    int fy0;
    int fx1;
    int fy1;
    if (!XCursor_swActive()) return false;
    lrect = XCursor_swRect();
    rw = lrect.width;
    rh = lrect.height;
    if (rw <= 0 || rh <= 0 ||
        !XWindow_rotationNormalize(rot, &rx))
        return false;
    XWindow_rotationMapRect(rot, logW, logH, &lrect.x, &lrect.y, &rw, &rh,
                            &rx, &ry, &rw, &rh);
    fx0 = rx + origin->x;
    fy0 = ry + origin->y;
    fx1 = fx0 + rw;
    fy1 = fy0 + rh;
    if (fx0 < 0) fx0 = 0;
    if (fy0 < 0) fy0 = 0;
    if (fx1 > info->m_width) fx1 = info->m_width;
    if (fy1 > info->m_height) fy1 = info->m_height;
    if (fx1 <= fx0 || fy1 <= fy0 ||
        fx1 - fx0 > XPBS_SW_CURSOR_MAX || fy1 - fy0 > XPBS_SW_CURSOR_MAX)
        return false;
    outRect->x = fx0;
    outRect->y = fy0;
    outRect->width = fx1 - fx0;
    outRect->height = fy1 - fy0;
    return true;
}

/**
 * @brief      把精灵像素盖写进指定缓冲（无备份、无在画记账）。
 * @details     xpbs_swCursorDraw 的落笔段抽出（2026-10-23 双缓冲双落笔
 *             用）：主缓冲由 xpbs_swCursorDraw 备份+落笔+记账；本帮手
 *             只把同一精灵像素原样盖进另一缓冲——sun4i legacy FBIOPAN_
 *             DISPLAY 间歇「吞翻页」（ioctl 成功而扫描out未动，驱动层
 *             自愈搬内容）时，软件写索引与真实扫描面可能相异，精灵只
 *             画「账面可见缓冲」会在吞翻页帧整体下屏（90° 全帧 present
 *             重载下实测 ~1/3 帧光标缺失）。双缓冲各盖一份 ≤19x12 精灵
 *             矩形后，任一缓冲被扫描都有精灵；非扫描份的还原不靠备份
 *             ——该矩形在差带账本（present 尾部/原位搬运均登记），下帧
 *             差带同步自会把真实内容搬平。矩形计算与主落笔同源（同一
 *             参考系输入必得同一矩形）。
 * @return     true 已盖写；false 单缓冲面板/精灵当前不可见（调用方跳过）。
 */
static bool xpbs_swCursorStampOther(const XPlatformDisplayInfo* info,
                                    int primaryIndex, size_t pixelBytes,
                                    int rot, int logW, int logH,
                                    const XPoint* origin, XRect* outPanelRect)
{
    XRect lrect;
    XRect panel;
    const uint8_t* bits;
    int bw;
    int bh;
    int dx;
    int dy;
    int other = primaryIndex ^ 1;
    if (!info->m_doubleBuffered) return false;
    if (!xpbs_swCursorPanelRect(info, rot, logW, logH, origin, &panel))
        return false;
    lrect = XCursor_swRect();
    bits = XCursor_swArrowBits();
    bw = XCursor_swArrowWidth();
    bh = XCursor_swArrowHeight();
    for (dy = panel.y; dy < panel.y + panel.height; ++dy)
    {
        uint8_t* row = (uint8_t*)info->m_frameBuffer +
            (size_t)(other * info->m_height + dy) * info->m_stride;
        for (dx = panel.x; dx < panel.x + panel.width; ++dx)
        {
            int lx = 0;
            int ly = 0;
            int sx;
            int sy;
            uint8_t v;
            XWindow_rotationUnmapPoint(rot, logW, logH, dx - origin->x,
                                       dy - origin->y, &lx, &ly);
            sx = lx - lrect.x;
            sy = ly - lrect.y;
            if (sx < 0 || sx >= bw || sy < 0 || sy >= bh) continue;
            v = bits[(size_t)sy * (size_t)bw + (size_t)sx];
            if (v == 0) continue; /* 透明：保留底图。 */
            xpbs_swCursorPixelWrite(row + (size_t)dx * pixelBytes,
                                    pixelBytes, v == 2);
        }
    }
    if (outPanelRect) *outPanelRect = panel;
    return true;
}

/** @brief 精灵行带的收窄 cacheSync（落笔/盖写后 DMA scanout 前 clean）。 */
static void xpbs_swCursorSyncRect(const XPlatformDisplayDriverOps* ops,
                                  const XPlatformDisplayInfo* info,
                                  int bufferIndex, const XRect* rect,
                                  size_t pixelBytes)
{
    size_t lo = (size_t)(bufferIndex * info->m_height + rect->y) *
                    info->m_stride +
                (size_t)rect->x * pixelBytes;
    size_t hi = (size_t)(bufferIndex * info->m_height + rect->y +
                         rect->height - 1) *
                    info->m_stride +
                (size_t)(rect->x + rect->width) * pixelBytes;
    xpbs_cacheSyncRangeWarnOnce(ops,
                                (uint8_t*)info->m_frameBuffer + lo,
                                hi - lo);
}

/**
 * @brief      把 srcIndex 缓冲的 rect 行带镜像到 dstIndex 缓冲（行拷贝）。
 * @details    原位搬运的旧矩形清理用（2026-10-23 双缓冲双落笔配套）：
 *             搬运在账面可见缓冲恢复旧矩形后，另一缓冲的同矩形还留着
 *             上一拍盖写/桥补的精灵像素——不同步则吞翻页态下旧位鬼影
 *             长期可见（另一缓冲恰为真实扫描面时）。行带 ≤19x19，随写
 *             随清 cache。
 *             【镜像落点 x 偏移根修 2026-10-23】dst 行址缺 rect->x 像
 *             素偏移（src 有）：每次搬运镜像都把 12x19 行带写到面板
 *             **x=0 起**——真机实证（勾选框验收抓帧）：旧精灵矩形
 *             (341,147) 的真实内容被整块错位拷到左边缘 (0,147)，黑边
 *             条带被页面底色内容覆盖成 12x19 补丁、随翻页节律闪现，
 *             且本镜像要清的另一缓冲旧位精灵像素从未被清。补上偏移
 *             后拷贝逐位同域，新旧矩形镜像语义照注释成立。
 */
static void xpbs_swCursorMirrorRect(const XPlatformDisplayInfo* info,
                                    int srcIndex, int dstIndex,
                                    const XRect* rect, size_t pixelBytes)
{
    int y;
    size_t rowBytes;
    if (!info->m_doubleBuffered) return;
    if (rect->width <= 0 || rect->height <= 0) return;
    rowBytes = (size_t)rect->width * pixelBytes;
    for (y = 0; y < rect->height; ++y)
    {
        const uint8_t* src = (const uint8_t*)info->m_frameBuffer +
            (size_t)(srcIndex * info->m_height + rect->y + y) *
                info->m_stride +
            (size_t)rect->x * pixelBytes;
        uint8_t* dst = (uint8_t*)info->m_frameBuffer +
            (size_t)(dstIndex * info->m_height + rect->y + y) *
                info->m_stride +
            (size_t)rect->x * pixelBytes;
        XMemcpy(dst, src, rowBytes);
    }
}

/**
 * @brief      把软件光标精灵画进指定缓冲并记录备份（pan 之后调用）。
 * @details    逻辑外接矩形（XCursor_swRect）按当前内容旋转正变换到
 *             面板矩形（rotationMapRect + 窗口全局原点，与
 *             xpbs_presentRectRowsRot 的窗口局部→设备正变换同基准，
 *             四方位逐位一致），先备份后逐像素落笔：面板像素经 R⁻¹
 *             反查逻辑坐标得精灵局部下标，透明跳过、黑/白按位深写
 *             全 0/全 1。每帧至多 12x19 像素读写，零堆零大 memcpy。
 *             调用约定：本调用前必须已对可见缓冲做过恢复（精灵不再
 *             在屏）；任一失败路径自动作废在画记录——精灵既已从屏上
 *             清掉，陈旧记录会在两帧后用陈旧备份污染真实内容。
 * @param      rot/logW/logH/origin 精灵位置参考坐标系——统一取喂给
 *             时刻记录的 XCursor_swPosFrame（命中窗口旋转角/逻辑宽高/
 *             全局原点；各重画路径同源，精灵面板位置只随喂给变化），
 *             换算基准与 xpbs_presentRectRowsRot 的窗口局部→设备正变
 *             换同口径。
 * @param      outPanelRect 输出实际落笔的面板外接矩形（账本/cacheflush
 *             记账用），可为 NULL。
 * @param      refreshBackup [陈旧备份回退根修 2026-10-23] true=无视矩形
 *             相等守卫重建备份——调用方按「底图是否被本帧写过」判定。
 * @param      refreshRects  [选择性重备份 2026-10-23] refreshBackup 时本
 *             帧实际写区的面板域矩形表（present=体脏区 footprint；旁
 *             路条带路径传 NULL=整块重建，其恢复纪律保证目标矩形无精
 *             灵像素）。非 NULL 时**只把 ∩精灵矩形的像素**从刚写入缓
 *             冲取回，其余像素保留旧备份——见规则 3 的桥补污染论证。
 * @return     true 已画（记录同步更新）。
 */
static bool xpbs_swCursorDraw(const XPlatformDisplayInfo* info, int writeIndex,
                              size_t pixelBytes, int rot, int logW, int logH,
                              const XPoint* origin, XRect* outPanelRect,
                              bool refreshBackup, const XRect* refreshRects,
                              int refreshRectCount)
{
    XRect lrect;
    XRect panel;
    const uint8_t* bits;
    int bw;
    int bh;
    int dx;
    int dy;
    if (!xpbs_swCursorDepthOk(pixelBytes) ||
        !xpbs_swCursorPanelRect(info, rot, logW, logH, origin, &panel))
    {
        g_xpbsSwCursor.m_active = false; /* 隐藏/格式不支持/完全出面板
                                            界：精灵下屏。 */
        return false;
    }
    lrect = XCursor_swRect(); /* 逻辑外接矩形（像素环只取原点反查位图
                                 下标；正变换已在 panelRect 完成）。 */
    bits = XCursor_swArrowBits();
    bw = XCursor_swArrowWidth();
    bh = XCursor_swArrowHeight();
    /* 【备份保持 2026-10-23】同矩形且备份仍有效时**不重备份**：
     * 1) 备份有效性以 m_backupValid/m_backupRect 记账，**不挂 m_active**
     *    ——present 头部恢复后记录作废、尾部重画同矩形，此刻双落笔/桥
     *    补画已把「账面目标缓冲」的矩形填成精灵像素（差带同步把桥补精
     *    灵当内容搬入写入缓冲），重备份=把精灵像素存成「真实底图」，下
     *    一次恢复即把残影写回屏（拖尾鬼影，真机 0° 实证）。矩形未变 ⇒
     *    现存备份就是该矩形真实内容（首次画时取自干净底图；底图变化由
     *    差带账本逐帧同步自愈）。
     * 2) 矩形变化（原位搬运/首画）照旧重备份：新矩形从未被盖写，读得
     *    即真实底图。
     * 3) 【陈旧备份回退根修 2026-10-23】规则 1 的「底图变化由差带账本
     *    逐帧同步自愈」只收敛**两块屏缓冲**，备份是永不刷新的第三份拷
     *    贝（m_backupValid/m_backupRect 全文件唯一写点即本分支）：光标
     *    静止驻留处被应用层重画（控件按下/勾选/文本变更，脏区盖住精灵
     *    矩形）时，同矩形跳过重备份让备份停在 pre-change 内容——下一
     *    present 头部恢复把 pre-change 内容写回**正在扫描的可见缓冲**、
     *    差带同步再把它搬平到写缓冲，一次性视觉变更在一个 present 周
     *    期内被回退且两缓冲均失真，直到该控件因其它原因重画；移开光标
     *    时原位搬运恢复再次写回同一陈旧备份。故 refreshBackup=true 时
     *    重建备份，把本帧写入的新内容收进备份。
     *    【选择性重备份 2026-10-23·真机勾选框实证】但**整块**重备份在
     *    「本帧写区部分覆盖精灵矩形」时不安全：present 头部桥补先把精
     *    灵像素盖进可见缓冲、差带同步紧随其后把可见缓冲的桥补精灵像素
     *    当内容搬入写缓冲（桥补=扫描连续性优先，先于同步），体写只覆
     *    盖写区——写区外（如勾选框脏区下缘之下）的精灵矩形行带在尾部
     *    重画时仍是桥补/孪生精灵像素，整块重备份即把尾行精灵位图存成
     *    「真实底图」（探针 bk row12-18 与精灵位图逐位吻合实证），下帧
     *    恢复写下尾行残影并随镜像扩散双缓冲。故 refreshRects 非 NULL
     *    时**只把 ∩精灵矩形的像素**从刚写入缓冲取回（体写=真实新内
     *    容，体写必后于同步故不被桥补污染），其余像素保留旧备份（归纳
     *    保持真实：初值取自干净底图，此后只被真实像素替换）；矩形表为
     *    NULL 则整块重建（旁路条带路径专用：其恢复纪律保证画前目标矩
     *    形=恢复+条带直写后的真实内容，无精灵像素可存）。 */
    if (refreshBackup ||
        !(g_xpbsSwCursor.m_backupValid &&
          g_xpbsSwCursor.m_backupRect.x == panel.x &&
          g_xpbsSwCursor.m_backupRect.y == panel.y &&
          g_xpbsSwCursor.m_backupRect.width == panel.width &&
          g_xpbsSwCursor.m_backupRect.height == panel.height &&
          g_xpbsSwCursor.m_pixelBytes == pixelBytes &&
          g_xpbsSwCursor.m_stride == info->m_stride))
    {
        /* 选择性重建判定：仅「同矩形 + 备份仍有效 + 调用方给了写区矩
         * 形表」才允许保留部分旧备份像素；否则整块重建。 */
        bool partial = false;
        if (refreshBackup && refreshRects &&
            g_xpbsSwCursor.m_backupValid &&
            g_xpbsSwCursor.m_backupRect.x == panel.x &&
            g_xpbsSwCursor.m_backupRect.y == panel.y &&
            g_xpbsSwCursor.m_backupRect.width == panel.width &&
            g_xpbsSwCursor.m_backupRect.height == panel.height &&
            g_xpbsSwCursor.m_pixelBytes == pixelBytes &&
            g_xpbsSwCursor.m_stride == info->m_stride)
        {
            partial = true;
        }
        if (!partial)
        {
            xpbs_swCursorBackup(info, writeIndex, &panel, pixelBytes);
        }
        else
        {
            int ri;
            for (ri = 0; ri < refreshRectCount; ++ri)
            {
                const XRect* c = &refreshRects[ri];
                int ix0 = c->x > panel.x ? c->x : panel.x;
                int iy0 = c->y > panel.y ? c->y : panel.y;
                int ix1 = c->x + c->width < panel.x + panel.width
                              ? c->x + c->width
                              : panel.x + panel.width;
                int iy1 = c->y + c->height < panel.y + panel.height
                              ? c->y + c->height
                              : panel.y + panel.height;
                int y;
                if (ix1 <= ix0 || iy1 <= iy0) continue;
                for (y = iy0; y < iy1; ++y)
                {
                    const uint8_t* src = (const uint8_t*)info->m_frameBuffer +
                        (size_t)(writeIndex * info->m_height + y) *
                            info->m_stride +
                        (size_t)ix0 * pixelBytes;
                    XMemcpy(g_xpbsSwCursor.m_pixels +
                                (size_t)(y - panel.y) * (size_t)panel.width *
                                    pixelBytes +
                                (size_t)(ix0 - panel.x) * pixelBytes,
                            src, (size_t)(ix1 - ix0) * pixelBytes);
                }
            }
        }
        g_xpbsSwCursor.m_backupValid = true;
        g_xpbsSwCursor.m_backupRect = panel;
    }
    for (dy = panel.y; dy < panel.y + panel.height; ++dy)
    {
        uint8_t* row = (uint8_t*)info->m_frameBuffer +
            (size_t)(writeIndex * info->m_height + dy) * info->m_stride;
        for (dx = panel.x; dx < panel.x + panel.width; ++dx)
        {
            int lx = 0;
            int ly = 0;
            int sx;
            int sy;
            uint8_t v;
            XWindow_rotationUnmapPoint(rot, logW, logH, dx - origin->x,
                                       dy - origin->y, &lx, &ly);
            sx = lx - lrect.x;
            sy = ly - lrect.y;
            if (sx < 0 || sx >= bw || sy < 0 || sy >= bh) continue;
            v = bits[(size_t)sy * (size_t)bw + (size_t)sx];
            if (v == 0) continue; /* 透明：保留底图。 */
            xpbs_swCursorPixelWrite(row + (size_t)dx * pixelBytes,
                                    pixelBytes, v == 2);
        }
    }
    /* 记录在画状态（恢复目标=本缓冲）。 */
    g_xpbsSwCursor.m_active = true;
    g_xpbsSwCursor.m_writeIndex = writeIndex;
    g_xpbsSwCursor.m_rect = panel;
    g_xpbsSwCursor.m_pixelBytes = pixelBytes;
    g_xpbsSwCursor.m_stride = info->m_stride;
    if (outPanelRect) *outPanelRect = panel;
    return true;
}

void XPlatformBackingStore_requestCursorMotionRefresh(void)
{
#if XGUI_ON && XPLATFORM_FBDEV_ON
    /* 空白处 tap/拖动的光标跟随延迟根修（2026-10-21）：点交互控件时触
     * 点沿触发控件重绘→present 尾部精灵随帧跟进（正常）；点空白处不产
     * 生任何脏区→无 present→精灵原地不动，直到下一次 UI 重绘（如 1Hz
     * HUD）才跳过去。本调用由 fbinput 在帧提交处检测到「面板坐标变化」
     * 后同步直调，在**可见缓冲内原位搬运**精灵：恢复旧矩形（备份写回=
     * 真实内容归位）→ 新矩形存备份 → 画新位。铁律（脏区级触发）：全部
     * fb 写严格限于新旧两个精灵外接矩形（各 ≤19x19，XCursor_swRect 内
     * 建箭头 12x19 + 旋转 90/270 外接对调）范围内；不 pan（写的即可见
     * 缓冲，cacheSync 后扫描输出即见）、不触发控件重绘、不进差带账本
     * （下帧 present 的既有恢复纪律自然衔接：在画记录的缓冲/矩形/备份
     * 经 xpbs_swCursorDraw 原子更新为可见缓冲+新矩形，present 顶部恢复
     * 新矩形、账本里旧精灵行带经差带同步把真实内容搬入写入缓冲清旧
     * 影）。调用线程=主线程事件循环 poll 回调（xpfi_pump），present 只
     * 发生在事件派发/绘制调用栈内，单线程设计下 poll 相不可能与
     * present 在途重叠——同步直调无重入，无需投递一拍。 */
    const XPlatformDisplayDriverOps* ops;
    XPlatformDisplayInfo info;
    XImageFormat panel = XImageFormat_Invalid;
    size_t pixelBytes;
    int visIndex;
    int srot = 0;
    int slogW = 0;
    int slogH = 0;
    XPoint sorigin;
    XRect newRect;
    XRect oldRect;
    bool restored;
    bool crossBuf = false; /* 在画记录指向写缓冲（跨缓冲搬运态）。 */
    /* [SWC 探针 2026-10-23] 原位搬运执行轨迹（矩形/执行或跳过原因）；
     * XGUI_SWCURSOR_DEBUG=1 开，默认零成本。 */
    static signed char swcDbg = -1;
    if (swcDbg < 0) swcDbg = (getenv("XGUI_SWCURSOR_DEBUG") != NULL);
    ops = XPlatformDisplayDriver_active();
    if (!ops || !ops->probe || !ops->probe(&info) || !info.m_frameBuffer ||
        info.m_stride == 0 || info.m_width < 1 || info.m_height < 1)
    {
        if (swcDbg)
            fprintf(stderr, "[SWC] motion SKIP no-fbdev\n");
        return; /* 无 fbdev 直写面板：X11/win32 等平台恒 no-op（零影响）。 */
    }
    if (info.m_doubleBuffered && g_xpbsFbFirstPresent)
    {
        if (swcDbg)
            fprintf(stderr, "[SWC] motion SKIP first-present\n");
        return; /* 首帧 present 前可见缓冲未定（同 blitSnapshotPanelRects
                 * 拒绝口径；此刻亦不可能有精灵在屏）。 */
    }
    /* 像深=面板扫描位深（probe 权威，present 直写同源）；缺席回落
     * RGB565 协商（fillPanelRects 同口径）。2/3/4 字节以外不支持直接
     * 返回（与 xpbs_swCursorDepthOk 守卫一致，绝不按错位深寻址）。 */
    if (info.m_format != XImageFormat_Invalid)
        pixelBytes = (size_t)((XImageFormat_bitDepth(info.m_format) + 7) / 8);
    else if (info.m_bitsPerPixel > 0)
        pixelBytes = ((size_t)info.m_bitsPerPixel + 7) / 8;
    else
    {
        if (!ops->formatNegotiate(XImageFormat_RGB16, &panel) ||
            panel != XImageFormat_RGB16)
            return;
        pixelBytes = 2;
    }
    if (pixelBytes == 0 || !xpbs_swCursorDepthOk(pixelBytes))
    {
        if (swcDbg)
            fprintf(stderr, "[SWC] motion SKIP depth=%zu\n", pixelBytes);
        return;
    }
    /* 可见面=写索引^1（pan 成功翻转换号/失败不换号两演化下不变式均成
     * 立，与 blitSnapshotPanelRects 同一判定）。
     * 【位置相关不绘制根修 2026-10-23】旧实现精灵在画记录指向写缓冲时
     * 直接跳过搬运：记录落写缓冲的帧（pan 异常帧的旧尾部落笔语义）精灵
     * 实际不在扫描面，跳过=触摸后光标冻结在旧位，直到某帧 present 尾部
     * 重画恰逢 pan 成功才复活——按 pan 成败节律在「哪个点位失败」上双稳
     * 轮转，外观即「按位置决定性不绘制」。现改为跨缓冲搬运：不在写缓冲
     * 侧做恢复（其内容可能已被后续 present 写新，回写备份会回退内容），
     * 只作废陈旧记录、把新精灵画进可见缓冲、新旧矩形并入差带账本（下方
     * 追加处）——下帧 present 的既有恢复+差带同步纪律把写缓冲侧陈旧精
     * 灵行带搬平。fb 写仍严格限于精灵外接矩形（本态只写新矩形一个），
     * 不触发控件重绘/整帧 present，与原位搬运同一最小写架构。 */
    visIndex = info.m_doubleBuffered ? (g_xpbsFbWriteIndex ^ 1) : 0;
    if (g_xpbsSwCursor.m_active && g_xpbsSwCursor.m_writeIndex != visIndex)
    {
        crossBuf = true;
        if (swcDbg)
            fprintf(stderr,
                    "[SWC] motion CROSS rec=%d vis=%d "
                    "rect=(%d,%d %dx%d)\n",
                    g_xpbsSwCursor.m_writeIndex, visIndex,
                    g_xpbsSwCursor.m_rect.x, g_xpbsSwCursor.m_rect.y,
                    g_xpbsSwCursor.m_rect.width, g_xpbsSwCursor.m_rect.height);
    }
    /* 新面板矩形（喂给参考系 XCursor_swPosFrame 正变换+面板界裁剪，
     * 与三条重画路径同源换算——旋转态原位搬运同样走参考系变换）：
     * 算不出=精灵当前不可见（Blank/未激活/出面板界）直接返回；矩形与
     * 在画记录相同=面板位置未变，无事可做（双保险，调用方判重之外再
     * 查一次）。 */
    XCursor_swPosFrame(&srot, &slogW, &slogH, &sorigin);
    if (!xpbs_swCursorPanelRect(&info, srot, slogW, slogH, &sorigin,
                                &newRect))
    {
        if (swcDbg)
            fprintf(stderr,
                    "[SWC] motion SKIP panelRect rot=%d logW=%d logH=%d "
                    "org=(%d,%d) act=%d\n",
                    srot, slogW, slogH, sorigin.x, sorigin.y,
                    g_xpbsSwCursor.m_active ? 1 : 0);
        return;
    }
    /* 矩形与在画记录相同=面板位置未变，无事可做（双保险，调用方判重
     * 之外再查一次）。跨缓冲态不适用：记录指向写缓冲=精灵不在扫描面，
     * 「同矩形」不代表可见，必须照画（否则冻结在盲记录上）。 */
    if (!crossBuf && g_xpbsSwCursor.m_active &&
        g_xpbsSwCursor.m_rect.x == newRect.x &&
        g_xpbsSwCursor.m_rect.y == newRect.y &&
        g_xpbsSwCursor.m_rect.width == newRect.width &&
        g_xpbsSwCursor.m_rect.height == newRect.height)
    {
        if (swcDbg)
            fprintf(stderr, "[SWC] motion SKIP same-rect (%d,%d %dx%d)\n",
                    newRect.x, newRect.y, newRect.width, newRect.height);
        return;
    }
    /* 原位搬运两步（先恢复后画，与 present 头部恢复+尾部重画同序）：
     * 恢复把旧矩形真实内容写回可见缓冲并作废在画记录（纯查询帮手不
     * 作废，成对纪律归写路径）；画在新矩形重备份+逐像素落笔并重建记
     * 录（失败=精灵下屏且记录已自动作废，等 present 重建，无陈旧账）。
     * 跨缓冲态（crossBuf）不做恢复：旧精灵在写缓冲侧（不可见），本态
     * 只作废记录并把新精灵画进可见缓冲，写缓冲侧陈旧行带由下方账本
     * 收编、下帧差带同步搬平。 */
    oldRect = g_xpbsSwCursor.m_rect;
    restored = false;
    if (!crossBuf)
    {
        restored = xpbs_swCursorRestoreAt(&info, visIndex, pixelBytes);
        if (restored)
        {
            /* 旧矩形镜像到另一缓冲（2026-10-23 鬼影根修）：另一缓冲同矩
             * 形还留着上一拍盖写/桥补的精灵像素，只清账面可见缓冲时吞
             * 翻页会让鬼影滞留扫描面。行拷贝 ≤19x19，写后即清 cache。 */
            xpbs_swCursorMirrorRect(&info, visIndex, visIndex ^ 1, &oldRect,
                                    pixelBytes);
            if (info.m_doubleBuffered)
                xpbs_swCursorSyncRect(ops, &info, visIndex ^ 1, &oldRect,
                                      pixelBytes);
        }
    }
    else if (g_xpbsSwCursor.m_active)
    {
        /* 跨缓冲态补恢复（2026-10-23）：记录指向的缓冲里还画着旧精灵，
         * 账面可见缓冲对不上号时上面对 visIndex 的恢复是空操作——旧精灵
         * 在真实扫描面（吞翻页使记录缓冲恰为扫描面）残留成双光标鬼影，
         * 直到差带同步碰巧搬平。此处对记录缓冲直接按备份还原（写仍限于
         * 旧精灵矩形），并把还原结果镜像到对侧，鬼影当拍即清。 */
        if (xpbs_swCursorRestoreAt(&info, g_xpbsSwCursor.m_writeIndex,
                                   pixelBytes))
        {
            xpbs_swCursorMirrorRect(&info, g_xpbsSwCursor.m_writeIndex,
                                    g_xpbsSwCursor.m_writeIndex ^ 1, &oldRect,
                                    pixelBytes);
            if (info.m_doubleBuffered)
                xpbs_swCursorSyncRect(ops, &info,
                                      g_xpbsSwCursor.m_writeIndex ^ 1,
                                      &oldRect, pixelBytes);
        }
    }
    xpbs_swCursorInvalidate();
    /* [陈旧备份回退根修 2026-10-23] 原位搬运不传强制重备份：旧矩形已
     * 恢复（可见缓冲该矩形=真实内容）、本路径不直写任何底图行带，矩
     * 形相等守卫的既有语义原样保留（同矩形早退更在上游拦截）。 */
    if (!xpbs_swCursorDraw(&info, visIndex, pixelBytes, srot, slogW, slogH,
                           &sorigin, &newRect, false, NULL, 0))
    {
        if (swcDbg)
            fprintf(stderr, "[SWC] motion DRAW-FAIL (%d,%d %dx%d)\n",
                    newRect.x, newRect.y, newRect.width, newRect.height);
        return;
    }
    /* 恢复段先行收窄 cacheSync（先于画调用：画失败早退也不欠同步——
     * 恢复已写可见缓冲，精灵旧位必须 clean 后扫描输出才见真实内容）。*/
    if (restored)
        xpbs_swCursorSyncRect(ops, &info, visIndex, &oldRect, pixelBytes);
    /* 【吞翻页双落笔 2026-10-23】另一缓冲同盖一份（无记账，与 present
     * 尾部双落笔同纪律）：吞翻页帧账面可见缓冲≠真实扫描面，单落笔会
     * 让触摸后的光标迟到一次 present（重载下实测百毫秒级盲窗）；两缓
     * 冲各一份，任一扫描面即刻见新位。非扫描份的旧位还原由下方账本
     * 登记收编（下帧差带同步搬平真实内容）。 */
    {
        XRect swOther;
        if (xpbs_swCursorStampOther(&info, visIndex, pixelBytes, srot,
                                    slogW, slogH, &sorigin, &swOther))
            xpbs_swCursorSyncRect(ops, &info, visIndex ^ 1, &swOther,
                                  pixelBytes);
    }
    if (swcDbg)
        fprintf(stderr,
                "[SWC] motion MOVED buf=%d old=(%d,%d %dx%d) "
                "new=(%d,%d %dx%d) restored=%d cross=%d\n",
                visIndex, oldRect.x, oldRect.y, oldRect.width,
                oldRect.height, newRect.x, newRect.y, newRect.width,
                newRect.height, restored ? 1 : 0, crossBuf ? 1 : 0);
    /* 精灵外接矩形并入差带账本（与 present 尾部追加同口径，2026-10-23
     * 原位搬运账本收编）：搬运只改可见缓冲，写入缓冲在新旧精灵矩形处
     * 停留陈旧内容（旧精灵残影/更早帧内容）——不收编则下帧 present 翻
     * 页后该行带以陈旧内容上屏、头部恢复又把它备成「真内容」反复回跳。
     * 收编后下帧 present 头部恢复（先于差带同步）已把可见缓冲备 rect
     * 还原为真实内容，差带同步把真实内容搬入写入缓冲，两缓冲在精灵矩
     * 形处收敛；跨缓冲态（crossBuf）写缓冲侧的陈旧精灵行带同批搬平。
     * 只记账不落笔：本调用仍严格限于可见缓冲的新旧精灵矩形写。 */
    if (info.m_doubleBuffered && g_xpbsFbBackDirtyInit)
    {
        if (oldRect.width > 0 && oldRect.height > 0)
            XRegion_addRect(&g_xpbsFbBackDirty, &oldRect);
        XRegion_addRect(&g_xpbsFbBackDirty, &newRect);
    }
    /* 落笔段收窄 cacheSync（DMA scanout 前 clean）：恢复段与落笔段各自
     * 独立同步——合并成单一跨度会在新旧矩形相距远（跳点/跨屏 tap）时
     * 把中间整幅间隔卷进 msync（A33 整幅 ~30ms），破坏「只碰两个
     * ≤19x19 矩形」的性能口径。无恢复（无旧精灵）只同步落笔段。 */
    {
        size_t lo = (size_t)(visIndex * info.m_height + newRect.y) *
                        info.m_stride +
                    (size_t)newRect.x * pixelBytes;
        size_t hi = (size_t)(visIndex * info.m_height + newRect.y +
                             newRect.height - 1) *
                        info.m_stride +
                    (size_t)(newRect.x + newRect.width) * pixelBytes;
        xpbs_cacheSyncRangeWarnOnce(ops,
                                    (uint8_t*)info.m_frameBuffer + lo,
                                    hi - lo);
    }
#endif
}

#endif /* XCURSOR_ON */

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
#if XCURSOR_ON
    /* [陈旧备份回退根修 2026-10-23] 本帧写区（体脏区 footprint/memset
     * 整清）与精灵矩形的交叠表：尾部重画按它做**选择性**重备份——交
     * 叠像素取自刚写入缓冲（体写实内容），其余保留旧备份（消费方=
     * xpbs_swCursorDraw 规则 3；整清走 swFrameFullRewrite 整块重建）。 */
    XRect swFrameWriteRects[XPBS_OCCLUDER_MAX];
    int swFrameWriteRectCount = 0;
    bool swFrameFullRewrite = false;
#endif
    int rot = 0;      /* 窗口内容有效旋转角（2026-10-18）。 */
    int logW = 0;     /* 整窗逻辑宽高（旋转换算基准）。 */
    int logH = 0;
    XPoint originDev; /* rot≠0 时窗口面板设备原点（B 域正映射；须活到
                       函数尾，origin 全程消费）。 */
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
#if XCURSOR_ON
    /* 软件光标精灵恢复（单点统一入口，严格先于一切「从可见缓冲读源
     * 像素」的搬运与任何提前 return：下方差带账本行拷贝、native 零拷
     * 贝早退、脏区落笔全在其后）。恢复目标=当前可见缓冲（写索引^1，
     * 与 blitSnapshotPanelRects 同一不变式）；精灵不在该缓冲时静默。
     * 恢复后精灵即不在屏，在画记录同步作废——本帧尾部重画成功才重建
     * 记录；重画被早退/失败跳过也不会残留陈旧账（防两帧后陈旧备份
     * 污染真实内容）。 */
    /* 【present 期精灵在屏桥 2026-10-23】恢复把可见缓冲的精灵矩形还原
     * 成真实内容、差带同步又把写入缓冲的同矩形搬平——本 present 主体
     * （差带搬运+整帧落笔+阻塞 pan）期间两缓冲的精灵矩形**双双无精灵**
     *，直到尾部重画才回来。0° 小脏区帧该窗口仅微秒级；90° 全帧 R⁻¹
     * 落笔+阻塞 pan 把窗口拉长到十几毫秒且 present 连续（真机 90° 实测
     * ~1/3 快照/回扫拍点光标缺失）。故先记下精灵在画态，在 pan 之前把
     * 精灵补画回即将持续扫描的可见缓冲（xpbs_swCursorStampOther 主缓冲
     * 传 writeIndex^1 的对偶=直接盖 writeIndex^1 本尊），扫出面全程有
     * 精灵；尾部重画照旧（同位不重备份，见 xpbs_swCursorDraw）。 */
    {
        XRect swBridgeRect;
        int swBridgeRot = 0;
        int swBridgeLogW = 0;
        int swBridgeLogH = 0;
        XPoint swBridgeOrigin;
        bool swBridgeActive = g_xpbsSwCursor.m_active;
        swBridgeRect = g_xpbsSwCursor.m_rect;
        XCursor_swPosFrame(&swBridgeRot, &swBridgeLogW, &swBridgeLogH,
                           &swBridgeOrigin);
        xpbs_swCursorRestoreAt(&info,
                               info.m_doubleBuffered
                                   ? (g_xpbsFbWriteIndex ^ 1)
                                   : 0,
                               pixelBytes);
        xpbs_swCursorInvalidate();
        if (swBridgeActive && info.m_doubleBuffered)
        {
            XRect swBridgeOut;
            int visBridge = info.m_doubleBuffered ? (g_xpbsFbWriteIndex ^ 1)
                                                  : 0;
            if (xpbs_swCursorStampOther(&info, visBridge ^ 1, pixelBytes,
                                        swBridgeRot, swBridgeLogW,
                                        swBridgeLogH, &swBridgeOrigin,
                                        &swBridgeOut))
                xpbs_swCursorSyncRect(ops, &info, visBridge, &swBridgeOut,
                                      pixelBytes);
        }
    }
#endif
    /* 内容旋转（2026-10-18）：有效角每帧现查窗口（纯读，无缓存——窗口
       几何不变的 0↔180 切换刷不到任何平台缓存）；整窗逻辑宽高取窗口
       几何（旋转换算基准），绝不用图像尺寸（攒批子图按批次外接矩形
       分配，尺寸≠窗口宽高）。 */
    if (window)
    {
        XRect geometry = XWindow_geometry(window);
        rot = XWindow_contentRotationDegrees(window);
        logW = geometry.width;
        logH = geometry.height;
        if (logW <= 0) logW = XImage_width(image);
        if (logH <= 0) logH = XImage_height(image);
    }
    if (logW <= 0 || logH <= 0) return false;
    srcBase = XImage_constBits(image);
    imgBpl = XImage_bytesPerLine(image);
    if (!srcBase || imgBpl <= 0) return false;
    /* 真零拷贝判定：native 模式下公共层传入的 image 本就架在 fb 映射上
       （getNativeBuffer 返回了按窗口位置偏移的映射视图），painter 写入
       即已落在该窗口的屏幕区域，无需也无法再 memcpy——只做 cacheSync
       提交。区分依据：数据指针命中映射区间。
       【旋转分流闸 2026-10-21 定稿】旋转≠0 禁止落进本快分支：真零拷贝
       无中间帧可做 R⁻¹ 变换，且原位逐像素自写会读写同一段 fb 映射（行
       进方向上先写后读=内容自污染）。正常流切换钩子已把旋转实例切出零
       拷贝模式，此处命中即契约破例（native+旋转并存）——拒绝直写并打
       一次性诊断，回落平台提交路径，绝不静默上屏未旋转内容。fbdev 影
       子缓冲架构下 srcBase 恒不命中映射区间，本闸零参与（零回归）。 */
    if (srcBase >= (const uint8_t*)info.m_frameBuffer &&
        srcBase < (const uint8_t*)info.m_frameBuffer + info.m_frameBufferSize)
    {
        if (rot != 0)
        {
            static bool xpbsNativeRotWarned;
            if (!xpbsNativeRotWarned)
            {
                xpbsNativeRotWarned = true;
                fprintf(stderr,
                        "[fbdev] WARNING: native (zero-copy) backing store "
                        "presented with content rotation %d — refusing "
                        "direct commit (no intermediate frame to transform; "
                        "rotation switch hook should have dropped this "
                        "instance out of native mode).\n", rot);
            }
            return false;
        }
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
        /* originDev 须活到函数尾（origin 指针全程消费），放本函数顶层
         * 作用域之外会悬垂——与下方 occluder/账本/行写同寿。 */
        XPoint originZero;
        XPoint_init(&originZero, 0, 0);
        origin = fbOrigin ? fbOrigin : &originZero;
    }
    if (window && rot != 0)
    {
        /* 【B 域原点正映射 2026-10-22】fbOrigin 参数是窗口逻辑全局
         * 位（xpbs_windowFbOrigin 直出 geometry.xy）；rot≠0 的直写
         * 落笔原点必须用其面板设备原点（xpbs_topLevelDeviceRect 正
         * 映射）——弹层逻辑「下方」随转成面板「左方」落在 opener
         * 旁，主窗全屏正映射仍 (0,0)，rot=0 不进本分支逐位不变。
         * 内容变换基准 logW/logH 仍取窗口自身逻辑尺寸（own-basis
         * 旋转与桌面旋转在窗内逐点同变换，仅原点不同），不动。 */
            XRect windowGeometry = XWindow_geometry(window);
            XRect windowDeviceRect;
            xpbs_topLevelDeviceRect(rot, &windowGeometry, &info,
                                    &windowDeviceRect);
            originDev.x = windowDeviceRect.x;
            originDev.y = windowDeviceRect.y;
            origin = &originDev;
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
                    /* 【R12 2026-10-18】用 footprint 而非逻辑几何：弹层
                       内容旋转后屏幕占位是宽高对调后的矩形，按逻辑矩形剔除
                       会错位剔除。
                       【B 域遮挡 2026-10-22】遮挡矩形取高层顶层的旋转正映
                       射面板 footprint（xpbs_topLevelDeviceRect），与正映
                       射后的行写落笔域同域；旧 screenFootprint（A 域：原
                       点不变仅对调宽高）与非原点弹层的实际落板域错位——
                       90° 下保护的是弹层旧逻辑位、真实弹层区反被主窗整帧
                       洗掉。下层 xpbs_footprintRectToWindowRect 逆映射即
                       按本域设计。rot=0 两函数恒等，逐位零回归。 */
                    {
                        XRect aboveGeometry = XWindow_geometry(above);
                        xpbs_topLevelDeviceRect(
                            XWindow_contentRotationDegrees(above),
                            &aboveGeometry, &info, &g);
                    }
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
    {
        /* 诊断探针（XGUI_ROT_DEBUG=1）：弹层/旋转态 present 一行账——
           命中「弹层窗口 present」与「主窗带遮挡剔除 present」两类。 */
        static signed char dbg = -1;
        if (dbg < 0) dbg = (getenv("XGUI_ROT_DEBUG") != NULL);
        if (dbg && window &&
            (rot != 0 || XWindow_type(window) == XWindowType_Popup ||
             occluderCount > 0))
        {
            XRect fp;
            XRect geom = XWindow_geometry(window);
            int rcount = region ? region->count : 0;
            XRect r0;
            /* 【B 域探针 2026-10-22】fp 记实际落笔的面板设备 footprint
             * （正映射，与本函数 origin/遮挡同口径）；geom 仍记逻辑全局
             * 几何，两域并读。 */
            xpbs_topLevelDeviceRect(rot, &geom, &info, &fp);
            XRect_init(&r0, 0, 0, 0, 0);
            if (rcount > 0) r0 = region->rects[0];
            fprintf(stderr,
                    "[PBS] present win=%p type=%d rot=%d occl=%d "
                    "geom=(%d,%d %dx%d) fp=(%d,%d %dx%d) rects=%d "
                    "r0=(%d,%d %dx%d)\n",
                    (void*)window, (int)XWindow_type(window), rot,
                    occluderCount, geom.x, geom.y, geom.width, geom.height,
                    fp.x, fp.y, fp.width, fp.height, rcount, r0.x, r0.y,
                    r0.width, r0.height);
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
        /* 【R12 2026-10-18】换角帧：账本 g_xpbsFbBackDirty 存的是**上一
           角度**的 fb 坐标，直接用于本帧差带同步会把上一角度的条带搬到
           当前写区（真机拖影：旋转后偶尔残留上一角度内容）。矩形从旧角
           映到新角不是单次 MapRect 能表达的（跨 90/270 还要换轴基准），
           故换角那一帧按「首帧」处置：清零整段映射 + 清空账本，下一帧
           起账本即当前角度的正确账。代价=换角一次整屏 memset（换角是
           人工低频动作，可接受）；0° ↔ 0° 不入本分支（逐位零回归）。 */
        if (g_xpbsFbRotInit && g_xpbsFbRot != rot)
            g_xpbsFbFirstPresent = true;
        g_xpbsFbRot = rot;
        g_xpbsFbRotInit = true;
        if (g_xpbsFbFirstPresent)
        {
            memset((uint8_t*)info.m_frameBuffer, 0, info.m_frameBufferSize);
            g_xpbsFbFirstPresent = false;
#if XCURSOR_ON
            xpbs_swCursorInvalidate(); /* 整段清零把精灵一并抹掉：在画
                                          记录同步作废，防陈旧备份下帧
                                          写回垃圾。 */
            swFrameFullRewrite = true; /* [陈旧备份回退根修 2026-10-23]
                                          整段清零=全面板底图被本帧写过
                                          （含精灵矩形），尾部重画须整
                                          块按清零后内容重备份。 */
#endif
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
                    /* 【R12 2026-10-18】旋转后「本帧写区是否盖满面板」
                       判据改按 footprint：90/270 时窗口在屏幕上的占位是
                       对调后的矩形，用未旋转的逻辑尺寸会误判为未盖满
                       （多余的差带同步，正确性无损）或反之。 */
                    if (rect->x <= off->x && rect->y <= off->y &&
                        rect->x + rect->width >=
                            off->x + XImage_width(image) &&
                        rect->y + rect->height >=
                            off->y + XImage_height(image) &&
                        off->x <= 0 && off->y <= 0 &&
                        xpbs_rotationCoversPanel(rot, image,
                                                 &info))
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
                 * 图像范围 ∩ 面板范围），供差分求交。
                 * 【旋转定稿 2026-10-21】经 xpbs_windowRectToFootprint
                 * 正变换：写区=实际落笔的 footprint（rot=0 恒等逐字节
                 * 一致；90/270 按对调后坐标差分，与账本/落笔同基准）。 */
                for (i = 0; i < region->count &&
                            fbWriteCount < XPBS_OCCL_WORK_MAX; ++i)
                {
                    const XRect* rect = &region->rects[i];
                    int wx0, wy0, wx1, wy1;
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
                    if (wx1 <= wx0 || wy1 <= wy0) continue;
                    xpbs_windowRectToFootprint(rot, logW, logH, origin,
                                               &info, wx0, wy0, wx1, wy1,
                                               &fbWrite[fbWriteCount]);
                    if (fbWrite[fbWriteCount].width <= 0 ||
                        fbWrite[fbWriteCount].height <= 0)
                        continue;
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
         * 进入对应缓冲（0 号缓冲偏移 0，逐帧交替写入互不覆盖）。
         * 【旋转域修正 2026-10-22】本钳制只对 rot==0 生效：wx/wy 在
         * rot≠0 时是**逻辑域**坐标（逻辑 600x1000 对面板 1024x600 轴
         * 对调），按面板轴直钳会把逻辑高 1000 截成 600 → MapRect 后
         * 设备侧 x∈[24,424) 整列永不落笔（真机 90° 左侧 424px 黑带 +
         * 弹层关闭后该域残影永不恢复的根因）。rot≠0 的fb 写界由
         * xpbs_presentRectRowsRot 内 devX0/devY1 面板钳制兜住；源读界
         * 由上方图像范围钳制（逻辑域）保证。 */
        if (rot == 0)
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
            /* 遮挡剔除：把本脏矩形按各高层弹层矩形切分为周边条带，仅
             * 上屏未被遮挡的部分（工作集溢出则降级为按原矩形整块上屏
             * ——退回旧行为，绝不丢帧）。
             * 【旋转域修正 2026-10-22】切分全程在本窗**局部逻辑域**进
             * 行：弹层遮挡矩形（设备 footprint）先经
             * xpbs_footprintRectToWindowRect 反查到本窗逻辑域再求交
             * （rot=0 即减原点，逐位同旧口径）；切出的条带保持逻辑域
             * 直付 xpbs_presentRectRowsRot（内部自做正变换落笔）。此前
             * 工作矩形按 wx+origin 混用逻辑/设备两域，90/270 下切分
             * 与落笔双双错位。 */
            XRect work[XPBS_OCCL_WORK_MAX];
            int wn = 1;
            int oi;
            bool truncated = false;
            work[0].x = wx0;
            work[0].y = wy0;
            work[0].width = wx1 - wx0;
            work[0].height = wy1 - wy0;
            for (oi = 0; oi < occluderCount && !truncated; ++oi)
            {
                XRect next[XPBS_OCCL_WORK_MAX];
                int nn = 0;
                int wi2;
                XRect occlLocal;
                xpbs_footprintRectToWindowRect(rot, logW, logH, origin,
                                               occluders[oi].x,
                                               occluders[oi].y,
                                               occluders[oi].width,
                                               occluders[oi].height,
                                               &occlLocal);
                {
                    const XRect* o = &occlLocal;
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
                    xpbs_presentRectRowsRot(image, off, origin, &info,
                                         writeIndex, srcBase, pixelBytes,
                                         imgBpl,
                                         r->x,
                                         r->y,
                                         r->x + r->width,
                                         r->y + r->height,
                                         rot, logW, logH,
                                         &syncLo, &syncHi, &syncAny);
                }
                continue;
            }
            /* 截断降级：落到底部按未剔除原矩形整块上屏。 */
        }
        xpbs_presentRectRowsRot(image, off, origin, &info, writeIndex,
                             srcBase, pixelBytes, imgBpl,
                             wx0, wy0, wx1, wy1,
                             rot, logW, logH,
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
            if (wx1 <= wx0 || wy1 <= wy0) continue;
            /* 【旋转定稿 2026-10-21】账本按**实际落笔 footprint** 记账
             * （xpbs_windowRectToFootprint：rot=0 恒等逐字节一致；
             * 90/270 记对调后坐标——上一帧真实写过的行带才搬得对，
             * 未旋转投影记账在稳态 90/270 差带同步会漏搬/错搬残影）。 */
            xpbs_windowRectToFootprint(rot, logW, logH, origin, &info,
                                       wx0, wy0, wx1, wy1, &fbRect);
            if (fbRect.width <= 0 || fbRect.height <= 0) continue;
            XRegion_addRect(&g_xpbsFbBackDirty, &fbRect);
        }
    }
#if XCURSOR_ON
    /* [陈旧备份回退根修 2026-10-23] 本帧写区∩精灵矩形交叠表收集（仅备
     * 份仍在记时才有消费方；memset 整清已在首帧分支置整块重建）：体脏
     * 区按差带账本同款「图像范围钳制 + footprint 正变换」投影到面板域
     * 再求交。遮挡剔除在位时体写实际落笔 ⊊ region（弹层矩形被剔除），
     * 交叠像素未必被体写——此态放弃选择性重建（保持旧备份=规则 1 的
     * 跳过语义，宁冒回退不冒污染）；交叠矩形超表同样放弃。其余情形尾
     * 部重画按交叠表做像素级选择性重备份：交叠像素=体写真实新内容（体
     * 写必后于桥补/同步），非交叠像素保留旧备份（归纳真实）——勾选类
     * 一次性视觉变更进备份不再回退（本根修目标），桥补/孪生精灵像素不
     * 进备份（真机尾行残影实证的污染路径关闭）。不相交时保持矩形相等
     * 守卫的跳过路径，逐位零回归。 */
    if (g_xpbsSwCursor.m_backupValid && !swFrameFullRewrite &&
        occluderCount == 0)
    {
        int swRot = 0;
        int swLogW = 0;
        int swLogH = 0;
        XPoint swOrigin;
        XRect swPanel;
        XCursor_swPosFrame(&swRot, &swLogW, &swLogH, &swOrigin);
        if (xpbs_swCursorPanelRect(&info, swRot, swLogW, swLogH, &swOrigin,
                                   &swPanel))
        {
            for (i = 0; i < region->count; ++i)
            {
                const XRect* rect = &region->rects[i];
                XRect fbRect;
                XRect hit;
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
                if (wx1 <= wx0 || wy1 <= wy0) continue;
                xpbs_windowRectToFootprint(rot, logW, logH, origin, &info,
                                           wx0, wy0, wx1, wy1, &fbRect);
                if (fbRect.width <= 0 || fbRect.height <= 0) continue;
                hit.x = fbRect.x > swPanel.x ? fbRect.x : swPanel.x;
                hit.y = fbRect.y > swPanel.y ? fbRect.y : swPanel.y;
                hit.width =
                    (fbRect.x + fbRect.width < swPanel.x + swPanel.width
                         ? fbRect.x + fbRect.width
                         : swPanel.x + swPanel.width) -
                    hit.x;
                hit.height =
                    (fbRect.y + fbRect.height < swPanel.y + swPanel.height
                         ? fbRect.y + fbRect.height
                         : swPanel.y + swPanel.height) -
                    hit.y;
                if (hit.width <= 0 || hit.height <= 0) continue;
                if (swFrameWriteRectCount >= XPBS_OCCLUDER_MAX)
                {
                    /* 交叠超表：放弃选择性重建（宁冒回退不冒污染）。 */
                    swFrameWriteRectCount = 0;
                    break;
                }
                swFrameWriteRects[swFrameWriteRectCount++] = hit;
            }
        }
    }
#endif
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
#if XCURSOR_ON
    /* 软件光标精灵重画（pan 之后单点挂载）：画入 pan 后的**实际可见
     * 缓冲**（双缓冲= g_xpbsFbWriteIndex^1——pan 成功翻转换号、失败
     * 不换号，两种演化下该式都指向可见面，与本文件既有可见面判据同
     * 一不变式；单缓冲= 0 号恒可见，pan(0) 恒成功不换号且写缓冲被钳
     * 回 0 号，裸 ^1 会指向不存在的 1 号缓冲——判据与 :973 恢复/:853
     * 原位搬运/:2373 快照直写同款三元式）。
     * 【闪烁根修 2026-10-23】旧实现画入 writeIndex（=pan 成功时的可见
     * 缓冲）；pan 间歇失败的驱动上（FB_ACTIVATE_VBL 排队期再次 pan 得
     * EBUSY 等，真机以帧率节律交替失败），本帧顶部恢复已把精灵从可见
     * 缓冲擦掉、尾部重画却落进未翻页成功的写入缓冲——该帧屏幕无精灵，
     * 下帧 pan 成功又有：光标「亮一帧灭一帧」逐帧交替（RAM 面板交替
     * 注入 pan 失败实证：NO-CURSOR/CURSOR 逐帧交替、内容其余部分两缓
     * 冲收敛一致只有精灵矩形可见差异——与真机症状逐项吻合）。画入
     * g_xpbsFbWriteIndex^1 后，pan 失败帧精灵在可见缓冲原位补回（顶部
     * 擦除+尾部重画净零视觉变化），精灵可见性不再与 pan 成败耦合；
     * 在画记录同步指向可见缓冲，下帧顶部恢复/原位搬运守卫按既有不变
     * 式工作，pan 重试成功帧的差带账本照常把旧精灵行带搬平。
     * 变换用喂给参考系（XCursor_swPosFrame：pos 记账在最近命中窗口的
     * 局部坐标系）而非「当前呈现窗口」的 rot/logW/logH/origin——本窗≠
     * 命中窗时（手指停在 A 窗、HUD 类自续重绘的 B 窗每次 present）按
     * 本窗换算会把精灵画到错位处（偏移=两窗原点/旋转差），两次呈现
     * 交替即来回跳；同参考系下精灵面板位置只随喂给变化，多窗交替上
     * 屏位置恒一。 */
    {
        XRect swRect;
        int srot = 0;
        int slogW = 0;
        int slogH = 0;
        XPoint sorigin;
        int swTarget = info.m_doubleBuffered ? (g_xpbsFbWriteIndex ^ 1)
                                             : 0; /* pan 后实际可见缓冲（单
                                                    * 缓冲恒 0 号——pan(0)
                                                    * 恒成功不换号，写缓冲
                                                    * 亦被钳回 0 号；裸 ^1
                                                    * 会在单缓冲面板指向不
                                                    * 存在的 1 号缓冲，精灵
                                                    * 逐帧交替越界读写，与
                                                    * :973 恢复/:853 原位搬
                                                    * 运/:2373 快照直写同款
                                                    * 三元判据）。 */
        XCursor_swPosFrame(&srot, &slogW, &slogH, &sorigin);
        /* [陈旧备份回退根修 2026-10-23] 本帧写区触碰过精灵矩形时重建备
         * 份：整清帧整块重建；普通帧传交叠表做像素级选择性重建（规则 3
         * 选择性论证）。pan 失败帧目标缓冲=可见缓冲（恢复后真实内容）
         * 、体写区像素=刚写入新内容，两种演化下取回的都是真实底图。 */
        if (xpbs_swCursorDraw(
                &info, swTarget, pixelBytes, srot, slogW, slogH, &sorigin,
                &swRect, swFrameFullRewrite || swFrameWriteRectCount > 0,
                swFrameFullRewrite ? NULL : swFrameWriteRects,
                swFrameFullRewrite ? 0 : swFrameWriteRectCount))
        {
            /* 画在 cacheSync/pan 之后：精灵行带单独收窄同步一次（与上
               方行带收窄口径一致，约 19 行亚毫秒级）。 */
            xpbs_swCursorSyncRect(ops, &info, swTarget, &swRect, pixelBytes);
            /* 【吞翻页双落笔 2026-10-23】另一缓冲同盖一份精灵（无记账）：
             * 驱动吞翻页帧软件写索引≠真实扫描面，只画账面可见缓冲会整
             * 帧下屏（90° 全帧 present 重载实测 ~1/3 帧缺失）；两缓冲各
             * 一份后任一扫描面都有精灵。非扫描份的还原走差带账本（下帧
             * 同步真实内容搬平），不建备份不进在画记录。 */
            {
                XRect swOther;
                if (xpbs_swCursorStampOther(&info, swTarget, pixelBytes,
                                            srot, slogW, slogH, &sorigin,
                                            &swOther))
                    xpbs_swCursorSyncRect(ops, &info, swTarget ^ 1, &swOther,
                                          pixelBytes);
            }
            /* 精灵外接矩形并入差带账本（与上方账本更新同口径追加）：
               下帧差带同步把该行带从可见缓冲搬入写入缓冲，跨缓冲内容
               同步（pan 轮换后精灵不丢、旧位不残留）。 */
            if (info.m_doubleBuffered)
                XRegion_addRect(&g_xpbsFbBackDirty, &swRect);
            {
                /* [SWC 探针 2026-10-23] present 尾部重画一行账。 */
                static signed char dbg2 = -1;
                if (dbg2 < 0) dbg2 = (getenv("XGUI_SWCURSOR_DEBUG") != NULL);
                if (dbg2)
                    fprintf(stderr,
                            "[SWC] present-draw win=%p buf=%d rect=(%d,%d "
                            "%dx%d) rot=%d refr=%d regs=%d\n",
                            (void*)window, swTarget, swRect.x, swRect.y,
                            swRect.width, swRect.height, srot,
                            (swFrameFullRewrite || swFrameWriteRectCount > 0)
                                ? 1
                                : 0,
                            region->count);
            }
        }
        else
        {
            /* [SWC 探针 2026-10-23] present 尾部重画跳过原因（精灵未激
               活/出界/Blank——本帧精灵下屏）。 */
            static signed char dbg2 = -1;
            if (dbg2 < 0) dbg2 = (getenv("XGUI_SWCURSOR_DEBUG") != NULL);
            if (dbg2)
                fprintf(stderr,
                        "[SWC] present-draw SKIP win=%p buf=%d rot=%d "
                        "logW=%d logH=%d org=(%d,%d)\n",
                        (void*)window, swTarget, srot, slogW, slogH,
                        sorigin.x, sorigin.y);
        }
    }
#endif
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
#if XCURSOR_ON
    bool spriteHit = false; /* 填色条带命中精灵在画矩形（尾部续画依据）。 */
    int swBuf = 0;          /* 精灵所在缓冲号（恢复前捕获）。 */
#endif
    if (!ops || !ops->probe || !ops->probe(&info) || !info.m_frameBuffer ||
        info.m_stride == 0 || info.m_width < 1 || info.m_height < 1)
        return;
    if (!ops->formatNegotiate(XImageFormat_RGB16, &panel) ||
        panel != XImageFormat_RGB16)
        return; /* 仅承诺与直写同款 RGB565 面板；其余格式维持旧行为。 */
    pixelBytes = 2;
#if XCURSOR_ON
    /* 软件光标精灵守护（先于本路径全部 fb 直写；与孪生路径 blitPanel
     * Rects/present 头部恢复同纪律）：填色条带若与精灵在画矩形相交，
     * 先把备份底图写回（真实内容归位）再落填色、在画记录同步作废——
     * 否则精灵记录成陈旧账：下帧 present 顶部恢复把填色前内容写回可
     * 见缓冲，精灵行带经差带账本同步又把备份内容搬入写入缓冲，暴露
     * 条带在精灵矩形（≤12x19px）内长期回跳填色前内容。不相交则精灵
     * 与账本原样保留（快照拖拽步期间可能无 present，不闪失）；尾部
     * 按喂给参考系（XCursor_swPosFrame）续画，与三条重画路径同源。 */
    if (g_xpbsSwCursor.m_active)
    {
        int j;
        for (j = 0; j < count && !spriteHit; ++j)
        {
            const XRect* r = &rects[j];
            int ix0;
            int iy0;
            int ix1;
            int iy1;
            if (!r || r->width <= 0 || r->height <= 0) continue;
            ix0 = r->x > g_xpbsSwCursor.m_rect.x ? r->x
                                                 : g_xpbsSwCursor.m_rect.x;
            iy0 = r->y > g_xpbsSwCursor.m_rect.y ? r->y
                                                 : g_xpbsSwCursor.m_rect.y;
            ix1 = r->x + r->width < g_xpbsSwCursor.m_rect.x +
                                        g_xpbsSwCursor.m_rect.width
                      ? r->x + r->width
                      : g_xpbsSwCursor.m_rect.x + g_xpbsSwCursor.m_rect.width;
            iy1 = r->y + r->height < g_xpbsSwCursor.m_rect.y +
                                         g_xpbsSwCursor.m_rect.height
                      ? r->y + r->height
                      : g_xpbsSwCursor.m_rect.y + g_xpbsSwCursor.m_rect.height;
            if (ix1 > ix0 && iy1 > iy0) spriteHit = true;
        }
        if (spriteHit)
        {
            swBuf = g_xpbsSwCursor.m_writeIndex;
            xpbs_swCursorRestoreAny(&info, pixelBytes);
            xpbs_swCursorInvalidate();
        }
    }
#endif
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
#if XCURSOR_ON
    /* 软件光标精灵续画（条带填完之后；仅本路径刚恢复过的精灵才续画，
     * 不新造——画失败时 xpbs_swCursorDraw 自动作废在画记录，无陈旧
     * 账）：按喂给参考系画回精灵原缓冲，快照拖拽步（期间可能无
     * present）精灵不消失；精灵行带收窄同步一次，与 blitPanelRects
     * 续画同口径。 */
    if (spriteHit)
    {
        XRect swRect;
        int srot = 0;
        int slogW = 0;
        int slogH = 0;
        XPoint sorigin;
        XCursor_swPosFrame(&srot, &slogW, &slogH, &sorigin);
        /* [陈旧备份回退根修 2026-10-23] 条带填色已触碰精灵矩形（本续画
         * 仅 spriteHit 时执行）——重备份从刚写入的填色内容取：矩形
         * 相等守卫会让备份停留填色前内容，下一 present 头部恢复即把曝
         * 光条带在精灵矩形内回退。目标矩形此刻=恢复+填色后的真实内容
         * （恢复纪律保证无精灵像素），传 NULL 整块重建。 */
        if (xpbs_swCursorDraw(&info, swBuf, pixelBytes, srot, slogW, slogH,
                              &sorigin, &swRect, true, NULL, 0))
        {
            size_t lo = (size_t)(swBuf * info.m_height + swRect.y) *
                            info.m_stride +
                        (size_t)swRect.x * pixelBytes;
            size_t hi = (size_t)(swBuf * info.m_height + swRect.y +
                                 swRect.height - 1) *
                            info.m_stride +
                        (size_t)(swRect.x + swRect.width) * pixelBytes;
            xpbs_cacheSyncRangeWarnOnce(ops,
                                        (uint8_t*)info.m_frameBuffer + lo,
                                        hi - lo);
        }
    }
#endif
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
 * 直写同款（源图像格式==面板扫描格式，否则 no-op 维持旧行为）。
 * 【旋转自洽 2026-10-21】源窗口内容旋转≠0 时条带按 R⁻¹ 变换落板
 * （xpbs_blitRectRowsRot，两缓冲同写），rot=0 原行拷贝逐字节不变。*/
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
    int blitRot = 0;   /* 源窗口内容旋转角（0=原行拷贝路径，逐字节不变）。 */
    int blitLogW = 0;  /* 旋转变换基准：整窗逻辑宽高。 */
    int blitLogH = 0;
#if XCURSOR_ON
    bool swHadSprite = false; /* 恢复返回值：精灵是否原本在屏（续画依据）。 */
    int swBuf = 0;            /* 精灵所在缓冲号（恢复前捕获）。 */
#endif
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
    /* 【旋转自洽 2026-10-21】源后备缓冲属旋转窗口时，其图像是**逻辑朝
     * 向**帧：条带内容须按 R⁻¹ 变换落板（正变换定 footprint 落笔区 +
     * 逐设备像素反查源，与 present 直写同口径），照搬未旋转行拷贝会把
     * 逻辑坐标当面板坐标写（错位残影）。矩形口径=窗口局部逻辑坐标
     * （条带-origin，调用方以窗口逻辑几何+原点喂入）；rot==0 走原行
     * 拷贝（逐字节不变）。变换基准 logW/logH 取窗口逻辑几何（与图像
     * 同尺寸；缺席回落图像尺寸）。 */
    {
        XWindow* srcWindow = XPlatformBackingStore_window(src);
        blitRot = srcWindow ? XWindow_contentRotationDegrees(srcWindow) : 0;
        if (blitRot != 0)
        {
            XRect g = XWindow_geometry(srcWindow);
            blitLogW = g.width > 0 ? g.width : imgW;
            blitLogH = g.height > 0 ? g.height : imgH;
        }
    }
#if XCURSOR_ON
    /* 软件光标精灵恢复（先于本路径全部 fb 直写）：让位条带落笔若盖住
       精灵，恢复先把真实底图还原，条带照常覆盖；返回值作续画依据。 */
    swBuf = g_xpbsSwCursor.m_writeIndex;
    swHadSprite = xpbs_swCursorRestoreAny(&info, pixelBytes);
#endif
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
        if (blitRot != 0)
        {
            /* 旋转态：R⁻¹ 变换落板，两缓冲同写（与下方行拷贝同款双缓冲
               不失步语义；blit 族契约无 cacheSync，同步出参传 NULL）。
               源界=sx0..sy1（局部逻辑钳后界），落笔区由 helper 正变换
               +面板钳制决定——旋转 footprint 与未旋转投影在负 origin
               下不同，故本分支只按源界判空、不经下方未旋转面板投影
               空判定（那会误跳合法落笔）。 */
            if (sx1 <= sx0 || sy1 <= sy0) continue;
            xpbs_blitRectRowsRot(image, origin, &info, 0, srcBase,
                                 pixelBytes, imgBpl,
                                 sx0, sy0, sx1, sy1,
                                 blitRot, blitLogW, blitLogH,
                                 NULL, NULL, NULL);
            if (info.m_doubleBuffered)
                xpbs_blitRectRowsRot(image, origin, &info, 1, srcBase,
                                     pixelBytes, imgBpl,
                                     sx0, sy0, sx1, sy1,
                                     blitRot, blitLogW, blitLogH,
                                     NULL, NULL, NULL);
            continue;
        }
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
#if XCURSOR_ON
    /* 软件光标精灵续画（条带写完之后）：精灵原本在屏才续画（不新造；
     * 画失败时 xpbs_swCursorDraw 自动作废在画记录，无陈旧账）。本路径
     * 无窗口上下文（只有源后备缓冲与全局落点），变换用喂给参考系
     * （XCursor_swPosFrame）而非旧实现的恒等系（rot=0/原点 (0,0)）——
     * pos 记账在最近命中窗口的局部坐标系，命中窗口原点非零/旋转非零
     * （对话框/键盘拖拽让位路径）时恒等换算把精灵画到错位的面板坐标
     * （偏移=窗口原点/未旋转），与按命中窗换算的呈现路径交替来回跳。 */
    if (swHadSprite)
    {
        XRect swRect;
        int srot = 0;
        int slogW = 0;
        int slogH = 0;
        XPoint sorigin;
        XCursor_swPosFrame(&srot, &slogW, &slogH, &sorigin);
        /* [陈旧备份回退根修 2026-10-23] 让位条带已按源内容直写两缓冲：
         * 盖住精灵矩形时矩形相等守卫会让备份停留条带前内容，下一
         * present 头部恢复即回退条带内容——强制重备份按当前缓冲现状
         * 重建（恢复已把记录矩形还原为真实内容，条带未盖时逐字节等
         * 价、盖住时同步新内容），精灵像素无从存入。传 NULL 整块重建。 */
        if (xpbs_swCursorDraw(&info, swBuf, pixelBytes, srot, slogW, slogH,
                              &sorigin, &swRect, true, NULL, 0))
        {
            size_t lo = (size_t)(swBuf * info.m_height + swRect.y) *
                            info.m_stride +
                        (size_t)swRect.x * pixelBytes;
            size_t hi = (size_t)(swBuf * info.m_height + swRect.y +
                                 swRect.height - 1) *
                            info.m_stride +
                        (size_t)(swRect.x + swRect.width) * pixelBytes;
            xpbs_cacheSyncRangeWarnOnce(ops,
                                        (uint8_t*)info.m_frameBuffer + lo,
                                        hi - lo);
        }
    }
#endif
#else
    (void)src; (void)rects; (void)count; (void)origin;
#endif
}

/**
 * @brief 目标锚定逆变换落笔：面板矩形内逐设备像素经 UnmapPoint 反查
 *        源窗口逻辑帧并拷贝（restorePanelRects 单缓冲循环体；2026-10-21
 *        旋转条带基根修配套）。
 * @details 与 xpbs_blitRectRowsRot 方向相反：后者收「窗口局部逻辑矩
 *          形」正变换定落笔区（键盘整窗直提/快照直提契约），本 helper
 *          收「面板落笔矩形」逐设备像素反查源——让位条带在面板上的
 *          位置已知（footprint 基差带 ∩ footprint(other)），缺的只是
 *          「该位置住着源窗哪块内容」的逆查。源界=整幅图像界（sx/sy
 *          出界跳过；UnmapPoint 的定义域恰为源窗 footprint，出域落点
 *          自然被界外判剔除，无需另做 footprint 钳制）。rot==0 恒等
 *          逐像素拷贝（与行拷贝同结果；调用方 rot=0 仍走 blitPanel
 *          Rects 原行拷贝路径，本 helper 只服务旋转态）。
 * @return  有落笔返回 true（预留；现调用方无需同步账本）。
 */
#if XGUI_ON && XPLATFORM_FBDEV_ON
static bool xpbs_restoreRectRowsRot(const XImage* image, const XPoint* origin,
                                    const XPlatformDisplayInfo* info,
                                    int writeIndex, const uint8_t* srcBase,
                                    size_t pixelBytes, int imgBpl,
                                    int dx0, int dy0, int dx1, int dy1,
                                    int rot, int logW, int logH,
                                    int imgW, int imgH)
{
    int dx;
    int dy;
    (void)image;
    if (dx1 <= dx0 || dy1 <= dy0) return false;
    for (dy = dy0; dy < dy1; ++dy)
    {
        uint8_t* dstRow = (uint8_t*)info->m_frameBuffer +
            (size_t)(writeIndex * info->m_height + dy) * info->m_stride;
        for (dx = dx0; dx < dx1; ++dx)
        {
            int sx = 0;
            int sy = 0;
            const uint8_t* p;
            XWindow_rotationUnmapPoint(rot, logW, logH, dx - origin->x,
                                       dy - origin->y, &sx, &sy);
            /* 反查落回图像界外（footprint 域外/几何与图像不一致边缘）：
               跳过。 */
            if (sx < 0 || sy < 0 || sx >= imgW || sy >= imgH) continue;
            p = srcBase + (int64_t)sy * imgBpl +
                (int64_t)sx * (int64_t)pixelBytes;
            XMemcpy(dstRow + (size_t)dx * pixelBytes, p, pixelBytes);
        }
    }
    return true;
}
#endif /* XGUI_ON && XPLATFORM_FBDEV_ON */

void XPlatformBackingStore_restorePanelRects(XPlatformBackingStore* src,
                                             const XRect* rects, int count)
{
#if XGUI_ON && XPLATFORM_FBDEV_ON
    /* 让位条带归位还原的旋转态配套入口（2026-10-21 旋转条带基根修）：
     * rects 语义=面板坐标落笔区（调用方已按 footprint 域算好：面板带
     * =footprint(oldG)/footprint(newG) 差带 ∩ footprint(other)），本
     * 入口逐设备像素经 UnmapPoint 反查源窗口逻辑帧后落板。契约与
     * blitPanelRects 完全同款：同步双缓冲直写（写入即见）、不碰差带
     * 账本与翻页状态、无 pan、无 cacheSync（同 fill/blit 口径）；
     * 软件光标精灵恢复/续画纪律同款。blitPanelRects 的前向映射契约
     * （矩形=窗口局部逻辑坐标，正变换定落笔区）刻意不动——键盘整窗
     * 直提路（XVirtualKeyboard 紧凑拖移）矩形=整窗逻辑几何，依赖前向
     * 变换落到 footprint。源窗口/几何缺席按原点 (0,0)、恒等变换处理
     * （逐像素拷贝）。调用方 rot=0 时应继续走 blitPanelRects（行拷贝
     * 原路径逐字节不变）；本入口 rot=0 逐像素恒等，结果一致。 */
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
    int rot = 0;      /* 源窗口内容旋转角（缺席=0 恒等）。 */
    int logW = 0;     /* 逆查基准：整窗逻辑宽高（与 present 同源）。 */
    int logH = 0;
    XPoint origin;
#if XCURSOR_ON
    bool swHadSprite = false; /* 恢复返回值：精灵是否原本在屏（续画依据）。 */
    int swBuf = 0;            /* 精灵所在缓冲号（恢复前捕获）。 */
#endif
    origin.x = 0;
    origin.y = 0;
    if (!src || !rects || count <= 0) return;
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
    {
        XWindow* srcWindow = XPlatformBackingStore_window(src);
        if (srcWindow)
        {
            XRect g = XWindow_geometry(srcWindow);
            rot = XWindow_contentRotationDegrees(srcWindow);
            origin.x = g.x;
            origin.y = g.y;
            logW = g.width > 0 ? g.width : imgW;
            logH = g.height > 0 ? g.height : imgH;
            if (rot != 0)
            {
                /* 【B 域锚点 2026-10-22】逐设备像素反查的参考原点取窗口
                 * 的面板设备原点（xpbs_topLevelDeviceRect 正映射，与
                 * present 直写落笔域同域），不再是逻辑左上 g.xy——归位
                 * 条带（面板域）里的设备点只有减 B 域原点才是「源窗局部
                 * 设备坐标」，减 A 域逻辑原点会把非原点旋转窗的逆查整体
                 * 平移错位（90° 对话框条带反查到旧逻辑位内容）。logW/
                 * logH 变换基准仍取逻辑宽高（own-basis 与 present 一致）
                 * 不动。rot=0 正映射恒等，逐位不变。 */
                XRect devRect;
                xpbs_topLevelDeviceRect(rot, &g, &info, &devRect);
                origin.x = devRect.x;
                origin.y = devRect.y;
            }
        }
        else
        {
            logW = imgW;
            logH = imgH;
        }
    }
#if XCURSOR_ON
    /* 软件光标精灵恢复（先于本路径全部 fb 直写；blitPanelRects 同
       纪律）：落笔若盖住精灵，先还原真实底图再落条带，返回值作续画
       依据。 */
    swBuf = g_xpbsSwCursor.m_writeIndex;
    swHadSprite = xpbs_swCursorRestoreAny(&info, pixelBytes);
#endif
    for (i = 0; i < count; ++i)
    {
        const XRect* r = &rects[i];
        int dx0;
        int dy0;
        int dx1;
        int dy1;
        if (!r || r->width <= 0 || r->height <= 0) continue;
        /* 面板界钳制兜底（调用方已按 footprint 域裁剪）。 */
        dx0 = r->x < 0 ? 0 : r->x;
        dy0 = r->y < 0 ? 0 : r->y;
        dx1 = r->x + r->width > info.m_width ? info.m_width
                                             : r->x + r->width;
        dy1 = r->y + r->height > info.m_height ? info.m_height
                                               : r->y + r->height;
        if (dx1 <= dx0 || dy1 <= dy0) continue;
        xpbs_restoreRectRowsRot(image, &origin, &info, 0, srcBase,
                                pixelBytes, imgBpl,
                                dx0, dy0, dx1, dy1,
                                rot, logW, logH, imgW, imgH);
        /* 双缓冲面板：同内容同步进后台缓冲，翻页不失步（blit 同款）。 */
        if (info.m_doubleBuffered)
            xpbs_restoreRectRowsRot(image, &origin, &info, 1, srcBase,
                                    pixelBytes, imgBpl,
                                    dx0, dy0, dx1, dy1,
                                    rot, logW, logH, imgW, imgH);
    }
#if XCURSOR_ON
    /* 软件光标精灵续画（条带写完之后；blitPanelRects 同口径：喂给参
       考系换算，精灵原本在屏才续画，画失败自动作废在画记录）。 */
    if (swHadSprite)
    {
        XRect swRect;
        int srot = 0;
        int slogW = 0;
        int slogH = 0;
        XPoint sorigin;
        XCursor_swPosFrame(&srot, &slogW, &slogH, &sorigin);
        /* [陈旧备份回退根修 2026-10-23] 归位条带直写同 blitPanelRects：
         * 强制重备份按当前缓冲现状重建（整块），条带盖住精灵矩形时备
         * 份同步新内容，防下帧 present 头部恢复回退条带内容。 */
        if (xpbs_swCursorDraw(&info, swBuf, pixelBytes, srot, slogW, slogH,
                              &sorigin, &swRect, true, NULL, 0))
        {
            size_t lo = (size_t)(swBuf * info.m_height + swRect.y) *
                            info.m_stride +
                        (size_t)swRect.x * pixelBytes;
            size_t hi = (size_t)(swBuf * info.m_height + swRect.y +
                                 swRect.height - 1) *
                            info.m_stride +
                        (size_t)(swRect.x + swRect.width) * pixelBytes;
            xpbs_cacheSyncRangeWarnOnce(ops,
                                        (uint8_t*)info.m_frameBuffer + lo,
                                        hi - lo);
        }
    }
#endif
#else
    (void)src; (void)rects; (void)count;
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
    int snapRot = 0;   /* 拖动窗口内容旋转角（0=原行拷贝路径，逐字节不变）。 */
    int snapLogW = 0;  /* 旋转变换基准：整窗逻辑宽高。 */
    int snapLogH = 0;
    XPoint originIn;   /* 调用方原点（逻辑全局左上，A 域）。 */
    XPoint originDev;  /* rot≠0 落笔原点（B 域正映射；origin 改指此处，
                        * 函数层声明与 present originDev 同款全寿）。 */
#if XCURSOR_ON
    bool swHadSprite = false; /* 恢复返回值：精灵是否原本在屏（续画依据）。 */
#endif
    if (!snapshot || !snapshot->m_data || !rects || count <= 0 || !origin)
        return false;
    originIn = *origin;
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
    /* 【旋转自洽 2026-10-21】拖动窗口内容旋转≠0 时快照是**逻辑朝向**
     * 帧：条带落笔按 R⁻¹ 变换（与 present 直写/xpbs_blitRectRowsRot 同
     * 口径：正变换定 footprint 落笔区 + 逐设备像素反查源），照搬未旋转
     * 行拷贝会把逻辑坐标当面板坐标写（拖动残影）。遮挡切分与落笔同域
     * 【2026-10-22 根修】：occluder（面板 footprint 域）先逆映射回本窗
     * 逻辑域再切分，见收集处注——旧稿跨域切分致旋转态遮挡/可见条带对
     * 调错位。变换基准 logW/logH 取 selfWindow 逻辑几何（与快照同尺寸，
     * 拖动不改尺寸；缺席回落快照尺寸）。 */
    if (selfWindow)
    {
        snapRot = XWindow_contentRotationDegrees(selfWindow);
        if (snapRot != 0)
        {
            XRect g = XWindow_geometry(selfWindow);
            XRect devRect;
            snapLogW = g.width > 0 ? g.width : imgW;
            snapLogH = g.height > 0 ? g.height : imgH;
            /* 【B 域快照原点 2026-10-22】拖动/让位条带通道与 present 直
             * 写同域收口：落笔原点取 xpbs_topLevelDeviceRect 正映射（拖
             * 动窗口的面板设备矩形左上），不再是调用方传入的逻辑左上
             * （A 域）——90° 拖非原点装饰窗（对话框）时 A 域原点把快照
             * 按旧逻辑位落板（逻辑 (100,200 400x300) 落 (100,200
             * 300x400)，真位 (524,100 300x400)），拖动中错位、松手整帧
             * PAINT 跳到 B 域位。调用方 rects/origin 逻辑域契约不动，
             * 本入口内部平移对齐（见矩形循环）。occluder 侧已同为本域
             * （上方收集处 xpbs_topLevelDeviceRect），unmap+回加复合随
             * origin 同步换锚，遮挡/落笔仍全程同域。 */
            xpbs_topLevelDeviceRect(snapRot, &g, &info, &devRect);
            originDev.x = devRect.x;
            originDev.y = devRect.y;
            origin = &originDev;
        }
    }
#if XCURSOR_ON
    /* 软件光标精灵恢复（先于本路径全部 fb 直写）：拖动条带若盖住精灵，
       先把备份底图写回，条带照常覆盖（恢复顺序先于落笔才不脏账）；
       返回值作翻页后续画依据。 */
    swHadSprite = xpbs_swCursorRestoreAt(&info, visIndex, pixelBytes);
#endif
    {
        /* 诊断探针（XGUI_ROT_DEBUG=1）：拖动快照直写一行账——rot≠0 才
           有意义，geom 两域并读（originIn=调用方逻辑左上、originDev=面
           板 B 域落笔原点；落笔区=originDev+footprint(logW,logH)）。 */
        static signed char dbg = -1;
        if (dbg < 0) dbg = (getenv("XGUI_ROT_DEBUG") != NULL);
        if (dbg && snapRot != 0 && selfWindow)
            fprintf(stderr,
                    "[PBS] snapBlit win=%p rot=%d logW=%d logH=%d "
                    "originIn=(%d,%d) originDev=(%d,%d)\n",
                    (void*)selfWindow, snapRot, snapLogW, snapLogH,
                    originIn.x, originIn.y, originDev.x, originDev.y);
    }
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
                    /* 【R12 对齐 2026-10-21】用 footprint 而非逻辑几何：
                       旋转弹层在屏幕上的占位是宽高对调后的矩形，按逻辑
                       矩形剔除会错位剔除（与 present 直写的 R12 同根修
                       对齐；rot=0 两函数恒等，逐字节零回归）。
                       【B 域遮挡 2026-10-22】与 present 直写遮挡同批：occluder
                       取高层顶层旋转正映射面板 footprint
                       （xpbs_topLevelDeviceRect），与弹层实际落板域同域；
                       旧 screenFootprint（A 域）在 90° 下对非原点弹层取的
                       是旧逻辑位，拖动快照会把真实弹层区洗掉。下方逆映射
                       入库链按面板域 occluder 设计，消费不变。 */
                    {
                        XRect aboveGeometry = XWindow_geometry(above);
                        xpbs_topLevelDeviceRect(
                            XWindow_contentRotationDegrees(above),
                            &aboveGeometry, &info, &g);
                    }
                gx1 = g.x + g.width;
                gy1 = g.y + g.height;
                if (g.x < 0) { g.width += g.x; g.x = 0; }
                if (g.y < 0) { g.height += g.y; g.y = 0; }
                if (gx1 > info.m_width) gx1 = info.m_width;
                if (gy1 > info.m_height) gy1 = info.m_height;
                if (gx1 <= g.x || gy1 <= g.y) continue;
                if (occluderCount < XPBS_OCCLUDER_MAX)
                {
                    /* 【旋转遮挡切分坐标系根修 2026-10-22】occluder 是
                     * 面板 footprint 域矩形，而本函数 full/band 的口径
                     * 是「origin+窗口局部逻辑」域。snapRot !=0 时把
                     * occluder 先逆映射回本窗局部逻辑域
                     * （xpbs_unmapRectRot 与 MapRect 严格互逆，再平移回
                     * origin 基准）后入库，切分全程同域：交集对双射可
                     * 交换，切出的逻辑条带其正变换像恰为面板可见条带，
                     * 下方 blitRectRowsRot 按其「窗口局部逻辑矩形」契
                     * 约消费。旧稿以面板域 occluder 直接切逻辑域
                     * full——遮挡/可见条带对调错位（rot=90/180/270 同
                     * 错，被遮面板像素被拖窗快照洗写、可见条带滞留旧内
                     * 容）；rot=0 时 footprint 域==逻辑域，不进本分支，
                     * 路径逐字节不变。 */
                    XRect occ;
                    occ.x = g.x;
                    occ.y = g.y;
                    occ.width = gx1 - g.x;
                    occ.height = gy1 - g.y;
                    if (snapRot != 0)
                    {
                        int lx = 0;
                        int ly = 0;
                        int lw = 0;
                        int lh = 0;
                        xpbs_unmapRectRot(snapRot, snapLogW, snapLogH,
                                          occ.x - origin->x,
                                          occ.y - origin->y,
                                          occ.width, occ.height,
                                          &lx, &ly, &lw, &lh);
                        occ.x = lx + origin->x;
                        occ.y = ly + origin->y;
                        occ.width = lw;
                        occ.height = lh;
                    }
                    occluders[occluderCount] = occ;
                    ++occluderCount;
                }
            }
            XClassDelete(tops);
        }
    }
    for (i = 0; i < count; ++i)
    {
        const XRect* r = &rects[i];
        XRect rc;
        /* 双向钳制：先图像范围（图像坐标=矩形-origin），再面板范围；
         * 与 blitPanelRects 同款。 */
        int sx0;
        int sy0;
        int sx1;
        int sy1;
        int x0;
        int y0;
        int x1;
        int y1;
        size_t rowBytes;
        if (!r || r->width <= 0 || r->height <= 0) continue;
        if (snapRot != 0)
        {
            /* 【B 域快照原点 2026-10-22】矩形域平移：调用方入参=
             * 「originIn+窗口局部逻辑」域（装饰拖拽传逻辑几何、origin=
             * 逻辑左上），本函数工作域已整体迁「originDev+窗口局部逻
             * 辑」（origin 已改指 originDev）；同平移后图像钳制/遮挡切
             * 分/落笔全程同域。rot=0 不进本分支，路径逐字节不变。 */
            rc = *r;
            rc.x += originDev.x - originIn.x;
            rc.y += originDev.y - originIn.y;
            r = &rc;
        }
        sx0 = r->x - origin->x;
        sy0 = r->y - origin->y;
        sx1 = sx0 + r->width;
        sy1 = sy0 + r->height;
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
                if (snapRot != 0)
                {
                    /* 旋转态：条带按 R⁻¹ 变换落笔（只写可见面，与下方
                       行拷贝同账本口径：实际 footprint 落笔范围计入
                       cacheflush 收窄账）。 */
                    xpbs_blitRectRowsRot(snapshot, origin, &info, visIndex,
                                         srcBase, pixelBytes, imgBpl,
                                         band->x - origin->x,
                                         band->y - origin->y,
                                         band->x - origin->x + band->width,
                                         band->y - origin->y + band->height,
                                         snapRot, snapLogW, snapLogH,
                                         &syncLo, &syncHi, &syncAny);
                    continue;
                }
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
        if (snapRot != 0)
        {
            /* 旋转态：整矩形按 R⁻¹ 变换落笔（无遮挡剔除路径）。 */
            xpbs_blitRectRowsRot(snapshot, origin, &info, visIndex,
                                 srcBase, pixelBytes, imgBpl,
                                 x0 - origin->x, y0 - origin->y,
                                 x1 - origin->x, y1 - origin->y,
                                 snapRot, snapLogW, snapLogH,
                                 &syncLo, &syncHi, &syncAny);
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
#if XCURSOR_ON
    /* 软件光标精灵续画（写完/翻页之后）：变换同样用喂给参考系（
     * XCursor_swPosFrame）而非按 selfWindow 现查几何——拖拽步内窗口
     * 原点已前移（pos 记账在步前几何），按现几何正变换每步自带一步
     * 滞后偏移，与 present/旁路续画交替时即来回跳；喂给参考系下精灵
     * 面板位置恒=手指面板位置、只随喂给变化，三条重画路径同源零跳
     * 变。精灵原本在屏才续画；画失败时 xpbs_swCursorDraw 自动作废在
     * 画记录。 */
    if (swHadSprite)
    {
        XRect swRect;
        int srot = 0;
        int slogW = 0;
        int slogH = 0;
        XPoint sorigin;
        XCursor_swPosFrame(&srot, &slogW, &slogH, &sorigin);
        /* [陈旧备份回退根修 2026-10-23] 快照拖动条带直写可见缓冲同上：
         * 条带盖住精灵矩形时强制重备份同步新内容（恢复已把记录矩形还
         * 原为真实内容，重备份无精灵像素可存），防下一 present 头部恢
         * 复把拖动中画面在精灵矩形内回退。传 NULL 整块重建。 */
        if (xpbs_swCursorDraw(&info, visIndex, pixelBytes, srot, slogW,
                              slogH, &sorigin, &swRect, true, NULL, 0))
        {
            /* 画在 cacheflush/pan 之后：精灵行带单独收窄同步一次。 */
            size_t lo = (size_t)(visIndex * info.m_height + swRect.y) *
                            info.m_stride +
                        (size_t)swRect.x * pixelBytes;
            size_t hi = (size_t)(visIndex * info.m_height + swRect.y +
                                 swRect.height - 1) *
                            info.m_stride +
                        (size_t)(swRect.x + swRect.width) * pixelBytes;
            xpbs_cacheSyncRangeWarnOnce(ops,
                                        (uint8_t*)info.m_frameBuffer + lo,
                                        hi - lo);
        }
    }
#endif
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
