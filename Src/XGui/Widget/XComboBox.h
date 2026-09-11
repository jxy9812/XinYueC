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
void XComboBox_init(XComboBox* self, XWidget* parent, XWidgetFlags flags);
#define XComboBox_create(parent, flags) XComboBox_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, (parent), (flags))
XComboBox* XComboBox_create_ex(XMemoryType memory, XWidget* parent, XWidgetFlags flags);
#define XComboBox_deinit_base(self) XWidget_deinit_base((XWidget*)(self))
#define XComboBox_delete_base(self) XWidget_delete_base((XWidget*)(self))

/* ==================== 项管理（对标 QComboBox public API） ==================== */

int XComboBox_count(const XComboBox* self);
int XComboBox_maxCount(const XComboBox* self);
void XComboBox_setMaxCount(XComboBox* self, int max);
int XComboBox_maxVisibleItems(const XComboBox* self);
void XComboBox_setMaxVisibleItems(XComboBox* self, int maxItems);
bool XComboBox_duplicatesEnabled(const XComboBox* self);
void XComboBox_setDuplicatesEnabled(XComboBox* self, bool enable);
void XComboBox_setFrame(XComboBox* self, bool on);
bool XComboBox_hasFrame(const XComboBox* self);
int XComboBox_insertPolicy(const XComboBox* self);
void XComboBox_setInsertPolicy(XComboBox* self, int policy);
int XComboBox_sizeAdjustPolicy(const XComboBox* self);
void XComboBox_setSizeAdjustPolicy(XComboBox* self, int policy);
int XComboBox_minimumContentsLength(const XComboBox* self);
void XComboBox_setMinimumContentsLength(XComboBox* self, int characters);
const char* XComboBox_placeholderText(const XComboBox* self);
void XComboBox_setPlaceholderText(XComboBox* self, const char* placeholderText);
bool XComboBox_isEditable(const XComboBox* self);
void XComboBox_setEditable(XComboBox* self, bool editable);
XLineEdit* XComboBox_lineEdit(const XComboBox* self);
int XComboBox_currentIndex(const XComboBox* self);
const char* XComboBox_currentText(const XComboBox* self);
const char* XComboBox_itemText(const XComboBox* self, int index);
int XComboBox_findText(const XComboBox* self, const char* text);
void XComboBox_insertItem(XComboBox* self, int index, const char* text);
void XComboBox_insertItems(XComboBox* self, int index, const char* const* texts);
void XComboBox_addItem(XComboBox* self, const char* text);
void XComboBox_addItems(XComboBox* self, const char* const* texts);
void XComboBox_insertSeparator(XComboBox* self, int index);
void XComboBox_removeItem(XComboBox* self, int index);
void XComboBox_setItemText(XComboBox* self, int index, const char* text);
void XComboBox_clear(XComboBox* self);

/* ==================== 弹出与选择 ==================== */

void XComboBox_showPopup_base(XComboBox* self);
void XComboBox_hidePopup_base(XComboBox* self);
bool XComboBox_popupVisible(const XComboBox* self);
void XComboBox_setCurrentIndex(XComboBox* self, int index);
void XComboBox_setCurrentText(XComboBox* self, const char* text);
void XComboBox_clearEditText(XComboBox* self);
void XComboBox_setEditText(XComboBox* self, const char* text);

/* ==================== 信号 ==================== */

void* XComboBox_activated_signal(XComboBox* self);
void* XComboBox_textActivated_signal(XComboBox* self);
void* XComboBox_highlighted_signal(XComboBox* self);
void* XComboBox_textHighlighted_signal(XComboBox* self);
void* XComboBox_currentIndexChanged_signal(XComboBox* self);
void* XComboBox_currentTextChanged_signal(XComboBox* self);
void* XComboBox_editTextChanged_signal(XComboBox* self);
void* XComboBox_popupShown_signal(XComboBox* self);
void* XComboBox_popupHidden_signal(XComboBox* self);

#ifdef __cplusplus
}
#endif
#endif /* XWIDGET_ON && XCOMBOBOX_ON && XLINEEDIT_ON */

#ifdef __cplusplus
}
#endif
int XComboBox_findData(const XComboBox* self, const char* data);
void XComboBox_setItemIcon(XComboBox* self, int index, const char* icon);
void XComboBox_setItemData(XComboBox* self, int index, const char* data);
const char* XComboBox_itemData(const XComboBox* self, int index);
void XComboBox_showPopup_2(XComboBox* self);
void XComboBox_hidePopup_2(XComboBox* self);
void XComboBox_setCompleter(XComboBox* self, void* completer);
#endif /* XCOMBOBOX_H */
