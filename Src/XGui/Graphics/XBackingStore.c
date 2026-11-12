/******************************************************************************
 * @file       XBackingStore.c
 * @brief      XBackingStore 后备存储类实现（对标 Qt 6.8 QBackingStore）。
 * @author     XinYueC 团队
 ******************************************************************************/
#if XGUI_ON

#include "XBackingStore.h"

#include "XAlgorithm.h"
#include "XMemory.h"
#include "XString.h"
#include "XImageFormat.h"
#include "XImage.h"
#include <limits.h>
#include <math.h>

#if XBACKINGSTORE_ON && XPLATFORMBACKINGSTORE_ON && XPLATFORMINTEGRATION_ON

#include "XPlatformBackingStore.h"
#include "XPlatformIntegration.h"
#if XGUIAPPLICATION_ON
#include "XGuiApplication.h"
#endif /* XGUIAPPLICATION_ON */
#include "XPlatformNativeInterface.h"
/** @brief XBackingStore 私有数据块。 */
struct XBackingStorePrivate
{
    XWindow*              m_window;   /**< 绑定的窗口（借用，不持有）。 */
    XPlatformBackingStore* m_platform; /**< 平台后端句柄（拥有，delete 释放）。 */
    XSize                 m_size;     /**< 最近一次接受的有效 resize 尺寸
                                           （逻辑像素；Qt QBackingStore::size
                                           语义——物理尺寸=逻辑×dpr 只在
                                           平台调用边界展开）。 */
    XSize                 m_nativeSize; /**< 平台缓冲物理尺寸快照（逻辑×dpr；
                                           Qt QBackingStorePrivate::nativeSize，
                                           beginPaint 据此自动重检 resize）。 */
    XRegion               m_staticContents; /**< Qt 语义的静态区域快照（逻辑像素）。 */
};

/* ==================== DPR 渲染边界（Qt qbackingstore.cpp 对齐） ====================
 * 启用（XGuiApplication_highDpiRenderingEnabled，env XGUI_DPR_RENDER=0 关）
 * 且窗口 dpr>1 时：平台缓冲=物理尺寸（逻辑×dpr）、beginPaint/flush/scroll
 * 区域在公共层×dpr（Qt qbackingstore.cpp:117-149/197-225/254-266），
 * paintDevice 直挂 devicePixelRatio 元数据——【有意偏差】Qt:141-148 刻意
 * 另建独立共享数据镜像承载 dpr 防回灌平台插件，本实现直写平台图像：
 * 安卓 present 层以该标签为物理/逻辑缓冲判定的权威事实（详见
 * XBackingStore_paintImage 内注）。dpr==1 时全部直通，旧链路逐位零回归。
 * 逻辑→设备矩形用**边坐标取整**（四边各自 round 后求尺寸），相邻矩形
 * 映射后仍无缝隙/无重叠。 */

/** @brief 按 Qt qhighdpiscaling.cpp roundScaleFactor（:334-367）对窗口
 *         dpr 应用取整策略——XGuiApplication_setHighDpiScaleFactorRoundingPolicy
 *         的消费点（2026-11-12 扫描 P1#5 接线：此前 m_dpiPolicy 全链无
 *         消费者，违反「不许留选了不兑现的选项」红线）。
 * @details Unset/PassThrough=原值（Qt 6.8 缺省 PassThrough，
 *          qguiapplication.cpp:139；X 缺省 Unset 同语义）——缺省链
 *          逐位零变化。已知约束：posix/win32 present 1:1 闸读原始
 *          XWindow_devicePixelRatio（Drive 层文件，本批未授权）——策略
 *          把 >1 因子收整到 1 时（Floor/RoundPreferFloor × 分数 dpr）
 *          公共层走逻辑缓冲而平台闸仍按物理缓冲直拷，须 Drive 侧同批
 *          跟进；安卓 present 读图像标签（非原始 dpr），不受影响。 */
