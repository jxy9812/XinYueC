/**
 * @file       XHttpServerWebSocketUpgradeResponse.h
 * - @brief      HTTP 服务端 WebSocket 升级判定值对象，对标 Qt 类型。
 * - @details    该对象只表达升级判定，不直接拥有或操作 WebSocket 套接字。
 */

#ifndef XHTTPSERVERWEBSOCKETUPGRADERESPONSE_H
#define XHTTPSERVERWEBSOCKETUPGRADERESPONSE_H
#include "XHttp_config.h"

#ifdef __cplusplus
extern "C" {
#endif
#if XPROTOCOL_ON
#if XHTTP_ON

#include "XClass.h"
#include "XByteArray.h"
#include <stdbool.h>

XCLASS_DEFINE_BEGING(XHttpServerWebSocketUpgradeResponse)
XCLASS_DEFINE_EXTEND_END(XHttpServerWebSocketUpgradeResponse, XClass)

/** @brief WebSocket 升级判定类型。 */
typedef enum XHttpServerWebSocketUpgradeResponse_Type {
    XHttpServerWebSocketUpgradeResponse_Accept = 0,
    XHttpServerWebSocketUpgradeResponse_Deny,
    XHttpServerWebSocketUpgradeResponse_PassToNext
} XHttpServerWebSocketUpgradeResponse_Type;

/** @brief WebSocket 升级判定值对象。 */
typedef struct XHttpServerWebSocketUpgradeResponse {
    XClass m_class;                         /**< 第一个成员，继承 XClass。 */
    XHttpServerWebSocketUpgradeResponse_Type m_type; /**< 判定类型。 */
    int m_denyStatus;                       /**< 拒绝状态码，默认403。 */
    XByteArray* m_denyMessage;              /**< 拒绝消息；对象拥有。 */
} XHttpServerWebSocketUpgradeResponse;

/**
 * - @brief 初始化 WebSocket 升级响应虚函数表。
 * - @return 升级响应虚函数表；初始化失败返回 NULL，不由调用者释放。
 */
XVtable* XHttpServerWebSocketUpgradeResponse_class_init(void);
/**
 * - @brief 创建接受 WebSocket 升级的判定对象。
 * - @return 新建 Accept 判定对象；调用者必须使用 XHttpServerWebSocketUpgradeResponse_delete_base 释放，分配失败返回 NULL。
 */
XHttpServerWebSocketUpgradeResponse* XHttpServerWebSocketUpgradeResponse_accept(void);
/**
 * - @brief 创建拒绝 WebSocket 升级的判定对象。
 * - @return 新建 Deny 判定对象；默认状态码为 403，调用者必须使用 XHttpServerWebSocketUpgradeResponse_delete_base 释放，分配失败返回 NULL。
 */
XHttpServerWebSocketUpgradeResponse* XHttpServerWebSocketUpgradeResponse_deny(void);
/**
 * - @brief 创建带状态码和消息的拒绝判定对象。
 * - @param status 拒绝 HTTP 状态码；小于 100 或大于 599 时使用 403。
 * - @param message 拒绝消息；借用，NULL 表示空消息，创建时深拷贝。
 * - @return 新建 Deny 判定对象；调用者必须使用 XHttpServerWebSocketUpgradeResponse_delete_base 释放，分配失败返回 NULL。
 */
XHttpServerWebSocketUpgradeResponse* XHttpServerWebSocketUpgradeResponse_denyWith(
    int status, const XByteArray* message);
/**
 * - @brief 创建交给下一处理器的升级判定对象。
 * - @return 新建 PassToNext 判定对象；调用者必须使用 XHttpServerWebSocketUpgradeResponse_delete_base 释放，分配失败返回 NULL。
 */
XHttpServerWebSocketUpgradeResponse* XHttpServerWebSocketUpgradeResponse_passToNext(void);
/**
 * - @brief 深拷贝创建 WebSocket 升级判定对象。
 * - @param other 源升级判定对象；借用且不能为 NULL。
 * - @return 新建判定对象；调用者必须使用 XHttpServerWebSocketUpgradeResponse_delete_base 释放，参数无效或拷贝失败返回 NULL。
 */
XHttpServerWebSocketUpgradeResponse* XHttpServerWebSocketUpgradeResponse_create_copy(
    const XHttpServerWebSocketUpgradeResponse* other);
#define XHttpServerWebSocketUpgradeResponse_deinit_base XClass_deinit_base
#define XHttpServerWebSocketUpgradeResponse_delete_base XClass_delete_base
/**
 * - @brief 获取升级判定类型。
 * - @param self 升级判定对象；可为 NULL。
 * - @return 判定类型；self 为空时返回 XHttpServerWebSocketUpgradeResponse_Accept。
 */
XHttpServerWebSocketUpgradeResponse_Type XHttpServerWebSocketUpgradeResponse_type(
    const XHttpServerWebSocketUpgradeResponse* self);
/**
 * - @brief 获取拒绝状态码。
 * - @param self 升级判定对象；可为 NULL。
 * - @return 拒绝 HTTP 状态码；self 为空时返回 403。
 */
int XHttpServerWebSocketUpgradeResponse_denyStatus(
    const XHttpServerWebSocketUpgradeResponse* self);
/**
 * - @brief 获取拒绝消息。
 * - @param self 升级判定对象；可为 NULL。
 * - @return 内部拒绝消息借用指针；未设置或 self 为空时返回 NULL，调用者不得释放或修改。
 */
const XByteArray* XHttpServerWebSocketUpgradeResponse_denyMessage_const(
    const XHttpServerWebSocketUpgradeResponse* self);
/**
 * - @brief 交换两个升级判定对象的内容。
 * - @param self 左侧升级判定对象；不能为 NULL。
 * - @param other 右侧升级判定对象；不能为 NULL。
 */
void XHttpServerWebSocketUpgradeResponse_swap(
    XHttpServerWebSocketUpgradeResponse* self,
    XHttpServerWebSocketUpgradeResponse* other);

#endif // XHTTP_ON
#endif /* XPROTOCOL_ON */
#ifdef __cplusplus
}
#endif

#endif /* XHTTPSERVERWEBSOCKETUPGRADERESPONSE_H */
