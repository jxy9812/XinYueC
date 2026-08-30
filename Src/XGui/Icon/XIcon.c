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
#include "XIconScaledPixmapCache.h"
#include "XIconStyleHelper.h"
#include "XImageReader.h"
#include "XAlignment.h"
#include <limits.h>
#include <math.h>
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
    XString*    m_fileName; /**< 惰性加载条目的文件名 */
    XSize       m_requestedSize; /**< 调用方请求的逻辑尺寸 */
    XIconMode   m_mode;     /**< 图标模式 */
    XIconState  m_state;    /**< 图标状态 */
    bool        m_loaded;   /**< 已加载真实像素图 */
}XIconEntry;

static void XIconEntry_copy(void* destination, const void* source)
{
    const XIconEntry* src = (const XIconEntry*)source;
    XIconEntry* dest = (XIconEntry*)destination;
    if (!dest || !src) return;
    XPixmap_init(&dest->m_pixmap);
    XPixmap_copy_base(&dest->m_pixmap, &src->m_pixmap);
    dest->m_fileName = src->m_fileName
        ? XString_create_copy(src->m_fileName) : NULL;
    dest->m_requestedSize = src->m_requestedSize;
    dest->m_mode = src->m_mode;
    dest->m_state = src->m_state;
    dest->m_loaded = src->m_loaded;
}

static void XIconEntry_move(void* destination, const void* source)
{
    XIconEntry* dest = (XIconEntry*)destination;
    XIconEntry* src = (XIconEntry*)source;
    if (!dest || !src) return;
    XPixmap_init(&dest->m_pixmap);
    XPixmap_move_base(&dest->m_pixmap, &src->m_pixmap);
    dest->m_fileName = src->m_fileName;
    src->m_fileName = NULL;
    dest->m_requestedSize = src->m_requestedSize;
    dest->m_mode = src->m_mode;
    dest->m_state = src->m_state;
    dest->m_loaded = src->m_loaded;
}

