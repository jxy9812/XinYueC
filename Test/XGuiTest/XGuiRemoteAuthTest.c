/**@****************************************************************************
 * @file       XGuiRemoteAuthTest.c
 * @brief      XGuiRemote 访问口令认证矩阵测试(独立可执行, 自带 main)。
 * @details    XGuiRemote.md §3.8 认证语义(2026-10-04 用户裁定)的端到端
 *             矩阵, 在单进程内经两类互联对驱动(Xvfb 离屏顶层):
 *               - 回环对(XGuiRemoteLoopbackDevice): 服务端真会话 +
 *                 XGuiClient 真控件(XGuiRemoteTest.c xr_e2e 同款) +
 *                 裸协议探针(手工 HELLO/CHALLENGE/RESPONSE);
 *               - 127.0.0.1 真 TCP + 进程内 MITM 代理: 全量抓取线上
 *                 字节, 断言口令明文不出现在任何网络负载。
 *             矩阵:
 *               a) 无口令匿名连接 OK(向后兼容基线) + 匿名形态可查;
 *               b) 设口令后: 无口令客户端快速失败 / 错口令被拒
 *                  (服务端 AUTH_FAILED 断链) / 对口令准入(形态=已认证);
 *               c) 运行期 clearAccessPassword 后匿名又能连, 存量已认证
 *                  会话不断; 运行期 setAccessPassword 后存量匿名会话
 *                  不断, 新匿名连接被拒;
 *               d) 挑战 nonce 逐连接不同(两次抓取比对);
 *               e) 认证失败断链上限可配置(默认 1; 设 5 时前 4 次可重答,
 *                  第 5 次断链);
 *               f) 抓包确认口令明文不出现在网络负载(明文也不入本测试
 *                  任何输出)。
 * @note       运行口径: 仓库 bin 目录或任意 DISPLAY 就绪环境; 无显示时
 *             仅 (0) 单元段可跑, 其余段自动跳过并如实标注 NOT-RUN。
 * @author     XinYueC 团队
 ******************************************************************************/
#define _GNU_SOURCE /* memmem(抓包明文扫描; GNU 扩展)。 */
#include "XGuiConfig.h"
#include "XGuiRemoteProto.h" /* XGUI_REMOTE_ON 单一来源(先于 #if 求值)。 */

#if XGUI_REMOTE_ON

#include "XGuiRemoteAuth.h"
#include "XGuiRemoteLoopback.h"
#include "XGuiServer.h"
#include "XGuiClient.h"
#include "XGuiApplication.h"
#include "XWidget.h"
#include "XObject.h"
#include "XPalette.h"
#include "XDateTime.h"
#include "XThread.h"
#include "XMemory.h"
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <poll.h>

/* ==================== 断言与泵工具 ==================== */

static int ta_failures = 0;
static int ta_runs = 0;

static void ta_expect(bool cond, const char* what)
{
    ++ta_runs;
    if (!cond) {
        fprintf(stderr, "[AUTH-FAIL] %s\n", what ? what : "");
        ++ta_failures;
    }
}

/** @brief 墙钟毫秒(等待循环时钟, 同 XGuiRemoteTest 口径)。 */
static int64_t ta_nowMs(void)
{
    return XDateTime_currentMSecsSinceEpoch();
}

/** @brief 泵事件循环 ms 毫秒(服务器/客户端 poll 回调在此推进)。 */
static void ta_pumpFor(int ms)
{
    int64_t deadline = ta_nowMs() + ms;
    while (ta_nowMs() < deadline) {
        XGuiApplication_processEvents(XEventLoop_AllEvents);
        XThread_msleep(2);
    }
    XGuiApplication_processEvents(XEventLoop_AllEvents);
}

/** @brief 条件等待: 泵事件循环直至 cond 达成或超时。 */
static bool ta_waitUntil(bool (*cond)(void*), void* ud, int timeoutMs)
{
    int64_t deadline = ta_nowMs() + timeoutMs;
    while (ta_nowMs() < deadline) {
        XGuiApplication_processEvents(XEventLoop_AllEvents);
        if (cond(ud)) return true;
        XThread_msleep(2);
    }
    XGuiApplication_processEvents(XEventLoop_AllEvents);
    return cond(ud);
}

/** @brief 确保应用单例存在(独立直跑兜底, 同 XGuiRemoteTest 惯例)。 */
static void ta_ensureApp(void)
{
    (void)XGuiApplication_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, 0, NULL);
}

/** @brief 诊断: 环境变量 TA_DEBUG=1 时十六进制转储(临时诊断探针)。 */
static int ta_debugOn(void)
{
    static int on = -1;
    if (on < 0) on = getenv("TA_DEBUG") ? 1 : 0;
    return on;
}
static void ta_hexDump(const char* tag, const uint8_t* p, size_t n)
{
    size_t i;
    if (!ta_debugOn()) return;
    fprintf(stderr, "[AUTH-DBG] %s (%zu B):", tag, n);
    for (i = 0; i < n && i < 64; ++i) fprintf(stderr, " %02x", p[i]);
    fprintf(stderr, "\n");
}

/** @brief 离屏顶层色块(镜像源, 同 XGuiRemoteTest xr_paintColor 口径)。 */
static void ta_paintColor(XWidget* w, int r, int g, int b)
{
    XPalette pal;
    XColor c;
    XPalette_init_default(&pal);
    XColor_init_rgb(&c, r, g, b, 255);
    XPalette_setColor(&pal, XPaletteColorGroup_Active,
                      XPaletteColorRole_Window, c);
    XPalette_setColor(&pal, XPaletteColorGroup_Inactive,
                      XPaletteColorRole_Window, c);
    XWidget_setPalette(w, &pal);
    XWidget_setAutoFillBackground(w, true);
}

/* ==================== 观测拓扑(服务端真会话 + XGuiClient 真控件) ====== */

#define TA_TOPO_MAX_CLI 2

typedef struct TaTopo {
    XWidget* top;
    XGuiServer* server;
    XGuiClient* client[TA_TOPO_MAX_CLI];
    XGuiRemoteLoopbackDevice* devA[TA_TOPO_MAX_CLI];
    XGuiRemoteLoopbackDevice* devB[TA_TOPO_MAX_CLI];
    int sid[TA_TOPO_MAX_CLI];
    /* 观测计数(槽自增)。 */
    int srvConn, srvDisc, srvErr;
    int srvLastErrCode, srvLastDiscReason;
    int cliConn[TA_TOPO_MAX_CLI], cliDisc[TA_TOPO_MAX_CLI];
    int cliLastReason[TA_TOPO_MAX_CLI];
    int attachCount;
} TaTopo;

static TaTopo* ta_cur = NULL;

static void ta_onSrvConnected(XObject* sender, XVarList* args)
{
    (void)sender;
    if (args && ta_cur) {
        XVarList_args_1(args, int, sid);
        (void)sid;
        ++ta_cur->srvConn;
    }
}
static void ta_onSrvDisconnected(XObject* sender, XVarList* args)
{
    (void)sender;
    if (args && ta_cur) {
        XVarList_args_2(args, int, sid, int, reason);
        (void)sid;
        ta_cur->srvLastDiscReason = reason;
        ++ta_cur->srvDisc;
    }
}
static void ta_onSrvError(XObject* sender, XVarList* args)
{
    (void)sender;
    if (args && ta_cur) {
        XVarList_args_2(args, int, sid, int, code);
        (void)sid;
        ta_cur->srvLastErrCode = code;
        ++ta_cur->srvErr;
    }
}
static void ta_onCliConnected(XObject* sender, XVarList* args)
{
    int k;
    (void)args;
    if (!ta_cur) return;
    for (k = 0; k < TA_TOPO_MAX_CLI; ++k)
        if ((XObject*)ta_cur->client[k] == sender) ++ta_cur->cliConn[k];
}
static void ta_onCliDisconnected(XObject* sender, XVarList* args)
{
    int k;
    if (!ta_cur) return;
    for (k = 0; k < TA_TOPO_MAX_CLI; ++k) {
        if ((XObject*)ta_cur->client[k] == sender) {
            if (args) {
                XVarList_args_1(args, int, reason);
                ta_cur->cliLastReason[k] = reason;
            }
            ++ta_cur->cliDisc[k];
            return;
        }
    }
}

static void ta_resetTopo(TaTopo* t)
{
    memset(t, 0, sizeof(*t));
}

/** @brief 拓扑搭建: 离屏顶层 + 服务端(host+attach 回环 A 端) + 可选
 *         客户端(直挂 B 端)。attachCount>0 时搭 client[0]。 */
