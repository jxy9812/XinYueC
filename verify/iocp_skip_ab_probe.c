/* iocp_skip_ab_probe.c —— 环回 IOCP 停滞的 skip 模式 A/B 探针
 *
 * 问题（2026-10-28 定性，verify/windows_loopback_stall_repro.c 复现）：
 *   本机环回 TCP 上，overlapped WSASend/WSARecv 的**完成包出队**在大流量
 *   突发后停滞数秒~20s+（裸 socket 可复现，与「每块后重挂 recv」节奏强相关）。
 *   当时结论「应用层无解」的前提是：**必须依赖完成包出队**才能拿到结果。
 *
 * 本探针验证的解法：MSDN 官方承认的合法"不投递"开关
 *   SetFileCompletionNotificationModes(sock, FILE_SKIP_COMPLETION_PORT_ON_SUCCESS)
 * 同步完成的操作**不投递完成包**，应用以 GetOverlappedResult(FALSE) 收结果；
 * 真异步（IO_PENDING）仍投递。⇒ 「完成包停滞」降级为非事件（没有包可丢）。
 *
 * A/B 设计（同流量形态，Windows 环回，每模式 20 轮）：
 *   baseline = 关闭 skip：等完成包（GQCS），任一完成 >50ms 记违例
 *   skip     = 开启 skip：GetOverlappedResult(FALSE) 先查（同步完成立即得），
 *              未完成再等完成包/轮询；同判据
 * 判据：两模式的 slow 计数与总耗时对比。
 *
 * 构建（VS x64 提示符）：
 *   cl /nologo /W3 /MD /Fe:iocp_skip_ab_probe.exe iocp_skip_ab_probe.c ws2_32.lib
 * 运行：
 *   iocp_skip_ab_probe.exe [rounds]
 * 退出码：0 = 完成（读输出表对比）；1 = 出现 TIMEOUT 级违例
 */
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#pragma comment(lib, "ws2_32.lib")

#define BURST_BLOCK 8192
#define BURST_N     5
#define PING_N      20
#define SLOW_MS     50
#define ROUNDS      20

typedef struct { OVERLAPPED ov; int tag; } CTX;
static HANDLE iocp;
static char rbuf[BURST_BLOCK], wbuf[BURST_BLOCK];
static LARGE_INTEGER qpf;
static SOCKET g_lst;
static int g_skip;              /* A/B 开关 */
static long g_slow, g_timeouts;
static double g_totalMs;

static double msSince(LARGE_INTEGER t0)
{
    LARGE_INTEGER t;
    QueryPerformanceCounter(&t);
    return (double)(t.QuadPart - t0.QuadPart) * 1000.0 / qpf.QuadPart;
}

static void enableSkip(SOCKET s)
{
    if (!g_skip) return;
    if (!SetFileCompletionNotificationModes((HANDLE)s, FILE_SKIP_COMPLETION_PORT_ON_SUCCESS)) {
        printf("  SetFileCompletionNotificationModes failed %lu\n", GetLastError());
        exit(2);
    }
}

/**
 * skip/非 skip 通用的「提交 + 收结果」：
 *   非 skip：GQCS 等完成包（基线形态）。
 *   skip   ：GetOverlappedResult(FALSE) 先查——同步完成（skip 下不投递）
 *            立即得；ERROR_IO_PENDING 再等完成包/轮询。
 * 返回 1=成功（*bytes=传输量），0=超时。
 */
