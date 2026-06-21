// XTcpServer.c
// Copyright (C) 2026 Your Project Authors
// SPDX-License-Identifier: MIT OR LGPL-3.0-only
//
// C 语言实现 Qt6 QTcpServer，继承自 XObject。
// 基于 XNetwork_platform 提供的 TCP 服务器 API。

#include "XTcpServer.h"
#include "XTcpSocket.h"
#include "XNetwork_platform.h"
#include "XMemory.h"
#include "XString.h"
#include "XEvent.h"
#include "XCoreApplication.h"
#include "XDateTime.h"
#include <string.h>

// ==================== 辅助宏 ====================
#define getPriv(server) ((XNetworkSocketPrivate*)(server)->d_ptr)

// ==================== 内部虚函数前向声明 ====================

static void VXTcpServer_deinit(XTcpServer* server);
static bool VXTcpServer_event(XTcpServer* server, XEvent* e);
static bool VXTcpServer_HasPendingConnections(const XTcpServer* server);
static XTcpSocket* VXTcpServer_NextPendingConnection(XTcpServer* server);
static void VXTcpServer_IncomingConnection(XTcpServer* server, intptr_t handle);

// ==================== 虚函数表初始化 ====================

XVtable* XTcpServer_class_init(void)
{
	XVTABLE_CREAT_DEFAULT
#if VTABLE_ISSTACK
	XVTABLE_STACK_INIT_DEFAULT(XCLASS_VTABLE_GET_SIZE(XTcpServer))
#else
	XVTABLE_HEAP_INIT_DEFAULT
#endif
	XVTABLE_INHERIT_XCLASS(XObject);
	void* table[] = {
		VXTcpServer_HasPendingConnections,
		VXTcpServer_NextPendingConnection,
		VXTcpServer_IncomingConnection
	};
	XVTABLE_ADD_FUNC_LIST_DEFAULT(table);
	// 重载析构
	XVTABLE_OVERLOAD_DEFAULT(EXClass_Deinit, VXTcpServer_deinit);
	// 重载事件处理
	XVTABLE_OVERLOAD_DEFAULT(EXObject_Event, VXTcpServer_event);
#if SHOWCONTAINERSIZE
	printf("XTcpServer vtable size: %d\n", XVtable_size(XVTABLE_DEFAULT));
#endif
	return XVTABLE_DEFAULT;
}

// ==================== 内部虚函数实现 ====================

/**
 * @brief 事件处理虚函数
 * @note 处理 XEVENT_TYPE_SOCK_ACT 事件，当服务器套接字可读时接受新连接
 */
static bool VXTcpServer_event(XTcpServer* server, XEvent* e)
{
	if (!server || !e) return false;

	// 处理套接字活动事件
	if (e->type == XEVENT_TYPE_SOCK_ACT) {
		XEventSockAct* sockAct = (XEventSockAct*)e;
		
		// 检查是否为读事件（新连接到达）
		if (sockAct->actType & XSocketAct_Accept) {
			// 如果暂停接受连接，不处理
			if (server->pauseAccepting) {
				return true;
			}

			XSocketHandle clientHandle = XNetwork_serverGetAcceptedSocket(server->d_ptr,
				&server->lastAcceptedAddr, &server->lastAcceptedPort);

			if (!XSocketDescriptor_isValid(XSocketDescriptor_fromIntptr(clientHandle))) {
				// 没有更多连接可接受
				return true;
			}

			// 调用虚函数处理新连接
			XTcpServer_incomingConnection_base(server, clientHandle);
			XNetwork_serverContinueAccept(server->d_ptr);
		}
		return true;
	}
	else if (e->type == XEVENT_TYPE_SOCK_CLOSE) {
		// 服务器套接字关闭
		server->listening = false;
		return true;
	}

	// 调用父类事件处理
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

	// 取出队首元素
	XTcpSocket* sock = XVector_At_Base(server->pendingConnections, 0, XTcpSocket*);
	// 移除队首
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

	// 创建 TCP 套接字
	XTcpSocket* socket = XTcpSocket_create();
	if (!socket) {
		server->lastError = XAbstractSocket_SocketResourceError;
		if (server->errorString) {
			XString_delete_base(server->errorString);
		}
		server->errorString = XString_create_utf8("Failed to create socket object");
		// 关闭不接受的句柄
		XNetwork_serverClose(handle);
		return;
	}

	// 设置描述符和状态（内部会创建 XNetworkSocketPrivate 并注册到事件循环）
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
		XNetwork_serverClose(handle);
		return;
	}

	// 设置为服务器的子对象
	XObject_setParent((XObject*)socket, (XObject*)server);

	// 设置对端地址和端口（从 lastAcceptedAddr/lastAcceptedPort 获取）
	XAbstractSocket_setPeerAddress((XAbstractSocket*)socket, &server->lastAcceptedAddr);
	XAbstractSocket_setPeerPort((XAbstractSocket*)socket, server->lastAcceptedPort);

	// 添加到待处理连接队列
	XTcpServer_addPendingConnection(server, socket);
}

