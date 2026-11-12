/******************************************************************************
 * @file       XGraphicsScene.h
 * @brief      XGraphicsScene 图形场景（对标 Qt 6.8 QGraphicsScene）。
 * @details    Graphics View 场景层最小闭环（W2-b 域）：item 注册/索引、
 *             命中测试（itemAt/itemsRect/itemsPath）、事件分发
 *             （鼠标/滚轮/悬停/按键/上下文菜单，复用框架既有事件对象）、
 *             选中（批量闸 + selectionChanged 信号一次/批）、焦点、
 *             整场景渲染（render 按堆叠序逐项 paint）。索引为线性遍历
 *             （Qt 缺省 BSP 树索引的子集口径；每拍现算排序，量级小时
 *             等价，W4 可换 BSP/缓存——见文件尾注）。
 * @note       对齐依据（Qt 6.8.3 原文；函数级出处逐条写在实现注释里）：
 *             - qgraphicsscene.cpp:2396 addItem（跨场景先 removeItem；
 *               ItemSceneChange 前置通知可否决/改道；子树逐个 addItem；
 *               批量闸内 selectionChanged 只发一次；ItemSceneHasChanged
 *               收尾派发）；
 *             - qgraphicsscene.cpp:2812 removeItem + removeItemHelper
 *               （**scene 不拥有被 remove 的 item**——所有权回归调用方，
 *               子树整棵摘离且结构保持；焦点/抓取/悬停引用随摘清理）；
 *             - qgraphicsscene.cpp:2282 clear（自顶向下 delete 全部
 *               顶层项；子项经 ~QGraphicsItem 级联析构并各自注销）；
 *             - qgraphicssceneindex.cpp recursive_items_helper（items()
 *               升序=DFS：父先子后、同级按 z 升序+插入序；隐藏与全透明
 *               剔除；DescendingOrder=整表反转）；
 *             - qgraphicssceneindex.cpp intersect_point/intersect_rect/
 *               intersect_path（_q_adjustRect 零宽高修补 + 包围盒预筛 +
 *               局部坐标 shape/contains 精判；itemAt=items(point,
 *               IntersectsItemShape,Descending).first()，2100）；
 *             - qgraphicsscene.cpp:2194 setSelectionArea / 2248
 *               clearSelection（selectionChanging 批量闸内每项
 *               setSelected，闸撤后按 changed 发一次 selectionChanged）；
 *             - qgraphicsscene.cpp:2878 setFocusItem + setFocusItemHelper
 *               （不可聚焦/不可见 ⇒ 只清焦；旧焦发 FocusOut、新焦发
 *               FocusIn；Qt focusItemChanged 信号本轮不登）；
 *             - qgraphicsscene.cpp:3948/3973/4003 鼠标三事件 + 1309
 *               mousePressEventHandler（命中候选降序逐个隐式抓取直到
 *               accepted；顶层可聚焦项随按下获焦；无人接受⇒清抓取+清选；
 *               stickyFocus 按住焦点）；
 *             - qgraphicsscene.cpp:3763 dispatchHoverEvent（悬停目标切换
 *               时旧项 Leave、新项 Enter、目标 Move；本轮以 m_lastHoverItem
 *               单项跟踪承载——Qt 的 hoverItems 栈承载的是**子项链**
 *               Enter/Leave 逐级展开，本子集无子项链悬停分支，W4 扩）；
 *             - qgraphicsscene.cpp:1746 render（升序逐项：
 *               worldTransform=sceneTransform、opacity=effectiveOpacity、
 *               item->paint；背景/前景笔刷与 aspectRatio 缩放本轮不登）；
 *             - qgraphicsscene.cpp:3095 update（脏区登记：本轮只累计并
 *               查询，帧合成属 W3/W4）。
 * @author     XinYueC 团队
 ******************************************************************************/
#ifndef XGRAPHICSSCENE_H
#define XGRAPHICSSCENE_H

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
#include "XEvent.h"
#include "XGraphicsItem.h" /* item 树全部语义在此；场景只做注册/派发。 */
#include "XWindowEvent.h"  /* XWheelEvent：滚轮事件承载（复用框架既有
                              事件对象，不新造事件类型）。 */

#if XPAINTER_PATH_ON

/* ==================== 类虚函数表枚举 ==================== */
XCLASS_DEFINE_BEGING(XGraphicsScene)
XCLASS_DEFINE_EXTEND_END(XGraphicsScene, XObject) /* 本子集无新虚槽位：
    Deinit 覆写在 class_init 内挂（释放顶层表容器 + 父类清尾）。 */

/* ==================== 枚举 ==================== */

