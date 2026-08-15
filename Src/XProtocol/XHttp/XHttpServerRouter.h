/**
 * @file       XHttpServerRouter.h
 * - @brief      HTTP 服务端路由器，对标 Qt QHttpServerRouter。
 * - @details    路由器只保存规则和回调，不直接调用平台网络 API；监听由 XHttpServer 负责。
 */

#ifndef XHTTPSERVERROUTER_H
#define XHTTPSERVERROUTER_H
#include "XHttp_config.h"

#ifdef __cplusplus
extern "C" {
#endif
#if XPROTOCOL_ON
#if XHTTP_ON

#include "XClass.h"
#include "XHttpServer.h"
#include "XVector.h"
#include <stdbool.h>
#include <stdint.h>

XCLASS_DEFINE_BEGING(XHttpServerRouter)
XCLASS_DEFINE_EXTEND_END(XHttpServerRouter, XClass)

/** @brief HTTP 路由器；拥有加入成功的全部规则。 */
struct XHttpServerRouter {
    XClass m_class;             /**< 第一个成员，继承 XClass。 */
    XHttpServer* m_server;      /**< 所属服务端借用指针。 */
    XVector* m_rules;           /**< XHttpServerRouterRule* 列表；对象拥有。 */
};

/**
 * - @brief 初始化 HTTP 服务端路由器虚函数表。
 * - @return 路由器虚函数表；初始化失败返回 NULL，不由调用者释放。
 */
XVtable* XHttpServerRouter_class_init(void);
/**
 * - @brief 创建 HTTP 服务端路由器。
 * - @param server 所属服务端；借用，可为 NULL，必须在路由器使用期间保持有效。
 * - @return 新建路由器；调用者必须使用 XHttpServerRouter_delete_base 释放，分配失败返回 NULL。
 */
XHttpServerRouter* XHttpServerRouter_create_ex(XMemoryType memory,  XHttpServer* server);
#define XHttpServerRouter_deinit_base XClass_deinit_base
#define XHttpServerRouter_delete_base XClass_delete_base
/**
 * - @brief 获取已注册规则数量。
 * - @param self HTTP 服务端路由器；可为 NULL。
 * - @return 已注册规则数量；self 为空时返回 0。
 */
size_t XHttpServerRouter_size(const XHttpServerRouter* self);
/**
 * - @brief 将规则所有权移入路由器。
 * - @param self 路由器；不能为 NULL。
 * - @param rule 规则；成功后由路由器拥有，失败时仍由调用者拥有。
 * - @return 成功返回 true。
 */
bool XHttpServerRouter_addRule(XHttpServerRouter* self,
                               XHttpServerRouterRule* rule);
/**
 * - @brief 创建并加入一条路由规则。
 * - @param self 路由器；不能为 NULL。
 * - @param pathPattern UTF-8 路径模式；不能为 NULL。
 * - @param methods 方法掩码。
 * - @param handler 路由处理器；不能为 NULL。
 * - @param context 上下文借用指针。
 * - @return 路由器拥有的规则指针，失败返回 NULL。
 */
XHttpServerRouterRule* XHttpServerRouter_addRule_utf8(
    XHttpServerRouter* self,
    const char* pathPattern,
    uint32_t methods,
    XHttpServer_RouteHandler handler,
    void* context);
/**
 * - @brief 清空全部路由规则。
 * - @param self HTTP 服务端路由器；可为 NULL，NULL 时不执行。
 */
void XHttpServerRouter_clear(XHttpServerRouter* self);
/**
 * - @brief 按注册顺序查找并执行第一条命中规则。
 * - @param self 路由器；不能为 NULL。
 * - @param request 请求借用。
 * - @param responder 响应器借用。
 * - @return 有规则命中返回 true，否则返回 false。
 */
bool XHttpServerRouter_handleRequest(const XHttpServerRouter* self,
                                     const XHttpServerRequest* request,
                                     XHttpServerResponder* responder);

#endif // XHTTP_ON
#endif /* XPROTOCOL_ON */
#ifdef __cplusplus
}
#endif


/* XClass create API default-memory wrappers. */
#undef XHttpServerRouter_create
#define XHttpServerRouter_create(...) XHttpServerRouter_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, ##__VA_ARGS__)

#endif /* XHTTPSERVERROUTER_H */
