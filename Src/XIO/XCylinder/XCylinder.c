#include "XCylinder.h"
#include "XMemory.h"
#include <string.h>

XCylinder* XCylinder_new(XCylinder_PortInit* port)
{
	if (port == NULL)
		return NULL;
	XCylinder* cylinder = XMemory_malloc(sizeof(XCylinder));
	if (cylinder == NULL)
		return;
	//printf("初始化\n");
	XSwitchDevice_init(&(cylinder->m_sv),&(port->sv));
	XSwitchDevice_init(&(cylinder->m_dl), &(port->dl));
	XSwitchDevice_init(&(cylinder->m_ul), &(port->ul));
	cylinder->m_sv.m_parent.device = cylinder;
	cylinder->m_dl.m_parent.device = cylinder;
	cylinder->m_ul.m_parent.device = cylinder;
	return cylinder;
}

void XCylinder_poll(XCylinder* cylinder)
{
	if (cylinder == NULL)
		return;
	XSwitchDevice_poll(&(cylinder->m_dl));
	XSwitchDevice_poll(&(cylinder->m_sv));
	XSwitchDevice_poll(&(cylinder->m_ul));
}
