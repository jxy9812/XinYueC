/******************************************************************************
 * @file       XGraphicsItem.h
 * @brief      XGraphicsItem 图形项基类（对标 Qt 6.8 QGraphicsItem）。
 * @details    场景图（Graphics View）地基：承载 QGraphicsItem 的核心几何/
 *             变换/父子/z 序/标志/命中语义。坐标变换矩阵复用框架既有的
 *             XImageTransform 载体（Src/XGui/Graphics/XImage.h:49-60，与
 *             QPainter 世界变换同型；本模块补齐 2D 仿射矩阵最小代数，
 *             见 XGraphicsItem_matrix* 族——矩阵乘法/求逆/映射此前框架
 *             无现成实现，已 grep Src/XGui/Graphics + Src/XCore 取证）。
 *             scene 关联（W2-b 域落地）：m_scene 借用回指 + scene()/
 *             setSceneInternal（内部）+ ItemChange/Event 两个新虚槽——
 *             注册/命中/事件/选中的**所有权与通知链**在
 *             XGraphicsScene.h（scene 坐标仍以「根项链坐标」承载，
 *             mapToScene/sceneBoundingRect 沿父链累积变换、不依赖
 *             scene 对象，挂 scene 后口径不变）。
 * @note       对齐依据（Qt 6.8.3 原文 + Qt 6.8.0 offscreen 探针，探针源码
 *             与输出见本轮台账）：
 *             - qgraphicsitem.cpp:1789 setParentItem（自身为父=警告+返回；
 *               同值早退；祖先→后代 Qt 文档明言 undefined、6.8.0 实测
 *               挂死 ⇒ XGui 取安全分支拒绝）；
 *             - qgraphicsitem.cpp:1528 ~QGraphicsItem（父析构级联
 *               `while (!children.isEmpty()) delete children.first()`）；
 *             - qgraphicsitem.cpp:1818 childItems（按堆叠序=z 升序返回，
 *               同 z 按插入序；qgraphicsitem_p.h:622 qt_closestLeaf）；
 *             - qgraphicsitem.cpp:2563 setVisible / p.h explicitlyHidden
 *               （父隐藏⇒子有效隐藏；显式隐藏子在父再现后仍隐藏；重挂
 *               随新父可见性；探针 §10 逐条实测）；
 *             - qgraphicsitem.cpp:2737 setSelected（无 ItemIsSelectable
 *               强制 false；setFlags 摘除该位自动落选，探针 §11）；
 *             - qgraphicsitem.cpp:4653 stackBefore（非兄弟拒绝，探针 §8）；
 *             - qgraphicsitem.cpp:4913 contains（缺省 shape 命中）；
 *               4857 shape（缺省=boundingRect 矩形，局部坐标）；
 *             - qgraphicsitem.cpp:4287 deviceTransform（= sceneTransform ∘
 *               viewportTransform；【W3-c 勘误收口】Qt 原文本就不含 dpr
 *               ——dpr 在 QPainter hidpi 链（qpainter.cpp:626-643），见
 *               deviceTransform 声明注）；
 *             - qgraphicsitem.cpp:4764 sceneBoundingRect（boundingRect 经
 *               sceneTransform 映射的包盒；探针 §2 与手工矩阵精确一致）；
 *             - qgraphicsitem.cpp:1392 childrenBoundingRectHelper（后代
 *               boundingRect 逐一映射到本项局部坐标后取并集）；
 *             - p.h:236 calcEffectiveOpacity（不透明度沿父链累乘）。
 *             **与任务书的一处分歧（已按 Qt 原文+探针双证裁定）**：
 *             「重挂时保持 scenePos 不变」不是 Qt 语义——Qt 的
 *             setParentItem/setParentItemHelper（qgraphicsitem.cpp:1201）
 *             从不调整 pos，pos 保持不变、scenePos 随新父链变化（Qt 6.8.0
 *             探针 §3 实测：pos=(7,9) 不变，scenePos (7,9)→(110.662,120.158)）。
 *             本类按 Qt 真语义实现，相关验收断言以探针数值为准。
 * @author     XinYueC 团队
 ******************************************************************************/
#ifndef XGRAPHICSITEM_H
#define XGRAPHICSITEM_H

