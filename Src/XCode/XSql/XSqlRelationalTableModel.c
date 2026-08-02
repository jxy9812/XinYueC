/**
 * @file       XSqlRelationalTableModel.c
 * @brief      SQL 关系表模型实现。
 */
#include "XSqlRelationalTableModel.h"

#include <string.h>

static void VXSqlRelationalTableModel_deinit(XSqlRelationalTableModel* model);
static void VXSqlRelationalTableModel_clear(XSqlRelationalTableModel* model);
static bool VXSqlRelationalTableModel_select(XSqlRelationalTableModel* model);
static void VXSqlRelationalTableModel_setTable(XSqlRelationalTableModel* model, const char* tableName);
static void VXSqlRelationalTableModel_setRelation(XSqlRelationalTableModel* model, int column, const XSqlRelation* relation);
static XSqlTableModel* VXSqlRelationalTableModel_relationModel(const XSqlRelationalTableModel* model, int column);
static void VXSqlRelationalTableModel_revertRow(XSqlRelationalTableModel* model, int row);
static XString* VXSqlRelationalTableModel_selectStatement(const XSqlRelationalTableModel* model);
static XString* VXSqlRelationalTableModel_orderByClause(const XSqlRelationalTableModel* model);
static bool VXSqlRelationalTableModel_updateRowInTable(XSqlTableModel* model, int row,
                                                       const XSqlRecord* values);
static bool VXSqlRelationalTableModel_insertRowIntoTable(XSqlTableModel* model,
                                                         const XSqlRecord* values);

static void xsql_relational_clear_relations(XSqlRelationalTableModel* model)
{
    if (!model) return;
    for (size_t i = 0; i < model->m_relationCount; ++i) {
        if (model->m_relationModels && model->m_relationModels[i])
            XSqlTableModel_delete_base(model->m_relationModels[i]);
    }
    for (size_t i = 0; i < model->m_relationCount; ++i) {
        if (model->m_relations[i]) XSqlRelation_delete_base(model->m_relations[i]);
    }
    if (model->m_relations) XFree_System(model->m_relations);
    if (model->m_relationModels) XFree_System(model->m_relationModels);
    model->m_relations = NULL;
    model->m_relationModels = NULL;
    model->m_relationCount = 0;
    model->m_relationCapacity = 0;
}

static bool xsql_relational_reserve(XSqlRelationalTableModel* model, size_t wanted)
{
    XSqlRelation** relations;
    XSqlTableModel** relationModels;
    if (wanted <= model->m_relationCapacity) return true;
    size_t capacity = model->m_relationCapacity ? model->m_relationCapacity * 2 : 8;
    while (capacity < wanted) capacity *= 2;
    relations = (XSqlRelation**)XMalloc_System(capacity * sizeof(XSqlRelation*));
    relationModels = (XSqlTableModel**)XMalloc_System(capacity * sizeof(XSqlTableModel*));
    if (!relations || !relationModels) {
        if (relations) XFree_System(relations);
        if (relationModels) XFree_System(relationModels);
        return false;
    }
    memset(relations, 0, capacity * sizeof(XSqlRelation*));
    memset(relationModels, 0, capacity * sizeof(XSqlTableModel*));
    if (model->m_relationCount) {
        memcpy(relations, model->m_relations, model->m_relationCount * sizeof(XSqlRelation*));
        memcpy(relationModels, model->m_relationModels,
               model->m_relationCount * sizeof(XSqlTableModel*));
    }
    if (model->m_relations) XFree_System(model->m_relations);
    if (model->m_relationModels) XFree_System(model->m_relationModels);
    model->m_relations = relations;
    model->m_relationModels = relationModels;
    model->m_relationCapacity = capacity;
    return true;
}

