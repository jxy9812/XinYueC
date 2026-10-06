/** @file       XGuiRemoteInputViewTest.c
 *  @brief      远程输入链路 + 客户端视图适配缩放 专项探针(独立可执行)。
 *  @details    mcgs 战役输入路(2026-10-04)桌面自证载体, 回环零网络拓扑
 *              (XGuiRemoteLoopbackDevice_createPair, 同 XGuiRemoteTest.c
 *              xr_e2e_setup 惯例), 覆盖:
 *               T1 FIT 信箱几何(FB_META 不改控件几何 + 变换读回);
 *               T2 远端四角标记色块在 640x420 视图内全部可见(信箱黑边);
 *               T3 视图点击逆映射(变换契约断言 + 回环 e2e 命中断言);
 *               T4 键盘链 e2e(客户端合成键 → INPUT_KEY → 服务器注入 →
 *                  服务端 XLineEdit 文本落账);
 *               T5 滚轮逆映射 e2e(客户端滚轮 → 服务器 XScrollArea 滚动);
 *               T6 服务端注入多顶层命中(重叠 Popup 窗按钮可达, 对齐
 *                  XPlatformFbInput topLevelAt 口径);
 *               T7 FIT↔1:1 往返切换(1:1 恢复原尺寸原行为; 切回 FIT
 *                  信箱几何复原, 无旧帧残留);
 *               T8 x 分带 5 点点击扫描(横向 10/30/50/70/90% 五带各一点,
 *                  服务端目标计数核验——历史「客户端点击 x 向盲区」回归
 *                  口径, 覆盖左带既往事故区)。
 *              运行: Xvfb 显示下直跑, 退出码=失败断言数(0=全绿);
 *              失败行 [IV-FAIL] 打 stderr(门脚本 grep 口径)。
 *  @author     XinYueC 团队
 */
#include "XGuiServer.h"
#include "XGuiClient.h"
#include "XGuiRemoteLoopback.h"
#include "XGuiApplication.h"
#include "XWidget.h"
#include "XObject.h"
#include "XEvent.h"
#include "XCheckBox.h"
#include "XLineEdit.h"
#include "XScrollArea.h"
#include "XAbstractScrollArea.h"
#include "XScrollBar.h"
#include "XLabel.h"
#include "XImage.h"
#include "XPalette.h"
#include "XGeometry.h"
#include "XDateTime.h"
#include "XThread.h"
#include "XMemory.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

/* ==================== 断言与泵工具 ==================== */

static int iv_failures = 0;

static void iv_expect(bool cond, const char* what)
{
    if (!cond) {
        fprintf(stderr, "[IV-FAIL] %s\n", what ? what : "");
        ++iv_failures;
    }
}

static void iv_expect_msg(bool cond, const char* what, const char* detail)
{
    if (!cond) {
        fprintf(stderr, "[IV-FAIL] %s%s%s\n", what ? what : "",
                detail ? ": " : "", detail ? detail : "");
        ++iv_failures;
    }
}

static int64_t iv_nowMs(void) { return XDateTime_currentMSecsSinceEpoch(); }

static void iv_pumpFor(int ms)
{
    int64_t deadline = iv_nowMs() + ms;
    while (iv_nowMs() < deadline) {
        XGuiApplication_processEvents(XEventLoop_AllEvents);
        XThread_msleep(2);
    }
    XGuiApplication_processEvents(XEventLoop_AllEvents);
}

static bool iv_waitUntil(bool (*cond)(void*), void* ud, int timeoutMs)
{
    int64_t deadline = iv_nowMs() + timeoutMs;
    while (iv_nowMs() < deadline) {
        XGuiApplication_processEvents(XEventLoop_AllEvents);
        if (cond(ud)) return true;
        XThread_msleep(2);
    }
    XGuiApplication_processEvents(XEventLoop_AllEvents);
    return cond(ud);
}

/* ==================== 拓扑 ==================== */

#define IV_TOP_W 1024
#define IV_TOP_H 600
#define IV_VIEW_W 640
#define IV_VIEW_H 420

/** 服务端树 4 角标记块尺寸(远端像素)。 */
#define IV_MARK 64
/** 点击目标钮尺寸(远端像素; 吸收 ±1px 取整漂移)。 */
#define IV_TGT 16

