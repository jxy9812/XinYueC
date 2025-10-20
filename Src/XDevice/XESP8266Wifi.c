#include "XESP8266Wifi.h"
#include "XMemory.h"
#include "XString.h"
#include "XTimer.h"
#include "XEventLoop.h"
#include <string.h>

#define AT_RESPONSE_OK "OK"
#define AT_RESPONSE_ERROR "ERROR"
#define AT_RESPONSE_CONNECT "CONNECT"
#define AT_RESPONSE_DISCONNECT "DISCONNECT"

// 前向声明
static bool VXESP8266_open(XESP8266Wifi* device, XIODeviceBaseMode mode);
static bool VXESP8266_close(XESP8266Wifi* device);
static size_t VXESP8266_write(XESP8266Wifi* device, const char* data, size_t maxSize);
static size_t VXESP8266_read(XESP8266Wifi* device, char* data, size_t maxSize);
static size_t VXESP8266_getBytesAvailable(XESP8266Wifi* device);
static void VXESP8266_processResponse(XESP8266Wifi* device);

static void VXESP8266_deinit(XESP8266Wifi* device);
static void VXESP8266_timeoutCallback(void* userData);

/**
 * @brief 虚函数表初始化
 */
XVtable* XESP8266Wifi_class_init() {
    XVTABLE_CREAT_DEFAULT
#if VTABLE_ISSTACK
        XVTABLE_STACK_INIT_DEFAULT(XCLASS_VTABLE_GET_SIZE(XESP8266Wifi))
#else
        XVTABLE_HEAP_INIT_DEFAULT
#endif
        // 继承XIODeviceBase
        XVTABLE_INHERIT_DEFAULT(XIODeviceBase_class_init());
    // 重载虚函数
    XVTABLE_OVERLOAD_DEFAULT(EXIODeviceBase_Open, VXESP8266_open);
    XVTABLE_OVERLOAD_DEFAULT(EXIODeviceBase_Close, VXESP8266_close);
    XVTABLE_OVERLOAD_DEFAULT(EXIODeviceBase_Write, VXESP8266_write);
    XVTABLE_OVERLOAD_DEFAULT(EXIODeviceBase_Read, VXESP8266_read);
    XVTABLE_OVERLOAD_DEFAULT(EXIODeviceBase_GetBytesAvailable, VXESP8266_getBytesAvailable);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Deinit, VXESP8266_deinit);
#if SHOWCONTAINERSIZE
    printf("XESP8266Wifi size:%d\n", XVtable_size(XVTABLE_DEFAULT));
#endif
    return XVTABLE_DEFAULT;
}

/**
 * @brief 初始化ESP8266设备
 */
