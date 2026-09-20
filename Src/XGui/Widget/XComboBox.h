/**
 * @file       XComboBox.h
 * @brief      XComboBox 下拉组合框控件（对标 Qt 6.8 QComboBox）。
 * @details    文本项下拉选择框：
 *             - 项管理：addItem/insertItem/insertItems/removeItem/
 *               setItemText/insertSeparator/clear、count/currentIndex/
 *               currentText/itemText、maxCount（超限丢弃）/maxVisibleItems/
 *               duplicatesEnabled；
 *             - 枚举：InsertPolicy（NoInsert=0..InsertAlphabetically=6，
 *               数值对齐 Qt）、SizeAdjustPolicy（AdjustToContents=0/
 *               AdjustToContentsOnFirstShow=1/AdjustToMinimumContents
 *               LengthWithIcon=2）——字段保留，尺寸自适应按
 *               AdjustToContents 实现于内部重排；
 *             - 弹出：showPopup/hidePopup（虚槽；第一版以回调窗口
 *               形式弹出项列表，选中回调 activated）；
 *             - 可编辑：setEditable + 内嵌 XLineEdit（editText 路径），
 *               placeholder/frame/validator 转发；
 *             - 补全（可编辑）：setCompleterMode/isCompleterMode——
 *               输入前缀自动过滤下拉弹层并高亮命中项（对标 QCompleter
 *               PopupCompletion 子集，复用下拉弹层承载，前缀匹配不
 *               区分大小写）；Enter 采纳高亮补全、Esc 收起弹层不改
 *               文本、Up/Down/PageUp/PageDown 在命中行间移动高亮；
 *             - 插入策略：insertPolicy/setInsertPolicy（枚举已对齐
 *               Qt 顺序）在可编辑文本编辑结束（Enter/失焦）时结算
 *               （对标 QComboBoxPrivate::returnPressed/editingFinished）：
 *               NoInsert 不插入；duplicates 关闭且文本已存在仅置当前
 *               项；InsertAtTop/InsertAtBottom/InsertAfterCurrent/
 *               InsertBeforeCurrent/InsertAlphabetically 按位插入新
 *               条目，InsertAtCurrent 以编辑文本替换当前项文本；用户
 *               激活路径（Enter）结算后发射 activated/textActivated；
 *             - 信号：activated(int)/textActivated(const char*)/
 *               highlighted(int)/currentIndexChanged(int)/
 *               currentTextChanged(const char*)/editTextChanged(
 *               const char*)。
 * @note       模块总开关 XCOMBOBOX_ON 定义于 XGuiConfig.h；=0 时裁剪
 *             全部公共 API。依赖 XWIDGET_ON、XLINEEDIT_ON。
 * @author     XinYueC 团队
 ******************************************************************************/
#ifndef XCOMBOBOX_H
#define XCOMBOBOX_H
#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>
#include "XGuiConfig.h"
#include "XWidget.h"
#include "XString.h"
#include "XStringList.h"
#include "XLineEdit.h"
/** @brief XCompleter 前向声明（补全类见 Task 2.19）。 */
typedef struct XCompleter XCompleter;

#if XWIDGET_ON && XCOMBOBOX_ON && XLINEEDIT_ON

/* ==================== 枚举（数值对齐 Qt） ==================== */

/** @brief 插入策略（对标 QComboBox::InsertPolicy）。 */
typedef enum XComboBoxInsertPolicy
{
    XComboBoxInsertPolicy_NoInsert = 0,              /**< 不插入。 */
    XComboBoxInsertPolicy_InsertAtTop = 1,           /**< 顶部插入。 */
    XComboBoxInsertPolicy_InsertAtCurrent = 2,       /**< 当前项处插入。 */
    XComboBoxInsertPolicy_InsertAtBottom = 3,        /**< 底部插入（默认）。 */
    XComboBoxInsertPolicy_InsertAfterCurrent = 4,    /**< 当前项之后。 */
    XComboBoxInsertPolicy_InsertBeforeCurrent = 5,   /**< 当前项之前。 */
    XComboBoxInsertPolicy_InsertAlphabetically = 6   /**< 按字母序插入。 */
} XComboBoxInsertPolicy;

/** @brief 尺寸自适应策略（对标 QComboBox::SizeAdjustPolicy）。 */
typedef enum XComboBoxSizeAdjustPolicy
{
    XComboBoxSizeAdjustPolicy_AdjustToContents = 0,  /**< 随内容调整。 */
    XComboBoxSizeAdjustPolicy_AdjustToContentsOnFirstShow = 1, /**< 首次显示时调整。 */
    XComboBoxSizeAdjustPolicy_AdjustToMinimumContentsLengthWithIcon = 2 /**< 按最小内容长度。 */
} XComboBoxSizeAdjustPolicy;

