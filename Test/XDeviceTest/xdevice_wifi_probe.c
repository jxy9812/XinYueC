/**
 * @file       xdevice_wifi_probe.c
 * @brief      无线子系统双层实测探针：
 *              ① L0 设备契约自检（对齐 XDevice 统一抽象设计文档 §10「通用
 *                 测试」：open/close、重复 close、属性读写、错误码）；
 *              ② L1 业务层流程（值语义 + 会话管理）：枚举适配器 → 发起扫描
 *                 → 轮询扫描状态机 → 取回结果清单 →（可选）连接并回读。
 * @details    独立可执行（xdevice_wifi_probe.exe），不依赖 GUI 与事件循环：
 *              状态机用阻塞轮询驱动（每次 200ms，与上层用定时器轮询同形，
 *              只是这里没有事件循环可用）。
 *
 *              L0 段全程走父类统一调度（便捷 API 只是转发到
 *              XDevice_control / XDevice_getProperty）；L1 段走业务层对象，
 *              二者都不碰平台实现。
 *
 *              用法：
 *                xdevice_wifi_probe.exe                 仅枚举 + 扫描
 *                xdevice_wifi_probe.exe <ssid> [口令]   追加连接测试
 *
 *              判读口径：
 *                - capabilities=0            平台无无线后端（如 POSIX 现阶段）
 *                - 适配器数=0                本机无无线网卡 / WLAN 服务未起
 *                - 扫描状态 Failed            驱动拒绝发起扫描
 *                - 扫描 0 条且 4 秒超时       周边无可用 AP（正常）
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "XDeviceWiFi.h"
#include "XNetworkWiFi.h"
#include "XString.h"
#include "XVariant.h"
#include "XVector.h"

#ifdef _WIN32
#include <windows.h>
static void probeSleepMs(unsigned int ms) { Sleep(ms); }
#else
#include <unistd.h>
static void probeSleepMs(unsigned int ms) { usleep(ms * 1000); }
#endif

/** @brief 安全方式枚举 → 可读文本。 */
static const char* probeSecurityText(XDeviceWiFiSecurity security)
{
    switch (security) {
    case XDeviceWiFi_SecurityOpen:          return "开放";
    case XDeviceWiFi_SecurityWep:           return "WEP";
    case XDeviceWiFi_SecurityWpaPsk:        return "WPA-PSK";
    case XDeviceWiFi_SecurityWpa2Psk:       return "WPA2-PSK";
    case XDeviceWiFi_SecurityWpa2Enterprise: return "WPA2-企业";
    case XDeviceWiFi_SecurityWpa3Sae:       return "WPA3-SAE";
    default:                                return "未知";
    }
}

/** @brief 扫描状态枚举 → 可读文本。 */
static const char* probeScanText(XDeviceWiFiScanState state)
{
    switch (state) {
    case XDeviceWiFi_ScanIdle:      return "空闲";
    case XDeviceWiFi_ScanRunning:   return "扫描中";
    case XDeviceWiFi_ScanCompleted: return "已完成";
    case XDeviceWiFi_ScanFailed:    return "失败";
    default:                        return "?";
    }
}

/** @brief 连接状态枚举 → 可读文本。 */
static const char* probeConnectText(XDeviceWiFiConnectState state)
{
    switch (state) {
    case XDeviceWiFi_ConnectIdle:       return "空闲";
    case XDeviceWiFi_ConnectConnecting: return "连接中";
    case XDeviceWiFi_ConnectConnected:  return "已连接";
    case XDeviceWiFi_ConnectFailed:     return "失败";
    default:                            return "?";
    }
}

/**
 * @brief L0 设备契约自检（对齐统一抽象设计文档 §10 通用测试）。
 * @param fd 已打开的无线设备句柄。
 */
