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
	XVTABLE_CREAT_DEFAULT
	//虚函数表初始化
#if VTABLE_ISSTACK
	XVTABLE_STACK_INIT_DEFAULT(XSWITCHDEVICEBASE_VTABLE_SIZE)
#else
	XVTABLE_HEAP_INIT_DEFAULT
#endif
	//继承类
	XVTABLE_INHERIT_DEFAULT(XIODeviceBase_class_init());
	void* table[] = { VXSwitchDevice_setState,VXSwitchDevice_getState };
	//追加虚函数
	XVTABLE_ADD_FUNC_LIST_DEFAULT(table);
	//重载
	XVTABLE_OVERLOAD_DEFAULT(EXObject_Poll,VXIODevice_poll);
#if SHOWCONTAINERSIZE
	printf("XSwitchDeviceBase size:%d\n", XVtable_size(XVTABLE_DEFAULT));
#endif
	return XVTABLE_DEFAULT;
}

void VXSwitchDevice_setState(XSwitchDeviceBase* sw, bool state)
{
	if (sw && ((sw->m_class.m_mode) & XIODeviceBase_WriteOnly))
	{
		if (sw->m_state != state)
		{
			bool trigger;
			switch (sw->m_triggerMode)
			{
			case XSwitchDeviceBase_Trigger_High:trigger= state; break;
			case XSwitchDeviceBase_Trigger_Low:trigger= !state; break;
			default:
				ISNULL(0, "发生错误"); break;
				break;
			}
			XIODeviceBase_write_base(sw, &trigger, sizeof(bool));
			sw->m_state = state;
			
			if (sw->m_stateChangeCallback)
			{
				XObject_postEvent(sw, XEventFunc_create(sw->m_stateChangeCallback, sw), XEVENT_PRIORITY_NORMAL);
			}
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
	if (sw->m_class.m_mode & XIODeviceBase_ReadOnly)
	{
		//扫描保存状态
		//bool state = sw->m_buffer;
		
		//读取当前电平状态
		bool trigger;
		XIODeviceBase_read_base(sw, &trigger, 1);
		bool state;
		switch (sw->m_triggerMode)
		{
			case XSwitchDeviceBase_Trigger_High:state = trigger; break;
			case XSwitchDeviceBase_Trigger_Low:state = !trigger; break;
			default:
				ISNULL(0,"发生错误"); break;
				break;
		}
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
			if (sw->m_stateChangeCallback)
			{
				XObject_postEvent(sw, XEventFunc_create(sw->m_stateChangeCallback, sw), XEVENT_PRIORITY_NORMAL);
			}
		}
	}
}