/**
 * @brief      命中测试模式（对标 Qt::ItemSelectionMode，值一致）。
 */
typedef enum XGraphicsItemSelectionMode
{
    XGraphicsItemSelectionMode_ContainsItemShape = 0,        /**< 形状包含。 */
    XGraphicsItemSelectionMode_IntersectsItemShape = 1,      /**< 形状相交。 */
    XGraphicsItemSelectionMode_ContainsItemBoundingRect = 2, /**< 包围盒包含。 */
    XGraphicsItemSelectionMode_IntersectsItemBoundingRect = 3 /**< 包围盒相交。 */
} XGraphicsItemSelectionMode;

/** @brief 命中集合排序（对标 Qt::SortOrder：升序=堆叠自底向上，
 *         降序=自顶向下；值一致）。 */
typedef enum XGraphicsSortOrder
{
    XGraphicsSortOrder_AscendingOrder = 0,  /**< 升序（下层在前）。 */
    XGraphicsSortOrder_DescendingOrder = 1  /**< 降序（上层在前）。 */
} XGraphicsSortOrder;

/** @brief 选中操作（对标 Qt::ItemSelectionOperation；本轮只实现替换支，
 *         追加支是 QGraphicsScene::setSelectionArea 的 5.14+ 重载，
 *         W4 扩）。 */
typedef enum XGraphicsItemSelectionOperation
{
    XGraphicsItemSelectionOperation_ReplaceSelection = 0, /**< 替换选中。 */
    XGraphicsItemSelectionOperation_AddToSelection = 1    /**< 追加选中（本轮
        登记枚举值、setSelectionArea 暂按替换处理，见 openNotes）。 */
} XGraphicsItemSelectionOperation;

/* ==================== 类与实例生命周期 ==================== */

/**
 * @brief      XGraphicsScene 场景类；m_class 必须是第一个成员。
 * @details    场景**不拥有**仍在场景中的 item 的所有权账本之外的东西：
 *             Qt 口径——addItem 后场景「接管」顶层项链（clear() 会 delete
 *             仍存在的顶层项与整棵子项链），removeItem 摘离后所有权回归
 *             调用方（不删）。场景内部只登记**顶层项**（子项随树可达，
 *             对标 Qt index 的 topLevelItems 登记口径）。
 *             @warning 结构体新增字段一律**尾部追加**（AGENTS.md 位置
 *             初始化器铁律；本结构 init 全字段显式清零）。
 */
typedef struct XGraphicsScene
{
    XObject m_class;              /**< 基类成员；必须是第一个。 */
    XVector* m_topLevelItems;     /**< 顶层项表（XGraphicsItem* 借用指针，
                                       **插入序**=顶层兄弟插入序；items()
                                       的同级排序键=z 升序+表内序）。 */
    XRectF m_sceneRect;           /**< 场景矩形（宽/高均 0=未显式设置，
                                       sceneRect() 回落 itemsBoundingRect
                                       ——Qt d->sceneRect 置位口径）。 */
    XGraphicsItem* m_focusItem;   /**< 焦点项（借用；对标 d->focusItem）。 */
    XGraphicsItem* m_lastHoverItem; /**< 最近悬停项（借用；悬停目标切换
                                       时旧项 Leave/新项 Enter——Qt
                                       dispatchHoverEvent 的单项承载）。 */
    XGraphicsItem* m_mouseGrabberItem; /**< 鼠标抓取项（借用；隐式抓取——
                                       对标 d->mouseGrabberItems 单项化）。 */
    XPointF m_mouseDownScenePos;  /**< 当前抓取的按下锚点（场景坐标；对标
                                       d->mouseGrabberButtonDownScenePos
                                       按单键表的单项化，本轮一轮一抓）。 */
    XPointF m_dragInitialPos;     /**< 抓取项按下时的父坐标位置（对标
                                       d->movingItemsInitialPositions
                                       的单项化；仅在抓取期有效）。 */
    bool m_hasDragAnchor;         /**< 拖动锚点有效位（对标
                                       movingItemsInitialPositions 非空）。 */
    XPointF m_lastEventScenePos;  /**< 最近一次派发事件的场景坐标（浮点，
                                       供缺省拖动/松开判定位取用——事件
                                       对象 m_position 是整数项局部坐标，
                                       浮点场景坐标必须另存）。 */
    int m_selectionChanging;      /**< 选中批量闸深度（对标
                                       d->selectionChanging；>0 时逐项
                                       setSelected 不发信号，闸撤后按
                                       changed 发一次）。 */
    bool m_stickyFocus;           /**< 粘性焦点（对标 d->stickyFocus：置位时
                                       空点按下不清焦）。 */
    bool m_dirtyAll;              /**< 全场景脏位（update()）。 */
    XRectF m_dirtyRect;           /**< 区域脏累计矩形（updateRect；与
                                       m_dirtyAll 任一为真即有待合成）。 */
    /* ---- 新增字段一律追加在此注释之后 ---- */
} XGraphicsScene;

