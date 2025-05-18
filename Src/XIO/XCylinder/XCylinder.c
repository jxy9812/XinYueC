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
	cylinder->m_sv->m_parent.device = cylinder;
	cylinder->m_dl->m_parent.device = cylinder;
	cylinder->m_ul->m_parent.device = cylinder;
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

void XCylinder_poll(XCylinder* cylinder)
{
	if (cylinder == NULL)
		return;
	XSwitchDevice_poll((cylinder->m_dl));
	XSwitchDevice_poll((cylinder->m_sv));
	XSwitchDevice_poll((cylinder->m_ul));
}
