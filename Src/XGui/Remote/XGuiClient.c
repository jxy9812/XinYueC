/**
 * @file       XGuiClient.c
 * @brief      XGuiClient 远程视图控件实现(画面合成 + 输入转发 + 断线重连)。
 * @details    契约见冻结头 XGuiClient.h, 设计依据仓库根 XGuiRemote.md §7:
 *               - 画面: FB_UPDATE 逐 tile 经 XGuiRemoteCodec_decodeTile 直写
 *                 本地 backbuffer(XImage, 尺寸=远端 FB_META), tile 矩形并入
 *                 损伤 XRegion 后 XWidget_updateRegion 增量上屏; paintEvent
 *                 虚槽 XPainter_drawImage 整幅绘出; 未连接画占位文本, 不画
 *                 陈旧帧;
 *               - I/O: 全部在 GUI 线程, 事件循环 poll 回调驱动帧泵; 发送侧
 *                 按 XGuiRemoteProto_writeFrame 返回值处理部分写(帧尾待写
 *                 outbox, 每圈按档位 txBudgetBytes 限预算续传);
 *               - 输入转发(需求 9 口径, XGuiRemote.md §7.4 逐条落实): 鼠标
 *                 移动/按下/释放/双击/滚轮 + 键盘按下/释放 + IME 文本经
 *                 INPUT_* 消息直发远端; 键盘槽 hasFocus 门控; 按下抓取
 *                 (grabMouse)拖拽越界按 i16(±32767 饱和)编码不裁剪, 远端
 *                 负责裁剪; PRESS/RELEASE 成对纪律(断链清集合并补发
 *                 RELEASE); resource 档鼠标移动合并窗口, 按下/释放/双击/
 *                 滚轮永不合并; 无本地虚拟指针/软键盘控件, 远端自绘光标
 *                 与 IME;
 *               - 重连: XTimer 单次触发指数退避(基础 intervalMs, 上限 30s),
 *                 重连成功(FB_META)自动 FB_REQUEST 全量;
 *               - 保活: 按档位间隔发 PING, 超时未收对端帧判死链转重连;
 *                 PONG 回显测 rtt;
 *               - 统计: 累加计数, 断链不清零。
 *             线程约定: 控件与全部公共 API 仅限 GUI 线程; 设备读写与帧泵
 *             全在 GUI 线程(poll 回调), 无内部线程。
 * @author     XinYueC 团队
 */
#include "XGuiClient.h"
#include <stdio.h>   /* fprintf: 基准追踪 stderr(XGUI_REMOTE_BENCH_TRACE)。 */
#if XGUI_REMOTE_ON

#include "XGuiRemoteCodec.h"
#include "XWidget_Protected.h"
#include "XEvent.h"
#include "XWindowEvent.h"
#include "XPainter.h"
#include "XImage.h"
#include "XString.h"
#include "XVarList.h"
#include "XMemory.h"
#include "XAbstractEventDispatcher.h"
#include "XTcpSocket.h"
#include "XTimer.h"
#include "XDateTime.h"
#include "XCryptographicHash.h"
#include "XSystem.h" /* XSystem_environment: TLS 自签部署环境口(唯一环境入口)。 */
#if defined(XNETWORK_SSL_ON) && XNETWORK_SSL_ON
#include "XSslSocket.h"
#endif
#include <string.h>

/* ==================== 私有常量 ==================== */

/** @brief 发送 outbox 字节帽: 客户端帧小(≤2KiB 级), 慢链路下保护内存
 *         有界; 溢出丢弃新帧并记 BUFFER_OVERFLOW 错误(不丢已排队帧,
 *         保 PRESS/RELEASE 顺序)。 */
#define XGC_OUTBOX_CAP (256u * 1024u)
/** @brief 每圈 poll 读泵字节预算(防单圈长任务饿死 GUI)。 */
#define XGC_READ_BUDGET_PER_POLL (256u * 1024u)
/** @brief 每圈 poll 读块栈缓冲字节数。 */
#define XGC_READ_CHUNK (16u * 1024u)
/** @brief 指数退避上限(毫秒, XGuiRemote.md §7.3)。 */
#define XGC_RECONNECT_MAX_MS 30000u
/** @brief 重连基础间隔缺省值(XGuiClient_setAutoReconnect intervalMs=0)。 */
#define XGC_RECONNECT_DEFAULT_MS 1000u
/** @brief fpsMilli 滑动窗口宽度(毫秒)。 */
#define XGC_FPS_WINDOW_MS 1000u
/** @brief 主机名/对端名缓冲上限。 */
#define XGC_HOST_CAP 128

/* ==================== 私有实现块 ==================== */

/** @brief 客户端私有会话状态(冻结契约不暴露, 见 XGuiClient.h m_d)。 */
typedef struct XGuiClientPrivate {
    /* ---- 传输(需求 2: 任意 XIODevice) ---- */
    XIODevice* m_dev;            /**< 当前传输设备(借用或自有, 见下)。 */
    bool       m_devOwned;       /**< true=本控件创建的 socket(拆除时销毁)。 */
    bool       m_devIsSsl;       /**< true=设备为 XSslSocket(TLS 档)。 */
    bool       m_tlsWanted;      /**< 网络档: 请求 TLS 连接。 */
    bool       m_sessionReady;   /**< 设备可用已确认(明文 connected/TLS 加密
                                  *   完成), 横幅已可发。 */
    bool       m_bannerSent;     /**< 本端 8 字节横幅已足额写出。 */
    bool       m_bannerGot;      /**< 对端横幅已收全并校验。 */
    uint8_t    m_bannerBuf[XGUI_REMOTE_BANNER_BYTES]; /**< 对端横幅攒字节。 */
    int        m_bannerGotBytes; /**< 对端横幅已攒字节数。 */

    /* ---- 连接意图与重连(§7.3) ---- */
    char     m_host[XGC_HOST_CAP];        /**< 目标主机(UTF-8)。 */
    uint16_t m_port;                      /**< 目标端口。 */
    char     m_peerName[XGC_HOST_CAP];    /**< TLS 校验对端名(可空)。 */
    bool     m_wantConnect;               /**< 用户请求过连接(网络档);
                                           *   setTransport 档恒 false。 */
    bool     m_autoReconnect;             /**< 断线自动重连开关。 */
    uint32_t m_reconnectBaseMs;           /**< 基础重连间隔。 */
    uint32_t m_reconnectAttempts;         /**< 连续重连次数(指数退避指数)。 */
    XTimer*  m_reconnectTimer;            /**< 单次触发重连定时器(拥有)。 */

    /* ---- 会话状态机(XGuiRemoteSessionState) ---- */
    XGuiRemoteSessionState m_state;
    XGuiRemoteFrameReader  m_reader;         /**< 增量帧泵。 */
    uint32_t m_localCaps;                    /**< 本端能力位(HELLO 携带)。 */
    uint32_t m_peerCaps;                     /**< 协商后能力交集。 */
    uint16_t m_negotiatedVersion;            /**< 协商版本(取双方较小)。 */
    XGuiRemoteAuthMethod m_authMethod;       /**< 服务端选定认证法。 */
    char     m_password[256];  /**< 口令原文(仅驻留至认证完成, 冻结头注)。 */
    XGuiRemoteMsgAuthChallenge m_challenge;  /**< 当前认证挑战。 */
    bool     m_challengeValid;               /**< 挑战在途。 */

    /* ---- 远端画面(§7.2) ---- */
    XImage  m_fb;                /**< 本地 backbuffer(值成员; 空图=未就绪)。 */
    bool    m_fbValid;          /**< backbuffer 与远端 FB_META 一致。 */
    uint16_t m_fbWidth;         /**< 远端窗口宽(FB_META)。 */
    uint16_t m_fbHeight;        /**< 远端窗口高。 */
    uint8_t m_fbFormat;         /**< 线上像素格式(XGuiRemotePixelFormat)。 */
    uint16_t m_tileWidth;       /**< 远端 tile 宽。 */
    uint16_t m_tileHeight;      /**< 远端 tile 高。 */
    uint8_t m_profileId;        /**< 远端当前档位 id。 */
    char    m_title[XGUI_REMOTE_MAX_NAME_BYTES + 1]; /**< 远端标题(终止保证)。 */
    int     m_titleBytes;       /**< 标题字节数(≤MAX_NAME)。 */
    XRegion m_damage;           /**< 本批 FB_UPDATE 损伤累积(复用容量)。 */

    /* ---- 保活与统计(§7.5) ---- */
    XGuiRemoteProfile m_profile;      /**< 本端档位参数副本(§5.2 各自持有)。 */
    uint64_t m_lastPeerFrameMs;       /**< 最近一次收到对端任何帧(单调 ms)。 */
    uint64_t m_lastPingSentMs;        /**< 最近一次发出 PING(单调 ms)。 */
    uint64_t m_pingSentTs;            /**< 在途 PING 携带的时间戳(回显匹配)。 */
    bool     m_pingAwait;             /**< 有在途 PING。 */
    uint64_t m_fpsWindowStartMs;      /**< fps 滑动窗口起点。 */
    uint32_t m_fpsWindowUpdates;      /**< 窗口内 FB_UPDATE 帧数。 */
    XGuiRemoteStats m_stats;          /**< 累加统计(断链不清零)。 */
    uint32_t m_reconnectCountTotal;   /**< 累计重连尝试(连接达成不清零;
                                       *   2026-10-02 加法式: 演示页实时
                                       *   统计「重连 N 次」口径)。 */

    /* ---- 输入转发(需求 9, §7.4) ---- */
    bool     m_fwdPointer;            /**< 鼠标/滚轮转发开关(默认开)。 */
    bool     m_fwdKeyboard;           /**< 键盘转发开关(默认开)。 */
    bool     m_fwdIme;                /**< IME 转发开关(默认开)。 */
    bool     m_fwdTouch;              /**< 触摸转发开关(默认关; 2026-10-02
                                       *   加法式: 演示页触摸直发通道配套
                                       *   的本地 TOUCH_* 事件转发门)。 */
    uint16_t m_pressedButtons;        /**< 已转发 PRESS 未配对 RELEASE 的键
                                       *   集合(XGUI_REMOTE_BTN_* 掩码)。 */
    uint32_t* m_pressedKeys;          /**< 已转发 PRESS 未配对 RELEASE 的键
                                       *   码集合(XKey 码位)。 */
    int      m_pressedKeyCount;
    int      m_pressedKeyCap;
    XGuiRemoteMsgInputPointer m_pendingMove; /**< 合并中的鼠标移动(resource 档)。 */
    bool     m_hasPendingMove;        /**< 有待发移动。 */
    uint64_t m_lastMoveSentMs;        /**< 最近一次移动实际发出(单调 ms)。 */

    /* ---- 发送 outbox(部分写续传, §6.4 纪律的客户端侧) ---- */
    uint8_t* m_outbox;               /**< 待写帧字节队列(线性)。 */
    size_t   m_outboxCap;            /**< 队列存储容量。 */
    size_t   m_outboxLen;            /**< 队列有效字节总数。 */
    size_t   m_outboxOff;            /**< 队头已写出偏移。 */
    bool     m_overflowNotified;     /**< 本会话已发过 BUFFER_OVERFLOW 错误。 */

    /* ---- poll 回调句柄 ---- */
    XHandle m_pollHandle;            /**< 事件循环轮询回调句柄(未注册 NULL)。 */
    XTimer* m_pumpTimer;             /**< 会话泵兜底定时器(基准优化 2026-10-02:
                                          空闲事件循环对网络完成事件的唤醒存在
                                          实测 ~170ms 级散布(resource 档收侧);
                                          8ms 周期直呼 pollPump 把收发延迟钳到
                                          ≤8ms。与 poll 回调同口, 空转近零成本)。 */
} XGuiClientPrivate;

/* ==================== 前置声明(实现互调) ==================== */

static bool xgc_pollPump(void* userData);
/** @brief 会话泵兜底定时回调(基准优化 2026-10-02): 直呼 pollPump。 */
static void xgc_pumpTimerCb(void* userData, XTimerData* timer)
{
    (void)timer;
    xgc_pollPump((XGuiClient*)userData);
}
static void xgc_teardown(XGuiClient* self, XGuiRemoteByeReason reason,
                         XGuiRemoteError errCode, bool sendBye, bool silent);
static bool xgc_sendFrame(XGuiClient* self, XGuiRemoteMsgType type,
                          const void* payload, size_t payloadBytes);
static void xgc_connectNetwork(XGuiClient* self);
static void xgc_onSocketConnected(XObject* receiver, XVarList* args);
static void xgc_onSocketDisconnected(XObject* receiver, XVarList* args);
static void xgc_onSocketError(XObject* receiver, XVarList* args);

/* ==================== 内部工具 ==================== */

/** @brief 单调毫秒时钟(Src 统一时间源: XDateTime 已是 CLOCK_MONOTONIC,
 *         先例 XFileDialog.c xff_nowMs)。 */
static int64_t xgc_nowMs(void)
{
    return XDateTime_currentMSecsSinceEpoch();
}

/** @brief i16 饱和(±32767; §3.2 坐标口径: 越界不裁剪, 远端负责)。 */
static int16_t xgc_satI16(int v)
{
    if (v > 32767) return 32767;
    if (v < -32768) return -32768;
    return (int16_t)v;
}

/** @brief XMouseButton → 线上鼠标键位(XEvent.h 位定义原值透传)。 */
static uint8_t xgc_buttonBit(XMouseButton button)
{
    return (uint8_t)(button & 0xFFu);
}

/** @brief 调色板取色(XPALETTE_ON 关闭时回退深灰, 占位画面可辨)。 */
static uint32_t xgc_paletteColor(const XGuiClient* self, XPaletteColorRole role)
{
#if XPALETTE_ON
    XPalette palette = XWidget_palette((XWidget*)self);
    XColor c = XPalette_color(&palette, XPaletteColorGroup_Current, role);
    return XColor_rgba(&c);
#else
    (void)self;
    (void)role;
    return 0xFF202020u;
#endif /* XPALETTE_ON */
}

/** @brief 信号发射(m_signalSlot 未挂时丢弃 args 防泄漏; 同 XMovie_emit)。 */
static void xgc_emit(XGuiClient* self, size_t signal, XVarList* args)
{
    if (self && ((XObject*)self)->m_signalSlot) {
        XObject_emitSignal((XObject*)self, signal, args, NULL, NULL,
                           XEVENT_PRIORITY_NORMAL);
    } else if (args) {
        XVarList_delete(args);
    }
}

