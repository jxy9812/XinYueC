#include"XProtocolStackTest.h"
#include"XDataFrameComm.h"
#include"XSerialPortBase.h"
#include"XVector.h"
#include"XTimerBase.h"
void XDataFrameCommTest()
{
	printf("开始创建串口\n");
	XSerialPortBase* USART = XSerialPortWin32_create(); 
	USART->m_baudRate = 115200;
	USART->m_portNum = 20;
	XIODeviceBase_setReadBuffer_base(USART,1024);
	XIODeviceBase_setWriteBuffer_base(USART, 1024);
	XDataFrameComm* comm = XDataFrameComm_create(USART);
	XDataFrameComm_setFrameEndType_base(comm, XDFC_FRAME_END_MARKER);
	{
		uint8_t sendFrameTail[] = {0x01, 0xFE,0xFE,0xFE };
		uint8_t recvFrameTail[] = { 0xFF,0xFF,0xFF };
		XDataFrameComm_setSendFrameTail(comm, sendFrameTail, sizeof(sendFrameTail));
		XDataFrameComm_setRecvFrameTail(comm, recvFrameTail, sizeof(recvFrameTail));
	}
	XDataFrameComm_setCommMode_base(comm, XDFC_COMM_MODE_HALF_DUPLEX);
	XDataFrameComm_setFrameEndType_base(comm, XDFC_FRAME_END_MARKER);
	XDataFrameComm_setSendValidCRC16(comm,true);
	XDataFrameComm_setRecvValidCRC16(comm,true);
	//XDataFrameComm_addPeriodicSendText(comm,false, 500, "main.cuttingMotorSp.val=888");
	XDataFrameComm_connect_base(comm);
	size_t speed=1,current = XTimerBase_getCurrentTime();
	while (true)
	{
		if (XTimerBase_getCurrentTime() > current + 1000)
		{
			XDataFrameComm_sendTextFmt(comm, false,  "main.cuttingMotorSp.val=%d", speed++);
			current = XTimerBase_getCurrentTime();
		}
		XDataFrameComm_poll_base(comm);
	}
}