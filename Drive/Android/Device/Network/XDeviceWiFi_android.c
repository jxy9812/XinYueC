/**
 * @file       XDeviceWiFi_android.c
 * @brief      Android 平台无线网络实现（JNI WifiManager：扫描 + 连接态查询）。
 * @details    契约见 Src/XDevice/XDeviceWiFi.h。本文件只实现**平台叶子函数**
 *             （设备虚槽 VXDeviceWiFi_* 的落地实现），功能入口一律经父类的
 *             XDevice_control / XDevice_getProperty 走统一调度。
 *
 *             能力口径（对标 win32 WlanAPI 后端的诚实评估）：
 *               - CapScan：WifiManager.startScan() 发起 + getScanResults()
 *                 取系统缓存（对齐系统 WiFi 列表的数据源）；
 *               - CapConnect/CapDisconnect：经 **WifiNetworkSuggestion**
 *                 把网络**建议**给系统（XWifiConnector.java）。选这个
 *                 模型而非 WifiNetworkSpecifier+requestNetwork 的原因：
 *                 后者是"应用持有的临时连接"——不写入系统已保存列表，
 *                 系统设置页显示为"应用持有"且**不显示 IP/网关**
 *                 （真机实证：OwnerUid=1000 但 RequestorUid=我方，
 *                 Settings 页该网络无 IP/无网关）。suggestion 模型下
 *                 系统弹通知让用户确认，用户同意后**系统自己持有连接**
 *                 并写入自己的列表，Settings 页 IP/网关显示正常。
 *               - CapProfile：增删已保存网络（removeNetwork 等）普通应用
 *                 受限，仍未实现——能力位如实不含。
 *               - 连接态查询：getConnectionInfo（SSID/BSSID/RSSI，只读，
 *                 无需定位权限）。
 *
 *             权限依赖（Manifest 已补）：
 *               - ACCESS_WIFI_STATE / CHANGE_WIFI_STATE：普通权限；
 *               - ACCESS_FINE_LOCATION：API 26+ 扫描结果必需（运行时权限，
 *                 缺失时 getScanResults 恒空表——本实现不伪造数据，
 *                 结果如实为空；测试/部署侧经 pm grant 或设置页授权）。
 *
 *             扫描状态机口径：Android 无"扫描完成"事件可达 NDK（
 *             SCAN_RESULTS_AVAILABLE_ACTION 广播需要 Java 侧注册器），
 *             采用**缓存即结果**语义：startScan 受理即 Running，下一次
 *             ScanState 轮询（≥XDEVWIFI_ANDROID_RESULT_DELAY_MS 后）即
 *             Completed——getScanResults 本身就是系统当前缓存，与用户在
 *             系统设置页看到的一致。节流（系统 4 次/2 分钟上限）时
 *             startScan 返回 false 仍受理：缓存照常可取。
 * @author     XinYueC 团队
 ******************************************************************************/
#include "XNetwork_config.h"

#if XDEVICE_ON && XDEVICE_WIFI_ON

#if defined(XNETWORK_USE_PLATFORM_API) && defined(__ANDROID__)

#include "XDeviceWiFi.h"
#include "XString.h"
#include "XVector.h"
#include "XDateTime.h"
#include "XAndroidJniChannels.h"
#include <string.h>
#include <stdio.h>

/* =========================================================================
 * 内部状态（扫描状态机；单适配器模型）
 * ========================================================================= */

/** @brief 结果就绪时延：startScan 后至少经过此时长才判 Completed
 *  （给系统一点刷新缓存的时间；纯轮询语义，无线程）。 */
#define XDEVWIFI_ANDROID_RESULT_DELAY_MS 800

typedef struct XDevWifiAndroidState {
    bool open;                 /**< platformOpen 已成功。 */
    bool scanRunning;          /**< 扫描已发起、未收口。 */
    int64_t scanStartMs;       /**< 发起时刻（单调毫秒）。 */
} XDevWifiAndroidState;

static XDevWifiAndroidState g_xdevWifiAndroid = { false, false, 0 };

/** @brief 适配器标识（安卓单无线口惯例名；与 /sys/class/net 一致）。 */
static const char* const XDEVWIFI_ANDROID_ADAPTER_ID = "wlan0";

