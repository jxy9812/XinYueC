/**
 * @file       XDeviceWiFi_win32.c
 * @brief      Windows 平台无线网络实现（Native WiFi / WlanAPI）。
 * @details    契约见 Src/XDevice/XDeviceWiFi.h。
 *
 *             平台语义映射（Native WiFi 官方行为，SDK 10.0.26100）：
 *              - 扫描：WlanScan 文档原文 "The WlanScan function returns
 *                immediately." —— 发起即返回；结果靠 ACM 通知
 *                （wlan_notification_acm_scan_complete）或 4 秒超时兜底后
 *                再取。本实现两者都做：通知到位立刻收口，否则按驱动规范
 *                上限（4 秒）收口；
 *              - 连接：WlanConnect 同样是"发起即返回"，连接结果经
 *                WlanQueryInterface(wlan_intf_opcode_current_connection)
 *                轮询判定；
 *              - 口令：先 WlanSetProfile 写 profile（口令在 XML 内），再
 *                WlanConnect 连。
 *
 *             线程纪律：WlanRegisterNotification 的回调由 WLAN 服务线程
 *             触发，本实现**只在该回调里写一个原子标志**，绝不触碰界面、
 *             绝不跨线程投递事件——上层用事件循环定时器轮询状态即可。
 *
 *             适配器标识 = 网卡 GUID 串（StringFromGUID2 产出，与 IP Helper
 *             的 AdapterName 同源），故 ifIndex 可由 GetAdaptersAddresses
 *             对齐回 XNetworkInterface 的 index，IP 配置通路得以复用。
 */
#include "XNetwork_config.h"

#if XDEVICE_ON && XDEVICE_WIFI_ON

#if defined(_WIN32)

/* ====== Windows 宏定义必须在所有头文件之前 ======*/
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0600
#endif

/* ====== 项目头文件 ====== */
#include "XDeviceWiFi.h"
#include "XMemory.h"
#include "XString.h"
#include "XVector.h"

/* ====== Windows SDK 头文件 ======
 * 顺序要点：winsock2.h 必须在 windows.h 之前（winsock.h 与 winsock2.h 互斥，
 * 与 Drive/windows/Device/Network/XDeviceNetwork_win32.c 同口径）；
 * iphlpapi.h 的 IP_ADAPTER_ADDRESSES 依赖 winsock2 类型，故紧随其后；
 * objbase.h 提供 StringFromGUID2（网卡 GUID 串）。 */
#include <winsock2.h>
#include <ws2tcpip.h>
#include <iphlpapi.h>
#include <wlanapi.h>
#include <objbase.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Native WiFi（wlanapi）—— 与 CMake 的 wlanapi 链接项双保险。 */
#pragma comment(lib, "wlanapi.lib")

/* =========================================================================
 * 常量与内部结构
 * ========================================================================= */

/** @brief 扫描超时兜底（毫秒）：Windows 规范要求驱动 4 秒内完成扫描。 */
#define XWIFI_SCAN_TIMEOUT_MS 4000u
/** @brief 连接超时兜底（毫秒）。 */
#define XWIFI_CONNECT_TIMEOUT_MS 20000u
/** @brief 本机适配器槽位上限（由设备层配置 XDEVICE_WIFI_MAX_ADAPTERS 决定）。 */
#define XWIFI_MAX_ADAPTERS XDEVICE_WIFI_MAX_ADAPTERS
/** @brief 适配器标识串长度（GUID 串 38 字符 + 终止符）。 */
#define XWIFI_ADAPTER_ID_MAX 64
/** @brief SSID 文本缓冲区长度。 */
#define XWIFI_SSID_MAX 64
/** @brief 安全方式速查表容量。 */
#define XWIFI_SECURITY_MAX 64

/**
 * @brief 单个无线适配器的运行时状态。
 * @details 平台侧以 GUID 为身份，扫描/连接状态机挂在同一槽位上。
 */
typedef struct XWifiAdapterSlot {
    bool used;                            /**< 槽位是否占用。 */
    GUID guid;                            /**< 网卡 GUID。 */
    char id[XWIFI_ADAPTER_ID_MAX];        /**< GUID 串（UTF-8/ASCII）。 */
    char name[128];                       /**< 友好名（UTF-8；空=未知）。 */
    char description[256];                /**< 硬件描述（UTF-8；空=未知）。 */
    uint32_t ifIndex;                     /**< 与 XNetworkInterface index 同源；0=不可用。 */
    XDeviceWiFiScanState scanState;       /**< 扫描状态机。 */
    DWORD scanStartMs;                    /**< 扫描发起时刻（GetTickCount）。 */
    volatile LONG scanNotified;           /**< ACM 扫描完成通知标志（跨线程只写此位）。 */
    XDeviceWiFiConnectState connectState; /**< 连接状态机。 */
    DWORD connectStartMs;                 /**< 连接发起时刻。 */
} XWifiAdapterSlot;

/**
 * @brief 扫描结果里的安全方式速查表（按 SSID 匹配）。
 */
typedef struct XWifiSecurityEntry {
    char ssid[XWIFI_SSID_MAX];            /**< 网络名（UTF-8 字节）。 */
    XDeviceWiFiSecurity security;        /**< 统一安全方式。 */
    bool secured;                        /**< 是否需要口令。 */
    bool valid;                          /**< 表项是否有效。 */
} XWifiSecurityEntry;

