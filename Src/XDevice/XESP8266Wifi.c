#include "XESP8266Wifi.h"
#include "XMemory.h"
#include "XString.h"
#include "XTimer.h"
#include "XCoreApplication.h"
#include "XLockFreeQueue.h"
#include "XThread.h"
#include <string.h>

void VXESP8266_processResponse(XESP8266Wifi* device);

// 前向声明
static bool VXESP8266_open(XESP8266Wifi* device, XIODeviceBaseMode mode);
static void VXESP8266_close(XESP8266Wifi* device);
static size_t VXESP8266_write(XESP8266Wifi* device, const char* data, size_t maxSize);
static size_t VXESP8266_read(XESP8266Wifi* device, char* data, size_t maxSize);
static size_t VXESP8266_getBytesAvailable(XESP8266Wifi* device);
static void VXIODevice_setWriteBuffer(XIODevice* io, size_t count);
static void VXIODevice_setReadBuffer(XIODevice* io, size_t count);
static size_t VXIODevice_getBytesAvailable(XIODevice* io);
static size_t VXIODevice_getBytesToWrite(XIODevice* io);

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
        XVTABLE_INHERIT_DEFAULT(XIODevice_class_init());
    // 重载虚函数
    XVTABLE_OVERLOAD_DEFAULT(EXIODevice_Open, VXESP8266_open);
    XVTABLE_OVERLOAD_DEFAULT(EXIODevice_Close, VXESP8266_close);
   /* XVTABLE_OVERLOAD_DEFAULT(EXIODevice_Write, VXESP8266_write);
    XVTABLE_OVERLOAD_DEFAULT(EXIODevice_Read, VXESP8266_read);
    XVTABLE_OVERLOAD_DEFAULT(EXIODevice_GetBytesAvailable, VXESP8266_getBytesAvailable);*/
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Deinit, VXESP8266_deinit);

   /* XVTABLE_OVERLOAD_DEFAULT(EXIODevice_SetReadBuffer, VXIODevice_setReadBuffer);
    XVTABLE_OVERLOAD_DEFAULT(EXIODevice_SetWriteBuffer, VXIODevice_setWriteBuffer);
    XVTABLE_OVERLOAD_DEFAULT(EXIODevice_GetBytesAvailable, VXIODevice_getBytesAvailable);
    XVTABLE_OVERLOAD_DEFAULT(EXIODevice_GetBytesToWrite, VXIODevice_getBytesToWrite);*/
#if SHOWCONTAINERSIZE
    printf("XESP8266Wifi size:%d\n", XVtable_size(XVTABLE_DEFAULT));
#endif
    return XVTABLE_DEFAULT;
}

/**
 * @brief 初始化ESP8266设备
 */
