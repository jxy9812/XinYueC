/**
 * @file       XItemDelegate.c
 * @brief      XItemDelegate 条目委托实现（编辑器四虚槽默认路径 + 内嵌行
 *             文本编辑器 + commitData/closeEditor 信号）。
 * @details    默认实现对标 Qt 6.8 QStyledItemDelegate 的缺省编辑器工厂：
 *             行文本编辑器（XLineEdit 本地派生 XItemLineEditor，参照
 *             XComboBox 内嵌 XComboEdit 方案）装载 EditRole 文本并全选、
 *             回写 EditRole 通路、按条目矩形整格摆放；编辑器键盘语义
 *             （Return/Enter 与 Tab/Backtab 提交关闭、Esc 放弃关闭、
 *             失焦提交关闭）对标 QStyledItemDelegate::eventFilter。
 * @author     XinYueC 团队
 ******************************************************************************/
#include "XItemDelegate.h"

#include "XLineEdit.h"
#include "XMemory.h"
#include "XClass.h"
#include "XVarList.h"
#include "XEvent.h"
#include "XEventType.h"

#if XWIDGET_ON && XTABLEWIDGET_ON

/* ==================== 信号发射助手 ==================== */

static void xide_emitEditor(XItemDelegate* self, size_t signal,
                            XWidget* editor)
{
    XVarList* args = XVarList_Create(XVar(XWidget*, editor));
    if (!args) return;
    if (self && ((XObject*)self)->m_signalSlot) {
        XObject_emitSignal((XObject*)self, signal, args, NULL, NULL,
                           XEVENT_PRIORITY_NORMAL);
    } else {
        XVarList_delete(args);
    }
}

static void xide_emitEditorHint(XItemDelegate* self, size_t signal,
                                XWidget* editor, int hint)
{
    XVarList* args = XVarList_Create(XVar(XWidget*, editor),
                                     XVar(int, hint));
    if (!args) return;
    if (self && ((XObject*)self)->m_signalSlot) {
        XObject_emitSignal((XObject*)self, signal, args, NULL, NULL,
                           XEVENT_PRIORITY_NORMAL);
    } else {
        XVarList_delete(args);
    }
}

/* ==================== 默认虚槽实现（对标 QStyledItemDelegate 缺省编辑器） ==================== */

static void VXItemDelegate_defaultSetEditorData(XItemDelegate* self,
                                                XWidget* editor,
                                                XAbstractItemView* view,
                                                int row, int col);
static void VXItemDelegate_defaultSetModelData(XItemDelegate* self,
                                               XWidget* editor,
                                               XAbstractItemView* view,
                                               int row, int col);

/*
 * 内嵌行文本编辑器（XItemLineEditor）：XLineEdit 不可直接修改，参照
 * XComboBox 内嵌 XComboEdit 方案本地派生。持有发起视图与所属委托回指，
 * 拦截编辑结束键并经委托信号走视图提交/关闭闭环（对标 Qt 缺省编辑器
 * 由 QStyledItemDelegate::eventFilter 拦截结束键的壳-过滤关系）。
 */
XCLASS_DEFINE_BEGING(XItemLineEditor)
XCLASS_DEFINE_EXTEND_END(XItemLineEditor, XLineEdit)

/** @brief 委托内嵌行编辑器对象；m_base 必须是第一个成员。 */
typedef struct XItemLineEditor
{
    XLineEdit m_base;                 /**< 基类成员（嵌 XLineEdit）；必须是第一个。 */
    XAbstractItemView* m_view;        /**< 发起编辑的视图（借用；可为 NULL）。 */
    XItemDelegate* m_delegate;        /**< 所属委托（借用；信号发射源）。 */
} XItemLineEditor;

static void VXItemLineEditor_keyPressEvent(XWidget* self, XEvent* event);
static void VXItemLineEditor_focusOutEvent(XWidget* self, XEvent* event);

