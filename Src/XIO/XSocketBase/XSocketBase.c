#include"XSocketBase.h"
#include"XMemory.h"
#include<string.h>
XSocketBase* XSocketBase_create()
{
	XSocketBase* socket = XMemory_malloc(sizeof(XSocketBase));
	XSocketBase_init(socket);
	return socket;
}

void XSocketBase_init(XSocketBase* socket)
{
	if (socket == NULL)
		return;
	memset(((XIODeviceBase*)socket) + 1, 0, sizeof(XSocketBase) - sizeof(XIODeviceBase));
	XIODeviceBase_init(socket,NULL);
	XClassGetVtable(socket) = XSocketBase_class_init();


}

const char* XSocketBase_peerAddress(XSocketBase* socket)
{
	if (socket == NULL)
		return NULL;
}

const char* XSocketBase_peerName(XSocketBase* socket)
{
	if(socket==NULL)
		return NULL;
}

uint16_t XSocketBase_peerPort(XSocketBase* socket)
{
	if (socket)
		return socket->m_peerPort;
	return 0;
}

XSocketType XSocketBase_socketType(const XSocketBase* socket)
{
	if (socket)
		return socket->m_socketType;
	return XSOCKET_TYPE_UNKNOWN;
}

XSocketState XSocketBase_state(const XSocketBase* socket)
{
	if(socket)
		return socket->m_state;
	return XSOCKET_UNCONNECTED_STATE;
}