static void probeDeviceContract(XFd fd)
{
    XVariant variant;
    bool ok;
    int err;

    /* 属性读写：合法属性（能力位）应读得出。 */
    memset(&variant, 0, sizeof(variant));
    XVariant_init(&variant, NULL, 0, XVariantType_NULL);
    ok = XDevice_getProperty(fd, XDeviceWiFiProperty_Capabilities, &variant);
    printf("== L0 契约: 读属性 %s caps=0x%04x\n", ok ? "成功" : "失败",
           ok ? XVariant_toUint32(&variant) : 0);
    XClassDeinit((XClass*)&variant);

    /* 属性写入：无线属性均为只读，写入应被拒并留下错误码。 */
    memset(&variant, 0, sizeof(variant));
    XVariant_init(&variant, NULL, 0, XVariantType_NULL);
    XVariant_setValue_int(&variant, 1);
    ok = XDevice_setProperty(fd, XDeviceWiFiProperty_Capabilities, &variant);
    XClassDeinit((XClass*)&variant);
    err = XDeviceWiFi_lastError(fd);
    printf("== L0 契约: 写只读属性 %s（期望被拒）lastError=%d"
           "（期望 %d=InvalidArgument）\n",
           ok ? "被接受" : "被拒", err, (int)XDeviceError_InvalidArgument);

    /* 未知属性应失败。 */
    memset(&variant, 0, sizeof(variant));
    XVariant_init(&variant, NULL, 0, XVariantType_NULL);
    ok = XDevice_getProperty(fd, 0x7FFFFF00u, &variant);
    XClassDeinit((XClass*)&variant);
    printf("== L0 契约: 读未知属性 %s（期望失败）\n", ok ? "成功" : "失败");

    /* 命令通道：非法命令应被拒（不崩、不误执行）。 */
    ok = XDevice_control(fd, 0x7FFFFF00u, NULL, NULL);
    printf("== L0 契约: 未知命令 %s（期望失败）\n", ok ? "成功" : "失败");
}

/**
 * @brief L1 业务层流程自检：会话 → 适配器 → 扫描 → 结果 →（可选）连接。
 * @param ssid 目标网络名；可为 NULL（不测连接）。
 * @param passphrase 口令；可为 NULL。
 */
