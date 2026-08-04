/**
 * @file       XSqlTableModel.c
 * @brief      SQL 表模型实现。
 */
#include "XSqlTableModel.h"

#include <string.h>

static void VXSqlTableModel_deinit(XSqlTableModel* model);
static void VXSqlTableModel_clear(XSqlTableModel* model);
static void VXSqlTableModel_setTable(XSqlTableModel* model, const char* tableName);
static void VXSqlTableModel_setEditStrategy(XSqlTableModel* model, XSqlTableEditStrategy strategy);
static void VXSqlTableModel_setSort(XSqlTableModel* model, int column, XSqlSortOrder order);
static void VXSqlTableModel_setFilter(XSqlTableModel* model, const char* filter);
static void VXSqlTableModel_revertRow(XSqlTableModel* model, int row);
static bool VXSqlTableModel_select(XSqlTableModel* model);
static bool VXSqlTableModel_selectRow(XSqlTableModel* model, int row);
static bool VXSqlTableModel_updateRowInTable(XSqlTableModel* model, int row, const XSqlRecord* values);
static bool VXSqlTableModel_insertRowIntoTable(XSqlTableModel* model, const XSqlRecord* values);
static bool VXSqlTableModel_deleteRowFromTable(XSqlTableModel* model, int row);
static XString* VXSqlTableModel_orderByClause(const XSqlTableModel* model);
static XString* VXSqlTableModel_selectStatement(const XSqlTableModel* model);
static bool xsql_table_delete_record(XSqlTableModel* model, int row, const XSqlRecord* source);
static XSqlRecord* xsql_table_key_values(const XSqlTableModel* model,
                                         const XSqlRecord* source);

XVtable* XSqlTableModel_class_init(void)
{
    XVTABLE_INIT_DEFAULT(XSqlTableModel)
	XCLASS_SET_CLASS_NAME_DEFAULT("XSqlTableModel");
    XVTABLE_INHERIT_XCLASS(XSqlQueryModel);
    XVTABLE_ADD_FUNC_LIST_DEFAULT(((void*[]){
        VXSqlTableModel_setTable, VXSqlTableModel_setEditStrategy,
        VXSqlTableModel_setSort, VXSqlTableModel_setFilter,
        VXSqlTableModel_revertRow, VXSqlTableModel_select,
        VXSqlTableModel_selectRow, VXSqlTableModel_updateRowInTable,
        VXSqlTableModel_insertRowIntoTable, VXSqlTableModel_deleteRowFromTable,
        VXSqlTableModel_orderByClause, VXSqlTableModel_selectStatement
    }));
    XVTABLE_OVERLOAD_DEFAULT(EXSqlQueryModel_Clear, VXSqlTableModel_clear);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Deinit, VXSqlTableModel_deinit);
    return XVTABLE_DEFAULT;
}

void XSqlTableModel_init(XSqlTableModel* model, const XSqlDatabase* database)
{
    if (!model) return;
    memset(((unsigned char*)model) + sizeof(XSqlQueryModel), 0, sizeof(*model) - sizeof(XSqlQueryModel));
    XSqlQueryModel_init(&model->m_parent);
    XClassSetVtable(model, XSqlTableModel);
    model->m_strategy = XSqlTableEditStrategy_OnRowChange;
    model->m_sortColumn = -1;
    XSqlIndex_init(&model->m_primaryKey);
    model->m_database = database ? XSqlDatabase_create_copy(database) : NULL;
}

XSqlTableModel* XSqlTableModel_create(const XSqlDatabase* database)
{
    XSqlTableModel* model = (XSqlTableModel*)XMalloc_System(sizeof(XSqlTableModel));
    if (!model) return NULL;
    memset(model, 0, sizeof(*model));
    XSqlTableModel_init(model, database);
    Set_Class_MemoryFree(model, XFree_System);
    return model;
}

static void xsql_table_clear_dirty(XSqlTableModel* model)
{
    if (!model) return;
    for (size_t i = 0; i < model->m_dirtyCapacity; ++i) {
        if (model->m_originalRows && model->m_originalRows[i])
            XSqlRecord_delete_base(model->m_originalRows[i]);
    }
    for (size_t i = 0; i < model->m_removedCount; ++i) {
        if (model->m_removedRows[i]) XSqlRecord_delete_base(model->m_removedRows[i]);
    }
    if (model->m_dirty) XFree_System(model->m_dirty);
    if (model->m_inserted) XFree_System(model->m_inserted);
    if (model->m_deleted) XFree_System(model->m_deleted);
    if (model->m_originalRows) XFree_System(model->m_originalRows);
    if (model->m_removedRows) XFree_System(model->m_removedRows);
    model->m_dirty = NULL;
    model->m_inserted = NULL;
    model->m_deleted = NULL;
    model->m_originalRows = NULL;
    model->m_dirtyCapacity = 0;
    model->m_removedRows = NULL;
    model->m_removedCount = 0;
    model->m_removedCapacity = 0;
}

static bool xsql_table_reserve_dirty(XSqlTableModel* model, size_t wanted)
{
    bool* dirty;
    bool* inserted;
    bool* deleted;
    XSqlRecord** originals;
    size_t oldCapacity;
    if (wanted <= model->m_dirtyCapacity) return true;
    size_t capacity = model->m_dirtyCapacity ? model->m_dirtyCapacity * 2 : 8;
    while (capacity < wanted) capacity *= 2;
    dirty = (bool*)XMalloc_System(capacity * sizeof(bool));
    inserted = (bool*)XMalloc_System(capacity * sizeof(bool));
    deleted = (bool*)XMalloc_System(capacity * sizeof(bool));
    originals = (XSqlRecord**)XMalloc_System(capacity * sizeof(XSqlRecord*));
    if (!dirty || !inserted || !deleted || !originals) {
        if (dirty) XFree_System(dirty);
        if (inserted) XFree_System(inserted);
        if (deleted) XFree_System(deleted);
        if (originals) XFree_System(originals);
        return false;
    }
    oldCapacity = model->m_dirtyCapacity;
    memset(dirty, 0, capacity * sizeof(bool));
    memset(inserted, 0, capacity * sizeof(bool));
    memset(deleted, 0, capacity * sizeof(bool));
    memset(originals, 0, capacity * sizeof(XSqlRecord*));
    if (oldCapacity) {
        memcpy(dirty, model->m_dirty, oldCapacity * sizeof(bool));
        memcpy(inserted, model->m_inserted, oldCapacity * sizeof(bool));
        memcpy(deleted, model->m_deleted, oldCapacity * sizeof(bool));
        memcpy(originals, model->m_originalRows, oldCapacity * sizeof(XSqlRecord*));
    }
    if (model->m_dirty) XFree_System(model->m_dirty);
    if (model->m_inserted) XFree_System(model->m_inserted);
    if (model->m_deleted) XFree_System(model->m_deleted);
    if (model->m_originalRows) XFree_System(model->m_originalRows);
    model->m_dirty = dirty;
    model->m_inserted = inserted;
    model->m_deleted = deleted;
    model->m_originalRows = originals;
    model->m_dirtyCapacity = capacity;
    return true;
}