/* ==================== 类定义 ==================== */
XCLASS_DEFINE_BEGING(XComboBox)
XCLASS_DEFINE_EXTEND_END(XComboBox, XWidget)

/**
 * @brief      XComboBox 组合框对象；m_base 必须是第一个成员。
 * @details    字段含义（对标 QComboBox 同名属性）：
 *             - m_items/m_itemCount：文本项数组（堆分配，上限
 *               XCOMBOBOX_MAX_ITEMS）；
 *             - m_currentIndex：当前项（-1 = 无）；
 *             - m_maxCount/m_maxVisibleItems：容量与弹出可见项数；
 *             - m_duplicatesEnabled：允许重复项；
 *             - m_editable/m_lineEdit：可编辑模式与内嵌编辑框；
 *             - m_insertPolicy/m_sizeAdjustPolicy：策略字段；
 *             - m_minimumContentsLength：最小内容字符数；
 *             - m_frame：边框开关；m_placeholderText：占位文本；
 *             - m_popupVisible：弹出状态（内部）。
 *             调用者不得手工修改字段；一律走公开 API。
 */
/** @brief XListView 前向声明（弹出列表视图）。 */
typedef struct XListView XListView;
/** @brief XAbstractItemModel 前向声明（弹出列表数据模型）。 */
typedef struct XAbstractItemModel XAbstractItemModel;

typedef struct XComboBox
{
    XWidget m_base;                    /**< 基类成员；必须是第一个。 */
    XString** m_items;                 /**< 项文本数组（每项 XString* 拥有）。 */
    XString** m_itemData;              /**< 项数据数组（平行；对象拥有）。 */
    XString** m_itemIcons;             /**< 项图标路径数组（平行；对象拥有）。 */
    XCompleter* m_completer;           /**< 补全器（借用；可为 NULL）。 */
    int m_iconSize;                    /**< 图标尺寸（方边像素；默认 16）。 */
    int     m_itemCount;               /**< 当前项数。 */
    int     m_itemCapacity;            /**< 数组容量。 */
    int     m_currentIndex;            /**< 当前项索引（-1 无）。 */
    int     m_maxCount;                /**< 最大项数（默认 2147483647 截为可用上限）。 */
    int     m_maxVisibleItems;         /**< 弹出最大可见项数（默认 10）。 */
    bool    m_duplicatesEnabled;       /**< 允许重复项。 */
    bool    m_editable;                /**< 可编辑模式。 */
    XLineEdit* m_lineEdit;             /**< 可编辑模式的内嵌编辑框（拥有）。 */
    bool    m_completerMode;           /**< 可编辑时启用前缀补全过滤（对标内建
                                            completer 的 PopupCompletion 子集）。 */
    bool    m_completionActive;        /**< 弹层当前处于补全过滤态（内部）。 */
    int     m_completionRow;           /**< 补全弹层当前高亮行（内部；-1 无）。 */
    int     m_insertPolicy;            /**< 插入策略。 */
    int     m_sizeAdjustPolicy;        /**< 尺寸自适应策略。 */
    int     m_minimumContentsLength;   /**< 最小内容字符数。 */
    bool    m_frame;                   /**< 边框开关（默认 true）。 */    XString* m_placeholderText;             /**< 字符串字段（对象拥有）。 */
    bool    m_popupVisible;            /**< 弹出可见（内部）。 */
    int     m_savedHeight;             /**< 弹出前高度（展开/收起恢复）。 */
    XListView* m_popupView;            /**< 弹出列表视图（对象拥有；懒创建；对标 view）。 */
    XAbstractItemModel* m_model;       /**< 条目数据模型（对象拥有；懒创建并随条目同步）。 */
    int     m_modelColumn;             /**< 模型显示列（对标 modelColumn）。 */
    int     m_rootRow;                 /**< 根索引行（Qt QModelIndex 的平铺简化承载）。 */
    int     m_rootCol;                 /**< 根索引列。 */
    void*   m_validator;               /**< 校验器不透明指针（XValidator 体系未建，仅承载）。 */
    void*   m_itemDelegate;            /**< 条目委托不透明指针（委托体系未建，仅承载）。 */
    XTimerId m_grabTimer;              /**< 弹出后延迟执行平台鼠标抓取的定时器；无效时为
                                            XTIMER_INVALID_ID，仅供内部使用。 */
} XComboBox;

/* ==================== 生命周期 ==================== */

XVtable* XComboBox_class_init(void);
/** @brief XCombo盒init（对标 Qt 同名接口）。
 * @param self 目标控件指针。
 * @param parent 父控件指针；可为 NULL。
 * @param flags 窗口标志位组合。
 * @return 无返回值。
 */
