/**
 * @file       XHttpRequest.h
 * - @brief      HTTP 请求值对象公开 API。
 * - @details    对齐 Qt 6.8 QNetworkRequest 的 URL、头部、请求体和传输配置；
 *             只提供协议层数据，不直接创建套接字或调用任何平台网络 API。
 */

#ifndef XHTTPREQUEST_H
#define XHTTPREQUEST_H
#include "XHttp_config.h"

#ifdef __cplusplus
extern "C" {
#endif
#if XPROTOCOL_ON
#if XHTTP_ON

#include "XClass.h"
#include "XHttp1Configuration.h"
#include "XHttp2Configuration.h"
#include "XHttpHeaders.h"
#include "XUrl.h"
#include "XByteArray.h"
#include "XVariant.h"
#include <stdbool.h>
#include <stdint.h>

XCLASS_DEFINE_BEGING(XHttpRequest)
XCLASS_DEFINE_EXTEND_END(XHttpRequest, XClass)

/**
 * - @brief HTTP 请求方法。
 * - @details 方法名称采用 HTTP/1.1 大写拼写；Custom 方法使用 m_customMethod。
 */
typedef enum XHttpRequest_Method {
    XHttpRequest_Head = 0,       /**< HEAD：只请求响应头。 */
    XHttpRequest_Get,            /**< GET：读取资源。 */
    XHttpRequest_Post,           /**< POST：提交资源或执行操作。 */
    XHttpRequest_Put,            /**< PUT：完整替换资源。 */
    XHttpRequest_Delete,         /**< DELETE：删除资源。 */
    XHttpRequest_Patch,          /**< PATCH：部分更新资源。 */
    XHttpRequest_Custom          /**< 自定义方法；必须先设置方法名称。 */
} XHttpRequest_Method;

/**
 * - @brief HTTP 请求优先级。
 * - @details 数值约定与 Qt QNetworkRequest::Priority 对齐，数值越小优先级越高。
 */
typedef enum XHttpRequest_Priority {
    XHttpRequest_HighPriority = 1,     /**< 高优先级。 */
    XHttpRequest_NormalPriority = 3,   /**< 默认优先级。 */
    XHttpRequest_LowPriority = 5       /**< 低优先级。 */
} XHttpRequest_Priority;

/**
 * - @brief 重定向策略。
 * - @details 与 Qt QNetworkRequest::RedirectPolicy 的四个策略对应。
 */
typedef enum XHttpRequest_RedirectPolicy {
    XHttpRequest_ManualRedirectPolicy = 0,       /**< 不自动跟随重定向。 */
    XHttpRequest_NoLessSafeRedirectPolicy,        /**< 只允许安全性不降低的重定向。 */
    XHttpRequest_SameOriginRedirectPolicy,        /**< 只允许同源重定向。 */
    XHttpRequest_UserVerifiedRedirectPolicy       /**< 由调用者确认后再跟随。 */
} XHttpRequest_RedirectPolicy;

/**
 * - @brief QNetworkRequest::Attribute 的请求属性编号。
 * - @details 属性值使用 XVariant 保存；User 到 UserMax 为用户自定义编号范围。
 */
typedef enum XHttpRequest_Attribute {
    XHttpRequest_HttpStatusCodeAttribute,
    XHttpRequest_HttpReasonPhraseAttribute,
    XHttpRequest_RedirectionTargetAttribute,
    XHttpRequest_ConnectionEncryptedAttribute,
    XHttpRequest_CacheLoadControlAttribute,
    XHttpRequest_CacheSaveControlAttribute,
    XHttpRequest_SourceIsFromCacheAttribute,
    XHttpRequest_DoNotBufferUploadDataAttribute,
    XHttpRequest_HttpPipeliningAllowedAttribute,
    XHttpRequest_HttpPipeliningWasUsedAttribute,
    XHttpRequest_CustomVerbAttribute,
    XHttpRequest_CookieLoadControlAttribute,
    XHttpRequest_AuthenticationReuseAttribute,
    XHttpRequest_CookieSaveControlAttribute,
    XHttpRequest_MaximumDownloadBufferSizeAttribute,
    XHttpRequest_DownloadBufferAttribute,
    XHttpRequest_SynchronousRequestAttribute,
    XHttpRequest_BackgroundRequestAttribute,
    XHttpRequest_EmitAllUploadProgressSignalsAttribute,
    XHttpRequest_Http2AllowedAttribute,
    XHttpRequest_Http2WasUsedAttribute,
    XHttpRequest_OriginalContentLengthAttribute,
    XHttpRequest_RedirectPolicyAttribute,
    XHttpRequest_Http2DirectAttribute,
    XHttpRequest_ResourceTypeAttribute,
    XHttpRequest_AutoDeleteReplyOnFinishAttribute,
    XHttpRequest_ConnectionCacheExpiryTimeoutSecondsAttribute,
    XHttpRequest_Http2CleartextAllowedAttribute,
    XHttpRequest_UseCredentialsAttribute,
    XHttpRequest_FullLocalServerNameAttribute,
    XHttpRequest_UserAttribute = 1000,
    XHttpRequest_UserMaxAttribute = 32767
} XHttpRequest_Attribute;

/** @brief 请求属性内部项；值由所属请求对象拥有。 */
typedef struct XHttpRequest_AttributeItem {
    int m_code;        /**< 属性编号。 */
    XVariant* m_value; /**< 属性值；由请求对象拥有。 */
} XHttpRequest_AttributeItem;

/**
 * - @brief HTTP 请求值对象。
 * - @details 所有指针成员均由请求对象拥有；m_class 必须是第一个成员并由 XClass
 *          管理。请求可安全深拷贝，也可将资源移动到另一个已初始化对象。
 */
typedef struct XHttpRequest {
    XClass m_class;                         /**< 第一个成员，由 XClass 管理，禁止手工修改。 */
    XUrl* m_url;                            /**< 请求 URL；由对象拥有。 */
    XHttpHeaders* m_headers;               /**< 请求头；由对象拥有。 */
    XByteArray* m_body;                    /**< 请求体；由对象拥有。 */
    XByteArray* m_customMethod;            /**< 自定义方法名称；由对象拥有。 */
    XHttp1Configuration* m_http1Configuration; /**< HTTP/1 配置；由对象拥有。 */
    XHttp2Configuration* m_http2Configuration; /**< HTTP/2 配置；由对象拥有。 */
    XVector* m_attributes;                 /**< XHttpRequest_AttributeItem 列表；由对象拥有。 */
    XHttpRequest_Method m_method;          /**< 请求方法。 */
    XHttpRequest_Priority m_priority;      /**< 调度优先级。 */
    XHttpRequest_RedirectPolicy m_redirectPolicy; /**< 重定向策略。 */
    int m_maximumRedirectsAllowed;         /**< 允许的最大重定向次数；负数表示使用管理器默认值。 */
    int m_transferTimeout;                 /**< 单次传输超时，单位毫秒；0 表示使用管理器默认值。 */
    bool m_autoDecompress;                 /**< 是否请求并自动解压 gzip/deflate 响应。 */
} XHttpRequest;

/**
 * - @brief 初始化 XHttpRequest 的虚函数表。
 * - @return 共享虚函数表；失败返回 NULL。
 */
XVtable* XHttpRequest_class_init(void);

/**
 * - @brief 初始化 HTTP 请求对象。
 * - @param self 待初始化对象；不能为 NULL。
 * - @return 无。初始化后 URL 为空、方法为 GET、超时为 0。
 */
void XHttpRequest_init(XHttpRequest* self);

/**
 * - @brief 创建默认 HTTP 请求对象。
 * - @return 新对象，调用者必须使用 XHttpRequest_delete_base 释放；失败返回 NULL。
 */
XHttpRequest* XHttpRequest_create_ex(XMemoryType memory);

/**
 * - @brief 从 URL 创建 HTTP 请求对象。
 * - @param url 请求 URL；借用，创建时深拷贝；NULL 时返回 NULL。
 * - @return 新对象，调用者必须使用 XHttpRequest_delete_base 释放。
 */
XHttpRequest* XHttpRequest_create_url(const XUrl* url);

/**
 * - @brief 深拷贝创建 HTTP 请求。
 * - @param other 源请求；NULL 时返回 NULL。
 * - @return 新对象，调用者必须使用 XHttpRequest_delete_base 释放。
 */
XHttpRequest* XHttpRequest_create_copy(const XHttpRequest* other);

/**
 * - @brief 移动创建 HTTP 请求。
 * - @param other 源请求；成功后源对象保持已初始化但为空的状态。
 * - @return 新对象，调用者必须使用 XHttpRequest_delete_base 释放。
 */
XHttpRequest* XHttpRequest_create_move(XHttpRequest* other);

/**
 * - @brief 反初始化、删除、拷贝和移动调度入口。
 * - @details copy/move 支持未初始化目标，并处理目标与源相同的情况。
 */
#define XHttpRequest_deinit_base XClass_deinit_base
#define XHttpRequest_delete_base XClass_delete_base
#define XHttpRequest_copy_base XClass_copy_base
#define XHttpRequest_move_base XClass_move_base

/**
 * - @brief 获取请求 URL 的借用只读指针。
 * - @param self 请求对象；NULL 时返回 NULL。
 * - @return 内部 URL；请求被修改或销毁后失效，调用者不得释放或修改。
 */
const XUrl* XHttpRequest_url_const(const XHttpRequest* self);

/**
 * - @brief 设置请求 URL。
 * - @param self 请求对象；不能为 NULL。
 * - @param url 新 URL；借用，函数执行时深拷贝；NULL 会清空 URL。
 * - @return 成功返回 true；分配失败时旧 URL 保持不变。
 */
bool XHttpRequest_setUrl(XHttpRequest* self, const XUrl* url);

/**
 * - @brief 从 UTF-8 字符串设置请求 URL。
 * - @param self 请求对象；不能为 NULL。
 * - @param url UTF-8 URL 字符串；借用，不能为 NULL。
 * - @return 解析并设置成功返回 true；失败时旧 URL 保持不变。
 */
bool XHttpRequest_setUrl_utf8(XHttpRequest* self, const char* url);

/**
 * - @brief 获取请求头的借用只读指针。
 * - @param self 请求对象；NULL 时返回 NULL。
 * - @return 内部头对象；请求销毁后失效，调用者不得释放。
 */
const XHttpHeaders* XHttpRequest_headers_const(const XHttpRequest* self);

/**
 * - @brief 获取可修改的请求头。
 * - @param self 请求对象；NULL 时返回 NULL。
 * - @return 内部头对象；仍由请求对象拥有，不得单独释放。
 */
XHttpHeaders* XHttpRequest_headers(XHttpRequest* self);

/**
 * - @brief 设置或追加请求头。
 * - @param self 请求对象；不能为 NULL。
 * - @param name 字段名称；借用且必须是 HTTP token。
 * - @param value 字段值；借用，不能包含非法控制字符。
 * - @return 成功返回 true；失败时请求头保持不变。
 */
bool XHttpRequest_setRawHeader(XHttpRequest* self, const char* name, const char* value);
/**
 * - @brief 判断是否存在指定原始请求头。
 * - @param self HTTP 请求对象；可为 NULL。
 * - @param name 请求头名称；借用且必须是合法 HTTP token，比较忽略 ASCII 大小写。
 * - @return 存在至少一个同名请求头返回 true；参数无效或未找到返回 false。
 */
bool XHttpRequest_hasRawHeader(const XHttpRequest* self, const XByteArray* name);
/**
 * - @brief 获取首个同名原始请求头值副本。
 * - @param self HTTP 请求对象；可为 NULL。
 * - @param name 请求头名称；借用且必须是合法 HTTP token，比较忽略 ASCII 大小写。
 * - @return 新建请求头值；调用者必须使用 XByteArray_delete_base 释放，未找到、参数无效或拷贝失败返回 NULL。
 */
XByteArray* XHttpRequest_rawHeader(const XHttpRequest* self, const XByteArray* name);
/**
 * - @brief 获取原始请求头名称副本列表。
 * - @param self HTTP 请求对象；可为 NULL。
 * - @return 新建名称列表，元素类型为 XByteArray*；调用者必须使用 XHttpHeaders_values_free 释放，self 为空或分配失败返回 NULL。
 */
XVector* XHttpRequest_rawHeaderList(const XHttpRequest* self);
/**
 * - @brief 设置或替换已知请求头。
 * - @param self HTTP 请求对象；不能为 NULL。
 * - @param header 已知请求头枚举；使用 XHttpHeaders_WellKnownHeader 的合法值。
 * - @param value 请求头值；借用，可为 NULL，非 NULL 时深拷贝。
 * - @return 设置成功返回 true；参数无效或内存不足返回 false，失败时原请求头保持不变。
 */
bool XHttpRequest_setHeaderKnown(XHttpRequest* self, XHttpHeaders_WellKnownHeader header,
                                 const XByteArray* value);
/**
 * - @brief 获取已知请求头值副本。
 * - @param self HTTP 请求对象；可为 NULL。
 * - @param header 已知请求头枚举；使用 XHttpHeaders_WellKnownHeader 的合法值。
 * - @return 新建请求头值；调用者必须使用 XByteArray_delete_base 释放，未设置、参数无效或拷贝失败返回 NULL。
 */
XByteArray* XHttpRequest_headerKnown(const XHttpRequest* self,
                                     XHttpHeaders_WellKnownHeader header);

/**
 * - @brief 获取 HTTP/1 配置借用指针。
 * - @param self 请求对象；可为 NULL。
 * - @return 内部配置；请求修改或销毁后失效，调用者不得释放。
 */
const XHttp1Configuration* XHttpRequest_http1Configuration_const(const XHttpRequest* self);

/**
 * - @brief 设置 HTTP/1 配置。
 * - @param self 请求对象；不能为 NULL。
 * - @param configuration 配置；借用，函数执行时深拷贝；不能为 NULL。
 * - @return 成功返回 true；内存不足或参数无效返回 false。
 */
bool XHttpRequest_setHttp1Configuration(XHttpRequest* self,
                                        const XHttp1Configuration* configuration);

/**
 * - @brief 获取 HTTP/2 配置借用指针。
 * - @param self 请求对象；可为 NULL。
 * - @return 内部配置；请求修改或销毁后失效，调用者不得释放。
 */
const XHttp2Configuration* XHttpRequest_http2Configuration_const(const XHttpRequest* self);

/**
 * - @brief 设置 HTTP/2 配置。
 * - @param self 请求对象；不能为 NULL。
 * - @param configuration 配置；借用，函数执行时深拷贝；不能为 NULL。
 * - @return 成功返回 true；内存不足或参数无效返回 false。
 */
bool XHttpRequest_setHttp2Configuration(XHttpRequest* self,
                                        const XHttp2Configuration* configuration);

/**
 * - @brief 获取请求体的借用只读指针。
 * - @param self 请求对象；NULL 时返回 NULL。
 * - @return 内部请求体；请求被修改或销毁后失效，调用者不得释放。
 */
const XByteArray* XHttpRequest_body_const(const XHttpRequest* self);

/**
 * - @brief 设置请求体的深拷贝。
 * - @param self 请求对象；不能为 NULL。
 * - @param body 请求体；借用，NULL 等价于空请求体。
 * - @return 成功返回 true；失败时原请求体保持不变。
 */
bool XHttpRequest_setBody(XHttpRequest* self, const XByteArray* body);

/**
 * - @brief 设置 UTF-8 请求体。
 * - @param self 请求对象；不能为 NULL。
 * - @param body UTF-8 字符串；借用，NULL 等价于空请求体。
 * - @return 成功返回 true；失败时原请求体保持不变。
 */
bool XHttpRequest_setBody_utf8(XHttpRequest* self, const char* body);

/**
 * - @brief 设置请求方法。
 * - @param self 请求对象；不能为 NULL。
 * - @param method 请求方法枚举值。
 * - @return 成功返回 true；枚举非法时返回 false，原方法保持不变。
 */
bool XHttpRequest_setMethod(XHttpRequest* self, XHttpRequest_Method method);

/**
 * - @brief 设置自定义 HTTP 方法。
 * - @param self 请求对象；不能为 NULL。
 * - @param method 方法名称；借用且必须是 HTTP token，函数会复制它。
 * - @return 成功返回 true；失败时原方法保持不变。
 */
bool XHttpRequest_setCustomMethod(XHttpRequest* self, const char* method);

/**
 * - @brief 从字节数组设置自定义 HTTP 方法。
 * - @param self 请求对象；不能为 NULL。
 * - @param method 方法字节；借用，必须是 HTTP token，函数会复制；不能为 NULL。
 * - @return 成功返回 true；失败时原方法保持不变。
 */
bool XHttpRequest_setCustomMethod_bytes(XHttpRequest* self, const XByteArray* method);

/**
 * - @brief 获取请求方法枚举值。
 * - @param self 请求对象；NULL 时返回 GET。
 * - @return 当前方法。
 */
XHttpRequest_Method XHttpRequest_method(const XHttpRequest* self);

/**
 * - @brief 获取自定义方法名称的借用只读指针。
 * - @param self 请求对象；NULL 或非 Custom 方法时返回 NULL。
 * - @return 内部方法名称；调用者不得释放或修改。
 */
const XByteArray* XHttpRequest_customMethod_const(const XHttpRequest* self);

/**
 * - @brief 设置请求优先级。
 * - @param self 请求对象；不能为 NULL。
 * - @param priority 高、普通或低优先级。
 * - @return 枚举合法返回 true，否则返回 false。
 */
bool XHttpRequest_setPriority(XHttpRequest* self, XHttpRequest_Priority priority);

/**
 * - @brief 获取请求优先级。
 * - @param self 请求对象；NULL 时返回普通优先级。
 * - @return 当前优先级。
 */
XHttpRequest_Priority XHttpRequest_priority(const XHttpRequest* self);

/**
 * - @brief 设置重定向策略。
 * - @param self 请求对象；不能为 NULL。
 * - @param policy 重定向策略枚举值。
 * - @return 成功返回 true；枚举非法返回 false。
 */
bool XHttpRequest_setRedirectPolicy(XHttpRequest* self, XHttpRequest_RedirectPolicy policy);

/**
 * - @brief 获取重定向策略。
 * - @param self 请求对象；NULL 时返回手动策略。
 * - @return 当前重定向策略。
 */
XHttpRequest_RedirectPolicy XHttpRequest_redirectPolicy(const XHttpRequest* self);

/**
 * - @brief 设置允许的最大重定向次数。
 * - @param self 请求对象；不能为 NULL。
 * - @param maximumRedirectsAllowed 最大次数；负数表示使用管理器默认值。
 * - @return 无。
 */
void XHttpRequest_setMaximumRedirectsAllowed(XHttpRequest* self, int maximumRedirectsAllowed);

/**
 * - @brief 获取允许的最大重定向次数。
 * - @param self 请求对象；NULL 时返回 -1。
 * - @return 最大次数。
 */
int XHttpRequest_maximumRedirectsAllowed(const XHttpRequest* self);

/**
 * - @brief 设置传输超时。
 * - @param self 请求对象；不能为 NULL。
 * - @param timeout 超时时间，单位毫秒；0 表示使用管理器默认值。
 * - @return 无。
 */
void XHttpRequest_setTransferTimeout(XHttpRequest* self, int timeout);

/**
 * - @brief 获取传输超时。
 * - @param self 请求对象；NULL 时返回 0。
 * - @return 超时时间，单位毫秒。
 */
int XHttpRequest_transferTimeout(const XHttpRequest* self);

/**
 * - @brief 设置是否自动解压响应。
 * - @param self 请求对象；不能为 NULL。
 * - @param enabled true 表示请求 gzip/deflate 并在回复中解压。
 * - @return 无。
 */
void XHttpRequest_setAutoDecompress(XHttpRequest* self, bool enabled);

/**
 * - @brief 获取自动解压设置。
 * - @param self 请求对象；NULL 时返回 false。
 * - @return 是否自动解压。
 */
bool XHttpRequest_autoDecompress(const XHttpRequest* self);

/**
 * - @brief 设置请求属性。
 * - @param self 请求对象；不能为 NULL。
 * - @param code 属性编号；支持 Qt 内置编号和 User 到 UserMax。
 * - @param value 属性值；借用，函数执行时深拷贝；NULL 清除属性。
 * - @return 成功返回 true；编号、参数或内存无效返回 false。
 */
bool XHttpRequest_setAttribute(XHttpRequest* self, int code, const XVariant* value);
/**
 * - @brief 获取请求属性副本。
 * - @param self 请求对象；可为 NULL。
 * - @param code 属性编号。
 * - @return 新属性值；未设置返回 NULL，调用者必须使用 XVariant_delete_base 释放。
 */
XVariant* XHttpRequest_attribute(const XHttpRequest* self, int code);
/**
 * - @brief 清除一个请求属性。
 * - @param self HTTP 请求对象；可为 NULL，NULL 时不执行。
 * - @param code 要清除的属性编号；可使用内置编号或用户自定义编号。
 */
void XHttpRequest_clearAttribute(XHttpRequest* self, int code);
/**
 * - @brief 清除全部请求属性。
 * - @param self HTTP 请求对象；可为 NULL，NULL 时不执行。
 */
void XHttpRequest_clearAttributes(XHttpRequest* self);

/**
 * - @brief 将请求序列化为 HTTP/1.1 wire 数据。
 * - @param self 请求对象；不能为 NULL。
 * - @param includeConnectionClose true 时自动添加 Connection: close（未显式设置时）。
 * - @return 新建请求字节数组，调用者必须使用 XByteArray_delete_base 释放；失败返回 NULL。
 */
XByteArray* XHttpRequest_toHttp1(const XHttpRequest* self, bool includeConnectionClose);

#endif // XHTTP_ON
#endif /* XPROTOCOL_ON */
#ifdef __cplusplus
}
#endif


/* XClass create API default-memory wrappers. */
#undef XHttpRequest_create
#define XHttpRequest_create() XHttpRequest_create_ex(XCLASS_DEFAULT_MEMORY_TYPE)

#endif /* XHTTPREQUEST_H */
