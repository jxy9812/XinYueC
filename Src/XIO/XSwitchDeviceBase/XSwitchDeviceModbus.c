#include "XSwitchDeviceModbus.h"
#include "XModbusDigitalSwitch.h"
#include "XMemory.h"
#include <string.h>
static bool VXIODevice_open(XSwitchDeviceModbus* sw, XIODeviceBaseMode mode);
static size_t VXIODevice_write(XSwitchDeviceModbus* sw, const char* data, size_t maxSize);//写入
static size_t VXIODevice_read(XSwitchDeviceModbus* sw, char* data, size_t maxSize);//读取
static void VXIODevice_close(XSwitchDeviceModbus* sw);
static void VXIODevice_poll(XSwitchDeviceModbus* sw);
XVtable* XSwitchDeviceModbus_class_init()
{
	XVTABLE_CREAT_DEFAULT
		//虚函数表初始化
#if VTABLE_ISSTACK
		XVTABLE_STACK_INIT_DEFAULT(XSWITCHDEVICEMODBUS_VTABLE_SIZE)
#else
		XVTABLE_HEAP_INIT_DEFAULT
#endif
		//继承类
		XVTABLE_INHERIT_DEFAULT(XSwitchDeviceBase_class_init());
	// void* table[] = { VXSwitchDevice_setState,VXSwitchDevice_getState };
	// //追加虚函数
	// XVTABLE_ADD_FUNC_LIST_DEFAULT(table);
	//重载
	XVTABLE_OVERLOAD_DEFAULT(EXIODeviceBase_Open, VXIODevice_open);
	XVTABLE_OVERLOAD_DEFAULT(EXIODeviceBase_Write, VXIODevice_write);
	XVTABLE_OVERLOAD_DEFAULT(EXIODeviceBase_Read, VXIODevice_read);
	XVTABLE_OVERLOAD_DEFAULT(EXIODeviceBase_Close, VXIODevice_close);
	XVTABLE_OVERLOAD_DEFAULT(EXObject_Poll, VXIODevice_poll);
#if SHOWCONTAINERSIZE
	printf("XSwitchDeviceModbus size:%d\n", XVtable_size(XVTABLE_DEFAULT));
#endif
	return XVTABLE_DEFAULT;
}

XSwitchDeviceModbus* XSwitchDeviceModbus_create(XModbusDigitalSwitch* ds, uint16_t portNum)
{
	if (ds == NULL)
		return NULL;
	XSwitchDeviceModbus* sw = XMemory_malloc(sizeof(XSwitchDeviceModbus));
	if (sw == NULL)
		return NULL;
	XSwitchDeviceModbus_init(sw);
	XClassGetVtable(sw) = XSwitchDeviceModbus_class_init();
	sw->m_ds = ds;
	sw->m_portNum = portNum;
	return sw;
}

void XSwitchDeviceModbus_init(XSwitchDeviceModbus* sw)
{
	if (sw == NULL)
		return;
	memset(((XSwitchDeviceBase*)sw) + 1, 0, sizeof(XSwitchDeviceModbus) - sizeof(XSwitchDeviceBase));
	XSwitchDeviceBase_init(sw);
	XClassGetVtable(sw) = XSwitchDeviceModbus_class_init();
}

bool VXIODevice_open(XSwitchDeviceModbus* sw, XIODeviceBaseMode mode)
{
	if (XIODeviceBase_isOpen_base(sw))
		return true;//已经打开了
	if (!(mode & XIODeviceBase_ReadOnly || mode & XIODeviceBase_WriteOnly))
		return false;
	if (!XModbusDigitalSwitch_XSwitchDeviceModbusOpen(sw->m_ds, sw, mode, sw->m_portNum))
		return false;
	((XIODeviceBase*)sw)->m_mode = mode;
	return true;
}

size_t VXIODevice_write(XSwitchDeviceModbus* sw, const char* data, size_t maxSize)
{
	if (((XIODeviceBase*)sw)->m_mode & XIODeviceBase_WriteOnly)
	{
		if (XModbusDigitalSwitch_writeOut(sw->m_ds, sw->m_portNum, *data))
			return maxSize;
	}
	return 0;
}

size_t VXIODevice_read(XSwitchDeviceModbus* sw, char* data, size_t maxSize)
{
	if (((XIODeviceBase*)sw)->m_mode & XIODeviceBase_ReadOnly)
	{
		if (XModbusDigitalSwitch_readIn(sw->m_ds, sw->m_portNum, data))
			return maxSize;
	}
	else if (((XIODeviceBase*)sw)->m_mode & XIODeviceBase_WriteOnly)
	{
		if (XModbusDigitalSwitch_readOut(sw->m_ds, sw->m_portNum, data))
			return maxSize;
	}
	return 0;
}

void VXIODevice_close(XSwitchDeviceModbus* sw)
{
	if (!XIODeviceBase_isOpen_base(sw))
		return;
	XModbusDigitalSwitch_XSwitchDeviceModbusClose(sw->m_ds,sw, ((XIODeviceBase*)sw)->m_mode,sw->m_portNum);
	((XIODeviceBase*)sw)->m_mode = XIODeviceBase_NotOpen;
}

void VXIODevice_poll(XSwitchDeviceModbus* sw)
{
	XSwitchDeviceBase* base = sw;
	if (base->m_parent.m_mode & XIODeviceBase_ReadOnly)
	{
		//读取当前电平状态
		bool trigger;
		XIODeviceBase_read_base(sw, &trigger, 1);
		bool state;
		switch (base->m_triggerMode)
		{
		case XSwitchDeviceBase_Trigger_High:state = trigger; break;
		case XSwitchDeviceBase_Trigger_Low:state = !trigger; break;
		default:
			ISNULL(0, "发生错误"); break;
			break;
		}
		if (state != base->m_state)
		{
			base->m_state = state;
			if (base->m_stateChangeCallback)
			{
				XObject_postEvent(sw, XEventFunc_create(sw, base->m_stateChangeCallback, sw), XEVENT_PRIORITY_NORMAL);
			}
		}
	}
}
