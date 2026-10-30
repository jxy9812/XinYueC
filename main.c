/**
 * @file main.c
 * @brief XinYueC 默认控制台入口。
 * @details
 * 无命令行参数时启动 XConsoleShell，通过标准输入输出驱动命令循环；
 * 旧的交互式 XMenuTest 不再作为默认入口，测试统一由 Test 命令调度。
 * --test 选项保留用于构建脚本和自动化测试兼容；交互退出使用 Shell 的 exit 命令。
 * Shell 输入由事件调度器非阻塞轮询，默认 Shell 生命周期由事件调度器托管。
 */

#include "CXinYueConfig.h"
#include "XCoreApplication.h"
#include "XCommandLineParser.h"
#include "XCommandLineOption.h"
#include "XString.h"
#include "XStringList.h"
#include "XPrintf.h"
#include "XDeviceTest.h"
#include "XIOTest.h"
#include "XDeviceFileTest.h"
#include "XDeviceNetworkTest.h"
#include "XDeviceSerialPortTest.h"
#include "XProcessTest.h"
#include "XConsoleShellTest.h"
#include "XConsoleShellBackendTest.h"
#include "XSshTelnetClientTest.h"
#include "XCryptographicPrimitiveTest.h"
#include "XSslTest.h"
#include "XProtocolTest.h"
#include "XLinkTest.h"
#include "XLinkDemo.h"
#include "XNtpTest.h"
#include "XDdnsTest.h"
#include "XDataTest.h"
#include "XPropertyTest.h"
#include "XTestCommand.h"
#include "XCodeTest.h"
#include "XUpgrade.h"
#if XCONSOLE_SHELL_ON && XCONSOLE_SHELL_COMMAND_ON && XCONSOLE_SHELL_IO_ON && \
    XCONSOLE_SHELL_ASYNC_ON
