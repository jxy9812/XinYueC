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
#if XGUI_ON

#include "XItemDelegate.h"

#include "XLineEdit.h"
#include "XMemory.h"
#include "XClass.h"
#include "XVarList.h"
#include "XEvent.h"
#include "XEventType.h"
#include "XStyleOption.h"  /* XStyleOptionViewItem / XStyleOptionViewItem_init（paint/sizeHint 选项）。 */
#include "XPainter.h"      /* XPainter_save/restore/setClipRect（绘制裁剪）。 */

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

/** @brief 销毁编辑器：对标 Qt 缺省 destroyEditor 的延迟销毁
 *         （qabstractitemdelegate.cpp:237-241 `editor->deleteLater()`）。 */
static void VXItemDelegate_defaultDestroyEditor(XItemDelegate* self,
                                                XWidget* editor)
{
    (void)self;
    if (!editor) return;
    XObject_deleteLater((XObject*)editor);
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
    if (!editor || !view) return;
    text = XLineEdit_text((XLineEdit*)editor);
    XAbstractItemView_setItemText(view, row, col,
                                  XItemDataRole_EditRole, text);
    /* 回写可能改变条目文本 ⇒ 尺寸提示可能变化：对标 QAbstractItemDelegate
     * 的 sizeHintChanged 语义（委托确知尺寸提示变化时发射）在本缺省路径
     * 的落点。 */
    XItemDelegate_sizeHintChanged_signal(self, row, col);
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

/* ==================== 绘制面（paint/sizeHint，本批接通） ==================== */

/**
 * @brief 缺省 Paint 虚槽（对标 QItemDelegate::paint 的绘制骨架）。
 *
 *        本库策略（对齐 Qt 但适配既有视图）：缺省委托**不绘制**任何像素——
 *        派生视图在 paint_base 返回后继续既有自绘（像素零漂移）。裁剪开关
 *        hasClipping() 在缺省路径下**无消费方**（对标矩阵注记"无 paint 时
 *        该位无消费方"）；覆写路径的条目矩形裁剪由 paint_base 在分派前
 *        统一施加（非本槽自行读取——见 paint_base 的 save/setClipRect 段）。
 *        如此缺省委托对现有视图绘制零侵入；自定义委托覆写后由视图按
 *        paintOverridden 分流——XTableView 接管内容自绘 / XTableWidget
 *        叠加绘制（两视图口径见 XItemDelegate.h 文件头 @details）。
 *
 * @param self   目标委托。
 * @param painter 绘制器（已平移到视图局部原点）。
 * @param option 条目样式选项（矩形/状态/字体/调色板/对齐/省略模式）。
 * @param view   发起绘制的视图（提供模型数据与角色取数）。
 * @param row    行号。
 * @param col    列号。
 */
static void VXItemDelegate_defaultPaint(XItemDelegate* self, XPainter* painter,
                                        XStyleOptionViewItem* option,
                                        XAbstractItemView* view, int row,
                                        int col)
{
    /* 缺省无操作：不绘制、不裁剪。绘制面查委托的接线由视图 paintEvent
     * 在每格调用 paint_base 完成；缺省委托回落视图既有自绘。自定义委托
     * 覆写本槽时由视图按 paintOverridden 分流（XTableView 接管内容自绘 /
     * XTableWidget 叠加绘制，不是像素级完全接管）。 */
    (void)self;
    (void)painter;
    (void)option;
    (void)view;
    (void)row;
    (void)col;
}

/**
 * @brief 缺省 SizeHint 虚槽（对标 QItemDelegate::sizeHint）。
 *
 *        缺省返回 0×0——调用方（视图的 resizeToContents / 行高列宽测算）
 *        据此回落既有测算（像素零漂移）。自定义委托覆写本槽后其返回值参与
 *        该格行高/列宽测算。
 *
 * @param self   目标委托。
 * @param option 条目样式选项（矩形/字体/状态等）。
 * @param view   发起测算的视图。
 * @param row    行号。
 * @param col    列号。
 * @return 条目尺寸（像素）；缺省 0×0。
 */
static XSize VXItemDelegate_defaultSizeHint(XItemDelegate* self,
                                            XStyleOptionViewItem* option,
                                            XAbstractItemView* view, int row,
                                            int col)
{
    (void)self;
    (void)option;
    (void)view;
    (void)row;
    (void)col;
    return (XSize){ 0, 0 };
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
    XVTABLE_OVERLOAD_DEFAULT(EXItemDelegate_DestroyEditor,
                             VXItemDelegate_defaultDestroyEditor);
    XVTABLE_OVERLOAD_DEFAULT(EXItemDelegate_SetEditorData,
                             VXItemDelegate_defaultSetEditorData);
    XVTABLE_OVERLOAD_DEFAULT(EXItemDelegate_SetModelData,
                             VXItemDelegate_defaultSetModelData);
    XVTABLE_OVERLOAD_DEFAULT(EXItemDelegate_UpdateEditorGeometry,
                             VXItemDelegate_defaultUpdateEditorGeometry);
    XVTABLE_OVERLOAD_DEFAULT(EXItemDelegate_Paint, VXItemDelegate_defaultPaint);
    XVTABLE_OVERLOAD_DEFAULT(EXItemDelegate_SizeHint,
                             VXItemDelegate_defaultSizeHint);
    return XVTABLE_DEFAULT;
}

void XItemDelegate_init(XItemDelegate* self)
{
    if (!self) return;
    XMemset(self, 0, sizeof(*self));
    XObject_init((XObject*)self);
    XClassSetVtable(self, XItemDelegate);
    /* 对标 Qt QItemDelegatePrivate() : f(nullptr), clipPainting(true)：
     * 裁剪缺省开启、编辑器工厂缺省 NULL（走内嵌行编辑器缺省路径）。 */
    self->m_clipPainting = true;
    self->m_itemEditorFactory = NULL;
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

/* ==================== 编辑器生命周期（查表分派；对外唯一入口 _base，
   用户 2026-11-14 定案） ==================== */

XWidget* XItemDelegate_createEditor_base(XItemDelegate* self,
                                         XAbstractItemView* view,
                                         int row, int col)
{
    XItemDelegate_CreateEditorFunc fn;
    if (ISNULL(self, "") || ISNULL(XClassGetVtable(self), ""))
        return NULL;
    fn = (XItemDelegate_CreateEditorFunc)XVtableGetFunc(
        XClassGetVtable(self), EXItemDelegate_CreateEditor, void*);
    if (!fn) return NULL;
    return fn(self, view, row, col);
}

void XItemDelegate_destroyEditor_base(XItemDelegate* self, XWidget* editor)
{
    XItemDelegate_DestroyEditorFunc fn;
    if (ISNULL(self, "") || ISNULL(XClassGetVtable(self), ""))
        return;
    fn = (XItemDelegate_DestroyEditorFunc)XVtableGetFunc(
        XClassGetVtable(self), EXItemDelegate_DestroyEditor, void*);
    if (!fn) return;
    fn(self, editor);
}

void XItemDelegate_setEditorData_base(XItemDelegate* self, XWidget* editor,
                                      XAbstractItemView* view, int row,
                                      int col)
{
    XItemDelegate_SetEditorDataFunc fn;
    if (ISNULL(self, "") || ISNULL(XClassGetVtable(self), ""))
        return;
    fn = (XItemDelegate_SetEditorDataFunc)XVtableGetFunc(
        XClassGetVtable(self), EXItemDelegate_SetEditorData, void*);
    if (!fn) return;
    fn(self, editor, view, row, col);
}

void XItemDelegate_setModelData_base(XItemDelegate* self, XWidget* editor,
                                     XAbstractItemView* view, int row,
                                     int col)
{
    XItemDelegate_SetModelDataFunc fn;
    if (ISNULL(self, "") || ISNULL(XClassGetVtable(self), ""))
        return;
    fn = (XItemDelegate_SetModelDataFunc)XVtableGetFunc(
        XClassGetVtable(self), EXItemDelegate_SetModelData, void*);
    if (!fn) return;
    fn(self, editor, view, row, col);
}

void XItemDelegate_updateEditorGeometry_base(XItemDelegate* self,
                                             XWidget* editor,
                                             const XRect* cellRect)
{
    XItemDelegate_UpdateEditorGeometryFunc fn;
    if (ISNULL(self, "") || ISNULL(XClassGetVtable(self), ""))
        return;
    fn = (XItemDelegate_UpdateEditorGeometryFunc)XVtableGetFunc(
        XClassGetVtable(self), EXItemDelegate_UpdateEditorGeometry, void*);
    if (!fn) return;
    fn(self, editor, cellRect);
}

/* ==================== 绘制面（paint/sizeHint；对外唯一入口 _base） ==================== */

bool XItemDelegate_hasClipping(const XItemDelegate* self)
{
    /* 对标 Qt QItemDelegate::hasClipping（qitemdelegate.cpp:325-327）：
     * return d->clipPainting; 缺省 true。 */
    return self ? self->m_clipPainting : false;
}

void XItemDelegate_setClipping(XItemDelegate* self, bool clip)
{
    /* 对标 Qt QItemDelegate::setClipping（qitemdelegate.cpp:331-334）：
     * d->clipPainting = clip; 仅承载开关，不做绘制。 */
    if (!self) return;
    self->m_clipPainting = clip;
}

void* XItemDelegate_itemEditorFactory(const XItemDelegate* self)
{
    /* 对标 Qt QItemDelegate::itemEditorFactory（qitemdelegate.cpp:565-568）：
     * return d->f; 未设置返回 NULL（对齐"未设置则走默认工厂"的指针语义）。 */
    return self ? self->m_itemEditorFactory : NULL;
}

void XItemDelegate_setItemEditorFactory(XItemDelegate* self, void* factory)
{
    /* 对标 Qt QItemDelegate::setItemEditorFactory（qitemdelegate.cpp:578-581）：
     * d->f = factory; 仅承载指针（借用），不析构。 */
    if (!self) return;
    self->m_itemEditorFactory = factory;
}

void XItemDelegate_paint_base(XItemDelegate* self, XPainter* painter,
                              XStyleOptionViewItem* option,
                              XAbstractItemView* view, int row, int col)
{
    XItemDelegate_PaintFunc fn;
    bool clipped = false;
    if (ISNULL(self, "") || ISNULL(XClassGetVtable(self), ""))
        return;
    fn = (XItemDelegate_PaintFunc)XVtableGetFunc(XClassGetVtable(self),
                                                 EXItemDelegate_Paint, void*);
    if (!fn) return;
    /* 绘制裁剪（对标 Qt QItemDelegate::paint 的
     * `painter->save(); if (d->clipPainting) painter->setClipRect(opt.rect);`
     * … `painter->restore();`，qitemdelegate.cpp:375-377/434）。只在派生
     * 委托确实覆写 Paint（槽≠缺省 no-op）时施加，缺省路径不动绘制器状态；
     * 绘制结束必 restore，不把裁剪泄漏给后续格/网格线。
     * 裁剪开关编译期关闭（XPAINTER_CLIP_ON=0）时 setClipRect 与其操作枚举
     * 都不存在 ⇒ 整段守卫掉（该配置下 hasClipping 仍可读写，只是无绘制
     * 消费方，语义即 qt 的 `if (d->clipPainting)` 被编译掉）。 */
#if XPAINTER_CLIP_ON
    if (painter && option && self->m_clipPainting &&
        fn != VXItemDelegate_defaultPaint) {
        clipped = XPainter_save(painter);
        if (clipped)
            XPainter_setClipRect(painter, &option->m_base.m_rect,
                                 XPainterClipOperation_ReplaceClip);
    }
#else
    (void)clipped;
#endif /* XPAINTER_CLIP_ON */
    fn(self, painter, option, view, row, col);
    if (clipped) XPainter_restore(painter);
}

bool XItemDelegate_paintOverridden(const XItemDelegate* self)
{
    /* 精确判定：比对虚表 Paint 槽与缺省实现（非"子类即接管"，避免只覆写
     * 编辑器虚槽的子类被误判为接管绘制）。VXItemDelegate_defaultPaint 为本
     * 翻译单元静态符号，链接期可见。 */
    void* slot;
    if (ISNULL(self, "") || ISNULL(XClassGetVtable(self), ""))
        return false;
    slot = XVtableGetFunc(XClassGetVtable(self), EXItemDelegate_Paint, void*);
    return slot != (void*)VXItemDelegate_defaultPaint;
}

XSize XItemDelegate_sizeHint_base(XItemDelegate* self,
                                  XStyleOptionViewItem* option,
                                  XAbstractItemView* view, int row, int col)
{
    XItemDelegate_SizeHintFunc fn;
    XSize zero = { 0, 0 };
    if (ISNULL(self, "") || ISNULL(XClassGetVtable(self), ""))
        return zero;
    fn = (XItemDelegate_SizeHintFunc)XVtableGetFunc(
        XClassGetVtable(self), EXItemDelegate_SizeHint, void*);
    if (!fn) return zero;
    return fn(self, option, view, row, col);
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

/** @brief 发射两 int 载荷信号（sizeHintChanged 的行/列）。 */
static void xide_emitInt2(XItemDelegate* self, size_t signal, int a, int b)
{
    XVarList* args = XVarList_Create(XVar(int, a), XVar(int, b));
    if (!args) return;
    if (self && ((XObject*)self)->m_signalSlot) {
        XObject_emitSignal((XObject*)self, signal, args, NULL, NULL,
                           XEVENT_PRIORITY_NORMAL);
    } else {
        XVarList_delete(args);
    }
}

void* XItemDelegate_sizeHintChanged_signal(XItemDelegate* self, int row,
                                           int col)
{
    /* 真实发射点：缺省 setModelData（见 VXItemDelegate_defaultSetModelData）。 */
    xide_emitInt2(self, (size_t)XItemDelegate_sizeHintChanged_signal, row,
                  col);
    return (void*)(size_t)XItemDelegate_sizeHintChanged_signal;
}

#endif /* XWIDGET_ON && XTABLEWIDGET_ON */

#endif /* XGUI_ON */
