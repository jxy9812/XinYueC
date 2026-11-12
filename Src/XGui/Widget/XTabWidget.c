/**
 * @file       XTabWidget.c
 * @brief      XTabWidget 选项卡容器实现（对标 Qt 6.8 QTabWidget 子集）。
 * @details    页签条随 tabPosition 摆放（North/South/West/East，高
 *             XTABBAR_TAB_H），页容器占其余区域；每页是一个内部
 *             XWidget（页内容控件以它为 parent 放入，借用记录于
 *             m_clients 便于 indexOf；removeTab 时摘还调用方，页容器
 *             延迟销毁）。切页 = 显示当前页容器 + 隐藏其余。
 * @author     XinYueC 团队
 ******************************************************************************/
#if XGUI_ON

#include "CXinYueConfig.h"

#include "XAlgorithm.h"
#if XWIDGET_ON && XTABBAR_ON && XTABWIDGET_ON

#include "XTabWidget.h"
#include "XTabWidget_Protected.h"
#include "XAbstractItemView.h" /* [对齐 Qt 2026-11-03] elide 枚举缺省值。 */
#include "XWidget_Protected.h"
#include "XMemory.h"
#include "XEvent.h"
#include "XCoreApplication.h"

#define XTABWIDGET_BAR_H 24
/** @brief 页签条首选尺寸上限（usesScrollButtons 时的 200x200，
 *         对标 qtabwidget.cpp:863-864 `t.boundedTo(QSize(200,200))`）。 */
#define XTABWIDGET_BAR_HINT_LIMIT 200

/* ==================== 前向声明 ==================== */
static void VXTabWidget_resizeEvent(XWidget* self, XEvent* event);
static void VXTabWidget_changeEvent(XWidget* self, XEvent* event);
static void VXTabWidget_copy(XTabWidget* self, const XTabWidget* other);
static void VXTabWidget_move(XTabWidget* self, XTabWidget* other);
static void xtabwidget_currentChangedForward(void* sender, XVarList* args);
static void xtabwidget_emitIntForward(XTabWidget* self);
static void xtabwidget_tabMovedForward(void* sender, XVarList* args);
static void xtabwidget_tabBarClickedForward(void* sender, XVarList* args);
static void xtabwidget_tabBarDoubleClickedForward(void* sender, XVarList* args);
static void xtabwidget_tabCloseRequestedForward(void* sender, XVarList* args);
static void xtabwidget_updateTabBarPosition(XTabWidget* self); /* [P0-01] */

/* ==================== 内部辅助 ==================== */

/** @brief 分配页容器与页签几何（随 tabPosition 摆放，对标
 *  QTabWidgetPrivate::updateTabBarPosition → setUpLayout）。 */
static void xtabwidget_layout(XTabWidget* self)
{
    int w = XWidget_width((XWidget*)self);
    int h = XWidget_height((XWidget*)self);
    int barH;
    int barX = 0;
    int barY = 0;
    int barW;
    int pageX = 0;
    int pageY = 0;
    int pageW;
    int pageH;
    int i;
    {   /* 页签条高度由 XTabBar 统一提供：溢出滚动模式恒单行(24)，
           否则按换行布局行数计（此前本地复算与滚动模式会漂移）。
           autoHide 且单页时条已隐藏（xtabbar_autoHideTabs），页面区
           占满全高（对标 Qt setUpLayout 随条显隐重算）。 */
        barH = XTabBar_barHeightHint(self->m_tabBarActive, w);
        if (barH < 1) barH = 24; /* 24 = XTABBAR_TAB_H 兜底。 */
        if (self->m_tabBarActive->m_autoHide && self->m_tabBarActive->m_count <= 1)
            barH = 0;
    }
    barW = w;
    pageW = w;
    pageH = h;
    /* 页签条四向摆放（North/South 横条占行；West/East 竖条贴缘占全高，
     * [P0-01 2026-11-12] 此前条几何高只取条高提示 24（水平绘制时代的
     * 简化承载），竖排页签自第 2 页起全部越出条外——现在条几何 =
     * 厚度(barHeightHint) × 全高，页区占横向余量；页签条形状由
     * setTabPosition 经 updateTabBarPosition 映射成 RoundedWest/East
     * （XTabBar 据此竖排布局/命中/文本旋转），对标 Qt setUpLayout 的
     * West/East 分支与 updateTabBarPosition（qtabwidget.cpp:960-982）。 */
    switch (self->m_tabPosition) {
    case 1: /* South：条在底，页占上余。 */
        barY = h - barH;
        pageH = h - barH;
        break;
    case 2: /* West：条贴左缘竖条（厚=barHeightHint，高=全高），页占右余。 */
        barW = barH;
        pageX = barH;
        pageW = w - barH;
        break;
    case 3: /* East：条贴右缘竖条（厚=barHeightHint，高=全高），页占左余。 */
        barX = w - barH;
        barW = barH;
        pageW = w - barH;
        break;
    default: /* North：条在顶，页占下余。 */
        /* [根修 2026-10-17] pageY 必须=barH：此前漏置（pageY 恒 0），
         * 页容器从条下 0 位起排=与页签条重叠（对标 Qt setUpLayout
         * North 的 QRect(0, barH, w, h-barH)；目验：日历签月头嵌进
         * 条行、滚动签白底整块盖条）。South/West/East 三分支本就正确。 */
        pageY = barH;
        pageH = h - barH;
        break;
    }
    if (pageW < 1) pageW = 1;
    if (pageH < 1) pageH = 1;
    /* West/East 的条高 = 容器全高（竖条）；North/South 保持条行高。 */
    XWidget_setGeometry((XWidget*)self->m_tabBarActive, barX, barY, barW,
                        (self->m_tabPosition >= 2) ? h : barH);
    for (i = 0; i < self->m_count; ++i) {
        if (self->m_pages[i])
            XWidget_setGeometry(self->m_pages[i], pageX, pageY, pageW, pageH);
        /* 用户内容控件（client）必须铺满页容器，否则停留在默认几何
           (0,0,100,30)，子控件坐标越界导致 childAt 命中失败、鼠标
           事件永远到不了内容（对标 QStackedLayout 填满几何语义）。 */
        if (self->m_clients[i])
            XWidget_setGeometry(self->m_clients[i], 0, 0, pageW, pageH);
    }
    /* 角部件（对标 QTabWidget 角部件）：两槽位（左/右）置于页签条行内，按
       其当前尺寸放置（超出时截断），高度以页签条高为上限；Qt 以
       sizeHint 并在样式中为页签预留空间，此处未接入预留逻辑，页签
       可能与角部件重叠。South 时随条行沉底；West/East 维持顶行
       （竖条已全高承载后角部件叠在条顶部，Qt 中角部件本就只对
       North/South 有布局语义，West/East 属预留）。 */
    {
        XWidget* left = self->m_cornerWidgetLeft;
        XWidget* right = self->m_cornerWidgetRight;
        int cornerY = (self->m_tabPosition == 1) ? barY : 0;
        int cw;
        int ch;
        if (left) {
            cw = XWidget_width(left);
            ch = XWidget_height(left);
            if (cw < 1) cw = 1;
            if (cw > w) cw = w;
            if (ch > barH) ch = barH;
            XWidget_setGeometry(left, 0, cornerY, cw, ch);
        }
        if (right) {
            cw = XWidget_width(right);
            ch = XWidget_height(right);
            if (cw < 1) cw = 1;
            if (cw > w) cw = w;
            if (ch > barH) ch = barH;
            XWidget_setGeometry(right, w - cw, cornerY, cw, ch);
        }
    }
}

/** @brief 切页：仅显示当前页容器。 */
static void xtabwidget_showCurrent(XTabWidget* self)
{
    int i;
    for (i = 0; i < self->m_count; ++i) {
        if (self->m_pages[i]) {
            bool show = (i == self->m_currentIndex);
            XWidget_setVisible(self->m_pages[i], show);
            /* 内容控件显式 show：reparent 后 m_explicitShow=0 不会随
             * 页容器可见而自动显示（对标 QWidget::setVisible 语义）。 */
            if (show && self->m_clients[i])
                XWidget_show(self->m_clients[i]);
        }
    }
    XWidget_update((XWidget*)self);
}

