/******************************************************************************
 * @file       XPlatformFramebuffer_posix.c
 * @brief      Linux fbdev 显示驱动模板实现（/dev/fb0 + mmap 直写 +
 *             FBIOPAN_DISPLAY 翻页 + FBIO_WAITFORVSYNC 垂直同步）。
 * @details    实现 XPlatformDisplayDriver 契约（Src/XGui/Platform/
 *             XPlatformDisplayDriver.h）的 Linux fbdev 端，六钩子对应：
 *             - probe           ：open(XPLATFORM_FBDEV_DEVICE, O_RDWR)
 *                                 -> FBIOGET_FSCREENINFO（fix.line_length
 *                                 /smem_len）+ FBIOGET_VSCREENINFO（xres
 *                                 /yres/bpp/位域）-> mmap MAP_SHARED；
 *                                 无 /dev/fb 的环境（开发机桌面）open 即
 *                                 失败，恒定返回不可用（预期路径）；
 *             - formatNegotiate ：按 var 位域（red/green/blue/transp 的
 *                                 offset/length）识别面板扫描格式映射为
 *                                 XImageFormat（565 -> RGB16 等），与表
 *                                 面格式选择器 XGUI_BACKINGSTORE_IMAGE_
 *                                 FORMAT_RGB16 对照判定"直写 or 转换"；
 *             - pan             ：FBIOPAN_DISPLAY 翻页。activate 置
 *                                 FB_ACTIVATE_VBL 使翻页在下个垂直消隐
 *                                 生效——这是主线 ABI 对"pan 等待 VSYNC"
 *                                 的等价表达；部分厂商内核提供的
 *                                 FBIOPAN_VSYNC 扩展经 #ifdef 优先尝试
 *                                 （主线 linux/fb.h 未定义该宏时整段
 *                                 裁剪，开发机可编译）；单缓冲面板为
 *                                 no-op（直写即可见）；
 *             - cacheSync       ：DMA scanout 前 CPU cache 同步。主线
 *                                 用户态没有通用 clean/invalidate 入口，
 *                                 fbdev mmap 在一致性平台（多数 ARM SoC
 *                                 的 fb 为 uncached/write-combine）不需
 *                                 要同步；模板默认 msync(MS_SYNC) 尽力
 *                                 写回并对 EINVAL（设备映射不支持）视为
 *                                 成功，非一致性板级经注册自定义 ops
 *                                 覆盖本实现（BSP 专有 ioctl 接入点）；
 *             - waitVsync       ：FBIO_WAITFORVSYNC（主线 ABI，传 0 号
 *                                 VSYNC）；驱动不支持时以 FBIOGET_VBLANK
 *                                 1ms 轮询退化等待（count 变化沿或
 *                                 VSYNCING 置位），有超时上限不 busy loop；
 *             - stride          ：面板扫描格式返回 fix.line_length（硬件
 *                                 对齐可能大于 width*bpp）；其余格式回
 *                                 落公共层 XImageFormat_bytesPerLine。
 *             与 X11 窗口后端互斥共存（为什么零影响）：本文件不触碰
 *             XPlatformNativeWindow 任何状态，注册表由板级启动代码显
 *             式调用 XPlatformFramebuffer_register 才进入；X11 路径不
 *             查询显示驱动注册表。也绝不调用 XPlatformNativeWindow_
 *             isAvailable 判互斥——该入口会惰性拉起 X11 连接，嵌入式
 *             无 X 环境会被无辜建连。
 * @note       本文件只在「__linux__ && XGUI_ON && XPLATFORM_FBDEV_ON」
 *             时参与编译（开关默认 0）；内存体系：驱动全程无堆分配，
 *             状态静态 + mmap 设备映射，XMemory 管辖的堆内存此处无引用。
 *             单线程主循环设计（与 X11 后端同口径），不加锁。
 * @author     XinYueC 团队
 ******************************************************************************/
#include "XPlatformFramebuffer_posix.h"

#if defined(__linux__) && XGUI_ON && XPLATFORM_FBDEV_ON

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <time.h>
#include <unistd.h>
#include <linux/fb.h>

