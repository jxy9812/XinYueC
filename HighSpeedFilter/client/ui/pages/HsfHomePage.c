/**
 * @file       HsfHomePage.c
 * @brief      主界面页实现：标题/副标题/快捷入口/状态行的层次排布。
 * @details    层次：标题（20px 粗）→ 副标题（13px 弱色）→ 快捷入口
 *             （横屏一行 / 竖屏一列，随形态切换）→ 底部状态行。
 *             快捷入口=三棵子树的直达按钮（监视/数据/设置），点击即
 *             跳到该子树的主菜单页。
 */
#include "HsfHomePage.h"

#include "HsfTheme.h"
#include "XLabel.h"
#include "XPushButton.h"
#include "XBoxLayout.h"
#include "XObject.h"
#include "XMemory.h"
#include <string.h>

#if XWIDGET_ON && XPUSHBUTTON_ON && XLABEL_ON

/** @brief 主界面页对象；m_base 必须是第一个成员。 */
typedef struct HsfHomePage
{
    XWidget     m_base;      /**< 页容器；必须是第一个成员。 */
    XBoxLayout  m_layout;    /**< 页内垂直布局。 */
    XLabel      m_title;     /**< 主标题。 */
    XLabel      m_subtitle;  /**< 副标题。 */
    XBoxLayout  m_actionsRow;/**< 快捷入口行（横屏横排/竖屏竖排）。 */
    XPushButton m_actions[3];/**< 快捷入口按钮。 */
    XLabel      m_status;    /**< 底部状态行。 */
    HsfMenuNavFn m_navFn;    /**< 导航回调。 */
    void*        m_navUserData; /**< 回调用户数据。 */
} HsfHomePage;

/** @brief 快捷入口点击槽：按按钮下标跳对应子树主菜单页。 */
static void hsfHomeActionSlot(XObject* receiver, XVarList* args)
{
    HsfHomePage* self = (HsfHomePage*)receiver;
    static const int kTargets[3] = {
        HSF_PAGE_MONITOR_MENU, HSF_PAGE_DATA_MENU, HSF_PAGE_SETTINGS_MENU
    };
    int idx = -1;
    (void)args;
    if (!self || !self->m_navFn) return;
    /* 槽按按钮分派：接收者=页容器，按钮下标经组 id 传入不可得 →
       三个按钮各用一枚静态转发（m_owner 回到页）。 */
    if (receiver == (XObject*)&self->m_actions[0]) idx = 0;
    else if (receiver == (XObject*)&self->m_actions[1]) idx = 1;
    else if (receiver == (XObject*)&self->m_actions[2]) idx = 2;
    if (idx < 0) return;
    self->m_navFn(self->m_navUserData, kTargets[idx]);
}

