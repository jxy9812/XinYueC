/**
 * @file       XGuiRemoteTest.c
 * @brief      XGuiRemote 远程窗口回归测试（协议/编解码/回环会话/伤害管线）。
 * @details    设计依据仓库根 XGuiRemote.md §9 测试方案与泳道 D 任务书：
 *               (a) 协议单元：20 条消息 enc→dec 逐字段回环相等；帧泵跨包/
 *                 粘包/半帧/超 16MiB 拒绝；banner memcmp 冻结口径；Profile
 *                 enc/dec + sanitize 夹取；封闭枚举非法值 dec 返回 false；
 *               (b) 编解码单元：RLE 冻结格式向量（§5.4 字面量/重复/混合/
 *                 截断报 CORRUPT）；zlib 编解码回环；ARGB32↔RGB565 不透明
 *                 像素往返位相等；tileHash 稳定性；
 *               (c) 回环端到端（零网络）：XGuiRemoteLoopbackDevice_createPair_ex
 *                 对上起 XGuiServer_attachTransport + XGuiClient_setTransport，
 *                 断言 握手→FB_META→全量 FB_UPDATE（小环容量逼出全有全无写 +
 *                 帧尾待写缓冲续传，帧序不乱流不误断链）→客户端 backbuffer
 *                 像素抽样==源像素（离屏控件树源）→注入 INPUT_POINTER 点击
 *                 远端响应（含抓取越界 i16 负坐标）→BYE；
 *               (d) 伤害：内容未变 tile 哈希去重、maxFps 认领门控节流、
 *                 有界队列水位丢批计数；
 *               (e) 开关矩阵：XGUI_REMOTE_ON=1 默认路径全量覆盖；=0 时本
 *                 模块整体从头文件裁空，套件按 stub 惯例静默通过。
 *             端到端驱动口径：服务器/客户端帧泵均挂 XAbstractEventDispatcher
 *             poll 回调（XGuiServer.c xgs_ensurePump / XGuiClient.c
 *             xgc_sessionDeviceReady），编码线程独立轮询脏位——测试以
 *             processEvents + 毫秒级让位循环泵至条件达成（有界超时）。
 *             服务端画面源 = 离屏顶层控件树（show 后 XWidget_repaint 同步
 *             paint→flush，present 回调采集进会话影子缓冲）；客户端侧经
 *             XWidget_grab 临时画布派发 paintEvent 抽样 backbuffer 内容。
 * @author     XinYueC 团队
 ******************************************************************************/
#include "XGuiRemoteTest.h"
#include "XGuiConfig.h" /* 门控宏定义源：必须在 #if 之前（XKeyboardTest 惯例，
                            宏未定义时 #if 恒 0 → 静默 stub 恒绿的实锅预防）。 */
#include "XGuiRemoteProto.h" /* XGUI_REMOTE_ON 开关单一来源（#ifndef 定义于
                                本头文件头, 不在 XGuiConfig.h）——必须先于
                                下方 #if 求值, 否则真实测试体整段编译出。 */

#if XWIDGET_ON && XGUI_REMOTE_ON

#include "XGuiRemoteProto.h"
#include "XGuiRemoteCodec.h"
#include "XGuiRemoteLoopback.h"
#include "XGuiServer.h"
#include "XGuiClient.h"
#include "XRemoteSessionBar.h" /* (e) 悬浮会话工具条回环 e2e(2026-10-03)。 */
#include "XGuiApplication.h"
#include "XWidget.h"
#include "XObject.h"
#include "XEvent.h"
#include "XCheckBox.h"
#include "XPushButton.h" /* 悬浮条断开按钮合成点击(2026-10-03)。 */
#include "XLabel.h"
#include "XImage.h"
#include "XPixmap.h"
#include "XPalette.h"
#include "XDateTime.h" /* XDateTime_currentMSecsSinceEpoch（等待循环时钟）。 */
#include "XThread.h"   /* XThread_msleep（泵循环让位）。 */
#include "XMemory.h"
#include <stdio.h>
#include <string.h>

/* ==================== 断言与泵工具 ==================== */

static int xr_failures = 0;

static void xr_expect(bool cond, const char* what)
{
    if (!cond) {
        fprintf(stderr, "[XR-FAIL] %s\n", what ? what : "");
        ++xr_failures;
    }
}

/** @brief 带上下文的失败（拼接动态信息；what 前缀说明采样点）。 */
static void xr_expect_msg(bool cond, const char* what, const char* detail)
{
    if (!cond) {
        fprintf(stderr, "[XR-FAIL] %s%s%s\n", what ? what : "",
                detail ? ": " : "", detail ? detail : "");
        ++xr_failures;
    }
}

/** @brief 墙钟毫秒（等待循环/节流断言共用时钟）。 */
static int64_t xr_nowMs(void)
{
    return XDateTime_currentMSecsSinceEpoch();
}

/** @brief 泵事件循环至 deadline（服务器/客户端 poll 回调在此推进）。 */
static void xr_pumpUntil(int64_t deadlineMs)
{
    while (xr_nowMs() < deadlineMs) {
        XGuiApplication_processEvents(XEventLoop_AllEvents);
        XThread_msleep(2);
    }
    /* 末圈再泵一次：deadline 前最后一拍到达的字节走完派发。 */
    XGuiApplication_processEvents(XEventLoop_AllEvents);
}

/** @brief 泵事件循环 ms 毫秒。 */
static void xr_pumpFor(int ms)
{
    xr_pumpUntil(xr_nowMs() + ms);
}

/** @brief 条件等待：泵事件循环直至 cond 达成或超时（返回是否达成）。 */
static bool xr_waitUntil(bool (*cond)(void*), void* ud, int timeoutMs)
{
    int64_t deadline = xr_nowMs() + timeoutMs;
    while (xr_nowMs() < deadline) {
        XGuiApplication_processEvents(XEventLoop_AllEvents);
        if (cond(ud)) return true;
        XThread_msleep(2);
    }
    XGuiApplication_processEvents(XEventLoop_AllEvents);
    return cond(ud);
}

/** @brief 确保应用单例存在（回归主入口已建；独立直跑兜底，同款惯例）。 */
static void xr_ensureApp(void)
{
    (void)XGuiApplication_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, 0, NULL);
}

/* ==================== (a) 协议单元 ==================== */

/** @brief 填一份 HELLO 测试档（含区分度名称）。 */
static void xr_fillHello(XGuiRemoteMsgHello* m, uint16_t ver, uint8_t auth,
                         const char* name)
{
    size_t n = strlen(name);
    if (n > XGUI_REMOTE_MAX_NAME_BYTES) n = XGUI_REMOTE_MAX_NAME_BYTES;
    memset(m, 0, sizeof(*m));
    m->protocolVersion = ver;
    m->capabilities = 0xDEADBEEFu;
    m->authMethod = auth;
    m->nameBytes = (uint16_t)n;
    memcpy(m->name, name, n);
}

static bool xr_helloEqual(const XGuiRemoteMsgHello* a, const XGuiRemoteMsgHello* b)
{
    return a->protocolVersion == b->protocolVersion &&
           a->capabilities == b->capabilities &&
           a->authMethod == b->authMethod &&
           a->nameBytes == b->nameBytes &&
           memcmp(a->name, b->name, XGUI_REMOTE_MAX_NAME_BYTES) == 0;
}

/** @brief 小端序列化辅助：显式字节布局 + 回环。 */
static void xr_test_putget(void)
{
    uint8_t b[8];
    XGuiRemoteProto_putU16(b, 0x1234u);
    xr_expect(b[0] == 0x34 && b[1] == 0x12, "putU16 小端字节序");
    XGuiRemoteProto_putU32(b, 0x12345678u);
    xr_expect(b[0] == 0x78 && b[1] == 0x56 && b[2] == 0x34 && b[3] == 0x12,
              "putU32 小端字节序");
    XGuiRemoteProto_putU64(b, 0x0102030405060708ull);
    xr_expect(b[0] == 0x08 && b[7] == 0x01, "putU64 小端字节序");
    XGuiRemoteProto_putU16(b, 0x0102u);
    XGuiRemoteProto_putU32(b + 2, 0x05060708u);
    xr_expect(XGuiRemoteProto_getU16(b) == 0x0102u &&
              XGuiRemoteProto_getU32(b + 2) == 0x05060708u,
              "getU16/getU32 回读一致");
    XGuiRemoteProto_putU64(b, 0x8877665544332211ull);
    xr_expect(XGuiRemoteProto_getU64(b) == 0x8877665544332211ull,
              "putU64/getU64 回环");
}

/** @brief 横幅：组包 + memcmp("XGR1",4) 冻结口径 + 版本读取。 */
static void xr_test_banner(void)
{
    uint8_t b[XGUI_REMOTE_BANNER_BYTES];
    const uint8_t garbage[XGUI_REMOTE_BANNER_BYTES] =
        {'X', 'G', 'Q', '1', 1, 0, 0, 0};
    XGuiRemoteProto_makeBanner(b);
    xr_expect(memcmp(b, "XGR1", 4) == 0, "横幅魔数逐字节 'XGR1'");
    xr_expect(XGuiRemoteProto_bannerIsValid(b), "横幅校验通过");
    xr_expect(XGuiRemoteProto_bannerVersion(b) == XGUI_REMOTE_PROTOCOL_VERSION,
              "横幅版本 = 1");
    xr_expect(b[6] == 0 && b[7] == 0, "横幅保留位为 0");
    /* 冻结口径反例：校验是字节串 memcmp，非 LE 整数运算——同长度非法
     * 魔数必须拒绝（'XGRQ' 的 LE 整数仅差一位也拒）。 */
    xr_expect(!XGuiRemoteProto_bannerIsValid(garbage), "非法魔数拒绝");
}

