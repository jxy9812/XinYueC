/******************************************************************************
 * @file       XGuiRemoteUdpChannel.c
 * @brief      XGuiRemote UDP 低延迟旁路通道实现(设计稿 §4 数据面 + §7 事件驱动)。
 * @details    契约见 XGuiRemoteUdpChannel.h。实现要点(全部实读核实依据):
 *               - 收侧事件驱动: XUdpSocket bind(XDeviceNetwork_socketBind 的
 *                 UDP 分支, XDeviceNetwork_posix.c:762)即在 XNetIoRing 提交
 *                 RECV(io_uring/epoll 双引擎), 就绪推 CQ → XEventSockAct(Read)
 *                 → XAbstractSocket 读环 + readyRead 信号——本文件零轮询代码,
 *                 drain 由 readyRead 直连槽与 tick 兜底双路触发;
 *               - 发送: 非阻塞 sendto 直用自有 fd(MSG_DONTWAIT; EAGAIN/ENOBUFS
 *                 按丢帧语义处理——FRAME 丢旧兼容, INPUT 由 NACK 补回);
 *                 平台缺陷绕开见设计稿 §1(XDeviceNetwork 的 UDP SEND 无目的
 *                 地址, 未连接套接字必 ENOTCONN);
 *               - FRAME(S→C): seq 自增无确认, 接收端丢旧(seq≤已见即弃);
 *               - INPUT(C→S): seq 自增 + 64 帧重传缓存; 服务端 expect 语义
 *                 (重排槽 64/NACK(缺失首序号)/20ms 重发/2s 跳进兜底);
 *               - CTRL: 1s 心跳 PING(兼 NAT 保活)/PONG(携带服务端帧活性
 *                 u32 msAgo, 供客户端判"服务端在发而本端没收到"的丢帧恢复);
 *               - 线程约定: 全部 API 仅 GUI 线程(与 XGuiServer/XGuiClient
 *                 泵同线程); 服务端编码线程只经会话锁读 udpSplitCap,
 *                 不触本通道;
 *               - slots[64] 双角色复用: 服务端=INPUT 重排槽, 客户端=INPUT
 *                 重传缓存(角色创建时定版, 互斥使用)。
 * @author     XinYueC 团队
 ******************************************************************************/
#include "XGuiRemoteUdpChannel.h"

#if XGUI_REMOTE_ON

#include "XUdpSocket.h"
#include "XAbstractSocket.h"
#include "XHostAddress.h"
#include "XAbstractNetIoRing.h"
#include "XCoreApplication.h" /* removePostedEvents: 套接字销毁前摘在途事件(UAF 根修)。 */
#include "XMemory.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#if defined(__linux__)
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <errno.h>
#include <time.h>
#include <sys/ioctl.h>
#include <linux/sockios.h>
#ifndef SIOCGSTAMPNS
#define SIOCGSTAMPNS 0x8907
#endif
#define XUDP_HAVE_SENDTO 1
#define XUDP_HAVE_SOCKSTAMP 1
#else
#define XUDP_HAVE_SENDTO 0
#define XUDP_HAVE_SOCKSTAMP 0
#endif

/* ==================== 私有常量 ==================== */

#define XUDP_HEARTBEAT_MS        1000  /**< CTRL-PING 周期(兼 NAT 保活)。 */
#define XUDP_REORDER_WINDOW      64    /**< 重排窗口/重传缓存深度(槽=seq%64)。 */
#define XUDP_RENACK_MS           20    /**< 缺口 NACK 重发周期。 */
#define XUDP_GAP_GIVEUP_MS       2000  /**< 缺口跳进兜底(NACK 无回应时限)。 */
#define XUDP_PEER_CAP            64    /**< 对端地址文本缓冲。 */

/** @brief 槽节点(服务端=重排待派发; 客户端=重传缓存), 惰性分配。 */
typedef struct XudpNode {
    uint32_t seq;                   /**< 节点序号(0xFFFFFFFF=无效槽)。 */
    uint8_t  msgType;               /**< INPUT 消息类型。 */
    size_t   len;                   /**< 负载字节数。 */
    size_t   cap;                   /**< 负载缓冲容量(复用免频繁分配)。 */
    uint8_t* data;                  /**< 负载副本(不含 msgType 字节)。 */
} XudpNode;

/** @brief 通道对象。 */
struct XGuiRemoteUdpChannel {
    XGuiRemoteUdpChannelConfig cfg;      /**< 角色/回调(创建时定版)。 */

    XUdpSocket* sock;                    /**< UDP 套接字(拥有)。 */
    bool     active;                     /**< 数据面激活(帧+输入走 UDP)。 */
    bool     sockReady;                  /**< 套接字就绪(bind/prepare 成功)。 */
    int      fd;                         /**< 原生 fd(sendto 直用; <0 不可用)。 */

    /* ---- 会话绑定 ---- */
    bool     bound;                      /**< 已绑定会话。 */
    uint32_t token;                      /**< 会话凭据(逐数据报校验)。 */

    /* ---- 序号空间 ---- */
    uint32_t txSeqFrame;                 /**< FRAME 发送序号(服务端)。 */
    uint32_t txSeqInput;                 /**< INPUT 发送序号(客户端)。 */
    uint32_t rxFramesSeen;               /**< FRAME 已见最大序号(客户端丢旧)。 */
    uint32_t rxExpectInput;              /**< INPUT 期望序号(服务端)。 */
    int64_t  gapSinceMs;                 /**< INPUT 缺口开启时刻(0=无缺口)。 */

    /* ---- 发送目的(服务端=会话对端; 客户端=服务器) ---- */
    char     peerAddr[XUDP_PEER_CAP];    /**< 点分 IPv4 文本。 */
    uint16_t peerPort;
    bool     hasDest;

    /* ---- 节拍 ---- */
    int64_t  lastRecvMs;                 /**< 最近合法数据报(INT64_MAX=未收)。 */
    int64_t  lastPingSentMs;             /**< 最近 CTRL-PING。 */
    int64_t  lastNackSentMs;             /**< 最近 NACK(缺口重发节拍)。 */
    int64_t  lastFrameSendMs;            /**< 最近 FRAME 发出(服务端, PONG 携带)。 */
    int64_t  lastFrameRecvMs;            /**< 最近 FRAME 收下(客户端)。 */
    uint32_t pongFrameMsAgo;             /**< 最近 PONG 携带的服务端帧时差。 */

    /* ---- 收侧解析(字节流重同步缓冲) ---- */
    uint8_t  rxBuf[XGUI_REMOTE_UDP_HEADER_BYTES + XGUI_REMOTE_UDP_MAX_PAYLOAD];
    size_t   rxLen;
    uint64_t kernStampUs; /**< 最近一次 read 批次内核收包戳(MONO µs; 0=无效,
                               仅 XGUI_REMOTE_WAKE_PROF 探针写)。 */

    /* ---- 服务端重排槽 / 客户端重传缓存(seq%64 索引, 惰性分配) ---- */
    XudpNode* slots[XUDP_REORDER_WINDOW];

    /* ---- 统计 ---- */
    uint8_t  rxLogMask;                  /**< 逐通道首帧日志掩码(追踪用)。 */
    uint64_t txDatagrams;
    uint64_t rxDatagrams;
    uint64_t droppedInputs;
    uint64_t nackSent;
    uint64_t nackRecv;
    uint64_t retransSent;
};

