/**
 * @file       XSocketPipeTest.c
 * @brief      命名管道（Windows 本地流）传输专项回归（--test xsocket-pipe）。
 *
 * @details    [为什么要另立门户] Windows 的 WSAEventSelect 套接字后端把
 *             「本地流」（命名管道）与 TCP 放在**同一套上下文**里
 *             （`XDeviceNetworkContextWin32Select`），但收发实现完全不同：
 *             管道走 `CreateFileA` + **同步** `ReadFile`/`WriteFile`
 *             （Drive/windows/Device/Network/XDeviceNetwork_win32_select.c 的
 *             pipe 分支），**不走** WSAEventSelect / recv / send / 就绪轮询表。
 *             ⇒ XSocketStallTest.c 里那 20 多条套接字回归锁，**一条都覆盖不到
 *             管道路径**；管道一旦坏掉（MySQL over named pipe 这条链直接断），
 *             现有门禁依然全绿。
 *
 *             本文件补四组：
 *               1) 连通 + 双向收发逐字节一致；
 *               2) 对端关管（ERROR_BROKEN_PIPE）⇒ 本端及时察觉；
 *               3) 大块（>=64KB）跨管道读写不截断；
 *               4) 写背压触达 + 1MB 续发完整（**背压的确定性覆盖**在管道，
 *                  因为 Windows 的 TCP 发送缓冲/接收窗口都自动调节 ⇒ TCP 上
 *                  一次 send 就能把 1MB 全收下，压不出背压）。
 *
 * @note 读路径是**同步 ReadFile** ⇒ 会阻塞事件循环 ⇒ 用例里等数据时
 *       **不能**依赖 processEvents 推进（管道路径压根没有就绪事件）。
 * @note 服务端一侧框架没有提供（MySQL 服务端自己建管道实例），所以本文件直接
 *       用 Win32 API 建管道服务端（CreateNamedPipeA / ConnectNamedPipe /
 *       ReadFile / WriteFile）；客户端一侧走框架的
 *       `XAbstractSocket_connectLocalStream_private`（与 MySQL 客户端同一条路）。
 * @note 非 Windows 平台没有命名管道 ⇒ 整套跳过（不算失败）。
 */
#include "XIOTest.h"
#include "XTcpSocket.h"
#include "XAbstractSocket.h"
/* 私有头：本地流连接的唯一入口（MySQL 客户端也用它）。
   框架没给命名管道留公开 API，只能走这条内部通道。 */
#include "XAbstractSocket_p.h"
#include "XDeviceNetwork.h"     /* XDeviceNetwork_LocalStream_NamedPipe */
#include "XIODevice.h"
#include "XCoreApplication.h"
#include "XDateTime.h"
#include "XPrintf.h"
#include "XString.h"
#include "XThread.h"
#include <string.h>

#if DEMOTEST_ON

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#define SPT_HAS_PIPE 1
#else
#define SPT_HAS_PIPE 0
#endif

#define SPT_ASSERT(cond, ...) do {                                        \
    if (!(cond)) {                                                        \
        XPrintf("  [失败] " __VA_ARGS__);                                 \
        XPrintf("  (第%d行)\n", __LINE__);                                 \
        return false;                                                     \
    }                                                                     \
} while (0)

#define SPT_PASS(msg) XPrintf("  [通过] %s\n", msg)

/* ==================== 管道服务端（Win32 API） ==================== */

#if SPT_HAS_PIPE

/** @brief 管道服务端实例。 */
typedef struct SpServer {
    HANDLE hPipe;      /**< 管道实例句柄。 */
    char   path[128];  /**< 完整路径 \\.\pipe\<name>。 */
    bool   connected;  /**< 是否已完成 ConnectNamedPipe。 */
} SpServer;

/** @brief 生成唯一的管道名（带进程号，避免同机并行跑时互相串）。 */
static void sp_make_name(char* buf, size_t n, int idx)
{
    snprintf(buf, n, "\\\\.\\pipe\\XinYueCPipeTest_%lu_%d",
             (unsigned long)GetCurrentProcessId(), idx);
}

