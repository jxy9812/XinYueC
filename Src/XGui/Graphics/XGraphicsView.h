/******************************************************************************
 * @file       XGraphicsView.h
 * @brief      XGraphicsView 场景视图（对标 Qt 6.8 QGraphicsView）。
 * @details    Graphics View 视图层最小闭环（§B17/W3-c 域）：继承
 *             XAbstractScrollArea 复用其视口与双滚动条；视口变换
 *             （matrix ∘ T(-scroll)）、坐标映射（mapToScene/mapFromScene）、
 *             centerOn、滚轮（先场景后滚动条）、拖动模式（NoDrag/
 *             ScrollHandDrag/RubberBandDrag）、渲染（renderViewport/
 *             render 经 XGraphicsScene_render 以视口变换绘制）与
 *             scene 联动（invalidateScene/updateSceneRect）。
 *             矩阵代数复用 W1 的 XGraphicsItem_matrix* 族（XGraphicsItem.h
 *             公开 API，跨 TU 复用不双份；W2 头注建议的「抬升独立
 *             XTransform 模块」会改 W1/W2 owned 的公开 API 面，超出本轮
 *             所有权，未执行——登记 openNotes）。
 * @note       对齐依据（Qt 6.8.3 原文，函数级出处逐条写在实现注释里）：
 *             - qgraphicsview.cpp:3798 viewportTransform（= matrix *
 *               T(-hbar.value+…) 即 matrix * T(-scroll)；scroll=条值−缩进）；
 *             - :2362 mapToScene（p+=scroll 后 matrix.inverted().map）与
 *               :2454 mapFromScene（matrix.map(p)−scroll 后取整）；
 *             - :344 recalculateContentSize（viewRect=matrix.mapRect(
 *               sceneRect)；放得下时按 alignment 算缩进+条 range(0,0)，
 *               放不下时条 range=[round(left), round(right−width)]、
 *               pageStep=视口宽、singleStep=宽/20）；
 *             - :1878 centerOn（viewPoint=matrix.map(pos)；条值=
 *               round(viewPoint−viewport尺寸/2)；有缩进的轴不动）；
 *             - :3845 setTransform（combine 时 matrix*旧阵；变换后按
 *               transformationAnchor 经 centerView 重新定位）；:1819/:1832/
 *               :1858 rotate/scale/translate（改 d->matrix 后走 setTransform）；
 *             - :478-500 centerView（锚点三分支的**唯一事实源**：
 *               AnchorUnderMouse 在 `q->underMouse()` 为真时
 *               `diff = mapToScene(viewport 中心) − mapToScene(鼠标在视口的
 *               位置)` 后 `centerOn(lastMouseMoveScenePoint + diff)`——
 *               两个 mapToScene 都在**新矩阵**下求值，lastMouseMoveScenePoint
 *               是**旧矩阵**下记录的鼠标处场景点 ⇒ 缩放前后鼠标下的场景点
 *               不动；underMouse 为假 ⇒ 回落 centerOn(lastCenterPoint)；
 *               AnchorViewCenter=centerOn(lastCenterPoint)；NoAnchor=不动）；
 *             - :1363-1372/:1395-1403 setTransformationAnchor/setResizeAnchor
 *               （置 AnchorUnderMouse 时 `d->viewport->setMouseTracking(true)`，
 *               :1370-1371）；:2718-2750 setupViewport（同上，鼠标跟踪块
 *               :2743-2749：锚点为 AnchorUnderMouse 时开视口鼠标跟踪）；
 *             - :3372 wheelEvent（**无任何修饰键分支**：转发场景
 *               GraphicsSceneWheel，场景不收才回落 QAbstractScrollArea
 *               滚条步进——任务书「Ctrl+滚轮缩放」不是 Qt 语义，探针
 *               复证见本轮台账，按 Qt 真语义不设缩放档）；
 *             - :3192/:3279/:3303 mousePress/Move/Release（拖动模式三支：
 *               场景收了 press 即返回；RubberBandDrag 空白处按下起带、
 *               move 期间 setSelectionArea(mapToScene(band))；ScrollHandDrag
 *               左键起手拖条、release 按 motions 判点击清选）；
 *             - :3406 paintEvent（painter.setWorldTransform(viewportTransform)
 *               → drawBackground → 场景 drawItems → drawForeground）；
 *             - :3709-3723 drawBackground（**唯一实现**就是
 *               `painter->fillRect(rect, d->backgroundBrush)`——纯色/渐变/
 *               标准图案/纹理全部由 QPainter 的笔刷机制承担；NoBrush 且
 *               有场景时回落 scene->drawBackground）。本框架的
 *               renderViewport 背景分派见 XGraphicsView.c 的
 *               gview_drawBackground（渐变/图案复用 XPainter 光栅、纹理走
 *               XPainter 平铺原语，无「不支持就告警跳过」分支）；
 *             - :2105 render（painterMatrix = matrix*T(-scroll)*
 *               T(target.tl)*S(xratio,yratio)*T(-source.tl)；clip target；
 *               drawBackground/drawItems）；
 *             - :2702 updateSceneRect（未设显式 sceneRect 才跟随重算）；
 *             - :1656 invalidateScene。
 *             **与任务书的两处分歧（按 Qt 6.8.3 原文裁定，取证在本文件与
 *             实现注释）：**
 *             ① deviceTransform **不含 dpr**。Qt 原文
 *                qgraphicsitem.cpp:4287-4294：`return d_ptr->sceneTransform *
 *                viewportTransform`（ItemIgnoresTransformations 分支外的
 *                唯一公式）；dpr 由**绘制设备**承载——QPainterPrivate::
 *                updateMatrix（qpainter.cpp:626-643）把
 *                hidpiScaleTransform() 乘在 combined 矩阵末端（world×view×
 *                hidpi），viewportTransform/deviceTransform 全程是逻辑
 *                坐标。本框架同链已就位：XPainter_begin_image 从目标图像
 *                元数据载入 dpr（XPainter.c:8312-8318），末端折入有效变换
 *                （XPainter.c:2351-2355，头注释点名 qpainter.cpp:626-643
 *                同款）。故本域交付的「dpr 完整版」= 视图 dpr 访问器
 *                （XWindow_devicePixelRatio 复用）+ 渲染管线对 dpr 目标
 *                自动放大 + deviceTransform/viewportTransform 矩阵与
 *                dpr 无关（断言见 XGraphicsViewAlignTest ③），而不是把
 *                dpr 乘进矩阵——那是 Qt 没有的行为。
 *             ② 滚轮缩放：Qt QGraphicsView 没有内置滚轮缩放（见上
 *                wheelEvent 原文）；「Ctrl+滚轮缩放」是应用层惯例
 *                （examples 里的 View 类自实现），不在基类。本类按 Qt
 *                真语义只做「场景优先、滚动条兜底」。
 * @author     XinYueC 团队
 ******************************************************************************/
