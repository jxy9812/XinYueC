/**
 * @file       XComboBox.h
 * @brief      XComboBox 下拉组合框控件（对标 Qt 6.8 QComboBox）。
 * @details    文本项下拉选择框：
 *             - 项管理：addItem/insertItem/insertItems/removeItem/
 *               setItemText/insertSeparator/clear、count/currentIndex/
 *               currentText/itemText、maxCount（超限丢弃）/maxVisibleItems/
 *               duplicatesEnabled；addItem/insertItem 提供
 *               (icon,text,userData) 与 (text,userData) 重载（对标
 *               QComboBox 同名重载）；insertSeparator 插 Qt 口径分隔条
 *               （空文本 + 不可选标记，弹层绘横线，见 isItemSeparator）；
 *               findText/findData 提供 Qt::MatchFlags 版本（_3 重载）；
 *             - 枚举：InsertPolicy（NoInsert=0..InsertAlphabetically=6，
 *               数值对齐 Qt）、SizeAdjustPolicy（AdjustToContents=0/
 *               AdjustToContentsOnFirstShow=1/AdjustToMinimumContents
 *               LengthWithIcon=2，数值对齐 Qt）——缺省
 *               AdjustToContentsOnFirstShow（Qt 同，qcombobox_p.h:396）：
 *               尺寸提示带缓存（P0-04 对标 Qt d->sizeHint），**首次显示
 *               后冻结**，内容变化不再自动变宽；运行期 clear+重填条目的
 *               组合框应显式 setSizeAdjustPolicy(AdjustToContents)（见
 *               setSizeAdjustPolicy 注记）；
 *             - 补全器：setCompleter 转发内嵌行编辑（对标 QComboBox::
 *               setCompleter，非可编辑直接忽略；补全器 activated 联动
 *               置当前项并发 activated 对）；completerMode 降级承载
 *               「无 completer 时的内建前缀过滤」（见 setCompleter/
 *               setCompleterMode 注记）；
 *             - 弹出：showPopup/hidePopup（对标 Qt public virtual；内部经
 *               独立顶层 XListView 弹层承载，行激活发射 activated 并收起）；
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
 *               条目（AtCurrent/AfterCurrent/BeforeCurrent 无当前项
 *               时不动作，对标 Qt），InsertAtCurrent 以编辑文本替换
 *               当前项文本，InsertAlphabetically 大小写不敏感按
 *               UTF-8 字节序（等价码点序，中文参与排序）；用户
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

#if XGUI_ON
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

/**
 * @brief 查找匹配标志（对标 Qt 6.8 Qt::MatchFlag，数值逐位一致）。
 * @details 取值来源 /usr/include/x86_64-linux-gnu/qt6/QtCore/qnamespace.h
 *          :1530-1542（Qt 6.8.0 实测）。`XComboBox_findText_3` /
 *          `XComboBox_findData_3` 消费本组标志。
 *          **Qt 6.8 实测行为**（本机 Qt 6.8.0 + offscreen 探针）：
 *          - `MatchExactly` 走 QVariant/QString 相等比较，**恒大小写敏感**
 *            （`Qt::MatchExactly` 不带 `MatchCaseSensitive` 时 "RED"
 *            仍不命中 "red"，探针 findData= -1）；
 *          - `MatchContains`/`MatchStartsWith`/`MatchEndsWith` 缺省
 *            大小写不敏感，带 `MatchCaseSensitive` 后敏感；
 *          - `MatchRegularExpression`/`MatchWildcard` 本库无正则/通配
 *            引擎（Qt 的 QRegularExpression / QRegExp 体系未建），实现
 *            返回 -1（不静默当归一字符串比较）。
 */