/** @brief 页签条 currentChanged 转发槽（同步 m_currentIndex 与显示页）。
 *  @note  connect_2 的槽首参是发送者（页签条本身）——沿 parentWidget
 *         上溯到所属 XTabWidget 后再同步。 */
static void xtabwidget_currentChangedForward(void* sender, XVarList* args)
{
    XWidget* bar = (XWidget*)sender;
    XWidget* owner = bar ? XWidget_parentWidget(bar) : NULL;
    XTabWidget* self = owner ? (XTabWidget*)owner : NULL;
    (void)args;
    /* 退休条（setTabBar 后被替换的旧条）的残留连接不改本容器状态。 */
    if (!self || (XTabBar*)sender != self->m_tabBarActive) return;
    self->m_currentIndex = self->m_tabBarActive->m_currentIndex;
    xtabwidget_showCurrent(self);
    if (((XObject*)self)->m_signalSlot)
        xtabwidget_emitIntForward(self);
}

/** @brief 转发 currentChanged 给本控件订阅者。 */
static void xtabwidget_emitIntForward(XTabWidget* self)
{
    XVarList* arguments = XVarList_Create(XVar(int, self->m_currentIndex));
    if (!arguments) return;
    if (((XObject*)self)->m_signalSlot) {
        XObject_emitSignal((XObject*)self,
                           (size_t)XTabWidget_currentChanged_signal(
                               self, self->m_currentIndex),
                           arguments, NULL, NULL, XEVENT_PRIORITY_NORMAL);
    } else {
        XVarList_delete(arguments);
    }
}

/**
 * @brief      从页签条上溯所属 XTabWidget。
 * @param      sender 页签条对象（connect_2 槽首参）。
 * @return     所属容器；上溯失败或该条已不是当前生效条返回 NULL。
 */
static XTabWidget* xtabwidget_ownerOf(void* sender)
{
    XWidget* bar = (XWidget*)sender;
    XWidget* owner = bar ? XWidget_parentWidget(bar) : NULL;
    XTabWidget* self = owner ? (XTabWidget*)owner : NULL;
    /* setTabBar 换过条后，旧条若仍是子控件（内嵌缺省条只隐藏不销毁），
     * 其转发连接仍在——按「发送者必须等于当前生效条」滤掉，避免退休条
     * 的事件改到本容器的页与当前索引。 */
    if (!self || (XTabBar*)sender != self->m_tabBarActive) return NULL;
    return self;
}

/** @brief 页签条 tabMoved 转发槽：页/内容平行数组随换位重排（对标
 *  QTabWidgetPrivate::tabMoved，qtabwidget.cpp:771-777：stack 页从
 *  from 摘除再插入 to；setMovable 打开拖拽后页签序与页数组序才能
 *  保持一致）。不转发信号——换位致当前索引变化时页签条自身补发的
 *  currentChanged 经既有转发槽同步容器当前页（对标 QTabBar::moveTab
 *  尾部 qtabbar.cpp:2087-2089）。 */
static void xtabwidget_tabMovedForward(void* sender, XVarList* args)
{
    XTabWidget* self = xtabwidget_ownerOf(sender);
    int from = 0;
    int to = 0;
    XWidget* page;
    XWidget* client;
    if (!self || !args) return;
    XVarList_start(args);
    from = XVarList_arg(args, int);
    to = XVarList_arg(args, int);
    if (from < 0 || to < 0 || from >= self->m_count || to >= self->m_count ||
        from == to)
        return;
    page = self->m_pages[from];
    client = self->m_clients[from];
    if (from < to) {
        XMemmove(&self->m_pages[from], &self->m_pages[from + 1],
                 sizeof(XWidget*) * (size_t)(to - from));
        XMemmove(&self->m_clients[from], &self->m_clients[from + 1],
                 sizeof(XWidget*) * (size_t)(to - from));
    } else {
        XMemmove(&self->m_pages[to + 1], &self->m_pages[to],
                 sizeof(XWidget*) * (size_t)(from - to));
        XMemmove(&self->m_clients[to + 1], &self->m_clients[to],
                 sizeof(XWidget*) * (size_t)(from - to));
    }
    self->m_pages[to] = page;
    self->m_clients[to] = client;
    self->m_currentIndex = self->m_tabBarActive->m_currentIndex;
    xtabwidget_layout(self);
    xtabwidget_showCurrent(self);
}

/** @brief 转发页签单击（tabClicked → tabBarClicked(int)）。 */
static void xtabwidget_tabBarClickedForward(void* sender, XVarList* args)
{
    XTabWidget* self = xtabwidget_ownerOf(sender);
    int idx;
    XVarList* arguments;

    if (!self) return;
    idx = 0;
    if (args) {
        XVarList_start(args);
        idx = XVarList_arg(args, int);
    }
    if (!((XObject*)self)->m_signalSlot) return;
    arguments = XVarList_Create(XVar(int, idx));
    if (!arguments) return;
    XObject_emitSignal((XObject*)self,
                       (size_t)XTabWidget_tabBarClicked_signal,
                       arguments, NULL, NULL, XEVENT_PRIORITY_NORMAL);
}

/** @brief 转发页签双击（tabDoubleClicked → tabBarDoubleClicked(int)）。 */
static void xtabwidget_tabBarDoubleClickedForward(void* sender, XVarList* args)
{
    XTabWidget* self = xtabwidget_ownerOf(sender);
    int idx;
    XVarList* arguments;

    if (!self) return;
    idx = 0;
    if (args) {
        XVarList_start(args);
        idx = XVarList_arg(args, int);
    }
    if (!((XObject*)self)->m_signalSlot) return;
    arguments = XVarList_Create(XVar(int, idx));
    if (!arguments) return;
    XObject_emitSignal((XObject*)self,
                       (size_t)XTabWidget_tabBarDoubleClicked_signal,
                       arguments, NULL, NULL, XEVENT_PRIORITY_NORMAL);
}

/** @brief 转发页签关闭请求（tabCloseRequested(int)）。
 *  @note  发射点为“tabsClosable 且点击页签关闭按钮”，位于 XTabBar 侧
 *         （对标 QTabBar::tabCloseRequested）。XTabBar 的关闭按钮为
 *         每页真实子控件（xtabbar_syncCloseButtons 创建、点击发
 *         tabCloseRequested），本转发为**真发射**通路：容器订阅者经
 *         XTabWidget_tabCloseRequested_signal 收到页签索引。 */
static void xtabwidget_tabCloseRequestedForward(void* sender, XVarList* args)
{
    XTabWidget* self = xtabwidget_ownerOf(sender);
    int idx;
    XVarList* arguments;

    if (!self) return;
    idx = 0;
    if (args) {
        XVarList_start(args);
        idx = XVarList_arg(args, int);
    }
    if (!((XObject*)self)->m_signalSlot) return;
    arguments = XVarList_Create(XVar(int, idx));
    if (!arguments) return;
    XObject_emitSignal((XObject*)self,
                       (size_t)XTabWidget_tabCloseRequested_signal,
                       arguments, NULL, NULL, XEVENT_PRIORITY_NORMAL);
}

/* ==================== 虚槽实现 ==================== */

static void VXTabWidget_resizeEvent(XWidget* self, XEvent* event)
{
    XTabWidget* tw = (XTabWidget*)self;
    if (!tw || !event || XEvent_type(event) != XEVENT_TYPE_RESIZE) return;
    xtabwidget_layout(tw);
    XEvent_ignore(event);
}

static void VXTabWidget_changeEvent(XWidget* self, XEvent* event)
{
    XEvent_ignore(event);
    (void)self;
}

static void VXTabWidget_copy(XTabWidget* self, const XTabWidget* other)
{
    if (!self || !other || self == other) return;
    if (XClassIsVtableNull(self)) XTabWidget_init(self, NULL, 0);
    XClass_Parent(XWidget, EXClass_Copy,
                  void(*)(XWidget*, const XWidget*))((XWidget*)self,
                                                     (const XWidget*)other);
    /* 页与内容控件不可复制（Qt 同语义：仅结构属性拷贝）。 */
    self->m_tabPosition = other->m_tabPosition;
    self->m_tabsClosable = other->m_tabsClosable;
    self->m_movable = other->m_movable;
    /* 角部件为借用指针，不随 copy 转移：清空本侧登记，避免悬挂。 */
    self->m_cornerWidgetLeft = NULL;
    self->m_cornerWidgetRight = NULL;
}

