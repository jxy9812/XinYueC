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
#include "XObject.h"
#include "XVarList.h"
#include "XMemory.h"
#include <string.h>

#if XWIDGET_ON && XPUSHBUTTON_ON && XLABEL_ON && XSCROLLAREA_ON

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

    /* 标题行（移动态无标题，滚动壳从顶开始）。 */
    titleH = self->m_mobile ? 0 : HSF_NAV_TITLE_H;
    if (!self->m_mobile) {
        XWidget_setGeometry((XWidget*)&self->m_title, HSF_NAV_PAD, 4,
                            pw - HSF_NAV_PAD * 2, 20);
        XWidget_show((XWidget*)&self->m_title);
    }

    /* 滚动壳占位（标题行下方到底部）。 */
    scrollH = ph - titleH;
    if (scrollH < 1) scrollH = 1;
    XWidget_setGeometry((XWidget*)&self->m_scroll, 0, titleH,
                        pw, scrollH);
    XWidget_show((XWidget*)&self->m_scroll);
    XWidget_raise((XWidget*)&self->m_scroll);

    /* 条目树：顶层（=主菜单）左对齐；其子项缩进挂其下，仅展开者可见。 */
    tree = HsfPages_navTree(&count);
    for (i = 0; i < self->m_count && i < count; ++i) {
        HsfNavItem* item = &self->m_items[i];
        bool isChild = (tree[i].childOf >= 0);
        if (isChild) {
            /* 子项：父主菜单未展开 → 隐藏且不占位。 */
            if (self->m_expanded != tree[i].childOf) {
                XWidget_hide((XWidget*)&item->m_btn);
                continue;
            }
            {
                int indent = self->m_mobile ? HSF_NAV_M_INDENT
                                             : HSF_NAV_INDENT;
                XWidget_setGeometry((XWidget*)&item->m_btn,
                                    HSF_NAV_PAD + indent, y,
                                    pw - HSF_NAV_PAD * 2 - indent,
                                    self->m_mobile ? HSF_NAV_M_ROW_H
                                                   : HSF_NAV_CHILD_H);
            }
            y += self->m_mobile ? HSF_NAV_M_ROW_PITCH : HSF_NAV_CHILD_PITCH;
        } else {
            XWidget_setGeometry((XWidget*)&item->m_btn, HSF_NAV_PAD, y,
                                pw - HSF_NAV_PAD * 2,
                                self->m_mobile ? HSF_NAV_M_ITEM_H
                                               : HSF_NAV_ITEM_H);
            y += self->m_mobile ? HSF_NAV_M_ITEM_PITCH : HSF_NAV_ITEM_PITCH;
            /* 移动态：主菜单之间留分区间距（同 demo 移动端菜单）。 */
            if (self->m_mobile)
                y += HSF_NAV_M_GAP;
        }
        XWidget_show((XWidget*)&item->m_btn);
    }
    /* 内容区尺寸：宽=面板宽、高=条目总高（不可拉伸 ⇒ 保滚动语义）。 */
    XWidget_resize(&self->m_content, pw, y > 1 ? y : 1);
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
    HsfTheme_applyDefaultFont(&self->m_base);
    /* 面板最小宽度：拖拽把手可改宽，但窄于此字会挤掉条目。 */
    if (!mobile)
        XWidget_setMinimumWidth(&self->m_base, 130);

    /* 标题行（移动态不建——竖屏菜单页自带页头）。 */
    XLabel_init(&self->m_title, &self->m_base, 0);
    HsfTheme_applyDefaultFont((XWidget*)&self->m_title);
    XLabel_setText_2(&self->m_title, mobile ? "" : "导航");
    XLabel_setTextPixelSize(&self->m_title, 14);
    XLabel_setAlignment(&self->m_title,
                        XAlignment_Left | XAlignment_VCenter);
    XWidget_setFixedHeight((XWidget*)&self->m_title, HSF_NAV_TITLE_H);
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
    XObject_connect_1((XObject*)&self->m_group,
                      (size_t)XButtonGroup_idClicked_signal(NULL, 0),
                      (XObject*)self, hsfNavItemSlot,
                      XConnectionType_Direct);

    XWidget_show(&self->m_base);
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