void XESP8266Wifi_init(XESP8266Wifi* device, XIODevice* io) {
    if (ISNULL(device, "device is NULL")) return;

    // 初始化父类
    memset(((XObject*)device) + 1, 0, sizeof(XESP8266Wifi) - sizeof(XObject));
    XIODevice_init(&device->m_class);

    // 初始化成员变量
    device->m_io = io;
    device->m_wifiStatus = XESP8266_Status_Disconnected;
    //device->m_serverStatus = XESP8266_Status_Disconnected;
    device->m_timeoutTimer = XTimer_create();
    XTimer_setUserData(device->m_timeoutTimer,device);
    XTimer_setTimerCallback(device->m_timeoutTimer, VXESP8266_timeoutCallback);
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
        device->m_connections[i].remaining_recv_size = 0; 
        device->m_connections[i].m_readBuffer = NULL;
        device->m_connections[i].m_writeBuffer = NULL;
        memset(device->m_connections[i].ip, 0, sizeof(device->m_connections[i].ip));
        device->m_connections[i].port = 0;
    }

    // 绑定底层设备的readyRead信号
    if (device->m_io) {
        XObject_connect1(device->m_io,
            XIODevice_readyRead_signal,
            device,
            VXESP8266_processResponse,XConnectionType_Auto);
    }
    XConnection* timeroutComm = XObject_connect1(device->m_timeoutTimer, XSignal(XTimer_timeout_signal), device->m_loop, XEventLoop_quit, XConnectionType_Auto);
    XConnection* okComm = XObject_connect1(device, XSignal(XESP8266Wifi_ok_signal), device->m_loop, XEventLoop_quit, XConnectionType_Auto);
    XConnection* errComm = XObject_connect1(device, XSignal(XESP8266Wifi_error_signal), device->m_loop, XEventLoop_quit, XConnectionType_Auto);
}
void VXESP8266_deinit(XESP8266Wifi* device)
{
    if(device->m_timeoutTimer)
    {
        XTimer_deleteLater(device->m_timeoutTimer);
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
        XEventLoop_deleteLater(device->m_loop);
        device->m_loop = NULL;
    }
    for (int i = 0; i < XESP8266_MAX_CONNS; i++) {
        device->m_connections[i].connId = -1;  // -1表示未使用
        device->m_connections[i].status = XESP8266_Status_Disconnected;
        device->m_connections[i].isServer = false;
        device->m_connections[i].remaining_recv_size = 0;
        if (device->m_connections[i].m_readBuffer)
        {
            XQueueBase_delete_base(device->m_connections[i].m_readBuffer);
            device->m_connections[i].m_readBuffer = NULL;
        }
        if (device->m_connections[i].m_writeBuffer)
        {
            XQueueBase_delete_base(device->m_connections[i].m_writeBuffer);
            device->m_connections[i].m_writeBuffer = NULL;
        }
        memset(device->m_connections[i].ip, 0, sizeof(device->m_connections[i].ip));
        device->m_connections[i].port = 0;
    }
    // 释放父对象
    XVtableGetFunc(XIODevice_class_init(), EXClass_Deinit, void(*)(XIODevice*))(device);
}

/**
 * @brief 创建ESP8266设备实例
 */
XESP8266Wifi* XESP8266Wifi_create(XIODevice* io) {
    XESP8266Wifi* device = XMemory_malloc(sizeof(XESP8266Wifi));
    if (ISNULL(device, "malloc failed")) return NULL;
    XESP8266Wifi_init(device, io);
    Set_Class_MemoryFree(device, XFree);
    return device;
}
void VXIODevice_setWriteBuffer(XIODevice* io, size_t count)
{
    XESP8266Wifi* device = io;
    //io->m_writeBuffer = count;
   /* for (size_t i = 0; i < XESP8266_MAX_CONNS; i++)
    {
        if (count != 0)
        {
            if (device->m_connections[i].m_writeBuffer == NULL)
                device->m_connections[i].m_writeBuffer = XLockFreeQueue_Create(char, count);
        }
        else if (device->m_connections[i].m_writeBuffer != NULL)
        {
            XLockFreeQueue_delete_base(device->m_connections[i].m_writeBuffer);
            device->m_connections[i].m_writeBuffer = NULL;
        }
    }*/
   
}

void VXIODevice_setReadBuffer(XIODevice* io, size_t count)
{
    XESP8266Wifi* device = io;
    //io->m_readBuffer = count;
   /* for (size_t i = 0; i < XESP8266_MAX_CONNS; i++)
    {
        if (count != 0)
        {
            if (device->m_connections[i].m_readBuffer == NULL)
                device->m_connections[i].m_readBuffer = XLockFreeQueue_Create(char, count);
        }
        else if (device->m_connections[i].m_readBuffer != NULL)
        {
            XLockFreeQueue_delete_base(device->m_connections[i].m_readBuffer);
            device->m_connections[i].m_readBuffer = NULL;
        }
    }*/
}
size_t VXIODevice_getBytesAvailable(XIODevice* io)
{
    return XESP8266Wifi_getBytesAvailable(io,0);
}
size_t VXIODevice_getBytesToWrite(XIODevice* io)
{
    return XESP8266Wifi_getBytesToWrite(io, 0);
}
/**
 * @brief 打开设备
 */
