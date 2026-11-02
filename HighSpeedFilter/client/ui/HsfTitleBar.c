/**
 * @file       HsfTitleBar.c
 * @brief      顶部标题栏实现：蓝底 + ☰ 图标 + 居中标题 + 右侧时钟/温湿度。
 * @details    标题栏本体是**容器**（XWidget 子类）：右侧时钟/温湿度是
 *             它的子标签（手动定位，右侧块整体右对齐）；标题标签占
 *             「☰ 区 ~ 右侧块」之间的剩余空间并居中——保证右侧块宽度
 *             变化时标题仍视觉居中。
 */
#include "HsfTitleBar.h"

#include "HsfTheme.h"
#include "XDateTime.h"
#include "XObject.h"
#include "XPixmap.h"
#include "XPainter.h"
#include "XWidget_Protected.h"
#include <stdio.h>
#include <string.h>

#if XWIDGET_ON && XFRAME_ON && XLABEL_ON

/* ---- 几何/格式常量 ---- */
#define HSF_TITLE_ICON_W   48  /**< ☰ 触控区宽（Material 48dp）。 */
#define HSF_TITLE_GAP      10  /**< 右侧块内元素间距。 */
#define HSF_TITLE_PAD       0  /**< 右端留白（0=温湿度列贴窗口右缘）。 */
#define HSF_TITLE_WIDE_MIN 700 /**< 宽栏阈值：显示「日期+时间」格式。 */
#define HSF_TITLE_TEMP_W   62  /**< 温度标签定宽（℃ 值+余量）。 */
#define HSF_TITLE_HUMID_W  60  /**< 湿度标签定宽（% 值+余量）。 */
#define HSF_TITLE_TIME_W    78  /**< 日期/时间标签定宽（YYYY-MM-DD）。 */

/** @brief 格式化当前日期（YYYY-MM-DD）。 */
static void hsfTitleFormatDate(char* buf, size_t size)
{
    XDateTime now = XDateTime_currentDateTime();
    snprintf(buf, size, "%04d-%02d-%02d",
             XDate_year(&now.m_date), XDate_month(&now.m_date),
             XDate_day(&now.m_date));
}

/** @brief 格式化当前时间（HH:MM:SS）。 */
static void hsfTitleFormatTime(char* buf, size_t size)
{
    XDateTime now = XDateTime_currentDateTime();
    snprintf(buf, size, "%02d:%02d:%02d",
             XTime_hour(&now.m_time), XTime_minute(&now.m_time),
             XTime_second(&now.m_time));
}

/** @brief 右对齐小标签（时钟/温湿度）：白字、鼠标穿透。 */
static XLabel* hsfTitleMakeSideLabel(XWidget* bar, int pixelSize)
{
    XLabel* label = XLabel_create(bar, 0);
    XFont font;
    if (!label) return NULL;
    font = XWidget_font(bar);
    XWidget_setFont((XWidget*)label, &font);
    XClassDeinit(&font);
    XLabel_setTextPixelSize(label, pixelSize);
    XLabel_setAlignment(label, XAlignment_Right | XAlignment_VCenter);
    XWidget_setForegroundRole((XWidget*)label,
                              XPaletteColorRole_HighlightedText);
    XWidget_setAttribute((XWidget*)label,
                         XWidgetAttribute_TransparentForMouseEvents, true);
    return label;
}

/** @brief 刷新右侧三个标签的文本（时钟/温度/湿度）。 */
static void hsfTitleRefreshSideTexts(HsfTitleBar* self)
{
    char buf[64];
    if (!self) return;
    hsfTitleFormatDate(buf, sizeof(buf));
    if (self->m_date)
        XLabel_setText_2(self->m_date, buf);
    hsfTitleFormatTime(buf, sizeof(buf));
    if (self->m_clock)
        XLabel_setText_2(self->m_clock, buf);
    snprintf(buf, sizeof(buf), "%.1f\xE2\x84\x83", self->m_temperature);
    if (self->m_temp)
        XLabel_setText_2(self->m_temp, buf);
    snprintf(buf, sizeof(buf), "%.1f%%", self->m_humidity);
    if (self->m_humid)
        XLabel_setText_2(self->m_humid, buf);
}

