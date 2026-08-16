#include "XMqtt_config.h"
#if XPROTOCOL_ON
#if XMQTT_ON
#if XMQTT_SERVER_ON
#include "XMqttTcpServer.h"
#include "XMemory.h"
#include "XSignalSlot.h"
#include "XMap.h"
#include "XString.h"
#include <string.h>

/* ==================== 虚函数前置声明 ==================== */
static bool V_sendData(XMqttServer* base, void* transport,
                       const uint8_t* data, size_t size);
static void V_closeClient(XMqttServer* base, void* transport);
static void V_deinit(XMqttServer* base);

/* ==================== 槽函数前置声明 ==================== */
static void mqtt_tcp_on_new_connection(XObject* receiver, XVarList* args);
static void mqtt_tcp_on_ready_read(XObject* receiver, XVarList* args);
static void mqtt_tcp_on_disconnected(XObject* receiver, XVarList* args);

/* ==================== 内部工具 ==================== */

/**
 * @brief 获取当前信号的发送者（客户端套接字）。
 * @param server MQTT TCP 服务器实例。
 * @return 对应的 XTcpSocket 指针，失败返回 NULL。
 */
static XTcpSocket* mqtt_tcp_sender_socket(XMqttTcpServer* server)
{
    XObject* sender;
    if (!server) return NULL;
    sender = XObject_sender((XObject*)server);
    return (XTcpSocket*)sender;
}

/**
 * @brief 断开并销毁一个客户端套接字。
 * @details 先断开服务器与套接字之间的信号连接，避免重复处理断开事件，
 *          再调用协议层结束连接、关闭底层套接字并延迟销毁套接字对象。
 * @param server MQTT TCP 服务器实例。
 * @param socket 要关闭的客户端套接字。
 */
static void mqtt_tcp_drop_client(XMqttTcpServer* server, XTcpSocket* socket)
{
    if (!server || !socket) return;
    XObject_disconnect_1((XObject*)socket, 0, (XObject*)server, NULL);
    XMqttServer_endClient((XMqttServer*)server, socket);
    XAbstractSocket_disconnectFromHost_base((XAbstractSocket*)socket);
    XTcpSocket_deleteLater(socket);
}

/* ==================== 虚函数实现 ==================== */

/**
 * @brief 向客户端套接字写入原始字节（重写 XMqttServer::sendData）。
 * @return 全部字节写入成功返回 true。
 */
static bool V_sendData(XMqttServer* base, void* transport,
                       const uint8_t* data, size_t size)
{
    XTcpSocket* socket = (XTcpSocket*)transport;
    int64_t written;
    if (!base || !socket || (!data && size)) return false;
    if (size > (size_t)INT64_MAX) return false;
    written = XTcpSocket_write_1(socket, (const char*)data, (int64_t)size);
    return written == (int64_t)size;
}

/**
 * @brief 主动断开客户端（重写 XMqttServer::closeClient）。
 * @details 协议违规、保活超时、同 clientId 抢占等场景由引擎调用本函数。
 *          断开会触发套接字 disconnected 信号，由槽函数完成状态清理。
 */
static void V_closeClient(XMqttServer* base, void* transport)
{
    XTcpSocket* socket = (XTcpSocket*)transport;
    if (!base || !socket) return;
    XAbstractSocket_disconnectFromHost_base((XAbstractSocket*)socket);
}

/**
 * @brief 析构虚函数：关闭 TCP 服务器并释放客户端映射。
 */
static void V_deinit(XMqttServer* base)
{
    XMqttTcpServer* server = (XMqttTcpServer*)base;
    if (!server) return;
    if (server->m_tcpServer) {
        XTcpServer_close(server->m_tcpServer);
        XTcpServer_deleteLater(server->m_tcpServer);
        server->m_tcpServer = NULL;
    }
    if (server->m_connectedClients) {
        XMapBase_delete_base((XMapBase*)server->m_connectedClients);
        server->m_connectedClients = NULL;
    }
    XClass_Deinit_Parent(XMqttServer, server);
}

/* ==================== 类初始化 ==================== */

XVtable* XMqttTcpServer_class_init(void)
{
    XVTABLE_INIT_DEFAULT(XMqttTcpServer)
    XVTABLE_INHERIT_XCLASS(XMqttServer);
    XVTABLE_OVERLOAD_DEFAULT(EXMqttServer_SendData, V_sendData);
    XVTABLE_OVERLOAD_DEFAULT(EXMqttServer_CloseClient, V_closeClient);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Deinit, V_deinit);
    return XVTABLE_DEFAULT;
}

XMqttTcpServer* XMqttTcpServer_create_ex(XMemoryType memory)
{
    XMqttTcpServer* server = (XMqttTcpServer*)XMemory_malloc(sizeof(XMqttTcpServer), memory);
    if (server) {
        XMqttTcpServer_init(server);
        Set_Class_Memory(server, memory); Set_Class_IsHeap(server, true);
    }
    return server;
}

void XMqttTcpServer_init(XMqttTcpServer* server)
{
    if (!server) return;
    XMqttServer_init((XMqttServer*)server);
    XClassGetVtable(server) = XMqttTcpServer_class_init();
    server->m_tcpServer = XTcpServer_create();
    if (server->m_tcpServer) {
        XObject_connect_1((XObject*)server->m_tcpServer,
                          XSignal(XTcpServer_newConnection_signal),
                          (XObject*)server,
                          mqtt_tcp_on_new_connection,
                          XConnectionType_Auto);
    }
    server->m_connectedClients = XMap_Create(XTcpSocket*, uint8_t, uintptr_t_compare);
}

