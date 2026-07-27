/******************************************************************************
 * @file       XIcon.c
 * @brief      XIcon 图标类实现（对标 Qt 6.8 QIcon）
 * @author     XinYueC 团队
 ******************************************************************************/
#include "XIcon.h"
#include "XAtomic.h"
#include "XClass.h"
#include "XVtable.h"
#include "XMemory.h"
#include "XVector.h"
#include <limits.h>
#include <string.h>
#include <stdlib.h>

/* ========== 图标条目数据结构 ========== */

/**
 * @brief      图标条目，存储像素图及其模式/状态
 */
typedef struct XIconEntry
{
    XPixmap     m_pixmap;   /**< 像素图数据 */
    XIconMode   m_mode;     /**< 图标模式 */
    XIconState  m_state;    /**< 图标状态 */
}XIconEntry;

/**
 * @brief      XIcon 私有数据结构
 */
typedef struct XIconPrivate
{
    XAtomic_int32_t  m_refCount;    /**< 引用计数 */
    XIconEntry*      m_entries;     /**< 图标条目数组 */
    int              m_entryCount;  /**< 条目数量 */
    int              m_entryCapacity; /**< 条目容量 */
    bool             m_isMask;      /**< 是否为掩码 */
    int64_t          m_cacheKey;    /**< 缓存键值 */
    int              m_serialNumber;/**< 序列号 */
}XIconPrivate;

static int g_iconSerialCounter = 1;

static void XIconPrivate_touch(XIconPrivate* d)
{
    if (d)
        d->m_cacheKey = (int64_t)(uint32_t)g_iconSerialCounter++ << 32;
}

static XIconPrivate* XIconPrivate_create()
{
    XIconPrivate* d = (XIconPrivate*)XMalloc_System(sizeof(XIconPrivate));
    if (!d) return NULL;
    memset(d, 0, sizeof(XIconPrivate));
    XAtomic_init(d->m_refCount, 1);
    d->m_serialNumber = g_iconSerialCounter;
    XIconPrivate_touch(d);
    return d;
}

static void XIconPrivate_ref(XIconPrivate* d) { if (d) XAtomic_fetch_add_int32(&d->m_refCount, 1, XAtomic_MemoryOrder_SeqCst); }

static void XIconPrivate_unref(XIconPrivate* d)
{
    if (!d) return;
    if (XAtomic_fetch_add_int32(&d->m_refCount, -1, XAtomic_MemoryOrder_SeqCst) == 1)
    {
        for (int i = 0; i < d->m_entryCount; i++)
            XPixmap_deinit_base(&d->m_entries[i].m_pixmap);
        if (d->m_entries) XFree_System(d->m_entries);
        XFree_System(d);
    }
}

static void XIconPrivate_addEntry(XIconPrivate* d, const XPixmap* pixmap, XIconMode mode, XIconState state)
{
    if (!d || !pixmap || XPixmap_isNull(pixmap)) return;
    if (d->m_entryCount >= d->m_entryCapacity)
    {
        int newCap = d->m_entryCapacity ? d->m_entryCapacity * 2 : 8;
        XIconEntry* newEntries = (XIconEntry*)XMalloc_System(newCap * sizeof(XIconEntry));
        if (!newEntries) return;
        if (d->m_entries)
        {
            memcpy(newEntries, d->m_entries, d->m_entryCount * sizeof(XIconEntry));
            XFree_System(d->m_entries);
        }
        d->m_entries = newEntries;
        d->m_entryCapacity = newCap;
    }
    XPixmap_init(&d->m_entries[d->m_entryCount].m_pixmap);
    XPixmap_copy_base(&d->m_entries[d->m_entryCount].m_pixmap, pixmap);
    d->m_entries[d->m_entryCount].m_mode = mode;
    d->m_entries[d->m_entryCount].m_state = state;
    d->m_entryCount++;
    XIconPrivate_touch(d);
}

/* ========== 虚函数实现 ========== */

static void VXIcon_copy(XIcon* dest, const XIcon* src)
{
    if (ISNULL(dest, "XIcon") || ISNULL(src, "XIcon")) return;
    if (dest == src) return;
    if (XClassIsVtableNull(dest)) XIcon_init(dest);
    if (dest->m_data)
        XIconPrivate_unref(dest->m_data);
    dest->m_data = src->m_data;
    XIconPrivate_ref(dest->m_data);
}

