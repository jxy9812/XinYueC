/******************************************************************************
 * @file       XImageCache.c
 * @brief      解码图像字节预算 LRU 缓存实现（对标 LVGL lv_image_cache /
 *             Qt QPixmapCache 的资源受限设备纪律）。
 * @details    以「路径 + 格式名」为键缓存已解码位图；条目存放在静态数组
 *             池中（上限 XIMAGECACHE_MAX_ENTRIES，#ifndef 兜底 32，编译期
 *             可覆写），用双向链表维护 LRU 顺序（链头 = 最近使用，链尾 =
 *             最旧，淘汰自链尾开始）：
 *             - 字节成本按 XImageFormat_bytesPerLine(w, format) * h +
 *               条目簿记估算；单条目超预算整体拒绝；预算不足或条目池满
 *               时从链尾淘汰直至放得下，淘汰条目经 XImage_deinit_base
 *               释放共享位图数据；
 *             - 键为 path '\n' format（format NULL 记空串），总长超
 *               XIMAGECACHE_KEY_MAX 直接不缓存；
 *             - 命中出参经 XCopy 浅共享缓存条目（XImage 为引用计数的
 *               COW 对象，调用方写入会自动分离，不污染缓存），禁止
 *               深拷贝；命中同时把条目提升到链头；
 *             - XIMAGECACHE_MAX_BYTES=0（默认）时全部操作经 #if 门控
 *               编译期退化为直通：lookup 恒未命中、insert/invalidate/
 *               clear 恒 no-op，静态池与链表完全不存在，零运行时开销。
 * @note       线程纪律：GUI 主线程专用，无锁（与 painter 静态缓存同纪
 *             律）；下方全部静态状态只允许在 GUI 主线程访问，不加任何
 *             同步原语。
 * @author     XinYueC 团队
 ******************************************************************************/
#include "XImageCache.h"

#if XIMAGECACHE_ON && XIMAGECACHE_MAX_BYTES > 0

#include "XImage.h"
#include "XMemory.h"
#include <string.h>
/* ========== 配置与数据结构 ========== */

/** @brief 条目池上限；编译期可用 -DXIMAGECACHE_MAX_ENTRIES 覆写。 */
#ifndef XIMAGECACHE_MAX_ENTRIES
#define XIMAGECACHE_MAX_ENTRIES 32
#endif

/**
 * @brief      缓存条目（静态池元素，地址稳定，可被双向链表安全引用）。
 */
typedef struct XImageCacheEntry
{
    bool     m_inUse;                    /**< 是否在用 */
    char     m_key[XIMAGECACHE_KEY_MAX]; /**< 缓存键：path '\n' format */
    XImage   m_image;                    /**< 缓存的解码位图（引用计数共享） */
    size_t   m_bytes;                    /**< 本条目字节成本估算（含簿记） */
    struct XImageCacheEntry* m_prev;     /**< LRU 前驱（更近使用端） */
    struct XImageCacheEntry* m_next;     /**< LRU 后继（更旧端） */
}XImageCacheEntry;

/**
 * @brief      缓存全部静态状态。
 * @note       GUI 主线程专用，无锁；任何读写都必须发生在 GUI 主线程。
 */
static XImageCacheEntry g_pool[XIMAGECACHE_MAX_ENTRIES]; /**< 静态条目池 */
static XImageCacheEntry* g_lruHead = NULL;  /**< 链头（最近使用） */
static XImageCacheEntry* g_lruTail = NULL;  /**< 链尾（最旧，淘汰起点） */
static size_t g_totalBytes = 0;             /**< 当前字节占用（含簿记） */
static int    g_entryCount = 0;             /**< 当前条目数 */

/* ========== 内部工具 ========== */

/** @brief 编译期预算转运行期值（XIMAGECACHE_MAX_BYTES 可为括号表达式）。 */
static size_t cacheBudget(void)
{
    return (size_t)(XIMAGECACHE_MAX_BYTES);
}

