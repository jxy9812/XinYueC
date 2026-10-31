#include"XPrintf.h"
#if XPRINTF_OUTPUT_REDIRECT_ON
#include "XAtomic.h"
#include "XMutex.h" /* 抽水线程字节队列互斥锁。 */
#endif
#include <limits.h>
#include"XStringUtils.h" /* strlen 直出改经 XStrlen（外部依赖约束） */
#include"XString.h"
#include"XByteArray.h"

/* XPrintf 是统一输出入口，底层控制台写出必须使用 fwrite，避免递归调用自身。 */
static int xprintf_write_stdout(const char* data, size_t size)
{
    size_t written;

    if (!data || !size)
        return 0;
    written = fwrite(data, 1, size, stdout);
    return written > (size_t)INT_MAX ? INT_MAX : (int)written;
}

#if XPRINTF_OUTPUT_REDIRECT_ON
/* XAtomic 统一提供跨平台的线程局部存储限定符，避免各模块重复判断编译器。 */
static XATOMIC_THREAD_LOCAL XPrintfOutputWrite g_xprintf_output_write;
static XATOMIC_THREAD_LOCAL void* g_xprintf_output_userData;

/* [2026-10-31 重入门] 回调链内再打日志（label setText → 布局 → 框架内部
 * XError/XPrintf）会递归进入本函数 → 无限递归 → 栈踩坏 → 垃圾函数指针/
 * 崩溃（demo 实测：xprintf_write_redirect 调用处读 0xFFFFFFFFFFFFFFFF）。
 * 重入期改写**原 fd 1**（改道前保存的副本，控制台/调试器输出窗口），
 * 绝不递归回调、也绝不写管道（fd 1 已是管道，写回 = 自回声）。 */
static XATOMIC_THREAD_LOCAL int g_xprintf_redirectDepth;



/* ==================== 标准 C 输出旁路拦截 ====================
 * [2026-10-31 根因修复] 此前 XPrintf 只重定向自身各入口
 * （XPrintf/XPrintf_3/XPrintf_string…）——测试代码里直接调
 * `printf`/`fprintf(stdout, …)`/`puts` 的输出完全绕过重定向，落进
 * 控制台而不进 GUI 输出区。用户实测：测试菜单点了动作，输出区里
 * "少了很多行"，控制台却有（XBinarySearchTest 等大量用例用 printf）。
 * Qt 侧无此问题：qDebug/qInfo 与 stdout 都归 QMessageHandler 统一收口。
 *
 * 处方：作用域生效期间把 **流程 1 号文件描述符** 重定向到匿名管道，
 * 后台线程持续抽走管道内容并转投 g_xprintf_output_write。stdio 家族
 * （printf/fputs/puts/fwrite(stdout)）与 WriteFile(GetStdHandle) 全部
 * 经 fd 1，一次改道即全部收口，无需逐个 API 打桩。
 *
 * 时序：XPrintf_outputPush 建管、_dup2 改道、起读线程；
 *       XPrintf_outputPop 恢复原 fd 1、关闭管道写端、等读线程收尾。
 * 崩溃免疫要点：读线程挂起期若主线程已 pop（句柄失效），只允许它读到
 * EOF 自然退出；pop 必须等线程真正结束才 free 上下文，杜绝 UAF。
 * 只作用于当前同步作用域（Shell 命令执行期 / GUI 动作触发期），
 * 其余时间 fd 1 原样，控制台行为零变化。 */
#include <fcntl.h>
#include <string.h>

typedef struct XPrintfStdoutRelay
{
    int savedFd1;      /**< 改道前的原 fd 1（-1=未改道）。 */
    int pipeRead;      /**< 管道读端（-1=未建）。 */
    int pipeWrite;     /**< 管道写端（-1=未建）。 */
    void* thread;      /**< 抽水线程句柄（平台相关）。 */
    int* stopFlag;     /**< 读线程退出标志（堆，线程与主线程共享）。 */
    /* [2026-10-31 第二轮根修] 抽水线程**不得直接调用任何捕获回调**：
     * 回调链（demo 的 tm_captureWrite → XLabel_setText → 布局/堆操作）不是
     * 线程安全的，抽水线程与主线程并发调用即堆破坏（实测：菜单树
     * 损坏（空按钮）+回调指针变垃圾（调用处读 0xFFFFFFFFFFFFFFFF）。
     * 处置：抽水线程只把字节追加进**互斥锁保护的字节队列**；主线程在
     * processEvents 排空（XPrintf_drainRedirectedOutput）时才在主线程调 TLS
     * 回调（GUI 安全）。 */
    XMutex* queueMutex;   /**< 队列互斥锁（懒创）。 */
    char* queueBuf;       /**< 待排空字节（堆）。 */
    size_t queueLen;      /**< 队列字节数。 */
    size_t queueCap;      /**< 队列容量。 */
} XPrintfStdoutRelay;

