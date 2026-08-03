#include "XESP8266Wifi.h"
#include "XIODevice.h"
#include "XCoreApplication.h"
#include "XEventLoop.h"
#include "XLockFreeQueue.h"
#include "XCircularQueue.h"
#include <string.h>

// ========== 辅助宏（通过 m_base 访问 XATComm 字段） ==========
#define AT_RESPONSE_DATA    ((const char*)XByteArray_data(device->m_base.m_responseBuffer))
#define AT_RESPONSE_SIZE    XByteArray_size_base(device->m_base.m_responseBuffer)
#define AT_RESPONSE_OK      (AT_RESPONSE_DATA && strstr(AT_RESPONSE_DATA, "OK"))
#define AT_RESPONSE_ERROR   (AT_RESPONSE_DATA && (strstr(AT_RESPONSE_DATA, "ERROR") || strstr(AT_RESPONSE_DATA, "FAIL")))
#define AT_RESPONSE_CONNECT (AT_RESPONSE_DATA && strstr(AT_RESPONSE_DATA, "CONNECT"))
#define AT_RESPONSE_DISCONNECT (AT_RESPONSE_DATA && strstr(AT_RESPONSE_DATA, "DISCONNECT"))

// ========== 前向声明 ==========
static bool g_ok(XESP8266Wifi* device);
static bool g_prompt(XESP8266Wifi* device);
static bool g_error(XESP8266Wifi* device);
static bool Reset(XESP8266Wifi* device);
static bool TestAT(XESP8266Wifi* device);
static bool SetMultiConnMode(XESP8266Wifi* device);
static bool SetMode(XESP8266Wifi* device);
static bool WriteData(XESP8266Wifi* device);
static bool ConfigAP(XESP8266Wifi* device);
static bool StartServer(XESP8266Wifi* device);
static bool StopServer(XESP8266Wifi* device);
static bool SetTransparent(XESP8266Wifi* device);
static bool EnterTransparent(XESP8266Wifi* device);
static bool ConnectWiFi(XESP8266Wifi* device);
static bool DisconnectWiFi(XESP8266Wifi* device);
static bool ConnectServer(XESP8266Wifi* device);
static bool DisconnectServer(XESP8266Wifi* device);
static bool GetLocalIP(XESP8266Wifi* device);
static bool ConnectClient(XESP8266Wifi* device);
static bool ClosedClient(XESP8266Wifi* device);
static bool recvData(XESP8266Wifi* device, const char* buffer);
static void setBuffer(XESP8266Wifi* device, int connId);

static void clearConnBuffers(XESP8266ConnInfo* info)
{
    if (!info) return;
    if (info->m_readBuffer) {
        XQueueBase_delete_base(info->m_readBuffer);
        info->m_readBuffer = NULL;
    }
    if (info->m_writeBuffer) {
        XQueueBase_delete_base(info->m_writeBuffer);
        info->m_writeBuffer = NULL;
    }
    info->remaining_recv_size = 0;
}

// ========== 通用响应处理 ==========

bool g_ok(XESP8266Wifi* device)
{
    if (AT_RESPONSE_OK)
    {
        device->m_base.m_operationResult = 1;
        if (device->m_base.m_timeoutId != XFD_INVALID) {
            XObject_killTimer(&device->m_base.m_base, device->m_base.m_timeoutId);
            device->m_base.m_timeoutId = XFD_INVALID;
        }
        XESP8266Wifi_ok_signal(device);
        return true;
    }
    return false;
}

bool g_error(XESP8266Wifi* device)
{
    if (AT_RESPONSE_ERROR)
    {
        char* data = (char*)XByteArray_data(device->m_base.m_responseBuffer);
        device->m_base.m_operationResult = 0;
        device->m_base.m_currentOp = XESP8266_Op_None;
        XESP8266Wifi_error_signal(device, -1, data ? data : "AT command failed");
        if (device->m_base.m_timeoutId != XFD_INVALID) {
            XObject_killTimer(&device->m_base.m_base, device->m_base.m_timeoutId);
            device->m_base.m_timeoutId = XFD_INVALID;
        }
        return true;
    }
    return false;
}

