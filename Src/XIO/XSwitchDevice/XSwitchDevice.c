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

void XSwitchDevice_free(XSwitchDevice* sw)
{
	if (sw)
	{
		XMemory_free(sw);
	}
}

void XSwitchDevice_init(XSwitchDevice* sw, XSwitchDevice_PortFuncInit* port)
{
	if (sw == NULL || port == NULL)
		return NULL;
	XIODevice_init(&(sw->m_parent), &(port->parentPort));
	//开始初始化
	memset(&(sw->state), 0, sizeof(XSwitchDevice) - sizeof(XIODevice) - sizeof(XSwitchDevice_PortFunc));
	//绑定函数指针
	memcpy(&(sw->m_port), &(port->SwitchPort), sizeof(XSwitchDevice_PortFunc));
	if (sw->m_parent.m_port.poll_funcPointer == NULL)
		sw->m_parent.m_port.poll_funcPointer = XSwitchDevice_pollDefaultMethod;
}

void XSwitchDevice_pollDefaultMethod(XSwitchDevice* sw)
{
	if (sw == NULL)
		return;
	if(sw->m_parent.m_mode & XIODeviceBase_ReadOnly)
	{
		//扫描保存状态
		bool state = sw->state;
		//读取当前状态
		XIODevice_read(sw, &state, 1);
		//printf("state:%s\n", state ? "true" : "false");
		if (state != sw->state)
		{
			sw->state = state;
			if (sw->m_port.stateChangeCallback)//判断当前是否有状态改变回调函数
				sw->m_port.stateChangeCallback(sw);
		}
	}
}

void XSwitchDevice_setState(XSwitchDevice* sw, bool state)
{
	//printf("A\n");
	if (sw && ((sw->m_parent.m_mode) & XIODeviceBase_WriteOnly))
	{
		XIODevice_write(sw, &state, sizeof(bool));
		//if (sw->m_parent.m_port.poll_funcPointer == NULL)
		if ((sw->state != state) && sw->m_port.stateChangeCallback)
		{
			sw->state = state;
			sw->m_port.stateChangeCallback(sw);
		}
	}
}

bool XSwitchDevice_getState(XSwitchDevice* sw)
{
	if(sw)
		return sw->state;
	return false;
}