/** @brief 发 errorOccurred(XVar 需左值, 统一经本助手落局部变量)。 */
static void xgc_emitError(XGuiClient* self, int code)
{
    xgc_emit(self, (size_t)XGuiClient_errorOccurred_signal,
             XVarList_Create(XVar(int, code)));
}

/** @brief 画面是否可用(Streaming 且 backbuffer 就绪; 决定画帧还是占位)。 */
static bool xgc_hasPicture(const XGuiClient* self)
{
    const XGuiClientPrivate* d;
    if (!self || !self->m_d) return false;
    d = (const XGuiClientPrivate*)self->m_d;
    return d->m_state == XGUI_REMOTE_STATE_STREAMING && d->m_fbValid &&
           !XImage_isNull(&d->m_fb);
}

/** @brief 输入转发通道是否就绪(转发开 + Streaming + 远端元信息已知)。 */
static bool xgc_inputChannelReady(const XGuiClient* self, bool enabled)
{
    const XGuiClientPrivate* d;
    if (!enabled || !self || !self->m_d) return false;
    d = (const XGuiClientPrivate*)self->m_d;
    return d->m_state == XGUI_REMOTE_STATE_STREAMING && d->m_fbValid;
}

/* ==================== 发送链(outbox + writeFrame 纪律) ==================== */

/*
 * 客户端发送帧小(输入/请求 ≤2KiB 级), 一般无部分写; 但仍按冻结纪律处理:
 * writeFrame 返回 < 总帧长时把余量入 outbox, poll 回调每圈按档位
 * txBudgetBytes 续传; outbox 非空时新帧整帧入队保序(不再直写)。
 */

/** @brief outbox 入队(容量帽 XGC_OUTBOX_CAP; 返回 false=溢出丢弃)。 */
static bool xgc_outboxEnqueue(XGuiClientPrivate* d,
                              const uint8_t* bytes, size_t count)
{
    size_t need;
    if (d->m_outboxOff > 0) {
        /* 压实: 已写出前缀丢弃, 余量搬到队头。 */
        size_t pending = d->m_outboxLen - d->m_outboxOff;
        if (pending > 0) {
            memmove(d->m_outbox, d->m_outbox + d->m_outboxOff, pending);
        }
        d->m_outboxLen = pending;
        d->m_outboxOff = 0;
    }
    need = d->m_outboxLen + count;
    if (need > XGC_OUTBOX_CAP) return false;
    if (need > d->m_outboxCap) {
        size_t cap = d->m_outboxCap ? d->m_outboxCap : 4096;
        uint8_t* p;
        while (cap < need) cap *= 2;
        p = (uint8_t*)XRealloc_System(d->m_outbox, cap);
        if (!p) return false;
        d->m_outbox = p;
        d->m_outboxCap = cap;
    }
    memcpy(d->m_outbox + d->m_outboxLen, bytes, count);
    d->m_outboxLen += count;
    return true;
}

/**
 * @brief  发送一帧(writeFrame 冻结纪律: 返回值≥0, 短写余量入 outbox)。
 * @return true 已被设备/队列接受(整帧); false 硬错误或溢出丢弃。
 */
static bool xgc_sendFrame(XGuiClient* self, XGuiRemoteMsgType type,
                          const void* payload, size_t payloadBytes)
{
    XGuiClientPrivate* d;
    /* 客户端消息上限自守: IME 双文本满载(2+1024+2+1024+16)亦远小于此。 */
    uint8_t frame[XGUI_REMOTE_FRAME_HEADER_BYTES +
                  2u * XGUI_REMOTE_MAX_TEXT_BYTES + 32u];
    size_t total;
    int64_t accepted;
    if (!self || !self->m_d) return false;
    d = (XGuiClientPrivate*)self->m_d;
    if (!d->m_dev) return false;
    if (payloadBytes > sizeof(frame) - XGUI_REMOTE_FRAME_HEADER_BYTES) {
        return false;
    }
    total = XGUI_REMOTE_FRAME_HEADER_BYTES + payloadBytes;
    XGuiRemoteProto_putU32(frame, (uint32_t)payloadBytes);
    frame[4] = (uint8_t)type;
    if (payloadBytes != 0) {
        memcpy(frame + XGUI_REMOTE_FRAME_HEADER_BYTES, payload, payloadBytes);
    }

    if (d->m_outboxLen > d->m_outboxOff) {
        /* 有待写余量: 整帧入队保序(冻结纪律: 待写清空前不直写新帧)。 */
        if (!xgc_outboxEnqueue(d, frame, total)) {
            if (!d->m_overflowNotified) {
                d->m_overflowNotified = true;
                xgc_emitError(self, (int)XGUI_REMOTE_ERR_BUFFER_OVERFLOW);
            }
            return false;
        }
        return true;
    }

    accepted = XGuiRemoteProto_writeFrame(d->m_dev, type, payload, payloadBytes);
    if (accepted < 0) return false; /* 设备硬错误, 上层按断链处理。 */
    d->m_stats.bytesSent += (uint64_t)accepted;
    if ((size_t)accepted < total) {
        /* 部分写=背压: 余量入 outbox 留待下圈续传。 */
        if (!xgc_outboxEnqueue(d, frame + (size_t)accepted,
                               total - (size_t)accepted)) {
            if (!d->m_overflowNotified) {
                d->m_overflowNotified = true;
                xgc_emitError(self, (int)XGUI_REMOTE_ERR_BUFFER_OVERFLOW);
            }
            return false;
        }
    }
    return true;
}

/** @brief poll 回调内续传 outbox 余量(限档位 txBudgetBytes 预算)。 */
static void xgc_flushOutbox(XGuiClient* self)
{
    XGuiClientPrivate* d;
    size_t budget;
    if (!self || !self->m_d) return;
    d = (XGuiClientPrivate*)self->m_d;
    if (!d->m_dev || d->m_outboxLen <= d->m_outboxOff) return;
    budget = d->m_profile.txBudgetBytes;
    if (budget == 0) budget = XGC_OUTBOX_CAP; /* 档位 0=不限(防御死循环)。 */
    while (d->m_outboxLen > d->m_outboxOff && budget > 0) {
        size_t remain = d->m_outboxLen - d->m_outboxOff;
        size_t want = remain < budget ? remain : budget;
        int64_t written = XIODevice_write_1(d->m_dev,
                (const char*)(d->m_outbox + d->m_outboxOff), (int64_t)want);
        if (written < 0) return;             /* 硬错误: 交由断链路径处理。 */
        if (written == 0) break;             /* 设备暂不可写, 下圈再试。 */
        d->m_outboxOff += (size_t)written;
        d->m_stats.bytesSent += (uint64_t)written;
        budget -= ((size_t)written < budget) ? (size_t)written : budget;
    }
    if (d->m_outboxOff >= d->m_outboxLen) {
        d->m_outboxOff = 0;
        d->m_outboxLen = 0;
    }
}

/* ==================== 键盘按下集合(PRESS/RELEASE 成对纪律) ==================== */

static bool xgc_pressedKeysAdd(XGuiClientPrivate* d, uint32_t key)
{
    int i;
    for (i = 0; i < d->m_pressedKeyCount; ++i) {
        if (d->m_pressedKeys[i] == key) return true; /* 已在集合(幂等)。 */
    }
    if (d->m_pressedKeyCount >= d->m_pressedKeyCap) {
        int cap = d->m_pressedKeyCap ? d->m_pressedKeyCap * 2 : 8;
        uint32_t* p = (uint32_t*)XRealloc_System(d->m_pressedKeys,
                                                 (size_t)cap * sizeof(uint32_t));
        if (!p) return false;
        d->m_pressedKeys = p;
        d->m_pressedKeyCap = cap;
    }
    d->m_pressedKeys[d->m_pressedKeyCount++] = key;
    return true;
}

static bool xgc_pressedKeysRemove(XGuiClientPrivate* d, uint32_t key)
{
    int i;
    for (i = 0; i < d->m_pressedKeyCount; ++i) {
        if (d->m_pressedKeys[i] == key) {
            int j;
            for (j = i + 1; j < d->m_pressedKeyCount; ++j) {
                d->m_pressedKeys[j - 1] = d->m_pressedKeys[j];
            }
            --d->m_pressedKeyCount;
            return true;
        }
    }
    return false;
}

/* ==================== 会话建立与拆除 ==================== */

/** @brief 发 HELLO(版本/能力/认证建议/端名, §3.4; BANNER_WAIT 后调用)。 */
static void xgc_sendHello(XGuiClient* self)
{
    XGuiClientPrivate* d;
    XGuiRemoteMsgHello hello;
    const char* name = "XGuiClient";
    size_t n;
    if (!self || !self->m_d) return;
    d = (XGuiClientPrivate*)self->m_d;
    if (!d->m_dev) return;
    XMemset(&hello, 0, sizeof(hello));
    hello.protocolVersion = XGUI_REMOTE_PROTOCOL_VERSION;
    hello.capabilities = d->m_localCaps;
    hello.authMethod = (uint8_t)(d->m_password[0] != '\0'
            ? XGUI_REMOTE_AUTH_SHA256_CHALLENGE : XGUI_REMOTE_AUTH_NONE);
    n = strlen(name);
    if (n > XGUI_REMOTE_MAX_NAME_BYTES) n = XGUI_REMOTE_MAX_NAME_BYTES;
    hello.nameBytes = (uint16_t)n;
    memcpy(hello.name, name, n);
    /* 冻结契约(XGuiRemoteProto.h "严禁把结构体整块 cast 上网"): 先经 enc*
     * 显式小端序列化再上网(修复: 原直发 sizeof(struct) 含 padding 且字段
     * 错位, 服务端严格 dec(off==len) 必拒 → 握手必败)。 */
    {
        uint8_t buf[XGUI_REMOTE_MAX_NAME_BYTES + 16];
        size_t len = XGuiRemoteProto_encHello(buf, sizeof(buf), &hello);
        if (len > 0)
            xgc_sendFrame(self, XGUI_REMOTE_MSG_HELLO, buf, len);
    }
}

/** @brief 重连定时器回调(指数退避重连; §7.3)。 */
static void xgc_reconnectFire(void* userData, XTimerData* timer)
{
    XGuiClient* self = (XGuiClient*)userData;
    XGuiClientPrivate* d;
    (void)timer;
    if (!self || !self->m_d) return;
    d = (XGuiClientPrivate*)self->m_d;
    if (d->m_state != XGUI_REMOTE_STATE_DISCONNECTED || !d->m_wantConnect ||
        d->m_dev != NULL) {
        return;
    }
    xgc_connectNetwork(self); /* 复用网络连接路径(重试计数继续累加)。 */
}

/** @brief 安排下一次重连(基础间隔 ×2^n, 上限 30s)。 */
static void xgc_scheduleReconnect(XGuiClient* self)
{
    XGuiClientPrivate* d;
    uint32_t delay;
    uint32_t shift;
    uint32_t i;
    if (!self || !self->m_d) return;
    d = (XGuiClientPrivate*)self->m_d;
    if (!d->m_autoReconnect || !d->m_wantConnect || !d->m_reconnectTimer) {
        return;
    }
    ++d->m_reconnectAttempts;
    ++d->m_reconnectCountTotal; /* 演示页统计: 累计口径, 达成不清零。 */
    delay = d->m_reconnectBaseMs;
    shift = d->m_reconnectAttempts;
    if (shift > 16) shift = 16;
    /* delay = base × 2^(attempts-1), 逐次翻倍防溢出。 */
    for (i = 1; i < shift && delay < XGC_RECONNECT_MAX_MS; ++i) {
        delay *= 2u;
    }
    if (delay > XGC_RECONNECT_MAX_MS) delay = XGC_RECONNECT_MAX_MS;
    XTimer_setTimeout(d->m_reconnectTimer, delay);
    XTimer_start_base(d->m_reconnectTimer);
}

/**
 * @brief  拆除当前会话(协议断开/传输丢失/静默换接/析构共用)。
 * @param  reason   断开原因(disconnected 信号载荷)。
 * @param  errCode  会话错误码(NONE=不发 errorOccurred)。
 * @param  sendBye  true=设备仍开且会话就绪时先补发 RELEASE 纪律帧+BYE。
 * @param  silent   true=静默拆除(不发 disconnected/errorOccurred/不重连,
 *                  connectToHost 重入与析构用)。
 */
