/******************************************************************************
 * @file       XLayout.h
 * @brief      XLayout 抽象布局基类（对标 Qt 6.8 QLayout）。
 * @details    XLayout 继承 XLayoutItem，是盒式/网格等具体布局的公共基类，
 *             与 Qt 的 QLayout 一一对应：
 *             - 条目管理：addItem()/itemAt()/takeAt()/count() 四个虚函数
 *               （对标 QLayout 的纯虚函数）与 removeWidget()/removeItem()/
 *               indexOf() 通用入口；
 *             - 几何与尺寸协商：继承 XLayoutItem 的 sizeHint/minimumSize/
 *               maximumSize/heightForWidth 等虚函数，具体算法由子类实现，
 *               布局本身作为条目再次挂入父布局时按同一套协议参与分配；
 *             - 内容边距：setContentsMargins()/contentsMargins()/
 *               getContentsMargins()（对标 QLayout 同名 API）；
 *             - 间距：setSpacing()/spacing()（-1 表示沿用父布局/样式默认；
 *               QGridLayout 在此基础上再提供行列独立间距）；
 *             - 激活与失效：activate()/update()/invalidate()——activate
 *               强制重新执行一次性布局解算，invalidate 只标记需要重算，
 *               下一次几何分配时生效；
 *             - 尺寸约束：setSizeConstraint()/sizeConstraint()（对标
 *               QLayout::SizeConstraint，作用于挂接控件的最小/最大尺寸）；
 *             - 对齐：setAlignment()（控件/子布局/条目三个重载）、
 *               alignmentRect()；
 *             - 挂接：parentWidget()（含子布局，沿父布局链上溯）；
 *             - 条目数组与挂接字段公开仅供实现读取，调用方不得直接修改。
 *             本类与 QLayout 一样为抽象基类：库内仅提供 XBoxLayout 与
 *             XGridLayout 两个具体实现，调用方不应直接实例化 XLayout。
 * @note       布局数组条目所有权约定：addWidget/addSpacing 等由布局自动
 *             创建的内部条目归布局所有（deinit 时自动释放）；经
 *             addItem/addLayout 显式传入的条目以及 addLayout 的子布局
 *             对象只按借用指针管理，释放责任仍在调用方。本模块不依赖
 *             任何平台 API，嵌入式可用。
 * @note       模块总开关 XLAYOUT_ON 定义于 XGuiConfig.h；置 0 时裁剪
 *             整个布局系统公共 API（含本类）。
 * @author     XinYueC 团队
 ******************************************************************************/
#ifndef XLAYOUT_H
#define XLAYOUT_H
#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>
#include "XGuiConfig.h"
#include "XLayoutItem.h"
#include "XLayout_config.h"

#if XLAYOUT_ON

/* ==================== 尺寸约束（对标 QLayout::SizeConstraint） ==================== */

/**
 * @brief      布局尺寸约束（对标 Qt 6.8 QLayout::SizeConstraint，数值一致）。
 * @details    布局挂接到控件后，激活时会按约束把布局的最小/首选/最大
 *             尺寸写回控件，约束控件能被压小/放大的范围。SetDefault
 *             按“至少容纳最小尺寸”处理（与 Qt 缺省一致）。
 */
typedef enum XLayoutSizeConstraint
{
    XLayoutSizeConstraint_SetDefault       = 0, /**< 默认：控件最小尺寸取布局最小尺寸。 */
    XLayoutSizeConstraint_SetNoConstraint  = 1, /**< 不约束控件尺寸。 */
    XLayoutSizeConstraint_SetMinimumSize   = 2, /**< 控件最小尺寸取布局最小尺寸。 */
    XLayoutSizeConstraint_SetFixedSize     = 3, /**< 控件固定为布局首选尺寸。 */
    XLayoutSizeConstraint_SetMaximumSize   = 4, /**< 控件最大尺寸取布局最大尺寸。 */
    XLayoutSizeConstraint_SetMinAndMaxSize = 5  /**< 控件最小/最大尺寸取布局对应尺寸。 */
} XLayoutSizeConstraint;

