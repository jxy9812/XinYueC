/* xgui_demo_pages.h —— XGuiWindowDemo 扩展页面契约（对标 Qt 示例的分页组织）。
 *
 * 每个演示页面一个独立翻译单元（xgui_demo_page_*.c），与主文件
 * xgui_window_demo.c 解耦：主文件只负责导航接线、页面注册与 autotest
 * 调度，页面内部控件装配与自测断言自包含在本文件族内。
 *
 * 所有权约定：
 *  - build 在 parent 下创建并返回页面根控件（堆对象，父子链级联析构，
 *    主文件不单独释放）；
 *  - 页面内部控件指针由各实现文件以 static 结构自持（demo 单实例），
 *    build 时登记，autotest 时使用；
 *  - autotest 入参 page 与登记的根控件不符时返回 -1（防错页调用）。
 *
 * autotest 口径（与主文件 demo_input_autotest 一致）：
 *  - 事件经 XObject_event_base 直发（与真实输入同路径）；
 *  - 断言输出 XPrintf("XGuiAutoTest: [PASS] 中文描述\n") / [FAIL]；
 *  - 返回失败断言数（0=全过），全程非阻塞（不进模态循环/事件等待）。
 */

#ifndef XGUI_DEMO_PAGES_H
#define XGUI_DEMO_PAGES_H

#include "XWidget.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief 页面向主窗口状态栏反馈交互结果的回调（user 为主窗口指针）。 */
typedef void (*DemoPageStatusFn)(void* user, const char* text);

/** @brief 构建条目视图页（XListView/XListWidget/XTreeView/XTreeWidget/
 *         XTableView/XTableWidget/XHeaderView，对标 QListView 族示例）。 */
XWidget* demo_page_views_build(XWidget* parent,
                               DemoPageStatusFn status, void* user);

/** @brief 构建对话框页（XMessageBox/XInputDialog/XFileDialog/XColorDialog/
 *         XProgressDialog/XDialog+XDialogButtonBox，触发按钮 + 非阻塞打开）。 */
XWidget* demo_page_dialogs_build(XWidget* parent,
                                 DemoPageStatusFn status, void* user);

/** @brief 构建高级控件页（XTextEdit 富文本/XCompleter/XKeySequenceEdit/
 *         XShortcut/XSizeGrip/XFocusFrame/XRubberBand/XToolTip/XSplashScreen/
 *         XMainWindow+XDockWidget 独立窗口）。 */
XWidget* demo_page_advanced_build(XWidget* parent,
                                  DemoPageStatusFn status, void* user);

/** @brief 构建图形效果页（XGraphicsOpacityEffect/XGraphicsBlurEffect/
 *         XGraphicsDropShadowEffect 挂样例控件，启用/禁用切换，无效果基线）。 */
XWidget* demo_page_effects_build(XWidget* parent,
                                 DemoPageStatusFn status, void* user);

/** @brief 各页自测：调用前主程序已把该页切为当前页且几何有效。
 *  @return 失败断言数（0=全过；page 不符=-1）。 */
int demo_page_views_autotest(XWidget* page);
int demo_page_dialogs_autotest(XWidget* page);
int demo_page_advanced_autotest(XWidget* page);
int demo_page_effects_autotest(XWidget* page);

#ifdef __cplusplus
}
#endif

#endif /* XGUI_DEMO_PAGES_H */