XVtable* XSqlRelationalTableModel_class_init(void)
{
    XVTABLE_CREAT_DEFAULT
#if VTABLE_ISSTACK
    XVTABLE_STACK_INIT_DEFAULT(XCLASS_VTABLE_GET_SIZE(XSqlRelationalTableModel))
#else
    XVTABLE_HEAP_INIT_DEFAULT
#endif
    XVTABLE_INHERIT_XCLASS(XSqlTableModel);
    XVTABLE_ADD_FUNC_LIST_DEFAULT(((void*[]){
        VXSqlRelationalTableModel_setRelation,
        VXSqlRelationalTableModel_relationModel
    }));
    XVTABLE_OVERLOAD_DEFAULT(EXSqlQueryModel_Clear, VXSqlRelationalTableModel_clear);
    XVTABLE_OVERLOAD_DEFAULT(EXSqlTableModel_Select, VXSqlRelationalTableModel_select);
    XVTABLE_OVERLOAD_DEFAULT(EXSqlTableModel_SetTable, VXSqlRelationalTableModel_setTable);
    XVTABLE_OVERLOAD_DEFAULT(EXSqlTableModel_RevertRow, VXSqlRelationalTableModel_revertRow);
    XVTABLE_OVERLOAD_DEFAULT(EXSqlTableModel_SelectStatement, VXSqlRelationalTableModel_selectStatement);
    XVTABLE_OVERLOAD_DEFAULT(EXSqlTableModel_OrderByClause, VXSqlRelationalTableModel_orderByClause);
    XVTABLE_OVERLOAD_DEFAULT(EXSqlTableModel_UpdateRowInTable, VXSqlRelationalTableModel_updateRowInTable);
    XVTABLE_OVERLOAD_DEFAULT(EXSqlTableModel_InsertRowIntoTable, VXSqlRelationalTableModel_insertRowIntoTable);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Deinit, VXSqlRelationalTableModel_deinit);
    return XVTABLE_DEFAULT;
}

void XSqlRelationalTableModel_init(XSqlRelationalTableModel* model, const XSqlDatabase* database)
{
    if (!model) return;
    memset(((unsigned char*)model) + sizeof(XSqlTableModel), 0, sizeof(*model) - sizeof(XSqlTableModel));
    XSqlTableModel_init(&model->m_parent, database);
    XClassSetVtable(model, XSqlRelationalTableModel);
    XSqlRecord_init(&model->m_baseRecord);
    model->m_joinMode = XSqlRelationJoinMode_InnerJoin;
}

XSqlRelationalTableModel* XSqlRelationalTableModel_create(const XSqlDatabase* database)
{
    XSqlRelationalTableModel* model = (XSqlRelationalTableModel*)XMalloc_System(sizeof(XSqlRelationalTableModel));
    if (!model) return NULL;
    memset(model, 0, sizeof(*model));
    XSqlRelationalTableModel_init(model, database);
    Set_Class_MemoryFree(model, XFree_System);
    return model;
}

static void VXSqlRelationalTableModel_deinit(XSqlRelationalTableModel* model)
{
    if (!model) return;
    xsql_relational_clear_relations(model);
    XSqlRecord_deinit_base(&model->m_baseRecord);
    XClass_Deinit_Parent(XSqlTableModel, model);
}

static const XSqlRelation* xsql_relational_relation_const(const XSqlRelationalTableModel* model, int column)
{
    return model && column >= 0 && (size_t)column < model->m_relationCount ? model->m_relations[column] : NULL;
}

static XString* xsql_relational_variant_text(const XVariant* value)
{
    XString* result = value ? XVariant_toString(value) : NULL;
    int type = value ? XVariant_type((XVariant*)value) : XVariantType_NULL;
    if (!result && type >= XVariantType_Uint8 && type <= XVariantType_Size_t)
        result = XString_create_fmt_utf8("%lld", (long long)XVariant_toInt64(value));
    return result;
}