void XComboBox_init(XComboBox* self, XWidget* parent, XWidgetFlags flags);
#define XComboBox_create(parent, flags) XComboBox_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, (parent), (flags))
/** @brief XCombo盒createex（对标 Qt 同名接口）。
 * @param memory XMemoryType 参数。
 * @param parent 父控件指针；可为 NULL。
 * @param flags 窗口标志位组合。
 * @return 返回对象指针；无效时返回 NULL。
 */
XComboBox* XComboBox_create_ex(XMemoryType memory, XWidget* parent, XWidgetFlags flags);
#define XComboBox_deinit_base(self) XWidget_deinit_base((XWidget*)(self))
#define XComboBox_delete_base(self) XWidget_delete_base((XWidget*)(self))

/* ==================== 项管理（对标 QComboBox public API） ==================== */

int XComboBox_count(const XComboBox* self);
/** @brief XCombo盒max数量（对标 Qt 同名接口）。
 * @param self 目标控件指针。
 * @return 返回对应数值；无效时返回 0 或 -1（视接口语义）。
 */
int XComboBox_maxCount(const XComboBox* self);
/** @brief XCombo盒setMax数量（对标 Qt 同名接口）。
 * @param self 目标控件指针。
 * @param max 最大值。
 * @return 无返回值。
 */
void XComboBox_setMaxCount(XComboBox* self, int max);
/** @brief XCombo盒max可见Items（对标 Qt 同名接口）。
 * @param self 目标控件指针。
 * @return 返回对应数值；无效时返回 0 或 -1（视接口语义）。
 */
int XComboBox_maxVisibleItems(const XComboBox* self);
/** @brief XCombo盒setMax可见Items（对标 Qt 同名接口）。
 * @param self 目标控件指针。
 * @param maxItems 最大项数。
 * @return 无返回值。
 */
void XComboBox_setMaxVisibleItems(XComboBox* self, int maxItems);
/** @brief XCombo盒duplicates启用（对标 Qt 同名接口）。
 * @param self 目标控件指针。
 * @return 条件成立返回 true，否则返回 false。
 */
bool XComboBox_duplicatesEnabled(const XComboBox* self);
/** @brief XCombo盒setDuplicates启用（对标 Qt 同名接口）。
 * @param self 目标控件指针。
 * @param enable bool 开关：true 启用。
 * @return 无返回值。
 */
void XComboBox_setDuplicatesEnabled(XComboBox* self, bool enable);
/** @brief XCombo盒set边框（对标 Qt 同名接口）。
 * @param self 目标控件指针。
 * @param on bool：true 开启。
 * @return 无返回值。
 */
void XComboBox_setFrame(XComboBox* self, bool on);
/** @brief XCombo盒has边框（对标 Qt 同名接口）。
 * @param self 目标控件指针。
 * @return 条件成立返回 true，否则返回 false。
 */
bool XComboBox_hasFrame(const XComboBox* self);
/** @brief XCombo盒insert策略（对标 Qt 同名接口）。
 * @details 只读非可编辑组合框无效果；可编辑组合框在编辑结束
 *          （Enter/失焦）时按本策略结算（各值语义见枚举与类型
 *          @details 说明）。
 * @param self 目标控件指针。
 * @return 返回对应数值；无效时返回 0 或 -1（视接口语义）。
 */
int XComboBox_insertPolicy(const XComboBox* self);
/** @brief XCombo盒set插入策略（对标 Qt 同名接口）。
 * @param self 目标控件指针。
 * @param policy 策略枚举。
 * @return 无返回值。
 */
void XComboBox_setInsertPolicy(XComboBox* self, int policy);
/** @brief XCombo盒sizeAdjust策略（对标 Qt 同名接口）。
 * @param self 目标控件指针。
 * @return 返回对应数值；无效时返回 0 或 -1（视接口语义）。
 */
int XComboBox_sizeAdjustPolicy(const XComboBox* self);
/** @brief XCombo盒set尺寸Adjust策略（对标 Qt 同名接口）。
 * @param self 目标控件指针。
 * @param policy 策略枚举。
 * @return 无返回值。
 */
void XComboBox_setSizeAdjustPolicy(XComboBox* self, int policy);
/** @brief XCombo盒minimum内容Length（对标 Qt 同名接口）。
 * @param self 目标控件指针。
 * @return 返回对应数值；无效时返回 0 或 -1（视接口语义）。
 */
int XComboBox_minimumContentsLength(const XComboBox* self);
/** @brief XCombo盒set最小内容Length（对标 Qt 同名接口）。
 * @param self 目标控件指针。
 * @param characters int 参数。
 * @return 无返回值。
 */
void XComboBox_setMinimumContentsLength(XComboBox* self, int characters);
/** @brief XCombo盒placeholder文本（对标 Qt 同名接口）。
 * @param self 目标控件指针。
 * @return 返回 UTF-8 文本；无效时返回空串。
 */