/** @brief 建管道服务端实例（还不 accept）。 */
static bool sp_server_create(SpServer* s, const char* path)
{
    memset(s, 0, sizeof(*s));
    snprintf(s->path, sizeof(s->path), "%s", path);
    s->hPipe = CreateNamedPipeA(path,
                                PIPE_ACCESS_DUPLEX,
                                PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT,
                                1,          /* 最大实例数 1 */
                                65536,      /* 输出缓冲 */
                                65536,      /* 输入缓冲 */
                                0,          /* 默认超时 */
                                NULL);
    if (s->hPipe == INVALID_HANDLE_VALUE) {
        XPrintf("  [诊断] CreateNamedPipeA 失败 err=%lu\n", (unsigned long)GetLastError());
        return false;
    }
    return true;
}

/** @brief 等待客户端连上（客户端已 CreateFile 成功时立即返回）。 */
static bool sp_server_accept(SpServer* s)
{
    BOOL ok = ConnectNamedPipe(s->hPipe, NULL);
    if (!ok) {
        DWORD err = GetLastError();
        if (err != ERROR_PIPE_CONNECTED) {
            XPrintf("  [诊断] ConnectNamedPipe 失败 err=%lu\n", (unsigned long)err);
            return false;
        }
    }
    s->connected = true;
    return true;
}

/** @brief 服务端读（同步）。 */
static int sp_server_read(SpServer* s, char* buf, int want)
{
    DWORD got = 0;
    if (!s || s->hPipe == INVALID_HANDLE_VALUE) return -1;
    if (!ReadFile(s->hPipe, buf, (DWORD)want, &got, NULL)) {
        XPrintf("  [诊断] 服务端 ReadFile 失败 err=%lu\n", (unsigned long)GetLastError());
        return -1;
    }
    return (int)got;
}

/** @brief 服务端写（同步）。 */
static int sp_server_write(SpServer* s, const char* buf, int len)
{
    DWORD put = 0;
    if (!s || s->hPipe == INVALID_HANDLE_VALUE) return -1;
    if (!WriteFile(s->hPipe, buf, (DWORD)len, &put, NULL)) {
        XPrintf("  [诊断] 服务端 WriteFile 失败 err=%lu\n", (unsigned long)GetLastError());
        return -1;
    }
    return (int)put;
}

/** @brief 关掉管道服务端（对端随后会读到 ERROR_BROKEN_PIPE）。 */
static void sp_server_close(SpServer* s)
{
    if (!s || s->hPipe == INVALID_HANDLE_VALUE) return;
    if (s->connected) DisconnectNamedPipe(s->hPipe);
    CloseHandle(s->hPipe);
    s->hPipe = INVALID_HANDLE_VALUE;
    s->connected = false;
}

/* ==================== 客户端（框架侧） ==================== */

/** @brief 用框架的本地流接口连上命名管道。 */
static XTcpSocket* sp_client_connect(const char* path)
{
    XTcpSocket* sock;
    XString* endpoint;

    sock = XTcpSocket_create();
    if (!sock) return NULL;
    endpoint = XString_create_utf8(path);
    if (!endpoint) {
        XTcpSocket_deleteLater((XObject*)sock);
        return NULL;
    }
    if (!XAbstractSocket_connectLocalStream_private((XAbstractSocket*)sock,
                                                    endpoint,
                                                    XDeviceNetwork_LocalStream_NamedPipe,
                                                    3000)) {
        XPrintf("  [诊断] connectLocalStream 失败（管道路径未建立）\n");
        XClassDelete(endpoint);
        XTcpSocket_deleteLater((XObject*)sock);
        return NULL;
    }
    XClassDelete(endpoint);
    return sock;
}

