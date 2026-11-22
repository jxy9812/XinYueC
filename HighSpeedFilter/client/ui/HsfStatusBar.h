/**
 * @file       HsfStatusBar.h
 * @brief      底部黑色状态栏：左状态文本 + 右下角账户信息（高 26 定高）。
 * @details    XLabel 子类自绘黑底——底色自带（不依赖根背景）：中段内容
 *             控件随窗口缩放可能向下越界，状态栏若只靠根背景着色会被
 *             后画内容盖住；配合 raise 后永可见。
 *             右下角固定显示登录账户（「账号·角色」，未登录「未登录」）；
 *             左侧状态文本（HsfStatusBar_setText，存储仍走基类 XLabel）
 *             绘制时按「栏宽 - 账户保留宽」自动省略（ElideRight），窄窗/
 *             竖屏下两段文本互不重叠——账户信息原先在顶部标题栏，2026-11-11
 *             整体搬到本栏（标题栏不再保留账号控件）。
 */
#ifndef HSFSTATUSBAR_H
#define HSFSTATUSBAR_H

#ifdef __cplusplus
extern "C" {
#endif

#include "CXinYueConfig.h"
#include "XWidget.h"
#include "XLabel.h"

#if XWIDGET_ON && XFRAME_ON && XLABEL_ON

/** @brief 状态栏高度（chrome 度量单一事实源：主窗布局/悬浮层锚定共用）。 */
#define HSF_STATUS_BAR_H 26

XCLASS_DEFINE_BEGING(HsfStatusBar)
XCLASS_DEFINE_EXTEND_END(HsfStatusBar, XLabel)

/** @brief 底部黑色状态栏对象；m_base 必须是第一个成员。 */
typedef struct HsfStatusBar
{
    XLabel m_base;              /**< XLabel 基类；必须是第一个成员。 */
    char   m_user[128];         /**< 右下角账户文案（UTF-8；init 清零，RTC1 红线）。 */
} HsfStatusBar;

/** @brief 账户区动作（对齐 Python StatusBar 的 switchUserRequested/
 *         logoutRequested 两信号；点账户区弹菜单后由 HsfStatusBar 回调）。 */
typedef enum HsfStatusBarAccountAction
{
    HsfStatusBarAccount_Logout = 0,   /**< 退出登录。 */
    HsfStatusBarAccount_SwitchUser = 1 /**< 切换账户（重新登录）。 */
} HsfStatusBarAccountAction;

/** @brief 账户菜单动作回调（userData, HsfStatusBarAccountAction）。
 *  @note  控件只发动作不碰业务（对齐 Python：信号由 MainWindow 接）。 */
typedef void (*HsfStatusBarAccountFn)(void* userData, int action);

/**
 * @brief      初始化 HsfStatusBar 类虚函数表。
 * @return     共享 XVtable 指针（调用方不得释放）。
 */
XVtable* HsfStatusBar_class_init(void);

/**
 * @brief      初始化黑色状态栏（高 26 定高、白字、左缩进 16）。
 * @param self 待初始化对象；不可为 NULL。
 * @param parent 父控件（通常为主窗根）。
 * @param flags XLabel 创建标志。
 */
void HsfStatusBar_init(HsfStatusBar* self, XWidget* parent,
                       XWidgetFlags flags);

/**
 * @brief      设置状态文本（UTF-8）并触发重绘。
 * @param self 目标状态栏。
 * @param utf8 状态文本（借用指针）。
 * @note       文本存进基类 XLabel（XLabel_text 可读回原文）；绘制时才按
 *             剩余宽做省略——存储与绘制分离。
 */
void HsfStatusBar_setText(HsfStatusBar* self, const char* utf8);

/**
 * @brief      设置右下角账户文案（「账号·角色」；未登录显示「未登录」）。
 * @param self 目标状态栏。
 * @param user 登录用户名（UTF-8，**借用**）；NULL/空串=未登录 ⇒ 显示「未登录」。
 * @param roleName 角色中文名（HsfLinkClient_roleName()，**借用**）；
 *             NULL/空串时只显示账号本身。
 * @note       文案在本函数内拷进 m_user，调用方无需保活入参；内部做同值
 *             早退（strcmp）——1Hz 兜底节拍重复调用不产生脏区。
 */
void HsfStatusBar_setUser(HsfStatusBar* self, const char* user,
                          const char* roleName);

/**
 * @brief      状态栏字号实时重放（全局字体缩放；chrome 无页归属只乘全局）。
 * @param self 目标状态栏。
 * @param pct  全局缩放百分比（50–200；HsfUi hsfUi_globalFontPct 口径）。
 * @note       重放状态栏字号与定高（源码基准 14px / HSF_STATUS_BAR_H）；
 *           内部做 pct 同值闸——重复触发不标脏（防布局风暴）。
 */
void HsfStatusBar_applyFont(HsfStatusBar* self, int pct);

/**
 * @brief      注册账户区动作回调（点击右下角账户区弹「切换账户/退出登录」
 *             菜单，选中后回调；NULL=只显示不弹菜单）。
 * @param self 目标状态栏。
 * @param fn   动作回调（HsfStatusBarAccountFn）；业务在调用方（HsfUi）。
 * @param userData 回调令牌（借用）。
 * @note       对齐 Python StatusBar 的账户钮（switchUserRequested /
 *             logoutRequested 两信号，控件本身不碰业务逻辑）。
 */
void HsfStatusBar_setAccountAction(HsfStatusBar* self,
                                   HsfStatusBarAccountFn fn, void* userData);

/**
 * @brief      查询某坐标是否落在右下角账户命中区（与绘制公式同源同参）。
 * @param self 目标状态栏。
 * @param x,y  控件本地坐标。
 * @return 命中返回 true。@note 供探针/测试断言与鼠标处理共用一份口径。
 */
bool HsfStatusBar_accountHitTest(const HsfStatusBar* self, int x, int y);

/**
 * @brief      读当前账户显示文案（含「未登录」态；含下拉箭头后缀）。
 * @param self 目标状态栏。@param buf 输出缓冲（UTF-8）。@param cap 容量。
 * @note       对齐 Python `lineEdit().text()` 的只读取文本入口；探针断言用。
 */
void HsfStatusBar_userText(const HsfStatusBar* self, char* buf, int cap);

#endif /* XWIDGET_ON && XFRAME_ON && XLABEL_ON */

#ifdef __cplusplus
}
#endif

#endif /* HSFSTATUSBAR_H */
