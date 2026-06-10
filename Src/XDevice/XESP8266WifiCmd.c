#include"XESP8266Wifi.h"
#include "XTimer.h"
#include "XEventLoop.h"
#include "XLockFreeQueue.h"
#include <string.h>
#define AT_RESPONSE_OK          (strstr(device->m_responseBuffer, "OK"))
#define AT_RESPONSE_ERROR       (strstr(device->m_responseBuffer, "ERROR"))
#define AT_RESPONSE_CONNECT     (strstr(device->m_responseBuffer, "CONNECT"))
#define AT_RESPONSE_DISCONNECT  (strstr(device->m_responseBuffer, "DISCONNECT"))
static bool g_ok(XESP8266Wifi* device);
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
//有新的客户端连接处理
static bool ConnectClient(XESP8266Wifi* device);
//有客户端断开连接
static bool ClosedClient(XESP8266Wifi* device);
//接受到新的数据
static bool recvData(XESP8266Wifi* device,char*data);
//设置缓冲区
static void setBuffer(XESP8266Wifi* device, int connId);
bool g_ok(XESP8266Wifi* device)
{
    char* hasOk = AT_RESPONSE_OK;
    if (hasOk)
    {
        device->m_operationResult = true;
        XTimer_stop_base(device->m_timeoutTimer);
        XESP8266Wifi_ok_signal(device);
        XEventLoop_quit(device->m_loop);
        return true;
    }
    return false;
}
bool g_error(XESP8266Wifi* device)
{
    char* hasError = AT_RESPONSE_ERROR;
    if (hasError)
    {
        *hasError = 0;
        XESP8266Wifi_error_signal(device, -1, device->m_responseBuffer);
        XTimer_stop_base(device->m_timeoutTimer);
        XEventLoop_quit(device->m_loop);
        return true;
    }
    return false;
}
bool Reset(XESP8266Wifi* device)
{
    if (g_ok(device))
        return true;
    if (g_error(device))
        return true;
    return false;
}
bool TestAT(XESP8266Wifi* device)
{
    if (g_ok(device))
        return true;
    if (g_error(device))
        return true;
    return false;
}
bool SetMultiConnMode(XESP8266Wifi* device)
{
    if (g_ok(device))
        return true;
    if (g_error(device))
        return true;
    return false;
}
bool SetMode(XESP8266Wifi* device)
{
    if (g_ok(device))
        return true;
    if (g_error(device))
        return true;
    return false;
}
//写入数据指令
bool WriteData(XESP8266Wifi* device)
{
    if (g_ok(device))
        return true;
    if (g_error(device))
    {
 /*       char* hasError = (strstr(device->m_responseBuffer, "ERROR"));
        if (hasError)*/
        return true;
    }
    return false;
}
bool ConfigAP(XESP8266Wifi* device)
{
    if (g_ok(device))
        return true;
    if (g_error(device))
        return true;
    return false;
}
bool StartServer(XESP8266Wifi* device)
{
    if (g_ok(device))
        return true;
    if (g_error(device))
        return true;
    return false;
}
bool StopServer(XESP8266Wifi* device)
{
    if (g_ok(device))
        return true;
    if (g_error(device))
        return true;
    return false;
}
bool SetTransparent(XESP8266Wifi* device)
{
    if (g_ok(device))
        return true;
    if (g_error(device))
        return true;
    return false;
}
bool EnterTransparent(XESP8266Wifi* device)
{
    if (g_ok(device))
        return true;
    if (g_error(device))
        return true;
    return false;
}
bool ConnectWiFi(XESP8266Wifi* device)
{
    char* hasDisconnect = AT_RESPONSE_DISCONNECT;
    if (!hasDisconnect && (AT_RESPONSE_OK || AT_RESPONSE_CONNECT))
    {
        device->m_wifiStatus = XESP8266_Status_Connected;
        XESP8266Wifi_wifiStatusChanged_signal(device, XESP8266_Status_Connected);

        device->m_operationResult = true;
        XTimer_stop_base(device->m_timeoutTimer);
        XESP8266Wifi_ok_signal(device);
        XEventLoop_quit(device->m_loop);

        return true;
    }
   
    char* hasError = hasDisconnect?false: AT_RESPONSE_ERROR;
    char* hasClosed = hasError ? false : (strstr(device->m_responseBuffer, "CLOSED"));
    if (hasDisconnect || hasError|| hasClosed)
    {//重新尝试连接
        if (hasError)
        {
            device->m_wifiStatus = XESP8266_Status_Error;
            device->m_operationResult = false;
            XESP8266Wifi_wifiStatusChanged_signal(device, XESP8266_Status_Error);
            *hasError = 0;
            XESP8266Wifi_error_signal(device, -1, device->m_responseBuffer);
        }
        else
        {
            if (hasClosed)
            {
                *hasClosed = 0;
            }
            else
            {
                char* hasWifi = strstr(device->m_responseBuffer, "WIFI");
                if(hasWifi)*hasWifi = 0;
            }
        }
       
        XIODevice_write_1(device->m_io, device->m_responseBuffer, strlen(device->m_responseBuffer));
        device->m_responseLen = 0;
        return false;
    }
    return false;
}
bool DisconnectWiFi(XESP8266Wifi* device)
{
    if (AT_RESPONSE_OK || AT_RESPONSE_DISCONNECT)
    {
        device->m_wifiStatus = XESP8266_Status_Disconnected;
        XESP8266Wifi_wifiStatusChanged_signal(device, XESP8266_Status_Disconnected);

        device->m_operationResult = true;
        XTimer_stop_base(device->m_timeoutTimer);
        XESP8266Wifi_ok_signal(device);
        XEventLoop_quit(device->m_loop);
        return true;
    }
    return false;
}
bool ConnectServer(XESP8266Wifi* device)
{
    int connId = device->m_pendingConnId;

    if (/*AT_RESPONSE_OK ||*/ AT_RESPONSE_CONNECT)
    {
        device->m_connections[connId].status = XESP8266_Status_Connected;
        device->m_pendingStatus = XESP8266_Status_Connected;
        device->m_activeConnCount++;
        setBuffer(device,connId);//设置缓冲区
        device->m_operationResult = true;
        XESP8266Wifi_serverStatusChanged_signal(device, connId, XESP8266_Status_Connected);

        XESP8266Wifi_ok_signal(device);
        XESP8266Wifi_connect_signal(device, connId);
        XTimer_stop_base(device->m_timeoutTimer);
        return true;
    }
    else if (AT_RESPONSE_ERROR) 
    {
        //device->m_operationResult = false;
        XESP8266Wifi_serverStatusChanged_signal(device, connId, XESP8266_Status_Error);
        *strstr(device->m_responseBuffer, "ERROR") = 0;
        XIODevice_write_1(device->m_io, device->m_responseBuffer, strlen(device->m_responseBuffer));
        device->m_responseLen = 0;
        return false;
    }
    return false;
}
bool DisconnectServer(XESP8266Wifi* device)
{
    if (AT_RESPONSE_OK)
    {
        device->m_operationResult = true;
        // 更新指定连接状态
        if (device->m_pendingConnId != -1)
        {
            device->m_connections[device->m_pendingConnId].status = XESP8266_Status_Disconnected;
            device->m_pendingConnId = -1;
            device->m_activeConnCount--;
        }
        else {
            // 断开所有连接
            for (int i = 0; i < XESP8266_MAX_CONNS; i++) {
                device->m_connections[i].status = XESP8266_Status_Disconnected;
            }
            device->m_activeConnCount = 0;
        }
        XESP8266Wifi_ok_signal(device);
        XTimer_stop_base(device->m_timeoutTimer);
        return true;
    }
    if (g_error(device))
        return true;
    return false;
}
bool ConnectClient(XESP8266Wifi* device)
{
    char* connect = strstr(device->m_responseBuffer, ",CONNECT");
    // 解析服务器模式下的客户端连接（如："0,CONNECT" 表示ID=0的客户端连接）
    if (connect != NULL)
    {
        int connId = -1;
        if (sscanf((connect - 1 >= device->m_responseBuffer)? connect - 1: device->m_responseBuffer, "%d,CONNECT", &connId) == 1) {
            if (connId >= 0 && connId < XESP8266_MAX_CONNS) {
                device->m_connections[connId].connId = connId;
                device->m_connections[connId].status = XESP8266_Status_Connected;
                device->m_connections[connId].isServer = true;  // 服务器端的客户端连接
                device->m_activeConnCount++;
                setBuffer(device,connId);
                // 触发客户端连接信号（可自定义信号传递connId）
                XESP8266Wifi_connect_signal(device, connId);
                return true;
            }
        }
    }
    return false;
}
bool ClosedClient(XESP8266Wifi* device)
{
    if (strstr(device->m_responseBuffer, "CLOSED") != NULL)
    {
        int connId = -1;
        if (sscanf(device->m_responseBuffer, "%d,CLOSED", &connId) == 1) {
            if (connId >= 0 && connId < XESP8266_MAX_CONNS) 
            {
                XESP8266ConnInfo* info = device->m_connections + connId;
                info->connId = connId;
                info->status = XESP8266_Status_Disconnected;
                info->isServer = true;  // 服务器端的客户端连接
                if (info->m_readBuffer)
                {
                    XQueueBase_delete_base(info->m_readBuffer);
                    info->m_readBuffer = NULL;
                }
                if (info->m_writeBuffer)
                {
                    XQueueBase_delete_base(info->m_writeBuffer);
                    info->m_writeBuffer = NULL;
                }
                device->m_activeConnCount--;
                // 触发客户端连接信号（可自定义信号传递connId）
                XESP8266Wifi_disconnect_signal(device, connId);
                return true;
            }
        }
    }
    return false;
}
bool recvData(XESP8266Wifi* device, char* buffer)
{
    // 解析数据接收（如："+IPD,<connId>,<len>:data"）
    if (device->m_currentOp == XESP8266_Op_RecvData)
    {
        int size = device->m_connections[device->m_pendingConnId].remaining_recv_size > device->m_responseLen ? device->m_responseLen : device->m_connections[device->m_pendingConnId].remaining_recv_size;
        //存在接收缓冲区，压入缓冲区队列
        if (device->m_connections[device->m_pendingConnId].m_readBuffer)
        {
            for (size_t i = 0; i < size; i++)
            {
                XQueueBase_push_base(device->m_connections[device->m_pendingConnId].m_readBuffer, buffer + i);
            }
        }
        XESP8266Wifi_readyRead_signal(device, device->m_pendingConnId);
        if (device->m_connections[device->m_pendingConnId].remaining_recv_size > device->m_responseLen)
        {
            device->m_connections[device->m_pendingConnId].remaining_recv_size -= device->m_responseLen;
            device->m_responseLen = 0;
            device->m_currentOp = XESP8266_Op_RecvData;
            return false;
        }
        else
        {
            device->m_connections[device->m_pendingConnId].remaining_recv_size = 0;
            return true;
        }
    }
    else
    {
        char* ipd = strstr(buffer, "+IPD,");
        if (ipd == NULL)
            return false;
        int connId=0, len=0;
        char* data = NULL;
        if (device->m_multiConnMode)
        {
            if (sscanf(ipd, "+IPD,%d,%d:", &connId, &len) == 2) 
            {
                data = strstr(ipd, ":") + 1;  // 定位到数据部分
            }
        }
        else
        {
           /* int len;*///+IPD,5:esp32
            if (sscanf(ipd, "+IPD,%d:", &len) == 1)
            {
               data = strstr(ipd, ":") + 1;  // 定位到数据部分
            }
        }
        device->m_pendingConnId = connId;
        device->m_connections[connId].remaining_recv_size = (len - (device->m_responseLen - (buffer - device->m_responseBuffer) - (data - buffer)));
        int size = len - device->m_connections[connId].remaining_recv_size;
        //存在接收缓冲区，压入缓冲区队列
        if (device->m_connections[connId].m_readBuffer)
        {
            for (size_t i = 0; i < size; i++)
            {
                XQueueBase_push_base(device->m_connections[connId].m_readBuffer, data + i);
            }
        }
        // 触发带连接ID的数据接收信号
        XESP8266Wifi_readyRead_signal(device, connId);
        if (device->m_connections[connId].remaining_recv_size)
        {
            device->m_responseLen = 0;
            device->m_currentOp = XESP8266_Op_RecvData;
            return false;
        }
    }
    
    return true;
}
void setBuffer(XESP8266Wifi* device, int connId)
{
    ////读取缓冲区
    //{
    //    size_t count = ((XIODevice*)device)->m_readBuffer;
    //    //创建对应的缓冲区
    //    if (count == 0 && device->m_connections[connId].m_readBuffer)
    //    {//设置无需缓冲区
    //        XQueueBase_delete_base(device->m_connections[connId].m_readBuffer);
    //        device->m_connections[connId].m_readBuffer = NULL;
    //    }
    //    else if (count > 0)
    //    {
    //        //存在缓冲区，但是跟设置的不一样
    //        if (device->m_connections[connId].m_readBuffer && count != XLockFreeQueue_size_base(device->m_connections[connId].m_readBuffer))
    //        {
    //            XQueueBase_delete_base(device->m_connections[connId].m_readBuffer);
    //            device->m_connections[connId].m_readBuffer = NULL;
    //        }
    //        if (device->m_connections[connId].m_readBuffer == NULL)
    //            device->m_connections[connId].m_readBuffer = XLockFreeQueue_Create(char, count);
    //    }
    //}
    ////写入缓冲区
    //{
    //    size_t count = ((XIODevice*)device)->m_writeBuffer;
    //    //创建对应的缓冲区
    //    if (count == 0 && device->m_connections[connId].m_writeBuffer)
    //    {//设置无需缓冲区
    //        XQueueBase_delete_base(device->m_connections[connId].m_writeBuffer);
    //        device->m_connections[connId].m_writeBuffer = NULL;
    //    }
    //    else if (count > 0)
    //    {
    //        //存在缓冲区，但是跟设置的不一样
    //        if (device->m_connections[connId].m_writeBuffer && count != XLockFreeQueue_size_base(device->m_connections[connId].m_writeBuffer))
    //        {
    //            XQueueBase_delete_base(device->m_connections[connId].m_writeBuffer);
    //            device->m_connections[connId].m_writeBuffer = NULL;
    //        }
    //        if (device->m_connections[connId].m_writeBuffer == NULL)
    //            device->m_connections[connId].m_writeBuffer = XLockFreeQueue_Create(char, count);
    //    }
    //}
}
/**
 * @brief 处理AT指令响应
 */