/* ==================== 虚函数表（对标 QLayout 纯虚函数） ==================== */

/**
 * @brief XLayout 虚函数表枚举。
 * @details 全部槽位继承 XLayoutItem（XClass 3 槽 + XLayoutItem 15 槽）；
 *          新增 5 个槽位分别对标 QLayout 的 addItem() / itemAt() /
 *          takeAt() / count()，以及 QLayoutPrivate::replaceAt()
 *          （replaceWidget 的“原位替换”内部钩子，基类默认拒绝并返回
 *          NULL，盒式/网格布局覆盖实现原位替换且不触碰关联单元格/
 *          伸展因子）。 */
XCLASS_DEFINE_BEGING(XLayout)
XCLASS_DEFINE_ENUM(XLayout, AddItem) = XCLASS_VTABLE_GET_SIZE(XLayoutItem),
XCLASS_DEFINE_ENUM(XLayout, ItemAt),
XCLASS_DEFINE_ENUM(XLayout, TakeAt),
XCLASS_DEFINE_ENUM(XLayout, Count),
XCLASS_DEFINE_ENUM(XLayout, ReplaceItemAt),
XCLASS_DEFINE_END(XLayout)

/**
 * @brief      XLayout 布局对象；m_base 必须是第一个成员。
 * @details    字段含义（实现内部状态，调用方不得直接修改）：
 *             - m_items：条目数组，元素为 XLayoutItem*；
 *             - m_itemCount/m_itemCapacity：条目数与数组容量；
 *             - m_parentWidget：挂接的控件借用指针（顶层布局）；
 *             - m_parentLayout：父布局借用指针（子布局）；
 *             - m_menuBar：布局菜单栏借用指针（对标 QLayout::setMenuBar；
 *               total* 尺寸把菜单栏高度附加到结果中，默认 NULL）；
 *             - m_contentsMargins：内容边距原始值（默认全 -1 = 未设置，
 *               X 无样式时解析为 0；setContentsMargins(-1,...) 即
 *               unsetContentsMargins）；
 *             - m_spacing：条目间距（-1=沿用父级/默认，最终 0）；
 *             - m_sizeConstraint：尺寸约束（默认 SetDefault）；
 *             - m_alignmentRectCache：alignmentRect 缓存（内部）；
 *             - m_cachedMin/m_cachedMax/m_cachedHint：尺寸缓存（内部）；
 *             - m_isDirty：尺寸缓存失效标志；
 *             - m_activated：已成功激活标志；
 *             - m_enabled：布局启用标志（默认 true，对标 QLayout::
 *               setEnabled/isEnabled；当前布局算法不因禁用隐藏条目，
 *               Qt 语义同样仅保存标志供 childEvent/事件处理使用）。
 */
typedef struct XLayout
{
    XLayoutItem         m_base;            /**< 基类成员；必须是第一个。 */
    XLayoutItem**       m_items;           /**< 条目数组（借用/内部拥有指针）。 */
    int                 m_itemCount;       /**< 条目数量。 */
    int                 m_itemCapacity;    /**< 条目数组容量。 */
    XWidget*            m_parentWidget;    /**< 挂接控件借用指针。 */
    XLayout*            m_parentLayout;    /**< 父布局借用指针。 */
    XWidget*            m_menuBar;         /**< 菜单栏借用指针（total* 使用）。 */
    XMargins            m_contentsMargins; /**< 内容边距原始值（-1=未设置）。 */
    int                 m_spacing;         /**< 条目间距；-1 表示沿用默认。 */
    XLayoutSizeConstraint m_sizeConstraint;/**< 尺寸约束。 */
    XRect               m_alignmentRectCache; /**< alignmentRect 缓存。 */
    XSize               m_cachedMin;       /**< 最小尺寸缓存（失效后由子类重算）。 */
    XSize               m_cachedMax;       /**< 最大尺寸缓存。 */
    XSize               m_cachedHint;      /**< 首选尺寸缓存。 */
    uint32_t            m_isDirty : 1;     /**< 尺寸缓存失效标志。 */
    uint32_t            m_activated : 1;   /**< 已成功激活标志。 */
    uint32_t            m_enabled : 1;     /**< 布局启用标志（默认 true）。 */
    uint32_t            m_reservedLayout : 29; /**< 保留位。 */
} XLayout;

