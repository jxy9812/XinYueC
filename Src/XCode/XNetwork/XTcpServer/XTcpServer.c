// XTcpServer.c
// Copyright (C) 2026 Your Project Authors
// SPDX-License-Identifier: MIT OR LGPL-3.0-only
//
// C ?????? Qt6 QTcpServer?????? XObject??
// ?? XDeviceNetwork.h ??? TCP ????? API?

#include "XTcpServer.h"
#include "XTcpSocket.h"
#include "XDeviceNetwork.h"
#include "XVarList.h"
#include "XVariant.h"
#include "XMemory.h"
#include "XString.h"
#include "XEvent.h"
#include "XCoreApplication.h"
#include "XDateTime.h"
#include <string.h>
#if XNETWORK_ON
#if XNETWORK_TCPSERVER_ON

static bool xtcpserver_control(XTcpServer* server, XDeviceNetworkCommand command,
                               const XVarList* input, XVarList* output)
{
	if (!server || server->m_deviceFd == XFD_INVALID) return false;
	return XDevice_control(server->m_deviceFd, command, input, output);
}

static void xtcpserver_closeAcceptedSocket(XTcpServer* server, XDeviceNetworkSocketHandle handle)
{
	XVarList* input;
	if (!server || handle < 0) return;
	input = XVarList_Create(XVar(XDeviceNetworkSocketHandle, handle));
	if (!input) return;
	(void)xtcpserver_control(server, XDeviceNetworkCommand_CloseAcceptedSocket, input, NULL);
	XVarList_delete(input);
}

// ==================== ????????? ====================

static void VXTcpServer_deinit(XTcpServer* server);
static bool VXTcpServer_event(XTcpServer* server, XEvent* e);
static bool VXTcpServer_HasPendingConnections(const XTcpServer* server);
static XTcpSocket* VXTcpServer_NextPendingConnection(XTcpServer* server);
static void VXTcpServer_IncomingConnection(XTcpServer* server, intptr_t handle);

// ==================== ???????????====================

XVtable* XTcpServer_class_init(void)
{
	XVTABLE_INIT_DEFAULT(XTcpServer)
	XVTABLE_INHERIT_XCLASS(XObject);
	void* table[] = {
		VXTcpServer_HasPendingConnections,
		VXTcpServer_NextPendingConnection,
		VXTcpServer_IncomingConnection
	};
	XVTABLE_ADD_FUNC_LIST_DEFAULT(table);
	// ????
	XVTABLE_OVERLOAD_DEFAULT(EXClass_Deinit, VXTcpServer_deinit);
	// ??????
	XVTABLE_OVERLOAD_DEFAULT(EXObject_Event, VXTcpServer_event);
	XCLASS_SHOW_SIZE_DEFAULT(XTcpServer);
	return XVTABLE_DEFAULT;
}

// ==================== ??????? ====================

/**
 * @brief ???????
 * @note ?? XEVENT_TYPE_SOCK_ACT ??,???????????????
 */
static bool VXTcpServer_event(XTcpServer* server, XEvent* e)
{
	if (!server || !e) return false;

	// ??????????????
	if (e->m_type == XEVENT_TYPE_SOCK_ACT) {
		XEventSockAct* sockAct = (XEventSockAct*)e;

		// ????????(?????)
		if (sockAct->actType & XSocketActType_Accept) {
			// ????????,???
			if (server->pauseAccepting) {
				return true;
			}

				XDeviceNetworkSocketHandle clientHandle = -1;
				XDeviceNetworkSocketHandle* handleOut = &clientHandle;
				XHostAddress* addressOut = &server->lastAcceptedAddr;
				uint16_t* portOut = &server->lastAcceptedPort;
				XVarList* output = XVarList_Create(XVar(XDeviceNetworkSocketHandle*, handleOut),
					XVar(XHostAddress*, addressOut), XVar(uint16_t*, portOut));
				bool accepted = output && xtcpserver_control(server,
					XDeviceNetworkCommand_GetAcceptedSocket, NULL, output);
				if (output) XVarList_delete(output);

				if (!accepted || !XSocketDescriptor_isValid(XSocketDescriptor_fromIntptr(clientHandle))) {
				// ??????????????
				return true;
			}

			// ??????????
			XTcpServer_incomingConnection_base(server, clientHandle);
				(void)xtcpserver_control(server, XDeviceNetworkCommand_ContinueAccept, NULL, NULL);
		}
		return true;
	}
	else if (e->m_type == XEVENT_TYPE_SOCK_CLOSE) {
		// ????????
		server->listening = false;
		return true;
	}

	// ????????
	return XClass_Parent(XObject, EXObject_Event, bool (*)(XObject*, XEvent*))((XObject*)server, e);
}

