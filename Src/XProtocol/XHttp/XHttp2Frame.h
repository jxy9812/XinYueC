/**
 * @file       XHttp2Frame.h
 * - @brief      HTTP/2 RFC 7540 帧头和帧载荷编解码。
 * - @details    只处理协议字节，不直接访问套接字或平台 API；帧内存使用项目容器。
 */

#ifndef XHTTP2FRAME_H
#define XHTTP2FRAME_H
#include "XHttp_config.h"

#ifdef __cplusplus
extern "C" {
#endif
#if XPROTOCOL_ON
#if XHTTP_ON

#include "XClass.h"
#include "XByteArray.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/** @brief HTTP/2 客户端连接前言。 */
#define XHttp2Frame_ClientPreface "PRI * HTTP/2.0\r\n\r\nSM\r\n\r\n"

/** @brief RFC 7540 帧类型。 */
typedef enum XHttp2Frame_Type {
    XHttp2Frame_Data = 0x0,
    XHttp2Frame_Headers = 0x1,
    XHttp2Frame_Priority = 0x2,
    XHttp2Frame_RstStream = 0x3,
    XHttp2Frame_Settings = 0x4,
    XHttp2Frame_PushPromise = 0x5,
    XHttp2Frame_Ping = 0x6,
    XHttp2Frame_GoAway = 0x7,
    XHttp2Frame_WindowUpdate = 0x8,
    XHttp2Frame_Continuation = 0x9
} XHttp2Frame_Type;

/** @brief HTTP/2 常用帧标志。 */
#define XHttp2Frame_EndStream UINT8_C(0x1)
#define XHttp2Frame_EndHeaders UINT8_C(0x4)
#define XHttp2Frame_Padded UINT8_C(0x8)
#define XHttp2Frame_PriorityFlag UINT8_C(0x20)
#define XHttp2Frame_Ack UINT8_C(0x1)

XCLASS_DEFINE_BEGING(XHttp2Frame)
XCLASS_DEFINE_EXTEND_END(XHttp2Frame, XClass)

/**
 * - @brief 一个完整 HTTP/2 帧。
 * - @details payload 不包含 9 字节帧头；stream ID 保存低 31 位。
 */
typedef struct XHttp2Frame {
    XClass m_class;          /**< 第一个成员，继承 XClass。 */
    XByteArray* m_payload;  /**< 帧载荷；对象拥有。 */
    uint32_t m_streamId;    /**< 流 ID，最高位必须为 0。 */
    uint8_t m_type;         /**< 帧类型。 */
    uint8_t m_flags;        /**< 帧标志。 */
} XHttp2Frame;

/**
 * - @brief 初始化 HTTP/2 帧虚函数表。
 * - @return HTTP/2 帧虚函数表；初始化失败返回 NULL，不由调用者释放。
 */
XVtable* XHttp2Frame_class_init(void);
/**
 * - @brief 初始化空 HTTP/2 帧。
 * - @param self 待初始化的帧对象；不能为 NULL。
 */
void XHttp2Frame_init(XHttp2Frame* self);
/**
 * - @brief 创建空 HTTP/2 帧。
 * - @return 新建帧对象；调用者必须使用 XHttp2Frame_delete_base 释放，分配失败返回 NULL。
 */
/**
 * - @brief 创建指定字段的 HTTP/2 帧。
 * - @param type 帧类型；使用 XHttp2Frame_Type 的值或 RFC 7540 扩展帧类型。
 * - @param flags 帧标志；由调用方按帧类型提供。
 * - @param streamId 流 ID；仅允许低 31 位，最高位必须为 0。
 * - @param payload 帧载荷；借用，NULL 等价于空载荷，创建时深拷贝。
 * - @return 新建帧对象；调用者必须使用 XHttp2Frame_delete_base 释放，字段无效或分配失败返回 NULL。
 */
XHttp2Frame* XHttp2Frame_create_ex(XMemoryType memory, uint8_t type, uint8_t flags, uint32_t streamId,
                                   const XByteArray* payload);
#define XHttp2Frame_deinit_base XClass_deinit_base
#define XHttp2Frame_delete_base XClass_delete_base

/**
 * - @brief 设置帧载荷。
 * - @param self 帧对象；不能为 NULL。
 * - @param payload 载荷；借用并深拷贝，NULL 等价于空载荷。
 * - @return 成功返回 true；载荷超过 16777215 或内存不足返回 false。
 */
bool XHttp2Frame_setPayload(XHttp2Frame* self, const XByteArray* payload);
/**
 * - @brief 获取帧载荷的只读借用指针。
 * - @param self 帧对象；可为 NULL。
 * - @return 内部载荷借用指针；self 为空时返回 NULL，调用者不得释放或修改。
 */
const XByteArray* XHttp2Frame_payload_const(const XHttp2Frame* self);
/**
 * - @brief 设置帧类型。
 * - @param self 帧对象；可为 NULL，NULL 时不执行。
 * - @param type 帧类型；使用 XHttp2Frame_Type 的值或 RFC 7540 扩展帧类型。
 */
void XHttp2Frame_setType(XHttp2Frame* self, uint8_t type);
/**
 * - @brief 获取帧类型。
 * - @param self 帧对象；可为 NULL。
 * - @return 帧类型；self 为空时返回 XHttp2Frame_Data。
 */
uint8_t XHttp2Frame_type(const XHttp2Frame* self);
/**
 * - @brief 设置帧标志。
 * - @param self 帧对象；可为 NULL，NULL 时不执行。
 * - @param flags 帧标志；由调用方按帧类型提供。
 */
void XHttp2Frame_setFlags(XHttp2Frame* self, uint8_t flags);
/**
 * - @brief 获取帧标志。
 * - @param self 帧对象；可为 NULL。
 * - @return 帧标志；self 为空时返回 0。
 */
uint8_t XHttp2Frame_flags(const XHttp2Frame* self);
/**
 * - @brief 设置帧流 ID。
 * - @param self 帧对象；可为 NULL。
 * - @param streamId 流 ID；仅允许低 31 位，最高位为 1 时拒绝。
 * - @return 设置成功返回 true；self 为空或流 ID 非法返回 false。
 */
bool XHttp2Frame_setStreamId(XHttp2Frame* self, uint32_t streamId);
/**
 * - @brief 获取帧流 ID。
 * - @param self 帧对象；可为 NULL。
 * - @return 低 31 位流 ID；self 为空时返回 0。
 */
uint32_t XHttp2Frame_streamId(const XHttp2Frame* self);

/**
 * - @brief 校验帧头中由载荷长度决定的 RFC 7540 约束。
 * - @param self 帧对象；不能为 NULL。
 * - @return 合法返回 true；帧类型要求的固定长度或 SETTINGS 长度不合法返回 false。
 * - @details 未知帧类型按 RFC 7540 5.1 忽略，不因未知类型失败；流 ID 的语义由连接层校验。
 */
bool XHttp2Frame_validateHeader(const XHttp2Frame* self);

/**
 * - @brief 校验 DATA、HEADERS 和 PUSH_PROMISE 的 padding/priority 载荷布局。
 * - @param self 帧对象；不能为 NULL。
 * - @return 帧头和载荷布局均合法返回 true，否则返回 false。
 */
bool XHttp2Frame_validatePayload(const XHttp2Frame* self);

/**
 * - @brief 编码完整 HTTP/2 帧。
 * - @param self 帧对象；不能为 NULL。
 * - @return 新字节数组；调用者必须释放；非法帧或内存不足返回 NULL。
 */
XByteArray* XHttp2Frame_toByteArray(const XHttp2Frame* self);

/**
 * - @brief 从一段输入解码一帧。
 * - @param data 输入字节；借用，不能为 NULL。
 * - @param size 输入长度；不足 9 字节或载荷不完整返回 NULL。
 * - @param consumed 输出已消费字节数；失败时写入 0，可为 NULL。
 * - @return 新帧对象；调用者必须释放。
 */
XHttp2Frame* XHttp2Frame_fromBytes(const void* data, size_t size, size_t* consumed);

/**
 * - @brief 判断输入是否以客户端前言开头。
 * - @param data 输入字节；借用。
 * - @param size 输入长度。
 * - @return 完整前言存在返回 true。
 */
bool XHttp2Frame_hasClientPreface(const void* data, size_t size);

#endif // XHTTP_ON
#endif /* XPROTOCOL_ON */
#ifdef __cplusplus
}
#endif


/* XClass create API default-memory wrappers. */
#undef XHttp2Frame_create
#define XHttp2Frame_create() \
	XHttp2Frame_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, 0, 0, 0, NULL)

#endif /* XHTTP2FRAME_H */
