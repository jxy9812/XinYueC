#include"XPLC.h"
#include"XMemory.h"
#include"XTimerBase.h"
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
	XObject_init(plc);
	XClassGetVtable(plc) = XPLC_class_init();
	//XPLC_setDelayMsCb(plc, XTimerBase_delay_ms);
	XPLC_setScanPeriod(plc,10);
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

void XPLC_setScanPeriod(XPLC* plc, uint16_t delay_ms)
{
	if (plc)
		plc->m_delay_ms = delay_ms;
}

int32_t XPLC_getScanPeriod(XPLC* plc)
{
	if(plc)
		return plc->m_delay_ms;
	return -1;
}

void XPLC_setDelayMsCb(XPLC* plc, void(*delay_ms_cb)(size_t))
{
	if (plc)
		plc->m_delay_ms_cb = delay_ms_cb;
}
