/**
 * @file       XModbusDigitalInput.c
 * @brief      Modbus 离散量输入（FC02）轮询通道适配器实现。
 * @details    定时器拍发起 FC02 单点读（XModbusDiscreteInputs 数据单元，
 *             quantity=1）；应答经 finished 信号回收：值翻转/首拍推送
 *             inputChanged，失败/超时报错清 valid，恢复后清错误置 valid。
 *             上一拍在途时本拍跳过，不叠加请求。主站借用不拥有；本适配器
 *             发出的 Reply 由自己 deleteLater 释放（对齐 XVfdModbusLink）。
 */
#include "XModbusDigitalInput.h"

#if XINDUSTRY_ON && XINDUSTRY_CHANNEL_ON && XINDUSTRY_MODBUS_ON

#include <string.h>
#include "XMemory.h"
#include "XEvent.h"
#include "XIoChannel_Protected.h"

/* ==================== 内部实现（static，仅供本文件使用） ==================== */

/* 应答槽前置声明：发起请求的内部工具需要取其地址连接 finished 信号。 */
static void VXModbusDigitalInput_onReplyFinished(XObject* receiver, XVarList* args);

/**
 * @brief 把 Modbus 设备错误映射为通道层错误码。
 * @param error Modbus 设备错误码。
 * @return 对应的 XIoChannelError 枚举值；超时映射为 Timeout，其余非零错误
 *         一律映射为 Hardware。
 */
static int modbusDiMapError(XModbusDevice_Error error)
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
static void modbusDiDropReply(XModbusDigitalInput* self, XModbusReply* reply)
{
    if (!reply) return;
    XObject_disconnect_1((XObject*)reply, XSignal(XModbusReply_finished_signal),
                         (XObject*)self, VXModbusDigitalInput_onReplyFinished);
    XObject_deleteLater((XObject*)reply);
}

/**
 * @brief 单次轮询：发起 FC02 单点读请求并登记在途应答。
 * @param self 目标适配器；不能为 NULL。
 * @return 无。未挂接或上一拍在途时静默跳过；发送失败报硬件错误。
 */
static void modbusDiPoll(XModbusDigitalInput* self)
{
    XModbusDataUnit* unit;
    XModbusReply* reply;
    if (!self->m_client || self->m_pendingReply) {
        /* 未挂接 / 上一拍在途：跳过本拍，不叠加请求。 */
        return;
    }
    unit = XModbusDataUnit_create_ex(XCLASS_DEFAULT_MEMORY_TYPE,
                                     XModbusDiscreteInputs,
                                     (uint16_t)self->m_address, 1);
    if (!unit) return;
    reply = XModbusClient_sendReadRequest(self->m_client, unit,
                                          self->m_serverAddress,
                                          XModbusRequestPriority_Normal);
    XClassDelete((XClass*)unit); /* 请求构建为值拷贝，单元即刻释放。 */
    if (!reply) {
        if (XIoChannel_lastError(&self->m_class.m_class) != (int)XIoChannelError_Hardware) {
            XIoChannel_setError(&self->m_class.m_class, XIoChannelError_Hardware);
        }
        return;
    }
    self->m_pendingReply = reply;
    XObject_connect_1((XObject*)reply, XSignal(XModbusReply_finished_signal),
                      (XObject*)self, VXModbusDigitalInput_onReplyFinished,
                      XConnectionType_Direct);
}

/* ==================== 应答槽（经 connect_1 连接） ==================== */

/**
 * @brief 轮询应答完成槽：按 sender 取回真正的 Reply 再取值/报错并释放。
 * @param receiver 连接的接收者（即本适配器）；**不是** Reply，禁止直接强转。
 * @param args 信号参数（未使用）。
 * @return 无。
 * @note connect_1 第 1 参是 receiver；真正的发送者（XModbusReply*）必须经
 *       XObject_sender(receiver) 取回——对齐 XVfdModbusLink 的推荐写法。
 */
static void VXModbusDigitalInput_onReplyFinished(XObject* receiver, XVarList* args)
{
    XModbusDigitalInput* self;
    XModbusReply* reply;
    XIoChannel* io;
    (void)args;
    self = (XModbusDigitalInput*)receiver;
    if (!self) return;
    reply = (XModbusReply*)XObject_sender(receiver);
    if (!reply) return;
    io = &self->m_class.m_class;
    if (self->m_pendingReply == reply) self->m_pendingReply = NULL;

    if (XModbusReply_error(reply) == XModbusDevice_NoError) {
        const XModbusDataUnit* result = XModbusReply_result_const(reply);
        bool value;
        if (!result || XModbusDataUnit_valueCount(result) < 1) {
            /* 应答成功但无数据：按协议异常报错，留待下一拍恢复。 */
            if (XIoChannel_lastError(io) != (int)XIoChannelError_Hardware) {
                XIoChannel_setError(io, XIoChannelError_Hardware);
            }
        } else {
            XIoChannel_clearError(io);
            XIoChannel_setValid(io, true);
            value = (XModbusDataUnit_value(result, 0) != 0);
            if (!self->m_hasLastValue || value != self->m_lastValue) {
                self->m_lastValue = value;
                self->m_hasLastValue = true;
                XDigitalInput_inputChanged_signal(&self->m_class, value);
            }
        }
    } else {
        int error = modbusDiMapError(XModbusReply_error(reply));
        if (XIoChannel_lastError(io) != error) {
            XIoChannel_setError(io, error);
        }
    }
    XObject_deleteLater((XObject*)reply);
}

