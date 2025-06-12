#include"XProtocolStackTest.h"
#include"XDataFrameComm.h"
#include"XSerialPortBase.h"
#include"XVector.h"
void XDataFrameCommTest()
{
	printf("开始创建串口\n");
	XSerialPortBase* USART = XSerialPortWin32_create(); 
	USART->m_baudRate = 9600;
	USART->m_portNum = 2;
	XDataFrameComm* comm = XDataFrameComm_create(USART);
	XDataFrameComm_setFrameEndType_base(comm, XDFC_FRAME_END_MARKER);
	{
		uint8_t frameTail[] = { 0xFF,0xFF,0xFF };
		XDataFrameComm_setRecvFrameTail(comm, frameTail, 3);
		XDataFrameComm_setSendFrameTail(comm, frameTail, 3);
	}
	
	{
		XDataFrameComm_sendString(comm, "main.cuttingMotorSp.val=999",false);
	}
	XDataFrameComm_connect_base(comm);
	while (true)
	{
		XDataFrameComm_poll_base(comm);
	}
}