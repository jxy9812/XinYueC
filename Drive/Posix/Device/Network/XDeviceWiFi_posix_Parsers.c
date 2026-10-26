/**
 * @file       XDeviceWiFi_posix_Parsers.c
 * @brief      POSIX 无线后端纯解析函数实现（无系统调用、无全局状态）。
 * @details    契约见 XDeviceWiFi_posix_Parsers.h。本文件只做文本/字节流解析，
 *             不打开任何文件、不发起 ioctl——便于用合成数据做单元测试
 *             （Test/XDeviceTest/xdevice_wifi_posix_parsers_test.c）。
 *
 *             WEXT 事件流布局（内核 wext-core.c 的 iwe_stream_add_point）：
 *               展开布局（同宽用户态/内核）：
 *                 [len:2][cmd:2][pad:4][length:2][flags:2][pad:4][extra...]
 *                 → lcp=8，点事件数据偏移 16，定长事件载荷偏移 8
 *               紧凑布局（64 位内核 + 32 位用户态，IW_REQUEST_FLAG_COMPAT）：
 *                 [len:2][cmd:2][length:2][flags:2][extra...]
 *                 → lcp=4，点事件数据偏移 8，定长事件载荷偏移 4
 *             即：**点事件数据紧跟点事件头之后**，头内不含指针字段（内核已
 *             把 pointer 置空并省略），故不能按 struct iw_point 的 sizeof
 *             直接步进。
 */
#include "XNetwork_config.h"

#if XDEVICE_ON && XDEVICE_WIFI_ON

#include "XDeviceWiFi_posix_Parsers.h"
#include "XDeviceWiFi.h"
#include "XMemory.h"
#include "XString.h"
#include "XVector.h"

#include <stdlib.h>
#include <string.h>

#if defined(__linux__)
#include <linux/wireless.h>
#include <sys/socket.h>
#endif

/* =========================================================================
 * 一、数值换算
 * ========================================================================= */

int XDevWifiPosix_ChannelFromMhz(int mhz)
{
    if (mhz <= 0) return 0;
    if (mhz == 2484) return 14;
    if (mhz >= 2412 && mhz <= 2472) return (mhz - 2407) / 5;
    if (mhz >= 5000 && mhz <= 5900) return (mhz - 5000) / 5;
    if (mhz >= 5955 && mhz <= 7125) return (mhz - 5950) / 5;
    return 0;
}

int XDevWifiPosix_DbmToPercent(int dbm)
{
    if (dbm <= -100) return 0;
    if (dbm >= -50) return 100;
    return (dbm + 100) * 2;
}

int XDevWifiPosix_QualityToPercent(int quality)
{
    int percent;
    if (quality <= 0) return 0;
    percent = (quality > 100) ? (quality * 100 / 255) : quality;
    if (percent > 100) percent = 100;
    return percent;
}

/* =========================================================================
 * 二、安全方式
 * ========================================================================= */

XDeviceWiFiSecurity XDevWifiPosix_SecurityFromText(const char* text)
{
    if (!text || !text[0]) return XDeviceWiFi_SecurityOpen;
    /* 顺序即强度：WPA3/SAE 与 802.1X 先判，避免被 "WPA" 子串截胡。 */
    if (strstr(text, "SAE") || strstr(text, "WPA3"))
        return XDeviceWiFi_SecurityWpa3Sae;
    if (strstr(text, "802.1X") || strstr(text, "EAP"))
        return XDeviceWiFi_SecurityWpa2Enterprise;
    if (strstr(text, "WEP")) return XDeviceWiFi_SecurityWep;
    if (strstr(text, "WPA2") || strstr(text, "RSN"))
        return XDeviceWiFi_SecurityWpa2Psk;
    if (strstr(text, "WPA")) return XDeviceWiFi_SecurityWpaPsk;
    /* 只有 [ESS]/[IBSS]/"WPA1 WPA2" 之外的能力串：按开放处理。 */
    return XDeviceWiFi_SecurityOpen;
}

/* =========================================================================
 * 三、nmcli -t 转义格式
 * ========================================================================= */

