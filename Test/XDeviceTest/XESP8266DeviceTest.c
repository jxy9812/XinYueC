#include "XIOTest.h"
#include "XESP8266Wifi.h"
#include "XATComm.h"
#include "XMemory.h"
#include "XMenu.h"
#include "XAction.h"
#include "XCoreApplication.h"
#include "XEventLoop.h"
#include "XPrintf.h"
#include "XByteArray.h"
#include "XSerialPort.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include "XDateTime.h"

// ========== 信号回调 ==========

static int g_responseCount = 0;
static int g_okCount = 0;
static int g_errorCount = 0;

static void onResponse(void* sender, XVarList* args)
{
    g_responseCount++;
    XVarList_args_1(args, char*, data);
    XPrintf("  [response] %s\n", data ? data : "(null)");
}

static void onOk(void* sender, XVarList* args)
{
    g_okCount++;
    XPrintf("  [OK]\n");
}

static void onError(void* sender, XVarList* args)
{
    g_errorCount++;
    XVarList_args_1(args, char*, msg);
    XPrintf("  [ERROR] %s\n", msg ? msg : "");
}

static void resetCounters(void)
{
    g_responseCount = 0;
    g_okCount = 0;
    g_errorCount = 0;
}

// ========== 通用串口打开 ==========

static XSerialPort* openSerial(const char* prompt)
{
    char portName[64];
    XPrintf("%s", prompt);
    if (scanf("%63s", portName) != 1 || portName[0] == '\0') {
#ifdef _WIN32
        strcpy(portName, "COM3");
#else
        strcpy(portName, "/dev/ttyUSB0");
#endif
        XPrintf("  使用默认串口: %s\n", portName);
    }

    XSerialPort* serial = XSerialPort_create();
    XSerialPort_setPortName(serial, portName);
    XSerialPort_setBaudRate(serial, XSerialPort_Baud115200, XSerialPort_AllDirections);
    XSerialPort_setDataBits(serial, XSerialPort_Data8);
    XSerialPort_setParity(serial, XSerialPort_NoParity);
    XSerialPort_setStopBits(serial, XSerialPort_OneStop);
    XSerialPort_setFlowControl(serial, XSerialPort_NoFlowControl);

    if (!XSerialPort_open_base((XIODevice*)serial, XIODevice_ReadWrite)) {
        XPrintf("❌ 无法打开串口 %s\n", portName);
        XSerialPort_delete_base(serial);
        return NULL;
    }
    XPrintf("  ✅ 串口已打开\n\n");
    return serial;
}

static void closeSerial(XSerialPort* serial)
{
    if (!serial) return;
    XSerialPort_close_base((XIODevice*)serial);
    XSerialPort_delete_base(serial);
}

// ========== 公共初始化 ==========

static XESP8266Wifi* initWifi(XSerialPort* serial)
{
    if (!serial) {
        serial = openSerial("输入串口名称 (如 COM3): ");
        if (!serial) return NULL;
    }

    XESP8266Wifi* wifi = XESP8266Wifi_create(serial);
    if (!wifi) {
        XPrintf_3("❌ XESP8266Wifi 创建失败\n");
        closeSerial(serial);
        return NULL;
    }

    resetCounters();
    XObject_connect_1(wifi, XESP8266Wifi_ok_signal, NULL, onOk, XConnectionType_Auto);
    XObject_connect_1(wifi, XESP8266Wifi_error_signal, NULL, onError, XConnectionType_Auto);
    XObject_connect_1(wifi, XESP8266Wifi_response_signal, NULL, onResponse, XConnectionType_Auto);

    // 1. 测试 AT 基本通信
    XPrintf("--- Test AT ---\n");
    bool ok = XESP8266Wifi_testAT(wifi, 3000);
    if (!ok) {
        XPrintf_3("❌ AT 测试失败，无法与 ESP8266 通信\n");
        XESP8266Wifi_deleteLater(wifi);
        closeSerial(serial);
        return NULL;
    }
    XPrintf("  ✅ AT 通信正常\n");

    // 2. 设置 STA 模式
    resetCounters();
    XPrintf("--- Set Mode STA ---\n");
    ok = XESP8266Wifi_setMode(wifi, XESP8266_Mode_STA, 3000);
    if (!ok) {
        XPrintf_3("❌ 设置 STA 模式失败\n");
        XESP8266Wifi_deleteLater(wifi);
        closeSerial(serial);
        return NULL;
    }
    XPrintf("  ✅ STA 模式已设置\n");

    return wifi;
}

// ========== TCP 客户端测试 ==========

