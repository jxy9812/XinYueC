#include "XStepMotor.h"
#include "XPrintf.h"   /* printf 直出改经 XPrintf（输出重定向栈，约束文档时间/依赖规则） */
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
	XVTABLE_INIT_DEFAULT(XStepMotor)
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
	//XVTABLE_OVERLOAD_DEFAULT(EXObject_Poll, NULL);
	XCLASS_SHOW_SIZE_DEFAULT(XStepMotor);
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
	motor->m_ena = ENA;
	motor->m_dir = DIR;
	motor->m_pul = PUL;
	XStepMotor_setDevice_base(motor, motor);
	XStepMotor_setControlMode_base(motor, XStepMotorMode_SpeedControl);
	//XObject_addEventFilter(motor, XEVENT_FUNC_RUN, XEventFunc_handler, NULL);
}

void VXStepMotor_deinit(XStepMotor* motor)
{
	if (!motor) return;
	// m_ena/m_dir/m_pul 是 init/create_ex 传入的借用指针，归调用方（创建者）所有；
	// 电机只是接收方，不负责释放（接收方代释放入参会造成错误释放或双释放）。
	//调用父类释放方法：释放 XObject 层资源（线程数据引用、信号槽、子对象、事件/定时器注销等）
	XClass_Deinit_Parent(XObject, (XObject*)motor);
}

bool VXStepMotor_isOpen(XStepMotor* motor)
{
	//m_pul必须要存在
	if(motor->m_pul==NULL|| !XPWMDeviceBase_isOpen(motor->m_pul))
		return false;
	if(motor->m_ena!=NULL&& !XSwitchDeviceBase_isOpen(motor->m_ena))
		return false;
	if (motor->m_dir != NULL && !XSwitchDeviceBase_isOpen(motor->m_dir))
		return false;
	return true;
}

void VXStepMotor_open(XStepMotor* motor)
{
	if(motor->m_pul)
		XPWMDeviceBase_open_base(motor->m_pul, XIODevice_WriteOnly);
	if (motor->m_ena)
		XSwitchDeviceBase_open_base(motor->m_ena, XIODevice_WriteOnly);
	if (motor->m_dir)
		XSwitchDeviceBase_open_base(motor->m_dir, XIODevice_WriteOnly);
}

bool VXStepMotor_isRunning(XStepMotor* motor)
{
	if (motor->m_pul == NULL || (motor->m_ena != NULL && !XSwitchDeviceBase_getState_base(motor->m_ena)))
		return false;
	return XPWMDeviceBase_isRunning_base(motor->m_pul);
}

void VXStepMotor_close(XStepMotor* motor)
{
	if (motor->m_dir)
		XSwitchDeviceBase_close_base(motor->m_dir);
	if (motor->m_ena)
		XSwitchDeviceBase_close_base(motor->m_ena);
	if (motor->m_pul)
		XPWMDeviceBase_close_base(motor->m_pul);
}

void VXStepMotor_IRQHandler(XStepMotor* motor)
{
	if (motor->m_pul == NULL || (motor->m_ena != NULL && !XSwitchDeviceBase_getState_base(motor->m_ena)))
		return;//pul不存在或使能没开的时候不计算距离
	//计算距离
	if (motor->m_dir == NULL || XSwitchDeviceBase_getState_base(motor->m_dir))
		++motor->m_directionPulses;
	else
		--motor->m_directionPulses;
	
	if (motor->m_controlMode == XStepMotorMode_SpeedControl)
		return;
	if (motor->m_controlMode & XStepMotorMode_DistanceControl || motor->m_controlMode & XStepMotorMode_PositionControl)
	{	//累计脉冲数
		++motor->m_currentPulses;
		if (motor->m_setPulses == motor->m_currentPulses)
		{//达到设定脉冲数了
			motor->m_setPulses = 0;
			motor->m_currentPulses = 0;//清空脉冲计数
			XPWMDeviceBase_stop_base(motor->m_pul);
		}
	}
}

void VXStepMotor_setDevice(XStepMotor* motor, void* device)
{
	//if (motor->m_pul)
	//{
	//	XIODevice_setDevice_base(motor->m_pul, device);
	//	//XIODevice_setCallbackQueue(motor->m_pul, XIODevice_CallbackQueue(motor));
	//}
	//if (motor->m_ena)
	//{
	//	XIODevice_setDevice_base(motor->m_ena, device);
	//	//XIODevice_setCallbackQueue(motor->m_ena, XIODevice_CallbackQueue(motor));
	//}
	//if (motor->m_dir)
	//{
	//	XIODevice_setDevice_base(motor->m_dir, device);
	//	//XIODevice_setCallbackQueue(motor->m_dir, XIODevice_CallbackQueue(motor));
	//}
}

void VXStepMotor_setENA(XStepMotor* motor, bool isEnabled)
{
	XSwitchDeviceBase_setState_base(motor->m_ena, isEnabled);
}

void VXStepMotor_setDIR(XStepMotor* motor, bool isForward)
{
	XSwitchDeviceBase_setState_base(motor->m_dir, isForward);
}

void VXStepMotor_start(XStepMotor* motor)
{
	//motor->m_currentSpeed = 0;//清空转速
	//开启pwm输出
	XPWMDeviceBase_start_base(motor->m_pul);
}

void VXStepMotor_stop(XStepMotor* motor)
{
	//关闭pwm输出
	if (VXStepMotor_isRunning(motor))
	{
		XPWMDeviceBase_stop_base(motor->m_pul);
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

		XPWMDeviceBase_setFrequency_base(motor->m_pul, secr);
		if (motor->m_pul->m_dutyCycle == 0)
			XPWMDeviceBase_setDutyCycle_base(motor->m_pul, 50);
		else
			XPWMDeviceBase_setDutyCycle_base(motor->m_pul, motor->m_pul->m_dutyCycle);
		if (VXStepMotor_isRunning(motor)&&motor->m_speedChangeCb!=NULL)
		{
			//XObject_postEvent(motor, XEventFunc_create(motor->m_speedChangeCb, motor, NULL), XEVENT_PRIORITY_NORMAL);
		}
	}
}

void VXStepMotor_setRevolutions(XStepMotor* motor, double revolutions)
{
	if (motor->m_controlMode == XStepMotorMode_DistanceControl)
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
		//XPrintf("脉冲数:%d\n",(int)motor->m_setPulses);
	}
	else if (motor->m_controlMode == XStepMotorMode_PositionControl)
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
		case XStepMotorMode_PositionControl: break;
		case XStepMotorMode_SpeedControl: break;
		case XStepMotorMode_TorqueControl:break;
		default:break;
	}
	motor->m_controlMode = mode;
}

bool VXStepMotor_isTaskFinish(XStepMotor* motor)
{
	return motor->m_setPulses==0;
}

//void VXStepMotor_IRQHandler(XStepMotor* motor)
//{
//	
//}