/** @brief 读取占位文本（返回新建 XString*，调用方负责 delete_base；对标 QComboBox::placeholderText）。
 * @param self 目标控件指针。
 * @return 新建 XString*；self 为 NULL 或未设置时返回 NULL。
 */
XString* XComboBox_placeholderText(const XComboBox* self);
/** @brief 读取占位文本（UTF-8 借用，对标 QComboBox::placeholderText）。
 * @param self 目标控件指针。
 * @return 内部 UTF-8 借用指针；未设置时返回空串，不得释放或修改。
 */
const char* XComboBox_placeholderText_2(const XComboBox* self);
/** @brief 设置占位文本（XString 主版本；对标 QComboBox::setPlaceholderText）。
 * @param self 目标控件指针。
 * @param placeholderText 借用 XString*；可为 NULL（按空串处理）。
 * @return 无返回值。
 */
void XComboBox_setPlaceholderText(XComboBox* self, const XString* placeholderText);
/** @brief 设置占位文本（UTF-8 兼容重载，转发主版本；对标 QComboBox::setPlaceholderText）。
 * @param self 目标控件指针。
 * @param placeholderText UTF-8 文本；可为 NULL（按空串处理）。
 * @return 无返回值。
 */
void XComboBox_setPlaceholderText_2(XComboBox* self, const char* placeholderText);
/** @brief XCombo盒is可编辑（对标 Qt 同名接口）。
 * @param self 目标控件指针。
 * @return 条件成立返回 true，否则返回 false。
 */
bool XComboBox_isEditable(const XComboBox* self);
/** @brief XCombo盒set可编辑（对标 Qt 同名接口）。
 * @param self 目标控件指针。
 * @param editable bool 参数。
 * @return 无返回值。
 */
void XComboBox_setEditable(XComboBox* self, bool editable);
/** @brief XCombo盒lineEdit（对标 Qt 同名接口）。
 * @param self 目标控件指针。
 * @return 返回对象指针；无效时返回 NULL。
 */
XLineEdit* XComboBox_lineEdit(const XComboBox* self);
/**
 * @brief      安装自定义行编辑框（对标 QComboBox::setLineEdit）。
 * @details    组合框取得所有权：安装或组合框销毁时释放行编辑框；若组合
 *             框当前不可编辑，先隐式置为可编辑（对标 Qt 安装编辑框的
 *             可用性要求）。旧行编辑框被释放，调用方不得重复释放。
 * @param      self 目标控件指针；传入 NULL 时函数不执行任何操作。
 * @param      edit 新行编辑框；不能为 NULL；可带任意父控件，安装后归
 *             组合框管理。
 * @return     无返回值。
 */
void XComboBox_setLineEdit(XComboBox* self, XLineEdit* edit);

/* ==================== 弹出列表部件化（对标 view/model/validator 族） ==================== */

/**
 * @brief      查询弹出列表视图（对标 QComboBox::view）。
 * @details    首次访问懒创建内置 XListView，并以组合框条目初始化其
 *             数据模型；视图由组合框拥有。
 * @param      self 目标控件指针；NULL 返回 NULL。
 * @return     借用指针，属于组合框内部存储，不能释放。
 */
XListView* XComboBox_view(XComboBox* self);
/**
 * @brief      安装自定义弹出列表视图（对标 QComboBox::setView）。
 * @details    组合框取得视图所有权：旧视图被释放；原数据模型自动
 *             设置到新视图。调用方之后不得重复释放传入的视图。
 * @param      self 目标控件指针；传入 NULL 时函数不执行任何操作。
 * @param      view 新视图；不能为 NULL；可带任意父控件，安装后归
 *             组合框管理。
 * @return     无返回值。
 */
void XComboBox_setView(XComboBox* self, XListView* view);
/**
 * @brief      查询条目数据模型（对标 QComboBox::model）。
 * @details    首次访问懒创建内置模型并随组合框条目同步（增删改条目
 *             后再次访问时刷新）。
 * @param      self 目标控件指针；NULL 返回 NULL。
 * @return     借用指针，属于组合框内部存储，不能释放。
 */
XAbstractItemModel* XComboBox_model(XComboBox* self);
/**
 * @brief      安装外部数据模型（对标 QComboBox::setModel）。
 * @details    组合框取得模型所有权（模型须为堆对象）；旧模型被释放，
 *             弹出视图（若已创建）切换到新模型。安装后条目数以模型为
 *             准的联动为简化承载：组合框条目数组仍是数据源。
 * @param      self 目标控件指针；传入 NULL 时函数不执行任何操作。
 * @param      model 新模型；NULL 仅清除并释放当前模型。
 * @return     无返回值。
 */
void XComboBox_setModel(XComboBox* self, XAbstractItemModel* model);
/**
 * @brief      查询模型显示列（对标 QComboBox::modelColumn）。 @param self 目标控件。 @return 列号（默认 0）。
 */
