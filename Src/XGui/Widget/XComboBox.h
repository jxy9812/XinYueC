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
#include "XLineEdit.h"

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
typedef struct XComboBox
{
    XWidget m_base;                    /**< 基类成员；必须是第一个。 */
    char**  m_items;                   /**< 项文本数组（每项拥有）。 */
    int     m_itemCount;               /**< 当前项数。 */
    int     m_itemCapacity;            /**< 数组容量。 */
    int     m_currentIndex;            /**< 当前项索引（-1 无）。 */
    int     m_maxCount;                /**< 最大项数（默认 2147483647 截为可用上限）。 */
    int     m_maxVisibleItems;         /**< 弹出最大可见项数（默认 10）。 */
    bool    m_duplicatesEnabled;       /**< 允许重复项。 */
    bool    m_editable;                /**< 可编辑模式。 */
    XLineEdit* m_lineEdit;             /**< 可编辑模式的内嵌编辑框（拥有）。 */
    int     m_insertPolicy;            /**< 插入策略。 */
    int     m_sizeAdjustPolicy;        /**< 尺寸自适应策略。 */
    int     m_minimumContentsLength;   /**< 最小内容字符数。 */
    bool    m_frame;                   /**< 边框开关（默认 true）。 */
    char    m_placeholderText[64];     /**< 占位文本。 */
    bool    m_popupVisible;            /**< 弹出可见（内部）。 */
    int     m_savedHeight;             /**< 弹出前高度（展开/收起恢复）。 */
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
const char* XComboBox_placeholderText(const XComboBox* self);
/** @brief XCombo盒set占位文本（对标 Qt 同名接口）。
 * @param self 目标控件指针。
 * @param placeholderText const char 参数。
 * @return 无返回值。
 */
void XComboBox_setPlaceholderText(XComboBox* self, const char* placeholderText);
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
/** @brief XCombo盒current索引（对标 Qt 同名接口）。
 * @param self 目标控件指针。
 * @return 返回对应数值；无效时返回 0 或 -1（视接口语义）。
 */
int XComboBox_currentIndex(const XComboBox* self);
/** @brief XCombo盒current文本（对标 Qt 同名接口）。
 * @param self 目标控件指针。
 * @return 返回 UTF-8 文本；无效时返回空串。
 */
const char* XComboBox_currentText(const XComboBox* self);
/** @brief XCombo盒item文本（对标 Qt 同名接口）。
 * @param self 目标控件指针。
 * @param index 索引（0 起）。
 * @return 返回 UTF-8 文本；无效时返回空串。
 */
const char* XComboBox_itemText(const XComboBox* self, int index);
/** @brief XCombo盒find文本（对标 Qt 同名接口）。
 * @param self 目标控件指针。
 * @param text UTF-8 文本。
 * @return 返回对应数值；无效时返回 0 或 -1（视接口语义）。
 */
int XComboBox_findText(const XComboBox* self, const char* text);
/** @brief XCombo盒insert项（对标 Qt 同名接口）。
 * @param self 目标控件指针。
 * @param index 索引（0 起）。
 * @param text UTF-8 文本。
 * @return 无返回值。
 */
void XComboBox_insertItem(XComboBox* self, int index, const char* text);
/** @brief XCombo盒insertItems（对标 Qt 同名接口）。
 * @param self 目标控件指针。
 * @param index 索引（0 起）。
 * @param texts const char* const 参数。
 * @return 无返回值。
 */
void XComboBox_insertItems(XComboBox* self, int index, const char* const* texts);
/** @brief XCombo盒add项（对标 Qt 同名接口）。
 * @param self 目标控件指针。
 * @param text UTF-8 文本。
 * @return 无返回值。
 */
void XComboBox_addItem(XComboBox* self, const char* text);
/** @brief XCombo盒addItems（对标 Qt 同名接口）。
 * @param self 目标控件指针。
 * @param texts const char* const 参数。
 * @return 无返回值。
 */
void XComboBox_addItems(XComboBox* self, const char* const* texts);
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
/** @brief XCombo盒set项文本（对标 Qt 同名接口）。
 * @param self 目标控件指针。
 * @param index 索引（0 起）。
 * @param text UTF-8 文本。
 * @return 无返回值。
 */
void XComboBox_setItemText(XComboBox* self, int index, const char* text);
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
/** @brief XCombo盒set当前文本（对标 Qt 同名接口）。
 * @param self 目标控件指针。
 * @param text UTF-8 文本。
 * @return 无返回值。
 */
void XComboBox_setCurrentText(XComboBox* self, const char* text);
/** @brief XCombo盒clearEdit文本（对标 Qt 同名接口）。
 * @param self 目标控件指针。
 * @return 无返回值。
 */
void XComboBox_clearEditText(XComboBox* self);
/** @brief XCombo盒setEdit文本（对标 Qt 同名接口）。
 * @param self 目标控件指针。
 * @param text UTF-8 文本。
 * @return 无返回值。
 */
void XComboBox_setEditText(XComboBox* self, const char* text);

/* ==================== 信号 ==================== */

void* XComboBox_activated_signal(XComboBox* self);
/** @brief XCombo盒text激活 信号地址（发射经 XObject_emitSignal）。
 * @param self 目标控件指针。
 * @return 返回对象指针；无效时返回 NULL。
 */
void* XComboBox_textActivated_signal(XComboBox* self);
/** @brief XCombo盒highlighted 信号地址（发射经 XObject_emitSignal）。
 * @param self 目标控件指针。
 * @return 返回对象指针；无效时返回 NULL。
 */
void* XComboBox_highlighted_signal(XComboBox* self);
/** @brief XCombo盒textHighlighted 信号地址（发射经 XObject_emitSignal）。
 * @param self 目标控件指针。
 * @return 返回对象指针；无效时返回 NULL。
 */
void* XComboBox_textHighlighted_signal(XComboBox* self);
/** @brief XCombo盒current索引变更 信号地址（发射经 XObject_emitSignal）。
 * @param self 目标控件指针。
 * @return 返回对象指针；无效时返回 NULL。
 */
void* XComboBox_currentIndexChanged_signal(XComboBox* self);
/** @brief XCombo盒current文本变更 信号地址（发射经 XObject_emitSignal）。
 * @param self 目标控件指针。
 * @return 返回对象指针；无效时返回 NULL。
 */
void* XComboBox_currentTextChanged_signal(XComboBox* self);
/** @brief XCombo盒edit文本变更 信号地址（发射经 XObject_emitSignal）。
 * @param self 目标控件指针。
 * @return 返回对象指针；无效时返回 NULL。
 */
void* XComboBox_editTextChanged_signal(XComboBox* self);
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

#ifdef __cplusplus
}
#endif
/** @brief XCombo盒find数据（对标 Qt 同名接口）。
 * @param self 目标控件指针。
 * @param data const char 参数。
 * @return 返回对应数值；无效时返回 0 或 -1（视接口语义）。
 */
