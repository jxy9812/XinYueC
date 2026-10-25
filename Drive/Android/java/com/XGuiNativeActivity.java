/**
 * XGuiNativeActivity —— NativeActivity 的 Java 子类（安卓无障碍桥宿主）。
 *
 * 背景（2026-10-24 安卓无障碍接入）：安卓读屏器（TalkBack）只认 Java 侧
 * View 树。纯 native 画布（android.app.NativeActivity）在系统眼里只是
 * "一个 View"，读屏器念不出任何控件。本类做两件事：
 *   1. 继承 NativeActivity，保留原生窗口/输入通道（libxguidemo.so 的
 *      ANativeActivity_onCreate 仍由 NativeActivity 触发，行为不变）；
 *   2. 在窗口上装一个"虚拟视图树"宿主 View（XGuiA11yHost），把框架的
 *      XWidget 控件树以 AccessibilityNodeProvider 形式暴露给系统，
 *      节点内容由 native 侧快照提供（见 XPlatformAccessibility_android.c）。
 *
 * 注意：宿主 View 尺寸为 0×0 且不消费触摸事件（不 focusable/clickable），
 * 仅作为无障碍节点树的挂载点；触摸仍由 native 画布与输入队列处理。
 */
package com.xinyuec.android;

import android.Manifest;
import android.app.NativeActivity;
import android.content.pm.PackageManager;
import android.os.Bundle;
import android.view.View;
import android.view.ViewGroup;

public class XGuiNativeActivity extends NativeActivity {

    /** 无障碍虚拟视图树宿主（0×0，仅挂载节点树）。 */
    private View a11yHost;

    /** 定位权限请求码（WiFi 扫描结果用）。 */
    private static final int REQ_WIFI_SCAN_LOCATION = 0x5757;

