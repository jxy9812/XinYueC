/******************************************************************************
 * @file       XLayoutItem.c
 * @brief      XLayoutItem 布局条目基类实现（对标 Qt 6.8 QLayoutItem）。
 * @details    本文件实现：
 *             - XLayoutItem 类共享虚函数表（继承 XClass 3 槽 + 15 个布局
 *               尺寸/几何/查询槽位）与空条目默认语义；
 *             - 库内控件条目 XWidgetItem（对标 QWidgetItem）与空白条目
 *               XSpacerItem（对标 QSpacerItem）的全部虚函数实现；
 *             - 两个内部条目工厂 XLayoutItem_createWidgetItem /
 *               XLayoutItem_createSpacerItem（仅供布局实现调用）；
 *             - 全部 _base 调度函数（按对象虚表转发，跨布局安全）。
 *             尺寸策略数值与 Qt 6.8 一致；控件隐藏按 isEmpty 收缩。
 * @note       两个内部条目类仅在本文件与 XLayout 实现中可见，不对外公开。
 *             本文件不依赖任何平台 API，嵌入式可用。
 * @author     XinYueC 团队
 ******************************************************************************/
#include "XLayoutItem.h"
#include "XLayoutItem_Protected.h"
#include "XLayout_Internal.h"
#include "XMemory.h"
#include <string.h>

#if XLAYOUT_ON

/* ==================== XLayoutItem 基类默认虚函数实现 ==================== */

/** @brief 基类默认首选尺寸：空条目 (0,0)。 */
static XSize VXLayoutItem_sizeHint(const XLayoutItem* self)
{
    XSize out;
    (void)self;
    XSize_init(&out, 0, 0);
    return out;
}

/** @brief 基类默认最小尺寸：(0,0)。 */
static XSize VXLayoutItem_minimumSize(const XLayoutItem* self)
{
    XSize out;
    (void)self;
    XSize_init(&out, 0, 0);
    return out;
}

/** @brief 基类默认最大尺寸：XWIDGET_MAX_SIZE（不设上限）。 */
static XSize VXLayoutItem_maximumSize(const XLayoutItem* self)
{
    XSize out;
    (void)self;
    XSize_init(&out, XWIDGET_MAX_SIZE, XWIDGET_MAX_SIZE);
    return out;
}

/** @brief 基类默认伸展方向：两个方向都不伸展。 */
static XLayoutExpandingDirections VXLayoutItem_expandingDirections(
    const XLayoutItem* self)
{
    (void)self;
    return XLayoutExpandingDirection_None;
}

/** @brief 基类默认空判定：空条目始终为空。 */
static bool VXLayoutItem_isEmpty(const XLayoutItem* self)
{
    (void)self;
    return true;
}

/** @brief 基类默认几何设置：仅保存几何快照到 m_geometry。 */
static void VXLayoutItem_setGeometry(XLayoutItem* self, const XRect* rect)
{
    if (!self || !rect) return;
    self->m_geometry = *rect;
}

/** @brief 基类默认几何查询：返回最近一次分配几何。 */
static XRect VXLayoutItem_geometry(const XLayoutItem* self)
{
    XRect out;
    XRect_init(&out, 0, 0, 0, 0);
    if (!self) return out;
    return self->m_geometry;
}

/** @brief 基类默认控件查询：空条目不承载控件。 */
static XWidget* VXLayoutItem_widget(const XLayoutItem* self)
{
    (void)self;
    return NULL;
}

/** @brief 基类默认子布局查询：空条目不承载子布局。 */
static XLayout* VXLayoutItem_layout(const XLayoutItem* self)
{
    (void)self;
    return NULL;
}

/** @brief 基类默认 heightForWidth 支持：不支持。 */
static bool VXLayoutItem_hasHeightForWidth(const XLayoutItem* self)
{
    (void)self;
    return false;
}

/** @brief 基类默认首选高度：-1（无 hfw 语义）。 */
static int VXLayoutItem_heightForWidth(const XLayoutItem* self, int width)
{
    (void)self;
    (void)width;
    return -1;
}

/** @brief 基类默认最小高度：-1（无 hfw 语义）。 */
static int VXLayoutItem_minimumHeightForWidth(const XLayoutItem* self, int width)
{
    (void)self;
    (void)width;
    return -1;
}

/** @brief 基类默认失效：基类条目无可缓存子项，no-op。 */
static void VXLayoutItem_invalidate(XLayoutItem* self)
{
    (void)self;
}

/** @brief 基类默认控件类型：DefaultType。 */
static XWidgetSizePolicyControlTypes VXLayoutItem_controlTypes(
    const XLayoutItem* self)
{
    (void)self;
    return XWidgetSizePolicyControl_DefaultType;
}

