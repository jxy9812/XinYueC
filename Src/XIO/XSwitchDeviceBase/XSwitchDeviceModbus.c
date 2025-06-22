#include "XSwitchDeviceModbus.h"
#include "XMemory.h"
#include <string.h>
static bool VXIODevice_open(XSwitchDeviceModbus* sw, XIODeviceBaseMode mode);
static size_t VXIODevice_write(XSwitchDeviceModbus* sw, const char* data, size_t maxSize);//写入
static size_t VXIODevice_read(XSwitchDeviceModbus* sw, char* data, size_t maxSize);//读取
static void VXIODevice_close(XSwitchDeviceModbus* sw);
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
	/*XVTABLE_OVERLOAD_DEFAULT(EXIODeviceBase_Open, VXIODevice_open);
	XVTABLE_OVERLOAD_DEFAULT(EXIODeviceBase_Write, VXIODevice_write);
	XVTABLE_OVERLOAD_DEFAULT(EXIODeviceBase_Read, VXIODevice_read);
	XVTABLE_OVERLOAD_DEFAULT(EXIODeviceBase_Close, VXIODevice_close);*/
#if SHOWCONTAINERSIZE
	printf("XSwitchDeviceModbus size:%d\n", XVtable_size(XVTABLE_DEFAULT));
#endif
	return XVTABLE_DEFAULT;
}

XSwitchDeviceModbus* XSwitchDeviceModbus_create(XModbus* modbus)
{
	if (modbus == NULL)
		return NULL;
	XSwitchDeviceModbus* sw = XMemory_malloc(sizeof(XSwitchDeviceModbus));
	if (sw == NULL)
		return NULL;
	XSwitchDeviceModbus_init(sw);
	sw->m_modbus = modbus;
	return sw;
}

void XSwitchDeviceModbus_init(XSwitchDeviceModbus* sw)
{
	if (sw == NULL)
		return;
	memset(((XSwitchDeviceBase*)sw) + 1, 0, sizeof(XSwitchDeviceModbus) - sizeof(XSwitchDeviceBase));
	XSwitchDeviceBase_init(sw, NULL);
	XClassGetVtable(sw) = XSwitchDeviceModbus_class_init();
}

bool VXIODevice_open(XSwitchDeviceModbus* sw, XIODeviceBaseMode mode)
{
	return false;
}
