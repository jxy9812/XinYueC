#include "XPwmDevice.h"
#include "XMemory.h"
#include <string.h>
#include <assert.h>
//声明
void VXPWMDevice_setFrequency(XPWMDeviceBase* pwm, size_t f);
void VXPWMDevice_setDutyCycle(XPWMDeviceBase* pwm, uint8_t d);
void VXPWMDevice_start(XPWMDeviceBase* pwm);
void VXPWMDevice_stop(XPWMDeviceBase* pwm);
bool VXPWMDevice_isRunning(XPWMDeviceBase* pwm);
size_t VXPWMDevice_getFrequency(XPWMDeviceBase* pwm);
uint8_t VXPWMDevice_getDutyCycle(XPWMDeviceBase* pwm);
XVtable* XPWMDeviceVtable = NULL;
#if VTABLE_ISSTACK
static XVtable vtable;//虚函数类
static void* vtable_data[XPWMDEVICE_VTABLE_SIZE];//虚函数数据
#endif
void XPWMDeviceBase_class_init()
{
	//仅初始化一次
	if (XPWMDeviceVtable)
		return;
#if !VTABLE_ISSTACK
	XPWMDeviceVtable = XVtable_new();
#else
	XPWMDeviceVtable = &vtable;
	XVtable_Init_Stack(&vtable, vtable_data);
#endif
	//继承的函数
	XVtable_append_vtable(XPWMDeviceVtable, XIODeviceVtable);
	void* table[] = { 
		VXPWMDevice_setFrequency,VXPWMDevice_setDutyCycle,
		VXPWMDevice_start,VXPWMDevice_stop,
		VXPWMDevice_isRunning,VXPWMDevice_getFrequency,
		VXPWMDevice_getDutyCycle };
	//追加的函数
	XVtable_append_array(XPWMDeviceVtable, table, sizeof(table) / sizeof(table[0]));
#if SHOWCONTAINERSIZE
	printf("XPWMDeviceBase size:%d\n", XVtable_size(XPWMDeviceVtable));
#endif
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
			pwm->m_runChangeCallback(pwm);
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
				pwm->m_runChangeCallback(pwm);
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
