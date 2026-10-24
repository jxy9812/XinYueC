/**
 * XNetAdapter —— 安卓网卡配置适配器（供 XDeviceNetwork 的 posix 后端经 JNI 调用）。
 *
 * 背景（2026-10-24）：demo 网络设置页的"模式/网关/DNS/IPv6"读自
 * XNetworkInterface_queryConfig → posix 后端。该后端的常规取数在安卓全空：
 *   - 网关：读 /proc/net/route —— 应用域被 SELinux 拒（/proc/net 不可读）；
 *   - DNS：读 /etc/resolv.conf —— 安卓没有（DNS 由 netd 按网络管理）；
 *   - DHCP 模式：找 udhcpc pid/租约文件 —— 安卓不存在 → 恒判"静态"。
 * 系统级权威源：
 *   - WifiManager.getDhcpInfo()：v4 网关/DNS/DHCP 服务器/租约（模式判定）；
 *   - ConnectivityManager.getLinkProperties(activeNetwork)：v6 默认网关 +
 *     实际下发 DNS（v4/v6 皆可，比 DhcpInfo 全）。
 *
 * 设置（切换静态/动态）：安卓 WiFi 的 IP 模式在 WifiConfiguration 上
 * （ipAssignment=STATIC/DHCP + staticIpConfiguration）。这两个成员已从
 * 公开 SDK 移除（android-37 的 android.jar 里没有），设备运行时仍在——
 * 全部经反射。且受配置所有权限制：**用户在系统设置里连过的网络，
 * updateNetwork 会被拒**（"does not have permission to update
 * configuration"）；应用自己创建的网络（XWifiConnector A2 新建）可改。
 * IPv6 静态：AOSP 的 StaticIpConfiguration 只支持 v4 地址——安卓系统
 * 整体不提供 WiFi 静态 IPv6（系统设置页同样没有该选项），如实不支持。
 */
package com.xinyuec.android;

import android.content.Context;
import android.net.ConnectivityManager;
import android.net.DhcpInfo;
import android.net.LinkProperties;
import android.net.Network;
import android.net.RouteInfo;
import android.net.wifi.WifiConfiguration;
import android.net.wifi.WifiInfo;
import android.net.wifi.WifiManager;
import java.lang.reflect.Constructor;
import java.lang.reflect.Field;
import java.lang.reflect.Method;
import java.net.InetAddress;
import java.util.ArrayList;
import java.util.Collection;
import java.util.List;

public class XNetAdapter {

    private static final String TAG = "XNetAdapter";

    /**
     * 查询活动网络的链路信息（ConnectivityManager.LinkProperties）。
     *
     * @return [0]=IPv6 默认网关文本, [1]=DNS1, [2]=DNS2, [3]=接口名（如
     *         "wlan0"）；缺项为 ""。调用方（C 侧）按接口名匹配后才采用。
     */
    public static String[] linkInfo(Context ctx) {
        String[] out = {"", "", "", ""};
        try {
            ConnectivityManager cm = (ConnectivityManager)
                    ctx.getSystemService(Context.CONNECTIVITY_SERVICE);
            if (cm == null) return out;
            Network net = cm.getActiveNetwork();
            if (net == null) return out;
            LinkProperties lp = cm.getLinkProperties(net);
            if (lp == null) return out;
            String iface = lp.getInterfaceName();
            out[3] = iface == null ? "" : iface;
            for (RouteInfo r : lp.getRoutes()) {
                if (r == null || !r.isDefaultRoute()) continue;
                InetAddress gw = r.getGateway();
                if (gw == null) continue;
                String t = gw.getHostAddress();
                if (t != null && t.contains(":") && out[0].isEmpty()) {
                    out[0] = t; /* IPv6 默认网关（v4 网关由 DhcpInfo 覆盖） */
                }
            }
            List<InetAddress> dns = lp.getDnsServers();
            int taken = 0;
            for (InetAddress d : dns) {
                if (d == null || taken >= 2) continue;
                String t = d.getHostAddress();
                if (t == null || t.isEmpty()) continue;
                if (taken == 0) { out[1] = t; taken = 1; }
                else { out[2] = t; break; }
            }
        } catch (Throwable t) {
            android.util.Log.w(TAG, "linkInfo: " + t);
        }
        return out;
    }

    /**
     * 切换为 DHCP（动态获取）。
     *
     * 当前已是 DHCP（有租约）→ 目标态已达成，直接成功；否则对当前连接的
     * WiFi 配置 updateNetwork(ipAssignment=DHCP)——对用户在系统里连过的
     * 网络会被所有权检查拒绝（如实返回 false）。
     */
    @SuppressWarnings("deprecation")
    public static boolean setDhcpMode(Context ctx) {
        try {
            WifiManager wm = (WifiManager)
                    ctx.getSystemService(Context.WIFI_SERVICE);
            if (wm == null) return false;
            DhcpInfo di = wm.getDhcpInfo();
            if (di != null && di.serverAddress != 0) {
                return true; /* 已是 DHCP（租约在）——目标态已达成。 */
            }
            int netId = currentNetId(wm);
            if (netId < 0) return false;
            WifiConfiguration cfg = new WifiConfiguration();
            cfg.networkId = netId;
            if (!setIpAssignment(cfg, "DHCP")) return false;
            int r = wm.updateNetwork(cfg);
            android.util.Log.i(TAG, "setDhcp updateNetwork -> " + r);
            return r != -1;
        } catch (Throwable t) {
            android.util.Log.w(TAG, "setDhcpMode: " + t);
            return false;
        }
    }

