/******************************************************************************
 * @file       XHeaderView_Protected.h
 * @brief      XHeaderView 保护接口（仅供宿主视图/内部实现/对齐回归锁使用）。
 * @details    本文件声明 Qt QHeaderView 中不进公开头文件的内部接线入口：
 *             ① 表头带事件转发的移动段（K-68 拖拽换位的手势驱动入口，
 *                与 XHeaderView.h 公开的 forwardPress/forwardRelease/
 *                forwardDoubleClick 同族）；② 手势活动查询（宿主视图
 *                在表头带外转发 release/move 的门）；③ 按压段查询
 *                （K-69 highlightSections 绘制路径的 State_Sunken 依据，
 *                对标 Qt QHeaderViewPrivate::pressed，qheaderview.cpp:
 *                2870-2872）；④ stretchLastSection 的视口可用长推送
 *                （K-73；Qt 中表头经 QAbstractScrollArea 基类自持视口、
 *                Resize 事件即重排，qheaderview.cpp:2817-2827——本表头
 *                为隐藏子件、视口在表格侧，以推送承载同一事实）；
 *                ⑤ 视口长变化检测/重推（K-73a：宿主在滚动条显隐等
 *                无表格 resize 的视口几何变化点挂同值闸入口，仅长度
 *                真变时重排——对标 Qt 表头 Resize 事件即 resizeSections
 *                的几何承接口径）；⑥ 末段快照只读（K-68 残留 c 的
 *                qMax 下限事实源）。
 *             2026-11-15 §B3：setStretchViewportLength 落值时**同时**
 *             把同一内容区可用长写入 m_offsetViewportLen，供 K-64 的
 *             setOffsetToLastSection（length − 视口宽）消费；宿主无需
 *             再推第二份。新增的两个 protected 虚槽
 *             （PaintSection/SectionSizeFromContents）按 _base 契约暴露
 *             在公开头 XHeaderView.h，经 EXHeaderView_* 槽分派。
 *             普通用户代码不应直接包含或调用；XHeaderView.h 只保留
 *             公开 API。
 * @author     XinYueC 团队
 ******************************************************************************/
#ifndef XHEADERVIEW_PROTECTED_H
#define XHEADERVIEW_PROTECTED_H