#ifndef XGRAPHICSVIEW_H
#define XGRAPHICSVIEW_H

#ifdef __cplusplus
extern "C" {
#endif

#if XGUI_ON

#include <stdint.h>
#include <stdbool.h>
#include "XGuiConfig.h"
#include "XGeometry.h"
#include "XBrush.h"        /* 背景笔刷（复用框架既有值类型）。 */
#include "XImage.h"        /* XImageTransform：矩阵载体（复用框架既有）。 */
#include "XGraphicsItem.h" /* item/scene 前置声明 + 矩阵代数（W1 公开 API，
                              本头文件的 API 面在其中 XPAINTER_PATH_ON 守卫
                              内；与本文件守卫同开关）。 */
#include "XAbstractScrollArea.h"

#if XWIDGET_ON && XFRAME_ON && XSCROLLBAR_ON && XABSTRACTSCROLLAREA_ON && \
    XPAINTER_PATH_ON

/* ==================== 类虚函数表枚举 ==================== */
XCLASS_DEFINE_BEGING(XGraphicsView)
XCLASS_DEFINE_EXTEND_END(XGraphicsView, XAbstractScrollArea)
/* 本子集无新虚槽位：视图不是被派生重写画布的对象（Qt QGraphicsView 的
 * drawBackground/drawForeground 钩子在本子集由 setBackgroundBrush 承载，
 * 定制绘制钩子 W4 随 option 形参再议）；PaintEvent/ResizeEvent/ShowEvent/
 * ScrollContentsBy/ViewportEvent 各覆写在 class_init 内挂。 */

/* ==================== 枚举 ==================== */

/**
 * @brief      拖动模式（对标 QGraphicsView::DragMode，数值一致，
 *             qgraphicsview.h:61-65）。
 */
typedef enum XGraphicsViewDragMode
{
    XGraphicsViewDragMode_NoDrag = 0,         /**< 纯场景交互（缺省）。 */
    XGraphicsViewDragMode_ScrollHandDrag = 1, /**< 左键手型拖拽滚动。 */
    XGraphicsViewDragMode_RubberBandDrag = 2  /**< 空白处按下拉橡皮筋选区。 */
} XGraphicsViewDragMode;

/**
 * @brief      变换/缩放的锚点（对标 QGraphicsView::ViewportAnchor，
 *             数值一致，qgraphicsview.h:48-52）。
 */
typedef enum XGraphicsViewViewportAnchor
{
    XGraphicsViewViewportAnchor_NoAnchor = 0,          /**< 锚点不动。 */
    XGraphicsViewViewportAnchor_AnchorViewCenter = 1,  /**< 视口中心（缺省）。 */
    XGraphicsViewViewportAnchor_AnchorUnderMouse = 2   /**< 鼠标下点（缩放/
        重设变换时保持**鼠标位置下的场景点**不动——对标 Qt 6.8.3
        qgraphicsview.cpp:482-491 的 centerView 分支：以最近鼠标事件的
        （视口位置, 场景点）成对记录换算 `centerOn(lastMouseMoveScenePoint
        + diff)`；鼠标不在视图上（无 ENTER/已 LEAVE）时回落
        AnchorViewCenter。选它会把视口鼠标跟踪打开（Qt :1370-1371）。 */
} XGraphicsViewViewportAnchor;

/* ==================== 类与实例 ==================== */

/**
 * @brief      XGraphicsView 场景视图控件；m_base 必须是第一个成员。
 * @details    场景**借用**回指（视图不拥有场景——Qt 原文 "The view does
 *             not take ownership of scene"，qgraphicsview.cpp:1706 文档）。
 *             @warning 结构体新增字段一律**尾部追加**（AGENTS.md 位置
 *             初始化器铁律；本结构 init 全字段显式清零）。
 */
typedef struct XGraphicsView
{
    XAbstractScrollArea m_base;   /**< 基类成员；必须是第一个。 */
    XGraphicsScene* m_scene;      /**< 场景（借用指针；对标 d->scene）。 */
    XImageTransform m_matrix;     /**< 视图变换（场景→视口前段；对标
                                       d->matrix，恒有值、缺省单位阵）。 */
    XPointF m_lastCenterPoint;    /**< 最近中心点（场景坐标；setTransform 后
                                       按 anchor centerOn 它——对标
                                       d->lastCenterPoint）。 */
    XRectF m_sceneRect;           /**< 显式场景矩形（m_hasSceneRect=false 时
                                       忽略、回落 scene->sceneRect——对标
                                       d->sceneRect/hasSceneRect）。 */
    XBrush m_backgroundBrush;     /**< 背景笔刷（缺省 NoBrush=未设，回落不画
                                       ——对标 d->backgroundBrush；Qt 的
                                       drawBackground 在未设时回落场景笔刷，
                                       本框架场景层无笔刷（W2），未设时
                                       行为=不铺场景底色）。 */
    XPoint m_lastMouseViewport;   /**< 最近鼠标位置（视口坐标；手型拖拽的
                                       帧间差分基准——对标
                                       d->lastMouseEvent.position 单项化；
                                       AnchorUnderMouse 的「当前鼠标位置」
                                       也取它（Qt 取 QCursor::pos()，本框架
                                       无可靠实时光标查询 ⇒ 用最近鼠标事件
                                       的位置，与 m_lastMouseScenePoint 成对）。 */
    int m_scrollX;                /**< 缓存水平滚动量（=−左缩进+横条值；对标
                                       d->scrollX，dirtyScroll 时重读）。 */
    int m_scrollY;                /**< 缓存垂直滚动量（=条值−上缩进；对标
                                       d->scrollY）。 */
    int m_leftIndent;             /**< 水平缩进（场景放得下时按对齐居中；
                                       对标 d->leftIndent）。 */
    int m_topIndent;              /**< 垂直缩进（对标 d->topIndent）。 */
    XRect m_rubberBandRect;       /**< 橡皮筋矩形（视口坐标；空=无带——对标
                                       d->rubberBandRect）。 */
    int m_alignment;              /**< 对齐位（XAlignment 水平/垂直各一；
                                       缺省 HCenter|VCenter=0x84——对标
                                       alignment 属性缺省居中）。 */
    uint8_t m_dragMode;           /**< 拖动模式（XGraphicsViewDragMode）。 */
    uint8_t m_transformationAnchor; /**< 变换锚点（XGraphicsViewViewportAnchor；
                                       缺省 AnchorViewCenter）。 */
    uint8_t m_resizeAnchor;       /**< resize 锚点（同上枚举；缺省
                                       AnchorViewCenter，本轮与变换锚点同
                                       分派路径）。 */
    bool m_identityMatrix;        /**< m_matrix 恒等快门（mapToScene 快径——
                                       对标 d->identityMatrix）。 */
    bool m_hasSceneRect;          /**< 显式场景矩形已设置（对标
                                       d->hasSceneRect）。 */
    bool m_interactive;           /**< 场景交互允许（缺省 true——对标
                                       d->sceneInteractionAllowed）。 */
    bool m_handScrolling;         /**< 手型拖拽进行中（对标 d->handScrolling）。 */
    bool m_rubberBanding;         /**< 橡皮筋拖拽进行中（对标
                                       d->rubberBanding）。 */
    bool m_dirtyScroll;           /**< 滚动缓存待重读（对标 d->dirtyScroll）。 */
    int m_handScrollMotions;      /**< 手型拖拽累计移动次数（release 判「点击
                                       清选」——对标 d->handScrollMotions）。 */
    XPoint m_rubberBandStart;     /**< 橡皮筋起点（视口坐标按下点；对标
                                       d->mousePressViewPoint 的带侧用途）。 */
    bool m_lastEventAccepted;     /**< 最近一次场景鼠标事件的接受态（对标
                                       d->lastMouseEvent.isAccepted() 的单
                                       位承载；手拖 release 的「点击清选」
                                       守卫消费，Qt :3318-3323）。 */
    bool m_keepLastCenterPoint;   /**< 一次性「保留中心点」闸（对标
                                       d->keepLastCenterPoint：setScene 置位、
                                       resizeEvent 消费后清零，Qt :1749/
                                       :3598-3605）。 */
    /* ---- 新增字段一律追加在此注释之后 ---- */
    XPointF m_lastMouseScenePoint; /**< 最近鼠标事件处的场景点（对标
                                       d->lastMouseMoveScenePoint，
                                       qgraphicsview_p.h:78）。与
                                       m_lastMouseViewport **成对**更新
                                       （ENTER/press/move/release 同拍写入，
                                       见 XGraphicsView.c 的
                                       gview_recordMousePos）——AnchorUnderMouse
                                       的 centerView 以它作「变换发生前鼠标下
                                       的场景点」（Qt :483-488）。 */
    bool m_mouseInside;           /**< 视口鼠标在内跟踪位（视口 ENTER 置位、
                                       LEAVE 清零，见 VX_gview_viewportEvent）。
                                       与 XWidget_underMouse 取并集构成 Qt
                                       centerView 的 `q->underMouse()` 口径
                                       （qgraphicsview.cpp:483）：控件悬停位
                                       只由窗口级 ENTER/LEAVE 冒泡维护，无窗口
                                       的离屏/嵌入式用法（与测试）拿不到，
                                       故补视口事件跟踪位。 */
} XGraphicsView;

/* ==================== 生命周期 ==================== */

/**
 * @brief      XGraphicsView 类虚函数表初始化。
 * @return     共享 XVtable 指针。
 */
XVtable* XGraphicsView_class_init(void);

/**
 * @brief      初始化 XGraphicsView（对标 QGraphicsView 构造：无场景、
 *             单位变换、拖动模式 NoDrag、交互允许、对齐居中、
 *             锚点 AnchorViewCenter、背景笔刷 NoBrush）。
 * @param      self 目标对象指针；不可为 NULL。
 * @param      parent 父控件；可为 NULL。
 * @param      flags 控件标志位。
 */
void XGraphicsView_init(XGraphicsView* self, XWidget* parent,
                        XWidgetFlags flags);
#define XGraphicsView_create(parent, flags) \
    XGraphicsView_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, (parent), (flags))