static void xgc_teardown(XGuiClient* self, XGuiRemoteByeReason reason,
                         XGuiRemoteError errCode, bool sendBye, bool silent)
{
    XGuiClientPrivate* d;
    XIODevice* dev;
    if (!self || !self->m_d) return;
    d = (XGuiClientPrivate*)self->m_d;
    dev = d->m_dev;
    if (dev == NULL && d->m_state == XGUI_REMOTE_STATE_DISCONNECTED) {
        return; /* 已拆除, 幂等。 */
    }
#if defined(XGC_TILE_DEBUG) && XGC_TILE_DEBUG
    fprintf(stderr, "XGC_TEARDOWN reason=%d err=%d sendBye=%d state=%d\n",
            (int)reason, (int)errCode, (int)sendBye, (int)d->m_state);
#endif

    /* PRESS/RELEASE 成对纪律(§7.4): 断链前向远端补发全部 RELEASE,
     * 防服务端 grabMouse/按键滞留; 设备已死则直接清集合。 */
    if (dev != NULL && sendBye && d->m_sessionReady) {
        int i;
        for (i = 0; i < d->m_pressedKeyCount; ++i) {
            XGuiRemoteMsgInputKey key;
            uint8_t kbuf[24];
            size_t klen;
            XMemset(&key, 0, sizeof(key));
            key.action = XGUI_REMOTE_KEY_RELEASE;
            key.key = d->m_pressedKeys[i];
            key.timestampMs = (uint32_t)xgc_nowMs();
            /* 冻结契约: enc* 显式序列化后上网(修复: 结构体直发)。 */
            klen = XGuiRemoteProto_encInputKey(kbuf, sizeof(kbuf), &key);
            if (klen > 0)
                xgc_sendFrame(self, XGUI_REMOTE_MSG_INPUT_KEY, kbuf, klen);
        }
        if (d->m_pressedButtons != 0) {
            int bit;
            for (bit = 0; bit < 16; ++bit) {
                uint16_t mask = (uint16_t)(1u << bit);
                if (d->m_pressedButtons & mask) {
                    XGuiRemoteMsgInputPointer ptr;
                    uint8_t pbuf[24];
                    size_t plen;
                    XMemset(&ptr, 0, sizeof(ptr));
                    ptr.action = XGUI_REMOTE_PTR_RELEASE;
                    ptr.button = (uint8_t)mask;
                    ptr.buttons = 0;
                    ptr.timestampMs = (uint32_t)xgc_nowMs();
                    plen = XGuiRemoteProto_encInputPointer(pbuf, sizeof(pbuf),
                                                           &ptr);
                    if (plen > 0)
                        xgc_sendFrame(self, XGUI_REMOTE_MSG_INPUT_POINTER,
                                      pbuf, plen);
                }
            }
        }
        {
            XGuiRemoteMsgBye bye;
            uint8_t bbuf[XGUI_REMOTE_MAX_MSG_BYTES + 16];
            size_t blen;
            XMemset(&bye, 0, sizeof(bye));
            bye.reason = (uint8_t)reason;
            blen = XGuiRemoteProto_encBye(bbuf, sizeof(bbuf), &bye);
            if (blen > 0)
                xgc_sendFrame(self, XGUI_REMOTE_MSG_BYE, bbuf, blen);
        }
        xgc_flushOutbox(self); /* BYE/RELEASE 余量尽力而出(一圈内)。 */
    }
    d->m_pressedButtons = 0;
    d->m_pressedKeyCount = 0;
    d->m_hasPendingMove = false;
    d->m_pingAwait = false;
    d->m_challengeValid = false;

    /* 画面作废(§7.3): 不绘制陈旧帧, backbuffer 待下次 FB_META 重建。 */
    d->m_fbValid = false;

    /* outbox 是会话级余量缓冲, 断链即弃(统计保留累加值)。 */
    d->m_outboxLen = 0;
    d->m_outboxOff = 0;
    d->m_overflowNotified = false;
    d->m_sessionReady = false;
    d->m_bannerSent = false;
    d->m_bannerGot = false;
    d->m_bannerGotBytes = 0;
    d->m_state = XGUI_REMOTE_STATE_DISCONNECTED;

    /* 帧 reader 复位(内部缓冲保留复用, 析构时统一释放)。 */
    XGuiRemoteFrameReader_deinit(&d->m_reader);
    XGuiRemoteFrameReader_init(&d->m_reader);

    /* 设备处置: 自有 socket 中止+延迟销毁; 借用设备仅解除引用。
     * 先清 m_dev 再 abort——socket 的 disconnected 信号回入本控件时
     * 以 m_dev==NULL 判定幂等, 不重复断链。 */
    d->m_dev = NULL;
    if (dev != NULL) {
        if (d->m_devOwned) {
            XAbstractSocket_abort((XAbstractSocket*)dev);
            XObject_deleteLater((XObject*)dev);
            d->m_devOwned = false;
        }
        d->m_devIsSsl = false;
    }

    /* poll 回调注销(无设备即无泵)。 */
    if (d->m_pollHandle) {
        XAbstractEventDispatcher_removePollCallback(d->m_pollHandle);
        d->m_pollHandle = NULL;
    }
    if (d->m_pumpTimer) { /* 兜底定时器随会话同生死(基准优化 2026-10-02)。 */
        XTimer_stop_base(d->m_pumpTimer);
        XObject_deleteLater((XObject*)d->m_pumpTimer);
        d->m_pumpTimer = NULL;
    }

    if (silent) return;
    if (errCode != XGUI_REMOTE_ERR_NONE) {
        xgc_emitError(self, (int)errCode);
    }
    xgc_emit(self, (size_t)XGuiClient_disconnected_signal,
             XVarList_Create(XVar(int, reason)));
    xgc_scheduleReconnect(self);
}

/** @brief 对端横幅校验通过(§3.3): 魔数 memcmp + 版本下限。 */
static void xgc_bannerComplete(XGuiClient* self)
{
    XGuiClientPrivate* d;
    if (!self || !self->m_d) return;
    d = (XGuiClientPrivate*)self->m_d;
    d->m_bannerGot = true;
    if (!XGuiRemoteProto_bannerIsValid(d->m_bannerBuf)) {
        xgc_teardown(self, XGUI_REMOTE_BYE_PROTOCOL_ERROR,
                     XGUI_REMOTE_ERR_PROTOCOL, true, false);
        return;
    }
    if (XGuiRemoteProto_bannerVersion(d->m_bannerBuf) <
        XGUI_REMOTE_PROTOCOL_VERSION) {
        /* 对端版本低于本端可支持最小版本 → BYE(版本不匹配), §3.5。 */
        xgc_teardown(self, XGUI_REMOTE_BYE_VERSION,
                     XGUI_REMOTE_ERR_VERSION, true, false);
        return;
    }
    if (d->m_bannerSent) {
        d->m_state = XGUI_REMOTE_STATE_HANDSHAKING;
        xgc_sendHello(self); /* 双方横幅齐 → 我方 HELLO(§3.7)。 */
    }
}

/** @brief 对端横幅收齐且本端横幅已发足 → 发 HELLO。 */
static void xgc_sendHelloIfBannersDone(XGuiClient* self)
{
    XGuiClientPrivate* d;
    if (!self || !self->m_d) return;
    d = (XGuiClientPrivate*)self->m_d;
    if (d->m_bannerSent && d->m_bannerGot &&
        d->m_state == XGUI_REMOTE_STATE_BANNER_WAIT) {
        d->m_state = XGUI_REMOTE_STATE_HANDSHAKING;
        xgc_sendHello(self);
    }
}

/** @brief 本端横幅未发足时补发(横幅 8 字节级, TCP 几乎不会部分写)。 */
static void xgc_ensureBannerSent(XGuiClient* self)
{
    XGuiClientPrivate* d;
    uint8_t banner[XGUI_REMOTE_BANNER_BYTES];
    int64_t written;
    if (!self || !self->m_d) return;
    d = (XGuiClientPrivate*)self->m_d;
    if (!d->m_dev || !d->m_sessionReady || d->m_bannerSent) return;
    XGuiRemoteProto_makeBanner(banner);
    written = XIODevice_write_1(d->m_dev, (const char*)banner,
                                (int64_t)XGUI_REMOTE_BANNER_BYTES);
    if (written > 0) d->m_stats.bytesSent += (uint64_t)written;
    d->m_bannerSent = (written == (int64_t)XGUI_REMOTE_BANNER_BYTES);
    xgc_sendHelloIfBannersDone(self);
}

/** @brief 设备就绪(明文 connected / TLS 加密完成): 发横幅进入 BANNER_WAIT。 */
static void xgc_sessionDeviceReady(XGuiClient* self)
{
    XGuiClientPrivate* d;
    uint8_t banner[XGUI_REMOTE_BANNER_BYTES];
    int64_t written;
    if (!self || !self->m_d) return;
    d = (XGuiClientPrivate*)self->m_d;
    if (!d->m_dev || d->m_sessionReady) return;
    if (!XIODevice_isOpen(d->m_dev)) return;

    /* 注册 poll 回调(无事件循环时句柄为 NULL, 会话无法泵帧——部署形态
     * 限制, 下次连接重试)。 */
    if (!d->m_pollHandle) {
        d->m_pollHandle = XAbstractEventDispatcher_addPollCallback(
            xgc_pollPump, self);
    }
    /* 兜底定时器(基准优化 2026-10-02): 直呼 pollPump, 把空闲事件循环下的
     * 收发延迟钳到 ≤8ms(实测 resource 档收侧 ~170ms 散布的平台唤醒问题,
     * 在 Remote 层内兜底; 会话断开即停, 空转近零成本)。 */
    if (d->m_pollHandle && !d->m_pumpTimer) {
        d->m_pumpTimer = XTimer_create_ex(XCLASS_DEFAULT_MEMORY_TYPE);
        if (d->m_pumpTimer) {
            XTimer_setInterval(d->m_pumpTimer, 8);
            XTimer_setTimerCallback(d->m_pumpTimer, xgc_pumpTimerCb);
            XTimer_setUserData(d->m_pumpTimer, self);
            XTimer_start_base(d->m_pumpTimer);
        }
    }

    d->m_sessionReady = true;
    d->m_state = XGUI_REMOTE_STATE_BANNER_WAIT;
    d->m_lastPeerFrameMs = (uint64_t)xgc_nowMs();
    d->m_lastPingSentMs = d->m_lastPeerFrameMs;
    d->m_fpsWindowStartMs = d->m_lastPeerFrameMs;
    d->m_fpsWindowUpdates = 0;

    XGuiRemoteProto_makeBanner(banner);
    written = XIODevice_write_1(d->m_dev, (const char*)banner,
                                (int64_t)XGUI_REMOTE_BANNER_BYTES);
    if (written > 0) d->m_stats.bytesSent += (uint64_t)written;
    d->m_bannerSent = (written == (int64_t)XGUI_REMOTE_BANNER_BYTES);
    xgc_sendHelloIfBannersDone(self);
}

/* ==================== FB_META / FB_UPDATE(画面合成, §7.2) ==================== */

/* 基准追踪(XGUI_REMOTE_BENCH_TRACE=1 启用; 2026-10-02 延迟基准诊断,
 * stderr 单行, 未启用零成本)。 */
/* [2026-10-03 互联测试指挥官] tile 逐枚踪(XGC_TILE_DEBUG=1 编译期启用):
 * 真机镜像碎片/黑洞定位用, 生产零成本。 */
#ifndef XGC_TILE_DEBUG
#define XGC_TILE_DEBUG 0
#endif

/* 档位变化逐次踪(XGC_PROFILE_DEBUG=1 编译期启用): 真机互联切档取证用,
 * 生产零成本。 */
#ifndef XGC_PROFILE_DEBUG
#define XGC_PROFILE_DEBUG 0
#endif

static int xgc_benchTrace = -1;
static int xgc_benchTraceOn(void)
{
    if (xgc_benchTrace < 0)
        xgc_benchTrace =
            (XSystem_environment("XGUI_REMOTE_BENCH_TRACE") != NULL);
    return xgc_benchTrace;
}

/** @brief 线上格式 → 本地 XImage 格式(1:1 解至本地, 无转换绘制)。 */
static XImageFormat xgc_localImageFormat(uint8_t wireFormat)
{
    if (wireFormat == (uint8_t)XGUI_REMOTE_PF_RGB565) {
        return XImageFormat_RGB16;
    }
    return XImageFormat_ARGB32; /* ARGB32(未知值按基线兜底)。 */
}

/** @brief 线上格式像素字节数。 */
static int xgc_formatBpp(uint8_t wireFormat)
{
    return (wireFormat == (uint8_t)XGUI_REMOTE_PF_RGB565) ? 2 : 4;
}

/** @brief FB_META: 重建 backbuffer + setFixedSize(1:1) + 进入 Streaming。 */
static void xgc_handleFbMeta(XGuiClient* self, const XGuiRemoteMsgFbMeta* meta)
{
    XGuiClientPrivate* d;
    bool sizeChanged;
    if (!self || !self->m_d || !meta) return;
    d = (XGuiClientPrivate*)self->m_d;
    if (meta->width == 0 || meta->height == 0) return; /* 非法元信息忽略。 */

    /* 重建条件须含线上格式变化(档位热切换 performance↔resource 同尺寸
     * 换格式, XGuiRemote.md §5.3): 只比尺寸会把 RGB565 tile 以 2B/px
     * 写进残留的 ARGB32(4B/px) backbuffer——水平 2:1 压缩+色道错乱
     * (E2E 资源档换档实测复现, 2026-10-02)。 */
    sizeChanged = !d->m_fbValid || (meta->width != d->m_fbWidth) ||
                  (meta->height != d->m_fbHeight) || XImage_isNull(&d->m_fb) ||
                  (XImage_format(&d->m_fb) !=
                   xgc_localImageFormat(meta->format));
    /* 档位变化诊断(仅变化时一行, 互联测试切档断言的 grep 证据锚;
     * FB_META 宣告档位 = 服务端协商生效的端到端事实, 2026-10-03)。 */
#if defined(XGC_PROFILE_DEBUG) && XGC_PROFILE_DEBUG
    if (meta->profileId != d->m_profileId) {
        fprintf(stderr, "XGuiClient: fb-meta profile-id=%u\n",
                (unsigned)meta->profileId);
    }
#endif
    d->m_fbWidth = meta->width;
    d->m_fbHeight = meta->height;
    d->m_fbFormat = meta->format;
    d->m_tileWidth = meta->tileWidth;
    d->m_tileHeight = meta->tileHeight;
    d->m_profileId = meta->profileId;

    /* 标题(flags bit0 有效)。 */
    if (meta->flags & 0x01u) {
        int n = meta->titleBytes;
        if (n > XGUI_REMOTE_MAX_NAME_BYTES) n = XGUI_REMOTE_MAX_NAME_BYTES;
        if (n > 0) memcpy(d->m_title, meta->title, (size_t)n);
        d->m_title[n] = '\0';
        d->m_titleBytes = n;
    } else {
        d->m_title[0] = '\0';
        d->m_titleBytes = 0;
    }

    /* 档位跟随(§5.2: 双方各自持有副本; FB_META 宣告档位, 本端本地参数
     * 跟随预设——移动合并窗口/保活间隔/写出预算随之生效; CUSTOM 保留现值)。 */
    if (d->m_profileId == (uint8_t)XGUI_REMOTE_PROFILE_PERFORMANCE) {
        XGuiRemoteProfile_initPerformance(&d->m_profile);
    } else if (d->m_profileId == (uint8_t)XGUI_REMOTE_PROFILE_RESOURCE ||
               d->m_profileId == (uint8_t)XGUI_REMOTE_PROFILE_AUTO) {
        XGuiRemoteProfile_initResource(&d->m_profile);
    }
    XGuiRemoteProfile_sanitize(&d->m_profile);

    if (sizeChanged) {
        if (!XImage_reinit_ex(&d->m_fb, (int)d->m_fbWidth, (int)d->m_fbHeight,
                              xgc_localImageFormat(d->m_fbFormat))) {
            d->m_fbValid = false; /* 分配失败: 下批 META 重试。 */
            return;
        }
    }
    d->m_fbValid = true;

    /* 控件采用远端尺寸(§7.2: setFixedSize 语义, V1 恒 1:1)。 */
    XWidget_setFixedSize((XWidget*)self, (int)d->m_fbWidth,
                         (int)d->m_fbHeight);

    if (d->m_state != XGUI_REMOTE_STATE_STREAMING) {
        d->m_state = XGUI_REMOTE_STATE_STREAMING;
        d->m_reconnectAttempts = 0; /* 连接达成, 退避计数复位。 */
        xgc_emit(self, (size_t)XGuiClient_connected_signal, NULL);
    }
    xgc_emit(self, (size_t)XGuiClient_remoteMetaChanged_signal, NULL);

    /* 全量刷新(§3.7: FB_META → FB_REQUEST(mode=1) → 整幅 tile 批;
     * §7.3: 重连成功自动全量——每会话首次 META 走同一入口)。 */
    {
        XGuiRemoteMsgFbRequest req;
        uint8_t rbuf[16];
        size_t rlen;
        XMemset(&req, 0, sizeof(req));
        req.mode = 1; /* 全量刷新。 */
        /* 冻结契约: enc* 显式序列化后上网(修复: 结构体直发含 padding,
         * 10B ≠ 线上 9B, 服务端严格 dec 必拒)。 */
        rlen = XGuiRemoteProto_encFbRequest(rbuf, sizeof(rbuf), &req);
        if (rlen > 0)
            xgc_sendFrame(self, XGUI_REMOTE_MSG_FB_REQUEST, rbuf, rlen);
    }
}

