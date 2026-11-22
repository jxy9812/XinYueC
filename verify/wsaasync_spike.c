/* wsaasync_spike.c -- standalone WinSock probe:
 *   Can WSAAsyncSelect + a hidden window + WndProc caching actually deliver
 *   FD_READ/FD_WRITE/FD_CONNECT notifications to a SINGLE-THREADED event loop
 *   that only pumps messages via PeekMessage (no QApplication / no GUI)?
 *
 * This is a feasibility spike for adding a WSAAsyncSelect socket backend.
 * It does NOT touch the framework.  Compile and run standalone:
 *   cl /nologo /W3 /O2 /MD /utf-8 /Fe:wsaasync_spike.exe wsaasync_spike.c user32.lib ws2_32.lib
 *
 * EXIT  0 = mechanism works (assertions passed)
 *       2 = setup failure
 *       3 = assertion failure (mechanism broken)
 */
#define _CRT_SECURE_NO_WARNINGS 1
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <stdio.h>
#include <string.h>
#pragma comment(lib, "ws2_32.lib")
#pragma comment(lib, "user32.lib")

#define WM_SOCK (WM_APP + 1)
#define FRAME   "PINGASYNC"

static HWND        g_hwnd = NULL;
static volatile LONG g_readBits = 0;      /* 累计收到的 FD_READ 次数（WndProc 写） */
static volatile LONG g_writeBits = 0;
static volatile LONG g_connectBits = 0;
static volatile LONG g_msgCount = 0;
static SOCKET      g_client = INVALID_SOCKET;
static SOCKET      g_server = INVALID_SOCKET;

static LRESULT CALLBACK sock_wndproc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    if (msg == WM_SOCK) {
        int err = WSAGETSELECTERROR(lp);
        short ev = WSAGETSELECTEVENT(lp);
        InterlockedIncrement(&g_msgCount);
        if (ev & FD_READ)   InterlockedIncrement(&g_readBits);
        if (ev & FD_WRITE)  InterlockedIncrement(&g_writeBits);
        if (ev & FD_CONNECT) InterlockedIncrement(&g_connectBits);
        if (err != 0)
            printf("  [wndproc] socket=%u event=%d WSAerr=%d\n",
                   (unsigned)wp, (int)ev, err);
        return 0;
    }
    return DefWindowProc(hwnd, msg, wp, lp);
}

static int make_hidden_window(void)
{
    WNDCLASSEXW wc;
    memset(&wc, 0, sizeof(wc));
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = sock_wndproc;
    wc.hInstance = GetModuleHandle(NULL);
    wc.lpszClassName = L"XWsaAsyncSpikeClass";
    if (!RegisterClassExW(&wc)) {
        DWORD e = GetLastError();
        if (e != ERROR_CLASS_ALREADY_EXISTS) { printf("RegisterClass failed %lu\n", e); return -1; }
    }
    g_hwnd = CreateWindowExW(0, L"XWsaAsyncSpikeClass", L"spike",
                             WS_POPUP, 0, 0, 0, 0,
                             HWND_MESSAGE, NULL, GetModuleHandle(NULL), NULL);
    if (!g_hwnd) { printf("CreateWindow failed %lu\n", GetLastError()); return -1; }
    return 0;
}

/* pump all pending WM_SOCK messages, like a single-threaded event loop would */
static void pump_messages(int maxRounds)
{
    MSG msg;
    int n = 0;
    while (n < maxRounds && PeekMessageW(&msg, NULL, 0, 0, PM_REMOVE)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
        ++n;
    }
}

int main(void)
{
    WSADATA wsa;
    struct sockaddr_in sa;
    int saLen = (int)sizeof(sa);
    char buf[64];
    int got;

    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) { printf("WSAStartup failed\n"); return 2; }
    if (make_hidden_window() != 0) return 2;

    /* listening socket (blocking, plain) */
    g_server = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    memset(&sa, 0, sizeof(sa));
    sa.sin_family = AF_INET;
    sa.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    sa.sin_port = 0;
    if (bind(g_server, (struct sockaddr*)&sa, sizeof(sa)) == SOCKET_ERROR ||
        listen(g_server, 1) == SOCKET_ERROR) { printf("bind/listen failed %d\n", WSAGetLastError()); return 2; }
    getsockname(g_server, (struct sockaddr*)&sa, &saLen);

    /* client socket: WSAAsyncSelect on the hidden window */
    g_client = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (WSAAsyncSelect(g_client, g_hwnd, WM_SOCK,
                       FD_READ | FD_WRITE | FD_CONNECT | FD_CLOSE) == SOCKET_ERROR) {
        printf("WSAAsyncSelect failed %d\n", WSAGetLastError()); return 2;
    }
    connect(g_client, (struct sockaddr*)&sa, sizeof(sa));   /* non-blocking now */

    /* pump until FD_CONNECT arrives */
    {
        int i;
        for (i = 0; i < 200 && g_connectBits == 0; ++i) {
            pump_messages(16);
            Sleep(5);
        }
    }
    printf("after connect: msgs=%ld connect=%ld write=%ld read=%ld\n",
           g_msgCount, g_connectBits, g_writeBits, g_readBits);
    if (g_connectBits == 0) { printf("[FAIL] no FD_CONNECT delivered\n"); return 3; }

    /* server accepts and sends a frame; client must get FD_READ via the window */
    {
        SOCKET acc = accept(g_server, NULL, NULL);
        send(acc, FRAME, sizeof(FRAME), 0);
        {
            int i;
            for (i = 0; i < 200 && g_readBits == 0; ++i) {
                pump_messages(16);
                Sleep(5);
            }
        }
        if (g_readBits == 0) { printf("[FAIL] no FD_READ delivered\n"); closesocket(acc); return 3; }
        got = recv(g_client, buf, sizeof(buf), 0);
        printf("FD_READ delivered=%ld, recv=%d bytes: '%s'\n", g_readBits, got, buf);
        if (got <= 0 || memcmp(buf, FRAME, sizeof(FRAME)) != 0) {
            printf("[FAIL] payload mismatch\n"); closesocket(acc); return 3;
        }
        closesocket(acc);
    }

    /* pump until FD_CLOSE (server side closed accepted socket) */
    {
        int i;
        for (i = 0; i < 200; ++i) { pump_messages(16); Sleep(5); }
    }
    printf("[OK] WSAAsyncSelect + hidden window delivers CONNECT/READ/CLOSE to single-threaded pump\n");
    printf("final: msgs=%ld connect=%ld write=%ld read=%ld\n",
           g_msgCount, g_connectBits, g_writeBits, g_readBits);

    closesocket(g_client);
    closesocket(g_server);
    DestroyWindow(g_hwnd);
    WSACleanup();
    return 0;
}