/**
 * @brief      XGraphicsScene 类虚函数表初始化。
 * @return     共享 XVtable 指针。
 */
XVtable* XGraphicsScene_class_init(void);

/**
 * @brief      初始化 XGraphicsScene（对标 QGraphicsScene 构造：无项、
 *             场景矩形未设置、无焦点/抓取/悬停、非粘性焦点、不脏）。
 * @param      self 目标对象指针；不可为 NULL。
 */
void XGraphicsScene_init(XGraphicsScene* self);
#define XGraphicsScene_create() XGraphicsScene_create_ex(XCLASS_DEFAULT_MEMORY_TYPE)
/**
 * @brief      使用指定内存类型创建 XGraphicsScene。
 * @param      memory 对象内存类型。
 * @return     新对象指针；失败返回 NULL。
 */
XGraphicsScene* XGraphicsScene_create_ex(XMemoryType memory);

/* ==================== 场景矩形 ==================== */

/**
 * @brief      场景矩形（对标 sceneRect）。
 * @details    未显式设置（宽/高均 0）时回落 itemsBoundingRect()（Qt
 *             原文：if the scene rect is unset, it equals the bounding
 *             rect of all items on the scene）。
 * @param      self 目标场景；可为 NULL（返回空矩形）。
 * @return     场景坐标矩形。
 */
XRectF XGraphicsScene_sceneRect(const XGraphicsScene* self);

/**
 * @brief      设置场景矩形（对标 setSceneRect；同值早退）。
 * @param      self 目标场景；可为 NULL（空操作）。
 * @param      rect 新矩形；为 NULL 按 (0,0,0,0)（=未设置）处理。
 */
void XGraphicsScene_setSceneRect(XGraphicsScene* self, const XRectF* rect);

/**
 * @brief      全部项的场景包围盒（对标 itemsBoundingRect：所有 item——
 *             含子项——的 sceneBoundingRect 并集；无项返回空矩形）。
 * @param      self 目标场景；可为 NULL（返回空矩形）。
 * @return     场景坐标包围盒。
 */
XRectF XGraphicsScene_itemsBoundingRect(const XGraphicsScene* self);

/* ==================== item 注册（所有权按 Qt） ==================== */

/**
 * @brief      加入项及其整棵子项链（对标 addItem，qgraphicsscene.cpp:
 *             2396 原文逐支）：
 *             ① item 为 NULL：告警返回；
 *             ② 已属本场景：告警返回（Qt：already been added）；
 *             ③ 属其它场景：先从旧场景移除（removeItem）再入本场景；
 *             ④ **前置**派发 ItemSceneChange（值=本场景）：返回 NULL=否决
 *                本次加入（树保持原状）；返回另一场景=改道加入该场景；
 *             ⑤ 子项为其它场景所属时先摘父（Qt：detach from parent），
 *                随后**每个子项各自走一遍完整 addItem**（含各自的通知与
 *                递归）；
 *             ⑥ 顶层项登记进场景表；selectedItems 口径的批量闸内不发
 *                selectionChanged，收尾按「选中集合是否变化」发一次——
 *                记账覆盖**整棵加入子树**（子树任一节点已选中即算变化，
 *                Qt selectedItems.size 前后对比的等价承载）；
 *             ⑦ **后置**派发 ItemSceneHasChanged（值=本场景）。
 *             所有权：本场景接管该项链（clear() 会删除仍存在的顶层项）。
 * @param      self 目标场景；可为 NULL（空操作）。
 * @param      item 目标项；可为 NULL（告警返回）。
 */
void XGraphicsScene_addItem(XGraphicsScene* self, XGraphicsItem* item);

/**
 * @brief      摘离项及其整棵子项链（对标 removeItem + removeItemHelper，
 *             qgraphicsscene.cpp:2812）：
 *             ① item 为 NULL 或 item->scene() != 本场景：告警返回；
 *             ② **前置**派发 ItemSceneChange（值=NULL）：返回 NULL=按摘离
 *                继续；返回另一场景=改道加入该场景；
 *             ③ 子树整棵摘离（各节点场景回指清 NULL），**树结构保持**、
 *                不从父项摘离（Qt removeItemHelper 同：父已摘离场景时
 *                子不再 detach）；
 *             ④ 焦点/鼠标抓取/悬停引用落在子树内则一并清理；
 *             ⑤ **后置**派发 ItemSceneHasChanged（值=NULL）。
 *             所有权：**场景不删除被 remove 的 item**（Qt 原文：the item
 *             's ownership is transferred back to the caller）——调用方
 *             自行删除或另行加入其它场景。
 * @param      self 目标场景；可为 NULL（空操作）。
 * @param      item 目标项；可为 NULL（告警返回）。
 */