void VXESP8266_processResponse(XESP8266Wifi* device)
{
    if (ISNULL(device, "device is NULL")) return;

    // 读取底层设备数据
    size_t available = XIODevice_bytesAvailable_base(device->m_io);
    if (available == 0) return;

    // 读取数据到响应缓冲区
    if (device->m_responseLen + available < sizeof(device->m_responseBuffer) - 1) 
    {
        available = XIODevice_read_1(device->m_io,
            device->m_responseBuffer + device->m_responseLen,
            available);
        device->m_responseLen += available;
        device->m_responseBuffer[device->m_responseLen] = '\0';
        XPrintf("\n||<<%s>>||\n", device->m_responseBuffer);

    }
    bool hand = false;
    // 透传模式下直接转发数据
    if (device->m_transparentMode)
    {
        int connId = 0;
        if (device->m_connections[connId].m_readBuffer)
        {
            for (size_t i = 0; i < device->m_responseLen; i++)
            {
                XQueueBase_push_base(device->m_connections[connId].m_readBuffer, device->m_responseBuffer + i);
            }
        }
        XESP8266Wifi_readyRead_signal(device, connId/*,device->m_responseBuffer, device->m_responseLen*/);
        device->m_responseLen = 0;
        return;
    }
    else if (device->m_currentOp == XESP8266_Op_RecvData)
    {
        hand = recvData(device, device->m_responseBuffer);
    }
    else
    {
        char* IPD = strstr(device->m_responseBuffer, "+IPD,");
        if (IPD)
        {//接受数据响应
            hand= recvData(device, IPD);
        }
    }
    if(!hand)
    {
        // 触发响应信号
        XESP8266Wifi_atResponse_signal(device, (void*)device->m_responseBuffer);


        // 根据当前操作类型处理响应
        switch (device->m_currentOp)
        {
        case XESP8266_Op_Reset:hand = Reset(device); break;
        case XESP8266_Op_TestAT:hand = TestAT(device); break;
        case XESP8266_Op_SetMultiConnMode:hand = SetMultiConnMode(device); break;
        case XESP8266_Op_SetMode:hand = SetMode(device); break;
        case XESP8266_Op_WriteData:hand = WriteData(device); break;
        case XESP8266_Op_ConfigAP:hand = ConfigAP(device); break;
        case XESP8266_Op_StartServer:hand = StartServer(device); break;
        case XESP8266_Op_StopServer:hand = StopServer(device); break;
        case XESP8266_Op_SetTransparent:hand = SetTransparent(device); break;
        case XESP8266_Op_EnterTransparent:hand = EnterTransparent(device); break;
        case XESP8266_Op_ConnectWiFi:hand = ConnectWiFi(device); break;
        case XESP8266_Op_DisconnectWiFi:hand = DisconnectWiFi(device); break;
        case XESP8266_Op_ConnectServer:hand = ConnectServer(device); break;
        case XESP8266_Op_DisconnectServer:hand = DisconnectServer(device); break;

        default:
            break;
        }
        // 非透传模式下处理多连接状态
        if (!hand && !device->m_transparentMode)
        {
            hand = ConnectClient(device);
            if(!hand)
                hand = ClosedClient(device);
        }
    }
    // 处理完成后清空缓冲区（非透传模式）
    if (hand)
    {
        //memset(device->m_responseBuffer, 0, device->m_responseLen < sizeof(device->m_responseBuffer) ? device->m_responseLen : sizeof(device->m_responseBuffer));
        device->m_responseLen = 0;
        device->m_currentOp = XESP8266_Op_None;
    }
}