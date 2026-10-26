/**
 * @file       XDeviceWiFi_posix_Parsers.h
 * @brief      POSIX 无线后端的**纯解析函数**（无副作用、无系统调用）。
 * @details    从 XDeviceWiFi_posix.c 拆出的原因：
 *               - 解析是整条链路里最容易出错、也最难在无硬件环境下验证的
 *                 部分（WEXT 事件流布局、wpa_supplicant 制表符格式、
 *                 nmcli -t 转义），单独成文件后可用合成数据做单元测试
 *                 （Test/XDeviceTest/xdevice_wifi_posix_parsers_test.c）；
 *               - 平台编排（ioctl / 控制口 / 子进程）与文本解析分离，
 *                 编排层只关心状态机。
 *
 *             本文件不是公开 API：只被 POSIX 后端与对应单元测试包含。
 */
#ifndef XDEVICEWIFI_POSIX_PARSERS_H
#define XDEVICEWIFI_POSIX_PARSERS_H

#include "XNetwork_config.h"

#if XDEVICE_ON && XDEVICE_WIFI_ON

#include "XDeviceWiFi.h"
#include "XVector.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* =========================================================================
 * 一、数值换算（三处文本格式共用）
 * ========================================================================= */

/**
 * @brief 载波频率（MHz）→ 信道号。
 * @param mhz 频率；0 或未知返回 0。
 * @return 2.4G / 5G / 6G 三段信道号；未知返回 0。
 */
int XDevWifiPosix_ChannelFromMhz(int mhz);

/**
 * @brief dBm → 0..100 信号强度（Android calculateSignalLevel 同公式）。
 * @param dbm 信号电平；-100 以下按 0、-50 以上按 100。
 */
int XDevWifiPosix_DbmToPercent(int dbm);

/**
 * @brief WEXT 链路质量 0..255 → 0..100。
 * @param quality 驱动上报质量值；<=0 返回 0。
 */
int XDevWifiPosix_QualityToPercent(int quality);

/* =========================================================================
 * 二、安全方式判定
 * ========================================================================= */

/**
 * @brief 安全描述串 → 统一安全方式。
 * @details 同时接受 wpa_supplicant 的 flags（"[WPA2-PSK-CCMP][ESS]"）、
 *          NetworkManager 的 SECURITY 列（"WPA1 WPA2"/"WPA3"/"WEP"/""）。
 *          判定按强度从高到低，避免 "WPA2" 被 "WPA" 抢先命中。
 * @param text 描述串；NULL/空串=开放网络。
 */
XDeviceWiFiSecurity XDevWifiPosix_SecurityFromText(const char* text);

/* =========================================================================
 * 三、nmcli -t 转义格式
 * ========================================================================= */

/**
 * @brief 原地反转义 nmcli -t 的输出（`\:` → ':'，`\\` → '\'）。
 * @param text 待处理文本；NULL 安全。
 */
void XDevWifiPosix_UnescapeTerse(char* text);

/**
 * @brief 按**未转义**的冒号切分 nmcli -t 输出行，并逐字段反转义。
 * @param line 可写行缓冲（函数会写入 NUL）。
 * @param fields 输出字段指针数组。
 * @param maxFields 最多切出的字段数。
 * @return 实际字段数。
 */
int XDevWifiPosix_SplitTerse(char* line, char** fields, int maxFields);

/* =========================================================================
 * 四、扫描结果解析
 * ========================================================================= */

/**
 * @brief 解析 wpa_supplicant `SCAN_RESULTS` 应答。
 * @details 表头后每行 TAB 分隔五段：
 *          `bssid / frequency / signal level / flags / ssid`；
 *          SSID 允许含空格，取第四段之后的全部内容。
 * @param text 应答文本（可含首行表头）。
 * @param outNetworks 输出容器（元素 XDeviceWiFiNetworkInfo*，由
 *        XDeviceWiFi_freeNetworks 释放）；不能为 NULL。
 * @return 成功返回 true（结果可为空容器）；参数非法返回 false。
 */
bool XDevWifiPosix_ParseWpaScanResults(const char* text, XVector** outNetworks);

/**
 * @brief 解析 `nmcli -t -f SSID,BSSID,SIGNAL,CHAN,SECURITY device wifi list`。
 * @details SIGNAL 为 0..100、CHAN 为信道号、SECURITY 为空白分隔描述串。
 * @param text 命令输出。
 * @param outNetworks 输出容器；不能为 NULL。
 * @return 成功返回 true（结果可为空容器）；参数非法返回 false。
 */
bool XDevWifiPosix_ParseNmWifiList(const char* text, XVector** outNetworks);

/**
 * @brief 解析 `wpa_cli STATUS` 应答中的某个键值。
 * @param reply 应答文本；@param key 键名（如 "wpa_state"）。
 * @param out 输出缓冲；@param cap 容量。
 * @return 找到返回 true。
 */
bool XDevWifiPosix_StatusValue(const char* reply, const char* key, char* out,
                               size_t cap);

/**
 * @brief 生成 wpa_supplicant 接受的字符串值（引号形式或十六进制）。
 * @details 全可打印 ASCII 且不含 `"` / `\` 时输出 `"text"`；否则输出
 *          小写十六进制串（wpa_supplicant 对 ssid/psk 均接受）。
 * @return 成功返回 true；空串或缓冲不足返回 false。
 */
bool XDevWifiPosix_BuildWpaValue(const char* text, char* out, size_t cap);

/* =========================================================================
 * 五、WEXT 事件流解析（仅 Linux）
 * ========================================================================= */

#if defined(__linux__)

/**
 * @brief 判定 WEXT 事件流是展开布局还是紧凑（compat）布局。
 * @details 内核按调用者位宽选择布局：同宽=展开（LCP 头 8 字节），
 *          64 位内核 + 32 位用户态=紧凑（LCP 头 4 字节）。这里用定长事件
 *          （BSSID/FREQ/MODE/QUAL）的 len 反推，不依赖 WE 版本号。
 * @param buf 事件流；@param len 长度。
 * @return true=紧凑布局；false=展开布局（无法判定时按展开处理）。
 */
bool XDevWifiPosix_WextIsPacked(const unsigned char* buf, size_t len);

/**
 * @brief 把 SIOCGIWSCAN 的事件流解析为网络列表。
 * @details 以 SIOCGIWAP 作为一条记录的起点（WEXT 约定），逐条累积
 *          ESSID / FREQ / QUAL / ENCODE；隐藏网络无 ESSID，ssid 留 NULL。
 *          点事件（ESSID/ENCODE）的数据区紧跟在点事件头之后，头内不含
 *          指针字段（内核已把 pointer 置换为空）。
 * @param buf 事件流；@param len 流长度。
 * @param outNetworks 输出容器（用 XDeviceWiFi_freeNetworks 释放）。
 * @return 成功返回 true（可为空容器）；参数非法返回 false。
 */
bool XDevWifiPosix_ParseWextScan(const unsigned char* buf, size_t len,
                                 XVector** outNetworks);

#endif /* __linux__ */

#ifdef __cplusplus
}
#endif

#endif /* XDEVICE_ON && XDEVICE_WIFI_ON */

#endif /* XDEVICEWIFI_POSIX_PARSERS_H */
