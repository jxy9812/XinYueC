/**
 * @file       HsfStatusBar.c
 * @brief      底部黑色状态栏实现：先铺不透明黑底，再画左侧状态文本（按
 *             剩余宽省略）与右下角账户文本（右对齐避让）。
 */
#include "HsfStatusBar.h"

#include "HsfTheme.h"
#include "HsfUi.h"             /* hsfUi_fontPx（chrome 字号折算，只乘全局） */
#include "XWidget_Protected.h" /* XWidget_paintImage/paintOffset（自绘用） */
#include "XPixmap.h"
#include "XPainter.h"
#include "XCommonStyle.h"      /* XCommonStyle_elideText（左文本按剩余宽省略） */
#include "XPalette.h"          /* XPalette_color（前景色取用，对齐 XLabel 口径） */
#include "XMenu.h"             /* 账户区菜单（切换账户/退出登录）。 */
#include "XAction.h"
#include "XObject.h"
#include "XCursor.h"           /* PointingHandCursor（账户区手型光标）。 */
#include "XWindowEvent.h"      /* XMouseEvent_position（账户区命中）。 */
#include <stdio.h>
#include <string.h>

#if XWIDGET_ON && XFRAME_ON && XLABEL_ON

/* 状态栏源码基准：字号 14、定高 HSF_STATUS_BAR_H。全局缩放的幂等锚点
 * = 基准永远是这份源码常量、pct 现读现算（applyFont 从不读回当前字号
 * 当新基准，150%→160% 不会层层相乘）。 */
#define HSF_STATUS_FONT_PX 14

/* 右下角账户保留宽：按账户文本实测宽 + 余量，夹在 [MIN, MAX]。数值沿用
 * 原标题栏账号标签的宽度档（96/150，2026-11-11 搬家）：96 下限保证与
 * 标题栏右块视觉等重，150 上限防超长用户名把左状态区挤没。 */
#define HSF_STATUS_USER_MIN_W 96
#define HSF_STATUS_USER_MAX_W 150
#define HSF_STATUS_USER_GAP   8   /**< 左状态区与账户区之间的间隔。 */
#define HSF_STATUS_INDENT     16  /**< 左状态文本缩进（原 XLabel indent 口径）。 */

/** @brief 状态栏已应用的全局百分比（同值闸；chrome 单例口径）。 */
static int s_statusFontPct = -1;

/* ---- 账户区（可点，2026-11-11 对齐 Python StatusBar 账户钮） ---- */

/** @brief 账户菜单动作回调（HsfUi 接线；控件只发动作不碰业务）。 */
static HsfStatusBarAccountFn s_accountFn;
static void* s_accountUser;
/** 当前弹出菜单的两个动作（槽里按 sender 回映射动作类型；
 *  一次只弹一个菜单 ⇒ 文件级静态足够）。 */
static XAction* s_menuActs[2];
static int s_menuActCount;

/** @brief 拼右下角账户文案（三分支口径照搬原标题栏 setUser）。
 *  @note  末尾统一补「  ∨」下拉箭头（对齐 Python `_sync_text` 的
 *         `summary + "  ▾"`：没有内建下拉 ⇒ 文字尾部补箭头提示可点）。
 *         字形用「∨」(U+2228)而非「▾」：内置字库缺 U+25BE ⇒ 原来的箭头
 *         根本不显示（2026-11-12 截图实见：状态栏只有「未登录」）。
 *         命中区按实测宽含箭头。 */
static void hsfStatusUserText(char* buf, int cap,
                              const char* user, const char* roleName)
{
    if (!user || !user[0]) {
        snprintf(buf, (size_t)cap, "未登录  ∨");
    } else if (roleName && roleName[0]) {
        snprintf(buf, (size_t)cap, "%s·%s  ∨", user, roleName);
    } else {
        snprintf(buf, (size_t)cap, "%s  ∨", user);
    }
}

/** @brief 账户命中区（控件本地坐标；与绘制公式同源同参——矩形铁律）。
 *  @note  账户保留区=右端贴缘、宽=实测宽+GAP*2 夹 [MIN,MAX]、高=整栏；
 *         未登录（文案恒有「未登录」）同样可点（对齐 Python 恒可点）。 */