/**
 * @brief      使用指定内存类型创建 XGraphicsView。
 * @param      memory 对象内存类型。
 * @param      parent 父控件；可为 NULL。
 * @param      flags 控件标志位。
 * @return     新对象指针；失败返回 NULL。
 */
XGraphicsView* XGraphicsView_create_ex(XMemoryType memory, XWidget* parent,
                                       XWidgetFlags flags);

/* ==================== 场景关联 ==================== */

/**
 * @brief      设置场景（对标 setScene，qgraphicsview.cpp:1714：同场景早退；
 *             换场全视口标脏；重算滚动范围；lastCenterPoint=sceneRect
 *             中心）。
 * @details    视图**不接管**场景所有权（Qt 原文：The view does not take
 *             ownership of scene）；scene 为 NULL = 摘离当前场景（Qt 同，
 *             摘离后 sceneRect 未显式设置时回落空矩形）。
 * @param      self 目标视图；可为 NULL（空操作）。
 * @param      scene 新场景借用指针；可为 NULL。
 */
void XGraphicsView_setScene(XGraphicsView* self, XGraphicsScene* scene);

/**
 * @brief      当前场景（对标 scene；借用指针）。
 * @param      self 目标视图；可为 NULL（返回 NULL）。
 * @return     场景借用指针；未设置返回 NULL。禁止释放。
 */
