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
#include "XSystem.h"
#include <stdio.h>
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
#include "XIndustryTest.h"
#include "XLinkTest.h"
#include "XLinkDemo.h"
#include "XNtpTest.h"
#include "XDdnsTest.h"
#include "XDataTest.h"
#include "XPropertyTest.h"
#include "XTestCommand.h"
#include "XCodeTest.h"
#include "XUpgrade.h"
#if XGUI_ON
#include "xq_align_run.h"
#endif
#if XCONSOLE_SHELL_ON && XCONSOLE_SHELL_COMMAND_ON && XCONSOLE_SHELL_IO_ON && \
    XCONSOLE_SHELL_ASYNC_ON
#include "XConsoleShell.h"
#include "XAbstractEventDispatcher.h"
#endif

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* 内存账本（门禁用）：XMemory_statistics 给出「系统堆在用」与「池已分配给用户」
   两个口径——后者**不含池的空闲块保留**，正是区分「池保留」与「用户真泄漏」
   的干净判据（LSan 会把池空闲块也报成 leak，噪声大）。 */
#include "XMemory.h"


/* [qt-align 参数透传 2026-11-12（远端 a4d9af05 合规扫描·main.c runner 死参数根修）]
   `--test qt-align` 语境下 runner 专属选项的判定口径——与
   Test/QtAlign/xq_align_run.c 的解析分支逐条同源（--list/--all 精确匹配；
   --filter=/--shard=/--log=/--leak-tolerance= 前缀匹配）。宿主 parser 未注册
   这些选项：直接喂进去 parse() 即报「未知选项」退出；裸 --list 更会抢先命中
   宿主「列出测试命令」分支（main() 里 list 检查先于 test 检查）。故检出
   qt-align 派发时由 main() 预扫收进子 argv 透传给 xq_align_run，并从喂给
   parser 的 XStringList 剔除；非 qt-align 路径不启用剔除，宿主 -l/--list
   语义零变化。 */
static bool main_isQtAlignRunnerOption(const char* arg)
{
    if (!arg) return false;
    return strcmp(arg, "--list") == 0 ||
           strcmp(arg, "--all") == 0 ||
           strncmp(arg, "--filter=", 9) == 0 ||
           strncmp(arg, "--shard=", 8) == 0 ||
           strncmp(arg, "--log=", 6) == 0 ||
           strncmp(arg, "--leak-tolerance=", 17) == 0;
}

/* 预扫原始 args：检出 qt-align 派发请求（--test qt-align / --test=qt-align /
   -t qt-align / -t=qt-align 任一形式；后两种=号拆分与 XCommandLineParser 的
   --key=value 及短选项取值口径一致，见 XCommandLineParser.c parse()）。 */
static bool main_qtAlignRequested(int argc, char* args[])
{
    int i;
    for (i = 1; i < argc; ++i) {
        if (strcmp(args[i], "--test") == 0 || strcmp(args[i], "-t") == 0) {
            if (i + 1 < argc && strcmp(args[i + 1], "qt-align") == 0)
                return true;
        } else if (strcmp(args[i], "--test=qt-align") == 0 ||
                   strcmp(args[i], "-t=qt-align") == 0) {
            return true;
        }
    }
    return false;
}

static int main_run_test_dispatch(const char* testPath, int subArgc, char** subArgv);

static int main_run_test_path(const char* testPath, int subArgc, char** subArgv)
{
    /* [内存账本门禁] 派发前后各取一次 XMemory 统计：差值即本次测试的净遗留。
       口径：systemBytes=系统堆在用（malloc 未 free）；poolUsedBytes=内存池已
       分配给用户的字节（**不含池的空闲块保留**）。两者任一为正 ⇒ 真泄漏。
       开关 XTEST_MEMSTAT=1（默认关，不干扰既有测试输出与解析）。 */
    const bool memStat = (getenv("XTEST_MEMSTAT") != NULL);
    XMemoryStatistics before;
    int rc;
    if (memStat) {
        XMemory_setStatisticsEnabled(true);
        before = XMemory_statistics(XMEMORY_TYPE_HYBRID);
    }
    rc = main_run_test_dispatch(testPath, subArgc, subArgv);
    if (memStat) {
        XMemoryStatistics after = XMemory_statistics(XMEMORY_TYPE_HYBRID);
        XPrintf("[memstat] sysDelta=%lld poolDelta=%lld poolTotal=%llu sysPeak=%llu\n",
                (long long)(after.systemBytes - before.systemBytes),
                (long long)(after.poolUsedBytes - before.poolUsedBytes),
                (unsigned long long)after.poolTotalBytes,
                (unsigned long long)after.systemPeakBytes);
    }
    return rc;
}

