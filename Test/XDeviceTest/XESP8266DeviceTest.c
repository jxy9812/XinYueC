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
#include "XTcpServer.h"
#include "XTcpSocket.h"
#include "XNetworkInterface.h"
#include "XThread.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include "XDateTime.h"

static XSerialPort* openSerialByName(const char* portName)
{
    if (!portName || portName[0] == '\0') return NULL;

    XSerialPort* serial = XSerialPort_create();
    if (!serial) return NULL;
    XSerialPort_setPortName(serial, portName);
    XSerialPort_setBaudRate(serial, XSerialPort_Baud115200, XSerialPort_AllDirections);
    XSerialPort_setDataBits(serial, XSerialPort_Data8);
    XSerialPort_setParity(serial, XSerialPort_NoParity);
    XSerialPort_setStopBits(serial, XSerialPort_OneStop);
    XSerialPort_setFlowControl(serial, XSerialPort_NoFlowControl);

    if (!XSerialPort_open_base((XIODevice*)serial, XIODevice_ReadWrite)) {
        XPrintf("  无法打开串口 %s (error=%d)\n", portName, (int)XSerialPort_error(serial));
        XSerialPort_delete_base(serial);
        return NULL;
    }
    XPrintf("  串口已打开: %s\n", portName);
    return serial;
}

// ========== 信号回调 ==========

static int g_responseCount = 0;
static int g_okCount = 0;
static int g_errorCount = 0;
static int g_wifiStatusChangedCount = 0;
static int g_serverStatusChangedCount = 0;
static int g_readyReadCount = 0;
static int g_connectCount = 0;
static int g_disconnectCount = 0;

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
    XVarList_args_2(args, int, errorCode, char*, msg);
    XPrintf("  [ERROR %d] %s\n", errorCode, msg ? msg : "");
}

static void onWifiStatusChanged(void* sender, XVarList* args)
{
    (void)sender;
    g_wifiStatusChangedCount++;
    XVarList_args_1(args, XESP8266WifiStatus, status);
    XPrintf("  [WiFi status] %d\n", (int)status);
}

static void onServerStatusChanged(void* sender, XVarList* args)
{
    (void)sender;
    g_serverStatusChangedCount++;
    XVarList_args_2(args, int, connId, XESP8266WifiStatus, status);
    XPrintf("  [server status] connId=%d status=%d\n", connId, (int)status);
}

static void onReadyRead(void* sender, XVarList* args)
{
    (void)sender;
    g_readyReadCount++;
    XVarList_args_1(args, int, connId);
    XPrintf("  [readyRead] connId=%d\n", connId);
}

static void onConnect(void* sender, XVarList* args)
{
    (void)sender;
    g_connectCount++;
    XVarList_args_1(args, int, connId);
    XPrintf("  [connect] connId=%d\n", connId);
}

static void onDisconnect(void* sender, XVarList* args)
{
    (void)sender;
    g_disconnectCount++;
    XVarList_args_1(args, int, connId);
    XPrintf("  [disconnect] connId=%d\n", connId);
}

static void resetCounters(void)
{
    g_responseCount = 0;
    g_okCount = 0;
    g_errorCount = 0;
    g_wifiStatusChangedCount = 0;
    g_serverStatusChangedCount = 0;
    g_readyReadCount = 0;
    g_connectCount = 0;
    g_disconnectCount = 0;
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

    return openSerialByName(portName);
}

static void closeSerial(XSerialPort* serial)
{
    if (!serial) return;
    XSerialPort_close_base((XIODevice*)serial);
    XSerialPort_delete_base(serial);
}