/** @brief WLAN 客户端句柄（懒申请）。 */
static HANDLE s_wlanClient = NULL;
/** @brief WLAN 客户端句柄的引用计数（设备会话数）。
 *  @details 客户端句柄是进程级单例，而设备会话的 open/close 一一映射到它；
 *           多会话并存时必须引用计数，否则先关闭的会话会把另一个会话的
 *           句柄一并释放（后者的调用随即失效）。 */
static int s_clientRefs = 0;
/** @brief 适配器槽位表。 */
static XWifiAdapterSlot s_slots[XWIFI_MAX_ADAPTERS];
/** @brief 通知是否注册成功（决定扫描能否被通知提前唤醒）。 */
static bool s_notifyRegistered = false;
/** @brief 安全方式速查表（扫描时填充）。 */
static XWifiSecurityEntry s_security[XWIFI_SECURITY_MAX];
/** @brief 安全方式速查表条目数。 */
static int s_securityCount = 0;

/* =========================================================================
 * 内部辅助
 * ========================================================================= */

/** @brief UTF-16 → UTF-8 堆拷贝（XMalloc_System 分配；失败返回 NULL）。 */
static char* wifiDupUtf8(const wchar_t* wide)
{
    int chars;
    char* utf8;
    if (!wide) wide = L"";
    chars = WideCharToMultiByte(CP_UTF8, 0, wide, -1, NULL, 0, NULL, NULL);
    if (chars <= 0) return NULL;
    utf8 = (char*)XMalloc_System((size_t)chars);
    if (!utf8) return NULL;
    if (WideCharToMultiByte(CP_UTF8, 0, wide, -1, utf8, chars, NULL, NULL) <= 0) {
        XFree_System(utf8);
        return NULL;
    }
    return utf8;
}

/** @brief UTF-8 → UTF-16 堆拷贝（XMalloc_System 分配；失败返回 NULL）。 */
static wchar_t* wifiDupWide(const char* utf8)
{
    int chars;
    wchar_t* wide;
    if (!utf8) utf8 = "";
    chars = MultiByteToWideChar(CP_UTF8, 0, utf8, -1, NULL, 0);
    if (chars <= 0) return NULL;
    wide = (wchar_t*)XMalloc_System((size_t)chars * sizeof(wchar_t));
    if (!wide) return NULL;
    if (MultiByteToWideChar(CP_UTF8, 0, utf8, -1, wide, chars) != chars) {
        XFree_System(wide);
        return NULL;
    }
    return wide;
}

/** @brief 定长文本拷贝（自带终止符，不触发 strncpy 的弃用告警）。 */
static void wifiCopyText(char* dst, size_t capacity, const char* src)
{
    size_t n;
    if (!dst || capacity == 0) return;
    if (!src) {
        dst[0] = '\0';
        return;
    }
    n = strlen(src);
    if (n >= capacity) n = capacity - 1;
    memcpy(dst, src, n);
    dst[n] = '\0';
}

/**
 * @brief ACM 通知回调：只置原子标志，绝不触碰界面与事件循环。
 * @param data 通知数据（借用）；@param context 注册时传入的上下文（未用）。
 */
static void WINAPI wifiNotifyCallback(PWLAN_NOTIFICATION_DATA data, PVOID context)
{
    int i;
    (void)context;
    if (!data) return;
    if (data->NotificationCode != (DWORD)wlan_notification_acm_scan_complete) return;
    for (i = 0; i < XWIFI_MAX_ADAPTERS; ++i) {
        if (!s_slots[i].used) continue;
        /* C 模式下 IsEqualGUID 走 memcmp 实参直传分支，须传指针。 */
        if (IsEqualGUID(&s_slots[i].guid, &data->InterfaceGuid))
            s_slots[i].scanNotified = 1;
    }
}

/** @brief 注册 ACM 通知（失败不致命，退化为纯超时兜底）。 */
static void wifiRegisterNotification(void)
{
    DWORD prevSource = 0;
    if (!s_wlanClient) return;
    if (WlanRegisterNotification(s_wlanClient, WLAN_NOTIFICATION_SOURCE_ACM, TRUE,
                                 wifiNotifyCallback, NULL, NULL,
                                 &prevSource) == ERROR_SUCCESS)
        s_notifyRegistered = true;
}

/** @brief 懒申请 WLAN 客户端句柄。 */
static bool wifiEnsureClient(void)
{
    DWORD version = 0;
    HANDLE handle = NULL;
    if (s_wlanClient) return true;
    if (WlanOpenHandle(2, NULL, &version, &handle) != ERROR_SUCCESS)
        return false;
    s_wlanClient = handle;
    wifiRegisterNotification();
    return true;
}

/** @brief 清空槽位表（保留客户端句柄）。 */
static void wifiClearSlots(void)
{
    memset(s_slots, 0, sizeof(s_slots));
}

/**
 * @brief 重建适配器槽位表（WlanEnumInterfaces + IP Helper 对齐 ifIndex）。
 * @return 至少枚举到一个适配器返回 true。
 */
