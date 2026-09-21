/******************************************************************************
 * @file       XGraphicsEffect.h
 * @brief      XGraphicsEffect 图形效果基类（对标 Qt 6.8 QGraphicsEffect
 *            : QObject）。
 * @details    继承 XObject，提供效果对象的公共 API 面：setEnabled/isEnabled
 *             （默认启用）、enabledChanged(bool) 信号与 update() 请求重绘
 *             入口。效果实例经 XWidget_setGraphicsEffect 挂接到控件
 *             （对标 QWidget::setGraphicsEffect）。
 * @note       渲染管线（对标 QGraphicsEffect::draw 的 source→pixmap→draw
 *             离屏管线）：XWidget paintTree 派发本控件绘制前，把携带启用
 *             效果的控件+可见子树渲染到 ARGB32_Premultiplied 离屏快照，
 *             再经 XGraphicsEffect_drawWidget 按"脏区∩控件区域"完成效果
 *             处理并回贴绘制目标；处理区域外扩量由虚函数 boundingRectFor
 *             的语义给出。三种内置效果对应物：XGraphicsOpacityEffect
 *             （对标 QGraphicsOpacityEffect）、XGraphicsBlurEffect（对标
 *             QGraphicsBlurEffect，简化盒式模糊）、
 *             XGraphicsDropShadowEffect（对标 QGraphicsDropShadowEffect，
 *             固定模糊）。
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
/** @brief XImage 前向声明（离屏快照/效果画布；完整定义见 XImage.h）。 */
typedef struct XImage XImage;

XCLASS_DEFINE_BEGING(XGraphicsEffect)
XCLASS_DEFINE_ENUM(XGraphicsEffect, SourceChanged) = XCLASS_VTABLE_GET_SIZE(XObject), /**< 源变更虚函数（对标
    QGraphicsEffect::sourceChanged）；首槽必须锚定父类槽位总数，
    否则从 0 起编号会覆写根基类槽（EXClass_Deinit 等） */
XCLASS_DEFINE_ENUM(XGraphicsEffect, BoundingRectFor), /**< 效果包围盒虚函数
    （对标 QGraphicsEffect::boundingRectFor） */
XCLASS_DEFINE_ENUM(XGraphicsEffect, Draw),             /**< 效果绘制虚函数
    （对标 QGraphicsEffect::draw） */
XCLASS_DEFINE_END(XGraphicsEffect)  /* 自动续号（=父槽总数+3）；注意
    不可用 EXTEND_END——它会把 END_SIZE 重置回父类槽位数，容量被砍
    导致 OVERLOAD 索引越界。 */

/**
 * @brief      源变更标志位（对标 QGraphicsEffect::ChangeFlag 数值语义）。
 */
typedef enum XGraphicsEffectChangeFlag
{
    XGraphicsEffectChange_ContentsChanged = 1 << 0, /**< 源内容变化（对标
        QGraphicsEffect::ContentsChanged）。 */
    XGraphicsEffectChange_GeometryChanged = 1 << 1  /**< 源几何变化（对标
        QGraphicsEffect::GeometryChanged）。 */
} XGraphicsEffectChangeFlag;

/**
 * @brief      XGraphicsEffect 图形效果对象；m_class 必须是第一个成员。
 * @details    持有启用标志与效果源借用指针；不持有平台资源。
 */
typedef struct XGraphicsEffect
{
    XObject m_class;   /**< 基类成员；必须是第一个。 */
    bool m_enabled;    /**< 启用标志；默认 true。 */
    XWidget* m_source; /**< 效果源（对标 QGraphicsEffect 的 source；XGui 以挂接
                            控件承载，借用指针，由 XWidget_setGraphicsEffect 设置）。 */
} XGraphicsEffect;

/**
 * @brief      效果绘制上下文（XGui 适配结构；对标 draw(QPainter*) 的
 *             source+目标画布二元组）。
 * @details    管线把控件+可见子树的快照与效果输出画布一并交给 Draw 虚
 *             函数；两者均为 ARGB32_Premultiplied、0xAARRGGBB 本机端序。
 */
typedef struct XGraphicsEffectDrawContext
{
    XImage* m_source;     /**< 源离屏快照（控件全幅，widget 局部坐标对齐；
                               借用；绘制期间有效）。 */
    XRect   m_sourceRect; /**< 本次处理的源区域（控件局部坐标；已按效果
                               外扩量与脏区裁剪）。 */
    XImage* m_dest;       /**< 效果输出画布（尺寸=m_destRect，从全透明
                               开始；Draw 实现负责填充全部内容）。 */
    XRect   m_destRect;   /**< m_dest 覆盖的控件局部矩形 = boundingRectFor
                               (m_sourceRect) 取整。 */
} XGraphicsEffectDrawContext;

/** @brief Draw 虚函数槽位类型（对标 QGraphicsEffect::draw）。 */
typedef void (*XGraphicsEffect_DrawSlot)(XGraphicsEffect* self,
                                         XGraphicsEffectDrawContext* ctx);