static bool VXTcpServer_HasPendingConnections(const XTcpServer* server)
{
	if (!server || !server->pendingConnections) return false;
	return XVector_size_base(server->pendingConnections) > 0;
}

static XTcpSocket* VXTcpServer_NextPendingConnection(XTcpServer* server)
{
	if (!server || !server->pendingConnections) return NULL;
	if (XVector_size_base(server->pendingConnections) <= 0) return NULL;

	// ?????????
	XTcpSocket* sock = XVector_At_Base(server->pendingConnections, 0, XTcpSocket*);
	// ??????
	XVector_removeAt_base(server->pendingConnections, 0);
	return sock;
}

static void VXTcpServer_IncomingConnection(XTcpServer* server, intptr_t handle)
{
	if (!server) return;
	if (handle < 0) {
		server->lastError = XAbstractSocket_ConnectionRefusedError;
		if (server->errorString) {
			XClassDelete(server->errorString);
		}
		server->errorString = XString_create_utf8("Invalid socket descriptor");
		XTcpServer_acceptError_signal(server, XAbstractSocket_ConnectionRefusedError);
		return;
	}

	// Construct the final derived socket before its first descriptor binding, so
	// a second object never takes ownership of an already-bound descriptor.
	XTcpSocket* socket = server->incomingSocketFactory
		? server->incomingSocketFactory(server->incomingSocketFactoryContext)
		: XTcpSocket_create();
	if (!socket) {
		server->lastError = XAbstractSocket_SocketResourceError;
		if (server->errorString) {
			XClassDelete(server->errorString);
		}
		server->errorString = XString_create_utf8("Failed to create socket object");
		// ????????
			xtcpserver_closeAcceptedSocket(server, handle);
		return;
	}

	// ????????(????? XDeviceNetworkContext ????????)
	if (!XAbstractSocket_setSocketDescriptor_base(
			(XAbstractSocket*)socket, handle,
			XAbstractSocket_ConnectedState,
			XIODevice_ReadWrite)) {
		XTcpSocket_deleteLater(socket);
		server->lastError = XAbstractSocket_ConnectionRefusedError;
		if (server->errorString) {
			XClassDelete(server->errorString);
		}
		server->errorString = XString_create_utf8("Failed to set socket descriptor");
			xtcpserver_closeAcceptedSocket(server, handle);
		return;
	}

	// ??????????
	XObject_setParent((XObject*)socket, (XObject*)server);

	// ?????????(? lastAcceptedAddr/lastAcceptedPort ??)
	XAbstractSocket_setPeerAddress((XAbstractSocket*)socket, &server->lastAcceptedAddr);
	XAbstractSocket_setPeerPort((XAbstractSocket*)socket, server->lastAcceptedPort);

	// ??????????
	XTcpServer_addPendingConnection(server, socket);
}

// ==================== ???? ====================

static void VXTcpServer_deinit(XTcpServer* server)
{
	if (!server) return;
	// ?????(??????? fd)
	//XCoreApplication_sendPostedEvents(server, 0);
	XTcpServer_close(server);
	//XCoreApplication_sendPostedEvents(server,0);
	// ?????????
	if (server->pendingConnections) {
		int64_t count = XVector_size_base(server->pendingConnections);
		int64_t i;
		XTcpSocket* sock;
		for (i = 0; i < count; i++) {
			sock = XVector_At_Base(server->pendingConnections, i, XTcpSocket*);
			if (sock) {
				XObject_deinitLater((XObject*)sock);
			}
		}
		XClassDelete(server->pendingConnections);
		server->pendingConnections = NULL;
	}

	// ???????????
	if (server->errorString) {
		XClassDelete(server->errorString);
		server->errorString = NULL;
	}

	// ??????(close ???? serverAddress,???? deinit ????)
	XClassDeinit(&server->serverAddress);
	XClassDeinit(&server->lastAcceptedAddr);
	
	// ??????
	XClassDeinit(&server->proxy);
	
	// ??????(XObject ? XClass)
	XClass_Deinit_Parent(XObject, server);
}

// ==================== ???????? ====================