/** @brief adapterId 是否为本平台适配器（空串按默认适配器接受）。 */
static bool xdevwifi_android_adapterMatch(const char* adapterId)
{
    return !adapterId || !adapterId[0] ||
           strcmp(adapterId, XDEVWIFI_ANDROID_ADAPTER_ID) == 0;
}

/** @brief 能力串（如 "[WPA2-PSK-CCMP][ESS]"）→ 安全方式判定。 */
static XDeviceWiFiSecurity xdevwifi_android_parseSecurity(
    const char* capabilities)
{
    if (!capabilities || !capabilities[0]) return XDeviceWiFi_SecurityOpen;
    if (strstr(capabilities, "SAE") != NULL)
        return XDeviceWiFi_SecurityWpa3Sae;
    if (strstr(capabilities, "EAP") != NULL)
        return XDeviceWiFi_SecurityWpa2Enterprise;
    if (strstr(capabilities, "WEP") != NULL)
        return XDeviceWiFi_SecurityWep;
    if (strstr(capabilities, "WPA2") != NULL || strstr(capabilities, "RSN"))
        return XDeviceWiFi_SecurityWpa2Psk;
    if (strstr(capabilities, "WPA") != NULL)
        return XDeviceWiFi_SecurityWpaPsk;
    return XDeviceWiFi_SecurityOpen;
}

/** @brief dBm 信号 → 0..100（Android calculateSignalLevel 标准公式）。 */
static int xdevwifi_android_levelPercent(int dbm)
{
    if (dbm <= -100) return 0;
    if (dbm >= -50) return 100;
    return (dbm + 100) * 2;
}

/** @brief 载波 MHz → 信道号（2.4G/5G 两段；未知 0）。 */
static int xdevwifi_android_channel(int frequencyMhz)
{
    if (frequencyMhz >= 2412 && frequencyMhz <= 2472)
        return (frequencyMhz - 2412) / 5 + 1;
    if (frequencyMhz == 2484) return 14;
    if (frequencyMhz >= 5160 && frequencyMhz <= 5885)
        return (frequencyMhz - 5000) / 5;
    return 0;
}

/* =========================================================================
 * 平台叶子实现
 * ========================================================================= */

bool XDeviceWiFi_platformOpen(void)
{
    /* 就绪判定=JNI 通道可用（VM/Activity 注入即视为 WifiManager 可取）。 */
    g_xdevWifiAndroid.open = true;
    g_xdevWifiAndroid.scanRunning = false;
    g_xdevWifiAndroid.scanStartMs = 0;
    return true;
}

void XDeviceWiFi_platformClose(void)
{
    g_xdevWifiAndroid.open = false;
    g_xdevWifiAndroid.scanRunning = false;
    g_xdevWifiAndroid.scanStartMs = 0;
}

bool XDeviceWiFi_platformCapabilities(uint32_t* outCapabilities)
{
    if (!outCapabilities) return false;
    /* 扫描 + 连接/断开（WifiNetworkSpecifier 通道，见 platformStartConnect）。
     * profile（增删已保存网络）仍未实现——普通应用在现代安卓上无免弹窗的
     * profile 管理通道，故能力位如实不含 CapProfile。 */
    *outCapabilities = XDeviceWiFi_CapScan | XDeviceWiFi_CapConnect |
                       XDeviceWiFi_CapDisconnect;
    return true;
}

bool XDeviceWiFi_platformEnumAdapters(XVector** outAdapters)
{
    XDeviceWiFiAdapterInfo* info;
    if (!outAdapters) return false;
    *outAdapters = NULL;
    if (!g_xdevWifiAndroid.open) return false;
    *outAdapters = XVector_Create(XDeviceWiFiAdapterInfo*);
    if (!*outAdapters) return false;
    info = (XDeviceWiFiAdapterInfo*)XMalloc_System(sizeof(*info));
    if (!info) {
        XClassDelete((XClass*)*outAdapters);
        *outAdapters = NULL;
        return false;
    }
    memset(info, 0, sizeof(*info));
    info->id = XString_create_utf8(XDEVWIFI_ANDROID_ADAPTER_ID);
    info->name = XString_create_utf8("Wi-Fi");
    info->description = XString_create_utf8("Android WifiManager");
    info->ifIndex = 0; /* 无 ifIndex 语义（与 XNetworkInterface 对齐可后续补） */
    info->capabilities = XDeviceWiFi_CapScan;
    info->mode = XDeviceWiFi_ModeStation;
    XVector_push_back_1_base(*outAdapters, &info);
    return true;
}

