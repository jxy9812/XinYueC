/**
 * @file       XSqlError.h
 * @brief      SQL 错误信息类，对齐 Qt 6.8 QSqlError。
 * @details    错误文本由对象拥有；返回的新文本对象由调用者使用
 *             XString_delete_base 释放。
 */
#ifndef XSQLERROR_H
#define XSQLERROR_H

#ifdef __cplusplus
extern "C" {
#endif

#include "XSqlGlobal.h"
#include "XString.h"

XCLASS_DEFINE_BEGING(XSqlError)
XCLASS_DEFINE_EXTEND_END(XSqlError, XClass)

/**
 * @brief SQL 错误对象。
 */
typedef struct XSqlError {
    XClass m_class;          /**< 第一个成员，由 XClass 管理，禁止手工修改。 */
    XString* m_driverText;   /**< 驱动错误文本，由对象拥有。 */
    XString* m_databaseText; /**< 数据库错误文本，由对象拥有。 */
    XString* m_errorCode;    /**< 数据库原生错误码，由对象拥有。 */
    XSqlErrorType m_type;    /**< 错误类型。 */
} XSqlError;

/**
 * @brief 初始化错误对象虚函数表。
 * @return 共享虚函数表；初始化失败返回 NULL。
 */
XVtable* XSqlError_class_init(void);

/**
 * @brief 初始化栈上错误对象。
 * @param error 待初始化对象；不能为 NULL。
 * @return 无。
 */
void XSqlError_init(XSqlError* error);

/**
 * @brief 创建 SQL 错误对象。
 * @param driverText 驱动错误文本；NULL 按空文本处理。
 * @param databaseText 数据库错误文本；NULL 按空文本处理。
 * @param type 错误类型。
 * @param errorCode 原生错误码；NULL 按空文本处理。
 * @return 新对象；失败返回 NULL，调用者使用 XSqlError_delete_base 释放。
 */
XSqlError* XSqlError_create_ex(XMemoryType memory,  const XString* driverText,
                            const XString* databaseText,
                            XSqlErrorType type,
                            const XString* errorCode);
/**
 * @brief 使用 UTF-8 文本创建 SQL 错误。
 * @param driverText 驱动错误文本；借用，可为 NULL。
 * @param databaseText 数据库错误文本；借用，可为 NULL。
 * @param type 错误类型。
 * @param errorCode 原生错误码；借用，可为 NULL。
 * @return 新错误对象，调用者必须使用 XSqlError_delete_base 释放；失败返回 NULL。
 */
XSqlError* XSqlError_create_utf8(const char* driverText, const char* databaseText,
                                 XSqlErrorType type, const char* errorCode);
/**
 * @brief 深拷贝创建 SQL 错误。
 * @param other 源错误；借用，不能为 NULL。
 * @return 新错误对象，调用者必须使用 XSqlError_delete_base 释放；失败返回 NULL。
 */
XSqlError* XSqlError_create_copy(const XSqlError* other);
/**
 * @brief 移动创建 SQL 错误。
 * @param other 源错误；不能为 NULL，成功后资源被移出。
 * @return 新错误对象，调用者必须使用 XSqlError_delete_base 释放；失败返回 NULL。
 */
XSqlError* XSqlError_create_move(XSqlError* other);

/** @brief 调用 XClass 析构入口释放错误对象拥有的文本。 */
#define XSqlError_deinit_base XClass_deinit_base
/** @brief 释放由 XSqlError_create 系列函数返回的错误对象。 */
#define XSqlError_delete_base XClass_delete_base

/** @brief 交换两个错误对象内容。 @param left 左错误对象；不能为 NULL。 @param right 右错误对象；不能为 NULL。 @return 无；三个文本与错误类型一并交换。 */
void XSqlError_swap(XSqlError* left, XSqlError* right);

/** @brief 设置驱动错误文本。 @param error 错误对象；不能为 NULL。 @param text 驱动文本；借用并深复制，可为 NULL 以清空。 @return 无；内存不足时保留旧文本。 */
void XSqlError_setDriverText(XSqlError* error, const XString* text);
/** @brief 设置数据库错误文本。 @param error 错误对象；不能为 NULL。 @param text 数据库文本；借用并深复制，可为 NULL 以清空。 @return 无；内存不足时保留旧文本。 */
void XSqlError_setDatabaseText(XSqlError* error, const XString* text);
/** @brief 设置数据库原生错误码。 @param error 错误对象；不能为 NULL。 @param code 错误码；借用并深复制，可为 NULL 以清空。 @return 无；内存不足时保留旧文本。 */
void XSqlError_setNativeErrorCode(XSqlError* error, const XString* code);
/** @brief 设置错误类型。 @param error 错误对象；不能为 NULL。 @param type 新错误类型。 @return 无；不会修改现有错误文本。 */
void XSqlError_setType(XSqlError* error, XSqlErrorType type);
/** @brief 获取驱动错误文本副本。 @param error 错误对象；可为 NULL。 @return 新字符串所有权；调用者使用 XString_delete_base 释放。 */
XString* XSqlError_driverText(const XSqlError* error);
/** @brief 获取数据库错误文本副本。 @param error 错误对象；可为 NULL。 @return 新字符串所有权；调用者使用 XString_delete_base 释放。 */
XString* XSqlError_databaseText(const XSqlError* error);
/** @brief 获取原生错误码副本。 @param error 错误对象；可为 NULL。 @return 新字符串所有权；调用者使用 XString_delete_base 释放。 */
XString* XSqlError_nativeErrorCode(const XSqlError* error);
/** @brief 获取错误类型。 @param error 错误对象；可为 NULL。 @return 错误类型；NULL 时返回 UnknownError。 */
XSqlErrorType XSqlError_type(const XSqlError* error);
/** @brief 拼接驱动和数据库错误文本。 @param error 错误对象；可为 NULL。 @return 新字符串所有权；调用者使用 XString_delete_base 释放。 */
XString* XSqlError_text(const XSqlError* error);
/** @brief 判断错误对象是否表示错误。 @param error 错误对象；可为 NULL。 @return 错误类型不是 NoError 返回 true；NULL 返回 false。 */
bool XSqlError_isValid(const XSqlError* error);
/**
 * @brief 比较两个错误对象。
 * @param left 左错误；可为 NULL。
 * @param right 右错误；可为 NULL。
 * @return 内容相等返回 true，否则返回 false。
 */
bool XSqlError_equals(const XSqlError* left, const XSqlError* right);

#ifdef __cplusplus
}
#endif


/* XClass create API default-memory wrappers. */
#undef XSqlError_create
#define XSqlError_create(...) XSqlError_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, __VA_ARGS__)

#endif /* XSQLERROR_H */