/** @brief 客户端释放。 */
static void sp_client_close(XTcpSocket* sock)
{
    if (!sock) return;
    XIODevice_close_base((XIODevice*)sock);
    XTcpSocket_deleteLater((XObject*)sock);
    XCoreApplication_processEvents(XEventLoop_AllEvents);
}

/**
 * @brief 用例一：连通 + 双向收发逐字节一致。
 * @details 双向都测：客户端 → 服务端、服务端 → 客户端。
 * @note 位置相关填充（`(i*31+7)&0xFF`）：错位/串包立刻可辨，不用全同字符。
 */
static bool sp_connect_and_bidirectional(void)
{
    enum { kLen = 32 };
    char path[128];
    SpServer server;
    XTcpSocket* cli = NULL;
    char out[kLen];
    char in[kLen];
    int i;
    int got;

    sp_make_name(path, sizeof(path), 1);
    if (!sp_server_create(&server, path)) {
        XPrintf("  [失败] 建管道服务端失败\n");
        return false;
    }

    cli = sp_client_connect(path);
    if (!cli) {
        XPrintf("  [失败] 客户端连管道失败\n");
        sp_server_close(&server);
        return false;
    }
    if (!sp_server_accept(&server)) {
        XPrintf("  [失败] 服务端 accept 失败\n");
        sp_client_close(cli);
        sp_server_close(&server);
        return false;
    }

    for (i = 0; i < kLen; ++i)
        out[i] = (char)((i * 31 + 7) & 0xFF);

    /* --- 方向一：客户端 → 服务端 --- */
    if (XIODevice_write_1((XIODevice*)cli, out, kLen) != kLen) {
        XPrintf("  [失败] 客户端写失败\n");
        sp_client_close(cli);
        sp_server_close(&server);
        return false;
    }
    memset(in, 0, sizeof(in));
    got = sp_server_read(&server, in, kLen);
    if (got != kLen || memcmp(in, out, kLen) != 0) {
        XPrintf("  [失败] 服务端收到的 %d 字节与发出不符\n", got);
        sp_client_close(cli);
        sp_server_close(&server);
        return false;
    }

    /* --- 方向二：服务端 → 客户端 --- */
    if (sp_server_write(&server, out, kLen) != kLen) {
        XPrintf("  [失败] 服务端写失败\n");
        sp_client_close(cli);
        sp_server_close(&server);
        return false;
    }
    memset(in, 0, sizeof(in));
    got = 0;
    {
        /* 管道读是同步的：框架侧没有就绪事件 ⇒ 只能靠「读环空时直取内核」
         * 那条路（win32-select 后端的 socketRead 对管道**不走**直取）——
         * 所以这里给一段等待窗口，等框架把数据搬进读环。 */
        uint64_t start = XDateTime_currentMSecsSinceEpoch();
        while (XDateTime_currentMSecsSinceEpoch() - start < 1000) {
            int64_t n = XIODevice_read_1((XIODevice*)cli, in + got, kLen - got);
            if (n > 0) {
                got += (int)n;
                if (got >= kLen) break;
                continue;
            }
            XThread_msleep(2);
        }
    }
    if (got != kLen || memcmp(in, out, kLen) != 0) {
        XPrintf("  [失败] 客户端只收到 %d/%d 字节（管道读方向没交付数据）\n",
                got, kLen);
        XPrintf("  [诊断] 管道读路径：后端 XDeviceNetwork_socketRead 对 namedPipe "
                "不走「读环空直取内核」，而就绪派发（DispatchReady）对 "
                "p->socket==INVALID_SOCKET 直接 return ⇒ 管道永远进不了读环\n");
        sp_client_close(cli);
        sp_server_close(&server);
        return false;
    }

    sp_client_close(cli);
    sp_server_close(&server);
    SPT_PASS("管道连通 + 双向收发逐字节一致");
    return true;
}

/**
 * @brief 用例二：对端关管（ERROR_BROKEN_PIPE）⇒ 本端必须及时察觉。
 * @details 判据：服务端关管后，客户端再写必须**报错**（不能静默成功、
 *          不能把数据吞进写环了事）。
 */
