/* windows_loopback_stall_repro.c —— Windows 环回 IOCP 传输停滞最小复现程序
 *
 * 背景（2026-10-28，XLink v1.3-dev Windows 复测战役）：
 *   XLink 文件传输端到端在 Windows 上"下载 Open 帧已写出但对端收不到、
 *   双端先后静默判线"（XLink.md §14.3）。逐层插桩定位到：37KB 突发
 *   结束后，两侧挂起的 overlapped WSASend/WSARecv 完成整体停滞（秒级
 *   直至 20s+，成批释放），期间 IOCP 轮询存活、完成管线无丢失（序列号
 *   四站配对审计通过）、单端口（assoc/轮询句柄一致）、TCP_NODELAY 无效、
 *   Winsock 目录无第三方 LSP。本程序把该停滞从框架中剥离：裸 socket
 *   亦可复现（20 轮建拆连接 + 8KB 突发 + 小帧乒乓，任一完成 >50ms 记
 *   违例，退出码非 0）。
 *
 * 结论：停滞触发点位于本机内核/驱动栈（Linux epoll 从不复现），XLink
 *   层以 XLINK_HEARTBEAT_TIMEOUT_MS 3000→6000 缓解（覆盖多数停滞窗），
 *   内核侧触发点留待 WinDbg/环境对比继续追查。
 *
 * 构建（VS x64 提示符）：
 *   cl /nologo /W3 /MD /Fe:repro2.exe windows_loopback_stall_repro.c ^
 *      ws2_32.lib mswsock.lib
 * 运行：
 *   repro2.exe [variant] [spin]
 *     variant: 0=ConnectEx+AcceptEx（对齐框架路径）
 *              2=plain connect；3=plain accept；4=both plain
 *     spin:    1=GQCPS(timeout=0)+Sleep(5) 忙轮询（对齐框架事件循环），
 *              0=阻塞 GQCPS(timeout)
 *
 * 实测（2026-10-28，本机）：各变体均出现 "burst recv timeout"——
 *   8KB 突发块发送完成后，对端已挂起的 WSARecv 完成迟迟不到；
 *   而单连接一次性突发的对照程序（无逐块 recv 重挂） completions 全部
 *   0.0ms。停滞与「每块后重挂 recv」的节奏强相关。
 */
#include <winsock2.h>
#include <ws2tcpip.h>
#include <mswsock.h>
#include <stdio.h>
#include <stdlib.h>
#include <windows.h>
#pragma comment(lib, "ws2_32.lib")

typedef struct { OVERLAPPED ov; int tag; } CTX;
static CTX cRcli, cRacc, cW, cAcc, cConn;
static HANDLE iocp;
static char rbuf[8192], wbuf[8192];
static LARGE_INTEGER qpf;
static LPFN_CONNECTEX pConnectEx;
static LPFN_ACCEPTEX pAcceptEx;
static SOCKET g_lst;
static int g_bad;
static int g_variant;
static int g_spinMode;

/* 意外完成包暂存（不得丢弃：丢包即假超时） */
typedef struct { OVERLAPPED* ov; DWORD by; ULONG_PTR k; } PEND;
static PEND g_pend[64];
static int g_npend;

static double msSince(LARGE_INTEGER t0)
{
    LARGE_INTEGER t;
    QueryPerformanceCounter(&t);
    return (double)(t.QuadPart - t0.QuadPart) * 1000.0 / qpf.QuadPart;
}

static void loadExt(SOCKET s)
{
    DWORD n;
    GUID gA = WSAID_ACCEPTEX, gC = WSAID_CONNECTEX;
    WSAIoctl(s, SIO_GET_EXTENSION_FUNCTION_POINTER, &gA, sizeof gA,
             &pAcceptEx, sizeof pAcceptEx, &n, NULL, NULL);
    WSAIoctl(s, SIO_GET_EXTENSION_FUNCTION_POINTER, &gC, sizeof gC,
             &pConnectEx, sizeof pConnectEx, &n, NULL, NULL);
}

static int takePend(OVERLAPPED* want, DWORD* by, ULONG_PTR* k)
{
    int i;
    for (i = 0; i < g_npend; i++) {
        if (g_pend[i].ov == want) {
            *by = g_pend[i].by; *k = g_pend[i].k;
            g_pend[i] = g_pend[--g_npend];
            return 1;
        }
    }
    return 0;
}

