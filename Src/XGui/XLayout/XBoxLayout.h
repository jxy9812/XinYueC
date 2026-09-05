/******************************************************************************
 * @file       XBoxLayout.h
 * @brief      XBoxLayout 盒式布局（对标 Qt 6.8 QBoxLayout/QHBoxLayout/QVBoxLayout）。
 * @details    XBoxLayout 继承 XLayout，把子条目沿主轴排成一行/一列：
 *             - 方向：LeftToRight（水平左→右）、RightToLeft（水平右→左）、
 *               TopToBottom（垂直上→下）、BottomToTop（垂直下→上）；
 *             - 条目管理：addWidget/addLayout/addSpacing/addStretch 与
 *               对应 insert* 系列（对标 QBoxLayout 同名 API）；
 *             - 伸展因子：setStretch/stretch 与 setStretchFactor（控件/子布局
 *               两个重载）；多余空间按 stretch 比例分配，stretch 缺失时
 *               提供给可伸展（expanding）条目，否则留在主轴末尾；
 *             - 尺寸协商：sizeHint/minimumSize/maximumSize/expandingDirections、
 *               heightForWidth 体系（子条目任一支持 hfw 即启用）；
 *             - 交叉轴对齐：条目 alignment==0 时填满交叉轴；设置了水平/垂直
 *               对齐位后按对齐收拢到首选尺寸（对齐数值与 Qt::Alignment 一致）；
 *             - 便捷类型 XHBoxLayout / XVBoxLayout（typedef）与对应的
 *               *_create(parent) 工厂（parent 可选，对标 Qt 同名构造）；
 *             - 间距：继承 XLayout 的 setSpacing/spacing（-1 沿用父级）；
 *               边距/尺寸约束/挂接均继承 XLayout。
 * @note       内部条目的 addWidget/addSpacing/addStretch 归布局所有（布局
 *             deinit 时释放）；addLayout/addItem 传入的条目不取所有权。
 *             模块开关 XLAYOUT_BOX_ON（XLayout_config.h），置 0 时本类
 *             公共 API 全部裁剪。不依赖任何平台 API，嵌入式可用。
 * @author     XinYueC 团队
 ******************************************************************************/
#ifndef XBOXLAYOUT_H
#define XBOXLAYOUT_H
#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>
#include "XGuiConfig.h"
#include "XLayout.h"
#include "XSpacerItem.h"
#include "XLayout_config.h"

#if XLAYOUT_ON && XLAYOUT_BOX_ON

/* ==================== 布局方向（对标 QBoxLayout::Direction） ==================== */

/**
 * @brief      盒式布局主轴方向（对标 Qt 6.8 QBoxLayout::Direction）。
 */
typedef enum XBoxLayoutDirection
{
    XBoxLayoutDirection_LeftToRight  = 0, /**< 水平，条目从左到右排列。 */
    XBoxLayoutDirection_RightToLeft  = 1, /**< 水平，条目从右到左排列。 */
    XBoxLayoutDirection_TopToBottom  = 2, /**< 垂直，条目从上到下排列。 */
    XBoxLayoutDirection_BottomToTop  = 3  /**< 垂直，条目从下到上排列。 */
} XBoxLayoutDirection;

/* ==================== 虚函数表（不新增槽位，仅重载 XLayout 槽位） ==================== */

/**
 * @brief XBoxLayout 虚函数表枚举：继承 XLayout 全部槽位，不新增虚函数。
 */
XCLASS_DEFINE_BEGING(XBoxLayout)
XCLASS_DEFINE_EXTEND_END(XBoxLayout, XLayout)

/** @brief 整齐排列的条目伸展数组长度起点；内部随条目数动态增长。 */
#define XBOXLAYOUT_STRETCH_CHUNK 8

/**
 * @brief      XBoxLayout 盒式布局对象；m_base 必须是第一个成员。
 * @details    字段含义（实现内部状态，调用方不得直接修改）：
 *             - m_direction：主轴方向；
 *             - m_stretches：与条目数组一一平行的伸展因子数组（0=不参与
 *               多余空间分配；越大分配比例越高），插入/取出条目时同步维护。
 */
typedef struct XBoxLayout
{
    XLayout                m_base;          /**< 基类成员；必须是第一个。 */
    XBoxLayoutDirection    m_direction;     /**< 主轴方向。 */
    int*                   m_stretches;     /**< 伸展因子数组（与条目平行）。 */
    int                    m_stretchCapacity; /**< 伸展数组容量。 */
} XBoxLayout;

