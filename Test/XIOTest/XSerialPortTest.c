#include"XIOTest.h"
#include"XSerialPort.h"
#include"XMemory.h"
#include"XMenu.h"
#include"XAction.h"
#include"XCoreApplication.h"
#include"XPrintf.h"
static void XSerialPortTest();

static void readyRead_slot(XObject* sender, XObject* receiver, void* args)
{
    size_t readSize = XSerialPort_bytesAvailable_base(sender);
    if (readSize == 0)
        return;
    static char buff[1024];
    size_t len = XSerialPort_read_base(sender, buff, readSize);
    if (len > 0)
    {
        buff[len] = 0;
        XPrintf("%s", buff);
    }
}
void XSerialPortTest()
{
    XSerialPort* serial = XSerialPort_create();
    XSerialPort_setBaudRate(serial,115200,XSerialPort_AllDirections);
    XSerialPort_setPortName(serial,"COM20");
    if (!XSerialPort_open_base(serial, XIODevice_ReadWrite))
    {
        XSerialPort_delete_base(serial);
        XCoreApplication_quit();
        return;
    }
    XObject_connect1(serial,XSignal(XIODevice_readyRead_signal), serial, readyRead_slot,XConnectionType_Auto);
    XCoreApplication_exec();
}

void XMenu_XSerialPortTest(XMenu* root)
{
    XMenu* menu = XMenu_create("XSerialPort(串口)");
    XMenu_addMenu(root, menu);
    {
        XAction* action = XMenu_addAction(menu, "主测试");
        XAction_setAction(action, XSerialPortTest);
    }
}