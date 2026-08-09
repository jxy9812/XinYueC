#include "XESP8266Wifi.h"
#include "XMemory.h"
#include "XString.h"
#include "XCoreApplication.h"
#include "XEventLoop.h"
#include "XLockFreeQueue.h"
#include "XDateTime.h"
#include <string.h>
static void VXESP8266_deinit(XESP8266Wifi* device);
// XESP8266WifiCmd.c 中的 processResponse
void VXESP8266_processResponse_cmd(XESP8266Wifi* device);

/* Windows 串口后端的重叠读有时不会到达事件分发器。这里同样排空待处理
 * 字节，使各同步等待 API 与 XATComm_sendCommand() 的行为一致。 */
static void VXESP8266_pollIo(XESP8266Wifi* device)
{
    if (!device || !device->m_base.m_io) return;
    if (XIODevice_bytesAvailable_base(device->m_base.m_io) > 0) {
        XClassGetVirtualFunc(device, EXATComm_ProcessResponse, void(*)(XATComm*))(&device->m_base);
    }
}

/**
 * @brief 重载父类 ProcessResponse 虚函数
 * @details 先调用 XATComm 默认实现（读数据+emit signal），再调用 ESP8266 专用解析。
 */
static void VXESP8266_processResponse(XATComm* comm)
{
    // 先调用父类默认实现（读数据到 m_responseBuffer，emit response/ok/error）
    XClass_Parent(XATComm, EXATComm_ProcessResponse, void(*)(XATComm*))(comm);

    // 再调用 ESP8266 专用解析
    VXESP8266_processResponse_cmd((XESP8266Wifi*)comm);
}

/**
 * @brief 虚函数表初始化
 */
XVtable* XESP8266Wifi_class_init(void) {
    XVTABLE_INIT_DEFAULT(XESP8266Wifi)
        XVTABLE_INHERIT_XCLASS(XATComm);

    // 重载 ProcessResponse
    XVTABLE_OVERLOAD_DEFAULT(EXATComm_ProcessResponse, VXESP8266_processResponse);
    // 重载析构
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Deinit, VXESP8266_deinit);

    XCLASS_SHOW_SIZE_DEFAULT(XESP8266Wifi);
    return XVTABLE_DEFAULT;
}

/**
 * @brief 初始化ESP8266设备
 */
void XESP8266Wifi_init(XESP8266Wifi* device, XIODevice* io) {
    if (ISNULL(device, "device is NULL")) return;

    // 初始化父类 XATComm（AT 通信引擎）
    memset(((XATComm*)device) + 1, 0, sizeof(XESP8266Wifi) - sizeof(XATComm));
    XATComm_init(&device->m_base, io);
    XClassGetVtable(device) = XESP8266Wifi_class_init();

    // 初始化 WiFi 特有成员
    device->m_wifiStatus = XESP8266_Status_Disconnected;
    device->m_wifiMode = XESP8266_Mode_STA;
    device->m_transparentMode = false;
    device->m_multiConnMode = false;
    device->m_ssid = XString_create();
    device->m_password = XString_create();
    XHostAddress_init(&device->m_localIP);
    XHostAddress_init(&device->m_netmask);
    XHostAddress_init(&device->m_gateway);
    device->m_pendingConnId = -1;
    device->m_pendingStatus = XESP8266_Status_Disconnected;

    // 初始化多连接数组
    device->m_activeConnCount = 0;
    for (int i = 0; i < XESP8266_MAX_CONNS; i++) {
        device->m_connections[i].connId = -1;
        device->m_connections[i].status = XESP8266_Status_Disconnected;
        device->m_connections[i].isServer = false;
        device->m_connections[i].remaining_recv_size = 0;
        device->m_connections[i].m_readBuffer = NULL;
        device->m_connections[i].m_writeBuffer = NULL;
        memset(device->m_connections[i].ip, 0, sizeof(device->m_connections[i].ip));
        device->m_connections[i].port = 0;
    }
}

/**
 * @brief 析构
 */
