// XTcpServer.c
// Copyright (C) 2026 Your Project Authors
// SPDX-License-Identifier: MIT OR LGPL-3.0-only
//
// C 璇█瀹炵幇 Qt6 QTcpServer锛岀户鎵胯嚜 XObject銆?
// 基于 XNetwork.h 提供的 TCP 服务器平台 API。

#include "XTcpServer.h"
#include "XTcpSocket.h"
#include "XNetwork.h"
#include "XMemory.h"
#include "XString.h"
#include "XEvent.h"
#include "XCoreApplication.h"
#include "XDateTime.h"
#include <string.h>
#if XNETWORK_ON
#if XNETWORK_TCPSERVER_ON

// ==================== 杈呭姪瀹?====================
#define getPriv(server) ((XNetworkSocketPrivate*)(server)->d_ptr)

// ==================== 鍐呴儴铏氬嚱鏁板墠鍚戝０鏄?====================

static void VXTcpServer_deinit(XTcpServer* server);
static bool VXTcpServer_event(XTcpServer* server, XEvent* e);
static bool VXTcpServer_HasPendingConnections(const XTcpServer* server);
static XTcpSocket* VXTcpServer_NextPendingConnection(XTcpServer* server);
static void VXTcpServer_IncomingConnection(XTcpServer* server, intptr_t handle);

// ==================== 铏氬嚱鏁拌〃鍒濆鍖?====================

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
	// 閲嶈浇鏋愭瀯
	XVTABLE_OVERLOAD_DEFAULT(EXClass_Deinit, VXTcpServer_deinit);
	// 閲嶈浇浜嬩欢澶勭悊
	XVTABLE_OVERLOAD_DEFAULT(EXObject_Event, VXTcpServer_event);
	XCLASS_SHOW_SIZE_DEFAULT(XTcpServer);
	return XVTABLE_DEFAULT;
}

// ==================== 鍐呴儴铏氬嚱鏁板疄鐜?====================

/**
 * @brief 浜嬩欢澶勭悊铏氬嚱鏁?
 * @note 澶勭悊 XEVENT_TYPE_SOCK_ACT 浜嬩欢锛屽綋鏈嶅姟鍣ㄥ鎺ュ瓧鍙鏃舵帴鍙楁柊杩炴帴
 */
static bool VXTcpServer_event(XTcpServer* server, XEvent* e)
{
	if (!server || !e) return false;

	// 澶勭悊濂楁帴瀛楁椿鍔ㄤ簨浠?
	if (e->type == XEVENT_TYPE_SOCK_ACT) {
		XEventSockAct* sockAct = (XEventSockAct*)e;

		// 妫€鏌ユ槸鍚︿负璇讳簨浠讹紙鏂拌繛鎺ュ埌杈撅級
		if (sockAct->actType & XSocketAct_Accept) {
			// 濡傛灉鏆傚仠鎺ュ彈杩炴帴锛屼笉澶勭悊
			if (server->pauseAccepting) {
				return true;
			}

			XSocketHandle clientHandle = XNetwork_serverGetAcceptedSocket(server->d_ptr,
				&server->lastAcceptedAddr, &server->lastAcceptedPort);

			if (!XSocketDescriptor_isValid(XSocketDescriptor_fromIntptr(clientHandle))) {
				// 娌℃湁鏇村杩炴帴鍙帴鍙?
				return true;
			}

			// 璋冪敤铏氬嚱鏁板鐞嗘柊杩炴帴
			XTcpServer_incomingConnection_base(server, clientHandle);
			XNetwork_serverAccept(server->d_ptr);
		}
		return true;
	}
	else if (e->type == XEVENT_TYPE_SOCK_CLOSE) {
		// 鏈嶅姟鍣ㄥ鎺ュ瓧鍏抽棴
		server->listening = false;
		return true;
	}

	// 璋冪敤鐖剁被浜嬩欢澶勭悊
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

	// 鍙栧嚭闃熼鍏冪礌
	XTcpSocket* sock = XVector_At_Base(server->pendingConnections, 0, XTcpSocket*);
	// 绉婚櫎闃熼
	XVector_removeAt_base(server->pendingConnections, 0);
	return sock;
}

