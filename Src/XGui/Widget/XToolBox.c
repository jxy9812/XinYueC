/**
 * @file       XToolBox.c
 * @brief      工具箱控件实现（对标 Qt 6.8 QToolBox 全部公共 API）。
 * @details    与同名头文件的公共 API 一一对应；内部实现细节见
 *             头文件 @note 与函数级 Doxygen 注释。
 * @author     XinYueC 团队
 */

#include "XToolBox.h"
#include "XStringUtils.h"
#include "XString.h"
#include "XStyle.h"
#include "XStyleOption.h"
#include "XMemory.h"
#include "XEvent.h"
#include "XVarList.h"
#include "XGuiConfig.h"

#include "XAlgorithm.h"
#include "XWidget_Protected.h"

#if XWIDGET_ON && XFRAME_ON && XTOOLBOX_ON

#define XTOOLBOX_HEADER_H 22  /**< 页头条高（paint/layout/mouse 共用）。 */

/* ==================== 内部条目 ==================== */

typedef struct XToolBoxItem
{
    XWidget* widget;   /**< 页面控件（借用，归调用方/容器）。 */
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
        XString_delete_base(item->text);
        item->text = NULL;
    }
    if (item->icon) {
        XString_delete_base(item->icon);
        item->icon = NULL;
    }
    if (item->tooltip) {
        XString_delete_base(item->tooltip);
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
        XSize hint = XWidget_sizeHint(current);
        ph = hint.height;
    }
    if (ph <= 0 || ph > contentH) return contentH;
    return ph;
}

/** @brief 布局：当前页控件置于激活页头之下（页高按 sizeHint 顶置，
 *           见 xtb2_pageHeight；其余页隐藏）。
 *  @details 对齐 QToolBox::setCurrentIndex 的显隐语义：非当前页一律
 *           setVisible(false)，防止外部 show() 绕过工具箱的页面管理
 *           （Qt 中页面装在隐藏的 ScrollArea 容器内无此问题）。 */
static void xtb2_layout(XToolBox* self)
{
    XWidget* current;
    XRect r;
    int w = XWidget_width((XWidget*)self);
    int64_t i;
    int64_t n;
    if (!self) return;
    n = self->m_items ? XVector_size_base((const XContainer*)self->m_items)
                      : 0;
    current = XToolBox_currentWidget(self);
    for (i = 0; i < n; ++i) {
        XToolBoxItem** item =
            (XToolBoxItem**)XVector_at_base(self->m_items, i);
        XWidget* page;
        if (!item || !*item) continue;
        page = (*item)->widget;
        if (!page) continue;
        if (page != current && XWidget_isVisible(page))
            XWidget_setVisible(page, false);
    }
    if (!current) return;
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
    XWidget_setGeometryRect(current, &r);
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

/* ==================== 事件处理 ==================== */

static void VX_toolBox_paintEvent(XWidget* self, XEvent* event)
{
    XToolBox* box = (XToolBox*)self;
    XPainter painter;
    XImage* image;
    XPoint offset;
    XRect head;
    XRect line;
    uint32_t highlight;
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
                           XPaletteColorRole_WindowText);
        windowText = XColor_rgba(&c);
        c = XPalette_color(&palette, XPaletteColorGroup_Current,
                           XPaletteColorRole_Mid);
        mid = XColor_rgba(&c);
    }
#else
    highlight = 0xFF3080C0u;
    windowText = 0xFF000000u;
    mid = 0xFF808080u;
#endif /* XPALETTE_ON */
    if (box->m_items) {
        for (i = 0; i < n; ++i) {
            XToolBoxItem** item =
                (XToolBoxItem**)XVector_at_base(box->m_items, i);
            if (!item || !*item) continue;
            XRect_init(&head, 0, y, w, XTOOLBOX_HEADER_H);
#if XSTYLE_ON
            if (XStyle_defaultStyle() != NULL) {
                XStyle* style = XStyle_defaultStyle();
                XStyleOption opt;
                XStyleOption_init(&opt, XStyleCE_ToolBoxTab);
                opt.m_rect = head;
                opt.m_state = XWidget_isEnabled((XWidget*)box)
                    ? XStyleState_Enabled : 0;
                if ((int)i == box->m_currentIndex)
                    opt.m_state |= XStyleState_Selected;
                opt.m_tabSelected = ((int)i == box->m_currentIndex);
                opt.m_text = (*item)->text
                    ? XString_toUtf8((*item)->text) : "";
#if XPALETTE_ON
                opt.m_palette = XWidget_palette((XWidget*)box);
#endif
                XStyle_drawControl(style, XStyleCE_ToolBoxTab, &opt,
                                   &painter, (XWidget*)box);
                y += XTOOLBOX_HEADER_H;
                XRect_init(&line, 0, y - 1, w, 1);
                XPainter_fillRect(&painter, &line, windowText);
                /* 激活页之后预留整块页高，后续页头排在页下方（与
                 * xtb2_layout 的页面定位同口径，对标 QToolBox VBox）。 */
                if ((int)i == box->m_currentIndex)
                    y += pageH;
                continue;
            }
#endif /* XSTYLE_ON */
            if ((int)i == box->m_currentIndex)
                XPainter_fillRect(&painter, &head, highlight);
            else
                XPainter_fillRect(&painter, &head, mid);
            XPainter_drawText(&painter, 6, y + 15,
                              (*item)->text
                                  ? XString_toUtf8((*item)->text) : "",
                              windowText);
            y += XTOOLBOX_HEADER_H;
            XRect_init(&line, 0, y - 1, w, 1);
            XPainter_fillRect(&painter, &line, windowText);
            if ((int)i == box->m_currentIndex)
                y += pageH;
        }
    }
    XPainter_deinit(&painter);
}