typedef struct IvTopo {
    XWidget* host;            /* 客户端视图宿主 640x420(小视图场景)。 */
    XGuiClient* client;
    XWidget* top;             /* 服务端镜像树 1024x600。 */
    XWidget* mark[4];         /* 4 角标记块(TL/TR/BL/BR)。 */
    XCheckBox* tgt[4];        /* 逆映射点击目标(远端 4 处)。 */
    XCheckBox* band[5];       /* T8 x 分带扫描目标(远端横向 5 带)。 */
    XCheckBox* popupBtn;      /* T6 重叠弹层按钮。 */
    XWidget* popup;           /* T6 重叠顶层。 */
    XLineEdit* edit;          /* T4 键盘链读出点。 */
    XScrollArea* scroll;      /* T5 滚轮链读出点。 */
    XWidget* big;             /* T5 滚动内容子控件(传播对照用)。 */
    XGuiServer* server;
    XGuiRemoteLoopbackDevice* devA;
    XGuiRemoteLoopbackDevice* devB;
    int tgtHits[4];
    int bandHits[5];
    int popupHits;
} IvTopo;

static IvTopo* iv_cur = NULL;

static void iv_onTgt0(XObject* s, XVarList* a) { (void)s; (void)a; iv_cur->tgtHits[0]++; }
static void iv_onTgt1(XObject* s, XVarList* a) { (void)s; (void)a; iv_cur->tgtHits[1]++; }
static void iv_onTgt2(XObject* s, XVarList* a) { (void)s; (void)a; iv_cur->tgtHits[2]++; }
static void iv_onTgt3(XObject* s, XVarList* a) { (void)s; (void)a; iv_cur->tgtHits[3]++; }
static void iv_onBand0(XObject* s, XVarList* a) { (void)s; (void)a; iv_cur->bandHits[0]++; }
static void iv_onBand1(XObject* s, XVarList* a) { (void)s; (void)a; iv_cur->bandHits[1]++; }
static void iv_onBand2(XObject* s, XVarList* a) { (void)s; (void)a; iv_cur->bandHits[2]++; }
static void iv_onBand3(XObject* s, XVarList* a) { (void)s; (void)a; iv_cur->bandHits[3]++; }
static void iv_onBand4(XObject* s, XVarList* a) { (void)s; (void)a; iv_cur->bandHits[4]++; }
static void iv_onPopup(XObject* s, XVarList* a) { (void)s; (void)a; iv_cur->popupHits++; }

/** @brief 服务端树纯色块着色(e2e 惯例: palette 精确渲染色)。 */
static void iv_paintColor(XWidget* w, int r, int g, int b)
{
    XPalette pal;
    XColor c;
    XPalette_init_default(&pal);
    XColor_init_rgb(&c, r, g, b, 255);
    XPalette_setColor(&pal, XPaletteColorGroup_Active,
                      XPaletteColorRole_Window, c);
    XPalette_setColor(&pal, XPaletteColorGroup_Inactive,
                      XPaletteColorRole_Window, c);
    XPalette_setColor(&pal, XPaletteColorGroup_Disabled,
                      XPaletteColorRole_Window, c);
    XWidget_setPalette(w, &pal);
    XWidget_setAutoFillBackground(w, true);
}

/** @brief 事件收敛条件: 客户端 tile 计数 300ms 无增长。 */
static uint32_t iv_tilesLast = 0;
static bool iv_tilesSettled(void* ud)
{
    XGuiRemoteStats st;
    (void)ud;
    if (!iv_cur || !XGuiClient_isRemoteAlive(iv_cur->client)) return false;
    XGuiClient_statistics(iv_cur->client, &st);
    if (st.tileCount != iv_tilesLast) {
        iv_tilesLast = st.tileCount;
        return false;
    }
    return true;
}

static bool iv_streaming(void* ud) { (void)ud;
    return iv_cur && XGuiClient_isRemoteAlive(iv_cur->client); }