static XRect hsfStatusUserRect(const HsfStatusBar* self)
{
    XRect r;
    XFont font;
    int barW;
    int reserve;
    XRect_init(&r, 0, 0, 0, 0);
    if (!self) return r;
    barW = XWidget_width((const XWidget*)self);
    font = XWidget_font((const XWidget*)self);
    reserve = (self->m_user[0] ? XPainter_textWidth(&font, self->m_user) : 0)
              + HSF_STATUS_USER_GAP * 2;
    XClassDeinit(&font);
    if (reserve < HSF_STATUS_USER_MIN_W) reserve = HSF_STATUS_USER_MIN_W;
    if (reserve > HSF_STATUS_USER_MAX_W) reserve = HSF_STATUS_USER_MAX_W;
    if (reserve > barW) reserve = barW;
    XRect_init(&r, barW - reserve, 0, reserve,
               XWidget_height((const XWidget*)self));
    return r;
}

/** @brief 账户菜单动作槽：sender 区分「切换账户 / 退出登录」。 */
static void hsfStatusAccountSlot(XObject* receiver, XVarList* args)
{
    HsfStatusBar* bar = (HsfStatusBar*)receiver;
    XObject* sender;
    int i;
    (void)args;
    if (!bar) return;
    sender = XObject_sender((XObject*)bar);
    for (i = 0; i < s_menuActCount; ++i) {
        if ((XObject*)s_menuActs[i] == sender) {
            if (s_accountFn)
                s_accountFn(s_accountUser,
                            i == 0 ? HsfStatusBarAccount_SwitchUser
                                   : HsfStatusBarAccount_Logout);
            return;
        }
    }
}

/** @brief 点击账户区 ⇒ 弹「切换账户 / 退出登录」菜单（顶层 + 幂等）。
 *  @note  状态栏贴窗口底边 ⇒ 菜单往**上**弹（Python 同口径），几何交给
 *         XMenu_popup 的屏幕钳位兜底；菜单动作槽里只回调，不碰业务。 */
static void hsfStatusAccountMenuOpen(HsfStatusBar* bar)
{
    XMenu* menu;
    XPoint pos;
    XSize hint;
    XRect hit;
    XPoint local;
    XAction* a;
    if (!bar || !s_accountFn) return;
    menu = XMenu_create();
    if (!menu) return;
    s_menuActCount = 0;
    a = XAction_create();
    if (a) {
        XAction_setText_2(a, "切换账户");
        XObject_connect((XObject*)a, XSignal(XAction_triggered_signal),
                          (XObject*)bar, hsfStatusAccountSlot,
                          XConnectionType_Direct);
        XMenu_addActionAction(menu, a);
        if (s_menuActCount < 2) s_menuActs[s_menuActCount++] = a;
    }
    a = XAction_create();
    if (a) {
        XAction_setText_2(a, "退出登录");
        XObject_connect((XObject*)a, XSignal(XAction_triggered_signal),
                          (XObject*)bar, hsfStatusAccountSlot,
                          XConnectionType_Direct);
        XMenu_addActionAction(menu, a);
        if (s_menuActCount < 2) s_menuActs[s_menuActCount++] = a;
    }
    /* 定位：账户区左上角的**上方**（状态栏贴窗口底）。 */
    hit = hsfStatusUserRect(bar);
    local.x = hit.x;
    local.y = hit.y;
    pos = XWidget_mapToGlobal((XWidget*)bar, &local);
    /* [2026-11-13 合并修正] 对外统一走 `*_base` 契约（XMenu.h:650-655，
     * 本类不另设无后缀公开函数）。 */
    hint = XMenu_sizeHint_base(menu);
    pos.y -= hint.height > 0 ? hint.height : 60;
    XWidget_setAttribute((XWidget*)menu, XWidgetAttribute_DeleteOnClose,
                         true);
    XMenu_popup(menu, &pos);
}

/** @brief 绘制事件：铺黑底 → 左状态文本（ElideRight）→ 右下角账户（右对齐）。
 *  @details 左文本与账户的「保留宽」在同一函数一次算出，天然同源同参：
 *           账户按实测宽取保留区（夹 [MIN,MAX]），左文本可用宽 = 栏宽 -
 *           保留宽 - 间隔 - 缩进，超长部分省略号截尾——窄窗/竖屏下两段
 *           文本互不重叠。左文本的存储仍走基类 XLabel（XLabel_text 可读
 *           回原文），省略只发生在绘制时。 */
