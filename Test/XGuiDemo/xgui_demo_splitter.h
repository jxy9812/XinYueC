/* xgui_demo_splitter.h —— XGuiDemo 通用分割条（demo 内部共享控件）。
 *
 * 可自绘、可拖拽的布局分割条：拖动回调受控尺寸，双击收起，收起态
 * 单击展开。导航面板（xgui_window_demo.c）与远程客户端页
 * （xgui_demo_page_remote_client.c）共用。
 *
 * 口径：
 *  - 受控尺寸的语义由使用方裁定（左/右停靠=宽度、上/下=高度、
 *    RC 控制列=列宽），分割条只上报「按下时基准尺寸 + 拖拽增量」，
 *    方向符号按 edge 折算（0左 1右 2上 3下：左/上拖出为正增量）；
 *  - 拖拽经 XWidget_grabMouse 全程接管（光标出条不丢move）；
 *  - 光标 SplitH/SplitV 随 edge 自动设置（XCURSOR_ON=0 时省略）；
 *  - 非阻塞：所有回调直发，回调方自行触发重排（adapt/layout）。
 */

#ifndef XGUI_DEMO_SPLITTER_H
#define XGUI_DEMO_SPLITTER_H

#include "XWidget.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief 分割条交互回调（全部借用，直发同栈）。 */
typedef struct DemoSplitterCallbacks
{
    int (*sizeFor)(void* owner);             /**< 查询当前受控尺寸。 */
    void (*applySize)(void* owner, int size);/**< 拖拽应用新尺寸（使用方钳位+重排）。 */
    void (*toggleCollapse)(void* owner);     /**< 双击收起 / 收起态单击展开。 */
} DemoSplitterCallbacks;

/** @brief 创建分割条（堆对象，父子链级联析构）。
 *  @param parent   父控件（建议=顶层内容宿主，与面板同级）。
 *  @param edge     所属停靠边 0=左 1=右 2=上 3=下（决定条向/光标/增量符号）。
 *  @param cbs      回调表（借用，须全非空）。
 *  @param owner    回调上下文（借用）。 */
XWidget* DemoSplitter_create_ex(int memoryType, XWidget* parent, int edge,
                                const DemoSplitterCallbacks* cbs, void* owner);

/** @brief 同步状态（重绘条向/行为/光标相关呈现；几何由使用方 set）。 */
void DemoSplitter_setState(XWidget* self, int edge, bool collapsed);

#ifdef __cplusplus
}
#endif

#endif /* XGUI_DEMO_SPLITTER_H */
