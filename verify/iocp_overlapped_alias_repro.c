/**
 * @file iocp_overlapped_alias_repro.c
 * @brief IOCP 读完成丢失的机制级复现器（独立于框架，纯 Win32 + Winsock）。
 *
 * 目的：验证「同一 OVERLAPPED 在上一笔操作未被完成包回收时又被投递」
 * 是否会造成完成包丢失 / 读通道永久停摆。这是
 * `docs/Windows_IOCP读完成丢失_交接_20261104.md` 中「完成包丢失」
 * 现象的最小机制解释。
 *
 * 三个用例（各自独立 socket 对 + 独立 IOCP）：
 *   A 别名重投：同一 OVERLAPPED 连续投两次 WSARecv（上一笔仍在飞）
 *   B 取消即重投：CancelIoEx 后立刻用同一 OVERLAPPED 重投
 *   C 对照（正确用法）：只在上一笔完成包回收后才重投
 *
 * 判据（每一项都由两个独立官方手段交叉验证）：
 *   - 完成包计数（GetQueuedCompletionStatus）
 *   - 内核待读字节（FIONREAD）
 *
 * 编译：
 *   cl /nologo /W3 /MD iocp_overlapped_alias_repro.c /link ws2_32.lib
 */

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <stdio.h>
#include <string.h>

#pragma comment(lib, "ws2_32.lib")

#define RECV_BUF_SIZE 4096

typedef struct Probe {
    SOCKET client;
    SOCKET server;
} Probe;

static SOCKET g_listen = INVALID_SOCKET;

/* 建一对本机环回 TCP 连接：返回 server 端（用于 WSARecv），client 端用于 send。 */
static int probe_open(Probe* p)
{
    struct sockaddr_in addr;
    int addrLen = sizeof(addr);
    SOCKET listener;
    u_long nb = 1;

    p->client = INVALID_SOCKET;
    p->server = INVALID_SOCKET;

    listener = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (listener == INVALID_SOCKET) return -1;

    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = 0;
    if (bind(listener, (struct sockaddr*)&addr, sizeof(addr)) != 0) return -1;
    if (listen(listener, 1) != 0) return -1;
    if (getsockname(listener, (struct sockaddr*)&addr, &addrLen) != 0) return -1;

    p->client = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (connect(p->client, (struct sockaddr*)&addr, sizeof(addr)) != 0) return -1;

    p->server = accept(listener, NULL, NULL);
    closesocket(listener);
    if (p->server == INVALID_SOCKET) return -1;

    /* 与服务端后端一致：套接字非阻塞 + 归属 IOCP。 */
    ioctlsocket(p->server, FIONBIO, &nb);
    ioctlsocket(p->client, FIONBIO, &nb);
    return 0;
}

static void probe_close(Probe* p)
{
    if (p->server != INVALID_SOCKET) closesocket(p->server);
    if (p->client != INVALID_SOCKET) closesocket(p->client);
    p->server = p->client = INVALID_SOCKET;
}

/* 从 client 发 n 字节（非阻塞 + 轮询写完，避免阻塞主线程观察窗口）。 */
static int probe_send(Probe* p, int n)
{
    static char payload[8192];
    int sent = 0;
    memset(payload, 'x', sizeof(payload));
    while (sent < n) {
        int chunk = n - sent;
        int r;
        if (chunk > (int)sizeof(payload)) chunk = (int)sizeof(payload);
        r = send(p->client, payload, chunk, 0);
        if (r > 0) { sent += r; continue; }
        if (r == SOCKET_ERROR && WSAGetLastError() == WSAEWOULDBLOCK) {
            Sleep(1);
            continue;
        }
        return sent;
    }
    return sent;
}

/* 收完成包，最多等 waitMs 毫秒；返回完成包个数，bytes 累加，errors 记录失败笔数。 */
static int probe_drain(HANDLE iocp, int waitMs, DWORD* bytes, int* errors,
                       DWORD* firstError, DWORD* aborted)
{
    ULONGLONG deadline = GetTickCount64() + (ULONGLONG)waitMs;
    int count = 0;
    *bytes = 0;
    *errors = 0;
    *firstError = 0;
    *aborted = 0;

    for (;;) {
        DWORD got = 0;
        ULONG_PTR key = 0;
        LPOVERLAPPED ov = NULL;
        ULONGLONG now = GetTickCount64();
        DWORD slice;
        BOOL ok;

        if (now >= deadline) break;
        slice = (DWORD)(deadline - now);
        ok = GetQueuedCompletionStatus(iocp, &got, &key, &ov, slice);
        if (!ov) break;                       /* 超时或无效：无完成包 */
        ++count;
        *bytes += got;
        if (!ok) {
            DWORD err = GetLastError();
            ++(*errors);
            if (err == ERROR_OPERATION_ABORTED) ++(*aborted);
            if (*firstError == 0) *firstError = err;
        }
    }
    return count;
}

