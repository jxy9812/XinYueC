/**
 * @file       XCompleter.c
 * @brief      补全对象实现（对标 Qt 6.8 QCompleter 核心公共 API）。
 * @details    与同名头文件的公共 API 一一对应；前缀匹配从模型
 *             completionColumn 列逐行读取文本并按 filterMode 过滤，
 *             命中列表存储于 m_matches，信号按 Qt 发射点发射
 *             highlighted 系列（activated 系列保留供调用方触发）。
 * @author     XinYueC 团队
 */

#include "XCompleter.h"
#include "XMemory.h"
#include "XVarList.h"
#include "XVector.h"
#include "XGuiConfig.h"

#if XWIDGET_ON && XTABLEWIDGET_ON

/* ==================== 内部工具 ==================== */

/** @brief 发射带 XString* 参数的信号（无连接时释放参数，防泄漏）。 */
static void xcompleter_emitTextSignal(XCompleter* self, size_t signal,
                                      const XString* text)
{
    XVarList* args = XVarList_Create(XVar(XString*, text));
    if (!args) return;
    if (self && ((XObject*)self)->m_signalSlot) {
        XObject_emitSignal((XObject*)self, signal, args, NULL, NULL,
                           XEVENT_PRIORITY_NORMAL);
    } else {
        XVarList_delete(args);
    }
}

/** @brief 发射带 int 参数的信号。 */
static void xcompleter_emitIntSignal(XCompleter* self, size_t signal,
                                     int row)
{
    XVarList* args = XVarList_Create(XVar(int, row));
    if (!args) return;
    if (self && ((XObject*)self)->m_signalSlot) {
        XObject_emitSignal((XObject*)self, signal, args, NULL, NULL,
                           XEVENT_PRIORITY_NORMAL);
    } else {
        XVarList_delete(args);
    }
}

/** @brief 发射带 (int,int) 双参数的信号。 */
static void xcompleter_emitInt2Signal(XCompleter* self, size_t signal,
                                      int row, int col)
{
    XVarList* args =
        XVarList_Create(XVar(int, row), XVar(int, col));
    if (!args) return;
    if (self && ((XObject*)self)->m_signalSlot) {
        XObject_emitSignal((XObject*)self, signal, args, NULL, NULL,
                           XEVENT_PRIORITY_NORMAL);
    } else {
        XVarList_delete(args);
    }
}

/** @brief 释放并替换拥有的 XString 槽位（src 为 NULL 时清空）。 */
static void xcompleter_replaceString(XString** slot, const XString* src)
{
    XString* copy;
    if (!slot) return;
    if (*slot) {
        XString_delete_base((XClass*)*slot);
        *slot = NULL;
    }
    if (!src) return;
    copy = XString_create_copy(src);
    if (copy)
        *slot = copy;
}

/** @brief 判断文本是否按过滤模式命中前缀。 */
static bool xcompleter_matchText(const XCompleter* self,
                                 const XString* text)
{
    const XString* prefix;
    XChar_CaseSensitivity cs;
    if (!text) return false;
    if (!self->m_prefix) return true; /* 空前缀命中全部非空单元格（Qt 语义）。 */
    prefix = self->m_prefix;
    if (XString_size(prefix) == 0) return true; /* 空串前缀命中全部（Qt 语义）。 */
    cs = self->m_caseSensitivity;
    switch (self->m_filterMode) {
    case XCompleterFilterMode_Contains:
        return XString_contains(text, prefix, cs);
    case XCompleterFilterMode_EndsWith:
        return XString_endsWith(text, prefix, cs);
    case XCompleterFilterMode_StartsWith:
    default:
        return XString_startsWith(text, prefix, cs);
    }
}