static void VXESP8266_deinit(XESP8266Wifi* device)
{
    if(!device) return;

    if (device->m_ssid) {
        XString_delete_base(device->m_ssid);
        device->m_ssid = NULL;
    }
    if (device->m_password) {
        XString_delete_base(device->m_password);
        device->m_password = NULL;
    }

    for (int i = 0; i < XESP8266_MAX_CONNS; i++) {
        device->m_connections[i].connId = -1;
        device->m_connections[i].status = XESP8266_Status_Disconnected;
        device->m_connections[i].isServer = false;
        device->m_connections[i].remaining_recv_size = 0;
        if (device->m_connections[i].m_readBuffer) {
            XQueueBase_delete_base(device->m_connections[i].m_readBuffer);
            device->m_connections[i].m_readBuffer = NULL;
        }
        if (device->m_connections[i].m_writeBuffer) {
            XQueueBase_delete_base(device->m_connections[i].m_writeBuffer);
            device->m_connections[i].m_writeBuffer = NULL;
        }
        memset(device->m_connections[i].ip, 0, sizeof(device->m_connections[i].ip));
        device->m_connections[i].port = 0;
    }

    XClass_Deinit_Parent(XATComm, device);
}

/**
 * @brief 创建ESP8266设备实例
 */
XESP8266Wifi* XESP8266Wifi_create(XIODevice* io) {
    XESP8266Wifi* device = XMalloc_System(sizeof(XESP8266Wifi));
    if (ISNULL(device, "malloc failed")) return NULL;
    XESP8266Wifi_init(device, io);
    Set_Class_MemoryFree(device, XFree_System);
    return device;
}

// ========== 具体AT指令实现（委托给 XATComm）==========

bool XESP8266Wifi_testAT(XESP8266Wifi* device, int msecs)
{
    if (ISNULL(device, "device is NULL")) return false;
    return XATComm_sendCommand(&device->m_base, "AT", XESP8266_Op_TestAT, msecs);
}

bool XESP8266Wifi_reset(XESP8266Wifi* device, int msecs)
{
    if (ISNULL(device, "device is NULL")) return false;
    bool result = XATComm_sendCommand(&device->m_base, "AT+RST", XESP8266_Op_Reset, msecs);
    if (result) {
        device->m_wifiStatus = XESP8266_Status_Disconnected;
        for (int i = 0; i < XESP8266_MAX_CONNS; i++) {
            device->m_connections[i].status = XESP8266_Status_Disconnected;
            device->m_connections[i].connId = -1;
        }
        device->m_activeConnCount = 0;
    }
    return result;
}

bool XESP8266Wifi_setMultiConnMode(XESP8266Wifi* device, bool enable, int msecs)
{
    if (ISNULL(device, "device is NULL")) return false;
    char cmd[15];
    snprintf(cmd, sizeof(cmd), "AT+CIPMUX=%d", enable ? 1 : 0);
    bool result = XATComm_sendCommand(&device->m_base, cmd, XESP8266_Op_SetMultiConnMode, msecs);
    if (result) {
        device->m_multiConnMode = enable;
    }
    return result;
}

bool XESP8266Wifi_setMode(XESP8266Wifi* device, XESP8266WifiMode mode, int msecs)
{
    if (ISNULL(device, "device is NULL") ||
        mode < XESP8266_Mode_STA || mode > XESP8266_Mode_STA_AP) return false;
    char cmd[15];
    snprintf(cmd, sizeof(cmd), "AT+CWMODE=%d", mode);
    bool result = XATComm_sendCommand(&device->m_base, cmd, XESP8266_Op_SetMode, msecs);
    if (result) {
        device->m_wifiMode = mode;
    }
    return result;
}

bool XESP8266Wifi_connectWiFi(XESP8266Wifi* device, const char* ssid, const char* password, int msecs)
{
    if (ISNULL(device, "device is NULL") || ISNULL(ssid, "ssid is NULL") || ISNULL(password, "password is NULL"))
        return false;
    if (ssid[0] == '\0' || strlen(ssid) > 32 || strlen(password) > 64)
        return false;

    device->m_wifiStatus = XESP8266_Status_Connecting;
    XESP8266Wifi_wifiStatusChanged_signal(device, XESP8266_Status_Connecting);

    XString_assign_utf8(device->m_ssid, ssid);
    XString_assign_utf8(device->m_password, password);
    char cmd[128];
    snprintf(cmd, sizeof(cmd), "AT+CWJAP=\"%s\",\"%s\"", ssid, password);
    return XATComm_sendCommand(&device->m_base, cmd, XESP8266_Op_ConnectWiFi, msecs);
}

