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
    size_t readSize = XSerialPort_getBytesAvailable_base(sender);
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
    XSerialPortBase* serial = XSerialPort_create();
    if (!XSerialPort_open_base(serial, XIODeviceBase_ReadWrite, 6, 115200, SP_PAR_NONE))
    {
        XSerialPort_delete_base(serial);
        XCoreApplication_quit();
        return;
    }
    XObject_connect(serial,XSignal(XIODeviceBase_readyRead_signal), serial, readyRead_slot,XConnectionType_Auto);
    //XSerialPortBase_setReadBuffer_base(serial,1024);
    //线程接收数据
    //threadTest(serial);
    //主线程处理数据
   /* char buff[1024];
    while (true)
    {
        size_t readSize = XSerialPort_getBytesAvailable_base(serial);
        if (readSize == 0)
            continue;
        size_t len = XSerialPort_read_base(serial, buff, readSize);
        if (len >0)
        {
            buff[len] = 0;
            XPrintf("%s", buff);
        }
    }*/
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