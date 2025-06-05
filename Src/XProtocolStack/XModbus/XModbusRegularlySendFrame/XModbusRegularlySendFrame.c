#include "XModbusRegularlySendFrame.h"
#include "XMemory.h"
#include <string.h>
XModbusRegularlySendFrameLsit* XModbusRegularlySendFrameList_create()
{
	XModbusRegularlySendFrameLsit* list = XListSLinked_Create(XModbusRegularlySendFrame);
	//memset(list,0,sizeof(XModbusRegularlySendFrame));

	return list;
}