static bool xsql_table_reserve_removed(XSqlTableModel* model, size_t wanted)
{
    XSqlRecord** rows;
    size_t capacity;
    if (wanted <= model->m_removedCapacity) return true;
    capacity = model->m_removedCapacity ? model->m_removedCapacity * 2 : 8;
    while (capacity < wanted) capacity *= 2;
    rows = (XSqlRecord**)XRealloc_System(model->m_removedRows,
                                         capacity * sizeof(XSqlRecord*));
    if (!rows) return false;
    for (size_t i = model->m_removedCapacity; i < capacity; ++i) rows[i] = NULL;
    model->m_removedRows = rows;
    model->m_removedCapacity = capacity;
    return true;
}

static bool xsql_table_append_removed(XSqlTableModel* model, const XSqlRecord* record)
{
    XSqlRecord* copy;
    if (!model || !record || !xsql_table_reserve_removed(model, model->m_removedCount + 1))
        return false;
    copy = XSqlRecord_create_copy(record);
    if (!copy) return false;
    model->m_removedRows[model->m_removedCount++] = copy;
    return true;
}

static void xsql_table_clear_original(XSqlTableModel* model, int row)
{
    if (!model || row < 0 || (size_t)row >= model->m_dirtyCapacity) return;
    if (model->m_originalRows[row]) XSqlRecord_delete_base(model->m_originalRows[row]);
    model->m_originalRows[row] = NULL;
}

static bool xsql_table_snapshot_row(XSqlTableModel* model, int row)
{
    if (!model || row < 0 || (size_t)row >= model->m_parent.m_rowCount
        || !xsql_table_reserve_dirty(model, model->m_parent.m_rowCount)) return false;
    if (!model->m_inserted[row] && !model->m_originalRows[row]) {
        model->m_originalRows[row] = XSqlRecord_create_copy(model->m_parent.m_rows[row]);
        if (!model->m_originalRows[row]) return false;
    }
    return true;
}

static bool xsql_table_reserve_rows(XSqlTableModel* model, size_t wanted)
{
    XSqlRecord** rows;
    size_t capacity;
    if (wanted <= model->m_parent.m_rowCapacity) return true;
    capacity = model->m_parent.m_rowCapacity ? model->m_parent.m_rowCapacity * 2 : 8;
    while (capacity < wanted) capacity *= 2;
    rows = (XSqlRecord**)XRealloc_System(model->m_parent.m_rows,
                                         capacity * sizeof(XSqlRecord*));
    if (!rows) return false;
    model->m_parent.m_rows = rows;
    model->m_parent.m_rowCapacity = capacity;
    return true;
}

static void xsql_table_remove_row_state(XSqlTableModel* model, int row, size_t oldCount)
{
    size_t tail;
    if (!model || row < 0 || (size_t)row >= oldCount) return;
    tail = oldCount - (size_t)row - 1;
    xsql_table_clear_original(model, row);
    if (tail) {
        memmove(&model->m_dirty[row], &model->m_dirty[row + 1], tail * sizeof(bool));
        memmove(&model->m_inserted[row], &model->m_inserted[row + 1], tail * sizeof(bool));
        memmove(&model->m_deleted[row], &model->m_deleted[row + 1], tail * sizeof(bool));
        memmove(&model->m_originalRows[row], &model->m_originalRows[row + 1],
                tail * sizeof(XSqlRecord*));
    }
    model->m_dirty[oldCount - 1] = false;
    model->m_inserted[oldCount - 1] = false;
    model->m_deleted[oldCount - 1] = false;
    model->m_originalRows[oldCount - 1] = NULL;
}

static void VXSqlTableModel_deinit(XSqlTableModel* model)
{
    if (!model) return;
    if (model->m_database) XSqlDatabase_delete_base(model->m_database);
    if (model->m_tableName) XString_delete_base(model->m_tableName);
    if (model->m_filter) XString_delete_base(model->m_filter);
    xsql_table_clear_dirty(model);
    XSqlIndex_deinit_base(&model->m_primaryKey);
    model->m_database = NULL; model->m_tableName = NULL; model->m_filter = NULL;
    XClass_Deinit_Parent(XSqlQueryModel, model);
}

