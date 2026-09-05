/******************************************************************************
 * @file       XStackedLayout.h
 * @brief      XStackedLayout 堆叠布局（对标 Qt 6.8 QStackedLayout）。
 * @details    一个布局同时管理多个控件页面，但在 StackOne 模式下只显示
 *             当前页面；StackAll 模式将所有页面放入同一矩形并保持可见。
 *             页面条目使用现有 XLayout/XLayoutItem 控件条目工厂，布局的
 *             边距和父控件挂接沿用 XLayout 约定。布局对象本身不是
 *             XObject，因此 currentChanged/widgetRemoved 仅提供与 Qt
 *             一致的信号标识函数；页面切换由外部控件的信号槽调用
 *             setCurrentIndex/setCurrentWidget 完成。
 * @note       XLAYOUT_STACKED_ON 置 0 时裁剪本类；不依赖任何平台 API。
 * @author     XinYueC 团队
 ******************************************************************************/
#ifndef XSTACKEDLAYOUT_H
#define XSTACKEDLAYOUT_H
#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include "XGuiConfig.h"
#include "XLayout.h"
#include "XLayout_config.h"

#if XLAYOUT_ON && XLAYOUT_STACKED_ON

/** @brief 页面堆叠可见性模式（对标 QStackedLayout::StackingMode）。 */
typedef enum XStackedLayoutStackingMode
{
    XStackedLayoutStackOne = 0, /**< 仅显示当前页面（默认）。 */
    XStackedLayoutStackAll = 1  /**< 所有页面可见，并共享同一几何。 */
} XStackedLayoutStackingMode;

/** @brief XStackedLayout 不新增虚函数槽位，仅重载 XLayout 槽位。 */
XCLASS_DEFINE_BEGING(XStackedLayout)
XCLASS_DEFINE_EXTEND_END(XStackedLayout, XLayout)

/**
 * @brief      堆叠布局对象；m_base 必须是第一个成员。
 * @details    页面条目由基类数组保存；m_currentIndex 为空布局时为 -1。
 */
typedef struct XStackedLayout
{
    XLayout m_base;                         /**< 基类布局；必须是第一个成员。 */
    int m_currentIndex;                     /**< 当前页面索引，空布局为 -1。 */
    XStackedLayoutStackingMode m_stackingMode; /**< 当前可见性模式。 */
} XStackedLayout;

/** @brief 初始化 XStackedLayout 类虚函数表并返回共享表指针。 */
XVtable* XStackedLayout_class_init(void);

/**
 * @brief 初始化空堆叠布局。
 * @param self 待初始化布局对象；不可为 NULL。
 */
void XStackedLayout_init(XStackedLayout* self);

/**
 * @brief 创建堆叠布局。
 * @param parent 可选父控件；非 NULL 时自动调用 XWidget_setLayout。
 * @return 新布局对象；失败返回 NULL，调用方使用 XLayout_delete_base 释放。
 */
XStackedLayout* XStackedLayout_create(XWidget* parent);

/** @brief 通过 XClass 虚表释放布局资源。 */
#define XStackedLayout_deinit_base(self) XClass_deinit_base((XClass*)(self))
/** @brief 删除堆上的堆叠布局对象。 */
#define XStackedLayout_delete_base(self) XClass_delete_base((XClass*)(self))

/**
 * @brief 向末尾追加页面控件（对标 QStackedLayout::addWidget）。
 * @param self 目标布局；可为 NULL。
 * @param widget 页面控件借用指针；可为 NULL（忽略）。
 * @return 页面索引；失败返回 -1。
 */
int XStackedLayout_addWidget(XStackedLayout* self, XWidget* widget);

/**
 * @brief 在指定位置插入页面控件（对标 QStackedLayout::insertWidget）。
 * @param self 目标布局；可为 NULL。
 * @param index 插入位置；负数或超出末尾时追加。
 * @param widget 页面控件借用指针；可为 NULL（失败）。
 * @return 实际页面索引；失败返回 -1。
 */
int XStackedLayout_insertWidget(XStackedLayout* self, int index,
                                XWidget* widget);

/**
 * @brief 添加已有布局条目（对标 QStackedLayout::addItem，仅接受控件条目）。
 * @param self 目标布局；可为 NULL。
 * @param item 控件条目借用指针；非控件条目会被忽略。
 */
void XStackedLayout_addItem(XStackedLayout* self, XLayoutItem* item);

/**
 * @brief 返回当前页面控件（对标 QStackedLayout::currentWidget）。
 * @param self 目标布局；可为 NULL。
 * @return 当前页面借用指针；无当前页面返回 NULL。
 */
XWidget* XStackedLayout_currentWidget(const XStackedLayout* self);

/**
 * @brief 返回当前页面索引（对标 QStackedLayout::currentIndex）。
 * @param self 目标布局；可为 NULL。
 * @return 当前索引；空布局或失败返回 -1。
 */
int XStackedLayout_currentIndex(const XStackedLayout* self);

/**
 * @brief 返回指定索引页面控件（对标 QStackedLayout::widget）。
 * @param self 目标布局；可为 NULL。
 * @param index 页面索引，从 0 开始。
 * @return 页面借用指针；越界或非控件条目返回 NULL。
 */
XWidget* XStackedLayout_widget(const XStackedLayout* self, int index);

/**
 * @brief 返回页面数量（对标 QStackedLayout::count）。
 * @param self 目标布局；可为 NULL。
 * @return 页面数量；失败返回 0。
 */
int XStackedLayout_count(const XStackedLayout* self);

/**
 * @brief 设置当前页面索引（对标 QStackedLayout::setCurrentIndex）。
 * @param self 目标布局；可为 NULL。
 * @param index 有效页面索引；无效索引不改变当前页。
 */
void XStackedLayout_setCurrentIndex(XStackedLayout* self, int index);

/**
 * @brief 按控件设置当前页面（对标 QStackedLayout::setCurrentWidget）。
 * @param self 目标布局；可为 NULL。
 * @param widget 已加入布局的页面控件；不属于布局时忽略。
 */
void XStackedLayout_setCurrentWidget(XStackedLayout* self, XWidget* widget);

/**
 * @brief 返回当前堆叠模式（对标 QStackedLayout::stackingMode）。
 * @param self 目标堆叠布局；可为 NULL。
 * @return 当前模式；self 为 NULL 时返回 StackOne 默认值。
 */
XStackedLayoutStackingMode XStackedLayout_stackingMode(
    const XStackedLayout* self);

/**
 * @brief 设置堆叠模式（对标 QStackedLayout::setStackingMode）。
 * @param self 目标布局；可为 NULL。
 * @param mode StackOne 或 StackAll。
 */
void XStackedLayout_setStackingMode(XStackedLayout* self,
                                    XStackedLayoutStackingMode mode);

/**
 * @brief currentChanged 信号标识函数（对标 QStackedLayout::currentChanged）。
 * @param self 信号发送布局；可为 NULL，仅用于取得标识。
 * @param index 新当前页面索引。
 * @return 稳定的信号标识值。
 */
void* XStackedLayout_currentChanged_signal(XStackedLayout* self, int index);

/**
 * @brief widgetRemoved 信号标识函数（对标 QStackedLayout::widgetRemoved）。
 * @param self 信号发送布局；可为 NULL，仅用于取得标识。
 * @param index 被移除页面索引。
 * @return 稳定的信号标识值。
 */
void* XStackedLayout_widgetRemoved_signal(XStackedLayout* self, int index);

#endif /* XLAYOUT_ON && XLAYOUT_STACKED_ON */

#ifdef __cplusplus
}
#endif
#endif /* XSTACKEDLAYOUT_H */