/* ==================== 类初始化与生命周期 ==================== */

/**
 * @brief      初始化 XLayout 类虚函数表并返回共享表指针。
 * @return     XLayout 类的共享 XVtable 指针。
 */
XVtable* XLayout_class_init(void);

/**
 * @brief      初始化空布局（对标 QLayout 受保护构造）。
 * @details    仅在子类构造函数中调用；内部条目数组初始为空，内容边距
 *             全 -1（未设置，X 无样式时代码默认 0）、间距 -1、约束
 *             SetDefault、菜单栏 NULL、启用标志 true。调用方不得直接
 *             实例化 XLayout。
 * @param      self 待初始化的布局对象；不可为 NULL。
 */
void XLayout_init(XLayout* self);

/** @brief 通过 XClass 虚表释放布局资源（栈/外部存储对象使用）。 */
#define XLayout_deinit_base(self)  XClass_deinit_base((XClass*)(self))
/** @brief 删除堆上的布局对象。 */
#define XLayout_delete_base(self)  XClass_delete_base((XClass*)(self))
/** @brief 通过 XClass 虚表深拷贝布局（仅复制配置与标量字段，不复制条目树）。 */
#define XLayout_copy_base(self, other) \
    XClass_copy_base((XClass*)(self), (const XClass*)(other))
/** @brief 通过 XClass 虚表移动布局（转移内部条目数组所有权）。 */
#define XLayout_move_base(self, other) \
    XClass_move_base((XClass*)(self), (XClass*)(other))

/* ==================== 条目管理虚函数（对标 QLayout 纯虚函数） ==================== */

/**
 * @brief      向布局追加一个条目（虚函数入口，对标 QLayout::addItem）。
 * @details    具体语义由子类覆盖（盒式追加到末尾、网格追加到下一个
 *             空位）。条目对象只按借用指针保存；由调用方创建的条目
 *             释放责任仍在调用方。
 * @param      self 目标布局；可为 NULL。
 * @param      item 条目借用指针；可为 NULL（忽略）。
 */
void XLayout_addItem_base(XLayout* self, XLayoutItem* item);

/**
 * @brief      返回指定索引的条目（对标 QLayout::itemAt）。
 * @param      self 目标布局；可为 NULL。
 * @param      index 条目索引，从 0 开始。
 * @return     条目借用指针；越界或失败返回 NULL。
 */
XLayoutItem* XLayout_itemAt_base(const XLayout* self, int index);

/**
 * @brief      取出并移除指定索引的条目（对标 QLayout::takeAt）。
 * @details    条目从布局中移除但对象仍归调用方所有（即使原先是布局
 *             内部创建的条目，取走内置条目后释放责任也转移到调用方）。
 * @param      self 目标布局；可为 NULL。
 * @param      index 条目索引，从 0 开始。
 * @return     条目借用指针；越界或失败返回 NULL。
 */
XLayoutItem* XLayout_takeAt_base(XLayout* self, int index);

/**
 * @brief      返回布局中的条目数量（对标 QLayout::count）。
 * @param      self 目标布局；可为 NULL。
 * @return     条目数量；失败返回 0。
 */
int XLayout_count_base(const XLayout* self);

/**
 * @brief      原位替换指定索引条目（虚函数入口；对标
 *             QLayoutPrivate::replaceAt）。
 * @details    基类默认拒绝替换并返回 NULL；XBoxLayout 覆盖实现“原地换出
 *             条目对象但保留该位置的伸展因子”，XGridLayout 覆盖实现
 *             “原地换出条目对象但保留该单元格位置”。返回值是旧条目
 *             （所有权转移给调用方）；替换失败返回 NULL。
 * @param      self 目标布局；可为 NULL。
 * @param      index 条目索引。
 * @param      item 新条目借用指针；可为 NULL（视为替换失败）。
 * @return     被换出的旧条目；失败返回 NULL。
 */
