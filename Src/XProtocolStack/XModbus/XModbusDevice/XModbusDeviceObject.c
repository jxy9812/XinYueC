#include "XModbusDeviceObject.h"
#include <string.h>
void XModbusDeviceObject_init(XModbusDeviceObject* de)
{
	if (de == NULL)
		return;
	memset(de,0,sizeof(XModbusDeviceObject));
}

void XModbusDeviceObject_setCallback(XModbusDeviceObject* de, void(*cb)(void* userData))
{
	if (de)
		de->cb = cb;
}

void XModbusDeviceObject_setUserData(XModbusDeviceObject* de, void* userData)
{
	if (de)
		de->userData = userData;

}
