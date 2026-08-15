/**
 * @file       XNetworkRequestFactory.h
 * - @brief      可复制的 HTTP 请求工厂，对标 Qt 6.8 QNetworkRequestFactory。
 * - @details    工厂保存基础 URL、公共头、认证、查询参数、超时、优先级和请求属性；
 *             创建出的请求彼此独立，HTTP 模块不直接调用平台 API。
 */

#ifndef XNETWORKREQUESTFACTORY_H
#define XNETWORKREQUESTFACTORY_H
#include "XHttp_config.h"

#ifdef __cplusplus
extern "C" {
#endif
#if XPROTOCOL_ON
#if XHTTP_ON

#include "XHttpRequest.h"
#include "XVariant.h"

XCLASS_DEFINE_BEGING(XNetworkRequestFactory)
XCLASS_DEFINE_EXTEND_END(XNetworkRequestFactory, XClass)

/** @brief QNetworkRequest::Attribute 的可移植 C 枚举。 */
typedef enum XNetworkRequestFactory_Attribute {
    XNetworkRequestFactory_HttpStatusCodeAttribute,
    XNetworkRequestFactory_HttpReasonPhraseAttribute,
    XNetworkRequestFactory_RedirectionTargetAttribute,
    XNetworkRequestFactory_ConnectionEncryptedAttribute,
    XNetworkRequestFactory_CacheLoadControlAttribute,
    XNetworkRequestFactory_CacheSaveControlAttribute,
    XNetworkRequestFactory_SourceIsFromCacheAttribute,
    XNetworkRequestFactory_DoNotBufferUploadDataAttribute,
    XNetworkRequestFactory_HttpPipeliningAllowedAttribute,
    XNetworkRequestFactory_HttpPipeliningWasUsedAttribute,
    XNetworkRequestFactory_CustomVerbAttribute,
    XNetworkRequestFactory_CookieLoadControlAttribute,
    XNetworkRequestFactory_AuthenticationReuseAttribute,
    XNetworkRequestFactory_CookieSaveControlAttribute,
    XNetworkRequestFactory_MaximumDownloadBufferSizeAttribute,
    XNetworkRequestFactory_DownloadBufferAttribute,
    XNetworkRequestFactory_SynchronousRequestAttribute,
    XNetworkRequestFactory_BackgroundRequestAttribute,
    XNetworkRequestFactory_EmitAllUploadProgressSignalsAttribute,
    XNetworkRequestFactory_Http2AllowedAttribute,
    XNetworkRequestFactory_Http2WasUsedAttribute,
    XNetworkRequestFactory_OriginalContentLengthAttribute,
    XNetworkRequestFactory_RedirectPolicyAttribute,
    XNetworkRequestFactory_Http2DirectAttribute,
    XNetworkRequestFactory_ResourceTypeAttribute,
    XNetworkRequestFactory_AutoDeleteReplyOnFinishAttribute,
    XNetworkRequestFactory_ConnectionCacheExpiryTimeoutSecondsAttribute,
    XNetworkRequestFactory_Http2CleartextAllowedAttribute,
    XNetworkRequestFactory_UseCredentialsAttribute,
    XNetworkRequestFactory_FullLocalServerNameAttribute,
    XNetworkRequestFactory_UserAttribute = 1000,
    XNetworkRequestFactory_UserMaxAttribute = 32767
} XNetworkRequestFactory_Attribute;

/** @brief 工厂内部保存的请求属性。 */
typedef struct XNetworkRequestFactory_AttributeItem {
    int m_code;          /**< 属性编号；包含内置和用户编号。 */
    XVariant* m_value;   /**< 属性值；由工厂拥有。 */
} XNetworkRequestFactory_AttributeItem;

/**
 * - @brief 请求模板工厂。
 * - @details 所有成员由工厂拥有；属性值使用 XVariant 深拷贝，工厂复制不会共享可变状态。
 */
typedef struct XNetworkRequestFactory {
    XClass m_class;                 /**< 第一个成员，由 XClass 管理。 */
    XUrl* m_baseUrl;                /**< 基础 URL；由工厂拥有。 */
    XHttpHeaders* m_commonHeaders;  /**< 公共请求头；由工厂拥有。 */
    XByteArray* m_bearerToken;      /**< Bearer 令牌；由工厂拥有。 */
    XByteArray* m_userName;         /**< Basic 用户名；由工厂拥有。 */
    XByteArray* m_password;         /**< Basic 密码；由工厂拥有。 */
    XByteArray* m_queryParameters;  /**< 原始 URL 查询参数，不含问号；由工厂拥有。 */
    XVector* m_attributes;          /**< XNetworkRequestFactory_AttributeItem 值列表；由工厂拥有。 */
    int m_transferTimeout;          /**< 默认传输超时，单位毫秒。 */
    XHttpRequest_Priority m_priority; /**< 默认请求优先级。 */
} XNetworkRequestFactory;

/**
 * - @brief 初始化请求工厂虚函数表。
 * - @return 请求工厂虚函数表；初始化失败返回 NULL，不由调用者释放。
 */
XVtable* XNetworkRequestFactory_class_init(void);
/**
 * - @brief 初始化请求工厂。
 * - @param self 待初始化的请求工厂；不能为 NULL。
 */
void XNetworkRequestFactory_init(XNetworkRequestFactory* self);
/**
 * - @brief 创建空请求工厂。
 * - @return 新建请求工厂；调用者必须使用 XNetworkRequestFactory_delete_base 释放，分配失败返回 NULL。
 */
XNetworkRequestFactory* XNetworkRequestFactory_create_ex(XMemoryType memory);
/**
 * - @brief 从基础 URL 创建请求工厂。
 * - @param baseUrl 基础 URL；借用，可为 NULL，创建时深拷贝。
 * - @return 新建请求工厂；调用者必须使用 XNetworkRequestFactory_delete_base 释放，分配或拷贝失败返回 NULL。
 */
XNetworkRequestFactory* XNetworkRequestFactory_create_url(const XUrl* baseUrl);
/**
 * - @brief 深拷贝创建请求工厂。
 * - @param other 源请求工厂；借用且不能为 NULL。
 * - @return 新建请求工厂；调用者必须使用 XNetworkRequestFactory_delete_base 释放，参数无效或拷贝失败返回 NULL。
 */
XNetworkRequestFactory* XNetworkRequestFactory_create_copy(const XNetworkRequestFactory* other);
/**
 * - @brief 移动创建请求工厂。
 * - @param other 源请求工厂；借用且不能为 NULL，成功后保留已初始化的空状态。
 * - @return 新建请求工厂；调用者必须使用 XNetworkRequestFactory_delete_base 释放，参数无效或分配失败返回 NULL。
 */
XNetworkRequestFactory* XNetworkRequestFactory_create_move(XNetworkRequestFactory* other);

#define XNetworkRequestFactory_deinit_base XClass_deinit_base
#define XNetworkRequestFactory_delete_base XClass_delete_base
#define XNetworkRequestFactory_copy_base XClass_copy_base
#define XNetworkRequestFactory_move_base XClass_move_base

/**
 * - @brief 获取基础 URL 副本。
 * - @param self 请求工厂；可为 NULL。
 * - @return 新建基础 URL；调用者必须使用 XUrl_delete_base 释放，未设置或 self 为空时返回 NULL。
 */
XUrl* XNetworkRequestFactory_baseUrl(const XNetworkRequestFactory* self);
/**
 * - @brief 设置基础 URL。
 * - @param self 请求工厂；不能为 NULL。
 * - @param url 基础 URL；借用，NULL 表示清空，非 NULL 时深拷贝。
 * - @return 设置成功返回 true；self 无效或深拷贝失败返回 false。
 */
bool XNetworkRequestFactory_setBaseUrl(XNetworkRequestFactory* self, const XUrl* url);
/**
 * - @brief 获取公共请求头副本。
 * - @param self 请求工厂；可为 NULL。
 * - @return 新建请求头对象；调用者必须使用 XHttpHeaders_delete_base 释放，self 为空或分配失败返回 NULL。
 */
XHttpHeaders* XNetworkRequestFactory_commonHeaders(const XNetworkRequestFactory* self);
/**
 * - @brief 替换公共请求头。
 * - @param self 请求工厂；不能为 NULL。
 * - @param headers 请求头；借用，NULL 表示清空，非 NULL 时深拷贝。
 * - @return 设置成功返回 true；self 无效或深拷贝失败返回 false。
 */
bool XNetworkRequestFactory_setCommonHeaders(XNetworkRequestFactory* self, const XHttpHeaders* headers);
/**
 * - @brief 清空公共请求头。
 * - @param self 请求工厂；可为 NULL。
 */
void XNetworkRequestFactory_clearCommonHeaders(XNetworkRequestFactory* self);
/**
 * - @brief 获取 Bearer 令牌副本。
 * - @param self 请求工厂；可为 NULL。
 * - @return 新建 Bearer 令牌；调用者必须使用 XByteArray_delete_base 释放，未设置或 self 为空时返回 NULL。
 */
XByteArray* XNetworkRequestFactory_bearerToken(const XNetworkRequestFactory* self);
/**
 * - @brief 设置 Bearer 令牌。
 * - @param self 请求工厂；不能为 NULL。
 * - @param token Bearer 令牌；借用，NULL 表示清除，非 NULL 时深拷贝。
 * - @return 设置成功返回 true；self 无效或深拷贝失败返回 false。
 */
bool XNetworkRequestFactory_setBearerToken(XNetworkRequestFactory* self, const XByteArray* token);
/**
 * - @brief 清除 Bearer 令牌。
 * - @param self 请求工厂；可为 NULL。
 */
void XNetworkRequestFactory_clearBearerToken(XNetworkRequestFactory* self);
/**
 * - @brief 设置 Basic 认证用户名。
 * - @param self 请求工厂；不能为 NULL。
 * - @param name 用户名；借用，NULL 表示清除，非 NULL 时深拷贝。
 * - @return 设置成功返回 true；self 无效或深拷贝失败返回 false。
 */
bool XNetworkRequestFactory_setUserName(XNetworkRequestFactory* self, const XByteArray* name);
/**
 * - @brief 获取 Basic 认证用户名副本。
 * - @param self 请求工厂；可为 NULL。
 * - @return 新建用户名；调用者必须使用 XByteArray_delete_base 释放，未设置或 self 为空时返回 NULL。
 */
XByteArray* XNetworkRequestFactory_userName(const XNetworkRequestFactory* self);
/**
 * - @brief 清除 Basic 认证用户名。
 * - @param self 请求工厂；可为 NULL。
 */
void XNetworkRequestFactory_clearUserName(XNetworkRequestFactory* self);
/**
 * - @brief 设置 Basic 认证密码。
 * - @param self 请求工厂；不能为 NULL。
 * - @param password 密码；借用，NULL 表示清除，非 NULL 时深拷贝。
 * - @return 设置成功返回 true；self 无效或深拷贝失败返回 false。
 */
bool XNetworkRequestFactory_setPassword(XNetworkRequestFactory* self, const XByteArray* password);
/**
 * - @brief 获取 Basic 认证密码副本。
 * - @param self 请求工厂；可为 NULL。
 * - @return 新建密码；调用者必须使用 XByteArray_delete_base 释放，未设置或 self 为空时返回 NULL。
 */
XByteArray* XNetworkRequestFactory_password(const XNetworkRequestFactory* self);
/**
 * - @brief 清除 Basic 认证密码。
 * - @param self 请求工厂；可为 NULL。
 */
void XNetworkRequestFactory_clearPassword(XNetworkRequestFactory* self);
/**
 * - @brief 设置原始查询参数。
 * - @param self 请求工厂；不能为 NULL。
 * - @param query 原始查询参数；借用且不含问号，NULL 表示清空，非 NULL 时深拷贝。
 * - @return 设置成功返回 true；self 无效或深拷贝失败返回 false。
 */
bool XNetworkRequestFactory_setQueryParameters(XNetworkRequestFactory* self, const XByteArray* query);
/**
 * - @brief 获取原始查询参数副本。
 * - @param self 请求工厂；可为 NULL。
 * - @return 新建查询参数字节数组；调用者必须使用 XByteArray_delete_base 释放，未设置或 self 为空时返回 NULL。
 */
XByteArray* XNetworkRequestFactory_queryParameters(const XNetworkRequestFactory* self);
/**
 * - @brief 清空原始查询参数。
 * - @param self 请求工厂；可为 NULL。
 */
void XNetworkRequestFactory_clearQueryParameters(XNetworkRequestFactory* self);
/**
 * - @brief 设置默认传输超时。
 * - @param self 请求工厂；不能为 NULL。
 * - @param timeout 超时时间，单位毫秒；小于 0 时按 0 处理。
 */
void XNetworkRequestFactory_setTransferTimeout(XNetworkRequestFactory* self, int timeout);
/**
 * - @brief 获取默认传输超时。
 * - @param self 请求工厂；可为 NULL。
 * - @return 超时时间，单位毫秒；self 为空时返回 30000。
 */
int XNetworkRequestFactory_transferTimeout(const XNetworkRequestFactory* self);
/**
 * - @brief 设置默认请求优先级。
 * - @param self 请求工厂；不能为 NULL。
 * - @param priority 请求优先级；必须是 XHttpRequest_HighPriority、NormalPriority 或 LowPriority。
 * - @return 设置成功返回 true；参数无效时返回 false。
 */
bool XNetworkRequestFactory_setPriority(XNetworkRequestFactory* self, XHttpRequest_Priority priority);
/**
 * - @brief 获取默认请求优先级。
 * - @param self 请求工厂；可为 NULL。
 * - @return 默认请求优先级；self 为空时返回 XHttpRequest_NormalPriority。
 */
XHttpRequest_Priority XNetworkRequestFactory_priority(const XNetworkRequestFactory* self);

/**
 * - @brief 按基础 URL 创建请求。
 * - @param self 请求工厂；不能为 NULL。
 * - @return 新建 HTTP 请求；调用者必须使用 XHttpRequest_delete_base 释放，基础 URL 未设置或创建失败返回 NULL。
 */
XHttpRequest* XNetworkRequestFactory_createRequest(const XNetworkRequestFactory* self);
/**
 * - @brief 按相对或绝对路径创建请求。
 * - @param self 请求工厂；不能为 NULL。
 * - @param path 路径 UTF-8 文本；借用且不能为 NULL，相对路径基于基础 URL 解析。
 * - @return 新建 HTTP 请求；调用者必须使用 XHttpRequest_delete_base 释放，参数无效或创建失败返回 NULL。
 */
XHttpRequest* XNetworkRequestFactory_createRequest_path(const XNetworkRequestFactory* self,
                                                        const char* path);
/**
 * - @brief 按路径和原始查询参数创建请求。
 * - @param self 请求工厂；不能为 NULL。
 * - @param path 路径 UTF-8 文本；借用且不能为 NULL，相对路径基于基础 URL 解析。
 * - @param query 原始查询参数；借用，可为 NULL 且不含问号，非 NULL 时覆盖工厂默认查询参数。
 * - @return 新建 HTTP 请求；调用者必须使用 XHttpRequest_delete_base 释放，参数无效或创建失败返回 NULL。
 */
XHttpRequest* XNetworkRequestFactory_createRequest_path_query(const XNetworkRequestFactory* self,
                                                              const char* path,
                                                              const XByteArray* query);

/**
 * - @brief 设置或替换请求模板属性。
 * - @param self 请求工厂；不能为 NULL。
 * - @param code 属性编号；可使用 XNetworkRequestFactory_Attribute 中的内置编号或用户自定义编号。
 * - @param value 属性值；借用，NULL 表示清除该属性，非 NULL 时深拷贝。
 * - @return 设置成功返回 true；self 无效或深拷贝失败返回 false。
 */
bool XNetworkRequestFactory_setAttribute(XNetworkRequestFactory* self, int code,
                                         const XVariant* value);
/**
 * - @brief 获取请求模板属性副本。
 * - @param self 请求工厂；可为 NULL。
 * - @param code 属性编号。
 * - @return 新建属性值；调用者必须使用 XVariant_delete_base 释放，未设置、参数无效或拷贝失败返回 NULL。
 */
XVariant* XNetworkRequestFactory_attribute(const XNetworkRequestFactory* self, int code);
/**
 * - @brief 清除一个请求模板属性。
 * - @param self 请求工厂；可为 NULL。
 * - @param code 要清除的属性编号。
 */
void XNetworkRequestFactory_clearAttribute(XNetworkRequestFactory* self, int code);
/**
 * - @brief 清除全部请求模板属性。
 * - @param self 请求工厂；可为 NULL。
 */
void XNetworkRequestFactory_clearAttributes(XNetworkRequestFactory* self);

#endif // XHTTP_ON
#endif /* XPROTOCOL_ON */
#ifdef __cplusplus
}
#endif


/* XClass create API default-memory wrappers. */
#undef XNetworkRequestFactory_create
#define XNetworkRequestFactory_create() XNetworkRequestFactory_create_ex(XCLASS_DEFAULT_MEMORY_TYPE)

#endif /* XNETWORKREQUESTFACTORY_H */