/**
 * @brief      组键：path '\n' format（format NULL 记空串）。
 * @param      out 输出键缓冲区（容量 XIMAGECACHE_KEY_MAX）。
 * @return     键长度（不含结束符）；path NULL/空或总长超限返回 -1。
 */
static int buildKey(const char* path, const char* format,
                    char out[XIMAGECACHE_KEY_MAX])
{
    size_t pathLen;
    size_t formatLen;
    if (!path || !path[0]) return -1;
    pathLen = strlen(path);
    formatLen = format ? strlen(format) : 0;
    /* 需要容纳 path + '\n' + format + '\0'。 */
    if (pathLen > (size_t)(XIMAGECACHE_KEY_MAX - 2)) return -1;
    if (formatLen > (size_t)(XIMAGECACHE_KEY_MAX - 2) - pathLen) return -1;
    if (pathLen > 0) XMemcpy(out, path, pathLen);
    out[pathLen] = '\n';
    if (formatLen > 0) XMemcpy(out + pathLen + 1, format, formatLen);
    out[pathLen + 1 + formatLen] = '\0';
    return (int)(pathLen + 1 + formatLen);
}

/** @brief 按键线性查条目（条目上限 32，线性扫描即可，无哈希依赖）。 */
static XImageCacheEntry* findEntry(const char* key)
{
    int i;
    for (i = 0; i < XIMAGECACHE_MAX_ENTRIES; ++i)
    {
        if (g_pool[i].m_inUse && strcmp(g_pool[i].m_key, key) == 0)
            return &g_pool[i];
    }
    return NULL;
}

/** @brief 从双向链表摘除条目（不释放资源）。 */
static void unlinkEntry(XImageCacheEntry* entry)
{
    if (entry->m_prev) entry->m_prev->m_next = entry->m_next;
    else               g_lruHead = entry->m_next;
    if (entry->m_next) entry->m_next->m_prev = entry->m_prev;
    else               g_lruTail = entry->m_prev;
    entry->m_prev = NULL;
    entry->m_next = NULL;
}

/** @brief 头插（标记为最近使用）。 */
static void pushFront(XImageCacheEntry* entry)
{
    entry->m_prev = NULL;
    entry->m_next = g_lruHead;
    if (g_lruHead) g_lruHead->m_prev = entry;
    g_lruHead = entry;
    if (!g_lruTail) g_lruTail = entry;
}

/**
 * @brief      销毁条目：移出链表 + XImage_deinit_base 释放共享位图数据。
 * @note       引用计数归零时像素内存随之释放；他人持有的浅共享副本不受
 *             影响（COW 保证其继续可用）。
 */
static void destroyEntry(XImageCacheEntry* entry)
{
    unlinkEntry(entry);
    g_totalBytes -= entry->m_bytes;
    g_entryCount--;
    XImage_deinit_base(&entry->m_image);
    entry->m_inUse = false;
    entry->m_key[0] = '\0';
    entry->m_bytes = 0;
}

/** @brief 估算条目字节成本：bytesPerLine(w, format) * h + 条目簿记。 */
static size_t entryCost(const XImage* image)
{
    int64_t bytes;
    int bytesPerLine = XImageFormat_bytesPerLine(XImage_width(image),
                                                 XImage_format(image));
    bytes = (int64_t)bytesPerLine * (int64_t)XImage_height(image)
            + (int64_t)sizeof(XImageCacheEntry);
    if (bytes < 0) return 0;
    return (size_t)bytes;
}

/** @brief 取一个空闲条目（线性扫池）；池满返回 NULL。 */
static XImageCacheEntry* acquireFreeSlot(void)
{
    int i;
    for (i = 0; i < XIMAGECACHE_MAX_ENTRIES; ++i)
    {
        if (!g_pool[i].m_inUse) return &g_pool[i];
    }
    return NULL;
}

/* ========== 公共 API ========== */

