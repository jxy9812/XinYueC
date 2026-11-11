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
 *                w1.graphicsEffect() 变 NULL、w2 持有），防双删。
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

#if XWIDGET_ON

/** @brief XWidget 前向声明（效果源承载；完整定义见 XWidget.h）。 */
typedef struct XWidget XWidget;
/** @brief XImage 前向声明（离屏快照/效果画布；完整定义见 XImage.h）。 */
typedef struct XImage XImage;
/** @brief XPainter 前向声明（drawSource 绘制目标；完整定义见 XPainter.h）。 */
typedef struct XPainter XPainter;
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
 * @brief      源变更标志位（对标 QGraphicsEffect::ChangeFlag，位值逐项
 *             一致：qgraphicseffect.h:31-37）。
 */
typedef enum XGraphicsEffectChangeFlag
{
    XGraphicsEffectChange_SourceAttached = 0x1,             /**< 效果已挂接到源。 */
    XGraphicsEffectChange_SourceDetached = 0x2,             /**< 效果已从源摘除。 */
    XGraphicsEffectChange_SourceBoundingRectChanged = 0x4,  /**< 源包围盒变化。 */
    XGraphicsEffectChange_SourceInvalidated = 0x8,          /**< 源外观已失效。 */
    /* 兼容别名（Qt 无对应）：仅剩 Test/XGuiDemo/xgui_demo_apitest_labels.c
       按名断言其存在与取值（该文件不在本批可编辑范围）。框架内部已改用
       规范名 XGraphicsEffectChange_SourceAttached（XWidget.c 挂接点）。 */
    XGraphicsEffectChange_ContentsChanged =
        XGraphicsEffectChange_SourceAttached,               /**< 已废弃别名
        （原 1<<0）：等价 SourceAttached。 */
    XGraphicsEffectChange_GeometryChanged =
        XGraphicsEffectChange_SourceAttached                /**< 已废弃别名
        （原 1<<1）：等价 SourceAttached。 */
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
 * @details    有源时把效果包围盒（boundingRect，含效果外扩）整体标记为
 *             脏区；无源为无操作（对标 Qt `if (d->source) d->source->update()`）。
 *             与上一轮标脏的包围盒取**并集**后一并标记——效果参数收缩
 *             （模糊半径/偏移变小）时旧外扩环同样被覆写，不留残影。
 * @param      self 目标效果。
 * @return     无返回值。
 */
void XGraphicsEffect_update(XGraphicsEffect* self);
/**
 * @brief      通知效果包围盒已变化（对标 QGraphicsEffect::updateBoundingRect；
 *             Qt 中为 protected，供派生效果的属性 setter 调用）。
 * @details    Qt 语义：包围盒变化（尤其**收缩**）时新旧两个矩形都要失效。
 *             XGui 不单独缓存"旧包围盒"，改由 XGraphicsEffect_update 的
 *             「上次标脏矩形 ∪ 本次包围盒」实现同一效果，故本函数等价于
 *             update()。保留独立名字是为让派生效果的 setter 与 Qt 源码
 *             逐行同构（Qt 的 setBlurRadius/setOffset 调的就是它）。
 * @param      self 目标效果。
 * @return     无返回值。
 */
void XGraphicsEffect_updateBoundingRect(XGraphicsEffect* self);

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
 *             摘除（source 传 NULL）只断开关联，不触碰控件侧槽位——控件
 *             侧由 XWidget_setGraphicsEffect 自己复位。
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
 *             XWidget_setGraphicsEffect 挂接效果时以 SourceAttached
 *             触发（兼容别名 ContentsChanged|GeometryChanged 同值），
 *             保证效果安装即重绘。
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
 *             入参/返回值均为**源本地坐标**（与 Qt 相同）。
 *
 * @param      self 目标效果；可为 NULL。
 * @param      sourceRect 源矩形；为 NULL 时返回空矩形。
 * @return     效果作用后的包围盒。
 */
XRectF XGraphicsEffect_boundingRectFor(const XGraphicsEffect* self,
                                       const XRectF* sourceRect);

/**
 * @brief      获取源包围盒（对标 QGraphicsEffect::sourceBoundingRect 的
 *             logical 坐标口径）。
 *
 * @details    Qt 返回效果源在**逻辑坐标系**下的包围盒；控件作源时即
 *             (0, 0, width, height)——原点恒为 (0,0)，不是控件在父级中的
 *             位置（本机 Qt 6.8 实测：child.setGeometry(30,40,80,20) 的
 *             效果 boundingRect() = (0,0,80,20)）。无源返回空矩形。
 *             Qt 的同名函数另有 device 坐标重载；XGui 无设备像素比抽象，
 *             只提供逻辑坐标口径（不设 CoordinateSystem 形参）。
 *
 * @param      self 目标效果；可为 NULL。
 * @return     源本地坐标包围盒；无源为空矩形。
 */
XRectF XGraphicsEffect_sourceBoundingRect(const XGraphicsEffect* self);

/**
 * @brief      源是否"本质上就是像素图"（对标 QGraphicsEffect::sourceIsPixmap；
 *             Qt 中为 protected）。
 *
 * @details    Qt 仅在源是 QGraphicsPixmapItem 一类像素图项时返回 true；
 *             控件源（QWidgetEffectSourcePrivate::isPixmap）恒为 false
 *             （本机 Qt 6.8 探针实测：挂在 QWidget 上的效果 draw() 内
 *             sourceIsPixmap() = false）。XGui 的效果源**恒为控件**
 *             （XGui 没有 QGraphicsItem/QGraphicsPixmapItem 图形项世界），
 *             故本函数恒返回 false；保留它是为了让自定义效果的
 *             `sourceIsPixmap() ? Logical : Device` 优化分支写法可原样移植。
 *
 * @param      self 目标效果；可为 NULL。
 * @return     恒 false（控件源；无源也是 false）。
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
