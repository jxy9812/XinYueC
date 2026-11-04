
/**
 * @file       HsfLogin.c
 * @brief      客户端登录窗口实现。
 * @details    [流程] 登录钮 → 组装 host:port → HsfLinkClient_open（注册
 *             ready/fail 回调）→ 按钮禁用等待 → ready&&isReady：记忆
 *             地址/用户 → done(true)；fail（认证失败/首连不可达）：
 *             done(false) 并恢复输入。ready 回调是多订阅表，登录窗槽位
 *             与远端 provider 互不覆盖。[快捷] 下拉「本机服务器」一键
 *             回填 127.0.0.1:6100（服务端缺省端口口径见 XhsfLinkProto.h）。
 *             [离屏截图] --shot 模式下窗口在屏外静默出图后退出（CI 用）。
 *             [主窗之上·无边框·模态] 见 HsfLogin.h 文件头：父链居中、
 *             原生 transient owner、应用模态门挡住主窗输入。
 */
#include "HsfLogin.h"

#include "HsfLinkClient.h"
#include "HsfSettingsStore.h"
#include "HsfTheme.h"
#include "XhsfLinkProto.h"

#include "XGuiApplication.h"
#include "XWidget.h"
#include "XBoxLayout.h"
#include "XLabel.h"
#include "XLineEdit.h"
#include "XAbstractButton.h"
#include "XComboBox.h"
#include "XCheckBox.h"
#include "XSpinBox.h"
#include "XPushButton.h"
#include "XObject.h"
#include "XDateTime.h"
#include "XMemory.h"
#include "XPrintf.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <stddef.h>
#include "XImage.h"
#include "XPainter.h"
#include "XWindowEvent.h"   /* XMouseEvent_* 访问器（自绘条拖动/关闭）。 */
#include "XWidget_Protected.h"

/** @brief 登录窗固定尺寸（竖屏小窗；够放表单即可）。 */
#define HSF_LOGIN_W  420
#define HSF_LOGIN_H  560

/** @brief 自绘标题条高度（无边框窗的拖动把手 + 关闭钮区）。 */
#define HSF_LOGIN_BAR_H  36
/** @brief 关闭钮边长（右上角 [×] 热区）。 */
#define HSF_LOGIN_CLOSE_W 36

/** @brief 标题条配色（与主窗品牌蓝同源，见 HsfTheme.h）。 */
#define HSF_LOGIN_BAR_BG     0xff1f4e79u /**< 条底色（品牌深蓝）。 */
#define HSF_LOGIN_BAR_HOVER  0xffe81123u /**< [×] 悬停/按住红（Win10 口径）。 */
#define HSF_LOGIN_FRAME      0xff5a6572u /**< 面板 1px 边框。 */

/** @brief 下拉预设：本机服务器。 */
#define HSF_LOGIN_PRESET_LOCAL   0
/** @brief 下拉预设：自定义地址（不回填）。 */
#define HSF_LOGIN_PRESET_CUSTOM  1

/** @brief 登录窗对象（m_base 必须第一成员）。 */
struct HsfLoginWindow {
    XWidget      m_base;        /**< 顶层窗口；必须是第一个成员。 */
    XVBoxLayout  m_layout;      /**< 表单垂直布局。 */
    XLabel       m_title;       /**< 标题。 */
    XLabel       m_hostLabel;   /**< 「服务器地址」。 */
    XLabel       m_portLabel;   /**< 「端口」。 */
    XComboBox    m_preset;      /**< 预设下拉（本机服务器/自定义）。 */
    XLineEdit    m_hostEdit;    /**< 地址编辑框。 */
    XSpinBox     m_portSpin;    /**< 端口。 */
    XLabel       m_userLabel;   /**< 「用户名」。 */
    XLineEdit    m_userEdit;    /**< 用户名。 */
    XLabel       m_passLabel;   /**< 「密码」。 */
    XLineEdit    m_passEdit;    /**< 密码（密码回显）。 */
    XCheckBox    m_rememberUser; /**< 「记住账号」（勾选=用户名落本地并回填）。 */
    XCheckBox    m_rememberPass; /**< 「记住密码」（勾选=用户名+口令混淆落本地）。 */
    XPushButton  m_loginBtn;    /**< 登录钮。 */
    XLabel       m_status;      /**< 状态/错误行。 */
    /* ---- 状态 ---- */
    HsfLoginDoneFn m_done;      /**< 结果回调。 */
    void*          m_doneUser;  /**< 回调令牌。 */
    bool           m_pending;   /**< 登录在途（回调已挂，等待结果）。 */
    bool           m_finished;  /**< 已回调过（结果只发一次）。 */
    bool           m_modalGate; /**< 已登记应用模态门（关闭时要摘）。 */
    XTimerId       m_shotTimer; /**< 离屏截图拍（XTIMER_INVALID_ID=无）。 */
    const char*    m_shotPath;  /**< 截图输出路径（借用；NULL=无）。 */
    /* ---- 无边框窗的拖动/关闭（自绘条，见 paintEvent） ---- */
    bool           m_dragArmed; /**< 条上按下（拖动武装）。 */
    bool           m_closeHot;  /**< [×] 悬停态（重绘驱动）。 */
    bool           m_closeDown; /**< [×] 按住态。 */
    XPoint         m_dragOrigin;/**< 按下时的**全局**指针位。 */
    XPoint         m_dragWinPos;/**< 按下时的窗口全局原点。 */
};

XCLASS_DEFINE_BEGING(HsfLoginWindow)
XCLASS_DEFINE_EXTEND_END(HsfLoginWindow, XWidget)


/* ==================== 无头登录（CI/联调） ==================== */