XLayoutItem* XLayout_replaceItemAt_base(XLayout* self, int index,
                                        XLayoutItem* item);

/* ==================== 通用条目管理（对标 QLayout） ==================== */

/**
 * @brief      移除包装指定控件的条目（对标 QLayout::removeWidget）。
 * @details    只移除条目，不删除控件；被移除的内部条目对象由调用方
 *             使用 XLayoutItem_delete_base 释放或析构管理。
 * @param      self 目标布局；可为 NULL。
 * @param      widget 目标控件借用指针；可为 NULL（忽略）。
 */
void XLayout_removeWidget(XLayout* self, XWidget* widget);

/**
 * @brief      移除指定条目（对标 QLayout::removeItem）。
 * @details    只移除条目；被移除的内部条目对象由调用方使用
 *             XLayoutItem_delete_base 释放或析构管理。
 * @param      self 目标布局；可为 NULL。
 * @param      item 目标条目借用指针；可为 NULL（忽略）。
 */
void XLayout_removeItem(XLayout* self, XLayoutItem* item);

/**
 * @brief      返回包装指定控件的条目索引（对标 QLayout::indexOf(QWidget*)）。
 * @param      self 目标布局；可为 NULL。
 * @param      widget 目标控件借用指针。
 * @return     条目索引；未找到或失败返回 -1。
 */
int XLayout_indexOf(const XLayout* self, const XWidget* widget);

/**
 * @brief      返回指定条目的索引（对标 QLayout::indexOf(QLayoutItem*)）。
 * @details    与 Qt 一致使用 itemAt() 虚函数线性扫描（不直接读内部
 *             数组），对自定义布局子类依然有效。
 * @param      self 目标布局；可为 NULL。
 * @param      item 目标条目指针；可为 NULL。
 * @return     条目索引；未找到或失败返回 -1。
 */
int XLayout_indexOfItem(XLayout* self, const XLayoutItem* item);

/**
 * @brief      用 to 控件替换布局中 from 控件的条目（对标
 *             QLayout::replaceWidget）。
 * @details    行为与 Qt 6.8 一致：
 *             - from 与 to 相同时返回 NULL（所有权仍归布局，未做任何
 *               修改）；
 *             - 只扫描本布局的条目；recursive 为 true 时递归到子布局
 *               查找（对标 Qt::FindChildrenRecursively）；
 *             - 找到后新建控件条目（对齐沿用旧条目的对齐），经
 *               ReplaceItemAt 槽原位替换；子类（盒式/网格）保留该位置
 *               的伸展因子/单元格；
 *             - 成功时返回旧条目（所有权转移给调用方，旧控件不再被本
 *               布局管理）；失败（未找到/替换被拒绝）返回 NULL 并销毁
 *               新建条目。
 * @param      self 目标布局；可为 NULL。
 * @param      from 被替换控件借用指针。
 * @param      to 新控件借用指针。
 * @param      recursive 是否递归查找子布局（true=FindChildrenRecursively）。
 * @return     旧条目（调用方负责释放）；未找到或失败返回 NULL。
 */
#if XLAYOUT_TOTAL_ON
XLayoutItem* XLayout_replaceWidget(XLayout* self, XWidget* from,
                                   XWidget* to, bool recursive);
#endif /* XLAYOUT_TOTAL_ON */

/**
 * @brief      查找并返回包装指定控件的条目（对标 QLayout::itemAt(indexOf)）。
 * @param      self 目标布局；可为 NULL。
 * @param      widget 目标控件借用指针。
 * @return     控件条目借用指针；未找到或失败返回 NULL。
 */
XLayoutItem* XLayout_itemForWidget(const XLayout* self, const XWidget* widget);

/**
 * @brief      为挂接控件设置对齐（对标 QLayout::setAlignment(QWidget*, ...)）。
 * @details    之后该控件的条目按给定对齐摆放；affects 后续几何分配。
 * @param      self 目标布局；可为 NULL。
 * @param      widget 目标控件借用指针；可为 NULL。
 * @param      alignment 对齐标志组合。
 */
