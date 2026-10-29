/**
 * @file       XModbusAnalogOutput.c
 * @brief      Modbus 写单保持寄存器（FC06）通道适配器实现。
 * @details    write 把定点工程值反换算为 16 位原始码（int64 中间量，余数向
 *             最近值取整，结果钳位 0..65535），组装 XModbusHoldingRegisters
 *             单点数据单元发 FC06，不等应答即返回 true；应答经 finished 信号
 *             回收，成功同步发射 writeDone(true) 并置 valid，失败报错并发射
 *             writeDone(false)。从站地址 0 按广播处理（受理即完成）。主站
 *             借用不拥有；本适配器发出的 Reply 由自己 deleteLater 释放
 *             （对齐 XVfdModbusLink）。
 */
#include "XModbusAnalogOutput.h"

#if XINDUSTRY_ON && XINDUSTRY_CHANNEL_ON && XINDUSTRY_MODBUS_ON

#include <string.h>
#include "XMemory.h"
#include "XEvent.h"
#include "XIoChannel_Protected.h"

/* ==================== 内部实现（static，仅供本文件使用） ==================== */

/* 应答槽前置声明：释放工具需要取其地址断开 finished 连接。 */
static void VXModbusAnalogOutput_onReplyFinished(XObject* receiver, XVarList* args);

/**
 * @brief 把 Modbus 设备错误映射为通道层错误码。
 * @param error Modbus 设备错误码。
 * @return 对应的 XIoChannelError 枚举值；超时映射为 Timeout，其余非零错误
 *         一律映射为 Hardware。
 */
static int modbusAoMapError(XModbusDevice_Error error)
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
static void modbusAoDropReply(XModbusAnalogOutput* self, XModbusReply* reply)
{
    if (!reply) return;
    XObject_disconnect_1((XObject*)reply, XSignal(XModbusReply_finished_signal),
                         (XObject*)self, VXModbusAnalogOutput_onReplyFinished);
    XObject_deleteLater((XObject*)reply);
}

/**
 * @brief 定点工程值 → 16 位原始码反换算：(value - offset) * scaleDen / scaleNum。
 * @param self 目标适配器（提供标度参数）；不能为 NULL。
 * @param value 定点工程值。
 * @return 16 位原始码。int64 中间量防溢出；scaleNum 为 0 视为 1（防除零）；
 *         整除余数向最近值取整（四舍五入，.5 远离零）；结果钳位到 0..65535。
 */
static uint16_t modbusAoToRaw(const XModbusAnalogOutput* self, int32_t value)
{
    int64_t den = (self->m_scaleDen == 0) ? 1 : (int64_t)self->m_scaleDen;
    int64_t num = ((int64_t)value - (int64_t)self->m_offset) * den;
    int64_t numAbs = (num < 0) ? -num : num;
    int64_t snAbs = (self->m_scaleNum < 0) ? -(int64_t)self->m_scaleNum
                                           : (int64_t)self->m_scaleNum;
    int64_t quotient;
    int64_t remainder2;
    if (snAbs == 0) snAbs = 1; /* scaleNum 为 0 视为 1。 */
    /* 按绝对值整除 + 余数向最近值取整（.5 远离零），再还原符号。 */
    quotient = numAbs / snAbs;
    remainder2 = (numAbs % snAbs) * 2;
    if (remainder2 >= snAbs) quotient += 1;
    if (num < 0) quotient = -quotient;
    if (quotient < 0) quotient = 0;
    if (quotient > 65535) quotient = 65535;
    return (uint16_t)quotient;
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
static void VXModbusAnalogOutput_onReplyFinished(XObject* receiver, XVarList* args)
{
    XModbusAnalogOutput* self;
    XModbusReply* reply;
    XIoChannel* io;
    bool ok;
    (void)args;
    self = (XModbusAnalogOutput*)receiver;
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
        int error = modbusAoMapError(XModbusReply_error(reply));
        if (XIoChannel_lastError(io) != error) {
            XIoChannel_setError(io, error);
        }
    }
    XAnalogOutput_writeDone_signal(&self->m_class, ok);
    XObject_deleteLater((XObject*)reply);
}

/* ==================== 虚函数实现（static，仅供 class_init 注册） ==================== */