bool XESP8266Wifi_disconnectWiFi(XESP8266Wifi* device, int msecs)
{
    if (ISNULL(device, "device is NULL")) return false;
    return XATComm_sendCommand(&device->m_base, "AT+CWQAP", XESP8266_Op_DisconnectWiFi, msecs);
}

bool XESP8266Wifi_configAP(XESP8266Wifi* device, const char* ssid, const char* password,
    int channel, XESP8266WifiEncryption encrypt, int msecs) {
    if (ISNULL(device, "device is NULL") || ISNULL(ssid, "ssid is NULL") ||
        (encrypt != XESP8266_Encrypt_None && ISNULL(password, "password is NULL")))
        return false;
    if (ssid[0] == '\0' || strlen(ssid) > 32 || channel < 1 || channel > 13 ||
        encrypt < XESP8266_Encrypt_None || encrypt > XESP8266_Encrypt_WPA_WPA2_PSK ||
        (encrypt != XESP8266_Encrypt_None && (strlen(password) < 8 || strlen(password) > 64)))
        return false;

    char cmd[128];
    int written = snprintf(cmd, sizeof(cmd), "AT+CWSAP=\"%s\",\"%s\",%d,%d", ssid,
        password ? password : "", channel, encrypt);
    return written > 0 && (size_t)written < sizeof(cmd) &&
        XATComm_sendCommand(&device->m_base, cmd, XESP8266_Op_ConfigAP, msecs);
}

int XESP8266Wifi_connectServer(XESP8266Wifi* device, XESP8266WifiProtocol protocol,
    const char* ip, uint16_t port, int connId, int msecs)
{
    if (ISNULL(device, "device is NULL") || ISNULL(ip, "ip is NULL") || port == 0 ||
        (protocol != XESP8266_Protocol_TCP && protocol != XESP8266_Protocol_UDP))
        return -1;

    int actualConnId = 0;
    if (!device->m_multiConnMode) {
        device->m_connections[actualConnId].connId = actualConnId;
        strncpy(device->m_connections[actualConnId].ip, ip, sizeof(device->m_connections[actualConnId].ip) - 1);
        device->m_connections[actualConnId].ip[sizeof(device->m_connections[actualConnId].ip) - 1] = '\0';
        device->m_connections[actualConnId].port = port;
        device->m_connections[actualConnId].protocol = protocol;
        device->m_connections[actualConnId].status = XESP8266_Status_Connecting;
        device->m_pendingConnId = actualConnId;
        device->m_pendingStatus = XESP8266_Status_Connecting;
        XESP8266Wifi_serverStatusChanged_signal(device, actualConnId, XESP8266_Status_Connecting);

        char cmd[128];
        const char* proto = (protocol == XESP8266_Protocol_TCP) ? "TCP" : "UDP";
        snprintf(cmd, sizeof(cmd), "AT+CIPSTART=\"%s\",\"%s\",%d", proto, ip, port);
        bool sendOk = XATComm_sendCommand(&device->m_base, cmd, XESP8266_Op_ConnectServer, msecs);
        if (!sendOk) {
            device->m_connections[actualConnId].connId = -1;
            device->m_connections[actualConnId].status = XESP8266_Status_Disconnected;
            device->m_pendingConnId = -1;
            device->m_pendingStatus = XESP8266_Status_Disconnected;
            return -1;
        }
        if (msecs)
            return device->m_pendingStatus == XESP8266_Status_Connected ? actualConnId : -1;
        return actualConnId;
    }
    else {
        actualConnId = connId;
        if (actualConnId == -1) {
            for (int i = 0; i < XESP8266_MAX_CONNS; i++) {
                if (device->m_connections[i].connId == -1) {
                    actualConnId = i;
                    break;
                }
            }
            if (actualConnId == -1) {
                XDEBUG_PRINTF("No available connection slot");
                return -1;
            }
        }
        else if (actualConnId < 0 || actualConnId >= XESP8266_MAX_CONNS) {
            XDEBUG_PRINTF("Invalid connId: %d", actualConnId);
            return -1;
        }
    }

    device->m_connections[actualConnId].connId = actualConnId;
    strncpy(device->m_connections[actualConnId].ip, ip, sizeof(device->m_connections[actualConnId].ip) - 1);
    device->m_connections[actualConnId].ip[sizeof(device->m_connections[actualConnId].ip) - 1] = '\0';
    device->m_connections[actualConnId].port = port;
    device->m_connections[actualConnId].protocol = protocol;
    device->m_connections[actualConnId].status = XESP8266_Status_Connecting;
    device->m_pendingConnId = actualConnId;
    device->m_pendingStatus = XESP8266_Status_Connecting;

    XESP8266Wifi_serverStatusChanged_signal(device, actualConnId, XESP8266_Status_Connecting);

    char cmd[128];
    if (device->m_multiConnMode) {
        snprintf(cmd, sizeof(cmd), "AT+CIPSTART=%d,\"%s\",\"%s\",%d",
            actualConnId,
            protocol == XESP8266_Protocol_TCP ? "TCP" : "UDP",
            ip, port);
    }
    else {
        snprintf(cmd, sizeof(cmd), "AT+CIPSTART=\"%s\",\"%s\",%d",
            protocol == XESP8266_Protocol_TCP ? "TCP" : "UDP",
            ip, port);
    }

    bool sendOk = XATComm_sendCommand(&device->m_base, cmd, XESP8266_Op_ConnectServer, msecs);
    if (!sendOk) {
        device->m_connections[actualConnId].connId = -1;
        device->m_connections[actualConnId].status = XESP8266_Status_Disconnected;
        device->m_pendingConnId = -1;
        device->m_pendingStatus = XESP8266_Status_Disconnected;
        return -1;
    }

    if (msecs)
        return device->m_pendingStatus == XESP8266_Status_Connected ? actualConnId : -1;
    return actualConnId;
}