static bool iv_setup(IvTopo* e)
{
    XGuiRemoteLoopbackDevice* devB = NULL;
    memset(e, 0, sizeof(*e));
    (void)XGuiApplication_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, 0, NULL);
    iv_cur = e;

    /* 1) 服务端镜像树 1024x600: 4 角标记 + 4 点击目标 + 行编辑 + 滚动区。 */
    e->top = XWidget_create(NULL, 0);
    if (!e->top) return false;
    XWidget_resize(e->top, IV_TOP_W, IV_TOP_H);
    iv_paintColor(e->top, 0x21, 0x53, 0x9B); /* 底: 深蓝 21539b。 */
    {
        struct { int x, y, r, g, b; } mk[4] = {
            { 0, 0, 0xD9, 0x30, 0x25 },                 /* TL 红。 */
            { IV_TOP_W - IV_MARK, 0, 0x2E, 0x9E, 0x44 }, /* TR 绿。 */
            { 0, IV_TOP_H - IV_MARK, 0x1B, 0x7F, 0xC4 }, /* BL 蓝。 */
            { IV_TOP_W - IV_MARK, IV_TOP_H - IV_MARK,
              0xF2, 0xB7, 0x05 }                          /* BR 黄。 */
        };
        int i;
        for (i = 0; i < 4; ++i) {
            e->mark[i] = XWidget_create(e->top, 0);
            XWidget_setGeometry(e->mark[i], mk[i].x, mk[i].y, IV_MARK,
                                IV_MARK);
            iv_paintColor(e->mark[i], mk[i].r, mk[i].g, mk[i].b);
            XWidget_show(e->mark[i]);
        }
    }
    {
        struct { int x, y; void (*fn)(XObject*, XVarList*); } tg[4] = {
            { 92, 92, iv_onTgt0 },
            { 492, 292, iv_onTgt1 },
            { 892, 492, iv_onTgt2 },
            { 392, 542, iv_onTgt3 }
        };
        int i;
        for (i = 0; i < 4; ++i) {
            e->tgt[i] = XCheckBox_create(e->top, 0);
            XWidget_setGeometry((XWidget*)e->tgt[i], tg[i].x, tg[i].y,
                                IV_TGT, IV_TGT);
            XWidget_show((XWidget*)e->tgt[i]);
            XObject_connect_2((XObject*)e->tgt[i],
                              XSignal(XAbstractButton_clicked_signal),
                              tg[i].fn);
        }
    }
    /* T8 x 分带行: 远端横向 10/30/50/70/90% 五带, 同一 y=200 带线,
     * 中心 (102/307/512/717/922, 200)——覆盖历史「左带点击丢失」事故区
     * (真机口径 fb x≤~368)。y 带线避开行编辑(40..68)与滚动区(350+)。 */
    {
        struct { int cx; void (*fn)(XObject*, XVarList*); } bd[5] = {
            { 102, iv_onBand0 },
            { 307, iv_onBand1 },
            { 512, iv_onBand2 },
            { 717, iv_onBand3 },
            { 922, iv_onBand4 }
        };
        int i;
        for (i = 0; i < 5; ++i) {
            e->band[i] = XCheckBox_create(e->top, 0);
            XWidget_setGeometry((XWidget*)e->band[i], bd[i].cx - IV_TGT / 2,
                                200 - IV_TGT / 2, IV_TGT, IV_TGT);
            XWidget_show((XWidget*)e->band[i]);
            XObject_connect_2((XObject*)e->band[i],
                              XSignal(XAbstractButton_clicked_signal),
                              bd[i].fn);
        }
    }
    e->edit = XLineEdit_create(e->top, 0);
    XWidget_setGeometry((XWidget*)e->edit, 300, 40, 200, 28);
    XWidget_show((XWidget*)e->edit);
    e->scroll = XScrollArea_create(e->top, 0);
    XWidget_setGeometry((XWidget*)e->scroll, 600, 350, 200, 200);
    {
        XWidget* big = XWidget_create(NULL, 0); /* 高内容: 纵向可滚。 */
        XWidget_resize(big, 400, 800);
        iv_paintColor(big, 0x60, 0x60, 0x60);
        XScrollArea_setWidget(e->scroll, big);
        XWidget_show(big);
        e->big = big;
    }
    XWidget_show((XWidget*)e->scroll);
    XWidget_show(e->top);
    XWidget_repaint(e->top);

    /* 2) 服务器 + 回环对。 */
    e->server = XGuiServer_create(NULL);
    if (!e->server) return false;
    if (!XGuiServer_host(e->server, e->top)) return false;
    e->devA = XGuiRemoteLoopbackDevice_createPair(64u * 1024u, &devB);
    if (!e->devA || !devB) return false;
    e->devB = devB;
    if (XGuiServer_attachTransport(e->server, (XIODevice*)e->devA) <= 0)
        return false;

    /* 3) 客户端: 640x420 小视图(小于远端 1024x600), 默认 FIT。
     *    宿主挪到 (1200,0) 避开服务端镜像树(0,0,1024,600): 同进程回环
     *    里两窗同入 XGuiApplication 窗口表, 重叠+后登记会让
     *    xgs_mapInjectTarget→topLevelAt 把注入命中到客户端宿主窗
     *    (真实跨进程部署无此重叠; 桌面 e2e 客户端不 show 亦无此题)。 */
    e->host = XWidget_create(NULL, 0);
    if (!e->host) return false;
    XWidget_resize(e->host, IV_VIEW_W, IV_VIEW_H);
    XWidget_move(e->host, 1200, 0);
    XWidget_show(e->host);
    e->client = XGuiClient_create(e->host, 0);
    if (!e->client) return false;
    XWidget_setGeometry((XWidget*)e->client, 0, 0, IV_VIEW_W, IV_VIEW_H);
    XWidget_show((XWidget*)e->client);
    XGuiClient_setTransport(e->client, (XIODevice*)e->devB);
    return true;
}