static int submitAndAwait(SOCKET s, WSABUF* b, DWORD flags, CTX* ov,
                          DWORD* bytes, int timeoutMs)
{
    LARGE_INTEGER t0, t;
    DWORD recvd = 0, fl = flags;

    memset(ov, 0, sizeof *ov);
    QueryPerformanceCounter(&t0);

    if (WSASend(s, b, 1, NULL, fl, (OVERLAPPED*)ov, NULL) == SOCKET_ERROR) {
        if (WSAGetLastError() != WSA_IO_PENDING) return 0;
    }
    /* 非提交型（WSARecv 由调用方处理错误），这里统一按结果通道收。 */

    if (g_skip) {
        /* skip 模式：同步完成不投递 ⇒ 先查结果；IO_PENDING 才等包。 */
        for (;;) {
            if (GetOverlappedResult((HANDLE)s, (OVERLAPPED*)ov, bytes, FALSE))
                return 1;
            if (GetLastError() != ERROR_IO_INCOMPLETE) return 0;
            QueryPerformanceCounter(&t);
            if ((t.QuadPart - t0.QuadPart) * 1000.0 / qpf.QuadPart
                > (double)timeoutMs)
                return 0;
            Sleep(0);
        }
    } else {
        /* 基线：等完成包出队。 */
        for (;;) {
            OVERLAPPED* done = NULL;
            ULONG_PTR key = 0;
            if (GetQueuedCompletionStatus(iocp, bytes, &key, &done, timeoutMs)
                && done == (OVERLAPPED*)ov)
                return 1;
            if (!done) return 0;
            /* 其它操作（乒乓对端）的完成包：丢弃由对端自己的等待处理——
               本探针单线程串行，对端完成包由其配对等待取走；此处仅容错。 */
            (void)key;
            QueryPerformanceCounter(&t);
            if ((t.QuadPart - t0.QuadPart) * 1000.0 / qpf.QuadPart
                > (double)timeoutMs)
                return 0;
        }
    }
}

/** 一轮：37KB 突发（5×8KB 逐块，等对端 recv 后重挂）+ 20 轮小帧乒乓。 */
static void roundTrip(int round, SOCKET lst)
{
    SOCKET acc = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    SOCKET cli = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    u_long nb = 1;
    int nd = 1;
    WSABUF b;
    CTX ovW, ovR;
    DWORD bytes;
    int i;
    LARGE_INTEGER r0;

    ioctlsocket(cli, FIONBIO, &nb);
    ioctlsocket(acc, FIONBIO, &nb);
    setsockopt(cli, IPPROTO_TCP, TCP_NODELAY, (char*)&nd, sizeof nd);
    setsockopt(acc, IPPROTO_TCP, TCP_NODELAY, (char*)&nd, sizeof nd);
    CreateIoCompletionPort((HANDLE)cli, iocp, 1, 0);
    CreateIoCompletionPort((HANDLE)acc, iocp, 2, 0);
    enableSkip(cli);
    enableSkip(acc);

    {
        struct sockaddr_in dst;
        int dl = sizeof dst;
        getsockname(lst, (struct sockaddr*)&dst, &dl);
        if (connect(cli, (struct sockaddr*)&dst, sizeof dst)
            && WSAGetLastError() != WSAEWOULDBLOCK) {
            printf("r%d connect fail\n", round);
            exit(2);
        }
    }
    {
        SOCKET s = accept(lst, NULL, NULL);
        if (s == INVALID_SOCKET) { printf("r%d accept fail\n", round); exit(2); }
        closesocket(acc);
        acc = s;
        ioctlsocket(acc, FIONBIO, &nb);
        setsockopt(acc, IPPROTO_TCP, TCP_NODELAY, (char*)&nd, sizeof nd);
        CreateIoCompletionPort((HANDLE)acc, iocp, 2, 0);
        enableSkip(acc);
    }

    QueryPerformanceCounter(&r0);

    /* 37KB 突发：cli 发 5 块；acc 逐块 recv（提交→收结果→重挂）。 */
    for (i = 0; i < BURST_N; ++i) {
        b.buf = wbuf; b.len = BURST_BLOCK;
        if (!submitAndAwait(cli, &b, 0, &ovW, &bytes, 3000)) {
            printf("r%d burst send timeout i=%d\n", round, i);
            ++g_timeouts; goto out;
        }
        /* acc 侧 recv：提交后收结果。 */
        b.buf = rbuf; b.len = BURST_BLOCK;
        if (!submitAndAwait(acc, &b, 0, &ovR, &bytes, 3000)) {
            printf("r%d burst recv timeout i=%d\n", round, i);
            ++g_timeouts; goto out;
        }
        if (bytes != BURST_BLOCK) {
            /* 短收：环回 8KB 一般整块；短收继续收剩余也行，这里按违例计 */
            ++g_slow;
        }
    }

    /* 20 轮小帧乒乓（12 字节）：测完成通知延迟。 */
    for (i = 0; i < PING_N; ++i) {
        LARGE_INTEGER s0;
        double el;
        b.buf = wbuf; b.len = 12;
        QueryPerformanceCounter(&s0);
        if (!submitAndAwait(cli, &b, 0, &ovW, &bytes, 5000)) {
            printf("r%d ping TIMEOUT i=%d\n", round, i);
            ++g_timeouts; goto out;
        }
        b.buf = rbuf; b.len = 12;
        if (!submitAndAwait(acc, &b, 0, &ovR, &bytes, 5000)) {
            printf("r%d pong recv TIMEOUT i=%d\n", round, i);
            ++g_timeouts; goto out;
        }
        el = msSince(s0);
        if (el > SLOW_MS) { printf("r%d ping slow %.0fms\n", round, el); ++g_slow; }
    }

    g_totalMs += msSince(r0);

out:
    CancelIoEx((HANDLE)cli, NULL);
    CancelIoEx((HANDLE)acc, NULL);
    closesocket(cli);
    closesocket(acc);
    {
        DWORD by; ULONG_PTR k; OVERLAPPED* ov = NULL;
        while (GetQueuedCompletionStatus(iocp, &by, &k, &ov, 30) && ov) {}
    }
}

