/**
 * @file       XPlatformAccessibility_android.c
 * @brief      Android 辅助功能桥（TalkBack 虚拟视图树）+ JNI 导出。
 * @details    [2026-10-24 安卓无障碍接入] 安卓读屏器（TalkBack）只认 Java
 *             侧 View 树；纯 native 画布（NativeActivity）在系统眼里只是
 *             "一个 View"，读屏器念不出任何控件。本实现用**虚拟视图树**
 *             桥接（对标 Qt Android 插件与 SDL 的做法）：
 *
 *             - 原生侧（本文件）：从 XWidget 控件树构建**扁平快照**（每节点
 *               携带角色/类名/文本/描述/屏幕矩形/标志位/父子下标）。快照在
 *               **渲染线程**重建（控件树只在该线程可安全遍历），Java 侧
 *               只读已发布副本——与触摸通道"UI 线程入队 / 渲染线程消费"
 *               同纪律，杜绝跨线程读控件树。
 *             - Java 侧（XGuiA11yProvider.java）：AccessibilityNodeProvider
 *               按 virtualViewId 取快照节点，组装 AccessibilityNodeInfo
 *               （obtain(host, virtualId) + addChild(host, childId) 挂虚拟
 *               子节点）；ACTION_CLICK 回投原生执行控件点击。
 *
 *             角色映射：按 vtable 身份识别常见控件（XPushButton→Button、
 *             XCheckBox→CheckBox、XLineEdit/XPlainTextEdit→EditText、
 *             XLabel→TextView、XComboBox→Spinner），未识别者按通用 View。
 *             安卓类名用官方 android.widget.* 名称，读屏器据此播报
 *             "按钮/编辑框/文本"。
 *
 *             坐标口径：控件全局逻辑坐标（XWidget_mapToGlobal）→ 安卓需要
 *             **屏幕物理像素**：× dpr + 窗口屏幕原点（与输入坐标同源，见
 *             XPlatformWindowAndroid.c 的 viewSpace 标定分支）。
 * @note       平台 API 只出现在 Drive；本文件仅在 __ANDROID__ 下编译出实体。
 * @author     XinYueC 团队
 ******************************************************************************/
#include "XPlatformAccessibility.h"

#if XWINDOW_ON && XACCESSIBLE_ON && defined(__ANDROID__)

#include "XWindow.h"
#include "XWidget.h"
#include "XApplication.h"
#include "XGuiApplication.h"
#include "XMemory.h"
#include "XString.h"
#include "XVector.h"
#include "XAbstractButton.h"
#include "XAbstractButton_Protected.h" /* XAbstractButton_isInstance（派生类登记）。 */
#include "XPushButton.h"
#include "XCheckBox.h"
#include "XRadioButton.h"
#include "XCommandLinkButton.h"
#include "XLineEdit.h"
#include "XPlainTextEdit.h"
#include "XLabel.h"
#include "XComboBox.h"
#include "XPlatformScreen.h"
#include "XDateTime.h"
#include "XAndroidA11yNodes.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#ifdef __ANDROID__
#include <android/log.h>
#endif

/* =========================================================================
 * 快照结构
 * ========================================================================= */

/** @brief 节点→控件指针映射（与快照同下标；动作回投用）。
 *  @note   指针只在渲染线程有效（控件树线程约束），故动作必须回投渲染
 *          线程执行——见 XAndroid_a11yPostAction。 */
static XWidget** g_xa11yWidgets = NULL;   /**< 与 g_xa11yPublished 同序。 */
static int g_xa11yWidgetCapacity = 0;

/** @brief 快照容器（发布给 Java 的只读副本）。 */
typedef struct XAndroidA11ySnapshot {
    XAndroidA11yNode* nodes; /**< 节点数组（堆）。 */
    int count;               /**< 节点数。 */
    int capacity;            /**< 数组容量。 */
} XAndroidA11ySnapshot;

static pthread_mutex_t g_xa11yMutex = PTHREAD_MUTEX_INITIALIZER;
static XAndroidA11ySnapshot g_xa11yPublished; /**< 已发布副本（Java 读）。 */
static XAndroidA11ySnapshot g_xa11yBuilding;  /**< 渲染线程构建中。 */
static XWidget** g_xa11yPublishedWidgets = NULL; /**< 已发布：节点→控件。 */
static XWidget** g_xa11yBuildingWidgets = NULL;  /**< 构建中：节点→控件。 */
static int g_xa11yPublishedWidgetCapacity = 0;
static int g_xa11yBuildingWidgetCapacity = 0;
static bool g_xa11yDirty = true;              /**< 树变化待重建标志。 */
static int64_t g_xa11yLastBuildMs = 0;       /**< 上次快照重建时刻。 */

