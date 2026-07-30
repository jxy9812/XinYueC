/**
 * @file       XHttpReply.h
 * - @brief      HTTP 响应对象及增量 HTTP/1.1 解析 API。
 * - @details    对齐 Qt 6.8 QNetworkReply 的核心状态、错误、头部、响应体和信号；
 *             feed/endOfInput 由网络管理器调用，不直接访问平台套接字。
 */

#ifndef XHTTPREPLY_H
#define XHTTPREPLY_H

#ifdef __cplusplus
extern "C" {
#endif

#include "XHttpHeaders.h"
#include "XHttp2Headers.h"
#include "XHttpRequest.h"
#include "XObject.h"
#include "XString.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

XCLASS_DEFINE_BEGING(XHttpReply)
XCLASS_DEFINE_EXTEND_END(XHttpReply, XObject)

/**
 * - @brief HTTP 响应生命周期状态。
 * - @details 状态语义对齐 QNetworkReply::isFinished/isRunning；解析完成后为 Finished。
 */
typedef enum XHttpReply_State {
    XHttpReply_Idle = 0,        /**< 尚未收到响应数据。 */
    XHttpReply_Receiving,       /**< 正在接收并解析响应。 */
    XHttpReply_Finished         /**< 响应已完整接收或已失败。 */
} XHttpReply_State;

/**
 * - @brief QNetworkReply 风格的网络错误枚举。
 * - @details 数值只在 XinYueC 内稳定；网络管理器应同时提供 errorString。
 */
typedef enum XHttpReply_NetworkError {
    XHttpReply_NoError = 0,                         /**< 没有错误。 */
    XHttpReply_ConnectionRefusedError,              /**< 连接被拒绝。 */
    XHttpReply_RemoteHostClosedError,               /**< 远端关闭连接。 */
    XHttpReply_HostNotFoundError,                   /**< 主机名无法解析。 */
    XHttpReply_TimeoutError,                        /**< 操作超时。 */
    XHttpReply_OperationCanceledError,              /**< 操作被取消。 */
    XHttpReply_SslHandshakeFailedError,             /**< TLS 握手失败。 */
    XHttpReply_TemporaryNetworkError,               /**< 可重试的网络错误。 */
    XHttpReply_NetworkSessionFailedError,           /**< 网络会话失败。 */
    XHttpReply_BackgroundRequestNotAllowedError,    /**< 后台请求不允许。 */
    XHttpReply_UnknownNetworkError,                 /**< 未知网络错误。 */
    XHttpReply_UnknownProxyError,                   /**< 未知代理错误。 */
    XHttpReply_UnknownContentError,                 /**< 未知内容错误。 */
    XHttpReply_ProtocolUnknownError,                /**< 协议未知。 */
    XHttpReply_ProtocolInvalidOperationError,       /**< 协议操作无效。 */
    XHttpReply_ContentAccessDenied,                 /**< 内容访问被拒绝。 */
    XHttpReply_ContentOperationNotPermittedError,   /**< 内容操作不允许。 */
    XHttpReply_ContentNotFoundError,                /**< 内容不存在。 */
    XHttpReply_AuthenticationRequiredError,         /**< 需要认证。 */
    XHttpReply_ContentReSendError,                  /**< 需要重新发送内容。 */
    XHttpReply_ProtocolWrongServerError,            /**< 服务器协议不匹配。 */
    XHttpReply_UnknownServerError,                  /**< 未知服务器错误。 */
    XHttpReply_TooManyRedirectsError,               /**< 重定向次数过多。 */
    XHttpReply_InsecureRedirectError,               /**< 不安全重定向。 */
    XHttpReply_InternalServerError,                 /**< 本地 HTTP 实现错误。 */
    XHttpReply_OperationNotImplementedError,        /**< 操作尚未实现。 */
    XHttpReply_ServiceUnavailableError              /**< 服务不可用。 */
} XHttpReply_NetworkError;

/**
 * - @brief HTTP 响应对象。
 * - @details 对象拥有请求副本、响应头、响应体、解析缓冲和错误字符串；所有成员都由
 *          XHttpReply 的反初始化函数释放，调用者不能直接释放内部指针。
 */
typedef struct XHttpReply {
    XObject m_class;                         /**< 第一个成员，继承 XObject。 */
    XHttpRequest* m_request;                 /**< 请求副本；由对象拥有。 */
    XHttpHeaders* m_headers;                /**< 响应头；由对象拥有。 */
    XHttpHeaders* m_trailers;               /**< chunked 尾部头；由对象拥有。 */
    XByteArray* m_body;                     /**< 已解析响应体；由对象拥有。 */
    XByteArray* m_input;                    /**< 尚未完全解析的原始输入；由对象拥有。 */
    XByteArray* m_reason;                   /**< 状态原因短语；由对象拥有。 */
    XString* m_errorString;                 /**< 错误描述；由对象拥有。 */
    size_t m_parseOffset;                   /**< 原始输入解析游标。 */
    size_t m_contentLength;                 /**< Content-Length 数值。 */
    size_t m_chunkRemaining;               /**< 当前 chunk 尚未读取的字节数。 */
    uint64_t m_bodyReceived;                /**< 已接收的实体字节数。 */
    int m_statusCode;                       /**< HTTP 状态码；未解析时为 0。 */
    XHttpReply_State m_state;               /**< 当前生命周期状态。 */
    XHttpReply_NetworkError m_error;        /**< 当前错误码。 */
    bool m_headerParsed;                    /**< 是否已经解析响应头。 */
    bool m_hasContentLength;                /**< 是否存在合法 Content-Length。 */
    bool m_chunked;                         /**< 是否使用 Transfer-Encoding: chunked。 */
    bool m_chunkNeedCrlf;                   /**< 当前 chunk 数据后是否等待 CRLF。 */
    bool m_chunkTrailer;                    /**< 是否正在读取 chunk 尾部头。 */
    bool m_noBody;                          /**< 当前响应按协议禁止实体。 */
    bool m_closeDelimited;                  /**< 响应体以输入结束作为边界。 */
    bool m_inputEnded;                      /**< 网络层已通知输入结束。 */
    bool m_errorEmitted;                    /**< 是否已经发射错误信号。 */
    bool m_deferFinished;                   /**< 自动重定向时暂缓 finished 信号。 */
    bool m_deferAuthentication;             /**< 管理器是否需要先处理 401/407 认证挑战。 */
    bool m_authenticationPending;           /**< 已收到待管理器处理的 401/407 响应。 */
    bool m_finishedPending;                 /**< 已解析完成但 finished 尚未发射。 */
} XHttpReply;

/**
 * - @brief 初始化 XHttpReply 的虚函数表。
 * - @return 共享虚函数表；失败返回 NULL。
 */
XVtable* XHttpReply_class_init(void);

/**
 * - @brief 初始化响应对象。
 * - @param self 待初始化对象；不能为 NULL。
 * - @param request 请求对象；借用，初始化时深拷贝，可为 NULL。
 * - @return 无。
 */
void XHttpReply_init(XHttpReply* self, const XHttpRequest* request);

/**
 * - @brief 创建响应对象。
 * - @param request 请求对象；借用，创建时深拷贝，可为 NULL。
 * - @return 新对象，调用者必须使用 XHttpReply_delete_base 释放；失败返回 NULL。
 */
XHttpReply* XHttpReply_create(const XHttpRequest* request);

/**
 * - @brief 反初始化、删除和延迟删除入口。
 * - @details 删除函数会释放响应对象及其拥有的全部成员；延迟删除由 XObject 事件系统处理。
 */
#define XHttpReply_deinit_base XClass_deinit_base
#define XHttpReply_delete_base XClass_delete_base
#define XHttpReply_deleteLater XObject_deleteLater
#define XHttpReply_deinitLater XObject_deinitLater

/**
 * - @brief 获取原始请求副本。
 * - @param self 响应对象；NULL 时返回 NULL。
 * - @return 新建请求副本，调用者必须使用 XHttpRequest_delete_base 释放。
 */
XHttpRequest* XHttpReply_request(const XHttpReply* self);

/**
 * - @brief 获取内部请求借用指针。
 * - @param self 响应对象；NULL 时返回 NULL。
 * - @return 内部请求；响应销毁后失效，调用者不得释放。
 */
const XHttpRequest* XHttpReply_request_const(const XHttpReply* self);

/**
 * - @brief 获取响应头借用指针。
 * - @param self 响应对象；NULL 时返回 NULL。
 * - @return 内部响应头；调用者不得释放或修改。
 */
const XHttpHeaders* XHttpReply_headers_const(const XHttpReply* self);

/**
 * - @brief 获取响应尾部头借用指针。
 * - @param self 响应对象；NULL 时返回 NULL。
 * - @return chunked 尾部头；没有尾部头时仍返回空对象。
 */
const XHttpHeaders* XHttpReply_trailers_const(const XHttpReply* self);

/**
 * - @brief 获取状态码。
 * - @param self 响应对象；NULL 时返回 0。
 * - @return HTTP 状态码。
 */
int XHttpReply_statusCode(const XHttpReply* self);
/**
 * - @brief 获取响应 URL 副本。
 * - @param self HTTP 响应对象；可为 NULL。
 * - @return 新建响应 URL；调用者必须使用 XUrl_delete_base 释放，self 或请求 URL 为空时返回 NULL。
 */
XUrl* XHttpReply_url(const XHttpReply* self);
/**
 * - @brief 判断是否存在指定原始响应头。
 * - @param self HTTP 响应对象；可为 NULL。
 * - @param name 响应头名称；借用且不能为 NULL，比较忽略 ASCII 大小写。
 * - @return 存在至少一个同名响应头返回 true；参数无效或未找到返回 false。
 */
bool XHttpReply_hasRawHeader(const XHttpReply* self, const XByteArray* name);
/**
 * - @brief 获取首个同名原始响应头值副本。
 * - @param self HTTP 响应对象；可为 NULL。
 * - @param name 响应头名称；借用且不能为 NULL，比较忽略 ASCII 大小写。
 * - @return 新建响应头值；调用者必须使用 XByteArray_delete_base 释放，未找到或参数无效返回 NULL。
 */
XByteArray* XHttpReply_rawHeader(const XHttpReply* self, const XByteArray* name);
/**
 * - @brief 获取原始响应头名称副本列表。
 * - @param self HTTP 响应对象；可为 NULL。
 * - @return 新建名称列表，元素类型为 XByteArray*；调用者必须使用 XHttpHeaders_values_free 释放，参数无效或分配失败返回 NULL。
 */
XVector* XHttpReply_rawHeaderList(const XHttpReply* self);
/**
 * - @brief 获取响应头深拷贝。
 * - @param self HTTP 响应对象；可为 NULL。
 * - @return 新建响应头对象；调用者必须使用 XHttpHeaders_delete_base 释放，self 为空或拷贝失败返回 NULL。
 */
XHttpHeaders* XHttpReply_headers(const XHttpReply* self);
/**
 * - @brief 获取已知响应头值副本。
 * - @param self HTTP 响应对象；可为 NULL。
 * - @param header 已知响应头枚举；使用 XHttpHeaders_WellKnownHeader 的合法值。
 * - @return 新建响应头值；调用者必须使用 XByteArray_delete_base 释放，未找到、参数无效或拷贝失败返回 NULL。
 */
XByteArray* XHttpReply_headerKnown(const XHttpReply* self,
                                   XHttpHeaders_WellKnownHeader header);

/**
 * - @brief 获取原因短语副本。
 * - @param self 响应对象；NULL 时返回 NULL。
 * - @return 新建字节数组，调用者必须使用 XByteArray_delete_base 释放。
 */
XByteArray* XHttpReply_reasonPhrase(const XHttpReply* self);

/**
 * - @brief 获取响应体借用指针。
 * - @param self 响应对象；NULL 时返回 NULL。
 * - @return 已接收实体数据；响应销毁后失效，调用者不得释放。
 */
const XByteArray* XHttpReply_body_const(const XHttpReply* self);

/**
 * - @brief 读取并消费当前已接收响应体。
 * - @param self 响应对象；NULL 时返回 NULL。
 * - @return 新建响应体副本并清空内部可读数据，调用者必须释放；无数据返回空数组。
 */
XByteArray* XHttpReply_readAll(XHttpReply* self);

/**
 * - @brief 获取当前错误码。
 * - @param self 响应对象；NULL 时返回 UnknownNetworkError。
 * - @return QNetworkReply 风格错误码。
 */
XHttpReply_NetworkError XHttpReply_error(const XHttpReply* self);

/**
 * - @brief 获取错误描述副本。
 * - @param self 响应对象；NULL 时返回 NULL。
 * - @return 新建字符串，调用者必须使用 XString_delete_base 释放。
 */
XString* XHttpReply_errorString(const XHttpReply* self);

/**
 * - @brief 设置网络层错误并结束响应。
 * - @param self 响应对象；不能为 NULL。
 * - @param error 错误码；不能为 NoError。
 * - @param errorString UTF-8 错误描述；借用，函数执行时深拷贝；可为 NULL。
 * - @return 无；已结束响应不会重复发射错误或 finished。
 * - @note 供 XNetworkAccessManager 将 XAbstractSocket 错误映射到 QNetworkReply 错误。
 */
void XHttpReply_setError(XHttpReply* self, XHttpReply_NetworkError error, const char* errorString);

/**
 * - @brief 设置是否延后处理 HTTP 401/407 认证挑战。
 * - @param self 响应对象；不能为 NULL。
 * - @param enabled true 时 401/407 完整响应先保持 finished 待发状态，交由管理器发射认证信号；
 *        false 时立即按 AuthenticationRequiredError 结束。
 * - @return 无；只应在请求开始前由 XNetworkAccessManager 设置。
 */
void XHttpReply_setDeferAuthentication(XHttpReply* self, bool enabled);
/**
 * - @brief 判断响应是否等待管理器处理认证挑战。
 * - @param self 响应对象；可为 NULL。
 * - @return 收到 401 或 407 且结束信号尚未发射时返回 true；否则返回 false。
 */
bool XHttpReply_authenticationPending(const XHttpReply* self);
/**
 * - @brief 在认证器未提供可重发凭据时完成认证错误。
 * - @param self 响应对象；不能为 NULL。
 * - @return 本次完成待处理认证错误返回 true；没有认证挑战时返回 false。
 * - @note 本函数只供 XNetworkAccessManager 在 authenticationRequired 信号返回后调用。
 */
bool XHttpReply_finishAuthenticationChallenge(XHttpReply* self);

/**
 * - @brief 为重定向复用响应对象并替换请求。
 * - @param self 响应对象；不能为 NULL。
 * - @param request 下一跳请求；借用，函数执行时深拷贝；不能为 NULL。
 * - @return 成功返回 true；失败时原响应状态和请求保持不变。
 * - @note 供管理器实现 Qt QNetworkReply 的同一对象重定向行为。
 */
bool XHttpReply_resetForRequest(XHttpReply* self, const XHttpRequest* request);

/**
 * - @brief 判断 finished 信号是否暂缓待发。
 * - @param self 响应对象；可为 NULL。
 * - @return 已完成且等待管理器决定重定向结果时返回 true。
 */
bool XHttpReply_finishedSignalPending(const XHttpReply* self);

/**
 * - @brief 发射暂缓的 finished 信号。
 * - @param self 响应对象；不能为 NULL。
 * - @return 本次确实发射返回 true；没有待发信号返回 false。
 * - @note 仅供网络管理器在自动重定向完成或拒绝后调用。
 */
bool XHttpReply_emitFinished(XHttpReply* self);

/**
 * - @brief 获取 Location 重定向目标。
 * - @param self 响应对象；NULL 或没有合法 Location 时返回 NULL。
 * - @return 新建 URL，调用者必须使用 XUrl_delete_base 释放。
 */
XUrl* XHttpReply_redirectTarget(const XHttpReply* self);

/**
 * - @brief 判断响应是否已结束。
 * - @param self 响应对象；NULL 时返回 false。
 * - @return 已完成或已失败返回 true。
 */
bool XHttpReply_isFinished(const XHttpReply* self);

/**
 * - @brief 判断响应是否仍在运行。
 * - @param self 响应对象；NULL 时返回 false。
 * - @return 已开始且未结束返回 true。
 */
bool XHttpReply_isRunning(const XHttpReply* self);

/**
 * - @brief 增量输入 HTTP 字节。
 * - @param self 响应对象；不能为 NULL。
 * - @param data 输入字节；借用，函数返回后可立即释放。
 * - @param size 输入字节数；允许为 0，但 data 非 NULL 时才读取。
 * - @return 成功解析或等待更多数据返回 true；协议错误、对象已结束或分配失败返回 false。
 */
bool XHttpReply_feed(XHttpReply* self, const void* data, size_t size);

/**
 * - @brief 将一个已解码的 HTTP/2 响应头块交给响应对象。
 * - @param self 响应对象；不能为 NULL，且不能已经完成。
 * - @param headers 已解码头字段列表；借用，调用期间只读，不能为 NULL。
 * - @param endStream 是否同时收到 END_STREAM 标志；为 true 时没有响应体。
 * - @return 成功返回 true；缺少 :status、字段非法或内存不足返回 false。
 * - @note 该入口只处理 HTTP/2 语义，帧拆分和 HPACK 解码由网络管理器完成。
 */
bool XHttpReply_feedHttp2Headers(XHttpReply* self,
                                 const XHttp2HeaderList* headers,
                                 bool endStream);

/**
 * - @brief 将一个 HTTP/2 DATA 帧载荷交给响应对象。
 * - @param self 响应对象；不能为 NULL，且必须已经收到响应头。
 * - @param data DATA 载荷；借用，允许在 size 为 0 时为 NULL。
 * - @param size 载荷字节数；不能超出项目容器可表示范围。
 * - @param endStream 是否同时收到 END_STREAM 标志；为 true 时完成响应。
 * - @return 成功返回 true；状态不允许、数据非法或内存不足返回 false。
 */
bool XHttpReply_feedHttp2Data(XHttpReply* self, const void* data, size_t size,
                              bool endStream);

/**
 * - @brief 通知解析器网络输入已经结束。
 * - @param self 响应对象；不能为 NULL。
 * - @return 响应完整结束返回 true；输入不完整或协议错误返回 false。
 */
bool XHttpReply_endOfInput(XHttpReply* self);

/**
 * - @brief 取消响应。
 * - @param self 响应对象；不能为 NULL。
 * - @return 无；重复取消不会重复发射 finished。
 */
void XHttpReply_abort(XHttpReply* self);

/**
 * - @brief readyRead 信号。
 * - @param reply 信号发送对象；可为 NULL，仅用于获取信号标识。
 * - @return 信号句柄。
 */
void* XHttpReply_readyRead_signal(XHttpReply* reply);

/**
 * - @brief metaDataChanged 信号。
 * - @param reply 信号发送对象；可为 NULL，仅用于获取信号标识。
 * - @return 信号句柄。
 */
void* XHttpReply_metaDataChanged_signal(XHttpReply* reply);

/**
 * - @brief finished 信号。
 * - @param reply 信号发送对象；可为 NULL，仅用于获取信号标识。
 * - @return 信号句柄。
 */
void* XHttpReply_finished_signal(XHttpReply* reply);

/**
 * - @brief errorOccurred 信号。
 * - @param reply 信号发送对象；可为 NULL，仅用于获取信号标识。
 * - @param error 错误码。
 * - @return 信号句柄。
 */
void* XHttpReply_errorOccurred_signal(XHttpReply* reply, XHttpReply_NetworkError error);

/**
 * - @brief downloadProgress 信号。
 * - @param reply 信号发送对象；可为 NULL，仅用于获取信号标识。
 * - @param received 已接收字节数。
 * - @param total 总字节数；未知时为 UINT64_MAX。
 * - @return 信号句柄。
 */
void* XHttpReply_downloadProgress_signal(XHttpReply* reply, uint64_t received, uint64_t total);

/**
 * - @brief redirected 信号。
 * - @param reply 信号发送对象；可为 NULL，仅用于获取信号标识。
 * - @param target 重定向目标；信号调用期间借用，不转移所有权。
 * - @return 信号句柄。
 */
void* XHttpReply_redirected_signal(XHttpReply* reply, const XUrl* target);

#ifdef __cplusplus
}
#endif

#endif /* XHTTPREPLY_H */
