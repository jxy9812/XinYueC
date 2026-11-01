/**
 * @file       main_server.c
 * @brief      高速过滤器服务端入口：XCoreApplication 控制台事件循环。
 * @details    用法：HsfServer [端口]（缺省 6000，0=系统分配）。
 *             事件循环内完成 accept/收发/滤波；Ctrl+C 退出。
 */
#include "XCoreApplication.h"
#include "XhsfServer.h"
#include "XhsfProtocol.h"
#include "XPrintf.h"
#include <stdlib.h>

int main(int argc, char** argv)
{
    XCoreApplication* app;
    XhsfServer* srv;
    unsigned port = XHSF_SERVER_DEFAULT_PORT;

    if (argc > 1) port = (unsigned)atoi(argv[1]);

    app = XCoreApplication_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, argc, argv);
    if (!app) {
        XPrintf("HsfServer: 事件循环创建失败\n");
        return 1;
    }

    srv = XhsfServer_create();
    if (!srv || !XhsfServer_listen(srv, port)) {
        XPrintf("HsfServer: 监听失败（端口 %u）\n", port);
        if (srv) XhsfServer_deleteLater(srv);
        XClassDelete(app);
        return 1;
    }
    XPrintf("HsfServer: 监听端口 %u\n", XhsfServer_port(srv));

    XCoreApplication_exec(); /* 控制台事件循环：accept/收发/滤波全在此驱动。 */

    XhsfServer_deleteLater(srv);
    XClassDelete(app);
    return 0;
}