/** @brief 单 tile 解码直写 backbuffer(带 stride), 成功并入损伤区。 */
static void xgc_decodeTile(XGuiClient* self, const XGuiRemoteMsgFbTile* tile,
                           uint8_t wireFormat)
{
    XGuiClientPrivate* d;
    int bpp = xgc_formatBpp(wireFormat);
    int tx;
    int ty;
    int tw;
    int th;
    int stride;
    uint8_t* row;
    XRect r;
    if (!self || !self->m_d || !tile) return;
    d = (XGuiClientPrivate*)self->m_d;
    if (XImage_isNull(&d->m_fb) || bpp == 0) return;

    tx = tile->x;
    ty = tile->y;
    tw = tile->w;
    th = tile->h;
    /* tile 裁剪进远端画面边界(越界 tile 跳过, 不作协议错误断链)。 */
    if (tx >= (int)d->m_fbWidth || ty >= (int)d->m_fbHeight) return;
    if (tx + tw > (int)d->m_fbWidth) tw = (int)d->m_fbWidth - tx;
    if (ty + th > (int)d->m_fbHeight) th = (int)d->m_fbHeight - ty;
    if (tw <= 0 || th <= 0) return;

    row = XImage_scanLine(&d->m_fb, ty);
    if (!row) return;
    stride = XImage_bytesPerLine(&d->m_fb);
    if (XGuiRemoteCodec_decodeTile(tile->payload, tile->payloadBytes,
                                   (XGuiRemoteCodecId)tile->codec,
                                   (XGuiRemotePixelFormat)wireFormat,
                                   row + (size_t)tx * (size_t)bpp, stride,
                                   (XGuiRemotePixelFormat)wireFormat,
                                   tw, th) != 0) {
        return; /* 解码失败(截断/编解码器未编译): 跳过该 tile。 */
    }
    XRect_init(&r, tx, ty, tw, th);
    XRegion_addRect(&d->m_damage, &r);
}

/** @brief FB_UPDATE: 帧级头 + 逐 tile(偏移自 FB_UPDATE_HEADER_BYTES)。 */
static void xgc_handleFbUpdate(XGuiClient* self, const uint8_t* payload,
                               size_t len)
{
    XGuiClientPrivate* d;
    XGuiRemoteMsgFbUpdate upd;
    size_t offset = XGUI_REMOTE_FB_UPDATE_HEADER_BYTES;
    uint16_t i;
    if (!self || !self->m_d) return;
    d = (XGuiClientPrivate*)self->m_d;
    if (len < XGUI_REMOTE_FB_UPDATE_HEADER_BYTES) {
        xgc_teardown(self, XGUI_REMOTE_BYE_PROTOCOL_ERROR,
                     XGUI_REMOTE_ERR_PROTOCOL, true, false);
        return;
    }
    if (!XGuiRemoteProto_decFbUpdate(payload, len, &upd)) {
        xgc_teardown(self, XGUI_REMOTE_BYE_PROTOCOL_ERROR,
                     XGUI_REMOTE_ERR_PROTOCOL, true, false);
        return;
    }
    if (upd.tileCount > 0 && !d->m_fbValid) {
        return; /* 元信息未就绪: 陈旧几何无意义, 丢帧等 META。 */
    }

    for (i = 0; i < upd.tileCount; ++i) {
        XGuiRemoteMsgFbTile tile;
        size_t next = 0;
        if (!XGuiRemoteProto_decFbTile(payload, len, offset, &tile, &next)) {
            xgc_teardown(self, XGUI_REMOTE_BYE_PROTOCOL_ERROR,
                         XGUI_REMOTE_ERR_PROTOCOL, true, false);
            return;
        }
        xgc_decodeTile(self, &tile, upd.format);
#if defined(XGC_TILE_DEBUG) && XGC_TILE_DEBUG
        if (d->m_stats.tileCount < 4000u)
            fprintf(stderr, "XGC_TILE #%u t=(%u,%u %ux%u) fmt=%u len=%u\n",
                    (unsigned)d->m_stats.tileCount,
                    (unsigned)tile.x, (unsigned)tile.y,
                    (unsigned)tile.w, (unsigned)tile.h,
                    (unsigned)upd.format, (unsigned)tile.payloadBytes);
#endif
        offset = next;
        ++d->m_stats.tileCount;
    }
    ++d->m_stats.updateCount;

    /* fpsMilli 滑动窗口估计(§7.5)。 */
    {
        int64_t now = xgc_nowMs();
        int64_t elapsed = now - (int64_t)d->m_fpsWindowStartMs;
        ++d->m_fpsWindowUpdates;
        if (elapsed >= (int64_t)XGC_FPS_WINDOW_MS) {
            d->m_stats.fpsMilli = (uint32_t)
                ((uint64_t)d->m_fpsWindowUpdates * 1000u /
                 (uint64_t)(elapsed > 0 ? elapsed : 1));
            d->m_fpsWindowStartMs = (uint64_t)now;
            d->m_fpsWindowUpdates = 0;
        }
    }

    /* 损伤区增量上屏(§7.2): tile 矩形=控件局部坐标(1:1)。 */
    if (!XRegion_isEmpty(&d->m_damage)) {
        XWidget_updateRegion((XWidget*)self, &d->m_damage);
        XRegion_clear(&d->m_damage);
    }
    if (xgc_benchTraceOn())
        fprintf(stderr, "XGC_TRACE fbup t=%llu tiles=%u\n",
                (unsigned long long)(uint64_t)xgc_nowMs(),
                (unsigned)upd.tileCount);
}

/* ==================== 认证(SHA256_CHALLENGE, §3.8) ==================== */

/** @brief 发 AUTH_RESPONSE: SHA256(SHA256(口令) || nonce)。 */
static void xgc_sendAuthResponse(XGuiClient* self)
{
    XGuiClientPrivate* d;
    XGuiRemoteMsgAuthResponse resp;
    uint8_t storedHash[XGUI_REMOTE_AUTH_RESPONSE_BYTES];
    uint8_t concat[XGUI_REMOTE_AUTH_RESPONSE_BYTES +
                   XGUI_REMOTE_AUTH_NONCE_BYTES];
    size_t nonceBytes;
    XByteArrayView view;
    if (!self || !self->m_d) return;
    d = (XGuiClientPrivate*)self->m_d;
    if (!d->m_challengeValid) return;

    XMemset(&resp, 0, sizeof(resp));
    /* 第一轮: storedHash = SHA256(password)。 */
    view = XCryptographicHash_hashInto((char*)storedHash, sizeof(storedHash),
                                       d->m_password, strlen(d->m_password),
                                       XCryptographicHash_Sha256);
    if (view.m_size != (int64_t)sizeof(storedHash)) return; /* 哈希失败。 */
    nonceBytes = d->m_challenge.nonceBytes;
    if (nonceBytes > XGUI_REMOTE_AUTH_NONCE_BYTES) {
        nonceBytes = XGUI_REMOTE_AUTH_NONCE_BYTES;
    }
    memcpy(concat, storedHash, sizeof(storedHash));
    memcpy(concat + sizeof(storedHash), d->m_challenge.nonce, nonceBytes);
    /* 第二轮: response = SHA256(storedHash || nonce)。 */
    view = XCryptographicHash_hashInto((char*)resp.response,
                                       sizeof(resp.response),
                                       (const char*)concat, sizeof(concat),
                                       XCryptographicHash_Sha256);
    if (view.m_size != (int64_t)sizeof(resp.response)) return;
    resp.responseBytes = XGUI_REMOTE_AUTH_RESPONSE_BYTES;
    /* 冻结契约: enc* 显式序列化后上网(修复: 结构体直发)。 */
    {
        uint8_t abuf[XGUI_REMOTE_AUTH_RESPONSE_BYTES + 8];
        size_t alen = XGuiRemoteProto_encAuthResponse(abuf, sizeof(abuf),
                                                      &resp);
        if (alen > 0)
            xgc_sendFrame(self, XGUI_REMOTE_MSG_AUTH_RESPONSE, abuf, alen);
    }
}

/** @brief 认证/握手收尾: 口令即焚(冻结头: 明文仅驻留至认证完成)。 */
static void xgc_handshakeDone(XGuiClient* self)
{
    XGuiClientPrivate* d;
    if (!self || !self->m_d) return;
    d = (XGuiClientPrivate*)self->m_d;
    XMemset(d->m_password, 0, sizeof(d->m_password));
    d->m_challengeValid = false;
    /* Streaming 状态等 FB_META 宣告画面参数后才进入(冻结头 connected
     * 信号口径: "首帧 FB_META 已收到"); 此处仅清认证在途态。 */
}

/* ==================== 帧分发(状态机) ==================== */

/** @brief 按会话状态分发一帧(未知类型跳过: 前向兼容, §3.4)。 */
static void xgc_dispatchFrame(XGuiClient* self, XGuiRemoteMsgType type,
                              const uint8_t* payload, size_t len)
{
    XGuiClientPrivate* d;
    if (!self || !self->m_d) return;
    d = (XGuiClientPrivate*)self->m_d;

    switch (type) {
    case XGUI_REMOTE_MSG_HELLO_ACK: {
        XGuiRemoteMsgHello ack;
        if (d->m_state != XGUI_REMOTE_STATE_HANDSHAKING) return;
        if (!XGuiRemoteProto_decHello(payload, len, &ack)) {
            xgc_teardown(self, XGUI_REMOTE_BYE_PROTOCOL_ERROR,
                         XGUI_REMOTE_ERR_PROTOCOL, true, false);
            return;
        }
        if (ack.protocolVersion < XGUI_REMOTE_PROTOCOL_VERSION) {
            xgc_teardown(self, XGUI_REMOTE_BYE_VERSION,
                         XGUI_REMOTE_ERR_VERSION, true, false);
            return;
        }
        /* 版本协商取 min(双方版本)(§3.5); 本端恒支持版本 1。 */
        d->m_negotiatedVersion =
            ack.protocolVersion < XGUI_REMOTE_PROTOCOL_VERSION
                ? ack.protocolVersion : (uint16_t)XGUI_REMOTE_PROTOCOL_VERSION;
        d->m_peerCaps = ack.capabilities & d->m_localCaps;
        if ((XGuiRemoteAuthMethod)ack.authMethod ==
            XGUI_REMOTE_AUTH_NONE) {
            xgc_handshakeDone(self); /* 无认证: 等 FB_META。 */
        } else if ((XGuiRemoteAuthMethod)ack.authMethod ==
                   XGUI_REMOTE_AUTH_SHA256_CHALLENGE) {
            d->m_authMethod = XGUI_REMOTE_AUTH_SHA256_CHALLENGE;
            d->m_state = XGUI_REMOTE_STATE_AUTHENTICATING;
        } else {
            /* 认证法为封闭枚举, 未知值=协议错误(§3.4 冻结口径)。 */
            xgc_teardown(self, XGUI_REMOTE_BYE_PROTOCOL_ERROR,
                         XGUI_REMOTE_ERR_PROTOCOL, true, false);
        }
        return;
    }
    case XGUI_REMOTE_MSG_AUTH_CHALLENGE: {
        if (d->m_state != XGUI_REMOTE_STATE_AUTHENTICATING) return;
        if (!XGuiRemoteProto_decAuthChallenge(payload, len, &d->m_challenge)) {
            xgc_teardown(self, XGUI_REMOTE_BYE_PROTOCOL_ERROR,
                         XGUI_REMOTE_ERR_PROTOCOL, true, false);
            return;
        }
        d->m_challengeValid = true;
        xgc_sendAuthResponse(self);
        return;
    }
    case XGUI_REMOTE_MSG_AUTH_RESULT: {
        XGuiRemoteMsgAuthResult result;
        if (d->m_state != XGUI_REMOTE_STATE_AUTHENTICATING) return;
        if (!XGuiRemoteProto_decAuthResult(payload, len, &result)) {
            xgc_teardown(self, XGUI_REMOTE_BYE_PROTOCOL_ERROR,
                         XGUI_REMOTE_ERR_PROTOCOL, true, false);
            return;
        }
        if (result.ok == 1) {
            xgc_handshakeDone(self);
        } else {
            /* 认证失败 → BYE(认证失败) → 断链(重连后仍需口令则再认证)。 */
            xgc_teardown(self, XGUI_REMOTE_BYE_AUTH_FAILED,
                         XGUI_REMOTE_ERR_AUTH, true, false);
        }
        return;
    }
    case XGUI_REMOTE_MSG_FB_META: {
        XGuiRemoteMsgFbMeta meta;
        if (!XGuiRemoteProto_decFbMeta(payload, len, &meta)) {
            xgc_teardown(self, XGUI_REMOTE_BYE_PROTOCOL_ERROR,
                         XGUI_REMOTE_ERR_PROTOCOL, true, false);
            return;
        }
        xgc_handleFbMeta(self, &meta);
        return;
    }
    case XGUI_REMOTE_MSG_FB_UPDATE: {
        xgc_handleFbUpdate(self, payload, len);
        return;
    }
    case XGUI_REMOTE_MSG_PROFILE_RESULT: {
        XGuiRemoteMsgProfileResult result;
        if (!XGuiRemoteProto_decProfileResult(payload, len, &result)) {
            xgc_teardown(self, XGUI_REMOTE_BYE_PROTOCOL_ERROR,
                         XGUI_REMOTE_ERR_PROTOCOL, true, false);
            return;
        }
        /* 接受与否均以远端 FB_META(新参数广播)为准(§5.3); 此处无状态。 */
        (void)result;
        return;
    }
    case XGUI_REMOTE_MSG_PING: {
        uint64_t ts = 0;
        if (XGuiRemoteProto_decPing(payload, len, &ts)) {
            /* PONG 回显对端时刻(双向保活, §3.4); 冻结契约: enc* 显式
             * 小端序列化后上网(修复: 结构体直发在大端机字节序颠倒)。 */
            uint8_t pbuf[8];
            size_t plen = XGuiRemoteProto_encPing(pbuf, sizeof(pbuf), ts);
            if (plen > 0)
                xgc_sendFrame(self, XGUI_REMOTE_MSG_PONG, pbuf, plen);
        }
        return;
    }
    case XGUI_REMOTE_MSG_PONG: {
        uint64_t ts = 0;
        if (XGuiRemoteProto_decPing(payload, len, &ts) && d->m_pingAwait &&
            ts == d->m_pingSentTs) {
            int64_t rtt = xgc_nowMs() - (int64_t)ts;
            d->m_stats.rttMs = rtt > 0 ? (uint32_t)rtt : 0;
            d->m_pingAwait = false;
        }
        return;
    }
    case XGUI_REMOTE_MSG_BYE: {
        XGuiRemoteMsgBye bye;
        if (!XGuiRemoteProto_decBye(payload, len, &bye)) {
            xgc_teardown(self, XGUI_REMOTE_BYE_PROTOCOL_ERROR,
                         XGUI_REMOTE_ERR_PROTOCOL, true, false);
            return;
        }
        /* 对端优雅断开: 不补发 BYE(否则双 BYE)。 */
        xgc_teardown(self, (XGuiRemoteByeReason)bye.reason,
                     XGUI_REMOTE_ERR_NONE, false, false);
        return;
    }
    case XGUI_REMOTE_MSG_ERROR: {
        XGuiRemoteMsgError err;
        if (!XGuiRemoteProto_decError(payload, len, &err)) {
            xgc_teardown(self, XGUI_REMOTE_BYE_PROTOCOL_ERROR,
                         XGUI_REMOTE_ERR_PROTOCOL, true, false);
            return;
        }
        xgc_emitError(self, (int)err.code);
        return;
    }
    default:
        return; /* 未知名节/类型: 帧层前向兼容跳过(§3.4)。 */
    }
}

