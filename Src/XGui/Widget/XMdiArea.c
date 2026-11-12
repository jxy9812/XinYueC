/**
 * @file       XMdiArea.c
 * @brief      多文档接口区域及子窗口控件实现（对标 Qt 6.8 QMdiArea/QMdiSubWindow 全部公共 API）。
 * @details    与同名头文件的公共 API 一一对应；内部实现细节见
 *             头文件 @note 与函数级 Doxygen 注释。
 * @author     XinYueC 团队
 */

#if XGUI_ON

#include "XMdiArea.h"
#include "XMemory.h"
#include "XEvent.h"
#include "XVarList.h"
#include "XPainter.h"
#include "XGuiConfig.h"

#include "XAlgorithm.h"
#include "XStringUtils.h"
#include "XWidget_Protected.h"
#if XMENU_ON
#include "XMenu.h"
#endif
#if XABSTRACTSCROLLAREA_ON
#include "XAbstractScrollArea.h"
#endif
#if XTABBAR_ON
#include "XTabBar.h"          /* Tabbed 视图页签条（P3-53）。 */
#endif
#if XGUIAPPLICATION_ON
#include "XGuiApplication.h"  /* sizeHint 主屏（P3-51，对标
                               * QGuiApplication::primaryScreen）。 */
#endif
#if XSCREEN_ON
#include "XScreen.h"          /* virtualSize（P3-51）。 */
#endif
#include "XStyle.h"           /* PM 度量（minimumSizeHint，P3-52）。 */
#include "XStyleOption.h"     /* XStylePM_MdiSubWindowMinimizedWidth /
                               * XStylePM_TitleBarHeight。 */

#if XWIDGET_ON && XABSTRACTSCROLLAREA_ON && XMDIAREA_ON

/* ==================== 动态类型判定（子窗口识别） ==================== */

/** @brief 子窗口激活信号发射体（定义见下方 XMdiArea 段；此处前置声明
 *         便于活动窗收口助手复用）。 */
static void xmdi_emitActivated(XMdiArea* self, XMdiSubWindow* window);
/** @brief 子窗口状态变化信号发射体（定义见下方 XMdiArea 段；折叠/
 *         最小化联动助手在定义点之前使用）。 */
static void xmdi_emitStateChanged(XMdiSubWindow* sw, int oldState,
                                  int newState);
/** @brief Tabbed 视图页签条装配与布局（P3-53；定义在文件后段）。
 *         resizeEvent/setActiveSubWindow/setViewMode 等早于此定义的
 *         消费点经此前置声明调用。 */
static void xmdi_tabBarSync(XMdiArea* self);
/** @brief 图标网格排布（B9；定义在文件后段，对标
 *         QMdiAreaPrivate::arrangeMinimizedSubWindows）。子窗折叠/最小化/
 *         还原联动在定义点之前使用。 */
void XMdiArea_arrangeIcons(XMdiArea* self);
/** @brief 最小化窗的图标尺寸（B9；定义在图标排布段，对标
 *         QMdiSubWindowPrivate::iconSize）。enterMinimized 在定义点之前用。 */
static void xmdi_iconSize(const XMdiSubWindow* sw, XSize* out);

/** @brief Tabbed→SubWindow 退出几何快照条目（实现文件内部；字段
 *         m_tabbedRestore 的元素类型，P3-53）。 */
typedef struct XmdiTabRestore
{
    XMdiSubWindow* m_sw;   /**< 快照所属子窗口（借用）。 */
    XRect m_geom;          /**< 进 Tabbed 前的几何（视口局部坐标）。 */
} XmdiTabRestore;

/**
 * @brief      判断控件是否为 XMdiSubWindow（或其派生类）实例。
 * @details    XMdiArea::addSubWindow / removeSubWindow 需要区分「传入的是
 *             子窗口本身」还是「子窗口的内容件」——对标 Qt 的
 *             `qobject_cast<QMdiSubWindow*>(widget)` 分支
 *             （qmdiarea.cpp:1949-1957 / 1984-1999）。C 侧无 RTTI，识别
 *             机制=虚表槽位指纹：比对 EXWidget_PaintEvent 与 EXClass_Deinit
 *             两槽与 XMdiSubWindow_class_init 注册的静态实现——任何经
 *             XVTABLE_INHERIT_XCLASS(XMdiSubWindow) 派生的类都原样继承
 *             这两个槽，未继承本类虚表的控件不可能持有同槽函数指针。
 *             全程经 XVtable_at 越界安全读取，异类控件恒返回 false。
 * @param      candidate 待判定控件借用指针；可为 NULL。
 * @return     candidate 为 XMdiSubWindow 或其派生实例返回 true。
 */
static bool xmdi_isSubWindow(const XWidget* candidate)
{
    XVtable* vt;
    XVtable* self_vt;
    if (!candidate) return false;
    /* XWidget 嵌 XObject、XObject 嵌 XClass（m_class 恒为第一成员），
     * 任意控件对象首字节即 XClass 头，读虚表指针无越界。 */
    vt = ((const XClass*)candidate)->m_vtable;
    if (!vt) return false;
    self_vt = XMdiSubWindow_class_init();
    return XVtable_at(vt, (size_t)EXWidget_PaintEvent) ==
               XVtable_at(self_vt, (size_t)EXWidget_PaintEvent) &&
           XVtable_at(vt, (size_t)EXClass_Deinit) ==
               XVtable_at(self_vt, (size_t)EXClass_Deinit);
}

/**
 * @brief      在子窗口数组中的下标。
 * @param      self 目标区域；可为 NULL。
 * @param      window 子窗口借用指针；可为 NULL。
 * @return     找到返回下标；未登记/参数无效返回 -1。
 */
static int64_t xmdi_subWindowIndex(const XMdiArea* self,
                                   const XMdiSubWindow* window)
{
    int64_t i;
    int64_t n;
    if (!self || !self->m_subWindows || !window) return -1;
    n = XVector_size_base((const XContainer*)self->m_subWindows);
    for (i = 0; i < n; ++i) {
        XMdiSubWindow** slot =
            (XMdiSubWindow**)XVector_at_base(self->m_subWindows, i);
        if (slot && *slot == window) return i;
    }
    return -1;
}

/**
 * @brief      激活历史账本：登记新子窗口到最前。
 * @details    对标 QMdiAreaPrivate::appendChild 的
 *             `indicesToActivatedChildren.prepend(childWindows.size()-1)`
 *             （qmdiarea.cpp:812）。本实现按指针记录（Qt 按 childWindows
 *             下标记录），二者语义等价且免去删窗后的下标平移维护。
 * @param      self 目标区域；可为 NULL。
 * @param      window 子窗口借用指针；可为 NULL。
 * @return     无返回值。
 */
static void xmdi_historyAdd(XMdiArea* self, XMdiSubWindow* window)
{
    XMdiSubWindow* item;
    if (!self || !self->m_activationHistory || !window) return;
    item = window;
    XVector_insert_1_base(self->m_activationHistory, 0, &item, 1);
}

/**
 * @brief      激活历史账本：摘除已移除的子窗口。
 * @details    对标 updateActiveWindow 的
 *             `indicesToActivatedChildren.removeAll(index)` 与后续下标
 *             递减（qmdiarea.cpp:1105-1113）；按指针记账时只需删除该
 *             指针一项。
 * @param      self 目标区域；可为 NULL。
 * @param      window 子窗口借用指针；可为 NULL。
 * @return     无返回值。
 */
static void xmdi_historyRemove(XMdiArea* self, const XMdiSubWindow* window)
{
    int64_t i;
    int64_t n;
    if (!self || !self->m_activationHistory || !window) return;
    n = XVector_size_base((const XContainer*)self->m_activationHistory);
    for (i = 0; i < n; ++i) {
        XMdiSubWindow** slot =
            (XMdiSubWindow**)XVector_at_base(self->m_activationHistory, i);
        if (slot && *slot == window) {
            XVector_remove_base(self->m_activationHistory, i, 1);
            return;
        }
    }
}

/**
 * @brief      激活历史账本：把刚激活的子窗口移到最前。
 * @details    对标 activateWindow 的
 *             `indicesToActivatedChildren.move(index, 0)`
 *             （qmdiarea.cpp:1018-1024）：最近激活在前。
 * @param      self 目标区域；可为 NULL。
 * @param      window 子窗口借用指针；可为 NULL。
 * @return     无返回值。
 */
static void xmdi_historyRaise(XMdiArea* self, XMdiSubWindow* window)
{
    int64_t i;
    int64_t n;
    if (!self || !self->m_activationHistory || !window) return;
    n = XVector_size_base((const XContainer*)self->m_activationHistory);
    for (i = 0; i < n; ++i) {
        XMdiSubWindow** slot =
            (XMdiSubWindow**)XVector_at_base(self->m_activationHistory, i);
        if (slot && *slot == window) {
            if (i > 0) XVector_move((XVector*)self->m_activationHistory, i, 0);
            return;
        }
    }
}

/**
 * @brief      活动子窗被移出后的激活收口（对标 QMdiAreaPrivate::
 *             updateActiveWindow / resetActiveWindow）。
 * @details    Qt 在移除活动窗后挑下一个可见子窗激活；无候选则
 *             resetActiveWindow 并发射 subWindowActivated(nullptr)
 *             （qmdiarea.cpp:1049-1126）。隐藏子窗不参与激活——对齐
 *             `activateWindow` 的 `if (child->isHidden()) return;`
 *             （qmdiarea.cpp:941-947）。**例外：Tabbed 视图**（P3-53）
 *             下非活动成员按页签语义恒隐藏，若仍按隐藏排除，关闭活动
 *             页签会误判「无候选」而失去激活窗；此时按 Qt 页签视图的
 *             indexToLastActiveTab 口径在登记表中挑下一个。
 * @param      self 目标区域；可为 NULL。
 * @param      removed 刚被移出的子窗口借用指针（仅用于排除自身）。
 */
static void xmdi_reactivateAfterRemove(XMdiArea* self,
                                       XMdiSubWindow* removed)
{
    int64_t i;
    int64_t n;
    bool tabbed;
    if (!self) return;
    if (self->m_active != removed) return;
    self->m_active = NULL;
    tabbed = (int)self->m_viewMode == (int)XMdiAreaViewMode_TabbedView;
    n = self->m_subWindows
            ? XVector_size_base((const XContainer*)self->m_subWindows) : 0;
    for (i = 0; i < n; ++i) {
        XMdiSubWindow** slot =
            (XMdiSubWindow**)XVector_at_base(self->m_subWindows, i);
        if (!slot || !*slot || *slot == removed) continue;
        if (!tabbed && XWidget_isHidden((XWidget*)*slot)) continue;
        XMdiArea_setActiveSubWindow(self, *slot);
        return;
    }
    xmdi_emitActivated(self, NULL);
}

/* ==================== XMdiSubWindow ==================== */

/* ==================== 子窗几何常量与标题带按钮 ==================== */

/** @brief 子窗标题带高度（像素；paintEvent 头带、内容件挂载与拖拽
 *         命中域共用同一常量口径）。 */
#define XMDI_SUBWINDOW_TITLEBAR_HEIGHT 20
/* 有父窗口时子窗 chrome 的最小宽度（标题带系统菜单图标位 + 最小化/关闭
 * 按钮位合计的经验下界；对标 Qt 由 CC_TitleBar 各子控件矩形求和得到的
 * minWidth，qmdisubwindow.cpp:1719-1745）。 */
#define XMDI_SUBWINDOW_CHROME_MIN_WIDTH 50
/* 标题带按钮命中矩形（与 paintEvent 的绘制位置同源）：关闭 "×" 画在
 * w-18 附近、最小化 8x1 横线画在 w-34..w-26，故命中域取两段互不重叠的
 * 区域。 */
#define XMDI_SUBWINDOW_BTN_H 17

/* 子窗窗框宽（像素）：**本库 chrome 实际保留的边框内缩**。paintEvent 画
 * 1px 窗框、内容件挂在 x=1/w-2（xmdi_subWindowContentRect），故可命中的
 * 边缘带厚度只能是 1——更宽的带会落在内容件矩形内、被内容件接住，永不
 * 送达子窗口。
 *
 * 与 Qt 的差异（有意，有依据）：Qt 的八向命中区按
 * `style()->pixelMetric(PM_MdiSubWindowFrameWidth)` 取带厚
 * （qmdisubwindow.cpp:1541，QCommonStyle 为 dpiScaled(4)，
 * qcommonstyle.cpp:4589-4591），并用同一值把内容件内缩
 * （updateGeometryConstraints 的 setContentsMargins(margin, titleBar,
 * margin, margin)，qmdisubwindow.cpp:1108-1109）——两者同源，所以 Qt 的
 * 4px 带永远可达。本库的窗框按本项目设计语言取 1px（同文件
 * VX_mdiSubWindow_paintEvent 头注的既有裁定），内容件也按 1px 内缩
 * （既有对齐用例断言的 x=1），故带厚跟内容内缩同取 1px 才可达。 */
#define XMDI_SUBWINDOW_FRAME_WIDTH 1

/* ==================== 八向边缘命中区（对标 Qt getRegion） ==================== */
/* 对标 Qt 6.8 qmdisubwindow.cpp:1531-1596 的 QMdiSubWindowPrivate::getRegion /
 * getOperation，以及 :1013-1024 initOperationMap 的操作→变更位→光标表。
 * Qt 把该枚举/表放在私有头（qmdisubwindow_p.h:93-141），本文件按同一数值
 * 与同一几何公式实现（公开枚举 XMdiSubWindowEdgeOperation 见头文件）。 */

/* 变更位（qmdisubwindow_p.h:107-115 逐值相同）。 */
#define XMDI_OP_HMOVE 0x01
#define XMDI_OP_VMOVE 0x02
#define XMDI_OP_HRESIZE 0x04
#define XMDI_OP_VRESIZE 0x08
#define XMDI_OP_HRESIZE_REVERSE 0x10
#define XMDI_OP_VRESIZE_REVERSE 0x20

/* 光标形状码（本文件内部）：不能直接存 XCursorShape 常量——XCURSOR_ON=0
 * 时回退头只定义 XCursor_Arrow，其余枚举量不存在，表会编译失败。 */
#define XMDI_CURSOR_ARROW 0
#define XMDI_CURSOR_SIZE_VER 1
#define XMDI_CURSOR_SIZE_HOR 2
#define XMDI_CURSOR_SIZE_FDIAG 3
#define XMDI_CURSOR_SIZE_BDIAG 4

/** @brief 操作信息表元素（对标 OperationInfo，qmdisubwindow_p.h:127-136）。 */
typedef struct XmdiOperationInfo
{
    int m_operation;    /**< XMdiSubWindowEdgeOperation。 */
    uint32_t m_flags;   /**< XMDI_OP_* 组合。 */
    int m_cursorShape;  /**< XMDI_CURSOR_* 码。 */
} XmdiOperationInfo;

/* 表序 = qmdisubwindow.cpp:1013-1024 initOperationMap 的插入序（Move 在
 * 首、四边次之、四角最后）。命中判定按「角→边→Move」优先级另行排序，
 * 因为 Qt 靠 Move 区**内缩**（不含外框带）实现互斥，本实现直接按序判。 */
static const XmdiOperationInfo kXmdiOperations[] = {
    /* 指定初始化器（仓库铁律：初值与字段顺序解耦）。 */
    {.m_operation = XMdiSubWindowEdgeOperation_Move,
     .m_flags = XMDI_OP_HMOVE | XMDI_OP_VMOVE,
     .m_cursorShape = XMDI_CURSOR_ARROW},
    {.m_operation = XMdiSubWindowEdgeOperation_TopResize,
     .m_flags = XMDI_OP_VMOVE | XMDI_OP_VRESIZE | XMDI_OP_VRESIZE_REVERSE,
     .m_cursorShape = XMDI_CURSOR_SIZE_VER},
    {.m_operation = XMdiSubWindowEdgeOperation_BottomResize,
     .m_flags = XMDI_OP_VRESIZE,
     .m_cursorShape = XMDI_CURSOR_SIZE_VER},
    {.m_operation = XMdiSubWindowEdgeOperation_LeftResize,
     .m_flags = XMDI_OP_HMOVE | XMDI_OP_HRESIZE | XMDI_OP_HRESIZE_REVERSE,
     .m_cursorShape = XMDI_CURSOR_SIZE_HOR},
    {.m_operation = XMdiSubWindowEdgeOperation_RightResize,
     .m_flags = XMDI_OP_HRESIZE,
     .m_cursorShape = XMDI_CURSOR_SIZE_HOR},
    {.m_operation = XMdiSubWindowEdgeOperation_TopLeftResize,
     .m_flags = XMDI_OP_HMOVE | XMDI_OP_VMOVE | XMDI_OP_HRESIZE |
                XMDI_OP_VRESIZE | XMDI_OP_VRESIZE_REVERSE |
                XMDI_OP_HRESIZE_REVERSE,
     .m_cursorShape = XMDI_CURSOR_SIZE_FDIAG},
    {.m_operation = XMdiSubWindowEdgeOperation_TopRightResize,
     .m_flags = XMDI_OP_VMOVE | XMDI_OP_HRESIZE | XMDI_OP_VRESIZE |
                XMDI_OP_VRESIZE_REVERSE,
     .m_cursorShape = XMDI_CURSOR_SIZE_BDIAG},
    {.m_operation = XMdiSubWindowEdgeOperation_BottomLeftResize,
     .m_flags = XMDI_OP_HMOVE | XMDI_OP_HRESIZE | XMDI_OP_VRESIZE |
                XMDI_OP_HRESIZE_REVERSE,
     .m_cursorShape = XMDI_CURSOR_SIZE_BDIAG},
    {.m_operation = XMdiSubWindowEdgeOperation_BottomRightResize,
     .m_flags = XMDI_OP_HRESIZE | XMDI_OP_VRESIZE,
     .m_cursorShape = XMDI_CURSOR_SIZE_FDIAG}
};

/** @brief 取操作的变更位组合（未登记操作按 0）。
 * @param operation XMdiSubWindowEdgeOperation。
 * @return XMDI_OP_* 位组合。 */
static uint32_t xmdi_operationFlags(int operation)
{
    size_t i;
    for (i = 0; i < sizeof(kXmdiOperations) / sizeof(kXmdiOperations[0]); ++i) {
        if (kXmdiOperations[i].m_operation == operation)
            return kXmdiOperations[i].m_flags;
    }
    return 0;
}

/** @brief 取操作的光标形状码（对标 OperationInfo::cursorShape）。
 * @param operation XMdiSubWindowEdgeOperation。
 * @return XMDI_CURSOR_* 码。 */
static int xmdi_operationCursor(int operation)
{
    size_t i;
    for (i = 0; i < sizeof(kXmdiOperations) / sizeof(kXmdiOperations[0]); ++i) {
        if (kXmdiOperations[i].m_operation == operation)
            return kXmdiOperations[i].m_cursorShape;
    }
    return XMDI_CURSOR_ARROW;
}

/** @brief 点是否在「一角」的 L 形带内（对标 getRegion 的角区减内矩）。
 * @details Qt 原文（qmdisubwindow.cpp:1572-1589）：
 *          `QRegion(w-tb, 0, tb, tb) - QRegion(w-tb, fw, tb-fw, tb-fw)` ——
 *          L 形 = 该角的**外框带**（上/下一条 + 左/右一条），中间 (tb-fw)²
 *          方块被减掉交给标题带/Move 区。此处按同一几何直算，避免为一次
 *          命中查询建 XRegion。
 * @param x/y 子窗局部坐标。
 * @param left/right/top/bottom 该角所在的两条边方向。
 * @return 命中返回 true。
 */
static bool xmdi_cornerContains(int x, int y, int w, int h, int tb, int fw,
                                bool rightEdge, bool bottomEdge)
{
    int x0 = rightEdge ? w - tb : 0;   /* 角区起点（Qt: width-titleBarHeight / 0）。 */
    int y0 = bottomEdge ? h - tb : 0;
    bool inBox;
    bool inInner;
    if (tb <= 0) return false;
    inBox = x >= x0 && x < x0 + tb && y >= y0 && y < y0 + tb;
    if (!inBox) return false;
    /* 内缩方块：横向在外框带之内侧起算（Qt 左侧角取 fw、右侧角取
     * width-titleBarHeight，两式在几何上等价于「角区左/右边界 + fw」）。 */
    {
        int ix0 = rightEdge ? w - tb : fw;
        int iy0 = bottomEdge ? h - tb : fw;
        int inner = tb - fw;
        if (inner <= 0) return true;
        inInner = x >= ix0 && x < ix0 + inner &&
                  y >= iy0 && y < iy0 + inner;
    }
    return !inInner;
}

/** @brief 计算子窗上一坐标点的窗框操作（对标 getOperation，
 *         qmdisubwindow.cpp:1590-1596）。
 * @details 判定优先级：四角 → 四边 → Move（标题带）。Qt 靠「Move 区内缩、
 *          不含外框带」实现与八向区互斥，本实现直接按优先级判，等价且
 *          不依赖迭代顺序。
 * @param sw 目标子窗口；为 NULL 返回 None。
 * @param x/y 子窗局部坐标。
 * @return XMdiSubWindowEdgeOperation。
 */
static int xmdi_edgeOperationAtPosition(const XMdiSubWindow* sw, int x, int y)
{
    int w;
    int h;
    const int tb = XMDI_SUBWINDOW_TITLEBAR_HEIGHT;
    const int fw = XMDI_SUBWINDOW_FRAME_WIDTH;
    if (!sw) return XMdiSubWindowEdgeOperation_None;
    if (!XWidget_parentWidget((const XWidget*)sw))
        return XMdiSubWindowEdgeOperation_None; /* 无父＝无 chrome、无交互。 */
    w = XWidget_width((const XWidget*)sw);
    h = XWidget_height((const XWidget*)sw);
    if (w <= 0 || h <= 0) return XMdiSubWindowEdgeOperation_None;
    if (x < 0 || y < 0 || x >= w || y >= h)
        return XMdiSubWindowEdgeOperation_None;
    /* 四角（L 形带）。 */
    if (xmdi_cornerContains(x, y, w, h, tb, fw, false, false))
        return XMdiSubWindowEdgeOperation_TopLeftResize;
    if (xmdi_cornerContains(x, y, w, h, tb, fw, true, false))
        return XMdiSubWindowEdgeOperation_TopRightResize;
    if (xmdi_cornerContains(x, y, w, h, tb, fw, false, true))
        return XMdiSubWindowEdgeOperation_BottomLeftResize;
    if (xmdi_cornerContains(x, y, w, h, tb, fw, true, true))
        return XMdiSubWindowEdgeOperation_BottomRightResize;
    /* 四边（qt: QRegion(titleBarHeight, 0, width-2*tb, fw) 等）。 */
    if (y < fw && x >= tb && x < w - tb)
        return XMdiSubWindowEdgeOperation_TopResize;
    if (y >= h - fw && x >= tb && x < w - tb)
        return XMdiSubWindowEdgeOperation_BottomResize;
    if (x < fw && y >= tb && y < h - tb)
        return XMdiSubWindowEdgeOperation_LeftResize;
    if (x >= w - fw && y >= tb && y < h - tb)
        return XMdiSubWindowEdgeOperation_RightResize;
    /* 标题带拖动区（Qt: QRegion(fw, fw, width-2*fw, titleBar-fw)）。 */
    if (y >= fw && y < tb && x >= fw && x < w - fw)
        return XMdiSubWindowEdgeOperation_Move;
    return XMdiSubWindowEdgeOperation_None;
}

/** @brief 该操作是否是缩放（对标 QMdiSubWindowPrivate::isResizeOperation）。 */
static bool xmdi_isResizeOperation(int operation)
{
    return operation >= XMdiSubWindowEdgeOperation_TopResize &&
           operation <= XMdiSubWindowEdgeOperation_BottomRightResize;
}

/** @brief 子窗当前是否允许移动（对标 updateGeometryConstraints 的
 *         moveEnabled，qmdisubwindow.cpp:1110-1119）。
 * @details Qt：最大化窗、以及最小化但未折叠的图标窗不可移动；折叠窗
 *          （最小化 + shaded）仍可移动。
 * @param sw 目标子窗口。
 * @return 允许返回 true。 */
static bool xmdi_subWindowMoveEnabled(const XMdiSubWindow* sw)
{
    if (!sw) return false;
    if (!XWidget_parentWidget((const XWidget*)sw)) return false;
    if (sw->m_maximizedMode || XWidget_isMaximized((const XWidget*)sw))
        return false;
    if (XWidget_isMinimized((const XWidget*)sw) && !sw->m_shaded)
        return false;
    return true;
}

/** @brief 子窗当前是否允许缩放（同上 resizeEnabled 判定）。
 * @details Qt：最大化／最小化图标态不可缩放；折叠态与
 *          `MSWindowsFixedSizeDialogHint` 亦不可缩放。
 * @param sw 目标子窗口。
 * @return 允许返回 true。 */
static bool xmdi_subWindowResizeEnabled(const XMdiSubWindow* sw)
{
    if (!sw) return false;
    if (!xmdi_subWindowMoveEnabled(sw)) return false;
    if (sw->m_shaded) return false;
    if (XWidget_windowFlags((const XWidget*)sw) &
        (XWidgetFlags)XWindowType_MSWindowsFixedSizeDialogHint)
        return false;
    return true;
}

/** @brief 按操作刷新子窗光标（对标 updateCursor，qmdisubwindow.cpp:1063-1082）。
 * @details None ⇒ unsetCursor（回落父链/默认箭头）；Move/八向 ⇒ 对应形状。
 *          同形状早退，避免每次移动都重建光标对象并重下发平台。
 * @param sw 目标子窗口。
 * @param operation XMdiSubWindowEdgeOperation。
 * @return 无返回值。 */
static void xmdi_applyOperationCursor(XMdiSubWindow* sw, int operation)
{
#if XCURSOR_ON
    XCursorShape shape;
    XCursor* cursor;
    int code;
    if (!sw) return;
    if (sw->m_edgeOperation == (uint8_t)operation) return;
    sw->m_edgeOperation = (uint8_t)operation;
    if (operation == XMdiSubWindowEdgeOperation_None) {
        XWidget_unsetCursor((XWidget*)sw);
        return;
    }
    code = xmdi_operationCursor(operation);
    switch (code) {
    case XMDI_CURSOR_SIZE_VER:   shape = XCursor_SizeVer; break;
    case XMDI_CURSOR_SIZE_HOR:   shape = XCursor_SizeHor; break;
    case XMDI_CURSOR_SIZE_FDIAG: shape = XCursor_SizeFDiag; break;
    case XMDI_CURSOR_SIZE_BDIAG: shape = XCursor_SizeBDiag; break;
    default:                     shape = XCursor_Arrow; break;
    }
    cursor = XCursor_create_shape(shape);
    if (!cursor) return;
    XWidget_setCursor((XWidget*)sw, cursor);
    XClassDelete((XCursor*)cursor);
#else
    if (sw) sw->m_edgeOperation = (uint8_t)operation;
#endif /* XCURSOR_ON */
}


