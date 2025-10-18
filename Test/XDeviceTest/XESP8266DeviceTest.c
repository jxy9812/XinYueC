#include"XDeviceTest.h"
#include"XPrintf.h"
#include"XESP8266Wifi.h"
#include"XSerialPort.h"
#include"XCoreApplication.h"
#include"XMenu.h"
#include<string.h>
#if DEMOTEST
void XESP8266WifiTest(XMenu* root)
{
	XSerialPort* serial =XSerialPort_create();
    if (!XSerialPort_open_base(serial, XIODeviceBase_ReadWrite, 8, 115200, SP_PAR_NONE))
    {
        XSerialPort_delete_base(serial);
        XCoreApplication_quit();
        return;
    }
	XESP8266Wifi* wifi=XESP8266Wifi_create(serial);
    if (!XESP8266Wifi_testAT(wifi))
        goto delete;
    XPrintf("测试AT成功\n");
    XEventLoop_delay(500);
    //if (!XESP8266Wifi_setMode(wifi, XESP8266_Mode_STA_AP))
    //    goto delete;
    //XPrintf("设置模式成功\n");
    XEventLoop_delay(2000);
    //XESP8266Wifi_configAP(wifi,"ESP8266","12345678",6, XESP8266_Encrypt_None);
    //XESP8266Wifi_reset(wifi);
    if (!XESP8266Wifi_connectWiFi(wifi, "XX", "20130520"))
        goto delete;
    XPrintf("连接Wifi成功\n");
    XEventLoop_delay(3000);
    if (!XESP8266Wifi_connectServer(wifi, XESP8266_Protocol_TCP, "192.168.1.51", 6666))
        goto delete;
    XPrintf("连接服务器成功\n");
    
    char str[] = "你好\r\n";
    XEventLoop_delay(500);
    XESP8266Wifi_sendData(wifi, str,strlen(str));
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
        XAction* action = XMenu_addAction(menu, "主测试");
        XAction_setAction(action, XESP8266WifiTest);
    }
}
#endif