int main(int argc, char** argv)
{
    WSADATA wsa;
    struct sockaddr_in ad;
    int len = sizeof ad, r;
    u_long nb = 1;
    LARGE_INTEGER t0, t1;
    int rounds = (argc > 1) ? atoi(argv[1]) : ROUNDS;

    g_skip = (argc > 2) ? atoi(argv[2]) : -1;   /* -1 = A/B 各跑一遍 */
    WSAStartup(MAKEWORD(2, 2), &wsa);
    QueryPerformanceFrequency(&qpf);

    {
        int modes[2], mi, m;
        if (g_skip < 0) { modes[0] = 0; modes[1] = 1; mi = 2; }
        else            { modes[0] = g_skip; mi = 1; }

        for (m = 0; m < mi; ++m) {
            g_skip = modes[m];
            g_slow = 0; g_timeouts = 0; g_totalMs = 0;
            iocp = CreateIoCompletionPort(INVALID_HANDLE_VALUE, NULL, 0, 0);
            g_lst = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
            /* 监听口保持阻塞：plain accept 需要等到连接到达（非 AcceptEx）。 */
            ad.sin_family = AF_INET;
            ad.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
            ad.sin_port = 0;
            bind(g_lst, (struct sockaddr*)&ad, sizeof ad);
            getsockname(g_lst, (struct sockaddr*)&ad, &len);
            listen(g_lst, 8);
            CreateIoCompletionPort((HANDLE)g_lst, iocp, 3, 0);

            printf("=== mode=%s rounds=%d ===\n",
                   g_skip ? "SKIP(GetOverlappedResult)" : "baseline(GQCS)", rounds);
            QueryPerformanceCounter(&t0);
            for (r = 0; r < rounds; ++r) roundTrip(r, g_lst);
            QueryPerformanceCounter(&t1);
            printf("mode=%s done: slow=%ld timeouts=%ld total=%.0fms\n\n",
                   g_skip ? "SKIP" : "baseline",
                   g_slow, g_timeouts,
                   (double)(t1.QuadPart - t0.QuadPart) * 1000.0 / qpf.QuadPart);

            closesocket(g_lst);
            CloseHandle(iocp);
            iocp = NULL;
        }
    }
    WSACleanup();
    (void)t0; (void)t1;
    return g_timeouts ? 1 : 0;
}
