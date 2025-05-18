#include "XCylinder.h"
#include "XMemory.h"
#include <string.h>

XCylinder* XCylinder_new(XCylinder_PortInit* port)
{
	if (port == NULL)
		return NULL;
	XCylinder* cylinder = XMemory_malloc(sizeof(XCylinder));
	if (cylinder == NULL)
		return cylinder;
	memset(cylinder,0,sizeof(XCylinder));
	//printf("初始化\n");
	cylinder->m_sv=XSwitchDevice_new(&(port->sv));
	cylinder->m_dl = XSwitchDevice_new(&(port->dl));
	cylinder->m_ul = XSwitchDevice_new(&(port->ul));
	//绑定设备
	XCylinder_setDevice(cylinder, cylinder);
	return cylinder;
}

void XCylinder_free(XCylinder* cylinder)
{
	if (cylinder == NULL)
		return;
	if (cylinder->m_dl)
		XSwitchDevice_free(cylinder->m_dl);
	if (cylinder->m_sv)
		XSwitchDevice_free(cylinder->m_sv);
	if (cylinder->m_ul)
		XSwitchDevice_free(cylinder->m_ul);
}

void XCylinder_setDevice(XCylinder* cylinder, void* device)
{
	if (cylinder != NULL)
	{
		XIODevice_setDevice(cylinder->m_dl, device);
		XIODevice_setDevice(cylinder->m_sv, device);
		XIODevice_setDevice(cylinder->m_ul, device);
	}
}

void XCylinder_open(XCylinder* cylinder)
{
	XIODevice_open(cylinder->m_dl, XIODeviceBase_ReadOnly);
	XIODevice_open(cylinder->m_ul, XIODeviceBase_ReadOnly);
	XIODevice_open(cylinder->m_sv, XIODeviceBase_WriteOnly);
}

void XCylinder_poll(XCylinder* cylinder)
{
	if (cylinder == NULL)
		return;
	XSwitchDevice_poll((cylinder->m_dl));
	XSwitchDevice_poll((cylinder->m_sv));
	XSwitchDevice_poll((cylinder->m_ul));
}