/* ==================== 内部工具 ==================== */

/** @brief 状态迁移(发射)。 */
static void xudp_setState(XGuiRemoteUdpChannel* c, int state)
{
    if (!c || !c->cfg.onStateChanged) return;
    c->cfg.onStateChanged(c->cfg.user, state);
}

#if XUDP_HAVE_SENDTO
/** @brief 构造 sockaddr_in(点分 IPv4 + 端口); 失败 false。 */
static bool xudp_buildDest(const char* addrUtf8, uint16_t port,
                           struct sockaddr_in* out)
{
    if (!addrUtf8 || !addrUtf8[0] || !out) return false;
    memset(out, 0, sizeof(*out));
    out->sin_family = AF_INET;
    out->sin_port = htons(port);
    if (inet_pton(AF_INET, addrUtf8, &out->sin_addr) != 1) return false;
    return true;
}

/** @brief 非阻塞 sendto(自有 fd; EAGAIN/ENOBUFS 按递交成功=可能丢处理)。 */
static bool xudp_sendSys(XGuiRemoteUdpChannel* c, const uint8_t* dg, size_t len)
{
    struct sockaddr_in dest;
    ssize_t n;
    if (!c || c->fd < 0 || !c->hasDest) return false;
    if (!xudp_buildDest(c->peerAddr, c->peerPort, &dest)) return false;
    do {
        n = sendto(c->fd, dg, len, MSG_DONTWAIT,
                   (const struct sockaddr*)&dest, sizeof(dest));
    } while (n < 0 && errno == EINTR);
    if (n < 0) {
        /* EAGAIN/ENOBUFS=发送队列暂满: 丢帧语义(FRAME 丢旧/INPUT 走 NACK),
         * 其余错误(如 ENETUNREACH)同样不致命——通道活性由心跳静默判定。 */
        return (errno == EAGAIN || errno == EWOULDBLOCK || errno == ENOBUFS);
    }
    c->txDatagrams++;
    return true;
}
#else
static bool xudp_sendSys(XGuiRemoteUdpChannel* c, const uint8_t* dg, size_t len)
{
    (void)c; (void)dg; (void)len;
    return false;
}
#endif /* XUDP_HAVE_SENDTO */

/** @brief 组 16B 头 + 负载并发送。 */
static bool xudp_sendDatagram(XGuiRemoteUdpChannel* c, uint8_t channel,
                              uint32_t seq, const uint8_t* payload, size_t len)
{
    uint8_t dg[XGUI_REMOTE_UDP_HEADER_BYTES + XGUI_REMOTE_UDP_MAX_PAYLOAD];
    if (!c || !c->sockReady || !c->bound) return false;
    if (len > XGUI_REMOTE_UDP_MAX_PAYLOAD) return false;
    dg[0] = 'X'; dg[1] = 'G'; dg[2] = 'U'; dg[3] = '1';
    dg[4] = 1;                          /* 版本。 */
    dg[5] = channel;
    dg[6] = (uint8_t)(len & 0xFFu);
    dg[7] = (uint8_t)((uint32_t)len >> 8);
    XGuiRemoteProto_putU32(dg + 8, c->token);
    XGuiRemoteProto_putU32(dg + 12, seq);
    if (len) memcpy(dg + XGUI_REMOTE_UDP_HEADER_BYTES, payload, len);
    return xudp_sendSys(c, dg, XGUI_REMOTE_UDP_HEADER_BYTES + len);
}

/** @brief 发 CTRL 子帧(sub: 1=PING 2=PONG 3=NACK)。 */
static void xudp_sendCtrl(XGuiRemoteUdpChannel* c, uint8_t sub,
                          const uint8_t* payload, size_t len)
{
    uint8_t buf[8];
    if (len > sizeof(buf)) return;
    if (len) memcpy(buf, payload, len);
    (void)xudp_sendDatagram(c, XGUI_REMOTE_UDP_CHANNEL_CTRL, 0, buf, len);
}

/** @brief 发 NACK(缺失首序号 + 数量)。 */
static void xudp_sendNack(XGuiRemoteUdpChannel* c, uint32_t firstSeq,
                          uint16_t count)
{
    uint8_t buf[6];
    XGuiRemoteProto_putU32(buf, firstSeq);
    XGuiRemoteProto_putU16(buf + 4, count);
    xudp_sendCtrl(c, 3, buf, 6);
    c->nackSent++;
}

/** @brief 释放槽节点(数据+节点)。 */
static void xudp_freeNode(XudpNode* node)
{
    if (!node) return;
    if (node->data) XFree_System(node->data);
    XFree_System(node);
}

/** @brief 缺口是否仍开启(存在任何槽节点)。 */
static bool xudp_gapOpen(const XGuiRemoteUdpChannel* c)
{
    int i;
    for (i = 0; i < XUDP_REORDER_WINDOW; ++i)
        if (c->slots[i]) return true;
    return false;
}

/** @brief 丢弃全部槽节点(跳进兜底), 返回丢弃数。 */
static uint32_t xudp_clearSlots(XGuiRemoteUdpChannel* c)
{
    uint32_t dropped = 0;
    int i;
    for (i = 0; i < XUDP_REORDER_WINDOW; ++i) {
        if (c->slots[i]) {
            xudp_freeNode(c->slots[i]);
            c->slots[i] = NULL;
            ++dropped;
        }
    }
    return dropped;
}

/* ==================== INPUT 重排/派发(服务端) ==================== */

/** @brief 派发一条输入(msgType+负载)到业务回调。 */
static void xudp_deliverInput(XGuiRemoteUdpChannel* c, uint8_t msgType,
                              const uint8_t* payload, size_t len, uint32_t seq)
{
    if (c->cfg.onRecv)
        c->cfg.onRecv(c->cfg.user, XGUI_REMOTE_UDP_CHANNEL_INPUT, msgType,
                      seq, payload, len);
}

/** @brief 顺派发从 expect 起的连续槽。 */
static void xudp_drainContiguous(XGuiRemoteUdpChannel* c)
{
    for (;;) {
        uint32_t idx = c->rxExpectInput % XUDP_REORDER_WINDOW;
        XudpNode* node = c->slots[idx];
        if (!node || node->seq != c->rxExpectInput) break;
        c->slots[idx] = NULL;
        c->rxExpectInput++;
        xudp_deliverInput(c, node->msgType, node->data, node->len, node->seq);
        xudp_freeNode(node);
    }
    if (!xudp_gapOpen(c)) c->gapSinceMs = 0;
}