static HsfLoginDoneFn g_headlessDone = NULL;    /**< 结果回调。 */
static void* g_headlessUser = NULL;             /**< 回调令牌。 */
static bool g_headlessPending = false;          /**< 在途标志。 */

/** @brief 无头登录就绪代理：转发 done(true)。 */
static void hsfHeadlessOnReadyProxy(void* userData)
{
    HsfLoginDoneFn done;
    void* user;
    (void)userData;
    if (!g_headlessPending || !HsfLinkClient_isReady()) return;
    g_headlessPending = false;
    done = g_headlessDone;
    user = g_headlessUser;
    g_headlessDone = NULL;
    if (done) done(user, true, "");
}

/** @brief 无头登录失败代理：转发 done(false)。 */
static void hsfHeadlessOnFailProxy(void* userData)
{
    HsfLoginDoneFn done;
    void* user;
    (void)userData;
    if (!g_headlessPending) return;
    g_headlessPending = false;
    done = g_headlessDone;
    user = g_headlessUser;
    g_headlessDone = NULL;
    if (done) done(user, false, "认证失败/服务器不可达/root 已在别处登录");
}

/* ==================== 内部工具 ==================== */

/**
 * @brief 摘除应用模态门（登录窗收尾的必做一步）。
 * @details 门是全局单值：登录窗登记后主窗输入被吞。登录成功（主窗接管
 *          交互）与取消/失败（退出循环）都必须解除，否则主窗永久只读。
 *          `gate==self` 守卫使重复调用幂等（XDialog 同口径）。
 */
static void hsfLoginDropGate(HsfLoginWindow* self)
{
    if (!self || !self->m_modalGate) return;
    self->m_modalGate = false;
    if (XWidget_applicationModalWidget() == (XWidget*)self)
        XWidget_setApplicationModalWidget(NULL);
}

/**
 * @brief 完成回调（一次性保护；窗口在自动模式外由调用方回收）。
 */
static void hsfLoginFinish(HsfLoginWindow* self, bool ok, const char* err)
{
    if (!self || self->m_finished) return;
    self->m_finished = true;
    self->m_pending = false;
    /* 结果已定 → 交互交还调用方（成功=主窗；失败=退出）。模态门先于此
     * 解除，回调里做的收尾动作（建主窗/退循环）不会被自己的门挡住。 */
    hsfLoginDropGate(self);
    if (self->m_done)
        self->m_done(self->m_doneUser, ok, ok ? "" : (err ? err : ""));
}

/* ==================== 口令本地混淆（XOR+hex，绑定用户名） ==================== */
/* [口径] 这不是加密：目的是 INI 里不出现口令明文（防肩窥/误拷贝泄露）。
 * 本机攻击者可逆。密钥 = "HsfLogin:" + 用户名 循环异或，换用户名即失效。 */

/** @brief 生成与用户名绑定的异或密钥流。 */
static void hsfLoginKeyStream(const char* user, uint8_t* out, size_t cap)
{
    static const char kPrefix[] = "HsfLogin:";
    size_t klen = sizeof(kPrefix) - 1;
    size_t ulen = user ? strlen(user) : 0;
    size_t total = klen + ulen;
    size_t i;
    for (i = 0; i < cap; ++i) {
        const char* src = (i < klen) ? kPrefix + i
                          : (ulen ? user + (i - klen) % ulen : kPrefix);
        out[i] = (uint8_t)*src;
        (void)total;
    }
}

/**
 * @brief 口令 → 混淆 hex 文本（INI 落盘形态）。
 * @return 成功返回 true；容量不足返回 false。
 */
static bool hsfLoginObfuscate(const char* user, const char* pass,
                              char* outHex, size_t cap)
{
    uint8_t key[64];
    size_t n = pass ? strlen(pass) : 0;
    size_t i;
    if (n * 2 + 1 > cap) return false;
    if (n > 64) return false;   /* 与密钥流容量对齐的超长防御。 */
    hsfLoginKeyStream(user, key, sizeof(key));
    for (i = 0; i < n; ++i)
        snprintf(outHex + i * 2, 3, "%02x", (unsigned)(pass[i] ^ key[i]));
    outHex[n * 2] = '\0';
    return true;
}

/**
 * @brief 混淆 hex 文本 → 口令（逆变换）。
 * @return 成功返回 true；hex 非法/容量不足返回 false。
 */
static bool hsfLoginDeobfuscate(const char* user, const char* hex,
                                char* outPass, size_t cap)
{
    uint8_t key[64];
    size_t n = hex ? strlen(hex) : 0;
    size_t i;
    if (n == 0 || (n % 2) != 0 || n / 2 >= cap) return false;
    hsfLoginKeyStream(user, key, sizeof(key));
    for (i = 0; i < n / 2; ++i) {
        char byte[3];
        unsigned v = 0;
        byte[0] = hex[i * 2];
        byte[1] = hex[i * 2 + 1];
        byte[2] = '\0';
        v = (unsigned)strtoul(byte, NULL, 16);
        outPass[i] = (char)(v ^ key[i]);
    }
    outPass[n / 2] = '\0';
    return true;
}

/**
 * @brief 记忆连接参数（设置落盘；口令按勾选决定混淆保存或清除）。
 */
static void hsfLoginRemember(const char* host, int port, const char* user,
                             const char* pass, bool rememberUser,
                             bool rememberPass)
{
    /* 联动归一（QQ 口径）：存密码必存账号；两者皆否时用户名也不留。 */
    if (rememberPass) rememberUser = true;
    if (!HsfSettingsStore_isOpen()) return;
    HsfSettingsStore_setString("server/lastHost", host);
    HsfSettingsStore_setInt("server/lastPort", port);
    HsfSettingsStore_setInt("login/rememberUser", rememberUser ? 1 : 0);
    HsfSettingsStore_setInt("login/rememberPass", rememberPass ? 1 : 0);
    if (rememberUser && user && user[0])
        HsfSettingsStore_setString("server/lastUser", user);
    else
        HsfSettingsStore_setString("server/lastUser", "");   /* 不记=清除。 */
    if (rememberPass && pass && pass[0]) {
        char hex[160];
        if (hsfLoginObfuscate(user, pass, hex, sizeof(hex)))
            HsfSettingsStore_setString("login/passHex", hex);
        else
            HsfSettingsStore_setString("login/passHex", "");
    } else {
        HsfSettingsStore_setString("login/passHex", "");   /* 不记=清除。 */
    }
    HsfSettingsStore_sync();
}

