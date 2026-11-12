/******************************************************************************
 * @file       XGraphicsEffect.h
 * @brief      XGraphicsEffect 图形效果基类（对标 Qt 6.8 QGraphicsEffect
 *            : QObject）。
 * @details    继承 XObject，提供效果对象的公共 API 面：setEnabled/isEnabled
 *             （默认启用）、enabledChanged(bool) 信号与 update() 请求重绘
 *             入口。效果实例经 XWidget_setGraphicsEffect 挂接到控件
 *             （对标 QWidget::setGraphicsEffect），或经 W4 起的
 *             XGraphicsItem_setGraphicsEffect 挂接到图形项（对标
 *             QGraphicsItem::setGraphicsEffect；**同一效果只有一个源**，
 *             换宿主时旧宿主槽位摘空）。
 * @note       渲染管线（对标 QGraphicsEffect::draw 的 source→pixmap→draw
 *             离屏管线）有两条消费路径：
 *             ① 控件源：XWidget paintTree 派发本控件绘制前，把携带启用
 *                效果的控件+可见子树渲染到 ARGB32_Premultiplied 离屏快照，
 *                再经 XGraphicsEffect_drawWidget 按"脏区∩控件区域"完成效果
 *                处理并回贴绘制目标；
 *             ② 图形项源（W4）：XGraphicsScene_render 对挂效果的项先把整棵
 *                子树离屏渲染（画布坐标 = 项本地坐标平移），经
 *                XGraphicsEffect_drawToContext 派发 Draw，再把输出画布回贴
 *                到场景（对标 QGraphicsScenePrivate::drawItemHelper 的
 *                effect 分支）。
 *             两条路径的"处理区域外扩量"都由虚函数 boundingRectFor 的语义
 *             给出。三种内置效果对应物：XGraphicsOpacityEffect
 *             （对标 QGraphicsOpacityEffect，含 opacityMask）、
 *             XGraphicsBlurEffect（对标 QGraphicsBlurEffect，指数模糊核）、
 *             XGraphicsDropShadowEffect（对标 QGraphicsDropShadowEffect，
 *             指数模糊投影）。
 * @note       生命周期（与本机 Qt 6.8 实测逐条一致）：
 *             ① 控件**拥有**挂接的效果（XWidget_setGraphicsEffect）；换装/
 *                置 NULL/控件析构都会删除旧效果；
 *             ② 效果**不**被重设 QObject 父对象（Qt 实测 effect.parent()
 *                仍为 NULL）——故效果不会被父链带走两次，只有控件负责删；
 *             ③ 效果被直接删除时反查源控件并清空其 graphicsEffect 槽（对标
 *                Qt QWidgetEffectSourcePrivate::detach，实测 w.graphicsEffect()
 *                变 NULL），避免控件留悬垂指针；
 *             ④ 同一效果挂到第二个控件时从原控件**摘下**（Qt 实测
 *                w1.graphicsEffect() 变 NULL、w2 持有），防双删；
 *             ⑤ **图形项源同款**（W4，对标 QGraphicsItem::setGraphicsEffect，
 *                qgraphicsitem.cpp:2900-2919）：图形项拥有效果、换装删旧、
 *                项析构删效果、挂到第二项时从原项摘下、直接删效果时项的槽位
 *                摘空（见 XGraphicsItem_graphicsEffectDetach）；
 *             ⑥ 控件源与图形项源**互斥**（Qt 的 QGraphicsEffectSource 是单一
 *                指针）：从控件搬到图形项（或反向）时旧宿主槽位摘空，
 *                所有权随宿主转移。
 * @author     XinYueC 团队
 ******************************************************************************/
#ifndef XGRAPHICSEFFECT_H
#define XGRAPHICSEFFECT_H

#if XGUI_ON
#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>
#include "XGuiConfig.h"
#include "XObject.h"
#include "XClass.h"
#include "XGeometry.h"
#include "XImage.h"   /* XImage（离屏快照/效果画布）与 XImageTransform
                         （W4 的设备系取源矩阵；上下文按值持有）。 */

#if XWIDGET_ON

/** @brief XWidget 前向声明（效果源承载；完整定义见 XWidget.h）。 */
typedef struct XWidget XWidget;
/** @brief XPainter 前向声明（drawSource 绘制目标；完整定义见 XPainter.h）。 */
typedef struct XPainter XPainter;
/** @brief XGraphicsItem 前向声明（item 作为效果源；完整定义见
 *         XGraphicsItem.h，W4 起效果可挂到图形项上——对标
 *         QGraphicsItem::setGraphicsEffect）。 */
typedef struct XGraphicsItem XGraphicsItem;
/** @brief 效果绘制上下文前向声明（完整定义见下文
 *         XGraphicsEffectDrawContext；结构体持其借用指针）。 */
struct XGraphicsEffectDrawContext;

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
 * @brief      源像素图补边方式（对标 QGraphicsEffect::PixmapPadMode，枚举值
 *             逐项一致：qgraphicseffect.h:40-44）。
 * @details    仅用于 XGraphicsEffect_sourcePixmap：决定返回图像在源包围盒
 *             之外额外补多少透明边。
 */
