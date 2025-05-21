#include"XPWMDevice.h"
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

void XPWMDevice_free(XPWMDevice* pwm)
{
	if (pwm!=NULL)
	{
		XMemory_free(pwm);
	}
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
}

void XPWMDevice_setFrequency(XPWMDevice* pwm, size_t f)
{
	if (pwm!=NULL)
	{
		pwm->m_frequency = f;
		if(pwm->m_isRun)
			XPWMDevice_start(pwm);
	}
}

void XPWMDevice_setDutyCycle(XPWMDevice* pwm, uint8_t d)
{
	if (pwm!=NULL&&d>=0&&d<=100)
	{
		pwm->m_dutyCycle = d;
		if (pwm->m_isRun)
			XPWMDevice_start(pwm);
	}
}

void XPWMDevice_start(XPWMDevice* pwm)
{
	if (pwm != NULL)
	{
		XPWMDevice_stop(pwm);
		//开始运行
		if (pwm->m_port.start)
		{
			pwm->m_port.start(pwm);
			pwm->m_isRun = true;
			if (pwm->m_port.runChangeCallback)
				pwm->m_port.runChangeCallback(pwm);
		}
	}
}

void XPWMDevice_stop(XPWMDevice* pwm)
{
	if (pwm != NULL)
	{
		if (pwm->m_isRun)
		{//如果运行则执行关闭
			if (pwm->m_port.stop)
			{
				pwm->m_port.stop(pwm);
				pwm->m_isRun = false;
				if (pwm->m_port.runChangeCallback)
					pwm->m_port.runChangeCallback(pwm);
			}
		}
	}
}

bool XPWMDevice_isRunning(XPWMDevice* pwm)
{
	if(pwm==NULL)
		return false;
	return pwm->m_isRun;
}

size_t XPWMDevice_getFrequency(XPWMDevice* pwm)
{
	if(pwm==NULL)
		return 0;
	return pwm->m_frequency;
}

uint8_t XPWMDevice_getDutyCycle(XPWMDevice* pwm)
{
	if (pwm == NULL)
		return 0;
	return pwm->m_dutyCycle;
}