/** @brief 读字符串键的便捷封装（空缓冲安全）。 */
static void hsfLoginRecall(const char* key, char* out, size_t cap)
{
    out[0] = '\0';
    if (HsfSettingsStore_isOpen())
        HsfSettingsStore_getString(key, out, cap);
}

/* ==================== 槽 ==================== */

/**
 * @brief 就绪回调：连接+认证成功（或断线重连翻转）。
 */
static void hsfLoginOnReady(void* userData)
{
    HsfLoginWindow* self = (HsfLoginWindow*)userData;
    if (!self || !HsfLinkClient_isReady()) return;
    if (self->m_pending) {
        hsfLoginRemember(XLineEdit_text(&self->m_hostEdit),
                         XSpinBox_value(&self->m_portSpin),
                         XLineEdit_text(&self->m_userEdit),
                         XLineEdit_text(&self->m_passEdit),
                         XCheckBox_isChecked(&self->m_rememberUser),
                         XCheckBox_isChecked(&self->m_rememberPass));
        XLabel_setText_2(&self->m_status, "登录成功，进入主界面…");
    }
    hsfLoginFinish(self, true, "");
}

/**
 * @brief 失败回调：认证失败或首连不可达。
 */
static void hsfLoginOnFail(void* userData)
{
    HsfLoginWindow* self = (HsfLoginWindow*)userData;
    if (!self || !self->m_pending) return;
    XLabel_setText_2(&self->m_status, "登录失败：密码错误/服务器不可达/root 已在别处登录");
    XWidget_setEnabled((XWidget*)&self->m_loginBtn, true);
}

/**
 * @brief 登录钮点击：收集表单发起连接。
 */
static void hsfLoginBtnSlot(XObject* receiver, XVarList* args)
{
    HsfLoginWindow* self = (HsfLoginWindow*)((char*)receiver -
                                             offsetof(HsfLoginWindow, m_loginBtn));
    const char* host;
    const char* user;
    const char* pass;
    (void)args;
    if (!self || self->m_pending) return;
    host = XLineEdit_text(&self->m_hostEdit);
    user = XLineEdit_text(&self->m_userEdit);
    pass = XLineEdit_text(&self->m_passEdit);
    if (!host[0]) {
        XLabel_setText_2(&self->m_status, "请输入服务器地址");
        return;
    }
    if (!user[0]) {
        XLabel_setText_2(&self->m_status, "请输入用户名");
        return;
    }
    /* 回调必须在 open **之后**注册：open 会重置订阅表（无头路径同款坑，
       2026-11-04 实锅——先注册被 memset 清掉，登录成功后窗口永远不跳）。 */
    if (!HsfLinkClient_open(host, (uint16_t)XSpinBox_value(&self->m_portSpin),
                            user, pass)) {
        XLabel_setText_2(&self->m_status, "连接发起失败（地址格式非法）");
        return;
    }
    HsfLinkClient_setReadyCallback(hsfLoginOnReady, self);
    HsfLinkClient_setFailCallback(hsfLoginOnFail, self);
    self->m_pending = true;
    XWidget_setEnabled((XWidget*)&self->m_loginBtn, false);
    XLabel_setText_2(&self->m_status, "正在连接服务器…");
}

/**
 * @brief 「记住密码」切换：勾上则连带勾「记住账号」。
 * @note  取消自身不连带（取消密码不影响账号记忆）；「无账号则密码无意义」
 *        由「记住账号」侧的取消联动保证。
 */
static void hsfLoginRememberPassSlot(XObject* receiver, XVarList* args)
{
    HsfLoginWindow* self = (HsfLoginWindow*)((char*)receiver -
                                             offsetof(HsfLoginWindow, m_rememberPass));
    (void)args;
    if (!self) return;
    if (XCheckBox_isChecked(&self->m_rememberPass))
        XCheckBox_setChecked(&self->m_rememberUser, true);
}

/**
 * @brief 「记住账号」切换：取消则连带取消「记住密码」。
 * @note  勾上不连带（只记账号不必记密码）。
 */
static void hsfLoginRememberUserSlot(XObject* receiver, XVarList* args)
{
    HsfLoginWindow* self = (HsfLoginWindow*)((char*)receiver -
                                             offsetof(HsfLoginWindow, m_rememberUser));
    (void)args;
    if (!self) return;
    if (!XCheckBox_isChecked(&self->m_rememberUser))
        XCheckBox_setChecked(&self->m_rememberPass, false);
}

/**
 * @brief 预设下拉切换：本机服务器一键回填（自定义不动作）。
 */
static void hsfLoginPresetSlot(XObject* receiver, XVarList* args)
{
    HsfLoginWindow* self = (HsfLoginWindow*)((char*)receiver -
                                             offsetof(HsfLoginWindow, m_preset));
    int idx;
    (void)args;
    if (!self) return;
    idx = XComboBox_currentIndex(&self->m_preset);
    if (idx == HSF_LOGIN_PRESET_LOCAL) {
        XLineEdit_setText(&self->m_hostEdit, "127.0.0.1");
        XSpinBox_setValue(&self->m_portSpin, (int)HSF_XLINK_DEFAULT_PORT);
    }
}