static float xbs_applyDpiRoundingPolicy(float dpr)
{
#if XGUIAPPLICATION_ON
    float rounded;
    switch (XGuiApplication_highDpiScaleFactorRoundingPolicy())
    {
        case XGuiDpiRoundingPolicy_Round:
            /* Qt qRound：半值远离零（qnumeric.h:372-375）。 */
            rounded = (dpr >= 0.0f) ? floorf(dpr + 0.5f)
                                    : ceilf(dpr - 0.5f);
            break;
        case XGuiDpiRoundingPolicy_Ceil:
            rounded = ceilf(dpr);
            break;
        case XGuiDpiRoundingPolicy_Floor:
            rounded = floorf(dpr);
            break;
        case XGuiDpiRoundingPolicy_RoundPreferFloor:
            /* Qt:356-359：小数部分 <0.75 向下（「小 UI」优先），否则向上。 */
            rounded = (dpr - floorf(dpr) < 0.75f) ? floorf(dpr)
                                                  : ceilf(dpr);
            break;
        case XGuiDpiRoundingPolicy_PassThrough:
        case XGuiDpiRoundingPolicy_Unset:
        default:
            return dpr;
    }
    /* Qt:366-368：因子下限钳 1（Qt 不支持 <1 因子渲染）。 */
    if (rounded < 1.0f)
        rounded = 1.0f;
    return rounded;
#else
    return dpr; /* 无应用层：策略源不可用，维持透传（现状行为）。 */
#endif /* XGUIAPPLICATION_ON */
}

/** @brief 本后备存储的设备像素比：开关关闭/无窗口/dpr<=1 一律 1.0（直通）。 */
static float xbs_backingStoreDpr(const XBackingStore* self)
{
    float dpr;
    if (!self || !self->m_data) return 1.0f;
#if XGUIAPPLICATION_ON
    if (!XGuiApplication_highDpiRenderingEnabled()) return 1.0f;
#endif /* XGUIAPPLICATION_ON */
    dpr = XWindow_devicePixelRatio(self->m_data->m_window);
    if (!isfinite((double)dpr) || !(dpr > 1.0f))
        return 1.0f;
    dpr = xbs_applyDpiRoundingPolicy(dpr);
    if (!(dpr > 1.0f))
        return 1.0f; /* 策略收整到 1：整窗走逻辑直通链（Qt 因子下限同点）。 */
    return dpr;
}

/** @brief 单值→设备像素换算（double 域四则 + int 范围钳位）。
 *  @details Qt 侧坐标换算全程 qreal（qhighdpiscaling_p.h 的 map 系）；
 *           本实现坐标是 int——在 double 域乘/除 dpr 后再取整，消除
 *           `(float)(rect->x + rect->width)` 这类「整型加法先于浮点
 *           转换」的溢出口径（扫描 P2#17；极端坐标 >2^31/dpr 时先在
 *           int 域溢出），以及 (int) 截断超出 int 范围浮点值的 UB
 *           （C99 6.3.1.4）。取整方向对齐 Qt qRound（半值远离零，
 *           qnumeric.h:372-375）：非负域与旧 floorf(v+0.5) 逐位等值，
 *           负半值差 1px（旧式向 +∞ 收，qRound 向远离零收）。 */
static int xbs_roundToDevice(double v)
{
    if (!(v > -2147483648.0))
        return INT_MIN; /* NaN/越下界钳位（NaN 比较恒假落入此支）。 */
    if (v >= 2147483647.0)
        return INT_MAX;
    return (int)round(v);
}

/** @brief 逻辑矩形→设备矩形（四边各自 round，保相邻无缝）。 */
static XRect xbs_mapRectToDevice(const XRect* rect, float dpr)
{
    XRect out;
    int left;
    int top;
    int right;
    int bottom;
    left = xbs_roundToDevice((double)rect->x * dpr);
    top = xbs_roundToDevice((double)rect->y * dpr);
    /* 右/下开边分项浮点（Qt qhighdpiscaling map 语义）：先整型相加再
       乘 dpr 的旧式在极端坐标会先 int 溢出，分项后各自 double 域。 */
    right = xbs_roundToDevice((double)rect->x * dpr +
                              (double)rect->width * dpr);
    bottom = xbs_roundToDevice((double)rect->y * dpr +
                               (double)rect->height * dpr);
    out.x = left;
    out.y = top;
    out.width = right - left;
    out.height = bottom - top;
    return out;
}