/* ==================== TCP 服务器控制 ==================== */

bool XMqttTcpServer_listen(XMqttTcpServer* server, const XHostAddress* address,
                           uint16_t port)
{
    if (!server || !server->m_tcpServer) return false;
    return XTcpServer_listen(server->m_tcpServer, address, port);
}

void XMqttTcpServer_close(XMqttTcpServer* server)
{
    XMap* clients;
    XVector* keys;
    if (!server) return;
    clients = server->m_connectedClients;
    if (clients && XMapBase_size_base((XMapBase*)clients) > 0) {
        keys = XMapBase_keys_base((XMapBase*)clients);
        if (keys) {
            for (size_t i = 0; i < XVector_size_base(keys); ++i) {
                XTcpSocket** slot = (XTcpSocket**)XVector_at_base(keys, (int64_t)i);
                XTcpSocket* socket = slot ? *slot : NULL;
                if (socket)
                    mqtt_tcp_drop_client(server, socket);
            }
            XVector_delete_base(keys);
        }
        XMapBase_clear_base((XMapBase*)clients);
    }
    if (server->m_tcpServer)
        XTcpServer_close(server->m_tcpServer);
}

bool XMqttTcpServer_isListening(const XMqttTcpServer* server)
{
    return server && server->m_tcpServer ? XTcpServer_isListening(server->m_tcpServer) : false;
}

uint16_t XMqttTcpServer_serverPort(const XMqttTcpServer* server)
{
    return server && server->m_tcpServer ? XTcpServer_serverPort(server->m_tcpServer) : 0;
}

int XMqttTcpServer_connectedClientCount(const XMqttTcpServer* server)
{
    return server && server->m_connectedClients ?
        (int)XMapBase_size_base((XMapBase*)server->m_connectedClients) : 0;
}

/* ==================== 槽函数 ==================== */

/**
 * @brief 处理 TCP 服务器 newConnection 信号：接受全部待处理连接。
 */
static void mqtt_tcp_on_new_connection(XObject* receiver, XVarList* args)
{
    XMqttTcpServer* server = (XMqttTcpServer*)receiver;
    (void)args;
    if (!server || !server->m_tcpServer || !server->m_connectedClients) return;
    while (XTcpServer_hasPendingConnections_base(server->m_tcpServer)) {
        XTcpSocket* socket = XTcpServer_nextPendingConnection_base(server->m_tcpServer);
        uint8_t marker = 1;
        if (!socket) continue;
        if (!XMapBase_insert_base((XMapBase*)server->m_connectedClients, &socket, &marker)) {
            XTcpSocket_close_base(socket);
            XTcpSocket_deleteLater(socket);
            continue;
        }
        if (!XMqttServer_beginClient((XMqttServer*)server, socket)) {
            XMapBase_remove_base((XMapBase*)server->m_connectedClients, &socket);
            XTcpSocket_close_base(socket);
            XTcpSocket_deleteLater(socket);
            continue;
        }
        XObject_connect_1((XObject*)socket,
                          XSignal(XTcpSocket_readyRead_signal),
                          (XObject*)server,
                          mqtt_tcp_on_ready_read,
                          XConnectionType_Auto);
        XObject_connect_1((XObject*)socket,
                          XSignal(XTcpSocket_disconnected_signal),
                          (XObject*)server,
                          mqtt_tcp_on_disconnected,
                          XConnectionType_Auto);
    }
}

/**
 * @brief 处理客户端 readyRead 信号：读取全部可用字节交给协议引擎。
 */
static void mqtt_tcp_on_ready_read(XObject* receiver, XVarList* args)
{
    XMqttTcpServer* server = (XMqttTcpServer*)receiver;
    XTcpSocket* socket;
    XByteArray* bytes;
    (void)args;
    if (!server) return;
    socket = mqtt_tcp_sender_socket(server);
    if (!socket) return;
    bytes = XTcpSocket_readAll_2(socket);
    if (bytes) {
        XMqttServer_feedData((XMqttServer*)server, socket,
                             (const uint8_t*)XByteArray_constData(bytes),
                             (size_t)XByteArray_size_base(bytes));
        XByteArray_delete_base(bytes);
    }
}

/**
 * @brief 处理客户端 disconnected 信号：清理协议状态与客户端映射。
 */
static void mqtt_tcp_on_disconnected(XObject* receiver, XVarList* args)
{
    XMqttTcpServer* server = (XMqttTcpServer*)receiver;
    XTcpSocket* socket;
    (void)args;
    if (!server || !server->m_connectedClients) return;
    socket = mqtt_tcp_sender_socket(server);
    if (!socket) return;
    XMqttServer_endClient((XMqttServer*)server, socket);
    XMapBase_remove_base((XMapBase*)server->m_connectedClients, &socket);
    XObject_disconnect_1((XObject*)socket, 0, (XObject*)server, NULL);
    XTcpSocket_deleteLater(socket);
}

#endif /* XMQTT_SERVER_ON */
#endif /* XMQTT_ON */
#endif /* XPROTOCOL_ON */
