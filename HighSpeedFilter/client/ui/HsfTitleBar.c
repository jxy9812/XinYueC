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

/** @brief 全局运行模式钮宽度（顶栏右块左侧，可点击切换）。 */
#define HSF_TITLE_MODE_W 96

/** @brief 设备健康指示宽度（模式钮左侧；够放"故障99/离线99"）。 */
#define HSF_TITLE_HEALTH_W 108

/** @brief 登录账号标签宽度：按实时文本宽 + 余量，夹在 [MIN, MAX]。
 *  @details 12px 字库下「admin·普通用户」约 84px（半角 6/全角 12），
 *           96 的下限保证与健康(108)/模式(96)视觉等重；超长账号截断，
 *           不让一个 40 字符的用户名把标题区挤没。 */
#define HSF_TITLE_USER_MIN_W 96
#define HSF_TITLE_USER_MAX_W 150
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
    if (self->m_tempValid)
        snprintf(buf, sizeof(buf), "%.1f℃", self->m_temperature);
    else
        snprintf(buf, sizeof(buf), "--℃");
    if (self->m_temp)
        /* [不要在这里补 XWidget_update 2026-11-08] 原先带一句「XGui 无自动
         * 文本变化失效」的注释并无条件 XWidget_update。该前提**不成立**：
         * XLabel_setText 末尾经 label_updateLabel 已经
         * XWidget_updateGeometry + XWidget_update，文本真变了必然重绘；
         * 而这里再补一次，等于把 setText 的同值早退完全废掉——本函数被时钟
         * 与温湿度推送反复调用（实测 ~18 次/秒），两个 47×20 标签因此每秒
         * 产生 36 次入帧脏区，是标题栏持续重绘的根因。 */
        XLabel_setText_2(self->m_temp, buf);
    if (self->m_humValid)
        snprintf(buf, sizeof(buf), "%.1f%%", self->m_humidity);
    else
        snprintf(buf, sizeof(buf), "--%%");
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

    /* 自动运行状态片：模式钮左侧（手动模式隐藏 ⇒ 不占位）。 */
    if (self->m_autoChip &&
        XWidget_isVisible((XWidget*)self->m_autoChip)) {
        int chipW = XWidget_sizeHint((XWidget*)self->m_autoChip).width;
        int chipX;
        if (chipW < 96) chipW = 96;
        chipX = x - gap - chipW;
        if (chipX < HSF_TITLE_ICON_W) chipX = HSF_TITLE_ICON_W;
        XWidget_setGeometry((XWidget*)self->m_autoChip, chipX, 0,
                            chipW, barH);
        x = chipX;
    }

    /* 全局运行模式：时钟列左侧独立一段（整栏高、垂直居中），
     * 与右侧时钟/温湿度不重叠——三块各自占位，标题居中区间随之收窄。 */
    if (self->m_modeBtn) {
        int modeX = x - gap - HSF_TITLE_MODE_W;
        if (modeX < HSF_TITLE_ICON_W) modeX = HSF_TITLE_ICON_W;
        XWidget_setGeometry((XWidget*)self->m_modeBtn, modeX, 0,
                            HSF_TITLE_MODE_W, barH);
        x = modeX;
    }

    /* 设备健康：模式钮再往左一格。 */
    if (self->m_healthLbl) {
        int hx = x - gap - HSF_TITLE_HEALTH_W;
        if (hx < HSF_TITLE_ICON_W) hx = HSF_TITLE_ICON_W;
        XWidget_setGeometry((XWidget*)self->m_healthLbl, hx, 0,
                            HSF_TITLE_HEALTH_W, barH);
        x = hx;
    }

    /* 登录账号：健康指示再往左一格（「账号·角色」，未登录「未登录」）。
     * [窄栏不压字] 其余右块标签越界时 clamp 到 ☰ 区会互相叠字；账号是
     * 新增块，不能靠 clamp 混过去。空间不足以同时放下它和标题最小宽
     * （40px）时**隐藏**该标签并把位置让回健康指示，宽栏重排自动恢复——
     * 隐藏态不发占位，健康/模式/温湿度的既有排布完全不变。 */
    if (self->m_userLbl) {
        int userW = XWidget_sizeHint((XWidget*)self->m_userLbl).width + 8;
        int userX;
        if (userW < HSF_TITLE_USER_MIN_W) userW = HSF_TITLE_USER_MIN_W;
        if (userW > HSF_TITLE_USER_MAX_W) userW = HSF_TITLE_USER_MAX_W;
        userX = x - gap - userW;
        if (userX - gap < HSF_TITLE_ICON_W + 40) {
            /* 用显式隐藏位（isHidden）而不是 isVisible 做翻转判据：
             * isVisible 含父链，窗口未 show 时恒 false，会把"已被宽度
             * 隐藏"误判成"未隐藏"，窗口一 show 就露出压字的标签。 */
            if (!XWidget_isHidden((XWidget*)self->m_userLbl))
                XWidget_hide((XWidget*)self->m_userLbl);
        } else {
            if (XWidget_isHidden((XWidget*)self->m_userLbl))
                XWidget_show((XWidget*)self->m_userLbl);
            XWidget_setGeometry((XWidget*)self->m_userLbl, userX, 0,
                                userW, barH);
            x = userX;
        }
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
    if (XEvent_type(event) == XEVENT_TYPE_MOUSE_BUTTON_PRESS && bar && bar->m_modeFn) {
        XMouseEvent* mouse = (XMouseEvent*)event;
        XPoint pos = XMouseEvent_position(mouse);
        int barW = XWidget_width(&bar->m_base);
        /* [命中区用标签实际几何，不能硬算] 旧实现按 barW - HSF_TITLE_MODE_W -
         *   HSF_TITLE_GAP 硬算命中区，而标签实际画在「时钟列左侧」（relayout 里
         *   modeX = 时钟列左界 - gap - MODE_W）。两者在宽窗口下相差上百像素
         *   （1165 宽时标签在 x≈881、命中区在 x≈1059）⇒ 点「模式: 手动」
         *   落在温湿度区、事件被吞 ⇒ **点击毫无反应**（现场报"手动自动无法
         *   切换"的根因）。改为取标签自身几何：绘制与命中同源，永不漂移。 */
        if (bar->m_modeBtn) {
            XRect r = XWidget_geometry((XWidget*)bar->m_modeBtn);
            if (pos.x >= r.x && pos.x <= r.x + r.width) {
                bar->m_modeFn(bar->m_modeUserData);
                XEvent_accept(event);
                return;
            }
        }
        (void)barW;
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
    self->m_tempValid = false; /* 未收到遥测前显示 "--"，不显示 0.0 */
    self->m_humValid = false;

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
    /* 全局运行模式（手动/自动）：服务端一档管控全部设备，故置于顶栏。 */
    self->m_modeBtn = hsfTitleMakeSideLabel((XWidget*)&self->m_base, 12);
    if (self->m_modeBtn) {
        XLabel_setAlignment(self->m_modeBtn, XAlignment_HCenter | XAlignment_VCenter);
        /* 可点击：撤掉基类的鼠标穿透。 */
        XWidget_setAttribute((XWidget*)self->m_modeBtn,
                             XWidgetAttribute_TransparentForMouseEvents, false);
        XLabel_setText_2(self->m_modeBtn,
                         "模式--");   /* 模式-- */
    }
    /* 自动运行状态片：只在自动模式显示（手动模式隐藏）——"自动模式"是
     * 授权态，"控制器在跑"是运行态，两者分开呈现（对齐 Python 客户端）。 */
    self->m_autoChip = hsfTitleMakeSideLabel((XWidget*)&self->m_base, 12);
    if (self->m_autoChip) {
        XLabel_setAlignment(self->m_autoChip,
                            XAlignment_HCenter | XAlignment_VCenter);
        XLabel_setText_2(self->m_autoChip, " ");
        XWidget_setVisible((XWidget*)self->m_autoChip, false);
    }
    /* 设备健康指示：正常 / 故障N / 离线N（事件驱动推送，变化才刷）。 */
    self->m_healthLbl = hsfTitleMakeSideLabel((XWidget*)&self->m_base, 12);
    if (self->m_healthLbl) {
        XLabel_setAlignment(self->m_healthLbl,
                            XAlignment_HCenter | XAlignment_VCenter);
        XLabel_setText_2(self->m_healthLbl, "设备--"); /* 设备-- */
    }
    /* 登录账号：账号·角色（未登录显示「未登录」，如实反映会话状态）。 */
    self->m_userLbl = hsfTitleMakeSideLabel((XWidget*)&self->m_base, 12);
    if (self->m_userLbl) {
        XLabel_setAlignment(self->m_userLbl,
                            XAlignment_HCenter | XAlignment_VCenter);
        XLabel_setText_2(self->m_userLbl, "未登录"); /* 未登录 */
    }
    self->m_temp = hsfTitleMakeSideLabel((XWidget*)&self->m_base, 12);
    self->m_humid = hsfTitleMakeSideLabel((XWidget*)&self->m_base, 12);
    if (self->m_temp)
        XLabel_setAlignment(self->m_temp, XAlignment_Left | XAlignment_VCenter);
    if (self->m_humid)
        XLabel_setAlignment(self->m_humid, XAlignment_Left | XAlignment_VCenter);
    if (self->m_date) XWidget_show((XWidget*)self->m_date);
    if (self->m_clock) XWidget_show((XWidget*)self->m_clock);
    if (self->m_modeBtn) XWidget_show((XWidget*)self->m_modeBtn);
    if (self->m_healthLbl) XWidget_show((XWidget*)self->m_healthLbl);
    if (self->m_userLbl) XWidget_show((XWidget*)self->m_userLbl);
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
/**
 * @brief 注册全局运行模式钮点击回调。
 */
void HsfTitleBar_setModeHandler(HsfTitleBar* self, HsfTitleBarModeFn fn,
                               void* userData)
{
    if (!self) return;
    self->m_modeFn = fn;
    self->m_modeUserData = userData;
}

/**
 * @brief 更新顶栏运行模式显示。
 */
void HsfTitleBar_setAutoRunning(HsfTitleBar* self, bool known, bool running,
                                 bool autoMode)
{
    if (!self || !self->m_autoChip) return;
    if (!autoMode) {
        /* 手动模式隐藏（此时"控制器在跑"无意义——远程控制才可用）。 */
        XLabel_setText_2(self->m_autoChip, " ");
        XWidget_setVisible((XWidget*)self->m_autoChip, false);
        hsfTitleRelayout(self);
        return;
    }
    if (!known) {
        XLabel_setText_2(self->m_autoChip,
                         "自动·未知");
    } else if (running) {
        XLabel_setText_2(self->m_autoChip,
                         "自动·运行中");
    } else {
        XLabel_setText_2(self->m_autoChip,
                         "自动·已停止");
    }
    XWidget_setVisible((XWidget*)self->m_autoChip, true);
    hsfTitleRelayout(self);
}

void HsfTitleBar_setRunMode(HsfTitleBar* self, bool known, bool autoMode)
{
    if (!self || !self->m_modeBtn) return;
    self->m_modeKnown = known;
    self->m_modeAuto = autoMode;
    if (!known) {
        XLabel_setText_2(self->m_modeBtn, "模式--");  /* 模式-- */
    } else if (autoMode) {
        XLabel_setText_2(self->m_modeBtn,
            "模式：自动");  /* 模式：自动 */
    } else {
        XLabel_setText_2(self->m_modeBtn,
            "模式：手动");  /* 模式：手动 */
    }
    hsfTitleRelayout(self);
}
/**
 * @brief 更新顶栏设备健康指示。
 */
void HsfTitleBar_setHealth(HsfTitleBar* self, bool known, int fault, int offline)
{
    char buf[40];
    if (!self || !self->m_healthLbl) return;
    if (!known) {
        XLabel_setText_2(self->m_healthLbl, "设备--");  /* 设备-- */
    } else if (fault > 0) {
        snprintf(buf, sizeof(buf), "故障%d", fault);     /* 故障N */
        XLabel_setText_2(self->m_healthLbl, buf);
    } else if (offline > 0) {
        snprintf(buf, sizeof(buf), "离线%d", offline);   /* 离线N */
        XLabel_setText_2(self->m_healthLbl, buf);
    } else {
        XLabel_setText_2(self->m_healthLbl,
                         "设备正常"); /* 设备正常 */
    }
    hsfTitleRelayout(self);
}

/**
 * @brief 设置顶栏登录账号（「账号·角色」；未登录显示「未登录」）。
 * @details 入参是 UTF-8 借用指针，文本在本函数内拷贝进标签（XLabel 持有
 *          自己的 XString），调用后可释放/覆盖入参。角色名用
 *          HsfLinkClient_roleName() 的四档中文名（管理员/操作者/普通用户），
 *          不另造第五种叫法——它已与 XPermPolicy_roleName 同口径。
 */
void HsfTitleBar_setUser(HsfTitleBar* self, const char* user,
                         const char* roleName)
{
    char buf[128];
    if (!self || !self->m_userLbl) return;
    if (!user || !user[0]) {
        XLabel_setText_2(self->m_userLbl, "未登录"); /* 未登录 */
    } else if (roleName && roleName[0]) {
        snprintf(buf, sizeof(buf), "%s·%s", user, roleName);
        XLabel_setText_2(self->m_userLbl, buf);
    } else {
        XLabel_setText_2(self->m_userLbl, user);
    }
    hsfTitleRelayout(self);
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

/**
 * @brief 一次设置温湿度（小数口径；无效时传 NAN 表示"无数据"）。
 * @details 数值位数变化会改变右侧列宽（温度 "9.9" → "-10.0" 多一位），
 *          单独调 setTemperature/setHumidity 只刷文本不重排，右侧块会与
 *          标题重叠。本函数在两次赋值后统一刷文本 + 重排，调用方一帧内
 *          只触发一次布局。
 * @note  -999.0 及以下的温度值按"无数据"处理（显示 "--"），避免初次
 *        连接尚未收到遥测时显示 0.0 误导现场。
 */
void HsfTitleBar_setClimate(HsfTitleBar* self, double celsius, double percent,
                            bool tempValid, bool humValid)
{
    if (!self) return;
    /* [同值早退 2026-11-08] 下面是"落字段 + 重建文本 + 重排 + **同步
     * repaint 两个标签**"。调用方 hsf_titleSyncClimate 由**每秒时钟节拍**
     * 驱动（时钟定时器顺带刷新温湿度），温度没变也每秒跑一遍 ⇒ 两个标签
     * 每秒各被同步重画一次（画像实测：标题栏标签稳态 3 笔/秒脏区里 2 笔
     * 来自这里）。值完全相同就什么都不会变，提前返回即可；真正变化时
     * 行为不变。 */
    if (self->m_temperature == celsius && self->m_humidity == percent &&
        self->m_tempValid == tempValid && self->m_humValid == humValid)
        return;
    self->m_temperature = celsius;
    self->m_humidity = percent;
    self->m_tempValid = tempValid;
    self->m_humValid = humValid;
    hsfTitleRefreshSideTexts(self);
    hsfTitleRelayout(self);
    /* 子控件与父栏一并同步重绘。离屏合成路径（--shot 后备图像）下
     * XWidget_update 的异步标脏可能赶不上出图时刻，且父级按自身脏区
     * 决定绘制范围——这里用 repaint 走同步绘制，保证文本立即可见。 */
    if (self->m_temp) XWidget_repaint((XWidget*)self->m_temp);
    if (self->m_humid) XWidget_repaint((XWidget*)self->m_humid);
}

#endif /* XWIDGET_ON && XFRAME_ON && XLABEL_ON */