bool XESP8266Wifi_disconnectConn(XESP8266Wifi* device, int connId, int msecs)
{
    if (ISNULL(device, "device is NULL")) return false;
    char cmd[32];
    if (!device->m_multiConnMode) {
        snprintf(cmd, sizeof(cmd), "AT+CIPCLOSE");
    }
    else {
        if (connId < 0 || connId >= XESP8266_MAX_CONNS ||
            device->m_connections[connId].connId == -1) {
            return false;
        }
        snprintf(cmd, sizeof(cmd), "AT+CIPCLOSE=%d", connId);
    }
    device->m_pendingConnId = connId;
    return XATComm_sendCommand(&device->m_base, cmd, XESP8266_Op_DisconnectServer, msecs);
}

bool XESP8266Wifi_startServer(XESP8266Wifi* device, XESP8266WifiProtocol protocol, uint16_t port, int msecs)
{
    if (ISNULL(device, "device is NULL") || port == 0 ||
        (protocol != XESP8266_Protocol_TCP && protocol != XESP8266_Protocol_UDP)) return false;
    if (!device->m_multiConnMode) {
        if (!XESP8266Wifi_setMultiConnMode(device, true, msecs)) return false;
    }
    if (protocol == XESP8266_Protocol_TCP) {
        char cmd[64];
        snprintf(cmd, sizeof(cmd), "AT+CIPSERVER=1,%d", port);
        return XATComm_sendCommand(&device->m_base, cmd, XESP8266_Op_StartServer, msecs);
    }
    else {
        return XESP8266Wifi_connectServer(device, protocol, "0.0.0.0", port, -1, msecs) >= 0;
    }
}

bool XESP8266Wifi_stopServer(XESP8266Wifi* device, int msecs)
{
    if (ISNULL(device, "device is NULL")) return false;
    return XATComm_sendCommand(&device->m_base, "AT+CIPSERVER=0", XESP8266_Op_StopServer, msecs);
}

// ========== 数据读写 ==========