void XESP8266Wifi_init(XESP8266Wifi* device, XIODeviceBase* io) {
    if (ISNULL(device, "device is NULL")) return;

    // 初始化父类
    memset(((XObject*)device) + 1, 0, sizeof(XESP8266Wifi) - sizeof(XObject));
    XIODeviceBase_init(&device->m_class);

    // 初始化成员变量
    device->m_io = io;
    device->m_wifiStatus = XESP8266_Status_Disconnected;
    //device->m_serverStatus = XESP8266_Status_Disconnected;
    device->m_timeoutTimer = XTimer_create();
    XTimerBase_setUserData_base(device->m_timeoutTimer,device);
    XTimerBase_setTimerCallback_base(device->m_timeoutTimer, VXESP8266_timeoutCallback);
    device->m_ssid = XString_create();
    device->m_password = XString_create();
    //device->m_serverIP = XString_create();
    //device->m_serverPort = 0;
    //device->m_protocol = XESP8266_Protocol_TCP;
    device->m_transparentMode = false;
    device->m_loop = XEventLoop_create();
    device->m_operationResult = false;
    device->m_currentOp = XESP8266_Op_None;
    device->m_responseLen = 0;
    memset(device->m_responseBuffer, 0, sizeof(device->m_responseBuffer));

    // 设置虚函数表
    XClassGetVtable(device) = XESP8266Wifi_class_init();

    // 初始化多连接数组
    device->m_activeConnCount = 0;
    device->m_multiConnMode = false;  // 默认关闭多连接
    for (int i = 0; i < XESP8266_MAX_CONNS; i++) {
        device->m_connections[i].connId = -1;  // -1表示未使用
        device->m_connections[i].status = XESP8266_Status_Disconnected;
        device->m_connections[i].isServer = false;
        memset(device->m_connections[i].ip, 0, sizeof(device->m_connections[i].ip));
        device->m_connections[i].port = 0;
    }

    // 绑定底层设备的readyRead信号
    if (device->m_io) {
        XObject_connect(device->m_io,
            XIODeviceBase_readyRead_signal,
            device,
            VXESP8266_processResponse,XConnectionType_Auto);
    }
    XConnection* timeroutComm = XObject_connect(device->m_timeoutTimer, XSignal(XTimer_timeout_signal), device->m_loop, XEventLoop_quit_base, XConnectionType_Auto);
    XConnection* okComm = XObject_connect(device, XSignal(XESP8266Wifi_ok_signal), device->m_loop, XEventLoop_quit_base, XConnectionType_Auto);
    XConnection* errComm = XObject_connect(device, XSignal(XESP8266Wifi_error_signal), device->m_loop, XEventLoop_quit_base, XConnectionType_Auto);
}
void VXESP8266_deinit(XESP8266Wifi* device)
{
    if(device->m_timeoutTimer)
    {
        XTimer_delete_base(device->m_timeoutTimer);
        device->m_timeoutTimer = NULL;
    }
    if (device->m_ssid)
    {
        XString_delete_base(device->m_ssid);
        device->m_ssid = NULL;
    }
    if (device->m_password)
    {
        XString_delete_base(device->m_password);
        device->m_password = NULL;
    }
   /* if (device->m_serverIP)
    {
        XString_delete_base(device->m_serverIP);
        device->m_serverIP = NULL;
    }*/
    if (device->m_loop)
    {
        XEventLoop_delete_base(device->m_loop);
        device->m_loop = NULL;
    }
    // 释放父对象
    XVtableGetFunc(XIODeviceBase_class_init(), EXClass_Deinit, void(*)(XIODeviceBase*))(device);
}

/**
 * @brief 创建ESP8266设备实例
 */
XESP8266Wifi* XESP8266Wifi_create(XIODeviceBase* io) {
    XESP8266Wifi* device = XMemory_malloc(sizeof(XESP8266Wifi));
    if (ISNULL(device, "malloc failed")) return NULL;
    XESP8266Wifi_init(device, io);
    return device;
}

/**
 * @brief 打开设备
 */
static bool VXESP8266_open(XESP8266Wifi* device, XIODeviceBaseMode mode) {
    if (ISNULL(device, "device is NULL") || ISNULL(device->m_io, "m_io is NULL"))
        return false;

    return XIODeviceBase_open_base(device->m_io, mode);
}

/**
 * @brief 关闭设备
 */
static bool VXESP8266_close(XESP8266Wifi* device) {
    if (ISNULL(device, "device is NULL") || ISNULL(device->m_io, "m_io is NULL"))
        return false;

    return XIODeviceBase_close_base(device->m_io);
}

/**
 * @brief 写入数据
 */
static size_t VXESP8266_write(XESP8266Wifi* device, const char* data, size_t maxSize) {
    if (ISNULL(device, "device is NULL") || ISNULL(device->m_io, "m_io is NULL") ||
        ISNULL(data, "data is NULL") || maxSize == 0)
        return 0;

    return XIODeviceBase_write_base(device->m_io, data, maxSize);
}

/**
 * @brief 读取数据
 */
static size_t VXESP8266_read(XESP8266Wifi* device, char* data, size_t maxSize) {
    if (ISNULL(device, "device is NULL") || ISNULL(device->m_io, "m_io is NULL") ||
        ISNULL(data, "data is NULL") || maxSize == 0)
        return 0;

    return XIODeviceBase_read_base(device->m_io, data, maxSize);
}

