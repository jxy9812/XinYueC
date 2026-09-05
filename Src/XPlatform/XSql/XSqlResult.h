/**
 * @file       XSqlResult.h
 * @brief      SQL 查询结果抽象类，对齐 Qt 6.8 QSqlResult。
 * @details    具体数据库必须通过 vtable 重载 reset、fetch、data 等接口；
 *             本类只保存公共查询状态和绑定参数，不包含任何数据库 API。
 */
#ifndef XSQLRESULT_H
#define XSQLRESULT_H

#ifdef __cplusplus
extern "C" {
#endif

#include "XSqlError.h"
#include "XSqlRecord.h"
#include "XVariantList.h"
#include "XStringList.h"

typedef struct XSqlDriver XSqlDriver;

/**
 * @brief XSqlResult 的虚函数槽位定义。
 * @details 这些槽位仅供具体数据库驱动重载；公共调用方必须使用本头文件的
 *          *_base 分派入口，不能直接依赖槽位序号。
 */
XCLASS_DEFINE_BEGING(XSqlResult)
XCLASS_DEFINE_ENUM(XSqlResult, Data) = XCLASS_VTABLE_GET_SIZE(XClass),
XCLASS_DEFINE_ENUM(XSqlResult, IsNull),
XCLASS_DEFINE_ENUM(XSqlResult, Reset),
XCLASS_DEFINE_ENUM(XSqlResult, Fetch),
XCLASS_DEFINE_ENUM(XSqlResult, FetchNext),
XCLASS_DEFINE_ENUM(XSqlResult, FetchPrevious),
XCLASS_DEFINE_ENUM(XSqlResult, FetchFirst),
XCLASS_DEFINE_ENUM(XSqlResult, FetchLast),
XCLASS_DEFINE_ENUM(XSqlResult, Size),
XCLASS_DEFINE_ENUM(XSqlResult, NumRowsAffected),
XCLASS_DEFINE_ENUM(XSqlResult, Record),
XCLASS_DEFINE_ENUM(XSqlResult, LastInsertId),
XCLASS_DEFINE_ENUM(XSqlResult, Exec),
XCLASS_DEFINE_ENUM(XSqlResult, Prepare),
XCLASS_DEFINE_ENUM(XSqlResult, SavePrepare),
XCLASS_DEFINE_ENUM(XSqlResult, BindValuePos),
XCLASS_DEFINE_ENUM(XSqlResult, BindValueName),
XCLASS_DEFINE_ENUM(XSqlResult, SetAt),
XCLASS_DEFINE_ENUM(XSqlResult, SetActive),
XCLASS_DEFINE_ENUM(XSqlResult, SetLastError),
XCLASS_DEFINE_ENUM(XSqlResult, SetQuery),
XCLASS_DEFINE_ENUM(XSqlResult, SetSelect),
XCLASS_DEFINE_ENUM(XSqlResult, SetForwardOnly),
XCLASS_DEFINE_ENUM(XSqlResult, ExecBatch),
XCLASS_DEFINE_ENUM(XSqlResult, DetachFromResultSet),
XCLASS_DEFINE_ENUM(XSqlResult, SetNumericalPrecisionPolicy),
XCLASS_DEFINE_ENUM(XSqlResult, NextResult),
XCLASS_DEFINE_ENUM(XSqlResult, Handle),
XCLASS_DEFINE_END(XSqlResult)

/**
 * @brief 绑定语法。
 */
typedef enum XSqlBindingSyntax {
    XSqlBindingSyntax_Positional = 0, /**< 位置绑定。 */
    XSqlBindingSyntax_Named            /**< 命名绑定。 */
} XSqlBindingSyntax;

/**
 * @brief SQL 查询结果对象。
 */
typedef struct XSqlResult {
    XClass m_class;                    /**< 第一个成员，由 XClass 管理。 */
    const XSqlDriver* m_driver;        /**< 所属驱动借用指针。 */
    XSqlError m_lastError;             /**< 最近一次错误。 */
    XSqlRecord m_record;               /**< 当前结果记录描述。 */
    XVariant m_lastInsertId;           /**< 最近插入 ID。 */
    XString* m_lastQuery;              /**< 最近设置的 SQL，由对象拥有。 */
    XString* m_executedQuery;          /**< 实际执行 SQL，由对象拥有。 */
    XVariant** m_boundValues;          /**< 绑定值数组，由对象拥有。 */
    XSqlParamType* m_boundTypes;       /**< 绑定参数类型数组。 */
    XString** m_boundNames;            /**< 命名绑定数组，由对象拥有。 */
    size_t m_boundCount;               /**< 绑定参数数量。 */
    size_t m_boundCapacity;            /**< 绑定参数容量。 */
    size_t m_bindCount;                /**< addBindValue 的当前写入位置。 */
    int m_at;                          /**< 当前游标位置。 */
    int m_size;                        /**< 结果行数；未知时为 -1。 */
    int m_numRowsAffected;             /**< 受影响行数。 */
    bool m_active;                     /**< 查询是否激活。 */
    bool m_select;                     /**< 是否为 SELECT 查询。 */
    bool m_forwardOnly;                /**< 是否只允许向前遍历。 */
    bool m_positionalBindingEnabled;   /**< 是否启用位置绑定。 */
    XSqlBindingSyntax m_bindingSyntax; /**< 当前绑定语法。 */
    XSqlNumericalPrecisionPolicy m_precisionPolicy; /**< 数值精度策略。 */
    void* m_handle;                    /**< 后端句柄借用值，不由公共层释放。 */
} XSqlResult;

/**
 * @brief 初始化 XSqlResult 虚函数表。
 * @return 共享虚函数表；初始化失败返回 NULL。
 */
XVtable* XSqlResult_class_init(void);
/**
 * @brief 初始化结果对象并关联驱动。
 * @param result 待初始化结果对象；不能为 NULL。
 * @param driver 所属驱动；借用，可为 NULL。
 * @return 无；后端通过 vtable 实现数据库相关操作。
 */
void XSqlResult_init(XSqlResult* result, const XSqlDriver* driver);
/**
 * @brief 创建结果对象。
 * @param driver 所属驱动；借用，可为 NULL。
 * @return 新结果对象，调用者必须使用 XSqlResult_delete_base 释放；失败返回 NULL。
 */
XSqlResult* XSqlResult_create_ex(XMemoryType memory,  const XSqlDriver* driver);

/** @brief 调用 XClass 析构入口释放查询文本、绑定值和后端结果资源。 */
#define XSqlResult_deinit_base XClass_deinit_base
/** @brief 释放由 XSqlResult_create 系列函数返回的结果对象。 */
#define XSqlResult_delete_base XClass_delete_base

/**
 * @brief 获取当前游标位置。
 * @param result 结果对象；NULL 返回 BeforeFirstRow。
 * @return 游标位置；特殊位置使用 XSqlLocation 表示。
 */
int XSqlResult_at(const XSqlResult* result);
/**
 * @brief 获取最近设置的查询文本。
 * @param result 结果对象；NULL 返回空字符串对象。
 * @return 新字符串对象，调用者必须使用 XString_delete_base 释放。
 */
XString* XSqlResult_lastQuery(const XSqlResult* result);
/**
 * @brief 获取最近执行的查询文本。
 * @param result 结果对象；NULL 返回空字符串对象。
 * @return 新字符串对象，调用者必须使用 XString_delete_base 释放。
 */
XString* XSqlResult_executedQuery(const XSqlResult* result);
/**
 * @brief 获取最近错误的副本。
 * @param result 结果对象；NULL 返回未知错误对象。
 * @return 新错误对象，调用者必须使用 XSqlError_delete_base 释放。
 */
XSqlError* XSqlResult_lastError(const XSqlResult* result);
/**
 * @brief 判断结果是否位于有效记录。
 * @param result 结果对象；NULL 返回 false。
 * @return 游标不在 BeforeFirstRow 或 AfterLastRow 时返回 true；active 状态独立判断。
 */
bool XSqlResult_isValid(const XSqlResult* result);
/**
 * @brief 判断结果是否已激活。
 * @param result 结果对象；NULL 返回 false。
 * @return 已激活返回 true，否则返回 false。
 */
bool XSqlResult_isActive(const XSqlResult* result);
/**
 * @brief 判断结果是否为查询结果集。
 * @param result 结果对象；NULL 返回 false。
 * @return SELECT 结果返回 true，否则返回 false。
 */
bool XSqlResult_isSelect(const XSqlResult* result);
/**
 * @brief 判断结果是否只允许向前遍历。
 * @param result 结果对象；NULL 返回 false。
 * @return 启用 forward-only 返回 true，否则返回 false。
 */
bool XSqlResult_isForwardOnly(const XSqlResult* result);
/**
 * @brief 获取所属驱动借用指针。
 * @param result 结果对象；NULL 返回 NULL。
 * @return 驱动借用指针，结果对象销毁后失效，调用者不得释放。
 */
const XSqlDriver* XSqlResult_driver(const XSqlResult* result);
/**
 * @brief 设置游标位置。
 * @param result 结果对象；不能为 NULL。
 * @param at 新游标位置。
 * @return 无；具体后端可以重载。
 */
void XSqlResult_setAt_base(XSqlResult* result, int at);
/**
 * @brief 设置结果激活状态。
 * @param result 结果对象；不能为 NULL。
 * @param active 是否激活。
 * @return 无。
 */
void XSqlResult_setActive_base(XSqlResult* result, bool active);
/**
 * @brief 设置最近错误。
 * @param result 结果对象；不能为 NULL。
 * @param error 错误对象；借用，函数会复制内容。
 * @return 无；error 为 NULL 时保持原错误。
 */
void XSqlResult_setLastError_base(XSqlResult* result, const XSqlError* error);
/**
 * @brief 设置最近查询文本。
 * @param result 结果对象；不能为 NULL。
 * @param query SQL 文本；借用，函数会复制内容，可为 NULL。
 * @return 无。
 */
void XSqlResult_setQuery_base(XSqlResult* result, const XString* query);
/**
 * @brief 设置 SELECT 标志。
 * @param result 结果对象；不能为 NULL。
 * @param select 是否为 SELECT 查询。
 * @return 无。
 */
void XSqlResult_setSelect_base(XSqlResult* result, bool select);
/**
 * @brief 设置只向前状态。
 * @param result 结果对象；不能为 NULL。
 * @param forwardOnly 是否只向前遍历。
 * @return 无。
 */
void XSqlResult_setForwardOnly_base(XSqlResult* result, bool forwardOnly);
/**
 * @brief 执行已准备查询。
 * @param result 结果对象；不能为 NULL。
 * @return 执行成功返回 true，否则返回 false。
 */
bool XSqlResult_exec_base(XSqlResult* result);
/**
 * @brief 准备 SQL 查询。
 * @param result 结果对象；不能为 NULL。
 * @param query SQL 文本；借用，函数会复制内容。
 * @return 准备成功返回 true，否则返回 false。
 */
bool XSqlResult_prepare_base(XSqlResult* result, const XString* query);
/**
 * @brief 使用驱动特定规则准备 SQL 查询。
 * @param result 结果对象；不能为 NULL。
 * @param query SQL 文本；借用。
 * @return 准备成功返回 true，否则返回 false。
 */
bool XSqlResult_savePrepare_base(XSqlResult* result, const XString* query);
/**
 * @brief 按位置绑定参数。
 * @param result 结果对象；不能为 NULL。
 * @param position 参数位置，从 0 开始。
 * @param value 参数值；借用，函数会复制内容。
 * @param type 参数方向和属性。
 * @return 无；非法参数时对象保持不变。
 */
void XSqlResult_bindValue_base(XSqlResult* result, int position, const XVariant* value, XSqlParamType type);
/**
 * @brief 按 XString 占位符名称绑定参数。
 * @param result 结果对象；不能为 NULL。
 * @param name 占位符名称；借用，可为 NULL。
 * @param value 参数值；借用，函数深复制，可为 NULL 表示 SQL NULL。
 * @param type 参数方向和属性；使用 XSqlParamTypeFlag 组合。
 * @return 无；名称无效或内存不足时绑定状态保持可用部分。
 */
void XSqlResult_bindValue_2_base(XSqlResult* result, const XString* name, const XVariant* value, XSqlParamType type);
/**
 * @brief 按 UTF-8 占位符名称绑定参数。
 * @param result 结果对象；不能为 NULL。
 * @param name 占位符名称；借用。
 * @param value 参数值；借用，函数会复制内容。
 * @param type 参数方向和属性。
 * @return 无；非法参数时对象保持不变。
 */
void XSqlResult_bindValue_utf8_base(XSqlResult* result, const char* name, const XVariant* value, XSqlParamType type);
/**
 * @brief 追加位置绑定参数。
 * @param result 结果对象；不能为 NULL。
 * @param value 参数值；借用，函数会复制内容。
 * @param type 参数方向和属性。
 * @return 无。
 */
void XSqlResult_addBindValue(XSqlResult* result, const XVariant* value, XSqlParamType type);
/**
 * @brief 按 UTF-8 名称获取绑定值副本。
 * @param result 结果对象；不能为 NULL。
 * @param name 占位符名称；借用。
 * @return 新值对象，调用者必须使用 XVariant_delete_base 释放；不存在时返回空值对象。
 */
XVariant* XSqlResult_boundValue_utf8(const XSqlResult* result, const char* name);
/**
 * @brief 按 XString 占位符名称获取绑定值副本。
 * @param result 结果对象；可为 NULL。
 * @param name 占位符名称；借用，可为 NULL。
 * @return 新值对象所有权；调用者使用 XVariant_delete_base 释放，未找到返回空值对象。
 */
XVariant* XSqlResult_boundValue_2(const XSqlResult* result, const XString* name);
/**
 * @brief 按位置获取绑定值副本。
 * @param result 结果对象；不能为 NULL。
 * @param position 参数位置，从 0 开始。
 * @return 新值对象，调用者必须使用 XVariant_delete_base 释放；越界时返回空值对象。
 */
XVariant* XSqlResult_boundValue(const XSqlResult* result, int position);
/**
 * @brief 获取全部绑定值副本。
 * @param result 结果对象；不能为 NULL。
 * @return 新列表，调用者必须使用 XVariantList_delete_base 释放。
 */
XVariantList* XSqlResult_boundValues(const XSqlResult* result);
/**
 * @brief 获取命名绑定名称列表。
 * @param result 结果对象；不能为 NULL。
 * @return 新列表，调用者必须使用 XStringList_delete_base 释放。
 */
XStringList* XSqlResult_boundValueNames(const XSqlResult* result);
/**
 * @brief 按位置获取绑定名称副本。
 * @param result 结果对象；不能为 NULL。
 * @param position 参数位置，从 0 开始。
 * @return 新字符串，调用者必须使用 XString_delete_base 释放；不存在时返回空字符串对象。
 */
XString* XSqlResult_boundValueName(const XSqlResult* result, int position);
/**
 * @brief 按 UTF-8 名称获取绑定参数类型。
 * @param result 结果对象；不能为 NULL。
 * @param name 占位符名称；借用。
 * @return 绑定类型；未找到返回 In。
 */
XSqlParamType XSqlResult_bindValueType_utf8(const XSqlResult* result, const char* name);
/**
 * @brief 按位置获取绑定参数类型。
 * @param result 结果对象；不能为 NULL。
 * @param position 参数位置，从 0 开始。
 * @return 绑定类型；越界返回 In。
 */
XSqlParamType XSqlResult_bindValueType(const XSqlResult* result, int position);
/**
 * @brief 获取绑定参数数量。
 * @param result 结果对象；NULL 返回 0。
 * @return 当前绑定参数数量。
 */
int XSqlResult_boundValueCount(const XSqlResult* result);
/**
 * @brief 判断是否存在输出参数。
 * @param result 结果对象；NULL 返回 false。
 * @return 存在输出或输入输出参数返回 true。
 */
bool XSqlResult_hasOutValues(const XSqlResult* result);
/**
 * @brief 获取绑定语法。
 * @param result 结果对象；NULL 返回 Positional。
 * @return 当前绑定语法。
 */
XSqlBindingSyntax XSqlResult_bindingSyntax(const XSqlResult* result);
/**
 * @brief 获取当前记录字段值。
 * @param result 结果对象；不能为 NULL。
 * @param field 字段索引，从 0 开始。
 * @return 新值对象，调用者必须使用 XVariant_delete_base 释放。
 */
XVariant* XSqlResult_data_base(XSqlResult* result, int field);
/**
 * @brief 判断当前字段是否为 NULL。
 * @param result 结果对象；不能为 NULL。
 * @param field 字段索引，从 0 开始。
 * @return 字段为 NULL 返回 true，否则返回 false。
 */
bool XSqlResult_isNull_base(XSqlResult* result, int field);
/**
 * @brief 重新执行查询。
 * @param result 结果对象；不能为 NULL。
 * @param query SQL 文本；借用。
 * @return 执行成功返回 true，否则返回 false。
 */
bool XSqlResult_reset_base(XSqlResult* result, const XString* query);
/**
 * @brief 移动到指定记录。
 * @param result 结果对象；不能为 NULL。
 * @param index 目标行索引，从 0 开始。
 * @return 移动成功返回 true，否则返回 false。
 */
bool XSqlResult_fetch_base(XSqlResult* result, int index);
/**
 * @brief 移动到下一条记录。
 * @param result 结果对象；不能为 NULL。
 * @return 移动成功返回 true，否则返回 false。
 */
bool XSqlResult_fetchNext_base(XSqlResult* result);
/**
 * @brief 移动到上一条记录。
 * @param result 结果对象；不能为 NULL。
 * @return 移动成功返回 true，否则返回 false。
 */
bool XSqlResult_fetchPrevious_base(XSqlResult* result);
/**
 * @brief 移动到第一条记录。
 * @param result 结果对象；不能为 NULL。
 * @return 移动成功返回 true，否则返回 false。
 */
bool XSqlResult_fetchFirst_base(XSqlResult* result);
/**
 * @brief 移动到最后一条记录。
 * @param result 结果对象；不能为 NULL。
 * @return 移动成功返回 true，否则返回 false。
 */
bool XSqlResult_fetchLast_base(XSqlResult* result);
/**
 * @brief 获取结果行数。
 * @param result 结果对象；NULL 或未知时返回 -1。
 * @return 结果行数。
 */
int XSqlResult_size_base(XSqlResult* result);
/**
 * @brief 获取受影响行数。
 * @param result 结果对象；NULL 或未知时返回 -1。
 * @return 受影响行数。
 */
int XSqlResult_numRowsAffected_base(XSqlResult* result);
/**
 * @brief 获取当前记录描述副本。
 * @param result 结果对象；NULL 返回空记录对象。
 * @return 新记录对象，调用者必须使用 XSqlRecord_delete_base 释放。
 */
XSqlRecord* XSqlResult_record_base(XSqlResult* result);
/**
 * @brief 获取最后插入 ID 副本。
 * @param result 结果对象；NULL 返回空值对象。
 * @return 新值对象，调用者必须使用 XVariant_delete_base 释放。
 */
XVariant* XSqlResult_lastInsertId_base(XSqlResult* result);
/**
 * @brief 批量执行绑定参数。
 * @param result 结果对象；不能为 NULL。
 * @param mode 批量展开模式。
 * @return 执行成功返回 true，否则返回 false。
 */
bool XSqlResult_execBatch_base(XSqlResult* result, XSqlBatchExecutionMode mode);
/**
 * @brief 从后端结果集解除关联。
 * @param result 结果对象；不能为 NULL。
 * @return 无。
 */
void XSqlResult_detachFromResultSet_base(XSqlResult* result);
/**
 * @brief 设置数值精度策略。
 * @param result 结果对象；不能为 NULL。
 * @param policy 数值精度策略。
 * @return 无；对齐 Qt QSqlResult::setNumericalPrecisionPolicy 虚函数。
 */
void XSqlResult_setNumericalPrecisionPolicy_base(XSqlResult* result, XSqlNumericalPrecisionPolicy policy);
/**
 * @brief 获取数值精度策略。
 * @param result 结果对象；NULL 返回 HighPrecision。
 * @return 当前策略。
 */
XSqlNumericalPrecisionPolicy XSqlResult_numericalPrecisionPolicy(const XSqlResult* result);
/**
 * @brief 启用或禁用位置绑定。
 * @param result 结果对象；不能为 NULL。
 * @param enable 是否启用。
 * @return 无。
 */
void XSqlResult_setPositionalBindingEnabled(XSqlResult* result, bool enable);
/**
 * @brief 查询位置绑定是否启用。
 * @param result 结果对象；NULL 返回 false。
 * @return 已启用返回 true，否则返回 false。
 */
bool XSqlResult_isPositionalBindingEnabled(const XSqlResult* result);
/**
 * @brief 移动到下一个结果集。
 * @param result 结果对象；不能为 NULL。
 * @return 成功切换返回 true，否则返回 false。
 */
bool XSqlResult_nextResult_base(XSqlResult* result);
/**
 * @brief 获取后端结果句柄。
 * @param result 结果对象；不能为 NULL。
 * @return 后端借用句柄；公共层不释放，未实现时返回 NULL。
 */
void* XSqlResult_handle_base(XSqlResult* result);
/**
 * @brief 清空结果状态和绑定参数。
 * @param result 结果对象；不能为 NULL。
 * @return 无；对象保持已初始化状态。
 */
void XSqlResult_clear(XSqlResult* result);
/**
 * @brief 清零绑定参数数量。
 * @param result 结果对象；不能为 NULL。
 * @return 无；已分配绑定容量保留。
 */
void XSqlResult_resetBindCount(XSqlResult* result);

#ifdef __cplusplus
}
#endif


/* XClass create API default-memory wrappers. */
#undef XSqlResult_create
#define XSqlResult_create(...) XSqlResult_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, __VA_ARGS__)

#endif /* XSQLRESULT_H */