typedef enum XComboBoxMatchFlag
{
    XComboBoxMatchExactly = 0,            /**< 完全相等（对标 MatchExactly）。 */
    XComboBoxMatchContains = 1,           /**< 包含（对标 MatchContains）。 */
    XComboBoxMatchStartsWith = 2,         /**< 前缀（对标 MatchStartsWith）。 */
    XComboBoxMatchEndsWith = 3,           /**< 后缀（对标 MatchEndsWith）。 */
    XComboBoxMatchRegularExpression = 4,  /**< 正则（无引擎，返回 -1；对标 MatchRegularExpression）。 */
    XComboBoxMatchWildcard = 5,           /**< 通配（无引擎，返回 -1；对标 MatchWildcard）。 */
    XComboBoxMatchFixedString = 8,        /**< 固定字符串（对标 MatchFixedString）。 */
    XComboBoxMatchTypeMask = 0x0F,        /**< 匹配类型掩码（对标 MatchTypeMask）。 */
    XComboBoxMatchCaseSensitive = 16,     /**< 大小写敏感（对标 MatchCaseSensitive）。 */
    XComboBoxMatchWrap = 32,              /**< 环绕（对标 MatchWrap；findText/findData 不消费）。 */
    XComboBoxMatchRecursive = 64          /**< 递归（对标 MatchRecursive；平铺模型不消费）。 */
} XComboBoxMatchFlag;

/* ==================== 类定义 ==================== */
XCLASS_DEFINE_BEGING(XComboBox)
/** @brief 弹出层宽度虚槽（对标 QFontComboBox 在 resizeEvent 里把容器宽
 *         设为 `min(width*5/3, 屏宽)` 的定制点，qfontcombobox.cpp:526-537）。
 *         Qt 无同名公共 API，C 侧以本类自有虚槽承载：XFontComboBox 在
 *         class_init 重载该槽即可改变弹层宽，其余组合框保持「宽同控件」。 */