void XTcpServer_init(XTcpServer* server)
{
	if (!server) return;

	// ????????
	XObject_init(&server->base);

	// ????????
	server->m_deviceFd = XFD_INVALID;
	//server->serverHandle = XSocketDescriptor_Invalid();
	XHostAddress_init(&server->serverAddress);
	server->serverPort = 0;
	server->listening = false;
	server->pauseAccepting = false;
	server->pendingConnections = XVector_Create(XTcpSocket*);
	server->maxPendingConnections = 30;
	server->listenBacklogSize = 50;
	server->lastError = XAbstractSocket_UnknownSocketError;
	server->errorString = NULL;
	XNetworkProxy_init(&server->proxy);
	server->incomingSocketFactory = NULL;
	server->incomingSocketFactoryContext = NULL;
	// ????????????????????
	XHostAddress_init(&server->lastAcceptedAddr);
	server->lastAcceptedPort = 0;

	// ??????
	XClassSetVtable(server, XTcpServer);
}

XTcpServer* XTcpServer_create_ex(XMemoryType memory)
{
	XTcpServer* server = XMemory_malloc(sizeof(XTcpServer), memory);
	if (!server) return NULL;

	XTcpServer_init(server);
	Set_Class_Memory(server, memory); Set_Class_IsHeap(server, true);
	return server;
}

// ==================== ???? ====================

bool XTcpServer_listen(XTcpServer* server, const XHostAddress* address, uint16_t port)
{
	XDeviceNetworkOpenOptions options;
	XVariant value;
	int error = XDeviceError_None;
	if (!server) return false;
	if (server->listening) return false;

	// ??????
	XHostAddress listenAddr;
	XHostAddress_init(&listenAddr);
	if (address) {
		XClassCopy(&listenAddr, address);
	} else {
		XHostAddress_setAddressSpecial(&listenAddr, XHostAddress_AnySpecial);
	}
	memset(&options, 0, sizeof(options));
	options.m_base.m_openMode = XIODevice_ReadWrite;
	options.m_socketType = XDeviceNetwork_Tcp;
	options.m_protocol = (XDeviceNetworkProtocol)XHostAddress_protocol(&listenAddr);
	options.m_operation = XDeviceNetworkOpen_Listen;
	options.m_address = &listenAddr;
	options.m_port = port;
	options.m_reuseAddress = true;
	options.m_owner = server;
	options.m_listenBacklog = server->listenBacklogSize;
	server->m_deviceFd = XDevice_open(XDeviceType_Socket, &options.m_base, &error);
	if (server->m_deviceFd == XFD_INVALID) {
		XClassDeinit(&listenAddr);
		server->lastError = XAbstractSocket_AddressInUseError;
		if (server->errorString) {
			XClassDelete(server->errorString);
		}
		server->errorString = XString_create_utf8("Failed to bind to address");
		return false;
	}
	XClassDeinit(&server->serverAddress);
	XClassCopy(&server->serverAddress, &listenAddr);
	XClassDeinit(&listenAddr);
	/* ???(? XAbstractSocket socketGetBoolProperty ??): memset+XVariant_init
	 * ? vtable, ?? XVariant_setValue_null ? XClassDeinit ?????
	 * ? memset+XVariant_clear ?????????--ASan ???? listen
	 * ?? 4B int ??(XVariant.c:443 XMalloc_System)? */
	memset(&value, 0, sizeof(value));
	XVariant_init(&value, NULL, 0, XVariantType_NULL);
	if (!XDevice_getProperty(server->m_deviceFd, (XDeviceProperty)XDeviceNetworkProperty_LocalPort, &value)) {
		XVariant_setValue_null(&value);
		XDevice_close(server->m_deviceFd);
		server->m_deviceFd = XFD_INVALID;
		return false;
	}
	server->serverPort = (uint16_t)XVariant_toInt(&value);
	XVariant_setValue_null(&value);
	server->listening = true;
	server->pauseAccepting = false;

	/* ?????? Accept(serverCreate ???,accept ????????) */
	if (!xtcpserver_control(server, XDeviceNetworkCommand_ContinueAccept, NULL, NULL)) {
		XTcpServer_close(server);
		return false;
	}

	return true;
}

void XTcpServer_close(XTcpServer* server)
{
	if (!server) return;
	if (server->m_deviceFd == XFD_INVALID) return;
	XDevice_close(server->m_deviceFd);
	server->m_deviceFd = XFD_INVALID;
	server->listening = false;
	server->serverPort = 0;
	XClassDeinit(&server->serverAddress);
	XHostAddress_init(&server->serverAddress);
}

bool XTcpServer_isListening(const XTcpServer* server)
{
	if (!server) return false;
	return server->listening;
}

// ==================== ???? ====================

void XTcpServer_setMaxPendingConnections(XTcpServer* server, int numConnections)
{
	if (!server) return;
	server->maxPendingConnections = numConnections;
}

