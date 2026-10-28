/******************************************************************************
 * @file       XPlatformOrientation_win32.c
 * @brief      XPlatformOrientation 契约的 Win32 实现（DEVMODE 显示方向）。
 * @details    Win32 的显示方向由显示模式的 DEVMODE.dmDisplayOrientation
 *             持有（0/90/180/270 四档），改向的标准做法是：
 *             EnumDisplaySettings 取当前模式 → 改 dmDisplayOrientation
 *             并按新朝向对调 dmPelsWidth/dmPelsHeight（Win32 约定：
 *             90/270 档下宽高是对调后的值）→ ChangeDisplaySettingsEx
 *             提交。
 *
 *             尺寸基准：DEVMODE 的宽高是**当前朝向口径**的值，转屏后
 *             宽高已经是对调过的，故换向时须以**自然尺寸**（未旋转）为
 *             基准重算，否则「90° → 180°」这类二次换向会把尺寸再错
 *             一次。自然尺寸在首次读到模式时捕获（当前朝向为 90/270
 *             则把宽高对调回自然口径）。
 *
 *             「跟随系统」：Win32 的显示模式没有「交回传感器」语义，
 *             落点是**恢复进程启动时捕获的原始模式**（默认朝向）。
 * @note       平台 API 只出现在 Drive；本文件仅在 _WIN32 下编译出实体，
 *             其他平台整文件为空（Unsupported 哨兵同样让位）。
 * @author     XinYueC 团队
 ******************************************************************************/
#include "XPlatformOrientation.h"

#if defined(_WIN32) && XGUI_ON

#include <windows.h>

/* Windows SDK wingdi.h 的方向常量名为 DMDO_DEFAULT/DMDO_90/DMDO_180/DMDO_270，
 * 此处按本文件原用的 DMDO_ORIENTATION_* 记法对齐（值一一对应）。 */
#ifndef DMDO_ORIENTATION_0
#define DMDO_ORIENTATION_0    DMDO_DEFAULT
#define DMDO_ORIENTATION_90   DMDO_90
#define DMDO_ORIENTATION_180  DMDO_180
#define DMDO_ORIENTATION_270  DMDO_270
#endif

static DEVMODE g_xoriDefaultMode;   /**< 启动时捕获的原始显示模式。 */
static bool    g_xoriHaveDefault = false;
static int     g_xoriNatW = 0;       /**< 显示自然宽（未旋转）。 */
static int     g_xoriNatH = 0;       /**< 显示自然高（未旋转）。 */
static bool    g_xoriNatKnown = false;

/** @brief 捕获自然尺寸与启动默认模式（首次读到模式时）。 */
static void xoriEnsureDefaults(void)
{
    DEVMODE dm;
    if (g_xoriNatKnown) return;
    ZeroMemory(&dm, sizeof(dm));
    dm.dmSize = sizeof(DEVMODE);
    if (!EnumDisplaySettings(NULL, ENUM_CURRENT_SETTINGS, &dm)) return;
    if (dm.dmPelsWidth <= 0 || dm.dmPelsHeight <= 0) return;
    if (!g_xoriHaveDefault) {
        g_xoriDefaultMode = dm;
        g_xoriHaveDefault = true;
    }
    /* 当前朝向为 90/270 时 DEVMODE 宽高是对调过的 → 反对调回自然口径。 */
    if (dm.dmDisplayOrientation == DMDO_ORIENTATION_90 ||
        dm.dmDisplayOrientation == DMDO_ORIENTATION_270) {
        g_xoriNatW = dm.dmPelsHeight;
        g_xoriNatH = dm.dmPelsWidth;
    } else {
        g_xoriNatW = dm.dmPelsWidth;
        g_xoriNatH = dm.dmPelsHeight;
    }
    g_xoriNatKnown = true;
}

