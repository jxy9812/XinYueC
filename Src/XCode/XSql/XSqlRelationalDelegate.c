/**
 * @file       XSqlRelationalDelegate.c
 * @brief      SQL 关系模型委托的无界面核心实现。
 */
#include "XSqlRelationalDelegate.h"

#include <string.h>

XVtable* XSqlRelationalDelegate_class_init(void)
{
    XVTABLE_CREAT_DEFAULT
#if VTABLE_ISSTACK
    XVTABLE_STACK_INIT_DEFAULT(XSqlRelationalDelegate)
#else
    XVTABLE_HEAP_INIT_DEFAULT
#endif
    XVTABLE_INHERIT_XCLASS(XObject);
    return XVTABLE_DEFAULT;
}

void XSqlRelationalDelegate_init(XSqlRelationalDelegate* delegate)
{
    if (!delegate) return;
    memset(delegate, 0, sizeof(*delegate));
    XObject_init(&delegate->m_class);
    XClassSetVtable(delegate, XSqlRelationalDelegate);
}

XSqlRelationalDelegate* XSqlRelationalDelegate_create(void)
{
    XSqlRelationalDelegate* delegate = (XSqlRelationalDelegate*)XMalloc_System(sizeof(XSqlRelationalDelegate));
    if (!delegate) return NULL;
    XSqlRelationalDelegate_init(delegate);
    Set_Class_MemoryFree(delegate, XFree_System);
    return delegate;
}

int XSqlRelationalDelegate_fieldIndex(const XSqlTableModel* model, const XSqlDriver* driver, const XString* fieldName) { if (!model || !fieldName) return -1; XString* stripped = XSqlDriver_isIdentifierEscaped_base(driver, fieldName, XSqlIdentifierType_FieldName) ? XSqlDriver_stripDelimiters_base(driver, fieldName, XSqlIdentifierType_FieldName) : XString_create_copy(fieldName); int result = stripped ? XSqlTableModel_fieldIndex(model, XString_toUtf8(stripped)) : -1; if (stripped) XString_delete_base(stripped); return result; }
XVariant* XSqlRelationalDelegate_displayValue(const XSqlRelationalTableModel* model, int row, int column) { return XSqlRelationalTableModel_data(model, row, column, XSqlItemDataRole_Display); }
XVariant* XSqlRelationalDelegate_editValue(const XSqlRelationalTableModel* model, int row, int column) { return XSqlRelationalTableModel_data(model, row, column, XSqlItemDataRole_Edit); }
bool XSqlRelationalDelegate_setModelData(XSqlRelationalTableModel* model, int row, int column, const XVariant* displayValue, const XVariant* editValue)
{
    const XVariant* value = editValue ? editValue : displayValue;
    return value && XSqlRelationalTableModel_setData(model, row, column, value,
                                                      XSqlItemDataRole_Edit);
}
XSqlTableModel* XSqlRelationalDelegate_createEditorModel(const XSqlRelationalTableModel* model, int column) { return XSqlRelationalTableModel_relationModel(model, column); }