static bool VXESP8266_open(XESP8266Wifi* device, XIODeviceBaseMode mode) {
    if (ISNULL(device, "device is NULL") || ISNULL(device->m_io, "m_io is NULL"))
        return false;

    return XIODevice_open_base(device->m_io, mode);
}

/**
 * @brief 关闭设备
 */
static void VXESP8266_close(XESP8266Wifi* device) {
    if (ISNULL(device, "device is NULL") || ISNULL(device->m_io, "m_io is NULL"))
        return ;

    XIODevice_close_base(device->m_io);
}

/**
 * @brief 写入数据
 */
static size_t VXESP8266_write(XESP8266Wifi* device, const char* data, size_t maxSize) 
{
    if (ISNULL(device, "device is NULL") || ISNULL(device->m_io, "m_io is NULL") ||
        ISNULL(data, "data is NULL") || maxSize == 0)
        return 0;

    return XESP8266Wifi_write(device, 0,data, maxSize,1000);
}

/**
 * @brief 读取数据
 */
static size_t VXESP8266_read(XESP8266Wifi* device, char* data, size_t maxSize) 
{
    if (ISNULL(device, "device is NULL") || ISNULL(device->m_io, "m_io is NULL") ||
        ISNULL(data, "data is NULL") || maxSize == 0)
        return 0;

    return XESP8266Wifi_read(device,0, data, maxSize,0);
}

/**
 * @brief 获取可用字节数
 */
static size_t VXESP8266_getBytesAvailable(XESP8266Wifi* device) {
    if (ISNULL(device, "device is NULL") || ISNULL(device->m_io, "m_io is NULL"))
        return 0;

    return XIODevice_bytesAvailable_base(device->m_io);
}

/**
 * @brief 发送AT指令通用函数
 */
