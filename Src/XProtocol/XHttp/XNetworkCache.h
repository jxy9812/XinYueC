/**
 * @file       XNetworkCache.h
 * - @brief      HTTP 网络缓存元数据和磁盘缓存 API，对标 Qt 6.8。
 * - @details    缓存使用 XFile/XIODevice 抽象，不直接调用平台文件 API；缓存条目和
 *             元数据均采用 XinYueC 所有权约定。
 */

#ifndef XNETWORKCACHE_H
#define XNETWORKCACHE_H
#include "XHttp_config.h"

#ifdef __cplusplus
extern "C" {
#endif
#if XPROTOCOL_ON
#if XHTTP_ON

#include "XClass.h"
#include "XHttpHeaders.h"
#include "XObject.h"
#include "XUrl.h"
#include "XVector.h"
#include <stdbool.h>
#include <stdint.h>

XCLASS_DEFINE_BEGING(XNetworkCacheMetaData)
XCLASS_DEFINE_EXTEND_END(XNetworkCacheMetaData, XClass)

/**
 * - @brief 网络缓存元数据值对象。
 * - @details 时间使用 Unix UTC 毫秒；负值表示未设置。所有 URL 和头对象由元数据拥有。
 */
typedef struct XNetworkCacheMetaData {
    XClass m_class;               /**< 第一个成员，继承 XClass。 */
    XUrl* m_url;                  /**< 缓存 URL；对象拥有。 */
    XHttpHeaders* m_headers;      /**< 响应头；对象拥有。 */
    int64_t m_lastModifiedMSecs;  /**< 最后修改时间；负值表示未设置。 */
    int64_t m_expirationMSecs;    /**< 过期时间；负值表示未设置。 */
    bool m_saveToDisk;            /**< 是否允许持久化。 */
    bool m_valid;                 /**< 元数据是否有效。 */
} XNetworkCacheMetaData;

/**
 * - @brief 初始化网络缓存元数据虚函数表。
 * - @return 缓存元数据虚函数表；初始化失败返回 NULL，不由调用者释放。
 */
XVtable* XNetworkCacheMetaData_class_init(void);
/**
 * - @brief 初始化网络缓存元数据。
 * - @param self 待初始化的缓存元数据；不能为 NULL。
 */
void XNetworkCacheMetaData_init(XNetworkCacheMetaData* self);
/**
 * - @brief 创建网络缓存元数据。
 * - @return 新建缓存元数据；调用者必须使用 XNetworkCacheMetaData_delete_base 释放，分配失败返回 NULL。
 */
XNetworkCacheMetaData* XNetworkCacheMetaData_create_ex(XMemoryType memory);
/**
 * - @brief 深拷贝创建网络缓存元数据。
 * - @param other 源缓存元数据；借用且不能为 NULL。
 * - @return 新建缓存元数据；调用者必须使用 XNetworkCacheMetaData_delete_base 释放，参数无效或拷贝失败返回 NULL。
 */
XNetworkCacheMetaData* XNetworkCacheMetaData_create_copy(const XNetworkCacheMetaData* other);
/**
 * - @brief 移动创建网络缓存元数据。
 * - @param other 源缓存元数据；借用且不能为 NULL，成功后保持可释放的已初始化状态。
 * - @return 新建缓存元数据；调用者必须使用 XNetworkCacheMetaData_delete_base 释放，参数无效或分配失败返回 NULL。
 */
XNetworkCacheMetaData* XNetworkCacheMetaData_create_move(XNetworkCacheMetaData* other);

#define XNetworkCacheMetaData_deinit_base XClass_deinit_base
#define XNetworkCacheMetaData_delete_base XClass_delete_base
#define XNetworkCacheMetaData_copy_base XClass_copy_base
#define XNetworkCacheMetaData_move_base XClass_move_base

/**
 * - @brief 设置缓存 URL。
 * - @param self 元数据；不能为 NULL。
 * - @param url URL；借用，函数执行时深拷贝；NULL 清空并使元数据无效。
 * - @return 成功返回 true；内存不足返回 false。
 */
bool XNetworkCacheMetaData_setUrl(XNetworkCacheMetaData* self, const XUrl* url);
/**
 * - @brief 获取缓存 URL。
 * - @param self 缓存元数据；可为 NULL。
 * - @return 内部 URL 借用指针；self 或 URL 为空时返回 NULL，调用者不得释放或修改。
 */
const XUrl* XNetworkCacheMetaData_url_const(const XNetworkCacheMetaData* self);
/**
 * - @brief 设置响应头。
 * - @param self 元数据；不能为 NULL。
 * - @param headers 头集合；借用，函数执行时深拷贝；NULL 表示空头集合。
 * - @return 成功返回 true；内存不足返回 false。
 */
bool XNetworkCacheMetaData_setHeaders(XNetworkCacheMetaData* self, const XHttpHeaders* headers);
/**
 * - @brief 获取缓存响应头。
 * - @param self 缓存元数据；可为 NULL。
 * - @return 内部响应头借用指针；self 或响应头为空时返回 NULL，调用者不得释放或修改。
 */
const XHttpHeaders* XNetworkCacheMetaData_headers_const(const XNetworkCacheMetaData* self);
/**
 * - @brief 设置最后修改时间。
 * - @param self 缓存元数据；可为 NULL，NULL 时不执行。
 * - @param msecs Unix UTC 毫秒时间戳；负值表示清除未设置时间。
 */
void XNetworkCacheMetaData_setLastModifiedMSecs(XNetworkCacheMetaData* self, int64_t msecs);
/**
 * - @brief 获取最后修改时间。
 * - @param self 缓存元数据；可为 NULL。
 * - @return Unix UTC 毫秒时间戳；未设置或 self 为空时返回 -1。
 */
int64_t XNetworkCacheMetaData_lastModifiedMSecs(const XNetworkCacheMetaData* self);
/**
 * - @brief 设置过期时间。
 * - @param self 缓存元数据；可为 NULL，NULL 时不执行。
 * - @param msecs Unix UTC 毫秒时间戳；负值表示清除未设置时间。
 */
void XNetworkCacheMetaData_setExpirationMSecs(XNetworkCacheMetaData* self, int64_t msecs);
/**
 * - @brief 获取过期时间。
 * - @param self 缓存元数据；可为 NULL。
 * - @return Unix UTC 毫秒时间戳；未设置或 self 为空时返回 -1。
 */
int64_t XNetworkCacheMetaData_expirationMSecs(const XNetworkCacheMetaData* self);
/**
 * - @brief 设置是否允许持久化缓存。
 * - @param self 缓存元数据；可为 NULL，NULL 时不执行。
 * - @param enabled true 表示允许写入持久缓存，false 表示仅作为临时条目。
 */
void XNetworkCacheMetaData_setSaveToDisk(XNetworkCacheMetaData* self, bool enabled);
/**
 * - @brief 判断是否允许持久化缓存。
 * - @param self 缓存元数据；可为 NULL。
 * - @return 允许写入持久缓存返回 true；self 为空时返回 false。
 */
bool XNetworkCacheMetaData_saveToDisk(const XNetworkCacheMetaData* self);
/**
 * - @brief 判断缓存元数据是否有效。
 * - @param self 缓存元数据；可为 NULL。
 * - @return 元数据标记有效且 URL 有效时返回 true；否则返回 false。
 */
bool XNetworkCacheMetaData_isValid(const XNetworkCacheMetaData* self);
/**
 * - @brief 判断两个缓存元数据是否相同。
 * - @param lhs 左侧缓存元数据；可为 NULL。
 * - @param rhs 右侧缓存元数据；可为 NULL。
 * - @return URL、响应头、时间、持久化标记和有效标记均相同时返回 true，否则返回 false。
 */
bool XNetworkCacheMetaData_equals(const XNetworkCacheMetaData* lhs,
                                  const XNetworkCacheMetaData* rhs);
/**
 * - @brief 交换两个缓存元数据的内容。
 * - @param lhs 左侧缓存元数据；不能为 NULL。
 * - @param rhs 右侧缓存元数据；不能为 NULL。
 */
void XNetworkCacheMetaData_swap(XNetworkCacheMetaData* lhs, XNetworkCacheMetaData* rhs);

XCLASS_DEFINE_BEGING(XNetworkDiskCache)
XCLASS_DEFINE_EXTEND_END(XNetworkDiskCache, XObject)

/**
 * - @brief HTTP 磁盘缓存对象。
 * - @details 缓存目录为空时使用进程内缓存；设置目录后条目仍通过项目文件抽象管理。
 */
typedef struct XNetworkDiskCache {
    XObject m_class;              /**< 第一个成员，继承 XObject。 */
    XString* m_cacheDirectory;    /**< 缓存目录；对象拥有。 */
    XVector* m_entries;           /**< 内部条目列表；对象拥有。 */
    int64_t m_maximumCacheSize;   /**< 最大缓存字节数；负值表示不限制。 */
    int64_t m_cacheSize;          /**< 当前缓存字节数。 */
} XNetworkDiskCache;

/**
 * - @brief 初始化网络磁盘缓存虚函数表。
 * - @return 磁盘缓存虚函数表；初始化失败返回 NULL，不由调用者释放。
 */
XVtable* XNetworkDiskCache_class_init(void);
/**
 * - @brief 初始化网络磁盘缓存对象。
 * - @param self 待初始化的磁盘缓存；不能为 NULL。
 */
void XNetworkDiskCache_init(XNetworkDiskCache* self);
/**
 * - @brief 创建网络磁盘缓存对象。
 * - @return 新建磁盘缓存；调用者必须使用 XNetworkDiskCache_delete_base 释放，分配失败返回 NULL。
 */
XNetworkDiskCache* XNetworkDiskCache_create_ex(XMemoryType memory);
#define XNetworkDiskCache_deinit_base XClass_deinit_base
#define XNetworkDiskCache_delete_base XClass_delete_base
#define XNetworkDiskCache_deleteLater XObject_deleteLater

/**
 * - @brief 设置缓存目录。
 * - @param self 缓存对象；不能为 NULL。
 * - @param directory 目录；借用，函数执行时深拷贝；NULL 清空目录并使用内存缓存。
 * - @return 成功返回 true；内存不足返回 false。
 */
bool XNetworkDiskCache_setCacheDirectory(XNetworkDiskCache* self, const XString* directory);
/**
 * - @brief 获取缓存目录。
 * - @param self 磁盘缓存对象；可为 NULL。
 * - @return 缓存目录借用指针；self 或目录为空时返回 NULL，调用者不得释放或修改。
 */
const XString* XNetworkDiskCache_cacheDirectory_const(const XNetworkDiskCache* self);
/**
 * - @brief 设置最大缓存容量。
 * - @param self 磁盘缓存对象；不能为 NULL。
 * - @param size 最大缓存字节数；负值表示不限制。
 */
void XNetworkDiskCache_setMaximumCacheSize(XNetworkDiskCache* self, int64_t size);
/**
 * - @brief 获取最大缓存容量。
 * - @param self 磁盘缓存对象；可为 NULL。
 * - @return 最大缓存字节数；self 为空时返回 -1，表示不限制。
 */
int64_t XNetworkDiskCache_maximumCacheSize(const XNetworkDiskCache* self);
/**
 * - @brief 获取当前缓存已用容量。
 * - @param self 磁盘缓存对象；可为 NULL。
 * - @return 当前缓存字节数；self 为空时返回 0。
 */
int64_t XNetworkDiskCache_cacheSize(const XNetworkDiskCache* self);
/**
 * - @brief 查询 URL 的缓存元数据。
 * - @param self 缓存对象；可为 NULL。
 * - @param url URL；借用，不能为 NULL。
 * - @return 新元数据对象；未找到返回无效但非 NULL 元数据，调用者必须释放。
 */
XNetworkCacheMetaData* XNetworkDiskCache_metaData(const XNetworkDiskCache* self,
                                                  const XUrl* url);
/**
 * - @brief 读取 URL 的缓存数据。
 * - @param self 缓存对象；可为 NULL。
 * - @param url URL；借用，不能为 NULL。
 * - @return 新字节数组；未找到返回 NULL，调用者必须释放。
 */
XByteArray* XNetworkDiskCache_data(const XNetworkDiskCache* self, const XUrl* url);
/**
 * - @brief 写入或替换缓存条目。
 * - @param self 缓存对象；不能为 NULL。
 * - @param metadata 元数据；借用，函数执行时深拷贝；不能为 NULL。
 * - @param data 响应数据；借用，函数执行时深拷贝；不能为 NULL。
 * - @return 成功返回 true；超过容量或内存不足返回 false。
 */
bool XNetworkDiskCache_insert(XNetworkDiskCache* self,
                              const XNetworkCacheMetaData* metadata,
                              const XByteArray* data);
/**
 * - @brief 删除指定 URL 的缓存条目。
 * - @param self 磁盘缓存对象；不能为 NULL。
 * - @param url 缓存 URL；借用且不能为 NULL。
 * - @return 找到并删除条目返回 true；参数无效或未找到条目返回 false。
 */
bool XNetworkDiskCache_remove(XNetworkDiskCache* self, const XUrl* url);
/**
 * - @brief 删除全部缓存条目。
 * - @param self 磁盘缓存对象；可为 NULL，NULL 时不执行。
 */
void XNetworkDiskCache_clear(XNetworkDiskCache* self);

#endif // XHTTP_ON
#endif /* XPROTOCOL_ON */
#ifdef __cplusplus
}
#endif


/* XClass create API default-memory wrappers. */
#undef XNetworkCacheMetaData_create
#define XNetworkCacheMetaData_create() XNetworkCacheMetaData_create_ex(XCLASS_DEFAULT_MEMORY_TYPE)
#undef XNetworkDiskCache_create
#define XNetworkDiskCache_create() XNetworkDiskCache_create_ex(XCLASS_DEFAULT_MEMORY_TYPE)

#endif /* XNETWORKCACHE_H */
