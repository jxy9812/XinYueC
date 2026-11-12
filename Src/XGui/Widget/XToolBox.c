/**
 * @file       XToolBox.c
 * @brief      工具箱控件实现（对标 Qt 6.8 QToolBox 全部公共 API）。
 * @details    与同名头文件的公共 API 一一对应；内部实现细节见
 *             头文件 @note 与函数级 Doxygen 注释。
 * @author     XinYueC 团队
 */

#if XGUI_ON

#include "XToolBox.h"
#include "XToolBox_Protected.h"
#include "XStringUtils.h"
#include "XString.h"
#include "XStyle.h"
#include "XStyleOption.h"
#include "XMemory.h"
#include "XEvent.h"
#include "XVarList.h"
#include "XGuiConfig.h"
#include "XIcon.h"     /* [P0-80] 页头图标绘制（路径 → XIcon → XImage）。 */
#include "XPixmap.h"
#include "XToolTip.h"  /* [P0-36] 条目提示悬停弹出（对标 QToolTip）。 */
#if XSCROLLAREA_ON && XABSTRACTSCROLLAREA_ON
#include "XScrollArea.h" /* 页容器：对标 QToolBox 每页一只 QScrollArea。 */
#endif

#include "XAlgorithm.h"
#include "XWidget_Protected.h"

#if XWIDGET_ON && XFRAME_ON && XTOOLBOX_ON

#define XTOOLBOX_HEADER_H 22  /**< 页头条高（paint/layout/mouse 共用）。 */
/** @brief 页头图标绘制尺寸（P0-80；对标 QToolBox 页头按钮的图标位）。 */
#define XTOOLBOX_ICON_PX 16

/* ==================== 内部条目 ==================== */

typedef struct XToolBoxItem
{
    XWidget* widget;   /**< 页面控件（借用，归调用方/容器）。 */
    XWidget* area;     /**< 页容器（XScrollArea；本工具箱拥有）。Qt 把每页
                            装进一只 QScrollArea（qtoolbox.cpp:322-334）
                            ⇒ 页的 parentWidget 是滚动区的 viewport、页内
                            可滚动。XSCROLLAREA_ON=0 时为 NULL（退化回
                            直接挂父）。 */
    XString* text;     /**< 页头文本（对象拥有）。 */
    XString* icon;     /**< 页头图标路径（对象拥有；可为 NULL）。 */
    XString* tooltip;  /**< 条目提示（对标 QToolBox itemToolTip；对象拥有；可为 NULL）。 */
    bool enabled;      /**< 条目启用。 */
} XToolBoxItem;

static XToolBoxItem* xtb2_itemCreate(XWidget* widget, const char* text)
{
    XToolBoxItem* item =
        (XToolBoxItem*)XMalloc_System(sizeof(XToolBoxItem));
    if (!item) return NULL;
    item->widget = widget;
    item->area = NULL;
    item->enabled = true;
    item->text = XString_create_utf8(text ? text : "");
    item->icon = NULL;
    item->tooltip = NULL;
    return item;
}

static void xtb2_itemDestroy(XToolBoxItem* item)
{
    if (!item) return;
    if (item->text) {
        XClassDelete(item->text);
        item->text = NULL;
    }
    if (item->icon) {
        XClassDelete(item->icon);
        item->icon = NULL;
    }
    if (item->tooltip) {
        XClassDelete(item->tooltip);
        item->tooltip = NULL;
    }
    XFree_System(item);
}

static int xtb2_currentIndexOf(const XToolBox* self)
{
    if (!self || self->m_currentIndex < 0) return -1;
    if (!self->m_items ||
        self->m_currentIndex >=
            (int)XVector_size_base((const XContainer*)self->m_items))
        return -1;
    return self->m_currentIndex;
}

/** @brief 激活页占用的页高（layout/paint/mousePress 三处共用口径）。
 *  @details 复扫 #55 钉死：页几何此前恒占满内容区（h-n*22），裸控件
 *  页（如 QLabel，自身 Left|AlignVCenter）会把内容推进内容区垂直
 *  中点——实测「工具箱页一」y≈323 ≈ 22 + (430-44-16)/2 + 行高，与
 *  xtb2_layout 的输出逐像素吻合（xtb2_layout 确被调用，几何链无恙；
 *  「内容仍居中」的真实来源是页矩形被拉满后裸控件按自身对齐取中）。
 *  对标 Qt 观感：qtoolbox.cpp:323-336 页装在 sv(QScrollArea)、VBox 中
 *  button/page 交替，页内内容由页自身布局首行顶置；本库条目常为裸
 *  控件（无页内布局、sizeHint 非虚槽不可查），改为页高按其自身现高
 *  顶置（构造/调用方给定；超过内容区或为 0 时才占满），等价 Qt
 *  「页容器 VBox 首行置顶」的视觉结果，后续页头继续顺次下移。 */
static int xtb2_pageHeight(const XToolBox* self, int count)
{
    XWidget* current;
    int headerTotal;
    int h;
    int contentH;
    int ph;
    if (!self) return 0;
    h = XWidget_height((XWidget*)self);
    headerTotal = (count > 0 ? count : 0) * XTOOLBOX_HEADER_H;
    contentH = h > headerTotal ? h - headerTotal : 0;
    if (contentH <= 0) return 0;
    current = XToolBox_currentWidget(self);
    if (!current) return contentH;
    ph = XWidget_height(current);
    /* 页自身现高不可用（0：尚未 setGeometry，或几何曾被瞬态挤压写成
     * 0）时按 sizeHint 推导（对标 QToolBox 页高经 QVBoxLayout 中
     * QScrollArea::sizeHint 委托页自身 sizeHint 的口径）。 */
    if (ph <= 0) {
        XSize hint = XWidget_sizeHint_base(current);
        ph = hint.height;
    }
    if (ph <= 0 || ph > contentH) return contentH;
    return ph;
}