size_t XESP8266Wifi_write(XESP8266Wifi* device, int connId, const void* data, size_t size, int msecs)
{
    if (ISNULL(device, "device is NULL") || ISNULL(data, "data is NULL") || size == 0)
        return 0;

    if (!device->m_multiConnMode) {
        if (device->m_transparentMode) {
            return XIODevice_write_1(device->m_base.m_io, data, size);
        }
        else {
            char cmd[32];
            snprintf(cmd, sizeof(cmd), "AT+CIPSEND=%d", (int)size);
            if (!XATComm_sendCommand(&device->m_base, cmd, XESP8266_Op_WriteData, msecs)) {
                return 0;
            }
            if (XIODevice_write_1(device->m_base.m_io, data, size) == size) {
                if (XATComm_sendCommand(&device->m_base, NULL, XESP8266_Op_WriteData, msecs)) {
                    return size;
                }
            }
            return 0;
        }
    }

    if (connId < 0 || connId >= XESP8266_MAX_CONNS ||
        device->m_connections[connId].status != XESP8266_Status_Connected)
        return 0;

    if (device->m_transparentMode) {
        char cmd[32];
        snprintf(cmd, sizeof(cmd), "AT+CIPSEND=%d", connId);
        if (!XATComm_sendCommand(&device->m_base, cmd, XESP8266_Op_WriteData, msecs)) {
            return 0;
        }
        return XIODevice_write_1(device->m_base.m_io, data, size) == size ? size : 0;
    }
    else {
        char cmd[32];
        snprintf(cmd, sizeof(cmd), "AT+CIPSEND=%d,%d", connId, (int)size);
        if (!XATComm_sendCommand(&device->m_base, cmd, XESP8266_Op_WriteData, msecs)) {
            return 0;
        }
        if (XIODevice_write_1(device->m_base.m_io, data, size) == size) {
            if (XATComm_sendCommand(&device->m_base, NULL, XESP8266_Op_WriteData, msecs)) {
                return size;
            }
        }
        return 0;
    }
}

size_t XESP8266Wifi_read(XESP8266Wifi* device, int connId, void* data, size_t size, int msecs)
{
    if (ISNULL(device, "device is NULL") || ISNULL(data, "data is NULL") || size == 0)
        return 0;

    if (!device->m_multiConnMode)
        connId = 0;
    else if (connId < 0 || connId >= XESP8266_MAX_CONNS)
        return 0;

    XCircularQueue* queue = device->m_connections[connId].m_readBuffer;
    if (!queue) return 0;

    size_t remaining = size;
    size_t queueSize = XQueueBase_size_base(queue);
    size_t recvSize = remaining > queueSize ? queueSize : remaining;
    for (size_t i = 0; i < recvSize; i++) {
        XQueueBase_receive_base(queue, ((char*)data) + i);
    }
    remaining -= recvSize;

    if (remaining == 0) return size;
    if (msecs <= 0) return size - remaining;

    uint64_t deadline = XDateTime_currentMSecsSinceEpoch() + msecs;
    while (XDateTime_currentMSecsSinceEpoch() < deadline) {
        XCoreApplication_processEvents(XEventLoop_AllEvents);
        if (remaining && XQueueBase_receive_base(queue, ((char*)data) + (size - remaining))) {
            --remaining;
            if (remaining == 0) return size;
        }
    }
    return size - remaining;
}

size_t XESP8266Wifi_getBytesAvailable(XESP8266Wifi* device, int connId)
{
    if (ISNULL(device, "device is NULL")) return 0;
    if (!device->m_multiConnMode) connId = 0;
    else if (connId < 0 || connId >= XESP8266_MAX_CONNS) return 0;
    XCircularQueue* queue = device->m_connections[connId].m_readBuffer;
    return queue ? XCircularQueue_size_base(queue) : 0;
}

size_t XESP8266Wifi_getBytesToWrite(XESP8266Wifi* device, int connId)
{
    if (ISNULL(device, "device is NULL")) return 0;
    if (!device->m_multiConnMode) connId = 0;
    else if (connId < 0 || connId >= XESP8266_MAX_CONNS) return 0;
    XCircularQueue* queue = device->m_connections[connId].m_writeBuffer;
    return queue ? XCircularQueue_size_base(queue) : 0;
}

// ========== 透传模式 ==========

bool XESP8266Wifi_enterTransparentMode(XESP8266Wifi* device, int msecs)
{
    if (ISNULL(device, "device is NULL")) return false;
    if (!XATComm_sendCommand(&device->m_base, "AT+CIPMODE=1", XESP8266_Op_SetTransparent, msecs))
        return false;
    if (!XATComm_sendCommand(&device->m_base, "AT+CIPSEND", XESP8266_Op_EnterTransparent, msecs))
        return false;
    device->m_transparentMode = true;
    return true;
}

