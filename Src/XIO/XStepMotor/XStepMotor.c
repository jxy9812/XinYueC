#include "XStepMotor.h"
#include "XMemory.h"
#include <string.h>
#include <stdio.h>

XStepMotor* XStepMotor_new(XSwitchDeviceBase* ENA, XSwitchDeviceBase* DIR, XPWMDeviceBase* PUL)
{
	if (ENA == NULL|| DIR==NULL|| PUL==NULL)
		return NULL;
	XStepMotor* motor = XMemory_malloc(sizeof(XSwitchDeviceBase));
	if (motor == NULL)
		return motor;
	XStepMotor_init(motor, ENA,DIR,PUL);
	return motor;
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

void XStepMotor_open_base(XStepMotor* motor)
{
	if (motor != NULL)
	{
		XIODeviceBase_open_base(motor->m_ENA, XIODeviceBase_WriteOnly);
		XIODeviceBase_open_base(motor->m_DIR, XIODeviceBase_WriteOnly);
		XIODeviceBase_open_base(motor->m_PUL, XIODeviceBase_WriteOnly);
	}
}

void XStepMotor_setDevice_base(XStepMotor* motor, void* device)
{
	if (motor != NULL)
	{
		XIODeviceBase_setDevice_base(motor->m_PUL, device);
		XIODeviceBase_setDevice_base(motor->m_ENA, device);
		XIODeviceBase_setDevice_base(motor->m_DIR, device);
	}
}

void XStepMotor_setENA_base(XStepMotor* motor, bool open)
{
	if (motor == NULL)
		return;
	XSwitchDeviceBase_setState_base(motor->m_ENA, open);
}

void XStepMotor_setDIR_base(XStepMotor* motor, bool corotation)
{
	if (motor == NULL)
		return;
	XSwitchDeviceBase_setState_base(motor->m_DIR, corotation);
}

void XStepMotor_start_base(XStepMotor* motor)
{
	if (motor == NULL)
		return;
	motor->m_currentSpeed = 0;//清空转速
	motor->m_currentPulses = 0;//清空脉冲计数
	//motor->start = true;
	//开启pwm输出
	XPWMDeviceBase_start_base(motor->m_PUL);
}

void XStepMotor_stop_base(XStepMotor* motor)
{
	if (motor == NULL)
		return;
	//关闭pwm输出
	XPWMDeviceBase_stop_base(motor->m_PUL);
}

void XStepMotor_setStepsPerRevolution_base(XStepMotor* motor, uint16_t num)
{
	if (motor != NULL)
	{
		motor->m_pulsesPerRevolution = num;
	}
}

void XStepMotor_setSpeed_base(XStepMotor* motor, double speed)
{
	if (motor == NULL)
		return;
	motor->m_currentSpeed = speed;
	uint32_t secr = speed * motor->m_pulsesPerRevolution / 60.0;//一秒脉冲数目  频率

	XPWMDeviceBase_setFrequency_base(motor->m_PUL, secr);
	if (motor->m_PUL->m_dutyCycle == 0)
		XPWMDeviceBase_setDutyCycle_base(motor->m_PUL, 50);

}

void XStepMotor_setRevolutions_base(XStepMotor* motor, double num)
{
	if (motor != NULL)
	{
		if (num > 0.0)
		{
			XStepMotor_setDIR_base(motor, true);
			motor->m_setPulses = num * motor->m_pulsesPerRevolution;
		}
		else if (num < 0.0)
		{
			XStepMotor_setDIR_base(motor, false);
			motor->m_setPulses = -num * motor->m_pulsesPerRevolution;
		}
		//printf("脉冲数:%d\n",(int)motor->m_setPulses);
	}
}

double XStepMotor_getRevolutions(XStepMotor* motor)
{
	if (motor == NULL)
		return 0.0;
	return ((double)motor->m_directionPulses) / motor->m_pulsesPerRevolution;
}

void XStepMotor_poll(XStepMotor* motor)
{
	if (motor == NULL)
		return;
	XSwitchDeviceBase_poll_base(motor->m_DIR);
	XSwitchDeviceBase_poll_base(motor->m_ENA);
}

void XStepMotor_timerCallback(XStepMotor* motor)
{
	if (motor == NULL)
		return NULL;
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