void XGraphicsScene_removeItem(XGraphicsScene* self, XGraphicsItem* item);

/**
 * @brief      移除并删除全部项（对标 clear，qgraphicsscene.cpp:2282）：
 *             自顶向下 delete 场景内**仍存在的顶层项**（子项经
 *             ~XGraphicsItem 级联析构、析构内各自向场景注销）；场景其余
 *             状态（场景矩形/粘性焦点/脏区）不动（Qt 原文：leaves the
 *             state of the scene unchanged）。
 * @param      self 目标场景；可为 NULL（空操作）。
 */
void XGraphicsScene_clear(XGraphicsScene* self);

/* ==================== 索引与命中 ==================== */

/**
 * @brief      全部项（对标 items(Qt::SortOrder)）。
 * @details    **索引纯登记口径**：含隐藏/全透明项（Qt 的
 *             index->items(order) 不过滤可见性，探针 P14 实证：items()
 *             含隐藏 E；几何重载 items(rect/point/path) 才按「有效可见
 *             且有效不透明度>0」剔除）。升序=堆叠自底向上（DFS：父先
 *             子后，同级 z 升序+插入序，对标 recursive_items_helper）；
 *             降序=整表反转（Qt 同）。
 * @warning    返回**新分配**的 XVector（元素为 XGraphicsItem* 借用指针，
 *             禁止释放元素）；容器本身由调用方 XClassDelete 释放。无项
 *             返回 NULL。
 * @param      self 目标场景；可为 NULL。
 * @param      order XGraphicsSortOrder。
 * @return     项指针表；调用方释放容器。
 */
XVector* XGraphicsScene_items(const XGraphicsScene* self,
                              XGraphicsSortOrder order);

/**
 * @brief      与点命中的项集（对标 items(QPointF, mode, order)）。
 * @details    「可见」口径同 Qt：有效可见且有效不透明度>0（递归剔除
 *             隐藏/全透明子树）；先包围盒预筛再经 contains_base 在项
 *             局部坐标精判（_q_adjustRect 零宽高修补同口径）。
 * @warning    返回**新分配**的 XVector（元素借用，禁止释放）；容器由
 *             调用方 XClassDelete 释放。空集返回 NULL。
 * @param      self 目标场景；可为 NULL。
 * @param      pos 场景坐标点；可为 NULL（返回 NULL）。
 * @param      mode XGraphicsItemSelectionMode。
 * @param      order XGraphicsSortOrder。
 * @return     项指针表；调用方释放容器。
 */
XVector* XGraphicsScene_itemsAt(const XGraphicsScene* self, const XPointF* pos,
                                XGraphicsItemSelectionMode mode,
                                XGraphicsSortOrder order);

/**
 * @brief      与矩形相交的项集（对标 items(QRectF, mode, order)）。
 * @details    缺省口径（Qt IntersectsItemShape）：包围盒相交预筛后经
 *             shape_base 精判；ContainsItemShape=矩形含形状；
 *             Contains/Intersects BoundingRect=只比包围盒。
 * @warning    返回**新分配**的 XVector（元素借用，禁止释放）；容器由
 *             调用方 XClassDelete 释放。空集返回 NULL。
 * @param      self 目标场景；可为 NULL。
 * @param      rect 场景坐标矩形；可为 NULL（返回 NULL）。
 * @param      mode XGraphicsItemSelectionMode。
 * @param      order XGraphicsSortOrder。
 * @return     项指针表；调用方释放容器。
 */
XVector* XGraphicsScene_itemsRect(const XGraphicsScene* self, const XRectF* rect,
                                  XGraphicsItemSelectionMode mode,
                                  XGraphicsSortOrder order);

/**
 * @brief      与路径相交的项集（对标 items(QPainterPath, mode, order)）。
 * @details    预筛 = 路径 controlPointRect 与项场景包围盒相交；精判 =
 *             项形状（或包围盒）**场景坐标包围盒** 与路径的
 *             intersectsRect/containsRect（XPainterPath 原语是 int 口径，
 *             边外扩半格取整）。**矩形图元精确**；凹形图元按包围盒承载
 *             可能多报——Qt 用 QPainterPath 布尔运算（intersects/
 *             contains O(N·logN)），本框架尚无路径-路径布尔原语，
 *             W4 随该原语收口（openNotes 已登记）。
 * @warning    返回**新分配**的 XVector（元素借用，禁止释放）；容器由
 *             调用方 XClassDelete 释放。空集返回 NULL。
 * @param      self 目标场景；可为 NULL。
 * @param      path 场景坐标路径；可为 NULL（返回 NULL）。
 * @param      mode XGraphicsItemSelectionMode。
 * @param      order XGraphicsSortOrder。
 * @return     项指针表；调用方释放容器。
 */
