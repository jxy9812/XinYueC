/**
 * @file       XVirtualKeyboardSelectionListModel.h
 * @brief      XVirtualKeyboardSelectionListModel 候选列表数据模型公开
 *             API（对标 Qt 6.8 QVirtualKeyboardSelectionListModel）。
 * @details    继承偏差如实标注：XGui 无 QAbstractListModel 等价基类，
 *             模型退化为引擎持有的数据对象+信号，视图=XVirtualKeyboard 候选
 *             带直接消费（Qt 为 QAbstractListModel）。枚举照抄 Qt：
 *             Type{WordCandidateList=0}；Role{Display=0(=Qt DisplayRole)、
 *             WordCompletionLength=0x0101(UserRole+1)、Dictionary=0x0102、
 *             CanRemoveSuggestion=0x0103}；DictionaryType{Default,User}。
 *             data/dataAt 返回新建 XVariant*（文本角色 XVariantType_String
 *             承载），调用方释放。实例仅由引擎 TU 创建（Qt 私有构造+
 *             friend 口径；创建入口落引擎保护头）。
 * @note       模块总开关 XVIRTUALKEYBOARD_ON；实现只依赖 XinYueC 抽象层。
 * @author     XinYueC 团队
 ******************************************************************************/
#ifndef XVIRTUALKEYBOARDSELECTIONLISTMODEL_H
#define XVIRTUALKEYBOARDSELECTIONLISTMODEL_H
#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>
#include "XGuiConfig.h"
#include "XObject.h"
#include "XMemory.h"
#include "XVariant.h"

#if XVIRTUALKEYBOARD_ON

/* 数据源输入法类型（直接引用公共头，无包含环）。 */
#include "XVirtualKeyboardAbstractInputMethod.h"

/**
 * @brief      候选列表类型（对标 Type；目前仅一种）。
 */
typedef enum XVirtualKeyboardSelectionListModelType
{
    XVirtualKeyboardSelectionListModelType_WordCandidateList = 0
    /**< 候选词列表（唯一类型，对标 WordCandidateList = 0）。 */
} XVirtualKeyboardSelectionListModelType;

/**
 * @brief      候选数据角色（对标 Role；数值与 Qt 一致）。
 */
typedef enum XVirtualKeyboardSelectionListModelRole
{
    XVirtualKeyboardSelectionListModelRole_Display = 0,
    /**< 候选文本（=Qt::DisplayRole）。 */
    XVirtualKeyboardSelectionListModelRole_WordCompletionLength = 0x0101,
    /**< 前缀补全长度（=Qt::UserRole+1；区分整词与补全部分）。 */
    XVirtualKeyboardSelectionListModelRole_Dictionary = 0x0102,
    /**< 候选来源词典索引。 */
    XVirtualKeyboardSelectionListModelRole_CanRemoveSuggestion = 0x0103
    /**< 候选能否被移除。 */
} XVirtualKeyboardSelectionListModelRole;

/**
 * @brief      候选词典类型（对标 DictionaryType）。
 */
typedef enum XVirtualKeyboardSelectionListModelDictionaryType
{
    XVirtualKeyboardSelectionListModelDictionaryType_Default = 0,
    /**< 默认词典。 */
    XVirtualKeyboardSelectionListModelDictionaryType_User
    /**< 用户词典。 */
} XVirtualKeyboardSelectionListModelDictionaryType;

/** @brief 声明 XVirtualKeyboardSelectionListModel 虚函数枚举：继承
 *         XObject（无新增槽位）。 */
XCLASS_DEFINE_BEGING(XVirtualKeyboardSelectionListModel)
XCLASS_DEFINE_EXTEND_END(XVirtualKeyboardSelectionListModel, XObject)

/**
 * @brief      候选列表数据模型对象；m_class 必须为第一个成员。
 * @details    m_data 私有块保存数据源反向引用与缓存计数。
 */
typedef struct XVirtualKeyboardSelectionListModel
{
    XObject m_class;   /**< 第一个成员，由 XObject 管理。 */
    void* m_data;      /**< 私有数据块，由对象拥有；仅供实现使用。 */
} XVirtualKeyboardSelectionListModel;

/** @brief 初始化类虚函数表并返回共享表指针。 */
XVtable* XVirtualKeyboardSelectionListModel_class_init(void);

/* ==================== 生命周期（所有权边界） ==================== */

/**
 * @brief      通过 XClass 虚表反初始化（栈/外部存储对象使用）。
 * @details    创建入口在 Protected 头（仅引擎 TU 构造），归属方
 *             （XVirtualKeyboardInputEngine）在自身 TU 内释放，故释放
 *             接口必须在公开头声明。
 */
#define XVirtualKeyboardSelectionListModel_deinit_base(self) \
    XClass_deinit_base((XClass*)(self))
/** @brief 删除堆上对象（归属方释放自有候选模型用）。 */
#define XVirtualKeyboardSelectionListModel_delete_base(self) \
    XClass_delete_base((XClass*)(self))

/* ==================== 查询 ==================== */

/**
 * @brief      返回行数（对标 rowCount）。
 * @details    数据源缺席返回 0；否则经数据源 selectionListItemCount
 *             实时查询。
 * @param      self 模型对象借用指针；可为 NULL。
 * @return     行数；数据源缺席或 self 为 NULL 返回 0。
 */