XGraphicsScene* XGraphicsView_scene(const XGraphicsView* self);

/**
 * @brief      场景交互开关（对标 setInteractive；缺省允许。关闭后鼠标/
 *             滚轮不再派发场景事件，但手型拖拽滚动仍可用——Qt 原文：
 *             "even in non-interactive mode, scroll hand dragging is
 *             allowed"，qgraphicsview.cpp:3196-3198）。
 * @param      self 目标视图；可为 NULL（空操作）。
 * @param      allowed true=允许场景交互。
 */
void XGraphicsView_setInteractive(XGraphicsView* self, bool allowed);

/**
 * @brief      场景交互是否允许（对标 isInteractive）。
 * @param      self 目标视图；可为 NULL（返回 false）。
 */
bool XGraphicsView_isInteractive(const XGraphicsView* self);

/* ==================== 场景矩形与对齐 ==================== */

/**
 * @brief      视图的场景矩形（对标 sceneRect 属性）。
 * @details    显式设置过（setSceneRect 非空参）返回设置值；否则回落
 *             scene->sceneRect()（未设场景返回空矩形）——Qt 原文：
 *             "If unset, or if a null QRectF is set, this property has
 *             the same value as QGraphicsScene::sceneRect"。
 * @param      self 目标视图；可为 NULL（返回空矩形）。
 * @return     场景坐标矩形。
 */
XRectF XGraphicsView_sceneRect(const XGraphicsView* self);