static bool ta_setup(TaTopo* t, int withClient)
{
    XGuiRemoteLoopbackDevice* devB = NULL;
    ta_resetTopo(t);
    ta_ensureApp();
    ta_cur = t;

    t->top = XWidget_create(NULL, 0);
    if (!t->top) return false;
    XWidget_resize(t->top, 200, 150);
    ta_paintColor(t->top, 0x21, 0x53, 0x9B);
    XWidget_show(t->top);
    XWidget_repaint(t->top);

    t->server = XGuiServer_create(NULL);
    if (!t->server) return false;
    if (!XGuiServer_host(t->server, t->top)) return false;
    XObject_connect_2((XObject*)t->server,
                      XSignal(XGuiServer_clientConnected_signal),
                      ta_onSrvConnected);
    XObject_connect_2((XObject*)t->server,
                      XSignal(XGuiServer_clientDisconnected_signal),
                      ta_onSrvDisconnected);
    XObject_connect_2((XObject*)t->server,
                      XSignal(XGuiServer_sessionError_signal), ta_onSrvError);

    if (withClient) {
        t->devA[0] = XGuiRemoteLoopbackDevice_createPair(256 * 1024, &devB);
        if (!t->devA[0] || !devB) return false;
        t->devB[0] = devB;
        t->client[0] = XGuiClient_create(NULL, 0);
        if (!t->client[0]) return false;
        XObject_connect_2((XObject*)t->client[0],
                          XSignal(XGuiClient_connected_signal),
                          ta_onCliConnected);
        XObject_connect_2((XObject*)t->client[0],
                          XSignal(XGuiClient_disconnected_signal),
                          ta_onCliDisconnected);
        /* 不在此处 attach/setTransport: 回环对的会话接入统一走
         * ta_matrixConnect(每次连接都需新 attach, 见其注)。 */
    }
    return true;
}

/** @brief 排干回环单端残留字节(上一会话在途帧; 设备不随会话关闭)。 */
static void ta_drainDevice(XGuiRemoteLoopbackDevice* d)
{
    uint8_t tmp[512];
    if (!d) return;
    while (XIODevice_read_1((XIODevice*)d, (char*)tmp,
                            (int64_t)sizeof(tmp)) > 0) {
    }
}

/** @brief 回环对的一次接入: 双端排干残留 → 服务端 attach 新会话 →
 *         客户端 setTransport。
 *  @note  回环互联与 TCP 不同: ①服务端会话随断链关闭后, 同一对设备
 *         再次连接前必须重新 attach; ②上一会话的未读在途字节留在环上,
 *         不排干会被新会话当作横幅/帧消费(实锅: 服务端把残留 PING 帧
 *         头当作横幅魔数校验失败 → protocol error)。每次接入产生新
 *         sid, 调用方以 t->sid[k] 读最新形态。 */
static bool ta_matrixConnect(TaTopo* t, int k)
{
    if (!t->server || !t->devA[k] || !t->client[k]) return false;
    ta_drainDevice(t->devA[k]);
    ta_drainDevice(t->devB[k]);
    t->sid[k] = XGuiServer_attachTransport(t->server,
                                           (XIODevice*)t->devA[k]);
    if (t->sid[k] <= 0) return false;
    XGuiClient_setTransport(t->client[k], (XIODevice*)t->devB[k]);
    return true;
}

/** @brief 追加第 2 个客户端(运行期多会话场景; 匿名/带口令/何时接入
 *         全由调用方设——此处不得 setTransport, 否则第二会话在基线期
 *         就接入并污染会话计数基线)。 */
static bool ta_setupSecondClient(TaTopo* t)
{
    XGuiRemoteLoopbackDevice* devB = NULL;
    ta_cur = t;
    t->devA[1] = XGuiRemoteLoopbackDevice_createPair(256 * 1024, &devB);
    if (!t->devA[1] || !devB) return false;
    t->devB[1] = devB;
    t->client[1] = XGuiClient_create(NULL, 0);
    if (!t->client[1]) return false;
    XObject_connect_2((XObject*)t->client[1],
                      XSignal(XGuiClient_connected_signal),
                      ta_onCliConnected);
    XObject_connect_2((XObject*)t->client[1],
                      XSignal(XGuiClient_disconnected_signal),
                      ta_onCliDisconnected);
    return true;
}

static void ta_teardown(TaTopo* t)
{
    int i;
    ta_cur = NULL;
    for (i = 0; i < TA_TOPO_MAX_CLI; ++i) {
        if (t->client[i]) XClassDelete(t->client[i]);
    }
    if (t->server) XClassDelete(t->server);
    for (i = 0; i < TA_TOPO_MAX_CLI; ++i) {
        if (t->devA[i]) XClassDelete((XClass*)t->devA[i]);
        if (t->devB[i]) XClassDelete((XClass*)t->devB[i]);
    }
    if (t->top) XClassDelete(t->top);
    ta_resetTopo(t);
    /* 删除后受控清泵(仅此一圈, 严禁长泵): 删除前已 posted 的定时器事件
     * 在队首, 此刻对象仍存活但已停(定时器 id 失配被丢弃); 随后
     * deleteLater 定版释放。跳过该圈会把"停表残留事件 + 延迟删除"拖进
     * 下一拓扑的泵圈, 与新定时器同址复用时残事件命中已释放服务端
     * (实锅: xgs_pumpCallback 踏入已释放私有块)。 */
    XGuiApplication_processEvents(XEventLoop_AllEvents);
    XThread_msleep(10);
    XGuiApplication_processEvents(XEventLoop_AllEvents);
}

/* ---- 等待条件 ---- */
/* [死码清理] ta_condCliConn0 已删除：全仓无调用点（见审计清单）。
 */
/* [死码清理] ta_condCliDisc0 已删除：全仓无调用点（见审计清单）。
 */
/* [死码清理] ta_condSrvErr 已删除：全仓无调用点（见审计清单）。
 */

/* ==================== 裸协议探针(回环 B 端手工说话) ====================
 *  @note 服务端跨连接复用(同会话矩阵定式): 每连接只轮换回环对与
 *        attach/detach, 服务端与镜像顶层全程存活。 */

typedef struct TaRaw {
    XWidget* top;
    XGuiServer* server;
    XGuiRemoteLoopbackDevice* devA;
    XGuiRemoteLoopbackDevice* devB;
    XGuiRemoteFrameReader reader;
    int sid;
    bool bannerSent; /**< 本端(C 侧)横幅已发(§3.3: 双向横幅)。 */
    bool connected;  /**< 当前有挂接的探针会话。 */
} TaRaw;

/** @brief 搭裸探针服务端(一次; 口令由调用方设, 会话按需 attach)。 */
static bool ta_rawOpenOnce(TaRaw* r, const char* password)
{
    memset(r, 0, sizeof(*r));
    ta_ensureApp();
    r->top = XWidget_create(NULL, 0);
    if (!r->top) return false;
    XWidget_resize(r->top, 200, 150);
    ta_paintColor(r->top, 0x33, 0x33, 0x33);
    XWidget_show(r->top);
    XWidget_repaint(r->top);
    r->server = XGuiServer_create(NULL);
    if (!r->server) return false;
    if (!XGuiServer_host(r->server, r->top)) return false;
    if (password && !XGuiServer_setAccessPassword(r->server, password))
        return false;
    return true;
}

/** @brief 发起一条探针连接: 新回环对 attach + 横幅消费。 */
static bool ta_rawConnect(TaRaw* r)
{
    XGuiRemoteLoopbackDevice* devB = NULL;
    uint8_t banner[XGUI_REMOTE_BANNER_BYTES];
    int64_t n;
    if (r->connected) return false;
    r->devA = XGuiRemoteLoopbackDevice_createPair(256 * 1024, &devB);
    if (!r->devA || !devB) return false;
    r->devB = devB;
    r->sid = XGuiServer_attachTransport(r->server, (XIODevice*)r->devA);
    if (r->sid <= 0) return false;
    r->connected = true;
    r->bannerSent = false; /* 新连接须重发本端横幅(§3.3 双向横幅)。 */
    XGuiRemoteFrameReader_init(&r->reader);
    /* 消费 attach 即发的 8 字节横幅(轮询泵至凑齐)。 */
    {
        int64_t got = 0;
        int64_t deadline = ta_nowMs() + 3000;
        while (got < XGUI_REMOTE_BANNER_BYTES && ta_nowMs() < deadline) {
            n = XIODevice_read_1((XIODevice*)r->devB,
                                 (char*)banner + got,
                                 (int64_t)(XGUI_REMOTE_BANNER_BYTES - got));
            if (n > 0) got += n;
            else ta_pumpFor(5);
        }
        if (got < XGUI_REMOTE_BANNER_BYTES) return false;
        if (!XGuiRemoteProto_bannerIsValid(banner)) return false;
        ta_hexDump("raw banner", banner, XGUI_REMOTE_BANNER_BYTES);
    }
    return true;
}