#include "XConsoleShell.h"
#include "XAbstractEventDispatcher.h"
#endif

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int main_run_test_path(const char* testPath)
{
    if (!testPath) return 1;
    if (strcmp(testPath, "esp8266-auto") == 0) {
        const char* port = getenv("XESP8266_PORT");
        const char* ssid = getenv("XESP8266_SSID");
        const char* password = getenv("XESP8266_PASSWORD");
        return XESP8266WifiTest_runAutomated(port, ssid, password);
    }
    if (strcmp(testPath, "esp8266-unit") == 0)
        return XESP8266WifiTest_runUnit();
#if DEMOTEST_ON
    if (strcmp(testPath, "eventloop") == 0) {
        /* sendPostedEvents 所有权/泄漏探针（ESP8266 慢速泄漏排查桌面闸门）。 */
        XCoreApplicationTest_runSendPostedEventsProbe();
        return 0;
    }
    if (strcmp(testPath, "xcore-application") == 0) {
        XCoreApplicationTest(NULL);
        return 0;
    }
#endif
    if (strcmp(testPath, "xdevice-file") == 0)
        return XDeviceFileTest_runAll() ? 0 : 1;
    if (strcmp(testPath, "xdevice-network") == 0)
        return XDeviceNetworkTest_runAll() ? 0 : 1;
    /* UDP 定向收发红线：驱动 UDP 目的地址/connect/源地址修复的回归闸门。 */
    if (strcmp(testPath, "udp-directed") == 0)
        return XUdpDirectedTest_run() == 0 ? 0 : 1;
    if (strcmp(testPath, "xdevice-serial-port") == 0)
        return XDeviceSerialPortTest_runAll() ? 0 : 1;
    if (strcmp(testPath, "xdata") == 0)
        return XDataTest_runAll() ? 0 : 1;
#if DEMOTEST_ON && XPROPERTY_ON
    if (strcmp(testPath, "xproperty") == 0)
    {
        XPropertyTestAll();
        return 0;
    }
#endif
    if (strcmp(testPath, "xprocess") == 0)
        return XProcessTest_runAll() ? 0 : 1;
    if (strcmp(testPath, "xconsole-shell") == 0)
        return XConsoleShellTest_runAll() ? 0 : 1;
    if (strcmp(testPath, "xconsole-shell-backend") == 0)
        return XConsoleShellBackendTest_runAll() ? 0 : 1;
    if (strcmp(testPath, "ssh-telnet-client") == 0)
        return XSshTelnetClientTest_runAll() ? 0 : 1;
    if (strcmp(testPath, "xcryptographic") == 0)
        return XCryptographicPrimitiveTest_runAll() ? 0 : 1;
    if (strcmp(testPath, "xssl") == 0)
        return XSslTest_runAll() ? 0 : 1;
    if (strcmp(testPath, "xmqtt") == 0)
        return XMqttTest_runAll() ? 0 : 1;
    if (strcmp(testPath, "xmqtt-tcp-api") == 0)
        return XMqttTcpServerApiUnitTest_run() ? 0 : 1;
    if (strcmp(testPath, "xmqtt-unit") == 0)
        return XMqttServerUnitTest_run() ? 0 : 1;
    if (strcmp(testPath, "xmqtt-tcp-server") == 0)
        return XMqttTcpServerProcess_run() ? 0 : 1;
    if (strcmp(testPath, "xmqtt-tcp-client") == 0)
        return XMqttTcpClientProcess_run() ? 0 : 1;
    if (strcmp(testPath, "xmqtt-tcp-interop") == 0)
        return XMqttTcpInteropTest_run() ? 0 : 1;
#if XPROTOCOL_ON && XMODBUS_HUB_ON
    /* XModbus 聚合设备闭环 + 485 战役（原独立 main 可执行收编，2026-10-29；
       运行前须由驱动脚本起好对应虚拟从站，返回码=失败数）。 */
    if (strcmp(testPath, "xmodbus-hub") == 0)
        return XModbusHubTest_run() == 0 ? 0 : 1;
    if (strcmp(testPath, "xprotocol-campaign") == 0)
        return XProtocolCampaignTest_run() == 0 ? 0 : 1;
#endif
#if XPROTOCOL_ON && XMODBUS_ON
    /* Modbus 服务端 FC 全量覆盖（含读事件信号口径回归）：进程内自跑，
       无需外部虚拟从站，返回码=失败数。 */
    if (strcmp(testPath, "xmodbus-coverage") == 0)
        return XModbusCoverageTest_run() == 0 ? 0 : 1;
#endif
#if XPROTOCOL_ON && XVFD_ON
    /* XVfd 变频器闭环三件套（pty 路径走缺省文件回退）。 */
    if (strcmp(testPath, "xvfd-m512") == 0)
        return XVfdM512Test_run() == 0 ? 0 : 1;
    if (strcmp(testPath, "xvfd-bus485") == 0)
        return XVfdBus485Test_run() == 0 ? 0 : 1;
    if (strcmp(testPath, "xvfd-atv61") == 0)
        return XVfdAtv61Test_run() == 0 ? 0 : 1;
#endif
#if XPROTOCOL_ON && XLINK_ON
    /* XLink 私有应用层协议（控制进程 <-> GUI 进程 消息互通）全量回归。 */
    if (strcmp(testPath, "xlink") == 0)
        return XLinkTest_run() == 0 ? 0 : 1;
    /* 质量门禁：内存泄漏 / 性能基准 / 边界与对抗。 */
    if (strcmp(testPath, "xlink-quality") == 0)
        return XLinkQualityTest_run() == 0 ? 0 : 1;
    /* 实战案例·双进程联调：本进程扮演服务端（或客户端），另一个终端跑对端。 */
    if (strcmp(testPath, "xlink-demo-server") == 0)
        return XLinkDemoServer_run();
    if (strcmp(testPath, "xlink-demo-client") == 0)
        return XLinkDemoClient_run();
    /* 实战案例·本进程联调：两个角色同进程跑真实 TCP 环回（CI 友好）。 */
    if (strcmp(testPath, "xlink-demo") == 0)
        return XLinkDemo_run();
    /* 全功能闸门：功能 + 质量 + 案例联调（发布前跑这一个即可）。 */
    if (strcmp(testPath, "xlink-all") == 0)
        return (XLinkTest_run() == 0 && XLinkQualityTest_run() == 0
                && XLinkDemo_run() == 0) ? 0 : 1;
    if (strcmp(testPath, "xlink-codec") == 0) {
        XLinkCodecTest();
        return 0;
    }
    if (strcmp(testPath, "xlink-session") == 0) {
        XLinkSessionTest();
        return 0;
    }
    /* XLink 客户端包装（自动重连）：真实 TCP 环回全链路
       （原 XLinkClientTest.c 已并入 XLinkTest.c 分组十四，声明在 XLinkTest.h）。 */
#if XLINK_CLIENT_ON
    if (strcmp(testPath, "xlink-client") == 0)
        return XLinkClientTest_run() == 0 ? 0 : 1;
#endif
#endif
#if XPROTOCOL_ON && XNTP_ON
    /* XNtp SNTP 客户端全量回归（离线：进程内模拟 NTP 服务器）。 */
    if (strcmp(testPath, "ntp") == 0)
        return XNtpTest_run() == 0 ? 0 : 1;
    /* 真实网络校时联调（对 ntp.aliyun.com 发真实请求）。 */
    if (strcmp(testPath, "ntp-real") == 0)
        return XNtpRealTest_run() == 0 ? 0 : 1;
#endif
#if XPROTOCOL_ON && XDDNS_ON
    /* XDdns 动态域名解析全量回归（离线：本地 mock 服务器扮演阿里云）。 */
    if (strcmp(testPath, "ddns") == 0)
        return XDdnsTest_run() == 0 ? 0 : 1;
    /* 真实网络全链路联调（凭据经 XDNS_AKID/XDNS_SECRET 环境变量传入）。 */
    if (strcmp(testPath, "ddns-real") == 0)
        return XDdnsRealTest_run() == 0 ? 0 : 1;
#endif
#if DEMOTEST_ON && XS7_ON
    if (strcmp(testPath, "xs7-unit") == 0)
        return XS7Test_runAll() ? 0 : 1;
    /* S7 集成测试：目标 PLC IP 由环境变量 XS7_PLC_IP 指定（默认 192.168.1.251） */
    if (strcmp(testPath, "xs7-plc") == 0)
        return XS7Test_integration_run() ? 0 : 1;
#endif
    if (strcmp(testPath, "all") == 0) {
        bool esp8266 = XESP8266WifiTest_runUnit() == 0;
        bool deviceFile = XDeviceFileTest_runAll();
        bool deviceNetwork = XDeviceNetworkTest_runAll();
        bool deviceSerialPort = XDeviceSerialPortTest_runAll();
        bool data = XDataTest_runAll();
        bool process = XProcessTest_runAll();
        bool shell = XConsoleShellTest_runAll();
        bool backend = XConsoleShellBackendTest_runAll();
        bool clients = XSshTelnetClientTest_runAll();
        bool cryptographic = XCryptographicPrimitiveTest_runAll();
        bool ssl = XSslTest_runAll();
        bool mqtt = XMqttTest_runAll();
        bool ok = esp8266 && deviceFile && deviceNetwork && deviceSerialPort && data && process && shell && backend && clients && cryptographic && ssl && mqtt;
#if XPROTOCOL_ON && XLINK_ON
        /* XLink 私有协议回归并入全量闸门：功能 + 质量 + 案例联调。 */
        ok = ok && (XLinkTest_run() == 0);
        ok = ok && (XLinkQualityTest_run() == 0);
        ok = ok && (XLinkDemo_run() == 0);
#endif
        return ok ? 0 : 1;
    }
    XPrintf("未知测试命令: %s\n", testPath);
    return 1;
}