/**
 * @brief      设置显式场景矩形（对标 setSceneRect；空矩形=清除显式设置，
 *             回落场景口径；变更后重算滚动范围）。
 * @param      self 目标视图；可为 NULL（空操作）。
 * @param      rect 新矩形；为 NULL 按 (0,0,0,0)=未设置处理。
 */
void XGraphicsView_setSceneRect(XGraphicsView* self, const XRectF* rect);

/**
 * @brief      场景小于视口时的对齐（对标 alignment 属性；缺省水平/垂直
 *             居中）。水平位取 Left/HCenter/Right、垂直位取 Top/VCenter/
 *             Bottom（未命中缺省支=居中，Qt switch 缺省支同口径）。
 * @param      self 目标视图；可为 NULL（空操作）。
 * @param      alignment XAlignment 位组合。
 */
void XGraphicsView_setAlignment(XGraphicsView* self, int alignment);

/**
 * @brief      对齐位（对标 alignment）。
 * @param      self 目标视图；可为 NULL（返回 0）。
 */
int XGraphicsView_alignment(const XGraphicsView* self);

/**
 * @brief      设置背景笔刷（对标 setBackgroundBrush，qgraphicsview.cpp:2584；
 *             值拷贝入）。设置后全视口标脏。
 * @details    全部 XBrushStyle 都真绘制（对标 Qt drawBackground 的
 *             `fillRect(rect, d->backgroundBrush)`，见文件头注）：纯色=
 *             填充色；Linear/Radial/Conical=XPainter 渐变光栅（几何按暴露
 *             矩形生成，XGradient 基类不带几何）；Dense1..DiagCross=XPainter
 *             标准 8×8 图案；TexturePattern=纹理平铺（tile 网格锚定逻辑
 *             原点）。NoBrush=清除（Qt 的 NoBrush 支回落场景笔刷，本框架
 *             场景层无笔刷 ⇒ 不铺底）。
 * @note       纹理的**借用**契约来自 XBrush 本身：若笔刷用
 *             XBrush_setTextureImage 设纹理，XImage 归调用方所有，须覆盖
 *             本视图的使用期（XBrush_setTexture 传入的 XPixmap 则由画刷
 *             驻留池持有副本）。笔刷自身 transform（XBrush_setTransform）
 *             不参与背景绘制（XBrush_applyToPainter 不消费它——框架口径）。
 * @param      self 目标视图；可为 NULL（空操作）。
 * @param      brush 新笔刷；NULL 按 NoBrush 处理。
 */
void XGraphicsView_setBackgroundBrush(XGraphicsView* self,
                                      const XBrush* brush);

/**
 * @brief      背景笔刷（借用指针，随本视图存亡，禁止释放）。
 * @param      self 目标视图；可为 NULL（返回 NULL）。
 */
const XBrush* XGraphicsView_backgroundBrush_const(const XGraphicsView* self);

/* ==================== 变换 ==================== */

/**
 * @brief      视图变换矩阵（对标 transform；借用指针，setTransform 后
 *             失效）。
 * @param      self 目标视图；可为 NULL（返回 NULL）。
 * @return     内部矩阵借用指针。
 */
const XImageTransform* XGraphicsView_transform_const(const XGraphicsView* self);

/**
 * @brief      视口变换（对标 viewportTransform，qgraphicsview.cpp:3798）：
 *             matrix * T(-scrollX, -scrollY)，把**场景坐标映射到视口
 *             坐标**；逆映射用 matrixInvert 后按 mapToScene 口径先加
 *             scroll（Qt mapToScene 原文同构，不走矩阵逆的平移项）。
 * @param      self 目标视图；可为 NULL（输出单位阵）。
 * @param      out 输出矩阵；不可为 NULL。
 */
void XGraphicsView_viewportTransform(const XGraphicsView* self,
                                     XImageTransform* out);

/**
 * @brief      设置视图变换（对标 setTransform，qgraphicsview.cpp:3845）：
 *             combine=true 时 matrix **先作用**（Qt：d->matrix = matrix *
 *             d->matrix）；同值早退；变更后重算滚动范围并按锚点重居中
 *             （缺省锚点 AnchorViewCenter=对 lastCenterPoint centerOn），
 *             最后全视口标脏。
 * @param      self 目标视图；可为 NULL（空操作）。
 * @param      matrix 新矩阵；NULL 按单位阵处理。
 * @param      combine true=与现矩阵组合，false=替换。
 */
void XGraphicsView_setTransform(XGraphicsView* self,
                                const XImageTransform* matrix, bool combine);

/**
 * @brief      重置为单位变换（对标 resetTransform = setTransform(QTransform)）。
 * @param      self 目标视图；可为 NULL（空操作）。
 */
void XGraphicsView_resetTransform(XGraphicsView* self);

/**
 * @brief      旋转变换（对标 rotate，qgraphicsview.cpp:1819：matrix.rotate
 *             后走 setTransform）。
 * @param      self 目标视图；可为 NULL（空操作）。
 * @param      deg 角度（度；正角在 y 向下屏幕坐标呈顺时针，Qt 同口径）。
 */
void XGraphicsView_rotate(XGraphicsView* self, float deg);