/** @brief 重排：右侧块右对齐，标题占剩余空间居中。
 *  @details 布局在标题栏尺寸变化后需重排（右侧块宽度随文本变化）。 */
static void hsfTitleRelayout(HsfTitleBar* self)
{
    int barW;
    int barH;
    int gap = HSF_TITLE_GAP;
    int x;
    int clockW = 0;
    int tempW = 0;
    int humidW = 0;
    if (!self) return;
    barW = XWidget_width(&self->m_base);
    barH = XWidget_height(&self->m_base);
    if (barW < 1 || barH < 1) return;
    hsfTitleRefreshSideTexts(self);

    /* 标签宽度直接取 sizeHint（框架已按 Qt 语义经 vtable 分派到
       XLabel 的**实时文本计算**，见 XWidget SizeHint 虚槽）。 */
    if (self->m_date) {
        clockW = XWidget_sizeHint((XWidget*)self->m_date).width;
    }
    if (self->m_clock) {
        int tw = XWidget_sizeHint((XWidget*)self->m_clock).width;
        if (tw > clockW) clockW = tw;
    }
    if (clockW < 40) clockW = 40;
    if (self->m_date)
        XWidget_setFixedWidth((XWidget*)self->m_date, clockW);
    if (self->m_clock)
        XWidget_setFixedWidth((XWidget*)self->m_clock, clockW);
    /* 温湿度：同样走 XWidget_sizeHint（虚分派到 XLabel 实时文本宽），
       列宽取较宽者对齐。 */
    {
        int colW = 0;
        if (self->m_temp) {
            tempW = XWidget_sizeHint((XWidget*)self->m_temp).width;
            colW = tempW;
        }
        if (self->m_humid) {
            humidW = XWidget_sizeHint((XWidget*)self->m_humid).width;
            if (humidW > colW) colW = humidW;
        }
        if (colW < 30) colW = 30;
        tempW = humidW = colW;
        if (self->m_temp)
            XWidget_setFixedWidth((XWidget*)self->m_temp, colW);
        if (self->m_humid)
            XWidget_setFixedWidth((XWidget*)self->m_humid, colW);
    }

    /* 从右往左排两竖列：
     *   右列 = 温度(上)/湿度(下)；其左列 = 日期(上)/时间(下)。 */
    {
        int colW = (tempW > humidW) ? tempW : humidW;
        int colX = barW - HSF_TITLE_PAD - colW;
        int halfH = barH / 2;
        /* 温湿度上下两行（竖列）：上=温度，下=湿度。 */
        if (self->m_temp)
            XWidget_setGeometry((XWidget*)self->m_temp, colX, 0,
                                colW, halfH);
        if (self->m_humid)
            XWidget_setGeometry((XWidget*)self->m_humid, colX, halfH,
                                colW, barH - halfH);
        /* 日期/时间竖列：上=日期，下=时间。 */
        x = colX - gap - clockW;
        if (self->m_date)
            XWidget_setGeometry((XWidget*)self->m_date, x, 0, clockW, halfH);
        if (self->m_clock)
            XWidget_setGeometry((XWidget*)self->m_clock, x, halfH,
                                clockW, barH - halfH);
    }

    /* 标题：☰ 区右侧 ~ 右侧块左侧，居中（右侧块宽度变化不影响居中）。 */
    {
        int left = HSF_TITLE_ICON_W;
        int right = (x > left + 40) ? (x - gap) : (left + 40);
        if (self->m_title)
            XWidget_setGeometry((XWidget*)self->m_title, left, 0,
                                right - left, barH);
    }
}

