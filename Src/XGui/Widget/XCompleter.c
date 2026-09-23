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
#include "XListWidget.h"
#include "XLineEdit.h"
#include "XMemory.h"
#include "XVarList.h"
#include "XVector.h"
#include "XStringUtils.h"
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
    if (XString_size_base((const XContainer*)prefix) == 0)
        return true; /* 空串前缀命中全部（Qt 语义）。 */
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

/**
 * @brief 取 UTF-8 字节区间 [begin, begin+len) 新建 XString。
 * @return 新建对象（调用方拥有）；参数无效或分配失败返回 NULL；len 为
 *         0 时返回空串对象。
 */
static XString* xcompleter_subStringUtf8(const char* utf8, size_t begin,
                                         size_t len)
{
    XString* s;
    if (!utf8) return NULL;
    s = XString_create();
    if (!s) return NULL;
    if (len == 0) return s; /* XString_create 已是空串。 */
    if (!XString_assign_with_length_utf8(s, utf8 + begin, len)) {
        XString_delete_base((XClass*)s);
        return NULL;
    }
    return s;
}

/**
 * @brief 排序模型下二分定位候选区间起点。
 * @details 仅在 modelSorting 非 UnsortedModel、filterMode 为 StartsWith、
 *          前缀非空且声明的大小写排序方式与 caseSensitivity 一致时调用
 *          （对齐 Qt 关于大小写不一致无法加速的约束）。按 XString_compare
 *          序找首个不小于前缀的行；大小写不敏感排序模型先对前缀与探测
 *          单元格做 XString_toLower 再比较。探测到 NULL 单元格（破坏
 *          有序假设）或分配失败时返回 false，调用方回退线性扫描。
 * @param self 目标补全对象；不可为 NULL（调用方已校验）。
 * @param rows 模型行数。
 * @param outStart 输出二分起点行号（成功时写 0..rows）。
 * @return 二分结果有效返回 true；应回退线性扫描返回 false。
 */
static bool xcompleter_sortedLowerBound(const XCompleter* self, int64_t rows,
                                        int64_t* outStart)
{
    int64_t lo = 0;
    int64_t hi = rows;
    bool insensitive;
    XString* prefixLower = NULL;
    if (rows <= 0 || !outStart) return false;
    insensitive = (self->m_modelSorting ==
                   XCompleterModelSorting_CaseInsensitivelySortedModel);
    if (insensitive) {
        if (self->m_caseSensitivity != XChar_CaseInsensitive) return false;
        prefixLower = XString_toLower(self->m_prefix);
        if (!prefixLower) return false;
    } else if (self->m_caseSensitivity != XChar_CaseSensitive) {
        return false; /* Qt：与模型排序大小写不一致时不加速。 */
    }
    while (lo < hi) {
        int64_t mid = lo + (hi - lo) / 2;
        const XString* cell = XAbstractItemModel_data(
            self->m_model, (int)mid, self->m_completionColumn);
        int32_t cmp;
        if (!cell) { /* 空洞破坏有序假设：放弃二分。 */
            if (prefixLower) XString_delete_base((XClass*)prefixLower);
            return false;
        }
        if (insensitive) {
            XString* cellLower = XString_toLower(cell);
            if (!cellLower) {
                XString_delete_base((XClass*)prefixLower);
                return false;
            }
            cmp = XString_compare(cellLower, prefixLower);
            XString_delete_base((XClass*)cellLower);
        } else {
            cmp = XString_compare(cell, self->m_prefix);
        }
        if (cmp < 0)
            lo = mid + 1;
        else
            hi = mid;
    }
    if (prefixLower) XString_delete_base((XClass*)prefixLower);
    *outStart = lo;
    return true;
}

/**
 * @brief 把完成列表当前行切换到 row 并同步 currentCompletion。
 * @details row 应在 [0, 候选数) 内（调用方已归一）；越界或数据缺失时仅
 *          置 currentRow 并清空 currentCompletion。
 */
