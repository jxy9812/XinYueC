#include"XPWMDevice.h"
#include"XMemory.h"
#include<string.h>
XPWMDevice* XPWMDevice_new(XPWMDevice_PortFuncInit* port)
{
	if (port == NULL)
		return NULL;
	XPWMDevice* pwm = XMemory_malloc(sizeof(XPWMDevice));
	if (pwm == NULL)
		return pwm;
	
	XPWMDevice_init(pwm, port);
	return pwm;
}

void XPWMDevice_init(XPWMDevice* pwm, XPWMDevice_PortFuncInit* port)
{
	if (pwm == NULL || port == NULL)
		return ;
	memset(pwm, 0, sizeof(XPWMDevice));
	XIODevice_init(&(pwm->m_parent), &(port->parentPort));
	//开始初始化
	//memset(&(pwm->m_parent)+1, 0, sizeof(XPWMDevice) - sizeof(XIODevice) - sizeof(XPWMDevice_PortFunc));
	//pwm->m_timer = XTimer_new(&(port->timerPort));
	//pwm->m_timer->data = pwm;
	//绑定函数指针
	memcpy(&(pwm->m_port), &(port->pwmPort), sizeof(XPWMDevice_PortFunc));

	XPWMDevice_class_init();
	XClassGetVtable(pwm) = XPWMDeviceVtable;
}

void XPWMDevice_setFrequency_base(XPWMDevice* pwm, size_t f)
{
	if (ISNULL(pwm, "") || ISNULL(XClassGetVtable(pwm), ""))
		return;
	XClassGetVirtualFunc(pwm, EXPWMDevice_SetFrequency, void(*)(XPWMDevice*, size_t))(pwm, f);
}

void XPWMDevice_setDutyCycle_base(XPWMDevice* pwm, uint8_t d)
{
	if (ISNULL(pwm, "") || ISNULL(XClassGetVtable(pwm), ""))
		return;
	XClassGetVirtualFunc(pwm, EXPWMDevice_SetDutyCycle, void(*)(XPWMDevice*, uint8_t))(pwm, d);
}

void XPWMDevice_start_base(XPWMDevice* pwm)
{
	if (ISNULL(pwm, "") || ISNULL(XClassGetVtable(pwm), ""))
		return;
	XClassGetVirtualFunc(pwm, EXPWMDevice_Start, void(*)(XPWMDevice*))(pwm);
}

void XPWMDevice_stop_base(XPWMDevice* pwm)
{
	if (ISNULL(pwm, "") || ISNULL(XClassGetVtable(pwm), ""))
		return;
	XClassGetVirtualFunc(pwm, EXPWMDevice_Stop, void(*)(XPWMDevice*))(pwm);
}

bool XPWMDevice_isRunning_base(XPWMDevice* pwm)
{
	if (ISNULL(pwm, "") || ISNULL(XClassGetVtable(pwm), ""))
		return false;
	return XClassGetVirtualFunc(pwm, EXPWMDevice_IsRunning, bool(*)(XPWMDevice*))(pwm);
}

size_t XPWMDevice_getFrequency_base(XPWMDevice* pwm)
{
	if (ISNULL(pwm, "") || ISNULL(XClassGetVtable(pwm), ""))
		return 0;
	return XClassGetVirtualFunc(pwm, EXPWMDevice_GetFrequency, size_t(*)(XPWMDevice*))(pwm);
}

uint8_t XPWMDevice_getDutyCycle_base(XPWMDevice* pwm)
{
	if (ISNULL(pwm, "") || ISNULL(XClassGetVtable(pwm), ""))
		return 0;
	return XClassGetVirtualFunc(pwm, EXPWMDevice_GetDutyCycle, uint8_t(*)(XPWMDevice*))(pwm);
}