/* ==================== poll 帧泵(GUI 线程, §4.4) ==================== */

/** @brief 读泵: 横幅攒字节 → 帧泵 → 状态机分发(单圈限预算)。 */
static void xgc_pumpRead(XGuiClient* self)
{
    XGuiClientPrivate* d;
    char buf[XGC_READ_CHUNK];
    size_t budget = XGC_READ_BUDGET_PER_POLL;
    if (!self || !self->m_d) return;
    d = (XGuiClientPrivate*)self->m_d;
    if (!d->m_dev || !d->m_sessionReady) return;

    while (budget > 0) {
        int64_t avail = XIODevice_bytesAvailable_base(d->m_dev);
        int64_t want;
        int64_t got;
        const uint8_t* p;
        size_t n;
        int r;
        if (avail <= 0) break;
        want = avail < (int64_t)XGC_READ_CHUNK ? avail
                                               : (int64_t)XGC_READ_CHUNK;
        if ((size_t)want > budget) want = (int64_t)budget;
        got = XIODevice_read_1(d->m_dev, buf, want);
        if (got <= 0) break;
        d->m_stats.bytesReceived += (uint64_t)got;
        d->m_lastPeerFrameMs = (uint64_t)xgc_nowMs();
        budget -= (size_t)got;

        p = (const uint8_t*)buf;
        n = (size_t)got;
        /* 横幅攒字节(裸 8 字节, 非长度前缀帧)。 */
        while (!d->m_bannerGot && n > 0) {
            size_t need = (size_t)(XGUI_REMOTE_BANNER_BYTES -
                                   d->m_bannerGotBytes);
            size_t take = n < need ? n : need;
            memcpy(d->m_bannerBuf + d->m_bannerGotBytes, p, take);
            d->m_bannerGotBytes += (int)take;
            p += take;
            n -= take;
            if (d->m_bannerGotBytes == XGUI_REMOTE_BANNER_BYTES) {
                xgc_bannerComplete(self);
                if (!d->m_dev) return; /* 校验失败已断链。 */
            }
        }
        if (n == 0) continue;

        /* 帧泵: 凑满一帧交一帧(冻结头典型用法)。 */
        r = XGuiRemoteFrameReader_feed(&d->m_reader, p, n);
        while (r > 0) {
            if (xgc_benchTraceOn() &&
                d->m_reader.type == XGUI_REMOTE_MSG_FB_UPDATE)
                fprintf(stderr, "XGC_TRACE recv t=%llu bytes=%zu\n",
                        (unsigned long long)(uint64_t)xgc_nowMs(),
                        d->m_reader.payloadLen + 5u);
            xgc_dispatchFrame(self, d->m_reader.type, d->m_reader.payload,
                              d->m_reader.payloadLen);
            if (!d->m_dev) return; /* 帧内触发断链。 */
            r = XGuiRemoteFrameReader_feed(&d->m_reader, NULL, 0);
        }
        if (r < 0) {
            xgc_teardown(self, XGUI_REMOTE_BYE_PROTOCOL_ERROR,
                         XGUI_REMOTE_ERR_PROTOCOL, true, false);
            return;
        }
    }
}

/** @brief poll 回调: TLS 门控 → outbox 续传 → 横幅补发 → 保活 → 读泵。 */
static bool xgc_pollPump(void* userData)
{
    XGuiClient* self = (XGuiClient*)userData;
    XGuiClientPrivate* d;
    int64_t now;
    if (!self || !self->m_d) return true;
    d = (XGuiClientPrivate*)self->m_d;
    if (!d->m_dev) return true;

    /* 会话就绪检查: TLS 档加密完成前不泵会话(横幅必须走密文信道);
     * 明文档等 connected 信号(亦兜底 isOpen)。TLS 握手由读写路径驱动
     * (xssl_v_writeData/readData 每调必泵)——本圈一次 0 长写专用于推进
     * 握手: 0 长写不产生明文消费, 握手若在本圈内完成也不吞掉对端横幅
     * 首字节(读路径探测的缺陷, 已弃用)。E2E 实测根因(2026-10-02):
     * 握手仅在显式 start/同步 waitFor/读写路径推进, poll 泵若只等
     * isEncrypted 则双端互等永不握手。 */
    if (!d->m_sessionReady) {
#if defined(XNETWORK_SSL_ON) && XNETWORK_SSL_ON
        if (d->m_devIsSsl) {
            if (!XSslSocket_isEncrypted((XSslSocket*)d->m_dev)) {
                (void)XIODevice_write_1(d->m_dev, "", 0);
                return true;
            }
        } else
#endif
        if (!XIODevice_isOpen(d->m_dev)) return true;
        xgc_sessionDeviceReady(self);
        if (!d->m_dev) return true;
    }

    xgc_flushOutbox(self);
    xgc_ensureBannerSent(self);
    if (xgc_benchTraceOn() && d->m_state == XGUI_REMOTE_STATE_STREAMING)
        fprintf(stderr, "XGC_TRACE pump t=%llu avail=%lld\n",
                (unsigned long long)(uint64_t)xgc_nowMs(),
                (long long)(d->m_dev ? XIODevice_bytesAvailable_base(d->m_dev)
                                     : 0));

    now = xgc_nowMs();
    /* 保活(§7.3): 档位间隔发 PING(横幅/握手完成起); 超时未收对端任何帧
     * 判死链转重连——BANNER_WAIT 一并纳管, 防对端横幅不至永久悬挂。 */
    if (d->m_state != XGUI_REMOTE_STATE_DISCONNECTED) {
        if (d->m_state == XGUI_REMOTE_STATE_STREAMING ||
            d->m_state == XGUI_REMOTE_STATE_HANDSHAKING ||
            d->m_state == XGUI_REMOTE_STATE_AUTHENTICATING) {
            if (!d->m_pingAwait &&
                (uint64_t)(now - (int64_t)d->m_lastPingSentMs) >=
                    d->m_profile.pingIntervalMs) {
                uint64_t ts = (uint64_t)now;
                uint8_t pbuf[8]; /* 冻结契约: enc* 显式小端序列化后上网。 */
                size_t plen = XGuiRemoteProto_encPing(pbuf, sizeof(pbuf), ts);
                if (plen > 0 &&
                    xgc_sendFrame(self, XGUI_REMOTE_MSG_PING, pbuf, plen)) {
                    d->m_pingSentTs = ts;
                    d->m_pingAwait = true;
                    d->m_lastPingSentMs = (uint64_t)now;
                }
            }
        }
        if ((uint64_t)(now - (int64_t)d->m_lastPeerFrameMs) >
                d->m_profile.pingTimeoutMs) {
            xgc_teardown(self, XGUI_REMOTE_BYE_TIMEOUT,
                         XGUI_REMOTE_ERR_TIMEOUT, false, false);
            return true;
        }
    }

    /* resource 档移动合并窗口到期: 发出合并中的最新移动(永不合并的
     * PRESS/RELEASE/双击/滚轮不经此处, §7.4)。 */
    if (d->m_hasPendingMove &&
        (uint64_t)(now - (int64_t)d->m_lastMoveSentMs) >=
            d->m_profile.mouseMoveThrottleMs) {
        XGuiRemoteMsgInputPointer move = d->m_pendingMove;
        uint8_t mbuf[24]; /* 冻结契约: enc* 显式序列化后上网(修复: 直发)。 */
        size_t mlen;
        d->m_hasPendingMove = false;
        move.timestampMs = (uint32_t)now;
        mlen = XGuiRemoteProto_encInputPointer(mbuf, sizeof(mbuf), &move);
        if (mlen > 0 &&
            xgc_sendFrame(self, XGUI_REMOTE_MSG_INPUT_POINTER, mbuf, mlen)) {
            d->m_lastMoveSentMs = (uint64_t)now;
        }
    }

    xgc_pumpRead(self);
    return true;
}

/* ==================== 网络连接路径(TCP/TLS, §4.1/§4.3) ==================== */

/** @brief socket 信号槽: connected → 明文档会话就绪(TLS 档交由 poll 泵
 *         在加密完成后就绪)。 */
static void xgc_onSocketConnected(XObject* receiver, XVarList* args)
{
    XGuiClient* self = (XGuiClient*)receiver;
    XGuiClientPrivate* d;
    (void)args;
    if (!self || !self->m_d) return;
    d = (XGuiClientPrivate*)self->m_d;
    if (!d->m_dev || d->m_sessionReady) return;
#if defined(XNETWORK_SSL_ON) && XNETWORK_SSL_ON
    if (d->m_devIsSsl) return; /* 加密完成后由 poll 泵就绪。 */
#endif
    xgc_sessionDeviceReady(self);
}

/** @brief socket 信号槽: disconnected → 传输丢失(断链可重连)。 */
static void xgc_onSocketDisconnected(XObject* receiver, XVarList* args)
{
    XGuiClient* self = (XGuiClient*)receiver;
    XGuiClientPrivate* d;
    (void)args;
    if (!self || !self->m_d) return;
    d = (XGuiClientPrivate*)self->m_d;
    if (!d->m_dev) return; /* 本端已拆除(abort 路径), 幂等。 */
    /* 传输层断开: 无 BYE 可发(对端已不可达)。 */
    xgc_teardown(self, XGUI_REMOTE_BYE_NORMAL, XGUI_REMOTE_ERR_NONE,
                 false, false);
}

/** @brief socket 信号槽: errorOccurred → 记录错误并断链(可重连)。
 *  @note  与 colorSchemeChangedSlot 同款口径: 不解析 args, 发信号侧先落值
 *         后发射, 单线程模型下读回设备当前错误值即本次错误。 */
static void xgc_onSocketError(XObject* receiver, XVarList* args)
{
    XGuiClient* self = (XGuiClient*)receiver;
    XGuiClientPrivate* d;
    (void)args;
    if (!self || !self->m_d) return;
    d = (XGuiClientPrivate*)self->m_d;
    if (!d->m_dev) return;
    /* 传输级错误统一归化 INTERNAL(XGuiRemoteError 无传输错误专用枚举),
     * 随断链一并广播; errorOccurred 之后通常伴随 disconnected。 */
    xgc_teardown(self, XGUI_REMOTE_BYE_NORMAL, XGUI_REMOTE_ERR_INTERNAL,
                 false, false);
}