int XComboBox_modelColumn(const XComboBox* self);
/**
 * @brief      设置模型显示列（对标 QComboBox::setModelColumn）。
 * @param      self 目标控件；传入 NULL 时函数不执行任何操作。
 * @param      column 列号；越界由视图侧按模型列数钳制。
 * @return     无返回值。
 */
void XComboBox_setModelColumn(XComboBox* self, int column);
/**
 * @brief      设置根模型索引（对标 QComboBox::setRootModelIndex）。
 * @details    XGui 无 QModelIndex，以 (row, col) 平铺承载；当前模型
 *             为平铺列表时 (0,0) 即全量根。
 * @param      self 目标控件；传入 NULL 时函数不执行任何操作。
 * @param      row 根行号。
 * @param      col 根列号。
 * @return     无返回值。
 */
void XComboBox_setRootModelIndex(XComboBox* self, int row, int col);
/**
 * @brief      查询根模型索引（对标 QComboBox::rootModelIndex）。
 * @param      self 目标控件；NULL 时输出 0。
 * @param      row 输出根行号；可 NULL 忽略（调用方提供存储）。
 * @param      col 输出根列号；可 NULL 忽略。
 * @return     无返回值。
 */
void XComboBox_rootModelIndex(const XComboBox* self, int* row, int* col);
/**
 * @brief      设置输入校验器（对标 QComboBox::setValidator）。
 * @details    XValidator 体系未建立：validator 以不透明指针承载，
 *             当前版本仅保存状态不参与输入过滤（头文件已注明）。
 * @param      self 目标控件；传入 NULL 时函数不执行任何操作。
 * @param      validator 校验器对象；NULL 清除；仅承载不取得所有权。
 * @return     无返回值。
 */
void XComboBox_setValidator(XComboBox* self, void* validator);
/**
 * @brief      查询输入校验器（对标 QComboBox::validator）。
 * @param      self 目标控件；NULL 返回 NULL。
 * @return     不透明指针；未设置返回 NULL。
 */
void* XComboBox_validator(const XComboBox* self);
/**
 * @brief      设置条目委托（对标 QComboBox::setItemDelegate）。
 * @details    XGui 尚未建立委托（item delegate）类体系：delegate 以
 *             不透明指针承载，当前版本仅保存状态，不参与弹出行绘制
 *             或编辑（头文件已注明）。委托为借用语义，组合框不取得
 *             所有权，调用方负责其生命周期。
 * @param      self 目标控件；传入 NULL 时函数不执行任何操作。
 * @param      delegate 委托对象指针；NULL 清除；仅承载不取得所有权。
 * @return     无返回值。
 */
void XComboBox_setItemDelegate(XComboBox* self, void* delegate);
/**
 * @brief      查询条目委托（对标 QComboBox::itemDelegate）。
 * @details    XGui 委托体系未建：仅返回 setItemDelegate 保存的不透明
 *             指针，未设置时返回 NULL。
 * @param      self 目标控件；NULL 返回 NULL。
 * @return     不透明指针；未设置返回 NULL；借用语义，不得释放。
 */
void* XComboBox_itemDelegate(const XComboBox* self);
/**
 * @brief      输入法查询（对标 QComboBox::inputMethodQuery，简化承载）。
 * @details    当前仅支持返回编辑文本类查询：可编辑模式返回行编辑框
 *             内容，只读模式返回当前项文本；其余查询返回空文本。
 * @param      self 目标控件；NULL 返回 NULL。
 * @param      query 查询类别（Qt::InputMethodQuery 数值）。
 * @return     新建 XString*（空文本也返回对象）；调用方负责
 *             XString_delete_base 释放。
 */
XString* XComboBox_inputMethodQuery(XComboBox* self, int query);
/** @brief XCombo盒current索引（对标 Qt 同名接口）。
 * @param self 目标控件指针。
 * @return 返回对应数值；无效时返回 0 或 -1（视接口语义）。
 */
int XComboBox_currentIndex(const XComboBox* self);
/** @brief 读取当前项文本（返回新建 XString*，调用方负责 delete_base；对标 QComboBox::currentText）。
 * @param self 目标控件指针。
 * @return 新建 XString*；无当前项时返回空 XString*。
 */
XString* XComboBox_currentText(const XComboBox* self);
/** @brief 读取当前项文本（UTF-8 借用；对标 QComboBox::currentText）。
 * @param self 目标控件指针。
 * @return 内部 UTF-8 借用指针；无当前项时返回空串，不得释放或修改。
 */
const char* XComboBox_currentText_2(const XComboBox* self);
/** @brief 读取指定项文本（返回新建 XString*，调用方负责 delete_base；对标 QComboBox::itemText）。
 * @param self 目标控件指针。
 * @param index 索引（0 起）。
 * @return 新建 XString*；参数无效时返回 NULL。
 */
