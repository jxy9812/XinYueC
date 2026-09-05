/******************************************************************************
 * @file       XLayoutItem.h
 * @brief      XLayoutItem 布局条目基类（对标 Qt 6.8 QLayoutItem）。
 * @details    XLayoutItem 继承 XClass，是整个布局系统的“单元”抽象，与
 *             Qt 的 QLayoutItem 一一对应：
 *             - 几何：geometry()/setGeometry()（虚函数，对标 QLayoutItem
 *               geometry/setGeometry）；
 *             - 尺寸协商：sizeHint()/minimumSize()/maximumSize() 与
 *               expandingDirections()（是否可沿水平/垂直方向伸展）；
 *             - heightForWidth 体系：hasHeightForWidth()/heightForWidth()/
 *               minimumHeightForWidth()（先定宽再算高的换行文本等场景）；
 *             - 形态查询：isEmpty()（是否为空/隐藏）、widget()（返回条目
 *               承载的控件，非控件条目返回 NULL）、layout()（返回条目
 *               承载的子布局，非布局条目返回 NULL）；
 *             - 对齐与策略：alignment()/setAlignment()（对标
 *               QLayoutItem::alignment/setAlignment，数值与 Qt::Alignment
 *               一致）、controlTypes()（对标 QSizePolicy::controlTypes）；
 *             - invalidate()：失效内部尺寸缓存，通知父级重算。
 *             布局(控件)自管理的“控件条目 / 空白条目”在库内以 XWidgetItem /
 *             XSpacerItem 形式实现（对应 Qt 的 QWidgetItem / QSpacerItem，
 *             XSpacerItem 自 XSpacerItem.h 公开，XWidgetItem 仍为内部实现，
 *             不对外公开）；用户自绘条目可直接继承
 *             XLayoutItem 并实现上述虚函数后经 XLayout_addItem 挂入布局。
 *             本模块与 QLayoutItem 一样允许直接作为占位/空条目基类使用，
 *             但不提供违反抽象的 XLayoutItem_create（Qt 中 QLayoutItem 为
 *             抽象类，不可直接实例化）。
 * @note       模块总开关 XLAYOUT_ON 定义于 XGuiConfig.h；依赖 XWIDGET_ON
 *             （关闭时本模块一并裁剪）。本模块不引用任何平台 API，嵌入式
 *             可用。
 * @author     XinYueC 团队
 ******************************************************************************/
#ifndef XLAYOUTITEM_H
#define XLAYOUTITEM_H
#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>
#include "XGuiConfig.h"
#include "XClass.h"
#include "XGeometry.h"
#include "XWidget.h"

/** @brief XLayout 抽象布局类前向声明（条目可承载子布局，指针借用）。 */
typedef struct XLayout XLayout;

/** @brief 空白条目类前向声明（对标 QSpacerItem；见 XSpacerItem.h）。
 *  XLayoutItem_spacerItem_base 的类型安全下转目标，基类条目返回 NULL。 */
typedef struct XSpacerItem XSpacerItem;

#if XLAYOUT_ON

/* ==================== 对齐标志（对标 Qt::Alignment） ==================== */

/* XAlignment 为控件/布局共用对齐枚举；此处仅保留 XLayoutAlignment* 兼容
 * 别名，使既有布局代码无需迁移即可继续使用旧名称。 */
#include "XAlignment.h"

/** @brief 布局对齐标志（兼容旧名称，等价 XAlignment）。 */
typedef XAlignment XLayoutAlignment;
/** @brief 布局对齐标志组合类型（兼容旧名称，等价 XAlignments）。 */
typedef XAlignments XLayoutAlignments;