/** @brief 服务端 INPUT 数据报处理(expect 语义, 设计稿 §4.2)。 */
static void xudp_onInputDatagram(XGuiRemoteUdpChannel* c, uint32_t seq,
                                 uint8_t msgType, const uint8_t* payload,
                                 size_t len, int64_t nowMs)
{
    if (seq == c->rxExpectInput) {
        c->rxExpectInput++;
        xudp_deliverInput(c, msgType, payload, len, seq);
        xudp_drainContiguous(c);
        return;
    }
    if ((int32_t)(seq - c->rxExpectInput) < 0) {
        return; /* 重复(NACK 重传与原帧竞速): 丢。 */
    }
    if (seq - c->rxExpectInput >= XUDP_REORDER_WINDOW) {
        /* 远超窗口: 跳进兜底(丢计数, 不死锁, 设计稿 §8.2)。 */
        c->droppedInputs += xudp_clearSlots(c);
        c->droppedInputs += (uint64_t)(seq - c->rxExpectInput);
        c->rxExpectInput = seq + 1;
        c->gapSinceMs = 0;
        xudp_deliverInput(c, msgType, payload, len, seq);
        return;
    }
    /* 窗口内缺口: 入重排槽 + NACK(20ms 重发由 tick 承担)。 */
    {
        uint32_t idx = seq % XUDP_REORDER_WINDOW;
        XudpNode* node = c->slots[idx];
        if (node && node->seq == seq) return; /* 同号重复。 */
        if (node) { /* 槽被过期序号占用(异常): 清。 */
            xudp_freeNode(node);
            c->slots[idx] = NULL;
        }
        node = (XudpNode*)XMalloc_System(sizeof(*node));
        if (node) {
            node->data = (uint8_t*)XMalloc_System(len ? len : 1);
            if (!node->data) {
                XFree_System(node);
            } else {
                node->seq = seq;
                node->msgType = msgType;
                node->len = len;
                node->cap = len;
                memcpy(node->data, payload, len);
                c->slots[idx] = node;
                if (c->gapSinceMs == 0) c->gapSinceMs = nowMs;
                xudp_sendNack(c, c->rxExpectInput,
                              (uint16_t)(seq - c->rxExpectInput));
                c->lastNackSentMs = nowMs;
            }
        }
        /* 分配失败: 本帧丢弃(输入丢失; 画面由全量刷新兜底)。 */
    }
}

/* ==================== [wake] 段⓪ 内核收包戳(诊断, XGUI_REMOTE_WAKE_PROF) ==================== */

/* ring 层 profMarkRecv 的打点在 CQ 回收时刻, 内核/套接字侧排队不可见;
 * 本探针经 SIOCGSTAMPNS 取 skb 内核收包时刻(REALTIME 轴)与派发完成
 * (profNowUs, MONOTONIC 轴)同轴化, 补齐「sendto→用户态派发」全程。
 * 批次语义: 一批 read 粘连多个数据报时戳取末包, 批内首包样本偏小
 * (低偏诊断可接受); 样本并入既有 5s [wake][cli/srv-kern] 汇总行。 */

#if XUDP_HAVE_SOCKSTAMP
static uint64_t xudp_wakeRtOffUs = 0; /**< REALTIME−MONOTONIC(µs), 惰性一次。 */
static int      xudp_wakeRtOffInit = 0;

/** @brief read 批次后取内核收包戳(SIOCGSTAMPNS; 失败/未开探针=0)。 */
static void xudp_wakeKernStamp(XGuiRemoteUdpChannel* c)
{
    struct timespec ts;
    if (!c) return;
    c->kernStampUs = 0;
    if (!XGuiRemoteUdp_wakeProfOn() || c->fd < 0) return;
    if (ioctl(c->fd, SIOCGSTAMPNS, &ts) != 0) return;
    if (!xudp_wakeRtOffInit) {
        struct timespec rt;
        struct timespec mo;
        if (clock_gettime(CLOCK_REALTIME, &rt) != 0 ||
            clock_gettime(CLOCK_MONOTONIC, &mo) != 0) {
            xudp_wakeRtOffInit = 1; /* 失败定版 0 偏移(样本仍 REALTIME 轴, 弃用)。 */
            return;
        }
        xudp_wakeRtOffUs =
            (uint64_t)rt.tv_sec * 1000000ULL + (uint64_t)rt.tv_nsec / 1000ULL -
            ((uint64_t)mo.tv_sec * 1000000ULL + (uint64_t)mo.tv_nsec / 1000ULL);
        xudp_wakeRtOffInit = 1;
    }
    c->kernStampUs =
        (uint64_t)ts.tv_sec * 1000000ULL + (uint64_t)ts.tv_nsec / 1000ULL -
        xudp_wakeRtOffUs;
}
#else
static void xudp_wakeKernStamp(XGuiRemoteUdpChannel* c)
{
    if (c) c->kernStampUs = 0;
}
#endif /* XUDP_HAVE_SOCKSTAMP */

/** @brief 本批解析派发完成后采样「内核收包→派发完成」(探针关闭/无戳=跳过)。 */
static void xudp_wakeKernSample(XGuiRemoteUdpChannel* c)
{
    const char* tag;
    uint64_t kernUs;
    if (!c || !c->kernStampUs || !XGuiRemoteUdp_wakeProfOn()) return;
    kernUs = c->kernStampUs;
    c->kernStampUs = 0;
    tag = (c->cfg.role == XGUI_REMOTE_UDP_ROLE_SERVER) ? "srv-kern"
                                                       : "cli-kern";
    XGuiRemoteUdp_wakeProfSample(tag, kernUs);
}

/* ==================== 收侧解析 ==================== */

/* 联调追踪(XGUI_REMOTE_UDP_TRACE=1 启用; 首帧/丢弃单行, 与
 * XGUI_REMOTE_BENCH_TRACE 同纪律, 未启用零成本)。 */
static int xudp_traceOn = -1;
#define XUDP_TRACE_ON() \
    (xudp_traceOn < 0 ? (xudp_traceOn = \
        (getenv("XGUI_REMOTE_UDP_TRACE") != NULL)) : xudp_traceOn)

/**
 * @brief  从字节流缓冲解析数据报(支持粘连; 坏头逐字节重同步)。
 * @return true 缓冲内已无可整帧数据(余量留在缓冲头)。
 */
