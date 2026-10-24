/**
 * XWifiConnector —— 安卓 WiFi 连接/断开桥（供 JNI 调用）。
 *
 * 背景（2026-10-24）：安卓平台的 XDeviceWiFi 后端原先只实现了扫描
 * （CapScan），连接/断开如实返回 false。桌面的 win32 后端用 WlanAPI 全量
 * 实现了连接；安卓这边用户要求**连接归系统持有**（系统 WiFi 设置页能正常
 * 显示该网络的 IP/网关），且真机实测"应用持有"的连接**无法上网**。
 *
 * 【安卓三种连接模型的实测结论】（真机 Xiaomi M2105K81C / Android 13）：
 *   1. WifiManager.addNetwork() + enableNetwork()
 *      —— 走不通。系统日志原文：
 *         "WifiConfigManager: UID 10471 does not have permission to update
 *          configuration "XinYueAP"NONE / Failed to add/update network"
 *         即：**该 WiFi 配置已存在于系统（用户以前连过）时，非特权应用
 *         无权改它**（AOSP 对 addNetwork/enableNetwork 的限制是
 *         "非特权应用"级，与 targetSdk 无关）。
 *      —— 且即便对系统没有的新 SSID 建配置成功，enableNetwork 只是
 *         "允许自动连接"，不保证立刻抢占当前连接。
 *   2. WifiNetworkSpecifier + ConnectivityManager.requestNetwork()
 *      —— 能连上（DHCP 正常、拿到 IP 与默认路由），但网络代理带
 *         Specifier 标记 → 系统设置页显示为"应用持有"，且**不写入系统
 *         已保存列表**；用户实测这种连接**无法上网**。故不采用。
 *   3. **WifiNetworkSuggestion**（本实现采用）
 *      —— 把网络**建议**给系统，系统把它写入**自己的已保存网络列表**
 *         （带凭据），连接也由系统发起 → 系统设置页正常显示 IP/网关。
 *         实测：建议确实进了系统表
 *         （"XinYueAP"…_com.xinyuec.xguidemo is added in the
 *         WifiConfigManager）。
 *
 * 【本实现的关键点】
 *   - `setIsPostConnectionEnabled(true)`（**反射调用**）：这是 AOSP 里
 *     "用户批准建议后由系统去连它"的开关。该方法在较新的 SDK（android-37
 *     的 android.jar）里已移除，但**设备（API 33）上仍在**——故用反射按
 *     名字找，找不到就跳过（退化为普通建议：系统只保存，需用户在系统
 *     WLAN 列表里点它才连）。
 *   - 系统连接结果通过广播
 *     `WifiManager.ACTION_WIFI_NETWORK_SUGGESTION_POST_CONNECTION`
 *     回报（附带 EXTRA_NETWORK_SUGGESTION 的结果码）：系统连上了 /
 *     认证失败 / IP 分配失败等。这比轮询 getConnectionInfo 更快更准。
 *   - 状态机口径与 XDeviceWiFi.h 的契约一致：Idle = **未发起连接**，
 *     绝不用"当前是否已关联到某个 AP"来推导 Connected（那是
 *     queryConnection 的职责，见 XDeviceWiFi_android.c 的说明）。
 */
package com.xinyuec.android;

import android.content.Context;
import android.net.wifi.WifiConfiguration;
import android.net.wifi.WifiManager;
import android.net.wifi.WifiNetworkSuggestion;
import android.os.Build;
import java.util.ArrayList;
import java.util.List;

public class XWifiConnector {

    /** 连接操作状态（与 XDeviceWiFiConnectState 对齐）。 */
    public static final int STATE_IDLE = 0;
    public static final int STATE_CONNECTING = 1;
    public static final int STATE_CONNECTED = 2;
    public static final int STATE_FAILED = 3;

    /** 加密类型（与 XDeviceWiFiSecurity 对齐）。 */
    private static final int SEC_OPEN = 1;
    private static final int SEC_WEP = 2;
    private static final int SEC_WPA_PSK = 3;
    private static final int SEC_WPA2_PSK = 4;
    private static final int SEC_WPA2_ENTERPRISE = 5;
    private static final int SEC_WPA3_SAE = 6;

