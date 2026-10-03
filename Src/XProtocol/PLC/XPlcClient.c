#include "XPlc_config.h"
#if XPROTOCOL_ON
#if XPLC_ON
#if XPLC_CORE_ON
#include "XPlcClient.h"
#include "XPlcClient_Protected.h"
#include "XMemory.h"
#include "XString.h"
#include "XVariant.h"
#include "XByteArray.h"
#include "XHashMap.h"
#include <string.h>

// 虚函数重载声明
static void VXPlcClient_deinit(XPlcClient* client);
static bool VXPlcClient_sendRequest(XPlcClient* client, const XByteArray* payload, XPlcReply* reply);
static bool VXPlcClient_processResponse(XPlcClient* client, const XByteArray* payload, XPlcReply* reply);

// ================== 虚表初始化 ==================
XVtable* XPlcClient_class_init(void)
{
    XVTABLE_INIT_DEFAULT(XPlcClient)
        // 继承 XPlcDevice
        XVTABLE_INHERIT_XCLASS(XPlcDevice);

    void* table[] = {
        VXPlcClient_sendRequest,
        VXPlcClient_processResponse
    };
    XVTABLE_ADD_FUNC_LIST_DEFAULT(table);

    // 重载析构
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Deinit, VXPlcClient_deinit);

    XCLASS_SHOW_SIZE_DEFAULT(XPlcClient);
    return XVTABLE_DEFAULT;
}

// ================== 构造/析构 ==================
XPlcClient* XPlcClient_create_ex(XMemoryType memory) {
    XPlcClient* client = (XPlcClient*)XMemory_malloc(sizeof(XPlcClient), memory);
    if (client) {
        XPlcClient_init(client);
        Set_Class_Memory(client, memory); Set_Class_IsHeap(client, true);
    }
    return client;
}

void XPlcClient_init(XPlcClient* client) {
    if (!client) return;

    // 初始化基类
    XPlcDevice_init((XPlcDevice*)client);
    XClassGetVtable(client) = XPlcClient_class_init();

    // 初始化成员变量（S7 握手比 Modbus 慢，默认超时 3000ms）
    client->m_timeout = 3000;
    client->m_numberOfRetries = 3;
    client->m_timeoutTimer = XTIMER_INVALID_ID;
    client->m_poolMap = NULL;

    // 初始化自动重连配置
    client->m_autoReconnect = false;       // 默认关闭
    client->m_reconnectInterval = 1000;    // 默认1秒
    client->m_maxReconnectAttempts = -1;   // 默认无限次
    client->m_reconnectAttempts = 0;       // 当前重连次数
    client->m_reconnectTimer = XTIMER_INVALID_ID;
}

static void VXPlcClient_deinit(XPlcClient* client) {
    if (!client) return;

    // 停止超时与重连定时器
    XPlcClient_timeoutTimerStop(client);
    XPlcClient_reconnectTimerStop(client);
    if (client->m_poolMap)
    {
        XClassDelete((XClass*)client->m_poolMap);
        client->m_poolMap = NULL;
    }
    // 调用基类析构
    XClass_Deinit_Parent(XPlcDevice, (XPlcDevice*)client);
}

// ================== 虚函数默认实现 ==================

/**
 * @brief 默认的 SendRequest 实现
 * @note 基类是抽象的，不提供默认发送功能，子类必须重写此槽
 */
static bool VXPlcClient_sendRequest(XPlcClient* client, const XByteArray* payload, XPlcReply* reply) {
    (void)client; (void)payload; (void)reply;
    return false;
}

/**
 * @brief 默认的 ProcessResponse 实现
 * @note 基类是抽象的，不提供默认解析功能，子类必须重写此槽
 */
static bool VXPlcClient_processResponse(XPlcClient* client, const XByteArray* payload, XPlcReply* reply) {
    (void)client; (void)payload; (void)reply;
    return false;
}

// ================== 虚函数查表分派入口 ==================

bool XPlcClient_sendRequest_base(XPlcClient* client, const XByteArray* payload, XPlcReply* reply)
{
    if (!client || !XClassGetVtable(client)) return false;
    return XClassGetVirtualFunc(client, EXPlcClient_SendRequest,
        bool(*)(XPlcClient*, const XByteArray*, XPlcReply*))(client, payload, reply);
}

bool XPlcClient_processResponse_base(XPlcClient* client, const XByteArray* payload, XPlcReply* reply)
{
    if (!client || !XClassGetVtable(client)) return false;
    return XClassGetVirtualFunc(client, EXPlcClient_ProcessResponse,
        bool(*)(XPlcClient*, const XByteArray*, XPlcReply*))(client, payload, reply);
}

// ================== Reply 创建辅助 ==================
XPlcReply* XPlcClient_createReply(XPlcClient* client, const XByteArray* request, XPlcReply_ReplyType type) {
    if (!client || !request) {
        return NULL;
    }
    XByteArray* copy = XByteArray_create_copy(request);
    if (!copy) return NULL;
    XPlcReply* reply = XPlcClient_createReply_ref(client, copy, type);
    if (!reply)
        XClassDelete((XClass*)copy);
    return reply;
}