typedef enum XGraphicsEffectPixmapPadMode
{
    XGraphicsEffectPadMode_NoPad = 0,                   /**< 不补边（对标 NoPad）。 */
    XGraphicsEffectPadMode_PadToTransparentBorder = 1,  /**< 四周各补 1px 透明边
        （对标 PadToTransparentBorder；Qt QWidgetEffectSourcePrivate::pixmap
        的 `sourceRect.adjusted(-1,-1,1,1)`，qwidget.cpp:5846-5847）。 */
    XGraphicsEffectPadMode_PadToEffectiveBoundingRect = 2 /**< 补到效果包围盒
        （对标 PadToEffectiveBoundingRect；即 boundingRectFor(源包围盒) 的
        整数对齐矩形，模糊/投影的外扩量全部落在图像内，
        qwidget.cpp:5844-5845）。 */
} XGraphicsEffectPixmapPadMode;

/**
 * @brief      源像素图/源包围盒的坐标系（对标 Qt::CoordinateSystem，
 *             **数值逐项一致**：qnamespace.h:1602-1605
 *             `enum CoordinateSystem { DeviceCoordinates, LogicalCoordinates };`
 *             ⇒ Device=0、Logical=1）。
 * @details    Qt 的 QGraphicsEffect::sourcePixmap/sourceBoundingRect 都带
 *             `Qt::CoordinateSystem system = Qt::LogicalCoordinates` 形参：
 *               - Logical：源本地逻辑坐标（图形项/控件自己的坐标）；
 *               - Device：**当前绘制上下文**的设备坐标（Qt 靠
 *                 QGraphicsItemEffectSourcePrivate::info 里的 painter 取得；
 *                 无绘制上下文时 Qt 打印 "Not yet implemented, lacking
 *                 device context" 告警并返回空——qgraphicsitem.cpp:11081-11085
 *                 的 `if (!info && deviceCoordinates)` 分支）。
 *             XGui 的适配：XGui 没有 QGraphicsEffectSource 抽象，设备变换由
 *             调用方经 `XGraphicsEffect_sourcePixmap_2` 的 deviceTransform
 *             形参显式传入（管线内部就是 XGraphicsScene_render 的
 *             XPainter_deviceTransform）。详见该函数的 @note。
 */
typedef enum XGraphicsEffectCoordinateSystem
{
    XGraphicsEffectCoordinateSystem_Device = 0,  /**< 设备坐标（DeviceCoordinates）。 */
    XGraphicsEffectCoordinateSystem_Logical = 1  /**< 逻辑坐标（LogicalCoordinates，
                                                      缺省）。 */
} XGraphicsEffectCoordinateSystem;

/**
 * @brief      源重绘回调（XGui 适配；对标 Qt QGraphicsEffectSource::draw
 *             内部的 `scene->draw(item, painter, ...)` 回环）。
 * @details    效果要拿到"设备分辨率的源像素图"时，必须把源**再画一遍**到
 *             效果自己的离屏画布上（Qt 由 QGraphicsItemEffectSourcePrivate::
 *             pixmap 内的 `scened->draw(...)` 完成）。XGui 把这一步收敛成
 *             本回调：管线在绘制上下文里挂上它，回调负责把源（控件子树 /
 *             图形项子树）按**源画布坐标**绘入传入的 painter（painter 的
 *             世界变换已由 sourcePixmap_2 设好，回调只需按画布坐标绘制，
 *             不要 reset）。
 * @param      userData 挂接方私有数据（借用；仅在本次 Draw 期间有效）。
 * @param      painter 目标绘制器（借用；用户坐标 = 源画布坐标）。
 */
typedef void (*XGraphicsEffectRenderSourceProc)(void* userData,
                                                XPainter* painter);

/**
 * @brief      源变更标志位（对标 QGraphicsEffect::ChangeFlag，位值逐项
 *             一致：qgraphicseffect.h:31-37）。
 */
typedef enum XGraphicsEffectChangeFlag
{
    XGraphicsEffectChange_SourceAttached = 0x1,             /**< 效果已挂接到源。 */
    XGraphicsEffectChange_SourceDetached = 0x2,             /**< 效果已从源摘除。 */
    XGraphicsEffectChange_SourceBoundingRectChanged = 0x4,  /**< 源包围盒变化。 */
    XGraphicsEffectChange_SourceInvalidated = 0x8           /**< 源外观已失效。 */
    /* 已废弃别名 ContentsChanged / GeometryChanged（原等价 SourceAttached）
       随 P2-22/23 删除（台账 docs/Qt68对齐_整改台账_20261111.md:400-401）：
       全库唯一消费者 apitest_labels 的别名断言已收敛到规范名。 */
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
    struct XGraphicsEffectDrawContext* m_drawCtx; /**< 本次 Draw 派发上下文
                            （借用，仅在 XGraphicsEffect_drawWidget 调用 Draw
                            槽位期间非 NULL；供 XGraphicsEffect_drawSource
                            取源快照。其余时刻为 NULL，禁止缓存）。 */
    XRect m_lastBounds;    /**< 上一次 update() 标脏的效果包围盒（父坐标；
                                有效性由 m_lastBoundsValid 判定）。作用：效果
                                参数收缩（如 blurRadius 减小）时把**旧**包围盒
                                一并标脏，否则外扩环残留（对标 Qt
                                updateBoundingRect 同时失效新旧矩形）。 */
    bool m_lastBoundsValid; /**< m_lastBounds 是否有效。 */
    /* ---- 新增字段一律追加在此注释之后（AGENTS.md 位置初始化器铁律）---- */
    struct XGraphicsItem* m_itemSource; /**< 图形项源（**借用**；对标
                                QGraphicsItem::setGraphicsEffect 建立的
                                QGraphicsItemEffectSource，W4）。与
                                m_source（控件源）互斥：同一效果只能有一个
                                源。由 XGraphicsItem_setGraphicsEffect 设置、
                                XGraphicsEffect_setItemSource 维护；效果析构
                                时经 XGraphicsItem_graphicsEffectDetach 摘空
                                源的槽位（防悬垂）。禁止释放。 */
} XGraphicsEffect;

