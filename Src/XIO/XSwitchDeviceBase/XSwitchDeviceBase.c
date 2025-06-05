#include"XSwitchDeviceBase.h"
#include"XMemory.h"
#include<string.h>
#define Parent(ptr) ((XIODeviceBase*)(ptr))
#define Port(ptr)  ((XSwitchDevice_PortFunc*)(ptr->m_parent.m_port))
XSwitchDeviceBase* XSwitchDeviceBase_create(XVtable* vtable)
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
	XIODeviceBase_init(sw, vtable);
	XSwitchDeviceBase_class_init();
	if(vtable==NULL)
		XClassGetVtable(sw) = XSwitchDeviceBase_class_init();
	else 
		XClassGetVtable(sw) = vtable;
}

void XSwitchDeviceBase_setStateChangeCallback(XSwitchDeviceBase* sw, void(*callback)(XSwitchDeviceBase* io))
{
	if (sw)
		sw->m_stateChangeCallback = callback;
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