    /**
     * 切换为静态 IPv4（反射 WifiConfiguration.ipAssignment=STATIC +
     * staticIpConfiguration；两者已从 SDK 移除，设备运行时仍在）。
     *
     * 同样受所有权限制：仅应用自己创建的网络可改。
     */
    @SuppressWarnings("deprecation")
    public static boolean setStaticV4(Context ctx, String ip, int prefix,
                                      String gw, String dns1, String dns2) {
        try {
            WifiManager wm = (WifiManager)
                    ctx.getSystemService(Context.WIFI_SERVICE);
            if (wm == null || ip == null || ip.isEmpty() || prefix <= 0) {
                return false;
            }
            int netId = currentNetId(wm);
            if (netId < 0) {
                android.util.Log.w(TAG, "setStaticV4: no current network");
                return false;
            }
            Object staticCfg = buildStaticIp(ip, prefix, gw, dns1, dns2);
            if (staticCfg == null) {
                android.util.Log.w(TAG, "setStaticV4: build static cfg failed");
                return false;
            }
            WifiConfiguration cfg = new WifiConfiguration();
            cfg.networkId = netId;
            if (!setIpAssignment(cfg, "STATIC")) return false;
            if (!setStaticIpField(cfg, staticCfg)) return false;
            int r = wm.updateNetwork(cfg);
            android.util.Log.i(TAG, "setStatic updateNetwork -> " + r);
            return r != -1;
        } catch (Throwable t) {
            android.util.Log.w(TAG, "setStaticV4: " + t);
            return false;
        }
    }

    /* ==================== 内部辅助 ==================== */

    /** 当前连接的 WiFi 配置 networkId（按 SSID 匹配已保存列表；无 -1）。 */
    @SuppressWarnings("deprecation")
    private static int currentNetId(WifiManager wm) {
        try {
            WifiInfo info = wm.getConnectionInfo();
            if (info == null) return -1;
            String ssid = info.getSSID();
            if (ssid == null || ssid.equals("<unknown ssid>")) return -1;
            ssid = stripQuotes(ssid);
            List<WifiConfiguration> list = wm.getConfiguredNetworks();
            if (list == null) return -1;
            for (WifiConfiguration c : list) {
                if (c == null || c.SSID == null) continue;
                if (ssid.equals(stripQuotes(c.SSID))) return c.networkId;
            }
        } catch (Throwable t) {
            android.util.Log.w(TAG, "currentNetId: " + t);
        }
        return -1;
    }

    private static String stripQuotes(String s) {
        if (s.startsWith("\"") && s.endsWith("\"") && s.length() >= 2) {
            return s.substring(1, s.length() - 1);
        }
        return s;
    }

    /** 反射设置 WifiConfiguration.ipAssignment（枚举 STATIC/DHCP）。 */
    private static boolean setIpAssignment(WifiConfiguration cfg,
                                           String value) {
        try {
            Field f = WifiConfiguration.class.getField("ipAssignment");
            Class<?> enumCls = f.getType();
            Object enumValue = Enum.valueOf((Class<Enum>) enumCls.asSubclass(Enum.class), value);
            f.set(cfg, enumValue);
            return true;
        } catch (Throwable t) {
            android.util.Log.w(TAG, "setIpAssignment(" + value + "): " + t);
            return false;
        }
    }

    /** 反射设置 WifiConfiguration.staticIpConfiguration 字段。 */
    private static boolean setStaticIpField(WifiConfiguration cfg,
                                            Object staticCfg) {
        try {
            Field f = WifiConfiguration.class.getField("staticIpConfiguration");
            f.set(cfg, staticCfg);
            return true;
        } catch (Throwable t) {
            android.util.Log.w(TAG, "setStaticIpField: " + t);
            return false;
        }
    }

    /**
     * 反射构造 android.net.StaticIpConfiguration（构造器与 setter 在 SDK
     * 桩里不可见，设备运行时存在；兼容 setDnsServers 的 List/Collection 形参）。
     */
    private static Object buildStaticIp(String ip, int prefix,
                                        String gw, String dns1, String dns2) {
        try {
            Class<?> cls = Class.forName("android.net.StaticIpConfiguration");
            Constructor<?> ctor = cls.getDeclaredConstructor();
            ctor.setAccessible(true);
            Object obj = ctor.newInstance();

            InetAddress addr = InetAddress.getByName(ip);
            Class<?> laCls = Class.forName("android.net.LinkAddress");
            Constructor<?> laCtor = laCls.getConstructor(InetAddress.class,
                                                         int.class);
            Object linkAddr = laCtor.newInstance(addr, prefix);
            cls.getMethod("setIpAddress", laCls).invoke(obj, linkAddr);

            if (gw != null && !gw.isEmpty()) {
                cls.getMethod("setGateway", InetAddress.class)
                        .invoke(obj, InetAddress.getByName(gw));
            }
            List<InetAddress> dns = new ArrayList<>();
            if (dns1 != null && !dns1.isEmpty()) {
                dns.add(InetAddress.getByName(dns1));
            }
            if (dns2 != null && !dns2.isEmpty()) {
                dns.add(InetAddress.getByName(dns2));
            }
            Method mSetDns = null;
            try {
                mSetDns = cls.getMethod("setDnsServers", Collection.class);
            } catch (NoSuchMethodException e) {
                mSetDns = cls.getMethod("setDnsServers", List.class);
            }
            mSetDns.invoke(obj, dns);
            return obj;
        } catch (Throwable t) {
            android.util.Log.w(TAG, "buildStaticIp: " + t);
            return null;
        }
    }
}
