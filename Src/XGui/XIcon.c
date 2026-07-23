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

static int g_iconSerialCounter = 0;

static XIconPrivate* XIconPrivate_create()
{
    XIconPrivate* d = (XIconPrivate*)XMalloc_System(sizeof(XIconPrivate));
    if (!d) return NULL;
    memset(d, 0, sizeof(XIconPrivate));
    XAtomic_init(d->m_refCount, 1);
    d->m_serialNumber = (g_iconSerialCounter++);
    d->m_cacheKey = ((int64_t)d->m_serialNumber << 32) | (int64_t)(uintptr_t)d;
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
    if (!d || !pixmap) return;
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
}

/* ========== 虚函数实现 ========== */

static void VXIcon_copy(XIcon* dest, const XIcon* src)
{
    if (ISNULL(dest, "XIcon") || ISNULL(src, "XIcon")) return;
    if (dest->m_data)
        XIconPrivate_unref(dest->m_data);
    dest->m_data = src->m_data;
    XIconPrivate_ref(dest->m_data);
}

static void VXIcon_move(XIcon* dest, XIcon* src)
{
    if (ISNULL(dest, "XIcon") || ISNULL(src, "XIcon")) return;
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
    if (!XClassIsVtableNull(self))
        XIcon_deinit_base(self);
    XIcon_init(self);
    XIcon_copy_base(self, other);
}
void XIcon_move(XIcon* self, XIcon* other)
{
    if (ISNULL(self, "XIcon") || ISNULL(other, "XIcon")) return;
    if (!XClassIsVtableNull(self))
        XIcon_deinit_base(self);
    XIcon_init(self);
    XIcon_move_base(self, other);
}
void XIcon_deinit(XIcon* self) { XIcon_deinit_base(self); }
void XIcon_copy_base(XIcon* dest, const XIcon* src)
{
    if (ISNULL(dest, "XIcon") || ISNULL(src, "XIcon")) return;
    VXIcon_copy(dest, src);
}
void XIcon_move_base(XIcon* dest, XIcon* src)
{
    if (ISNULL(dest, "XIcon") || ISNULL(src, "XIcon")) return;
    VXIcon_move(dest, src);
}
void XIcon_deinit_base(XIcon* self)
{
    if (ISNULL(self, "XIcon")) return;
    VXIcon_deinit(self);
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

int64_t XIcon_cacheKey(const XIcon* self) { return (self && self->m_data) ? self->m_data->m_cacheKey : 0; }

void XIcon_pixmap(const XIcon* self, int width, int height, XIconMode mode, XIconState state, XPixmap* out)
{
    if (!self || !self->m_data || !out) return;
    // 查找最佳匹配条目
    XIconEntry* best = NULL;
    for (int i = 0; i < self->m_data->m_entryCount; i++)
    {
        XIconEntry* e = &self->m_data->m_entries[i];
        if (!best)
        {
            best = e;
            continue;
        }
        // 优先匹配模式和状态
        if (e->m_mode == mode && e->m_state == state)
        {
            best = e;
            break;
        }
        if (e->m_mode == mode && best->m_mode != mode) { best = e; continue; }
        if (e->m_state == state && best->m_state != state) { best = e; continue; }
    }
    if (best)
    {
        if (XPixmap_width(&best->m_pixmap) == width && XPixmap_height(&best->m_pixmap) == height)
            XPixmap_copy_base(out, &best->m_pixmap);
        else
            XPixmap_scaled(&best->m_pixmap, width, height, 0, 0, out);
    }
}

void XIcon_pixmapExtent(const XIcon* self, int extent, XIconMode mode, XIconState state, XPixmap* out)
{
    XIcon_pixmap(self, extent, extent, mode, state, out);
}

void XIcon_pixmapRatio(const XIcon* self, int width, int height, float devicePixelRatio,
                       XIconMode mode, XIconState state, XPixmap* out)
{
    XIcon_pixmap(self, (int)(width * devicePixelRatio), (int)(height * devicePixelRatio), mode, state, out);
}

void XIcon_actualSize(const XIcon* self, int width, int height, XIconMode mode, XIconState state, XSize* out)
{
    if (out) { out->width = width; out->height = height; }
}

void XIcon_paint(const XIcon* self, void* painter, int x, int y, int w, int h,
                 uint32_t alignment, XIconMode mode, XIconState state)
{
    (void)self; (void)painter; (void)x; (void)y; (void)w; (void)h; (void)alignment; (void)mode; (void)state;
}

void XIcon_addPixmap(XIcon* self, const XPixmap* pixmap, XIconMode mode, XIconState state)
{
    if (!self || !self->m_data || !pixmap) return;
    XIcon_detach(self);
    XIconPrivate_addEntry(self->m_data, pixmap, mode, state);
}

void XIcon_addFile(XIcon* self, const char* fileName, int width, int height,
                   XIconMode mode, XIconState state)
{
    if (!self || !self->m_data || !fileName) return;
    XPixmap pixmap;
    if (width > 0 && height > 0)
        XPixmap_init_ex(&pixmap, width, height);
    else
        XPixmap_init(&pixmap);
    XPixmap_load(&pixmap, fileName, NULL, 0);
    if (!XPixmap_isNull(&pixmap))
        XIcon_addPixmap(self, &pixmap, mode, state);
    XPixmap_deinit_base(&pixmap);
}

void XIcon_availableSizes(const XIcon* self, XIconMode mode, XIconState state, void* out)
{
    (void)self; (void)mode; (void)state; (void)out;
}

void XIcon_setIsMask(XIcon* self, bool isMask)
{
    if (!self || !self->m_data) return;
    XIcon_detach(self);
    self->m_data->m_isMask = isMask;
}

bool XIcon_isMask(const XIcon* self) { return (self && self->m_data) ? self->m_data->m_isMask : false; }

void XIcon_fromTheme(const char* name, const XIcon* fallback, XIcon* out)
{
    (void)name;
    if (fallback)
        XIcon_copy_base(out, fallback);
    else
        XIcon_init(out);
}

bool XIcon_hasThemeIcon(const char* name) { (void)name; return false; }
void* XIcon_themeSearchPaths() { return NULL; }
void XIcon_setThemeSearchPaths(void* paths) { (void)paths; }
const char* XIcon_themeName() { return ""; }
void XIcon_setThemeName(const char* name) { (void)name; }
const char* XIcon_fallbackThemeName() { return ""; }
void XIcon_setFallbackThemeName(const char* name) { (void)name; }



