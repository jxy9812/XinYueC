/******************************************************************************
 * @file       XLayout_Internal.h
 * @brief      XLayout 布局系统内部头文件（不对外公开，仅供库内实现使用）。
 * @details    内含两类内部实现类与布局基类的受保护助手：
 *             - XWidgetItem：控件条目（对标 Qt 的 QWidgetItem），把
 *               XWidget 包装成 XLayoutItem，尺寸/伸展/几何全部委托给
 *               控件；控件本身只按借用指针持有，不取得所有权；
 *             - XSpacerItem：空白条目（对标 Qt 的 QSpacerItem，自
 *               XSpacerItem.h 公开；本头经由 XSpacerItem.h 纳入），承载
 *               固定尺寸或可伸展的空白，支持尺寸策略；
 *             - XLayout 的 attach/detach、条目数组受保护助手与有效
 *               间距查询，供 XBoxLayout/XGridLayout/XWidget 接入。
 *             本文件不得被库外代码包含；不依赖任何平台 API。
 * @author     XinYueC 团队
 ******************************************************************************/
#ifndef XLAYOUT_INTERNAL_H
#define XLAYOUT_INTERNAL_H
#ifdef __cplusplus
extern "C" {
#endif

#include "XGuiConfig.h"
#include "XLayoutItem.h"
#include "XSpacerItem.h"
#include "XLayout.h"

#if XLAYOUT_ON

/* ==================== 控件条目（对标 Qt QWidgetItem，内部实现） ==================== */

/**
 * @brief      控件条目对象（内部）。
 * @details    m_base 必须位于第一位；m_widget 为控件借用指针，条目不
 *             拥有控件。虚函数实现把尺寸提示/几何委托给 XWidget。
 */
typedef struct XWidgetItem
{
    XLayoutItem m_base;   /**< 基类成员；必须是第一个。 */
    XWidget* m_widget;    /**< 控件借用指针；不为 NULL。 */
} XWidgetItem;

/**
 * @brief      初始化控件条目类虚函数表并返回共享表指针（内部）。
 * @return     XWidgetItem 类的共享 XVtable 指针。
 */
XVtable* XWidgetItem_class_init(void);

/**
 * @brief      初始化控件条目（内部）。
 * @param      self 条目对象；不可为 NULL。
 * @param      widget 控件借用指针；可为 NULL（之后视为空条目）。
 */
void XWidgetItem_init(XWidgetItem* self, XWidget* widget);

/* ==================== 空白条目（对标 Qt QSpacerItem，自 XSpacerItem.h 公开） ==================== */

/* ==================== 内部条目工厂（仅在 XLayout 实现中使用） ==================== */

/**
 * @brief      创建包装控件的内部控件条目（堆对象）。
 * @param      widget 控件借用指针；可为 NULL。
 * @return     新条目对象；失败返回 NULL，由调用方负责释放责任登记。
 */
XLayoutItem* XLayoutItem_createWidgetItem(XWidget* widget);

/**
 * @brief      创建内部空白条目（堆对象）。
 * @param      size  首选空白尺寸。
 * @param      hPolicy 水平尺寸策略。
 * @param      vPolicy 垂直尺寸策略。
 * @return     新条目对象；失败返回 NULL，由调用方负责释放责任登记。
 */
XLayoutItem* XLayoutItem_createSpacerItem(const XSize* size,
                                          XWidgetSizePolicyPolicy hPolicy,
                                          XWidgetSizePolicyPolicy vPolicy);

/* ==================== XLayout 受保护助手（供子类与控件接入） ==================== */

/**
 * @brief      在数组末尾追加条目（受保护）。
 * @details    内部条目（addWidget/addSpacing 等创建）传 owned=true，
 *             布局 deinit 时释放；调用方传入的条目传 owned=false，
 *             释放责任仍在调用方。
 * @param      self 目标布局；可为 NULL。
 * @param      item 条目借用指针；可为 NULL。
 * @param      owned 布局是否拥有条目对象。
 * @return     新索引；失败返回 -1。
 */
int XLayout_appendItem(XLayout* self, XLayoutItem* item, bool owned);

/**
 * @brief      在指定索引插入条目（受保护）。
 * @param      self 目标布局；可为 NULL。
 * @param      index 插入位置；越界时追到末尾。
 * @param      item 条目借用指针；可为 NULL。
 * @param      owned 布局是否拥有条目对象。
 * @return     实际插入索引；失败返回 -1。
 */
int XLayout_insertItemAt(XLayout* self, int index, XLayoutItem* item, bool owned);

/**
 * @brief      返回条目在数组中的索引（受保护）。
 * @param      self 目标布局；可为 NULL。
 * @param      item 目标条目；可为 NULL。
 * @return     条目索引；未找到或失败返回 -1。
 */
int XLayout_indexOfItem(XLayout* self, const XLayoutItem* item);

/**
 * @brief      把布局挂接到控件上（受保护；XWidget_setLayout 调用）。
 * @details    记录控件借用指针并清空布局正向（控件→布局）冗余引用；
 *             已挂接其他控件的布局会被重新挂接（Qt 语义）。
 * @param      self 目标布局；可为 NULL。
 * @param      widget 控件借用指针；可为 NULL 表示解除挂接（等效 detach）。
 */
void XLayout_attachWidget(XLayout* self, XWidget* widget);

/**
 * @brief      解除布局与控件的挂接（受保护；控件销毁时调用）。
 * @param      self 目标布局；可为 NULL。
 */
void XLayout_detachWidget(XLayout* self);

/**
 * @brief      返回布局应使用的有效间距（受保护）。
 * @details    自身间距为 0 及以上时直接返回；为 -1 时向父布局/父控件
 *             链查询，最终默认 0。
 * @param      self 目标布局；可为 NULL。
 * @return     有效间距（像素）。
 */
int XLayout_effectiveSpacing(const XLayout* self);

/**
 * @brief      返回去除内容边距后的内部矩形（受保护）。
 * @details    与公开无参 XLayout_contentsRect 区分：本版本直接对
 *             rect 收拢，不依赖布局存储的几何。
 * @param      self 目标布局；可为 NULL。
 * @param      rect 原始矩形；可为 NULL。
 * @return     内部可用矩形。
 */
XRect XLayout_contentsRectForRect(const XLayout* self, const XRect* rect);

/**
 * @brief      条目挂接后的关联设置（受保护；子布局登记父布局，控件
 *             条目登记父控件）。ReplaceItemAt 原位替换后用于把新条目
 *             接入布局链。
 * @param      self 目标布局；可为 NULL。
 * @param      item 条目借用指针；可为 NULL。
 */
void XLayout_linkItem(XLayout* self, XLayoutItem* item);

/**
 * @brief      返回条目内边距（受保护；对齐解算使用）。
 * @param      self 目标布局；可为 NULL。
 * @return     布局内容边距。
 */
XMargins XLayout_layoutMargins(const XLayout* self);

#endif /* XLAYOUT_ON */

#ifdef __cplusplus
}
#endif
#endif /* XLAYOUT_INTERNAL_H */
