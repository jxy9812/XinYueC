#include"XModbusRTU.h"
#include"XTimerWheel.h"
#include"XMemory.h"
#include<string.h>
XModbusRTU* XModbusRTU_createSerialPort(XSerialPortBase* serial, XTimerBase* timerT35Expired, XTimerBase* timerSendExpired)
{
	XModbusRTU* rtu = XMemory_malloc(sizeof(XModbusRTU));
	if (rtu == NULL)
		return rtu;
	XModbusRTU_init(rtu, serial,timerT35Expired,timerSendExpired);
	//printf("创建\n");
	uint32_t timerout = 3.5 * (10 + serial->m_parity) * 1000 / serial->m_baudRate;
	if (timerout < 2)
		timerout = 2;
	XTimerBase_setTimeout_base(((XDataFrameComm*)rtu)->m_timerRecvExpired, timerout);
	XTimerBase_setTimeout_base(((XDataFrameComm*)rtu)->m_timerSendExpired, timerout* MB_MASTER_RECV_WAIT_TIME);
	return rtu;
}