/** @brief 基类默认空白下转：非空白条目返回 NULL（对标 QLayoutItem::spacerItem）。 */
static XSpacerItem* VXLayoutItem_spacerItem(const XLayoutItem* self)
{
    (void)self;
    return NULL;
}

/* ==================== XWidgetItem 控件条目（内部） ==================== */

/** @brief 控件条目虚表枚举：不新增槽位，仅重载 XLayoutItem 槽位。 */
XCLASS_DEFINE_BEGING(XWidgetItem)
XCLASS_DEFINE_EXTEND_END(XWidgetItem, XLayoutItem)

/** @brief 控件条目空判定（前置声明；实现见后文）。 */
static bool VXWidgetItem_isEmpty(const XLayoutItem* self);

/** @brief 控件条目首选尺寸：委托给控件 sizeHint；无效时回退最小尺寸提示。
 *  @details 控件隐藏且未保留隐藏尺寸（条目已空）时返回 (0,0)，与 Qt
 *           QWidgetItem::sizeHint 一致——布局只按条目实际尺寸排布空白。 */
static XSize VXWidgetItem_sizeHint(const XLayoutItem* self)
{
    XWidgetItem* item = (XWidgetItem*)self;
    XSize hint;
    XSize minHint;
    XSize max;
    XSize out;
    XSize_init(&out, 0, 0);
    if (!item || !item->m_widget) return out;
    if (VXWidgetItem_isEmpty((const XLayoutItem*)self)) return out;
    hint = XWidget_sizeHint(item->m_widget);
    minHint = XWidget_minimumSizeHint(item->m_widget);
    max = XWidget_maximumSize(item->m_widget);
    if (hint.width < 0) hint.width = minHint.width;
    if (hint.height < 0) hint.height = minHint.height;
    if (hint.width < 0) hint.width = 0;
    if (hint.height < 0) hint.height = 0;
    if (max.width >= 0 && hint.width > max.width) hint.width = max.width;
    if (max.height >= 0 && hint.height > max.height) hint.height = max.height;
    return hint;
}

/** @brief 控件条目最小尺寸：控件 minimumSize 与最小尺寸提示取大，上界为
 *  maximumSize；条目已空（隐藏且不留尺寸）时返回 (0,0)（对标 Qt
 *  QWidgetItem::minimumSize）。 */
static XSize VXWidgetItem_minimumSize(const XLayoutItem* self)
{
    XWidgetItem* item = (XWidgetItem*)self;
    XSize min;
    XSize minHint;
    XSize max;
    XSize out;
    XSize_init(&out, 0, 0);
    if (!item || !item->m_widget) return out;
    if (VXWidgetItem_isEmpty((const XLayoutItem*)self)) return out;
    min = XWidget_minimumSize(item->m_widget);
    minHint = XWidget_minimumSizeHint(item->m_widget);
    max = XWidget_maximumSize(item->m_widget);
    out.width = (min.width > minHint.width) ? min.width : minHint.width;
    out.height = (min.height > minHint.height) ? min.height : minHint.height;
    if (out.width < 0) out.width = 0;
    if (out.height < 0) out.height = 0;
    if (max.width >= 0 && out.width > max.width) out.width = max.width;
    if (max.height >= 0 && out.height > max.height) out.height = max.height;
    return out;
}

/** @brief 控件条目最大尺寸：直接返回控件 maximumSize；条目已空（隐藏且
 *  不留尺寸）时返回 (0,0)（对标 Qt QWidgetItem::maximumSize）。 */
static XSize VXWidgetItem_maximumSize(const XLayoutItem* self)
{
    XWidgetItem* item = (XWidgetItem*)self;
    XSize out;
    XSize_init(&out, 0, 0);
    if (!item || !item->m_widget) return out;
    if (VXWidgetItem_isEmpty((const XLayoutItem*)self)) return out;
    return XWidget_maximumSize(item->m_widget);
}

/** @brief 控件条目伸展方向：来自控件尺寸策略的 expandingDirections。 */
static XLayoutExpandingDirections VXWidgetItem_expandingDirections(
    const XLayoutItem* self)
{
    XWidgetItem* item = (XWidgetItem*)self;
    XWidgetSizePolicy policy;
    if (!item || !item->m_widget)
        return XLayoutExpandingDirection_None;
    policy = XWidget_sizePolicy(item->m_widget);
    return (XLayoutExpandingDirections)XWidgetSizePolicy_expandingDirections(
        &policy);
}

/** @brief 控件条目空判定：控件隐藏且未保留隐藏尺寸时为空。 */
static bool VXWidgetItem_isEmpty(const XLayoutItem* self)
{
    XWidgetItem* item = (XWidgetItem*)self;
    XWidgetSizePolicy policy;
    if (!item || !item->m_widget) return true;
    if (XWidget_isWindow(item->m_widget)) return true;
    if (!XWidget_isHidden(item->m_widget)) return false;
    policy = XWidget_sizePolicy(item->m_widget);
    return !XWidgetSizePolicy_retainSizeWhenHidden(&policy);
}

