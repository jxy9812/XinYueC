/******************************************************************************
 * @file       XGraphicsEffect.h
 * @brief      XGraphicsEffect 图形效果基类（对标 Qt 6.8 QGraphicsEffect
 *            : QObject）。
 * @details    继承 XObject，提供效果对象的公共 API 面：setEnabled/isEnabled
 *             （默认启用）、enabledChanged(bool) 信号与 update() 请求重绘
 *             入口。效果实例经 XWidget_setGraphicsEffect 挂接到控件
 *             （对标 QWidget::setGraphicsEffect）。
 * @note       模块总开关 XWIDGET_ON 有效（效果由 XWidget 承载）。
 * @note       QGraphicsEffect 的绘制虚接口（draw/boundingRectFor/
 *             sourceChanged 等 protected 面）与 XGraphicsEffectSource 不建
 *             对应物：XGui 暂无 QGraphicsEffectSource，效果绘制由 XWidget
 *             渲染管线后续扩展；update() 当前仅占位不触发重绘。
 * @author     XinYueC 团队
 ******************************************************************************/
#ifndef XGRAPHICSEFFECT_H
#define XGRAPHICSEFFECT_H
#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>
#include "XGuiConfig.h"
#include "XObject.h"
#include "XClass.h"
#include "XGeometry.h"

#if XWIDGET_ON

/** @brief XWidget 前向声明（效果源承载；完整定义见 XWidget.h）。 */
typedef struct XWidget XWidget;

XCLASS_DEFINE_BEGING(XGraphicsEffect)
XCLASS_DEFINE_EXTEND_END(XGraphicsEffect, XObject)

/**
 * @brief      XGraphicsEffect 图形效果对象；m_class 必须是第一个成员。
 * @details    当前为纯状态对象（启用标志）；不持有平台资源。
 */
typedef struct XGraphicsEffect
{
    XObject m_class;   /**< 基类成员；必须是第一个。 */
    bool m_enabled;    /**< 启用标志；默认 true。 */
    XWidget* m_source; /**< 效果源（对标 QGraphicsEffect 的 source；XGui 以挂接
                            控件承载，借用指针，由 XWidget_setGraphicsEffect 设置）。 */
} XGraphicsEffect;

/**
 * @brief      XGraphicsEffect 类虚函数表初始化（对标 Qt 同名接口）。
 * @return     共享 XVtable 指针。
 */
XVtable* XGraphicsEffect_class_init(void);

/**
 * @brief      初始化 XGraphicsEffect（对标 QGraphicsEffect 构造）。
 * @param      self 目标对象指针；不可为 NULL。
 * @return     无返回值。
 */
void XGraphicsEffect_init(XGraphicsEffect* self);
#define XGraphicsEffect_create() XGraphicsEffect_create_ex(XCLASS_DEFAULT_MEMORY_TYPE)
/**
 * @brief      使用指定内存类型创建 XGraphicsEffect。
 * @param      memory 对象内存类型。
 * @return     新对象指针；失败返回 NULL。
 */
XGraphicsEffect* XGraphicsEffect_create_ex(XMemoryType memory);
#define XGraphicsEffect_deinit_base(self) XClass_deinit_base((XClass*)(self))
#define XGraphicsEffect_delete_base(self) XClass_delete_base((XClass*)(self))

/**
 * @brief      查询效果是否启用（对标 QGraphicsEffect::isEnabled）。
 * @param      self 目标效果；可为 NULL。
 * @return     启用返回 true；无效返回 false。
 */
bool XGraphicsEffect_isEnabled(const XGraphicsEffect* self);
/**
 * @brief      设置启用状态（对标 QGraphicsEffect::setEnabled）。
 * @details    状态实际变化时发射 enabledChanged(bool)。
 * @param      self 目标效果。
 * @param      enable true=启用，false=禁用。
 * @return     无返回值。
 */
void XGraphicsEffect_setEnabled(XGraphicsEffect* self, bool enable);
/**
 * @brief      请求效果重绘（对标 QGraphicsEffect::update）。
 * @note       当前为占位实现：XGui 渲染管线尚未接入效果绘制。
 * @param      self 目标效果。
 * @return     无返回值。
 */
void XGraphicsEffect_update(XGraphicsEffect* self);

/**
 * @brief      启用状态变化信号（对标 QGraphicsEffect::enabledChanged；
 *             setEnabled 与手动触发时发射）。
 * @param      self 目标效果。
 * @param      enabled 新启用状态。
 * @return     信号标识。
 */
void* XGraphicsEffect_enabledChanged_signal(XGraphicsEffect* self, bool enabled);

/* ==================== 效果源与包围盒（对标 source/boundingRect） ========== */

/**
 * @brief      获取效果源（对标 QGraphicsEffect::source）。
 *
 * @details    Qt 返回内部 QGraphicsEffectSource*；XGui 未建该来源抽象，
 *             以挂接的效果承载控件（XWidget*，借用）作为源，由
 *             XWidget_setGraphicsEffect 挂接时设置。
 *
 * @param      self 目标效果；可为 NULL。
 * @return     效果源控件借用指针；无源时为 NULL。
 */
XWidget* XGraphicsEffect_source(const XGraphicsEffect* self);

/**
 * @brief      设置效果源（XGui 适配接口，非 Qt 公共 API）。
 *
 * @details    仅供 XWidget_setGraphicsEffect 挂接/摘除效果时调用，用于
 *             维护 source() 的返回值；不转移所有权。
 *
 * @param      self 目标效果。
 * @param      source 效果源控件借用指针；可为 NULL 清除。
 * @return     无返回值。
 */
void XGraphicsEffect_setSource(XGraphicsEffect* self, XWidget* source);

/**
 * @brief      按源矩形计算效果包围盒（对标 QGraphicsEffect::boundingRectFor）。
 *
 * @details    基类实现原样返回源矩形（与 Qt 的 QGraphicsEffect 基类一致），
 *             派生效果可覆盖语义。
 *
 * @param      self 目标效果；可为 NULL。
 * @param      sourceRect 源矩形；为 NULL 时返回空矩形。
 * @return     效果作用后的包围盒。
 */
XRectF XGraphicsEffect_boundingRectFor(const XGraphicsEffect* self,
                                       const XRectF* sourceRect);

/**
 * @brief      获取效果包围盒（对标 QGraphicsEffect::boundingRect）。
 *
 * @details    有源时返回 boundingRectFor(源控件几何)；无源时返回空矩形，
 *             与 Qt 的 `if (d->source) ... return QRectF();` 一致。
 *
 * @param      self 目标效果；可为 NULL。
 * @return     效果包围盒（浮点矩形）。
 */
XRectF XGraphicsEffect_boundingRect(const XGraphicsEffect* self);

#endif /* XWIDGET_ON */

#endif /* XGRAPHICSEFFECT_H */