static void xudp_parseStream(XGuiRemoteUdpChannel* c, int64_t nowMs)
{
    for (;;) {
        size_t avail = c->rxLen;
        uint16_t payloadLen;
        uint8_t channel;
        uint32_t token;
        uint32_t seq;
        const uint8_t* p;
        size_t consumed;
        if (avail < XGUI_REMOTE_UDP_HEADER_BYTES) return;
        p = c->rxBuf;
        /* 魔数/版本/长度域重同步: 逐字节滑动。 */
        if (!(p[0] == 'X' && p[1] == 'G' && p[2] == 'U' && p[3] == '1') ||
            p[4] != 1) {
            memmove(c->rxBuf, c->rxBuf + 1, avail - 1);
            c->rxLen = avail - 1;
            continue;
        }
        channel = p[5];
        payloadLen = (uint16_t)(p[6] | ((uint16_t)p[7] << 8));
        if (payloadLen > XGUI_REMOTE_UDP_MAX_PAYLOAD) {
            memmove(c->rxBuf, c->rxBuf + 1, avail - 1);
            c->rxLen = avail - 1;
            continue;
        }
        if (avail < (size_t)XGUI_REMOTE_UDP_HEADER_BYTES + payloadLen)
            return; /* 半截: 留待续收。 */
        token = XGuiRemoteProto_getU32(p + 8);
        seq = XGuiRemoteProto_getU32(p + 12);
        consumed = (size_t)XGUI_REMOTE_UDP_HEADER_BYTES + payloadLen;
        if (!c->bound || token != c->token) {
            /* 非本会话数据报: 丢弃且不刷新 lastRecv(静默判定不受干扰)。 */
            if (XUDP_TRACE_ON())
                fprintf(stderr, "[XUDP-TRACE] drop unbound/token "
                        "bound=%d exp=%08x got=%08x ch=%u\n",
                        c->bound ? 1 : 0, c->token, token, channel);
            size_t rest = c->rxLen - consumed;
            if (rest) memmove(c->rxBuf, c->rxBuf + consumed, rest);
            c->rxLen = rest;
            continue;
        }
        c->lastRecvMs = nowMs;
        c->rxDatagrams++;
        if (XUDP_TRACE_ON() && !(c->rxLogMask & (1u << channel))) {
            c->rxLogMask |= (uint8_t)(1u << channel);
            fprintf(stderr, "[XUDP-TRACE] first datagram ch=%u seq=%u "
                    "len=%u role=%s total=%llu\n", channel, seq, payloadLen,
                    c->cfg.role == XGUI_REMOTE_UDP_ROLE_SERVER ? "srv"
                                                               : "cli",
                    (unsigned long long)c->rxDatagrams);
        }
        {
            const uint8_t* payload = p + XGUI_REMOTE_UDP_HEADER_BYTES;
            if (channel == XGUI_REMOTE_UDP_CHANNEL_FRAME) {
                if (c->cfg.role == XGUI_REMOTE_UDP_ROLE_CLIENT) {
                    /* 最新帧优先: 重复/乱序旧帧直接丢(设计稿 §4.1)。 */
                    if ((int32_t)(seq - c->rxFramesSeen) > 0) {
                        c->rxFramesSeen = seq;
                        c->lastFrameRecvMs = nowMs;
                        if (c->cfg.onRecv)
                            c->cfg.onRecv(c->cfg.user, channel,
                                          (uint8_t)XGUI_REMOTE_MSG_FB_UPDATE,
                                          seq, payload, payloadLen);
                    }
                }
            } else if (channel == XGUI_REMOTE_UDP_CHANNEL_INPUT) {
                if (c->cfg.role == XGUI_REMOTE_UDP_ROLE_SERVER &&
                    payloadLen >= 1) {
                    xudp_onInputDatagram(c, seq, payload[0], payload + 1,
                                         (size_t)payloadLen - 1, nowMs);
                }
            } else if (channel == XGUI_REMOTE_UDP_CHANNEL_CTRL &&
                       payloadLen >= 1) {
                uint8_t sub = payload[0];
                if (sub == 1) {
                    /* PING: 服务端对已绑定会话回 PONG(携带服务端帧活性)。 */
                    if (c->cfg.role == XGUI_REMOTE_UDP_ROLE_SERVER) {
                        uint32_t msAgo = XGuiRemoteUdpChannel_msSinceFrameSend(
                                c, nowMs);
                        uint8_t pong[4];
                        XGuiRemoteProto_putU32(pong, msAgo);
                        xudp_sendCtrl(c, 2, pong, 4);
                    }
                } else if (sub == 2 && payloadLen >= 5) {
                    /* PONG: 客户端记录服务端帧时差(丢帧恢复判据)。 */
                    if (c->cfg.role == XGUI_REMOTE_UDP_ROLE_CLIENT)
                        c->pongFrameMsAgo = XGuiRemoteProto_getU32(payload + 1);
                } else if (sub == 3 && payloadLen >= 6) {
                    /* NACK: 客户端按缓存重传。 */
                    if (c->cfg.role == XGUI_REMOTE_UDP_ROLE_CLIENT) {
                        c->nackRecv++;
                        XGuiRemoteUdpChannel_nackRetransmit(
                                c, XGuiRemoteProto_getU32(payload + 1),
                                XGuiRemoteProto_getU16(payload + 5));
                    }
                }
            }
            /* 未知通道: 丢弃(前向兼容)。 */
        }
        {
            size_t rest = c->rxLen - consumed;
            if (rest) memmove(c->rxBuf, c->rxBuf + consumed, rest);
            c->rxLen = rest;
        }
    }
}

/* ==================== 契约实现 ==================== */

XGuiRemoteUdpChannel* XGuiRemoteUdpChannel_create_ex(
        const XGuiRemoteUdpChannelConfig* config)
{
    XGuiRemoteUdpChannel* c;
    if (!config || (config->role != XGUI_REMOTE_UDP_ROLE_SERVER &&
                    config->role != XGUI_REMOTE_UDP_ROLE_CLIENT)) {
        return NULL;
    }
    c = (XGuiRemoteUdpChannel*)XCalloc_System(1, sizeof(*c));
    if (!c) return NULL;
    c->cfg = *config;
    c->fd = -1;
    c->lastRecvMs = INT64_MAX;
    c->lastFrameRecvMs = INT64_MAX;
    c->lastFrameSendMs = INT64_MAX;
    c->pongFrameMsAgo = 0xFFFFFFFFu;
    return c;
}

void XGuiRemoteUdpChannel_delete(XGuiRemoteUdpChannel* c)
{
    int i;
    if (!c) return;
    XGuiRemoteUdpChannel_closeSocket(c);
    for (i = 0; i < XUDP_REORDER_WINDOW; ++i)
        xudp_freeNode(c->slots[i]);
    XFree_System(c);
}

bool XGuiRemoteUdpChannel_serverBind(XGuiRemoteUdpChannel* c,
                                     uint16_t preferredPort,
                                     uint16_t* boundPortOut)
{
    uint16_t port;
    if (!c || c->cfg.role != XGUI_REMOTE_UDP_ROLE_SERVER) return false;
    XGuiRemoteUdpChannel_closeSocket(c);
    c->sock = XUdpSocket_create_ex(XCLASS_DEFAULT_MEMORY_TYPE);
    if (!c->sock) return false;
    for (port = preferredPort; (uint16_t)(port - preferredPort) < 8; ++port) {
        if (XAbstractSocket_bindAny((XAbstractSocket*)c->sock, port,
                                    XAbstractSocket_ReuseAddressHint)) {
            break;
        }
    }
    if ((uint16_t)(port - preferredPort) >= 8) {
        XGuiRemoteUdpChannel_closeSocket(c);
        return false; /* 全占用: 静默纯 TCP(能力位不宣告)。 */
    }
    c->sockReady = true;
    c->fd = (int)XAbstractSocket_socketDescriptor_base(
            (XAbstractSocket*)c->sock);
    if (boundPortOut)
        *boundPortOut = XAbstractSocket_localPort((XAbstractSocket*)c->sock);
    return true;
}

bool XGuiRemoteUdpChannel_clientPrepare(XGuiRemoteUdpChannel* c,
                                        const char* hostUtf8, uint16_t port)
{
    size_t n;
    if (!c || c->cfg.role != XGUI_REMOTE_UDP_ROLE_CLIENT) return false;
    if (!hostUtf8 || !hostUtf8[0] || port == 0) return false;
    XGuiRemoteUdpChannel_closeSocket(c);
    c->sock = XUdpSocket_create_ex(XCLASS_DEFAULT_MEMORY_TYPE);
    if (!c->sock) return false;
    if (!XAbstractSocket_bindAny((XAbstractSocket*)c->sock, 0,
                                 XAbstractSocket_DefaultForPlatform)) {
        XGuiRemoteUdpChannel_closeSocket(c);
        return false;
    }
    n = strlen(hostUtf8);
    if (n >= sizeof(c->peerAddr)) n = sizeof(c->peerAddr) - 1;
    memcpy(c->peerAddr, hostUtf8, n);
    c->peerAddr[n] = '\0';
    c->peerPort = port;
    c->hasDest = true;
    c->sockReady = true;
    c->fd = (int)XAbstractSocket_socketDescriptor_base(
            (XAbstractSocket*)c->sock);
    return true;
}