/**
 * @brief 获取可用字节数
 */
static size_t VXESP8266_getBytesAvailable(XESP8266Wifi* device) {
    if (ISNULL(device, "device is NULL") || ISNULL(device->m_io, "m_io is NULL"))
        return 0;

    return XIODeviceBase_getBytesAvailable_base(device->m_io);
}

/**
 * @brief 发送AT指令通用函数
 */
static bool XESP8266Wifi_sendATCommand(XESP8266Wifi* device, const char* cmd, XESP8266WifiOpType op, int timeout) {
    if (ISNULL(device, "device is NULL") || ISNULL(cmd, "cmd is NULL"))
        return false;

    // 停止当前超时定时器
    XTimer_stop_base(device->m_timeoutTimer);

    // 保存当前操作类型
    device->m_currentOp = op;

    // 清空响应缓冲区
    device->m_responseLen = 0;
    memset(device->m_responseBuffer, 0, sizeof(device->m_responseBuffer));
    device->m_operationResult = false;
    // 发送AT指令（添加回车换行）
    char atCmd[256];
    snprintf(atCmd, sizeof(atCmd), "%s\r\n", cmd);
   
    size_t sent = XIODeviceBase_write_base(device->m_io, atCmd, strlen(atCmd));
    if (sent != strlen(atCmd)) {
        DEBUG_PRINTF("AT command send failed: %s", cmd);
        return false;
    }

    // 启动超时定时器
    if (timeout > 0) 
    {
       /* XConnection*timeroutComm= XObject_connect(device->m_timeoutTimer, XSignal(XTimer_timeout_signal), device->m_loop, XEventLoop_quit_base, XConnectionType_Auto);
        XConnection* okComm = XObject_connect(device,XSignal(XESP8266Wifi_ok_signal), device->m_loop, XEventLoop_quit_base, XConnectionType_Auto);
        XConnection* errComm = XObject_connect(device, XSignal(XESP8266Wifi_error_signal), device->m_loop, XEventLoop_quit_base, XConnectionType_Auto);*/
        
        XTimer_setInterval_base(device->m_timeoutTimer, timeout);
        XTimer_setTimeout_base(device->m_timeoutTimer, timeout);
        XTimer_start_base(device->m_timeoutTimer);
        XEventLoop_exec_base(device->m_loop);
        //XObject_disconnect_conn(timeroutComm);
        //XObject_disconnect_conn(okComm);
        //XObject_disconnect_conn(errComm);
    }

    return device->m_operationResult;
}

/**
 * @brief 处理AT指令响应
 */