bool XDeviceWiFi_platformStartScan(const char* adapterId)
{
    if (!g_xdevWifiAndroid.open || !xdevwifi_android_adapterMatch(adapterId))
        return false;
    /* 受理即 Running；系统节流（返回 false）不影响缓存可取性。 */
    (void)XAndroid_wifiStartScan();
    g_xdevWifiAndroid.scanRunning = true;
    g_xdevWifiAndroid.scanStartMs = XDateTime_currentMSecsSinceEpoch();
    return true;
}

XDeviceWiFiScanState XDeviceWiFi_platformScanState(const char* adapterId)
{
    if (!g_xdevWifiAndroid.open || !xdevwifi_android_adapterMatch(adapterId))
        return XDeviceWiFi_ScanIdle;
    if (!g_xdevWifiAndroid.scanRunning) return XDeviceWiFi_ScanIdle;
    if (XDateTime_currentMSecsSinceEpoch() - g_xdevWifiAndroid.scanStartMs
            >= XDEVWIFI_ANDROID_RESULT_DELAY_MS)
        return XDeviceWiFi_ScanCompleted;
    return XDeviceWiFi_ScanRunning;
}

bool XDeviceWiFi_platformTakeScanResults(const char* adapterId,
                                         XVector** outNetworks)
{
    XAndroidWifiScanItem items[32];
    XVector* networks;
    int count;
    int i;

    if (!outNetworks) return false;
    *outNetworks = NULL;
    if (!g_xdevWifiAndroid.open || !xdevwifi_android_adapterMatch(adapterId))
        return false;
    if (XDeviceWiFi_platformScanState(adapterId) !=
        XDeviceWiFi_ScanCompleted)
        return false;

    networks = XVector_Create(XDeviceWiFiNetworkInfo*);
    if (!networks) return false;
    count = XAndroid_wifiScanResults(items,
                                     (int)(sizeof(items) / sizeof(items[0])));
    for (i = 0; i < count; ++i) {
        XDeviceWiFiNetworkInfo* info =
            (XDeviceWiFiNetworkInfo*)XMalloc_System(sizeof(*info));
        if (!info) break;
        memset(info, 0, sizeof(*info));
        if (items[i].ssid[0])
            info->ssid = XString_create_utf8(items[i].ssid);
        if (items[i].bssid[0])
            info->bssid = XString_create_utf8(items[i].bssid);
        info->signalStrength =
            xdevwifi_android_levelPercent(items[i].level);
        info->channel = xdevwifi_android_channel(items[i].frequency);
        info->security =
            xdevwifi_android_parseSecurity(items[i].capabilities);
        info->secured = info->security != XDeviceWiFi_SecurityOpen &&
                        info->security != XDeviceWiFi_SecurityUnknown;
        XVector_push_back_1_base(networks, &info);
    }
    g_xdevWifiAndroid.scanRunning = false; /* 取走即复位（契约语义） */
    g_xdevWifiAndroid.scanStartMs = 0;
    *outNetworks = networks;
    return true;
}

bool XDeviceWiFi_platformStartConnect(const char* adapterId, const char* ssid,
                                      const char* passphrase,
                                      XDeviceWiFiSecurity security)
{
    /* [安卓连接通道 2026-10-24] 走 WifiNetworkSpecifier + 系统确认框
     * （XWifiConnector.java）：这是普通应用在现代安卓上唯一合规的连接
     * 方式——WifiManager.addNetwork/enableNetwork 自 API 29 起对非特权
     * 应用抛 SecurityException。系统弹窗由用户确认，故本函数返回 true
     * 只表示"请求已受理"，连上与否由 connectState 反映。
     *
     * 能力位已如实改为 CapConnect（见 platformCapabilities）。 */
    if (!g_xdevWifiAndroid.open || !xdevwifi_android_adapterMatch(adapterId))
        return false;
    if (!ssid || !ssid[0]) return false;
    return XAndroid_wifiConnect(ssid, passphrase, (int)security);
}

