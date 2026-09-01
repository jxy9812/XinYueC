/******************************************************************************
 * @file       XIconThemeEngine.c
 * @brief      XIconThemeEngine 主题图标引擎实现（对标 Qt 6.8 QIconLoaderEngine）。
 * @note       取图时通过 XIconInternal_resolveThemePixmapSize 按目标矩形
 *             解析主题文件，并按需保持宽高比缩放；SVG 支持由 XGui 图像
 *             编解码配置裁剪。
 ******************************************************************************/
#include "XIconThemeEngine.h"
#include "XIconThemeInternal.h"
#include "XIconScaledPixmapCache.h"
#include "XIconStyleHelper.h"
#include "XPainter.h"
#include "XPixmap.h"
#include "XImage.h"
#include "XMemory.h"
#include <limits.h>
#include <math.h>
#include <string.h>

static void themeEngine_pixmapForSize(const XIconThemeEngine* self,
                                      int targetSize, int iconScale,
                                      int outputWidth, int outputHeight,
                                      XPixmap* out)
{
    if (!self || !self->m_iconName || !out || targetSize <= 0 ||
        iconScale <= 0 || outputWidth <= 0 || outputHeight <= 0) return;
    XIconInternal_resolveThemePixmapSizeScaleRect(
        XString_toUtf8(self->m_iconName), targetSize, iconScale, outputWidth,
        outputHeight, out);
}

/* 对标 QIconPrivate::pixmapDevicePixelRatio()，根据实际像素数修正 DPR。 */
static float themeEngine_pixmapDevicePixelRatio(float displayRatio,
                                                int requestedWidth,
                                                int requestedHeight,
                                                int actualWidth,
                                                int actualHeight)
{
    double targetWidth;
    double targetHeight;
    double adjusted;
    if (!(displayRatio > 0.0f) || !isfinite(displayRatio) ||
        requestedWidth <= 0 || requestedHeight <= 0 || actualWidth <= 0 ||
        actualHeight <= 0)
        return 1.0f;
    targetWidth = floor((double)requestedWidth * displayRatio + 0.5);
    targetHeight = floor((double)requestedHeight * displayRatio + 0.5);
    if (targetWidth <= 0.0 || targetHeight <= 0.0) return 1.0f;
    if ((actualWidth == (int)targetWidth && actualHeight <= (int)targetHeight) ||
        (actualWidth <= (int)targetWidth && actualHeight == (int)targetHeight))
        return displayRatio;
    adjusted = displayRatio * 0.5 *
        ((double)actualWidth / targetWidth +
         (double)actualHeight / targetHeight);
    return adjusted > 1.0 ? (float)adjusted : 1.0f;
}

static void VXIconThemeEngine_scaledPixmap(const XIconThemeEngine* self,
                                           const XSize* size, XIconMode mode,
                                           XIconState state, float scale,
                                           XPixmap* out);

/**
 * @brief      取得主题引擎绘制目标的设备像素比。
 * @param target 绘制器；仅图像设备提供目标 DPR。
 * @return 有效的正设备像素比；未知或非法值按 1.0 返回。
 * @note       Qt QIconLoaderEngine::paint() 从 painter->device() 读取 DPR；
 *             Picture 设备和自定义设备在便携层没有可查询的 DPR，按 1.0 处理。
 */
static float themeEngine_devicePixelRatio(const XPainter* target)
{
    float ratio = 1.0f;
    if (target && target->m_deviceKind == XPainterDevice_Image &&
        target->m_image)
        ratio = XImage_devicePixelRatio(target->m_image);
    if (!(ratio > 0.0f) || !isfinite(ratio)) ratio = 1.0f;
    return ratio;
}

/**
 * @brief      将逻辑矩形按设备像素比换算为物理整数矩形。
 * @param logical 逻辑目标矩形。
 * @param ratio 设备像素比。
 * @param out 输出物理矩形。
 * @return 换算结果在 int 范围内且宽高为正时返回 true。
 */
