/**
 * @file       HsfMenuPage.c
 * @brief      通用菜单列表页实现：页头 + 返回钮 + 导航按钮列装配。
 * @details    connect_1 槽首参=receiver=页容器（m_base 首成员指针相等），
 *             反推 HsfMenuPage 后查按钮→页索引表调导航回调。C 无闭包，
 *             每个按钮槽位一枚静态转发函数（同 XGuiDemo kNavSlots 口径）。
 *             堆分配载体随主窗级联释放（XMemory_malloc + Set_Class_IsHeap，
 *             demo_wrapTabPage 同款）。
 */
#include "HsfMenuPage.h"

#include "HsfTheme.h"
#include "XObject.h"
#include "XMemory.h"
#include <string.h>

#if XWIDGET_ON && XPUSHBUTTON_ON && XLABEL_ON

/** @brief 按钮槽统一出口：按按钮下标查页索引并调导航回调。 */
static void hsfMenuEmit(HsfMenuPage* page, int btnIndex)
{
    if (!page || !page->m_navFn) return;
    if (btnIndex < 0 || btnIndex >= page->m_count) return;
    page->m_navFn(page->m_navUserData, page->m_pageIndex[btnIndex]);
}

/* ---- 每按钮一枚静态槽（槽首参=receiver=页容器，m_base 首成员） ---- */
static void HsfMenuPage_slot0(XObject* r, XVarList* a) { (void)a; hsfMenuEmit((HsfMenuPage*)r, 0); }
static void HsfMenuPage_slot1(XObject* r, XVarList* a) { (void)a; hsfMenuEmit((HsfMenuPage*)r, 1); }
static void HsfMenuPage_slot2(XObject* r, XVarList* a) { (void)a; hsfMenuEmit((HsfMenuPage*)r, 2); }
static void HsfMenuPage_slot3(XObject* r, XVarList* a) { (void)a; hsfMenuEmit((HsfMenuPage*)r, 3); }
static void HsfMenuPage_slot4(XObject* r, XVarList* a) { (void)a; hsfMenuEmit((HsfMenuPage*)r, 4); }
static void HsfMenuPage_slot5(XObject* r, XVarList* a) { (void)a; hsfMenuEmit((HsfMenuPage*)r, 5); }
static void HsfMenuPage_slot6(XObject* r, XVarList* a) { (void)a; hsfMenuEmit((HsfMenuPage*)r, 6); }
static void HsfMenuPage_slot7(XObject* r, XVarList* a) { (void)a; hsfMenuEmit((HsfMenuPage*)r, 7); }

static void (*const kMenuSlots[HSF_MENU_MAX_ENTRIES])(XObject*, XVarList*) = {
    HsfMenuPage_slot0, HsfMenuPage_slot1, HsfMenuPage_slot2, HsfMenuPage_slot3,
    HsfMenuPage_slot4, HsfMenuPage_slot5, HsfMenuPage_slot6, HsfMenuPage_slot7
};

/** @brief 创建菜单列表页。 */
XWidget* HsfMenuPage_create(XWidget* parent, const char* headText,
                            const HsfMenuEntry* entries, int count,
                            int backIndex, HsfMenuNavFn navFn,
                            void* userData)
{
    HsfMenuPage* self;
    HsfMenuEntry tmp[HSF_MENU_MAX_ENTRIES];
    int n = 0;
    int i;

    if (!parent)
        return NULL;
    self = (HsfMenuPage*)XMemory_malloc(sizeof(HsfMenuPage),
                                        XCLASS_DEFAULT_MEMORY_TYPE);
    if (!self)
        return NULL;
    memset(self, 0, sizeof(HsfMenuPage));
    XWidget_init(&self->m_base, parent, 0);
    Set_Class_Memory(self, XCLASS_DEFAULT_MEMORY_TYPE);
    Set_Class_IsHeap(self, true); /* 整块随主窗级联释放。 */
    self->m_navFn = navFn;
    self->m_navUserData = userData;

    HsfTheme_applyDefaultFont(&self->m_base);

    /* 条目装配：**不建「返回」项**（同 XGuiWindowDemo 口径——导航由
       左侧树/☰ 主菜单页承担，页内不重复返回入口）。
       backIndex 参数保留仅为接口兼容，不参与装配。 */
    (void)backIndex;
    for (i = 0; entries && i < count && n < HSF_MENU_MAX_ENTRIES; ++i, ++n) {
        tmp[n].name = entries[i].name;
        tmp[n].pageIndex = entries[i].pageIndex;
    }
    self->m_count = n;

    /* 页头。 */
    if (headText) {
        XLabel_init(&self->m_head, &self->m_base, 0);
        HsfTheme_applyDefaultFont((XWidget*)&self->m_head);
        XLabel_setText_2(&self->m_head, headText);
        XLabel_setTextPixelSize(&self->m_head, 16);
        XLabel_setAlignment(&self->m_head,
                            XAlignment_Left | XAlignment_VCenter);
        XWidget_show((XWidget*)&self->m_head);
    }

    /* 导航按钮列（点击→静态槽→导航回调透传页索引）。 */
    for (i = 0; i < n; ++i) {
        XPushButton_init(&self->m_btns[i], &self->m_base, 0);
        HsfTheme_applyDefaultFont((XWidget*)&self->m_btns[i]);
        XPushButton_setText_2(&self->m_btns[i], tmp[i].name);
        self->m_pageIndex[i] = tmp[i].pageIndex;
        XObject_connect((XObject*)&self->m_btns[i],
                          XSignal(XAbstractButton_clicked_signal),
                          (XObject*)self, kMenuSlots[i],
                          XConnectionType_Direct);
        XWidget_show((XWidget*)&self->m_btns[i]);
    }

    /* 页内垂直布局：页头 + 按钮列 + 尾部伸展。 */
    XBoxLayout_init(&self->m_layout, XBoxLayoutDirection_TopToBottom);
    if (headText)
        XBoxLayout_addWidget(&self->m_layout, (XWidget*)&self->m_head);
    for (i = 0; i < n; ++i)
        XBoxLayout_addWidget(&self->m_layout, (XWidget*)&self->m_btns[i]);
    XBoxLayout_addStretch(&self->m_layout, 1);
    XLayout_setContentsMargins((XLayout*)&self->m_layout,
                               24, 16, 24, 24);
    XLayout_setSpacing((XLayout*)&self->m_layout, 8);
    XWidget_setLayout(&self->m_base, (XLayout*)&self->m_layout);

    XWidget_show(&self->m_base);
    return &self->m_base;
}

#endif /* XWIDGET_ON && XPUSHBUTTON_ON && XLABEL_ON */