/** @brief 条目的页宿主：有页容器（XScrollArea）用容器，否则退回页控件
 *         本身（XSCROLLAREA_ON=0 的退化配置）。 */
static XWidget* xtb2_itemHost(const XToolBoxItem* item)
{
    if (!item) return NULL;
    return item->area ? item->area : item->widget;
}

/** @brief 下标 index 的页宿主（越界返回 NULL）。 */
static XWidget* xtb2_hostAt(const XToolBox* self, int index)
{
    XToolBoxItem** item;
    if (!self || !self->m_items || index < 0 ||
        index >= (int)XVector_size_base((const XContainer*)self->m_items))
        return NULL;
    item = (XToolBoxItem**)XVector_at_base(self->m_items, index);
    return xtb2_itemHost(item ? *item : NULL);
}

/** @brief 为条目建页容器（对标 qtoolbox.cpp:322-336：
 *         `QScrollArea *sv = new QScrollArea(q); sv->setFrameShape(NoFrame);
 *         sv->setWidget(widget); sv->setWidgetResizable(true);`）。
 *  @details Qt 里页的 parentWidget() 是滚动区的 **viewport**（不是工具箱），
 *           页内容超高时页内滚动。X 侧用 XScrollArea 承载同一语义：
 *           容器归本工具箱，页归容器视口，widgetResizable=true 让页随容器
 *           尺寸伸缩（与 Qt 同）。
 *  @param self 工具箱。
 *  @param widget 页面控件（调用方借用，转入容器视口）。
 *  @return 页容器；XSCROLLAREA_ON=0 时返回 NULL，页直接挂到工具箱。 */
static XWidget* xtb2_createArea(XToolBox* self, XWidget* widget)
{
#if XSCROLLAREA_ON && XABSTRACTSCROLLAREA_ON
    XScrollArea* area;
    if (!self || !widget) return NULL;
    area = XScrollArea_create((XWidget*)self, 0);
    if (area) {
        XScrollArea_setWidget(area, widget);
        XScrollArea_setWidgetResizable(area, true);
        return (XWidget*)area;
    }
    /* 建容器失败：退化为直接挂父（不丢页）。 */
    XWidget_setParent(widget, (XWidget*)self, 0);
    return NULL;
#else
    if (self && widget) XWidget_setParent(widget, (XWidget*)self, 0);
    return NULL;
#endif
}

/** @brief 布局：当前页容器置于激活页头之下（页高按 sizeHint 顶置，
 *           见 xtb2_pageHeight；其余页容器隐藏）。
 *  @details 对齐 QToolBox::setCurrentIndex 的显隐语义：非当前页一律
 *           setVisible(false)，防止外部 show() 绕过工具箱的页面管理
 *           （Qt 中页面装在被隐藏的 ScrollArea 容器里，容器不可见即整页
 *           不可见）。 */
static void xtb2_layout(XToolBox* self)
{
    XWidget* current;
    XWidget* currentHost;
    XRect r;
    int w = XWidget_width((XWidget*)self);
    int64_t i;
    int64_t n;
    if (!self) return;
    n = self->m_items ? XVector_size_base((const XContainer*)self->m_items)
                      : 0;
    current = XToolBox_currentWidget(self);
    currentHost = xtb2_hostAt(self, self->m_currentIndex);
    for (i = 0; i < n; ++i) {
        XToolBoxItem** item =
            (XToolBoxItem**)XVector_at_base(self->m_items, i);
        XWidget* host;
        if (!item || !*item) continue;
        host = xtb2_itemHost(*item);
        if (!host) continue;
        if (host != currentHost && XWidget_isVisible(host))
            XWidget_setVisible(host, false);
    }
    if (!current || !currentHost) return;
    /* 内容紧跟激活页头正下方（对标 QToolBox 的 VBox 布局：button 与
       page 交替入列，qtoolbox.cpp:335-336 layout->addWidget(button)/
       addWidget(sv)）；页高按 sizeHint 顶置（见 xtb2_pageHeight）。 */
    {
        int pageY = (self->m_currentIndex + 1) * XTOOLBOX_HEADER_H;
        int pageH;
        if (self->m_currentIndex < 0) pageY = 0;
        pageH = xtb2_pageHeight(self, (int)n);
        /* 容器瞬态挤压（contentH≤0，如页签容器拉伸中态）时不写页几何：
         * 把 0 高写进页矩形后，后续布局会把该 0 当「页自身现高」再走
         * 占满回退，页内容从此悬空居中（night #55 初始态内容距页头
         * 127px 的根因链：boxH=6 瞬态 → 页高被写 0 → boxH=310/430
         * 布局回退 contentH）。保留页现几何即可让随后的正常布局按
         * 页真实高度（如子控件默认 100x30）顶置。 */
        if (pageH <= 0) return;
        XRect_init(&r, 0, pageY, w, pageH);
    }
    /* 几何写回**页容器**（页本身由 XScrollArea 的 widgetResizable 排版）。 */
    XWidget_setGeometryRect(currentHost, &r);
}

static void xtb2_emitChanged(XToolBox* self, int index)
{
    XVarList* args = XVarList_Create(XVar(int, index));
    if (!args) return;
    if (self && ((XObject*)self)->m_signalSlot) {
        XObject_emitSignal((XObject*)self,
                           (size_t)XToolBox_currentChanged_signal, args,
                           NULL, NULL, XEVENT_PRIORITY_NORMAL);
    } else {
        XVarList_delete(args);
    }
}

/* ==================== 保护虚槽（对标 itemInserted/itemRemoved） ============ */

void XToolBox_itemInserted(XToolBox* self, int index)
{
    void (*fn)(XToolBox*, int);
    if (!self) return;
    if (!XClassGetVtable(self)) return;
    fn = XClassGetVirtualFunc(self, EXToolBox_ItemInserted,
                              void (*)(XToolBox*, int));
    if (fn && fn != XToolBox_itemInserted_base)
        fn(self, index);
    else
        XToolBox_itemInserted_base(self, index);
}