/** @brief 水平盒式布局便捷类型（对标 QHBoxLayout）。 */
typedef XBoxLayout XHBoxLayout;
/** @brief 垂直盒式布局便捷类型（对标 QVBoxLayout）。 */
typedef XBoxLayout XVBoxLayout;

/* ==================== 类初始化与生命周期 ==================== */

/**
 * @brief      初始化 XBoxLayout 类虚函数表并返回共享表指针。
 * @return     XBoxLayout 类的共享 XVtable 指针。
 */
XVtable* XBoxLayout_class_init(void);

/**
 * @brief      初始化盒式布局（对标 QBoxLayout::QBoxLayout(Direction)）。
 * @param      self 待初始化的布局对象；不可为 NULL。
 * @param      direction 主轴方向。
 */
void XBoxLayout_init(XBoxLayout* self, XBoxLayoutDirection direction);

/**
 * @brief      创建水平盒式布局（对标 QHBoxLayout(QWidget* parent)）。
 * @param      parent 可选父控件；非 NULL 时自动挂接
 *             （XWidget_setLayout），布局本身仍为调用方所有。
 * @return     新布局对象；失败返回 NULL，调用方用 XLayout_delete_base 释放。
 */
XHBoxLayout* XHBoxLayout_create(XWidget* parent);

/**
 * @brief      创建垂直盒式布局（对标 QVBoxLayout(QWidget* parent)）。
 * @param      parent 可选父控件；非 NULL 时自动挂接。
 * @return     新布局对象；失败返回 NULL。
 */
XVBoxLayout* XVBoxLayout_create(XWidget* parent);

/**
 * @brief      创建指定方向的盒式布局。
 * @param      direction 主轴方向。
 * @param      parent 可选父控件；非 NULL 时自动挂接。
 * @return     新布局对象；失败返回 NULL。
 */
XBoxLayout* XBoxLayout_create(XBoxLayoutDirection direction, XWidget* parent);

/** @brief 通过 XClass 虚表释放盒式布局资源。 */
#define XBoxLayout_deinit_base(self) XClass_deinit_base((XClass*)(self))
/** @brief 删除堆上的盒式布局对象。 */
#define XBoxLayout_delete_base(self) XClass_delete_base((XClass*)(self))
/**
 * @brief 深拷贝盒式布局配置，不复制条目树。
 * @details self 与 other 必须是同一具体盒式布局类型且都已由 XBoxLayout_init
 *          或创建函数初始化；操作复用目标对象，不检查或初始化未构造的存储。
 */
#define XBoxLayout_copy_base(self, other) \
    XClass_copy_base((XClass*)(self), (const XClass*)(other))
/**
 * @brief 移动盒式布局及条目所有权。
 * @details self 与 other 必须是同一具体盒式布局类型且都已由 XBoxLayout_init
 *          或创建函数初始化；操作复用目标对象并释放其原有伸展/条目资源，
 *          不检查或初始化未构造的存储。
 */
#define XBoxLayout_move_base(self, other) \
    XClass_move_base((XClass*)(self), (XClass*)(other))

/* ==================== 方向访问 ==================== */

/**
 * @brief      设置盒式布局方向（对标 QBoxLayout::setDirection）。
 * @param      self 目标布局；可为 NULL。
 * @param      direction 新方向。
 */
void XBoxLayout_setDirection(XBoxLayout* self, XBoxLayoutDirection direction);

/**
 * @brief      返回盒式布局方向（对标 QBoxLayout::direction）。
 * @param      self 目标布局；可为 NULL。
 * @return     当前方向；失败返回 LeftToRight。
 */
XBoxLayoutDirection XBoxLayout_direction(const XBoxLayout* self);

/* ==================== 追加条目（对标 QBoxLayout add* 系列） ==================== */

/**
 * @brief      追加控件（对标 QBoxLayout::addWidget）。
 * @details    自动创建内部控件条目（归布局所有，布局 deinit 时释放）；
 *             控件本身为借用，不取得所有权；伸展因子默认 0。
 * @param      self 目标布局；可为 NULL。
 * @param      widget 控件借用指针；可为 NULL（追加空条目并忽略）。
 */
void XBoxLayout_addWidget(XBoxLayout* self, XWidget* widget);

