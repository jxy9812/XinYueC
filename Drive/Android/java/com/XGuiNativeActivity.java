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
     * 写入系统「屏幕超时」时长（毫秒）——即委托系统自动息屏。
     *
     * 需要 WRITE_SETTINGS 权限（安卓 6.0 起为「特殊权限」：须由用户在
     * 系统设置里对本应用单独授权，声明权限本身不等于已获授权）。未授权
     * 时 Settings.System.putInt 抛 SecurityException，此处吞掉并返回
     * false，由 native 侧如实上报「未获授权」——不静默假成功。
     *
     * @param millis 超时毫秒；<=0 表示「从不自动熄屏」（系统侧写 0 即
     *                关闭屏幕超时）。
     * @return true=已写入；false=无权限或写入失败。
     */
    public boolean setScreenOffTimeout(int millis) {
        try {
            int value = (millis > 0) ? millis : 0;
            return android.provider.Settings.System.putInt(
                    getContentResolver(),
                    android.provider.Settings.System.SCREEN_OFF_TIMEOUT, value);
        } catch (Throwable ignored) {
        }
        return false;
    }

    /**
     * 是否已获 WRITE_SETTINGS 授权（可写系统设置）。
     *
     * @return true=已授权；false=未授权（界面据此提示用户去系统设置授权）。
     */
    public boolean canWriteSystemSettings() {
        try {
            // Settings.System.canWrite 的入参是 Context（Activity 本身即
            // Context），不是 ContentResolver——传错类型编译不过。
            return android.provider.Settings.System.canWrite(this);
        } catch (Throwable ignored) {
        }
        return false;
    }

    /**
     * 引导用户到系统设置页为本应用授予「修改系统设置」权限。
     *
     * WRITE_SETTINGS 是安卓的「特殊权限」：清单声明 ≠ 已获授权，须由
     * 用户在系统设置里逐应用开启。本方法跳转 ACTION_MANAGE_WRITE_
     * SETTINGS（本应用详情页），用户开启后应用即可写系统「屏幕超时」。
     *
     * @return true=已跳转；false=跳转失败（活动不存在等）。
     */
    public boolean requestWriteSettingsPermission() {
        try {
            android.content.Intent intent = new android.content.Intent(
                    android.provider.Settings.ACTION_MANAGE_WRITE_SETTINGS,
                    android.net.Uri.parse("package:" + getPackageName()));
            intent.addFlags(android.content.Intent.FLAG_ACTIVITY_NEW_TASK);
            startActivity(intent);
            return true;
        } catch (Throwable ignored) {
        }
        return false;
    }

    /**
     * 读取应用窗口当前是否全屏（窗口边界 == 显示真实边界）。
     *
     * 口径：decorView 尺寸 vs Display.getRealMetrics（显示**真实**尺寸，
     * 不随旋转/内容裁剪变化）。相等=全屏（沉浸式/普通全屏 Activity）；
     * 小于=应用运行在窗口化形态（自由窗口/分屏），此时框架窗口状态应
     * 保持 Normal 而不是想当然推全屏（用户裁定 2026-10-27：启动**读取**
     * 平台真实状态对齐，不硬编码）。
     *
     * @return true=全屏（含读取失败兜底——真机常态即全屏）；false=窗口化。
     */
    public boolean isWindowFullscreen() {
        try {
            View decor = getWindow().getDecorView();
            android.view.Display display = getWindowManager().getDefaultDisplay();
            if (decor == null || display == null) return true;
            android.util.DisplayMetrics real = new android.util.DisplayMetrics();
            display.getRealMetrics(real);
            int dw = decor.getWidth();
            int dh = decor.getHeight();
            if (dw <= 0 || dh <= 0) return true;  /* 未完成布局：兜底全屏。 */
            return dw >= real.widthPixels && dh >= real.heightPixels;
        } catch (Throwable ignored) {
            return true;
        }
    }

    /**
     * 申请显示方向（自横屏起顺时针度数）。
     *
     * 口径与框架统一：0=横屏、90=竖屏、180=倒置横屏、270=倒置竖屏，
     * -1=跟随系统（交回系统按传感器/用户设置决定）。
     *
     * 安卓的显示方向只能由系统掌握——Activity.setRequestedOrientation 是
     * 唯一正规入口（清单 android:screenOrientation 只给初值，运行期改向
     * 必须走这个 API）。系统采纳后会重建 surface，应用侧表现为收到一次
     * resize；native 侧不自行转任何像素（那是「画布旋转」，两者正交）。
     *
     * 注意：setRequestedOrientation 属 UI 线程 API，非 UI 线程调用时
     * 投到主线程执行（受理语义：本次调用返回即表示请求已下发）。
     *
     * @param degrees 目标角度（0/90/180/270），-1=跟随系统。
     */
    public void setRequestedOrientationByDegrees(int degrees) {
        final int mode;
        switch (degrees) {
            case 0:
                mode = android.content.pm.ActivityInfo.SCREEN_ORIENTATION_LANDSCAPE;
                break;
            case 90:
                mode = android.content.pm.ActivityInfo.SCREEN_ORIENTATION_PORTRAIT;
                break;
            case 180:
                mode = android.content.pm.ActivityInfo.SCREEN_ORIENTATION_REVERSE_LANDSCAPE;
                break;
            case 270:
                mode = android.content.pm.ActivityInfo.SCREEN_ORIENTATION_REVERSE_PORTRAIT;
                break;
            default:
                mode = android.content.pm.ActivityInfo.SCREEN_ORIENTATION_UNSPECIFIED;
                break;
        }
        Runnable apply = new Runnable() {
            @Override
            public void run() {
                setRequestedOrientation(mode);
            }
        };
        try {
            if (android.os.Looper.myLooper() == android.os.Looper.getMainLooper()) {
                apply.run();
            } else {
                runOnUiThread(apply);
            }
        } catch (Throwable ignored) {
            // 请求失败不抛给 native：调用方按「未生效」处理。
        }
    }

    /**
     * 回读当前**实际**显示方向（自横屏起顺时针度数；未知返回 -1）。
     *
     * 判定链：Display.getRotation() 给出相对「面板自然方向」的旋转档
     * （0/90/180/270），Display.getRealMetrics() 给出**不随旋转变换**的
     * 面板自然尺寸——两者合起来才能把「自然竖屏的设备转到横屏」与
     * 「自然横屏的设备停在横屏」区分开（只读 rotation 无法区分：两种
     * 设备的 ROTATION_90 含义相反）。
     */
    public int getDisplayOrientationDegrees() {
        try {
            android.view.Display display = getWindowManager().getDefaultDisplay();
            if (display == null) {
                return -1;
            }
            android.util.DisplayMetrics real = new android.util.DisplayMetrics();
            display.getRealMetrics(real);
            int rot = display.getRotation() & 3;
            boolean naturalPortrait = real.widthPixels < real.heightPixels;
            if (naturalPortrait) {
                /* 自然竖屏设备：rot0=正竖(90) rot1=横(0) rot2=倒竖(270)
                 * rot3=倒横(180)。 */
                switch (rot) {
                    case 0: return 90;
                    case 1: return 0;
                    case 2: return 270;
                    default: return 180;
                }
            }
            /* 自然横屏设备：rot0=正横(0) rot1=竖(90) rot2=倒横(180)
             * rot3=倒竖(270)。 */
            switch (rot) {
                case 0: return 0;
                case 1: return 90;
                case 2: return 180;
                default: return 270;
            }
        } catch (Throwable ignored) {
        }
        return -1;
    }

    /**
     * 系统是否会采纳应用的方向请求。
     *
     * Android 12 起系统会在**大屏设备**上忽略 targetSdk>=31 应用的方向
     * 请求；Android 16（API 36）起该策略扩大到全部应用（大屏设备一律
     * 忽略）。本应用 targetSdk=28，故 API 31~35 的大屏仍采纳，API 36+
     * 不采纳——此处如实回报，供 native 侧把「窗口方向」控件置灰而不是
     * 让用户点了没反应。
     */
    public boolean canRequestOrientation() {
        try {
            int sdk = android.os.Build.VERSION.SDK_INT;
            if (sdk >= 36) {
                int swDp = getResources().getConfiguration().smallestScreenWidthDp;
                return swDp < 600;
            }
            return true;
        } catch (Throwable ignored) {
        }
        return false;
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