static bool sp_broken_pipe_detected(void)
{
    enum { kLen = 16 };
    char path[128];
    SpServer server;
    XTcpSocket* cli = NULL;
    char payload[kLen];
    int64_t w;
    int i;

    sp_make_name(path, sizeof(path), 2);
    if (!sp_server_create(&server, path)) {
        XPrintf("  [失败] 建管道服务端失败\n");
        return false;
    }
    cli = sp_client_connect(path);
    if (!cli) {
        XPrintf("  [失败] 客户端连管道失败\n");
        sp_server_close(&server);
        return false;
    }
    if (!sp_server_accept(&server)) {
        XPrintf("  [失败] 服务端 accept 失败\n");
        sp_client_close(cli);
        sp_server_close(&server);
        return false;
    }
    for (i = 0; i < kLen; ++i)
        payload[i] = (char)((i * 13 + 5) & 0xFF);

    /* 先打通一次，确认管道确实通。 */
    if (XIODevice_write_1((XIODevice*)cli, payload, kLen) != kLen) {
        XPrintf("  [失败] 首次写失败\n");
        sp_client_close(cli);
        sp_server_close(&server);
        return false;
    }
    {
        char sink[kLen];
        if (sp_server_read(&server, sink, kLen) != kLen) {
            XPrintf("  [失败] 首次读失败\n");
            sp_client_close(cli);
            sp_server_close(&server);
            return false;
        }
    }

    /* 对端关管。 */
    sp_server_close(&server);
    XThread_msleep(50);

    /* 本端再写：必须报错。 */
    w = XIODevice_write_1((XIODevice*)cli, payload, kLen);
    if (w >= 0) {
        XPrintf("  [失败] 对端关管后写仍返回 %lld（静默成功，断链没被察觉）\n",
                (long long)w);
        XPrintf("  [诊断] 后端 socketWrite 对 XNETWORK_IO_ERROR 的处理是"
                "「整包入写环并回报受理」⇒ 断链被吞掉\n");
        sp_client_close(cli);
        return false;
    }

    sp_client_close(cli);
    XPrintf("  [通过] 对端关管后写报错（返回 %lld）\n", (long long)w);
    return true;
}

/**
 * @brief 用例三：大块（>=64KB）跨管道读写不截断。
 * @details 客户端 → 服务端方向：按 4KB 一块写、每写一块服务端读一块
 *          （管道的 WriteFile 是**同步阻塞**的，服务端不读就会把线程钉死）。
 *          服务端 → 客户端方向只写一小块（管道缓冲会被写满 ⇒ 同步写会阻塞，
 *          用例不能把进程挂住）。
 */