/** @brief 20 条消息 enc→dec 逐字段回环（含边界长度 0/满内联缓冲）。 */
static void xr_test_messagesRoundtrip(void)
{
    uint8_t buf[2 * XGUI_REMOTE_MAX_TEXT_BYTES + 64];
    size_t n;

    { /* 1/2 HELLO + HELLO_ACK（共用结构）。 */
        XGuiRemoteMsgHello m, o;
        xr_fillHello(&m, XGUI_REMOTE_PROTOCOL_VERSION,
                     XGUI_REMOTE_AUTH_NONE, "xr-client");
        n = XGuiRemoteProto_encHello(buf, sizeof(buf), &m);
        xr_expect(n == 9 + m.nameBytes, "HELLO 编码长度 = 9+名称");
        xr_expect(XGuiRemoteProto_decHello(buf, n, &o) && xr_helloEqual(&m, &o),
                  "HELLO enc→dec 逐字段相等");
        xr_fillHello(&m, 1, XGUI_REMOTE_AUTH_SHA256_CHALLENGE,
                     "0123456789012345678901234567890123456789"
                     "012345678901234567890123");
        n = XGuiRemoteProto_encHello(buf, sizeof(buf), &m);
        xr_expect(n > 0 && XGuiRemoteProto_decHello(buf, n, &o) &&
                  xr_helloEqual(&m, &o), "HELLO_ACK 64B 满名称回环");
        xr_expect(XGuiRemoteProto_encHello(buf, 5, &m) == 0,
                  "HELLO 缓冲不足返回 0");
        xr_expect(!XGuiRemoteProto_decHello(buf, 8, &o),
                  "HELLO 负载截断 dec 拒绝");
    }
    { /* 3 AUTH_CHALLENGE。 */
        XGuiRemoteMsgAuthChallenge m, o;
        uint16_t i;
        memset(&m, 0, sizeof(m));
        m.method = XGUI_REMOTE_AUTH_SHA256_CHALLENGE;
        m.nonceBytes = XGUI_REMOTE_AUTH_NONCE_BYTES;
        for (i = 0; i < m.nonceBytes; ++i) m.nonce[i] = (uint8_t)(i * 7 + 1);
        n = XGuiRemoteProto_encAuthChallenge(buf, sizeof(buf), &m);
        xr_expect(n == 3 + m.nonceBytes, "AUTH_CHALLENGE 编码长度");
        xr_expect(XGuiRemoteProto_decAuthChallenge(buf, n, &o) &&
                  o.method == m.method &&
                  o.nonceBytes == m.nonceBytes &&
                  memcmp(o.nonce, m.nonce, XGUI_REMOTE_AUTH_NONCE_BYTES) == 0,
                  "AUTH_CHALLENGE 逐字段相等");
    }
    { /* 4 AUTH_RESPONSE。 */
        XGuiRemoteMsgAuthResponse m, o;
        memset(&m, 0, sizeof(m));
        m.responseBytes = XGUI_REMOTE_AUTH_RESPONSE_BYTES;
        memset(m.response, 0xA5, sizeof(m.response));
        n = XGuiRemoteProto_encAuthResponse(buf, sizeof(buf), &m);
        xr_expect(XGuiRemoteProto_decAuthResponse(buf, n, &o) &&
                  o.responseBytes == m.responseBytes &&
                  memcmp(o.response, m.response,
                         XGUI_REMOTE_AUTH_RESPONSE_BYTES) == 0,
                  "AUTH_RESPONSE 逐字段相等");
    }
    { /* 5 AUTH_RESULT。 */
        XGuiRemoteMsgAuthResult m, o;
        memset(&m, 0, sizeof(m));
        m.ok = 1;
        m.textBytes = (uint16_t)strlen("welcome");
        memcpy(m.text, "welcome", m.textBytes);
        n = XGuiRemoteProto_encAuthResult(buf, sizeof(buf), &m);
        xr_expect(XGuiRemoteProto_decAuthResult(buf, n, &o) &&
                  o.ok == 1 && o.textBytes == m.textBytes &&
                  memcmp(o.text, m.text, m.textBytes) == 0,
                  "AUTH_RESULT 逐字段相等");
        m.textBytes = XGUI_REMOTE_MAX_MSG_BYTES; /* MAX_MSG 满宽。 */
        memset(m.text, 'x', sizeof(m.text));
        n = XGuiRemoteProto_encAuthResult(buf, sizeof(buf), &m);
        xr_expect(n > 0 && XGuiRemoteProto_decAuthResult(buf, n, &o) &&
                  o.textBytes == XGUI_REMOTE_MAX_MSG_BYTES &&
                  memcmp(o.text, m.text, XGUI_REMOTE_MAX_MSG_BYTES) == 0,
                  "AUTH_RESULT 256B 满文本回环");
    }
    { /* 6 FB_META。 */
        XGuiRemoteMsgFbMeta m, o;
        memset(&m, 0, sizeof(m));
        m.width = 320;
        m.height = 240;
        m.format = XGUI_REMOTE_PF_ARGB32;
        m.tileWidth = 128;
        m.tileHeight = 128;
        m.profileId = XGUI_REMOTE_PROFILE_PERFORMANCE;
        m.flags = 0x01;
        m.titleBytes = (uint16_t)strlen("镜像窗口");
        memcpy(m.title, "镜像窗口", m.titleBytes);
        n = XGuiRemoteProto_encFbMeta(buf, sizeof(buf), &m);
        xr_expect(XGuiRemoteProto_decFbMeta(buf, n, &o) &&
                  o.width == 320 && o.height == 240 &&
                  o.format == XGUI_REMOTE_PF_ARGB32 &&
                  o.tileWidth == 128 && o.tileHeight == 128 &&
                  o.profileId == XGUI_REMOTE_PROFILE_PERFORMANCE &&
                  o.flags == 0x01 && o.titleBytes == m.titleBytes &&
                  memcmp(o.title, m.title, m.titleBytes) == 0,
                  "FB_META 逐字段相等");
    }
    { /* 7 FB_REQUEST。 */
        XGuiRemoteMsgFbRequest m, o;
        memset(&m, 0, sizeof(m));
        m.mode = 1;
        n = XGuiRemoteProto_encFbRequest(buf, sizeof(buf), &m);
        xr_expect(n == 9, "FB_REQUEST 编码 9 字节");
        xr_expect(XGuiRemoteProto_decFbRequest(buf, n, &o) &&
                  o.mode == 1 && o.x == 0 && o.w == 0,
                  "FB_REQUEST 逐字段相等");
    }
    { /* 7b FB_REQUEST 视口通告(2026-10-05 方案③④加法式语义; 线上布局
       * 不变, mode=0+rect 老服务端本就零操作)。 */
        XGuiRemoteMsgFbRequest m, o;
        memset(&m, 0, sizeof(m));
        m.mode = 0; /* 视口通告=无刷新动作。 */
        m.x = 16;
        m.y = 32;
        m.w = 504;
        m.h = 458;
        n = XGuiRemoteProto_encFbRequest(buf, sizeof(buf), &m);
        xr_expect(n == 9, "FB_REQUEST 视口通告编码 9 字节");
        xr_expect(XGuiRemoteProto_decFbRequest(buf, n, &o) &&
                  o.mode == 0 && o.x == 16 && o.y == 32 &&
                  o.w == 504 && o.h == 458,
                  "FB_REQUEST mode=0+视口逐字段相等");
        xr_expect((XGUI_REMOTE_CAP_FB_REQUEST & 0xFFu) == 0 &&
                  XGUI_REMOTE_CAP_FB_REQUEST != XGUI_REMOTE_CAP_UDP,
                  "CAP_FB_REQUEST 能力位加法式不与既有位冲突");
    }
    { /* 8 FB_UPDATE 帧级头 + tile 记录（encFbUpdate 恒 8 字节头）。 */
        XGuiRemoteMsgFbUpdate h, ho;
        XGuiRemoteMsgFbTile t, to;
        uint8_t pixels[7] = {1, 2, 3, 4, 5, 6, 7};
        size_t off, next;
        memset(&h, 0, sizeof(h));
        h.sequence = 0xFFFFFFFEu; /* 回绕前界。 */
        h.tileCount = 1;
        h.format = XGUI_REMOTE_PF_RGB565;
        h.flags = 0;
        n = XGuiRemoteProto_encFbUpdate(buf, sizeof(buf), &h);
        xr_expect(n == XGUI_REMOTE_FB_UPDATE_HEADER_BYTES,
                  "encFbUpdate 恒返回帧级头 8 字节");
        memset(&t, 0, sizeof(t));
        t.x = 1;
        t.y = 2;
        t.w = 3;
        t.h = 4;
        t.codec = XGUI_REMOTE_CODEC_RLE;
        t.flags = XGUI_REMOTE_TILE_FLAG_NONE;
        t.payload = pixels;
        t.payloadBytes = 7;
        n += XGuiRemoteProto_encFbTile(buf + n, sizeof(buf) - n, &t, pixels);
        xr_expect(XGuiRemoteProto_decFbUpdate(buf, 8, &ho) &&
                  ho.sequence == h.sequence && ho.tileCount == 1 &&
                  ho.format == XGUI_REMOTE_PF_RGB565,
                  "FB_UPDATE 帧级头逐字段相等");
        off = XGUI_REMOTE_FB_UPDATE_HEADER_BYTES; /* 首条偏移 = 8(冻结)。 */
        xr_expect(XGuiRemoteProto_decFbTile(buf, n, off, &to, &next) &&
                  to.x == 1 && to.y == 2 && to.w == 3 && to.h == 4 &&
                  to.codec == XGUI_REMOTE_CODEC_RLE &&
                  to.payloadBytes == 7 &&
                  memcmp(to.payload, pixels, 7) == 0 && next == n,
                  "FB tile 记录逐字段相等(payload 借用指向负载内部)");
        xr_expect(!XGuiRemoteProto_decFbTile(buf, n, next, &to, NULL),
                  "tile 记录越界偏移 dec 拒绝");
        xr_expect(!XGuiRemoteProto_decFbTile(buf, 8 + 9, off, &to, NULL),
                  "tile 记录截断 dec 拒绝");
    }
    { /* 9 FB_ACK。 */
        XGuiRemoteMsgFbAck m, o;
        memset(&m, 0, sizeof(m));
        m.ackSequence = 42;
        m.windowBytes = 0x00FF00u;
        n = XGuiRemoteProto_encFbAck(buf, sizeof(buf), &m);
        xr_expect(n == 8, "FB_ACK 编码 8 字节");
        xr_expect(XGuiRemoteProto_decFbAck(buf, n, &o) &&
                  o.ackSequence == 42 && o.windowBytes == 0x00FF00u,
                  "FB_ACK 逐字段相等");
    }
    { /* 10 INPUT_KEY。 */
        XGuiRemoteMsgInputKey m, o;
        memset(&m, 0, sizeof(m));
        m.action = XGUI_REMOTE_KEY_PRESS;
        m.key = (uint32_t)XKey_A;
        m.modifiers = XGUI_REMOTE_MOD_SHIFT | XGUI_REMOTE_MOD_CTRL;
        m.nativeScanCode = 38;
        m.timestampMs = 123456u;
        n = XGuiRemoteProto_encInputKey(buf, sizeof(buf), &m);
        xr_expect(n == 17, "INPUT_KEY 编码 17 字节");
        xr_expect(XGuiRemoteProto_decInputKey(buf, n, &o) &&
                  o.action == XGUI_REMOTE_KEY_PRESS &&
                  o.key == (uint32_t)XKey_A &&
                  o.modifiers == m.modifiers &&
                  o.nativeScanCode == 38 && o.timestampMs == 123456u,
                  "INPUT_KEY 逐字段相等");
    }
    { /* 11 INPUT_POINTER（i16 两端界）。 */
        XGuiRemoteMsgInputPointer m, o;
        memset(&m, 0, sizeof(m));
        m.action = XGUI_REMOTE_PTR_PRESS;
        m.button = XGUI_REMOTE_BTN_LEFT;
        m.buttons = XGUI_REMOTE_BTN_LEFT;
        m.modifiers = 0;
        m.x = -32768;
        m.y = 32767;
        m.timestampMs = 99u;
        n = XGuiRemoteProto_encInputPointer(buf, sizeof(buf), &m);
        xr_expect(n == 16, "INPUT_POINTER 编码 16 字节");
        xr_expect(XGuiRemoteProto_decInputPointer(buf, n, &o) &&
                  o.action == XGUI_REMOTE_PTR_PRESS &&
                  o.button == XGUI_REMOTE_BTN_LEFT &&
                  o.buttons == XGUI_REMOTE_BTN_LEFT &&
                  o.x == -32768 && o.y == 32767 && o.timestampMs == 99u,
                  "INPUT_POINTER i16 边界逐字段相等");
    }
    { /* 12 INPUT_WHEEL。 */
        XGuiRemoteMsgInputWheel m, o;
        memset(&m, 0, sizeof(m));
        m.angleX = 0;
        m.angleY = -120;
        m.pixelX = 0;
        m.pixelY = -8;
        m.buttons = 0;
        m.modifiers = XGUI_REMOTE_MOD_ALT;
        m.x = 10;
        m.y = 20;
        m.timestampMs = 7u;
        n = XGuiRemoteProto_encInputWheel(buf, sizeof(buf), &m);
        xr_expect(XGuiRemoteProto_decInputWheel(buf, n, &o) &&
                  o.angleY == -120 && o.pixelY == -8 &&
                  o.modifiers == m.modifiers && o.x == 10 && o.y == 20,
                  "INPUT_WHEEL 逐字段相等");
    }
    { /* 13 INPUT_TOUCH（多点 + per-id + 压力 Q8）。 */
        XGuiRemoteMsgInputTouch m, o;
        int i;
        memset(&m, 0, sizeof(m));
        m.action = XGUI_REMOTE_TOUCH_UPDATE;
        m.pointCount = 2;
        m.timestampMs = 55u;
        m.points[0].id = 11;
        m.points[0].state = XGUI_REMOTE_TP_PRESSED;
        m.points[0].x = 1;
        m.points[0].y = 2;
        m.points[0].pressureQ8 = 200;
        m.points[1].id = 12;
        m.points[1].state = XGUI_REMOTE_TP_RELEASED;
        m.points[1].x = 30;
        m.points[1].y = 40;
        m.points[1].pressureQ8 = 0;
        n = XGuiRemoteProto_encInputTouch(buf, sizeof(buf), &m);
        xr_expect(n > 0, "INPUT_TOUCH 编码成功");
        xr_expect(XGuiRemoteProto_decInputTouch(buf, n, &o) &&
                  o.action == XGUI_REMOTE_TOUCH_UPDATE &&
                  o.pointCount == 2 &&
                  o.points[0].id == 11 &&
                  o.points[0].state == XGUI_REMOTE_TP_PRESSED &&
                  o.points[0].pressureQ8 == 200 &&
                  o.points[1].id == 12 &&
                  o.points[1].state == XGUI_REMOTE_TP_RELEASED,
                  "INPUT_TOUCH 主点/副点 per-id 状态相等");
        for (i = 0; i < 2; ++i) {
            xr_expect(o.points[i].x == m.points[i].x &&
                      o.points[i].y == m.points[i].y &&
                      o.points[i].reserved == 0,
                      "INPUT_TOUCH 触点坐标/保留位相等");
        }
    }
    { /* 14 INPUT_IME（MAX_TEXT 满宽）。 */
        XGuiRemoteMsgInputIme m, o;
        memset(&m, 0, sizeof(m));
        m.preeditBytes = XGUI_REMOTE_MAX_TEXT_BYTES;
        memset(m.preedit, 'n', sizeof(m.preedit));
        m.commitBytes = 3;
        memcpy(m.commit, "中", 3);
        m.replacementStart = -5;
        m.replacementLength = 2;
        m.cursorPosition = 7;
        m.anchorPosition = 3;
        n = XGuiRemoteProto_encInputIme(buf, sizeof(buf), &m);
        xr_expect(n > 0, "INPUT_IME 编码成功");
        xr_expect(XGuiRemoteProto_decInputIme(buf, n, &o) &&
                  o.preeditBytes == XGUI_REMOTE_MAX_TEXT_BYTES &&
                  memcmp(o.preedit, m.preedit, XGUI_REMOTE_MAX_TEXT_BYTES) == 0 &&
                  o.commitBytes == 3 && memcmp(o.commit, "中", 3) == 0 &&
                  o.replacementStart == -5 && o.replacementLength == 2 &&
                  o.cursorPosition == 7 && o.anchorPosition == 3,
                  "INPUT_IME 逐字段相等");
        xr_expect(XGuiRemoteProto_encInputIme(buf, 16, &m) == 0,
                  "INPUT_IME 缓冲不足返回 0");
    }
    { /* 15/16 PROFILE_SET / PROFILE_RESULT。 */
        XGuiRemoteMsgProfileSet m, o;
        XGuiRemoteMsgProfileResult r, ro;
        memset(&m, 0, sizeof(m));
        m.profileId = XGUI_REMOTE_PROFILE_CUSTOM;
        m.hasCustomProfile = 1;
        XGuiRemoteProfile_initResource(&m.profile);
        n = XGuiRemoteProto_encProfileSet(buf, sizeof(buf), &m);
        xr_expect(n > 0, "PROFILE_SET 编码成功");
        xr_expect(XGuiRemoteProto_decProfileSet(buf, n, &o) &&
                  o.profileId == XGUI_REMOTE_PROFILE_CUSTOM &&
                  o.hasCustomProfile == 1 &&
                  o.profile.wireFormat == XGUI_REMOTE_PF_RGB565 &&
                  o.profile.tileWidth == 32 && o.profile.maxFps == 15,
                  "PROFILE_SET 携带自定义档回环");
        memset(&r, 0, sizeof(r));
        r.accepted = 0;
        r.profileId = XGUI_REMOTE_PROFILE_RESOURCE;
        n = XGuiRemoteProto_encProfileResult(buf, sizeof(buf), &r);
        xr_expect(XGuiRemoteProto_decProfileResult(buf, n, &ro) &&
                  ro.accepted == 0 &&
                  ro.profileId == XGUI_REMOTE_PROFILE_RESOURCE,
                  "PROFILE_RESULT 逐字段相等");
    }
    { /* 17/18 PING / PONG（u64 回显）。 */
        uint64_t t = 0x1122334455667788ull, o = 0;
        n = XGuiRemoteProto_encPing(buf, sizeof(buf), t);
        xr_expect(n == 8, "PING 编码 8 字节");
        xr_expect(XGuiRemoteProto_decPing(buf, n, &o) && o == t,
                  "PING/PONG 时间戳回环");
        xr_expect(!XGuiRemoteProto_decPing(buf, 7, &o), "PING 截断拒绝");
    }
    { /* 19 BYE（开放字段 reason 不校验）。 */
        XGuiRemoteMsgBye m, o;
        memset(&m, 0, sizeof(m));
        m.reason = XGUI_REMOTE_BYE_SERVER_SHUTDOWN;
        m.textBytes = 5;
        memcpy(m.text, "bye-1", 5);
        n = XGuiRemoteProto_encBye(buf, sizeof(buf), &m);
        xr_expect(XGuiRemoteProto_decBye(buf, n, &o) &&
                  o.reason == XGUI_REMOTE_BYE_SERVER_SHUTDOWN &&
                  o.textBytes == 5 && memcmp(o.text, "bye-1", 5) == 0,
                  "BYE 逐字段相等");
        m.reason = 0xEE; /* 开放字段：未知原因原样透传。 */
        n = XGuiRemoteProto_encBye(buf, sizeof(buf), &m);
        xr_expect(XGuiRemoteProto_decBye(buf, n, &o) && o.reason == 0xEE,
                  "BYE 未知 reason 开放字段不拒绝");
    }
    { /* 20 ERROR（开放字段 code 不校验）。 */
        XGuiRemoteMsgError m, o;
        memset(&m, 0, sizeof(m));
        m.code = (uint16_t)XGUI_REMOTE_ERR_FRAME_TOO_LARGE;
        m.textBytes = 4;
        memcpy(m.text, "big!", 4);
        n = XGuiRemoteProto_encError(buf, sizeof(buf), &m);
        xr_expect(XGuiRemoteProto_decError(buf, n, &o) &&
                  o.code == (uint16_t)XGUI_REMOTE_ERR_FRAME_TOO_LARGE &&
                  memcmp(o.text, "big!", 4) == 0,
                  "ERROR 逐字段相等");
        m.code = 0xBEEF;
        n = XGuiRemoteProto_encError(buf, sizeof(buf), &m);
        xr_expect(XGuiRemoteProto_decError(buf, n, &o) && o.code == 0xBEEF,
                  "ERROR 未知 code 开放字段不拒绝");
    }
}

