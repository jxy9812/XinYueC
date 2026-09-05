/******************************************************************************
 * @file       XPixmapCache.c
 * @brief      XPixmapCache 全局像素图缓存类实现（对标 Qt 6.8 QPixmapCache）
 * @author     XinYueC 团队
 * @note       修复项：
 *             1. 新增原子自旋锁，保护缓存内部链表和引用计数；
 *             2. Key 数据引用计数/序列号/有效性改为原子访问；
 *             3. findKey 未命中、removeKey 调用后按 Qt 语义使键失效；
 *             4. replace 按 Qt 6.8 语义先移除旧键，再向调用者绑定新键；
 *             5. 序列号原子分配，哈希不再依赖未初始化的死字段。
 *             6. insert 覆盖旧字符串键时使用内部无锁路径，避免递归上锁。
 ******************************************************************************/
#include "XPixmapCache.h"
#include "XMemory.h"
#include "XCoreApplication.h"
#include "XSync_config.h"
#if XSYNC_ON && XTHREADDATA_ON && XTHREAD_ON
#include "XThread.h"
#include "XThreadData.h"
#endif
#include <string.h>
#include <limits.h>

/*
 * QPixmapCache 的互斥锁只保证内部数据结构不会被并发破坏，并不意味着
 * 公共缓存 API 可以从任意线程调用。Qt 6.8 在每个公共操作前调用
 * qt_pixmapcache_thread_test()，非主线程的查找、插入、限制、移除和清空
 * 都会被忽略。本地 XThreadData 已经提供了同样的主线程标识，因此这里
 * 只通过 XCore/XSync 抽象调用，避免把平台线程 API 泄漏到 XGui。
 */
static bool cacheThreadAllowed(void)
{
#if XSYNC_ON && XTHREADDATA_ON && XTHREAD_ON
    if (XThread_isMainThread())
        return true;

    /*
     * 与 Qt 的 QThreadData::current(true) 保持一致：没有应用对象时，
     * 第一次在当前线程取得线程数据的线程会成为 adopted main thread。
     * 这使得不创建 XCoreApplication 的独立 XGui 调用仍可正常工作；
     * 一旦主线程数据已经建立，工作线程不会再被提升为主线程。
     */
    if (!XThreadData_mainThread())
    {
        XThreadData* current = XThreadData_current();
        if (current)
        {
            XThreadData_initMainThread(NULL);
            return XThread_isMainThread();
        }
    }
    return false;
#else
    /* 关闭线程对象或同步线程数据后，库退化为单线程模型。 */
    return true;
#endif
}

/* ========== 缓存保护锁 ========== */

/**
 * @brief      缓存原子自旋锁
 * @note       使用 XAtomic 提供的 CAS 原语实现，不依赖平台互斥 API，
 *             保证裁剪 XSync 后仍可用，并保持嵌入式友好。
 */
typedef struct XPixmapCacheLock
{
    XAtomic_bool m_state;       /**< 锁状态：false=未锁定，true=已锁定 */
}XPixmapCacheLock;

static XPixmapCacheLock g_cacheLock = { { false } };

/**
 * @brief      获取缓存锁（原子自旋等待）
 * @note       临界区均为有界操作，自旋等待可保证嵌入式环境的确定性与安全性。
 */
static void cacheLockAcquire(void)
{
    for (;;)
    {
        bool expected = false;
        if (XAtomic_compare_exchange_strong_bool(&g_cacheLock.m_state, &expected, true,
                                                 XAtomic_MemoryOrder_AcqRel,
                                                 XAtomic_MemoryOrder_Acquire))
            return;
        XAtomic_memory_barrier_acquire();
    }
}

/**
 * @brief      释放缓存锁
 */
static void cacheLockRelease(void)
{
    XAtomic_store_bool(&g_cacheLock.m_state, false, XAtomic_MemoryOrder_Release);
}

/* ========== 缓存数据结构 ========== */

/**
 * @brief      缓存条目
 */
typedef struct XCacheEntry
{
    XString*         m_key;       /**< 字符串键（可为 NULL） */
    XPixmapCacheKeyData* m_keyData; /**< 共享 Key 数据（可为 NULL） */
    XPixmap          m_pixmap;    /**< 缓存的像素图 */
    struct XCacheEntry* m_next;   /**< 链表下一项 */
    int              m_size;      /**< 缓存项大小估计（KB） */
    bool             m_hasStringKey; /**< 是否有字符串键 */
}XCacheEntry;