void XLayout_setAlignmentWidget(XLayout* self, XWidget* widget,
                                XLayoutAlignments alignment);

/**
 * @brief      为挂接的子布局设置对齐（对标 QLayout::setAlignment(QLayout*, ...)）。
 * @param      self 目标布局；可为 NULL。
 * @param      child 子布局借用指针；可为 NULL。
 * @param      alignment 对齐标志组合。
 */
void XLayout_setAlignmentLayout(XLayout* self, XLayout* child,
                                XLayoutAlignments alignment);

/**
 * @brief      为任意条目设置对齐并返回是否成功（内部增强）。
 * @param      self 目标布局；可为 NULL。
 * @param      item 目标条目借用指针；可为 NULL。
 * @param      alignment 对齐标志组合。
 * @return     条目属于本布局且设置成功返回 true；否则返回 false。
 */
bool XLayout_setAlignmentItem(XLayout* self, XLayoutItem* item,
                              XLayoutAlignments alignment);

/**
 * @brief      删除布局中所有条目（对标 QLayout::deleteAllItems）。
 * @details    布局内部创建的条目对象被释放；调用方创建的借用条目只从
 *             数组移除。布局本身保持不变，可继续使用。
 * @param      self 目标布局；可为 NULL。
 */
void XLayout_deleteAllItems(XLayout* self);

/* ==================== 内容边距 / 间距 / 对齐矩形（对标 QLayout） ==================== */

/**
 * @brief      设置布局内容边距（对标 QLayout::setContentsMargins）。
 * @param      self 目标布局；可为 NULL。
 * @param      left 左边距。
 * @param      top 上边距。
 * @param      right 右边距。
 * @param      bottom 下边距。
 */
void XLayout_setContentsMargins(XLayout* self, int left, int top,
                                int right, int bottom);

/**
 * @brief      返回布局内容边距（对标 QLayout::contentsMargins）。
 * @param      self 目标布局；可为 NULL。
 * @return     内容边距（空指针返回全 0）。
 */
XMargins XLayout_contentsMargins(const XLayout* self);

/**
 * @brief      输出布局内容边距（对标 QLayout::getContentsMargins）。
 * @param      self 目标布局；可为 NULL。
 * @param      left/top/right/bottom 输出参数；可为 NULL 跳过对应项。
 */
void XLayout_getContentsMargins(const XLayout* self,
                                int* left, int* top, int* right, int* bottom);

/**
 * @brief      清除自定义内容边距，回到默认（对标 QLayout::
 *             unsetContentsMargins）。
 * @details    等效 setContentsMargins(-1,-1,-1,-1)；之后
 *             contentsMargins 按“未设置”解析（X 无样式 → 0）。
 * @param      self 目标布局；可为 NULL。
 */
#if XLAYOUT_TOTAL_ON
void XLayout_unsetContentsMargins(XLayout* self);
#endif /* XLAYOUT_TOTAL_ON */

/**
 * @brief      返回布局当前几何扣除内容边距后的矩形（对标
 *             QLayout::contentsRect）。
 * @details    等价 Qt 的 d->rect.adjusted(left, top, -right, -bottom)：
 *             以条目最近一次分配到的几何（geometry()）为基准，向内
 *             收缩解析后的内容边距；矩形宽度/高度不小于 0。布局尚未
 *             分配过几何时返回全 0 矩形。
 * @param      self 目标布局；可为 NULL。
 * @return     内容矩形；失败返回全 0 矩形。
 */
#if XLAYOUT_TOTAL_ON
XRect XLayout_contentsRect(const XLayout* self);
#endif /* XLAYOUT_TOTAL_ON */

/**
 * @brief      设置布局条目间距（对标 QLayout::setSpacing）。
 * @param      self 目标布局；可为 NULL。
 * @param      spacing 间距像素；-1 表示沿用父布局/默认（0）。
 */
void XLayout_setSpacing(XLayout* self, int spacing);

/**
 * @brief      返回布局间距（对标 QLayout::spacing）。
 * @param      self 目标布局；可为 NULL。
 * @return     间距像素；未设置(-1)或失败时返回 -1（可用
 *             XLayout_effectiveSpacing 取得生效值）。
 */
