/**
 * XGuiA11yHost —— 无障碍虚拟视图树宿主（View + AccessibilityNodeProvider）。
 *
 * 节点数据由 native 侧提供（XPlatformAccessibility_android.c 构建的控件树
 * 快照）：本类只做"节点下标 → AccessibilityNodeInfo"的组装与动作回投。
 *
 * 虚拟视图机制：宿主 View 自身是 virtual id = HOST_VIEW_ID 的根节点；每个
 * native 节点用其"快照下标 + VIRTUAL_ID_BASE"作为 virtual id。子节点通过
 * AccessibilityNodeInfo.addChild(host, childVirtualId) 挂载——系统据此在
 * 同一宿主下展开子树。
 *
 * 触摸透传：宿主铺满窗口（0×0 会被系统当作不可见节点跳过），但 onTouchEvent
 * 返回 false，使事件继续派发给下层的 native SurfaceView——触摸行为零变化。
 */
package com.xinyuec.android;

import android.content.Context;
import android.graphics.Rect;
import android.view.MotionEvent;
import android.view.View;
import android.view.accessibility.AccessibilityNodeInfo;
import android.view.accessibility.AccessibilityNodeProvider;

import java.util.List;

public class XGuiA11yHost extends View {

    /** 虚拟节点：native 快照下标 + 偏移。 */
    public static final int VIRTUAL_ID_BASE = 1;

    /** Provider 单例（系统会反复询问 getAccessibilityNodeProvider）。 */
    private final XGuiA11yProvider provider;

    public XGuiA11yHost(Context context) {
        super(context);
        provider = new XGuiA11yProvider(this);
    }

    @Override
    public AccessibilityNodeProvider getAccessibilityNodeProvider() {
        return provider;
    }

    /**
     * 强制铺满父容器。
     *
     * 说明：宿主必须占据整屏，否则系统判定其"可见区域为空"→ 整棵虚拟子树
     * 被判不可见，读屏器直接跳过（uiautomator dump 实证：宿主节点 bounds
     * 曾为 [0,0][0,0]，子树虽在却挂在一个空矩形下）。NativeActivity 的内容
     * 视图是 native surface，常规 measure 拿不到可用尺寸，故自行按父容器
     * 尺寸铺满。
     */
    @Override
    protected void onMeasure(int widthMeasureSpec, int heightMeasureSpec) {
        View parent = (View) getParent();
        int w = parent != null ? parent.getWidth() : 0;
        int h = parent != null ? parent.getHeight() : 0;
        if (w <= 0) {
            w = getResources().getDisplayMetrics().widthPixels;
        }
        if (h <= 0) {
            h = getResources().getDisplayMetrics().heightPixels;
        }
        setMeasuredDimension(w, h);
    }

    /** 触摸透传：不消费任何事件，交回下层 native SurfaceView。 */
    @Override
    public boolean onTouchEvent(MotionEvent event) {
        return false;
    }

    /**
     * 节点提供者：按 virtual id 取 native 快照并组装节点信息。
     */
    static class XGuiA11yProvider extends AccessibilityNodeProvider {

        private final XGuiA11yHost host;

        XGuiA11yProvider(XGuiA11yHost host) {
            this.host = host;
        }