static bool themeEngine_scaleRect(const XRect* logical, float ratio,
                                  XRect* out)
{
    double x;
    double y;
    double width;
    double height;
    if (!logical || !out || !(ratio > 0.0f) || !isfinite(ratio) ||
        logical->width <= 0 || logical->height <= 0)
        return false;
    x = (double)logical->x * (double)ratio;
    y = (double)logical->y * (double)ratio;
    width = (double)logical->width * (double)ratio;
    height = (double)logical->height * (double)ratio;
    if (!isfinite(x) || !isfinite(y) || !isfinite(width) ||
        !isfinite(height) || x > (double)INT_MAX || x < (double)INT_MIN ||
        y > (double)INT_MAX || y < (double)INT_MIN ||
        width > (double)INT_MAX || height > (double)INT_MAX)
        return false;
    /* QRect/QTransform 的整数栅格化采用最近整数；负位置向远离零方向取整。 */
    out->x = (int)(x >= 0.0 ? floor(x + 0.5) : ceil(x - 0.5));
    out->y = (int)(y >= 0.0 ? floor(y + 0.5) : ceil(y - 0.5));
    out->width = (int)floor(width + 0.5);
    out->height = (int)floor(height + 0.5);
    return out->width > 0 && out->height > 0;
}

static void VXIconThemeEngine_paint(const XIconThemeEngine* self,
                                    void* painter, const XRect* rect,
                                    XIconMode mode, XIconState state)
{
    XPainter* target = (XPainter*)painter;
    XPixmap pixmap;
    XPixmap scaled;
    const XPixmap* drawPixmap;
    XImage image;
    XRect sourceRect;
    XRect physicalRect;
    const XRect* drawRect = rect;
    float deviceRatio;
    int targetSize;
    bool saved = false;
    (void)state;
    if (!self || !target || !rect || !target->m_drawImage) return;
    deviceRatio = themeEngine_devicePixelRatio(target);
    if (deviceRatio != 1.0f) {
        if (!themeEngine_scaleRect(rect, deviceRatio, &physicalRect)) return;
        drawRect = &physicalRect;
    }
    XPixmap_init(&pixmap);
    XPixmap_init(&scaled);
    drawPixmap = &pixmap;
    /* Qt 6.8 qiconloader.cpp:783-788 先以 painter 设备 DPR 将
       rect.size() 换成物理像素，再把逻辑矩形交给 drawPixmap()；便携
       XPainter 的图像设备坐标直接对应物理像素，因此这里同时换算位置和
       尺寸，Picture/自定义设备仍保持 1.0 的原有逻辑坐标。 */
    targetSize = drawRect->width < drawRect->height
        ? drawRect->width : drawRect->height;
    themeEngine_pixmapForSize(self, targetSize, 1, drawRect->width,
                              drawRect->height, &pixmap);
    if (!XPixmap_isNull(&pixmap)) {
        XPixmap styled;
        XPixmap_init(&styled);
        XIconStyleHelper_apply(mode, &pixmap, &styled);
        if (!XPixmap_isNull(&styled))
        {
            XPixmap_deinit_base(&pixmap);
            XPixmap_move_base(&pixmap, &styled);
        }
        XPixmap_deinit_base(&styled);
        if (XPixmap_width(&pixmap) != drawRect->width ||
            XPixmap_height(&pixmap) != drawRect->height) {
            XPixmap_scaled(&pixmap, drawRect->width, drawRect->height,
                           0, 0, &scaled);
            if (!XPixmap_isNull(&scaled)) drawPixmap = &scaled;
        }
        XImage_init(&image);
        XPixmap_toImage(drawPixmap, &image);
        if (target->m_save) saved = target->m_save(target);
        sourceRect.x = 0;
        sourceRect.y = 0;
        sourceRect.width = XPixmap_width(drawPixmap);
        sourceRect.height = XPixmap_height(drawPixmap);
#if XPAINTER_IMAGE_RECT_ON
        if (XImage_width(&image) == drawRect->width &&
            XImage_height(&image) == drawRect->height)
            target->m_drawImage(target, &image, drawRect->x, drawRect->y);
        else
            XPainter_drawImageRect(target, drawRect, &image, &sourceRect);
#else
        target->m_drawImage(target, &image, drawRect->x, drawRect->y);
#endif
        if (saved && target->m_restore) target->m_restore(target);
        XImage_deinit_base(&image);
    }
    XPixmap_deinit_base(&scaled);
    XPixmap_deinit_base(&pixmap);
}