int XVirtualKeyboardSelectionListModel_rowCount(
        const XVirtualKeyboardSelectionListModel* self);

/**
 * @brief      返回行数（对标 count；同 rowCount）。
 * @param      self 模型对象借用指针；可为 NULL。
 * @return     行数；数据源缺席或 self 为 NULL 返回 0。
 */
int XVirtualKeyboardSelectionListModel_count(
        const XVirtualKeyboardSelectionListModel* self);

/**
 * @brief      取候选数据（对标 data(index, role)）。
 * @param      self 模型对象借用指针；可为 NULL。
 * @param      index 全量 0 基下标。
 * @param      role 数据角色（XVirtualKeyboardSelectionListModelRole）。
 * @return     新建 XVariant*（Display=候选串 XVariantType_String；
 *             WordCompletionLength=Int32；Dictionary=Int32；
 *             CanRemoveSuggestion=Bool）；越界/无数据源返回 NULL。
 *             调用方负责释放。
 */
XVariant* XVirtualKeyboardSelectionListModel_data(
        const XVirtualKeyboardSelectionListModel* self, int index, int role);

/**
 * @brief      直接取数据（对标 Q_INVOKABLE dataAt(index, role)）。
 * @details    与 data 同实现（Qt 两者等价；本模型非视图代理）。
 * @param      self 模型对象借用指针；可为 NULL。
 * @param      index 全量 0 基下标。
 * @param      role 数据角色（XVirtualKeyboardSelectionListModelRole）。
 * @return     新建 XVariant*，调用方负责释放；越界/无数据源/self 为
 *             NULL 返回 NULL。
 */
XVariant* XVirtualKeyboardSelectionListModel_dataAt(
        const XVirtualKeyboardSelectionListModel* self, int index, int role);

/**
 * @brief      返回数据源输入法（对标 dataSource）。
 * @param      self 模型对象借用指针；可为 NULL。
 * @return     输入法借用指针（引擎装配链所有，不得释放）；未装配或
 *             self 为 NULL 返回 NULL。
 */
XVirtualKeyboardAbstractInputMethod*
XVirtualKeyboardSelectionListModel_dataSource(
        const XVirtualKeyboardSelectionListModel* self);

/**
 * @brief      返回模型类型（装配时确定；默认 WordCandidateList）。
 * @param      self 模型对象借用指针；可为 NULL。
 * @return     模型类型枚举值；self 为 NULL 返回 0。
 */
XVirtualKeyboardSelectionListModelType
XVirtualKeyboardSelectionListModel_type(
        const XVirtualKeyboardSelectionListModel* self);

/* ==================== 交互（Q_INVOKABLE 对标） ==================== */

/**
 * @brief      选中候选（对标 selectItem）。
 * @details    转发数据源 selectionListItemSelected 并发 itemSelected。
 * @param      self 模型对象；可为 NULL。
 * @param      index 全量 0 基下标。
 * @return     已转发返回 true；越界/无数据源/self 为 NULL 返回 false。
 */
bool XVirtualKeyboardSelectionListModel_selectItem(
        XVirtualKeyboardSelectionListModel* self, int index);

/**
 * @brief      移除候选（对标 removeItem）。
 * @details    转发数据源 selectionListRemoveItem；无用户词典面（本轮
 *             availableDictionaries 恒空）时数据源返回 false=无操作。
 * @param      self 模型对象；可为 NULL。
 * @param      index 全量 0 基下标。
 * @return     数据源已移除返回 true；越界/无数据源/self 为 NULL 返回
 *             false。
 */
bool XVirtualKeyboardSelectionListModel_removeItem(
        XVirtualKeyboardSelectionListModel* self, int index);

/* ==================== 信号（纯 ID getter） ==================== */

/**
 * @brief      countChanged() 信号标识（候选数变化）。
 * @param      self 模型对象借用指针；可为 NULL。
 * @return     信号标识借用指针（内部存储，不得释放）；self 为 NULL
 *             返回 NULL。
 */
void* XVirtualKeyboardSelectionListModel_countChanged_signal(
        XVirtualKeyboardSelectionListModel* self);
/**
 * @brief      activeItemChanged(int index) 信号标识（高亮候选变化）。
 * @param      self 模型对象借用指针；可为 NULL。
 * @param      index 仅占位（本 getter 不读取）。
 * @return     信号标识借用指针（内部存储，不得释放）；self 为 NULL
 *             返回 NULL。
 */
void* XVirtualKeyboardSelectionListModel_activeItemChanged_signal(
        XVirtualKeyboardSelectionListModel* self, int index);
/**
 * @brief      itemSelected(int index) 信号标识（候选被选中）。
 * @param      self 模型对象借用指针；可为 NULL。
 * @param      index 仅占位（本 getter 不读取）。
 * @return     信号标识借用指针（内部存储，不得释放）；self 为 NULL
 *             返回 NULL。
 */
void* XVirtualKeyboardSelectionListModel_itemSelected_signal(
        XVirtualKeyboardSelectionListModel* self, int index);

#endif /* XVIRTUALKEYBOARD_ON */

#ifdef __cplusplus
}
#endif
#endif /* XVIRTUALKEYBOARDSELECTIONLISTMODEL_H */