XVector* XGraphicsScene_itemsPath(const XGraphicsScene* self,
                                  const XPainterPath* path,
                                  XGraphicsItemSelectionMode mode,
                                  XGraphicsSortOrder order);

/**
 * @brief      点位最上层可见项（对标 itemAt，qgraphicsscene.cpp:2100：
 *             items(pos, IntersectsItemShape, DescendingOrder).first()）。
 * @param      self 目标场景；可为 NULL（返回 NULL）。
 * @param      pos 场景坐标点；可为 NULL（返回 NULL）。
 * @return     最上层命中项借用指针；无命中返回 NULL。
 */
XGraphicsItem* XGraphicsScene_itemAt(const XGraphicsScene* self,
                                     const XPointF* pos);

/* ==================== 选中 ==================== */

/**
 * @brief      按矩形设置选中集（对标 setSelectionArea(QRectF...) 的路径
 *             化简：矩形→矩形路径，模式 IntersectsItemShape、操作
 *             ReplaceSelection、批量闸内 selectionChanged 一次/批）。
 * @details    仅 ItemIsSelectable 的项响应 setSelected（其余项被 Qt 的
 *             setSelected 强制落选，跳过即可——本函数只对可选项调用
 *             setSelected，路径外可选项经 setSelected(false) 落选）。
 * @param      self 目标场景；可为 NULL（空操作）。
 * @param      rect 场景坐标选择矩形；可为 NULL（清选）。
 */
void XGraphicsScene_setSelectionAreaRect(XGraphicsScene* self,
                                         const XRectF* rect);

/**
 * @brief      按路径设置选中集（对标 setSelectionArea(QPainterPath,
 *             Qt::ReplaceSelection, Qt::IntersectsItemShape)，2194 原文）。
 * @param      self 目标场景；可为 NULL（空操作）。
 * @param      path 场景坐标选择路径；可为 NULL（清选）。
 */
void XGraphicsScene_setSelectionAreaPath(XGraphicsScene* self,
                                         const XPainterPath* path);

/**
 * @brief      清除全部选中（对标 clearSelection，2248：批量闸内逐项
 *             setSelected(false)，闸撤后按 changed 发一次 selectionChanged）。
 * @param      self 目标场景；可为 NULL（空操作）。
 */
void XGraphicsScene_clearSelection(XGraphicsScene* self);

/**
 * @brief      选中项集（对标 selectedItems；本轮按堆叠升序返回——Qt 文档
 *             明言「no particular order」，确定性升序是合法特例且便于
 *             断言）。
 * @warning    返回**新分配**的 XVector（元素借用，禁止释放）；容器由
 *             调用方 XClassDelete 释放。空集返回 NULL。
 * @param      self 目标场景；可为 NULL。
 * @return     项指针表；调用方释放容器。
 */
XVector* XGraphicsScene_selectedItems(const XGraphicsScene* self);

/**
 * @brief      发射「选中集已变化」信号（对标 QGraphicsScene::
 *             selectionChanged，无参数）。
 * @return     信号标识。
 * @note       槽函数原型：void slot(XObject* sender, XVarList* args)，
 *             无参数。本信号只在「一批选中变更」收尾发射一次（批量闸
 *             撤除且确有变化时；单个 setSelected 在闸外也会即时发射——
 *             Qt 两路同口径）。
 */
void* XGraphicsScene_selectionChanged_signal(XGraphicsScene* self);

/* ==================== 焦点 ==================== */

/**
 * @brief      设置焦点项（对标 setFocusItem + setFocusItemHelper）。
 * @details    item 不可聚焦（无 ItemIsFocusable）或不可见 ⇒ 只清焦不授焦
 *             （Qt 原文分支）；旧焦点项收 FocusOut、新焦点项收 FocusIn
 *             （经 sendEvent→Event 槽）。item 为 NULL 等价 clearFocus。
 * @param      self 目标场景；可为 NULL（空操作）。
 * @param      item 目标项；可为 NULL（清焦）。
 */
void XGraphicsScene_setFocusItem(XGraphicsScene* self, XGraphicsItem* item);