/** @brief 逻辑区域→设备区域（逐矩形映射；out 由调用方 init/deinit）。 */
static void xbs_mapRegionToDevice(const XRegion* region, float dpr,
                                  XRegion* out)
{
    int i;
    for (i = 0; i < region->count; ++i)
    {
        XRect mapped = xbs_mapRectToDevice(&region->rects[i], dpr);
        XRegion_addRect(out, &mapped);
    }
}

/**
 * @brief 按 Qt QBackingStore::handle() 语义懒创建平台后端。
 * @details 构造阶段已经有平台后端时直接复用；如果对象创建时尚未
 *          建立 XGuiApplication 平台集成，则在后续调用 handle() 时
 *          再尝试创建。平台句柄始终由 XBackingStore 持有，调用方只借用。
 */
static XPlatformBackingStore* xBackingStore_ensureHandle(
        const XBackingStore* self)
{
    if (!self || !self->m_data) return NULL;
    if (self->m_data->m_platform) return self->m_data->m_platform;
#if XGUIAPPLICATION_ON && XPLATFORMNATIVEINTERFACE_ON
    {
        XPlatformNativeInterface* ni;
        XPlatformIntegration* gpi;
        ni = XGuiApplication_platformNativeInterface();
        gpi = ni ? XPlatformNativeInterface_integration(ni) : NULL;
        if (gpi)
            self->m_data->m_platform =
                XPlatformIntegration_createPlatformBackingStore(
                    gpi, self->m_data->m_window);
    }
#endif /* XGUIAPPLICATION_ON && XPLATFORMNATIVEINTERFACE_ON */
    return self->m_data->m_platform;
}

/** @brief 释放 XBackingStore：平台后端与私有块一并销毁。 */
static void VXBackingStore_deinit(XBackingStore* self)
{
    if (!self) return;
    if (self->m_data) {
        if (self->m_data->m_platform) {
            XPlatformBackingStore_delete(self->m_data->m_platform);
            self->m_data->m_platform = NULL;
        }
        XRegion_deinit(&self->m_data->m_staticContents);
        XFree_System(self->m_data);
        self->m_data = NULL;
    }
    XClass_Deinit_Parent(XObject, (XObject*)self);
}

XVtable* XBackingStore_class_init(void)
{
    XVTABLE_INIT_DEFAULT(XBackingStore)
    XVTABLE_INHERIT_XCLASS(XObject);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Deinit, VXBackingStore_deinit);
    return XVTABLE_DEFAULT;
}

void XBackingStore_init(XBackingStore* self, XWindow* window)
{
    if (!self) return;
    XMemset(self, 0, sizeof(XBackingStore));
    XObject_init((XObject*)self);
    XClassSetVtable(self, XBackingStore);
    self->m_data = (XBackingStorePrivate*)XMalloc_System(sizeof(XBackingStorePrivate));
    if (!self->m_data) return;
    XMemset(self->m_data, 0, sizeof(XBackingStorePrivate));
    self->m_data->m_window = window;
    XSize_init(&self->m_data->m_size, 0, 0);
    XRegion_init(&self->m_data->m_staticContents);
    /* 优先按 Qt 构造路径创建；没有应用/集成层时安全退化为空后端。
     * 后续 handle() 仍会再次尝试，以覆盖集成层延后建立的场景。 */
    xBackingStore_ensureHandle(self);
}

XBackingStore* XBackingStore_create_ex(XMemoryType memory, XWindow* window)
{
    XBackingStore* self = (XBackingStore*)XMemory_malloc(sizeof(XBackingStore), memory);
    if (!self) return NULL;
    XBackingStore_init(self, window);
    Set_Class_Memory(self, memory);
    Set_Class_IsHeap(self, true);
    return self;
}

XWindow* XBackingStore_window(const XBackingStore* self)
{
    return (self && self->m_data) ? self->m_data->m_window : NULL;
}