XString* XComboBox_itemText(const XComboBox* self, int index);
/** @brief 读取指定项文本（UTF-8 借用；对标 QComboBox::itemText）。
 * @param self 目标控件指针。
 * @param index 索引（0 起）。
 * @return 内部 UTF-8 借用指针；参数无效时返回空串，不得释放或修改。
 */
const char* XComboBox_itemText_2(const XComboBox* self, int index);
/** @brief 查找文本所在项（XString 主版本；对标 QComboBox::findText）。
 * @param self 目标控件指针。
 * @param text 借用 XString*；不能为 NULL。
 * @return 匹配项索引；未命中或参数无效时返回 -1。
 */
int XComboBox_findText(const XComboBox* self, const XString* text);
/** @brief 查找文本所在项（UTF-8 兼容重载；对标 QComboBox::findText）。
 * @param self 目标控件指针。
 * @param text UTF-8 文本；不能为 NULL。
 * @return 匹配项索引；未命中或参数无效时返回 -1。
 */
int XComboBox_findText_2(const XComboBox* self, const char* text);
/** @brief 在指定索引插入项（XString 主版本；对标 QComboBox::insertItem）。
 * @param self 目标控件指针。
 * @param index 索引（0 起，负数插最前、超出追加）。
 * @param text 借用 XString*；不能为 NULL。
 * @return 无返回值。
 */
void XComboBox_insertItem(XComboBox* self, int index, const XString* text);
/** @brief 在指定索引插入项（UTF-8 兼容重载，转发主版本）。
 * @param self 目标控件指针。
 * @param index 索引（0 起，负数插最前、超出追加）。
 * @param text UTF-8 文本；不能为 NULL。
 * @return 无返回值。
 */
void XComboBox_insertItem_2(XComboBox* self, int index, const char* text);
/** @brief 在指定索引批量插入项（XStringList 主版本；对标 QComboBox::insertItems）。
 * @param self 目标控件指针。
 * @param index 索引（0 起）。
 * @param texts 借用 XStringList*（元素为 XString*）；不能为 NULL。
 * @return 无返回值。
 */
void XComboBox_insertItems(XComboBox* self, int index, const XStringList* texts);
/** @brief 在指定索引批量插入项（UTF-8 兼容重载，转发主版本）。
 * @param self 目标控件指针。
 * @param index 索引（0 起）。
 * @param texts NULL 结尾的 UTF-8 字符串数组；不能为 NULL。
 * @return 无返回值。
 */
void XComboBox_insertItems_2(XComboBox* self, int index, const char* const* texts);
/** @brief 尾部追加项（XString 主版本；对标 QComboBox::addItem）。
 * @param self 目标控件指针。
 * @param text 借用 XString*；不能为 NULL。
 * @return 无返回值。
 */
void XComboBox_addItem(XComboBox* self, const XString* text);
/** @brief 尾部追加项（UTF-8 兼容重载，转发主版本）。
 * @param self 目标控件指针。
 * @param text UTF-8 文本；不能为 NULL。
 * @return 无返回值。
 */
void XComboBox_addItem_2(XComboBox* self, const char* text);
/** @brief 批量追加项（XStringList 主版本；对标 QComboBox::addItems）。
 * @param self 目标控件指针。
 * @param texts 借用 XStringList*（元素为 XString*）；不能为 NULL。
 * @return 无返回值。
 */
void XComboBox_addItems(XComboBox* self, const XStringList* texts);
/** @brief 批量追加项（UTF-8 兼容重载，转发主版本）。
 * @param self 目标控件指针。
 * @param texts NULL 结尾的 UTF-8 字符串数组；不能为 NULL。
 * @return 无返回值。
 */
void XComboBox_addItems_2(XComboBox* self, const char* const* texts);
/** @brief XCombo盒insertSeparator（对标 Qt 同名接口）。
 * @param self 目标控件指针。
 * @param index 索引（0 起）。
 * @return 无返回值。
 */
void XComboBox_insertSeparator(XComboBox* self, int index);
/** @brief XCombo盒remove项（对标 Qt 同名接口）。
 * @param self 目标控件指针。
 * @param index 索引（0 起）。
 * @return 无返回值。
 */
void XComboBox_removeItem(XComboBox* self, int index);
/** @brief 设置指定项文本（XString 主版本；对标 QComboBox::setItemText）。
 * @param self 目标控件指针。
 * @param index 索引（0 起）。
 * @param text 借用 XString*；不能为 NULL。
 * @return 无返回值。
 */
void XComboBox_setItemText(XComboBox* self, int index, const XString* text);
/** @brief 设置指定项文本（UTF-8 兼容重载，转发主版本）。
 * @param self 目标控件指针。
 * @param index 索引（0 起）。
 * @param text UTF-8 文本；不能为 NULL。
 * @return 无返回值。
 */
