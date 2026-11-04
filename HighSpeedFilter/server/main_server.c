
/**
 * @file       main_server.c
 * @brief      高速过滤器服务端入口：XCoreApplication 控制台事件循环。
 * @details    装配顺序：设置（XSettings 落程序根目录）→ 工业 IO
 *             （HsfServerIo：XIoChannelGroup + XModbusHub 自动管理变频器
 *             与模拟量）→ XLink 服务端（HsfServerApp：登录鉴权 + 命令
 *             路由 + 遥测推送）→ 事件循环。用法：
 *               HsfServer [端口]        端口缺省取配置文件 cfg/hsf_server.ini
 *                                       的 [server] port（首次运行自动落盘
 *                                       6100）；命令行指定只对本次启动生效
 *               HsfServer --no-io       只起 XLink/登录（IO 装配跳过，
 *                                       供通讯链路联调）
 *             部署参数（串口号/波特率/站号/模拟量地址）见 HsfServerConfig.h。
 *             事件循环内完成鉴权/轮询/推送；Ctrl+C 退出。
 */
#include "XCoreApplication.h"
#include "HsfServerSettings.h"
#include "HsfServerIo.h"
#include "HsfServerApp.h"
#include "XhsfLinkProto.h"
#include "XPrintf.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(int argc, char** argv)
{
    XCoreApplication* app;
    unsigned port = 0;              /* 0=未指定，按配置文件解析 */
    bool portFromArgv = false;
    bool setupIo = true;
    int i;

    /* 诊断型控制台程序：stdout 去缓冲，保证管道/重定向下逐行可见。 */
    setvbuf(stdout, NULL, _IONBF, 0);

    for (i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "--no-io") == 0) {
            setupIo = false;
        } else {
            port = (unsigned)atoi(argv[i]);
            portFromArgv = (port > 0 && port <= 65535);
        }
    }

    app = XCoreApplication_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, argc, argv);
    if (!app) {
        XPrintf("HsfServer: 事件循环创建失败\n");
        return 1;
    }
    XPrintf("HsfServer: 启动\n");

    /* 设置持久化：程序运行根目录 cfg/hsf_server.ini（对齐客户端口径）。
     * 打开失败不阻断启动。 */
    HsfServerSettings_open();

    /* 监听端口：命令行显式指定只对本次启动生效（不写回配置文件——
     * HsfServer 与 HsfServerFlow 共用同一份 cfg/hsf_server.ini，写回会把
     * 另一路服务端的端口一起改掉）。未指定则取配置 server/port；配置缺键
     * 时 resolvePort 会把缺省 6100 落盘。 */
    if (portFromArgv) {
        XPrintf("HsfServer: 端口取自命令行 %u（本次生效，不写配置）\n", port);
    } else {
        port = (unsigned)HsfServerSettings_resolvePort();
    }

    /* 工业 IO：变频器（RTU 站 2）+ 模拟量（TCP 8 路）。IO 失败不阻断
     * XLink 启动——登录/遥测链路可先联调，设备恢复后重启服务端。 */
    if (setupIo) {
        if (!HsfServerIo_start()) {
            XPrintf("HsfServer: IO 装配失败（无可用从站）——继续仅提供"
                    "登录链路\n");
        }
    } else {
        XPrintf("HsfServer: --no-io：跳过 IO 装配\n");
    }

    /* XLink 服务端：监听 + 登录鉴权 + 路由 + 遥测。失败即退出（无可
     * 服务内容）。 */
    if (!HsfServerApp_start((uint16_t)port)) {
        XPrintf("HsfServer: XLink 服务端启动失败\n");
        HsfServerIo_stop();
        HsfServerSettings_close();
        XClassDelete((XClass*)app);
        return 2;
    }

    XCoreApplication_exec();   /* 控制台事件循环：鉴权/轮询/推送全在此驱动。 */

    XPrintf("HsfServer: 事件循环退出，收尾\n");
    HsfServerApp_stop();
    HsfServerIo_stop();
    HsfServerSettings_close();
    XClassDelete((XClass*)app);
    return 0;
}
