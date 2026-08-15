/**
 * @file       XHttpMultipart.h
 * - @brief      HTTP MIME multipart 请求体类型，对标 Qt 6.8 QHttpPart/QHttpMultiPart。
 * - @details    该模块只负责 MIME body 的内存构造，不直接访问平台网络 API。
 */

#ifndef XHTTPMULTIPART_H
#define XHTTPMULTIPART_H
#include "XHttp_config.h"

#ifdef __cplusplus
extern "C" {
#endif
#if XPROTOCOL_ON
#if XHTTP_ON

#include "XByteArray.h"
#include "XHttpHeaders.h"
#include "XIODevice.h"
#include "XObject.h"
#include "XVector.h"
#include <stdbool.h>
#include <stddef.h>

XCLASS_DEFINE_BEGING(XHttpPart)
XCLASS_DEFINE_EXTEND_END(XHttpPart, XClass)

/**
 * - @brief 可设置的 MIME 常用头字段。
 * - @details 该枚举使用字节值作为字段内容，避免在 HTTP 层引入 QVariant 依赖。
 */
typedef enum XHttpPart_KnownHeader {
    XHttpPart_ContentTypeHeader = 0,        /**< Content-Type。 */
    XHttpPart_ContentDispositionHeader,     /**< Content-Disposition。 */
    XHttpPart_ContentTransferEncodingHeader,/**< Content-Transfer-Encoding。 */
    XHttpPart_ContentIdHeader,              /**< Content-ID。 */
    XHttpPart_ContentDescriptionHeader      /**< Content-Description。 */
} XHttpPart_KnownHeader;

/**
 * - @brief 单个 MIME multipart 部件。
 * - @details 复制操作深拷贝头和内存 body；bodyDevice 仅借用，销毁部件时不会释放设备。
 */
typedef struct XHttpPart {
    XClass m_class;             /**< 第一个成员，继承 XClass。 */
    XHttpHeaders* m_headers;   /**< 部件头，当前对象拥有。 */
    XByteArray* m_body;        /**< 内存 body，当前对象拥有。 */
    XIODevice* m_bodyDevice;   /**< 设备 body，借用，不由当前对象释放。 */
} XHttpPart;

/**
 * - @brief 初始化 XHttpPart 虚函数表。
 * - @return 共享虚函数表；失败返回 NULL。
 */
XVtable* XHttpPart_class_init(void);

/**
 * - @brief 创建空 MIME 部件。
 * - @return 新部件；调用者必须使用 XHttpPart_delete_base 释放，失败返回 NULL。
 */
XHttpPart* XHttpPart_create_ex(XMemoryType memory);

/**
 * - @brief 深拷贝创建 MIME 部件。
 * - @param other 源部件；可为 NULL。
 * - @return 新部件；失败返回 NULL。
 */
XHttpPart* XHttpPart_create_copy(const XHttpPart* other);

/**
 * - @brief 移动创建 MIME 部件。
 * - @param other 源部件；成功后其拥有成员被置空，不能为 NULL。
 * - @return 新部件；失败返回 NULL。
 */
XHttpPart* XHttpPart_create_move(XHttpPart* other);

/**
 * - @brief 初始化调用者提供的 MIME 部件存储。
 * - @param self 待初始化部件；不能为 NULL。
 */
void XHttpPart_init(XHttpPart* self);

/**
 * - @brief 反初始化、删除和复制入口。
 * - @details 删除函数释放部件拥有的头和 body，不释放借用的 bodyDevice。
 */
#define XHttpPart_deinit_base XClass_deinit_base
#define XHttpPart_delete_base XClass_delete_base
#define XHttpPart_copy_base XClass_copy_base
#define XHttpPart_move_base XClass_move_base

/**
 * - @brief 设置原始 MIME 头。
 * - @param self 目标部件；不能为 NULL。
 * - @param name 头名称；借用，必须是 HTTP token。
 * - @param value 头值；借用，不得包含 CR、LF、NUL 或控制字符。
 * - @return 成功返回 true；参数或内存无效返回 false。
 */
bool XHttpPart_setRawHeader(XHttpPart* self, const XByteArray* name, const XByteArray* value);

/**
 * - @brief 使用 UTF-8 文本设置原始 MIME 头。
 * - @param self 目标部件；不能为 NULL。
 * - @param name 头名称 UTF-8 文本；不能为 NULL。
 * - @param value 头值 UTF-8 文本；为 NULL 时按空值处理。
 * - @return 成功返回 true；字段非法或内存不足返回 false。
 */
bool XHttpPart_setRawHeader_utf8(XHttpPart* self, const char* name, const char* value);

/**
 * - @brief 设置常用 MIME 头。
 * - @param self 目标部件；不能为 NULL。
 * - @param header 常用头枚举值。
 * - @param value 头值；借用，函数执行时深拷贝。
 * - @return 成功返回 true；枚举无效或参数无效返回 false。
 */
bool XHttpPart_setHeader(XHttpPart* self, XHttpPart_KnownHeader header, const XByteArray* value);

/**
 * - @brief 设置内存 body。
 * - @param self 目标部件；不能为 NULL。
 * - @param body body 数据；借用，函数执行时深拷贝；传 NULL 表示空 body。
 * - @return 成功返回 true；内存不足返回 false。
 * - @note 设置内存 body 会清除已设置的 bodyDevice。
 */
bool XHttpPart_setBody(XHttpPart* self, const XByteArray* body);

/**
 * - @brief 使用 UTF-8 文本设置内存 body。
 * - @param self 目标部件；不能为 NULL。
 * - @param body body 文本；为 NULL 时按空 body 处理。
 * - @return 成功返回 true；内存不足返回 false。
 */
bool XHttpPart_setBody_utf8(XHttpPart* self, const char* body);

/**
 * - @brief 设置 body 设备。
 * - @param self 目标部件；不能为 NULL。
 * - @param device 已打开且可读的设备；借用，可为 NULL 以恢复内存 body。
 * - @return 成功返回 true；当前实现仅保存借用指针。
 * - @note 设备必须在 multipart body 生成时仍然有效，部件不会关闭或释放设备。
 */
bool XHttpPart_setBodyDevice(XHttpPart* self, XIODevice* device);

/**
 * - @brief 获取部件头的只读借用对象。
 * - @param self 部件；可为 NULL。
 * - @return 内部头对象；调用者不能释放，部件销毁后失效。
 */
const XHttpHeaders* XHttpPart_headers_const(const XHttpPart* self);

/**
 * - @brief 获取内存 body 的只读借用对象。
 * - @param self 部件；可为 NULL。
 * - @return 内部 body；调用者不能释放，部件销毁后失效。
 */
const XByteArray* XHttpPart_body_const(const XHttpPart* self);

/**
 * - @brief 获取借用的 body 设备。
 * - @param self 部件；可为 NULL。
 * - @return body 设备；调用者不能释放。
 */
XIODevice* XHttpPart_bodyDevice(const XHttpPart* self);

XCLASS_DEFINE_BEGING(XHttpMultiPart)
XCLASS_DEFINE_EXTEND_END(XHttpMultiPart, XObject)

/**
 * - @brief multipart 的已知内容类型。
 * - @details 枚举语义对标 QHttpMultiPart::ContentType。
 */
typedef enum XHttpMultiPart_ContentType {
    XHttpMultiPart_MixedType = 0,       /**< multipart/mixed。 */
    XHttpMultiPart_RelatedType,         /**< multipart/related。 */
    XHttpMultiPart_FormDataType,        /**< multipart/form-data。 */
    XHttpMultiPart_AlternativeType      /**< multipart/alternative。 */
} XHttpMultiPart_ContentType;

/**
 * - @brief MIME multipart body 构造器。
 * - @details 对象拥有部件副本和 boundary；append 不保存调用者部件指针。
 */
typedef struct XHttpMultiPart {
    XObject m_class;                     /**< 第一个成员，继承 XObject。 */
    XVector* m_parts;                   /**< XHttpPart* 列表，当前对象拥有。 */
    XByteArray* m_boundary;             /**< boundary 文本，当前对象拥有。 */
    XHttpMultiPart_ContentType m_type;  /**< multipart 子类型。 */
} XHttpMultiPart;

/**
 * - @brief 初始化 XHttpMultiPart 虚函数表。
 * - @return 共享虚函数表；失败返回 NULL。
 */
XVtable* XHttpMultiPart_class_init(void);

/**
 * - @brief 创建 MixedType multipart。
 * - @return 新对象；调用者必须使用 XHttpMultiPart_delete_base 释放。
 */
XHttpMultiPart* XHttpMultiPart_create_ex(XMemoryType memory, XHttpMultiPart_ContentType type);

/**
 * - @brief 按指定类型创建 multipart。
 * - @param type multipart 子类型；必须是已知枚举值。
 * - @return 新对象；失败返回 NULL。
 */
XHttpMultiPart* XHttpMultiPart_create_type(XHttpMultiPart_ContentType type);

/**
 * - @brief 初始化 multipart。
 * - @param self 待初始化对象；不能为 NULL。
 * - @param type multipart 子类型；无效值按 MixedType 处理。
 */
void XHttpMultiPart_init(XHttpMultiPart* self, XHttpMultiPart_ContentType type);

/**
 * - @brief 反初始化和删除入口。
 * - @details 删除函数释放所有已追加部件、boundary 和对象本身。
 */
#define XHttpMultiPart_deinit_base XClass_deinit_base
#define XHttpMultiPart_delete_base XClass_delete_base
#define XHttpMultiPart_deleteLater XObject_deleteLater

/**
 * - @brief 追加一个部件副本。
 * - @param self multipart 对象；不能为 NULL。
 * - @param part 部件；借用，函数执行时深拷贝；不能为 NULL。
 * - @return 成功返回 true；内存不足或参数无效返回 false。
 */
bool XHttpMultiPart_append(XHttpMultiPart* self, const XHttpPart* part);

/**
 * - @brief 设置 multipart 子类型。
 * - @param self multipart 对象；不能为 NULL。
 * - @param type 新子类型；必须是已知枚举值。
 * - @return 成功返回 true；枚举无效返回 false。
 */
bool XHttpMultiPart_setContentType(XHttpMultiPart* self, XHttpMultiPart_ContentType type);

/**
 * - @brief 获取 boundary 的只读借用对象。
 * - @param self multipart 对象；可为 NULL。
 * - @return boundary 字节数组；调用者不能释放。
 */
const XByteArray* XHttpMultiPart_boundary_const(const XHttpMultiPart* self);

/**
 * - @brief 设置 boundary。
 * - @param self multipart 对象；不能为 NULL。
 * - @param boundary boundary 字节；借用，函数执行时深拷贝，不能为空。
 * - @return 成功返回 true；包含 CR、LF、空白或非法字符返回 false。
 */
bool XHttpMultiPart_setBoundary(XHttpMultiPart* self, const XByteArray* boundary);

/**
 * - @brief 生成完整 multipart body。
 * - @param self multipart 对象；不能为 NULL。
 * - @return 新 body；调用者必须用 XHttpMultiPart_body_delete 释放，失败返回 NULL。
 * - @note 生成设备 body 时会从当前设备位置读取到结束，不关闭、不释放设备。
 */
XByteArray* XHttpMultiPart_toByteArray(const XHttpMultiPart* self);

/**
 * - @brief 生成 Content-Type 头值。
 * - @param self multipart 对象；不能为 NULL。
 * - @return 新 UTF-8 头值；调用者必须使用 XClass_delete_base 释放。
 */
XByteArray* XHttpMultiPart_contentType(const XHttpMultiPart* self);

/**
 * - @brief 释放 multipart body 返回值。
 * - @param body XHttpMultiPart_toByteArray 返回的 body；可为 NULL。
 */
#define XHttpMultiPart_body_delete XClass_delete_base

#endif // XHTTP_ON
#endif /* XPROTOCOL_ON */
#ifdef __cplusplus
}
#endif


/* XClass create API default-memory wrappers. */
#undef XHttpPart_create
#define XHttpPart_create() XHttpPart_create_ex(XCLASS_DEFAULT_MEMORY_TYPE)
#undef XHttpMultiPart_create
#define XHttpMultiPart_create() \
	XHttpMultiPart_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, XHttpMultiPart_MixedType)

#endif
