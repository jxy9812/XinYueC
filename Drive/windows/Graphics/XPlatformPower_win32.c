/******************************************************************************
 * @file       XPlatformPower_win32.c
 * @brief      XPlatformPower 契约的 Win32 实现（显示器电源管理）。
 * @details    统一平台 API 的 Win32 侧：
 *             - 熄屏/唤醒：向顶层窗口投递 WM_SYSCOMMAND + SC_MONITORPOWER
 *               （2=关闭显示器，1=省电，0=恢复点亮）。消息目标是前台窗口
 *               （GetForegroundWindow），无前台窗口时回退桌面窗口——该消息
 *               由系统 DefWindowProc 处理，与本仓库窗口自绘框架的窗口
 *               消息处理互不干扰。
 *             - 系统挂起：SetSuspendState 需 SE_SHUTDOWN_NAME 权限，普通
 *               桌面进程即使调用也会被系统拒绝，且该动作无法先做无害
 *               探测（调用即真挂起）。故**不声明** SystemSuspend 能力位
 *               ——界面据此把该项置灰，而不是让用户点了才发现失败。
 * @note       平台 API 只出现在 Drive；本文件仅在 _WIN32 下编译出实体，
 *             其他平台整文件为空（Unsupported 哨兵同样让位）。
 * @author     XinYueC 团队
 ******************************************************************************/
#include "XPlatformPower.h"

#if defined(_WIN32) && XGUI_ON

#include <windows.h>

/** @brief 取用于投递 SC_MONITORPOWER 的窗口句柄。 */
static HWND xpow_powerTarget(void)
{
    HWND hwnd = GetForegroundWindow();
    if (!hwnd) hwnd = GetDesktopWindow();
    return hwnd;
}

/** @brief 向目标窗口投递显示器电源命令。 */
static bool xpow_monitorPower(int powerAction)
{
    HWND hwnd = xpow_powerTarget();
    if (!hwnd) return false;
    SendMessage(hwnd, WM_SYSCOMMAND, (WPARAM)SC_MONITORPOWER,
                (LPARAM)powerAction);
    return true;
}

unsigned XPlatformPowerDriver_capabilities(void)
{
    /* 屏幕关/开具备；系统挂起因权限约束不声明（见文件头说明）。 */
    return (unsigned)XPlatformPowerCap_ScreenControl;
}

bool XPlatformPowerDriver_setScreenState(XPlatformPowerScreenState state)
{
    if (state == XPlatformPowerScreenState_Off)
        return xpow_monitorPower(2); /* SC_MONITORPOWER: 2=关闭显示器。 */
    return xpow_monitorPower(0);     /* SC_MONITORPOWER: 0=恢复点亮。 */
}

bool XPlatformPowerDriver_setSystemState(XPlatformPowerSystemState state)
{
    if (state == XPlatformPowerSystemState_Suspend) {
        /* 需 SE_SHUTDOWN_NAME 权限，普通进程必被拒；不做调用（避免误
         * 挂起），由能力位口径保证界面不会提供该入口。 */
        return false;
    }
    return true;
}

bool XPlatformPowerDriver_setScreenTimeout(int64_t millis)
{
    (void)millis;
    /* Win32 侧不支持「委托平台计时」：熄屏是应用自己关屏（SC_MONITOR
     * POWER），时长阈值由公共层回落到本地空闲计时。 */
    return false;
}

bool XPlatformPowerDriver_requestScreenTimeoutPermission(void)
{
    /* Win32 自行关屏计时，无需任何系统授权。 */
    return false;
}

#endif /* defined(_WIN32) && XGUI_ON */