static void VXSqlTableModel_setTable(XSqlTableModel* model, const char* tableName)
{
    XSqlRecord* record;
    XSqlIndex* key;
    if (!model) return;

    xsql_table_clear_dirty(model);
    XClass_Parent(XSqlQueryModel, EXSqlQueryModel_Clear,
                  void(*)(XSqlQueryModel*))(&model->m_parent);
    XSqlRecord_clear(&model->m_primaryKey.m_parent);
    if (model->m_tableName) XString_delete_base(model->m_tableName);
    model->m_tableName = tableName ? XString_create_utf8(tableName) : NULL;
    if (!model->m_database || !model->m_tableName) return;

    /* Qt loads the record before select(), so filters and sort columns are usable. */
    record = XSqlDatabase_record(model->m_database, model->m_tableName);
    if (record) {
        XSqlRecord_move_base(&model->m_parent.m_record, record);
        XSqlRecord_delete_base(record);
    }
    key = XSqlDatabase_primaryIndex(model->m_database, model->m_tableName);
    if (key) {
        XClass_Parent(XSqlIndex, EXClass_Move,
                      void(*)(XSqlIndex*, XSqlIndex*))(&model->m_primaryKey, key);
        XSqlIndex_delete_base(key);
    }
}
void XSqlTableModel_setTable_utf8(XSqlTableModel* model, const char* tableName) { if (model && !XClassIsVtableNull(model)) XClassGetVirtualFunc(model, EXSqlTableModel_SetTable, void(*)(XSqlTableModel*, const char*))(model, tableName); }
void XSqlTableModel_setTable(XSqlTableModel* model, const XString* tableName) { XSqlTableModel_setTable_utf8(model, tableName ? XString_toUtf8(tableName) : NULL); }
XString* XSqlTableModel_tableName(const XSqlTableModel* model) { return model && model->m_tableName ? XString_create_copy(model->m_tableName) : XString_create(); }
XSqlItemFlags XSqlTableModel_flags(const XSqlTableModel* model, int row, int column) { if (!model || row < 0 || column < 0 || row >= XSqlQueryModel_rowCount(&model->m_parent) || column >= XSqlQueryModel_columnCount(&model->m_parent)) return XSqlItemFlag_NoItemFlags; XSqlItemFlags flags = XSqlItemFlag_ItemIsSelectable | XSqlItemFlag_ItemIsEnabled; bool editable = !model->m_deleted || !model->m_deleted[row]; if (editable) editable = !XSqlField_isReadOnly(XSqlRecord_field_const(model->m_parent.m_rows[row], column)); if (editable && model->m_strategy != XSqlTableEditStrategy_OnManualSubmit && (!model->m_dirty || !model->m_dirty[row])) { for (size_t i = 0; i < model->m_parent.m_rowCount; ++i) if (model->m_dirty && model->m_dirty[i]) { editable = false; break; } } if (editable) flags |= XSqlItemFlag_ItemIsEditable; return flags; }
XSqlRecord* XSqlTableModel_record_current(const XSqlTableModel* model) { return model ? XSqlQueryModel_record_current(&model->m_parent) : XSqlRecord_create(); }
XSqlRecord* XSqlTableModel_record(const XSqlTableModel* model, int row) { return model ? XSqlQueryModel_record(&model->m_parent, row) : XSqlRecord_create(); }
XVariant* XSqlTableModel_data(const XSqlTableModel* model, int row, int column, XSqlItemDataRole role)
{
    if (!model || (role != XSqlItemDataRole_Display && role != XSqlItemDataRole_Edit)
        || row < 0 || column < 0 || (size_t)row >= model->m_parent.m_rowCount
        || column >= XSqlQueryModel_columnCount(&model->m_parent))
        return XVariant_create_null();
    if (model->m_dirty && model->m_dirty[row])
        return XSqlRecord_value(model->m_parent.m_rows[row], column);
    return XSqlQueryModel_data(&model->m_parent, row, column, role);
}
bool XSqlTableModel_setData(XSqlTableModel* model, int row, int column, const XVariant* value, XSqlItemDataRole role)
{
    const XSqlField* field;
    const XVariant* oldValue;
    bool submitted;
    if (!model || role != XSqlItemDataRole_Edit || row < 0 || column < 0 || !value
        || (size_t)row >= model->m_parent.m_rowCount
        || column >= XSqlQueryModel_columnCount(&model->m_parent)) return false;
    if (model->m_deleted && model->m_deleted[row]) return false;
    field = XSqlRecord_field_const(model->m_parent.m_rows[row], column);
    if (XSqlField_isReadOnly(field)) return false;
    if (model->m_strategy != XSqlTableEditStrategy_OnManualSubmit
        && (!model->m_dirty[row])) {
        for (size_t i = 0; i < model->m_parent.m_rowCount; ++i)
            if (i != (size_t)row && model->m_dirty && model->m_dirty[i]) return false;
    }
    oldValue = XSqlField_value_const(field);
    if (oldValue && XVariant_type((XVariant*)oldValue) == XVariant_type((XVariant*)value)
        && XVariant_compare((XVariant*)oldValue, (XVariant*)value) == 0
        && (!model->m_inserted || !model->m_inserted[row])) return true;
    if (!xsql_table_snapshot_row(model, row)) return false;
    XSqlRecord_setValue(model->m_parent.m_rows[row], column, value);
    model->m_dirty[row] = true;
    XSqlQueryModel_dataChanged_signal(&model->m_parent, row, column, row, column);
    if (model->m_strategy != XSqlTableEditStrategy_OnFieldChange || model->m_inserted[row])
        return true;
    submitted = XSqlTableModel_updateRowInTable(model, row, model->m_parent.m_rows[row]);
    if (submitted) {
        model->m_dirty[row] = false;
        xsql_table_clear_original(model, row);
    }
    return submitted;
}
bool XSqlTableModel_clearItemData(XSqlTableModel* model, int row, int column) { XVariant* value = XVariant_create_null(); bool result = XSqlTableModel_setData(model, row, column, value, XSqlItemDataRole_Edit); XVariant_delete_base(value); return result; }
XVariant* XSqlTableModel_headerData(const XSqlTableModel* model, int section, XSqlOrientation orientation, XSqlItemDataRole role)
{
    if (!model) return XVariant_create_null();
    if (orientation == XSqlOrientation_Vertical && role == XSqlItemDataRole_Display
        && section >= 0 && (size_t)section < model->m_parent.m_rowCount
        && model->m_inserted) {
        if (model->m_inserted[section]) return XVariant_create_utf8_str("*");
        if (model->m_deleted && model->m_deleted[section]) return XVariant_create_utf8_str("!");
    }
    return XSqlQueryModel_headerData(&model->m_parent, section, orientation, role);
}
bool XSqlTableModel_isDirty(const XSqlTableModel* model) { if (!model) return false; if (model->m_removedCount) return true; for (size_t i = 0; i < model->m_parent.m_rowCount; ++i) if (model->m_dirty && model->m_dirty[i]) return true; return false; }
bool XSqlTableModel_isDirty_row(const XSqlTableModel* model, int row) { return model && row >= 0 && (size_t)row < model->m_parent.m_rowCount && model->m_dirty && model->m_dirty[row]; }
static void VXSqlTableModel_clear(XSqlTableModel* model) { if (model) { xsql_table_clear_dirty(model); XClass_Parent(XSqlQueryModel, EXSqlQueryModel_Clear, void(*)(XSqlQueryModel*))(&model->m_parent); } }
void XSqlTableModel_clear(XSqlTableModel* model) { if (model && !XClassIsVtableNull(model)) XClassGetVirtualFunc(model, EXSqlQueryModel_Clear, void(*)(XSqlTableModel*))(model); }
static void VXSqlTableModel_setEditStrategy(XSqlTableModel* model,
                                            XSqlTableEditStrategy strategy)
{
    if (!model) return;
    XSqlTableModel_revertAll(model);
    model->m_strategy = strategy;
}
void XSqlTableModel_setEditStrategy(XSqlTableModel* model, XSqlTableEditStrategy strategy) { if (model && !XClassIsVtableNull(model)) XClassGetVirtualFunc(model, EXSqlTableModel_SetEditStrategy, void(*)(XSqlTableModel*, XSqlTableEditStrategy))(model, strategy); }
XSqlTableEditStrategy XSqlTableModel_editStrategy(const XSqlTableModel* model) { return model ? model->m_strategy : XSqlTableEditStrategy_OnRowChange; }
XSqlIndex* XSqlTableModel_primaryKey(const XSqlTableModel* model) { return model ? XSqlIndex_create_copy(&model->m_primaryKey) : XSqlIndex_create(); }
XSqlDatabase* XSqlTableModel_database(const XSqlTableModel* model) { return model && model->m_database ? XSqlDatabase_create_copy(model->m_database) : XSqlDatabase_create(); }
int XSqlTableModel_fieldIndex(const XSqlTableModel* model, const char* fieldName) { return model ? XSqlRecord_indexOf_utf8(&model->m_parent.m_record, fieldName) : -1; }
int XSqlTableModel_fieldIndex_2(const XSqlTableModel* model, const XString* fieldName) { return model ? XSqlRecord_indexOf(&model->m_parent.m_record, fieldName) : -1; }
void XSqlTableModel_sort(XSqlTableModel* model, int column, XSqlSortOrder order) { XSqlTableModel_setSort(model, column, order); XSqlTableModel_select(model); }
static void VXSqlTableModel_setSort(XSqlTableModel* model, int column, XSqlSortOrder order) { if (model) { model->m_sortColumn = column; model->m_sortOrder = order; } }
void XSqlTableModel_setSort(XSqlTableModel* model, int column, XSqlSortOrder order) { if (model && !XClassIsVtableNull(model)) XClassGetVirtualFunc(model, EXSqlTableModel_SetSort, void(*)(XSqlTableModel*, int, XSqlSortOrder))(model, column, order); }
XString* XSqlTableModel_filter(const XSqlTableModel* model) { return model && model->m_filter ? XString_create_copy(model->m_filter) : XString_create(); }
static void VXSqlTableModel_setFilter(XSqlTableModel* model, const char* filter) { if (!model) return; if (model->m_filter) XString_delete_base(model->m_filter); model->m_filter = filter ? XString_create_utf8(filter) : NULL; if (XSqlQuery_isActive(&model->m_parent.m_query)) XSqlTableModel_select(model); }
void XSqlTableModel_setFilter_utf8(XSqlTableModel* model, const char* filter) { if (model && !XClassIsVtableNull(model)) XClassGetVirtualFunc(model, EXSqlTableModel_SetFilter, void(*)(XSqlTableModel*, const char*))(model, filter); }
void XSqlTableModel_setFilter(XSqlTableModel* model, const XString* filter) { XSqlTableModel_setFilter_utf8(model, filter ? XString_toUtf8(filter) : NULL); }
int XSqlTableModel_rowCount(const XSqlTableModel* model) { return model ? XSqlQueryModel_rowCount(&model->m_parent) : 0; }
bool XSqlTableModel_removeColumns(XSqlTableModel* model, int column, int count)
{
    bool active;
    if (!model) return false;
    active = XSqlQuery_isActive(&model->m_parent.m_query);
    if (!XSqlQueryModel_removeColumns(&model->m_parent, column, count)) return false;
    return !active || XSqlTableModel_select(model);
}
bool XSqlTableModel_removeRows(XSqlTableModel* model, int row, int count)
{
    if (!model || row < 0 || count <= 0 || (size_t)(row + count) > model->m_parent.m_rowCount
        || !xsql_table_reserve_dirty(model, model->m_parent.m_rowCount)) return false;
    if (model->m_strategy != XSqlTableEditStrategy_OnManualSubmit
        && (count > 1 || ((!model->m_dirty || !model->m_dirty[row])
                          && XSqlTableModel_isDirty(model)))) return false;
    for (int i = count - 1; i >= 0; --i) {
        int target = row + i;
        size_t oldCount = model->m_parent.m_rowCount;
        if (model->m_inserted[target]) {
            XSqlRecord_delete_base(model->m_parent.m_rows[target]);
            if (oldCount > (size_t)target + 1)
                memmove(&model->m_parent.m_rows[target], &model->m_parent.m_rows[target + 1],
                        (oldCount - (size_t)target - 1) * sizeof(XSqlRecord*));
            xsql_table_remove_row_state(model, target, oldCount);
            --model->m_parent.m_rowCount;
            continue;
        }
        if (!xsql_table_snapshot_row(model, target)) return false;
        model->m_deleted[target] = true;
        model->m_dirty[target] = true;
    }
    return model->m_strategy == XSqlTableEditStrategy_OnManualSubmit
        || XSqlTableModel_submitAll(model);
}