XCLASS_DEFINE_ENUM(XComboBox, PopupWidth) = XCLASS_VTABLE_GET_SIZE(XWidget),
XCLASS_DEFINE_END(XComboBox)

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
    XImage**  m_itemImages;            /**< 项图标解码缓存（平行；对象拥有；
                                            P3-56：按 iconSize 预缩放后的
                                            XImage，闭态/弹层装饰绘制消费；
                                            懒解码，路径/尺寸变化即失效。 */
    bool*    m_itemSeparators;         /**< 项分隔条标记数组（平行；对象拥有；
                                            对标 Qt 的
                                            `itemData(i, Qt::AccessibleDescriptionRole)
                                            == "separator"` 判据）。 */
    XLineEdit* m_lineEdit;             /**< 可编辑模式的内嵌编辑框（拥有）。 */
    XListView* m_popupView;            /**< 弹出列表视图（对象拥有；懒创建；对标 view）。 */
    XAbstractItemModel* m_model;       /**< 条目数据模型（内部懒创建的归本控件；外部
                                            setModel 安装的为借用）。 */
    XString* m_placeholderText;        /**< 字符串字段（对象拥有）。 */
    void*   m_validator;               /**< 校验器不透明指针（XValidator 体系未建，仅承载）。 */
    XTimerId m_grabTimer;              /**< 弹出后延迟执行平台鼠标抓取的定时器；无效时为
                                            XTIMER_INVALID_ID，仅供内部使用。 */
    int     m_itemCount;               /**< 当前项数。 */
    int     m_itemCapacity;            /**< 数组容量。 */
    int     m_currentIndex;            /**< 当前项索引（-1 无）。 */
    int     m_maxCount;                /**< 最大项数（默认 2147483647 截为可用上限）。 */
    int     m_maxVisibleItems;         /**< 弹出最大可见项数（默认 10）。 */
    int     m_minimumContentsLength;   /**< 最小内容字符数。 */
    int     m_modelColumn;             /**< 模型显示列（对标 modelColumn；setter 原样保存）。 */
    int     m_rootRow;                 /**< 根索引行（Qt QModelIndex 的平铺简化承载）。 */
    int     m_rootCol;                 /**< 根索引列。 */
    int     m_savedHeight;             /**< 弹出前高度（展开/收起恢复）。 */
    int16_t m_completionRow;           /**< 补全弹层当前高亮行（内部；-1 无）。 */
    int16_t m_iconSize;                /**< 图标尺寸（方边像素；默认 16）。 */
    uint8_t m_insertPolicy;            /**< 插入策略（XComboBoxInsertPolicy 0..6）。 */
    uint8_t m_sizeAdjustPolicy;        /**< 尺寸自适应策略（XComboBoxSizeAdjustPolicy 0..2）。 */
    uint8_t m_duplicatesEnabled : 1;   /**< 允许重复项。 */
    uint8_t m_editable : 1;            /**< 可编辑模式。 */
    uint8_t m_completerMode : 1;       /**< 可编辑时启用前缀补全过滤（对标内建
                                            completer 的 PopupCompletion 子集）。 */
    uint8_t m_completionActive : 1;    /**< 弹层当前处于补全过滤态（内部）。 */
    uint8_t m_frame : 1;               /**< 边框开关（默认 true）。 */
    uint8_t m_popupVisible : 1;        /**< 弹出可见（内部）。 */
    uint8_t m_modelOwned : 1;          /**< m_model 归本控件拥有（内部懒创建＝真；
                                            外部 setModel 安装＝假，对标 Qt
                                            「仅删除 parent 为本控件的旧模型」）。 */
    uint8_t m_shownOnce : 1;           /**< 已首次显示（内部；对标 Qt d->shownOnce，
                                            qcombobox.cpp:3090-3099——
                                            AdjustToContentsOnFirstShow 档首显
                                            失效一次尺寸缓存后置位）。 */
    bool    m_sizeHintValid;           /**< 尺寸提示缓存有效（内部；对标 Qt
                                            d->sizeHint != QSize() 的缓存位，
                                            qcombobox.cpp:2508-2521；附
                                            m_hintFontProbe 字体指纹——X 的
                                            setFont 不发 ChangeEvent
                                            （XWidget.c XWidget_setFont 直改值
                                            + updateGeometry），Qt 靠 FontChange
                                            失效（qcombobox.cpp:3043），此处以
                                            探针宽度漂移补失效）。 */
    int     m_hintFontProbe;           /**< 缓存字体指纹（内部；缓存填充时的
                                            探针文本宽，见 m_sizeHintValid）。 */
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
 * @details 缺省 AdjustToContentsOnFirstShow（Qt 同）：尺寸提示首次显示后
 *          冻结，此后 addItem/insertItem/removeItem/setItemText 等**内容
 *          变化不再自动变宽**（Qt qcombobox.cpp:1084-1161 仅 AdjustToContents
 *          档随内容失效）。运行期 clear+重填/改文本的组合框（数据驱动页
 *          的通道/筛选/策略下拉等）必须显式设 AdjustToContents，否则首显
 *          后重填的长项被省略号截断。本 setter 切换即重算一次尺寸（Qt
 *          qcombobox.cpp:1617-1625）。
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
 * @details    所有权对齐 Qt 6.8（qcombobox.cpp:2038-2062）：组合框**不**
 *             取得新模型所有权——新模型为借用，本控件析构不释放它；仅当
 *             被替换的旧模型是组合框内部懒创建（m_modelOwned）时才释放，
 *             等价 Qt 的 `if (d->model && d->model->QObject::parent() ==
 *             this) delete d->model;`。
 *
 *             **模型是唯一数据源**（Qt 6.8 实测：setModel 后 count 随模型
 *             行数、itemText 取自 modelColumn 列；删除外部模型后 count=0、
 *             currentIndex=-1）。本实现的条目数组是模型的镜像：
 *             - setModel 立刻按模型行数/文本重建条目，并按 Qt 口径重设当前
 *               项（空模型 ⇒ -1；非空 ⇒ 0）；
 *             - 连接模型的 rowsInserted/rowsRemoved/dataChanged/destroyed，
 *               模型变化即同步（插入在当前项之前静默右移当前索引，移除当前
 *               项按 Qt rowsRemoved 口径改选，模型销毁 ⇒ 清空）；
 *             - addItem/insertItem/setItemText/removeItem/clear 在外部模型
 *               安装期间**写回模型**（insertRows/removeRows/setData），
 *               不再只改本地镜像；
 *             - 项图标/项数据（role 简化承载）模型无对应角色，仍存于组合框
 *               本地平行数组（Qt 走 DecorationRole/UserRole），见
 *               XComboBox_setItemIcon/setItemData 注记。
 *
 *             与 Qt 的差异：Qt 的条目**就是**模型行且 `view()` 直接展示模型；
 *             本库镜像一份文本用于闭态绘制与 findText 等便捷查询，外部直接改
 *             模型（setData）同样经 dataChanged 同步，故对外语义一致。
 * @param      self 目标控件指针；传入 NULL 时函数不执行任何操作。
 * @param      model 新模型（借用）；NULL 为空操作（对标 Qt 对 0 模型告警
 *             并返回）。
 * @return     无返回值。
 */