/**
 * @brief      效果绘制上下文（XGui 适配结构；对标 draw(QPainter*) 的
 *             source+目标画布二元组）。
 * @details    管线把控件+可见子树的快照与效果输出画布一并交给 Draw 虚
 *             函数；两者均为 ARGB32_Premultiplied、0xAARRGGBB 本机端序。
 */
typedef struct XGraphicsEffectDrawContext
{
    XImage* m_source;     /**< 源离屏快照（借用；绘制期间有效）。控件源为
                               控件全幅快照（像素 (0,0) = 控件局部 (0,0)）；
                               图形项源为场景渲染的源位图（像素 (0,0) =
                               **m_sourceBounds.topLeft** 指向的画布位置）。 */
    XRect   m_sourceRect; /**< 本次处理的源区域（控件局部坐标；已按效果
                               外扩量与脏区裁剪）。 */
    XImage* m_dest;       /**< 效果输出画布（尺寸=m_destRect，从全透明
                               开始；Draw 实现负责填充全部内容）。 */
    XRect   m_destRect;   /**< m_dest 覆盖的控件局部矩形 = boundingRectFor
                               (m_sourceRect) 取整。 */
    /* ---- 新增字段一律追加在此注释之后（AGENTS.md 位置初始化器铁律）----
     * 以下字段供 sourcePixmap_2(Device) 使用。**旧调用点（W4 之前的
     * 合成上下文）不填它们也能照常工作**：既有 sourcePixmap 不读新字段，
     * 新字段只由 sourcePixmap_2 / drawToContext 路径消费。新代码请一律
     * 经 XGraphicsEffectDrawContext_init 起手，避免栈垃圾（MSVC /RTC1）。 */
    XRectF  m_sourceBounds; /**< 源内容在源画布坐标系里的包围盒（可带
                               非零原点；0 面积视为"未设置"⇒回落
                               sourceBoundingRect）。**补边与内容原点共用
                               这一个 rect**：sourcePixmap_2(Device) 把
                               它映射到设备空间后求 boundingRectFor 补边
                               （pad 不随视图缩放），源内容也以
                               `topLeft` 为原点绘入结果位图（m_renderSource
                               按画布坐标绘制；无回调时 m_source 的像素
                               (0,0) 即 topLeft）。任何一侧单独再叠「项本地/
                               场景平移」都会让内容相对补边错位（复核
                               gdb 实测的 (10,20) 偏移即此）。 */
    XImageTransform m_deviceTransform; /**< 源画布 → 设备**像素**的变换
                               （含 dpr；Qt QGraphicsItemEffectSourcePrivate
                               里 info->painter->worldTransform() 的 XGui
                               对应物，见 sourcePixmap_2 的 @note）。Qt 的
                               worldTransform() **不含** dpr（dpr 由
                               painter->device() 单独承载，qgraphicsitem.cpp:
                               11109-11111）；XGui 把 dpr 折在末端，故补边前
                               先用 m_devicePixelRatio ÷dpr 回设备逻辑空间。 */
    bool m_hasDeviceTransform; /**< m_deviceTransform 是否有效。 */
    XGraphicsEffectRenderSourceProc m_renderSource; /**< 源重绘回调；可为
                               NULL（此时源快照 m_source 即源内容）。 */
    void* m_renderSourceData;  /**< 源重绘回调私有数据（借用）。 */
    uint32_t m_renderHints;    /**< 外层绘制器的渲染提示位（XPainterRenderHints
                               位集合；0 = 不干预）。源重绘必须继承它——对标
                               Qt `pixmapPainter.setRenderHints(info ?
                               info->painter->renderHints() :
                               QPainter::TextAntialiasing)`（qgraphicsitem.cpp:
                               11114）：否则视图开 Antialiasing/
                               SmoothPixmapTransform 时，挂效果项的源内容与
                               无效果路径渲染不一致（锯齿/像素化）。 */
    float m_devicePixelRatio;  /**< 绘制设备的像素比（对标 Qt
                               QGraphicsItemEffectSourcePrivate::pixmap 里的
                               `info->painter->device()->devicePixelRatio()`，
                               qgraphicsitem.cpp:11109-11110；XGui 取目标图像
                               的 XImage_devicePixelRatio）。**只影响 Device 取
                               源**：Qt 的补边在**设备逻辑空间**求
                               （paddedEffectRect 收到的 sourceRect 已是
                               worldTransform.mapRect 的结果，
                               qgraphicsitem.cpp:11015-11018 / 11054-11055），
                               位图物理尺寸再 ×dpr
                               （qgraphicsitem.cpp:11110-11111）。XGui 的
                               m_deviceTransform 把 dpr 折在末端（XPainter 的
                               设备变换 = world × view × dpr）⇒ sourcePixmap_2
                               先用本值把设备矩形 ÷dpr 回"设备逻辑空间"补边、
                               再 ×dpr 回物理像素：**视图缩放不放大 pad**，
                               dpr 仍按 Qt 口径放大物理位图。
                               0 / 非有限值视作 1.0（旧上下文零回归）；
                               XGraphicsEffectDrawContext_init 置 1.0f。 */
} XGraphicsEffectDrawContext;