bool XSqlTableModel_insertRows(XSqlTableModel* model, int row, int count)
{
    XSqlRecord** inserted;
    size_t oldCount;
    if (!model || row < 0 || count <= 0 || (size_t)row > model->m_parent.m_rowCount) return false;
    if (model->m_strategy != XSqlTableEditStrategy_OnManualSubmit
        && (count != 1 || XSqlTableModel_isDirty(model))) return false;
    oldCount = model->m_parent.m_rowCount;
    if (!xsql_table_reserve_rows(model, oldCount + (size_t)count)
        || !xsql_table_reserve_dirty(model, oldCount + (size_t)count)) return false;
    inserted = (XSqlRecord**)XMalloc_System((size_t)count * sizeof(XSqlRecord*));
    if (!inserted) return false;
    memset(inserted, 0, (size_t)count * sizeof(XSqlRecord*));
    for (int i = 0; i < count; ++i) {
        inserted[i] = XSqlRecord_create_copy(&model->m_parent.m_record);
        if (!inserted[i]) {
            for (int j = 0; j < i; ++j) XSqlRecord_delete_base(inserted[j]);
            XFree_System(inserted);
            return false;
        }
        XSqlRecord_clearValues(inserted[i]);
    }
    if (oldCount > (size_t)row) {
        memmove(&model->m_parent.m_rows[row + count], &model->m_parent.m_rows[row],
                (oldCount - (size_t)row) * sizeof(XSqlRecord*));
        memmove(&model->m_dirty[row + count], &model->m_dirty[row],
                (oldCount - (size_t)row) * sizeof(bool));
        memmove(&model->m_inserted[row + count], &model->m_inserted[row],
                (oldCount - (size_t)row) * sizeof(bool));
        memmove(&model->m_deleted[row + count], &model->m_deleted[row],
                (oldCount - (size_t)row) * sizeof(bool));
        memmove(&model->m_originalRows[row + count], &model->m_originalRows[row],
                (oldCount - (size_t)row) * sizeof(XSqlRecord*));
    }
    for (int i = 0; i < count; ++i) {
        model->m_parent.m_rows[row + i] = inserted[i];
        model->m_dirty[row + i] = true;
        model->m_inserted[row + i] = true;
        model->m_deleted[row + i] = false;
        model->m_originalRows[row + i] = NULL;
        XSqlTableModel_primeInsert_signal(model, row + i);
    }
    model->m_parent.m_rowCount = oldCount + (size_t)count;
    XFree_System(inserted);
    return true;
}
bool XSqlTableModel_insertRecord(XSqlTableModel* model, int row, const XSqlRecord* record) { int target = row < 0 ? XSqlTableModel_rowCount(model) : row; if (!XSqlTableModel_insertRows(model, target, 1)) return false; if (!XSqlTableModel_setRecord(model, target, record)) { XSqlTableModel_revertRow(model, target); return false; } return true; }
bool XSqlTableModel_setRecord(XSqlTableModel* model, int row, const XSqlRecord* record)
{
    const XSqlField* sourceField;
    const XSqlField* targetField;
    XString* fieldName;
    int target;
    bool submitted;
    if (!model || !record || row < 0 || (size_t)row >= model->m_parent.m_rowCount
        || (model->m_deleted && model->m_deleted[row])) return false;
    if (model->m_strategy != XSqlTableEditStrategy_OnManualSubmit
        && (!model->m_dirty[row])) {
        for (size_t i = 0; i < model->m_parent.m_rowCount; ++i)
            if (i != (size_t)row && model->m_dirty && model->m_dirty[i]) return false;
    }
    for (int i = 0; i < XSqlRecord_count(record); ++i) {
        fieldName = XSqlRecord_fieldName(record, i);
        target = fieldName ? XSqlRecord_indexOf_utf8(&model->m_parent.m_record,
                                                     XString_toUtf8(fieldName)) : -1;
        targetField = XSqlRecord_field_const(&model->m_parent.m_record, target);
        if (fieldName) XString_delete_base(fieldName);
        if (target < 0 || XSqlField_isReadOnly(targetField)) return false;
    }
    if (!xsql_table_snapshot_row(model, row)) return false;
    for (int i = 0; i < XSqlRecord_count(record); ++i) {
        fieldName = XSqlRecord_fieldName(record, i);
        target = fieldName ? XSqlRecord_indexOf_utf8(&model->m_parent.m_record,
                                                     XString_toUtf8(fieldName)) : -1;
        sourceField = XSqlRecord_field_const(record, i);
        if (target >= 0 && sourceField) {
            XSqlRecord_setValue(model->m_parent.m_rows[row], target,
                                XSqlField_value_const(sourceField));
            XSqlRecord_setGenerated(model->m_parent.m_rows[row], target,
                                    XSqlField_isGenerated(sourceField));
            XSqlQueryModel_dataChanged_signal(&model->m_parent, row, target, row, target);
        }
        if (fieldName) XString_delete_base(fieldName);
    }
    model->m_dirty[row] = true;
    if (model->m_strategy != XSqlTableEditStrategy_OnFieldChange || model->m_inserted[row])
        return true;
    submitted = XSqlTableModel_updateRowInTable(model, row, model->m_parent.m_rows[row]);
    if (submitted) {
        model->m_dirty[row] = false;
        xsql_table_clear_original(model, row);
    }
    return submitted;
}
static void VXSqlTableModel_revertRow(XSqlTableModel* model, int row)
{
    size_t oldCount;
    if (!model || !model->m_dirty || row < 0 || (size_t)row >= model->m_parent.m_rowCount) return;
    if (model->m_inserted[row]) {
        oldCount = model->m_parent.m_rowCount;
        XSqlRecord_delete_base(model->m_parent.m_rows[row]);
        if (oldCount > (size_t)row + 1)
            memmove(&model->m_parent.m_rows[row], &model->m_parent.m_rows[row + 1],
                    (oldCount - (size_t)row - 1) * sizeof(XSqlRecord*));
        xsql_table_remove_row_state(model, row, oldCount);
        --model->m_parent.m_rowCount;
        return;
    }
    if (model->m_deleted && model->m_deleted[row]) {
        model->m_deleted[row] = false;
        model->m_dirty[row] = false;
        xsql_table_clear_original(model, row);
        return;
    }
    if (model->m_dirty[row] && model->m_originalRows[row]) {
        XSqlRecord_copy_base(model->m_parent.m_rows[row], model->m_originalRows[row]);
        xsql_table_clear_original(model, row);
    }
    model->m_dirty[row] = false;
}
void XSqlTableModel_revertRow(XSqlTableModel* model, int row) { if (model && !XClassIsVtableNull(model)) XClassGetVirtualFunc(model, EXSqlTableModel_RevertRow, void(*)(XSqlTableModel*, int))(model, row); }
static XString* VXSqlTableModel_orderByClause(const XSqlTableModel* model)
{
    XSqlDriver* driver;
    XString* name;
    XString* table;
    XString* escapedName;
    XString* escapedTable;
    XString* result;
    if (!model || model->m_sortColumn < 0 || !model->m_database || !model->m_tableName)
        return XString_create();
    name = XSqlRecord_fieldName(&model->m_parent.m_record, model->m_sortColumn);
    if (!name || XString_length_base(name) == 0) {
        if (name) XString_delete_base(name);
        return XString_create();
    }
    driver = XSqlDatabase_driver(model->m_database);
    table = XString_create_copy(model->m_tableName);
    escapedName = driver ? XSqlDriver_escapeIdentifier_base(driver, name,
                                                            XSqlIdentifierType_FieldName)
                         : XString_create_copy(name);
    escapedTable = driver ? XSqlDriver_escapeIdentifier_base(driver, table,
                                                             XSqlIdentifierType_TableName)
                          : XString_create_copy(table);
    result = XString_create_utf8(" ORDER BY ");
    if (result && escapedTable && escapedName) {
        XString_append(result, escapedTable);
        XString_append_utf8(result, ".");
        XString_append(result, escapedName);
        XString_append_utf8(result, model->m_sortOrder == XSqlSortOrder_Descending
            ? " DESC" : " ASC");
    }
    if (name) XString_delete_base(name);
    if (table) XString_delete_base(table);
    if (escapedName) XString_delete_base(escapedName);
    if (escapedTable) XString_delete_base(escapedTable);
    return result;
}
XString* XSqlTableModel_orderByClause(const XSqlTableModel* model) { return model && !XClassIsVtableNull(model) ? XClassGetVirtualFunc(model, EXSqlTableModel_OrderByClause, XString*(*)(const XSqlTableModel*))(model) : XString_create(); }
static XString* VXSqlTableModel_selectStatement(const XSqlTableModel* model)
{
    XSqlDriver* driver;
    XString* result;
    XString* order;
    if (!model || !model->m_database || !model->m_tableName) return XString_create();
    driver = XSqlDatabase_driver(model->m_database);
    result = driver ? XSqlDriver_sqlStatement_base(driver, XSqlStatementType_SelectStatement,
                                                    model->m_tableName,
                                                    &model->m_parent.m_record, false) : NULL;
    if (!result) return XString_create();
    if (model->m_filter && XString_length_base(model->m_filter) > 0) {
        XString_append_utf8(result, " WHERE ");
        XString_append(result, model->m_filter);
    }
    order = XSqlTableModel_orderByClause(model);
    if (order) {
        XString_append(result, order);
        XString_delete_base(order);
    }
    return result;
}
XString* XSqlTableModel_selectStatement(const XSqlTableModel* model) { return model && !XClassIsVtableNull(model) ? XClassGetVirtualFunc(model, EXSqlTableModel_SelectStatement, XString*(*)(const XSqlTableModel*))(model) : XString_create(); }
static bool VXSqlTableModel_select(XSqlTableModel* model)
{
    if (!model || !model->m_database || !model->m_tableName) return false;
    XString* sql = XSqlTableModel_selectStatement(model);
    XSqlQuery* query = XSqlDatabase_exec(model->m_database, sql);
    if (sql) XString_delete_base(sql);
    if (!query || !XSqlQuery_isActive(query)) {
        XSqlError* error = query ? XSqlQuery_lastError(query)
            : XSqlDatabase_lastError(model->m_database);
        if (error) {
            XSqlQueryModel_setLastError(&model->m_parent, error);
            XSqlError_delete_base(error);
        }
        if (query) XSqlQuery_delete_base(query);
        return false;
    }
    XSqlQueryModel_setQuery_move(&model->m_parent, query);
    XSqlQuery_delete_base(query);
    xsql_table_clear_dirty(model);
    if (!xsql_table_reserve_dirty(model, model->m_parent.m_rowCount)) return false;
    XSqlIndex* key = XSqlDatabase_primaryIndex(model->m_database, model->m_tableName);
    if (key) { XClass_Parent(XSqlIndex, EXClass_Move, void(*)(XSqlIndex*, XSqlIndex*))(&model->m_primaryKey, key); XSqlIndex_delete_base(key); }
    return true;
}
bool XSqlTableModel_select(XSqlTableModel* model) { return model && !XClassIsVtableNull(model) && XClassGetVirtualFunc(model, EXSqlTableModel_Select, bool(*)(XSqlTableModel*))(model); }
static bool VXSqlTableModel_selectRow(XSqlTableModel* model, int row)
{
    XSqlDriver* driver;
    XSqlRecord* keys;
    XString* sql;
    XString* where;
    XSqlQuery* query;
    bool found;
    bool result;

    if (!model || !model->m_database || !model->m_tableName || row < 0
        || (size_t)row >= model->m_parent.m_rowCount) return false;
    driver = XSqlDatabase_driver(model->m_database);
    keys = xsql_table_key_values(model, model->m_parent.m_rows[row]);
    sql = driver ? XSqlDriver_sqlStatement_base(driver, XSqlStatementType_SelectStatement,
                                                 model->m_tableName,
                                                 &model->m_parent.m_record, false) : NULL;
    where = keys && driver ? XSqlDriver_sqlStatement_base(driver,
                                                           XSqlStatementType_WhereStatement,
                                                           model->m_tableName, keys, false) : NULL;
    if (sql && where) XString_append(sql, where);
    query = sql ? XSqlDatabase_exec(model->m_database, sql) : NULL;
    result = query && XSqlQuery_isActive(query);
    found = result && XSqlQuery_first(query);
    if (found) {
        XSqlRecord* refreshed = XSqlQuery_record(query);
        if (refreshed) {
            for (int column = 0; column < XSqlRecord_count(refreshed); ++column) {
                XVariant* value = XSqlQuery_value(query, column);
                if (value) {
                    XSqlRecord_setValue(refreshed, column, value);
                    XVariant_delete_base(value);
                }
            }
            XSqlRecord_copy_base(model->m_parent.m_rows[row], refreshed);
            XSqlRecord_delete_base(refreshed);
        } else {
            result = false;
        }
    } else if (result) {
        XSqlRecord_clearValues(model->m_parent.m_rows[row]);
    }
    if (!result) {
        XSqlError* error = query ? XSqlQuery_lastError(query)
            : XSqlDatabase_lastError(model->m_database);
        if (error) {
            XSqlQueryModel_setLastError(&model->m_parent, error);
            XSqlError_delete_base(error);
        }
    } else if (model->m_dirty && (size_t)row < model->m_dirtyCapacity) {
        xsql_table_clear_original(model, row);
        model->m_dirty[row] = false;
        model->m_inserted[row] = false;
        XSqlQueryModel_dataChanged_signal(&model->m_parent, row, 0, row,
                                          XSqlQueryModel_columnCount(&model->m_parent) - 1);
    }
    if (query) XSqlQuery_delete_base(query);
    if (where) XString_delete_base(where);
    if (sql) XString_delete_base(sql);
    if (keys) XSqlRecord_delete_base(keys);
    return result;
}
bool XSqlTableModel_selectRow(XSqlTableModel* model, int row) { return model && !XClassIsVtableNull(model) && XClassGetVirtualFunc(model, EXSqlTableModel_SelectRow, bool(*)(XSqlTableModel*, int))(model, row); }

