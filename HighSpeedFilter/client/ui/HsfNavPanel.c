/**
 * @file       HsfNavPanel.c
 * @brief      导航面板实现：手风琴树（主菜单就地展开子菜单）+ 几何重排。
 * @details    机制对标 XGuiWindowDemo 侧栏：
 *             - 滚动区 widgetResizable=false（内容区不被撑到视口高，
 *               保住滚动语义）+ 内容区显式 resize（宽=面板宽、高=条目总高）；
 *             - 条目按钮直接挂内容区，绝对几何定位（父节点左对齐、
 *               子节点缩进挂其下 → 树形层级一目了然）；
 *             - 条目可勾选并入互斥组（当前页常亮），导航经组 idClicked
 *               单连接派发（组 id=条目下标）。
 *
 *             手风琴语义（同 demo 的单一 m_navCategory）：点主菜单就地
 *             展开其子菜单（子项插在其下、其余项顺延），点另一个主菜单
 *             收起前一个；再点同一个收起自己。叶子项点击=导航。
 *
 *             两种形态（同一份树，两套度量）：
 *             - 宽屏侧栏：26px 条目、缩进 16、带标题行；
 *             - 移动态（竖屏菜单页）：44px 触控行、40px 分区标题行、
 *               分区间距 10、无标题行（同 XGuiWindowDemo 移动端菜单）。
 */
#include "HsfNavPanel.h"

#include "HsfTheme.h"
#include "HsfUi.h"      /* hsfUi_fontPx / hsfUi_globalFontPct（全局字号折算） */
#include "XObject.h"
#include "XVarList.h"
#include "XMemory.h"
#include <string.h>

#if XWIDGET_ON && XPUSHBUTTON_ON && XLABEL_ON && XSCROLLAREA_ON

/* [面板自身虚表] 本面板用**显式绝对几何**排布（文件头「对标
 * XGuiWindowDemo 侧栏」：不用盒布局），框架没有布局对象会在容器尺寸
 * 变化时替我们重排子项——必须自己重载 RESIZE 接住。只重载
 * resizeEvent 一个槽，其余全部继承 XWidget（同 NetPageHost 口径）。 */
XCLASS_DEFINE_BEGING(HsfNavPanel)
XCLASS_DEFINE_EXTEND_END(HsfNavPanel, XWidget)

/* ---- 宽屏侧栏度量（对标 demo 侧栏节奏） ---- */
#define HSF_NAV_TITLE_H     28  /**< 标题行高。 */
#define HSF_NAV_ITEM_H      26  /**< 顶层条目高。 */
#define HSF_NAV_CHILD_H     24  /**< 子条目高。 */
#define HSF_NAV_ITEM_PITCH  28  /**< 顶层条目行距。 */
#define HSF_NAV_CHILD_PITCH 26  /**< 子条目行距。 */
#define HSF_NAV_INDENT      16  /**< 子层级缩进。 */
#define HSF_NAV_PAD         8   /**< 面板左右留白。 */

/* ---- 移动态度量（竖屏菜单页；同 demo 移动端菜单） ---- */
#define HSF_NAV_M_ITEM_H    40  /**< 分区（顶层）标题行高。 */
#define HSF_NAV_M_ROW_H     44  /**< 条目触控行高（Material 44dp）。 */
#define HSF_NAV_M_ITEM_PITCH 42 /**< 分区标题行距。 */
#define HSF_NAV_M_ROW_PITCH  44 /**< 条目行距。 */
#define HSF_NAV_M_INDENT    16  /**< 子项缩进。 */
#define HSF_NAV_M_GAP       10  /**< 分区间距。 */

/* 字号源码基准（全局字体缩放的幂等锚点：applyFont 以这份常量折算，
 * 从不读回当前字号当新基准——150%→160% 不会层层相乘）。 */
#define HSF_NAV_TITLE_FONT_PX 14  /**< 面板标题「导航」。 */
#define HSF_NAV_ITEM_FONT_PX  16  /**< 条目按钮文本（默认渲染口径）。 */