/**
 * @brief CloseEvent：视为取消登录（回调 success=false 并退出循环）。
 */
static void VHsfLoginWindow_closeEvent(XWidget* widget, XEvent* event)
{
    HsfLoginWindow* self = (HsfLoginWindow*)widget;
    XClass_Parent(XWidget, EXWidget_CloseEvent,
                  void (*)(XWidget*, XEvent*))(widget, event);
    if (self && !self->m_finished) {
        HsfLinkClient_close();      /* 在途连接一并撤销。 */
        hsfLoginFinish(self, false, "已取消");   /* 内含模态门摘除。 */
    }
    XGuiApplication_quit();
    if (event) XEvent_accept((XEvent*)event);
}

/* ==================== 自绘标题条（无边框窗的拖动/关闭） ==================== */

/**
 * @brief [×] 命中（窗口局部坐标）。
 * @details 与绘制几何严格同源：右上角 HSF_LOGIN_CLOSE_W 见方，条带内。
 */
static bool hsfLoginCloseHit(const HsfLoginWindow* self, int x, int y)
{
    if (!self) return false;
    if (y < 0 || y >= HSF_LOGIN_BAR_H) return false;
    return x >= XWidget_width((const XWidget*)self) - HSF_LOGIN_CLOSE_W &&
           x < XWidget_width((const XWidget*)self);
}

/**
 * @brief 绘制：面板底/边框 + 顶部条带（标题 + [×]）。
 * @details 无边框窗没有 WM 装饰，标题条与关闭钮必须自绘（板端无 WM 时
 *          更是唯一出口）。条带同时是拖动把手——空白区按下即拖窗。
 */
static void VHsfLoginWindow_paintEvent(XWidget* self, XEvent* event)
{
    HsfLoginWindow* win = (HsfLoginWindow*)self;
    XImage* image;
    XPoint offset;
    XPainter painter;
    XRect rect;
    int w;
    int h;
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
    w = XWidget_width(self);
    h = XWidget_height(self);

    /* 条带：品牌蓝底 + 居中标题 + 右上 [×]。 */
    XRect_init(&rect, 0, 0, w, HSF_LOGIN_BAR_H);
    XPainter_fillRect(&painter, &rect, HSF_LOGIN_BAR_BG);
    XPainter_setPen(&painter, 0xffffffffu);
    XRect_init(&rect, 0, 0, w - HSF_LOGIN_CLOSE_W, HSF_LOGIN_BAR_H);
    XPainter_drawTextRect(&painter, &rect,
                          XPAINTER_TEXT_ALIGN_CENTER | XPAINTER_TEXT_ALIGN_VCENTER,
                          "PMMA高速过滤器 - 登录", 0xffffffffu);
    /* [×] 钮（悬停/按住红底白叉）。 */
    if (win && (win->m_closeHot || win->m_closeDown)) {
        XRect_init(&rect, w - HSF_LOGIN_CLOSE_W, 0,
                   HSF_LOGIN_CLOSE_W, HSF_LOGIN_BAR_H);
        XPainter_fillRect(&painter, &rect, HSF_LOGIN_BAR_HOVER);
    }
    if (win) {
        int cx = w - HSF_LOGIN_CLOSE_W / 2;
        int cy = HSF_LOGIN_BAR_H / 2;
        int i;
        XPainter_setPen(&painter, 0xffffffffu);
        for (i = -6; i <= 6; ++i) {
            XPainter_drawLine(&painter, cx + i, cy + i, cx + i + 1, cy + i + 1);
            XPainter_drawLine(&painter, cx + i, cy - i, cx + i + 1, cy - i - 1);
        }
    }

    /* 面板 1px 边框（无 WM 时窗口边界完全靠自绘）。 */
    XPainter_setPen(&painter, HSF_LOGIN_FRAME);
    XRect_init(&rect, 0, 0, w, h);
    XPainter_drawRect(&painter, &rect);

    XPainter_end(&painter);
    XPainter_deinit(&painter);
}

/**
 * @brief 鼠标按下：条带空白区=拖动武装；[×]=按住态。
 * @note  [×] 走「按下武装、释放命中触发」（Win10 caption 钮语义），
 *        中途移出即取消。条带外不消费，交子树（表单控件）。
 */
static void VHsfLoginWindow_mousePressEvent(XWidget* self, XEvent* event)
{
    HsfLoginWindow* win = (HsfLoginWindow*)self;
    XMouseEvent* mouse = (XMouseEvent*)event;
    XPoint pos;
    if (!self || !event || !mouse ||
        XEvent_type(event) != XEVENT_TYPE_MOUSE_BUTTON_PRESS) {
        XClass_Parent(XWidget, EXWidget_MousePressEvent,
                      void (*)(XWidget*, XEvent*))(self, event);
        return;
    }
    pos = XMouseEvent_position(mouse);
    if (hsfLoginCloseHit(win, pos.x, pos.y)) {
        win->m_closeDown = true;
        XWidget_update(self);
        XEvent_accept(event);
        return;
    }
    if (pos.y >= 0 && pos.y < HSF_LOGIN_BAR_H &&
        XMouseEvent_button(mouse) == XMouseButton_LeftButton) {
        /* 拖动武装：记按下时的全局指针位与窗口原点，MOVE 里按差值搬窗。
         * 抓取鼠标——指针移出窗口后 MOVE/RELEASE 仍投本窗（否则松手在
         * 窗外时抓取态残留）。 */
        XPoint g = XMouseEvent_globalPosition(mouse);
        win->m_dragArmed = true;
        win->m_dragOrigin = g;
        win->m_dragWinPos.x = XWidget_x(self);
        win->m_dragWinPos.y = XWidget_y(self);
        XWidget_grabMouse(self);
        XEvent_accept(event);
        return;
    }
    XClass_Parent(XWidget, EXWidget_MousePressEvent,
                  void (*)(XWidget*, XEvent*))(self, event);
}