static bool wifiRefreshSlots(void)
{
    WLAN_INTERFACE_INFO_LIST* list = NULL;
    unsigned int i;
    if (!wifiEnsureClient()) return false;
    wifiClearSlots();
    /* 接口表由 WlanEnumInterfaces 自行分配，收尾须用 WlanFreeMemory 释放。 */
    if (WlanEnumInterfaces(s_wlanClient, NULL, &list) != ERROR_SUCCESS ||
        !list)
        return false;
    for (i = 0; i < list->dwNumberOfItems && i < XWIFI_MAX_ADAPTERS; ++i) {
        WLAN_INTERFACE_INFO* info = &list->InterfaceInfo[i];
        XWifiAdapterSlot* slot = &s_slots[i];
        wchar_t guidWide[XWIFI_ADAPTER_ID_MAX];
        char guidStr[XWIFI_ADAPTER_ID_MAX];
        char* guidUtf8;
        if (StringFromGUID2(&info->InterfaceGuid, guidWide,
                            (int)(sizeof(guidWide) / sizeof(guidWide[0]))) == 0)
            continue;
        guidUtf8 = wifiDupUtf8(guidWide);
        if (!guidUtf8) continue;
        wifiCopyText(guidStr, sizeof(guidStr), guidUtf8);
        XFree_System(guidUtf8);
        slot->used = true;
        slot->guid = info->InterfaceGuid;
        wifiCopyText(slot->id, sizeof(slot->id), guidStr);
        slot->name[0] = '\0';
        slot->description[0] = '\0';
        slot->scanState = XDeviceWiFi_ScanIdle;
        slot->connectState = XDeviceWiFi_ConnectIdle;
        slot->scanNotified = 0;
        {
            char* friendly = wifiDupUtf8(info->strInterfaceDescription);
            if (friendly) {
                wifiCopyText(slot->name, sizeof(slot->name), friendly);
                wifiCopyText(slot->description, sizeof(slot->description),
                             friendly);
                XFree_System(friendly);
            }
        }
        /* ifIndex 对齐：IP Helper 的 AdapterName 即 GUID 串。 */
        {
            PIP_ADAPTER_ADDRESSES head = NULL;
            PIP_ADAPTER_ADDRESSES adapter;
            ULONG size = 0;
            GetAdaptersAddresses(AF_UNSPEC, GAA_FLAG_INCLUDE_PREFIX, NULL, NULL,
                                 &size);
            if (size > 0) {
                head = (PIP_ADAPTER_ADDRESSES)XMalloc_System(size);
                if (head) {
                    if (GetAdaptersAddresses(AF_UNSPEC,
                                             GAA_FLAG_INCLUDE_PREFIX, NULL,
                                             head, &size) == ERROR_SUCCESS) {
                        for (adapter = head; adapter; adapter = adapter->Next) {
                            if (adapter->AdapterName &&
                                _stricmp(adapter->AdapterName, guidStr) == 0) {
                                slot->ifIndex = (uint32_t)adapter->IfIndex;
                                if (adapter->FriendlyName) {
                                    char* name =
                                        wifiDupUtf8(adapter->FriendlyName);
                                    if (name && name[0])
                                        wifiCopyText(slot->name,
                                                     sizeof(slot->name), name);
                                    if (name) XFree_System(name);
                                }
                                break;
                            }
                        }
                    }
                    XFree_System(head);
                }
            }
        }
    }
    WlanFreeMemory(list);
    for (i = 0; i < XWIFI_MAX_ADAPTERS; ++i)
        if (s_slots[i].used) return true;
    return false;
}

/**
 * @brief 按适配器标识查槽位；未命中时重建一次表再查。
 */
static XWifiAdapterSlot* wifiFindSlot(const char* adapterId)
{
    int i;
    if (!adapterId || !adapterId[0]) return NULL;
    for (i = 0; i < XWIFI_MAX_ADAPTERS; ++i) {
        if (s_slots[i].used && strcmp(s_slots[i].id, adapterId) == 0)
            return &s_slots[i];
    }
    if (!wifiRefreshSlots()) return NULL;
    for (i = 0; i < XWIFI_MAX_ADAPTERS; ++i) {
        if (s_slots[i].used && strcmp(s_slots[i].id, adapterId) == 0)
            return &s_slots[i];
    }
    return NULL;
}

/** @brief 中心频点(MHz) → 信道号；无法判定返回 0。 */
static int wifiChannelFromFrequency(ULONG mhz)
{
    if (mhz == 0) return 0;
    if (mhz == 2484) return 14;
    if (mhz >= 2412 && mhz <= 2472) return (int)((mhz - 2407) / 5);
    if (mhz >= 5000 && mhz <= 5900) return (int)((mhz - 5000) / 5);
    if (mhz >= 5955 && mhz <= 7125) return (int)((mhz - 5950) / 5);
    return 0;
}

