#include"XPWMDeviceBase.h"
#include"XMemory.h"
#include<string.h>
XPWMDeviceBase* XPWMDeviceBase_create_ex(XMemoryType memory)
{
	/*if (port == NULL)
		return NULL;*/
	XPWMDeviceBase* pwm = XMemory_malloc(sizeof(XPWMDeviceBase), memory);
	if (pwm == NULL)
		return pwm;
	
	XPWMDeviceBase_init(pwm);
	Set_Class_Memory(pwm, memory); Set_Class_IsHeap(pwm, true);
	return pwm;
}

void XPWMDeviceBase_init(XPWMDeviceBase* pwm)
{
	if (pwm == NULL )
		return ;
	//初始化父类以外的数据
	memset(((XIODevice*)pwm)+1, 0, sizeof(XPWMDeviceBase) - sizeof(XIODevice));
	XIODevice_init(pwm);
	//开始初始化
	XPWMDeviceBase_class_init();
	XClassGetVtable(pwm) = XPWMDeviceBase_class_init();
}

void XPWMDeviceBase_setRunChangeCallback(XPWMDeviceBase* sw, void(*callback)(XPWMDeviceBase* pwm))
{
	if (sw)
		sw->m_runChangeCallback = callback;
}

void XPWMDeviceBase_setFrequency_base(XPWMDeviceBase* pwm, size_t f)
{
	if (ISNULL(pwm, "") || ISNULL(XClassGetVtable(pwm), ""))
		return;
	XClassGetVirtualFunc(pwm, EXPWMDeviceBase_SetFrequency, void(*)(XPWMDeviceBase*, size_t))(pwm, f);
}

void XPWMDeviceBase_setDutyCycle_base(XPWMDeviceBase* pwm, uint8_t d)
{
	if (ISNULL(pwm, "") || ISNULL(XClassGetVtable(pwm), ""))
		return;
	XClassGetVirtualFunc(pwm, EXPWMDeviceBase_SetDutyCycle, void(*)(XPWMDeviceBase*, uint8_t))(pwm, d);
}

void XPWMDeviceBase_start_base(XPWMDeviceBase* pwm)
{
	if (ISNULL(pwm, "") || ISNULL(XClassGetVtable(pwm), ""))
		return;
	XClassGetVirtualFunc(pwm, EXPWMDeviceBase_Start, void(*)(XPWMDeviceBase*))(pwm);
}

void XPWMDeviceBase_stop_base(XPWMDeviceBase* pwm)
{
	if (ISNULL(pwm, "") || ISNULL(XClassGetVtable(pwm), ""))
		return;
	XClassGetVirtualFunc(pwm, EXPWMDeviceBase_Stop, void(*)(XPWMDeviceBase*))(pwm);
}

bool XPWMDeviceBase_isRunning_base(XPWMDeviceBase* pwm)
{
	if (ISNULL(pwm, "") || ISNULL(XClassGetVtable(pwm), ""))
		return false;
	return XClassGetVirtualFunc(pwm, EXPWMDeviceBase_IsRunning, bool(*)(XPWMDeviceBase*))(pwm);
}

size_t XPWMDeviceBase_getFrequency_base(XPWMDeviceBase* pwm)
{
	if (ISNULL(pwm, "") || ISNULL(XClassGetVtable(pwm), ""))
		return 0;
	return XClassGetVirtualFunc(pwm, EXPWMDeviceBase_GetFrequency, size_t(*)(XPWMDeviceBase*))(pwm);
}

uint8_t XPWMDeviceBase_getDutyCycle_base(XPWMDeviceBase* pwm)
{
	if (ISNULL(pwm, "") || ISNULL(XClassGetVtable(pwm), ""))
		return 0;
	return XClassGetVirtualFunc(pwm, EXPWMDeviceBase_GetDutyCycle, uint8_t(*)(XPWMDeviceBase*))(pwm);
}