XImage* XBackingStore_paintImage(XBackingStore* self)
{
    XPlatformBackingStore* platform = XBackingStore_handle(self);
    XImage* image;
    float dpr;
    if (!platform) return NULL;
    image = XPlatformBackingStore_paintDevice(platform);
    if (!image) return NULL;
    /* DPR 模式下给绘制设备挂 devicePixelRatio：QPainter 侧据此装配
       scale(dpr) 基础变换（qpainter.cpp updateMatrix 末端
       hidpiScaleTransform 同源）。
       【有意偏差 2026-11-12 扫描 P1#3】Qt qbackingstore.cpp:141-148 刻意
       另建共享数据独立镜像（QImage(bits,...) 包装同源像素）承载 dpr，
       防新 dpr 回灌平台插件；本实现把标签**直写**平台 paintDevice 图像
       ——安卓 present 层以该标签为「物理/逻辑缓冲」判定的权威事实
       （XPlatformWindowAndroid.c:1744-1746/1995/2052：标签>1 ⇒ present
       1:1 直拷，否则按 presentScale 放大），XWidget 表面裁剪/瓦片/grab
       路径同样消费该标签；posix/win32/DRM 只读图像宽高物理值。改独立
       镜像须同步改上述全部读点（跨批次协调面），本批次维持直写。
       旧链（dpr=1）显式复位 1.0，防跨屏拖窗/回退闸切换残留。
       【refCount 护栏】标签值变化且图像仍被共享（refCount>1）时，
       XImage_setDevicePixelRatio 会经 detachMetadata 整幅克隆像素——
       native 零拷贝模式下会把 painter 目标从驱动缓冲上分叉（画面不落
       盘）。2026-11-12 全库静态核实 paintImage 调用时 refCount 恒为 1：
       快照路径全部深拷贝（xpbs_deepCopy 注释明言 XClassCopy 共享引用
       不能用；XBackingStore_toImage/拖动快照 memcpy 同）；resize 内
       oldImage/nativeImage 两处 COW 引用均在 resize 返回前释放；双缓冲
       m_image/m_image2 各持独立数据。护栏为防御性：若未来出现持共享
       引用跨帧的新路径，修该持有方，不得放开本护栏。 */
    dpr = xbs_backingStoreDpr(self);
    if (dpr != XImage_devicePixelRatio(image) && !XImage_isDetached(image))
        return image; /* 共享图像拒写标签（见上注）：静默维持旧标签。 */
    XImage_setDevicePixelRatio(image, dpr);
    return image;
}

bool XBackingStore_nextTile(XBackingStore* self, XRect* tileRect)
{
    XPlatformBackingStore* platform = XBackingStore_handle(self);
    if (!platform || !tileRect) return false;
    return XPlatformBackingStore_nextTile(platform, tileRect);
}

XPoint XBackingStore_paintOrigin(const XBackingStore* self)
{
    XPoint out;
    XPoint_init(&out, 0, 0);
    if (!self || !self->m_data || !self->m_data->m_platform)
        return out;
    out = XPlatformBackingStore_paintOrigin(self->m_data->m_platform);
    {
        /* 平台 paintOrigin 为设备像素域（PARTIAL tile 光标等）；消费方
           （XWidget_paintOffset=逻辑累加偏移−本值）按逻辑域运算——
           DPR 模式下 ÷dpr 归域，dpr=1 逐位等值。 */
        float dpr = xbs_backingStoreDpr(self);
        if (dpr != 1.0f)
        {
            out.x = xbs_roundToDevice((double)out.x / dpr);
            out.y = xbs_roundToDevice((double)out.y / dpr);
        }
    }
    return out;
}

XPlatformBackingStore* XBackingStore_handle(const XBackingStore* self)
{
    /* QBackingStore::handle() 在首次调用时创建平台对象，之后只返回同一
     * 借用句柄；它不把平台对象的所有权转交调用方。 */
    return xBackingStore_ensureHandle(self);
}

bool XBackingStore_toImage(XBackingStore* self, XImage* out)
{
    XPlatformBackingStore* platform = XBackingStore_handle(self);
    if (!platform) return false;
    return XPlatformBackingStore_toImage(platform, out);
}

