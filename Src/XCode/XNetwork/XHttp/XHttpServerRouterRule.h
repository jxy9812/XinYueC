/**
 * @file       XHttpServerRouterRule.h
 * - @brief      HTTP 服务端路径路由规则，对标 Qt QHttpServerRouterRule。
 * - @details    规则只保存路径模式和回调借用关系，不直接访问套接字或平台 API。
 */

#ifndef XHTTPSERVERROUTERRULE_H
#define XHTTPSERVERROUTERRULE_H

#ifdef __cplusplus
extern "C" {
#endif

#include "XClass.h"
#include "XHttpServer.h"
#include "XString.h"
#include <stdbool.h>
#include <stdint.h>

XCLASS_DEFINE_BEGING(XHttpServerRouterRule)
XCLASS_DEFINE_EXTEND_END(XHttpServerRouterRule, XClass)

/** @brief 路由规则对象；由 XHttpServerRouter 拥有时不得单独释放。 */
struct XHttpServerRouterRule {
    XClass m_class;                         /**< 第一个成员，继承 XClass。 */
    XString* m_pathPattern;                 /**< 路径模式；对象拥有。 */
    uint32_t m_methods;                     /**< 允许的方法掩码。 */
    XHttpServer_RouteHandler m_handler;     /**< 规则处理器；不拥有。 */
    void* m_context;                        /**< 处理器上下文；不拥有。 */
};

/**
 * - @brief 初始化 HTTP 服务端路由规则虚函数表。
 * - @return 路由规则虚函数表；初始化失败返回 NULL，不由调用者释放。
 */
XVtable* XHttpServerRouterRule_class_init(void);
/**
 * - @brief 创建路由规则。
 * - @param pathPattern UTF-8 路径模式；不能为 NULL。
 * - @param methods 方法掩码；0 表示不匹配任何方法。
 * - @param handler 路由处理器；不能为 NULL。
 * - @param context 上下文借用指针。
 * - @return 新规则；调用者或路由器负责释放。
 */
XHttpServerRouterRule* XHttpServerRouterRule_create(const char* pathPattern,
                                                    uint32_t methods,
                                                    XHttpServer_RouteHandler handler,
                                                    void* context);
#define XHttpServerRouterRule_deinit_base XClass_deinit_base
#define XHttpServerRouterRule_delete_base XClass_delete_base
/**
 * - @brief 获取路径模式。
 * - @param self 路由规则；可为 NULL。
 * - @return 内部路径模式借用指针；self 为空时返回 NULL，调用者不得释放或修改。
 */
const XString* XHttpServerRouterRule_pathPattern_const(
    const XHttpServerRouterRule* self);
/**
 * - @brief 获取允许的请求方法掩码。
 * - @param self 路由规则；可为 NULL。
 * - @return 请求方法掩码；self 为空时返回 0。
 */
uint32_t XHttpServerRouterRule_methods(const XHttpServerRouterRule* self);
/**
 * - @brief 判断路由规则是否匹配请求。
 * - @param self 路由规则；可为 NULL。
 * - @param request 服务端请求；借用且不能为 NULL，仅在请求处理回调期间有效。
 * - @return 路径模式与方法掩码均匹配返回 true；否则返回 false。
 */
bool XHttpServerRouterRule_matches(const XHttpServerRouterRule* self,
                                   const XHttpServerRequest* request);
/**
 * - @brief 执行规则处理器。
 * - @param self 规则；不能为 NULL。
 * - @param request 请求借用。
 * - @param responder 响应器借用。
 * - @return 规则匹配且处理器存在返回 true，否则返回 false。
 */
bool XHttpServerRouterRule_exec(const XHttpServerRouterRule* self,
                                const XHttpServerRequest* request,
                                XHttpServerResponder* responder);

#ifdef __cplusplus
}
#endif

#endif /* XHTTPSERVERROUTERRULE_H */
