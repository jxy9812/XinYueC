/**
 * @file       XItemDelegate.h
 * @brief      XItemDelegate 条目委托与编辑闭环（对标 Qt 6.8
 *             QAbstractItemDelegate/QStyledItemDelegate 编辑面）。
 * @details    承载条目编辑器生命周期五虚槽（createEditor/destroyEditor/
 *             setEditorData/setModelData/updateEditorGeometry，对标
 *             QAbstractItemDelegate 同名虚函数）与 commitData/closeEditor/
 *             sizeHintChanged 三信号（对标同名信号，
 *             EndEditHint 数值对齐 Qt）；默认实现（对标 Qt 缺省
 *             QStyledItemDelegate 的行文本编辑器路径）提供内嵌 XLineEdit
 *             编辑器：Enter/Tab 提交、Esc 放弃、失焦提交关闭（对标 Qt
 *             缺省编辑器事件过滤语义）。
 * @details    绘制面（paint/sizeHint）已实现：Paint/SizeHint 双虚槽参与
 *             **XTableView / XTableWidget** 的条目绘制与行高/列宽测算（真实
 *             调用点是这两个派生类的 paintEvent 与 resizeColumn/RowToContents；
 *             XAbstractItemView 基类自身不含绘制/测算消费，它只提供共用的
 *             委托解析入口 XAbstractItemView_resolveDelegate）。缺省委托未
 *             覆写 Paint/SizeHint 时视图回落既有自绘（像素零漂移），自定义
 *             委托覆写即生效。
 *             两视图接入形态不同（2026-11-15 复核订正，勿再混写）：
 *             XTableView = 覆写者**接管内容自绘**（跳过 xtv_drawCellContent；
 *             选中/交替行底色与网格线仍由视图画）；XTableWidget = 视图既有
 *             item role 自绘照常 + 覆写者**叠加绘制**（不是完全接管——item
 *             级 bg/fg/勾选/装饰不经 option 下发，完全接管需先把
 *             XTableWidgetItem role 桥接进 XStyleOptionViewItem，登记专项
 *             未落）。
 *             hasClipping/setClipping 门控绘制裁剪（paint_base 在委托覆写
 *             Paint 时按位施加条目矩形裁剪）；itemEditorFactory/
 *             setItemEditorFactory **仅承载与查询**（借用指针，委托不析构）：
 *             框架尚无 XItemEditorFactory 类型，故 createEditor 未接工厂，
 *             缺省一律走内嵌行编辑器——接工厂需先落工厂类型本身。
 * @note       模块总开关 XTABLEWIDGET_ON；XObject 派生。委托为借用承载
 *             （视图不析构用户委托，仅析构自己懒创建的默认委托）。
 * @author     XinYueC 团队
 ******************************************************************************/
#ifndef XITEMDELEGATE_H
#define XITEMDELEGATE_H

#if XGUI_ON
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
XCLASS_DEFINE_ENUM(XItemDelegate, DestroyEditor),
XCLASS_DEFINE_ENUM(XItemDelegate, SetEditorData),
XCLASS_DEFINE_ENUM(XItemDelegate, SetModelData),
XCLASS_DEFINE_ENUM(XItemDelegate, UpdateEditorGeometry),
/** @brief 条目绘制虚槽（对标 QItemDelegate::paint；虚槽分派；缺省为无操作，
 *        由派生视图继续既有自绘——见 paint_base @note）。 */
XCLASS_DEFINE_ENUM(XItemDelegate, Paint),
/** @brief 条目尺寸提示虚槽（对标 QItemDelegate::sizeHint；虚槽分派；缺省为
 *        无操作，由视图既有测算兜底——见 sizeHint_base @note）。 */
XCLASS_DEFINE_ENUM(XItemDelegate, SizeHint),
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
    bool m_clipPainting; /**< 绘制裁剪开关（对标 QItemDelegate::clipping，缺省 true）。
                              paint_base 在派生委托覆写 Paint 时按此位给绘制器施加
                              条目矩形裁剪（对齐 Qt `if (d->clipPainting)
                              painter->setClipRect(opt.rect)`）。 */
    void* m_itemEditorFactory; /**< 编辑器工厂不透明指针（对标
                                    QItemDelegate::itemEditorFactory；缺省 NULL=走
                                    框架内嵌行编辑器缺省路径）。仅承载（生命周期归
                                    调用方）。 */
} XItemDelegate;

