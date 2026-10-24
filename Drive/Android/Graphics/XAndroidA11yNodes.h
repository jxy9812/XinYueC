/**
 * @file       XAndroidA11yNodes.h
 * @brief      安卓无障碍桥的共享数据布局（快照节点 + 标志位）。
 * @details    [2026-10-24 安卓无障碍接入] 快照构建器
 *             （XPlatformAccessibility_android.c）与 JNI 层
 *             （XPlatformAccessibilityJNI_android.c）共享同一节点布局——
 *             两处各持一份声明会随字段演进失配，故抽此头单一来源。
 *             布局仅含定长字段（无指针），可跨线程按值拷贝。
 * @author     XinYueC 团队
 ******************************************************************************/
#ifndef XANDROIDA11YNODES_H
#define XANDROIDA11YNODES_H

/** 节点标志位。 */
#define XA11Y_FLAG_FOCUSABLE  0x0001
#define XA11Y_FLAG_CLICKABLE  0x0002
#define XA11Y_FLAG_ENABLED    0x0004
#define XA11Y_FLAG_VISIBLE    0x0008
#define XA11Y_FLAG_EDITABLE   0x0010
#define XA11Y_FLAG_CHECKED    0x0020

/** 读屏器动作（Java 投递 → 渲染线程消费）。 */
#define XA11Y_ACTION_CLICK     1
#define XA11Y_ACTION_FOCUS     2

/** @brief 单节点快照（扁平数组；Java 侧按下标索引）。 */
typedef struct XAndroidA11yNode {
    int parent;            /**< 父节点下标；根为 -1。 */
    int firstChild;        /**< 首个子节点下标；无子为 -1。 */
    int childCount;        /**< 子节点数。 */
    int flags;             /**< XA11Y_FLAG_* 位集。 */
    int x, y, w, h;        /**< 屏幕物理像素矩形。 */
    char className[48];    /**< 安卓类名（如 android.widget.Button）。 */
    char text[160];        /**< 文本内容（UTF-8）。 */
    char description[160]; /**< 描述（UTF-8）。 */
} XAndroidA11yNode;

#endif /* XANDROIDA11YNODES_H */