static bool sp_large_block_no_truncation(void)
{
    enum { kTotal = 64 * 1024, kChunk = 4096 };
    static char out[kTotal];
    static char in[kTotal];
    char path[128];
    SpServer server;
    XTcpSocket* cli = NULL;
    int sent = 0;
    int got = 0;
    int i;

    sp_make_name(path, sizeof(path), 3);
    if (!sp_server_create(&server, path)) {
        XPrintf("  [失败] 建管道服务端失败\n");
        return false;
    }
    cli = sp_client_connect(path);
    if (!cli) {
        XPrintf("  [失败] 客户端连管道失败\n");
        sp_server_close(&server);
        return false;
    }
    if (!sp_server_accept(&server)) {
        XPrintf("  [失败] 服务端 accept 失败\n");
        sp_client_close(cli);
        sp_server_close(&server);
        return false;
    }
    for (i = 0; i < kTotal; ++i)
        out[i] = (char)((i * 31 + 7) & 0xFF);

    /* --- 客户端 → 服务端：64KB 逐块写、逐块读 --- */
    while (sent < kTotal) {
        int n = kTotal - sent;
        if (n > kChunk) n = kChunk;
        if (XIODevice_write_1((XIODevice*)cli, out + sent, n) != n) {
            XPrintf("  [失败] 第 %d 字节处写失败\n", sent);
            sp_client_close(cli);
            sp_server_close(&server);
            return false;
        }
        sent += n;
        {
            int r = sp_server_read(&server, in + got, n);
            if (r != n) {
                XPrintf("  [失败] 服务端在 %d 字节处只读到 %d/%d\n", got, r, n);
                sp_client_close(cli);
                sp_server_close(&server);
                return false;
            }
            got += r;
        }
    }
    if (got != kTotal) {
        XPrintf("  [失败] 服务端只收到 %d/%d 字节（大块被截断）\n", got, kTotal);
        sp_client_close(cli);
        sp_server_close(&server);
        return false;
    }
    if (memcmp(in, out, kTotal) != 0) {
        XPrintf("  [失败] 64KB 内容不符（截断/错位）\n");
        sp_client_close(cli);
        sp_server_close(&server);
        return false;
    }

    /* --- 服务端 → 客户端：只写一小块（避免同步写把线程钉死） --- */
    {
        enum { kBack = 1024 };
        char back[kBack];
        int64_t n = 0;
        uint64_t start;
        for (i = 0; i < kBack; ++i)
            back[i] = (char)((i * 7 + 3) & 0xFF);
        if (sp_server_write(&server, back, kBack) != kBack) {
            XPrintf("  [失败] 服务端反向写失败\n");
            sp_client_close(cli);
            sp_server_close(&server);
            return false;
        }
        start = XDateTime_currentMSecsSinceEpoch();
        while (XDateTime_currentMSecsSinceEpoch() - start < 1000) {
            int64_t r = XIODevice_read_1((XIODevice*)cli, in + n, kBack - n);
            if (r > 0) {
                n += r;
                if (n >= kBack) break;
                continue;
            }
            XThread_msleep(2);
        }
        if (n != kBack || memcmp(in, back, kBack) != 0) {
            XPrintf("  [失败] 客户端反向只收到 %lld/%d 字节（管道读方向没交付数据）\n",
                    (long long)n, kBack);
            sp_client_close(cli);
            sp_server_close(&server);
            return false;
        }
    }

    sp_client_close(cli);
    sp_server_close(&server);
    SPT_PASS("64KB 跨管道读写不截断");
    return true;
}

/**
 * @brief 用例四：管道写背压 + 续发完整性（**背压的确定性覆盖**）。
 * @details TCP 上背压**无法确定性构造**：Windows 的发送缓冲与接收窗口都
 *          带自动调节，loopback 上一次 `send` 就能把 1MB 全部收下
 *          （实测 SO_SNDBUF 已压到 4096 仍不返回 `WSAEWOULDBLOCK`）。
 *          命名管道的缓冲是**固定**的（默认 64KB）⇒ 写满必然触发
 *          `ERROR_NO_DATA` ⇒ 后端把它当**背压**（数据入写环等续发）
 *          ⇒ 背压分支**必然**被触达。
 *
 *          判据两条：① 写环必须出现待发数据（背压确实被触达，不是空跑）；
 *          ② 服务端最终必须逐字节收齐全部 1MB（续发不丢、不乱序）。
 */
