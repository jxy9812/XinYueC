/** @file       XDdnsHttp.c
 * @brief      XDdns 轻量 HTTP GET 传输（裸 socket 直发，绕开 XNM）。
 * @details    为什么不用 XNetworkAccessManager：XNM 的同步 GET 对真实
 *             互联网地址存在崩溃/超时问题（2026-10-16 实测：控制台进程
 *             settle 崩溃、GUI 进程超时），而裸 socket 直连收发正常。
 *             DDNS 的请求全部为无状态的签名 GET，用 HTTP/1.0 +
 *             Connection: close 的最小客户端最贴合：
 *               · 服务器应答完即关连接，无需 keep-alive 与管道化；
 *               · 数据驱动收包（静默窗口内无新数据即收齐），不依赖对端
 *                 关闭事件（部分平台不落地）；
 *               · https 端点经 XSslSocket 走 TLS（云厂商 API 强制）。
 *             实现只依赖 XinYueC 抽象层，禁止调用 Win32、POSIX 或其他平台 API。
 * @author     XinYueC 团队
 */
#include "XDdnsHttp.h"

#if XDDNS_ON && XDDNS_CLIENT_ON

#include "XUrl.h"
#include "XString.h"
#include "XTcpSocket.h"
#include "XAbstractSocket.h"
#include "XSslSocket.h"
#include "XDateTime.h"
#include "XThread.h"
#include "XMemory.h"
#include <string.h>
#include <stdlib.h>

/** @brief 单次请求的接收缓冲上限（阿里云 Describe 应答 <2KB，余量充足）。 */
#define XDDNS_HTTP_RECV_MAX 4096u
/** @brief 单次 read 的请求上限（分块收，避免大块读触发驱动边界）。 */
#define XDDNS_HTTP_READ_CHUNK 256u
/** @brief 收包静默窗口（毫秒）：该窗口内无新数据即视为应答收齐。 */
#define XDDNS_HTTP_IDLE_MS 150

/* 大缓冲置于文件级静态区：XDdnsHttp_get 在事件循环深处调用，函数栈上再
 * 叠 ~8KB 数组（请求行 + 接收缓冲 + 请求路径）在深栈嵌套下有溢出风险。
 * XDdns 非重入（客户端单飞调用），静态区复用安全。 */
static char g_xdnsHttpHost[XDDNS_NAME_MAX];
static char g_xdnsHttpPath[XDDNS_URL_MAX];
static char g_xdnsHttpRequest[XDDNS_URL_MAX + 160];
static char g_xdnsHttpRecv[XDDNS_HTTP_RECV_MAX + 1];

/**
 * @brief 从 URL 拆出 host/port/path?query（供请求行与 Host 头使用）。
 * @param  urlUtf8 完整 URL（借用）。
 * @param  hostOut 主机名输出缓冲区（调用方提供存储空间）。
 * @param  hostCap 主机缓冲容量。
 * @param  portOut 端口输出（调用方提供存储空间）；URL 未携带时按 scheme
 *                 补默认（http=80/https=443）。
 * @param  reqPathOut 请求行路径输出（调用方提供存储空间，"/" 或 "/?query"）。
 * @param  pathCap 请求路径缓冲容量。
 * @return 解析成功返回 true；参数非法或容量不足返回 false。
 */