/**
 * @brief      初始化效果绘制上下文（全字段清零；XGui 适配接口）。
 * @details    自定义效果/测试自造上下文时先调本函数，再把四个基础字段
 *             （m_source/m_sourceRect/m_dest/m_destRect）填好即可——与
 *             W4 之前"逐个字段赋值"的写法等价，但不留栈垃圾（MSVC
 *             /RTC1 未初始化读会冻住进程，AGENTS.md 实锅）。
 * @param      ctx 目标上下文；可为 NULL（空操作）。
 */
void XGraphicsEffectDrawContext_init(XGraphicsEffectDrawContext* ctx);

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

/**
 * @brief      查询效果是否启用（对标 QGraphicsEffect::isEnabled）。
 * @param      self 目标效果；可为 NULL。
 * @return     启用返回 true；无效返回 false。
 */
bool XGraphicsEffect_isEnabled(const XGraphicsEffect* self);
/**
 * @brief      设置启用状态（对标 QGraphicsEffect::setEnabled）。
 * @details    状态实际变化时**先**请求源重绘、**再**发射 enabledChanged(bool)
 *             （顺序对齐 Qt：`if (d->source) d->source->update(); emit
 *             enabledChanged(enable);`）。禁用后效果分支跳过、源按常规
 *             路径绘制，外扩环由 update() 标脏覆写。
 *             同值调用不发射、不重绘（Qt 同值早退）。
 * @param      self 目标效果。
 * @param      enable true=启用，false=禁用。
 * @return     无返回值。
 */
void XGraphicsEffect_setEnabled(XGraphicsEffect* self, bool enable);
/**
 * @brief      请求效果重绘（对标 QGraphicsEffect::update）。
 * @details    有源时标脏「**源控件矩形**（父坐标）∪ 效果包围盒
 *             （boundingRectFor 的外扩结果）∪ 上一轮效果包围盒」；无源为
 *             无操作（对标 Qt `if (d->source) d->source->update()`——Qt 的
 *             invalidateCache 最终让**整个源控件**失效）。
 *             源控件矩形必须恒进脏区：boundingRectFor 可**收缩**到小于源
 *             矩形（负模糊半径/负偏移），四周边环不在效果包围盒内，只标
 *             包围盒会留上一帧边缘像素。【P0-59 2026-11-11】包围盒为空/
 *             面积为负时不再直接 return——照旧标源控件矩形并作废旧包围盒
 *             缓存（否则 setBlurRadius(5→-20) 或 setEnabled(false) 之后上一
 *             帧画面永远留在屏上）。与上一轮标脏的包围盒取**并集**后一并
 *             标记——效果参数收缩时旧外扩环同样被覆写，不留残影。
 * @param      self 目标效果。
 * @return     无返回值。
 */
void XGraphicsEffect_update(XGraphicsEffect* self);
/**
 * @brief      通知效果包围盒已变化（对标 QGraphicsEffect::updateBoundingRect；
 *             Qt 中为 protected，供派生效果的属性 setter 调用）。
 * @details    Qt 语义：包围盒变化（尤其**收缩**）时新旧两个矩形都要失效。
 *             XGui 不单独缓存"旧包围盒"，改由 XGraphicsEffect_update 的
 *             「源控件矩形 ∪ 本次包围盒 ∪ 上次包围盒」实现同一效果，故本
 *             函数等价于 update()。保留独立名字是为让派生效果的 setter 与
 *             Qt 源码逐行同构（Qt 的 setBlurRadius/setOffset 调的就是它）。
 * @param      self 目标效果。
 * @return     无返回值。
 */
void XGraphicsEffect_updateBoundingRect(XGraphicsEffect* self);

/**
 * @brief      浮点近等值判定（对标 Qt 6.8 qFuzzyCompare(float, float)）。
 * @details    Qt 原文（corelib/global/qnumeric.h:393-396）：
 *             `return (qAbs(p1 - p2) * 100000.f <= min(qAbs(p1), qAbs(p2)));`
 *             置零保护同 Qt：任一方为 0（或异号近零）时相对容差无意义，
 *             只认精确相等。
 *             【P0-60/61/62 2026-11-11】三个内置效果的属性 setter 一律用它
 *             做"值未变"早退（Qt qgraphicseffect.cpp:763 / :954 / :1091 用的
 *             都是 qFuzzyCompare），避免 5.0 与 5.000001f 这类近等值多发一次
 *             changed 信号、白跑一次包围盒重算与重绘。
 * @param      a 第一个浮点值。
 * @param      b 第二个浮点值。
 * @return     true 表示按 Qt 口径视为相等。
 */
bool XGraphicsEffect_fuzzyCompare(float a, float b);

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
 *             XWidget_setGraphicsEffect 挂接时设置。**图形项源不走本入口**
 *             （返回 NULL）——图形项源经 XGraphicsEffect_itemSource 查询
 *             （W4；两条源互斥，见 XGraphicsEffect_setItemSource）。
 *
 * @param      self 目标效果；可为 NULL。
 * @return     效果源控件借用指针；无控件源（含图形项源）时为 NULL。
 */
XWidget* XGraphicsEffect_source(const XGraphicsEffect* self);

/**
 * @brief      设置效果源（XGui 适配接口，非 Qt 公共 API）。
 *
 * @details    仅供 XWidget_setGraphicsEffect 挂接/摘除效果时调用，用于
 *             维护 source() 的返回值；不转移所有权。
 *             摘除（source 传 NULL）只断开关联，不触碰控件侧槽位——控件
 *             侧由 XWidget_setGraphicsEffect 自己复位。
 *             【W4】挂新控件（source 非 NULL）而本效果原挂着**图形项**源时：
 *             先发 sourceChanged(SourceDetached)、再断图形项侧关联（对标 Qt
 *             setGraphicsEffectSource，qgraphicseffect_p.h:121-134；
 *             一个效果只有一个源）。
 *
 * @param      self 目标效果。
 * @param      source 效果源控件借用指针；可为 NULL 清除。
 * @return     无返回值。
 */
