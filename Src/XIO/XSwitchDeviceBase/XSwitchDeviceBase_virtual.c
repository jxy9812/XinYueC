#include "XSwitchDeviceBase.h"
#include "XMemory.h"
#include <string.h>
#include <assert.h>
//声明 
static void VXSwitchDevice_setState(XSwitchDeviceBase* sw, bool state);
static bool VXSwitchDevice_getState(XSwitchDeviceBase* sw);
static void VXIODevice_poll(XSwitchDeviceBase* sw);
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
	XVtable_At(XSwitchDeviceBaseVtable, EXIODeviceBase_Poll) = VXIODevice_poll;
#if SHOWCONTAINERSIZE
	printf("XSwitchDeviceBase size:%d\n", XVtable_size(XSwitchDeviceBaseVtable));
#endif
}

void VXSwitchDevice_setState(XSwitchDeviceBase* sw, bool state)
{
	if (sw && ((sw->m_parent.m_mode) & XIODeviceBase_WriteOnly))
	{
		XIODeviceBase_write_base(sw, &state, sizeof(bool));
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

void VXIODevice_poll(XSwitchDeviceBase* sw)
{
	if (sw->m_parent.m_mode & XIODeviceBase_ReadOnly)
	{
		//扫描保存状态
		bool state = sw->m_buffer;
		//读取当前状态
		XIODeviceBase_read_base(sw, &state, 1);
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
