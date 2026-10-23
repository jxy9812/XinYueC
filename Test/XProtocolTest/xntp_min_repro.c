/** @file       xntp_min_repro.c
 * @brief       XNtpClient「应答超时」最小控制台复现程序（分阶段探针）。
 * @details     绕开 GUI，直接实例化 XNtpClient 对 ntp.aliyun.com 跑一次
 *              真实查询，stderr 分阶段打点：
 *                [1] DNS 解析（XHostInfo 直查，逐地址打印）
 *                [2] 同进程裸 POSIX 对照组（socket/sendto/recvfrom，证明
 *                    本进程本沙箱 UDP 123 出网畅通——等价同机 python 直连）
 *                [3] XNtpClient 全链路：解析结果/socket 创建与本地端口/
 *                    writeDatagram 返回值/进入等待时刻/信号到达时刻/
 *                    超时时刻/收包缓冲 bytesAvailable 终值
 *              逐阶段对比即可定位断点在 DNS、发送、还是接收。
 *              退出码 0=取到服务器时间；1=失败。
 * @author      XinYueC 团队
 */
#include "CXinYueConfig.h"

#if XPROTOCOL_ON && XNTP_ON

#include "XNtpClient.h"
#include "XHostInfo.h"
#include "XHostAddress.h"
#include "XUdpSocket.h"
#include "XIODevicePrivate.h"
#include "XCoreApplication.h"
#include "XDateTime.h"
#include "XThread.h"
#include "XPrintf.h"
#include "XMemory.h"

/* 裸 POSIX 对照组仅在本复现程序内使用（诊断工具，非库代码）。 */
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <netdb.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#define REPRO_SERVER    "ntp.aliyun.com"
#define REPRO_PORT      123
#define REPRO_WAIT_MS   9000

/** @brief 毫秒时戳打点。 */
static long repro_now(void)
{
    return (long)XDateTime_currentMSecsSinceEpoch();
}

/** @brief 阶段打点（stderr，带毫秒时标）。 */
#define REPRO_LOG(...)                                                    \
    do {                                                                  \
        fprintf(stderr, "[%8ld ms] ", repro_now());                       \
        fprintf(stderr, __VA_ARGS__);                                     \
        fprintf(stderr, "\n");                                            \
        fflush(stderr);                                                   \
    } while (0)

/* ========================================================================== */
/*                        阶段 1：DNS 解析探针                               */
/* ========================================================================== */

static int repro_stage_dns(void)
{
    XHostInfo* info = XHostInfo_fromName2(REPRO_SERVER);
    const XVector* addresses;
    size_t i;
    int count = 0;

    REPRO_LOG("[1-DNS] XHostInfo_fromName(\"%s\") -> %s", REPRO_SERVER,
              info ? "对象返回" : "NULL");
    if (!info)
        return 0;
    REPRO_LOG("[1-DNS] error=%d (0=NoError)", (int)XHostInfo_error(info));
    if (XHostInfo_error(info) != XHostInfo_NoError) {
        XClassDelete(info);
        return 0;
    }
    addresses = XHostInfo_addresses_const(info);
    if (addresses) {
        XVector_iterator it = XVector_begin((XVector*)addresses);
        for (i = 0; i < XVector_size_base(addresses); ++i) {
            XHostAddress* a = (XHostAddress*)XVector_iterator_data(&it);
            if (a) {
                char buf[64] = { 0 };
                XString* s = XHostAddress_toString(a);
                if (s) {
                    const char* u = XString_toUtf8(s);
                    if (u && strlen(u) < sizeof(buf))
                        memcpy(buf, u, strlen(u));
                    XClassDelete(s);
                }
                REPRO_LOG("[1-DNS] 地址[%zu] = %s (protocol=%d)", i,
                          buf[0] ? buf : "?", (int)XHostAddress_protocol(a));
                ++count;
            }
            XVector_iterator_add((XVector*)addresses, &it);
        }
    }
    XClassDelete(info);
    return count;
}

/* ========================================================================== */
/*                 阶段 2：同进程裸 POSIX UDP 对照组                         */
/* ========================================================================== */