bool Reset(XESP8266Wifi* device)  { if (g_ok(device)) return true; if (g_error(device)) return true; return false; }
bool TestAT(XESP8266Wifi* device) { if (g_ok(device)) return true; if (g_error(device)) return true; return false; }
bool SetMultiConnMode(XESP8266Wifi* device) { if (g_ok(device)) return true; if (g_error(device)) return true; return false; }
bool SetMode(XESP8266Wifi* device)  { if (g_ok(device)) return true; if (g_error(device)) return true; return false; }
bool WriteData(XESP8266Wifi* device) { if (g_prompt(device)) return true; if (g_ok(device)) return true; if (g_error(device)) return true; return false; }
bool ConfigAP(XESP8266Wifi* device) { if (g_ok(device)) return true; if (g_error(device)) return true; return false; }
bool StartServer(XESP8266Wifi* device) { if (g_ok(device)) return true; if (g_error(device)) return true; return false; }
bool StopServer(XESP8266Wifi* device) { if (g_ok(device)) return true; if (g_error(device)) return true; return false; }
bool SetTransparent(XESP8266Wifi* device) { if (g_ok(device)) return true; if (g_error(device)) return true; return false; }
bool EnterTransparent(XESP8266Wifi* device) { if (g_prompt(device)) return true; if (g_ok(device)) return true; if (g_error(device)) return true; return false; }

static bool parseIPFromBuf_cmd(const char* buf, const char* prefix, char* out, size_t outLen) {
    const char* ipStart = strstr(buf, prefix);
    if (!ipStart) return false;
    ipStart = strchr(ipStart, '"');
    if (!ipStart) return false;
    ipStart++;
    const char* ipEnd = strchr(ipStart, '"');
    if (!ipEnd || ipEnd == ipStart) return false;
    size_t len = (size_t)(ipEnd - ipStart);
    if (len >= outLen) return false;
    memcpy(out, ipStart, len);
    out[len] = '\0';
    return true;
}

bool GetLocalIP(XESP8266Wifi* device) {
    if (!AT_RESPONSE_DATA) return false;
    bool gotOk = g_ok(device);
    bool gotError = !gotOk && g_error(device);
    if (gotOk || gotError) {
        char ipStr[64] = {0};
        size_t dataLen = AT_RESPONSE_SIZE;
        char* buf = XMalloc_System(dataLen + 1);
        if (!buf) return true;
        memcpy(buf, AT_RESPONSE_DATA, dataLen);
        buf[dataLen] = '\0';
        const char* data = buf;
        if (parseIPFromBuf_cmd(data, "+CIPSTA:netmask:", ipStr, sizeof(ipStr)) &&
            ipStr[0] && strcmp(ipStr, "0.0.0.0") != 0) {
            XHostAddress_setAddress(&device->m_netmask, ipStr);
        }
        if (parseIPFromBuf_cmd(data, "+CIPSTA:gateway:", ipStr, sizeof(ipStr)) &&
            ipStr[0] && strcmp(ipStr, "0.0.0.0") != 0) {
            XHostAddress_setAddress(&device->m_gateway, ipStr);
        }
        if (parseIPFromBuf_cmd(data, "+CIPSTA:ip:", ipStr, sizeof(ipStr)) &&
            ipStr[0] && strcmp(ipStr, "0.0.0.0") != 0) {
            XHostAddress_setAddress(&device->m_localIP, ipStr);
        }
        XFree_System(buf);
        /* 不含 CIPSTA 数据的 OK 仍表示命令已完成，例如 ESP-AT 短暂返回
         * "busy p..." 时。调用方可安全重试，不会让 m_currentOp 卡住。 */
        return true;
    }
    return false;
}

