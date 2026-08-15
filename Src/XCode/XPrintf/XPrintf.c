#include"XPrintf.h"
#include"XString.h"
#include"XByteArray.h"

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
    if (str == NULL)
        return 0;
#ifdef _WIN32
    XPrintf_initConsole();
#if XPRINTF_UTF8_CONSOLE
    printf("%s", XString_toUtf8(str));
#else
    printf("%s", XString_toLocal(str));
#endif
#else
    printf("%s", XString_toUtf8(str));
#endif
    return XString_length_base(str);
}

/* ========================================================================== */
/*                              XPrintf_3                                     */
/* ========================================================================== */
int XPrintf_3(const char* utf8_str)
{
    if (!utf8_str) return 0;

#ifdef _WIN32
    XPrintf_initConsole();
#if XPRINTF_UTF8_CONSOLE
    /* UTF-8模式：直接输出 */
    return printf("%s", utf8_str);
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

    int result = printf("%s", gbk_buf);
    XFree_System(gbk_buf);
    return result;
#endif
#else
    return printf("%s", utf8_str);
#endif
}

/* ========================================================================== */
/*                              XPrintf_4                                     */
/* ========================================================================== */
int XPrintf_4(const XByteArray* array)
{
    if(!array||XByteArray_isEmpty_base(array))
        return 0;
    size_t len = XByteArray_size_base(array);
    for_each_iterator(array, XByteArray,it)
    {
        char c = XByteArray_iterator_data(&it);
        putchar(c);
    }
    return len;
}

/* ========================================================================== */
/*                              XPrintf (主函数)                               */
/* ========================================================================== */
int XPrintf(const char* format, ...)
{
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

    /* 步骤3：根据模式输出 */
    int result = 0;
#ifdef _WIN32
    XPrintf_initConsole();
#if XPRINTF_UTF8_CONSOLE
    /* UTF-8模式：直接输出 */
    result = printf("%s", utf8_buf);
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
        result = printf("%s", gbk_buf);
    }
    XFree_System(gbk_buf);
#endif
#else
    /* Linux/macOS：直接输出UTF-8 */
    result = printf("%s", utf8_buf);
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
    XPrintf_initConsole();
#endif

    XChar chs[] = { *ch, 0 };
    char buff[5] = { 0 };
#if defined(_WIN32) && !XPRINTF_UTF8_CONSOLE
    /* GBK模式：XChar(UTF-16) -> GBK */
    XChar_toLocalStream(&chs, 1, buff, 5);
#else
    /* UTF-8模式：XChar(UTF-16) -> UTF-8 */
    XChar_toUtf8Stream(&chs, 1, (uint8_t*)buff, 5);
#endif
    return printf("%s", buff);
}
