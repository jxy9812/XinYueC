/**
 * @file       XModbusPoint.c
 * @brief      Modbus 数据源点实现（XIoAnalogPoint 派生；Hub 缓存读写 +
 *             自订阅信号推送）。
 * @details    [2026-11-01 通道组重构] 虚槽实现：
 *               ReadRaw：读 Hub 缓存（AI/AO 经标度换算为工程值口径）；
 *               WriteRaw：DO→写 Coils、AO→写 HR（标度逆换算）；DI/AI 只读。
 *             自订阅：init 连接 Hub valueChanged/writeCompleted（receiver=
 *             本点，RawReceiver 直连——点非 XObject，信号槽不做其簿记）；
 *             valueChanged 命中本点 → 标度换算 → xIoPointFeedValue
 *             推送组；writeCompleted 命中本点 → xIoPointWriteDone。
 *             区映射（m_type + useHolding）：
 *               DigitalInput→DI；DigitalOutput→Coils；
 *               AnalogInput→IR/HR(useHolding)；AnalogOutput→HR。
 */
#include "XModbusPoint.h"

#if XINDUSTRY_ON && XINDUSTRY_CHANNEL_ON && XINDUSTRY_MODBUS_ON && XMODBUS_HUB_ON

#include <string.h>
#include <stddef.h>
#include "XMemory.h"
#include "XEvent.h"
#include "XIoChannelGroup.h"
#include "XIoPoint_Protected.h"

/* ==================== 内部实现（static，仅供本文件使用） ==================== */

/* hub 信号槽前置声明：init 需要取其地址连接，deinit 断开。 */
static void VXModbusPoint_onHubValueChanged(XObject* receiver, XVarList* args);
static void VXModbusPoint_onHubWriteCompleted(XObject* receiver, XVarList* args);

/**
 * @brief 点类型 → Hub 寄存器区映射。
 * @param point 目标点；不能为 NULL。
 * @return 对应的 XModbusRegisterType。
 */
static XModbusRegisterType modbusPtArea(const XModbusPoint* point)
{
    switch (point->m_base.m_base.m_type) {
    case (uint16_t)XIoPoint_DigitalInput:
        return XModbusDiscreteInputs;
    case (uint16_t)XIoPoint_DigitalOutput:
        return XModbusCoils;
    case (uint16_t)XIoPoint_AnalogInput:
        return (point->m_base.m_base.m_flags.bits.m_useHolding != 0)
                   ? XModbusHoldingRegisters
                   : XModbusInputRegisters;
    case (uint16_t)XIoPoint_AnalogOutput:
    default:
        return XModbusHoldingRegisters;
    }
}

/**
 * @brief 原始码 → 点值口径：DI/DO 取 0/1；AI/AO 经标度换算为工程值。
 * @param point 目标点；不能为 NULL。
 * @param raw 原始码。
 * @return 点值（与 m_value 同口径）。
 */
static int32_t modbusPtToPointValue(XModbusPoint* point, uint16_t raw)
{
    if (point->m_base.m_base.m_type == (uint16_t)XIoPoint_AnalogInput ||
        point->m_base.m_base.m_type == (uint16_t)XIoPoint_AnalogOutput) {
        return xIoAnalogPointToEngineering(&point->m_base, (int32_t)raw);
    }
    return (raw != 0) ? 1 : 0;
}

/**
 * @brief 点值 → 原始码：DI/DO 直传；AO 经标度逆换算。DI/AI 不写。
 * @param point 目标点；不能为 NULL。
 * @param value 点值（点单位）。
 * @return 16 位原始码。
 */
static uint16_t modbusPtToRaw(XModbusPoint* point, int32_t value)
{
    if (point->m_base.m_base.m_type == (uint16_t)XIoPoint_AnalogOutput) {
        return xIoAnalogPointToRaw((const XIoAnalogPoint*)point, value);
    }
    return (value != 0) ? 1u : 0u;
}

/* ==================== hub 信号槽（经 connect_1 连接，receiver=点） ==================== */

/**
 * @brief Hub 值变化槽：区/地址命中本点 → 标度换算 → xIoPointFeedValue 推送。
 * @param receiver 连接的接收者（即本点）；**不是** Hub，禁止直接强转。
 * @param args 信号参数（int type, int virtualStart, int count）。
 * @return 无。
 */
