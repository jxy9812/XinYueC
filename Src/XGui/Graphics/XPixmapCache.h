/******************************************************************************
 * @file       XPixmapCache.h
 * @brief      XPixmapCache 全局像素图缓存类（对标 Qt 6.8 QPixmapCache）
 * @author     XinYueC 团队
 * @note       提供全局像素图缓存功能，支持通过字符串键或 Key 对象管理缓存项。
 *             缓存采用 LRU（最近最少使用）淘汰策略。内部链表由原子自旋锁
 *             保护，但和 Qt 6.8 一样，公共缓存操作只在主线程生效；工作线程
 *             的查找、插入、替换、限制、移除和清空都会被忽略。Key 的引用计数
 *             与有效性标记使用原子操作，允许安全地观察生命周期状态。
 ******************************************************************************/
#ifndef XPIXMAPCACHE_H
#define XPIXMAPCACHE_H
#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>
#include "XPixmap.h"
#include "XAtomic.h"
#include "XString.h"

/* 前向声明 */
typedef struct XPixmapCacheKeyData XPixmapCacheKeyData;

/**
 * @brief      XPixmapCache 缓存键类（对标 Qt 6.8 QPixmapCache::Key）
 * @note       用于标识缓存中的像素图项，支持比较和哈希操作。
 *             有效键（isValid）在缓存项被移除、缓存被清空、查找失败
 *             （findKey 未命中）或调用 removeKey 后失效；所有键副本
 *             共享同一份键数据，因此一个副本失效会影响全部副本。
 *             键对象仅持有引用，不拥有缓存项本身，必须调用
 *             XPixmapCacheKey_deinit 释放引用。
 */
typedef struct XPixmapCacheKey
{
    XPixmapCacheKeyData* m_data;   /**< 键数据指针（共享引用计数管理） */
}XPixmapCacheKey;

/**
 * @brief      初始化缓存键（空键）
 * @param key 待初始化的缓存键指针；NULL 时直接返回
 */
void XPixmapCacheKey_init(XPixmapCacheKey* key);

/**
 * @brief      复制缓存键（增加目标引用的共享键数据）
 * @param dest 目标缓存键指针
 * @param src  源缓存键指针
 * @note       深拷贝引用语义：复制后 dest 与 src 共享同一份键数据，
 *             任何副本失效都会导致所有副本失效。
 */
void XPixmapCacheKey_copy(XPixmapCacheKey* dest, const XPixmapCacheKey* src);

/**
 * @brief      释放缓存键资源（减少共享键数据引用，引用归零时释放）
 * @param key 待释放的缓存键指针
 */
void XPixmapCacheKey_deinit(XPixmapCacheKey* key);

/**
 * @brief      判断缓存键是否有效
 * @param key 缓存键指针
 * @return 有效返回 true
 * @note       使用原子读取，可跨线程安全调用。
 */
bool XPixmapCacheKey_isValid(const XPixmapCacheKey* key);

/**
 * @brief      判断两个缓存键是否相等
 * @param a 缓存键 A 指针
 * @param b 缓存键 B 指针
 * @return 相等返回 true
 * @note       两个键共享同一份键数据时视为相等（指针身份比较）。
 */
bool XPixmapCacheKey_equals(const XPixmapCacheKey* a, const XPixmapCacheKey* b);

/**
 * @brief      获取缓存键哈希值
 * @param key 缓存键指针
 * @return 哈希值；无效键返回 0
 */
uint64_t XPixmapCacheKey_hash(const XPixmapCacheKey* key);

/**
 * @brief      交换两个缓存键（仅交换数据指针，不涉及引用计数）
 * @param a 缓存键 A 指针
 * @param b 缓存键 B 指针
 */
void XPixmapCacheKey_swap(XPixmapCacheKey* a, XPixmapCacheKey* b);

/**
 * @brief      XPixmapCache 全局像素图缓存类（对标 Qt 6.8 QPixmapCache）
 * @note       所有方法均为静态函数，提供全局缓存管理。
 *             缓存结构内部使用原子自旋锁保护，但所有实际缓存 API 均有主线程
 *             限制，与 Qt 6.8 的 QPixmapCache 一致。工作线程调用时，查询返回
 *             失败，设置/移除/清空请求不产生任何效果。
 */

/**
 * @brief      获取缓存大小限制（KB）
 * @return 主线程上的缓存大小限制（KB）；工作线程调用返回 0
 */