/** @brief 帧泵：跨包/半帧/粘包/超 16MiB 拒绝 + writeFrame 纪律。 */
static void xr_test_frameReader(void)
{
    XGuiRemoteFrameReader r;
    uint8_t big[16];
    uint8_t small[16];
    uint8_t join[64];
    size_t bigLen, smallLen;
    int got;

    /* 造两帧完整帧(5B 帧头 + 负载): PING(8B) + FB_REQUEST(9B)。 */
    bigLen = 0;
    XGuiRemoteProto_putU32(big + bigLen, 8);
    bigLen += 4;
    big[bigLen++] = (uint8_t)XGUI_REMOTE_MSG_PING;
    bigLen += XGuiRemoteProto_encPing(big + bigLen, sizeof(big) - bigLen,
                                      0xAABBCCDD11223344ull);
    {
        XGuiRemoteMsgFbRequest req;
        memset(&req, 0, sizeof(req));
        req.mode = 1;
        smallLen = 0;
        XGuiRemoteProto_putU32(small + smallLen, 9);
        smallLen += 4;
        small[smallLen++] = (uint8_t)XGUI_REMOTE_MSG_FB_REQUEST;
        smallLen += XGuiRemoteProto_encFbRequest(small + smallLen,
                                                 sizeof(small) - smallLen,
                                                 &req);
    }
    xr_expect(bigLen == 13 && smallLen == 14, "测试整帧长度就绪(5B 头+负载)");

    /* 1) 整帧一次喂入。 */
    XGuiRemoteFrameReader_init(&r);
    got = XGuiRemoteFrameReader_feed(&r, big, bigLen);
    xr_expect(got == 1 && r.type == XGUI_REMOTE_MSG_PING &&
              r.payloadLen == 8 &&
              XGuiRemoteProto_getU64(r.payload) == 0xAABBCCDD11223344ull,
              "帧泵整帧一次交付");
    got = XGuiRemoteFrameReader_feed(&r, NULL, 0);
    xr_expect(got == 0, "帧耗尽后续读返回 0");

    /* 2) 半帧 + 跨包凑齐（分段喂入）。 */
    XGuiRemoteFrameReader_deinit(&r);
    XGuiRemoteFrameReader_init(&r);
    got = XGuiRemoteFrameReader_feed(&r, small, 4);
    xr_expect(got == 0, "帧头未凑齐返回 0");
    got = XGuiRemoteFrameReader_feed(&r, small + 4, 5);
    xr_expect(got == 0, "负载未凑齐返回 0(半帧)");
    got = XGuiRemoteFrameReader_feed(&r, small + 9, smallLen - 9);
    xr_expect(got == 1 && r.type == XGUI_REMOTE_MSG_FB_REQUEST &&
              r.payloadLen == 9, "跨包半帧凑齐交付");

    /* 3) 粘包：两帧拼一块喂入，feed(NULL,0) 逐帧取走。 */
    XGuiRemoteFrameReader_deinit(&r);
    XGuiRemoteFrameReader_init(&r);
    memcpy(join, big, bigLen);
    memcpy(join + bigLen, small, smallLen);
    got = XGuiRemoteFrameReader_feed(&r, join, bigLen + smallLen);
    xr_expect(got == 1 && r.type == XGUI_REMOTE_MSG_PING, "粘包首帧交付");
    got = XGuiRemoteFrameReader_feed(&r, NULL, 0);
    xr_expect(got == 1 && r.type == XGUI_REMOTE_MSG_FB_REQUEST,
              "粘包次帧经空喂续读交付");
    got = XGuiRemoteFrameReader_feed(&r, NULL, 0);
    xr_expect(got == 0, "粘包取尽返回 0");
    XGuiRemoteFrameReader_deinit(&r);

    /* 4) 长/超长帧长度字段。 */
    XGuiRemoteFrameReader_init(&r);
    join[0] = 0x01;
    join[1] = 0x00;
    join[2] = 0x01; /* u32 = 0x00010001 = 64Ki+1(合法, 等待负载)。 */
    join[3] = 0x00;
    join[4] = (uint8_t)XGUI_REMOTE_MSG_PING;
    xr_expect(XGuiRemoteFrameReader_feed(&r, join, 5) == 0,
              "合法长帧头等待负载不报错");
    XGuiRemoteFrameReader_deinit(&r);
    XGuiRemoteFrameReader_init(&r);
    join[0] = 0xFF;
    join[1] = 0xFF;
    join[2] = 0x00; /* u32 = 0x0100FFFF = 16MiB+64Ki-1(超帽)。 */
    join[3] = 0x01;
    join[4] = (uint8_t)XGUI_REMOTE_MSG_PING;
    got = XGuiRemoteFrameReader_feed(&r, join, 5);
    xr_expect(got == -1, "长度字段超 16MiB 硬帽返回 -1");
    xr_expect(XGuiRemoteFrameReader_feed(&r, NULL, 0) == -1,
              "协议错误粘性：续读仍 -1");
    XGuiRemoteFrameReader_deinit(&r);

    /* 5) writeFrame 拒绝路径(零写出)与正常路径返回值。 */
    {
        XGuiRemoteLoopbackDevice* peer = NULL;
        XGuiRemoteLoopbackDevice* dev =
            XGuiRemoteLoopbackDevice_createPair(4096, &peer);
        xr_expect(dev != NULL && peer != NULL, "回环设备对创建");
        if (dev && peer) {
            /* 载荷指针有效但谎报超帽长度: 在触载荷前即被拒。 */
            xr_expect(XGuiRemoteProto_writeFrame(
                          (XIODevice*)dev, XGUI_REMOTE_MSG_PING, big,
                          (size_t)XGUI_REMOTE_MAX_FRAME_BYTES + 1) == -1,
                      "writeFrame 超过 16MiB 拒绝");
            xr_expect(XGuiRemoteProto_writeFrame(NULL, XGUI_REMOTE_MSG_PING,
                                                 small, 4) == -1,
                      "writeFrame NULL 设备拒绝");
            xr_expect(XGuiRemoteProto_writeFrame((XIODevice*)dev,
                                                 XGUI_REMOTE_MSG_PING, NULL,
                                                 4) == -1,
                      "writeFrame 空负载非零长拒绝");
            xr_expect(XGuiRemoteLoopbackDevice_bufferedBytes(dev) == 0,
                      "被拒帧零字节写出");
            xr_expect(XGuiRemoteProto_writeFrame((XIODevice*)dev,
                                                 XGUI_REMOTE_MSG_PING, small,
                                                 4) == (int64_t)(5 + 4),
                      "writeFrame 返回整帧接受字节数");
            xr_expect(XGuiRemoteLoopbackDevice_bufferedBytes(peer) == 9,
                      "对端可见 9 字节");
        }
        if (dev) XClassDelete((XClass*)dev);
        if (peer) XClassDelete((XClass*)peer);
    }
}

/** @brief 档位：两预设参数表 + sanitize 夹取 + enc/dec 回环 + isValid。 */
static void xr_test_profile(void)
{
    XGuiRemoteProfile p, q;
    uint8_t buf[64];
    size_t n;

    XGuiRemoteProfile_initPerformance(&p);
    xr_expect(p.wireFormat == XGUI_REMOTE_PF_ARGB32 &&
                  p.codec == XGUI_REMOTE_CODEC_ZLIB && p.zlibLevel == 1 &&
                  p.tileWidth == 128 && p.tileHeight == 128 &&
                  p.maxFps == 60 && p.mouseMoveThrottleMs == 0 &&
                  p.encodeQueueBytes == 2u * 1024u * 1024u &&
                  p.txBudgetBytes == 256u * 1024u,
              "performance 预设参数表(§5.2)");
    xr_expect(XGuiRemoteProfile_isValid(&p), "performance 预设合法");

    XGuiRemoteProfile_initResource(&p);
    xr_expect(p.wireFormat == XGUI_REMOTE_PF_RGB565 &&
                  p.codec == XGUI_REMOTE_CODEC_RLE &&
                  p.tileWidth == 32 && p.tileHeight == 32 &&
                  p.maxFps == 15 && p.mouseMoveThrottleMs == 30 &&
                  p.encodeQueueBytes == 256u * 1024u &&
                  p.txBudgetBytes == 32u * 1024u,
              "resource 预设参数表(§5.2)");
    XGuiRemoteProfile_initAuto(&p);
    xr_expect(p.wireFormat == XGUI_REMOTE_PF_RGB565 && p.maxFps == 15 &&
                  p.tileWidth == 32,
              "auto 预设 V1 等价 resource");

    /* sanitize 夹取：越界取边界；非法枚举回退。 */
    memset(&p, 0, sizeof(p));
    p.wireFormat = (XGuiRemotePixelFormat)9;
    p.codec = (XGuiRemoteCodecId)77;
    p.zlibLevel = 0;
    p.tileWidth = 1;
    p.tileHeight = 9999;
    p.maxFps = 0;
    p.encodeQueueBytes = 1;
    p.txBudgetBytes = (size_t)-1;
    p.mouseMoveThrottleMs = 99999;
    p.pingIntervalMs = 1;
    p.pingTimeoutMs = 99999999u;
    XGuiRemoteProfile_sanitize(&p);
    xr_expect(p.wireFormat == XGUI_REMOTE_PF_ARGB32 &&
                  p.codec == XGUI_REMOTE_CODEC_RLE && p.zlibLevel == 1 &&
                  p.tileWidth == 16 && p.tileHeight == 512 && p.maxFps == 1 &&
                  p.encodeQueueBytes == 64u * 1024u &&
                  p.txBudgetBytes == XGUI_REMOTE_MAX_FRAME_BYTES &&
                  p.mouseMoveThrottleMs == 1000 &&
                  p.pingIntervalMs == 100 &&
                  p.pingTimeoutMs == 60u * 60u * 1000u,
              "sanitize 越界夹取到冻结边界");
    xr_expect(XGuiRemoteProfile_isValid(&p), "夹取后合法");

    /* enc/dec 回环（dec 侧已 sanitize）。 */
    XGuiRemoteProfile_initResource(&p);
    p.tileWidth = 48;
    p.maxFps = 33;
    n = XGuiRemoteProfile_enc(buf, sizeof(buf), &p);
    xr_expect(n == 32, "档位自定义块 32 字节冻结布局");
    xr_expect(XGuiRemoteProfile_dec(buf, n, &q) &&
                  q.wireFormat == p.wireFormat && q.codec == p.codec &&
                  q.zlibLevel == p.zlibLevel && q.tileWidth == 48 &&
                  q.tileHeight == p.tileHeight && q.maxFps == 33 &&
                  q.encodeQueueBytes == p.encodeQueueBytes &&
                  q.txBudgetBytes == p.txBudgetBytes &&
                  q.mouseMoveThrottleMs == p.mouseMoveThrottleMs &&
                  q.pingIntervalMs == p.pingIntervalMs &&
                  q.pingTimeoutMs == p.pingTimeoutMs,
              "档位 enc→dec 逐字段回环");
    xr_expect(!XGuiRemoteProfile_dec(buf, n - 1, &q), "档位块截断拒绝");
    xr_expect(XGuiRemoteProfile_enc(buf, 8, &p) == 0, "档位缓冲不足返回 0");
}

/** @brief 封闭枚举非法值 dec 返回 false（冻结双轨口径 §3.4）。 */
static void xr_test_closedEnums(void)
{
    uint8_t buf[2 * XGUI_REMOTE_MAX_TEXT_BYTES + 64];
    size_t n;

    { /* HELLO: authMethod 偏移 6。 */
        XGuiRemoteMsgHello m, o;
        xr_fillHello(&m, 1, XGUI_REMOTE_AUTH_NONE, "x");
        n = XGuiRemoteProto_encHello(buf, sizeof(buf), &m);
        buf[6] = 0xAB;
        xr_expect(!XGuiRemoteProto_decHello(buf, n, &o),
                  "HELLO 认证法非法 dec 拒绝");
    }
    { /* AUTH_CHALLENGE: method 偏移 0。 */
        XGuiRemoteMsgAuthChallenge m, o;
        memset(&m, 0, sizeof(m));
        m.method = XGUI_REMOTE_AUTH_NONE;
        m.nonceBytes = XGUI_REMOTE_AUTH_NONCE_BYTES;
        n = XGuiRemoteProto_encAuthChallenge(buf, sizeof(buf), &m);
        buf[0] = 0x7F;
        xr_expect(!XGuiRemoteProto_decAuthChallenge(buf, n, &o),
                  "AUTH_CHALLENGE 认证法非法 dec 拒绝");
    }
    { /* FB_META: format 偏移 4 / profileId 偏移 9。 */
        XGuiRemoteMsgFbMeta m, o;
        memset(&m, 0, sizeof(m));
        m.width = 8;
        m.height = 8;
        m.format = XGUI_REMOTE_PF_RGB565;
        m.tileWidth = 16;
        m.tileHeight = 16;
        m.profileId = XGUI_REMOTE_PROFILE_RESOURCE;
        n = XGuiRemoteProto_encFbMeta(buf, sizeof(buf), &m);
        buf[4] = 0x55;
        xr_expect(!XGuiRemoteProto_decFbMeta(buf, n, &o),
                  "FB_META 线上格式非法 dec 拒绝");
        buf[4] = XGUI_REMOTE_PF_RGB565;
        buf[9] = 0x03; /* 0..2 与 0xFF 之外的档位 id。 */
        xr_expect(!XGuiRemoteProto_decFbMeta(buf, n, &o),
                  "FB_META 档位 id 非法 dec 拒绝");
    }
    { /* FB_REQUEST: mode 偏移 0。 */
        XGuiRemoteMsgFbRequest m, o;
        memset(&m, 0, sizeof(m));
        m.mode = 0;
        n = XGuiRemoteProto_encFbRequest(buf, sizeof(buf), &m);
        buf[0] = 2;
        xr_expect(!XGuiRemoteProto_decFbRequest(buf, n, &o),
                  "FB_REQUEST 模式非法 dec 拒绝");
    }
    { /* FB_UPDATE 头: format 偏移 6；tile 记录: codec 偏移 8。 */
        XGuiRemoteMsgFbUpdate h, ho;
        XGuiRemoteMsgFbTile t, to;
        uint8_t px[2] = {0, 0};
        memset(&h, 0, sizeof(h));
        h.sequence = 1;
        h.tileCount = 1;
        h.format = XGUI_REMOTE_PF_ARGB32;
        n = XGuiRemoteProto_encFbUpdate(buf, sizeof(buf), &h);
        buf[6] = 0x7F;
        xr_expect(!XGuiRemoteProto_decFbUpdate(buf, n, &ho),
                  "FB_UPDATE 线上格式非法 dec 拒绝");
        buf[6] = XGUI_REMOTE_PF_ARGB32;
        memset(&t, 0, sizeof(t));
        t.x = 0;
        t.y = 0;
        t.w = 1;
        t.h = 1;
        t.codec = XGUI_REMOTE_CODEC_RAW;
        t.payload = px;
        t.payloadBytes = 2;
        n += XGuiRemoteProto_encFbTile(buf + n, sizeof(buf) - n, &t, px);
        buf[8 + 8] = 0x99; /* tile 记录 codec 字节(帧级头 8 + 记录内偏移 8)。 */
        xr_expect(!XGuiRemoteProto_decFbTile(buf, n,
                                             XGUI_REMOTE_FB_UPDATE_HEADER_BYTES,
                                             &to, NULL),
                  "tile 编解码器非法 dec 拒绝");
    }
    { /* INPUT_KEY: action 偏移 0。 */
        XGuiRemoteMsgInputKey m, o;
        memset(&m, 0, sizeof(m));
        m.action = XGUI_REMOTE_KEY_RELEASE;
        n = XGuiRemoteProto_encInputKey(buf, sizeof(buf), &m);
        buf[0] = 3;
        xr_expect(!XGuiRemoteProto_decInputKey(buf, n, &o),
                  "INPUT_KEY 动作非法 dec 拒绝");
    }
    { /* INPUT_POINTER: action 偏移 0。 */
        XGuiRemoteMsgInputPointer m, o;
        memset(&m, 0, sizeof(m));
        m.action = XGUI_REMOTE_PTR_MOVE;
        n = XGuiRemoteProto_encInputPointer(buf, sizeof(buf), &m);
        buf[0] = 4;
        xr_expect(!XGuiRemoteProto_decInputPointer(buf, n, &o),
                  "INPUT_POINTER 动作非法 dec 拒绝");
    }
    { /* INPUT_TOUCH: action 偏移 0。 */
        XGuiRemoteMsgInputTouch m, o;
        memset(&m, 0, sizeof(m));
        m.action = XGUI_REMOTE_TOUCH_BEGIN;
        m.pointCount = 0;
        n = XGuiRemoteProto_encInputTouch(buf, sizeof(buf), &m);
        buf[0] = 4;
        xr_expect(!XGuiRemoteProto_decInputTouch(buf, n, &o),
                  "INPUT_TOUCH 动作非法 dec 拒绝");
    }
    { /* PROFILE_SET/RESULT: profileId 偏移 0/1。 */
        XGuiRemoteMsgProfileSet m, o;
        XGuiRemoteMsgProfileResult r, ro;
        memset(&m, 0, sizeof(m));
        m.profileId = XGUI_REMOTE_PROFILE_PERFORMANCE;
        m.hasCustomProfile = 0;
        n = XGuiRemoteProto_encProfileSet(buf, sizeof(buf), &m);
        buf[0] = 0x7E;
        xr_expect(!XGuiRemoteProto_decProfileSet(buf, n, &o),
                  "PROFILE_SET 档位 id 非法 dec 拒绝");
        memset(&r, 0, sizeof(r));
        r.accepted = 1;
        r.profileId = XGUI_REMOTE_PROFILE_AUTO;
        n = XGuiRemoteProto_encProfileResult(buf, sizeof(buf), &r);
        buf[1] = 0x7E;
        xr_expect(!XGuiRemoteProto_decProfileResult(buf, n, &ro),
                  "PROFILE_RESULT 档位 id 非法 dec 拒绝");
    }
}

