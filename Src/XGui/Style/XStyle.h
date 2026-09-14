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

XCLASS_DEFINE_BEGING(XStyle)
XCLASS_DEFINE_ENUM(XStyle, DrawPrimitive) = XCLASS_VTABLE_GET_SIZE(XObject),
XCLASS_DEFINE_ENUM(XStyle, DrawControl),
XCLASS_DEFINE_ENUM(XStyle, DrawComplexControl),
XCLASS_DEFINE_ENUM(XStyle, PixelMetric),
XCLASS_DEFINE_ENUM(XStyle, SizeFromContents),
XCLASS_DEFINE_ENUM(XStyle, Polish),
XCLASS_DEFINE_ENUM(XStyle, Unpolish),
XCLASS_DEFINE_END(XStyle)

/**
 * @brief 样式引擎基类（对标 Qt 6.8 QStyle）。
 *
 *        定义 drawPrimitive/drawControl/drawComplexControl/pixelMetric/
 *        sizeFromContents/polish 虚表槽位；XCommonStyle 提供公共实现，
 *        XFusionStyle 提供 Fusion 主题。控件绘制通过
 *        XStyle_drawControl/drawPrimitive 分派。
 */
typedef struct XStyle
{
    XObject m_base;   /**< 基类成员；必须是第一个。 */
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
 * @param ct 内容类型（0 按钮/1 复选/2 单选/3 输入框/4 页签/5 菜单项）。
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