#if XGUI_ON
#ifdef __cplusplus
extern "C" {
#endif

#include "XHeaderView.h"

#if XWIDGET_ON && XTABLEWIDGET_ON

/**
 * @brief 把表头带内的鼠标**移动**事件转发给表头（K-68 2026-11-14）。
 *
 *        与公开的 forwardPress/forwardRelease/forwardDoubleClick 同族：
 *        调用方（XTableWidget 表头带转发）负责命中判定与**坐标换算**
 *        （x 扣除行号列宽），本函数直呼表头自身的鼠标移动虚槽——
 *        与真实事件同一套语义，无第二套判定。
 *
 *        @note 转发承载三件事（对标 Qt QHeaderView::mouseMoveEvent，
 *        qheaderview.cpp:2550-2662）：悬停段进入发射 sectionEntered；
 *        分隔线手柄光标提示；**MoveSection 拖拽换位手势**的移动段
 *        （sectionsMovable 门控下按拖动阈值机算目标视觉序）。
 *
 * @param self 目标表头；NULL 忽略。
 * @param event 已换算成表头局部坐标的 XEVENT_TYPE_MOUSE_MOVE 事件
 *              （借用；NULL 忽略）。
 * @return 无返回值。
 */
void XHeaderView_forwardMove(XHeaderView* self, XEvent* event);

/**
 * @brief 查询表头是否有进行中的拖拽手势（K-68 2026-11-14）。
 *
 *        覆盖两类手势：分隔线拖拽调宽（第八轮②）与段拖拽换位
 *        （MoveSection）。宿主视图在**表头带外**（y >= headerHeight）
 *        收到 release/move 时，凭本查询决定是否仍向表头转发——对标
 *        Qt 里表头 grabMouse 后事件直达表头、与光标位置无关的语义
 *        （本表头是隐藏子件、grab 无效，转发由视图承担）。
 *
 * @param self 目标表头；NULL 返回 false。
 * @return 调宽或换位手势进行中返回 true。
 */
bool XHeaderView_gestureActive(const XHeaderView* self);

/**
 * @brief 查询当前按压段（对标 QHeaderViewPrivate::pressed；K-69）。
 *
 *        绘制路径按 Qt initStyleOptionForIndex 的门（qheaderview.cpp:
 *        2868-2872）用它在按压段上补 State_Sunken（clickable 开启时）。
 *        返回值即按压链记录的段号（xhv_pressMap 旁表承载）。
 *
 * @param self 目标表头；NULL 或无按压记录返回 -1。
 * @return 当前按压段逻辑号（-1=无）。
 */
int XHeaderView_pressedSection(const XHeaderView* self);

/**
 * @brief 推送表头内容区的可用长（K-73 stretchLastSection；框架内部）。
 *
 *        对标 Qt：QHeaderView 继承 QAbstractScrollArea、自持视口，
 *        QTableView::updateGeometries 把表头几何摆到视口宽
 *        （qtableview.cpp:2302-2309），表头 Resize 事件即
 *        resizeSections() 重排（qheaderview.cpp:2817-2827）——
 *        stretchLastSection 的「末段 = 视口长 − 其余可见段合计」
 *        因此生效。本表头为隐藏子件（表头带由表格自绘），视口在
 *        宿主侧：宿主视图在尺寸变化（resizeEvent/行号列显隐）时把
 *        **列布局可用长**（视口宽 − 行号列宽）推送进来；>= 0 时引擎
 *        即刻按当前末段重排，< 0（未推送）引擎不跑。
 *
 *        @note 与 Qt 一致：全局 Stretch 模式（m_resizeMode==Stretch，与
 *        视图侧分发器同一判定字段）的池分配在视图侧（等分口径，探针 A），
 *        引擎跳过；非全局模式下按 Qt 延迟重排的**池**口径重排
 *        （qheaderview.cpp:3553-3640）：末段强制视为 Stretch 进池、其它
 *        生效模式为 Stretch 的可见段同池，非池可见段按现宽扣除，余量>0
 *        时 stretchLen = qMax(余量/池成员数, 最小段宽)、余数按视觉序逐池
 *        成员 +1、末段再 qMax 末段快照宽（qheaderview.cpp:3619-3623）；
 *        余量<=0 整体跳过、池成员保现宽（:3591 门，探针 P1-4b）。
 *
 * @param self 目标表头；NULL 忽略。
 * @param length 内容区可用长（像素；<0 视为未推送，引擎停跑）。
 * @return 无返回值。
 */
void XHeaderView_setStretchViewportLength(XHeaderView* self, int length);

/**
 * @brief 视口长变化检测/重推（K-73a 2026-11-14；框架内部入口）。
 *
 *        与 setStretchViewportLength 的差别只在**同值闸**：length 与
 *        当前已推送值相同 ⇒ 不动（不重排），真变（含从未推送 <0 的
 *        首推）⇒ 等价 setStretchViewportLength 落值并即刻重排。
 *        宿主视图把它挂在**无表格 resize 的视口几何变化点**——滚动条
 *        显隐（AsNeeded 阈值跨越）改变视口宽时只有视口自己的 Resize
 *        事件（Qt 口径：QTableView::updateGeometries 以视口几何为事实
 *        源摆表头，qtableview.cpp:2301,2309；表头几何变 ⇒ Resize ⇒
 *        resizeSections，qheaderview.cpp:2817-2827）⇒ 宿主在视口
 *        Resize 处以本入口承载同一事实，重复推送/冗余事件不触发引擎。
 *
 * @param self 目标表头；NULL 忽略。
 * @param length 内容区可用长（像素；<0 视为未推送，引擎停跑）。
 * @return 无返回值。
 */
void XHeaderView_syncViewportLength(XHeaderView* self, int length);

/**
 * @brief 查询 stretchLastSection 末段快照（K-73 引擎态只读；框架内部）。
 *
 *        对标 Qt QHeaderViewPrivate 的 lastSectionLogicalIdx/lastSectionSize
 *        （qheaderview_p.h:70-71「Only trust when we stretch last section」）：
 *        仅 stretchLastSection 开启时有意义——Qt resizeSections 的视觉末段
 *        分配 qMax(stretchSectionLength, lastSectionSize)
 *        （qheaderview.cpp:3619-3623）就以该快照为下限，全局 Stretch 的
 *        视图侧分配器（XTableWidget）需要同一事实源。快照经
 *        setStretchLastSection(hh,true) 的 setNewLastSection 立账
 *        （qheaderview.cpp:3490-3496）、resizeSection 的 K-73 钩维护
 *        （:851-852）。
 *
 * @param self 目标表头；NULL 视为无快照。
 * @param outLogical 输出快照末段逻辑号（可空；无快照置 -1）。
 * @param outSize 输出快照末段宽（可空；无快照置 -1）。
 * @return stretchLastSection 开启且快照逻辑段有效返回 true（否则两输出
 *         均为 -1，调用方按下限不生效处理）。
 */
bool XHeaderView_lastSectionSnapshot(const XHeaderView* self,
                                     int* outLogical, int* outSize);

#endif /* XWIDGET_ON && XTABLEWIDGET_ON */

#ifdef __cplusplus
}
#endif
#endif /* XGUI_ON */

#endif /* XHEADERVIEW_PROTECTED_H */