/** @brief 标题带是否显示/响应关闭按钮。
 * @details Qt 的 CloseAction 可见条件是 `windowFlags &
 *          Qt::WindowSystemMenuHint` 且非 FramelessWindowHint
 *          （QMdiSubWindowPrivate::updateActions，qmdisubwindow.cpp:
 *          1930-1960）；按钮命中与绘制共用本判定。
 * @param sw 目标子窗口；可为 NULL。
 * @return 可关闭返回 true。 */
static bool xmdi_subWindowHasCloseButton(const XMdiSubWindow* sw)
{
    XWidgetFlags flags;
    if (!sw) return false;
    flags = XWidget_windowFlags((XWidget*)sw);
    if (flags & (XWidgetFlags)XWindowType_FramelessWindowHint) return false;
    return (flags & (XWidgetFlags)XWindowType_WindowSystemMenuHint) != 0;
}

/** @brief 标题带是否显示/响应最小化按钮（Qt：WindowMinimizeButtonHint，
 *         同上 updateActions）。
 * @param sw 目标子窗口；可为 NULL。
 * @return 可最小化返回 true。 */
static bool xmdi_subWindowHasMinButton(const XMdiSubWindow* sw)
{
    XWidgetFlags flags;
    if (!sw) return false;
    flags = XWidget_windowFlags((XWidget*)sw);
    if (flags & (XWidgetFlags)XWindowType_FramelessWindowHint) return false;
    return (flags & (XWidgetFlags)XWindowType_WindowMinimizeButtonHint) != 0;
}

/** @brief 取标题带按钮的命中矩形（局部坐标）。
 * @param sw 目标子窗口。
 * @param close true=关闭按钮，false=最小化按钮。
 * @param out 输出矩形（可为 NULL）。
 * @return 该按钮有绘制/命中位返回 true（窗口过窄返回 false）。 */
static bool xmdi_subWindowButtonRect(const XMdiSubWindow* sw, bool close,
                                     XRect* out)
{
    int w;
    if (!sw) return false;
    w = XWidget_width((const XWidget*)sw);
    if (w <= 40) return false;
    if (close) {
        /* "×" 画在 w-18 附近：命中域 [w-26, w-10)。 */
        if (w - 26 < 18) return false; /* 让开系统菜单图标位 */
        if (out) XRect_init(out, w - 26, 2, 16, XMDI_SUBWINDOW_BTN_H);
    } else {
        /* 最小化 8x1 横线画在 w-34..w-26：命中域 [w-40, w-26)。 */
        if (w - 40 < 18) return false;
        if (out) XRect_init(out, w - 40, 2, 14, XMDI_SUBWINDOW_BTN_H);
    }
    return true;
}

/** @brief 标题带关闭按钮的关闭链：close() + DeleteOnClose 收口。
 * @details 对标 ControllerWidget 的 `_q_close` → `mdiChild->close()`
 *          （qmdisubwindow.cpp:701/617），随后按 QWidget 的
 *          WA_DeleteOnClose 语义回收内部创建的子窗（qmdiarea.cpp:1962）。
 *          关闭被 closeEvent 拒绝时保持原状（Qt 同）。
 * @param sw 目标子窗口。
 * @return 无返回值。 */
static void xmdi_closeFromTitleBar(XMdiSubWindow* sw)
{
    XMdiArea* area;
    bool deleteOnClose;
    int64_t i;
    if (!sw) return;
    area = XMdiSubWindow_mdiArea(sw);
    deleteOnClose = XWidget_testAttribute((XWidget*)sw,
                                          XWidgetAttribute_DeleteOnClose);
    if (!XWidget_close((XWidget*)sw)) return; /* 关闭被拒绝 */
    if (!area) return;
    if (deleteOnClose) {
        i = xmdi_subWindowIndex(area, sw);
        if (i >= 0) XVector_remove_base(area->m_subWindows, i, 1);
        xmdi_historyRemove(area, sw);
    }
    xmdi_reactivateAfterRemove(area, sw);
    if (deleteOnClose) XObject_deleteLater((XObject*)sw);
}

/** @brief 子窗 chrome 绘制（对标 QMdiSubWindow::paintEvent，
 *         qmdisubwindow.cpp:3086-3138 的标准条构造）。
 * @details 第八轮总验收 FAIL② 根修：此前仅涂「裸蓝标题条 + 白体」，
 *          无窗框无图标无按钮位（r3e_mdi_zoom.png）。补齐：
 *          - 窗框 1px（Mid；对标 PE_FrameWindow，宽度按本项目设计
 *            语言取 1px 而非 Fusion 的 PM_MdiSubWindowFrameWidth=4）；
 *          - 标题带：激活=Highlight 底（qmdisubwindow.cpp:1637-1644
 *            State_Active 分支；激活判定=mdiArea()->activeSubWindow
 *            ==self），非激活=Window 底；
 *          - 带左系统菜单图标位（迷你窗图形，对标 CC_TitleBar 的
 *            SC_TitleBarSysMenu）、右最小化/关闭按钮（SC_TitleBar
 *            MinButton/CloseButton：命中域见
 *            xmdi_subWindowButtonRect，关闭走 close 链、最小化走
 *            showMinimized；按钮显隐受 windowFlags 提示位与
 *            FramelessWindowHint 约束，同 QMdiSubWindowPrivate::
 *            updateActions）；
 *          - 标题文本自基类 windowTitle()（对标 QWidget 属性；Qt 的
 *            QMdiSubWindow 没有独立标题字段）；setWidget 时为空则采纳
 *            内容件 windowTitle（qmdisubwindow.cpp:994 标题同步链的
 *            静态版）。
 *          脏区口径不变：只绘事件脏区∩各铬件（night #50 修复保持）。
 *          内容件挂载已内收 1px（XMdiSubWindow_setWidget/resizeEvent），
 *          侧框线不再被子控件覆盖。 */
static void VX_mdiSubWindow_paintEvent(XWidget* self, XEvent* event)
{
    XMdiSubWindow* sw = (XMdiSubWindow*)self;
    XPainter painter;
    XImage* image;
    XPoint offset;
    XRect head;
    XRect body;
    XRect exposed;
    XRect piece;
    XRect clipHead;
    const char* titleText;
    uint32_t bandColor;
    uint32_t textColor;
    uint32_t frameColor;
    uint32_t base;
    bool active;
    int w;
    int h;
    XWidget* parent;
    XRect visible;
    int pw;
    int ph;
    if (!sw || !event) return;
    w = XWidget_width(self);
    h = XWidget_height(self);
    image = XWidget_paintImage(self);
    if (!image) return;
    XPainter_init(&painter, NULL);
    if (!XPainter_begin_image(&painter, image)) {
        XPainter_deinit(&painter);
        return;
    }
    offset = XWidget_paintOffset(self);
    if (offset.x != 0 || offset.y != 0)
        XPainter_translate(&painter, (float)offset.x, (float)offset.y);
#if XPALETTE_ON
    {
        XPalette palette = XWidget_palette(self);
        XColor c = XPalette_color(&palette, XPaletteColorGroup_Current,
                                  XPaletteColorRole_Base);
        base = XColor_rgba(&c);
        c = XPalette_color(&palette, XPaletteColorGroup_Current,
                           XPaletteColorRole_Mid);
        frameColor = XColor_rgba(&c);
        c = XPalette_color(&palette, XPaletteColorGroup_Current,
                           XPaletteColorRole_Window);
        /* 非激活标题带底色默认（对标 titleBarOptions 的 Inactive 色组，
         * qmdisubwindow.cpp:1640-1643）。 */
        bandColor = XColor_rgba(&c);
        c = XPalette_color(&palette, XPaletteColorGroup_Current,
                           XPaletteColorRole_WindowText);
        textColor = XColor_rgba(&c);
    }
#else
    bandColor = 0xFFD8D8D8u;
    textColor = 0xFF000000u;
    base = 0xFFFFFFFFu;
    frameColor = 0xFF808080u;
#endif /* XPALETTE_ON */
    /* 激活判定：所属 MDI 区当前活动子窗（脱离区域的独立子窗按非激
       活呈现，同 Qt 无 parent 时无活动态）；激活带=Highlight 底 +
       HighlightedText 前景（qmdisubwindow.cpp:1637-1639）。 */
    {
        XMdiArea* area = XMdiSubWindow_mdiArea(sw);
        active = area && XMdiArea_activeSubWindow(area) ==
                            (XMdiSubWindow*)sw;
    }
    if (active) {
#if XPALETTE_ON
        XPalette palette = XWidget_palette(self);
        XColor c = XPalette_color(&palette, XPaletteColorGroup_Current,
                                  XPaletteColorRole_Highlight);
        bandColor = XColor_rgba(&c);
        c = XPalette_color(&palette, XPaletteColorGroup_Current,
                           XPaletteColorRole_HighlightedText);
        textColor = XColor_rgba(&c);
#else
        bandColor = 0xFF3080C0u;
        textColor = 0xFFFFFFFFu;
#endif /* XPALETTE_ON */
    }
    /* 只绘事件脏区（本地坐标，收进控件矩形）：此前恒整幅涂写标题条
     * +白底，多矩形脏区逐矩形派发时本窗的整幅白底会抹掉先派矩形刚
     * 恢复的内容标签像素，且抹写不受 present 区域保护（night #50
     * 交互擦除链的子窗一级；对标 Qt paintEvent 经裁剪只绘暴露区）。 */
    exposed = ((const XPaintEvent*)event)->m_rect;
    if (exposed.x < 0) { exposed.width += exposed.x; exposed.x = 0; }
    if (exposed.y < 0) { exposed.height += exposed.y; exposed.y = 0; }
    if (exposed.x + exposed.width > w) exposed.width = w - exposed.x;
    if (exposed.y + exposed.height > h) exposed.height = h - exposed.y;
    /* 父界裁剪（对标 Qt 通用语义：子控件绘制恒被 parentWidget() 矩形
     * 裁剪）。本框架 paintTree 不按父矩形裁剪子件绘制，子窗被拖到视
     * 口边缘后带/体像素会泼到视口外的页签标签、状态栏等区域（夜间 D
     * 猎捕 d1_i4 实锚）。此处子窗一级自裁剪：绘制域收进「父件矩形平
     * 移到本窗局部坐标」的交集，拖出视口的部分不再落笔，观感与 Qt
     * 一致（体/带在视口边缘截断）。父区缺失（独立子窗）或退化时不裁
     * 剪。无效暴露区（完全出界）直接跳过绘制。 */
    parent = XWidget_parentWidget(self);
    if (parent) {
        pw = XWidget_width(parent);
        ph = XWidget_height(parent);
        if (pw > 0 && ph > 0) {
            XRect geoSelf = XWidget_geometry(self);
            XRect_init(&visible, -geoSelf.x, -geoSelf.y, pw, ph);
            exposed = XRect_intersected(&exposed, &visible);
        }
    }
    if (exposed.width <= 0 || exposed.height <= 0) {
        XPainter_deinit(&painter);
        return;
    }
    /* 体不透明填充（行 20 起、内收侧/底框线）：此前仅涂标题条，标题条
     * 以下透明透出视口背景。 */
    if (h > 21 && w > 2) {
        XRect_init(&body, 1, 20, w - 2, h - 21);
        body = XRect_intersected(&body, &exposed);
        if (body.width > 0 && body.height > 0)
            XPainter_fillRect(&painter, &body, base);
    }
    /* 标题带（框线内 1..19 行；第 19 行兼作带下分隔）。 */
    XRect_init(&head, 1, 1, w > 2 ? w - 2 : 0, 19);
    clipHead = head;
    /* 图标/文本绘制用 clipHead 作画家裁剪，须同受父界约束（head 填充
     * 已随 exposed 收进父界；此处保持两者口径一致）。 */
    if (parent && pw > 0 && ph > 0)
        clipHead = XRect_intersected(&clipHead, &visible);
    head = XRect_intersected(&head, &exposed);
    if (head.width > 0 && head.height > 0)
        XPainter_fillRect(&painter, &head, bandColor);
    /* 窗框 1px：顶/左/右/底（最后绘制，保证框线压住体填充）。 */
    if (w > 0 && h > 0) {
        if (0 >= exposed.y && 0 < exposed.y + exposed.height) {
            XRect_init(&piece, exposed.x, 0, exposed.width, 1);
            XPainter_fillRect(&painter, &piece, frameColor);
        }
        if (h - 1 >= exposed.y && h - 1 < exposed.y + exposed.height &&
            exposed.width > 0) {
            XRect_init(&piece, exposed.x, h - 1, exposed.width, 1);
            XPainter_fillRect(&painter, &piece, frameColor);
        }
        if (0 >= exposed.x && 0 < exposed.x + exposed.width) {
            XRect_init(&piece, 0, exposed.y, 1, exposed.height);
            XPainter_fillRect(&painter, &piece, frameColor);
        }
        if (w - 1 >= exposed.x && w - 1 < exposed.x + exposed.width &&
            exposed.height > 0) {
            XRect_init(&piece, w - 1, exposed.y, 1, exposed.height);
            XPainter_fillRect(&painter, &piece, frameColor);
        }
    }
    if (head.width > 0 && head.height > 0) {
        XPainter_setClipRect(&painter, &clipHead,
                             XPainterClipOperation_ReplaceClip);
        /* 左：系统菜单图标（迷你窗图形，对标 CC_TitleBar 的
         * SC_TitleBarSysMenu；点击即弹出系统菜单，同 Qt 的
         * ControlLabel::_q_showSystemMenu，qmdisubwindow.cpp:416-448）。 */
        XPainter_setPen(&painter, textColor);
        XPainter_drawRect_2(&painter, 4.0f, 4.0f, 10.0f, 10.0f);
        XRect_init(&piece, 5, 5, 8, 3);
        XPainter_fillRect(&painter, &piece, textColor);
        /* 标题文本：与 QMdiSubWindow 同源读基类 windowTitle()
         * （qmdisubwindow.cpp:3086-3138 的 titleBarOptions()->text），
         * 不再维护独立标题字段。 */
        titleText = "";
        {
            const XString* title = XWidget_windowTitle(self);
            if (title && XString_size((const XContainer*)title) > 0)
                titleText = XString_toUtf8(title);
        }
        if (titleText && *titleText)
            /* x=18 让出图标位；基线 14 同 XDockWidget 标题条口径。 */
            XPainter_drawText(&painter, 18, 14, titleText, textColor);
        /* 右：最小化（8x1 横线）+ 关闭（"×" 字形）按钮，位置与命中矩形
         * 同源（xmdi_subWindowButtonRect）；显隐按窗口标志提示位
         * （QMdiSubWindowPrivate::updateActions 同判据）。 */
        if (w > 40) {
            if (xmdi_subWindowHasMinButton(sw) &&
                xmdi_subWindowButtonRect(sw, false, &piece)) {
                XRect lineRect;
                XRect_init(&lineRect, w - 34, 10, 8, 1);
                XPainter_fillRect(&painter, &lineRect, textColor);
            }
            if (xmdi_subWindowHasCloseButton(sw))
                XPainter_drawText(&painter, w - 18, 14, "\xc3\x97",
                                  textColor);
        }
        XPainter_setClipping(&painter, false);
    }
    XPainter_deinit(&painter);
}

/* ==================== 子窗移动/八向缩放会话 ==================== */
/* 对标 Qt 6.8.3 qmdisubwindow.cpp 的整条交互链：
 *   mousePressEvent:3153-3180（getOperation → updateCursor →
 *     mousePressPosition/oldGeometry → grabMouse）
 *   → mouseMoveEvent:3290-3313（setNewGeometry(parentWidget()->
 *     mapFromGlobal(globalPosition()))）
 *   → setNewGeometry:1151-1213（钳位 + 变更位 + 尺寸下限）
 *   → mouseReleaseEvent:3234-3260（重算 currentOperation + 光标）
 * 以及 qmdisubwindow.cpp:1013-1024 的操作→变更位→光标表、:1531-1600 的
 * getRegion/getOperation 命中区。
 * 会话状态按模块级单例收拢在实现文件内——鼠标单点语义下同一时刻至多一路
 * 会话，口径同 XSizeGrip.c 的 g_sizeGripDragActive 模块级登记。头带高度 20
 * 与 paintEvent 头带及内容件 y=20 挂载（XMdiSubWindow_setWidget）共用
 * XMDI_SUBWINDOW_TITLEBAR_HEIGHT（定义于本文件子窗 chrome 常量段）。 */
/* 拖拽视口钳位边距（对标 Qt 6.8.3 qmdisubwindow.cpp 文件级
 * static const int BoundaryMargin = 5）。 */
#define XMDI_SUBWINDOW_BOUNDARY_MARGIN 5
/** @brief 子窗移动/缩放会话（对标 QMdiSubWindowPrivate 的
 *         currentOperation / mousePressPosition / oldGeometry 三件套，
 *         qmdisubwindow_p.h:150-153，qmdisubwindow.cpp:1151-1213）。
 * @details 会话状态按模块级单例收拢在实现文件内——鼠标单点语义下同一时刻
 *          至多一路会话（同 XSizeGrip.c 的 g_sizeGripDragActive 口径）。
 *          移动与缩放**共用同一会话槽**：Qt 的 currentOperation 也是同一个
 *          字段，两者互斥（操作表里每项只带自己的变更位）。
 *          坐标一律存**父区（视口）局部**与**全局**两套：增量用全局差值算
 *          （局部坐标随命中接收者改写不可靠），钳位公式用父区局部量。 */
typedef struct XmdiPointerSession
{
    bool m_active;            /**< 会话进行中（按下后、释放前）。 */
    XMdiSubWindow* m_window;  /**< 会话归属子窗（借用；防串扰）。 */
    int m_operation;          /**< XMdiSubWindowEdgeOperation。 */
    XPoint m_pressCursor;     /**< 会话的「光标锚点」：**父区（视口）局部**
                               *   坐标（对标 Qt 的 mousePressPosition =
                               *   mapToParent(pos)，qmdisubwindow.cpp:931/
                               *   936/3169）。鼠标路径按全局差值推进它、
                               *   键盘路径按方向键步长推进它——两条路径
                               *   共用同一字段，故语义必须同域。 */
    XPoint m_pressLocal;      /**< 按下点在本子窗局部坐标（= 父区局部
                               *   minus 旧几何原点，Qt 的
                               *   mousePressPosition - oldGeometry 同义；
                               *   一次交互内不刷新，对应 Qt 的
                               *   oldGeometry）。 */
    XPoint m_pressOriginGlobal; /**< 按下时的**屏幕全局**坐标（仅鼠标路径
                                 *   用：每步增量 = 当前全局 − 本值）。 */
    XRect m_startGeometry;    /**< 按下时子窗几何（父区局部；Qt oldGeometry）。 */
} XmdiPointerSession;

static XmdiPointerSession g_mdiSession = {
    .m_active = false,
    .m_window = NULL,
    .m_operation = XMdiSubWindowEdgeOperation_None,
    .m_pressCursor = {0, 0},
    .m_pressLocal = {0, 0},
    .m_pressOriginGlobal = {0, 0},
    .m_startGeometry = {0, 0, 0, 0}};

/* ==================== 键盘交互模式（系统菜单 &Move / &Size） ============ */
/* 对标 QMdiSubWindowPrivate::_q_enterInteractiveMode / leaveInteractiveMode
 * （qmdisubwindow.cpp:905-945、965-980）与 QMdiSubWindow::keyPressEvent
 * （:3316-3375）。Qt 用 isInInteractiveMode + currentOperation 两个私有字段，
 * 本实现按同一语义收拢成模块级单例（同 g_mdiSession 的裁定）。 */

/** @brief 键盘交互模式状态。
 * @details 操作本身记在 g_mdiSession.m_operation（两路径共用会话），
 *          这里只记「在位」与归属窗（Qt 的 isInInteractiveMode 一位）。 */
typedef struct XmdiInteractiveMode
{
    bool m_active;            /**< 交互模式进行中（Qt isInInteractiveMode）。 */
    XMdiSubWindow* m_window;  /**< 归属子窗（借用）。 */
} XmdiInteractiveMode;

static XmdiInteractiveMode g_mdiInteractive = {
    .m_active = false, .m_window = NULL};

/** @brief 进入键盘交互模式（对标 _q_enterInteractiveMode，
 *         qmdisubwindow.cpp:905-945）。
 * @details Qt 语义：
 *          - Move：`currentOperation = Move;
 *            pressPos = QPoint(q->width() / 2, titleBarHeight() - 1);`
 *          - Resize：`currentOperation = isLeftToRight() ? BottomRightResize
 *            : BottomLeftResize;`，`offset = PM_MdiSubWindowFrameWidth / 2`，
 *            `pressPos = (w - offset, h - offset)`（RTL 取 `(offset, h-offset)`）；
 *          - 之后 `mousePressPosition = mapToParent(pressPos);
 *            oldGeometry = geometry(); isInInteractiveMode = true;
 *            setFocus(); grabMouse();`。
 *          与 Qt 的一处**有意差异**：Qt 尾段 `q->cursor().setPos(...)` 把
 *          系统光标物理挪到按下点。本框架不在交互入口挪用户指针（外部
 *          可见副作用），键盘增量改以「当前会话基准点」推进（见
 *          VX_mdiSubWindow_keyPressEvent），行为等价且不劫持指针。
 * @param sw 目标子窗口；NULL/无父时直接返回（Qt `if (!parent())` 同）。
 * @param operation Move 或 BottomRight/BottomLeftResize。
 * @return 无返回值。 */
static void xmdi_enterInteractiveMode(XMdiSubWindow* sw, int operation)
{
    XPoint pressLocal;
    int w;
    int h;
    int offset;
    if (!sw) return;
    if (!XWidget_parentWidget((const XWidget*)sw)) return;
    /* 启用门（Qt 的 `if (d->resizeEnabled || d->moveEnabled)`）：
     * 最大化/最小化图标态不可交互，折叠态可移动不可缩放。 */
    if (operation == XMdiSubWindowEdgeOperation_Move) {
        if (!xmdi_subWindowMoveEnabled(sw)) return;
    } else if (!xmdi_subWindowResizeEnabled(sw)) {
        return;
    }
    w = XWidget_width((const XWidget*)sw);
    h = XWidget_height((const XWidget*)sw);
    offset = XMDI_SUBWINDOW_FRAME_WIDTH / 2;
    if (operation == XMdiSubWindowEdgeOperation_Move)
        XPoint_init(&pressLocal, w / 2, XMDI_SUBWINDOW_TITLEBAR_HEIGHT - 1);
    else if (XWidget_isLeftToRight((const XWidget*)sw))
        XPoint_init(&pressLocal, w - offset, h - offset);
    else
        XPoint_init(&pressLocal, offset, h - offset);
    g_mdiSession.m_active = true;
    g_mdiSession.m_window = sw;
    g_mdiSession.m_operation = operation;
    g_mdiSession.m_pressLocal = pressLocal;
    g_mdiSession.m_startGeometry = XWidget_geometry((XWidget*)sw);
    /* m_pressCursor 在此路径存**父区局部的光标位置**（Qt 的
     * `mousePressPosition` 就是 mapToParent 后的父区坐标，
     * qmdisubwindow.cpp:931/936；mouseMoveEvent 才用 mapFromGlobal 喂
     * 同域坐标）。鼠标路径亦同：按下时按「本窗局部按下点 + 旧几何原点」
     * 换算成父区局部（见 xmdi_sessionPressPosition）。 */
    g_mdiSession.m_pressCursor.x =
        g_mdiSession.m_startGeometry.x + pressLocal.x;
    g_mdiSession.m_pressCursor.y =
        g_mdiSession.m_startGeometry.y + pressLocal.y;
    /* Qt 尾段把系统光标 warp 到 mapToGlobal(pressPos) ⇒ 交互期间若收到
     * 鼠标 MOVE，其全局差值要以「按下点全局坐标」为基准。本实现不物理
     * warp（文件头声明的有意差异），但基准点必须登记，否则交互期间来
     * 一帧 MOVE 会拿陈旧 origin 算出大跳变（复核意见②）。 */
    g_mdiSession.m_pressOriginGlobal =
        XWidget_mapToGlobal((const XWidget*)sw, &pressLocal);
    xmdi_applyOperationCursor(sw, operation);
    g_mdiInteractive.m_active = true;
    g_mdiInteractive.m_window = sw;
    XWidget_setFocus((XWidget*)sw);
    XWidget_grabMouse((XWidget*)sw);
}

/** @brief 退出键盘交互模式（对标 leaveInteractiveMode，
 *         qmdisubwindow.cpp:965-980）。
 * @param sw 目标子窗口；NULL = 清任何在位的交互模式。
 * @return 无返回值。 */
static void xmdi_leaveInteractiveMode(XMdiSubWindow* sw)
{
    XMdiSubWindow* w;
    if (!g_mdiInteractive.m_active) return;
    if (sw && g_mdiInteractive.m_window != sw) return;
    w = g_mdiInteractive.m_window;
    g_mdiInteractive.m_active = false;
    g_mdiInteractive.m_window = NULL;
    if (!w) return;
    /* 会话同步收口（Qt 的 currentOperation = None）：鼠标会话与交互模式
     * 共用 g_mdiSession。 */
    if (g_mdiSession.m_window == w) {
        g_mdiSession.m_active = false;
        g_mdiSession.m_window = NULL;
        g_mdiSession.m_operation = XMdiSubWindowEdgeOperation_None;
    }
    XWidget_releaseMouse((XWidget*)w);
    xmdi_applyOperationCursor(w, XMdiSubWindowEdgeOperation_None);
}

/** @brief 内容件在子窗内的挂载矩形（标题带下方、框线内收 1px）。
 * @details chrome 窗框（paintEvent）绘制占用的 1px 边缘不归内容件：
 *          x=1 起、宽 w-2（避侧框线），y=20（标题带 0..19 行）起、
 *          高 h-21（底框线 1 行）；尺寸不足时钳 0（折叠态 h=20 时
 *          内容件 0 高不绘，同 Qt 折叠仅标题条）。setWidget 挂载与
 *          resizeEvent 随框布局共用同一口径。 */
static void xmdi_subWindowContentRect(const XMdiSubWindow* self, XRect* out)
{
    int w;
    int h;
    const int fw = XMDI_SUBWINDOW_FRAME_WIDTH;
    if (!out) return;
    w = self ? XWidget_width((const XWidget*)self) : 0;
    h = self ? XWidget_height((const XWidget*)self) : 0;
    XRect_init(out, fw, XMDI_SUBWINDOW_TITLEBAR_HEIGHT,
               w > 2 * fw ? w - 2 * fw : 0,
               h > XMDI_SUBWINDOW_TITLEBAR_HEIGHT + fw
                   ? h - XMDI_SUBWINDOW_TITLEBAR_HEIGHT - fw : 0);
}

static int xmdi_dragClamp(int value, int low, int high)
{
    if (value < low) return low;
    if (value > high) return high;
    return value;
}

