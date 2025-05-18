#include "XStepMotor.h"
#include <string.h>
#include <stdio.h>
#include "XMap.h"
#include "XEquality.h"
#include "XLess.h"
//#include "FreeRTOS.h"

XStepMotor* XStepMotor_new(XStepMotor_PortFuncInit* port)
{
	if (port == NULL)
		return NULL;
	XStepMotor* motor= XMemory_malloc(sizeof(XSwitchDevice));
	if (motor == NULL)
		return motor;
	XStepMotor_init(motor,port);
	return motor;
}

void XStepMotor_init(XStepMotor* motor, XStepMotor_PortFuncInit* port)
{
	if (motor == NULL || port == NULL)
		return NULL;
	//XIODevice_init(&(motor->m_parent), &(port->parentPort));
	//开始初始化
	memset(motor, 0, sizeof(XStepMotor) /*- sizeof(XIODevice) */- sizeof(XStepMotor_PortFunc));
	motor->m_PUL = XPWMDevice_new(&(port->PUL));
	motor->m_ENA = XPWMDevice_new(&(port->ENA));
	motor->m_DIR = XPWMDevice_new(&(port->DIR));
	//绑定函数指针
	memcpy(&(motor->m_port), &(port->StepMotorPort), sizeof(XStepMotor_PortFunc));
}

void XStepMotor_setENA(XStepMotor* motor, bool open)
{
	if (motor == NULL)
		return;
	XSwitchDevice_setState(motor->m_ENA, open);
}

void XStepMotor_setDIR(XStepMotor* motor, bool corotation)
{
	if (motor == NULL)
		return;
	XSwitchDevice_setState(motor->m_DIR, corotation);
}

void XStepMotor_start(XStepMotor* motor)
{
	if (motor == NULL)
		return;
	motor->m_currentSpeed = 0;//清空转速
	motor->m_currentPulses = 0;//清空脉冲计数
	//motor->start = true;
	//开启pwm输出
	XPWMDevice_start(motor->m_PUL);
}

void XStepMotor_stop(XStepMotor* motor)
{
	if (motor == NULL)
		return;
	//关闭pwm输出
	XPWMDevice_stop(motor->m_PUL);
}

void XStepMotor_setPulsesPerRevolution(XStepMotor* motor, uint16_t num)
{
	if (motor != NULL)
	{
		motor->m_pulsesPerRevolution = num;
	}
}

void XStepMotor_setSpeed(XStepMotor* motor, uint16_t speed)
{
	if (motor == NULL)
		return;
	motor->m_currentSpeed = speed;
	uint32_t secr = speed * motor->m_pulsesPerRevolution / 60;//一秒脉冲数目  频率
	XPWMDevice_setFrequency(motor->m_PUL,secr);

}

void XStepMotor_setNumRotations(XStepMotor* motor, double num)
{
	if (motor != NULL)
	{
		motor->m_setPulses = num * motor->m_pulsesPerRevolution;
	}
}

double XStepMotor_numRotations(XStepMotor* motor)
{
	if (motor == NULL)
		return 0.0;
	return motor->m_directionPulses / motor->m_pulsesPerRevolution;
}

void XStepMotor_poll(XStepMotor* motor)
{
	if (motor == NULL)
		return;
	XSwitchDevice_poll(motor->m_DIR);
	XSwitchDevice_poll(motor->m_ENA);
}

void XStepMotor_timerCallback(XStepMotor* motor)
{
	if (motor==NULL)
		return NULL;
	//累计脉冲数
	++motor->m_currentPulses;
	//计算距离
	if (XSwitchDevice_getState(motor->m_DIR))
		++motor->m_directionPulses;
	else
		--motor->m_directionPulses;

	if (motor->m_setPulses != 0 && motor->m_setPulses == motor->m_currentPulses)
	{//达到设定脉冲数了
		motor->m_setPulses = 0;
		XPWMDevice_stop(motor->m_PUL);
	}
}
