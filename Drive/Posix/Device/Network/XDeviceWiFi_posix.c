/**
 * @file       XDeviceWiFi_posix.c
 * @brief      POSIX 平台无线网络实现（当前为占位，后续接 nmcli）。
 * @details    契约见 Src/XDevice/XDeviceWiFi.h。
 *
 *             本文件只实现**平台叶子函数**（设备虚槽 VXDeviceWiFi_* 的
 *             落地实现）：功能入口一律经父类的 XDevice_control /
 *             XDevice_getProperty 走统一调度，业务层见不到本文件。
 *
 *             现状：本平台后端**尚未实现真实能力**，全部入口如实返回
 *             "不支持"（能力位 0 / 空适配器列表 / 发起失败 / 状态恒 Idle），
 *             不静默成功、不崩溃。上层据此如实禁用无线入口。
 *
 *             后续实现口径（nmcli / NetworkManager）：
 *               - 适配器枚举：nmcli device status 过滤 wifi 类型，
 *                 设备标识取设备路径（如 /dev/wlan0）；
 *               - 扫描：nmcli device wifi rescan 发起（立即返回），
 *                 轮询 nmcli device wifi list 取结果，超时按 4 秒兜底；
 *               - 连接：nmcli device wifi connect <ssid> password <pwd>；
 *               - 无线口识别：借 Qt 6.8 口径（ARPHRD_ETHER 时用
 *                 ioctl(SIOCGIWMODE) 判定），而非按接口名猜测。
 */
#include "XNetwork_config.h"

#if XDEVICE_ON && XDEVICE_WIFI_ON

#if defined(XNETWORK_USE_PLATFORM_API) && \
    (defined(__linux__) || defined(__APPLE__) || defined(__BSD__) || \
     defined(__FreeBSD__) || defined(__OpenBSD__) || defined(__NetBSD__)) && \
    !defined(__ANDROID__) /* [2026-10-24 平台补扫] 安卓由
                             Drive/Android/Device/Network/XDeviceWiFi_android.c
                             承担（JNI WifiManager），此处排除防重复定义。 */

#include "XDeviceWiFi.h"
#include "XString.h"
#include "XVector.h"

/* =========================================================================
 * 平台叶子实现
 * ========================================================================= */

bool XDeviceWiFi_platformOpen(void)
{
    /* 本平台尚未实现无线后端：无线设备打开即失败。 */
    return false;
}

void XDeviceWiFi_platformClose(void)
{
    /* 本平台无平台侧全局资源。 */
}

bool XDeviceWiFi_platformCapabilities(uint32_t* outCapabilities)
{
    if (!outCapabilities) return false;
    *outCapabilities = 0;
    return false;
}

bool XDeviceWiFi_platformEnumAdapters(XVector** outAdapters)
{
    if (!outAdapters) return false;
    *outAdapters = XVector_Create(XDeviceWiFiAdapterInfo*);
    return *outAdapters != NULL;
}

bool XDeviceWiFi_platformStartScan(const char* adapterId)
{
    (void)adapterId;
    return false;
}

XDeviceWiFiScanState XDeviceWiFi_platformScanState(const char* adapterId)
{
    (void)adapterId;
    return XDeviceWiFi_ScanIdle;
}

bool XDeviceWiFi_platformTakeScanResults(const char* adapterId,
                                         XVector** outNetworks)
{
    (void)adapterId;
    if (outNetworks) *outNetworks = NULL;
    return false;
}

bool XDeviceWiFi_platformStartConnect(const char* adapterId, const char* ssid,
                                      const char* passphrase,
                                      XDeviceWiFiSecurity security)
{
    (void)adapterId;
    (void)ssid;
    (void)passphrase;
    (void)security;
    return false;
}

XDeviceWiFiConnectState XDeviceWiFi_platformConnectState(const char* adapterId)
{
    (void)adapterId;
    return XDeviceWiFi_ConnectIdle;
}

bool XDeviceWiFi_platformQueryConnection(const char* adapterId,
                                         XDeviceWiFiConnectionInfo* outInfo)
{
    (void)adapterId;
    if (outInfo) {
        outInfo->ssid = NULL;
        outInfo->bssid = NULL;
        outInfo->signalStrength = -1;
        outInfo->security = XDeviceWiFi_SecurityUnknown;
        outInfo->state = XDeviceWiFi_ConnectIdle;
    }
    return false;
}

bool XDeviceWiFi_platformDisconnect(const char* adapterId)
{
    (void)adapterId;
    return false;
}

bool XDeviceWiFi_platformForgetProfile(const char* adapterId, const char* ssid)
{
    (void)adapterId;
    (void)ssid;
    return false;
}

#endif /* POSIX 平台 */

#endif /* XNETWORK_ON && XNETWORK_WIFI_ON */