/** @brief 编辑器创建虚槽签名（对标 createEditor(parent, option, index)；
 *         parent 由 view 承载、option 以条目几何在 updateEditorGeometry
 *         阶段生效，见各虚槽 @note）。 */
typedef XWidget* (*XItemDelegate_CreateEditorFunc)(
    XItemDelegate* self, XAbstractItemView* view, int row, int col);
/** @brief 编辑器销毁虚槽签名（对标 destroyEditor(editor, index)；
 *         几何/模型索引参数在销毁路径不参与，简化为编辑器单参）。 */
typedef void (*XItemDelegate_DestroyEditorFunc)(XItemDelegate* self,
                                                XWidget* editor);
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
/** @brief 条目绘制虚槽签名（对标 paint(painter, option, index)；option 为
 *        条目样式选项——矩形/状态/字体/调色板/对齐/省略模式，由调用方按格填充）。 */
typedef void (*XItemDelegate_PaintFunc)(XItemDelegate* self, XPainter* painter,
                                       XStyleOptionViewItem* option,
                                       XAbstractItemView* view, int row,
                                       int col);
/** @brief 条目尺寸提示虚槽签名（对标 sizeHint(option, index)）。 */
typedef XSize (*XItemDelegate_SizeHintFunc)(XItemDelegate* self,
                                            XStyleOptionViewItem* option,
                                            XAbstractItemView* view, int row,
                                            int col);

/* ==================== 生命周期 ==================== */

XVtable* XItemDelegate_class_init(void);
/** @brief 初始化条目委托。 @param self 目标对象；不可为 NULL。 */
void XItemDelegate_init(XItemDelegate* self);
/** @brief 使用指定内存类型创建条目委托。
 * @param memory 内存类型。 @return 新建对象；失败 NULL。 */
XItemDelegate* XItemDelegate_create_ex(XMemoryType memory);
#define XItemDelegate_create() \
    XItemDelegate_create_ex(XCLASS_DEFAULT_MEMORY_TYPE)

/* ==================== 编辑器生命周期（查表分派入口；对外唯一入口
   一律 `_base`，用户 2026-11-14 定案） ==================== */

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
 * @note  虚函数入口只留 `_base`，函数体直接按 EXItemDelegate_CreateEditor
 *        虚表槽分派；不提供无后缀同名公开入口，也不通过另一函数二级
 *        转发。派生类业务实现仍是 static VX* 直挂虚表。
 */
XWidget* XItemDelegate_createEditor_base(XItemDelegate* self,
                                         XAbstractItemView* view,
                                         int row, int col);
/**
 * @brief 销毁条目编辑器（对标 QAbstractItemDelegate::destroyEditor；虚槽分派）。
 *
 *        默认实现与 Qt 一致走**延迟销毁**（Qt 原文
 *        qabstractitemdelegate.cpp:237-241：`editor->deleteLater();`）；
 *        本库对应 `XObject_deleteLater`。视图在 closeEditor 收口时应经本
 *        入口释放编辑器（Qt QAbstractItemView::closeEditor 调
 *        delegate->destroyEditor）——视图侧接线属基础层职责，见 batch
 *        baseNeeds 记录。
 *
 * @param self 目标委托（可为 NULL：无操作）。
 * @param editor 编辑器控件指针（createEditor 产物；可为 NULL：无操作）。
 * @return 无返回值。
 * @note  虚函数入口只留 `_base`（同 createEditor_base 定案口径）。
 */
void XItemDelegate_destroyEditor_base(XItemDelegate* self, XWidget* editor);
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
 * @note  虚函数入口只留 `_base`（同 createEditor_base 定案口径）。
 */