/** @brief 控件条目 heightForWidth 支持（前置声明，实现见后文）。 */
static bool VXWidgetItem_hasHeightForWidth(const XLayoutItem* self);
/** @brief 控件条目首选高度（前置声明，实现见后文）。 */
static int VXWidgetItem_heightForWidth(const XLayoutItem* self, int width);

/** @brief 控件条目几何设置：保存快照，按对齐收拢后应用到控件。
 * @details 对标 Qt QWidgetItem::setGeometry（qlayoutitem.cpp）：
 *          - 条目已空（控件隐藏且不留尺寸）时直接返回，不对控件改几何；
 *          - 先按最大尺寸把尺寸收拢；
 *          - 显式设置了对齐位时，水平/垂直方向上把尺寸放宽到首选尺寸限，
 *            heightForWidth 场景用 heightForWidth(宽) 限高；
 *          - 水平位置：Right→贴右、HCenter→水平居中、否则贴左；
 *            垂直位置：Bottom→贴底、VCenter→垂直居中、否则贴顶。 */
static void VXWidgetItem_setGeometry(XWidgetItem* self, const XRect* rect)
{
    XRect r;
    XSize s;
    XSize pref;
    XSize max;
    XLayoutAlignments align;
    bool explicitAlignment;
    bool rightToLeft;
    bool alignLeft;
    bool alignRight;
    int x;
    int y;
    if (!self || !rect) return;
    self->m_base.m_geometry = *rect;
    if (!self->m_widget) return;
    if (VXWidgetItem_isEmpty((const XLayoutItem*)self))
        return;
    r = *rect;
    s.width = r.width;
    s.height = r.height;
    max = VXWidgetItem_maximumSize((const XLayoutItem*)self);
    if (max.width >= 0 && s.width > max.width) s.width = max.width;
    if (max.height >= 0 && s.height > max.height) s.height = max.height;
    x = r.x;
    y = r.y;
    explicitAlignment = self->m_base.m_hasAlignment != 0;
    align = explicitAlignment ? self->m_base.m_alignment : 0;
    rightToLeft = XWidget_layoutDirection(self->m_widget) ==
                  XWidgetLayoutDirection_RightToLeft;
    alignLeft = (align & XLayoutAlignment_Left) != 0;
    alignRight = (align & XLayoutAlignment_Right) != 0;
    if (rightToLeft && !(align & XLayoutAlignment_Absolute)) {
        bool swap = alignLeft;
        alignLeft = alignRight;
        alignRight = swap;
    }

    /* Qt 只在显式指定相应轴对齐时按首选尺寸收缩。 */
    pref = VXWidgetItem_sizeHint((const XLayoutItem*)self);
    if (explicitAlignment &&
        (align & XLayoutAlignment_HorizontalMask) &&
        pref.width >= 0 && s.width > pref.width)
        s.width = pref.width;
    if (explicitAlignment && (align & XLayoutAlignment_VerticalMask)) {
        if (VXWidgetItem_hasHeightForWidth((const XLayoutItem*)self)) {
            int hfw = VXWidgetItem_heightForWidth((const XLayoutItem*)self,
                                                  s.width);
            if (hfw >= 0 && s.height > hfw) s.height = hfw;
        } else if (pref.height >= 0 && s.height > pref.height) {
            s.height = pref.height;
        }
    }

    /* visualAlignment：无水平位时默认为 Left；无对齐时垂直默认为居中。 */
    if (alignRight)
        x = r.x + (r.width - s.width);
    else if (!alignLeft && explicitAlignment &&
             (align & XLayoutAlignment_HorizontalMask))
        x = r.x + (r.width - s.width) / 2;
    else
        x = r.x;
    if (align & XLayoutAlignment_Bottom)
        y = r.y + (r.height - s.height);
    else if (align & XLayoutAlignment_Top)
        y = r.y;
    else if (align & (XLayoutAlignment_VCenter | XLayoutAlignment_Baseline))
        y = r.y + (r.height - s.height) / 2;
    else if (!explicitAlignment || !(align & XLayoutAlignment_VerticalMask))
        y = r.y + (r.height - s.height) / 2;
    if (s.width < 0) s.width = 0;
    if (s.height < 0) s.height = 0;
    if (x < 0) {
        s.width += x;
        x = 0;
    }
    if (y < 0) {
        s.height += y;
        y = 0;
    }
    XRect_init(&r, x, y, s.width, s.height);
    XWidget_setGeometryRect(self->m_widget, &r);
}

/** @brief 控件条目控件查询：返回被包装控件。 */
static XWidget* VXWidgetItem_widget(const XLayoutItem* self)
{
    XWidgetItem* item = (XWidgetItem*)self;
    return item ? item->m_widget : NULL;
}