static XATOMIC_THREAD_LOCAL XPrintfStdoutRelay g_xprintf_stdout_relay;
static XATOMIC_THREAD_LOCAL int g_xprintf_stdout_relayActive;

/* 平台差异收敛：POSIX 用 pipe/_pipe 与 dup2，Windows 用 _pipe/_dup2。
 * 读线程统一用「轮询 + 非阻塞读」实现，避免依赖平台线程 API 差异：
 * 每轮先取退出标志，再从管道读可用字节转投回调。 */
#ifdef _WIN32
#include <io.h>
#include <process.h>
#include <windows.h> /* WaitForSingleObject/CloseHandle/Sleep（抽水线程收尾）。 */
#define XPRINTF_CLOSE_FD _close
#define XPRINTF_DUP2 _dup2
#define XPRINTF_OPEN_PIPE(fds, sz, mode) _pipe((fds), (sz), (mode))
#define XPRINTF_READ_FD _read
#define XPRINTF_WRITE_FD _write
typedef intptr_t XPrintfThreadHandle;
static XPrintfThreadHandle xprintf_threadStart(unsigned (__stdcall* fn)(void*),
                                               void* arg)
{
    return _beginthreadex(NULL, 0, fn, arg, 0, NULL);
}
static void xprintf_threadJoin(XPrintfThreadHandle h)
{
    if (h) { WaitForSingleObject((HANDLE)h, 5000); CloseHandle((HANDLE)h); }
}
static void xprintf_sleepMs(int ms) { Sleep((DWORD)ms); }
#else
#include <unistd.h>
#include <pthread.h>
#include <errno.h>
#define XPRINTF_CLOSE_FD close
#define XPRINTF_DUP2 dup2
#define XPRINTF_OPEN_PIPE(fds, sz, mode) pipe((fds))
#define XPRINTF_READ_FD read
#define XPRINTF_WRITE_FD write
typedef pthread_t XPrintfThreadHandle;
static XPrintfThreadHandle xprintf_threadStart(void* (*fn)(void*), void* arg)
{
    pthread_t t;
    if (pthread_create(&t, NULL, fn, arg) != 0) return 0;
    return t;
}
static void xprintf_threadJoin(XPrintfThreadHandle h)
{
    if (h) pthread_join(h, NULL);
}
static void xprintf_sleepMs(int ms) { usleep((useconds_t)ms * 1000u); }
#endif

/** @brief 管道抽水线程：把 fd 重定向后落进管道的字节转投重定向回调。
 *  @note  退出条件=退出标志置位且管道已读到 EOF/无后续数据。上下文
 *         由主线程在 join 之后释放，线程内不释放任何堆对象。 */
static
#ifdef _WIN32
unsigned __stdcall
#else
void*
#endif
xprintf_stdoutRelayThread(void* arg)
{
    XPrintfStdoutRelay* relay = (XPrintfStdoutRelay*)arg;
    char buf[1024];
    int idleRounds = 0;
    if (!relay) return 0;
    while (relay->stopFlag && !*relay->stopFlag) {
        int n = (int)XPRINTF_READ_FD(relay->pipeRead, buf, (unsigned)sizeof(buf));
        if (n > 0) {
            idleRounds = 0;
            /* 只入队（互斥锁保护）；主线程 processEvents 排空时才调 TLS 回调
             * （GUI 安全）。队列无上限；OOM 时丢增量保命。 */
            if (relay->queueMutex) {
                XMutex_lock(relay->queueMutex);
                if (relay->queueLen + (size_t)n + 1 > relay->queueCap) {
                    size_t ncap = relay->queueCap ? relay->queueCap * 2 : 4096;
                    char* nbuf;
                    while (ncap < relay->queueLen + (size_t)n + 1) ncap *= 2;
                    nbuf = (char*)XRealloc_System(relay->queueBuf, ncap);
                    if (nbuf) { relay->queueBuf = nbuf; relay->queueCap = ncap; }
                }
                if (relay->queueBuf &&
                    relay->queueLen + (size_t)n + 1 <= relay->queueCap) {
                    memcpy(relay->queueBuf + relay->queueLen, buf, (size_t)n);
                    relay->queueLen += (size_t)n;
                    relay->queueBuf[relay->queueLen] = 0;
                } /* 队列失败（OOM）：丢增量。 */
                XMutex_unlock(relay->queueMutex);
            }
        } else if (n == 0) {
            break; /* EOF：写端已关闭。 */
        } else {
            /* 无数据可读：短暂让出，避免忙转。收尾期最多再转 100ms。 */
            if (++idleRounds > 100) break;
            xprintf_sleepMs(1);
        }
    }
    return 0;
}

