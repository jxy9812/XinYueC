/**
 * @file       XModbusDigitalOutput.c
 * @brief      Modbus 写单线圈（FC05）通道适配器实现。
 * @details    write 组装 XModbusCoils 单点数据单元发 FC05，不等应答即返回
 *             true；应答经 finished 信号回收，成功同步发射 writeDone(true)
 *             并置 valid，失败报错并发射 writeDone(false)。从站地址 0 按广播
 *             处理（受理即完成）。主站借用不拥有；本适配器发出的 Reply 由
 *             自己 deleteLater 释放（对齐 XVfdModbusLink）。
 */
#include "XModbusDigitalOutput.h"

#if XINDUSTRY_ON && XINDUSTRY_CHANNEL_ON && XINDUSTRY_MODBUS_ON

#include <string.h>
#include "XMemory.h"
#include "XEvent.h"
#include "XIoChannel_Protected.h"

/* ==================== 内部实现（static，仅供本文件使用） ==================== */

/* 应答槽前置声明：释放工具需要取其地址断开 finished 连接。 */
static void VXModbusDigitalOutput_onReplyFinished(XObject* receiver, XVarList* args);

/**
 * @brief 把 Modbus 设备错误映射为通道层错误码。
 * @param error Modbus 设备错误码。
 * @return 对应的 XIoChannelError 枚举值；超时映射为 Timeout，其余非零错误
 *         一律映射为 Hardware。
 */
static int modbusDoMapError(XModbusDevice_Error error)
{
    if (error == XModbusDevice_NoError) return XIoChannelError_None;
    if (error == XModbusDevice_TimeoutError) return XIoChannelError_Timeout;
    return XIoChannelError_Hardware;
}

/**
 * @brief 释放一个登记在途的应答：先断开本对象槽连接再延迟删除。
 * @param self 目标适配器；不能为 NULL。
 * @note 断开在前，保证应答晚到/对象先亡时不回调进半析构的适配器。
 */
static void modbusDoDropReply(XModbusDigitalOutput* self, XModbusReply* reply)
{
    if (!reply) return;
    XObject_disconnect_1((XObject*)reply, XSignal(XModbusReply_finished_signal),
                         (XObject*)self, VXModbusDigitalOutput_onReplyFinished);
    XObject_deleteLater((XObject*)reply);
}

/* ==================== 应答槽（经 connect_1 连接） ==================== */

/**
 * @brief 写应答完成槽：按 sender 取回真正的 Reply，发 writeDone 并释放。
 * @param receiver 连接的接收者（即本适配器）；**不是** Reply，禁止直接强转。
 * @param args 信号参数（未使用）。
 * @return 无。
 * @note connect_1 第 1 参是 receiver；真正的发送者（XModbusReply*）必须经
 *       XObject_sender(receiver) 取回——对齐 XVfdModbusLink 的推荐写法。
 */
static void VXModbusDigitalOutput_onReplyFinished(XObject* receiver, XVarList* args)
{
    XModbusDigitalOutput* self;
    XModbusReply* reply;
    XIoChannel* io;
    bool ok;
    (void)args;
    self = (XModbusDigitalOutput*)receiver;
    if (!self) return;
    reply = (XModbusReply*)XObject_sender(receiver);
    if (!reply) return;
    io = &self->m_class.m_class;
    if (self->m_pendingReply == reply) self->m_pendingReply = NULL;

    ok = (XModbusReply_error(reply) == XModbusDevice_NoError);
    if (ok) {
        XIoChannel_clearError(io);
        XIoChannel_setValid(io, true);
    } else {
        int error = modbusDoMapError(XModbusReply_error(reply));
        if (XIoChannel_lastError(io) != error) {
            XIoChannel_setError(io, error);
        }
    }
    XDigitalOutput_writeDone_signal(&self->m_class, ok);
    XObject_deleteLater((XObject*)reply);
}

/* ==================== 虚函数实现（static，仅供 class_init 注册） ==================== */

