/**
 * @file XVfdModbusLink.c
 * @brief Modbus 链路适配器实现（请求提交 + 应答路由 + 忙碌判定）。
 */
#include "XVfdModbusLink.h"
#if XVFD_ON
#if XVFD_LINK_ON

#include "XModbusReply.h"
#include "XVarList.h"
#include <string.h>

/* =============== 内部工具 =============== */
static XVfdModbusPending* mbTakePendingSlot(XVfdModbusLink* link)
{
    int i;
    for (i = 0; i < XVFD_MODBUS_LINK_PENDING_CAP; ++i) {
        if (!link->m_pending[i].m_used) return &link->m_pending[i];
    }
    return NULL;
}

static XVfdModbusPending* mbFindPendingByReply(XVfdModbusLink* link, XModbusReply* reply)
{
    int i;
    for (i = 0; i < XVFD_MODBUS_LINK_PENDING_CAP; ++i) {
        if (link->m_pending[i].m_used && link->m_pending[i].m_reply == reply)
            return &link->m_pending[i];
    }
    return NULL;
}

/**
 * @brief 应答统一收口：路由登记回调 → 健康回报 → 释放条目与应答对象。
 * @note connect_1 语义：第一参 receiver=本链路；真发送者（XModbusReply*）
 *       经 XObject_sender(receiver) 取回（约束文档推荐写法，规避把 receiver
 *       当 sender 强转的错位陷阱）。
 */
static void mbOnReplyFinished(XObject* receiver, XVarList* args)
{
    XVfdModbusLink* link;
    XModbusReply* reply;
    XVfdModbusPending* entry;
    bool ok;
    const XModbusDataUnit* result;
    (void)args;
    link = (XVfdModbusLink*)receiver;
    if (!link) return;
    reply = (XModbusReply*)XObject_sender(receiver);
    if (!reply) return;
    entry = mbFindPendingByReply(link, reply);
    if (!entry) return;

    ok = (XModbusReply_error(reply) == XModbusDevice_NoError);
    result = ok ? XModbusReply_result_const(reply) : NULL;
    if (entry->m_done) {
        entry->m_done(entry->m_owner, entry->m_userData, ok, result);
    }
    XVfdLink_reportResult(&link->m_base, entry->m_owner, entry->m_unitId, ok);
    memset(entry, 0, sizeof(XVfdModbusPending));
    if (link->m_outstanding > 0) --link->m_outstanding;
    XObject_deleteLater((XObject*)reply);
}

/* =============== 虚函数表 / 生命周期 =============== */
XVtable* XVfdModbusLink_class_init(void)
{
    XVTABLE_INIT_DEFAULT(XVfdModbusLink)
    XVTABLE_INHERIT_XCLASS(XVfdLink);
    XCLASS_SET_CLASS_NAME_DEFAULT("XVfdModbusLink");
    XCLASS_SHOW_SIZE_DEFAULT(XVfdModbusLink);
    return XVTABLE_DEFAULT;
}

XVfdModbusLink* XVfdModbusLink_create_ex(XMemoryType memory)
{
    XVfdModbusLink* link = (XVfdModbusLink*)XMemory_malloc(sizeof(XVfdModbusLink), memory);
    if (!link) return NULL;
    XVfdModbusLink_init(link);
    Set_Class_Memory(link, memory);
    Set_Class_IsHeap(link, true);
    return link;
}

void XVfdModbusLink_init(XVfdModbusLink* link)
{
    if (!link) return;
    memset(link, 0, sizeof(XVfdModbusLink));
    XVfdLink_init(&link->m_base);
    XClassGetVtable(link) = XVfdModbusLink_class_init();
}

/* =============== 客户端绑定 =============== */
bool XVfdModbusLink_setClient(XVfdModbusLink* link, XModbusClient* client)
{
    if (!link || !client) return false;
    link->m_client = client;
    return true;
}