/** @brief BoundingRectFor 虚函数槽位类型（对标 boundingRectFor）。 */
typedef XRectF (*XGraphicsEffect_BoundingRectForSlot)(
    const XGraphicsEffect* self, const XRectF* sourceRect);
/** @brief SourceChanged 虚函数槽位类型（对标 sourceChanged）。 */
typedef void (*XGraphicsEffect_SourceChangedSlot)(XGraphicsEffect* self,
                                                  int flags);

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
 * @details    状态实际变化时发射 enabledChanged(bool)，并经 update() 请求
 *             重绘（对标 Qt：setEnabled 变化即触发源重绘，禁用时恢复
 *             无效果画面）。
 * @param      self 目标效果。
 * @param      enable true=启用，false=禁用。
 * @return     无返回值。
 */
void XGraphicsEffect_setEnabled(XGraphicsEffect* self, bool enable);
/**
 * @brief      请求效果重绘（对标 QGraphicsEffect::update）。
 * @details    有源时把效果包围盒（boundingRect，含效果外扩）整体标记为
 *             脏区；无源为无操作。
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
 * @brief      源变更通知（对标 QGraphicsEffect::sourceChanged，经虚表
 *             分派到 SourceChanged 槽位）。
 *
 * @details    基类默认实现转 update() 请求重绘；派生效果可覆盖。
 *             XWidget_setGraphicsEffect 挂接效果时以
 *             ContentsChanged|GeometryChanged 触发，保证效果安装即重绘。
 *
 * @param      self 目标效果；可为 NULL。
 * @param      flags XGraphicsEffectChangeFlag 位或组合。
 * @return     无返回值。
 */
void XGraphicsEffect_sourceChanged(XGraphicsEffect* self, int flags);

/**
 * @brief      按源矩形计算效果包围盒（对标 QGraphicsEffect::boundingRectFor，
 *             经虚表分派到 BoundingRectFor 槽位）。
 *
 * @details    基类默认实现原样返回源矩形（与 Qt 的 QGraphicsEffect 基类
 *             一致），内置派生效果覆盖为"源矩形按效果外扩量生长"。
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

/* ==================== 效果绘制管线（XGui 适配接口） ==================== */

/**
 * @brief      效果挂接绘制入口：对控件执行"效果处理并回贴"（XGui 适配
 *             接口，对标 Qt drawWidget 中 effect->draw(&p) 分支）。
 *
 * @details    由 XWidget paintTree 的效果挂接渲染段在控件常规绘制派发前
 *             调用；sourceImage 须为控件全幅、未施效的离屏快照（快照的
 *             渲染由 XWidget 侧完成，期间效果被临时摘除以防递归）。管线：
 *              1. 取控件绘制目标与偏移（XWidget_paintImage/paintOffset）；
 *              2. 处理区域 = 脏区∩控件区域（性能边界：效果控件区域小于
 *                 视口时仅处理该区域），再按 boundingRectFor 的外扩量
 *                 生长为 sourceRect/destRect；
 *              3. 创建 destRect 尺寸的透明画布，经 Draw 槽位虚分派完成
 *                 效果处理（对标 source→pixmap→draw 管线的 draw 步）；
 *              4. 经 XPainter_drawImage 把画布回贴到绘制目标。
 *             任一步无法完成（无绘制目标/空区域/画布分配失败）返回
 *             false，调用方回退常规绘制路径。
 *
 * @param      self 目标效果；不可为 NULL。
 * @param      widget 效果源控件；不可为 NULL。
 * @param      sourceImage 控件全幅离屏快照（借用）；不可为 NULL 且有效。
 * @param      paintRegion 本控件局部坐标绘制区域；不可为 NULL。
 * @return     效果处理并回贴成功返回 true；否则返回 false。
 */
bool XGraphicsEffect_drawWidget(XGraphicsEffect* self, XWidget* widget,
                                XImage* sourceImage, const XRegion* paintRegion);

/**
 * @brief      对图像指定区域做原地 3x3 盒式模糊（XGui 内部共享助手）。
 *
 * @details    水平+垂直两趟分离 3 点盒式均值（对标 3x3 卷积核），逐通道
 *             独立处理，因此与 0xAARRGGBB 的通道字节序无关；区域外像素
 *             视为透明参与均值。供 XGraphicsBlurEffect 与
 *             XGraphicsDropShadowEffect 复用，非 Qt 公共 API。
 *
 * @param      image 目标图像（ARGB32_Premultiplied）；不可为 NULL。
 * @param      rect 模糊区域（图像坐标）；NULL 表示整幅。
 * @return     无返回值。
 */
void XGraphicsEffect_blurBox3(XImage* image, const XRect* rect);

#endif /* XWIDGET_ON */

#endif /* XGRAPHICSEFFECT_H */
