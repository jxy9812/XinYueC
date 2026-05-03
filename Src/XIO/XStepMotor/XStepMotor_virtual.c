#include "XStepMotor.h"
#include "XMemory.h"
#include <string.h>
#include <stdio.h>
//声明 
static void VXStepMotor_deinit(XStepMotor* motor);
static bool VXStepMotor_isOpen(XStepMotor* motor);
static void VXStepMotor_open(XStepMotor* motor);
static bool VXStepMotor_isRunning(XStepMotor* motor);
static void VXStepMotor_close(XStepMotor* motor);
static void VXStepMotor_IRQHandler(XStepMotor* motor);
static void VXStepMotor_setDevice(XStepMotor* motor, void* device);
static void VXStepMotor_setENA(XStepMotor* motor, bool isEnabled);
static void VXStepMotor_setDIR(XStepMotor* motor, bool isForward);
static void VXStepMotor_start(XStepMotor* motor);
static void VXStepMotor_stop(XStepMotor* motor);
static void VXStepMotor_setStepsPerRevolution(XStepMotor* motor, uint16_t steps);
static void VXStepMotor_setSpeed(XStepMotor* motor, double speed);
static void VXStepMotor_setRevolutions(XStepMotor* motor, double revolutions);
static void VXStepMotor_setControlMode(XStepMotor* motor, XStepMotorMode mode);
static bool VXStepMotor_isTaskFinish(XStepMotor* motor);
XVtable* XStepMotor_class_init()
{
	XVTABLE_CREAT_DEFAULT
		//虚函数表初始化
#if VTABLE_ISSTACK
	XVTABLE_STACK_INIT_DEFAULT(XSTEPMOTOR_VTABLE_SIZE)
#else
	XVTABLE_HEAP_INIT_DEFAULT
#endif
	//继承类
	XVTABLE_INHERIT_XCLASS(XClass);
	void* table[] = {
		VXStepMotor_isOpen,
		VXStepMotor_open,VXStepMotor_isRunning,
		VXStepMotor_close,VXStepMotor_IRQHandler,
		VXStepMotor_setDevice,VXStepMotor_setENA,
		VXStepMotor_setDIR,VXStepMotor_start,
		VXStepMotor_stop,VXStepMotor_setStepsPerRevolution,
		VXStepMotor_setSpeed,VXStepMotor_setRevolutions,
		VXStepMotor_setControlMode,VXStepMotor_isTaskFinish
	};
	//追加虚函数
	XVTABLE_ADD_FUNC_LIST_DEFAULT(table);
	//重载
	XVTABLE_OVERLOAD_DEFAULT(EXClass_Deinit, VXStepMotor_deinit);
	XVTABLE_OVERLOAD_DEFAULT(EXObject_Poll, NULL);
#if SHOWCONTAINERSIZE
	printf("XStepMotor size:%d\n", XVtable_size(XVTABLE_DEFAULT));
#endif
	return XVTABLE_DEFAULT;
}

void XStepMotor_init(XStepMotor* motor, XSwitchDeviceBase* ENA, XSwitchDeviceBase* DIR, XPWMDeviceBase* PUL)
{
	if (motor == NULL || PUL == NULL)
		return NULL;
	//开始初始化
	memset(motor, 0, sizeof(XStepMotor));
	XObject_init(motor);
	XClassGetVtable(motor) = XStepMotor_class_init();
	motor->m_ENA = ENA;
	motor->m_DIR = DIR;
	motor->m_PUL = PUL;
	XStepMotor_setDevice_base(motor, motor);
	XStepMotor_setControlMode_base(motor, XSM_SPEED_CONTROL);
	//XObject_addEventFilter(motor, XEVENT_FUNC_RUN, XEventFunc_handler, NULL);
}

void VXStepMotor_deinit(XStepMotor* motor)
{
	if (motor->m_ENA)
	{
		XSwitchDeviceBase_delete_base(motor->m_ENA);
		motor->m_ENA=NULL;
	}
	if (motor->m_DIR)
	{
		XSwitchDeviceBase_delete_base(motor->m_DIR);
		motor->m_DIR=NULL;
	}
	if (motor->m_PUL)
	{
		XPWMDeviceBase_delete_base(motor->m_PUL);
		motor->m_PUL = NULL;
	}
	//调用父类释放方法
	XVtableGetFunc(XClass_class_init(), EXClass_Deinit,void(*)(XClass*));
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
		XPWMDeviceBase_open_base(motor->m_PUL, XIODevice_WriteOnly);
	if (motor->m_ENA)
		XSwitchDeviceBase_open_base(motor->m_ENA, XIODevice_WriteOnly);
	if (motor->m_DIR)
		XSwitchDeviceBase_open_base(motor->m_DIR, XIODevice_WriteOnly);
}