void XToolBox_itemInserted_base(XToolBox* self, int index)
{
    /* 对标 Qt 缺省实现：空操作（qtoolbox.cpp:691-695）。 */
    (void)self;
    (void)index;
}

void XToolBox_itemRemoved(XToolBox* self, int index)
{
    void (*fn)(XToolBox*, int);
    if (!self) return;
    if (!XClassGetVtable(self)) return;
    fn = XClassGetVirtualFunc(self, EXToolBox_ItemRemoved,
                              void (*)(XToolBox*, int));
    if (fn && fn != XToolBox_itemRemoved_base)
        fn(self, index);
    else
        XToolBox_itemRemoved_base(self, index);
}

void XToolBox_itemRemoved_base(XToolBox* self, int index)
{
    /* 对标 Qt 缺省实现：空操作（qtoolbox.cpp:702-706）。 */
    (void)self;
    (void)index;
}

/* ==================== 事件处理 ==================== */

/** @brief 绘制页头图标（路径 → XIcon → XImage，页头行内左起；P0-80，
 *  版式先例 XTabBar 的 xtabbar_drawTabIcon）。
 * @param painter 绘制器。
 * @param item 条目（icon 路径承载）。
 * @param x 图标左缘。
 * @param y 图标上缘。
 * @return 无。
 */
static void xtb2_drawItemIcon(XPainter* painter, const XToolBoxItem* item,
                              int x, int y)
{
    const char* path;
    XIcon icon;
    XPixmap pixmap;
    XImage image;
    if (!painter || !item || !item->icon) return;
    path = XString_toUtf8(item->icon);
    if (!path || !path[0]) return;
    XIcon_init_file_2(&icon, path);
    if (XIcon_isNull(&icon)) {
        XClassDeinit((XClass*)&icon);
        return;
    }
    XPixmap_init(&pixmap);
    XIcon_pixmap(&icon, XTOOLBOX_ICON_PX, XTOOLBOX_ICON_PX, XIconMode_Normal,
                 XIconState_Off, &pixmap);
    XImage_init(&image);
    XPixmap_toImage(&pixmap, &image);
    if (XImage_width(&image) > 0 && XImage_height(&image) > 0)
        XPainter_drawImage(painter, &image, x, y);
    XClassDeinit((XClass*)&image);
    XClassDeinit((XClass*)&pixmap);
    XClassDeinit((XClass*)&icon);
}

/** @brief 条目是否带页头图标（路径非空即视为带图；P0-80）。 */
static bool xtb2_itemHasIcon(const XToolBoxItem* item)
{
    const char* path;
    if (!item || !item->icon) return false;
    path = XString_toUtf8(item->icon);
    return path && path[0];
}

/** @brief y 坐标 → 页头条目索引（-1 = 非页头区；P0-36/P0-80 的命中
 *  与 paint/layout 同源）。
 * @details 与 VX_toolBox_mousePressEvent / xtb2_layout / 页头绘制共用
 *          同一映射：激活页之前的页头每 XTOOLBOX_HEADER_H px 一个，
 *          激活页之下预留整块页高，其后页头继续顺排；激活页内容区与
 *          越界区返回 -1。 */
static int xtb2_headerIndexAt(const XToolBox* box, int y)
{
    int count;
    int pageH;
    int index;
    if (!box) return -1;
    count = box->m_items
                ? (int)XVector_size_base((const XContainer*)box->m_items) : 0;
    if (count <= 0) return -1;
    pageH = xtb2_pageHeight(box, count);
    if (box->m_currentIndex >= 0 && pageH > 0) {
        int contentTop = (box->m_currentIndex + 1) * XTOOLBOX_HEADER_H;
        if (y >= contentTop && y < contentTop + pageH)
            return -1; /* 激活页内容区：不是页头（点击归内容控件）。 */
        if (y >= contentTop + pageH) {
            /* 激活页之后的页头区。 */
            int below = y - (contentTop + pageH);
            index = box->m_currentIndex + 1 + below / XTOOLBOX_HEADER_H;
        } else {
            /* 激活页及其之前的页头区（含激活页头本身）。 */
            index = y / XTOOLBOX_HEADER_H;
        }
    } else {
        index = y / XTOOLBOX_HEADER_H;
    }
    if (index < 0 || index >= count) return -1;
    return index;
}

/* ---- 悬停提示（P0-36）：页头命中变化即弹/收（对标 Qt 悬停 tooltip
 * 链；XWidget 无 ToolTip 事件派发，走 MouseMove 重算命中）。 ---- */

/** @brief 悬停换条：索引变化时按条目提示弹/收，锚到页头条矩形。 */
static void xtb2_hoverUpdate(XToolBox* box, int index)
{
    XToolBoxItem** slot;
    const char* tipUtf8;
    XPoint origin;
    XRect anchor;
    int w;
    if (!box) return;
    if (index == box->m_hoverIndex) return; /* 同条目不重弹。 */
    if (index < 0 || index >= XToolBox_count(box)) {
        box->m_hoverIndex = -1;
        XToolTip_hideText();
        return;
    }
    box->m_hoverIndex = (int16_t)index;
    slot = (XToolBoxItem**)XVector_at_base(box->m_items, index);
    tipUtf8 = (slot && *slot && (*slot)->tooltip)
                  ? XString_toUtf8((*slot)->tooltip) : NULL;
    if (!tipUtf8 || !tipUtf8[0]) {
        XToolTip_hideText(); /* 换到无提示条目：收起旧提示。 */
        return;
    }
    w = XWidget_width((XWidget*)box);
    origin.x = 0;
    origin.y = index * XTOOLBOX_HEADER_H;
    origin = XWidget_mapToGlobal((XWidget*)box, &origin);
    anchor.x = origin.x;
    anchor.y = origin.y;
    anchor.width = w > 0 ? w : 1;
    anchor.height = XTOOLBOX_HEADER_H;
    XToolTip_showText_2(origin.x, origin.y, tipUtf8, (XWidget*)box, &anchor,
                        XWidget_toolTipDuration((XWidget*)box));
}