#include "XImageFormat.h"
#include "XPlatformNativeWindow.h" /* isAvailable：反向时序防护。 */
#include "XPlatformScreen.h"
/* 屏幕登记（opt-in，XPLATFORM_FBDEV_REGISTER_SCREEN）：经公共层既有
 * 通道把面板登记为框架 XScreen（平台层只采集+上报，公共层零改动）。
 * 三头各自带配置守卫（XSCREEN_ON / XGUIAPPLICATION_ON / WSI 四宏），
 * 全裁剪配置下仅声明消失、包含安全；登记函数体在同款守卫内。 */
#include "XScreen.h"
#include "XGuiApplication.h"
#include "XWindowSystemInterface.h"

/* ==================== 进程级驱动状态（静态，无堆分配） ==================== */

static int g_xpdfbFd = -1;                 /**< fb 设备描述符；<0 未打开。 */
static uint8_t* g_xpdfbMap = NULL;         /**< mmap 直写地址（拥有）。 */
static size_t g_xpdfbMapSize = 0;          /**< 映射区总长（字节）。 */
static struct fb_fix_screeninfo g_xpdfbFix;/**< 固定信息（line_length 等）。 */
static struct fb_var_screeninfo g_xpdfbVar;/**< 可变信息（xres/位域等）。 */
static XPlatformDisplayInfo g_xpdfbInfo;   /**< probe 能力快照。 */
static bool g_xpdfbDeviceReady = false;    /**< 设备已探测并映射。 */
static bool g_xpdfbRegistered = false;     /**< ops 表已进注册表。 */
/** @brief 运行期设备路径覆盖（板级经
 *  XPlatformNativeWindow_useFramebufferDriver 传入；NULL 用编译期宏）。 */
static const char* g_xpdfbDevicePath = NULL;

/* 显示驱动注册表：进程内至多一个活动驱动（单屏互斥，为什么见契约头）。
 * 当前唯一驱动实现随本编译单元落地；后续驱动增多再上提公共层 .c。 */
static const XPlatformDisplayDriverOps* g_xpdfbActiveOps = NULL;

/* ==================== 格式映射（var 位域 -> XImageFormat） ==================== */

/**
 * @brief      按 fb_var_screeninfo 位域识别面板扫描格式。
 * @details    为什么按位域而非 bpp 粗分：同为 16bpp 存在 565 与 555，
 *             32bpp 存在 RGBX/ARGB，直写正确性依赖位域精确匹配。位域
 *             offset 按 little-endian 内存布局解释（fbdev 惯例，ARM/
 *             x86 嵌入式面板均为此序）。
 * @param      var 已取回的可变屏幕信息。
 * @return     对应 XImageFormat；无法识别返回 XImageFormat_Invalid。
 */
static XImageFormat xpdfb_formatFromVar(const struct fb_var_screeninfo* var)
{
    if (var->bits_per_pixel == 16)
    {
        if (var->red.length == 5 && var->green.length == 6 &&
            var->blue.length == 5 && var->red.offset == 11 &&
            var->green.offset == 5 && var->blue.offset == 0)
            return XImageFormat_RGB16; /* 典型嵌入式 RGB565 面板。 */
        if (var->red.length == 5 && var->green.length == 5 &&
            var->blue.length == 5)
            return XImageFormat_RGB555;
    }
    else if (var->bits_per_pixel == 24)
    {
        if (var->red.length == 8 && var->green.length == 8 &&
            var->blue.length == 8 && var->red.offset == 16 &&
            var->green.offset == 8 && var->blue.offset == 0)
            return XImageFormat_RGB888;
    }
    else if (var->bits_per_pixel == 32)
    {
        if (var->red.length == 8 && var->green.length == 8 &&
            var->blue.length == 8 && var->red.offset == 16 &&
            var->green.offset == 8 && var->blue.offset == 0)
        {
            if (var->transp.length == 8 && var->transp.offset == 24)
                return XImageFormat_ARGB32;
            if (var->transp.length == 0)
                return XImageFormat_RGB32;
        }
    }
    return XImageFormat_Invalid;
}

/* ==================== 设备探测与释放 ==================== */

/**
 * @brief      打开设备并完成 FBIOGET_*SCREENINFO + mmap（幂等）。
 * @return     true 设备已就绪（含此前已就绪）；false 不可用。
 */