static void VXESP8266_processResponse(XESP8266Wifi* device) 
{
    if (ISNULL(device, "device is NULL")) return;

    // 读取底层设备数据
    size_t available = VXESP8266_getBytesAvailable(device);
    if (available == 0) return;

    // 读取数据到响应缓冲区
    if (device->m_responseLen + available < sizeof(device->m_responseBuffer) - 1) {
        available = VXESP8266_read(device,
            device->m_responseBuffer + device->m_responseLen,
            available);
        device->m_responseLen += available;
        device->m_responseBuffer[device->m_responseLen] = '\0';
        XPrintf("\n%s\n", device->m_responseBuffer);
        
    }

    // 透传模式下直接转发数据
    if (device->m_transparentMode) {
      /*  XObject_emitSignal(device,
            XESP8266Wifi_dataReceived_signal,
            (void*)device->m_responseBuffer,
            device->m_responseLen);
        device->m_responseLen = 0;
        return;*/
    }

    // 检查是否包含OK或ERROR
    bool hasOk = (strstr(device->m_responseBuffer, AT_RESPONSE_OK) != NULL);
    bool hasError = (strstr(device->m_responseBuffer, AT_RESPONSE_ERROR) != NULL);
    bool hasConnect = (strstr(device->m_responseBuffer, AT_RESPONSE_CONNECT) != NULL);
    bool hasDisconnect = (strstr(device->m_responseBuffer, AT_RESPONSE_DISCONNECT) != NULL);

    // 触发响应信号
    XESP8266Wifi_atResponse_signal(device, (void*)device->m_responseBuffer);

    // 根据当前操作类型处理响应
    switch (device->m_currentOp) 
    {
    case XESP8266_Op_TestAT:
    case XESP8266_Op_SetMultiConnMode:
    case XESP8266_Op_SetMode:
    case XESP8266_Op_WriteData:
        if (hasOk) {
            device->m_operationResult = true;
            XTimer_stop_base(device->m_timeoutTimer);
            XESP8266Wifi_ok_signal(device);
        }
        break;
    case XESP8266_Op_ConnectWiFi:
        if (hasDisconnect)
        {//重新尝试连接
            *strstr(device->m_responseBuffer, "WIFI") = 0;
            XIODeviceBase_write_base(device->m_io, device->m_responseBuffer, strlen(device->m_responseBuffer));
            device->m_responseLen = 0;
            return;
        }
        else if (hasOk|| hasConnect) {
            // 连接成功
            device->m_wifiStatus = XESP8266_Status_Connected;
            device->m_operationResult = true;
            XESP8266Wifi_wifiStatusChanged_signal(device, XESP8266_Status_Connected);
            XESP8266Wifi_ok_signal(device);
            XTimer_stop_base(device->m_timeoutTimer);
        }
        else if (hasError) 
        {
            device->m_wifiStatus = XESP8266_Status_Error;
            device->m_operationResult = false;
            XESP8266Wifi_wifiStatusChanged_signal(device, XESP8266_Status_Error);
            XESP8266Wifi_error_signal(device,-1,"连接失败");
            XTimer_stop_base(device->m_timeoutTimer);
        }
        break;

    case XESP8266_Op_ConnectServer:
        int connId = device->m_pendingConnId;
        XTimer_stop_base(device->m_timeoutTimer);
        if (hasConnect || hasOk) {
            device->m_connections[connId].status = XESP8266_Status_Connected;
            device->m_pendingStatus = XESP8266_Status_Connected;
            device->m_activeConnCount++;
            //device->m_serverStatus = XESP8266_Status_Connected;
            device->m_operationResult = true;
            XESP8266Wifi_serverStatusChanged_signal(device, XESP8266_Status_Connected);

            XESP8266Wifi_ok_signal(device);
            XESP8266Wifi_connect_signal(device);
        }
        else if (hasError) {
            //device->m_serverStatus = XESP8266_Status_Error;
            device->m_operationResult = false;
            XESP8266Wifi_serverStatusChanged_signal(device, XESP8266_Status_Error);
        }
        break;

        // 其他操作类型的响应处理...
    default:
        break;
    }
    // 非透传模式下处理多连接数据/状态
    if (!device->m_transparentMode) {
        // 解析服务器模式下的客户端连接（如："0,CONNECT" 表示ID=0的客户端连接）
        if (strstr(device->m_responseBuffer, "CONNECT") != NULL) {
            int connId = -1;
            if (sscanf(device->m_responseBuffer, "%d,CONNECT", &connId) == 1) {
                if (connId >= 0 && connId < XESP8266_MAX_CONNS) {
                    device->m_connections[connId].connId = connId;
                    device->m_connections[connId].status = XESP8266_Status_Connected;
                    device->m_connections[connId].isServer = true;  // 服务器端的客户端连接
                    device->m_activeConnCount++;
                    // 触发客户端连接信号（可自定义信号传递connId）
                    //XESP8266Wifi_clientConnected_signal(device, connId);
                }
            }
        }

        // 解析数据接收（如："+IPD,<connId>,<len>:data"）
        char* ipd = strstr(device->m_responseBuffer, "+IPD,");
        if (ipd != NULL) {
            int connId, len;
            if (sscanf(ipd, "+IPD,%d,%d:", &connId, &len) == 2) {
                char* data = ipd + strlen("+IPD,%d,%d:") + 2;  // 定位到数据部分
                // 触发带连接ID的数据接收信号
                //XESP8266Wifi_dataReceivedWithConnId_signal(device, connId, data, len);
            }
        }
    }
    // 处理完成后清空缓冲区（非透传模式）
    if (hasOk || hasError || hasConnect || hasDisconnect) 
    {
        memset(device->m_responseBuffer, 0, device->m_responseLen<sizeof(device->m_responseBuffer)? device->m_responseLen: sizeof(device->m_responseBuffer));
        device->m_responseLen = 0;
        device->m_currentOp = XESP8266_Op_None;
    }
}


