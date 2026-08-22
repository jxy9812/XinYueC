/**
 * @file       XPrintfPlatform_win32.c
 * @brief      Windows XPrintf 控制台代码页实现。
 * @details    平台头和控制台 API 仅允许出现在 Drive/windows；Src/XPrintf
 *             通过无平台的单一函数声明调用这里的实现。
 */
#include "CXinYueConfig.h"

#if defined(_WIN32)
#include <windows.h>
#include <locale.h>

#if XPRINTF_UTF8_CONSOLE
void XPrintf_platformInitConsole(void)
{
    static int done;
    if (done) return;
    done = 1;
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);
    setlocale(LC_ALL, ".utf8");
}
#else
void XPrintf_platformInitConsole(void)
{
    static int done;
    UINT acp;
    if (done) return;
    done = 1;
    acp = GetACP();
    if (GetConsoleOutputCP() != acp) SetConsoleOutputCP(acp);
    if (GetConsoleCP() != acp) SetConsoleCP(acp);
}
#endif

#endif /* _WIN32 */