void XDevWifiPosix_UnescapeTerse(char* text)
{
    char* src;
    char* dst;
    if (!text) return;
    src = text;
    dst = text;
    while (*src) {
        if (*src == '\\' && src[1]) {
            ++src;
            *dst++ = *src++;
        } else {
            *dst++ = *src++;
        }
    }
    *dst = '\0';
}

int XDevWifiPosix_SplitTerse(char* line, char** fields, int maxFields)
{
    int count = 0;
    char* cursor;
    if (!line || !fields || maxFields <= 0) return 0;
    cursor = line;
    while (count < maxFields && cursor) {
        char* scan = cursor;
        fields[count++] = cursor;
        while (*scan) {
            if (*scan == '\\' && scan[1]) {
                scan += 2;
                continue;
            }
            if (*scan == ':') break;
            ++scan;
        }
        if (*scan == ':') {
            *scan = '\0';
            cursor = scan + 1;
        } else {
            cursor = NULL;
        }
    }
    {
        int i;
        for (i = 0; i < count; ++i) XDevWifiPosix_UnescapeTerse(fields[i]);
    }
    return count;
}

/* =========================================================================
 * 四、结果条目构造
 * ========================================================================= */

/** @brief 新建并追加一条扫描结果；失败静默跳过（解析不因单条失败中断）。 */
static void xdevwifi_addNetwork(XVector* networks, const char* ssid,
                                const char* bssid, int signal, int channel,
                                XDeviceWiFiSecurity security)
{
    XDeviceWiFiNetworkInfo* info;
    if (!networks) return;
    info = (XDeviceWiFiNetworkInfo*)XCalloc_System(1, sizeof(*info));
    if (!info) return;
    if (ssid && ssid[0]) info->ssid = XString_create_utf8(ssid);
    if (bssid && bssid[0]) info->bssid = XString_create_utf8(bssid);
    info->signalStrength = (signal < 0) ? -1 : signal;
    info->channel = channel;
    info->security = security;
    info->secured = (security != XDeviceWiFi_SecurityOpen);
    if (!XVector_push_back_1_base(networks, &info)) {
        if (info->ssid) XClassDelete(info->ssid);
        if (info->bssid) XClassDelete(info->bssid);
        XFree_System(info);
    }
}

/* =========================================================================
 * 五、扫描结果解析
 * ========================================================================= */

bool XDevWifiPosix_ParseWpaScanResults(const char* text, XVector** outNetworks)
{
    XVector* networks;
    const char* line;
    bool headerSkipped = false;

    if (!outNetworks) return false;
    *outNetworks = NULL;
    if (!text) return false;
    networks = XVector_Create(XDeviceWiFiNetworkInfo*);
    if (!networks) return false;

    line = text;
    while (*line) {
        const char* end = strchr(line, '\n');
        size_t lineLen = end ? (size_t)(end - line) : strlen(line);
        /* 跳过空行与表头（表头首段是字面量 "bssid"）。 */
        if (lineLen > 0) {
            if (!headerSkipped) {
                if (lineLen > 5 && strncmp(line, "bssid", 5) == 0)
                    headerSkipped = true;
                else if (strchr(line, '\t') == NULL)
                    headerSkipped = true;
            }
        }
        if (headerSkipped && lineLen > 0 &&
            !(lineLen > 5 && strncmp(line, "bssid", 5) == 0)) {
            char row[512];
            size_t copy = lineLen < sizeof(row) - 1 ? lineLen : sizeof(row) - 1;
            char* fields[5];
            int count = 0;
            char* cursor;
            memcpy(row, line, copy);
            row[copy] = '\0';
            cursor = row;
            fields[count++] = cursor;
            while (count < 5) {
                char* tab = strchr(cursor, '\t');
                if (!tab) break;
                *tab = '\0';
                cursor = tab + 1;
                fields[count++] = cursor;
            }
            if (count == 5) {
                int frequency = atoi(fields[1]);
                int signalDbm = atoi(fields[2]);
                xdevwifi_addNetwork(networks, fields[4], fields[0],
                                    XDevWifiPosix_DbmToPercent(signalDbm),
                                    XDevWifiPosix_ChannelFromMhz(frequency),
                                    XDevWifiPosix_SecurityFromText(fields[3]));
            }
        }
        if (!end) break;
        line = end + 1;
    }
    *outNetworks = networks;
    return true;
}