/** @brief 结束当前探针连接(detach 会话 + 销毁回环对; 服务端存活)。 */
static void ta_rawDisconnect(TaRaw* r)
{
    if (!r->connected) return;
    XGuiRemoteFrameReader_deinit(&r->reader);
    if (r->sid > 0 && XGuiServer_sessionCount(r->server) > 0) {
        ta_pumpFor(60); /* 让 BYE/断链落定, 防对活跃会话 detach。 */
        if (XGuiServer_sessionCount(r->server) > 0)
            XGuiServer_detachTransport(r->server, r->sid);
    }
    r->sid = 0;
    r->connected = false;
    ta_pumpFor(60); /* detach/BYE 收尾落定(服务端存活, 泵圈安全)。 */
    if (r->devA) XClassDelete((XClass*)r->devA);
    if (r->devB) XClassDelete((XClass*)r->devB);
    r->devA = NULL;
    r->devB = NULL;
}

/** @brief 收尾: 断开探针连接并删除裸服务端(全程仅此一次删除)。 */
static void ta_rawFinish(TaRaw* r)
{
    ta_rawDisconnect(r);
    if (r->server) XClassDelete(r->server);
    if (r->top) XClassDelete(r->top);
    memset(r, 0, sizeof(*r));
    /* 删除后禁止泵圈(防 posted 事件打悬垂对象)。 */
}

/** @brief 从 B 端读一帧(泵驱动; 超时返回 false)。
 *  @note  交付纪律: feed 返回 1 时必须立刻交付该帧——不得回圈再喂
 *         (NULL,0), 否则会把同批粘包的下一帧抢先交付(顺序错乱)。 */
static bool ta_rawReadFrame(TaRaw* r, XGuiRemoteMsgType* type,
                            uint8_t* payload, size_t cap, size_t* lenOut,
                            int timeoutMs)
{
    uint8_t buf[2048];
    int64_t deadline = ta_nowMs() + timeoutMs;
    while (ta_nowMs() < deadline) {
        int rc;
        /* 先探询已入队帧(上批粘包余量)。 */
        rc = XGuiRemoteFrameReader_feed(&r->reader, NULL, 0);
        if (rc > 0) {
            size_t len = r->reader.payloadLen;
            if (len > cap) len = cap;
            memcpy(payload, r->reader.payload, len);
            *type = r->reader.type;
            if (lenOut) *lenOut = len;
            return true;
        }
        if (rc < 0) return false;
        {
            int64_t n = XIODevice_read_1((XIODevice*)r->devB, (char*)buf,
                                         (int64_t)sizeof(buf));
            if (n > 0) {
                ta_hexDump("raw rx", buf, (size_t)n);
                rc = XGuiRemoteFrameReader_feed(&r->reader, buf, (size_t)n);
                if (ta_debugOn())
                    fprintf(stderr, "[AUTH-DBG] feed(n=%lld) rc=%d\n",
                            (long long)n, rc);
                if (rc > 0) {
                    size_t len = r->reader.payloadLen;
                    if (len > cap) len = cap;
                    memcpy(payload, r->reader.payload, len);
                    *type = r->reader.type;
                    if (lenOut) *lenOut = len;
                    return true;
                }
                if (rc < 0) return false;
            }
        }
        XGuiApplication_processEvents(XEventLoop_AllEvents);
        XThread_msleep(2);
    }
    return false;
}

/** @brief 向 A 端(服务端)写一整帧。 */
static bool ta_rawWriteFrame(TaRaw* r, XGuiRemoteMsgType type,
                             const uint8_t* payload, size_t payloadBytes)
{
    uint8_t frame[512];
    size_t total = XGUI_REMOTE_FRAME_HEADER_BYTES + payloadBytes;
    int64_t accepted;
    if (total > sizeof(frame)) return false;
    frame[0] = (uint8_t)(payloadBytes & 0xFF);
    frame[1] = (uint8_t)((payloadBytes >> 8) & 0xFF);
    frame[2] = (uint8_t)((payloadBytes >> 16) & 0xFF);
    frame[3] = (uint8_t)((payloadBytes >> 24) & 0xFF);
    frame[4] = (uint8_t)type;
    if (payloadBytes) memcpy(frame + XGUI_REMOTE_FRAME_HEADER_BYTES,
                             payload, payloadBytes);
    accepted = XIODevice_write_1((XIODevice*)r->devB, (const char*)frame,
                                 (int64_t)total);
    return accepted == (int64_t)total;
}

/** @brief 发 HELLO(先补 §3.3 本端横幅; 建议匿名, 名 raw-probe)。 */
static bool ta_rawSendHello(TaRaw* r)
{
    XGuiRemoteMsgHello hello;
    uint8_t buf[XGUI_REMOTE_MAX_NAME_BYTES + 16];
    uint8_t banner[XGUI_REMOTE_BANNER_BYTES];
    size_t n;
    const char* name = "raw-probe";
    if (!r->bannerSent) {
        /* 服务端先攒 8B 客户端横幅再进 HANDSHAKING(XGuiServer.c 读侧);
         * 漏发横幅=魔数校验失败=protocol error(实锅修正)。 */
        XGuiRemoteProto_makeBanner(banner);
        if (XIODevice_write_1((XIODevice*)r->devB, (const char*)banner,
                              XGUI_REMOTE_BANNER_BYTES) !=
            (int64_t)XGUI_REMOTE_BANNER_BYTES) {
            return false;
        }
        r->bannerSent = true;
    }
    memset(&hello, 0, sizeof(hello));
    hello.protocolVersion = XGUI_REMOTE_PROTOCOL_VERSION;
    hello.capabilities = 0;
    hello.authMethod = (uint8_t)XGUI_REMOTE_AUTH_NONE;
    hello.nameBytes = (uint16_t)strlen(name);
    memcpy(hello.name, name, strlen(name));
    n = XGuiRemoteProto_encHello(buf, sizeof(buf), &hello);
    if (n == 0) return false;
    ta_hexDump("raw tx hello-payload", buf, n);
    return ta_rawWriteFrame(r, XGUI_REMOTE_MSG_HELLO, buf, n);
}

/** @brief 读到指定类型帧为止(跳过 PING/PONG 等伴生帧; 超时 false)。 */
static bool ta_rawReadTyped(TaRaw* r, XGuiRemoteMsgType want,
                            uint8_t* payload, size_t cap, size_t* lenOut,
                            int timeoutMs)
{
    XGuiRemoteMsgType type;
    int64_t deadline = ta_nowMs() + timeoutMs;
    for (;;) {
        int left = (int)(deadline - ta_nowMs());
        if (left <= 0) return false;
        if (!ta_rawReadFrame(r, &type, payload, cap, lenOut, left))
            return false;
        if (type == want) return true;
        /* PING/PONG/FB_* 等帧与本断言无关, 跳过续读。 */
    }
}

/** @brief 完成 HELLO→HELLO_ACK→AUTH_CHALLENGE 读取, 出参 nonce(32B)。 */
static bool ta_rawGetChallenge(TaRaw* r, uint8_t* nonce, size_t nonceCap,
                               int* nonceBytesOut)
{
    uint8_t payload[512];
    size_t len = 0;
    XGuiRemoteMsgAuthChallenge ch;
    if (!ta_rawReadTyped(r, XGUI_REMOTE_MSG_HELLO_ACK, payload,
                         sizeof(payload), &len, 3000))
        return false;
    if (!ta_rawReadTyped(r, XGUI_REMOTE_MSG_AUTH_CHALLENGE, payload,
                         sizeof(payload), &len, 3000))
        return false;
    if (!XGuiRemoteProto_decAuthChallenge(payload, len, &ch)) return false;
    if (ch.nonceBytes > nonceCap) return false;
    memcpy(nonce, ch.nonce, ch.nonceBytes);
    if (nonceBytesOut) *nonceBytesOut = ch.nonceBytes;
    return true;
}

/** @brief 读 AUTH_RESULT, 出参 ok。 */
static bool ta_rawReadAuthResult(TaRaw* r, int* okOut, int timeoutMs)
{
    uint8_t payload[512];
    size_t len = 0;
    XGuiRemoteMsgAuthResult res;
    if (!ta_rawReadTyped(r, XGUI_REMOTE_MSG_AUTH_RESULT, payload,
                         sizeof(payload), &len, timeoutMs))
        return false;
    if (!XGuiRemoteProto_decAuthResult(payload, len, &res)) return false;
    if (okOut) *okOut = res.ok;
    return true;
}