/** @brief 建立 fd 1 到管道的改道（作用域 push 期）。
 *  @note  XPRINTF_NO_STDOUT_RELAY=1 时跳过改道（诊断开关：仅 XPrintf
 *         自身路径进重定向，标准 C 输出走原控制台——用于二分定位）。 */
static int xprintf_write_redirect(const void* data, size_t size)
{
    int64_t written;
    if (!g_xprintf_output_write) return -1;
    if (!size) return 0;
    if (!data) return -1;
    if (g_xprintf_redirectDepth > 0) {
        /* 重入：写回原 fd 1（未改道副本），不递归回调。 */
        if (g_xprintf_stdout_relay.savedFd1 >= 0)
            XPRINTF_WRITE_FD(g_xprintf_stdout_relay.savedFd1, data,
                             (unsigned)size);
        return (int)size;
    }
    ++g_xprintf_redirectDepth;
    written = g_xprintf_output_write(g_xprintf_output_userData,
                                     (const char*)data, size);
    --g_xprintf_redirectDepth;
    if (written < 0 || (uint64_t)written != (uint64_t)size)
        return -1;
    return size > (size_t)INT_MAX ? INT_MAX : (int)size;
}
static void xprintf_stdoutRelayStart(void)
{
    XPrintfStdoutRelay* relay = &g_xprintf_stdout_relay;
    int fds[2];
    int* stop;
    if (g_xprintf_stdout_relayActive) return;
    if (getenv("XPRINTF_NO_STDOUT_RELAY")) return;
    if (XPRINTF_OPEN_PIPE(fds, 64 * 1024, 0) != 0) return; /* 建管失败：只留 XPrintf 路径。 */
    relay->pipeRead = fds[0];
    relay->pipeWrite = fds[1];
    relay->savedFd1 = -1;
    relay->thread = 0;
    relay->queueBuf = NULL;
    relay->queueLen = 0;
    relay->queueCap = 0;
    relay->queueMutex = XMutex_create(XLock_NonRecursive);
    stop = (int*)XMalloc_System(sizeof(int));
    if (!stop) {
        XPRINTF_CLOSE_FD(relay->pipeRead);
        XPRINTF_CLOSE_FD(relay->pipeWrite);
        relay->pipeRead = relay->pipeWrite = -1;
        return;
    }
    *stop = 0;
    relay->stopFlag = stop;
    /* 保存原 fd 1 副本（pop 时 dup2 还原；原句柄不动）。 */
    {
#ifdef _WIN32
        int dup = _dup(1);
#else
        int dup = dup(1);
#endif
        relay->savedFd1 = dup;
    }
    if (relay->savedFd1 < 0 ||
        XPRINTF_DUP2(relay->pipeWrite, 1) < 0) {
        if (relay->savedFd1 >= 0) XPRINTF_CLOSE_FD(relay->savedFd1);
        XPRINTF_CLOSE_FD(relay->pipeRead);
        XPRINTF_CLOSE_FD(relay->pipeWrite);
        XFree_System(stop);
        relay->stopFlag = NULL;
        relay->pipeRead = relay->pipeWrite = -1;
        relay->savedFd1 = -1;
        return;
    }
    /* 冲刷 stdio 缓冲：改道前已写入但未落盘的字节属旧目标。 */
    fflush(stdout);
    relay->thread = xprintf_threadStart(xprintf_stdoutRelayThread, relay);
    g_xprintf_stdout_relayActive = 1;
}