static void VXIconThemeEngine_actualSize(const XIconThemeEngine* self,
                                         const XSize* size, XIconMode mode,
                                         XIconState state, XSize* out)
{
    XPixmap pixmap;
    XVector available;
    int target;
    int declaredSize = 0;
    int declaredDistance = INT_MAX;
    bool preserveAspect = false;
    size_t i;
    (void)mode;
    (void)state;
    if (!out) return;
    out->width = 0;
    out->height = 0;
    if (!self || !size || size->width <= 0 || size->height <= 0) return;
    target = size->width < size->height ? size->width : size->height;
    if (self->m_iconName && XIconInternal_themeUsesScalableEntry(
            XString_toUtf8(self->m_iconName), target)) {
        /* Qt qiconloader.cpp:878-880 only returns the complete requested
           rectangle after entryForSize() selected a Scalable entry.  A
           theme may register both a fixed PNG and a scalable SVG; checking
           whether any scalable directory exists would incorrectly make the
           fixed entry return the rectangle as well. */
        *out = *size;
        return;
    }
    /* Qt QIconLoaderEngine::actualSize() 对 Fallback 条目委托给
       QIcon(entry->filename).actualSize()。独立回退文件可能是非方形图像，
       此时必须沿用 QPixmapIconEngine::adjustSize() 的宽高比，而不能像
       主题目录条目一样强制返回正方形。availableThemeSizes() 已复用同一
       主题搜索顺序；只有查询结果包含非方形尺寸时才进入该回退分支，避免
       主题固定/阈值条目的方形契约被实际文件内容改变。 */
    XVector_init(&available, sizeof(XSize), true);
    if (self->m_iconName && XIconInternal_availableThemeSizes(
            XString_toUtf8(self->m_iconName), &available)) {
        for (i = 0; i < XVector_size_base((const XContainer*)&available); ++i) {
            const XSize* availableSize = (const XSize*)XVector_at_base(
                &available, (int64_t)i);
            if (availableSize && availableSize->width > 0 &&
                availableSize->height > 0 &&
                availableSize->width != availableSize->height) {
                preserveAspect = true;
                break;
            }
            /* Qt QIconLoaderEngine::actualSize() uses the selected
               QIconDirInfo::size metadata, not the decoded file dimensions.
               Keep the closest declared square size so a malformed or
               undersized file cannot make a fixed theme entry report a
               smaller logical size than its index.theme contract. */
            if (!preserveAspect && availableSize &&
                availableSize->width == availableSize->height &&
                availableSize->width > 0) {
                int distance = availableSize->width > target
                    ? availableSize->width - target
                    : target - availableSize->width;
                if (distance < declaredDistance) {
                    declaredDistance = distance;
                    declaredSize = availableSize->width;
                }
                if (distance == 0)
                    break;
            }
        }
    }
    XVector_deinit_base((XClass*)&available);
    /* QIconLoaderEngine::entryForSize() matches the smaller edge of the
       requested rectangle; fixed/threshold entries never exceed it. */
    XPixmap_init(&pixmap);
    if (!XIconInternal_resolveThemePixmapSourceSize(
            XString_toUtf8(self->m_iconName), target, &pixmap) ||
        XPixmap_isNull(&pixmap)) {
        out->width = 0;
        out->height = 0;
    } else {
        int sourceWidth = XPixmap_width(&pixmap);
        int sourceHeight = XPixmap_height(&pixmap);
        if (preserveAspect) {
            double scale = 1.0;
            if (sourceWidth > size->width || sourceHeight > size->height) {
                scale = (double)size->width / sourceWidth;
                if ((double)size->height / sourceHeight < scale)
                    scale = (double)size->height / sourceHeight;
            }
            out->width = (int)(sourceWidth * scale + 0.5);
            out->height = (int)(sourceHeight * scale + 0.5);
        } else {
            int source = declaredSize > 0 ? declaredSize :
                (sourceWidth < sourceHeight ? sourceWidth : sourceHeight);
            if (source > target) source = target;
            out->width = source;
            out->height = source;
        }
    }
    XPixmap_deinit_base(&pixmap);
}