static void VXTcpServer_IncomingConnection(XTcpServer* server, intptr_t handle)
{
	if (!server) return;
	if (handle < 0) {
		server->lastError = XAbstractSocket_ConnectionRefusedError;
		if (server->errorString) {
			XString_delete_base(server->errorString);
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
			XString_delete_base(server->errorString);
		}
		server->errorString = XString_create_utf8("Failed to create socket object");
		// 鍏抽棴涓嶆帴鍙楃殑鍙ユ焺
		XNetwork_serverClose(getPriv(server), handle);
		return;
	}

	// 璁剧疆鎻忚堪绗﹀拰鐘舵€侊紙鍐呴儴浼氬垱寤?XNetworkSocketPrivate 骞舵敞鍐屽埌浜嬩欢寰幆锛?
	if (!XAbstractSocket_setSocketDescriptor_base(
			(XAbstractSocket*)socket, handle,
			XAbstractSocket_ConnectedState,
			XIODevice_ReadWrite)) {
		XTcpSocket_deleteLater(socket);
		server->lastError = XAbstractSocket_ConnectionRefusedError;
		if (server->errorString) {
			XString_delete_base(server->errorString);
		}
		server->errorString = XString_create_utf8("Failed to set socket descriptor");
		XNetwork_serverClose(getPriv(server), handle);
		return;
	}

	// 璁剧疆涓烘湇鍔″櫒鐨勫瓙瀵硅薄
	XObject_setParent((XObject*)socket, (XObject*)server);

	// 璁剧疆瀵圭鍦板潃鍜岀鍙ｏ紙浠?lastAcceptedAddr/lastAcceptedPort 鑾峰彇锛?
	XAbstractSocket_setPeerAddress((XAbstractSocket*)socket, &server->lastAcceptedAddr);
	XAbstractSocket_setPeerPort((XAbstractSocket*)socket, server->lastAcceptedPort);

	// 娣诲姞鍒板緟澶勭悊杩炴帴闃熷垪
	XTcpServer_addPendingConnection(server, socket);
}

// ==================== 鏋愭瀯鍑芥暟 ====================

static void VXTcpServer_deinit(XTcpServer* server)
{
	if (!server) return;
	// 鍏抽棴鏈嶅姟鍣紙浼氭竻鐞?d_ptr锛?
	//XCoreApplication_sendPostedEvents(server, 0);
	XTcpServer_close(server);
	//XCoreApplication_sendPostedEvents(server,0);
	// 娓呯悊寰呭鐞嗚繛鎺ラ槦鍒?
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
		XVector_delete_base(server->pendingConnections);
		server->pendingConnections = NULL;
	}

	// 娓呯悊閿欒瀛楃涓?
	if (server->errorString) {
		XString_delete_base(server->errorString);
		server->errorString = NULL;
	}

	// 閲婃斁鍦板潃缁撴瀯锛坈lose 涓凡閲嶇疆 serverAddress锛岃繖閲屽啀娆?deinit 鏄畨鍏ㄧ殑锛?
	XHostAddress_deinit_base(&server->serverAddress);
	XHostAddress_deinit_base(&server->lastAcceptedAddr);
	
	// 閲婃斁浠ｇ悊璧勬簮
	XNetworkProxy_deinit_base(&server->proxy);
	
	// 璋冪敤鐖剁被鏋愭瀯锛圶Object 鈫?XClass锛?
	XClass_Deinit_Parent(XObject, server);
}

// ==================== 鏋勯€犱笌鏋愭瀯 ====================

void XTcpServer_init(XTcpServer* server)
{
	if (!server) return;

	// 鍒濆鍖栧熀绫?
	XObject_init(&server->base);

	// 鍒濆鍖栨垚鍛?
	server->d_ptr = NULL;
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
	// 鍒濆鍖栦复鏃跺瓨鍌ㄧ殑瀹㈡埛绔湴鍧€
	XHostAddress_init(&server->lastAcceptedAddr);
	server->lastAcceptedPort = 0;

	// 璁剧疆铏氬嚱鏁拌〃
	XClassSetVtable(server, XTcpServer);
}

