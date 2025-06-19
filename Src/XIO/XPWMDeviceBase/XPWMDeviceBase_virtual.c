#include "XPWMDeviceBase.h"
#include "XMemory.h"
#include <string.h>
#include <assert.h>
//声明
static void VXPWMDevice_setFrequency(XPWMDeviceBase* pwm, size_t f);
static void VXPWMDevice_setDutyCycle(XPWMDeviceBase* pwm, uint8_t d);
static void VXPWMDevice_start(XPWMDeviceBase* pwm);
static void VXPWMDevice_stop(XPWMDeviceBase* pwm);
static bool VXPWMDevice_isRunning(XPWMDeviceBase* pwm);
static size_t VXPWMDevice_getFrequency(XPWMDeviceBase* pwm);
static uint8_t VXPWMDevice_getDutyCycle(XPWMDeviceBase* pwm);
XVtable* XPWMDeviceBase_class_init()
{
	XVTABLE_CREAT_DEFAULT
		//虚函数表初始化
#if VTABLE_ISSTACK
	XVTABLE_STACK_INIT_DEFAULT(XPWMDEVICE_VTABLE_SIZE)
#else
	XVTABLE_HEAP_INIT_DEFAULT
#endif
	//继承类
	XVTABLE_INHERIT_DEFAULT(XIODeviceBase_class_init());
	void* table[] = { 
		VXPWMDevice_setFrequency,VXPWMDevice_setDutyCycle,
		VXPWMDevice_start,VXPWMDevice_stop,
		VXPWMDevice_isRunning,VXPWMDevice_getFrequency,
		VXPWMDevice_getDutyCycle };
	//追加虚函数
	XVTABLE_ADD_FUNC_LIST_DEFAULT(table);
#if SHOWCONTAINERSIZE
	printf("XPWMDeviceBase size:%d\n", XVtable_size(XVTABLE_DEFAULT));
#endif
	return XVTABLE_DEFAULT;
}

void VXPWMDevice_setFrequency(XPWMDeviceBase* pwm, size_t f)
{
	if (pwm != NULL)
	{
		pwm->m_frequency = f;
		if (pwm->m_isRun)
			XPWMDeviceBase_start_base(pwm);
	}
}

void VXPWMDevice_setDutyCycle(XPWMDeviceBase* pwm, uint8_t d)
{
	if (pwm != NULL && d >= 0 && d <= 100)
	{
		pwm->m_dutyCycle = d;
		if (pwm->m_isRun)
			XPWMDeviceBase_start_base(pwm);
	}
}

void VXPWMDevice_start(XPWMDeviceBase* pwm)
{
	if (pwm != NULL)
	{
		XPWMDeviceBase_stop_base(pwm);
		/**************************************************/
		ISNULL(0,"请重载这个函数,这是模板");
		/**************************************************/
		pwm->m_isRun = true;
		if (pwm->m_runChangeCallback)
		{
			if (XIODeviceBase_CallbackQueue(pwm))
				XIODeviceBase_callbackQueue_push(pwm, pwm->m_runChangeCallback);
			else
				pwm->m_runChangeCallback(pwm);
		}
	}
}

void VXPWMDevice_stop(XPWMDeviceBase* pwm)
{
	if (pwm != NULL)
	{
		if (pwm->m_isRun)
		{//如果运行则执行关闭
			/**************************************************/
			ISNULL(0, "请重载这个函数,这是模板");
			/**************************************************/
			pwm->m_isRun = false;
			if (pwm->m_runChangeCallback)
			{
				if (XIODeviceBase_CallbackQueue(pwm))
				{
					XIODeviceBase_callbackQueue_push(pwm, pwm->m_runChangeCallback);
				}
				else
				{
					pwm->m_runChangeCallback(pwm);
				}
			}
		}
	}
}

bool VXPWMDevice_isRunning(XPWMDeviceBase* pwm)
{
	if (pwm == NULL)
		return false;
	return pwm->m_isRun;
}

size_t VXPWMDevice_getFrequency(XPWMDeviceBase* pwm)
{
	if (pwm == NULL)
		return 0;
	return pwm->m_frequency;
}

uint8_t VXPWMDevice_getDutyCycle(XPWMDeviceBase* pwm)
{
	if (pwm == NULL)
		return 0;
	return pwm->m_dutyCycle;
}