int XLayout_spacing(const XLayout* self);

/**
 * @brief      返回条目在给定矩形内按自身对齐产生的实际矩形
 *             （对标 QLayout::alignmentRect）。
 * @param      self 目标布局；可为 NULL。
 * @param      rect 分配矩形；可为 NULL 时按全 0 矩形处理。
 * @return     对齐后的矩形；条目不可伸展时收拢到首选尺寸并按对齐摆放。
 */
XRect XLayout_alignmentRect(const XLayout* self, const XRect* rect);

/* ==================== 尺寸约束（对标 QLayout） ==================== */

/**
 * @brief      设置布局尺寸约束（对标 QLayout::setSizeConstraint）。
 * @param      self 目标布局；可为 NULL。
 * @param      constraint 约束枚举值。
 */
void XLayout_setSizeConstraint(XLayout* self, XLayoutSizeConstraint constraint);

/**
 * @brief      返回布局尺寸约束（对标 QLayout::sizeConstraint）。
 * @param      self 目标布局；可为 NULL。
 * @return     约束枚举值；失败返回 SetDefault。
 */
XLayoutSizeConstraint XLayout_sizeConstraint(const XLayout* self);

#if XLAYOUT_TOTAL_ON

/* ==================== 菜单栏 / 启用标志 / total* 尺寸（对标 QLayout） ==================== */

/**
 * @brief      设置布局菜单栏（对标 QLayout::setMenuBar）。
 * @details    菜单栏按借用指针保存（不取得所有权）；totalMinimumSize /
 *             totalSizeHint / totalMaximumSize / totalMinimumHeightForWidth /
 *             totalHeightForWidth 会把菜单栏高度附加到结果顶部，使顶层
 *             控件为菜单栏预留空间。控件本身仍需由调用方管理可见性与
 *             几何。
 * @param      self 目标布局；可为 NULL。
 * @param      widget 菜单栏控件借用指针；可为 NULL（清除）。
 */
void XLayout_setMenuBar(XLayout* self, XWidget* widget);

/**
 * @brief      返回布局菜单栏（对标 QLayout::menuBar）。
 * @param      self 目标布局；可为 NULL。
 * @return     菜单栏借用指针；未设置或失败返回 NULL。
 */
XWidget* XLayout_menuBar(const XLayout* self);

/**
 * @brief      设置布局启用标志（对标 QLayout::setEnabled）。
 * @details    与 Qt 一致：QLayout::setEnabled 仅保存启用标志（默认
 *             true），供事件过滤等场景使用；布局算法不会因禁用而隐藏
 *             条目。禁用父布局时 Qt 不再响应子条目移除事件，X 当前
 *             没有 Qt 的 QObject childEvent 机制，这里仅对齐公开接口
 *             语义。
 * @param      self 目标布局；可为 NULL。
 * @param      enable 是否启用。
 */
void XLayout_setEnabled(XLayout* self, bool enable);

/**
 * @brief      查询布局启用标志（对标 QLayout::isEnabled）。
 * @param      self 目标布局；可为 NULL。
 * @return     启用返回 true；失败返回 false。
 */
bool XLayout_isEnabled(const XLayout* self);

/**
 * @brief      返回包含内容边距与菜单栏高度的首选尺寸（对标
 *             QLayout::totalSizeHint；Qt 标记为 internal，仍属公开
 *             接口）。
 * @details    对 topLevel 布局（m_parentWidget 非 NULL）按 Qt 语义把
 *             控件 frame margins 计为 0（X 无 QStyle/frame）并附加
 *             菜单栏高度；布局支持 heightForWidth 时高度按首选宽度
 *             对应高度取值。
 * @param      self 目标布局；可为 NULL。
 * @return     合计首选尺寸；失败返回 (0,0)。
 */
XSize XLayout_totalSizeHint(const XLayout* self);

/**
 * @brief      返回包含内容边距与菜单栏高度的最小尺寸（对标
 *             QLayout::totalMinimumSize）。
 * @param      self 目标布局；可为 NULL。
 * @return     合计最小尺寸；失败返回 (0,0)。
 */
