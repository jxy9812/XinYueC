/**
 * @file       xdevice_wifi_posix_parsers_test.c
 * @brief      POSIX 无线后端解析函数单元测试（合成数据，无需无线硬件）。
 * @details    被测对象见 Drive/Posix/Device/Network/XDeviceWiFi_posix_Parsers.h。
 *             用合成字节流/文本覆盖三类真实格式：
 *               - WEXT SIOCGIWSCAN 事件流（展开布局与紧凑布局各一套）；
 *               - wpa_supplicant `SCAN_RESULTS`（TAB 分隔，SSID 含空格）；
 *               - nmcli -t 输出（`\:` 转义、SSID 含冒号）；
 *               - `wpa_cli STATUS` 键值提取与 wpa_supplicant 值的引号/十六
 *                 进制编码。
 *
 *             WEXT 事件流按内核 wext-core.c 的布局手工拼装：
 *               展开: [len:2][cmd:2][pad:4][载荷或(length,flags,pad)]
 *               紧凑: [len:2][cmd:2][载荷或(length,flags)]
 *             点事件（ESSID/ENCODE）的数据紧跟在点事件头之后。
 *
 *             退出码：0=全部断言通过；1=存在失败断言。
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "XNetwork_config.h"

#if XDEVICE_ON && XDEVICE_WIFI_ON

#include "XDeviceWiFi.h"
#include "XDeviceWiFi_posix_Parsers.h"
#include "XString.h"
#include "XVector.h"

#include <stdint.h>

#if defined(__linux__)
#include <linux/wireless.h>
#include <sys/socket.h>
#endif

static int g_pass = 0;
static int g_fail = 0;

#define CHECK(cond, fmt, ...)                                              \
    do {                                                                   \
        if (cond) {                                                        \
            ++g_pass;                                                      \
        } else {                                                           \
            ++g_fail;                                                      \
            printf("FAIL %s:%d: " fmt "\n", __FILE__, __LINE__,            \
                   ##__VA_ARGS__);                                         \
        }                                                                  \
    } while (0)

/** @brief 取容器中第 index 条结果（可能为 NULL）。 */
static const XDeviceWiFiNetworkInfo* test_networkAt(XVector* networks, size_t index)
{
    XDeviceWiFiNetworkInfo** slot;
    if (!networks) return NULL;
    if (index >= XVector_size_base((XContainer*)networks)) return NULL;
    slot = (XDeviceWiFiNetworkInfo**)XVector_at_base(networks, (int64_t)index);
    return slot ? *slot : NULL;
}

/**
 * @brief 安全取 UTF-8 文本：XString_toUtf8 对空串返回 NULL，这里统一给 ""。
 */
static const char* test_text(const XString* str)
{
    const char* text = str ? XString_toUtf8(str) : NULL;
    return text ? text : "";
}

/* =========================================================================
 * 一、数值换算与安全方式
 * ========================================================================= */