// ========== WiFi 连接/断开处理 ==========

bool ConnectWiFi(XESP8266Wifi* device)
{
    if (!AT_RESPONSE_DATA) return false;
    char* data = (char*)XByteArray_data(device->m_base.m_responseBuffer);
    char* hasDisconnect = strstr(data, "DISCONNECT");
    char* hasOk = strstr(data, "OK");
    char* hasGotIp = strstr(data, "WIFI GOT IP");

    if (!hasDisconnect && (hasOk || hasGotIp))
    {
        device->m_wifiStatus = XESP8266_Status_Connected;
        XESP8266Wifi_wifiStatusChanged_signal(device, XESP8266_Status_Connected);

        device->m_base.m_operationResult = 1;
        if (device->m_base.m_timeoutId != XFD_INVALID) {
            XObject_killTimer(&device->m_base.m_base, device->m_base.m_timeoutId);
            device->m_base.m_timeoutId = XFD_INVALID;
        }
        XESP8266Wifi_ok_signal(device);
        return true;
    }

    char* hasError = hasDisconnect ? NULL : strstr(data, "ERROR");
    char* hasFail = (hasDisconnect || hasError) ? NULL : strstr(data, "FAIL");
    char* hasClosed = hasError ? NULL : strstr(data, "CLOSED");
    if (hasDisconnect || hasError || hasFail || hasClosed)
    {
        if (hasError || hasFail) {
            device->m_wifiStatus = XESP8266_Status_Error;
            device->m_base.m_operationResult = 0;
            device->m_base.m_currentOp = XESP8266_Op_None;
            XESP8266Wifi_wifiStatusChanged_signal(device, XESP8266_Status_Error);
            XESP8266Wifi_error_signal(device, -1, data);
        }
        else {
            if (hasClosed) *hasClosed = 0;
            else { char* hasWifi = strstr(data, "WIFI"); if (hasWifi) *hasWifi = 0; }
        }

        XByteArray_clear_base(device->m_base.m_responseBuffer);
        /* CWJAP 协商期间（或模块丢弃旧关联时）常会单独发出 WIFI
         * DISCONNECT。它不是本次连接的终态响应；保持 m_currentOp 有效，
         * 让后续 WIFI CONNECTED/GOT IP 完成连接。ERROR/FAIL/CLOSED 仍为终态。 */
        if (hasDisconnect && !hasError && !hasFail && !hasClosed)
            return false;
        return true;
    }
    return false;
}

bool DisconnectWiFi(XESP8266Wifi* device)
{
    if (!AT_RESPONSE_DATA) return false;
    char* data = (char*)XByteArray_data(device->m_base.m_responseBuffer);

    if (strstr(data, "OK") || strstr(data, "DISCONNECT"))
    {
        device->m_wifiStatus = XESP8266_Status_Disconnected;
        XESP8266Wifi_wifiStatusChanged_signal(device, XESP8266_Status_Disconnected);

        device->m_base.m_operationResult = 1;
        if (device->m_base.m_timeoutId != XFD_INVALID) {
            XObject_killTimer(&device->m_base.m_base, device->m_base.m_timeoutId);
            device->m_base.m_timeoutId = XFD_INVALID;
        }
        XESP8266Wifi_ok_signal(device);
        return true;
    }
    return false;
}

// ========== 服务器连接处理 ==========