/* ==================== (b) 编解码单元 ==================== */

/** @brief RLE 冻结格式向量（XGuiRemote.md §5.4）。 */
static void xr_test_rleVectors(void)
{
    /* ARGB32(4B/单元) 向量：字面量 / 重复 / 混合，逐字节精确比对。 */
    static const uint32_t srcA[3] = {0xFF112233u, 0xFF445566u, 0xFF778899u};
    static const uint32_t srcB[5] = {0xFFABCDEFu, 0xFFABCDEFu, 0xFFABCDEFu,
                                     0xFFABCDEFu, 0xFFABCDEFu};
    /* 混合: A,B | C×4 | D —— 贪婪: <3 单元字面量, ≥3 重复段。 */
    static const uint32_t srcC[7] = {0xFF000001u, 0xFF000002u, 0xFF000003u,
                                     0xFF000003u, 0xFF000003u, 0xFF000003u,
                                     0xFF000004u};
    uint8_t enc[256];
    uint32_t dec[16];
    const uint8_t* u;
    int n, r;

    /* 1) 纯字面量 3 单元: [0x02][12B 原样]。 */
    n = XGuiRemoteCodec_encodeTile((const uint8_t*)srcA, 12,
                                   XGUI_REMOTE_PF_ARGB32, 3, 1,
                                   XGUI_REMOTE_PF_ARGB32,
                                   XGUI_REMOTE_CODEC_RLE, 1, enc, sizeof(enc));
    xr_expect(n == 13 && enc[0] == 0x02 && memcmp(enc + 1, srcA, 12) == 0,
              "RLE 字面量段向量(控制字节 c+1 单元)");
    memset(dec, 0, sizeof(dec));
    r = XGuiRemoteCodec_decodeTile(enc, (size_t)n, XGUI_REMOTE_CODEC_RLE,
                                   XGUI_REMOTE_PF_ARGB32, (uint8_t*)dec, 12,
                                   XGUI_REMOTE_PF_ARGB32, 3, 1);
    xr_expect(r == 0 && memcmp(dec, srcA, 12) == 0, "RLE 字面量段解码回环");

    /* 2) 纯重复 5 单元: [0x83][单元] (c-0x80+2=5 → c=0x83)。 */
    n = XGuiRemoteCodec_encodeTile((const uint8_t*)srcB, 20,
                                   XGUI_REMOTE_PF_ARGB32, 5, 1,
                                   XGUI_REMOTE_PF_ARGB32,
                                   XGUI_REMOTE_CODEC_RLE, 1, enc, sizeof(enc));
    u = (const uint8_t*)&srcB[0];
    xr_expect(n == 5 && enc[0] == 0x83 && memcmp(enc + 1, u, 4) == 0,
              "RLE 重复段向量(控制字节 c-0x80+2 次)");
    memset(dec, 0, sizeof(dec));
    r = XGuiRemoteCodec_decodeTile(enc, (size_t)n, XGUI_REMOTE_CODEC_RLE,
                                   XGUI_REMOTE_PF_ARGB32, (uint8_t*)dec, 20,
                                   XGUI_REMOTE_PF_ARGB32, 5, 1);
    xr_expect(r == 0 && memcmp(dec, srcB, 20) == 0, "RLE 重复段解码回环");

    /* 3) 混合: [0x01 A B][0x82 C][0x00 D]。 */
    n = XGuiRemoteCodec_encodeTile((const uint8_t*)srcC, 28,
                                   XGUI_REMOTE_PF_ARGB32, 7, 1,
                                   XGUI_REMOTE_PF_ARGB32,
                                   XGUI_REMOTE_CODEC_RLE, 1, enc, sizeof(enc));
    xr_expect(n == (1 + 8) + (1 + 4) + (1 + 4) && enc[0] == 0x01 &&
                  enc[1 + 8] == 0x82 && enc[6 + 8] == 0x00,
              "RLE 混合向量(贪婪: ≥3 重复, 否则字面量)");
    memset(dec, 0, sizeof(dec));
    r = XGuiRemoteCodec_decodeTile(enc, (size_t)n, XGUI_REMOTE_CODEC_RLE,
                                   XGUI_REMOTE_PF_ARGB32, (uint8_t*)dec, 28,
                                   XGUI_REMOTE_PF_ARGB32, 7, 1);
    xr_expect(r == 0 && memcmp(dec, srcC, 28) == 0, "RLE 混合解码回环");

    /* 4) RGB565(2B/单元) 重复向量。 */
    {
        static const uint16_t s16[4] = {0x1234, 0x1234, 0x1234, 0x1234};
        uint16_t d16[8];
        n = XGuiRemoteCodec_encodeTile((const uint8_t*)s16, 8,
                                       XGUI_REMOTE_PF_RGB565, 4, 1,
                                       XGUI_REMOTE_PF_RGB565,
                                       XGUI_REMOTE_CODEC_RLE, 1, enc,
                                       sizeof(enc));
        xr_expect(n == 3 && enc[0] == 0x82 && enc[1] == 0x34 && enc[2] == 0x12,
                  "RLE RGB565 重复段向量(小端单元, 1 控制字节+2B 单元)");
        memset(d16, 0, sizeof(d16));
        r = XGuiRemoteCodec_decodeTile(enc, (size_t)n, XGUI_REMOTE_CODEC_RLE,
                                       XGUI_REMOTE_PF_RGB565, (uint8_t*)d16, 8,
                                       XGUI_REMOTE_PF_RGB565, 4, 1);
        xr_expect(r == 0 && d16[0] == 0x1234 && d16[3] == 0x1234,
                  "RLE RGB565 解码回环");

        /* 5) 截断/越界/尾随垃圾 → CORRUPT。 */
        r = XGuiRemoteCodec_decodeTile(enc, (size_t)n - 1,
                                       XGUI_REMOTE_CODEC_RLE,
                                       XGUI_REMOTE_PF_RGB565, (uint8_t*)d16, 8,
                                       XGUI_REMOTE_PF_RGB565, 4, 1);
        xr_expect(r == -XGUI_REMOTE_CODEC_ERR_CORRUPT, "RLE 截断流报 CORRUPT");
        {
            uint8_t junk[8];
            memcpy(junk, enc, (size_t)n);
            junk[n] = 0x00; /* 尾随字面量控制字节却无数据。 */
            r = XGuiRemoteCodec_decodeTile(junk, (size_t)n + 1,
                                           XGUI_REMOTE_CODEC_RLE,
                                           XGUI_REMOTE_PF_RGB565,
                                           (uint8_t*)d16, 8,
                                           XGUI_REMOTE_PF_RGB565, 4, 1);
            xr_expect(r == -XGUI_REMOTE_CODEC_ERR_CORRUPT,
                      "RLE 尾随垃圾报 CORRUPT");
        }
        {
            /* 重复段声明 4 单元 > 3 宽 tile 容量。 */
            uint8_t bad[3] = {0x82, 0x34, 0x12};
            r = XGuiRemoteCodec_decodeTile(bad, 3, XGUI_REMOTE_CODEC_RLE,
                                           XGUI_REMOTE_PF_RGB565,
                                           (uint8_t*)d16, 8,
                                           XGUI_REMOTE_PF_RGB565, 3, 1);
            xr_expect(r == -XGUI_REMOTE_CODEC_ERR_CORRUPT,
                      "RLE 越过 tile 边界报 CORRUPT");
        }
    }

    /* 6) maxEncodedSize 预算 ≥ 128 全异单元最坏字面量编码。 */
    {
        static const uint32_t rand32[128] = {
            0xFF000001u, 0xFF000002u, 0xFF000003u, 0xFF000004u, 0xFF000005u,
            0xFF000006u, 0xFF000007u, 0xFF000008u, 0xFF000009u, 0xFF00000Au,
            0xFF00000Bu, 0xFF00000Cu, 0xFF00000Du, 0xFF00000Eu, 0xFF00000Fu,
            0xFF000010u, 0xFF000011u, 0xFF000012u, 0xFF000013u, 0xFF000014u,
            0xFF000015u, 0xFF000016u, 0xFF000017u, 0xFF000018u, 0xFF000019u,
            0xFF00001Au, 0xFF00001Bu, 0xFF00001Cu, 0xFF00001Du, 0xFF00001Eu,
            0xFF00001Fu, 0xFF000020u, 0xFF000021u, 0xFF000022u, 0xFF000023u,
            0xFF000024u, 0xFF000025u, 0xFF000026u, 0xFF000027u, 0xFF000028u,
            0xFF000029u, 0xFF00002Au, 0xFF00002Bu, 0xFF00002Cu, 0xFF00002Du,
            0xFF00002Eu, 0xFF00002Fu, 0xFF000030u, 0xFF000031u, 0xFF000032u,
            0xFF000033u, 0xFF000034u, 0xFF000035u, 0xFF000036u, 0xFF000037u,
            0xFF000038u, 0xFF000039u, 0xFF00003Au, 0xFF00003Bu, 0xFF00003Cu,
            0xFF00003Du, 0xFF00003Eu, 0xFF00003Fu, 0xFF000040u, 0xFF000041u,
            0xFF000042u, 0xFF000043u, 0xFF000044u, 0xFF000045u, 0xFF000046u,
            0xFF000047u, 0xFF000048u, 0xFF000049u, 0xFF00004Au, 0xFF00004Bu,
            0xFF00004Cu, 0xFF00004Du, 0xFF00004Eu, 0xFF00004Fu, 0xFF000050u,
            0xFF000051u, 0xFF000052u, 0xFF000053u, 0xFF000054u, 0xFF000055u,
            0xFF000056u, 0xFF000057u, 0xFF000058u, 0xFF000059u, 0xFF00005Au,
            0xFF00005Bu, 0xFF00005Cu, 0xFF00005Du, 0xFF00005Eu, 0xFF00005Fu,
            0xFF000060u, 0xFF000061u, 0xFF000062u, 0xFF000063u, 0xFF000064u,
            0xFF000065u, 0xFF000066u, 0xFF000067u, 0xFF000068u, 0xFF000069u,
            0xFF00006Au, 0xFF00006Bu, 0xFF00006Cu, 0xFF00006Du, 0xFF00006Eu,
            0xFF00006Fu, 0xFF000070u, 0xFF000071u, 0xFF000072u, 0xFF000073u,
            0xFF000074u, 0xFF000075u, 0xFF000076u, 0xFF000077u, 0xFF000078u,
            0xFF000079u, 0xFF00007Au, 0xFF00007Bu, 0xFF00007Cu, 0xFF00007Du,
            0xFF00007Eu, 0xFF00007Fu, 0xFF000080u
        };
        size_t cap = XGuiRemoteCodec_maxEncodedSize(XGUI_REMOTE_CODEC_RLE, 128,
                                                    1, 4);
        uint8_t* bigBuf = (uint8_t*)XMalloc_System(cap ? cap : 1);
        xr_expect(cap >= 128u * 4u + 1u, "RLE maxEncodedSize ≥ RAW");
        if (bigBuf) {
            int en = XGuiRemoteCodec_encodeTile(
                (const uint8_t*)rand32, 512, XGUI_REMOTE_PF_ARGB32, 128, 1,
                XGUI_REMOTE_PF_ARGB32, XGUI_REMOTE_CODEC_RLE, 1, bigBuf, cap);
            xr_expect(en > 0 && (size_t)en <= cap,
                      "128 全异单元最坏字面量在预算内");
            XFree_System(bigBuf);
        }
    }

    /* 7) 跨行重复(2026-10-04 行游标编码器加测): 3 宽×2 高, 行主序线性流
     *    A B C C C D —— 重复段从行 0 末单元跨到行 1(单元流跨行连续)。
     *    贪婪: [lit A B][rep C×3][lit D] = [0x01][AB][0x81][C][0x00][D]。 */
    {
        static const uint16_t xs[6] = {0x0A0A, 0x0B0B, 0x0C0C,
                                       0x0C0C, 0x0C0C, 0x0D0D};
        uint16_t xd[6];
        n = XGuiRemoteCodec_encodeTile((const uint8_t*)xs, 6,
                                       XGUI_REMOTE_PF_RGB565, 3, 2,
                                       XGUI_REMOTE_PF_RGB565,
                                       XGUI_REMOTE_CODEC_RLE, 1, enc,
                                       sizeof(enc));
        xr_expect(n == (1 + 4) + (1 + 2) + (1 + 2) && enc[0] == 0x01 &&
                      enc[5] == 0x81 && enc[8] == 0x00,
                  "RLE 跨行重复段向量(单元流跨行连续)");
        memset(xd, 0, sizeof(xd));
        r = XGuiRemoteCodec_decodeTile(enc, (size_t)n, XGUI_REMOTE_CODEC_RLE,
                                       XGUI_REMOTE_PF_RGB565, (uint8_t*)xd, 12,
                                       XGUI_REMOTE_PF_RGB565, 3, 2);
        xr_expect(r == 0 && memcmp(xd, xs, sizeof(xs)) == 0,
                  "RLE 跨行重复解码回环");
    }

    /* 8) 跨行字面量 + stride 填充跳过: 3 宽×2 高 stride=10(行尾 4B 填充),
     *    6 全异单元字面量段跨行 —— 字面量按行分块复制, 填充不上网。 */
    {
        static const uint16_t xf[6] = {0x0102, 0x0304, 0x0506,
                                       0x0708, 0x090A, 0x0B0C};
        uint16_t fd[6];
        n = XGuiRemoteCodec_encodeTile((const uint8_t*)xf, 10,
                                       XGUI_REMOTE_PF_RGB565, 3, 2,
                                       XGUI_REMOTE_PF_RGB565,
                                       XGUI_REMOTE_CODEC_RLE, 1, enc,
                                       sizeof(enc));
        xr_expect(n == 1 + 12 && enc[0] == 0x05 &&
                      memcmp(enc + 1, xf, 12) == 0,
                  "RLE 跨行字面量向量(stride 填充跳过)");
        memset(fd, 0, sizeof(fd));
        r = XGuiRemoteCodec_decodeTile(enc, (size_t)n, XGUI_REMOTE_CODEC_RLE,
                                       XGUI_REMOTE_PF_RGB565, (uint8_t*)fd, 12,
                                       XGUI_REMOTE_PF_RGB565, 3, 2);
        xr_expect(r == 0 && memcmp(fd, xf, sizeof(xf)) == 0,
                  "RLE 跨行字面量解码回环");
    }

    /* 9) 重复段封顶 129 + 行回绕: 2 宽×65 高(130 单元)全同值 →
     *    [0xFF][u](129 次) + [0x00][u](1 次) = 6 字节。 */
    {
        static uint16_t flat[130]; /* 零初始化后全量填充。 */
        uint16_t fd2[130];
        size_t k;
        for (k = 0; k < 130u; ++k) {
            flat[k] = 0xFEEB;
        }
        n = XGuiRemoteCodec_encodeTile((const uint8_t*)flat, 4,
                                       XGUI_REMOTE_PF_RGB565, 2, 65,
                                       XGUI_REMOTE_PF_RGB565,
                                       XGUI_REMOTE_CODEC_RLE, 1, enc,
                                       sizeof(enc));
        xr_expect(n == 6 && enc[0] == 0xFF && enc[1] == 0xEB &&
                      enc[2] == 0xFE && enc[3] == 0x00 && enc[4] == 0xEB &&
                      enc[5] == 0xFE,
                  "RLE 重复段 129 封顶向量(余量转字面量)");
        memset(fd2, 0, sizeof(fd2));
        r = XGuiRemoteCodec_decodeTile(enc, (size_t)n, XGUI_REMOTE_CODEC_RLE,
                                       XGUI_REMOTE_PF_RGB565, (uint8_t*)fd2,
                                       sizeof(fd2), XGUI_REMOTE_PF_RGB565, 2,
                                       65);
        xr_expect(r == 0 && fd2[0] == 0xFEEB && fd2[129] == 0xFEEB,
                  "RLE 129 封顶解码回环");
    }
}