static XVariant* xsql_relational_lookup(const XSqlRelationalTableModel* model, int row, int column)
{
    const XSqlRelation* relation = xsql_relational_relation_const(model, column);
    if (!relation || !XSqlRelation_isValid(relation)) return NULL;
    XSqlTableModel* child = XSqlRelationalTableModel_relationModel(model, column);
    if (!child) return NULL;
    XString* indexName = XSqlRelation_indexColumn(relation);
    XString* displayName = XSqlRelation_displayColumn(relation);
    int indexColumn = XSqlTableModel_fieldIndex(child, XString_toUtf8(indexName));
    int displayColumn = XSqlTableModel_fieldIndex(child, XString_toUtf8(displayName));
    XVariant* source = XSqlTableModel_data(&model->m_parent, row, column, XSqlItemDataRole_Edit);
    XVariant* result = NULL;
    if (source && indexColumn >= 0 && displayColumn >= 0) {
        for (int childRow = 0; childRow < XSqlTableModel_rowCount(child); ++childRow) {
            XVariant* indexValue = XSqlTableModel_data(child, childRow, indexColumn, XSqlItemDataRole_Edit);
            bool equal = indexValue && source
                && XVariant_type(indexValue) == XVariant_type(source)
                && XVariant_compare(indexValue, source) == 0;
            if (!equal && indexValue && source) {
                XString* left = xsql_relational_variant_text(indexValue);
                XString* right = xsql_relational_variant_text(source);
                equal = left && right && XString_equals(left, right, XChar_CaseSensitive);
                if (left) XString_delete_base(left);
                if (right) XString_delete_base(right);
            }
            if (equal) {
                result = XSqlTableModel_data(child, childRow, displayColumn, XSqlItemDataRole_Display);
                XVariant_delete_base(indexValue);
                break;
            }
            if (indexValue) XVariant_delete_base(indexValue);
        }
    }
    if (!result) result = source ? XVariant_create_copy(source) : XVariant_create_null();
    if (source) XVariant_delete_base(source);
    if (indexName) XString_delete_base(indexName);
    if (displayName) XString_delete_base(displayName);
    return result;
}

XVariant* XSqlRelationalTableModel_data(const XSqlRelationalTableModel* model, int row, int column, XSqlItemDataRole role)
{
    if (!model) return XVariant_create_null();
    if (role == XSqlItemDataRole_Display
        && model->m_parent.m_strategy != XSqlTableEditStrategy_OnFieldChange
        && row >= 0 && (size_t)row < model->m_parent.m_parent.m_rowCount
        && model->m_parent.m_dirty && model->m_parent.m_dirty[row]) {
        XVariant* relationValue = xsql_relational_lookup(model, row, column);
        if (relationValue) return relationValue;
    }
    return XSqlTableModel_data(&model->m_parent, row, column, role);
}

bool XSqlRelationalTableModel_setData(XSqlRelationalTableModel* model, int row, int column,
                                      const XVariant* value, XSqlItemDataRole role)
{
    const XSqlRelation* relation;
    XSqlTableModel* child;
    XString* indexName;
    int indexColumn;
    bool found = false;
    if (!model || !value || role != XSqlItemDataRole_Edit) return false;
    relation = xsql_relational_relation_const(model, column);
    if (!relation || !XSqlRelation_isValid(relation) || !XVariant_isValid(value))
        return XSqlTableModel_setData(&model->m_parent, row, column, value, role);
    child = XSqlRelationalTableModel_relationModel(model, column);
    indexName = XSqlRelation_indexColumn(relation);
    indexColumn = indexName ? XSqlTableModel_fieldIndex(child, XString_toUtf8(indexName)) : -1;
    if (indexName) XString_delete_base(indexName);
    for (int childRow = 0; child && indexColumn >= 0 && childRow < XSqlTableModel_rowCount(child); ++childRow) {
        XVariant* indexValue = XSqlTableModel_data(child, childRow, indexColumn,
                                                    XSqlItemDataRole_Edit);
        found = indexValue && XVariant_type(indexValue) == XVariant_type((XVariant*)value)
            && XVariant_compare(indexValue, (XVariant*)value) == 0;
        if (!found && indexValue) {
            XString* left = xsql_relational_variant_text(indexValue);
            XString* right = xsql_relational_variant_text(value);
            found = left && right && XString_equals(left, right, XChar_CaseSensitive);
            if (left) XString_delete_base(left);
            if (right) XString_delete_base(right);
        }
        if (indexValue) XVariant_delete_base(indexValue);
        if (found) break;
    }
    if (!found) return false;
    return model && XSqlTableModel_setData(&model->m_parent, row, column, value, role);
}