/** @brief 拖动一步的脏区全覆盖（对标 QMdiAreaPrivate 移动 update 系统）。
 * @details 第八轮总验收 FAIL① 根修：拖后标题栏左端「台阶状缺角」
 *          （r3e_mdi_drag2_zoom.png）= 旧位部分行列从未进入任何已派发
 *          脏区快照。XWidget_setGeometry 内联的 old∪new 包围盒失效
 *          （XWidget.c recomputeGeometry）与显式 update 共用同一异步
 *          管线（顶层脏区 → PAINT 快照 → 求差派发），单次快照竞态下
 *          可能漏行。本助手在每步拖动后追加一次显式 update：
 *          旧位 ∪ 新位 ∪ 平移扫掠 delta 带（横/纵四条带）逐矩形入队，
 *          同区域多次入队幂等，任一次快照漏区都被其余入队补齐——
 *          脏区覆盖从「单来源单快照」变为「双来源多快照」全闭合。
 * @param      parent 子窗父容器（视口；矩形同子窗局部坐标系）。
 * @param      oldRect 本步拖动前子窗矩形。
 * @param      newRect 本步拖动后子窗矩形。
 * @return     无返回值。
 */
static void xmdi_dragUpdateCoverage(XWidget* parent,
                                    const XRect* oldRect,
                                    const XRect* newRect)
{
    XRegion region;
    XRect band;
    int dx = newRect->x - oldRect->x;
    int dy = newRect->y - oldRect->y;
    int minY = oldRect->y < newRect->y ? oldRect->y : newRect->y;
    int minX = oldRect->x < newRect->x ? oldRect->x : newRect->x;
    int spanH = oldRect->height > newRect->height
                    ? oldRect->height : newRect->height;
    int spanW = oldRect->width > newRect->width
                    ? oldRect->width : newRect->width;
    if (!parent) return;
    XRegion_init(&region);
    XRegion_addRect(&region, oldRect);
    XRegion_addRect(&region, newRect);
    /* 横向扫掠带：水平位移在新旧位之间扫过的竖条（含对角移动的
       斜向耦合段，覆盖域=平移 Minkowski 和，宽 |dx| 高 h+|dy|）。 */
    if (dx > 0) {
        XRect_init(&band, oldRect->x + oldRect->width, minY, dx,
                   spanH + (dy < 0 ? -dy : dy));
        XRegion_addRect(&region, &band);
    } else if (dx < 0) {
        XRect_init(&band, newRect->x + newRect->width, minY, -dx,
                   spanH + (dy < 0 ? -dy : dy));
        XRegion_addRect(&region, &band);
    }
    /* 纵向扫掠带：垂直位移扫过的横条（宽 w+|dx| 高 |dy|）。 */
    if (dy > 0) {
        XRect_init(&band, minX, oldRect->y + oldRect->height,
                   spanW + (dx < 0 ? -dx : dx), dy);
        XRegion_addRect(&region, &band);
    } else if (dy < 0) {
        XRect_init(&band, minX, newRect->y + newRect->height,
                   spanW + (dx < 0 ? -dx : dx), -dy);
        XRegion_addRect(&region, &band);
    }
    XWidget_updateRegion(parent, &region);
    XRegion_deinit(&region);
}

static void VX_mdiSubWindow_mousePressEvent(XWidget* self, XEvent* event)
{
    XMdiSubWindow* sw = (XMdiSubWindow*)self;
    XMouseEvent* me = (XMouseEvent*)event;
    XPoint pos;
    int operation;
    if (!sw || !event ||
        XEvent_type(event) != XEVENT_TYPE_MOUSE_BUTTON_PRESS) return;
    /* 键盘交互模式随按下立即退出（对标 qmdisubwindow.cpp:3151-3152：
     * `if (d->isInInteractiveMode) d->leaveInteractiveMode();` 在一切
     * 分支之前）。缺了它：交互中按左键会照常建鼠标会话并改几何，随后的
     * release 结束会话，但交互模式仍在位——下一次 keyPressEvent 拿着
     * 陈旧 m_startGeometry 计算，窗口跳回旧位（复核意见②实测）。 */
    xmdi_leaveInteractiveMode(sw);
    if (XMouseEvent_button(me) != XMouseButton_LeftButton) {
        /* 非左键：交基类默认链（对标 qmdisubwindow.cpp:3163-3166
         * 非左键 ignore 沿父链传播）。注意须用 XClass_Parent 直呼父类
         * 实现——XWidget_mousePressEvent_base 是 vtable 派发助手，会重入
         * 本重载造成无限递归（复扫-5 实证）。 */
        XClass_Parent(XWidget, EXWidget_MousePressEvent,
                      void (*)(XWidget*, XEvent*))((XWidget*)self, event);
        return;
    }
    pos = XMouseEvent_position(me);
    /* 命中操作（对标 currentOperation = d->getOperation(pos)，
     * qmdisubwindow.cpp:3163-3175）：四角/四边/标题带三档。
     * 头带以下且不在窗框带内 ⇒ None，交基类链（内容件命中域）。 */
    operation = xmdi_edgeOperationAtPosition(sw, pos.x, pos.y);
    if (operation == XMdiSubWindowEdgeOperation_None) {
        XClass_Parent(XWidget, EXWidget_MousePressEvent,
                      void (*)(XWidget*, XEvent*))((XWidget*)self, event);
        return;
    }
    /* 按下即激活本子窗（对标 qmdisubwindow.cpp:958 按压激活链，
     * qmdiarea.cpp 视口事件过滤器的 setActiveSubWindow 同义）：激活态
     * 驱动标题带 Highlight/Window 配色（paintEvent），拖动非活动窗时
     * 配色即时翻转，不再出现「灰带被拖」。区域缺失（独立子窗）跳过。 */
    {
        XMdiArea* area = XMdiSubWindow_mdiArea(sw);
        if (area) XMdiArea_setActiveSubWindow(area, sw);
    }
    /* 标题带按钮命中优先于拖拽（对标 CC_MdiControls 的
     * hitTestComplexControl + _q_close/_q_minimize 连接，
     * qmdisubwindow.cpp:519-529、617-627、701-703）。命中矩形与
     * paintEvent 的绘制位置同源（xmdi_subWindowButtonRect）。按钮只落在
     * 标题带（Move 区）内，缩放带不受影响。 */
    if (operation == XMdiSubWindowEdgeOperation_Move) {
        XRect btn;
        /* 左侧系统菜单图标（同 paintEvent 的 4..14 方框）：弹出系统菜单
         * （对标 SC_TitleBarSysMenu 的 _q_showSystemMenu）。 */
        if (pos.x >= 4 && pos.x < 14 && pos.y >= 4 && pos.y < 14 &&
            XMdiSubWindow_systemMenu(sw)) {
            XMdiSubWindow_showSystemMenu(sw);
            XEvent_accept(event);
            return;
        }
        if (xmdi_subWindowHasCloseButton(sw) &&
            xmdi_subWindowButtonRect(sw, true, &btn) &&
            pos.x >= btn.x && pos.x < btn.x + btn.width &&
            pos.y >= btn.y && pos.y < btn.y + btn.height) {
            xmdi_closeFromTitleBar(sw);
            XEvent_accept(event);
            return;
        }
        if (xmdi_subWindowHasMinButton(sw) &&
            xmdi_subWindowButtonRect(sw, false, &btn) &&
            pos.x >= btn.x && pos.x < btn.x + btn.width &&
            pos.y >= btn.y && pos.y < btn.y + btn.height) {
            XWidget_showMinimized((XWidget*)sw);
            XEvent_accept(event);
            return;
        }
    }
    /* 操作被当前状态禁用时不动（对标 qmdisubwindow.cpp:3163-3175 的
     * `if (d->resizeEnabled || d->moveEnabled)`：禁用即不建会话）。 */
    if (operation == XMdiSubWindowEdgeOperation_Move) {
        if (!xmdi_subWindowMoveEnabled(sw)) {
            XEvent_accept(event); /* 已激活，但不再移动。 */
            return;
        }
    } else if (!xmdi_subWindowResizeEnabled(sw)) {
        XEvent_accept(event);
        return;
    }
    /* 记录会话基线（对标 qmdisubwindow.cpp:3167-3172：
     * updateCursor + mousePressPosition = mapToParent(pos) +
     * oldGeometry = geometry()）。锚点存**父区局部**（Qt 同域）；移动
     * 增量用全局坐标差值算（局部坐标随命中接收者改写不可靠），钳位公式
     * 用父区局部量。光标立即按操作刷新（Qt 此处 updateCursor 同义）。 */
    xmdi_applyOperationCursor(sw, operation);
    g_mdiSession.m_active = true;
    g_mdiSession.m_window = sw;
    g_mdiSession.m_operation = operation;
    g_mdiSession.m_startGeometry = XWidget_geometry(self);
    g_mdiSession.m_pressLocal = pos;
    /* m_pressCursor = 父区局部光标锚点（Qt mapToParent(pos) 同域）=
     * 旧几何原点 + 本窗局部按下点；全局按下点另存一份供每步差值用。 */
    g_mdiSession.m_pressCursor.x = g_mdiSession.m_startGeometry.x + pos.x;
    g_mdiSession.m_pressCursor.y = g_mdiSession.m_startGeometry.y + pos.y;
    g_mdiSession.m_pressOriginGlobal = XMouseEvent_globalPosition(me);
    /* 对标 Qt 按压建立的隐式抓取（qt_button_down 后续 move/release
     * 直达按下控件）：移动/缩放中光标必然越出命中带，显式抓取保证后续
     * 移动/释放仍送达本子窗（同 XSizeGrip.c:133 / XSplitter.c:456）。 */
    XWidget_grabMouse(self);
    XEvent_accept(event);
}

/** @brief 取一次会话的「父区局部按下点」（对标 mousePressPosition，
 *         qmdisubwindow.cpp:3169 `mapToParent(pos)`）。
 * @details Qt 把按下点存成父区坐标后，setNewGeometry 里所有钳位公式都写
 *          `pos - mousePressPosition` 与 `mousePressPosition - oldGeometry`。
 *          本实现按同一口径换算：父区局部 = 子窗几何原点 + 本窗局部。
 * @param session 会话；不为 NULL。
 * @param out 输出父区局部坐标。
 * @return 无返回值。 */
static void xmdi_sessionPressPosition(const XmdiPointerSession* session,
                                      XPoint* out)
{
    if (!out) return;
    XPoint_init(out, 0, 0);
    if (!session) return;
    out->x = session->m_startGeometry.x + session->m_pressLocal.x;
    out->y = session->m_startGeometry.y + session->m_pressLocal.y;
}

/** @brief 子窗的内部最小尺寸（对标 QMdiSubWindowPrivate::
 *         internalMinimumSize，qmdisubwindow.cpp:1105-1107）。
 * @details Qt 原文：
 *          `internalMinimumSize = (!q->isMinimized() && !q->minimumSize().isNull())
 *                                 ? q->minimumSize() : q->minimumSizeHint();`
 *          ——即「用户显式设过 minimumSize 就用它，否则用**最小尺寸提示**」。
 *          这一分支是缩放下限的唯一正确来源（`setNewGeometry(QRect*)` 用
 *          `size.expandedTo(internalMinimumSize)`，qmdisubwindow_p.h:278-285）：
 *          直接用 `XWidget_minimumSize` 会拿到 0，缩放可把子窗压成 0×0
 *          （实测：右下角狠拖后窗变 0×0、chrome 全丢）。
 * @param sw 目标子窗口；不为 NULL。
 * @param out 输出尺寸。
 * @return 无返回值。 */
static void xmdi_internalMinimumSize(const XMdiSubWindow* sw, XSize* out)
{
    XSize user;
    if (!out) return;
    XSize_init(out, 0, 0);
    if (!sw) return;
    user = XWidget_minimumSize((const XWidget*)sw);
    if (!XWidget_isMinimized((const XWidget*)sw) &&
        (user.width > 0 || user.height > 0)) {
        *out = user;
        return;
    }
    *out = XWidget_minimumSizeHint_base((const XWidget*)sw);
}

/** @brief 一步移动/缩放的新几何（对标 QMdiSubWindowPrivate::setNewGeometry，
 *         qmdisubwindow.cpp:1151-1213）。
 * @details 逐分支对标：
 *          ① 钳位（:1160-1189）：AllowOutsideArea 关时
 *             - 移动：posX ∈ [BoundaryMargin, parentW-BoundaryMargin]、
 *               posY ≤ parentH-BoundaryMargin；本实现纵向上界取严为
 *               parentH-标题带高（同既有拖拽会话注记：本框架视口无滚动
 *               回捞路径）；
 *             - 缩放：反向（左/上边）时 `posX ≥ 按下点 - oldGeometry.x`
 *               （即宽度不小于 0）；正向（右/下边）时
 *               `posX ≤ parentW - (oldGeometry.x+width - 按下点.x)`。
 *          ② 位移分量（:1192-1201）：`getMoveDeltaComponent` —— 带 HResize
 *             的 Move 分量夹到 `[width-maxWidth, width-minSize.width]`
 *             （角缩放时移动量不得把尺寸压到最小以下）。
 *          ③ 尺寸分量（:1204-1210）：`getResizeDeltaComponent` —— 反向位取
 *             负号（左/上边拖拽等价于「原点前移 + 尺寸增”）。
 *          ④ 尺寸下限（qmdisubwindow_p.h:278-285 setNewGeometry(QRect*)）：
 *             `size.expandedTo(internalMinimumSize)`。
 * @param sw 会话子窗。
 * @param operation 操作码。
 * @param pressPos 父区局部的按下点。
 * @param oldGeom 会话起始几何。
 * @param cursorPos 本步指针的父区局部坐标。
 * @param out 输出新几何（父区局部）。
 * @return 无返回值。
 */
static void xmdi_sessionGeometry(XMdiSubWindow* sw, int operation,
                                 const XPoint* pressPos, const XRect* oldGeom,
                                 const XPoint* cursorPos, XRect* out)
{
    uint32_t cflags;
    int posX;
    int posY;
    XWidget* parent;
    int parentW;
    int parentH;
    bool restrictH;
    bool restrictV;
    int newX;
    int newY;
    int newW;
    int newH;
    XSize minSize;
    XSize maxSize;
    if (!out) return;
    /* 先写零矩形兜底（复核意见⑪）：防御路径全部 return 时 *out 也有
     * 确定值，调用方（键盘/鼠标会话）不会读到未初始化栈内存。 */
    XRect_init(out, 0, 0, 0, 0);
    if (!sw || !pressPos || !oldGeom || !cursorPos) {
        if (oldGeom) *out = *oldGeom;
        return;
    }
    cflags = xmdi_operationFlags(operation);
    posX = cursorPos->x;
    posY = cursorPos->y;
    parent = XWidget_parentWidget((XWidget*)sw);
    parentW = parent ? XWidget_width(parent) : 0;
    parentH = parent ? XWidget_height(parent) : 0;
    /* AllowOutsideAreaHorizontally/Vertically 默认关（与 Qt 同：
     * testOption 取反，qmdisubwindow.cpp:1160-1161）。 */
    restrictH = !XMdiSubWindow_testOption(
        sw, XMdiSubWindowOption_AllowOutsideAreaHorizontally);
    restrictV = !XMdiSubWindow_testOption(
        sw, XMdiSubWindowOption_AllowOutsideAreaVertically);
    if (parent && (restrictH || restrictV)) {
        if (restrictV &&
            ((cflags & XMDI_OP_VRESIZE_REVERSE) != 0 ||
             operation == XMdiSubWindowEdgeOperation_Move)) {
            /* Qt: posY = qMin(qMax(mousePressPosition.y() - oldGeometry.y(),
             *                       posY), parentRect.height() - BoundaryMargin)
             * —— 上界本轮取 parentH - 标题带高（见本函数头注）。 */
            int low = pressPos->y - oldGeom->y;
            int high = parentH - XMDI_SUBWINDOW_TITLEBAR_HEIGHT;
            posY = xmdi_dragClamp(posY, low, high);
        }
        if (operation == XMdiSubWindowEdgeOperation_Move) {
            if (restrictH)
                posX = xmdi_dragClamp(posX, XMDI_SUBWINDOW_BOUNDARY_MARGIN,
                                      parentW - XMDI_SUBWINDOW_BOUNDARY_MARGIN);
            if (restrictV)
                posY = posY < parentH
                           ? posY
                           : parentH - XMDI_SUBWINDOW_BOUNDARY_MARGIN;
        } else {
            if (restrictH) {
                if (cflags & XMDI_OP_HRESIZE_REVERSE)
                    posX = xmdi_dragClamp(posX, pressPos->x - oldGeom->x,
                                          posX);
                else {
                    int high = parentW - (oldGeom->x + oldGeom->width -
                                          pressPos->x);
                    posX = posX < high ? posX : high;
                }
            }
            if (restrictV && !(cflags & XMDI_OP_VRESIZE_REVERSE)) {
                int high = parentH - (oldGeom->y + oldGeom->height -
                                      pressPos->y);
                posY = posY < high ? posY : high;
            }
        }
    }
    newX = oldGeom->x;
    newY = oldGeom->y;
    newW = oldGeom->width;
    newH = oldGeom->height;
    minSize = XWidget_minimumSize((const XWidget*)sw);
    maxSize = XWidget_maximumSize((const XWidget*)sw);
    /* 缩放下限取 internalMinimumSize（Qt :1105-1107 的分支：用户
     * minimumSize 非空则用它，否则用 minimumSizeHint）。移动分量夹取
     * （getMoveDeltaComponent）也用它——Qt 原式
     * `oldGeometry.width() - internalMinimumSize.width()`。 */
    xmdi_internalMinimumSize(sw, &minSize);
    if (cflags & (XMDI_OP_HMOVE | XMDI_OP_VMOVE)) {
        /* getMoveDeltaComponent（qmdisubwindow.cpp:178-187）：带 reverse
         * 的 move 分量把增量夹进「不得越过最大/最小尺寸」的区间。 */
        int dx = posX - pressPos->x;
        int dy = posY - pressPos->y;
        if (cflags & XMDI_OP_HMOVE) {
            if (cflags & XMDI_OP_HRESIZE) {
                int maxDelta = oldGeom->width - minSize.width;
                int minDelta = oldGeom->width - maxSize.width;
                if (dx > 0) dx = dx < maxDelta ? dx : maxDelta;
                else dx = dx > minDelta ? dx : minDelta;
            }
            newX = oldGeom->x + dx;
        }
        if (cflags & XMDI_OP_VMOVE) {
            if (cflags & XMDI_OP_VRESIZE) {
                int maxDelta = oldGeom->height - minSize.height;
                int minDelta = oldGeom->height - maxSize.height;
                if (dy > 0) dy = dy < maxDelta ? dy : maxDelta;
                else dy = dy > minDelta ? dy : minDelta;
            }
            newY = oldGeom->y + dy;
        }
    }
    if (cflags & (XMDI_OP_HRESIZE | XMDI_OP_VRESIZE)) {
        /* getResizeDeltaComponent（qmdisubwindow.cpp:189-199）：反向位取负。 */
        int dx = (cflags & XMDI_OP_HRESIZE)
                     ? ((cflags & XMDI_OP_HRESIZE_REVERSE)
                            ? -(posX - pressPos->x)
                            : (posX - pressPos->x))
                     : 0;
        int dy = (cflags & XMDI_OP_VRESIZE)
                     ? ((cflags & XMDI_OP_VRESIZE_REVERSE)
                            ? -(posY - pressPos->y)
                            : (posY - pressPos->y))
                     : 0;
        newW = oldGeom->width + dx;
        newH = oldGeom->height + dy;
    }
    /* setNewGeometry(QRect*)：size.expandedTo(internalMinimumSize)。 */
    if (newW < minSize.width) newW = minSize.width;
    if (newH < minSize.height) newH = minSize.height;
    if (maxSize.width > 0 && newW > maxSize.width) newW = maxSize.width;
    if (maxSize.height > 0 && newH > maxSize.height) newH = maxSize.height;
    XRect_init(out, newX, newY, newW, newH);
}

static void VX_mdiSubWindow_mouseMoveEvent(XWidget* self, XEvent* event)
{
    XMdiSubWindow* sw = (XMdiSubWindow*)self;
    XMouseEvent* me = (XMouseEvent*)event;
    XWidget* parent;
    XPoint cursorPos;
    XPoint pressPos;
    XRect newRect;
    XPoint pos;
    int operation;
    if (!sw || !event || XEvent_type(event) != XEVENT_TYPE_MOUSE_MOVE)
        return;
    if (!g_mdiSession.m_active || g_mdiSession.m_window != sw ||
        XMouseEvent_buttons(me) != XMouseButton_LeftButton) {
        /* 键盘交互模式进行中：操作/光标不随物理 MOVE 重算（对标
         * qmdisubwindow.cpp:3295-3301 `if ((buttons & LeftButton) ||
         * d->isInInteractiveMode)` 提前 return——尾段的 getOperation +
         * updateCursor 只在非交互模式执行；交互光标由 enter 时
         * updateCursor 设定，离开由 leaveInteractiveMode 复位。缺了本闸：
         * 键盘 &Size 交互中指针滑过内容区会把操作光标刷成箭头
         * （复核返工订正，锁 interactiveModeCursorStableOnHover）。
         * 基类链同 Press：XClass_Parent 直呼（_base 助手会重入
         * 本重载，复扫-5 悬停栈溢出实证）。 */
        if (g_mdiInteractive.m_active && g_mdiInteractive.m_window == sw) {
            XClass_Parent(XWidget, EXWidget_MouseMoveEvent,
                          void (*)(XWidget*, XEvent*))((XWidget*)self, event);
            return;
        }
        /* 无会话：只做 hover 光标刷新，然后交基类链（对标
         * qmdisubwindow.cpp:3290-3313：非按压时按 getOperation 更新光标、
         * 不移动）。 */
        pos = XMouseEvent_position(me);
        operation = xmdi_edgeOperationAtPosition(sw, pos.x, pos.y);
        if (operation != XMdiSubWindowEdgeOperation_Move &&
            xmdi_isResizeOperation(operation) &&
            !xmdi_subWindowResizeEnabled(sw))
            operation = XMdiSubWindowEdgeOperation_None;
        else if (operation == XMdiSubWindowEdgeOperation_Move &&
                 !xmdi_subWindowMoveEnabled(sw))
            operation = XMdiSubWindowEdgeOperation_None;
        xmdi_applyOperationCursor(sw, operation);
        XClass_Parent(XWidget, EXWidget_MouseMoveEvent,
                      void (*)(XWidget*, XEvent*))((XWidget*)self, event);
        return;
    }
    parent = XWidget_parentWidget(self);
    if (!parent) {
        XEvent_accept(event);
        return;
    }
    /* 本步指针的**父区局部**坐标：起始锚点（父区局部）+ 全局位移增量。
     * 与 Qt 的 `parentWidget()->mapFromGlobal(globalPosition())`
     * （qmdisubwindow.cpp:3302-3304）等价：本框架不做父区旋转补偿，故
     * 「按下点父区坐标 + 全局差值」与「当前位置 mapFromGlobal」同值，
     * 且不依赖命中接收者改写后的局部坐标。 */
    {
        XPoint np = XMouseEvent_globalPosition(me);
        xmdi_sessionPressPosition(&g_mdiSession, &pressPos);
        cursorPos.x = g_mdiSession.m_pressCursor.x +
                      (np.x - g_mdiSession.m_pressOriginGlobal.x);
        cursorPos.y = g_mdiSession.m_pressCursor.y +
                      (np.y - g_mdiSession.m_pressOriginGlobal.y);
    }
    xmdi_sessionGeometry(sw, g_mdiSession.m_operation, &pressPos,
                         &g_mdiSession.m_startGeometry, &cursorPos, &newRect);
    /* 落几何 + 脏区全覆盖（setGeometry 内联的 old∪new 包围盒之外再补一次
     * 扫掠带，第八轮 FAIL① 台阶缺角根修；缩放同时改位置与尺寸，扫掠带
     * 由同一助手按新旧矩形差生成）。 */
    {
        XRect oldRect = XWidget_geometry(self);
        XRect applied;
        XWidget_setGeometryRect(self, &newRect);
        applied = XWidget_geometry(self);
        if (applied.x != oldRect.x || applied.y != oldRect.y ||
            applied.width != oldRect.width || applied.height != oldRect.height)
            xmdi_dragUpdateCoverage(parent, &oldRect, &applied);
    }
    XEvent_accept(event);
}

static void VX_mdiSubWindow_mouseReleaseEvent(XWidget* self, XEvent* event)
{
    XMdiSubWindow* sw = (XMdiSubWindow*)self;
    XMouseEvent* me = (XMouseEvent*)event;
    if (!sw || !event ||
        XEvent_type(event) != XEVENT_TYPE_MOUSE_BUTTON_RELEASE) return;
    if (XMouseEvent_button(me) != XMouseButton_LeftButton) {
        /* 对标 qmdisubwindow.cpp:3241 非左键交基类默认链（XClass_Parent
         * 直呼，同 Press/Move 防重入口径）。 */
        XClass_Parent(XWidget, EXWidget_MouseReleaseEvent,
                      void (*)(XWidget*, XEvent*))((XWidget*)self, event);
        return;
    }
    /* 结束会话并解除抓取（对标 mouseReleaseEvent:3247-3256：
     * oldGeometry = geometry() → currentOperation = getOperation(pos) →
     * updateCursor()）。光标按**释放点**重算：拖到边缘停下就显示该边
     * 光标，回到内部即恢复箭头（Qt 在 release 里重算 currentOperation
     * 的同义行为）。缩放结束后几何已落，无需额外收尾。 */
    if (g_mdiSession.m_window == sw) {
        XPoint pos = XMouseEvent_position(me);
        int operation;
        g_mdiSession.m_window = NULL;
        g_mdiSession.m_active = false;
        g_mdiSession.m_operation = XMdiSubWindowEdgeOperation_None;
        XWidget_releaseMouse(self);
        operation = xmdi_edgeOperationAtPosition(sw, pos.x, pos.y);
        if (operation == XMdiSubWindowEdgeOperation_Move)
            operation = xmdi_subWindowMoveEnabled(sw)
                            ? operation : XMdiSubWindowEdgeOperation_None;
        else if (xmdi_isResizeOperation(operation) &&
                 !xmdi_subWindowResizeEnabled(sw))
            operation = XMdiSubWindowEdgeOperation_None;
        xmdi_applyOperationCursor(sw, operation);
    } else {
        XWidget_releaseMouse(self);
    }
    XEvent_accept(event);
}

/** @brief 最大化呈现期间重确保窗口状态位（对标
 *         QMdiSubWindowPrivate::ensureWindowState(Qt::WindowMaximized)，
 *         qmdisubwindow.cpp:1667-1694）。
 * @details setGeometry 会清 Maximized/FullScreen 位（XWidget.c:6757-6762），
 *          Qt 在 resizeEvent/moveEvent 尾段（:3041-3042/3079-3080）用
 *          `if (d->isMaximizeMode) d->ensureWindowState(Qt::WindowMaximized)`
 *          把位补回，语义 = `overrideWindowState(current | Maximized 且清
 *          Minimized/FullScreen)`——走 **override**（不派发
 *          WindowStateChange、无递归）。缺了它：父区 resizeEvent 重铺
 *          最大化子窗（qmdiarea.cpp:2296-2320 的 child->resize 分支）后
 *          状态位被清 ⇒ 之后 showNormal 因「新旧态相同」早退
 *          （XWidget.c:8995）⇒ changeEvent 不派发 ⇒ 还原几何永不执行。
 * @param sw 目标子窗口；NULL 直接返回。
 * @return 无返回值。 */