void XGuiRemoteUdpChannel_closeSocket(XGuiRemoteUdpChannel* c)
{
    if (!c) return;
    if (c->sock) {
        /* [2026-10-04 UAF 根修] 环完成的 XEventSockAct 以套接字对象为收件人
         * 异步投递(dispatchCQEntry → postEvent); 重建路径(closeSocket+新建,
         * 客户端每次 OFFER/重绑走此)先删对象时, 队列中在途事件悬垂——投递
         * 即写已释放内存(偶发堆破坏: 客户端 TCP 帧头偶发垃圾 → 协议错误
         * 重连风暴, 实证 ~2.8MB 周期)。删除前摘除本对象在途 SOCK_ACT。 */
        XCoreApplication_removePostedEvents((XObject*)c->sock,
                                            XEVENT_TYPE_SOCK_ACT);
        XAbstractSocket_abort((XAbstractSocket*)c->sock);
        XClassDelete((XClass*)c->sock);
        c->sock = NULL;
    }
    c->sockReady = false;
    c->active = false;
    c->fd = -1;
    c->rxLen = 0;
    c->hasDest = false;
}

XObject* XGuiRemoteUdpChannel_socketObject(const XGuiRemoteUdpChannel* c)
{
    if (!c || !c->sock) return NULL;
    return (XObject*)c->sock;
}

bool XGuiRemoteUdpChannel_socketReady(const XGuiRemoteUdpChannel* c)
{
    return c ? c->sockReady : false;
}

void XGuiRemoteUdpChannel_bindSession(XGuiRemoteUdpChannel* c, uint32_t token,
                                      const char* peerAddrUtf8,
                                      uint16_t peerUdpPort)
{
    if (!c) return;
    c->bound = true;
    c->token = token;
    c->txSeqFrame = 0;
    c->txSeqInput = 0;
    c->rxFramesSeen = 0;
    c->rxExpectInput = 0;
    c->gapSinceMs = 0;
    c->lastRecvMs = INT64_MAX;
    c->lastFrameRecvMs = INT64_MAX;
    c->pongFrameMsAgo = 0xFFFFFFFFu;
    (void)xudp_clearSlots(c);
    if (c->cfg.role == XGUI_REMOTE_UDP_ROLE_SERVER && peerAddrUtf8 &&
        peerAddrUtf8[0] && peerUdpPort != 0) {
        size_t n = strlen(peerAddrUtf8);
        if (n >= sizeof(c->peerAddr)) n = sizeof(c->peerAddr) - 1;
        memcpy(c->peerAddr, peerAddrUtf8, n);
        c->peerAddr[n] = '\0';
        c->peerPort = peerUdpPort;
        c->hasDest = true;
    }
}

void XGuiRemoteUdpChannel_unbindSession(XGuiRemoteUdpChannel* c)
{
    if (!c) return;
    c->bound = false;
    c->active = false;
    if (c->cfg.role == XGUI_REMOTE_UDP_ROLE_SERVER) c->hasDest = false;
    (void)xudp_clearSlots(c);
}

bool XGuiRemoteUdpChannel_sessionBound(const XGuiRemoteUdpChannel* c)
{
    return c ? c->bound : false;
}

void XGuiRemoteUdpChannel_setSessionUser(XGuiRemoteUdpChannel* c, void* user)
{
    if (!c) return;
    c->cfg.user = user;
}

uint32_t XGuiRemoteUdpChannel_sessionToken(const XGuiRemoteUdpChannel* c)
{
    return (c && c->bound) ? c->token : 0;
}

void XGuiRemoteUdpChannel_setActive(XGuiRemoteUdpChannel* c, bool active)
{
    if (!c || c->active == active) return;
    c->active = active;
    if (active) {
        /* 序号空间复位(重建语义): 对端 bindSession 同步复位。 */
        c->txSeqFrame = 0;
        c->txSeqInput = 0;
        c->rxFramesSeen = 0;
        c->rxExpectInput = 0;
        c->gapSinceMs = 0;
        c->lastFrameSendMs = INT64_MAX;
        (void)xudp_clearSlots(c);
        xudp_setState(c, (int)XGUI_REMOTE_UDP_STATE_ACTIVE);
    } else {
        xudp_setState(c, (int)XGUI_REMOTE_UDP_STATE_FALLBACK);
    }
}

int XGuiRemoteUdpChannel_state(const XGuiRemoteUdpChannel* c)
{
    if (!c) return (int)XGUI_REMOTE_UDP_STATE_OFF;
    if (c->active) return (int)XGUI_REMOTE_UDP_STATE_ACTIVE;
    if (c->sockReady && c->bound) return (int)XGUI_REMOTE_UDP_STATE_TRYING;
    return (int)XGUI_REMOTE_UDP_STATE_OFF;
}

bool XGuiRemoteUdpChannel_sendFrame(XGuiRemoteUdpChannel* c,
                                    const uint8_t* payload, size_t len,
                                    int64_t nowMs)
{
    uint32_t seq;
    if (!c || !c->active || !c->bound) return false;
    if (len > XGUI_REMOTE_UDP_MAX_PAYLOAD) return false;
    seq = c->txSeqFrame;
    if (!xudp_sendDatagram(c, XGUI_REMOTE_UDP_CHANNEL_FRAME, seq,
                           payload, len)) {
        return false; /* 未递交(无目的/未绑定): 调用方回落 TCP 本帧。 */
    }
    c->txSeqFrame = seq + 1;
    c->lastFrameSendMs = nowMs;
    return true;
}

bool XGuiRemoteUdpChannel_sendInput(XGuiRemoteUdpChannel* c,
                                    const uint8_t* payload, size_t len)
{
    XudpNode* node;
    uint32_t idx;
    uint32_t seq;
    if (!c || !c->active || !c->bound) return false;
    if (len < 1 || len > XGUI_REMOTE_UDP_MAX_PAYLOAD) return false;
    seq = c->txSeqInput;
    if (!xudp_sendDatagram(c, XGUI_REMOTE_UDP_CHANNEL_INPUT, seq,
                           payload, len)) {
        return false; /* 未递交: 序号不前进, 调用方回落 TCP 本条。 */
    }
    c->txSeqInput = seq + 1;
    if (XUDP_TRACE_ON() && seq == 0)
        fprintf(stderr, "[XUDP-TRACE] first input sent seq=0 len=%zu "
                "dest=%s:%u\n", len, c->peerAddr, c->peerPort);
    /* 入重传缓存(槽 seq%64; 逐出即不补, 服务端跳进兜底)。 */
    idx = seq % XUDP_REORDER_WINDOW;
    node = c->slots[idx];
    if (!node) {
        node = (XudpNode*)XMalloc_System(sizeof(*node));
        if (!node) return true; /* 已发出, 缓存缺位不重传。 */
        memset(node, 0, sizeof(*node));
        c->slots[idx] = node;
    }
    if (node->cap < len) {
        uint8_t* grown = (uint8_t*)XMalloc_System(len);
        if (!grown) {
            node->seq = 0xFFFFFFFFu; /* 标记无效槽(不参与重传)。 */
            node->len = 0;
            return true;
        }
        if (node->data) XFree_System(node->data);
        node->data = grown;
        node->cap = len;
    }
    memcpy(node->data, payload, len);
    node->seq = seq;
    node->msgType = payload[0];
    node->len = len;
    return true;
}