/** @brief 快照重建节拍（毫秒）：几何变化不经过 notify，按节拍兜底重建。 */
#define XA11Y_REBUILD_INTERVAL_MS 500

/* 读屏器动作请求（Java 线程投递 → 渲染线程消费）。 */
static int g_xa11yPendingAction = 0;   /**< 0=无；1=点击；2=聚焦。 */
static int g_xa11yPendingIndex = -1;    /**< 目标节点下标。 */

/** @brief 消费待办动作（渲染线程调用；定义在本文件后段）。 */
static void xa11y_consumePendingAction(void);

/** @brief 重建并发布快照（渲染线程调用；由安卓平台层事件泵驱动）。 */
void xa11y_pumpSnapshot(void);

/* =========================================================================
 * 构建器（渲染线程）
 * ========================================================================= */

/** @brief 构建上下文。 */
typedef struct XAndroidA11yBuilder {
    XAndroidA11ySnapshot* snapshot;
    XWidget** widgets;     /**< 节点→控件指针（与 snapshot 同序）。 */
    int widgetCapacity;    /**< widgets 容量。 */
    float dpr;              /**< 设备像素比（逻辑→物理）。 */
    int originX, originY;   /**< 窗口屏幕原点（物理像素）。 */
} XAndroidA11yBuilder;

/** @brief 追加一个节点（返回下标；容量不足返回 -1）。 */
static int xa11y_appendNode(XAndroidA11ySnapshot* snapshot,
                             const XAndroidA11yNode* node)
{
    if (snapshot->count >= snapshot->capacity) {
        int newCapacity = snapshot->capacity ? snapshot->capacity * 2 : 64;
        XAndroidA11yNode* grown = (XAndroidA11yNode*)realloc(
            snapshot->nodes, (size_t)newCapacity * sizeof(XAndroidA11yNode));
        if (!grown) return -1;
        snapshot->nodes = grown;
        snapshot->capacity = newCapacity;
    }
    snapshot->nodes[snapshot->count] = *node;
    return snapshot->count++;
}

/** @brief 控件 → 安卓类名（角色判定；未知者按通用 View）。
 *  @details 按钮族用 **XAbstractButton_isInstance**（走框架的派生类登记
 *           表）而非 vtable 身份相等——后者只认 XPushButton/XCheckBox 本身，
 *           派生类（XCommandLinkButton / XToolButton / XRadioButton …）
 *           全部落空 → 真机实证：命令链接按钮与工具按钮在 dump 里退化成
 *           无文本的 android.view.View（读屏器念不出"命令链接按钮"）。
 */
static const char* xa11y_classNameOf(XWidget* widget)
{
    if (!widget) return "android.view.View";
    /* 复选框/单选先于普通按钮判定（它们的 vtable 也登记在按钮族里）。 */
    if (XClassGetVtable(widget) == XCheckBox_class_init())
        return "android.widget.CheckBox";
    if (XClassGetVtable(widget) == XRadioButton_class_init())
        return "android.widget.RadioButton";
    if (XAbstractButton_isInstance((const XObject*)widget))
        return "android.widget.Button";
    if (XClassGetVtable(widget) == XLineEdit_class_init())
        return "android.widget.EditText";
    if (XClassGetVtable(widget) == XPlainTextEdit_class_init())
        return "android.widget.EditText";
    if (XClassGetVtable(widget) == XLabel_class_init())
        return "android.widget.TextView";
    if (XClassGetVtable(widget) == XComboBox_class_init())
        return "android.widget.Spinner";
    return "android.view.View";
}

/** @brief UTF-8 安全拷贝：截断回退字符边界，并弃掉 JNI 不收的四字节字。
 *  @details [2026-10-28 根修] 原实现全是裸 strncpy：节点 text/description
 *           缓冲只有 160 字节，而中文一行 64 个全角字约 192 字节，必然
 *           在某个多字节字符中间截断。JNI 的 NewStringUTF 对非法 Modified
 *           UTF-8 直接 Abort（真机实证：SIGABRT / "input is not valid
 *           Modified UTF-8: illegal continuation byte 0"，读屏器或
 *           uiautomator 一查询即崩）。
 *           Modified UTF-8 只认 1~3 字节序列（增补平面须拆代理对，本层不
 *           做拆对），故 4 字节序列（emoji 等）整字丢弃——宁可少一个字，
 *           不能让读屏器把宿主进程 abort 掉。 */