/* ==================== (0) 单元: 存储/掩码/上限/公式对称 ==================== */

static void ta_unitTests(void)
{
    XGuiRemoteAuthServerStore store;
    uint8_t nonce[32];
    uint8_t resp[XGUI_REMOTE_AUTH_RESPONSE_BYTES];
    char masked[32];
    int fails = 0;
    int i;

    fprintf(stderr, "[AUTH-TEST] (0) 单元: 存储/掩码/失败上限/公式对称\n");

    XGuiRemoteAuth_storeInit(&store);
    ta_expect(!store.hasPassword &&
              store.authMethod == XGUI_REMOTE_AUTH_NONE &&
              store.failureLimit == 1,
              "(0) storeInit 默认匿名+失败上限 1");

    ta_expect(XGuiRemoteAuth_serverSetAccessPassword(&store, "unit-pw-01") &&
              store.hasPassword &&
              store.authMethod == XGUI_REMOTE_AUTH_SHA256_CHALLENGE,
              "(0) setAccessPassword 设口令即启用挑战");

    ta_expect(XGuiRemoteAuth_serverAccessPassword(&store, masked,
                                                  sizeof(masked)) &&
              strcmp(masked, "********") == 0,
              "(0) 掩码查询=已设+定长掩码");
    ta_expect(XGuiRemoteAuth_serverAccessPassword(&store, masked, 4) &&
              strlen(masked) == 3,
              "(0) 小容量掩码安全截断");
    ta_expect(XGuiRemoteAuth_serverAccessPassword(&store, NULL, 0),
              "(0) NULL 缓冲仅查询布尔(已设=true)");

    /* 公式对称: 客户端计算 == 服务端同式重算认可。 */
    for (i = 0; i < 32; ++i) nonce[i] = (uint8_t)(0xA0 + i);
    ta_expect(XGuiRemoteAuth_clientComputeResponse("unit-pw-01", nonce, 32,
                                                   resp),
              "(0) 客户端应答计算成功");
    ta_expect(XGuiRemoteAuth_serverVerifyResponse(&store, nonce, 32, resp,
                                                  sizeof(resp),
                                                  &fails) ==
                  XGUI_REMOTE_AUTH_VERDICT_OK && fails == 0,
              "(0) 服务端同式重算认可客户端应答");

    /* 错应答: 默认上限 1 → REJECT。 */
    fails = 0;
    resp[0] ^= 0xFF; /* 翻转 → 必错应答(此后保持错样, 勿再翻转)。 */
    ta_expect(XGuiRemoteAuth_serverVerifyResponse(&store, nonce, 32, resp,
                                                  sizeof(resp),
                                                  &fails) ==
                  XGUI_REMOTE_AUTH_VERDICT_REJECT && fails == 1,
              "(0) 错应答默认上限 1 → REJECT 且计数+1");

    /* 上限配置: 5 → 前错 4 次 RETRY, 第 5 次 REJECT。 */
    XGuiRemoteAuth_serverSetFailureLimit(&store, 5);
    ta_expect(XGuiRemoteAuth_serverFailureLimit(&store) == 5,
              "(0) 失败上限可配置");
    fails = 0;
    {
        XGuiRemoteAuthVerdict v = XGUI_REMOTE_AUTH_VERDICT_OK;
        int k;
        for (k = 0; k < 4; ++k)
            v = XGuiRemoteAuth_serverVerifyResponse(&store, nonce, 32, resp,
                                                    sizeof(resp), &fails);
        ta_expect(v == XGUI_REMOTE_AUTH_VERDICT_RETRY && fails == 4,
                  "(0) 上限 5 时前 4 次错应答 RETRY");
        v = XGuiRemoteAuth_serverVerifyResponse(&store, nonce, 32, resp,
                                                sizeof(resp), &fails);
        ta_expect(v == XGUI_REMOTE_AUTH_VERDICT_REJECT && fails == 5,
                  "(0) 第 5 次错应答 REJECT");
    }
    /* 上限归一: 0/负 → 1。 */
    {
        XGuiRemoteAuthServerStore s2;
        XGuiRemoteAuth_storeInit(&s2);
        XGuiRemoteAuth_serverSetFailureLimit(&s2, 0);
        ta_expect(XGuiRemoteAuth_serverFailureLimit(&s2) == 1,
                  "(0) 上限 0 归一为 1");
        XGuiRemoteAuth_serverSetFailureLimit(&s2, -7);
        ta_expect(XGuiRemoteAuth_serverFailureLimit(&s2) == 1,
                  "(0) 上限负值归一为 1");
    }

    /* 清口令回匿名; 空串等价清除。 */
    XGuiRemoteAuth_serverClearAccessPassword(&store);
    ta_expect(!store.hasPassword &&
              store.authMethod == XGUI_REMOTE_AUTH_NONE,
              "(0) clearAccessPassword 清口令回匿名");
    ta_expect(XGuiRemoteAuth_serverSetAccessPassword(&store, "tmp") &&
              XGuiRemoteAuth_serverSetAccessPassword(&store, "") &&
              !store.hasPassword &&
              store.authMethod == XGUI_REMOTE_AUTH_NONE,
              "(0) 空串 setAccessPassword 等价清除");

    /* 遗留 API 语义不变: setPassword 不动方法旋钮。 */
    ta_expect(XGuiRemoteAuth_storeSetPassword(&store, "legacy") &&
              store.authMethod == XGUI_REMOTE_AUTH_NONE,
              "(0) 遗留 setPassword 不动方法旋钮");
}

/* ==================== 会话矩阵(单服务端复用; 2026-10-04 定式) =========
 *  @note 服务端对象生命周期内反复 create/destroy 会在"停表残留事件 +
 *        延迟删除"与新定时器同址复用间踩踏已释放私有块(实锅定位:
 *        xgs_pumpCallback 踏入已释放 d 块)。矩阵改为单服务端贯穿
 *        a/b/c/f(口令运行期切换正是被测语义), raw 探针单服务端贯穿
 *        d/e(会话以 attach/detach 轮换); 删除收敛到收尾一次性执行,
 *        删除后不再泵圈。 */
/* 矩阵口令(严禁打印/入日志)。 */
static const char* TA_PW = "S3cret-wf6x-Pw";

typedef struct TaMatrix {
    TaTopo t;
    bool up;
} TaMatrix;

/* ---- 按客户端下标的等待条件(引用块; 严禁 int 截断指针) ---- */
typedef struct TaCliRef {
    TaTopo* t;
    int k;
    int connBase; /* 等待基线(相位起点计数), 条件=当前值>基线。 */
    int discBase;
} TaCliRef;
static bool ta_condCliConnK(void* ud)
{
    TaCliRef* ref = (TaCliRef*)ud;
    return ref->t->cliConn[ref->k] > ref->connBase;
}
static bool ta_condCliDiscK(void* ud)
{
    TaCliRef* ref = (TaCliRef*)ud;
    return ref->t->cliDisc[ref->k] > ref->discBase;
}

/** @brief 断开一个客户端并等其 disconnected 落定(显式断链, 无重连)。 */
static void ta_disconnectClient(TaTopo* t, int k)
{
    TaCliRef ref;
    if (!t->client[k]) return;
    if (XGuiClient_state(t->client[k]) == XGUI_REMOTE_STATE_DISCONNECTED)
        return;
    ref.t = t;
    ref.k = k;
    ref.connBase = t->cliConn[k];
    ref.discBase = t->cliDisc[k];
    XGuiClient_disconnectFromServer(t->client[k]);
    ta_waitUntil(ta_condCliDiscK, &ref, 3000);
}

/** @brief (a) 匿名连接基线(客户端 0)。 */
static void ta_mAnonymous(TaMatrix* m)
{
    TaTopo* t = &m->t;
    TaCliRef ref;
    fprintf(stderr, "[AUTH-TEST] (a) 无口令匿名连接(向后兼容基线)\n");
    ref.t = t;
    ref.k = 0;
    ref.connBase = 0;
    ref.discBase = 0;
    if (!ta_matrixConnect(t, 0)) {
        ta_expect(false, "(a) 回环接入");
        return;
    }
    ta_waitUntil(ta_condCliConnK, &ref, 3000);
    ta_expect(t->cliConn[0] == 1, "(a) 匿名客户端接入(connected 信号)");
    ta_expect(XGuiClient_isRemoteAlive(t->client[0]),
              "(a) 匿名会话 Streaming(FB_META 已至)");
    ta_expect(XGuiServer_sessionCount(t->server) == 1, "(a) 服务端会话数 1");
    ta_expect(t->srvConn == 1, "(a) 服务端 connected 信号 1 次");
    ta_expect(XGuiServer_sessionAuthState(t->server, t->sid[0]) == 0,
              "(a) 会话认证形态=匿名(0)");
    ta_expect(XGuiServer_sessionAuthState(t->server, 999999) == -1,
              "(a) 不存在会话查询=-1");
    ta_disconnectClient(t, 0);
    ta_expect(XGuiServer_sessionCount(t->server) == 0,
              "(a) 显式断链后会话回收");
}

