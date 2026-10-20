#include"XIOTest.h"
#include"XSerialPort.h"
#include"XSerialPortInfo.h"
#include"XMemory.h"
#include"XTestMenu.h"
#include"XAction.h"
#include"XCoreApplication.h"
#include"XPrintf.h"
#include"XVector.h"
#include"XString.h"
static void XSerialPortTest();
static void XSerialPortInfoTest();

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
        XIODevice_deleteLater(serial);
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
        XIODevice_deleteLater(serial);
        XCoreApplication_processEvents(0);
    }
   
   /* XObject_connect_1(serial,XSignal(XIODevice_readyRead_signal), serial, readyRead_slot,XConnectionType_Auto);
    XCoreApplication_exec();*/
}

/**
 * @brief 枚举本机串口并打印各端口的静态信息。
 */
static void XSerialPortInfoTest()
{
    XSerialPortInfoList* list = XSerialPortInfo_availablePorts();
    size_t count;
    size_t i;
    if (!list)
    {
        XPrintf("枚举串口失败\n");
        return;
    }
    count = XVector_size_base((const XContainer*)list);
    XPrintf("可用串口 %d 个\n", (int)count);
    for (i = 0; i < count; ++i)
    {
        /* 列表元素是指针，XVector_at_base 给的是槽地址，需再解引用一层。 */
        XSerialPortInfo** slot = (XSerialPortInfo**)XVector_at_base(list, (int64_t)i);
        XSerialPortInfo* info = slot ? *slot : NULL;
        const XString* name;
        const XString* description;
        if (!info) continue;
        name = XSerialPortInfo_portName(info);
        description = XSerialPortInfo_description(info);
        XPrintf("  %s  描述=%s  VID=%04X PID=%04X %s\n",
                name ? XString_toUtf8(name) : "(null)",
                description ? XString_toUtf8(description) : "(无)",
                XSerialPortInfo_vendorId(info),
                XSerialPortInfo_productId(info),
                XSerialPortInfo_isBusy(info) ? "占用中" : "空闲");
    }
    for (i = 0; i < count; ++i)
    {
        XSerialPortInfo** slot = (XSerialPortInfo**)XVector_at_base(list, (int64_t)i);
        if (slot && *slot) XClassDelete(*slot);
    }
    XClassDelete(list);
}

void XTestMenu_XSerialPortTest(XTestMenu* root)
{
    XTestMenu* menu = XTestMenu_create("XSerialPort(串口)");
    XTestMenu_addMenu(root, menu);
    {
        XAction* action = XTestMenu_addAction(menu, "主测试");
        XTestMenu_setActionFunction(action, XSerialPortTest);
    }
    {
        XAction* action = XTestMenu_addAction(menu, "枚举串口");
        XTestMenu_setActionFunction(action, XSerialPortInfoTest);
    }
}