/**
 * @brief      追加控件并指定伸展因子与对齐（对标 QBoxLayout::addWidget
 *             的 stretch/alignment 重载）。
 * @details    伸展因子只作用于主轴（direction 方向），相对其他条目按
 *             比例分配多余空间；对齐标志作用于交叉轴，0=填满整个单元格。
 * @param      self 目标布局；可为 NULL。
 * @param      widget 控件借用指针；可为 NULL（忽略）。
 * @param      stretch 伸展因子。
 * @param      alignment 条目对齐标志（0=填满整格）。
 */
void XBoxLayout_addWidgetEx(XBoxLayout* self, XWidget* widget, int stretch,
                            XLayoutAlignments alignment);

/**
 * @brief      追加子布局（对标 QBoxLayout::addLayout）。
 * @details    子布局对象按借用指针挂入（不取所有权、不入计数生命周期），
 *             其父布局自动登记为 self。
 * @param      self 目标布局；可为 NULL。
 * @param      child 子布局借用指针；可为 NULL（忽略）。
 */
void XBoxLayout_addLayout(XBoxLayout* self, XLayout* child);

/**
 * @brief      追加子布局并指定伸展因子（对标 QBoxLayout::addLayout 的
 *             stretch 重载）。
 * @param      self 目标布局；可为 NULL。
 * @param      child 子布局借用指针；可为 NULL（忽略）。
 * @param      stretch 伸展因子。
 */
void XBoxLayout_addLayoutEx(XBoxLayout* self, XLayout* child, int stretch);

/**
 * @brief      追加固定空白（对标 QBoxLayout::addSpacing）。
 * @param      self 目标布局；可为 NULL。
 * @param      size 主轴方向固定空白尺寸（像素），负值按 0 处理。
 */
void XBoxLayout_addSpacing(XBoxLayout* self, int size);

/**
 * @brief      追加可伸展空白（对标 QBoxLayout::addStretch）。
 * @param      self 目标布局；可为 NULL。
 * @param      stretch 伸展因子（参与多余空间按比例分配）。
 */
void XBoxLayout_addStretch(XBoxLayout* self, int stretch);

#if XLAYOUT_SPACER_ON
/**
 * @brief      追加手动创建的空白条目（对标 QBoxLayout::addSpacerItem）。
 * @details    空白条目对象所有权转移给布局（布局 deinit 时一并释放，
 *             与 Qt 语义一致）；按 Qt 语义挂入后视为 magic 空白，
 *             setDirection 翻转主轴时按空白类型自动翻转横纵策略。
 * @param      self 目标布局；可为 NULL。
 * @param      item 空白条目借用指针；可为 NULL（忽略）。
 */
void XBoxLayout_addSpacerItem(XBoxLayout* self, XSpacerItem* item);
#endif /* XLAYOUT_SPACER_ON */

/**
 * @brief      限制交叉轴最小尺寸（对标 QBoxLayout::addStrut）。
 * @details    水平盒强制交叉轴（高度）不小于 size；垂直盒强制交叉轴
 *             （宽度）不小于 size。其他尺寸约束只会提高该下限。创建
 *             内部 magic 空白条目（归布局所有，布局 deinit 时释放）。
 * @param      self 目标布局；可为 NULL。
 * @param      size 交叉轴最小尺寸（像素），负值按 0 处理。
 */
void XBoxLayout_addStrut(XBoxLayout* self, int size);

/**
 * @brief      追加任意条目（对标 QBoxLayout::addItem）。
 * @details    条目按借用指针保存，释放责任仍在调用方；伸展因子默认 0。
 * @param      self 目标布局；可为 NULL。
 * @param      item 条目借用指针；可为 NULL（忽略）。
 */
void XBoxLayout_addItem(XBoxLayout* self, XLayoutItem* item);

/* ==================== 插入条目（对标 QBoxLayout insert* 系列） ==================== */

/**
 * @brief      在指定索引插入控件（对标 QBoxLayout::insertWidget）。
 * @param      self 目标布局；可为 NULL。
 * @param      index 插入位置；越界追到末尾。
 * @param      widget 控件借用指针；可为 NULL（插入空条目并忽略）。
 * @param      stretch 伸展因子。
 */
void XBoxLayout_insertWidget(XBoxLayout* self, int index, XWidget* widget,
                             int stretch);

/**
 * @brief      在指定索引插入控件并指定伸展因子与对齐（对标
 *             QBoxLayout::insertWidget 的 alignment 重载）。
 * @param      self 目标布局；可为 NULL。
 * @param      index 插入位置；越界追到末尾。
 * @param      widget 控件借用指针；可为 NULL（忽略）。
 * @param      stretch 伸展因子。
 * @param      alignment 条目对齐标志（0=填满整格）。
 */