static XSqlRecord* xsql_table_key_values(const XSqlTableModel* model, const XSqlRecord* source)
{
    XSqlRecord* keys;
    if (!model || !source) return NULL;
    keys = XSqlRecord_keyValues(source, &model->m_primaryKey.m_parent);
    if (keys && XSqlRecord_count(keys) == 0) {
        XSqlRecord_delete_base(keys);
        keys = XSqlRecord_create_copy(source);
    }
    return keys;
}

static bool xsql_table_execute(XSqlTableModel* model, XSqlStatementType type,
                               const XSqlRecord* values, const XSqlRecord* whereValues)
{
    XSqlDriver* driver;
    XString* sql;
    XString* where = NULL;
    XSqlQuery* query;
    bool prepared;
    bool result;
    if (!model || !model->m_database || !model->m_tableName || !values) return false;
    driver = XSqlDatabase_driver(model->m_database);
    prepared = driver && XSqlDriver_hasFeature_base(driver, XSqlDriverFeature_PreparedQueries);
    sql = driver ? XSqlDriver_sqlStatement_base(driver, type, model->m_tableName, values,
                                                prepared) : NULL;
    if (whereValues && sql) {
        where = XSqlDriver_sqlStatement_base(driver, XSqlStatementType_WhereStatement,
                                             model->m_tableName, whereValues, prepared);
        if (where) XString_append(sql, where);
    }
    query = prepared ? XSqlQuery_create_database(model->m_database)
                     : (sql ? XSqlDatabase_exec(model->m_database, sql) : NULL);
    if (prepared && query && sql && XSqlQuery_prepare(query, sql)) {
        const XSqlRecord* records[2] = { NULL, whereValues };
        records[0] = type == XSqlStatementType_DeleteStatement ? NULL : values;
        for (size_t recordIndex = 0; recordIndex < 2; ++recordIndex) {
            const XSqlRecord* record = records[recordIndex];
            if (!record) continue;
            for (int column = 0; column < XSqlRecord_count(record); ++column) {
                XVariant* value;
                if (!XSqlRecord_isGenerated(record, column)) continue;
                if (recordIndex == 1 && XSqlRecord_isNull(record, column)) continue;
                value = XSqlRecord_value(record, column);
                if (value) {
                    XSqlQuery_addBindValue(query, value, XSqlParamType_In);
                    XVariant_delete_base(value);
                }
            }
        }
        result = XSqlQuery_exec(query);
    } else {
        result = !prepared && query && XSqlQuery_isActive(query);
    }
    if (!result) {
        XSqlError* error = query ? XSqlQuery_lastError(query)
            : XSqlDatabase_lastError(model->m_database);
        if (error) {
            XSqlQueryModel_setLastError(&model->m_parent, error);
            XSqlError_delete_base(error);
        }
    }
    if (query) XSqlQuery_delete_base(query);
    if (where) XString_delete_base(where);
    if (sql) XString_delete_base(sql);
    return result;
}

