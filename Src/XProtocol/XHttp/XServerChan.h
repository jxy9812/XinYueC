/**
 * @file       XServerChan.h
 * - @brief      Server酱消息推送客户端。
 * - @details    封装 SendKey 管理、Server酱 Turbo/Server酱³ 端点选择、
 *             application/x-www-form-urlencoded 请求和 JSON 响应解析。
 *             默认请求使用 Server酱官方 HTTPS 接口；自定义端点仅用于
 *             网关、代理或本地测试，不会改变 SendKey 的保密责任。
 */

#ifndef XSERVERCHAN_H
#define XSERVERCHAN_H
#include "XHttp_config.h"

#ifdef __cplusplus
extern "C" {
#endif
#if XPROTOCOL_ON
#if XHTTP_ON

#include "XClass.h"
#include "XHttpReply.h"
#include "XNetworkAccessManager.h"
#include "XObject.h"
#include "XString.h"
#include "XByteArray.h"
#include <stdbool.h>

XCLASS_DEFINE_BEGING(XServerChanResult)
XCLASS_DEFINE_EXTEND_END(XServerChanResult, XClass)

/**
 * - @brief Server酱结果错误类型。
 * - @details NoError 只表示本地请求、HTTP 响应和 Server酱 JSON code 均成功。
 */
typedef enum XServerChanResult_Error {
    XServerChanResult_NoError = 0,        /**< 请求成功，且 Server酱返回 code 为0。 */
    XServerChanResult_InvalidArgument,     /**< 调用参数无效。 */
    XServerChanResult_InvalidSendKey,     /**< SendKey 为空或格式不受支持。 */
    XServerChanResult_InvalidEndpoint,    /**< 自定义端点 URL 无效。 */
    XServerChanResult_NoEventLoop,        /**< 当前线程没有可处理网络事件的应用事件循环。 */
    XServerChanResult_NetworkError,       /**< TCP、DNS、TLS 或其他网络层错误。 */
    XServerChanResult_Timeout,            /**< 请求在客户端超时。 */
    XServerChanResult_HttpError,          /**< HTTP 状态码不是 2xx。 */
    XServerChanResult_InvalidResponse,    /**< 响应不是包含 code 字段的合法 JSON 对象。 */
    XServerChanResult_ApiError            /**< Server酱返回了非零 code。 */
} XServerChanResult_Error;

/**
 * - @brief Server酱消息发送结果。
 * - @details 结果对象拥有 message 和 responseBody；调用者负责销毁结果对象。
 */
typedef struct XServerChanResult {
    XClass m_class;                    /**< 第一个成员，由 XClass 管理。 */
    int m_apiCode;                     /**< Server酱 JSON 中的 code；未解析时为-1。 */
    int m_httpStatusCode;              /**< HTTP 状态码；未收到响应时为0。 */
    XServerChanResult_Error m_error;   /**< 本地、HTTP 或 Server酱错误类型。 */
    XString* m_message;                /**< Server酱 message 或本地错误消息；对象拥有。 */
    XByteArray* m_responseBody;        /**< 原始响应正文；对象拥有。 */
    bool m_success;                    /**< 是否完成成功，等价于 error 为NoError且code为0。 */
} XServerChanResult;

/**
 * - @brief 初始化结果对象的虚函数表。
 * - @return 共享虚函数表；失败返回 NULL。
 */
XVtable* XServerChanResult_class_init(void);

/**
 * - @brief 初始化结果对象。
 * - @param self 待初始化对象；不能为 NULL。
 * - @return 无；初始化后 code 为-1、HTTP 状态码为0且结果为失败。
 */
void XServerChanResult_init(XServerChanResult* self);

/**
 * - @brief 创建空结果对象。
 * - @return 新对象；调用者必须使用 XServerChanResult_delete_base 释放。
 */
XServerChanResult* XServerChanResult_create_ex(XMemoryType memory);

/**
 * - @brief 深拷贝创建结果对象。
 * - @param other 源结果对象；不能为 NULL。
 * - @return 新对象；失败返回 NULL。
 */
XServerChanResult* XServerChanResult_create_copy(const XServerChanResult* other);

/**
 * - @brief 移动创建结果对象。
 * - @param other 源结果对象；资源转移后保持已初始化但为空的状态。
 * - @return 新对象；失败返回 NULL。
 */
XServerChanResult* XServerChanResult_create_move(XServerChanResult* other);

/** @brief 结果对象的反初始化、删除、拷贝和移动调度入口。 */
#define XServerChanResult_deinit_base XClass_deinit_base
#define XServerChanResult_delete_base XClass_delete_base
#define XServerChanResult_copy_base XClass_copy_base
#define XServerChanResult_move_base XClass_move_base

/**
 * - @brief 获取 Server酱返回的 code。
 * - @param self 结果对象；可为 NULL。
 * - @return code；对象为空时返回-1。
 */
int XServerChanResult_apiCode(const XServerChanResult* self);

/**
 * - @brief 获取 HTTP 状态码。
 * - @param self 结果对象；可为 NULL。
 * - @return HTTP 状态码；对象为空时返回0。
 */
int XServerChanResult_httpStatusCode(const XServerChanResult* self);

/**
 * - @brief 获取结果错误类型。
 * - @param self 结果对象；可为 NULL。
 * - @return 错误类型；对象为空时返回 InvalidArgument。
 */
XServerChanResult_Error XServerChanResult_error(const XServerChanResult* self);

/**
 * - @brief 判断消息是否发送成功。
 * - @param self 结果对象；可为 NULL。
 * - @return Server酱 code 为0且没有本地/HTTP错误时返回 true。
 */
bool XServerChanResult_isSuccess(const XServerChanResult* self);

/**
 * - @brief 获取结果消息的只读借用指针。
 * - @param self 结果对象；可为 NULL。
 * - @return 内部消息字符串；对象销毁或再次修改后失效，调用者不得释放。
 */
const XString* XServerChanResult_message_const(const XServerChanResult* self);

/**
 * - @brief 获取原始响应正文的只读借用指针。
 * - @param self 结果对象；可为 NULL。
 * - @return 内部响应正文；对象销毁后失效，调用者不得释放或修改。
 */
const XByteArray* XServerChanResult_responseBody_const(const XServerChanResult* self);

XCLASS_DEFINE_BEGING(XServerChan)
XCLASS_DEFINE_EXTEND_END(XServerChan, XObject)

/**
 * - @brief Server酱客户端类。
 * - @details 客户端拥有网络访问管理器和 SendKey。SendKey 不会写入日志，
 *          调用者仍不得将它硬编码进源码或提交到版本库。
 */
typedef struct XServerChan {
    XObject m_class;                    /**< 第一个成员，继承 XObject。 */
    XNetworkAccessManager* m_manager;  /**< 网络访问管理器；对象拥有。 */
    XString* m_sendKey;                 /**< SendKey；对象拥有，禁止打印。 */
    XString* m_endpointUrl;             /**< 自定义 API 端点；为空时按 SendKey 自动生成。 */
    int m_transferTimeout;              /**< 传输超时，单位毫秒；0 表示使用底层默认值。 */
} XServerChan;

/**
 * - @brief 初始化客户端的虚函数表。
 * - @return 共享虚函数表；失败返回 NULL。
 */
XVtable* XServerChan_class_init(void);

/**
 * - @brief 初始化客户端。
 * - @param self 待初始化对象；不能为 NULL。
 * - @return 无；默认超时为30000毫秒，使用Server酱官方端点。
 */
void XServerChan_init(XServerChan* self);

/**
 * - @brief 创建空客户端。
 * - @return 新对象；调用者必须使用 XServerChan_delete_base 释放。
 */
/**
 * - @brief 使用 SendKey 创建客户端。
 * - @param sendKey UTF-8 SendKey；借用，创建时深拷贝，不能为 NULL。
 * - @return 新对象；SendKey 无效或分配失败时返回 NULL。
 */
XServerChan* XServerChan_create_ex(XMemoryType memory, const char* sendKey);

/**
 * - @brief 深拷贝创建客户端。
 * - @param other 源客户端；不能为 NULL。
 * - @return 新对象；失败返回 NULL。活动网络请求不会被拷贝。
 */
XServerChan* XServerChan_create_copy(const XServerChan* other);

/**
 * - @brief 移动创建客户端。
 * - @param other 源客户端；资源转移后保持已初始化但未配置 SendKey 的状态。
 * - @return 新对象；失败返回 NULL。
 */
XServerChan* XServerChan_create_move(XServerChan* other);

/** @brief 客户端的反初始化、删除、拷贝和移动调度入口。 */
#define XServerChan_deinit_base XClass_deinit_base
#define XServerChan_delete_base XClass_delete_base
#define XServerChan_copy_base XClass_copy_base
#define XServerChan_move_base XClass_move_base

/**
 * - @brief 设置 SendKey。
 * - @param self 客户端；不能为 NULL。
 * - @param sendKey UTF-8 SendKey；借用，函数执行时深拷贝；不能为空。
 * - @return 设置成功返回 true；无效 key 或分配失败返回 false。
 * - @note 支持 SCT... Turbo key 和 sctp<uid>t... Server酱³ key。
 */
bool XServerChan_setSendKey_utf8(XServerChan* self, const char* sendKey);

/**
 * - @brief 从环境变量设置 SendKey。
 * - @param self 客户端；不能为 NULL。
 * - @param variableName 环境变量名；为 NULL 时使用 SERVERCHAN_SENDKEY。
 * - @return 读取并设置成功返回 true；变量不存在、为空或 key 无效返回 false。
 */
bool XServerChan_setSendKey_environment(XServerChan* self, const char* variableName);

/**
 * - @brief 判断客户端是否已经设置 SendKey。
 * - @param self 客户端；可为 NULL。
 * - @return 已设置有效 SendKey 返回 true。
 */
bool XServerChan_hasSendKey(const XServerChan* self);

/**
 * - @brief 设置自定义 API 端点。
 * - @param self 客户端；不能为 NULL。
 * - @param endpointUrl UTF-8 HTTP/HTTPS URL；借用，函数执行时深拷贝；为 NULL 时恢复官方端点。
 * - @return URL 有效并设置成功返回 true；非法 URL 或分配失败返回 false。
 * - @note 可用于本地测试、企业网关或代理；官方端点会根据 SendKey 自动选择。
 */
bool XServerChan_setEndpointUrl_utf8(XServerChan* self, const char* endpointUrl);

/**
 * - @brief 设置 HTTPS 信任 CA 文件。
 * - @param self 客户端；不能为 NULL。
 * - @param certificatePath UTF-8 PEM CA 证书文件路径；借用，函数执行时加载；不能为 NULL。
 * - @return 加载并设置成功返回 true；文件不存在、格式无效或对象为空返回 false。
 * - @note 只影响后续 HTTPS 请求；不建议为了绕过证书错误而改用 VerifyNone。
 */
bool XServerChan_setSslCaCertificateFile_utf8(XServerChan* self,
                                              const char* certificatePath);

/**
 * - @brief 设置传输超时。
 * - @param self 客户端；不能为 NULL。
 * - @param timeout 超时毫秒数；负数按0处理，0表示使用网络管理器默认值。
 * - @return 无。
 */
void XServerChan_setTransferTimeout(XServerChan* self, int timeout);

/**
 * - @brief 获取传输超时。
 * - @param self 客户端；可为 NULL。
 * - @return 超时毫秒数；对象为空时返回30000。
 */
int XServerChan_transferTimeout(const XServerChan* self);

/**
 * - @brief 异步发送一条 Server酱消息。
 * - @param self 客户端；不能为 NULL且必须设置有效 SendKey。
 * - @param title 消息标题；UTF-8 借用，不能为空且不能包含换行。
 * - @param desp 消息正文；UTF-8 借用，可为 NULL；支持 Server酱 Markdown。
 * - @return 新建 HTTP 回复；调用者必须使用 XHttpReply_delete_base 释放；参数无效或发送失败返回 NULL。
 * - @note 返回后需要由调用者运行 XCoreApplication 事件循环；完成后检查 XHttpReply。
 */
XHttpReply* XServerChan_send(XServerChan* self, const char* title, const char* desp);

/**
 * - @brief 同步发送一条 Server酱消息并解析结果。
 * - @param self 客户端；不能为 NULL且必须设置有效 SendKey。
 * - @param title 消息标题；UTF-8 借用，不能为空且不能包含换行。
 * - @param desp 消息正文；UTF-8 借用，可为 NULL；支持 Server酱 Markdown。
 * - @return 新建结果对象；调用者必须使用 XServerChanResult_delete_base 释放；始终返回本地结果，失败信息位于结果对象的 m_error 字段。
 * - @note 当前线程必须已经有 XCoreApplication 事件循环；函数会按 transferTimeout 轮询网络事件。
 */
XServerChanResult* XServerChan_sendBlocking(XServerChan* self,
                                            const char* title,
                                            const char* desp);

#endif // XHTTP_ON
#endif /* XPROTOCOL_ON */
#ifdef __cplusplus
}
#endif


/* XClass create API default-memory wrappers. */
#undef XServerChan_create
#define XServerChan_create() \
	XServerChan_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, NULL)
#undef XServerChanResult_create
#define XServerChanResult_create() XServerChanResult_create_ex(XCLASS_DEFAULT_MEMORY_TYPE)

#endif /* XSERVERCHAN_H */