/** @brief 控件条目 heightForWidth 支持：来自尺寸策略标志。 */
static bool VXWidgetItem_hasHeightForWidth(const XLayoutItem* self)
{
    XWidgetItem* item = (XWidgetItem*)self;
    XWidgetSizePolicy policy;
    if (!item || !item->m_widget) return false;
    policy = XWidget_sizePolicy(item->m_widget);
    return XWidgetSizePolicy_hasHeightForWidth(&policy);
}

/** @brief 控件条目首选高度：转发到 QWidget 式 heightForWidth。 */
static int VXWidgetItem_heightForWidth(const XLayoutItem* self, int width)
{
    XWidgetItem* item = (XWidgetItem*)self;
    int height;
    if (!item || !item->m_widget) return -1;
    if (!VXWidgetItem_hasHeightForWidth(self)) return -1;
    height = XWidget_heightForWidth(item->m_widget, width);
    return height < 0 ? VXWidgetItem_sizeHint(self).height : height;
}

/** @brief 控件条目最小高度：Qt QWidget 没有独立 minimumHeightForWidth，
 *         使用同一 hfw 结果并由 XWidget 负责最小/最大高度钳位。 */
static int VXWidgetItem_minimumHeightForWidth(const XLayoutItem* self, int width)
{
    XWidgetItem* item = (XWidgetItem*)self;
    int height;
    if (!item || !item->m_widget) return -1;
    if (!VXWidgetItem_hasHeightForWidth(self)) return -1;
    height = XWidget_heightForWidth(item->m_widget, width);
    return height < 0 ? VXWidgetItem_minimumSize(self).height : height;
}

/** @brief 控件条目控件类型：来自控件尺寸策略。 */
static XWidgetSizePolicyControlTypes VXWidgetItem_controlTypes(
    const XLayoutItem* self)
{
    XWidgetItem* item = (XWidgetItem*)self;
    XWidgetSizePolicy policy;
    if (!item || !item->m_widget)
        return XWidgetSizePolicyControl_DefaultType;
    policy = XWidget_sizePolicy(item->m_widget);
    return (XWidgetSizePolicyControlTypes)XWidgetSizePolicy_controlType(&policy);
}

/** @brief 控件条目销毁：控件为借用指针，不释放。 */
static void VXWidgetItem_deinit(XWidgetItem* self)
{
    if (!self) return;
    self->m_widget = NULL;
    XClass_Deinit_Parent(XLayoutItem, (XLayoutItem*)self);
}

/** @brief 控件条目拷贝：复制标量字段与借用控件指针（不复制控件本身）。 */
static void VXWidgetItem_copy(XWidgetItem* self, const XWidgetItem* other)
{
    if (!self || !other || self == other) return;
    if (XClassIsVtableNull(self)) XWidgetItem_init(self, NULL);
    self->m_base = other->m_base;
    self->m_widget = other->m_widget;
}

/** @brief 控件条目移动：转移借用控件指针并清空源对象。 */
static void VXWidgetItem_move(XWidgetItem* self, XWidgetItem* other)
{
    if (!self || !other || self == other) return;
    if (XClassIsVtableNull(self)) XWidgetItem_init(self, NULL);
    self->m_base = other->m_base;
    self->m_widget = other->m_widget;
    other->m_widget = NULL;
}

/* ==================== XSpacerItem 空白条目（内部） ==================== */

/** @brief 空白条目虚表枚举：不新增槽位，仅重载 XLayoutItem 槽位。 */
XCLASS_DEFINE_BEGING(XSpacerItem)
XCLASS_DEFINE_EXTEND_END(XSpacerItem, XLayoutItem)

/** @brief 空白条目首选尺寸：固定首选空白尺寸。 */
static XSize VXSpacerItem_sizeHint(const XLayoutItem* self)
{
    XSpacerItem* item = (XSpacerItem*)self;
    XSize out;
    XSize_init(&out, 0, 0);
    if (!item) return out;
    return item->m_size;
}

/** @brief 空白条目最小尺寸：ShrinkFlag 方向收缩到 0，其余方向保持首选尺寸
 *  （对标 Qt QSpacerItem::minimumSize）。 */
static XSize VXSpacerItem_minimumSize(const XLayoutItem* self)
{
    XSpacerItem* item = (XSpacerItem*)self;
    XSize out;
    XSize_init(&out, 0, 0);
    if (!item) return out;
    if (!(item->m_hPolicy & 0x04u)) out.width = item->m_size.width;
    if (!(item->m_vPolicy & 0x04u)) out.height = item->m_size.height;
    if (out.width < 0) out.width = 0;
    if (out.height < 0) out.height = 0;
    return out;
}

/** @brief 空白条目最大尺寸：GrowFlag 方向放开到 XWIDGET_MAX_SIZE，
 *  其余方向固定首选尺寸（GrowFlag=0x01，对标 Qt QSpacerItem::maximumSize）。 */