static bool xsql_table_delete_record(XSqlTableModel* model, int row, const XSqlRecord* source)
{
    XSqlRecord* keys;
    bool result;
    if (!model || !source) return false;
    keys = xsql_table_key_values(model, source);
    if (!keys) return false;
    XSqlTableModel_beforeDelete_signal(model, row);
    result = xsql_table_execute(model, XSqlStatementType_DeleteStatement, source, keys);
    XSqlRecord_delete_base(keys);
    return result;
}

static bool VXSqlTableModel_updateRowInTable(XSqlTableModel* model, int row, const XSqlRecord* values)
{
    const XSqlRecord* source;
    XSqlRecord* keys;
    bool result;
    if (!model || !values || row < 0 || (size_t)row >= model->m_parent.m_rowCount) return false;
    source = model->m_originalRows && model->m_originalRows[row]
        ? model->m_originalRows[row] : model->m_parent.m_rows[row];
    keys = xsql_table_key_values(model, source);
    if (!keys) return false;
    XSqlTableModel_beforeUpdate_signal(model, row, (XSqlRecord*)values);
    result = xsql_table_execute(model, XSqlStatementType_UpdateStatement, values, keys);
    XSqlRecord_delete_base(keys);
    return result;
}
bool XSqlTableModel_updateRowInTable(XSqlTableModel* model, int row, const XSqlRecord* values) { return model && !XClassIsVtableNull(model) && XClassGetVirtualFunc(model, EXSqlTableModel_UpdateRowInTable, bool(*)(XSqlTableModel*, int, const XSqlRecord*))(model, row, values); }
static bool VXSqlTableModel_insertRowIntoTable(XSqlTableModel* model, const XSqlRecord* values) { if (!model || !values) return false; XSqlTableModel_beforeInsert_signal(model, (XSqlRecord*)values); return xsql_table_execute(model, XSqlStatementType_InsertStatement, values, NULL); }
bool XSqlTableModel_insertRowIntoTable(XSqlTableModel* model, const XSqlRecord* values) { return model && !XClassIsVtableNull(model) && XClassGetVirtualFunc(model, EXSqlTableModel_InsertRowIntoTable, bool(*)(XSqlTableModel*, const XSqlRecord*))(model, values); }
static bool VXSqlTableModel_deleteRowFromTable(XSqlTableModel* model, int row) { const XSqlRecord* source; if (!model || row < 0 || (size_t)row >= model->m_parent.m_rowCount) return false; source = model->m_originalRows && model->m_originalRows[row] ? model->m_originalRows[row] : model->m_parent.m_rows[row]; return xsql_table_delete_record(model, row, source); }
bool XSqlTableModel_deleteRowFromTable(XSqlTableModel* model, int row) { return model && !XClassIsVtableNull(model) && XClassGetVirtualFunc(model, EXSqlTableModel_DeleteRowFromTable, bool(*)(XSqlTableModel*, int))(model, row); }
bool XSqlTableModel_submit(XSqlTableModel* model)
{
    if (!model) return false;
    return model->m_strategy == XSqlTableEditStrategy_OnManualSubmit
        || XSqlTableModel_submitAll(model);
}