/**
 * @brief TCP 客户端测试
 * @details 连接 WiFi → 连接远程服务器 → 收发数据 → 断开
 */
static void XESP8266WifiTest_tcpClient(void)
{
    XPrintf_3("\n========== ESP8266 TCP 客户端测试 ==========\n\n");

    XSerialPort* serial = openSerial("输入串口名称 (如 COM3): ");
    if (!serial) return;

    XESP8266Wifi* wifi = initWifi(serial);
    if (!wifi) return;

    // 3. 连接 WiFi
    char ssid[64], password[64];
    XPrintf("输入 WiFi SSID: ");
    scanf("%63s", ssid);
    XPrintf("输入 WiFi 密码: ");
    scanf("%63s", password);

    XPrintf("\n--- 连接 WiFi: %s ---\n", ssid);
    resetCounters();
    bool ok = XESP8266Wifi_connectWiFi(wifi, ssid, password, 15000);
    if (!ok) {
        XPrintf_3("❌ WiFi 连接失败\n");
        XESP8266Wifi_deleteLater(wifi);
        closeSerial(serial);
        return;
    }
    XPrintf("  ✅ WiFi 已连接\n");

    // 4. 连接远程 TCP 服务器
    char serverIp[64], serverPort[16];
    XPrintf("输入服务器 IP: ");
    scanf("%63s", serverIp);
    XPrintf("输入服务器端口: ");
    scanf("%15s", serverPort);

    uint16_t port = (uint16_t)atoi(serverPort);
    if (port == 0) {
        XPrintf("  使用默认端口: 80\n");
        port = 80;
    }

    XPrintf("\n--- 连接服务器: %s:%d ---\n", serverIp, port);
    resetCounters();
    int connId = XESP8266Wifi_connectServer(wifi, XESP8266_Protocol_TCP, serverIp, port, 0, 10000);
    if (connId < 0) {
        XPrintf_3("❌ 服务器连接失败\n");
        XESP8266Wifi_deleteLater(wifi);
        closeSerial(serial);
        return;
    }
    XPrintf("  ✅ 已连接到服务器 (connId=%d)\n", connId);

    // 5. 发送 HTTP 请求（如果连的是 80 端口）
    if (port == 80) {
        const char* httpReq = "GET / HTTP/1.0\r\nHost: hello\r\n\r\n";
        XPrintf("\n--- 发送 HTTP 请求 ---\n");
        size_t sent = XESP8266Wifi_write(wifi, connId, httpReq, strlen(httpReq), 5000);
        XPrintf("  发送 %zu 字节\n", (size_t)sent);
    }

    // 6. 接收数据
    XPrintf("\n--- 接收数据（等待 5 秒）---\n");
    {
        uint64_t deadline = XDateTime_currentMSecsSinceEpoch() + 50000;
        char recvBuf[1024];
        while (XDateTime_currentMSecsSinceEpoch() < deadline) {
            XCoreApplication_processEvents(XEventLoop_AllEvents);
            size_t avail = XESP8266Wifi_getBytesAvailable(wifi, connId);
            if (avail > 0) {
                size_t toRead = avail > sizeof(recvBuf) - 1 ? sizeof(recvBuf) - 1 : avail;
                size_t n = XESP8266Wifi_read(wifi, connId, recvBuf, toRead, 100);
                if (n > 0) {
                    recvBuf[n] = '\0';
                    XPrintf("  收到 %zu 字节:\n%s\n", (size_t)n, recvBuf);
                }
            }
        }
    }

    // 7. 断开连接
    XPrintf("\n--- 断开连接 ---\n");
    XESP8266Wifi_disconnectConn(wifi, connId, 3000);

    // 清理
    XPrintf("\n--- 清理 ---\n");
    XESP8266Wifi_disconnectWiFi(wifi, 5000);
    XESP8266Wifi_deleteLater(wifi);
    closeSerial(serial);

    XPrintf_3("\n✅ TCP 客户端测试完成\n");
}

// ========== TCP 服务器测试 ==========

/**
 * @brief TCP 服务器测试
 * @details 连接 WiFi → 开启 TCP 服务器 → 等待客户端连接 → 收发数据
 */
