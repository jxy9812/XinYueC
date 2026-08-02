/**
 * @file       XSqlRelationalDelegate.h
 * @brief      SQL 关系模型委托的无界面核心，对齐 Qt 6.8 QSqlRelationalDelegate。
 * @details    本库不依赖 Qt Widgets。该类提供关系字段编辑所需的数据转换，
 *             上层 GUI 可以据此创建下拉框或其他编辑器。
 */
#ifndef XSQLRELATIONALDELEGATE_H
#define XSQLRELATIONALDELEGATE_H

#ifdef __cplusplus
extern "C" {
#endif

#include "XSqlRelationalTableModel.h"
#include "XObject.h"

XCLASS_DEFINE_BEGING(XSqlRelationalDelegate)
XCLASS_DEFINE_EXTEND_END(XSqlRelationalDelegate, XObject)

/**
 * @brief SQL 关系模型委托的纯数据对象。
 * @details 对齐 Qt 的关系编辑器数据转换职责，但不创建或拥有任何 GUI 控件。
 */
typedef struct XSqlRelationalDelegate {
    XObject m_class; /**< 第一个成员，由 XObject 管理；委托不可复制。 */
} XSqlRelationalDelegate;

/**
 * @brief 初始化关系委托对象。
 * @param delegate 待初始化对象；不能为 NULL。
 * @return 无；委托继承 XObject，不支持复制。
 */
void XSqlRelationalDelegate_init(XSqlRelationalDelegate* delegate);
/**
 * @brief 创建关系委托对象。
 * @return 新委托对象，调用者必须使用 XSqlRelationalDelegate_delete_base 释放；失败返回 NULL。
 */
XSqlRelationalDelegate* XSqlRelationalDelegate_create(void);
/** @brief 调用 XClass 析构入口释放关系委托对象。 */
#define XSqlRelationalDelegate_deinit_base XClass_deinit_base
/** @brief 释放由 XSqlRelationalDelegate_create 返回的关系委托对象。 */
#define XSqlRelationalDelegate_delete_base XClass_delete_base
/**
 * @brief 按驱动规则查找字段索引。
 * @param model 表模型；不能为 NULL。
 * @param driver 数据库驱动；借用，可为 NULL。
 * @param fieldName 字段名；借用。
 * @return 字段索引；未找到返回 -1。
 */
int XSqlRelationalDelegate_fieldIndex(const XSqlTableModel* model, const XSqlDriver* driver, const XString* fieldName);
/**
 * @brief 获取关系字段显示值副本。
 * @param model 关系表模型；NULL 返回空值对象。
 * @param row 行号，从 0 开始。
 * @param column 列号，从 0 开始。
 * @return 新值对象，调用者必须使用 XVariant_delete_base 释放。
 */
XVariant* XSqlRelationalDelegate_displayValue(const XSqlRelationalTableModel* model, int row, int column);
/**
 * @brief 获取关系字段编辑值副本。
 * @param model 关系表模型；NULL 返回空值对象。
 * @param row 行号，从 0 开始。
 * @param column 列号，从 0 开始。
 * @return 新值对象，调用者必须使用 XVariant_delete_base 释放。
 */
XVariant* XSqlRelationalDelegate_editValue(const XSqlRelationalTableModel* model, int row, int column);
/**
 * @brief 将编辑器值写回关系模型。
 * @param model 关系表模型；不能为 NULL。
 * @param row 行号，从 0 开始。
 * @param column 列号，从 0 开始。
 * @param displayValue 显示值；借用，可为 NULL。
 * @param editValue 编辑值；借用，可为 NULL。
 * @return 全部写入成功返回 true，否则返回 false。
 */
bool XSqlRelationalDelegate_setModelData(XSqlRelationalTableModel* model, int row, int column, const XVariant* displayValue, const XVariant* editValue);
/**
 * @brief 创建关系列的编辑器子模型。
 * @param model 关系表模型；不能为 NULL。
 * @param column 关系列，从 0 开始。
 * @return 关系模型拥有的借用子模型；无关系时返回 NULL。
 */
XSqlTableModel* XSqlRelationalDelegate_createEditorModel(const XSqlRelationalTableModel* model, int column);

#ifdef __cplusplus
}
#endif

#endif /* XSQLRELATIONALDELEGATE_H */