static void xa11y_copyUtf8(char* out, const char* src, size_t cap)
{
    size_t i = 0;
    if (!out || cap == 0) return;
    out[0] = '\0';
    if (!src) return;
    while (src[i] != '\0' && i + 1 < cap) {
        unsigned char c = (unsigned char)src[i];
        size_t len;
        if ((c & 0x80u) == 0x00u) len = 1;
        else if ((c & 0xE0u) == 0xC0u) len = 2;
        else if ((c & 0xF0u) == 0xE0u) len = 3;
        else if ((c & 0xF8u) == 0xF0u) len = 4;
        else len = 1;   /* 孤立续字节：源数据本身有问题，按 1 字节放过。 */
        if (len == 4) {          /* Modified UTF-8 无 4 字节形态：整字弃。 */
            i += 4;
            continue;
        }
        if (i + len + 1 > cap) break;   /* 整字放不下：舍弃，保 NUL 位。 */
        memcpy(out + i, src + i, len);
        i += len;
    }
    out[i] = '\0';
}

/** @brief 取控件文本（按类型分派；空串=无文本）。 */
static void xa11y_readText(XWidget* widget, const char* className,
                            char* out, size_t cap)
{
    out[0] = '\0';
    if (!widget || cap == 0) return;
    if (strcmp(className, "android.widget.EditText") == 0) {
        if (XClassGetVtable(widget) == XLineEdit_class_init()) {
            const char* text = XLineEdit_text((XLineEdit*)widget);
            if (text) {
                xa11y_copyUtf8(out, text, cap);
            }
        }
        return;
    }
    if (strcmp(className, "android.widget.TextView") == 0) {
        const XString* text = XLabel_text((XLabel*)widget);
        if (text) {
            const char* utf8 = XString_toUtf8(text);
            if (utf8) {
                xa11y_copyUtf8(out, utf8, cap);
            }
        }
        return;
    }
    /* 按钮族（Button/CheckBox/RadioButton）：文本在 XAbstractButton 上
     * （按钮标题），不能走 accessibleName 兜底——无障碍名是"可访问名"，
     * 与按钮标题是两个概念，探针实测按钮 text 曾恒为空。
     * 派生类（XCommandLinkButton/XToolButton …）的标题同样存放在
     * XAbstractButton 基类字段上，故按类名分派即可覆盖。 */
    if (strcmp(className, "android.widget.Button") == 0 ||
        strcmp(className, "android.widget.CheckBox") == 0 ||
        strcmp(className, "android.widget.RadioButton") == 0) {
        const XString* text = XAbstractButton_text((const XAbstractButton*)widget);
        if (text) {
            const char* utf8 = XString_toUtf8(text);
            if (utf8) {
                xa11y_copyUtf8(out, utf8, cap);
            }
        }
        return;
    }
    /* 其它类型：用无障碍名称（可访问名）兜底。 */
    {
        const XString* name = XWidget_accessibleName(widget);
        if (name) {
            const char* utf8 = XString_toUtf8(name);
            if (utf8 && utf8[0]) {
                xa11y_copyUtf8(out, utf8, cap);
            }
        }
    }
}

/** @brief 取控件描述（无障碍描述）。 */
static void xa11y_readDescription(XWidget* widget, char* out, size_t cap)
{
    out[0] = '\0';
    if (!widget || cap == 0) return;
    {
        const XString* desc = XWidget_accessibleDescription(widget);
        if (desc) {
            const char* utf8 = XString_toUtf8(desc);
            if (utf8 && utf8[0]) {
                xa11y_copyUtf8(out, utf8, cap);
                return;
            }
        }
    }
    /* 命令链接按钮：自带 description 字段（QCommandLinkButton::description，
     * 双行按钮的第二行），读屏器应把它作为 content-desc 念出来。 */
    if (XClassGetVtable(widget) == XCommandLinkButton_class_init()) {
        const XString* desc =
            XCommandLinkButton_description((XCommandLinkButton*)widget);
        if (desc) {
            const char* utf8 = XString_toUtf8(desc);
            if (utf8 && utf8[0]) {
                xa11y_copyUtf8(out, utf8, cap);
            }
        }
    }
}

/** @brief 为一个控件发射节点（不递归；子节点由 BFS 队列后续处理）。
 *  @return 本节点下标；失败返回 -1。
 */