/** @brief 认证/加密算法 → 统一安全方式。 */
static XDeviceWiFiSecurity wifiMapSecurity(DOT11_AUTH_ALGORITHM auth,
                                           DOT11_CIPHER_ALGORITHM cipher)
{
    switch (auth) {
    case DOT11_AUTH_ALGO_80211_OPEN:
        return (cipher == DOT11_CIPHER_ALGO_NONE) ? XDeviceWiFi_SecurityOpen
                                                  : XDeviceWiFi_SecurityWep;
    case DOT11_AUTH_ALGO_80211_SHARED_KEY:
        return XDeviceWiFi_SecurityWep;
    case DOT11_AUTH_ALGO_WPA:
    case DOT11_AUTH_ALGO_WPA_PSK:
    case DOT11_AUTH_ALGO_WPA_NONE:
        return XDeviceWiFi_SecurityWpaPsk;
    case DOT11_AUTH_ALGO_RSNA:
    case DOT11_AUTH_ALGO_RSNA_PSK:
        return XDeviceWiFi_SecurityWpa2Psk;
    case DOT11_AUTH_ALGO_WPA3:
        return XDeviceWiFi_SecurityWpa2Enterprise;
    case DOT11_AUTH_ALGO_WPA3_SAE:
        return XDeviceWiFi_SecurityWpa3Sae;
    default:
        break;
    }
    return XDeviceWiFi_SecurityUnknown;
}

/** @brief 安全方式 → profile XML 的 authentication 串。 */
static const char* wifiProfileAuth(XDeviceWiFiSecurity security)
{
    switch (security) {
    case XDeviceWiFi_SecurityOpen:        return "open";
    case XDeviceWiFi_SecurityWep:         return "open";
    case XDeviceWiFi_SecurityWpaPsk:      return "WPAPSK";
    case XDeviceWiFi_SecurityWpa2Psk:     return "WPA2PSK";
    case XDeviceWiFi_SecurityWpa2Enterprise: return "WPA2";
    case XDeviceWiFi_SecurityWpa3Sae:     return "WPA3SAE";
    default:                              return "WPA2PSK";
    }
}

/** @brief 安全方式 → profile XML 的 encryption 串。 */
static const char* wifiProfileEncryption(XDeviceWiFiSecurity security)
{
    switch (security) {
    case XDeviceWiFi_SecurityOpen:        return "none";
    case XDeviceWiFi_SecurityWep:         return "WEP";
    case XDeviceWiFi_SecurityWpaPsk:      return "TKIP";
    case XDeviceWiFi_SecurityWpa2Enterprise: return "AES";
    default:                              return "AES";
    }
}

/** @brief XML 文本转义（& < > " '）。 */
static void wifiXmlEscape(const char* src, char* dst, size_t capacity)
{
    size_t out = 0;
    if (!dst || capacity == 0) return;
    dst[0] = '\0';
    if (!src) return;
    while (*src && out + 7 < capacity) {
        switch (*src) {
        case '&': memcpy(dst + out, "&amp;", 5);  out += 5; break;
        case '<': memcpy(dst + out, "&lt;", 4);   out += 4; break;
        case '>': memcpy(dst + out, "&gt;", 4);   out += 4; break;
        case '"': memcpy(dst + out, "&quot;", 6); out += 6; break;
        case '\'': memcpy(dst + out, "&apos;", 6); out += 6; break;
        default:  dst[out++] = *src; break;
        }
        src++;
    }
    dst[out] = '\0';
}

/** @brief SSID 文本 → 十六进制串（profile XML 的 <hex> 节点用）。 */
static void wifiSsidToHex(const char* ssid, char* dst, size_t capacity)
{
    static const char kHex[] = "0123456789ABCDEF";
    size_t i;
    size_t len;
    size_t out = 0;
    if (!dst || capacity == 0) return;
    dst[0] = '\0';
    if (!ssid) return;
    len = strlen(ssid);
    for (i = 0; i < len && out + 2 < capacity; ++i) {
        unsigned char c = (unsigned char)ssid[i];
        dst[out++] = kHex[(c >> 4) & 0x0F];
        dst[out++] = kHex[c & 0x0F];
    }
    dst[out] = '\0';
}

/**
 * @brief 组装 profile XML（UTF-8）。
 * @param ssid 网络名；@param passphrase 口令（开放网络可为 NULL）；
 *        @param security 安全方式。
 * @return 新分配的 UTF-8 串（调用方用 XFree_System 释放）；失败返回 NULL。
 */
