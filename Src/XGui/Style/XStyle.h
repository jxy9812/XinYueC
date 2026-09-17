#ifndef XSTYLE_H
#define XSTYLE_H
#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>
#include "XGuiConfig.h"
#include "XObject.h"
#include "XGeometry.h"
#include "XStyleOption.h"

#if XSTYLE_ON

typedef struct XPainter XPainter;
typedef struct XWidget XWidget;
typedef struct XPixmap XPixmap;
typedef struct XIcon XIcon;
typedef struct XFont XFont;
typedef struct XPalette XPalette;

XCLASS_DEFINE_BEGING(XStyle)
XCLASS_DEFINE_ENUM(XStyle, DrawPrimitive) = XCLASS_VTABLE_GET_SIZE(XObject),
XCLASS_DEFINE_ENUM(XStyle, DrawControl),
XCLASS_DEFINE_ENUM(XStyle, DrawComplexControl),
XCLASS_DEFINE_ENUM(XStyle, PixelMetric),
XCLASS_DEFINE_ENUM(XStyle, SizeFromContents),
XCLASS_DEFINE_ENUM(XStyle, Polish),
XCLASS_DEFINE_ENUM(XStyle, Unpolish),
XCLASS_DEFINE_ENUM(XStyle, StyleHint),
XCLASS_DEFINE_ENUM(XStyle, SubElementRect),
XCLASS_DEFINE_ENUM(XStyle, SubControlRect),
XCLASS_DEFINE_ENUM(XStyle, HitTestComplexControl),
XCLASS_DEFINE_ENUM(XStyle, StandardPixmap),
XCLASS_DEFINE_ENUM(XStyle, StandardIcon),
XCLASS_DEFINE_ENUM(XStyle, GeneratedIconPixmap),
XCLASS_DEFINE_ENUM(XStyle, LayoutSpacing),
XCLASS_DEFINE_ENUM(XStyle, DrawItemText),
XCLASS_DEFINE_ENUM(XStyle, DrawItemPixmap),
XCLASS_DEFINE_ENUM(XStyle, ItemTextRect),
XCLASS_DEFINE_ENUM(XStyle, ItemPixmapRect),
XCLASS_DEFINE_ENUM(XStyle, StandardPalette),
XCLASS_DEFINE_END(XStyle)

/**
 * @brief 样式引擎基类（对标 Qt 6.8 QStyle）。
 *
 *        定义 drawPrimitive/drawControl/drawComplexControl/pixelMetric/
 *        sizeFromContents/styleHint/subElementRect/subControlRect/
 *        hitTestComplexControl/standardPixmap/standardIcon/
 *        generatedIconPixmap/layoutSpacing/drawItemText/drawItemPixmap/
 *        itemTextRect/itemPixmapRect/standardPalette/polish/unpolish
 *        虚表槽位；XCommonStyle 提供公共实现，XFusionStyle 提供 Fusion
 *        主题。控件绘制通过 XStyle_drawControl/drawPrimitive 分派。
 */
typedef struct XStyle
{
    XObject m_base;   /**< 基类成员；必须是第一个。 */
    struct XStyle* m_proxy; /**< 被代理的底层样式（对标 QStyle 私有成员 proxyStyle；
                                 借用指针，包装样式（XStyleSheetStyle）设置，其余为 NULL）。 */
} XStyle;

XVtable* XStyle_class_init(void);

/**
 * @brief 初始化嵌入式样式。
 *
 * @param self 目标样式指针，不能为空。
 * @return 无返回值。
 */
void XStyle_init(XStyle* self);

/**
 * @brief 堆上创建样式。
 *
 * @param memory 内存类型。
 * @return 样式指针；分配失败返回 NULL。
 */
XStyle* XStyle_create_ex(XMemoryType memory);
#define XStyle_create() XStyle_create_ex(XCLASS_DEFAULT_MEMORY_TYPE)

/** @brief 析构入口（查表分派父类析构）。 */
#define XStyle_deinit_base(self) XClass_deinit_base((XClass*)(self))

/** @brief 删除堆上样式（查表分派析构并释放内存）。 */
#define XStyle_delete_base(self) XClass_delete_base((XClass*)(self))

/* ==================== 样式标识与代理（对标 QStyle::name/proxy） ==================== */

/**
 * @brief 读取样式类名（对标 QStyle::name）。
 *
 *        Qt 的 name() 返回 metaObject()->className()，此处等价取虚表类名
 *        （如 "XCommonStyle"/"XFusionStyle"/"XStyleSheetStyle"）；名称前缀
 *        为 X（XGui 类名），语义与 Qt 一一对应。
 *
 * @param self 目标样式指针；可为 NULL。
 * @return UTF-8 类名字符串（静态/虚表存储，不得释放）；self 为 NULL 时返回 ""。
 */
