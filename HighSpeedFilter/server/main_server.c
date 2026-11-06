
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
 *               HsfServer --sim         全部从站换成**进程内模拟设备**
 *                                       （不打开任何串口/不连任何设备，
 *                                       服务端照常推遥测、接受阀控命令；
 *                                       写操作只改内存模型，不驱动继电器）
 *               HsfServer --sim --sim-valve-move <ms|-1>
 *                                       覆盖阀行程：-1=卡死（验证"动作超时
 *                                       未到位"告警）；25000 等超 20s 的值
 *                                       同样触发告警（对齐 Python
 *                                       server_py 的 --sim-valve-move）
 *               HsfServer --sim --sim-valve-no-feedback
 *                                       行程开关未接线（DI 恒 0，验证
 *                                       "无到位反馈（检查行程开关接线）"）
 *             部署参数（串口号/波特率/站号/模拟量地址）见 HsfServerConfig.h。
 *             事件循环内完成鉴权/轮询/推送；Ctrl+C 退出。
 */
#include "XCoreApplication.h"
#include "HsfServerSettings.h"
#include "HsfServerIo.h"
#include "HsfServerApp.h"
#include "XhsfLinkProto.h"
#include "XPrintf.h"
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(int argc, char** argv)
{
    XCoreApplication* app;
    unsigned port = 0;              /* 0=未指定，按配置文件解析 */
    bool portFromArgv = false;
    bool setupIo = true;
    bool simMode = false;
    uint32_t simValveMoveMs = 0;    /* 0=缺省行程 */
    bool simValveNoFeedback = false;
    int i;

    /* 诊断型控制台程序：stdout 去缓冲，保证管道/重定向下逐行可见。 */
    setvbuf(stdout, NULL, _IONBF, 0);

    for (i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "--no-io") == 0) {
            setupIo = false;
        } else if (strcmp(argv[i], "--sim") == 0) {
            simMode = true;
        } else if (strcmp(argv[i], "--sim-valve-move") == 0) {
            /* -1=阀卡死（UINT32_MAX 口径，见 HsfServerIo_setSimValveOverride）。 */
            if (i + 1 < argc) {
                ++i;
                simValveMoveMs = (strcmp(argv[i], "-1") == 0)
                                     ? UINT32_MAX
                                     : (uint32_t)atoi(argv[i]);
            }
        } else if (strcmp(argv[i], "--sim-valve-no-feedback") == 0) {
            simValveNoFeedback = true;
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
        /* [模拟模式] 必须早于 start 置位：装配时按它选客户端类型。 */
        if (simMode) {
            HsfServerIo_setSimMode(true);
            if (simValveMoveMs != 0 || simValveNoFeedback) {
                HsfServerIo_setSimValveOverride(simValveMoveMs,
                                                simValveNoFeedback);
                if (simValveMoveMs == UINT32_MAX)
                    XPrintf("HsfServer: --sim-valve-move -1：阀卡死"
                            "（验证 20s 超时告警）\n");
                else if (simValveMoveMs != 0)
                    XPrintf("HsfServer: --sim-valve-move %lu：阀行程覆盖\n",
                            (unsigned long)simValveMoveMs);
                if (simValveNoFeedback)
                    XPrintf("HsfServer: --sim-valve-no-feedback：行程开关"
                            "未接线（DI 恒 0）\n");
            }
            XPrintf("HsfServer: --sim：全部从站使用进程内模拟设备"
                    "（不打开串口/不连设备，写操作只改内存模型）\n");
        }
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