/** @brief 发起网络连接(明文 TCP 或 TLS; connectToHost 重连共用)。 */
static void xgc_connectNetwork(XGuiClient* self)
{
    XGuiClientPrivate* d;
    if (!self || !self->m_d) return;
    d = (XGuiClientPrivate*)self->m_d;
    if (d->m_host[0] == '\0' || d->m_port == 0) return;

    /* 重复调用先静默断开旧连接(冻结头注)。 */
    xgc_teardown(self, XGUI_REMOTE_BYE_NORMAL, XGUI_REMOTE_ERR_NONE,
                 false, true);

#if XGUI_REMOTE_TLS_ON
#if defined(XNETWORK_SSL_ON) && XNETWORK_SSL_ON
    if (d->m_tlsWanted) {
        XSslSocket* ssl = XSslSocket_create_ex(XCLASS_DEFAULT_MEMORY_TYPE);
        XString* host;
        if (!ssl) {
            xgc_emitError(self, (int)XGUI_REMOTE_ERR_INTERNAL);
            return;
        }
        /* 自签证书部署口(与 XGuiServer 的环境策略口 xgs_tlsPolicyFromEnv
         * 同族): 环境变量 XGUI_REMOTE_TLS_INSECURE=1 时客户端只加密不校验
         * 对端证书链(QueryPeer——仍请求证书, 不强校验)。缺省不设, 行为
         * 与原口径逐字节一致(VerifyPeer 强校验)。E2E 联调实测根因: 默认
         * VERIFY_REQUIRED 下自签服务端证书握手必败且契约无配置口
         * (XSslSocket_ignoreSslErrors 为 MVP 记录位, 从不参与握手判定),
         * 2026-10-02。 */
        {
            const char* insecure = XSystem_environment("XGUI_REMOTE_TLS_INSECURE");
            if (insecure != NULL && insecure[0] == '1' && insecure[1] == '\0')
                XSslSocket_setPeerVerifyMode(ssl, XSSL_QueryPeer);
        }
        host = XString_create_utf8(d->m_host);
        if (!host) {
            XObject_deleteLater((XObject*)ssl);
            xgc_emitError(self, (int)XGUI_REMOTE_ERR_INTERNAL);
            return;
        }
        if (d->m_peerName[0] != '\0') {
            XString* peer = XString_create_utf8(d->m_peerName);
            if (peer) {
                XSslSocket_connectToHostEncrypted_3(
                    ssl, host, d->m_port, peer, XIODevice_ReadWrite,
                    XAbstractSocket_AnyIPProtocol);
                XClass_delete_base((XClass*)peer);
            } else {
                XSslSocket_connectToHostEncrypted_2(
                    ssl, host, d->m_port, XIODevice_ReadWrite,
                    XAbstractSocket_AnyIPProtocol);
            }
        } else {
            XSslSocket_connectToHostEncrypted_2(
                ssl, host, d->m_port, XIODevice_ReadWrite,
                XAbstractSocket_AnyIPProtocol);
        }
        XClass_delete_base((XClass*)host);
        /* 异步握手启动(§4.3): 本仓 XSslSocket 后端的握手只在显式
         * startClientEncryption/同步 waitFor* 路径推进(connectToHost-
         * Encrypted 的信号异步路径不自动开跑)——缺此调用客户端永不发
         * ClientHello(TCP 连上后 tx=0 双端互等)。E2E 实测根因,
         * 2026-10-02。 */
        XSslSocket_startClientEncryption(ssl);
        d->m_dev = (XIODevice*)ssl;
        d->m_devOwned = true;
        d->m_devIsSsl = true;
    } else
#endif /* XNETWORK_SSL_ON */
#endif /* XGUI_REMOTE_TLS_ON */
    {
        XTcpSocket* tcp = XTcpSocket_create_ex(XCLASS_DEFAULT_MEMORY_TYPE);
        if (!tcp) {
            xgc_emitError(self, (int)XGUI_REMOTE_ERR_INTERNAL);
            return;
        }
        d->m_dev = (XIODevice*)tcp;
        d->m_devOwned = true;
        d->m_devIsSsl = false;
        XAbstractSocket_connectToHost_base((XAbstractSocket*)tcp, d->m_host,
                                           d->m_port, XIODevice_ReadWrite,
                                           XAbstractSocket_AnyIPProtocol);
    }

    /* 连接状态经信号驱动(冻结头: connected/disconnected/errorOccurred
     * XAbstractSocket.h:524/532/550)。 */
    XObject_connect_1((XObject*)d->m_dev,
                      (size_t)XAbstractSocket_connected_signal,
                      (XObject*)self, xgc_onSocketConnected,
                      XConnectionType_Direct);
    XObject_connect_1((XObject*)d->m_dev,
                      (size_t)XAbstractSocket_disconnected_signal,
                      (XObject*)self, xgc_onSocketDisconnected,
                      XConnectionType_Direct);
    XObject_connect_1((XObject*)d->m_dev,
                      (size_t)XAbstractSocket_errorOccurred_signal,
                      (XObject*)self, xgc_onSocketError,
                      XConnectionType_Direct);

    /* poll 泵在连接发起时即注册(TCP/TLS 一致): TLS 档握手由读路径驱动
     * (xgc_pollPump 加密前每圈 1 字节读泵之), 而 sessionDeviceReady 的
     * 注册点要到加密完成才可达——先注册后握手, 否则 TLS 双端互等
     * (E2E 实测根因, 2026-10-02)。sessionDeviceReady 内同名注册块保留
     * 为兜底(重复注册有 m_pollHandle 守卫)。 */
    if (!d->m_pollHandle) {
        d->m_pollHandle = XAbstractEventDispatcher_addPollCallback(
            xgc_pollPump, self);
    }
}

/* ==================== 虚槽: 画面绘制 ==================== */

/** @brief 占位画面: 按会话状态画背景与状态文本(不画陈旧帧, §7.3)。 */
static void xgc_paintPlaceholder(XGuiClient* self, XPainter* painter)
{
    XGuiClientPrivate* d;
    XRect rc;
    const char* text;
    uint32_t window;
    uint32_t windowText;
    if (!self || !self->m_d) return;
    d = (XGuiClientPrivate*)self->m_d;
    XRect_init(&rc, 0, 0, XWidget_width((XWidget*)self),
               XWidget_height((XWidget*)self));
    window = xgc_paletteColor(self, XPaletteColorRole_Window);
    windowText = xgc_paletteColor(self, XPaletteColorRole_WindowText);
    XPainter_fillRect(painter, &rc, window);

    switch (d->m_state) {
    case XGUI_REMOTE_STATE_BANNER_WAIT:
    case XGUI_REMOTE_STATE_HANDSHAKING:
        text = "正在连接远端服务器…";
        break;
    case XGUI_REMOTE_STATE_AUTHENTICATING:
        text = "正在认证…";
        break;
    case XGUI_REMOTE_STATE_STREAMING:
        text = "等待远端画面…";
        break;
    case XGUI_REMOTE_STATE_DISCONNECTED:
    default:
        text = (d->m_autoReconnect && d->m_wantConnect)
            ? "连接中断, 正在重连…" : "未连接远端服务器";
        break;
    }
    XPainter_drawTextRect(painter, &rc,
                          XPAINTER_TEXT_ALIGN_HCENTER |
                          XPAINTER_TEXT_ALIGN_VCENTER,
                          text, windowText);
}

static void VX_xgc_paintEvent(XWidget* self, XEvent* event)
{
    XGuiClient* c = (XGuiClient*)self;
    XPainter painter;
    XImage* target;
    XPoint offset;
    XRect rc;
    if (!c || !c->m_d || !event) return;
    target = XWidget_paintImage(self);
    if (!target) return;
    XPainter_init(&painter, NULL);
    if (!XPainter_begin_image(&painter, target)) {
        XPainter_deinit(&painter);
        return;
    }
    offset = XWidget_paintOffset(self);
    if (offset.x != 0 || offset.y != 0) {
        XPainter_translate(&painter, (float)offset.x, (float)offset.y);
    }
    if (xgc_hasPicture(c)) {
        /* 整幅绘制(§7.2): backbuffer 与控件 1:1 同尺寸。 */
        XRect_init(&rc, 0, 0, XWidget_width(self), XWidget_height(self));
        XPainter_fillRect(&painter, &rc, 0xFF000000u);
        XPainter_drawImage(&painter, &c->m_d->m_fb, 0, 0);
    } else {
        xgc_paintPlaceholder(c, &painter);
    }
    XPainter_end(&painter);
    XPainter_deinit(&painter);
}

/* ==================== 虚槽: 鼠标/滚轮转发(需求 9) ==================== */

/** @brief 按下/双击共用: 抓取 + 置按下集合 + 发 PRESS/DBL_CLICK。 */
static void xgc_forwardPointerDown(XGuiClient* self, XMouseEvent* me,
                                   uint8_t action)
{
    XGuiClientPrivate* d;
    XGuiRemoteMsgInputPointer msg;
    if (!self || !self->m_d || !me) return;
    d = (XGuiClientPrivate*)self->m_d;
    if (!xgc_inputChannelReady(self, d->m_fwdPointer)) return;

    /* 按下期间抓取(§7.4): 拖拽越界事件仍派发到本控件至释放(Qt 同语义)。 */
    XWidget_grabMouse((XWidget*)self);

    XMemset(&msg, 0, sizeof(msg));
    msg.action = action;
    msg.button = xgc_buttonBit(me->m_button);
    msg.buttons = (uint16_t)me->m_buttons;
    d->m_pressedButtons |= (uint16_t)msg.button; /* 成对纪律: 置按下集合。 */
    msg.modifiers = (uint32_t)me->m_modifiers;
    /* 抓取期越界坐标不裁剪: i16 ±32767 饱和编码, 远端负责裁剪(§3.2)。 */
    msg.x = xgc_satI16(me->m_position.x);
    msg.y = xgc_satI16(me->m_position.y);
    msg.timestampMs = me->m_timestamp;
    /* 冻结契约: enc* 显式序列化后上网(修复: 结构体直发)。 */
    {
        uint8_t pbuf[24];
        size_t plen = XGuiRemoteProto_encInputPointer(pbuf, sizeof(pbuf), &msg);
        if (plen > 0)
            xgc_sendFrame(self, XGUI_REMOTE_MSG_INPUT_POINTER, pbuf, plen);
    }
}

static void VX_xgc_mousePressEvent(XWidget* self, XEvent* event)
{
    XGuiClient* c = (XGuiClient*)self;
    if (!c || !event || XEvent_type(event) != XEVENT_TYPE_MOUSE_BUTTON_PRESS) {
        return;
    }
    /* 点击夺焦(需求 9「首次点击由框架标准逻辑自然夺焦」的落地; 2026-10-02
     * 修复: 处理器此前不登记焦点——demo 设置页模式下控件被摆进 Tab 链外
     * (隐藏镜像视图), 键盘/IME 转发通道因 hasFocus 门控恒死。对标
     * XAbstractButton/XTabBar 的 XFocusReason_Mouse 点击夺焦惯例, 与
     * XLineEdit 点击聚焦同语义; 键盘转发侧另有 hasFocus 双保险门控
     * (VX_xgc_keyPressEvent), 无焦点时按键本就不达此处, 行为无回归面。 */
    XWidget_setFocusReason(self, XFocusReason_Mouse);
    xgc_forwardPointerDown(c, (XMouseEvent*)event, XGUI_REMOTE_PTR_PRESS);
    XEvent_accept(event);
}

static void VX_xgc_mouseDoubleClickEvent(XWidget* self, XEvent* event)
{
    XGuiClient* c = (XGuiClient*)self;
    if (!c || !event ||
        XEvent_type(event) != XEVENT_TYPE_MOUSE_BUTTON_DBL_CLICK) {
        return;
    }
    /* 双击显式发 action=3(§6.6: 不依赖服务端平台阈值识别); 键集合语义
     * 同 PRESS(该键物理按下中, 后随 RELEASE 配对)。 */
    xgc_forwardPointerDown(c, (XMouseEvent*)event, XGUI_REMOTE_PTR_DBL_CLICK);
    XEvent_accept(event);
}

static void VX_xgc_mouseReleaseEvent(XWidget* self, XEvent* event)
{
    XGuiClient* c = (XGuiClient*)self;
    XGuiClientPrivate* d;
    XGuiRemoteMsgInputPointer msg;
    XMouseEvent* me;
    if (!c || !c->m_d || !event ||
        XEvent_type(event) != XEVENT_TYPE_MOUSE_BUTTON_RELEASE) {
        return;
    }
    d = (XGuiClientPrivate*)c->m_d;
    me = (XMouseEvent*)event;
    if (XWidget_mouseGrabber() == self) {
        XWidget_releaseMouse(self); /* 释放抓取(§7.4)。 */
    }
    if (xgc_inputChannelReady(c, d->m_fwdPointer) && me) {
        XMemset(&msg, 0, sizeof(msg));
        msg.action = XGUI_REMOTE_PTR_RELEASE;
        msg.button = xgc_buttonBit(me->m_button);
        msg.buttons = (uint16_t)me->m_buttons;
        d->m_pressedButtons &= (uint16_t)(~msg.button); /* 成对纪律: 清集合。 */
        msg.modifiers = (uint32_t)me->m_modifiers;
        msg.x = xgc_satI16(me->m_position.x);
        msg.y = xgc_satI16(me->m_position.y);
        msg.timestampMs = me->m_timestamp;
        /* 冻结契约: enc* 显式序列化后上网(修复: 结构体直发)。 */
        {
            uint8_t pbuf[24];
            size_t plen = XGuiRemoteProto_encInputPointer(pbuf, sizeof(pbuf),
                                                          &msg);
            if (plen > 0)
                xgc_sendFrame(c, XGUI_REMOTE_MSG_INPUT_POINTER, pbuf, plen);
        }
    }
    XEvent_accept(event);
}

static void VX_xgc_mouseMoveEvent(XWidget* self, XEvent* event)
{
    XGuiClient* c = (XGuiClient*)self;
    XGuiClientPrivate* d;
    XMouseEvent* me;
    if (!c || !c->m_d || !event ||
        XEvent_type(event) != XEVENT_TYPE_MOUSE_MOVE) {
        return;
    }
    d = (XGuiClientPrivate*)c->m_d;
    me = (XMouseEvent*)event;
    if (!xgc_inputChannelReady(c, d->m_fwdPointer) || !me) return;
    {
        XGuiRemoteMsgInputPointer msg;
        int64_t now = xgc_nowMs();
        XMemset(&msg, 0, sizeof(msg));
        msg.action = XGUI_REMOTE_PTR_MOVE;
        msg.button = xgc_buttonBit(me->m_button);
        msg.buttons = (uint16_t)me->m_buttons;
        msg.modifiers = (uint32_t)me->m_modifiers;
        /* 抓取期越界坐标: 同样 i16 饱和编码不裁剪(§3.2/§7.4)。 */
        msg.x = xgc_satI16(me->m_position.x);
        msg.y = xgc_satI16(me->m_position.y);
        msg.timestampMs = me->m_timestamp;

        if (d->m_profile.mouseMoveThrottleMs > 0) {
            /* resource 档合并窗口: 窗口内仅保存最新移动, 到期由 poll 泵发出
             * (按下/释放/双击/滚轮永不合并, §7.4)。 */
            d->m_pendingMove = msg;
            d->m_hasPendingMove = true;
            if ((uint64_t)(now - (int64_t)d->m_lastMoveSentMs) >=
                d->m_profile.mouseMoveThrottleMs) {
                XGuiRemoteMsgInputPointer out = d->m_pendingMove;
                uint8_t obuf[24]; /* 冻结契约: enc* 序列化后上网(修复)。 */
                size_t olen;
                d->m_hasPendingMove = false;
                out.timestampMs = (uint32_t)now;
                olen = XGuiRemoteProto_encInputPointer(obuf, sizeof(obuf),
                                                       &out);
                if (olen > 0 &&
                    xgc_sendFrame(c, XGUI_REMOTE_MSG_INPUT_POINTER,
                                  obuf, olen)) {
                    d->m_lastMoveSentMs = (uint64_t)now;
                }
            }
        } else {
            /* performance 档(0ms): 直传。 */
            uint8_t mbuf[24]; /* 冻结契约: enc* 序列化后上网(修复: 直发)。 */
            size_t mlen = XGuiRemoteProto_encInputPointer(mbuf, sizeof(mbuf),
                                                          &msg);
            if (mlen > 0 &&
                xgc_sendFrame(c, XGUI_REMOTE_MSG_INPUT_POINTER, mbuf, mlen)) {
                d->m_lastMoveSentMs = (uint64_t)now;
            }
        }
    }
    XEvent_accept(event);
}

