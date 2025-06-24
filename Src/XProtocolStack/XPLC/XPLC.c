#include"XPLC.h"
#include"XMemory.h"
XPLC* XPLC_create()
{
	XPLC* plc = XMemory_malloc(sizeof(XPLC));
	XPLC_init(plc);
	return plc;
}

void XPLC_init(XPLC* plc)
{
	if (plc == NULL)
		return;

}

bool XPLC_addOutIODevice_base(XPLC* plc, int32_t id, XIODeviceBase* io)
{
	if (ISNULL(plc, "") || ISNULL(XClassGetVtable(plc), ""))
		return false;
	return XClassGetVirtualFunc(plc, EXPLC_AddOutIODevice, bool(*)(XPLC*, int32_t, XIODeviceBase*))(plc,id, io);
}

bool XPLC_addInIODevice_base(XPLC* plc, int32_t id,XIODeviceBase* io)
{
	if (ISNULL(plc, "") || ISNULL(XClassGetVtable(plc), ""))
		return false;
	return XClassGetVirtualFunc(plc, EXPLC_AddInIODevice, bool(*)(XPLC*, int32_t, XIODeviceBase*))(plc, id,io);
}

bool XPLC_removeOutId_base(XPLC* plc, int32_t id)
{
	if (ISNULL(plc, "") || ISNULL(XClassGetVtable(plc), ""))
		return false;
	return XClassGetVirtualFunc(plc, EXPLC_RemoveOutId, bool(*)(XPLC*, int32_t))(plc, id);
}

bool XPLC_removeInId_base(XPLC* plc, int32_t id)
{
	if (ISNULL(plc, "") || ISNULL(XClassGetVtable(plc), ""))
		return false;
	return XClassGetVirtualFunc(plc, EXPLC_RemoveInId, bool(*)(XPLC*, int32_t))(plc, id);
}

bool XPLC_removeIODevice_base(XPLC* plc, XIODeviceBase* io)
{
	if (ISNULL(plc, "") || ISNULL(XClassGetVtable(plc), ""))
		return false;
	return XClassGetVirtualFunc(plc, EXPLC_RemoveIODevice, bool(*)(XPLC*, XIODeviceBase*))(plc, io);
}

void XPLC_poll_base(XPLC* plc)
{
	if (ISNULL(plc, "") || ISNULL(XClassGetVtable(plc), ""))
		return ;
	XClassGetVirtualFunc(plc, EXPLC_Poll, void(*)(XPLC*))(plc);
}

void XPLC_setCallbackQueue(XPLC* plc, XIOCallbackQueue* queue)
{
	if (plc)
		plc->m_callbackQueue = queue;
}