static int repro_stage_raw(void)
{
    struct addrinfo hints;
    struct addrinfo* result = NULL;
    struct sockaddr_in dest;
    unsigned char packet[48];
    unsigned char reply[256];
    int fd = -1;
    long t0;
    int ok = 0;

    REPRO_LOG("[2-RAW] 裸 POSIX 对照组开始（同进程，证明 UDP 123 出网畅通）");
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_DGRAM;
    if (getaddrinfo(REPRO_SERVER, NULL, &hints, &result) != 0 || !result) {
        REPRO_LOG("[2-RAW] getaddrinfo 失败");
        return 0;
    }
    memset(&dest, 0, sizeof(dest));
    dest.sin_family = AF_INET;
    dest.sin_port = htons(REPRO_PORT);
    dest.sin_addr = ((struct sockaddr_in*)result->ai_addr)->sin_addr;
    freeaddrinfo(result);
    {
        char ip[INET_ADDRSTRLEN] = { 0 };
        inet_ntop(AF_INET, &dest.sin_addr, ip, sizeof(ip));
        REPRO_LOG("[2-RAW] 目标 %s:%d", ip, REPRO_PORT);
    }

    fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (fd < 0) {
        REPRO_LOG("[2-RAW] socket() 失败");
        return 0;
    }
    /* SNTP 客户端请求：LI=0 VN=4 Mode=3，Transmit(T1) 填非零。 */
    memset(packet, 0, sizeof(packet));
    packet[0] = 0x23;
    packet[40] = 0x01;
    t0 = repro_now();
    {
        ssize_t sent = sendto(fd, packet, sizeof(packet), 0,
                              (const struct sockaddr*)&dest, sizeof(dest));
        REPRO_LOG("[2-RAW] sendto(48B) = %zd (errno=%d)", sent, sent < 0 ? errno : 0);
        if (sent == (ssize_t)sizeof(packet)) {
            struct timeval tv;
            ssize_t got;
            tv.tv_sec = 4;
            tv.tv_usec = 0;
            setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
            got = recvfrom(fd, reply, sizeof(reply), 0, NULL, NULL);
            REPRO_LOG("[2-RAW] recvfrom = %zd (errno=%d) 耗时 %ld ms%s",
                      got, got < 0 ? errno : 0, repro_now() - t0,
                      got >= 48 ? " [模式字节 0x24=服务器应答]" : "");
            if (got >= 48 && (reply[0] & 0x07) == 4) {
                uint32_t secs = ((uint32_t)reply[40] << 24)
                              | ((uint32_t)reply[41] << 16)
                              | ((uint32_t)reply[42] << 8)
                              | (uint32_t)reply[43];
                REPRO_LOG("[2-RAW] 对照组成功: 服务器 NTP 秒=%u "
                          "(Unix 秒=%u)", secs, secs - 2208988800u);
                ok = 1;
            }
        }
    }
    close(fd);
    return ok;
}

/* ========================================================================== */
/*                    阶段 3：XNtpClient 全链路探针                          */
/* ========================================================================== */

static volatile int g_state = 0; /* 0=等待中 1=成功 2=失败 */
static XUdpSocket* g_probeSock = NULL; /* 由探针定期回读收包缓冲 */

static void repro_on_synced(XObject* sender, XVarList* args)
{
    (void)sender; (void)args;
    REPRO_LOG("[3-NTP] synchronized 信号到达");
    g_state = 1;
}

static void repro_on_failed(XObject* sender, XVarList* args)
{
    (void)sender; (void)args;
    REPRO_LOG("[3-NTP] syncFailed 信号到达");
    g_state = 2;
}