static void xcompleter_applyCurrentRow(XCompleter* self, int row)
{
    int* item;
    const XString* cell;
    if (!self) return;
    self->m_currentRow = row;
    xcompleter_replaceString(&self->m_currentCompletion, NULL);
    if (row < 0 || !self->m_matches) return;
    item = (int*)XVector_at_base(self->m_matches, row);
    if (!item) return;
    if (!self->m_model) return;
    cell = XAbstractItemModel_data(self->m_model, *item,
                                   self->m_completionColumn);
    if (cell) xcompleter_replaceString(&self->m_currentCompletion, cell);
}

/** @brief 重建完成列表；命中首项变化时发射 highlighted 系列信号。 */
static void xcompleter_rebuild(XCompleter* self)
{
    const XString* firstText;
    XString* oldCompletion;
    int64_t rows;
    int64_t i;
    int64_t start = 0;
    int column;
    int firstRow = -1;
    bool hadOld;
    bool sorted;

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
    /* 排序模型 + StartsWith + 前缀非空：二分定位候选区间起点。 */
    sorted = false;
    if (self->m_prefix &&
        XString_size_base((const XContainer*)self->m_prefix) > 0 &&
        self->m_filterMode == XCompleterFilterMode_StartsWith &&
        self->m_modelSorting != XCompleterModelSorting_UnsortedModel) {
        sorted = xcompleter_sortedLowerBound(self, rows, &start);
    }
    for (i = start; i < rows; ++i) {
        const XString* cell =
            XAbstractItemModel_data(self->m_model, (int)i, column);
        if (cell && xcompleter_matchText(self, cell)) {
            int row = (int)i;
            XVector_push_back_1_base(self->m_matches, &row);
            if (firstRow < 0) {
                firstRow = row;
                firstText = cell;
            }
        } else if (sorted) {
            break; /* 有序模型命中区间连续：首个不命中即区间结束。 */
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
    self->m_modelSorting = XCompleterModelSorting_UnsortedModel;
    self->m_completionColumn = 0;
    self->m_completionRole = -1;
    self->m_caseSensitivity = XChar_CaseSensitive;
    self->m_maxVisibleItems = 7;
    self->m_wrapAround = true; /* Qt 默认 true。 */
    self->m_popup = NULL;
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

void XCompleter_setModelSorting(XCompleter* self,
                                XCompleterModelSorting sorting)
{
    if (!self || self->m_modelSorting == sorting) return;
    self->m_modelSorting = sorting;
    /* Qt 切换排序假设时重建补全引擎；本实现等价地重建完成列表。 */
    xcompleter_rebuild(self);
}

XCompleterModelSorting XCompleter_modelSorting(const XCompleter* self)
{
    return self ? self->m_modelSorting
                : XCompleterModelSorting_UnsortedModel;
}

void XCompleter_setWrapAround(XCompleter* self, bool wrap)
{
    if (!self) return;
    self->m_wrapAround = wrap;
}

bool XCompleter_wrapAround(const XCompleter* self)
{
    return self ? self->m_wrapAround : true;
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

void XCompleter_setPopup(XCompleter* self, XWidget* popup)
{
    if (!self) return;
    /* XGui 弹出列表为自绘非部件承载：仅保存借用指针，不做部件化接管。 */
    self->m_popup = popup;
}

/* ==================== 补全结果（对标 QCompleter） ==================== */

/* ---------- 默认弹层（对标 QCompleter popup；懒建于首次匹配） ---------- */

/** @brief 补全弹层点击槽：行号 -> 候选文本 -> 写回编辑框并隐藏。 */
static void xc_popupClickedSlot(XObject* receiver, XVarList* args)
{
    XCompleter* self = (XCompleter*)receiver;
    XString* text;
    XWidget* editor;
    if (!self || !args) return;
    XVarList_args_1(args, int, row);
    if (row < 0) return;
    XCompleter_setCurrentRow(self, row);
    text = XCompleter_currentCompletion(self);
    editor = self->m_widget;
    /* 写回编辑框（默认弹层服务于 XLineEdit 场景；其余编辑控件仅发
       activated 信号供调用方接线）。 */
    if (editor && XClassGetVtable(editor) == XLineEdit_class_init()) {
        XLineEdit_setText((XLineEdit*)editor,
                          text ? XString_toUtf8(text) : "");
        XLineEdit_cursorForward((XLineEdit*)editor, false, 1 << 20);
    }
    if (text) XString_delete_base((XClass*)text);
    XCompleter_hidePopup(self);
}

/** @brief 编辑框在其顶层窗口中的偏移（弹层定位用）。 */
static void xc_editorTopOffset(XCompleter* self, int* outX, int* outY)
{
    XWidget* editor;
    XWidget* top;
    XWidget* w;
    int ox = 0;
    int oy = 0;
    editor = self ? self->m_widget : NULL;
    if (!editor) {
        if (outX) *outX = 0;
        if (outY) *outY = 0;
        return;
    }
    top = XWidget_topLevelWidget(editor);
    w = editor;
    while (w && w != top) {
        ox += XWidget_x(w);
        oy += XWidget_y(w);
        w = XWidget_parentWidget(w);
    }
    if (outX) *outX = ox;
    if (outY) *outY = oy;
}

void XCompleter_hidePopup(XCompleter* self)
{
    if (!self) return;
    /* 外接弹层优先：setPopup 挂接的外部视图同样响应隐藏（Esc 链路
       对两种弹层形态一致）。 */
    if (self->m_popup && XWidget_isVisible(self->m_popup))
        XWidget_setVisible(self->m_popup, false);
    if (!self->m_defaultPopup) return;
    if (XWidget_isVisible((XWidget*)self->m_defaultPopup))
        XWidget_setVisible((XWidget*)self->m_defaultPopup, false);
}

static void xc_defaultPopupSync(XCompleter* self)
{
    XWidget* editor;
    int count;
    int rows;
    int ex = 0;
    int ey = 0;
    int i;
    if (!self) return;
    editor = self->m_widget;
    count = XCompleter_completionCount(self);
    if (!editor || self->m_popup) return; /* 外接弹层优先，本通路不介入。 */
    if (count <= 0) {
        XCompleter_hidePopup(self);
        return;
    }
    if (!self->m_defaultPopup) {
        XWidget* top = XWidget_topLevelWidget(editor);
        if (!top) return;
        self->m_defaultPopup =
            XListWidget_create((XWidget*)top, 0);
        if (!self->m_defaultPopup) return;
        /* 弹层挂顶层窗口（随顶层析构，本类不重复拥有）；点击经槽
           回写编辑框并隐藏。 */
        XObject_connect_1((XObject*)self->m_defaultPopup,
                          (size_t)XListWidget_itemClicked_signal(NULL, 0),
                          (XObject*)self, xc_popupClickedSlot,
                          XConnectionType_Direct);
    }
    XListWidget_clear(self->m_defaultPopup);
    for (i = 0; i < count; ++i) {
        int* modelRow = (int*)XVector_at_base(self->m_matches, i);
        const XString* cell;
        char buf[256];
        if (!modelRow || !self->m_model) continue;
        cell = XAbstractItemModel_data(self->m_model, *modelRow,
                                       self->m_completionColumn);
        if (!cell) continue;
        snprintf(buf, sizeof(buf), "%s",
                 XString_toUtf8(cell) ? XString_toUtf8(cell) : "");
        XListWidget_addItem_2(self->m_defaultPopup, buf);
    }
    rows = count > 6 ? 6 : count;
    xc_editorTopOffset(self, &ex, &ey);
    XWidget_setGeometry((XWidget*)self->m_defaultPopup,
                        ex, ey + XWidget_height(editor),
                        XWidget_width(editor) > 160 ? XWidget_width(editor) : 160,
                        rows * 24 + 4);
    XWidget_setVisible((XWidget*)self->m_defaultPopup, true);
}

void XCompleter_complete(XCompleter* self)
{
    if (!self) return;
    xcompleter_rebuild(self);
    /* 默认弹层同步：Popup/Unfiltered 模式且候选非空时于编辑框下方
       显示候选列表（对标 QCompleter popup）；无候选隐藏。 */
    if (self->m_completionMode == XCompleterCompletionMode_PopupCompletion ||
        self->m_completionMode ==
            XCompleterCompletionMode_UnfilteredPopupCompletion) {
        xc_defaultPopupSync(self);
    }
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

int XCompleter_completionCount(const XCompleter* self)
{
    if (!self || !self->m_matches) return 0;
    return (int)XVector_size_base((const XContainer*)self->m_matches);
}

bool XCompleter_setCurrentRow(XCompleter* self, int row)
{
    int count;
    if (!self) return false;
    count = XCompleter_completionCount(self);
    if (count <= 0) return false;
    if (row < 0 || row >= count) {
        if (!self->m_wrapAround) return false;
        /* 环绕归一：负值从末项往前，超出从首项继续。 */
        row %= count;
        if (row < 0) row += count;
    }
    xcompleter_applyCurrentRow(self, row);
    return true;
}

XAbstractItemModel* XCompleter_completionModel(const XCompleter* self)
{
    /* XGui 无补全代理模型：返回底层模型借用（见头文件 @note）。 */
    return self ? self->m_model : NULL;
}

XString* XCompleter_pathFromIndex(const XCompleter* self, int row)
{
    const XString* cell;
    if (!self || !self->m_model || row < 0) return NULL;
    if (row >= XAbstractItemModel_rowCount(self->m_model)) return NULL;
    cell = XAbstractItemModel_data(self->m_model, row,
                                   self->m_completionColumn);
    if (!cell) return NULL;
    return XString_create_copy(cell);
}

void XCompleter_splitPath(const XCompleter* self, const XString* path,
                          XString** dirOut, XString** fileOut)
{
    const char* utf8;
    size_t len;
    size_t i;
    size_t slash;
    size_t dirLen;
    size_t fileStart;
    (void)self; /* 保留 Qt 成员函数形态；XGui 无 QFileSystemModel 判定。 */
    if (dirOut) *dirOut = NULL;
    if (fileOut) *fileOut = NULL;
    utf8 = path ? XString_toUtf8(path) : NULL;
    if (!utf8) utf8 = "";
    len = XStrlen(utf8);
    slash = (size_t)-1;
    for (i = len; i > 0; --i) {
        if (utf8[i - 1] == '/') {
            slash = i - 1;
            break;
        }
    }
    dirLen = (slash == (size_t)-1) ? 0 : slash;
    fileStart = (slash == (size_t)-1) ? 0 : slash + 1;
    if (dirOut)
        *dirOut = xcompleter_subStringUtf8(utf8, 0, dirLen);
    if (fileOut)
        *fileOut = xcompleter_subStringUtf8(utf8, fileStart, len - fileStart);
}

void XCompleter_splitPath_2(const XCompleter* self, const char* path,
                            XString** dirOut, XString** fileOut)
{
    XString* tmp;
    if (!path) {
        XCompleter_splitPath(self, NULL, dirOut, fileOut);
        return;
    }
    tmp = XString_create_utf8(path);
    XCompleter_splitPath(self, tmp, dirOut, fileOut);
    if (tmp) XString_delete_base((XClass*)tmp);
}

XWidget* XCompleter_popup(const XCompleter* self)
{
    if (!self) return NULL;
    if (self->m_popup) return self->m_popup;
    return (XWidget*)self->m_defaultPopup;
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