/**
 * @brief      缩放变换（对标 scale，qgraphicsview.cpp:1832）。
 * @param      self 目标视图；可为 NULL（空操作）。
 * @param      sx X 缩放系数。
 * @param      sy Y 缩放系数。
 */
void XGraphicsView_scale(XGraphicsView* self, float sx, float sy);

/**
 * @brief      平移变换（对标 translate，qgraphicsview.cpp:1858；改的是
 *             视图矩阵，不是滚动条——平移导航走 translate，滚动走条值）。
 * @param      self 目标视图；可为 NULL（空操作）。
 * @param      dx X 平移量（场景单位的前置平移）。
 * @param      dy Y 平移量。
 */
void XGraphicsView_translate(XGraphicsView* self, float dx, float dy);

/**
 * @brief      设备像素比（视图所在顶层窗口的 dpr；对标
 *             QGraphicsView::devicePixelRatio——QWidget::devicePixelRatio
 *             的视图侧等价访问）。
 * @details    dpr 单一事实源=框架既有 XWindow_devicePixelRatio
 *             （XWindow.h:661，经 XWidget_windowHandle 取顶层句柄）；
 *             未显示/无句柄返回 1.0（Qt 未挂窗口的 widget 同口径）。
 *             **矩阵语义提示**：本值只用于「渲染目标物理像素」换算
 *             （QPainter hidpi 链同款，qpainter.cpp:626-643），不进入
 *             viewportTransform/deviceTransform（见文件头注 ①）。
 * @param      self 目标视图；可为 NULL（返回 1.0）。
 * @return     设备像素比（非法值兜底 1.0）。
 */
float XGraphicsView_devicePixelRatio(const XGraphicsView* self);

/* ==================== 坐标映射 ==================== */

/**
 * @brief      视口坐标 → 场景坐标（对标 mapToScene(QPoint)，
 *             qgraphicsview.cpp:2362：p += scroll 后 matrix.inverted().map）。
 * @param      self 目标视图；可为 NULL（按**恒等映射**：无视图=无视口
 *             变换/无滚动，输入坐标原样返回——不做任何虚构补偿）。
 * @param      viewportPoint 视口（像素整数）坐标；NULL 按 (0,0)。
 * @return     场景坐标（浮点）。
 */
XPointF XGraphicsView_mapToScene(const XGraphicsView* self,
                                 const XPoint* viewportPoint);

/**
 * @brief      场景坐标 → 视口坐标（对标 mapFromScene(QPointF)，
 *             qgraphicsview.cpp:2454：matrix.map(p) − scroll 后取整——
 *             Qt 返回 QPoint，本框架视口坐标本就是整数像素）。
 * @param      self 目标视图；可为 NULL（按**恒等映射**取整返回——
 *             与 mapToScene 的 NULL 口径对称）。
 * @param      scenePoint 场景坐标；NULL 按 (0,0)。
 * @return     视口像素坐标（就近取整**远离零**，Qt qRound 口径）。
 */
XPoint XGraphicsView_mapFromScene(const XGraphicsView* self,
                                  const XPointF* scenePoint);

/**
 * @brief      视口矩形 → 场景矩形（对标 mapToScene(QRect) 的**包盒**口径：
 *             Qt 返回 QPolygonF，本框架以四角映射的轴对齐包盒承载——
 *             旋转/剪切下的多边形信息由包盒近似，文档口径见 Qt
 *             mapToScene(QRect) 四角映射）。
 * @param      self 目标视图；可为 NULL（返回空矩形）。
 * @param      viewportRect 视口整数矩形；NULL 返回空矩形。
 * @return     场景坐标包盒（未取整）。
 */
XRectF XGraphicsView_mapToSceneRect(const XGraphicsView* self,
                                    const XRect* viewportRect);

/**
 * @brief      场景矩形 → 视口矩形（对标 mapFromScene(QRectF) 的包盒口径：
 *             四角经 viewportTransform 映射后取包盒再取整——Qt 返回
 *             QPolygon(int)，包盒是合法特例）。
 * @param      self 目标视图；可为 NULL（返回空矩形）。
 * @param      sceneRect 场景矩形；NULL 返回空矩形。
 * @return     视口整数包盒。
 */
XRect XGraphicsView_mapFromSceneRect(const XGraphicsView* self,
                                     const XRectF* sceneRect);

/* ==================== 导航 ==================== */

/**
 * @brief      把场景点居中（对标 centerOn(QPointF)，qgraphicsview.cpp:1878：
 *             viewPoint=matrix.map(pos)；无水平缩进的轴设横条值=
 *             round(viewPoint.x − viewport宽/2)、无垂直缩进的轴同理；
 *             有缩进（场景放得下）的轴不动——Qt 同）。
 * @param      self 目标视图；可为 NULL（空操作）。
 * @param      pos 场景坐标点；NULL 按 (0,0)。
 */
void XGraphicsView_centerOn(XGraphicsView* self, const XPointF* pos);