static void xmdi_ensureMaximizedWindowState(XMdiSubWindow* sw)
{
    XWindowStates states;
    if (!sw) return;
    states = (XWindowStates)(
        (XWidget_windowState((const XWidget*)sw) &
         ~(XWindowStates)(XWindowState_Minimized | XWindowState_FullScreen |
                          XWindowState_NoState)) |
        (XWindowStates)XWindowState_Maximized);
    XWidget_overrideWindowState((XWidget*)sw, states);
}

/** @brief 鼠标进入：命中操作复位 + 光标重算（对标 QMdiSubWindow::event
 *         的 QEvent::Enter 分支，qmdisubwindow.cpp:2700-2703：
 *         `d->currentOperation = None; d->updateCursor();`）。
 * @details 意义：上一子窗缩放带设置的光标形状随指针跨窗迁移残留时，
 *          Enter 把操作复位并恢复箭头（拖拽会话进行中不介入——抓取
 *          期间 Enter 本不该派发，防御兜底）。 */
static void VX_mdiSubWindow_enterEvent(XWidget* self, XEvent* event)
{
    XMdiSubWindow* sw = (XMdiSubWindow*)self;
    (void)event;
    if (!sw) return;
    if (g_mdiSession.m_active && g_mdiSession.m_window == sw) return;
    xmdi_applyOperationCursor(sw, XMdiSubWindowEdgeOperation_None);
}

/** @brief 鼠标离开：光标复位（框架补齐，Qt 无对应分支——QMdiSubWindow
 *         的 event() 不处理 QEvent::Leave；Qt 光标按控件作用域、离开
 *         即失效，而 XWidget_setCursor 落在**顶层原生窗**
 *         （XWidget.c:11142-11145），同顶层的其它控件会被残留的缩放
 *         光标覆盖 ⇒ 离开时按 Enter 同口径清形。拖拽会话进行中不介入）。 */
static void VX_mdiSubWindow_leaveEvent(XWidget* self, XEvent* event)
{
    XMdiSubWindow* sw = (XMdiSubWindow*)self;
    (void)event;
    if (!sw) return;
    if (g_mdiSession.m_active && g_mdiSession.m_window == sw) return;
    xmdi_applyOperationCursor(sw, XMdiSubWindowEdgeOperation_None);
}

/** @brief 尺寸变化时内容件随框布局（对标 QMdiSubWindow::resizeEvent，
 *         qmdisubwindow.cpp:3026：缩放后内容几何与框保持一致）。
 * @details 此前子窗 resize 后内容件保持 creation 尺寸——chrome 窗框
 *          绘制后表现为框大内容小、框线内残白。拖动只改位置不发
 *          RESIZE（recomputeGeometry 仅 sizeChanged 派发），跟手链
 *          不受本槽影响。链式口径同 XSplitter resizeEvent（不链基
 *          类，XWidget 默认 Resize 槽为空实现）。
 *          最大化呈现期间尾段重确保状态位（Qt :3041-3042 同款，见
 *          xmdi_ensureMaximizedWindowState 头注）——父区 resize 的
 *          重铺分支（VX_mdiArea_resizeEvent 的 setGeometry）与任何其它
 *          几何来源都会经过这里。 */
static void VX_mdiSubWindow_resizeEvent(XWidget* self, XEvent* event)
{
    XMdiSubWindow* sw = (XMdiSubWindow*)self;
    XRect r;
    if (!sw || !event || XEvent_type(event) != XEVENT_TYPE_RESIZE) return;
    if (sw->m_widget) {
        xmdi_subWindowContentRect(sw, &r);
        XWidget_setGeometryRect(sw->m_widget, &r);
    }
    if (sw->m_maximizedMode) xmdi_ensureMaximizedWindowState(sw);
}

/** @brief 键盘交互模式的方向键处理（对标 QMdiSubWindow::keyPressEvent，
 *         qmdisubwindow.cpp:3316-3375）。
 * @details Qt 原文要点：
 *          ① 非交互模式或无父 ⇒ `keyEvent->ignore(); return;`；
 *          ② 方向键按 Shift 取 pageStep、否则 singleStep
 *             （:3325-3354 的四分支表）；
 *          ③ Escape / Return / Enter ⇒ `leaveInteractiveMode()` 后**直接
 *             return**（不 ignore、不再下发位移）；
 *          ④ 其余键 ignore；
 *          ⑤ 位移经 `setNewGeometry` 落几何，且**实际增量与请求不符时**
 *             用实际增量回写指针位置（:3360-3373 的 actualDelta 修正）。
 *
 *          本实现把 ⑤ 的「指针位置」换成会话里的**光标锚点**
 *          （g_mdiSession.m_pressCursor，父区局部）：每次成功一步后按
 *          实际增量修正它，于是连续按键逐格累进、且被最小尺寸/父区钳住后
 *          不再堆积无效增量（与 Qt「把光标挪到实际位置」的意图一致，但
 *          不劫持用户指针）。
 * @param self 目标控件。
 * @param event 键盘事件。
 * @return 无返回值。 */
static void VX_mdiSubWindow_keyPressEvent(XWidget* self, XEvent* event)
{
    XMdiSubWindow* sw = (XMdiSubWindow*)self;
    XKeyEvent* ke = (XKeyEvent*)event;
    int key;
    XKeyboardModifiers mods;
    int singleStep;
    int pageStep;
    int step;
    int dx = 0;
    int dy = 0;
    XWidget* parent;
    XPoint pressPos;
    XPoint cursorPos;
    XRect newRect;
    if (!sw || !event || XEvent_type(event) != XEVENT_TYPE_KEY_PRESS) return;
    if (!g_mdiInteractive.m_active || g_mdiInteractive.m_window != sw ||
        !XWidget_parentWidget(self)) {
        /* ① 非交互模式：交基类链（Qt 的 keyEvent->ignore() 同义）。 */
        XClass_Parent(XWidget, EXWidget_KeyPressEvent,
                      void (*)(XWidget*, XEvent*))((XWidget*)self, event);
        return;
    }
    key = XKeyEvent_key(ke);
    mods = XKeyEvent_modifiers(ke);
    singleStep = sw->m_keyboardSingleStep;
    pageStep = sw->m_keyboardPageStep;
    /* ② 步长（Shift=pageStep，否则 singleStep；0/负值按 Qt「0=不动、
     * 负=反向」原样生效——本函数不做下界检查，:2402-2407 同）。 */
    step = (mods & XKeyboardModifier_ShiftModifier) ? pageStep : singleStep;
    switch (key) {
    case XKey_Right: dx = step; break;
    case XKey_Left:  dx = -step; break;
    case XKey_Down:  dy = step; break;
    case XKey_Up:    dy = -step; break;
    case XKey_Escape:
    case XKey_Return:
    case XKey_Enter:
        /* ③ 退出交互模式（Qt :3350-3353）。 */
        xmdi_leaveInteractiveMode(sw);
        XEvent_accept(event);
        return;
    default:
        /* ④ 其余键 ignore（交基类链，与 Qt keyEvent->ignore() 同义）。 */
        XClass_Parent(XWidget, EXWidget_KeyPressEvent,
                      void (*)(XWidget*, XEvent*))((XWidget*)self, event);
        return;
    }
    if (dx == 0 && dy == 0) {
        XEvent_accept(event); /* step=0：消费但不动作（Qt 同）。 */
        return;
    }
    parent = XWidget_parentWidget(self);
    /* 键盘路径按 Qt 的语义推进「光标位置」（qmdisubwindow.cpp:3364-3366
     * `cursor().pos() + delta` → `mapFromGlobal`）：会话锚点
     * m_pressCursor 就是**父区局部的光标位置**，按 delta 前移后与固定
     * 锚点（m_startGeometry + m_pressLocal）求差即得**累计**位移。
     * 只前移局部锚点、每次重算起点会把累计量吃掉（实测第二键位移只剩
     * 15、第三键被拉回原点）。m_pressLocal / m_startGeometry 保持进入
     * 模式时的原值（对应 Qt 的 mousePressPosition / oldGeometry ——
     * 一次交互中不刷新）。 */
    g_mdiSession.m_pressCursor.x += dx;
    g_mdiSession.m_pressCursor.y += dy;
    xmdi_sessionPressPosition(&g_mdiSession, &pressPos);
    cursorPos = g_mdiSession.m_pressCursor;
    xmdi_sessionGeometry(sw, g_mdiSession.m_operation, &pressPos,
                         &g_mdiSession.m_startGeometry, &cursorPos, &newRect);
    {
        XRect oldRect = XWidget_geometry(self);
        XRect applied;
        XWidget_setGeometryRect(self, &newRect);
        applied = XWidget_geometry(self);
        /* ⑤ 实际增量修正（Qt :3360-3373 的 actualDelta 回写）：
         *   `newPosition += (actualDelta - delta); cursor().setPos(...)`。
         *   被最小尺寸/父区钳住后，把「光标位置」拉回到实际落点，后续按键
         *   不再堆积无效增量。 */
        {
            int actualDx;
            int actualDy;
            if (g_mdiSession.m_operation == XMdiSubWindowEdgeOperation_Move) {
                actualDx = applied.x - oldRect.x;
                actualDy = applied.y - oldRect.y;
            } else if (XWidget_isLeftToRight((const XWidget*)sw)) {
                actualDx = applied.width - oldRect.width;
                actualDy = applied.height - oldRect.height;
            } else {
                /* RTL 缩放：原点侧被推/拉，实际位移按 x 差量算
                 * （Qt :3385-3392：
                 *   `int dx = isLeftToRight() ? currentGeometry.width()
                 *       - oldGeometry.width() : currentGeometry.x()
                 *       - oldGeometry.x();`）。 */
                actualDx = applied.x - oldRect.x;
                actualDy = applied.height - oldRect.height;
            }
            g_mdiSession.m_pressCursor.x += actualDx - dx;
            g_mdiSession.m_pressCursor.y += actualDy - dy;
        }
        if (parent && (applied.x != oldRect.x || applied.y != oldRect.y ||
                       applied.width != oldRect.width ||
                       applied.height != oldRect.height))
            xmdi_dragUpdateCoverage(parent, &oldRect, &applied);
    }
    XEvent_accept(event);
}

/** @brief 折叠/最小化进入前记录恢复几何（对标 restoreSize + oldGeometry，
 *         qmdisubwindow.cpp:2586-2589/1246-1288）。
 * @details 只在首次进入「非正常态」时记录：折叠与最小化互斥切换时
 *          （例如缩小态直接最小化）不覆盖最初的正常几何。
 * @param sw 目标子窗口；可为 NULL。
 * @return 无返回值。 */
static void xmdi_saveRestoreGeometry(XMdiSubWindow* sw)
{
    if (!sw || sw->m_restoreValid) return;
    sw->m_restoreGeometry = XWidget_geometry((XWidget*)sw);
    sw->m_restoreValid = 1;
}

/** @brief 按记录的正常几何恢复（对标 setNormalMode，qmdisubwindow.cpp:
 *         1246-1288）。
 * @param sw 目标子窗口；可为 NULL。
 * @return 无返回值。 */
static void xmdi_restoreNormalGeometry(XMdiSubWindow* sw)
{
    if (!sw) return;
    if (sw->m_restoreValid) {
        XWidget_setGeometryRect((XWidget*)sw, &sw->m_restoreGeometry);
        sw->m_restoreValid = 0;
    }
}

/** @brief 折叠/最小化时隐藏内容件（对标 showShaded 的
 *         `baseWidget->hide()` 分支，qmdisubwindow.cpp:2610-2616：
 *         只隐藏「未被用户隐藏」的内容件，并记 isWidgetHiddenByUs）。
 * @param sw 目标子窗口；可为 NULL。
 * @return 无返回值。 */
static void xmdi_hideContent(XMdiSubWindow* sw)
{
    if (!sw || !sw->m_widget) return;
    if (XWidget_isHidden(sw->m_widget)) return;
    XWidget_hide(sw->m_widget);
    sw->m_contentHiddenByUs = 1;
}

/** @brief 恢复正常态时复显被我们隐藏的内容件（对标 setNormalMode 的
 *         `if (baseWidget && isWidgetHiddenByUs) baseWidget->show();`）。
 * @param sw 目标子窗口；可为 NULL。
 * @return 无返回值。 */
static void xmdi_showContent(XMdiSubWindow* sw)
{
    XRect r;
    if (!sw || !sw->m_widget) return;
    if (!sw->m_contentHiddenByUs) return;
    XWidget_show(sw->m_widget);
    sw->m_contentHiddenByUs = 0;
    xmdi_subWindowContentRect(sw, &r);
    XWidget_setGeometryRect(sw->m_widget, &r);
}

/**
 * @brief      进入折叠态（showShaded 的主体；对标 qmdisubwindow.cpp:
 *             2563-2646）。
 * @details    记录恢复几何 → 隐藏内容件 → 高度缩到标题带、宽度保持
 *             （Qt 为 `resize(d->internalMinimumSize)`，宽度也是 internal
 *             minimum；本框架宽度保持隐藏前值，标题条观感一致）→ 把
 *             基类窗口状态置为 WindowMinimized（Qt ensureWindowState
 *             (WindowMinimized)，本机探针 showShaded 后 windowState=0x9
 *             = WindowActive|WindowMinimized）→ 发本类 windowStateChanged
 *             （旧/新 = 本类状态位编码，Shaded 位见
 *             XMdiSubWindowState_Shaded）。
 * @param sw 目标子窗口；可为 NULL。
 * @return 无返回值。
 */
static void xmdi_enterShaded(XMdiSubWindow* sw)
{
    int oldState;
    int w;
    if (!sw) return;
    oldState = sw->m_state;
    xmdi_saveRestoreGeometry(sw);
    sw->m_shaded = 1;
    sw->m_state |= XMdiSubWindowState_Shaded;
    xmdi_hideContent(sw);
    w = XWidget_width((XWidget*)sw);
    XWidget_resize((XWidget*)sw, w, XMDI_SUBWINDOW_TITLEBAR_HEIGHT);
    /* Qt：showShaded 里 ensureWindowState(WindowMinimized)。基类状态变化
     * 会派发 WindowStateChange → 本类 changeEvent 见 m_shaded 已置位，
     * 不再进入最小化呈现。 */
    XWidget_setWindowState((XWidget*)sw,
                           (XWindowStates)((XWidget_windowState((XWidget*)sw) &
                                            ~(XWindowStates)XWindowState_Active) |
                                           (XWindowStates)XWindowState_Minimized));
    XWidget_update((XWidget*)sw);
    if (oldState != sw->m_state)
        xmdi_emitStateChanged(sw, oldState, sw->m_state);
}

/**
 * @brief      退出折叠态（对标 setNormalMode 的取消折叠分支，
 *             qmdisubwindow.cpp:1246-1288）。
 * @details    清除 Shaded 位、按记录几何恢复、复显内容件，并广播
 *             windowStateChanged。
 * @param sw 目标子窗口；可为 NULL。
 * @return 无返回值。
 */
static void xmdi_exitShaded(XMdiSubWindow* sw)
{
    int oldState;
    if (!sw || !sw->m_shaded) return;
    oldState = sw->m_state;
    sw->m_shaded = 0;
    sw->m_state &= (uint8_t)~XMdiSubWindowState_Shaded;
    xmdi_restoreNormalGeometry(sw);
    xmdi_showContent(sw);
    XWidget_update((XWidget*)sw);
    if (oldState != sw->m_state)
        xmdi_emitStateChanged(sw, oldState, sw->m_state);
}

/**
 * @brief      进入最小化呈现（对标 QMdiSubWindowPrivate::setMinimizeMode，
 *             qmdisubwindow.cpp:1218-1246）。
 * @details    Qt 把最小化子窗缩成标题带一条并摆到区域左下（本机探针：
 *             200x150 @ (50,50) → 196x24 @ (0,276)，仍可见、仍可激活）。
 *             本实现记录恢复几何、隐藏内容件、保持宽度并把窗口贴到父区
 *             底部（高度=标题带）。与折叠的区别是 isShaded() 保持 false
 *             （Qt 的 isShadeRequestFromMinimizeMode）。
 * @param sw 目标子窗口；可为 NULL。
 * @return 无返回值。
 */
static void xmdi_enterMinimized(XMdiSubWindow* sw)
{
    XWidget* parent;
    XSize icon;
    int w;
    int h;
    int ph;
    if (!sw || sw->m_minimized) return;
    parent = XWidget_parentWidget((XWidget*)sw);
    if (!parent) return;
    xmdi_saveRestoreGeometry(sw);
    sw->m_minimized = 1;
    xmdi_hideContent(sw);
    /* 图标尺寸取 iconSize()（对标 setMinimizeMode → showShaded →
     * `resize(d->internalMinimumSize)`，而 internalMinimumSize 在
     * updateGeometryConstraints 里由 minimumSizeHint() 得出，
     * minimumSizeHint 的最小化分支直接返回 d->iconSize()：
     * qmdisubwindow.cpp:3469-3471、2117-2123）。Qt 探针
     * 200x150@(50,50) → 196x24 @(0,276)：宽 = PM_MdiSubWindowMinimizedWidth
     * (196)、高 = 标题带高(24)。本框架标题带 20，故高 20、宽取样式 PM。
     * 取不到 iconSize（(-1,-1)）时回落旧口径：宽保持、高=标题带。 */
    xmdi_iconSize(sw, &icon);
    if (icon.width > 0 && icon.height > 0) {
        w = icon.width;
        h = icon.height;
    } else {
        w = XWidget_width((XWidget*)sw);
        h = XMDI_SUBWINDOW_TITLEBAR_HEIGHT;
    }
    ph = XWidget_height(parent);
    /* 先贴底部（图标行位置）；紧接着 arrangeMinimizedSubWindows 会整行
     * 重排（qmdiarea.cpp:939-943），此处贴底只保证单窗场景也落到底边。 */
    XWidget_setGeometry((XWidget*)sw, 0,
                        ph > h ? ph - h : 0, w, h);
    XWidget_update((XWidget*)sw);
}

/**
 * @brief      退出最小化呈现（恢复几何并复显内容）。
 * @param sw 目标子窗口；可为 NULL。
 * @return 无返回值。 */
static void xmdi_exitMinimized(XMdiSubWindow* sw)
{
    if (!sw || !sw->m_minimized) return;
    sw->m_minimized = 0;
    xmdi_restoreNormalGeometry(sw);
    xmdi_showContent(sw);
    XWidget_update((XWidget*)sw);
}

/**
 * @brief      进入区域最大化呈现（对标 QMdiSubWindowPrivate::setMaximizeMode，
 *             qmdisubwindow.cpp:1324-1400）。
 * @details    Qt 语义（逐条对标）：
 *             ① 记录恢复几何（`if (!restoreSize.isValid()) oldGeometry =
 *                geometry(); restoreSize = oldGeometry.size();`）——本实现
 *                复用折叠/最小化同一份 m_restoreGeometry 记录（三者互斥，
 *                状态机保证同时只有一个生效）；
 *             ② 铺满父区内容矩形（`setNewGeometry(parent->contentsRect())`）
 *                ——本框架父区是视口，直接铺满视口；
 *             ③ 期间 moveEnabled/resizeEnabled 均关（:1393-1400）——由
 *                xmdi_subWindowMoveEnabled/xmdi_subWindowResizeEnabled 的
 *                m_maximizedMode 分支承载；
 *             ④ 内容件随框（resizeEvent 已挂同一助手）。
 *             与 Qt 的一处差异：Qt 在最大化态由 `drawTitleBarWhenMaximized`
 *             决定是否画标题带（`SH_Workspace_FillSpaceOnMaximize` 样式提示；
 *             QCommonStyle 缺省返回 true ⇒ 保留标题带，
 *             qcommonstyle.cpp:5457）。本框架 paintEvent 恒画 20px 标题带，
 *             与 QCommonStyle 缺省一致 ⇒ 无需额外分支。
 * @param sw 目标子窗口；可为 NULL。
 * @return 无返回值。 */
static void xmdi_enterMaximized(XMdiSubWindow* sw)
{
    XWidget* parent;
    if (!sw || sw->m_maximizedMode) return;
    parent = XWidget_parentWidget((XWidget*)sw);
    if (!parent) return;
    xmdi_saveRestoreGeometry(sw);
    /* 先置位再改几何：改几何会派发 WindowStateChange → changeEvent 会再进
     * 本函数，位已置 ⇒ 早退不递归。 */
    sw->m_maximizedMode = 1;
    xmdi_showContent(sw); /* 折叠/最小化残留的内容件隐藏先复位（Qt :1352-1358 同）。 */
    XWidget_setGeometry((XWidget*)sw, 0, 0, XWidget_width(parent),
                        XWidget_height(parent));
    /* 状态位必须**在改几何之后**重新确保（对标 qmdisubwindow.cpp:1389-1391
     * 的原文注释：「QWidget::setGeometry will reset Qt::WindowMaximized so
     * we have to update it here.」——本框架 XWidget_setGeometry 同样清
     * Maximized/FullScreen 位）。 */
    XWidget_setWindowState((XWidget*)sw,
                           (XWindowStates)((XWidget_windowState((XWidget*)sw) &
                                            ~(XWindowStates)(
                                                XWindowState_Minimized |
                                                XWindowState_FullScreen)) |
                                           (XWindowStates)XWindowState_Maximized));
    /* 最大化期间不再响应边缘命中（Qt resizeEnabled/moveEnabled=false）。 */
    xmdi_applyOperationCursor(sw, XMdiSubWindowEdgeOperation_None);
    XWidget_update((XWidget*)sw);
}

/**
 * @brief      退出区域最大化（对标 setNormalMode 的取消最大化分支，
 *             qmdisubwindow.cpp:1247-1310）。
 * @details    清 m_maximizedMode 位并按记录的几何还原（Qt 用
 *             `newGeometry.setSize(restoreSize.expandedTo(internalMinimumSize))`
 *             + `setGeometry(oldGeometry)`）。还原后光标按当前点重算由下一次
 *             移动事件自然完成。
 * @param sw 目标子窗口；可为 NULL。
 * @return 无返回值。 */
static void xmdi_exitMaximized(XMdiSubWindow* sw)
{
    if (!sw || !sw->m_maximizedMode) return;
    sw->m_maximizedMode = 0;
    xmdi_restoreNormalGeometry(sw);
    xmdi_showContent(sw);
    XWidget_update((XWidget*)sw);
}

/**
 * @brief      changeEvent：窗口状态变化驱动的折叠/最小化/最大化联动
 *             （对标 QMdiSubWindow::event 的 WindowStateChange 处理，
 *             qmdisubwindow.cpp:2760-2775；Qt 的 setWindowState(NoState)
 *             经 QWidget 状态机进入 setNormalMode 复原几何）。
 * @details    WindowStateChange 由 XWidget_setWindowState / showNormal 等
 *             入口直派。Qt 原文（qmdisubwindow.cpp:2761-2773）按
 *             「Minimized 位新置 → showMinimized；Maximized 位新置 →
 *             showMaximized；三个特殊位全清 → showNormal」三分支，本实现
 *             按同一分支序（折叠是本库对 Minimized 的第二种呈现，先判）：
 *             - 折叠态 + Minimized 位被清除 ⇒ 退出折叠（恢复几何）；
 *             - 非折叠 + Minimized 位置位 ⇒ 进入最小化呈现；
 *             - Maximized 位置位 ⇒ 进入最大化呈现；
 *             - 特殊位全清 + 处于最小化/最大化呈现 ⇒ 复原。
 *             其余事件交父类 changeEvent（不吞）。
 * @param self 目标控件。
 * @param event 事件。
 * @return 无返回值。
 */
static void VX_mdiSubWindow_changeEvent(XWidget* self, XEvent* event)
{
    XMdiSubWindow* sw = (XMdiSubWindow*)self;
    if (!sw || !event) return;
    if (XEvent_type(event) == XEVENT_TYPE_WINDOW_STATE_CHANGE) {
        XWindowStates state = XWidget_windowState(self);
        bool minimized =
            (state & (XWindowStates)XWindowState_Minimized) != 0;
        bool maximized =
            (state & (XWindowStates)XWindowState_Maximized) != 0;
        if (sw->m_shaded) {
            if (!minimized) xmdi_exitShaded(sw);
        } else if (minimized) {
            if (sw->m_maximizedMode) xmdi_exitMaximized(sw);
            xmdi_enterMinimized(sw);
            /* 新最小化的窗加入图标行并整行重排（对标
             * _q_processWindowStateChanged 的 windowMinimized 分支
             * `isSubWindowsTiled = false; arrangeMinimizedSubWindows();`，
             * qmdiarea.cpp:721-723）。 */
            {
                XMdiArea* area = XMdiSubWindow_mdiArea(sw);
                if (area) XMdiArea_arrangeIcons(area);
            }
        } else if (maximized) {
            if (sw->m_minimized) xmdi_exitMinimized(sw);
            xmdi_enterMaximized(sw);
        } else {
            bool wasMinimized = sw->m_minimized != 0;
            if (sw->m_minimized) xmdi_exitMinimized(sw);
            if (sw->m_maximizedMode) xmdi_exitMaximized(sw);
            /* 从最小化还原 ⇒ 图标行让出该格、其余图标重排（对标
             * _q_processWindowStateChanged 的 windowRestored 分支
             * `if (oldState & Qt::WindowMinimized)
             * arrangeMinimizedSubWindows();`，qmdiarea.cpp:727-729）。 */
            if (wasMinimized) {
                XMdiArea* area = XMdiSubWindow_mdiArea(sw);
                if (area) XMdiArea_arrangeIcons(area);
            }
        }
    }
    XClass_Parent(XWidget, EXWidget_ChangeEvent,
                  void(*)(XWidget*, XEvent*))((XWidget*)self, event);
}

/**
 * @brief      内容件标题跟随（对标 QMdiSubWindowPrivate::updateWindowTitle，
 *             qmdisubwindow.cpp:1849-1868 + eventFilter 的 WindowTitleChange
 *             分支:2704-2711）。
 * @details    跟随规则：① 内容件标题为空 ⇒ 不动作（Qt 的
 *             `if (!titleWidget || titleWidget->windowTitle().isEmpty())
 *             return;`）；② 子窗标题为空、或仍等于上一次内容件标题
 *             （lastChildWindowTitle）⇒ 采纳内容件新标题；③ 子窗标题
 *             已被改成别的值（用户自定义）⇒ 保持不动。无论是否采纳，
 *             更新 lastChildWindowTitle 为内容件当前标题（Qt 在
 *             eventFilter 里无条件更新 :2710-2711）。
 * @param sw 目标子窗口；可为 NULL。
 * @return 无返回值。
 */