/**
 * @brief      当前焦点项（对标 focusItem）。
 * @param      self 目标场景；可为 NULL（返回 NULL）。
 * @return     焦点项借用指针；无焦点返回 NULL。
 */
XGraphicsItem* XGraphicsScene_focusItem(const XGraphicsScene* self);

/**
 * @brief      清除场景焦点（对标 clearFocus：旧焦点项收 FocusOut）。
 * @param      self 目标场景；可为 NULL（空操作）。
 */
void XGraphicsScene_clearFocus(XGraphicsScene* self);

/**
 * @brief      设置粘性焦点（对标 setStickyFocus：置位后点击空白不清焦）。
 * @param      self 目标场景；可为 NULL（空操作）。
 * @param      enabled true=粘性。
 */
void XGraphicsScene_setStickyFocus(XGraphicsScene* self, bool enabled);

/**
 * @brief      粘性焦点（对标 stickyFocus）。
 * @param      self 目标场景；可为 NULL（返回 false）。
 * @return     粘性焦点返回 true。
 */
bool XGraphicsScene_stickyFocus(const XGraphicsScene* self);

/* ==================== 事件分发 ==================== */

/**
 * @brief      直接向项派发事件（对标 QGraphicsScenePrivate::sendEvent）。
 * @details    不做坐标换算、不查命中——事件 m_position 已须为项局部
 *             坐标（场景级入口在派发前完成换算）；「递达」经 Event 虚槽。
 * @param      self 目标场景；可为 NULL（返回 false）。
 * @param      item 目标项；可为 NULL（返回 false）。
 * @param      event 框架事件对象（借用）；不可为 NULL。
 * @return     true=已递达（与 Qt sendEvent 同口径：过滤/禁用不在本子集）。
 */
bool XGraphicsScene_sendEvent(XGraphicsScene* self, XGraphicsItem* item,
                              XEvent* event);

/**
 * @brief      鼠标按下（对标 mousePressEvent → mousePressEventHandler）。
 * @details    event->m_position 为**场景坐标**（整数口径，复用框架事件
 *             对象的既定字段）。流程：悬停先行（Qt 按下先派悬停）→
 *             有抓取项则直送抓取项 → 否则命中候选降序逐个隐式抓取并发
 *             GRAPHICS_SCENE_MOUSE_PRESS，accepted 即止；顶层首个可聚焦
 *             可见项随按下获焦（无人获焦且非粘性 ⇒ 清焦）；全部拒绝 ⇒
 *             清抓取+清选中（Qt 原文尾段）。
 * @param      self 目标场景；可为 NULL（空操作）。
 * @param      event 鼠标事件（借用）；不可为 NULL（空操作）。
 */
void XGraphicsScene_mousePressEvent(XGraphicsScene* self, XMouseEvent* event);

/**
 * @brief      鼠标移动（对标 mouseMoveEvent）。
 * @details    无抓取项且有键按下 ⇒ 静默返回（Qt 同）；无抓取且无键 ⇒
 *             按悬停处理；有抓取项 ⇒ 事件换算到抓取项局部坐标直送
 *             （拖动语义在 Event 缺省实现的 move 分支）。
 * @param      self 目标场景；可为 NULL（空操作）。
 * @param      event 鼠标事件（借用）；不可为 NULL。
 */
void XGraphicsScene_mouseMoveEvent(XGraphicsScene* self, XMouseEvent* event);

/**
 * @brief      鼠标松开（对标 mouseReleaseEvent）。
 * @details    无抓取项 ⇒ 忽略；有抓取项 ⇒ 换算后直送；全键松开 ⇒ 清
 *             隐式抓取与拖动锚点，并按松开位置派一轮悬停（Qt 同）。
 * @param      self 目标场景；可为 NULL（空操作）。
 * @param      event 鼠标事件（借用）；不可为 NULL。
 */
void XGraphicsScene_mouseReleaseEvent(XGraphicsScene* self, XMouseEvent* event);

/**
 * @brief      当前鼠标抓取项（对标 mouseGrabberItem）。
 * @param      self 目标场景；可为 NULL（返回 NULL）。
 * @return     抓取项借用指针；无抓取返回 NULL。
 */
XGraphicsItem* XGraphicsScene_mouseGrabberItem(const XGraphicsScene* self);