/**
 * @brief      把项居中（对标 centerOn(QGraphicsItem*)，
 *             qgraphicsview.cpp:1918：centerOn(item->sceneBoundingRect()
 *             .center())）。
 * @param      self 目标视图；可为 NULL（空操作）。
 * @param      item 目标项；可为 NULL（空操作）。
 */
void XGraphicsView_centerOnItem(XGraphicsView* self, XGraphicsItem* item);

/* ==================== 设备变换 ==================== */

/**
 * @brief      项在本视图下的设备变换（对标
 *             item->deviceTransform(viewportTransform) 的**视图侧组合**：
 *             = item.sceneTransform ∘ viewportTransform）。
 * @details    语义与 dpr 的关系见文件头注 ①：Qt 的 deviceTransform 全程
 *             逻辑坐标（qgraphicsitem.cpp:4287-4294），dpr 由绘制器 hidpi
 *             链承载（qpainter.cpp:626-643；本框架 XPainter.c:2351 同款）。
 *             本入口即 XGraphicsItem_deviceTransform 以本视图当前
 *             viewportTransform 为实参的组合捷径。
 * @param      self 目标视图；可为 NULL（输出单位阵）。
 * @param      item 目标项；可为 NULL（输出 viewportTransform 原样）。
 * @param      out 输出矩阵；不可为 NULL。
 */
void XGraphicsView_deviceTransform(const XGraphicsView* self,
                                   const XGraphicsItem* item,
                                   XImageTransform* out);

/* ==================== 拖动模式 ==================== */

/**
 * @brief      设置拖动模式（对标 setDragMode，qgraphicsview.cpp:1503：
 *             ScrollHandDrag→NoDrag 切换时清 handScrolling）。
 * @param      self 目标视图；可为 NULL（空操作）。
 * @param      mode XGraphicsViewDragMode。
 */
void XGraphicsView_setDragMode(XGraphicsView* self,
                               XGraphicsViewDragMode mode);

/**
 * @brief      拖动模式（对标 dragMode）。
 * @param      self 目标视图；可为 NULL（返回 NoDrag）。
 */
XGraphicsViewDragMode XGraphicsView_dragMode(const XGraphicsView* self);

/**
 * @brief      设置变换锚点（对标 setTransformationAnchor，
 *             qgraphicsview.cpp:1363-1372；缺省 AnchorViewCenter）。
 * @details    AnchorUnderMouse 已实现（见枚举注释与 centerView 口径）；
 *             选它时同时把**视口**鼠标跟踪打开（Qt 原文
 *             `d->viewport->setMouseTracking(true)`，:1370-1371）——否则
 *             悬停期的移动事件不来，锚点位置只能停在最后一次按键拖动处。
 * @param      self 目标视图；可为 NULL（空操作）。
 * @param      anchor XGraphicsViewViewportAnchor。
 */
void XGraphicsView_setTransformationAnchor(
    XGraphicsView* self, XGraphicsViewViewportAnchor anchor);

/**
 * @brief      变换锚点（对标 transformationAnchor）。
 * @param      self 目标视图；可为 NULL（返回 AnchorViewCenter）。
 */
XGraphicsViewViewportAnchor XGraphicsView_transformationAnchor(
    const XGraphicsView* self);

/**
 * @brief      橡皮筋矩形（对标 rubberBandRect，qgraphicsview.cpp:1564：
 *             仅 RubberBandDrag 且交互允许且拖拽进行中返回有效值；
 *             视口坐标）。
 * @param      self 目标视图；可为 NULL（返回 NULL）。
 * @return     当前橡皮筋矩形（借用）；**NULL=无带**（不在 RubberBandDrag/
 *             非交互/未按下），非 NULL 时矩形为零尺寸表示刚起带。
 *             禁止释放。
 */
const XRect* XGraphicsView_rubberBandRect_const(const XGraphicsView* self);

/* ==================== 与 scene 联动 ==================== */

/**
 * @brief      场景区域失效（对标 invalidateScene，qgraphicsview.cpp:1656
 *             的最小版：把映射进视口的区域标脏请求重绘；layers 参数与本
 *             框架无背景缓存（CacheBackground）对不上的部分不建模）。
 * @param      self 目标视图；可为 NULL（空操作）。
 * @param      sceneRect 场景坐标失效区域；NULL=全视口标脏。
 */
void XGraphicsView_invalidateScene(XGraphicsView* self,
                                   const XRectF* sceneRect);

/**
 * @brief      场景矩形变更通知（对标 updateSceneRect，qgraphicsview.cpp:
 *             2702：未设显式 sceneRect 时跟随 rect 并重算滚动范围；
 *             已设显式值时忽略）。
 * @param      self 目标视图；可为 NULL（空操作）。
 * @param      rect 新场景矩形；**NULL=无操作**（保留现值——Qt 该 slot 的参数
 *             无空语义，调用方未给就不改，不做虚构的「空矩形」写回）。
 */
void XGraphicsView_updateSceneRect(XGraphicsView* self, const XRectF* rect);

/* ==================== 渲染 ==================== */