static int repro_stage_client(void)
{
    XNtpClient* client;
    int64_t tWait;
    int synced = 0;

    client = XNtpClient_create();
    if (!client) {
        REPRO_LOG("[3-NTP] XNtpClient_create 失败");
        return 0;
    }
    XObject_connect_2((XObject*)client,
                      XSignal(XNtpClient_synchronized_signal), repro_on_synced);
    XObject_connect_2((XObject*)client,
                      XSignal(XNtpClient_syncFailed_signal), repro_on_failed);
    XNtpClient_setServer_2(client, REPRO_SERVER, REPRO_PORT);
    XNtpClient_setTimeout(client, 4000);
    XNtpClient_setRetries(client, 0);

    if (!XNtpClient_syncNow(client)) {
        REPRO_LOG("[3-NTP] syncNow 返回 false（未发出）lastError=%d",
                  (int)XNtpClient_lastError(client));
        XClassDelete((XClass*)client);
        return 0;
    }
    REPRO_LOG("[3-NTP] syncNow 返回 true（内部 writeDatagram=%d）",
              XNtpClient_lastError(client) == 0 ? 48 : -1);
    REPRO_LOG("[3-NTP] 进入等待（上限 %d ms）", REPRO_WAIT_MS);
    tWait = (int64_t)repro_now();
    g_state = 0;
    while (g_state == 0 && repro_now() - tWait < REPRO_WAIT_MS) {
        XCoreApplication_processEvents(XEventLoop_AllEvents);
        XThread_msleep(10);
        /* 每 2 秒回读一次收包缓冲：应答若到达而未派发，此处能看见。 */
        if (g_probeSock) {
            static long lastProbe = 0;
            long now = repro_now();
            if (now - lastProbe >= 2000) {
                lastProbe = now;
                REPRO_LOG("[3-NTP] 心跳: state=%d bytesAvailable=%lld "
                          "本地端口=%u",
                          (int)XNtpClient_state(client),
                          (long long)XIODevice_bytesAvailable_base(
                              (const XIODevice*)g_probeSock),
                          (unsigned)XUdpSocket_localPort(g_probeSock));
            }
        }
    }
    if (g_state != 0)
        REPRO_LOG("[3-NTP] %ld ms 后出结果", repro_now() - tWait);
    else
        REPRO_LOG("[3-NTP] %ld ms 等待超时（无信号）", repro_now() - tWait);

    REPRO_LOG("[3-NTP] 终态: state=%d (0=Idle 1=Syncing 2=Synchronized "
              "3=Failed) lastError=%d (3=超时)",
              (int)XNtpClient_state(client),
              (int)XNtpClient_lastError(client));
    if (g_state == 1 || XNtpClient_isSynchronized(client)) {
        REPRO_LOG("[3-NTP] 成功: offset=%lld ms delay=%lld ms stratum=%u "
                  "serverSecs=%llu (本机墙钟=%lld, 差 %lld 秒)",
                  (long long)XNtpClient_offsetMs(client),
                  (long long)XNtpClient_roundTripMs(client),
                  (unsigned)XNtpClient_stratum(client),
                  (unsigned long long)XNtpClient_serverSecs(client),
                  (long long)XDateTime_currentSecsSinceEpoch(),
                  (long long)((int64_t)XNtpClient_serverSecs(client)
                              - XDateTime_currentSecsSinceEpoch()));
        synced = 1;
    } else if (g_probeSock) {
        REPRO_LOG("[3-NTP] 失败终检: 收包缓冲 %lld 字节（>0 说明应答到达"
                  "但未派发；0 说明报文从未上线或被丢弃）",
                  (long long)XIODevice_bytesAvailable_base(
                      (const XIODevice*)g_probeSock));
    }
    XClassDelete((XClass*)client);
    return synced;
}

/* ========================================================================== */
/*                 阶段 4：回环 mock 服务器对照（对齐 XNtpTest e2e 形态）      */
/* ========================================================================== */

static XUdpSocket* g_mockSock = NULL;
static int g_mockRequests = 0;
static int g_mockReplies = 0;