#define XLayoutAlignment_Left                  XAlignment_Left
#define XLayoutAlignment_Leading               XAlignment_Leading
#define XLayoutAlignment_Right                 XAlignment_Right
#define XLayoutAlignment_Trailing              XAlignment_Trailing
#define XLayoutAlignment_HCenter               XAlignment_HCenter
#define XLayoutAlignment_Justify               XAlignment_Justify
#define XLayoutAlignment_Absolute              XAlignment_Absolute
#define XLayoutAlignment_HorizontalMask        XAlignment_HorizontalMask
#define XLayoutAlignment_Top                   XAlignment_Top
#define XLayoutAlignment_Bottom                XAlignment_Bottom
#define XLayoutAlignment_VCenter               XAlignment_VCenter
#define XLayoutAlignment_Baseline              XAlignment_Baseline
#define XLayoutAlignment_VerticalMask          XAlignment_VerticalMask
#define XLayoutAlignment_Center                XAlignment_Center

typedef uint32_t XLayoutAlignments; /**< 布局对齐标志组合类型。 */

/* ==================== 伸展方向（对标 Qt::Orientations） ==================== */

/**
 * @brief      尺寸伸展方向位标志（对标 Qt 6.8 Qt::Orientations）。
 * @details    expandingDirections() 的返回值，水平/垂直位可组合；
 *             布局在对应轴上有多余空间时会把多余空间分给该条目/布局。
 */
typedef enum XLayoutExpandingDirection
{
    XLayoutExpandingDirection_None       = 0x0, /**< 两个方向都不伸展。 */
    XLayoutExpandingDirection_Horizontal = 0x1, /**< 水平方向可伸展（对标 Qt::Horizontal）。 */
    XLayoutExpandingDirection_Vertical   = 0x2, /**< 垂直方向可伸展（对标 Qt::Vertical）。 */
    XLayoutExpandingDirection_Both       = 0x3  /**< 水平+垂直均可伸展。 */
} XLayoutExpandingDirection;
typedef uint32_t XLayoutExpandingDirections; /**< 伸展方向组合类型。 */

/* ==================== 虚函数表（对标 QLayoutItem 虚函数） ==================== */

/**
 * @brief XLayoutItem 虚函数表枚举。
 * @details 前 3 个槽位继承自 XClass（Copy/Move/Deinit）；下述 15 个
 *          新槽位分别对标 QLayoutItem 的 sizeHint / minimumSize /
 *          maximumSize / expandingDirections / isEmpty / setGeometry /
 *          geometry / widget / layout / hasHeightForWidth /
 *          heightForWidth / minimumHeightForWidth / invalidate /
 *          controlTypes / spacerItem。 */
XCLASS_DEFINE_BEGING(XLayoutItem)
XCLASS_DEFINE_ENUM(XLayoutItem, SizeHint) = XCLASS_VTABLE_GET_SIZE(XClass),
XCLASS_DEFINE_ENUM(XLayoutItem, MinimumSize),
XCLASS_DEFINE_ENUM(XLayoutItem, MaximumSize),
XCLASS_DEFINE_ENUM(XLayoutItem, ExpandingDirections),
XCLASS_DEFINE_ENUM(XLayoutItem, IsEmpty),
XCLASS_DEFINE_ENUM(XLayoutItem, SetGeometry),
XCLASS_DEFINE_ENUM(XLayoutItem, Geometry),
XCLASS_DEFINE_ENUM(XLayoutItem, Widget),
XCLASS_DEFINE_ENUM(XLayoutItem, Layout),
XCLASS_DEFINE_ENUM(XLayoutItem, HasHeightForWidth),
XCLASS_DEFINE_ENUM(XLayoutItem, HeightForWidth),
XCLASS_DEFINE_ENUM(XLayoutItem, MinimumHeightForWidth),
XCLASS_DEFINE_ENUM(XLayoutItem, Invalidate),
XCLASS_DEFINE_ENUM(XLayoutItem, ControlTypes),
XCLASS_DEFINE_ENUM(XLayoutItem, SpacerItem),
XCLASS_DEFINE_END(XLayoutItem)