static int main_run_test_dispatch(const char* testPath, int subArgc, char** subArgv)
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
    /* XJson/XBson 全 API 组合与所有权/泄漏回归（逐场景堆在用字节数回基线）。 */
    if (strcmp(testPath, "xjson-bson") == 0)
        return XJsonBsonCoverageTest_run() == 0 ? 0 : 1;
#if DEMOTEST_ON && XPROPERTY_ON
    if (strcmp(testPath, "xproperty") == 0)
    {
        XPropertyTestAll();
        return 0;
    }
#endif
#if XGUI_ON
    /* Qt 6.8 对齐测试正式入口（Test/QtAlign 自注册用例；无独立 main 可执行）。
       [2026-11-12 整改] runner 选项（--list/--all/--filter=/--shard=/--log=
       /--leak-tolerance=）由 main() 预扫收进子 argv 后经 (subArgc, subArgv)
       透传——此前写死 (0, NULL)，runner 的全部命令行旋钮都是死参数（对齐
       测试失败无法 --filter=Tag 秒级隔离）。子 argv[0]=程序名占位，runner
       自 argv[1] 起解析；无透传参数时 main() 传 (0, NULL)，与旧行为逐位
       等价；交互 Test 菜单路径（xq_align_menu.c）不经此处，仍 (0, NULL)。 */
    if (strcmp(testPath, "qt-align") == 0)
        return xq_align_run(subArgc, subArgv);
#endif
    if (strcmp(testPath, "xprocess") == 0)
        return XProcessTest_runAll() ? 0 : 1;
#if DEMOTEST_ON && XSETTINGS_ON
    /* XSettings（对标 Qt 6.8 QSettings）全量测试（含内存平衡口径）。 */
    if (strcmp(testPath, "xsettings") == 0)
        return XSettingsTest_runAll() ? 0 : 1;
#endif
#if DEMOTEST_ON
    /* XStateMachine（对标 Qt 6.8 QStateMachine）全 API 组合矩阵。 */
    if (strcmp(testPath, "xstatemachine") == 0)
        return XStateMachineTest_runAll() ? 0 : 1;
    /* XAnimation（对标 Qt 6.8 QAbstractAnimation 族）全量测试。 */
    if (strcmp(testPath, "xanimation") == 0)
        return XAnimationTest_runAll() == 0 ? 0 : 1;
#endif
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
    /* XVfd 从站级约束单测（Hub 单帧读上限 / 功能码组边界 / 只 FC06）：
       进程内假从站载体，**不依赖传输层**（无 socket/pty），返回码=失败数。 */
    if (strcmp(testPath, "xvfd-hub-limits") == 0)
        return XVfdHubLimitsTest_run() == 0 ? 0 : 1;
    /* XChannel 通道层专项单元测试（点容器/链路虚槽/地址映射边界/
       装配失败路径）：进程内假链路载体，不依赖传输层。 */
    if (strcmp(testPath, "xchannel") == 0)
        return XChannelTest_run() == 0 ? 0 : 1;
#endif
#if XPROTOCOL_ON && XMODBUS_ON
    /* Modbus 服务端 FC 全量覆盖（含读事件信号口径回归）：进程内自跑，
       无需外部虚拟从站，返回码=失败数。 */
    if (strcmp(testPath, "xmodbus-coverage") == 0)
        return XModbusCoverageTest_run() ? 0 : 1;
    /* XModbus 核心值类型组合（Pdu/Adu/DataUnit/CommEvent/DeviceIdent/
     * Reply）：纯逻辑、无传输依赖，任意平台直跑，返回码=失败数。 */
    if (strcmp(testPath, "xmodbus-core") == 0)
        return XModbusCoreTest_run() == 0 ? 0 : 1;