bool XESP8266Wifi_exitTransparentMode(XESP8266Wifi* device, int msecs)
{
    if (ISNULL(device, "device is NULL") || !device->m_transparentMode)
        return false;
    XIODevice_write_1(device->m_base.m_io, "+++", 3);
    /* ESP-AT 要求 +++ 前后保留保护时间；等待期间仍处理事件，避免串口输入
     * 滞留在事件队列中。 */
    uint64_t guardDeadline = XDateTime_currentMSecsSinceEpoch() + 1100;
    while (XDateTime_currentMSecsSinceEpoch() < guardDeadline)
        XCoreApplication_processEvents(XEventLoop_AllEvents);
    if (!XATComm_sendCommand(&device->m_base, "AT+CIPMODE=0", XESP8266_Op_SetTransparent, msecs))
        return false;
    device->m_transparentMode = false;
    return true;
}

// ========== 状态查询 ==========

XESP8266WifiStatus XESP8266Wifi_getWiFiStatus(XESP8266Wifi* device) {
    return (device) ? device->m_wifiStatus : XESP8266_Status_Error;
}

XESP8266WifiStatus XESP8266Wifi_getServerStatus(XESP8266Wifi* device) {
    if (!device) return XESP8266_Status_Error;
    if (device->m_pendingConnId >= 0) return device->m_pendingStatus;
    for (int i = 0; i < XESP8266_MAX_CONNS; ++i) {
        XESP8266WifiStatus status = device->m_connections[i].status;
        if (status == XESP8266_Status_Connecting || status == XESP8266_Status_Connected)
            return status;
    }
    return XESP8266_Status_Disconnected;
}

XESP8266WifiMode XESP8266Wifi_getMode(XESP8266Wifi* device) {
    return (device) ? device->m_wifiMode : XESP8266_Mode_STA;
}
XHostAddress* XESP8266Wifi_getLocalIP(XESP8266Wifi* device, int msecs) {
    if (ISNULL(device, "device is NULL")) return NULL;
    if (msecs == 0) return NULL;
    XATComm* base = &device->m_base;
    // 1. 尝试 AT+CIFSR（部分固件返回完整IP），解析在 Cmd 层 GetLocalIP 中完成并缓存
  /*  if (XATComm_sendCommand(base, "AT+CIFSR", XESP8266_Op_GetLocalIP, msecs)) {
        if (!XHostAddress_isNull(&device->m_localIP)) {
            XHostAddress* addr = XMalloc_System(sizeof(XHostAddress));
            XHostAddress_init(addr); XClass_copy_base(addr, &device->m_localIP); return addr;
        }
    }*/
    // 2. WIFI GOT IP 后 AT+CIPSTA? 可能短暂返回 "busy p..."，在调用方
    // 的超时预算内重试。
    int attempts = 3;
    int perAttempt = msecs < 0 ? -1 : msecs / attempts;
    if (perAttempt > 0 && perAttempt < 1000) perAttempt = 1000;
    for (int attempt = 0; attempt < attempts; ++attempt) {
        if (XATComm_sendCommand(base, "AT+CIPSTA?", XESP8266_Op_GetLocalIP, perAttempt) &&
            !XHostAddress_isNull(&device->m_localIP)) {
            XHostAddress* addr = XMalloc_System(sizeof(XHostAddress));
            if (!addr) return NULL;
            XHostAddress_init(addr);
            XClass_copy_base(addr, &device->m_localIP);
            return addr;
        }
        if (msecs < 0) break;
        uint64_t retryDeadline = XDateTime_currentMSecsSinceEpoch() + 250;
        while (XDateTime_currentMSecsSinceEpoch() < retryDeadline) {
            XCoreApplication_processEvents(XEventLoop_AllEvents);
            VXESP8266_pollIo(device);
        }
    }
    return NULL;
}

// ========== 同步等待 ==========

bool XESP8266Wifi_waitForWiFiConnected(XESP8266Wifi* device, int msecs) {
    if (ISNULL(device, "device is NULL")) return false;
    if (device->m_wifiStatus == XESP8266_Status_Connected) return true;
    if (device->m_wifiStatus != XESP8266_Status_Connecting) return false;
    if (msecs == 0) return false;

    uint64_t deadline = msecs > 0 ? XDateTime_currentMSecsSinceEpoch() + msecs : 0;
    while (device->m_wifiStatus != XESP8266_Status_Connected) {
        if (device->m_wifiStatus == XESP8266_Status_Error || device->m_wifiStatus == XESP8266_Status_Disconnected)
            return false;
        XCoreApplication_processEvents(XEventLoop_AllEvents);
        VXESP8266_pollIo(device);
        if (msecs > 0 && XDateTime_currentMSecsSinceEpoch() >= deadline) return false;
    }
    return true;
}

