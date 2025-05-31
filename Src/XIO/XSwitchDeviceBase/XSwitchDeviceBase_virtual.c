#include "XSwitchDeviceBase.h"
#include "XMemory.h"
#include <string.h>
#include <assert.h>
//声明 
static void VXSwitchDevice_setState(XSwitchDeviceBase* sw, bool state);
static bool VXSwitchDevice_getState(XSwitchDeviceBase* sw);
static void VXIODevice_poll(XSwitchDeviceBase* sw);
XVtable* XSwitchDeviceBase_class_init()
{
	static XVtable* XClassVtable = NULL;
	if (XClassVtable)
		return XClassVtable;
	//虚函数表初始化
#if VTABLE_ISSTACK
	XVTABLE_STACK_DEFINITION(XSWITCHDEVICEBASE_VTABLE_SIZE)
	XVTABLE_STACK_INIT(XClassVtable)
#else
	XVTABLE_HEAP_INIT(XClassVtable)
#endif
	//继承的函数
	XVtable_append_vtable(XClassVtable, XIODeviceBase_class_init());
	void* table[] = { VXSwitchDevice_setState,VXSwitchDevice_getState };
	//追加的函数
	XVtable_append_array(XClassVtable, table, sizeof(table) / sizeof(table[0]));
	XVtable_At(XClassVtable, EXIODeviceBase_Poll) = VXIODevice_poll;
#if SHOWCONTAINERSIZE
	printf("XSwitchDeviceBase size:%d\n", XVtable_size(XClassVtable));
#endif
	return XClassVtable;
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