/** @brief 键盘：Return/Enter 与 Tab/Backtab 提交并关闭（NoHint/
 *         EditNextItem/EditPreviousItem，提交分支）；Esc 放弃关闭
 *         （RevertModelCache，放弃分支）；其余键交基类控制器正常编辑
 *         （对标 QStyledItemDelegate::eventFilter 的结束键拦截）。 */
static void VXItemLineEditor_keyPressEvent(XWidget* self, XEvent* event)
{
    XItemLineEditor* edit = (XItemLineEditor*)self;
    XItemDelegate* delegate;
    XKeyEvent* ke;
    int key;
    if (!edit || !event ||
        XEvent_type(event) != XEVENT_TYPE_KEY_PRESS) {
        XClass_Parent(XLineEdit, EXWidget_KeyPressEvent,
                      void (*)(XWidget*, XEvent*))(self, event);
        return;
    }
    delegate = edit->m_delegate;
    ke = (XKeyEvent*)event;
    key = XKeyEvent_key(ke);
    if (!delegate || !edit->m_view) {
        XClass_Parent(XLineEdit, EXWidget_KeyPressEvent,
                      void (*)(XWidget*, XEvent*))(self, event);
        return;
    }
    if (key == (int)XKey_Return || key == (int)XKey_Enter) {
        /* 提交分支：commitData（落模型）→ closeEditor(NoHint)。 */
        XItemDelegate_commitData_signal(delegate, (XWidget*)edit);
        XItemDelegate_closeEditor_signal(
            delegate, (XWidget*)edit, XItemDelegateEndEditHint_NoHint);
        XEvent_accept(event);
        return;
    }
    if (key == (int)XKey_Escape) {
        /* 放弃分支：仅 closeEditor(RevertModelCache)，不写模型。 */
        XItemDelegate_closeEditor_signal(
            delegate, (XWidget*)edit,
            XItemDelegateEndEditHint_RevertModelCache);
        XEvent_accept(event);
        return;
    }
    if (key == (int)XKey_Tab || key == (int)XKey_Backtab) {
        /* 提交分支 + 编辑链：Tab→下一项、Backtab→上一项（对标 Qt）。 */
        XItemDelegate_commitData_signal(delegate, (XWidget*)edit);
        XItemDelegate_closeEditor_signal(
            delegate, (XWidget*)edit,
            key == (int)XKey_Tab
                ? XItemDelegateEndEditHint_EditNextItem
                : XItemDelegateEndEditHint_EditPreviousItem);
        XEvent_accept(event);
        return;
    }
    XClass_Parent(XLineEdit, EXWidget_KeyPressEvent,
                  void (*)(XWidget*, XEvent*))(self, event);
}

/** @brief 失焦：先走基类回显状态推送，再在会话未关闭时经提交分支关闭
 *         （对标 Qt 编辑器失焦 → closeEditor(NoHint) 提交关闭；
 *         m_editClosing 门禁抑制视图主动关闭时的重入）。 */
static void VXItemLineEditor_focusOutEvent(XWidget* self, XEvent* event)
{
    XItemLineEditor* edit = (XItemLineEditor*)self;
    XAbstractItemView* view;
    XClass_Parent(XLineEdit, EXWidget_FocusOutEvent,
                  void (*)(XWidget*, XEvent*))(self, event);
    if (!edit || !edit->m_view || !edit->m_delegate) return;
    view = edit->m_view;
    if (view->m_editClosing) return;
    if (view->m_editor != (XWidget*)edit) return;
    XItemDelegate_closeEditor_signal(
        edit->m_delegate, (XWidget*)edit, XItemDelegateEndEditHint_NoHint);
}