static void iv_teardown(IvTopo* e)
{
    if (e->client) XClassDelete(e->client);
    if (e->server) XClassDelete(e->server);
    if (e->popup) XClassDelete(e->popup);
    if (e->devA) XClassDelete((XClass*)e->devA);
    if (e->devB) XClassDelete((XClass*)e->devB);
    if (e->top) XClassDelete(e->top);
    if (e->host) XClassDelete(e->host);
    if (iv_cur == e) iv_cur = NULL;
    memset(e, 0, sizeof(*e));
}

/* ==================== 事件合成 ==================== */

static void iv_sendMouse(XGuiClient* c, XEventType type, XMouseButton button,
                         XMouseButton buttons, int x, int y)
{
    XMouseEvent* me;
    XPoint pos;
    XPoint_init(&pos, x, y);
    me = XMouseEvent_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, type, button,
                               (XKeyboardModifiers)0, pos);
    if (!me) return;
    XMouseEvent_setButtons(me, buttons);
    XObject_event_base((XObject*)c, (XEvent*)me);
    XClassDelete((XEvent*)me);
}

static void iv_sendWheel(XGuiClient* c, int x, int y, int angleY)
{
    XWheelEvent we;
    XPoint pos;
    XPoint delta;
    XPoint_init(&pos, x, y);
    XPoint_init(&delta, 0, angleY);
    XWheelEvent_init(&we, XEVENT_TYPE_WHEEL, &pos, NULL, &delta,
                     XMouseButton_NoButton, (XKeyboardModifiers)0);
    XObject_event_base((XObject*)c, (XEvent*)&we);
}

static void iv_sendKey(XGuiClient* c, int key, bool press)
{
    XKeyEvent ke;
    XKeyEvent_init(&ke, press ? XEVENT_TYPE_KEY_PRESS
                              : XEVENT_TYPE_KEY_RELEASE,
                   key, (XKeyboardModifiers)0);
    XObject_event_base((XObject*)c, (XEvent*)&ke);
}

/* ==================== 像素与坐标工具 ==================== */

/** @brief 读抓取图像像素(ARGB32, 小端 BGRA 内存序)。 */
static uint32_t iv_pixel(XImage* img, int x, int y)
{
    const uint8_t* p = XImage_bits(img) +
                       (size_t)y * (size_t)XImage_bytesPerLine(img) +
                       (size_t)x * 4u;
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) |
           ((uint32_t)p[3] << 24);
}

/** @brief 远端像素 → 视图坐标(渲染正变换同款取整: int(rx*scale)。 */
static int iv_viewFromRemote(float scale, int off, int rx)
{
    return off + (int)((float)rx * scale);
}

/** @brief 视图坐标 → 远端像素(客户端输入逆映射契约: (v-off)/scale 截断)。 */
static int iv_viewToRemote(float scale, int off, int vx)
{
    return (int)(((float)(vx - off)) / scale);
}

/* ==================== 用例 ==================== */

/** 期望的 FIT 变换: scale=min(640/1024,420/600)=0.625, offX=0,
 *  offY=(420-round(600*0.625))/2=22。 */
#define IV_FIT_SCALE 0.625f
#define IV_FIT_OFFX 0
#define IV_FIT_OFFY 22

static void iv_assertFitTransform(IvTopo* e, const char* tag)
{
    float sc = 0.0f;
    int ox = -1, oy = -1;
    bool ok = XGuiClient_viewTransform(e->client, &sc, &ox, &oy);
    if (!ok || sc < IV_FIT_SCALE - 0.001f || sc > IV_FIT_SCALE + 0.001f ||
        ox != IV_FIT_OFFX || oy != IV_FIT_OFFY) {
        char msg[160];
        snprintf(msg, sizeof(msg), "%s ok=%d scale=%.4f off=(%d,%d)", tag,
                 (int)ok, (double)sc, ox, oy);
        iv_expect_msg(false, "FIT 变换读回 = contain 信箱期望值", msg);
    } else {
        iv_expect(true, tag);
    }
}

