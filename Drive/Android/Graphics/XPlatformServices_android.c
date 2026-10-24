/**
 * @file       XPlatformServices_android.c
 * @brief      XPlatformServices 安卓驱动（openUrl 经 JNI ACTION_VIEW）。
 * @details    [2026-10-24 平台补扫] 对标 posix 驱动的 xdg-open 通道：安卓
 *             沙箱无 fork/execp 与桌面壳，系统级"打开 URL"走 Activity 的
 *             startActivity(ACTION_VIEW)。JNI 通道在 XSystemAndroid.c
 *             （XAndroid_openUrl）。isAvailable 以 Activity 引用是否就绪
 *             为准（VM 未注入前如实报不可用）。
 * @note       平台 API 只出现在 Drive；本文件仅在 __ANDROID__ 下编译出实体。
 * @author     XinYueC 团队
 ******************************************************************************/
#include "XPlatformServices.h"
#include "XAndroidJniChannels.h"

#if XPLATFORMINTEGRATION_ON && defined(__ANDROID__)

#include <stdbool.h>

bool XPlatformServicesDriver_isAvailable(void)
{
    /* Activity 引用就绪即可发起 ACTION_VIEW（校验与失败反馈在 openUrl）。 */
    return XAndroid_hasActivity();
}

bool XPlatformServicesDriver_openUrl(const char* url)
{
    return XAndroid_openUrl(url);
}

#endif /* XPLATFORMINTEGRATION_ON && __ANDROID__ */