void XComboBox_setModel(XComboBox* self, XAbstractItemModel* model);
/**
 * @brief      查询弹出层期望宽度（对标 QFontComboBox 的弹层宽度定制）。
 * @details    Qt 6.8 实测：QComboBox 弹层宽 = 控件宽；QFontComboBox 在
 *             resizeEvent 里把弹层容器宽改为 `min(控件宽×5/3, 屏幕可用宽)`
 *             （qfontcombobox.cpp:526-537；本机探针 combo 120 宽 ⇒ 普通
 *             组合框 120、字体组合框 200）。本虚槽即该定制点：基类返回控件
 *             宽，派生类可重载。
 * @param      self 目标控件；NULL 返回 0。
 * @return     弹层期望宽度（px）。
 */
int XComboBox_popupWidth(const XComboBox* self);
/** @brief 弹层宽度的基类缺省实现（返回控件宽；子类重载后经此取基类口径）。
 * @param self 目标控件；NULL 返回 0。
 * @return 控件宽（px）。 */
int XComboBox_popupWidth_base(const XComboBox* self);
/**
 * @brief      查询条目是否为分隔条（对标 Qt 的
 *             `itemData(index, Qt::AccessibleDescriptionRole) == "separator"`）。
 * @details    Qt 6.8 没有 isSeparator() 公共 API；分隔条在 Qt 里是模型行的
 *             AccessibleDescriptionRole 数据（本机 Qt 探针：insertSeparator
 *             后该行文本为空、角色="separator"、flags 去掉
 *             ItemIsEnabled/ItemIsSelectable）。本库模型无角色体系，故以
 *             平行标记数组承载并提供本查询；`ItemIsEnabled` 的对应语义由
 *             isTabEnabled 式的条目启用位之外的「不可选中」体现在弹层命中
 *             路径（分隔条不响应悬停高亮与激活）。
 * @param      self 目标控件；NULL 返回 false。
 * @param      index 条目索引。
 * @return     分隔条返回 true；越界或普通条目返回 false。
 */
