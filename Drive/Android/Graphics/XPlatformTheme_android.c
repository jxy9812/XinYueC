/**
 * @file       XPlatformTheme_android.c
 * @brief      XPlatformTheme 安卓驱动（暗色探测经 JNI uiMode）。
 * @details    [2026-10-24 平台补扫] 对标 posix 驱动的 XDG 环境变量通道：
 *             安卓无桌面壳环境变量，主题态取 Configuration.uiMode 的
 *             NIGHT 位（系统深色模式）。图标通道安卓无 icon theme 概念，
 *             如实报不可用（上层走内置回退）。
 * @note       平台 API 只出现在 Drive；本文件仅在 __ANDROID__ 下编译出实体。
 * @author     XinYueC 团队
 ******************************************************************************/
#include "XPlatformTheme.h"
#include "XAndroidJniChannels.h"

#if XPLATFORMINTEGRATION_ON && defined(__ANDROID__)

#include <string.h>
#include "XStringList.h"

bool XPlatformThemeDriver_detect(bool* dark, char* name, size_t capacity)
{
    bool ok = false;
    bool isDark = false;
    if (XAndroid_queryDarkTheme(&isDark)) {
        ok = true;
        if (dark) *dark = isDark;
    }
    if (name && capacity) {
        strncpy(name, "android", capacity - 1);
        name[capacity - 1] = '\0';
    }
    /* 暗色位查询失败（无 VM/早期阶段）时返回 false：上层按未知主题回退，
       与 posix 无环境变量时同口径。 */
    return ok;
}

bool XPlatformThemeDriver_iconSearchPaths(bool fallback, XStringList* out)
{
    /* 安卓无 icon theme 目录约定：不提供搜索路径（上层用内置图标）。 */
    (void)fallback;
    (void)out;
    return false;
}

bool XPlatformThemeDriver_iconThemeName(bool fallback, char* name,
                                        size_t capacity)
{
    (void)fallback;
    if (!name || capacity == 0) return false;
    name[0] = '\0';
    return false;
}

#endif /* XPLATFORMINTEGRATION_ON && __ANDROID__ */