static void VX_xgc_wheelEvent(XWidget* self, XEvent* event)
{
    XGuiClient* c = (XGuiClient*)self;
    XGuiClientPrivate* d;
    XWheelEvent* we;
    XGuiRemoteMsgInputWheel msg;
    if (!c || !c->m_d || !event || XEvent_type(event) != XEVENT_TYPE_WHEEL) {
        return;
    }
    d = (XGuiClientPrivate*)c->m_d;
    we = (XWheelEvent*)event;
    if (!xgc_inputChannelReady(c, d->m_fwdPointer) || !we) return;
    /* 滚轮永不合并(§7.4); angleDelta ±120/格 透传(§6.6)。 */
    XMemset(&msg, 0, sizeof(msg));
    msg.angleX = xgc_satI16(we->m_angleDelta.x);
    msg.angleY = xgc_satI16(we->m_angleDelta.y);
    msg.pixelX = xgc_satI16(we->m_pixelDelta.x);
    msg.pixelY = xgc_satI16(we->m_pixelDelta.y);
    msg.buttons = (uint16_t)we->m_buttons;
    msg.modifiers = (uint32_t)we->m_modifiers;
    msg.x = xgc_satI16(we->m_position.x);
    msg.y = xgc_satI16(we->m_position.y);
    msg.timestampMs = (uint32_t)xgc_nowMs();
    /* 冻结契约: enc* 显式序列化后上网(修复: 结构体直发 24B ≠ 线上 22B)。 */
    {
        uint8_t wbuf[32];
        size_t wlen = XGuiRemoteProto_encInputWheel(wbuf, sizeof(wbuf), &msg);
        if (wlen > 0)
            xgc_sendFrame(c, XGUI_REMOTE_MSG_INPUT_WHEEL, wbuf, wlen);
    }
    XEvent_accept(event);
}

/* ==================== 虚槽: 键盘/IME 转发(需求 9) ==================== */

/** @brief 触摸转发(2026-10-02 加法式: XGuiClient_setForwardTouch 开启后
 *         生效; 主点必须放 points[0], 对齐服务端注入契约 §6.6)。 */
static void VX_xgc_touchEvent(XWidget* self, XEvent* event)
{
    XGuiClient* c = (XGuiClient*)self;
    XGuiClientPrivate* d;
    XTouchEvent* te;
    XGuiRemoteMsgInputTouch msg;
    const XTouchPoint* pts;
    int count;
    int i;
    XEventType type = XEvent_type(event);
    if (!c || !c->m_d || event ||
        (type != XEVENT_TYPE_TOUCH_BEGIN && type != XEVENT_TYPE_TOUCH_UPDATE &&
         type != XEVENT_TYPE_TOUCH_END && type != XEVENT_TYPE_TOUCH_CANCEL)) {
        return;
    }
    d = (XGuiClientPrivate*)c->m_d;
    te = (XTouchEvent*)event;
    /* 触摸属指针通道语义(§7.4 指针=指点类输入), 门控沿用 m_fwdPointer。 */
    if (!xgc_inputChannelReady(c, d->m_fwdPointer)) return;
    count = XTouchEvent_pointCount(te);
    if (count <= 0) return;
    if (count > XGUI_REMOTE_MAX_TOUCH_POINTS) count = XGUI_REMOTE_MAX_TOUCH_POINTS;
    pts = XTouchEvent_points(te);
    XMemset(&msg, 0, sizeof(msg));
    switch (type) {
    case XEVENT_TYPE_TOUCH_BEGIN:  msg.action = XGUI_REMOTE_TOUCH_BEGIN;  break;
    case XEVENT_TYPE_TOUCH_UPDATE: msg.action = XGUI_REMOTE_TOUCH_UPDATE; break;
    case XEVENT_TYPE_TOUCH_END:    msg.action = XGUI_REMOTE_TOUCH_END;    break;
    default:                       msg.action = XGUI_REMOTE_TOUCH_CANCEL; break;
    }
    msg.pointCount = (uint8_t)count;
    msg.timestampMs = (uint32_t)xgc_nowMs();
    for (i = 0; i < count; ++i) {
        XGuiRemoteMsgTouchPoint* dst = &msg.points[i];
        const XTouchPoint* src = pts ? &pts[i] : NULL;
        dst->id = src ? (uint32_t)src->m_id : 0u;
        /* 触点四态数值两端逐字对齐(QEventPoint::State), 原值透传。 */
        dst->state = src ? (uint8_t)src->m_state
                         : (uint8_t)XGUI_REMOTE_TP_PRESSED;
        dst->x = xgc_satI16(src ? src->m_position.x : 0);
        dst->y = xgc_satI16(src ? src->m_position.y : 0);
        /* 主点缺失(未分配列表)时以主点字段兜底。 */
        if (!src && i == 0) {
            XPoint p = XTouchEvent_position(te);
            dst->x = xgc_satI16(p.x);
            dst->y = xgc_satI16(p.y);
        }
        dst->pressureQ8 = src ? (uint8_t)(src->m_pressure * 255.0f + 0.5f)
                              : 255u;
    }
    {
        uint8_t tbuf[6 + XGUI_REMOTE_MAX_TOUCH_POINTS * 11];
        size_t tlen = XGuiRemoteProto_encInputTouch(tbuf, sizeof(tbuf), &msg);
        if (tlen > 0)
            xgc_sendFrame(c, XGUI_REMOTE_MSG_INPUT_TOUCH, tbuf, tlen);
    }
    XEvent_accept(event);
}

static void VX_xgc_keyPressEvent(XWidget* self, XEvent* event)
{
    XGuiClient* c = (XGuiClient*)self;
    XGuiClientPrivate* d;
    XKeyEvent* ke;
    XGuiRemoteMsgInputKey msg;
    if (!c || !c->m_d || !event || XEvent_type(event) != XEVENT_TYPE_KEY_PRESS) {
        return;
    }
    d = (XGuiClientPrivate*)c->m_d;
    ke = (XKeyEvent*)event;
    /* 焦点门控(§7.4): 仅本控件持有焦点时转发(控件无焦点时框架本就不投递
     * 按键, 此为双保险)。 */
    if (!XWidget_hasFocus(self) || !d->m_fwdKeyboard || !ke) return;
    XMemset(&msg, 0, sizeof(msg));
    msg.action = XGUI_REMOTE_KEY_PRESS;
    msg.key = (uint32_t)ke->m_key;
    msg.modifiers = (uint32_t)ke->m_modifiers;
    msg.nativeScanCode = ke->m_nativeScanCode;
    msg.timestampMs = ke->m_timestamp;
    if (!ke->m_autoRepeat) {
        /* 成对纪律: 首次按下置集合(自动重复的重复 PRESS 不重复置)。 */
        xgc_pressedKeysAdd(d, msg.key);
    }
    /* autoRepeat 原样转发重复 PRESS(服务端 handleKeyEvent_ex 有该参数)。 */
    {
        uint8_t kbuf[24]; /* 冻结契约: enc* 显式序列化后上网(修复: 结构体
                           * 直发 20B ≠ 线上 17B, 服务端严格 dec 必拒)。 */
        size_t klen = XGuiRemoteProto_encInputKey(kbuf, sizeof(kbuf), &msg);
        if (klen > 0)
            xgc_sendFrame(c, XGUI_REMOTE_MSG_INPUT_KEY, kbuf, klen);
    }
    XEvent_accept(event);
}

static void VX_xgc_keyReleaseEvent(XWidget* self, XEvent* event)
{
    XGuiClient* c = (XGuiClient*)self;
    XGuiClientPrivate* d;
    XKeyEvent* ke;
    XGuiRemoteMsgInputKey msg;
    if (!c || !c->m_d || !event ||
        XEvent_type(event) != XEVENT_TYPE_KEY_RELEASE) {
        return;
    }
    d = (XGuiClientPrivate*)c->m_d;
    ke = (XKeyEvent*)event;
    if (!XWidget_hasFocus(self) || !d->m_fwdKeyboard || !ke) return;
    XMemset(&msg, 0, sizeof(msg));
    msg.action = XGUI_REMOTE_KEY_RELEASE;
    msg.key = (uint32_t)ke->m_key;
    msg.modifiers = (uint32_t)ke->m_modifiers;
    msg.nativeScanCode = ke->m_nativeScanCode;
    msg.timestampMs = ke->m_timestamp;
    xgc_pressedKeysRemove(d, msg.key); /* 成对纪律: 清集合。 */
    /* 冻结契约: enc* 显式序列化后上网(修复: 结构体直发)。 */
    {
        uint8_t kbuf[24];
        size_t klen = XGuiRemoteProto_encInputKey(kbuf, sizeof(kbuf), &msg);
        if (klen > 0)
            xgc_sendFrame(c, XGUI_REMOTE_MSG_INPUT_KEY, kbuf, klen);
    }
    XEvent_accept(event);
}

static void VX_xgc_inputMethodEvent(XWidget* self, XEvent* event)
{
    XGuiClient* c = (XGuiClient*)self;
    XGuiClientPrivate* d;
    XInputMethodEvent* ime;
    XGuiRemoteMsgInputIme msg;
    const char* utf8;
    size_t n;
    if (!c || !c->m_d || !event ||
        XEvent_type(event) != XEVENT_TYPE_INPUT_METHOD) {
        return;
    }
    d = (XGuiClientPrivate*)c->m_d;
    ime = (XInputMethodEvent*)event;
    if (!XWidget_hasFocus(self) || !d->m_fwdIme || !ime) return;
    /* 原文透明转发(§7.4): 本地不做任何输入法逻辑; 超长按协议帽截断。 */
    XMemset(&msg, 0, sizeof(msg));
    if (ime->m_preeditString) {
        utf8 = XString_toUtf8(ime->m_preeditString);
        n = utf8 ? strlen(utf8) : 0;
        if (n > XGUI_REMOTE_MAX_TEXT_BYTES) n = XGUI_REMOTE_MAX_TEXT_BYTES;
        msg.preeditBytes = (uint16_t)n;
        if (n > 0) memcpy(msg.preedit, utf8, n);
    }
    if (ime->m_commitString) {
        utf8 = XString_toUtf8(ime->m_commitString);
        n = utf8 ? strlen(utf8) : 0;
        if (n > XGUI_REMOTE_MAX_TEXT_BYTES) n = XGUI_REMOTE_MAX_TEXT_BYTES;
        msg.commitBytes = (uint16_t)n;
        if (n > 0) memcpy(msg.commit, utf8, n);
    }
    msg.replacementStart = ime->m_replacementStart;
    msg.replacementLength = ime->m_replacementLength;
    msg.cursorPosition = ime->m_cursorPosition;
    msg.anchorPosition = ime->m_anchorPosition;
    /* 冻结契约: enc* 显式序列化后上网(修复: 结构体直发)。 */
    {
        uint8_t ibuf[2 * XGUI_REMOTE_MAX_TEXT_BYTES + 32];
        size_t ilen = XGuiRemoteProto_encInputIme(ibuf, sizeof(ibuf), &msg);
        if (ilen > 0)
            xgc_sendFrame(c, XGUI_REMOTE_MSG_INPUT_IME, ibuf, ilen);
    }
    XEvent_accept(event);
}

/* ==================== 生命周期 ==================== */

static void VX_xgc_deinit(XGuiClient* self)
{
    XGuiClientPrivate* d;
    if (!self || !self->m_d) {
        XClass_Deinit_Parent(XWidget, (XWidget*)self);
        return;
    }
    d = (XGuiClientPrivate*)self->m_d;

    xgc_teardown(self, XGUI_REMOTE_BYE_NORMAL, XGUI_REMOTE_ERR_NONE,
                 true, true);
    if (d->m_reconnectTimer) {
        XTimer_stop_base(d->m_reconnectTimer);
        XObject_deleteLater((XObject*)d->m_reconnectTimer);
        d->m_reconnectTimer = NULL;
    }
    XGuiRemoteFrameReader_deinit(&d->m_reader);
    XRegion_deinit(&d->m_damage);
    if (d->m_pressedKeys) {
        XFree_System(d->m_pressedKeys);
        d->m_pressedKeys = NULL;
    }
    if (d->m_outbox) {
        XFree_System(d->m_outbox);
        d->m_outbox = NULL;
    }
    XMemset(d->m_password, 0, sizeof(d->m_password)); /* 口令即焚。 */
    XImage_deinit_base(&d->m_fb);
    XFree_System(d);
    self->m_d = NULL;
    XClass_Deinit_Parent(XWidget, (XWidget*)self);
}

XVtable* XGuiClient_class_init(void)
{
    XVTABLE_INIT_DEFAULT(XGuiClient)
    XVTABLE_INHERIT_XCLASS(XWidget);
    XVTABLE_OVERLOAD_DEFAULT(EXWidget_PaintEvent, VX_xgc_paintEvent);
    XVTABLE_OVERLOAD_DEFAULT(EXWidget_MousePressEvent, VX_xgc_mousePressEvent);
    XVTABLE_OVERLOAD_DEFAULT(EXWidget_MouseReleaseEvent,
                             VX_xgc_mouseReleaseEvent);
    XVTABLE_OVERLOAD_DEFAULT(EXWidget_MouseDoubleClickEvent,
                             VX_xgc_mouseDoubleClickEvent);
    XVTABLE_OVERLOAD_DEFAULT(EXWidget_MouseMoveEvent, VX_xgc_mouseMoveEvent);
    XVTABLE_OVERLOAD_DEFAULT(EXWidget_WheelEvent, VX_xgc_wheelEvent);
    XVTABLE_OVERLOAD_DEFAULT(EXWidget_TouchEvent, VX_xgc_touchEvent);
    XVTABLE_OVERLOAD_DEFAULT(EXWidget_KeyPressEvent, VX_xgc_keyPressEvent);
    XVTABLE_OVERLOAD_DEFAULT(EXWidget_KeyReleaseEvent, VX_xgc_keyReleaseEvent);
    XVTABLE_OVERLOAD_DEFAULT(EXWidget_InputMethodEvent,
                             VX_xgc_inputMethodEvent);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Deinit, VX_xgc_deinit);
    return XVTABLE_DEFAULT;
}