static int xa11y_emitWidget(XAndroidA11yBuilder* builder, XWidget* widget,
                             int parentIndex)
{
    XAndroidA11yNode node;
    int index;
    XPoint global;

    if (!widget || !builder || !builder->snapshot) return -1;
    if (!XWidget_isVisible(widget)) return -1;

    memset(&node, 0, sizeof(node));
    node.parent = parentIndex;
    node.firstChild = -1;
    node.childCount = 0;
    node.flags = XA11Y_FLAG_VISIBLE;
    if (XWidget_isEnabled(widget)) node.flags |= XA11Y_FLAG_ENABLED;
    if (XWidget_focusPolicy(widget) != XWidgetFocusPolicy_NoFocus)
        node.flags |= XA11Y_FLAG_FOCUSABLE;

    strncpy(node.className, xa11y_classNameOf(widget),
            sizeof(node.className) - 1u);
    xa11y_readText(widget, node.className, node.text, sizeof(node.text));
    xa11y_readDescription(widget, node.description,
                           sizeof(node.description));

    if (strcmp(node.className, "android.widget.EditText") == 0)
        node.flags |= XA11Y_FLAG_EDITABLE;
    if (strcmp(node.className, "android.widget.Button") == 0 ||
        strcmp(node.className, "android.widget.CheckBox") == 0)
        node.flags |= XA11Y_FLAG_CLICKABLE;
    if (strcmp(node.className, "android.widget.CheckBox") == 0) {
        if (XAbstractButton_isChecked((XAbstractButton*)widget))
            node.flags |= XA11Y_FLAG_CHECKED;
    }

    /* 几何：控件本地矩形（父坐标系）→ 全局逻辑坐标 → 屏幕物理像素。
     *
     * mapToGlobal 的入参是"控件自身坐标系里的点"，且 accumulateOffset 已
     * 沿父链累加**控件自身**的 m_windowRect.x/y——故原点必须传 {0,0}，
     * 直接传 geometry 的 rect.x/y 会把控件自身偏移重复计一次（真机实证：
     * dump 报 "换边" 在 [288,177]，截图实际在 x≈176，整段左偏 112px）。
     * 与框架 XAccessible_rect 的取法一致（mapToGlobal(widget, {0,0})）。
     */
    {
        XRect rect = XWidget_geometry(widget);
        XPoint local;
        local.x = 0;
        local.y = 0;
        global = XWidget_mapToGlobal(widget, &local);
        node.x = (int)((float)global.x * builder->dpr) + builder->originX;
        node.y = (int)((float)global.y * builder->dpr) + builder->originY;
        node.w = (int)((float)rect.width * builder->dpr);
        node.h = (int)((float)rect.height * builder->dpr);
    }

    index = xa11y_appendNode(builder->snapshot, &node);
    if (index < 0) return -1;
    /* 记录节点→控件指针（动作回投用；同序增长）。 */
    if (index >= builder->widgetCapacity) {
        int newCap = builder->widgetCapacity ? builder->widgetCapacity * 2 : 64;
        XWidget** grown = (XWidget**)realloc(builder->widgets,
                                             (size_t)newCap * sizeof(XWidget*));
        if (!grown) return index;
        builder->widgets = grown;
        builder->widgetCapacity = newCap;
    }
    builder->widgets[index] = widget;
    return index;
}

/** @brief 收集顶层控件（快照遍历根）。
 *  @details 入口用 **XWidget_widgetForWindow(window)**（窗口反查顶层控件）
 *           逐窗口取，而不是 XApplication_topLevelWidgets()——后者依赖
 *           XApplication 单例（g_xapp），而 demo/应用走的是 XGuiApplication
 *           路径，XApplication 单例不存在 → 注册表恒空 → 快照恒空节点
 *           （真机实证：probe 打印 topLevelWidgets=0x0 / nodes=0）。
 *           XGuiApplication_topLevelWindows() 是应用级窗口表，任何 GUI
 *           应用都成立。 */
static XVector* xa11y_collectTopWidgets(void)
{
    XVector* windows = XGuiApplication_topLevelWindows();
    XVector* tops;
    size_t i;
    size_t n;
    if (!windows) return NULL;
    tops = XVector_Create(XWidget*);
    if (!tops) {
        XClassDelete((XClass*)windows);
        return NULL;
    }
    n = XVector_size_base((const XContainer*)windows);
    for (i = 0; i < n; ++i) {
        XWindow* win = XVector_At_Base(windows, (int64_t)i, XWindow*);
        XWidget* top = win ? XWidget_widgetForWindow(win) : NULL;
        if (top) XVector_Push_Back_Base(tops, XWidget*, top);
    }
    XClassDelete((XClass*)windows);
    return tops;
}

