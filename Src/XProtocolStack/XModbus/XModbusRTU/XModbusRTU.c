#include"XModbusRTU.h"
#include"XTimerWheel.h"
#include"XMemory.h"
#include<string.h>
XModbusRTU* XModbusRTU_newSerialPort(XSerialPortBase* serial, XTimerBase* timerT35Expired, XTimerBase* timerSendExpired)
{
	XModbusRTU* rtu = XMemory_malloc(sizeof(XModbusRTU));
	if (rtu == NULL)
		return rtu;
	XModbusRTU_init(rtu, timerT35Expired,timerSendExpired);
	((XCommunicatorBase*)rtu)->m_io = serial;
	uint32_t timerout = 3.5 * (10 + serial->m_parity) * 1000 / serial->m_baudRate;
	if (timerout < 2)
		timerout = 2;
	XTimerWheel_setTimeout_base(rtu->m_timerT35Expired, timerout);
	XTimerWheel_setTimeout_base(rtu->m_timerSendExpired, timerout* MB_MASTER_RECV_WAIT_TIME);
	return rtu;
}