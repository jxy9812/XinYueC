#include"XDeviceTest.h"
#include"XPrintf.h"
#include"XESP8266Wifi.h"
#include"XSerialPort.h"
#include"XCoreApplication.h"
#include"XMenu.h"
#include<string.h>
#if DEMOTEST
static void errorSlot(XObject* receiver, XVarList* list, XObject* sender)
{
    XVarList_start(list);
    int errorCode= XVarList_arg(list, int);
    const char* errorMsg = XVarList_arg(list, char*);
    XPrintf("发生了错误,错误代码:%d 错误信息:%s\n",errorCode,errorMsg);
}
static void dataReceived_signal(XObject* receiver, int connId, XObject* sender)
{
    static char data[1024];
    size_t size = XESP8266Wifi_read(sender,connId, data,XESP8266Wifi_getBytesAvailable(sender,connId),0);
    data[size] = 0;
    XPrintf("%d 接收到数据,%d字节:%s\n", connId, size, data);
}
static void XESP8266Wifi_TCP_Client_Test(XMenu* root)
{
	XSerialPort* serial =XSerialPort_create();
    if (!XSerialPort_open_base(serial, XIODeviceBase_ReadWrite, 8, 115200, XSerialPort_NoParity))
    {
        XSerialPort_delete_base(serial);
        XCoreApplication_quit();
        return;
    }
	XESP8266Wifi* wifi=XESP8266Wifi_create(serial);
    XIODeviceBase_setReadBuffer_base(wifi,1024);
    XObject_connect(wifi,XSignal(XESP8266Wifi_error_signal),NULL, errorSlot,XConnectionType_Auto);
    XObject_connect(wifi, XSignal(XESP8266Wifi_readyRead_signal), wifi, dataReceived_signal, XConnectionType_Queued);
    //XESP8266Wifi_reset(wifi,3000);
    //XEventLoop_delay(3000);
    XESP8266Wifi_exitTransparentMode(wifi,3000);
    XEventLoop_delay(1000);
    //XESP8266Wifi_setMultiConnMode(wifi,true,2000);
    if (!XESP8266Wifi_testAT(wifi,2000))
        goto delete;
    XPrintf("测试AT成功\n");
    XEventLoop_delay(500);
    if (!XESP8266Wifi_setMode(wifi, XESP8266_Mode_STA,2000))
        goto delete;
    XPrintf("设置模式成功\n");
    XEventLoop_delay(2000);
   // XESP8266Wifi_configAP(wifi,"ESP8266","12345678",6, XESP8266_Encrypt_None,3000);
    if (!XESP8266Wifi_connectWiFi(wifi, "XX", "20130520", 10000))
        goto delete;
    if (!XESP8266Wifi_waitForWiFiConnected(wifi, 10000))
        goto delete;
    XPrintf("连接Wifi成功\n");
    
    XEventLoop_delay(3000);
    int conn = XESP8266Wifi_connectServer(wifi, XESP8266_Protocol_TCP, "192.168.1.49", 6666,-1,10000);
    if (conn==-1)
        goto delete;
    if(!XESP8266Wifi_waitForServerConnected(wifi,10000))
        goto delete;
    XPrintf("连接服务器成功\n");
    //XESP8266Wifi_enterTransparentMode(wifi,3000);
    char str[] = "你好\r\n";
    XEventLoop_delay(500);
    if (!XESP8266Wifi_write(wifi, conn, str, strlen(str),1000))
        goto delete;
    //XESP8266Wifi_disconnectConn(wifi, conn,10000);
    return;
delete:
    XPrintf("ESP8266测试失败了，释放资源中\n");
    XESP8266Wifi_delete_base(wifi);
    //XSerialPort_delete_base(serial);
    XCoreApplication_processEvents(XEventLoop_AllEvents);
    XCoreApplication_quit();
}
static void XESP8266Wifi_TCP_Server_Test(XMenu* root)
{
    XSerialPort* serial = XSerialPort_create();
    if (!XSerialPort_open_base(serial, XIODeviceBase_ReadWrite, 8, 115200, XSerialPort_NoParity))
    {
        XSerialPort_delete_base(serial);
        XCoreApplication_quit();
        return;
    }
    XESP8266Wifi* wifi = XESP8266Wifi_create(serial);
    XIODeviceBase_setReadBuffer_base(wifi, 1024);
    XObject_connect(wifi, XSignal(XESP8266Wifi_error_signal), NULL, errorSlot, XConnectionType_Auto);
    XObject_connect(wifi, XSignal(XESP8266Wifi_readyRead_signal), wifi, dataReceived_signal, XConnectionType_Queued);
    XESP8266Wifi_reset(wifi,3000);
    //XEventLoop_delay(3000);
    XESP8266Wifi_exitTransparentMode(wifi, 3000);
    XEventLoop_delay(1000);
    //XESP8266Wifi_setMultiConnMode(wifi,true,2000);
    if (!XESP8266Wifi_testAT(wifi, 2000))
        goto delete;
    XPrintf("测试AT成功\n");
    XEventLoop_delay(500);
    if (!XESP8266Wifi_setMode(wifi, XESP8266_Mode_AP, 2000))
        goto delete;
    XPrintf("设置模式成功\n");
    XEventLoop_delay(2000);
     ;
    if (!XESP8266Wifi_configAP(wifi, "ESP8266", "12345678", 6, XESP8266_Encrypt_WPA_WPA2_PSK, 3000))
        goto delete;
    /*if (!XESP8266Wifi_waitForWiFiConnected(wifi, 10000))
        goto delete;*/
    XPrintf("创建Wifi成功\n");

    XEventLoop_delay(3000);
   /* int conn = XESP8266Wifi_connectServer(wifi, XESP8266_Protocol_TCP, "192.168.1.49", 6666, -1, 10000);
    if (conn == -1)
        goto delete;*/
    if (!XESP8266Wifi_startServer(wifi, XESP8266_Protocol_TCP,6666,10000))
        goto delete;
    XPrintf("创建服务器成功\n");
    //XESP8266Wifi_enterTransparentMode(wifi,3000);
    /*char str[] = "你好\r\n";
    XEventLoop_delay(500);
    if (!XESP8266Wifi_write(wifi, conn, str, strlen(str), 1000))
        goto delete;*/
    //XESP8266Wifi_disconnectConn(wifi, conn,10000);
    return;
    delete:
    XPrintf("ESP8266测试失败了，释放资源中\n");
    XESP8266Wifi_delete_base(wifi);
    //XSerialPort_delete_base(serial);
    XCoreApplication_processEvents(XEventLoop_AllEvents);
    XCoreApplication_quit();
}
void XMenu_XESP8266WifiTest(XMenu* root)
{
    XMenu* menu = XMenu_create("XESP8266Wifi(wifi)");
    XMenu_addMenu(root, menu);
    {
        XAction* action = XMenu_addAction(menu, "tcp客户端");
        XAction_setAction(action, XESP8266Wifi_TCP_Client_Test);
    }
    //XMenu_addMenu(root, menu);
    {
        XAction* action = XMenu_addAction(menu, "tcp服务器");
        XAction_setAction(action, XESP8266Wifi_TCP_Server_Test);
    }
}
#endif