bool XDevWifiPosix_ParseNmWifiList(const char* text, XVector** outNetworks)
{
    XVector* networks;
    const char* line;

    if (!outNetworks) return false;
    *outNetworks = NULL;
    if (!text) return false;
    networks = XVector_Create(XDeviceWiFiNetworkInfo*);
    if (!networks) return false;

    line = text;
    while (*line) {
        const char* end = strchr(line, '\n');
        size_t lineLen = end ? (size_t)(end - line) : strlen(line);
        if (lineLen > 0) {
            char row[512];
            size_t copy = lineLen < sizeof(row) - 1 ? lineLen : sizeof(row) - 1;
            char* fields[5];
            memcpy(row, line, copy);
            row[copy] = '\0';
            if (XDevWifiPosix_SplitTerse(row, fields, 5) == 5) {
                int signal = atoi(fields[2]);
                int channel = atoi(fields[3]);
                if (signal < 0 || signal > 100) signal = -1;
                if (channel < 0) channel = 0;
                xdevwifi_addNetwork(networks, fields[0], fields[1], signal,
                                    channel,
                                    XDevWifiPosix_SecurityFromText(fields[4]));
            }
        }
        if (!end) break;
        line = end + 1;
    }
    *outNetworks = networks;
    return true;
}

bool XDevWifiPosix_StatusValue(const char* reply, const char* key, char* out,
                               size_t cap)
{
    size_t keyLen;
    const char* line;
    if (!reply || !key || !out || cap == 0) return false;
    keyLen = strlen(key);
    out[0] = '\0';
    line = reply;
    while (*line) {
        const char* end = strchr(line, '\n');
        size_t lineLen = end ? (size_t)(end - line) : strlen(line);
        if (lineLen > keyLen && strncmp(line, key, keyLen) == 0 &&
            line[keyLen] == '=') {
            size_t valueLen = lineLen - keyLen - 1;
            if (valueLen >= cap) valueLen = cap - 1;
            memcpy(out, line + keyLen + 1, valueLen);
            out[valueLen] = '\0';
            return true;
        }
        if (!end) break;
        line = end + 1;
    }
    return false;
}

bool XDevWifiPosix_BuildWpaValue(const char* text, char* out, size_t cap)
{
    static const char kHex[] = "0123456789abcdef";
    size_t i;
    size_t length;
    bool plain = true;

    if (!text || !out || cap == 0) return false;
    length = strlen(text);
    if (length == 0) return false;
    for (i = 0; i < length; ++i) {
        unsigned char c = (unsigned char)text[i];
        if (c < 0x20 || c > 0x7e || c == '"' || c == '\\') {
            plain = false;
            break;
        }
    }
    if (plain) {
        if (length + 3 > cap) return false;
        out[0] = '"';
        memcpy(out + 1, text, length);
        out[length + 1] = '"';
        out[length + 2] = '\0';
        return true;
    }
    if (length * 2 + 1 > cap) return false;
    for (i = 0; i < length; ++i) {
        unsigned char c = (unsigned char)text[i];
        out[i * 2] = kHex[c >> 4];
        out[i * 2 + 1] = kHex[c & 0x0F];
    }
    out[length * 2] = '\0';
    return true;
}

/* =========================================================================
 * 六、WEXT 事件流
 * ========================================================================= */

#if defined(__linux__)

/** @brief 展开布局的事件前缀长度（len/cmd + 对齐填充）。 */
#define XDEVWIFI_LCP_EXPANDED 8u

/** @brief 紧凑布局的事件前缀长度。 */
#define XDEVWIFI_LCP_PACKED 4u

/** @brief 点事件在展开布局下的数据偏移（len/cmd + length/flags + 填充）。 */
#define XDEVWIFI_POINT_EXPANDED 16u

/** @brief 点事件在紧凑布局下的数据偏移（len/cmd + length/flags）。 */
#define XDEVWIFI_POINT_PACKED 8u

