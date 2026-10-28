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
 * 懒分配契约（[全量懒分配 2026-10-19]）：
 *  - build 不再在主窗口创建期调用——注册期主文件只建空占位壳写入
 *    m_extPages 槽位（保「堆叠下标=页索引」与窗口几何分支口径），真页
 *    根推迟到首显，由主文件 demo_ext_ensureBuilt 查文件级构建注册表
 *    g_extBuilders 同位换入；
 *  - 「首显」入口三族：demo_switchPage（用户/CLI --page/autotest 切
 *    页）、内建 autotest 效果页留证段、CLI autostart 豁免（远程双页在
 *    --remote-server/--remote-client 预置时事件循环前真建，否则页内
 *    autostart 守卫 m_ready 恒假=被控端静默不监听）；
 *  - 常驻不销毁：ensureBuilt 幂等（已建/已尝试即短路），页活到进程退
 *    出——销毁语义与现状一致，各页既有 deleteLater/断链退出清理定式
 *    不变；
 *  - build 时机从「窗口创建期」推迟到「首显期」，页内 build 不得依赖
 *    「其余页已建」的顺序假设（各页 static 自持结构本就互不引用，
 *    2026-10-19 全量扫描确认无跨页耦合）。
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

/** @brief 构建网络设置页（系统设置：枚举所有网卡/查看 IPv4 配置/
 *         DHCP↔静态模式切换/静态 IP·掩码·网关·DNS 应用）。
 *         枚举走 XNetworkInterface；应用走 XNetworkInterface_set*Mode
 *         （Windows=netsh 通路，需管理员权限；其余平台返回 NULL 或
 *         应用时如实提示不支持，主文件跳过注册或禁用）。 */
XWidget* demo_page_network_build(XWidget* parent,
                                 DemoPageStatusFn status, void* user);

/** @brief 网络设置页自测（非阻塞；绝不真实下发配置——只断言枚举/
 *         填充/非法输入拒应用门控）。 */
int demo_page_network_autotest(XWidget* page);

/** @brief 网络设置页自适应重排（信息区伸缩 + 状态行贴底；同上契约）。 */
void demo_page_network_adapt(XWidget* page);

/** @brief 网络设置页重新可见钩子（切回本页时主文件调用）：无线会话
 *         未开时重试一次并恢复入口（构建期平台瞬态失败置灰后的恢复口；
 *         已就绪时零开销返回）。 */
void demo_page_network_resume(void);

/** @brief 构建悬浮窗设置页（系统设置：性能悬浮层显示数据行/内存行
 *         显示与来源/九宫格预设位置/固定与拖动/字号/自适应尺寸，
 *         改动即时生效）。XGUI_PERFORMANCE_OVERLAY_ON=0 或控件裁剪
 *         时返回 NULL，主文件跳过注册。 */
XWidget* demo_page_overlay_settings_build(XWidget* parent,
                                          DemoPageStatusFn status, void* user);

/** @brief 悬浮窗设置页自测（非阻塞；逐项切换并断言悬浮层 getter，
 *         结束恢复默认）。 */
int demo_page_overlay_settings_autotest(XWidget* page);

/** @brief 悬浮窗设置页自适应重排（说明/状态行贴底；同上契约）。 */
void demo_page_overlay_settings_adapt(XWidget* page);

/** @brief 构建二维码演示页（XRcode 接入：内容输入 → 编码 → 预览/
 *         中心内嵌图/保存 PNG；2026-10-08 追加第 11 扩展页，页索引
 *         15）。控件或图像模块被裁剪时返回 NULL，主文件跳过注册。 */
XWidget* demo_page_qrcode_build(XWidget* parent,
                                DemoPageStatusFn status, void* user);

/** @brief 二维码演示页自测（非阻塞；装配/编码/内嵌图版本抬升/空输入
 *         拒编码门控；不触发模态文件对话框）。 */
int demo_page_qrcode_autotest(XWidget* page);

/** @brief 二维码演示页自适应重排（预览标签位置；同上契约）。 */
void demo_page_qrcode_adapt(XWidget* page);
/** @brief 构建软件更新页（系统设置：关于→软件更新：当前组件版本列表
 *         app/fontres、手动检查、策略显示、下载/校验/解包/激活四段进度
 *         条、重启与回滚提示；纯观察者+用户决策入口，引擎逻辑零复制）。
 *         引擎经 XUpgrade_init 懒启动（首次决策入口点击，autoCheck=
 *         false；工作区在系统临时目录 xgui_demo_update，不写 demo 资源
 *         /安装目录）。XUPGRADE_ON=0 或控件裁剪时返回 NULL，主文件跳过
 *         注册（2026-10-07 追加）。 */
XWidget* demo_page_update_build(XWidget* parent,
                                DemoPageStatusFn status, void* user);

