/**
 * @file       HsfPlaceHolder.c
 * @brief      共占位叶子页实现：标题 + 说明 + 待接入提示（层次化居中排布）。
 */
#include "HsfPlaceHolder.h"

#include "HsfTheme.h"
#include "XLabel.h"
#include "XPushButton.h"
#include "XBoxLayout.h"
#include "XObject.h"
#include "XMemory.h"
#include <string.h>

#if XWIDGET_ON && XPUSHBUTTON_ON && XLABEL_ON

/** @brief 占位页对象；m_base 必须是第一个成员。 */
typedef struct HsfPlaceHolder
{
    XWidget     m_base;      /**< 页容器；必须是第一个成员。 */
    XBoxLayout  m_layout;    /**< 页内垂直布局。 */
    XLabel      m_title;     /**< 页标题（粗）。 */
    XLabel      m_desc;      /**< 功能说明（弱色）。 */
    XLabel      m_hint;      /**< 待接入提示（更弱）。 */
    HsfMenuNavFn m_navFn;        /**< 导航回调（保留：后续页内入口用）。 */
    void*        m_navUserData;  /**< 回调用户数据。 */
    int          m_backIndex;    /**< 返回目标页（保留：后续页内返回用）。 */
} HsfPlaceHolder;

/** @brief 创建占位叶子页（堆分配，随主窗级联释放）。 */
XWidget* HsfPlaceHolder_create(XWidget* parent, const char* title,
                               const char* desc, const char* hint,
                               int backIndex, HsfMenuNavFn navFn,
                               void* userData)
{
    HsfPlaceHolder* self;
    if (!parent) return NULL;
    self = (HsfPlaceHolder*)XMemory_malloc(sizeof(HsfPlaceHolder),
                                           XCLASS_DEFAULT_MEMORY_TYPE);
    if (!self) return NULL;
    memset(self, 0, sizeof(HsfPlaceHolder));
    XWidget_init(&self->m_base, parent, 0);
    Set_Class_Memory(self, XCLASS_DEFAULT_MEMORY_TYPE);
    Set_Class_IsHeap(self, true); /* 整块随主窗级联释放。 */
    self->m_navFn = navFn;
    self->m_navUserData = userData;
    self->m_backIndex = backIndex;

    HsfTheme_applyDefaultFont(&self->m_base);

    /* 页标题（粗）。 */
    XLabel_init(&self->m_title, &self->m_base, 0);
    HsfTheme_applyDefaultFont((XWidget*)&self->m_title);
    {
        XFont f = XWidget_font((XWidget*)&self->m_title);
        XFont_setBold(&f, true);
        XWidget_setFont((XWidget*)&self->m_title, &f);
        XClassDeinit(&f);
    }
    XLabel_setText_2(&self->m_title, title ? title : "");
    XLabel_setTextPixelSize(&self->m_title, 17);
    XLabel_setAlignment(&self->m_title,
                        XAlignment_HCenter | XAlignment_VCenter);
    XWidget_setFixedHeight((XWidget*)&self->m_title, 26);
    XWidget_show((XWidget*)&self->m_title);

    /* 功能说明（弱色）。 */
    XLabel_init(&self->m_desc, &self->m_base, 0);
    HsfTheme_applyDefaultFont((XWidget*)&self->m_desc);
    XLabel_setText_2(&self->m_desc, desc ? desc : "");
    XLabel_setTextPixelSize(&self->m_desc, 13);
    XLabel_setAlignment(&self->m_desc,
                        XAlignment_HCenter | XAlignment_VCenter);
    HsfTheme_applySecondaryText((XWidget*)&self->m_desc);
    XWidget_setFixedHeight((XWidget*)&self->m_desc, 22);
    XWidget_show((XWidget*)&self->m_desc);

    /* 待接入提示（更弱）。 */
    XLabel_init(&self->m_hint, &self->m_base, 0);
    HsfTheme_applyDefaultFont((XWidget*)&self->m_hint);
    XLabel_setText_2(&self->m_hint, hint ? hint : "");
    XLabel_setTextPixelSize(&self->m_hint, 12);
    XLabel_setAlignment(&self->m_hint,
                        XAlignment_HCenter | XAlignment_VCenter);
    HsfTheme_applySecondaryText((XWidget*)&self->m_hint);
    XWidget_setFixedHeight((XWidget*)&self->m_hint, 20);
    XWidget_show((XWidget*)&self->m_hint);

    /* 页内布局：标题/说明/提示居中成一组（上下留白对称）。 */
    XBoxLayout_init(&self->m_layout, XBoxLayoutDirection_TopToBottom);
    XLayout_setContentsMargins((XLayout*)&self->m_layout, 24, 24, 24, 24);
    XLayout_setSpacing((XLayout*)&self->m_layout, 4);
    XWidget_setLayout(&self->m_base, (XLayout*)&self->m_layout);
    XBoxLayout_addStretch(&self->m_layout, 1);
    XBoxLayout_addWidget(&self->m_layout, (XWidget*)&self->m_title);
    XBoxLayout_addWidget(&self->m_layout, (XWidget*)&self->m_desc);
    XBoxLayout_addSpacing(&self->m_layout, 6);
    XBoxLayout_addWidget(&self->m_layout, (XWidget*)&self->m_hint);
    XBoxLayout_addStretch(&self->m_layout, 2);

    XWidget_show(&self->m_base);
    return &self->m_base;
}

#endif /* XWIDGET_ON && XPUSHBUTTON_ON && XLABEL_ON */