static void VX_toolBox_resizeEvent(XWidget* self, XEvent* event);

/** @brief 页头点击：y 坐标 → 条目索引并切换（对标 QToolBoxButton
 *         clicked → _q_buttonClicked → setCurrentIndex 链路）。y 映射
 *         与 paint/layout 同口径：激活页之前的页头每 22px 一个，激活
 *         页之下预留整块页高，其后页头继续每 22px 一个。 */
static void VX_toolBox_mousePressEvent(XWidget* self, XEvent* event)
{
    XToolBox* box = (XToolBox*)self;
    XMouseEvent* me;
    XPoint pos;
    int index;
    int pageH;
    int count;
    if (!box || !event ||
        XEvent_type(event) != XEVENT_TYPE_MOUSE_BUTTON_PRESS)
        return;
    me = (XMouseEvent*)event;
    pos = XMouseEvent_position(me);
    count = box->m_items
                ? (int)XVector_size_base((const XContainer*)box->m_items) : 0;
    if (count <= 0) {
        XEvent_ignore(event);
        return;
    }
    /* 页高与 xtb2_layout/paint 同口径（xtb2_pageHeight）。 */
    pageH = xtb2_pageHeight(box, count);
    if (box->m_currentIndex >= 0 && pageH > 0 &&
        pos.y >= (box->m_currentIndex + 1) * XTOOLBOX_HEADER_H + pageH) {
        /* 激活页之后的页头区。 */
        int below = pos.y - ((box->m_currentIndex + 1)
                             * XTOOLBOX_HEADER_H + pageH);
        index = box->m_currentIndex + 1 + below / XTOOLBOX_HEADER_H;
    } else {
        index = (int)(pos.y / XTOOLBOX_HEADER_H);
    }
    if (index < 0 || index >= count) {
        XEvent_ignore(event);
        return;
    }
    XToolBox_setCurrentIndex(box, index);
    XEvent_accept(event);
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
    if (self->m_items) {
        n = XVector_size_base((const XContainer*)self->m_items);
        for (i = 0; i < n; ++i) {
            XToolBoxItem** item =
                (XToolBoxItem**)XVector_at_base(self->m_items, i);
            xtb2_itemDestroy(item ? *item : NULL);
        }
        XVector_delete_base(self->m_items);
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
    XWidget_resize(self, 160, 160);
    hint.width = 160;
    hint.height = 160;
    XWidget_setSizeHint((XWidget*)self, &hint);
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
    XVector_insert_1_base(self->m_items, index, &item, 1);
    XWidget_setParent(widget, (XWidget*)self, 0);
    actual = index;
    if (self->m_currentIndex < 0)
        XToolBox_setCurrentIndex(self, actual);
    else {
        XWidget_setVisible(widget, false);
        xtb2_layout(self);
    }
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
    /* 复扫 R-74：移除当前页后旧页残留可见——此前仅出表，被移除页仍
     * 是本工具箱的可见子控件，叠印在新当前页上方成残影。Qt 契约为
     * 「widget 本身不删除」（QToolBox::removeItem 仅断开条目登记）；
     * 此处先隐藏并摘除父链再出表，控件归还调用方管理（后续 addItem
     * 会重新挂回）。隐藏须无条件执行：不能以 isVisible 门禁跳过——
     * 工具箱自身未显示时子页生效可见恒 false，门禁会漏清显式 show
     * 位（探针实测），残留位在后续 reparent/show 时旧页复现。 */
    if (widget) {
        XWidget_setVisible(widget, false);
        XWidget_setParent(widget, NULL, 0);
    }
    XVector_remove_base(self->m_items, index, 1);
    xtb2_itemDestroy(item);
    if (wasCurrent) {
        self->m_currentIndex = -1;
        n = XVector_size_base((const XContainer*)self->m_items);
        if (n > 0)
            XToolBox_setCurrentIndex(self,
                index < (int)n ? index : (int)n - 1);
    } else if (index < self->m_currentIndex) {
        self->m_currentIndex--;
    }
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
            XString_delete_base((*item)->icon);
            (*item)->icon = NULL;
        }
    } else {
        XString* copy = XString_create_copy(path);
        if (!copy) return;
        if ((*item)->icon)
            XString_delete_base((*item)->icon);
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
    XString_delete_base(tmp);
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
            XString_delete_base((*item)->tooltip);
            (*item)->tooltip = NULL;
        }
    } else {
        copy = XString_create_copy(tip);
        if (!copy) return;
        if ((*item)->tooltip)
            XString_delete_base((*item)->tooltip);
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
    XString_deinit_base(&tmp);
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
    if (!item || !*item || !(*item)->enabled) return;
    old = self->m_currentIndex;
    self->m_currentIndex = index;
    if (old >= 0) {
        XWidget* prev = XToolBox_widget(self, old);
        if (prev)
            XWidget_setVisible(prev, false);
    }
    {
        XWidget* cur = XToolBox_widget(self, index);
        if (cur)
            XWidget_setVisible(cur, true);
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
