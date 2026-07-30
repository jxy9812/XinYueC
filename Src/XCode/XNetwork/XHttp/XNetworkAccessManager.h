/**
 * @file       XNetworkAccessManager.h
 * - @brief      Qt QNetworkAccessManager 风格的 HTTP/HTTPS 异步访问管理器。
 * - @details    管理器只依赖 XinYueC 的 XTcpSocket、XSslSocket、XIODevice 和事件信号；
 *             HTTP 模块不直接调用 POSIX、Win32 或其他平台网络 API。
 */

#ifndef XNETWORKACCESSMANAGER_H
#define XNETWORKACCESSMANAGER_H

#ifdef __cplusplus
extern "C" {
#endif

#include "XHttpReply.h"
#include "XHttpAuthenticator.h"
#include "XHttpMultipart.h"
#include "XNetworkCookie.h"
#include "XHstsPolicy.h"
#include "XNetworkCache.h"
#include "XNetworkProxy.h"
#include "XSslSocket.h"
#include "XObject.h"
#include "XVector.h"
#include <stdbool.h>
#include <stddef.h>

XCLASS_DEFINE_BEGING(XNetworkAccessManager)
XCLASS_DEFINE_EXTEND_END(XNetworkAccessManager, XObject)

/**
 * - @brief HTTP 请求操作类型。
 * - @details 枚举值和 Qt QNetworkAccessManager::Operation 的公开语义一致。
 */
typedef enum XNetworkAccessManager_Operation {
    XNetworkAccessManager_UnknownOperation = 0, /**< 未知操作。 */
    XNetworkAccessManager_HeadOperation = 1,     /**< HEAD 操作。 */
    XNetworkAccessManager_GetOperation,          /**< GET 操作。 */
    XNetworkAccessManager_PutOperation,          /**< PUT 操作。 */
    XNetworkAccessManager_PostOperation,         /**< POST 操作。 */
    XNetworkAccessManager_DeleteOperation,       /**< DELETE 操作。 */
    XNetworkAccessManager_CustomOperation        /**< 自定义方法操作。 */
} XNetworkAccessManager_Operation;

/**
 * - @brief HTTP/HTTPS 网络访问管理器。
 * - @details 管理器拥有活动事务容器和可选代理配置；返回的 XHttpReply 默认由调用者
 *          拥有，只有启用 autoDeleteReplies 后才会在完成后延迟删除。
 */
typedef struct XNetworkAccessManager {
    XObject m_class;                    /**< 第一个成员，继承 XObject。 */
    XVector* m_transactions;            /**< 内部事务指针列表，由管理器拥有。 */
    XVector* m_http2Connections;        /**< HTTP/2 共享连接列表，由管理器拥有。 */
    XNetworkProxy* m_proxy;             /**< 可选代理配置，由管理器拥有。 */
    XNetworkCookieJar* m_cookieJar;     /**< CookieJar；由管理器拥有，可为空。 */
    XNetworkDiskCache* m_cache;          /**< 网络缓存；由管理器拥有，可为空。 */
    XVector* m_authenticationCache;      /**< 认证凭据缓存；管理器拥有，按来源/realm/方案隔离。 */
    XVector* m_hstsPolicies;             /**< HSTS 策略指针列表；由管理器拥有。 */
    XByteArray* m_hstsStoreDirectory;    /**< HSTS 持久化目录；由管理器拥有。 */
    int m_transferTimeout;              /**< 默认传输超时，单位毫秒。 */
    XHttpRequest_RedirectPolicy m_redirectPolicy; /**< 默认重定向策略。 */
    bool m_autoDeleteReplies;            /**< 是否在完成后延迟删除响应。 */
    bool m_hstsEnabled;                  /**< 是否启用 HSTS 自动升级和响应学习。 */
    bool m_hstsStoreEnabled;             /**< 是否启用 HSTS 持久化开关。 */
    XSslPeerVerifyMode m_sslPeerVerifyMode; /**< HTTPS 对端证书校验模式。 */
    XSslCertificate* m_sslCaCertificate; /**< HTTPS 校验 CA；所有权由管理器持有。 */
    bool m_deinitializing;              /**< 是否正在反初始化。 */
} XNetworkAccessManager;

/**
 * - @brief 初始化管理器虚函数表。
 * - @return 共享虚函数表；失败返回 NULL。
 */
XVtable* XNetworkAccessManager_class_init(void);

/**
 * - @brief 初始化管理器。
 * - @param self 待初始化对象；不能为 NULL。
 * - @return 无；默认超时为 30000 毫秒、手动重定向、不自动删除响应。
 */
void XNetworkAccessManager_init(XNetworkAccessManager* self);

/**
 * - @brief 创建管理器。
 * - @return 新对象，调用者必须使用 XNetworkAccessManager_delete_base 释放；失败返回 NULL。
 */
XNetworkAccessManager* XNetworkAccessManager_create(void);

/**
 * - @brief 反初始化、删除和延迟删除入口。
 * - @details 删除管理器时会取消所有活动响应并断开底层套接字连接；返回给调用者的响应
 *          默认仍需由调用者释放。
 */
#define XNetworkAccessManager_deinit_base XClass_deinit_base
#define XNetworkAccessManager_delete_base XClass_delete_base
#define XNetworkAccessManager_deleteLater XObject_deleteLater
#define XNetworkAccessManager_deinitLater XObject_deinitLater

/**
 * - @brief 发送任意操作。
 * - @param self 管理器；不能为 NULL。
 * - @param operation 操作类型；CustomOperation 时必须提供 customMethod。
 * - @param request 请求对象；借用，函数执行时深拷贝；不能为 NULL。
 * - @param body 请求体；借用，函数执行时深拷贝；无请求体传 NULL。
 * - @param customMethod 自定义方法字节；借用，CustomOperation 时必须提供。
 * - @return 新响应对象，默认由调用者使用 XHttpReply_delete_base 释放；失败返回 NULL。
 */
XHttpReply* XNetworkAccessManager_sendRequest(XNetworkAccessManager* self,
                                              XNetworkAccessManager_Operation operation,
                                              const XHttpRequest* request,
                                              const XByteArray* body,
                                              const XByteArray* customMethod);

/**
 * - @brief 发送 HEAD 请求。
 * - @param self 管理器；不能为 NULL。
 * - @param request 请求；借用，函数执行时深拷贝；不能为 NULL。
 * - @return 新响应对象，调用者负责释放；失败返回 NULL。
 */
XHttpReply* XNetworkAccessManager_head(XNetworkAccessManager* self, const XHttpRequest* request);

/**
 * - @brief 发送 GET 请求。
 * - @param self 管理器；不能为 NULL。
 * - @param request 请求；借用，函数执行时深拷贝；不能为 NULL。
 * - @return 新响应对象，调用者负责释放；失败返回 NULL。
 */
XHttpReply* XNetworkAccessManager_get(XNetworkAccessManager* self, const XHttpRequest* request);

/**
 * - @brief 发送 POST 请求。
 * - @param self 管理器；不能为 NULL。
 * - @param request 请求；借用，函数执行时深拷贝；不能为 NULL。
 * - @param body 请求体；借用，函数执行时深拷贝；可为 NULL。
 * - @return 新响应对象，调用者负责释放；失败返回 NULL。
 */
XHttpReply* XNetworkAccessManager_post(XNetworkAccessManager* self,
                                       const XHttpRequest* request,
                                       const XByteArray* body);

/**
 * - @brief 发送 multipart/form-data 等 MIME 请求体。
 * - @param self 管理器；不能为 NULL。
 * - @param request 请求；借用，函数执行时深拷贝；不能为 NULL。
 * - @param multipart multipart 对象；借用，函数执行时生成完整 body；不能为 NULL。
 * - @return 新响应对象，调用者负责释放；生成 body 或发送失败返回 NULL。
 * - @note 若 request 尚未设置 Content-Type，则自动设置 multipart 的 Content-Type；已有值保留。
 */
XHttpReply* XNetworkAccessManager_postMultipart(XNetworkAccessManager* self,
                                                const XHttpRequest* request,
                                                const XHttpMultiPart* multipart);

/**
 * - @brief 发送 PUT 请求。
 * - @param self 管理器；不能为 NULL。
 * - @param request 请求；借用，函数执行时深拷贝；不能为 NULL。
 * - @param body 请求体；借用，函数执行时深拷贝；可为 NULL。
 * - @return 新响应对象，调用者负责释放；失败返回 NULL。
 */
XHttpReply* XNetworkAccessManager_put(XNetworkAccessManager* self,
                                      const XHttpRequest* request,
                                      const XByteArray* body);

/**
 * - @brief 发送 DELETE 请求。
 * - @param self 管理器；不能为 NULL。
 * - @param request 请求；借用，函数执行时深拷贝；不能为 NULL。
 * - @return 新响应对象，调用者负责释放；失败返回 NULL。
 */
XHttpReply* XNetworkAccessManager_deleteResource(XNetworkAccessManager* self,
                                                 const XHttpRequest* request);

/**
 * - @brief 发送自定义方法请求。
 * - @param self 管理器；不能为 NULL。
 * - @param request 请求；借用，函数执行时深拷贝；不能为 NULL。
 * - @param customMethod 自定义 token；借用，函数执行时深拷贝；不能为 NULL。
 * - @param body 请求体；借用，函数执行时深拷贝；可为 NULL。
 * - @return 新响应对象，调用者负责释放；失败返回 NULL。
 */
XHttpReply* XNetworkAccessManager_sendCustomRequest(XNetworkAccessManager* self,
                                                    const XHttpRequest* request,
                                                    const XByteArray* customMethod,
                                                    const XByteArray* body);

/**
 * - @brief 设置默认传输超时。
 * - @param self 管理器；不能为 NULL。
 * - @param timeout 超时毫秒数；负数按 0 处理，0 表示使用底层默认值。
 * - @return 无。
 */
void XNetworkAccessManager_setTransferTimeout(XNetworkAccessManager* self, int timeout);

/**
 * - @brief 获取默认传输超时。
 * - @param self 管理器；NULL 时返回 30000。
 * - @return 超时毫秒数。
 */
int XNetworkAccessManager_transferTimeout(const XNetworkAccessManager* self);

/**
 * - @brief 设置默认重定向策略。
 * - @param self 管理器；不能为 NULL。
 * - @param policy Qt QNetworkRequest 重定向策略。
 * - @return 无。
 */
void XNetworkAccessManager_setRedirectPolicy(XNetworkAccessManager* self,
                                             XHttpRequest_RedirectPolicy policy);

/**
 * - @brief 获取默认重定向策略。
 * - @param self 管理器；NULL 时返回手动策略。
 * - @return 当前默认策略。
 */
XHttpRequest_RedirectPolicy XNetworkAccessManager_redirectPolicy(const XNetworkAccessManager* self);

/**
 * - @brief 设置是否自动延迟删除响应。
 * - @param self 管理器；不能为 NULL。
 * - @param enabled true 表示响应完成后由事件系统延迟删除。
 * - @return 无。
 */
void XNetworkAccessManager_setAutoDeleteReplies(XNetworkAccessManager* self, bool enabled);

/**
 * - @brief 查询是否自动删除响应。
 * - @param self 管理器；NULL 时返回 false。
 * - @return 是否自动删除。
 */
bool XNetworkAccessManager_autoDeleteReplies(const XNetworkAccessManager* self);

/**
 * - @brief 设置 HTTPS 对端证书校验模式。
 * - @param self 管理器；不能为 NULL。
 * - @param mode 校验模式；默认值为 XSSL_VerifyPeer。
 * - @return 无；只影响后续创建的 HTTPS 连接。
 */
void XNetworkAccessManager_setSslPeerVerifyMode(XNetworkAccessManager* self,
                                                XSslPeerVerifyMode mode);

/**
 * - @brief 获取 HTTPS 对端证书校验模式。
 * - @param self 管理器；NULL 时返回 XSSL_VerifyPeer。
 * - @return 当前校验模式。
 */
XSslPeerVerifyMode XNetworkAccessManager_sslPeerVerifyMode(
    const XNetworkAccessManager* self);

/**
 * - @brief 设置 HTTPS 信任 CA 证书。
 * - @param self 管理器；不能为 NULL。
 * - @param certificate CA 证书对象；所有权转移给管理器，可为 NULL 清除当前 CA；
 *        通常由 XSsl_certificateLoad 或 XSsl_certificateFromPem 创建。
 * - @return 成功返回 true；self 为 NULL 返回 false。
 * - @note 只影响后续创建的 HTTPS 连接，默认仍执行 XSSL_VerifyPeer 校验。
 */
bool XNetworkAccessManager_setSslCaCertificate(XNetworkAccessManager* self,
                                               XSslCertificate* certificate);

/**
 * - @brief 获取当前 HTTPS 信任 CA。
 * - @param self 管理器；可为 NULL。
 * - @return 管理器内部借用指针；调用者不得释放，管理器销毁或替换后失效。
 */
const XSslCertificate* XNetworkAccessManager_sslCaCertificate_const(
    const XNetworkAccessManager* self);

/**
 * - @brief 设置管理器代理配置。
 * - @param self 管理器；不能为 NULL。
 * - @param proxy 代理配置；借用，函数执行时深拷贝；NULL 清除自定义代理。
 * - @return 成功返回 true；分配失败时保留原配置。
 */
bool XNetworkAccessManager_setProxy(XNetworkAccessManager* self, const XNetworkProxy* proxy);

/**
 * - @brief 获取管理器代理配置借用指针。
 * - @param self 管理器；NULL 时返回 NULL。
 * - @return 内部代理配置；管理器修改或销毁后失效，调用者不得释放。
 */
const XNetworkProxy* XNetworkAccessManager_proxy_const(const XNetworkAccessManager* self);

/**
 * - @brief 设置 CookieJar。
 * - @param self 管理器；不能为 NULL。
 * - @param cookieJar 新 Jar；所有权转移给管理器，可为 NULL 清除 Jar。
 * - @return 成功返回 true；参数无效返回 false。
 */
bool XNetworkAccessManager_setCookieJar(XNetworkAccessManager* self, XNetworkCookieJar* cookieJar);

/**
 * - @brief 获取当前 CookieJar 借用指针。
 * - @param self 管理器；可为 NULL。
 * - @return 内部 Jar；调用者不能释放，管理器销毁或替换后失效。
 */
XNetworkCookieJar* XNetworkAccessManager_cookieJar(const XNetworkAccessManager* self);

/**
 * - @brief 设置网络缓存。
 * - @param self 管理器；不能为 NULL。
 * - @param cache 缓存对象；所有权转移给管理器，可为 NULL 清除缓存。
 * - @return 成功返回 true；self 为 NULL 返回 false。
 */
bool XNetworkAccessManager_setCache(XNetworkAccessManager* self, XNetworkDiskCache* cache);

/**
 * - @brief 获取当前网络缓存。
 * - @param self 网络访问管理器；可为 NULL。
 * - @return 当前网络缓存借用指针；self 或缓存为空时返回 NULL，调用者不得释放。
 */
XNetworkDiskCache* XNetworkAccessManager_cache(const XNetworkAccessManager* self);

/**
 * - @brief 清除当前网络缓存条目。
 * - @param self 网络访问管理器；可为 NULL，NULL 时不执行。
 */
void XNetworkAccessManager_clearAccessCache(XNetworkAccessManager* self);

/**
 * - @brief 清除 HTTP/1 与 HTTP/2 连接复用状态。
 * - @param self 网络访问管理器；可为 NULL，NULL 时不执行。
 */
void XNetworkAccessManager_clearConnectionCache(XNetworkAccessManager* self);

/**
 * - @brief 启用或禁用 HSTS。
 * - @param self 管理器；不能为 NULL。
 * - @param enabled true 时 HTTP 请求按策略升级为 HTTPS，并学习响应策略。
 * - @return 无。
 */
void XNetworkAccessManager_setStrictTransportSecurityEnabled(XNetworkAccessManager* self,
                                                             bool enabled);

/**
 * - @brief 查询 HSTS 是否启用。
 * - @param self 管理器；可为 NULL。
 * - @return 启用返回 true；NULL 返回 false。
 */
bool XNetworkAccessManager_isStrictTransportSecurityEnabled(const XNetworkAccessManager* self);

/**
 * - @brief 设置 HSTS 存储开关和目录。
 * - @param self 管理器；不能为 NULL。
 * - @param enabled 是否启用持久化存储。
 * - @param directory 目录路径；借用，函数执行时深拷贝；NULL 表示空目录。
 * - @return 成功返回 true；内存不足或 self 为 NULL 返回 false。
 * - @note 当前实现保留 Qt 的开关和目录语义，策略生命周期仍由当前管理器维护。
 */
bool XNetworkAccessManager_enableStrictTransportSecurityStore(XNetworkAccessManager* self,
                                                             bool enabled,
                                                             const XByteArray* directory);

/**
 * - @brief 查询 HSTS 存储开关。
 * - @param self 管理器；可为 NULL。
 * - @return 存储启用返回 true；NULL 返回 false。
 */
bool XNetworkAccessManager_isStrictTransportSecurityStoreEnabled(const XNetworkAccessManager* self);

/**
 * - @brief 添加或替换已知 HSTS 策略。
 * - @param self 管理器；不能为 NULL。
 * - @param policies XVector，元素类型为 XHstsPolicy*；借用，函数深拷贝。
 * - @return 成功返回 true；参数无效或内存不足返回 false。
 */
bool XNetworkAccessManager_addStrictTransportSecurityHosts(XNetworkAccessManager* self,
                                                            const XVector* policies);

/**
 * - @brief 获取当前未过期 HSTS 策略副本。
 * - @param self 管理器；可为 NULL。
 * - @return XVector，元素类型为 XHstsPolicy*；使用 XHstsPolicy_list_free 释放。
 */
XVector* XNetworkAccessManager_strictTransportSecurityHosts(const XNetworkAccessManager* self);

/**
 * - @brief 获取活动响应数量。
 * - @param self 管理器；NULL 时返回 0。
 * - @return 尚未完成的响应数量。
 */
size_t XNetworkAccessManager_activeReplyCount(const XNetworkAccessManager* self);

/**
 * - @brief manager.finished 信号。
 * - @param self 信号发送对象；可为 NULL，仅用于获取信号标识。
 * - @param reply 已完成响应；信号参数为 XHttpReply*，不转移所有权。
 * - @return 信号句柄。
 */
void* XNetworkAccessManager_finished_signal(XNetworkAccessManager* self, XHttpReply* reply);

/**
 * - @brief 服务器认证挑战信号，对齐 QNetworkAccessManager::authenticationRequired。
 * - @param self 管理器；信号发送对象。
 * - @param reply 收到 401 的响应；借用，认证回调期间有效。
 * - @param authenticator 认证器；借用，直接连接的槽函数填写用户名和密码以请求重发。
 * - @return 信号句柄。
 * - @warning 认证器仅在本次同步信号内有效；排队连接不能用于填写重发凭据。
 */
void* XNetworkAccessManager_authenticationRequired_signal(
    XNetworkAccessManager* self, XHttpReply* reply, XHttpAuthenticator* authenticator);

/**
 * - @brief 代理认证挑战信号，对齐 QNetworkAccessManager::proxyAuthenticationRequired。
 * - @param self 管理器；信号发送对象。
 * - @param proxy 当前代理配置；借用，可为 NULL。
 * - @param authenticator 认证器；借用，直接连接的槽函数填写用户名和密码以请求重发。
 * - @return 信号句柄。
 * - @warning 认证器仅在本次同步信号内有效；排队连接不能用于填写重发凭据。
 */
void* XNetworkAccessManager_proxyAuthenticationRequired_signal(
    XNetworkAccessManager* self, XNetworkProxy* proxy, XHttpAuthenticator* authenticator);

#ifdef __cplusplus
}
#endif

#endif /* XNETWORKACCESSMANAGER_H */