/* ==================== 虚函数实现（static，仅供 class_init 注册） ==================== */

static bool VXModbusDigitalInput_read(XModbusDigitalInput* self, bool* active)
{
    if (!self || !active) return false;
    if (!self->m_client) {
        XIoChannel_setError(&self->m_class.m_class, XIoChannelError_NotAttached);
        return false;
    }
    if (!self->m_hasLastValue) {
        /* 尚无成功轮询值：被动查询不改通道质量状态（质量由轮询路径维护）。 */
        return false;
    }
    *active = self->m_lastValue;
    return true;
}

static void VXModbusDigitalInput_timerEvent(XObject* obj, XTimerEvent* event)
{
    XModbusDigitalInput* self = (XModbusDigitalInput*)obj;
    if (!self || !event) goto parent_call;
    XEvent_accept(event);
    if (XTimerEvent_timerId(event) != self->m_pollTimer) goto parent_call;
    modbusDiPoll(self);
    return;
parent_call:
    /* 未知定时器 id 交父类处理，不吞事件。 */
    XClass_Parent(XObject, EXObject_TimerEvent,
                  void (*)(XObject*, XTimerEvent*))(obj, event);
}

static void VXModbusDigitalInput_deinit(XModbusDigitalInput* self)
{
    if (!self) return;
    if (self->m_pollTimer != XTIMER_INVALID_ID) {
        XObject_killTimer((XObject*)self, self->m_pollTimer);
        self->m_pollTimer = XTIMER_INVALID_ID;
    }
    if (self->m_pendingReply) {
        modbusDiDropReply(self, self->m_pendingReply);
        self->m_pendingReply = NULL;
    }
    self->m_client = NULL; /* 借用资源，不关闭不释放，仅清除引用。 */
    XClass_Deinit_Parent(XDigitalInput, (XDigitalInput*)self);
}

/* ==================== 虚函数表初始化（单例） ==================== */

XVtable* XModbusDigitalInput_class_init(void)
{
    XVTABLE_INIT_DEFAULT(XModbusDigitalInput)
    XVTABLE_INHERIT_XCLASS(XDigitalInput);
    XVTABLE_OVERLOAD_DEFAULT(EXDigitalInput_Read, VXModbusDigitalInput_read);
    XVTABLE_OVERLOAD_DEFAULT(EXObject_TimerEvent, VXModbusDigitalInput_timerEvent);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Deinit, VXModbusDigitalInput_deinit);
    return XVTABLE_DEFAULT;
}

/* ==================== 构造函数 ==================== */

void XModbusDigitalInput_init(XModbusDigitalInput* obj)
{
    if (!obj) return;
    memset(obj, 0, sizeof(XModbusDigitalInput));
    XDigitalInput_init(&obj->m_class);
    XClassSetVtable(obj, XModbusDigitalInput);
    obj->m_client = NULL;
    obj->m_serverAddress = 0;
    obj->m_address = 0;
    obj->m_pollTimer = XTIMER_INVALID_ID;
    obj->m_pendingReply = NULL;
    obj->m_lastValue = false;
    obj->m_hasLastValue = false;
}

bool XModbusDigitalInput_attach(XModbusDigitalInput* obj, XModbusClient* client,
                                int serverAddress, int address, uint32_t pollIntervalMs)
{
    XTimerId timer;
    if (!obj || !client || serverAddress < 1 || serverAddress > 247 ||
        address < 0 || address > 65535 || pollIntervalMs == 0u) {
        XERROR_PRINTF("XModbusDigitalInput_attach: 参数非法\n");
        return false;
    }
    timer = XObject_startTimer_ms((XObject*)obj, (uint64_t)pollIntervalMs,
                                  XTimerType_CoarseTimer);
    if (timer == XTIMER_INVALID_ID) {
        XERROR_PRINTF("XModbusDigitalInput_attach: 轮询定时器启动失败\n");
        return false;
    }
    /* 参数校验通过后再拆旧挂接，失败路径不影响原有状态。 */
    if (obj->m_pollTimer != XTIMER_INVALID_ID) {
        XObject_killTimer((XObject*)obj, obj->m_pollTimer);
    }
    if (obj->m_pendingReply) {
        modbusDiDropReply(obj, obj->m_pendingReply); /* 重新挂接：在途旧应答丢弃。 */
    }
    obj->m_pollTimer = timer;
    obj->m_client = client;
    obj->m_serverAddress = serverAddress;
    obj->m_address = address;
    obj->m_pendingReply = NULL;
    obj->m_lastValue = false;
    obj->m_hasLastValue = false; /* 重新挂接后首拍重新推送初值。 */
    return true;
}

XModbusDigitalInput* XModbusDigitalInput_create_ex(XModbusClient* client, int serverAddress,
                                                   int address, uint32_t pollIntervalMs)
{
    XModbusDigitalInput* obj =
        (XModbusDigitalInput*)XMalloc_System(sizeof(XModbusDigitalInput));
    if (!obj) return NULL;
    XModbusDigitalInput_init(obj);
    Set_Class_IsHeap(obj, true);
    if (!XModbusDigitalInput_attach(obj, client, serverAddress, address, pollIntervalMs)) {
        XClassDelete(obj);
        return NULL;
    }
    return obj;
}

#endif /* XINDUSTRY_ON && XINDUSTRY_CHANNEL_ON && XINDUSTRY_MODBUS_ON */