/** @brief 悬停复位并收提示（LEAVE / 条目增删共用）。 */
static void xtb2_hoverReset(XToolBox* box)
{
    if (!box) return;
    if (box->m_hoverIndex >= 0 || XToolTip_isVisible())
        XToolTip_hideText();
    box->m_hoverIndex = -1;
}

static void VX_toolBox_paintEvent(XWidget* self, XEvent* event)
{
    XToolBox* box = (XToolBox*)self;
    XPainter painter;
    XImage* image;
    XPoint offset;
    XRect head;
    XRect line;
    uint32_t highlight;
    uint32_t highlightedText;
    uint32_t windowText;
    uint32_t mid;
    int64_t i;
    int64_t n;
    int y = 0;
    int w = XWidget_width(self);
    int pageH;
    if (!box || !event) return;
    n = box->m_items
            ? XVector_size_base((const XContainer*)box->m_items) : 0;
    /* 页高与 xtb2_layout 同口径（xtb2_pageHeight）：激活页占其页头
     * 之下的整块页高，后续页头依次排在页之后（对标 QToolBox VBox）。 */
    pageH = xtb2_pageHeight(box, (int)n);
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
                           XPaletteColorRole_Highlight);
        highlight = XColor_rgba(&c);
        c = XPalette_color(&palette, XPaletteColorGroup_Current,
                           XPaletteColorRole_HighlightedText);
        highlightedText = XColor_rgba(&c);
        c = XPalette_color(&palette, XPaletteColorGroup_Current,
                           XPaletteColorRole_WindowText);
        windowText = XColor_rgba(&c);
        c = XPalette_color(&palette, XPaletteColorGroup_Current,
                           XPaletteColorRole_Mid);
        mid = XColor_rgba(&c);
    }
#else
    highlight = 0xFF3080C0u;
    highlightedText = 0xFFFFFFFFu;
    windowText = 0xFF000000u;
    mid = 0xFF808080u;
#endif /* XPALETTE_ON */
    if (box->m_items) {
        for (i = 0; i < n; ++i) {
            XToolBoxItem** item =
                (XToolBoxItem**)XVector_at_base(box->m_items, i);
            bool hasIcon;
            bool selected;
            if (!item || !*item) continue;
            hasIcon = xtb2_itemHasIcon(*item);
            selected = ((int)i == box->m_currentIndex);
            XRect_init(&head, 0, y, w, XTOOLBOX_HEADER_H);
#if XSTYLE_ON
            if (XWidget_style((const XWidget*)self) != NULL) {
                XStyle* style = XWidget_style((const XWidget*)self);
                XStyleOption opt;
                XStyleOption_init(&opt, XStyleCE_ToolBoxTab);
                opt.m_rect = head;
                opt.m_state = XWidget_isEnabled((XWidget*)box)
                    ? XStyleState_Enabled : 0;
                if (selected)
                    opt.m_state |= XStyleState_Selected;
                opt.m_tabSelected = selected;
                opt.m_text = (*item)->text
                    ? XString_toUtf8((*item)->text) : "";
#if XPALETTE_ON
                opt.m_palette = XWidget_palette((XWidget*)box);
#endif
                if (hasIcon) {
                    /* [P0-80] 样式层 ToolBoxTab 文本定在 r.x+6（不感知
                     * 图标），带图标时传空文本让样式只画底/边框，图标
                     * 与文本由控件侧左起自绘（版式先例 XTabBar 带图标
                     * 自绘组，避免叠字）。 */
                    opt.m_text = "";
                }
                XStyle_drawControl(style, XStyleCE_ToolBoxTab, &opt,
                                   &painter, (XWidget*)box);
                if (hasIcon) {
                    xtb2_drawItemIcon(&painter, *item, 6, y + 3);
                    XPainter_drawText(&painter,
                                      6 + XTOOLBOX_ICON_PX + 4, y + 15,
                                      (*item)->text
                                          ? XString_toUtf8((*item)->text)
                                          : "",
                                      selected ? highlightedText
                                               : windowText);
                }
                y += XTOOLBOX_HEADER_H;
                XRect_init(&line, 0, y - 1, w, 1);
                XPainter_fillRect(&painter, &line, windowText);
                /* 激活页之后预留整块页高，后续页头排在页下方（与
                 * xtb2_layout 的页面定位同口径，对标 QToolBox VBox）。 */
                if (selected)
                    y += pageH;
                continue;
            }
#endif /* XSTYLE_ON */
            if (selected)
                XPainter_fillRect(&painter, &head, highlight);
            else
                XPainter_fillRect(&painter, &head, mid);
            if (hasIcon) {
                /* [P0-80] 页头行内左起画 16px 图标，文本 x 从
                 * 「图标宽 + 间距」起；无图标保持原版式（文本 x=6）。 */
                xtb2_drawItemIcon(&painter, *item, 6, y + 3);
                XPainter_drawText(&painter,
                                  6 + XTOOLBOX_ICON_PX + 4, y + 15,
                                  (*item)->text
                                      ? XString_toUtf8((*item)->text) : "",
                                  windowText);
            } else {
                XPainter_drawText(&painter, 6, y + 15,
                                  (*item)->text
                                      ? XString_toUtf8((*item)->text) : "",
                                  windowText);
            }
            y += XTOOLBOX_HEADER_H;
            XRect_init(&line, 0, y - 1, w, 1);
            XPainter_fillRect(&painter, &line, windowText);
            if (selected)
                y += pageH;
        }
    }
    XPainter_deinit(&painter);
}

static void VX_toolBox_resizeEvent(XWidget* self, XEvent* event);