void XGraphicsEffect_setSource(XGraphicsEffect* self, XWidget* source);

#if XPAINTER_PATH_ON
/**
 * @brief      获取图形项源（XGui 适配接口；对标 QGraphicsItem 侧
 *             `graphicsEffect()` 的反向关联）。
 * @param      self 目标效果；可为 NULL。
 * @return     图形项借用指针（禁止释放）；无图形项源返回 NULL。
 */
XGraphicsItem* XGraphicsEffect_itemSource(const XGraphicsEffect* self);

/**
 * @brief      设置图形项源（XGui 适配接口，非 Qt 公共 API）。
 *
 * @details    仅供 XGraphicsItem_setGraphicsEffect /
 *             XGraphicsItem_graphicsEffectDetach 维护 `m_itemSource` 关联：
 *             - 设新项（item 非 NULL）：若原挂着**控件**源，先经
 *               XWidget_graphicsEffectDetach 把控件侧槽位摘空（Qt 实测：
 *               一个效果同时只有一个源，换宿主时旧宿主 graphicsEffect()
 *               变 NULL）；若原挂着别的图形项，先摘旧项的槽位。不转移
 *               所有权——效果的所有权始终归宿主（Qt 语义）。
 *             - 换宿主（item 非 NULL 且原挂着别的源）时**先发**
 *               sourceChanged(SourceDetached)，再摘旧宿主、挂新关联（对标
 *               Qt QGraphicsEffectPrivate::setGraphicsEffectSource，
 *               qgraphicseffect_p.h:121-134；通知时旧源仍可见，与仓库控件
 *               路径次序一致，见 Src/XGui/Widget/XWidget.c:14771-14820）。
 *               自定义效果据此清缓存/脱离源。
 *             - 传 NULL：**只断本侧关联**，不触碰图形项侧槽位（图形项侧由
 *               调用方自己复位，与 XGraphicsEffect_setSource 同款分工；
 *               也不发 SourceDetached——删除路径的静默口径见
 *               XWidget.c 的 P0-14 记录）。
 * @param      self 目标效果；可为 NULL（空操作）。
 * @param      item 图形项借用指针；可为 NULL 清除。
 * @return     无返回值。
 */
void XGraphicsEffect_setItemSource(XGraphicsEffect* self, XGraphicsItem* item);
#endif /* XPAINTER_PATH_ON */

/**
 * @brief      源变更通知（对标 QGraphicsEffect::sourceChanged，经虚表
 *             分派到 SourceChanged 槽位）。
 *
 * @details    基类默认实现转 update() 请求重绘；派生效果可覆盖。
 *             XWidget_setGraphicsEffect 挂接效果时以 SourceAttached
 *             触发，保证效果安装即重绘。
 *
 * @param      self 目标效果；可为 NULL。
 * @param      flags XGraphicsEffectChangeFlag 位或组合。
 * @return     无返回值。
 * @note       用户 2026-11-14 定案：虚函数入口只留 `_base`，函数体直接
 *             按虚表槽分派；不提供无后缀同名公开入口，也不通过另一函数
 *             二级转发。派生类业务实现仍是 static VX* 直挂虚表。
 */
void XGraphicsEffect_sourceChanged_base(XGraphicsEffect* self, int flags);

/**
 * @brief      按源矩形计算效果包围盒（对标 QGraphicsEffect::boundingRectFor，
 *             经虚表分派到 BoundingRectFor 槽位）。
 *
 * @details    基类默认实现原样返回源矩形（与 Qt 的 QGraphicsEffect 基类
 *             一致），内置派生效果覆盖为"源矩形按效果外扩量生长"。
 *             入参/返回值均为**源本地坐标**（与 Qt 相同）。
 *
 * @param      self 目标效果；可为 NULL。
 * @param      sourceRect 源矩形；为 NULL 时返回空矩形。
 * @return     效果作用后的包围盒。
 * @note       虚函数入口只留 `_base`（同 sourceChanged_base 定案口径）；
 *             槽位为空时回落基类默认实现 VXGraphicsEffect_boundingRectFor。
 */
XRectF XGraphicsEffect_boundingRectFor_base(const XGraphicsEffect* self,
                                            const XRectF* sourceRect);

/**
 * @brief      获取源包围盒（对标 QGraphicsEffect::sourceBoundingRect 的
 *             logical 坐标口径）。
 *
 * @details    Qt 返回效果源在**逻辑坐标系**下的包围盒；控件作源时即
 *             (0, 0, width, height)——原点恒为 (0,0)，不是控件在父级中的
 *             位置（本机 Qt 6.8 实测：child.setGeometry(30,40,80,20) 的
 *             效果 boundingRect() = (0,0,80,20)）。图形项源（W4）为
 *             item->boundingRect() ∪ childrenBoundingRect()（项本地坐标，
 *             对标 qgraphicsitem.cpp:11002-11021 的 boundingRect 实现，
 *             其中 11013-11015 为 `rect |= item->childrenBoundingRect()`）。
 *             无源返回空矩形。
 *             Device 坐标口径见 XGraphicsEffect_sourceBoundingRect_2
 *             （Qt 的同名函数带 `Qt::CoordinateSystem system =
 *             Qt::LogicalCoordinates` 形参，两坐标系都要有对应入口）。
 *
 * @param      self 目标效果；可为 NULL。
 * @return     源本地坐标包围盒；无源为空矩形。
 */
