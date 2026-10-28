/******************************************************************************
 * @file       XPlatformPower_android.c
 * @brief      XPlatformPower 契约的安卓实现（委托系统「屏幕超时」）。
 * @details    安卓**没有应用主动关屏的原语**：PowerManager.goToSleep 需
 *             DEVICE_POWER（signature|privileged）权限，普通应用拿不到；
 *             DevicePolicyManager.lockNow 是「锁屏」且需设备管理员，语义
 *             也不对。息屏在安卓是**系统行为**——系统按「屏幕超时」设置，
 *             在最后一次用户交互后自动熄屏。
 *
 *             故安卓侧走**委托系统**路径：
 *             - 自动熄屏时长 = 写 Settings.System.SCREEN_OFF_TIMEOUT，
 *               由系统自己计时熄屏（能力位 ScreenTimeout）；
 *             - 应用**不能**主动熄屏（无 ScreenOff 能力位），界面据此
 *               把「立即熄屏」置灰，而不是让用户点了才发现做不到；
 *             - 唤醒同样是系统行为（任意输入即亮屏），应用不能主动点亮
 *               （无 ScreenWake 能力位）。
 *
 *             写系统设置需 WRITE_SETTINGS 特殊权限（用户逐应用授权，
 *             声明权限不等于已授权）；未授权时 putInt 抛 SecurityException，
 *             通道吞掉并报 false，界面如实提示「需授权」。
 * @note       平台 API 只出现在 Drive；JNI 通道经 XAndroidJniChannels.h
 *             窄接口（业务层不得直呼）。
 * @author     XinYueC 团队
 ******************************************************************************/
#include "XPlatformPower.h"

#if defined(__ANDROID__) && XGUI_ON

#include "XAndroidJniChannels.h"

unsigned XPlatformPowerDriver_capabilities(void)
{
    /* 只有「设置自动熄屏时长（委托系统）」一项：安卓无主动关屏/主动
     * 点亮原语，也无整机挂起原语（Doze 由系统管，应用不可强制）。
     * Activity 未就绪时同样返回 0——能力位为 0 即「不具备任何电源管理
     * 能力」，调用方按不支持退化，无需另设 isAvailable 探针。 */
    if (!XAndroid_hasActivity()) return 0u;
    return (unsigned)XPlatformPowerCap_ScreenTimeout;
}

bool XPlatformPowerDriver_setScreenState(XPlatformPowerScreenState state)
{
    (void)state;
    /* 安卓无应用主动关屏原语（goToSleep 需签名权限）——如实报失败，
     * 界面据此禁用「立即熄屏」。 */
    return false;
}

bool XPlatformPowerDriver_setSystemState(XPlatformPowerSystemState state)
{
    (void)state;
    /* 整机挂起（Doze/挂起）由系统电源策略决定，应用不可强制。 */
    return false;
}

bool XPlatformPowerDriver_setScreenTimeout(int64_t millis)
{
    return XAndroid_setScreenOffTimeout((int)millis);
}

bool XPlatformPowerDriver_requestScreenTimeoutPermission(void)
{
    /* 引导用户到系统设置页为本应用授予 WRITE_SETTINGS（特殊权限，
     * 清单声明不等于已授权）。已授权时无需再跳，返回 false 让调用方
     * 知道「不必打扰用户」。 */
    if (XAndroid_canWriteSystemSettings()) return false;
    return XAndroid_requestWriteSystemSettings();
}

#endif /* defined(__ANDROID__) && XGUI_ON */
