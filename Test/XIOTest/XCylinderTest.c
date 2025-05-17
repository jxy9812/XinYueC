#include"XIOTest.h"
#if DEMOTEST
#include"XCylinder.h"
#include"XTimer.h"
#ifdef WIN32
bool ulState=false;
bool svState= false;
//定时器回调函数
static void XTimer_Callback(XTimer* timer)
{
	ulState = !ulState;
}
//写入继电器状态
static size_t setSVState(XIODevice* io, const char* data, size_t size)
{
	svState = *((bool*)data);
	//printf("写入数据%s\n", svState ? "true" : "false");
	return size;
}
//获取继电器状态
static size_t getSVState(XIODevice* io, char* data, size_t size)
{
	//printf("获取数据\n");
	*((bool*)data)= svState;
	return size;
}
//继电器状态改变时
static void svStateChangeCallback(XSwitchDevice* io)
{
	printf("继电器状态改变了:%s\n", XSwitchDevice_getState(io)?"true":"false");
}
//获取上限位状态
static size_t getULState(XIODevice* io, char* data, size_t size)
{
	*((bool*)data) = ulState;
	return 1;
}
//上限位状态改变时
static void ulStateChangeCallback(XSwitchDevice* io)
{
	printf("上限位状态改变了:%s\n", XSwitchDevice_getState(io) ? "true" : "false");
	XSwitchDevice_setState((&((XCylinder*)(io->m_parent.device))->m_sv), !svState);
	
}
#endif

void XCylinderTest()
{


	XSwitchDevice_PortFuncInit sv = {0};
	{
		sv.parentPort.writeData_funcPointer = setSVState;
		sv.parentPort.readData_funcPointer = getSVState;
		sv.SwitchPort.stateChangeCallback = svStateChangeCallback;
	}
	
	XSwitchDevice_PortFuncInit ul = { 0 };
	{
		ul.parentPort.readData_funcPointer = getULState;
		ul.SwitchPort.stateChangeCallback = ulStateChangeCallback;
	}
	XSwitchDevice_PortFuncInit dl = { 0 };

	XCylinder_PortInit init = { sv,ul,dl };
	XCylinder* cy= XCylinder_new(&init);

	//定时器模拟测试
#ifdef WIN32
	XTimer* timer = XTimer_newWin32();
	timer->m_port.timerCallback = XTimer_Callback;
	XTimer_setInterval(timer,1000);
	XTimer_start(timer);
#endif
	while (1)
	{
		XCylinder_poll(cy);
	}
}
#endif