#ifdef __cplusplus
extern "C" {
#endif

#if XGUI_ON

#include <stdint.h>
#include <stdbool.h>
#include "XGuiConfig.h"
#include "XObject.h"
#include "XClass.h"
#include "XGeometry.h"
#include "XImage.h"        /* XImageTransform：2D 齐次矩阵载体（复用框架既有）。 */
#include "XPainter.h"      /* XPainterPath：shape 缺省实现返回矩形路径。 */
#include "XEvent.h"        /* XEvent：Event 槽位事件载荷（复用框架既有，
                              不新造事件类型；鼠标/按键/滚轮等场景事件复用
                              XMouseEvent/XKeyEvent/XWheelEvent/XEnterEvent
                              与 XEVENT_TYPE_GRAPHICS_SCENE_* 类型码）。 */

/*
 * 场景图基类不依赖控件层（Qt 的 QGraphicsItem 也不是 QWidget 系）：
 * 门禁只挂 XGUI_ON；shape/contains 缺省实现依赖 XPainterPath，随
 * XPAINTER_PATH_ON（XGuiConfig.h:471 缺省 1，XGUI_ON=0 时级联裁 0）。
 */
#if XPAINTER_PATH_ON

/** @brief XPainter 前向声明（Paint 槽位绘制器；完整定义见 XPainter.h）。 */
typedef struct XPainter XPainter;

/** @brief XGraphicsEffect 前向声明（图形项效果；完整定义见
 *         Widget/XGraphicsEffect.h。头文件只前向声明不互含：
 *         XGraphicsEffect.h 同样只前向声明本类，两模块无包含环）。 */
typedef struct XGraphicsEffect XGraphicsEffect;

/** @brief XGraphicsScene 前向声明（所属场景，借用指针；完整定义见
 *         XGraphicsScene.h。头文件只前向声明不互含：XGraphicsScene.h 含
 *         本头，本 .c 内部按需含 XGraphicsScene.h 取内部协作入口）。 */
typedef struct XGraphicsScene XGraphicsScene;

/* ==================== 类虚函数表枚举 ==================== */
XCLASS_DEFINE_BEGING(XGraphicsItem)
XCLASS_DEFINE_ENUM(XGraphicsItem, BoundingRect) =
    XCLASS_VTABLE_GET_SIZE(XObject), /**< 包围盒虚函数（对标 QGraphicsItem::
        boundingRect 的纯虚语义；首槽必须锚定父类槽位总数）。缺省实现返回
        空矩形并登记——具体图元（W2 起）必须覆写。 */
XCLASS_DEFINE_ENUM(XGraphicsItem, Paint),   /**< 绘制虚函数（对标 paint；
        缺省空实现）。 */
XCLASS_DEFINE_ENUM(XGraphicsItem, Shape),   /**< 形状虚函数（对标 shape；
        缺省 = boundingRect 的矩形路径，局部坐标）。 */
XCLASS_DEFINE_ENUM(XGraphicsItem, Contains),/**< 命中虚函数（对标 contains；
        缺省 = shape 命中）。 */
XCLASS_DEFINE_ENUM(XGraphicsItem, ItemChange),/**< 状态变更通知（对标
        itemChange(GraphicsItemChange, const QVariant&)；缺省原样回传
        value——「不改值、不否决」，与 Qt 基类缺省同口径）。 */
XCLASS_DEFINE_ENUM(XGraphicsItem, Event),    /**< 场景事件分派入口（对标
        QGraphicsItem::sceneEvent——Qt 由 sceneEvent 按事件类型转各
        mousePressEvent/hoverEnterEvent 等虚函数；本类收敛为一个槽位，
        缺省实现按 Qt 各事件缺省语义分派，见 XGraphicsItem.c）。 */
XCLASS_DEFINE_END(XGraphicsItem)  /* 自动续号；不可用 EXTEND_END（会把
    END_SIZE 重置回父类槽位数，OVERLOAD 索引越界）。 */

/**
 * @brief      图元标志位（对标 QGraphicsItem::GraphicsItemFlag 的子集，
 *             位值与 Qt 6.8 qgraphicsitem.h:50-52 逐位一致）。
 * @details    Visible 不是 flag：Qt 的可见性是独立布尔（d->visible +
 *             explicitlyHidden，qgraphicsitem_p.h），QGraphicsItem 没有
 *             ItemIsVisible 标志位——本类同样以 isVisible/setVisible
 *             承载（不发明 0x8 位，避免与 Qt 的 ItemClipsToShape=0x8
 *             错位，W2 扩其余标志时位值不返工）。
 */
typedef enum XGraphicsItemFlag
{
    XGraphicsItemFlag_ItemIsMovable = 0x1,    /**< 可移动（qgraphicsitem.h:50）。 */
    XGraphicsItemFlag_ItemIsSelectable = 0x2, /**< 可选中（qgraphicsitem.h:51）。 */
    XGraphicsItemFlag_ItemIsFocusable = 0x4,  /**< 可聚焦（qgraphicsitem.h:52）。 */
    XGraphicsItemFlag_ItemSendsGeometryChanges = 0x800, /**< 几何变更通知闸
        （qgraphicsitem.h:61 同位值；**只闸 setPos 的
        ItemPositionHasChanged**（Qt qgraphicsitem.cpp:3858 为
        GeometryChanges|ScenePositionChanges 合取）与 Transform/Rotation/
        Scale 系（:3993-4224，本子集未登）——**不闸** Parent 系（Qt
        setParentItem :1798 的 ItemParentChange 前置通知无闸、:1375 的
        `if (newParentVariant)` 恒真 ⇒ Parent 系无条件派发，
        【复核修订 2026-11-16】勘误）。 */
    XGraphicsItemFlag_ItemSendsScenePositionChanges = 0x10000 /**< 场景位置
        通知闸（qgraphicsitem.h:66 同位值；setPos 亦可经它派发
        ItemPositionHasChanged，Qt 同）。 */
} XGraphicsItemFlag;

/**
 * @brief      状态变更通知枚举（对标 QGraphicsItem::GraphicsItemChange 的
 *             最小子集，**数值与 Qt 6.8 qgraphicsitem.h 逐值一致**——
 *             ItemVisibleChange=2 之后的枚举顺序连续、无显式值）。
 * @details    本轮只登记场景域需要的 6 项；后续扩展按 Qt 枚举顺序追加、
 *             不改既有值。变更通知由 setter/scene 在状态变化**之后**派发
 *             （Qt 的 HasChanged 系），ItemSceneChange 为**前置**通知：
 *             返回值可否决/改道（Qt addItem/removeItem 原文语义），缺省
 *             原样回传 value 即「不否决」。
 */
typedef enum XGraphicsItemChange
{
    XGraphicsItemChange_ItemPositionHasChanged = 9,  /**< 位置已变（后置；
        值=const XPointF*（父坐标新位置）。仅当 ItemSendsGeometryChanges
        或 ItemSendsScenePositionChanges 置位时派发（Qt setPos 同口径，
        qgraphicsitem.cpp:3854/3877）。 */
    XGraphicsItemChange_ItemSceneChange = 11,        /**< 场景将变（**前置**；
        值=XGraphicsScene*（目标场景；摘离时 NULL）。返回值=生效场景：
        NULL=否决本次加入/摘离；指向另一场景=改道到该场景（Qt
        QGraphicsScene::addItem/removeItem 原文同口径）。 */
    XGraphicsItemChange_ItemVisibleHasChanged = 12,  /**< 可见性已变（后置；
        值=const bool*（新有效可见位）。不设通知闸，状态变化即派发
        （Qt setVisibleHelper 同）。 */
    XGraphicsItemChange_ItemSelectedHasChanged = 14, /**< 选中态已变（后置；
        值=const bool*（新选中位）。不设通知闸（Qt setSelected 同）。 */
    XGraphicsItemChange_ItemParentHasChanged = 15,   /**< 父项已变（后置；
        值=XGraphicsItem*（新父项，摘父为 NULL）。**无条件派发**（Qt
        setParentItem :1798 的前置 ItemParentChange 无闸；
        setParentItemHelper :1375 `if (newParentVariant)` 恒真——
        【复核修订 2026-11-16】勘误，此前版本误注「受
        ItemSendsGeometryChanges 闸控」）。 */
    XGraphicsItemChange_ItemSceneHasChanged = 16     /**< 场景已变（后置；
        值=XGraphicsScene*（新场景，摘离为 NULL）。由 scene 在
        addItem/removeItem 收尾派发（Qt 同）。 */
} XGraphicsItemChange;

/** @brief 标志位集合（按位或组合；对标 GraphicsItemFlags）。 */
typedef uint32_t XGraphicsItemFlags;

/**
 * @brief      XGraphicsItem 图形项基类；m_class 必须是第一个成员。
 * @details    持有独立于 XObject 对象树的**项树**（Qt 的 QGraphicsItem
 *             同样不是 QObject、项树独立于对象树）：m_parentItem/
 *             m_childItems 由本类自管，父项拥有子项（父析构级联删子）。
 *             @warning 结构体新增字段一律**尾部追加**（AGENTS.md 位置
 *             初始化器铁律；本结构 init 全字段显式清零）。
 */
typedef struct XGraphicsItem
{
    XObject m_class;              /**< 基类成员；必须是第一个。 */
    XGraphicsItemFlags m_flags;   /**< 标志位（缺省 0；对标 flags()）。 */
    struct XGraphicsItem* m_parentItem; /**< 父项（借用指针；父拥有子）。 */
    XVector* m_childItems;        /**< 子项表（XGraphicsItem* 借用指针数组，
                                       **插入序**；childItems() 另按 z 升序
                                       返回拷贝——对标 d->children 与
                                       ensureSortedChildren 的分工）。 */
    int m_siblingIndex;           /**< 兄弟序号（父表中插入序；对标
                                       d->siblingIndex，同 z 决胜排序）。 */
    XPointF m_pos;                /**< 父坐标位置（对标 pos；重挂不变——
                                       Qt 真语义，scenePos 随父链变）。 */
    XImageTransform m_transform;  /**< 本项变换（恒有值，缺省单位阵；对标
                                       d->transformData->transform）。 */
    float m_z;                    /**< z 序（对标 zValue；缺省 0）。 */
    float m_opacity;              /**< 本项不透明度 0..1（对标 opacity）。 */
    bool m_visible;               /**< **有效**可见位（父链同步下沉，对标
                                       d->visible——Qt 模型，非"仅自身位"）。 */
    bool m_explicitlyHidden;      /**< 显式隐藏位（对标 explicitlyHidden：
                                       父再现时不复活显式隐藏的子）。 */
    bool m_selected;              /**< 选中态（对标 d->selected；入场景后
                                       由 XGraphicsScene_selectedItems/
                                       selectionChanged 汇聚）。 */
    bool m_acceptHoverEvents;     /**< 是否接受悬停事件（对标
                                       acceptHoverEvents）。 */
    struct XGraphicsScene* m_scene; /**< 所属场景（**借用**指针，场景不因此
                                       拥有本项——Qt 同：QGraphicsItem::
                                       scene() 只是回指；置位/摘除只经
                                       XGraphicsScene_addItem/removeItem
                                       与其内部路径，W2 域）。 */
    /* ---- 新增字段一律追加在此注释之后 ---- */
    XGraphicsEffect* m_graphicsEffect; /**< 挂接的图形效果（**本项拥有**；
                                       对标 d_ptr->graphicsEffect，
                                       qgraphicsitem_p.h。缺省 NULL；
                                       setGraphicsEffect 换装删旧、析构删本，
                                       与 Qt 同。效果侧的 m_itemSource 是
                                       互指借用，删除效果时经
                                       XGraphicsItem_graphicsEffectDetach
                                       摘空本槽）。 */
} XGraphicsItem;

/* ==================== 矩阵最小代数（QTransform 对应物） ====================
 * 框架此前只有 XImageTransform 结构（XPainter 世界变换内部消费），无任何
 * 矩阵代数入口（grep Src/XGui/Graphics + Src/XCore 取证）。本族按 Qt 6.8
 * qtransform.cpp 语义补齐 2D 仿射最小集，命名暂挂 XGraphicsItem 命名空间
 * （所有权在本模块文件）；W2 scene/view 若需跨文件复用，建议整族抬升为
 * 独立 XTransform 模块（新文件），本类公开 API 面不变。 */

/**
 * @brief      单位阵。
 * @param      m 输出矩阵；不可为 NULL。
 */
void XGraphicsItem_matrixIdentity(XImageTransform* m);

/**
 * @brief      平移矩阵（对标 QTransform::fromTranslate）。
 * @param      m 输出矩阵；不可为 NULL。
 * @param      dx X 平移量。
 * @param      dy Y 平移量。
 */
void XGraphicsItem_matrixFromTranslate(XImageTransform* m, float dx, float dy);

/**
 * @brief      就地把平移**前乘**进矩阵（对标 QTransform::translate——
 *             qtransform.cpp:352 对 TxScale/TxRotate 均为 T·M 语义：
 *             先平移当前坐标系、再套用原矩阵）。
 * @param      m 目标矩阵（就地修改）；不可为 NULL。
 * @param      dx X 平移量。
 * @param      dy Y 平移量。
 */
void XGraphicsItem_matrixTranslate(XImageTransform* m, float dx, float dy);

/**
 * @brief      就地对矩阵做缩放（对标 QTransform::scale——qtransform.cpp:
 *             420-448）：**S·M 前乘**语义，「先缩放坐标系（点坐标先乘
 *             缩放）、再套原矩阵」；线性项 m11/m12 乘 sx、m21/m22 乘 sy，
 *             **平移分量 dx/dy 不参与缩放**（Qt 各分支均不改平移；本机
 *             Qt 6.8.0 实测 translate(5,8).scale(2,2) → dx=5, dy=8）。
 * @param      m 目标矩阵（就地修改）；不可为 NULL。
 * @param      sx X 缩放系数。
 * @param      sy Y 缩放系数。
 */
void XGraphicsItem_matrixScale(XImageTransform* m, float sx, float sy);

/**
 * @brief      就地把旋转**前乘**进矩阵（对标 QTransform::rotate——
 *             qtransform.cpp:678 rotateRadians 对 Z 轴为 R·M 语义；正角
 *             逆时针，作用到 y 向下的屏幕坐标呈顺时针，与 Qt 同口径）。
 * @param      m 目标矩阵（就地修改）；不可为 NULL。
 * @param      deg 角度（度）。
 */
void XGraphicsItem_matrixRotate(XImageTransform* m, float deg);

/**
 * @brief      矩阵乘法（对标 QTransform::operator*）。
 * @details    (a*b).map(p) = b.map(a.map(p))——**a 先作用、b 后作用**
 *             （qtransform.cpp:907 展开式；行向量口径 p' = p·a·b）。
 * @param      out 输出矩阵；可与 a/b 同址。
 * @param      a 左操作数（先作用）。
 * @param      b 右操作数（后作用）。
 */
void XGraphicsItem_matrixMultiply(XImageTransform* out, const XImageTransform* a,
                                  const XImageTransform* b);

/**
 * @brief      仿射求逆（对标 QTransform::inverted）。
 * @param      in 输入矩阵；不可为 NULL。
 * @param      out 输出矩阵；可与 in 同址。
 * @return     true=成功；false=奇异矩阵（行列式 0，out 写单位阵，对标
 *             Qt 返回 identity 并置 ok=false）。
 */
bool XGraphicsItem_matrixInvert(const XImageTransform* in, XImageTransform* out);

/**
 * @brief      点映射（对标 QTransform::map(QPointF)；
 *             x' = m11·x + m21·y + dx）。
 * @param      m 矩阵；不可为 NULL。
 * @param      x 点 X 坐标。
 * @param      y 点 Y 坐标。
 * @param      out 输出点；不可为 NULL。
 */
void XGraphicsItem_matrixMapPoint(const XImageTransform* m, float x, float y,
                                  XPointF* out);

/**
 * @brief      矩形映射包盒（对标 QTransform::mapRect(QRectF)：映射四角
 *             后取轴对齐包围盒）。
 * @param      m 矩阵；不可为 NULL。
 * @param      rect 输入矩形；不可为 NULL。
 * @param      out 输出矩形；可与 rect 同址。
 */
void XGraphicsItem_matrixMapRect(const XImageTransform* m, const XRectF* rect,
                                 XRectF* out);

/* ==================== 虚函数槽位类型 ==================== */
/** @brief BoundingRect 虚函数槽位类型（对标 boundingRect）。 */
typedef XRectF (*XGraphicsItem_BoundingRectSlot)(const XGraphicsItem* self);
/** @brief Paint 虚函数槽位类型（对标 paint；Qt 另带 option/widget 实参，
 *         本框架 option 由场景 render 统一承载（世界变换/不透明度已入
 *         绘制器状态），W3/W4 视图域再议显式 option 形参）。 */
typedef void (*XGraphicsItem_PaintSlot)(XGraphicsItem* self, XPainter* painter);
/** @brief Shape 虚函数槽位类型（对标 shape）。out 由调用方
 *         XPainterPath_init/deinit；实现只许追加元素、不得重新 init。 */
typedef bool (*XGraphicsItem_ShapeSlot)(const XGraphicsItem* self,
                                        XPainterPath* out);
/** @brief Contains 虚函数槽位类型（对标 contains；点为**局部坐标**）。 */
typedef bool (*XGraphicsItem_ContainsSlot)(const XGraphicsItem* self,
                                           const XPointF* point);
/** @brief ItemChange 虚函数槽位类型（对标 itemChange）。
 *  @param change XGraphicsItemChange 通知码。
 *  @param value  按 change 解释的只读值指针（XGraphicsItemChange 各枚举
 *                注释逐一列出；可为 NULL）。
 *  @return       生效值指针：缺省实现原样回传 value（不否决/不改值，
 *                Qt 基类同口径）；ItemSceneChange 可返回 NULL 否决本次
 *                加入/摘离、或返回另一场景指针改道（Qt addItem/
 *                removeItem 原文语义）。 */
typedef const void* (*XGraphicsItem_ItemChangeSlot)(XGraphicsItem* self,
                                                    int change,
                                                    const void* value);
/** @brief Event 虚函数槽位类型（对标 QGraphicsItem::sceneEvent）。
 *  @param event 框架既有事件对象（借用；类型码为 XEVENT_TYPE_*，场景域
 *               用 XEVENT_TYPE_GRAPHICS_SCENE_* 与焦点/按键码位）。事件
 *               的 m_position 已由派发方换算到本项局部坐标。
 *  @return       true=已递达本项（对标 sendEvent 的返回）；事件是否被
 *                「消费」看 m_accepted（Qt 的 accepted 语义）。 */
typedef bool (*XGraphicsItem_EventSlot)(XGraphicsItem* self, XEvent* event);

/* ==================== 类与实例生命周期 ==================== */

/**
 * @brief      XGraphicsItem 类虚函数表初始化。
 * @return     共享 XVtable 指针。
 */
XVtable* XGraphicsItem_class_init(void);

/**
 * @brief      初始化 XGraphicsItem（对标 QGraphicsItem 构造：无父、pos(0,0)、
 *             单位变换、z=0、不透明度 1、可见、无标志）。
 * @param      self 目标对象指针；不可为 NULL。
 */
void XGraphicsItem_init(XGraphicsItem* self);
#define XGraphicsItem_create() XGraphicsItem_create_ex(XCLASS_DEFAULT_MEMORY_TYPE)
/**
 * @brief      使用指定内存类型创建 XGraphicsItem。
 * @param      memory 对象内存类型。
 * @return     新对象指针；失败返回 NULL。
 */
XGraphicsItem* XGraphicsItem_create_ex(XMemoryType memory);

/* ==================== 虚槽位唯一公开入口（_base 契约） ====================
 * 用户 2026-11-14 定案：虚函数对外唯一入口只留 `_base`，函数体直接按虚表
 * 槽分派；不提供无后缀同义入口。槽位为空时回落基类默认实现（VX* 静态
 * 函数，与 Qt「空槽 = 默认行为」同口径）。派生类业务实现 static VX* 直挂
 * 虚表；槽内回落父类用 XClass_Parent(XGraphicsItem, EXGraphicsItem_*, ...)。 */

/**
 * @brief      包围盒（对标 QGraphicsItem::boundingRect，经虚表分派）。
 * @details    Qt 中为纯虚；本基类缺省返回**空矩形**并登记——具体图元必须
 *             覆写（W2 图元域落矩形/椭圆/文本等缺省）。返回值为**局部
 *             坐标**，不受 pos/transform 影响（Qt 原文：unaffected by the
 *             items' transformation）。
 * @param      self 目标项；可为 NULL（返回空矩形）。
 * @return     局部坐标包围盒。
 */
XRectF XGraphicsItem_boundingRect_base(const XGraphicsItem* self);

/**
 * @brief      绘制（对标 paint，经虚表分派；缺省空实现）。
 * @param      self 目标项；可为 NULL（空操作）。
 * @param      painter 绘制器；可为 NULL（空操作）。
 */
void XGraphicsItem_paint_base(XGraphicsItem* self, XPainter* painter);

/**
 * @brief      形状（对标 shape，经虚表分派）。
 * @details    缺省实现 = boundingRect_base 的矩形路径（Qt 原文：The default
 *             implementation calls boundingRect() to return a simple
 *             rectangular shape）。路径为**局部坐标**。
 * @param      self 目标项；可为 NULL（返回 false）。
 * @param      out 输出路径；调用方已 XPainterPath_init、用后
 *             XPainterPath_deinit；实现只追加元素。
 * @return     true=已填充；false=参数无效。
 */
bool XGraphicsItem_shape_base(const XGraphicsItem* self, XPainterPath* out);

/**
 * @brief      命中（对标 contains，经虚表分派）。
 * @details    缺省实现 = shape_base 命中（Qt 原文：By default, this
 *             function calls shape()）。点为**局部坐标**——项自身的
 *             pos/transform 不参与判定（Qt 探针 §7：带旋转的项 contains
 *             局部点仍按局部形状判）。
 * @param      self 目标项；可为 NULL（返回 false）。
 * @param      point 局部坐标点；可为 NULL（返回 false）。
 * @return     点落在形状内返回 true。
 */
bool XGraphicsItem_contains_base(const XGraphicsItem* self, const XPointF* point);

/**
 * @brief      状态变更通知（对标 QGraphicsItem::itemChange，经虚表分派）。
 * @details    缺省实现原样回传 value。派发时机（Qt 6.8 原文口径）：
 *             - ItemSceneChange/ItemSceneHasChanged：由 XGraphicsScene_
 *               addItem/removeItem 前后派发（qgraphicsscene.cpp:2412/
 *               2824/2533/2834）；
 *             - ItemSelectedHasChanged：XGraphicsItem_setSelected 状态翻转
 *               后派发（qgraphicsitem.cpp:2768）；
 *             - ItemVisibleHasChanged：有效可见位翻转后派发
 *               （setVisibleHelper，qgraphicsitem.cpp:2525）；
 *             - ItemPositionHasChanged：setPos 且
 *               ItemSendsGeometryChanges/ItemSendsScenePositionChanges
 *               置位时派发（qgraphicsitem.cpp:3877）；
 *             - ItemParentHasChanged：setParentItem 成功重挂后**无条件**
 *               派发（Qt qgraphicsitem.cpp:1798 前置通知无闸、:1375
 *               `if (newParentVariant)` 恒真——【复核修订】勘误）。
 * @param      self 目标项；可为 NULL（原样返回 value）。
 * @param      change XGraphicsItemChange 通知码。
 * @param      value 按 change 解释的只读值指针；可为 NULL。
 * @return     生效值指针（缺省=value）。
 */
const void* XGraphicsItem_itemChange_base(XGraphicsItem* self, int change,
                                          const void* value);

/**
 * @brief      场景事件分派入口（对标 QGraphicsItem::sceneEvent，经虚表
 *             分派）。
 * @details    缺省实现按 Qt 6.8 各事件缺省语义分派：
 *             - GRAPHICS_SCENE_MOUSE_PRESS：左键+可选中⇒场景清选后选中本
 *               项（无 Ctrl 多选——qgraphicsitem.cpp mousePressEvent 缺省）；
 *               非可移动项忽略（accepted=false）；
 *             - GRAPHICS_SCENE_MOUSE_MOVE：左键+可移动⇒拖动本项（按
 *               场景锚点差值映射回父坐标，qgraphicsitem.cpp
 *               mouseMoveEvent 缺省的单项分支），可选中则顺带置选；
 *             - KEY_PRESS/KEY_RELEASE/WHEEL：缺省忽略（qgraphicsitem.cpp
 *               同名缺省 handler 均为 event->ignore()）；
 *             - GRAPHICS_SCENE_HOVER_*：缺省空操作（Qt hoverEnterEvent
 *               缺省仅 update()，本子集无项级缓存，空操作同效）。
 * @param      self 目标项；可为 NULL（返回 false）。
 * @param      event 框架事件对象（借用）。
 * @return     true=已递达本项；false=参数无效或对象无虚表。
 */
bool XGraphicsItem_event_base(XGraphicsItem* self, XEvent* event);

/* ==================== 几何与变换 ==================== */

/**
 * @brief      父坐标位置（对标 pos）。
 * @param      self 目标项；可为 NULL（返回 (0,0)）。
 * @return     位置副本。
 */
XPointF XGraphicsItem_pos(const XGraphicsItem* self);

/**
 * @brief      设置父坐标位置（对标 setPos；同值早退）。
 * @details    翻转后：新旧包围盒并集标脏（入场景时）+ 派发
 *             ItemPositionHasChanged（值=const XPointF*；仅
 *             ItemSendsGeometryChanges/ItemSendsScenePositionChanges
 *             置位时派发——Qt setPos 同口径，W2-b 域接线）。
 * @param      self 目标项；可为 NULL（空操作）。
 * @param      position 新位置；为 NULL 时按 (0,0) 处理。
 */
void XGraphicsItem_setPos(XGraphicsItem* self, const XPointF* position);

/**
 * @brief      本项变换（对标 transform；借用指针，随本项存亡，禁止释放；
 *             setTransform 后失效）。
 * @param      self 目标项；可为 NULL（返回 NULL）。
 * @return     内部变换矩阵借用指针。
 */
const XImageTransform* XGraphicsItem_transform_const(const XGraphicsItem* self);

/**
 * @brief      设置本项变换（对标 setTransform(matrix, combine)）。
 * @details    combine=true 时 matrix **先作用**、原矩阵后作用（Qt 原文：
 *             `newTransform(combine ? matrix * transform : matrix)`，
 *             qgraphicsitem.cpp:4473）；同值早退（Qt 同）。
 * @param      self 目标项；可为 NULL（空操作）。
 * @param      matrix 新矩阵；为 NULL 按 2D 单位阵处理（对标 QTransform()）。
 * @param      combine true=与现矩阵组合，false=替换。
 */
void XGraphicsItem_setTransform(XGraphicsItem* self, const XImageTransform* matrix,
                                bool combine);

/**
 * @brief      场景变换（对标 sceneTransform；本轮 scene = 根项链坐标）。
 * @details    沿父链累积：item→parent = 本项变换 ∘ T(pos)（变换先作用、
 *             再加位——qgraphicsitem.cpp:1124 combineTransformToParent），
 *             sceneTransform(item) = itemToParent ∘ sceneTransform(parent)
 *             （updateSceneTransformFromParent 同序，探针 §1 与手工链
 *             精确一致 maxRel=3.5e-16）。
 * @param      self 目标项；可为 NULL（输出单位阵）。
 * @param      out 输出矩阵；不可为 NULL。
 */
void XGraphicsItem_sceneTransform(const XGraphicsItem* self, XImageTransform* out);

/**
 * @brief      场景坐标位置（对标 scenePos = mapToScene(0,0)，
 *             qgraphicsitem.cpp:3798）。
 * @param      self 目标项；可为 NULL（返回 (0,0)）。
 * @return     场景（根项链）坐标位置。
 */
XPointF XGraphicsItem_scenePos(const XGraphicsItem* self);

/**
 * @brief      点映射到父坐标（对标 mapToParent；无父时等价 mapToScene）。
 * @param      self 目标项；可为 NULL（原样返回）。
 * @param      point 局部坐标点；可为 NULL（返回 (0,0)）。
 * @return     父（或根项链）坐标点。
 */
XPointF XGraphicsItem_mapToParent(const XGraphicsItem* self, const XPointF* point);

/**
 * @brief      点从父坐标映射回局部（对标 mapFromParent）。
 * @param      self 目标项；可为 NULL（原样返回）。
 * @param      point 父（或根项链）坐标点；可为 NULL（返回 (0,0)）。
 * @return     局部坐标点。
 */
XPointF XGraphicsItem_mapFromParent(const XGraphicsItem* self, const XPointF* point);

/**
 * @brief      点映射到另一项坐标（对标 mapToItem(item, point)）。
 * @details    item 为 NULL 时等价 mapToScene（Qt 原文同）。
 * @param      self 目标项；可为 NULL。
 * @param      item 目标项；可为 NULL（走 mapToScene）。
 * @param      point 本项局部坐标点；可为 NULL（返回 (0,0)）。
 * @return     目标项局部坐标点。
 */
XPointF XGraphicsItem_mapToItem(const XGraphicsItem* self, const XGraphicsItem* item,
                                const XPointF* point);

/**
 * @brief      点从另一项坐标映射回本项局部（对标 mapFromItem）。
 * @details    item 为 NULL 时等价 mapFromScene（Qt 原文同）。
 * @param      self 目标项；可为 NULL。
 * @param      item 源项；可为 NULL（走 mapFromScene）。
 * @param      point 源项局部坐标点；可为 NULL（返回 (0,0)）。
 * @return     本项局部坐标点。
 */
XPointF XGraphicsItem_mapFromItem(const XGraphicsItem* self, const XGraphicsItem* item,
                                  const XPointF* point);

/**
 * @brief      点映射到场景（根项链）坐标（对标 mapToScene）。
 * @param      self 目标项；可为 NULL（原样返回）。
 * @param      point 局部坐标点；可为 NULL（返回 (0,0)）。
 * @return     场景坐标点。
 */
XPointF XGraphicsItem_mapToScene(const XGraphicsItem* self, const XPointF* point);

/**
 * @brief      点从场景（根项链）坐标映射回局部（对标 mapFromScene）。
 * @param      self 目标项；可为 NULL（原样返回）。
 * @param      point 场景坐标点；可为 NULL（返回 (0,0)）。
 * @return     局部坐标点。
 */
XPointF XGraphicsItem_mapFromScene(const XGraphicsItem* self, const XPointF* point);

/**
 * @brief      矩形映射到场景坐标的包盒（对标 mapToScene(QRectF) 的包盒
 *             口径——Qt 返回 QPolygonF，场景包围盒即其包盒）。
 * @param      self 目标项；可为 NULL（原样返回）。
 * @param      rect 局部坐标矩形；可为 NULL（返回空矩形）。
 * @return     场景坐标轴对齐包盒。
 */
XRectF XGraphicsItem_mapRectToScene(const XGraphicsItem* self, const XRectF* rect);

/**
 * @brief      矩形从场景坐标包盒映射回局部（对标 mapFromScene(QRectF)
 *             的包盒口径；即 mapRectToScene 的逆映射）。
 * @param      self 目标项；可为 NULL（原样返回）。
 * @param      rect 场景坐标矩形；可为 NULL（返回空矩形）。
 * @return     局部坐标轴对齐包盒。
 */
XRectF XGraphicsItem_mapRectFromScene(const XGraphicsItem* self, const XRectF* rect);

/**
 * @brief      本项坐标系到另一项坐标系的变换矩阵（对标 itemTransform）。
 * @details    分派口径与 Qt 6.8 逐支一致（qgraphicsitem.cpp:4351）：
 *             父子/兄弟/祖先链/共同祖先/无共同祖先（各自根链坐标互映，
 *             对标 Qt 的 sceneTransform 组合支）。
 * @param      self 目标项；不可为 NULL。
 * @param      other 目标坐标系所属项；不可为 NULL。
 * @param      out 输出矩阵；不可为 NULL。
 * @return     true=成功；false=参数无效或求逆奇异（out=单位阵，对标
 *             Qt ok=false）。
 */
bool XGraphicsItem_itemTransform(const XGraphicsItem* self, const XGraphicsItem* other,
                                 XImageTransform* out);

/**
 * @brief      设备变换（对标 deviceTransform(viewportTransform)；**终版
 *             语义，W3-c 已收口**）。
 * @details    Qt 原文（qgraphicsitem.cpp:4287-4294）：可变换项返回
 *             `sceneTransform * viewportTransform`，**全程逻辑坐标、
 *             不含 dpr**——dpr 由绘制设备承载（QPainterPrivate::
 *             updateMatrix，qpainter.cpp:626-643，hidpiScaleTransform
 *             乘在 combined 矩阵末端；本框架同链=XPainter_begin_image
 *             从图像元数据载入 + painterEffectiveTransform 末端折入，
 *             XPainter.c:8312/2351）。W1 注的「dpr=1 的 logical 版、W4
 *             再接 dpr」经 W3-c 按 Qt 6.8.3 原文复核为**勘误**：Qt 的
 *             deviceTransform 本就没有 dpr 形参与 dpr 分支（缺的只是
 *             ItemIgnoresTransformations 支，本子集无该标志位），矩阵
 *             语义即终版；视图侧组合见 XGraphicsView_deviceTransform
 *             （XGraphicsView.h）。
 * @param      self 目标项；可为 NULL（输出 viewportTransform 原样）。
 * @param      viewportTransform 场景→视口（设备）矩阵（视图侧取
 *             XGraphicsView_viewportTransform）；为 NULL 按单位阵处理。
 * @param      out 输出矩阵；不可为 NULL。
 */
void XGraphicsItem_deviceTransform(const XGraphicsItem* self,
                                   const XImageTransform* viewportTransform,
                                   XImageTransform* out);

/**
 * @brief      场景坐标包围盒（对标 sceneBoundingRect = sceneTransform 把
 *             boundingRect 映射成包盒，qgraphicsitem.cpp:4764）。
 * @param      self 目标项；可为 NULL（返回空矩形）。
 * @return     场景（根项链）坐标包围盒。
 */
XRectF XGraphicsItem_sceneBoundingRect(const XGraphicsItem* self);

/**
 * @brief      后代包围盒（对标 childrenBoundingRect：全部后代（含孙代）
 *             的 boundingRect 逐一映射到**本项局部坐标**后取并集；不含
 *             本项自身；无后代返回空矩形）。
 * @param      self 目标项；可为 NULL（返回空矩形）。
 * @return     本项局部坐标的后代包围盒。
 */
XRectF XGraphicsItem_childrenBoundingRect(const XGraphicsItem* self);

/* ==================== 父子关系 ==================== */

/**
 * @brief      父项（对标 parentItem；借用指针）。
 * @param      self 目标项；可为 NULL。
 * @return     父项借用指针；无父返回 NULL。
 */
XGraphicsItem* XGraphicsItem_parentItem(const XGraphicsItem* self);

/**
 * @brief      顶层项（对标 topLevelItem：父链尽头的无父项）。
 * @param      self 目标项；可为 NULL。
 * @return     顶层项指针（self 无父时即 self）。
 */
XGraphicsItem* XGraphicsItem_topLevelItem(const XGraphicsItem* self);

/**
 * @brief      是否为 child 的祖先（对标 isAncestorOf；自身不算自己的祖先）。
 * @param      self 目标项；可为 NULL。
 * @param      child 疑似后代；可为 NULL。
 * @return     self 是 child 的严格祖先返回 true。
 */
bool XGraphicsItem_isAncestorOf(const XGraphicsItem* self, const XGraphicsItem* child);

/**
 * @brief      最近共同祖先（对标 commonAncestorItem；无共同祖先（两棵
 *             独立树）返回 NULL；self==other 返回 self）。
 * @param      self 目标项；可为 NULL。
 * @param      other 另一项；可为 NULL。
 * @return     共同祖先借用指针；可为 NULL。
 */
XGraphicsItem* XGraphicsItem_commonAncestorItem(const XGraphicsItem* self,
                                                const XGraphicsItem* other);

/**
 * @brief      设置父项（对标 setParentItem）。
 * @details    语义逐条对标 Qt 6.8（qgraphicsitem.cpp:1789 + 1201
 *             setParentItemHelper + 6.8.0 探针 §3/§4）：
 *             ① newParent == self：拒绝并告警（Qt：cannot assign as a
 *                parent of itself）；
 *             ② newParent == 现父：同值早退返回 true（Qt 静默 return）；
 *             ③ **祖先→后代（成环）**：Qt 文档明言 undefined、6.8.0 探针
 *                实测挂死 ⇒ XGui 取安全分支：拒绝并告警、树保持原状；
 *             ④ 正常重挂：从旧父表摘下、按插入序挂到新父表尾（兄弟序号
 *                重排）；**pos 保持不变**、scenePos 随新父链变化（Qt 真实
 *                语义，探针 §3 实测，任务书「保持 scenePos」与 Qt 相悖，
 *                见文件头注）；
 *             ⑤ 可见性随新父：新父隐藏（或本项非显式隐藏而父可见态不同）
 *                时按 setVisibleHelper(explicit=false) 下沉同步；摘父
 *                （newParent=NULL）后非显式隐藏的隐藏项恢复可见（Qt 同）。
 * @param      self 目标项；不可为 NULL。
 * @param      newParent 新父项；可为 NULL（摘父成顶层项）。
 * @return     true=关系成立（含同值早退）；false=被拒绝（成环/参数无效）。
 */
bool XGraphicsItem_setParentItem(XGraphicsItem* self, XGraphicsItem* newParent);

/**
 * @brief      子项列表，按堆叠序（z 升序，同 z 按插入序）返回（对标
 *             childItems；Qt 原文：sorted by stacking order, taking into
 *             account insertion order and z-values）。
 * @warning    返回**新分配**的 XVector（元素为 XGraphicsItem* 借用指针，
 *             禁止释放元素）；容器本身由调用方 XClassDelete 释放。无子
 *             返回 NULL。
 * @param      self 目标项；可为 NULL。
 * @return     z 升序子项指针表；调用方负责释放容器。
 */
XVector* XGraphicsItem_childItems(const XGraphicsItem* self);

/**
 * @brief      z 序（对标 zValue；缺省 0）。
 * @param      self 目标项；可为 NULL（返回 0）。
 * @return     z 值。
 */
float XGraphicsItem_zValue(const XGraphicsItem* self);

/**
 * @brief      设置 z 序（对标 setZValue；同值早退）。
 * @param      self 目标项；可为 NULL（空操作）。
 * @param      z 新 z 值。
 */
void XGraphicsItem_setZValue(XGraphicsItem* self, float z);

/**
 * @brief      堆叠到同级兄弟之前（对标 stackBefore）。
 * @details    只调整**插入序**（兄弟序号），对 childItems() 的可见效果
 *             体现在**同 z** 兄弟之间（不同 z 时 z 值决胜——Qt 同口径）。
 *             非兄弟（父项不同）拒绝并告警（Qt：cannot stack under ...，
 *             qgraphicsitem.cpp:4657）。Qt 无 stackAfter API，不发明
 *             （需要「排到某兄弟之后」时对该兄弟的后一位 stackBefore，
 *             探针 §8 同款用法）。
 * @param      self 目标项；不可为 NULL。
 * @param      sibling 参照兄弟；可为 NULL（拒绝）。
 * @return     true=已调整；false=被拒绝（非兄弟/参数无效）。
 */
bool XGraphicsItem_stackBefore(XGraphicsItem* self, const XGraphicsItem* sibling);

/* ==================== 标志位 / 可见性 / 选中 / 透明度 / 悬停 ==================== */

/**
 * @brief      标志位集合（对标 flags）。
 * @param      self 目标项；可为 NULL（返回 0）。
 * @return     XGraphicsItemFlag 位或组合。
 */
XGraphicsItemFlags XGraphicsItem_flags(const XGraphicsItem* self);

/**
 * @brief      设置标志位集合（对标 setFlags）。
 * @details    摘除 ItemIsSelectable 时自动落选（Qt 原文：if the item was
 *             selected, and flags does not enable ItemIsSelectable, the
 *             item is automatically unselected；探针 §11 实测 selected
 *             1→0）。
 * @param      self 目标项；可为 NULL（空操作）。
 * @param      flags 新标志位集合。
 */
void XGraphicsItem_setFlags(XGraphicsItem* self, XGraphicsItemFlags flags);

/**
 * @brief      开关单个标志位（对标 setFlag(flag, enabled)）。
 * @param      self 目标项；可为 NULL（空操作）。
 * @param      flag 标志位。
 * @param      enabled true=置位，false=清位。
 */
void XGraphicsItem_setFlag(XGraphicsItem* self, XGraphicsItemFlag flag, bool enabled);

/**
 * @brief      有效可见性（对标 isVisible；Qt 的 d->visible 是父链同步的
 *             **有效位**——父隐藏时子已同步为隐藏）。
 * @param      self 目标项；可为 NULL（返回 false）。
 * @return     可见返回 true。
 */
bool XGraphicsItem_isVisible(const XGraphicsItem* self);

/**
 * @brief      相对某祖先的可见性（对标 isVisibleTo，qgraphicsitem.cpp:2355
 *             原样移植：显式隐藏即 false；沿父链到 ancestor 为止只看
 *             explicitHidden 位）。
 * @param      self 目标项；可为 NULL（返回 false）。
 * @param      ancestor 参照祖先；为 NULL 表示「到根/场景是否可见」。
 * @return     若 ancestor 及以上全可见则本项会可见返回 true。
 */
bool XGraphicsItem_isVisibleTo(const XGraphicsItem* self, const XGraphicsItem* ancestor);

/**
 * @brief      设置可见性（对标 setVisible）。
 * @details    Qt 模型（setVisibleHelper 原样移植，探针 §10 全分支实测）：
 *             显式调用记 explicitlyHidden 位；父隐藏时子上的 show 不生效
 *             （子随父隐藏）；父再现时显式隐藏的子不复活；隐藏下沉到
 *             全部非显式隐藏的后代；摘父恢复非显式隐藏项的可见位。
 * @param      self 目标项；可为 NULL（空操作）。
 * @param      visible true=显示，false=隐藏。
 */
void XGraphicsItem_setVisible(XGraphicsItem* self, bool visible);

/**
 * @brief      是否选中（对标 isSelected）。
 * @param      self 目标项；可为 NULL（返回 false）。
 * @return     选中返回 true。
 */
bool XGraphicsItem_isSelected(const XGraphicsItem* self);

/**
 * @brief      设置选中态（对标 setSelected）。
 * @details    无 ItemIsSelectable 标志（或隐藏）时强制落选（Qt 原文
 *             qgraphicsitem.cpp:2744 `if (!(flags & ItemIsSelectable) ||
 *             !enabled || !visible) selected = false;`）。翻转后：标脏 +
 *             场景批量闸外即时发 selectionChanged + 派发
 *             ItemSelectedHasChanged（值=const bool*；W2-b 域接线）。
 * @param      self 目标项；可为 NULL（空操作）。
 * @param      selected true=选中，false=落选。
 */
void XGraphicsItem_setSelected(XGraphicsItem* self, bool selected);

/**
 * @brief      本项不透明度（对标 opacity；0..1，缺省 1）。
 * @param      self 目标项；可为 NULL（返回 1.0）。
 * @return     不透明度。
 */
float XGraphicsItem_opacity(const XGraphicsItem* self);

/**
 * @brief      设置本项不透明度（对标 setOpacity；夹到 [0,1] 同 Qt）。
 * @param      self 目标项；可为 NULL（空操作）。
 * @param      opacity 新不透明度。
 */
void XGraphicsItem_setOpacity(XGraphicsItem* self, float opacity);

/**
 * @brief      有效不透明度（对标 effectiveOpacity；p.h:236：本项 opacity
 *             沿父链逐级乘父项 opacity，本子集标志位不含
 *             ItemIgnoresParentOpacity/ItemDoesntPropagateOpacityToChildren，
 *             即恒全链累乘——两标志 W2 随其余标志位扩入时在此分支）。
 * @param      self 目标项；可为 NULL（返回 1.0）。
 * @return     有效不透明度。
 */
float XGraphicsItem_effectiveOpacity(const XGraphicsItem* self);

/**
 * @brief      是否接受悬停事件（对标 acceptHoverEvents；缺省 false）。
 * @param      self 目标项；可为 NULL（返回 false）。
 * @return     接受返回 true。
 */
bool XGraphicsItem_acceptHoverEvents(const XGraphicsItem* self);

/**
 * @brief      设置是否接受悬停事件（对标 setAcceptHoverEvents）。
 * @param      self 目标项；可为 NULL（空操作）。
 * @param      accept true=接受。
 */
void XGraphicsItem_setAcceptHoverEvents(XGraphicsItem* self, bool accept);

/* ==================== 场景关联（W2 域） ==================== */

/**
 * @brief      所属场景（对标 scene；借用指针）。
 * @param      self 目标项；可为 NULL（返回 NULL）。
 * @return     场景借用指针；未入场景返回 NULL。禁止释放。
 */
XGraphicsScene* XGraphicsItem_scene(const XGraphicsItem* self);

/**
 * @brief      设置所属场景回指（**内部协作入口**；对标 d_ptr->scene = …）。
 * @warning    仅限 XGraphicsScene 模块与其拆卸路径调用——对外请走
 *             XGraphicsScene_addItem/removeItem（所有权与通知链在那里）。
 *             不派发 ItemSceneChange/HasChanged（那是 addItem/removeItem
 *             的职责）；不做同值早退（内部路径由调用方保证幂等）。
 * @param      self 目标项；可为 NULL（空操作）。
 * @param      scene 新场景借用指针；可为 NULL（摘除回指）。
 * @return     true=已写入。
 */
bool XGraphicsItem_setSceneInternal(XGraphicsItem* self,
                                    XGraphicsScene* scene);

/* ==================== 图形效果（W4；对标 QGraphicsItem::graphicsEffect） ====
 * 所有权契约（**与 Qt 逐条一致**，qgraphicsitem.cpp:2880-2922）：
 *   ① 图形项**拥有**自己挂接的效果：setGraphicsEffect 换装时删除旧效果、
 *      图形项析构时删除效果；
 *   ② 同一效果不得同时挂在两个图形项上——挂到第二个项时从原项摘下
 *      （Qt 文档原文：If effect is the installed effect on a different
 *      item, setGraphicsEffect() will remove the effect from the item and
 *      install it on this item）；
 *   ③ 直接删除已挂接的效果 ⇒ 图形项的槽位被摘空（对标
 *      ~QGraphicsEffect → setGraphicsEffectSource(nullptr) 让
 *      item->graphicsEffect() 变 NULL）。
 * **本入口即接管语义**（对标 QWidget::setGraphicsEffect / QGraphicsItem::
 * setGraphicsEffect）：调用即失所有权（唯一例外是 self 为 NULL——无项可挂，
 * 此时不接管、不释放，见下方 @param）；不另设 `_ref` 同义别名（用户
 * 2026-11-16 定案：按 Qt 只有接管语义，写进头注）。 */

/**
 * @brief      挂接的效果（对标 graphicsEffect；**借用**指针，禁止释放）。
 * @param      self 目标项；可为 NULL。
 * @return     效果借用指针；无效果返回 NULL。随项存亡/换装失效。
 */
XGraphicsEffect* XGraphicsItem_graphicsEffect_const(const XGraphicsItem* self);

/**
 * @brief      设置图形效果（对标 QGraphicsItem::setGraphicsEffect；
 *             **接管所有权**，Qt 6.8 qgraphicsitem.cpp:2900-2919 逐条）。
 * @details    语义：
 *              ① 同值早退（`d_ptr->graphicsEffect == effect` 直接 return）；
 *              ② 旧效果先发 sourceChanged(SourceDetached)（通知时 source()
 *                 仍指旧项，自定义效果据此清缓存；对标
 *                 qgraphicseffect_p.h:121-134 的 `flags |= SourceDetached`），
 *                 再经 XGraphicsEffect_setItemSource(NULL) 断源、最后
 *                 XClassDelete（对标 `delete d_ptr->graphicsEffect`）；
 *              ③ 效果若挂在别的图形项/控件上，先从旧宿主摘下（防双删；
 *                 该路径的 Detached 由 XGraphicsEffect_setItemSource 发）；
 *              ④ 新效果挂上后 `XGraphicsEffect_setItemSource` 建立反向
 *                 关联并补发 SourceAttached，再作废旧/新包围盒的脏区
 *                 （对标 prepareGeometryChange，含效果外扩环）；
 *              ⑤ 传入 NULL = 只卸载（旧效果同样被删除并收到 SourceDetached，
 *                 对标 Qt：置空同样 delete 旧效果）。
 * @param      self 目标项；可为 NULL（此时**不**接管 effect——无项可挂，
 *             调用方仍持有 effect。为避免"传 NULL 丢所有权"的歧义，
 *             头注释明确：self 为 NULL 时不接管、不释放）。
 * @param      effect 新效果（可为 NULL=卸载）；本函数接管其所有权。
 */
void XGraphicsItem_setGraphicsEffect(XGraphicsItem* self,
                                     XGraphicsEffect* effect);

/**
 * @brief      摘空图形项侧的效果槽位（**内部协作入口**；对标 Qt
 *             QGraphicsItemEffectSourcePrivate 析构/分离时把
 *             `item->d_ptr->graphicsEffect` 置空）。
 * @warning    仅限 XGraphicsEffect 的 Deinit 槽与挂接路径调用——对外请走
 *             XGraphicsItem_setGraphicsEffect。只摘槽位、**不删除** effect
 *             （删除动作由触发方负责：效果自己析构时槽位先摘，图形项析构
 *             时先断源再删）。判据是「槽 == effect」，否则不动。
 * @param      self 目标项；可为 NULL（返回 false）。
 * @param      effect 正在分离的效果；可为 NULL（返回 false）。
 * @return     true=确实摘下了槽位。
 */
bool XGraphicsItem_graphicsEffectDetach(XGraphicsItem* self,
                                        XGraphicsEffect* effect);

#endif /* XPAINTER_PATH_ON */

#endif /* XGUI_ON */

#ifdef __cplusplus
}
#endif

#endif /* XGRAPHICSITEM_H */