static u_long probe_kernelPending(SOCKET s)
{
    u_long n = 0;
    if (ioctlsocket(s, FIONREAD, &n) != 0) return 0;
    return n;
}

/* ------------------------------------------------------------------ */
/* 用例 A：同一 OVERLAPPED 连续投两笔 WSARecv（上一笔仍在飞）            */
/* ------------------------------------------------------------------ */
static void case_alias(void)
{
    Probe p;
    HANDLE iocp;
    OVERLAPPED ov;
    char buf[RECV_BUF_SIZE];
    WSABUF wb;
    DWORD flags = 0, got = 0, bytes = 0, firstError = 0, aborted = 0;
    int r1, r2, e1, e2, errors = 0, packets;

    printf("\n=== A 别名重投（同一 OVERLAPPED 在飞时再投一次） ===\n");
    if (probe_open(&p) != 0) { printf("建链失败\n"); return; }
    iocp = CreateIoCompletionPort(INVALID_HANDLE_VALUE, NULL, 0, 0);
    if (!CreateIoCompletionPort((HANDLE)p.server, iocp, 1, 0)) {
        printf("关联 IOCP 失败\n"); probe_close(&p); return;
    }

    memset(&ov, 0, sizeof(ov));
    memset(buf, 0, sizeof(buf));
    wb.buf = buf;
    wb.len = sizeof(buf);

    r1 = WSARecv(p.server, &wb, 1, NULL, &flags, &ov, NULL);
    e1 = (r1 == 0) ? 0 : (int)WSAGetLastError();
    /* 关键动作：上一笔仍在飞，用**同一个** OVERLAPPED 再投一笔。 */
    r2 = WSARecv(p.server, &wb, 1, NULL, &flags, &ov, NULL);
    e2 = (r2 == 0) ? 0 : (int)WSAGetLastError();
    printf("  投递1: ret=%d err=%d   投递2(同一 OVERLAPPED): ret=%d err=%d\n",
           r1, e1, r2, e2);

    probe_send(&p, 1000);
    packets = probe_drain(iocp, 2000, &bytes, &errors, &firstError, &aborted);
    printf("  第一批 1000 字节 → 完成包=%d 字节=%lu 失败笔=%d err=%lu 内核待读=%lu\n",
           packets, (unsigned long)bytes, errors,
           (unsigned long)firstError, (unsigned long)probe_kernelPending(p.server));

    /* 第二批：若第一轮丢了完成包，读通道此刻应当已经停摆。 */
    probe_send(&p, 500);
    packets = probe_drain(iocp, 1500, &bytes, &errors, &firstError, &aborted);
    printf("  第二批  500 字节 → 完成包=%d 字节=%lu 内核待读=%lu\n",
           packets, (unsigned long)bytes,
           (unsigned long)probe_kernelPending(p.server));
    printf("  判定：%s\n",
           (bytes > 0 || probe_kernelPending(p.server) == 0)
               ? "两批均被取走（未复现停摆）"
               : "完成包丢失：数据在内核但读不再完成（读通道停摆）");

    probe_close(&p);
    CloseHandle(iocp);
}