void XBackingStore_resize(XBackingStore* self, const XSize* size)
{
    XPlatformBackingStore* platform;
    XSize native;
    float dpr;
    if (!self || !self->m_data || !size) return;
    if (size->width < 0 || size->height < 0) return;
    /* 对标 Qt QBackingStore::resize（qbackingstore.cpp:232-238）：公共
       size/nativeSize 先行更新，平台后端收**物理尺寸**（逻辑×dpr）；
       即使后端暂时不可用，size() 仍反映调用方最近一次传入的有效尺寸。
       dpr==1 时 native==size，旧链逐位透传。 */
    dpr = xbs_backingStoreDpr(self);
    self->m_data->m_size = *size;
    native.width = xbs_roundToDevice((double)size->width * dpr);
    native.height = xbs_roundToDevice((double)size->height * dpr);
    if (native.width < 0) native.width = 0;
    if (native.height < 0) native.height = 0;
    self->m_data->m_nativeSize = native;
    /* [2026-11-12 扫描 P2#15 探针零残留] 原此处有注释停用的
     * 「DPR 首配 dpr>1 全字号免 FT hint」接管段（(void)dpr +
     * XFontFt_setHintMinPxOverride 调用）——2026-11-09 三轮回退后
     * 摘除定位、setter 本体保留待 A/B 重验；按 AGENTS.md「连 include
     * 一起清」整段删除（XFontFt.h include 一并移除），接管意图与
     * A/B 重验事项改由 XFontFt.h/XFontFt.c 的休眠实验态文档承载。 */
    platform = XBackingStore_handle(self);
    if (platform) {
        XPlatformBackingStore_resize(platform, &native);
        /* 平台后端只需要当前缓冲范围内的静态提示，而公共快照不能被
         * 后端为实现提示而裁剪；重新设置会让各后端按自身缓冲裁剪。
         * 静态提示按目标缓冲域推送（Qt:237 一并 scale 语义）；公共
         * 快照保持逻辑域。 */
        if (dpr == 1.0f) {
            XPlatformBackingStore_setStaticContents(
                platform, &self->m_data->m_staticContents);
        }
        else
        {
            XRegion mapped;
            XRegion_init(&mapped);
            xbs_mapRegionToDevice(&self->m_data->m_staticContents, dpr,
                                  &mapped);
            XPlatformBackingStore_setStaticContents(platform, &mapped);
            XRegion_deinit(&mapped);
        }
    }
}

bool XBackingStore_setBuffers(XBackingStore* self,
                              void* buffer1, void* buffer2,
                              size_t bufferSize)
{
    XPlatformBackingStore* platform;
    if (!self || !self->m_data) return false;
    platform = XBackingStore_handle(self);
    if (!platform) return false;
    if (!XPlatformBackingStore_setBuffers(platform, buffer1, buffer2,
                                          bufferSize))
        return false;
    return true;
}

size_t XBackingStore_requiredBufferSize(const XSize* size)
{
    return XPlatformBackingStore_requiredBufferSize(size);
}

XSize XBackingStore_size(const XBackingStore* self)
{
    XSize out;
    XSize_init(&out, 0, 0);
    if (!self || !self->m_data) return out;
    return self->m_data->m_size;
}

bool XBackingStore_scroll(XBackingStore* self, const XRegion* area,
                          int dx, int dy)
{
    XPlatformBackingStore* platform;
    float dpr;
    float nativeDx;
    float nativeDy;
    if (!self || !self->m_data) return false;
    platform = XBackingStore_handle(self);
    if (!platform) return false;
    dpr = xbs_backingStoreDpr(self);
    if (dpr == 1.0f)
        return XPlatformBackingStore_scroll(platform, area, dx, dy);
    /* 对标 Qt qbackingstore.cpp:254-266：步长×factor；非整数设备步长
       下既有像素不可复用，返回 false 触发整段重画。 */
    nativeDx = (float)dx * dpr;
    nativeDy = (float)dy * dpr;
    if (floorf(nativeDx) != nativeDx || floorf(nativeDy) != nativeDy)
        return false;
    {
        XRegion native;
        bool ok;
        XRegion_init(&native);
        if (area)
            xbs_mapRegionToDevice(area, dpr, &native);
        ok = XPlatformBackingStore_scroll(platform,
                                          area ? &native : NULL,
                                          (int)nativeDx, (int)nativeDy);
        XRegion_deinit(&native);
        return ok;
    }
}

