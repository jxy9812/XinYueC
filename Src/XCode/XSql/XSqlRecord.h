/**
 * @file       XSqlRecord.h
 * @brief      SQL 记录类，对齐 Qt 6.8 QSqlRecord。
 * @details    记录深拥有字段副本，字段按插入顺序排列；按名称访问使用驱动无关的精确名称比较。
 */
#ifndef XSQLRECORD_H
#define XSQLRECORD_H

#ifdef __cplusplus
extern "C" {
#endif

#include "XSqlField.h"

XCLASS_DEFINE_BEGING(XSqlRecord)
XCLASS_DEFINE_EXTEND_END(XSqlRecord, XClass)

/**
 * @brief SQL 字段记录集合。
 */
typedef struct XSqlRecord {
    XClass m_class;        /**< 第一个成员，由 XClass 管理。 */
    XSqlField** m_fields;  /**< 字段指针数组，由记录对象拥有。 */
    size_t m_count;        /**< 当前字段数量。 */
    size_t m_capacity;     /**< 字段数组容量。 */
} XSqlRecord;

/**
 * @brief 初始化记录对象虚函数表。
 * @return 共享虚函数表；初始化失败返回 NULL。
 */
XVtable* XSqlRecord_class_init(void);
/** @brief 初始化空记录。 @param record 待初始化记录；不能为 NULL。 @return 无；对象进入可析构的空记录状态。 */
void XSqlRecord_init(XSqlRecord* record);
/** @brief 创建空记录。 @return 新记录所有权；调用者使用 XSqlRecord_delete_base 释放，失败返回 NULL。 */
XSqlRecord* XSqlRecord_create_ex(XMemoryType memory);
/** @brief 深拷贝创建记录。 @param other 源记录；借用，不能为 NULL。 @return 新记录所有权；调用者使用 XSqlRecord_delete_base 释放，失败返回 NULL。 */
XSqlRecord* XSqlRecord_create_copy(const XSqlRecord* other);
/** @brief 移动创建记录。 @param other 源记录；不能为 NULL，成功后资源被移出但对象仍需反初始化。 @return 新记录所有权；调用者使用 XSqlRecord_delete_base 释放，失败返回 NULL。 */
XSqlRecord* XSqlRecord_create_move(XSqlRecord* other);

/** @brief 调用 XClass 析构入口释放记录拥有的字段数组。 */
#define XSqlRecord_deinit_base XClass_deinit_base
/** @brief 释放由 XSqlRecord_create 系列函数返回的记录对象。 */
#define XSqlRecord_delete_base XClass_delete_base
/** @brief 将记录对象深复制到既有目标对象的基础复制入口。 */
#define XSqlRecord_copy_base XClass_copy_base
/** @brief 将记录对象资源移入既有目标对象的基础移动入口。 */
#define XSqlRecord_move_base XClass_move_base

/** @brief 交换两个记录对象内容。 @param left 左记录；不能为 NULL。 @param right 右记录；不能为 NULL。 @return 无；字段数组所有权一并交换。 */
void XSqlRecord_swap(XSqlRecord* left, XSqlRecord* right);

/** @brief 按位置获取字段值副本。 @param record 记录对象；可为 NULL。 @param index 字段索引，从 0 开始。 @return 新值所有权；调用者使用 XVariant_delete_base 释放，越界返回空值对象。 */
XVariant* XSqlRecord_value(const XSqlRecord* record, int index);
/** @brief 按 UTF-8 字段名获取字段值副本。 @param record 记录对象；可为 NULL。 @param name 字段名；借用，可为 NULL。 @return 新值所有权；调用者使用 XVariant_delete_base 释放，未找到返回空值对象。 */
XVariant* XSqlRecord_value_utf8(const XSqlRecord* record, const char* name);
/** @brief 按 XString 字段名获取字段值副本。 @param record 记录对象；可为 NULL。 @param name 字段名；借用，可为 NULL。 @return 新值所有权；调用者使用 XVariant_delete_base 释放，未找到返回空值对象。 */
XVariant* XSqlRecord_value_2(const XSqlRecord* record, const XString* name);
/** @brief 按位置设置字段值。 @param record 记录对象；不能为 NULL。 @param index 字段索引，从 0 开始。 @param value 字段值；借用并深复制，可为 NULL 表示 SQL NULL。 @return 无；越界或内存不足时保持原字段值。 */
void XSqlRecord_setValue(XSqlRecord* record, int index, const XVariant* value);
/** @brief 按 UTF-8 字段名设置字段值。 @param record 记录对象；不能为 NULL。 @param name 字段名；借用，可为 NULL。 @param value 字段值；借用并深复制，可为 NULL 表示 SQL NULL。 @return 无；名称不存在或内存不足时保持不变。 */
void XSqlRecord_setValue_utf8(XSqlRecord* record, const char* name, const XVariant* value);
/** @brief 按 XString 字段名设置字段值。 @param record 记录对象；不能为 NULL。 @param name 字段名；借用，可为 NULL。 @param value 字段值；借用并深复制，可为 NULL 表示 SQL NULL。 @return 无；名称不存在或内存不足时保持不变。 */
void XSqlRecord_setValue_2(XSqlRecord* record, const XString* name, const XVariant* value);
/** @brief 按位置设置字段为 SQL NULL。 @param record 记录对象；不能为 NULL。 @param index 字段索引，从 0 开始。 @return 无；越界时保持记录不变。 */
void XSqlRecord_setNull(XSqlRecord* record, int index);
/** @brief 按 UTF-8 字段名设置字段为 SQL NULL。 @param record 记录对象；不能为 NULL。 @param name 字段名；借用，可为 NULL。 @return 无；名称不存在时保持记录不变。 */
void XSqlRecord_setNull_utf8(XSqlRecord* record, const char* name);
/** @brief 按 XString 字段名设置字段为 SQL NULL。 @param record 记录对象；不能为 NULL。 @param name 字段名；借用，可为 NULL。 @return 无；名称不存在时保持记录不变。 */
void XSqlRecord_setNull_2(XSqlRecord* record, const XString* name);
/** @brief 判断按位置访问的字段是否为 SQL NULL。 @param record 记录对象；可为 NULL。 @param index 字段索引，从 0 开始。 @return 字段为 NULL、记录为 NULL 或越界返回 true。 */
bool XSqlRecord_isNull(const XSqlRecord* record, int index);
/** @brief 按 UTF-8 字段名判断字段是否为 SQL NULL。 @param record 记录对象；可为 NULL。 @param name 字段名；借用，可为 NULL。 @return 字段为 NULL 或名称未找到返回 true。 */
bool XSqlRecord_isNull_utf8(const XSqlRecord* record, const char* name);
/** @brief 按 XString 字段名判断字段是否为 SQL NULL。 @param record 记录对象；可为 NULL。 @param name 字段名；借用，可为 NULL。 @return 字段为 NULL 或名称未找到返回 true。 */
bool XSqlRecord_isNull_2(const XSqlRecord* record, const XString* name);
/** @brief 按 UTF-8 字段名查找索引。 @param record 记录对象；可为 NULL。 @param name 字段名；借用，可为 NULL。 @return 字段索引；未找到返回 -1。 */
int XSqlRecord_indexOf_utf8(const XSqlRecord* record, const char* name);
/** @brief 按 XString 字段名查找索引。 @param record 记录对象；可为 NULL。 @param name 字段名；借用，可为 NULL。 @return 字段索引；未找到返回 -1。 */
int XSqlRecord_indexOf(const XSqlRecord* record, const XString* name);
/** @brief 获取字段名副本。 @param record 记录对象；可为 NULL。 @param index 字段索引，从 0 开始。 @return 新字符串所有权；调用者使用 XString_delete_base 释放，越界返回空字符串。 */
XString* XSqlRecord_fieldName(const XSqlRecord* record, int index);
/** @brief 获取字段描述副本。 @param record 记录对象；可为 NULL。 @param index 字段索引，从 0 开始。 @return 新字段所有权；调用者使用 XSqlField_delete_base 释放，越界返回空字段。 */
XSqlField* XSqlRecord_field(const XSqlRecord* record, int index);
/** @brief 按 UTF-8 名称获取字段描述副本。 @param record 记录对象；可为 NULL。 @param name 字段名；借用，可为 NULL。 @return 新字段所有权；调用者使用 XSqlField_delete_base 释放，未找到返回空字段。 */
XSqlField* XSqlRecord_field_utf8(const XSqlRecord* record, const char* name);
/** @brief 按 XString 名称获取字段描述副本。 @param record 记录对象；可为 NULL。 @param name 字段名；借用，可为 NULL。 @return 新字段所有权；调用者使用 XSqlField_delete_base 释放，未找到返回空字段。 */
XSqlField* XSqlRecord_field_2(const XSqlRecord* record, const XString* name);
/** @brief 获取内部字段描述借用指针。 @param record 记录对象；可为 NULL。 @param index 字段索引，从 0 开始。 @return 只读字段借用指针；不得释放或修改，越界返回 NULL。 */
const XSqlField* XSqlRecord_field_const(const XSqlRecord* record, int index);
/** @brief 判断字段是否参与 SQL 生成。 @param record 记录对象；可为 NULL。 @param index 字段索引，从 0 开始。 @return 纳入生成语句返回 true；NULL 或越界返回 false。 */
bool XSqlRecord_isGenerated(const XSqlRecord* record, int index);
/** @brief 设置字段是否参与 SQL 生成。 @param record 记录对象；不能为 NULL。 @param index 字段索引，从 0 开始。 @param generated 是否纳入生成语句。 @return 无；越界时保持记录不变。 */
void XSqlRecord_setGenerated(XSqlRecord* record, int index, bool generated);
/** @brief 按 UTF-8 名称设置字段生成标志。 @param record 记录对象；不能为 NULL。 @param name 字段名；借用，可为 NULL。 @param generated 是否纳入生成语句。 @return 无；未找到时保持记录不变。 */
void XSqlRecord_setGenerated_utf8(XSqlRecord* record, const char* name, bool generated);
/** @brief 按 XString 名称设置字段生成标志。 @param record 记录对象；不能为 NULL。 @param name 字段名；借用，可为 NULL。 @param generated 是否纳入生成语句。 @return 无；未找到时保持记录不变。 */
void XSqlRecord_setGenerated_2(XSqlRecord* record, const XString* name, bool generated);
/** @brief 追加字段副本。 @param record 记录对象；不能为 NULL。 @param field 字段；借用并深复制，不能为 NULL。 @return 成功返回 true；内存不足或参数无效返回 false。 */
bool XSqlRecord_append(XSqlRecord* record, const XSqlField* field);
/** @brief 替换指定字段。 @param record 记录对象；不能为 NULL。 @param index 字段索引，从 0 开始。 @param field 字段；借用并深复制，不能为 NULL。 @return 成功返回 true；越界、内存不足或参数无效返回 false。 */
bool XSqlRecord_replace(XSqlRecord* record, int index, const XSqlField* field);
/** @brief 插入字段副本。 @param record 记录对象；不能为 NULL。 @param index 插入位置，允许等于字段数以追加。 @param field 字段；借用并深复制，不能为 NULL。 @return 成功返回 true；位置无效、内存不足或参数无效返回 false。 */
bool XSqlRecord_insert(XSqlRecord* record, int index, const XSqlField* field);
/** @brief 删除指定字段。 @param record 记录对象；不能为 NULL。 @param index 字段索引，从 0 开始。 @return 成功返回 true；越界或记录为 NULL 返回 false。 */
bool XSqlRecord_remove(XSqlRecord* record, int index);
/** @brief 判断记录是否不含字段。 @param record 记录对象；可为 NULL。 @return 没有字段或记录为 NULL 返回 true。 */
bool XSqlRecord_isEmpty(const XSqlRecord* record);
/** @brief 判断是否包含 UTF-8 字段名。 @param record 记录对象；可为 NULL。 @param name 字段名；借用，可为 NULL。 @return 包含返回 true，否则返回 false。 */
bool XSqlRecord_contains_utf8(const XSqlRecord* record, const char* name);
/** @brief 判断是否包含 XString 字段名。 @param record 记录对象；可为 NULL。 @param name 字段名；借用，可为 NULL。 @return 包含返回 true，否则返回 false。 */
bool XSqlRecord_contains(const XSqlRecord* record, const XString* name);
/** @brief 清空全部字段。 @param record 记录对象；不能为 NULL。 @return 无；释放所有字段并保留可继续使用的空记录状态。 */
void XSqlRecord_clear(XSqlRecord* record);
/** @brief 将所有字段当前值置为 SQL NULL。 @param record 记录对象；不能为 NULL。 @return 无；字段描述、默认值和生成标志保持不变。 */
void XSqlRecord_clearValues(XSqlRecord* record);
/** @brief 获取字段数量。 @param record 记录对象；可为 NULL。 @return 字段数量；NULL 返回 0。 */
int XSqlRecord_count(const XSqlRecord* record);
/** @brief 按键字段提取键值记录副本。 @param record 源记录；可为 NULL。 @param keyFields 键字段描述；借用，可为 NULL。 @return 新记录所有权；调用者使用 XSqlRecord_delete_base 释放。 */
XSqlRecord* XSqlRecord_keyValues(const XSqlRecord* record, const XSqlRecord* keyFields);
/** @brief 比较两个记录内容。 @param left 左记录；可为 NULL，按空记录处理。 @param right 右记录；可为 NULL，按空记录处理。 @return 字段数量、元数据和值均相等返回 true，否则返回 false。 */
bool XSqlRecord_equals(const XSqlRecord* left, const XSqlRecord* right);

#ifdef __cplusplus
}
#endif


/* XClass create API default-memory wrappers. */
#undef XSqlRecord_create
#define XSqlRecord_create(...) XSqlRecord_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, ##__VA_ARGS__)

#endif /* XSQLRECORD_H */