/** @brief 内嵌行编辑器子类虚表：覆写按键与失焦，其余继承 XLineEdit。 */
static XVtable* XItemLineEditor_class_init(void)
{
    XVTABLE_INIT_DEFAULT(XItemLineEditor)
    XVTABLE_INHERIT_XCLASS(XLineEdit);
    XVTABLE_OVERLOAD_DEFAULT(EXWidget_KeyPressEvent,
                             VXItemLineEditor_keyPressEvent);
    XVTABLE_OVERLOAD_DEFAULT(EXWidget_FocusOutEvent,
                             VXItemLineEditor_focusOutEvent);
    return XVTABLE_DEFAULT;
}

/** @brief 创建编辑器：内嵌行编辑器（父控件=视图，对象移交视图托管）。 */
static XWidget* VXItemDelegate_defaultCreateEditor(XItemDelegate* self,
                                                   XAbstractItemView* view,
                                                   int row, int col)
{
    XItemLineEditor* editor;
    (void)row;
    (void)col;
    if (!view) return NULL;
    editor =
        (XItemLineEditor*)XMemory_malloc(sizeof(*editor),
                                         XCLASS_DEFAULT_MEMORY_TYPE);
    if (!editor) return NULL;
    XMemset(editor, 0, sizeof(*editor));
    XLineEdit_init(&editor->m_base, (XWidget*)view, 0);
    XClassSetVtable(editor, XItemLineEditor);
    editor->m_view = view;
    editor->m_delegate = self;
    Set_Class_Memory(editor, XCLASS_DEFAULT_MEMORY_TYPE);
    Set_Class_IsHeap(editor, true);
    return (XWidget*)editor;
}

/** @brief 装载初值：EditRole 文本 + 全选（对标 QExpandingLineEdit 的
 *         selectAllOnFocus——键入即整体替换原值）。 */
static void VXItemDelegate_defaultSetEditorData(XItemDelegate* self,
                                                XWidget* editor,
                                                XAbstractItemView* view,
                                                int row, int col)
{
    const char* text;
    (void)self;
    if (!editor || !view) return;
    /* 默认编辑器为 XLineEdit 派生（缺省编辑器工厂语义）；自定义编辑器
     * 应由自定义委托覆写本槽。 */
    text = XAbstractItemView_itemText(view, row, col,
                                      XItemDataRole_EditRole);
    XLineEdit_setText((XLineEdit*)editor, text ? text : "");
    XLineEdit_selectAll((XLineEdit*)editor);
}

/** @brief 回写模型：行编辑器文本经 EditRole 通路写模型 setData（发射
 *         dataChanged 驱动视图刷新，对标 Qt 提交链 setModelData）。 */
static void VXItemDelegate_defaultSetModelData(XItemDelegate* self,
                                               XWidget* editor,
                                               XAbstractItemView* view,
                                               int row, int col)
{
    const char* text;
    (void)self;
    if (!editor || !view) return;
    text = XLineEdit_text((XLineEdit*)editor);
    XAbstractItemView_setItemText(view, row, col,
                                  XItemDataRole_EditRole, text);
}

/** @brief 摆放几何：条目矩形整格（对标 Qt 缺省 editor->setGeometry
 *         (option.rect)）。 */
static void VXItemDelegate_defaultUpdateEditorGeometry(
    XItemDelegate* self, XWidget* editor, const XRect* cellRect)
{
    (void)self;
    if (!editor || !cellRect) return;
    XWidget_setGeometryRect(editor, cellRect);
}

/* ==================== 类初始化与生命周期 ==================== */

static void VXItemDelegate_deinit(XItemDelegate* self);

/** @brief 析构：委托自身无资源，仅分派父类析构。 */
static void VXItemDelegate_deinit(XItemDelegate* self)
{
    if (!self) return;
    XClass_Deinit_Parent(XObject, (XObject*)self);
}