int XPixmapCache_cacheLimit();

/**
 * @brief      设置缓存大小限制（KB）
 * @param limit 缓存大小限制（KB）
 * @note       限制为 0 时表示禁用缓存：所有插入请求被拒绝，已有缓存项被清空。
 *             负值按 Qt 语义原样保留，并使所有正开销插入请求失败；设置限制后
 *             立即按 LRU 顺序修剪超出限制的缓存项。工作线程设置会被忽略。
 */
void XPixmapCache_setCacheLimit(int limit);

/**
 * @brief      通过字符串键查找缓存中的像素图
 * @param key    字符串键；NULL 或空串时返回 false
 * @param pixmap 输出像素图指针（找到时填充；可为 NULL 仅用于探测存在性）
 * @return 找到返回 true
 * @note       命中会将缓存项移动到 LRU 链表头部（标记最近使用）。
 */
bool XPixmapCache_find(const XString* key, XPixmap* pixmap);

/** @brief 使用 UTF-8 字符串键查找缓存。 @param key UTF-8 键。 @param pixmap 输出像素图（可为 NULL）。 @return 找到返回 true。 */
bool XPixmapCache_find_2(const char* key, XPixmap* pixmap);

/**
 * @brief      通过 Key 对象查找缓存中的像素图
 * @param key    缓存键指针
 * @param pixmap 输出像素图指针（找到时填充）
 * @return 找到返回 true
 * @note       未命中（缓存项已被淘汰或移除）时该键立即失效：
 *             key 及所有副本的 isValid 变为 false（对标 Qt 行为）。
 */
bool XPixmapCache_findKey(const XPixmapCacheKey* key, XPixmap* pixmap);

/**
 * @brief      通过字符串键插入像素图到缓存
 * @param key    字符串键；NULL、空串或像素图为 NULL 时返回 false
 * @param pixmap 待缓存的像素图指针
 * @return 插入成功返回 true
 * @note       相同字符串键已存在时先移除旧项再插入新项；
 *             若像素图尺寸超过缓存限制则拒绝插入并返回 false。
 */
bool XPixmapCache_insert(const XString* key, const XPixmap* pixmap);

/** @brief 使用 UTF-8 字符串键插入缓存。 @param key UTF-8 键。 @param pixmap 待缓存像素图。 @return 插入成功返回 true。 */
bool XPixmapCache_insert_2(const char* key, const XPixmap* pixmap);

/**
 * @brief      插入像素图到缓存并输出 Key 对象
 * @param pixmap 待缓存的像素图指针
 * @param key    输出缓存键指针（插入成功后有效；旧键引用被正确释放）
 * @return 插入成功返回 true
 */
bool XPixmapCache_insertKey(const XPixmap* pixmap, XPixmapCacheKey* key);

/**
 * @brief      替换缓存中的像素图（通过 Key 对象）
 * @param key    缓存键指针；成功后绑定到新条目的新 KeyData，旧副本失效
 * @param pixmap 新的像素图指针
 * @return 替换成功返回 true
 * @note       对标 Qt 6.8 头文件内联实现：先移除旧条目，使 key 及所有旧副本
 *             失效；再插入新条目并把新 KeyData 绑定到传入 key。若键已失效、
 *             工作线程调用、像素图超过缓存限制或插入失败则返回 false。
 */
bool XPixmapCache_replace(XPixmapCacheKey* key, const XPixmap* pixmap);

/**
 * @brief      通过字符串键移除缓存项
 * @param key 字符串键；NULL 或空串时直接返回
 */
void XPixmapCache_remove(const XString* key);

/** @brief 使用 UTF-8 字符串键移除缓存。 @param key UTF-8 键。 */
void XPixmapCache_remove_2(const char* key);

/**
 * @brief      通过 Key 对象移除缓存项
 * @param key 缓存键指针
 * @note       无论是否找到对应缓存项，调用后该键及所有副本立即失效
 *             （对标 Qt：remove 后键不再有效）。
 */
void XPixmapCache_removeKey(const XPixmapCacheKey* key);

/**
 * @brief      清空所有缓存项
 * @note       清空后所有由缓存持有的 Key 均失效；用户持有的键引用
 *             仍须调用 XPixmapCacheKey_deinit 释放。
 */
void XPixmapCache_clear();

#ifdef __cplusplus
}
#endif
#endif /* XPIXMAPCACHE_H */