void XGuiRemoteUdpChannel_nackRetransmit(XGuiRemoteUdpChannel* c,
                                         uint32_t firstSeq, uint16_t count)
{
    uint16_t i;
    if (!c || !c->bound) return;
    if (count > XUDP_REORDER_WINDOW) count = XUDP_REORDER_WINDOW;
    for (i = 0; i < count; ++i) {
        uint32_t s = firstSeq + i;
        uint32_t idx = s % XUDP_REORDER_WINDOW;
        XudpNode* node = c->slots[idx];
        if (node && node->seq == s && node->data && node->len) {
            uint8_t dg[XGUI_REMOTE_UDP_HEADER_BYTES + 2100];
            size_t len = node->len;
            if (len > 2100) continue; /* 超 IME 上限的异常缓存: 跳过。 */
            dg[0] = 'X'; dg[1] = 'G'; dg[2] = 'U'; dg[3] = '1';
            dg[4] = 1;
            dg[5] = XGUI_REMOTE_UDP_CHANNEL_INPUT;
            dg[6] = (uint8_t)(len & 0xFFu);
            dg[7] = (uint8_t)((uint32_t)len >> 8);
            XGuiRemoteProto_putU32(dg + 8, c->token);
            XGuiRemoteProto_putU32(dg + 12, s);
            memcpy(dg + XGUI_REMOTE_UDP_HEADER_BYTES, node->data, len);
            if (xudp_sendSys(c, dg, XGUI_REMOTE_UDP_HEADER_BYTES + len))
                c->retransSent++;
        }
    }
}

void XGuiRemoteUdpChannel_drain(XGuiRemoteUdpChannel* c, int64_t nowMs)
{
    if (!c || !c->sockReady) return;
    for (;;) {
        int64_t avail = c->sock ? XIODevice_bytesAvailable_base(
                (XIODevice*)c->sock) : 0;
        size_t space;
        int64_t n;
        if (avail <= 0) break;
        space = sizeof(c->rxBuf) - c->rxLen;
        if (space == 0) {
            /* 缓冲满且无可解析(病态流): 丢最旧头字节重同步。 */
            memmove(c->rxBuf, c->rxBuf + 1, sizeof(c->rxBuf) - 1);
            c->rxLen--;
            continue;
        }
        n = XIODevice_read_1((XIODevice*)c->sock,
                             (char*)(c->rxBuf + c->rxLen), (int64_t)space);
        if (n <= 0) break;
        c->rxLen += (size_t)n;
        xudp_wakeKernStamp(c);
        {
            uint64_t rx0 = c->rxDatagrams;
            xudp_parseStream(c, nowMs);
            if (c->rxDatagrams != rx0)
                xudp_wakeKernSample(c);
            else
                c->kernStampUs = 0; /* 半截批: 戳作废, 续收后另采。 */
        }
    }
}

void XGuiRemoteUdpChannel_tick(XGuiRemoteUdpChannel* c, int64_t nowMs)
{
    if (!c || !c->sockReady) return;
    XGuiRemoteUdpChannel_drain(c, nowMs);
    if (!c->bound) return;
    /* 心跳(兼 NAT 保活; 激活前后都发——客户端激活前发为打洞预热)。 */
    if (c->lastPingSentMs == 0 || nowMs - c->lastPingSentMs >=
            XUDP_HEARTBEAT_MS) {
        xudp_sendCtrl(c, 1, NULL, 0);
        c->lastPingSentMs = nowMs;
    }
    /* 缺口 NACK 重发(20ms 节拍) + 2s 跳进兜底(服务端)。 */
    if (c->cfg.role == XGUI_REMOTE_UDP_ROLE_SERVER && c->gapSinceMs != 0) {
        if (nowMs - c->gapSinceMs >= XUDP_GAP_GIVEUP_MS) {
            uint32_t dropped = xudp_clearSlots(c);
            c->droppedInputs += dropped;
            c->rxExpectInput += dropped; /* 近似推进: 跳过缺失段。 */
            c->gapSinceMs = 0;
        } else if (nowMs - c->lastNackSentMs >= XUDP_RENACK_MS) {
            xudp_sendNack(c, c->rxExpectInput, 1);
            c->lastNackSentMs = nowMs;
        }
    }
}

uint16_t XGuiRemoteUdpChannel_localPort(const XGuiRemoteUdpChannel* c)
{
    if (!c || !c->sock) return 0;
    return XAbstractSocket_localPort((XAbstractSocket*)c->sock);
}

int64_t XGuiRemoteUdpChannel_msSinceRecv(const XGuiRemoteUdpChannel* c,
                                         int64_t nowMs)
{
    if (!c || c->lastRecvMs == INT64_MAX) return INT64_MAX;
    return nowMs - c->lastRecvMs;
}

uint32_t XGuiRemoteUdpChannel_msSinceFrameSend(
        const XGuiRemoteUdpChannel* c, int64_t nowMs)
{
    if (!c || c->lastFrameSendMs == INT64_MAX) return 0xFFFFFFFFu;
    if (nowMs <= c->lastFrameSendMs) return 0;
    return (uint32_t)(nowMs - c->lastFrameSendMs);
}

uint32_t XGuiRemoteUdpChannel_pongFrameMsAgo(
        const XGuiRemoteUdpChannel* c)
{
    return c ? c->pongFrameMsAgo : 0xFFFFFFFFu;
}

uint64_t XGuiRemoteUdpChannel_txDatagrams(const XGuiRemoteUdpChannel* c)
{
    return c ? c->txDatagrams : 0;
}

uint64_t XGuiRemoteUdpChannel_rxDatagrams(const XGuiRemoteUdpChannel* c)
{
    return c ? c->rxDatagrams : 0;
}

uint64_t XGuiRemoteUdpChannel_droppedInputs(const XGuiRemoteUdpChannel* c)
{
    return c ? c->droppedInputs : 0;
}

/* ==================== [wake] 事件循环响应延迟探针(消息层) ================= */

#define XWAKE_TAG_MAX     8      /**< 标签槽数(两端四标签, 充分)。 */
#define XWAKE_SAMPLES     256    /**< 每窗口每标签样本数(满后滚动覆盖)。 */
#define XWAKE_WINDOW_US   5000000ULL

typedef struct XWakeSampleSet {
    uint32_t v[XWAKE_SAMPLES];
    int      n;
} XWakeSampleSet;

typedef struct XWakeTagSlot {
    const char*    tag;      /**< 标签(首次 strcmp 登记, 之后指针比对)。 */
    XWakeSampleSet set;
    uint32_t       over5ms;  /**< 窗口内超 5ms 样本数(隐式节拍证据)。 */
    uint32_t       maxUs;
} XWakeTagSlot;