static bool sp_write_backpressure_full(void)
{
    enum { kTotal = 1024 * 1024 };
    static char out[kTotal];
    static char in[kTotal];
    char path[128];
    SpServer server;
    XTcpSocket* cli = NULL;
    int64_t w;
    int got = 0;
    int i;
    bool sawQueued = false;

    sp_make_name(path, sizeof(path), 4);
    if (!sp_server_create(&server, path)) {
        XPrintf("  [失败] 建管道服务端失败\n");
        return false;
    }
    cli = sp_client_connect(path);
    if (!cli) {
        XPrintf("  [失败] 客户端连管道失败\n");
        sp_server_close(&server);
        return false;
    }
    if (!sp_server_accept(&server)) {
        XPrintf("  [失败] 服务端 accept 失败\n");
        sp_client_close(cli);
        sp_server_close(&server);
        return false;
    }
    for (i = 0; i < kTotal; ++i)
        out[i] = (char)((i * 31 + 7) & 0xFF);

    /* 一次写 1MB：管道缓冲只有 64KB ⇒ 必然写不满剩余部分 ⇒ 必然入写环。 */
    w = XIODevice_write_1((XIODevice*)cli, out, kTotal);
    if (w != kTotal) {
        XPrintf("  [失败] 客户端写 1MB 返回 %lld（应受理全部）\n", (long long)w);
        sp_client_close(cli);
        sp_server_close(&server);
        return false;
    }
    if (XIODevice_bytesToWrite_base((XIODevice*)cli) > 0)
        sawQueued = true;
    if (!sawQueued) {
        XPrintf("  [失败] 写环里没有待发数据 ⇒ 背压分支没被触达（本例无效）\n");
        sp_client_close(cli);
        sp_server_close(&server);
        return false;
    }

    /* 服务端逐块读；客户端侧靠事件环把写环推出去（管道没有写就绪事件，
     * 续发由后端在管道的就绪派发里主动 drain）。 */
    {
        uint64_t start = XDateTime_currentMSecsSinceEpoch();
        while (got < kTotal && XDateTime_currentMSecsSinceEpoch() - start < 30000) {
            int r;
            XCoreApplication_processEvents(XEventLoop_AllEvents);
            r = sp_server_read(&server, in + got, kTotal - got);
            if (r <= 0) {
                XPrintf("  [失败] 服务端读到 %d 字节后中断\n", got);
                sp_client_close(cli);
                sp_server_close(&server);
                return false;
            }
            got += r;
        }
    }
    if (got != kTotal) {
        XPrintf("  [失败] 服务端只收到 %d/%d 字节（背压续发丢数据）\n", got, kTotal);
        sp_client_close(cli);
        sp_server_close(&server);
        return false;
    }
    if (memcmp(in, out, kTotal) != 0) {
        XPrintf("  [失败] 1MB 内容不符（背压续发后乱序/损坏）\n");
        sp_client_close(cli);
        sp_server_close(&server);
        return false;
    }

    sp_client_close(cli);
    sp_server_close(&server);
    SPT_PASS("管道写背压被触达且 1MB 续发完整");
    return true;
}

#endif /* SPT_HAS_PIPE */

int XSocketPipeTest_run(void)
{
    typedef struct { const char* name; bool (*fn)(void); } Case;
#if SPT_HAS_PIPE
    static const Case kCases[] = {
        { "管道连通+双向收发一致", sp_connect_and_bidirectional },
        { "对端关管及时察觉",     sp_broken_pipe_detected },
        { "64KB 大块不截断",       sp_large_block_no_truncation },
        { "写背压触达+1MB续发完整", sp_write_backpressure_full }
    };
#else
    static const Case kCases[] = { { "（非 Windows，无命名管道）", NULL } };
#endif
    int i;
    int failed = 0;
    const int n = (int)(sizeof(kCases) / sizeof(kCases[0]));

    XPrintf("========== 命名管道传输回归开始（%d 项） ==========\n", n);
    for (i = 0; i < n; ++i) {
        XPrintf("---- [%d/%d] %s ----\n", i + 1, n, kCases[i].name);
        XCoreApplication_processEvents(XEventLoop_AllEvents);
#if SPT_HAS_PIPE
        if (!kCases[i].fn()) {
            ++failed;
            XPrintf("  [失败] %s\n", kCases[i].name);
        }
#else
        XPrintf("  [跳过] 本平台没有命名管道（仅 Windows）\n");
        break;
#endif
    }
#if !SPT_HAS_PIPE
    (void)kCases;
    (void)failed;
#endif
    XPrintf("\n========== 命名管道传输回归完成: %d 通过, %d 失败 ==========\n",
            n - failed, failed);
    return failed;
}

#endif /* DEMOTEST_ON */