#endif
#if XPROTOCOL_ON && XMODBUS_CLIENT_ON
    /* 虚拟 Modbus 设备（进程内应答）：验证设备物理层可替换——真设备拆了
       也能跑通 Hub → 链路 → 通道组 → 设备站全链。返回码=失败数。 */
    if (strcmp(testPath, "xmodbus-virtual") == 0)
        return XModbusVirtualTest_run() == 0 ? 0 : 1;
    /* HSF C 端**总回归**（单文件入口）：框架 Modbus 四层根修（异常帧按
       失败/嵌套收口不丢帧/写失败不饿死读/按从站粒度新鲜度）+ 客户端历史
       应答列映射契约。进程内假从站载体，不依赖传输层。
       Python 端对照文件：HighSpeedFilter/client_py/tests/test_hsf_regression.py */
    if (strcmp(testPath, "hsf-regression") == 0)
        return HsfRegressionTest_run() == 0 ? 0 : 1;
    /* 历史页「图表缩放取数」专项：2026-11-09 已并入 hsf_regression_test.c，
     * 由总回归的 §H 调用（断言只此一份）。这个独立入口保留作单跑调试用，
     * 指向同一函数，行为与 `--test hsf-regression` 的 §H 一致。 */
    if (strcmp(testPath, "hsf-chart-zoom") == 0)
        return HsfChartZoomTest_run() == 0 ? 0 : 1;
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
#if XINDUSTRY_ON && XSOLENOID_VALVE_ON
    /* Industry 电磁阀状态机 mock 回归（原独立 main 可执行收编，2026-11-01；
       进程内自跑，无硬件依赖，返回码=失败数）。 */
    if (strcmp(testPath, "xindustry-valve") == 0)
        return XIndustryValveTest_run() == 0 ? 0 : 1;
#endif
#if XINDUSTRY_ON && (XTEMP_HUM_ON || XPRESS_DIFF_ON)
    /* Industry 温湿度变送器/压差计 mock 回归（通道组喂点进程内自跑，
       2026-11-04；含编解码往返与泄漏压测，返回码=失败数）。 */
    if (strcmp(testPath, "xindustry-sensor") == 0)
        return XIndustrySensorTest_run() == 0 ? 0 : 1;
#endif
#if XINDUSTRY_ON && XFLOW_METER_ON
    /* Industry 电磁流量计 mock 回归（通道组喂点进程内自跑；含 32 位拼装、
       单位/报警解析、编解码往返与泄漏压测，返回码=失败数）。 */
    if (strcmp(testPath, "xindustry-flowmeter") == 0)
        return XIndustryFlowMeterTest_run() == 0 ? 0 : 1;
    /* 服务端流量计站隔离单测（Hub 本地缓存驱动，不起串口/TCP）。 */
    if (strcmp(testPath, "hsf-io-flowmeter") == 0)
        return XHsfServerIoFlowMeterTest_run() == 0 ? 0 : 1;
    /* 客户端流量计 provider 隔离单测（注入 BSON，不走网络）。 */
    if (strcmp(testPath, "hsf-client-flowmeter") == 0)
        return XHsfClientFlowMeterProviderTest_run() == 0 ? 0 : 1;
#endif
#if XINDUSTRY_ON && XTURBIDITY_ON
    /* Industry 在线浊度仪 mock 回归（通道组喂点进程内自跑）。最高优先级的断言
       是**真机帧回归**：WZT-701A 手册 4.3.10.2 的 01 03 04 D70A 411F =
       9.99NTU，锁定「CDAB 低字在前 + IEEE754 float32」这条缺省口径；另有
       字序/格式/除数三个旋钮、Analog 二级标度、编解码往返与泄漏压测。 */
    if (strcmp(testPath, "xindustry-turbidity") == 0)
        return XIndustryTurbidityTest_run() == 0 ? 0 : 1;
#endif
#if XINDUSTRY_ON && XBUTTON_STATION_ON
    /* Industry 按钮组/操作柱 mock 回归（通道组喂触点进程内自跑；含急停安全侧
       口径、转换开关双触点矛盾、指示灯跟随与写失败沿，返回码=失败数）。 */
    if (strcmp(testPath, "xindustry-button") == 0)
        return XIndustryButtonStationTest_run() == 0 ? 0 : 1;
#endif

#if XINDUSTRY_ON && XAUTO_CONTROL_ON
    /* Industry 自动运行控制器 mock 回归（纯框架件：四板斧状态机/急停锁存/
       步进闸门/节流/报警队列/BSON-JSON 往返，返回码=失败数）。 */
    if (strcmp(testPath, "xindustry-autocontrol") == 0)
        return XAutoControlTest_run() == 0 ? 0 : 1;
#endif
#if XINDUSTRY_ON && XACCOUNT_ON && XACCESS_CONTROL_ON
    /* Industry 访问控制三件 mock 回归（账户 CRUD/管理员不变式/锁定策略/
       权限位图/登录准入/BSON-JSON 往返，返回码=失败数）。 */
    if (strcmp(testPath, "xindustry-access") == 0)
        return XAccessControlTest_run() == 0 ? 0 : 1;