int XComboBox_findData(const XComboBox* self, const char* data);
/** @brief XCombo盒set项图标（对标 Qt 同名接口）。
 * @param self 目标控件指针。
 * @param index 索引（0 起）。
 * @param icon 图标路径。
 * @return 无返回值。
 */
void XComboBox_setItemIcon(XComboBox* self, int index, const char* icon);
/** @brief XCombo盒set项数据（对标 Qt 同名接口）。
 * @param self 目标控件指针。
 * @param index 索引（0 起）。
 * @param data const char 参数。
 * @return 无返回值。
 */
void XComboBox_setItemData(XComboBox* self, int index, const char* data);
/** @brief XCombo盒item数据（对标 Qt 同名接口）。
 * @param self 目标控件指针。
 * @param index 索引（0 起）。
 * @return 返回 UTF-8 文本；无效时返回空串。
 */
const char* XComboBox_itemData(const XComboBox* self, int index);
/** @brief XCombo盒showPopup2（对标 Qt 同名接口）。
 * @param self 目标控件指针。
 * @return 无返回值。
 */
void XComboBox_showPopup_2(XComboBox* self);
/** @brief XCombo盒hidePopup2（对标 Qt 同名接口）。
 * @param self 目标控件指针。
 * @return 无返回值。
 */
void XComboBox_hidePopup_2(XComboBox* self);
/** @brief XCombo盒setCompleter（对标 Qt 同名接口）。
 * @param self 目标控件指针。
 * @param completer 补全器指针。
 * @return 无返回值。
 */
void XComboBox_setCompleter(XComboBox* self, void* completer);
/** @brief XCombo盒set项文本2（对标 Qt 同名接口）。
 * @param self 目标控件指针。
 * @return 无返回值。
 */
void XComboBox_setItemText_2(XComboBox* self);
/** @brief XCombo盒max数量2（对标 Qt 同名接口）。
 * @param self 目标控件指针。
 * @return 无返回值。
 */
void XComboBox_maxCount_2(XComboBox* self);
/** @brief XCombo盒setMax数量2（对标 Qt 同名接口）。
 * @param self 目标控件指针。
 * @return 无返回值。
 */
void XComboBox_setMaxCount_2(XComboBox* self);
/** @brief XCombo盒set插入策略2（对标 Qt 同名接口）。
 * @param self 目标控件指针。
 * @return 无返回值。
 */
void XComboBox_setInsertPolicy_2(XComboBox* self);
/** @brief XCombo盒insert策略2（对标 Qt 同名接口）。
 * @param self 目标控件指针。
 * @return 无返回值。
 */
void XComboBox_insertPolicy_2(XComboBox* self);
/** @brief XCombo盒set尺寸Adjust策略2（对标 Qt 同名接口）。
 * @param self 目标控件指针。
 * @return 无返回值。
 */
void XComboBox_setSizeAdjustPolicy_2(XComboBox* self);
/** @brief XCombo盒sizeAdjust策略2（对标 Qt 同名接口）。
 * @param self 目标控件指针。
 * @return 无返回值。
 */
void XComboBox_sizeAdjustPolicy_2(XComboBox* self);
/** @brief XCombo盒set图标尺寸3（对标 Qt 同名接口）。
 * @param self 目标控件指针。
 * @return 无返回值。
 */
void XComboBox_setIconSize_3(XComboBox* self);
/** @brief XCombo盒icon尺寸2（对标 Qt 同名接口）。
 * @param self 目标控件指针。
 * @return 无返回值。
 */
void XComboBox_iconSize_2(XComboBox* self);
#endif /* XCOMBOBOX_H */