/**
 * @brief      悬停移动（对标 QGraphicsScenePrivate::dispatchHoverEvent 的
 *             单项承载）。
 * @details    命中自顶向下第一个 acceptHoverEvents 的可见项；与
 *             m_lastHoverItem 不同 ⇒ 旧项收 GRAPHICS_SCENE_HOVER_LEAVE、
 *             新项收 GRAPHICS_SCENE_HOVER_ENTER（XEnterEvent 承载：
 *             m_position=项局部坐标、m_scenePosition=场景坐标）；随后
 *             当前项收 GRAPHICS_SCENE_HOVER_MOVE。悬停事件只有
 *             setAcceptHoverEvents(true) 的项收到（Qt 同口径）。
 * @param      self 目标场景；可为 NULL（返回 false）。
 * @param      scenePos 场景坐标点；可为 NULL（返回 false）。
 * @return     有悬停目标时返回 true（Qt dispatchHoverEvent 返回口径）。
 */
bool XGraphicsScene_hoverMoveEvent(XGraphicsScene* self, const XPointF* scenePos);

/**
 * @brief      悬停离开（对标 QGraphicsScenePrivate::leaveScene 的单项
 *             承载：指针离开视口 ⇒ 最近悬停项收 GRAPHICS_SCENE_HOVER_
 *             LEAVE 并清跟踪）。
 * @param      self 目标场景；可为 NULL（空操作）。
 */
void XGraphicsScene_hoverLeaveEvent(XGraphicsScene* self);

/**
 * @brief      按键按下（对标 keyPressEvent）。
 * @details    直送当前焦点项；项缺省忽略 ⇒ 沿父链上溯（Qt 原文 do-while
 *             传播：accept 置位→派发→!accepted 且有父则继续）。
 * @param      self 目标场景；可为 NULL（空操作）。
 * @param      event 按键事件（借用）；不可为 NULL（空操作）。
 */
void XGraphicsScene_keyPressEvent(XGraphicsScene* self, XKeyEvent* event);

/**
 * @brief      按键松开（对标 keyReleaseEvent；传播规则同 keyPressEvent）。
 * @param      self 目标场景；可为 NULL（空操作）。
 * @param      event 按键事件（借用）；不可为 NULL。
 */
void XGraphicsScene_keyReleaseEvent(XGraphicsScene* self, XKeyEvent* event);

/**
 * @brief      上下文菜单（对标 contextMenuEvent：命中候选降序逐个派发
 *             XEVENT_TYPE_GRAPHICS_SCENE_CONTEXT_MENU（复用 XMouseEvent
 *             承载，右键语义由调用方置 m_button），accepted 即止；
 *             无人接受保持忽略）。
 * @param      self 目标场景；可为 NULL（空操作）。
 * @param      event 鼠标事件（借用；m_position=场景坐标）；不可为 NULL。
 */
void XGraphicsScene_contextMenuEvent(XGraphicsScene* self, XMouseEvent* event);

/**
 * @brief      滚轮（对标 wheelEvent：命中候选降序逐个派发
 *             XEVENT_TYPE_GRAPHICS_SCENE_WHEEL，accepted 即止；无人接受
 *             保持忽略。Qt 的 WheelFocus 置焦支属 QGraphicsWidget 子集，
 *             本轮不登）。
 * @param      self 目标场景；可为 NULL（空操作）。
 * @param      event 滚轮事件（借用；m_position=场景坐标）；不可为 NULL。
 */
#if XWINDOWEVENT_ON
void XGraphicsScene_wheelEvent(XGraphicsScene* self, XWheelEvent* event);
#endif

/* ==================== 渲染与脏区 ==================== */

/**
 * @brief      渲染场景到绘制器（对标 render(painter, target, source=
 *             target) 的 1:1 子集：无 aspectRatio 缩放、无背景/前景笔刷）。
 * @details    入口 painter->save、出口 restore，target 非空时先
 *             setClipRect(target, IntersectClip) 再 compose 世界变换——
 *             对标 Qt render 的 save/restore（qgraphicsscene.cpp:1784/
 *             1826）与 targetRect 裁剪（:1786，设备坐标）【复核修订
 *             2026-11-16】：调用方传入的 painter 状态不被本函数污染，
 *             target 外区域不落像素。target=NULL 渲染整个 sceneRect()
 *             （无裁剪——设备矩形未给）。逐项口径（Qt
 *             drawItems/drawItemHelper 同）：**堆叠升序**（含子项）、
 *             隐藏与全透明剔除、包围盒与区域相交预筛，对每项
 *             setWorldTransform(sceneTransform ∘ 已有变换) +
 *             setOpacity(effectiveOpacity) + XGraphicsItem_paint_base。
 *             target 语义（1:1 子集下唯一自洽口径）：target **兼任场景
 *             源区域与设备目标区域**（同位渲染，无缩放；世界变换=单位
 *             阵）；target=NULL 渲染整个 sceneRect() 到设备原点起。
 *             【W4】挂**启用效果**的项另走效果分支：整棵子树先离屏渲染到
 *             ARGB32_Premultiplied 位图（画布坐标 = 项本地坐标平移），经
 *             XGraphicsEffect_drawToContext 派发效果 Draw，再把输出画布
 *             回贴到场景（画布原点平移 ∘ 外层变换，dpr 由绘制器末端折入）；
 *             祖先挂了启用效果的子项**不再单独绘制**（对标 Qt drawItems 只
 *             遍历顶层项、由 drawSubtreeRecursive 递归子树，
 *             qgraphicsscene.cpp:4618-4623）；无效果项走原路径，逐像素不变。
 *             包围盒收缩到零面积的效果（如 blurRadius 极负）整块不落像素，
 *             与 Qt 同口径。
 * @param      self 目标场景；可为 NULL（空操作）。
 * @param      painter 绘制器；不可为 NULL（空操作）。调用方 painter 的
 *             状态（变换/裁剪/透明度）在返回后原样保留。
 * @param      target 场景坐标渲染区域；为 NULL 渲染整个场景矩形。
 */