static void VXTabWidget_move(XTabWidget* self, XTabWidget* other)
{
    if (!self || !other || self == other) return;
    if (XClassIsVtableNull(self)) XTabWidget_init(self, NULL, 0);
    XClass_Parent(XWidget, EXClass_Move,
                  void(*)(XWidget*, XWidget*))((XWidget*)self,
                                               (XWidget*)other);
    self->m_tabPosition = other->m_tabPosition;
    self->m_tabsClosable = other->m_tabsClosable;
    self->m_movable = other->m_movable;
    /* 角部件借用指针随 move 整体转移，源侧清空防双持。 */
    self->m_cornerWidgetLeft = other->m_cornerWidgetLeft;
    self->m_cornerWidgetRight = other->m_cornerWidgetRight;
    other->m_cornerWidgetLeft = NULL;
    other->m_cornerWidgetRight = NULL;
    other->m_tabPosition = 0;
    other->m_tabsClosable = false;
    other->m_movable = false;
}

/* ==================== 生命周期 ==================== */

static void VXTabWidget_deinit(XTabWidget* self)
{
    int i;
    int count;
    if (!self) return;
    count = self->m_count;
    /* [析构期安全 2026-11-11] hfw 回调持有本对象且会遍历 m_clients：
     * 页容器销毁时其子页（= m_clients 项）随之释放，若基类析构链上有
     * 布局查询 heightForWidth，就会读到悬垂指针。先摘回调并把页数归零
     * （遍历立即结束），再按捕获的页数回收页容器。 */
    XWidget_setHeightForWidthHandler((XWidget*)self, NULL, NULL);
    self->m_count = 0;
    if (self->m_pages) {
        /* 页容器为堆对象（insertTab 处 XMemory_malloc + Set_Class_Memory +
           IsHeap(true)）：统一经 XClassDelete 虚析构并按登记分配器归还
           本体（同 removeTab 的 deleteLater → delete_base 释放路径）。 */
        for (i = 0; i < count; ++i) {
            if (self->m_pages[i]) {
                XClassDelete((XClass*)self->m_pages[i]);
                self->m_pages[i] = NULL;
            }
        }
        XFree_System(self->m_pages);
        self->m_pages = NULL;
    }
    if (self->m_clients) {
        XFree_System(self->m_clients);
        self->m_clients = NULL;
    }
    /* 角部件登记为借用记录：此处仅解除登记；部件本体经由父子链随
       下方 XWidget 基类析构级联释放（见 setCornerWidget 注）。 */
    self->m_cornerWidgetLeft = NULL;
    self->m_cornerWidgetRight = NULL;
    XClass_Deinit_Parent(XWidget, (XWidget*)self);
}

/* update/size 计算函数定义在后段，此处先声明供槽桥接调用。 */
static XSize xtabwidget_computeSizeHint(const XTabWidget* self);
static XSize xtabwidget_computeMinimumSizeHint(const XTabWidget* self);

/* SizeHint 虚槽桥接（对标 Qt 的 sizeHint/minimumSizeHint 虚函数）：
   查询即现算（页/页签条/角部件任一变化都反映到结果，不依赖缓存）。 */
static XSize VXTabWidget_sizeHintSlot(const XWidget* self);
static XSize VXTabWidget_minimumSizeHintSlot(const XWidget* self);

static XSize VXTabWidget_sizeHintSlot(const XWidget* self)
{
    if (!self) { XSize out; XSize_init(&out, -1, -1); return out; }
    return xtabwidget_computeSizeHint((const XTabWidget*)self);
}

static XSize VXTabWidget_minimumSizeHintSlot(const XWidget* self)
{
    if (!self) { XSize out; XSize_init(&out, -1, -1); return out; }
    return xtabwidget_computeMinimumSizeHint((const XTabWidget*)self);
}

XVtable* XTabWidget_class_init(void)
{
    XVTABLE_INIT_DEFAULT(XTabWidget)
    XVTABLE_INHERIT_XCLASS(XWidget);
    XVTABLE_OVERLOAD_DEFAULT(EXWidget_SizeHint, VXTabWidget_sizeHintSlot);
    XVTABLE_OVERLOAD_DEFAULT(EXWidget_MinimumSizeHint, VXTabWidget_minimumSizeHintSlot);

    XVTABLE_OVERLOAD_DEFAULT(EXWidget_ResizeEvent, VXTabWidget_resizeEvent);
    XVTABLE_OVERLOAD_DEFAULT(EXWidget_ChangeEvent, VXTabWidget_changeEvent);
    /* [Qt 对齐 2026-11-11] 页增删保护虚槽缺省实现（空操作）。 */
    XVTABLE_OVERLOAD_DEFAULT(EXTabWidget_TabInserted,
                             XTabWidget_tabInserted_base);
    XVTABLE_OVERLOAD_DEFAULT(EXTabWidget_TabRemoved,
                             XTabWidget_tabRemoved_base);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Deinit, VXTabWidget_deinit);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Copy, VXTabWidget_copy);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Move, VXTabWidget_move);

    return XVTABLE_DEFAULT;
}

/* ==================== 尺寸提示（对标 Qt basicSize 家族） ====================
 * [Qt 对齐 2026-11-11] Qt 6.8 的 QTabWidget::sizeHint/minimumSizeHint/
 * heightForWidth 共用 basicSize()（qtabwidget.cpp:802-811）：
 *   横放（North/South）：( max(s.w, t.w+rc.w+lc.w),
 *                          s.h + max(rc.h, max(lc.h, t.h)) )
 *   竖放（West/East）  ：( s.w + max(rc.w, max(lc.w, t.w)),
 *                          max(s.h, t.h+rc.h+lc.h) )
 * 其中 s = 页尺寸、t = 页签条尺寸、lc/rc = 左上/右上角部件尺寸。
 * 页尺寸取**所有可见页签的页**（此前只取当前页 ⇒ 切页后首选尺寸跳动，
 * 且隐藏页签仍被计入）；页容器是内部空控件，故读用户内容控件 m_clients。 */

/** @brief 三值取大。 */
static int xtabwidget_max3(int a, int b, int c)
{
    int m = a > b ? a : b;
    return m > c ? m : c;
}

/** @brief 页签条是否处于「自动隐藏生效」态（对标 isAutoHidden，
 *         qtabwidget.cpp:163-167：autoHide 且页数 ≤ 1）。 */
static bool xtabwidget_isAutoHidden(const XTabWidget* self)
{
    return self->m_tabBarActive->m_autoHide && self->m_tabBarActive->m_count <= 1;
}

/** @brief 页签条尺寸提示（自动隐藏 ⇒ (0,0)；usesScrollButtons 时夹 200）。 */
static XSize xtabwidget_barHint(const XTabWidget* self)
{
    XSize t;
    XSize_init(&t, 0, 0);
    if (!self) return t;
    if (xtabwidget_isAutoHidden(self)) return t;
    t = XWidget_sizeHint_base((XWidget*)self->m_tabBarActive);
    if (t.width < 0) t.width = 0;
    if (t.height < 0) t.height = 0;
    if (XTabWidget_usesScrollButtons(self)) {
        if (t.width > XTABWIDGET_BAR_HINT_LIMIT)
            t.width = XTABWIDGET_BAR_HINT_LIMIT;
        if (t.height > XTABWIDGET_BAR_HINT_LIMIT)
            t.height = XTABWIDGET_BAR_HINT_LIMIT;
    }
    return t;
}

/** @brief 角部件槽位判定（对标 QTabWidget 只保留「左/右」两个槽位：
 *         `corner & Qt::TopRightCorner` 决定右槽，否则左槽——
 *         qtabwidget.cpp:592-625；TopLeft 与 BottomLeft 返回同一控件，
 *         TopRight 与 BottomRight 亦然，本机 Qt 6.8 实测一致）。 */
static bool xtabwidget_cornerIsRight(int corner)
{
    return (corner & XTABWIDGET_CORNER_TOPRIGHT) != 0;
}

