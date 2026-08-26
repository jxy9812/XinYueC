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
#include "XStringList.h"
#include "XPainter.h"
#include "XIconEngine.h"
#include "XIconThemeEngine.h"
#include "XIconThemeInternal.h"
#include <limits.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

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

static void XIconEntry_copy(void* destination, const void* source)
{
    const XIconEntry* src = (const XIconEntry*)source;
    XIconEntry* dest = (XIconEntry*)destination;
    if (!dest || !src) return;
    XPixmap_init(&dest->m_pixmap);
    XPixmap_copy_base(&dest->m_pixmap, &src->m_pixmap);
    dest->m_mode = src->m_mode;
    dest->m_state = src->m_state;
}

static void XIconEntry_move(void* destination, const void* source)
{
    XIconEntry* dest = (XIconEntry*)destination;
    XIconEntry* src = (XIconEntry*)source;
    if (!dest || !src) return;
    XPixmap_init(&dest->m_pixmap);
    XPixmap_move_base(&dest->m_pixmap, &src->m_pixmap);
    dest->m_mode = src->m_mode;
    dest->m_state = src->m_state;
}

static void XIconEntry_deinit(void* value)
{
    XIconEntry* entry = (XIconEntry*)value;
    if (entry) XPixmap_deinit_base(&entry->m_pixmap);
}

/**
 * @brief      XIcon 私有数据结构
 */
typedef struct XIconPrivate
{
    XAtomic_int32_t  m_refCount;    /**< 引用计数 */
    XVector          m_entries;     /**< 图标条目容器 */
    bool             m_isMask;      /**< 是否为掩码 */
    int64_t          m_cacheKey;    /**< 缓存键值 */
    int              m_serialNumber;/**< 序列号 */
    XString*         m_name;        /**< 文件或主题名称 */
    XIconEngine*     m_engine;      /**< 可选的自定义图标引擎，独占所有权 */
}XIconPrivate;

static int g_iconSerialCounter = 1;
static XStringList* g_iconThemeSearchPaths;
static XStringList* g_iconFallbackSearchPaths;
static XString* g_iconThemeName;
static XString* g_iconFallbackThemeName;

static void XIcon_replaceString(XString** destination, const char* value)
{
    XString* replacement = value ? XString_create_utf8(value) : NULL;
    if (*destination) XString_delete_base((XClass*)*destination);
    *destination = replacement;
}

static void XIcon_replaceString_2(XString** destination, const XString* value)
{
    XString* replacement = value ? XString_create_copy(value) : NULL;
    if (*destination) XString_delete_base((XClass*)*destination);
    *destination = replacement;
}

static XStringList* XIcon_paths(bool fallback)
{
    XStringList** paths = fallback ? &g_iconFallbackSearchPaths : &g_iconThemeSearchPaths;
    if (!*paths) *paths = XStringList_create();
    return *paths;
}

static bool XIcon_tryThemeFile(const char* fileName, XPixmap* out)
{
    XPixmap candidate;
    bool loaded;
    if (!fileName || !fileName[0]) return false;
    XPixmap_init(&candidate);
    loaded = XPixmap_load_2(&candidate, fileName, NULL, 0);
    if (loaded && out) {
        XPixmap_move_base(out, &candidate);
        XPixmap_deinit_base(&candidate);
    } else {
        XPixmap_deinit_base(&candidate);
    }
    return loaded;
}

