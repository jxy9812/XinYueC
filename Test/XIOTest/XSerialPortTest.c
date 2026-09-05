#include"XIOTest.h"
#include"XSerialPort.h"
#include"XMemory.h"
#include"XTestMenu.h"
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
    int len = XSerialPort_read_base(sender, buff, readSize);
    if (len > 0)
    {
        buff[len] = 0;
        XPrintf("%s", buff);
    }
}
void XSerialPortTest()
{
    while (true)
    {
    XSerialPort* serial = XSerialPort_create();
    XSerialPort_setBaudRate(serial,115200,XSerialPort_AllDirections);
    XSerialPort_setPortName(serial,"COM20");
    if (!XSerialPort_open_base(serial, XIODevice_ReadWrite))
    {
        XSerialPort_delete_base(serial);
        //XCoreApplication_quit();
        //return;
        continue;
    }

        static char buff[1024];
        XIODevice_waitForReadyRead_base(serial,INT32_MAX);
        size_t readSize = XSerialPort_bytesAvailable_base(serial);
        int len = XSerialPort_read_base(serial, buff, readSize);
        if (len > 0)
        {
            buff[len] = 0;
            XPrintf("%s", buff);
        }
        XSerialPort_delete_base(serial);
        XCoreApplication_processEvents(0);
    }
   
   /* XObject_connect_1(serial,XSignal(XIODevice_readyRead_signal), serial, readyRead_slot,XConnectionType_Auto);
    XCoreApplication_exec();*/
}

void XTestMenu_XSerialPortTest(XTestMenu* root)
{
    XTestMenu* menu = XTestMenu_create("XSerialPort(串口)");
    XTestMenu_addMenu(root, menu);
    {
        XAction* action = XTestMenu_addAction(menu, "主测试");
        XTestMenu_setActionFunction(action, XSerialPortTest);
    }
}