void XBackingStore_beginPaint(XBackingStore* self, const XRegion* region)
{
    XPlatformBackingStore* platform;
    float dpr;
    /* region=NULL 合法：恢复父版本「NULL 按整窗」语义（2026-11-12 扫描
       P2#16 契约收窄回退）——平台 beginPaint 对 NULL/空域补整幅
       （XPlatformBackingStore.c：xpbs_clipRegion 空入 + isEmpty 补
       full），dpr=1 直传即整窗；DPR 腿见下方 NULL 分支。 */
    if (!self || !self->m_data) return;
    platform = XBackingStore_handle(self);
    if (!platform) return;
    dpr = xbs_backingStoreDpr(self);
    /* 对标 Qt qbackingstore.cpp:121-122：nativeSize 与 逻辑×factor 失配
       时先自动 resize（窗口 dpr 运行期变化后的第一次 beginPaint 自愈；
       首帧 m_size=0x0 与初始 nativeSize=0x0 恒相等，不触发）。 */
    {
        int expectW =
            xbs_roundToDevice((double)self->m_data->m_size.width * dpr);
        int expectH =
            xbs_roundToDevice((double)self->m_data->m_size.height * dpr);
        if (expectW != self->m_data->m_nativeSize.width ||
            expectH != self->m_data->m_nativeSize.height)
            XBackingStore_resize(self, &self->m_data->m_size);
    }
    if (dpr == 1.0f) {
        XPlatformBackingStore_beginPaint(platform, region);
        return;
    }
    /* 对标 Qt:125：绘制区域×factor 交平台（平台裁剪按物理缓冲域）。
       region=NULL 透传：平台补的整幅本就是物理缓冲全域（设备域整窗），
       与「逻辑整窗×dpr」映射等价，无需构造矩形。 */
    if (region)
    {
        XRegion native;
        XRegion_init(&native);
        xbs_mapRegionToDevice(region, dpr, &native);
        XPlatformBackingStore_beginPaint(platform, &native);
        XRegion_deinit(&native);
    }
    else
    {
        XPlatformBackingStore_beginPaint(platform, NULL);
    }
}

void XBackingStore_endPaint(XBackingStore* self)
{
    XPlatformBackingStore* platform = XBackingStore_handle(self);
    if (!platform) return;
    XPlatformBackingStore_endPaint(platform);
}