/** @brief 软件更新页自测（非阻塞；不发起真实网络检查——事件路径经真实
 *         listener 入队 + 泵出回调验证，懒启动走真实 XUpgrade_init）。 */
int demo_page_update_autotest(XWidget* page);

/** @brief 软件更新页自适应重排（信息列/进度条随根宽收放，提示/状态行
 *         贴底；同上契约）。 */
void demo_page_update_adapt(XWidget* page);

/** @brief 软件更新页退出清理：停 UI 泵 → XUpgrade_deinit → 回收队列互
 *        斥（须在主窗口析构前调用；页面未构建/引擎未启动时零操作）。 */
void demo_page_update_shutdown(void);

/** @brief 构建 Modbus 主站调试页（TCP/串口 RTU 双通道连接、从站地址、
 *         远程 IO 模块适配[输入/输出点数]、轮询引擎与 IO 点位网格）。
 *         XModbus 模块或控件裁剪时返回 NULL（2026-10-12 追加，
 *         2026-10-13 双页拆分改两函数）。 */
XWidget* demo_page_modbus_master_build(XWidget* parent,
                                       DemoPageStatusFn status, void* user);

/** @brief Modbus 主站页自测（非阻塞；不发起真实网络/串口请求，验控件
 *         存在、构建后未连接、标签-输入框几何不相交）。 */
int demo_page_modbus_master_autotest(XWidget* page);

/** @brief Modbus 主站页自适应重排（绝对定位布局，当前无自适应需求）。 */
void demo_page_modbus_master_adapt(XWidget* page);

/** @brief 构建 Modbus 从站调试页（TCP 监听/串口 RTU、本站地址、点数
 *         配置、启动/停止）。数据区为**四块面板同屏常驻**：DI/DO 为自绘
 *         灯格（MbSwLamp，圆圈+编号，单击即写数据区，无主站亦可单机调）；
 *         IR/HR 为双 XTableWidget 行内编辑（回车/失焦提交）。每块面板的
 *         内容在自身空间不足时由该面板自带滚动条承载，面板本身永不被
 *         裁切、永不隐藏。2026-10-18 四区同屏重构（原单表 + DO/DI/IR/HR
 *         下拉互斥切换）。 */
XWidget* demo_page_modbus_slave_build(XWidget* parent,
                                      DemoPageStatusFn status, void* user);

/** @brief Modbus 从站页自测（非阻塞；验四区存在与挂载链、滚动壳固定
 *         尺寸、两表装配且互相独立、灯格已建、数据宿主就绪（server
 *         常驻）但未监听、标签-输入框几何不相交）。 */
int demo_page_modbus_slave_autotest(XWidget* page);

/** @brief Modbus 从站页自适应重排：视口宽不足以并排放下「两灯格区 +
 *         两寄存器表」时切紧凑档（纵向堆叠），否则宽档（灯格列 +
 *         寄存器列横排，IR/HR 并排）；转发页内 mb_slaveSyncViewport
 *         （与 build 末尾、页根 resizeEvent 构成三个幂等入口）。 */
void demo_page_modbus_slave_adapt(XWidget* page);

/** @brief 构建窗口设置页（系统设置：皮肤/边框(标题栏)/状态栏/最大化/
 *         全屏/窗口方向/鼠标光标显示/屏幕休眠(熄屏与系统休眠超时) +
 *         复位默认；改动即时生效）。控件或样式/
 *         调色板段被裁剪时返回 NULL，主文件跳过注册（2026-10-17 追加）。 */
XWidget* demo_page_window_settings_build(XWidget* parent,
                                         DemoPageStatusFn status, void* user);

/** @brief 窗口设置页自测（非阻塞；逐项切换并断言框架 getter 回读，
 *         结束恢复默认态）。 */
int demo_page_window_settings_autotest(XWidget* page);

/** @brief 窗口设置页自适应重排（说明/状态行贴底；同上契约）。 */
void demo_page_window_settings_adapt(XWidget* page);

/** @brief 窗口状态改变推入（主窗 changeEvent 收到 WINDOW_STATE_CHANGE
 *         时调用；整页控件回读主窗当前态）。未 build 时空操作。
 *         [2026-10-28 对齐 Qt] 全路径状态变化事件最终送达顶层控件
 *         changeEvent，页面由此自动修正「全屏显示」等复选框回读。 */
void demo_page_window_settings_syncFromWindow(void);

/** @brief 构建时钟与 DDNS 页（系统设置：NTP 应用内校时[服务器/立即校时/
 *         自动校时/偏移时延状态] + 动态域名解析[阿里云凭据/域名记录/
 *         IPv6 开关/立即检查/自动检查/当前与推送 IP 状态]）。
 *         XNTP_ON 与 XDDNS_ON 均关闭时返回 NULL，主文件跳过注册
 *         （2026-10-16 追加页 19）。 */
XWidget* demo_page_timeddns_build(XWidget* parent,
                                  DemoPageStatusFn status, void* user);