#endif
#if XINDUSTRY_ON && XINDUSTRY_CHANNEL_ON
    /* 电磁阀组反馈新鲜度语义（纯业务层，不需要从站）：模块离线必须报
       "无法控制"，不得伪装成"无到位反馈（检查行程开关接线）"，也不得
       伪造"动作中 → 动作超时未到位"。 */
    if (strcmp(testPath, "hsf-io-valve-fresh") == 0)
        return XHsfServerIoValveFreshTest_run() == 0 ? 0 : 1;
    /* 服务端模拟设备模式（HsfServer --sim）：多从站按站号分派 / VFD 整段
       应答 / 静止量心跳拍 / 32 位量低字非 0，外加阀控端到端。 */
    if (strcmp(testPath, "hsf-sim-mode") == 0)
        return XHsfServerIoSimModeTest_run() == 0 ? 0 : 1;
#endif
#if XINDUSTRY_ON && XINDUSTRY_CHANNEL_ON && XINDUSTRY_MODBUS_ON
    /* Industry Modbus 适配器闭环（TCP 环回进程内自跑；RTU 组合经
       INDUSTRY_RTU_PTY 环境变量由驱动脚本驱动，返回码=失败数）。 */
    if (strcmp(testPath, "xindustry-modbus") == 0)
        return XIndustryModbusTest_run() == 0 ? 0 : 1;
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
#if DEMOTEST_ON
    /* XTcpServer 全用例回归（含 2026-11-04 新增的「同一连接多帧收发 /
     * 连接 socket 所有权 / pending 队列单次投递」三把锁）。 */
    if (strcmp(testPath, "xtcpserver") == 0)
        return XTcpServerTest_run() == 0 ? 0 : 1;
    /* 套接字「读挂起后不再收数据」专项（长空闲/高频/背压/双向/重连/串包）。 */
    if (strcmp(testPath, "xsocket-stall") == 0)
        return XSocketStallTest_run() == 0 ? 0 : 1;
    /* bindAny 地址族选择 / IPv4-only 回退（2026-11-07 A33 无 IPv6 根修）：
     * 双栈优先保持 + XNET_DISABLE_IPV6=1 强制回退 0.0.0.0 + 回退后真实收发。 */
    if (strcmp(testPath, "udp-bind-family") == 0)
        return XUdpBindFamilyTest_run() == 0 ? 0 : 1;
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
    XPrintf("  --test xjson-bson (XJson/XBson 全 API 组合 + 所有权/泄漏回归)\n");
    XPrintf("  --test qt-align (Qt 6.8 对齐测试全套，Test/QtAlign 自注册用例)\n");
    XPrintf("  --test xsettings\n");
    XPrintf("  --test xstatemachine\n");
    XPrintf("  --test xanimation\n");
    XPrintf("  --test xlink (功能) / xlink-quality (质量) / xlink-demo (案例联调)\n");
    XPrintf("  --test xlink-demo-server + xlink-demo-client (双进程联调)\n");
    XPrintf("  --test xlink-all (全功能闸门) / all (全仓闸门)\n");
    XPrintf("  --test xlink-client (客户端包装·自动重连)\n");
    XPrintf("  --test xmodbus-hub / xprotocol-campaign (需先起虚拟从站)\n");
    XPrintf("  --test xmodbus-coverage (Modbus 服务端 FC 全量覆盖，进程内自跑)\n");
    XPrintf("  --test xvfd-m512 / xvfd-bus485 / xvfd-atv61 (需先起虚拟从站)\n");
    XPrintf("  --test xvfd-hub-limits (Hub 从站级约束单测, 进程内自跑)\n");
    XPrintf("  --test xindustry-valve (电磁阀 mock) / xindustry-modbus (TCP 环回；RTU 组合需脚本起从站)\n");
    XPrintf("  --test xindustry-sensor (温湿度/压差计 mock) / xindustry-flowmeter (电磁流量计 mock)\n");
    XPrintf("  --test xindustry-button (按钮组/操作柱 mock：急停/启动/停止/复位 + 手自动 + 指示灯)\n");
    XPrintf("  --test xindustry-autocontrol (自动运行控制器 mock：四板斧状态机/急停锁存/报警队列)\n");
    XPrintf("  --test xindustry-access (访问控制 mock：账户/权限/登录策略)\n");
    XPrintf("  --test xindustry-turbidity (在线浊度仪 mock：真机帧回归 + 字序/格式/除数 + Analog 二级标度)\n");
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
    /* [qt-align 参数透传 2026-11-12] 检出 `--test qt-align` 时收 runner 专属
       参数的子 argv（仅存 args[] 原串指针；subArgc==0 即旧行为）。 */
    int subArgc = 0;
    char** subArgv = NULL;