XModbusClient* XVfdModbusLink_client_const(const XVfdModbusLink* link)
{
    return link ? link->m_client : NULL;
}

/* =============== 请求提交 =============== */
bool XVfdModbusLink_submitRead(XVfdModbusLink* link, XObject* owner, int unitId,
                               uint16_t address, uint16_t count,
                               XVfdModbusLinkDoneFunc done, void* userData,
                               XModbusRequestPriority priority)
{
    XModbusDataUnit* unit;
    XModbusReply* reply;
    XVfdModbusPending* entry;
    if (!link || !owner || !done || !link->m_client) return false;
    if (unitId < 0 || unitId > 247 || count == 0 || count > 125) return false;
    if (link->m_outstanding >= XVFD_MODBUS_LINK_PENDING_CAP) return false;
    unit = XModbusDataUnit_create_ex(XCLASS_DEFAULT_MEMORY_TYPE,
                                     XModbusHoldingRegisters, address, (size_t)count);
    if (!unit) return false;
    reply = XModbusClient_sendReadRequest(link->m_client, unit, unitId, priority);
    XClassDelete((XClass*)unit);
    if (!reply) return false;
    entry = mbTakePendingSlot(link);
    if (!entry) {
        XObject_deleteLater((XObject*)reply);
        return false;
    }
    entry->m_reply = reply;
    entry->m_owner = owner;
    entry->m_userData = userData;
    entry->m_done = done;
    entry->m_unitId = unitId;
    entry->m_used = true;
    ++link->m_outstanding;
    XObject_connect_1((XObject*)reply, XSignal(XModbusReply_finished_signal),
                      (XObject*)link, mbOnReplyFinished, XConnectionType_Direct);
    return true;
}

bool XVfdModbusLink_submitWriteSingle(XVfdModbusLink* link, XObject* owner, int unitId,
                                      uint16_t address, uint16_t value,
                                      XVfdModbusLinkDoneFunc done, void* userData,
                                      XModbusRequestPriority priority)
{
    XModbusDataUnit* unit;
    XModbusReply* reply;
    XVfdModbusPending* entry;
    if (!link || !owner || !done || !link->m_client) return false;
    if (unitId < 0 || unitId > 247) return false;
    if (link->m_outstanding >= XVFD_MODBUS_LINK_PENDING_CAP) return false;
    unit = XModbusDataUnit_create_ex(XCLASS_DEFAULT_MEMORY_TYPE,
                                     XModbusHoldingRegisters, address, 1);
    if (!unit) return false;
    XModbusDataUnit_setValue(unit, 0, value);
    reply = XModbusClient_sendWriteRequest(link->m_client, unit, unitId, priority);
    XClassDelete((XClass*)unit);
    if (!reply) return false;

    if (unitId == 0) {
        /* 广播：无应答可等，立即收口（ok），不进登记表；链路健康不记账。 */
        XObject_deleteLater((XObject*)reply);
        done(owner, userData, true, NULL);
        return true;
    }
    entry = mbTakePendingSlot(link);
    if (!entry) {
        XObject_deleteLater((XObject*)reply);
        return false;
    }
    entry->m_reply = reply;
    entry->m_owner = owner;
    entry->m_userData = userData;
    entry->m_done = done;
    entry->m_unitId = unitId;
    entry->m_used = true;
    ++link->m_outstanding;
    XObject_connect_1((XObject*)reply, XSignal(XModbusReply_finished_signal),
                      (XObject*)link, mbOnReplyFinished, XConnectionType_Direct);
    return true;
}

bool XVfdModbusLink_isBusy(const XVfdModbusLink* link)
{
    return link ? (link->m_outstanding > 0) : true;
}

int XVfdModbusLink_outstanding(const XVfdModbusLink* link)
{
    return link ? link->m_outstanding : 0;
}

#endif /* XVFD_LINK_ON */
#endif /* XVFD_ON */