static char* wifiBuildProfileXml(const char* ssid, const char* passphrase,
                                 XDeviceWiFiSecurity security)
{
    char name[512];
    char key[512];
    char hex[2 * 256 + 8];
    char* xml;
    size_t capacity = 4096;
    int written;
    wifiXmlEscape(ssid, name, sizeof(name));
    wifiXmlEscape(passphrase ? passphrase : "", key, sizeof(key));
    wifiSsidToHex(ssid, hex, sizeof(hex));
    xml = (char*)XMalloc_System(capacity);
    if (!xml) return NULL;
    /* 架构口径（v1 schema）：connectionType/connectionMode 是 WLANProfile
     * 的直接子节点（SSIDConfig 之后、MSM 之前），放进 SSIDConfig 会被
     * WlanSetProfile 以 WLAN_REASON_INVALID_PROFILE_SCHEMA(0x80001) 同步
     * 拒绝（netsh 报"配置文件格式无效"），表现为"连接发起失败"。 */
    if (security == XDeviceWiFi_SecurityOpen) {
        written = snprintf(xml, capacity,
            "<?xml version=\"1.0\" encoding=\"UTF-8\"?>"
            "<WLANProfile xmlns=\"http://www.microsoft.com/networking/WLAN/profile/v1\">"
            "<name>%s</name>"
            "<SSIDConfig><SSID><hex>%s</hex></SSID></SSIDConfig>"
            "<connectionType>ESS</connectionType>"
            "<connectionMode>manual</connectionMode>"
            "<MSM><security><authEncryption>"
            "<authentication>%s</authentication><encryption>%s</encryption>"
            "<useOneX>false</useOneX>"
            "</authEncryption></security></MSM></WLANProfile>",
            name, hex, wifiProfileAuth(security),
            wifiProfileEncryption(security));
    } else {
        written = snprintf(xml, capacity,
            "<?xml version=\"1.0\" encoding=\"UTF-8\"?>"
            "<WLANProfile xmlns=\"http://www.microsoft.com/networking/WLAN/profile/v1\">"
            "<name>%s</name>"
            "<SSIDConfig><SSID><hex>%s</hex></SSID></SSIDConfig>"
            "<connectionType>ESS</connectionType>"
            "<connectionMode>manual</connectionMode>"
            "<MSM><security><authEncryption>"
            "<authentication>%s</authentication><encryption>%s</encryption>"
            "<useOneX>false</useOneX>"
            "</authEncryption>"
            "<sharedKey><keyType>passPhrase</keyType><protected>false</protected>"
            "<keyMaterial>%s</keyMaterial></sharedKey>"
            "</security></MSM></WLANProfile>",
            name, hex, wifiProfileAuth(security),
            wifiProfileEncryption(security), key);
    }
    if (written < 0 || (size_t)written >= capacity) {
        XFree_System(xml);
        return NULL;
    }
    return xml;
}

/** @brief 6 字节 MAC → "AA:BB:CC:DD:EE:FF" 文本（UTF-8 堆分配）。 */
static char* wifiFormatBssid(const DOT11_MAC_ADDRESS bssid)
{
    static const char kHex[] = "0123456789ABCDEF";
    char* text = (char*)XMalloc_System(18);
    int i;
    if (!text) return NULL;
    for (i = 0; i < 6; ++i) {
        text[i * 3] = kHex[(bssid[i] >> 4) & 0x0F];
        text[i * 3 + 1] = kHex[bssid[i] & 0x0F];
        text[i * 3 + 2] = (i == 5) ? '\0' : ':';
    }
    return text;
}

/** @brief 释放单个网络条目（内部工具）。 */
static void wifiFreeNetworkEntry(XDeviceWiFiNetworkInfo* info)
{
    if (!info) return;
    if (info->ssid) XClassDelete(info->ssid);
    if (info->bssid) XClassDelete(info->bssid);
    XFree_System(info);
}

/** @brief 释放单个适配器条目（内部工具）。 */
static void wifiFreeAdapterEntry(XDeviceWiFiAdapterInfo* info)
{
    if (!info) return;
    if (info->id) XClassDelete(info->id);
    if (info->name) XClassDelete(info->name);
    if (info->description) XClassDelete(info->description);
    XFree_System(info);
}

/**
 * @brief 采集可见网络的加密方式（按 SSID 归档）。
 * @details 数据源 WlanGetAvailableNetworkList；失败则留空（安全方式未知）。
 */
static void wifiCollectSecurity(void)
{
    WLAN_AVAILABLE_NETWORK_LIST* available = NULL;
    DWORD i;
    s_securityCount = 0;
    memset(s_security, 0, sizeof(s_security));
    for (i = 0; i < XWIFI_MAX_ADAPTERS; ++i) {
        XWifiAdapterSlot* slot = &s_slots[i];
        if (!slot->used) continue;
        if (WlanGetAvailableNetworkList(s_wlanClient, &slot->guid, 0, NULL,
                                       &available) != ERROR_SUCCESS ||
            !available)
            continue;
        {
            DWORD k;
            for (k = 0; k < available->dwNumberOfItems &&
                        s_securityCount < XWIFI_SECURITY_MAX;
                 ++k) {
                WLAN_AVAILABLE_NETWORK* net = &available->Network[k];
                XWifiSecurityEntry* entry = &s_security[s_securityCount++];
                DWORD len = net->dot11Ssid.uSSIDLength;
                if (len > XWIFI_SSID_MAX - 1) len = XWIFI_SSID_MAX - 1;
                memcpy(entry->ssid, net->dot11Ssid.ucSSID, len);
                entry->ssid[len] = '\0';
                entry->security = wifiMapSecurity(
                    net->dot11DefaultAuthAlgorithm,
                    net->dot11DefaultCipherAlgorithm);
                entry->secured = net->bSecurityEnabled ? true : false;
                entry->valid = true;
            }
        }
        WlanFreeMemory(available);
        available = NULL;
    }
}

/** @brief 按 SSID 查安全方式；未命中返回 Unknown / 未加密。 */
static void wifiLookupSecurity(const char* ssid, XDeviceWiFiSecurity* outSecurity,
                               bool* outSecured)
{
    int i;
    *outSecurity = XDeviceWiFi_SecurityUnknown;
    *outSecured = false;
    if (!ssid) return;
    for (i = 0; i < s_securityCount; ++i) {
        if (!s_security[i].valid) continue;
        if (strcmp(s_security[i].ssid, ssid) == 0) {
            *outSecurity = s_security[i].security;
            *outSecured = s_security[i].secured;
            return;
        }
    }
}