static void xmdi_syncTitleFromWidget(XMdiSubWindow* sw)
{
    const XString* childTitle;
    const XString* ownTitle;
    bool follow;
    if (!sw || !sw->m_widget) return;
    childTitle = XWidget_windowTitle(sw->m_widget);
    if (!childTitle || XString_size((const XContainer*)childTitle) == 0) {
        if (!sw->m_lastChildTitle)
            sw->m_lastChildTitle = XString_create();
        if (sw->m_lastChildTitle)
            XString_assign_utf8(sw->m_lastChildTitle, "");
        return;
    }
    ownTitle = XWidget_windowTitle((XWidget*)sw);
    if (ownTitle && XString_size((const XContainer*)ownTitle) > 0) {
        follow = !sw->m_lastChildTitle ||
                 XString_size((const XContainer*)sw->m_lastChildTitle) == 0 ||
                 XString_equals(ownTitle, sw->m_lastChildTitle,
                                XChar_CaseSensitive);
    } else {
        follow = true; /* 子窗标题为空 ⇒ 始终跟随内容件 */
    }
    if (follow) XWidget_setWindowTitle((XWidget*)sw, childTitle);
    if (!sw->m_lastChildTitle) sw->m_lastChildTitle = XString_create();
    if (sw->m_lastChildTitle)
        XString_assign(sw->m_lastChildTitle, childTitle);
}

/**
 * @brief      事件过滤器：监视内容件的标题变化（对标 QMdiSubWindow 在
 *             setWidget 里 `baseWidget->installEventFilter(this)` 与
 *             eventFilter 的 WindowTitleChange 分支，qmdisubwindow.cpp:
 *             2293-2295、2704-2711）。
 * @param self 过滤器对象（子窗口）。
 * @param watched 被观察对象。
 * @param event 事件。
 * @return 恒 false（不过滤；Qt 末尾返回 QWidget::eventFilter 的结果，
 *         对本分支同为 false）。
 */
static bool VX_mdiSubWindow_eventFilter(XObject* self, XObject* watched,
                                        XEvent* event)
{
    XMdiSubWindow* sw = (XMdiSubWindow*)self;
    if (!sw || !event) return false;
    if (sw->m_widget && watched == (XObject*)sw->m_widget &&
        XEvent_type(event) == XEVENT_TYPE_WINDOW_TITLE_CHANGE)
        xmdi_syncTitleFromWidget(sw);
    return false;
}

#if XMENU_ON
/* ---- 缺省系统菜单条目槽（对标 createSystemMenu 的 SLOT 连接，
 *      qmdisubwindow.cpp:2321-2350） ---- */

/** @brief 系统菜单 Restore：showNormal。 */
static void xmdi_sysMenuRestore(XObject* receiver, XVarList* args)
{ (void)args; if (receiver) XWidget_showNormal((XWidget*)receiver); }

/** @brief 系统菜单 Minimize：showMinimized。 */
static void xmdi_sysMenuMinimize(XObject* receiver, XVarList* args)
{ (void)args; if (receiver) XWidget_showMinimized((XWidget*)receiver); }

/** @brief 系统菜单 Maximize：showMaximized。 */
static void xmdi_sysMenuMaximize(XObject* receiver, XVarList* args)
{ (void)args; if (receiver) XWidget_showMaximized((XWidget*)receiver); }

/** @brief 系统菜单 Stay on Top：翻转 WindowStaysOnTopHint。 */
static void xmdi_sysMenuStayOnTop(XObject* receiver, XVarList* args)
{
    XVarList_args_1(args, bool, checked);
    if (receiver)
        XWidget_setWindowFlag((XWidget*)receiver,
                              (XWidgetFlags)XWindowType_WindowStaysOnTopHint,
                              checked);
}

/** @brief 系统菜单 Close：close。 */
static void xmdi_sysMenuClose(XObject* receiver, XVarList* args)
{ (void)args; if (receiver) XWidget_close((XWidget*)receiver); }

/** @brief 键盘交互模式状态（定义在本段之后，槽函数先用到）。
 *  见下方「键盘交互模式」段的类型定义。 */
static void xmdi_enterInteractiveMode(XMdiSubWindow* sw, int operation);

/** @brief 系统菜单 &Move：进入键盘移动模式
 *         （对标 actions[MoveAction] → SLOT(_q_enterInteractiveMode())，
 *         qmdisubwindow.cpp:1034、905-918）。 */
static void xmdi_sysMenuMove(XObject* receiver, XVarList* args)
{
    (void)args;
    if (receiver)
        xmdi_enterInteractiveMode((XMdiSubWindow*)receiver,
                                  XMdiSubWindowEdgeOperation_Move);
}

/** @brief 系统菜单 &Size：进入键盘缩放模式（同上 :1035、918-925）。 */
static void xmdi_sysMenuSize(XObject* receiver, XVarList* args)
{
    (void)args;
    if (receiver)
        xmdi_enterInteractiveMode(
            (XMdiSubWindow*)receiver,
            XWidget_isLeftToRight((const XWidget*)receiver)
                ? XMdiSubWindowEdgeOperation_BottomRightResize
                : XMdiSubWindowEdgeOperation_BottomLeftResize);
}

/** @brief 建一条菜单项并接到子窗口槽（对齐 Qt addToSystemMenu）。
 * @return 新建动作借用指针（归菜单所有）；失败返回 NULL。 */
static XAction* xmdi_sysMenuAdd(XMdiSubWindow* sw, XMenu* menu,
                                const char* utf8,
                                void (*slot)(XObject*, XVarList*))
{
    XAction* action;
    if (!sw || !menu || !slot) return NULL;
    action = XMenu_addAction_2(menu, utf8);
    if (!action) return NULL;
    XObject_connect((XObject*)action, XSignal(XAction_triggered_signal),
                      (XObject*)sw, slot, XConnectionType_Direct);
    return action;
}

/**
 * @brief      构造标准系统菜单（对标 QMdiSubWindowPrivate::createSystemMenu，
 *             qmdisubwindow.cpp:2321-2350）。
 * @details    条目与 Qt **同序**（qmdisubwindow.cpp:1038-1046 的 addToSystemMenu
 *             调用序）：&Restore / &Move / &Size / Mi&nimize / Ma&ximize /
 *             Stay on &Top（可选中）/ 分隔线 / &Close。
 *             早期版本不列 &Move / &Size（当时无交互模式）；B9 已把该模式
 *             建成（键盘方向键移动/缩放，见 VX_mdiSubWindow_keyPressEvent），
 *             故两项一并补上并真兑现——不留「点了不响应」的占位项。
 * @param sw 目标子窗口。
 * @return 新建菜单（归子窗口所有）；分配失败返回 NULL。
 */
static XMenu* xmdi_createSystemMenu(XMdiSubWindow* sw)
{
    XMenu* menu = XMenu_create();
    XAction* stayAction;
    if (!menu) return NULL;
    /* 条目顺序 = Qt ensurePolished 建表序（qmdisubwindow.cpp:1038-1046）。 */
    xmdi_sysMenuAdd(sw, menu, "&Restore", xmdi_sysMenuRestore);
    xmdi_sysMenuAdd(sw, menu, "&Move", xmdi_sysMenuMove);
    xmdi_sysMenuAdd(sw, menu, "&Size", xmdi_sysMenuSize);
    xmdi_sysMenuAdd(sw, menu, "Mi&nimize", xmdi_sysMenuMinimize);
    xmdi_sysMenuAdd(sw, menu, "Ma&ximize", xmdi_sysMenuMaximize);
    stayAction = xmdi_sysMenuAdd(sw, menu, "Stay on &Top",
                                 xmdi_sysMenuStayOnTop);
    /* 可选中态（对齐 actions[StayOnTopAction]->setCheckable(true)）。 */
    if (stayAction) XAction_setCheckable(stayAction, true);
    XMenu_addSeparator(menu);
    xmdi_sysMenuAdd(sw, menu, "&Close", xmdi_sysMenuClose);
    return menu;
}
#endif /* XMENU_ON */

static void VX_mdiSubWindow_deinit(XMdiSubWindow* self)
{
    if (!self) return;
    if (self->m_widget) {
        /* 摘过滤器必须先于删内容件：XObject 析构按 m_watchedObjects
         * 回落 m_filters（XObject.c deinit 反注册段），内容件先释放会
         * 留下悬垂被观察指针。 */
        XObject_removeEventFilter((XObject*)self->m_widget, (XObject*)self);
        XClassDelete(self->m_widget);
        self->m_widget = NULL;
    }
    if (self->m_lastChildTitle) {
        XClassDelete((XClass*)self->m_lastChildTitle);
        self->m_lastChildTitle = NULL;
    }
    if (self->m_systemMenu) {
        XClassDelete((XClass*)self->m_systemMenu);
        self->m_systemMenu = NULL;
    }
    XClass_Deinit_Parent(XWidget, (XWidget*)self);
}

/* SizeHint 族虚槽（`VX*` 槽体，业务定义在本文件尾部；按《api命名风格》
 * *_base 契约：业务逻辑只放 `VX*` 槽体，对外唯一入口是分派
 * `XWidget_sizeHint_base`/`XWidget_minimumSizeHint_base`）。 */
static XSize VXMdiSubWindow_sizeHint(const XWidget* self);
static XSize VXMdiSubWindow_minimumSizeHint(const XWidget* self);

XVtable* XMdiSubWindow_class_init(void)
{
    XVTABLE_INIT_DEFAULT(XMdiSubWindow)
    XVTABLE_INHERIT_XCLASS(XWidget);
    XVTABLE_OVERLOAD_DEFAULT(EXWidget_SizeHint, VXMdiSubWindow_sizeHint);
    XVTABLE_OVERLOAD_DEFAULT(EXWidget_MinimumSizeHint, VXMdiSubWindow_minimumSizeHint);
    XVTABLE_OVERLOAD_DEFAULT(EXWidget_PaintEvent,
                             VX_mdiSubWindow_paintEvent);
    XVTABLE_OVERLOAD_DEFAULT(EXWidget_ResizeEvent,
                             VX_mdiSubWindow_resizeEvent);
    XVTABLE_OVERLOAD_DEFAULT(EXWidget_MousePressEvent,
                             VX_mdiSubWindow_mousePressEvent);
    XVTABLE_OVERLOAD_DEFAULT(EXWidget_MouseMoveEvent,
                             VX_mdiSubWindow_mouseMoveEvent);
    XVTABLE_OVERLOAD_DEFAULT(EXWidget_MouseReleaseEvent,
                             VX_mdiSubWindow_mouseReleaseEvent);
    XVTABLE_OVERLOAD_DEFAULT(EXWidget_KeyPressEvent,
                             VX_mdiSubWindow_keyPressEvent);
    XVTABLE_OVERLOAD_DEFAULT(EXWidget_EnterEvent,
                             VX_mdiSubWindow_enterEvent);
    XVTABLE_OVERLOAD_DEFAULT(EXWidget_LeaveEvent,
                             VX_mdiSubWindow_leaveEvent);
    XVTABLE_OVERLOAD_DEFAULT(EXWidget_ChangeEvent,
                             VX_mdiSubWindow_changeEvent);
    XVTABLE_OVERLOAD_DEFAULT(EXObject_EventFilter,
                             VX_mdiSubWindow_eventFilter);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Deinit, VX_mdiSubWindow_deinit);
    return XVTABLE_DEFAULT;
}

void XMdiSubWindow_init(XMdiSubWindow* self, XWidget* parent,
                        XWidgetFlags flags)
{
    XWidgetFlags mdiFlags;
    if (!self) return;
    XMemset(self, 0, sizeof(*self));
    /* 对标 QMdiSubWindow 构造（qmdisubwindow.cpp:2202-2210）：
     * `QWidget(parent, 0)` 经 QWidget 的窗口标志规范化后得到
     * 0x800f012（SubWindow 类型 + WindowTitleHint|WindowSystemMenuHint|
     * WindowMinimizeButtonHint|WindowMaximizeButtonHint|
     * WindowCloseButtonHint，本机 Qt 6.8.0 探针实测 addSubWindow 后
     * windowFlags==0x800f012）；无父构造实测 0x8800f001（Window 类型 +
     * 同一组提示位）。缺省窗口外观与标题带按钮的可用性都取决于这些
     * 提示位，故类型位缺省时补齐类型并并入提示位；调用方显式给了
     * FramelessWindowHint 则不加提示位（Qt 的 sizeParameters 同判据）。 */
    mdiFlags = flags;
    if (!(mdiFlags & (XWidgetFlags)XWindowType_TypeMask))
        mdiFlags |= (XWidgetFlags)(parent ? XWindowType_SubWindow
                                          : XWindowType_Window);
    if (!(mdiFlags & (XWidgetFlags)XWindowType_FramelessWindowHint))
        mdiFlags |= (XWidgetFlags)(
            (XWidgetFlags)XWindowType_WindowTitleHint |
            (XWidgetFlags)XWindowType_WindowSystemMenuHint |
            (XWidgetFlags)XWindowType_WindowMinimizeButtonHint |
            (XWidgetFlags)XWindowType_WindowMaximizeButtonHint |
            (XWidgetFlags)XWindowType_WindowCloseButtonHint);
    XWidget_init(&self->m_base, parent, mdiFlags);
    XClassSetVtable(self, XMdiSubWindow);
    Set_Class_Memory(self, XCLASS_DEFAULT_MEMORY_TYPE);
    Set_Class_IsHeap(self, false);
    XWidget_resize((XWidget*)self, 200, 150);
    /* 默认值与 Qt QMdiSubWindowPrivate 一致。 */
    self->m_keyboardSingleStep = 5;
    self->m_keyboardPageStep = 20;
    /* 鼠标跟踪：八向边缘光标要在**无按键**的悬停移动时刷新
     * （对标 QMdiSubWindow 构造的 setMouseTracking(true)，
     * qmdisubwindow.cpp:2213；Qt 老版为 WA_MouseTracking，
     * qwidget.cpp:11455 同义）。不置位则只有按下后才有 MOUSE_MOVE，
     * 边缘光标永远不出现。 */
    XWidget_setMouseTracking((XWidget*)self, true);
    /* 焦点策略 StrongFocus（对标 QMdiSubWindow 构造
     * qmdisubwindow.cpp:2215 `setFocusPolicy(Qt::StrongFocus)`）：
     * 系统菜单 &Move/&Size 的键盘交互、方向键移动/缩放都靠子窗持有
     * 焦点（XWidget.c:5052 缺省 NoFocus 会吞掉 keyPressEvent）。 */
    self->m_base.m_focusPolicy = XWidgetFocusPolicy_StrongFocus;
    self->m_edgeOperation = (uint8_t)XMdiSubWindowEdgeOperation_None;
    /* 构造即建标准系统菜单（qmdisubwindow.cpp:2202-2210 调
     * createSystemMenu；探针 systemMenu() 恒非空，含 Restore/Minimize/
     * Maximize/StayOnTop/Close）。 */
#if XMENU_ON
    self->m_systemMenu = xmdi_createSystemMenu(self);
#endif
}

XMdiSubWindow* XMdiSubWindow_create_ex(XMemoryType memory, XWidget* parent,
                                       XWidgetFlags flags)
{
    XMdiSubWindow* self =
        (XMdiSubWindow*)XMemory_malloc(sizeof(*self), memory);
    if (!self) return NULL;
    XMdiSubWindow_init(self, parent, flags);
    Set_Class_Memory(self, memory);
    Set_Class_IsHeap(self, true);
    return self;
}

void XMdiSubWindow_setWidget(XMdiSubWindow* self, XWidget* widget)
{
    XRect r;
    if (!self) return;
    /* 对标 QMdiSubWindow::setWidget（qmdisubwindow.cpp:2257-2287）：
       同指针重设幂等返回（Qt 同款护栏）；widget 为 NULL 等价
       removeBaseWidget——摘除旧件不销毁；不同指针先摘旧件再装新件
       （替换语义可用，此前以 `if (m_widget) return` 静默拒绝）。 */
    if (widget == self->m_widget) return;
    if (self->m_widget) {
        XWidget* old = self->m_widget;
        const XString* ownTitle = XWidget_windowTitle((XWidget*)self);
        const XString* oldTitle = XWidget_windowTitle(old);
        /* removeBaseWidget 对标（qmdisubwindow.cpp:985-1005）：
           ① 摘事件过滤器（否则旧件销毁后本对象在旧件 m_filters 里
              留悬垂：XObject 析构反注册段会回落读旧件的 m_filters）；
           ② 若子窗标题正是从旧内容件继承来的（旧件标题 == 子窗标题），
              清空子窗标题（Qt 的 `if (baseWidget->windowTitle() ==
              q->windowTitle()) { q->setWindowTitle(QString()); ... }`）；
           ③ 父子链交还调用方（不删，所有权归还）。 */
        XObject_removeEventFilter((XObject*)old, (XObject*)self);
        if (oldTitle && ownTitle &&
            XString_size((const XContainer*)ownTitle) > 0 &&
            XString_equals(oldTitle, ownTitle, XChar_CaseSensitive)) {
            XWidget_setWindowTitle((XWidget*)self, NULL);
        }
        XWidget_setParent(old, NULL, 0);
        self->m_widget = NULL;
    }
    if (!widget) {
        if (self->m_lastChildTitle) {
            XClassDelete((XClass*)self->m_lastChildTitle);
            self->m_lastChildTitle = NULL;
        }
        return;
    }
    self->m_widget = widget;
    XWidget_setParent(widget, (XWidget*)self, 0);
    /* 内容件内收 1px 挂载（x=1 起，底留框线行）：chrome 窗框绘制后
     * 侧框线不再被内容件覆盖；标题带区（0..19 行）仍为拖拽命中域。 */
    xmdi_subWindowContentRect(self, &r);
    XWidget_setGeometryRect(widget, &r);
    /* 对标 Qt（qmdisubwindow.cpp:2293-2299）：
     *   baseWidget->installEventFilter(this);
     *   if (windowTitle().isEmpty()) updateWindowTitle(true);
     *   lastChildWindowTitle = baseWidget->windowTitle();
     * ——装标题跟随过滤器；子窗标题为空时采纳内容件标题；记录上一次
     * 内容件标题供 eventFilter 的跟随判定。 */
    XObject_installEventFilter((XObject*)widget, (XObject*)self);
    {
        const XString* selfTitle = XWidget_windowTitle((XWidget*)self);
        if (!selfTitle || XString_size((const XContainer*)selfTitle) == 0) {
            const XString* wt = XWidget_windowTitle(widget);
            if (wt && XString_size((const XContainer*)wt) > 0)
                XWidget_setWindowTitle((XWidget*)self, wt);
        }
    }
    {
        const XString* wt = XWidget_windowTitle(widget);
        if (!self->m_lastChildTitle) self->m_lastChildTitle = XString_create();
        if (self->m_lastChildTitle) {
            if (wt) XString_assign(self->m_lastChildTitle, wt);
            else XString_assign_utf8(self->m_lastChildTitle, "");
        }
    }
}

XWidget* XMdiSubWindow_widget(const XMdiSubWindow* self)
{
    return self ? self->m_widget : NULL;
}

void XMdiSubWindow_setWindowTitle_2(XMdiSubWindow* self, const char* utf8)
{
    if (!self) return;
    /* 对标 QMdiSubWindow：标题即基类 QWidget::windowTitle 属性
     * （qmdisubwindow.cpp:994 的 updateWindowTitle 与标题条绘制同源），
     * 本接口只是 UTF-8 便捷重载，不再维护独立字段。 */
    XWidget_setWindowTitle_2((XWidget*)self, utf8 ? utf8 : "");
    XWidget_update((XWidget*)self);
}

const char* XMdiSubWindow_windowTitle_2(const XMdiSubWindow* self)
{
    const XString* title;
    const char* text;
    if (!self) return "";
    title = XWidget_windowTitle((const XWidget*)self);
    if (!title) return "";
    text = XString_toUtf8(title);
    return text ? text : "";
}

/* ==================== XMdiArea ==================== */

static void xmdi_emitActivated(XMdiArea* self, XMdiSubWindow* window)
{
    XVarList* args = XVarList_Create(XVar(XMdiSubWindow*, window));
    if (!args) return;
    if (self && ((XObject*)self)->m_signalSlot) {
        XObject_emitSignal((XObject*)self,
            (size_t)XMdiArea_subWindowActivated_signal, args,
            NULL, NULL, XEVENT_PRIORITY_NORMAL);
    } else {
        XVarList_delete(args);
    }
}

static void xmdi_emitAboutToActivate(XMdiSubWindow* sw)
{
    if (!sw) return;
    if (((XObject*)sw)->m_signalSlot) {
        XObject_emitSignal((XObject*)sw,
            (size_t)XMdiSubWindow_aboutToActivate_signal,
            NULL, NULL, NULL, XEVENT_PRIORITY_NORMAL);
    }
}

static void xmdi_emitStateChanged(XMdiSubWindow* sw,
                                  int oldState, int newState)
{
    XVarList* args;
    if (!sw) return;
    args = XVarList_Create(XVar(int, oldState), XVar(int, newState));
    if (!args) return;
    if (((XObject*)sw)->m_signalSlot) {
        XObject_emitSignal((XObject*)sw,
            (size_t)XMdiSubWindow_windowStateChanged_signal, args,
            NULL, NULL, XEVENT_PRIORITY_NORMAL);
    } else {
        XVarList_delete(args);
    }
}

/**
 * @brief      派生判定子窗是否仍处 tileSubWindows 铺出的网格态
 *             （对标 Qt 6.8.3 QMdiAreaPrivate::isSubWindowsTiled）。
 * @details    根因（二次复扫 R-24）：resizeEvent 曾无条件 tileSubWindows，
 *             默认 SubWindowView 下用户摆位每次区域尺寸变化即被摧毁。
 *             Qt 6.8.3 的 resizeEvent 仅在 isSubWindowsTiled 为真时重铺
 *             （qmdiarea.cpp:2288），该标志由 tileSubWindows 置位、用户
 *             移动/缩放子窗清零。本实现 XMdiArea 结构体在头文件中
 *             （本修复不可改头文件）且子窗无 move/resize 事件挂点，无
 *             字段承载该标志，故按几何派生：平铺网格（cols=2）的特征
 *             是全部子窗等宽等高、第 i 窗位于基准窗 +(列*宽, 行*高)
 *             的网格点。区域 resize 不移动子窗、相对关系保持 → 判定为
 *             平铺并随新尺寸重铺；用户挪动任一子窗即破坏网格 → 不再
 *             重铺。单子窗（n==1）时"平铺铺满整域"与"用户随意摆位"
 *             在 resize 后几何上不可区分，保守判否不重铺，保证永不毁
 *             摆位（退化：单子窗少了"铺满新域"一步）。
 */
static bool xmdi_isTiled(const XMdiArea* self)
{
    int64_t i;
    int64_t n;
    XMdiSubWindow* base;
    int baseX;
    int baseY;
    int baseW;
    int baseH;
    if (!self || !self->m_subWindows) return false;
    n = XVector_size_base((const XContainer*)self->m_subWindows);
    if (n < 2) return false;
    base = *(XMdiSubWindow**)XVector_at_base(self->m_subWindows, 0);
    if (!base) return false;
    baseX = XWidget_x((XWidget*)base);
    baseY = XWidget_y((XWidget*)base);
    baseW = XWidget_width((XWidget*)base);
    baseH = XWidget_height((XWidget*)base);
    for (i = 1; i < n; ++i) {
        XMdiSubWindow* sw =
            *(XMdiSubWindow**)XVector_at_base(self->m_subWindows, i);
        int col = (int)(i % 2); /* 与 XMdiArea_tileSubWindows 的列布局一致。 */
        int row = (int)(i / 2);
        if (!sw) return false;
        if (XWidget_x((XWidget*)sw) != baseX + col * baseW ||
            XWidget_y((XWidget*)sw) != baseY + row * baseH ||
            XWidget_width((XWidget*)sw) != baseW ||
            XWidget_height((XWidget*)sw) != baseH)
            return false;
    }
    return true;
}

static void VX_mdiArea_resizeEvent(XWidget* self, XEvent* event)
{
    if (!self) return;
    /* 链回基类视口/滚动条重排（框架口径同 XScrollArea 的
       XClass_Parent(XAbstractScrollArea, EXWidget_ResizeEvent) 链；
       Qt 中该职责由 QAbstractScrollArea 私有排版机制承担，本框架
       落在 VX_asa_resizeEvent）。此前未链回：MDI 区被页签容器拉伸
       后视口停留 creation 尺寸 200x150——addSubWindow 的级联摆位
       按视口宽高钳位把子窗 2 钳回 (0,0)，与子窗 1 完全叠死（文档 1
       标签被盖、两条蓝标题条重合成一条），滚动条排版也永不发生
       （gdb 树遍历实证：viewport rect=(0,0 200x150) 而 ASA
       rect=(0,0 752x430)）。 */
    XClass_Parent(XAbstractScrollArea, EXWidget_ResizeEvent,
                  XWidgetEventSlot)(self, event);
    /* 对标 Qt 6.8.3 QMdiArea::resizeEvent（qmdiarea.cpp:2273-2328）：
       ① 平铺网格态：重铺（:2288 分支，本实现按几何派生判定，见
          xmdi_isTiled 头注）；
       ② 否则**最大化子窗随区域放大**（:2296-2320 的 `child->resize(
          realSize)` 分支，realSize 先 expandedTo(child->minimumSizeHint())）；
       ③ Tabbed 视图（P3-53）随区域尺寸重排页签条与活动子窗；
       ④ 图标行随尺寸重排（:2324-2328 尾段 arrangeMinimizedSubWindows）。
       最大化子窗的跟随必须先于图标行——Qt 在最大化态会**跳过**图标行
       重排（改走 resize 定时器），本实现无定时器，直接按「有最大化子窗
       就不排图标」收口（最大化窗铺满视口，图标行被压在下面无需排）。 */
    if ((int)((XMdiArea*)self)->m_viewMode ==
        (int)XMdiAreaViewMode_TabbedView) {
        xmdi_tabBarSync((XMdiArea*)self);
    } else if (xmdi_isTiled((const XMdiArea*)self)) {
        XMdiArea_tileSubWindows((XMdiArea*)self);
    } else {
        bool hasMaximized = false;
        XWidget* viewport =
            XAbstractScrollArea_viewport((XAbstractScrollArea*)self);
        if (!viewport) viewport = (XWidget*)self;
        {
            int64_t i;
            int64_t n = ((XMdiArea*)self)->m_subWindows
                            ? XVector_size_base((const XContainer*)
                                                    ((XMdiArea*)self)
                                                        ->m_subWindows)
                            : 0;
            for (i = 0; i < n; ++i) {
                XMdiSubWindow* sw = XVector_At_Base(
                    ((XMdiArea*)self)->m_subWindows, i, XMdiSubWindow*);
                XSize minHint;
                int w;
                int h;
                if (!sw || !sw->m_maximizedMode) continue;
                minHint = XWidget_minimumSizeHint_base((const XWidget*)sw);
                w = XWidget_width(viewport);
                h = XWidget_height(viewport);
                if (w < minHint.width) w = minHint.width;
                if (h < minHint.height) h = minHint.height;
                XWidget_setGeometry((XWidget*)sw, 0, 0, w, h);
                hasMaximized = true;
            }
        }
        /* 图标行只在此分支重排（Qt 最大化态改走定时器，见上）。 */
        if (!hasMaximized) XMdiArea_arrangeIcons((XMdiArea*)self);
    }
}