static bool xdns_http_split_url(const char* urlUtf8, char* hostOut,
                               size_t hostCap, uint16_t* portOut,
                               char* reqPathOut, size_t pathCap)
{
    XUrl* url;
    XString* us;
    const XString* host;
    const XString* path;
    const XString* query;
    const XString* scheme;
    int port;
    bool ok = false;

    if (!urlUtf8 || !hostOut || !portOut || !reqPathOut)
        return false;
    url = XUrl_create();
    if (!url)
        return false;
    us = XString_create_utf8(urlUtf8);
    if (!us) {
        XClassDelete(url);
        return false;
    }
    XUrl_setUrl(url, us, XUrl_TolerantMode);
    XClassDelete(us);

    host = XUrl_host_const(url);
    scheme = XUrl_scheme_const(url);
    path = XUrl_path_const(url);
    query = XUrl_query_const(url);
    if (!host || !scheme) {
        XClassDelete(url);
        return false;
    }

    {
        const char* hostUtf8 = XString_toUtf8(host);
        const char* schemeUtf8 = XString_toUtf8(scheme);
        if (!hostUtf8 || XStrlen(hostUtf8) >= hostCap) {
            XClassDelete(url);
            return false;
        }
        memcpy(hostOut, hostUtf8, XStrlen(hostUtf8) + 1);
        *portOut = (uint16_t)(XStrstr(schemeUtf8, "https") == schemeUtf8
                                  ? 443u : 80u);
    }
    port = (int)XUrl_port(url);
    if (port > 0)
        *portOut = (uint16_t)port;

    {
        const char* pathUtf8 = path ? XString_toUtf8(path) : "/";
        const char* queryUtf8 = query ? XString_toUtf8(query) : NULL;
        size_t used = 0;
        if (!pathUtf8 || pathUtf8[0] == '\0')
            pathUtf8 = "/";
        if (XStrlen(pathUtf8) + (queryUtf8 ? XStrlen(queryUtf8) + 1 : 0) + 1
            > pathCap) {
            XClassDelete(url);
            return false;
        }
        /* 容量已预先校验，memcpy 安全（XStrcpy 无容量参数） */
        memcpy(reqPathOut, pathUtf8, XStrlen(pathUtf8) + 1);
        used = XStrlen(pathUtf8);
        if (queryUtf8 && queryUtf8[0]) {
            memcpy(reqPathOut + used, "?", 1);
            ++used;
            memcpy(reqPathOut + used, queryUtf8, XStrlen(queryUtf8) + 1);
        }
    }
    ok = true;
    XClassDelete(url);
    return ok;
}