/** @brief 指定角的角部件（左/右槽位映射）。 */
static XWidget* xtabwidget_cornerSlot(const XTabWidget* self, int corner)
{
    if (!self) return NULL;
    return xtabwidget_cornerIsRight(corner) ? self->m_cornerWidgetRight
                                            : self->m_cornerWidgetLeft;
}

/** @brief 指定角部件的尺寸提示（未设置 ⇒ (0,0)；minimum=true 取最小值提示）。 */
static XSize xtabwidget_cornerHint(const XTabWidget* self, int corner,
                                   bool minimum)
{
    XSize out;
    XWidget* widget = xtabwidget_cornerSlot(self, corner);
    XSize_init(&out, 0, 0);
    if (!widget) return out;
    out = minimum ? XWidget_minimumSizeHint_base(widget)
                  : XWidget_sizeHint_base(widget);
    if (out.width < 0) out.width = 0;
    if (out.height < 0) out.height = 0;
    return out;
}

/** @brief 页的「智能最小尺寸」（对标 qSmartMinSize：minimumSizeHint 与
 *         minimumSize 取大、maximumSize 夹口，qtabwidget.cpp:890
 *         `d->stack->minimumSizeHint()` = 各页的该值取大）。 */
static XSize xtabwidget_pageMinSize(XWidget* page)
{
    XSize out;
    XSize minimumSize;
    XSize maximumSize;
    if (!page) {
        XSize_init(&out, 0, 0);
        return out;
    }
    out = XWidget_minimumSizeHint_base(page);
    minimumSize = XWidget_minimumSize(page);
    maximumSize = XWidget_maximumSize(page);
    if (out.width < minimumSize.width) out.width = minimumSize.width;
    if (out.height < minimumSize.height) out.height = minimumSize.height;
    if (maximumSize.width >= 0 && out.width > maximumSize.width)
        out.width = maximumSize.width;
    if (maximumSize.height >= 0 && out.height > maximumSize.height)
        out.height = maximumSize.height;
    if (out.width < 0) out.width = 0;
    if (out.height < 0) out.height = 0;
    return out;
}

/** @brief basicSize（qtabwidget.cpp:802-811）。 */
static XSize xtabwidget_basicSize(bool horizontal, const XSize* lc,
                                  const XSize* rc, const XSize* s,
                                  const XSize* t)
{
    XSize out;
    if (horizontal) {
        out.width = s->width > (t->width + rc->width + lc->width)
                        ? s->width
                        : (t->width + rc->width + lc->width);
        out.height = s->height + xtabwidget_max3(rc->height, lc->height,
                                                 t->height);
    } else {
        out.width = s->width + xtabwidget_max3(rc->width, lc->width,
                                               t->width);
        out.height = s->height > (t->height + rc->height + lc->height)
                         ? s->height
                         : (t->height + rc->height + lc->height);
    }
    return out;
}

/** @brief 页签条横放（North/South）判定；West/East 竖放。 */
static bool xtabwidget_barIsHorizontal(const XTabWidget* self)
{
    return self->m_tabPosition == 0 || self->m_tabPosition == 1;
}

/** @brief 尺寸提示（对标 QTabWidget::sizeHint，qtabwidget.cpp:837-878）。 */
static XSize xtabwidget_computeSizeHint(const XTabWidget* self)
{
    XSize lc;
    XSize rc;
    XSize s;
    XSize t;
    XSize out;
    int i;
    if (!self) {
        XSize_init(&out, 0, 0);
        return out;
    }
    lc = xtabwidget_cornerHint(self, XTABWIDGET_CORNER_TOPLEFT, false);
    rc = xtabwidget_cornerHint(self, XTABWIDGET_CORNER_TOPRIGHT, false);
    XSize_init(&s, 0, 0);
    for (i = 0; i < self->m_count; ++i) {
        XSize pageHint;
        if (!self->m_clients[i]) continue;
        if (!XTabWidget_isTabVisible(self, i)) continue; /* Qt: isTabVisible 过滤 */
        pageHint = XWidget_sizeHint_base(self->m_clients[i]);
        if (pageHint.width < 0) pageHint.width = 0;
        if (pageHint.height < 0) pageHint.height = 0;
        s = XSize_expandedTo(&s, &pageHint);
    }
    t = xtabwidget_barHint(self);
    out = xtabwidget_basicSize(xtabwidget_barIsHorizontal(self), &lc, &rc,
                              &s, &t);
    return out;
}

/** @brief 最小尺寸提示（对标 QTabWidget::minimumSizeHint，
 *         qtabwidget.cpp:880-908）。 */
static XSize xtabwidget_computeMinimumSizeHint(const XTabWidget* self)
{
    XSize lc;
    XSize rc;
    XSize s;
    XSize t;
    XSize out;
    int i;
    if (!self) {
        XSize_init(&out, 0, 0);
        return out;
    }
    lc = xtabwidget_cornerHint(self, XTABWIDGET_CORNER_TOPLEFT, true);
    rc = xtabwidget_cornerHint(self, XTABWIDGET_CORNER_TOPRIGHT, true);
    XSize_init(&s, 0, 0);
    for (i = 0; i < self->m_count; ++i) {
        XSize pageMin;
        if (!self->m_clients[i]) continue;
        pageMin = xtabwidget_pageMinSize(self->m_clients[i]);
        s = XSize_expandedTo(&s, &pageMin);
    }
    /* 页签条最小提示：XTabBar 未重载 minimumSizeHint（Qt QTabBar 有），
     * 此处取 `自动隐藏 ? 0 : 条 minimumSizeHint`（无效时按 0 计）。 */
    XSize_init(&t, 0, 0);
    if (!xtabwidget_isAutoHidden(self)) {
        t = XWidget_minimumSizeHint_base((XWidget*)self->m_tabBarActive);
        if (t.width < 0) t.width = 0;
        if (t.height < 0) t.height = 0;
    }
    out = xtabwidget_basicSize(xtabwidget_barIsHorizontal(self), &lc, &rc,
                              &s, &t);
    return out;
}

/** @brief 首选项/最小项缓存刷新（页增删、属性变更后调用；查询槽直接
 *         现算，缓存只服务 XWidget 非虚回读与外部 sizeHint 读取）。 */
static void xtabwidget_updateSizeHint(XTabWidget* self)
{
    XSize hint;
    if (!self) return;
    hint = xtabwidget_computeSizeHint(self);
    XWidget_setSizeHint((XWidget*)self, &hint);
    hint = xtabwidget_computeMinimumSizeHint(self);
    XWidget_setMinimumSizeHint((XWidget*)self, &hint);
}

/** @brief heightForWidth 回调（对标 QTabWidget::heightForWidth，
 *         qtabwidget.cpp:910-953）。
 * @details 页签条横放时内容宽 = 整宽；竖放时先扣页签条/角部件占宽，
 *          再把该宽交给各页的 heightForWidth 取大（QStackedLayout::
 *          heightForWidth 同口径：逐页直问控件、再与最小高取大）。
 *          Qt 侧 CT_TabWidget 的 padding 在 Fusion 为 0，X 无样式内缩，
 *          故 padding 一律按 0。 */
static int xtabwidget_heightForWidth(XTabWidget* self, int width)
{
    XSize lc;
    XSize rc;
    XSize t;
    XSize s;
    XSize content;
    int stackWidth;
    int stackHeight = 0;
    int minHeight = 0;
    int i;
    bool horizontal;
    if (!self) return -1;
    lc = xtabwidget_cornerHint(self, XTABWIDGET_CORNER_TOPLEFT, false);
    rc = xtabwidget_cornerHint(self, XTABWIDGET_CORNER_TOPRIGHT, false);
    t = xtabwidget_barHint(self);
    horizontal = xtabwidget_barIsHorizontal(self);
    stackWidth = width;
    if (!horizontal)
        stackWidth -= xtabwidget_max3(t.width, lc.width, rc.width);
    if (stackWidth < 0) stackWidth = 0;
    for (i = 0; i < self->m_count; ++i) {
        XSize pageMin;
        int hfw;
        if (!self->m_clients[i]) continue;
        hfw = XWidget_heightForWidth(self->m_clients[i], stackWidth);
        if (hfw > stackHeight) stackHeight = hfw;
        pageMin = xtabwidget_pageMinSize(self->m_clients[i]);
        if (pageMin.height > minHeight) minHeight = pageMin.height;
    }
    if (stackHeight < minHeight) stackHeight = minHeight;
    XSize_init(&s, stackWidth, stackHeight);
    content = xtabwidget_basicSize(horizontal, &lc, &rc, &s, &t);
    return content.height;
}