/** @brief 绘制：只填充 paint 事件携带的脏区（对标 QMdiArea::paintEvent，
 *         qmdiarea.cpp:2669-2674「for (const QRect &exposedRect :
 *         paintEvent->region()) fillRect(exposedRect, background)」）。
 * @details 根因（night #50 交互擦除）：基类 VX_asa_paintEvent 恒以
 *          (0,0,w,h) 整幅涂写——XWidget_paintTree 按单矩形逐个派发且
 *          各 widget 绘制不受该矩形硬裁剪（仅表面裁剪=整区域外接框），
 *          于是 ①脏区外子窗像素在部分重绘期间即被抹白（后备帧内）；
 *          ②多矩形区域逐矩形派发时，后派矩形的整幅涂抹抹掉先派矩形
 *          刚恢复的子窗内容，整区域一次性 present 后即白屏（点击/
 *          拖拽后子窗铬架+内容全空的实锚：gdb 探针 pe=(0,0 200x150)
 *          全量子窗重绘后紧随的另一矩形派发把标题条抹白）。改为只涂
 *          事件脏区（含底边框线也按脏区裁剪），每矩形派发只改动自身
 *          矩形，多矩形序列可交换且与 present 区域严格闭合。 */
static void VX_mdiArea_paintEvent(XWidget* self, XEvent* event)
{
    XPainter painter;
    XImage* image;
    XPoint offset;
    XRect rect;
    XRect line;
    uint32_t base;
    uint32_t mid;
    XMdiArea* area = (XMdiArea*)self;
    int w;
    int h;
    if (!self || !event || XEvent_type(event) != XEVENT_TYPE_PAINT) return;
    w = XWidget_width(self);
    h = XWidget_height(self);
    image = XWidget_paintImage(self);
    if (!image) return;
    XPainter_init(&painter, NULL);
    if (!XPainter_begin_image(&painter, image)) {
        XPainter_deinit(&painter);
        return;
    }
    offset = XWidget_paintOffset(self);
    if (offset.x != 0 || offset.y != 0)
        XPainter_translate(&painter, (float)offset.x, (float)offset.y);
#if XPALETTE_ON
    {
        XPalette palette = XWidget_palette(self);
        XColor c;
        c = XPalette_color(&palette, XPaletteColorGroup_Current,
                           XPaletteColorRole_Base);
        base = XColor_rgba(&c);
        c = XPalette_color(&palette, XPaletteColorGroup_Current,
                           XPaletteColorRole_Mid);
        mid = XColor_rgba(&c);
    }
#else
    base = 0xFFFFFFFFu;
    mid = 0xFF808080u;
#endif /* XPALETTE_ON */
    /* 脏区∩控件矩形：事件矩形可能越出控件边界，先收进自身范围
       （同 XWidget_paintEvent_default 的裁剪口径）。 */
    rect = ((const XPaintEvent*)event)->m_rect;
    if (rect.x < 0) { rect.width += rect.x; rect.x = 0; }
    if (rect.y < 0) { rect.height += rect.y; rect.y = 0; }
    if (rect.x + rect.width > w) rect.width = w - rect.x;
    if (rect.y + rect.height > h) rect.height = h - rect.y;
    /* 【P3-54 对标 qmdiarea.cpp:2669-2675 paintEvent
     * 「painter.fillRect(exposedRect, d->background)」】背景三态：
     * ① setBackground_2 设了非 NoBrush 画刷 ⇒ 按刷填充（纯色/渐变经
     *    XBrush_applyToPainter + fillRect_2，对标 QBrush）；
     * ② 便捷色 m_background 非 0 ⇒ 按该 ARGB 填充；
     * ③ 都缺省 ⇒ 调色板 Base（既有口径）。 */
    if (rect.width > 0 && rect.height > 0) {
        if (area && XBrush_style(&area->m_backgroundBrush) !=
                        XBrushStyle_NoBrush) {
            XBrush_applyToPainter(&area->m_backgroundBrush, &painter, &rect);
            XPainter_fillRect_2(&painter, &rect);
        } else if (area && area->m_background != 0) {
            XPainter_fillRect(&painter, &rect, area->m_background);
        } else {
            XPainter_fillRect(&painter, &rect, base);
        }
    }
    /* 底边框线（同基类的 Mid 1px 线）：仅绘制落在脏区内的段落。 */
    if (h > 0 && (h - 1) >= rect.y &&
        (h - 1) < rect.y + rect.height && rect.width > 0) {
        XRect_init(&line, rect.x, h - 1, rect.width, 1);
        XPainter_fillRect(&painter, &line, mid);
    }
    XPainter_deinit(&painter);
}

static void VX_mdiArea_deinit(XMdiArea* self)
{
    if (!self) return;
    if (self->m_subWindows) {
        int64_t i;
        int64_t n = XVector_size_base(
            (const XContainer*)self->m_subWindows);
        for (i = 0; i < n; ++i) {
            XMdiSubWindow** sw =
                (XMdiSubWindow**)XVector_at_base(self->m_subWindows, i);
            if (sw && *sw)
                XClassDelete((XClass*)*sw);
        }
        XClassDelete(self->m_subWindows);
        self->m_subWindows = NULL;
    }
    if (self->m_activationHistory) {
        /* 借用指针账本：只释放数组（子窗口已在上方级联销毁）。 */
        XClassDelete(self->m_activationHistory);
        self->m_activationHistory = NULL;
    }
    if (self->m_tabbedRestore) {
        /* 几何快照（P3-53）：值条目，只释放数组。页签条为区域子控件，
         * 随控件树级联销毁，此处仅清登记。 */
        XClassDelete(self->m_tabbedRestore);
        self->m_tabbedRestore = NULL;
    }
    self->m_tabBar = NULL;
    XClass_Deinit_Parent(XAbstractScrollArea, (XAbstractScrollArea*)self);
}

/* SizeHint 族虚槽（`VX*` 槽体，对标 QMdiArea::sizeHint/minimumSizeHint
 * P3-51/52，业务定义在本文件尾部；按《api命名风格》*_base 契约：业务
 * 逻辑只放 `VX*` 槽体，对外唯一入口是分派
 * `XWidget_sizeHint_base`/`XWidget_minimumSizeHint_base`）。 */
static XSize VXmdiArea_sizeHint(const XWidget* self);
static XSize VXmdiArea_minimumSizeHint(const XWidget* self);

XVtable* XMdiArea_class_init(void)
{
    XVTABLE_INIT_DEFAULT(XMdiArea)
    XVTABLE_INHERIT_XCLASS(XAbstractScrollArea);
    XVTABLE_OVERLOAD_DEFAULT(EXWidget_ResizeEvent, VX_mdiArea_resizeEvent);
    XVTABLE_OVERLOAD_DEFAULT(EXWidget_PaintEvent, VX_mdiArea_paintEvent);
    XVTABLE_OVERLOAD_DEFAULT(EXWidget_SizeHint, VXmdiArea_sizeHint);
    XVTABLE_OVERLOAD_DEFAULT(EXWidget_MinimumSizeHint,
                             VXmdiArea_minimumSizeHint);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Deinit, VX_mdiArea_deinit);
    return XVTABLE_DEFAULT;
}

void XMdiArea_init(XMdiArea* self, XWidget* parent, XWidgetFlags flags)
{
    if (!self) return;
    XMemset(self, 0, sizeof(*self));
    XAbstractScrollArea_init(&self->m_base, parent, flags);
    XClassSetVtable(self, XMdiArea);
    Set_Class_Memory(self, XCLASS_DEFAULT_MEMORY_TYPE);
    Set_Class_IsHeap(self, false);
    self->m_subWindows = XVector_Create(XMdiSubWindow*);
    /* 激活历史账本（最近激活在前；对标 QMdiAreaPrivate::
     * indicesToActivatedChildren，qmdiarea.cpp:812/1018-1024）。 */
    self->m_activationHistory = XVector_Create(XMdiSubWindow*);
    self->m_active = NULL;
    self->m_tabBar = NULL;       /* Tabbed 视图页签条：惰性装配（P3-53）。 */
    self->m_tabbedRestore = NULL; /* 退出几何快照：进 Tabbed 时创建。 */
    self->m_viewMode = (int)XMdiAreaViewMode_SubWindowView;
    self->m_background = 0;
    self->m_backgroundBrush = XBrush_create(); /* NoBrush（P3-54）。 */
    self->m_tabPosition = 0;
    self->m_tabShape = 0;
    self->m_tabsMovable = false;
    self->m_tabsClosable = false;
    self->m_activationOrder = (int)XMdiAreaWindowOrder_CreationOrder;
    self->m_options = 0;
    self->m_documentMode = false;
    self->m_inTabBarSync = false;
}

XMdiArea* XMdiArea_create_ex(XMemoryType memory, XWidget* parent,
                             XWidgetFlags flags)
{
    XMdiArea* self = (XMdiArea*)XMemory_malloc(sizeof(*self), memory);
    if (!self) return NULL;
    XMdiArea_init(self, parent, flags);
    Set_Class_Memory(self, memory);
    Set_Class_IsHeap(self, true);
    return self;
}

XMdiSubWindow* XMdiArea_addSubWindow(XMdiArea* self, XWidget* widget)
{
    /* 对标 Qt addSubWindow(widget, windowFlags = Qt::WindowFlags()) 的
     * 缺省实参形态：原入口即 _2 的 flags=0 版（P0-52 补形参）。 */
    return XMdiArea_addSubWindow_2(self, widget, 0);
}

XMdiSubWindow* XMdiArea_addSubWindow_2(XMdiArea* self, XWidget* widget,
                                       XWidgetFlags flags)
{
    XMdiSubWindow* sw;
    XWidget* viewport;
    if (!self || !widget || !self->m_subWindows) return NULL;
    /* 子窗挂到视口（对标 QMdiAreaPrivate::appendChild，qmdiarea.cpp:
     * 791-797「child->setParent(viewport, ...)」）：挂到区域本体时子窗
     * 与视口为兄弟、被视口绘制次序压住且几何/裁剪口径错位——表现为
     * 子窗不可见仅剩残迹。 */
    viewport = XAbstractScrollArea_viewport((XAbstractScrollArea*)self);
    if (xmdi_isSubWindow(widget)) {
        /* 对标 QMdiArea::addSubWindow 的 qobject_cast 分支
         * （qmdiarea.cpp:1949-1957）：widget 本身已是子窗口 ⇒ **直接接管**
         * ——只把父链挂到视口并入列，不新建外壳、不动其内容件、不置
         * WA_DeleteOnClose（Qt 只给内部新建的那些置该属性）。已在区域内的
         * 子窗 Qt 只告警并原样返回（qmdiarea.cpp:1951-1954），本实现同。 */
        sw = (XMdiSubWindow*)widget;
        if (xmdi_subWindowIndex(self, sw) >= 0) return sw;
        /* 对标 qmdiarea.cpp:1955 `child->setParent(viewport(), windowFlags
         * ? windowFlags : child->windowFlags())` + QWidget 换父的类型位
         * 规范化：独立子窗的 Window 类型位被清掉、统一成 SubWindow，
         * 非类型提示位保留（本机 Qt 6.8.0 探针：独立子窗
         * flags=0x8800f001 → addSubWindow 后 0x800f012、isWindow=0）。
         * 【P0-52】基 flags 按显式 flags 优先：flags ? flags :
         * widget 自身 windowFlags（Qt 同式）。 */
        {
            XWidgetFlags baseFlags =
                flags ? flags : XWidget_windowFlags(widget);
            XWidgetFlags subFlags =
                (baseFlags & ~(XWidgetFlags)XWindowType_TypeMask) |
                (XWidgetFlags)XWindowType_SubWindow;
            XWidget_setParent(widget, viewport ? viewport : (XWidget*)self,
                              subFlags);
        }
    } else {
        /* 【P0-52 对标 qmdiarea.cpp:1944-1947】普通控件入口：调用方的
         * windowFlags 传给新建外壳（Qt 为
         * QMdiSubWindow(viewport(), windowFlags)），0 = 既有缺省行为。
         * 头文件此前声称「两种入口与 Qt 同构」却未提形参缺失，属漏
         * 实现+文档不实，随本重载一并收口。 */
        sw = XMdiSubWindow_create_ex(XCLASS_DEFAULT_MEMORY_TYPE,
                                     viewport ? viewport : (XWidget*)self,
                                     flags);
        if (!sw) return NULL;
        /* 对标 qmdiarea.cpp:1962 `child->setAttribute(Qt::WA_DeleteOnClose)`：
         * 内部创建的子窗关闭即回收（QMdiArea 不另持所有权账本）；
         * XMdiArea_closeAllSubWindows/closeActiveSubWindow 据此判定是否
         * 从数组摘除并延迟释放。 */
        XWidget_setAttribute((XWidget*)sw, XWidgetAttribute_DeleteOnClose,
                             true);
        XMdiSubWindow_setWidget(sw, widget);
    }
    /* 级联摆放（对标 appendChild 的 place(placer, child)：新子窗不全
       等位叠死在前一窗上）；越界时回落视口原点。 */
    {
        int64_t index = XVector_size_base(
            (const XContainer*)self->m_subWindows);
        int step = 24;
        int vw = viewport ? XWidget_width(viewport) : 0;
        int vh = viewport ? XWidget_height(viewport) : 0;
        int swx = (int)(index % 8) * step;
        int swy = (int)(index % 8) * step;
        int swW = XWidget_width((XWidget*)sw);
        int swH = XWidget_height((XWidget*)sw);
        if (vw > 0 && swx + swW > vw) swx = 0;
        if (vh > 0 && swy + swH > vh) swy = 0;
        XWidget_move((XWidget*)sw, swx, swy);
    }
    XVector_push_back_1_base(self->m_subWindows, &sw);
    xmdi_historyAdd(self, sw);
    XWidget_show((XWidget*)sw);
    XMdiArea_setActiveSubWindow(self, sw);
    return sw;
}

void XMdiArea_removeSubWindow(XMdiArea* self, XWidget* widget)
{
    int64_t i;
    int64_t n;
    if (!self || !self->m_subWindows || !widget) return;
    n = XVector_size_base((const XContainer*)self->m_subWindows);
    /* ① widget 本身是子窗口：摘父链 + 移出数组，**不删除对象**
     *    （qmdiarea.cpp:1984-1999；Qt 文档「widget is never actually
     *    deleted by QMdiArea」）。活动窗被移出时按 updateActiveWindow
     *    口径改激活下一个可见子窗。 */
    if (xmdi_isSubWindow(widget)) {
        i = xmdi_subWindowIndex(self, (const XMdiSubWindow*)widget);
        if (i < 0) return; /* 未登记：Qt 告警后直接返回 */
        XVector_remove_base(self->m_subWindows, i, 1);
        xmdi_historyRemove(self, (XMdiSubWindow*)widget);
        xmdi_reactivateAfterRemove(self, (XMdiSubWindow*)widget);
        /* 摘父链（转顶层）但保留其余窗口标志（Qt setParent(nullptr) 只
         * 把类型位转成 Window，标题带按钮提示位不丢）。 */
        XWidget_setParent(widget, NULL, XWidget_windowFlags(widget));
        /* 子窗集合已变：页签视图同步页签（P3-53；活动窗被移出时
         * reactivateAfterRemove 已同步过，此处幂等）。 */
        xmdi_tabBarSync(self);
        return;
    }
    /* ② widget 是某子窗的内容件：**只把内容件从子窗摘除**，子窗口
     *    保留在 subWindowList 里（qmdiarea.cpp:2004-2016 与官方文档
     *    「if an internal widget is passed in, the child widget is set to
     *    nullptr and the QMdiSubWindow is *not* removed」；本机 Qt 6.8.0
     *    探针：removeSubWindow(内容件) 后 subWindowList().size() 不变、
     *    child->widget()==nullptr、content->parentWidget()==nullptr、子窗
     *    仍可见）。内容件摘链后交还调用方（QMdiSubWindow::setWidget
     *    (nullptr) 即 removeBaseWidget，qmdisubwindow.cpp:985-1005）。 */
    for (i = 0; i < n; ++i) {
        XMdiSubWindow** sw =
            (XMdiSubWindow**)XVector_at_base(self->m_subWindows, i);
        if (sw && *sw && XMdiSubWindow_widget(*sw) == widget) {
            XMdiSubWindow_setWidget(*sw, NULL);
            return;
        }
    }
}

const XVector* XMdiArea_subWindowList(const XMdiArea* self)
{
    return self ? self->m_subWindows : NULL;
}

XVector* XMdiArea_subWindowList_2(const XMdiArea* self, int order)
{
    XVector* out;
    int64_t i;
    int64_t n;
    out = XVector_Create(XMdiSubWindow*);
    if (!out) return NULL;
    if (!self) return out;
    if (order == (int)XMdiAreaWindowOrder_StackingOrder) {
        /* 视口子控件顺序 = 叠加次序：XWidget_raise/lower/stackUnder 在
         * 父控件子数组中移动（XWidget.c XWidget_raise），与 Qt 的
         * viewport->children() 顺序同构（qmdiarea.cpp:1360-1371）。 */
        XWidget* viewport =
            XAbstractScrollArea_viewport((XAbstractScrollArea*)self);
        const XVector* children =
            viewport ? XObject_children((XObject*)viewport) : NULL;
        if (children) {
            n = XVector_size_base((const XContainer*)children);
            for (i = 0; i < n; ++i) {
                XObject* obj = *(XObject**)XVector_at_base(children, i);
                XMdiSubWindow* sw;
                if (!obj || !obj->m_is_widget) continue;
                sw = (XMdiSubWindow*)obj;
                if (xmdi_subWindowIndex(self, sw) < 0) continue;
                XVector_push_back_1_base(out, &sw);
            }
        }
        return out;
    }
    if (order == (int)XMdiAreaWindowOrder_ActivationHistoryOrder &&
        self->m_activationHistory) {
        /* 激活历史由旧到新：账本「最近激活在前」，逆序输出
         * （qmdiarea.cpp:1372-1385 从 indicesToActivatedChildren 尾部
         * 向前遍历）。 */
        n = XVector_size_base((const XContainer*)self->m_activationHistory);
        for (i = n; i > 0; --i) {
            XMdiSubWindow** slot = (XMdiSubWindow**)XVector_at_base(
                self->m_activationHistory, i - 1);
            if (slot && *slot)
                XVector_push_back_1_base(out, slot);
        }
        return out;
    }
    /* CreationOrder（缺省）：登记（创建）顺序。 */
    if (self->m_subWindows) {
        n = XVector_size_base((const XContainer*)self->m_subWindows);
        for (i = 0; i < n; ++i) {
            XMdiSubWindow** slot =
                (XMdiSubWindow**)XVector_at_base(self->m_subWindows, i);
            if (slot && *slot) XVector_push_back_1_base(out, slot);
        }
    }
    return out;
}

XMdiSubWindow* XMdiArea_activeSubWindow(const XMdiArea* self)
{
    return self ? self->m_active : NULL;
}

void XMdiArea_setActiveSubWindow(XMdiArea* self, XMdiSubWindow* window)
{
    if (!self) return;
    if (!window) {
        /* 对标 qmdiarea.cpp:1805-1811：setActiveSubWindow(nullptr) →
           activateWindow(nullptr) → 失活当前窗并经 resetActiveWindow
           发射 subWindowActivated(nullptr)（qmdiarea.cpp:957-963）。
           此前 NULL 直接到 return，无法触发失活。区域为空时 Qt 的
           activateWindow 亦直接返回（qmdiarea.cpp:951-955）——无活动
           窗时同样不发射。 */
        if (!self->m_active) return;
        {
            /* 失活只清「活动态」相关的位；折叠位（Shaded）是窗口自身
             * 形态，不随失活清掉（否则重新激活时 windowStateChanged
             * 载荷与 isShaded() 失同步）。 */
            int oldState = self->m_active->m_state;
            int newState = oldState & XMdiSubWindowState_Shaded;
            xmdi_emitStateChanged(self->m_active, oldState, newState);
            self->m_active->m_state = (uint8_t)newState;
        }
        XWidget_update((XWidget*)self->m_active);
        self->m_active = NULL;
        xmdi_emitActivated(self, NULL);
        /* Tabbed 视图（P3-53）：失活后页签条下标回 -1 口径由 sync 收口。 */
        xmdi_tabBarSync(self);
        return;
    }
    if (self->m_active == window) return;
    /* 激活时「最大化随激活转移」（对标 _q_deactivateAllWindows 的
     * showActiveWindowMaximized 捕获 + emitWindowActivated 的兑现，
     * qmdiarea.cpp:660-700、1012-1024）：
     *   ① 选项 DontMaximizeSubWindowOnActivation 清零（缺省）且**另有**
     *      可见子窗处于最大化 ⇒ 记 carryMaximized 并把该窗 showNormal
     *      （Qt 的 `child->showNormal()`，:687）；
     *   ② 新激活窗在置顶后按该标志 showMaximized（Qt
     *      `if (!activeWindow->isMaximized()) activeWindow->showMaximized();`，
     *      :1019-1023）。
     * 选项置位时一步都不做——旧窗保持最大化、新窗保持普通态，这正是
     * DontMaximizeSubWindowOnActivation 的语义（此前该位只存不兑现）。 */
    {
        bool carryMaximized = false;
        if (!XMdiArea_testOption(
                self,
                XMdiAreaAreaOption_DontMaximizeSubWindowOnActivation)) {
            /* 先拷贝快照再遍历（对标 qmdiarea.cpp:676-679 的原文注释：
             * 「Take a copy because child->showNormal() could indirectly
             * call QCoreApplication::sendEvent(), which could call unknown
             * code that e.g. recurses into the class modifying
             * childWindows」）。直解引用 m_subWindows 遍历时 showNormal
             * 若重入增删子窗，XVector_At_Base 会读已缩容数组。 */
            XVector* snapshot = XMdiArea_subWindowList_2(
                self, (int)XMdiAreaWindowOrder_CreationOrder);
            int64_t ci;
            int64_t cn = snapshot
                             ? XVector_size_base((const XContainer*)snapshot)
                             : 0;
            for (ci = 0; ci < cn; ++ci) {
                XMdiSubWindow* child =
                    XVector_At_Base(snapshot, ci, XMdiSubWindow*);
                if (!child || child == window) continue;
                if (!XWidget_isVisible((const XWidget*)child)) continue;
                if (!XWidget_isMaximized((const XWidget*)child) &&
                    !child->m_maximizedMode)
                    continue;
                carryMaximized = true;
                XWidget_showNormal((XWidget*)child);
            }
            if (snapshot) XClassDelete((XClass*)snapshot);
        }
        if (self->m_active) {
            int oldState = self->m_active->m_state;
            int newState = oldState & XMdiSubWindowState_Shaded;
            xmdi_emitStateChanged(self->m_active, oldState, newState);
            self->m_active->m_state = (uint8_t)newState;
            /* 活动态驱动标题带配色（paintEvent 激活分支），失活窗立即
               重绘，否则保留 Highlight 底的陈旧像素。 */
            XWidget_update((XWidget*)self->m_active);
        }
        xmdi_emitAboutToActivate(window);
        self->m_active = window;
        /* 激活历史账本（对标 activateWindow 的
         * `indicesToActivatedChildren.move(index, 0)`，qmdiarea.cpp:1018-1024）。 */
        xmdi_historyRaise(self, window);
        /* 激活即置顶（对标 activateWindow 的 internalRaise(activeWindow)，
         * qmdiarea.cpp:1025）：子控件数组次序即绘制/叠加次序
         * （XWidget_raise 把子件移到父 children 末尾），StackingOrder 因此
         * 反映最近激活的窗口在最上层。 */
        XWidget_raise((XWidget*)window);
        if (carryMaximized && !XWidget_isMaximized((const XWidget*)window))
            XWidget_showMaximized((XWidget*)window);
    }
    xmdi_emitStateChanged(window, 0, window->m_state);
    xmdi_emitActivated(self, window);
    XWidget_update((XWidget*)window);
    /* Tabbed 视图（P3-53）：激活即切页——页签条当前下标跟随活动子窗、
     * 仅活动子窗可见。SubWindowView 下 sync 直接短路，无开销。 */
    xmdi_tabBarSync(self);
}

void XMdiArea_closeAllSubWindows(XMdiArea* self)
{
    int64_t i;
    XMdiSubWindow* activeBefore;
    if (!self || !self->m_subWindows) return;
    activeBefore = self->m_active;
    /* 逆序遍历（删除后下标不漂移）；**不用**同步列表拷贝——close 可能
     * 重入改数组（对齐 qmdiarea.cpp:1863-1871 先拷贝 childWindows 的
     * 动机），每步按当前长度重读槽位。 */
    i = XVector_size_base((const XContainer*)self->m_subWindows) - 1;
    for (; i >= 0; --i) {
        XMdiSubWindow** slot;
        XMdiSubWindow* sw;
        bool deleteOnClose;
        if (i >= XVector_size_base((const XContainer*)self->m_subWindows))
            continue; /* 重入已缩短数组 */
        slot = (XMdiSubWindow**)XVector_at_base(self->m_subWindows, i);
        if (!slot || !*slot) continue;
        sw = *slot;
        deleteOnClose = XWidget_testAttribute(
            (XWidget*)sw, XWidgetAttribute_DeleteOnClose);
        /* 对标 qmdiarea.cpp:1857-1873（逐窗 `child->close()`）：**走关闭
         * 事件**——忽略关闭事件的子窗口保持打开（Qt 文档明写
         * 「Subwindows that ignore the close event will remain open」），
         * 旧实现无条件 deleteLater 会吞掉该否决。 */
        if (!XWidget_close((XWidget*)sw)) continue;
        /* 非内部创建（未被赋予 DeleteOnClose）的子窗仅隐藏，仍留在
         * subWindowList 里——本机 Qt 6.8.0 实测：用户自建
         * QMdiSubWindow 经 addSubWindow 后 close() 返回 1、对象存活、
         * subWindowList().size() 不变。 */
        if (!deleteOnClose) continue;
        XVector_remove_base(self->m_subWindows, i, 1);
        xmdi_historyRemove(self, sw);
        XObject_deleteLater((XObject*)sw);
    }
    /* 活动窗被关闭（或被隐藏）后收口激活：还剩可见子窗则激活之，
     * 否则发 subWindowActivated(nullptr)（qmdiarea.cpp:1049-1126）。 */
    if (activeBefore && self->m_active == activeBefore &&
        (xmdi_subWindowIndex(self, activeBefore) < 0 ||
         XWidget_isHidden((XWidget*)activeBefore)))
        xmdi_reactivateAfterRemove(self, activeBefore);
    /* 子窗集合已变：页签视图同步页签（P3-53，幂等）。 */
    xmdi_tabBarSync(self);
}