bool XComboBox_isItemSeparator(const XComboBox* self, int index);
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
 * @details    对标 Qt 语义转发给弹层视图：等价
 *             `view()->setItemDelegate(delegate)`（XAbstractItemView_
 *             setItemDelegate，qcombobox.cpp:2014-2019）——委托参与弹层
 *             视图的**编辑路径**（行级→列级→本指针→内建默认委托的解析
 *             顺序，见 XAbstractItemView.h 委托节）；委托绘制/sizeHint
 *             钩子本库仍未建（XItemDelegate.h 注记），弹层行绘制仍按
 *             内建 role 承载。委托为借用语义，组合框与视图都不取得
 *             所有权，调用方负责其生命周期；传入非 XItemDelegate 指针
 *             并在开启弹层编辑后使用为未定义行为（视图按 XItemDelegate*
 *             虚表分派）。NULL 按 Qt 口径拒绝（qWarning「cannot set a 0
 *             delegate」的静默等价：不转发、保留现委托；视图级恢复默认
 *             委托可经 XComboBox_view 后直调
 *             XAbstractItemView_setItemDelegate(view, NULL)）。
 * @param      self 目标控件；传入 NULL 时函数不执行任何操作。
 * @param      delegate 委托对象指针（XItemDelegate* 或其派生）；NULL
 *             为空操作。
 * @return     无返回值。
 */
void XComboBox_setItemDelegate(XComboBox* self, void* delegate);
/**
 * @brief      查询条目委托（对标 QComboBox::itemDelegate）。
 * @details    对标 Qt 读弹层视图（qcombobox.cpp:2006-2012）：返回
 *             view()->itemDelegate()——即 setItemDelegate 安装的指针；
 *             未安装时为 NULL（内建默认委托仅在编辑会话期懒创建，不经
 *             本查询返回）。首次调用会懒创建弹层视图。
 * @param      self 目标控件；NULL 返回 NULL。
 * @return     借用指针；未设置返回 NULL；不得释放。
 */
void* XComboBox_itemDelegate(const XComboBox* self);
/**
 * @brief      输入法查询（对标 QComboBox::inputMethodQuery，简化承载）。
 * @details    当前仅支持返回编辑文本类查询：可编辑模式返回行编辑框
 *             内容，只读模式返回当前项文本；其余查询返回空文本。
 *             注：Qt 口径的强类型版本见 `XComboBox_inputMethodQuery_2`；
 *             本函数是项目既有的 XString 简化入口（调用点在
 *             Test/XGuiDemo），保留不动。
 * @param      self 目标控件；NULL 返回 NULL。
 * @param      query 查询类别（Qt::InputMethodQuery 数值）。
 * @return     新建 XString*（空文本也返回对象）；调用方负责
 *             XClassDelete 释放。
 */
XString* XComboBox_inputMethodQuery(XComboBox* self, int query);
/**
 * @brief      输入法查询（双参重载；对标 QComboBox::inputMethodQuery(
 *             Qt::InputMethodQuery, const QVariant&)，qcombobox.h:161）。
 * @details    Qt 6.8 本机探针实测（QComboBox 只读与可编辑两态）：
 *             - 可编辑 ⇒ 除下述外一律取**内嵌行编辑框**的查询结果：
 *               ImCursorRectangle = 光标矩形（QRect，探针
 *               QRect(66,0 9x19)）、ImSurroundingText = 编辑文本、
 *               ImCursorPosition = 光标字符位置；
 *             - 只读 ⇒ 走 QWidget 缺省：ImCursorRectangle =
 *               (width/2, 0, 1, height)（探针 QRect(40,0 1x30)），
 *               ImSurroundingText/ImCursorPosition 为**无效变体**；
 *             - argument 不改变结果（Qt 对 ImCursorRectangle 才转发
 *               lineEdit，行编辑的双参实现忽略 argument；探针带
 *               QPointF 参数结果一致）。
 *             本实现与 Qt 同构：可编辑时把 ImCursorRectangle/
 *             ImSurroundingText/ImCursorPosition 交给行编辑框
 *             （XWidget_inputMethodQuery 虚槽），其余回落基类
 *             XWidget_inputMethodQuery（含 ImInputItemClipRectangle/
 *             ImHints/ImEnabled/ImAnchorPosition/ImAnchorRectangle）。
 * @param      self 目标控件；NULL 返回 NULL。
 * @param      query 查询项（Qt::InputMethodQuery 数值）。
 * @param      argument 备用参数（Qt 的 QVariant；本库以 XVariant 借用
 *             承载，可 NULL）。仅随转发下行，当前无查询项消费。
 * @return     新建 XVariant（调用方 XClassDelete 释放）；不支持的查询项
 *             返回 NULL（等价 Qt 无效 QVariant）。
 */
