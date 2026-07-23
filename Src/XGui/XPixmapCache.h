/******************************************************************************
 * @file       XPixmapCache.h
 * @brief      XPixmapCache 全局像素图缓存类（对标 Qt 6.8 QPixmapCache）
 * @author     XinYueC 团队
 * @note       提供全局像素图缓存功能，支持通过字符串键或 Key 对象管理缓存项
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

/* 前向声明 */
typedef struct XPixmapCacheKeyData XPixmapCacheKeyData;

/**
 * @brief      XPixmapCache 缓存键类（对标 Qt 6.8 QPixmapCache::Key）
 * @note       用于标识缓存中的像素图项，支持比较和哈希操作
 */
typedef struct XPixmapCacheKey
{
    XPixmapCacheKeyData* m_data;   /**< 键数据指针 */
}XPixmapCacheKey;

/**
 * @brief      初始化缓存键
 * @param key 待初始化的缓存键指针
 */
void XPixmapCacheKey_init(XPixmapCacheKey* key);

/**
 * @brief      复制缓存键
 * @param dest 目标缓存键指针
 * @param src  源缓存键指针
 */
void XPixmapCacheKey_copy(XPixmapCacheKey* dest, const XPixmapCacheKey* src);

/**
 * @brief      释放缓存键资源
 * @param key 待释放的缓存键指针
 */
void XPixmapCacheKey_deinit(XPixmapCacheKey* key);

/**
 * @brief      判断缓存键是否有效
 * @param key 缓存键指针
 * @return 有效返回 true
 */
bool XPixmapCacheKey_isValid(const XPixmapCacheKey* key);

/**
 * @brief      判断两个缓存键是否相等
 * @param a 缓存键 A 指针
 * @param b 缓存键 B 指针
 * @return 相等返回 true
 */
bool XPixmapCacheKey_equals(const XPixmapCacheKey* a, const XPixmapCacheKey* b);

/**
 * @brief      交换两个缓存键
 * @param a 缓存键 A 指针
 * @param b 缓存键 B 指针
 */
void XPixmapCacheKey_swap(XPixmapCacheKey* a, XPixmapCacheKey* b);

/**
 * @brief      XPixmapCache 全局像素图缓存类（对标 Qt 6.8 QPixmapCache）
 * @note       所有方法均为静态函数，提供全局缓存管理
 */

/**
 * @brief      获取缓存大小限制（KB）
 * @return 缓存大小限制（KB）
 */
int XPixmapCache_cacheLimit();

/**
 * @brief      设置缓存大小限制（KB）
 * @param limit 缓存大小限制（KB）
 */
void XPixmapCache_setCacheLimit(int limit);

/**
 * @brief      通过字符串键查找缓存中的像素图
 * @param key    字符串键
 * @param pixmap 输出像素图指针（找到时填充）
 * @return 找到返回 true
 */
bool XPixmapCache_find(const char* key, XPixmap* pixmap);

/**
 * @brief      通过 Key 对象查找缓存中的像素图
 * @param key    缓存键指针
 * @param pixmap 输出像素图指针（找到时填充）
 * @return 找到返回 true
 */
bool XPixmapCache_findKey(const XPixmapCacheKey* key, XPixmap* pixmap);

/**
 * @brief      通过字符串键插入像素图到缓存
 * @param key    字符串键
 * @param pixmap 待缓存的像素图指针
 * @return 插入成功返回 true
 */
bool XPixmapCache_insert(const char* key, const XPixmap* pixmap);

/**
 * @brief      插入像素图到缓存并返回 Key 对象
 * @param pixmap 待缓存的像素图指针
 * @param key    输出缓存键指针
 * @return 插入成功返回 true
 */
bool XPixmapCache_insertKey(const XPixmap* pixmap, XPixmapCacheKey* key);

/**
 * @brief      替换缓存中的像素图（通过 Key 对象）
 * @param key    缓存键指针（更新后指向新插入的项）
 * @param pixmap 新的像素图指针
 * @return 替换成功返回 true
 */
bool XPixmapCache_replace(XPixmapCacheKey* key, const XPixmap* pixmap);

/**
 * @brief      通过字符串键移除缓存项
 * @param key 字符串键
 */
void XPixmapCache_remove(const char* key);

/**
 * @brief      通过 Key 对象移除缓存项
 * @param key 缓存键指针
 */
void XPixmapCache_removeKey(const XPixmapCacheKey* key);

/**
 * @brief      清空所有缓存项
 */
void XPixmapCache_clear();

#ifdef __cplusplus
}
#endif
#endif /* XPIXMAPCACHE_H */