/** @brief zlib 编解码回环（XGUI_REMOTE_ZLIB_ON=1 默认编译在内）。 */
static void xr_test_zlibRoundtrip(void)
{
#if XGUI_REMOTE_ZLIB_ON
    static const uint32_t src[64 * 33]; /* 零初始化: 大面积可压内容。 */
    uint8_t enc[64 * 33 * 4 + 64];
    uint32_t dec[64 * 33];
    size_t cap;
    int n, r;
    cap = XGuiRemoteCodec_maxEncodedSize(XGUI_REMOTE_CODEC_ZLIB, 64, 33, 4);
    xr_expect(cap >= 64u * 33u * 4u, "zlib maxEncodedSize ≥ RAW 上限");
    n = XGuiRemoteCodec_encodeTile((const uint8_t*)src, 64 * 4,
                                   XGUI_REMOTE_PF_ARGB32, 64, 33,
                                   XGUI_REMOTE_PF_ARGB32,
                                   XGUI_REMOTE_CODEC_ZLIB, 6, enc, sizeof(enc));
    xr_expect(n > 0 && (size_t)n < sizeof(src), "zlib 编码且零页高压缩");
    memset(dec, 0xAA, sizeof(dec));
    r = XGuiRemoteCodec_decodeTile(enc, (size_t)n, XGUI_REMOTE_CODEC_ZLIB,
                                   XGUI_REMOTE_PF_ARGB32, (uint8_t*)dec,
                                   64 * 4, XGUI_REMOTE_PF_ARGB32, 64, 33);
    xr_expect(r == 0 && dec[0] == 0 && dec[64 * 33 - 1] == 0,
              "zlib 解码回环");
    r = XGuiRemoteCodec_decodeTile(enc, (size_t)n / 2, XGUI_REMOTE_CODEC_ZLIB,
                                   XGUI_REMOTE_PF_ARGB32, (uint8_t*)dec,
                                   64 * 4, XGUI_REMOTE_PF_ARGB32, 64, 33);
    xr_expect(r == -XGUI_REMOTE_CODEC_ERR_CORRUPT, "zlib 截断流报 CORRUPT");
    xr_expect(XGuiRemoteCodec_hasCodec(XGUI_REMOTE_CODEC_ZLIB),
              "ZLIB 在编时 hasCodec=true");
#else
    xr_expect(!XGuiRemoteCodec_hasCodec(XGUI_REMOTE_CODEC_ZLIB),
              "ZLIB 裁剪时 hasCodec=false");
#endif
}

/** @brief ARGB32↔RGB565 转换无损性 + tileHash 稳定性。
 *  @note  冻结口径(XGuiRemoteCodec.h): 565→ARGB32 高位位复制展开, 使
 *         "不透明像素往返位相等"对**全部 565 输入**成立(展开后再编码
 *         565 精确还原); 反向(ARGB32→565→ARGB32)仅对低 3/2 位为零的
 *         8 bit 分量精确(565 量化丢弃低位)。两向各测其成立域。 */
static void xr_test_convertAndHash(void)
{
    uint8_t argb[4 * 512];
    uint8_t mid[2 * 512];
    uint8_t back[4 * 512];
    uint8_t argb2[4 * 512];
    int i;
    int r, g, b;

    /* 1) 565 全体入射: 565→ARGB32→565 逐位还原(展开的位复制可逆)。 */
    for (i = 0; i < 512; ++i) {
        uint16_t v = (uint16_t)((i * 3717) & 0xFFFFu);
        mid[i * 2 + 0] = (uint8_t)(v & 0xFFu);        /* 565 小端。 */
        mid[i * 2 + 1] = (uint8_t)(v >> 8);
    }
    XGuiRemoteCodec_convertPixels(mid, XGUI_REMOTE_PF_RGB565, argb,
                                  XGUI_REMOTE_PF_ARGB32, 512);
    xr_expect(argb[3] == 0xFFu && argb[7] == 0xFFu, "565→ARGB32 恒不透明");
    XGuiRemoteCodec_convertPixels(argb, XGUI_REMOTE_PF_ARGB32, back,
                                  XGUI_REMOTE_PF_RGB565, 512);
    xr_expect(memcmp(mid, back, 2 * 512) == 0,
              "RGB565→ARGB32→RGB565 全输入逐位还原(高位位复制展开)");

    /* 2) ARGB32 入射: 往返结果 == 冻结量化+展开公式的规范像。
     *    量化: r5=r>>3, g6=g>>2, b5=b>>3; 展开: r'=(r5<<3)|(r5>>2) 等
     *    (XGuiRemoteCodec.h 冻结口径)。仅 565 规范形输入才逐位回到原值
     *    (由断言 1 覆盖), 此处验证量化域上的公式一致性。 */
    i = 0;
    for (r = 0; r <= 255; r += 51) {
        for (g = 0; g <= 255; g += 51) {
            for (b = 0; b <= 255; b += 51) {
                int k;
                for (k = 0; k < 8 && i < 512; ++k, ++i) {
                    argb[i * 4 + 0] = (uint8_t)(b + k); /* 内存序 B,G,R,A。 */
                    argb[i * 4 + 1] = (uint8_t)(g + k);
                    argb[i * 4 + 2] = (uint8_t)(r + k);
                    argb[i * 4 + 3] = 0xFFu;
                }
            }
        }
    }
    for (; i < 512; ++i) {
        argb[i * 4 + 0] = (uint8_t)i;
        argb[i * 4 + 1] = (uint8_t)(255 - i);
        argb[i * 4 + 2] = (uint8_t)(i * 3);
        argb[i * 4 + 3] = 0xFFu;
    }
    memcpy(argb2, argb, 4 * 512);
    XGuiRemoteCodec_convertPixels(argb2, XGUI_REMOTE_PF_ARGB32, mid,
                                  XGUI_REMOTE_PF_RGB565, 512);
    XGuiRemoteCodec_convertPixels(mid, XGUI_REMOTE_PF_RGB565, back,
                                  XGUI_REMOTE_PF_ARGB32, 512);
    {
        bool formulaOk = true;
        int j;
        for (j = 0; j < 512 && formulaOk; ++j) {
            uint8_t r5 = (uint8_t)(argb2[j * 4 + 2] >> 3);
            uint8_t g6 = (uint8_t)(argb2[j * 4 + 1] >> 2);
            uint8_t b5 = (uint8_t)(argb2[j * 4 + 0] >> 3);
            uint8_t er = (uint8_t)((r5 << 3) | (r5 >> 2));
            uint8_t eg = (uint8_t)((g6 << 2) | (g6 >> 4));
            uint8_t eb = (uint8_t)((b5 << 3) | (b5 >> 2));
            if (back[j * 4 + 0] != eb || back[j * 4 + 1] != eg ||
                back[j * 4 + 2] != er || back[j * 4 + 3] != 0xFFu) {
                formulaOk = false;
            }
        }
        xr_expect(formulaOk,
                  "ARGB32→RGB565→ARGB32 == 量化+高位位复制展开的规范像"
                  "(不透明)");
    }

    /* 同格式直拷。 */
    memcpy(back, argb, 4 * 512);
    memset(argb, 0x5A, 4 * 512);
    XGuiRemoteCodec_convertPixels(back, XGUI_REMOTE_PF_ARGB32, argb,
                                  XGUI_REMOTE_PF_ARGB32, 512);
    xr_expect(memcmp(argb, back, 4 * 512) == 0, "同格式转换直拷");

    /* tileHash: FNV-1a 32 位——偏移初值/单字节解析值/稳定性/区分度。 */
    xr_expect(XGuiRemoteCodec_tileHash(NULL, 100) == 2166136261u,
              "tileHash 空指针返回偏移初值");
    xr_expect(XGuiRemoteCodec_tileHash((const uint8_t*)"a", 1) ==
                  (2166136261u ^ 0x61u) * 16777619u,
              "tileHash FNV-1a 解析值('a')");
    xr_expect(XGuiRemoteCodec_tileHash((const uint8_t*)"abc", 3) ==
                  XGuiRemoteCodec_tileHash((const uint8_t*)"abc", 3),
              "tileHash 同内容稳定");
    xr_expect(XGuiRemoteCodec_tileHash((const uint8_t*)"abc", 3) !=
                  XGuiRemoteCodec_tileHash((const uint8_t*)"abd", 3),
              "tileHash 异内容区分");
    xr_expect(XGuiRemoteCodec_tileHash((const uint8_t*)"\0\0\0\0", 4) !=
                  XGuiRemoteCodec_tileHash((const uint8_t*)"\0\0\0\1", 4),
              "tileHash 逐字节敏感(含零字节)");
}

/* ==================== (c) 回环设备语义 ==================== */

static void xr_test_loopbackDevice(void)
{
    XGuiRemoteLoopbackDevice* peer = NULL;
    XGuiRemoteLoopbackDevice* a = XGuiRemoteLoopbackDevice_createPair(64,
                                                                      &peer);
    char buf[128];
    int64_t n;

    xr_expect(a != NULL && peer != NULL, "回环设备对创建");
    if (!a || !peer) return;
    xr_expect(XGuiRemoteLoopbackDevice_peerAlive(a) &&
                  XGuiRemoteLoopbackDevice_peerAlive(peer),
              "双端初始互为存活");

    /* A 写 → B 读；全有全无：超容量整块拒写。 */
    n = XIODevice_write_1((XIODevice*)a, "abcdef", 6);
    xr_expect(n == 6, "A 写入 6 字节");
    xr_expect(XGuiRemoteLoopbackDevice_bufferedBytes(peer) == 6,
              "对端缓冲可见 6 字节");
    n = XIODevice_write_1(
        (XIODevice*)a, "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz",
        62);
    xr_expect(n == 0, "全有全无: 剩余 58 < 62 一个字节都不写");
    xr_expect(XGuiRemoteLoopbackDevice_bufferedBytes(peer) == 6,
              "拒写后对端缓冲不变");
    memset(buf, 0, sizeof(buf));
    n = XIODevice_read_1((XIODevice*)peer, buf, (int64_t)sizeof(buf));
    xr_expect(n == 6 && memcmp(buf, "abcdef", 6) == 0, "B 读出 A 写入内容");

    /* 腾空后可写; 部分读。 */
    n = XIODevice_write_1((XIODevice*)a, "XYZ", 3);
    xr_expect(n == 3, "腾空后可写");
    n = XIODevice_read_1((XIODevice*)peer, buf, 2);
    xr_expect(n == 2 && buf[0] == 'X' && buf[1] == 'Y', "部分读出");
    n = XIODevice_read_1((XIODevice*)peer, buf, (int64_t)sizeof(buf));
    xr_expect(n == 1 && buf[0] == 'Z', "余量读尽");

    /* shutdown: 本端清空并对端 EOF 视图。 */
    XIODevice_write_1((XIODevice*)a, "123", 3);
    XGuiRemoteLoopbackDevice_shutdown(peer);
    xr_expect(XGuiRemoteLoopbackDevice_bufferedBytes(peer) == 0,
              "shutdown 清空本端缓冲");
    xr_expect(!XGuiRemoteLoopbackDevice_peerAlive(a), "对端视角存活转 false");
    n = XIODevice_read_1((XIODevice*)a, buf, (int64_t)sizeof(buf));
    xr_expect(n == 0, "对端关闭后读 EOF");
    n = XIODevice_write_1((XIODevice*)a, "987", 3);
    xr_expect(n == 0, "对端关闭后写恒 0");

    /* 任意一端 delete 安全; 末删者释放共享块(对端不悬垂)。 */
    XClassDelete((XClass*)a);
    xr_expect(!XGuiRemoteLoopbackDevice_peerAlive(peer),
              "单端 delete 后对端视角存活 false");
    n = XIODevice_read_1((XIODevice*)peer, buf, (int64_t)sizeof(buf));
    xr_expect(n == 0, "对端 delete 后读 EOF");
    XClassDelete((XClass*)peer);
}

/* ==================== (c) 回环端到端（零网络） ==================== */

/**
 * 端到端拓扑（同一时刻至多一份存活, 顺序用例; 信号槽 XSlotFunc2 以
 * 发送者为首参不带上下文, 故经 xr_cur 路由到当前拓扑的观测计数）。
 */
typedef struct XrE2e {
    XWidget* top;        /* 服务端镜像树（顶层）。 */
    XWidget* childA;     /* 采样色块 A。 */
    XWidget* childB;     /* 采样色块 B。 */
    XCheckBox* box;      /* 远端点击目标。 */
    XLabel* noise;       /* 噪声标签(丢批场景; 其余场景 NULL)。 */
    XPixmap noisePm[2];  /* 交替噪声像素图(内容可区分)。 */
    XGuiServer* server;
    int sessionId;
    XGuiClient* client;
    XGuiRemoteLoopbackDevice* devA;
    XGuiRemoteLoopbackDevice* devB;
} XrE2e;