void XGraphicsScene_render(XGraphicsScene* self, XPainter* painter,
                           const XRectF* target);

/**
 * @brief      全场景标脏（对标 update(QRectF())；只登记，帧合成属 W3/W4）。
 * @param      self 目标场景；可为 NULL（空操作）。
 */
void XGraphicsScene_update(XGraphicsScene* self);

/**
 * @brief      区域标脏（对标 update(rect)；累计进 m_dirtyRect，只登记）。
 * @param      self 目标场景；可为 NULL（空操作）。
 * @param      rect 场景坐标区域；为 NULL 等价全场景标脏。
 */
void XGraphicsScene_updateRect(XGraphicsScene* self, const XRectF* rect);

/**
 * @brief      是否有待合成脏区（update/updateRect 之后为真）。
 * @param      self 目标场景；可为 NULL（返回 false）。
 * @return     m_dirtyAll 或 m_dirtyRect 非空返回 true。
 */
bool XGraphicsScene_isDirty(const XGraphicsScene* self);

/**
 * @brief      脏区累计矩形（借用指针，随场景存亡）。
 * @details    全场景脏时返回场景矩形（未设置时 itemsBoundingRect）；
 *             区域脏返回累计矩形；无脏区返回 NULL。render 后可调用
 *             XGraphicsScene_clearDirty 复位。
 * @param      self 目标场景；可为 NULL（返回 NULL）。
 * @return     脏矩形借用指针；无脏区返回 NULL。
 */
const XRectF* XGraphicsScene_dirtyRect_const(const XGraphicsScene* self);

/**
 * @brief      清除脏区登记（帧合成方消费后复位；对标视图侧消费 dirty）。
 * @param      self 目标场景；可为 NULL（空操作）。
 */
void XGraphicsScene_clearDirty(XGraphicsScene* self);

/* ==================== 内部协作入口（仅供 XGraphicsItem 拆卸路径） ========== */

/**
 * @brief      从场景注销项（**内部协作入口**；对标 removeItemHelper 的
 *             inDestructor 分支——~QGraphicsItem 经它摘离场景）。
 * @warning    仅限 XGraphicsItem_deinit 与 XGraphicsScene 模块内部调用：
 *             不派发 ItemSceneChange/HasChanged（Qt 析构路径同样不派发）、
 *             不删 item、不从父项摘离（析构级联已保证）；清理场景对
 *             该项（及仍挂在其子树内的节点）的焦点/抓取/悬停/拖动引用
 *             与顶层登记。item 未属本场景时空操作。
 * @param      scene 目标场景；可为 NULL（空操作）。
 * @param      item 目标项；可为 NULL（空操作）。
 */
void XGraphicsScene_removeItemInternal(XGraphicsScene* scene,
                                       XGraphicsItem* item);

/**
 * @brief      单项选中变化后的信号收口（**内部协作入口**；对标
 *             QGraphicsItem::setSelected 中「!selectionChanging 即发
 *             selectionChanged」的闸位判断）。
 * @warning    仅限 XGraphicsItem_setSelected 调用：批量闸内静默（Qt
 *             同口径），闸外且确有变化才发 selectionChanged。
 * @param      scene 目标场景；可为 NULL（空操作）。
 * @param      item 发生选中变化的项；可为 NULL（空操作）。
 */
void XGraphicsScene_itemSelectedChangedInternal(XGraphicsScene* scene,
                                                XGraphicsItem* item);

#endif /* XPAINTER_PATH_ON */

#endif /* XGUI_ON */

#ifdef __cplusplus
}
#endif

#endif /* XGRAPHICSSCENE_H */
