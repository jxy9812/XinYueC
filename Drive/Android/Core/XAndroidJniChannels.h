/**
 * @file       XAndroidJniChannels.h
 * @brief      XSystemAndroid.c 的 JNI 通道共享声明（Drive/Android 内部用）。
 * @details    [2026-10-24 平台补扫] 平台叶子实现文件（剪贴板后端、WiFi 后端、
 *             Services/Theme 驱动）与 XSystemAndroid.c 之间的窄接口：VM/
 *             Activity 引用由壳层注入（XAndroid_setJavaVm/_setMainActivity），
 *             全部通道异常安全（失败 false/0，无异常外泄）。仅限 Drive/Android
 *             内部 include——业务层一律走框架契约，不得直呼。
 * @author     XinYueC 团队
 ******************************************************************************/
#ifndef XANDROIDJNICHANNELS_H
#define XANDROIDJNICHANNELS_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ==================== 生命周期 ==================== */

/** @brief Activity 引用是否就绪（各驱动 isAvailable 口径）。 */
bool XAndroid_hasActivity(void);

/* ==================== 剪贴板（ClipboardManager） ==================== */

bool XAndroid_clipboardGetText(char** outText);
bool XAndroid_clipboardSetText(const char* utf8);
bool XAndroid_clipboardClear(void);

/* ==================== Services（ACTION_VIEW） ==================== */

/** @brief 打开系统浏览器/对应处理器；对标 posix xdg-open 通道。 */
bool XAndroid_openUrl(const char* url);

/* ==================== Theme（Configuration.uiMode） ==================== */

/** @brief 查询系统暗色主题（UI_MODE_NIGHT_YES 位）。 */
bool XAndroid_queryDarkTheme(bool* outDark);

/* ==================== WiFi（WifiManager） ==================== */

/** @brief 扫描结果条目（C 侧固定容量拷贝；字段与 android.net.wifi
 *  ScanResult 公开字段同源）。 */
typedef struct XAndroidWifiScanItem {
    char ssid[33];           /**< SSID（UTF-8，IEEE 32 八位组 + NUL）。 */
    char bssid[18];          /**< MAC "aa:bb:cc:dd:ee:ff"。 */
    char capabilities[64];   /**< 原始能力串（如 "[WPA2-PSK-CCMP]"）。 */
    int level;               /**< 信号 dBm（负值）。 */
    int frequency;           /**< 载波 MHz。 */
} XAndroidWifiScanItem;

/** @brief 发起一次系统扫描（立即返回；false=节流/失败，缓存仍可取）。 */
bool XAndroid_wifiStartScan(void);

/** @brief 取系统当前扫描结果缓存；返回实际条数（<=max）。 */
int XAndroid_wifiScanResults(XAndroidWifiScanItem* items, int max);

/** @brief 查询当前连接（true=已连接；SSID 剥引号、未知= false）。 */
bool XAndroid_wifiConnection(char* outSsid, int ssidCap,
                             char* outBssid, int bssidCap, int* outRssi);

/* ==================== 网络 DHCP 信息（WifiManager.getDhcpInfo） ================= */

/** @brief 当前 Wi-Fi 连接的 DHCP 租约快照（字段与 DhcpInfo 同源，主机序）。
 *  @details 安卓应用域读不了 /proc/net（网关）也没有 /etc/resolv.conf
 *           （DNS 由 netd 按网络管理）——posix 后端 queryInterfaceConfig
 *           的常规取数路径在安卓全空。系统级权威源是
 *           WifiManager.getDhcpInfo()：本通道把它拷出来供 L0 网络后端
 *           填充 网关/DNS/DHCP 服务器/模式 判定。 */
typedef struct XAndroidDhcpInfo {
    unsigned int ipAddress;   /**< 本机 IPv4（主机序；0=无租约）。 */
    unsigned int gateway;     /**< 默认网关 IPv4（主机序）。 */
    unsigned int netmask;     /**< 掩码 IPv4（主机序）。 */
    unsigned int dns1;        /**< 首选 DNS IPv4（主机序）。 */
    unsigned int dns2;        /**< 备用 DNS IPv4（主机序）。 */
    unsigned int serverAddress; /**< DHCP 服务器 IPv4（主机序；0=非 DHCP）。 */
    int leaseDuration;        /**< 租约时长（秒）。 */
} XAndroidDhcpInfo;

/** @brief 取当前 Wi-Fi 的 DHCP 信息（true=有有效租约且字段已填）。 */
bool XAndroid_netDhcpInfo(XAndroidDhcpInfo* outInfo);

/** @brief 取活动网络的链路信息（ConnectivityManager.LinkProperties，
 *         经 XNetAdapter 桥）。
 *  @details 填 IPv6 默认网关 / DNS1 / DNS2 / 接口名（均为文本，缺为空串）。
 *           调用方按接口名匹配后才采用（LinkProperties 描述的是活动网络）。 */
bool XAndroid_netLinkInfo(char* outV6Gateway, int v6GatewayCap,
                          char* outDns1, int dns1Cap,
                          char* outDns2, int dns2Cap,
                          char* outIfaceName, int ifaceNameCap);

/** @brief 切 DHCP 模式（XNetAdapter.setDhcpMode：已 DHCP=成功；否则对当前
 *         WiFi 配置 updateNetwork(DHCP)——受配置所有权限制）。 */
bool XAndroid_netSetDhcp(void);

/** @brief 切静态 IPv4（XNetAdapter.setStaticV4：反射 WifiConfiguration；
 *         受配置所有权限制——仅应用创建的网络可改）。 */
bool XAndroid_netSetStaticV4(const char* ip, int prefixLength,
                             const char* gateway,
                             const char* dnsPrimary, const char* dnsSecondary);

/* ==================== WiFi 连接/断开（XWifiConnector 桥） ================= */

/** @brief 发起连接（会弹系统确认框，用户同意后系统切网）。
 *  @param ssid       目标 SSID（明文）。
 *  @param passphrase 口令（开放网络传 NULL）。
 *  @param security   加密类型（XDeviceWiFiSecurity 值域）。
 *  @return true=请求已受理（不代表已连上；连上与否查 connectState）。 */
bool XAndroid_wifiConnect(const char* ssid, const char* passphrase,
                          int security);

/** @brief 断开：解除我方发起的连接请求。
 *  @return true=已解除；false=无我方请求可解除（该网络由系统连上，
 *          普通应用无权强制断开——不谎报成功）。 */
bool XAndroid_wifiDisconnect(void);

/** @brief 连接操作状态机（0=Idle 未发起, 1=Connecting, 2=Connected,
 *         3=Failed；与 XDeviceWiFiConnectState 值域一致）。 */
int XAndroid_wifiConnectState(void);

#ifdef __cplusplus
}
#endif

#endif /* XANDROIDJNICHANNELS_H */