bool XImageCache_lookup(const char* path, const char* format, XImage* out)
{
    char key[XIMAGECACHE_KEY_MAX];
    XImageCacheEntry* entry;
    /* NULL/空路径或超长键：视为未命中。 */
    if (buildKey(path, format, key) < 0) return false;
    entry = findEntry(key);
    if (!entry) return false;
    /* 浅共享出参：XCopy 走虚表拷贝，仅对缓存条目位图引用计数 +1
     * （XImage.c VXImage_copy），不复制像素，调用方写入经 COW 分离。 */
    if (out) XCopy(out, &entry->m_image);
    /* 命中即提升为最近使用：移到链头。 */
    if (g_lruHead != entry)
    {
        unlinkEntry(entry);
        pushFront(entry);
    }
    return true;
}

void XImageCache_insert(const char* path, const char* format,
                        const XImage* image)
{
    char key[XIMAGECACHE_KEY_MAX];
    const size_t budget = cacheBudget();
    XImageCacheEntry* slot;
    size_t cost;
    if (!image || XImage_isNull(image)) return;  /* 只登记成功解码的位图 */
    if (buildKey(path, format, key) < 0) return; /* 超长键直接不缓存 */
    cost = entryCost(image);
    if (cost > budget) return;                   /* 单条目超预算：整体拒绝 */
    /* 键重复时替换旧条目：先摘除旧项，再按新条目走常规插入。 */
    slot = findEntry(key);
    if (slot) destroyEntry(slot);
    /* 预算不足或条目池满：从链尾（最旧端）淘汰到放得下。 */
    while (g_entryCount > 0 &&
           (g_totalBytes + cost > budget || !acquireFreeSlot()))
    {
        destroyEntry(g_lruTail);
    }
    slot = acquireFreeSlot();
    if (!slot) return;  /* 防御性兜底：池全空仍无槽位属配置异常 */
    XMemcpy(slot->m_key, key, strlen(key) + 1);
    /* 条目槽已清零；XCopy 走虚表完成首次 init 并取得共享数据引用，
     * 调用方之后可随意处置自己的副本（解引用由引用计数守护）。 */
    XCopy(&slot->m_image, image);
    slot->m_bytes = cost;
    slot->m_inUse = true;
    pushFront(slot);
    g_totalBytes += cost;
    g_entryCount++;
}

void XImageCache_invalidate(const char* path, const char* format)
{
    char key[XIMAGECACHE_KEY_MAX];
    XImageCacheEntry* entry;
    if (buildKey(path, format, key) < 0) return;
    entry = findEntry(key);
    if (entry) destroyEntry(entry);  /* 未命中安全 no-op */
}

void XImageCache_clear(void)
{
    while (g_lruTail) destroyEntry(g_lruTail);
}

void XImageCache_stats(size_t* outBytes, int* outCount)
{
    if (outBytes) *outBytes = g_totalBytes;
    if (outCount) *outCount = g_entryCount;
}

#else /* XIMAGECACHE_MAX_BYTES <= 0（预算 0）或 XIMAGECACHE_ON=0（模块
         裁剪）：两条路径共用同一份直通空实现——lookup 恒未命中、其余
         恒 no-op。空实现无条件保留，保证裁剪态下 XImage.c 的无条件
         调用有外部链接定义可链（gcc ≥14 对隐式声明硬报错）。静态池、
         链表与工具函数一律不生成，零运行时开销。 */

bool XImageCache_lookup(const char* path, const char* format, XImage* out)
{
    (void)path; (void)format; (void)out;
    return false;  /* 恒未命中 */
}

void XImageCache_insert(const char* path, const char* format,
                        const XImage* image)
{
    (void)path; (void)format; (void)image;  /* 恒忽略 */
}

void XImageCache_invalidate(const char* path, const char* format)
{
    (void)path; (void)format;  /* 恒 no-op */
}

void XImageCache_clear(void)
{
}

void XImageCache_stats(size_t* outBytes, int* outCount)
{
    if (outBytes) *outBytes = 0;
    if (outCount) *outCount = 0;
}

#endif /* XIMAGECACHE_ON && XIMAGECACHE_MAX_BYTES > 0 */