/** @brief 排布折算用的当前全局百分比（navScale 读它；非同值闸——
 *  同值闸在 m_appliedFontPct 按实例记账，见 HsfNavPanel.h 成员注释：
 *  导航面板有两个实例，文件级闸会吞掉第二个实例的重放）。 */
static int s_navFontPct = -1;

/** @brief 度量 × 当前生效全局百分比（update 排布高度/行距统一走它）。 */
static int navScale(int px)
{
    const int pct = s_navFontPct > 0 ? s_navFontPct : 100;
    return (px * pct + 50) / 100;
}

/** @brief 树节点是否有子节点（决定是否可展开）。 */
static bool hsfNavHasSubs(const HsfNavNode* tree, int count, int index)
{
    int i;
    if (!tree) return false;
    for (i = 0; i < count; ++i)
        if (tree[i].childOf == index) return true;
    return false;
}

/** @brief 条目点击槽（互斥组 idClicked 单连接）：组 id = 条目下标。
 *  @details 对标 XGuiWindowDemo 侧栏（demo_navCatSlot 同款口径）：
 *           一次连接覆盖全部条目。**主菜单项=就地展开/收起**（不导航），
 *           **叶子项=导航到该页**。 */
static void hsfNavItemSlot(XObject* receiver, XVarList* args)
{
    HsfNavPanel* panel = (HsfNavPanel*)receiver;
    const HsfNavNode* tree;
    int count = 0;
    int nodeIndex;
    if (!panel || !args) return;
    /* XVarList_args_1 自行声明形参变量（勿预声明）。 */
    XVarList_args_1(args, int, id);
    if (id < 0 || id >= panel->m_count) return;
    nodeIndex = panel->m_items[id].m_nodeIndex;
    tree = HsfPages_navTree(&count);
    if (!tree || nodeIndex < 0 || nodeIndex >= count) return;
    if (hsfNavHasSubs(tree, count, nodeIndex)) {
        /* 主菜单：就地展开/收起（点别的自动收起本个——单展开手风琴）。 */
        if (panel->m_expanded == nodeIndex)
            panel->m_expanded = -1;
        else
            panel->m_expanded = nodeIndex;
        HsfNavPanel_update(panel);
        return;
    }
    if (panel->m_navFn && tree[nodeIndex].pageIndex >= 0)
        panel->m_navFn(panel->m_navUserData, tree[nodeIndex].pageIndex);
}

/** @brief 当前页高亮刷新（互斥组内单选；勾选态由框架按 checked 绘制）。 */
static void hsfNavApplyActive(HsfNavPanel* self)
{
    const HsfNavNode* tree;
    int count = 0;
    int i;
    if (!self) return;
    tree = HsfPages_navTree(&count);
    for (i = 0; i < self->m_count; ++i) {
        int nodeIndex = self->m_items[i].m_nodeIndex;
        bool on = (nodeIndex >= 0 && nodeIndex < count &&
                   tree[nodeIndex].pageIndex == self->m_activePage);
        XAbstractButton_setChecked((XAbstractButton*)&self->m_items[i].m_btn,
                                   on);
    }
}

/** @brief 同值早退的 setGeometry（**切页热路径**，非风格问题）。
 *  @details 框架的 XWidget_setGeometry **刻意不复刻 Qt 的同值早退**：每次
 *           调用都会 XLayout_invalidate 父布局 + 标脏该控件 ⇒ 一次切页里
 *           导航面板逐条重排 15 次 = 15 次布局失效 + 15 次重绘（实测切页
 *           后导航区一次重绘 7~52ms）。项目铁律「每拍都调的 setter 必须
 *           自己判同值」——本函数即该守卫。 */
static void navSetGeometry(XWidget* w, int x, int y, int width, int height)
{
    if (!w) return;
    if (XWidget_x(w) == x && XWidget_y(w) == y &&
        XWidget_width(w) == width && XWidget_height(w) == height)
        return;
    XWidget_setGeometry(w, x, y, width, height);
}

