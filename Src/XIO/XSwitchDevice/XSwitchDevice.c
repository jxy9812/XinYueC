#include"XSwitchDevice.h"
#include"XMemory.h"
#include<string.h>
#define Parent(ptr) ((XIODeviceBase*)(ptr))
#define Port(ptr)  ((XSwitchDevice_PortFunc*)(ptr->m_parent.m_port))
XSwitchDeviceBase* XSwitchDeviceBase_new(XVtable* vtable)
{
	XSwitchDeviceBase* sw = XMemory_malloc(sizeof(XSwitchDeviceBase));
	//开始初始化
	memset(sw, 0, sizeof(XSwitchDeviceBase));
	if (sw == NULL)
		return NULL;
	XSwitchDeviceBase_init(sw, vtable);
	return sw;
}

void XSwitchDeviceBase_init(XSwitchDeviceBase* sw, XVtable* vtable)
{
	if (sw == NULL)
		return ;
	memset(((XIODeviceBase*)sw) + 1, 0, sizeof(XSwitchDeviceBase) - sizeof(XIODeviceBase));
	XIODevice_init(sw, vtable);
	XSwitchDeviceBase_class_init();
	if(vtable==NULL)
		XClassGetVtable(sw) = XSwitchDeviceBaseVtable;
	else 
		XClassGetVtable(sw) = vtable;
	if (XVtable_At(XClassGetVtable(sw), EXIODeviceBase_Poll) == NULL)
		XVtable_At(XClassGetVtable(sw), EXIODeviceBase_Poll) = XSwitchDeviceBase_pollDefaultMethod;
}

void XSwitchDeviceBase_setStateChangeCallback(XSwitchDeviceBase* sw, void(*callback)(XSwitchDeviceBase* io))
{
	if (sw)
		sw->m_stateChangeCallback = callback;
}

void XSwitchDeviceBase_pollDefaultMethod(XSwitchDeviceBase* sw)
{
	if (sw == NULL)
		return;
	if(sw->m_parent.m_mode & XIODeviceBase_ReadOnly)
	{
		//扫描保存状态
		bool state = sw->m_buffer;
		//读取当前状态
		XIODevice_read_base(sw, &state, 1);
		//printf("m_state:%s\n", m_state ? "true" : "false");
		if (state != sw->m_buffer)
		{
			sw->m_buffer = state;
			return;
		}
		if (sw->m_buffer != sw->m_state)
		//if (m_state != sw->m_state)
		{
			sw->m_state = sw->m_buffer;
			if (sw->m_stateChangeCallback)//判断当前是否有状态改变回调函数
				sw->m_stateChangeCallback(sw);
		}
	}
}

void XSwitchDeviceBase_setState_base(XSwitchDeviceBase* sw, bool state)
{
	if (ISNULL(sw, "") || ISNULL(XClassGetVtable(sw), ""))
		return ;
	XClassGetVirtualFunc(sw, EXSwitchDeviceBase_SetState, void(*)(XSwitchDeviceBase*,bool))(sw, state);
}

bool XSwitchDeviceBase_getState_base(XSwitchDeviceBase* sw)
{
	if (ISNULL(sw, "") || ISNULL(XClassGetVtable(sw), ""))
		return false;
	return XClassGetVirtualFunc(sw, EXSwitchDeviceBase_GetState, bool(*)(XSwitchDeviceBase*))(sw);
}