XVariant* XComboBox_inputMethodQuery_2(XComboBox* self, int query,
                                       const XVariant* argument);
/** @brief XCombo盒current索引（对标 Qt 同名接口）。
 * @param self 目标控件指针。
 * @return 返回对应数值；无效时返回 0 或 -1（视接口语义）。
 */
int XComboBox_currentIndex(const XComboBox* self);
/** @brief 读取当前文本（返回新建 XString*，调用方负责 delete_base；对标 QComboBox::currentText）。
 * @details 对标 Qt 6.8 文档：可编辑时 current text 即行编辑显示值——
 *          编辑框文本与当前项文本分叉（用户编辑中/程序化 setEditText
 *          未命中项）时返回编辑框文本；同步态或不可编辑时返回当前项
 *          文本（无当前项返回空文本）。
 * @param self 目标控件指针。
 * @return 新建 XString*；无当前项时返回空 XString*。
 */
XString* XComboBox_currentText(const XComboBox* self);
/** @brief 读取当前文本（UTF-8 借用；对标 QComboBox::currentText）。
 * @details 分叉语义同 XComboBox_currentText；分叉态借用的是行编辑框
 *          内部缓冲（行编辑文本变更后失效），同步态借用项文本存储。
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
/**
 * @brief      按 Qt::MatchFlags 查找文本所在项（对标
 *             QComboBox::findText(text, flags)，qcombobox.h:61-65）。
 * @details    匹配口径（Qt 6.8 本机探针实测，见 XComboBoxMatchFlag）：
 *             MatchExactly 恒大小写敏感；Contains/StartsWith/EndsWith
 *             缺省不敏感、带 MatchCaseSensitive 后敏感；
 *             MatchRegularExpression/MatchWildcard 无引擎返回 -1。
 * @param      self 目标控件；NULL 返回 -1。
 * @param      text UTF-8 文本；NULL 或空串返回 -1。
 * @param      flags XComboBoxMatchFlag 位组合。
 * @return     首个命中项索引；未命中 -1。
 */
int XComboBox_findText_3(const XComboBox* self, const char* text,
                         unsigned flags);
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
/** @brief 在指定索引插入带图标与用户数据的项（对标
 *         `QComboBox::insertItem(int, const QIcon&, const QString&,
 *         const QVariant& userData = {})`，qcombobox.h:132-141）。
 * @details 图标以路径承载（本库无 QIcon 值参数；与 setItemIcon 同源），
 *          用户数据以字符串承载（本库 setItemData 同源）。等价于
 *          insertItem_2 + setItemIcon_2 + setItemData_2 的组合，单次接线
 *          避免中间态（插入瞬间条目既无图标也无数据）。图标/数据为 NULL
 *          表示该字段为空。
 * @param self 目标控件指针。
 * @param index 索引（0 起，负数插最前、超出追加）。
 * @param iconPath 图标路径（UTF-8）；可为 NULL。
 * @param text UTF-8 文本；不能为 NULL。
 * @param userData 用户数据（UTF-8）；可为 NULL。
 * @return 无返回值。
 */
void XComboBox_insertItem_3(XComboBox* self, int index, const char* iconPath,
                            const char* text, const char* userData);
/** @brief 在指定索引插入带用户数据的项（对标
 *         `QComboBox::insertItem(int, const QString&, const QVariant&)`）。
 * @param self 目标控件指针。
 * @param index 索引（0 起，负数插最前、超出追加）。
 * @param text UTF-8 文本；不能为 NULL。
 * @param userData 用户数据（UTF-8）；可为 NULL。
 * @return 无返回值。
 */
