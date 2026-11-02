/**
 * @file       HsfStack.c
 * @brief      堆栈窗口实现：宿主 + XStackedLayout 装配与切页收口。
 */
#include "HsfStack.h"

#include <string.h>

#if XWIDGET_ON && XLAYOUT_ON && XLAYOUT_STACKED_ON

/** @brief 初始化堆栈窗口。 */
void HsfStack_init(HsfStack* self, XWidget* parent)
{
    if (!self) return;
    memset(self, 0, sizeof(HsfStack));
    XWidget_init(&self->m_host, parent, 0);
    /* 中段宿主缺省 sizePolicy 改双向 Expanding（同 XGuiDemo：布局按
     * hint 落尺寸会让余量整段留白，中段必须吃满伸展）。 */
    XWidget_setSizePolicy(&self->m_host,
                          XWidgetSizePolicy_Expanding,
                          XWidgetSizePolicy_Expanding);
    XStackedLayout_init(&self->m_layout);
    XWidget_setLayout(&self->m_host, (XLayout*)&self->m_layout);
    XWidget_show(&self->m_host);
}

/** @brief 取宿主控件。 */
XWidget* HsfStack_host(HsfStack* self)
{
    return self ? &self->m_host : NULL;
}

/** @brief 向堆叠末尾追加页面控件。 */
int HsfStack_addPage(HsfStack* self, XWidget* page)
{
    if (!self || !page) return -1;
    return XStackedLayout_addWidget(&self->m_layout, page);
}

/** @brief 切换当前页。 */
void HsfStack_go(HsfStack* self, int index)
{
    if (!self) return;
    XStackedLayout_setCurrentIndex(&self->m_layout, index);
}

/** @brief 取当前页索引。 */
int HsfStack_current(HsfStack* self)
{
    if (!self) return -1;
    return XStackedLayout_currentIndex(&self->m_layout);
}

#endif /* XWIDGET_ON && XLAYOUT_ON && XLAYOUT_STACKED_ON */