XDeviceWiFiConnectState XDeviceWiFi_platformConnectState(const char* adapterId)
{
    /* [连接状态机口径修正 2026-10-24] 本函数报的是**连接操作**的状态机，
     * 不是"当前是否已关联到某个 AP"——契约见 XDeviceWiFi.h：
     *   Idle --startConnect()--> Connecting --> Connected / Failed
     *   其中 ConnectIdle = "空闲（未发起连接）"。
     * win32 后端按此实现（slot->connectState 初始 Idle，只在 startConnect
     * 时才离开 Idle）。
     *
     * 此前安卓实现误用"是否已关联 AP"来推导：设备已被系统连上某个 AP
     * （真机常态）时恒返回 ConnectConnected。后果：上层（demo 网络设置页
     * 的 250ms 轮询定时器）以"连接状态优先"判断，先命中"已连接"分支就
     * return，**永远走不到扫描分支** → 扫描完成后 AP 下拉框不填任何项
     * （真机实证：点「扫描」后下拉框全空）。
     *
     * 安卓的连接通道已实现（WifiNetworkSpecifier，见 platformStartConnect），
     * 故本函数转发**连接操作**的状态机（XWifiConnector 侧维护）：
     * Idle=未发起 / Connecting=等待用户确认或切换中 / Connected=已连上 /
     * Failed=用户拒绝或失败。系统弹窗确认前一直是 Connecting。
     *
     * "当前连的是谁"由 platformQueryConnection 单独回答（页面状态行走
     * 那条路，语义正确且不受本函数影响）。 */
    if (!g_xdevWifiAndroid.open || !xdevwifi_android_adapterMatch(adapterId))
        return XDeviceWiFi_ConnectIdle;
    switch (XAndroid_wifiConnectState()) {
    case 1: return XDeviceWiFi_ConnectConnecting;
    case 2: return XDeviceWiFi_ConnectConnected;
    case 3: return XDeviceWiFi_ConnectFailed;
    default: return XDeviceWiFi_ConnectIdle;
    }
}

bool XDeviceWiFi_platformQueryConnection(const char* adapterId,
                                         XDeviceWiFiConnectionInfo* outInfo)
{
    char ssid[33];
    char bssid[18];
    int rssi = 0;

    if (!outInfo || !g_xdevWifiAndroid.open ||
        !xdevwifi_android_adapterMatch(adapterId))
        return false;
    memset(outInfo, 0, sizeof(*outInfo));
    outInfo->signalStrength = -1;
    outInfo->security = XDeviceWiFi_SecurityUnknown;
    outInfo->state = XDeviceWiFi_ConnectIdle;
    if (!XAndroid_wifiConnection(ssid, (int)sizeof(ssid),
                                 bssid, (int)sizeof(bssid), &rssi))
        return false;
    outInfo->ssid = XString_create_utf8(ssid);
    if (bssid[0]) outInfo->bssid = XString_create_utf8(bssid);
    outInfo->signalStrength = xdevwifi_android_levelPercent(rssi);
    outInfo->state = XDeviceWiFi_ConnectConnected;
    return true;
}

bool XDeviceWiFi_platformDisconnect(const char* adapterId)
{
    /* suggestion 模型下，连接归**系统**持有：我方能撤的是"我方提交的建议"
     * （removeNetworkSuggestions）。真正的断开由系统侧完成（用户在系统
     * WiFi 列表断开 / 切到别的网络）——应用无权强制断开系统持有的连接。
     * 无我方建议可撤时如实返回 false（不谎报）。 */
    if (!g_xdevWifiAndroid.open || !xdevwifi_android_adapterMatch(adapterId))
        return false;
    return XAndroid_wifiDisconnect();
}

bool XDeviceWiFi_platformForgetProfile(const char* adapterId, const char* ssid)
{
    (void)adapterId; (void)ssid;
    return false;
}

#endif /* XNETWORK_USE_PLATFORM_API && __ANDROID__ */

#endif /* XDEVICE_ON && XDEVICE_WIFI_ON */