void XComboBox_insertItem_4(XComboBox* self, int index, const char* text,
                            const char* userData);
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
/** @brief 追加带图标与用户数据的项（对标
 *         `QComboBox::addItem(const QIcon&, const QString&,
 *         const QVariant& userData = {})`，qcombobox.h:132-141）。
 * @details 等价 insertItem_3(count(), ...)（Qt addItem 即插到尾部）。
 * @param self 目标控件指针。
 * @param iconPath 图标路径（UTF-8）；可为 NULL。
 * @param text UTF-8 文本；不能为 NULL。
 * @param userData 用户数据（UTF-8）；可为 NULL。
 * @return 无返回值。
 */
void XComboBox_addItem_3(XComboBox* self, const char* iconPath,
                         const char* text, const char* userData);
/** @brief 追加带用户数据的项（对标
 *         `QComboBox::addItem(const QString&, const QVariant& userData)`）。
 * @param self 目标控件指针。
 * @param text UTF-8 文本；不能为 NULL。
 * @param userData 用户数据（UTF-8）；可为 NULL。
 * @return 无返回值。
 */
void XComboBox_addItem_4(XComboBox* self, const char* text,
                         const char* userData);
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
 * @details 插入**不可选分隔条**（Qt 6.8 实测：条目文本为空、
 *          `itemData(i, Qt::AccessibleDescriptionRole) == "separator"`、
 *          flags 去掉 ItemIsEnabled/ItemIsSelectable；索引越界口径同
 *          insertItem——负数插最前、超出追加）。弹层对该行绘制横线，
 *          悬停/激活/补全命中均跳过。
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

/**
 * @brief      弹出条目列表（对标 QComboBox::showPopup，public virtual）。
 * @details    对标 Qt 6.8 qcombobox.cpp:2621-2625：无条目（count()<=0）
 *             时为空操作、不建弹层；已有弹层时为空操作（幂等）。子类经
 *             虚表重载 `showPopup` 名称的公共入口即本函数，内部实现挂在
 *             XComboBox_showPopup_base 供基类链调用。
 * @param      self 目标控件指针；NULL 时函数不执行任何操作。
 * @return     无返回值。
 */
void XComboBox_showPopup(XComboBox* self);
/**
 * @brief      收起条目列表（对标 QComboBox::hidePopup，public virtual）。
 * @details    对标 Qt 6.8 qcombobox.cpp:2866-2871：未弹出时为空操作；
 *             收起时解除弹层模态抓取并把焦点交回宿主顶层窗口。
 * @param      self 目标控件指针；NULL 时函数不执行任何操作。
 * @return     无返回值。
 */
void XComboBox_hidePopup(XComboBox* self);
/** @brief 弹出条目列表的基类实现（子类重载后的父类链入口；语义同
 *         XComboBox_showPopup）。 @param self 目标控件。 */
void XComboBox_showPopup_base(XComboBox* self);
/** @brief 收起条目列表的基类实现（子类重载后的父类链入口；语义同
 *         XComboBox_hidePopup）。 @param self 目标控件。 */
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
 * @details 图标参与渲染（P3-56）：闭态当前项图标、弹层 DecorationRole
 *          逐行装饰（按 iconSize 预缩放，解码图像由组合框自持）与
 *          sizeHint 的 iconSize+4 宽度计入（Qt CE_ComboBoxLabel/
 *          recomputeSizeHint 口径）；路径解码失败该条目按无图标画。
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
/**
 * @brief      按 Qt::MatchFlags 查找数据所在项（对标
 *             QComboBox::findData(data, role, flags) 的 flags 维度；
 *             role 维度由本库「项数据仅一份字符串」简化覆盖，见
 *             setItemData）。
 * @details    匹配口径同 XComboBox_findText_3（Qt 6.8 探针实测：
 *             MatchExactly 恒大小写敏感；Contains/StartsWith/EndsWith
 *             缺省不敏感；正则/通配无引擎返回 -1）。
 * @param      self 目标控件；NULL 返回 -1。
 * @param      data UTF-8 数据；NULL 或空串返回 -1。
 * @param      flags XComboBoxMatchFlag 位组合。
 * @return     首个命中项索引；未命中 -1。
 */