static XSize VXSpacerItem_maximumSize(const XLayoutItem* self)
{
    XSpacerItem* item = (XSpacerItem*)self;
    XSize out;
    XSize_init(&out, XWIDGET_MAX_SIZE, XWIDGET_MAX_SIZE);
    if (!item) return out;
    out.width = (item->m_hPolicy & 0x01u) ? XWIDGET_MAX_SIZE : item->m_size.width;
    out.height = (item->m_vPolicy & 0x01u) ? XWIDGET_MAX_SIZE : item->m_size.height;
    return out;
}

/** @brief 空白条目伸展方向：ExpandFlag 方向可伸展（对标 Qt）。 */
static XLayoutExpandingDirections VXSpacerItem_expandingDirections(
    const XLayoutItem* self)
{
    XSpacerItem* item = (XSpacerItem*)self;
    XLayoutExpandingDirections dirs = XLayoutExpandingDirection_None;
    if (!item) return dirs;
    if (item->m_hPolicy & 0x02u)
        dirs |= XLayoutExpandingDirection_Horizontal;
    if (item->m_vPolicy & 0x02u)
        dirs |= XLayoutExpandingDirection_Vertical;
    return dirs;
}

/** @brief 空白条目空判定：空白条目始终为空。 */
static bool VXSpacerItem_isEmpty(const XLayoutItem* self)
{
    (void)self;
    return true;
}

/** @brief 空白条目空白下转：返回自身（对标 QSpacerItem::spacerItem）。 */
static XSpacerItem* VXSpacerItem_spacerItem(const XLayoutItem* self)
{
    return (XSpacerItem*)self;
}

/** @brief 空白条目几何设置：仅保存几何快照。 */
static void VXSpacerItem_setGeometry(XSpacerItem* self, const XRect* rect)
{
    if (!self || !rect) return;
    self->m_base.m_geometry = *rect;
}

/** @brief 空白条目销毁：无自有资源。 */
static void VXSpacerItem_deinit(XSpacerItem* self)
{
    if (!self) return;
    XClass_Deinit_Parent(XLayoutItem, (XLayoutItem*)self);
}

/** @brief 空白条目拷贝：复制标量字段。 */
static void VXSpacerItem_copy(XSpacerItem* self, const XSpacerItem* other)
{
    if (!self || !other || self == other) return;
    if (XClassIsVtableNull(self))
        XSpacerItem_init(self, 0, 0, XWidgetSizePolicy_Fixed,
                         XWidgetSizePolicy_Fixed);
    self->m_base = other->m_base;
    self->m_size = other->m_size;
    self->m_hPolicy = other->m_hPolicy;
    self->m_vPolicy = other->m_vPolicy;
    self->m_isMagic = other->m_isMagic;
}

/** @brief 空白条目移动：转移字段并清空源。 */
static void VXSpacerItem_move(XSpacerItem* self, XSpacerItem* other)
{
    if (!self || !other || self == other) return;
    if (XClassIsVtableNull(self))
        XSpacerItem_init(self, 0, 0, XWidgetSizePolicy_Fixed,
                         XWidgetSizePolicy_Fixed);
    self->m_base = other->m_base;
    self->m_size = other->m_size;
    self->m_hPolicy = other->m_hPolicy;
    self->m_vPolicy = other->m_vPolicy;
    self->m_isMagic = other->m_isMagic;
    other->m_isMagic = 0;
    XSize_init(&other->m_size, 0, 0);
}

/* ==================== 类初始化与实例生命周期 ==================== */

XVtable* XLayoutItem_class_init(void)
{
    XVTABLE_INIT_DEFAULT(XLayoutItem)
    XVTABLE_INHERIT_XCLASS(XClass);
    void* table[] = {
        VXLayoutItem_sizeHint,       /* SizeHint */
        VXLayoutItem_minimumSize,    /* MinimumSize */
        VXLayoutItem_maximumSize,    /* MaximumSize */
        VXLayoutItem_expandingDirections, /* ExpandingDirections */
        VXLayoutItem_isEmpty,        /* IsEmpty */
        VXLayoutItem_setGeometry,    /* SetGeometry */
        VXLayoutItem_geometry,       /* Geometry */
        VXLayoutItem_widget,         /* Widget */
        VXLayoutItem_layout,         /* Layout */
        VXLayoutItem_hasHeightForWidth, /* HasHeightForWidth */
        VXLayoutItem_heightForWidth, /* HeightForWidth */
        VXLayoutItem_minimumHeightForWidth, /* MinimumHeightForWidth */
        VXLayoutItem_invalidate,     /* Invalidate */
        VXLayoutItem_controlTypes,   /* ControlTypes */
        VXLayoutItem_spacerItem      /* SpacerItem */
    };
    XVTABLE_ADD_FUNC_LIST_DEFAULT(table);
    return XVTABLE_DEFAULT;
}