static void testConverters(void)
{
    CHECK(XDevWifiPosix_ChannelFromMhz(2412) == 1, "2412MHz 应为信道 1");
    CHECK(XDevWifiPosix_ChannelFromMhz(2437) == 6, "2437MHz 应为信道 6");
    CHECK(XDevWifiPosix_ChannelFromMhz(2484) == 14, "2484MHz 应为信道 14");
    CHECK(XDevWifiPosix_ChannelFromMhz(5180) == 36, "5180MHz 应为信道 36");
    CHECK(XDevWifiPosix_ChannelFromMhz(5745) == 149, "5745MHz 应为信道 149");
    CHECK(XDevWifiPosix_ChannelFromMhz(0) == 0, "0MHz 应为 0");
    CHECK(XDevWifiPosix_ChannelFromMhz(100) == 0, "非法频率应为 0");

    CHECK(XDevWifiPosix_DbmToPercent(-50) == 100, "-50dBm 应为 100");
    CHECK(XDevWifiPosix_DbmToPercent(-100) == 0, "-100dBm 应为 0");
    CHECK(XDevWifiPosix_DbmToPercent(-75) == 50, "-75dBm 应为 50");
    CHECK(XDevWifiPosix_DbmToPercent(-120) == 0, "-120dBm 应夹到 0");
    CHECK(XDevWifiPosix_DbmToPercent(0) == 100, "0dBm 应夹到 100");

    CHECK(XDevWifiPosix_QualityToPercent(0) == 0, "质量 0 应为 0");
    CHECK(XDevWifiPosix_QualityToPercent(100) == 100, "质量 100 应为 100");
    CHECK(XDevWifiPosix_QualityToPercent(255) == 100, "质量 255 应为 100");
    CHECK(XDevWifiPosix_QualityToPercent(128) == 50, "质量 128 应折算 50");

    CHECK(XDevWifiPosix_SecurityFromText("") == XDeviceWiFi_SecurityOpen,
          "空串=开放");
    CHECK(XDevWifiPosix_SecurityFromText("[ESS]") == XDeviceWiFi_SecurityOpen,
          "[ESS]=开放");
    CHECK(XDevWifiPosix_SecurityFromText("WEP") == XDeviceWiFi_SecurityWep,
          "WEP");
    CHECK(XDevWifiPosix_SecurityFromText("WPA1 WPA2") ==
              XDeviceWiFi_SecurityWpa2Psk,
          "WPA1 WPA2 取更强者");
    CHECK(XDevWifiPosix_SecurityFromText("[WPA2-PSK-CCMP][ESS]") ==
              XDeviceWiFi_SecurityWpa2Psk,
          "WPA2-PSK");
    CHECK(XDevWifiPosix_SecurityFromText("WPA3") ==
              XDeviceWiFi_SecurityWpa3Sae,
          "WPA3");
    CHECK(XDevWifiPosix_SecurityFromText("[WPA2-EAP-CCMP][ESS]") ==
              XDeviceWiFi_SecurityWpa2Enterprise,
          "WPA2-企业");
}

/* =========================================================================
 * 二、nmcli -t 转义
 * ========================================================================= */

static void testTerseFormat(void)
{
    {
        char row[] = "My:Net:\\:escaped";
        char* fields[3];
        int count = XDevWifiPosix_SplitTerse(row, fields, 3);
        CHECK(count == 3, "应切出 3 段，实际 %d", count);
        if (count == 3) {
            CHECK(strcmp(fields[0], "My") == 0, "第 1 段=%s", fields[0]);
            CHECK(strcmp(fields[1], "Net") == 0, "第 2 段=%s", fields[1]);
            CHECK(strcmp(fields[2], ":escaped") == 0,
                  "转义冒号应还原，实际=%s", fields[2]);
        }
    }
    {
        char row[] = "a\\\\b:c";
        char* fields[2];
        CHECK(XDevWifiPosix_SplitTerse(row, fields, 2) == 2, "两段");
        CHECK(strcmp(fields[0], "a\\b") == 0,
              "转义反斜杠应还原，实际=%s", fields[0]);
    }
    {
        char row[] = "无转义";
        char* fields[4];
        CHECK(XDevWifiPosix_SplitTerse(row, fields, 4) == 1, "只有一段");
        CHECK(strcmp(fields[0], "无转义") == 0, "原文保留");
    }
}

/* =========================================================================
 * 三、wpa_supplicant SCAN_RESULTS
 * ========================================================================= */