static void repro_mock_readyRead(XObject* sender, XVarList* args)
{
    XUdpSocket* sock = (XUdpSocket*)sender;
    XHostAddress senderAddr;
    uint16_t senderPort = 0;
    uint8_t wire[48];
    uint8_t reply[48];
    uint64_t nowMs;
    uint32_t secs, frac;

    (void)args;
    if (!sock)
        return;
    XHostAddress_init(&senderAddr);
    {
        int64_t got = XUdpSocket_readDatagram(sock, (char*)wire,
                                              (int64_t)sizeof(wire),
                                              &senderAddr, &senderPort);
        fprintf(stderr, "[XNTPDBG] mock slot: readDatagram=%lld senderPort=%u\n",
                (long long)got, (unsigned)senderPort);
        if (got < 48) {
            XClassDeinit(&senderAddr);
            return;
        }
    }
    g_mockRequests++;
    /* 构造服务器应答：Mode=4 Stratum=5 回显 T1（Transmit→Originate）。 */
    nowMs = (uint64_t)XDateTime_currentWallMSecsSinceEpoch();
    secs = (uint32_t)(nowMs / 1000ULL) + 2208988800u;
    frac = (uint32_t)((nowMs % 1000ULL) * 4294967u);
    memset(reply, 0, sizeof(reply));
    reply[0] = 0x24; /* LI=0 VN=4 Mode=Server */
    reply[1] = 5;
    /* RFC 5905：应答 Originate 字段回显请求的 Transmit（T1），客户端据
       此做防重放比对——回显错字段会被 XNtpPacket_isValidServerReply 拒收。 */
    reply[24] = wire[40]; reply[25] = wire[41];
    reply[26] = wire[42]; reply[27] = wire[43];
    reply[28] = wire[44]; reply[29] = wire[45];
    reply[30] = wire[46]; reply[31] = wire[47];
    reply[32] = (uint8_t)(secs >> 24); reply[33] = (uint8_t)(secs >> 16);
    reply[34] = (uint8_t)(secs >> 8);  reply[35] = (uint8_t)secs;
    reply[36] = (uint8_t)(frac >> 24); reply[37] = (uint8_t)(frac >> 16);
    reply[38] = (uint8_t)(frac >> 8);  reply[39] = (uint8_t)frac;
    reply[40] = (uint8_t)(secs >> 24); reply[41] = (uint8_t)(secs >> 16);
    reply[42] = (uint8_t)(secs >> 8);  reply[43] = (uint8_t)secs;
    reply[44] = (uint8_t)(frac >> 24); reply[45] = (uint8_t)(frac >> 16);
    reply[46] = (uint8_t)(frac >> 8);  reply[47] = (uint8_t)frac;
    if (XUdpSocket_writeDatagram(sock, (const char*)reply, 48,
                                 &senderAddr, senderPort) > 0)
        g_mockReplies++;
    XClassDeinit(&senderAddr);
}

static int repro_stage_loopback(void)
{
    XNtpClient* client;
    uint16_t port;
    int64_t tWait;
    int ok = 0;

    g_mockSock = XUdpSocket_create();
    if (!g_mockSock || !XUdpSocket_bindAny(g_mockSock, 0,
                                           XAbstractSocket_DefaultForPlatform)) {
        REPRO_LOG("[4-LOOP] mock 服务器创建/绑定失败");
        if (g_mockSock) { XUdpSocket_abort(g_mockSock);
            XClassDelete((XClass*)g_mockSock); g_mockSock = NULL; }
        return 0;
    }
    fprintf(stderr, "[XNTPDBG] mock created sock=%p m_d=%p\n",
            (const void*)g_mockSock,
            (const void*)((const XIODevice*)g_mockSock)->m_d);
    port = (uint16_t)XUdpSocket_localPort(g_mockSock);
    XObject_connect_2((XObject*)g_mockSock, XSignal(XIODevice_readyRead_signal),
                      repro_mock_readyRead);
    REPRO_LOG("[4-LOOP] mock 服务器绑定 127.0.0.1:%u", (unsigned)port);

    client = XNtpClient_create();
    if (!client) {
        XUdpSocket_abort(g_mockSock);
        XClassDelete((XClass*)g_mockSock);
        g_mockSock = NULL;
        return 0;
    }
    fprintf(stderr, "[XNTPDBG] client created obj=%p\n", (const void*)client);
    XObject_connect_2((XObject*)client,
                      XSignal(XNtpClient_synchronized_signal), repro_on_synced);
    XObject_connect_2((XObject*)client,
                      XSignal(XNtpClient_syncFailed_signal), repro_on_failed);
    XNtpClient_setServer_2(client, "127.0.0.1", port);
    XNtpClient_setTimeout(client, 2000);
    XNtpClient_setRetries(client, 0);
    g_state = 0;
    if (!XNtpClient_syncNow(client)) {
        REPRO_LOG("[4-LOOP] syncNow 拒绝");
        XClassDelete((XClass*)client);
        XUdpSocket_abort(g_mockSock);
        XClassDelete((XClass*)g_mockSock);
        g_mockSock = NULL;
        return 0;
    }
    tWait = (int64_t)repro_now();
    while (g_state == 0 && repro_now() - tWait < 4000) {
        XCoreApplication_processEvents(XEventLoop_AllEvents);
        XThread_msleep(5);
    }
    REPRO_LOG("[4-LOOP] 结果: state=%d mockRequests=%d mockReplies=%d "
              "offset=%lldms delay=%lldms (%ld ms)",
              (int)XNtpClient_state(client), g_mockRequests, g_mockReplies,
              (long long)XNtpClient_offsetMs(client),
              (long long)XNtpClient_roundTripMs(client),
              repro_now() - tWait);
    ok = (g_state == 1 && g_mockRequests > 0 && g_mockReplies > 0);
    XClassDelete((XClass*)client);
    XUdpSocket_abort(g_mockSock);
    XClassDelete((XClass*)g_mockSock);
    g_mockSock = NULL;
    return ok;
}