/** @brief heightForWidth 虚槽桥接（XWidgetHeightForWidthHandler 签名）。 */
static int xtabwidget_heightForWidthSlot(XWidget* widget, int width,
                                         void* userData)
{
    (void)userData;
    return xtabwidget_heightForWidth((XTabWidget*)widget, width);
}

/** @brief 按「是否有页支持 hfw」挂/摘本控件的 heightForWidth 回调。
 * @details Qt 的 QTabWidget::hasHeightForWidth = 自身策略 || 页栈策略
 *          （qtabwidget.cpp:224-236）；X 无 HasHeightForWidth 虚槽，
 *          以「回调存在即支持」的 XWidget 口径等价：只有确有页支持
 *          hfw 时才挂回调——否则任何页签容器都会被布局当成 hfw 控件。 */
static void xtabwidget_syncHeightForWidth(XTabWidget* self)
{
    bool has = false;
    int i;
    if (!self) return;
    for (i = 0; i < self->m_count; ++i) {
        if (self->m_clients[i] &&
            XWidget_hasHeightForWidth(self->m_clients[i])) {
            has = true;
            break;
        }
    }
    XWidget_setHeightForWidthHandler(
        (XWidget*)self, has ? xtabwidget_heightForWidthSlot : NULL,
        has ? self : NULL);
}

void XTabWidget_tabInserted(XTabWidget* self, int index)
{
    void (*fn)(XTabWidget*, int);
    if (!self) return;
    if (!XClassGetVtable(self)) return;
    fn = XClassGetVirtualFunc(self, EXTabWidget_TabInserted,
                              void (*)(XTabWidget*, int));
    if (fn && fn != XTabWidget_tabInserted_base)
        fn(self, index);
    else
        XTabWidget_tabInserted_base(self, index);
}

void XTabWidget_tabInserted_base(XTabWidget* self, int index)
{
    /* 对标 Qt 缺省实现：空操作（qtabwidget.cpp:1233-1238）。 */
    (void)self;
    (void)index;
}

void XTabWidget_tabRemoved(XTabWidget* self, int index)
{
    void (*fn)(XTabWidget*, int);
    if (!self) return;
    if (!XClassGetVtable(self)) return;
    fn = XClassGetVirtualFunc(self, EXTabWidget_TabRemoved,
                              void (*)(XTabWidget*, int));
    if (fn && fn != XTabWidget_tabRemoved_base)
        fn(self, index);
    else
        XTabWidget_tabRemoved_base(self, index);
}

void XTabWidget_tabRemoved_base(XTabWidget* self, int index)
{
    /* 对标 Qt 缺省实现：空操作（qtabwidget.cpp:1244-1249）。 */
    (void)self;
    (void)index;
}


/** @brief 把当前生效页签条的五路信号接到本容器的转发槽
 *         （对标 QTabWidget::setTabBar 的 connect 段，qtabwidget.cpp:722-730）。 */
static void xtabwidget_connectBar(XTabWidget* self)
{
    XTabBar* bar;
    if (!self || !self->m_tabBarActive) return;
    bar = self->m_tabBarActive;
    XObject_connect_2((XObject*)bar,
                      (size_t)XTabBar_currentChanged_signal(bar, 0),
                      xtabwidget_currentChangedForward);
    /* 对标 qtabwidget.cpp:725-726：连接 QTabBar::tabMoved → 页数组随
       换位重排（setMovable 打开后拖拽换位页签序/页序才能一致）。 */
    XObject_connect_2((XObject*)bar,
                      (size_t)XTabBar_tabMoved_signal(bar, 0, 0),
                      xtabwidget_tabMovedForward);
    XObject_connect_2((XObject*)bar,
                      (size_t)XTabBar_tabBarClicked_signal(bar, 0),
                      xtabwidget_tabBarClickedForward);
    XObject_connect_2((XObject*)bar,
                      (size_t)XTabBar_tabBarDoubleClicked_signal(bar, 0),
                      xtabwidget_tabBarDoubleClickedForward);
    XObject_connect_2((XObject*)bar,
                      (size_t)XTabBar_tabCloseRequested_signal(bar),
                      xtabwidget_tabCloseRequestedForward);
}

void XTabWidget_init(XTabWidget* self, XWidget* parent, XWidgetFlags flags)
{
    if (!self) return;
    XMemset(self, 0, sizeof(XTabWidget)); /* 先清零子类字段，再走父类 init。 */
    XWidget_init((XWidget*)self, parent, flags);
    XClassSetVtable(self, XTabWidget);

    self->m_pages = NULL;
    self->m_clients = NULL;
    self->m_count = 0;
    self->m_capacity = 0;
    self->m_currentIndex = -1;
    self->m_tabPosition = 0; /* North */
    self->m_tabsClosable = false;
    self->m_movable = false;
    self->m_cornerWidgetLeft = NULL;
    self->m_cornerWidgetRight = NULL;
    /* 缺省页签条 = 内嵌值成员；setTabBar 可换成外部条（m_tabBarActive
     * 指向当前生效条，见 setTabBar）。 */
    self->m_tabBarActive = &self->m_tabBar;
    XTabBar_init(&self->m_tabBar, (XWidget*)self, 0);
    XWidget_show((XWidget*)self->m_tabBarActive);
    xtabwidget_updateSizeHint(self);
    /* 空容器无 hfw 页 ⇒ 不挂回调（与 Qt「页栈有 hfw 才支持」同口径）。 */
    xtabwidget_syncHeightForWidth(self);
    xtabwidget_connectBar(self);
}

XTabWidget* XTabWidget_create_ex(XMemoryType memory, XWidget* parent,
                                 XWidgetFlags flags)
{
    XTabWidget* self = (XTabWidget*)XMemory_malloc(sizeof(XTabWidget), memory);
    if (!self) return NULL;
    XTabWidget_init(self, parent, flags);
    Set_Class_Memory(self, memory); Set_Class_IsHeap(self, true);
    return self;
}

/* ==================== API ==================== */

int XTabWidget_addTab(XTabWidget* self, XWidget* page, const XString* label)
{
    return XTabWidget_insertTab(self, self ? self->m_count : 0, page, label);
}

int XTabWidget_addTab_2(XTabWidget* self, XWidget* page, const char* label)
{
    return XTabWidget_insertTab_2(self, self ? self->m_count : 0, page, label);
}