XPlcReply* XPlcClient_createReply_move(XPlcClient* client, XByteArray* request, XPlcReply_ReplyType type)
{
    if (!client || !request) {
        return NULL;
    }
    XByteArray* moved = XByteArray_create_move(request);
    if (!moved) return NULL;
    XPlcReply* reply = XPlcClient_createReply_ref(client, moved, type);
    if (!reply)
        XClassDelete((XClass*)moved);
    return reply;
}

XPlcReply* XPlcClient_createReply_ref(XPlcClient* client, XByteArray* request, XPlcReply_ReplyType type)
{
    if (!client || !request) {
        return NULL;
    }

    // 创建 Reply 对象并挂接请求回带数据
    XPlcReply* reply = XPlcReply_create(type);
    if (!reply) return NULL;
    reply->m_request = request;
    return reply;
}

// ================== 配置 Getters/Setters ==================
size_t XPlcClient_timeout(const XPlcClient* client) {
    return client ? client->m_timeout : 3000;
}

void XPlcClient_setTimeout(XPlcClient* client, size_t newTimeout) {
    if (!client || client->m_timeout == newTimeout) return;
    client->m_timeout = newTimeout;
    XPlcClient_timeoutChanged_signal(client, (int)newTimeout);
}

int16_t XPlcClient_numberOfRetries(const XPlcClient* client) {
    return client ? client->m_numberOfRetries : 3;
}

void XPlcClient_setNumberOfRetries(XPlcClient* client, uint8_t number) {
    if (!client) return;
    client->m_numberOfRetries = number;
}

// ================== 自动重连配置 API ==================
bool XPlcClient_autoReconnect(const XPlcClient* client) {
    return client ? client->m_autoReconnect : false;
}

void XPlcClient_setAutoReconnect(XPlcClient* client, bool enabled) {
    if (!client) return;
    client->m_autoReconnect = enabled;

    // 如果禁用自动重连，停止正在进行的重连定时器
    if (!enabled && client->m_reconnectTimer != XTIMER_INVALID_ID) {
        XObject_killTimer((XObject*)client, client->m_reconnectTimer);
        client->m_reconnectTimer = XTIMER_INVALID_ID;
    }
}

size_t XPlcClient_reconnectInterval(const XPlcClient* client) {
    return client ? client->m_reconnectInterval : 1000;
}

void XPlcClient_setReconnectInterval(XPlcClient* client, size_t interval) {
    if (!client) return;
    client->m_reconnectInterval = interval;
}

int16_t XPlcClient_maxReconnectAttempts(const XPlcClient* client) {
    return client ? client->m_maxReconnectAttempts : -1;
}

void XPlcClient_setMaxReconnectAttempts(XPlcClient* client, int16_t attempts) {
    if (!client) return;
    client->m_maxReconnectAttempts = attempts;
}

int16_t XPlcClient_reconnectAttempts(const XPlcClient* client) {
    return client ? client->m_reconnectAttempts : 0;
}

// ================== 受保护的定时器 API ==================

void XPlcClient_timeoutTimerStop(XPlcClient* client)
{
    if (!client) return;
    if (client->m_timeoutTimer != XTIMER_INVALID_ID)
    {
        XObject_killTimer((XObject*)client, client->m_timeoutTimer);
        client->m_timeoutTimer = XTIMER_INVALID_ID;
    }
}

void XPlcClient_timeoutTimerStart(XPlcClient* client)
{
    if (!client) return;
    XPlcClient_timeoutTimerStop(client);
    client->m_timeoutTimer = XObject_startTimer_ms((XObject*)client,
        XPlcClient_timeout(client), XTimerType_CoarseTimer);
}

void XPlcClient_reconnectTimerStop(XPlcClient* client)
{
    if (!client) return;
    if (client->m_reconnectTimer != XTIMER_INVALID_ID)
    {
        XObject_killTimer((XObject*)client, client->m_reconnectTimer);
        client->m_reconnectTimer = XTIMER_INVALID_ID;
    }
}

void XPlcClient_reconnectTimerStart(XPlcClient* client)
{
    if (!client) return;
    XPlcClient_reconnectTimerStop(client);
    client->m_reconnectTimer = XObject_startTimer_ms((XObject*)client,
        XPlcClient_reconnectInterval(client), XTimerType_CoarseTimer);
}

// ================== 信号 ==================
void* XPlcClient_timeoutChanged_signal(XPlcClient* client, int newTimeout) {
    XEmitSignal((XObject*)client, XPlcClient_timeoutChanged_signal,
        XVarList_Create(XVar(int, newTimeout)),
        NULL, NULL, XEVENT_PRIORITY_NORMAL);
}

#endif /* XPLC_CORE_ON */
#endif /* XPLC_ON */
#endif /* XPROTOCOL_ON */