static bool VXModbusDigitalOutput_write(XModbusDigitalOutput* self, bool active)
{
    XIoChannel* io;
    XModbusDataUnit* unit;
    XModbusReply* reply;
    if (!self) return false;
    io = &self->m_class.m_class;
    if (!self->m_client) {
        XIoChannel_setError(io, XIoChannelError_NotAttached);
        return false;
    }
    if (self->m_pendingReply) {
        /* 同一时刻只允许一个在途写应答；在途期间的新写入按忙碌拒绝。 */
        if (XIoChannel_lastError(io) != (int)XIoChannelError_Busy) {
            XIoChannel_setError(io, XIoChannelError_Busy);
        }
        return false;
    }
    unit = XModbusDataUnit_create_ex(XCLASS_DEFAULT_MEMORY_TYPE,
                                     XModbusCoils, (uint16_t)self->m_address, 1);
    if (!unit) {
        XIoChannel_setError(io, XIoChannelError_Unknown);
        return false;
    }
    XModbusDataUnit_setValue(unit, 0, active ? 1 : 0);
    reply = XModbusClient_sendWriteRequest(self->m_client, unit,
                                           self->m_serverAddress,
                                           XModbusRequestPriority_Normal);
    XClassDelete((XClass*)unit); /* 请求构建为值拷贝，单元即刻释放。 */
    if (!reply) {
        XIoChannel_setError(io, XIoChannelError_Hardware);
        return false;
    }
    if (self->m_serverAddress == 0) {
        /* 广播：无从站应答可等，受理即完成（对齐 XVfdModbusLink 广播语义）。 */
        XObject_deleteLater((XObject*)reply);
        XIoChannel_clearError(io);
        XIoChannel_setValid(io, true);
        XDigitalOutput_writeDone_signal(&self->m_class, true);
        return true;
    }
    self->m_pendingReply = reply;
    XObject_connect_1((XObject*)reply, XSignal(XModbusReply_finished_signal),
                      (XObject*)self, VXModbusDigitalOutput_onReplyFinished,
                      XConnectionType_Direct);
    return true; /* 受理语义：完成确认经 writeDone 异步冒泡。 */
}

static void VXModbusDigitalOutput_deinit(XModbusDigitalOutput* self)
{
    if (!self) return;
    if (self->m_pendingReply) {
        modbusDoDropReply(self, self->m_pendingReply);
        self->m_pendingReply = NULL;
    }
    self->m_client = NULL; /* 借用资源，不关闭不释放，仅清除引用。 */
    XClass_Deinit_Parent(XDigitalOutput, (XDigitalOutput*)self);
}

/* ==================== 虚函数表初始化（单例） ==================== */

XVtable* XModbusDigitalOutput_class_init(void)
{
    XVTABLE_INIT_DEFAULT(XModbusDigitalOutput)
    XVTABLE_INHERIT_XCLASS(XDigitalOutput);
    XVTABLE_OVERLOAD_DEFAULT(EXDigitalOutput_Write, VXModbusDigitalOutput_write);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Deinit, VXModbusDigitalOutput_deinit);
    return XVTABLE_DEFAULT;
}

/* ==================== 构造函数 ==================== */

void XModbusDigitalOutput_init(XModbusDigitalOutput* obj)
{
    if (!obj) return;
    memset(obj, 0, sizeof(XModbusDigitalOutput));
    XDigitalOutput_init(&obj->m_class);
    XClassSetVtable(obj, XModbusDigitalOutput);
    obj->m_client = NULL;
    obj->m_serverAddress = 0;
    obj->m_address = 0;
    obj->m_pendingReply = NULL;
}

bool XModbusDigitalOutput_attach(XModbusDigitalOutput* obj, XModbusClient* client,
                                 int serverAddress, int address)
{
    if (!obj || !client || serverAddress < 0 || serverAddress > 247 ||
        address < 0 || address > 65535) {
        XERROR_PRINTF("XModbusDigitalOutput_attach: 参数非法\n");
        return false;
    }
    if (obj->m_pendingReply) {
        modbusDoDropReply(obj, obj->m_pendingReply); /* 重新挂接：在途旧应答丢弃。 */
    }
    obj->m_client = client;
    obj->m_serverAddress = serverAddress;
    obj->m_address = address;
    obj->m_pendingReply = NULL;
    return true;
}

XModbusDigitalOutput* XModbusDigitalOutput_create_ex(XModbusClient* client, int serverAddress,
                                                     int address)
{
    XModbusDigitalOutput* obj =
        (XModbusDigitalOutput*)XMalloc_System(sizeof(XModbusDigitalOutput));
    if (!obj) return NULL;
    XModbusDigitalOutput_init(obj);
    Set_Class_IsHeap(obj, true);
    if (!XModbusDigitalOutput_attach(obj, client, serverAddress, address)) {
        XClassDelete(obj);
        return NULL;
    }
    return obj;
}

#endif /* XINDUSTRY_ON && XINDUSTRY_CHANNEL_ON && XINDUSTRY_MODBUS_ON */