/* 观测计数（setup 复位; 槽自增）。 */
static XrE2e* xr_cur = NULL;
static int xr_clickCount;
static int xr_srvConnected, xr_srvDisconnected, xr_srvErrors;
static int xr_srvLastErrorCode, xr_srvLastDisconnectReason;
static int xr_cliConnected, xr_cliDisconnected, xr_cliErrors, xr_cliMeta;

static void xr_resetCounters(void)
{
    xr_clickCount = 0;
    xr_srvConnected = 0;
    xr_srvDisconnected = 0;
    xr_srvErrors = 0;
    xr_srvLastErrorCode = -1;
    xr_srvLastDisconnectReason = -1;
    xr_cliConnected = 0;
    xr_cliDisconnected = 0;
    xr_cliErrors = 0;
    xr_cliMeta = 0;
}

static void xr_onBoxClicked(XObject* sender, XVarList* args)
{
    (void)sender;
    (void)args;
    ++xr_clickCount;
}
static void xr_onSrvConnected(XObject* sender, XVarList* args)
{
    (void)sender;
    if (!args) return;
    ++xr_srvConnected;
}
static void xr_onSrvDisconnected(XObject* sender, XVarList* args)
{
    (void)sender;
    if (!args) return;
    XVarList_args_2(args, int, sid, int, reason);
    (void)sid;
    xr_srvLastDisconnectReason = reason;
    ++xr_srvDisconnected;
}
static void xr_onSrvError(XObject* sender, XVarList* args)
{
    (void)sender;
    if (!args) return;
    XVarList_args_2(args, int, sid, int, code);
    (void)sid;
    xr_srvLastErrorCode = code;
    ++xr_srvErrors;
}
static void xr_onCliConnected(XObject* sender, XVarList* args)
{
    (void)sender;
    (void)args;
    ++xr_cliConnected;
}
static void xr_onCliDisconnected(XObject* sender, XVarList* args)
{
    (void)sender;
    (void)args;
    ++xr_cliDisconnected;
}
static void xr_onCliError(XObject* sender, XVarList* args)
{
    (void)sender;
    if (!args) return;
    ++xr_cliErrors;
}
static void xr_onCliMeta(XObject* sender, XVarList* args)
{
    (void)sender;
    (void)args;
    ++xr_cliMeta;
}

/** @brief 服务端树着色工具（不透明纯色, 采样正控）。 */
static void xr_paintColor(XWidget* w, int r, int g, int b)
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

/** @brief 生成 4×4 块粒度噪声像素图（seed 区分两幅内容）。 */
static void xr_fillNoiseImage(XImage* img, int w, int h, uint32_t seed)
{
    int x, y;
    for (y = 0; y < h; y += 4) {
        for (x = 0; x < w; x += 4) {
            XRect rc;
            uint32_t v = 0xFF000000u |
                         (((x * 73856093u) ^ (y * 19349663u) ^ seed) &
                          0xFFFFFFu);
            XRect_init(&rc, x, y, 4, 4);
            XImage_fillRect(img, &rc, v);
        }
    }
}

/** @brief 构建端到端拓扑（镜像树 + 回环对 + 双端会话; 未泵握手）。
 *  @param ringBytes       回环每端容量（小容量逼出全有全无写）。
 *  @param w, h            镜像窗口尺寸。
 *  @param withNoise       true 时叠放 128×96 噪声标签（丢批场景主体）。
 *  @param profileOverride 非 NULL 时在 attach 前定版服务端档位（新会话
 *                         于 HELLO 时采纳, 不产生换档二次 FB_META）。 */
static bool xr_e2e_setup(XrE2e* e, size_t ringBytes, int w, int h,
                         bool withNoise, const XGuiRemoteProfile* profileOverride)
{
    XGuiRemoteLoopbackDevice* devB = NULL;
    memset(e, 0, sizeof(*e));
    xr_ensureApp();
    xr_resetCounters();
    xr_cur = e;

    /* 1) 服务端镜像树（离屏顶层 + 两个色块子控件 + 点击目标）。 */
    e->top = XWidget_create(NULL, 0);
    if (!e->top) return false;
    XWidget_resize(e->top, w, h);
    xr_paintColor(e->top, 0x21, 0x53, 0x9B); /* 深蓝底。 */
    e->childA = XWidget_create(e->top, 0);
    XWidget_setGeometry(e->childA, 12, 12, w / 3, h / 3);
    xr_paintColor(e->childA, 0xD9, 0x30, 0x25); /* 红。 */
    XWidget_show(e->childA);
    e->childB = XWidget_create(e->top, 0);
    XWidget_setGeometry(e->childB, w - 12 - w / 4, h - 12 - h / 4, w / 4,
                        h / 4);
    xr_paintColor(e->childB, 0x2E, 0x9E, 0x44); /* 绿。 */
    XWidget_show(e->childB);
    e->box = XCheckBox_create(e->top, 0);
    XWidget_setGeometry((XWidget*)e->box, w * 2 / 5, h / 2 - 14, 96, 28);
    XWidget_show((XWidget*)e->box);
    XObject_connect_2((XObject*)e->box,
                      XSignal(XAbstractButton_clicked_signal), xr_onBoxClicked);

    /* 噪声标签（不可压字节量主体: 128×96×4B ≈ 48KiB/整幅批）。 */
    if (withNoise) {
        XImage* img = XImage_create_ex(XCLASS_DEFAULT_MEMORY_TYPE);
        int i;
        e->noise = XLabel_create(e->top, 0);
        XWidget_setGeometry((XWidget*)e->noise, 0, 0, 128, 96);
        for (i = 0; i < 2; ++i) {
            XPixmap_init(&e->noisePm[i]);
            if (img && XImage_reinit_ex(img, 128, 96, XImageFormat_ARGB32)) {
                xr_fillNoiseImage(img, 128, 96, i ? 0x5EED0000u : 0x00FA0000u);
                XPixmap_init_image(&e->noisePm[i], img, 0);
            }
        }
        if (img) XClassDelete(img);
        XLabel_setPixmap(e->noise, &e->noisePm[0]);
        XWidget_show((XWidget*)e->noise);
    }

    /* 顶层 show: 子控件生效可见(m_visible 含父链) + 原生窗/后备存储就绪。
     * repaint 同步走 paint→flush; present 回调此刻尚未登记(host 在后)。 */
    XWidget_show(e->top);
    XWidget_repaint(e->top);

    /* 2) 服务器: (可选)attach 前定版档位 → 绑定镜像 → 直挂回环 A 端。 */
    e->server = XGuiServer_create(NULL);
    if (!e->server) return false;
    if (profileOverride) XGuiServer_setProfile(e->server, profileOverride);
    if (!XGuiServer_host(e->server, e->top)) {
        xr_expect(false, "XGuiServer_host 绑定镜像顶层");
        return false;
    }
    xr_expect(XGuiServer_hostedWidget(e->server) == e->top,
              "hostedWidget 回读");
    e->devA = XGuiRemoteLoopbackDevice_createPair(ringBytes, &devB);
    if (!e->devA || !devB) return false;
    e->devB = devB;
    e->sessionId = XGuiServer_attachTransport(e->server, (XIODevice*)e->devA);
    xr_expect(e->sessionId > 0, "attachTransport 建会话");
    if (e->sessionId <= 0) return false;
    xr_expect(XGuiServer_sessionCount(e->server) == 1, "服务端会话计数 1");
    XObject_connect_2((XObject*)e->server,
                      XSignal(XGuiServer_clientConnected_signal),
                      xr_onSrvConnected);
    XObject_connect_2((XObject*)e->server,
                      XSignal(XGuiServer_clientDisconnected_signal),
                      xr_onSrvDisconnected);
    XObject_connect_2((XObject*)e->server,
                      XSignal(XGuiServer_sessionError_signal), xr_onSrvError);

    /* 3) 客户端: 顶层控件直挂回环 B 端。 */
    e->client = XGuiClient_create(NULL, 0);
    if (!e->client) return false;
    XGuiClient_setTransport(e->client, (XIODevice*)e->devB);
    xr_expect(XGuiClient_state(e->client) == XGUI_REMOTE_STATE_BANNER_WAIT ||
                  XGuiClient_state(e->client) == XGUI_REMOTE_STATE_HANDSHAKING,
              "setTransport 后进入横幅/握手态");
    XObject_connect_2((XObject*)e->client,
                      XSignal(XGuiClient_connected_signal), xr_onCliConnected);
    XObject_connect_2((XObject*)e->client,
                      XSignal(XGuiClient_disconnected_signal),
                      xr_onCliDisconnected);
    XObject_connect_2((XObject*)e->client,
                      XSignal(XGuiClient_errorOccurred_signal), xr_onCliError);
    XObject_connect_2((XObject*)e->client,
                      XSignal(XGuiClient_remoteMetaChanged_signal),
                      xr_onCliMeta);
    return true;
}

/** @brief 拆除端到端拓扑（BYE 已完成会话关闭; 设备末删）。 */
static void xr_e2e_teardown(XrE2e* e)
{
    int i;
    if (e->client) XClassDelete(e->client);
    if (e->server) XClassDelete(e->server);
    if (e->devA) XClassDelete((XClass*)e->devA);
    if (e->devB) XClassDelete((XClass*)e->devB);
    for (i = 0; i < 2; ++i) XClassDeinit(&e->noisePm[i]);
    if (e->top) XClassDelete(e->top);
    if (xr_cur == e) xr_cur = NULL;
    memset(e, 0, sizeof(*e));
}

typedef struct XrStreamCond {
    uint32_t baseTiles; /* 触发时已收 tile 基线。 */
    uint32_t wantMore;  /* 期望再收到的 tile 数。 */
} XrStreamCond;

static uint32_t xr_clientTiles(void)
{
    XGuiRemoteStats st;
    XGuiClient_statistics(xr_cur ? xr_cur->client : NULL, &st);
    return st.tileCount;
}

/** @brief 泵至客户端 tile/update 计数 stableMs 毫秒无增长(在途帧排干),
 *         上限 timeoutMs。返回稳定时的 tile 计数。 */
static uint32_t xr_waitStatsStable(int stableMs, int timeoutMs)
{
    XGuiRemoteStats st;
    uint32_t lastTiles;
    uint32_t lastUpdates;
    int64_t lastChange = xr_nowMs();
    int64_t deadline = xr_nowMs() + timeoutMs;
    XGuiClient_statistics(xr_cur ? xr_cur->client : NULL, &st);
    lastTiles = st.tileCount;
    lastUpdates = st.updateCount;
    while (xr_nowMs() < deadline) {
        XGuiApplication_processEvents(XEventLoop_AllEvents);
        XThread_msleep(5);
        XGuiClient_statistics(xr_cur ? xr_cur->client : NULL, &st);
        if (st.tileCount != lastTiles || st.updateCount != lastUpdates) {
            lastTiles = st.tileCount;
            lastUpdates = st.updateCount;
            lastChange = xr_nowMs();
        }
        else if (xr_nowMs() - lastChange >= stableMs) {
            break;
        }
    }
    return lastTiles;
}

/** @brief 等待条件: 已连接且 tile 计数 ≥ 基线+期望增量。 */
static bool xr_streamed(void* ud)
{
    XrStreamCond* c = (XrStreamCond*)ud;
    if (!xr_cur || !xr_cliConnected) return false;
    if (!XGuiClient_isRemoteAlive(xr_cur->client)) return false;
    return xr_clientTiles() >= c->baseTiles + c->wantMore;
}

/** @brief 读抓取图像像素(ARGB32_Premultiplied, 小端 BGRA 内存序)。 */
static uint32_t xr_pixel(const XImage* img, int x, int y)
{
    const uint8_t* p = XImage_bits(img) +
                       (size_t)y * (size_t)XImage_bytesPerLine(img) +
                       (size_t)x * 4u;
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) |
           ((uint32_t)p[3] << 24);
}

/** @brief 合成指针事件直达客户端顶层（局部坐标, 零换算转发口径）。 */
static void xr_sendMouse(XGuiClient* client, XEventType type,
                         XMouseButton button, XMouseButton buttons, int x,
                         int y)
{
    XMouseEvent* me;
    XPoint pos;
    XPoint_init(&pos, x, y);
    me = XMouseEvent_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, type, button,
                               (XKeyboardModifiers)XKeyboardModifier_NoModifier,
                               pos);
    if (!me) return;
    XMouseEvent_setButtons(me, buttons);
    XObject_event_base((XObject*)client, (XEvent*)me);
    XClassDelete((XEvent*)me);
}

/** @brief 基线端到端: 握手→FB_META→全量→像素抽样→点击→越界→BYE。
 *  @note  环容量 8KiB << 单帧 FB_UPDATE, 写出链全程走"全有全无写 + 帧尾
 *         待写缓冲续传"——本用例同时即慢消费者场景的断言载体。 */