const char* XStyle_name(const XStyle* self);

/**
 * @brief 读取被代理的底层样式（对标 QStyle::proxy）。
 *
 *        多数样式无代理，返回 NULL；XStyleSheetStyle 包装底层样式时返回
 *        被包装样式，与 Qt 的 QStyleSheetStyle::proxy() 语义一致。
 *
 * @param self 目标样式指针；可为 NULL。
 * @return 被代理样式借用指针（不转移所有权）；无代理时为 NULL。
 */
XStyle* XStyle_proxy(const XStyle* self);

/* ==================== 绘制分派（对标 QStyle） ==================== */

/**
 * @brief 绘制基元元素（分派 drawPrimitive）。
 *
 * @param self 目标样式指针。
 * @param pe XStylePrimitiveElement 枚举。
 * @param option 样式选项（局部坐标）。
 * @param painter 目标画家。
 * @param widget 关联控件（可空，借用）。
 * @return 无返回值。
 */
void XStyle_drawPrimitive(XStyle* self, int pe, const XStyleOption* option,
                          XPainter* painter, const XWidget* widget);

/**
 * @brief 绘制控件元素（分派 drawControl）。
 *
 * @param self 目标样式指针。
 * @param ce XStyleControlElement 枚举。
 * @param option 样式选项。
 * @param painter 目标画家。
 * @param widget 关联控件（可空，借用）。
 * @return 无返回值。
 */
void XStyle_drawControl(XStyle* self, int ce, const XStyleOption* option,
                        XPainter* painter, const XWidget* widget);

/**
 * @brief 绘制复杂控件（分派 drawComplexControl）。
 *
 * @param self 目标样式指针。
 * @param cc XStyleComplexControl 枚举。
 * @param option 样式选项。
 * @param painter 目标画家。
 * @param widget 关联控件（可空，借用）。
 * @return 无返回值。
 */
void XStyle_drawComplexControl(XStyle* self, int cc,
                               const XStyleOption* option,
                               XPainter* painter, const XWidget* widget);

/**
 * @brief 查询像素度量（分派 pixelMetric）。
 *
 * @param self 目标样式指针。
 * @param pm XStylePixelMetric 枚举。
 * @param option 样式选项（可空）。
 * @return 像素值。
 */
int XStyle_pixelMetric(XStyle* self, int pm, const XStyleOption* option);

/**
 * @brief 计算内容尺寸（分派 sizeFromContents）。
 *
 * @param self 目标样式指针。
 * @param ct XStyleContentsType 枚举。
 * @param option 样式选项。
 * @param contentSize 内容尺寸。
 * @return 总尺寸。
 */
XSize XStyle_sizeFromContents(XStyle* self, int ct,
                              const XStyleOption* option,
                              XSize contentSize);

/**
 * @brief 应用样式到控件（分派 polish）。
 *
 * @param self 目标样式指针。
 * @param widget 目标控件。
 * @return 无返回值。
 */
void XStyle_polish(XStyle* self, XWidget* widget);

/**
 * @brief 撤销样式（分派 unpolish）。
 *
 * @param self 目标样式指针。
 * @param widget 目标控件。
 * @return 无返回值。
 */
void XStyle_unpolish(XStyle* self, XWidget* widget);

/**
 * @brief 查询样式提示（分派 styleHint，对标 QStyle::styleHint）。
 *
 * @param self 目标样式指针。
 * @param hint XStyleStyleHint 枚举。
 * @param option 样式选项（可空）。
 * @param widget 关联控件（可空，借用）。
 * @return 提示值（int 语义按枚举而定）。
 */
int XStyle_styleHint(XStyle* self, int hint, const XStyleOption* option,
                     const XWidget* widget);

/**
 * @brief 计算子元素矩形（分派 subElementRect）。
 *
 * @param self 目标样式指针。
 * @param subElement XStyleSubElement 枚举。
 * @param option 样式选项。
 * @param widget 关联控件（可空，借用）。
 * @return 子元素矩形。
 */
XRect XStyle_subElementRect(XStyle* self, int subElement,
                            const XStyleOption* option,
                            const XWidget* widget);

/**
 * @brief 计算复杂控件子控件矩形（分派 subControlRect）。
 *
 * @param self 目标样式指针。
 * @param cc XStyleComplexControl 枚举。
 * @param option 样式选项。
 * @param sc XStyleSubControl 位标志。
 * @param widget 关联控件（可空，借用）。
 * @return 子控件矩形。
 */
XRect XStyle_subControlRect(XStyle* self, int cc,
                            const XStyleOption* option, int sc,
                            const XWidget* widget);