/** @brief 绘制事件：铺深蓝底 + 左端 ☰ 三横条（图标自绘，不依赖字库字形）。 */
static void VHsfTitleBar_paintEvent(XWidget* self, XEvent* event)
{
    {
        HsfTitleBar* bar = (HsfTitleBar*)self;
        XImage* image;
        XPoint offset;
        XPainter painter;
        XRect rect;
        if (!self || !event ||
            XEvent_type(event) != XEVENT_TYPE_PAINT) return;
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
        XPainter_fillRect(&painter, &rect, HSF_COLOR_BRAND);
        /* ☰：左端触控区画三横条（字库无 U+2630 字形，自绘）。 */
        {
            int cx = 14;
            int cy = XWidget_height(self) / 2;
            int i;
            for (i = 0; i < 3; ++i) {
                XRect line;
                line.x = cx; line.y = cy - 9 + i * 8;
                line.width = 20; line.height = 2;
                XPainter_fillRect(&painter, &line, 0xffffffffu);
            }
        }
        XPainter_end(&painter);
        XPainter_deinit(&painter);
        (void)bar;
    }
}

/** @brief 尺寸变化：重排右侧块与居中标题。 */
static void VHsfTitleBar_resizeEvent(XWidget* self, XEvent* event)
{
    XClass_Parent(XWidget, EXWidget_ResizeEvent,
                  void (*)(XWidget*, XEvent*))(self, event);
    hsfTitleRelayout((HsfTitleBar*)self);
}

/** @brief 鼠标按下：命中 ☰ 触控区（左侧 48dp）→ 调归属窗回调。 */
static void VHsfTitleBar_mousePressEvent(XWidget* self, XEvent* event)
{
    HsfTitleBar* bar = (HsfTitleBar*)self;
    if (!self || !event) return;
    if (XEvent_type(event) == XEVENT_TYPE_MOUSE_BUTTON_PRESS &&
        bar && bar->m_menuFn) {
        XMouseEvent* mouse = (XMouseEvent*)event;
        XPoint pos = XMouseEvent_position(mouse);
        if (pos.x < HSF_TITLE_ICON_W) {
            bar->m_menuFn(bar->m_menuUserData);
            XEvent_accept(event);
            return;
        }
    }
    XClass_Parent(XWidget, EXWidget_MousePressEvent,
                  void (*)(XWidget*, XEvent*))(self, event);
}

/** @brief 标题栏类虚表初始化。 */
XVtable* HsfTitleBar_class_init(void)
{
    XVTABLE_INIT_DEFAULT(HsfTitleBar)
    XVTABLE_INHERIT_XCLASS(XWidget);
    XVTABLE_OVERLOAD_DEFAULT(EXWidget_PaintEvent, VHsfTitleBar_paintEvent);
    XVTABLE_OVERLOAD_DEFAULT(EXWidget_ResizeEvent, VHsfTitleBar_resizeEvent);
    XVTABLE_OVERLOAD_DEFAULT(EXWidget_MousePressEvent,
                             VHsfTitleBar_mousePressEvent);
    return XVTABLE_DEFAULT;
}

