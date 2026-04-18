#include "XStepMotor.h"
#include "XMemory.h"
#include <string.h>
#include <stdio.h>

XStepMotor* XStepMotor_create(XSwitchDeviceBase* ENA, XSwitchDeviceBase* DIR, XPWMDeviceBase* PUL)
{
	if (PUL==NULL)
		return NULL;
	XStepMotor* motor = XMemory_malloc(sizeof(XStepMotor));
	if (motor == NULL)
		return motor;
	XStepMotor_init(motor, ENA,DIR,PUL);
	Set_Class_MemoryFree(motor, XFree);
	return motor;
}

void XStepMotor_open_base(XStepMotor* motor)
{
	if (ISNULL(motor, "") || ISNULL(XClassGetVtable(motor), ""))
		return ;
	XClassGetVirtualFunc(motor, EXStepMotor_Open, void(*)(XStepMotor*))(motor);
}

void XStepMotor_setDevice_base(XStepMotor* motor, void* device)
{
	if (ISNULL(motor, "") || ISNULL(XClassGetVtable(motor), ""))
		return;
	XClassGetVirtualFunc(motor, EXStepMotor_SetDevice, void(*)(XStepMotor*, void*))(motor,device);
}

void XStepMotor_setENA_base(XStepMotor* motor, bool isEnabled)
{
	if (ISNULL(motor, "") || ISNULL(XClassGetVtable(motor), ""))
		return;
	XClassGetVirtualFunc(motor, EXStepMotor_SetENA, void(*)(XStepMotor*, bool))(motor, isEnabled);
}

void XStepMotor_setDIR_base(XStepMotor* motor, bool isForward)
{
	if (ISNULL(motor, "") || ISNULL(XClassGetVtable(motor), ""))
		return;
	XClassGetVirtualFunc(motor, EXStepMotor_SetDIR, void(*)(XStepMotor*, bool))(motor, isForward);
}

void XStepMotor_start_base(XStepMotor* motor)
{
	if (ISNULL(motor, "") || ISNULL(XClassGetVtable(motor), ""))
		return;
	XClassGetVirtualFunc(motor, EXStepMotor_Start, void(*)(XStepMotor*))(motor);
}

void XStepMotor_stop_base(XStepMotor* motor)
{
	if (ISNULL(motor, "") || ISNULL(XClassGetVtable(motor), ""))
		return;
	XClassGetVirtualFunc(motor, EXStepMotor_Stop, void(*)(XStepMotor*))(motor);
}

void XStepMotor_setStepsPerRevolution_base(XStepMotor* motor, uint16_t num)
{
	if (ISNULL(motor, "") || ISNULL(XClassGetVtable(motor), ""))
		return;
	XClassGetVirtualFunc(motor, EXStepMotor_SetStepsPerRevolution, void(*)(XStepMotor*, uint16_t))(motor, num);
}

void XStepMotor_setSpeed_base(XStepMotor* motor, double speed)
{
	if (ISNULL(motor, "") || ISNULL(XClassGetVtable(motor), ""))
		return;
	XClassGetVirtualFunc(motor, EXStepMotor_SetSpeed, void(*)(XStepMotor*, double))(motor, speed);
}

void XStepMotor_setControlMode_base(XStepMotor* motor, XStepMotorMode mode)
{
	if (ISNULL(motor, "") || ISNULL(XClassGetVtable(motor), ""))
		return;
	XClassGetVirtualFunc(motor, EXStepMotor_SetControlMode, void(*)(XStepMotor*, XStepMotorMode))(motor, mode);
}

void XStepMotor_setRevolutions_base(XStepMotor* motor, double revolutions)
{
	if (ISNULL(motor, "") || ISNULL(XClassGetVtable(motor), ""))
		return;
	XClassGetVirtualFunc(motor, EXStepMotor_SetRevolutions, void(*)(XStepMotor*, double))(motor, revolutions);
}

double XStepMotor_getRevolutions(XStepMotor* motor)
{
	if (motor == NULL)
		return 0.0;
	return ((double)motor->m_currentPulses) / motor->m_pulsesPerRevolution;
}

void XStepMotor_resetRevolutions(XStepMotor* motor)
{
	if (motor)
		motor->m_currentPulses = 0;
}

double XStepMotor_getPosition(XStepMotor* motor)
{
	if (motor == NULL)
		return 0.0;
	return ((double)motor->m_directionPulses) / motor->m_pulsesPerRevolution;
}

void XStepMotor_resetOrigin(XStepMotor* motor)
{
	if (motor != NULL)
		motor->m_directionPulses = 0;
}

bool XStepMotor_isTaskFinish_base(XStepMotor* motor)
{
	if (ISNULL(motor, "") || ISNULL(XClassGetVtable(motor), ""))
		return false;
	return XClassGetVirtualFunc(motor, EXStepMotor_IsTaskFinish, bool(*)(XStepMotor*))(motor);
}

bool XStepMotor_isOpen_base(XStepMotor* motor)
{
	if (ISNULL(motor, "") || ISNULL(XClassGetVtable(motor), ""))
		return false;
	return XClassGetVirtualFunc(motor, EXStepMotor_IsOpen, bool(*)(XStepMotor*))(motor);
}

bool XStepMotor_isRunning_base(XStepMotor* motor)
{
	if (ISNULL(motor, "") || ISNULL(XClassGetVtable(motor), ""))
		return false;
	return XClassGetVirtualFunc(motor, EXStepMotor_IsRunning, bool(*)(XStepMotor*))(motor);
}
void XStepMotor_IRQHandler(XStepMotor* motor)
{
	if (ISNULL(motor, "") || ISNULL(XClassGetVtable(motor), ""))
		return;
	XClassGetVirtualFunc(motor, EXStepMotor_IRQHandler, void(*)(XStepMotor*))(motor);
}

//void XStepMotor_IRQHandler(XStepMotor* motor)
//{
//	if (ISNULL(motor, "") || ISNULL(XClassGetVtable(motor), ""))
//		return;
//	XClassGetVirtualFunc(motor, EXStepMotor_IRQHandler, void(*)(XStepMotor*))(motor);
//}

void XStepMotor_close_base(XStepMotor* motor)
{
	if (ISNULL(motor, "") || ISNULL(XClassGetVtable(motor), ""))
		return;
	XClassGetVirtualFunc(motor, EXStepMotor_Close, void(*)(XStepMotor*))(motor);
}

void XStepMotor_setSpeedChangeCb(XStepMotor* motor, void(*speedChangeCb)(XStepMotor* motor))
{
	if (motor)
		motor->m_speedChangeCb = speedChangeCb;
}