/**
 * @brief      全局缓存状态
 * @note       g_cache 与 g_cacheLock 配套使用：任何对缓存的
 *             m_head/m_cacheSize/m_entryCount/m_cacheLimit 的读写都必须在
 *             cacheLockAcquire/cacheLockRelease 之间完成。
 */
static struct
{
    XCacheEntry* m_head;      /**< 链表头（LRU 头部 = 最近使用） */
    int64_t      m_cacheSize; /**< 当前缓存总大小（估计值，KB） */
    int          m_cacheLimit;/**< 缓存大小限制（KB）；0 = 禁用缓存 */
    int          m_entryCount;/**< 条目数量 */
} g_cache = {NULL, 0, 10240, 0};  // 默认限制 10MB

/* ========== XPixmapCacheKey 实现 ========== */

/**
 * @brief      XPixmapCacheKey 私有数据
 * @note       引用计数、序列号与有效性均为原子变量，可跨线程安全访问；
 *             缓存操作在持有缓存锁时修改有效性，键用户可无锁读取。
 */
typedef struct XPixmapCacheKeyData
{
    XAtomic_int32_t  m_refCount;    /**< 引用计数（缓存项持有 1 份 + 每个键持有 1 份） */
    XAtomic_uint32_t m_serial;      /**< 全局唯一序列号（原子分配，用于哈希） */
    XAtomic_bool     m_isValid;     /**< 是否有效（原子访问） */
}XPixmapCacheKeyData;

/** @brief 全局键序列号计数器（原子分配，保证多线程唯一性）。 */
static XAtomic_uint32_t g_keySerialCounter = { 1u };

/**
 * @brief      原子分配下一个键序列号（0 不会被分配）
 */
static uint32_t nextKeySerial(void)
{
    uint32_t serial;
    do
    {
        serial = XAtomic_fetch_add_uint32(&g_keySerialCounter, 1u,
                                          XAtomic_MemoryOrder_Relaxed);
    }
    while (serial == 0u);
    return serial;
}

/**
 * @brief      创建键数据对象
 * @param valid 初始有效性
 * @return 新键数据指针；失败返回 NULL
 */
static XPixmapCacheKeyData* XPixmapCacheKeyData_create(bool valid)
{
    XPixmapCacheKeyData* d = (XPixmapCacheKeyData*)XMalloc_System(sizeof(XPixmapCacheKeyData));
    if (!d) return NULL;
    memset(d, 0, sizeof(XPixmapCacheKeyData));
    XAtomic_init(d->m_refCount, 1);
    XAtomic_init(d->m_serial, nextKeySerial());
    XAtomic_init(d->m_isValid, valid);
    return d;
}

/** @brief 增加键数据引用计数。 */
static void XPixmapCacheKeyData_ref(XPixmapCacheKeyData* d)
{
    if (d) XAtomic_fetch_add_int32(&d->m_refCount, 1, XAtomic_MemoryOrder_SeqCst);
}

/** @brief 减少键数据引用计数，归零时释放。 */
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
    return key && key->m_data &&
           XAtomic_load_bool(&key->m_data->m_isValid, XAtomic_MemoryOrder_Acquire);
}

/** @brief 使键数据失效（原子写入；在缓存锁保护下调用以保证互斥）。 */
static void XPixmapCacheKeyData_invalidate(XPixmapCacheKeyData* data)
{
    if (data)
        XAtomic_store_bool(&data->m_isValid, false, XAtomic_MemoryOrder_Release);
}

bool XPixmapCacheKey_equals(const XPixmapCacheKey* a, const XPixmapCacheKey* b)
{
    return a && b && a->m_data == b->m_data;
}