XTcpServer* XTcpServer_create(void)
{
	XTcpServer* server = XNew(XTcpServer);
	if (!server) return NULL;

	XTcpServer_init(server);
	Set_Class_MemoryFree(server, XFree_System);
	return server;
}

// ==================== 鏍稿績鍔熻兘 ====================

bool XTcpServer_listen(XTcpServer* server, const XHostAddress* address, uint16_t port)
{
	if (!server) return false;
	if (server->listening) return false;

	// 纭繚缃戠粶瀛愮郴缁熷凡鍒濆鍖?
	XNetwork_ensureInit();

	// 浣跨敤榛樿鍦板潃
	XHostAddress listenAddr;
	XHostAddress_init(&listenAddr);
	if (address) {
		XHostAddress_copy_base(&listenAddr, address);
	} else {
		XHostAddress_setAddressSpecial(&listenAddr, XHostAddress_AnySpecial);
	}
	if (!server->d_ptr)
		server->d_ptr = XNetwork_createSocketPrivate(server);
	// 璋冪敤骞冲彴 API 鍒涘缓鏈嶅姟鍣?
	// 娉ㄦ剰锛氫唬鐞嗘湇鍔″櫒鍔熻兘宸茬Щ鑷冲簲鐢ㄥ眰瀹炵幇锛屽钩鍙板眰涓嶅啀鎻愪緵浠ｇ悊API
	XServerHandle handle = XNetwork_serverCreate(server->d_ptr,
		&listenAddr, port, server->listenBacklogSize, true);

	if (!XSocketDescriptor_isValid(XSocketDescriptor_fromIntptr(handle))) {
		XHostAddress_deinit_base(&listenAddr);
		server->lastError = XAbstractSocket_AddressInUseError;
		if (server->errorString) {
			XString_delete_base(server->errorString);
		}
		server->errorString = XString_create_utf8("Failed to bind to address");
		return false;
	}
	//server->serverHandle = XSocketDescriptor_fromIntptr(handle);
	XHostAddress_deinit_base(&server->serverAddress);
	XHostAddress_copy_base(&server->serverAddress, &listenAddr);
	XHostAddress_deinit_base(&listenAddr);
	server->serverPort = XNetwork_serverPort(handle);
	server->listening = true;
	server->pauseAccepting = false;

	/* 启动首次异步 Accept（serverCreate 仅创建，accept 由用户层显式启动） */
	XNetwork_serverAccept(server->d_ptr);

	return true;
}

void XTcpServer_close(XTcpServer* server)
{
	if (!server) return;
	if (!server->listening) return;

	XNetwork_serverClose(server->d_ptr, XTcpServer_socketDescriptor(server));

	if (server->d_ptr) {
		XNetwork_deleteSocketPrivate(server->d_ptr);
		server->d_ptr = NULL;
	}
	server->listening = false;
	server->serverPort = 0;
	XHostAddress_deinit_base(&server->serverAddress);
	XHostAddress_init(&server->serverAddress);
}

bool XTcpServer_isListening(const XTcpServer* server)
{
	if (!server) return false;
	return server->listening;
}

// ==================== 杩炴帴绠＄悊 ====================

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

// ==================== 濂楁帴瀛楁弿杩扮 ====================

intptr_t XTcpServer_socketDescriptor(const XTcpServer* server)
{
	if (!server||!server->d_ptr) return -1;
	return XNetwork_socketDescriptor(server->d_ptr);
}