static void XESP8266WifiTest_tcpServer(void)
{
    XPrintf_3("\n========== ESP8266 TCP 服务器测试 ==========\n\n");

    XSerialPort* serial = openSerial("输入串口名称 (如 COM3): ");
    if (!serial) return;

    XESP8266Wifi* wifi = initWifi(serial);
    if (!wifi) return;

    // 3. 连接 WiFi
    char ssid[64], password[64];
    XPrintf("输入 WiFi SSID: ");
    scanf("%63s", ssid);
    XPrintf("输入 WiFi 密码: ");
    scanf("%63s", password);

    XPrintf("\n--- 连接 WiFi: %s ---\n", ssid);
    resetCounters();
    bool ok = XESP8266Wifi_connectWiFi(wifi, ssid, password, 15000);
    if (!ok) {
        XPrintf_3("❌ WiFi 连接失败\n");
        XESP8266Wifi_deleteLater(wifi);
        closeSerial(serial);
        return;
    }
    XPrintf("  ✅ WiFi 已连接\n");

    // 4. 获取本机 IP
    XPrintf("\n--- 获取本机 IP ---\n");
    resetCounters();
    XHostAddress* ip = XESP8266Wifi_getLocalIP(wifi, 3000);
    if (ip) {
        XString* ipStr = XHostAddress_toString(ip);
        XPrintf("  本机 IP: %s\n", XString_toUtf8(ipStr));
        XString_delete_base(ipStr);
        XFree_System(ip);
    } else {
        XPrintf("  IP 查询: ❌ 失败\n");
    }

    // 5. 开启 TCP 服务器
    XPrintf("\n--- 开启 TCP 服务器 (端口 8080) ---\n");
    resetCounters();
    ok = XESP8266Wifi_startServer(wifi, XESP8266_Protocol_TCP, 8080, 5000);
    if (!ok) {
        XPrintf_3("❌ 开启服务器失败\n");
        XESP8266Wifi_deleteLater(wifi);
        closeSerial(serial);
        return;
    }
    XPrintf("  ✅ TCP 服务器已开启 (端口 8080)\n");

    // 6. 等待客户端连接
    XPrintf("\n--- 等待客户端连接 (60 秒，按 Ctrl+C 退出) ---\n");
    {
        uint64_t deadline = XDateTime_currentMSecsSinceEpoch() + 60000;
        int lastConnCount = wifi->m_activeConnCount;
        while (XDateTime_currentMSecsSinceEpoch() < deadline) {
            XCoreApplication_processEvents(XEventLoop_AllEvents);
            if (wifi->m_activeConnCount > lastConnCount) {
                for (int i = 0; i < XESP8266_MAX_CONNS; i++) {
                    if (wifi->m_connections[i].status == XESP8266_Status_Connected &&
                        wifi->m_connections[i].isServer) {
                        XPrintf("  ✅ 客户端已连接 (connId=%d)\n", i);
                        lastConnCount = wifi->m_activeConnCount;

                        XPrintf("  等待接收数据...\n");
                        uint64_t recvDeadline = XDateTime_currentMSecsSinceEpoch() + 10000;
                        char recvBuf[1024];
                        while (XDateTime_currentMSecsSinceEpoch() < recvDeadline) {
                            XCoreApplication_processEvents(XEventLoop_AllEvents);
                            size_t avail = XESP8266Wifi_getBytesAvailable(wifi, i);
                            if (avail > 0) {
                                size_t toRead = avail > sizeof(recvBuf) - 1 ? sizeof(recvBuf) - 1 : avail;
                                size_t n = XESP8266Wifi_read(wifi, i, recvBuf, toRead, 100);
                                if (n > 0) {
                                    recvBuf[n] = '\0';
                                    XPrintf("  收到 %zu 字节:\n%s\n", (size_t)n, recvBuf);

                                    const char* reply = "Hello from ESP8266!\r\n";
                                    XESP8266Wifi_write(wifi, i, reply, strlen(reply), 3000);
                                    XPrintf("  已回复客户端\n");
                                }
                            }
                        }
                        break;
                    }
                }
            }
        }
    }

    // 7. 关闭服务器
    XPrintf("\n--- 关闭服务器 ---\n");
    XESP8266Wifi_stopServer(wifi, 5000);

    // 清理
    XPrintf("\n--- 清理 ---\n");
    XESP8266Wifi_disconnectWiFi(wifi, 5000);
    XESP8266Wifi_deleteLater(wifi);
    closeSerial(serial);

    XPrintf_3("\n✅ TCP 服务器测试完成\n");
}

// ========== 菜单注册 ==========

void XMenu_XESP8266WifiTest(XMenu* root)
{
    XMenu* menu = XMenu_create("XESP8266Wifi(AT指令WiFi)");
    XMenu_addMenu(root, menu);
    {
        XAction* action = XMenu_addAction(menu, "TCP 客户端测试");
        XAction_setAction(action, XESP8266WifiTest_tcpClient);
    }
    {
        XAction* action = XMenu_addAction(menu, "TCP 服务器测试");
        XAction_setAction(action, XESP8266WifiTest_tcpServer);
    }
}