/** @brief 构建整棵快照（渲染线程调用）——见 xa11y_buildSnapshotBfs 实现。
 *  @param widgets **节点→控件指针数组的指针**（进出参）：数组在扩容时会
 *         realloc 换地址，必须把新地址回写给调用方——否则调用方发布出去的
 *         是扩容前的旧指针（首轮扩容即 realloc(NULL,…) → 旧值 NULL），
 *         动作回投查表恒空 → 读屏器点按钮落不到控件（真机探针实证：
 *         baseline 直接 click 槽被调 1 次，a11y 动作回投 0 次）。
 */
static void xa11y_buildSnapshotBfs(XAndroidA11ySnapshot* snapshot,
                                   XWidget*** widgets, int* widgetCapacity,
                                   float dpr, int originX, int originY);

/** @brief 队列项（BFS 遍历用）。 */
typedef struct XAndroidA11yQueueItem {
    XWidget* widget;   /**< 待处理控件；NULL = 合成根节点。 */
    int parent;        /**< 父节点下标。 */
} XAndroidA11yQueueItem;

/** @brief 广度优先构建快照（渲染线程调用）。
 *  @details **为什么必须 BFS 而不是递归 DFS**：Java 侧按 (firstChild,
 *           childCount) 取子节点，隐含"某节点的子节点在扁平数组里**连续**
 *           排列"的约定。递归前序 DFS 下，孙节点会插在两个子节点之间
 *           （父→子1→孙1→子2），firstChild+i 取到的是孙节点——真机实证：
 *           uiautomator dump 出现大量重复子树、深度 11、控件被挂到错误
 *           父节点下（"导航"/"换边"各出现 5~6 次）。BFS 的出队序即发射序，
 *           同一父节点的子节点在队列中相邻入队、出队也相邻，故其下标必连续，
 *           约定成立。
 *
 *           节点 0 是**合成根**（代表应用/窗口容器，非真实控件）：多窗口
 *           场景下顶层控件不止一个，Java 侧需要唯一根来挂载整棵树。
 */