static void VHsfStatusBar_paintEvent(XWidget* self, XEvent* event)
{
    HsfStatusBar* bar;
    XImage* image;
    XPoint offset;
    XPainter painter;
    XRect rect;
    uint32_t textColor;
    int barW;
    int barH;
    int reserve;
    if (!self || !event || XEvent_type(event) != XEVENT_TYPE_PAINT) return;
    bar = (HsfStatusBar*)self;
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
    barW = XWidget_width(self);
    barH = XWidget_height(self);
    XRect_init(&rect, 0, 0, barW, barH);
    XPainter_fillRect(&painter, &rect, HSF_COLOR_STATUS_BG);
    /* 字体/取色对齐 XLabel 绘制口径（XLabel.c label_drawTextContent /
     * label_color）：XWidget_font 已含 setTextPixelSize 写入的字号；
     * 前景色按调色板 Current 组 + 控件前台角色解析。 */
    {
        XFont font = XWidget_font(self);
        XPalette palette;
        XColor color;
        XPainter_setFont(&painter, &font);
        palette = XWidget_palette(self);
        color = XPalette_color(&palette, XPaletteColorGroup_Current,
                               XWidget_foregroundRole(self));
        textColor = XColor_rgba(&color);
        /* 账户保留区：与命中测试同源（hsfStatusUserRect，矩形铁律）。 */
        {
            XRect ur = hsfStatusUserRect(bar);
            reserve = barW - ur.x;
        }
        /* 右下角账户：右端贴栏右缘、垂直居中（drawTextRect 缺省裁剪到
         * 布局矩形，不越右缘）。 */
        if (bar->m_user[0]) {
            XRect userRect;
            XRect_init(&userRect, barW - reserve, 0, reserve, barH);
            XPainter_drawTextRect(&painter, &userRect,
                                  XPAINTER_TEXT_ALIGN_RIGHT |
                                      XPAINTER_TEXT_ALIGN_VCENTER |
                                      XPAINTER_TEXT_SINGLE_LINE,
                                  bar->m_user, textColor);
        }
        /* 左侧状态文本：可用宽不足时跳过（XCommonStyle_elideText 在
         * maxW<=0 时原样返回原文，必须在这里拦住，否则整段溢出）。 */
        {
            int availW = barW - reserve - HSF_STATUS_USER_GAP - HSF_STATUS_INDENT;
            if (availW > 0) {
                const XString* txt = XLabel_text(&bar->m_base);
                const char* utf8 = txt ? XString_toUtf8(txt) : NULL;
                char elided[256];
                XRect textRect;
                const char* shown;
                if (!utf8) utf8 = "";
                shown = XCommonStyle_elideText(elided, (int)sizeof(elided),
                                               utf8, 1 /* ElideRight */,
                                               availW, &font);
                XRect_init(&textRect, HSF_STATUS_INDENT, 0, availW, barH);
                XPainter_drawTextRect(&painter, &textRect,
                                      XPAINTER_TEXT_ALIGN_LEFT |
                                          XPAINTER_TEXT_ALIGN_VCENTER |
                                          XPAINTER_TEXT_SINGLE_LINE,
                                      shown, textColor);
            }
        }
        XClassDeinit(&font);
    }
    XPainter_end(&painter);
    XPainter_deinit(&painter);
}

/** @brief 按下：命中右下角账户区 ⇒ 弹「切换账户 / 退出登录」菜单。 */
static void VHsfStatusBar_mousePressEvent(XWidget* self, XEvent* event)
{
    HsfStatusBar* bar = (HsfStatusBar*)self;
    XMouseEvent* me;
    XPoint pos;
    XRect hit;
    if (!bar || !event ||
        XEvent_type(event) != XEVENT_TYPE_MOUSE_BUTTON_PRESS) {
        XClass_Parent(XLabel, EXWidget_MousePressEvent,
                      void (*)(XWidget*, XEvent*))(self, event);
        return;
    }
    me = (XMouseEvent*)event;
    pos = XMouseEvent_position(me);
    hit = hsfStatusUserRect(bar);
    if (s_accountFn && pos.x >= hit.x && pos.x < hit.x + hit.width &&
        pos.y >= hit.y && pos.y < hit.y + hit.height) {
        hsfStatusAccountMenuOpen(bar);
        XEvent_accept(event);
        return;
    }
    XClass_Parent(XLabel, EXWidget_MousePressEvent,
                  void (*)(XWidget*, XEvent*))(self, event);
}

