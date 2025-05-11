#include "XModbusRegularlySendFrame.h"
#include "XMemory.h"
#include <string.h>
XModbusRegularlySendFrameLsit* XModbusRegularlySendFrameLsit_new()
{
	XModbusRegularlySendFrameLsit* list = XList_New(XModbusRegularlySendFrame);
	//memset(list,0,sizeof(XModbusRegularlySendFrame));

	return list;
}