static void VXIconThemeEngine_pixmap(const XIconThemeEngine* self,
                                     const XSize* size, XIconMode mode,
                                     XIconState state, XPixmap* out)
{
    /* Qt 6.8 QIconLoaderEngine::pixmap() 直接委托 scaledPixmap(..., 1.0)，
       因而普通 DPI 也必须走 PixmapEntry 的缩放、样式和缓存键路径。 */
    VXIconThemeEngine_scaledPixmap(self, size, mode, state, 1.0f, out);
}

static void VXIconThemeEngine_addPixmap(XIconThemeEngine* self,
                                        const XPixmap* pixmap,
                                        XIconMode mode, XIconState state)
{
    (void)self;
    (void)pixmap;
    (void)mode;
    (void)state;
}

static void VXIconThemeEngine_addFile(XIconThemeEngine* self,
                                      const XString* fileName,
                                      const XSize* size, XIconMode mode,
                                      XIconState state)
{
    (void)self;
    (void)fileName;
    (void)size;
    (void)mode;
    (void)state;
}

static XString* VXIconThemeEngine_key(const XIconThemeEngine* self)
{
    (void)self;
    /* Qt 6.8 QIconLoaderEngine::key() 返回固定引擎类型名；图标名称由
       iconName() 单独提供，不能拼进 key，否则引擎类型无法稳定识别。 */
    return XString_create_utf8("QIconLoaderEngine");
}

static XIconEngine* VXIconThemeEngine_clone(const XIconThemeEngine* self)
{
    if (!self) return NULL;
    return (XIconEngine*)XIconThemeEngine_create_ex(
        XCLASS_DEFAULT_MEMORY_TYPE, self->m_iconName);
}

static bool VXIconThemeEngine_read(XIconThemeEngine* self, XIODevice* device)
{
    (void)self;
    (void)device;
    return false;
}

static bool VXIconThemeEngine_write(const XIconThemeEngine* self,
                                    XIODevice* device)
{
    (void)self;
    (void)device;
    return false;
}

static void VXIconThemeEngine_availableSizes(const XIconThemeEngine* self,
                                             XIconMode mode, XIconState state,
                                             XVector* out)
{
    (void)mode;
    (void)state;
    if (!out || !self || !self->m_iconName) return;
    XIconInternal_availableThemeSizes(XString_toUtf8(self->m_iconName), out);
}

static XString* VXIconThemeEngine_iconName(const XIconThemeEngine* self)
{
    XString* matched;
    if (!self || !self->m_iconName) return XString_create();
    /* Qt QIconLoaderEngine::iconName() 返回 QThemeIconInfo::iconName，
       即短横线回退后实际登记的名称，而非始终返回请求字符串。 */
    matched = XIconInternal_resolveThemeIconName(
        XString_toUtf8(self->m_iconName));
    if (matched) return matched;
    /* 保留项目既有未命中引擎的名称可见性，避免改变兼容 API。 */
    return XString_create_copy(self->m_iconName);
}

static bool VXIconThemeEngine_isNull(const XIconThemeEngine* self)
{
    if (!self || !self->m_iconName) return true;
    /* Qt QIconLoaderEngine::isNull() 只判断主题条目是否登记，不提前
       解码文件；损坏图标仍保持非空，直到 pixmap()/paint() 才失败。 */
    return !XIconInternal_themeHasIcon(XString_toUtf8(self->m_iconName));
}