static void main_print_test_list(void)
{
    XPrintf("可用测试命令:\n");
    XPrintf("  Test xprocess\n");
    XPrintf("  Test xconsole-shell\n");
    XPrintf("  Test xconsole-shell-backend\n");
    XPrintf("  Test ssh-telnet-client\n");
    XPrintf("  Test xcryptographic\n");
    XPrintf("  Test xssl\n");
    XPrintf("  Test xmqtt\n");
    XPrintf("  --test xs7-unit\n");
    XPrintf("  --test xs7-plc\n");
    XPrintf("  Test all\n");
    XPrintf("  --test esp8266-unit\n");
    XPrintf("  --test esp8266-auto\n");
    XPrintf("  --test xdevice-file\n");
    XPrintf("  --test xdevice-network\n");
    XPrintf("  --test xdevice-serial-port\n");
    XPrintf("  --test xdata\n");
    XPrintf("  --test xlink (功能) / xlink-quality (质量) / xlink-demo (案例联调)\n");
    XPrintf("  --test xlink-demo-server + xlink-demo-client (双进程联调)\n");
    XPrintf("  --test xlink-all (全功能闸门) / all (全仓闸门)\n");
    XPrintf("  --test xlink-client (客户端包装·自动重连)\n");
    XPrintf("  --test xmodbus-hub / xprotocol-campaign (需先起虚拟从站)\n");
    XPrintf("  --test xmodbus-coverage (Modbus 服务端 FC 全量覆盖，进程内自跑)\n");
    XPrintf("  --test xvfd-m512 / xvfd-bus485 / xvfd-atv61 (需先起虚拟从站)\n");
    XPrintf("  Test 菜单「协议栈 → XLink(xlink)」: 客户端包装 / 实战案例·服务端 / 实战案例·客户端\n");
    XPrintf("  Test 菜单「协议栈 → XModbus/XVfd」: Hub闭环 / 485战役 / 变频器三件套\n");
}