/**
 * @brief 超时回调函数
 */
static void VXESP8266_timeoutCallback(void* userData)
{
    //XPrintf("回调触发\n");
    XESP8266Wifi* device = (XESP8266Wifi*)userData;
    if (ISNULL(device, "device is NULL")) return;

    DEBUG_PRINTF("Operation timeout, op: %d", device->m_currentOp);

    // 设置错误状态
    switch (device->m_currentOp) {
    case XESP8266_Op_ConnectWiFi:
        device->m_wifiStatus = XESP8266_Status_Error;
       /* XObject_emitSignal(device,
            XESP8266Wifi_wifiStatusChanged_signal,
            (void*)XESP8266_Status_Error,
            0);*/
        break;

    case XESP8266_Op_ConnectServer:
        //device->m_serverStatus = XESP8266_Status_Error;
        /*XObject_emitSignal(device,
            XESP8266Wifi_serverStatusChanged_signal,
            (void*)XESP8266_Status_Error,
            0);*/
        break;

    default:
        break;
    }

    device->m_operationResult = false;
    device->m_currentOp = XESP8266_Op_None;
    //XEvent_set(device->m_waitEvent);
}

// 具体AT指令实现
bool XESP8266Wifi_testAT(XESP8266Wifi* device) {
    return XESP8266Wifi_sendATCommand(device, "AT", XESP8266_Op_TestAT, 1000);
}

bool XESP8266Wifi_reset(XESP8266Wifi* device) {
    if (ISNULL(device, "device is NULL")) return false;
    device->m_wifiStatus = XESP8266_Status_Disconnected;
    //device->m_serverStatus = XESP8266_Status_Disconnected;
    return XESP8266Wifi_sendATCommand(device, "AT+RST", XESP8266_Op_Reset, 3000);
}

bool XESP8266Wifi_setMultiConnMode(XESP8266Wifi* device, bool enable) {
    if (ISNULL(device, "device is NULL")) return false;
    char cmd[15];
    snprintf(cmd, sizeof(cmd), "AT+CIPMUX=%d", enable ? 1 : 0);
    bool result = XESP8266Wifi_sendATCommand(device, cmd, XESP8266_Op_SetMultiConnMode, 2000);
    if (result) {
        device->m_multiConnMode = enable;
    }
    return result;
}

bool XESP8266Wifi_setMode(XESP8266Wifi* device, XESP8266WifiMode mode) {
    if (ISNULL(device, "device is NULL")) return false;
    char cmd[15];
    snprintf(cmd, sizeof(cmd), "AT+CWMODE=%d", mode);
    return XESP8266Wifi_sendATCommand(device, cmd, XESP8266_Op_SetMode, 2000);
}

bool XESP8266Wifi_connectWiFi(XESP8266Wifi* device, const char* ssid, const char* password) 
{
    if (ISNULL(device, "device is NULL") || ISNULL(ssid, "ssid is NULL") || ISNULL(password, "password is NULL"))
        return false;

    //device->m_wifiStatus = XESP8266_Status_Connecting;
    //XObject_emitSignal(device,
    //    XESP8266Wifi_wifiStatusChanged_signal,
    //    (void*)XESP8266_Status_Connecting,
    //    0);

    XString_assign_utf8(device->m_ssid, ssid);
    XString_assign_utf8(device->m_password, password);

    char cmd[128];
    snprintf(cmd, sizeof(cmd), "AT+CWJAP=\"%s\",\"%s\"", ssid, password);
    return XESP8266Wifi_sendATCommand(device, cmd, XESP8266_Op_ConnectWiFi, 5000);
}

