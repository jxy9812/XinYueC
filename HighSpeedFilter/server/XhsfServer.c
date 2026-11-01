/**
 * @file       XhsfServer.c
 * @brief      高速过滤器服务端实现（TCP 文本协议 + IIR 低通滤波）。
 * @details    单线程事件循环、零锁。接收/断开经 XTcpServer/XTcpSocket
 *             信号驱动；行解析在每连接缓冲上做增量切分，半行跨包保留。
 *             XTcpSocket 为 XObject 派生：断开后 deleteLater 统一回收。
 */
#include "XhsfServer.h"

#include "XTcpServer.h"
#include "XTcpSocket.h"
#include "XHostAddress.h"
#include "XMemory.h"
#include "XPrintf.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

/* ==================== 内部实现 ==================== */

/** @brief 槽位查找：按套接字指针定位客户端槽，未命中 -1。 */
static int hsfFindSlot(XhsfServer* srv, const XTcpSocket* sock)
{
    int i;
    for (i = 0; i < XHSF_SERVER_MAX_CLIENTS; ++i) {
        if (srv->m_clients[i] == sock) return i;
    }
    return -1;
}

/** @brief IIR 低通：提交一个采样，返回滤波后 y（首样直接播种）。 */
static double hsfFilterPush(XhsfServer* srv, double x)
{
    if (!srv->m_hasY) {
        srv->m_filterY = x;
        srv->m_hasY = true;
    }
    else {
        srv->m_filterY += srv->m_alpha * (x - srv->m_filterY);
    }
    return srv->m_filterY;
}

/** @brief 应答写回（失败仅计数日志：模板不做重发队列）。 */
static void hsfReply(XTcpSocket* sock, const char* text)
{
    int64_t sent = XTcpSocket_write_1(sock, text, (int64_t)strlen(text));
    if (sent <= 0)
        XPrintf("[HsfServer] 应答写失败（对端可能已断开）\n");
}

/**
 * @brief 处理一行协议。
 * @return 返回响应文本（静态缓冲）供调用方回写；无响应返回 NULL。
 */
static const char* hsfProcessLine(XhsfServer* srv, char* line)
{
    static char reply[64];
    if (line[0] == 'd') {
        /* d <浮点>：提交采样 → 滤波应答。 */
        double x = atof(line + 1);
        double y = hsfFilterPush(srv, x);
        snprintf(reply, sizeof(reply), "r %.4f\n", y);
        XhsfServer_filtered_signal(srv, y);
        return reply;
    }
    if (line[0] == 'a') {
        /* a <浮点>：设置平滑系数，钳位 (0..1]。 */
        double a = atof(line + 1);
        if (a <= 0.0) a = 0.001;
        if (a > 1.0) a = 1.0;
        srv->m_alpha = a;
        snprintf(reply, sizeof(reply), "ok\n");
        return reply;
    }
    if (line[0] == 'z') {
        /* z：复位滤波器（丢弃播种，下一样重播种）。 */
        srv->m_hasY = false;
        srv->m_filterY = 0.0;
        snprintf(reply, sizeof(reply), "ok\n");
        return reply;
    }
    return NULL; /* 未知行静默丢弃（模板不实现错误回包）。 */
}

/** @brief 行切分：就绪缓冲里逐 '\n' 提取并应答，半行保留跨包。 */
static void hsfProcessBuffer(XhsfServer* srv, int slot)
{
    char* buf = srv->m_rxBuf[slot];
    int* len = &srv->m_rxLen[slot];
    int start = 0;
    int i;
    for (i = 0; i < *len; ++i) {
        if (buf[i] != '\n') continue;
        buf[i] = '\0'; /* 行内行尾替换为字符串结尾。 */
        if (i > start) { /* 空行跳过。 */
            const char* reply = hsfProcessLine(srv, buf + start);
            if (reply) hsfReply(srv->m_clients[slot], reply);
        }
        start = i + 1;
    }
    /* 半行搬回到缓冲头。 */
    if (start > 0) {
        memmove(buf, buf + start, (size_t)(*len - start));
        *len -= start;
    }
}

/* ==================== 信号槽（connect_2：第 1 参=真 sender） ==================== */

static void hsfOnReadyRead(XObject* sender, XVarList* args)
{
    XhsfServer* srv;
    XTcpSocket* sock = (XTcpSocket*)sender;
    int slot;
    (void)args;
    if (!sock) return;
    srv = (XhsfServer*)XObject_parent(sender);
    if (!srv) return;
    slot = hsfFindSlot(srv, sock);
    if (slot < 0) return; /* 已摘除的连接迟到事件：丢弃。 */

    /* 读循环：缓冲满则截断（该行超长按协议违约处理，丢弃尾部）。 */
    for (;;) {
        int64_t n;
        int room = XHSF_SERVER_RXBUF_SIZE - srv->m_rxLen[slot];
        if (room <= 0) { srv->m_rxLen[slot] = 0; room = XHSF_SERVER_RXBUF_SIZE; }
        n = XTcpSocket_read_1(sock, srv->m_rxBuf[slot] + srv->m_rxLen[slot], room);
        if (n <= 0) break;
        srv->m_rxLen[slot] += (int)n;
        hsfProcessBuffer(srv, slot);
    }
}