static void xa11y_buildSnapshotBfs(XAndroidA11ySnapshot* snapshot,
                                   XWidget*** widgets, int* widgetCapacity,
                                   float dpr, int originX, int originY)
{
    XAndroidA11yBuilder builder;
    XVector* topWidgets;
    XAndroidA11yQueueItem* queue = NULL;
    int queueCount = 0;
    int queueCapacity = 0;
    int head = 0;
    int rootIndex = -1;
    size_t i;

    snapshot->count = 0;
    builder.snapshot = snapshot;
    builder.widgets = *widgets;
    builder.widgetCapacity = *widgetCapacity;
    builder.dpr = (dpr > 0.0f) ? dpr : 1.0f;
    builder.originX = originX;
    builder.originY = originY;

    /* ---- 合成根节点（index 0）---- */
    {
        XAndroidA11yNode root;
        memset(&root, 0, sizeof(root));
        root.parent = -1;
        root.firstChild = -1;
        root.childCount = 0;
        root.flags = XA11Y_FLAG_VISIBLE | XA11Y_FLAG_ENABLED;
        strncpy(root.className, "android.view.View",
                sizeof(root.className) - 1u);
        rootIndex = xa11y_appendNode(snapshot, &root);
        if (rootIndex < 0) {
            *widgetCapacity = builder.widgetCapacity;
            *widgets = builder.widgets;
            return;
        }
        /* 根节点无对应控件：动作回投查表时按 NULL 跳过。 */
        if (rootIndex >= builder.widgetCapacity) {
            int newCap = builder.widgetCapacity ? builder.widgetCapacity * 2
                                                : 64;
            XWidget** grown = (XWidget**)realloc(
                builder.widgets, (size_t)newCap * sizeof(XWidget*));
            if (grown) {
                builder.widgets = grown;
                builder.widgetCapacity = newCap;
            }
        }
        if (rootIndex < builder.widgetCapacity)
            builder.widgets[rootIndex] = NULL;
    }

    topWidgets = xa11y_collectTopWidgets();
    if (!topWidgets) {
        *widgetCapacity = builder.widgetCapacity;
        *widgets = builder.widgets;
        return;
    }

    /* 顶层控件入队（父下标 = 合成根）。 */
    for (i = 0; i < XVector_size_base((const XContainer*)topWidgets); ++i) {
        XWidget* widget = XVector_At_Base(topWidgets, (int64_t)i, XWidget*);
        if (!widget) continue;
        if (queueCount >= queueCapacity) {
            int newCap = queueCapacity ? queueCapacity * 2 : 64;
            XAndroidA11yQueueItem* grown =
                (XAndroidA11yQueueItem*)realloc(
                    queue, (size_t)newCap * sizeof(XAndroidA11yQueueItem));
            if (!grown) break;
            queue = grown;
            queueCapacity = newCap;
        }
        queue[queueCount].widget = widget;
        queue[queueCount].parent = rootIndex;
        ++queueCount;
    }
    XClassDelete((XClass*)topWidgets);

    /* ---- BFS 主循环 ---- */
    while (head < queueCount) {
        XWidget* widget = queue[head].widget;
        int parentIndex = queue[head].parent;
        int index;
        const XVector* children;
        size_t total;

        ++head;
        index = xa11y_emitWidget(&builder, widget, parentIndex);
        if (index < 0) continue; /* 不可见控件：连同子树一起剪掉。 */

        children = XObject_children((const XObject*)widget);
        if (!children) continue;
        total = XVector_size_base((const XContainer*)children);
        for (i = 0; i < total; ++i) {
            XObject* child = XVector_At_Base(children, (int64_t)i, XObject*);
            if (!child || !XObject_isWidgetType(child)) continue;
            if (queueCount >= queueCapacity) {
                int newCap = queueCapacity ? queueCapacity * 2 : 64;
                XAndroidA11yQueueItem* grown =
                    (XAndroidA11yQueueItem*)realloc(
                        queue, (size_t)newCap * sizeof(XAndroidA11yQueueItem));
                if (!grown) break;
                queue = grown;
                queueCapacity = newCap;
            }
            queue[queueCount].widget = (XWidget*)child;
            queue[queueCount].parent = index;
            ++queueCount;
        }
    }

    /* ---- 子节点连续性回填 ----
     * BFS 下同一父节点的子节点下标连续；此处按 parent 统计首子下标与个数，
     * 供 Java 侧按 (firstChild, childCount) 取子节点。 */
    {
        int n = snapshot->count;
        int* first = (int*)calloc((size_t)(n > 0 ? n : 1), sizeof(int));
        int* count = (int*)calloc((size_t)(n > 0 ? n : 1), sizeof(int));
        if (first && count) {
            for (i = 0; i < (size_t)n; ++i) {
                int p = snapshot->nodes[i].parent;
                if (p < 0 || p >= n) continue;
                if (count[p] == 0) first[p] = (int)i;
                ++count[p];
            }
            for (i = 0; i < (size_t)n; ++i) {
                int p = (int)i;
                snapshot->nodes[p].firstChild =
                    count[p] > 0 ? first[p] : -1;
                snapshot->nodes[p].childCount = count[p];
            }
        }
        if (first) free(first);
        if (count) free(count);
    }

    if (queue) free(queue);
    *widgetCapacity = builder.widgetCapacity;
    *widgets = builder.widgets;
}

/* =========================================================================
 * 平台驱动契约（对标 win32/posix 的 Driver_* 族）
 * ========================================================================= */

typedef struct XPlatformAccessibilityAndroid {
    bool active;
} XPlatformAccessibilityAndroid;

bool XPlatformAccessibilityDriver_start(XPlatformAccessibility* bridge,
                                        void** nativeState)
{
    XPlatformAccessibilityAndroid* state;
    (void)bridge;
    if (nativeState) *nativeState = NULL;
    if (!nativeState) return false;
    state = (XPlatformAccessibilityAndroid*)XMalloc_System(sizeof(*state));
    if (!state) return false;
    memset(state, 0, sizeof(*state));
    state->active = true;
    *nativeState = state;
    return true;
}

void XPlatformAccessibilityDriver_stop(void* nativeState)
{
    XPlatformAccessibilityAndroid* state =
        (XPlatformAccessibilityAndroid*)nativeState;
    if (!state) return;
    XFree_System(state);
    /* 释放已发布快照（避免进程退出前残留堆块）。 */
    pthread_mutex_lock(&g_xa11yMutex);
    if (g_xa11yPublished.nodes) free(g_xa11yPublished.nodes);
    memset(&g_xa11yPublished, 0, sizeof(g_xa11yPublished));
    if (g_xa11yPublishedWidgets) free(g_xa11yPublishedWidgets);
    g_xa11yPublishedWidgets = NULL;
    g_xa11yPublishedWidgetCapacity = 0;
    if (g_xa11yBuilding.nodes) free(g_xa11yBuilding.nodes);
    memset(&g_xa11yBuilding, 0, sizeof(g_xa11yBuilding));
    if (g_xa11yBuildingWidgets) free(g_xa11yBuildingWidgets);
    g_xa11yBuildingWidgets = NULL;
    g_xa11yBuildingWidgetCapacity = 0;
    pthread_mutex_unlock(&g_xa11yMutex);
}