void XLayoutItem_init(XLayoutItem* self)
{
    if (!self) return;
    memset(self, 0, sizeof(XLayoutItem));
    XClass_init((XClass*)self);
    XClassSetVtable(self, XLayoutItem);
    XRect_init(&self->m_geometry, 0, 0, 0, 0);
    self->m_alignment = 0;
    self->m_hasAlignment = 0;
    self->m_ownedByLayout = 0;
}

XVtable* XWidgetItem_class_init(void)
{
    XVTABLE_INIT_DEFAULT(XWidgetItem)
    XVTABLE_INHERIT_XCLASS(XLayoutItem);
    XVTABLE_OVERLOAD_DEFAULT(EXLayoutItem_SizeHint, VXWidgetItem_sizeHint);
    XVTABLE_OVERLOAD_DEFAULT(EXLayoutItem_MinimumSize, VXWidgetItem_minimumSize);
    XVTABLE_OVERLOAD_DEFAULT(EXLayoutItem_MaximumSize, VXWidgetItem_maximumSize);
    XVTABLE_OVERLOAD_DEFAULT(EXLayoutItem_ExpandingDirections,
                             VXWidgetItem_expandingDirections);
    XVTABLE_OVERLOAD_DEFAULT(EXLayoutItem_IsEmpty, VXWidgetItem_isEmpty);
    XVTABLE_OVERLOAD_DEFAULT(EXLayoutItem_SetGeometry, VXWidgetItem_setGeometry);
    XVTABLE_OVERLOAD_DEFAULT(EXLayoutItem_Geometry, VXLayoutItem_geometry);
    XVTABLE_OVERLOAD_DEFAULT(EXLayoutItem_Widget, VXWidgetItem_widget);
    XVTABLE_OVERLOAD_DEFAULT(EXLayoutItem_Layout, VXLayoutItem_layout);
    XVTABLE_OVERLOAD_DEFAULT(EXLayoutItem_HasHeightForWidth,
                             VXWidgetItem_hasHeightForWidth);
    XVTABLE_OVERLOAD_DEFAULT(EXLayoutItem_HeightForWidth,
                             VXWidgetItem_heightForWidth);
    XVTABLE_OVERLOAD_DEFAULT(EXLayoutItem_MinimumHeightForWidth,
                             VXWidgetItem_minimumHeightForWidth);
    XVTABLE_OVERLOAD_DEFAULT(EXLayoutItem_Invalidate, VXLayoutItem_invalidate);
    XVTABLE_OVERLOAD_DEFAULT(EXLayoutItem_ControlTypes,
                             VXWidgetItem_controlTypes);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Deinit, VXWidgetItem_deinit);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Copy, VXWidgetItem_copy);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Move, VXWidgetItem_move);
    return XVTABLE_DEFAULT;
}

void XWidgetItem_init(XWidgetItem* self, XWidget* widget)
{
    if (!self) return;
    memset(self, 0, sizeof(XWidgetItem));
    XLayoutItem_init(&self->m_base);
    XClassSetVtable(self, XWidgetItem);
    self->m_widget = widget;
}

XVtable* XSpacerItem_class_init(void)
{
    XVTABLE_INIT_DEFAULT(XSpacerItem)
    XVTABLE_INHERIT_XCLASS(XLayoutItem);
    XVTABLE_OVERLOAD_DEFAULT(EXLayoutItem_SizeHint, VXSpacerItem_sizeHint);
    XVTABLE_OVERLOAD_DEFAULT(EXLayoutItem_MinimumSize, VXSpacerItem_minimumSize);
    XVTABLE_OVERLOAD_DEFAULT(EXLayoutItem_MaximumSize, VXSpacerItem_maximumSize);
    XVTABLE_OVERLOAD_DEFAULT(EXLayoutItem_ExpandingDirections,
                             VXSpacerItem_expandingDirections);
    XVTABLE_OVERLOAD_DEFAULT(EXLayoutItem_IsEmpty, VXSpacerItem_isEmpty);
    XVTABLE_OVERLOAD_DEFAULT(EXLayoutItem_SetGeometry, VXSpacerItem_setGeometry);
    XVTABLE_OVERLOAD_DEFAULT(EXLayoutItem_Geometry, VXLayoutItem_geometry);
    XVTABLE_OVERLOAD_DEFAULT(EXLayoutItem_Widget, VXLayoutItem_widget);
    XVTABLE_OVERLOAD_DEFAULT(EXLayoutItem_Layout, VXLayoutItem_layout);
    XVTABLE_OVERLOAD_DEFAULT(EXLayoutItem_HasHeightForWidth,
                             VXLayoutItem_hasHeightForWidth);
    XVTABLE_OVERLOAD_DEFAULT(EXLayoutItem_HeightForWidth,
                             VXLayoutItem_heightForWidth);
    XVTABLE_OVERLOAD_DEFAULT(EXLayoutItem_MinimumHeightForWidth,
                             VXLayoutItem_minimumHeightForWidth);
    XVTABLE_OVERLOAD_DEFAULT(EXLayoutItem_Invalidate, VXLayoutItem_invalidate);
    XVTABLE_OVERLOAD_DEFAULT(EXLayoutItem_ControlTypes,
                             VXLayoutItem_controlTypes);
    XVTABLE_OVERLOAD_DEFAULT(EXLayoutItem_SpacerItem,
                             VXSpacerItem_spacerItem);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Deinit, VXSpacerItem_deinit);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Copy, VXSpacerItem_copy);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Move, VXSpacerItem_move);
    return XVTABLE_DEFAULT;
}