/** @brief 页头点击：y 坐标 → 条目索引并切换（对标 QToolBoxButton
 *         clicked → _q_buttonClicked → setCurrentIndex 链路）。y 映射
 *         与 paint/layout 同口径：激活页之前的页头每 22px 一个，激活
 *         页之下预留整块页高，其后页头继续每 22px 一个。
 *  @details 仅页头行才切页：激活页头正下方至页底是其内容区，命中即
 *           忽略（不换算索引）——内容区点击归内容控件，被页控件忽略
 *           后上抛到此的按下不得再被整除误判成页头（否则 y≈42 落在
 *           激活页一内容区会按 42/22=1 直接跳页二；Qt 中页内容点击
 *           绝不切页）。 */
static void VX_toolBox_mousePressEvent(XWidget* self, XEvent* event)
{
    XToolBox* box = (XToolBox*)self;
    XMouseEvent* me;
    XPoint pos;
    int index;
    if (!box || !event ||
        XEvent_type(event) != XEVENT_TYPE_MOUSE_BUTTON_PRESS)
        return;
    me = (XMouseEvent*)event;
    pos = XMouseEvent_position(me);
    /* 页头命中映射与 paint/layout/悬停提示同源（xtb2_headerIndexAt）：
     * 激活页内容区与越界区均 -1 ⇒ 忽略（内容区点击归内容控件，几何
     * 与 xtb2_layout 的页面定位同口径）。 */
    index = xtb2_headerIndexAt(box, pos.y);
    if (index < 0) {
        XEvent_ignore(event);
        return;
    }
    /* 对标 Qt：禁用条目的页头按钮 setEnabled(false)（setItemEnabled
     * 链），点击不发射 clicked 也不切页；程序化 setCurrentIndex 不受此
     * 限（ setCurrentIndex 无 enabled 拦截）。 */
    if (!XToolBox_isItemEnabled(box, index)) {
        XEvent_ignore(event);
        return;
    }
    XToolBox_setCurrentIndex(box, index);
    XEvent_accept(event);
}

/** @brief 悬停移动：页头命中变化即弹/收条目提示（P0-36；与页头点击
 *  同一映射源 xtb2_headerIndexAt，提示不会挂错条目）。 */
static void VX_toolBox_mouseMoveEvent(XWidget* self, XEvent* event)
{
    XToolBox* box = (XToolBox*)self;
    XMouseEvent* me;
    if (!box || !event ||
        XEvent_type(event) != XEVENT_TYPE_MOUSE_MOVE)
        return;
    me = (XMouseEvent*)event;
    xtb2_hoverUpdate(box, xtb2_headerIndexAt(box,
                                             XMouseEvent_position(me).y));
    XEvent_accept(event);
}

/** @brief 指针离开工具箱：收条目提示（P0-36；指针移入页头之下的页
 *  容器子控件时悬停合成器对本控件发 LEAVE，提示随收）。 */
static void VX_toolBox_leaveEvent(XWidget* self, XEvent* event)
{
    XToolBox* box = (XToolBox*)self;
    if (box) xtb2_hoverReset(box);
    if (event) XEvent_accept(event);
}

static void VX_toolBox_resizeEvent(XWidget* self, XEvent* event)
{
    (void)event;
    xtb2_layout((XToolBox*)self);
}

/* ==================== 生命周期与虚表 ==================== */

static void VX_toolBox_deinit(XToolBox* self)
{
    int64_t i;
    int64_t n;
    if (!self) return;
    xtb2_hoverReset(self); /* [P0-36] 析构收悬停提示（锚区借用本控件）。 */
    if (self->m_items) {
        n = XVector_size_base((const XContainer*)self->m_items);
        for (i = 0; i < n; ++i) {
            XToolBoxItem** item =
                (XToolBoxItem**)XVector_at_base(self->m_items, i);
            xtb2_itemDestroy(item ? *item : NULL);
        }
        XClassDelete(self->m_items);
        self->m_items = NULL;
    }
    XClass_Deinit_Parent(XFrame, (XFrame*)self);
}

XVtable* XToolBox_class_init(void)
{
    XVTABLE_INIT_DEFAULT(XToolBox)
    XVTABLE_INHERIT_XCLASS(XFrame);
    XVTABLE_OVERLOAD_DEFAULT(EXWidget_PaintEvent, VX_toolBox_paintEvent);
    XVTABLE_OVERLOAD_DEFAULT(EXWidget_ResizeEvent, VX_toolBox_resizeEvent);
    XVTABLE_OVERLOAD_DEFAULT(EXWidget_MousePressEvent, VX_toolBox_mousePressEvent);
    /* [P0-36] 条目提示悬停弹出 + 离开收起。 */
    XVTABLE_OVERLOAD_DEFAULT(EXWidget_MouseMoveEvent, VX_toolBox_mouseMoveEvent);
    XVTABLE_OVERLOAD_DEFAULT(EXWidget_LeaveEvent, VX_toolBox_leaveEvent);
    /* [Qt 对齐 2026-11-11] 页增删保护虚槽缺省实现（空操作）。 */
    XVTABLE_OVERLOAD_DEFAULT(EXToolBox_ItemInserted,
                             XToolBox_itemInserted_base);
    XVTABLE_OVERLOAD_DEFAULT(EXToolBox_ItemRemoved,
                             XToolBox_itemRemoved_base);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Deinit, VX_toolBox_deinit);
    return XVTABLE_DEFAULT;
}