/**
 * @brief 从 BSS 条目的信息元素（IE）块解析信道号。
 * @details 部分驱动不填 WLAN_BSS_ENTRY.ulChCenterFrequency（实测为 0），
 *          此时从 IE 块里找 DS Parameter IE（ID=3）或 HT Operation IE
 *          （ID=61）的首字节低 4 位。
 * @param entry BSS 条目。
 * @return 信道号；未解析到返回 0。
 */
static int wifiChannelFromIe(const WLAN_BSS_ENTRY* entry)
{
    const BYTE* ie;
    DWORD size;
    DWORD i = 0;
    if (!entry || entry->ulIeSize < 2) return 0;
    ie = (const BYTE*)entry + entry->ulIeOffset;
    size = entry->ulIeSize;
    while (i + 2 <= size) {
        BYTE id = ie[i];
        BYTE len = ie[i + 1];
        if (i + 2 + len > size) break;
        if (id == 3 && len >= 1) return (int)ie[i + 2];
        if (id == 61 && len >= 1) return (int)(ie[i + 2] & 0x0F);
        i += 2 + len;
    }
    return 0;
}

/**
 * @brief 采集可见网络列表（BSS 列表为主，可用网络列表补安全方式）。
 * @param slot 适配器槽位。
 * @return 新建的 XVector*（元素为 XDeviceWiFiNetworkInfo*）；失败返回 NULL。
 */
static XVector* wifiCollectNetworks(XWifiAdapterSlot* slot)
{
    WLAN_BSS_LIST* bssList = NULL;
    XVector* networks;
    DWORD i;
    if (WlanGetNetworkBssList(s_wlanClient, &slot->guid, NULL,
                              dot11_BSS_type_infrastructure, FALSE, NULL,
                              &bssList) != ERROR_SUCCESS || !bssList)
        return NULL;
    wifiCollectSecurity();
    networks = XVector_Create(XDeviceWiFiNetworkInfo*);
    if (!networks) {
        WlanFreeMemory(bssList);
        return NULL;
    }
    for (i = 0; i < bssList->dwNumberOfItems; ++i) {
        WLAN_BSS_ENTRY* entry = &bssList->wlanBssEntries[i];
        XDeviceWiFiNetworkInfo* info =
            (XDeviceWiFiNetworkInfo*)XMalloc_System(sizeof(*info));
        char* bssid;
        char ssidText[XWIFI_SSID_MAX];
        DWORD len = entry->dot11Ssid.uSSIDLength;
        if (!info) continue;
        memset(info, 0, sizeof(*info));
        if (len > XWIFI_SSID_MAX - 1) len = XWIFI_SSID_MAX - 1;
        memcpy(ssidText, entry->dot11Ssid.ucSSID, len);
        ssidText[len] = '\0';
        /* 隐藏网络不广播网络名：SSID 为空时 ssid 留 NULL。 */
        if (ssidText[0]) info->ssid = XString_create_utf8(ssidText);
        bssid = wifiFormatBssid(entry->dot11Bssid);
        if (bssid) {
            info->bssid = XString_create_utf8(bssid);
            XFree_System(bssid);
        }
        info->signalStrength = (int)entry->uLinkQuality;
        if (info->signalStrength < 0) info->signalStrength = -1;
        info->channel = wifiChannelFromFrequency(entry->ulChCenterFrequency);
        if (info->channel == 0) info->channel = wifiChannelFromIe(entry);
        wifiLookupSecurity(ssidText, &info->security, &info->secured);
        XVector_push_back_1_base(networks, &info);
    }
    WlanFreeMemory(bssList);
    return networks;
}

/* =========================================================================
 * 平台钩子
 * ========================================================================= */

bool XDeviceWiFi_platformOpen(void)
{
    if (!wifiEnsureClient()) return false;
    s_clientRefs++;
    return true;
}

void XDeviceWiFi_platformClose(void)
{
    /* 引用计数归零才真正释放：多会话并存时，先关闭的会话不得释放他人
     * 仍在用的客户端句柄。 */
    if (s_clientRefs > 0) s_clientRefs--;
    if (s_clientRefs > 0) return;
    /* 销毁顺序对齐统一设备抽象 §11.4：先取消在途扫描并屏蔽迟到通知
     * （槽位清空后回调再到达也无处可写），再注销事件源、关闭原生句柄。 */
    wifiClearSlots();
    s_notifyRegistered = false;
    if (s_wlanClient) {
        WlanCloseHandle(s_wlanClient, NULL);
        s_wlanClient = NULL;
    }
}

/* =========================================================================
 * 平台叶子实现（经设备虚槽 VXDeviceWiFi_* 调用；业务层不得直接调用）
 * ========================================================================= */

bool XDeviceWiFi_platformCapabilities(uint32_t* outCapabilities)
{
    if (!outCapabilities) return false;
    if (!wifiEnsureClient()) {
        *outCapabilities = 0;
        return false;
    }
    *outCapabilities = XDeviceWiFi_CapScan | XDeviceWiFi_CapConnect |
                       XDeviceWiFi_CapDisconnect | XDeviceWiFi_CapProfile;
    return true;
}