bool XESP8266Wifi_disconnectWiFi(XESP8266Wifi* device) {
    if (ISNULL(device, "device is NULL")) return false;
    device->m_wifiStatus = XESP8266_Status_Disconnected;
    return XESP8266Wifi_sendATCommand(device, "AT+CWQAP", XESP8266_Op_DisconnectWiFi, 3000);
}

bool XESP8266Wifi_configAP(XESP8266Wifi* device, const char* ssid, const char* password,
    int channel, XESP8266WifiEncryption encrypt) {
    if (ISNULL(device, "device is NULL") || ISNULL(ssid, "ssid is NULL") || ISNULL(password, "password is NULL"))
        return false;

    char cmd[128];
    snprintf(cmd, sizeof(cmd), "AT+CWSAP=\"%s\",\"%s\",%d,%d", ssid, password, channel, encrypt);
    return XESP8266Wifi_sendATCommand(device, cmd, XESP8266_Op_ConfigAP, 2000);
}

int XESP8266Wifi_connectServer(XESP8266Wifi* device, XESP8266WifiProtocol protocol,
    const char* ip, uint16_t port, int connId)
{
    if (ISNULL(device, "device is NULL") ||
        ISNULL(ip, "ip is NULL") || port == 0) {
        return -1;
    }
    int actualConnId = 0;
    if (!device->m_multiConnMode)
    {
        //device->m_serverStatus = XESP8266_Status_Connecting;
        XESP8266Wifi_serverStatusChanged_signal(device, XESP8266_Status_Connecting);
        //XString_assign_utf8(device->m_serverIP, ip);
        //device->m_serverPort = port;
        //device->m_protocol = protocol;

        char cmd[128];
        const char* proto = (protocol == XESP8266_Protocol_TCP) ? "TCP" : "UDP";
        snprintf(cmd, sizeof(cmd), "AT+CIPSTART=\"%s\",\"%s\",%d", proto, ip, port);
        XESP8266Wifi_sendATCommand(device, cmd, XESP8266_Op_ConnectServer, 0);
        return XESP8266_MAX_CONNS;
    }
    else
    {
        // 校验连接ID有效性
        actualConnId = connId;
        if (actualConnId == -1) {
            // 自动分配未使用的连接ID
            for (int i = 0; i < XESP8266_MAX_CONNS; i++) {
                if (device->m_connections[i].connId == -1) {
                    actualConnId = i;
                    break;
                }
            }
            if (actualConnId == -1) {
                DEBUG_PRINTF("No available connection slot");
                return -1;
            }
        }
        else if (actualConnId < 0 || actualConnId >= XESP8266_MAX_CONNS) {
            DEBUG_PRINTF("Invalid connId: %d", actualConnId);
            return -1;
        }
    }
   
    // 保存连接参数（用于后续响应处理）
    device->m_connections[actualConnId].connId = actualConnId;
    strncpy(device->m_connections[actualConnId].ip, ip, sizeof(device->m_connections[actualConnId].ip) - 1);
    device->m_connections[actualConnId].port = port;
    device->m_connections[actualConnId].protocol = protocol;
    device->m_connections[actualConnId].status = XESP8266_Status_Connecting;
    device->m_pendingConnId = actualConnId; // 标记为等待结果的连接
    device->m_pendingStatus = XESP8266_Status_Disconnected; // 初始化等待状态
    

    // 构建连接AT指令（多连接模式需要指定连接ID）
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

    // 发送AT指令（非阻塞：不进入事件循环，仅发送指令）
    bool sendOk = XESP8266Wifi_sendATCommand(device, cmd, XESP8266_Op_ConnectServer, 0);
    if (!sendOk) {
        device->m_connections[actualConnId].connId = -1; // 发送失败，释放连接ID
        return -1;
    }

    return actualConnId; // 返回连接ID，用于后续等待
}

bool XESP8266Wifi_disconnectServer(XESP8266Wifi* device) {
    if (ISNULL(device, "device is NULL")) return false;
    //device->m_serverStatus = XESP8266_Status_Disconnected;
    return XESP8266Wifi_sendATCommand(device, "AT+CIPCLOSE", XESP8266_Op_DisconnectServer, 2000);
}