static void hsfOnDisconnected(XObject* sender, XVarList* args)
{
    XhsfServer* srv;
    XTcpSocket* sock = (XTcpSocket*)sender;
    int slot;
    (void)args;
    if (!sock) return;
    srv = (XhsfServer*)XObject_parent(sender);
    if (!srv) return;
    slot = hsfFindSlot(srv, sock);
    if (slot >= 0) {
        srv->m_clients[slot] = NULL;
        srv->m_rxLen[slot] = 0;
    }
    XPrintf("[HsfServer] 客户端断开\n");
    XObject_deleteLater(sender); /* 处于自身信号链上：延迟释放。 */
}

static void hsfOnNewConnection(XObject* sender, XVarList* args)
{
    XhsfServer* srv = (XhsfServer*)XObject_parent(sender);
    (void)args;
    if (!srv || !srv->m_listener) return;
    /* 就绪连接可能一次到多个：循环取尽。 */
    for (;;) {
        XTcpSocket* sock = XTcpServer_nextPendingConnection_base(srv->m_listener);
        int slot;
        if (!sock) break;
        slot = hsfFindSlot(srv, NULL);
        if (slot < 0) {
            XPrintf("[HsfServer] 连接已满（>%d），拒绝\n", XHSF_SERVER_MAX_CLIENTS);
            XObject_deleteLater((XObject*)sock);
            continue;
        }
        /* 接管套接字：挂为服务端子对象（readyRead/断开槽经
         * XObject_parent(sender) 反查服务端，不设 parent 槽直接失效）
         * + 析构链统一回收。 */
        XObject_setParent((XObject*)sock, (XObject*)srv);
        srv->m_clients[slot] = sock;
        srv->m_rxLen[slot] = 0;
        /* connect_2：槽第 1 参=真 sender（套接字），约束文档红线。 */
        XObject_connect_2((XObject*)sock,
                          XSignal(XTcpSocket_readyRead_signal), hsfOnReadyRead);
        XObject_connect_2((XObject*)sock,
                          XSignal(XTcpSocket_disconnected_signal), hsfOnDisconnected);
        XPrintf("[HsfServer] 客户端接入（槽 %d）\n", slot);
    }
}

/* ==================== 虚函数 ==================== */

static void VXhsfServer_deinit(XhsfServer* srv)
{
    int i;
    if (!srv) return;
    if (srv->m_listener) {
        XClassDelete(srv->m_listener); /* 套接字不在此逐一清理： listener 子对象析构链统一收尾。 */
        srv->m_listener = NULL;
    }
    for (i = 0; i < XHSF_SERVER_MAX_CLIENTS; ++i) {
        srv->m_clients[i] = NULL;
        srv->m_rxLen[i] = 0;
    }
    XClass_Deinit_Parent(XhsfServer, srv);
}

XVtable* XhsfServer_class_init(void)
{
    XVTABLE_INIT_DEFAULT(XhsfServer)
    XVTABLE_INHERIT_XCLASS(XObject);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Deinit, VXhsfServer_deinit);
    return XVTABLE_DEFAULT;
}

/* ==================== 构造 ==================== */

void XhsfServer_init(XhsfServer* srv)
{
    int i;
    if (!srv) return;
    memset(srv, 0, sizeof(*srv));
    XObject_init((XObject*)srv);
    XClassSetVtable(srv, XhsfServer);
    srv->m_alpha = 0.5;
    for (i = 0; i < XHSF_SERVER_MAX_CLIENTS; ++i) srv->m_clients[i] = NULL;
}

XhsfServer* XhsfServer_create(void)
{
    XhsfServer* srv = (XhsfServer*)XMemory_calloc(1, sizeof(XhsfServer),
                                                  XCLASS_DEFAULT_MEMORY_TYPE);
    if (!srv) return NULL;
    XhsfServer_init(srv);
    Set_Class_IsHeap(srv, true);
    return srv;
}

/* ==================== 启停 ==================== */

bool XhsfServer_listen(XhsfServer* srv, unsigned port)
{
    bool ok;
    if (!srv || srv->m_listener) return false;
    srv->m_listener = XTcpServer_create_ex(XCLASS_DEFAULT_MEMORY_TYPE);
    if (!srv->m_listener) return false;
    XObject_setParent((XObject*)srv->m_listener, (XObject*)srv);
    /* 连接到达信号（connect_2：槽第 1 参=真 sender=listener）。 */
    XObject_connect_2((XObject*)srv->m_listener,
                      XSignal(XTcpServer_newConnection_signal), hsfOnNewConnection);
    {
        /* 绑定地址走堆对象（XModbusTcpServer 同款）：XHostAddress_AnyIPv4
         * 是未挂虚表的 const 全局，直喂 listen 会在内部 XClassCopy 时
         * XClassGetVtable(src)=NULL 报错（2026-10-31 实证）。 */
        XHostAddress* any = XHostAddress_create_fromString("0.0.0.0");
        if (!any) return false;
        ok = XTcpServer_listen(srv->m_listener, any, (uint16_t)port);
        XClassDelete(any);
    }
    if (!ok) return false;
    srv->m_port = (unsigned)XTcpServer_serverPort(srv->m_listener);
    return true;
}

unsigned XhsfServer_port(const XhsfServer* srv)
{
    return srv ? srv->m_port : 0;
}

/* ==================== 信号 ==================== */

void* XhsfServer_filtered_signal(XhsfServer* srv, double y)
{
    XEmitSignal(srv, XhsfServer_filtered_signal,
                XVarList_Create(XVar(double, y)), NULL, NULL,
                XEVENT_PRIORITY_NORMAL);
}