static bool VXModbusAnalogOutput_write(XModbusAnalogOutput* self, int32_t value)
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
                                     XModbusHoldingRegisters,
                                     (uint16_t)self->m_address, 1);
    if (!unit) {
        XIoChannel_setError(io, XIoChannelError_Unknown);
        return false;
    }
    XModbusDataUnit_setValue(unit, 0, modbusAoToRaw(self, value));
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
        XAnalogOutput_writeDone_signal(&self->m_class, true);
        return true;
    }
    self->m_pendingReply = reply;
    XObject_connect_1((XObject*)reply, XSignal(XModbusReply_finished_signal),
                      (XObject*)self, VXModbusAnalogOutput_onReplyFinished,
                      XConnectionType_Direct);
    return true; /* 受理语义：完成确认经 writeDone 异步冒泡。 */
}

static void VXModbusAnalogOutput_deinit(XModbusAnalogOutput* self)
{
    if (!self) return;
    if (self->m_pendingReply) {
        modbusAoDropReply(self, self->m_pendingReply);
        self->m_pendingReply = NULL;
    }
    self->m_client = NULL; /* 借用资源，不关闭不释放，仅清除引用。 */
    XClass_Deinit_Parent(XAnalogOutput, (XAnalogOutput*)self);
}

/* ==================== 虚函数表初始化（单例） ==================== */

XVtable* XModbusAnalogOutput_class_init(void)
{
    XVTABLE_INIT_DEFAULT(XModbusAnalogOutput)
    XVTABLE_INHERIT_XCLASS(XAnalogOutput);
    XVTABLE_OVERLOAD_DEFAULT(EXAnalogOutput_Write, VXModbusAnalogOutput_write);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Deinit, VXModbusAnalogOutput_deinit);
    return XVTABLE_DEFAULT;
}

/* ==================== 构造函数 ==================== */

void XModbusAnalogOutput_init(XModbusAnalogOutput* obj)
{
    if (!obj) return;
    memset(obj, 0, sizeof(XModbusAnalogOutput));
    XAnalogOutput_init(&obj->m_class);
    XClassSetVtable(obj, XModbusAnalogOutput);
    obj->m_client = NULL;
    obj->m_serverAddress = 0;
    obj->m_address = 0;
    obj->m_scaleNum = 1;
    obj->m_scaleDen = 1;
    obj->m_offset = 0;
    obj->m_pendingReply = NULL;
}

bool XModbusAnalogOutput_attach(XModbusAnalogOutput* obj, XModbusClient* client,
                                int serverAddress, int address,
                                int32_t scaleNum, int32_t scaleDen, int32_t offset)
{
    if (!obj || !client || serverAddress < 0 || serverAddress > 247 ||
        address < 0 || address > 65535) {
        XERROR_PRINTF("XModbusAnalogOutput_attach: 参数非法\n");
        return false;
    }
    if (obj->m_pendingReply) {
        modbusAoDropReply(obj, obj->m_pendingReply); /* 重新挂接：在途旧应答丢弃。 */
    }
    obj->m_client = client;
    obj->m_serverAddress = serverAddress;
    obj->m_address = address;
    obj->m_scaleNum = scaleNum;
    obj->m_scaleDen = scaleDen;
    obj->m_offset = offset;
    obj->m_pendingReply = NULL;
    return true;
}

XModbusAnalogOutput* XModbusAnalogOutput_create_ex(XModbusClient* client, int serverAddress,
                                                   int address, int32_t scaleNum,
                                                   int32_t scaleDen, int32_t offset)
{
    XModbusAnalogOutput* obj =
        (XModbusAnalogOutput*)XMalloc_System(sizeof(XModbusAnalogOutput));
    if (!obj) return NULL;
    XModbusAnalogOutput_init(obj);
    Set_Class_IsHeap(obj, true);
    if (!XModbusAnalogOutput_attach(obj, client, serverAddress, address,
                                    scaleNum, scaleDen, offset)) {
        XClassDelete(obj);
        return NULL;
    }
    return obj;
}

#endif /* XINDUSTRY_ON && XINDUSTRY_CHANNEL_ON && XINDUSTRY_MODBUS_ON */