XSize XLayout_totalMinimumSize(const XLayout* self);

/**
 * @brief      返回包含内容边距与菜单栏高度的最大尺寸（对标
 *             QLayout::totalMaximumSize）。
 * @param      self 目标布局；可为 NULL。
 * @return     合计最大尺寸；失败返回 (XWIDGET_MAX_SIZE, XWIDGET_MAX_SIZE)。
 */
XSize XLayout_totalMaximumSize(const XLayout* self);

/**
 * @brief      返回包含菜单栏高度的最小高度（给定宽度；对标
 *             QLayout::totalMinimumHeightForWidth）。
 * @param      self 目标布局；可为 NULL。
 * @param      width 宽度（像素）。
 * @return     合计最小高度；失败返回 0。
 */
int XLayout_totalMinimumHeightForWidth(const XLayout* self, int width);

/**
 * @brief      返回包含菜单栏高度的首选高度（给定宽度；对标
 *             QLayout::totalHeightForWidth）。
 * @param      self 目标布局；可为 NULL。
 * @param      width 宽度（像素）。
 * @return     合计首选高度；失败返回 0。
 */
int XLayout_totalHeightForWidth(const XLayout* self, int width);

/**
 * @brief      返回满足控件全部尺寸约束且尽可能接近 size 的尺寸
 *             （对标 Qt 6.8 QLayout::closestAcceptableSize 静态接口）。
 * @details    Qt 中该函数是 QLayout 的 static 成员（参数只有控件与
 *             目标尺寸），本实现按同签名提供模块级入口，内部使用
 *             widget 挂接的布局计算 heightForWidth 约束：
 *             - result = size 收缩到控件最大尺寸、再展开到最小尺寸；
 *             - 布局有 heightForWidth 且 result 高度小于
 *               minimumHeightForWidth(result.width) 时，按 Qt 原算法
 *               补高（常数 hfw 与垂直收缩与逐宽二分三种分支）。
 * @param      widget 目标控件借用指针；可为 NULL。
 * @param      size 期望尺寸。
 * @return     满足约束的尺寸；widget 为 NULL 时返回 (0,0)。
 */
XSize XLayout_closestAcceptableSize(const XWidget* widget, XSize size);

#endif /* XLAYOUT_TOTAL_ON */

/* ==================== 挂接与激活（对标 QLayout） ==================== */

/**
 * @brief      返回布局挂接的控件（对标 QLayout::parentWidget）。
 * @details    子布局沿父布局链上溯到顶层布局再返回其挂接控件。
 * @param      self 目标布局；可为 NULL。
 * @return     控件借用指针；未挂接或失败返回 NULL。
 */
XWidget* XLayout_parentWidget(const XLayout* self);

/**
 * @brief      重新执行布局（对标 QLayout::activate）。
 * @details    使用挂接控件的客户区（或当前几何）作为分配矩形，重新
 *             解算所有条目几何；若布局尚未挂接控件且从未分配过几何，
 *             本函数直接返回 false。成功返回 true。
 * @param      self 目标布局；可为 NULL。
 * @return     本次重新解算是否执行；失败返回 false。
 */
bool XLayout_activate(XLayout* self);

/**
 * @brief      重画所有条目（对标 QLayout::update）。
 * @details    对布局挂接及包含的所有控件调用 update 请求重绘；布局
 *             本身未挂接控件时是 no-op。
 * @param      self 目标布局；可为 NULL。
 */
void XLayout_update(XLayout* self);

/**
 * @brief      使布局所有条目失效并安排重算（对标 QLayout::invalidate）。
 * @details    等效对自身与全部子条目调用 invalidate 虚函数，清除尺寸
 *             缓存；下一次 setGeometry/activate 时重新解算。
 * @param      self 目标布局；可为 NULL。
 */
void XLayout_invalidate(XLayout* self);

#endif /* XLAYOUT_ON */

#ifdef __cplusplus
}
#endif
#endif /* XLAYOUT_H */