bool VXStepMotor_isRunning(XStepMotor* motor)
{
	if (motor->m_PUL == NULL || (motor->m_ENA != NULL && !XSwitchDeviceBase_getState_base(motor->m_ENA)))
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

void VXStepMotor_IRQHandler(XStepMotor* motor)
{
	if (motor->m_PUL == NULL || (motor->m_ENA != NULL && !XSwitchDeviceBase_getState_base(motor->m_ENA)))
		return;//pul不存在或使能没开的时候不计算距离
	//计算距离
	if (motor->m_DIR == NULL || XSwitchDeviceBase_getState_base(motor->m_DIR))
		++motor->m_directionPulses;
	else
		--motor->m_directionPulses;
	
	if (motor->m_ControlMode == XSM_SPEED_CONTROL)
		return;
	if (motor->m_ControlMode & XSM_DISTANCE_CONTROL || motor->m_ControlMode & XSM_POSITION_CONTROL)
	{	//累计脉冲数
		++motor->m_currentPulses;
		if (motor->m_setPulses == motor->m_currentPulses)
		{//达到设定脉冲数了
			motor->m_setPulses = 0;
			motor->m_currentPulses = 0;//清空脉冲计数
			XPWMDeviceBase_stop_base(motor->m_PUL);
		}
	}
}

void VXStepMotor_setDevice(XStepMotor* motor, void* device)
{
	//if (motor->m_PUL)
	//{
	//	XIODevice_setDevice_base(motor->m_PUL, device);
	//	//XIODevice_setCallbackQueue(motor->m_PUL, XIODevice_CallbackQueue(motor));
	//}
	//if (motor->m_ENA)
	//{
	//	XIODevice_setDevice_base(motor->m_ENA, device);
	//	//XIODevice_setCallbackQueue(motor->m_ENA, XIODevice_CallbackQueue(motor));
	//}
	//if (motor->m_DIR)
	//{
	//	XIODevice_setDevice_base(motor->m_DIR, device);
	//	//XIODevice_setCallbackQueue(motor->m_DIR, XIODevice_CallbackQueue(motor));
	//}
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
	//motor->m_currentSpeed = 0;//清空转速
	//开启pwm输出
	XPWMDeviceBase_start_base(motor->m_PUL);
}

void VXStepMotor_stop(XStepMotor* motor)
{
	//关闭pwm输出
	if (VXStepMotor_isRunning(motor))
	{
		XPWMDeviceBase_stop_base(motor->m_PUL);
		if (motor->m_currentSpeed != 0)
		{
			//motor->m_currentSpeed = 0;
			if (motor->m_speedChangeCb)
			{
				//XObject_postEvent(motor,XEventFunc_create(motor->m_speedChangeCb,motor, NULL), XEVENT_PRIORITY_NORMAL);
			}
		}
	}
}

void VXStepMotor_setStepsPerRevolution(XStepMotor* motor, uint16_t steps)
{
	motor->m_pulsesPerRevolution = steps;
}

void VXStepMotor_setSpeed(XStepMotor* motor, double speed)
{
	if (motor->m_currentSpeed != speed)
	{
		motor->m_currentSpeed = speed;
		uint32_t secr = speed * motor->m_pulsesPerRevolution / 60.0;//一秒脉冲数目  频率

		XPWMDeviceBase_setFrequency_base(motor->m_PUL, secr);
		if (motor->m_PUL->m_dutyCycle == 0)
			XPWMDeviceBase_setDutyCycle_base(motor->m_PUL, 50);
		else
			XPWMDeviceBase_setDutyCycle_base(motor->m_PUL, motor->m_PUL->m_dutyCycle);
		if (VXStepMotor_isRunning(motor)&&motor->m_speedChangeCb!=NULL)
		{
			//XObject_postEvent(motor, XEventFunc_create(motor->m_speedChangeCb, motor, NULL), XEVENT_PRIORITY_NORMAL);
		}
	}
}

void VXStepMotor_setRevolutions(XStepMotor* motor, double revolutions)
{
	if (motor->m_ControlMode == XSM_DISTANCE_CONTROL)
	{
		if (revolutions > 0.0)
		{
			XStepMotor_setDIR_base(motor, true);
			motor->m_setPulses = revolutions * (motor->m_pulsesPerRevolution);
		}
		else if (revolutions < 0.0)
		{
			XStepMotor_setDIR_base(motor, false);
			motor->m_setPulses = -1.0 * revolutions * (motor->m_pulsesPerRevolution);
		}
		XStepMotor_resetRevolutions(motor);
		motor->m_currentPulses = 0;//清空脉冲计数
		//printf("脉冲数:%d\n",(int)motor->m_setPulses);
	}
	else if (motor->m_ControlMode == XSM_POSITION_CONTROL)
	{
		int64_t posPulses = revolutions * (motor->m_pulsesPerRevolution);//设置的位置脉冲数
		if ((motor->m_directionPulses) > posPulses)
		{
			XStepMotor_setDIR_base(motor, false);
			motor->m_setPulses = (motor->m_directionPulses) - posPulses;
		}
		else if ((motor->m_directionPulses) < posPulses)
		{
			XStepMotor_setDIR_base(motor, true);
			motor->m_setPulses = posPulses - (motor->m_directionPulses);
		}
		else
		{
			return;
		}
		XStepMotor_resetRevolutions(motor);
		motor->m_currentPulses = 0;//清空脉冲计数
	}
}

void VXStepMotor_setControlMode(XStepMotor* motor, XStepMotorMode mode)
{
	switch (mode)
	{
		case XSM_POSITION_CONTROL: break;
		case XSM_SPEED_CONTROL: break;
		case XSM_TORQUE_CONTROL:break;
		default:break;
	}
	motor->m_ControlMode = mode;
}

bool VXStepMotor_isTaskFinish(XStepMotor* motor)
{
	return motor->m_setPulses==0;
}

//void VXStepMotor_IRQHandler(XStepMotor* motor)
//{
//	
//}