/** @brief (b) 设口令后: 无口令快速失败/错口令拒绝/对口令准入。 */
static void ta_mPassword(TaMatrix* m)
{
    TaTopo* t = &m->t;
    int pack[2];

    TaCliRef ref;
    int disc0 = t->cliDisc[0];
    int conn0 = t->cliConn[0];
    int err0 = t->srvErr;
    fprintf(stderr, "[AUTH-TEST] (b) 设口令后: 无口令/错口令/对口令\n");
    ref.t = t;
    ref.k = 0;
    ref.connBase = conn0;
    ref.discBase = disc0;


    /* b1: 无口令客户端 → 快速失败(BYE/AUTH_FAILED), 不发必错应答。 */
    ta_expect(XGuiServer_setAccessPassword(t->server, TA_PW),
              "(b) 运行期设口令");
    ta_expect(XGuiServer_accessPassword(t->server, NULL, 0),
              "(b) 设口令后掩码查询=已设");
    if (!ta_matrixConnect(t, 0)) {
        ta_expect(false, "(b1) 回环接入");
        return;
    }
    ta_waitUntil(ta_condCliDiscK, &ref, 3000);
    ta_expect(t->cliDisc[0] == disc0 + 1 &&
              t->cliLastReason[0] == (int)XGUI_REMOTE_BYE_AUTH_FAILED,
              "(b1) 无口令客户端被拒(AUTH_FAILED)");
    ta_expect(t->cliConn[0] == conn0, "(b1) 无口令客户端未达成连接");
    ta_pumpFor(120);
    ta_expect(XGuiServer_sessionCount(t->server) == 0,
              "(b1) 服务端会话回收");

    /* b2: 错口令客户端 → 服务端 RESULT(0)+BYE(AUTH_FAILED)+ERR。 */
    ref.discBase = disc0 + 1;
    XGuiClient_setAccessPassword(t->client[0], "totally-wrong-pw");
    if (!ta_matrixConnect(t, 0)) {
        ta_expect(false, "(b2) 回环接入");
        return;
    }
    ta_waitUntil(ta_condCliDiscK, &ref, 3000);
    ta_expect(t->cliDisc[0] == disc0 + 2 &&
              t->cliLastReason[0] == (int)XGUI_REMOTE_BYE_AUTH_FAILED,
              "(b2) 错口令客户端被拒(AUTH_FAILED)");
    {
        int64_t deadline = ta_nowMs() + 1000;
        while (t->srvErr == err0 && ta_nowMs() < deadline)
            ta_pumpFor(10);
    }
    ta_expect(t->srvErr == err0 + 1 &&
              t->srvLastErrCode == (int)XGUI_REMOTE_ERR_AUTH,
              "(b2) 服务端 sessionError(ERR_AUTH)");
    ta_pumpFor(120);
    ta_expect(XGuiServer_sessionCount(t->server) == 0,
              "(b2) 服务端会话回收");

    /* b3: 对口令客户端 → 准入, 会话形态=已认证(1)。 */
    XGuiClient_setAccessPassword(t->client[0], TA_PW);
    if (!ta_matrixConnect(t, 0)) {
        ta_expect(false, "(b3) 回环接入");
        return;
    }
    ta_waitUntil(ta_condCliConnK, &ref, 3000);
    ta_expect(t->cliConn[0] == conn0 + 1, "(b3) 对口令客户端接入");
    ta_expect(XGuiClient_isRemoteAlive(t->client[0]),
              "(b3) 会话 Streaming");
    ta_expect(XGuiServer_sessionAuthState(t->server, t->sid[0]) == 1,
              "(b3) 会话认证形态=已认证(1)");
}

/** @brief (c) 运行期清/设口令: 存量会话不断, 新会话按当前策略。 */
static void ta_mRuntimeSetClear(TaMatrix* m)
{
    TaTopo* t = &m->t;
    TaCliRef ref;

    int disc0Base = t->cliDisc[0];
    fprintf(stderr, "[AUTH-TEST] (c) 运行期设/清口令: 存量会话不断\n");

    /* c2 前半: 已认证会话在册 → 运行期清口令 → 匿名第二会话接入。 */
    {
        int sidAuth = t->sid[0];
        ref.t = t;
        ref.k = 1;
        ref.connBase = t->cliConn[1];
        ref.discBase = t->cliDisc[1];
        XGuiServer_clearAccessPassword(t->server);
        ta_expect(!XGuiServer_accessPassword(t->server, NULL, 0) &&
                  XGuiServer_authMethod(t->server) ==
                      XGUI_REMOTE_AUTH_NONE,
                  "(c2) 运行期清口令回匿名模式");
        if (!ta_matrixConnect(t, 1)) {
            ta_expect(false, "(c2) 回环接入(客户端 1)");
            return;
        }
        ta_waitUntil(ta_condCliConnK, &ref, 3000);
        ta_expect(t->cliConn[1] == 1, "(c2) 清口令后匿名第二会话接入");
        ta_expect(XGuiServer_sessionCount(t->server) == 2,
                  "(c2) 存量已认证+新匿名两会话并存");
        ta_expect(t->cliDisc[0] == disc0Base &&
                  XGuiClient_isRemoteAlive(t->client[0]),
                  "(c2) 存量已认证会话不断");
        ta_expect(XGuiServer_sessionAuthState(t->server, sidAuth) == 1,
                  "(c2) 存量会话形态仍=已认证");
        ta_expect(XGuiServer_sessionAuthState(t->server, t->sid[1]) == 0,
                  "(c2) 新会话形态=匿名");
    }

    /* c1 后半: 再设口令 → 两个存量会话(已认证+匿名)都不断; 新匿名被拒。 */
    ref.k = 1;
    ta_expect(XGuiServer_setAccessPassword(t->server, TA_PW),
              "(c1) 再设口令(存量两会话在册)");
    ta_pumpFor(300);
    ta_expect(t->cliDisc[0] == disc0Base &&
              XGuiClient_isRemoteAlive(t->client[0]) &&
              XGuiClient_isRemoteAlive(t->client[1]),
              "(c1) 设口令后两个存量会话均不断");
    ta_expect(XGuiServer_sessionAuthState(t->server, t->sid[1]) == 0,
              "(c1) 存量匿名会话形态仍=匿名(接入时结论)");
    {
        int disc1 = t->cliDisc[1];
        ta_disconnectClient(t, 1);
        ref.discBase = disc1 + 1; /* 显式断链已落账, 等待下一次断链。 */
        ta_pumpFor(120);
        XGuiClient_clearAccessPassword(t->client[1]);
        if (!ta_matrixConnect(t, 1)) {
            ta_expect(false, "(c1) 回环接入(客户端 1)");
            return;
        }
        ta_waitUntil(ta_condCliDiscK, &ref, 3000);
        ta_expect(t->cliDisc[1] == disc1 + 2 &&
                  t->cliLastReason[1] == (int)XGUI_REMOTE_BYE_AUTH_FAILED,
                  "(c1) 清+设循环后新匿名连接仍被拒");
    }
}

/** @brief (f) 复用矩阵服务端: 127.0.0.1 真互联 + MITM 抓包(明文不过网)。
 *  @note 函数体与单服版一致, 仅拓扑来源改为 TaMatrix(见 ta_wireCaptureOn)。 */
/* ==================== (d) nonce 逐连接随机 + 裸协议拒绝/准入 ========= */