static void VXModbusPoint_onHubValueChanged(XObject* receiver, XVarList* args)
{
    XModbusPoint* self;
    XModbusHub* hub;
    uint16_t raw = 0;
    if (!args) return;
    self = (XModbusPoint*)receiver;
    if (!self || !self->m_hub) return;
    hub = (XModbusHub*)XObject_sender(receiver);
    if (!hub) return;
    XVarList_args_3(args, int, type, int, vStart, int, count);
    if (type != (int)modbusPtArea(self)) return;
    if (self->m_address < vStart || self->m_address >= vStart + count) return;
    if (!XModbusHub_value(hub, (XModbusRegisterType)type,
                          self->m_address, &raw)) return;
    xIoPointFeedValue(&self->m_base, modbusPtToPointValue(self, raw));
}

/**
 * @brief Hub 写收口槽：区/地址命中本点 → xIoPointWriteDone 转发组。
 * @param receiver 连接的接收者（即本点）；**不是** Hub，禁止直接强转。
 * @param args 信号参数（int type, int virtualStart, int count, bool ok）。
 * @return 无。
 */
static void VXModbusPoint_onHubWriteCompleted(XObject* receiver, XVarList* args)
{
    XModbusPoint* self;
    if (!args) return;
    self = (XModbusPoint*)receiver;
    if (!self || !self->m_hub) return;
    XVarList_args_4(args, int, type, int, vStart, int, count, bool, ok);
    if (type != (int)modbusPtArea(self)) return;
    if (self->m_address < vStart || self->m_address >= vStart + count) return;
    xIoPointWriteDone(&self->m_base, ok);
}

/* ==================== 虚函数实现（static，仅供 class_init 注册） ==================== */

/**
 * @brief ReadRaw：读 Hub 缓存（AI/AO 换算为工程值口径）。
 */
static bool VXModbusPoint_readRaw(XModbusPoint* self, int32_t* outValue)
{
    uint16_t raw = 0;
    if (!self || !self->m_hub || !outValue) return false;
    if (!XModbusHub_value(self->m_hub, modbusPtArea(self),
                          self->m_address, &raw)) return false;
    *outValue = modbusPtToPointValue(self, raw);
    return true;
}

/**
 * @brief WriteRaw：DO→Coils、AO→HR（标度逆换算）；DI/AI 拒绝。
 */
static bool VXModbusPoint_writeRaw(XModbusPoint* self, int32_t value)
{
    if (!self || !self->m_hub) return false;
    if (self->m_base.m_base.m_type == (uint16_t)XIoPoint_DigitalOutput) {
        return XModbusHub_writeValue(self->m_hub, XModbusCoils,
                                     self->m_address,
                                     modbusPtToRaw(self, value));
    }
    if (self->m_base.m_base.m_type == (uint16_t)XIoPoint_AnalogOutput) {
        return XModbusHub_writeValue(self->m_hub, XModbusHoldingRegisters,
                                     self->m_address,
                                     modbusPtToRaw(self, value));
    }
    return false; /* 只读点不可写。 */
}

/**
 * @brief 点析构：断开 Hub 订阅后走父类链。
 */
static void VXModbusPoint_deinit(XModbusPoint* self)
{
    if (!self) return;
    if (self->m_valueConn) {
        XObject_disconnect_1((XObject*)self->m_hub,
                             XSignal(XModbusHub_valueChanged_signal),
                             (XObject*)self, VXModbusPoint_onHubValueChanged);
        self->m_valueConn = NULL;
    }
    if (self->m_doneConn) {
        XObject_disconnect_1((XObject*)self->m_hub,
                             XSignal(XModbusHub_writeCompleted_signal),
                             (XObject*)self, VXModbusPoint_onHubWriteCompleted);
        self->m_doneConn = NULL;
    }
    self->m_hub = NULL;
    XClass_Deinit_Parent(XIoPoint, (XIoPoint*)self);
}

/* ==================== 虚函数表初始化（单例） ==================== */

XVtable* XModbusPoint_class_init(void)
{
    XVTABLE_INIT_DEFAULT(XModbusPoint)
    XVTABLE_INHERIT_XCLASS(XIoPoint);
    XVTABLE_OVERLOAD_DEFAULT(EXIoPoint_ReadRaw, VXModbusPoint_readRaw);
    XVTABLE_OVERLOAD_DEFAULT(EXIoPoint_WriteRaw, VXModbusPoint_writeRaw);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Deinit, VXModbusPoint_deinit);
    XCLASS_SHOW_SIZE_DEFAULT(XModbusPoint);
    return XVTABLE_DEFAULT;
}

/* ==================== 初始化 / 工厂 ==================== */