/** @brief 按当前面板尺寸重排条目几何（标题 + 滚动壳 + 条目树）。
 *  @details 手风琴：仅当前展开的主菜单显示其子项（其余子项隐藏且
 *           不占位——子项插在主菜单下、其余项顺延）。 */
void HsfNavPanel_update(HsfNavPanel* self)
{
    const HsfNavNode* tree;
    int count = 0;
    int pw;
    int ph;
    int scrollH;
    int titleH;
    int y = 0;
    int i;
    if (!self) return;
    pw = XWidget_width(&self->m_base);
    ph = XWidget_height(&self->m_base);
    if (pw < 1) pw = 1;
    if (ph < 1) ph = 1;

    /* 标题行（移动态无标题，滚动壳从顶开始）。度量 × 全局缩放（字号
     * 变大行高/行距同步放大，否则文字被裁——见 navScale 注释）。 */
    titleH = self->m_mobile ? 0 : navScale(HSF_NAV_TITLE_H);
    if (!self->m_mobile) {
        navSetGeometry((XWidget*)&self->m_title, HSF_NAV_PAD, 4,
                       pw - HSF_NAV_PAD * 2, navScale(20));
        if (XWidget_isHidden((XWidget*)&self->m_title))
            XWidget_show((XWidget*)&self->m_title);
    }

    /* 滚动壳占位（标题行下方到底部）。 */
    scrollH = ph - titleH;
    if (scrollH < 1) scrollH = 1;
    navSetGeometry((XWidget*)&self->m_scroll, 0, titleH, pw, scrollH);
    if (XWidget_isHidden((XWidget*)&self->m_scroll))
        XWidget_show((XWidget*)&self->m_scroll);
    XWidget_raise((XWidget*)&self->m_scroll);

    /* 条目树：顶层（=主菜单）左对齐；其子项缩进挂其下，仅展开者可见。 */
    tree = HsfPages_navTree(&count);
    for (i = 0; i < self->m_count && i < count; ++i) {
        HsfNavItem* item = &self->m_items[i];
        bool isChild = (tree[i].childOf >= 0);
        if (isChild) {
            /* 子项：父主菜单未展开 → 隐藏且不占位。
             * [同值早退] 框架 XWidget_show/hide 刻意不复刻 Qt 的同值早退，
             * 每次调用都会 XLayout_invalidate 父布局；分割条拖动时本函数
             * 逐帧重跑，20+ 条目无守卫地 show 一遍就是布局/脏区风暴
             * （项目铁律「推送里每拍都调的 setter 必须自己判同值」）。
             * 显式隐藏位没变就别调。 */
            if (self->m_expanded != tree[i].childOf) {
                if (!XWidget_isHidden((XWidget*)&item->m_btn))
                    XWidget_hide((XWidget*)&item->m_btn);
                continue;
            }
            {
                int indent = self->m_mobile ? HSF_NAV_M_INDENT
                                             : HSF_NAV_INDENT;
                navSetGeometry((XWidget*)&item->m_btn,
                               HSF_NAV_PAD + indent, y,
                               pw - HSF_NAV_PAD * 2 - indent,
                               navScale(self->m_mobile
                                            ? HSF_NAV_M_ROW_H
                                            : HSF_NAV_CHILD_H));
            }
            y += navScale(self->m_mobile ? HSF_NAV_M_ROW_PITCH
                                         : HSF_NAV_CHILD_PITCH);
        } else {
            navSetGeometry((XWidget*)&item->m_btn, HSF_NAV_PAD, y,
                           pw - HSF_NAV_PAD * 2,
                           navScale(self->m_mobile ? HSF_NAV_M_ITEM_H
                                                   : HSF_NAV_ITEM_H));
            y += navScale(self->m_mobile ? HSF_NAV_M_ITEM_PITCH
                                         : HSF_NAV_ITEM_PITCH);
            /* 移动态：主菜单之间留分区间距（同 demo 移动端菜单）。 */
            if (self->m_mobile)
                y += navScale(HSF_NAV_M_GAP);
        }
        if (XWidget_isHidden((XWidget*)&item->m_btn))
            XWidget_show((XWidget*)&item->m_btn);
    }
    /* 内容区尺寸：宽=面板宽、高=条目总高（不可拉伸 ⇒ 保滚动语义）。 */
    XWidget_resize(&self->m_content, pw, y > 1 ? y : 1);
}