static void ta_nonceAndRawProbe(TaRaw* r)
{
    uint8_t nonce1[32], nonce2[32];
    int nb1 = 0, nb2 = 0;
    uint8_t good[XGUI_REMOTE_AUTH_RESPONSE_BYTES];
    int ok = -1;

    fprintf(stderr, "[AUTH-TEST] (d) nonce 逐连接不同 + 裸协议拒/准\n");

    /* 连接 1: 取 nonce1, 发必错应答 → RESULT(0)+BYE(AUTH_FAILED)。 */
    if (!ta_rawConnect(r)) {
        ta_expect(false, "(d) 探针连接 1");
        ta_rawDisconnect(r);
        return;
    }
    ta_expect(ta_rawSendHello(r), "(d) HELLO 发送");
    ta_expect(ta_rawGetChallenge(r, nonce1, sizeof(nonce1), &nb1),
              "(d) 挑战读取(nonce1)");
    ta_expect(nb1 == XGUI_REMOTE_AUTH_NONCE_BYTES, "(d) nonce 32 字节");
    {
        uint8_t garbage[XGUI_REMOTE_AUTH_RESPONSE_BYTES];
        XGuiRemoteMsgAuthResponse resp;
        uint8_t buf[XGUI_REMOTE_AUTH_RESPONSE_BYTES + 8];
        size_t n;
        memset(garbage, 0x5A, sizeof(garbage));
        memset(&resp, 0, sizeof(resp));
        memcpy(resp.response, garbage, sizeof(garbage));
        resp.responseBytes = XGUI_REMOTE_AUTH_RESPONSE_BYTES;
        n = XGuiRemoteProto_encAuthResponse(buf, sizeof(buf), &resp);
        ta_expect(n > 0 &&
                  ta_rawWriteFrame(r, XGUI_REMOTE_MSG_AUTH_RESPONSE, buf, n),
                  "(d) 必错应答发送");
    }
    ta_expect(ta_rawReadAuthResult(r, &ok, 3000) && ok == 0,
              "(d) 裸协议错应答 → AUTH_RESULT(ok=0)");
    {
        XGuiRemoteMsgType type;
        uint8_t payload[512];
        size_t len = 0;
        ta_expect(ta_rawReadTyped(r, XGUI_REMOTE_MSG_BYE, payload,
                                  sizeof(payload), &len, 3000),
                  "(d) 错应答后服务端 BYE");
    }
    ta_pumpFor(120);
    ta_expect(XGuiServer_sessionCount(r->server) == 0,
              "(d) 错应答断链后会话回收");
    ta_rawDisconnect(r);

    /* 连接 2: 取 nonce2 → 必须与 nonce1 不同; 对口令应答 → RESULT(1)。 */
    if (!ta_rawConnect(r)) {
        ta_expect(false, "(d) 探针连接 2");
        ta_rawDisconnect(r);
        return;
    }
    ta_expect(ta_rawSendHello(r), "(d) HELLO 发送(连接 2)");
    ta_expect(ta_rawGetChallenge(r, nonce2, sizeof(nonce2), &nb2),
              "(d) 挑战读取(nonce2)");
    ta_expect(nb2 == XGUI_REMOTE_AUTH_NONCE_BYTES &&
              memcmp(nonce1, nonce2, (size_t)nb1) != 0,
              "(d) 两连接挑战 nonce 不同(防重放)");
    ta_expect(XGuiRemoteAuth_clientComputeResponse(TA_PW, nonce2,
                                                   (size_t)nb2, good),
              "(d) 对口令应答计算");
    {
        XGuiRemoteMsgAuthResponse resp;
        uint8_t buf[XGUI_REMOTE_AUTH_RESPONSE_BYTES + 8];
        size_t n;
        memset(&resp, 0, sizeof(resp));
        memcpy(resp.response, good, sizeof(good));
        resp.responseBytes = XGUI_REMOTE_AUTH_RESPONSE_BYTES;
        n = XGuiRemoteProto_encAuthResponse(buf, sizeof(buf), &resp);
        ta_expect(n > 0 &&
                  ta_rawWriteFrame(r, XGUI_REMOTE_MSG_AUTH_RESPONSE, buf, n),
                  "(d) 对口令应答发送");
    }
    ta_expect(ta_rawReadAuthResult(r, &ok, 3000) && ok == 1,
              "(d) 对口令应答 → AUTH_RESULT(ok=1) 准入");
    ta_pumpFor(120);
    ta_expect(XGuiServer_sessionCount(r->server) == 1 &&
              XGuiServer_sessionAuthState(r->server, r->sid) == 1,
              "(d) 对口令准入会话形态=已认证");
    ta_rawDisconnect(r);
    ta_pumpFor(60);
    ta_expect(XGuiServer_sessionCount(r->server) == 0,
              "(d) 显式 detach 后会话回收");
}

/* ==================== (e) 失败上限 5: 前 4 次可重答, 第 5 次断链 ====== */

static void ta_failureLimitFive(TaRaw* r)
{
    uint8_t nonce[32];
    int nb = 0;
    uint8_t bad[XGUI_REMOTE_AUTH_RESPONSE_BYTES];
    XGuiRemoteMsgAuthResponse resp;
    uint8_t buf[XGUI_REMOTE_AUTH_RESPONSE_BYTES + 8];
    size_t n = 0;
    int ok = -1;
    int k;
    uint8_t payload[512];
    size_t len = 0;

    fprintf(stderr,
            "[AUTH-TEST] (e) 认证失败上限 5(错 4 次可重答, 第 5 次断)\n");
    XGuiServer_setAuthFailureLimit(r->server, 5);
    ta_expect(XGuiServer_authFailureLimit(r->server) == 5,
              "(e) 上限读回=5");

    if (!ta_rawConnect(r)) {
        ta_expect(false, "(e) 探针连接");
        ta_rawDisconnect(r);
        return;
    }
    ta_expect(ta_rawSendHello(r), "(e) HELLO 发送");
    ta_expect(ta_rawGetChallenge(r, nonce, sizeof(nonce), &nb),
              "(e) 挑战读取");
    memset(bad, 0xA5, sizeof(bad));
    memset(&resp, 0, sizeof(resp));
    memcpy(resp.response, bad, sizeof(bad));
    resp.responseBytes = XGUI_REMOTE_AUTH_RESPONSE_BYTES;
    n = XGuiRemoteProto_encAuthResponse(buf, sizeof(buf), &resp);
    ta_expect(n > 0, "(e) 应答编码");

    for (k = 1; k <= 4; ++k) {
        int okK = -1;
        (void)k;
        ta_expect(ta_rawWriteFrame(r, XGUI_REMOTE_MSG_AUTH_RESPONSE,
                                   buf, n),
                  "(e) 错应答发送");
        ta_expect(ta_rawReadAuthResult(r, &okK, 3000) && okK == 0,
                  "(e) 错应答 RESULT(0) 可重答(未断链)");
    }
    ta_expect(XGuiServer_sessionCount(r->server) == 1,
              "(e) 前 4 次错应答会话仍在");
    ta_expect(ta_rawWriteFrame(r, XGUI_REMOTE_MSG_AUTH_RESPONSE, buf, n),
              "(e) 第 5 次错应答发送");
    ta_expect(ta_rawReadAuthResult(r, &ok, 3000) && ok == 0,
              "(e) 第 5 次 RESULT(0)");
    ta_expect(ta_rawReadTyped(r, XGUI_REMOTE_MSG_BYE, payload,
                              sizeof(payload), &len, 3000),
              "(e) 第 5 次后 BYE 断链");
    ta_pumpFor(120);
    ta_expect(XGuiServer_sessionCount(r->server) == 0,
              "(e) 第 5 次后会话回收");
    ta_rawDisconnect(r);
    XGuiServer_setAuthFailureLimit(r->server, 1); /* 复位默认口径。 */
}

/* ==================== (f) 复用矩阵服务端: 真互联 + MITM 抓包 ========== */

#define TA_CAP_BYTES (256 * 1024)

typedef struct TaProxy {
    int listenFd;
    int cFd; /* 客户端侧(accept 而来)。 */
    int sFd; /* 服务端侧(connect 而去)。 */
    uint8_t cap[TA_CAP_BYTES];
    size_t capLen;
    bool connected;
} TaProxy;

static void ta_setNonBlock(int fd)
{
    int fl = fcntl(fd, F_GETFL, 0);
    if (fl >= 0) (void)fcntl(fd, F_SETFL, fl | O_NONBLOCK);
}

