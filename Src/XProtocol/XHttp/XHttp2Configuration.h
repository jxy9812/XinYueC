/**
 * @file       XHttp2Configuration.h
 * - @brief      HTTP/2 配置值对象，对标 Qt 6.8 QHttp2Configuration。
 * - @details    本配置仅保存 RFC 7540/9113 会话参数，不直接访问套接字或平台 API。
 */

#ifndef XHTTP2CONFIGURATION_H
#define XHTTP2CONFIGURATION_H
#include "XHttp_config.h"

#ifdef __cplusplus
extern "C" {
#endif
#if XPROTOCOL_ON
#if XHTTP_ON

#include "XClass.h"
#include <stdbool.h>
#include <stdint.h>

XCLASS_DEFINE_BEGING(XHttp2Configuration)
XCLASS_DEFINE_EXTEND_END(XHttp2Configuration, XClass)

/** @brief RFC 7540 流控窗口最大值。 */
#define XHttp2Configuration_MaxWindowSize UINT32_C(2147483647)
/** @brief RFC 7540 最大帧 payload 最小值。 */
#define XHttp2Configuration_MinFrameSize UINT32_C(16384)
/** @brief RFC 7540 最大帧 payload 最大值。 */
#define XHttp2Configuration_MaxFrameSize UINT32_C(16777215)

/**
 * - @brief HTTP/2 会话配置。
 * - @details 默认关闭 server push、启用 Huffman，窗口分别为 65535，最大帧为 16384。
 */
typedef struct XHttp2Configuration {
    XClass m_class;                    /**< 第一个成员，继承 XClass。 */
    bool m_serverPushEnabled;          /**< 是否允许 server push。 */
    bool m_huffmanCompressionEnabled;  /**< 是否启用 HPACK Huffman。 */
    uint32_t m_sessionReceiveWindowSize;/**< 会话级接收窗口。 */
    uint32_t m_streamReceiveWindowSize; /**< 流级接收窗口。 */
    uint32_t m_maxFrameSize;           /**< 最大帧 payload。 */
} XHttp2Configuration;

/**
 * - @brief 初始化 HTTP/2 配置虚函数表。
 * - @return HTTP/2 配置虚函数表；初始化失败返回 NULL，不由调用者释放。
 */
XVtable* XHttp2Configuration_class_init(void);
/**
 * - @brief 初始化 HTTP/2 配置为 Qt 默认值。
 * - @param self 待初始化的配置对象；不能为 NULL。
 */
void XHttp2Configuration_init(XHttp2Configuration* self);
/**
 * - @brief 创建 HTTP/2 配置对象。
 * - @return 新建配置对象；调用者必须使用 XHttp2Configuration_delete_base 释放，分配失败返回 NULL。
 */
XHttp2Configuration* XHttp2Configuration_create_ex(XMemoryType memory);
/**
 * - @brief 深拷贝创建 HTTP/2 配置对象。
 * - @param other 源配置对象；借用且不能为 NULL。
 * - @return 新建配置对象；调用者必须使用 XHttp2Configuration_delete_base 释放，参数无效或分配失败返回 NULL。
 */
XHttp2Configuration* XHttp2Configuration_create_copy(const XHttp2Configuration* other);
/**
 * - @brief 移动创建 HTTP/2 配置对象。
 * - @param other 源配置对象；借用且不能为 NULL，成功后恢复为已初始化的默认状态。
 * - @return 新建配置对象；调用者必须使用 XHttp2Configuration_delete_base 释放，参数无效或分配失败返回 NULL。
 */
XHttp2Configuration* XHttp2Configuration_create_move(XHttp2Configuration* other);

/** @brief 配置生命周期和值语义入口。 */
#define XHttp2Configuration_deinit_base XClass_deinit_base
#define XHttp2Configuration_delete_base XClass_delete_base
#define XHttp2Configuration_copy_base XClass_copy_base
#define XHttp2Configuration_move_base XClass_move_base

/**
 * - @brief 设置 server push 开关。
 * - @param self 配置对象；不能为 NULL。
 * - @param enabled true 允许服务端推送，false 禁止服务端推送。
 * - @return 无；self 为 NULL 时不执行。
 */
void XHttp2Configuration_setServerPushEnabled(XHttp2Configuration* self, bool enabled);
/**
 * - @brief 获取 server push 开关。
 * - @param self 配置对象；可为 NULL。
 * - @return 是否允许服务端推送；self 为 NULL 返回 false。
 */
bool XHttp2Configuration_serverPushEnabled(const XHttp2Configuration* self);
/**
 * - @brief 设置 HPACK Huffman 压缩开关。
 * - @param self 配置对象；不能为 NULL。
 * - @param enabled true 启用 Huffman 编码，false 禁用 Huffman 编码。
 * - @return 无；self 为 NULL 时不执行。
 */
void XHttp2Configuration_setHuffmanCompressionEnabled(XHttp2Configuration* self, bool enabled);
/**
 * - @brief 获取 HPACK Huffman 压缩开关。
 * - @param self 配置对象；可为 NULL。
 * - @return 是否启用 Huffman 编码；self 为 NULL 返回 false。
 */
bool XHttp2Configuration_huffmanCompressionEnabled(const XHttp2Configuration* self);
/**
 * - @brief 设置会话级接收窗口。
 * - @param self 配置对象；不能为 NULL。
 * - @param size 窗口大小；必须为 1 到 XHttp2Configuration_MaxWindowSize。
 * - @return 设置成功返回 true；参数非法或 self 为 NULL 返回 false，旧值不变。
 */
bool XHttp2Configuration_setSessionReceiveWindowSize(XHttp2Configuration* self, uint32_t size);
/**
 * - @brief 获取会话级接收窗口。
 * - @param self 配置对象；可为 NULL。
 * - @return 窗口大小；self 为 NULL 返回默认值 65535。
 */
uint32_t XHttp2Configuration_sessionReceiveWindowSize(const XHttp2Configuration* self);
/**
 * - @brief 设置流级接收窗口。
 * - @param self 配置对象；不能为 NULL。
 * - @param size 窗口大小；必须为 1 到 XHttp2Configuration_MaxWindowSize。
 * - @return 设置成功返回 true；参数非法或 self 为 NULL 返回 false，旧值不变。
 */
bool XHttp2Configuration_setStreamReceiveWindowSize(XHttp2Configuration* self, uint32_t size);
/**
 * - @brief 获取流级接收窗口。
 * - @param self 配置对象；可为 NULL。
 * - @return 窗口大小；self 为 NULL 返回默认值 65535。
 */
uint32_t XHttp2Configuration_streamReceiveWindowSize(const XHttp2Configuration* self);
/**
 * - @brief 设置 HTTP/2 最大帧 payload 长度。
 * - @param self 配置对象；不能为 NULL。
 * - @param size 帧 payload 长度；必须为 16384 到 16777215（含边界）。
 * - @return 设置成功返回 true；参数非法或 self 为 NULL 返回 false，旧值不变。
 */
bool XHttp2Configuration_setMaxFrameSize(XHttp2Configuration* self, uint32_t size);
/**
 * - @brief 获取 HTTP/2 最大帧 payload 长度。
 * - @param self 配置对象；可为 NULL。
 * - @return 帧 payload 长度；self 为 NULL 返回默认值 16384。
 */
uint32_t XHttp2Configuration_maxFrameSize(const XHttp2Configuration* self);

#endif // XHTTP_ON
#endif /* XPROTOCOL_ON */
#ifdef __cplusplus
}
#endif


/* XClass create API default-memory wrappers. */
#undef XHttp2Configuration_create
#define XHttp2Configuration_create() XHttp2Configuration_create_ex(XCLASS_DEFAULT_MEMORY_TYPE)

#endif
