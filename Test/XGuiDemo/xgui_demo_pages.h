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

/** @brief 构建屏幕虚拟键盘页（XLineEdit 输入 + 底部 XVirtualKeyboard + 自动弹
 *         出；XKEYBOARD_ON=0 时返回 NULL，主文件跳过注册）。 */
XWidget* demo_page_keyboard_build(XWidget* parent,
                                  DemoPageStatusFn status, void* user);

/** @brief 各页自测：调用前主程序已把该页切为当前页且几何有效。
 *  @return 失败断言数（0=全过；page 不符=-1）。 */
int demo_page_views_autotest(XWidget* page);
int demo_page_dialogs_autotest(XWidget* page);
int demo_page_advanced_autotest(XWidget* page);
int demo_page_effects_autotest(XWidget* page);
int demo_page_keyboard_autotest(XWidget* page);

/** @brief 扩展页底部锚定控件自适应（2026-10-03 双行分组导航随附契约）。
 * @details 内容区高度随 CSD 系统栏（无 WM 环境占 30px）与窗口尺寸变
 *          化（800x600 下 WM=472 / CSD=442），按 480/494 设计稿硬编码
 *          的底部控件在 CSD 下会被裁剪。adapt 由主文件
 *          demo_layout_content 在切页/resize/startup 全路径调用，把
 *          各页底部锚定控件按当前根几何贴底重排；page 与登记根不符
 *          时静默返回（与 autotest 的 -1 防错页口径同源，静默即可——
 *          几何重排无需向调用方报错）。 */
void demo_page_views_adapt(XWidget* page);          /**< 条目视图：状态行贴底。 */
void demo_page_advanced_adapt(XWidget* page);       /**< 高级控件：SizeGrip 贴右下角。 */
void demo_page_effects_adapt(XWidget* page);        /**< 图形效果：组③启/禁按钮贴底。 */
void demo_page_remote_client_adapt(XWidget* page);  /**< 远程客户端：统计块贴底。 */

/** @brief 构建远程窗口设置页（XGuiServer 服务开关/端口/档位/TLS/证书/
 *         私钥/认证口令/会话状态；CLI 预置与退出清理见
 *         xgui_demo_page_remote_server.h。XGUI_REMOTE_ON=0 或控件裁剪
 *         时返回 NULL，主文件跳过注册）。 */
XWidget* demo_page_remote_server_build(XWidget* parent,
                                       DemoPageStatusFn status, void* user);

/** @brief 远程窗口设置页自测（非阻塞；监听演练一律端口 0，结束恢复
 *         未监听）。 */
int demo_page_remote_server_autotest(XWidget* page);

/** @brief 构建远程客户端设置页（IP/端口/连接断开/连接状态/实时统计/
 *         档位/TLS/模拟触摸；CLI 预置与退出清理见
 *         xgui_demo_page_remote_client.h。XGUI_REMOTE_ON=0 或控件裁剪
 *         时返回 NULL，主文件跳过注册；2026-10-02 追加）。 */
XWidget* demo_page_remote_client_build(XWidget* parent,
                                       DemoPageStatusFn status, void* user);

/** @brief 远程客户端设置页自测（非阻塞；不做真实网络演练，仅断言
 *         控件装配/预置/门控）。 */
int demo_page_remote_client_autotest(XWidget* page);

/** @brief 键盘页无头截图钩子（Tools/VirtualKeyboard/style_check.py 风格自动化专用；
 *         其余页面无此契约）。环境变量全部缺省时零操作、零开销：
 *   - XGUI_KB_AUTOSHOW=1   聚焦默认编辑框并 XVirtualKeyboard_popup 弹出键盘
 *                          （守护轮询 200ms 在 --screenshot 3 帧内到
 *                          不了，无头截图须显式弹出；真机弹收主判据
 *                          已改按下位置驱动 notifyPress——本钩子直呼
 *                          popup 契约不变，XGUI_KB_CLOSE=1 收层后无
 *                          PRESS/焦点边沿不重弹，新语义下场景全兼容）；
 *   - XGUI_KB_MODE=textlower 弹出后切 TextLower 内置布局并开 popovers
 *                          （气泡场景；IME 拼音布局无 POPOVER 位）；
 *   - XGUI_KB_CHINESE=1    切拼音中文态（候选带出现）；
 *   - XGUI_KB_COMPOSE=ni   逐字符直点同名字符键（注入组串/候选）；
 *   - XGUI_KB_PRESS_LABEL=q 令指定 label 键进入按下保持态（直写
 *                          m_pressedKey/m_pressArmed，不注入释放）；
 *   - XGUI_KB_CLOSE=1      弹出后立即 closePopup（收层残板检查）；
 *   - XGUI_KB_DUMP=1       向 stdout 打 XKB-GEO 前缀几何行（控件矩
 *                          形/键位矩形/ctrl/候选带矩形），供脚本与布
 *                          局公式双簿比对。
 *   - XGUI_KB_LAYOUT=t9|english|pinyin
 *                          切换布局款型（拼音九键/英文全键/拼音全键；
 *                          IME 启用且停 User1 槽位时随行落 TextLower
 *                          让款型主表可见）；
 *   - XGUI_KB_PANEL=selector|edit|float
 *                          直点对应工具栏图标（键盘选择面板/文字编辑
 *                          面板/紧凑悬浮小键盘；真实 menuBarHit→图标
 *                          激活路径），并打 XKB-GEO panel 证据行。
 *  调用点=主文件窗口几何定版之后、事件循环之前（弹出几何一次到位，
 *  不依赖定时器边沿）。
 */
void demo_page_keyboard_headless_hook(void);

/**
 * @brief      无头截图覆盖目标（--screenshot 捕获对象重定向查询）。
 * @details    紧凑悬浮态（XGUI_KB_PANEL=float）下键盘是独立顶层 Popup，
 *             不在主窗 paintImage 内——返回键盘控件指针让主文件截取其
 *             自身后备图像（尺寸=紧凑矩形）；其余状态返回 NULL（按主
 *             窗口径截图）。无键盘实例/钩子裁剪时恒 NULL。
 * @return     截图目标控件借用指针；NULL=按主窗口径。
 */
XWidget* demo_page_keyboard_screenshot_target(void);

#ifdef __cplusplus
}
#endif

#endif /* XGUI_DEMO_PAGES_H */