#if XUPGRADE_ON
    /* XUpgrade 接线：--xupgrade-apply 分流到 apply 桩（该桩无头文件，extern 原型见 XUpgradeApplyStub.c） */
    extern int XUpgradeApplyStub_main(int argc, char* argv[]);
    for (int i = 1; i < argc; ++i)
        if (strcmp(args[i], "--xupgrade-apply") == 0)
            return XUpgradeApplyStub_main(argc, args);
    XUpgrade_selfCheck(); /* 启动自检（GUI 与引擎初始化之前）：失败内部自动回滚，不阻塞启动 */
#endif

    setvbuf(stdout, NULL, _IONBF, 0);

    /* 【计时精度】向框架请求 1ms 级等待粒度，进程期内一直持有。
       Windows 系统默认粒度 15.625ms，会让 Sleep(2) 实际睡 15.4ms（实测），
       所有 2ms 级轮询的耗时预算被放大约 7.7 倍；XLink 的 4MB 传输正是因此
       看起来「跑不完」。平台实现与引用计数都在框架里（XSystem 域），应用
       不该自己调 timeBeginPeriod / NtSetTimerResolution。 */
    (void)XSystem_requestTimerPrecision();

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

    /* [qt-align 参数透传 2026-11-12] 预扫原始 args（喂 parser 之前）：
       检出 `--test qt-align` 派发请求时，把 runner 专属参数收进子 argv
       （subArgv[0]=程序名占位，runner 自 argv[1] 起解析；指针只借 args[]
       原串，无拷贝无所有权），并从下方喂给 parser 的清单剔除——否则
       parser 报「未知选项」退出、裸 --list 抢先命中宿主列表分支。检出但
       无 runner 参数时不建子 argv，传 (0, NULL) 与旧行为逐位等价；
       分配失败（极端兜底）同样退回旧行为。 */
    if (main_qtAlignRequested(argc, args)) {
        int j;
        subArgv = (char**)malloc(sizeof(char*) * (size_t)argc);
        if (subArgv) {
            subArgv[0] = args[0];
            subArgc = 1;
            for (j = 1; j < argc; ++j) {
                if (main_isQtAlignRunnerOption(args[j]))
                    subArgv[subArgc++] = args[j];
            }
            if (subArgc == 1) {
                /* 无 runner 参数：(0, NULL) 与 (1, {argv0}) 等价，省一次
                   透传；数组即刻归还（剔除闸见下，同样自动关闭）。 */
                free(subArgv);
                subArgv = NULL;
                subArgc = 0;
            }
        }
    }

    if (argc > 1) {
        arguments = XStringList_create();
        if (!arguments) {
            free(subArgv);
            XCommandLineParser_delete(parser);
            XClassDelete(app);
            return 1;
        }
        for (int i = 0; i < argc; ++i) {
            /* [qt-align 参数透传] 检出 qt-align 派发时剔除 runner 专属参数
               （判定见 main_isQtAlignRunnerOption 注释）；其余语境原样喂。 */
            if (subArgv && i > 0 && main_isQtAlignRunnerOption(args[i]))
                continue;
            XStringList_push_back_utf8(arguments, args[i]);
        }
        if (!XCommandLineParser_parse(parser, arguments)) {
            XPrintf("错误: %s\n", XCommandLineParser_errorText(parser));
            XPrintf("使用 --help 查看帮助信息。\n");
            free(subArgv);
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
            free(subArgv);
            XClassDelete(arguments);
            XCommandLineParser_delete(parser);
            XClassDelete(app);
            return 0;
        }
        if (XCommandLineParser_isSet_2(parser, "list") ||
            XCommandLineParser_isSet_2(parser, "l")) {
            main_print_test_list();
            free(subArgv);
            XClassDelete(arguments);
            XCommandLineParser_delete(parser);
            XClassDelete(app);
            return 0;
        }
        if (XCommandLineParser_isSet_2(parser, "test") ||
            XCommandLineParser_isSet_2(parser, "t")) {
            result = main_run_test_path(XCommandLineParser_value_2(parser, "test"),
                                        subArgc, subArgv);
            free(subArgv);
            XClassDelete(arguments);
            XCommandLineParser_delete(parser);
            XClassDelete(app);
            return result;
        }
        XClassDelete(arguments);
    }
    free(subArgv);
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