XRectF XGraphicsEffect_sourceBoundingRect(const XGraphicsEffect* self);

/**
 * @brief      按坐标系取源包围盒（对标 Qt 6.8 `QGraphicsEffect::
 *             sourceBoundingRect(Qt::CoordinateSystem)` 的双坐标系形态）。
 *
 * @details    `coordinateSystem`：
 *               - `XGraphicsEffectCoordinateSystem_Logical`：等价
 *                 XGraphicsEffect_sourceBoundingRect（源本地逻辑坐标）；
 *               - `XGraphicsEffectCoordinateSystem_Device`：把源包围盒经
 *                 deviceTransform 映射到**设备像素**（与 sourcePixmap_2 的
 *                 Device 口径同源：dpr 已折进 deviceTransform ⇒ 返回的是
 *                 物理像素矩形；Qt 的 Device 口返回"逻辑设备坐标"，
 *                 两者按 physical = logical × dpr 一一对应，本机 Qt 6.8.0
 *                 探针实测见 XGraphicsEffectDeviceAlignTest 文件头）。
 *                 **必须在 Draw 期间调用**（设备变换来自绘制上下文；
 *                 Qt 同：无 info 上下文时告警并返回空矩形，
 *                 qgraphicsitem.cpp:10998-11007）。
 * @param      self 目标效果；可为 NULL（返回空矩形）。
 * @param      deviceTransform 源画布 → 设备像素的矩阵（含 dpr）；为 NULL 时
 *             用上下文里的 m_deviceTransform（无效则按单位阵）。
 * @param      coordinateSystem XGraphicsEffectCoordinateSystem 值
 *             （非 Device 一律按 Logical）。
 * @return     对应坐标系的源包围盒；无源/Draw 之外取 Device 返回空矩形。
 */
XRectF XGraphicsEffect_sourceBoundingRect_2(
    const XGraphicsEffect* self, const XImageTransform* deviceTransform,
    int coordinateSystem);

/**
 * @brief      源是否"本质上就是像素图"（对标 QGraphicsEffect::sourceIsPixmap；
 *             Qt 中为 protected）。
 *
 * @details    Qt 仅在源是 QGraphicsPixmapItem 一类像素图项时返回 true；
 *             控件源（QWidgetEffectSourcePrivate::isPixmap）恒为 false
 *             （本机 Qt 6.8 探针实测：挂在 QWidget 上的效果 draw() 内
 *             sourceIsPixmap() = false）。W4 起 XGui 有了图形项世界，但
 *             **没有 QGraphicsPixmapItem 的对应物**（XGraphicsItem 子集不含
 *             像素图项类型）⇒ 图形项源同样恒 false（Qt 的
 *             `QGraphicsPixmapItem` 分支在 XGui 无对应实现）。故本函数恒
 *             返回 false；保留它是为了让自定义效果的
 *             `sourceIsPixmap() ? Logical : Device` 优化分支写法可原样移植。
 *
 * @param      self 目标效果；可为 NULL。
 * @return     恒 false（控件/图形项源均非像素图；无源也是 false）。
 */
bool XGraphicsEffect_sourceIsPixmap(const XGraphicsEffect* self);

/**
 * @brief      获取源图像（对标 QGraphicsEffect::sourcePixmap；Qt 中为
 *             protected）。
 *
 * @details    Qt 把源（控件/图形项）渲染进 QPixmap 并按 mode 补透明边；
 *             XGui 的对应物是 ARGB32_Premultiplied 的 XImage：
 *               - 源矩形 = sourceBoundingRect()（控件局部 (0,0,w,h)）；
 *               - NoPad 取源矩形；PadToTransparentBorder 四周各 +1；
 *                 PadToEffectiveBoundingRect 取 boundingRectFor(源矩形)
 *                 的整数对齐矩形（floor 原点 / ceil 右下角）；
 *               - `offset` 回传该图像左上角在源本地坐标中的位置
 *                 （PadToEffectiveBoundingRect 时为负，如 (-4,-4)）。
 *             本机 Qt 6.8 实测（30x20 子控件、基类效果）：
 *                 NoPad                       → 30x20, offset (0,0)
 *                 PadToTransparentBorder      → 32x22, offset (-1,-1)
 *                 PadToEffectiveBoundingRect  → 30x20, offset (0,0)
 *             绘制期（Draw 槽位内）直接复用管线已有的源快照；Draw 之外返回
 *             NULL——Qt 6.8 的 QWidgetEffectSource 实现在 draw() 之外会解引用
 *             空绘制上下文而**崩溃**（本机探针实测段错误），XGui 以 NULL
 *             优雅返回，且不另造离屏源（那会把已施效画面当源、递归施效）。
 *
 * @param      self 目标效果；可为 NULL。
 * @param      offset 可选输出：图像左上角在源本地坐标中的偏移；可为 NULL。
 * @param      mode 补边方式（XGraphicsEffectPixmapPadMode）。
 * @return     新分配的 XImage（调用方负责 XClassDelete）；无源、非 Draw
 *             期间、源尺寸为 0 或分配失败返回 NULL。
 */
XImage* XGraphicsEffect_sourcePixmap(const XGraphicsEffect* self,
                                     XPoint* offset,
                                     XGraphicsEffectPixmapPadMode mode);