/** @brief 时钟与 DDNS 页自测（非阻塞；不触真实网络——NTP 清服务器触发
 *         NoServer、DDNS 无凭据触发 NoConfig，均即时失败路径）。 */
int demo_page_timeddns_autotest(XWidget* page);

/** @brief 时钟与 DDNS 页自适应重排（两状态区随根高伸缩；同上契约）。 */
void demo_page_timeddns_adapt(XWidget* page);

/**
 * @brief 主窗口性能悬浮层访问器（悬浮窗设置页专用；主文件实现）。
 * @param user build 回调透传的主窗口指针（DemoWin*）。
 * @return 性能悬浮层控件借用指针；XGUI_PERFORMANCE_OVERLAY_ON=0 或
 *         未初始化时 NULL。返回 void* 避免本头引入悬浮层头文件。
 */
void* demo_main_overlay(void* user);

/**
 * @brief 主窗口性能悬浮层九宫格预设位置（悬浮窗设置页专用；主文件实现）。
 * @param user build 回调透传的主窗口指针（DemoWin*）。
 * @param preset XPerformanceOverlayPosition 枚举值（0..8；传 int 免引入
 *               悬浮层头文件）。
 * @details 主文件据此挂起/恢复「resize 自动重锚右下角」行为：preset
 *          为 BottomRight 恢复自动重锚，其余预设挂起（用户显式定位
 *          优先，窗口缩放不抢位置）。
 */
void demo_main_overlay_applyPreset(void* user, int preset);

/**
 * @brief 主窗口顶层控件访问器（窗口设置页专用；主文件实现）。
 * @param user build 回调透传的主窗口指针（DemoWin*）。
 * @return 顶层控件（DemoWin.m_base）借用指针；未就绪时 NULL。
 */
void* demo_main_toplevel(void* user);

/**
 * @brief 主窗口底部状态栏显隐（窗口设置页专用；主文件实现）。
 * @details 显隐同步收放内容区底部 26px 预留并重排悬浮层锚定。
 * @return 切换后状态栏是否可见。
 */
bool demo_main_setStatusBarVisible(void* user, bool visible);

/** @brief 查询主窗口底部状态栏当前可见性（窗口设置页自测断言用）。 */
bool demo_main_statusBarVisible(void* user);

/**
 * @brief 静态场景缓存失效并整窗重绘（窗口设置页皮肤切换专用；主文件实现）。
 * @details 皮肤换表+方案联动只影响后续绘制：主窗静态基底（窗口背景/
 *          标题栏带）带缓存，必须显式失效重建才能翻色。
 */
void demo_main_sceneInvalidate(void* user);

/**
 * @brief 主窗 chrome 全量重排（窗口设置页边框开关专用；主文件实现）。
 * @details 边框动态开关只经框架 setFrameMargins 收放 CSD 保留边距，
 *          demo 蓝条文字几何/内容区避让/静态基底不会自发重排——本
 *          访问器一条龙：悬浮层重锚 + chrome/内容区重排 + 静态场景
 *          失效 + 整窗重绘。状态栏显隐与边框开关共用。
 */
void demo_main_refreshChrome(void* user);

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
/**
 * @brief      键盘页无头自动化请求查询（[懒分配 2026-10-19] 新增豁免
 *             契约；其余页面无此契约）。
 * @details    XGUI_KB_* 环境族（见 headless_hook 注）任一非空即返回真：
 *             懒分配下页 9 首显才真建，主文件须在调 headless_hook 前
 *             据此 ensureBuilt（钩子首行 s_kbd.page 判空，未建时整体零
 *             操作=风格检查动态层全空）。环境族全缺省返回假——维持全
 *             量懒分配口径，常态零开销。开关裁剪时恒假。
 * @return     是否请求键盘页无头自动化（需先真建页面）。
 */
bool demo_page_keyboard_headless_requested(void);

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

/**
 * @brief 构建 ATV61 变频器页（Modbus RTU 主站面板：串口名/波特率/从站
 *        地址 + 启停控制（正转启动/反转启动/受控停机/自由停车/快速停机/
 *        故障复位）+ 频率给定 + 监视/报警区）。Modbus 模块或控件裁剪时
 *        返回 NULL（主文件跳过注册）。
 */
XWidget* demo_page_atv61_build(XWidget* parent,
                               DemoPageStatusFn status, void* user);

/**
 * @brief ATV61 变频器页自测（非阻塞：验控件存在、默认波特率 38400、
 *        构建后未连接、故障码表映射）。
 */
int demo_page_atv61_autotest(XWidget* page);

/**
 * @brief ATV61 变频器页自适应重排（盒式布局自适应，无需手工重排）。
 */
void demo_page_atv61_adapt(XWidget* page);

#ifdef __cplusplus
}
#endif

#endif /* XGUI_DEMO_PAGES_H */
