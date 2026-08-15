/**
 * @file       XHttp2Connection.h
 * - @brief      HTTP/2 客户端连接状态机和多路复用流管理。
 * - @details    本对象只维护 RFC 7540/9113 协议字节和状态，不创建套接字、不调用平台 API。
 *             传输层取得 XHttp2Connection_takeOutgoing 的字节后写出，并将收到的字节传给
 *             XHttp2Connection_feed。一个连接共享 HPACK、SETTINGS 和连接级流控窗口。
 */

#ifndef XHTTP2CONNECTION_H
#define XHTTP2CONNECTION_H
#include "XHttp_config.h"

#ifdef __cplusplus
extern "C" {
#endif
#if XPROTOCOL_ON
#if XHTTP_ON

#include "XClass.h"
#include "XHttp2Client.h"
#include "XHttp2Headers.h"
#include "XHttpReply.h"
#include "XVector.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

XCLASS_DEFINE_BEGING(XHttp2Connection)
XCLASS_DEFINE_EXTEND_END(XHttp2Connection, XClass)

/** @brief 与 RFC 7540 5.1 对齐的 HTTP/2 流状态。 */
typedef enum XHttp2Stream_State {
    XHttp2Stream_Idle = 0,          /**< 未使用的流。 */
    XHttp2Stream_Open,              /**< 两端都可发送帧。 */
    XHttp2Stream_HalfClosedLocal,   /**< 本端已发送 END_STREAM。 */
    XHttp2Stream_HalfClosedRemote,  /**< 对端已发送 END_STREAM。 */
    XHttp2Stream_ReservedRemote,    /**< 由服务端 PUSH_PROMISE 保留。 */
    XHttp2Stream_Closed             /**< 双方关闭或已重置。 */
} XHttp2Stream_State;

/**
 * - @brief 连接内部流记录。
 * - @details 结构对外只读；其中 reply 由连接拥有，除 takePushedReply 的所有权转移外，
 *          调用者不得删除。
 */
typedef struct XHttp2Stream {
    XHttpReply* m_reply;             /**< 响应对象；连接拥有。 */
    XByteArray* m_wire;              /**< 尚待根据流控写出的请求字节；流拥有。 */
    XByteArray* m_headerBlock;       /**< 跨 CONTINUATION 的头块；流拥有。 */
    uint32_t m_id;                   /**< 流标识。 */
    int64_t m_sendWindow;            /**< 对端授予的流发送窗口。 */
    int64_t m_recvWindow;            /**< 本端流接收窗口。 */
    size_t m_writeOffset;            /**< m_wire 已消费字节数。 */
    size_t m_dataOffset;             /**< 当前 DATA 帧已发送载荷数。 */
    bool m_headerEndStream;          /**< 当前 HEADERS 是否携带 END_STREAM。 */
    bool m_headersActive;            /**< 是否等待该流 CONTINUATION。 */
    bool m_pushed;                   /**< 是否为服务端推送流。 */
    bool m_replyOwned;               /**< 是否由连接负责释放 m_reply。 */
    XHttp2Stream_State m_state;      /**< 当前流状态。 */
} XHttp2Stream;

/**
 * - @brief 一个可复用 HTTP/2 客户端连接。
 * - @details 连接拥有会话、解码器、全部流和待发送/待解析字节。m_streams 中保存
 *          XHttp2Stream*，m_pushedReplies 中保存等待应用接收的 XHttpReply*。
 */
typedef struct XHttp2Connection {
    XClass m_class;                       /**< 第一个成员，继承 XClass。 */
    XHttp2Configuration* m_configuration; /**< 本地配置；对象拥有。 */
    XHttp2ClientSession* m_session;       /**< 共享客户端会话和 HPACK 编码器；对象拥有。 */
    XHttp2HeaderDecoder* m_decoder;       /**< 共享 HPACK 解码器；对象拥有。 */
    XVector* m_streams;                   /**< XHttp2Stream* 列表；对象拥有。 */
    XVector* m_pushedReplies;             /**< 已完成推送响应；对象拥有，直到取走。 */
    XByteArray* m_input;                  /**< 未组成完整帧的输入；对象拥有。 */
    XByteArray* m_outgoing;               /**< 等待传输层写出的字节；对象拥有。 */
    int64_t m_sessionSendWindow;          /**< 对端连接发送窗口。 */
    int64_t m_sessionRecvWindow;          /**< 本地连接接收窗口。 */
    uint32_t m_peerInitialWindowSize;     /**< 对端流初始发送窗口。 */
    uint32_t m_peerMaxFrameSize;          /**< 对端最大帧 payload。 */
    uint32_t m_sessionRecvTarget;         /**< 本地连接窗口目标。 */
    uint32_t m_streamRecvTarget;          /**< 本地流窗口目标。 */
    uint32_t m_continuationStreamId;      /**< 当前期望 CONTINUATION 的流号。 */
    uint32_t m_promisedStreamId;          /**< 当前 PUSH_PROMISE 的承诺流号。 */
    uint32_t m_lastIncomingStreamId;      /**< 服务端已创建或承诺的最大偶数流号。 */
    uint32_t m_remoteLastStreamId;        /**< 对端 GOAWAY 最后处理流号。 */
    bool m_waitingForSettingsAck;         /**< 本端 SETTINGS 是否等待 ACK。 */
    bool m_initialSessionWindowSent;      /**< 初始连接级窗口扩展是否已写出。 */
    bool m_continuationPushPromise;       /**< 当前连续头块是否属于 PUSH_PROMISE。 */
    bool m_goingAway;                     /**< 是否收到或发送 GOAWAY。 */
    bool m_goAwaySent;                    /**< 是否已发送 GOAWAY。 */
    bool m_failed;                        /**< 是否发生不可恢复连接错误。 */
} XHttp2Connection;

/**
 * - @brief 初始化 HTTP/2 连接虚函数表。
 * - @return HTTP/2 连接虚函数表；初始化失败返回 NULL，不由调用者释放。
 */
XVtable* XHttp2Connection_class_init(void);
/**
 * - @brief 按默认 HTTP/2 配置初始化连接。
 * - @param self 待初始化的连接对象；不能为 NULL。
 */
void XHttp2Connection_init(XHttp2Connection* self);
/**
 * - @brief 创建默认 HTTP/2 客户端连接。
 * - @return 新建客户端连接；调用者必须使用 XHttp2Connection_delete_base 释放，分配失败返回 NULL。
 */
/**
 * - @brief 创建并复制指定 HTTP/2 配置的客户端连接。
 * - @param configuration 本地配置；借用，不能为 NULL。
 * - @return 新连接；调用者必须释放；内存不足返回 NULL。
 */
XHttp2Connection* XHttp2Connection_create_ex(XMemoryType memory, const XHttp2Configuration* configuration);
#define XHttp2Connection_deinit_base XClass_deinit_base
#define XHttp2Connection_delete_base XClass_delete_base

/**
 * - @brief 将一个请求加入连接并分配客户端奇数流。
 * - @param self 连接；不能为 NULL，且未收到 GOAWAY。
 * - @param request 请求；借用，函数期间深拷贝到响应对象，不能为 NULL。
 * - @param streamId 输出流号；不能为 NULL。
 * - @return 新响应对象的借用指针；连接销毁前有效；失败返回 NULL。
 * - @note 调用后使用 XHttp2Connection_takeOutgoing 取得要写出的前言、SETTINGS、HEADERS/DATA。
 */
XHttpReply* XHttp2Connection_sendRequest(XHttp2Connection* self,
                                         const XHttpRequest* request,
                                         uint32_t* streamId);

/**
 * - @brief 将调用方已创建的响应对象加入连接并分配客户端奇数流。
 * - @param self 连接；不能为 NULL，且未收到 GOAWAY。
 * - @param reply 响应对象；借用，必须包含有效请求，且在流结束前保持有效。
 * - @param streamId 输出流号；不能为 NULL。
 * - @return 成功返回 true；编码、流数限制或状态不允许时返回 false。
 * - @note 连接不取得 reply 所有权。本接口供 XNetworkAccessManager 等需要保持既有
 *       响应对象身份的传输层使用。
 */
bool XHttp2Connection_sendRequestReply(XHttp2Connection* self,
                                       XHttpReply* reply,
                                       uint32_t* streamId);

/**
 * - @brief 将已通过 HTTP/1.1 h2c 升级发送的请求接管为 HTTP/2 流 1。
 * - @param self 新建且尚未分配普通请求流的连接；不能为 NULL。
 * - @param reply 原 HTTP/1.1 升级请求的响应对象；借用，必须已重置为未完成状态。
 * - @return 成功返回 true；会话状态、内存或写出队列失败返回 false。
 * - @note 本接口只发送客户端前言和 SETTINGS，不重复发送已经通过 HTTP/1.1 发出的请求头或体。
 */
bool XHttp2Connection_adoptUpgradedRequest(XHttp2Connection* self,
                                           XHttpReply* reply);

/**
 * - @brief 输入从传输层收到的任意长度 HTTP/2 字节。
 * - @param self 连接；不能为 NULL。
 * - @param data 输入字节；借用，size 为 0 时可为 NULL。
 * - @param size 输入长度。
 * - @return 成功解析或缓存不完整帧返回 true；协议/流控错误返回 false。
 */
bool XHttp2Connection_feed(XHttp2Connection* self, const void* data, size_t size);

/**
 * - @brief 取走当前待写出的协议字节。
 * - @param self 连接；不能为 NULL。
 * - @return 新字节数组；调用者必须释放；无数据时返回空数组，内存不足返回 NULL。
 */
XByteArray* XHttp2Connection_takeOutgoing(XHttp2Connection* self);

/**
 * - @brief 获取指定流的响应对象。
 * - @param self HTTP/2 连接；可为 NULL。
 * - @param streamId 流 ID；必须是已分配的客户端奇数流或已接收的推送偶数流。
 * - @return 响应借用指针；流不存在或 self 为空时返回 NULL，调用者不得释放或保存到连接销毁后。
 */
XHttpReply* XHttp2Connection_replyForStream(XHttp2Connection* self, uint32_t streamId);
/**
 * - @brief 解除一个借用响应对象与流的关联。
 * - @param self 连接；不能为 NULL。
 * - @param streamId 流号。
 * - @param reply 原响应对象；必须与该流当前借用对象相同。
 * - @return 成功解除返回 true；连接拥有响应或参数不匹配返回 false。
 * - @note 响应完成并由上层销毁后调用，避免保活连接留下悬空响应指针；若流已关闭，
 *       其内部记录会立即回收，不影响后续客户端流号分配。
 */
bool XHttp2Connection_detachReply(XHttp2Connection* self, uint32_t streamId,
                                  const XHttpReply* reply);
/**
 * - @brief 获取当前连接保留的流记录数量。
 * - @param self HTTP/2 连接；可为 NULL。
 * - @return 流记录数量，包含尚未回收的已关闭流；self 为空时返回 0。
 */
size_t XHttp2Connection_streamCount(const XHttp2Connection* self);
/**
 * - @brief 取走一个已完成的服务端推送响应。
 * - @param self 连接；不能为 NULL。
 * - @return 响应所有权转移给调用者；没有完成推送时返回 NULL。
 */
XHttpReply* XHttp2Connection_takePushedReply(XHttp2Connection* self);
/**
 * - @brief 判断连接是否不再接受新流。
 * - @param self HTTP/2 连接；可为 NULL。
 * - @return 已收到或发送 GOAWAY，或发生不可恢复连接错误时返回 true；self 为空时返回 false。
 */
bool XHttp2Connection_isGoingAway(const XHttp2Connection* self);

#endif // XHTTP_ON
#endif /* XPROTOCOL_ON */
#ifdef __cplusplus
}
#endif


/* XClass create API default-memory wrappers. */
#undef XHttp2Connection_create
#define XHttp2Connection_create() \
	XHttp2Connection_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, NULL)

#endif /* XHTTP2CONNECTION_H */