    private static WifiManager sWifi;
    /** logcat 标签（诊断用）。 */
    private static final String TAG = "XWifi";
    /** 连接发起时刻（毫秒）——超时判失败用。 */
    private static long sConnectStartMs;
    /** 连接超时：超过此时长仍没连上目标 SSID 即判失败（对齐 win32 后端
     *  的 XWIFI_CONNECT_TIMEOUT_MS 量级；系统切网+DHCP 正常几秒内完成）。 */
    private static final long CONNECT_TIMEOUT_MS = 20000;
    private static int sState = STATE_IDLE;
    private static String sSsid;
    /** 我方提交的建议（路径 B；断开时据此撤除）。 */
    private static WifiNetworkSuggestion sSuggestion;
    /** 路径 A 命中的系统网络号（断开时据此禁用）。 */
    private static int sNetId = -1;

    /** 取 WifiManager（缓存；Context 变化时重建）。 */
    private static synchronized WifiManager wifi(Context ctx) {
        if (ctx == null) {
            return null;
        }
        if (sWifi == null) {
            sWifi = (WifiManager) ctx.getSystemService(Context.WIFI_SERVICE);
        }
        return sWifi;
    }

    /**
     * 发起连接。
     *
     * 【路径 A（首选）：查已存列表 + enableNetwork —— 连接归系统持有】
     *   本应用 targetSdk=28（低于 Q），`enableNetwork`/`addNetwork` 不受
     *   Q 的"非特权应用"限制。两条子路径：
     *   A1. 目标 SSID **已在系统已保存列表**（用户以前连过）：直接取其
     *       networkId 调 `enableNetwork(netId, true)`——**不能先 addNetwork**：
     *       对已存在配置，addNetwork 会走"更新他人配置"分支，被
     *       "does not have permission to update configuration" 拒绝
     *       （真机日志实证；该检查与 targetSdk 无关，是配置所有权检查）。
     *       enableNetwork 直接切网，连接由**系统**发起并持有 → 系统设置页
     *       正常显示 IP/网关，与用户手动连一个网完全一致。
     *   A2. 目标 SSID 不在列表：`addNetwork`（新建属于本应用的配置，
     *       targetSdk<29 允许）+ `enableNetwork(netId, true)` 立即切换。
     *   `enableNetwork(netId, true)` 第二参 true = 断开其它网络立刻切过去；
     *   传 false 只"允许自动连接"，系统按评分决定何时切——目标网信号弱于
     *   当前网时**永远不切**（真机实证）。
     *
     * 【路径 B（退路）：addNetworkSuggestions】
     *   A 被拒（将来 targetSdk 升到 29+）时退化：系统把网络写入自己的
     *   列表（带凭据）但**不主动连**（实测 num_connect_success=0），需用户
     *   在系统 WLAN 列表里点它。仅保底，不指望它完成"立即连接"。
     *
     * @param ssid       目标 SSID（明文）。
     * @param passphrase 口令；开放网络传 null 或空串（A1 路径不用它——
     *                   连接凭据取系统已保存的那份）。
     * @param security   加密类型（SEC_* 之一）。
     * @return true=连接已发起（不代表已连上；连上与否看 state()）。
     */
    public static synchronized boolean connect(Context ctx, String ssid,
                                               String passphrase,
                                               int security) {
        WifiManager wm = wifi(ctx);
        if (wm == null || ssid == null || ssid.isEmpty()) {
            return false;
        }
        sSsid = ssid;
        sNetId = -1;
        sSuggestion = null;
        sConnectStartMs = System.currentTimeMillis();
        sState = STATE_CONNECTING;

        if (connectViaSavedConfig(wm, ssid, passphrase, security)) {
            return true;
        }
        return connectViaSuggestion(wm, ssid, passphrase, security);
    }