/**
 * @brief 鼠标移动：拖动窗 / [×] 悬停跟踪。
 * @details 拖动按**全局**指针位移搬窗（局部坐标在窗口自身移动时会跟着
 *          变，用它算差值会自激漂移）。拖到窗口上限/屏边由平台钳位。
 */
static void VHsfLoginWindow_mouseMoveEvent(XWidget* self, XEvent* event)
{
    HsfLoginWindow* win = (HsfLoginWindow*)self;
    XMouseEvent* mouse = (XMouseEvent*)event;
    XPoint pos;
    if (!self || !event || !mouse ||
        XEvent_type(event) != XEVENT_TYPE_MOUSE_MOVE) {
        XClass_Parent(XWidget, EXWidget_MouseMoveEvent,
                      void (*)(XWidget*, XEvent*))(self, event);
        return;
    }
    pos = XMouseEvent_position(mouse);
    if (win->m_dragArmed &&
        (XMouseEvent_buttons(mouse) & XMouseButton_LeftButton) != 0) {
        XPoint g = XMouseEvent_globalPosition(mouse);
        XWidget_move(self,
                     win->m_dragWinPos.x + (g.x - win->m_dragOrigin.x),
                     win->m_dragWinPos.y + (g.y - win->m_dragOrigin.y));
        XEvent_accept(event);
        return;
    }
    if (win->m_dragArmed) {
        /* 中途丢按键：收口拖动与抓取。 */
        win->m_dragArmed = false;
        if (XWidget_mouseGrabber() == self) XWidget_releaseMouse(self);
    }
    {
        bool hot = hsfLoginCloseHit(win, pos.x, pos.y);
        if (hot != win->m_closeHot) {
            win->m_closeHot = hot;
            XWidget_update(self);
        }
    }
    XClass_Parent(XWidget, EXWidget_MouseMoveEvent,
                  void (*)(XWidget*, XEvent*))(self, event);
}

/**
 * @brief 鼠标释放：拖动收口；[×] 命中即取消登录（同标题栏关闭）。
 */
static void VHsfLoginWindow_mouseReleaseEvent(XWidget* self, XEvent* event)
{
    HsfLoginWindow* win = (HsfLoginWindow*)self;
    XMouseEvent* mouse = (XMouseEvent*)event;
    bool wasDown;
    bool inside;
    XPoint pos;
    if (!self || !event || !mouse ||
        XEvent_type(event) != XEVENT_TYPE_MOUSE_BUTTON_RELEASE) {
        XClass_Parent(XWidget, EXWidget_MouseReleaseEvent,
                      void (*)(XWidget*, XEvent*))(self, event);
        return;
    }
    pos = XMouseEvent_position(mouse);
    if (win->m_dragArmed) {
        win->m_dragArmed = false;
        if (XWidget_mouseGrabber() == self) XWidget_releaseMouse(self);
        XEvent_accept(event);
        return;
    }
    wasDown = win->m_closeDown;
    inside = hsfLoginCloseHit(win, pos.x, pos.y);
    win->m_closeDown = false;
    if (wasDown) {
        XWidget_update(self);
        XEvent_accept(event);
        if (inside && !win->m_finished) {
            /* 与标题栏关闭同口径：取消登录并退出循环。 */
            HsfLinkClient_close();
            hsfLoginFinish(win, false, "已取消");
            XGuiApplication_quit();
        }
        return;
    }
    XClass_Parent(XWidget, EXWidget_MouseReleaseEvent,
                  void (*)(XWidget*, XEvent*))(self, event);
}

/**
 * @brief 指针离开：[×] 悬停态归零（无 WM 时无自动重绘）。
 */
static void VHsfLoginWindow_leaveEvent(XWidget* self, XEvent* event)
{
    HsfLoginWindow* win = (HsfLoginWindow*)self;
    if (win && win->m_closeHot) {
        win->m_closeHot = false;
        XWidget_update(self);
    }
    XClass_Parent(XWidget, EXWidget_LeaveEvent,
                  void (*)(XWidget*, XEvent*))(self, event);
}

/**
 * @brief 离屏截图拍：后备图像出 PNG 后退出（CI 验收用）。
 */
static void VHsfLoginWindow_timerEvent(XObject* obj, XTimerEvent* event)
{
    HsfLoginWindow* self = (HsfLoginWindow*)obj;
    XTimerId id = XTimerEvent_timerId(event);
    XEvent_accept((XEvent*)event);
    if (!self || id != self->m_shotTimer || id == XTIMER_INVALID_ID) return;
    XObject_killTimer(obj, self->m_shotTimer);
    self->m_shotTimer = XTIMER_INVALID_ID;
    if (self->m_shotPath) {
        XImage* image = XWidget_paintImage(&self->m_base);
        if (image) {
            XPrintf("HsfClient: 保存登录窗截图到 %s\n", self->m_shotPath);
            if (!XImage_save_2(image, self->m_shotPath, "PNG", 95))
                XPrintf("HsfClient: 登录窗截图保存失败\n");
        }
    }
    XGuiApplication_quit();
}