static bool ta_proxyListen(TaProxy* p, uint16_t port)
{
    struct sockaddr_in addr;
    memset(p, 0, sizeof(*p));
    p->listenFd = -1;
    p->cFd = -1;
    p->sFd = -1;
    p->listenFd = socket(AF_INET, SOCK_STREAM, 0);
    if (p->listenFd < 0) return false;
    {
        int one = 1;
        setsockopt(p->listenFd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
    }
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = htons(port);
    if (bind(p->listenFd, (struct sockaddr*)&addr, sizeof(addr)) < 0 ||
        listen(p->listenFd, 1) < 0) {
        close(p->listenFd);
        p->listenFd = -1;
        return false;
    }
    ta_setNonBlock(p->listenFd);
    return true;
}

/** @brief MITM 泵: 双向搬运 + 全量抓取(两向合流顺序抓取; 仅在
 *         connected 后有数据面语义, accept/桥接由调用方驱动)。 */
static void ta_proxyPump(TaProxy* p)
{
    uint8_t buf[4096];
    int dir;
    if (!p->connected) return;
    for (dir = 0; dir < 2; ++dir) {
        int rfd = (dir == 0) ? p->cFd : p->sFd;
        int wfd = (dir == 0) ? p->sFd : p->cFd;
        for (;;) {
            ssize_t n = recv(rfd, buf, sizeof(buf), 0);
            if (n <= 0) break;
            if (p->capLen + (size_t)n <= TA_CAP_BYTES) {
                memcpy(p->cap + p->capLen, buf, (size_t)n);
                p->capLen += (size_t)n;
            }
            {
                ssize_t off = 0;
                while (off < n) {
                    ssize_t w = send(wfd, buf + off, (size_t)(n - off), 0);
                    if (w <= 0) break;
                    off += w;
                }
            }
        }
    }
}

/** @brief 泵回调适配(ta_sockReadFrame 的 pumpExtra 槽, void* 签名)。 */
static void ta_proxyPumpV(void* ud)
{
    ta_proxyPump((TaProxy*)ud);
}

static void ta_proxyClose(TaProxy* p)
{
    if (p->cFd >= 0) close(p->cFd);
    if (p->sFd >= 0) close(p->sFd);
    if (p->listenFd >= 0) close(p->listenFd);
    memset(p, 0, sizeof(*p));
}

/** @brief 原始 socket 客户端帧读取器(非阻塞 recv → 帧解析)。 */
typedef struct TaSock {
    int fd;
    XGuiRemoteFrameReader reader;
    uint8_t bannerGot;
    uint8_t banner[8];
    bool bannerSent; /**< 本端(C 侧)横幅已发(§3.3 双向横幅)。 */
} TaSock;

static bool ta_sockWriteRaw(TaSock* s, const uint8_t* p, size_t n)
{
    size_t off = 0;
    while (off < n) {
        ssize_t w = send(s->fd, p + off, n - off, 0);
        if (w > 0) off += (size_t)w;
        else if (w < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
            XGuiApplication_processEvents(XEventLoop_AllEvents);
            XThread_msleep(2);
        }
        else return false;
    }
    return true;
}

static bool ta_sockWriteFrame(TaSock* s, XGuiRemoteMsgType type,
                              const uint8_t* payload, size_t payloadBytes)
{
    uint8_t frame[512];
    size_t total = XGUI_REMOTE_FRAME_HEADER_BYTES + payloadBytes;
    if (total > sizeof(frame)) return false;
    frame[0] = (uint8_t)(payloadBytes & 0xFF);
    frame[1] = (uint8_t)((payloadBytes >> 8) & 0xFF);
    frame[2] = (uint8_t)((payloadBytes >> 16) & 0xFF);
    frame[3] = (uint8_t)((payloadBytes >> 24) & 0xFF);
    frame[4] = (uint8_t)type;
    if (payloadBytes) memcpy(frame + XGUI_REMOTE_FRAME_HEADER_BYTES,
                             payload, payloadBytes);
    return ta_sockWriteRaw(s, frame, total);
}

static bool ta_sockReadFrame(TaSock* s, XGuiRemoteMsgType* type,
                             uint8_t* payload, size_t cap, size_t* lenOut,
                             int timeoutMs, void (*pumpExtra)(void*),
                             void* pumpUd)
{
    uint8_t buf[4096];
    int64_t deadline = ta_nowMs() + timeoutMs;
    while (ta_nowMs() < deadline) {
        int rc;
        /* 先探询已入队帧(交付纪律同 ta_rawReadFrame: 勿回圈重喂)。 */
        rc = XGuiRemoteFrameReader_feed(&s->reader, NULL, 0);
        if (rc > 0) {
            size_t len = s->reader.payloadLen;
            if (len > cap) len = cap;
            memcpy(payload, s->reader.payload, len);
            *type = s->reader.type;
            if (lenOut) *lenOut = len;
            return true;
        }
        if (rc < 0) return false;
        {
            ssize_t n = recv(s->fd, buf, sizeof(buf), 0);
            if (n > 0) {
                size_t i;
                for (i = 0; i < (size_t)n && s->bannerGot < 8; ++i)
                    s->banner[s->bannerGot++] = buf[i];
                if (s->bannerGot == 8 && i <= (size_t)n) {
                    rc = XGuiRemoteFrameReader_feed(&s->reader, buf + i,
                                                    (size_t)n - i);
                    if (rc > 0) {
                        size_t len = s->reader.payloadLen;
                        if (len > cap) len = cap;
                        memcpy(payload, s->reader.payload, len);
                        *type = s->reader.type;
                        if (lenOut) *lenOut = len;
                        return true;
                    }
                    if (rc < 0) return false;
                }
            }
        }
        if (pumpExtra) pumpExtra(pumpUd);
        XGuiApplication_processEvents(XEventLoop_AllEvents);
        XThread_msleep(2);
    }
    return false;
}

/** @brief 读到指定类型帧为止(socket 版, 跳过 PING 等伴生帧)。 */
static bool ta_sockReadTyped(TaSock* s, XGuiRemoteMsgType want,
                             uint8_t* payload, size_t cap, size_t* lenOut,
                             int timeoutMs, void (*pumpExtra)(void*),
                             void* pumpUd)
{
    XGuiRemoteMsgType type;
    int64_t deadline = ta_nowMs() + timeoutMs;
    for (;;) {
        int left = (int)(deadline - ta_nowMs());
        if (left <= 0) return false;
        if (!ta_sockReadFrame(s, &type, payload, cap, lenOut, left,
                              pumpExtra, pumpUd))
            return false;
        if (type == want) return true;
    }
}

/** @brief (f) 在矩阵服务端上开 127.0.0.1 监听, 经 MITM 代理走完整认证,
 *         全量抓线并断言口令明文不出现在网络负载。 */
static void ta_wireCaptureOn(TaMatrix* m)
{
    TaTopo* t = &m->t;
    TaProxy proxy;
    TaSock sock;
    XGuiRemoteMsgHello hello;
    uint8_t buf[512];
    uint8_t good[XGUI_REMOTE_AUTH_RESPONSE_BYTES];
    XGuiRemoteMsgAuthResponse resp;
    size_t n;
    uint16_t sport = 47391, cport = 47392;
    int attempt;

    fprintf(stderr,
            "[AUTH-TEST] (f) 127.0.0.1 真互联 + MITM 抓包(明文不过网)\n");

    /* 服务端真 TCP 监听(端口占用时顺延重试; 以 serverPort 实际值为准)。 */
    {
        bool listening = false;
        for (attempt = 0; attempt < 8 && !listening; ++attempt) {
            listening = XGuiServer_listen_2(t->server, "127.0.0.1", sport);
            if (!listening) ++sport;
        }
        ta_expect(listening, "(f) 服务端 127.0.0.1 监听");
        if (!listening) return;
        sport = XGuiServer_serverPort(t->server); /* 桥接目标=实际端口。 */
    }
    /* MITM 代理监听(端口占用顺延)。 */
    {
        bool ok2 = false;
        for (attempt = 0; attempt < 8 && !ok2; ++attempt) {
            ok2 = ta_proxyListen(&proxy, cport);
            if (!ok2) ++cport;
        }
        ta_expect(ok2, "(f) MITM 代理监听");
        if (!ok2) {
            XGuiServer_close(t->server);
            return;
        }
    }

    /* 原始客户端 → 代理; 泵圈完成 accept + 桥接。 */
    memset(&sock, 0, sizeof(sock));
    XGuiRemoteFrameReader_init(&sock.reader);
    sock.fd = socket(AF_INET, SOCK_STREAM, 0);
    ta_expect(sock.fd >= 0, "(f) 原始客户端 socket");
    {
        struct sockaddr_in addr;
        int r;
        memset(&addr, 0, sizeof(addr));
        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        addr.sin_port = htons(cport);
        r = connect(sock.fd, (struct sockaddr*)&addr, sizeof(addr));
        ta_expect(r == 0, "(f) 原始客户端连上代理");
        if (r != 0) {
            close(sock.fd);
            ta_proxyClose(&proxy);
            XGuiServer_close(t->server);
            return;
        }
    }
    ta_setNonBlock(sock.fd);
    {
        int64_t deadline = ta_nowMs() + 3000;
        while (!proxy.connected && ta_nowMs() < deadline) {
            int c = accept(proxy.listenFd, NULL, NULL);
            if (c >= 0) {
                struct sockaddr_in addr;
                proxy.cFd = c;
                ta_setNonBlock(proxy.cFd);
                proxy.sFd = socket(AF_INET, SOCK_STREAM, 0);
                memset(&addr, 0, sizeof(addr));
                addr.sin_family = AF_INET;
                addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
                addr.sin_port = htons(sport);
                if (connect(proxy.sFd, (struct sockaddr*)&addr,
                            sizeof(addr)) == 0) {
                    ta_setNonBlock(proxy.sFd);
                    proxy.connected = true;
                }
                else {
                    ta_expect(false, "(f) 代理桥接服务端失败");
                }
            }
            else {
                XGuiApplication_processEvents(XEventLoop_AllEvents);
                XThread_msleep(2);
            }
        }
        ta_expect(proxy.connected, "(f) 代理桥接建立");
    }

    /* 经代理走完整认证: 等横幅 → 客户端横幅 → HELLO → ACK → 挑战 →
     * 对口令应答 → RESULT(1)。 */
    {
        XGuiRemoteMsgType type;
        uint8_t payload[512];
        size_t len = 0;
        bool got;
        /* 等服务端横幅(双向横幅 §3.3; 无帧期仅消费横幅字节)。 */
        {
            int64_t deadline = ta_nowMs() + 3000;
            while (sock.bannerGot < 8 && ta_nowMs() < deadline) {
                (void)ta_sockReadFrame(&sock, &type, payload,
                                       sizeof(payload), &len, 50,
                                       ta_proxyPumpV, &proxy);
            }
        }
        ta_expect(sock.bannerGot == 8 &&
                  XGuiRemoteProto_bannerIsValid(sock.banner),
                  "(f) 经代理收到横幅");
        {
            /* §3.3 双向横幅: 服务端攒 8B 客户端横幅后才进 HANDSHAKING。 */
            uint8_t sbanner[XGUI_REMOTE_BANNER_BYTES];
            XGuiRemoteProto_makeBanner(sbanner);
            ta_expect(ta_sockWriteRaw(&sock, sbanner,
                                      XGUI_REMOTE_BANNER_BYTES),
                      "(f) 客户端横幅经代理上行");
            sock.bannerSent = true;
        }
        memset(&hello, 0, sizeof(hello));
        hello.protocolVersion = XGUI_REMOTE_PROTOCOL_VERSION;
        hello.capabilities = 0;
        hello.authMethod = (uint8_t)XGUI_REMOTE_AUTH_NONE;
        hello.nameBytes = 10;
        memcpy(hello.name, "wire-probe", 10);
        n = XGuiRemoteProto_encHello(buf, sizeof(buf), &hello);
        ta_expect(n > 0 && ta_sockWriteFrame(&sock, XGUI_REMOTE_MSG_HELLO,
                                             buf, n),
                  "(f) HELLO 经代理上行");
        got = ta_sockReadTyped(&sock, XGUI_REMOTE_MSG_HELLO_ACK, payload,
                               sizeof(payload), &len, 3000,
                               ta_proxyPumpV, &proxy);
        ta_expect(got, "(f) HELLO_ACK 经代理下行");
        got = ta_sockReadTyped(&sock, XGUI_REMOTE_MSG_AUTH_CHALLENGE, payload,
                               sizeof(payload), &len, 3000,
                               ta_proxyPumpV, &proxy);
        ta_expect(got, "(f) 挑战经代理下行");
        if (got) {
            XGuiRemoteMsgAuthChallenge ch;
            ta_expect(XGuiRemoteProto_decAuthChallenge(payload, len, &ch),
                      "(f) 挑战解码");
            ta_expect(XGuiRemoteAuth_clientComputeResponse(
                          TA_PW, ch.nonce, ch.nonceBytes, good),
                      "(f) 对口令应答计算");
            memset(&resp, 0, sizeof(resp));
            memcpy(resp.response, good, sizeof(good));
            resp.responseBytes = XGUI_REMOTE_AUTH_RESPONSE_BYTES;
            n = XGuiRemoteProto_encAuthResponse(buf, sizeof(buf), &resp);
            ta_expect(n > 0 &&
                      ta_sockWriteFrame(&sock,
                                        XGUI_REMOTE_MSG_AUTH_RESPONSE,
                                        buf, n),
                      "(f) 应答经代理上行");
            {
                XGuiRemoteMsgAuthResult res;
                got = ta_sockReadTyped(&sock, XGUI_REMOTE_MSG_AUTH_RESULT,
                                       payload, sizeof(payload), &len, 3000,
                                       ta_proxyPumpV, &proxy);
                ta_expect(got &&
                          XGuiRemoteProto_decAuthResult(payload, len,
                                                        &res) &&
                          res.ok == 1,
                          "(f) 经代理完整认证准入(RESULT ok=1)");
            }
        }
    }
    ta_pumpFor(100);
    close(sock.fd);
    XGuiRemoteFrameReader_deinit(&sock.reader);
    ta_pumpFor(100);

    /* 抓包断言: 负载非空、含横幅与挑战帧、绝不含口令明文。 */
    ta_expect(proxy.capLen > 64, "(f) 抓包非空");
    ta_expect(proxy.capLen >= 8 &&
              memcmp(proxy.cap, "XGR1", 4) == 0,
              "(f) 抓包含协议横幅");
    ta_expect(!memmem(proxy.cap, proxy.capLen, TA_PW, strlen(TA_PW)),
              "(f) 口令明文不出现在网络负载(MITM 全量抓取)");
    ta_expect(!memmem(proxy.cap, proxy.capLen, "totally-wrong-pw",
                      strlen("totally-wrong-pw")),
              "(f) 历史口令串不出现在网络负载");
    ta_proxyClose(&proxy);
    XGuiServer_close(t->server);
}

/* ==================== main ==================== */

int main(void)
{
    TaMatrix m;
    TaRaw raw;
    const char* skip = getenv("TA_SKIP"); /* 诊断门: 跳过 a/c 或 d/e/f。 */
    ta_ensureApp();
    fprintf(stderr, "[AUTH-TEST] XGuiRemoteAuthTest 开始\n");

    ta_unitTests(); /* (0) 单元(无对象拓扑)。 */
    {
        XWidget* probe = XWidget_create(NULL, 0);
        if (probe) {
            XClassDelete(probe);
            memset(&m, 0, sizeof(m));
            if (!skip || !strchr(skip, 'a')) {
                fprintf(stderr, "[AUTH-TEST] 矩阵拓扑搭建\n");
                if (!ta_setup(&m.t, 1)) {
                    ta_expect(false, "矩阵拓扑搭建");
                }
                else {
                    m.up = true;
                    if (!ta_setupSecondClient(&m.t))
                        ta_expect(false, "第二客户端搭建");
                    ta_mAnonymous(&m);        /* (a) */
                    if (!skip || !strchr(skip, 'b'))
                        ta_mPassword(&m);     /* (b) */
                    if (!skip || !strchr(skip, 'c'))
                        ta_mRuntimeSetClear(&m); /* (c) */
                    /* (f) 复用矩阵服务端(断净会话后)。 */
                    ta_disconnectClient(&m.t, 0);
                    ta_disconnectClient(&m.t, 1);
                    ta_pumpFor(120);
                    if (!skip || !strchr(skip, 'f'))
                        ta_wireCaptureOn(&m);
                }
            }
            if (!skip || !strchr(skip, 'd') || !strchr(skip, 'e')) {
                if (!ta_rawOpenOnce(&raw, TA_PW)) {
                    ta_expect(false, "裸探针服务端搭建");
                }
                else {
                    if (!skip || !strchr(skip, 'd'))
                        ta_nonceAndRawProbe(&raw); /* (d) */
                    if (!skip || !strchr(skip, 'e'))
                        ta_failureLimitFive(&raw); /* (e) */
                }
            }
        }
        else {
            fprintf(stderr,
                    "[AUTH-TEST] 无显示环境: (a)-(f) 会话矩阵跳过\n");
        }
    }

    /* 收尾一次性删除(之后不再泵圈): 断言与摘要先落盘。 */
    fflush(stderr);
    if (m.up) ta_teardown(&m.t);
    ta_rawFinish(&raw);
    fprintf(stderr, "[AUTH-TEST] 完成: 断言 %d, 失败 %d%s\n", ta_runs,
            ta_failures, ta_failures ? " —— 存在失败" : " —— 全部通过");
    fflush(stderr);
    return ta_failures ? 1 : 0;
}

#endif /* XGUI_REMOTE_ON */