/** @brief T3/T6 共用: 视图点击 → 契约断言 + 回环命中等待。 */
static void iv_clickAndAwait(IvTopo* e, int vx, int vy, int rx, int ry,
                             int* hitCounter, int tgtIdx, const char* tag)
{
    float sc = 0.0f;
    int ox = 0, oy = 0;
    int mx, my;
    int64_t deadline;
    (void)XGuiClient_viewTransform(e->client, &sc, &ox, &oy);
    mx = iv_viewToRemote(sc, ox, vx);
    my = iv_viewToRemote(sc, oy, vy);
    /* 契约断言: 逆映射值落在目标钮远端矩形内(换算值逐位核对)。 */
    if (mx < rx - IV_TGT / 2 || mx > rx + IV_TGT / 2 ||
        my < ry - IV_TGT / 2 || my > ry + IV_TGT / 2) {
        char msg[160];
        snprintf(msg, sizeof(msg),
                 "%s view=(%d,%d) 逆映射=(%d,%d) 期望中心=(%d,%d)", tag,
                 vx, vy, mx, my, rx, ry);
        iv_expect_msg(false, "视图点击逆映射落在目标远端矩形", msg);
        return;
    }
    iv_sendMouse(e->client, XEVENT_TYPE_MOUSE_BUTTON_PRESS,
                 XMouseButton_LeftButton, XMouseButton_LeftButton, vx, vy);
    iv_sendMouse(e->client, XEVENT_TYPE_MOUSE_BUTTON_RELEASE,
                 XMouseButton_LeftButton, XMouseButton_NoButton, vx, vy);
    deadline = iv_nowMs() + 5000;
    while (iv_nowMs() < deadline) {
        XGuiApplication_processEvents(XEventLoop_AllEvents);
        XThread_msleep(2);
        if (*hitCounter > 0) break;
    }
    if (*hitCounter != 1) {
        char msg[96];
        snprintf(msg, sizeof(msg), "%s hits=%d", tag, *hitCounter);
        iv_expect_msg(false, "远端目标收到转发点击(x1)", msg);
    } else {
        iv_expect(true, tag);
    }
    (void)tgtIdx;
}