void XMdiArea_cascadeSubWindows(XMdiArea* self)
{
    int64_t i;
    int64_t n;
    int offset = 0;
    int w = XWidget_width((XWidget*)self);
    int h = XWidget_height((XWidget*)self);
    if (!self || !self->m_subWindows) return;
    /* Tabbed 视图（P3-53）：层叠对页签视图无意义，恢复页签布局
     * （对标 Qt 页签视图下 cascade 只影响可见的唯一子窗）。 */
    if ((int)self->m_viewMode == (int)XMdiAreaViewMode_TabbedView) {
        xmdi_tabBarSync(self);
        return;
    }
    n = XVector_size_base((const XContainer*)self->m_subWindows);
    for (i = 0; i < n; ++i) {
        XMdiSubWindow** sw =
            (XMdiSubWindow**)XVector_at_base(self->m_subWindows, i);
        XRect r;
        if (!sw || !*sw) continue;
        XRect_init(&r, offset, offset,
                   w > 60 ? w - 40 - offset : 200,
                   h > 60 ? h - 40 - offset : 150);
        XWidget_setGeometryRect((XWidget*)*sw, &r);
        offset += 24;
    }
}

void XMdiArea_tileSubWindows(XMdiArea* self)
{
    int64_t i;
    int64_t n;
    int count;
    int cols = 2;
    int rows;
    int w = XWidget_width((XWidget*)self);
    int h = XWidget_height((XWidget*)self);
    if (!self || !self->m_subWindows) return;
    /* Tabbed 视图（P3-53）：同 cascade，恢复页签布局。 */
    if ((int)self->m_viewMode == (int)XMdiAreaViewMode_TabbedView) {
        xmdi_tabBarSync(self);
        return;
    }
    n = XVector_size_base((const XContainer*)self->m_subWindows);
    count = (int)n;
    if (count <= 0) return;
    rows = (count + cols - 1) / cols;
    for (i = 0; i < n; ++i) {
        XMdiSubWindow** sw =
            (XMdiSubWindow**)XVector_at_base(self->m_subWindows, i);
        XRect r;
        int col = (int)(i % cols);
        int row = (int)(i / cols);
        if (!sw || !*sw) continue;
        XRect_init(&r, col * w / cols, row * h / rows,
                   w / cols, h / rows);
        XWidget_setGeometryRect((XWidget*)*sw, &r);
    }
}

/* ==================== 图标排布（arrangeIcons） ==================== */

/** @brief 最小化子窗的图标尺寸（对标 QMdiSubWindowPrivate::iconSize，
 *         qmdisubwindow.cpp:2117-2123）。
 * @details Qt：`if (!parent || windowFlags & FramelessWindowHint) return
 *          QSize(-1,-1);` 否则 `QSize(PM_MdiSubWindowMinimizedWidth,
 *          titleBarHeight())`。本框架的标题带高度是常量 20
 *          （XMDI_SUBWINDOW_TITLEBAR_HEIGHT）。
 * @param sw 目标子窗口；可为 NULL。
 * @param out 输出尺寸。
 * @return 无返回值；不适用时输出 (-1,-1)（Qt 同）。 */
static void xmdi_iconSize(const XMdiSubWindow* sw, XSize* out)
{
    XStyle* style;
    int pmWidth = 0;
    if (!out) return;
    XSize_init(out, -1, -1);
    if (!sw) return;
    if (!XWidget_parentWidget((const XWidget*)sw)) return;
    if (XWidget_windowFlags((const XWidget*)sw) &
        (XWidgetFlags)XWindowType_FramelessWindowHint)
        return;
    style = XWidget_style((const XWidget*)sw);
    if (style)
        pmWidth = XStyle_pixelMetric_base(
            style, (int)XStylePM_MdiSubWindowMinimizedWidth, NULL);
    XSize_init(out, pmWidth, XMDI_SUBWINDOW_TITLEBAR_HEIGHT);
}

/** @brief 图标排布的收集器：把「最小化且非折叠」的可见子窗按激活顺序
 *         收进 out（对标 rearrange 的 IconTiler 分支，
 *         qmdiarea.cpp:900-920）。
 * @details Qt 原文（:905-911）：
 *          `widgets = subWindowList(activationOrder, reverseList=false)`
 *          然后只挑 `child->isVisible() && child->isMinimized() &&
 *          !child->isShaded()`。RegularTiler 反过来跳过最小化窗。
 * @param self 目标区域。
 * @param out 输出数组（XMdiSubWindow*；调用方创建/清空）。
 * @return 收进的子窗数。 */
static int xmdi_collectIconWindows(const XMdiArea* self, XVector* out)
{
    XVector* list;
    int64_t i;
    int64_t n;
    int count = 0;
    if (!self || !out) return 0;
    /* 激活顺序取区域设置（Qt: subWindowList(activationOrder)，:901）。 */
    list = XMdiArea_subWindowList_2(self, self->m_activationOrder);
    if (!list) return 0;
    n = XVector_size_base((const XContainer*)list);
    for (i = 0; i < n; ++i) {
        XMdiSubWindow* sw = XVector_At_Base(list, i, XMdiSubWindow*);
        if (!sw) continue;
        if (!XWidget_isVisible((const XWidget*)sw)) continue;
        if (!XWidget_isMinimized((const XWidget*)sw)) continue;
        if (sw->m_shaded) continue; /* 折叠窗留在原位（Qt :907 同）。 */
        XVector_push_back_1_base(out, &sw);
        ++count;
    }
    XClassDelete((XClass*)list);
    return count;
}

/** @brief 图标网格排布（对标 QMdiAreaPrivate::arrangeMinimizedSubWindows
 *         → IconTiler::rearrange，qmdiarea.cpp:939-943、329-357）。
 * @details Qt 原文（IconTiler::rearrange）：
 *          ```
 *          const int n = widgets.size();
 *          const int width  = qMax(widgets.at(0)->width(), 1);
 *          const int height = widgets.at(0)->height();
 *          const int ncols  = qMax(domain.width() / width, 1);
 *          const int nrows  = n / ncols + ((n % ncols) ? 1 : 0);
 *          for (row = 0; row < nrows; ++row)
 *            for (col = 0; col < ncols; ++col) {
 *              x = col * width;
 *              y = domain.height() - height - row * height;  // 自底向上
 *              widget->setGeometry(visualRect(ld, domain, QRect({x,y}, widget->size())));
 *            }
 *          ```
 *          要点：① 列数按**首个子窗实际宽度**算（不是提示宽），至少 1 列；
 *          ② 行自**底边向上**堆（先排满最下一行）；③ x 从 0 起（不除余
 *          居中），y 恰使最下一行贴 domain 底边；④ 每格尺寸取子窗**当前**
 *          尺寸（不是统一格宽）。本实现逐条对齐。
 * @param self 目标区域；NULL/空列表返回。
 * @return 无返回值。
 */
void XMdiArea_arrangeIcons(XMdiArea* self)
{
    XVector* widgets;
    XWidget* domainWidget;
    XRect domain;
    int n;
    int width;
    int height;
    int ncols;
    int nrows;
    int domainW;
    int domainH;
    int i = 0;
    int row;
    int col;
    if (!self || !self->m_subWindows) return;
    /* Tabbed 视图（P3-53）：图标排布对页签视图无意义，恢复页签布局
     * （同 cascade/tile 的既有裁定：Qt 页签视图下不存在图标窗）。 */
    if ((int)self->m_viewMode == (int)XMdiAreaViewMode_TabbedView) {
        xmdi_tabBarSync(self);
        return;
    }
    widgets = XVector_Create(XMdiSubWindow*);
    if (!widgets) return;
    n = xmdi_collectIconWindows(self, widgets);
    if (n <= 0) {
        XClassDelete((XClass*)widgets);
        return;
    }
    /* 排布域 = 视口矩形（Qt: `QRect domain = viewport->rect();`，
     * qmdiarea.cpp:920；IconTiler 不参与 RegularTiler 的
     * resizeToMinimumTileSize 分支，:921-922 只对 tiler 生效）。 */
    domainWidget = XAbstractScrollArea_viewport((XAbstractScrollArea*)self);
    if (!domainWidget) domainWidget = (XWidget*)self;
    domainW = XWidget_width(domainWidget);
    domainH = XWidget_height(domainWidget);
    /* 首窗尺寸（Qt: widgets.at(0)->width()/height()）。 */
    {
        XMdiSubWindow* first = XVector_At_Base(widgets, 0, XMdiSubWindow*);
        width = first ? XWidget_width((const XWidget*)first) : 0;
        height = first ? XWidget_height((const XWidget*)first) : 0;
    }
    if (width < 1) width = 1;
    ncols = domainW / width;
    if (ncols < 1) ncols = 1;
    nrows = n / ncols + ((n % ncols) ? 1 : 0);
    XRect_init(&domain, 0, 0, domainW, domainH);
    for (row = 0; row < nrows && i < n; ++row) {
        for (col = 0; col < ncols && i < n; ++col) {
            XMdiSubWindow* sw = XVector_At_Base(widgets, i, XMdiSubWindow*);
            XRect r;
            ++i;
            if (!sw) continue;
            /* Qt 的 QRect(QPoint(x, y), widget->size())：每格取子窗自身
             * 尺寸（图标窗已由 setMinimizeMode 缩成 iconSize）。 */
            XRect_init(&r, col * width, domainH - height - row * height,
                       XWidget_width((const XWidget*)sw),
                       XWidget_height((const XWidget*)sw));
            /* 视觉矩形（Qt: QStyle::visualRect(layoutDirection, domain,
             * newGeometry)）——RTL 时按域宽镜像。方向取**子窗自身**的
             * layoutDirection（Qt 原文 `visualRect(widget->layoutDirection(),
             * ...)`，qmdiarea.cpp:349；子窗未显式设置时继承区域方向，
             * 显式设置后该窗单独镜像——复核返工订正，此前取区域方向，
             * 锁 arrangeIconsFollowsWindowLayoutDirection）。 */
            r = XStyle_visualRect(
                (int)XWidget_layoutDirection((const XWidget*)sw), &domain,
                &r);
            XWidget_setGeometryRect((XWidget*)sw, &r);
        }
    }
    XClassDelete((XClass*)widgets);
}

void XMdiArea_setViewMode(XMdiArea* self, XMdiAreaViewMode mode)
{
    if (!self) return;
    if (mode != XMdiAreaViewMode_SubWindowView &&
        mode != XMdiAreaViewMode_TabbedView)
        return; /* 越界值忽略（Qt 对非法枚举告警后不动）。 */
    if ((int)mode == self->m_viewMode)
        return; /* 同值早退（Qt d->setViewMode 首判同款）。 */
    if (mode == XMdiAreaViewMode_TabbedView) {
        /* 【P3-53 进 Tabbed】捕获当前几何：Tabbed 期间活动子窗被布局
         * 成铺满视口（对标 Qt showMaximized），退出时按本快照回放
         * （对标 Qt 切回子窗视图对最大化子窗 showNormal 的还原语义）。
         * 之后加入的子窗无快照，退出时保持页签布局几何。 */
        int64_t i;
        int64_t n = self->m_subWindows
                        ? XVector_size_base(
                              (const XContainer*)self->m_subWindows)
                        : 0;
        if (!self->m_tabbedRestore)
            self->m_tabbedRestore = XVector_Create(XmdiTabRestore);
        if (self->m_tabbedRestore)
            XVector_clear_base((XContainer*)self->m_tabbedRestore);
        for (i = 0; i < n; ++i) {
            XMdiSubWindow* sw = XVector_At_Base(self->m_subWindows, i,
                                                XMdiSubWindow*);
            XmdiTabRestore entry;
            if (!sw) continue;
            entry.m_sw = sw;
            entry.m_geom = XWidget_geometry((XWidget*)sw);
            if (self->m_tabbedRestore)
                XVector_push_back_1_base(self->m_tabbedRestore, &entry);
        }
        self->m_viewMode = (int)mode;
        xmdi_tabBarSync(self);
        return;
    }
    /* 【P3-53 切回 SubWindowView】撤页签条 + 还视口边距 + 回放快照 +
     * 恢复全部子窗可见（页签视图期间隐藏的非活动窗回到平铺）。
     * closeAll 留下的隐藏子窗同样被恢复——页签视图的隐藏由本机制
     * 承担，平铺视图不存在「登记但隐藏」的常态。 */
    self->m_viewMode = (int)mode;
    xmdi_tabBarSync(self);
    if (self->m_tabbedRestore) {
        int64_t i;
        int64_t rn = XVector_size_base(
            (const XContainer*)self->m_tabbedRestore);
        for (i = 0; i < rn; ++i) {
            XmdiTabRestore* e =
                (XmdiTabRestore*)XVector_at_base(self->m_tabbedRestore, i);
            if (e && e->m_sw && xmdi_subWindowIndex(self, e->m_sw) >= 0)
                XWidget_setGeometryRect((XWidget*)e->m_sw, &e->m_geom);
        }
        XVector_clear_base((XContainer*)self->m_tabbedRestore);
    }
    {
        int64_t i;
        int64_t n = self->m_subWindows
                        ? XVector_size_base(
                              (const XContainer*)self->m_subWindows)
                        : 0;
        for (i = 0; i < n; ++i) {
            XMdiSubWindow* sw = XVector_At_Base(self->m_subWindows, i,
                                                XMdiSubWindow*);
            if (sw && XWidget_isHidden((XWidget*)sw))
                XWidget_show((XWidget*)sw);
        }
    }
}

XMdiAreaViewMode XMdiArea_viewMode(const XMdiArea* self)
{
    return self ? (XMdiAreaViewMode)self->m_viewMode
                : XMdiAreaViewMode_SubWindowView;
}

int XMdiArea_subWindowCount(const XMdiArea* self)
{
    return (self && self->m_subWindows)
               ? (int)XVector_size_base(
                     (const XContainer*)self->m_subWindows)
               : 0;
}

/* ==================== 信号 ==================== */

void* XMdiArea_subWindowActivated_signal(XMdiArea* self,
                                         XMdiSubWindow* window)
{
    (void)self; (void)window;
    return (void*)(size_t)XMdiArea_subWindowActivated_signal;
}

void* XMdiSubWindow_aboutToActivate_signal(XMdiSubWindow* self)
{
    (void)self;
    return (void*)(size_t)XMdiSubWindow_aboutToActivate_signal;
}
void* XMdiSubWindow_windowStateChanged_signal(XMdiSubWindow* self,
                                              int oldState, int newState)
{
    (void)self; (void)oldState; (void)newState;
    return (void*)(size_t)XMdiSubWindow_windowStateChanged_signal;
}

/* ==================== Task 2.7：MdiArea 补充 API ==================== */

XMdiSubWindow* XMdiArea_currentSubWindow(const XMdiArea* self)
{ return self ? self->m_active : NULL; }

void XMdiArea_setBackground(XMdiArea* self, uint32_t color)
{
    if (!self) return;
    self->m_background = color;
    /* 【P3-54】便捷色与画刷保持单一事实源：非 0 色即纯色刷（绘制期
     * 走刷路径）；0 = 未设（NoBrush，回退调色板 Base）。 */
    if (color != 0) {
        XColor c = XColor_create_rgb((int)((color >> 16) & 0xFFu),
                                     (int)((color >> 8) & 0xFFu),
                                     (int)(color & 0xFFu),
                                     (int)((color >> 24) & 0xFFu));
        self->m_backgroundBrush = XBrush_create_color(
            c, XBrushStyle_SolidPattern);
    } else {
        self->m_backgroundBrush = XBrush_create();
    }
    XWidget_update((XWidget*)self);
}

void XMdiArea_setBackground_2(XMdiArea* self, const XBrush* brush)
{
    if (!self) return;
    /* 【P3-54 对标 setBackground(const QBrush&)】画刷为事实源：非
     * NoBrush 存刷并把便捷色视图同步为刷色（纯色）或归 0（渐变/纹理
     * 无单色视图）；NoBrush 等价 setBackground(0)。 */
    if (!brush || XBrush_style(brush) == XBrushStyle_NoBrush) {
        self->m_backgroundBrush = XBrush_create();
        self->m_background = 0;
    } else {
        self->m_backgroundBrush = *brush;
        if (XBrush_style(brush) == XBrushStyle_SolidPattern) {
            XColor c = XBrush_color(brush);
            self->m_background = XColor_rgba(&c);
        } else {
            self->m_background = 0;
        }
    }
    XWidget_update((XWidget*)self);
}

uint32_t XMdiArea_background(const XMdiArea* self)
{ return self ? self->m_background : 0; }

const XBrush* XMdiArea_background_2(const XMdiArea* self)
{ return self ? &self->m_backgroundBrush : NULL; }

void XMdiArea_setTabPosition(XMdiArea* self, int position)
{
    if (!self) return;
    self->m_tabPosition = (uint8_t)position;
    /* Tabbed 视图（P3-53）：页签条位置随 setter 生效（North/South 有
     * 几何口径；West/East 的水平页签条退化北向，见 xmdi_tabBarSync）。 */
    if ((int)self->m_viewMode == (int)XMdiAreaViewMode_TabbedView)
        xmdi_tabBarSync(self);
}
int XMdiArea_tabPosition(const XMdiArea* self)
{ return self ? self->m_tabPosition : 0; }

void XMdiArea_setTabsMovable(XMdiArea* self, bool movable)
{
    if (!self) return;
    self->m_tabsMovable = movable;
    /* 【P3-53】Tabbed 视图下直接下发页签条（拖拽交换由 XTabBar 承载，
     * tabMoved 槽把子窗表按同序平移）。 */
#if XTABBAR_ON
    if (self->m_tabBar)
        XTabBar_setMovable((XTabBar*)self->m_tabBar, movable);
#endif
}
bool XMdiArea_tabsMovable(const XMdiArea* self)
{ return self ? self->m_tabsMovable : false; }

void XMdiArea_setTabsClosable(XMdiArea* self, bool closable)
{
    if (!self) return;
    self->m_tabsClosable = closable;
    /* 【P3-53】Tabbed 视图下直接下发页签条（关闭按钮绘制/命中由
     * XTabBar 承载，tabCloseRequested → 关闭对应子窗）。 */
#if XTABBAR_ON
    if (self->m_tabBar)
        XTabBar_setTabsClosable((XTabBar*)self->m_tabBar, closable);
#endif
}
bool XMdiArea_tabsClosable(const XMdiArea* self)
{ return self ? self->m_tabsClosable : false; }

void XMdiArea_setActivationOrder(XMdiArea* self, int order)
{ if (self) self->m_activationOrder = order; }
int XMdiArea_activationOrder(const XMdiArea* self)
{ return self ? self->m_activationOrder : 0; }

/* ==================== Task 2.7：XMdiSubWindow 补充 API ==================== */

void XMdiSubWindow_setOption(XMdiSubWindow* self, int option, bool on)
{
    if (!self) return;
    if (on) self->m_options |= option;
    else self->m_options &= ~option;
}

bool XMdiSubWindow_testOption(const XMdiSubWindow* self, int option)
{
    return self ? (self->m_options & option) != 0 : false;
}

/* 对标 QMdiSubWindow::setKeyboardSingleStep/PageStep
 * （qmdisubwindow.cpp:2402-2407、2429-2434）：Qt 显式注释不做下界检查
 * ——负值表示反向步进、0 表示不动，均原样存储。此前本实现用
 * `step > 0` 门槛把 0/负值静默忽略（读回旧值），与 Qt 相悖。 */
void XMdiSubWindow_setKeyboardSingleStep(XMdiSubWindow* self, int step)
{ if (self) self->m_keyboardSingleStep = step; }

int XMdiSubWindow_keyboardSingleStep(const XMdiSubWindow* self)
{ return self ? self->m_keyboardSingleStep : 0; }

void XMdiSubWindow_setKeyboardPageStep(XMdiSubWindow* self, int step)
{ if (self) self->m_keyboardPageStep = step; }

int XMdiSubWindow_keyboardPageStep(const XMdiSubWindow* self)
{ return self ? self->m_keyboardPageStep : 0; }

bool XMdiSubWindow_isShaded(const XMdiSubWindow* self)
{ return self ? self->m_shaded : false; }

void XMdiSubWindow_showShaded(XMdiSubWindow* self)
{
    if (!self) return;
    /* 对标 QMdiSubWindow::showShaded（qmdisubwindow.cpp:2553-2561）：
     * ① 无父窗口直接返回——独立子窗不是 MDI 工作区窗口，折叠语义
     *    不适用（本机 Qt 6.8.0 探针：独立 QMdiSubWindow showShaded 后
     *    isShaded()==0、尺寸不变）；
     * ② 已是折叠态再调也返回——showShaded 不是开关，取消折叠走
     *    showNormal / setWindowState(NoState)（setNormalMode 分支，
     *    qmdisubwindow.cpp:1246-1288）。 */
    if (!XWidget_parentWidget((XWidget*)self)) return;
    if (self->m_shaded) return;
    xmdi_enterShaded(self);
}

void XMdiSubWindow_showSystemMenu(XMdiSubWindow* self)
{
    XPoint pos;
    if (!self || !self->m_systemMenu) return;
    XPoint_init(&pos, 4, XWidget_height((XWidget*)self) > 20 ? 18 : 0);
    XMenu_popup(self->m_systemMenu, &pos);
}

void XMdiSubWindow_setSystemMenu(XMdiSubWindow* self, XMenu* systemMenu)
{
    if (!self) return;
    if (self->m_systemMenu == systemMenu) return;
    if (self->m_systemMenu)
        XClassDelete((XClass*)self->m_systemMenu);
    self->m_systemMenu = systemMenu;
}

XMenu* XMdiSubWindow_systemMenu(const XMdiSubWindow* self)
{ return self ? self->m_systemMenu : NULL; }

XMdiArea* XMdiSubWindow_mdiArea(const XMdiSubWindow* self)
{
    XWidget* parent;
    XWidget* node;
    if (!self) return NULL;
    parent = XWidget_parentWidget((XWidget*)self);
    /* 对标 QMdiSubWindow::mdiArea（qmdisubwindow.cpp:2527-2538）：
     * 沿父链上溯，命中 XMdiArea 祖先且「本子窗直父=该区视口」才认
     * 归属。此前直呼 parent 强转 XMdiArea*——addSubWindow 实际把子
     * 窗挂在视口（qmdiarea.cpp:791-797 appendChild 同构），反查恒返
     * 视口指针，回归断言 "[MDI-FAIL] ext: mdiArea 反查" 即此根因。
     * 类判定经虚表类名（qobject_cast 的本框架等价，同 XStyle.c:710
     * 口径），非本框架裸指针强转。 */
    for (node = parent; node; node = XWidget_parentWidget(node)) {
        XVtable* vt = XClassGetVtable((XClass*)node);
        if (vt && XVTABLE_GET_NAME(vt) &&
            XStrcmp(XVTABLE_GET_NAME(vt), "XMdiArea") == 0) {
            XWidget* viewport = XAbstractScrollArea_viewport(
                (XAbstractScrollArea*)node);
            if (viewport == parent) return (XMdiArea*)node;
        }
    }
    return NULL;
}

/** @brief SizeHint 基类默认实现（`VX*` 槽体，注册虚表默认槽；对标
 *         QMdiSubWindow::sizeHint）。按《api命名风格》约束：业务逻辑
 *         只放 `VX*` 槽体；对外唯一入口是分派 `XWidget_sizeHint_base`
 *         （头文件别名 `XMdiSubWindow_sizeHint_base`）；子类槽内回落
 *         父类实现用 `XClass_Parent(XWidget, EXWidget_SizeHint, ...)`。 */
static XSize VXMdiSubWindow_sizeHint(const XWidget* self)
{
    XSize size;
    XSize minHint;
    XWidget* parent;
    bool decorated;
    int margin;
    int w;
    int h;
    const XMdiSubWindow* win = (const XMdiSubWindow*)self;
    if (!win) {
        XSize_init(&size, 0, 0);
        return size;
    }
    /* 对标 QMdiSubWindow::sizeHint（qmdisubwindow.cpp:3451-3462）：
     *   sizeParameters(&margin, &minWidth);  // 无父/无边框 → margin=0
     *   QSize size(2 * margin, titleBarHeight() + margin);
     *   if (baseWidget && baseWidget->sizeHint().isValid())
     *       size += baseWidget->sizeHint();
     *   return size.expandedTo(minimumSizeHint());
     * ——**没有** 200x150 下限（旧实现的下限已删除）。titleBarHeight()
     * 在无父/无边框时为 0（qmdisubwindow.cpp:1702-1708），本机探针：
     * 独立 QMdiSubWindow sizeHint=0x0；有父 + 内容 min(120,80) 时
     * 128x108（Fusion margin=4、titleBar=24）——本框架 chrome 取
     * margin=1（左右框线 + 底框线）、标题带 20。 */
    parent = XWidget_parentWidget((XWidget*)self);
    decorated = parent &&
                !(XWidget_windowFlags((XWidget*)self) &
                  (XWidgetFlags)XWindowType_FramelessWindowHint);
    margin = decorated ? XMDI_SUBWINDOW_FRAME_WIDTH : 0;
    w = 2 * margin;
    h = decorated ? XMDI_SUBWINDOW_TITLEBAR_HEIGHT + margin : 0;
    if (win->m_widget) {
        XSize ws = XWidget_sizeHint_base(win->m_widget);
        if (ws.width > 0 && ws.height > 0) {
            w += ws.width;
            h += ws.height;
        }
    }
    minHint = VXMdiSubWindow_minimumSizeHint(self);
    if (w < minHint.width) w = minHint.width;
    if (h < minHint.height) h = minHint.height;
    XSize_init(&size, w, h);
    return size;
}

/** @brief MinimumSizeHint 基类默认实现（`VX*` 槽体；命名契约同
 *         VXMdiSubWindow_sizeHint）。 */
static XSize VXMdiSubWindow_minimumSizeHint(const XWidget* self)
{
    XSize size;
    bool inArea;
    int margin;
    int titleBar;
    int minW;
    int minH;
    const XMdiSubWindow* win = (const XMdiSubWindow*)self;
    if (!win) {
        XSize_init(&size, 0, 0);
        return size;
    }
    /* 对标 QMdiSubWindow::minimumSizeHint（qmdisubwindow.cpp:3465-3510）：
     * ① **最小化图标窗优先**（:3469-3471）：
     *    `if (parent() && isMinimized() && !isShaded())
     *         return d->iconSize();`
     *    ——必须先于装饰/内容计算返回，否则最小化窗的最小提示会取到
     *    隐藏前的内容尺寸、与它实际缩成的图标尺寸矛盾。
     * ② sizeParameters 在**无父窗口**时 margin/minWidth 均为 0、标题带
     *    高度也为 0 ⇒ 独立子窗的最小尺寸只由内容件决定（实测 0x0）；
     * ③ 有父窗口时 = 标题带 + 内容件 minimumSizeHint（内容不可用时为
     *    标题带自身的最小宽，本实现取 chrome 的 50px 口径）；
     * ④ 折叠态只保留标题带高度、宽度取 max(最小宽, 当前宽)
     *    （:3480-3481）。
     * 此前恒返回固定 (50,20)，完全忽略内容件——内容最小尺寸大于窗口
     * 时布局会把内容压扁。 */
    inArea = XMdiSubWindow_mdiArea(win) != NULL;
    if (inArea && XWidget_isMinimized((const XWidget*)self) && !win->m_shaded) {
        XSize icon;
        xmdi_iconSize(win, &icon);
        if (icon.width > 0 && icon.height > 0)
            return icon;
    }
    margin = inArea ? XMDI_SUBWINDOW_FRAME_WIDTH : 0;
    titleBar = inArea ? XMDI_SUBWINDOW_TITLEBAR_HEIGHT : 0;
    if (inArea && win->m_shaded) {
        int w = XWidget_width((const XWidget*)self);
        if (w < XMDI_SUBWINDOW_CHROME_MIN_WIDTH)
            w = XMDI_SUBWINDOW_CHROME_MIN_WIDTH;
        XSize_init(&size, w, titleBar);
        return size;
    }
    minW = inArea ? XMDI_SUBWINDOW_CHROME_MIN_WIDTH : 0;
    minH = titleBar + margin;              /* 标题带 + 底框线 */
    if (win->m_widget) {
        XSize contentMin = XWidget_minimumSizeHint_base(win->m_widget);
        if (contentMin.width > 0)
            minW = (contentMin.width + 2 * margin > minW)
                       ? contentMin.width + 2 * margin : minW;
        if (contentMin.height > 0) minH += contentMin.height;
    }
    XSize_init(&size, minW, minH);
    return size;
}