bool XPlatformAccessibilityDriver_isActive(void* nativeState)
{
    XPlatformAccessibilityAndroid* state =
        (XPlatformAccessibilityAndroid*)nativeState;
    return state && state->active;
}

void XPlatformAccessibilityDriver_notify(void* nativeState,
                                         XAccessibleEvent event,
                                         XAccessible* accessible)
{
    /* 事件只置"树已变"标志；真正重建在渲染线程的 processEvents 里
       （控件树只在该线程可安全遍历）。 */
    (void)nativeState;
    (void)event;
    (void)accessible;
    pthread_mutex_lock(&g_xa11yMutex);
    g_xa11yDirty = true;
    pthread_mutex_unlock(&g_xa11yMutex);
}

void XPlatformAccessibilityDriver_processEvents(void* nativeState)
{
    /* 渲染线程（事件泵所在线程）调用：在此重建快照并发布。
       跨线程纪律：控件树只在本线程遍历，Java 侧只读已发布副本。
       [2026-10-24] 正常路径下由安卓平台层事件泵直接调用
       xa11y_pumpSnapshot()（见 XPlatformWindowAndroid.c）；此处保留
       作为 Driver 契约的兜底入口（若平台层未接，帧泵仍会走到）。 */
    XPlatformAccessibilityAndroid* state =
        (XPlatformAccessibilityAndroid*)nativeState;
    if (!state || !state->active) return;
    xa11y_pumpSnapshot();
}

/** @brief 重建并发布快照（渲染线程调用；平台层事件泵驱动）。
 *  @details 由安卓平台层的每帧事件泵调用（XPlatformWindowAndroid.c 的
 *           XPlatformNativeWindow_processPendingEvents）——demo 走的是
 *           XCoreApplication_exec 循环，**不经过 XGuiApplication_processEvents**，
 *           故无障碍快照不能挂在应用层泵上（否则永不重建，节点树为空）。
 *
 *           重建节流：控件树的**内容**变化由 notify 置脏（创建/销毁/改名/
 *           状态），但**几何**变化（布局/动画/滚动）不经过 notify——故不能
 *           只看脏标记，否则读屏器拿到的坐标会停在旧值。按固定节拍
 *           （XA11Y_REBUILD_INTERVAL_MS）重建：控件树规模有限（百级节点），
 *           成本可忽略；节拍同时也兜底覆盖 notify 漏报的场景。 */
void xa11y_pumpSnapshot(void)
{
    XPlatformScreenDpiInfo dpiInfo;
    float dpr = 1.0f;
    int originX = 0;
    int originY = 0;
    int64_t now;

    /* 消费读屏器动作（Java 线程投递 → 本线程执行；控件树线程约束）。 */
    xa11y_consumePendingAction();

    now = XDateTime_currentMSecsSinceEpoch();
    if (g_xa11yLastBuildMs > 0 &&
        now - g_xa11yLastBuildMs < XA11Y_REBUILD_INTERVAL_MS) {
        return;
    }
    g_xa11yLastBuildMs = now;

    if (XPlatformScreen_queryDpi(&dpiInfo))
        dpr = dpiInfo.m_density > 0.0f ? dpiInfo.m_density : 1.0f;
    XPlatformScreen_queryOrigin(&originX, &originY);

    /* 构建到 building（渲染线程私有），完成后整体发布。
       widgets 数组进出参：扩容后地址会变，必须回写 g_xa11yBuildingWidgets。 */
    xa11y_buildSnapshotBfs(&g_xa11yBuilding, &g_xa11yBuildingWidgets,
                        &g_xa11yBuildingWidgetCapacity, dpr, originX, originY);
#ifdef __ANDROID__
    /* 诊断（一次性）：节点数与首节点角色，logcat tag=XPad。 */
    {
        static int s_logged = 0;
        if (!s_logged && g_xa11yBuilding.count > 0) {
            s_logged = 1;
            __android_log_print(ANDROID_LOG_INFO, "XPad",
                                "a11y snapshot: nodes=%d first=%s dpr=%.2f",
                                g_xa11yBuilding.count,
                                g_xa11yBuilding.nodes[0].className,
                                (double)dpr);
        }
    }
#endif
    pthread_mutex_lock(&g_xa11yMutex);
    if (g_xa11yPublished.nodes) free(g_xa11yPublished.nodes);
    if (g_xa11yPublishedWidgets) free(g_xa11yPublishedWidgets);
    g_xa11yPublished = g_xa11yBuilding;
    g_xa11yPublishedWidgets = g_xa11yBuildingWidgets;
    g_xa11yPublishedWidgetCapacity = g_xa11yBuildingWidgetCapacity;
    memset(&g_xa11yBuilding, 0, sizeof(g_xa11yBuilding));
    g_xa11yBuildingWidgets = NULL;
    g_xa11yBuildingWidgetCapacity = 0;
    pthread_mutex_unlock(&g_xa11yMutex);
}

