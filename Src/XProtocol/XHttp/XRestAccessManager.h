/**
 * @file       XRestAccessManager.h
 * - @brief      REST 便捷访问管理器，对标 Qt 6.8 QRestAccessManager。
 * - @details    本对象仅包装 XNetworkAccessManager，不直接访问平台网络 API 或拥有底层管理器。
 */

#ifndef XRESTACCESSMANAGER_H
#define XRESTACCESSMANAGER_H
#include "XHttp_config.h"

#ifdef __cplusplus
extern "C" {
#endif
#if XPROTOCOL_ON
#if XHTTP_ON

#include "XNetworkAccessManager.h"
#include "XRestReply.h"

XCLASS_DEFINE_BEGING(XRestAccessManager)
XCLASS_DEFINE_EXTEND_END(XRestAccessManager, XObject)

/** @brief REST 管理器；m_manager 为借用的底层网络管理器。 */
typedef struct XRestAccessManager {
    XObject m_class;                    /**< 第一个成员，由 XObject 管理。 */
    XNetworkAccessManager* m_manager;  /**< 底层管理器；借用，不由 REST 管理器删除。 */
} XRestAccessManager;

/**
 * - @brief 初始化 REST 访问管理器。
 * - @param self 待初始化的 REST 管理器；不能为 NULL。
 * - @param manager 底层网络访问管理器；借用，可为 NULL，必须在 REST 管理器使用期间保持有效。
 */
void XRestAccessManager_init(XRestAccessManager* self, XNetworkAccessManager* manager);
/**
 * - @brief 创建 REST 访问管理器。
 * - @param manager 底层网络访问管理器；借用，可为 NULL，必须在 REST 管理器使用期间保持有效。
 * - @return 新建 REST 管理器；调用者必须使用 XRestAccessManager_delete_base 释放，分配失败返回 NULL。
 */
XRestAccessManager* XRestAccessManager_create_ex(XMemoryType memory,  XNetworkAccessManager* manager);

#define XRestAccessManager_deinit_base XClass_deinit_base
#define XRestAccessManager_delete_base XClass_delete_base
#define XRestAccessManager_deleteLater XObject_deleteLater

/**
 * - @brief 获取底层网络访问管理器。
 * - @param self REST 管理器；可为 NULL。
 * - @return 底层管理器借用指针；self 为空时返回 NULL，不得释放。
 */
XNetworkAccessManager* XRestAccessManager_networkAccessManager(const XRestAccessManager* self);
/**
 * - @brief 发送 DELETE 请求。
 * - @param self REST 管理器；不能为 NULL。
 * - @param request 请求对象；借用且不能为 NULL，调用期间必须保持有效。
 * - @return 新建 HTTP 响应；调用者必须使用 XHttpReply_delete_base 释放，参数无效或发送失败返回 NULL。
 */
XHttpReply* XRestAccessManager_deleteResource(XRestAccessManager* self, const XHttpRequest* request);
/**
 * - @brief 发送 HEAD 请求。
 * - @param self REST 管理器；不能为 NULL。
 * - @param request 请求对象；借用且不能为 NULL，调用期间必须保持有效。
 * - @return 新建 HTTP 响应；调用者必须使用 XHttpReply_delete_base 释放，参数无效或发送失败返回 NULL。
 */
XHttpReply* XRestAccessManager_head(XRestAccessManager* self, const XHttpRequest* request);
/**
 * - @brief 发送 GET 请求。
 * - @param self REST 管理器；不能为 NULL。
 * - @param request 请求对象；借用且不能为 NULL，调用期间必须保持有效。
 * - @return 新建 HTTP 响应；调用者必须使用 XHttpReply_delete_base 释放，参数无效或发送失败返回 NULL。
 */
XHttpReply* XRestAccessManager_get(XRestAccessManager* self, const XHttpRequest* request);
/**
 * - @brief 发送带 body 的 GET 请求。
 * - @param self REST 管理器；不能为 NULL。
 * - @param request 请求对象；借用且不能为 NULL，调用期间必须保持有效。
 * - @param body 请求 body；借用，可为 NULL，发送前由底层复制。
 * - @return 新建 HTTP 响应；调用者必须使用 XHttpReply_delete_base 释放，参数无效或发送失败返回 NULL。
 */
XHttpReply* XRestAccessManager_getWithData(XRestAccessManager* self, const XHttpRequest* request,
                                           const XByteArray* body);
/**
 * - @brief 发送 POST 请求。
 * - @param self REST 管理器；不能为 NULL。
 * - @param request 请求对象；借用且不能为 NULL，调用期间必须保持有效。
 * - @param body 请求 body；借用，可为 NULL，发送前由底层复制。
 * - @return 新建 HTTP 响应；调用者必须使用 XHttpReply_delete_base 释放，参数无效或发送失败返回 NULL。
 */
XHttpReply* XRestAccessManager_post(XRestAccessManager* self, const XHttpRequest* request,
                                    const XByteArray* body);
/**
 * - @brief 发送 JSON POST 请求。
 * - @param self REST 管理器；不能为 NULL。
 * - @param request 请求对象；借用且不能为 NULL，调用期间必须保持有效。
 * - @param json JSON 文档；借用且不能为 NULL，发送前序列化并设置 application/json。
 * - @return 新建 HTTP 响应；调用者必须使用 XHttpReply_delete_base 释放，参数无效或发送失败返回 NULL。
 */
XHttpReply* XRestAccessManager_postJson(XRestAccessManager* self, const XHttpRequest* request,
                                        const XJsonDocument* json);
/**
 * - @brief 发送 PUT 请求。
 * - @param self REST 管理器；不能为 NULL。
 * - @param request 请求对象；借用且不能为 NULL，调用期间必须保持有效。
 * - @param body 请求 body；借用，可为 NULL，发送前由底层复制。
 * - @return 新建 HTTP 响应；调用者必须使用 XHttpReply_delete_base 释放，参数无效或发送失败返回 NULL。
 */
XHttpReply* XRestAccessManager_put(XRestAccessManager* self, const XHttpRequest* request,
                                   const XByteArray* body);
/**
 * - @brief 发送 JSON PUT 请求。
 * - @param self REST 管理器；不能为 NULL。
 * - @param request 请求对象；借用且不能为 NULL，调用期间必须保持有效。
 * - @param json JSON 文档；借用且不能为 NULL，发送前序列化并设置 application/json。
 * - @return 新建 HTTP 响应；调用者必须使用 XHttpReply_delete_base 释放，参数无效或发送失败返回 NULL。
 */
XHttpReply* XRestAccessManager_putJson(XRestAccessManager* self, const XHttpRequest* request,
                                       const XJsonDocument* json);
/**
 * - @brief 发送 PATCH 请求。
 * - @param self REST 管理器；不能为 NULL。
 * - @param request 请求对象；借用且不能为 NULL，调用期间必须保持有效。
 * - @param body 请求 body；借用，可为 NULL，发送前由底层复制。
 * - @return 新建 HTTP 响应；调用者必须使用 XHttpReply_delete_base 释放，参数无效或发送失败返回 NULL。
 */
XHttpReply* XRestAccessManager_patch(XRestAccessManager* self, const XHttpRequest* request,
                                     const XByteArray* body);
/**
 * - @brief 发送 JSON PATCH 请求。
 * - @param self REST 管理器；不能为 NULL。
 * - @param request 请求对象；借用且不能为 NULL，调用期间必须保持有效。
 * - @param json JSON 文档；借用且不能为 NULL，发送前序列化并设置 application/json。
 * - @return 新建 HTTP 响应；调用者必须使用 XHttpReply_delete_base 释放，参数无效或发送失败返回 NULL。
 */
XHttpReply* XRestAccessManager_patchJson(XRestAccessManager* self, const XHttpRequest* request,
                                         const XJsonDocument* json);
/**
 * - @brief 发送 multipart POST 请求。
 * - @param self REST 管理器；不能为 NULL。
 * - @param request 请求对象；借用且不能为 NULL，调用期间必须保持有效。
 * - @param multipart multipart 内容；借用且不能为 NULL，发送前由底层编码。
 * - @return 新建 HTTP 响应；调用者必须使用 XHttpReply_delete_base 释放，参数无效或发送失败返回 NULL。
 */
XHttpReply* XRestAccessManager_postMultipart(XRestAccessManager* self, const XHttpRequest* request,
                                             const XHttpMultiPart* multipart);
/**
 * - @brief 发送自定义 HTTP 方法请求。
 * - @param self REST 管理器；不能为 NULL。
 * - @param request 请求对象；借用且不能为 NULL，调用期间必须保持有效。
 * - @param method 自定义方法名；借用且不能为 NULL，必须是合法 HTTP 方法字节序列。
 * - @param body 请求 body；借用，可为 NULL，发送前由底层复制。
 * - @return 新建 HTTP 响应；调用者必须使用 XHttpReply_delete_base 释放，参数无效或发送失败返回 NULL。
 */
XHttpReply* XRestAccessManager_sendCustomRequest(XRestAccessManager* self,
                                                 const XHttpRequest* request,
                                                 const XByteArray* method,
                                                 const XByteArray* body);
/**
 * - @brief 将 HTTP 响应包装为 REST 响应。
 * - @param self REST 管理器；可为 NULL，当前参数仅用于保持 API 对齐。
 * - @param reply HTTP 响应；借用且不能为 NULL，必须在包装使用期间保持有效。
 * - @return 新建 REST 响应包装；调用者必须使用 XRestReply_delete_base 释放，参数无效或分配失败返回 NULL。
 */
XRestReply* XRestAccessManager_wrapReply(const XRestAccessManager* self, XHttpReply* reply);

#endif // XHTTP_ON
#endif /* XPROTOCOL_ON */
#ifdef __cplusplus
}
#endif


/* XClass create API default-memory wrappers. */
#undef XRestAccessManager_create
#define XRestAccessManager_create(...) XRestAccessManager_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, __VA_ARGS__)

#endif /* XRESTACCESSMANAGER_H */