void XSpacerItem_init(XSpacerItem* self, int width, int height,
                      XWidgetSizePolicyPolicy hPolicy,
                      XWidgetSizePolicyPolicy vPolicy)
{
    if (!self) return;
    memset(self, 0, sizeof(XSpacerItem));
    XLayoutItem_init(&self->m_base);
    XClassSetVtable(self, XSpacerItem);
    if (width < 0) width = 0;
    if (height < 0) height = 0;
    XSize_init(&self->m_size, width, height);
    self->m_hPolicy = (uint8_t)hPolicy;
    self->m_vPolicy = (uint8_t)vPolicy;
}

#if XLAYOUT_SPACER_ON

XSpacerItem* XSpacerItem_create(int width, int height,
                                XWidgetSizePolicyPolicy hPolicy,
                                XWidgetSizePolicyPolicy vPolicy)
{
    XSpacerItem* item = (XSpacerItem*)XMalloc_System(sizeof(XSpacerItem));
    if (!item) return NULL;
    memset(item, 0, sizeof(XSpacerItem));
    XSpacerItem_init(item, width, height, hPolicy, vPolicy);
    Set_Class_IsHeap(item, true);
    return item;
}

void XSpacerItem_changeSize(XSpacerItem* self, int width, int height,
                            XWidgetSizePolicyPolicy hPolicy,
                            XWidgetSizePolicyPolicy vPolicy)
{
    if (!self) return;
    if (width < 0) width = 0;
    if (height < 0) height = 0;
    XSize_init(&self->m_size, width, height);
    self->m_hPolicy = (uint8_t)hPolicy;
    self->m_vPolicy = (uint8_t)vPolicy;
}

XWidgetSizePolicy XSpacerItem_sizePolicy(const XSpacerItem* self)
{
    if (!self) return XWidgetSizePolicy_create();
    return XWidgetSizePolicy_create_ex(
        (XWidgetSizePolicyPolicy)self->m_hPolicy,
        (XWidgetSizePolicyPolicy)self->m_vPolicy,
        XWidgetSizePolicyControl_DefaultType);
}

#endif /* XLAYOUT_SPACER_ON */

XLayoutItem* XLayoutItem_createWidgetItem(XWidget* widget)
{
    XWidgetItem* item = (XWidgetItem*)XMalloc_System(sizeof(XWidgetItem));
    if (!item) return NULL;
    memset(item, 0, sizeof(XWidgetItem));
    XWidgetItem_init(item, widget);
    Set_Class_IsHeap(item, true);
    return (XLayoutItem*)item;
}

XLayoutItem* XLayoutItem_createSpacerItem(const XSize* size,
                                          XWidgetSizePolicyPolicy hPolicy,
                                          XWidgetSizePolicyPolicy vPolicy)
{
    XSpacerItem* item = (XSpacerItem*)XMalloc_System(sizeof(XSpacerItem));
    if (!item) return NULL;
    memset(item, 0, sizeof(XSpacerItem));
    XSpacerItem_init(item, size ? size->width : 0, size ? size->height : 0,
                     hPolicy, vPolicy);
    Set_Class_IsHeap(item, true);
    return (XLayoutItem*)item;
}

/* ==================== 虚函数调度函数（跨对象类型安全转发） ==================== */

XSize XLayoutItem_sizeHint_base(const XLayoutItem* self)
{
    XSize out;
    XSize_init(&out, 0, 0);
    if (!self || !XClassGetVtable(self)) return out;
    return XClassGetVirtualFunc(self, EXLayoutItem_SizeHint,
                                XSize(*)(const XLayoutItem*))(self);
}

XSize XLayoutItem_minimumSize_base(const XLayoutItem* self)
{
    XSize out;
    XSize_init(&out, 0, 0);
    if (!self || !XClassGetVtable(self)) return out;
    return XClassGetVirtualFunc(self, EXLayoutItem_MinimumSize,
                                XSize(*)(const XLayoutItem*))(self);
}

