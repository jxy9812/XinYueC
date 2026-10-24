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

import android.app.NativeActivity;
import android.os.Bundle;
import android.view.View;
import android.view.ViewGroup;

public class XGuiNativeActivity extends NativeActivity {

    /** 无障碍虚拟视图树宿主（0×0，仅挂载节点树）。 */
    private View a11yHost;

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
        installAccessibilityHost();
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