bool XSqlRelationalTableModel_removeColumns(XSqlRelationalTableModel* model, int column, int count)
{
    if (!model || column < 0 || count <= 0 || column + count > XSqlQueryModel_columnCount(&model->m_parent.m_parent)) return false;
    if ((size_t)column < model->m_relationCount) {
        size_t end = model->m_relationCount < (size_t)(column + count) ? model->m_relationCount : (size_t)(column + count);
        for (size_t i = (size_t)column; i < end; ++i) {
            if (model->m_relations[i]) XSqlRelation_delete_base(model->m_relations[i]);
            if (model->m_relationModels[i]) XSqlTableModel_delete_base(model->m_relationModels[i]);
        }
        memmove(&model->m_relations[column], &model->m_relations[end], (model->m_relationCount - end) * sizeof(XSqlRelation*));
        memmove(&model->m_relationModels[column], &model->m_relationModels[end],
                (model->m_relationCount - end) * sizeof(XSqlTableModel*));
        model->m_relationCount -= end - (size_t)column;
        for (size_t i = model->m_relationCount; i < model->m_relationCapacity; ++i) {
            model->m_relations[i] = NULL;
            model->m_relationModels[i] = NULL;
        }
    }
    return XSqlTableModel_removeColumns(&model->m_parent, column, count);
}
static void VXSqlRelationalTableModel_clear(XSqlRelationalTableModel* model)
{
    if (!model) return;
    xsql_relational_clear_relations(model);
    XSqlRecord_clear(&model->m_baseRecord);
    XClass_Parent(XSqlTableModel, EXSqlQueryModel_Clear,
                  void(*)(XSqlTableModel*))(&model->m_parent);
}
void XSqlRelationalTableModel_clear(XSqlRelationalTableModel* model) { if (model && !XClassIsVtableNull(model)) XClassGetVirtualFunc(model, EXSqlQueryModel_Clear, void(*)(XSqlRelationalTableModel*))(model); }
static void VXSqlRelationalTableModel_setTable(XSqlRelationalTableModel* model, const char* tableName)
{
    XSqlRecord* record;
    if (!model) return;
    XSqlRecord_clear(&model->m_baseRecord);
    record = model->m_parent.m_database
        ? XSqlDatabase_record_utf8(model->m_parent.m_database, tableName) : NULL;
    if (record) {
        XSqlRecord_move_base(&model->m_baseRecord, record);
        XSqlRecord_delete_base(record);
    }
    XClass_Parent(XSqlTableModel, EXSqlTableModel_SetTable,
                  void(*)(XSqlTableModel*, const char*))(&model->m_parent, tableName);
}
void XSqlRelationalTableModel_setTable_utf8(XSqlRelationalTableModel* model, const char* tableName) { if (model && !XClassIsVtableNull(model)) XClassGetVirtualFunc(model, EXSqlTableModel_SetTable, void(*)(XSqlRelationalTableModel*, const char*))(model, tableName); }
void XSqlRelationalTableModel_setTable(XSqlRelationalTableModel* model, const XString* tableName) { XSqlRelationalTableModel_setTable_utf8(model, tableName ? XString_toUtf8(tableName) : NULL); }
static void VXSqlRelationalTableModel_setRelation(XSqlRelationalTableModel* model, int column, const XSqlRelation* relation)
{
    if (!model || column < 0 || !xsql_relational_reserve(model, (size_t)column + 1)) return;
    while (model->m_relationCount <= (size_t)column) {
        model->m_relations[model->m_relationCount] = NULL;
        model->m_relationModels[model->m_relationCount++] = NULL;
    }
    if (model->m_relations[column]) XSqlRelation_delete_base(model->m_relations[column]);
    if (model->m_relationModels[column]) XSqlTableModel_delete_base(model->m_relationModels[column]);
    model->m_relations[column] = relation ? XSqlRelation_create_copy(relation) : NULL;
    model->m_relationModels[column] = NULL;
}
void XSqlRelationalTableModel_setRelation(XSqlRelationalTableModel* model, int column, const XSqlRelation* relation) { if (model && !XClassIsVtableNull(model)) XClassGetVirtualFunc(model, EXSqlRelationalTableModel_SetRelation, void(*)(XSqlRelationalTableModel*, int, const XSqlRelation*))(model, column, relation); }
XSqlRelation* XSqlRelationalTableModel_relation(const XSqlRelationalTableModel* model, int column) { const XSqlRelation* relation = xsql_relational_relation_const(model, column); return relation ? XSqlRelation_create_copy(relation) : XSqlRelation_create(); }
static XSqlTableModel* VXSqlRelationalTableModel_relationModel(const XSqlRelationalTableModel* model, int column)
{
    const XSqlRelation* relation = xsql_relational_relation_const(model, column);
    XSqlRelationalTableModel* mutableModel = (XSqlRelationalTableModel*)model;
    if (!model || !relation || !XSqlRelation_isValid(relation)) return NULL;
    if (model->m_relationModels && model->m_relationModels[column])
        return model->m_relationModels[column];
    XSqlDatabase* database = XSqlTableModel_database(&model->m_parent);
    XSqlTableModel* child = XSqlTableModel_create(database);
    if (database) XSqlDatabase_delete_base(database);
    XString* table = XSqlRelation_tableName(relation);
    if (child) {
        XSqlTableModel_setTable_utf8(child, XString_toUtf8(table));
        if (!XSqlTableModel_select(child)) {
            XSqlTableModel_delete_base(child);
            child = NULL;
        }
    }
    if (table) XString_delete_base(table);
    if (child) mutableModel->m_relationModels[column] = child;
    return child;
}
XSqlTableModel* XSqlRelationalTableModel_relationModel(const XSqlRelationalTableModel* model, int column) { return model && !XClassIsVtableNull(model) ? XClassGetVirtualFunc(model, EXSqlRelationalTableModel_RelationModel, XSqlTableModel*(*)(const XSqlRelationalTableModel*, int))(model, column) : NULL; }
void XSqlRelationalTableModel_setJoinMode(XSqlRelationalTableModel* model, XSqlRelationJoinMode mode) { if (model) model->m_joinMode = mode; }
XSqlRelationJoinMode XSqlRelationalTableModel_joinMode(const XSqlRelationalTableModel* model) { return model ? model->m_joinMode : XSqlRelationJoinMode_InnerJoin; }
static XString* xsql_relational_escape(const XSqlDatabase* database, const XString* name, XSqlIdentifierType type)
{
    XSqlDriver* driver = XSqlDatabase_driver(database);
    return driver ? XSqlDriver_escapeIdentifier_base(driver, name, type) : XString_create_copy(name);
}

