/**
 * @file       XHttpServer.h
 * - @brief      HTTP/1 服务端公共 API，对标 Qt HttpServer 的可移植 C 接口。
 * - @details    服务端基于 XTcpServer 和 XObject 事件，不直接调用平台网络 API。
 */

#ifndef XHTTPSERVER_H
#define XHTTPSERVER_H
#include "XHttp_config.h"

#ifdef __cplusplus
extern "C" {
#endif
#if XPROTOCOL_ON
#if XHTTP_ON

#include "XClass.h"
#include "XHttpHeaders.h"
#include "XObject.h"
#include "XTcpServer.h"
#include "XUrl.h"
#include "XVector.h"
#include <stdbool.h>
#include <stdint.h>

/** @brief HTTP 服务端请求方法。 */
typedef enum XHttpServerRequest_Method {
    XHttpServerRequest_Unknown = 0,
    XHttpServerRequest_Get,
    XHttpServerRequest_Put,
    XHttpServerRequest_Delete,
    XHttpServerRequest_Post,
    XHttpServerRequest_Head,
    XHttpServerRequest_Options,
    XHttpServerRequest_Patch,
    XHttpServerRequest_Connect,
    XHttpServerRequest_Trace
} XHttpServerRequest_Method;

/**
 * - @brief 将请求方法转换为路由方法掩码。
 * - @param method 请求方法枚举值。
 * - @return 对应的单方法掩码；Unknown 返回0。
 */
#define XHttpServerRequest_methodFlag(method) \
    (((method) > XHttpServerRequest_Unknown && \
      (method) <= XHttpServerRequest_Trace) ? \
        (UINT32_C(1) << ((unsigned)(method) - 1U)) : UINT32_C(0))

/** @brief 匹配 Qt AnyKnown 的全部已知 HTTP 方法。 */
#define XHttpServerRequest_AnyKnownMethods \
    (XHttpServerRequest_methodFlag(XHttpServerRequest_Get) | \
     XHttpServerRequest_methodFlag(XHttpServerRequest_Put) | \
     XHttpServerRequest_methodFlag(XHttpServerRequest_Delete) | \
     XHttpServerRequest_methodFlag(XHttpServerRequest_Post) | \
     XHttpServerRequest_methodFlag(XHttpServerRequest_Head) | \
     XHttpServerRequest_methodFlag(XHttpServerRequest_Options) | \
     XHttpServerRequest_methodFlag(XHttpServerRequest_Patch) | \
     XHttpServerRequest_methodFlag(XHttpServerRequest_Connect) | \
     XHttpServerRequest_methodFlag(XHttpServerRequest_Trace))

/** @brief HTTP 服务端响应状态码。 */
typedef enum XHttpServerResponse_StatusCode {
    XHttpServerResponse_Continue = 100,
    XHttpServerResponse_SwitchingProtocols = 101,
    XHttpServerResponse_Processing = 102,
    XHttpServerResponse_Ok = 200,
    XHttpServerResponse_Created = 201,
    XHttpServerResponse_Accepted = 202,
    XHttpServerResponse_NonAuthoritativeInformation = 203,
    XHttpServerResponse_NoContent = 204,
    XHttpServerResponse_ResetContent = 205,
    XHttpServerResponse_PartialContent = 206,
    XHttpServerResponse_MultiStatus = 207,
    XHttpServerResponse_AlreadyReported = 208,
    XHttpServerResponse_IMUsed = 226,
    XHttpServerResponse_MultipleChoices = 300,
    XHttpServerResponse_MovedPermanently = 301,
    XHttpServerResponse_Found = 302,
    XHttpServerResponse_SeeOther = 303,
    XHttpServerResponse_NotModified = 304,
    XHttpServerResponse_UseProxy = 305,
    XHttpServerResponse_TemporaryRedirect = 307,
    XHttpServerResponse_PermanentRedirect = 308,
    XHttpServerResponse_BadRequest = 400,
    XHttpServerResponse_Unauthorized = 401,
    XHttpServerResponse_PaymentRequired = 402,
    XHttpServerResponse_Forbidden = 403,
    XHttpServerResponse_NotFound = 404,
    XHttpServerResponse_MethodNotAllowed = 405,
    XHttpServerResponse_NotAcceptable = 406,
    XHttpServerResponse_ProxyAuthenticationRequired = 407,
    XHttpServerResponse_RequestTimeout = 408,
    XHttpServerResponse_Conflict = 409,
    XHttpServerResponse_Gone = 410,
    XHttpServerResponse_LengthRequired = 411,
    XHttpServerResponse_PreconditionFailed = 412,
    XHttpServerResponse_PayloadTooLarge = 413,
    XHttpServerResponse_UriTooLong = 414,
    XHttpServerResponse_UnsupportedMediaType = 415,
    XHttpServerResponse_RequestRangeNotSatisfiable = 416,
    XHttpServerResponse_ExpectationFailed = 417,
    XHttpServerResponse_ImATeapot = 418,
    XHttpServerResponse_MisdirectedRequest = 421,
    XHttpServerResponse_UnprocessableEntity = 422,
    XHttpServerResponse_Locked = 423,
    XHttpServerResponse_FailedDependency = 424,
    XHttpServerResponse_UpgradeRequired = 426,
    XHttpServerResponse_PreconditionRequired = 428,
    XHttpServerResponse_TooManyRequests = 429,
    XHttpServerResponse_RequestHeaderFieldsTooLarge = 431,
    XHttpServerResponse_UnavailableForLegalReasons = 451,
    XHttpServerResponse_InternalServerError = 500,
    XHttpServerResponse_NotImplemented = 501,
    XHttpServerResponse_BadGateway = 502,
    XHttpServerResponse_ServiceUnavailable = 503,
    XHttpServerResponse_GatewayTimeout = 504,
    XHttpServerResponse_HttpVersionNotSupported = 505,
    XHttpServerResponse_VariantAlsoNegotiates = 506,
    XHttpServerResponse_InsufficientStorage = 507,
    XHttpServerResponse_LoopDetected = 508,
    XHttpServerResponse_NotExtended = 510,
    XHttpServerResponse_NetworkAuthenticationRequired = 511,
    XHttpServerResponse_NetworkConnectTimeoutError = 599
} XHttpServerResponse_StatusCode;

XCLASS_DEFINE_BEGING(XHttpServerRequest)
XCLASS_DEFINE_EXTEND_END(XHttpServerRequest, XClass)

/** @brief 服务端请求对象；回调执行期间只读使用。 */
typedef struct XHttpServerRequest {
    XClass m_class;                         /**< 第一个成员，继承 XClass。 */
    XUrl* m_url;                            /**< 请求 URL；对象拥有。 */
    XHttpHeaders* m_headers;                /**< 请求头；对象拥有。 */
    XByteArray* m_body;                     /**< 请求正文；对象拥有。 */
    XHttpServerRequest_Method m_method;     /**< 请求方法。 */
    uint16_t m_remotePort;                  /**< 对端端口。 */
    uint16_t m_localPort;                   /**< 本地端口。 */
} XHttpServerRequest;

/**
 * - @brief 初始化服务端请求虚函数表。
 * - @return 服务端请求虚函数表；初始化失败返回 NULL，不由调用者释放。
 */
XVtable* XHttpServerRequest_class_init(void);
/**
 * - @brief 创建服务端请求对象。
 * - @return 新建请求对象；调用者必须使用 XHttpServerRequest_delete_base 释放，分配失败返回 NULL。
 */
XHttpServerRequest* XHttpServerRequest_create(void);
#define XHttpServerRequest_deinit_base XClass_deinit_base
#define XHttpServerRequest_delete_base XClass_delete_base
/**
 * - @brief 获取请求 URL。
 * - @param self 服务端请求；可为 NULL。
 * - @return 内部 URL 借用指针；self 为空时返回 NULL，调用者不得释放或修改。
 */
const XUrl* XHttpServerRequest_url_const(const XHttpServerRequest* self);
/**
 * - @brief 获取请求查询字符串。
 * - @param self 服务端请求；可为 NULL。
 * - @return 内部查询字符串借用指针；self、URL 或查询字符串为空时返回 NULL，调用者不得释放或修改。
 */
const XString* XHttpServerRequest_query_const(const XHttpServerRequest* self);
/**
 * - @brief 获取请求方法。
 * - @param self 服务端请求；可为 NULL。
 * - @return 请求方法；self 为空时返回 XHttpServerRequest_Unknown。
 */
XHttpServerRequest_Method XHttpServerRequest_method(const XHttpServerRequest* self);
/**
 * - @brief 获取请求头。
 * - @param self 服务端请求；可为 NULL。
 * - @return 内部请求头借用指针；self 为空时返回 NULL，调用者不得释放或修改。
 */
const XHttpHeaders* XHttpServerRequest_headers_const(const XHttpServerRequest* self);
/**
 * - @brief 获取请求正文。
 * - @param self 服务端请求；可为 NULL。
 * - @return 内部请求正文借用指针；self 为空时返回 NULL，调用者不得释放或修改。
 */
const XByteArray* XHttpServerRequest_body_const(const XHttpServerRequest* self);
/**
 * - @brief 获取指定请求头值副本。
 * - @param self 服务端请求；可为 NULL。
 * - @param key 请求头名称；借用且不能为 NULL，比较忽略 ASCII 大小写。
 * - @return 新建请求头值；调用者必须使用 XByteArray_delete_base 释放，未找到、参数无效或分配失败返回 NULL。
 */
XByteArray* XHttpServerRequest_value(const XHttpServerRequest* self, const XByteArray* key);
/**
 * - @brief 获取对端端口。
 * - @param self 服务端请求；可为 NULL。
 * - @return 对端端口；self 为空时返回 0。
 */
uint16_t XHttpServerRequest_remotePort(const XHttpServerRequest* self);
/**
 * - @brief 获取本地端口。
 * - @param self 服务端请求；可为 NULL。
 * - @return 本地端口；self 为空时返回 0。
 */
uint16_t XHttpServerRequest_localPort(const XHttpServerRequest* self);

XCLASS_DEFINE_BEGING(XHttpServerResponse)
XCLASS_DEFINE_EXTEND_END(XHttpServerResponse, XClass)

/** @brief 服务端响应值对象。 */
typedef struct XHttpServerResponse {
    XClass m_class;                         /**< 第一个成员，继承 XClass。 */
    XByteArray* m_body;                     /**< 响应正文；对象拥有。 */
    XByteArray* m_mimeType;                 /**< MIME 类型；对象拥有。 */
    XHttpHeaders* m_headers;                /**< 响应头；对象拥有。 */
    XHttpServerResponse_StatusCode m_statusCode; /**< 状态码。 */
} XHttpServerResponse;

/**
 * - @brief 初始化服务端响应虚函数表。
 * - @return 服务端响应虚函数表；初始化失败返回 NULL，不由调用者释放。
 */
XVtable* XHttpServerResponse_class_init(void);
/**
 * - @brief 创建指定状态码的服务端响应。
 * - @param status HTTP 响应状态码。
 * - @return 新建响应对象；调用者必须使用 XHttpServerResponse_delete_base 释放，分配失败返回 NULL。
 */
XHttpServerResponse* XHttpServerResponse_create_status(XHttpServerResponse_StatusCode status);
/**
 * - @brief 创建带正文的服务端响应。
 * - @param body 响应正文；借用，可为 NULL，创建时深拷贝。
 * - @param status HTTP 响应状态码。
 * - @return 新建响应对象；调用者必须使用 XHttpServerResponse_delete_base 释放，分配失败返回 NULL。
 */
XHttpServerResponse* XHttpServerResponse_create_body(const XByteArray* body,
                                                     XHttpServerResponse_StatusCode status);
/**
 * - @brief 创建带 MIME 类型和正文的服务端响应。
 * - @param mimeType MIME 类型；借用，可为 NULL，创建时深拷贝。
 * - @param body 响应正文；借用，可为 NULL，创建时深拷贝。
 * - @param status HTTP 响应状态码。
 * - @return 新建响应对象；调用者必须使用 XHttpServerResponse_delete_base 释放，分配失败返回 NULL。
 */
XHttpServerResponse* XHttpServerResponse_create_mime(const XByteArray* mimeType,
                                                     const XByteArray* body,
                                                     XHttpServerResponse_StatusCode status);
#define XHttpServerResponse_deinit_base XClass_deinit_base
#define XHttpServerResponse_delete_base XClass_delete_base
/**
 * - @brief 获取响应正文。
 * - @param self 服务端响应；可为 NULL。
 * - @return 内部正文借用指针；self 为空时返回 NULL，调用者不得释放或修改。
 */
const XByteArray* XHttpServerResponse_data_const(const XHttpServerResponse* self);
/**
 * - @brief 获取响应 MIME 类型。
 * - @param self 服务端响应；可为 NULL。
 * - @return 内部 MIME 类型借用指针；未设置或 self 为空时返回 NULL，调用者不得释放或修改。
 */
const XByteArray* XHttpServerResponse_mimeType_const(const XHttpServerResponse* self);
/**
 * - @brief 获取响应状态码。
 * - @param self 服务端响应；可为 NULL。
 * - @return HTTP 响应状态码；self 为空时返回 XHttpServerResponse_InternalServerError。
 */
XHttpServerResponse_StatusCode XHttpServerResponse_statusCode(const XHttpServerResponse* self);
/**
 * - @brief 获取可修改的响应头。
 * - @param self 服务端响应；可为 NULL。
 * - @return 内部响应头借用指针；self 为空时返回 NULL，调用者不得单独释放。
 */
XHttpHeaders* XHttpServerResponse_headers(XHttpServerResponse* self);
/**
 * - @brief 替换响应头。
 * - @param self 服务端响应；不能为 NULL。
 * - @param headers 响应头；借用，可为 NULL，非 NULL 时深拷贝。
 * - @return 设置成功返回 true；self 无效或拷贝失败返回 false。
 */
bool XHttpServerResponse_setHeaders(XHttpServerResponse* self, const XHttpHeaders* headers);

typedef struct XHttpServerResponder XHttpServerResponder;
/**
 * - @brief 服务端请求处理回调。
 * - @param request 请求只读借用对象；仅在回调期间有效。
 * - @param responder 响应器借用对象；回调中可写一次响应。
 * - @param context 注册回调时提供的上下文；服务器不拥有。
 */
typedef void (*XHttpServer_Handler)(const XHttpServerRequest* request,
                                    XHttpServerResponder* responder,
                                    void* context);

/**
 * - @brief 路由规则处理回调。
 * - @param request 请求只读借用对象；仅在回调期间有效。
 * - @param responder 响应器借用对象；回调中可写一次响应。
 * - @param context 注册规则时提供的上下文；路由器不拥有。
 */
typedef void (*XHttpServer_RouteHandler)(const XHttpServerRequest* request,
                                         XHttpServerResponder* responder,
                                         void* context);

typedef struct XHttpServerRouter XHttpServerRouter;
typedef struct XHttpServerRouterRule XHttpServerRouterRule;

/** @brief 请求响应器；由服务端回调期间临时提供。 */
struct XHttpServerResponder {
    XObject* m_server;             /**< 服务端借用指针。 */
    XObject* m_socket;             /**< 套接字借用指针。 */
    XHttpServerRequest_Method m_method; /**< 当前请求方法，用于 HEAD 无正文语义。 */
    bool m_sent;                   /**< 是否已发送响应。 */
};

/**
 * - @brief 发送完整服务端响应。
 * - @param responder 当前请求的响应器；借用且不能为 NULL，仅在请求处理回调期间有效。
 * - @param response 服务端响应值对象；借用且不能为 NULL，调用期间保持有效。
 * - @return 响应写入队列成功返回 true；响应器已发送、参数无效或写出失败返回 false。
 */
bool XHttpServerResponder_sendResponse(XHttpServerResponder* responder,
                                       const XHttpServerResponse* response);
/**
 * - @brief 构造并发送服务端响应。
 * - @param responder 当前请求的响应器；借用且不能为 NULL，仅在请求处理回调期间有效。
 * - @param body 响应正文；借用，可为 NULL，调用期间保持有效。
 * - @param mimeType MIME 类型；借用，可为 NULL，调用期间保持有效。
 * - @param status HTTP 响应状态码。
 * - @return 响应写入队列成功返回 true；响应器已发送、参数无效或写出失败返回 false。
 */
bool XHttpServerResponder_write(XHttpServerResponder* responder,
                                const XByteArray* body,
                                const XByteArray* mimeType,
                                XHttpServerResponse_StatusCode status);
/**
 * - @brief 发送不含正文的服务端响应。
 * - @param responder 当前请求的响应器；借用且不能为 NULL，仅在请求处理回调期间有效。
 * - @param status HTTP 响应状态码。
 * - @return 响应写入队列成功返回 true；响应器已发送、参数无效或写出失败返回 false。
 */
bool XHttpServerResponder_writeStatus(XHttpServerResponder* responder,
                                      XHttpServerResponse_StatusCode status);

XCLASS_DEFINE_BEGING(XHttpServer)
XCLASS_DEFINE_EXTEND_END(XHttpServer, XObject)

/** @brief HTTP/1 服务端。 */
typedef struct XHttpServer {
    XObject m_class;                       /**< 第一个成员，继承 XObject。 */
    XTcpServer* m_tcpServer;               /**< TCP 监听器；对象拥有。 */
    XVector* m_connections;                /**< 活动连接列表；对象拥有。 */
    XHttpServer_Handler m_handler;         /**< 请求处理器；不拥有。 */
    void* m_handlerContext;                /**< 处理器上下文；不拥有。 */
    XHttpServerRouter* m_router;            /**< 路由器；对象拥有。 */
    XHttpServer_Handler m_missingHandler;   /**< 路由未命中处理器；不拥有。 */
    void* m_missingHandlerContext;          /**< 缺省处理器上下文；不拥有。 */
    XConnection* m_newConnection;         /**< newConnection 信号连接。 */
} XHttpServer;

/**
 * - @brief 初始化 HTTP 服务端虚函数表。
 * - @return 服务端虚函数表；初始化失败返回 NULL，不由调用者释放。
 */
XVtable* XHttpServer_class_init(void);
/**
 * - @brief 初始化 HTTP 服务端。
 * - @param self 待初始化的服务端；不能为 NULL。
 */
void XHttpServer_init(XHttpServer* self);
/**
 * - @brief 创建 HTTP 服务端。
 * - @return 新建服务端；调用者必须使用 XHttpServer_delete_base 释放，分配失败返回 NULL。
 */
XHttpServer* XHttpServer_create(void);
#define XHttpServer_deinit_base XClass_deinit_base
#define XHttpServer_delete_base XClass_delete_base
#define XHttpServer_deleteLater XObject_deleteLater

/**
 * - @brief 设置默认请求处理回调。
 * - @param self HTTP 服务端；不能为 NULL。
 * - @param handler 请求处理回调；NULL 表示清除默认处理回调。
 * - @param context 回调上下文；借用，不由服务端释放，必须在回调注册期间保持有效。
 */
void XHttpServer_setHandler(XHttpServer* self, XHttpServer_Handler handler, void* context);
/**
 * - @brief 设置路由未命中时的处理器。
 * - @param self 服务端；不能为 NULL。
 * - @param handler 缺省处理器；NULL 恢复默认404响应。
 * - @param context 上下文借用指针。
 */
void XHttpServer_setMissingHandler(XHttpServer* self,
                                   XHttpServer_Handler handler,
                                   void* context);
/**
 * - @brief 获取服务端路由器。
 * - @param self HTTP 服务端；可为 NULL。
 * - @return 内部路由器借用指针；self 为空时返回 NULL，调用者不得释放或保存到服务端销毁后。
 */
XHttpServerRouter* XHttpServer_router(const XHttpServer* self);
/**
 * - @brief 注册一条路由规则。
 * - @param self 服务端；不能为 NULL。
 * - @param pathPattern 路径模式，例如 /users/<arg>；UTF-8 借用字符串。
 * - @param methods 方法掩码，使用 XHttpServerRequest_methodFlag 或 AnyKnownMethods。
 * - @param handler 路由处理器；不能为 NULL。
 * - @param context 上下文借用指针。
 * - @return 路由器拥有的规则指针，失败返回 NULL。
 */
XHttpServerRouterRule* XHttpServer_route(XHttpServer* self,
                                         const char* pathPattern,
                                         uint32_t methods,
                                         XHttpServer_RouteHandler handler,
                                         void* context);
/**
 * - @brief 开始监听 HTTP 服务端端口。
 * - @param self HTTP 服务端；不能为 NULL。
 * - @param address 监听地址；借用，可为 NULL，NULL 表示任意地址。
 * - @param port 监听端口；0 由系统分配临时端口。
 * - @return 成功开始监听返回 true；服务端状态或地址、端口无效时返回 false。
 */
bool XHttpServer_listen(XHttpServer* self, const XHostAddress* address, uint16_t port);
/**
 * - @brief 判断服务端是否正在监听。
 * - @param self HTTP 服务端；可为 NULL。
 * - @return 正在监听返回 true；self 为空时返回 false。
 */
bool XHttpServer_isListening(const XHttpServer* self);
/**
 * - @brief 获取实际监听端口。
 * - @param self HTTP 服务端；可为 NULL。
 * - @return 实际监听端口；未监听或 self 为空时返回 0。
 */
uint16_t XHttpServer_serverPort(const XHttpServer* self);
/**
 * - @brief 获取底层 TCP 监听器。
 * - @param self HTTP 服务端；可为 NULL。
 * - @return 内部 TCP 监听器借用指针；self 为空时返回 NULL，调用者不得释放。
 */
XTcpServer* XHttpServer_tcpServer(const XHttpServer* self);
/**
 * - @brief 关闭监听并清理活动连接。
 * - @param self HTTP 服务端；可为 NULL，NULL 时不执行。
 */
void XHttpServer_close(XHttpServer* self);

#endif // XHTTP_ON
#endif /* XPROTOCOL_ON */
#ifdef __cplusplus
}
#endif

#endif /* XHTTPSERVER_H */