static struct XWakeProfState {
    uint64_t     winStartUs;
    XWakeTagSlot tags[XWAKE_TAG_MAX];
} xwakeProf;
/* -1=未探测 env(静态零初始化≠已禁用, 探测一次后 0/1 定版)。 */
static int xwakeProfOn = -1;

static void xwakeFlush(uint64_t nowUs); /* 前向: wakeProfPoll 先于定义调用。 */

bool XGuiRemoteUdp_wakeProfOn(void)
{
    if (xwakeProfOn < 0) {
        const char* env = getenv("XGUI_REMOTE_WAKE_PROF");
        xwakeProfOn = (env && env[0] && env[0] != '0') ? 1 : 0;
    }
    return xwakeProfOn != 0;
}

void XGuiRemoteUdp_wakeProfPoll(void)
{
    uint64_t nowUs;
    if (!XGuiRemoteUdp_wakeProfOn()) return;
    nowUs = XAbstractNetIoRing_profNowUs();
    if (xwakeProf.winStartUs &&
        nowUs - xwakeProf.winStartUs >= XWAKE_WINDOW_US)
        xwakeFlush(nowUs);
}

static int xwakeCmpU32(const void* a, const void* b)
{
    uint32_t x = *(const uint32_t*)a, y = *(const uint32_t*)b;
    return (x < y) ? -1 : ((x > y) ? 1 : 0);
}

static uint32_t xwakePct(const XWakeSampleSet* set, int pctX100)
{
    uint32_t buf[XWAKE_SAMPLES];
    int n = set->n;
    int idx;
    if (n <= 0) return 0;
    if (n > XWAKE_SAMPLES) n = XWAKE_SAMPLES;
    memcpy(buf, set->v, (size_t)n * sizeof(uint32_t));
    qsort(buf, (size_t)n, sizeof(uint32_t), xwakeCmpU32);
    idx = (n * pctX100) / 10000;
    if (idx >= n) idx = n - 1;
    return buf[idx];
}

/** @brief 输出全部已登记标签的窗口汇总并复位(每 5s 一批, 防日志洪水)。 */
static void xwakeFlush(uint64_t nowUs)
{
    int i;
    for (i = 0; i < XWAKE_TAG_MAX; ++i) {
        XWakeTagSlot* t = &xwakeProf.tags[i];
        if (!t->tag) continue;
        fprintf(stderr,
                "[wake][%s] n=%d p50=%u p95=%u max=%u over5ms=%u\n",
                t->tag, t->set.n, xwakePct(&t->set, 5000),
                xwakePct(&t->set, 9500), t->maxUs, t->over5ms);
        t->set.n = 0;
        t->over5ms = 0;
        t->maxUs = 0;
    }
    xwakeProf.winStartUs = nowUs;
}

void XGuiRemoteUdp_wakeProfSample(const char* tag, uint64_t arriveUs)
{
    uint64_t nowUs;
    int i;
    XWakeTagSlot* slot = NULL;
    uint32_t latUs;
    if (!XGuiRemoteUdp_wakeProfOn() || !tag || !arriveUs) return;
    nowUs = XAbstractNetIoRing_profNowUs();
    if (!xwakeProf.winStartUs) xwakeProf.winStartUs = nowUs;
    else if (nowUs - xwakeProf.winStartUs >= XWAKE_WINDOW_US)
        xwakeFlush(nowUs);
    if (arriveUs > nowUs) return; /* 时钟异常(跨进程戳误用)防御。 */
    latUs = (uint32_t)(nowUs - arriveUs);
    /* 已登记标签: 指针/内容匹配(先取值后比较, 禁 strcmp(NULL,..))。 */
    for (i = 0; i < XWAKE_TAG_MAX; ++i) {
        const char* t = xwakeProf.tags[i].tag;
        if (t == tag || (t && strcmp(t, tag) == 0)) {
            slot = &xwakeProf.tags[i];
            break;
        }
    }
    /* 未登记: 占首个空槽(标签为调用方静态字符串常量, 存指针即可)。 */
    if (!slot) {
        for (i = 0; i < XWAKE_TAG_MAX; ++i) {
            if (!xwakeProf.tags[i].tag) {
                slot = &xwakeProf.tags[i];
                slot->tag = tag;
                break;
            }
        }
    }
    if (!slot) return;
    slot->set.v[(slot->set.n < XWAKE_SAMPLES)
                    ? slot->set.n
                    : (slot->set.n % XWAKE_SAMPLES)] = latUs;
    ++slot->set.n;
    if (latUs > slot->maxUs) slot->maxUs = latUs;
    if (latUs > 5000u) ++slot->over5ms;
}

uint64_t XGuiRemoteUdpChannel_probeArrivalUs(const XGuiRemoteUdpChannel* c)
{
    if (!c || !c->sock) return 0;
    /* 键=套接字属主对象(XFd 表 desc->object), 非原生 fd——XFd id 是池
     * 索引, 与 sendto 直用的原生 fd 数值无关(见 XAbstractNetIoRing.h)。 */
    return XAbstractNetIoRing_profLastRecvUs((const void*)c->sock);
}

/* ==================== [stage] fb→镜像腿分段计时探针 ====================
 * env XGUI_REMOTE_STAGE_PROF=1 门控(模式与落盘风格同 [wake]: 逐段采样,
 * 每 5s 每标签汇总一行 [stage][tag] n=.. p50=..us p95=..us max=..us;
 * 关闭时打点判据恒 false, 热路径零成本)。
 * 分段口径(mcgs-campaign2「分段分解」任务书):
 *   服务器: srv-cap 呈现回调采集拷贝时长 / srv-lat 采集端→编码线程认领
 *           (maxFps 门控+唤醒等待) / srv-scan 加锁脏扫描+tile 拷出(认领
 *           循环) / srv-enc 认领→批次入队(哈希去重+编码+拆帧) /
 *           srv-q 入队→泵发送始 / srv-send 发送调用时长 /
 *           srv-in2fb 输入注入→呈现回调端(fb 翻转前缘, 设备本地单钟)。
 *   客户端: cli-dec FB_UPDATE 消息解码时长 / cli-merg 损伤合并窗口等待
 *           (解码完→上屏触发, ms 精度×1000 归一) / cli-paint 上屏触发→
 *           paintEvent 派发 / cli-blit paintEvent 绘制(含 FIT 缩放 blit;
 *           paintEvent 返回后的框架 flush 上屏走 XGPU_W_FRAME_PROF=1
 *           的 [wprof] 行, 不在此重复)。
 *   字节列(bytes=窗口合计/avg=每事件均值, stageProfBytes 挂点):
 *   srv-cap 采集写影线格式字节 / srv-enc 编码输出批字节(与 srv-cap 比
 *   即压缩比) / srv-send 发线字节 / cli-dec 收侧 FB_UPDATE 载荷字节
 *   (与 srv-send 对账)。
 * 跨线程段(srv-lat/srv-q)用单调钟戳经队列节点/会话字段传递, 同进程
 * CLOCK_MONOTONIC 同轴可减; 跨机器段(发送→收包)不做减法, 归入残差。 */

