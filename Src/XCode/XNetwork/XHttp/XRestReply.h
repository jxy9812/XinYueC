/**
 * @file       XRestReply.h
 * - @brief      REST 响应读取包装，对标 Qt 6.8 QRestReply。
 * - @details    本对象仅包装 HTTP 响应的读取语义，不直接访问套接字或平台 API。
 */

#ifndef XRESTREPLY_H
#define XRESTREPLY_H

#ifdef __cplusplus
extern "C" {
#endif

#include "XHttpReply.h"
#include "XJsonDocument.h"

XCLASS_DEFINE_BEGING(XRestReply)
XCLASS_DEFINE_EXTEND_END(XRestReply, XClass)

/**
 * - @brief REST 响应轻量包装。
 * - @details m_reply 为借用指针；包装不删除网络响应，响应必须在包装使用期间保持有效。
 */
typedef struct XRestReply {
    XClass m_class;          /**< 第一个成员，由 XClass 管理。 */
    XHttpReply* m_reply;     /**< 被包装的网络响应；借用，不由包装拥有。 */
} XRestReply;

/**
 * - @brief 初始化 REST 响应包装。
 * - @param self 待初始化的包装对象；不能为 NULL。
 * - @param reply 被包装的 HTTP 响应；借用，可为 NULL，必须在包装使用期间保持有效。
 */
void XRestReply_init(XRestReply* self, XHttpReply* reply);
/**
 * - @brief 创建 REST 响应包装。
 * - @param reply 被包装的 HTTP 响应；借用，可为 NULL，必须在包装使用期间保持有效。
 * - @return 新建 REST 包装；调用者必须使用 XRestReply_delete_base 释放，分配失败返回 NULL。
 */
XRestReply* XRestReply_create(XHttpReply* reply);
/**
 * - @brief 深拷贝创建 REST 响应包装。
 * - @param other 源包装；借用且不能为 NULL，内部 HTTP 响应指针仍按借用语义复制。
 * - @return 新建 REST 包装；调用者必须使用 XRestReply_delete_base 释放，参数无效或分配失败返回 NULL。
 */
XRestReply* XRestReply_create_copy(const XRestReply* other);

#define XRestReply_deinit_base XClass_deinit_base
#define XRestReply_delete_base XClass_delete_base
#define XRestReply_copy_base XClass_copy_base

/**
 * - @brief 获取被包装的 HTTP 响应。
 * - @param self REST 包装；可为 NULL。
 * - @return 被包装的 HTTP 响应借用指针；self 或内部响应为空时返回 NULL，不得释放。
 */
XHttpReply* XRestReply_networkReply(const XRestReply* self);
/**
 * - @brief 读取 JSON body。
 * - @param self REST 包装；不能为 NULL。
 * - @param errorText 可选输出参数；失败时接收新建中文错误描述，调用者使用 XString_delete_base 释放。
 * - @return 新建 JSON 文档；调用者必须使用 XJsonDocument_delete 释放，失败返回 NULL。
 */
XJsonDocument* XRestReply_readJson(XRestReply* self, XString** errorText);
/**
 * - @brief 读取剩余响应 body。
 * - @param self REST 包装；不能为 NULL。
 * - @return 新建 body 字节数组；调用者必须使用 XByteArray_delete_base 释放，参数无效或分配失败返回 NULL。
 */
XByteArray* XRestReply_readBody(XRestReply* self);
/**
 * - @brief 按 UTF-8 读取剩余响应 body。
 * - @param self REST 包装；不能为 NULL。
 * - @return 新建 UTF-8 字符串；调用者必须使用 XString_delete_base 释放，参数无效或转换失败返回 NULL。
 */
XString* XRestReply_readText(XRestReply* self);
/**
 * - @brief 判断 REST 响应是否成功。
 * - @param self REST 包装；可为 NULL。
 * - @return 无网络错误且 HTTP 状态码在 200 到 299 范围内返回 true，否则返回 false。
 */
bool XRestReply_isSuccess(const XRestReply* self);
/**
 * - @brief 获取 HTTP 状态码。
 * - @param self REST 包装；可为 NULL。
 * - @return HTTP 状态码；self 或内部响应为空时返回 0。
 */
int XRestReply_httpStatus(const XRestReply* self);
/**
 * - @brief 判断 HTTP 状态码是否为成功范围。
 * - @param self REST 包装；可为 NULL。
 * - @return 状态码在 200 到 299 范围内返回 true，否则返回 false。
 */
bool XRestReply_isHttpStatusSuccess(const XRestReply* self);
/**
 * - @brief 判断响应是否存在网络或协议错误。
 * - @param self REST 包装；可为 NULL。
 * - @return 包装为空或内部响应存在错误时返回 true，否则返回 false。
 */
bool XRestReply_hasError(const XRestReply* self);
/**
 * - @brief 获取网络错误枚举。
 * - @param self REST 包装；可为 NULL。
 * - @return 网络错误；self 或内部响应为空时返回 XHttpReply_UnknownNetworkError。
 */
XHttpReply_NetworkError XRestReply_error(const XRestReply* self);
/**
 * - @brief 获取错误描述副本。
 * - @param self REST 包装；可为 NULL。
 * - @return 新建错误描述字符串；调用者必须使用 XString_delete_base 释放，未设置错误时返回 NULL。
 */
XString* XRestReply_errorString(const XRestReply* self);

#ifdef __cplusplus
}
#endif

#endif /* XRESTREPLY_H */