int XTabWidget_insertTab(XTabWidget* self, int index, XWidget* page,
                         const XString* label)
{
    if (!self || !page || !label) return -1;
    /* 对标 QStackedLayout::insertWidget（qstackedlayout.cpp:192-194）：
       负数/越界索引一律追加到末尾（此前负数插最前）。 */
    if (index < 0 || index > self->m_count) index = self->m_count;
    {
        /* 扩容。 */
        XWidget** pages;
        XWidget** clients;
        int cap = self->m_capacity > 0 ? self->m_capacity : 4;
        while (cap < self->m_count + 1) cap *= 2;
        if (self->m_count + 1 > self->m_capacity) {
            pages = (XWidget**)XRealloc_System(self->m_pages,
                                               sizeof(XWidget*) * (size_t)cap);
            if (!pages) return -1;
            self->m_pages = pages;
            clients = (XWidget**)XRealloc_System(self->m_clients,
                                                 sizeof(XWidget*) * (size_t)cap);
            if (!clients) return -1;
            self->m_clients = clients;
            self->m_capacity = cap;
        }
        XMemmove(&self->m_pages[index + 1], &self->m_pages[index],
                sizeof(XWidget*) * (size_t)(self->m_count - index));
        XMemmove(&self->m_clients[index + 1], &self->m_clients[index],
                sizeof(XWidget*) * (size_t)(self->m_count - index));
        /* 页容器（内部 XWidget，parent 为本控件）。 */
        self->m_pages[index] = (XWidget*)XMemory_malloc(sizeof(XWidget),
                                                        XCLASS_DEFAULT_MEMORY_TYPE);
        if (!self->m_pages[index]) return -1;
        XWidget_init(self->m_pages[index], (XWidget*)self, 0);
        Set_Class_Memory(self->m_pages[index], XCLASS_DEFAULT_MEMORY_TYPE);
        Set_Class_IsHeap(self->m_pages[index], true);
        self->m_clients[index] = page;
        /* 内容控件挂到页容器。 */
        XWidget_setParent(page, self->m_pages[index], 0);

        ++self->m_count;
        (void)XTabBar_insertTab(self->m_tabBarActive, index, label);
        xtabwidget_layout(self);
        /* 页签条首项经 setCurrentIndex 走发射路径（对标 qtabbar.cpp:
           968-969），转发槽已同步 m_currentIndex 与页显隐；此处对无
           订阅者路径兜底同步一次。 */
        if (self->m_currentIndex != self->m_tabBarActive->m_currentIndex)
            self->m_currentIndex = self->m_tabBarActive->m_currentIndex;
        xtabwidget_showCurrent(self);
        xtabwidget_updateSizeHint(self);
        /* 新页可能带 hfw（换行标签等）：重挂本控件的 hfw 回调。 */
        xtabwidget_syncHeightForWidth(self);
        /* 对标 QTabWidget::insertTab 末尾 tabInserted(index)（qtabwidget.cpp:444）。 */
        XTabWidget_tabInserted(self, index);
        return index;
    }
}

int XTabWidget_insertTab_2(XTabWidget* self, int index, XWidget* page,
                           const char* label)
{
    XString_Init_Utf8(tmp, label ? label : "");
    index = XTabWidget_insertTab(self, index, page, tmp);
    XClassDeinit(tmp);
    return index;
}

void XTabWidget_removeTab(XTabWidget* self, int index)
{
    if (!self || index < 0 || index >= self->m_count) return;
    if (self->m_pages[index]) {
        /* 对标 QTabWidget::removeTab（qtabwidget.cpp:627-632）→
           QStackedWidget::removeWidget（qstackedwidget.cpp:149-163）：
           页内容控件"not deleted but simply removed … causing it to
           be hidden"——先经 XWidget_setParent(NULL) 把调用方页控件
           摘出页容器（转独立顶层并隐藏，所有权归还调用方），否则
           页容器析构经 XObject 基类级联会连带销毁调用方控件，页指针
           即成悬垂（与本头文件"页控件不销毁，由调用方管理"契约相
           反）。页容器本体仍延迟回收：删除可能处于对象自身事件/信号
           发射帧（如页签信号槽内调本函数或 clear），同步 deinit+free
           即 UAF（对标 Qt deleteLater 语义）；创建时已按堆登记分配
           器（见 insertTab），延迟删除经 XClass_delete_base 归还堆
           存储。 */
        if (self->m_clients[index]) {
            XWidget_setParent(self->m_clients[index], NULL, 0);
            /* [Qt 对齐 2026-11-05] "causing it to be hidden"：Qt 摘父经
             * setParent_sys（qwidget.cpp:11017-11018，newparent==NULL）
             * 置回 Hidden 位=交还调用方即隐藏。本库 setParent 无同款
             * 置位，此处显式补齐——显式 hide 也同步 m_explicitShow，
             * 调用方下次 setParent/show 语义不受构造位污染。 */
            XWidget_setVisible(self->m_clients[index], false);
        }
        XWidget_setParent(self->m_pages[index], NULL, 0);
        XObject_deleteLater((XObject*)self->m_pages[index]);
    }
    XMemmove(&self->m_pages[index], &self->m_pages[index + 1],
            sizeof(XWidget*) * (size_t)(self->m_count - index - 1));
    XMemmove(&self->m_clients[index], &self->m_clients[index + 1],
            sizeof(XWidget*) * (size_t)(self->m_count - index - 1));
    --self->m_count;
    if (self->m_currentIndex >= self->m_count)
        self->m_currentIndex = self->m_count - 1;
    XTabBar_removeTab(self->m_tabBarActive, index);
    xtabwidget_layout(self);
    xtabwidget_showCurrent(self);
    xtabwidget_updateSizeHint(self);
    /* 移除的页若曾是唯一的 hfw 页：摘掉本控件的 hfw 回调。 */
    xtabwidget_syncHeightForWidth(self);
    /* 对标 QTabWidget::removeTab → QTabWidgetPrivate::removeTab 末尾
     * `q->tabRemoved(index)`（qtabwidget.cpp:763-768）。 */
    XTabWidget_tabRemoved(self, index);
}

int XTabWidget_count(const XTabWidget* self) { return self ? self->m_count : 0; }
int XTabWidget_currentIndex(const XTabWidget* self)
{
    return self ? self->m_currentIndex : -1;
}

void XTabWidget_setCurrentIndex(XTabWidget* self, int index)
{
    if (!self || index < 0 || index >= self->m_count) return;
    XTabBar_setCurrentIndex(self->m_tabBarActive, index); /* 转发槽同步显示。 */
}

XWidget* XTabWidget_currentWidget(const XTabWidget* self)
{
    if (self && self->m_currentIndex >= 0 && self->m_currentIndex < self->m_count)
        return self->m_clients[self->m_currentIndex];
    return NULL;
}

XWidget* XTabWidget_widget(const XTabWidget* self, int index)
{
    if (self && index >= 0 && index < self->m_count) return self->m_clients[index];
    return NULL;
}

int XTabWidget_indexOf(const XTabWidget* self, const XWidget* page)
{
    int i;
    if (!self || !page) return -1;
    for (i = 0; i < self->m_count; ++i)
        if (self->m_clients[i] == page) return i;
    return -1;
}

XString* XTabWidget_tabText(const XTabWidget* self, int index)
{
    return self ? XTabBar_tabText(self->m_tabBarActive, index) : NULL;
}

const char* XTabWidget_tabText_2(const XTabWidget* self, int index)
{
    return self ? XTabBar_tabText_2(self->m_tabBarActive, index) : "";
}

void XTabWidget_setTabText(XTabWidget* self, int index, const XString* text)
{
    if (self) XTabBar_setTabText(self->m_tabBarActive, index, text);
}

void XTabWidget_setTabText_2(XTabWidget* self, int index, const char* text)
{
    if (self) XTabBar_setTabText_2(self->m_tabBarActive, index, text);
}

bool XTabWidget_isTabEnabled(const XTabWidget* self, int index)
{
    return self ? XTabBar_isTabEnabled(self->m_tabBarActive, index) : false;
}

void XTabWidget_setTabEnabled(XTabWidget* self, int index, bool enabled)
{
    /* P0-6 根因：index 无界检即索引 m_clients[]——index<0/越界读堆
       越界数据；且 removeTab 后尾槽残留悬垂指针（memmove 左移不清
       尾槽，被删页的内容控件已随页容器级联析构），越界一档穿透非空
       判定经 XWidget_setEnabled 写已释放内存构成 UAF（indexOf 未命中
       返回 -1 被直传即触发）。对标 QTabWidget::setTabEnabled 对无效
       index 无操作；比照本函数委托的 XTabBar_setTabEnabled（含完整
       界检早退）补界检。 */
    if (!self || index < 0 || index >= self->m_count) return;
    XTabBar_setTabEnabled(self->m_tabBarActive, index, enabled);
    if (self->m_clients[index])
        XWidget_setEnabled(self->m_clients[index], enabled);
}

XTabBar* XTabWidget_tabBar(const XTabWidget* self)
{
    return self ? ((XTabWidget*)self)->m_tabBarActive : NULL;
}

