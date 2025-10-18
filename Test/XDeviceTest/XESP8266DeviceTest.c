#include"XDeviceTest.h"
#include"XPrintf.h"
#include"XESP8266Wifi.h"
#include"XSerialPort.h"
#include"XCoreApplication.h"
#include"XMenu.h"
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
    //XEventLoop_delay(500);
    XESP8266Wifi_setMode(wifi, XESP8266_Mode_STA_AP);
    //XEventLoop_delay(500);
    XESP8266Wifi_configAP(wifi,"ESP8266","12345678",6, XESP8266_Encrypt_None);
    XESP8266Wifi_reset(wifi);
    return;
delete:
    XPrintf("ESP8266测试失败了，释放资源中\n");
    XESP8266Wifi_delete_base(wifi);
    XSerialPort_delete_base(serial);
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