/**
 * @file       XSqlField.h
 * @brief      SQL 字段描述类，对齐 Qt 6.8 QSqlField。
 * @details    字段对象同时保存元数据、当前值和默认值；所有返回的字符串和值均为独立副本，
 *             除非 API 名称以 _const 结尾。
 */
#ifndef XSQLFIELD_H
#define XSQLFIELD_H

#ifdef __cplusplus
extern "C" {
#endif

#include "XSqlGlobal.h"
#include "XString.h"

XCLASS_DEFINE_BEGING(XSqlField)
XCLASS_DEFINE_EXTEND_END(XSqlField, XClass)

/**
 * @brief SQL 字段描述和字段值。
 */
typedef struct XSqlField {
    XClass m_class;             /**< 第一个成员，由 XClass 管理。 */
    XString* m_name;            /**< 字段名，由对象拥有。 */
    XString* m_tableName;       /**< 所属表名，由对象拥有。 */
    XVariant m_value;           /**< 当前字段值。 */
    XVariant m_defaultValue;    /**< 字段默认值。 */
    int m_metaType;              /**< XVariantType 类型值。 */
    int m_sqlType;               /**< 数据库原生类型编号。 */
    XSqlFieldRequiredStatus m_requiredStatus; /**< 必需状态。 */
    int m_length;                /**< 字段长度，未知时为 -1。 */
    int m_precision;             /**< 小数精度，未知时为 -1。 */
    bool m_readOnly;             /**< 是否只读。 */
    bool m_generated;            /**< 是否参与生成的 SQL。 */
    bool m_autoValue;            /**< 是否由数据库自动生成。 */
} XSqlField;

/**
 * @brief 初始化字段对象虚函数表。
 * @return 共享虚函数表；初始化失败返回 NULL。
 */
XVtable* XSqlField_class_init(void);
/**
 * @brief 初始化空字段。
 * @param field 待初始化字段；不能为 NULL。
 * @return 无。
 */
void XSqlField_init(XSqlField* field);
/**
 * @brief 使用字段名、类型和表名初始化字段。
 * @param field 待初始化字段；不能为 NULL。
 * @param fieldName 字段名；借用，函数会复制，可为 NULL。
 * @param metaType XVariant 类型编号。
 * @param tableName 所属表名；借用，函数会复制，可为 NULL。
 * @return 无。
 */
void XSqlField_init_ex(XSqlField* field, const XString* fieldName,
                       int metaType, const XString* tableName);
/**
 * @brief 创建空字段。
 * @return 新字段，调用者必须使用 XSqlField_delete_base 释放；失败返回 NULL。
 */
/**
 * @brief 创建字段。
 * @param fieldName 字段名；借用。
 * @param metaType XVariant 类型编号。
 * @param tableName 所属表名；借用。
 * @return 新字段，调用者必须使用 XSqlField_delete_base 释放；失败返回 NULL。
 */
XSqlField* XSqlField_create_ex(XMemoryType memory, const XString* fieldName, int metaType,
                               const XString* tableName);
/**
 * @brief 深拷贝创建字段。
 * @param other 源字段；借用，不能为 NULL。
 * @return 新字段，调用者必须使用 XSqlField_delete_base 释放；失败返回 NULL。
 */
XSqlField* XSqlField_create_copy(const XSqlField* other);
/**
 * @brief 移动创建字段。
 * @param other 源字段；不能为 NULL，成功后资源被移出。
 * @return 新字段，调用者必须使用 XSqlField_delete_base 释放；失败返回 NULL。
 */
XSqlField* XSqlField_create_move(XSqlField* other);

/** @brief 调用 XClass 析构入口释放字段名称和字段值。 */
#define XSqlField_deinit_base XClass_deinit_base
/** @brief 释放由 XSqlField_create 系列函数返回的字段对象。 */
#define XSqlField_delete_base XClass_delete_base

/** @brief 交换两个字段对象内容。 @param left 左字段；不能为 NULL。 @param right 右字段；不能为 NULL。 @return 无；元数据、当前值和默认值一并交换。 */
void XSqlField_swap(XSqlField* left, XSqlField* right);

/** @brief 设置字段当前值。 @param field 字段对象；不能为 NULL。 @param value 字段值；借用并深复制，可为 NULL 以设置 SQL NULL。 @return 无；内存不足时保留旧值。 */
void XSqlField_setValue(XSqlField* field, const XVariant* value);
/** @brief 获取字段当前值副本。 @param field 字段对象；可为 NULL。 @return 新值所有权；调用者使用 XVariant_delete_base 释放。 */
XVariant* XSqlField_value(const XSqlField* field);
/** @brief 获取字段内部值借用指针。 @param field 字段对象；可为 NULL。 @return 只读内部值；调用者不得释放或修改，字段销毁或赋新值后失效。 */
const XVariant* XSqlField_value_const(const XSqlField* field);
/** @brief 设置字段名。 @param field 字段对象；不能为 NULL。 @param name 字段名；借用并深复制，可为 NULL 以清空。 @return 无；内存不足时保留旧名称。 */
void XSqlField_setName(XSqlField* field, const XString* name);
/** @brief 获取字段名副本。 @param field 字段对象；可为 NULL。 @return 新字符串所有权；调用者使用 XString_delete_base 释放。 */
XString* XSqlField_name(const XSqlField* field);
/** @brief 设置所属表名。 @param field 字段对象；不能为 NULL。 @param name 表名；借用并深复制，可为 NULL 以清空。 @return 无；内存不足时保留旧名称。 */
void XSqlField_setTableName(XSqlField* field, const XString* name);
/** @brief 获取所属表名副本。 @param field 字段对象；可为 NULL。 @return 新字符串所有权；调用者使用 XString_delete_base 释放。 */
XString* XSqlField_tableName(const XSqlField* field);
/** @brief 将字段当前值置为 SQL NULL。 @param field 字段对象；不能为 NULL。 @return 无；不会修改默认值或元数据。 */
void XSqlField_clear(XSqlField* field);
/** @brief 判断字段当前值是否为 SQL NULL。 @param field 字段对象；可为 NULL。 @return 为 NULL 或字段指针为空返回 true。 */
bool XSqlField_isNull(const XSqlField* field);
/** @brief 设置只读标志。 @param field 字段对象；不能为 NULL。 @param readOnly 是否只读。 @return 无；该标志仅影响模型生成的可编辑性。 */
void XSqlField_setReadOnly(XSqlField* field, bool readOnly);
/** @brief 获取只读标志。 @param field 字段对象；可为 NULL。 @return 只读返回 true；NULL 返回 false。 */
bool XSqlField_isReadOnly(const XSqlField* field);
/** @brief 设置自动值标志。 @param field 字段对象；不能为 NULL。 @param autoValue 是否由数据库自动生成。 @return 无；不会直接写入数据库。 */
void XSqlField_setAutoValue(XSqlField* field, bool autoValue);
/** @brief 获取自动值标志。 @param field 字段对象；可为 NULL。 @return 自动生成返回 true；NULL 返回 false。 */
bool XSqlField_isAutoValue(const XSqlField* field);
/** @brief 设置 XVariant 元类型。 @param field 字段对象；不能为 NULL。 @param metaType XVariantType 编号。 @return 无；不会转换当前值。 */
void XSqlField_setMetaType(XSqlField* field, int metaType);
/** @brief 获取 XVariant 元类型。 @param field 字段对象；可为 NULL。 @return 元类型编号；NULL 返回 0。 */
int XSqlField_metaType(const XSqlField* field);
/** @brief 获取 Qt 兼容字段类型编号。 @param field 字段对象；可为 NULL。 @return 等价于 XSqlField_metaType 的元类型编号。 */
int XSqlField_type(const XSqlField* field);
/** @brief 设置 Qt 兼容字段类型编号。 @param field 字段对象；不能为 NULL。 @param type XVariantType 编号。 @return 无；等价于 XSqlField_setMetaType。 */
void XSqlField_setType(XSqlField* field, int type);
/** @brief 设置数据库原生类型编号。 @param field 字段对象；不能为 NULL。 @param sqlType 驱动定义的原生类型编号。 @return 无；不会转换当前值。 */
void XSqlField_setSqlType(XSqlField* field, int sqlType);
/** @brief 获取数据库原生类型编号。 @param field 字段对象；可为 NULL。 @return 驱动类型编号；NULL 返回 0。 */
int XSqlField_typeId(const XSqlField* field);
/** @brief 获取 Qt 6.8 typeID 兼容别名。 @param field 字段对象；可为 NULL。 @return 等价于 XSqlField_typeId 的原生类型编号。 */
int XSqlField_typeID(const XSqlField* field);
/** @brief 设置字段必需状态。 @param field 字段对象；不能为 NULL。 @param status 必需状态。 @return 无；该元数据不会自动校验当前值。 */
void XSqlField_setRequiredStatus(XSqlField* field, XSqlFieldRequiredStatus status);
/** @brief 设置字段是否必需。 @param field 字段对象；不能为 NULL。 @param required 为 true 设为 Required，否则设为 Optional。 @return 无。 */
void XSqlField_setRequired(XSqlField* field, bool required);
/** @brief 获取字段必需状态。 @param field 字段对象；可为 NULL。 @return 必需状态；NULL 返回 Unknown。 */
XSqlFieldRequiredStatus XSqlField_requiredStatus(const XSqlField* field);
/** @brief 设置字段长度。 @param field 字段对象；不能为 NULL。 @param length 声明长度；未知时使用 -1。 @return 无。 */
void XSqlField_setLength(XSqlField* field, int length);
/** @brief 获取字段长度。 @param field 字段对象；可为 NULL。 @return 声明长度；NULL 或未知返回 -1。 */
int XSqlField_length(const XSqlField* field);
/** @brief 设置字段数值精度。 @param field 字段对象；不能为 NULL。 @param precision 小数精度；未知时使用 -1。 @return 无。 */
void XSqlField_setPrecision(XSqlField* field, int precision);
/** @brief 获取字段数值精度。 @param field 字段对象；可为 NULL。 @return 小数精度；NULL 或未知返回 -1。 */
int XSqlField_precision(const XSqlField* field);
/** @brief 设置字段默认值。 @param field 字段对象；不能为 NULL。 @param value 默认值；借用并深复制，可为 NULL 以设为 SQL NULL。 @return 无；内存不足时保留旧默认值。 */
void XSqlField_setDefaultValue(XSqlField* field, const XVariant* value);
/** @brief 获取字段默认值副本。 @param field 字段对象；可为 NULL。 @return 新值所有权；调用者使用 XVariant_delete_base 释放。 */
XVariant* XSqlField_defaultValue(const XSqlField* field);
/** @brief 设置是否参与 SQL 生成。 @param field 字段对象；不能为 NULL。 @param generated 是否纳入 INSERT/UPDATE 语句。 @return 无；不会修改字段值。 */
void XSqlField_setGenerated(XSqlField* field, bool generated);
/** @brief 查询是否参与 SQL 生成。 @param field 字段对象；可为 NULL。 @return 纳入生成语句返回 true；NULL 返回 false。 */
bool XSqlField_isGenerated(const XSqlField* field);
/** @brief 判断字段描述是否有效。 @param field 字段对象；可为 NULL。 @return 拥有非空字段名返回 true；NULL 或空名称返回 false。 */
bool XSqlField_isValid(const XSqlField* field);
/** @brief 比较两个字段。 @param left 左字段；可为 NULL，按空字段处理。 @param right 右字段；可为 NULL，按空字段处理。 @return 全部元数据和值相等返回 true，否则返回 false。 */
bool XSqlField_equals(const XSqlField* left, const XSqlField* right);

#ifdef __cplusplus
}
#endif


/* XClass create API default-memory wrappers. */
#undef XSqlField_create
#define XSqlField_create() \
	XSqlField_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, NULL, XVariantType_NULL, NULL)

#endif /* XSQLFIELD_H */