bool XDevWifiPosix_WextIsPacked(const unsigned char* buf, size_t len)
{
    size_t off = 0;
    if (!buf) return false;
    while (off + 4 <= len) {
        uint16_t cmd = 0;
        uint16_t eventLen = 0;
        size_t payload = 0;
        /* struct iw_event 的字段顺序是 len 在前、cmd 在后（len@0 / cmd@2）。 */
        memcpy(&eventLen, buf + off, sizeof(eventLen));
        memcpy(&cmd, buf + off + 2, sizeof(cmd));
        if (eventLen < 4 || off + eventLen > len) break;
        switch (cmd) {
        case SIOCGIWAP:   payload = sizeof(struct sockaddr); break;
        case SIOCGIWFREQ: payload = sizeof(struct iw_freq); break;
        case SIOCGIWMODE: payload = sizeof(__u32); break;
        case IWEVQUAL:    payload = sizeof(struct iw_quality); break;
        default: payload = 0; break;
        }
        if (payload != 0) {
            if (eventLen == XDEVWIFI_LCP_EXPANDED + payload) return false;
            if (eventLen == XDEVWIFI_LCP_PACKED + payload) return true;
        }
        off += eventLen;
    }
    return false; /* 无法判定：按展开布局处理（同宽系统为主流）。 */
}

/** @brief 点事件头内的 length 字段偏移。 */
static size_t xdevwifi_pointLengthOff(bool packed)
{
    return packed ? XDEVWIFI_LCP_PACKED : XDEVWIFI_LCP_EXPANDED;
}

/** @brief 点事件数据（extra）起始偏移。 */
static size_t xdevwifi_pointDataOff(bool packed)
{
    return packed ? XDEVWIFI_POINT_PACKED : XDEVWIFI_POINT_EXPANDED;
}

/** @brief 定长事件载荷偏移（与事件头同一偏移）。 */
static size_t xdevwifi_fixedPayloadOff(bool packed)
{
    return packed ? XDEVWIFI_LCP_PACKED : XDEVWIFI_LCP_EXPANDED;
}

/** @brief 收口当前 BSS 记录（有 BSSID 才输出）。 */
static void xdevwifi_flushBss(XVector* networks, const char* ssid,
                              const char* bssid, bool haveBssid, int signal,
                              int channel, bool secured)
{
    if (!haveBssid) return;
    /* WEXT 无法区分 WEP / WPA / WPA2：加密但未知一律记 Unknown（secured=true），
     * 不臆测具体算法；开放网络如实记 Open。 */
    xdevwifi_addNetwork(networks, ssid, bssid, signal, channel,
                        secured ? XDeviceWiFi_SecurityUnknown
                                : XDeviceWiFi_SecurityOpen);
}