static void probeBusinessLayer(const char* ssid, const char* passphrase)
{
    XNetworkWiFi* wifi = XNetworkWiFi_create();
    XVector* adapters = NULL;
    XVector* networks = NULL;
    const XNetworkWiFiAdapter* adapter = NULL;
    size_t count;
    size_t i;
    int err = 0;

    if (!wifi) {
        printf("== L1: 会话对象创建失败\n");
        return;
    }
    if (!XNetworkWiFi_open(wifi, &err)) {
        printf("== L1: 打开无线设备失败（err=%d）\n", err);
        XClassDelete(wifi);
        return;
    }
    printf("== L1: 会话已打开 caps=0x%04x\n", XNetworkWiFi_capabilities(wifi));

    adapters = XNetworkWiFi_adapters(wifi);
    if (!adapters) {
        printf("== L1: 适配器枚举失败\n");
        XClassDelete(wifi);
        return;
    }
    count = XVector_size_base(adapters);
    printf("== L1: 无线适配器 %u 个\n", (unsigned)count);
    for (i = 0; i < count; ++i) {
        const XNetworkWiFiAdapter* item =
            (const XNetworkWiFiAdapter*)XVector_at_base(adapters, (int64_t)i);
        const XString* name;
        const XString* id;
        if (!item) continue;
        name = XNetworkWiFiAdapter_name(item);
        id = XNetworkWiFiAdapter_id(item);
        printf("  [%u] name=%s id=%s ifIndex=%u caps=0x%04x\n", (unsigned)i,
               name ? XString_toUtf8(name) : "",
               id ? XString_toUtf8(id) : "",
               (unsigned)XNetworkWiFiAdapter_ifIndex(item),
               (unsigned)XNetworkWiFiAdapter_capabilities(item));
    }
    if (count == 0) {
        printf("== L1: 本机无无线适配器，跳过扫描\n");
        XClassDelete(adapters);
        XClassDelete(wifi);
        return;
    }
    adapter = (const XNetworkWiFiAdapter*)XVector_at_base(adapters, 0);

    printf("== L1: 发起扫描\n");
    if (!XNetworkWiFi_startScan(wifi, adapter)) {
        printf("== L1: 扫描发起失败\n");
        XClassDelete(adapters);
        XClassDelete(wifi);
        return;
    }
    {
        XDeviceWiFiScanState state = XDeviceWiFi_ScanRunning;
        int tick = 0;
        while (state == XDeviceWiFi_ScanRunning && tick < 60) {
            probeSleepMs(200);
            state = XNetworkWiFi_scanState(wifi, adapter);
            ++tick;
        }
        printf("== L1: 扫描终态 %s（%d 次轮询）\n", probeScanText(state), tick);
    }
    networks = XNetworkWiFi_takeScanResults(wifi, adapter);
    count = networks ? XVector_size_base(networks) : 0;
    printf("== L1: 扫描结果 %u 条\n", (unsigned)count);
    for (i = 0; i < count; ++i) {
        const XNetworkWiFiNetwork* net =
            (const XNetworkWiFiNetwork*)XVector_at_base(networks, (int64_t)i);
        const XString* netSsid;
        const XString* netBssid;
        if (!net) continue;
        netSsid = XNetworkWiFiNetwork_ssid(net);
        netBssid = XNetworkWiFiNetwork_bssid(net);
        printf("  ssid=%-32s bssid=%-18s 信号=%3d 信道=%2d 加密=%s\n",
               netSsid ? XString_toUtf8(netSsid) : "(隐藏网络)",
               netBssid ? XString_toUtf8(netBssid) : "",
               XNetworkWiFiNetwork_signalStrength(net),
               XNetworkWiFiNetwork_channel(net),
               probeSecurityText(XNetworkWiFiNetwork_security(net)));
    }
    if (networks) XClassDelete(networks);

    if (ssid) {
        printf("== L1: 发起连接 ssid=%s\n", ssid);
        if (XNetworkWiFi_connect(wifi, adapter, ssid, passphrase,
                                 XDeviceWiFi_SecurityWpa2Psk)) {
            XDeviceWiFiConnectState state = XDeviceWiFi_ConnectConnecting;
            int tick = 0;
            while (state == XDeviceWiFi_ConnectConnecting && tick < 120) {
                probeSleepMs(250);
                state = XNetworkWiFi_connectState(wifi, adapter);
                ++tick;
            }
            printf("== L1: 连接终态 %s（%d 次轮询）\n", probeConnectText(state),
                   tick);
            {
                XNetworkWiFiConnection* conn =
                    XNetworkWiFi_queryConnection(wifi, adapter);
                if (conn) {
                    const XString* connSsid = XNetworkWiFiConnection_ssid(conn);
                    const XString* connBssid = XNetworkWiFiConnection_bssid(conn);
                    printf("== L1: 当前连接 ssid=%s bssid=%s 信号=%d 加密=%s\n",
                           connSsid ? XString_toUtf8(connSsid) : "",
                           connBssid ? XString_toUtf8(connBssid) : "",
                           XNetworkWiFiConnection_signalStrength(conn),
                           probeSecurityText(XNetworkWiFiConnection_security(conn)));
                    XClassDelete(conn);
                } else {
                    printf("== L1: 当前连接 未连接\n");
                }
            }
        } else {
            printf("== L1: 连接发起失败\n");
        }
    }

    XClassDelete(adapters);
    XClassDelete(wifi); /* 会话析构 → 关闭无线设备。 */
}

int main(int argc, char** argv)
{
    XFd fd;
    int err = 0;

    /* ① L0 设备契约：打开无线设备并做通用契约自检。 */
    fd = XDeviceWiFi_open(NULL, &err);
    if (fd == XFD_INVALID) {
        printf("== 无线设备打开失败（err=%d）\n", err);
        return 1;
    }
    printf("== 无线设备已打开（fd=%d）\n", (int)fd);
    probeDeviceContract(fd);

    /* ② L1 业务层：另起一个会话跑完整流程（平台句柄按会话引用计数）。 */
    probeBusinessLayer(argc >= 2 ? argv[1] : NULL, argc >= 3 ? argv[2] : NULL);

    /* 重复 close：句柄已回收，父类按去重保护处理，应无副作用、不崩溃。 */
    XDeviceWiFi_close(fd);
    XDeviceWiFi_close(fd);
    printf("== L0 契约: 重复 close 已执行（期望无副作用）\n");
    return 0;
}