bool XDeviceWiFi_platformEnumAdapters(XVector** outAdapters)
{
    XVector* adapters;
    int i;
    if (!outAdapters) return false;
    *outAdapters = NULL;
    adapters = XVector_Create(XDeviceWiFiAdapterInfo*);
    if (!adapters) return false;
    *outAdapters = adapters;
    if (!wifiEnsureClient()) return true;
    if (!wifiRefreshSlots()) return true;
    for (i = 0; i < XWIFI_MAX_ADAPTERS; ++i) {
        XWifiAdapterSlot* slot = &s_slots[i];
        XDeviceWiFiAdapterInfo* info;
        if (!slot->used) continue;
        info = (XDeviceWiFiAdapterInfo*)XMalloc_System(sizeof(*info));
        if (!info) continue;
        memset(info, 0, sizeof(*info));
        info->id = XString_create_utf8(slot->id);
        info->name = XString_create_utf8(slot->name[0] ? slot->name : slot->id);
        info->description = slot->description[0]
                                ? XString_create_utf8(slot->description)
                                : NULL;
        info->ifIndex = slot->ifIndex;
        info->capabilities = XDeviceWiFi_CapScan | XDeviceWiFi_CapConnect |
                             XDeviceWiFi_CapDisconnect | XDeviceWiFi_CapProfile;
        info->mode = XDeviceWiFi_ModeStation;
        XVector_push_back_1_base(adapters, &info);
    }
    return true;
}

bool XDeviceWiFi_platformStartScan(const char* adapterId)
{
    XWifiAdapterSlot* slot = wifiFindSlot(adapterId);
    if (!slot) return false;
    if (slot->scanState == XDeviceWiFi_ScanRunning) return false;
    if (WlanScan(s_wlanClient, &slot->guid, NULL, NULL, NULL) != ERROR_SUCCESS) {
        slot->scanState = XDeviceWiFi_ScanFailed;
        return false;
    }
    slot->scanState = XDeviceWiFi_ScanRunning;
    slot->scanStartMs = GetTickCount();
    slot->scanNotified = 0;
    return true;
}

XDeviceWiFiScanState XDeviceWiFi_platformScanState(const char* adapterId)
{
    XWifiAdapterSlot* slot = wifiFindSlot(adapterId);
    if (!slot) return XDeviceWiFi_ScanIdle;
    if (slot->scanState != XDeviceWiFi_ScanRunning) return slot->scanState;
    if (slot->scanNotified) {
        slot->scanState = XDeviceWiFi_ScanCompleted;
        return slot->scanState;
    }
    if ((GetTickCount() - slot->scanStartMs) >= XWIFI_SCAN_TIMEOUT_MS) {
        /* 驱动未在规范时限内回报：按超时收口（结果可能为空集）。 */
        slot->scanState = XDeviceWiFi_ScanCompleted;
        return slot->scanState;
    }
    return XDeviceWiFi_ScanRunning;
}

bool XDeviceWiFi_platformTakeScanResults(const char* adapterId,
                                         XVector** outNetworks)
{
    XWifiAdapterSlot* slot = wifiFindSlot(adapterId);
    XVector* networks;
    if (!outNetworks) return false;
    *outNetworks = NULL;
    if (!slot) return false;
    if (slot->scanState != XDeviceWiFi_ScanCompleted) return false;
    slot->scanState = XDeviceWiFi_ScanIdle;
    slot->scanNotified = 0;
    networks = wifiCollectNetworks(slot);
    if (!networks) return false;
    *outNetworks = networks;
    return true;
}

bool XDeviceWiFi_platformStartConnect(const char* adapterId, const char* ssid,
                                      const char* passphrase,
                                      XDeviceWiFiSecurity security)
{
    XWifiAdapterSlot* slot = wifiFindSlot(adapterId);
    WLAN_CONNECTION_PARAMETERS params;
    wchar_t* profileWide = NULL;
    wchar_t* wideSsid = NULL;
    char* profileXml = NULL;
    DWORD reasonCode = 0;
    if (!slot || !ssid || !ssid[0]) return false;
    profileXml = wifiBuildProfileXml(ssid, passphrase, security);
    if (!profileXml) return false;
    profileWide = wifiDupWide(profileXml);
    XFree_System(profileXml);
    if (!profileWide) return false;
    /* dwFlags=0：按用户级 profile 写入（写系统级 profile 才需管理员）；
     * bOverwrite=TRUE：同名 profile 覆盖。 */
    if (WlanSetProfile(s_wlanClient, &slot->guid, 0, profileWide, NULL, TRUE,
                       NULL, &reasonCode) != ERROR_SUCCESS) {
        XFree_System(profileWide);
        slot->connectState = XDeviceWiFi_ConnectFailed;
        return false;
    }
    XFree_System(profileWide);
    memset(&params, 0, sizeof(params));
    params.wlanConnectionMode = wlan_connection_mode_profile;
    /* profile 名 = SSID 原文（与 profile XML 的 <name> 一致），需宽字符。 */
    wideSsid = wifiDupWide(ssid);
    params.strProfile = wideSsid;
    params.pDot11Ssid = NULL;
    params.pDesiredBssidList = NULL;
    params.dot11BssType = dot11_BSS_type_any;
    params.dwFlags = 0;
    if (WlanConnect(s_wlanClient, &slot->guid, &params, NULL) != ERROR_SUCCESS) {
        if (wideSsid) XFree_System(wideSsid);
        slot->connectState = XDeviceWiFi_ConnectFailed;
        return false;
    }
    if (wideSsid) XFree_System(wideSsid);
    slot->connectState = XDeviceWiFi_ConnectConnecting;
    slot->connectStartMs = GetTickCount();
    return true;
}