/**
 * @brief 命中测试复杂控件（分派 hitTestComplexControl）。
 *
 * @param self 目标样式指针。
 * @param cc XStyleComplexControl 枚举。
 * @param option 样式选项。
 * @param x 命中点 X。
 * @param y 命中点 Y。
 * @param widget 关联控件（可空，借用）。
 * @return XStyleSubControl 位标志。
 */
int XStyle_hitTestComplexControl(XStyle* self, int cc,
                                 const XStyleOption* option, int x, int y,
                                 const XWidget* widget);

/**
 * @brief 生成标准位图（分派 standardPixmap；返回新对象，调用方删除）。
 *
 * @param self 目标样式指针。
 * @param sp XStyleStandardPixmap 枚举。
 * @param option 样式选项（可空）。
 * @param widget 关联控件（可空，借用）。
 * @return 新位图对象（NULL=无）。
 */
XPixmap* XStyle_standardPixmap(XStyle* self, int sp,
                               const XStyleOption* option,
                               const XWidget* widget);

/**
 * @brief 生成标准图标（分派 standardIcon；返回新对象，调用方删除）。
 *
 * @param self 目标样式指针。
 * @param sp XStyleStandardPixmap 枚举。
 * @param option 样式选项（可空）。
 * @param widget 关联控件（可空，借用）。
 * @return 新图标对象（NULL=无）。
 */
XIcon* XStyle_standardIcon(XStyle* self, int sp,
                           const XStyleOption* option,
                           const XWidget* widget);

/**
 * @brief 生成图标位图（分派 generatedIconPixmap；返回新对象，调用方删除）。
 *
 * @param self 目标样式指针。
 * @param mode 图标模式（XIconMode：0 正常/1 禁用/2 活动/3 选中）。
 * @param pixmap 源位图。
 * @param option 样式选项（可空）。
 * @return 新位图对象（NULL=无）。
 */
XPixmap* XStyle_generatedIconPixmap(XStyle* self, int mode,
                                    const XPixmap* pixmap,
                                    const XStyleOption* option);

/**
 * @brief 查询布局间距（分派 layoutSpacing）。
 *
 * @param self 目标样式指针。
 * @param control1 控制类型 1（XSizePolicy 控制位）。
 * @param control2 控制类型 2。
 * @param orientation 方向：0 水平/1 垂直。
 * @param option 样式选项（可空）。
 * @param widget 关联控件（可空，借用）。
 * @return 间距；-1=使用布局默认。
 */
int XStyle_layoutSpacing(XStyle* self, int control1, int control2,
                         int orientation, const XStyleOption* option,
                         const XWidget* widget);

/**
 * @brief 查询两组控件类型之间的合并布局间距（对标
 *        QStyle::combinedLayoutSpacing）。
 *
 *        按 Qt 语义对两组控件类型位组合做笛卡尔积，逐对调用
 *        layoutSpacing 并取最大值（初始 0；因此无实现注册时返回 0，
 *        与 Qt 的 qMax(-1, 0) 行为一致）。
 *
 * @param self 目标样式指针。
 * @param controls1 第一组控件类型位组合（XWidgetSizePolicyControlType 位或）。
 * @param controls2 第二组控件类型位组合（XWidgetSizePolicyControlType 位或）。
 * @param orientation 方向：0 水平/1 垂直。
 * @param option 样式选项（可空）。
 * @param widget 关联控件（可空，借用）。
 * @return 两组之间最大间距；任一组为空位组合时返回 0。
 */
int XStyle_combinedLayoutSpacing(XStyle* self, int controls1, int controls2,
                                 int orientation, const XStyleOption* option,
                                 const XWidget* widget);

/**
 * @brief 绘制对齐文本（分派 drawItemText）。
 *
 * @param self 目标样式指针。
 * @param painter 目标画家。
 * @param rect 目标矩形。
 * @param alignment XAlignment 位组合。
 * @param palette 调色板（借用）。
 * @param enabled 是否启用。
 * @param text UTF-8 文本（可空）。
 * @param textRole XPaletteColorRole 文本角色。
 * @return 无返回值。
 */
void XStyle_drawItemText(XStyle* self, XPainter* painter,
                         const XRect* rect, int alignment,
                         const XPalette* palette, bool enabled,
                         const char* text, int textRole);

/**
 * @brief 绘制对齐位图（分派 drawItemPixmap）。
 *
 * @param self 目标样式指针。
 * @param painter 目标画家。
 * @param rect 目标矩形。
 * @param alignment XAlignment 位组合。
 * @param pixmap 源位图（借用）。
 * @return 无返回值。
 */
void XStyle_drawItemPixmap(XStyle* self, XPainter* painter,
                           const XRect* rect, int alignment,
                           const XPixmap* pixmap);