/* =========================================================================
 * Java 侧查询接口（由 JNI 层调用）
 * ========================================================================= */

/** @brief 取已发布快照的节点数（Java 侧入口）。 */
int XAndroid_a11yNodeCount(void)
{
    int count;
    pthread_mutex_lock(&g_xa11yMutex);
    count = g_xa11yPublished.count;
    pthread_mutex_unlock(&g_xa11yMutex);
    return count;
}

/** @brief 取节点快照（Java 侧按字段逐项取；返回 false=下标越界）。 */
bool XAndroid_a11yNodeInfo(int index, XAndroidA11yNode* out)
{
    bool ok = false;
    if (!out) return false;
    pthread_mutex_lock(&g_xa11yMutex);
    if (index >= 0 && index < g_xa11yPublished.count) {
        *out = g_xa11yPublished.nodes[index];
        ok = true;
    }
    pthread_mutex_unlock(&g_xa11yMutex);
    return ok;
}

/** @brief 取虚拟树根节点下标（顶层控件首节点；无则 -1）。
 *  @details 快照按顶层控件顺序构建，第一个顶层控件即根（其父为 -1）。 */
int XAndroid_a11yRootIndex(void)
{
    int root = -1;
    pthread_mutex_lock(&g_xa11yMutex);
    if (g_xa11yPublished.count > 0) {
        int i;
        for (i = 0; i < g_xa11yPublished.count; ++i) {
            if (g_xa11yPublished.nodes[i].parent < 0) {
                root = i;
                break;
            }
        }
    }
    pthread_mutex_unlock(&g_xa11yMutex);
    return root;
}

/** @brief 投递读屏器动作（Java 线程调用；渲染线程消费）。
 *  @details 控件树只在渲染线程可安全操作，故此处只置请求槽。 */
bool XAndroid_a11yPostAction(int index, int action)
{
    bool ok;
    pthread_mutex_lock(&g_xa11yMutex);
    ok = (index >= 0 && index < g_xa11yPublished.count);
    if (ok) {
        g_xa11yPendingAction = action;
        g_xa11yPendingIndex = index;
    }
    pthread_mutex_unlock(&g_xa11yMutex);
    return ok;
}

/** @brief 消费待办动作（渲染线程调用）。 */
static void xa11y_consumePendingAction(void)
{
    int action;
    int index;
    XWidget* widget = NULL;
    pthread_mutex_lock(&g_xa11yMutex);
    action = g_xa11yPendingAction;
    index = g_xa11yPendingIndex;
    g_xa11yPendingAction = 0;
    g_xa11yPendingIndex = -1;
    if (action != 0 && index >= 0 && index < g_xa11yPublished.count &&
        g_xa11yPublishedWidgets)
        widget = g_xa11yPublishedWidgets[index];
    pthread_mutex_unlock(&g_xa11yMutex);
    if (!widget) return;

    if (action == 1) {
        /* 点击：按钮走 XAbstractButton_click（触发 clicked 信号，与真点击
           同路径）；其它控件退化为聚焦。判定用 isInstance（覆盖
           XCommandLinkButton/XToolButton 等派生类），不只认 vtable 相等。 */
        if (XAbstractButton_isInstance((const XObject*)widget))
            XAbstractButton_click((XAbstractButton*)widget);
        else
            XWidget_setFocus(widget);
    } else if (action == 2) {
        XWidget_setFocus(widget);
    }
}

#endif /* XWINDOW_ON && XACCESSIBLE_ON && __ANDROID__ */