void XGuiClient_init(XGuiClient* self, XWidget* parent, XWidgetFlags flags)
{
    XGuiClientPrivate* d;
    if (!self) return;
    XMemset(self, 0, sizeof(*self));
    XWidget_init(&self->m_base, parent, flags);
    XClassSetVtable(self, XGuiClient);
    Set_Class_Memory(self, XCLASS_DEFAULT_MEMORY_TYPE);
    Set_Class_IsHeap(self, false);

    d = (XGuiClientPrivate*)XCalloc_System(1, sizeof(XGuiClientPrivate));
    self->m_d = d;
    if (!d) return;

    XImage_init(&d->m_fb);            /* 空 backbuffer, FB_META 重建。 */
    XRegion_init(&d->m_damage);
    XGuiRemoteFrameReader_init(&d->m_reader);
    XGuiRemoteProfile_initPerformance(&d->m_profile);
    XGuiRemoteProfile_sanitize(&d->m_profile);
    d->m_state = XGUI_REMOTE_STATE_DISCONNECTED;
    d->m_fwdPointer = true;
    d->m_fwdKeyboard = true;
    d->m_fwdIme = true;
    d->m_fwdTouch = false; /* 默认关(2026-10-02): 单点触摸已由鼠标路径+仿真覆盖。 */
    d->m_reconnectBaseMs = XGC_RECONNECT_DEFAULT_MS;

    /* 能力位(§3.5): 按编译期真值宣告。 */
    d->m_localCaps = XGUI_REMOTE_CAP_RGB565 | XGUI_REMOTE_CAP_IME |
                     XGUI_REMOTE_CAP_PROFILE_SET;
#if XGUI_REMOTE_ZLIB_ON
    if (XGuiRemoteCodec_hasCodec(XGUI_REMOTE_CODEC_ZLIB)) {
        d->m_localCaps |= XGUI_REMOTE_CAP_ZLIB;
    }
#endif
#if XGUI_REMOTE_TLS_ON
#if defined(XNETWORK_SSL_ON) && XNETWORK_SSL_ON
    d->m_localCaps |= XGUI_REMOTE_CAP_TLS;
#endif
#endif

    /* 需求 9: StrongFocus(首次点击由框架标准逻辑自然夺焦)+ 启用本地
     * IME 提示(WA_InputMethodEnabled)。 */
    XWidget_setFocusPolicy(&self->m_base, XWidgetFocusPolicy_StrongFocus);
    XWidget_setAttribute(&self->m_base,
                         XWidgetAttribute_InputMethodEnabled, true);
    XWidget_setInputMethodHints(&self->m_base, XInputMethodHint_None);
    /* 鼠标移动直捕转发需 tracking(无按键也收 Move)。 */
    XWidget_setMouseTracking(&self->m_base, true);

    /* 重连定时器(单次触发; 指数退避由 xgc_scheduleReconnect 驱动)。 */
    d->m_reconnectTimer = XTimer_create_ex(XCLASS_DEFAULT_MEMORY_TYPE);
    if (d->m_reconnectTimer) {
        XTimer_setSingleShot(d->m_reconnectTimer, true);
        XTimer_setUserData(d->m_reconnectTimer, self);
        XTimer_setTimerCallback(d->m_reconnectTimer, xgc_reconnectFire);
    }
}

XGuiClient* XGuiClient_create_ex(XMemoryType memory, XWidget* parent,
                                 XWidgetFlags flags)
{
    XGuiClient* self = (XGuiClient*)XMemory_malloc(sizeof(*self), memory);
    if (!self) return NULL;
    XGuiClient_init(self, parent, flags);
    Set_Class_Memory(self, memory);
    Set_Class_IsHeap(self, true);
    return self;
}

/* ==================== 连接(需求 2: 传输无关) ==================== */

void XGuiClient_connectToHost(XGuiClient* self, const char* hostUtf8,
                              uint16_t port)
{
    XGuiClientPrivate* d;
    size_t n;
    if (!self || !self->m_d || !hostUtf8 || hostUtf8[0] == '\0') return;
    d = (XGuiClientPrivate*)self->m_d;
    n = strlen(hostUtf8);
    if (n >= XGC_HOST_CAP) n = XGC_HOST_CAP - 1;
    memcpy(d->m_host, hostUtf8, n);
    d->m_host[n] = '\0';
    d->m_port = port;
    d->m_peerName[0] = '\0';
    d->m_tlsWanted = false;
    d->m_wantConnect = true;
    d->m_reconnectAttempts = 0;
    xgc_connectNetwork(self);
}

#if XGUI_REMOTE_TLS_ON
void XGuiClient_connectToHostEncrypted(XGuiClient* self, const char* hostUtf8,
                                       uint16_t port,
                                       const char* peerNameUtf8)
{
    XGuiClientPrivate* d;
    size_t n;
    if (!self || !self->m_d || !hostUtf8 || hostUtf8[0] == '\0') return;
    d = (XGuiClientPrivate*)self->m_d;
    n = strlen(hostUtf8);
    if (n >= XGC_HOST_CAP) n = XGC_HOST_CAP - 1;
    memcpy(d->m_host, hostUtf8, n);
    d->m_host[n] = '\0';
    if (peerNameUtf8 && peerNameUtf8[0] != '\0') {
        n = strlen(peerNameUtf8);
        if (n >= XGC_HOST_CAP) n = XGC_HOST_CAP - 1;
        memcpy(d->m_peerName, peerNameUtf8, n);
        d->m_peerName[n] = '\0';
    } else {
        d->m_peerName[0] = '\0';
    }
    d->m_port = port;
    d->m_tlsWanted = true;
    d->m_wantConnect = true;
    d->m_reconnectAttempts = 0;
    xgc_connectNetwork(self);
}
#endif /* XGUI_REMOTE_TLS_ON */

void XGuiClient_setTransport(XGuiClient* self, XIODevice* device)
{
    XGuiClientPrivate* d;
    if (!self || !self->m_d) return;
    d = (XGuiClientPrivate*)self->m_d;

    /* 旧会话静默拆除(借用设备不销毁)。 */
    xgc_teardown(self, XGUI_REMOTE_BYE_NORMAL, XGUI_REMOTE_ERR_NONE,
                 false, true);
    d->m_host[0] = '\0';
    d->m_port = 0;
    d->m_tlsWanted = false;
    d->m_wantConnect = false; /* 自定义传输无自动重连语义。 */

    if (!device) return;
    d->m_dev = device;
    d->m_devOwned = false;
    d->m_devIsSsl = false;
    if (XIODevice_isOpen(device)) {
        /* 传入后状态直接进入握手阶段(冻结头注): 横幅即发。 */
        d->m_state = XGUI_REMOTE_STATE_BANNER_WAIT;
        xgc_sessionDeviceReady(self);
    }
}

void XGuiClient_disconnectFromServer(XGuiClient* self)
{
    XGuiClientPrivate* d;
    if (!self || !self->m_d) return;
    d = (XGuiClientPrivate*)self->m_d;
    d->m_wantConnect = false; /* 用户主动断开: 不自动重连(冻结头注)。 */
    if (d->m_reconnectTimer) XTimer_stop_base(d->m_reconnectTimer);
    xgc_teardown(self, XGUI_REMOTE_BYE_NORMAL, XGUI_REMOTE_ERR_NONE,
                 true, false);
}

void XGuiClient_setAutoReconnect(XGuiClient* self, bool enabled,
                                 uint32_t intervalMs)
{
    XGuiClientPrivate* d;
    if (!self || !self->m_d) return;
    d = (XGuiClientPrivate*)self->m_d;
    d->m_autoReconnect = enabled;
    d->m_reconnectBaseMs = intervalMs > 0 ? intervalMs
                                          : XGC_RECONNECT_DEFAULT_MS;
    if (!enabled && d->m_reconnectTimer) {
        XTimer_stop_base(d->m_reconnectTimer);
    }
}

XGuiRemoteSessionState XGuiClient_state(const XGuiClient* self)
{
    if (!self || !self->m_d) return XGUI_REMOTE_STATE_DISCONNECTED;
    return ((const XGuiClientPrivate*)self->m_d)->m_state;
}

/* ==================== 认证与档位 ==================== */

void XGuiClient_setPassword(XGuiClient* self, const char* passwordUtf8)
{
    XGuiClientPrivate* d;
    size_t n;
    if (!self || !self->m_d) return;
    d = (XGuiClientPrivate*)self->m_d;
    XMemset(d->m_password, 0, sizeof(d->m_password));
    if (!passwordUtf8) return;
    n = strlen(passwordUtf8);
    if (n >= sizeof(d->m_password)) n = sizeof(d->m_password) - 1;
    memcpy(d->m_password, passwordUtf8, n);
}

void XGuiClient_requestProfileId(XGuiClient* self, XGuiRemoteProfileId id)
{
    XGuiClientPrivate* d;
    XGuiRemoteMsgProfileSet ps;
    if (!self || !self->m_d) return;
    d = (XGuiClientPrivate*)self->m_d;
    if (!d->m_dev || !d->m_sessionReady) return; /* 无会话: 无操作。 */
    if (id == XGUI_REMOTE_PROFILE_CUSTOM) {
        /* 自定义档需参数块, 冻结契约只暴露预设 id 请求; V1 拒绝并报错。 */
        xgc_emitError(self, (int)XGUI_REMOTE_ERR_PROTOCOL);
        return;
    }
    XMemset(&ps, 0, sizeof(ps));
    ps.profileId = (uint8_t)id;
    ps.hasCustomProfile = 0;
    /* 冻结契约: enc* 显式序列化后上网(修复: 结构体直发含对齐填充,
     * 服务端严格 dec 必拒)。 */
    {
        uint8_t pbuf[48];
        size_t plen = XGuiRemoteProto_encProfileSet(pbuf, sizeof(pbuf), &ps);
        if (plen > 0)
            xgc_sendFrame(self, XGUI_REMOTE_MSG_PROFILE_SET, pbuf, plen);
    }
}

void XGuiClient_setInputForwardingEnabled(XGuiClient* self, bool pointer,
                                          bool keyboard, bool ime)
{
    XGuiClientPrivate* d;
    if (!self || !self->m_d) return;
    d = (XGuiClientPrivate*)self->m_d;
    d->m_fwdPointer = pointer;
    d->m_fwdKeyboard = keyboard;
    d->m_fwdIme = ime;
}

/* ==================== 查询 ==================== */

void XGuiClient_remoteSize(const XGuiClient* self, XSize* out)
{
    const XGuiClientPrivate* d;
    if (!out) return;
    XSize_init(out, 0, 0);
    if (!self || !self->m_d) return;
    d = (const XGuiClientPrivate*)self->m_d;
    XSize_init(out, (int)d->m_fbWidth, (int)d->m_fbHeight);
}

int XGuiClient_remoteTitle(const XGuiClient* self, char* buffer, int cap)
{
    const XGuiClientPrivate* d;
    int n;
    int copy;
    if (!self || !self->m_d) return 0;
    d = (const XGuiClientPrivate*)self->m_d;
    n = d->m_titleBytes;
    if (!buffer || cap <= 0) return n;
    copy = n < cap - 1 ? n : cap - 1;
    if (copy > 0) memcpy(buffer, d->m_title, (size_t)copy);
    buffer[copy] = '\0';
    return n;
}

bool XGuiClient_isRemoteAlive(const XGuiClient* self)
{
    if (!self || !self->m_d) return false;
    return ((const XGuiClientPrivate*)self->m_d)->m_state ==
           XGUI_REMOTE_STATE_STREAMING;
}

void XGuiClient_statistics(const XGuiClient* self, XGuiRemoteStats* out)
{
    const XGuiClientPrivate* d;
    if (!out) return;
    XMemset(out, 0, sizeof(*out));
    if (!self || !self->m_d) return; /* self 可为 NULL(冻结头注)。 */
    d = (const XGuiClientPrivate*)self->m_d;
    *out = d->m_stats;
}

void XGuiClient_statisticsExtended(const XGuiClient* self,
                                   XGuiRemoteStatsExtended* out)
{
    const XGuiClientPrivate* d;
    if (!out) return;
    XMemset(out, 0, sizeof(*out));
    if (!self || !self->m_d) return; /* self 可为 NULL(与 statistics 同口径)。 */
    d = (const XGuiClientPrivate*)self->m_d;
    out->bytesSent = d->m_stats.bytesSent;
    out->bytesReceived = d->m_stats.bytesReceived;
    out->updateCount = d->m_stats.updateCount;
    out->tileCount = d->m_stats.tileCount;
    out->fpsMilli = d->m_stats.fpsMilli;
    out->rttMs = d->m_stats.rttMs;
    out->reconnectCount = d->m_reconnectCountTotal;
}

void XGuiClient_setForwardTouch(XGuiClient* self, bool enabled)
{
    XGuiClientPrivate* d;
    if (!self || !self->m_d) return;
    d = (XGuiClientPrivate*)self->m_d;
    d->m_fwdTouch = enabled;
    /* 开启时补 WA_AcceptTouchEvents(默认关时框架本不向本控件投 TOUCH_*,
     * 不打扰既有鼠标路径; 关闭仅断转发, 属性保持无害)。 */
    XWidget_setAttribute(&self->m_base, XWidgetAttribute_AcceptTouchEvents,
                         enabled);
}

void XGuiClient_sendTouchFrame(XGuiClient* self,
                               const XGuiRemoteMsgInputTouch* msg)
{
    XGuiClientPrivate* d;
    uint8_t tbuf[6 + XGUI_REMOTE_MAX_TOUCH_POINTS * 11];
    size_t tlen;
    if (!self || !self->m_d || !msg) return;
    d = (XGuiClientPrivate*)self->m_d;
    if (msg->pointCount == 0) return;      /* 空帧: 无操作(防服务端拒)。 */
    if (!d->m_dev || !d->m_sessionReady) return; /* 无会话: 无操作。 */
    tlen = XGuiRemoteProto_encInputTouch(tbuf, sizeof(tbuf), msg);
    if (tlen > 0)
        xgc_sendFrame(self, XGUI_REMOTE_MSG_INPUT_TOUCH, tbuf, tlen);
}

/* ==================== 信号(GUI 线程发射) ==================== */

void* XGuiClient_connected_signal(XGuiClient* self)
{
    (void)self;
    return (void*)(size_t)XGuiClient_connected_signal;
}

void* XGuiClient_disconnected_signal(XGuiClient* self, int reason)
{
    (void)self;
    (void)reason;
    return (void*)(size_t)XGuiClient_disconnected_signal;
}

void* XGuiClient_remoteMetaChanged_signal(XGuiClient* self)
{
    (void)self;
    return (void*)(size_t)XGuiClient_remoteMetaChanged_signal;
}

void* XGuiClient_errorOccurred_signal(XGuiClient* self, int errorCode)
{
    (void)self;
    (void)errorCode;
    return (void*)(size_t)XGuiClient_errorOccurred_signal;
}

#endif /* XGUI_REMOTE_ON */
