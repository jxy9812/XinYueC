/******************************************************************************
 * @file       XSpacerItem.h
 * @brief      XSpacerItem 空白条目（对标 Qt 6.8 QSpacerItem）。
 * @details    XSpacerItem 继承 XLayoutItem，表示布局中的固定/可伸展空白，
 *            与 Qt 的 QSpacerItem 一一对应：
 *            - 构造：XSpacerItem_create(w, h, hPolicy, vPolicy)（对标
 *              QSpacerItem(int, int, QSizePolicy::Policy,
 *              QSizePolicy::Policy)）；
 *            - 类型安全下转：XLayoutItem_spacerItem_base()（对标
 *              QLayoutItem::spacerItem，基类条目返回 NULL）；
 *            - 查询：sizePolicy()（对标 QSpacerItem::sizePolicy，返回
 *              XWidgetSizePolicy，数值与 Qt 6.8 一致）；
 *            - 修改：changeSize()（对标 QSpacerItem::changeSize）；
 *            - 尺寸协商：sizeHint / minimumSize / maximumSize /
 *              expandingDirections / isEmpty 由本类虚表实现覆盖，位语义
 *              与 Qt 6.8 qsizepolicy.h 完全一致（GrowFlag=1、
 *              ExpandFlag=2、ShrinkFlag=4、IgnoreFlag=8）：
 *              - minimumSize：ShrinkFlag 方向收缩到 0，否则保持首选尺寸；
 *              - maximumSize：GrowFlag 方向放开到 XWIDGET_MAX_SIZE，
 *                否则固定首选尺寸；
 *              - expandingDirections：ExpandFlag 方向可伸展；
 *              - isEmpty：始终 true（空白不参与占用判定）。
 *             用户自绘条目也可以继承 XSpacerItem 语义扩展（布局对
 *             spacerItem() 返回非 NULL 的条目按空白处理）。
 * @note       结构体与 XSpacerItem_init / XSpacerItem_class_init 是
 *             布局系统运行必需（XBoxLayout 的 addSpacing / addStretch /
 *             addStrut 内部空白均由此构造），随总开关 XLAYOUT_ON 编译；
 *             公开的 XSpacerItem_create / XSpacerItem_changeSize /
 *             XSpacerItem_sizePolicy 受 XLAYOUT_SPACER_ON 门控——嵌入式
 *             用户若不需要手动创建空白或挂接 addSpacerItem，可关闭该
 *             开关裁剪堆对象工厂与查询代码。对象挂入布局（addSpacerItem）
 *             后所有权转移给布局（Qt 语义），布局释放时一并销毁。
 *             本模块不依赖任何平台 API，嵌入式可用。
 * @author     XinYueC 团队
 ******************************************************************************/
#ifndef XSPACERITEM_H
#define XSPACERITEM_H
#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>
#include "XGuiConfig.h"
#include "XLayout_config.h"
#include "XLayoutItem.h"
#include "XWidget.h"

#if XLAYOUT_ON

/* ==================== 空白条目对象（对标 QSpacerItem） ==================== */

/**
 * @brief      XSpacerItem 空白条目对象；m_base 必须是第一个成员。
 * @details    字段含义：
 *             - m_size：首选空白尺寸（对标 QSpacerItem 私有 width/height）；
 *             - m_hPolicy / m_vPolicy：水平/垂直尺寸策略
 *               （XWidgetSizePolicyPolicy，数值与 Qt 6.8 一致）；
 *             - m_isMagic：内部标志，标记本空白由 XBoxLayout 的
 *               addSpacing / addStretch / addStrut / insertSpacerItem
 *               自动挂接（对标 Qt QBoxLayoutItem::magic），
 *               setDirection 翻转主轴方向时按此翻转横纵策略；
 *               仅布局内部维护，调用方不得手工修改。
 */
