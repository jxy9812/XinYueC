#include"XIOTest.h"
#if DEMOTEST
#include"XPWMDeviceBase.h"
#include"XTimerBase.h"
#include"XPrintf.h"
static struct XPWMDeviceTimer
{
	XTimerBase* timer1;//一个周期
	XTimerBase* timer2;//电平反转
}pwmTimer;
static bool state=false;
static void TimerCallback(XTimerBase* timer)
{
	
	if (timer == pwmTimer.timer1)
	{
		state = true;
		XTimerBase_start_base(pwmTimer.timer2);
	}
	else
	{
		state = false;
		XTimerBase_stop_base(pwmTimer.timer2);
	}
	XPrintf("%s\n", state ? "高电平" : "低电平");
}
static bool XPWMDeviceOpen(XPWMDeviceBase* pwm, XIODeviceBaseMode mode)//打开IO设备
{
	XPrintf("创建定时器\n");
	//pwmTimer.timer1 = XTimer_new_Win32ThreadpoolTimer();
	//XTimer_create(pwmTimer.timer1);
	//pwmTimer.timer1->m_port.timerCallback = TimerCallback;
	//pwmTimer.timer2 = XTimer_new_Win32ThreadpoolTimer();
	//XTimer_create(pwmTimer.timer2);
	//pwmTimer.timer2->m_port.timerCallback = TimerCallback;
	//pwm->m_userData = &pwmTimer;
	
}
static void XPWMDeviceStart(XPWMDeviceBase* pwm)
{
	//((struct XPWMDeviceTimer*)(pwm->data))->timer1->m_interval = 1.0 / (pwm->m_frequency) * 1000;
	//XPrintf("启动定时器:%f\n", ((struct XPWMDeviceTimer*)(pwm->data))->timer1->interval * (pwm->m_dutyCycle) / 100.0);
	//((struct XPWMDeviceTimer*)(pwm->data))->timer2->m_interval = ((struct XPWMDeviceTimer*)(pwm->data))->timer1->m_interval * (pwm->m_dutyCycle) / 100.0;
	//XTimer_startBase(((struct XPWMDeviceTimer*)(pwm->data))->timer1);
}
//模拟测试
void XPWMDeviceTest()
{
	//XIODevice_PortFuncInit  ioPort = { 0 };
	//ioPort.open_funcPointer = XPWMDeviceOpen;
	//XPWMDevice_PortFunc     pwmPort = { 0 };
	//pwmPort.start=XPWMDeviceStart;
	//XPWMDevice_PortFuncInit port = { 0 };
	//port.parentPort = ioPort;
	//port.pwmPort = pwmPort;
	//XPWMDeviceBase* pwm = XPWMDeviceBase_create(&port);
	//XPWMDeviceBase_setFrequency_base(pwm,5);
	//XPWMDeviceBase_setDutyCycle_base(pwm, 50);
	//XIODeviceBase_open_base(pwm, XIODeviceBase_ReadWrite);
	//XPWMDeviceBase_start_base(pwm);
	while (true);
}
#endif