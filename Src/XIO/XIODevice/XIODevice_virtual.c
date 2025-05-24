#include"XIODevice.h"

//声明 
XVtable* XIODeviceVtable = NULL;
#if VTABLE_ISSTACK
static XVtable vtable;//虚函数类
static void* vtable_data[XIODEVICE_VTABLE_SIZE];//虚函数数据
#endif





void XIODevice_class_init()
{
	//仅初始化一次
	if (XIODeviceVtable)
		return;
#if !VTABLE_ISSTACK
	XIODeviceVtable = XVtable_new();
#else
	XIODeviceVtable = &vtable;
	XVtable_init_stack(&vtable, vtable_data, sizeof(vtable_data) / sizeof(vtable_data[0]));
#endif
	//void* table[] = { };
	//XVtable_append_array(XIODeviceVtable, table, sizeof(table) / sizeof(table[0]));
#if SHOWCONTAINERSIZE
	printf("XIODevice size:%d\n", XVtable_size(XIODeviceVtable));
#endif
}