static void VXIconThemeEngine_scaledPixmap(const XIconThemeEngine* self,
                                           const XSize* size, XIconMode mode,
                                           XIconState state, float scale,
                                           XPixmap* out)
{
    int target;
    int physicalWidth;
    int physicalHeight;
    float ratio = scale;
    double physicalWidthValue;
    double physicalHeightValue;
    double dprScaled;
    const char* iconName;
    int dprThousand;
    int iconScale;
    double scaleCeil;
    (void)state;
    if (!out) return;
    XPixmap_init(out);
    if (!size || size->width <= 0 || size->height <= 0 ||
        !(scale > 0.0f) || !isfinite(scale)) return;
    /* The theme entry is selected from min(width,height), then scaled in
       physical pixels; this keeps non-square requests within the target. */
    physicalWidthValue = (double)size->width * (double)ratio + 0.5;
    physicalHeightValue = (double)size->height * (double)ratio + 0.5;
    if (physicalWidthValue > (double)INT_MAX ||
        physicalHeightValue > (double)INT_MAX)
        return;
    physicalWidth = (int)physicalWidthValue;
    physicalHeight = (int)physicalHeightValue;
    target = (int)((size->width < size->height ? size->width : size->height) *
                   (double)ratio + 0.5);
    if (target <= 0) return;
    scaleCeil = ceil((double)ratio);
    if (scaleCeil > (double)INT_MAX) return;
    iconScale = (int)scaleCeil;
    if (iconScale <= 0) iconScale = 1;
    iconName = (self && self->m_iconName) ? XString_toUtf8(self->m_iconName) : NULL;
    dprScaled = (double)ratio * 1000.0 + 0.5;
    dprThousand = dprScaled > (double)INT_MAX
        ? INT_MAX : (int)dprScaled;
    themeEngine_pixmapForSize(self,
                              size->width < size->height ? size->width :
                              size->height,
                              iconScale, physicalWidth, physicalHeight, out);
    if (!XPixmap_isNull(out)) {
        XPixmap styled;
        XPixmap_init(&styled);
        XIconStyleHelper_apply(mode, out, &styled);
        if (!XPixmap_isNull(&styled))
        {
            XPixmap_deinit_base(out);
            XPixmap_move_base(out, &styled);
        }
        XPixmap_deinit_base(&styled);
        {
            float calculated = themeEngine_pixmapDevicePixelRatio(
                ratio, size->width, size->height, XPixmap_width(out),
                XPixmap_height(out));
            XPixmap_setDevicePixelRatio(out, calculated);
            /* QIconLoaderEngine::PixmapEntry::pixmap() 以 adjustSize() 后的
               实际像素尺寸及计算 DPR 组成缓存键。主题资源可能小于请求尺寸，
               此时计算 DPR 会低于请求比例，不能提前用请求键查找。 */
            dprScaled = (double)calculated * 1000.0 + 0.5;
            dprThousand = dprScaled > (double)INT_MAX
                ? INT_MAX : (int)dprScaled;
            if (iconName && XIconScaledPixmapCache_find(
                    "qt_icon_theme/", iconName,
                    XIconStyleHelper_paletteCacheKey(), mode,
                    XPixmap_width(out), XPixmap_height(out), dprThousand,
                    out)) {
                return;
            }
        }
        if (iconName)
            XIconScaledPixmapCache_insert(
                "qt_icon_theme/", iconName,
                XIconStyleHelper_paletteCacheKey(), mode,
                XPixmap_width(out), XPixmap_height(out), dprThousand, out);
    }
}

static void VXIconThemeEngine_virtualHook(const XIconThemeEngine* self,
                                          int id, void* data)
{
    (void)self;
    (void)id;
    (void)data;
}

static void VXIconThemeEngine_deinit(XIconThemeEngine* self)
{
    if (!self) return;
    if (self->m_iconName) {
        XString_delete_base((XClass*)self->m_iconName);
        self->m_iconName = NULL;
    }
    XClass_Deinit_Parent(XIconEngine, (XIconEngine*)self);
}

static void VXIconThemeEngine_copy(XIconThemeEngine* self,
                                   const XIconThemeEngine* other)
{
    if (!self || !other || self == other) return;
    if (XClassIsVtableNull(self)) XIconThemeEngine_init(self, NULL);
    XClass_Parent(XIconEngine, EXClass_Copy,
                  void(*)(XIconEngine*, const XIconEngine*))(
        (XIconEngine*)self, (const XIconEngine*)other);
    if (self->m_iconName) {
        XString_delete_base((XClass*)self->m_iconName);
        self->m_iconName = NULL;
    }
    self->m_iconName = other->m_iconName
        ? XString_create_copy(other->m_iconName) : NULL;
}

