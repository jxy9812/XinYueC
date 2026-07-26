/******************************************************************************
 * @file       XPixmapCache.c
 * @brief      XPixmapCache 全局像素图缓存类实现（对标 Qt 6.8 QPixmapCache）
 * @author     XinYueC 团队
 ******************************************************************************/
#include "XPixmapCache.h"
#include "XMemory.h"
#include <string.h>
#include <stdlib.h>
#include <limits.h>

/* ========== 缓存数据结构 ========== */

/**
 * @brief      缓存条目
 */
typedef struct XCacheEntry
{
    char*            m_key;       /**< 字符串键（可为 NULL） */
    XPixmapCacheKeyData* m_keyData; /**< 共享 Key 数据（可为 NULL） */
    XPixmap          m_pixmap;    /**< 缓存的像素图 */
    struct XCacheEntry* m_next;   /**< 链表下一项 */
    int              m_size;      /**< 缓存项大小估计 */
    bool             m_hasStringKey; /**< 是否有字符串键 */
}XCacheEntry;

/**
 * @brief      全局缓存状态
 */
static struct
{
    XCacheEntry* m_head;      /**< 链表头 */
    int64_t      m_cacheSize; /**< 当前缓存总大小（估计值，KB） */
    int          m_cacheLimit;/**< 缓存大小限制（KB） */
    int          m_entryCount;/**< 条目数量 */
} g_cache = {NULL, 0, 10240, 0};  // 默认限制 10MB

/* ========== XPixmapCacheKey 实现 ========== */

/**
 * @brief      XPixmapCacheKey 私有数据
 */
typedef struct XPixmapCacheKeyData
{
    XAtomic_int32_t  m_refCount;    /**< 引用计数 */
    int64_t          m_cacheKey;    /**< 像素图缓存键值 */
    int              m_serial;      /**< 序列号 */
    bool             m_isValid;     /**< 是否有效 */
}XPixmapCacheKeyData;

static int g_keySerialCounter = 0;

static XPixmapCacheKeyData* XPixmapCacheKeyData_create(bool valid)
{
    XPixmapCacheKeyData* d = (XPixmapCacheKeyData*)XMalloc_System(sizeof(XPixmapCacheKeyData));
    if (!d) return NULL;
    memset(d, 0, sizeof(XPixmapCacheKeyData));
    XAtomic_init(d->m_refCount, 1);
    d->m_serial = (g_keySerialCounter++);
    d->m_isValid = valid;
    return d;
}

static void XPixmapCacheKeyData_ref(XPixmapCacheKeyData* d) { if (d) XAtomic_fetch_add_int32(&d->m_refCount, 1, XAtomic_MemoryOrder_SeqCst); }

static void XPixmapCacheKeyData_unref(XPixmapCacheKeyData* d)
{
    if (!d) return;
    if (XAtomic_fetch_add_int32(&d->m_refCount, -1, XAtomic_MemoryOrder_SeqCst) == 1)
        XFree_System(d);
}

void XPixmapCacheKey_init(XPixmapCacheKey* key)
{
    if (!key) return;
    key->m_data = NULL;
}

void XPixmapCacheKey_copy(XPixmapCacheKey* dest, const XPixmapCacheKey* src)
{
    if (!dest || !src) return;
    if (dest == src || dest->m_data == src->m_data) return;
    XPixmapCacheKeyData_ref(src->m_data);
    XPixmapCacheKeyData_unref(dest->m_data);
    dest->m_data = src->m_data;
}

void XPixmapCacheKey_deinit(XPixmapCacheKey* key)
{
    if (!key || !key->m_data) return;
    XPixmapCacheKeyData_unref(key->m_data);
    key->m_data = NULL;
}

bool XPixmapCacheKey_isValid(const XPixmapCacheKey* key)
{
    return key && key->m_data && key->m_data->m_isValid;
}

bool XPixmapCacheKey_equals(const XPixmapCacheKey* a, const XPixmapCacheKey* b)
{
    return a && b && a->m_data == b->m_data;
}

void XPixmapCacheKey_swap(XPixmapCacheKey* a, XPixmapCacheKey* b)
{
    if (!a || !b) return;
    XPixmapCacheKeyData* tmp = a->m_data;
    a->m_data = b->m_data;
    b->m_data = tmp;
}

/* ========== 缓存管理 ========== */

/**
 * @brief      估算像素图大小
 */
static int estimateSize(const XPixmap* pixmap)
{
    int64_t size;
    if (!pixmap) return 1;
    size = (int64_t)XPixmap_width(pixmap) * XPixmap_height(pixmap) * XPixmap_depth(pixmap);
    size /= 8 * 1024;
    if (size < 1) return 1;
    return size > INT_MAX ? INT_MAX : (int)size;
}

static void destroyEntry(XCacheEntry* entry)
{
    if (!entry) return;
    if (entry->m_keyData)
    {
        entry->m_keyData->m_isValid = false;
        XPixmapCacheKeyData_unref(entry->m_keyData);
    }
    XFree_System(entry->m_key);
    XPixmap_deinit_base(&entry->m_pixmap);
    XFree_System(entry);
}

static void unlinkAndDestroy(XCacheEntry** link)
{
    XCacheEntry* entry = *link;
    *link = entry->m_next;
    g_cache.m_cacheSize -= entry->m_size;
    g_cache.m_entryCount--;
    destroyEntry(entry);
}

static void touchEntry(XCacheEntry** link)
{
    XCacheEntry* entry;
    if (link == &g_cache.m_head) return;
    entry = *link;
    *link = entry->m_next;
    entry->m_next = g_cache.m_head;
    g_cache.m_head = entry;
}

/**
 * @brief      移除缓存项，释放空间
 */