/** @brief 初始化 HsfStatusBar 类虚函数表。 */
XVtable* HsfStatusBar_class_init(void)
{
    XVTABLE_INIT_DEFAULT(HsfStatusBar)
    XVTABLE_INHERIT_XCLASS(XLabel);
    XVTABLE_OVERLOAD_DEFAULT(EXWidget_PaintEvent,
                             VHsfStatusBar_paintEvent);
    XVTABLE_OVERLOAD_DEFAULT(EXWidget_MousePressEvent,
                             VHsfStatusBar_mousePressEvent);
    return XVTABLE_DEFAULT;
}

/** @brief 初始化黑色状态栏：chrome 全套配置收在本模块。 */
void HsfStatusBar_init(HsfStatusBar* self, XWidget* parent,
                       XWidgetFlags flags)
{
    if (!self) return;
    /* RTC1 红线：m_user 随后会被 strcmp 读（setUser 同值早退），先清零。
     * 放在 XLabel_init 之前（与 HsfTitleBar_init 同款次序）。 */
    memset(self, 0, sizeof(HsfStatusBar));
    XLabel_init(&self->m_base, parent, flags);
    XClassSetVtable(self, HsfStatusBar);

    HsfTheme_applyDefaultFont((XWidget*)self);
    XLabel_setTextPixelSize((XLabel*)self, hsfUi_fontPx(HSF_STATUS_FONT_PX));
    XLabel_setAlignment((XLabel*)self,
                        XAlignment_Left | XAlignment_VCenter);
    /* 定高条减去上下 margin 后内容区易为负，缩进用 indent 不用 margin。 */
    XLabel_setIndent((XLabel*)self, 16);
    XWidget_setForegroundRole((XWidget*)self,
                              XPaletteColorRole_HighlightedText);
    XWidget_setFixedHeight((XWidget*)self, hsfUi_fontPx(HSF_STATUS_BAR_H));
    s_statusFontPct = hsfUi_globalFontPct();
    XWidget_show((XWidget*)self);
}

/** @brief 设置状态文本。 */
void HsfStatusBar_setText(HsfStatusBar* self, const char* utf8)
{
    if (!self) return;
    XLabel_setText_2((XLabel*)self, utf8);
}

/** @brief 设置右下角账户文案（同值早退：1Hz 兜底重复调用零脏区）。 */
void HsfStatusBar_setUser(HsfStatusBar* self, const char* user,
                          const char* roleName)
{
    char buf[128];
    if (!self) return;
    hsfStatusUserText(buf, (int)sizeof(buf), user, roleName);
    if (strcmp(buf, self->m_user) == 0) return;
    snprintf(self->m_user, sizeof(self->m_user), "%s", buf);
    XWidget_update((XWidget*)self);
}

/** @brief 状态栏字号实时重放（源码基准 14 / 26，pct 同值闸）。 */
void HsfStatusBar_applyFont(HsfStatusBar* self, int pct)
{
    if (!self || pct <= 0) return;
    if (pct == s_statusFontPct) return; /* 同值早退：重复触发不标脏。 */
    s_statusFontPct = pct;
    XLabel_setTextPixelSize((XLabel*)self,
                            (HSF_STATUS_FONT_PX * pct + 50) / 100);
    XWidget_setFixedHeight((XWidget*)self,
                           (HSF_STATUS_BAR_H * pct + 50) / 100);
}

/** @brief 注册账户区动作回调（点账户区弹菜单；控件只发动作不碰业务）。 */
void HsfStatusBar_setAccountAction(HsfStatusBar* self,
                                   HsfStatusBarAccountFn fn, void* userData)
{
    if (!self) return;
    s_accountFn = fn;
    s_accountUser = userData;
}

/** @brief 账户命中测试（与绘制/鼠标处理同一份几何口径）。 */
bool HsfStatusBar_accountHitTest(const HsfStatusBar* self, int x, int y)
{
    XRect hit = hsfStatusUserRect(self);
    return x >= hit.x && x < hit.x + hit.width &&
           y >= hit.y && y < hit.y + hit.height;
}

/** @brief 读当前账户显示文案（含「未登录」态与下拉箭头后缀）。 */
void HsfStatusBar_userText(const HsfStatusBar* self, char* buf, int cap)
{
    if (!self || !buf || cap <= 0) return;
    snprintf(buf, (size_t)cap, "%s", self->m_user);
}

#endif /* XWIDGET_ON && XFRAME_ON && XLABEL_ON */