static void VXIcon_move(XIcon* dest, XIcon* src)
{
    if (ISNULL(dest, "XIcon") || ISNULL(src, "XIcon")) return;
    if (dest == src) return;
    if (XClassIsVtableNull(dest)) XIcon_init(dest);
    if (dest->m_data)
        XIconPrivate_unref(dest->m_data);
    dest->m_data = src->m_data;
    src->m_data = NULL;
}

static void VXIcon_deinit(XIcon* self)
{
    if (ISNULL(self, "XIcon")) return;
    if (self->m_data)
    {
        XIconPrivate_unref(self->m_data);
        self->m_data = NULL;
    }
}

/* ========== 虚函数表初始化 ========== */

XVtable* XIcon_class_init()
{
    XVTABLE_CREAT_DEFAULT;
    XVTABLE_STACK_INIT_DEFAULT(XCLASS_VTABLE_GET_SIZE(XIcon));
    XVTABLE_INHERIT_XCLASS(XClass);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Copy, VXIcon_copy);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Move, VXIcon_move);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Deinit, VXIcon_deinit);
    return XVTABLE_DEFAULT;
}

XIcon* XIcon_create()
{
    XIcon* self = (XIcon*)XMalloc_System(sizeof(XIcon));
    if (!self) return NULL;
    XIcon_init(self);
    Set_Class_MemoryFree(self, XFree_System);
    return self;
}

void XIcon_init(XIcon* self)
{
    if (ISNULL(self, "XIcon")) return;
    memset(self, 0, sizeof(XIcon));
    XClass_init((XClass*)self);
    XClassSetVtable(self, XIcon);
    self->m_data = XIconPrivate_create();
}

void XIcon_init_pixmap(XIcon* self, const XPixmap* pixmap)
{
    XIcon_init(self);
    XIconPrivate_addEntry(self->m_data, pixmap, XIconMode_Normal, XIconState_Off);
}

void XIcon_init_file(XIcon* self, const char* fileName)
{
    XIcon_init(self);
    XPixmap pixmap;
    XPixmap_init_file(&pixmap, fileName, NULL, 0);
    if (!XPixmap_isNull(&pixmap))
        XIconPrivate_addEntry(self->m_data, &pixmap, XIconMode_Normal, XIconState_Off);
    XPixmap_deinit_base(&pixmap);
}

void XIcon_init_engine(XIcon* self, void* engine) { (void)engine; XIcon_init(self); }
void XIcon_copy(XIcon* self, const XIcon* other)
{
    if (ISNULL(self, "XIcon") || ISNULL(other, "XIcon")) return;
    if (self == other) return;
    if (!XClassIsVtableNull(self))
        XIcon_deinit_base(self);
    XIcon_init(self);
    XIcon_copy_base(self, other);
}
void XIcon_move(XIcon* self, XIcon* other)
{
    if (ISNULL(self, "XIcon") || ISNULL(other, "XIcon")) return;
    if (self == other) return;
    if (!XClassIsVtableNull(self))
        XIcon_deinit_base(self);
    XIcon_init(self);
    XIcon_move_base(self, other);
}
void XIcon_deinit(XIcon* self) { XIcon_deinit_base(self); }
void XIcon_copy_base(XIcon* dest, const XIcon* src)
{
    if (ISNULL(dest, "XIcon") || ISNULL(src, "XIcon")) return;
    if (ISNULL(XClassGetVtable(src), "Vtable")) return;
    XClassGetVirtualFunc(src, EXClass_Copy, void(*)(XIcon*, const XIcon*))(dest, src);
}
void XIcon_move_base(XIcon* dest, XIcon* src)
{
    if (ISNULL(dest, "XIcon") || ISNULL(src, "XIcon")) return;
    if (ISNULL(XClassGetVtable(src), "Vtable")) return;
    XClassGetVirtualFunc(src, EXClass_Move, void(*)(XIcon*, XIcon*))(dest, src);
}
void XIcon_deinit_base(XIcon* self)
{
    if (ISNULL(self, "XIcon")) return;
    if (ISNULL(XClassGetVtable(self), "Vtable")) return;
    XClassGetVirtualFunc(self, EXClass_Deinit, void(*)(XIcon*))(self);
}



void XIcon_delete_base(XIcon* self)
{
    XClass_delete_base((XClass*)self);
}
bool XIcon_isNull(const XIcon* self) { return !self || !self->m_data || self->m_data->m_entryCount == 0; }
bool XIcon_isDetached(const XIcon* self) { return !self || !self->m_data || XAtomic_load_int32(&self->m_data->m_refCount, XAtomic_MemoryOrder_Relaxed) == 1; }