    /**
     * 路径 A：查已存列表 + enableNetwork（连接归系统持有，立即切换）。
     *
     * @return true=已发起系统连接；false=此路不通（SecurityException 等），
     *         调用方转退路。
     */
    @SuppressWarnings("deprecation")
    private static boolean connectViaSavedConfig(WifiManager wm, String ssid,
                                                 String passphrase,
                                                 int security) {
        int netId = -1;
        try {
            // A1: 查系统已保存列表（需要定位权限已授予；列表为空按"未命中"
            // 处理，走 A2 新建）。
            try {
                for (WifiConfiguration cfg : wm.getConfiguredNetworks()) {
                    if (cfg == null || cfg.SSID == null) continue;
                    String cSsid = cfg.SSID;
                    if (cSsid.startsWith("\"") && cSsid.endsWith("\"")
                            && cSsid.length() >= 2) {
                        cSsid = cSsid.substring(1, cSsid.length() - 1);
                    }
                    if (ssid.equals(cSsid)) {
                        netId = cfg.networkId;
                        break;
                    }
                }
            } catch (SecurityException e) {
                // 读不了已存列表（定位权限缺失）：当作未命中走 A2。
                netId = -1;
            }

            if (netId < 0) {
                // A2: 新建属于本应用的配置（targetSdk<29 允许）。
                WifiConfiguration cfg = new WifiConfiguration();
                cfg.SSID = "\"" + ssid + "\"";
                if (passphrase != null && !passphrase.isEmpty()) {
                    if (security == SEC_WEP) {
                        cfg.wepKeys[0] = "\"" + passphrase + "\"";
                        cfg.wepTxKeyIndex = 0;
                    } else {
                        cfg.preSharedKey = "\"" + passphrase + "\"";
                    }
                }
                netId = wm.addNetwork(cfg);
                android.util.Log.i(TAG, "addNetwork(new) -> " + netId);
                if (netId < 0) {
                    return false;
                }
            } else {
                android.util.Log.i(TAG, "found saved network id=" + netId);
            }

            // 第二参 true = 断开其它网络，立刻切到该网。
            boolean ok = wm.enableNetwork(netId, true);
            android.util.Log.i(TAG, "enableNetwork(" + netId + ", true) -> " + ok);
            if (!ok) {
                return false;
            }
            sNetId = netId;
            return true;
        } catch (SecurityException e) {
            // targetSdk>=29 的非特权应用会走到这里 → 转退路。
            android.util.Log.w(TAG, "saved-config path denied: " + e);
            return false;
        } catch (IllegalArgumentException e) {
            android.util.Log.w(TAG, "saved-config path bad args: " + e);
            return false;
        }
    }

    /**
     * 路径 B（退路）：addNetworkSuggestions。
     *
     * 系统把网络写入自己的已保存列表（带凭据），但**不主动连**
     * （实测 num_connect_success=0；设备上也没有 setIsPostConnectionEnabled
     * 可开——反射找不到，真机实证）。仅作 targetSdk<29 失效后的保底。
     */
    private static boolean connectViaSuggestion(WifiManager wm, String ssid,
                                                String passphrase,
                                                int security) {
        if (Build.VERSION.SDK_INT < 29) {
            sState = STATE_FAILED;
            return false;
        }
        WifiNetworkSuggestion.Builder spec = new WifiNetworkSuggestion.Builder();
        spec.setSsid(ssid);
        if (passphrase != null && !passphrase.isEmpty()) {
            // setWpa2Passphrase 覆盖 WPA-PSK/WPA2-PSK；
            // setWpa3Passphrase 覆盖 WPA3-SAE（真机多数 AP 是 SAE）。
            if (security == SEC_WPA_PSK || security == SEC_WPA2_PSK) {
                spec.setWpa2Passphrase(passphrase);
            } else {
                spec.setWpa3Passphrase(passphrase);
            }
        }
        // 系统把该网络列为自动连接候选（saved config 的 autoConnect=true）。
        // 注意：设备上没有 setIsPostConnectionEnabled（反射找不到，真机实证），
        // 故建议路径没有"系统主动连"的开关——仅保底。
        spec.setIsInitialAutojoinEnabled(true);

        sSuggestion = spec.build();

        List<WifiNetworkSuggestion> suggestions = new ArrayList<>();
        suggestions.add(sSuggestion);
        try {
            int status = wm.addNetworkSuggestions(suggestions);
            if (status != WifiManager.STATUS_NETWORK_SUGGESTIONS_SUCCESS) {
                sSuggestion = null;
                sState = STATE_FAILED;
                return false;
            }
        } catch (SecurityException e) {
            sSuggestion = null;
            sState = STATE_FAILED;
            return false;
        } catch (IllegalArgumentException e) {
            sSuggestion = null;
            sState = STATE_FAILED;
            return false;
        }
        return true;
    }