int XTabWidget_setTabBar(XTabWidget* self, XTabBar* tb)
{
    XTabBar* old;
    if (!self || !tb) return -1;
    if (self->m_tabBarActive == tb) return 0;
    /* 对标 QTabWidget::setTabBar（qtabwidget.cpp:711-739）：
     * 「must be called before any tabs have been added, or the behavior is
     * undefined」。已有页签时 Qt 会删掉旧条（连同其页签）而页栈不动，
     * 状态即自相矛盾——这里显式拒绝（返回 -1），不做半套替换。 */
    if (self->m_count > 0) return -1;
    old = self->m_tabBarActive;
    /* 新条 reparent 到本控件并显式 show（Qt `tb->setParent(this);
     * tb->show();`）。 */
    if (XWidget_parentWidget((XWidget*)tb) != (XWidget*)self)
        XWidget_setParent((XWidget*)tb, (XWidget*)self, 0);
    XWidget_show((XWidget*)tb);
    self->m_tabBarActive = tb;
    /* 焦点代理指向新条（Qt `setFocusProxy(d->tabs);`）。 */
    XWidget_setFocusProxy((XWidget*)self, (XWidget*)tb);
    /* 旧条：外部换入的条归本容器，按 Qt `delete d->tabs` 销毁；内嵌缺省
     * 条是值成员不可删，只隐藏。两者残留的转发连接由
     * xtabwidget_ownerOf/currentChanged 的「发送者 == 生效条」闸拦掉。 */
    if (old) {
        if (old == &self->m_tabBar)
            XWidget_setVisible((XWidget*)old, false);
        else
            XClassDelete((XClass*)old);
    }
    xtabwidget_connectBar(self);
    /* 新条的形状/布局按当前 tabPosition 补推（[P0-01] 形状映射 +
       竖条几何随动）。 */
    xtabwidget_updateTabBarPosition(self);
    return 0;
}
void XTabWidget_setCurrentWidget(XTabWidget* self, XWidget* page)
{
    int index;
    if (!self || !page) return;
    index = XTabWidget_indexOf(self, page);
    if (index >= 0) XTabWidget_setCurrentIndex(self, index);
}

void XTabWidget_setWidget(XTabWidget* self, XWidget* widget)
{
    int idx;
    int held;
    XWidget* old;
    if (!self || widget == (XWidget*)self) return;
    /* 替换以“存在页容器”为前提：尚无任何页时不动作（建页须页签
       文本，走 addTab/insertTab，setWidget 不代建）。 */
    idx = self->m_currentIndex;
    if (idx < 0 || idx >= self->m_count || !self->m_pages[idx]) return;
    old = self->m_clients[idx];
    /* 同指针幂等：重设现有内容为无操作（对标 Qt setWidget 家族）。 */
    if (old == widget) return;
    /* 新控件已登记在其它页：仅解除那条借用记录（控件本体随下方
       reparent 归入当前页容器，原页转为无内容——对标 Qt 同一子控件
       挂到新父即随迁）。 */
    if (widget) {
        held = XTabWidget_indexOf(self, widget);
        if (held >= 0 && held != idx) self->m_clients[held] = NULL;
    }
    /* 替换语义：旧控件摘除父链转独立顶层（XWidget_setParent(NULL)
       即窗口化并隐藏），不销毁——所有权转移调用方（对标 QDockWidget
       /QMdiSubWindow::setWidget 旧件不删）。 */
    if (old)
        XWidget_setParent(old, NULL, 0);
    /* 装入新控件并布局：reparent 到当前页容器，铺满页几何并按当前
       页同步显隐。 */
    self->m_clients[idx] = widget;
    if (widget)
        XWidget_setParent(widget, self->m_pages[idx], 0);
    xtabwidget_layout(self);
    xtabwidget_showCurrent(self);
    xtabwidget_updateSizeHint(self);
    /* 换入/换出的内容会改变 hfw 支持（Qt 的 hasHeightForWidth 经页栈
     * 汇总），重挂回调与首选尺寸。 */
    xtabwidget_syncHeightForWidth(self);
}

void XTabWidget_setTabIcon(XTabWidget* self, int index, const XString* path)
{
    if (self) XTabBar_setTabIcon(self->m_tabBarActive, index, path);
}
void XTabWidget_setTabIcon_2(XTabWidget* self, int index, const char* path)
{
    if (self) XTabBar_setTabIcon_2(self->m_tabBarActive, index, path);
}
const XString* XTabWidget_tabIcon(const XTabWidget* self, int index)
{
    return self ? XTabBar_tabIcon(self->m_tabBarActive, index) : NULL;
}
const char* XTabWidget_tabIcon_2(const XTabWidget* self, int index)
{
    return self ? XTabBar_tabIcon_2(self->m_tabBarActive, index) : "";
}

void XTabWidget_setTabToolTip(XTabWidget* self, int index, const XString* tip)
{
    if (self) XTabBar_setTabToolTip(self->m_tabBarActive, index, tip);
}
void XTabWidget_setTabToolTip_2(XTabWidget* self, int index, const char* tip)
{
    if (self) XTabBar_setTabToolTip_2(self->m_tabBarActive, index, tip);
}

void XTabWidget_setTabWhatsThis(XTabWidget* self, int index,
                                const XString* text)
{
    /* 对标 QTabWidget::setTabWhatsThis（qtabwidget.cpp:1226）→
     * QTabBar::setTabWhatsThis：帮助文本与提示是**两套独立存储**
     * （XTabBar 的 m_tabWhatsThis / m_tabToolTips 平行数组）。 */
    if (self) XTabBar_setTabWhatsThis(self->m_tabBarActive, index, text);
}
void XTabWidget_setTabWhatsThis_2(XTabWidget* self, int index,
                                  const char* text)
{
    if (self) XTabBar_setTabWhatsThis_2(self->m_tabBarActive, index, text);
}

void XTabWidget_setTabVisible(XTabWidget* self, int index, bool visible)
{
    if (self) XTabBar_setTabVisible(self->m_tabBarActive, index, visible);
}
bool XTabWidget_isTabVisible(const XTabWidget* self, int index)
{
    return self ? XTabBar_isTabVisible(self->m_tabBarActive, index) : true;
}

void XTabWidget_setTabBarAutoHide(XTabWidget* self, bool enable)
{
    if (!self) return;
    /* 对标 QTabBar::setAutoHide（qtabbar.cpp:2743-2749）：显隐随
       count>1 重算的真路径在页签条（insertTab/removeTab 亦各自重算，
       qtabbar.cpp:999/1082）；容器侧随后重排——页签条行隐去后页面
       区须占满全高。 */
    XTabBar_setAutoHide(self->m_tabBarActive, enable);
    xtabwidget_layout(self);
}
bool XTabWidget_tabBarAutoHide(const XTabWidget* self)
{
    return self ? self->m_tabBarActive->m_autoHide : false;
}


int XTabWidget_tabPosition(const XTabWidget* self)
{
    return self ? self->m_tabPosition : 0;
}

/** @brief 页签位置 → 页签条形状映射 + 重排（对标
 *  QTabWidgetPrivate::updateTabBarPosition，qtabwidget.cpp:960-982：
 *  形状 = 容器 Rounded/Triangular 档 × 页签位置，qtabwidget.cpp:1420-1432
 *  的 _q_tb_tabBarShapeFrom）。
 * @details [P0-01] West/East 的形状即 XTabBar 的竖排判据
 *          （QTabBarPrivate::verticalTabs：形状 2/3/6/7 竖排）——仅换
 *          几何不换形状时页签条仍横排，24px 竖条里页签全部越出。
 *          本容器无独立 Rounded/Triangular 属性，家族位（形状 &~3：
 *          0=Rounded、4=Triangular）跟随页签条当前形状，方向分量一律
 *          由 tabPosition 决定；setTabBar 换条后同口径补推。 */
static void xtabwidget_updateTabBarPosition(XTabWidget* self)
{
    unsigned family;
    if (!self || !self->m_tabBarActive) return;
    family = self->m_tabBarActive->m_shape & ~3u; /* 0=Rounded / 4=Triangular。 */
    XTabBar_setShape(self->m_tabBarActive,
                     (int)family + (int)self->m_tabPosition);
    xtabwidget_layout(self);
}

void XTabWidget_setTabPosition(XTabWidget* self, int position)
{
    if (!self || position < 0 || position > 3) return;
    if (self->m_tabPosition == (uint32_t)position) return;
    self->m_tabPosition = (uint32_t)position;
    /* 对标 QTabWidget::setTabPosition（qtabwidget.cpp:1001-1008）：
       setter 立即经 updateTabBarPosition → setUpLayout 重排（形状随
       位置映射，P0-01）。 */
    xtabwidget_updateTabBarPosition(self);
}