void XBoxLayout_insertWidgetEx(XBoxLayout* self, int index, XWidget* widget,
                               int stretch, XLayoutAlignments alignment);

/**
 * @brief      在指定索引插入子布局（对标 QBoxLayout::insertLayout）。
 * @param      self 目标布局；可为 NULL。
 * @param      index 插入位置；越界追到末尾。
 * @param      child 子布局借用指针；可为 NULL（忽略）。
 * @param      stretch 伸展因子。
 */
void XBoxLayout_insertLayout(XBoxLayout* self, int index, XLayout* child,
                             int stretch);

/**
 * @brief      在指定索引插入固定空白（对标 QBoxLayout::insertSpacing）。
 * @param      self 目标布局；可为 NULL。
 * @param      index 插入位置；越界追到末尾。
 * @param      size 固定空白尺寸（像素），负值按 0 处理。
 */
void XBoxLayout_insertSpacing(XBoxLayout* self, int index, int size);

/**
 * @brief      在指定索引插入可伸展空白（对标 QBoxLayout::insertStretch）。
 * @param      self 目标布局；可为 NULL。
 * @param      index 插入位置；越界追到末尾。
 * @param      stretch 伸展因子。
 */
void XBoxLayout_insertStretch(XBoxLayout* self, int index, int stretch);

#if XLAYOUT_SPACER_ON
/**
 * @brief      在指定索引插入手动创建的空白条目（对标
 *             QBoxLayout::insertSpacerItem）。
 * @details    空白条目对象所有权转移给布局；按 Qt 语义挂入后视为 magic
 *             空白（setDirection 翻转主轴时按空白类型翻转横纵策略）。
 * @param      self 目标布局；可为 NULL。
 * @param      index 插入位置；越界追到末尾。
 * @param      item 空白条目借用指针；可为 NULL（忽略）。
 */
void XBoxLayout_insertSpacerItem(XBoxLayout* self, int index, XSpacerItem* item);
#endif /* XLAYOUT_SPACER_ON */

/**
 * @brief      在指定索引插入任意条目（对标 QBoxLayout::insertItem）。
 * @param      self 目标布局；可为 NULL。
 * @param      index 插入位置；越界追到末尾。
 * @param      item 条目借用指针；可为 NULL（忽略）。
 * @param      stretch 伸展因子。
 */
void XBoxLayout_insertItem(XBoxLayout* self, int index, XLayoutItem* item,
                           int stretch);

/* ==================== 伸展因子（对标 QBoxLayout） ==================== */

/**
 * @brief      设置指定索引条目的伸展因子（对标 QBoxLayout::setStretch）。
 * @param      self 目标布局；可为 NULL。
 * @param      index 条目索引。
 * @param      stretch 伸展因子（≥0）。
 */
void XBoxLayout_setStretch(XBoxLayout* self, int index, int stretch);

/**
 * @brief      返回指定索引条目的伸展因子（对标 QBoxLayout::stretch）。
 * @param      self 目标布局；可为 NULL。
 * @param      index 条目索引。
 * @return     伸展因子；越界或失败返回 0。
 */
int XBoxLayout_stretch(const XBoxLayout* self, int index);

/**
 * @brief      为指定控件设置伸展因子（对标 QBoxLayout::setStretchFactor）。
 * @param      self 目标布局；可为 NULL。
 * @param      widget 目标控件借用指针。
 * @param      stretch 伸展因子。
 * @return     控件存在于本布局时返回 true；否则返回 false。
 */
bool XBoxLayout_setStretchFactorWidget(XBoxLayout* self, XWidget* widget,
                                       int stretch);

/**
 * @brief      为指定子布局设置伸展因子（对标 QBoxLayout::setStretchFactor）。
 * @param      self 目标布局；可为 NULL。
 * @param      child 目标子布局借用指针。
 * @param      stretch 伸展因子。
 * @return     子布局存在于本布局时返回 true；否则返回 false。
 */
bool XBoxLayout_setStretchFactorLayout(XBoxLayout* self, XLayout* child,
                                       int stretch);

#endif /* XLAYOUT_ON && XLAYOUT_BOX_ON */

#ifdef __cplusplus
}
#endif
#endif /* XBOXLAYOUT_H */