/** @brief 撤销改道、等抽水线程收尾。 */
static void xprintf_stdoutRelayStop(void)
{
    XPrintfStdoutRelay* relay = &g_xprintf_stdout_relay;
    if (!g_xprintf_stdout_relayActive) return;
    g_xprintf_stdout_relayActive = 0;
    fflush(stdout);
    if (relay->savedFd1 >= 0) {
        XPRINTF_DUP2(relay->savedFd1, 1);
        XPRINTF_CLOSE_FD(relay->savedFd1);
        relay->savedFd1 = -1;
    }
    /* 关写端：读端读到 EOF，抽水线程自然结束（不靠标志抢跑）。 */
    if (relay->pipeWrite >= 0) {
        XPRINTF_CLOSE_FD(relay->pipeWrite);
        relay->pipeWrite = -1;
    }
    if (relay->thread) {
        xprintf_threadJoin((XPrintfThreadHandle)relay->thread);
        relay->thread = 0;
    }
    if (relay->pipeRead >= 0) {
        XPRINTF_CLOSE_FD(relay->pipeRead);
        relay->pipeRead = -1;
    }
    /* 线程已 join：此刻释放共享标志安全（无并发读者）。 */
    if (relay->stopFlag) {
        *relay->stopFlag = 1;
        XFree_System(relay->stopFlag);
        relay->stopFlag = NULL;
    }
}

/* ==================== 排空抽水队列（主线程） ==================== */

/** @brief 把抽水线程收集的标准 C 输出在**当前线程**转投给 TLS 回调。
 *  @note  由 processEvents 在主线程调用；重入门（回调链再 XPrintf）由
 *         xprintf_write_redirect 的 depth 门写回原 fd 1，不递归。 */
void XPrintf_drainRedirectedOutput(void)
{
    XPrintfStdoutRelay* relay = &g_xprintf_stdout_relay;
    if (!g_xprintf_stdout_relayActive || !relay->queueMutex) return;
    if (!g_xprintf_output_write) return;
    XMutex_lock(relay->queueMutex);
    if (relay->queueLen > 0 && relay->queueBuf) {
        /* 交换出队（O(1)）：回调链可能再入队，不能持锁调。 */
        char* out = relay->queueBuf;
        size_t len = relay->queueLen;
        relay->queueBuf = NULL;
        relay->queueLen = 0;
        relay->queueCap = 0;
        XMutex_unlock(relay->queueMutex);
        ++g_xprintf_redirectDepth;
        g_xprintf_output_write(g_xprintf_output_userData, out, (int64_t)len);
        --g_xprintf_redirectDepth;
        XFree_System(out);
    } else {
        XMutex_unlock(relay->queueMutex);
    }
}

bool XPrintf_outputPush(XPrintfOutputScope* scope, XPrintfOutputWrite write,
                        void* userData)
{
    if (!scope || !write) return false;
    /* 重复 push 守卫只认「签名匹配且 active」——脏作用域（未零初始化的
     * 栈对象、已 pop 过又被复用的对象）不参与判定，直接接管。反过来说，
     * 旧实现只看 active 字节：脏内存里的 0xCC 让 push 静默失败（输出
     * 不进 GUI 输出区），随后调用方无条件 pop 又把 0xCCCCCCCCCCCCCCCC
     * 回写进 TLS 回调槽——下一次 XPrintf 即跳转该地址。 */
    if (scope->magic == XPRINTF_SCOPE_MAGIC && scope->active) return false;
    scope->previousWrite = g_xprintf_output_write;
    scope->previousUserData = g_xprintf_output_userData;
    scope->magic = XPRINTF_SCOPE_MAGIC;
    scope->active = true;
    g_xprintf_output_write = write;
    g_xprintf_output_userData = userData;
    /* 标准 C 输出旁路（printf/fputs/WriteFile）一并收口到本回调：只在
     * 最外层作用域建管，嵌套作用域沿用同一管道（回调随 push 切换）。 */
    if (!g_xprintf_stdout_relayActive)
        xprintf_stdoutRelayStart();
    return true;
}

void XPrintf_outputPop(XPrintfOutputScope* scope)
{
    if (!scope) return;
    /* 签名不匹配 = 从未经本实现 push（脏内存 / 他人伪造）：绝不回写
     * previousWrite，否则等于把一个未定义指针装进 TLS 回调槽。 */
    if (scope->magic != XPRINTF_SCOPE_MAGIC) return;
    scope->magic = 0;
    if (!scope->active) return;
    g_xprintf_output_write = scope->previousWrite;
    g_xprintf_output_userData = scope->previousUserData;
    scope->previousWrite = NULL;
    scope->previousUserData = NULL;
    scope->active = false;
    /* 无外层重定向才收管道（嵌套期由外层继续持有）。 */
    if (!g_xprintf_output_write)
        xprintf_stdoutRelayStop();
}
#endif