static void VXIconThemeEngine_move(XIconThemeEngine* self,
                                   XIconThemeEngine* other)
{
    if (!self || !other || self == other) return;
    if (XClassIsVtableNull(self)) XIconThemeEngine_init(self, NULL);
    XClass_Parent(XIconEngine, EXClass_Move,
                  void(*)(XIconEngine*, XIconEngine*))(
        (XIconEngine*)self, (XIconEngine*)other);
    if (self->m_iconName) {
        XString_delete_base((XClass*)self->m_iconName);
        self->m_iconName = NULL;
    }
    self->m_iconName = other->m_iconName;
    other->m_iconName = NULL;
}

XVtable* XIconThemeEngine_class_init(void)
{
    XVTABLE_INIT_DEFAULT(XIconThemeEngine)
    XVTABLE_INHERIT_XCLASS(XIconEngine);
    XVTABLE_OVERLOAD_DEFAULT(EXIconEngine_Paint, VXIconThemeEngine_paint);
    XVTABLE_OVERLOAD_DEFAULT(EXIconEngine_ActualSize, VXIconThemeEngine_actualSize);
    XVTABLE_OVERLOAD_DEFAULT(EXIconEngine_Pixmap, VXIconThemeEngine_pixmap);
    XVTABLE_OVERLOAD_DEFAULT(EXIconEngine_AddPixmap, VXIconThemeEngine_addPixmap);
    XVTABLE_OVERLOAD_DEFAULT(EXIconEngine_AddFile, VXIconThemeEngine_addFile);
    XVTABLE_OVERLOAD_DEFAULT(EXIconEngine_Key, VXIconThemeEngine_key);
    XVTABLE_OVERLOAD_DEFAULT(EXIconEngine_Clone, VXIconThemeEngine_clone);
    XVTABLE_OVERLOAD_DEFAULT(EXIconEngine_Read, VXIconThemeEngine_read);
    XVTABLE_OVERLOAD_DEFAULT(EXIconEngine_Write, VXIconThemeEngine_write);
    XVTABLE_OVERLOAD_DEFAULT(EXIconEngine_AvailableSizes, VXIconThemeEngine_availableSizes);
    XVTABLE_OVERLOAD_DEFAULT(EXIconEngine_IconName, VXIconThemeEngine_iconName);
    XVTABLE_OVERLOAD_DEFAULT(EXIconEngine_IsNull, VXIconThemeEngine_isNull);
    XVTABLE_OVERLOAD_DEFAULT(EXIconEngine_ScaledPixmap, VXIconThemeEngine_scaledPixmap);
    XVTABLE_OVERLOAD_DEFAULT(EXIconEngine_VirtualHook, VXIconThemeEngine_virtualHook);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Copy, VXIconThemeEngine_copy);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Move, VXIconThemeEngine_move);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Deinit, VXIconThemeEngine_deinit);
    return XVTABLE_DEFAULT;
}

XIconThemeEngine* XIconThemeEngine_create_ex(XMemoryType memory,
                                             const XString* iconName)
{
    XIconThemeEngine* self = (XIconThemeEngine*)XMemory_malloc(
        sizeof(XIconThemeEngine), memory);
    if (!self) return NULL;
    memset(self, 0, sizeof(XIconThemeEngine));
    XIconThemeEngine_init(self, iconName);
    Set_Class_Memory(self, memory);
    Set_Class_IsHeap(self, true);
    return self;
}

XIconThemeEngine* XIconThemeEngine_create_2_ex(XMemoryType memory,
                                               const char* iconName)
{
    XIconThemeEngine* result;
    XString* name = iconName ? XString_create_utf8(iconName) : NULL;
    result = XIconThemeEngine_create_ex(memory, name);
    if (name) XString_delete_base((XClass*)name);
    return result;
}

void XIconThemeEngine_init(XIconThemeEngine* self, const XString* iconName)
{
    if (!self) return;
    memset(self, 0, sizeof(XIconThemeEngine));
    XIconEngine_init(&self->m_base);
    XClassSetVtable(self, XIconThemeEngine);
    self->m_iconName = iconName ? XString_create_copy(iconName) : NULL;
}

void XIconThemeEngine_init_2(XIconThemeEngine* self, const char* iconName)
{
    XString* name = iconName ? XString_create_utf8(iconName) : NULL;
    XIconThemeEngine_init(self, name);
    if (name) XString_delete_base((XClass*)name);
}