/** @brief 重建完成列表；命中首项变化时发射 highlighted 系列信号。 */
static void xcompleter_rebuild(XCompleter* self)
{
    const XString* firstText;
    XString* oldCompletion;
    int64_t rows;
    int64_t i;
    int column;
    int firstRow = -1;
    bool hadOld;

    if (!self || !self->m_matches) return;
    XVector_clear_base((XContainer*)self->m_matches);
    if (self->m_currentCompletion) {
        oldCompletion = XString_create_copy(self->m_currentCompletion);
        hadOld = true;
    } else {
        oldCompletion = NULL;
        hadOld = false;
    }
    self->m_currentRow = -1;
    xcompleter_replaceString(&self->m_currentCompletion, NULL);

    if (!self->m_model) return;
    rows = XAbstractItemModel_rowCount(self->m_model);
    column = self->m_completionColumn;
    firstText = NULL;
    for (i = 0; i < rows; ++i) {
        const XString* cell =
            XAbstractItemModel_data(self->m_model, (int)i, column);
        if (!cell) continue;
        if (xcompleter_matchText(self, cell)) {
            int row = (int)i;
            XVector_push_back_1_base(self->m_matches, &row);
            if (firstRow < 0) {
                firstRow = row;
                firstText = cell;
            }
        }
    }
    if (firstText) {
        self->m_currentRow = 0;
        xcompleter_replaceString(&self->m_currentCompletion, firstText);
        /* Qt 发射点：高亮变化时发射 highlighted(text)/highlighted(index)。 */
        if (!hadOld ||
            !XString_equals(self->m_currentCompletion, oldCompletion,
                            XChar_CaseSensitive)) {
            xcompleter_emitTextSignal(self,
                (size_t)XCompleter_highlighted_signal,
                self->m_currentCompletion);
            xcompleter_emitTextSignal(self,
                (size_t)XCompleter_textHighlighted_signal,
                self->m_currentCompletion);
            xcompleter_emitIntSignal(self,
                (size_t)XCompleter_highlightedRow_signal, firstRow);
        }
    }
    if (oldCompletion)
        XString_delete_base((XClass*)oldCompletion);
}

/* ==================== 生命周期与虚表 ==================== */

static void VX_completer_deinit(XCompleter* self)
{
    if (!self) return;
    if (self->m_prefix) {
        XString_delete_base((XClass*)self->m_prefix);
        self->m_prefix = NULL;
    }
    if (self->m_currentCompletion) {
        XString_delete_base((XClass*)self->m_currentCompletion);
        self->m_currentCompletion = NULL;
    }
    if (self->m_matches) {
        XVector_delete_base((XClass*)self->m_matches);
        self->m_matches = NULL;
    }
    XClass_Deinit_Parent(XObject, (XObject*)self);
}

XVtable* XCompleter_class_init(void)
{
    XVTABLE_INIT_DEFAULT(XCompleter)
    XVTABLE_INHERIT_XCLASS(XObject);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Deinit, VX_completer_deinit);
    return XVTABLE_DEFAULT;
}

void XCompleter_init(XCompleter* self, XObject* parent)
{
    if (!self) return;
    XMemset(self, 0, sizeof(*self));
    XObject_init(&self->m_base);
    if (parent)
        XObject_setParent((XObject*)self, parent);
    XClassSetVtable(self, XCompleter);
    Set_Class_Memory(self, XCLASS_DEFAULT_MEMORY_TYPE);
    Set_Class_IsHeap(self, false);
    self->m_completionMode = XCompleterCompletionMode_PopupCompletion;
    self->m_filterMode = XCompleterFilterMode_StartsWith;
    self->m_completionColumn = 0;
    self->m_completionRole = -1;
    self->m_caseSensitivity = XChar_CaseSensitive;
    self->m_maxVisibleItems = 7;
    self->m_currentRow = -1;
    self->m_matches = XVector_Create(int);
}

XCompleter* XCompleter_create_ex(XMemoryType memory, XObject* parent)
{
    XCompleter* self =
        (XCompleter*)XMemory_malloc(sizeof(*self), memory);
    if (!self) return NULL;
    XCompleter_init(self, parent);
    Set_Class_Memory(self, memory);
    Set_Class_IsHeap(self, true);
    return self;
}

XCompleter* XCompleter_create_2_ex(XMemoryType memory,
                                   XAbstractItemModel* model,
                                   XObject* parent)
{
    XCompleter* self = XCompleter_create_ex(memory, parent);
    if (!self) return NULL;
    self->m_model = model;
    return self;
}

/* ==================== 模型与属性（对标 QCompleter） ==================== */

void XCompleter_setModel(XCompleter* self, XAbstractItemModel* model)
{
    if (!self) return;
    self->m_model = model;
    xcompleter_rebuild(self);
}

XAbstractItemModel* XCompleter_model(const XCompleter* self)
{
    return self ? self->m_model : NULL;
}

void XCompleter_setCompletionMode(XCompleter* self,
                                  XCompleterCompletionMode mode)
{
    if (!self) return;
    self->m_completionMode = mode;
}

XCompleterCompletionMode XCompleter_completionMode(const XCompleter* self)
{
    return self ? self->m_completionMode
                : XCompleterCompletionMode_PopupCompletion;
}

void XCompleter_setCompletionPrefix(XCompleter* self,
                                    const XString* prefix)
{
    if (!self) return;
    xcompleter_replaceString(&self->m_prefix, prefix);
    xcompleter_rebuild(self);
}