#define XSTAGE_TAG_MAX     16     /**< 标签槽数(服务端 7 + 客户端 4, 充分)。 */
#define XSTAGE_SAMPLES     256    /**< 每窗口每标签样本数(满后滚动覆盖)。 */
#define XSTAGE_WINDOW_US   5000000ULL

typedef struct XStageSampleSet {
    uint32_t v[XSTAGE_SAMPLES];
    int      n;
} XStageSampleSet;

typedef struct XStageTagSlot {
    const char*    tag;      /**< 标签(调用方静态字符串常量, 存指针即可)。 */
    XStageSampleSet set;
    uint32_t       maxUs;
    uint64_t       bytes;    /**< 本窗口字节合计(stageProfBytes 累加)。 */
    uint64_t       bytesN;   /**< 本窗口字节事件数(算均值)。 */
} XStageTagSlot;

static struct XStageProfState {
    uint64_t      winStartUs;
    XStageTagSlot tags[XSTAGE_TAG_MAX];
} xstageProf;
/* -1=未探测 env(静态零初始化≠已禁用, 探测一次后 0/1 定版)。 */
static int xstageProfOn = -1;

static void xstageFlush(uint64_t nowUs); /* 前向: stageProfPoll 先于定义调用。 */

bool XGuiRemoteUdp_stageProfOn(void)
{
    if (xstageProfOn < 0) {
        const char* env = getenv("XGUI_REMOTE_STAGE_PROF");
        xstageProfOn = (env && env[0] && env[0] != '0') ? 1 : 0;
    }
    return xstageProfOn != 0;
}

uint64_t XGuiRemoteUdp_stageProfNowUs(void)
{
    if (!XGuiRemoteUdp_stageProfOn()) return 0;
    return XAbstractNetIoRing_profNowUs();
}

void XGuiRemoteUdp_stageProfPoll(void)
{
    uint64_t nowUs;
    if (!XGuiRemoteUdp_stageProfOn()) return;
    nowUs = XAbstractNetIoRing_profNowUs();
    if (xstageProf.winStartUs &&
        nowUs - xstageProf.winStartUs >= XSTAGE_WINDOW_US)
        xstageFlush(nowUs);
}

static int xstageCmpU32(const void* a, const void* b)
{
    uint32_t x = *(const uint32_t*)a, y = *(const uint32_t*)b;
    return (x < y) ? -1 : ((x > y) ? 1 : 0);
}

static uint32_t xstagePct(const XStageSampleSet* set, int pctX100)
{
    uint32_t buf[XSTAGE_SAMPLES];
    int n = set->n;
    int idx;
    if (n <= 0) return 0;
    if (n > XSTAGE_SAMPLES) n = XSTAGE_SAMPLES;
    memcpy(buf, set->v, (size_t)n * sizeof(uint32_t));
    qsort(buf, (size_t)n, sizeof(uint32_t), xstageCmpU32);
    idx = (n * pctX100) / 10000;
    if (idx >= n) idx = n - 1;
    return buf[idx];
}

/** @brief 输出全部已登记标签的窗口汇总并复位(每 5s 一批, 防日志洪水)。 */
static void xstageFlush(uint64_t nowUs)
{
    int i;
    for (i = 0; i < XSTAGE_TAG_MAX; ++i) {
        XStageTagSlot* t = &xstageProf.tags[i];
        if (!t->tag) continue;
        if (t->bytesN > 0) {
            fprintf(stderr,
                    "[stage][%s] n=%d p50=%u p95=%u max=%u "
                    "bytes=%llu avg=%llu\n",
                    t->tag, t->set.n, xstagePct(&t->set, 5000),
                    xstagePct(&t->set, 9500), t->maxUs,
                    (unsigned long long)t->bytes,
                    (unsigned long long)(t->bytes / t->bytesN));
        } else {
            fprintf(stderr,
                    "[stage][%s] n=%d p50=%u p95=%u max=%u\n",
                    t->tag, t->set.n, xstagePct(&t->set, 5000),
                    xstagePct(&t->set, 9500), t->maxUs);
        }
        t->set.n = 0;
        t->maxUs = 0;
        t->bytes = 0;
        t->bytesN = 0;
    }
    xstageProf.winStartUs = nowUs;
}

/** @brief 槽位查找/登记(span 与 bytes 共用; 满槽静默丢弃)。 */
static XStageTagSlot* xstageSlot(const char* tag)
{
    int i;
    for (i = 0; i < XSTAGE_TAG_MAX; ++i) {
        const char* t = xstageProf.tags[i].tag;
        if (t == tag || (t && strcmp(t, tag) == 0))
            return &xstageProf.tags[i];
    }
    /* 未登记: 占首个空槽。 */
    for (i = 0; i < XSTAGE_TAG_MAX; ++i) {
        if (!xstageProf.tags[i].tag) {
            xstageProf.tags[i].tag = tag;
            return &xstageProf.tags[i];
        }
    }
    return NULL;
}

/**
 * @brief  跨点段采样: 段长=t1Us−t0Us(同进程单调钟; t1<t0 视为时钟
 *         异常/陈旧戳丢弃, 0 戳=未打点跳过)。
 */
void XGuiRemoteUdp_stageProfSpan(const char* tag, uint64_t t0Us, uint64_t t1Us)
{
    XStageTagSlot* slot;
    uint32_t durUs;
    if (!XGuiRemoteUdp_stageProfOn() || !tag || !t0Us || !t1Us ||
        t1Us < t0Us)
        return;
    durUs = (uint32_t)(t1Us - t0Us);
    if (!xstageProf.winStartUs) {
        xstageProf.winStartUs = t1Us;
    } else if (t1Us - xstageProf.winStartUs >= XSTAGE_WINDOW_US) {
        xstageFlush(t1Us);
    }
    /* 已登记标签: 指针/内容匹配(先取值后比较, 禁 strcmp(NULL,..))。 */
    slot = xstageSlot(tag);
    if (!slot) return;
    slot->set.v[(slot->set.n < XSTAGE_SAMPLES)
                    ? slot->set.n
                    : (slot->set.n % XSTAGE_SAMPLES)] = durUs;
    ++slot->set.n;
    if (durUs > slot->maxUs) slot->maxUs = durUs;
}

/** @brief 单段时长便捷采样(t0=stageProfNowUs 打点; 关闭时 t0=0 恒跳过)。 */
void XGuiRemoteUdp_stageProfSample(const char* tag, uint64_t t0Us)
{
    if (!XGuiRemoteUdp_stageProfOn() || !t0Us) return;
    XGuiRemoteUdp_stageProfSpan(tag, t0Us, XAbstractNetIoRing_profNowUs());
}

void XGuiRemoteUdp_stageProfBytes(const char* tag, uint32_t n)
{
    XStageTagSlot* slot;
    if (!XGuiRemoteUdp_stageProfOn() || !tag || n == 0) return;
    if (xstageProf.winStartUs) {
        uint64_t nowUs = XAbstractNetIoRing_profNowUs();
        if (nowUs - xstageProf.winStartUs >= XSTAGE_WINDOW_US)
            xstageFlush(nowUs);
    }
    slot = xstageSlot(tag);
    if (!slot) return;
    slot->bytes += (uint64_t)n;
    ++slot->bytesN;
}

#endif /* XGUI_REMOTE_ON */