/** @brief 本契约度数 → DEVMODE 朝向常量。
 *  @return DMDO_ORIENTATION_*；角度非法返回 -1。
 *  @note   口径一一对应：0=横屏→DMDO_ORIENTATION_0、90=竖屏→_90、
 *          180=倒置横屏→_180、270=倒置竖屏→_270。 */
static int xoriDegreesToDmd(int degrees)
{
    switch (degrees) {
        case 0:   return DMDO_ORIENTATION_0;
        case 90:  return DMDO_ORIENTATION_90;
        case 180: return DMDO_ORIENTATION_180;
        case 270: return DMDO_ORIENTATION_270;
        default:  return -1;
    }
}

unsigned XPlatformOrientationDriver_capabilities(void)
{
    DEVMODE dm;
    ZeroMemory(&dm, sizeof(dm));
    dm.dmSize = sizeof(DEVMODE);
    if (!EnumDisplaySettings(NULL, ENUM_CURRENT_SETTINGS, &dm)) return 0u;
    /* 能读到显示模式即具备「申请方向 / 交回默认 / 回读当前」三项。 */
    return (unsigned)XPlatformOrientationCap_Request |
           (unsigned)XPlatformOrientationCap_Auto |
           (unsigned)XPlatformOrientationCap_Query;
}

bool XPlatformOrientationDriver_setOrientation(int degrees)
{
    DEVMODE dm;
    DWORD orientation;
    LONG rc;

    ZeroMemory(&dm, sizeof(dm));
    dm.dmSize = sizeof(DEVMODE);
    if (!EnumDisplaySettings(NULL, ENUM_CURRENT_SETTINGS, &dm)) return false;
    xoriEnsureDefaults();

    if (degrees == XPLATFORM_ORIENTATION_AUTO) {
        /* 「跟随系统」= 恢复启动时捕获的原始模式（默认朝向）。 */
        if (!g_xoriHaveDefault) return false;
        dm = g_xoriDefaultMode;
        dm.dmFields = DM_ORIENTATION | DM_PELSWIDTH | DM_PELSHEIGHT |
                      DM_BITSPERPEL | DM_DISPLAYFREQUENCY;
        rc = ChangeDisplaySettingsEx(NULL, &dm, NULL, CDS_UPDATEREGISTRY,
                                      NULL);
        return (rc == DISP_CHANGE_SUCCESSFUL);
    }

    orientation = (DWORD)xoriDegreesToDmd(degrees);
    if (orientation == (DWORD)-1) return false;
    dm.dmDisplayOrientation = orientation;
    if (g_xoriNatKnown) {
        /* 宽高按**目标朝向**口径给：90/270 档下宽高对调。 */
        if (orientation == DMDO_ORIENTATION_90 ||
            orientation == DMDO_ORIENTATION_270) {
            dm.dmPelsWidth = g_xoriNatH;
            dm.dmPelsHeight = g_xoriNatW;
        } else {
            dm.dmPelsWidth = g_xoriNatW;
            dm.dmPelsHeight = g_xoriNatH;
        }
    }
    dm.dmFields = DM_ORIENTATION | DM_PELSWIDTH | DM_PELSHEIGHT;
    rc = ChangeDisplaySettingsEx(NULL, &dm, NULL, CDS_UPDATEREGISTRY, NULL);
    return (rc == DISP_CHANGE_SUCCESSFUL);
}

int XPlatformOrientationDriver_currentOrientation(void)
{
    DEVMODE dm;
    ZeroMemory(&dm, sizeof(dm));
    dm.dmSize = sizeof(DEVMODE);
    if (!EnumDisplaySettings(NULL, ENUM_CURRENT_SETTINGS, &dm)) return -1;
    switch (dm.dmDisplayOrientation) {
        case DMDO_ORIENTATION_0:   return 0;
        case DMDO_ORIENTATION_90:  return 90;
        case DMDO_ORIENTATION_180: return 180;
        case DMDO_ORIENTATION_270: return 270;
        default: break;
    }
    return -1;
}

#endif /* defined(_WIN32) && XGUI_ON */
