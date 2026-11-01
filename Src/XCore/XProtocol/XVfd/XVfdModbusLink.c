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

/**
 * @brief 孤儿应答回收：cancelOwner 摘除业务收口后，应答在底层客户端侧自然终结
 *        （应答到达/超时/断开收口均为恰好一次 finished）时仅释放应答对象，
 *        不再回调已解绑 owner、不做健康记账。
 * @note  不能在 cancelOwner 现场直接终结/deleteLater 应答：排队中或在途的应答
 *        仍被 XModbusClient 发送队列/m_currentReply 引用，先行释放会在客户端
 *        随后的超时/收口路径造成悬垂解引用；故改为挂本钩子延迟到自然终结。
 */
static void mbOnOrphanReplyFinished(XObject* receiver, XVarList* args)
{
    XVfdModbusLink* link;
    XModbusReply* reply;
    (void)args;
    link = (XVfdModbusLink*)receiver;
    if (!link) return;
    reply = (XModbusReply*)XObject_sender(receiver);
    if (!reply) return;
    XObject_deleteLater((XObject*)reply);
}

/* =============== 在途登记清理 =============== */
int XVfdModbusLink_cancelOwner(XVfdModbusLink* link, XObject* owner)
{
    int i;
    int cancelled = 0;
    if (!link || !owner) return 0;
    for (i = 0; i < XVFD_MODBUS_LINK_PENDING_CAP; ++i) {
        XVfdModbusPending* entry = &link->m_pending[i];
        if (!entry->m_used || entry->m_owner != owner) continue;
        if (entry->m_reply) {
            if (XModbusReply_isFinished(entry->m_reply)) {
                /* 防御兜底：已终态应答不会再发 finished，孤儿钩子（等的是
                 * 未来的 finished）永不触发→同源泄漏；终态后底层客户端按
                 * A5 不变式不再解引用，就地同步释放（与析构路径同口径）。 */
                XClassDelete((XClass*)entry->m_reply);
            } else {
                /* 断开业务收口连接、改挂孤儿回收钩子：此后该 owner 的应答不再
                 * 回调；应答对象延迟到底层客户端自然终结时释放（见钩子注释）。 */
                XObject_disconnect_1((XObject*)entry->m_reply,
                                     XSignal(XModbusReply_finished_signal),
                                     (XObject*)link, mbOnReplyFinished);
                XObject_connect_1((XObject*)entry->m_reply,
                                  XSignal(XModbusReply_finished_signal),
                                  (XObject*)link, mbOnOrphanReplyFinished,
                                  XConnectionType_Direct);
            }
        }
        memset(entry, 0, sizeof(XVfdModbusPending));
        ++cancelled;
    }
    if (cancelled > 0) {
        if (link->m_outstanding >= cancelled) link->m_outstanding -= cancelled;
        else link->m_outstanding = 0;
    }
    return cancelled;
}

/* =============== 虚函数表 / 生命周期 =============== */

/** @brief 析构收口：清空在途登记表，防止链路销毁后遗留孤儿条目 */
static void VXVfdModbusLink_deinit(XObject* obj)
{
    XVfdModbusLink* link = (XVfdModbusLink*)obj;
    int i;
    if (!link) return;
    for (i = 0; i < XVFD_MODBUS_LINK_PENDING_CAP; ++i) {
        XVfdModbusPending* entry = &link->m_pending[i];
        if (!entry->m_used) continue;
        if (entry->m_reply) {
            if (XModbusReply_isFinished(entry->m_reply)) {
                /* 已终结应答：底层客户端按 A5 不变式不再解引用终态应答，
                 * 析构路径按约束文档用同步释放，杜绝孤儿泄漏。 */
                XClassDelete((XClass*)entry->m_reply);
            }
            /* 未终结应答仍被底层客户端发送队列/m_currentReply 引用，此处
             * 同步释放会与客户端随后的超时/收口路径冲突（UAF），只能随其
             * 自然终结泄漏（与历史行为一致，见 cancelOwner 孤儿钩子注释）。 */
        }
        memset(entry, 0, sizeof(XVfdModbusPending));
    }
    link->m_outstanding = 0;
    XClass_Parent(XVfdLink, EXClass_Deinit, void (*)(XObject*))((XObject*)link);
}

XVtable* XVfdModbusLink_class_init(void)
{
    XVTABLE_INIT_DEFAULT(XVfdModbusLink)
    XVTABLE_INHERIT_XCLASS(XVfdLink);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Deinit, VXVfdModbusLink_deinit);
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
    /* [A3 时序缺口兜底] reply 可能在 send 内部即同步终态（未连接/io 关闭/
     * 忙/空 PDU/发送失败等收口分支，setError 同步发射 finished）——此刻本
     * 链路尚未 connect，finished 发射给空监听集。若照旧登记条目，该条目
     * 永不收口：m_outstanding 不减、isBusy 恒真（XVfdModbusBase_PollDue
     * 无连接态门槛）→ 设备单飞永久卡死（串口拔插即触发）。故 send 返回后
     * 先查终态：已终态就地走 done+健康回报等价收口，不登记。 */
    if (XModbusReply_isFinished(reply)) {
        const bool okSync = (XModbusReply_error(reply) == XModbusDevice_NoError);
        const XModbusDataUnit* resultSync =
            okSync ? XModbusReply_result_const(reply) : NULL;
        done(owner, userData, okSync, resultSync);
        XVfdLink_reportResult(&link->m_base, owner, unitId, okSync);
        XObject_deleteLater((XObject*)reply);
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

    /* [A3 时序缺口兜底] 同 submitRead：send 内部同步终态的 reply 就地
     * done+健康回报，不登记条目（否则单飞永久卡死）。本检查必须先于广播
     * 分支：断链时广播写同样同步终态，若先走广播分支会向调用方假报 ok。 */
    if (XModbusReply_isFinished(reply)) {
        const bool okSync = (XModbusReply_error(reply) == XModbusDevice_NoError);
        const XModbusDataUnit* resultSync =
            okSync ? XModbusReply_result_const(reply) : NULL;
        done(owner, userData, okSync, resultSync);
        XVfdLink_reportResult(&link->m_base, owner, unitId, okSync);
        XObject_deleteLater((XObject*)reply);
        return true;
    }

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
