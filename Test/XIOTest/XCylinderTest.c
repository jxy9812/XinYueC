#include"XIOTest.h"
#if DEMOTEST
#include"XCylinder.h"
#ifdef WIN32
//Windows接口文件
#include"XModbusTest_Port.h"
#include"XSerialPort.h"
#include <windows.h>
// 告诉编译器链接 winmm.lib 库
#pragma comment(lib, "winmm.lib")
// 串口句柄
static HANDLE hSerial;
static  HANDLE hEvent;
static OVERLAPPED ov;
bool ulState=false;
bool svState= false;
// 定时器回调函数
static VOID CALLBACK TimerCallback(UINT uTimerID, UINT uMsg, DWORD_PTR dwUser, DWORD_PTR dw1, DWORD_PTR dw2)
{
	XCylinder* cy = ((XTimer*)dwUser);
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
	UINT timerId = timeSetEvent(1000, 1, TimerCallback, cy, TIME_PERIODIC);
#endif
	while (1)
	{
		XCylinder_poll(cy);
	}
}
#endif