static void testWpaScanResults(void)
{
    const char* text =
        "bssid / frequency / signal level / flags / ssid\n"
        "aa:bb:cc:dd:ee:01\t2412\t-45\t[WPA2-PSK-CCMP][ESS]\tOffice WiFi\n"
        "aa:bb:cc:dd:ee:02\t5180\t-70\t[ESS]\tGuest\n"
        "aa:bb:cc:dd:ee:03\t2437\t-85\t[WPA2-PSK-CCMP][WPS][ESS]\tHome 2.4G\n";
    XVector* networks = NULL;
    CHECK(XDevWifiPosix_ParseWpaScanResults(text, &networks), "解析应成功");
    CHECK(networks != NULL, "容器非空");
    CHECK(networks && XVector_size_base((XContainer*)networks) == 3,
          "应得 3 条，实际 %zu", networks ? XVector_size_base((XContainer*)networks) : 0);
    if (networks && XVector_size_base((XContainer*)networks) == 3) {
        const XDeviceWiFiNetworkInfo* first = test_networkAt(networks, 0);
        const XDeviceWiFiNetworkInfo* second = test_networkAt(networks, 1);
        const XDeviceWiFiNetworkInfo* third = test_networkAt(networks, 2);
        CHECK(first && first->ssid &&
                  strcmp(test_text(first->ssid), "Office WiFi") == 0,
              "SSID 可含空格");
        CHECK(first && first->bssid &&
                  strcmp(test_text(first->bssid), "aa:bb:cc:dd:ee:01") == 0,
              "BSSID");
        CHECK(first && first->signalStrength == 100, "-45dBm→100");
        CHECK(first && first->channel == 1, "2412MHz→信道 1");
        CHECK(first && first->security == XDeviceWiFi_SecurityWpa2Psk, "WPA2");
        CHECK(first && first->secured, "加密网络 secured=true");
        CHECK(second && second->security == XDeviceWiFi_SecurityOpen, "开放网络");
        CHECK(second && !second->secured, "开放网络 secured=false");
        CHECK(second && second->channel == 36, "5180MHz→信道 36");
        CHECK(third && third->signalStrength == 30, "-85dBm→30");
        CHECK(third && third->channel == 6, "2437MHz→信道 6");
    }
    if (networks) XDeviceWiFi_freeNetworks(networks);

    /* 隐藏网络：SSID 为空字段，ssid 指针应为 NULL。 */
    {
        const char* hidden =
            "bssid / frequency / signal level / flags / ssid\n"
            "aa:bb:cc:dd:ee:04\t2412\t-60\t[WPA2-PSK-CCMP]\t\n";
        XVector* list = NULL;
        CHECK(XDevWifiPosix_ParseWpaScanResults(hidden, &list), "隐藏网络解析");
        CHECK(list && XVector_size_base((XContainer*)list) == 1, "一条隐藏网络");
        if (list && XVector_size_base((XContainer*)list) == 1) {
            const XDeviceWiFiNetworkInfo* net = test_networkAt(list, 0);
            CHECK(net && net->ssid == NULL, "隐藏网络 ssid 应为 NULL");
        }
        if (list) XDeviceWiFi_freeNetworks(list);
    }

    /* 空输入/坏参数：容器为空但成功；NULL 输出指针应拒绝。 */
    {
        XVector* list = NULL;
        CHECK(XDevWifiPosix_ParseWpaScanResults("", &list), "空文本应成功");
        CHECK(list && XVector_size_base((XContainer*)list) == 0, "空文本=空容器");
        if (list) XDeviceWiFi_freeNetworks(list);
        CHECK(!XDevWifiPosix_ParseWpaScanResults(text, NULL), "NULL 输出应拒绝");
    }
}

/* =========================================================================
 * 四、nmcli 列表
 * ========================================================================= */