/** @brief 尺寸变化：立即按新尺寸重排内部条目（拖动过程逐帧跟手）。
 *  @details 为什么必须在这里做：面板与条目是**显式绝对几何**（见文件头
 *           「对标 XGuiWindowDemo 侧栏」），框架没有布局引擎会在面板被
 *           撑宽/压窄后把条目重算一遍。分割条拖拽经
 *           XWidget_setGeometryRect 改面板几何 → 框架同步派发 RESIZE
 *           （XWidget_recomputeGeometry → EXWidget_ResizeEvent）→ 本槽
 *           重跑 update，等价 Qt 的「QSplitter 拖拽 → 子控件 resize →
 *           布局立即 activate」实时重排。
 *           此前只在帧泵「主窗宽/高、当前页、形态档」签名变化时调
 *           update，而拖拽恰恰不改这四个量 ⇒ 面板容器变宽、条目宽度
 *           与 x 不动、容器右侧留白（现场截图口径）——本槽即其修复点。
 *  @note 只调 update：它只改**子控件**几何，不动 m_base 自身，故不会
 *        在 RESIZE 派发中递归回本槽。 */
static void VHsfNavPanel_resizeEvent(XWidget* self, XEvent* event)
{
    HsfNavPanel* panel = (HsfNavPanel*)self; /* m_base 是首成员，地址即面板。 */
    /* 基类槽必须取 XWidget 自己的虚表项（XClass_Parent）——经对象虚表
     * 的 *_base 入口会再派回本重载，无限递归。 */
    XClass_Parent(XWidget, EXWidget_ResizeEvent,
                  void (*)(XWidget*, XEvent*))(self, event);
    /* 构造中途（标题/滚动壳/条目尚未建齐）的 RESIZE 一律跳过：此时
     * HsfNavPanel_update 会碰到未初始化的子控件。initEx 末尾自会补一次。 */
    if (!panel || !panel->m_ready) return;
    HsfNavPanel_update(panel);
}

/** @brief HsfNavPanel 虚表：仅重载 resizeEvent，其余继承 XWidget。 */
static XVtable* HsfNavPanel_class_init(void)
{
    XVTABLE_INIT_DEFAULT(HsfNavPanel)
    XVTABLE_INHERIT_XCLASS(XWidget);
    XVTABLE_OVERLOAD_DEFAULT(EXWidget_ResizeEvent, VHsfNavPanel_resizeEvent);
    return XVTABLE_DEFAULT;
}

/** @brief 程序化点击指定条目（自测/无头验证用）。 */
void HsfNavPanel_clickItem(HsfNavPanel* self, int itemIndex)
{
    if (!self || itemIndex < 0 || itemIndex >= self->m_count) return;
    XAbstractButton_click((XAbstractButton*)&self->m_items[itemIndex].m_btn);
}

/** @brief 初始化导航面板（标题 + 滚动壳 + 树条目）。
 *  @param mobile true=移动态（竖屏菜单页）。 */