/**
 * @brief      按坐标系取源图像（对标 Qt 6.8 `QGraphicsEffect::sourcePixmap(
 *             Qt::CoordinateSystem, QPoint*, PixmapPadMode)` 的**双坐标系**
 *             形态；XGui 以显式 deviceTransform 形参替代 Qt 的
 *             QGraphicsItemEffectSourcePrivate::info 上下文）。
 *
 * @details    `coordinateSystem`：
 *               - `XGraphicsEffectCoordinateSystem_Logical`：等价既有
 *                 `XGraphicsEffect_sourcePixmap(self, offset,
 *                 PadToEffectiveBoundingRect)`（Qt 缺省 pad mode 就是
 *                 PadToEffectiveBoundingRect，qgraphicseffect.h:70-71），
 *                 源本地逻辑坐标、1 逻辑单位 = 1 像素。
 *               - `XGraphicsEffectCoordinateSystem_Device`：把源画布包围盒
 *                 （ctx->m_sourceBounds，缺省回落 sourceBoundingRect）**先经
 *                 deviceTransform 映射到设备矩形**，再按 pad mode
 *                 （PadToEffectiveBoundingRect，Qt 缺省）在**设备空间**补边
 *                 （求 boundingRectFor），取整为 effectRect——pad 的坐标系
 *                 口径见下：Qt 的 paddedEffectRect(Device, …) 收到的
 *                 sourceRect 已经是 `worldTransform().mapRect(rect)`
 *                 （qgraphicsitem.cpp:11015-11018），boundingRectFor 作用在
 *                 **设备坐标**上，故补边量是设备单位、**不随视图缩放放大**
 *                 （qgraphicsitem.cpp:11054-11055）。XGui 的 deviceTransform
 *                 折了 dpr ⇒ 先用 ctx->m_devicePixelRatio ÷dpr 回设备逻辑
 *                 空间补边、再 ×dpr 回物理像素（与 Qt 的
 *                 `QRectF(effectRectF.topLeft(), effectRectF.size() * dpr)`
 *                 qgraphicsitem.cpp:11110-11111 同口径：dpr 放大物理位图、
 *                 视图缩放不放大 pad）：
 *                   * 返回图像尺寸 = effectRect 的**设备像素**尺寸；
 *                   * `*offset` = effectRect.topLeft()（设备像素）；
 *                   * 图像内容 = 源经 `ctx->m_renderSource` 重画到
 *                     `T(-effectRect.topLeft) ∘ deviceTransform` 下的结果
 *                     （无回调时直接按该变换绘 ctx->m_source 快照）。
 *                     **内容原点恒为 `ctx->m_sourceBounds.topLeft`**——与
 *                     补边用的是同一个 rect：回调按画布坐标绘制（其画布
 *                     原点即该 topLeft），无回调时 m_source 的像素 (0,0)
 *                     对齐到该 topLeft。offset 即"图像左上角"，故源内容
 *                     左上在图像内的落位 = `deviceTransform(sourceBounds.
 *                     topLeft) − offset`（= pad × dpr 设备像素），与 Qt
 *                     qgraphicsitem.cpp:11110-11118 的
 *                     `effectRect`/`QTransform::fromTranslate(-effectRect)`
 *                     流程同构。
 *
 * @note       **与 Qt 的唯一口径差异（已按 Qt 原文取证，见 openNotes）**：
 *             Qt 的 Device 图像是"逻辑尺寸 × dpr 的物理位图 + dpr 元数据"，
 *             于是 offset 是**逻辑设备坐标**；XGui 没有 QPixmap 的 dpr
 *             元数据消费链（XImage 有 dpr，但派生效果滤镜按 1:1 逐像素
 *             处理位图），故本函数把 dpr **折进尺寸与 offset**：图像是
 *             dpr=1 的物理像素位图，offset 是物理像素。两者经
 *             `physical = logical × dpr` 一一对应（Qt 的
 *             `effectRectF.size() * dpr` 就是这个物理尺寸，
 *             qgraphicsitem.cpp:11110-11111）。消费方要把它画回绘制器时，
 *             用 `W' = invert(painterDeviceTransform) ∘ painterWorldTransform`
 *             把用户坐标切成像素空间（对标 Qt 的
 *             `painter->setWorldTransform(QTransform())` 画 dpr 位图）。
 * @param      self 目标效果；可为 NULL（返回 NULL）。
 * @param      deviceTransform 源画布 → 设备像素的矩阵（含 dpr；管线内取
 *             `XPainter_deviceTransform`）。为 NULL 时用
 *             `ctx->m_deviceTransform`（`m_hasDeviceTransform` 为假则按
 *             单位阵）。
 * @param      offset 可选输出：图像左上角（Logical=源本地坐标；
 *             Device=设备像素）；可为 NULL。
 * @param      coordinateSystem XGraphicsEffectCoordinateSystem 值
 *             （非 Device 一律按 Logical）。
 * @return     新分配的 XImage（ARGB32_Premultiplied，调用方 XClassDelete）；
 *             无源 / 非 Draw 期间 / 源尺寸为 0 / 分配失败返回 NULL。
 */
XImage* XGraphicsEffect_sourcePixmap_2(const XGraphicsEffect* self,
                                       const XImageTransform* deviceTransform,
                                       XPoint* offset,
                                       int coordinateSystem);

/**
 * @brief      获取效果包围盒（对标 QGraphicsEffect::boundingRect）。
 *
 * @details    有源时返回 boundingRectFor(源包围盒)，坐标系为**源本地坐标**
 *             （原点 (0,0)）；无源时返回空矩形，与 Qt 的
 *             `if (d->source) ... return QRectF();` 一致。
 *             注意：XGui 的脏区标记需要父坐标系，由 XGraphicsEffect_update
 *             内部把本结果平移到父级（不改变本函数的 Qt 口径）。
 *
 * @param      self 目标效果；可为 NULL。
 * @return     效果包围盒（浮点矩形，源本地坐标）。
 */