/**
 * @brief      XLayoutItem 布局条目对象；m_class 必须是第一个成员。
 * @details    字段含义：
 *             - m_geometry：条目最近一次分配到的几何（矩形）；
 *             - m_alignment：条目对齐标志（0 表示“填满分配空间”）；
 *             - m_hasAlignment：是否显式设置过对齐；
 *             - m_ownedByLayout：内部标志，标记该条目对象由所属布局创建/
 *               拥有（addWidget/addSpacing 等自动创建的条目置 1，布局
 *               deinit 时会释放；经 addItem/addLayout 由调用方传入的
 *               条目置 0，释放责任仍在调用方）。
 *             调用方不得手工修改任何字段；几何/对齐读写一律走公开 API。
 */
typedef struct XLayoutItem
{
    XClass              m_class;          /**< 基类成员；必须是第一个。 */
    XRect               m_geometry;       /**< 最近一次分配几何。 */
    XLayoutAlignments   m_alignment;      /**< 对齐标志（XLayoutAlignment 组合）。 */
    uint32_t            m_hasAlignment : 1;   /**< 是否显式设置过对齐。 */
    uint32_t            m_ownedByLayout : 1;  /**< 内部：是否由所属布局创建/拥有。 */
    uint32_t            m_reservedLayoutItem : 30; /**< 保留位。 */
} XLayoutItem;

/* ==================== 类初始化与生命周期 ==================== */

/**
 * @brief      初始化 XLayoutItem 类虚函数表并返回共享表指针。
 * @return     XLayoutItem 类的共享 XVtable 指针。
 */
XVtable* XLayoutItem_class_init(void);

/**
 * @brief      初始化空布局条目（对标 QLayoutItem 受保护构造）。
 * @details    本函数供子类构造函数调用；基类默认虚函数实现为空条目
 *             语义（尺寸全 0、isEmpty 返回 true、widget/layout 返回
 *             NULL）。调用方不得直接生产“裸条目”挂入布局，除非它
 *             实现了尺寸虚函数。
 * @param      self 待初始化的条目；不可为 NULL。
 */
void XLayoutItem_init(XLayoutItem* self);

/** @brief 通过 XClass 虚表释放条目资源（栈/外部存储对象使用）。 */
#define XLayoutItem_deinit_base(self)  XClass_deinit_base((XClass*)(self))
/** @brief 删除堆上的布局条目对象。 */
#define XLayoutItem_delete_base(self)  XClass_delete_base((XClass*)(self))

/* ==================== 尺寸协商虚函数调度（对标 QLayoutItem） ==================== */

/**
 * @brief      返回条目首选尺寸（对标 QLayoutItem::sizeHint）。
 * @param      self 目标条目；可为 NULL。
 * @return     首选尺寸；param self 为 NULL 或实现缺失时返回 (0,0)。
 */
XSize XLayoutItem_sizeHint_base(const XLayoutItem* self);

/**
 * @brief      返回条目最小尺寸（对标 QLayoutItem::minimumSize）。
 * @param      self 目标条目；可为 NULL。
 * @return     最小尺寸；失败返回 (0,0)。
 */
XSize XLayoutItem_minimumSize_base(const XLayoutItem* self);

/**
 * @brief      返回条目最大尺寸（对标 QLayoutItem::maximumSize）。
 * @param      self 目标条目；可为 NULL。
 * @return     最大尺寸；失败返回 (XWIDGET_MAX_SIZE, XWIDGET_MAX_SIZE)。
 */
XSize XLayoutItem_maximumSize_base(const XLayoutItem* self);

/**
 * @brief      返回条目可伸展方向（对标 QLayoutItem::expandingDirections）。
 * @param      self 目标条目；可为 NULL。
 * @return     XLayoutExpandingDirections 组合；失败返回 None。
 */
XLayoutExpandingDirections XLayoutItem_expandingDirections_base(const XLayoutItem* self);

