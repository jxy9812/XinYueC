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
	//继承的函数
	XVtable_append_vtable(XStepMotorVtable, XClassVtable);
	void* table[] = {
		VXStepMotor_isOpen,
		VXStepMotor_open,VXStepMotor_isRunning,
		VXStepMotor_close,VXStepMotor_poll,
		VXStepMotor_setDevice,VXStepMotor_setENA,
		VXStepMotor_setDIR,VXStepMotor_start,
		VXStepMotor_stop,VXStepMotor_setStepsPerRevolution,
		VXStepMotor_setSpeed,VXStepMotor_setRevolutions
	};
	XVtable_append_array(XStepMotorVtable, table, sizeof(table) / sizeof(table[0]));
	//重写的函数
	XVtable_At(XStepMotorVtable, EXClass_Free) = VXStepMotor_free;
#if SHOWCONTAINERSIZE
	printf("XStepMotor size:%d\n", XVtable_size(XStepMotorVtable));
#endif
}

void XStepMotor_init(XStepMotor* motor, XSwitchDeviceBase* ENA, XSwitchDeviceBase* DIR, XPWMDeviceBase* PUL)
{
	if (motor == NULL || ENA == NULL || DIR == NULL || PUL == NULL)
		return NULL;
	//开始初始化
	memset(motor, 0, sizeof(XStepMotor));
	XStepMotor_class_init();
	XClassGetVtable(motor) = XStepMotorVtable;
	motor->m_ENA = ENA;
	motor->m_DIR = DIR;
	motor->m_PUL = PUL;
	XStepMotor_setDevice_base(motor, motor);
}

void VXStepMotor_free(XStepMotor* motor)
{
	if (motor->m_ENA)
		XSwitchDeviceBase_free_base(motor->m_ENA);
	if (motor->m_DIR)
		XSwitchDeviceBase_free_base(motor->m_DIR);
	if (motor->m_PUL)
		XPWMDeviceBase_free_base(motor->m_PUL);
	//调用父类释放方法
	XVtableGetFunc(XClassVtable, EXClass_Free,void(*)(XClass*));
}

bool VXStepMotor_isOpen(XStepMotor* motor)
{
	//m_PUL必须要存在
	if(motor->m_PUL==NULL|| !XPWMDeviceBase_isOpen(motor->m_PUL))
		return false;
	if(motor->m_ENA!=NULL&& !XSwitchDeviceBase_isOpen(motor->m_ENA))
		return false;
	if (motor->m_DIR != NULL && !XSwitchDeviceBase_isOpen(motor->m_DIR))
		return false;
	return true;
}

void VXStepMotor_open(XStepMotor* motor)
{
	if(motor->m_PUL)
		XPWMDeviceBase_open_base(motor->m_PUL, XIODeviceBase_WriteOnly);
	if (motor->m_ENA)
		XSwitchDeviceBase_open_base(motor->m_ENA, XIODeviceBase_WriteOnly);
	if (motor->m_DIR)
		XSwitchDeviceBase_open_base(motor->m_DIR, XIODeviceBase_WriteOnly);
}

bool VXStepMotor_isRunning(XStepMotor* motor)
{
	if(motor->m_PUL==NULL)
		return false;
	if(motor->m_ENA!=NULL&& !XSwitchDeviceBase_getState_base(motor->m_ENA))
		return false;
	return XPWMDeviceBase_isRunning_base(motor->m_PUL);
}

void VXStepMotor_close(XStepMotor* motor)
{
	if (motor->m_DIR)
		XSwitchDeviceBase_close_base(motor->m_DIR);
	if (motor->m_ENA)
		XSwitchDeviceBase_close_base(motor->m_ENA);
	if (motor->m_PUL)
		XPWMDeviceBase_close_base(motor->m_PUL);
}

void VXStepMotor_poll(XStepMotor* motor)
{
	//累计脉冲数
	++motor->m_currentPulses;
	//计算距离
	if (XSwitchDeviceBase_getState_base(motor->m_DIR))
		++motor->m_directionPulses;
	else
		--motor->m_directionPulses;

	if (motor->m_setPulses != 0 && motor->m_setPulses == motor->m_currentPulses)
	{//达到设定脉冲数了
		motor->m_setPulses = 0;
		XPWMDeviceBase_stop_base(motor->m_PUL);
	}
}

void VXStepMotor_setDevice(XStepMotor* motor, void* device)
{
	if(motor->m_PUL)
		XIODeviceBase_setDevice_base(motor->m_PUL, device);
	if (motor->m_ENA)
		XIODeviceBase_setDevice_base(motor->m_ENA, device);
	if (motor->m_DIR)
		XIODeviceBase_setDevice_base(motor->m_DIR, device);
}

void VXStepMotor_setENA(XStepMotor* motor, bool isEnabled)
{
	XSwitchDeviceBase_setState_base(motor->m_ENA, isEnabled);
}

void VXStepMotor_setDIR(XStepMotor* motor, bool isForward)
{
	XSwitchDeviceBase_setState_base(motor->m_DIR, isForward);
}

void VXStepMotor_start(XStepMotor* motor)
{
	motor->m_currentSpeed = 0;//清空转速
	motor->m_currentPulses = 0;//清空脉冲计数
	//开启pwm输出
	XPWMDeviceBase_start_base(motor->m_PUL);
}

void VXStepMotor_stop(XStepMotor* motor)
{
	//关闭pwm输出
	XPWMDeviceBase_stop_base(motor->m_PUL);
}

void VXStepMotor_setStepsPerRevolution(XStepMotor* motor, uint16_t steps)
{
	motor->m_pulsesPerRevolution = steps;
}

void VXStepMotor_setSpeed(XStepMotor* motor, double speed)
{
	motor->m_currentSpeed = speed;
	uint32_t secr = speed * motor->m_pulsesPerRevolution / 60.0;//一秒脉冲数目  频率

	XPWMDeviceBase_setFrequency_base(motor->m_PUL, secr);
	if (motor->m_PUL->m_dutyCycle == 0)
		XPWMDeviceBase_setDutyCycle_base(motor->m_PUL, 50);
}

void VXStepMotor_setRevolutions(XStepMotor* motor, double revolutions)
{
	if (revolutions > 0.0)
	{
		XStepMotor_setDIR_base(motor, true);
		motor->m_setPulses = revolutions * motor->m_pulsesPerRevolution;
	}
	else if (revolutions < 0.0)
	{
		XStepMotor_setDIR_base(motor, false);
		motor->m_setPulses = -revolutions * motor->m_pulsesPerRevolution;
	}
	//printf("脉冲数:%d\n",(int)motor->m_setPulses);
}