void XModbusPoint_init(XModbusPoint* point, XModbusHub* hub)
{
    if (!point) return;
    XIoAnalogPoint_init(&point->m_base);
    XClassGetVtable(point) = XModbusPoint_class_init();
    /* [2026-11-01 越界根修] 尾段清零锚点必须是**首个派生成员**：m_address
     * 前置插入后，原 memset(&m_hub, sizeof-sizeof(base)) 起点晚 8 字节、
     * 长度不变 → 越过结构体末尾写 8 字节（/MDd 围栏在 free 时才爆，
     * CHECK_ALWAYS_DF 实证中止点=四点创建段）。offsetof 锚定与成员排布
     * 解耦，此后再加成员也不会复发。 */
    memset(&point->m_address, 0,
           sizeof(XModbusPoint) - offsetof(XModbusPoint, m_address));
    point->m_hub = hub;
    if (!hub) return;
    point->m_valueConn =
        XObject_connect_1((XObject*)hub, XSignal(XModbusHub_valueChanged_signal),
                          (XObject*)point, VXModbusPoint_onHubValueChanged,
                          XConnectionType_Direct | XConnectionType_RawReceiver);
    point->m_doneConn =
        XObject_connect_1((XObject*)hub, XSignal(XModbusHub_writeCompleted_signal),
                          (XObject*)point, VXModbusPoint_onHubWriteCompleted,
                          XConnectionType_Direct | XConnectionType_RawReceiver);
}

static XModbusPoint* modbusPtNew(XModbusHub* hub)
{
    XModbusPoint* p = (XModbusPoint*)XMemory_malloc(sizeof(XModbusPoint),
                                                    XCLASS_DEFAULT_MEMORY_TYPE);
    if (!p) return NULL;
    XModbusPoint_init(p, hub);
    Set_Class_IsHeap(p, true);
    return p;
}

XModbusPoint* XModbusPoint_createDigitalInput(XModbusHub* hub, int address, const char* tag)
{
    XModbusPoint* p;
    if (!hub || address < 0 || address > 65535) return NULL;
    p = modbusPtNew(hub);
    if (!p) return NULL;
    p->m_base.m_base.m_tag = tag;
    p->m_base.m_base.m_type = (uint16_t)XIoPoint_DigitalInput;
    p->m_address = (uint16_t)address;
    return p;
}

XModbusPoint* XModbusPoint_createDigitalOutput(XModbusHub* hub, int address, const char* tag)
{
    XModbusPoint* p;
    if (!hub || address < 0 || address > 65535) return NULL;
    p = modbusPtNew(hub);
    if (!p) return NULL;
    p->m_base.m_base.m_tag = tag;
    p->m_base.m_base.m_type = (uint16_t)XIoPoint_DigitalOutput;
    p->m_address = (uint16_t)address;
    return p;
}

XModbusPoint* XModbusPoint_createAnalogInput(XModbusHub* hub, int address,
                                             bool useHoldingRegisters,
                                             int32_t scaleNum, int32_t scaleDen,
                                             int32_t offset, int32_t deadband,
                                             const char* tag)
{
    XModbusPoint* p;
    if (!hub || address < 0 || address > 65535) return NULL;
    p = modbusPtNew(hub);
    if (!p) return NULL;
    p->m_base.m_base.m_tag = tag;
    p->m_base.m_base.m_type = (uint16_t)XIoPoint_AnalogInput;
    p->m_address = (uint16_t)address;
    p->m_base.m_base.m_flags.bits.m_useHolding = useHoldingRegisters ? 1u : 0u;
    p->m_base.m_scaleNum = scaleNum;
    p->m_base.m_scaleDen = scaleDen;
    p->m_base.m_offset = offset;
    p->m_base.m_deadband = (deadband < 0) ? 0 : deadband;
    return p;
}

XModbusPoint* XModbusPoint_createAnalogOutput(XModbusHub* hub, int address,
                                              int32_t scaleNum, int32_t scaleDen,
                                              int32_t offset, const char* tag)
{
    XModbusPoint* p;
    if (!hub || address < 0 || address > 65535) return NULL;
    p = modbusPtNew(hub);
    if (!p) return NULL;
    p->m_base.m_base.m_tag = tag;
    p->m_base.m_base.m_type = (uint16_t)XIoPoint_AnalogOutput;
    p->m_address = (uint16_t)address;
    p->m_base.m_scaleNum = scaleNum;
    p->m_base.m_scaleDen = scaleDen;
    p->m_base.m_offset = offset;
    return p;
}

#endif /* XINDUSTRY_ON && XINDUSTRY_CHANNEL_ON && XINDUSTRY_MODBUS_ON && XMODBUS_HUB_ON */