static void xr_test_e2eBasic(void)
{
    XrE2e e;
    XrStreamCond cond;
    XImage* svcSnap;
    XImage* cliSnap;
    XGuiRemoteStats st;
    XSize rsz;
    int w = 320, h = 240;

    if (!xr_e2e_setup(&e, 8u * 1024u, w, h, false, NULL)) {
        xr_expect(false, "端到端拓扑建立");
        xr_e2e_teardown(&e);
        return;
    }

    /* 1) 握手→FB_META→全量刷新(FB_REQUEST 由客户端自动发出)。 */
    cond.baseTiles = 0;
    cond.wantMore = 1;
    xr_expect(xr_waitUntil(xr_streamed, &cond, 8000),
              "握手→FB_META→客户端进入 Streaming");

    /* 2) 呈现真实内容: 全窗 repaint → present 采集 → 编码 → 推送。
     *    等满 6 个新 tile(ceil(320/128)×ceil(240/128)=3×2)。 */
    cond.baseTiles = xr_clientTiles();
    cond.wantMore = 3 * 2;
    XWidget_repaint(e.top);
    xr_expect(xr_waitUntil(xr_streamed, &cond, 8000),
              "全量 FB_UPDATE 收满 tile 网格(慢链路续传不丢批)");
    xr_pumpFor(300); /* 静默: 排干在途帧, 采样前画面收敛。 */

    XGuiClient_statistics(e.client, &st);
    xr_expect(st.updateCount >= 2,
              "FB_UPDATE 至少两批(零影子首轮 + 内容批)");
    XGuiClient_remoteSize(e.client, &rsz);
    xr_expect(rsz.width == w && rsz.height == h, "FB_META 尺寸 = 镜像窗口");
    xr_expect(XGuiClient_state(e.client) == XGUI_REMOTE_STATE_STREAMING,
              "客户端处于流式态");

    /* 3) 像素抽样: 客户端 backbuffer == 服务端源(采样覆盖底/块A/块B)。 */
    svcSnap = XWidget_grab(e.top);
    cliSnap = XWidget_grab((XWidget*)e.client);
    xr_expect(svcSnap != NULL && cliSnap != NULL, "双端抓取就绪");
    if (svcSnap && cliSnap) {
        struct
        {
            int x, y;
            const char* what;
        } pts[] = {
            {2, 2, "左上底色"},
            {40, 40, "色块 A 中心"},
            {w - 40, h - 40, "色块 B 中心"},
            {w - 2, 2, "右上底色"},
            {2, h - 2, "左下底色"},
            {w / 2, h - 30, "中下底色"},
        };
        size_t i;
        /* 正控: 服务端快照在色块处应与客户端一致且非空底(色值本身以
         * palette 实际渲染为准, 断言其不等于顶层底色, 防止双层同色假绿)。 */
        {
            uint32_t sA = xr_pixel(svcSnap, 40, 40);
            if (sA != 0xFFd93025u) {
                char msg[128];
                snprintf(msg, sizeof(msg), "实际=%08x(期望设定色 d93025; "
                         "若等于顶层底色 21539b 则子控件未自填底)",
                         (unsigned)sA);
                xr_expect_msg(false, "服务端源像素 = 色块 A 设定色(正控)", msg);
            }
        }
        for (i = 0; i < sizeof(pts) / sizeof(pts[0]); ++i) {
            uint32_t a = xr_pixel(svcSnap, pts[i].x, pts[i].y);
            uint32_t b = xr_pixel(cliSnap, pts[i].x, pts[i].y);
            if (a != b) {
                char msg[128];
                snprintf(msg, sizeof(msg),
                         "@(%d,%d) %s src=%08x cli=%08x", pts[i].x, pts[i].y,
                         pts[i].what, a, b);
                xr_expect_msg(false, "客户端 backbuffer 像素抽样==源像素", msg);
            }
        }
        xr_expect(xr_pixel(cliSnap, 40, 40) == 0xFFd93025u,
                  "客户端 backbuffer 色块 A = 源设定色");
    }
    if (svcSnap) XClassDelete(svcSnap);
    if (cliSnap) XClassDelete(cliSnap);
    xr_expect(xr_cliErrors == 0, "画面阶段客户端零错误");

    /* 4) 注入 INPUT_POINTER: 本地合成 press/release 命中客户端 → 转发远端
     *    → 服务端 WSI 注入镜像树 → 勾选框收到点击。 */
    xr_sendMouse(e.client, XEVENT_TYPE_MOUSE_BUTTON_PRESS,
                 XMouseButton_LeftButton, XMouseButton_LeftButton,
                 w * 2 / 5 + 8, h / 2 - 7);
    xr_sendMouse(e.client, XEVENT_TYPE_MOUSE_BUTTON_RELEASE,
                 XMouseButton_LeftButton, 0, w * 2 / 5 + 8, h / 2 - 7);
    {
        int64_t deadline = xr_nowMs() + 5000;
        while (xr_clickCount < 1 && xr_nowMs() < deadline) {
            XGuiApplication_processEvents(XEventLoop_AllEvents);
            XThread_msleep(2);
        }
        xr_expect(xr_clickCount == 1, "远端勾选框收到转发点击(x1)");
    }

    /* 5) 抓取越界 i16 负坐标: 按下(框内)→拖出窗口(-100,-50)→框外释放。
     *    服务端按冻结口径裁剪注入(不误断链); 释放点在框外不产生点击。 */
    xr_sendMouse(e.client, XEVENT_TYPE_MOUSE_BUTTON_PRESS,
                 XMouseButton_LeftButton, XMouseButton_LeftButton,
                 w * 2 / 5 + 8, h / 2 - 7);
    xr_sendMouse(e.client, XEVENT_TYPE_MOUSE_MOVE, XMouseButton_LeftButton,
                 XMouseButton_LeftButton, -100, -50);
    xr_sendMouse(e.client, XEVENT_TYPE_MOUSE_BUTTON_RELEASE,
                 XMouseButton_LeftButton, 0, -100, -50);
    xr_pumpFor(200);
    xr_expect(xr_clickCount == 1, "框外释放不产生额外点击");
    xr_expect(xr_cliErrors == 0 && xr_cliDisconnected == 0,
              "越界 i16 转发零错误不断链");
    xr_expect(XGuiClient_state(e.client) == XGUI_REMOTE_STATE_STREAMING,
              "越界拖拽后会话仍在 Streaming");
    xr_expect(XGuiServer_sessionCount(e.server) == 1, "服务端会话仍存活");

    /* 6) BYE 优雅退出: 客户端发起 → 服务端 clientDisconnected(NORMAL)。 */
    XGuiClient_disconnectFromServer(e.client);
    {
        int64_t deadline = xr_nowMs() + 5000;
        while (xr_srvDisconnected < 1 && xr_nowMs() < deadline) {
            XGuiApplication_processEvents(XEventLoop_AllEvents);
            XThread_msleep(2);
        }
    }
    xr_expect(xr_srvDisconnected == 1 &&
                  xr_srvLastDisconnectReason == XGUI_REMOTE_BYE_NORMAL,
              "服务端收到 BYE(NORMAL) 断开会话");
    xr_expect(XGuiClient_state(e.client) == XGUI_REMOTE_STATE_DISCONNECTED,
              "客户端转 Disconnected");
    xr_expect(!XGuiClient_isRemoteAlive(e.client), "远端画面不再可用");

    xr_e2e_teardown(&e);
}

/** @brief 档位热切换专项: performance 会话中途 setProfileId(resource) →
 *         FB_META 同尺寸换线上格式(ARGB32→RGB565) → 客户端必须按新格式
 *         重建 backbuffer(回归锁: 重建条件漏格式变化时 RGB565 tile 以
 *         2B/px 写进残留 4B/px 缓冲 → 水平 2:1 压缩+色道错乱)。
 *         末态像素抽样 = 源(RGB565 量化容差 ≤8/通道), 会话不断。 */
static void xr_test_e2eProfileHotSwitch(void)
{
    XrE2e e;
    XrStreamCond cond;
    XImage* svcSnap;
    XImage* cliSnap;
    int w = 320, h = 240;

    if (!xr_e2e_setup(&e, 64u * 1024u, w, h, false, NULL)) {
        xr_expect(false, "热切换拓扑建立(performance 初档)");
        xr_e2e_teardown(&e);
        return;
    }

    /* 1) performance 档握手 + 首幅全量。 */
    cond.baseTiles = 0;
    cond.wantMore = 1;
    xr_expect(xr_waitUntil(xr_streamed, &cond, 8000),
              "热切换: 初档握手进入 Streaming");
    cond.baseTiles = xr_clientTiles();
    cond.wantMore = 1;
    XWidget_repaint(e.top);
    xr_expect(xr_waitUntil(xr_streamed, &cond, 8000),
              "热切换: performance 首幅画面到齐");
    xr_pumpFor(200);

    /* 2) 运行期换档(§5.3): 帧边界生效 + 广播 FB_META(同尺寸换格式)。 */
    XGuiServer_setProfileId(e.server, XGUI_REMOTE_PROFILE_RESOURCE);
    {
        int meta0 = xr_cliMeta;
        int64_t deadline = xr_nowMs() + 8000;
        while (xr_cliMeta <= meta0 && xr_nowMs() < deadline)
            XGuiApplication_processEvents(XEventLoop_AllEvents);
        xr_expect(xr_cliMeta > meta0, "热切换: 客户端收到换档 FB_META");
    }
    cond.baseTiles = xr_clientTiles();
    cond.wantMore = 1;
    XWidget_repaint(e.top);
    xr_expect(xr_waitUntil(xr_streamed, &cond, 8000),
              "热切换: resource 档画面继续同步(RGB565/RLE 路径)");
    xr_pumpFor(300); /* 排干在途帧再采样。 */

    /* 3) 末态像素: 客户端 = 源(RGB565 量化容差 ≤8/通道)。 */
    svcSnap = XWidget_grab(e.top);
    cliSnap = XWidget_grab((XWidget*)e.client);
    xr_expect(svcSnap != NULL && cliSnap != NULL, "热切换: 双端抓取就绪");
    if (svcSnap && cliSnap) {
        struct
        {
            int x, y;
            const char* what;
        } pts[] = {
            {2, 2, "左上底色"},
            {40, 40, "色块 A 中心"},
            {w - 40, h - 40, "色块 B 中心"},
            {w - 2, 2, "右上底色"},
            {2, h - 2, "左下底色"},
        };
        size_t i;
        for (i = 0; i < sizeof(pts) / sizeof(pts[0]); ++i) {
            uint32_t a = xr_pixel(svcSnap, pts[i].x, pts[i].y);
            uint32_t b = xr_pixel(cliSnap, pts[i].x, pts[i].y);
            int dr = (int)((a >> 16) & 0xFF) - (int)((b >> 16) & 0xFF);
            int dg = (int)((a >> 8) & 0xFF) - (int)((b >> 8) & 0xFF);
            int db = (int)(a & 0xFF) - (int)(b & 0xFF);
            if (dr < 0) dr = -dr;
            if (dg < 0) dg = -dg;
            if (db < 0) db = -db;
            if (dr > 8 || dg > 8 || db > 8) {
                char msg[160];
                snprintf(msg, sizeof(msg),
                         "@(%d,%d) %s src=%08x cli=%08x Δ=(%d,%d,%d)>8",
                         pts[i].x, pts[i].y, pts[i].what, a, b, dr, dg, db);
                xr_expect_msg(false, "热切换末态像素=源(RGB565 容差)", msg);
            }
        }
    }
    if (svcSnap) XClassDelete(svcSnap);
    if (cliSnap) XClassDelete(cliSnap);

    /* 4) 会话存活: 换档不断链、零错误。 */
    xr_expect(XGuiClient_state(e.client) == XGUI_REMOTE_STATE_STREAMING,
              "热切换后会话仍在 Streaming");
    xr_expect(xr_cliErrors == 0 && xr_cliDisconnected == 0,
              "热切换零错误不断链");
    xr_expect(XGuiServer_sessionCount(e.server) == 1, "热切换后服务端会话存活");

    xr_e2e_teardown(&e);
}

/** @brief 慢消费者专项: RAW 大帧 + 8KiB 环 + 4KiB 写出预算 → 每帧必经
 *         全有全无写拒整帧 + 帧尾待写缓冲逐圈续传, tile 精确到齐(不重
 *         不漏=帧序不乱流)、不误断链、末态像素正确。
 *  @note  txBudget(4KiB) ≤ 环容量(8KiB): 回环设备全有全无写下, 待写
 *         续传单圈拟写量(min(尾长,预算))不大于环容量才能取得进展——
 *         这是回环测试拓扑的容量约束(TCP 部分写无此约束)。 */
static void xr_test_e2eSlowConsumer(void)
{
    XrE2e e;
    XrStreamCond cond;
    XGuiRemoteProfile p;
    XGuiRemoteStats st;
    uint32_t base;
    int w = 320, h = 240;

    if (!xr_e2e_setup(&e, 8u * 1024u, w, h, false, NULL)) {
        xr_expect(false, "慢消费者拓扑建立");
        xr_e2e_teardown(&e);
        return;
    }
    /* HELLO 采纳前定版: RAW 不压缩(整幅 300KB 级, 每帧远超环容量) +
     * 门控放宽 + 写出预算压到下限(4KiB ≤ 环 8KiB, 续传可进展)。 */
    XGuiRemoteProfile_initPerformance(&p);
    p.codec = XGUI_REMOTE_CODEC_RAW;
    p.maxFps = 240;
    p.txBudgetBytes = 4u * 1024u;
    XGuiServer_setProfile(e.server, &p);
    xr_expect(XGuiServer_profileId(e.server) == XGUI_REMOTE_PROFILE_CUSTOM,
              "自定义档位生效(CUSTOM)");

    cond.baseTiles = 0;
    cond.wantMore = 1;
    xr_expect(xr_waitUntil(xr_streamed, &cond, 15000),
              "慢消费者: 握手+零影子首轮经小环到齐");

    /* 零影子首轮与首显内容批(映射曝光触发的 present 采集)完全排干后
     * 再取基线——基线必须落在静态画面上, 否则首轮余量 tile 污染算术。 */
    base = xr_waitStatsStable(600, 20000);

    /* 内容变更批: 整窗换色(全幅内容变化 → 全部 tile 哈希失配 → 全量
     * 6 个 RAW 大 tile), 每帧远超环容量 → 帧尾待写缓冲逐圈续传。 */
    xr_paintColor(e.top, 0x10, 0x40, 0x80);
    cond.baseTiles = base;
    cond.wantMore = 3 * 2;
    XWidget_update(e.top);
    XWidget_repaint(e.top);
    xr_expect(xr_waitUntil(xr_streamed, &cond, 20000),
              "慢消费者: RAW 大帧全量到齐");
    xr_pumpFor(400); /* 静默观察: 不再增长即不重不漏。 */
    XGuiClient_statistics(e.client, &st);
    if (st.tileCount != base + 3 * 2) {
        char msg[128];
        snprintf(msg, sizeof(msg), "实际 tile=%u(基线 %u + 期望 6), "
                 "updateCount=%u", st.tileCount, base, st.updateCount);
        xr_expect_msg(false, "慢消费者: tile 精确到齐(帧序不乱流)", msg);
    }
    xr_expect(xr_cliErrors == 0 && xr_srvErrors == 0, "慢链路双方零错误");
    xr_expect(XGuiServer_sessionCount(e.server) == 1, "慢链路不误断链");
    {
        XImage* cliSnap = XWidget_grab((XWidget*)e.client);
        xr_expect(cliSnap != NULL, "慢链路客户端快照就绪");
        if (cliSnap) {
            xr_expect(xr_pixel(cliSnap, 160, 180) == 0xFF104080u,
                      "慢链路末态像素 = 变更后源设定色(顶层底色采样点)");
            XClassDelete(cliSnap);
        }
    }
    XGuiClient_disconnectFromServer(e.client);
    xr_pumpFor(100);
    xr_e2e_teardown(&e);
}

/* ==================== (d) 伤害管线 ==================== */

/** @brief 内容未变重绘: tile 哈希去重 → 零新批零新 tile。 */
static void xr_test_damageDedup(void)
{
    XrE2e e;
    XrStreamCond cond;
    XGuiRemoteStats st;
    uint32_t tiles0, updates0;

    if (!xr_e2e_setup(&e, 64u * 1024u, 320, 240, false, NULL)) {
        xr_expect(false, "去重拓扑建立");
        xr_e2e_teardown(&e);
        return;
    }
    cond.baseTiles = 0;
    cond.wantMore = 1;
    xr_expect(xr_waitUntil(xr_streamed, &cond, 8000), "去重: 进入流式");
    cond.baseTiles = xr_clientTiles();
    cond.wantMore = 3 * 2;
    XWidget_repaint(e.top);
    xr_expect(xr_waitUntil(xr_streamed, &cond, 8000), "去重: 首轮内容到齐");
    xr_pumpFor(250); /* 静默: 编码线程消化完脏位。 */
    XGuiClient_statistics(e.client, &st);
    updates0 = st.updateCount;
    tiles0 = st.tileCount;

    /* 内容未变全窗重绘 → 伤害全格标脏 → 认领后哈希全等 → 零发送。 */
    XWidget_repaint(e.top);
    xr_pumpFor(400);
    XGuiClient_statistics(e.client, &st);
    xr_expect(st.updateCount == updates0,
              "未变内容重绘零新 FB_UPDATE(哈希去重)");
    xr_expect(st.tileCount == tiles0, "未变内容重绘零新 tile");
    XGuiClient_disconnectFromServer(e.client);
    xr_pumpFor(100);
    xr_e2e_teardown(&e);
}