void XComboBox_setItemText_2(XComboBox* self, int index, const char* text);
/** @brief XCombo盒clear（对标 Qt 同名接口）。
 * @param self 目标控件指针。
 * @return 无返回值。
 */
void XComboBox_clear(XComboBox* self);

/* ==================== 弹出与选择 ==================== */

void XComboBox_showPopup_base(XComboBox* self);
/** @brief XCombo盒hidePopupbase（对标 Qt 同名接口）。
 * @param self 目标控件指针。
 * @return 无返回值。
 */
void XComboBox_hidePopup_base(XComboBox* self);
/** @brief XCombo盒popup可见（对标 Qt 同名接口）。
 * @param self 目标控件指针。
 * @return 条件成立返回 true，否则返回 false。
 */
bool XComboBox_popupVisible(const XComboBox* self);
/** @brief XCombo盒set当前索引（对标 Qt 同名接口）。
 * @param self 目标控件指针。
 * @param index 索引（0 起）。
 * @return 无返回值。
 */
void XComboBox_setCurrentIndex(XComboBox* self, int index);
/** @brief 设置当前文本（XString 主版本；对标 QComboBox::setCurrentText）。
 * @param self 目标控件指针。
 * @param text 借用 XString*；不能为 NULL。
 * @return 无返回值。
 */
void XComboBox_setCurrentText(XComboBox* self, const XString* text);
/** @brief 设置当前文本（UTF-8 兼容重载，转发主版本）。
 * @param self 目标控件指针。
 * @param text UTF-8 文本；不能为 NULL。
 * @return 无返回值。
 */
void XComboBox_setCurrentText_2(XComboBox* self, const char* text);
/** @brief XCombo盒clearEdit文本（对标 Qt 同名接口）。
 * @param self 目标控件指针。
 * @return 无返回值。
 */
void XComboBox_clearEditText(XComboBox* self);
/** @brief 设置可编辑框文本（XString 主版本；对标 QComboBox::setEditText）。
 * @param self 目标控件指针。
 * @param text 借用 XString*；不能为 NULL。
 * @return 无返回值。
 */
void XComboBox_setEditText(XComboBox* self, const XString* text);
/** @brief 设置可编辑框文本（UTF-8 兼容重载，转发主版本）。
 * @param self 目标控件指针。
 * @param text UTF-8 文本；不能为 NULL。
 * @return 无返回值。
 */
void XComboBox_setEditText_2(XComboBox* self, const char* text);
/** @brief 设置项图标路径（XString 主版本；对标 QComboBox::setItemIcon 的路径简化）。
 * @param self 目标控件。
 * @param index 项索引。
 * @param path 借用 XString*；可为 NULL（清除）。
 * @return 无返回值。
 */
void XComboBox_setItemIcon(XComboBox* self, int index, const XString* path);
/** @brief 设置项图标路径（UTF-8 兼容重载）。 */
void XComboBox_setItemIcon_2(XComboBox* self, int index, const char* path);
/** @brief 读取项图标路径（内部借用 XString*；不得释放）。 */
const XString* XComboBox_itemIcon(const XComboBox* self, int index);
/** @brief 读取项图标路径（UTF-8 借用）。 */
const char* XComboBox_itemIcon_2(const XComboBox* self, int index);
/** @brief 设置项数据（XString 主版本；对标 QComboBox::setItemData 的字符串简化）。
 * @param self 目标控件。
 * @param index 项索引。
 * @param data 借用 XString*；可为 NULL（清除）。
 * @return 无返回值。
 */
void XComboBox_setItemData(XComboBox* self, int index, const XString* data);
/** @brief 设置项数据（UTF-8 兼容重载）。 */
void XComboBox_setItemData_2(XComboBox* self, int index, const char* data);
/** @brief 读取项数据（内部借用 XString*；不得释放）。
 * @param self 目标控件。
 * @param index 项索引。
 * @return 借用 XString*；未设置返回 NULL。
 */
const XString* XComboBox_itemData(const XComboBox* self, int index);
/** @brief 读取项数据（UTF-8 借用）。 */
const char* XComboBox_itemData_2(const XComboBox* self, int index);
/** @brief 读取当前项数据（对标 QComboBox::currentData）。
 * @details 等价于 itemData(currentIndex())。
 * @param self 目标控件；传入 NULL 时返回 NULL。
 * @return 借用内部 XString 指针；未设置或当前项无效返回 NULL；
 *         禁止释放或修改。
 */
const XString* XComboBox_currentData(const XComboBox* self);
/** @brief 按数据查找项（XString 主版本；对标 QComboBox::findData）。
 * @param self 目标控件。
 * @param data 借用 XString*；不能为 NULL。
 * @return 项索引；未命中 -1。
 */