bool XESP8266Wifi_disconnectConn(XESP8266Wifi* device, int connId)
{
    if (ISNULL(device, "device is NULL") || connId < 0 || connId >= XESP8266_MAX_CONNS ||
        device->m_connections[connId].connId == -1) {
        return false;
    }

    // 发送断开指定连接指令：AT+CIPCLOSE=<connId>
    char cmd[32];
    snprintf(cmd, sizeof(cmd), "AT+CIPCLOSE=%d", connId);
    bool result = XESP8266Wifi_sendATCommand(device, cmd, XESP8266_Op_DisconnectServer, 2000);
    if (result) {
        device->m_connections[connId].status = XESP8266_Status_Disconnected;
        device->m_activeConnCount--;
    }
    return result;
}

bool XESP8266Wifi_startServer(XESP8266Wifi* device, XESP8266WifiProtocol protocol, uint16_t port) {
    if (ISNULL(device, "device is NULL")) return false;
    char cmd[64];
    snprintf(cmd, sizeof(cmd), "AT+CIPSERVER=1,%d", port);
    return XESP8266Wifi_sendATCommand(device, cmd, XESP8266_Op_StartServer, 2000);
}

bool XESP8266Wifi_stopServer(XESP8266Wifi* device) {
    return XESP8266Wifi_sendATCommand(device, "AT+CIPSERVER=0", XESP8266_Op_StopServer, 2000);
}

bool XESP8266Wifi_sendData(XESP8266Wifi* device, int connId, const void* data, size_t size) {
    if (ISNULL(device, "device is NULL") || ISNULL(data, "data is NULL") || size == 0) 
    {
        return false;
    }
    if (!device->m_multiConnMode)
    {
        if (device->m_transparentMode) {
            // 透传模式直接发送
            return VXESP8266_write(device, data, size) == size;
        }
        else {
            // 非透传模式使用AT指令
            char cmd[32];
            snprintf(cmd, sizeof(cmd), "AT+CIPSEND=%d", (int)size);
            if (!XESP8266Wifi_sendATCommand(device, cmd, XESP8266_Op_WriteData, 1000)) {
                return false;
            }
            // 等待"> "提示符（简单处理，实际可能需要更复杂的等待逻辑）
            //XEventLoop_delay(100);
            return VXESP8266_write(device, data, size) == size;
        }
    }
    if (connId < 0 || connId >= XESP8266_MAX_CONNS ||
        device->m_connections[connId].status != XESP8266_Status_Connected)
        return false;
    if (device->m_transparentMode) {
        // 多连接透传需先指定连接：AT+CIPSEND=<connId>
        char cmd[32];
        snprintf(cmd, sizeof(cmd), "AT+CIPSEND=%d", connId);
        if (!XESP8266Wifi_sendATCommand(device, cmd, XESP8266_Op_WriteData, 1000)) {
            return false;
        }
        // 透传模式发送数据
        return VXESP8266_write(device, data, size) == size;
    }
    else {
        // 非透传模式：AT+CIPSEND=<connId>,<size>
        char cmd[32];
        snprintf(cmd, sizeof(cmd), "AT+CIPSEND=%d,%d", connId, (int)size);
        if (!XESP8266Wifi_sendATCommand(device, cmd, XESP8266_Op_WriteData, 1000)) {
            return false;
        }
        // 发送实际数据
        return VXESP8266_write(device, data, size) == size;
    }
}

bool XESP8266Wifi_enterTransparentMode(XESP8266Wifi* device) {
    if (ISNULL(device, "device is NULL")) return false;

    // 先设置透传模式
    if (!XESP8266Wifi_sendATCommand(device, "AT+CIPMODE=1", XESP8266_Op_None, 1000)) {
        return false;
    }
    //XEventLoop_delay(100);

    // 进入透传
    if (!XESP8266Wifi_sendATCommand(device, "AT+CIPSEND", XESP8266_Op_EnterTransparent, 1000)) {
        return false;
    }

    device->m_transparentMode = true;
    return true;
}