/** @brief 登录窗类虚表初始化。 */
static XVtable* HsfLoginWindow_class_init(void)
{
    XVTABLE_INIT_DEFAULT(HsfLoginWindow)
    XVTABLE_INHERIT_XCLASS(XWidget);
    XVTABLE_OVERLOAD_DEFAULT(EXObject_TimerEvent, VHsfLoginWindow_timerEvent);
    XVTABLE_OVERLOAD_DEFAULT(EXWidget_CloseEvent, VHsfLoginWindow_closeEvent);
    XVTABLE_OVERLOAD_DEFAULT(EXWidget_PaintEvent,
                             VHsfLoginWindow_paintEvent);
    XVTABLE_OVERLOAD_DEFAULT(EXWidget_MousePressEvent,
                             VHsfLoginWindow_mousePressEvent);
    XVTABLE_OVERLOAD_DEFAULT(EXWidget_MouseMoveEvent,
                             VHsfLoginWindow_mouseMoveEvent);
    XVTABLE_OVERLOAD_DEFAULT(EXWidget_MouseReleaseEvent,
                             VHsfLoginWindow_mouseReleaseEvent);
    XVTABLE_OVERLOAD_DEFAULT(EXWidget_LeaveEvent,
                             VHsfLoginWindow_leaveEvent);
    XCLASS_SHOW_SIZE_DEFAULT(HsfLoginWindow);
    return XVTABLE_DEFAULT;
}

/* ==================== 表单装配 ==================== */

/**
 * @brief 建一行「标签 + 编辑框」。
 */
static void hsfLoginAddField(XVBoxLayout* layout, XLabel* label,
                             const char* text, XLineEdit* edit, XWidget* parent)
{
    XLabel_init(label, parent, 0);
    XLabel_setText_2(label, text);
    XWidget_show((XWidget*)label);
    XBoxLayout_addWidget(layout, (XWidget*)label);

    XLineEdit_init(edit, parent, 0);
    XWidget_setFixedHeight((XWidget*)edit, 36);
    XWidget_show((XWidget*)edit);
    XBoxLayout_addWidget(layout, (XWidget*)edit);
}

/* ==================== 公开 API ==================== */

HsfLoginWindow* HsfLoginWindow_create(HsfLoginDoneFn onDone, void* userData)
{
    return HsfLoginWindow_createFor(NULL, onDone, userData);
}

