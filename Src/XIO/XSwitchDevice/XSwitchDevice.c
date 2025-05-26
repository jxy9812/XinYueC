#include"XSwitchDevice.h"
#include"XMemory.h"
#include<string.h>
#define Parent(ptr) ((XIODevice*)(ptr))
#define Port(ptr)  ((XSwitchDevice_PortFunc*)(ptr->m_parent.m_port))
XSwitchDevice* XSwitchDevice_new(XSwitchDevice_PortFuncInit* port)
{
	if (port == NULL)
		return NULL;
	XSwitchDevice* sw = XMemory_malloc(sizeof(XSwitchDevice));
	if (sw == NULL)
		return sw;
	XSwitchDevice_init(sw,port);
	return sw;
}

void XSwitchDevice_init(XSwitchDevice* sw, XSwitchDevice_PortFuncInit* port)
{
	if (sw == NULL || port == NULL)
		return ;
	XIODevice_init(&(sw->m_parent), &(port->parentPort));
	//开始初始化
	memset(&(sw->state), 0, sizeof(XSwitchDevice) - sizeof(XIODevice) - sizeof(XSwitchDevice_PortFunc));
	//绑定函数指针
	memcpy(&(sw->m_port), &(port->SwitchPort), sizeof(XSwitchDevice_PortFunc));
	if (sw->m_parent.m_port.poll_funcPointer == NULL)
		sw->m_parent.m_port.poll_funcPointer = XSwitchDevice_pollDefaultMethod;
	XSwitchDevice_class_init();
	XClassGetVtable(sw) = XSwitchDeviceVtable;
}

void XSwitchDevice_pollDefaultMethod(XSwitchDevice* sw)
{
	if (sw == NULL)
		return;
	if(sw->m_parent.m_mode & XIODeviceBase_ReadOnly)
	{
		//扫描保存状态
		bool state = sw->buffer;
		//读取当前状态
		XIODevice_read(sw, &state, 1);
		//printf("state:%s\n", state ? "true" : "false");
		if (state != sw->buffer)
		{
			sw->buffer = state;
			return;
		}
		if (sw->buffer != sw->state)
		//if (state != sw->state)
		{
			sw->state = sw->buffer;
			if (sw->m_port.stateChangeCallback)//判断当前是否有状态改变回调函数
				sw->m_port.stateChangeCallback(sw);
		}
	}
}

void XSwitchDevice_setState(XSwitchDevice* sw, bool state)
{
	if (ISNULL(sw, ""))
		return ;
	XClassGetVirtualFunc(sw, EXSwitchDevice_SetState, void(*)(XSwitchDevice*,bool))(sw, state);
}

bool XSwitchDevice_getState(XSwitchDevice* sw)
{
	if (ISNULL(sw, ""))
		return false;
	return XClassGetVirtualFunc(sw, EXSwitchDevice_GetState, bool(*)(XSwitchDevice*))(sw);
}