void XIcon_detach(XIcon* self)
{
    if (!self || !self->m_data) return;
    if (XAtomic_load_int32(&self->m_data->m_refCount, XAtomic_MemoryOrder_Relaxed) > 1)
    {
        XIconPrivate* newData = XIconPrivate_create();
        if (newData)
        {
            for (int i = 0; i < self->m_data->m_entryCount; i++)
                XIconPrivate_addEntry(newData, &self->m_data->m_entries[i].m_pixmap,
                                      self->m_data->m_entries[i].m_mode,
                                      self->m_data->m_entries[i].m_state);
            newData->m_isMask = self->m_data->m_isMask;
            XIconPrivate_unref(self->m_data);
            self->m_data = newData;
        }
    }
}

int64_t XIcon_cacheKey(const XIcon* self)
{
    return (self && self->m_data && self->m_data->m_entryCount > 0) ? self->m_data->m_cacheKey : 0;
}

static int64_t XIconPrivate_entryArea(const XIconEntry* entry)
{
    int64_t width;
    int64_t height;
    if (!entry) return 0;
    width = XPixmap_width(&entry->m_pixmap);
    height = XPixmap_height(&entry->m_pixmap);
    if (width <= 0 || height <= 0) return 0;
    return width * height;
}

/* Match the QPixmapIconEngine policy: within one mode/state, prefer the
 * smallest resource that is at least as large as requested. If all resources
 * are smaller, use the largest one so callers never upscale unnecessarily. */
static const XIconEntry* XIconPrivate_tryMatch(const XIconPrivate* d, int width, int height,
                                               XIconMode mode, XIconState state)
{
    const XIconEntry* best = NULL;
    const int64_t requestedArea = (width > 0 && height > 0)
        ? (int64_t)width * height : 0;
    for (int i = 0; d && i < d->m_entryCount; ++i)
    {
        const XIconEntry* candidate = &d->m_entries[i];
        const int64_t candidateArea = XIconPrivate_entryArea(candidate);
        if (candidate->m_mode != mode || candidate->m_state != state || candidateArea <= 0)
            continue;
        if (!best)
        {
            best = candidate;
            continue;
        }

        const int64_t bestArea = XIconPrivate_entryArea(best);
        const bool candidateIsLargeEnough = candidateArea >= requestedArea;
        const bool bestIsLargeEnough = bestArea >= requestedArea;
        if (candidateIsLargeEnough != bestIsLargeEnough)
        {
            if (candidateIsLargeEnough) best = candidate;
        }
        else if (candidateIsLargeEnough)
        {
            if (candidateArea < bestArea) best = candidate;
        }
        else if (candidateArea > bestArea)
        {
            best = candidate;
        }
    }
    return best;
}

static const XIconEntry* XIconPrivate_bestEntry(const XIconPrivate* d, int width, int height,
                                                XIconMode mode, XIconState state)
{
    const XIconState oppositeState = (state == XIconState_On)
        ? XIconState_Off : XIconState_On;
    const XIconEntry* best = XIconPrivate_tryMatch(d, width, height, mode, state);
    if (best) return best;

    /* This is the fallback order used by QPixmapIconEngine::bestMatch().
     * Disabled/Selected prefer Normal and Active; Normal/Active prefer each
     * other, then fall back to Disabled/Selected. */
    if (mode == XIconMode_Disabled || mode == XIconMode_Selected)
    {
        const XIconMode oppositeMode = (mode == XIconMode_Disabled)
            ? XIconMode_Selected : XIconMode_Disabled;
        best = XIconPrivate_tryMatch(d, width, height, XIconMode_Normal, state);
        if (!best) best = XIconPrivate_tryMatch(d, width, height, XIconMode_Active, state);
        if (!best) best = XIconPrivate_tryMatch(d, width, height, mode, oppositeState);
        if (!best) best = XIconPrivate_tryMatch(d, width, height, XIconMode_Normal, oppositeState);
        if (!best) best = XIconPrivate_tryMatch(d, width, height, XIconMode_Active, oppositeState);
        if (!best) best = XIconPrivate_tryMatch(d, width, height, oppositeMode, state);
        if (!best) best = XIconPrivate_tryMatch(d, width, height, oppositeMode, oppositeState);
    }
    else
    {
        const XIconMode oppositeMode = (mode == XIconMode_Normal)
            ? XIconMode_Active : XIconMode_Normal;
        best = XIconPrivate_tryMatch(d, width, height, oppositeMode, state);
        if (!best) best = XIconPrivate_tryMatch(d, width, height, mode, oppositeState);
        if (!best) best = XIconPrivate_tryMatch(d, width, height, oppositeMode, oppositeState);
        if (!best) best = XIconPrivate_tryMatch(d, width, height, XIconMode_Disabled, state);
        if (!best) best = XIconPrivate_tryMatch(d, width, height, XIconMode_Selected, state);
        if (!best) best = XIconPrivate_tryMatch(d, width, height, XIconMode_Disabled, oppositeState);
        if (!best) best = XIconPrivate_tryMatch(d, width, height, XIconMode_Selected, oppositeState);
    }
    return best;
}