static void hsfNavPanel_initEx(HsfNavPanel* self, XWidget* parent,
                               bool mobile)
{
    const HsfNavNode* tree;
    int count = 0;
    int i;
    int n = 0;

    if (!self) return;
    memset(self, 0, sizeof(HsfNavPanel));
    self->m_activePage = -1;
    self->m_mobile = mobile;
    self->m_expanded = -1;

    XWidget_init(&self->m_base, parent, 0);
    /* 装本类虚表：resizeEvent 重载由此生效（容器尺寸变化 → 条目即时
     * 重排，见 VHsfNavPanel_resizeEvent 的「为什么」）。 */
    XClassSetVtable(&self->m_base, HsfNavPanel);
    HsfTheme_applyDefaultFont(&self->m_base);
    /* 面板最小宽度：拖拽把手可改宽，但窄于此字会挤掉条目。 */
    if (!mobile)
        XWidget_setMinimumWidth(&self->m_base, 130);

    /* 标题行（移动态不建——竖屏菜单页自带页头）。 */
    XLabel_init(&self->m_title, &self->m_base, 0);
    HsfTheme_applyDefaultFont((XWidget*)&self->m_title);
    XLabel_setText_2(&self->m_title, mobile ? "" : "导航");
    XLabel_setTextPixelSize(&self->m_title,
                            hsfUi_fontPx(HSF_NAV_TITLE_FONT_PX));
    XLabel_setAlignment(&self->m_title,
                        XAlignment_Left | XAlignment_VCenter);
    XWidget_setFixedHeight((XWidget*)&self->m_title,
                           hsfUi_fontPx(HSF_NAV_TITLE_H));
    if (mobile)
        XWidget_hide((XWidget*)&self->m_title);
    else
        XWidget_show((XWidget*)&self->m_title);

    /* 滚动壳：不可拉伸（内容区不被撑到视口高）+ 无边框（demo 同款）。 */
    XScrollArea_init(&self->m_scroll, &self->m_base, 0);
    XAbstractScrollArea_setHorizontalScrollBarPolicy(
        (XAbstractScrollArea*)&self->m_scroll,
        XScrollBarPolicy_AlwaysOff);
    XWidget_init(&self->m_content, (XWidget*)&self->m_scroll, 0);
    HsfTheme_applyDefaultFont(&self->m_content);
    XScrollArea_setWidget(&self->m_scroll, &self->m_content);
    XScrollArea_setWidgetResizable(&self->m_scroll, false);
    XFrame_setFrameStyle((XFrame*)&self->m_scroll,
                         (int)(XFrameShape_NoFrame | XFrameShadow_Plain));

    /* 条目互斥组（当前页常亮）。 */
    XButtonGroup_init(&self->m_group, NULL);
    XButtonGroup_setExclusive(&self->m_group, true);

    /* 逐节点建条目（按钮直接挂内容区，几何由 update 统一排）。 */
    tree = HsfPages_navTree(&count);
    for (i = 0; i < count && n < HSF_NAV_MAX_NODES; ++i) {
        HsfNavItem* item = &self->m_items[n];
        item->m_nodeIndex = i;
        item->m_parent = tree[i].childOf;
        XPushButton_init(&item->m_btn, &self->m_content, 0);
        HsfTheme_applyDefaultFont((XWidget*)&item->m_btn);
        {
            /* 条目文本补显式字号（基准 16=默认渲染口径；全局缩放经
             * applyFont 重放时不至于读回继承字号当基准）。 */
            XFont f = XWidget_font((XWidget*)&item->m_btn);
            XFont_setPixelSize(&f, hsfUi_fontPx(HSF_NAV_ITEM_FONT_PX));
            XWidget_setFont((XWidget*)&item->m_btn, &f);
            XClassDeinit(&f);
        }
        XPushButton_setText_2(&item->m_btn, tree[i].caption);
        XPushButton_setFlat(&item->m_btn, true);
        XWidget_setFixedHeight((XWidget*)&item->m_btn,
                               mobile ? HSF_NAV_M_ITEM_H : HSF_NAV_ITEM_H);
        XAbstractButton_setCheckable((XAbstractButton*)&item->m_btn, true);
        XButtonGroup_addButton(&self->m_group,
                               (XAbstractButton*)&item->m_btn, n);
        XWidget_show((XWidget*)&item->m_btn);
        ++n;
    }
    self->m_count = n;

    /* 组 idClicked 单连接覆盖全部条目（连接一次，勿入循环）。 */
    XObject_connect((XObject*)&self->m_group,
                      (size_t)XButtonGroup_idClicked_signal(NULL, 0),
                      (XObject*)self, hsfNavItemSlot,
                      XConnectionType_Direct);

    /* 子控件齐备，允许 resizeEvent 重排（此前的中途 RESIZE 一律跳过）。 */
    self->m_ready = true;
    s_navFontPct = hsfUi_globalFontPct(); /* update 排布折算的 pct 口径。 */
    XWidget_show(&self->m_base);
    HsfNavPanel_update(self);
}