static void trimCache()
{
    while (g_cache.m_cacheSize > g_cache.m_cacheLimit && g_cache.m_head)
    {
        XCacheEntry** link = &g_cache.m_head;
        while ((*link)->m_next) link = &(*link)->m_next;
        unlinkAndDestroy(link);
    }
}

/* ========== XPixmapCache API ========== */

int XPixmapCache_cacheLimit() { return g_cache.m_cacheLimit; }

void XPixmapCache_setCacheLimit(int limit)
{
    g_cache.m_cacheLimit = limit;
    trimCache();
}

bool XPixmapCache_find(const char* key, XPixmap* pixmap)
{
    XCacheEntry** link;
    if (!key || !key[0]) return false;
    link = &g_cache.m_head;
    while (*link)
    {
        XCacheEntry* entry = *link;
        if (entry->m_hasStringKey && entry->m_key && strcmp(entry->m_key, key) == 0)
        {
            if (pixmap) XPixmap_copy_base(pixmap, &entry->m_pixmap);
            touchEntry(link);
            return true;
        }
        link = &entry->m_next;
    }
    return false;
}

bool XPixmapCache_findKey(const XPixmapCacheKey* key, XPixmap* pixmap)
{
    XCacheEntry** link;
    if (!XPixmapCacheKey_isValid(key)) return false;
    link = &g_cache.m_head;
    while (*link)
    {
        XCacheEntry* entry = *link;
        if (entry->m_keyData == key->m_data)
        {
            if (pixmap) XPixmap_copy_base(pixmap, &entry->m_pixmap);
            touchEntry(link);
            return true;
        }
        link = &entry->m_next;
    }
    key->m_data->m_isValid = false;
    return false;
}

bool XPixmapCache_insert(const char* key, const XPixmap* pixmap)
{
    int cost;
    if (!key || !key[0] || !pixmap) return false;
    XPixmapCache_remove(key);
    cost = estimateSize(pixmap);
    if (cost > g_cache.m_cacheLimit) return false;
    XCacheEntry* entry = (XCacheEntry*)XMalloc_System(sizeof(XCacheEntry));
    if (!entry) return false;
    memset(entry, 0, sizeof(XCacheEntry));
    entry->m_key = (char*)XMalloc_System(strlen(key) + 1);
    if (!entry->m_key) { XFree_System(entry); return false; }
    strcpy(entry->m_key, key);
    XPixmap_init(&entry->m_pixmap);
    XPixmap_copy_base(&entry->m_pixmap, pixmap);
    entry->m_size = cost;
    entry->m_hasStringKey = true;
    entry->m_next = g_cache.m_head;
    g_cache.m_head = entry;
    g_cache.m_cacheSize += entry->m_size;
    g_cache.m_entryCount++;
    trimCache();
    return true;
}

bool XPixmapCache_insertKey(const XPixmap* pixmap, XPixmapCacheKey* key)
{
    int cost;
    XPixmapCacheKeyData* keyData;
    if (!pixmap || !key) return false;
    cost = estimateSize(pixmap);
    if (cost > g_cache.m_cacheLimit) return false;
    keyData = XPixmapCacheKeyData_create(true);
    if (!keyData) return false;
    XCacheEntry* entry = (XCacheEntry*)XMalloc_System(sizeof(XCacheEntry));
    if (!entry) { XPixmapCacheKeyData_unref(keyData); return false; }
    memset(entry, 0, sizeof(XCacheEntry));
    XPixmap_init(&entry->m_pixmap);
    XPixmap_copy_base(&entry->m_pixmap, pixmap);
    entry->m_keyData = keyData;
    entry->m_size = cost;
    entry->m_next = g_cache.m_head;
    g_cache.m_head = entry;
    g_cache.m_cacheSize += entry->m_size;
    g_cache.m_entryCount++;
    XPixmapCacheKeyData_ref(keyData);
    XPixmapCacheKeyData_unref(key->m_data);
    key->m_data = keyData;
    trimCache();
    return true;
}

bool XPixmapCache_replace(XPixmapCacheKey* key, const XPixmap* pixmap)
{
    if (!XPixmapCacheKey_isValid(key) || !pixmap) return false;
    XPixmapCache_removeKey(key);
    return XPixmapCache_insertKey(pixmap, key);
}

void XPixmapCache_remove(const char* key)
{
    if (!key) return;
    XCacheEntry** pp = &g_cache.m_head;
    while (*pp)
    {
        XCacheEntry* entry = *pp;
        if (entry->m_hasStringKey && entry->m_key && strcmp(entry->m_key, key) == 0)
        {
            *pp = entry->m_next;
            g_cache.m_cacheSize -= entry->m_size;
            g_cache.m_entryCount--;
            destroyEntry(entry);
            return;
        }
        pp = &entry->m_next;
    }
}

void XPixmapCache_removeKey(const XPixmapCacheKey* key)
{
    if (!XPixmapCacheKey_isValid(key)) return;
    XCacheEntry** pp = &g_cache.m_head;
    while (*pp)
    {
        XCacheEntry* entry = *pp;
        if (entry->m_keyData == key->m_data)
        {
            *pp = entry->m_next;
            g_cache.m_cacheSize -= entry->m_size;
            g_cache.m_entryCount--;
            destroyEntry(entry);
            return;
        }
        pp = &entry->m_next;
    }
    key->m_data->m_isValid = false;
}

void XPixmapCache_clear()
{
    XCacheEntry* entry = g_cache.m_head;
    while (entry)
    {
        XCacheEntry* next = entry->m_next;
        destroyEntry(entry);
        entry = next;
    }
    g_cache.m_head = NULL;
    g_cache.m_cacheSize = 0;
    g_cache.m_entryCount = 0;
}