static bool XIcon_resolveThemePixmap(const char* name, XPixmap* out)
{
    static const char* const extensions[] = {"", ".png", ".svg", ".xpm", ".bmp"};
    const XStringList* pathSets[2];
    const char* themeNames[2];
    size_t setIndex;
    size_t pathIndex;
    size_t extensionIndex;
    char fileName[1024];
    if (!name || !name[0]) return false;
    /* Absolute or explicitly relative names are valid theme resources too. */
    if (name[0] == '/' || name[0] == '.' || strchr(name, '/')) {
        for (extensionIndex = 0; extensionIndex < sizeof(extensions) / sizeof(extensions[0]); ++extensionIndex) {
            snprintf(fileName, sizeof(fileName), "%s%s", name, extensions[extensionIndex]);
            if (XIcon_tryThemeFile(fileName, out)) return true;
        }
    }
    pathSets[0] = XIcon_paths(false);
    pathSets[1] = XIcon_paths(true);
    themeNames[0] = g_iconThemeName ? XString_toUtf8(g_iconThemeName) : "";
    themeNames[1] = g_iconFallbackThemeName ? XString_toUtf8(g_iconFallbackThemeName) : "";
    for (setIndex = 0; setIndex < 2; ++setIndex) {
        const XStringList* paths = pathSets[setIndex];
        for (pathIndex = 0; paths && pathIndex < XStringList_size_base((XContainer*)paths); ++pathIndex) {
            const XString* path = (const XString*)XStringList_at_base((const XVector*)paths,
                                                                       (int64_t)pathIndex);
            const char* root = path ? XString_toUtf8(path) : NULL;
            if (!root || !root[0]) continue;
            for (extensionIndex = 0; extensionIndex < sizeof(extensions) / sizeof(extensions[0]); ++extensionIndex) {
                if (themeNames[setIndex] && themeNames[setIndex][0]) {
                    snprintf(fileName, sizeof(fileName), "%s/%s/%s%s", root,
                             themeNames[setIndex], name, extensions[extensionIndex]);
                    if (XIcon_tryThemeFile(fileName, out)) return true;
                }
                snprintf(fileName, sizeof(fileName), "%s/%s%s", root, name,
                         extensions[extensionIndex]);
                if (XIcon_tryThemeFile(fileName, out)) return true;
            }
        }
    }
    return false;
}

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
    XVector_init(&d->m_entries, sizeof(XIconEntry), true);
    XContainerSetDataCopyMethod(&d->m_entries, XIconEntry_copy);
    XContainerSetDataMoveMethod(&d->m_entries, XIconEntry_move);
    XContainerSetDataDeinitMethod(&d->m_entries, XIconEntry_deinit);
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
        XVector_deinit_base((XClass*)&d->m_entries);
        if (d->m_name) XString_delete_base((XClass*)d->m_name);
        if (d->m_engine) XIconEngine_delete_base(d->m_engine);
        XFree_System(d);
    }
}

static void XIconPrivate_setName_2(XIconPrivate* d, const XString* name);

static void XIconPrivate_setName(XIconPrivate* d, const char* name)
{
    XString* value = name ? XString_create_utf8(name) : NULL;
    XIconPrivate_setName_2(d, value);
    if (value) XString_delete_base((XClass*)value);
}

static void XIconPrivate_setName_2(XIconPrivate* d, const XString* name)
{
    if (!d) return;
    if (d->m_name) XString_delete_base((XClass*)d->m_name);
    d->m_name = name ? XString_create_copy(name) : NULL;
    XIconPrivate_touch(d);
}

static void XIconPrivate_addEntry(XIconPrivate* d, const XPixmap* pixmap, XIconMode mode, XIconState state)
{
    XIconEntry entry;
    if (!d || !pixmap || XPixmap_isNull(pixmap)) return;
    memset(&entry, 0, sizeof(entry));
    XPixmap_init(&entry.m_pixmap);
    XPixmap_copy_base(&entry.m_pixmap, pixmap);
    entry.m_mode = mode;
    entry.m_state = state;
    if (!XVector_push_back_move_1_base(&d->m_entries, &entry))
        XPixmap_deinit_base(&entry.m_pixmap);
    else
        XPixmap_deinit_base(&entry.m_pixmap);
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
    XVTABLE_INIT_DEFAULT(XIcon)
    XVTABLE_INHERIT_XCLASS(XClass);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Copy, VXIcon_copy);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Move, VXIcon_move);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Deinit, VXIcon_deinit);
    return XVTABLE_DEFAULT;
}