static bool xpdfb_probeDevice(void)
{
    int fd;
    struct fb_fix_screeninfo fix;
    struct fb_var_screeninfo var;
    size_t mapLen;
    void* map;
    XImageFormat format;
    if (g_xpdfbDeviceReady) return true;

    fd = open(g_xpdfbDevicePath ? g_xpdfbDevicePath : XPLATFORM_FBDEV_DEVICE,
              O_RDWR);
    if (fd < 0)
        return false; /* 无 /dev/fb 环境（开发机）：预期失败路径。 */

    if (ioctl(fd, FBIOGET_FSCREENINFO, &fix) != 0 ||
        ioctl(fd, FBIOGET_VSCREENINFO, &var) != 0)
    {
        close(fd);
        return false;
    }
    /* smem_len 为 0 的个别驱动按行距*yres_virtual 兜底估映射长度。 */
    mapLen = (size_t)fix.smem_len;
    if (mapLen == 0)
        mapLen = (size_t)fix.line_length * (size_t)var.yres_virtual;
    if (var.xres < 1 || var.yres < 1 || fix.line_length == 0 || mapLen == 0)
    {
        close(fd);
        return false;
    }
    map = mmap(NULL, mapLen, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (map == MAP_FAILED)
    {
        close(fd);
        return false;
    }

    format = xpdfb_formatFromVar(&var);
    g_xpdfbFd = fd;
    g_xpdfbMap = (uint8_t*)map;
    g_xpdfbMapSize = mapLen;
    g_xpdfbFix = fix;
    g_xpdfbVar = var;
    memset(&g_xpdfbInfo, 0, sizeof(g_xpdfbInfo));
    g_xpdfbInfo.m_width = (int)var.xres;
    g_xpdfbInfo.m_height = (int)var.yres;
    /* 面板物理尺寸（mm）：var.width/height 原值透传（内核 UAPI 注释
     * "picture in mm"），0=面板无 EDID/驱动未上报（未知），严禁编造
     * ——dpr 不得由 mm 反推（XPLATFORM_DISPLAY_INFO 契约 ABI 2）。 */
    g_xpdfbInfo.m_physicalWidthMm = (float)var.width;
    g_xpdfbInfo.m_physicalHeightMm = (float)var.height;
    g_xpdfbInfo.m_format = format;
    g_xpdfbInfo.m_bitsPerPixel = (int)var.bits_per_pixel;
    g_xpdfbInfo.m_stride = (size_t)fix.line_length;
    g_xpdfbInfo.m_frameBuffer = map;
    g_xpdfbInfo.m_frameBufferSize = mapLen;
    /* yres_virtual >= 2*yres 才有可翻页的后台缓冲；轮换翻页默认启用
     * （XPLATFORM_FBDEV_DOUBLEBUFFERED_ON=1），提交侧按累积脏区差带
     * 同步两缓冲，无整帧同步税。 */
    g_xpdfbInfo.m_doubleBuffered =
        XPLATFORM_FBDEV_DOUBLEBUFFERED_ON &&
        var.yres_virtual >= (uint32_t)var.yres * 2u;
    g_xpdfbDeviceReady = true;
    return true;
}

/** @brief 释放映射与设备，状态复位（未就绪安全 no-op）。 */
static void xpdfb_shutdown(void)
{
    if (g_xpdfbMap)
    {
        munmap(g_xpdfbMap, g_xpdfbMapSize);
        g_xpdfbMap = NULL;
    }
    g_xpdfbMapSize = 0;
    if (g_xpdfbFd >= 0)
    {
        close(g_xpdfbFd);
        g_xpdfbFd = -1;
    }
    g_xpdfbDeviceReady = false;
    memset(&g_xpdfbInfo, 0, sizeof(g_xpdfbInfo));
}

/* ==================== 契约六钩子实现 ==================== */

static bool xpdfb_probe(XPlatformDisplayInfo* outInfo)
{
    if (!xpdfb_probeDevice())
    {
        if (outInfo) memset(outInfo, 0, sizeof(*outInfo));
        return false;
    }
    if (outInfo) *outInfo = g_xpdfbInfo;
    return true;
}

static bool xpdfb_formatNegotiate(XImageFormat preferred,
                                  XImageFormat* outFormat)
{
    XImageFormat panel;
    if (outFormat) *outFormat = XImageFormat_Invalid;
    if (!xpdfb_probeDevice()) return false;
    panel = g_xpdfbInfo.m_format;
    if (panel == XImageFormat_Invalid)
    {
        if (outFormat) *outFormat = XImageFormat_Invalid;
        return false;
    }
    if (outFormat) *outFormat = panel;
    if (preferred == XImageFormat_Invalid)
        return true; /* 纯查询：仅报告面板格式。 */
    /* 直写可行当且仅当表面格式与面板扫描格式一致；不一致时面板格式
     * 已写入 outFormat，调用方据此建立转换路径（对标 QBackingStore 按
     * 屏幕格式分配缓冲，本框架编译期选择器在 XGuiConfig.h）。 */
    return preferred == panel;
}

static bool xpdfb_pan(int bufferIndex)
{
    if (!g_xpdfbDeviceReady || g_xpdfbFd < 0) return false;
    if (!g_xpdfbInfo.m_doubleBuffered)
        return bufferIndex == 0; /* 单缓冲直写即可见；翻页不可用。 */
    if (bufferIndex < 0 || bufferIndex > 1) return false;
    g_xpdfbVar.yoffset = (bufferIndex == 1) ? g_xpdfbVar.yres : 0;
    /* FB_ACTIVATE_VBL：翻页排队到下个垂直消隐生效，消除撕裂（主线 ABI
     * 的 pan+vsync 等价表达）。 */
    g_xpdfbVar.activate = FB_ACTIVATE_VBL;
#ifdef FBIOPAN_VSYNC
    /* 厂商内核扩展（部分 BSP 提供 pan+等 VSYNC 一体 ioctl）优先。 */
    if (ioctl(g_xpdfbFd, FBIOPAN_VSYNC, &g_xpdfbVar) == 0)
        return true;
#endif
    return ioctl(g_xpdfbFd, FBIOPAN_DISPLAY, &g_xpdfbVar) == 0;
}

static bool xpdfb_cacheSync(XPlatformDisplayCacheMode mode, void* address,
                            size_t length)
{
    void* base;
    size_t len;
    (void)mode; /* 模板统一按"写回"尽力处理；BSP 覆盖时按 mode 分支。 */
    if (!g_xpdfbDeviceReady || !g_xpdfbMap) return false;
    if (address)
    {
        base = address;
        len = length;
        /* 越界区间裁回映射区，防止板级误传拖垮内核调用。 */
        if ((const uint8_t*)base < g_xpdfbMap ||
            (const uint8_t*)base >= g_xpdfbMap + g_xpdfbMapSize)
            return false;
        if ((const uint8_t*)base + len > g_xpdfbMap + g_xpdfbMapSize)
            len = (size_t)(g_xpdfbMap + g_xpdfbMapSize -
                           (const uint8_t*)base);
    }
    else
    {
        base = g_xpdfbMap;
        len = g_xpdfbMapSize;
    }
    if (len == 0) return true;
    /* 主线用户态无通用 CPU 数据 cache clean 入口，本占位不谎报能力：
     * msync 面向文件页缓存回写，对设备 mmap 多数内核返回 0 但不执行
     * 任何 CPU DCache 清理动作——若在此返回 true，非一致性内存（DMA
     * 不与 CPU 缓存同步）且映射为 cached 的板子上调用方会误以为同步
     * 已完成，scanout 实际看不到 CPU 脏行（显示旧数据），比"没有钩子"
     * 更危险。因此：占位恒返回 false（= 未同步），消费链据此打印一次
     * 性诊断；映射为 uncached/write-combine 的板子（多数 fbdev 默认）
     * 本就无需软件同步，false 无害；真需要同步的板子经
     * XPlatformDisplayDriver_register 覆盖 cacheSync 为板级
     * cacheflush/dma_sync 实现，返回 true（对标 LVGL 的 cache
     * invalidate 回调注入模式：默认空钩子由用户注入，不假成功）。
     * 判定板级是否需要覆盖：真机若出现"CPU 明明画了、屏上缺/旧"的
     * 现象，即为非一致性 cached 映射。 */
    /* ARM Linux（非一致性 SoC，如本机全志 A33）：CPU write-back cache
     * 里的像素对 LCD DMA 不可见，必须显式清 DCache 才能上屏。主线内核
     * 为用户态映射提供私有 cacheflush 系统调用（__ARM_NR_cacheflush，
     * 0x0f0002，要求区间属本进程映射——fb mmap 满足）。真机验证：
     * 调用后"逐步显影/整窗中间态"消失（昆仑通态 A33 2026-09-27）。 */
#if defined(__arm__)
    {
        long rc = syscall(0x0f0002, (unsigned long)base,
                          (unsigned long)((char*)base + len), 0);
        if (rc == 0) return true;
    }
#endif
    (void)msync(base, len, MS_SYNC); /* 非 ARM 或系统调用失败：尽力
                                        写回页缓存；不据其判定 CPU
                                        cache 状态（见上）。 */
    return false; /* 未做 CPU cache 同步，不谎报成功。 */
}

static bool xpdfb_waitVsync(int timeoutMilliseconds)
{
    struct fb_vblank vbl;
    unsigned elapsed = 0;
    unsigned limit;
    int zero = 0;
    unsigned int retraceCount = 0;
    bool haveRetraceBase = false;
    if (!g_xpdfbDeviceReady || g_xpdfbFd < 0) return false;
    /* 主线 ABI：FBIO_WAITFORVSYNC，参数为 VSYNC 号（面板取 0）。 */
    if (ioctl(g_xpdfbFd, FBIO_WAITFORVSYNC, &zero) == 0)
        return true;
    /* 兜底：驱动不支持专用 ioctl 时按 FBIOGET_VBLANK 轮询等待——
     * 优先等垂直回扫计数变化沿（HAVE_COUNT），否则等 VSYNCING 置位
     * （HAVE_VSYNC）。1ms 步进，上限防呆（默认 100ms，约 6 帧@60Hz）。 */
    limit = timeoutMilliseconds > 0 ? (unsigned)timeoutMilliseconds : 100u;
    while (elapsed < limit)
    {
        memset(&vbl, 0, sizeof(vbl));
        if (ioctl(g_xpdfbFd, FBIOGET_VBLANK, &vbl) == 0)
        {
            if (vbl.flags & FB_VBLANK_HAVE_COUNT)
            {
                if (haveRetraceBase && vbl.count != retraceCount)
                    return true; /* 垂直回扫计数前进：已跨过一帧。 */
                retraceCount = vbl.count;
                haveRetraceBase = true;
            }
            else if ((vbl.flags & FB_VBLANK_HAVE_VSYNC) &&
                     (vbl.flags & FB_VBLANK_VSYNCING))
                return true;
        }
        {
            struct timespec ts;
            ts.tv_sec = 0;
            ts.tv_nsec = 1000000L; /* 1ms。 */
            nanosleep(&ts, NULL);
        }
        ++elapsed;
    }
    return false;
}

static size_t xpdfb_stride(int width, XImageFormat format)
{
    int depth;
    size_t pixelBytes;
    size_t minimal;
    if (width <= 0 || !g_xpdfbDeviceReady) return 0;
    depth = XImageFormat_bitDepth(format);
    if (depth <= 0) return 0;
    pixelBytes = (size_t)((depth + 7) / 8);
    minimal = (size_t)width * pixelBytes;
    /* 面板扫描格式：硬件行距可能含对齐填充，必须以 line_length 为准，
     * 否则逐行直写会整体错位。 */
    if (format == g_xpdfbInfo.m_format && g_xpdfbInfo.m_stride >= minimal)
        return g_xpdfbInfo.m_stride;
    /* 其余软件格式回落公共层口径（与 XImage/XBackingStore 一致）。 */
    return (size_t)XImageFormat_bytesPerLine(width, format);
}

/* ==================== ops 表与注册入口 ==================== */

/** @brief fbdev 驱动 ops 表（六钩子齐全，静态生命期）。 */
static const XPlatformDisplayDriverOps g_xpdfbOps =
{
    "fbdev",                              /* m_name。 */
    XPLATFORM_DISPLAY_DRIVER_ABI_VERSION, /* m_abiVersion。 */
    xpdfb_probe,
    xpdfb_formatNegotiate,
    xpdfb_pan,
    xpdfb_cacheSync,
    xpdfb_waitVsync,
    xpdfb_stride
};

/* ---- 契约注册表（注册表实现暂随本编译单元，见契约头 @note） ---- */

bool XPlatformDisplayDriver_register(const XPlatformDisplayDriverOps* ops)
{
    if (!ops || !ops->m_name) return false;
    if (ops->m_abiVersion != XPLATFORM_DISPLAY_DRIVER_ABI_VERSION) return false;
    /* 六钩子必须齐全：缺项的驱动注册即拒绝，消费方无需逐指针判空。 */
    if (!ops->probe || !ops->formatNegotiate || !ops->pan ||
        !ops->cacheSync || !ops->waitVsync || !ops->stride) return false;
    if (g_xpdfbActiveOps) return false; /* 单屏互斥：先注册者生效。 */
    g_xpdfbActiveOps = ops;
    return true;
}

void XPlatformDisplayDriver_unregister(const XPlatformDisplayDriverOps* ops)
{
    if (ops && g_xpdfbActiveOps == ops)
        g_xpdfbActiveOps = NULL;
}

const XPlatformDisplayDriverOps* XPlatformDisplayDriver_active(void)
{
    return g_xpdfbActiveOps;
}

/* ---- 屏幕登记（opt-in：XPLATFORM_FBDEV_REGISTER_SCREEN，缺省 0） ---- */

/* 定义守卫=opt-in ∧ 框架屏幕/应用/WSI 模块就绪（WSI handleScreenAdded
 * 声明守卫要求 XWINDOWSYSTEMINTERFACE_ON && XGUIAPPLICATION_ON &&
 * XWINDOW_ON && XWINDOWEVENT_ON 四宏，XWindowSystemInterface.h:41——
 * 在定版三宏 XSCREEN_ON/XGUIAPPLICATION_ON/XWINDOWSYSTEMINTERFACE_ON
 * 上补齐后两宏）；任一不满足即空壳（no-op），调用点无条件调用空壳，
 * 保证任何配置下无「定义未引用」静态函数（-Wall 干净）。 */
#if XPLATFORM_FBDEV_REGISTER_SCREEN && XSCREEN_ON && \
    XGUIAPPLICATION_ON && XWINDOWSYSTEMINTERFACE_ON && \
    XWINDOW_ON && XWINDOWEVENT_ON

static XScreen* g_xpdfbScreen = NULL;  /**< fbdev 唯一逻辑屏（平台层所有）。 */
static bool g_xpdfbScreenDone = false; /**< 仅 handleScreenAdded 成功后置位。 */

/**
 * @brief  把 fbdev 面板登记为框架 XScreen（登记范式对齐安卓定版样板
 *         xpad_ensureScreenRegistered，XPlatformWindowAndroid.c）。
 * @details 单屏静态参数，一次性登记；序与安卓定版一致=先登记后回填
 *          （XGuiApplication_screenAdded 内部挂 dpr/logicalDpi 变化槽，
 *          先回填后登记会把 dpr 推送发在挂槽之前而丢失）：
 *          - dpr = XPLATFORM_FBDEV_DEVICE_PIXEL_RATIO（板级显式注入，
 *            严禁由 mm 反推）；<=0 非法板级值兜底回落 1.0；
 *          - geometry = (0,0, xres/dpr, yres/dpr)（round，框架恒逻辑
 *            像素，物理=逻辑×dpr 同一 round 式，对标安卓 surf/dpr）；
 *          - physicalSize = 契约 mm 原值（0=未知，不编造）；
 *          - logicalDpi 永不回填（保持缺省 96；fbdev 无偏好源，物理
 *            放大由 dpr 单独承载——§0.2 归一化铁律下 scaleDpi 恒 96）。
 *          所有权：handleScreenAdded 返回 false（无应用单例/注册表失
 *          败）即 XClassDelete 回收、不置守卫——非安卓重试保留
 *          模式（fbdev 单屏参数静态，重建零成本，不占所有权灰色态）；
 *          应用单例未就绪时不创建对象直接返回（调用方须在 GUI 单例
 *          就绪后调用，见宏注释）。重复调用幂等（done 守卫）。
 *          线程口径：主线程（与 XPlatformFramebuffer_register 同线程、
 *          GUI 初始化期单线程；XObject 信号面无并发）。
 */
static void xpdfb_ensureScreenRegistered(void)
{
    XRect logicalGeom;
    XSizeF mm;
    float dpr = XPLATFORM_FBDEV_DEVICE_PIXEL_RATIO;
    if (g_xpdfbScreenDone) return;
    if (!g_xpdfbDeviceReady || !XGuiApplication_instance()) return;
    if (dpr <= 0.0f) dpr = 1.0f; /* 板级非法值兜底（防除零/负缩放）。 */
    /* 强制 DPI（XGUI_FORCE_DPI>0 时替换板级宏/原生读数，统一切口）。 */
    dpr = XPlatformScreen_applyDpiOverride(dpr);
    g_xpdfbScreen = XScreen_create();
    if (!g_xpdfbScreen) return;
    if (!XWindowSystemInterface_handleScreenAdded(g_xpdfbScreen))
    {
        /* 登记失败：无人接管，必须回收（所有权契约，WSI handleScreen
         * Added @details）。不置守卫，下轮调用重建重试。 */
        XClassDelete(g_xpdfbScreen);
        g_xpdfbScreen = NULL;
        return;
    }
    XGuiApplication_setPrimaryScreen(g_xpdfbScreen);
    g_xpdfbScreenDone = true;
    /* 回填（先登记后回填，见上；setter 值不变即静默差分）。 */
    XRect_init(&logicalGeom, 0, 0,
               (int)((float)g_xpdfbInfo.m_width / dpr + 0.5f),
               (int)((float)g_xpdfbInfo.m_height / dpr + 0.5f));
    mm.width = g_xpdfbInfo.m_physicalWidthMm;
    mm.height = g_xpdfbInfo.m_physicalHeightMm;
    XScreen_setDevicePixelRatio(g_xpdfbScreen, dpr);
    XScreen_setPhysicalSize(g_xpdfbScreen, &mm);
    XScreen_setGeometry(g_xpdfbScreen, &logicalGeom);
}

#else /* 屏幕登记被裁剪（opt-in 关闭或框架屏幕/应用/WSI 模块被裁剪）。 */

static void xpdfb_ensureScreenRegistered(void)
{
}

#endif /* XSCREEN_ON && XGUIAPPLICATION_ON && XWINDOWSYSTEMINTERFACE_ON && … */

/* ---- fbdev 注册入口（板级启动代码调用） ---- */

bool XPlatformFramebuffer_register(void)
{
    if (g_xpdfbRegistered)
        return XPlatformDisplayDriver_active() == &g_xpdfbOps;
    /* 反向时序防护：fbdev 必须在 GUI/X11 初始化之前注册。若已有 X11
     * 原生连接（先建过窗），fbdev 直写路径此后会与 X11 present 并存
     * ——格式匹配时 X11 窗口冻屏（fb 直写优先），属设计外时序。响亮
     * 告警并拒绝注册（不产生半激活状态），提示板级调整 main 初始化
     * 顺序（对标 Qt linuxfb 与 X11 后端互斥的加载期约束）。 */
    if (XPlatformNativeWindow_isAvailable())
    {
        fprintf(stderr,
                "[fbdev] ERROR: X11 connection already established; "
                "registering the framebuffer driver now would freeze "
                "existing windows. Call XPlatformFramebuffer_register() "
                "before XGuiApplication initialization.\n");
        return false;
    }
    if (!xpdfb_probeDevice())
        return false; /* 无 /dev/fb 环境：不可用，不注册（预期路径）。 */
    if (!XPlatformDisplayDriver_register(&g_xpdfbOps))
    {
        /* 已有活动驱动（互斥失败）：释放本设备，保持零占用。 */
        xpdfb_shutdown();
        return false;
    }
    g_xpdfbRegistered = true;
    /* opt-in：把面板登记为框架 XScreen（缺省 0 时函数为空壳 no-op，
     * 行为与历史版本逐位一致；定义守卫见上方屏幕登记段注释）。 */
    xpdfb_ensureScreenRegistered();
    return true;
}

void XPlatformFramebuffer_unregister(void)
{
    if (!g_xpdfbRegistered) return;
    XPlatformDisplayDriver_unregister(&g_xpdfbOps);
    xpdfb_shutdown();
    g_xpdfbRegistered = false;
}

bool XPlatformFramebuffer_isAvailable(void)
{
    return g_xpdfbRegistered &&
           XPlatformDisplayDriver_active() == &g_xpdfbOps;
}

const XPlatformDisplayDriverOps* XPlatformFramebuffer_driverOps(void)
{
    return &g_xpdfbOps;
}

/**
 * @brief      板级便捷入口：按设备路径探测并注册 fbdev 显示驱动
 *             （嵌入式 main 在 GUI 初始化前调用一次；桌面默认不调用）。
 * @details    内部即 probe + register（XPlatformFramebuffer_register 同款
 *             流程）：open(device) -> FBIOGET_*SCREENINFO -> mmap -> 识别
 *             面板格式 -> 进注册表。注册成功后公共层消费链接管上屏
 *             （后备存储按面板格式协商分配、present 经
 *             XPlatformBackingStore_posix.c 直写），X11 窗口创建路径
 *             拒绝（单屏互斥，见 XPlatformNativeWindow_posix.c）。重复
 *             调用幂等：已注册时直接返回当前可用状态，不再重复 probe；
 *             设备路径只在首次 probe 前生效。
 * @param      device fb 设备节点（如 "/dev/fb1"）；NULL/空串用编译期
 *             宏 XPLATFORM_FBDEV_DEVICE（默认 "/dev/fb0"）。
 * @return     true 已注册为活动显示驱动；false 不可用或已有活动驱动。
 */
bool XPlatformNativeWindow_useFramebufferDriver(const char* device)
{
    if (g_xpdfbRegistered)
        return XPlatformFramebuffer_isAvailable(); /* 幂等。 */
    if (device && *device)
        g_xpdfbDevicePath = device;
    return XPlatformFramebuffer_register();
}

/* ==================== XPlatformScreen 契约实现（fbdev） ==================== */
/* 统一平台 API 的 fbdev 侧：数据源与 xpdfb_ensureScreenRegistered 同源
   （probe 快照 g_xpdfbInfo + 板级 dpr 宏），公共层经
   Src/XGui/Platform/XPlatformScreen.h 的统一签名拉取。
   与 X11 实现（XPlatformNativeWindow_posix.c）同符号互斥：X11 在场时
   本实现让位（fbdev 面向无 X11 的嵌入门；XINYUE_C_HAS_X11 由 CMake
   find_package(X11) 注入）。与安卓实现（XSystemAndroid.c）同样互斥：
   安卓虽 __linux__ 且 XPLATFORM_FBDEV_ON=1（显示驱动直写模型），但
   屏幕信息源必须唯一——读数让位给 JNI DisplayMetrics。 */

#if !defined(XINYUE_C_HAS_X11) && !defined(__ANDROID__)

bool XPlatformScreen_isAvailable(void)
{
    return g_xpdfbDeviceReady && XPlatformDisplayDriver_active() == &g_xpdfbOps;
}

bool XPlatformScreen_queryDpi(XPlatformScreenDpiInfo* out)
{
    float dpr;
    if (!out) return false;
    if (!g_xpdfbDeviceReady) return false;
    dpr = XPLATFORM_FBDEV_DEVICE_PIXEL_RATIO;
    if (dpr <= 0.0f) dpr = 1.0f; /* 板级非法值兜底（同登记口径）。 */
    dpr = XPlatformScreen_applyDpiOverride(dpr);
    out->m_density = dpr;
    /* 板级未提供密度档：0=未知（诊断字段，严禁由 mm 编造）。 */
    out->m_densityDpi = 0;
    if (g_xpdfbInfo.m_physicalWidthMm > 0.0f &&
        g_xpdfbInfo.m_physicalHeightMm > 0.0f)
    {
        out->m_xdpi = (int)((float)g_xpdfbInfo.m_width * 25.4f /
                            g_xpdfbInfo.m_physicalWidthMm + 0.5f);
        out->m_ydpi = (int)((float)g_xpdfbInfo.m_height * 25.4f /
                            g_xpdfbInfo.m_physicalHeightMm + 0.5f);
    }
    else
    {
        out->m_xdpi = 0;
        out->m_ydpi = 0;
    }
    out->m_valid = true;
    return true;
}

bool XPlatformScreen_queryFrameSize(int* outWidth, int* outHeight)
{
    float dpr;
    if (!outWidth || !outHeight) return false;
    if (!g_xpdfbDeviceReady) return false;
    dpr = XPLATFORM_FBDEV_DEVICE_PIXEL_RATIO;
    if (dpr <= 0.0f) dpr = 1.0f;
    /* 逻辑口径 = 面板物理 ÷ dpr（与登记 geometry 同一 round 式）。 */
    *outWidth = (int)((float)g_xpdfbInfo.m_width / dpr + 0.5f);
    *outHeight = (int)((float)g_xpdfbInfo.m_height / dpr + 0.5f);
    return true;
}

bool XPlatformScreen_queryOrigin(int* outX, int* outY)
{
    /* fbdev 单屏直写：触摸即面板本地坐标，无窗口原点偏移。 */
    (void)outX;
    (void)outY;
    return false;
}

#endif /* !defined(XINYUE_C_HAS_X11) */

#endif /* defined(__linux__) && XGUI_ON && XPLATFORM_FBDEV_ON */