void XToolBox_init(XToolBox* self, XWidget* parent, XWidgetFlags flags)
{
    XSize hint;
    if (!self) return;
    XMemset(self, 0, sizeof(*self));
    XFrame_init(&self->m_base, parent, flags);
    XClassSetVtable(self, XToolBox);
    Set_Class_Memory(self, XCLASS_DEFAULT_MEMORY_TYPE);
    Set_Class_IsHeap(self, false);
    self->m_items = XVector_Create(XToolBoxItem*);
    self->m_currentIndex = -1;
    self->m_hoverIndex = -1; /* [P0-36] 悬停页头索引（-1 无）。 */
    XWidget_resize(self, 160, 160);
    hint.width = 160;
    hint.height = 160;
    XWidget_setSizeHint((XWidget*)self, &hint);
    /* [minimumSizeHint 补齐 2026-10-27 框架对齐] Qt 同类控件的
     * minimumSizeHint 不低于其 sizeHint（对标 Qt 6.8.3 各控件
     * minimumSizeHint 实现）：补齐后布局竖排收缩有真实下界，
     * 此前 min=0 控件可被压塌至不可见。 */
    XWidget_setMinimumSizeHint((XWidget*)self, &hint);
}

XToolBox* XToolBox_create_ex(XMemoryType memory, XWidget* parent,
                             XWidgetFlags flags)
{
    XToolBox* self = (XToolBox*)XMemory_malloc(sizeof(*self), memory);
    if (!self) return NULL;
    XToolBox_init(self, parent, flags);
    Set_Class_Memory(self, memory);
    Set_Class_IsHeap(self, true);
    return self;
}

/* ==================== 页面管理 ==================== */

int XToolBox_addItem(XToolBox* self, XWidget* widget, const char* utf8Text)
{
    int64_t n;
    if (!self || !widget || !self->m_items) return -1;
    n = XVector_size_base((const XContainer*)self->m_items);
    return XToolBox_insertItem(self, (int)n, widget, utf8Text);
}

int XToolBox_insertItem(XToolBox* self, int index, XWidget* widget,
                        const char* utf8Text)
{
    XToolBoxItem* item;
    int64_t n;
    int actual;
    if (!self || !widget || !self->m_items) return -1;
    item = xtb2_itemCreate(widget, utf8Text);
    if (!item) return -1;
    n = XVector_size_base((const XContainer*)self->m_items);
    if (index < 0 || (int64_t)index > n) index = (int)n;
    /* [顺序根修 2026-11-05] 先在**插入前**记住旧当前页控件——插入后
     * m_currentIndex 指向的槽已错位（旧 idx 现落在新插入项上），
     * currentWidget 会取到错误控件、indexOf 跟随成错值（apitest
     * 前插段实锚 cur=1≠2）。XVector_at_base 返回槽位地址，元素为
     * 指针须解引用。 */
    {
        XWidget* oldCurrent = NULL;
        if (self->m_currentIndex >= 0 && self->m_currentIndex < (int)n) {
            XToolBoxItem** slot = (XToolBoxItem**)XVector_at_base(
                (const XContainer*)self->m_items, self->m_currentIndex);
            XToolBoxItem* it = slot ? *slot : NULL;
            oldCurrent = it ? it->widget : NULL;
        }
        XVector_insert_1_base(self->m_items, index, &item, 1);
        /* 页装进本条目专属的 XScrollArea（对标 qtoolbox.cpp:322-336：
         * 每页一只 QScrollArea；页的 parentWidget 因而是滚动区 viewport）。 */
        item->area = xtb2_createArea(self, widget);
        actual = index;
        if (self->m_currentIndex < 0)
            XToolBox_setCurrentIndex(self, actual);
        else {
            XWidget* host = xtb2_itemHost(item);
            if (host) XWidget_setVisible(host, false);
            xtb2_layout(self);
            /* 对标 Qt qtoolbox.cpp:337-344 insertItem：插入位于当前页之前
             * （index <= 当前控件的新索引）时，按同一控件的新索引重选一次
             * 并发射 currentChanged；否则 m_currentIndex 数值不变但对应
             * 另一条目，currentWidget/currentIndex 与可见页脱节。 */
            {
                int newIndex = oldCurrent ? XToolBox_indexOf(self, oldCurrent) : -1;
                if (oldCurrent && newIndex >= 0 && index <= newIndex) {
                    self->m_currentIndex = -1; /* 触发 change（同 Qt 置空 currentPage）。 */
                    XToolBox_setCurrentIndex(self, newIndex);
                }
            }
        }
    }
    /* 对标 QToolBox::insertItem 末尾 itemInserted(index)（qtoolbox.cpp:353）。 */
    XToolBox_itemInserted(self, actual);
    return actual;
}