int main(int argc, char* args[])
{
    XCoreApplication* app;
    XCommandLineParser* parser;
    XCommandLineOption* option;
    XStringList* arguments;
    int result = 0;

#if XUPGRADE_ON
    /* XUpgrade 接线：--xupgrade-apply 分流到 apply 桩（该桩无头文件，extern 原型见 XUpgradeApplyStub.c） */
    extern int XUpgradeApplyStub_main(int argc, char* argv[]);
    for (int i = 1; i < argc; ++i)
        if (strcmp(args[i], "--xupgrade-apply") == 0)
            return XUpgradeApplyStub_main(argc, args);
    XUpgrade_selfCheck(); /* 启动自检（GUI 与引擎初始化之前）：失败内部自动回滚，不阻塞启动 */
#endif

    setvbuf(stdout, NULL, _IONBF, 0);
    XPrintf("=== XinYueC 启动 ===\n");
    app = XCoreApplication_create(argc, args);
    if (!app) return 1;

    parser = XCommandLineParser_create();
    if (!parser) {
        XClassDelete(app);
        return 1;
    }
    XCommandLineParser_setApplicationDescription_2(parser, "XinYueC 控制台");
    option = XCommandLineOption_createFull_2("t", "直接运行指定测试", "test-path", NULL);
    if (!option) {
        if (option) XCommandLineOption_delete(option);
        XCommandLineParser_delete(parser);
        XClassDelete(app);
        return 1;
    }
    XCommandLineOption_addName_2(option, "test");
    if (!XCommandLineParser_addOption(parser, option)) {
        XCommandLineOption_delete(option);
        XCommandLineParser_delete(parser);
        XClassDelete(app);
        return 1;
    }
    XCommandLineOption_delete(option);
    option = XCommandLineOption_createFull_2("l", "列出测试命令", NULL, NULL);
    if (!option) {
        if (option) XCommandLineOption_delete(option);
        XCommandLineParser_delete(parser);
        XClassDelete(app);
        return 1;
    }
    XCommandLineOption_addName_2(option, "list");
    if (!XCommandLineParser_addOption(parser, option)) {
        XCommandLineOption_delete(option);
        XCommandLineParser_delete(parser);
        XClassDelete(app);
        return 1;
    }
    XCommandLineOption_delete(option);
    XCommandLineParser_addHelpOption(parser);

    if (argc > 1) {
        arguments = XStringList_create();
        if (!arguments) {
            XCommandLineParser_delete(parser);
            XClassDelete(app);
            return 1;
        }
        for (int i = 0; i < argc; ++i)
            XStringList_push_back_utf8(arguments, args[i]);
        if (!XCommandLineParser_parse(parser, arguments)) {
            XPrintf("错误: %s\n", XCommandLineParser_errorText(parser));
            XPrintf("使用 --help 查看帮助信息。\n");
            XClassDelete(arguments);
            XCommandLineParser_delete(parser);
            XClassDelete(app);
            return 1;
        }
        if (XCommandLineParser_isSet_2(parser, "help") ||
            XCommandLineParser_isSet_2(parser, "h")) {
            XString* help = XCommandLineParser_helpText(parser);
            if (help) {
                XPrintf("%s\n", XString_toUtf8(help));
                XClassDelete(help);
            }
            XClassDelete(arguments);
            XCommandLineParser_delete(parser);
            XClassDelete(app);
            return 0;
        }
        if (XCommandLineParser_isSet_2(parser, "list") ||
            XCommandLineParser_isSet_2(parser, "l")) {
            main_print_test_list();
            XClassDelete(arguments);
            XCommandLineParser_delete(parser);
            XClassDelete(app);
            return 0;
        }
        if (XCommandLineParser_isSet_2(parser, "test") ||
            XCommandLineParser_isSet_2(parser, "t")) {
            result = main_run_test_path(XCommandLineParser_value_2(parser, "test"));
            XClassDelete(arguments);
            XCommandLineParser_delete(parser);
            XClassDelete(app);
            return result;
        }
        XClassDelete(arguments);
    }
    XCommandLineParser_delete(parser);

#if XCONSOLE_SHELL_ON && XCONSOLE_SHELL_COMMAND_ON && XCONSOLE_SHELL_IO_ON && \
    XCONSOLE_SHELL_ASYNC_ON
    {
        XAbstractEventDispatcher* dispatcher = XCoreApplication_eventDispatcher();
        XConsoleShell* shell = XAbstractEventDispatcher_consoleShell(dispatcher);
        if (shell)
            XConsoleShell_registerStaticCommands(shell, &XTestCommand, 1);
        result = XAbstractEventDispatcher_runDefaultShell(dispatcher);
    }
#else
    result = XCoreApplication_exec();
#endif
    XClassDelete(app);
    return result;
}
