/******************************************************************************
 * @file       XPlatformOrientation_android.c
 * @brief      XPlatformOrientation 契约的安卓实现（委托系统转屏）。
 * @details    安卓的显示方向由系统掌握，应用的正规入口只有一个：
 *             Activity.setRequestedOrientation（清单 android:screenOrientation
 *             只给初值，运行期改向必须走这个 API）。系统采纳后重建
 *             surface，应用侧表现为收到一次 resize——native 侧**不自行
 *             转任何像素**：逐像素转置是「画布旋转」（框架的
 *             XWindow_setContentRotation），与本契约正交，两者是两件事。
 *
 *             能力诚实原则：
 *             - Activity 未就绪 → 能力位 0（调用方按不支持退化）。
 *             - 系统会忽略方向请求的大屏场景（Android 16 起大屏一律
 *               忽略；Android 12 起忽略 targetSdk>=31 应用）→ 不报
 *               Request 能力，界面据此置灰，而不是让用户点了没反应。
 * @note       平台 API 只出现在 Drive；JNI 通道经 XAndroidJniChannels.h
 *             窄接口（业务层不得直呼）。
 * @author     XinYueC 团队
 ******************************************************************************/
#include "XPlatformOrientation.h"

#if defined(__ANDROID__) && XGUI_ON

#include "XAndroidJniChannels.h"

unsigned XPlatformOrientationDriver_capabilities(void)
{
    unsigned caps = 0u;
    if (!XAndroid_hasActivity()) return 0u;
    /* 「申请指定方向」：系统不采纳时（见 canRequestOrientation）不报
     * 该能力——能力位=「本平台做不到」，界面据此置灰。 */
    if (XAndroid_canRequestOrientation())
        caps |= (unsigned)XPlatformOrientationCap_Request;
    /* 「跟随系统」：UNSPECIFIED 是安卓的原生语义（交回系统按传感器/
     * 用户设置决定），恒可用。 */
    caps |= (unsigned)XPlatformOrientationCap_Auto;
    /* 「回读当前实际方向」：Display 读数恒可取（取不到时返回 -1）。 */
    caps |= (unsigned)XPlatformOrientationCap_Query;
    return caps;
}

bool XPlatformOrientationDriver_setOrientation(int degrees)
{
    return XAndroid_setRequestedOrientation(degrees);
}

int XPlatformOrientationDriver_currentOrientation(void)
{
    return XAndroid_getDisplayOrientation();
}

#endif /* defined(__ANDROID__) && XGUI_ON */