XWidget* XMdiSubWindow_maximizedButtonsWidget(const XMdiSubWindow* self)
{ (void)self; return NULL; }

XWidget* XMdiSubWindow_maximizedSystemMenuIconWidget(
    const XMdiSubWindow* self)
{ (void)self; return NULL; }

/* ==================== Task 2.7：XMdiArea 补充 API ==================== */

void XMdiArea_setOption(XMdiArea* self, int option, bool on)
{
    if (!self) return;
    if (on) self->m_options |= option;
    else self->m_options &= ~option;
}

bool XMdiArea_testOption(const XMdiArea* self, int option)
{
    return self ? (self->m_options & option) != 0 : false;
}

void XMdiArea_setDocumentMode(XMdiArea* self, bool enabled)
{
    if (!self) return;
    self->m_documentMode = enabled;
    /* 【P3-53】Tabbed 视图下同步下发页签条（扁平/无基底绘制消费由
     * XTabBar 承载）。 */
#if XTABBAR_ON
    if (self->m_tabBar)
        XTabBar_setDocumentMode((XTabBar*)self->m_tabBar, enabled);
#endif
    XWidget_update((XWidget*)self);
}

bool XMdiArea_documentMode(const XMdiArea* self)
{ return self ? self->m_documentMode : false; }

void XMdiArea_setTabShape(XMdiArea* self, int shape)
{
    if (!self) return;
    self->m_tabShape = (uint8_t)shape;
    /* 【P3-53】Tabbed 视图下同步下发页签条（形状绘制消费由 XTabBar
     * 承载；越界值由 XTabBar_setShape 忽略）。 */
#if XTABBAR_ON
    if (self->m_tabBar)
        XTabBar_setShape((XTabBar*)self->m_tabBar, shape);
#endif
}

int XMdiArea_tabShape(const XMdiArea* self)
{ return self ? self->m_tabShape : 0; }

void XMdiArea_closeActiveSubWindow(XMdiArea* self)
{
    XMdiSubWindow* sw;
    int64_t i;
    bool deleteOnClose;
    if (!self) return;
    sw = self->m_active;
    if (!sw) return; /* qmdiarea.cpp:1824-1828：无活动窗直接返回 */
    deleteOnClose = XWidget_testAttribute((XWidget*)sw,
                                          XWidgetAttribute_DeleteOnClose);
    /* 对标 Qt：closeActiveSubWindow 即 `d->active->close()`——走关闭事件，
     * **被拒绝时保持激活**（旧实现无条件置 m_active=NULL 再删）。 */
    if (!XWidget_close((XWidget*)sw)) return;
    if (!deleteOnClose) {
        /* 非内部创建的子窗仅隐藏，仍留在 subWindowList（Qt 同）。 */
        xmdi_reactivateAfterRemove(self, sw);
        return;
    }
    i = xmdi_subWindowIndex(self, sw);
    if (i >= 0) XVector_remove_base(self->m_subWindows, i, 1);
    xmdi_historyRemove(self, sw);
    /* 对标 Qt：关闭活动窗口后激活剩余的第一个可见子窗口；无剩余则发
     * subWindowActivated(nullptr)（qmdiarea.cpp:1049-1126）。 */
    xmdi_reactivateAfterRemove(self, sw);
    /* 关闭路径可能由子窗口自身事件（closeEvent/按键）触发；同步删会在其
     * 事件帧返回途中释放自身，改延迟回收（对标 Qt deleteLater）。 */
    XObject_deleteLater((XObject*)sw);
    /* 子窗集合已变：页签视图同步页签（P3-53，幂等）。 */
    xmdi_tabBarSync(self);
}

/**
 * @brief      按当前激活顺序找相邻可见子窗口（对标
 *             QMdiAreaPrivate::nextVisibleSubWindow，qmdiarea.cpp:1393-1440）。
 * @details    `activateNextSubWindow`/`activatePreviousSubWindow` 在 Qt 里
 *             按 `activationOrder()` 选序（不是固定创建序）。无活动窗时
 *             Qt 取列表末项为当前项再步进（本机探针：a,b,c 无活动 +
 *             activateNext ⇒ a；activatePrevious ⇒ b）；候选隐藏则继续
 *             步进，绕回当前项仍未找到则返回 NULL。**Tabbed 视图**（P3-53）
 *             下隐藏是页签成员的常态，故不按隐藏过滤（否则 next/prev
 *             只会在活动窗自身绕圈）。
 * @param      self 目标区域。
 * @param      step +1=下一个，-1=上一个。
 * @return     下一个可见子窗口借用指针；无候选返回 NULL。
 */
static XMdiSubWindow* xmdi_nextVisibleSubWindow(XMdiArea* self, int step)
{
    XVector* list;
    int n;
    int idx = -1;
    int i;
    int candidate;
    bool tabbed;
    XMdiSubWindow* result = NULL;
    if (!self) return NULL;
    tabbed = (int)self->m_viewMode == (int)XMdiAreaViewMode_TabbedView;
    list = XMdiArea_subWindowList_2(self, self->m_activationOrder);
    if (!list) return NULL;
    n = (int)XVector_size_base((const XContainer*)list);
    if (n <= 0) {
        XClassDelete((XClass*)list);
        return NULL;
    }
    if (self->m_active) {
        for (i = 0; i < n; ++i) {
            XMdiSubWindow** slot =
                (XMdiSubWindow**)XVector_at_base(list, i);
            if (slot && *slot == self->m_active) {
                idx = i;
                break;
            }
        }
    }
    if (idx < 0) idx = n - 1; /* 无活动窗/活动窗不在列表：取末项 */
    candidate = idx;
    for (i = 0; i < n; ++i) {
        XMdiSubWindow** slot;
        candidate = ((candidate + step) % n + n) % n;
        slot = (XMdiSubWindow**)XVector_at_base(list, candidate);
        if (slot && *slot &&
            (tabbed || !XWidget_isHidden((XWidget*)*slot))) {
            result = *slot;
            break;
        }
        if (candidate == idx) break;
    }
    XClassDelete((XClass*)list);
    return result;
}

void XMdiArea_activateNextSubWindow(XMdiArea* self)
{
    XMdiSubWindow* next;
    if (!self) return;
    next = xmdi_nextVisibleSubWindow(self, 1);
    if (next) XMdiArea_setActiveSubWindow(self, next);
}

void XMdiArea_activatePreviousSubWindow(XMdiArea* self)
{
    XMdiSubWindow* prev;
    if (!self) return;
    prev = xmdi_nextVisibleSubWindow(self, -1);
    if (prev) XMdiArea_setActiveSubWindow(self, prev);
}

/* ==================== P3-51/52：sizeHint / minimumSizeHint ==================== */

/** @brief SizeHint 基类默认实现（`VX*` 槽体，注册虚表默认槽；对标
 *         QMdiArea::sizeHint）。按《api命名风格》约束：业务逻辑只放
 *         `VX*` 槽体；对外唯一入口是分派 `XWidget_sizeHint_base`
 *         （头文件别名 `XMdiArea_sizeHint_base`）；子类槽内回落父类
 *         实现用 `XClass_Parent(XWidget, EXWidget_SizeHint, ...)`。 */
static XSize VXmdiArea_sizeHint(const XWidget* self)
{
    XSize size;
    XSize desktop;
    XScreen* screen = NULL;
    XWidget* node;
    int nestedCount = 0;
    int scaleFactor;
    int64_t i;
    int64_t n;
    const XMdiArea* area = (const XMdiArea*)self;
    if (!area) {
        XSize_init(&size, 0, 0);
        return size;
    }
    /* 对标 qmdiarea.cpp:1715-1723：嵌套层数 = 父链上 XMdiArea 祖先
     * 个数（嵌套工作区适配）。类判定经虚表类名（本框架 qobject_cast
     * 等价，同 XMdiSubWindow_mdiArea 口径）。 */
    for (node = XWidget_parentWidget((const XWidget*)self); node;
         node = XWidget_parentWidget(node)) {
        XVtable* vt = XClassGetVtable((XClass*)node);
        if (vt && XVTABLE_GET_NAME(vt) &&
            XStrcmp(XVTABLE_GET_NAME(vt), "XMdiArea") == 0)
            ++nestedCount;
    }
    scaleFactor = 3 * (nestedCount + 1);
    /* 主屏虚拟桌面尺寸（对标 QGuiApplication::primaryScreen()->
     * virtualSize()）；主屏缺席按 (0,0) 处理（下方子窗提示扩展兜底）。 */
#if XGUIAPPLICATION_ON && XSCREEN_ON
    screen = XGuiApplication_primaryScreen();
#endif
    if (screen)
        desktop = XScreen_virtualSize(screen);
    else
        XSize_init(&desktop, 0, 0);
    XSize_init(&size,
               (int)((int64_t)desktop.width * 2 / scaleFactor),
               (int)((int64_t)desktop.height * 2 / scaleFactor));
    /* 逐子窗 expandedTo(child->sizeHint())（sanityCheck 等价：空槽跳过）。 */
    n = area->m_subWindows
            ? XVector_size_base((const XContainer*)area->m_subWindows) : 0;
    for (i = 0; i < n; ++i) {
        XMdiSubWindow* sw =
            XVector_At_Base(area->m_subWindows, i, XMdiSubWindow*);
        XSize hint;
        if (!sw) continue;
        /* 子窗 sizeHint 经虚表分派（XMdiSubWindow 已注册 SizeHint 槽）。 */
        hint = XWidget_sizeHint_base((const XWidget*)sw);
        size = XSize_expandedTo(&size, &hint);
    }
    return size;
}

/** @brief MinimumSizeHint 基类默认实现（`VX*` 槽体；命名契约同
 *         VXmdiArea_sizeHint）。 */
static XSize VXmdiArea_minimumSizeHint(const XWidget* self)
{
    XSize size;
    XSize base;
    XStyle* style;
    int pmMinW = 0;
    int pmTitleH = 0;
    bool scrollBarsEnabled;
    int64_t i;
    int64_t n;
    const XMdiArea* area = (const XMdiArea*)self;
    if (!area) {
        XSize_init(&size, 0, 0);
        return size;
    }
    /* 对标 qmdiarea.cpp:1739-1741：
     * QSize(PM_MdiSubWindowMinimizedWidth, PM_TitleBarHeight)。 */
    style = XWidget_style((const XWidget*)self);
    if (style) {
        pmMinW = XStyle_pixelMetric_base(
            style, (int)XStylePM_MdiSubWindowMinimizedWidth, NULL);
        pmTitleH = XStyle_pixelMetric_base(
            style, (int)XStylePM_TitleBarHeight, NULL);
    }
    XSize_init(&size, pmMinW, pmTitleH);
    /* 直调基类槽位实现（XAbstractScrollArea_class_init() 虚表）：不可走
     * 对象虚表——本类注册了本槽，会再入自身（无限递归）。 */
    base = XClass_Parent(XAbstractScrollArea, EXWidget_MinimumSizeHint,
                         XSize (*)(const XWidget*))((const XWidget*)self);
    size = XSize_expandedTo(&size, &base);
    /* 对标 scrollBarsEnabled()（qmdiarea.cpp:1289-1292）：水平或垂直
     * 策略非 AlwaysOff 即开；关闭时逐子窗并入 minimumSizeHint。 */
    scrollBarsEnabled =
        XAbstractScrollArea_horizontalScrollBarPolicy(
            (const XAbstractScrollArea*)self) !=
            XScrollBarPolicy_AlwaysOff ||
        XAbstractScrollArea_verticalScrollBarPolicy(
            (const XAbstractScrollArea*)self) !=
            XScrollBarPolicy_AlwaysOff;
    if (!scrollBarsEnabled) {
        n = area->m_subWindows
                ? XVector_size_base(
                      (const XContainer*)area->m_subWindows)
                : 0;
        for (i = 0; i < n; ++i) {
            XMdiSubWindow* sw =
                XVector_At_Base(area->m_subWindows, i, XMdiSubWindow*);
            XSize hint;
            if (!sw) continue;
            hint = XWidget_minimumSizeHint_base((const XWidget*)sw);
            size = XSize_expandedTo(&size, &hint);
        }
    }
    return size;
}

/* ==================== P3-53：Tabbed 视图（单一区域页签条） ==================== */

#if XTABBAR_ON

/** @brief 区域页签条高度（像素；与 XMainWindow 停靠页签条同口径 24，
 *         XTabBar_barHeightHint 可用时取其提示值）。 */
#define XMDI_AREA_TABBAR_H 24

/** @brief 页签条标题文本（子窗 windowTitle，对标 Qt tabTextFor）。
 * @param self 目标区域；不为 NULL。
 * @param index 子窗表下标。
 * @return UTF-8 借用指针；空标题返回空串。 */
static const char* xmdi_tabTitle(const XMdiArea* self, int64_t index)
{
    XMdiSubWindow* sw;
    const XString* title;
    sw = XVector_At_Base(self->m_subWindows, index, XMdiSubWindow*);
    if (!sw) return "";
    title = XWidget_windowTitle((const XWidget*)sw);
    if (title && XString_size((const XContainer*)title) > 0)
        return XString_toUtf8(title);
    return "";
}

/** @brief 由页签条上溯宿主区域（虚表类名指纹，判定口径同
 *         XMdiSubWindow_mdiArea）。 */
static XMdiArea* xmdi_areaFromTabBar(const XWidget* bar)
{
    XWidget* node;
    if (!bar) return NULL;
    for (node = XWidget_parentWidget((XWidget*)bar); node;
         node = XWidget_parentWidget(node)) {
        XVtable* vt = XClassGetVtable((XClass*)node);
        if (vt && XVTABLE_GET_NAME(vt) &&
            XStrcmp(XVTABLE_GET_NAME(vt), "XMdiArea") == 0)
            return (XMdiArea*)node;
    }
    return NULL;
}

/** @brief 页签条 currentChanged 转发槽（connect_2 槽首参=发送者）。
 *  @details 对标 qmdiarea.cpp:732-752 _q_currentTabChanged(index)：
 *           按页签下标取子窗并激活（activateWindow 同口径）。重入由
 *           m_inTabBarSync 守卫（sync 回填 current 时抑制）。 */
static void xmdi_tabBarCurrentChanged(XObject* sender, XVarList* args)
{
    XTabBar* bar = (XTabBar*)sender;
    XMdiArea* self = xmdi_areaFromTabBar((XWidget*)bar);
    const XVector* list;
    int64_t n;
    int64_t idx;
    XMdiSubWindow* sw;
    (void)args;
    if (!self || self->m_inTabBarSync) return;
    if ((int)self->m_viewMode != (int)XMdiAreaViewMode_TabbedView) return;
    idx = XTabBar_currentIndex(bar);
    list = XMdiArea_subWindowList(self);
    n = list ? XVector_size_base((const XContainer*)list) : 0;
    if (n <= 0) return;
    if (idx < 0) idx = 0;
    if (idx >= n) idx = n - 1;
    sw = XVector_At_Base(list, idx, XMdiSubWindow*);
    if (sw) XMdiArea_setActiveSubWindow(self, sw);
}

/** @brief 页签条 tabCloseRequested 转发槽。
 *  @details 对标 qmdiarea.cpp:756-764 _q_closeTab(index)：
 *           `childWindows.at(index)->close()`——关闭该页签对应的子窗
 *           本身（不先激活）。关闭链与标题带关闭按钮同源
 *           （xmdi_closeFromTitleBar：拒绝即保持、DeleteOnClose 收口）。 */
static void xmdi_tabBarCloseRequested(XObject* sender, XVarList* args)
{
    XTabBar* bar = (XTabBar*)sender;
    XMdiArea* self = xmdi_areaFromTabBar((XWidget*)bar);
    const XVector* list;
    int64_t n;
    int idx = -1;
    XMdiSubWindow* sw;
    if (!self || self->m_inTabBarSync) return;
    if ((int)self->m_viewMode != (int)XMdiAreaViewMode_TabbedView) return;
    if (args) {
        XVarList_start(args);
        idx = XVarList_arg(args, int);
    }
    list = XMdiArea_subWindowList(self);
    n = list ? XVector_size_base((const XContainer*)list) : 0;
    if (idx < 0 || idx >= (int)n) return;
    sw = XVector_At_Base(list, idx, XMdiSubWindow*);
    if (!sw) return;
    xmdi_closeFromTitleBar(sw);
    /* 关闭可能摘窗（DeleteOnClose）或仅隐藏（接管窗）：页签与可见性
     * 随之收口。 */
    xmdi_tabBarSync(self);
}

/** @brief 页签条 tabMoved 转发槽。
 *  @details 对标 qmdiarea.cpp:766-785 _q_moveTab(from,to)：
 *           `childWindows.move(from,to)`——页签条已自行换位，这里只把
 *           子窗表按同序平移，保持「页签下标 ↔ 子窗下标」映射。 */
static void xmdi_tabBarMoved(XObject* sender, XVarList* args)
{
    XTabBar* bar = (XTabBar*)sender;
    XMdiArea* self = xmdi_areaFromTabBar((XWidget*)bar);
    int from = 0;
    int to = 0;
    int64_t n;
    if (!self || self->m_inTabBarSync) return;
    if ((int)self->m_viewMode != (int)XMdiAreaViewMode_TabbedView) return;
    if (args) {
        XVarList_start(args);
        from = XVarList_arg(args, int);
        to = XVarList_arg(args, int);
    }
    if (from < 0 || to < 0 || from == to) return;
    n = self->m_subWindows
            ? XVector_size_base((const XContainer*)self->m_subWindows) : 0;
    if (from >= (int)n || to >= (int)n) return;
    XVector_move(self->m_subWindows, from, to);
    /* 活动窗仍按指针跟踪；仅当激活序需要前置时由 Qt 的
     * indicesToActivatedChildren.move 口径保证——本实现按指针记账，
     * 平移子窗表不影响激活史。 */
}

#endif /* XTABBAR_ON */

/** @brief Tabbed 视图的页签条装配与布局（P3-53）。
 *  @details 单一 XTabBar 作为区域页签条（对标 Qt QMdiAreaTabBar）：
 *           页签 = 子窗创建序列表（与 subWindowList CreationOrder 同
 *           序，页签拖动经 tabMoved 平移子窗表保持映射）；
 *           currentChanged → setActiveSubWindow、tabCloseRequested →
 *           关闭对应子窗；仅活动子窗可见（其余隐藏，对标 Qt 页签视图
 *           一页一窗）。布局对标 updateTabBarGeometry：页签条占
 *           contentsRect 顶/底条带 + setViewportMargins 让位，活动子窗
 *           铺满让位后的视口（对标 showMaximized-in-area）；South 时
 *           条带在底，West/East 因页签条为水平单行（P0-01 前置）退化
 *           北向。SubWindowView：撤条 + 还边距（子窗可见性恢复在
 *           setViewMode 切换分支，避免覆盖 closeAll 的隐藏语义）。
 *           XTABBAR_ON=0：页签条不可用，视图模式退化为属性存储（与
 *           本批前行为一致）。 */
static void xmdi_tabBarSync(XMdiArea* self)
{
    int64_t n;
    int64_t i;
    if (!self) return;
    /* 重入保护：setViewportMargins 内部同步触发区域 resizeEvent，后者
     * 在 Tabbed 视图再调本函数；信号回填同理。整个同步体单飞。 */
    if (self->m_inTabBarSync) return;
    if ((int)self->m_viewMode != (int)XMdiAreaViewMode_TabbedView) {
        /* SubWindowView：撤条（隐藏 + 视口边距还原）。 */
        XAbstractScrollArea_setViewportMargins((XAbstractScrollArea*)self,
                                               0, 0, 0, 0);
#if XTABBAR_ON
        if (self->m_tabBar && !XWidget_isHidden(self->m_tabBar))
            XWidget_hide(self->m_tabBar);
#endif
        return;
    }
#if XTABBAR_ON
    self->m_inTabBarSync = true;
    {
        XTabBar* bar;
        int64_t activeIdx;
        int want;
        /* 页签条惰性装配（区域子控件，随区域级联销毁）。 */
        if (!self->m_tabBar) {
            XTabBar* created = XTabBar_create_ex(XCLASS_DEFAULT_MEMORY_TYPE,
                                                 (XWidget*)self, 0);
            if (!created) {
                self->m_inTabBarSync = false;
                return;
            }
            XObject_connect_2(
                (XObject*)created,
                (size_t)XTabBar_currentChanged_signal(created, 0),
                xmdi_tabBarCurrentChanged);
            XObject_connect_2(
                (XObject*)created,
                (size_t)XTabBar_tabCloseRequested_signal(created),
                xmdi_tabBarCloseRequested);
            XObject_connect_2(
                (XObject*)created,
                (size_t)XTabBar_tabMoved_signal(created, 0, 0),
                xmdi_tabBarMoved);
            self->m_tabBar = (XWidget*)created;
        }
        bar = (XTabBar*)self->m_tabBar;
        /* 属性下发（对标 Qt setViewMode 建条四连发 + 运行期 setter）。 */
        XTabBar_setTabsClosable(bar, self->m_tabsClosable != 0);
        XTabBar_setMovable(bar, self->m_tabsMovable != 0);
        XTabBar_setDocumentMode(bar, self->m_documentMode != 0);
        XTabBar_setShape(bar, (int)self->m_tabShape);
        /* 页签回填：子窗数量变化时重建；数量一致时按差异刷新标题
         * （对标 tabTextFor；setTabText 无同值早退，先比较再写避免
         * 每次同步都触发页签条布局失效）。 */
        n = self->m_subWindows
                ? XVector_size_base(
                      (const XContainer*)self->m_subWindows)
                : 0;
        if (XTabBar_count(bar) != (int)n) {
            while (XTabBar_count(bar) > 0)
                XTabBar_removeTab(bar, 0);
            for (i = 0; i < n; ++i)
                XTabBar_addTab_2(bar, xmdi_tabTitle(self, i));
        } else {
            for (i = 0; i < n; ++i) {
                const char* want = xmdi_tabTitle(self, i);
                const char* cur = XTabBar_tabText_2(bar, (int)i);
                if (want && cur && XStrcmp(want, cur) != 0)
                    XTabBar_setTabText_2(bar, (int)i, want);
            }
        }
        /* 活动下标与页签条一致（回填经重入保护）。无活动窗时页签落
         * 首个下标（setCurrentIndex 对空表早退，无信号副作用）。 */
        activeIdx = self->m_active
                        ? xmdi_subWindowIndex(self, self->m_active) : -1;
        want = (activeIdx >= 0 && activeIdx < n) ? (int)activeIdx : 0;
        if (n > 0 && XTabBar_currentIndex(bar) != want)
            XTabBar_setCurrentIndex(bar, want);
        /* 视口边距 + 页签条几何（对标 updateTabBarGeometry）。 */
        {
            XRect contents = XWidget_contentsRect((XWidget*)self);
            XRect barRect;
            int h = XTabBar_barHeightHint(
                bar, contents.width > 0 ? contents.width : 0);
            if (h <= 0) h = XMDI_AREA_TABBAR_H;
            if (self->m_tabPosition == 1 /* South（QTabWidget::South）*/) {
                XAbstractScrollArea_setViewportMargins(
                    (XAbstractScrollArea*)self, 0, 0, 0, h);
                XRect_init(&barRect, contents.x,
                           contents.y + contents.height - h,
                           contents.width, h);
            } else {
                /* North 缺省；West/East 水平条退化北向（P0-01 前置）。 */
                XAbstractScrollArea_setViewportMargins(
                    (XAbstractScrollArea*)self, 0, h, 0, 0);
                XRect_init(&barRect, contents.x, contents.y,
                           contents.width, h);
            }
            XWidget_setGeometryRect(self->m_tabBar, &barRect);
            /* 同值早退由本侧先行：XWidget_setVisible 无同值早退（每次
             * 都激活父布局），显式隐藏位才是判据。 */
            if (XWidget_isHidden(self->m_tabBar)) {
                XWidget_show(self->m_tabBar);
                XWidget_raise(self->m_tabBar); /* 首显提到视口之上 */
            }
        }
        /* 可见性：仅活动子窗可见。判据用**显式隐藏位**（isHidden）而非
         * 有效可见性：区域本身未显示时全部子窗 isVisible 恒假，按
         * isVisible 比较会每次同步都重复 setVisible（该入口无同值早退，
         * 每拍一次布局失效 ⇒ 风暴）；显式位才是本机制要维护的状态。 */
        for (i = 0; i < n; ++i) {
            XMdiSubWindow* sw =
                XVector_At_Base(self->m_subWindows, i, XMdiSubWindow*);
            bool wantVisible = sw && sw == self->m_active;
            if (!sw) continue;
            if (wantVisible && XWidget_isHidden((XWidget*)sw))
                XWidget_show((XWidget*)sw);
            else if (!wantVisible && !XWidget_isHidden((XWidget*)sw))
                XWidget_hide((XWidget*)sw);
        }
        /* 活动子窗铺满让位后的视口（对标 Qt 页签视图对当前窗
         * showMaximized：qmdiarea.cpp:1565-1570；退出时按
         * m_tabbedRestore 快照回放）。 */
        if (self->m_active) {
            XWidget* viewport =
                XAbstractScrollArea_viewport((XAbstractScrollArea*)self);
            XRect vp = viewport ? XWidget_geometry(viewport)
                                : XWidget_contentsRect((XWidget*)self);
            XRect wantRect;
            XRect cur = XWidget_geometry((XWidget*)self->m_active);
            XRect_init(&wantRect, 0, 0,
                       vp.width > 0 ? vp.width : 0,
                       vp.height > 0 ? vp.height : 0);
            if (cur.x != wantRect.x || cur.y != wantRect.y ||
                cur.width != wantRect.width ||
                cur.height != wantRect.height)
                XWidget_setGeometryRect((XWidget*)self->m_active,
                                        &wantRect);
        }
    }
    self->m_inTabBarSync = false;
#else
    (void)n;
    (void)i;
#endif /* XTABBAR_ON */
}

#endif /* XWIDGET_ON && XABSTRACTSCROLLAREA_ON && XMDIAREA_ON */

#endif /* XGUI_ON */