int main(void)
{
    IvTopo e;
    uint32_t baseTiles = 0;

    if (!iv_setup(&e)) {
        fprintf(stderr, "[IV-FAIL] 拓扑建立\n");
        iv_teardown(&e);
        return 1;
    }

    /* ---- 流式就绪 + 全量 tile 网格收敛 ---- */
    if (!iv_waitUntil(iv_streaming, NULL, 8000)) {
        fprintf(stderr, "[IV-FAIL] 握手→FB_META→Streaming\n");
        iv_teardown(&e);
        return 1;
    }
    {
        XGuiRemoteStats st;
        int guard;
        XWidget_repaint(e.top);
        for (guard = 0; guard < 100; ++guard) {
            iv_pumpFor(50);
            XGuiClient_statistics(e.client, &st);
            if (st.tileCount >= baseTiles + 8u * 5u) break; /* 8x5 网格。 */
        }
    }
    iv_pumpFor(300); /* 排干在途帧。 */

    /* ---- T1: FIT 信箱几何 ---- */
    iv_expect(XGuiClient_viewFitMode(e.client) == XGUI_CLIENT_VIEW_FIT,
              "T1 默认模式 = FIT");
    iv_expect(XWidget_width((XWidget*)e.client) == IV_VIEW_W &&
              XWidget_height((XWidget*)e.client) == IV_VIEW_H,
              "T1 FIT 下 FB_META 不改控件几何(保持 640x420)");
    iv_assertFitTransform(&e, "T1 变换读回 scale=0.625 off=(0,22)");

    /* ---- T2: 远端四角标记色块在视图内全部可见 + 信箱黑边 ---- */
    {
        XImage* snap = XWidget_grab((XWidget*)e.client);
        iv_expect(snap != NULL, "T2 客户端视图抓取就绪");
        if (snap) {
            struct { int rx, ry; uint32_t want; const char* what; } mk[4] = {
                { 32, 32, 0xFFD93025u, "左上红" },
                { IV_TOP_W - 32, 32, 0xFF2E9E44u, "右上绿" },
                { 32, IV_TOP_H - 32, 0xFF1B7FC4u, "左下蓝" },
                { IV_TOP_W - 32, IV_TOP_H - 32, 0xFFF2B705u, "右下黄" }
            };
            int i;
            for (i = 0; i < 4; ++i) {
                int vx = iv_viewFromRemote(IV_FIT_SCALE, IV_FIT_OFFX,
                                           mk[i].rx);
                int vy = iv_viewFromRemote(IV_FIT_SCALE, IV_FIT_OFFY,
                                           mk[i].ry);
                uint32_t got = iv_pixel(snap, vx, vy);
                if (got != mk[i].want) {
                    char msg[128];
                    snprintf(msg, sizeof(msg), "%s remote=(%d,%d)->view=(%d,%d) got=%08x want=%08x",
                             mk[i].what, mk[i].rx, mk[i].ry, vx, vy,
                             (unsigned)got, (unsigned)mk[i].want);
                    iv_expect_msg(false, "T2 远端四角标记色块视图内可见", msg);
                } else {
                    iv_expect(true, "T2 四角标记可见");
                }
            }
            /* 信箱黑边: 内容带 y∈[22,397), 上下黑条。 */
            iv_expect(iv_pixel(snap, 320, 8) == 0xFF000000u,
                      "T2 上黑边(320,8)=纯黑");
            iv_expect(iv_pixel(snap, 320, 410) == 0xFF000000u,
                      "T2 下黑边(320,410)=纯黑");
            XClassDelete(snap);
        }
    }

    /* ---- T3: 视图点击逆映射(4 目标) ---- */
    iv_clickAndAwait(&e, 62, 84, 100, 100, &e.tgtHits[0], 0,
                     "T3 点击 view(62,84)→远端(100,100)");
    iv_clickAndAwait(&e, 312, 209, 500, 300, &e.tgtHits[1], 1,
                     "T3 点击 view(312,209)→远端(500,300)");
    iv_clickAndAwait(&e, 562, 334, 900, 500, &e.tgtHits[2], 2,
                     "T3 点击 view(562,334)→远端(900,500)");
    iv_clickAndAwait(&e, 250, 365, 400, 550, &e.tgtHits[3], 3,
                     "T3 点击 view(250,365)→远端(400,550)");
    iv_expect(e.tgtHits[0] + e.tgtHits[1] + e.tgtHits[2] + e.tgtHits[3] == 4,
              "T3 四目标计数合计=4(无重复无丢失)");

    /* ---- T8: x 分带 5 点点击扫描(横向 10/30/50/70/90% 五带) ----
     *    历史「客户端点击 x 向盲区」(真机口径 fb x≤~368 才达服务端)
     *    回归口径: 五带各一点, 服务端分带目标计数核验(每带恰 1 次,
     *    合计 5 无重复无丢失); 左带 102/307 即既往事故区。 */
    {
        static const int bandCx[5] = { 102, 307, 512, 717, 922 };
        static const char* bandTag[5] = {
            "T8 左带10% view点击→远端(102,200)",
            "T8 左带30% view点击→远端(307,200)",
            "T8 中带50% view点击→远端(512,200)",
            "T8 右带70% view点击→远端(717,200)",
            "T8 右带90% view点击→远端(922,200)"
        };
        int i;
        for (i = 0; i < 5; ++i) {
            int vx = iv_viewFromRemote(IV_FIT_SCALE, IV_FIT_OFFX, bandCx[i]);
            int vy = iv_viewFromRemote(IV_FIT_SCALE, IV_FIT_OFFY, 200);
            iv_clickAndAwait(&e, vx, vy, bandCx[i], 200, &e.bandHits[i], i,
                             bandTag[i]);
        }
        iv_expect(e.bandHits[0] + e.bandHits[1] + e.bandHits[2] +
                  e.bandHits[3] + e.bandHits[4] == 5,
                  "T8 五带计数合计=5(无重复无丢失)");
    }

    /* ---- T4: 键盘链 e2e(客户端合成键 → 服务端行编辑落账) ----
     *     同进程回环的焦点契约: g_focusWidget 应用级单值——客户端转发
     *     门控与服务端按键派发各自需要焦点。时序: 先客户端持焦点发键
     *     (键只进回环设备, 服务端未派发) → 再焦点交回服务端行编辑 →
     *     泵使 INPUT_KEY 派发时 g_focusWidget 已在编辑框(跨进程真实部
     *     署两侧焦点独立, 无此时序约束)。 */
    {
        const char* before = XLineEdit_text(e.edit);
        iv_expect(before && before[0] == '\0', "T4 行编辑初始为空");
        XWidget_setFocus((XWidget*)e.client); /* 客户端转发焦点门控。 */
        iv_expect(XWidget_hasFocus((XWidget*)e.client),
                  "T4 客户端持焦点(转发门控开)");
        iv_sendKey(e.client, 'a', true);
        iv_sendKey(e.client, 'a', false);
        iv_sendKey(e.client, '1', true);
        iv_sendKey(e.client, '1', false);
        XWidget_setFocus((XWidget*)e.edit);   /* 服务端派发焦点落位。 */
        iv_pumpFor(300);
        {
            const char* txt = XLineEdit_text(e.edit);
            iv_expect(txt && strcmp(txt, "a1") == 0,
                      "T4 服务端行编辑收到 'a1'(键链端到端)");
        }
        /* 收起按下驱动虚拟键盘(框架 UX: 编辑框夺焦弹出、点空白收起):
         * 键盘停靠宿主窗下半带, 不收起会吃掉下半幅的后续滚轮/点击。 */
        {
            float sc2 = 0.0f;
            int ox2 = 0, oy2 = 0;
            int dx, dy;
            (void)XGuiClient_viewTransform(e.client, &sc2, &ox2, &oy2);
            dx = iv_viewFromRemote(sc2, ox2, 50);
            dy = iv_viewFromRemote(sc2, oy2, 250);
            iv_sendMouse(e.client, XEVENT_TYPE_MOUSE_BUTTON_PRESS,
                         XMouseButton_LeftButton, XMouseButton_LeftButton,
                         dx, dy);
            iv_sendMouse(e.client, XEVENT_TYPE_MOUSE_BUTTON_RELEASE,
                         XMouseButton_LeftButton, XMouseButton_NoButton,
                         dx, dy);
            iv_pumpFor(150);
        }
    }

    /* ---- T5: 滚轮逆映射 e2e(客户端滚轮 → 服务端滚动条滚动) ----
     *     读出点=纵向滚动条本体(XAbstractSlider 自带滚轮消费, 单步
     *     SingleStep): 视口内容子控件路径的滚轮在框架视口层被吞
     *     (childAt 深命中=viewport, 直投滚动区可滚而深命中不滚——
     *     框架既有行为, 非本路修复面), 故读出点选滚条本体保证确定性。 */
    {
        XScrollBar* bar = XAbstractScrollArea_verticalScrollBar(
            (XAbstractScrollArea*)e.scroll);
        XRect bg;
        XRect wg;
        XPoint zero, gp;
        float sc = 0.0f;
        int ox = 0, oy = 0;
        int v0, v1, d;
        int vxp, vyp;
        int bcx, bcy;
        bg = XWidget_geometry((XWidget*)bar);
        iv_expect(bg.width > 0 && bg.height > 0 && bar != NULL,
                  "T5 滚动条几何就绪");
        (void)XGuiClient_viewTransform(e.client, &sc, &ox, &oy);
        /* 滚条中心(远端坐标) → 视图坐标。geometry 为父系坐标, 须经
         * mapToGlobal 换到屏幕帧再减镜像窗原点得顶层局部(镜像帧)。 */
        XPoint_init(&zero, 0, 0);
        gp = XWidget_mapToGlobal((XWidget*)bar, &zero);
        wg = XWindow_geometry(XWidget_windowHandle(e.top));
        bcx = gp.x - wg.x + bg.width / 2;
        bcy = gp.y - wg.y + bg.height / 2;
        vxp = iv_viewFromRemote(sc, ox, bcx);
        vyp = iv_viewFromRemote(sc, oy, bcy);
        /* 正控: 滚轮直投服务端滚条(不经转发链), 验证读出点可滚。 */
        v0 = bar ? XScrollBar_value(bar) : -1;
        /* 角度 +120: steps=+1 → SingleStepAdd → 值 +singleStep(默认 1,
         * 起始 0 处 -120 为 Sub 会被下界钳位无位移)。 */
        iv_sendWheel((XGuiClient*)bar, 3, 10, 120);
        iv_pumpFor(100);
        v1 = bar ? XScrollBar_value(bar) : -1;
        d = (v0 >= 0 && v1 >= 0) ? v1 - v0 : -999;
        iv_expect(d > 0, "T5 正控: 滚条直投滚轮值增(读出点可滚)");
        if (bar) XScrollBar_setValue(bar, 0);
        iv_pumpFor(50);
        /* 全链: 客户端视图滚轮 → 逆映射 → 转发 → 注入 → 滚条值增。
         * 角度 +120(同正控口径, 起始 0 处 Sub 向被钳位)。 */
        if (bar) XScrollBar_setValue(bar, 0);
        iv_pumpFor(50);
        v0 = bar ? XScrollBar_value(bar) : -1;
        iv_sendWheel(e.client, vxp, vyp, 120);
        iv_pumpFor(300);
        v1 = bar ? XScrollBar_value(bar) : -1;
        d = (v0 >= 0 && v1 >= 0) ? v1 - v0 : -999;
        {
            char msg[160];
            snprintf(msg, sizeof(msg),
                     "barCenter=(%d,%d) view=(%d,%d)->remote=(%d,%d) "
                     "delta=%d (v0=%d v1=%d)",
                     bcx, bcy, vxp, vyp,
                     iv_viewToRemote(sc, ox, vxp),
                     iv_viewToRemote(sc, oy, vyp), d, v0, v1);
            iv_expect_msg(d > 0, "T5 滚轮全链值增(逆映射命中滚条)", msg);
        }
    }

    /* ---- T6: 服务端注入多顶层命中(重叠弹层按钮可达) ---- */
    {
        XRect wg;
        XWindow* win = XWidget_windowHandle(e.top);
        wg = XWindow_geometry(win);
        e.popup = XWidget_create(NULL, 0); /* 后登记=更上层(topLevelAt)。 */
        XWidget_resize(e.popup, 200, 150);
        XWidget_move(e.popup, wg.x + 700, wg.y + 100); /* 镜像局部(700,100)。 */
        e.popupBtn = XCheckBox_create(e.popup, 0);
        XWidget_setGeometry((XWidget*)e.popupBtn, 60, 60, 16, 16);
        XWidget_show((XWidget*)e.popupBtn);
        XObject_connect_2((XObject*)e.popupBtn,
                          XSignal(XAbstractButton_clicked_signal), iv_onPopup);
        XWidget_show(e.popup);
        iv_pumpFor(100);
        /* 镜像局部(760,160)=弹层按钮中心 → 视图 (475,122):
         * 760*0.625=475, 160*0.625=100, +22=122。 */
        iv_clickAndAwait(&e, 475, 122, 760, 160, &e.popupHits, -1,
                         "T6 弹层按钮 view(475,122)→镜像(760,160) 命中");
        iv_expect(e.tgtHits[0] + e.tgtHits[1] + e.tgtHits[2] + e.tgtHits[3] ==
                      4,
                  "T6 弹层命中不串扰镜像树目标");
        XClassDelete(e.popup);
        e.popup = NULL;
        iv_pumpFor(50);
    }

    /* ---- T7: FIT↔1:1 往返 ---- */
    XGuiClient_setViewFitMode(e.client, XGUI_CLIENT_VIEW_1TO1);
    iv_expect(XGuiClient_viewFitMode(e.client) == XGUI_CLIENT_VIEW_1TO1,
              "T7 切 1:1 模式读回");
    iv_expect(XWidget_width((XWidget*)e.client) == IV_TOP_W &&
              XWidget_height((XWidget*)e.client) == IV_TOP_H,
              "T7 1:1 恢复 setFixedSize(1024x600) 原行为");
    {
        float sc = 0.0f;
        int ox = -1, oy = -1;
        XImage* snap;
        XGuiClient_viewTransform(e.client, &sc, &ox, &oy);
        iv_expect(sc == 1.0f && ox == 0 && oy == 0,
                  "T7 1:1 变换 = 恒等");
        iv_pumpFor(150);
        snap = XWidget_grab((XWidget*)e.client);
        iv_expect(snap != NULL, "T7 1:1 抓取就绪");
        if (snap) {
            iv_expect(iv_pixel(snap, 32, 32) == 0xFFD93025u,
                      "T7 1:1 像素级: 左上标记(32,32)=红");
            XClassDelete(snap);
        }
    }
    XGuiClient_setViewFitMode(e.client, XGUI_CLIENT_VIEW_FIT);
    iv_expect(XWidget_width((XWidget*)e.client) == IV_VIEW_W &&
              XWidget_height((XWidget*)e.client) == IV_VIEW_H,
              "T7 切回 FIT 控件缩回宿主 640x420");
    iv_assertFitTransform(&e, "T7 切回 FIT 变换复原");
    iv_pumpFor(150);
    {
        XImage* snap = XWidget_grab((XWidget*)e.client);
        iv_expect(snap != NULL, "T7 FIT 抓取就绪");
        if (snap) {
            /* 旧帧残留判定: 信箱黑边回黑 + 标记色块回位。1:1 阶段
             * (320,8) 曾是画面内容, FIT 下必须回黑边。 */
            iv_expect(iv_pixel(snap, 320, 8) == 0xFF000000u,
                      "T7 无旧帧残留: (320,8) 回黑边");
            iv_expect(iv_pixel(snap, 320, 410) == 0xFF000000u,
                      "T7 无旧帧残留: (320,410) 回黑边");
            iv_expect(iv_pixel(snap, 20, 42) == 0xFFD93025u,
                      "T7 标记回位: view(20,42)=左上红");
            XClassDelete(snap);
        }
    }
    /* 切换后会话健康 + 点击链仍通(T3 目标 0 复点)。 */
    {
        int64_t deadline;
        e.tgtHits[0] = 0;
        iv_sendMouse(e.client, XEVENT_TYPE_MOUSE_BUTTON_PRESS,
                     XMouseButton_LeftButton, XMouseButton_LeftButton, 62, 84);
        iv_sendMouse(e.client, XEVENT_TYPE_MOUSE_BUTTON_RELEASE,
                     XMouseButton_LeftButton, XMouseButton_NoButton, 62, 84);
        deadline = iv_nowMs() + 5000;
        while (iv_nowMs() < deadline) {
            XGuiApplication_processEvents(XEventLoop_AllEvents);
            XThread_msleep(2);
            if (e.tgtHits[0] > 0) break;
        }
        iv_expect(e.tgtHits[0] == 1, "T7 切换后点击逆映射仍命中");
        iv_expect(XGuiClient_state(e.client) == XGUI_REMOTE_STATE_STREAMING,
                  "T7 往返切换后会话保持 Streaming");
    }

    iv_teardown(&e);
    fprintf(stderr, "XGuiRemoteInputViewTest: %d failure(s)\n", iv_failures);
    return iv_failures > 0 ? 1 : 0;
}