bool XTabWidget_tabsClosable(const XTabWidget* self)
{
    return self ? self->m_tabsClosable : false;
}

void XTabWidget_setTabsClosable(XTabWidget* self, bool closable)
{
    if (self) {
        self->m_tabsClosable = closable;
        XTabBar_setTabsClosable(self->m_tabBarActive, closable);
    }
}

bool XTabWidget_isMovable(const XTabWidget* self)
{
    return self ? self->m_movable : false;
}

void XTabWidget_setMovable(XTabWidget* self, bool movable)
{
    if (self) {
        self->m_movable = movable;
        XTabBar_setMovable(self->m_tabBarActive, movable);
    }
}

void XTabWidget_clear(XTabWidget* self)
{
    if (!self) return;
    while (self->m_count > 0) XTabWidget_removeTab(self, self->m_count - 1);
}

void XTabWidget_setDocumentMode(XTabWidget* self, bool enable)
{
    if (self) XTabBar_setDocumentMode(self->m_tabBarActive, enable);
}
bool XTabWidget_documentMode(const XTabWidget* self)
{
    return self ? XTabBar_documentMode(self->m_tabBarActive) : false;
}

void XTabWidget_setElideMode(XTabWidget* self, int mode)
{
    if (self) XTabBar_setElideMode(self->m_tabBarActive, mode);
}
int XTabWidget_elideMode(const XTabWidget* self)
{
    /* 对齐 Qt：QTabWidget::elideMode 转发页签条；self 为 NULL 时同样返回
     * 缺省 ElideRight（数值 1），与 XTabBar_elideMode 的 NULL 语义一致。 */
    return self ? XTabBar_elideMode(self->m_tabBarActive)
                : (int)XAbstractItemViewTextElideMode_ElideRight;
}

void XTabWidget_setTabShape(XTabWidget* self, int shape)
{
    if (self) XTabBar_setShape(self->m_tabBarActive, shape);
}
int XTabWidget_tabShape(const XTabWidget* self)
{
    return self ? XTabBar_shape(self->m_tabBarActive) : 0;
}

void XTabWidget_setUsesScrollButtons(XTabWidget* self, bool enable)
{
    if (self) XTabBar_setUsesScrollButtons(self->m_tabBarActive, enable);
}
bool XTabWidget_usesScrollButtons(const XTabWidget* self)
{
    return self ? XTabBar_usesScrollButtons(self->m_tabBarActive) : false;
}

void XTabWidget_setIconSize(XTabWidget* self, int size)
{
    /* 旧单值入口：方边语义（宽=高）转发页签条。 */
    if (self) XTabBar_setIconSize(self->m_tabBarActive, size);
}
void XTabWidget_setIconSize_2(XTabWidget* self, XSize size)
{
    if (self) XTabBar_setIconSize_2(self->m_tabBarActive, size);
}
int XTabWidget_iconSize(const XTabWidget* self)
{
    return self ? XTabBar_iconSize(self->m_tabBarActive) : 0;
}
XSize XTabWidget_iconSize_2(const XTabWidget* self)
{
    XSize size;
    if (!self) {
        /* 与旧单值入口的 NULL 口径一致（0 而非 16）。 */
        XSize_init(&size, 0, 0);
        return size;
    }
    return XTabBar_iconSize_2(self->m_tabBarActive);
}

const XString* XTabWidget_tabToolTip(const XTabWidget* self, int index)
{
    if (!self || index < 0 || index >= self->m_count) return NULL;
    return XTabBar_tabToolTip(self->m_tabBarActive, index);
}

const XString* XTabWidget_tabWhatsThis(const XTabWidget* self, int index)
{
    if (!self || index < 0 || index >= self->m_count) return NULL;
    /* 独立存储（对标 QTabWidget::tabWhatsThis → QTabBar::tabWhatsThis）。 */
    return XTabBar_tabWhatsThis(self->m_tabBarActive, index);
}

XWidget* XTabWidget_cornerWidget(const XTabWidget* self, int corner)
{
    if (!self || corner < 0 || corner > 3) return NULL;
    /* 对标 QTabWidget::cornerWidget（qtabwidget.cpp:627-634）：
     * `if (corner & Qt::TopRightCorner) return rightCornerWidget;
     *  return leftCornerWidget;`——四角只有两个槽位。 */
    return xtabwidget_cornerSlot(self, corner);
}

void XTabWidget_setCornerWidget(XTabWidget* self, XWidget* widget, int corner)
{
    XWidget* old;
    if (!self || corner < 0 || corner > 3) return;
    if (widget == (XWidget*)self) return;
    /* 挂载：Qt 同语义——角部件尚未挂到本控件时先 reparent。登记表
       仅为借用记录；本体经父子链随本控件析构级联释放（Qt setCornerWidget
       同：tab widget 销毁时删除角部件，除非调用方另行 reparent 接管）。 */
    if (widget && XWidget_parentWidget(widget) != (XWidget*)self)
        XWidget_setParent(widget, (XWidget*)self, 0);
    /* 对标 qtabwidget.cpp:608-616：按 `corner & TopRightCorner` 落左/右槽，
       同槽旧件只 hide 不销毁。 */
    old = xtabwidget_cornerIsRight(corner) ? self->m_cornerWidgetRight
                                          : self->m_cornerWidgetLeft;
    if (old && old != widget)
        XWidget_setVisible(old, false); /* 旧角部件隐藏（不销毁）。 */
    if (xtabwidget_cornerIsRight(corner))
        self->m_cornerWidgetRight = widget;
    else
        self->m_cornerWidgetLeft = widget;
    /* 复扫 R-85：reparent 只挂父链不产生可见性——框架显式 show 语义下
     * 新角部件 m_explicitShow=0 恒不可见（clients 已补 show 而角部件
     * 漏，见 xtabwidget_showCurrent 同口径注释），setter 补显式 show。 */
    if (widget)
        XWidget_show(widget);
    xtabwidget_layout(self);
}

/* ==================== 信号 ==================== */

void* XTabWidget_currentChanged_signal(XTabWidget* self, int index)
{
    (void)index;
    return (void*)(size_t)XTabWidget_currentChanged_signal;
}

void* XTabWidget_tabCloseRequested_signal(XTabWidget* self)
{
    /* 仅返回信号标识；真发射经 init 中 connect_2 的
     * xtabwidget_tabCloseRequestedForward 转发（发射点在 XTabBar 侧的
     * 真实关闭按钮，见头文件 @note）。 */
    (void)self;
    return (void*)(size_t)XTabWidget_tabCloseRequested_signal;
}




void* XTabWidget_tabBarClicked_signal(XTabWidget* self)
{
    XVarList* arguments;
    int index;

    if (!self)
        return (void*)(size_t)XTabWidget_tabBarClicked_signal;
    index = self->m_tabBarActive->m_currentIndex;
    arguments = XVarList_Create(XVar(int, index));
    if (arguments) {
        if (((XObject*)self)->m_signalSlot)
            XObject_emitSignal((XObject*)self,
                               (size_t)XTabWidget_tabBarClicked_signal,
                               arguments, NULL, NULL,
                               XEVENT_PRIORITY_NORMAL);
        else
            XVarList_delete(arguments);
    }
    return (void*)(size_t)XTabWidget_tabBarClicked_signal;
}

void* XTabWidget_tabBarDoubleClicked_signal(XTabWidget* self)
{
    XVarList* arguments;
    int index;

    if (!self)
        return (void*)(size_t)XTabWidget_tabBarDoubleClicked_signal;
    index = self->m_tabBarActive->m_currentIndex;
    arguments = XVarList_Create(XVar(int, index));
    if (arguments) {
        if (((XObject*)self)->m_signalSlot)
            XObject_emitSignal((XObject*)self,
                               (size_t)XTabWidget_tabBarDoubleClicked_signal,
                               arguments, NULL, NULL,
                               XEVENT_PRIORITY_NORMAL);
        else
            XVarList_delete(arguments);
    }
    return (void*)(size_t)XTabWidget_tabBarDoubleClicked_signal;
}


















#endif /* XWIDGET_ON && XTABBAR_ON && XTABWIDGET_ON */

#endif /* XGUI_ON */