int main(int argc, char* argv[])
{
    XCoreApplication* app;
    int dnsCount;
    int rawOk;
    int ntpOk;

    setvbuf(stdout, NULL, _IONBF, 0);
    app = XCoreApplication_create(argc, argv);
    if (!app)
        return 1;

    XPrintf("========== XNtp 最小复现 (%s:%d) ==========\n",
            REPRO_SERVER, REPRO_PORT);
    dnsCount = repro_stage_dns();
    rawOk = repro_stage_raw();

    /* 阶段 3 内建 socket 观察点：先建 XUdpSocket 并绑端口，交给 NTP？
     * XNtpClient 自持 socket（懒创建、私有），观察点改用公开 fd：
     * 为保持最小侵入，这里不持有 XNtpClient 内部 fd，阶段 3 心跳仅
     * 打印客户端公开状态。g_probeSock 保持 NULL。 */
    g_probeSock = NULL;
    ntpOk = repro_stage_client();

    /* 阶段 4：同进程回环 mock（与 XNtpTest e2e 同形态，区分「库行为」与
       「测试环境」）。 */
    {
        int loopOk = repro_stage_loopback();
        XPrintf("\n---------- 阶段结论 ----------\n");
        XPrintf("[1] DNS 解析: %s（%d 个地址）\n",
                dnsCount > 0 ? "OK" : "FAIL", dnsCount);
        XPrintf("[2] 同进程裸 UDP 对照: %s\n", rawOk ? "OK（出网畅通）" : "FAIL");
        XPrintf("[3] XNtpClient 全链路: %s\n",
                ntpOk ? "OK（取到服务器时间）" : "FAIL");
        XPrintf("[4] 回环 mock e2e（XNtpTest 同形态）: %s\n",
                loopOk ? "OK" : "FAIL");
        if (!ntpOk && dnsCount > 0 && rawOk) {
            XPrintf(">>> 断点定位：环境畅通、XNtpClient 超时 —— 缺陷在\n"
                    ">>> XNtpClient/XUdpSocket/平台链路（非防火墙），与\n"
                    ">>> 「UDP 123 出网可能被拦截」兜底文案无关。\n");
        }
        XClassDelete(app);
        return (ntpOk && loopOk) ? 0 : 1;
    }
}

#else /* !XNTP_ON */

#include <stdio.h>
int main(void)
{
    fprintf(stderr, "xntp_min_repro: XNTP 未启用（XNTP_ON=0），无复现对象\n");
    return 1;
}

#endif /* XNTP_ON */