/** @brief 创建主界面页。 */
XWidget* HsfHomePage_create(XWidget* parent, HsfMenuNavFn navFn,
                            void* userData)
{
    HsfHomePage* self;
    static const char* const kActions[3] = {
        "\xE5\xAE\x9E\xE6\x97\xB6\xE7\x9B\x91\xE8\xA7\x86", /* 实时监视 */
        "\xE6\x95\xB0\xE6\x8D\xAE\xE6\x9F\xA5\xE8\xAF\xA2", /* 数据查询 */
        "\xE5\x8F\x82\xE6\x95\xB0\xE8\xAE\xBE\xE7\xBD\xAE"  /* 参数设置 */
    };
    int i;

    if (!parent) return NULL;
    self = (HsfHomePage*)XMemory_malloc(sizeof(HsfHomePage),
                                       XCLASS_DEFAULT_MEMORY_TYPE);
    if (!self) return NULL;
    memset(self, 0, sizeof(HsfHomePage));
    XWidget_init(&self->m_base, parent, 0);
    Set_Class_Memory(self, XCLASS_DEFAULT_MEMORY_TYPE);
    Set_Class_IsHeap(self, true); /* 整块随主窗级联释放。 */
    self->m_navFn = navFn;
    self->m_navUserData = userData;

    HsfTheme_applyDefaultFont(&self->m_base);

    /* 主标题。 */
    XLabel_init(&self->m_title, &self->m_base, 0);
    HsfTheme_applyDefaultFont((XWidget*)&self->m_title);
    {
        XFont f = XWidget_font((XWidget*)&self->m_title);
        XFont_setBold(&f, true);
        XWidget_setFont((XWidget*)&self->m_title, &f);
        XClassDeinit(&f);
    }
    XLabel_setText_2(&self->m_title,
                     "\xE9\xAB\x98\xE9\x80\x9F\xE6\xBB\xA4\xE8\xBF\x87\xE5\x99\xA8"
                     "\xE5\xAE\xA2\xE6\x88\xB7\xE7\xAB\xAF");
    XLabel_setTextPixelSize(&self->m_title, 20);
    XLabel_setAlignment(&self->m_title,
                        XAlignment_HCenter | XAlignment_VCenter);
    XWidget_setFixedHeight((XWidget*)&self->m_title, 30);
    XWidget_show((XWidget*)&self->m_title);

    /* 副标题。 */
    XLabel_init(&self->m_subtitle, &self->m_base, 0);
    HsfTheme_applyDefaultFont((XWidget*)&self->m_subtitle);
    XLabel_setText_2(&self->m_subtitle,
                     "\xE4\xB8\x8A\xE4\xBD\x8E\xE4\xBB\xB6\xE4\xBB\x8B\xE7\x89\x88"
                     "\xE6\x9C\x8D\xE5\x8A\xA1\xE5\x99\xA8\xE2\x80\x94\xE2\x80\x94"
                     "\xE4\xBF\xA1\xE5\x8F\xB7\xE8\xBF\x87\xE6\xBB\xA4\xE4\xB8\x8E"
                     "\xE6\x95\xB0\xE6\x8D\xAE\xE9\x87\x87\xE6\x8E\xA5");
    XLabel_setTextPixelSize(&self->m_subtitle, 13);
    XLabel_setAlignment(&self->m_subtitle,
                        XAlignment_HCenter | XAlignment_VCenter);
    XWidget_setForegroundRole((XWidget*)&self->m_subtitle,
                              XPaletteColorRole_Mid);
    XWidget_setFixedHeight((XWidget*)&self->m_subtitle, 22);
    XWidget_show((XWidget*)&self->m_subtitle);

    /* 快捷入口（横屏一行 / 竖屏一列，方向由 adapt 切换）。 */
    XBoxLayout_init(&self->m_actionsRow, XBoxLayoutDirection_LeftToRight);
    XLayout_setSpacing((XLayout*)&self->m_actionsRow, 12);
    for (i = 0; i < 3; ++i) {
        XPushButton_init(&self->m_actions[i], &self->m_base, 0);
        HsfTheme_applyDefaultFont((XWidget*)&self->m_actions[i]);
        XPushButton_setText_2(&self->m_actions[i], kActions[i]);
        XWidget_setFixedHeight((XWidget*)&self->m_actions[i], 40);
        XObject_connect_1((XObject*)&self->m_actions[i],
                          XSignal(XAbstractButton_clicked_signal),
                          (XObject*)&self->m_actions[i],
                          hsfHomeActionSlot,
                          XConnectionType_Direct);
        XBoxLayout_addWidget(&self->m_actionsRow,
                             (XWidget*)&self->m_actions[i]);
        XWidget_show((XWidget*)&self->m_actions[i]);
    }
    XBoxLayout_addStretch(&self->m_actionsRow, 1);

    /* 底部状态行。 */
    XLabel_init(&self->m_status, &self->m_base, 0);
    HsfTheme_applyDefaultFont((XWidget*)&self->m_status);
    XLabel_setText_2(&self->m_status,
                     "\xE7\x8A\xB6\xE6\x80\x81\xEF\xBC\x9A\xE5\xB7\xB2\xE8\xBF\x9E"
                     "\xE6\x8E\xA5\xE2\x80\xA2\xE6\xA0\x87\xE5\x87\x86\xE9\x87\x87"
                     "\xE5\x8F\x91\xE6\xA0\x87\xE4\xB8\xAD");
    XLabel_setTextPixelSize(&self->m_status, 13);
    XLabel_setAlignment(&self->m_status,
                        XAlignment_Left | XAlignment_VCenter);
    XWidget_setForegroundRole((XWidget*)&self->m_status,
                              XPaletteColorRole_Mid);
    XWidget_setFixedHeight((XWidget*)&self->m_status, 22);
    XWidget_show((XWidget*)&self->m_status);

    /* 页内布局：标题/副标题居上、快捷入口居中、状态行贴底。 */
    XBoxLayout_init(&self->m_layout, XBoxLayoutDirection_TopToBottom);
    XLayout_setContentsMargins((XLayout*)&self->m_layout, 24, 28, 24, 20);
    XLayout_setSpacing((XLayout*)&self->m_layout, 6);
    XWidget_setLayout(&self->m_base, (XLayout*)&self->m_layout);
    XBoxLayout_addWidget(&self->m_layout, (XWidget*)&self->m_title);
    XBoxLayout_addWidget(&self->m_layout, (XWidget*)&self->m_subtitle);
    XBoxLayout_addSpacing(&self->m_layout, 28);
    XBoxLayout_addLayout(&self->m_layout, (XLayout*)&self->m_actionsRow);
    XBoxLayout_addStretch(&self->m_layout, 1);
    XBoxLayout_addWidget(&self->m_layout, (XWidget*)&self->m_status);

    XWidget_show(&self->m_base);
    return &self->m_base;
}

/** @brief 形态自适应：横屏快捷入口横排一行，竖屏改竖排一列。 */
void HsfHomePage_adapt(XWidget* page, bool compact)
{
    HsfHomePage* self = (HsfHomePage*)page;
    int i;
    if (!self) return;
    XBoxLayout_setDirection(&self->m_actionsRow,
                            compact ? XBoxLayoutDirection_TopToBottom
                                    : XBoxLayoutDirection_LeftToRight);
    /* 横屏：按钮按内容宽度并居中；竖屏：按钮铺满内容宽（更好点按）。 */
    for (i = 0; i < 3; ++i) {
        XWidget_setMinimumWidth((XWidget*)&self->m_actions[i],
                                compact ? 200 : 120);
        /* 横屏：三个入口等分内容宽（卡片行，视觉整齐）；竖屏：竖排整宽。 */
        XBoxLayout_setStretch(&self->m_actionsRow, i, compact ? 0 : 1);
    }
    XLayout_activate((XLayout*)&self->m_layout);
}

#endif /* XWIDGET_ON && XPUSHBUTTON_ON && XLABEL_ON */