void XItemDelegate_setEditorData_base(XItemDelegate* self, XWidget* editor,
                                      XAbstractItemView* view, int row,
                                      int col);
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
 * @note  虚函数入口只留 `_base`（同 createEditor_base 定案口径）。
 */
void XItemDelegate_setModelData_base(XItemDelegate* self, XWidget* editor,
                                     XAbstractItemView* view, int row,
                                     int col);
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
 * @note  虚函数入口只留 `_base`（同 createEditor_base 定案口径）。
 */
void XItemDelegate_updateEditorGeometry_base(XItemDelegate* self,
                                             XWidget* editor,
                                             const XRect* cellRect);

/* ==================== 绘制面（paint/sizeHint；本批接通，对标 Qt 6.8
   QItemDelegate） ==================== */

/**
 * @brief 绘制裁剪开关查询（对标 QItemDelegate::hasClipping）。
 * @param self 目标委托；可为 NULL（返回 false）。
 * @return 绘制裁剪是否开启（缺省 true，对齐 Qt 构造 `clipPainting=true`）。
 * @note  仅当自定义委托覆写 Paint 虚槽时，paint_base 才按此位给绘制器施加
 *        条目矩形裁剪；缺省委托不绘制故该位无消费方（语义与 Qt 一致：
 *        "无 paint 时该位无消费方"）。
 */
bool XItemDelegate_hasClipping(const XItemDelegate* self);
/**
 * @brief 设置绘制裁剪开关（对标 QItemDelegate::setClipping）。
 * @param self 目标委托；可为 NULL（无操作）。
 * @param clip 裁剪开关（true=绘制时把裁剪区设为条目矩形）。
 * @return 无返回值。
 */
void XItemDelegate_setClipping(XItemDelegate* self, bool clip);

/**
 * @brief 绘制条目（对标 QItemDelegate::paint；虚槽分派）。
 *
 *        缺省委托（未覆写 Paint 虚槽）此入口为**无操作**——派生视图在调用
 *        本入口后即回落既有自绘（像素零漂移，对齐现状）。自定义委托覆写
 *        Paint 虚槽后，本入口转发至其实现，由委托绘制该格的**条目内容**
 *        （两视图接入形态见文件头 @details；委托只拿到下方 option 已填项，
 *        勾选/装饰不在其中，不能"全权"重画整格语义）；覆写 Paint 时
 *        paint_base 会先按 hasClipping() 给绘制器施加条目矩形裁剪（对齐
 *        Qt `if (d->clipPainting) painter->setClipRect(opt.rect)`）。
 *
 * @param self 目标委托（可为 NULL：无操作，交视图自绘）。
 * @param painter 绘制器（视图传入，已平移到视图局部原点；不可为 NULL）。
 * @param option 条目样式选项（矩形/状态/字体/调色板/对齐/省略模式等；
 *               已填项：m_rect / m_state / m_palette / m_font / m_text /
 *               m_textElideMode / m_displayAlignment；m_checkState /
 *               m_decorationSize / m_features 等尚未填充（视图侧自绘承担），
 *               自定义委托不应假设它们有效；其余字段由调用方按格填充）。
 * @param view 发起绘制的视图（提供模型数据与角色取数）。
 * @param row 行号。
 * @param col 列号。
 * @return 无返回值。
 * @note  虚函数入口只留 `_base`（同 createEditor_base 定案口径）；派生类
 *        业务实现仍是 static VX* 直挂虚表。
 */
void XItemDelegate_paint_base(XItemDelegate* self, XPainter* painter,
                              XStyleOptionViewItem* option,
                              XAbstractItemView* view, int row, int col);

/**
 * @brief 判定委托是否覆写了 Paint 虚槽（对标"自定义委托接管绘制"的判定）。
 * @param self 目标委托；可为 NULL（返回 false）。
 * @return 委托的 Paint 虚槽已被派生类覆写返回 true；缺省委托或未覆写返回 false。
 * @note  视图 paintEvent 据本判定决定委托绘制是否参与该格（2026-11-15 复核
 *        订正，两视图口径不同）：true 时 XTableView 由委托接管**内容**自绘
 *        （跳过 xtv_drawCellContent；底色/网格线仍视图画），XTableWidget 由
 *        委托**叠加**绘制（视图既有 item role 自绘照常、不跳过）。false 时
 *        （含 NULL）两视图均沿用既有自绘（像素零漂移）。
 *        精确比对虚表槽与缺省实现，无"子类即接管"的误判（子类可只覆写
 *        createEditor 等而不影响绘制归属）。
 */