XIcon* XIcon_create_ex(XMemoryType memory)
{
    XIcon* self = (XIcon*)XMemory_malloc(sizeof(XIcon), memory);
    if (!self) return NULL;
    XIcon_init(self);
    Set_Class_Memory(self, memory); Set_Class_IsHeap(self, true);
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

void XIcon_init_file_2(XIcon* self, const char* fileName)
{
    XString* fileNameString = fileName ? XString_create_utf8(fileName) : NULL;
    XIcon_init_file(self, fileNameString);
    if (fileNameString) XString_delete_base((XClass*)fileNameString);
}

void XIcon_init_file(XIcon* self, const XString* fileName)
{
    XIcon_init(self);
    if (self && self->m_data) XIconPrivate_setName_2(self->m_data, fileName);
    XPixmap pixmap;
    XPixmap_init_file(&pixmap, fileName, NULL, 0);
    if (!XPixmap_isNull(&pixmap))
        XIconPrivate_addEntry(self->m_data, &pixmap, XIconMode_Normal, XIconState_Off);
    XPixmap_deinit_base(&pixmap);
}

void XIcon_init_engine(XIcon* self, XIconEngine* engine)
{
    XString* name;
    XIcon_init(self);
    if (!self || !self->m_data) {
        if (engine) XIconEngine_delete_base(engine);
        return;
    }
    self->m_data->m_engine = engine;
    name = engine ? XIconEngine_iconName_base(engine) : NULL;
    if (name) {
        XIconPrivate_setName_2(self->m_data, name);
        XString_delete_base((XClass*)name);
    }
}
bool XIcon_isNull(const XIcon* self)
{
    if (!self || !self->m_data) return true;
    if (self->m_data->m_engine)
        return XIconEngine_isNull_base(self->m_data->m_engine);
    return XVector_size_base((XContainer*)&self->m_data->m_entries) == 0;
}
bool XIcon_isDetached(const XIcon* self) { return !self || !self->m_data || XAtomic_load_int32(&self->m_data->m_refCount, XAtomic_MemoryOrder_Relaxed) == 1; }

void XIcon_detach(XIcon* self)
{
    if (!self || !self->m_data) return;
    if (XAtomic_load_int32(&self->m_data->m_refCount, XAtomic_MemoryOrder_Relaxed) > 1)
    {
        XIconPrivate* newData = XIconPrivate_create();
        if (newData)
        {
            if (self->m_data->m_engine) {
                newData->m_engine = XIconEngine_clone_base(self->m_data->m_engine);
                if (!newData->m_engine) {
                    XIconPrivate_unref(newData);
                    return;
                }
            }
            for (size_t i = 0; i < XVector_size_base((XContainer*)&self->m_data->m_entries); i++)
            {
                const XIconEntry* entry = (const XIconEntry*)XVector_at_base(&self->m_data->m_entries, (int64_t)i);
                XIconPrivate_addEntry(newData, &entry->m_pixmap, entry->m_mode, entry->m_state);
            }
            newData->m_isMask = self->m_data->m_isMask;
            XIconPrivate_setName(newData, self->m_data->m_name
                                 ? XString_toUtf8(self->m_data->m_name) : NULL);
            XIconPrivate_unref(self->m_data);
            self->m_data = newData;
        }
    }
}

int64_t XIcon_cacheKey(const XIcon* self)
{
    return (self && self->m_data && (self->m_data->m_engine ||
            XVector_size_base((XContainer*)&self->m_data->m_entries) > 0))
        ? self->m_data->m_cacheKey : 0;
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
    for (size_t i = 0; d && i < XVector_size_base((XContainer*)&d->m_entries); ++i)
    {
        const XIconEntry* candidate = (const XIconEntry*)XVector_at_base(&d->m_entries, (int64_t)i);
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
    if (self->m_data->m_engine)
    {
        XSize requested = { width, height };
        XIconEngine_pixmap_base(self->m_data->m_engine, &requested, mode, state, out);
        return;
    }
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
    if (self && self->m_data && self->m_data->m_engine) {
        XSize size = {width, height};
        if (!XClassIsVtableNull(out)) XPixmap_deinit_base(out);
        XIconEngine_scaledPixmap_base(self->m_data->m_engine, &size, mode, state,
                                      devicePixelRatio, out);
        return;
    }
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
    if (self->m_data->m_engine)
    {
        XSize requested = { width, height };
        XIconEngine_actualSize_base(self->m_data->m_engine, &requested, mode, state, out);
        return;
    }
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
    XPainter* target = (XPainter*)painter;
    XSize actual;
    XPixmap pixmap;
    XImage image;
    int drawX;
    int drawY;
    bool saved = true;
    if (!self || !target || !target->m_drawImage || w <= 0 || h <= 0) return;
    XIcon_actualSize(self, w, h, mode, state, &actual);
    if (actual.width <= 0 || actual.height <= 0) return;
    drawX = x;
    drawY = y;
    /* Qt::Alignment values are used as a portable bit mask here: Left=1,
     * Right=2, HCenter=4, Top=32, Bottom=64, VCenter=128. */
    if ((alignment & 4u) != 0u) drawX += (w - actual.width) / 2;
    else if ((alignment & 2u) != 0u) drawX += w - actual.width;
    if ((alignment & 128u) != 0u) drawY += (h - actual.height) / 2;
    else if ((alignment & 64u) != 0u) drawY += h - actual.height;
    if (self && self->m_data && self->m_data->m_engine) {
        XRect rect = {drawX, drawY, actual.width, actual.height};
        XIconEngine_paint_base(self->m_data->m_engine, painter, &rect, mode, state);
        return;
    }
    if (target->m_save) saved = target->m_save(target);
    XPixmap_init(&pixmap);
    XImage_init(&image);
    XIcon_pixmap(self, actual.width, actual.height, mode, state, &pixmap);
    if (!XPixmap_isNull(&pixmap)) {
        XPixmap_toImage(&pixmap, &image);
        target->m_drawImage(target, &image, drawX, drawY);
    }
    if (saved && target->m_restore) target->m_restore(target);
    XImage_deinit_base(&image);
    XPixmap_deinit_base(&pixmap);
}

void XIcon_addPixmap(XIcon* self, const XPixmap* pixmap, XIconMode mode, XIconState state)
{
    if (!self || !self->m_data || !pixmap || XPixmap_isNull(pixmap)) return;
    XIcon_detach(self);
    if (self->m_data->m_engine) {
        XIconEngine_addPixmap_base(self->m_data->m_engine, pixmap, mode, state);
        XIconPrivate_touch(self->m_data);
        return;
    }
    XIconPrivate_addEntry(self->m_data, pixmap, mode, state);
}

void XIcon_addFile_2(XIcon* self, const char* fileName, int width, int height,
                   XIconMode mode, XIconState state)
{
    XString* fileNameString = fileName ? XString_create_utf8(fileName) : NULL;
    XIcon_addFile(self, fileNameString, width, height, mode, state);
    if (fileNameString) XString_delete_base((XClass*)fileNameString);
}

void XIcon_addFile(XIcon* self, const XString* fileName, int width, int height,
                     XIconMode mode, XIconState state)
{
    if (!self || !self->m_data || !fileName) return;
    if (self->m_data->m_engine) {
        XSize size = {width, height};
        XIcon_detach(self);
        XIconEngine_addFile_base(self->m_data->m_engine, fileName, &size, mode, state);
        XIconPrivate_touch(self->m_data);
        return;
    }
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
    XVector_clear_base((XContainer*)sizes);
    if (!self || !self->m_data) return;
    if (self->m_data->m_engine) {
        XIconEngine_availableSizes_base(self->m_data->m_engine, mode, state, sizes);
        return;
    }

    for (size_t i = 0; i < XVector_size_base((XContainer*)&self->m_data->m_entries); ++i) {
        const XIconEntry* entry = (const XIconEntry*)XVector_at_base(&self->m_data->m_entries, (int64_t)i);
        if (entry->m_mode != mode || entry->m_state != state) continue;

        XSize size;
        size.width = XPixmap_width(&entry->m_pixmap);
        size.height = XPixmap_height(&entry->m_pixmap);
        bool duplicate = false;
        for (size_t j = 0; j < XVector_size_base((XContainer*)sizes); ++j) {
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

XString* XIcon_name(const XIcon* self)
{
    return (self && self->m_data && self->m_data->m_name)
        ? XString_create_copy(self->m_data->m_name) : XString_create();
}

const XString* XIcon_name_const(const XIcon* self)
{
    return (self && self->m_data && self->m_data->m_name) ? self->m_data->m_name : NULL;
}

const char* XIcon_name_2(const XIcon* self)
{
    return (self && self->m_data && self->m_data->m_name)
        ? XString_toUtf8(self->m_data->m_name) : "";
}

static void XIcon_fromThemeImpl(const XString* nameString,
                                  const char* utf8Name,
                                  const XIcon* fallback, XIcon* out)
{
    XString* created = NULL;
    XIconThemeEngine* engine;
    if (!out) return;
    if (!nameString && utf8Name) {
        created = XString_create_utf8(utf8Name);
        nameString = created;
    }
    XIcon_init(out);
    if (nameString) {
        engine = XIconThemeEngine_create_ex(XCLASS_DEFAULT_MEMORY_TYPE,
                                            nameString);
        if (engine) XIcon_init_engine(out, (XIconEngine*)engine);
    }
    if (created) XString_delete_base((XClass*)created);
    if (XIcon_isNull(out) && fallback)
        XIcon_copy_base(out, fallback);
}

void XIcon_fromTheme_2(const char* name, const XIcon* fallback, XIcon* out)
{
    XIcon_fromThemeImpl(NULL, name, fallback, out);
}

void XIcon_fromTheme(const XString* name, const XIcon* fallback, XIcon* out)
{
    XIcon_fromThemeImpl(name, NULL, fallback, out);
}

bool XIcon_hasThemeIcon_2(const char* name)
{
    XString* nameString = name ? XString_create_utf8(name) : NULL;
    bool found = XIcon_hasThemeIcon(nameString);
    if (nameString) XString_delete_base((XClass*)nameString);
    return found;
}

bool XIcon_hasThemeIcon(const XString* name)
{
    XPixmap pixmap;
    bool found;
    if (!name || XString_isEmpty_base((const XContainer*)name)) return false;
    XPixmap_init(&pixmap);
    found = XIcon_resolveThemePixmap(XString_toUtf8(name), &pixmap);
    XPixmap_deinit_base(&pixmap);
    return found;
}

static const char* XIcon_themeIconNameUtf8(XIconThemeIcon icon)
{
    static const char* const names[] = {
        "document-new", "document-open", "document-save", "document-save-as",
        "document-print", "document-close", "document-properties", "document-preview",
        "document-edit", "document-view", "document-reload", "folder", "folder-open",
        "folder-new", "user", "user-group", "user-add", "user-remove", "go-previous",
        "go-next", "go-up", "go-down", "media-playback-start", "media-playback-pause",
        "media-playback-stop", "media-record", "media-seek-forward", "media-seek-backward",
        "media-skip-forward", "media-skip-backward", "media-eject", "view-refresh",
        "view-list", "view-grid", "view-details", "view-sidebar", "view-fullscreen",
        "view-restore", "window-close", "window-minimize", "window-maximize", "window-restore",
        "application-exit", "help-browser", "help-about", "preferences-desktop"
    };
    return icon >= 0 && icon < (XIconThemeIcon)(sizeof(names) / sizeof(names[0]))
        ? names[icon] : NULL;
}

XString* XIcon_themeIconName(XIconThemeIcon icon)
{
    const char* name = XIcon_themeIconNameUtf8(icon);
    return name ? XString_create_utf8(name) : XString_create();
}

void XIcon_fromThemeIcon(XIconThemeIcon icon, const XIcon* fallback, XIcon* out)
{
    const char* name = XIcon_themeIconNameUtf8(icon);
    XIcon_fromTheme_2(name, fallback, out);
}

bool XIcon_hasThemeIconType(XIconThemeIcon icon)
{
    const char* name = XIcon_themeIconNameUtf8(icon);
    return name ? XIcon_hasThemeIcon_2(name) : false;
}

XStringList* XIcon_themeSearchPaths()
{
    XStringList* copy = XStringList_create();
    if (copy && g_iconThemeSearchPaths)
        XStringList_copy_base((XClass*)copy, (const XClass*)g_iconThemeSearchPaths);
    return copy;
}

XStringList* XIcon_themeSearchPaths_2()
{
    return XIcon_themeSearchPaths();
}

void XIcon_setThemeSearchPaths(const XStringList* source)
{
    XStringList* destination = XIcon_paths(false);
    if (destination == source) return;
    XStringList_clear_base((XContainer*)destination);
    if (source) XStringList_copy_base((XClass*)destination, (const XClass*)source);
}

void XIcon_setThemeSearchPaths_2(const XStringList* paths)
{
    XIcon_setThemeSearchPaths(paths);
}

XStringList* XIcon_fallbackSearchPaths()
{
    XStringList* copy = XStringList_create();
    if (copy && g_iconFallbackSearchPaths)
        XStringList_copy_base((XClass*)copy, (const XClass*)g_iconFallbackSearchPaths);
    return copy;
}

XStringList* XIcon_fallbackSearchPaths_2()
{
    return XIcon_fallbackSearchPaths();
}

void XIcon_setFallbackSearchPaths(const XStringList* source)
{
    XStringList* destination = XIcon_paths(true);
    if (destination == source) return;
    XStringList_clear_base((XContainer*)destination);
    if (source) XStringList_copy_base((XClass*)destination, (const XClass*)source);
}

void XIcon_setFallbackSearchPaths_2(const XStringList* paths)
{
    XIcon_setFallbackSearchPaths(paths);
}

XString* XIcon_themeName()
{
    return g_iconThemeName ? XString_create_copy(g_iconThemeName) : XString_create();
}
const char* XIcon_themeName_2()
{
    return g_iconThemeName ? XString_toUtf8(g_iconThemeName) : "";
}
void XIcon_setThemeName_2(const char* name) { XIcon_replaceString(&g_iconThemeName, name); }
void XIcon_setThemeName(const XString* name)
{
    XIcon_replaceString_2(&g_iconThemeName, name);
}
XString* XIcon_fallbackThemeName()
{
    return g_iconFallbackThemeName ? XString_create_copy(g_iconFallbackThemeName) : XString_create();
}
const char* XIcon_fallbackThemeName_2()
{
    return g_iconFallbackThemeName ? XString_toUtf8(g_iconFallbackThemeName) : "";
}
void XIcon_setFallbackThemeName_2(const char* name) { XIcon_replaceString(&g_iconFallbackThemeName, name); }
void XIcon_setFallbackThemeName(const XString* name)
{
    XIcon_replaceString_2(&g_iconFallbackThemeName, name);
}