/** @brief maxFps 认领门控: resource 档 15fps → 批数上界受 1000/maxFps 约束。 */
static void xr_test_damageThrottle(void)
{
    XrE2e e;
    XrStreamCond cond;
    XGuiRemoteStats st;
    int i;
    uint32_t updates0;
    int64_t t0;
    int flip = 0;

    if (!xr_e2e_setup(&e, 64u * 1024u, 320, 240, false, NULL)) {
        xr_expect(false, "节流拓扑建立");
        xr_e2e_teardown(&e);
        return;
    }
    cond.baseTiles = 0;
    cond.wantMore = 1;
    xr_expect(xr_waitUntil(xr_streamed, &cond, 8000), "节流: 进入流式");

    /* 运行期切档(§5.3): FB_META 广播 → 客户端跟随 resource 参数。 */
    XGuiServer_setProfileId(e.server, XGUI_REMOTE_PROFILE_RESOURCE);
    {
        int64_t deadline = xr_nowMs() + 5000;
        while (xr_cliMeta < 2 && xr_nowMs() < deadline) {
            XGuiApplication_processEvents(XEventLoop_AllEvents);
            XThread_msleep(2);
        }
        xr_expect(xr_cliMeta >= 2, "切档广播 FB_META 客户端跟随");
    }
    xr_pumpFor(200);
    XGuiClient_statistics(e.client, &st);
    updates0 = st.updateCount;

    /* 连续伤害: ~240ms 内 8 次变更高频重绘; 认领门控 1000/15≈67ms。 */
    t0 = xr_nowMs();
    for (i = 0; i < 8; ++i) {
        if (flip)
            xr_paintColor(e.childA, 0xD9, 0x30, 0x25);
        else
            xr_paintColor(e.childA, 0x10, 0x80, 0xC0);
        flip = !flip;
        XWidget_repaint(e.top);
        XGuiApplication_processEvents(XEventLoop_AllEvents);
        XThread_msleep(30);
    }
    xr_pumpFor(300);
    {
        int64_t elapsed = xr_nowMs() - t0;
        XGuiClient_statistics(e.client, &st);
        /* 上界: 门控节流(每 67ms 至多一批, 余量放宽); 下界: 至少一批。 */
        xr_expect(st.updateCount > updates0, "节流: 高频伤害仍持续推送");
        xr_expect((int)st.updateCount - (int)updates0 <=
                      (int)(elapsed / 66) + 8,
                  "节流: 批数受 1000/maxFps 上界约束");
    }
    xr_expect(xr_cliErrors == 0 && xr_srvErrors == 0, "节流: 无副作用错误");
    XGuiClient_disconnectFromServer(e.client);
    xr_pumpFor(100);
    xr_e2e_teardown(&e);
}

/** @brief 有界队列水位: 不可压噪声 + GUI 泵停滞 → 队列满丢最旧整批并
 *         计数上报 sessionError(BUFFER_OVERFLOW), 会话不断链。
 *  @note  生产循环期间刻意不泵事件循环: 写出链无背压打扰(大环 + 无
 *         poll 泵), 编码线程按门控节拍持续入队, 水位自然越过预算。 */
static void xr_test_damageQueueOverflow(void)
{
    XrE2e e;
    XrStreamCond cond;
    XGuiRemoteProfile p;
    XGuiRemoteStats st;
    int i;
    int flip = 0;
    uint32_t tilesAfter;

    /* 噪声标签 128×96(48KiB/批) + 队列下限 64KiB: 两批即越限。 */
    if (!xr_e2e_setup(&e, 256u * 1024u, 320, 240, true, NULL)) {
        xr_expect(false, "丢批拓扑建立");
        xr_e2e_teardown(&e);
        return;
    }
    XGuiRemoteProfile_initPerformance(&p);
    p.codec = XGUI_REMOTE_CODEC_RLE;   /* 噪声 RLE ≈ 原始体积(不可压)。 */
    p.maxFps = 240;                    /* 生产端放开(4ms 门控)。 */
    p.encodeQueueBytes = 64u * 1024u;  /* sanitize 下限。 */
    XGuiServer_setProfile(e.server, &p);

    cond.baseTiles = 0;
    cond.wantMore = 1;
    xr_expect(xr_waitUntil(xr_streamed, &cond, 8000), "丢批: 进入流式");

    /* 生产: 交替两幅噪声像素图 ×150 次(约 600ms), GUI 只 repaint 不泵。 */
    for (i = 0; i < 150; ++i) {
        XLabel_setPixmap(e.noise, &e.noisePm[flip]);
        flip = !flip;
        XWidget_repaint(e.top); /* 同步 paint→flush→present 采集。 */
        XThread_msleep(4);      /* 让位编码线程(≥门控周期)。 */
    }
    /* 恢复泵: 服务端上报丢批计数(sessionError), 队列余量排空。 */
    xr_pumpFor(400);
    XGuiClient_statistics(e.client, &st);
    tilesAfter = st.tileCount;

    xr_expect(xr_srvErrors >= 1, "队列水位超限产生丢批上报");
    xr_expect(xr_srvLastErrorCode == (int)XGUI_REMOTE_ERR_BUFFER_OVERFLOW,
              "丢批错误码 = BUFFER_OVERFLOW");
    xr_expect(XGuiServer_sessionCount(e.server) == 1 && xr_srvDisconnected == 0,
              "丢批不断链(有界内存而非故障)");
    xr_expect(xr_cliErrors == 0, "丢批场景客户端无感");
    xr_expect(tilesAfter > 0, "丢批前已收 tile 正常入客户端");

    XGuiClient_disconnectFromServer(e.client);
    xr_pumpFor(100);
    xr_e2e_teardown(&e);
}

/* ==================== (e) 悬浮会话工具条(回环 e2e) ==================== */

/** @brief 合成鼠标事件直达任意控件（局部坐标; xr_sendMouse 的控件
 *         泛化版——悬浮条/其子按钮用, 不经 childAt 命中路径）。 */
static void xr_sendWidgetMouse(XWidget* widget, XEventType type,
                               XMouseButton button, XMouseButton buttons,
                               int x, int y)
{
    XMouseEvent* me;
    XPoint pos;
    XPoint_init(&pos, x, y);
    me = XMouseEvent_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, type, button,
                               (XKeyboardModifiers)XKeyboardModifier_NoModifier,
                               pos);
    if (!me) return;
    XMouseEvent_setButtons(me, buttons);
    XObject_event_base((XObject*)widget, (XEvent*)me);
    XClassDelete((XEvent*)me);
}

/** @brief 条上零转发断言的发送字节上界: 健康会话 200ms 窗口至多 1 帧
 *         PING(5B 头+8B 负载=13B, 档位保活间隔 ≥5000ms); 任何被误转
 *         发的 INPUT_POINTER 至少 21B/帧——≤13B 即可排除指针转发。 */
#define XR_BAR_SENT_TOLERANCE 13u

static void xr_test_sessionBar(void)
{
    XrE2e e;
    XrStreamCond cond;
    XRemoteSessionBar* bar;
    XWidget* barWidget;
    XPushButton* discBtn;
    XGuiRemoteStats st;
    uint64_t sentBefore;
    int clickBefore;
    int64_t deadline;
    XRect view;

    if (!xr_e2e_setup(&e, 64u * 1024u, 320, 240, false, NULL)) {
        xr_expect(false, "悬浮条: 回环拓扑建立");
        xr_e2e_teardown(&e);
        return;
    }
    /* 悬浮条切档走 PROFILE_SET 协商(§5.3), 服务端须显式放行
     * （XGuiServer allowClientProfile 默认 false——私有块缺省值,
     * XGuiServer.c:200; 放行 API 见 XGuiServer.h:225）。 */
    XGuiServer_setAllowClientProfile(e.server, true);

    /* 1) 实例化+绑定; 会话进入流式后工具条自动浮现并展开。 */
    bar = XRemoteSessionBar_create(NULL, 0); /* 离屏顶层, 与视图互不遮挡。 */
    xr_expect(bar != NULL, "悬浮条: 实例化");
    if (!bar) {
        xr_e2e_teardown(&e);
        return;
    }
    barWidget = (XWidget*)bar;
    XRemoteSessionBar_setClient(bar, e.client);
    XRect_init(&view, 0, 0, 320, 240);
    XRemoteSessionBar_attachView(bar, &view);
    cond.baseTiles = 0;
    cond.wantMore = 1;
    xr_expect(xr_waitUntil(xr_streamed, &cond, 8000),
              "悬浮条: 会话进入流式");
    xr_expect(XWidget_isVisible(barWidget) &&
                  XRemoteSessionBar_isExpanded(bar),
              "悬浮条: 连接后自动浮现并展开");
    xr_expect(XWidget_width(barWidget) == 500 &&
                  XWidget_height(barWidget) == 34,
              "悬浮条: 展开态吸附几何(500x34)");

    /* 2) 展开/收起形态切换(悬浮球 44x44)。 */
    XRemoteSessionBar_collapse(bar);
    xr_expect(!XRemoteSessionBar_isExpanded(bar) &&
                  XWidget_width(barWidget) == 44 &&
                  XWidget_height(barWidget) == 44,
              "悬浮条: 收起为悬浮球(44x44)");
    XRemoteSessionBar_expand(bar);
    xr_expect(XRemoteSessionBar_isExpanded(bar),
              "悬浮条: 悬浮球重新展开为吸附条");

    /* 3) 条上点击零转发: 面板空白槽位([断开连接]|[档位] 间 x=94)按下-
     *    移动-释放, 断言零 INPUT_* 上行 + 远端 RemotePing 计数不变。 */
    xr_waitStatsStable(400, 4000); /* 排干在途帧, 基线落在静默窗。 */
    XGuiClient_statistics(e.client, &st);
    sentBefore = st.bytesSent;
    clickBefore = xr_clickCount;
    xr_sendWidgetMouse(barWidget, XEVENT_TYPE_MOUSE_BUTTON_PRESS,
                       XMouseButton_LeftButton, XMouseButton_LeftButton,
                       94, 17);
    xr_sendWidgetMouse(barWidget, XEVENT_TYPE_MOUSE_MOVE,
                       XMouseButton_LeftButton, XMouseButton_LeftButton,
                       120, 40);
    xr_sendWidgetMouse(barWidget, XEVENT_TYPE_MOUSE_BUTTON_RELEASE,
                       XMouseButton_LeftButton, 0, 120, 40);
    xr_pumpFor(200);
    XGuiClient_statistics(e.client, &st);
    xr_expect(st.bytesSent - sentBefore <= XR_BAR_SENT_TOLERANCE,
              "悬浮条: 条上拖拽点击零 INPUT 转发(上行 ≤1 保活 PING)");
    xr_expect(xr_clickCount == clickBefore,
              "悬浮条: 远端 RemotePing 计数不变(点击不转发远端)");
    xr_expect(xr_cliErrors == 0 && xr_cliDisconnected == 0,
              "悬浮条: 条上交互零副作用(不断链无错误)");

    /* 4) 经悬浮条切档: PROFILE_SET 协商达成以换档 FB_META 广播为准
     *    (与 e2eProfileHotSwitch 同口径; 服务端已放行)。 */
    {
        int metaBefore = xr_cliMeta;
        XRemoteSessionBar_requestProfile(bar, XGUI_REMOTE_PROFILE_RESOURCE);
        deadline = xr_nowMs() + 8000;
        while (xr_cliMeta <= metaBefore && xr_nowMs() < deadline) {
            XGuiApplication_processEvents(XEventLoop_AllEvents);
            XThread_msleep(2);
        }
        xr_expect(xr_cliMeta > metaBefore,
                  "悬浮条: 切档请求经 PROFILE_SET 协商成功(FB_META 广播)");
        xr_expect(XRemoteSessionBar_profile(bar) ==
                      XGUI_REMOTE_PROFILE_RESOURCE,
                  "悬浮条: 界面档位选择登记(resource)");
    }

    /* 5) 断开按钮: 合成点击真实按钮路径 → BYE(NORMAL) → 会话断开、
     *    工具条自动隐藏; 客户端对象仍存活(不杀进程不退出)。 */
    discBtn = XRemoteSessionBar_disconnectButton(bar);
    xr_expect(discBtn != NULL, "悬浮条: 断开按钮就位");
    if (discBtn) {
        xr_sendWidgetMouse((XWidget*)discBtn, XEVENT_TYPE_MOUSE_BUTTON_PRESS,
                           XMouseButton_LeftButton, XMouseButton_LeftButton,
                           40, 13);
        xr_sendWidgetMouse((XWidget*)discBtn,
                           XEVENT_TYPE_MOUSE_BUTTON_RELEASE,
                           XMouseButton_LeftButton, 0, 40, 13);
        deadline = xr_nowMs() + 5000;
        while (xr_srvDisconnected < 1 && xr_nowMs() < deadline) {
            XGuiApplication_processEvents(XEventLoop_AllEvents);
            XThread_msleep(2);
        }
        xr_expect(xr_srvDisconnected == 1 &&
                      xr_srvLastDisconnectReason == XGUI_REMOTE_BYE_NORMAL,
                  "悬浮条: 断开按钮 → BYE(NORMAL) 服务端断开会话");
        xr_expect(XGuiClient_state(e.client) ==
                      XGUI_REMOTE_STATE_DISCONNECTED,
                  "悬浮条: 客户端转未连接(会话级退出, 不杀进程)");
        xr_expect(!XWidget_isVisible(barWidget),
                  "悬浮条: 断开后工具条自动隐藏");
    }

    XClassDelete(bar);
    /* 诊断落 stderr(套件惯例): 供 runner/联调确认悬浮条 e2e 真实跑过。 */
    fprintf(stderr, "[XR-BAR] session-bar e2e done: fwdDelta=%llu clickDelta=%d "
                    "meta=%d srvDisc=%d\n",
            (unsigned long long)(st.bytesSent - sentBefore),
            xr_clickCount - clickBefore, xr_cliMeta, xr_srvDisconnected);
    xr_e2e_teardown(&e);
}

/* ==================== 入口 ==================== */

bool XGuiRemoteTest_runAll(void)
{
    xr_failures = 0;
    xr_ensureApp();

    /* (a) 协议单元。 */
    xr_test_putget();
    xr_test_banner();
    xr_test_messagesRoundtrip();
    xr_test_frameReader();
    xr_test_profile();
    xr_test_closedEnums();

    /* (b) 编解码单元。 */
    xr_test_rleVectors();
    xr_test_zlibRoundtrip();
    xr_test_convertAndHash();

    /* (c) 回环设备语义 + 端到端。 */
    xr_test_loopbackDevice();
    xr_test_e2eBasic();
    xr_test_e2eProfileHotSwitch();
    xr_test_e2eSlowConsumer();

    /* (d) 伤害管线。 */
    xr_test_damageDedup();
    xr_test_damageThrottle();
    xr_test_damageQueueOverflow();

    /* (e) 悬浮会话工具条(2026-10-03): 实例化→零转发→切档→断开。 */
    xr_test_sessionBar();

    if (xr_failures == 0) {
        fprintf(stderr, "XGuiRemote test: PASS\n");
        return true;
    }
    fprintf(stderr, "XGuiRemote test: %d assertion(s) failed\n", xr_failures);
    return false;
}

#else /* !XWIDGET_ON || !XGUI_REMOTE_ON */

/* 开关矩阵: 模块裁空(XGUI_REMOTE_ON=0 或控件层关闭)时静默通过
 * （XKeyboardTest 同款 stub 惯例——门禁环境默认全开, 走不到此分支）。 */
bool XGuiRemoteTest_runAll(void)
{
    return true;
}

#endif /* XWIDGET_ON && XGUI_REMOTE_ON */