bool XESP8266Wifi_waitForServerConnected(XESP8266Wifi* device, int msecs) {
    if (ISNULL(device, "device is NULL")) return false;
    if (device->m_pendingStatus == XESP8266_Status_Connected) return true;
    if (device->m_pendingStatus != XESP8266_Status_Connecting) return false;
    if (msecs == 0) return false;

    uint64_t deadline = msecs > 0 ? XDateTime_currentMSecsSinceEpoch() + msecs : 0;
    while (device->m_pendingStatus != XESP8266_Status_Connected) {
        if (device->m_pendingStatus == XESP8266_Status_Error || device->m_pendingStatus == XESP8266_Status_Disconnected)
            return false;
        XCoreApplication_processEvents(XEventLoop_AllEvents);
        VXESP8266_pollIo(device);
        if (msecs > 0 && XDateTime_currentMSecsSinceEpoch() >= deadline) return false;
    }
    return true;
}

bool XESP8266Wifi_waitForDisconnectConn(XESP8266Wifi* device, int connId, int msecs) {
    if (ISNULL(device, "device is NULL")) return false;
    if (!device->m_multiConnMode) connId = 0;
    else if (connId < 0 || connId >= XESP8266_MAX_CONNS) return false;
    if (device->m_connections[connId].status == XESP8266_Status_Disconnected) return true;
    if (msecs == 0) return false;

    uint64_t deadline = msecs > 0 ? XDateTime_currentMSecsSinceEpoch() + msecs : 0;
    while (device->m_connections[connId].status != XESP8266_Status_Disconnected) {
        XCoreApplication_processEvents(XEventLoop_AllEvents);
        VXESP8266_pollIo(device);
        if (msecs > 0 && XDateTime_currentMSecsSinceEpoch() >= deadline) return false;
    }
    return true;
}

// ========== 信号实现 ==========

void* XESP8266Wifi_wifiStatusChanged_signal(XESP8266Wifi* device, XESP8266WifiStatus status)
{
    XEmitSignal(device, XESP8266Wifi_wifiStatusChanged_signal, XVarList_Create(XVar(XESP8266WifiStatus, status)), NULL, NULL, XEVENT_PRIORITY_NORMAL);
}

void* XESP8266Wifi_serverStatusChanged_signal(XESP8266Wifi* device, int connId, XESP8266WifiStatus status)
{
    XEmitSignal(device, XESP8266Wifi_serverStatusChanged_signal, XVarList_Create(XVar(int, connId), XVar(XESP8266WifiStatus, status)), NULL, NULL, XEVENT_PRIORITY_NORMAL);
}

void* XESP8266Wifi_readyRead_signal(XESP8266Wifi* device, int connId)
{
    XEmitSignal(device, XESP8266Wifi_readyRead_signal, XVarList_Create(XVar(int, connId)), NULL, NULL, XEVENT_PRIORITY_NORMAL);
}

static void error_signal_data_delete(XVarList* list)
{
    if (!list) return;
    XVarList_start(list);
    XVarList_argOffset(list, int);
    char* msg = XVarList_arg(list, char*);
    if (msg) XFree_System(msg);
}

void* XESP8266Wifi_error_signal(XESP8266Wifi* device, int errorCode, const char* errorMsg)
{
    if (device) {
        size_t len = errorMsg ? strlen(errorMsg) : 0;
        char* msg = len ? XMalloc_System(len + 1) : NULL;
        if (msg) strcpy(msg, errorMsg);
        XVarList* list = XVarList_Create(XVar(int, errorCode), XVar(char*, msg));
        XObject_emitSignal(device, XESP8266Wifi_error_signal, list, error_signal_data_delete, NULL, XEVENT_PRIORITY_NORMAL);
    }
    return XESP8266Wifi_error_signal;
}

void* XESP8266Wifi_connect_signal(XESP8266Wifi* device, int connId)
{
    XEmitSignal(device, XESP8266Wifi_connect_signal, XVarList_Create(XVar(int, connId)), NULL, NULL, XEVENT_PRIORITY_NORMAL);
}

void* XESP8266Wifi_disconnect_signal(XESP8266Wifi* device, int connId)
{
    XEmitSignal(device, XESP8266Wifi_disconnect_signal, XVarList_Create(XVar(int, connId)), NULL, NULL, XEVENT_PRIORITY_NORMAL);
}