bool XItemDelegate_paintOverridden(const XItemDelegate* self);
/**
 * @brief 计算条目尺寸提示（对标 QItemDelegate::sizeHint；虚槽分派）。
 *
 *        缺省委托（未覆写 SizeHint 虚槽）此入口返回 **0×0**——调用方（视图
 *        的 resizeToContents / 行高列宽测算）据此回落既有测算（像素零漂移）。
 *        自定义委托覆写 SizeHint 虚槽后，本入口转发至其实现，返回值参与该格
 *        的行高/列宽测算。
 *
 * @param self 目标委托（可为 NULL：返回 0×0，交视图既有测算）。
 * @param option 条目样式选项（矩形/字体/状态等；由调用方按格填充）。
 * @param view 发起测算的视图（提供模型数据与角色取数）。
 * @param row 行号。
 * @param col 列号。
 * @return 条目尺寸（像素）；缺省/空返回 0×0。
 * @note  虚函数入口只留 `_base`（同 createEditor_base 定案口径）。
 * @note  **与 Qt 的有意偏离**：Qt `QStyledItemDelegate::sizeHint`
 *        （qstyleditemdelegate.cpp:391-403）在无 SizeHintRole 时回落
 *        `style->sizeFromContents(CT_ItemViewItem, …)`，恒返回真值；本框架
 *        缺省返回 0×0 作为「无意见」哨兵，由视图沿用自身既有文本/角色测算
 *        （避免两套测算口径打架与像素漂移）。视图侧消费口径统一为
 *        `if (sh.width > content) content = sh.width;`——0 即不干预。
 */
XSize XItemDelegate_sizeHint_base(XItemDelegate* self,
                                  XStyleOptionViewItem* option,
                                  XAbstractItemView* view, int row, int col);

/**
 * @brief 查询编辑器工厂（对标 QItemDelegate::itemEditorFactory）。
 * @param self 目标委托；可为 NULL（返回 NULL）。
 * @return 编辑器工厂不透明指针（NULL=未设置，走内嵌行编辑器缺省路径）。
 * @warning 返回指针为借用承载，生命周期归设置方；调用方不得释放。
 */
void* XItemDelegate_itemEditorFactory(const XItemDelegate* self);
/**
 * @brief 设置编辑器工厂（对标 QItemDelegate::setItemEditorFactory）。
 * @param self 目标委托；可为 NULL（无操作）。
 * @param factory 编辑器工厂不透明指针（NULL=清除，回落内嵌行编辑器缺省路径）。
 * @return 无返回值。
 * @warning 仅承载（借用），生命周期归调用方；委托不析构工厂。
 */
void XItemDelegate_setItemEditorFactory(XItemDelegate* self, void* factory);

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
/**
 * @brief sizeHintChanged(row,col) 信号（条目尺寸提示变化时发射；对标
 *        QAbstractItemDelegate::sizeHintChanged，Qt 载荷为 QModelIndex，
 *        C 端以行/列平面承载）。
 * @note 发射点：默认 setModelData 完成模型回写之后（回写可能改变条目
 *       文本进而改变尺寸提示；Qt 在委托确知尺寸提示变化时发射）。
 *       自定义委托覆写 setModelData 后应自行在尺寸提示变化处发射。
 * @param self 目标委托。
 * @param row 行号。
 * @param col 列号。
 * @return 信号句柄（函数地址，用作信号标识）。
 */
void* XItemDelegate_sizeHintChanged_signal(XItemDelegate* self, int row,
                                           int col);

#endif /* XWIDGET_ON && XTABLEWIDGET_ON */
#ifdef __cplusplus
}
#endif

#endif /* XGUI_ON */

#endif /* XITEMDELEGATE_H */
