/**
 * @file       XNetworkReply.h
 * - @brief      QNetworkReply 兼容命名入口。
 * - @details    XNetworkReply 是 XHttpReply 的 Qt 对齐名称别名；生命周期、信号和
 *             feed/endOfInput 增量解析行为与 XHttpReply 完全一致。
 */

#ifndef XNETWORKREPLY_H
#define XNETWORKREPLY_H
#include "XHttp_config.h"

#include "XHttpReply.h"
#if XPROTOCOL_ON
#if XHTTP_ON

/** @brief Qt QNetworkReply 的 XinYueC 类型别名。 */
typedef XHttpReply XNetworkReply;
/** @brief Qt QNetworkReply 状态别名。 */
typedef XHttpReply_State XNetworkReply_State;
/** @brief Qt QNetworkReply 错误别名。 */
typedef XHttpReply_NetworkError XNetworkReply_NetworkError;

#define XNetworkReply_Idle XHttpReply_Idle
#define XNetworkReply_Receiving XHttpReply_Receiving
#define XNetworkReply_Finished XHttpReply_Finished
#define XNetworkReply_NoError XHttpReply_NoError
#define XNetworkReply_ConnectionRefusedError XHttpReply_ConnectionRefusedError
#define XNetworkReply_RemoteHostClosedError XHttpReply_RemoteHostClosedError
#define XNetworkReply_HostNotFoundError XHttpReply_HostNotFoundError
#define XNetworkReply_TimeoutError XHttpReply_TimeoutError
#define XNetworkReply_OperationCanceledError XHttpReply_OperationCanceledError
#define XNetworkReply_SslHandshakeFailedError XHttpReply_SslHandshakeFailedError
#define XNetworkReply_UnknownNetworkError XHttpReply_UnknownNetworkError
#define XNetworkReply_ProtocolUnknownError XHttpReply_ProtocolUnknownError
#define XNetworkReply_ProtocolInvalidOperationError XHttpReply_ProtocolInvalidOperationError
#define XNetworkReply_InternalServerError XHttpReply_InternalServerError
#define XNetworkReply_ServiceUnavailableError XHttpReply_ServiceUnavailableError

#define XNetworkReply_class_init XHttpReply_class_init
#define XNetworkReply_init XHttpReply_init
#define XNetworkReply_create XHttpReply_create
#define XNetworkReply_deinit_base XHttpReply_deinit_base
#define XNetworkReply_delete_base XHttpReply_delete_base
#define XNetworkReply_deleteLater XHttpReply_deleteLater
#define XNetworkReply_request XHttpReply_request
#define XNetworkReply_request_const XHttpReply_request_const
#define XNetworkReply_headers_const XHttpReply_headers_const
#define XNetworkReply_trailers_const XHttpReply_trailers_const
#define XNetworkReply_statusCode XHttpReply_statusCode
#define XNetworkReply_url XHttpReply_url
#define XNetworkReply_hasRawHeader XHttpReply_hasRawHeader
#define XNetworkReply_rawHeader XHttpReply_rawHeader
#define XNetworkReply_rawHeaderList XHttpReply_rawHeaderList
#define XNetworkReply_headers XHttpReply_headers
#define XNetworkReply_headerKnown XHttpReply_headerKnown
#define XNetworkReply_reasonPhrase XHttpReply_reasonPhrase
#define XNetworkReply_body_const XHttpReply_body_const
#define XNetworkReply_readAll XHttpReply_readAll
#define XNetworkReply_error XHttpReply_error
#define XNetworkReply_errorString XHttpReply_errorString
#define XNetworkReply_setError XHttpReply_setError
#define XNetworkReply_resetForRequest XHttpReply_resetForRequest
#define XNetworkReply_finishedSignalPending XHttpReply_finishedSignalPending
#define XNetworkReply_emitFinished XHttpReply_emitFinished
#define XNetworkReply_redirectTarget XHttpReply_redirectTarget
#define XNetworkReply_isFinished XHttpReply_isFinished
#define XNetworkReply_isRunning XHttpReply_isRunning
#define XNetworkReply_feed XHttpReply_feed
#define XNetworkReply_endOfInput XHttpReply_endOfInput
#define XNetworkReply_abort XHttpReply_abort
#define XNetworkReply_readyRead_signal XHttpReply_readyRead_signal
#define XNetworkReply_metaDataChanged_signal XHttpReply_metaDataChanged_signal
#define XNetworkReply_finished_signal XHttpReply_finished_signal
#define XNetworkReply_errorOccurred_signal XHttpReply_errorOccurred_signal
#define XNetworkReply_downloadProgress_signal XHttpReply_downloadProgress_signal
#define XNetworkReply_redirected_signal XHttpReply_redirected_signal

#endif // XHTTP_ON
#endif /* XPROTOCOL_ON */
#endif /* XNETWORKREPLY_H */
