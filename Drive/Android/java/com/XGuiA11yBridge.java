/**
 * XGuiA11yBridge —— Java ↔ native 的无障碍桥（JNI 声明）。
 *
 * native 侧实现见 Drive/Android/Graphics/XPlatformAccessibilityJNI_android.c：
 * 节点数据来自渲染线程构建的控件树快照（XPlatformAccessibility_android.c），
 * 本类只做调用转发。
 */
package com.xinyuec.android;

public final class XGuiA11yBridge {

    /* 节点标志位（与 native 侧 XA11Y_FLAG_* 对齐）。 */
    public static final int FLAG_FOCUSABLE = 0x0001;
    public static final int FLAG_CLICKABLE = 0x0002;
    public static final int FLAG_ENABLED   = 0x0004;
    public static final int FLAG_VISIBLE   = 0x0008;
    public static final int FLAG_EDITABLE  = 0x0010;
    public static final int FLAG_CHECKED   = 0x0020;

    private XGuiA11yBridge() {
    }

    /**
     * 取节点信息。
     *
     * @param index native 快照下标
     * @return 11 元字符串数组（下标越界返回 null）：
     *         [0]=安卓类名 [1]=文本 [2]=描述 [3]=标志位(十进制)
     *         [4]=x [5]=y [6]=宽 [7]=高
     *         [8]=父下标 [9]=首子下标 [10]=子数
     */
    static native String[] nativeNodeInfo(int index);

    /** 触发节点对应控件的点击（读屏器 ACTION_CLICK）。 */
    static native boolean nativePerformClick(int index);

    /** 触发节点对应控件的聚焦（读屏器 ACTION_FOCUS）。 */
    static native boolean nativePerformFocus(int index);

    /** 当前快照节点数（诊断用）。 */
    static native int nativeNodeCount();

    /**
     * 虚拟树根节点下标（顶层控件首节点；无则 -1）。
     * 宿主根节点（HOST_VIEW_ID）需要把整棵树的根挂上去。
     */
    static native int nativeRootIndex();
}