void XCompleter_setCompletionPrefix_2(XCompleter* self, const char* utf8)
{
    XString* prefix;
    if (!self) return;
    prefix = utf8 ? XString_create_utf8(utf8) : NULL;
    XCompleter_setCompletionPrefix(self, prefix);
    if (prefix)
        XString_delete_base((XClass*)prefix);
}

XString* XCompleter_completionPrefix(const XCompleter* self)
{
    if (!self || !self->m_prefix) return NULL;
    return XString_create_copy(self->m_prefix);
}

void XCompleter_setCompletionColumn(XCompleter* self, int column)
{
    if (!self) return;
    self->m_completionColumn = column;
    xcompleter_rebuild(self);
}

int XCompleter_completionColumn(const XCompleter* self)
{
    return self ? self->m_completionColumn : 0;
}

void XCompleter_setCompletionRole(XCompleter* self, int role)
{
    if (!self) return;
    self->m_completionRole = role;
}

int XCompleter_completionRole(const XCompleter* self)
{
    return self ? self->m_completionRole : -1;
}

void XCompleter_setCaseSensitivity(XCompleter* self,
                                   XChar_CaseSensitivity cs)
{
    if (!self) return;
    self->m_caseSensitivity = cs;
    xcompleter_rebuild(self);
}

XChar_CaseSensitivity XCompleter_caseSensitivity(const XCompleter* self)
{
    return self ? self->m_caseSensitivity : XChar_CaseSensitive;
}

void XCompleter_setFilterMode(XCompleter* self, XCompleterFilterMode mode)
{
    if (!self) return;
    self->m_filterMode = mode;
    xcompleter_rebuild(self);
}

XCompleterFilterMode XCompleter_filterMode(const XCompleter* self)
{
    return self ? self->m_filterMode : XCompleterFilterMode_StartsWith;
}

void XCompleter_setMaxVisibleItems(XCompleter* self, int maxItems)
{
    if (!self) return;
    self->m_maxVisibleItems = maxItems < 0 ? 0 : maxItems;
}

int XCompleter_maxVisibleItems(const XCompleter* self)
{
    return self ? self->m_maxVisibleItems : 7;
}

void XCompleter_setWidget(XCompleter* self, XWidget* widget)
{
    if (!self) return;
    self->m_widget = widget;
}

XWidget* XCompleter_widget(const XCompleter* self)
{
    return self ? self->m_widget : NULL;
}

/* ==================== 补全结果（对标 QCompleter） ==================== */

void XCompleter_complete(XCompleter* self)
{
    if (!self) return;
    xcompleter_rebuild(self);
}

XString* XCompleter_currentCompletion(const XCompleter* self)
{
    if (!self || !self->m_currentCompletion) return NULL;
    return XString_create_copy(self->m_currentCompletion);
}

int XCompleter_currentRow(const XCompleter* self)
{
    return self ? self->m_currentRow : -1;
}

int XCompleter_currentIndex(const XCompleter* self)
{
    int* item;
    if (!self || self->m_currentRow < 0 || !self->m_matches) return -1;
    item = (int*)XVector_at_base(self->m_matches, self->m_currentRow);
    return item ? *item : -1;
}

XWidget* XCompleter_popup(const XCompleter* self)
{
    (void)self;
    return NULL;
}

/* ==================== 信号 ==================== */

void* XCompleter_activated_signal(XCompleter* self, const XString* text)
{
    (void)self;
    (void)text;
    return (void*)(size_t)XCompleter_activated_signal;
}

void* XCompleter_textActivated_signal(XCompleter* self, const XString* text)
{
    (void)self;
    (void)text;
    return (void*)(size_t)XCompleter_textActivated_signal;
}

void* XCompleter_highlighted_signal(XCompleter* self, const XString* text)
{
    (void)self;
    (void)text;
    return (void*)(size_t)XCompleter_highlighted_signal;
}

void* XCompleter_textHighlighted_signal(XCompleter* self,
                                        const XString* text)
{
    (void)self;
    (void)text;
    return (void*)(size_t)XCompleter_textHighlighted_signal;
}

void* XCompleter_highlightedRow_signal(XCompleter* self, int row)
{
    (void)self;
    (void)row;
    return (void*)(size_t)XCompleter_highlightedRow_signal;
}

void* XCompleter_activatedIndex_signal(XCompleter* self, int row, int col)
{
    (void)self;
    (void)row;
    (void)col;
    return (void*)(size_t)XCompleter_activatedIndex_signal;
}

#endif /* XWIDGET_ON && XTABLEWIDGET_ON */
