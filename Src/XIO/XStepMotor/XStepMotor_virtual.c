#include "XStepMotor.h"
#include "XMemory.h"
#include <string.h>
#include <stdio.h>
//声明 
static void VXStepMotor_free(XStepMotor* motor);
static bool VXStepMotor_isOpen(XStepMotor* motor);
static void VXStepMotor_open(XStepMotor* motor);
static bool VXStepMotor_isRunning(XStepMotor* motor);
static void VXStepMotor_close(XStepMotor* motor);
static void VXStepMotor_poll(XStepMotor* motor);
static void VXStepMotor_setDevice(XStepMotor* motor, void* device);
static void VXStepMotor_setENA(XStepMotor* motor, bool isEnabled);
static void VXStepMotor_setDIR(XStepMotor* motor, bool isForward);
static void VXStepMotor_start(XStepMotor* motor);
static void VXStepMotor_stop(XStepMotor* motor);
static void VXStepMotor_setStepsPerRevolution(XStepMotor* motor, uint16_t steps);
static void VXStepMotor_setSpeed(XStepMotor* motor, double speed);
static void VXStepMotor_setRevolutions(XStepMotor* motor, double revolutions);

XVtable* XStepMotorVtable = NULL;
#if VTABLE_ISSTACK
static XVtable vtable;//虚函数类
static void* vtable_data[XSTEPMOTOR_VTABLE_SIZE];//虚函数数据
#endif
void XStepMotor_class_init()
{
	//仅初始化一次
	if (XStepMotorVtable)
		return;
#if !VTABLE_ISSTACK
	XIODeviceVtable = XVtable_new();
#else
	XStepMotorVtable = &vtable;
	XVtable_init_stack(XStepMotorVtable, vtable_data, XSTEPMOTOR_VTABLE_SIZE);
#endif
	void* table[] = {
		VXStepMotor_free,VXStepMotor_isOpen,
		VXStepMotor_open,VXStepMotor_isRunning,
		VXStepMotor_close,VXStepMotor_poll,
		VXStepMotor_setDevice,VXStepMotor_setENA,
		VXStepMotor_setDIR,VXStepMotor_start,
		VXStepMotor_stop,VXStepMotor_setStepsPerRevolution,
		VXStepMotor_setSpeed,VXStepMotor_setRevolutions
	};
	XVtable_append_array(XStepMotorVtable, table, sizeof(table) / sizeof(table[0]));
#if SHOWCONTAINERSIZE
	printf("XStepMotor size:%d\n", XVtable_size(XStepMotorVtable));
#endif
}

void VXStepMotor_free(XStepMotor* motor)
{
}

bool VXStepMotor_isOpen(XStepMotor* motor)
{
	return false;
}

void VXStepMotor_open(XStepMotor* motor)
{
}

bool VXStepMotor_isRunning(XStepMotor* motor)
{
	return false;
}

void VXStepMotor_close(XStepMotor* motor)
{
}

void VXStepMotor_poll(XStepMotor* motor)
{
}

void VXStepMotor_setDevice(XStepMotor* motor, void* device)
{
}

void VXStepMotor_setENA(XStepMotor* motor, bool isEnabled)
{
}

void VXStepMotor_setDIR(XStepMotor* motor, bool isForward)
{
}

void VXStepMotor_start(XStepMotor* motor)
{
}

void VXStepMotor_stop(XStepMotor* motor)
{
}

void VXStepMotor_setStepsPerRevolution(XStepMotor* motor, uint16_t steps)
{
}

void VXStepMotor_setSpeed(XStepMotor* motor, double speed)
{
}

void VXStepMotor_setRevolutions(XStepMotor* motor, double revolutions)
{
}