XDeviceWiFiConnectState XDeviceWiFi_platformConnectState(const char* adapterId)
{
    XWifiAdapterSlot* slot = wifiFindSlot(adapterId);
    WLAN_CONNECTION_ATTRIBUTES* attributes = NULL;
    DWORD dataSize = 0;
    WLAN_OPCODE_VALUE_TYPE opcodeValueType = 0;
    if (!slot) return XDeviceWiFi_ConnectIdle;
    if (slot->connectState != XDeviceWiFi_ConnectConnecting)
        return slot->connectState;
    /* 数据缓冲由 WlanQueryInterface 自行分配（出参是 PVOID*），取值后
     * 须 WlanFreeMemory；传自备缓冲会被当作"接收缓冲指针的指针"，首 8
     * 字节被写入堆地址（isState 恒为垃圾），连接成功也判不出来，且每次
     * 调用泄漏一块 604 字节的 RPC 缓冲。 */
    if (WlanQueryInterface(s_wlanClient, &slot->guid,
                           wlan_intf_opcode_current_connection, NULL, &dataSize,
                           (PVOID*)&attributes, &opcodeValueType) ==
        ERROR_SUCCESS && attributes) {
        if (attributes->isState == wlan_interface_state_connected)
            slot->connectState = XDeviceWiFi_ConnectConnected;
        WlanFreeMemory(attributes);
        if (slot->connectState == XDeviceWiFi_ConnectConnected)
            return slot->connectState;
    }
    if ((GetTickCount() - slot->connectStartMs) >= XWIFI_CONNECT_TIMEOUT_MS) {
        slot->connectState = XDeviceWiFi_ConnectFailed;
        return slot->connectState;
    }
    return XDeviceWiFi_ConnectConnecting;
}

bool XDeviceWiFi_platformQueryConnection(const char* adapterId,
                                         XDeviceWiFiConnectionInfo* outInfo)
{
    XWifiAdapterSlot* slot = wifiFindSlot(adapterId);
    WLAN_CONNECTION_ATTRIBUTES* attributes = NULL;
    DWORD dataSize = 0;
    WLAN_OPCODE_VALUE_TYPE opcodeValueType = 0;
    if (!slot || !outInfo) return false;
    memset(outInfo, 0, sizeof(*outInfo));
    if (WlanQueryInterface(s_wlanClient, &slot->guid,
                           wlan_intf_opcode_current_connection, NULL, &dataSize,
                           (PVOID*)&attributes, &opcodeValueType) !=
            ERROR_SUCCESS ||
        !attributes)
        return false;
    if (attributes->isState != wlan_interface_state_connected) {
        WlanFreeMemory(attributes);
        outInfo->state = XDeviceWiFi_ConnectIdle;
        outInfo->signalStrength = -1;
        return false;
    }
    {
        const DOT11_SSID* ssid =
            &attributes->wlanAssociationAttributes.dot11Ssid;
        char ssidText[XWIFI_SSID_MAX];
        DWORD len = ssid->uSSIDLength;
        if (len > XWIFI_SSID_MAX - 1) len = XWIFI_SSID_MAX - 1;
        memcpy(ssidText, ssid->ucSSID, len);
        ssidText[len] = '\0';
        if (ssidText[0]) outInfo->ssid = XString_create_utf8(ssidText);
    }
    {
        char* bssid = wifiFormatBssid(
            attributes->wlanAssociationAttributes.dot11Bssid);
        if (bssid) {
            outInfo->bssid = XString_create_utf8(bssid);
            XFree_System(bssid);
        }
    }
    outInfo->signalStrength =
        (int)attributes->wlanAssociationAttributes.wlanSignalQuality;
    if (outInfo->signalStrength < 0) outInfo->signalStrength = -1;
    outInfo->security = wifiMapSecurity(
        attributes->wlanSecurityAttributes.dot11AuthAlgorithm,
        attributes->wlanSecurityAttributes.dot11CipherAlgorithm);
    WlanFreeMemory(attributes);
    outInfo->state = XDeviceWiFi_ConnectConnected;
    return true;
}

bool XDeviceWiFi_platformDisconnect(const char* adapterId)
{
    XWifiAdapterSlot* slot = wifiFindSlot(adapterId);
    if (!slot) return false;
    if (WlanDisconnect(s_wlanClient, &slot->guid, NULL) != ERROR_SUCCESS)
        return false;
    slot->connectState = XDeviceWiFi_ConnectIdle;
    return true;
}

bool XDeviceWiFi_platformForgetProfile(const char* adapterId, const char* ssid)
{
    XWifiAdapterSlot* slot = wifiFindSlot(adapterId);
    wchar_t* wideSsid;
    bool ok;
    if (!slot || !ssid || !ssid[0]) return false;
    wideSsid = wifiDupWide(ssid);
    if (!wideSsid) return false;
    ok = (WlanDeleteProfile(s_wlanClient, &slot->guid, wideSsid, NULL) ==
          ERROR_SUCCESS);
    XFree_System(wideSsid);
    return ok;
}

#endif /* _WIN32 */

#endif /* XNETWORK_ON && XNETWORK_WIFI_ON */