/**
 * @brief 计算文本矩形（分派 itemTextRect；字体度量用 XFont 近似）。
 *
 * @param self 目标样式指针。
 * @param font 字体（借用；NULL 用画布默认）。
 * @param rect 目标矩形。
 * @param alignment XAlignment 位组合。
 * @param enabled 是否启用。
 * @param text UTF-8 文本（可空）。
 * @return 文本矩形。
 */
XRect XStyle_itemTextRect(XStyle* self, const XFont* font,
                          const XRect* rect, int alignment, bool enabled,
                          const char* text);

/**
 * @brief 计算位图矩形（分派 itemPixmapRect）。
 *
 * @param self 目标样式指针。
 * @param rect 目标矩形。
 * @param alignment XAlignment 位组合。
 * @param pixmap 源位图（借用）。
 * @return 位图矩形。
 */
XRect XStyle_itemPixmapRect(XStyle* self, const XRect* rect,
                            int alignment, const XPixmap* pixmap);

/**
 * @brief 读取标准调色板（分派 standardPalette）。
 *
 * @param self 目标样式指针。
 * @return 标准调色板（按值返回）。
 */
XPalette XStyle_standardPalette(XStyle* self);

/* ==================== 静态工具（对标 QStyle 静态函数） ==================== */

/**
 * @brief 布局方向视觉矩形转换（对标 QStyle::visualRect）。
 *
 * @param direction 布局方向：0 左到右/1 右到左。
 * @param boundingRect 包围矩形。
 * @param logicalRect 逻辑矩形。
 * @return 视觉矩形。
 */
XRect XStyle_visualRect(int direction, const XRect* boundingRect,
                        const XRect* logicalRect);

/**
 * @brief 布局方向视觉坐标转换（对标 QStyle::visualPos）。
 *
 * @param direction 布局方向：0 左到右/1 右到左。
 * @param boundingRect 包围矩形。
 * @param logicalPos 逻辑坐标。
 * @return 视觉坐标。
 */
XPoint XStyle_visualPos(int direction, const XRect* boundingRect,
                        const XPoint* logicalPos);

/**
 * @brief 滑块逻辑值 → 像素位置（对标 QStyle::sliderPositionFromValue）。
 *
 * @param min 最小值。
 * @param max 最大值。
 * @param logicalValue 逻辑值。
 * @param span 像素跨度。
 * @param upsideDown 是否反向。
 * @return 像素位置。
 */
int XStyle_sliderPositionFromValue(int min, int max, int logicalValue,
                                   int span, bool upsideDown);

/**
 * @brief 滑块像素位置 → 逻辑值（对标 QStyle::sliderValueFromPosition）。
 *
 * @param min 最小值。
 * @param max 最大值。
 * @param pos 像素位置。
 * @param span 像素跨度。
 * @param upsideDown 是否反向。
 * @return 逻辑值。
 */
int XStyle_sliderValueFromPosition(int min, int max, int pos, int span,
                                   bool upsideDown);

/**
 * @brief 布局方向对齐转换（对标 QStyle::visualAlignment）。
 *
 * @param direction 布局方向：0 左到右/1 右到左。
 * @param alignment XAlignment 位组合。
 * @return 转换后对齐位。
 */
int XStyle_visualAlignment(int direction, int alignment);

/**
 * @brief 计算对齐矩形（对标 QStyle::alignedRect）。
 *
 * @param direction 布局方向：0 左到右/1 右到左。
 * @param alignment XAlignment 位组合。
 * @param size 内容尺寸。
 * @param rectangle 目标矩形。
 * @return 对齐后矩形。
 */
XRect XStyle_alignedRect(int direction, int alignment, const XSize* size,
                         const XRect* rectangle);

/* ==================== 全局默认样式 ==================== */

/**
 * @brief 设置全局默认样式（替换旧的并删除）。
 *
 * @param style 新默认样式（转移所有权）。
 * @return 无返回值。
 */
void XStyle_setDefaultStyle(XStyle* style);

/**
 * @brief 读取全局默认样式。
 *
 * @return 默认样式指针（生命周期由模块管理）。
 */
XStyle* XStyle_defaultStyle(void);

/**
 * @brief 安装全局样式表（对标 QApplication::setStyleSheet）。
 *
 *        以当前默认样式为底层源创建/复用 XStyleSheetStyle 并解析 CSS；
 *        后续绘制按规则匹配覆盖。重复调用更新规则表。
 *
 * @param css UTF-8 样式表文本（NULL/空=仅清除规则，保留包装）。
 * @return 解析成功返回 true。
 */
bool XStyle_installStyleSheet(const char* css);

#endif /* XSTYLE_ON */
#ifdef __cplusplus
}
#endif
#endif /* XSTYLE_H */