bool XDdnsHttp_get(const char* urlUtf8, uint32_t timeoutMs,
                   char* bodyOut, size_t cap, int* statusCodeOut)
{
    /* 大缓冲走文件级静态区（见定义处说明：避免深栈溢出）。 */
    char* const host = g_xdnsHttpHost;
    char* const reqPath = g_xdnsHttpPath;
    char* const request = g_xdnsHttpRequest;
    char* const recvBuf = g_xdnsHttpRecv;
    uint16_t port = 80u;
    XIODevice* sock = NULL;
    size_t recvUsed = 0;
    int64_t deadline;
    bool ok = false;
    int statusCode = 0;
    int n;

    if (!urlUtf8 || !bodyOut || cap == 0)
        return false;
    bodyOut[0] = '\0';
    if (statusCodeOut)
        *statusCodeOut = 0;

    if (!xdns_http_split_url(urlUtf8, host, XDDNS_NAME_MAX, &port,
                             reqPath, XDDNS_URL_MAX))
        return false;

    /* https 端点走 XSslSocket（云厂商 API 强制 TLS）；http 走裸 TCP。
     * 二者同为 XIODevice 家族，下方收发逻辑统一。 */
    if (port == 443u) {
        XSslSocket* ssl = XSslSocket_create();
        XString* peerName;
        if (!ssl)
            return false;
        /* 默认跳过对端证书校验（嵌入式无 CA 库的现实选择；请求本身由
         * POP 签名防篡改，云端亦按签名校验身份）。 */
        XSslSocket_setPeerVerifyMode(ssl, XSSL_VerifyNone);
        peerName = XString_create_utf8(host);
        if (!peerName) {
            XClassDelete((XClass*)ssl);
            return false;
        }
        XSslSocket_connectToHostEncrypted(ssl, peerName, port);
        XClassDelete(peerName);
        if (!XSslSocket_waitForEncrypted(ssl, 8000)) {
            XClassDelete((XClass*)ssl);
            return false;
        }
        sock = (XIODevice*)ssl;
    } else {
        sock = (XIODevice*)XTcpSocket_create();
        if (!sock)
            return false;
        XAbstractSocket_connectToHost_base((XAbstractSocket*)sock, host, port,
                                           XIODevice_ReadWrite,
                                           XAbstractSocket_IPv4Protocol);
        if (!XAbstractSocket_waitForConnected_base((XAbstractSocket*)sock,
                                                   5000)) {
            XClassDelete((XClass*)sock);
            return false;
        }
    }

    /* HTTP/1.0 + Connection: close：服务器发完即关，无需 keep-alive 与
     * chunked/Content-Length 解析。 */
    n = XSnprintf(request, XDDNS_URL_MAX + 160,
                  "GET %s HTTP/1.0\r\n"
                  "Host: %s\r\n"
                  "User-Agent: XinYueC-XDdns\r\n"
                  "Connection: close\r\n"
                  "\r\n",
                  reqPath, host);
    if (n <= 0 || (size_t)n >= XDDNS_URL_MAX + 160)
        goto cleanup;
    if (XIODevice_write_1(sock, request, n) != (int64_t)n)
        goto cleanup;

    /* 数据驱动收包：静默窗口内无新数据即视为应答收齐。部分平台对端关闭
     * 事件不落地，不能依赖 read 返回 0 判完整。 */
    recvUsed = 0;
    recvBuf[0] = '\0';
    deadline = XDateTime_currentMSecsSinceEpoch() + (int64_t)timeoutMs;
    for (;;) {
        int64_t got;
        size_t room;
        if (XDateTime_currentMSecsSinceEpoch() >= deadline)
            break; /* 总超时 */
        if (!XIODevice_waitForReadyRead_base(sock, XDDNS_HTTP_IDLE_MS))
            break; /* 静默窗口到：应答收齐 */
        room = XDDNS_HTTP_RECV_MAX - recvUsed;
        if (room == 0)
            break;
        if (room > XDDNS_HTTP_READ_CHUNK)
            room = XDDNS_HTTP_READ_CHUNK;
        got = XIODevice_read_1(sock, recvBuf + recvUsed, (int64_t)room);
        if (got <= 0)
            break; /* 对端关闭或读错误 */
        recvUsed += (size_t)got;
        recvBuf[recvUsed] = '\0';
    }
    recvBuf[recvUsed] = '\0';

    /* 拆状态行与应答体 */
    {
        const char* body = strstr(recvBuf, "\r\n\r\n");
        const char* sp;
        if (!body)
            goto cleanup;
        body += 4;
        if (strncmp(recvBuf, "HTTP/1.", 7) != 0)
            goto cleanup;
        sp = strchr(recvBuf, ' ');
        if (sp)
            statusCode = atoi(sp + 1);
        /* 应答体先拷贝（含非 2xx）：云端拒绝时正文里带 Code/Message
           （如 SignatureDoesNotMatch / InvalidAccessKeyId），是诊断关键，
           调用方按 statusCode 自行判定成败。 */
        if (XStrlen(body) < cap)
            memcpy(bodyOut, body, XStrlen(body) + 1);
        if (statusCode < 200 || statusCode >= 300)
            goto cleanup; /* 非 2xx：状态码与正文交调用方诊断 */
        if (XStrlen(body) >= cap)
            goto cleanup; /* 超长应答：不静默截断 */
        ok = true;
    }

cleanup:
    /* 状态码必须无条件回传：非 2xx（云端拒绝签名/权限）也是通过 goto
     * 跳到本处的，若只在成功路径上报，调用方永远拿不到 400/403 而只能
     * 看到笼统的 BadResponse，诊断信息全丢。 */
    if (statusCodeOut)
        *statusCodeOut = statusCode;

    if (sock) {
        XAbstractSocket_abort((XAbstractSocket*)sock);
        XClassDelete((XClass*)sock);
    }
    return ok;
}

#endif /* XDDNS_ON && XDDNS_CLIENT_ON */