int XTcpServer_maxPendingConnections(const XTcpServer* server)
{
	if (!server) return 0;
	return server->maxPendingConnections;
}

void XTcpServer_setIncomingSocketFactory(XTcpServer* server,
	XTcpServer_IncomingSocketFactory factory, void* context)
{
	if (!server || server->listening) return;
	server->incomingSocketFactory = factory;
	server->incomingSocketFactoryContext = context;
}

void XTcpServer_setListenBacklogSize(XTcpServer* server, int size)
{
	if (!server) return;
	server->listenBacklogSize = size;
}

int XTcpServer_listenBacklogSize(const XTcpServer* server)
{
	if (!server) return 0;
	return server->listenBacklogSize;
}

uint16_t XTcpServer_serverPort(const XTcpServer* server)
{
	if (!server) return 0;
	return server->serverPort;
}

const XHostAddress* XTcpServer_serverAddress(const XTcpServer* server)
{
	if (!server) return &XHostAddress_Null;
	return &server->serverAddress;
}

// ==================== ????????? ====================

intptr_t XTcpServer_socketDescriptor(const XTcpServer* server)
{
	XVariant value;
	if (!server || server->m_deviceFd == XFD_INVALID) return -1;
	/* ???: init ? vtable + setValue_null ????(????????,
	 * NativeHandle ptr ???????? 8B)? */
	memset(&value, 0, sizeof(value));
	XVariant_init(&value, NULL, 0, XVariantType_NULL);
	if (!XDevice_getProperty(server->m_deviceFd, XDeviceProperty_NativeHandle, &value)) {
		XVariant_setValue_null(&value);
		return -1;
	}
	{
		intptr_t descriptor = (intptr_t)XVariant_toPtr(&value);
		XVariant_setValue_null(&value);
		return descriptor;
	}
}

bool XTcpServer_setSocketDescriptor(XTcpServer* server, intptr_t socketDescriptor)
{
	XDeviceNetworkOpenOptions options;
	XVariant value;
	int error = XDeviceError_None;
	if (!server) return false;
	if (socketDescriptor < 0) return false;

	// ???????,???
	if (server->listening) {
		XTcpServer_close(server);
	}

	memset(&options, 0, sizeof(options));
	options.m_base.m_openMode = XIODevice_ReadWrite;
	options.m_socketType = XDeviceNetwork_Tcp;
	options.m_protocol = XDeviceNetwork_Any;
	options.m_operation = XDeviceNetworkOpen_ListenAdopt;
	options.m_socketDescriptor = socketDescriptor;
	options.m_owner = server;
	server->m_deviceFd = XDevice_open(XDeviceType_Socket, &options.m_base, &error);
	if (server->m_deviceFd == XFD_INVALID) return false;
	/* ???: init ? vtable + setValue_null ????(? XTcpServer_listen)? */
	memset(&value, 0, sizeof(value));
	XVariant_init(&value, NULL, 0, XVariantType_NULL);
	if (!XDevice_getProperty(server->m_deviceFd, (XDeviceProperty)XDeviceNetworkProperty_LocalPort, &value)) {
		XVariant_setValue_null(&value);
		XTcpServer_close(server);
		return false;
	}
	server->serverPort = (uint16_t)XVariant_toInt(&value);
	XVariant_setValue_null(&value);
	server->listening = true;
	if (!xtcpserver_control(server, XDeviceNetworkCommand_ContinueAccept, NULL, NULL)) {
		XTcpServer_close(server);
		return false;
	}

	return true;
}

// ==================== ???? ====================

bool XTcpServer_waitForNewConnection(XTcpServer* server, int msec, bool* timedOut)
{
	if (!server || !server->listening) {
		if (timedOut) *timedOut = false;
		return false;
	}

	if (timedOut) *timedOut = false;

	// ?????????,????
	if (XTcpServer_hasPendingConnections_base(server)) {
		return true;
	}

	// ?? msec == 0,???,????
	if (msec == 0) {
		if (timedOut) *timedOut = true;
		return false;
	}

	// ???????????
	uint64_t startTime = XDateTime_currentMSecsSinceEpoch();
	
	while (!XTcpServer_hasPendingConnections_base(server)) {
		// ????
		XCoreApplication_processEvents(XEventLoop_AllEvents);
		
		// ???????
		if (msec > 0) {
			uint64_t elapsed = XDateTime_currentMSecsSinceEpoch() - startTime;
			if (elapsed >= (uint64_t)msec) {
				if (timedOut) *timedOut = true;
				return false;
			}
		}
	}

	return true;
}

