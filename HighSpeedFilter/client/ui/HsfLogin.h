
/**
 * @file       HsfLogin.h
 * @brief      客户端登录窗口：用户名/密码 + 服务器地址 + 本机快连下拉。
 * @details    主窗之上的**无边框模态**顶层窗口（XWidget 子类）：登录经
 *             HsfLinkClient 的 XLink 内建挑战应答完成——窗口只收集凭据
 *             并等待就绪/失败回调，不做任何本地口令校验。服务器地址可
 *             编辑，下拉框提供「本机服务器」预设（一键回填
 *             127.0.0.1:6100）。地址/端口/用户名经 HsfSettingsStore 记忆
 *             （密码不落盘）。结果经 done 回调上抛（success=true 后调用
 *             方关闭本窗口；单线程事件循环内全程异步等待，不阻塞）。
 *
 *             [主窗先显·模态遮挡] 登录窗**以主窗为父**创建（
 *             HsfLoginWindow_createFor）：①框架首显自动按父窗客户区居中
 *             （xwidget_adjustPositionOnFirstShow / QDialog adjustPosition
 *             同口径）；②建窗时经父链登记原生 transient owner，登录窗
 *             恒在主窗之上；③创建即登记应用模态门，未登录时主窗的
 *             输入事件被平台窗口层吞掉（对标 Qt 模态对话框语义）。
 *             父传 NULL 时回落主屏居中（--login-shot 离屏出图用）。
 */
#ifndef HSFLOGIN_H
#define HSFLOGIN_H

#ifdef __cplusplus
extern "C" {
#endif

#include "CXinYueConfig.h"
#include "XWidget.h"
#include <stdbool.h>

/** @brief 登录结果回调（success=true：已连接并认证通过）。
 *  @param userData 注册令牌；success 登录是否成功；errUtf8 失败原因
 *         （UTF-8 借用；成功时为空串）。 */
typedef void (*HsfLoginDoneFn)(void* userData, bool success, const char* errUtf8);

/**
 * @brief 创建并显示登录窗口（顶层；堆对象）。
 * @param onDone 结果回调（登录成功/最终失败各触发一次；可为 NULL）。
 * @param userData 回调令牌。
 * @return 窗口指针；创建失败返回 NULL。@warning 调用方在登录成功后
 *         XClassDelete（或 deleteLater）回收。
 * @note  必须在 XGuiApplication 创建之后调用。窗口关闭（标题栏 X）按
 *        登录取消处理：回调 success=false 并退出事件循环。
 */
struct HsfLoginWindow;
typedef struct HsfLoginWindow HsfLoginWindow;
/**
 * @brief 创建并显示登录窗口（无父；主屏居中，无模态门）。
 * @param onDone 结果回调（登录成功/最终失败各触发一次；可为 NULL）。
 * @param userData 回调令牌。
 * @return 窗口指针；创建失败返回 NULL。@warning 调用方在登录成功后
 *         XClassDelete（或 deleteLater）回收。
 * @note  必须在 XGuiApplication 创建之后调用。窗口关闭（标题栏 X）按
 *        登录取消处理：回调 success=false 并退出事件循环。
 * @note  产品路径请用 HsfLoginWindow_createFor（带父窗居中 + 模态门）；
 *        本入口供离屏截图（--login-shot）等无主窗场景使用。
 */
HsfLoginWindow* HsfLoginWindow_create(HsfLoginDoneFn onDone, void* userData);

/**
 * @brief 创建并显示登录窗口（以 owner 为主窗：父链居中 + 模态遮挡）。
 * @param owner 主窗顶层控件（登录期间被本窗模态阻塞）；可 NULL
 *              （等价 HsfLoginWindow_create）。
 * @param onDone 结果回调；userData 回调令牌。
 * @return 窗口指针；创建失败返回 NULL。
 * @note  登录窗固定无边框（FramelessWindowHint）：不依赖 WM 装饰，
 *        在框架 CSD / 系统标题栏 / 无 WM（板端 fbdev）三种形态下观感
 *        一致，且不会被父窗原生装饰二次收边。
 */
HsfLoginWindow* HsfLoginWindow_createFor(XWidget* owner,
                                         HsfLoginDoneFn onDone,
                                         void* userData);

/**
 * @brief 武装离屏截图（CI 登录窗出图）：窗口移屏外，延时出 PNG 后退出。
 * @param self 登录窗；path 输出路径（借用）；w/h 窗口尺寸（0=缺省窗尺寸）。
 */
void HsfLoginWindow_armShot(HsfLoginWindow* self, const char* path,
                            int w, int h);

/**
 * @brief 无头登录（CI/联调用；不建窗口）。
 * @param spec "user:pass@host:port"（port 可省略取缺省）。
 * @param onDone 结果回调（必填；成功/失败各触发一次）。
 * @param userData 回调令牌。
 * @return 连接已发起返回 true；spec 非法返回 false。
 * @note  与窗口登录同一 HsfLinkClient 路径；成功后调用方自行建主窗。
 */
bool HsfLogin_startHeadless(const char* spec, HsfLoginDoneFn onDone, void* userData);

#ifdef __cplusplus
}
#endif
#endif /* HSFLOGIN_H */