bool ConnectServer(XESP8266Wifi* device)
{
    if (!AT_RESPONSE_DATA) return false;
    char* data = (char*)XByteArray_data(device->m_base.m_responseBuffer);
    int connId = device->m_pendingConnId;

    if (strstr(data, "CONNECT"))
    {
        if (connId < 0 || connId >= XESP8266_MAX_CONNS) return false;
        bool wasConnected = device->m_connections[connId].status == XESP8266_Status_Connected;
        device->m_connections[connId].status = XESP8266_Status_Connected;
        device->m_pendingStatus = XESP8266_Status_Connected;
        if (!wasConnected) device->m_activeConnCount++;
        setBuffer(device, connId);
        device->m_base.m_operationResult = 1;
        if (!wasConnected)
            XESP8266Wifi_serverStatusChanged_signal(device, connId, XESP8266_Status_Connected);

        if (!wasConnected) XESP8266Wifi_ok_signal(device);
        XESP8266Wifi_connect_signal(device, connId);
        if (device->m_base.m_timeoutId != XFD_INVALID) {
            XObject_killTimer(&device->m_base.m_base, device->m_base.m_timeoutId);
            device->m_base.m_timeoutId = XFD_INVALID;
        }
        return true;
    }
    else if (strstr(data, "ERROR") || strstr(data, "FAIL"))
    {
        device->m_connections[connId].status = XESP8266_Status_Error;
        device->m_pendingStatus = XESP8266_Status_Error;
        device->m_base.m_operationResult = 0;
        device->m_base.m_currentOp = XESP8266_Op_None;
        XESP8266Wifi_serverStatusChanged_signal(device, connId, XESP8266_Status_Error);
        XESP8266Wifi_error_signal(device, -1, data);
        return true;
    }
    return false;
}

bool DisconnectServer(XESP8266Wifi* device)
{
    if (!AT_RESPONSE_DATA) return false;
    char* data = (char*)XByteArray_data(device->m_base.m_responseBuffer);

    if (strstr(data, "OK"))
    {
        device->m_base.m_operationResult = 1;
        if (device->m_pendingConnId >= 0 && device->m_pendingConnId < XESP8266_MAX_CONNS) {
            XESP8266ConnInfo* info = &device->m_connections[device->m_pendingConnId];
            bool wasConnected = info->connId != -1 && info->status != XESP8266_Status_Disconnected;
            int connId = device->m_pendingConnId;
            info->connId = -1;
            info->status = XESP8266_Status_Disconnected;
            clearConnBuffers(info);
            device->m_pendingConnId = -1;
            if (wasConnected) {
                if (device->m_activeConnCount > 0)
                    device->m_activeConnCount--;
                XESP8266Wifi_serverStatusChanged_signal(device, connId, XESP8266_Status_Disconnected);
                XESP8266Wifi_disconnect_signal(device, connId);
            }
        }
        else {
            for (int i = 0; i < XESP8266_MAX_CONNS; i++) {
                bool wasConnected = device->m_connections[i].connId != -1 &&
                    device->m_connections[i].status != XESP8266_Status_Disconnected;
                device->m_connections[i].connId = -1;
                device->m_connections[i].status = XESP8266_Status_Disconnected;
                clearConnBuffers(&device->m_connections[i]);
                if (wasConnected) {
                    XESP8266Wifi_serverStatusChanged_signal(device, i, XESP8266_Status_Disconnected);
                    XESP8266Wifi_disconnect_signal(device, i);
                }
            }
            device->m_activeConnCount = 0;
        }
        XESP8266Wifi_ok_signal(device);
        if (device->m_base.m_timeoutId != XFD_INVALID) {
            XObject_killTimer(&device->m_base.m_base, device->m_base.m_timeoutId);
            device->m_base.m_timeoutId = XFD_INVALID;
        }
        return true;
    }
    if (g_error(device)) return true;
    return false;
}

// ========== 客户端连接/断开 ==========