bool XDevWifiPosix_ParseWextScan(const unsigned char* buf, size_t len,
                                 XVector** outNetworks)
{
    XVector* networks;
    bool packed;
    size_t off = 0;
    char curSsid[64];
    char curBssid[18];
    bool haveBssid = false;
    int curSignal = -1;
    int curChannel = 0;
    bool curSecured = false;

    if (!outNetworks) return false;
    *outNetworks = NULL;
    if (!buf || len == 0) return false;
    networks = XVector_Create(XDeviceWiFiNetworkInfo*);
    if (!networks) return false;

    packed = XDevWifiPosix_WextIsPacked(buf, len);
    curSsid[0] = '\0';
    curBssid[0] = '\0';

    while (off + 4 <= len) {
        uint16_t cmd = 0;
        uint16_t eventLen = 0;
        /* struct iw_event 的字段顺序是 len 在前、cmd 在后（len@0 / cmd@2）。 */
        memcpy(&eventLen, buf + off, sizeof(eventLen));
        memcpy(&cmd, buf + off + 2, sizeof(cmd));
        if (eventLen < 4 || off + eventLen > len) break;

        switch (cmd) {
        case SIOCGIWAP:
        {
            struct sockaddr address;
            const unsigned char* mac;
            if (xdevwifi_fixedPayloadOff(packed) + sizeof(address) > eventLen)
                break;
            memset(&address, 0, sizeof(address));
            memcpy(&address, buf + off + xdevwifi_fixedPayloadOff(packed),
                   sizeof(address));
            mac = (const unsigned char*)address.sa_data;
            if (mac[0] || mac[1] || mac[2] || mac[3] || mac[4] || mac[5]) {
                /* 新 BSS 开始：先收口上一条。 */
                xdevwifi_flushBss(networks, curSsid, curBssid, haveBssid,
                                  curSignal, curChannel, curSecured);
                snprintf(curBssid, sizeof(curBssid),
                         "%02X:%02X:%02X:%02X:%02X:%02X", mac[0], mac[1],
                         mac[2], mac[3], mac[4], mac[5]);
                haveBssid = true;
                curSsid[0] = '\0';
                curSignal = -1;
                curChannel = 0;
                curSecured = false;
            }
            break;
        }
        case SIOCGIWESSID:
        {
            /* dataOff/lengthOff 均为**相对本事件起点**的偏移，取值时须加 off。 */
            size_t dataOff = off + xdevwifi_pointDataOff(packed);
            size_t dataLen = 0;
            if (dataOff <= off + eventLen) {
                uint16_t declared = 0;
                memcpy(&declared, buf + off + xdevwifi_pointLengthOff(packed),
                       sizeof(declared));
                dataLen = (size_t)declared;
                if (dataLen > off + eventLen - dataOff)
                    dataLen = off + eventLen - dataOff;
                if (dataLen >= sizeof(curSsid)) dataLen = sizeof(curSsid) - 1;
                if (dataLen > 0) {
                    memcpy(curSsid, buf + dataOff, dataLen);
                    curSsid[dataLen] = '\0';
                } else {
                    curSsid[0] = '\0';
                }
            }
            break;
        }
        case IWEVQUAL:
        {
            struct iw_quality quality;
            size_t payload = xdevwifi_fixedPayloadOff(packed);
            if (payload + sizeof(quality) > eventLen) break;
            memset(&quality, 0, sizeof(quality));
            memcpy(&quality, buf + off + payload, sizeof(quality));
            if ((quality.updated & IW_QUAL_LEVEL_UPDATED) &&
                (quality.updated & IW_QUAL_DBM)) {
                int level = (int)quality.level;
                if (level > 128) level -= 256; /* 老驱动用无符号 dBm。 */
                curSignal = XDevWifiPosix_DbmToPercent(level);
            } else if (quality.updated & IW_QUAL_QUAL_UPDATED) {
                curSignal = XDevWifiPosix_QualityToPercent((int)quality.qual);
            } else if (quality.updated & IW_QUAL_LEVEL_UPDATED) {
                curSignal = XDevWifiPosix_QualityToPercent((int)quality.level);
            }
            break;
        }
        case SIOCGIWFREQ:
        {
            struct iw_freq freq;
            size_t payload = xdevwifi_fixedPayloadOff(packed);
            if (payload + sizeof(freq) > eventLen) break;
            memset(&freq, 0, sizeof(freq));
            memcpy(&freq, buf + off + payload, sizeof(freq));
            if (freq.e == 0 && freq.m > 0 && freq.m < 1000) {
                curChannel = (int)freq.m; /* 驱动直接给信道号。 */
            } else if (freq.m > 0) {
                long long mhz = (long long)freq.m;
                int exponent = (int)freq.e;
                while (exponent > 6) { mhz /= 10; --exponent; }
                while (exponent < 6) { mhz *= 10; ++exponent; }
                if (mhz > 0 && mhz < 100000)
                    curChannel = XDevWifiPosix_ChannelFromMhz((int)mhz);
            }
            break;
        }
        case SIOCGIWENCODE:
        {
            size_t lengthOff = xdevwifi_pointLengthOff(packed);
            if (lengthOff + 4 <= eventLen) {
                uint16_t flags = 0;
                memcpy(&flags, buf + off + lengthOff + 2, sizeof(flags));
                curSecured = ((flags & IW_ENCODE_DISABLED) == 0);
            }
            break;
        }
        default:
            break;
        }
        off += eventLen;
    }
    xdevwifi_flushBss(networks, curSsid, curBssid, haveBssid, curSignal,
                      curChannel, curSecured);
    *outNetworks = networks;
    return true;
}

#endif /* __linux__ */

#endif /* XDEVICE_ON && XDEVICE_WIFI_ON */
