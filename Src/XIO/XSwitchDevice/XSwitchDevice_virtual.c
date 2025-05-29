#include "XSwitchDevice.h"
#include "XMemory.h"
#include <string.h>
#include <assert.h>
//声明 
void VXSwitchDevice_setState(XSwitchDeviceBase* sw, bool state);
bool VXSwitchDevice_getState(XSwitchDeviceBase* sw);
XVtable* XSwitchDeviceBaseVtable = NULL;
#if VTABLE_ISSTACK
static XVtable vtable;//虚函数类
static void* vtable_data[XSWITCHDEVICEBASE_VTABLE_SIZE];//虚函数数据
#endif
void XSwitchDeviceBase_class_init()
{
	//仅初始化一次
	if (XSwitchDeviceBaseVtable)
		return;
#if !VTABLE_ISSTACK
	XSwitchDeviceBaseVtable = XVtable_new();
#else
	XSwitchDeviceBaseVtable = &vtable;
	XVtable_init_stack(&vtable, vtable_data, XSWITCHDEVICEBASE_VTABLE_SIZE);
#endif
	//继承的函数
	XVtable_append_vtable(XSwitchDeviceBaseVtable, XIODeviceBaseVtable);
	void* table[] = { VXSwitchDevice_setState,VXSwitchDevice_getState };
	//追加的函数
	XVtable_append_array(XSwitchDeviceBaseVtable, table, sizeof(table) / sizeof(table[0]));
#if SHOWCONTAINERSIZE
	printf("XSwitchDeviceBase size:%d\n", XVtable_size(XSwitchDeviceBaseVtable));
#endif
}

void VXSwitchDevice_setState(XSwitchDeviceBase* sw, bool state)
{
	if (sw && ((sw->m_parent.m_mode) & XIODeviceBase_WriteOnly))
	{
		XIODevice_write_base(sw, &state, sizeof(bool));
		//if (sw->m_parent.m_port.poll_funcPointer == NULL)
		if ((sw->m_state != state) && sw->m_stateChangeCallback)
		{
			sw->m_state = state;
			sw->m_stateChangeCallback(sw);
		}
	}
}

bool VXSwitchDevice_getState(XSwitchDeviceBase* sw)
{
	if (sw)
		return sw->m_state;
	return false;
}
