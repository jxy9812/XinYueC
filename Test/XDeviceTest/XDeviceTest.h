#ifndef XDEVICETEST_H
#define XDEVICETEST_H
#ifdef __cplusplus
extern "C" {
#endif
#include"CXinYueConfig.h"
#include"XClass.h"
#if DEMOTEST
bool XDeviceSerialPortTest_runAll(void);
void XMenu_XDeviceTest(XMenu* root);
void XMenu_XESP8266WifiTest(XMenu* root);
int XESP8266WifiTest_runAutomated(const char* portName, const char* ssid, const char* password);
int XESP8266WifiTest_runUnit(void);
#endif // DEMOTEST

#ifdef __cplusplus
}
#endif	
#endif
