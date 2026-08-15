#include"XPrintf.h"
#if XCONSOLE_SHELL_REMOTE_OUTPUT_REDIRECT_ON
#include "XAtomic.h"
#endif
#include <limits.h>
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

#if XCONSOLE_SHELL_REMOTE_OUTPUT_REDIRECT_ON
/* XAtomic 统一提供跨平台的线程局部存储限定符，避免各模块重复判断编译器。 */
static XATOMIC_THREAD_LOCAL XPrintfOutputWrite g_xprintf_output_write;
static XATOMIC_THREAD_LOCAL void* g_xprintf_output_userData;

static int xprintf_write_redirect(const void* data, size_t size)
{
    int64_t written;
    if (!g_xprintf_output_write) return -1;
    if (!size) return 0;
    if (!data) return -1;
    written = g_xprintf_output_write(g_xprintf_output_userData,
                                     (const char*)data, size);
    if (written < 0 || (uint64_t)written != (uint64_t)size)
        return -1;
    return size > (size_t)INT_MAX ? INT_MAX : (int)size;
}

bool XPrintf_outputPush(XPrintfOutputScope* scope, XPrintfOutputWrite write,
                        void* userData)
{
    if (!scope || scope->active || !write) return false;
    scope->previousWrite = g_xprintf_output_write;
    scope->previousUserData = g_xprintf_output_userData;
    scope->active = true;
    g_xprintf_output_write = write;
    g_xprintf_output_userData = userData;
    return true;
}

void XPrintf_outputPop(XPrintfOutputScope* scope)
{
    if (!scope || !scope->active) return;
    g_xprintf_output_write = scope->previousWrite;
    g_xprintf_output_userData = scope->previousUserData;
    scope->previousWrite = NULL;
    scope->previousUserData = NULL;
    scope->active = false;
}
#endif

/* ========================================================================== */
/*                        Windows 控制台编码控制                               */
/* ========================================================================== */
#ifdef _WIN32
#include <windows.h>
#include <locale.h>

#if XPRINTF_UTF8_CONSOLE
/* UTF-8模式：首次调用时将控制台切换为UTF-8，后续直接输出UTF-8 */
static void XPrintf_initConsole(void)
{
    static int s_done = 0;
    if (s_done) return;
    s_done = 1;
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);
    setlocale(LC_ALL, ".utf8");
}
#else
/* GBK模式：首次调用时确保控制台为系统ANSI代码页(CP_ACP)，与GBK转换匹配 */
static void XPrintf_initConsole(void)
{
    static int s_done = 0;
    if (s_done) return;
    s_done = 1;
    UINT acp = GetACP();
    if (GetConsoleOutputCP() != acp)
        SetConsoleOutputCP(acp);
    if (GetConsoleCP() != acp)
        SetConsoleCP(acp);
}
#endif
#endif /* _WIN32 */

/* ========================================================================== */
/*                              XPrintf_2                                     */
/* ========================================================================== */
int XPrintf_2(const XString* str)
{
    const char* utf8;
    if (str == NULL)
        return 0;
    utf8 = XString_toUtf8(str);
    if (!utf8)
        return 0;
#if XCONSOLE_SHELL_REMOTE_OUTPUT_REDIRECT_ON
    if (g_xprintf_output_write) {
        return xprintf_write_redirect(utf8, strlen(utf8));
    }
#endif
#ifdef _WIN32
    XPrintf_initConsole();
#if XPRINTF_UTF8_CONSOLE
    return xprintf_write_stdout(utf8, strlen(utf8));
#else
    const char* local = XString_toLocal(str);
    return local ? xprintf_write_stdout(local, strlen(local)) : 0;
#endif
#else
    return xprintf_write_stdout(utf8, strlen(utf8));
#endif
}

/* ========================================================================== */
/*                              XPrintf_3                                     */
/* ========================================================================== */
int XPrintf_3(const char* utf8_str)
{
    if (!utf8_str) return 0;
#if XCONSOLE_SHELL_REMOTE_OUTPUT_REDIRECT_ON
    if (g_xprintf_output_write)
        return xprintf_write_redirect(utf8_str, strlen(utf8_str));
#endif

#ifdef _WIN32
    XPrintf_initConsole();
#if XPRINTF_UTF8_CONSOLE
    /* UTF-8模式：直接输出 */
    return xprintf_write_stdout(utf8_str, strlen(utf8_str));
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

    int result = xprintf_write_stdout(gbk_buf, strlen(gbk_buf));
    XFree_System(gbk_buf);
    return result;
#endif
#else
    return xprintf_write_stdout(utf8_str, strlen(utf8_str));
#endif
}

/* ========================================================================== */
/*                              XPrintf_4                                     */
/* ========================================================================== */
int XPrintf_4(const XByteArray* array)
{
    size_t len;
    if(!array||XByteArray_isEmpty_base(array))
        return 0;
    len = XByteArray_size_base(array);
#if XCONSOLE_SHELL_REMOTE_OUTPUT_REDIRECT_ON
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

#if XCONSOLE_SHELL_REMOTE_OUTPUT_REDIRECT_ON
    if (g_xprintf_output_write) {
        result = xprintf_write_redirect(utf8_buf, strlen(utf8_buf));
        XFree_System(utf8_buf);
        return result;
    }
#endif

    /* 步骤3：根据模式输出 */
    result = 0;
#ifdef _WIN32
    XPrintf_initConsole();
#if XPRINTF_UTF8_CONSOLE
    /* UTF-8模式：直接输出 */
    result = xprintf_write_stdout(utf8_buf, strlen(utf8_buf));
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
        result = xprintf_write_stdout(gbk_buf, strlen(gbk_buf));
    }
    XFree_System(gbk_buf);
#endif
#else
    /* Linux/macOS：直接输出UTF-8 */
    result = xprintf_write_stdout(utf8_buf, strlen(utf8_buf));
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
#if XCONSOLE_SHELL_REMOTE_OUTPUT_REDIRECT_ON
    if (!g_xprintf_output_write)
#endif
    XPrintf_initConsole();
#endif

    XChar chs[] = { *ch, 0 };
    char buff[5] = { 0 };
#if defined(_WIN32) && !XPRINTF_UTF8_CONSOLE
    /* GBK模式：XChar(UTF-16) -> GBK */
#if XCONSOLE_SHELL_REMOTE_OUTPUT_REDIRECT_ON
    if (g_xprintf_output_write)
        XChar_toUtf8Stream(&chs, 1, (uint8_t*)buff, 5);
    else
#endif
        XChar_toLocalStream(&chs, 1, buff, 5);
#else
    /* UTF-8模式：XChar(UTF-16) -> UTF-8 */
    XChar_toUtf8Stream(&chs, 1, (uint8_t*)buff, 5);
#endif
#if XCONSOLE_SHELL_REMOTE_OUTPUT_REDIRECT_ON
    if (g_xprintf_output_write)
        return xprintf_write_redirect(buff, strlen(buff));
#endif
    return xprintf_write_stdout(buff, strlen(buff));
}
