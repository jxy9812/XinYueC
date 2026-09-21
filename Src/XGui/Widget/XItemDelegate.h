/**
 * @file       XItemDelegate.h
 * @brief      XItemDelegate 条目委托与编辑闭环（对标 Qt 6.8
 *             QAbstractItemDelegate/QStyledItemDelegate 编辑面）。
 * @details    承载条目编辑器生命周期四虚槽（createEditor/setEditorData/
 *             setModelData/updateEditorGeometry，对标 QAbstractItemDelegate
 *             同名虚函数）与 commitData/closeEditor 两信号（对标同名信号，
 *             EndEditHint 数值对齐 Qt）；默认实现（对标 Qt 缺省
 *             QStyledItemDelegate 的行文本编辑器路径）提供内嵌 XLineEdit
 *             编辑器：Enter/Tab 提交、Esc 放弃、失焦提交关闭（对标 Qt
 *             缺省编辑器事件过滤语义）。绘制（paint/sizeHint）不在本批
 *             范围：条目绘制仍由派生视图直绘（@note）。
 * @note       模块总开关 XTABLEWIDGET_ON；XObject 派生。委托为借用承载
 *             （视图不析构用户委托，仅析构自己懒创建的默认委托）。
 * @author     XinYueC 团队
 ******************************************************************************/
#ifndef XITEMDELEGATE_H
#define XITEMDELEGATE_H
#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>
#include "XGuiConfig.h"
#include "XObject.h"
#include "XWidget.h"
#include "XAbstractItemView.h"

#if XWIDGET_ON && XTABLEWIDGET_ON

/**
 * @brief      编辑器关闭提示（对标 QAbstractItemDelegate::EndEditHint，
 *             数值一致）。
 */
typedef enum XItemDelegateEndEditHint
{
    XItemDelegateEndEditHint_NoHint = 0,            /**< 无提示（提交分支，对齐 NoHint）。 */
    XItemDelegateEndEditHint_EditNextItem = 1,      /**< 关闭后编辑下一项（对齐 EditNextItem）。 */
    XItemDelegateEndEditHint_EditPreviousItem = 2,  /**< 关闭后编辑上一项（对齐 EditPreviousItem）。 */
    XItemDelegateEndEditHint_SubmitModelCache = 3,  /**< 提交模型缓存（对齐 SubmitModelCache；本库与 NoHint 同为提交分支）。 */
    XItemDelegateEndEditHint_RevertModelCache = 4   /**< 放弃修改（对齐 RevertModelCache；唯一放弃分支）。 */
} XItemDelegateEndEditHint;

/* ==================== 类定义 ==================== */
XCLASS_DEFINE_BEGING(XItemDelegate)
XCLASS_DEFINE_ENUM(XItemDelegate, CreateEditor) = XCLASS_VTABLE_GET_SIZE(XObject),
XCLASS_DEFINE_ENUM(XItemDelegate, SetEditorData),
XCLASS_DEFINE_ENUM(XItemDelegate, SetModelData),
XCLASS_DEFINE_ENUM(XItemDelegate, UpdateEditorGeometry),
XCLASS_DEFINE_END(XItemDelegate)

/**
 * @brief      条目委托对象；m_base 必须是第一个成员（嵌 XObject）。
 * @details    对标 QAbstractItemDelegate：XObject 基座承载 commitData/
 *             closeEditor 信号；编辑器四虚槽经类虚表分派，默认实现为
 *             QStyledItemDelegate 的行文本编辑器路径。调用方不得手工
 *             修改字段；派生委托按 XClass 惯例本地派生并覆写虚槽。
 */
typedef struct XItemDelegate
{
    XObject m_base; /**< 基类成员；必须是第一个，由 XClass 管理。 */
} XItemDelegate;

/** @brief 编辑器创建虚槽签名（对标 createEditor(parent, option, index)；
 *         parent 由 view 承载、option 以条目几何在 updateEditorGeometry
 *         阶段生效，见各虚槽 @note）。 */
typedef XWidget* (*XItemDelegate_CreateEditorFunc)(
    XItemDelegate* self, XAbstractItemView* view, int row, int col);
/** @brief 编辑器数据装载虚槽签名（对标 setEditorData(editor, index)）。 */
typedef void (*XItemDelegate_SetEditorDataFunc)(
    XItemDelegate* self, XWidget* editor, XAbstractItemView* view,
    int row, int col);
/** @brief 编辑器数据回写虚槽签名（对标 setModelData(editor, model, index)；
 *         model 经 view->m_model 取用）。 */
typedef void (*XItemDelegate_SetModelDataFunc)(
    XItemDelegate* self, XWidget* editor, XAbstractItemView* view,
    int row, int col);
/** @brief 编辑器几何虚槽签名（对标 updateEditorGeometry(editor, option,
 *         index)；条目矩形已按 visualRect 换算为视图局部坐标）。 */