XSize XLayoutItem_maximumSize_base(const XLayoutItem* self)
{
    XSize out;
    XSize_init(&out, XWIDGET_MAX_SIZE, XWIDGET_MAX_SIZE);
    if (!self || !XClassGetVtable(self)) return out;
    return XClassGetVirtualFunc(self, EXLayoutItem_MaximumSize,
                                XSize(*)(const XLayoutItem*))(self);
}

XLayoutExpandingDirections XLayoutItem_expandingDirections_base(
    const XLayoutItem* self)
{
    if (!self || !XClassGetVtable(self))
        return XLayoutExpandingDirection_None;
    return XClassGetVirtualFunc(self, EXLayoutItem_ExpandingDirections,
        XLayoutExpandingDirections(*)(const XLayoutItem*))(self);
}

bool XLayoutItem_isEmpty_base(const XLayoutItem* self)
{
    if (!self || !XClassGetVtable(self)) return true;
    return XClassGetVirtualFunc(self, EXLayoutItem_IsEmpty,
                                bool(*)(const XLayoutItem*))(self);
}

void XLayoutItem_setGeometry_base(XLayoutItem* self, const XRect* rect)
{
    if (!self || !rect || !XClassGetVtable(self)) return;
    XClassGetVirtualFunc(self, EXLayoutItem_SetGeometry,
                         void(*)(XLayoutItem*, const XRect*))(self, rect);
}

XRect XLayoutItem_geometry_base(const XLayoutItem* self)
{
    XRect out;
    XRect_init(&out, 0, 0, 0, 0);
    if (!self || !XClassGetVtable(self)) return out;
    return XClassGetVirtualFunc(self, EXLayoutItem_Geometry,
                                XRect(*)(const XLayoutItem*))(self);
}

XWidget* XLayoutItem_widget_base(const XLayoutItem* self)
{
    if (!self || !XClassGetVtable(self)) return NULL;
    return XClassGetVirtualFunc(self, EXLayoutItem_Widget,
                                XWidget*(*)(const XLayoutItem*))(self);
}

XLayout* XLayoutItem_layout_base(const XLayoutItem* self)
{
    if (!self || !XClassGetVtable(self)) return NULL;
    return XClassGetVirtualFunc(self, EXLayoutItem_Layout,
                                XLayout*(*)(const XLayoutItem*))(self);
}

bool XLayoutItem_hasHeightForWidth_base(const XLayoutItem* self)
{
    if (!self || !XClassGetVtable(self)) return false;
    return XClassGetVirtualFunc(self, EXLayoutItem_HasHeightForWidth,
                                bool(*)(const XLayoutItem*))(self);
}

int XLayoutItem_heightForWidth_base(const XLayoutItem* self, int width)
{
    if (!self || !XClassGetVtable(self)) return -1;
    return XClassGetVirtualFunc(self, EXLayoutItem_HeightForWidth,
                                int(*)(const XLayoutItem*, int))(self, width);
}

int XLayoutItem_minimumHeightForWidth_base(const XLayoutItem* self, int width)
{
    if (!self || !XClassGetVtable(self)) return -1;
    return XClassGetVirtualFunc(self, EXLayoutItem_MinimumHeightForWidth,
                                int(*)(const XLayoutItem*, int))(self, width);
}

void XLayoutItem_invalidate_base(XLayoutItem* self)
{
    if (!self || !XClassGetVtable(self)) return;
    XClassGetVirtualFunc(self, EXLayoutItem_Invalidate,
                         void(*)(XLayoutItem*))(self);
}

XWidgetSizePolicyControlTypes XLayoutItem_controlTypes_base(
    const XLayoutItem* self)
{
    if (!self || !XClassGetVtable(self))
        return XWidgetSizePolicyControl_DefaultType;
    return XClassGetVirtualFunc(self, EXLayoutItem_ControlTypes,
        XWidgetSizePolicyControlTypes(*)(const XLayoutItem*))(self);
}

XSpacerItem* XLayoutItem_spacerItem_base(const XLayoutItem* self)
{
    if (!self || !XClassGetVtable(self)) return NULL;
    return XClassGetVirtualFunc(self, EXLayoutItem_SpacerItem,
                                XSpacerItem*(*)(const XLayoutItem*))(self);
}

/* ==================== 对齐访问（对标 QLayoutItem::alignment/setAlignment） ==================== */

XLayoutAlignments XLayoutItem_alignment(const XLayoutItem* self)
{
    if (!self) return 0;
    return self->m_alignment;
}

void XLayoutItem_setAlignment(XLayoutItem* self, XLayoutAlignments alignment)
{
    if (!self) return;
    self->m_alignment = alignment;
    self->m_hasAlignment = (alignment != 0) ? 1 : 0;
    XLayoutItem_invalidate_base(self);
}

#endif /* XLAYOUT_ON */