/**
 * @brief      渲染当前视口到绘制器（paintEvent 的绘制核；对标
 *             QGraphicsView::paintEvent，qgraphicsview.cpp:3406）。
 * @details    流程（全部在**调用方 painter 的当前逻辑坐标系**里进行）：
 *             ① save；② 视口矩形裁剪（IntersectClip，vpRect 按**设置当时**
 *             的有效变换映射）；③ 平移到视口左上角（viewportRect 指明视口
 *             在 painter 坐标系里的位置；NULL=不裁剪不平移，painter 坐标系
 *             即视口坐标系）；④ viewportTransform **组合**进当前变换
 *             （setWorldTransform(…, combine=true)——调用方既有变换（
 *             paintOffset/离屏重定向/子控件坐标系）与 ③ 的平移都保留，
 *             对标 Qt render 的 setTransform(painterMatrix, true)，
 *             qgraphicsview.cpp:2180）；⑤ 背景笔刷铺暴露场景矩形
 *             （drawBackground 按笔刷样式分派——纯色填充 / 渐变走 XPainter
 *             渐变光栅 / 标准图案走 XPainter 图案位表 / 纹理按 tile 平铺；
 *             NoBrush 不铺底。笔刷状态写入包 save/restore，不污染后续项
 *             绘制。**纹理平铺性能口径**：XPixmap 纹理走框架平铺原语
 *             XPainter_drawTiledPixmap；只有纹理图像
 *             （XBrush_setTextureImage 的 XImage 槽）无平铺原语，按 tile
 *             尺寸循环 XPainter_drawImage——O(暴露面积 / tile 面积) 次
 *             贴图，大暴露区建议用 XPixmap 纹理或增大 tile）；⑥ 经
 *             XGraphicsScene_render 以暴露场景矩形绘制各项（逐项再叠
 *             sceneTransform——与 Qt drawItemHelper 的 setWorldTransform
 *             同构）；⑦ restore。
 *             渲染目标图像挂 dpr>1 时物理像素自动放大（XPainter hidpi
 *             链，XPainter.c:2351；Qt qpainter.cpp:626-643 同构）——本
 *             函数不感知 dpr。
 * @param      self 目标视图；可为 NULL（空操作）。
 * @param      painter 绘制器；不可为 NULL（空操作）。须已 begin；调用方
 *             painter 状态在返回后原样保留（save/restore 包裹）。
 * @param      viewportRect 视口在 painter 坐标系里的矩形；NULL=整幅即
 *             视口（不裁剪、不平移）。
 */
void XGraphicsView_renderViewport(XGraphicsView* self, XPainter* painter,
                                  const XRect* viewportRect);

/**
 * @brief      把视图内容渲染到绘制器（对标 QGraphicsView::render，
 *             qgraphicsview.cpp:2105 的最小版：aspectRatioMode 恒
 *             IgnoreAspectRatio——本接口只收 target/source 无比例枚举）。
 * @details    painterMatrix 的**净作用顺序** = matrix → T(-scroll) →
 *             T(-source.tl) → S(xratio, yratio) → T(target.tl)。Qt 原文
 *             qgraphicsview.cpp:2160-2166 写作
 *             `matrix * T(-scroll) * (T(target).scale(...).translate(-source))`，
 *             后一段是 QTransform 方法链（translate/scale 前乘）⇒ 先减
 *             source、再缩放、最后摆到 target；展开即上式。以 combine=true
 *             叠在调用方变换上；裁剪 target（IntersectClip）；再经
 *             XGraphicsScene_render 画 source 外扩 1px 对应的场景区域
 *             （Qt :2146 的 adjusted(-1,-1,1,1)）。
 *             缺省口径（Qt :2113-2125）：**source 缺省=视口矩形**
 *             （viewport 子控件自身尺寸，不含滚动条/框架）；**target
 *             缺省=source 尺寸**（Qt 对 Picture 设备取 source、其它设备取
 *             device 全幅；本框架 painter 无设备尺寸查询入口，退化为
 *             Picture 支——调用方显式给 target 可得精确结果）。
 *             未设场景（scene==NULL）=空操作，不触碰 painter（对标 Qt
 *             `if (!d->scene || ...) return;`，qgraphicsview.cpp:2110）。
 * @param      self 目标视图；可为 NULL（空操作）。
 * @param      painter 绘制器；不可为 NULL（空操作）。须已 begin。
 * @param      target 设备目标矩形（painter 逻辑坐标系）；NULL=source 尺寸。
 * @param      source 视口源矩形（视口坐标）；NULL=当前视口矩形。
 */
void XGraphicsView_render(XGraphicsView* self, XPainter* painter,
                          const XRectF* target, const XRect* source);

#endif /* XWIDGET_ON && XFRAME_ON && XSCROLLBAR_ON && XABSTRACTSCROLLAREA_ON && \
          XPAINTER_PATH_ON */

#endif /* XGUI_ON */

#ifdef __cplusplus
}
#endif

#endif /* XGRAPHICSVIEW_H */