HsfLoginWindow* HsfLoginWindow_createFor(XWidget* owner,
                                         HsfLoginDoneFn onDone,
                                         void* userData)
{
    HsfLoginWindow* self;

    self = (HsfLoginWindow*)XMemory_malloc(sizeof(HsfLoginWindow),
                                           XCLASS_DEFAULT_MEMORY_TYPE);
    if (!self) return NULL;
    memset(self, 0, sizeof(*self));
    /* [无边框 + 父链] 以 owner 为父：①XWidget_init 因带 Window 位仍把
     * 本窗判为顶层（独立后备存储/原生窗）；②建窗时父链登记的 transient
     * owner 让原生窗恒在 owner 之上（XWidget_createWindow 内的 owner
     * 链路）；③首显走 xwidget_adjustPositionOnFirstShow 的「父窗口客户区
     * 居中」分支（--login-shot 无父时回落到主屏居中）。
     * FramelessWindowHint：三种形态（框架 CSD / WM 系统条 / 无 WM 板端）
     * 观感一致，不会被父窗原生装饰二次收边。
     * @note 类型位必须显式带 Window：XWidget_init 只在**无父**时自动补
     *       Window 位；带父时仅 Window/Popup 位能让控件成为顶层，单写
     *       FramelessWindowHint 会退化成父窗里的普通子控件（布局子件、
     *       无独立后备存储）。 */
    XWidget_init(&self->m_base, owner,
                 (XWidgetFlags)(XWindowType_Window |
                                XWindowType_FramelessWindowHint));
    Set_Class_Memory(self, XCLASS_DEFAULT_MEMORY_TYPE);
    Set_Class_IsHeap(self, true);
    XClassGetVtable(&self->m_base) = HsfLoginWindow_class_init();
    self->m_done = onDone;
    self->m_doneUser = userData;
    self->m_shotTimer = XTIMER_INVALID_ID;

    XWidget_resize(&self->m_base, HSF_LOGIN_W, HSF_LOGIN_H);
    XWidget_setWindowTitle_2(&self->m_base, "PMMA高速过滤器 - 登录");

    XBoxLayout_init((XBoxLayout*)&self->m_layout, XBoxLayoutDirection_TopToBottom);
    /* 顶边距让位自绘标题条（无边框窗的条带画在客户区内，见
     * VHsfLoginWindow_paintEvent）；其余三边留表单呼吸位。 */
    XLayout_setContentsMargins((XLayout*)&self->m_layout,
                               28, HSF_LOGIN_BAR_H + 16, 28, 20);
    XLayout_setSpacing((XLayout*)&self->m_layout, 8);
    XWidget_setLayout(&self->m_base, (XLayout*)&self->m_layout);

    /* 标题。 */
    XLabel_init(&self->m_title, &self->m_base, 0);
    XLabel_setText_2(&self->m_title, "高速过滤器客户端登录");
    XLabel_setAlignment(&self->m_title, XAlignment_HCenter);
    XWidget_show((XWidget*)&self->m_title);
    XBoxLayout_addWidget(&self->m_layout, (XWidget*)&self->m_title);

    /* 服务器地址（预设下拉 + 编辑框 + 端口）。 */
    XLabel_init(&self->m_hostLabel, &self->m_base, 0);
    XLabel_setText_2(&self->m_hostLabel, "服务器地址");
    XWidget_show((XWidget*)&self->m_hostLabel);
    XBoxLayout_addWidget(&self->m_layout, (XWidget*)&self->m_hostLabel);

    XComboBox_init(&self->m_preset, &self->m_base, 0);
    XComboBox_addItem_2(&self->m_preset, "本机服务器 (127.0.0.1:6100)");
    XComboBox_addItem_2(&self->m_preset, "自定义地址");
    XComboBox_setCurrentIndex(&self->m_preset, HSF_LOGIN_PRESET_LOCAL);
    XWidget_setFixedHeight((XWidget*)&self->m_preset, 36);
    XObject_connect_2((XObject*)&self->m_preset,
                      XSignal(XComboBox_activated_signal), hsfLoginPresetSlot);
    XWidget_show((XWidget*)&self->m_preset);
    XBoxLayout_addWidget(&self->m_layout, (XWidget*)&self->m_preset);

    XLineEdit_init(&self->m_hostEdit, &self->m_base, 0);
    XLineEdit_setPlaceholderText(&self->m_hostEdit, "服务器 IP，如 127.0.0.1");
    XWidget_setFixedHeight((XWidget*)&self->m_hostEdit, 36);
    XWidget_show((XWidget*)&self->m_hostEdit);
    XBoxLayout_addWidget(&self->m_layout, (XWidget*)&self->m_hostEdit);

    XLabel_init(&self->m_portLabel, &self->m_base, 0);
    XLabel_setText_2(&self->m_portLabel, "端口");
    XWidget_show((XWidget*)&self->m_portLabel);
    XBoxLayout_addWidget(&self->m_layout, (XWidget*)&self->m_portLabel);

    XSpinBox_init(&self->m_portSpin, &self->m_base, 0);
    XSpinBox_setRange(&self->m_portSpin, 1, 65535);
    XSpinBox_setValue(&self->m_portSpin, (int)HSF_XLINK_DEFAULT_PORT);
    XWidget_setFixedHeight((XWidget*)&self->m_portSpin, 36);
    XWidget_show((XWidget*)&self->m_portSpin);
    XBoxLayout_addWidget(&self->m_layout, (XWidget*)&self->m_portSpin);

    /* 凭据。 */
    hsfLoginAddField(&self->m_layout, &self->m_userLabel, "用户名",
                     &self->m_userEdit, &self->m_base);
    XLineEdit_setPlaceholderText(&self->m_userEdit, "admin / root");
    hsfLoginAddField(&self->m_layout, &self->m_passLabel, "密码",
                     &self->m_passEdit, &self->m_base);
    XLineEdit_setEchoMode(&self->m_passEdit, (int)XLineEditEchoMode_Password);

    /* 两个勾选框（QQ 口径联动）：记住账号 / 记住密码，同一行横排。 */
    {
        XWidget* optRow = XWidget_create(&self->m_base, 0);
        XHBoxLayout* optBox;
        if (optRow) {
            XWidget_setFixedHeight(optRow, 30);
            optBox = XHBoxLayout_create(optRow);
            if (optBox) {
                XBoxLayout_init((XBoxLayout*)optBox,
                                XBoxLayoutDirection_LeftToRight);
                XLayout_setContentsMargins((XLayout*)optBox, 0, 0, 0, 0);
                XLayout_setSpacing((XLayout*)optBox, 12);
                XCheckBox_init(&self->m_rememberUser, optRow, 0);
                XCheckBox_setText_2(&self->m_rememberUser, "记住账号");
                XWidget_setFixedHeight((XWidget*)&self->m_rememberUser, 28);
                XObject_connect_1((XObject*)&self->m_rememberUser,
                                  XSignal(XAbstractButton_toggled_signal),
                                  (XObject*)&self->m_rememberUser,
                                  hsfLoginRememberUserSlot,
                                  XConnectionType_Direct);
                XWidget_show((XWidget*)&self->m_rememberUser);
                XBoxLayout_addWidget((XBoxLayout*)optBox,
                                     (XWidget*)&self->m_rememberUser);
                XCheckBox_init(&self->m_rememberPass, optRow, 0);
                XCheckBox_setText_2(&self->m_rememberPass, "记住密码");
                XWidget_setFixedHeight((XWidget*)&self->m_rememberPass, 28);
                XObject_connect_1((XObject*)&self->m_rememberPass,
                                  XSignal(XAbstractButton_toggled_signal),
                                  (XObject*)&self->m_rememberPass,
                                  hsfLoginRememberPassSlot,
                                  XConnectionType_Direct);
                XWidget_show((XWidget*)&self->m_rememberPass);
                XBoxLayout_addWidget((XBoxLayout*)optBox,
                                     (XWidget*)&self->m_rememberPass);
                XWidget_setLayout(optRow, (XLayout*)optBox);
            }
            XWidget_show(optRow);
            XBoxLayout_addWidget(&self->m_layout, optRow);
        }
    }

    /* 登录钮。 */
    XPushButton_init(&self->m_loginBtn, &self->m_base, 0);
    XPushButton_setText_2(&self->m_loginBtn, "登 录");
    XWidget_setFixedHeight((XWidget*)&self->m_loginBtn, 44);
    XObject_connect_1((XObject*)&self->m_loginBtn,
                      XSignal(XAbstractButton_clicked_signal),
                      (XObject*)&self->m_loginBtn, hsfLoginBtnSlot,
                      XConnectionType_Direct);
    XWidget_show((XWidget*)&self->m_loginBtn);
    XBoxLayout_addWidget(&self->m_layout, (XWidget*)&self->m_loginBtn);

    /* 状态行。 */
    XLabel_init(&self->m_status, &self->m_base, 0);
    XLabel_setAlignment(&self->m_status, XAlignment_HCenter);
    XLabel_setText_2(&self->m_status, "");
    XWidget_show((XWidget*)&self->m_status);
    XBoxLayout_addWidget(&self->m_layout, (XWidget*)&self->m_status);

    /* 上次连接参数回填（密码不记忆）。 */
    {
        char hostBuf[64];
        char userBuf[64];
        int lastPort = HsfSettingsStore_isOpen()
                           ? HsfSettingsStore_getInt("server/lastPort",
                                                     (int)HSF_XLINK_DEFAULT_PORT)
                           : (int)HSF_XLINK_DEFAULT_PORT;
        char passHexBuf[160];
        char passBuf[80];
        hsfLoginRecall("server/lastHost", hostBuf, sizeof(hostBuf));
        hsfLoginRecall("server/lastUser", userBuf, sizeof(userBuf));
        hsfLoginRecall("login/passHex", passHexBuf, sizeof(passHexBuf));
        if (hostBuf[0]) {
            XLineEdit_setText(&self->m_hostEdit, hostBuf);
            XSpinBox_setValue(&self->m_portSpin, lastPort);
            XComboBox_setCurrentIndex(&self->m_preset, HSF_LOGIN_PRESET_CUSTOM);
        }
        if (userBuf[0])
            XLineEdit_setText(&self->m_userEdit, userBuf);
        if (HsfSettingsStore_getInt("login/rememberUser", 1) == 1 && userBuf[0])
            XCheckBox_setChecked(&self->m_rememberUser, true);
        if (HsfSettingsStore_getInt("login/rememberPass", 0) == 1 &&
            passHexBuf[0] && userBuf[0] &&
            hsfLoginDeobfuscate(userBuf, passHexBuf, passBuf, sizeof(passBuf))) {
            XLineEdit_setText(&self->m_passEdit, passBuf);
            XCheckBox_setChecked(&self->m_rememberPass, true);
            XCheckBox_setChecked(&self->m_rememberUser, true);   /* 联动归一。 */
        }
    }

    /* 皮肤接入（统一管理）：样式表由 HsfTheme_install 全局生效，主题换肤
     * 走 XApplication_setStyleSheet_2（应用级，全部顶层窗随皮肤重绘）。
     * 主窗与登录窗同源——登录窗在主窗**之后**创建，因此拿到的是用户
     * 已选皮肤（此前登录窗先建，恒停在默认浅色）。字体经
     * HsfTheme_applyDefaultFont 逐控件装配，不在此另造 chrome。 */
    HsfTheme_applyDefaultFont(&self->m_base);
    HsfTheme_applyDefaultFont((XWidget*)&self->m_title);
    HsfTheme_applyDefaultFont((XWidget*)&self->m_hostLabel);
    HsfTheme_applyDefaultFont((XWidget*)&self->m_preset);
    HsfTheme_applyDefaultFont((XWidget*)&self->m_hostEdit);
    HsfTheme_applyDefaultFont((XWidget*)&self->m_portLabel);
    HsfTheme_applyDefaultFont((XWidget*)&self->m_portSpin);
    HsfTheme_applyDefaultFont((XWidget*)&self->m_userLabel);
    HsfTheme_applyDefaultFont((XWidget*)&self->m_userEdit);
    HsfTheme_applyDefaultFont((XWidget*)&self->m_passLabel);
    HsfTheme_applyDefaultFont((XWidget*)&self->m_passEdit);
    HsfTheme_applyDefaultFont((XWidget*)&self->m_rememberUser);
    HsfTheme_applyDefaultFont((XWidget*)&self->m_rememberPass);
    HsfTheme_applyDefaultFont((XWidget*)&self->m_loginBtn);
    HsfTheme_applyDefaultFont((XWidget*)&self->m_status);

    XWidget_show(&self->m_base);    /* 顶层窗口最后显示。 */

    /* 模态门：显示后登记（对标 XDialog::exec 的显示即模态化），挡住
     * owner 主窗的输入——未登录不操作主窗。owner==NULL（离屏截图）时
     * 不登记，避免把整个应用锁死在无主窗的会话里。 */
    if (owner) {
        XWidget_setApplicationModalWidget(&self->m_base);
        self->m_modalGate = true;
    }
    return self;
}

