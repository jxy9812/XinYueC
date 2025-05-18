#include"XIOTest.h"
#if DEMOTEST
#include"XPWMDevice.h"
#include"XTimer.h"
static struct XPWMDeviceTimer
{
	XTimer* timer1;//一个周期
	XTimer* timer2;//电平反转
}pwmTimer;
static bool state=false;
static void TimerCallback(XTimer* timer)
{
	
	if (timer == pwmTimer.timer1)
	{
		state = true;
		XTimer_start(pwmTimer.timer2);
	}
	else
	{
		state = false;
		XTimer_stop(pwmTimer.timer2);
	}
	printf("%s\n", state ? "高电平" : "低电平");
}
static bool XPWMDeviceOpen(XPWMDevice* pwm, XIODeviceBase mode)//打开IO设备
{
	printf("创建定时器\n");
	pwmTimer.timer1 = XTimer_new_Win32();
	XTimer_create(pwmTimer.timer1);
	pwmTimer.timer1->m_port.timerCallback = TimerCallback;
	pwmTimer.timer2 = XTimer_new_Win32();
	XTimer_create(pwmTimer.timer2);
	pwmTimer.timer2->m_port.timerCallback = TimerCallback;
	pwm->data = &pwmTimer;
	
}
static void XPWMDeviceStart(XPWMDevice* pwm)
{
	((struct XPWMDeviceTimer*)(pwm->data))->timer1->interval = 1.0 / (pwm->m_frequency) * 1000;
	//printf("启动定时器:%f\n", ((struct XPWMDeviceTimer*)(pwm->data))->timer1->interval * (pwm->m_dutyCycle) / 100.0);
	((struct XPWMDeviceTimer*)(pwm->data))->timer2->interval = ((struct XPWMDeviceTimer*)(pwm->data))->timer1->interval * (pwm->m_dutyCycle) / 100.0;
	XTimer_start(((struct XPWMDeviceTimer*)(pwm->data))->timer1);
}
//模拟测试
void XPWMDeviceTest()
{
	XIODevice_PortFuncInit  ioPort = { 0 };
	ioPort.open_funcPointer = XPWMDeviceOpen;
	XPWMDevice_PortFunc     pwmPort = { 0 };
	pwmPort.start=XPWMDeviceStart;
	XPWMDevice_PortFuncInit port = { 0 };
	port.parentPort = ioPort;
	port.pwmPort = pwmPort;
	XPWMDevice* pwm = XPWMDevice_new(&port);
	XPWMDevice_setFrequency(pwm,5);
	XPWMDevice_setDutyCycle(pwm, 50);
	XIODevice_open(pwm, XIODeviceBase_ReadWrite);
	XPWMDevice_start(pwm);
	while (true);
}
#endif