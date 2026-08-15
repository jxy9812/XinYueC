/**
 * @file       XHttp1Configuration.h
 * - @brief      HTTP/1 配置值对象，对标 Qt 6.8 QHttp1Configuration。
 * - @details    本配置仅保存 HTTP/1 连接策略，不直接访问套接字、平台线程或平台 API。
 */

#ifndef XHTTP1CONFIGURATION_H
#define XHTTP1CONFIGURATION_H
#include "XHttp_config.h"

#ifdef __cplusplus
extern "C" {
#endif
#if XPROTOCOL_ON
#if XHTTP_ON

#include "XClass.h"
#include <stddef.h>

XCLASS_DEFINE_BEGING(XHttp1Configuration)
XCLASS_DEFINE_EXTEND_END(XHttp1Configuration, XClass)

/**
 * - @brief HTTP/1 连接配置。
 * - @details 每主机连接数默认 6，取值范围为 1 到 255；对象采用 XClass 值语义。
 */
typedef struct XHttp1Configuration {
    XClass m_class;                         /**< 第一个成员，继承 XClass。 */
    size_t m_numberOfConnectionsPerHost;   /**< 每个 host:port 的连接数。 */
} XHttp1Configuration;

/**
 * - @brief 初始化 HTTP/1 配置虚函数表。
 * - @return HTTP/1 配置虚函数表；初始化失败返回 NULL，不由调用者释放。
 */
XVtable* XHttp1Configuration_class_init(void);
/**
 * - @brief 初始化 HTTP/1 配置对象。
 * - @param self 待初始化的配置对象；不能为 NULL。
 */
void XHttp1Configuration_init(XHttp1Configuration* self);
/**
 * - @brief 创建 HTTP/1 配置对象。
 * - @return 新建配置对象；调用者必须使用 XHttp1Configuration_delete_base 释放，失败返回 NULL。
 */
XHttp1Configuration* XHttp1Configuration_create_ex(XMemoryType memory);
/**
 * - @brief 深拷贝创建 HTTP/1 配置对象。
 * - @param other 源配置对象；借用且不能为 NULL。
 * - @return 新建配置对象；调用者必须使用 XHttp1Configuration_delete_base 释放，参数无效或分配失败返回 NULL。
 */
XHttp1Configuration* XHttp1Configuration_create_copy(const XHttp1Configuration* other);
/**
 * - @brief 移动创建 HTTP/1 配置对象。
 * - @param other 源配置对象；借用且不能为 NULL，成功后恢复为已初始化的默认状态。
 * - @return 新建配置对象；调用者必须使用 XHttp1Configuration_delete_base 释放，参数无效或分配失败返回 NULL。
 */
XHttp1Configuration* XHttp1Configuration_create_move(XHttp1Configuration* other);

/** @brief 配置生命周期和值语义入口。 */
#define XHttp1Configuration_deinit_base XClass_deinit_base
#define XHttp1Configuration_delete_base XClass_delete_base
#define XHttp1Configuration_copy_base XClass_copy_base
#define XHttp1Configuration_move_base XClass_move_base

/**
 * - @brief 设置每主机连接数。
 * - @param self 配置对象；不能为 NULL。
 * - @param amount 连接数；0 不修改，超过 255 按 255 处理。
 * - @return 成功修改或 amount 为 0 返回 true；self 为 NULL 返回 false。
 */
bool XHttp1Configuration_setNumberOfConnectionsPerHost(XHttp1Configuration* self, size_t amount);

/**
 * - @brief 获取每主机连接数。
 * - @param self 配置对象；可为 NULL。
 * - @return 连接数；self 为 NULL 返回 6。
 */
size_t XHttp1Configuration_numberOfConnectionsPerHost(const XHttp1Configuration* self);

#endif // XHTTP_ON
#endif /* XPROTOCOL_ON */
#ifdef __cplusplus
}
#endif


/* XClass create API default-memory wrappers. */
#undef XHttp1Configuration_create
#define XHttp1Configuration_create(...) XHttp1Configuration_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, ##__VA_ARGS__)

#endif
