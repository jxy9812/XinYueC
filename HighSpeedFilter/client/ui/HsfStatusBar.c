/**
 * @file       HsfStatusBar.c
 * @brief      底部黑色状态栏实现：先铺不透明黑底再绘制状态文本（白字）。
 */
#include "HsfStatusBar.h"

#include "HsfTheme.h"
#include "XWidget_Protected.h" /* XWidget_paintImage/paintOffset（自绘用） */
#include "XPixmap.h"
#include "XPainter.h"
#include "XPalette.h" /* chrome 文本令牌钉自身盘 */
#include "XColor.h"

#if XWIDGET_ON && XFRAME_ON && XLABEL_ON


/** @brief 绘制事件：树内按自身几何先填黑底，再走标签文本绘制。 */
static void VHsfStatusBar_paintEvent(XWidget* self, XEvent* event)
{
    XImage* image;
    XPoint offset;
    XPainter painter;
    XRect rect;
    if (!self || !event || XEvent_type(event) != XEVENT_TYPE_PAINT) return;
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
    XRect_init(&rect, 0, 0, XWidget_width(self), XWidget_height(self));
    XPainter_fillRect(&painter, &rect, HSF_COLOR_STATUS_BG);
    XLabel_drawContents((XLabel*)self, &painter);
    XPainter_end(&painter);
    XPainter_deinit(&painter);
}

/** @brief 初始化 HsfStatusBar 类虚函数表。 */
XVtable* HsfStatusBar_class_init(void)
{
    XVTABLE_INIT_DEFAULT(HsfStatusBar)
    XVTABLE_INHERIT_XCLASS(XLabel);
    XVTABLE_OVERLOAD_DEFAULT(EXWidget_PaintEvent,
                             VHsfStatusBar_paintEvent);
    return XVTABLE_DEFAULT;
}

/** @brief 初始化黑色状态栏：chrome 全套配置收在本模块。 */
void HsfStatusBar_init(HsfStatusBar* self, XWidget* parent,
                       XWidgetFlags flags)
{
    if (!self) return;
    XLabel_init(&self->m_base, parent, flags);
    XClassSetVtable(self, HsfStatusBar);

    HsfTheme_applyDefaultFont((XWidget*)self);
    XLabel_setTextPixelSize((XLabel*)self, 14);
    XLabel_setAlignment((XLabel*)self,
                        XAlignment_Left | XAlignment_VCenter);
    /* 定高条减去上下 margin 后内容区易为负，缩进用 indent 不用 margin。 */
    XLabel_setIndent((XLabel*)self, 16);
    /* [chrome 文本走令牌不走 HighlightedText 2026-11-11 双主题走查]
     * 黑底不随主题翻（HSF_COLOR_STATUS_BG 恒黑），文字原先挂调色板
     * HighlightedText——旗舰深色盘把该角色定制成 #101820（其语义是
     * 「强调蓝上的文字」），落在黑底上 1.17:1 全部不可读（55 张深色
     * 截图全中）。改按 HSF_COLOR_CHROME_TEXT（两主题同值白）钉自身盘，
     * 口径见 HsfTheme.h 该令牌注释。 */
    {
        XPalette p;
        XColor c;
        const uint32_t argb = HSF_COLOR_CHROME_TEXT;
        c = XColor_create_rgb((int)((argb >> 16) & 0xFFu),
                              (int)((argb >> 8) & 0xFFu),
                              (int)(argb & 0xFFu), 0xFF);
        p = XWidget_palette((XWidget*)self);
        XPalette_setColor(&p, XPaletteColorGroup_Active,
                          XPaletteColorRole_HighlightedText, c);
        XPalette_setColor(&p, XPaletteColorGroup_Inactive,
                          XPaletteColorRole_HighlightedText, c);
        XWidget_setPalette((XWidget*)self, &p);
        XWidget_setForegroundRole((XWidget*)self,
                                  XPaletteColorRole_HighlightedText);
    }
    XWidget_setFixedHeight((XWidget*)self, HSF_STATUS_BAR_H);
    XWidget_show((XWidget*)self);
}

/** @brief 设置状态文本。 */
void HsfStatusBar_setText(HsfStatusBar* self, const char* utf8)
{
    if (!self) return;
    XLabel_setText_2((XLabel*)self, utf8);
}

#endif /* XWIDGET_ON && XFRAME_ON && XLABEL_ON */