XVtable* XItemDelegate_class_init(void)
{
    XVTABLE_INIT_DEFAULT(XItemDelegate)
    XVTABLE_INHERIT_XCLASS(XObject);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Deinit, VXItemDelegate_deinit);
    XVTABLE_OVERLOAD_DEFAULT(EXItemDelegate_CreateEditor,
                             VXItemDelegate_defaultCreateEditor);
    XVTABLE_OVERLOAD_DEFAULT(EXItemDelegate_SetEditorData,
                             VXItemDelegate_defaultSetEditorData);
    XVTABLE_OVERLOAD_DEFAULT(EXItemDelegate_SetModelData,
                             VXItemDelegate_defaultSetModelData);
    XVTABLE_OVERLOAD_DEFAULT(EXItemDelegate_UpdateEditorGeometry,
                             VXItemDelegate_defaultUpdateEditorGeometry);
    return XVTABLE_DEFAULT;
}

void XItemDelegate_init(XItemDelegate* self)
{
    if (!self) return;
    XMemset(self, 0, sizeof(*self));
    XObject_init((XObject*)self);
    XClassSetVtable(self, XItemDelegate);
}

XItemDelegate* XItemDelegate_create_ex(XMemoryType memory)
{
    XItemDelegate* self =
        (XItemDelegate*)XMemory_malloc(sizeof(*self), memory);
    if (!self) return NULL;
    XItemDelegate_init(self);
    Set_Class_Memory(self, memory);
    Set_Class_IsHeap(self, true);
    return self;
}

/* ==================== 编辑器生命周期（查表分派） ==================== */

XWidget* XItemDelegate_createEditor(XItemDelegate* self,
                                    XAbstractItemView* view,
                                    int row, int col)
{
    XItemDelegate_CreateEditorFunc fn;
    if (!self) return NULL;
    fn = (XItemDelegate_CreateEditorFunc)XVtableGetFunc(
        XClassGetVtable(self), EXItemDelegate_CreateEditor, void*);
    if (!fn) return NULL;
    return fn(self, view, row, col);
}

void XItemDelegate_setEditorData(XItemDelegate* self, XWidget* editor,
                                 XAbstractItemView* view, int row, int col)
{
    XItemDelegate_SetEditorDataFunc fn;
    if (!self) return;
    fn = (XItemDelegate_SetEditorDataFunc)XVtableGetFunc(
        XClassGetVtable(self), EXItemDelegate_SetEditorData, void*);
    if (!fn) return;
    fn(self, editor, view, row, col);
}

void XItemDelegate_setModelData(XItemDelegate* self, XWidget* editor,
                                XAbstractItemView* view, int row, int col)
{
    XItemDelegate_SetModelDataFunc fn;
    if (!self) return;
    fn = (XItemDelegate_SetModelDataFunc)XVtableGetFunc(
        XClassGetVtable(self), EXItemDelegate_SetModelData, void*);
    if (!fn) return;
    fn(self, editor, view, row, col);
}

void XItemDelegate_updateEditorGeometry(XItemDelegate* self, XWidget* editor,
                                        const XRect* cellRect)
{
    XItemDelegate_UpdateEditorGeometryFunc fn;
    if (!self) return;
    fn = (XItemDelegate_UpdateEditorGeometryFunc)XVtableGetFunc(
        XClassGetVtable(self), EXItemDelegate_UpdateEditorGeometry, void*);
    if (!fn) return;
    fn(self, editor, cellRect);
}

/* ==================== 信号 ==================== */

void* XItemDelegate_commitData_signal(XItemDelegate* self, XWidget* editor)
{
    xide_emitEditor(self, (size_t)XItemDelegate_commitData_signal, editor);
    return (void*)(size_t)XItemDelegate_commitData_signal;
}

void* XItemDelegate_closeEditor_signal(XItemDelegate* self, XWidget* editor,
                                       int hint)
{
    xide_emitEditorHint(self, (size_t)XItemDelegate_closeEditor_signal,
                        editor, hint);
    return (void*)(size_t)XItemDelegate_closeEditor_signal;
}

#endif /* XWIDGET_ON && XTABLEWIDGET_ON */