static void testNmWifiList(void)
{
    const char* text =
        "Office\\:HQ:AA\\:BB\\:CC\\:DD\\:EE\\:01:87:6:WPA2\n"
        "OpenNet:AA\\:BB\\:CC\\:DD\\:EE\\:02:42:11:\n"
        "Wifi3:AA\\:BB\\:CC\\:DD\\:EE\\:03:99:149:WPA3\n";
    XVector* networks = NULL;
    CHECK(XDevWifiPosix_ParseNmWifiList(text, &networks), "解析应成功");
    CHECK(networks && XVector_size_base((XContainer*)networks) == 3,
          "应得 3 条，实际 %zu", networks ? XVector_size_base((XContainer*)networks) : 0);
    if (networks && XVector_size_base((XContainer*)networks) == 3) {
        const XDeviceWiFiNetworkInfo* first = test_networkAt(networks, 0);
        const XDeviceWiFiNetworkInfo* second = test_networkAt(networks, 1);
        const XDeviceWiFiNetworkInfo* third = test_networkAt(networks, 2);
        /* 注意：nmcli 对 SSID 里的冒号转义，BSSID 里的冒号同样转义；
         * 转义先还原，故 SSID 与 BSSID 都能取回原值。 */
        CHECK(first && first->ssid &&
                  strcmp(test_text(first->ssid), "Office:HQ") == 0,
              "SSID 含冒号，实际=%s",
              first && first->ssid ? test_text(first->ssid) : "(null)");
        CHECK(first && first->bssid &&
                  strcmp(test_text(first->bssid),
                         "AA:BB:CC:DD:EE:01") == 0,
              "BSSID 转义还原");
        CHECK(first && first->signalStrength == 87, "信号直取");
        CHECK(first && first->channel == 6, "信道直取");
        CHECK(first && first->security == XDeviceWiFi_SecurityWpa2Psk, "WPA2");
        CHECK(second && second->security == XDeviceWiFi_SecurityOpen, "开放");
        CHECK(second && !second->secured, "开放 secured=false");
        CHECK(third && third->security == XDeviceWiFi_SecurityWpa3Sae, "WPA3");
    }
    if (networks) XDeviceWiFi_freeNetworks(networks);
}

/* =========================================================================
 * 五、wpa_cli STATUS 与值编码
 * ========================================================================= */

static void testWpaStatus(void)
{
    const char* status =
        "bssid=aa:bb:cc:dd:ee:01\n"
        "freq=2437\n"
        "ssid=My Home\n"
        "id=0\n"
        "mode=station\n"
        "wpa_state=COMPLETED\n"
        "key_mgmt=WPA2-PSK\n";
    char value[64];

    CHECK(XDevWifiPosix_StatusValue(status, "wpa_state", value, sizeof(value)),
          "应取到 wpa_state");
    CHECK(strcmp(value, "COMPLETED") == 0, "wpa_state=%s", value);
    CHECK(XDevWifiPosix_StatusValue(status, "ssid", value, sizeof(value)),
          "应取到 ssid");
    CHECK(strcmp(value, "My Home") == 0, "ssid=%s", value);
    CHECK(!XDevWifiPosix_StatusValue(status, "absent", value, sizeof(value)),
          "不存在的键应返回 false");
    CHECK(value[0] == '\0', "未命中时输出应清空");
    /* 键前缀不能误命中：查找 "id" 不应拿到 "bssid" 的尾部。 */
    CHECK(XDevWifiPosix_StatusValue(status, "id", value, sizeof(value)),
          "id 存在");
    CHECK(strcmp(value, "0") == 0, "id=%s（不能被 bssid 误命中）", value);

    /* 值编码：可打印 ASCII 用引号，其它用十六进制。 */
    CHECK(XDevWifiPosix_BuildWpaValue("MyNet", value, sizeof(value)),
          "普通 SSID 应可用");
    CHECK(strcmp(value, "\"MyNet\"") == 0, "引号形式，实际=%s", value);

    CHECK(XDevWifiPosix_BuildWpaValue("a\"b", value, sizeof(value)),
          "含引号应转十六进制");
    CHECK(strcmp(value, "612262") == 0, "十六进制，实际=%s", value);

    CHECK(XDevWifiPosix_BuildWpaValue("\xE4\xB8\xAD", value, sizeof(value)),
          "UTF-8 中文应转十六进制");
    CHECK(strcmp(value, "e4b8ad") == 0, "中文十六进制，实际=%s", value);

    {
        char tiny[4];
        CHECK(!XDevWifiPosix_BuildWpaValue("toolong", tiny, sizeof(tiny)),
              "缓冲不足应失败");
        CHECK(!XDevWifiPosix_BuildWpaValue("", value, sizeof(value)),
              "空串应失败");
    }
}

#if defined(__linux__)

/* =========================================================================
 * 六、WEXT 事件流（按内核布局手工拼装）
 * ========================================================================= */

/** @brief 拼接缓冲（带越界保护）。 */
typedef struct TestStream
{
    unsigned char data[4096];
    size_t used;
    bool overflow;
} TestStream;