typedef struct XSpacerItem
{
    XLayoutItem  m_base;    /**< 基类成员；必须是第一个。 */
    XSize        m_size;    /**< 首选空白尺寸。 */
    uint8_t      m_hPolicy; /**< 水平尺寸策略。 */
    uint8_t      m_vPolicy; /**< 垂直尺寸策略。 */
    uint8_t      m_isMagic; /**< 内部：是否由盒式布局自动挂接（magic 空白）。 */
    uint8_t      m_reservedSpacer[3]; /**< 保留对齐字节。 */
} XSpacerItem;

/**
 * @brief      初始化 XSpacerItem 类虚函数表并返回共享表指针（内部）。
 * @return     XSpacerItem 类的共享 XVtable 指针。
 */
XVtable* XSpacerItem_class_init(void);

/**
 * @brief      初始化空白条目（内部；库内构造与子类构造使用）。
 * @param      self 条目对象；不可为 NULL。
 * @param      width  首选空白宽度。
 * @param      height 首选空白高度。
 * @param      hPolicy 水平尺寸策略。
 * @param      vPolicy 垂直尺寸策略。
 */
void XSpacerItem_init(XSpacerItem* self, int width, int height,
                      XWidgetSizePolicyPolicy hPolicy,
                      XWidgetSizePolicyPolicy vPolicy);

#if XLAYOUT_SPACER_ON

/**
 * @brief      创建空白条目（对标 QSpacerItem 公开构造）。
 * @details    在堆上创建空白条目并初始化虚表。返回对象所有权归调用方：
 *             - 可直接作为 XLayoutItem 使用（XLayoutItem_delete_base 释放）；
 *             - 可经 XLayoutItem_spacerItem_base 类型安全下转后查询/修改；
 *             - 可经 XBoxLayout_addSpacerItem / XBoxLayout_insertSpacerItem
 *               挂入布局，此时所有权转移给布局（布局销毁时一并释放）。
 * @param      width  首选空白宽度。
 * @param      height 首选空白高度。
 * @param      hPolicy 水平尺寸策略（Qt 默认 Minimum）。
 * @param      vPolicy 垂直尺寸策略（Qt 默认 Minimum）。
 * @return     新空白条目；失败返回 NULL。
 */
XSpacerItem* XSpacerItem_create(int width, int height,
                                XWidgetSizePolicyPolicy hPolicy,
                                XWidgetSizePolicyPolicy vPolicy);

/**
 * @brief      修改空白条目的首选尺寸与尺寸策略
 *             （对标 QSpacerItem::changeSize）。
 * @details    已挂入布局后调用本函数不会自动触发重排：需要随后调用
 *             XLayout_invalidate 使布局失效，下一次几何分配才生效
 *             （与 Qt 文档一致）。
 * @param      self 目标空白条目；可为 NULL。
 * @param      width  新首选宽度。
 * @param      height 新首选高度。
 * @param      hPolicy 新水平尺寸策略。
 * @param      vPolicy 新垂直尺寸策略。
 */
void XSpacerItem_changeSize(XSpacerItem* self, int width, int height,
                            XWidgetSizePolicyPolicy hPolicy,
                            XWidgetSizePolicyPolicy vPolicy);

/**
 * @brief      返回空白条目的尺寸策略（对标 QSpacerItem::sizePolicy）。
 * @details    水平/垂直策略来自 m_hPolicy / m_vPolicy；控件类型默认
 *             DefaultType、拉伸因子 0、无 hfw/wfh 标志（与 Qt 使用
 *             QSizePolicy(hPolicy, vPolicy) 构造一致）。
 * @param      self 目标空白条目；可为 NULL。
 * @return     尺寸策略；失败返回空策略（Preferred/Preferred）。
 */
XWidgetSizePolicy XSpacerItem_sizePolicy(const XSpacerItem* self);

#endif /* XLAYOUT_SPACER_ON */

#endif /* XLAYOUT_ON */

#ifdef __cplusplus
}
#endif
#endif /* XSPACERITEM_H */