        @Override
        public AccessibilityNodeInfo createAccessibilityNodeInfo(int virtualViewId) {
            int index = virtualViewId - VIRTUAL_ID_BASE;
            if (index < 0) {
                // 宿主根节点（HOST_VIEW_ID）：承载整棵虚拟树的根。
                AccessibilityNodeInfo root =
                        AccessibilityNodeInfo.obtain(host, host.getId());
                root.setClassName("android.view.View");
                root.setPackageName(host.getContext().getPackageName());
                root.setVisibleToUser(true);
                root.setEnabled(true);
                // 根节点必须给出真实屏幕矩形：默认 obtain() 得到的节点
                // boundsInScreen 是空矩形（0,0,0,0），系统据此判定该节点
                // "不可见"，整棵虚拟子树会被跳过（uiautomator dump 实证：
                // 宿主根曾报 bounds=[0,0][0,0]，子节点虽在却挂空矩形下）。
                applyScreenBounds(root);
                int rootIndex = XGuiA11yBridge.nativeRootIndex();
                if (rootIndex >= 0) {
                    // native 侧 index 0 是**合成根**（代表应用/窗口容器），
                    // 其子节点才是真正的顶层控件——故这里挂"合成根的子节点"，
                    // 合成根本身不进树（它是内部结构，不是界面元素）。
                    attachChildren(root, rootIndex);
                }
                return root;
            }
            String[] fields = XGuiA11yBridge.nativeNodeInfo(index);
            if (fields == null) {
                return null;
            }
            // fields: [className, text, description, flags, x, y, w, h,
            //          parentIndex, firstChild, childCount]
            AccessibilityNodeInfo info = AccessibilityNodeInfo.obtain(
                    host, virtualViewId);
            info.setClassName(fields[0]);
            info.setPackageName(host.getContext().getPackageName());
            if (fields[1] != null && fields[1].length() > 0) {
                info.setText(fields[1]);
            }
            if (fields[2] != null && fields[2].length() > 0) {
                info.setContentDescription(fields[2]);
            }
            int flags = Integer.parseInt(fields[3]);
            int x = Integer.parseInt(fields[4]);
            int y = Integer.parseInt(fields[5]);
            int w = Integer.parseInt(fields[6]);
            int h = Integer.parseInt(fields[7]);
            info.setBoundsInScreen(new Rect(x, y, x + w, y + h));
            info.setVisibleToUser((flags & XGuiA11yBridge.FLAG_VISIBLE) != 0);
            info.setEnabled((flags & XGuiA11yBridge.FLAG_ENABLED) != 0);
            info.setFocusable((flags & XGuiA11yBridge.FLAG_FOCUSABLE) != 0);
            info.setEditable((flags & XGuiA11yBridge.FLAG_EDITABLE) != 0);
            info.setChecked((flags & XGuiA11yBridge.FLAG_CHECKED) != 0);
            info.setClickable((flags & XGuiA11yBridge.FLAG_CLICKABLE) != 0);
            info.setScrollable(false);
            attachChildren(info, index);
            return info;
        }

        /** 挂载虚拟子节点（快照里首子 + 连续子区间）。 */
        private void attachChildren(AccessibilityNodeInfo info, int index) {
            String[] fields = XGuiA11yBridge.nativeNodeInfo(index);
            if (fields == null) {
                return;
            }
            int firstChild = Integer.parseInt(fields[9]);
            int childCount = Integer.parseInt(fields[10]);
            for (int i = 0; i < childCount; i++) {
                int childIndex = firstChild + i;
                info.addChild(host, childIndex + VIRTUAL_ID_BASE);
            }
        }

        /**
         * 给节点填上真实屏幕矩形（宿主根节点用）。
         *
         * 取窗口 decor 的屏幕位置与尺寸：宿主 View 铺满内容区，屏幕矩形取
         * decor 即可覆盖整棵虚拟树。定位不到时退化为整个显示屏。
         */
        private void applyScreenBounds(AccessibilityNodeInfo info) {
            android.view.View decor = host.getRootView();
            int[] location = new int[2];
            if (decor != null) {
                decor.getLocationOnScreen(location);
                int w = decor.getWidth();
                int h = decor.getHeight();
                if (w > 0 && h > 0) {
                    info.setBoundsInScreen(new Rect(location[0], location[1],
                            location[0] + w, location[1] + h));
                    return;
                }
            }
            android.util.DisplayMetrics dm =
                    host.getResources().getDisplayMetrics();
            info.setBoundsInScreen(new Rect(0, 0, dm.widthPixels, dm.heightPixels));
        }

        @Override
        public boolean performAction(int virtualViewId, int action,
                                     android.os.Bundle args) {
            int index = virtualViewId - VIRTUAL_ID_BASE;
            if (index < 0) {
                return false;
            }
            if (action == AccessibilityNodeInfo.ACTION_CLICK) {
                return XGuiA11yBridge.nativePerformClick(index);
            }
            if (action == AccessibilityNodeInfo.ACTION_FOCUS) {
                return XGuiA11yBridge.nativePerformFocus(index);
            }
            return false;
        }

        @Override
        public List<AccessibilityNodeInfo> findAccessibilityNodeInfosByText(
                String text, int virtualViewId) {
            return null;
        }

        @Override
        public AccessibilityNodeInfo findFocus(int focus) {
            return null;
        }
    }
}
