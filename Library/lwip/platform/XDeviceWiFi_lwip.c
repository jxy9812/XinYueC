/**
 * @file       XDeviceWiFi_lwip.c
 * @brief      lwIP 通用平台无线网络实现（占位）。
 * @details    契约见 Src/XDevice/XDeviceWiFi.h。
 *
 *             本文件只实现**平台叶子函数**（设备虚槽 VXDeviceWiFi_* 的
 *             落地实现）；功能入口一律经父类的 XDevice_control /
 *             XDevice_getProperty 走统一调度。
 *
 *             lwIP 本身不含无线驱动（802.11 由芯片厂商 SDK 提供，如
 *             ESP8266 的 net80211），故通用 lwIP 后端**不具备**无线能力：
 *             全部入口如实返回"不支持"。带无线 SDK 的板级移植层
 *             （platforms/esp8266 等）另行提供专用后端。
 */
#include "XNetwork_config.h"

#if XDEVICE_ON && XDEVICE_WIFI_ON

#ifdef XNETWORK_USE_LWIP

#include "XDeviceWiFi.h"
#include "XString.h"
#include "XVector.h"

/* =========================================================================
 * 平台叶子实现
 * ========================================================================= */

bool XDeviceWiFi_platformOpen(void)
{
    /* 通用 lwIP 后端无无线能力：无线设备打开即失败。 */
    return false;
}

void XDeviceWiFi_platformClose(void)
{
    /* 无平台侧全局资源。 */
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

#endif /* XNETWORK_USE_LWIP */

#endif /* XNETWORK_ON && XNETWORK_WIFI_ON */