/** @brief 导航面板字号实时重放（源码基准 14/16，pct 同值闸）。
 *  @details 只重设标题/条目字号并刷新 s_navFontPct，几何重排直接调
 *           update（排布度量全部过 navScale，一次重排全部跟手）。 */
void HsfNavPanel_applyFont(HsfNavPanel* self, int pct)
{
    int i;
    if (!self || pct <= 0) return;
    /* [同值闸按实例记账 2026-11-11] 两个实例（侧栏/移动树）被 reapplyAllFonts
     * 背靠背各调一次：文件级 static 闸会让第二次 early-return——移动树的
     * 字号停在旧值、排布却按新 pct 重排（行高/字号混合态）。 */
    if (pct == self->m_appliedFontPct) return;
    self->m_appliedFontPct = pct;
    s_navFontPct = pct; /* navScale 排布折算口径跟随（两实例同 pct）。 */
    XLabel_setTextPixelSize(&self->m_title,
                            (HSF_NAV_TITLE_FONT_PX * pct + 50) / 100);
    XWidget_setFixedHeight((XWidget*)&self->m_title,
                           (HSF_NAV_TITLE_H * pct + 50) / 100);
    for (i = 0; i < self->m_count; ++i) {
        XFont f = XWidget_font((XWidget*)&self->m_items[i].m_btn);
        XFont_setPixelSize(&f, (HSF_NAV_ITEM_FONT_PX * pct + 50) / 100);
        XWidget_setFont((XWidget*)&self->m_items[i].m_btn, &f);
        XClassDeinit(&f);
    }
    HsfNavPanel_update(self);
}

/** @brief 初始化导航面板（宽屏侧栏形态）。 */
void HsfNavPanel_init(HsfNavPanel* self, XWidget* parent)
{
    hsfNavPanel_initEx(self, parent, false);
}

/** @brief 初始化导航面板（移动态：竖屏菜单页的树形列表）。 */
void HsfNavPanel_initMobile(HsfNavPanel* self, XWidget* parent)
{
    hsfNavPanel_initEx(self, parent, true);
}

/** @brief 注册导航回调。 */
void HsfNavPanel_setNavHandler(HsfNavPanel* self, HsfNavPanelNavFn fn,
                               void* userData)
{
    if (!self) return;
    self->m_navFn = fn;
    self->m_navUserData = userData;
}

/** @brief 设当前页（高亮对应条目；展开其所属主菜单）。 */
void HsfNavPanel_setActivePage(HsfNavPanel* self, int pageIndex)
{
    const HsfNavNode* tree;
    int count = 0;
    int i;
    if (!self) return;
    self->m_activePage = pageIndex;
    /* 当前页所属主菜单自动展开（当前页在树里始终可见）。 */
    tree = HsfPages_navTree(&count);
    for (i = 0; i < count; ++i) {
        if (tree[i].pageIndex != pageIndex) continue;
        if (tree[i].childOf >= 0)
            self->m_expanded = tree[i].childOf;
        else if (hsfNavHasSubs(tree, count, i))
            self->m_expanded = i;
        break;
    }
    hsfNavApplyActive(self);
    HsfNavPanel_update(self);
}

#endif /* XWIDGET_ON && XPUSHBUTTON_ON && XLABEL_ON && XSCROLLAREA_ON */