bool ConnectClient(XESP8266Wifi* device)
{
    if (!AT_RESPONSE_DATA) return false;
    char* data = (char*)XByteArray_data(device->m_base.m_responseBuffer);
    bool handled = false;
    char* cursor = data;
    while (cursor) {
        char* connect = strstr(cursor, ",CONNECT");
        if (!connect) break;
        int connId = -1;
        char* scanStart = connect;
        while (scanStart > data && scanStart[-1] >= '0' && scanStart[-1] <= '9')
            --scanStart;
        if (sscanf(scanStart, "%d,CONNECT", &connId) == 1 &&
            connId >= 0 && connId < XESP8266_MAX_CONNS) {
            XESP8266ConnInfo* info = &device->m_connections[connId];
            if (info->status != XESP8266_Status_Connected) {
                info->connId = connId;
                info->status = XESP8266_Status_Connected;
                info->isServer = true;
                device->m_activeConnCount++;
                setBuffer(device, connId);
                XESP8266Wifi_serverStatusChanged_signal(device, connId, XESP8266_Status_Connected);
                XESP8266Wifi_connect_signal(device, connId);
            }
            handled = true;
        }
        cursor = connect + strlen(",CONNECT");
    }
    return handled;
}

bool ClosedClient(XESP8266Wifi* device)
{
    if (!AT_RESPONSE_DATA) return false;
    char* data = (char*)XByteArray_data(device->m_base.m_responseBuffer);
    bool handled = false;
    char* cursor = data;
    while (cursor) {
        char* closed = strstr(cursor, ",CLOSED");
        if (!closed) break;
        int connId = -1;
        char* scanStart = closed;
        while (scanStart > data && scanStart[-1] >= '0' && scanStart[-1] <= '9')
            --scanStart;
        if (sscanf(scanStart, "%d,CLOSED", &connId) == 1 &&
            connId >= 0 && connId < XESP8266_MAX_CONNS) {
            XESP8266ConnInfo* info = &device->m_connections[connId];
            bool wasConnected = info->connId != -1 && info->status != XESP8266_Status_Disconnected;
            info->connId = -1;
            info->status = XESP8266_Status_Disconnected;
            info->isServer = true;
            if (info->m_readBuffer) { XQueueBase_delete_base(info->m_readBuffer); info->m_readBuffer = NULL; }
            if (info->m_writeBuffer) { XQueueBase_delete_base(info->m_writeBuffer); info->m_writeBuffer = NULL; }
            if (wasConnected) {
                if (device->m_activeConnCount > 0) device->m_activeConnCount--;
                XESP8266Wifi_serverStatusChanged_signal(device, connId, XESP8266_Status_Disconnected);
                XESP8266Wifi_disconnect_signal(device, connId);
            }
            handled = true;
        }
        cursor = closed + strlen(",CLOSED");
    }
    return handled;
}

// ========== 数据接收 ==========