void XToolBox_removeItem(XToolBox* self, int index)
{
    XToolBoxItem* item;
    XWidget* widget;
    int wasCurrent;
    int64_t n;
    if (!self || !self->m_items || index < 0 ||
        index >= (int)XVector_size_base((const XContainer*)self->m_items))
        return;
    item = *(XToolBoxItem**)XVector_at_base(self->m_items, index);
    widget = item ? item->widget : NULL;
    wasCurrent = (index == self->m_currentIndex);
    /* 对标 QToolBox::removeItem（qtoolbox.cpp:453-462 → _q_widgetDestroyed
     * qtoolbox.cpp:423-449）：
     *   ① 「The widget is *not* deleted」且 `w->setParent(this)`——控件
     *      仍归本工具箱（随容器析构级联释放），不摘父链给调用方；
     *   ② 页面出表并隐藏（Qt 靠页容器 deleteLater 收起显示）；
     *   ③ 页表清空 ⇒ 发 currentChanged(-1)；
     *      移出的正是当前页 ⇒ 新当前页固定落在索引 0 并随之发
     *      currentChanged(0)（Qt `q->setCurrentIndex(0)`，不是原位置）。
     * 此前实现摘父链归还调用方、移除当前页后落到「同位置或末页」、
     * 清空时不发 currentChanged——三处都与 Qt 不一致。 */
    if (widget) {
        XWidget_setVisible(widget, false);
        /* 先把页从页容器里取回（XScrollArea_takeWidget：控件不销毁、父置
         * NULL 并隐藏），再回收页容器——否则容器析构会连带销毁页
         * （对标 Qt：QScrollArea 删除时其 widget 也随父链销毁，故 Qt 的
         * _q_widgetDestroyed 先 setParent(this) 把页摘出来）。 */
        if (item->area) {
#if XSCROLLAREA_ON && XABSTRACTSCROLLAREA_ON
            XScrollArea_takeWidget((XScrollArea*)item->area);
#endif
            /* 页容器延迟回收：removeItem 可能处在页容器自身的事件/信号
             * 帧内（如页头点击 → setCurrentIndex → 业务 removeItem），
             * 同步析构会在该帧返回途中释放自身（对标 Qt 的 deleteLater）。 */
            XObject_deleteLater((XObject*)item->area);
            item->area = NULL;
        }
        /* 严格对齐 Qt 的父对象：摘出条目但仍以本工具箱为父（Qt 显式
           setParent(this)）；控件不再参与布局（不在页表内）。 */
        if (XWidget_parentWidget(widget) != (XWidget*)self)
            XWidget_setParent(widget, (XWidget*)self, 0);
        XWidget_setVisible(widget, false);
    }
    XVector_remove_base(self->m_items, index, 1);
    xtb2_itemDestroy(item);
    xtb2_hoverReset(self); /* [P0-36] 条目序已变，悬停索引失效收提示。 */
    n = XVector_size_base((const XContainer*)self->m_items);
    if (n <= 0) {
        self->m_currentIndex = -1;
        xtb2_emitChanged(self, -1);
    } else if (wasCurrent) {
        self->m_currentIndex = -1; /* 置空当前页，使 setCurrentIndex(0) 真迁移。 */
        XToolBox_setCurrentIndex(self, 0);
    } else if (index < self->m_currentIndex) {
        self->m_currentIndex--;
    }
    /* 对标 QToolBox::removeItem 末尾 itemRemoved(index)（qtoolbox.cpp:461）。 */
    XToolBox_itemRemoved(self, index);
}

int XToolBox_count(const XToolBox* self)
{
    return (self && self->m_items)
               ? (int)XVector_size_base(
                     (const XContainer*)self->m_items)
               : 0;
}

XWidget* XToolBox_widget(const XToolBox* self, int index)
{
    XToolBoxItem** item;
    if (!self || !self->m_items || index < 0 ||
        index >= (int)XVector_size_base((const XContainer*)self->m_items))
        return NULL;
    item = (XToolBoxItem**)XVector_at_base(self->m_items, index);
    return item && *item ? (*item)->widget : NULL;
}

int XToolBox_indexOf(const XToolBox* self, const XWidget* widget)
{
    int64_t i;
    int64_t n;
    if (!self || !self->m_items || !widget) return -1;
    n = XVector_size_base((const XContainer*)self->m_items);
    for (i = 0; i < n; ++i) {
        XToolBoxItem** item =
            (XToolBoxItem**)XVector_at_base(self->m_items, i);
        if (item && *item && (*item)->widget == widget) return (int)i;
    }
    return -1;
}

/* ==================== 条目属性 ==================== */

void XToolBox_setItemText(XToolBox* self, int index, const char* utf8)
{
    XToolBoxItem** item;
    if (!self || !self->m_items || index < 0 ||
        index >= (int)XVector_size_base((const XContainer*)self->m_items))
        return;
    item = (XToolBoxItem**)XVector_at_base(self->m_items, index);
    if (item && *item) {
        if (!(*item)->text)
            (*item)->text = XString_create();
        if ((*item)->text)
            XString_assign_utf8((*item)->text, utf8 ? utf8 : "");
        /* [Qt 对齐 2026-11-02] Qt 的 setItemText 会 updateGeometry 并按新
           文本重算 sizeHint；此前只 update() → 标题变长后布局仍按旧宽。 */
        XWidget_updateGeometry((XWidget*)self);
        XWidget_update((XWidget*)self);
    }
}

const char* XToolBox_itemText(const XToolBox* self, int index)
{
    XToolBoxItem** item;
    if (!self || !self->m_items || index < 0 ||
        index >= (int)XVector_size_base((const XContainer*)self->m_items))
        return "";
    item = (XToolBoxItem**)XVector_at_base(self->m_items, index);
    if (!item || !*item || !(*item)->text) return "";
    return XString_toUtf8((*item)->text);
}

/* ==================== Task 2.10：图标 API ==================== */

void XToolBox_setItemIcon(XToolBox* self, int index, const XString* path)
{
    XToolBoxItem** item;
    if (!self || !self->m_items || index < 0 ||
        index >= (int)XVector_size_base((const XContainer*)self->m_items))
        return;
    item = (XToolBoxItem**)XVector_at_base(self->m_items, index);
    if (!item || !*item) return;
    if (!path) {
        if ((*item)->icon) {
            XClassDelete((*item)->icon);
            (*item)->icon = NULL;
        }
    } else {
        XString* copy = XString_create_copy(path);
        if (!copy) return;
        if ((*item)->icon)
            XClassDelete((*item)->icon);
        (*item)->icon = copy;
    }
    XWidget_update((XWidget*)self);
}

void XToolBox_setItemIcon_2(XToolBox* self, int index, const char* utf8)
{
    XString* tmp;
    if (!utf8) {
        XToolBox_setItemIcon(self, index, NULL);
        return;
    }
    tmp = XString_create_utf8(utf8);
    if (!tmp) return;
    XToolBox_setItemIcon(self, index, tmp);
    XClassDelete(tmp);
}

const XString* XToolBox_itemIcon(const XToolBox* self, int index)
{
    XToolBoxItem** item;
    if (!self || !self->m_items || index < 0 ||
        index >= (int)XVector_size_base((const XContainer*)self->m_items))
        return NULL;
    item = (XToolBoxItem**)XVector_at_base(self->m_items, index);
    return (item && *item) ? (*item)->icon : NULL;
}