void XBackingStore_flush(XBackingStore* self, const XRegion* region,
                         XWindow* window, const XPoint* offset)
{
    XPlatformBackingStore* platform;
    float dpr;
    /* region=NULL 合法：恢复父版本「NULL 按整窗」语义（扫描 P2#16）——
       平台 flush 对 NULL/空域补整幅（XPlatformBackingStore.c:1097-1110），
       dpr=1 直传即整窗；DPR 腿见下方 NULL 分支。 */
    if (!self || !self->m_data) return;
    platform = XBackingStore_handle(self);
    if (!platform) return;
    /* Qt QBackingStore::flush 的 nullptr window 表示绑定的顶层窗口，而
     * 非空 window 才表示一次子窗口提交；平台层不能替公共层补这个默认。 */
    if (!window) window = self->m_data->m_window;
    dpr = xbs_backingStoreDpr(self);
    if (dpr == 1.0f) {
        XPlatformBackingStore_flush(platform, window, region, offset);
        return;
    }
    if (!region)
    {
        /* 整窗提交：offset 仍按 ×dpr 映射后透传（平台补整幅）。 */
        XPoint nativeOffset;
        XPoint_init(&nativeOffset, 0, 0);
        if (offset)
        {
            nativeOffset.x = xbs_roundToDevice((double)offset->x * dpr);
            nativeOffset.y = xbs_roundToDevice((double)offset->y * dpr);
        }
        XPlatformBackingStore_flush(platform, window, NULL, &nativeOffset);
        return;
    }
    /* 对标 Qt qbackingstore.cpp:211-224：区域×factor + 分数 dpr 下
       region/offset 各自取整的 off-by-one 修正——以 (bbox+offset) 的
       映射为准回灌差值（Qt 断言差值 ≤1，本实现钳位防御）。 */
    {
        XRegion native;
        XPoint nativeOffset;
        int diffX = 0;
        int diffY = 0;
        XPoint_init(&nativeOffset, 0, 0);
        if (offset && (offset->x != 0 || offset->y != 0))
        {
            XRect bbox;
            XRect nativeBbox;
            nativeOffset.x = xbs_roundToDevice((double)offset->x * dpr);
            nativeOffset.y = xbs_roundToDevice((double)offset->y * dpr);
            XRegion_boundingRect(region, &bbox);
            nativeBbox = xbs_mapRectToDevice(&bbox, dpr);
            /* (bbox+offset)×dpr 分项浮点：整型加法先行会在极端坐标
               先溢出（扫描 P2#17）。 */
            diffX = xbs_roundToDevice((double)bbox.x * dpr +
                                      (double)offset->x * dpr) -
                    (nativeBbox.x + nativeOffset.x);
            diffY = xbs_roundToDevice((double)bbox.y * dpr +
                                      (double)offset->y * dpr) -
                    (nativeBbox.y + nativeOffset.y);
            if (diffX > 1) diffX = 1;
            if (diffX < -1) diffX = -1;
            if (diffY > 1) diffY = 1;
            if (diffY < -1) diffY = -1;
        }
        XRegion_init(&native);
        {
            int i;
            for (i = 0; i < region->count; ++i)
            {
                XRect mapped = xbs_mapRectToDevice(&region->rects[i], dpr);
                mapped.x += diffX;
                mapped.y += diffY;
                XRegion_addRect(&native, &mapped);
            }
        }
        XPlatformBackingStore_flush(platform, window, &native,
                                    &nativeOffset);
        XRegion_deinit(&native);
    }
}

void XBackingStore_setStaticContents(XBackingStore* self, const XRegion* region)
{
    if (!self || !self->m_data) return;

    /* QBackingStore::setStaticContents 只把值存到公共私有状态（Qt
     * qbackingstore.cpp:271-281），不按当前 backing image 裁剪。XRegion_copy
     * 会先扩容，成功后直接写入，避免每次设置静态区域都释放并重建数组。 */
    if (region && region->count < 0)
        return;
    if (region && region->count > 0 && !region->rects)
        return;
    if (region)
        XRegion_copy(region, &self->m_data->m_staticContents);
    else
        XRegion_clear(&self->m_data->m_staticContents);

    /* 平台实现可以为 resize/合成建立裁剪后的提示，但这不应改变上方
     * 公共 API 返回的原始快照。NULL 与空区域都传给平台以清除旧提示。
     * 平台提示按目标缓冲域推送（DPR 模式×dpr；Qt setStaticContents
     * 只存快照、由 resize 时 scale 推送的语义在 resize 路径兑现，
     * 此处即时推送保持平台侧新鲜度）。 */
    if (self->m_data->m_platform)
    {
        float dpr = xbs_backingStoreDpr(self);
        if (dpr == 1.0f) {
            XPlatformBackingStore_setStaticContents(
                self->m_data->m_platform, &self->m_data->m_staticContents);
        }
        else
        {
            XRegion mapped;
            XRegion_init(&mapped);
            xbs_mapRegionToDevice(&self->m_data->m_staticContents, dpr,
                                  &mapped);
            XPlatformBackingStore_setStaticContents(
                self->m_data->m_platform, &mapped);
            XRegion_deinit(&mapped);
        }
    }
}

XRegion XBackingStore_staticContents(const XBackingStore* self)
{
    XRegion out;
    XRegion_init(&out);
    if (!self || !self->m_data) return out;
    XRegion_copy(&self->m_data->m_staticContents, &out);
    return out;
}

bool XBackingStore_hasStaticContents(const XBackingStore* self)
{
    if (!self || !self->m_data) return false;
    return !XRegion_isEmpty(&self->m_data->m_staticContents);
}

#endif /* XBACKINGSTORE_ON && XPLATFORMBACKINGSTORE_ON && XPLATFORMINTEGRATION_ON */

#endif /* XGUI_ON */