bool XTcpServer_hasPendingConnections_base(const XTcpServer* server)
{
	if (!server) return false;
	return XClassGetVirtualFunc(server, EXTcpServer_HasPendingConnections,
		bool(*)(const XTcpServer*))(server);
}

XTcpSocket* XTcpServer_nextPendingConnection_base(XTcpServer* server)
{
	if (!server) return NULL;
	return XClassGetVirtualFunc(server, EXTcpServer_NextPendingConnection,
		XTcpSocket*(*)(XTcpServer*))(server);
}

// ==================== ?????? ====================

XAbstractSocket_SocketError XTcpServer_serverError(const XTcpServer* server)
{
	if (!server) return XAbstractSocket_UnknownSocketError;
	return server->lastError;
}

char* XTcpServer_errorString(const XTcpServer* server)
{
	if (!server) return NULL;
	if (!server->errorString) return NULL;

	// ???????????
	const char* str = XString_toUtf8(server->errorString);
	if (!str) return NULL;

	size_t len = strlen(str);
	char* result = (char*)XMalloc_System(len + 1);
	if (result) {
		memcpy(result, str, len + 1);
	}
	return result;
}

// ==================== ???? ====================

void XTcpServer_pauseAccepting(XTcpServer* server)
{
	if (!server) return;
	server->pauseAccepting = true;
}

void XTcpServer_resumeAccepting(XTcpServer* server)
{
	if (!server) return;
	server->pauseAccepting = false;
}

// ==================== ???? ====================

void XTcpServer_setProxy(XTcpServer* server, const XNetworkProxy* proxy)
{
	if (!server || !proxy) return;
	// ??????????????
	XClassDeinit(&server->proxy);
	// ??????????????
	XNetworkProxy_init(&server->proxy);
	server->proxy.type = proxy->type;
	server->proxy.capabilities = proxy->capabilities;
	server->proxy.port = proxy->port;
	if (proxy->hostName) {
		XNetworkProxy_setHostName(&server->proxy, proxy->hostName);
	}
	if (proxy->user) {
		XNetworkProxy_setUser(&server->proxy, proxy->user);
	}
	if (proxy->password) {
		XNetworkProxy_setPassword(&server->proxy, proxy->password);
	}
}

XNetworkProxy* XTcpServer_proxy(const XTcpServer* server)
{
	if (!server) return NULL;
	return (XNetworkProxy*)&server->proxy;
}

// ==================== ???????????====================

void XTcpServer_incomingConnection_base(XTcpServer* server, intptr_t handle)
{
	XClassGetVirtualFunc(server, EXTcpServer_IncomingConnection,
		void(*)(XTcpServer*, intptr_t))(server, handle);
}

void XTcpServer_addPendingConnection(XTcpServer* server, XTcpSocket* socket)
{
	int64_t currentCount;
	XTcpSocket* sock;
	
	if (!server || !socket) return;
	if (!server->pendingConnections) return;

	// ?????????????????
	currentCount = XVector_size_base(server->pendingConnections);
	if (currentCount >= server->maxPendingConnections) {
		// ????????????????????
		XAbstractSocket_close_base((XAbstractSocket*)socket);
		XObject_deinitLater((XObject*)socket);
		return;
	}

	// ????????
	sock = socket;
	XVector_push_back_1_base(server->pendingConnections, &sock);

	// ??? newConnection ?????t 6.8 ?????
	XTcpServer_newConnection_signal(server);

	// ?? pendingConnectionAvailable ??
	XTcpServer_pendingConnectionAvailable_signal(server);
}

// ==================== ???? ====================

void* XTcpServer_newConnection_signal(XTcpServer* server)
{
	XEmitSignal(server, XTcpServer_newConnection_signal, NULL, NULL, NULL, XEVENT_PRIORITY_NORMAL);
}

void* XTcpServer_pendingConnectionAvailable_signal(XTcpServer* server)
{
	XEmitSignal(server, XTcpServer_pendingConnectionAvailable_signal, NULL, NULL, NULL, XEVENT_PRIORITY_NORMAL);
}

void* XTcpServer_acceptError_signal(XTcpServer* server, XAbstractSocket_SocketError socketError)
{
	XEmitSignal(server, XTcpServer_acceptError_signal, XVarList_Create(XVar(XAbstractSocket_SocketError, socketError)), NULL, NULL, XEVENT_PRIORITY_NORMAL);
}
#endif // XNETWORK_TCPSERVER_ON
#endif /* XNETWORK_ON */