static bool readTextInput(const char* prompt, char text[64])
{
    XPrintf("%s", prompt);
    return scanf(" %63[^\n]", text) == 1 && text[0] != '\0';
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
static void XESP8266WifiTest_tcpClient(XVariant* data)
{
    (void)data;
    XPrintf_3("\n========== ESP8266 TCP 客户端测试 ==========\n\n");

    XSerialPort* serial = openSerial("输入串口名称 (如 COM3): ");
    if (!serial) return;

    XESP8266Wifi* wifi = initWifi(serial);
    if (!wifi) return;

    // 3. 连接 WiFi
    char ssid[64], password[64];
    if (!readTextInput("输入 WiFi SSID: ", ssid) ||
        !readTextInput("输入 WiFi 密码: ", password)) {
        XPrintf_3("❌ WiFi 参数输入失败\n");
        XESP8266Wifi_deleteLater(wifi);
        closeSerial(serial);
        return;
    }

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
static void XESP8266WifiTest_tcpServer(XVariant* data)
{
    (void)data;
    XPrintf_3("\n========== ESP8266 TCP 服务器测试 ==========\n\n");

    XSerialPort* serial = openSerial("输入串口名称 (如 COM3): ");
    if (!serial) return;

    XESP8266Wifi* wifi = initWifi(serial);
    if (!wifi) return;

    // 3. 连接 WiFi
    char ssid[64], password[64];
    if (!readTextInput("输入 WiFi SSID: ", ssid) ||
        !readTextInput("输入 WiFi 密码: ", password)) {
        XPrintf_3("❌ WiFi 参数输入失败\n");
        XESP8266Wifi_deleteLater(wifi);
        closeSerial(serial);
        return;
    }

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

static bool automatedStep(const char* name, bool result)
{
    XPrintf("  [%s] %s\n", result ? "通过" : "失败", name);
    return result;
}

/* 用于协议测试的确定性 XIODevice。响应按写入顺序消费，可覆盖 CIPSEND 的
 * 提示符、载荷和 SEND OK 交互，无需向已连接的 ESP8266 发送破坏性命令。 */
XCLASS_DEFINE_BEGING(XMockAtIo)
XCLASS_DEFINE_EXTEND_END(XMockAtIo, XIODevice)

typedef struct XMockAtIo {
    XIODevice m_base;
    XByteArray* m_input;
    XByteArray* m_writes;
    size_t m_readOffset;
    const char* m_responses[48];
    size_t m_responseCount;
    size_t m_responseIndex;
    bool m_failWrites;
} XMockAtIo;

static bool mockAtIo_append(XByteArray* buffer, const char* data, size_t size)
{
    size_t oldSize;
    char* target;
    if (!buffer || (!data && size != 0)) return false;
    oldSize = XByteArray_size_base(buffer);
    if (!XByteArray_resize_base(buffer, oldSize + size)) return false;
    target = (char*)XByteArray_data(buffer);
    if (!target) return false;
    if (size) memcpy(target + oldSize, data, size);
    return true;
}

static int64_t VMockAtIo_bytesAvailable(const XIODevice* base)
{
    const XMockAtIo* io = (const XMockAtIo*)base;
    size_t size = io && io->m_input ? XByteArray_size_base(io->m_input) : 0;
    return size > io->m_readOffset ? (int64_t)(size - io->m_readOffset) : 0;
}

static int64_t VMockAtIo_readData(XIODevice* base, char* data, int64_t maxlen)
{
    XMockAtIo* io = (XMockAtIo*)base;
    size_t available;
    size_t count;
    const char* source;
    if (!io || !data || maxlen <= 0) return -1;
    available = (size_t)VMockAtIo_bytesAvailable(base);
    if (available == 0) return 0;
    count = available < (size_t)maxlen ? available : (size_t)maxlen;
    source = (const char*)XByteArray_data(io->m_input);
    memcpy(data, source + io->m_readOffset, count);
    io->m_readOffset += count;
    return (int64_t)count;
}

static int64_t VMockAtIo_writeData(XIODevice* base, const char* data, int64_t len)
{
    XMockAtIo* io = (XMockAtIo*)base;
    const char* response;
    if (!io || !data || len <= 0 || io->m_failWrites) return -1;
    if (!mockAtIo_append(io->m_writes, data, (size_t)len)) return -1;
    if (io->m_responseIndex >= io->m_responseCount) return len;
    response = io->m_responses[io->m_responseIndex++];
    if (response && !mockAtIo_append(io->m_input, response, strlen(response))) return -1;
    return len;
}

static void VMockAtIo_deinit(XIODevice* base)
{
    XMockAtIo* io = (XMockAtIo*)base;
    if (!io) return;
    if (io->m_input) {
        XByteArray_delete_base(io->m_input);
        io->m_input = NULL;
    }
    if (io->m_writes) {
        XByteArray_delete_base(io->m_writes);
        io->m_writes = NULL;
    }
    XClass_Deinit_Parent(XIODevice, base);
}

static XVtable* XMockAtIo_class_init(void)
{
    XVTABLE_CREAT_DEFAULT
#if VTABLE_ISSTACK
    XVTABLE_STACK_INIT_DEFAULT(XMockAtIo)
#else
    XVTABLE_HEAP_INIT_DEFAULT
#endif
    XVTABLE_INHERIT_XCLASS(XIODevice);
    XVTABLE_OVERLOAD_DEFAULT(EXIODevice_BytesAvailable, VMockAtIo_bytesAvailable);
    XVTABLE_OVERLOAD_DEFAULT(EXIODevice_ReadData, VMockAtIo_readData);
    XVTABLE_OVERLOAD_DEFAULT(EXIODevice_WriteData, VMockAtIo_writeData);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Deinit, VMockAtIo_deinit);
    return XVTABLE_DEFAULT;
}

static XMockAtIo* mockAtIo_create(void)
{
    XMockAtIo* io = XMalloc_System(sizeof(XMockAtIo));
    if (!io) return NULL;
    memset(io, 0, sizeof(*io));
    XIODevice_init(&io->m_base);
    XClassGetVtable(io) = XMockAtIo_class_init();
    io->m_input = XByteArray_create();
    io->m_writes = XByteArray_create();
    if (!io->m_input || !io->m_writes || !XIODevice_open_base(&io->m_base, XIODevice_ReadWrite)) {
        XClass_delete_base((XClass*)io);
        return NULL;
    }
    Set_Class_MemoryFree(io, XFree_System);
    return io;
}

static bool mockAtIo_queueResponse(XMockAtIo* io, const char* response)
{
    if (!io || io->m_responseCount >= sizeof(io->m_responses) / sizeof(io->m_responses[0])) return false;
    io->m_responses[io->m_responseCount++] = response;
    return true;
}

static bool mockAtIo_pushInput(XMockAtIo* io, const char* data)
{
    return io && data && mockAtIo_append(io->m_input, data, strlen(data));
}

static bool mockAtIo_writesContain(const XMockAtIo* io, const char* needle)
{
    const char* data;
    size_t size;
    size_t needleSize;
    size_t i;
    if (!io || !needle) return false;
    data = io->m_writes ? (const char*)XByteArray_data(io->m_writes) : NULL;
    size = io->m_writes ? XByteArray_size_base(io->m_writes) : 0;
    needleSize = strlen(needle);
    if (!data || needleSize == 0 || needleSize > size) return false;
    for (i = 0; i + needleSize <= size; ++i) {
        if (memcmp(data + i, needle, needleSize) == 0) return true;
    }
    return false;
}

static void mockAtIo_processWifi(XESP8266Wifi* wifi)
{
    if (wifi && wifi->m_base.m_io && XIODevice_bytesAvailable_base(wifi->m_base.m_io) > 0)
        XClassGetVirtualFunc(wifi, EXATComm_ProcessResponse, void(*)(XATComm*))(&wifi->m_base);
}

static int g_atResponseCount = 0;
static int g_atOkCount = 0;
static int g_atErrorCount = 0;
static int g_atTimeoutCount = 0;
static int g_atTimeoutOp = 0;

static void onAtResponse(void* sender, XVarList* args)
{
    (void)sender;
    (void)args;
    g_atResponseCount++;
}

static void onAtOk(void* sender, XVarList* args)
{
    (void)sender;
    (void)args;
    g_atOkCount++;
}

static void onAtError(void* sender, XVarList* args)
{
    (void)sender;
    (void)args;
    g_atErrorCount++;
}

static void onAtTimeout(void* sender, XVarList* args)
{
    (void)sender;
    XVarList_args_1(args, int, op);
    g_atTimeoutCount++;
    g_atTimeoutOp = op;
}

static void resetAtCounters(void)
{
    g_atResponseCount = 0;
    g_atOkCount = 0;
    g_atErrorCount = 0;
    g_atTimeoutCount = 0;
    g_atTimeoutOp = 0;
}

int XESP8266WifiTest_runUnit(void)
{
    bool allOk = true;
    XMockAtIo* atIo = NULL;
    XATComm* comm = NULL;
    XMockAtIo* wifiIo = NULL;
    XESP8266Wifi* wifi = NULL;

    XPrintf("\n========== XATComm / ESP8266 API 模拟回归 ==========" "\n");

    atIo = mockAtIo_create();
    comm = atIo ? XATComm_create((XIODevice*)atIo) : NULL;
    allOk &= automatedStep("创建 XATComm", atIo && comm &&
        XATComm_responseSize(comm) == 0);
    if (comm) {
        resetAtCounters();
        XObject_connect_2((XObject*)comm, (size_t)XATComm_response_signal, onAtResponse);
        XObject_connect_2((XObject*)comm, (size_t)XATComm_ok_signal, onAtOk);
        XObject_connect_2((XObject*)comm, (size_t)XATComm_error_signal, onAtError);
        XObject_connect_2((XObject*)comm, (size_t)XATComm_timeout_signal, onAtTimeout);

        allOk &= mockAtIo_queueResponse(atIo, "OK\r\n");
        allOk &= automatedStep("XATComm OK 与命令格式", XATComm_sendCommand(comm, "AT+PING", 71, 100) &&
            comm->m_currentOp == 0 && g_atResponseCount == 1 && g_atOkCount == 1 &&
            mockAtIo_writesContain(atIo, "AT+PING\r\n") && XATComm_responseSize(comm) > 0 &&
            XATComm_responseData(comm) && strstr(XATComm_responseData(comm), "OK"));
        XATComm_clearResponse(comm);
        allOk &= automatedStep("XATComm 清空响应", XATComm_responseSize(comm) == 0);

        atIo->m_failWrites = true;
        allOk &= automatedStep("XATComm 写失败清理状态", !XATComm_sendCommand(comm, "AT+NOWRITE", 70, 0) &&
            comm->m_currentOp == 0);
        atIo->m_failWrites = false;

        allOk &= mockAtIo_queueResponse(atIo, "ERROR\r\n");
        allOk &= automatedStep("XATComm ERROR 立即完成", !XATComm_sendCommand(comm, "AT+BAD", 72, 100) &&
            comm->m_currentOp == 0 && g_atErrorCount == 1);

        allOk &= mockAtIo_pushInput(atIo, "OK\r\n");
        allOk &= automatedStep("XATComm 等待已有操作", XATComm_sendCommand(comm, NULL, 73, 100) &&
            g_atOkCount == 2);

        allOk &= automatedStep("XATComm 超时信号", !XATComm_sendCommand(comm, "AT+TIMEOUT", 74, 80) &&
            comm->m_currentOp == 0 && g_atTimeoutCount == 1 && g_atTimeoutOp == 74);
    }
    if (comm) XClass_delete_base((XClass*)comm);
    if (atIo) XClass_delete_base((XClass*)atIo);

    wifiIo = mockAtIo_create();
    wifi = wifiIo ? XESP8266Wifi_create((XIODevice*)wifiIo) : NULL;
    allOk &= automatedStep("创建 XESP8266Wifi", wifi &&
        XESP8266Wifi_getMode(wifi) == XESP8266_Mode_STA &&
        XESP8266Wifi_getWiFiStatus(wifi) == XESP8266_Status_Disconnected &&
        XESP8266Wifi_getServerStatus(wifi) == XESP8266_Status_Disconnected);
    if (wifi) {
        XHostAddress* localIp;
        XString* localIpText;
        char readBuffer[8] = {0};
        int connectBefore;
        int disconnectBefore;
        int readyReadBefore;
        int statusBefore;
        bool multiConnOpPreserved;

        resetCounters();
        XObject_connect_2((XObject*)wifi, (size_t)XESP8266Wifi_ok_signal, onOk);
        XObject_connect_2((XObject*)wifi, (size_t)XESP8266Wifi_error_signal, onError);
        XObject_connect_2((XObject*)wifi, (size_t)XESP8266Wifi_response_signal, onResponse);
        XObject_connect_2((XObject*)wifi, (size_t)XESP8266Wifi_wifiStatusChanged_signal, onWifiStatusChanged);
        XObject_connect_2((XObject*)wifi, (size_t)XESP8266Wifi_serverStatusChanged_signal, onServerStatusChanged);
        XObject_connect_2((XObject*)wifi, (size_t)XESP8266Wifi_readyRead_signal, onReadyRead);
        XObject_connect_2((XObject*)wifi, (size_t)XESP8266Wifi_connect_signal, onConnect);
        XObject_connect_2((XObject*)wifi, (size_t)XESP8266Wifi_disconnect_signal, onDisconnect);

        allOk &= automatedStep("ESP 参数校验", !XESP8266Wifi_setMode(wifi, 0, 0) &&
            !XESP8266Wifi_configAP(wifi, "ap", "123", 0, XESP8266_Encrypt_WPA2_PSK, 0) &&
            !XESP8266Wifi_configAP(wifi, "ap", "123", 1, XESP8266_Encrypt_WPA2_PSK, 0) &&
            XESP8266Wifi_connectServer(wifi, (XESP8266WifiProtocol)9, "1.2.3.4", 80, 0, 0) < 0 &&
            XESP8266Wifi_connectServer(wifi, XESP8266_Protocol_TCP, "1.2.3.4", 0, 0, 0) < 0 &&
            !XESP8266Wifi_startServer(wifi, XESP8266_Protocol_TCP, 0, 0) &&
            XESP8266Wifi_write(wifi, 0, "", 0, 0) == 0 &&
            XESP8266Wifi_read(wifi, 9, readBuffer, sizeof(readBuffer), 0) == 0);

        allOk &= mockAtIo_queueResponse(wifiIo, "OK\r\n");
        allOk &= automatedStep("ESP AT/reset", XESP8266Wifi_testAT(wifi, 100) &&
            mockAtIo_writesContain(wifiIo, "AT\r\n"));
        allOk &= mockAtIo_queueResponse(wifiIo, "OK\r\n");
        allOk &= automatedStep("ESP 复位", XESP8266Wifi_reset(wifi, 100) &&
            XESP8266Wifi_getWiFiStatus(wifi) == XESP8266_Status_Disconnected);
        {
            int errorBefore = g_errorCount;
            allOk &= mockAtIo_queueResponse(wifiIo, "ERROR\r\n");
            allOk &= automatedStep("ESP ERROR 回调", !XESP8266Wifi_setMode(wifi, XESP8266_Mode_STA, 100) &&
                g_errorCount == errorBefore + 1);
        }
        allOk &= mockAtIo_queueResponse(wifiIo, "OK\r\n");
        allOk &= automatedStep("ESP 设置 AP 模式", XESP8266Wifi_setMode(wifi, XESP8266_Mode_AP, 100) &&
            XESP8266Wifi_getMode(wifi) == XESP8266_Mode_AP);
        allOk &= mockAtIo_queueResponse(wifiIo, "OK\r\n");
        allOk &= automatedStep("ESP 配置开放 AP", XESP8266Wifi_configAP(wifi, "UnitAP", NULL, 6,
            XESP8266_Encrypt_None, 100) && mockAtIo_writesContain(wifiIo, "AT+CWSAP=\"UnitAP\",\"\",6,0\r\n"));
        allOk &= mockAtIo_queueResponse(wifiIo, "OK\r\n");
        allOk &= automatedStep("ESP 配置 WPA2 AP", XESP8266Wifi_configAP(wifi, "UnitAP", "12345678", 11,
            XESP8266_Encrypt_WPA2_PSK, 100) && mockAtIo_writesContain(wifiIo, "AT+CWSAP=\"UnitAP\",\"12345678\",11,3\r\n"));
        allOk &= mockAtIo_queueResponse(wifiIo, "WIFI CONNECTED\r\nWIFI GOT IP\r\nOK\r\n");
        allOk &= automatedStep("ESP 连接 WiFi", XESP8266Wifi_connectWiFi(wifi, "UnitSSID", "12345678", 100) &&
            XESP8266Wifi_waitForWiFiConnected(wifi, 0) && g_wifiStatusChangedCount >= 2);
        allOk &= mockAtIo_queueResponse(wifiIo, "+CIPSTA:ip:\"192.168.10.86\"\r\n+CIPSTA:gateway:\"192.168.10.1\"\r\nOK\r\n");
        localIp = XESP8266Wifi_getLocalIP(wifi, 100);
        localIpText = localIp ? XHostAddress_toString(localIp) : NULL;
        allOk &= automatedStep("ESP 查询本地 IP", localIpText &&
            strcmp(XString_toUtf8(localIpText), "192.168.10.86") == 0);
        if (localIpText) XString_delete_base(localIpText);
        if (localIp) XFree_System(localIp);

        allOk &= mockAtIo_queueResponse(wifiIo, "CONNECT\r\n");
        connectBefore = g_connectCount;
        allOk &= automatedStep("ESP TCP 单连接", XESP8266Wifi_connectServer(wifi, XESP8266_Protocol_TCP,
            "192.168.1.1", 18081, 0, 100) == 0 &&
            XESP8266Wifi_waitForServerConnected(wifi, 0) && g_connectCount == connectBefore + 1);
        allOk &= mockAtIo_queueResponse(wifiIo, ">\r\n");
        allOk &= mockAtIo_queueResponse(wifiIo, "SEND OK\r\n");
        allOk &= automatedStep("ESP 非透传写", XESP8266Wifi_write(wifi, 0, "pong", 4, 100) == 4 &&
            mockAtIo_writesContain(wifiIo, "AT+CIPSEND=4\r\n") && mockAtIo_writesContain(wifiIo, "pong"));
        readyReadBefore = g_readyReadCount;
        allOk &= mockAtIo_pushInput(wifiIo, "+IPD,5:hello");
        mockAtIo_processWifi(wifi);
        allOk &= automatedStep("ESP 单通道 +IPD 读取", g_readyReadCount == readyReadBefore + 1 &&
            XESP8266Wifi_getBytesAvailable(wifi, 0) == 5 &&
            XESP8266Wifi_read(wifi, 0, readBuffer, 5, 0) == 5 && memcmp(readBuffer, "hello", 5) == 0 &&
            XESP8266Wifi_getBytesToWrite(wifi, 0) == 0);
        disconnectBefore = g_disconnectCount;
        statusBefore = g_serverStatusChangedCount;
        allOk &= mockAtIo_queueResponse(wifiIo, "OK\r\n");
        allOk &= automatedStep("ESP 断开单连接信号", XESP8266Wifi_disconnectConn(wifi, 0, 100) &&
            XESP8266Wifi_waitForDisconnectConn(wifi, 0, 0) && g_disconnectCount == disconnectBefore + 1 &&
            g_serverStatusChangedCount == statusBefore + 1);

        allOk &= mockAtIo_queueResponse(wifiIo, "OK\r\n");
        allOk &= automatedStep("ESP 开启多连接", XESP8266Wifi_setMultiConnMode(wifi, true, 100));
        allOk &= mockAtIo_queueResponse(wifiIo, "1,CONNECT\r\n");
        allOk &= automatedStep("ESP UDP 多连接", XESP8266Wifi_connectServer(wifi, XESP8266_Protocol_UDP,
            "192.168.1.1", 18082, 1, 100) == 1);
        connectBefore = g_connectCount;
        allOk &= mockAtIo_pushInput(wifiIo, "2,CONNECT\r\n");
        mockAtIo_processWifi(wifi);
        allOk &= automatedStep("ESP 被动多通道连接", g_connectCount == connectBefore + 1);
        allOk &= mockAtIo_queueResponse(wifiIo, ">\r\n");
        allOk &= mockAtIo_queueResponse(wifiIo, "SEND OK\r\n");
        allOk &= automatedStep("ESP 多通道写", XESP8266Wifi_write(wifi, 1, "udp", 3, 100) == 3 &&
            mockAtIo_writesContain(wifiIo, "AT+CIPSEND=1,3\r\n"));
        readyReadBefore = g_readyReadCount;
        allOk &= mockAtIo_pushInput(wifiIo, "+IPD,1,3:one+IPD,2,3:two");
        mockAtIo_processWifi(wifi);
        memset(readBuffer, 0, sizeof(readBuffer));
        allOk &= automatedStep("ESP 多通道 +IPD", g_readyReadCount == readyReadBefore + 2 &&
            XESP8266Wifi_read(wifi, 1, readBuffer, 3, 0) == 3 && memcmp(readBuffer, "one", 3) == 0 &&
            XESP8266Wifi_read(wifi, 2, readBuffer, 3, 0) == 3 && memcmp(readBuffer, "two", 3) == 0);
        disconnectBefore = g_disconnectCount;
        allOk &= mockAtIo_pushInput(wifiIo, "1,CLOSED\r\n2,CLOSED\r\n");
        mockAtIo_processWifi(wifi);
        allOk &= automatedStep("ESP 多通道关闭通知", g_disconnectCount == disconnectBefore + 2 &&
            XESP8266Wifi_waitForDisconnectConn(wifi, 1, 0) && XESP8266Wifi_waitForDisconnectConn(wifi, 2, 0));

        wifi->m_base.m_currentOp = XESP8266_Op_SetMultiConnMode;
        wifi->m_base.m_operationResult = 0;
        allOk &= mockAtIo_pushInput(wifiIo, "4,CLOSED\r\n");
        mockAtIo_processWifi(wifi);
        multiConnOpPreserved = wifi->m_base.m_currentOp == XESP8266_Op_SetMultiConnMode;
        allOk &= mockAtIo_pushInput(wifiIo, "OK\r\n");
        mockAtIo_processWifi(wifi);
        allOk &= automatedStep("异步关闭不打断多连接命令",
            multiConnOpPreserved && wifi->m_base.m_operationResult != 0 &&
            wifi->m_base.m_currentOp == XESP8266_Op_None);

        allOk &= mockAtIo_queueResponse(wifiIo, "OK\r\n");
        allOk &= automatedStep("ESP 启动 TCP 服务", XESP8266Wifi_startServer(wifi, XESP8266_Protocol_TCP, 18080, 100));
        allOk &= mockAtIo_queueResponse(wifiIo, "OK\r\n");
        allOk &= automatedStep("ESP 停止 TCP 服务", XESP8266Wifi_stopServer(wifi, 100));
        allOk &= mockAtIo_queueResponse(wifiIo, "CONNECT\r\n");
        allOk &= automatedStep("ESP 启动 UDP 服务", XESP8266Wifi_startServer(wifi, XESP8266_Protocol_UDP, 18083, 100));
        allOk &= mockAtIo_queueResponse(wifiIo, "OK\r\n");
        allOk &= automatedStep("ESP 关闭 UDP 服务", XESP8266Wifi_disconnectConn(wifi, 0, 100));

        allOk &= mockAtIo_queueResponse(wifiIo, "OK\r\n");
        allOk &= automatedStep("ESP 关闭多连接", XESP8266Wifi_setMultiConnMode(wifi, false, 100));
        allOk &= mockAtIo_queueResponse(wifiIo, "CONNECT\r\n");
        allOk &= automatedStep("ESP 建立透传连接", XESP8266Wifi_connectServer(wifi, XESP8266_Protocol_TCP,
            "192.168.1.1", 18081, 0, 100) == 0);
        allOk &= mockAtIo_queueResponse(wifiIo, "OK\r\n");
        allOk &= mockAtIo_queueResponse(wifiIo, ">\r\n");
        allOk &= automatedStep("ESP 进入透传", XESP8266Wifi_enterTransparentMode(wifi, 100));
        allOk &= mockAtIo_queueResponse(wifiIo, NULL);
        allOk &= automatedStep("ESP 透传写", XESP8266Wifi_write(wifi, 0, "raw", 3, 100) == 3 &&
            mockAtIo_writesContain(wifiIo, "raw"));
        allOk &= mockAtIo_queueResponse(wifiIo, NULL);
        allOk &= mockAtIo_queueResponse(wifiIo, "OK\r\n");
        allOk &= automatedStep("ESP 退出透传", XESP8266Wifi_exitTransparentMode(wifi, 100) &&
            mockAtIo_writesContain(wifiIo, "+++") && mockAtIo_writesContain(wifiIo, "AT+CIPMODE=0\r\n"));
        allOk &= mockAtIo_queueResponse(wifiIo, "OK\r\n");
        allOk &= automatedStep("ESP 断开 WiFi", XESP8266Wifi_disconnectWiFi(wifi, 100) &&
            XESP8266Wifi_getWiFiStatus(wifi) == XESP8266_Status_Disconnected && g_responseCount > 0 && g_okCount > 0);
    }
    if (wifi) XClass_delete_base((XClass*)wifi);
    if (wifiIo) XClass_delete_base((XClass*)wifiIo);

    XPrintf("========== 模拟回归 %s ==========\n", allOk ? "通过" : "失败");
    return allOk ? 0 : 1;
}

static void pumpEsp(XESP8266Wifi* wifi)
{
    if (!wifi) return;
    XCoreApplication_processEvents(XEventLoop_AllEvents);
    if (wifi->m_base.m_io && XIODevice_bytesAvailable_base(wifi->m_base.m_io) > 0)
        XClassGetVirtualFunc(wifi, EXATComm_ProcessResponse, void(*)(XATComm*))(&wifi->m_base);
}

static bool hostTcpResolveLocalAddress(XHostAddress* out)
{
    const char* configuredAddress;
    XVector* addresses;
    size_t i;

    if (!out) return false;
    configuredAddress = getenv("XESP8266_HOST_IP");
    if (configuredAddress && configuredAddress[0]) {
        XHostAddress_setAddress(out, configuredAddress);
        return XHostAddress_protocol(out) == XHostAddress_IPv4Protocol &&
            !XHostAddress_isNull(out);
    }

    addresses = XNetworkInterface_allAddresses();
    if (!addresses) return false;
    for (i = 0; i < XVector_size_base(addresses); ++i) {
        XHostAddress* candidate = XVector_at_base(addresses, (int64_t)i);
        if (candidate && XHostAddress_protocol(candidate) == XHostAddress_IPv4Protocol &&
            !XHostAddress_isNull(candidate) && !XHostAddress_isLoopback(candidate) &&
            !XHostAddress_isLinkLocal(candidate)) {
            XHostAddress_copy_base(out, candidate);
            XVector_delete_base(addresses);
            return true;
        }
    }
    XVector_delete_base(addresses);
    return false;
}

static void hostTcpClose(XTcpSocket** socket)
{
    if (!socket || !*socket) return;
    XTcpSocket_abort(*socket);
    XTcpSocket_deleteLater(*socket);
    *socket = NULL;
}

static void hostTcpListenerClose(XTcpServer* listener)
{
    if (listener && XTcpServer_isListening(listener))
        XTcpServer_close(listener);
}

static bool hostTcpListen(XTcpServer** listener, const XHostAddress* bindAddress, uint16_t port)
{
    if (!listener || !bindAddress || port == 0) return false;
    if (!*listener) {
        *listener = XTcpServer_create();
        if (!*listener) return false;
    }
    hostTcpListenerClose(*listener);
    return XTcpServer_listen(*listener, bindAddress, port);
}

static XTcpSocket* hostTcpAccept(XTcpServer* listener, XESP8266Wifi* wifi, long msecs)
{
    uint64_t deadline = XDateTime_currentMSecsSinceEpoch() + (uint64_t)msecs;

    if (!listener) return NULL;
    while (XDateTime_currentMSecsSinceEpoch() < deadline) {
        pumpEsp(wifi);
        if (XTcpServer_hasPendingConnections_base(listener))
            return XTcpServer_nextPendingConnection_base(listener);
        XThread_msleep(1);
    }
    return NULL;
}

static bool hostTcpConnect(XTcpSocket** client, const char* peerIp, uint16_t port,
    XESP8266Wifi* wifi, long msecs)
{
    uint64_t deadline;

    if (!client || !peerIp || peerIp[0] == '\0' || port == 0) return false;
    hostTcpClose(client);
    *client = XTcpSocket_create();
    if (!*client) return false;
    XTcpSocket_connectToHost_base(*client, peerIp, port, XIODevice_ReadWrite,
        XAbstractSocket_IPv4Protocol);
    deadline = XDateTime_currentMSecsSinceEpoch() + (uint64_t)msecs;
    while (XDateTime_currentMSecsSinceEpoch() < deadline) {
        XAbstractSocket_SocketState state;
        pumpEsp(wifi);
        state = XTcpSocket_state(*client);
        if (state == XAbstractSocket_ConnectedState) return true;
        if (state == XAbstractSocket_UnconnectedState) break;
        XThread_msleep(1);
    }
    hostTcpClose(client);
    return false;
}

static bool hostTcpSendAll(XTcpSocket* client, const char* data, size_t size,
    XESP8266Wifi* wifi, long msecs)
{
    size_t sent = 0;
    uint64_t deadline;
    if (!client || !data || size == 0) return false;

    while (sent < size) {
        int64_t n = XTcpSocket_write_1(client, data + sent, (int64_t)(size - sent));
        if (n <= 0) return false;
        sent += (size_t)n;
    }
    deadline = XDateTime_currentMSecsSinceEpoch() + (uint64_t)msecs;
    while (XTcpSocket_bytesToWrite_base(client) > 0 &&
        XDateTime_currentMSecsSinceEpoch() < deadline) {
        pumpEsp(wifi);
        XThread_msleep(1);
    }
    if (XTcpSocket_bytesToWrite_base(client) != 0) return false;
    return true;
}

static bool hostTcpReceiveExact(XTcpSocket* client, char* data, size_t size,
    XESP8266Wifi* wifi, long msecs)
{
    size_t received = 0;
    uint64_t deadline = XDateTime_currentMSecsSinceEpoch() + (uint64_t)msecs;
    if (!client || !data || size == 0) return false;

    while (received < size && XDateTime_currentMSecsSinceEpoch() < deadline) {
        int64_t available;
        pumpEsp(wifi);
        available = XTcpSocket_bytesAvailable_base(client);
        if (available > 0) {
            size_t toRead = (size_t)available < size - received ?
                (size_t)available : size - received;
            int64_t n = XTcpSocket_read_1(client, data + received, (int64_t)toRead);
            if (n <= 0) return false;
            received += (size_t)n;
        }
        else XThread_msleep(1);
    }
    return received == size;
}

static bool waitEspServerConnection(XESP8266Wifi* wifi, int minimum, int ids[XESP8266_MAX_CONNS], long msecs)
{
    uint64_t deadline = XDateTime_currentMSecsSinceEpoch() + (uint64_t)msecs;
    while (XDateTime_currentMSecsSinceEpoch() < deadline) {
        int count = 0;
        int i;
        pumpEsp(wifi);
        for (i = 0; i < XESP8266_MAX_CONNS; ++i) {
            if (wifi->m_connections[i].isServer &&
                wifi->m_connections[i].status == XESP8266_Status_Connected) {
                if (ids) ids[count] = i;
                count++;
            }
        }
        if (count >= minimum) return true;
    }
    return false;
}

static bool waitEspPayload(XESP8266Wifi* wifi, int connId, char* out, size_t size, long msecs)
{
    size_t received = 0;
    uint64_t deadline = XDateTime_currentMSecsSinceEpoch() + (uint64_t)msecs;
    while (received < size && XDateTime_currentMSecsSinceEpoch() < deadline) {
        size_t available;
        pumpEsp(wifi);
        available = XESP8266Wifi_getBytesAvailable(wifi, connId);
        if (available > 0) {
            size_t toRead = available < size - received ? available : size - received;
            size_t n = XESP8266Wifi_read(wifi, connId, out + received, toRead, 0);
            received += n;
        }
    }
    return received == size;
}

/*
 * 真实设备套件：ESP8266 经串口连接，执行 AT 初始化以及本机 TCP 对端联调，
 * 覆盖非透传、透传和多通道数据路径。
 */
int XESP8266WifiTest_runAutomated(const char* portName, const char* ssid, const char* password)
{
    const char* port = (portName && portName[0]) ? portName : "COM8";
    const char* network = (ssid && ssid[0]) ? ssid : "XX";
    const char* secret = (password && password[0]) ? password : "20130520";
    const char* apName = getenv("XESP8266_AP_SSID");
    const char* apPassword = getenv("XESP8266_AP_PASSWORD");
    bool allOk = true;
    bool wifiConnected = false;
    bool transparent = false;
    bool espServerStarted = false;
    XSerialPort* serial = NULL;
    XESP8266Wifi* wifi = NULL;
    XHostAddress hostAddress;
    XTcpServer* listener = NULL;
    XTcpSocket* hostA = NULL;
    XTcpSocket* hostB = NULL;
    char hostIp[64] = {0};
    char espIp[64] = {0};
    int connId = -1;

    XHostAddress_init(&hostAddress);

    if (!apName || !apName[0]) apName = "XinYueC-Test-AP";
    if (!apPassword || !apPassword[0]) apPassword = "20130520";

    if (XESP8266WifiTest_runUnit() != 0) return 1;

    XPrintf("\n========== ESP8266 全 API 实机联调测试 ==========\n");
    XPrintf("  port=%s, ssid=%s\n", port, network);

    serial = openSerialByName(port);
    if (!serial) return 2;
    wifi = XESP8266Wifi_create((XIODevice*)serial);
    if (!wifi) {
        closeSerial(serial);
        return 2;
    }

    resetCounters();
    XObject_connect_2((XObject*)wifi, (size_t)XESP8266Wifi_ok_signal, onOk);
    XObject_connect_2((XObject*)wifi, (size_t)XESP8266Wifi_error_signal, onError);
    XObject_connect_2((XObject*)wifi, (size_t)XESP8266Wifi_response_signal, onResponse);
    XObject_connect_2((XObject*)wifi, (size_t)XESP8266Wifi_wifiStatusChanged_signal, onWifiStatusChanged);
    XObject_connect_2((XObject*)wifi, (size_t)XESP8266Wifi_serverStatusChanged_signal, onServerStatusChanged);
    XObject_connect_2((XObject*)wifi, (size_t)XESP8266Wifi_readyRead_signal, onReadyRead);
    XObject_connect_2((XObject*)wifi, (size_t)XESP8266Wifi_connect_signal, onConnect);
    XObject_connect_2((XObject*)wifi, (size_t)XESP8266Wifi_disconnect_signal, onDisconnect);

    /* 复位必须最先执行，清除上次测试遗留的透传或服务器状态。 */
    allOk &= automatedStep("复位模块", XESP8266Wifi_reset(wifi, 5000));
    {
        uint64_t deadline = XDateTime_currentMSecsSinceEpoch() + 1200;
        while (XDateTime_currentMSecsSinceEpoch() < deadline)
            XCoreApplication_processEvents(XEventLoop_AllEvents);
    }
    allOk &= automatedStep("AT 基本通信", XESP8266Wifi_testAT(wifi, 3000));
    allOk &= automatedStep("设置 STA 模式", XESP8266Wifi_setMode(wifi, XESP8266_Mode_STA, 3000));
    allOk &= automatedStep("查询 STA 工作模式", XESP8266Wifi_getMode(wifi) == XESP8266_Mode_STA);
    allOk &= automatedStep("关闭多连接模式", XESP8266Wifi_setMultiConnMode(wifi, false, 3000));

    {
        bool issued = XESP8266Wifi_connectWiFi(wifi, network, secret, 0);
        bool connected = issued && XESP8266Wifi_waitForWiFiConnected(wifi, 20000);
        allOk &= automatedStep("发起 WiFi 连接", issued);
        allOk &= automatedStep("等待 WiFi 连接", connected);
        allOk &= automatedStep("查询 WiFi 状态", XESP8266Wifi_getWiFiStatus(wifi) == XESP8266_Status_Connected);
        wifiConnected = connected;
    }
    if (!wifiConnected) {
        XPrintf("  WiFi 未关联，跳过网络收发阶段\n");
        goto cleanup;
    }

    {
        XHostAddress* localIp = XESP8266Wifi_getLocalIP(wifi, 5000);
        bool gotIp = localIp != NULL;
        if (localIp) {
            XString* ipText = XHostAddress_toString(localIp);
            if (ipText) {
                const char* text = XString_toUtf8(ipText);
                if (text) {
                    strncpy(espIp, text, sizeof(espIp) - 1);
                    espIp[sizeof(espIp) - 1] = '\0';
                    XPrintf("  ESP 本地 IP: %s\n", espIp);
                }
                XString_delete_base(ipText);
            }
            XFree_System(localIp);
        }
        allOk &= automatedStep("查询本地 IP", gotIp && espIp[0] != '\0');
    }

    {
        bool hostIpOk = hostTcpResolveLocalAddress(&hostAddress);
        XString* hostIpText = hostIpOk ? XHostAddress_toString(&hostAddress) : NULL;
        if (hostIpText) {
            const char* text = XString_toUtf8(hostIpText);
            if (text) {
                strncpy(hostIp, text, sizeof(hostIp) - 1);
                hostIp[sizeof(hostIp) - 1] = '\0';
            }
            XString_delete_base(hostIpText);
        }
        XPrintf("  主机网口地址: %s\n", hostIp[0] ? hostIp : "(未找到)");
        allOk &= automatedStep("确定主机网口地址", hostIpOk && hostIp[0] != '\0');
    }
    if (!hostIp[0] || !espIp[0]) goto cleanup;

    /* 单连接非透传请求/响应。 */
    if (!hostTcpListen(&listener, &hostAddress, 18081)) {
        allOk &= automatedStep("启动主机 TCP 对端", false);
        goto cleanup;
    }
    allOk &= automatedStep("启动主机 TCP 对端", true);
    connId = XESP8266Wifi_connectServer(wifi, XESP8266_Protocol_TCP, hostIp, 18081, 0, 0);
    allOk &= automatedStep("发起单连接 TCP", connId == 0);
    {
        bool connected = connId == 0 && XESP8266Wifi_waitForServerConnected(wifi, 10000) &&
            (hostA = hostTcpAccept(listener, wifi, 5000)) != NULL;
        allOk &= automatedStep("等待单连接建立", connected);
        allOk &= automatedStep("查询服务器状态", connected &&
            XESP8266Wifi_getServerStatus(wifi) == XESP8266_Status_Connected);
        if (connected) {
            const char* request = "XINYUEC-NONTRANSPARENT-PING";
            const char* reply = "XINYUEC-NONTRANSPARENT-PONG";
            char received[64] = {0};
            bool exchange = hostTcpSendAll(hostA, request, strlen(request), wifi, 5000) &&
                waitEspPayload(wifi, connId, received, strlen(request), 5000) &&
                memcmp(received, request, strlen(request)) == 0;
            allOk &= automatedStep("非透传主机到 ESP", exchange);
            if (exchange) {
                size_t sent = XESP8266Wifi_write(wifi, connId, reply, strlen(reply), -1);
                bool replyOk = sent == strlen(reply) && hostTcpReceiveExact(hostA, received,
                    strlen(reply), wifi, 5000) && memcmp(received, reply, strlen(reply)) == 0;
                allOk &= automatedStep("非透传 ESP 到主机", replyOk);
            }
            allOk &= automatedStep("查询接收缓冲区", XESP8266Wifi_getBytesAvailable(wifi, connId) == 0);
            allOk &= automatedStep("查询发送缓冲区", XESP8266Wifi_getBytesToWrite(wifi, connId) == 0);
        }
    }
    allOk &= automatedStep("断开单连接", XESP8266Wifi_disconnectConn(wifi, connId, -1));
    allOk &= automatedStep("等待单连接断开", XESP8266Wifi_waitForDisconnectConn(wifi, connId, 3000));
    hostTcpClose(&hostA);
    hostTcpListenerClose(listener);
    connId = -1;

    /* 透传模式使用主机监听端作为 ESP 的 TCP 对端。 */
    if (!hostTcpListen(&listener, &hostAddress, 18081)) {
        allOk &= automatedStep("重启主机 TCP 对端", false);
        goto cleanup;
    }
    connId = XESP8266Wifi_connectServer(wifi, XESP8266_Protocol_TCP, hostIp, 18081, 0, 0);
    {
        bool connected = connId == 0 && XESP8266Wifi_waitForServerConnected(wifi, 10000) &&
            (hostA = hostTcpAccept(listener, wifi, 5000)) != NULL;
        allOk &= automatedStep("建立透传 TCP 连接", connected);
        if (connected) {
            bool entered = XESP8266Wifi_enterTransparentMode(wifi, -1);
            allOk &= automatedStep("进入透传模式", entered);
            transparent = entered;
            if (entered) {
                const char* request = "XINYUEC-TRANSPARENT-PING";
                const char* reply = "XINYUEC-TRANSPARENT-PONG";
                char received[64] = {0};
                bool exchange = hostTcpSendAll(hostA, request, strlen(request), wifi, 5000) &&
                    waitEspPayload(wifi, 0, received, strlen(request), 5000) &&
                    memcmp(received, request, strlen(request)) == 0;
                allOk &= automatedStep("透传主机到 ESP", exchange);
                if (exchange) {
                    size_t sent = XESP8266Wifi_write(wifi, 0, reply, strlen(reply), -1);
                    bool replyOk = sent == strlen(reply) && hostTcpReceiveExact(hostA, received,
                        strlen(reply), wifi, 5000) && memcmp(received, reply, strlen(reply)) == 0;
                    allOk &= automatedStep("透传 ESP 到主机", replyOk);
                }
                allOk &= automatedStep("退出透传模式", XESP8266Wifi_exitTransparentMode(wifi, 5000));
                transparent = false;
            }
        }
    }
    allOk &= automatedStep("断开透传连接", XESP8266Wifi_disconnectConn(wifi, 0, 5000));
    allOk &= automatedStep("等待透传连接断开", XESP8266Wifi_waitForDisconnectConn(wifi, 0, 3000));
    hostTcpClose(&hostA);
    hostTcpListenerClose(listener);
    connId = -1;

    /* 两个独立客户端验证 CIPMUX=1、+IPD 通道解析及各通道读写缓冲区。 */
    allOk &= automatedStep("开启多连接模式", XESP8266Wifi_setMultiConnMode(wifi, true, 3000));
    espServerStarted = XESP8266Wifi_startServer(wifi, XESP8266_Protocol_TCP, 18080, 5000);
    allOk &= automatedStep("启动 ESP TCP 服务器", espServerStarted);
    if (espServerStarted) {
        int ids[XESP8266_MAX_CONNS] = {-1, -1, -1, -1, -1};
        bool connected = hostTcpConnect(&hostA, espIp, 18080, wifi, 5000) &&
            hostTcpConnect(&hostB, espIp, 18080, wifi, 5000) &&
            waitEspServerConnection(wifi, 2, ids, 10000);
        allOk &= automatedStep("建立两个多通道连接", connected);
        if (connected) {
            const char* request = "XINYUEC-MULTI-PING";
            const char* reply = "XINYUEC-MULTI-PONG";
            int i;
            bool exchange = hostTcpSendAll(hostA, request, strlen(request), wifi, 5000) &&
                hostTcpSendAll(hostB, request, strlen(request), wifi, 5000);
            for (i = 0; i < 2 && exchange; ++i) {
                char received[64] = {0};
                exchange = waitEspPayload(wifi, ids[i], received, strlen(request), 5000) &&
                    memcmp(received, request, strlen(request)) == 0;
                if (exchange) exchange = XESP8266Wifi_write(wifi, ids[i], reply, strlen(reply), -1) == strlen(reply);
            }
            if (exchange) {
                char receivedA[64] = {0};
                char receivedB[64] = {0};
                exchange = hostTcpReceiveExact(hostA, receivedA, strlen(reply), wifi, 5000) &&
                    hostTcpReceiveExact(hostB, receivedB, strlen(reply), wifi, 5000) &&
                    memcmp(receivedA, reply, strlen(reply)) == 0 &&
                    memcmp(receivedB, reply, strlen(reply)) == 0;
            }
            allOk &= automatedStep("多通道双向收发", exchange);
        }
        hostTcpClose(&hostA);
        hostTcpClose(&hostB);
        allOk &= automatedStep("停止 ESP TCP 服务器", XESP8266Wifi_stopServer(wifi, 5000));
        espServerStarted = false;
    }
    allOk &= automatedStep("关闭多连接模式", XESP8266Wifi_setMultiConnMode(wifi, false, 3000));

    allOk &= automatedStep("断开 WiFi", XESP8266Wifi_disconnectWiFi(wifi, 5000));
    allOk &= automatedStep("确认 WiFi 已断开", XESP8266Wifi_getWiFiStatus(wifi) == XESP8266_Status_Disconnected);

    /* CWSAP 会持久化 AP 配置，旧版 AT 固件还可能异步输出状态行。将它放在
     * 最后，避免状态切换中断前面的 TCP 收发用例。 */
    allOk &= automatedStep("设置 STA/AP 模式", XESP8266Wifi_setMode(wifi, XESP8266_Mode_STA_AP, 3000));
    allOk &= automatedStep("配置 AP 参数", XESP8266Wifi_configAP(wifi, apName, apPassword, 1,
        XESP8266_Encrypt_WPA_WPA2_PSK, -1));
    allOk &= automatedStep("查询 STA/AP 工作模式", XESP8266Wifi_getMode(wifi) == XESP8266_Mode_STA_AP);

cleanup:
    if (transparent) (void)XESP8266Wifi_exitTransparentMode(wifi, 3000);
    hostTcpClose(&hostA);
    hostTcpClose(&hostB);
    hostTcpListenerClose(listener);
    if (listener) XTcpServer_deleteLater(listener);
    if (espServerStarted) (void)XESP8266Wifi_stopServer(wifi, 3000);
    if (wifi) {
        if (wifiConnected && XESP8266Wifi_getWiFiStatus(wifi) != XESP8266_Status_Disconnected)
            (void)XESP8266Wifi_disconnectWiFi(wifi, 3000);
        XESP8266Wifi_deleteLater(wifi);
    }
    closeSerial(serial);
    XHostAddress_deinit_base(&hostAddress);
    XPrintf("========== 自动化测试 %s ==========\n", allOk ? "通过" : "失败");
    return allOk ? 0 : 1;
}

static void XESP8266WifiTest_automatedMenu(XVariant* data)
{
    (void)data;
    (void)XESP8266WifiTest_runAutomated(NULL, NULL, NULL);
}

// ========== 菜单注册 ==========

void XMenu_XESP8266WifiTest(XMenu* root)
{
    XMenu* menu = XMenu_create("XESP8266Wifi(AT指令WiFi)");
    XMenu_addMenu(root, menu);
    {
        XAction* action = XMenu_addAction(menu, "自动化串口回归测试 (COM8)");
        XAction_setAction(action, XESP8266WifiTest_automatedMenu);
    }
    {
        XAction* action = XMenu_addAction(menu, "TCP 客户端测试");
        XAction_setAction(action, XESP8266WifiTest_tcpClient);
    }
    {
        XAction* action = XMenu_addAction(menu, "TCP 服务器测试");
        XAction_setAction(action, XESP8266WifiTest_tcpServer);
    }
}