bool recvData(XESP8266Wifi* device, const char* buffer)
{
    size_t responseLen = AT_RESPONSE_SIZE;
    const char* base = (const char*)XByteArray_data(device->m_base.m_responseBuffer);

    /* 续传：继续接收上次未收完的 +IPD 数据 */
    if (device->m_base.m_currentOp == XESP8266_Op_RecvData)
    {
        int connId = device->m_pendingConnId;
        int remaining = device->m_connections[connId].remaining_recv_size;
        int size = (int)((int)responseLen < remaining ? (int)responseLen : remaining);

        if (size > 0 && device->m_connections[connId].m_readBuffer) {
            for (int i = 0; i < size; i++)
                XQueueBase_push_base(device->m_connections[connId].m_readBuffer, buffer + i);
        }
        XESP8266Wifi_readyRead_signal(device, connId);

        remaining -= size;
        if (remaining > 0) {
            device->m_connections[connId].remaining_recv_size = remaining;
            XByteArray_clear_base(device->m_base.m_responseBuffer);
            return false;
        }
        device->m_base.m_currentOp = XESP8266_Op_None;
        /* 一次串口读取可能同时包含前一帧尾部和下一帧开头，保留超出当前
         * 载荷的字节供下一次解析。 */
        if ((size_t)size < responseLen) {
            char* mutableBase = (char*)base;
            memmove(mutableBase, buffer + size, responseLen - (size_t)size);
            XByteArray_resize_base(device->m_base.m_responseBuffer,
                responseLen - (size_t)size);
            mutableBase[responseLen - (size_t)size] = '\0';
        } else {
            XByteArray_clear_base(device->m_base.m_responseBuffer);
        }
        return true;
    }

    /* 首次：从 buffer 中解析 +IPD,<id>,<len>:<data> */
    char* ipd = strstr((char*)buffer, "+IPD,");
    if (!ipd) return false;

    int connId = 0, len = 0;
    if (device->m_multiConnMode) {
        if (sscanf(ipd, "+IPD,%d,%d:", &connId, &len) != 2) return false;
    } else {
        if (sscanf(ipd, "+IPD,%d:", &len) != 1) return false;
    }
    if (len <= 0 || connId < 0 || connId >= XESP8266_MAX_CONNS) return false;

    const char* colon = strstr(ipd, ":");
    if (!colon) return false;
    const char* pdata = colon + 1;

    /* 本次能从缓冲区读到的数据长度 = base+responseLen - pdata */
    int dataAvail = (int)(base + responseLen - pdata);
    if (dataAvail <= 0) return false;
    int recvNow = (dataAvail < len) ? dataAvail : len;

    device->m_pendingConnId = connId;
    if (recvNow > 0 && device->m_connections[connId].m_readBuffer) {
        for (int i = 0; i < recvNow; i++)
            XQueueBase_push_base(device->m_connections[connId].m_readBuffer, pdata + i);
    }
    XESP8266Wifi_readyRead_signal(device, connId);

    int remaining = len - recvNow;
    if (remaining > 0) {
        device->m_connections[connId].remaining_recv_size = remaining;
        XByteArray_clear_base(device->m_base.m_responseBuffer);
        device->m_base.m_currentOp = XESP8266_Op_RecvData;
        return false;
    }
    device->m_base.m_currentOp = XESP8266_Op_None;
    {
        size_t consumed = (size_t)(pdata - base) + (size_t)recvNow;
        if (consumed < responseLen) {
            char* mutableBase = (char*)base;
            memmove(mutableBase, base + consumed, responseLen - consumed);
            XByteArray_resize_base(device->m_base.m_responseBuffer,
                responseLen - consumed);
            mutableBase[responseLen - consumed] = '\0';
        } else {
            XByteArray_clear_base(device->m_base.m_responseBuffer);
        }
    }
    return true;
}

void setBuffer(XESP8266Wifi* device, int connId)
{
    if (!device || connId < 0 || connId >= XESP8266_MAX_CONNS) return;
    XESP8266ConnInfo* conn = &device->m_connections[connId];
    if (!conn->m_readBuffer) {
        conn->m_readBuffer = XCircularQueue_Create(char, XESP8266_BUFFER_SIZE);
    }
    if (!conn->m_writeBuffer) {
        conn->m_writeBuffer = XCircularQueue_Create(char, XESP8266_BUFFER_SIZE);
    }
}

// ========== AT 响应处理入口 ==========

/**
 * @brief 处理AT指令响应（ESP8266 专用解析）
 */