XRectF XGraphicsEffect_boundingRect(const XGraphicsEffect* self);

/**
 * @brief      把源快照绘入当前效果的输出画布（对标 QGraphicsEffect::
 *             drawSource(QPainter*)；Qt 中为 protected）。
 *
 * @details    仅在自身的 Draw 槽位实现内、且正被
 *             XGraphicsEffect_drawWidget 派发时有效：此时效果持有本次绘制
 *             上下文（源快照 + 输出画布 + 画布矩形），本函数把源快照按
 *             画布原点对齐绘入 painter 指向的画布（越界自动裁剪）。
 *             派生效果因此不必各自重写"源对齐"样板；画布/快照格式为
 *             ARGB32_Premultiplied（与 Qt「效果画布仅支持 ARGB32 类」同口径）。
 *
 * @param      self 目标效果。
 * @param      painter 目标画布绘制器（须已 begin_image 到 ctx->m_dest）。
 * @return     无返回值；无绘制上下文或参数无效时为空操作。
 */
void XGraphicsEffect_drawSource(XGraphicsEffect* self, XPainter* painter);

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
 *             任一步无法完成（无绘制目标/源无效/画布分配失败）返回
 *             false，调用方回退常规绘制路径。
 *             例外：效果包围盒为空（boundingRectFor 把源收缩到零面积，
 *             典型是 blurRadius 极负）时返回 **true 且不落任何像素**——
 *             对标 Qt 的整块裁剪（本机实测 QGraphicsBlurEffect(-20) 渲染
 *             后控件区域全空），返回 false 会让调用方回退常规绘制。
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
 * @brief      对已备好的离屏画布派发 Draw 槽位（XGui 适配接口；对标 Qt
 *             QGraphicsScenePrivate::drawItemHelper 的
 *             `item->graphicsEffect->draw(painter)` 调用步）。
 *
 * @details    场景渲染（XGraphicsScene_render 的图形项效果分支）先离屏
 *             渲染源、建好输出画布与绘制上下文，再经本函数让派生效果完成
 *             像素处理。与 XGraphicsEffect_drawWidget 的区别：**不做**
 *             绘制目标解析/脏区裁剪/回贴——那些由调用方（场景）按自己的
 *             坐标系完成；本函数只负责：
 *               ① 保存并临时解除设备坐标表面裁剪（`XPainter_surfaceClip*`，
 *                  理由同 drawWidget：效果处理段在效果自有画布上进行，
 *                  外层设备坐标裁剪与之坐标系不同，会整帧裁空）；
 *               ② 挂上 `self->m_drawCtx = ctx`（Draw 期间 sourcePixmap /
 *                  drawSource 依赖它），按虚表分派 Draw 槽位，空槽回落
 *                  基类默认实现；
 *               ③ 摘除 `m_drawCtx` 并恢复表面裁剪。
 * @param      self 目标效果；不可为 NULL。
 * @param      ctx 绘制上下文（调用方填好四个画布字段；不可为 NULL，
 *             m_dest 必须有效）。
 * @return     已派发返回 true；参数无效或画布无效返回 false。
 */
bool XGraphicsEffect_drawToContext(XGraphicsEffect* self,
                                   XGraphicsEffectDrawContext* ctx);

/**
 * @brief      对整幅图像做原地指数模糊（XGui 内部共享助手，移植 Qt
 *             qt_blurImage/expblur）。
 *
 * @details    逐行前向+后向再逐列前向+后向的一维 IIR（Jani Huhtanen 双侧
 *             指数冲激响应），定标与 Qt 完全一致：aprec=12、zprec=10，
 *             `alpha = round(4096 · (1 − (2/255)^(1/radius)))`，radius ≤ 1e-5
 *             时取 4095（近似恒等）。因此：
 *               - 半径决定**核展宽**（半径越大摊得越开），不再是固定 3x3；
 *               - radius ≤ 1 时视觉上与源逐像素相同（Qt QPixmapBlurFilter
 *                 在 radius ≤ 1 时直接绘源，见 qpixmapfilter.cpp:577）；
 *               - quality=true 时按 Qt improvedQuality 走两遍（半径减半）、
 *                 更接近高斯。
 *             与 Qt 的差异：省略 `qt_halfScaled`（radius ≥ 4 时的半分辨率
 *             加速）——XGui 始终全分辨率滤波，结果更精细、代价更高，
 *             属性能差异而非语义差异（见同目录效果文件头注释）。
 *             逐通道独立处理，与 0xAARRGGBB 的通道字节序无关；alphaOnly
 *             时只滤 alpha 通道（Qt 投影用 expblur<...,true>）。
 *             供 XGraphicsBlurEffect 与 XGraphicsDropShadowEffect 复用，
 *             非 Qt 公共 API。
 *
 * @param      image 目标图像（ARGB32_Premultiplied）；不可为 NULL。
 * @param      radius 模糊半径；≤ 0 视为不模糊（近似恒等）。
 * @param      quality true=两遍（半半径）更高质量，对标 QualityHint。
 * @param      alphaOnly true=只模糊 alpha 通道（投影的 Qt 口径）。
 * @return     无返回值。
 */
void XGraphicsEffect_blurImage(XImage* image, float radius, bool quality,
                               bool alphaOnly);

#endif /* XWIDGET_ON */

#endif /* XGUI_ON */

#endif /* XGRAPHICSEFFECT_H */