static XString* xsql_relational_alias(int column)
{
    return XString_create_fmt_utf8("relTblAl_%d", column);
}

static XString* VXSqlRelationalTableModel_selectStatement(const XSqlRelationalTableModel* model)
{
    if (!model || !model->m_parent.m_tableName) return XString_create();
    if (XSqlRecord_count(&model->m_baseRecord) == 0)
        return XClass_Parent(XSqlTableModel, EXSqlTableModel_SelectStatement,
                             XString*(*)(const XSqlTableModel*))(&model->m_parent);
    XString* result = XString_create_utf8("SELECT ");
    XString* table = xsql_relational_escape(model->m_parent.m_database, model->m_parent.m_tableName, XSqlIdentifierType_TableName);
    int columns = XSqlRecord_count(&model->m_baseRecord);
    for (int i = 0; result && i < columns; ++i) {
        if (i) XString_append_utf8(result, ", ");
        XString* field = XSqlRecord_fieldName(&model->m_baseRecord, i);
        const XSqlRelation* relation = xsql_relational_relation_const(model, i);
        if (relation && XSqlRelation_isValid(relation)) {
            XString* relationTable = XSqlRelation_tableName(relation);
            XString* display = XSqlRelation_displayColumn(relation);
            XString* alias = xsql_relational_alias(i);
            XString* escapedAlias = xsql_relational_escape(model->m_parent.m_database, alias, XSqlIdentifierType_TableName);
            XString* escapedDisplay = xsql_relational_escape(model->m_parent.m_database, display, XSqlIdentifierType_FieldName);
            XString* escapedField = xsql_relational_escape(model->m_parent.m_database, field, XSqlIdentifierType_FieldName);
            XString_append(result, escapedAlias); XString_append_utf8(result, "."); XString_append(result, escapedDisplay);
            XString_append_utf8(result, " AS "); XString_append(result, escapedField);
            XString_delete_base(relationTable); XString_delete_base(display); XString_delete_base(alias); XString_delete_base(escapedAlias); XString_delete_base(escapedDisplay); XString_delete_base(escapedField);
        } else {
            XString* escapedField = xsql_relational_escape(model->m_parent.m_database, field, XSqlIdentifierType_FieldName);
            XString_append(result, table); XString_append_utf8(result, "."); XString_append(result, escapedField); XString_delete_base(escapedField);
        }
        if (field) XString_delete_base(field);
    }
    if (columns == 0) XString_append_utf8(result, "*");
    XString_append_utf8(result, " FROM "); XString_append(result, table);
    for (int i = 0; i < columns; ++i) {
        const XSqlRelation* relation = xsql_relational_relation_const(model, i);
        if (!relation || !XSqlRelation_isValid(relation)) continue;
        XString* relationTable = XSqlRelation_tableName(relation);
        XString* index = XSqlRelation_indexColumn(relation);
        XString* field = XSqlRecord_fieldName(&model->m_baseRecord, i);
        XString* escapedTable = xsql_relational_escape(model->m_parent.m_database, relationTable, XSqlIdentifierType_TableName);
        XString* alias = xsql_relational_alias(i);
        XString* escapedAlias = xsql_relational_escape(model->m_parent.m_database, alias, XSqlIdentifierType_TableName);
        XString* escapedIndex = xsql_relational_escape(model->m_parent.m_database, index, XSqlIdentifierType_FieldName);
        XString* escapedField = xsql_relational_escape(model->m_parent.m_database, field, XSqlIdentifierType_FieldName);
        XString_append_utf8(result, model->m_joinMode == XSqlRelationJoinMode_LeftJoin ? " LEFT JOIN " : " INNER JOIN ");
        XString_append(result, escapedTable); XString_append_utf8(result, " AS "); XString_append(result, escapedAlias); XString_append_utf8(result, " ON "); XString_append(result, table); XString_append_utf8(result, "."); XString_append(result, escapedField); XString_append_utf8(result, " = "); XString_append(result, escapedAlias); XString_append_utf8(result, "."); XString_append(result, escapedIndex);
        XString_delete_base(relationTable); XString_delete_base(index); XString_delete_base(field); XString_delete_base(escapedTable); XString_delete_base(alias); XString_delete_base(escapedAlias); XString_delete_base(escapedIndex); XString_delete_base(escapedField);
    }
    if (model->m_parent.m_filter && XString_length_base(model->m_parent.m_filter) > 0) { XString_append_utf8(result, " WHERE "); XString_append(result, model->m_parent.m_filter); }
    XString* order = XSqlRelationalTableModel_orderByClause(model); if (order) { XString_append(result, order); XString_delete_base(order); }
    if (table) XString_delete_base(table);
    return result;
}
XString* XSqlRelationalTableModel_selectStatement(const XSqlRelationalTableModel* model) { return model && !XClassIsVtableNull(model) ? XClassGetVirtualFunc(model, EXSqlTableModel_SelectStatement, XString*(*)(const XSqlRelationalTableModel*))(model) : XString_create(); }
static XString* VXSqlRelationalTableModel_orderByClause(const XSqlRelationalTableModel* model)
{
    const XSqlRelation* relation;
    XString* alias;
    XString* display;
    XString* escapedAlias;
    XString* escapedDisplay;
    XString* result;
    if (!model || model->m_parent.m_sortColumn < 0) return XString_create();
    relation = xsql_relational_relation_const(model, model->m_parent.m_sortColumn);
    if (!relation || !XSqlRelation_isValid(relation))
        return XClass_Parent(XSqlTableModel, EXSqlTableModel_OrderByClause,
                             XString*(*)(const XSqlTableModel*))(&model->m_parent);
    alias = xsql_relational_alias(model->m_parent.m_sortColumn);
    display = XSqlRelation_displayColumn(relation);
    escapedAlias = xsql_relational_escape(model->m_parent.m_database, alias,
                                          XSqlIdentifierType_TableName);
    escapedDisplay = xsql_relational_escape(model->m_parent.m_database, display,
                                            XSqlIdentifierType_FieldName);
    result = XString_create_fmt_utf8(" ORDER BY %s.%s %s", XString_toUtf8(escapedAlias),
                                     XString_toUtf8(escapedDisplay),
                                     model->m_parent.m_sortOrder == XSqlSortOrder_Descending
                                         ? "DESC" : "ASC");
    if (alias) XString_delete_base(alias);
    if (display) XString_delete_base(display);
    if (escapedAlias) XString_delete_base(escapedAlias);
    if (escapedDisplay) XString_delete_base(escapedDisplay);
    return result;
}
XString* XSqlRelationalTableModel_orderByClause(const XSqlRelationalTableModel* model) { return model ? XSqlTableModel_orderByClause(&model->m_parent) : XString_create(); }
static bool VXSqlRelationalTableModel_select(XSqlRelationalTableModel* model)
{
    return model && XClass_Parent(XSqlTableModel, EXSqlTableModel_Select,
                                  bool(*)(XSqlTableModel*))(&model->m_parent);
}
bool XSqlRelationalTableModel_select(XSqlRelationalTableModel* model) { return model && !XClassIsVtableNull(model) && XClassGetVirtualFunc(model, EXSqlTableModel_Select, bool(*)(XSqlRelationalTableModel*))(model); }
static bool VXSqlRelationalTableModel_updateRowInTable(XSqlTableModel* base, int row,
                                                       const XSqlRecord* values)
{
    XSqlRelationalTableModel* model = (XSqlRelationalTableModel*)base;
    XSqlRecord* translated = values ? XSqlRecord_create_copy(values) : NULL;
    bool result;
    if (!model || !translated) { if (translated) XSqlRecord_delete_base(translated); return false; }
    for (int i = 0; i < XSqlRecord_count(&model->m_baseRecord) && i < XSqlRecord_count(translated); ++i) {
        const XSqlRelation* relation = xsql_relational_relation_const(model, i);
        if (relation && XSqlRelation_isValid(relation)) {
            XSqlField* field = XSqlRecord_field(&model->m_baseRecord, i);
            XVariant* value = XSqlRecord_value(translated, i);
            if (field && value) {
                XSqlField_setValue(field, value);
                XSqlField_setGenerated(field, XSqlRecord_isGenerated(translated, i));
                XSqlRecord_replace(translated, i, field);
            }
            if (field) XSqlField_delete_base(field);
            if (value) XVariant_delete_base(value);
        }
    }
    result = XClass_Parent(XSqlTableModel, EXSqlTableModel_UpdateRowInTable,
                            bool(*)(XSqlTableModel*, int, const XSqlRecord*))(base, row, translated);
    XSqlRecord_delete_base(translated);
    return result;
}
static bool VXSqlRelationalTableModel_insertRowIntoTable(XSqlTableModel* base,
                                                         const XSqlRecord* values)
{
    XSqlRelationalTableModel* model = (XSqlRelationalTableModel*)base;
    XSqlRecord* translated = values ? XSqlRecord_create_copy(values) : NULL;
    bool result;
    if (!model || !translated) { if (translated) XSqlRecord_delete_base(translated); return false; }
    for (int i = 0; i < XSqlRecord_count(&model->m_baseRecord) && i < XSqlRecord_count(translated); ++i) {
        const XSqlRelation* relation = xsql_relational_relation_const(model, i);
        if (relation && XSqlRelation_isValid(relation)) {
            XSqlField* field = XSqlRecord_field(&model->m_baseRecord, i);
            XVariant* value = XSqlRecord_value(translated, i);
            if (field && value) {
                XSqlField_setValue(field, value);
                XSqlField_setGenerated(field, XSqlRecord_isGenerated(translated, i));
                XSqlRecord_replace(translated, i, field);
            }
            if (field) XSqlField_delete_base(field);
            if (value) XVariant_delete_base(value);
        }
    }
    result = XClass_Parent(XSqlTableModel, EXSqlTableModel_InsertRowIntoTable,
                            bool(*)(XSqlTableModel*, const XSqlRecord*))(base, translated);
    XSqlRecord_delete_base(translated);
    return result;
}
static void VXSqlRelationalTableModel_revertRow(XSqlRelationalTableModel* model, int row) { if (model) XClass_Parent(XSqlTableModel, EXSqlTableModel_RevertRow, void(*)(XSqlTableModel*, int))(&model->m_parent, row); }
void XSqlRelationalTableModel_revertRow(XSqlRelationalTableModel* model, int row) { if (model && !XClassIsVtableNull(model)) XClassGetVirtualFunc(model, EXSqlTableModel_RevertRow, void(*)(XSqlRelationalTableModel*, int))(model, row); }