bool XTcpServer_setSocketDescriptor(XTcpServer* server, intptr_t socketDescriptor)
{
	if (!server) return false;
	if (socketDescriptor < 0) return false;

	// 濡傛灉宸茬粡鍦ㄧ洃鍚紝鍏堝叧闂?
	if (server->listening) {
		XTcpServer_close(server);
	}

	// 鍒涘缓 XNetworkSocketPrivate 鐢ㄤ簬寮傛浜嬩欢閫氱煡
	XNetworkSocketPrivate* priv = XNetwork_createSocketPrivate(server);
	if (!priv) return false;

	// TCP server 不是 XIODevice，使用服务器专用的描述符接管路径。
	if (!XNetwork_serverSetDescriptor(priv, socketDescriptor)) {
		XNetwork_deleteSocketPrivate(priv);
		return false;
	}

	// 淇濆瓨鐘舵€?
	server->d_ptr = priv;
	//server->serverHandle = XSocketDescriptor_fromIntptr(socketDescriptor);
	server->serverPort = XNetwork_serverPort(XNetwork_socketDescriptor(priv));
	server->listening = true;
	XNetwork_serverAccept(server->d_ptr);

	return true;
}

// ==================== 杩炴帴澶勭悊 ====================

bool XTcpServer_waitForNewConnection(XTcpServer* server, int msec, bool* timedOut)
{
	if (!server || !server->listening) {
		if (timedOut) *timedOut = false;
		return false;
	}

	if (timedOut) *timedOut = false;

	// 濡傛灉宸叉湁寰呭鐞嗚繛鎺ワ紝鐩存帴杩斿洖
	if (XTcpServer_hasPendingConnections_base(server)) {
		return true;
	}

	// 濡傛灉 msec == 0锛岄潪闃诲锛岀洿鎺ヨ繑鍥?
	if (msec == 0) {
		if (timedOut) *timedOut = true;
		return false;
	}

	// 浣跨敤浜嬩欢寰幆绛夊緟鏂拌繛鎺?
	uint64_t startTime = XDateTime_currentMSecsSinceEpoch();
	
	while (!XTcpServer_hasPendingConnections_base(server)) {
		// 澶勭悊浜嬩欢
		XCoreApplication_processEvents(XEventLoop_AllEvents);
		
		// 妫€鏌ヨ秴鏃?
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

// ==================== 閿欒澶勭悊 ====================

XAbstractSocket_SocketError XTcpServer_serverError(const XTcpServer* server)
{
	if (!server) return XAbstractSocket_UnknownSocketError;
	return server->lastError;
}

char* XTcpServer_errorString(const XTcpServer* server)
{
	if (!server) return NULL;
	if (!server->errorString) return NULL;

	// 杩斿洖鏂板垎閰嶇殑瀛楃涓插壇鏈?
	const char* str = XString_toUtf8(server->errorString);
	if (!str) return NULL;

	size_t len = strlen(str);
	char* result = (char*)XMalloc_System(len + 1);
	if (result) {
		memcpy(result, str, len + 1);
	}
	return result;
}

// ==================== 鎺ュ彈鎺у埗 ====================

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

// ==================== 浠ｇ悊璁剧疆 ====================

void XTcpServer_setProxy(XTcpServer* server, const XNetworkProxy* proxy)
{
	if (!server || !proxy) return;
	// 鍏堥噴鏀炬棫鐨勪唬鐞嗚祫婧?
	XNetworkProxy_deinit_base(&server->proxy);
	// 閲嶆柊鍒濆鍖栧苟娣辨嫹璐?
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

// ==================== 鍙椾繚鎶ょ殑铏氬嚱鏁?====================

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

	// 妫€鏌ユ槸鍚﹁秴杩囨渶澶ц繛鎺ユ暟
	currentCount = XVector_size_base(server->pendingConnections);
	if (currentCount >= server->maxPendingConnections) {
		// 瓒呰繃鏈€澶ц繛鎺ユ暟锛屽叧闂柊杩炴帴
		XAbstractSocket_close_base((XAbstractSocket*)socket);
		XObject_deinitLater((XObject*)socket);
		return;
	}

	// 娣诲姞鍒伴槦鍒?
	sock = socket;
	XVector_push_back_1_base(server->pendingConnections, &sock);

	// 鍙戝皠 newConnection 淇″彿锛圦t 6.8 琛屼负锛?
	XTcpServer_newConnection_signal(server);

	// 鍙戝皠 pendingConnectionAvailable 淇″彿
	XTcpServer_pendingConnectionAvailable_signal(server);
}

// ==================== 淇″彿瀹炵幇 ====================

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