/* ------------------------------------------------------------------ */
/* 用例 B：CancelIoEx 之后立刻用同一 OVERLAPPED 重投                    */
/* ------------------------------------------------------------------ */
static void case_cancel_rearm(void)
{
    Probe p;
    HANDLE iocp;
    OVERLAPPED ov;
    char buf[RECV_BUF_SIZE];
    WSABUF wb;
    DWORD flags = 0, bytes = 0, firstError = 0, aborted = 0;
    int errors = 0, packets;

    printf("\n=== B 取消即重投（CancelIoEx 后立刻复用同一 OVERLAPPED） ===\n");
    if (probe_open(&p) != 0) { printf("建链失败\n"); return; }
    iocp = CreateIoCompletionPort(INVALID_HANDLE_VALUE, NULL, 0, 0);
    if (!CreateIoCompletionPort((HANDLE)p.server, iocp, 1, 0)) {
        printf("关联 IOCP 失败\n"); probe_close(&p); return;
    }

    memset(&ov, 0, sizeof(ov));
    memset(buf, 0, sizeof(buf));
    wb.buf = buf;
    wb.len = sizeof(buf);

    (void)WSARecv(p.server, &wb, 1, NULL, &flags, &ov, NULL);
    /* 模拟看门狗：取消挂起的读，然后**立即**在同一 OVERLAPPED 上重挂。 */
    CancelIoEx((HANDLE)p.server, &ov);
    (void)WSARecv(p.server, &wb, 1, NULL, &flags, &ov, NULL);

    probe_send(&p, 1000);
    packets = probe_drain(iocp, 2000, &bytes, &errors, &firstError, &aborted);
    printf("  第一批 1000 字节 → 完成包=%d 字节=%lu 失败笔=%d(ABORTED=%lu) "
           "内核待读=%lu\n",
           packets, (unsigned long)bytes, errors,
           (unsigned long)aborted, (unsigned long)probe_kernelPending(p.server));

    probe_send(&p, 500);
    packets = probe_drain(iocp, 1500, &bytes, &errors, &firstError, &aborted);
    printf("  第二批  500 字节 → 完成包=%d 字节=%lu 内核待读=%lu\n",
           packets, (unsigned long)bytes,
           (unsigned long)probe_kernelPending(p.server));
    printf("  判定：%s\n",
           (bytes > 0 || probe_kernelPending(p.server) == 0)
               ? "两批均被取走（未复现停摆）"
               : "完成包丢失：数据在内核但读不再完成（读通道停摆）");

    probe_close(&p);
    CloseHandle(iocp);
}

/* ------------------------------------------------------------------ */
/* 用例 C：对照 —— 上一笔完成包回收之后才重投（正确用法）               */
/* ------------------------------------------------------------------ */
static void case_sequential(void)
{
    Probe p;
    HANDLE iocp;
    OVERLAPPED ovA, ovB;
    char bufA[RECV_BUF_SIZE], bufB[RECV_BUF_SIZE];
    WSABUF wbA, wbB;
    DWORD flags = 0, bytes = 0, firstError = 0, aborted = 0;
    int errors = 0, packets;

    printf("\n=== C 对照（只在完成包回收后才重投） ===\n");
    if (probe_open(&p) != 0) { printf("建链失败\n"); return; }
    iocp = CreateIoCompletionPort(INVALID_HANDLE_VALUE, NULL, 0, 0);
    if (!CreateIoCompletionPort((HANDLE)p.server, iocp, 1, 0)) {
        printf("关联 IOCP 失败\n"); probe_close(&p); return;
    }

    memset(&ovA, 0, sizeof(ovA));
    memset(&ovB, 0, sizeof(ovB));
    wbA.buf = bufA; wbA.len = sizeof(bufA);
    wbB.buf = bufB; wbB.len = sizeof(bufB);

    (void)WSARecv(p.server, &wbA, 1, NULL, &flags, &ovA, NULL);
    probe_send(&p, 1000);
    packets = probe_drain(iocp, 2000, &bytes, &errors, &firstError, &aborted);
    printf("  第一批 1000 字节 → 完成包=%d 字节=%lu\n",
           packets, (unsigned long)bytes);

    /* 上一笔的完成包已经出队，此时才用另一份 OVERLAPPED 重投。 */
    (void)WSARecv(p.server, &wbB, 1, NULL, &flags, &ovB, NULL);
    probe_send(&p, 500);
    packets = probe_drain(iocp, 1500, &bytes, &errors, &firstError, &aborted);
    printf("  第二批  500 字节 → 完成包=%d 字节=%lu 内核待读=%lu\n",
           packets, (unsigned long)bytes,
           (unsigned long)probe_kernelPending(p.server));
    printf("  判定：%s\n",
           (bytes > 0 && probe_kernelPending(p.server) == 0)
               ? "两批均被取走（预期行为）"
               : "异常：对照路径也不通（说明复现器本身有问题）");

    probe_close(&p);
    CloseHandle(iocp);
}

int main(void)
{
    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) {
        printf("WSAStartup 失败\n");
        return 2;
    }

    printf("IOCP OVERLAPPED 别名复现器 —— 本机环回\n");
    case_alias();
    case_cancel_rearm();
    case_sequential();

    WSACleanup();
    return 0;
}