void VXESP8266_processResponse_cmd(XESP8266Wifi* device)
{
    if (ISNULL(device, "device is NULL")) return;

    bool hand = false;
    bool commandCompleted = false;

    // 透传模式下直接转发数据
    if (device->m_transparentMode) {
        int connId = 0;
        if (device->m_connections[connId].m_readBuffer) {
            size_t totalSize = XByteArray_size_base(device->m_base.m_responseBuffer);
            const char* respData = (const char*)XByteArray_data(device->m_base.m_responseBuffer);
            for (size_t i = 0; i < totalSize; i++) {
                XQueueBase_push_base(device->m_connections[connId].m_readBuffer, respData + i);
            }
        }
        XESP8266Wifi_readyRead_signal(device, connId);
        XByteArray_clear_base(device->m_base.m_responseBuffer);
        return;
    }
    else if (device->m_base.m_currentOp == XESP8266_Op_RecvData) {
        hand = recvData(device, AT_RESPONSE_DATA);
    }
    else {
        /* 排空当前缓冲区中的每个完整 +IPD 帧。ESP-AT 可将多个通道合并到
         * 一次 UART 读取中，例如 +IPD,0,... 后紧接 +IPD,1,...。 */
        while (AT_RESPONSE_DATA && strstr(AT_RESPONSE_DATA, "+IPD,")) {
            size_t before = AT_RESPONSE_SIZE;
            char* IPD = strstr((char*)AT_RESPONSE_DATA, "+IPD,");
            bool frameDone = recvData(device, IPD);
            hand = true;
            if (!frameDone || device->m_base.m_currentOp == XESP8266_Op_RecvData)
                break;
            if (AT_RESPONSE_SIZE >= before &&
                strstr(AT_RESPONSE_DATA, "+IPD,") == IPD)
                break;
        }
    }

    if (!hand) {
        XESP8266Wifi_response_signal(device, AT_RESPONSE_DATA);

        switch (device->m_base.m_currentOp) {
        case XESP8266_Op_Reset:            commandCompleted = Reset(device); break;
        case XESP8266_Op_TestAT:           commandCompleted = TestAT(device); break;
        case XESP8266_Op_SetMultiConnMode: commandCompleted = SetMultiConnMode(device); break;
        case XESP8266_Op_SetMode:          commandCompleted = SetMode(device); break;
        case XESP8266_Op_WriteData:        commandCompleted = WriteData(device); break;
        case XESP8266_Op_ConfigAP:         commandCompleted = ConfigAP(device); break;
        case XESP8266_Op_StartServer:      commandCompleted = StartServer(device); break;
        case XESP8266_Op_StopServer:       commandCompleted = StopServer(device); break;
        case XESP8266_Op_SetTransparent:   commandCompleted = SetTransparent(device); break;
        case XESP8266_Op_EnterTransparent: commandCompleted = EnterTransparent(device); break;
        case XESP8266_Op_ConnectWiFi:      commandCompleted = ConnectWiFi(device); break;
        case XESP8266_Op_DisconnectWiFi:   commandCompleted = DisconnectWiFi(device); break;
        case XESP8266_Op_ConnectServer:    commandCompleted = ConnectServer(device); break;
        case XESP8266_Op_DisconnectServer: commandCompleted = DisconnectServer(device); break;
        case XESP8266_Op_GetLocalIP:       commandCompleted = GetLocalIP(device); break;
        case XESP8266_Op_RecvData:         commandCompleted = true; break;
        default: break;
        }

        /* 连接事件可与 AT 命令响应交错到达。即使事件已处理，也必须保留
         * 当前操作码，直至本次命令的 OK、ERROR 或提示符真正到达。 */
        if (!device->m_transparentMode
            && device->m_base.m_currentOp != XESP8266_Op_RecvData) {
            bool asyncEventHandled = ConnectClient(device);
            if (!asyncEventHandled) asyncEventHandled = ClosedClient(device);
            hand = commandCompleted || asyncEventHandled;
        } else {
            hand = commandCompleted;
        }
    }

    if (hand && device->m_base.m_currentOp != XESP8266_Op_RecvData) {
        XByteArray_clear_base(device->m_base.m_responseBuffer);
        if (commandCompleted)
            device->m_base.m_currentOp = XESP8266_Op_None;
    }
}

/* CIPSEND 在载荷前返回 '>' 提示符，而不是 OK。 */
bool g_prompt(XESP8266Wifi* device)
{
    if (AT_RESPONSE_DATA && strchr(AT_RESPONSE_DATA, '>'))
    {
        device->m_base.m_operationResult = 1;
        if (device->m_base.m_timeoutId != XFD_INVALID) {
            XObject_killTimer(&device->m_base.m_base, device->m_base.m_timeoutId);
            device->m_base.m_timeoutId = XFD_INVALID;
        }
        return true;
    }
    return false;
}