    static {
        // NativeActivity 由系统按 meta-data android.app.lib_name 直接
        // dlopen 本库，**不经过 System.loadLibrary**——ART 因而不会调用
        // 库的 JNI_OnLoad，无障碍桥的 RegisterNatives 就不会执行
        // （真机实证：Java 侧报 "No implementation found"）。这里显式
        // 加载一次：ART 会调 JNI_OnLoad 完成方法绑定。系统侧的加载在
        // 之前已完成，重复 load 同一 so 是幂等的（同一 dlopen 句柄）。
        try {
            System.loadLibrary("xguidemo");
        } catch (Throwable ignored) {
            // 加载失败：原生侧仍由 NativeActivity 正常拉起（系统 dlopen
            // 路径不受影响），仅无障碍桥不可用。
        }
    }

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        installImmersive();
        requestWifiScanPermissions();
        installAccessibilityHost();
    }

    /**
     * 申请 WiFi 扫描所需的定位权限（ACCESS_FINE_LOCATION）。
     *
     * 背景（2026-10-25 真机实证）：安卓自 API 26 起把 WiFi 扫描结果列为
     * **位置数据**——没有定位权限时 `WifiManager.getScanResults()` 不抛
     * 异常，只**静默返回空表**。表现是：点「扫描」后状态机正常走完
     * （扫描中 → 扫描完成），但网络下拉框永远是空的，与"扫描功能失效"
     * 无法区分。系统侧 `cmd wifi list-scan-results` 同时能列出十几个 AP，
     * 对照即可确认是权限而非扫描本身。
     *
     * 定位是**运行时权限**，只能由应用在界面里向用户申请，install-time
     * 声明（清单里的 uses-permission）不够；adb 侧在 MIUI 上也常因
     * GRANT_RUNTIME_PERMISSIONS 受限而 `pm grant` 失败。故由本 Activity
     * 在启动时申请——首次安装/换签名重装后系统会重置该授权，必须重新弹。
     *
     * 只申请 FINE（COARSE 由系统随 FINE 一并授予，API 31+ 用户在弹窗里
     * 选"大致位置"时应用仍能扫描，结果精度不受影响——扫描列表本身不含
     * 坐标）。API 33+ 另有 NEARBY_WIFI_DEVICES，但仅对 targetSdk>=33 的
     * 应用强制；本应用 targetSdk=28，FINE 即可，不引入多余权限。
     */
    private void requestWifiScanPermissions() {
        try {
            if (checkSelfPermission(Manifest.permission.ACCESS_FINE_LOCATION)
                    == PackageManager.PERMISSION_GRANTED) {
                return;
            }
            requestPermissions(
                    new String[]{Manifest.permission.ACCESS_FINE_LOCATION},
                    REQ_WIFI_SCAN_LOCATION);
        } catch (Throwable ignored) {
            // 申请失败不阻断启动：扫描结果为空，功能其余部分照常。
        }
    }

    /**
     * 权限申请结果。
     *
     * 只记日志（logcat 标签 XGuiDemo），不弹自己的提示：拒绝时扫描结果
     * 如实为空，用户可随时在系统设置里补授权（系统在下次启动时会再次
     * 走到上面的申请逻辑）。日志用于真机排查"扫描空表"，把根因从
     * "扫描坏了"缩小到"权限被拒"。
     */
    @Override
    public void onRequestPermissionsResult(int requestCode,
                                           String[] permissions,
                                           int[] grantResults) {
        super.onRequestPermissionsResult(requestCode, permissions,
                                         grantResults);
        if (requestCode != REQ_WIFI_SCAN_LOCATION) {
            return;
        }
        boolean granted = grantResults.length > 0
                && grantResults[0] == PackageManager.PERMISSION_GRANTED;
        android.util.Log.i("XGuiDemo",
                "wifi-scan location permission "
                        + (granted ? "granted" : "DENIED")
                        + " (scan results will be "
                        + (granted ? "available" : "EMPTY until granted)")
                        + ")");
    }

    /**
     * 沉浸式全屏（用户裁定 2026-10-25 安卓默认全屏）。
     *
     * 背景：本机（安卓 15）对 targetSdk 28 的应用仍强制 edge-to-edge——
     * surface 恒为全屏（实测 1200x2608），系统状态栏（时钟/电量）叠画在
     * 应用之上。此前方案是"内容让位状态栏"（根布局下移 48dp）；用户最终
     * 选择沉浸式：状态栏整个隐藏，应用独占整屏，从顶部边缘下滑可临时
     * 呼出（半透明，自动收回）。
     *
     * 做法：decorFitsSystemWindows(false) 让 surface 拿满全屏 +
     * InsetsController.hide(statusBars) 隐藏状态栏 + BY_SWIPE 行为位
     * （呼出为瞬时条）。同时清除「浅色状态栏」外观标志——瞬时呼出条
     * 叠在应用深蓝顶栏上，白色图标才可读。native 侧的状态栏高度查询
     * （getStatusBarHeightPx）在隐藏后自然回报 0，让位逻辑自动失效。
     */
    private void installImmersive() {
        try {
            getWindow().setDecorFitsSystemWindows(false);
            /* 挖孔屏：状态栏隐藏后，老 targetSdk 应用默认**不允许窗口
               伸入挖孔区**（本机 144px ≈ 前摄开孔），surface 会被缩成
               2464、顶部留黑带（实测）。SHORT_EDGES/ALWAYS 允许窗口
               铺进挖孔区——状态栏已藏，那里就是应用的顶栏。 */
            android.view.WindowManager.LayoutParams lp = getWindow().getAttributes();
            if (android.os.Build.VERSION.SDK_INT >= 30) {
                lp.layoutInDisplayCutoutMode =
                        android.view.WindowManager.LayoutParams
                                .LAYOUT_IN_DISPLAY_CUTOUT_MODE_ALWAYS;
            } else if (android.os.Build.VERSION.SDK_INT >= 28) {
                lp.layoutInDisplayCutoutMode =
                        android.view.WindowManager.LayoutParams
                                .LAYOUT_IN_DISPLAY_CUTOUT_MODE_SHORT_EDGES;
            }
            getWindow().setAttributes(lp);
            /* 旧版旗标对新系统上的老 targetSdk 应用**仍然有效**：本应用
               targetSdk 28，实测 setDecorFitsSystemWindows 被系统忽略、
               surface 仍按状态栏旧位置内缩（顶部 144px 黑带）——
               LAYOUT_FULLSCREEN 让窗口布局铺满全屏，FULLSCREEN+IMMERSIVE
               把状态栏藏掉。新旧两套同时上，各吃各的。 */
            View decor = getWindow().getDecorView();
            decor.setSystemUiVisibility(
                    View.SYSTEM_UI_FLAG_LAYOUT_STABLE
                            | View.SYSTEM_UI_FLAG_LAYOUT_FULLSCREEN
                            | View.SYSTEM_UI_FLAG_FULLSCREEN
                            | View.SYSTEM_UI_FLAG_IMMERSIVE_STICKY);
            if (android.os.Build.VERSION.SDK_INT >= 30) {
                android.view.WindowInsetsController c = getWindow().getInsetsController();
                if (c != null) {
                    c.hide(android.view.WindowInsets.Type.statusBars());
                    c.setSystemBarsBehavior(
                            android.view.WindowInsetsController
                                    .BEHAVIOR_SHOW_TRANSIENT_BARS_BY_SWIPE);
                    c.setSystemBarsAppearance(
                            0,
                            android.view.WindowInsetsController
                                    .APPEARANCE_LIGHT_STATUS_BARS);
                }
            }
        } catch (Throwable ignored) {
            // 沉浸失败退回可让位版式（native 侧按 insets 自适应），不阻断启动。
        }
    }

    /**
     * 状态栏内缩高度（物理 px）；取不到返回 0。
     *
     * 背景（2026-10-25 手机竖横屏适配）：本机（安卓 15）**强制
     * edge-to-edge**——即便清单主题非 Fullscreen，native surface 仍拿到
     * 全屏尺寸（实测 1200x2608），系统状态栏（时钟/电量）直接叠在应用
     * 之上。demo 的应用栏若不避让，标题会被状态栏图标压住、顶部导航
     * 图标（☰）也点不到。
     *
     * native 侧（XSystemAndroid.c 的 JNI 通道
     * XSystemAndroid_queryStatusBarHeightPx）按此值把框架根布局的上边距
     * 让出来：应用栏落在状态栏之下。内容视图本身不内缩——native surface
     * 由系统直接管理，Java 侧改 padding 不生效（实测 surface 仍为
     * 1200x2608）。
     */
    public int getStatusBarHeightPx() {
        try {
            View decor = getWindow().getDecorView();
            if (android.os.Build.VERSION.SDK_INT >= 30) {
                if (decor.getRootWindowInsets() != null) {
                    return decor.getRootWindowInsets()
                            .getInsets(android.view.WindowInsets.Type.statusBars()).top;
                }
            } else if (decor.getRootWindowInsets() != null) {
                return decor.getRootWindowInsets().getSystemWindowInsetTop();
            }
        } catch (Throwable ignored) {
        }
        return 0;
    }

    /**
     * 安装无障碍宿主 View。
     * 说明：NativeActivity 的内容视图是 native 侧 surface，Java 侧没有
     * 可挂控件的容器——故用 WindowManager 的 content 视图（addContentView）
     * 追加宿主，供 AccessibilityNodeProvider 挂载节点树。
     *
     * 尺寸取 MATCH_PARENT：0×0 的宿主会被系统当作不可见节点跳过，虚拟
     * 树整棵不可达。宿主 onTouchEvent 返回 false（触摸透传），不影响
     * native 侧触摸。
     */
    private void installAccessibilityHost() {
        try {
            a11yHost = new XGuiA11yHost(this);
            ViewGroup decor = (ViewGroup) getWindow().getDecorView();
            ViewGroup content = decor.findViewById(android.R.id.content);
            ViewGroup.LayoutParams params = new ViewGroup.LayoutParams(
                    ViewGroup.LayoutParams.MATCH_PARENT,
                    ViewGroup.LayoutParams.MATCH_PARENT);
            if (content != null) {
                content.addView(a11yHost, params);
            } else {
                decor.addView(a11yHost, params);
            }
            // 宿主本身不参与焦点与触摸，只提供节点树。
            a11yHost.setFocusable(false);
            a11yHost.setClickable(false);
            a11yHost.setImportantForAccessibility(
                    View.IMPORTANT_FOR_ACCESSIBILITY_YES);
        } catch (Throwable ignored) {
            // 无障碍是增强能力：任何失败都不影响应用本体运行。
        }
    }
}