/* ========================================================================== */
/*                        Windows 控制台编码控制                               */
/* ========================================================================== */
#ifdef _WIN32
/* Windows 控制台代码页切换由 Drive/windows 提供；公共 Src 不包含平台头。 */
extern void XPrintf_platformInitConsole(void);
#endif /* _WIN32 */

/* ========================================================================== */
/*                            XPrintf_string                                   */
/* ========================================================================== */
int XPrintf_string(const XString* str)
{
    const char* utf8;
    if (str == NULL)
        return 0;
    utf8 = XString_toUtf8(str);
    if (!utf8)
        return 0;
#if XPRINTF_OUTPUT_REDIRECT_ON
    if (g_xprintf_output_write) {
        return xprintf_write_redirect(utf8, XStrlen(utf8));
    }
#endif
#ifdef _WIN32
    XPrintf_platformInitConsole();
#if XPRINTF_UTF8_CONSOLE
    return xprintf_write_stdout(utf8, XStrlen(utf8));
#else
    const char* local = XString_toLocal(str);
    return local ? xprintf_write_stdout(local, XStrlen(local)) : 0;
#endif
#else
    return xprintf_write_stdout(utf8, XStrlen(utf8));
#endif
}

/* ========================================================================== */
/*                              XPrintf_3                                     */
/* ========================================================================== */
int XPrintf_3(const char* utf8_str)
{
    if (!utf8_str) return 0;
#if XPRINTF_OUTPUT_REDIRECT_ON
    if (g_xprintf_output_write)
        return xprintf_write_redirect(utf8_str, XStrlen(utf8_str));
#endif

#ifdef _WIN32
    XPrintf_platformInitConsole();
#if XPRINTF_UTF8_CONSOLE
    /* UTF-8模式：直接输出 */
    return xprintf_write_stdout(utf8_str, XStrlen(utf8_str));
#else
    /* GBK模式：UTF-8 -> GBK 转换后输出 */
    int64_t gbk_len = XChar_utf8ToGbkStream(utf8_str, 0, NULL, 0);
    if (gbk_len <= 0) return 0;

    char* gbk_buf = (char*)XMalloc_System(gbk_len + 1);
    if (!gbk_buf) return 0;

    if (XChar_utf8ToGbkStream(utf8_str, 0, gbk_buf, gbk_len + 1) <= 0) {
        XFree_System(gbk_buf);
        return 0;
    }

    int result = xprintf_write_stdout(gbk_buf, XStrlen(gbk_buf));
    XFree_System(gbk_buf);
    return result;
#endif
#else
    return xprintf_write_stdout(utf8_str, XStrlen(utf8_str));
#endif
}

/* ========================================================================== */
/*                              XPrintf_4                                     */
/* ========================================================================== */
int XPrintf_4(const XByteArray* array)
{
    size_t len;
    if(!array||XByteArray_isEmpty_base((const XContainer*)array))
        return 0;
    len = XByteArray_size_base((const XContainer*)array);
#if XPRINTF_OUTPUT_REDIRECT_ON
    if (g_xprintf_output_write)
        return xprintf_write_redirect(XByteArray_data((XByteArray*)array), len);
#endif
    return xprintf_write_stdout((const char*)XByteArray_data((XByteArray*)array), len);
}

