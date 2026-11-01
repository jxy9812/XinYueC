/**
 * @file       main_client.c
 * @brief      高速过滤器客户端入口：XGuiApplication GUI 事件循环。
 * @details    用法：HsfClient [主机] [端口]（缺省 127.0.0.1 6000）。
 *             XGuiApplication 需要显示环境（Windows 桌面 / Linux X11）。
 */
#include "XGuiApplication.h"
#include "XhsfClient.h"
#include "XhsfProtocol.h"
#include "XPrintf.h"
#include <stdlib.h>

int main(int argc, char** argv)
{
    XGuiApplication* app;
    const char* host = (argc > 1) ? argv[1] : "127.0.0.1";
    unsigned port = (argc > 2) ? (unsigned)atoi(argv[2])
                               : XHSF_SERVER_DEFAULT_PORT;

    app = XGuiApplication_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, argc, argv);
    if (!app) {
        XPrintf("HsfClient: GUI 事件循环创建失败\n");
        return 1;
    }

    return XhsfClient_start(host, port); /* 进入 GUI 事件循环。 */
}