static void XIconEntry_deinit(void* value)
{
    XIconEntry* entry = (XIconEntry*)value;
    if (!entry) return;
    if (entry->m_fileName) XString_delete_base((XClass*)entry->m_fileName);
    XPixmap_deinit_base(&entry->m_pixmap);
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

static int64_t XIconPrivate_areaOf(int width, int height);
static int64_t XIconPrivate_entryPhysicalArea(const XIconEntry* entry);

static void XIconPrivate_addEntry(XIconPrivate* d, const XPixmap* pixmap, XIconMode mode, XIconState state)
{
    XIconEntry entry;
    size_t i;
    if (!d || !pixmap || XPixmap_isNull(pixmap)) return;
    /* 对标 QPixmapIconEngine::addPixmap()：相同 mode/state、物理尺寸和
       DPR 的条目就地替换，避免重复条目改变匹配顺序或 availableSizes。 */
    for (i = 0; i < XVector_size_base((XContainer*)&d->m_entries); ++i)
    {
        XIconEntry* existing = (XIconEntry*)XVector_at_base(
            &d->m_entries, (int64_t)i);
        int existingWidth;
        int existingHeight;
        float existingScale;
        if (!existing || existing->m_mode != mode || existing->m_state != state)
            continue;
        if (existing->m_fileName && !existing->m_loaded)
        {
            existingWidth = existing->m_requestedSize.width;
            existingHeight = existing->m_requestedSize.height;
            existingScale = 1.0f;
        }
        else
        {
            existingWidth = XPixmap_width(&existing->m_pixmap);
            existingHeight = XPixmap_height(&existing->m_pixmap);
            existingScale = XPixmap_devicePixelRatio(&existing->m_pixmap);
        }
        if (existingWidth != XPixmap_width(pixmap) ||
            existingHeight != XPixmap_height(pixmap) ||
            existingScale != XPixmap_devicePixelRatio(pixmap))
            continue;
        if (existing->m_fileName)
        {
            XString_delete_base((XClass*)existing->m_fileName);
            existing->m_fileName = NULL;
        }
        XPixmap_deinit_base(&existing->m_pixmap);
        XPixmap_init(&existing->m_pixmap);
        XPixmap_copy_base(&existing->m_pixmap, pixmap);
        existing->m_requestedSize.width = 0;
        existing->m_requestedSize.height = 0;
        existing->m_loaded = true;
        XIconPrivate_touch(d);
        return;
    }
    memset(&entry, 0, sizeof(entry));
    XPixmap_init(&entry.m_pixmap);
    XPixmap_copy_base(&entry.m_pixmap, pixmap);
    entry.m_mode = mode;
    entry.m_state = state;
    entry.m_loaded = true;
    if (!XVector_push_back_move_1_base(&d->m_entries, &entry))
        XPixmap_deinit_base(&entry.m_pixmap);
    else
        XPixmap_deinit_base(&entry.m_pixmap);
    XIconPrivate_touch(d);
}

static bool XIconPrivate_canReadFile(const XString* fileName)
{
    XImageReader reader;
    bool canRead;
    if (!fileName) return false;
    XImageReader_init_file(&reader, fileName, NULL);
    canRead = XImageReader_canRead(&reader);
    XImageReader_deinit_base(&reader);
    return canRead;
}

/* 对标 QPixmapIconEngine::addFile()：带请求尺寸时先保存文件名与尺寸，
 * 不立即读入像素图，等取图时再按需加载。 */
static void XIconPrivate_addFileEntry(XIconPrivate* d, const XString* fileName,
                                      int width, int height,
                                      XIconMode mode, XIconState state)
{
    XIconEntry entry;
    if (!d || !fileName || width <= 0 || height <= 0) return;
    if (!XIconPrivate_canReadFile(fileName)) return;
    memset(&entry, 0, sizeof(entry));
    XPixmap_init(&entry.m_pixmap);
    entry.m_fileName = XString_create_copy(fileName);
    if (!entry.m_fileName) return;
    entry.m_requestedSize.width = width;
    entry.m_requestedSize.height = height;
    entry.m_mode = mode;
    entry.m_state = state;
    entry.m_loaded = false;
    if (!XVector_push_back_move_1_base(&d->m_entries, &entry))
        XIconEntry_deinit(&entry);
    else
        XIconEntry_deinit(&entry);
    XIconPrivate_touch(d);
}

/* 对标 Qt 6.8 QPixmapIconEngine::addFile() 的无尺寸分支
 * （qicon.cpp:447-493）：无尺寸请求不是“只读第一帧”，而是登记资源
 * 中的全部图像。处理器能够报告 Size 时登记文件名和每一帧尺寸，像素
 * 仍然延迟到取图时加载；否则按帧读取并立即保存像素图。项目在关闭
 * XImageIOPlugin 的裁剪配置下没有长期持有的处理器，此时
 * XImageReader_read() 会重复走单帧内置加载路径，必须退回一次
 * XPixmap_load()，否则枚举会无界重复。 */
static void XIconPrivate_addFileEntries(XIconPrivate* d,
                                         const XString* fileName,
                                         XIconMode mode, XIconState state)
{
    XImageReader reader;
    XSize size;
    bool hasSize;
    int imageCount;
    int frame;
    if (!d || !fileName || XContainer_isEmpty_base((const XContainer*)fileName))
        return;
    if (!XIconPrivate_canReadFile(fileName))
        return;

    XImageReader_init_file(&reader, fileName, NULL);
    hasSize = XImageReader_supportsOption(&reader, XImageIOHandlerOption_Size);
    if (hasSize) {
        /* Size 选项只查询当前帧的尺寸，不消费图像数据。 */
        do {
            XImageReader_size(&reader, &size);
            if (size.width > 0 && size.height > 0)
                XIconPrivate_addFileEntry(d, fileName, size.width, size.height,
                                          mode, state);
        } while (XImageReader_jumpToNextImage(&reader));
        XImageReader_deinit_base(&reader);
        return;
    }

    /* imageCount() 是轻量处理器对多帧能力的边界：默认处理器返回 1，
       动画处理器返回实际帧数；返回负值表示裁剪内置加载路径，不应
       对永远成功的单帧 fallback read() 做 while 循环。 */
    imageCount = XImageReader_imageCount(&reader);
    if (imageCount <= 0) {
        XPixmap pixmap;
        XPixmap_init(&pixmap);
        if (XPixmap_load(&pixmap, fileName, NULL, 0))
            XIconPrivate_addEntry(d, &pixmap, mode, state);
        XPixmap_deinit_base(&pixmap);
        XImageReader_deinit_base(&reader);
        return;
    }

    for (frame = 0; frame < imageCount; ++frame) {
        XImage image;
        XPixmap pixmap;
        XImage_init(&image);
        if (!XImageReader_read(&reader, &image)) {
            XImage_deinit_base(&image);
            break;
        }
        XPixmap_init(&pixmap);
        XPixmap_init_image(&pixmap, &image, 0);
        if (!XPixmap_isNull(&pixmap))
            XIconPrivate_addEntry(d, &pixmap, mode, state);
        XPixmap_deinit_base(&pixmap);
        XImage_deinit_base(&image);
    }
    XImageReader_deinit_base(&reader);
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
    if (!self || !self->m_data || !fileName ||
        XContainer_isEmpty_base((const XContainer*)fileName))
        return;
    /* Qt QIcon(fileName) 只把文件加入引擎，像素在第一次取图时再加载
       （qicon.cpp:764-785）。名称要与引擎分开保存，便于 name() 在
       加载失败时仍保持调用方传入的文件名语义。 */
    XIconPrivate_setName_2(self->m_data, fileName);
    XIconPrivate_addFileEntries(self->m_data, fileName, XIconMode_Normal,
                                XIconState_Off);
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
                XIconEntry copy;
                const XIconEntry* entry = (const XIconEntry*)XVector_at_base(&self->m_data->m_entries, (int64_t)i);
                memset(&copy, 0, sizeof(copy));
                XIconEntry_copy(&copy, entry);
                if (!XVector_push_back_move_1_base(&newData->m_entries, &copy))
                    XIconEntry_deinit(&copy);
                else
                    XIconEntry_deinit(&copy);
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

static int64_t XIconPrivate_areaOf(int width, int height)
{
    return (width > 0 && height > 0) ? (int64_t)width * height : 0;
}

static float XIconPrivate_entryScale(const XIconEntry* entry)
{
    float scale;
    if (!entry) return 1.0f;
    if (entry->m_fileName && !entry->m_loaded)
        return 1.0f;
    scale = XPixmap_devicePixelRatio(&entry->m_pixmap);
    return (scale > 0.0f) ? scale : 1.0f;
}

static void XIconPrivate_entryLogicalSize(const XIconEntry* entry, XSize* out)
{
    float scale;
    if (out)
    {
        out->width = 0;
        out->height = 0;
    }
    if (!entry || !out) return;
    if (entry->m_fileName && !entry->m_loaded)
    {
        out->width = entry->m_requestedSize.width;
        out->height = entry->m_requestedSize.height;
        return;
    }
    scale = XIconPrivate_entryScale(entry);
    out->width = (int)((float)XPixmap_width(&entry->m_pixmap) / scale + 0.5f);
    out->height = (int)((float)XPixmap_height(&entry->m_pixmap) / scale + 0.5f);
}

/* 返回候选条目的物理像素面积。
 * Qt 的 QPixmapIconEngineEntry::size 保存的是 pixmap.size()（物理尺寸），
 * 因此同一 DPR 下的 bestSizeScaleMatch 必须在物理单位比较候选与请求，
 * 不能把逻辑尺寸直接和已经乘过 DPR 的请求面积混用。 */
static int64_t XIconPrivate_entryPhysicalArea(const XIconEntry* entry)
{
    if (!entry) return 0;
    if (entry->m_fileName && !entry->m_loaded)
        return XIconPrivate_areaOf(entry->m_requestedSize.width,
                                   entry->m_requestedSize.height);
    return XIconPrivate_areaOf(XPixmap_width(&entry->m_pixmap),
                               XPixmap_height(&entry->m_pixmap));
}

/* 惰性加载 addFile 条目；按 Qt QPixmapIconEngine::bestMatch() 的逻辑，
 * 优先选择请求物理尺寸的帧，找不到时保留最后成功读取的帧。 */
static bool XIconPrivate_loadFileEntry(XIconPrivate* d, XIconEntry* entry,
                                       int requestedWidth, int requestedHeight,
                                       float scale)
{
    XImageReader reader;
    XImage image;
    XImage previous;
    XSize target;
    double targetWidth;
    double targetHeight;
    XSize frameSize;
    XSize readerSize;
    bool exact = false;
    bool readAny = false;
    bool hasSize = false;
    bool ok;
    (void)d;
    if (!entry || !entry->m_fileName || entry->m_loaded)
        return entry != NULL && entry->m_loaded;
    if (requestedWidth <= 0 || requestedHeight <= 0 ||
        !(scale > 0.0f) || !isfinite(scale))
        return false;
    targetWidth = (double)requestedWidth * (double)scale + 0.5;
    targetHeight = (double)requestedHeight * (double)scale + 0.5;
    if (targetWidth <= 0.0 || targetHeight <= 0.0 ||
        targetWidth > (double)INT_MAX || targetHeight > (double)INT_MAX)
        return false;
    target.width = (int)targetWidth;
    target.height = (int)targetHeight;

    XImageReader_init_file(&reader, entry->m_fileName, NULL);
    XImage_init(&image);
    XImage_init(&previous);
    hasSize = XImageReader_supportsOption(&reader, XImageIOHandlerOption_Size);
    if (hasSize) {
        /* 尺寸查询本身不读取像素；对支持 Size 的处理器，先寻找精确帧。 */
        XImageReader_size(&reader, &readerSize);
        if (readerSize.width == target.width && readerSize.height == target.height)
            exact = true;
        while (!exact && XImageReader_jumpToNextImage(&reader)) {
            XImageReader_size(&reader, &readerSize);
            if (readerSize.width == target.width && readerSize.height == target.height)
                exact = true;
        }
        if (exact)
            ok = XImageReader_read(&reader, &image);
        else {
            (void)XImageReader_jumpToImage(&reader, 0);
            ok = false;
        }
    } else {
        ok = false;
    }

    if (!ok) {
        /* Qt 在无法通过 Size 选中时回到逐帧 read()，并保留最后一帧。 */
        (void)XImageReader_jumpToImage(&reader, 0);
        while (XImageReader_read(&reader, &image)) {
            readAny = true;
            frameSize.width = XImage_width(&image);
            frameSize.height = XImage_height(&image);
            if (frameSize.width == target.width && frameSize.height == target.height) {
                exact = true;
                break;
            }
            XImage_deinit_base(&previous);
            XImage_init(&previous);
            XImage_copy_base(&previous, &image);
            XImage_deinit_base(&image);
            XImage_init(&image);
        }
        if (!exact && XImage_isNull(&image) && !XImage_isNull(&previous)) {
            XImage_move_base(&image, &previous);
            XImage_init(&previous);
        }
        ok = readAny || !XImage_isNull(&image);
    }

    if (!ok || XImage_isNull(&image)) {
        XImage_deinit_base(&previous);
        XImage_deinit_base(&image);
        XImageReader_deinit_base(&reader);
        return false;
    }

    XPixmap_deinit_base(&entry->m_pixmap);
    XPixmap_init_image(&entry->m_pixmap, &image, 0);
    if (XPixmap_isNull(&entry->m_pixmap)) {
        XImage_deinit_base(&previous);
        XImage_deinit_base(&image);
        XImageReader_deinit_base(&reader);
        return false;
    }
    XPixmap_setDevicePixelRatio(&entry->m_pixmap, scale);
    entry->m_loaded = true;
    XImage_deinit_base(&previous);
    XImage_deinit_base(&image);
    XImageReader_deinit_base(&reader);
    return true;
}

/* 默认像素图路径在可能触发惰性加载前需要写权限；共享数据先分离。 */
static void XIconPrivate_ensureWritableForLazyLoad(XIcon* self)
{
    size_t i;
    XIconPrivate* d;
    if (!self || !self->m_data) return;
    d = self->m_data;
    for (i = 0; i < XVector_size_base((XContainer*)&d->m_entries); ++i)
    {
        const XIconEntry* entry = (const XIconEntry*)XVector_at_base(
            &d->m_entries, (int64_t)i);
        if (entry->m_fileName && !entry->m_loaded)
        {
            XIcon_detach(self);
            return;
        }
    }
}

/* 对标 QPixmapIconEngine::removePixmapEntry()：懒加载失败后删除失效
 * 文件条目，使后续 isNull()/availableSizes() 不再把无法解码的资源当作
 * 有效像素图；调用方必须已经通过写时分离取得独占私有数据。 */
static void XIconPrivate_removeEntry(XIconPrivate* d,
                                     const XIconEntry* target)
{
    size_t i;
    if (!d || !target) return;
    for (i = 0; i < XVector_size_base((XContainer*)&d->m_entries); ++i)
    {
        const XIconEntry* entry = (const XIconEntry*)XVector_at_base(
            &d->m_entries, (int64_t)i);
        if (entry == target)
        {
            XVector_remove_base(&d->m_entries, (int64_t)i, 1);
            XIconPrivate_touch(d);
            return;
        }
    }
}

/* 对标 QPixmapIconEngine::adjustSize()：物理资源超过请求尺寸时按比例缩小，
 * 否则保留原始物理尺寸。 */
static void XIconPrivate_adjustSize(int expectedWidth, int expectedHeight,
                                    int width, int height, XSize* out)
{
    double scale;
    if (out)
    {
        out->width = width;
        out->height = height;
    }
    if (!out || width <= 0 || height <= 0 || expectedWidth <= 0 || expectedHeight <= 0)
        return;
    if (width <= expectedWidth && height <= expectedHeight)
        return;
    scale = (double)expectedWidth / width;
    if ((double)expectedHeight / height < scale)
        scale = (double)expectedHeight / height;
    out->width = (int)(width * scale + 0.5);
    out->height = (int)(height * scale + 0.5);
}

/* 对标 QIconPrivate::pixmapDevicePixelRatio()：根据实际返回尺寸修正输出 DPR。 */
static float XIconPrivate_pixmapDevicePixelRatio(float displayRatio,
                                                 int expectedWidth, int expectedHeight,
                                                 int actualWidth, int actualHeight)
{
    double scale;
    double adjusted;
    if (expectedWidth <= 0 || expectedHeight <= 0)
        return 1.0f;
    if ((actualWidth == expectedWidth && actualHeight <= expectedHeight) ||
        (actualWidth <= expectedWidth && actualHeight == expectedHeight))
        return displayRatio;
    scale = 0.5 * ((double)actualWidth / expectedWidth +
                   (double)actualHeight / expectedHeight);
    adjusted = displayRatio * scale;
    return (adjusted > 1.0) ? (float)adjusted : 1.0f;
}

/* 将逻辑请求尺寸转换为 Qt QIcon::pixmap() 使用的物理目标尺寸。
 * 该转换只用于 DPR 修正，不改变传给图标引擎的逻辑尺寸；浮点结果
 * 超出 int 范围时必须拒绝，避免把未定义的浮点到整数转换带入公共 API。 */
static bool XIconPrivate_physicalTargetSize(int width, int height,
                                            float devicePixelRatio,
                                            int* targetWidth,
                                            int* targetHeight)
{
    double scaledWidth;
    double scaledHeight;
    if (!targetWidth || !targetHeight || width <= 0 || height <= 0 ||
        !(devicePixelRatio > 0.0f) || !isfinite(devicePixelRatio))
        return false;
    scaledWidth = (double)width * (double)devicePixelRatio + 0.5;
    scaledHeight = (double)height * (double)devicePixelRatio + 0.5;
    if (scaledWidth <= 0.0 || scaledHeight <= 0.0 ||
        scaledWidth > (double)INT_MAX || scaledHeight > (double)INT_MAX)
        return false;
    *targetWidth = (int)scaledWidth;
    *targetHeight = (int)scaledHeight;
    return *targetWidth > 0 && *targetHeight > 0;
}

/* 对标 QPixmapIconEngine::bestSizeScaleMatch()：先按 DPR 打分，优先匹配目标
 * DPR，其次取误差最小；DPR 相同时再按逻辑面积进行尺寸匹配。 */
static const XIconEntry* XIconPrivate_bestSizeScaleMatch(int width, int height,
                                                         float scale,
                                                         const XIconEntry* left,
                                                         const XIconEntry* right)
{
    float scaleLeft;
    float scaleRight;
    float scoreLeft;
    float scoreRight;
    float absLeft;
    float absRight;
    int64_t requestedArea;
    int64_t areaLeft;
    int64_t areaRight;
    if (!right) return left;
    if (!left) return right;

    scaleLeft = XIconPrivate_entryScale(left);
    scaleRight = XIconPrivate_entryScale(right);
    if (scaleLeft != scaleRight)
    {
        scoreLeft = scaleLeft - scale;
        scoreRight = scaleRight - scale;
        if ((scoreLeft < 0.0f) != (scoreRight < 0.0f))
            return (scoreRight < 0.0f) ? left : right;
        absLeft = (scoreLeft < 0.0f) ? -scoreLeft : scoreLeft;
        absRight = (scoreRight < 0.0f) ? -scoreRight : scoreRight;
        return (absLeft < absRight) ? left : right;
    }

    {
        double scaledWidth = (double)width * (double)scale + 0.5;
        double scaledHeight = (double)height * (double)scale + 0.5;
        int requestedWidth = (scaledWidth <= 0.0) ? 0
            : (scaledWidth > (double)INT_MAX ? INT_MAX : (int)scaledWidth);
        int requestedHeight = (scaledHeight <= 0.0) ? 0
            : (scaledHeight > (double)INT_MAX ? INT_MAX : (int)scaledHeight);
        requestedArea = XIconPrivate_areaOf(requestedWidth, requestedHeight);
    }
    areaLeft = XIconPrivate_entryPhysicalArea(left);
    areaRight = XIconPrivate_entryPhysicalArea(right);
    if (areaLeft >= requestedArea && areaRight >= requestedArea)
        return (areaLeft < areaRight) ? left : right;
    if (areaLeft < requestedArea && areaRight < requestedArea)
        return (areaLeft > areaRight) ? left : right;
    return (areaLeft >= requestedArea) ? left : right;
}

/* 对标 QPixmapIconEngine::tryMatch()：只在指定 mode/state 内寻找最优条目。 */
static const XIconEntry* XIconPrivate_tryMatchScale(const XIconPrivate* d,
                                                     int width, int height,
                                                     float scale,
                                                     XIconMode mode,
                                                     XIconState state)
{
    const XIconEntry* best = NULL;
    for (size_t i = 0; d && i < XVector_size_base((XContainer*)&d->m_entries); ++i)
    {
        const XIconEntry* candidate = (const XIconEntry*)XVector_at_base(&d->m_entries, (int64_t)i);
        if (candidate->m_mode != mode || candidate->m_state != state)
            continue;
        best = XIconPrivate_bestSizeScaleMatch(width, height, scale, best, candidate);
    }
    return best;
}

/* 对标 QPixmapIconEngine::bestMatch()：保持 Qt 的 mode/state 回退顺序。 */
static const XIconEntry* XIconPrivate_bestEntryScale(const XIconPrivate* d,
                                                      int width, int height,
                                                      float scale,
                                                      XIconMode mode,
                                                      XIconState state)
{
    const XIconState oppositeState = (state == XIconState_On)
        ? XIconState_Off : XIconState_On;
    const XIconEntry* best = XIconPrivate_tryMatchScale(d, width, height, scale, mode, state);
    if (best) return best;

    if (mode == XIconMode_Disabled || mode == XIconMode_Selected)
    {
        const XIconMode oppositeMode = (mode == XIconMode_Disabled)
            ? XIconMode_Selected : XIconMode_Disabled;
        best = XIconPrivate_tryMatchScale(d, width, height, scale, XIconMode_Normal, state);
        if (!best) best = XIconPrivate_tryMatchScale(d, width, height, scale, XIconMode_Active, state);
        if (!best) best = XIconPrivate_tryMatchScale(d, width, height, scale, mode, oppositeState);
        if (!best) best = XIconPrivate_tryMatchScale(d, width, height, scale, XIconMode_Normal, oppositeState);
        if (!best) best = XIconPrivate_tryMatchScale(d, width, height, scale, XIconMode_Active, oppositeState);
        if (!best) best = XIconPrivate_tryMatchScale(d, width, height, scale, oppositeMode, state);
        if (!best) best = XIconPrivate_tryMatchScale(d, width, height, scale, oppositeMode, oppositeState);
    }
    else
    {
        const XIconMode oppositeMode = (mode == XIconMode_Normal)
            ? XIconMode_Active : XIconMode_Normal;
        best = XIconPrivate_tryMatchScale(d, width, height, scale, oppositeMode, state);
        if (!best) best = XIconPrivate_tryMatchScale(d, width, height, scale, mode, oppositeState);
        if (!best) best = XIconPrivate_tryMatchScale(d, width, height, scale, oppositeMode, oppositeState);
        if (!best) best = XIconPrivate_tryMatchScale(d, width, height, scale, XIconMode_Disabled, state);
        if (!best) best = XIconPrivate_tryMatchScale(d, width, height, scale, XIconMode_Selected, state);
        if (!best) best = XIconPrivate_tryMatchScale(d, width, height, scale, XIconMode_Disabled, oppositeState);
        if (!best) best = XIconPrivate_tryMatchScale(d, width, height, scale, XIconMode_Selected, oppositeState);
    }
    return best;
}

/* 对标 QPixmapIconEngine::scaledPixmap() 的默认像素图路径。
 * 选定最优条目后，以源像素图缓存键参与 XIconScaledPixmapCache 键；未命中时
 * 按目标物理尺寸缩放并回写 DPR，随后将结果缓存。 */
static void XIconPrivate_scaledPixmap(const XIconPrivate* d, int width, int height,
                                      float devicePixelRatio, XIconMode mode,
                                      XIconState state, XPixmap* out)
{
    double scaledWidth;
    double scaledHeight;
    int targetWidth;
    int targetHeight;
    float outputRatio = 1.0f;
    const XIconEntry* best;
    XSize actual;
    char sourceKey[32];
    int dprThousand;
    uint64_t paletteKey;
    if (!out || !d) return;
    /* C 的浮点到整数转换对无穷值没有定义；公开高 DPI 路径会把正无穷
       送到这里，先拒绝它，避免生成不可分配的目标尺寸。 */
    if (!isfinite(devicePixelRatio)) return;
    if (devicePixelRatio <= 0.0f) devicePixelRatio = 1.0f;
    scaledWidth = (double)width * devicePixelRatio + 0.5;
    scaledHeight = (double)height * devicePixelRatio + 0.5;
    targetWidth = (scaledWidth <= 0.0) ? 0 :
        ((scaledWidth > INT_MAX) ? INT_MAX : (int)scaledWidth);
    targetHeight = (scaledHeight <= 0.0) ? 0 :
        ((scaledHeight > INT_MAX) ? INT_MAX : (int)scaledHeight);
    if (width <= 0 || height <= 0 || targetWidth <= 0 || targetHeight <= 0)
        return;
    best = XIconPrivate_bestEntryScale(
        d, width, height, devicePixelRatio, mode, state);
    if (!best) return;
    if (best->m_fileName && !best->m_loaded &&
        !XIconPrivate_loadFileEntry((XIconPrivate*)d, (XIconEntry*)best,
                                     width, height, devicePixelRatio))
    {
        XIconPrivate_removeEntry((XIconPrivate*)d, best);
        return;
    }
    actual.width = XPixmap_width(&best->m_pixmap);
    actual.height = XPixmap_height(&best->m_pixmap);
    XIconPrivate_adjustSize(targetWidth, targetHeight,
                            actual.width, actual.height, &actual);
    outputRatio = XIconPrivate_pixmapDevicePixelRatio(
        devicePixelRatio, targetWidth, targetHeight,
        actual.width, actual.height);
    dprThousand = (int)(outputRatio * 1000.0f + 0.5f);
    snprintf(sourceKey, sizeof(sourceKey), "%llx",
             (unsigned long long)XPixmap_cacheKey(&best->m_pixmap));
    paletteKey = XIconStyleHelper_paletteCacheKey();
    if (XIconScaledPixmapCache_find(
            "qt_icon_scale/", sourceKey, paletteKey, mode,
            actual.width, actual.height, dprThousand, out))
        return;
    XPixmap_copy_base(out, &best->m_pixmap);
    if (actual.width != XPixmap_width(&best->m_pixmap) ||
        actual.height != XPixmap_height(&best->m_pixmap))
    {
        XPixmap scaled;
        XPixmap_init(&scaled);
        XPixmap_scaled(&best->m_pixmap, actual.width, actual.height, 0, 0,
                       &scaled);
        if (!XPixmap_isNull(&scaled))
        {
            XPixmap_deinit_base(out);
            out->m_data = scaled.m_data;
            scaled.m_data = NULL;
        }
        XPixmap_deinit_base(&scaled);
    }
    if (best->m_mode != mode && mode != XIconMode_Normal)
    {
        XPixmap styled;
        XPixmap_init(&styled);
        XIconStyleHelper_apply(mode, out, &styled);
        if (!XPixmap_isNull(&styled))
        {
            XPixmap_deinit_base(out);
            XPixmap_move_base(out, &styled);
        }
        XPixmap_deinit_base(&styled);
    }
    XPixmap_setDevicePixelRatio(out, outputRatio);
    if (!XPixmap_isNull(out))
        XIconScaledPixmapCache_insert(
            "qt_icon_scale/", sourceKey, paletteKey, mode,
            actual.width, actual.height, dprThousand, out);
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
    XIconPrivate_ensureWritableForLazyLoad((XIcon*)(uintptr_t)self);
    XIconPrivate_scaledPixmap(self->m_data, width, height, 1.0f,
                              mode, state, out);
}

void XIcon_pixmapExtent(const XIcon* self, int extent, XIconMode mode, XIconState state, XPixmap* out)
{
    XIcon_pixmap(self, extent, extent, mode, state, out);
}

void XIcon_pixmapRatio(const XIcon* self, int width, int height, float devicePixelRatio,
                       XIconMode mode, XIconState state, XPixmap* out)
{
    int targetWidth;
    int targetHeight;
    if (!out) return;
    if (devicePixelRatio <= 0.0f) devicePixelRatio = 1.0f;
    if (self && self->m_data && self->m_data->m_engine) {
        XSize size = {width, height};
        if (!XClassIsVtableNull(out)) XPixmap_deinit_base(out);
        if (devicePixelRatio > 1.0f) {
            XIconEngine_scaledPixmap_base(self->m_data->m_engine, &size, mode,
                                          state, devicePixelRatio, out);
            /* Qt 6.8 QIcon::pixmap() 修正的是引擎返回像素图的 DPR，而
               不是无条件采用请求比例。自定义引擎可能返回较小或非正方
               形资源，因此必须使用实际物理尺寸重新计算。 */
            if (!XPixmap_isNull(out) &&
                XIconPrivate_physicalTargetSize(width, height,
                                                devicePixelRatio,
                                                &targetWidth, &targetHeight)) {
                float outputRatio = XIconPrivate_pixmapDevicePixelRatio(
                    devicePixelRatio, targetWidth, targetHeight,
                    XPixmap_width(out), XPixmap_height(out));
                XPixmap_setDevicePixelRatio(out, outputRatio);
            }
        } else {
            XIconEngine_pixmap_base(self->m_data->m_engine, &size, mode, state,
                                    out);
            if (!XPixmap_isNull(out)) XPixmap_setDevicePixelRatio(out, 1.0f);
        }
        return;
    }
    if (!self || !self->m_data)
        return;
    if (!XClassIsVtableNull(out)) XPixmap_deinit_base(out);
    XPixmap_init(out);
    XIconPrivate_ensureWritableForLazyLoad((XIcon*)(uintptr_t)self);
    if (!(devicePixelRatio > 1.0f)) {
        /* Qt keeps the normal-DPI path independent of the requested sub-normal
           ratio and fixes the returned pixmap DPR at one. */
        XIcon_pixmap(self, width, height, mode, state, out);
        if (!XPixmap_isNull(out)) XPixmap_setDevicePixelRatio(out, 1.0f);
        return;
    }
    XIconPrivate_scaledPixmap(self->m_data, width, height, devicePixelRatio,
                              mode, state, out);
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
    /* QIcon::actualSize() 通过 bestMatch() 触发懒加载；共享图标在此处
       修改条目前先分离，避免 const 查询改变其它副本的缓存生命周期。 */
    XIconPrivate_ensureWritableForLazyLoad((XIcon*)(uintptr_t)self);
    const XIconEntry* best = XIconPrivate_bestEntryScale(
        self->m_data, width, height, 1.0f, mode, state);
    if (!best) return;
    if (best->m_fileName && !best->m_loaded &&
        !XIconPrivate_loadFileEntry(self->m_data, (XIconEntry*)best,
                                    width, height, 1.0f))
    {
        XIconPrivate_removeEntry(self->m_data, best);
        return;
    }
    XSize sourceSize;
    XIconPrivate_entryLogicalSize(best, &sourceSize);
    int sourceWidth = sourceSize.width;
    int sourceHeight = sourceSize.height;
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

/* 对标 Qt 6.8 QIcon::actualSize() 的高 DPI 路径：引擎收到物理请求，
 * 返回的物理尺寸再按实际像素量修正 DPR，最后还原为逻辑尺寸。 */
void XIcon_actualSizeRatio(const XIcon* self, int width, int height,
                           float devicePixelRatio, XIconMode mode,
                           XIconState state, XSize* out)
{
    int targetWidth;
    int targetHeight;
    XSize actual;
    float outputRatio;
    if (!out) return;
    out->width = 0;
    out->height = 0;
    if (!self || !self->m_data || width <= 0 || height <= 0)
        return;
    /* NaN 与 Qt 的 !(ratio > 1) 分支一致；无穷值不能安全转换为尺寸。 */
    if (!(devicePixelRatio > 1.0f)) {
        XIcon_actualSize(self, width, height, mode, state, out);
        return;
    }
    if (!isfinite(devicePixelRatio) ||
        !XIconPrivate_physicalTargetSize(width, height, devicePixelRatio,
                                         &targetWidth, &targetHeight))
        return;

    actual.width = 0;
    actual.height = 0;
    if (self->m_data->m_engine) {
        XSize requested = { targetWidth, targetHeight };
        XIconEngine_actualSize_base(self->m_data->m_engine, &requested,
                                    mode, state, &actual);
    } else {
        /* 高 DPI actualSize 同样复用 bestMatch() 的懒加载语义；先对
           共享私有数据做写时分离，再允许加载成功的条目更新缓存。 */
        XIconPrivate_ensureWritableForLazyLoad((XIcon*)(uintptr_t)self);
        const XIconEntry* best = XIconPrivate_bestEntryScale(
            self->m_data, targetWidth, targetHeight, 1.0f, mode, state);
        if (!best) return;
        if (best->m_fileName && !best->m_loaded &&
            !XIconPrivate_loadFileEntry((XIconPrivate*)self->m_data,
                                         (XIconEntry*)best,
                                         targetWidth, targetHeight, 1.0f))
        {
            XIconPrivate_removeEntry(self->m_data, best);
            return;
        }
        actual.width = XPixmap_width(&best->m_pixmap);
        actual.height = XPixmap_height(&best->m_pixmap);
        XIconPrivate_adjustSize(targetWidth, targetHeight,
                                actual.width, actual.height, &actual);
    }
    if (actual.width <= 0 || actual.height <= 0)
        return;
    outputRatio = XIconPrivate_pixmapDevicePixelRatio(
        devicePixelRatio, targetWidth, targetHeight,
        actual.width, actual.height);
    if (!(outputRatio > 0.0f) || !isfinite(outputRatio))
        return;
    out->width = (int)((double)actual.width / outputRatio + 0.5);
    out->height = (int)((double)actual.height / outputRatio + 0.5);
    if (out->width < 0 || out->height < 0) {
        out->width = 0;
        out->height = 0;
    }
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
    bool saved = false;
    if (!self || !target || w <= 0 || h <= 0) return;
#if XPAINTER_LAYOUT_DIRECTION_ON
    /* 对齐 Qt QGuiApplicationPrivate::visualAlignment：没有水平位时补左对齐，
       RTL 且未指定 Absolute 时交换 Left/Right；Auto 方向不强制交换。 */
    if ((alignment & XAlignment_HorizontalMask) == 0u)
        alignment |= XAlignment_Left;
    if ((alignment & XAlignment_Absolute) == 0u &&
        (alignment & (XAlignment_Left | XAlignment_Right)) != 0u &&
        XPainter_layoutDirection(target) == XPainterLayoutDirection_RightToLeft) {
        alignment ^= (XAlignment_Left | XAlignment_Right);
        alignment |= XAlignment_Absolute;
    }
#endif /* XPAINTER_LAYOUT_DIRECTION_ON */
    XIcon_actualSize(self, w, h, mode, state, &actual);
    if (actual.width <= 0 || actual.height <= 0) return;
    drawX = x;
    drawY = y;
    /* Qt::Alignment values are used as a portable bit mask here: Left=1,
     * Right=2, HCenter=4, Top=32, Bottom=64, VCenter=128. */
    if ((alignment & 2u) != 0u) drawX += w - actual.width;
    else if ((alignment & 4u) != 0u) drawX += (w - actual.width) / 2;
    if ((alignment & 128u) != 0u) drawY += (h - actual.height) / 2;
    else if ((alignment & 64u) != 0u) drawY += h - actual.height;
    if (self && self->m_data && self->m_data->m_engine) {
        XRect rect = {drawX, drawY, actual.width, actual.height};
        XIconEngine_paint_base(self->m_data->m_engine, painter, &rect, mode, state);
        return;
    }
    if (!target->m_drawImage) return;
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
    XIcon_detach(self);
    if (width > 0 && height > 0)
    {
        XIconPrivate_addFileEntry(self->m_data, fileName, width, height,
                                  mode, state);
        return;
    }
    /* Qt 的无效 QSize 表示“加入文件中的全部帧”，而不是只读第一帧；
       文件构造和显式 addFile 的语义保持一致。 */
    XIconPrivate_addFileEntries(self->m_data, fileName, mode, state);
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
        XIconPrivate_entryLogicalSize(entry, &size);
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
    const char* nameUtf8;
    if (!out) return;
    if (!nameString && utf8Name) {
        created = XString_create_utf8(utf8Name);
        nameString = created;
    }
    /* QIcon::fromTheme() 返回新值；C 接口通过 out 写回时必须先释放
       调用方已有的私有数据，否则每次复用同一输出对象都会遗失一次
       XIconPrivate 引用。部分 Qt 兼容夹具直接把未初始化栈对象作为 out，
       此时其中的虚表字段可能是随机值，不能仅用“非 NULL”判定后调用析构；
       只有明确绑定 XIcon 虚表的对象才允许释放。 */
    if (XClassGetVtable(out) == XIcon_class_init())
        XIcon_deinit_base(out);
    XIcon_init(out);
    if (nameString) {
        nameUtf8 = XString_toUtf8(nameString);
        /* Qt QIcon::fromTheme() 对绝对路径直接构造文件图标；Unix 的 `/`
           路径和 Qt 资源的 `:/` 路径都不应交给 QIconLoader 当作主题名。 */
        if (nameUtf8 && (nameUtf8[0] == '/' || nameUtf8[0] == ':')) {
            XIcon_deinit_base(out);
            XIcon_init_file(out, nameString);
            goto fromTheme_done;
        }
        engine = XIconThemeEngine_create_ex(XCLASS_DEFAULT_MEMORY_TYPE,
                                            nameString);
        if (engine && out->m_data) {
            XString* engineName;
            /* out 已由上方初始化，直接接管引擎，避免重复创建私有数据。 */
            out->m_data->m_engine = (XIconEngine*)engine;
            engineName = XIconEngine_iconName_base(
                (const XIconEngine*)engine);
            if (engineName) {
                XIconPrivate_setName_2(out->m_data, engineName);
                XString_delete_base((XClass*)engineName);
            }
        } else if (engine) {
            XIconEngine_delete_base((XIconEngine*)engine);
        }
    }
fromTheme_done:
    if (created) XString_delete_base((XClass*)created);
    if (fallback) {
        bool useFallback = XIcon_isNull(out);
        /* Qt 6.8 QIcon::fromTheme(name, fallback) treats an engine with no
           available sizes as unusable even when isNull() is false.  The
           latter occurs for registered theme files whose decoding is
           deferred (or fails later), so query the default Normal/Off set
           before returning the theme engine. */
        if (!useFallback) {
            XVector available;
            XVector_init(&available, sizeof(XSize), true);
            XIcon_availableSizes(out, XIconMode_Normal, XIconState_Off,
                                 &available);
            useFallback = XVector_size_base(
                (const XContainer*)&available) == 0;
            XVector_deinit_base((XClass*)&available);
        }
        if (useFallback)
            XIcon_copy_base(out, fallback);
    }
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
    const char* nameUtf8;
    XString* resolvedName;
    bool matched;
    if (!name || XString_isEmpty_base((const XContainer*)name)) return false;
    nameUtf8 = XString_toUtf8(name);
    /* Qt QIcon::hasThemeIcon() delegates to fromTheme().  Absolute paths are
       handled by fromTheme() as ordinary file icons whose engine has no
       theme name, so they must not be reported as theme icons. */
    if (!nameUtf8 || nameUtf8[0] == '/' || nameUtf8[0] == ':') return false;
    /* Qt 6.8 的 hasThemeIcon() 比较 fromTheme(name).name() 与原始请求。
       因此短横线回退到较短名称时，虽然 fromTheme() 可绘制图标，
       hasThemeIcon() 仍必须返回 false；仅 exact 命中才算主题图标可用。 */
    resolvedName = XIconInternal_resolveThemeIconName(nameUtf8);
    matched = resolvedName && XString_equals_utf8(
        resolvedName, nameUtf8, XChar_CaseSensitive);
    if (resolvedName) XString_delete_base((XClass*)resolvedName);
    return matched;
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
    if (!copy) return NULL;
    if (g_iconThemeSearchPaths &&
        XStringList_size_base((const XContainer*)g_iconThemeSearchPaths) > 0) {
        XStringList_copy_base((XClass*)copy,
                              (const XClass*)g_iconThemeSearchPaths);
    } else {
        /* Qt QIconLoader::themeSearchPaths() 在用户未设置路径时始终
           附加内置资源目录，空列表也必须保持该默认项。平台主题路径
           由 XGui 的 Drive 层提供；公共层至少保留可移植的 :/icons。 */
        XStringList_push_back_utf8(copy, ":/icons");
    }
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
    XIconScaledPixmapCache_clear();
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
    XIconScaledPixmapCache_clear();
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
void XIcon_setThemeName_2(const char* name)
{
    XIcon_replaceString(&g_iconThemeName, name);
    XIconScaledPixmapCache_clear();
}
void XIcon_setThemeName(const XString* name)
{
    XIcon_replaceString_2(&g_iconThemeName, name);
    XIconScaledPixmapCache_clear();
}
XString* XIcon_fallbackThemeName()
{
    return g_iconFallbackThemeName ? XString_create_copy(g_iconFallbackThemeName) : XString_create();
}
const char* XIcon_fallbackThemeName_2()
{
    return g_iconFallbackThemeName ? XString_toUtf8(g_iconFallbackThemeName) : "";
}
void XIcon_setFallbackThemeName_2(const char* name)
{
    XIcon_replaceString(&g_iconFallbackThemeName, name);
    XIconScaledPixmapCache_clear();
}
void XIcon_setFallbackThemeName(const XString* name)
{
    XIcon_replaceString_2(&g_iconFallbackThemeName, name);
    XIconScaledPixmapCache_clear();
}