/* ========================================================================== */
/*                              XPrintf (主函数)                               */
/* ========================================================================== */
int XPrintf(const char* format, ...)
{
    int result;
    if (!format) return 0;

    va_list args;
    va_start(args, format);

    /* 步骤1：计算所需UTF-8缓冲区大小 */
    va_list args_copy;
    va_copy(args_copy, args);
    int utf8_len = vsnprintf(NULL, 0, format, args_copy) + 1;
    va_end(args_copy);

    if (utf8_len <= 0)
    {
        va_end(args);
        return 0;
    }

    /* 步骤2：格式化UTF-8内容 */
    char* utf8_buf = (char*)XMalloc_System(utf8_len);
    if (!utf8_buf)
    {
        va_end(args);
        return 0;
    }

    vsnprintf(utf8_buf, utf8_len, format, args);
    va_end(args);

#if XPRINTF_OUTPUT_REDIRECT_ON
    if (g_xprintf_output_write) {
        result = xprintf_write_redirect(utf8_buf, XStrlen(utf8_buf));
        XFree_System(utf8_buf);
        return result;
    }
#endif

    /* 步骤3：根据模式输出 */
    result = 0;
#ifdef _WIN32
    XPrintf_platformInitConsole();
#if XPRINTF_UTF8_CONSOLE
    /* UTF-8模式：直接输出 */
    result = xprintf_write_stdout(utf8_buf, XStrlen(utf8_buf));
#else
    /* GBK模式：UTF-8 -> GBK 转换后输出 */
    int gbk_len = XChar_utf8ToGbkStream(utf8_buf, 0, NULL, 0);
    if (gbk_len <= 0)
    {
        XFree_System(utf8_buf);
        return 0;
    }

    char* gbk_buf = (char*)XMalloc_System(gbk_len + 1);
    if (!gbk_buf)
    {
        XFree_System(utf8_buf);
        return 0;
    }

    if (XChar_utf8ToGbkStream(utf8_buf, 0, gbk_buf, gbk_len + 1) > 0)
    {
        result = xprintf_write_stdout(gbk_buf, XStrlen(gbk_buf));
    }
    XFree_System(gbk_buf);
#endif
#else
    /* Linux/macOS：直接输出UTF-8 */
    result = xprintf_write_stdout(utf8_buf, XStrlen(utf8_buf));
#endif

    XFree_System(utf8_buf);
    return result;
}

/* 先格式化用户参数，再由 XPrintf 统一处理平台输出编码和换行。 */
int XPrintf_context(const char* label, const char* file, const char* function,
                    int line, const char* format, ...)
{
    va_list args;
    va_list args_copy;
    int body_len;
    char* body;
    int result;

    if (!format) return 0;
    va_start(args, format);
    va_copy(args_copy, args);
    body_len = vsnprintf(NULL, 0, format, args_copy);
    va_end(args_copy);
    if (body_len < 0) {
        va_end(args);
        return 0;
    }
    body = (char*)XMalloc_System((size_t)body_len + 1u);
    if (!body) {
        va_end(args);
        return 0;
    }
    vsnprintf(body, (size_t)body_len + 1u, format, args);
    va_end(args);
    if (label && label[0]) {
        result = XPrintf("%s [FILE:%s][FUNC:%s][LINE:%d]\n->%s\n",
                         label, file, function, line, body);
    } else {
        result = XPrintf("[FILE:%s][FUNC:%s][LINE:%d]\n->%s\n",
                         file, function, line, body);
    }
    XFree_System(body);
    return result;
}

int XPrintf_line(const char* prefix, const char* format, ...)
{
    va_list args;
    va_list args_copy;
    int body_len;
    char* body;
    int result;

    if (!format) return 0;
    va_start(args, format);
    va_copy(args_copy, args);
    body_len = vsnprintf(NULL, 0, format, args_copy);
    va_end(args_copy);
    if (body_len < 0) {
        va_end(args);
        return 0;
    }
    body = (char*)XMalloc_System((size_t)body_len + 1u);
    if (!body) {
        va_end(args);
        return 0;
    }
    vsnprintf(body, (size_t)body_len + 1u, format, args);
    va_end(args);
    result = XPrintf("%s%s\n", prefix ? prefix : "", body);
    XFree_System(body);
    return result;
}

/* ========================================================================== */
/*                              XPrintf_5                                     */
/* ========================================================================== */
int XPrintf_5(XChar* ch)
{
    if (!ch) return 0;

#ifdef _WIN32
#if XPRINTF_OUTPUT_REDIRECT_ON
    if (!g_xprintf_output_write)
#endif
    XPrintf_platformInitConsole();
#endif

    XChar chs[] = { *ch, 0 };
    char buff[5] = { 0 };
#if defined(_WIN32) && !XPRINTF_UTF8_CONSOLE
    /* GBK模式：XChar(UTF-16) -> GBK */
#if XPRINTF_OUTPUT_REDIRECT_ON
    if (g_xprintf_output_write)
        XChar_toUtf8Stream(chs, 1, (uint8_t*)buff, 5);
    else
#endif
        XChar_toLocalStream(chs, 1, buff, 5);
#else
    /* UTF-8模式：XChar(UTF-16) -> UTF-8 */
    XChar_toUtf8Stream(chs, 1, (uint8_t*)buff, 5);
#endif
#if XPRINTF_OUTPUT_REDIRECT_ON
    if (g_xprintf_output_write)
        return xprintf_write_redirect(buff, XStrlen(buff));
#endif
    return xprintf_write_stdout(buff, XStrlen(buff));
}