int XComboBox_findData_3(const XComboBox* self, const char* data,
                         unsigned flags);
/**
 * @brief      设置补全器（对标 QComboBox::setCompleter；借用，不拥有）。
 * @details    对标 Qt 6.8（qcombobox.cpp:1950-1963）转发**内嵌行编辑**：
 *             等价 `lineEdit()->setCompleter(c)`，行编辑的输入控制随之
 *             驱动该补全器（前缀过滤/弹层/键导航由 XLineEdit 补全链路
 *             承载，见 XLineEdit_setCompleter）；同时连接补全器
 *             activated → 「置当前项 + 发射 activated/textActivated 对」
 *             （对标 QComboBoxPrivate::completerActivated），安装后补全
 *             弹层锚到组合框（Qt 末尾 c->setWidget(this) 同口径）。
 *             - 非可编辑/无行编辑：直接忽略（Qt qWarning 的静默等价）；
 *             - 补全器无模型则接通组合框条目模型（安装时 completer->
 *               setModel(d->model) 承载）；
 *             - 替换/解除时摘除旧补全器的 activated 联动并清其对本组合
 *               框的关联位（借用卫生；连接不去重，先断后连）；
 *             - 安装非空补全器即置 completerMode（降级承载语义：无
 *               completer 时 completerMode 为内建前缀过滤弹层开关，
 *               见 setCompleterMode；completer 在装期间内建前缀过滤
 *               停用，避免双层弹层）。
 *             生存期：组合框不取得补全器所有权，调用方保证不悬空。
 * @param      self 目标控件。
 * @param      completer 补全器借用指针；可为 NULL（解除）。
 * @return     无返回值。
 */
void XComboBox_setCompleter(XComboBox* self, XCompleter* completer);
/** @brief 查询补全器（对标 QComboBox::completer：读内嵌行编辑；
 *         借用指针；未安装返回 NULL）。 */
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
 * @details 尺寸参与渲染与几何（P3-56）：闭态/弹层按本尺寸解码绘制，
 *          sizeHint 计 iconSize+4 宽与 iconSize+2 最小高（Qt
 *          qcombobox.cpp:1685-1691 缓存失效同口径）；变更即重算尺寸。
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
 * @note  X 扩展信号：Qt 6.8 QComboBox 信号表只有 7 个
 *        （editTextChanged/activated/textActivated/highlighted/
 *        textHighlighted/currentIndexChanged/currentTextChanged，
 *        qcombobox.h:170-177），无 popupShown/popupHidden。本库保留它们
 *        作弹层显隐的内部诊断扩展——仓库既有测试依赖
 *        （Test/XGuiDemo/xgui_demo_apitest_input.c:468/470 连接、
 *        :2441-2445 断言），故不按「Qt 无对应」删除。
 * @param self 目标控件指针。
 * @return 返回对象指针；无效时返回 NULL。
 */
void* XComboBox_popupShown_signal(XComboBox* self);
/** @brief XCombo盒popupHidden 信号地址（发射经 XObject_emitSignal）。
 * @note  X 扩展信号（Qt 无对应），见 popupShown 注记。
 * @param self 目标控件指针。
 * @return 返回对象指针；无效时返回 NULL。
 */
void* XComboBox_popupHidden_signal(XComboBox* self);

#ifdef __cplusplus
}
#endif
#endif /* XWIDGET_ON && XCOMBOBOX_ON && XLINEEDIT_ON */

#endif /* XGUI_ON */

#endif /* XCOMBOBOX_H */