/**
 * @brief      查询条目是否为空（对标 QLayoutItem::isEmpty）。
 * @details    控件条目在控件隐藏时为空；布局条目在没有任何子条目时为
 *             空；空白条目始终为空。
 * @param      self 目标条目；可为 NULL。
 * @return     空返回 true；失败返回 true。
 */
bool XLayoutItem_isEmpty_base(const XLayoutItem* self);

/**
 * @brief      设置条目几何（对标 QLayoutItem::setGeometry）。
 * @details    布局引擎在激活布局时按分配结果调用；基类实现会保存几何
 *             快照到 m_geometry，子类（控件条目/布局对象）在此基础上
 *             把几何应用到承载的控件或继续分配给下级条目。
 * @param      self 目标条目；可为 NULL 时函数不执行。
 * @param      rect 分配矩形；可为 NULL 时不改变。
 */
void XLayoutItem_setGeometry_base(XLayoutItem* self, const XRect* rect);

/**
 * @brief      返回条目当前几何（对标 QLayoutItem::geometry）。
 * @param      self 目标条目；可为 NULL。
 * @return     最近一次分配几何；未分配过或失败时返回全 0 矩形。
 */
XRect XLayoutItem_geometry_base(const XLayoutItem* self);



/**
 * @brief      查询条目是否支持 heightForWidth（对标 QLayoutItem::hasHeightForWidth）。
 * @param      self 目标条目；可为 NULL。
 * @return     支持返回 true；失败返回 false。
 */
bool XLayoutItem_hasHeightForWidth_base(const XLayoutItem* self);

/**
 * @brief      返回给定宽度下的首选高度（对标 QLayoutItem::heightForWidth）。
 * @param      self 目标条目；可为 NULL。
 * @param      width 给定宽度（像素，一般不含布局边距）。
 * @return     首选高度；实现缺失或 width 非法时返回 -1。
 */
int XLayoutItem_heightForWidth_base(const XLayoutItem* self, int width);

/**
 * @brief      返回给定宽度下的最小高度（对标 QLayoutItem::minimumHeightForWidth）。
 * @param      self 目标条目；可为 NULL。
 * @param      width 给定宽度（像素，一般不含布局边距）。
 * @return     最小高度；失败返回 -1。
 */
int XLayoutItem_minimumHeightForWidth_base(const XLayoutItem* self, int width);

/**
 * @brief      使条目内部尺寸缓存失效（对标 QLayoutItem::invalidate）。
 * @details    布局(控件)尺寸或提示变化时调用，通知父级重新计算；对
 *             自身无可缓存子项的空条目是 no-op。失效会向上传播到顶级
 *             布局，使下一次激活时全链重新解算。
 * @param      self 目标条目；可为 NULL。
 */
void XLayoutItem_invalidate_base(XLayoutItem* self);



/*
 * QLayoutItem 中受保护的虚函数入口（widget/layout/controlTypes/spacerItem）
 * 已迁移至保护头文件 XLayoutItem_Protected.h，仅供子类与布局引擎内部使用。
 */

/* ==================== 对齐访问（对标 QLayoutItem::alignment/setAlignment） ==================== */

/**
 * @brief      返回条目对齐标志（对标 QLayoutItem::alignment）。
 * @param      self 目标条目；可为 NULL。
 * @return     对齐标志组合；未显式设置或失败时返回 0（表示填满分配空间）。
 */
XLayoutAlignments XLayoutItem_alignment(const XLayoutItem* self);

/**
 * @brief      设置条目对齐标志（对标 QLayoutItem::setAlignment）。
 * @details    0 表示“不设置对齐，填满分配空间”；设置了水平/垂直对齐位后，
 *             布局按该对齐摆放条目（不再拉伸到填满）。
 * @param      self 目标条目；可为 NULL。
 * @param      alignment 对齐标志组合。
 */
void XLayoutItem_setAlignment(XLayoutItem* self, XLayoutAlignments alignment);

#endif /* XLAYOUT_ON */

#ifdef __cplusplus
}
#endif
#endif /* XLAYOUTITEM_H */
