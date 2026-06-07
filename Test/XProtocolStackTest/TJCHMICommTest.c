#include"XProtocolStackTest.h"
#include"XTJCHMIComm.h"
#include"XSerialPort.h"
#include"XVector.h"
#include"XTimer.h"
#include"XString.h"
#include"XStringList.h"
#include"XTimerGroupBase.h"
#include"XMenu.h"
#include"XAction.h"
#include"XCoreApplication.h"
#include"XPrintf.h"
#include <stdio.h>
#include <stdlib.h>
static void XFuncCodeCb0x1A(uint8_t code, void* obj, void* data, void* userData)
{
	XVector* v = data;
	XPrintf("0x1a功能码调用\n");
}
static void XFuncCodeCb0x30(uint8_t code, void* obj, void* data, void* userData)
{
	XVector* v = data;
	XPrintf("0x30功能码调用\n");
	
}
void TJCHMICommTest()
{
	XPrintf("开始创建串口\n");
	XSerialPort* USART = XSerialPort_create();

	//XIODevice_setReadBuffer_base(USART, 1024);
	//XIODevice_setWriteBuffer_base(USART, 1024);
	XTJCHMIComm* comm = XTJCHMIComm_create(USART);
	XDataFrameComm_setFrameEndType_base(comm, XDFC_FRAME_END_MARKER);
	XDataFrameComm_setSendValidCRC16_base(comm, true);
	//XDataFrameComm_setRecvValidCRC16_base(comm, true);
	//XDataFrameComm_addPeriodicSendText(comm,false, 100, "ain.cuttingMotorSp.val=888");
	uint8_t funcCode = 0x1A;
	XDataFrameComm_addFuncCode(comm, &funcCode, XFuncCodeCb0x1A, NULL);
	funcCode = 0x30;
	XDataFrameComm_addFuncCode(comm, &funcCode, XFuncCodeCb0x30, NULL);
	if (!XDataFrameComm_connect_base(comm))
	{
		//XSerialPort_delete_base(USART);//内存管理已经被XTJCHMIComm接管
		XTJCHMIComm_delete_base(comm);
		XCoreApplication_quit();
		return;
	}
}

void XMenu_TJCHMICommTest(XMenu* root)
{
	XMenu* menu = XMenu_create("TJCHMIComm(陶晶驰通信类)");
	XMenu_addMenu(root, menu);
	{
		XAction* action = XMenu_addAction(menu, "主测试");
		XAction_setAction(action, TJCHMICommTest);
	}
}