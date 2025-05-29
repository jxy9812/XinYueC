#include "XSwitchDevice.h"
#include "XMemory.h"
#include <string.h>
#include <assert.h>
//声明 
void VXSwitchDevice_setState(XSwitchDevice* sw, bool state);
bool VXSwitchDevice_getState(XSwitchDevice* sw);
XVtable* XSwitchDeviceVtable = NULL;
#if VTABLE_ISSTACK
static XVtable vtable;//虚函数类
static void* vtable_data[XSWITCHDEVICE_VTABLE_SIZE];//虚函数数据
#endif
void XSwitchDevice_class_init()
{
	//仅初始化一次
	if (XSwitchDeviceVtable)
		return;
#if !VTABLE_ISSTACK
	XSwitchDeviceVtable = XVtable_new();
#else
	XSwitchDeviceVtable = &vtable;
	XVtable_init_stack(&vtable, vtable_data, sizeof(vtable_data) / sizeof(vtable_data[0]));
#endif
	//继承的函数
	XVtable_append_vtable(XSwitchDeviceVtable, XIODeviceVtable);
	void* table[] = { VXSwitchDevice_setState,VXSwitchDevice_getState };
	//追加的函数
	XVtable_append_array(XSwitchDeviceVtable, table, sizeof(table) / sizeof(table[0]));
#if SHOWCONTAINERSIZE
	printf("XSwitchDevice size:%d\n", XVtable_size(XSwitchDeviceVtable));
#endif
}

void VXSwitchDevice_setState(XSwitchDevice* sw, bool state)
{
	if (sw && ((sw->m_parent.m_mode) & XIODeviceBase_WriteOnly))
	{
		XIODevice_write_base(sw, &state, sizeof(bool));
		//if (sw->m_parent.m_port.poll_funcPointer == NULL)
		if ((sw->state != state) && sw->m_port.stateChangeCallback)
		{
			sw->state = state;
			sw->m_port.stateChangeCallback(sw);
		}
	}
}

bool VXSwitchDevice_getState(XSwitchDevice* sw)
{
	if (sw)
		return sw->state;
	return false;
}