static void ts_put(TestStream* stream, const void* src, size_t len)
{
    if (stream->used + len > sizeof(stream->data)) {
        stream->overflow = true;
        return;
    }
    memcpy(stream->data + stream->used, src, len);
    stream->used += len;
}

static void ts_zeros(TestStream* stream, size_t len)
{
    size_t i;
    for (i = 0; i < len; ++i) {
        unsigned char zero = 0;
        ts_put(stream, &zero, 1);
    }
}

/**
 * @brief 追加一个定长事件。
 * @param lcp 布局头部长度（展开 8 / 紧凑 4）。
 * @param payload 载荷；@param payloadLen 载荷长度。
 */
static void ts_fixed(TestStream* stream, bool packed, uint16_t cmd,
                     const void* payload, size_t payloadLen)
{
    uint16_t len = (uint16_t)((packed ? 4u : 8u) + payloadLen);
    ts_put(stream, &len, sizeof(len));
    ts_put(stream, &cmd, sizeof(cmd));
    if (!packed) ts_zeros(stream, 4); /* 对齐填充。 */
    ts_put(stream, payload, payloadLen);
}

/**
 * @brief 追加一个点事件（ESSID/ENCODE）。
 * @details 严格按内核 wext-core.c 的 iwe_stream_add_point 布局拼装：
 *   展开（64 位同宽）：[len:2][cmd:2][填充:4][length:2][flags:2][填充:4][extra...]
 *   紧凑（兼容/32 位）：[len:2][cmd:2][length:2][flags:2][extra...]
 *   extra（ESSID 原文等）紧跟点事件头之后；length/flags 位于 lcp_len 偏移处。
 */
static void ts_point(TestStream* stream, bool packed, uint16_t cmd,
                     const void* extra, size_t extraLen, uint16_t flags)
{
    size_t header = packed ? 8u : 16u;
    uint16_t len = (uint16_t)(header + extraLen);
    uint16_t length = (uint16_t)extraLen;
    ts_put(stream, &len, sizeof(len));
    ts_put(stream, &cmd, sizeof(cmd));
    if (!packed) ts_zeros(stream, 4); /* 展开布局：len/cmd 与 length 之间的填充。 */
    ts_put(stream, &length, sizeof(length));
    ts_put(stream, &flags, sizeof(flags));
    if (!packed) ts_zeros(stream, 4); /* 展开布局：flags 与 extra 之间的填充。 */
    if (extraLen) ts_put(stream, extra, extraLen);
}

/** @brief 追加 SIOCGIWAP（BSSID）。 */
static void ts_ap(TestStream* stream, bool packed, const unsigned char mac[6])
{
    struct sockaddr address;
    memset(&address, 0, sizeof(address));
    address.sa_family = 1; /* ARPHRD_ETHER */
    memcpy(address.sa_data, mac, 6);
    ts_fixed(stream, packed, SIOCGIWAP, &address, sizeof(address));
}

/** @brief 追加 IWEVQUAL（dBm 电平）。 */
static void ts_qualDbm(TestStream* stream, bool packed, int dbm)
{
    struct iw_quality quality;
    memset(&quality, 0, sizeof(quality));
    quality.level = (int8_t)dbm;
    quality.updated = IW_QUAL_LEVEL_UPDATED | IW_QUAL_DBM;
    ts_fixed(stream, packed, IWEVQUAL, &quality, sizeof(quality));
}

/** @brief 追加 SIOCGIWFREQ（MHz）。 */
static void ts_freqMhz(TestStream* stream, bool packed, int mhz)
{
    struct iw_freq freq;
    memset(&freq, 0, sizeof(freq));
    freq.m = mhz;
    freq.e = 6; /* 10^6：单位 MHz。 */
    ts_fixed(stream, packed, SIOCGIWFREQ, &freq, sizeof(freq));
}