bool XESP8266Wifi_exitTransparentMode(XESP8266Wifi* device) {
    if (ISNULL(device, "device is NULL") || !device->m_transparentMode)
        return false;

    // 发送+++退出透传（无回车）
    size_t sent = VXESP8266_write(device, "+++", 3);
    if (sent != 3) return false;

    // 等待退出完成
    //XEventLoop_delay(100);
    device->m_transparentMode = false;
    return true;
}

// 状态获取函数
XESP8266WifiStatus XESP8266Wifi_getWiFiStatus(XESP8266Wifi* device) {
    return (device) ? device->m_wifiStatus : XESP8266_Status_Error;
}

XESP8266WifiStatus XESP8266Wifi_getServerStatus(XESP8266Wifi* device) {
    //return (device) ? device->m_serverStatus : XESP8266_Status_Error;
    return 0;
}

// 同步等待函数
bool XESP8266Wifi_waitForWiFiConnected(XESP8266Wifi* device, int msecs) {
    if (ISNULL(device, "device is NULL")) return false;

    if (device->m_wifiStatus == XESP8266_Status_Connected) {
        return true;
    }

    /*XEvent_reset(device->m_waitEvent);
    bool result = XEvent_wait(device->m_waitEvent, msecs);
    return result && device->m_operationResult;*/
}

bool XESP8266Wifi_waitForServerConnected(XESP8266Wifi* device, int msecs) {
    if (ISNULL(device, "device is NULL")) return false;

   /* if (device->m_serverStatus == XESP8266_Status_Connected) {
        return true;
    }*/

   /* XEvent_reset(device->m_waitEvent);
    bool result = XEvent_wait(device->m_waitEvent, msecs);
    return result && device->m_operationResult;*/
}

// 信号实现
void* XESP8266Wifi_wifiStatusChanged_signal(XESP8266Wifi* device, XESP8266WifiStatus status) 
{
    EmitSignal(device, XESP8266Wifi_wifiStatusChanged_signal, (void*)status,NULL,NULL, XEVENT_PRIORITY_NORMAL);
}

void* XESP8266Wifi_serverStatusChanged_signal(XESP8266Wifi* device, XESP8266WifiStatus status) 
{
    EmitSignal(device, XESP8266Wifi_serverStatusChanged_signal, (void*)status,NULL,NULL, XEVENT_PRIORITY_NORMAL);
}

void* XESP8266Wifi_dataReceived_signal(XESP8266Wifi* device, const char* data, size_t size) {
    if (device) {
       /* XSignalSlot_emit(((XObject*)device)->m_signalSlot, XESP8266Wifi_dataReceived_signal,
            (size_t)data, size, XEVENT_PRIORITY_NORMAL);*/
    }
    return XESP8266Wifi_dataReceived_signal;
}

void* XESP8266Wifi_atResponse_signal(XESP8266Wifi* device, const char* response) {
    EmitSignal(device, XESP8266Wifi_atResponse_signal, response, NULL, NULL, XEVENT_PRIORITY_NORMAL);
}

void* XESP8266Wifi_error_signal(XESP8266Wifi* device, int errorCode, const char* errorMsg) 
{
    EmitSignal(device, XESP8266Wifi_error_signal, (void*)errorCode, NULL, NULL, XEVENT_PRIORITY_NORMAL);
}

void* XESP8266Wifi_ok_signal(XESP8266Wifi* device)
{
    EmitSignal(device, XESP8266Wifi_ok_signal,NULL, NULL, NULL, XEVENT_PRIORITY_NORMAL);
}

void* XESP8266Wifi_connect_signal(XESP8266Wifi* device)
{
    EmitSignal(device, XESP8266Wifi_connect_signal, NULL, NULL, NULL, XEVENT_PRIORITY_NORMAL);
}

void* XESP8266Wifi_disconnect_signal(XESP8266Wifi* device)
{
    EmitSignal(device, XESP8266Wifi_disconnect_signal, NULL, NULL, NULL, XEVENT_PRIORITY_NORMAL);
}