uint64_t XPixmapCacheKey_hash(const XPixmapCacheKey* key)
{
    uint64_t value;
    if (!XPixmapCacheKey_isValid(key)) return 0;
    value = (uint64_t)XAtomic_load_uint32(&key->m_data->m_serial,
                                          XAtomic_MemoryOrder_Relaxed);
    value ^= 0x9e3779b97f4a7c15ULL + (value << 6) + (value >> 2);
    return value;
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
 * @brief      估算像素图大小（KB）
 * @note       1 KB = 1024 字节；至少返回 1，保证零尺寸/空像素图也能计入开销。
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

/**
 * @brief      释放缓存条目资源（调用方需已持有缓存锁或处于单线程初始化）
 * @note       释放字符串键与像素图；共享 Key 数据先标记失效再解除
 *             缓存项持有的引用（用户键持有的引用由调用方负责释放）。
 */
static void destroyEntry(XCacheEntry* entry)
{
    if (!entry) return;
    if (entry->m_keyData)
    {
        XPixmapCacheKeyData_invalidate(entry->m_keyData);
        XPixmapCacheKeyData_unref(entry->m_keyData);
    }
    if (entry->m_key) XString_delete_base((XClass*)entry->m_key);
    XPixmap_deinit_base(&entry->m_pixmap);
    XFree_System(entry);
}

/**
 * @brief      从链表中摘除并销毁条目（调用方需已持有缓存锁）
 * @param link 指向待摘除条目前置链接的指针
 */
static void unlinkAndDestroy(XCacheEntry** link)
{
    XCacheEntry* entry = *link;
    *link = entry->m_next;
    g_cache.m_cacheSize -= entry->m_size;
    g_cache.m_entryCount--;
    destroyEntry(entry);
}

/**
 * @brief      将命中条目移动到链表头部（标记最近使用，调用方需已持有缓存锁）
 * @param link 指向命中条目前置链接的指针
 */
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
 * @brief      按 LRU 顺序移除链表尾部条目，直到总开销不超限制（调用方需已持有缓存锁）
 */
static void trimCache(void)
{
    while (g_cache.m_cacheSize > g_cache.m_cacheLimit && g_cache.m_head)
    {
        XCacheEntry** link = &g_cache.m_head;
        while ((*link)->m_next) link = &(*link)->m_next;
        unlinkAndDestroy(link);
    }
}

/**
 * @brief      内部移除：通过字符串键移除首个匹配条目（调用方需已持有缓存锁）
 */
static void removeByStringLocked(const XString* key)
{
    XCacheEntry** pp;
    if (!key || XString_isEmpty_base((const XContainer*)key)) return;
    pp = &g_cache.m_head;
    while (*pp)
    {
        XCacheEntry* entry = *pp;
        if (entry->m_hasStringKey && entry->m_key &&
            XString_equals(entry->m_key, key, XChar_CaseSensitive))
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

/* ========== XPixmapCache API ========== */

int XPixmapCache_cacheLimit(void)
{
    int limit;
    /* 对标 qpixmapcache.cpp:529-534：非主线程查询返回 0，且不触碰缓存。 */
    if (!cacheThreadAllowed())
        return 0;
    cacheLockAcquire();
    limit = g_cache.m_cacheLimit;
    cacheLockRelease();
    return limit;
}

void XPixmapCache_setCacheLimit(int limit)
{
    /* 对标 qpixmapcache.cpp:544-549：非主线程设置被忽略。负值本身不归零，
     * QCache 会保留该值并拒绝所有正开销条目。 */
    if (!cacheThreadAllowed())
        return;
    cacheLockAcquire();
    g_cache.m_cacheLimit = limit;
    trimCache();
    cacheLockRelease();
}

bool XPixmapCache_find(const XString* key, XPixmap* pixmap)
{
    XCacheEntry** link;
    bool found = false;
    /* 对标 qpixmapcache.cpp:426-434：线程检查位于键检查之前。 */
    if (!cacheThreadAllowed())
        return false;
    if (!key || XString_isEmpty_base((const XContainer*)key)) return false;
    cacheLockAcquire();
    link = &g_cache.m_head;
    while (*link)
    {
        XCacheEntry* entry = *link;
        if (entry->m_hasStringKey && entry->m_key &&
            XString_equals(entry->m_key, key, XChar_CaseSensitive))
        {
            if (pixmap) XPixmap_copy_base(pixmap, &entry->m_pixmap);
            touchEntry(link);
            found = true;
            break;
        }
        link = &entry->m_next;
    }
    cacheLockRelease();
    return found;
}

bool XPixmapCache_find_2(const char* key, XPixmap* pixmap)
{
    if (!cacheThreadAllowed())
        return false;
    XString* value = key ? XString_create_utf8(key) : NULL;
    bool result = XPixmapCache_find(value, pixmap);
    if (value) XString_delete_base((XClass*)value);
    return result;
}

bool XPixmapCache_findKey(const XPixmapCacheKey* key, XPixmap* pixmap)
{
    XCacheEntry** link;
    bool found = false;
    /* 非主线程必须在检查 Key 有效性之前返回；因此不会因线程限制而
     * 意外使调用者持有的有效 Key 失效。 */
    if (!cacheThreadAllowed())
        return false;
    if (!XPixmapCacheKey_isValid(key)) return false;
    cacheLockAcquire();
    link = &g_cache.m_head;
    while (*link)
    {
        XCacheEntry* entry = *link;
        if (entry->m_keyData == key->m_data)
        {
            if (pixmap) XPixmap_copy_base(pixmap, &entry->m_pixmap);
            touchEntry(link);
            found = true;
            break;
        }
        link = &entry->m_next;
    }
    if (!found)
    {
        /* 对标 Qt：缓存项已不存在时 Key 立即失效（所有副本共享同一数据） */
        XPixmapCacheKeyData_invalidate(key->m_data);
    }
    cacheLockRelease();
    return found;
}

bool XPixmapCache_insert(const XString* key, const XPixmap* pixmap)
{
    int cost;
    XCacheEntry* entry;
    if (!cacheThreadAllowed())
        return false;
    if (!key || XString_isEmpty_base((const XContainer*)key) || !pixmap) return false;
    cacheLockAcquire();
    removeByStringLocked(key);   /* 同键覆盖：先移除旧项（避免重复节点） */
    cost = estimateSize(pixmap);
    if (cost <= 0 || cost > g_cache.m_cacheLimit)
    {
        cacheLockRelease();
        return false;
    }
    entry = (XCacheEntry*)XMalloc_System(sizeof(XCacheEntry));
    if (!entry) { cacheLockRelease(); return false; }
    memset(entry, 0, sizeof(XCacheEntry));
    entry->m_key = XString_create_copy(key);
    if (!entry->m_key) { XFree_System(entry); cacheLockRelease(); return false; }
    /* 条目已清零；copy 基类会初始化目标并取得共享像素数据引用。 */
    XPixmap_copy_base(&entry->m_pixmap, pixmap);
    entry->m_size = cost;
    entry->m_hasStringKey = true;
    entry->m_next = g_cache.m_head;
    g_cache.m_head = entry;
    g_cache.m_cacheSize += entry->m_size;
    g_cache.m_entryCount++;
    trimCache();
    cacheLockRelease();
    return true;
}

bool XPixmapCache_insert_2(const char* key, const XPixmap* pixmap)
{
    if (!cacheThreadAllowed())
        return false;
    XString* value = key ? XString_create_utf8(key) : NULL;
    bool result = XPixmapCache_insert(value, pixmap);
    if (value) XString_delete_base((XClass*)value);
    return result;
}

bool XPixmapCache_insertKey(const XPixmap* pixmap, XPixmapCacheKey* key)
{
    int cost;
    XPixmapCacheKeyData* keyData;
    XCacheEntry* entry;
    if (!cacheThreadAllowed() || !pixmap || !key) return false;
    cacheLockAcquire();
    /* 若该 Key 已关联缓存项，先移除旧项，避免复用键插入产生孤儿节点。
     * 注意：仅当 Key 当前持有非空数据（复用旧键）时才需摘除旧条目；
     * 全新/已失效的 Key 其 m_data 为 NULL，而字符串键条目同样以 NULL
     * 作为 m_keyData，若不判空会误删无关的字符串键条目。 */
    if (key->m_data)
    {
        XCacheEntry** oldLink = &g_cache.m_head;
        while (*oldLink)
        {
            if ((*oldLink)->m_keyData == key->m_data)
            {
                unlinkAndDestroy(oldLink);
                break;
            }
            oldLink = &(*oldLink)->m_next;
        }
    }
    cost = estimateSize(pixmap);
    if (cost <= 0 || cost > g_cache.m_cacheLimit)
    {
        cacheLockRelease();
        return false;
    }
    keyData = XPixmapCacheKeyData_create(true);
    if (!keyData) { cacheLockRelease(); return false; }
    entry = (XCacheEntry*)XMalloc_System(sizeof(XCacheEntry));
    if (!entry)
    {
        XPixmapCacheKeyData_unref(keyData);
        cacheLockRelease();
        return false;
    }
    memset(entry, 0, sizeof(XCacheEntry));
    /* 条目已清零；copy 基类会初始化目标并取得共享像素数据引用。 */
    XPixmap_copy_base(&entry->m_pixmap, pixmap);
    entry->m_keyData = keyData;
    entry->m_size = cost;
    entry->m_next = g_cache.m_head;
    g_cache.m_head = entry;
    g_cache.m_cacheSize += entry->m_size;
    g_cache.m_entryCount++;
    /* Key 取得一份独立引用；旧键引用被释放（空键时为 NULL 无操作） */
    XPixmapCacheKeyData_ref(keyData);
    XPixmapCacheKeyData_unref(key->m_data);
    key->m_data = keyData;
    trimCache();
    cacheLockRelease();
    return true;
}

bool XPixmapCache_replace(XPixmapCacheKey* key, const XPixmap* pixmap)
{
    /*
     * Qt 6.8 的 replace 是头文件内联兼容接口（qpixmapcache.h:62-69）：
     * 先 remove(key)，再将 insert(pixmap) 生成的新 Key 赋回调用者。旧
     * KeyData 会在 remove 时失效，所以 key 的所有旧副本也会失效；只有
     * 调用者传入的这个 Key 在插入成功后重新绑定到新的 KeyData。这样
     * replace 的行为与“remove 后重新 insert”完全一致，也正确处理新
     * 像素图超出限制或内存分配失败的情况（key 最终保持无效）。
     */
    if (!cacheThreadAllowed() || !XPixmapCacheKey_isValid(key) || !pixmap)
        return false;
    XPixmapCache_removeKey(key);
    if (!XPixmapCache_insertKey(pixmap, key))
        return false;
    return XPixmapCacheKey_isValid(key);
}

void XPixmapCache_remove(const XString* key)
{
    /* 对标 qpixmapcache.cpp:554-559：非主线程移除请求被忽略。 */
    if (!cacheThreadAllowed())
        return;
    if (!key || XString_isEmpty_base((const XContainer*)key)) return;
    cacheLockAcquire();
    removeByStringLocked(key);
    cacheLockRelease();
}

void XPixmapCache_remove_2(const char* key)
{
    if (!cacheThreadAllowed())
        return;
    XString* value = key ? XString_create_utf8(key) : NULL;
    XPixmapCache_remove(value);
    if (value) XString_delete_base((XClass*)value);
}

void XPixmapCache_removeKey(const XPixmapCacheKey* key)
{
    XCacheEntry** pp;
    /* 线程门控必须先于 Key 有效性检查，保持 Qt 非主线程的无副作用语义。 */
    if (!cacheThreadAllowed())
        return;
    if (!XPixmapCacheKey_isValid(key)) return;
    cacheLockAcquire();
    pp = &g_cache.m_head;
    while (*pp)
    {
        XCacheEntry* entry = *pp;
        if (entry->m_keyData == key->m_data)
        {
            *pp = entry->m_next;
            g_cache.m_cacheSize -= entry->m_size;
            g_cache.m_entryCount--;
            destroyEntry(entry);
            break;
        }
        pp = &entry->m_next;
    }
    /* 对标 Qt：remove 调用后键立即失效（无论是否找到对应项） */
    XPixmapCacheKeyData_invalidate(key->m_data);
    cacheLockRelease();
}

void XPixmapCache_clear(void)
{
    XCacheEntry* entry;
    /* 对标 qpixmapcache.cpp:574-587：正常运行期间仅主线程可清空；
     * 应用进入 closingDown 阶段后，Qt 允许清理线程不再满足主线程检查。 */
    if (!XCoreApplication_closingDown() && !cacheThreadAllowed())
        return;
    cacheLockAcquire();
    entry = g_cache.m_head;
    while (entry)
    {
        XCacheEntry* next = entry->m_next;
        destroyEntry(entry);
        entry = next;
    }
    g_cache.m_head = NULL;
    g_cache.m_cacheSize = 0;
    g_cache.m_entryCount = 0;
    cacheLockRelease();
}