void XIcon_pixmap(const XIcon* self, int width, int height, XIconMode mode, XIconState state, XPixmap* out)
{
    if (!out) return;
    if (!XClassIsVtableNull(out)) XPixmap_deinit_base(out);
    XPixmap_init(out);
    if (!self || !self->m_data || width <= 0 || height <= 0) return;
    const XIconEntry* best = XIconPrivate_bestEntry(self->m_data, width, height, mode, state);
    if (best)
    {
        XSize actual;
        XIcon_actualSize(self, width, height, mode, state, &actual);
        if (XPixmap_width(&best->m_pixmap) == actual.width && XPixmap_height(&best->m_pixmap) == actual.height)
            XPixmap_copy_base(out, &best->m_pixmap);
        else
            XPixmap_scaled(&best->m_pixmap, actual.width, actual.height, 0, 0, out);
    }
}

void XIcon_pixmapExtent(const XIcon* self, int extent, XIconMode mode, XIconState state, XPixmap* out)
{
    XIcon_pixmap(self, extent, extent, mode, state, out);
}

void XIcon_pixmapRatio(const XIcon* self, int width, int height, float devicePixelRatio,
                       XIconMode mode, XIconState state, XPixmap* out)
{
    double scaledWidth;
    double scaledHeight;
    int targetWidth;
    int targetHeight;
    int actualWidth;
    int actualHeight;
    float outputRatio = 1.0f;
    if (!out) return;
    if (devicePixelRatio <= 0.0f) devicePixelRatio = 1.0f;
    scaledWidth = (double)width * devicePixelRatio + 0.5;
    scaledHeight = (double)height * devicePixelRatio + 0.5;
    targetWidth = (scaledWidth <= 0.0) ? 0 :
        ((scaledWidth > INT_MAX) ? INT_MAX : (int)scaledWidth);
    targetHeight = (scaledHeight <= 0.0) ? 0 :
        ((scaledHeight > INT_MAX) ? INT_MAX : (int)scaledHeight);
    XIcon_pixmap(self, targetWidth, targetHeight, mode, state, out);
    if (XPixmap_isNull(out)) return;

    /* QIconPrivate::pixmapDevicePixelRatio() only preserves the requested
     * ratio when the returned pixels cover the requested device size. For a
     * lower-resolution fallback, reduce the ratio so its logical size does
     * not become smaller than requested. */
    actualWidth = XPixmap_width(out);
    actualHeight = XPixmap_height(out);
    if (devicePixelRatio > 1.0f && targetWidth > 0 && targetHeight > 0)
    {
        if ((actualWidth == targetWidth && actualHeight <= targetHeight) ||
            (actualWidth <= targetWidth && actualHeight == targetHeight))
        {
            outputRatio = devicePixelRatio;
        }
        else
        {
            double scale = 0.5 * ((double)actualWidth / targetWidth +
                                  (double)actualHeight / targetHeight);
            double adjusted = devicePixelRatio * scale;
            outputRatio = (adjusted > 1.0) ? (float)adjusted : 1.0f;
        }
    }
    XPixmap_setDevicePixelRatio(out, outputRatio);
}

void XIcon_actualSize(const XIcon* self, int width, int height, XIconMode mode, XIconState state, XSize* out)
{
    if (!out) return;
    out->width = 0;
    out->height = 0;
    if (!self || !self->m_data || width <= 0 || height <= 0) return;
    const XIconEntry* best = XIconPrivate_bestEntry(self->m_data, width, height, mode, state);
    if (!best) return;
    int sourceWidth = XPixmap_width(&best->m_pixmap);
    int sourceHeight = XPixmap_height(&best->m_pixmap);
    if (sourceWidth <= 0 || sourceHeight <= 0) return;
    if (sourceWidth <= width && sourceHeight <= height)
    {
        out->width = sourceWidth;
        out->height = sourceHeight;
        return;
    }
    float scale = (float)width / sourceWidth;
    float heightScale = (float)height / sourceHeight;
    if (heightScale < scale) scale = heightScale;
    out->width = (int)(sourceWidth * scale + 0.5f);
    out->height = (int)(sourceHeight * scale + 0.5f);
}