int XComboBox_findData(const XComboBox* self, const XString* data);
/** @brief 按数据查找项（UTF-8 兼容重载）。 */
int XComboBox_findData_2(const XComboBox* self, const char* data);
/** @brief 设置补全器（对标 QComboBox::setCompleter；借用，不拥有）。
 * @param self 目标控件。
 * @param completer 补全器借用指针；可为 NULL（清除）。
 * @return 无返回值。
 */
void XComboBox_setCompleter(XComboBox* self, XCompleter* completer);
/** @brief 查询补全器。 @param self 目标控件。 @return 借用指针。 */
XCompleter* XComboBox_completer(const XComboBox* self);
/**
 * @brief      设置前缀补全过滤开关（对标 QComboBox 可编辑内建 completer
 *             的 PopupCompletion 子集；本子集仅此一种补全模式）。
 * @details    可编辑模式下输入前缀时，下拉弹层自动按前缀过滤条目并
 *             高亮命中项（大小写不敏感，对标 QCompleter 默认
 *             CaseInsensitive）：Enter 采纳高亮补全并发射既有
 *             activated/textActivated；Esc 收起弹层且不改变编辑文本；
 *             Up/Down/PageUp/PageDown 在命中行间移动高亮。无匹配时不
 *             弹层（空列表同）。对非可编辑组合框仅保存开关，转为可编
 *             辑后生效；关闭开关时存活的补全弹层立即收起（不改文本）。
 * @param      self 目标控件指针；传入 NULL 时函数不执行任何操作。
 * @param      enable bool 开关：true 启用前缀补全过滤。
 * @return     无返回值。
 */
void XComboBox_setCompleterMode(XComboBox* self, bool enable);
/**
 * @brief      查询前缀补全过滤开关（对标 completer 存在性语义）。
 * @param      self 目标控件指针。
 * @return     已启用返回 true；self 为 NULL 或未启用返回 false。
 */
bool XComboBox_isCompleterMode(const XComboBox* self);
/** @brief 设置图标尺寸（对标 setIconSize 的方边简化）。
 * @param self 目标控件。
 * @param size 方边像素（>0）。
 * @return 无返回值。
 */
void XComboBox_setIconSize(XComboBox* self, int size);
/** @brief 查询图标尺寸。 @param self 目标控件。 @return 方边像素。 */
int XComboBox_iconSize(const XComboBox* self);

/* ==================== 信号 ==================== */

void* XComboBox_activated_signal(XComboBox* self, int index);
/** @brief XCombo盒text激活 信号地址（发射经 XObject_emitSignal）。
 * @param self 目标控件指针。
 * @return 返回对象指针；无效时返回 NULL。
 */
void* XComboBox_textActivated_signal(XComboBox* self, const char* text);
/** @brief XCombo盒highlighted 信号地址（发射经 XObject_emitSignal）。
 * @param self 目标控件指针。
 * @return 返回对象指针；无效时返回 NULL。
 */
void* XComboBox_highlighted_signal(XComboBox* self, int index);
/** @brief XCombo盒textHighlighted 信号地址（发射经 XObject_emitSignal）。
 * @param self 目标控件指针。
 * @return 返回对象指针；无效时返回 NULL。
 */
void* XComboBox_textHighlighted_signal(XComboBox* self, const char* text);
/** @brief XCombo盒current索引变更 信号地址（发射经 XObject_emitSignal）。
 * @param self 目标控件指针。
 * @return 返回对象指针；无效时返回 NULL。
 */
void* XComboBox_currentIndexChanged_signal(XComboBox* self, int index);
/** @brief XCombo盒current文本变更 信号地址（发射经 XObject_emitSignal）。
 * @param self 目标控件指针。
 * @return 返回对象指针；无效时返回 NULL。
 */
void* XComboBox_currentTextChanged_signal(XComboBox* self, const char* text);
/** @brief XCombo盒edit文本变更 信号地址（发射经 XObject_emitSignal）。
 * @param self 目标控件指针。
 * @return 返回对象指针；无效时返回 NULL。
 */
void* XComboBox_editTextChanged_signal(XComboBox* self, const char* text);
/** @brief XCombo盒popupShown 信号地址（发射经 XObject_emitSignal）。
 * @param self 目标控件指针。
 * @return 返回对象指针；无效时返回 NULL。
 */
void* XComboBox_popupShown_signal(XComboBox* self);
/** @brief XCombo盒popupHidden 信号地址（发射经 XObject_emitSignal）。
 * @param self 目标控件指针。
 * @return 返回对象指针；无效时返回 NULL。
 */
void* XComboBox_popupHidden_signal(XComboBox* self);

#ifdef __cplusplus
}
#endif
#endif /* XWIDGET_ON && XCOMBOBOX_ON && XLINEEDIT_ON */

#endif /* XCOMBOBOX_H */
