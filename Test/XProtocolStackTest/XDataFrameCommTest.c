#include"XProtocolStackTest.h"
#include"XDataFrameComm.h"
#include"XSerialPort.h"
#include"XVector.h"
#include"XTimer.h"
#include"XMenu.h"
#include"XAction.h"
#include"XCoreApplication.h"
#include"XPrintf.h"
#include"XDateTime.h"
void XDataFrameCommTest()
{
	XPrintf("开始创建串口\n");
	XSerialPort* USART = XSerialPort_create(); 
	
	//XIODevice_setReadBuffer_base(USART,1024);
	//XIODevice_setWriteBuffer_base(USART, 1024);
	XDataFrameComm* comm = XDataFrameComm_create(USART);
	//XDataFrameComm_setFrameEndType_base(comm, XDFC_FRAME_END_MARKER);
	{
		uint8_t sendFrameTail[] = {0x01, 0xFE,0xFE,0xFE };
		uint8_t recvFrameTail1[] = { 0x01, 0xFE,0xFE,0xFE };
		XDataFrameComm_setSendFrameTail_base(comm, sendFrameTail, sizeof(sendFrameTail));
		XDataFrameComm_setRecvFrameTail_base(comm,  recvFrameTail1, sizeof(recvFrameTail1));
	}
	XDataFrameComm_setCommMode_base(comm, XDFC_COMM_MODE_HALF_DUPLEX);
	XDataFrameComm_setFrameEndType_base(comm, XDFC_FRAME_END_MARKER);
	XDataFrameComm_setSendValidCRC16_base(comm,true);
	XDataFrameComm_setRecvValidCRC16_base(comm,true);
	//XDataFrameComm_addPeriodicSendText(comm,false, 500, "main.cuttingMotorSp.val=888");
	if (!XDataFrameComm_connect_base(comm))
	{
		//XSerialPort_delete_base(USART);//内存管理已经被XDataFrameComm接管
		XDataFrameComm_delete_base(comm);
		XCoreApplication_quit();
		return;
	}
	size_t speed=1,current = XDateTime_currentMSecsSinceEpoch();
	while (true)
	{
		if (XDateTime_currentMSecsSinceEpoch() > current + 1000)
		{
			//XDataFrameComm_sendTextFmt(comm, false,  "ain.cuttingMotorSp.val=%d", speed++);
			current = XDateTime_currentMSecsSinceEpoch();
		}
		//XDataFrameComm_poll_base(comm);
	}
}

void XMenu_XDataFrameCommTest(XMenu* root)
{
	XMenu* menu = XMenu_create("XDataFrameComm(数据帧通信类)");
	XMenu_addMenu(root, menu);
	{
		XAction* action = XMenu_addAction(menu, "主测试");
		XAction_setAction(action, XDataFrameCommTest);
	}
}