void XIcon_paint(const XIcon* self, void* painter, int x, int y, int w, int h,
                 uint32_t alignment, XIconMode mode, XIconState state)
{
    (void)self; (void)painter; (void)x; (void)y; (void)w; (void)h; (void)alignment; (void)mode; (void)state;
}

void XIcon_addPixmap(XIcon* self, const XPixmap* pixmap, XIconMode mode, XIconState state)
{
    if (!self || !self->m_data || !pixmap || XPixmap_isNull(pixmap)) return;
    XIcon_detach(self);
    XIconPrivate_addEntry(self->m_data, pixmap, mode, state);
}

void XIcon_addFile(XIcon* self, const char* fileName, int width, int height,
                   XIconMode mode, XIconState state)
{
    if (!self || !self->m_data || !fileName) return;
    XPixmap pixmap;
    XPixmap_init(&pixmap);
    if (!XPixmap_load(&pixmap, fileName, NULL, 0))
    {
        XPixmap_deinit_base(&pixmap);
        return;
    }

    /* QIcon::addFile stores the requested QSize as the resource size.  Load
     * first so the decoder's native size is not discarded by XPixmap_load,
     * then make the requested raster when both dimensions are specified. */
    if (width > 0 && height > 0 &&
        (XPixmap_width(&pixmap) != width || XPixmap_height(&pixmap) != height))
    {
        XPixmap scaled;
        XPixmap_init(&scaled);
        XPixmap_scaled(&pixmap, width, height, 0, 0, &scaled);
        if (!XPixmap_isNull(&scaled))
        {
            XPixmap_deinit_base(&pixmap);
            pixmap.m_data = scaled.m_data;
            scaled.m_data = NULL;
        }
        XPixmap_deinit_base(&scaled);
    }
    if (!XPixmap_isNull(&pixmap))
        XIcon_addPixmap(self, &pixmap, mode, state);
    XPixmap_deinit_base(&pixmap);
}

void XIcon_availableSizes(const XIcon* self, XIconMode mode, XIconState state, void* out)
{
    XVector* sizes = (XVector*)out;
    if (!sizes) return;
    XVector_clear_base(sizes);
    if (!self || !self->m_data) return;

    for (int i = 0; i < self->m_data->m_entryCount; ++i) {
        const XIconEntry* entry = &self->m_data->m_entries[i];
        if (entry->m_mode != mode || entry->m_state != state) continue;

        XSize size;
        size.width = XPixmap_width(&entry->m_pixmap);
        size.height = XPixmap_height(&entry->m_pixmap);
        bool duplicate = false;
        for (size_t j = 0; j < XVector_size_base(sizes); ++j) {
            const XSize* existing = (const XSize*)XVector_at_base(sizes, (int64_t)j);
            if (existing && existing->width == size.width && existing->height == size.height) {
                duplicate = true;
                break;
            }
        }
        if (!duplicate) XVector_push_back_1_base(sizes, &size);
    }
}

void XIcon_setIsMask(XIcon* self, bool isMask)
{
    if (!self || !self->m_data) return;
    if (self->m_data->m_isMask == isMask) return;
    XIcon_detach(self);
    self->m_data->m_isMask = isMask;
    XIconPrivate_touch(self->m_data);
}

bool XIcon_isMask(const XIcon* self) { return (self && self->m_data) ? self->m_data->m_isMask : false; }

void XIcon_fromTheme(const char* name, const XIcon* fallback, XIcon* out)
{
    (void)name;
    if (!out) return;
    XIcon_init(out);
    if (fallback)
        XIcon_copy_base(out, fallback);
}

bool XIcon_hasThemeIcon(const char* name) { (void)name; return false; }
void* XIcon_themeSearchPaths() { return NULL; }
void XIcon_setThemeSearchPaths(void* paths) { (void)paths; }
const char* XIcon_themeName() { return ""; }
void XIcon_setThemeName(const char* name) { (void)name; }
const char* XIcon_fallbackThemeName() { return ""; }
void XIcon_setFallbackThemeName(const char* name) { (void)name; }
