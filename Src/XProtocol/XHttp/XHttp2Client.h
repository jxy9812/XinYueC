/**
 * @file       XHttp2Client.h
 * - @brief      HTTP/2 客户端前言、流号和请求帧调度 API。
 * - @details    只组装 RFC 7540 协议字节，不创建套接字，不调用平台 API。
 */

#ifndef XHTTP2CLIENT_H
#define XHTTP2CLIENT_H
#include "XHttp_config.h"

#ifdef __cplusplus
extern "C" {
#endif
#if XPROTOCOL_ON
#if XHTTP_ON

#include "XClass.h"
#include "XHttp2Configuration.h"
#include "XHttp2Frame.h"
#include "XHttp2Headers.h"
#include "XHttpRequest.h"
#include "XByteArray.h"
#include <stdbool.h>
#include <stdint.h>

XCLASS_DEFINE_BEGING(XHttp2ClientSession)
XCLASS_DEFINE_EXTEND_END(XHttp2ClientSession, XClass)

/** @brief 不持有套接字、但维护 HPACK 和流号状态的 HTTP/2 客户端会话。 */
typedef struct XHttp2ClientSession {
    XClass m_class;                         /**< 第一个成员，继承 XClass。 */
    XHttp2Configuration* m_configuration;  /**< HTTP/2 配置；对象拥有。 */
    XHttp2HeaderEncoder* m_encoder;         /**< 连接级 HPACK 编码器；对象拥有。 */
    uint32_t m_nextStreamId;                /**< 下一个客户端奇数流号。 */
    size_t m_peerMaxHeaderListSize;          /**< 对端允许的解压后头字段总大小。 */
    uint32_t m_peerMaxConcurrentStreams;     /**< 对端允许的同时活动流数。 */
    uint32_t m_peerMaxFrameSize;             /**< 对端允许的帧 payload 上限。 */
    size_t m_activeStreamCount;              /**< 尚未关闭的本地请求流数。 */
    bool m_started;                         /**< 是否已经生成客户端前言。 */
    bool m_goingAway;                       /**< 是否已收到对端 GOAWAY。 */
} XHttp2ClientSession;

/**
 * - @brief 初始化 HTTP/2 客户端会话虚函数表。
 * - @return 客户端会话虚函数表；初始化失败返回 NULL，不由调用者释放。
 */
XVtable* XHttp2ClientSession_class_init(void);
/**
 * - @brief 初始化 HTTP/2 客户端会话。
 * - @param self 待初始化的客户端会话；不能为 NULL。
 */
void XHttp2ClientSession_init(XHttp2ClientSession* self);
/**
 * - @brief 创建 HTTP/2 客户端会话。
 * - @return 新建客户端会话；调用者必须使用 XHttp2ClientSession_delete_base 释放，分配失败返回 NULL。
 */
XHttp2ClientSession* XHttp2ClientSession_create(void);
#define XHttp2ClientSession_deinit_base XClass_deinit_base
#define XHttp2ClientSession_delete_base XClass_delete_base

/**
 * - @brief 设置 HTTP/2 配置。
 * - @param self 会话；不能为 NULL。
 * - @param configuration 配置借用，调用时深拷贝；不能为 NULL。
 * - @return 成功返回 true；参数无效或内存不足返回 false。
 */
bool XHttp2ClientSession_setConfiguration(XHttp2ClientSession* self,
                                          const XHttp2Configuration* configuration);
/**
 * - @brief 获取会话 HTTP/2 配置。
 * - @param self 客户端会话；可为 NULL。
 * - @return 内部配置借用指针；self 为空时返回 NULL，调用者不得释放或修改。
 */
const XHttp2Configuration* XHttp2ClientSession_configuration_const(
    const XHttp2ClientSession* self);

/**
 * - @brief 接受对端 SETTINGS_HEADER_TABLE_SIZE。
 * - @param self 会话；不能为 NULL。
 * - @param size 对端允许的动态表最大字节数；范围为 0 到 65536。
 * - @return 成功返回 true；参数非法或内存状态无效返回 false。
 * - @details 成功后下一请求头块自动发出 HPACK 动态表大小更新。
 */
bool XHttp2ClientSession_setPeerHeaderTableSize(XHttp2ClientSession* self, size_t size);

/**
 * - @brief 设置对端 SETTINGS_MAX_HEADER_LIST_SIZE。
 * - @param self 会话；不能为 NULL。
 * - @param size 对端允许的解压后头字段总字节数；SIZE_MAX 表示未声明限制。
 * - @return 成功返回 true；self 为 NULL 返回 false。
 */
bool XHttp2ClientSession_setPeerMaxHeaderListSize(XHttp2ClientSession* self, size_t size);

/**
 * - @brief 设置对端 SETTINGS_MAX_CONCURRENT_STREAMS。
 * - @param self 会话；不能为 NULL。
 * - @param count 对端允许的同时活动流数，0 表示暂不接收新的流。
 * - @return 成功返回 true；self 为 NULL 返回 false。
 */
bool XHttp2ClientSession_setPeerMaxConcurrentStreams(XHttp2ClientSession* self,
                                                      uint32_t count);
/**
 * - @brief 设置对端 SETTINGS_MAX_FRAME_SIZE。
 * - @param self 会话；不能为 NULL。
 * - @param size 对端最大帧 payload；必须为 16384 到 16777215。
 * - @return 成功返回 true；参数非法返回 false，旧值保持不变。
 */
bool XHttp2ClientSession_setPeerMaxFrameSize(XHttp2ClientSession* self, uint32_t size);
/**
 * - @brief 获取当前活动的本地请求流数量。
 * - @param self 客户端会话；可为 NULL。
 * - @return 尚未关闭的本地请求流数量；self 为空时返回 0。
 */
size_t XHttp2ClientSession_activeStreamCount(const XHttp2ClientSession* self);
/**
 * - @brief 标记一个已分配的请求流已经关闭。
 * - @param self 会话；不能为 NULL。
 * - @return 成功减少活动流数返回 true；当前没有活动流或 self 为 NULL 返回 false。
 */
bool XHttp2ClientSession_markStreamClosed(XHttp2ClientSession* self);
/**
 * - @brief 标记会话已收到对端 GOAWAY。
 * - @param self 会话；不能为 NULL。
 * - @return 无；之后 XHttp2ClientSession_encodeRequest 不再分配新流。
 */
void XHttp2ClientSession_setGoingAway(XHttp2ClientSession* self);
/**
 * - @brief 判断是否已收到对端 GOAWAY。
 * - @param self 客户端会话；可为 NULL。
 * - @return 已收到对端 GOAWAY 返回 true；self 为空时返回 false。
 */
bool XHttp2ClientSession_isGoingAway(const XHttp2ClientSession* self);

/**
 * - @brief 生成客户端前言和 SETTINGS 帧。
 * - @param self 会话；不能为 NULL。
 * - @return 新协议字节；调用者释放；重复调用返回空数组而不重复前言。
 */
XByteArray* XHttp2ClientSession_start(XHttp2ClientSession* self);
/**
 * - @brief 判断是否已生成客户端前言。
 * - @param self 客户端会话；可为 NULL。
 * - @return 已生成前言返回 true；self 为空时返回 false。
 */
bool XHttp2ClientSession_isStarted(const XHttp2ClientSession* self);
/**
 * - @brief 分配并返回下一个客户端奇数流 ID。
 * - @param self 客户端会话；不能为 NULL。
 * - @return 新分配的奇数流 ID；会话不可用、已 GOAWAY、超过流 ID 上限或达到并发限制时返回 0。
 */
uint32_t XHttp2ClientSession_nextStreamId(XHttp2ClientSession* self);
/**
 * - @brief 预留 h2c HTTP/1.1 升级请求对应的客户端流 1。
 * - @param self 会话；不能为 NULL，且尚未分配普通请求流。
 * - @return 成功返回 true；流号、并发限制或 GOAWAY 状态不允许时返回 false。
 * - @note RFC 7540 3.2 规定升级前的 HTTP/1.1 请求对应 HTTP/2 流 1，后续请求从流 3 开始。
 */
bool XHttp2ClientSession_adoptUpgradedStream(XHttp2ClientSession* self);

/**
 * - @brief 将一个请求编码成 HEADERS/DATA 帧序列。
 * - @param self 会话；不能为 NULL，未 start 时会自动加入前言。
 * - @param request 请求借用；必须有有效 URL。
 * - @param streamId 输出流号；不能为 NULL。
 * - @return 新协议字节；调用者释放；失败返回 NULL。
 */
XByteArray* XHttp2ClientSession_encodeRequest(XHttp2ClientSession* self,
                                              const XHttpRequest* request,
                                              uint32_t* streamId);

#endif // XHTTP_ON
#endif /* XPROTOCOL_ON */
#ifdef __cplusplus
}
#endif

#endif /* XHTTP2CLIENT_H */