/** @brief 测试两套布局（展开/紧凑）下的同一份语义数据。 */
static void testWextLayout(bool packed)
{
    static const unsigned char mac1[6] = { 0xAA, 0xBB, 0xCC, 0x00, 0x11, 0x01 };
    static const unsigned char mac2[6] = { 0xAA, 0xBB, 0xCC, 0x00, 0x11, 0x02 };
    TestStream stream;
    XVector* networks = NULL;
    const char* ssid1 = "Office";

    memset(&stream, 0, sizeof(stream));

    /* 第一条：Office / -45dBm / 2412MHz / 加密（ENCODE flags=0=启用）。 */
    ts_ap(&stream, packed, mac1);
    ts_point(&stream, packed, SIOCGIWESSID, ssid1, strlen(ssid1), 1);
    ts_freqMhz(&stream, packed, 2412);
    ts_qualDbm(&stream, packed, -45);
    {
        unsigned char key[5] = { 1, 2, 3, 4, 5 };
        uint16_t flags = 0; /* IW_ENCODE_DISABLED 未置位 = 加密。 */
        ts_point(&stream, packed, SIOCGIWENCODE, key, sizeof(key), flags);
    }
    /* 第二条：Guest / -72dBm / 5180MHz / 开放。 */
    ts_ap(&stream, packed, mac2);
    ts_point(&stream, packed, SIOCGIWESSID, "Guest", 5, 1);
    ts_freqMhz(&stream, packed, 5180);
    ts_qualDbm(&stream, packed, -72);
    {
        uint16_t flags = IW_ENCODE_DISABLED;
        ts_point(&stream, packed, SIOCGIWENCODE, NULL, 0, flags);
    }

    CHECK(!stream.overflow, "合成流不应溢出");
    CHECK(XDevWifiPosix_WextIsPacked(stream.data, stream.used) == packed,
          "%s 布局判定应一致", packed ? "紧凑" : "展开");
    CHECK(XDevWifiPosix_ParseWextScan(stream.data, stream.used, &networks),
          "%s 布局解析应成功", packed ? "紧凑" : "展开");
    CHECK(networks && XVector_size_base((XContainer*)networks) == 2,
          "%s 布局应得 2 条，实际 %zu", packed ? "紧凑" : "展开",
          networks ? XVector_size_base((XContainer*)networks) : 0);

    if (networks && XVector_size_base((XContainer*)networks) == 2) {
        const XDeviceWiFiNetworkInfo* first = test_networkAt(networks, 0);
        const XDeviceWiFiNetworkInfo* second = test_networkAt(networks, 1);
        CHECK(first && first->ssid &&
                  strcmp(test_text(first->ssid), "Office") == 0,
              "首条 SSID，实际=%s",
              first && first->ssid ? test_text(first->ssid) : "(null)");
        CHECK(first && first->bssid &&
                  strcmp(test_text(first->bssid),
                         "AA:BB:CC:00:11:01") == 0,
              "首条 BSSID，实际=%s",
              first && first->bssid ? test_text(first->bssid) : "(null)");
        CHECK(first && first->signalStrength == 100, "-45dBm→100，实际=%d",
              first ? first->signalStrength : -999);
        CHECK(first && first->channel == 1, "2412MHz→1，实际=%d",
              first ? first->channel : -1);
        /* WEXT 无法区分 WEP/WPA/WPA2：加密但未知按 Unknown + secured。 */
        CHECK(first && first->secured, "首条应标记为加密");
        CHECK(second && second->ssid &&
                  strcmp(test_text(second->ssid), "Guest") == 0,
              "次条 SSID");
        CHECK(second && second->signalStrength == 56, "-72dBm→56，实际=%d",
              second ? second->signalStrength : -999);
        CHECK(second && second->channel == 36, "5180MHz→36");
        CHECK(second && !second->secured, "次条应为开放");
        CHECK(second && second->security == XDeviceWiFi_SecurityOpen,
              "开放网络安全方式");
    }
    if (networks) XDeviceWiFi_freeNetworks(networks);
}