// ==================== 析构函数 ====================

static void VXTcpServer_deinit(XTcpServer* server)
{
	if (!server) return;
	// 关闭服务器（会清理 d_ptr）
	//XCoreApplication_sendPostedEvents(server, 0);
	XTcpServer_close(server);
	//XCoreApplication_sendPostedEvents(server,0);
	// 清理待处理连接队列
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

	// 清理错误字符串
	if (server->errorString) {
		XString_delete_base(server->errorString);
		server->errorString = NULL;
	}

	// 释放地址结构（close 中已重置 serverAddress，这里再次 deinit 是安全的）
	XHostAddress_deinit_base(&server->serverAddress);
	XHostAddress_deinit_base(&server->lastAcceptedAddr);
	
	// 释放代理资源
	XNetworkProxy_deinit_base(&server->proxy);
	
	// 调用父类析构（XObject → XClass）
	XClass_Deinit_Parent(XObject, server);
}

// ==================== 构造与析构 ====================

void XTcpServer_init(XTcpServer* server)
{
	if (!server) return;

	// 初始化基类
	XObject_init(&server->base);

	// 初始化成员
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
	// 初始化临时存储的客户端地址
	XHostAddress_init(&server->lastAcceptedAddr);
	server->lastAcceptedPort = 0;

	// 设置虚函数表
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

// ==================== 核心功能 ====================

bool XTcpServer_listen(XTcpServer* server, const XHostAddress* address, uint16_t port)
{
	if (!server) return false;
	if (server->listening) return false;

	// 确保网络子系统已初始化
	XNetwork_ensureInit();

	// 使用默认地址
	XHostAddress listenAddr;
	XHostAddress_init(&listenAddr);
	if (address) {
		XHostAddress_copy_base(&listenAddr, address);
	} else {
		XHostAddress_setAddressSpecial(&listenAddr, XHostAddress_AnySpecial);
	}
	if (!server->d_ptr)
		server->d_ptr = XNetwork_createSocketPrivate(server);
	// 调用平台 API 创建服务器（支持代理）
	XServerHandle handle;
	if (server->proxy.type != XNetworkProxy_NoProxy && 
	    server->proxy.type != XNetworkProxy_DefaultProxy) {
		// 使用代理创建服务器
		handle = XNetwork_serverCreateWithProxy(server->d_ptr,
			&server->proxy, &listenAddr, port, server->listenBacklogSize, true);
	} else {
		// 普通服务器创建
		handle = XNetwork_serverCreate(server->d_ptr,
			&listenAddr, port, server->listenBacklogSize, true);
	}

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

	return true;
}

void XTcpServer_close(XTcpServer* server)
{
	if (!server) return;
	if (!server->listening) return;

	// 释放服务器套接字的 XNetworkSocketPrivate（会自动从事件循环注销）
	if (server->d_ptr) {
		XNetwork_deleteSocketPrivate(server->d_ptr);
		server->d_ptr = NULL;
	}

	// 关闭平台服务器
	XNetwork_serverClose(XTcpServer_socketDescriptor(server));

	// 更新状态
	//server->serverHandle = XSocketDescriptor_Invalid();
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

// ==================== 连接管理 ====================

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

// ==================== 套接字描述符 ====================

intptr_t XTcpServer_socketDescriptor(const XTcpServer* server)
{
	if (!server||!server->d_ptr) return -1;
	return XNetwork_socketHandle(server->d_ptr);
	//return XSocketDescriptor_toIntptr();
}

bool XTcpServer_setSocketDescriptor(XTcpServer* server, intptr_t socketDescriptor)
{
	if (!server) return false;
	if (socketDescriptor < 0) return false;

	// 如果已经在监听，先关闭
	if (server->listening) {
		XTcpServer_close(server);
	}

	// 创建 XNetworkSocketPrivate 用于异步事件通知
	XNetworkSocketPrivate* priv = XNetwork_createSocketPrivate(server);
	if (!priv) return false;

	// 设置描述符，注册到事件循环
	if (!XNetwork_socketSetDescriptor(priv, socketDescriptor,
			XAbstractSocket_ConnectedState, XIODevice_ReadOnly)) {
		XNetwork_deleteSocketPrivate(priv);
		return false;
	}

	// 保存状态
	server->d_ptr = priv;
	//server->serverHandle = XSocketDescriptor_fromIntptr(socketDescriptor);
	server->serverPort = XNetwork_serverPort(socketDescriptor);
	server->listening = true;

	return true;
}

// ==================== 连接处理 ====================

bool XTcpServer_waitForNewConnection(XTcpServer* server, int msec, bool* timedOut)
{
	if (!server || !server->listening) {
		if (timedOut) *timedOut = false;
		return false;
	}

	if (timedOut) *timedOut = false;

	// 如果已有待处理连接，直接返回
	if (XTcpServer_hasPendingConnections_base(server)) {
		return true;
	}

	// 如果 msec == 0，非阻塞，直接返回
	if (msec == 0) {
		if (timedOut) *timedOut = true;
		return false;
	}

	// 使用事件循环等待新连接
	uint64_t startTime = XDateTime_currentMSecsSinceEpoch();
	
	while (!XTcpServer_hasPendingConnections_base(server)) {
		// 处理事件
		XCoreApplication_processEvents(XEventLoop_AllEvents);
		
		// 检查超时
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
	return XClassGetVirtualFunc(server, EXTcpServer_HasPendingConnections,
		bool(*)(const XTcpServer*))(server);
}

XTcpSocket* XTcpServer_nextPendingConnection_base(XTcpServer* server)
{
	return XClassGetVirtualFunc(server, EXTcpServer_NextPendingConnection,
		XTcpSocket*(*)(XTcpServer*))(server);
}

// ==================== 错误处理 ====================

XAbstractSocket_SocketError XTcpServer_serverError(const XTcpServer* server)
{
	if (!server) return XAbstractSocket_UnknownSocketError;
	return server->lastError;
}

char* XTcpServer_errorString(const XTcpServer* server)
{
	if (!server) return NULL;
	if (!server->errorString) return NULL;

	// 返回新分配的字符串副本
	const char* str = XString_toUtf8(server->errorString);
	if (!str) return NULL;

	size_t len = strlen(str);
	char* result = (char*)XMalloc_System(len + 1);
	if (result) {
		memcpy(result, str, len + 1);
	}
	return result;
}

// ==================== 接受控制 ====================

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

// ==================== 代理设置 ====================

void XTcpServer_setProxy(XTcpServer* server, const XNetworkProxy* proxy)
{
	if (!server || !proxy) return;
	// 先释放旧的代理资源
	XNetworkProxy_deinit_base(&server->proxy);
	// 重新初始化并深拷贝
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

// ==================== 受保护的虚函数 ====================

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

	// 检查是否超过最大连接数
	currentCount = XVector_size_base(server->pendingConnections);
	if (currentCount >= server->maxPendingConnections) {
		// 超过最大连接数，关闭新连接
		XAbstractSocket_close_base((XAbstractSocket*)socket);
		XObject_deinitLater((XObject*)socket);
		return;
	}

	// 添加到队列
	sock = socket;
	XVector_push_back_1_base(server->pendingConnections, &sock);

	// 发射 newConnection 信号（Qt 6.8 行为）
	XTcpServer_newConnection_signal(server);

	// 发射 pendingConnectionAvailable 信号
	XTcpServer_pendingConnectionAvailable_signal(server);
}

// ==================== 信号实现 ====================

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