/**
 * @file       XHttp2Headers.h
 * - @brief      HTTP/2 HPACK 静态表和头块编解码。
 * - @details    头部名称允许 RFC 7540 的伪字段；对象不直接访问套接字。
 */

#ifndef XHTTP2HEADERS_H
#define XHTTP2HEADERS_H
#include "XHttp_config.h"

#ifdef __cplusplus
extern "C" {
#endif
#if XPROTOCOL_ON
#if XHTTP_ON

#include "XClass.h"
#include "XByteArray.h"
#include "XVector.h"
#include <stdbool.h>
#include <stddef.h>

XCLASS_DEFINE_BEGING(XHttp2HeaderList)
XCLASS_DEFINE_EXTEND_END(XHttp2HeaderList, XClass)

/** @brief HTTP/2 名值字段；名称和值由所属列表拥有。 */
typedef struct XHttp2HeaderField {
    XByteArray* m_name;   /**< 字段名称；对象拥有。 */
    XByteArray* m_value;  /**< 字段值；对象拥有。 */
} XHttp2HeaderField;

/** @brief HPACK 头字段列表。 */
typedef struct XHttp2HeaderList {
    XClass m_class;             /**< 第一个成员，继承 XClass。 */
    XHttp2HeaderField* m_fields; /**< 字段数组；对象拥有。 */
    size_t m_size;              /**< 字段数量。 */
    size_t m_capacity;          /**< 字段容量。 */
} XHttp2HeaderList;

XCLASS_DEFINE_BEGING(XHttp2HeaderDecoder)
XCLASS_DEFINE_EXTEND_END(XHttp2HeaderDecoder, XClass)

XCLASS_DEFINE_BEGING(XHttp2HeaderEncoder)
XCLASS_DEFINE_EXTEND_END(XHttp2HeaderEncoder, XClass)

/**
 * - @brief 有状态的 HPACK 解码器，对齐 Qt HPack::Decoder 的连接级生命周期。
 * - @details 动态表跨多个 HEADERS/CONTINUATION 头块保留；一个 HTTP/2 连接应只使用
 *          一个实例，连接关闭时销毁。
 */
typedef struct XHttp2HeaderDecoder {
    XClass m_class;                    /**< 第一个成员，继承 XClass。 */
    XHttp2HeaderField* m_dynamicFields; /**< 动态表字段；对象拥有。 */
    size_t m_dynamicSize;              /**< 动态表字段数。 */
    size_t m_dynamicCapacity;          /**< 动态表分配容量。 */
    size_t m_dynamicDataSize;          /**< 动态表 RFC 7541 字节大小。 */
    size_t m_dynamicMaxSize;           /**< 当前头块允许的动态表容量。 */
    size_t m_maxSize;                  /**< 对端 SETTINGS 允许的最大动态表容量。 */
} XHttp2HeaderDecoder;

/**
 * - @brief 有状态的 HPACK 编码器，对齐 Qt HPack::Encoder 的连接级生命周期。
 * - @details 动态表跨多个请求头块保留；收到对端 SETTINGS_HEADER_TABLE_SIZE 后，
 *          下一头块会自动携带动态表大小更新。
 */
typedef struct XHttp2HeaderEncoder {
    XClass m_class;                    /**< 第一个成员，继承 XClass。 */
    XHttp2HeaderField* m_dynamicFields; /**< 动态表字段；对象拥有。 */
    size_t m_dynamicSize;              /**< 动态表字段数。 */
    size_t m_dynamicCapacity;          /**< 动态表分配容量。 */
    size_t m_dynamicDataSize;          /**< 动态表 RFC 7541 字节大小。 */
    size_t m_dynamicMaxSize;           /**< 当前编码动态表容量。 */
    size_t m_maxSize;                  /**< 对端 SETTINGS 允许的最大容量。 */
    bool m_sizeUpdatePending;          /**< 下一头块是否须发送容量更新。 */
} XHttp2HeaderEncoder;

/**
 * - @brief 初始化 HTTP/2 头字段列表虚函数表。
 * - @return 头字段列表虚函数表；初始化失败返回 NULL，不由调用者释放。
 */
XVtable* XHttp2HeaderList_class_init(void);
/**
 * - @brief 初始化 HPACK 解码器虚函数表。
 * - @return HPACK 解码器虚函数表；初始化失败返回 NULL，不由调用者释放。
 */
XVtable* XHttp2HeaderDecoder_class_init(void);
/**
 * - @brief 初始化 HPACK 编码器虚函数表。
 * - @return HPACK 编码器虚函数表；初始化失败返回 NULL，不由调用者释放。
 */
XVtable* XHttp2HeaderEncoder_class_init(void);
/**
 * - @brief 初始化空 HTTP/2 头字段列表。
 * - @param self 待初始化的头字段列表；不能为 NULL。
 */
void XHttp2HeaderList_init(XHttp2HeaderList* self);
/**
 * - @brief 创建空 HTTP/2 头字段列表。
 * - @return 新建头字段列表；调用者必须使用 XHttp2HeaderList_delete_base 释放，分配失败返回 NULL。
 */
XHttp2HeaderList* XHttp2HeaderList_create(void);
#define XHttp2HeaderList_deinit_base XClass_deinit_base
#define XHttp2HeaderList_delete_base XClass_delete_base
#define XHttp2HeaderList_copy_base XClass_copy_base
#define XHttp2HeaderList_move_base XClass_move_base

/**
 * - @brief 初始化有状态 HPACK 解码器。
 * - @param self 待初始化的解码器；不能为 NULL。
 */
void XHttp2HeaderDecoder_init(XHttp2HeaderDecoder* self);
/**
 * - @brief 创建有状态 HPACK 解码器。
 * - @return 新建解码器；调用者必须使用 XHttp2HeaderDecoder_delete_base 释放，分配失败返回 NULL。
 */
XHttp2HeaderDecoder* XHttp2HeaderDecoder_create(void);
#define XHttp2HeaderDecoder_deinit_base XClass_deinit_base
#define XHttp2HeaderDecoder_delete_base XClass_delete_base
#define XHttp2HeaderDecoder_copy_base XClass_copy_base
#define XHttp2HeaderDecoder_move_base XClass_move_base

/**
 * - @brief 初始化有状态 HPACK 编码器。
 * - @param self 待初始化的编码器；不能为 NULL。
 */
void XHttp2HeaderEncoder_init(XHttp2HeaderEncoder* self);
/**
 * - @brief 创建有状态 HPACK 编码器。
 * - @return 新建编码器；调用者必须使用 XHttp2HeaderEncoder_delete_base 释放，分配失败返回 NULL。
 */
XHttp2HeaderEncoder* XHttp2HeaderEncoder_create(void);
#define XHttp2HeaderEncoder_deinit_base XClass_deinit_base
#define XHttp2HeaderEncoder_delete_base XClass_delete_base
#define XHttp2HeaderEncoder_copy_base XClass_copy_base
#define XHttp2HeaderEncoder_move_base XClass_move_base

/**
 * - @brief 设置对端 SETTINGS_HEADER_TABLE_SIZE 上限。
 * - @param self 解码器；不能为 NULL。
 * - @param size 动态表最大字节数；范围为 0 到 65536。
 * - @return 成功返回 true；参数非法或内存状态不变返回 false。
 */
bool XHttp2HeaderDecoder_setMaxDynamicTableSize(XHttp2HeaderDecoder* self,
                                                size_t size);
/**
 * - @brief 获取解码器允许的最大动态表大小。
 * - @param self HPACK 解码器；可为 NULL。
 * - @return 最大动态表字节数；self 为空时返回 0。
 */
size_t XHttp2HeaderDecoder_maxDynamicTableSize(const XHttp2HeaderDecoder* self);

/**
 * - @brief 设置对端 SETTINGS_HEADER_TABLE_SIZE 上限。
 * - @param self 编码器；不能为 NULL。
 * - @param size 对端允许的动态表最大字节数；范围为 0 到 65536。
 * - @return 成功返回 true；参数非法返回 false，旧状态保持不变。
 * - @note 容量变更会在下一次 XHttp2HeaderEncoder_encode 中发出 HPACK size update。
 */
bool XHttp2HeaderEncoder_setMaxDynamicTableSize(XHttp2HeaderEncoder* self,
                                                size_t size);
/**
 * - @brief 获取编码器允许的最大动态表大小。
 * - @param self HPACK 编码器；可为 NULL。
 * - @return 最大动态表字节数；self 为空时返回 0。
 */
size_t XHttp2HeaderEncoder_maxDynamicTableSize(const XHttp2HeaderEncoder* self);

/**
 * - @brief 解码一个完整 HPACK 头块并更新连接级动态表。
 * - @param self 解码器；不能为 NULL。
 * - @param data 头块字节；借用，函数返回后可释放；size 为 0 时可为 NULL。
 * - @param size 头块字节数。
 * - @return 新头字段列表；调用者必须释放；协议错误或内存不足返回 NULL。
 */
XHttp2HeaderList* XHttp2HeaderDecoder_decode(XHttp2HeaderDecoder* self,
                                             const void* data, size_t size);

/**
 * - @brief 编码一个头块并维护连接级 HPACK 动态表。
 * - @param self 编码器；不能为 NULL。
 * - @param headers 待编码头字段；借用，不能为 NULL。
 * - @param enableHuffman true 使用 RFC 7541 Huffman 字符串编码，false 使用原始字符串。
 * - @return 新头块字节；调用者必须释放；参数非法或内存不足返回 NULL。
 */
XByteArray* XHttp2HeaderEncoder_encode(XHttp2HeaderEncoder* self,
                                       const XHttp2HeaderList* headers,
                                       bool enableHuffman);

/**
 * - @brief 追加头字段。
 * - @param self 头字段列表；不能为 NULL。
 * - @param name 字段名称；借用，函数执行时深拷贝；普通字段必须小写，伪字段可用冒号开头。
 * - @param value 字段值；借用，函数执行时深拷贝；不能包含 CR/LF/NUL。
 * - @return 成功返回 true；非法字段或内存不足返回 false。
 */
bool XHttp2HeaderList_append(XHttp2HeaderList* self,
                             const XByteArray* name,
                             const XByteArray* value);
/**
 * - @brief 获取头字段数量。
 * - @param self 头字段列表；可为 NULL。
 * - @return 字段数量；self 为空时返回 0。
 */
size_t XHttp2HeaderList_size(const XHttp2HeaderList* self);
/**
 * - @brief 获取指定头字段名称。
 * - @param self 头字段列表；可为 NULL。
 * - @param index 字段索引；必须小于 XHttp2HeaderList_size 的返回值。
 * - @return 名称借用指针；索引无效或 self 为空时返回 NULL，调用者不得释放或修改。
 */
const XByteArray* XHttp2HeaderList_nameAt_const(const XHttp2HeaderList* self, size_t index);
/**
 * - @brief 获取指定头字段值。
 * - @param self 头字段列表；可为 NULL。
 * - @param index 字段索引；必须小于 XHttp2HeaderList_size 的返回值。
 * - @return 值借用指针；索引无效或 self 为空时返回 NULL，调用者不得释放或修改。
 */
const XByteArray* XHttp2HeaderList_valueAt_const(const XHttp2HeaderList* self, size_t index);

/**
 * - @brief HPACK 编码头块。
 * - @param self 头字段列表；不能为 NULL。
 * - @param enableHuffman 是否使用 RFC 7541 Huffman 字符串编码；false 使用原始字面量。
 * - @return 新字节数组；调用者必须释放；失败返回 NULL。
 */
XByteArray* XHttp2HeaderList_encode(const XHttp2HeaderList* self, bool enableHuffman);

/**
 * - @brief HPACK 解码头块。
 * - @param data 编码头块；借用，不能为 NULL。
 * - @param size 字节数。
 * - @return 新头字段列表；调用者必须释放；格式错误、非法填充或非法动态表索引返回 NULL。
 */
XHttp2HeaderList* XHttp2HeaderList_decode(const void* data, size_t size);

#endif // XHTTP_ON
#endif /* XPROTOCOL_ON */
#ifdef __cplusplus
}
#endif

#endif /* XHTTP2HEADERS_H */