void XSqlTableModel_revert(XSqlTableModel* model)
{
    if (model && model->m_strategy != XSqlTableEditStrategy_OnManualSubmit)
        XSqlTableModel_revertAll(model);
}
bool XSqlTableModel_submitAll(XSqlTableModel* model)
{
    if (!model || !model->m_database || !model->m_tableName) return false;
    while (model->m_removedCount) {
        XSqlRecord* row = model->m_removedRows[0];
        if (!xsql_table_delete_record(model, -1, row)) return false;
        XSqlRecord_delete_base(row);
        --model->m_removedCount;
        if (model->m_removedCount)
            memmove(&model->m_removedRows[0], &model->m_removedRows[1],
                    model->m_removedCount * sizeof(XSqlRecord*));
        model->m_removedRows[model->m_removedCount] = NULL;
    }
    for (size_t i = 0; i < model->m_parent.m_rowCount; ++i) {
        bool result;
        if (!model->m_dirty || !model->m_dirty[i]) continue;
        if (model->m_deleted && model->m_deleted[i]) {
            const XSqlRecord* source = model->m_originalRows && model->m_originalRows[i]
                ? model->m_originalRows[i] : model->m_parent.m_rows[i];
            result = xsql_table_delete_record(model, (int)i, source);
        } else {
            result = model->m_inserted[i]
            ? XSqlTableModel_insertRowIntoTable(model, model->m_parent.m_rows[i])
            : XSqlTableModel_updateRowInTable(model, (int)i, model->m_parent.m_rows[i]);
        }
        if (!result) return false;
        model->m_dirty[i] = false;
        model->m_inserted[i] = false;
        model->m_deleted[i] = false;
        xsql_table_clear_original(model, (int)i);
    }
    return XSqlTableModel_select(model);
}
void XSqlTableModel_revertAll(XSqlTableModel* model) { if (!model) return; if (model->m_database && model->m_tableName) XSqlTableModel_select(model); else if (model->m_dirty) memset(model->m_dirty, 0, model->m_dirtyCapacity * sizeof(bool)); }
void XSqlTableModel_setPrimaryKey(XSqlTableModel* model, const XSqlIndex* key) { if (model && key) XClass_Parent(XSqlIndex, EXClass_Copy, void(*)(XSqlIndex*, const XSqlIndex*))(&model->m_primaryKey, key); }
XSqlRecord* XSqlTableModel_primaryValues(const XSqlTableModel* model, int row) { return model && row >= 0 && (size_t)row < model->m_parent.m_rowCount ? XSqlRecord_keyValues(model->m_parent.m_rows[row], &model->m_primaryKey.m_parent) : XSqlRecord_create(); }

