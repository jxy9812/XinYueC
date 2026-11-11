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
#include "XFontFt.h"
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
 * paintDevice 挂 devicePixelRatio 元数据（Qt:133-148 的共享镜像等价——
 * 平台层只读图像宽高物理值，不读 dpr，直写平台图像即可）。dpr==1 时全部
 * 直通，旧链路逐位零回归。逻辑→设备矩形用**边坐标取整**（四边各自
 * round 后求尺寸），相邻矩形映射后仍无缝隙/无重叠。 */

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
    return dpr;
}

/** @brief 逻辑矩形→设备矩形（四边各自 round，保相邻无缝）。 */
static XRect xbs_mapRectToDevice(const XRect* rect, float dpr)
{
    XRect out;
    int left;
    int top;
    int right;
    int bottom;
    left = (int)floorf((float)rect->x * dpr + 0.5f);
    top = (int)floorf((float)rect->y * dpr + 0.5f);
    right = (int)floorf((float)(rect->x + rect->width) * dpr + 0.5f);
    bottom = (int)floorf((float)(rect->y + rect->height) * dpr + 0.5f);
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
    if (!platform) return NULL;
    image = XPlatformBackingStore_paintDevice(platform);
    if (!image) return NULL;
    /* 对标 Qt qbackingstore.cpp:133-148：DPR 模式下给绘制设备挂
       devicePixelRatio，QPainter 侧据此装配 scale(dpr) 基础变换
       （qpainter.cpp updateMatrix 末端 hidpiScaleTransform 同源）。
       Qt 因 QImage 隐式共享另建共享数据镜像防 dpr 回灌平台插件；
       本实现各平台后端只读图像宽高（物理值，两态一致），直挂元数据
       即等价。旧链（dpr=1）显式复位 1.0，防跨屏拖窗/回退闸切换残留。 */
    XImage_setDevicePixelRatio(image, xbs_backingStoreDpr(self));
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
            out.x = (int)floorf((float)out.x / dpr + 0.5f);
            out.y = (int)floorf((float)out.y / dpr + 0.5f);
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
    native.width = (int)floorf((float)size->width * dpr + 0.5f);
    native.height = (int)floorf((float)size->height * dpr + 0.5f);
    if (native.width < 0) native.width = 0;
    if (native.height < 0) native.height = 0;
    self->m_data->m_nativeSize = native;
    /* [DPR 观感根修 2026-11-09 第二轮] dpr>1 全字号免 FT hint（对齐
     * Skia/Qt hidpi 无 hint 亚像素渲染；LIGHT 的竖向 grid-fit 在
     * outline 域整数化竖笔——同屏对照系统文字实测笔画撑粗 3~4px vs
     * 2px）。接管发生在首字形光栅前（首次 resize 先于首次 paint，
     * alpha/路径缓存键不含 hint 位的约束因此满足）；env
     * XFONT_FT_HINT_MIN_PX 显式设置优先于本接管（bisect 保留）。
     * 仅 dpr>1 设定——dpr=1 桌面 hint 分档逐位不动（恒等红线）。
     * [2026-11-09 三轮回退] 安卓 1.5 密度实测该接管伴随文字大面积缺失
     * （真包 MuMu 复现，与 BlueStacks 用户截图一致）——单变量回退摘除
     * 定位；setter 本体保留，待带日志的 A/B 重验。 */
    (void)dpr;
    /* if (dpr > 1.0f)
        XFontFt_setHintMinPxOverride(XFONT_FT_HINT_MIN_PX_NEVER); */
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
    if (!self || !self->m_data || !region) return;
    platform = XBackingStore_handle(self);
    if (!platform) return;
    dpr = xbs_backingStoreDpr(self);
    /* 对标 Qt qbackingstore.cpp:121-122：nativeSize 与 逻辑×factor 失配
       时先自动 resize（窗口 dpr 运行期变化后的第一次 beginPaint 自愈；
       首帧 m_size=0x0 与初始 nativeSize=0x0 恒相等，不触发）。 */
    {
        int expectW = (int)floorf((float)self->m_data->m_size.width * dpr + 0.5f);
        int expectH = (int)floorf((float)self->m_data->m_size.height * dpr + 0.5f);
        if (expectW != self->m_data->m_nativeSize.width ||
            expectH != self->m_data->m_nativeSize.height)
            XBackingStore_resize(self, &self->m_data->m_size);
    }
    if (dpr == 1.0f) {
        XPlatformBackingStore_beginPaint(platform, region);
        return;
    }
    /* 对标 Qt:125：绘制区域×factor 交平台（平台裁剪按物理缓冲域）。 */
    {
        XRegion native;
        XRegion_init(&native);
        xbs_mapRegionToDevice(region, dpr, &native);
        XPlatformBackingStore_beginPaint(platform, &native);
        XRegion_deinit(&native);
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
    if (!self || !self->m_data || !region) return;
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
            nativeOffset.x =
                (int)floorf((float)offset->x * dpr + 0.5f);
            nativeOffset.y =
                (int)floorf((float)offset->y * dpr + 0.5f);
            XRegion_boundingRect(region, &bbox);
            nativeBbox = xbs_mapRectToDevice(&bbox, dpr);
            diffX = (int)floorf((float)(bbox.x + offset->x) * dpr + 0.5f) -
                    (nativeBbox.x + nativeOffset.x);
            diffY = (int)floorf((float)(bbox.y + offset->y) * dpr + 0.5f) -
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