static int waitCplt(OVERLAPPED* want, DWORD* by, ULONG_PTR* k, int timeoutMs)
{
    LARGE_INTEGER t0, t;
    if (takePend(want, by, k)) return 1;
    QueryPerformanceCounter(&t0);
    for (;;) {
        DWORD b; ULONG_PTR kk; OVERLAPPED* ov = NULL;
        DWORD to = g_spinMode ? 0 : (DWORD)timeoutMs;
        if (!GetQueuedCompletionStatus(iocp, &b, &kk, &ov, to) || !ov) {
            if (g_spinMode) {
                Sleep(5);
                QueryPerformanceCounter(&t);
                if ((t.QuadPart - t0.QuadPart) * 1000.0 / qpf.QuadPart
                    > timeoutMs) return 0;
                continue;
            }
            return 0;
        }
        if (ov == want) { *by = b; *k = kk; return 1; }
        if (g_npend < 64) {
            g_pend[g_npend].ov = ov; g_pend[g_npend].by = b;
            g_pend[g_npend].k = kk; g_npend++;
        }
    }
}

static void roundTrip(int round)
{
    SOCKET acc = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    SOCKET cli = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    u_long nb = 1;
    int nd = 1;
    WSABUF b;
    DWORD flags = 0, recvd = 0, nrec = 0;
    int gotPlainAcc = (g_variant == 2 || g_variant == 4);
    int i;

    ioctlsocket(cli, FIONBIO, &nb);
    setsockopt(cli, IPPROTO_TCP, TCP_NODELAY, (char*)&nd, sizeof(nd));
    setsockopt(acc, IPPROTO_TCP, TCP_NODELAY, (char*)&nd, sizeof(nd));
    {
        struct sockaddr_in any;
        any.sin_family = AF_INET;
        any.sin_addr.s_addr = INADDR_ANY;
        any.sin_port = 0;
        bind(cli, (struct sockaddr*)&any, sizeof any);
    }
    CreateIoCompletionPort((HANDLE)cli, iocp, 1, 0);
    CreateIoCompletionPort((HANDLE)acc, iocp, 2, 0);

    if (gotPlainAcc) {
        closesocket(acc);
        acc = accept(g_lst, NULL, NULL);
        if (acc == INVALID_SOCKET) { printf("r%d accept fail\n", round); exit(1); }
        ioctlsocket(acc, FIONBIO, &nb);
        setsockopt(acc, IPPROTO_TCP, TCP_NODELAY, (char*)&nd, sizeof(nd));
        CreateIoCompletionPort((HANDLE)acc, iocp, 2, 0);
    } else {
        memset(&cAcc, 0, sizeof cAcc);
        if (!pAcceptEx(g_lst, acc, wbuf, 0,
                       sizeof(struct sockaddr_in6) + 16,
                       sizeof(struct sockaddr_in6) + 16,
                       &nrec, (OVERLAPPED*)&cAcc)
            && WSAGetLastError() != WSA_IO_PENDING) {
            printf("AcceptEx fail %d\n", WSAGetLastError());
            exit(1);
        }
    }

    {
        struct sockaddr_in dst;
        int dl = sizeof dst;
        getsockname(g_lst, (struct sockaddr*)&dst, &dl);
        if (g_variant == 2 || g_variant == 4) {
            if (connect(cli, (struct sockaddr*)&dst, sizeof dst)
                && WSAGetLastError() != WSAEWOULDBLOCK) {
                printf("connect fail %d\n", WSAGetLastError());
                exit(1);
            }
        } else {
            memset(&cConn, 0, sizeof cConn);
            if (!pConnectEx(cli, (struct sockaddr*)&dst, sizeof dst,
                            NULL, 0, NULL, (OVERLAPPED*)&cConn)
                && WSAGetLastError() != WSA_IO_PENDING) {
                printf("ConnectEx fail %d\n", WSAGetLastError());
                exit(1);
            }
        }
    }

    /* 等连接/接受完成（plain 路径无 accept 完成包） */
    {
        DWORD by; ULONG_PTR k;
        int needAcc = !gotPlainAcc;
        int needConn = !(g_variant == 2 || g_variant == 4);
        while (needConn || needAcc) {
            OVERLAPPED* ov = NULL;
            if (!GetQueuedCompletionStatus(iocp, &by, &k, &ov, 2000) || !ov) {
                printf("r%d setup timeout\n", round);
                exit(1);
            }
            if (ov == (OVERLAPPED*)&cConn) needConn = 0;
            else if (ov == (OVERLAPPED*)&cAcc) needAcc = 0;
        }
        if (!gotPlainAcc)
            setsockopt(acc, SOL_SOCKET, SO_UPDATE_ACCEPT_CONTEXT, NULL, 0);
    }

    memset(&cRcli, 0, sizeof cRcli);
    b.buf = rbuf; b.len = sizeof rbuf;
    WSARecv(cli, &b, 1, &recvd, &flags, (OVERLAPPED*)&cRcli, NULL);
    memset(&cRacc, 0, sizeof cRacc);
    b.buf = rbuf; b.len = sizeof rbuf;
    WSARecv(acc, &b, 1, &recvd, &flags, (OVERLAPPED*)&cRacc, NULL);

    /* 37KB 突发（5×8KB），逐块等对端 recv 完成后重挂——对齐应用节奏 */
    for (i = 0; i < 5; i++) {
        DWORD by; ULONG_PTR k;
        LARGE_INTEGER s;
        memset(&cW, 0, sizeof cW);
        b.buf = wbuf; b.len = 8192;
        QueryPerformanceCounter(&s);
        WSASend(cli, &b, 1, NULL, 0, (OVERLAPPED*)&cW, NULL);
        if (!waitCplt((OVERLAPPED*)&cW, &by, &k, 3000)) {
            printf("r%d burst send timeout\n", round); exit(1);
        }
        if (msSince(s) > 50) {
            printf("r%d burst %d send slow %.0fms\n", round, i, msSince(s));
            g_bad++;
        }
        if (!waitCplt((OVERLAPPED*)&cRacc, &by, &k, 3000)) {
            printf("r%d burst recv timeout i=%d\n", round, i);
            g_bad++;
            goto out;
        }
        flags = 0;
        b.buf = rbuf; b.len = sizeof rbuf;
        WSARecv(acc, &b, 1, &recvd, &flags, (OVERLAPPED*)&cRacc, NULL);
    }

    /* 小帧乒乓 ×20 */
    for (i = 0; i < 20; i++) {
        DWORD by; ULONG_PTR k;
        LARGE_INTEGER s;
        memset(&cW, 0, sizeof cW);
        b.buf = wbuf; b.len = 12;
        QueryPerformanceCounter(&s);
        WSASend(cli, &b, 1, NULL, 0, (OVERLAPPED*)&cW, NULL);
        if (!waitCplt((OVERLAPPED*)&cW, &by, &k, 5000)) {
            printf("r%d ping %d TIMEOUT\n", round, i);
            g_bad++;
            goto out;
        }
        if (msSince(s) > 50) {
            printf("r%d ping %d slow %.0fms\n", round, i, msSince(s));
            g_bad++;
        }
        memset(&cW, 0, sizeof cW);
        b.buf = wbuf; b.len = 12;
        WSASend(acc, &b, 1, NULL, 0, (OVERLAPPED*)&cW, NULL);
        if (!waitCplt((OVERLAPPED*)&cRcli, &by, &k, 5000)) {
            printf("r%d pong timeout\n", round);
            exit(1);
        }
        flags = 0;
        b.buf = rbuf; b.len = sizeof rbuf;
        WSARecv(cli, &b, 1, &recvd, &flags, (OVERLAPPED*)&cRcli, NULL);
    }

out:
    CancelIoEx((HANDLE)cli, NULL);
    CancelIoEx((HANDLE)acc, NULL);
    closesocket(cli);
    closesocket(acc);
    for (;;) {
        DWORD by; ULONG_PTR k; OVERLAPPED* ov = NULL;
        if (!GetQueuedCompletionStatus(iocp, &by, &k, &ov, 30) || !ov) break;
    }
}

int main(int argc, char** argv)
{
    WSADATA wsa;
    struct sockaddr_in ad;
    int len = sizeof ad;
    u_long nb = 1;
    int r;

    g_variant = (argc > 1) ? atoi(argv[1]) : 0;
    g_spinMode = (argc > 2) ? atoi(argv[2]) : 0;
    WSAStartup(MAKEWORD(2, 2), &wsa);
    QueryPerformanceFrequency(&qpf);
    iocp = CreateIoCompletionPort(INVALID_HANDLE_VALUE, NULL, 0, 0);
    g_lst = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    ioctlsocket(g_lst, FIONBIO, &nb);
    loadExt(g_lst);
    ad.sin_family = AF_INET;
    ad.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    ad.sin_port = 0;
    bind(g_lst, (struct sockaddr*)&ad, sizeof ad);
    getsockname(g_lst, (struct sockaddr*)&ad, &len);
    listen(g_lst, 4);
    CreateIoCompletionPort((HANDLE)g_lst, iocp, 3, 0);
    for (r = 0; r < 20; r++) roundTrip(r);
    printf("variant=%d spin=%d done: %d slow/lost completions\n",
           g_variant, g_spinMode, g_bad);
    return g_bad ? 1 : 0;
}