void* XSqlTableModel_primeInsert_signal(XSqlTableModel* model, int row)
{
    XEmitSignal((XObject*)model, XSqlTableModel_primeInsert_signal,
                XVarList_create(2, sizeof(int), &row), NULL, NULL,
                XEVENT_PRIORITY_NORMAL);
}

void* XSqlTableModel_beforeInsert_signal(XSqlTableModel* model, XSqlRecord* record)
{
    XEmitSignal((XObject*)model, XSqlTableModel_beforeInsert_signal,
                XVarList_create(2, sizeof(XSqlRecord*), &record), NULL, NULL,
                XEVENT_PRIORITY_NORMAL);
}

void* XSqlTableModel_beforeUpdate_signal(XSqlTableModel* model, int row, XSqlRecord* record)
{
    XEmitSignal((XObject*)model, XSqlTableModel_beforeUpdate_signal,
                XVarList_create(4, sizeof(int), &row, sizeof(XSqlRecord*), &record),
                NULL, NULL, XEVENT_PRIORITY_NORMAL);
}

void* XSqlTableModel_beforeDelete_signal(XSqlTableModel* model, int row)
{
    XEmitSignal((XObject*)model, XSqlTableModel_beforeDelete_signal,
                XVarList_create(2, sizeof(int), &row), NULL, NULL,
                XEVENT_PRIORITY_NORMAL);
}