/** @brief 初始化标题栏。 */
void HsfTitleBar_init(HsfTitleBar* self, XWidget* parent)
{
    if (!self) return;
    memset(self, 0, sizeof(HsfTitleBar));
    XWidget_init(&self->m_base, parent, 0);
    XClassSetVtable(self, HsfTitleBar);
    self->m_temperature = 0.0; /* 默认值 0 */
    self->m_humidity = 0.0;    /* 默认值 0 */

    HsfTheme_applyDefaultFont(&self->m_base);
    {
        XFont f = XWidget_font((XWidget*)&self->m_base);
        XFont_setBold(&f, true);
        XWidget_setFont((XWidget*)&self->m_base, &f);
        XClassDeinit(&f);
    }
    XWidget_setFixedHeight((XWidget*)&self->m_base, HSF_TITLE_BAR_H);

    /* 居中标题标签。 */
    self->m_title = XLabel_create((XWidget*)&self->m_base, 0);
    if (self->m_title) {
        XFont f = XWidget_font((XWidget*)&self->m_base);
        XWidget_setFont((XWidget*)self->m_title, &f);
        XClassDeinit(&f);
        XLabel_setTextPixelSize(self->m_title, 16);
        XLabel_setAlignment(self->m_title,
                            XAlignment_HCenter | XAlignment_VCenter);
        XWidget_setForegroundRole((XWidget*)self->m_title,
                                  XPaletteColorRole_HighlightedText);
        XWidget_setAttribute((XWidget*)self->m_title,
                             XWidgetAttribute_TransparentForMouseEvents,
                             true);
        XWidget_show((XWidget*)self->m_title);
    }

    /* 右侧：时钟 / 温度 / 湿度。 */
    /* 日期行 + 时间行（上下两行，右侧竖列）。 */
    self->m_date = hsfTitleMakeSideLabel((XWidget*)&self->m_base, 12);
    self->m_clock = hsfTitleMakeSideLabel((XWidget*)&self->m_base, 12);
    /* 温湿度竖列两行 → 同为 12px（行高 20px）。
     * 竖列**左对齐**：右对齐时「0.0℃」与「0.0%」尾字宽不同 → 数字
     * 参差；左对齐后两行数字起点一致、纵向对齐成列。 */
    self->m_temp = hsfTitleMakeSideLabel((XWidget*)&self->m_base, 12);
    self->m_humid = hsfTitleMakeSideLabel((XWidget*)&self->m_base, 12);
    if (self->m_temp)
        XLabel_setAlignment(self->m_temp, XAlignment_Left | XAlignment_VCenter);
    if (self->m_humid)
        XLabel_setAlignment(self->m_humid, XAlignment_Left | XAlignment_VCenter);
    if (self->m_date) XWidget_show((XWidget*)self->m_date);
    if (self->m_clock) XWidget_show((XWidget*)self->m_clock);
    if (self->m_temp) XWidget_show((XWidget*)self->m_temp);
    if (self->m_humid) XWidget_show((XWidget*)self->m_humid);

    XWidget_show((XWidget*)&self->m_base);
    hsfTitleRelayout(self);
}

/** @brief 刷新时钟文本（供主窗 1s 定时器调用）。 */
void HsfTitleBar_refreshClock(HsfTitleBar* self)
{
    if (!self) return;
    hsfTitleRefreshSideTexts(self);
}

/** @brief 设置标题文本。 */
void HsfTitleBar_setTitle(HsfTitleBar* self, const char* utf8)
{
    if (!self || !self->m_title) return;
    XLabel_setText_2(self->m_title, utf8 ? utf8 : "");
}

/** @brief 注册 ☰ 点击回调。 */
void HsfTitleBar_setMenuHandler(HsfTitleBar* self, HsfTitleBarMenuFn fn,
                                void* userData)
{
    if (!self) return;
    self->m_menuFn = fn;
    self->m_menuUserData = userData;
}

/** @brief 设置温度（℃）。 */
void HsfTitleBar_setTemperature(HsfTitleBar* self, double celsius)
{
    if (!self) return;
    self->m_temperature = celsius;
    hsfTitleRefreshSideTexts(self);
}

/** @brief 取温度（℃）。 */
double HsfTitleBar_temperature(const HsfTitleBar* self)
{
    return self ? self->m_temperature : 0.0;
}

/** @brief 设置湿度（%）。 */
void HsfTitleBar_setHumidity(HsfTitleBar* self, double percent)
{
    if (!self) return;
    self->m_humidity = percent;
    hsfTitleRefreshSideTexts(self);
}

/** @brief 取湿度（%）。 */
double HsfTitleBar_humidity(const HsfTitleBar* self)
{
    return self ? self->m_humidity : 0.0;
}

#endif /* XWIDGET_ON && XFRAME_ON && XLABEL_ON */