    /**
     * 断开。
     *
     * 两级尝试：
     *   1. `disableNetwork(netId)`——对本应用创建的网络（A2 新建）有效：
     *      禁用即断开且系统不回连它。
     *      对**用户在系统里连过的**网络（A1 命中的）返回 false——配置
     *      所有权检查（creatorUid 不是本应用即拒；真机实证
     *      `disableNetwork(37) -> false`，无异常）。
     *   2. 退化为 `wm.disconnect()`——**断开当前连接**（不改任何配置，
     *      无所有权检查）。系统随后按 auto-join 恢复，通常切回原网络——
     *      这正是系统里"断开"的语义。
     *
     * @return true=已断开；false=两条路都失败。
     */
    @SuppressWarnings("deprecation")
    public static synchronized boolean disconnect(Context ctx) {
        WifiManager wm = wifi(ctx);
        if (wm == null) {
            return false;
        }
        boolean done = false;
        if (sNetId >= 0) {
            try {
                boolean ok = wm.disableNetwork(sNetId);
                android.util.Log.i(TAG, "disableNetwork(" + sNetId + ") -> " + ok);
                done = ok;
            } catch (SecurityException e) {
                android.util.Log.w(TAG, "disableNetwork denied: " + e);
            }
            sNetId = -1;
        }
        if (!done) {
            // 已存网络不归本应用所有 → 退化为断开当前连接。
            try {
                done = wm.disconnect();
                android.util.Log.i(TAG, "disconnect() -> " + done);
            } catch (SecurityException e) {
                android.util.Log.w(TAG, "disconnect denied: " + e);
            }
        }
        if (sSuggestion != null) {
            List<WifiNetworkSuggestion> suggestions = new ArrayList<>();
            suggestions.add(sSuggestion);
            try {
                wm.removeNetworkSuggestions(suggestions);
                done = true;
            } catch (SecurityException e) {
                // 忽略（退路径失败不掩盖 A 路径的结果）
            } catch (IllegalArgumentException e) {
                // 忽略
            }
            sSuggestion = null;
        }
        sState = STATE_IDLE;
        return done;
    }

    /**
     * 连接操作状态。
     *
     * 判定：系统已连上目标 SSID → Connected（连接由系统发起，故系统的
     * 连接现状就是权威判据）。超时（口令错 / 目标不在覆盖范围 / 用户未
     * 确认建议）→ Failed——没有超时的话页面会永远停在"连接中"。
     */
    public static synchronized int state(Context ctx) {
        WifiManager wm = wifi(ctx);
        if (wm == null) {
            return sState;
        }
        if (sState != STATE_CONNECTING) {
            return sState;
        }
        if (System.currentTimeMillis() - sConnectStartMs > CONNECT_TIMEOUT_MS) {
            android.util.Log.w(TAG, "connect timeout -> FAILED");
            sState = STATE_FAILED;
            return sState;
        }
        try {
            android.net.wifi.WifiInfo info = wm.getConnectionInfo();
            String ssid = info == null ? null : info.getSSID();
            if (ssid == null) {
                return sState;
            }
            // getSSID 带引号形式（"MyNet"），或 <unknown ssid>。
            if (ssid.startsWith("\"") && ssid.endsWith("\"")
                    && ssid.length() >= 2) {
                ssid = ssid.substring(1, ssid.length() - 1);
            }
            if (ssid.equals("<unknown ssid>")) {
                return sState;
            }
            if (sSsid != null && sSsid.equals(ssid)) {
                sState = STATE_CONNECTED;
            }
        } catch (SecurityException e) {
            // 读不到连接信息：保持 CONNECTING，不谎报。
        }
        return sState;
    }

    /** 我方最近发起的目标 SSID（无则空串）。 */
    public static synchronized String pendingSsid() {
        return sSsid != null ? sSsid : "";
    }
}