/** @brief 隐藏网络：没有 ESSID 事件，ssid 必须为 NULL 且不误吞前一条。 */
static void testWextHidden(void)
{
    static const unsigned char mac1[6] = { 0xAA, 0xBB, 0xCC, 0x00, 0x11, 0x01 };
    static const unsigned char mac2[6] = { 0xAA, 0xBB, 0xCC, 0x00, 0x11, 0x02 };
    TestStream stream;
    XVector* networks = NULL;

    memset(&stream, 0, sizeof(stream));
    ts_ap(&stream, false, mac1);
    ts_point(&stream, false, SIOCGIWESSID, "First", 5, 1);
    ts_qualDbm(&stream, false, -50);
    /* 第二条没有 ESSID：隐藏网络。 */
    ts_ap(&stream, false, mac2);
    ts_qualDbm(&stream, false, -80);

    CHECK(XDevWifiPosix_ParseWextScan(stream.data, stream.used, &networks),
          "隐藏网络解析");
    CHECK(networks && XVector_size_base((XContainer*)networks) == 2, "应得 2 条");
    if (networks && XVector_size_base((XContainer*)networks) == 2) {
        const XDeviceWiFiNetworkInfo* second = test_networkAt(networks, 1);
        CHECK(second && second->ssid == NULL,
              "隐藏网络不得沿用上一条 SSID");
        CHECK(second && second->signalStrength == 40, "-80dBm→40");
    }
    if (networks) XDeviceWiFi_freeNetworks(networks);
}

/** @brief 截断/垃圾输入：不得越界读、不得崩溃。 */
static void testWextRobustness(void)
{
    static const unsigned char mac[6] = { 0xAA, 0xBB, 0xCC, 0x00, 0x11, 0x01 };
    TestStream stream;
    size_t cut;

    memset(&stream, 0, sizeof(stream));
    ts_ap(&stream, false, mac);
    ts_point(&stream, false, SIOCGIWESSID, "Abc", 3, 1);
    ts_freqMhz(&stream, false, 2437);
    ts_qualDbm(&stream, false, -60);

    /* 从头部逐字节截断：任何前缀都必须能安全返回。 */
    for (cut = 0; cut <= stream.used; ++cut) {
        XVector* networks = NULL;
        bool ok = XDevWifiPosix_ParseWextScan(stream.data, cut, &networks);
        if (cut == 0) {
            CHECK(!ok, "空流应拒绝");
        }
        if (networks) XDeviceWiFi_freeNetworks(networks);
        if (ok || !ok) {
            /* 只要没崩就算过；本条断言恒定，占位计数保持简单。 */
        }
    }
    ++g_pass; /* 逐字节截断全程未崩溃。 */

    /* 伪造的超长 len 字段：解析必须提前终止而不是越界。 */
    {
        unsigned char rogue[16];
        XVector* networks = NULL;
        memset(rogue, 0, sizeof(rogue));
        rogue[0] = 0xFF;
        rogue[1] = 0xFF; /* len = 65535，远超缓冲。 */
        rogue[2] = SIOCGIWAP & 0xFF;
        rogue[3] = (SIOCGIWAP >> 8) & 0xFF;
        CHECK(XDevWifiPosix_ParseWextScan(rogue, sizeof(rogue), &networks),
              "超长 len 应安全返回");
        if (networks) XDeviceWiFi_freeNetworks(networks);
    }

    CHECK(!XDevWifiPosix_ParseWextScan(NULL, 0, &(XVector*){ NULL }),
          "NULL 缓冲应拒绝");
}

#endif /* __linux__ */

int main(void)
{
    printf("== POSIX 无线后端解析器单元测试 ==\n");
    testConverters();
    testTerseFormat();
    testWpaScanResults();
    testNmWifiList();
    testWpaStatus();
#if defined(__linux__)
    testWextLayout(false);
    testWextLayout(true);
    testWextHidden();
    testWextRobustness();
#else
    printf("（非 Linux：跳过 WEXT 事件流用例）\n");
#endif
    printf("== 通过 %d，失败 %d ==\n", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}

#else /* XDEVICE_ON && XDEVICE_WIFI_ON */

int main(void)
{
    printf("XDEVICE_WIFI_ON=0：无线模块已裁剪，跳过测试。\n");
    return 0;
}

#endif