void HsfLoginWindow_armShot(HsfLoginWindow* self, const char* path,
                            int w, int h)
{
    if (!self || !path || !path[0]) return;
    self->m_shotPath = path;
    XWidget_setGeometry(&self->m_base, -3000, -3000,
                        w > 0 ? w : HSF_LOGIN_W, h > 0 ? h : HSF_LOGIN_H);
    self->m_shotTimer = XObject_startTimer_ms((XObject*)self, 900u,
                                              XTimerType_CoarseTimer);
    if (self->m_shotTimer == XTIMER_INVALID_ID)
        XPrintf("HsfClient: 登录窗截图定时器创建失败\n");
}

bool HsfLogin_startHeadless(const char* spec, HsfLoginDoneFn onDone, void* userData)
{
    char buf[256];
    char* pass = NULL;
    char* host = NULL;
    char* portSep = NULL;
    uint16_t port = (uint16_t)HSF_XLINK_DEFAULT_PORT;
    bool ok;

    if (!spec || !spec[0] || !onDone) return false;
    snprintf(buf, sizeof(buf), "%s", spec);
    pass = strchr(buf, ':');
    host = strchr(buf, '@');
    if (!pass || !host || pass > host) return false;
    *pass = '\0';
    *host = '\0';
    /* host+1 = "host:port"，按最后一个 ':' 拆端口（无冒号取缺省）。 */
    portSep = strrchr(host + 1, ':');
    if (portSep) {
        int p;
        *portSep = '\0';
        p = atoi(portSep + 1);
        if (p <= 0 || p > 65535) return false;
        port = (uint16_t)p;
    }
    if (!((host + 1)[0])) return false;
    /* spec 解析完成后 buf=user，pass+1=password，host+1=host。
     * @note 回调必须在 open **之后**注册：open 会重置订阅表。 */
    ok = HsfLinkClient_open(host + 1, port, buf, pass + 1);
    if (!ok) return false;
    g_headlessDone = onDone;
    g_headlessUser = userData;
    g_headlessPending = true;
    HsfLinkClient_setFailCallback(hsfHeadlessOnFailProxy, NULL);
    HsfLinkClient_setReadyCallback(hsfHeadlessOnReadyProxy, NULL);
    return true;
}
