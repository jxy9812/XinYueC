#include "XModbusRegularlySendFrame.h"
#include "XMemory.h"
#include <string.h>
XModbusRegularlySendFrameLsit* XModbusRegularlySendFrameList_new()
{
	XModbusRegularlySendFrameLsit* list = XListSLinked_New(XModbusRegularlySendFrame);
	//memset(list,0,sizeof(XModbusRegularlySendFrame));

	return list;
}