typedef void (*XItemDelegate_UpdateEditorGeometryFunc)(
    XItemDelegate* self, XWidget* editor, const XRect* cellRect);

/* ==================== 生命周期 ==================== */

XVtable* XItemDelegate_class_init(void);
/** @brief 初始化条目委托。 @param self 目标对象；不可为 NULL。 */
void XItemDelegate_init(XItemDelegate* self);
/** @brief 使用指定内存类型创建条目委托。
 * @param memory 内存类型。 @return 新建对象；失败 NULL。 */
XItemDelegate* XItemDelegate_create_ex(XMemoryType memory);
#define XItemDelegate_create() \
    XItemDelegate_create_ex(XCLASS_DEFAULT_MEMORY_TYPE)
#define XItemDelegate_deinit_base(self) XClass_deinit_base((XClass*)(self))
#define XItemDelegate_delete_base(self) XClass_delete_base((XClass*)(self))

/* ==================== 编辑器生命周期（查表分派入口） ==================== */

/**
 * @brief 创建条目编辑器（对标 createEditor；虚槽分派）。
 *
 *        编辑器以 view 为父控件（对标 createEditor 的 parent 参数）；
 *        返回的控件指针所有权移交调用方（视图在 closeEditor 时析构，
 *        对标 Qt 视图托管编辑器生命周期）。
 *
 * @param self 目标委托。
 * @param view 发起编辑的视图（提供模型数据与父控件承载）。
 * @param row 行号。
 * @param col 列号。
 * @return 编辑器控件指针；委托为空、派生实现放弃或分配失败返回 NULL。
 */
XWidget* XItemDelegate_createEditor(XItemDelegate* self,
                                    XAbstractItemView* view,
                                    int row, int col);
/**
 * @brief 装载编辑器初值（对标 setEditorData；虚槽分派）。
 *
 *        默认实现读取条目 EditRole 文本写入行编辑器并全选（对标 Qt
 *        QExpandingLineEdit 的 selectAllOnFocus：键入即整体替换）。
 *
 * @param self 目标委托。
 * @param editor 编辑器控件指针（createEditor 产物）。
 * @param view 发起编辑的视图。
 * @param row 行号。
 * @param col 列号。
 * @return 无返回值。
 */
void XItemDelegate_setEditorData(XItemDelegate* self, XWidget* editor,
                                 XAbstractItemView* view, int row, int col);
/**
 * @brief 编辑器数据回写模型（对标 setModelData；虚槽分派）。
 *
 *        默认实现读取行编辑器文本经条目 EditRole 通路写模型 setData
 *        （发射 dataChanged，同 Qt 提交链）。
 *
 * @param self 目标委托。
 * @param editor 编辑器控件指针。
 * @param view 发起编辑的视图（模型经 view->m_model 取用）。
 * @param row 行号。
 * @param col 列号。
 * @return 无返回值。
 */
void XItemDelegate_setModelData(XItemDelegate* self, XWidget* editor,
                                XAbstractItemView* view, int row, int col);
/**
 * @brief 摆放编辑器几何（对标 updateEditorGeometry；虚槽分派）。
 *
 *        默认实现以条目矩形（视图局部坐标）整格摆放（对标 Qt 缺省的
 *        editor->setGeometry(option.rect)）。
 *
 * @param self 目标委托。
 * @param editor 编辑器控件指针。
 * @param cellRect 条目矩形（视图局部坐标；visualRect 产物）。
 * @return 无返回值。
 */
void XItemDelegate_updateEditorGeometry(XItemDelegate* self, XWidget* editor,
                                        const XRect* cellRect);

/* ==================== 信号（对标 QAbstractItemDelegate） ==================== */

/**
 * @brief commitData(editor) 信号（编辑器请求提交时发射；视图槽据此走
 *        setModelData → 模型 setData 提交通路，对标 Qt 同名信号）。
 * @param self 目标委托。
 * @param editor 编辑器控件指针。
 * @return 信号句柄（函数地址，用作信号标识）。
 */
void* XItemDelegate_commitData_signal(XItemDelegate* self, XWidget* editor);
/**
 * @brief closeEditor(editor, hint) 信号（编辑器请求关闭时发射；hint 为
 *        XItemDelegateEndEditHint 枚举，RevertModelCache 为放弃分支，
 *        对标 Qt 同名信号）。
 * @param self 目标委托。
 * @param editor 编辑器控件指针。
 * @param hint 关闭提示（XItemDelegateEndEditHint）。
 * @return 信号句柄（函数地址，用作信号标识）。
 */
void* XItemDelegate_closeEditor_signal(XItemDelegate* self, XWidget* editor,
                                       int hint);

#endif /* XWIDGET_ON && XTABLEWIDGET_ON */
#ifdef __cplusplus
}
#endif
#endif /* XITEMDELEGATE_H */