void XToolBox_setItemToolTip(XToolBox* self, int index, const XString* tip)
{
    XToolBoxItem** item;
    XString* copy;
    if (!self || !self->m_items || index < 0 ||
        index >= (int)XVector_size_base((const XContainer*)self->m_items))
        return;
    item = (XToolBoxItem**)XVector_at_base(self->m_items, index);
    if (!item || !*item) return;
    if (!tip) {
        if ((*item)->tooltip) {
            XClassDelete((*item)->tooltip);
            (*item)->tooltip = NULL;
        }
    } else {
        copy = XString_create_copy(tip);
        if (!copy) return;
        if ((*item)->tooltip)
            XClassDelete((*item)->tooltip);
        (*item)->tooltip = copy;
    }
}

void XToolBox_setItemToolTip_2(XToolBox* self, int index, const char* utf8)
{
    XString tmp;
    if (!self || !utf8) {
        XToolBox_setItemToolTip(self, index, NULL);
        return;
    }
    XString_init(&tmp);
    XString_assign_utf8(&tmp, utf8);
    XToolBox_setItemToolTip(self, index, &tmp);
    XClassDeinit(&tmp);
}

const XString* XToolBox_itemToolTip(const XToolBox* self, int index)
{
    XToolBoxItem** item;
    if (!self || !self->m_items || index < 0 ||
        index >= (int)XVector_size_base((const XContainer*)self->m_items))
        return NULL;
    item = (XToolBoxItem**)XVector_at_base(self->m_items, index);
    return (item && *item) ? (*item)->tooltip : NULL;
}

void XToolBox_setItemEnabled(XToolBox* self, int index, bool enabled)
{
    XToolBoxItem** item;
    if (!self || !self->m_items || index < 0 ||
        index >= (int)XVector_size_base((const XContainer*)self->m_items))
        return;
    item = (XToolBoxItem**)XVector_at_base(self->m_items, index);
    if (item && *item) {
        (*item)->enabled = enabled;
        XWidget_update((XWidget*)self);
        /* 对标 Qt qtoolbox.cpp:544-571 setItemEnabled：禁用当前页时向下
         * /向上找最近的启用页并切换（伴随 currentChanged）；找不到启用
         * 页时维持现状（Qt 的 setCurrentIndex(原索引) 被同页早退吸收）。
         */
        if (!enabled && index == self->m_currentIndex) {
            int up = index;
            int down = index;
            int count = XToolBox_count(self);
            int target = -1;
            while (up > 0 || down < count - 1) {
                if (down < count - 1) {
                    ++down;
                    if (XToolBox_isItemEnabled(self, down)) {
                        target = down;
                        break;
                    }
                }
                if (up > 0) {
                    --up;
                    if (XToolBox_isItemEnabled(self, up)) {
                        target = up;
                        break;
                    }
                }
            }
            if (target >= 0)
                XToolBox_setCurrentIndex(self, target);
        }
    }
}

bool XToolBox_isItemEnabled(const XToolBox* self, int index)
{
    XToolBoxItem** item;
    if (!self || !self->m_items || index < 0 ||
        index >= (int)XVector_size_base((const XContainer*)self->m_items))
        return false;
    item = (XToolBoxItem**)XVector_at_base(self->m_items, index);
    return (item && *item) ? (*item)->enabled : false;
}

/* ==================== 当前页槽 ==================== */

int XToolBox_currentIndex(const XToolBox* self)
{
    return self ? self->m_currentIndex : -1;
}

XWidget* XToolBox_currentWidget(const XToolBox* self)
{
    int index = xtb2_currentIndexOf(self);
    XToolBoxItem** item;
    if (index < 0 || !self || !self->m_items) return NULL;
    item = (XToolBoxItem**)XVector_at_base(self->m_items, index);
    return (item && *item) ? (*item)->widget : NULL;
}

void XToolBox_setCurrentIndex(XToolBox* self, int index)
{
    XToolBoxItem** item;
    int old;
    if (!self || !self->m_items || index < 0 ||
        index >= (int)XVector_size_base((const XContainer*)self->m_items))
        return;
    if (index == self->m_currentIndex) return;
    item = (XToolBoxItem**)XVector_at_base(self->m_items, index);
    /* 对标 Qt qtoolbox.cpp:387-403 setCurrentIndex：只拦截越界与同页，
       不检查条目 enabled（程序化切换允许落在禁用条目上）；点击路径对
       禁用条目的拦截在 mousePressEvent（对标 Qt 页头按钮
       setEnabled(false) 后 clicked 不发射）。 */
    if (!item || !*item) return;
    old = self->m_currentIndex;
    self->m_currentIndex = index;
    if (old >= 0) {
        XWidget* prevHost = xtb2_hostAt(self, old);
        if (prevHost)
            XWidget_setVisible(prevHost, false);
    }
    {
        XWidget* curHost = xtb2_hostAt(self, index);
        if (curHost)
            XWidget_setVisible(curHost, true);
    }
    xtb2_layout(self);
    /* 页头带由本控件自绘且纵向位置随 currentIndex 平移：切换后整箱
     * 标脏重绘，抹掉旧位置页头残影（rescan_r2 #55：切换后旧「页二」
     * 头像素带残留——页几何联动只标脏页矩形，覆盖不到自绘页头带）。 */
    XWidget_update((XWidget*)self);
    xtb2_emitChanged(self, index);
}

void XToolBox_setCurrentWidget(XToolBox* self, XWidget* widget)
{
    int index;
    if (!self || !widget) return;
    index = XToolBox_indexOf(self, widget);
    if (index >= 0)
        XToolBox_setCurrentIndex(self, index);
}

/* ==================== 信号 ==================== */

void* XToolBox_currentChanged_signal(XToolBox* self, int index)
{
    (void)self;
    (void)index;
    return (void*)(size_t)XToolBox_currentChanged_signal;
}







#endif /* XWIDGET_ON && XFRAME_ON && XTOOLBOX_ON */

#endif /* XGUI_ON */