static bool XESP8266Wifi_sendATCommand(XESP8266Wifi* device, const char* cmd, XESP8266WifiOpType op, int msecs) 
{
    if (ISNULL(device, "device is NULL") /*|| ISNULL(cmd, "cmd is NULL")*/)
        return false;

    // 停止当前超时定时器
    XTimer_stop_base(device->m_timeoutTimer);

    // 保存当前操作类型
    device->m_currentOp = op;

    // 清空响应缓冲区
    device->m_responseLen = 0;
    memset(device->m_responseBuffer, 0, sizeof(device->m_responseBuffer));
    device->m_operationResult = false;
    if(cmd)
    {
        // 发送AT指令（添加回车换行）
        char atCmd[256];
        snprintf(atCmd, sizeof(atCmd), "%s\r\n", cmd);

        size_t sent = XIODevice_write(device->m_io, atCmd, strlen(atCmd));
        if (sent != strlen(atCmd)) {
            DEBUG_PRINTF("AT command send failed: %s", cmd);
            return false;
        }
    }

    // 启动超时定时器
    if (msecs > 0) 
    {
       /* XConnection*timeroutComm= XObject_connect1(device->m_timeoutTimer, XSignal(XTimer_timeout_signal), device->m_loop, XEventLoop_quit, XConnectionType_Auto);
        XConnection* okComm = XObject_connect1(device,XSignal(XESP8266Wifi_ok_signal), device->m_loop, XEventLoop_quit, XConnectionType_Auto);
        XConnection* errComm = XObject_connect1(device, XSignal(XESP8266Wifi_error_signal), device->m_loop, XEventLoop_quit, XConnectionType_Auto);*/
        
        XTimer_setInterval(device->m_timeoutTimer, msecs);
        XTimer_setTimeout(device->m_timeoutTimer, msecs);
        XTimer_start_base(device->m_timeoutTimer);
        XEventLoop_exec(device->m_loop);
        //XObject_disconnect2(timeroutComm);
        //XObject_disconnect2(okComm);
        //XObject_disconnect2(errComm);
    }
    else if(msecs==-1)
    {//-1无限等待
        XEventLoop_exec(device->m_loop);
    }
    if(msecs>0|| msecs == -1)
        return device->m_operationResult;
    return true;
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
bool XESP8266Wifi_testAT(XESP8266Wifi* device, int msecs) 
{
    if (ISNULL(device, "device is NULL")) return false;
    return XESP8266Wifi_sendATCommand(device, "AT", XESP8266_Op_TestAT, msecs);
}

bool XESP8266Wifi_reset(XESP8266Wifi* device, int msecs)
{
    if (ISNULL(device, "device is NULL")) return false;
    bool result = XESP8266Wifi_sendATCommand(device, "AT+RST", XESP8266_Op_Reset, msecs);
    if (result)
    {
        device->m_wifiStatus = XESP8266_Status_Disconnected;
        // 断开所有连接
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
    bool result = XESP8266Wifi_sendATCommand(device, cmd, XESP8266_Op_SetMultiConnMode, msecs);
    if (result) {
        device->m_multiConnMode = enable;
    }
    return result;
}

bool XESP8266Wifi_setMode(XESP8266Wifi* device, XESP8266WifiMode mode, int msecs) 
{
    if (ISNULL(device, "device is NULL")) return false;
    char cmd[15];
    snprintf(cmd, sizeof(cmd), "AT+CWMODE=%d", mode);
    return XESP8266Wifi_sendATCommand(device, cmd, XESP8266_Op_SetMode, msecs);
}

bool XESP8266Wifi_connectWiFi(XESP8266Wifi* device, const char* ssid, const char* password, int msecs)
{
    if (ISNULL(device, "device is NULL") || ISNULL(ssid, "ssid is NULL") || ISNULL(password, "password is NULL"))
        return false;

    device->m_wifiStatus = XESP8266_Status_Connecting;
    XESP8266Wifi_wifiStatusChanged_signal(device, XESP8266_Status_Connecting);

    XString_assign_utf8(device->m_ssid, ssid);
    XString_assign_utf8(device->m_password, password);
    char cmd[128];
    snprintf(cmd, sizeof(cmd), "AT+CWJAP=\"%s\",\"%s\"", ssid, password);
    return XESP8266Wifi_sendATCommand(device, cmd, XESP8266_Op_ConnectWiFi, msecs);
}

bool XESP8266Wifi_disconnectWiFi(XESP8266Wifi* device, int msecs) 
{
    if (ISNULL(device, "device is NULL")) return false;
    //device->m_wifiStatus = XESP8266_Status_Disconnected;
    return XESP8266Wifi_sendATCommand(device, "AT+CWQAP", XESP8266_Op_DisconnectWiFi, msecs);
}

bool XESP8266Wifi_configAP(XESP8266Wifi* device, const char* ssid, const char* password,
    int channel, XESP8266WifiEncryption encrypt, int msecs) {
    if (ISNULL(device, "device is NULL") || ISNULL(ssid, "ssid is NULL") || ISNULL(password, "password is NULL"))
        return false;

    char cmd[128];
    snprintf(cmd, sizeof(cmd), "AT+CWSAP=\"%s\",\"%s\",%d,%d", ssid, password, channel, encrypt);
    return XESP8266Wifi_sendATCommand(device, cmd, XESP8266_Op_ConfigAP, msecs);
}

int XESP8266Wifi_connectServer(XESP8266Wifi* device, XESP8266WifiProtocol protocol,
    const char* ip, uint16_t port, int connId, int msecs)
{
    if (ISNULL(device, "device is NULL") ||
        ISNULL(ip, "ip is NULL") || port == 0) {
        return -1;
    }
    int actualConnId = 0;
    if (!device->m_multiConnMode)
    {
        //device->m_serverStatus = XESP8266_Status_Connecting;
        device->m_connections[actualConnId].connId = actualConnId;
        strncpy(device->m_connections[actualConnId].ip, ip, sizeof(device->m_connections[actualConnId].ip) - 1);
        device->m_connections[actualConnId].port = port;
        device->m_connections[actualConnId].protocol = protocol;
        device->m_connections[actualConnId].status = XESP8266_Status_Connecting;
        device->m_pendingConnId = actualConnId; // 标记为等待结果的连接
        device->m_pendingStatus = XESP8266_Status_Connecting; // 初始化等待状态
        XESP8266Wifi_serverStatusChanged_signal(device, actualConnId, XESP8266_Status_Connecting);

        char cmd[128];
        const char* proto = (protocol == XESP8266_Protocol_TCP) ? "TCP" : "UDP";
        snprintf(cmd, sizeof(cmd), "AT+CIPSTART=\"%s\",\"%s\",%d", proto, ip, port);
        XESP8266Wifi_sendATCommand(device, cmd, XESP8266_Op_ConnectServer, msecs);
        if (msecs)
            return device->m_pendingStatus == XESP8266_Status_Connected ? actualConnId : -1;
        return actualConnId;
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
    device->m_pendingStatus = XESP8266_Status_Connecting; // 初始化等待状态
    
    XESP8266Wifi_serverStatusChanged_signal(device, actualConnId, XESP8266_Status_Connecting);

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
    bool sendOk = XESP8266Wifi_sendATCommand(device, cmd, XESP8266_Op_ConnectServer, msecs);
    if (!sendOk) {
        device->m_connections[actualConnId].connId = -1; // 发送失败，释放连接ID
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
    if (!device->m_multiConnMode)
    {
        connId = 0;
        snprintf(cmd, sizeof(cmd), "AT+CIPCLOSE", connId);
    }
    else
    {
        if (connId < 0 || connId >= XESP8266_MAX_CONNS ||
            device->m_connections[connId].connId == -1) {
            return false;
        }
       
        snprintf(cmd, sizeof(cmd), "AT+CIPCLOSE=%d", connId);
    }
    device->m_pendingConnId = connId;
    // 发送断开指定连接指令：AT+CIPCLOSE=<connId>
    bool result = XESP8266Wifi_sendATCommand(device, cmd, XESP8266_Op_DisconnectServer, msecs);
    if (result&& msecs)
    {
        device->m_connections[connId].connId = -1;
        device->m_connections[connId].status = XESP8266_Status_Disconnected;
        device->m_activeConnCount--;
    }
    return result;
}

bool XESP8266Wifi_startServer(XESP8266Wifi* device, XESP8266WifiProtocol protocol, uint16_t port, int msecs) 
{
    if (ISNULL(device, "device is NULL")) return false;
    if (!device->m_multiConnMode)//服务器必须开启多连接模式
    {
        XESP8266Wifi_setMultiConnMode(device, true, msecs);
        XEventLoop_delay(100);
    }
    if(protocol== XESP8266_Protocol_TCP)
    {
        char cmd[64];
        snprintf(cmd, sizeof(cmd), "AT+CIPSERVER=1,%d", port);
        return XESP8266Wifi_sendATCommand(device, cmd, XESP8266_Op_StartServer, msecs);
    }
    else
    {
        return XESP8266Wifi_connectServer(device, protocol,"0.0.0.0", port,-1,msecs);
    }
}

bool XESP8266Wifi_stopServer(XESP8266Wifi* device, int msecs)
{
    return XESP8266Wifi_sendATCommand(device, "AT+CIPSERVER=0", XESP8266_Op_StopServer, msecs);
}

size_t XESP8266Wifi_write(XESP8266Wifi* device, int connId, const void* data, size_t size, int msecs)
{
    if (ISNULL(device, "device is NULL") || ISNULL(data, "data is NULL") || size == 0) 
    {
        return 0;
    }
    if (!device->m_multiConnMode)
    {//非多连接模式
        if (device->m_transparentMode) {
            // 透传模式直接发送
            return XIODevice_write(device->m_io, data, size);
        }
        else {
            // 非透传模式使用AT指令
            char cmd[32];
            snprintf(cmd, sizeof(cmd), "AT+CIPSEND=%d", (int)size);
            if (!XESP8266Wifi_sendATCommand(device, cmd, XESP8266_Op_WriteData, msecs)) {
                return 0;
            }
            // 等待"> "提示符（简单处理，实际可能需要更复杂的等待逻辑）
            //XEventLoop_delay(100);
            if (XIODevice_write(device->m_io, data, size) == size)
            {
                if (XESP8266Wifi_sendATCommand(device, NULL, XESP8266_Op_WriteData, msecs)) 
                {
                    return size;
                }
            }
            
           // XEventLoop_delay(100);
            return 0;
        }
    }
    if (connId < 0 || connId >= XESP8266_MAX_CONNS ||
        device->m_connections[connId].status != XESP8266_Status_Connected)
        return 0;
    if (device->m_transparentMode) {
        // 多连接透传需先指定连接：AT+CIPSEND=<connId>
        char cmd[32];
        snprintf(cmd, sizeof(cmd), "AT+CIPSEND=%d", connId);
        if (!XESP8266Wifi_sendATCommand(device, cmd, XESP8266_Op_WriteData, msecs)) {
            return 0;
        }
        // 透传模式发送数据
        return XIODevice_write(device->m_io, data, size) == size;
    }
    else {
        // 非透传模式：AT+CIPSEND=<connId>,<size>
        char cmd[32];
        snprintf(cmd, sizeof(cmd), "AT+CIPSEND=%d,%d", connId, (int)size);
        if (!XESP8266Wifi_sendATCommand(device, cmd, XESP8266_Op_WriteData, msecs)) {
            return 0;
        }
        // 发送实际数据
        if (XIODevice_write(device->m_io, data, size) == size)
        {
            if (XESP8266Wifi_sendATCommand(device, NULL, XESP8266_Op_WriteData, msecs))
            {
                return size;
            }
        }
        return 0;
        //return XIODevice_write(device->m_io, data, size);
    }
}

size_t XESP8266Wifi_read(XESP8266Wifi* device, int connId, void* data, size_t size, int msecs)
{
    if (ISNULL(device, "device is NULL") || ISNULL(data, "data is NULL") || size == 0)
    {
        return 0;
    }
    if (!device->m_multiConnMode)
    {
        connId = 0;//修正id 单连接默认0
    }
    else if (connId < 0 || connId >= XESP8266_MAX_CONNS /*||device->m_connections[connId].status != XESP8266_Status_Connected*/)
    {
        return 0;//超出范围
    }
    XCircularQueue* queue= device->m_connections[connId].m_readBuffer;//获取缓冲区队列
    if (!queue)
        return 0;//缓冲区不存在无法获取数据
    size_t remaining_size = size;//剩余需要的字节大小
    size_t queueSize = XQueueBase_size_base(queue);//当前接收缓冲区的数据大小
    size_t recvSize = remaining_size > queueSize ? queueSize : remaining_size;//当前所要接收的数据大小
    for (size_t i = 0; i < recvSize; i++)
    {
        XQueueBase_receive_base(queue, ((char*)data)+(size-remaining_size)+i);
    }
    remaining_size -= recvSize;//计算剩余大小
    if (remaining_size == 0)
        return size;//所需大小已全部接收
    if (msecs <= 0)
        return size - remaining_size;//立即结束，返回当前获取的数据大小
    //延迟等待
    size_t current= XTimer_getCurrentTime();
    while (XTimer_getCurrentTime()< current+ msecs)
    {
        XCoreApplication_processEvents(XEventLoop_AllEvents);
        if (remaining_size && XQueueBase_receive_base(queue, ((char*)data) + (size - remaining_size)))
        {
            --remaining_size;
            if (remaining_size == 0)
                return size;//接收完毕
        }
    }
    //延迟结束返回
    return size - remaining_size;
}

size_t XESP8266Wifi_getBytesAvailable(XESP8266Wifi* device, int connId)
{
    if (ISNULL(device, "device is NULL"))
    {
        return 0;
    }
    if (!device->m_multiConnMode)
    {
        connId = 0;//修正id 单连接默认0
    }
    else if (connId < 0 || connId >= XESP8266_MAX_CONNS /*||device->m_connections[connId].status != XESP8266_Status_Connected*/)
    {
        return 0;//超出范围
    }
    XCircularQueue* queue = device->m_connections[connId].m_readBuffer;//获取缓冲区队列
    if (!queue)
        return 0;//缓冲区不存在无法获取数据
    return XCircularQueue_size_base(queue);
}

size_t XESP8266Wifi_getBytesToWrite(XESP8266Wifi* device, int connId)
{
    if (ISNULL(device, "device is NULL"))
    {
        return 0;
    }
    if (!device->m_multiConnMode)
    {
        connId = 0;//修正id 单连接默认0
    }
    else if (connId < 0 || connId >= XESP8266_MAX_CONNS /*||device->m_connections[connId].status != XESP8266_Status_Connected*/)
    {
        return 0;//超出范围
    }
    XCircularQueue* queue = device->m_connections[connId].m_writeBuffer;//获取缓冲区队列
    if (!queue)
        return 0;//缓冲区不存在无法获取数据
    return XCircularQueue_size_base(queue);
}

bool XESP8266Wifi_enterTransparentMode(XESP8266Wifi* device, int msecs)
{
    if (ISNULL(device, "device is NULL")) return false;

    // 先设置透传模式
    if (!XESP8266Wifi_sendATCommand(device, "AT+CIPMODE=1", XESP8266_Op_SetTransparent, msecs)) {
        return false;
    }
    //XEventLoop_delay(100);

    // 进入透传
    if (!XESP8266Wifi_sendATCommand(device, "AT+CIPSEND", XESP8266_Op_EnterTransparent, msecs)) {
        return false;
    }

    device->m_transparentMode = true;
    return true;
}

bool XESP8266Wifi_exitTransparentMode(XESP8266Wifi* device, int msecs) 
{
    if (ISNULL(device, "device is NULL") || !device->m_transparentMode)
        return false;

    // 发送+++退出透传（无回车）
    size_t sent = XIODevice_write(device->m_io, "+++", 3);
    if (sent != 3) return false;
    XEventLoop_delay(100);
    if (!XESP8266Wifi_sendATCommand(device, "AT+CIPMODE=0", XESP8266_Op_SetTransparent, msecs)) {
        return false;
    }
    // 等待退出完成
    //XEventLoop_delay(msecs);
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

    if (device->m_wifiStatus == XESP8266_Status_Connected)
        return true;
    if (device->m_wifiStatus != XESP8266_Status_Connecting)
        return false;
    if (msecs > 0)
    {
        XTimer_setInterval(device->m_timeoutTimer, msecs);
        XTimer_setTimeout(device->m_timeoutTimer, msecs);
        XTimer_start_base(device->m_timeoutTimer);
        XEventLoop_exec(device->m_loop);
    }
    return  device->m_wifiStatus == XESP8266_Status_Connected;
}

bool XESP8266Wifi_waitForServerConnected(XESP8266Wifi* device, int msecs)
{
    if (ISNULL(device, "device is NULL")) return false;
    if (device->m_pendingStatus== XESP8266_Status_Connected)
        return true;
    if (device->m_pendingStatus != XESP8266_Status_Connecting)
        return false;
    if(msecs>0)
    {
        XTimer_setInterval(device->m_timeoutTimer, msecs);
        XTimer_setTimeout(device->m_timeoutTimer, msecs);
        XTimer_start_base(device->m_timeoutTimer);
        XEventLoop_exec(device->m_loop);
    }
    return  device->m_pendingStatus == XESP8266_Status_Connected;
}

bool XESP8266Wifi_waitForDisconnectConn(XESP8266Wifi* device, int connId, int msecs)
{
    if (ISNULL(device, "device is NULL"))
        return false;
    if (!device->m_multiConnMode)
    {
        connId = 0;
    }
    // 检查连接是否已断开
    if (device->m_connections[connId].status == XESP8266_Status_Disconnected)
        return true;
    device->m_pendingConnId = connId;
    // 设置超时
    if (msecs > 0)
    {
        XTimer_setInterval(device->m_timeoutTimer, msecs);
        XTimer_setTimeout(device->m_timeoutTimer, msecs);
        XTimer_start_base(device->m_timeoutTimer);
        XEventLoop_exec(device->m_loop);
    }
    return device->m_connections[connId].status == XESP8266_Status_Disconnected;
}

// 信号实现
void* XESP8266Wifi_wifiStatusChanged_signal(XESP8266Wifi* device, XESP8266WifiStatus status) 
{
    XEmitSignal(device, XESP8266Wifi_wifiStatusChanged_signal, (void*)status,NULL,NULL, XEVENT_PRIORITY_NORMAL);
}

void* XESP8266Wifi_serverStatusChanged_signal(XESP8266Wifi* device, int connId, XESP8266WifiStatus status)
{
    XVarList* list = XVarList_Create(XVar(int,connId), XVar(XESP8266WifiStatus,status));
    XEmitSignal(device, XESP8266Wifi_serverStatusChanged_signal, list, XVarList_delete,NULL, XEVENT_PRIORITY_NORMAL);
}

void* XESP8266Wifi_readyRead_signal(XESP8266Wifi* device, int connId)
{
    //XVarList* list = XVarList_Create(XVar(int, connId), XVar(char*, data), XVar(size_t, size));
    XEmitSignal(device, XESP8266Wifi_readyRead_signal, connId, NULL, NULL, XEVENT_PRIORITY_NORMAL);
}

void* XESP8266Wifi_atResponse_signal(XESP8266Wifi* device, const char* response)
{
    XEmitSignal(device, XESP8266Wifi_atResponse_signal, response, NULL, NULL, XEVENT_PRIORITY_NORMAL);
}
static void error_signal_data_delete(XVarList* list)
{
    if (!list) return;
    XVarList_start(list);
    XVarList_argOffset(list, int);
    char* msg=XVarList_arg(list,char*);
    if(msg)
        XMemory_free(msg);
    XVarList_delete(list);
}
void* XESP8266Wifi_error_signal(XESP8266Wifi* device, int errorCode, const char* errorMsg) 
{
    if (device)
    {
        size_t len = strlen(errorMsg);
        char* msg = len ? XMemory_malloc(len + 1) : NULL;
        if (msg)
            strcpy(msg, errorMsg);
        XVarList* list = XVarList_Create(XVar(int, errorCode), XVar(char*, msg));
        XObject_emitSignal(device, XESP8266Wifi_error_signal, list, error_signal_data_delete, ((void*)0), XEVENT_PRIORITY_NORMAL);
    }
    return XESP8266Wifi_error_signal;
}

void* XESP8266Wifi_ok_signal(XESP8266Wifi* device)
{
    XEmitSignal(device, XESP8266Wifi_ok_signal, NULL, NULL, NULL, XEVENT_PRIORITY_NORMAL);
}

void* XESP8266Wifi_connect_signal(XESP8266Wifi* device, int connId)
{
    XEmitSignal(device, XESP8266Wifi_connect_signal, connId, NULL, NULL, XEVENT_PRIORITY_NORMAL);
}

void* XESP8266Wifi_disconnect_signal(XESP8266Wifi* device, int connId)
{
    XEmitSignal(device, XESP8266Wifi_disconnect_signal, connId, NULL, NULL, XEVENT_PRIORITY_NORMAL);
}
