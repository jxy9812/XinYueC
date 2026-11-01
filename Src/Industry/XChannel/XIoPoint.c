/**
 * @file       XIoPoint.c
 * @brief      轻量 IO 点基类实现（XClass 直派生）。
 * @details    init 对齐 XModbusMapping 的 XClass 直派生模式：XClass_init +
 *             设虚表 + 清 XClass 之后尾段。虚槽 ReadRaw/WriteRaw 基类默认
 *             不实现（返回 false），数据来源由子类重写。组对接助手
 *             （feedValue/writeDone）经 m_group 反向指针转发组信号。
 */
#include "XIoPoint.h"

#if XINDUSTRY_ON && XINDUSTRY_CHANNEL_ON

#include "XIoPoint_Protected.h"
#include "XIoChannelGroup.h"
#include <string.h>

/* ==================== 虚函数实现（static，仅供 class_init 注册） ==================== */

/**
 * @brief ReadRaw 基类默认：无数据来源实现，返回 false。
 */
static bool VXIoPoint_readRaw(XIoPoint* self, int32_t* outValue)
{
    (void)self;
    (void)outValue;
    return false;
}

/**
 * @brief WriteRaw 基类默认：无传输动作实现，返回 false。
 */
static bool VXIoPoint_writeRaw(XIoPoint* self, int32_t value)
{
    (void)self;
    (void)value;
    return false;
}

/**
 * @brief 点析构：无堆成员，仅走父类链。
 * @param point 目标点；可为 NULL。
 */
static void VXIoPoint_deinit(XIoPoint* point)
{
    if (!point) return;
    XClass_Deinit_Parent(XClass, (XClass*)point);
}

/* ==================== 虚函数表初始化（单例） ==================== */

XVtable* XIoPoint_class_init(void)
{
    XVTABLE_INIT_DEFAULT(XIoPoint)
    XVTABLE_INHERIT_XCLASS(XClass);
    XVTABLE_OVERLOAD_DEFAULT(EXIoPoint_ReadRaw, VXIoPoint_readRaw);
    XVTABLE_OVERLOAD_DEFAULT(EXIoPoint_WriteRaw, VXIoPoint_writeRaw);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Deinit, VXIoPoint_deinit);
    XCLASS_SHOW_SIZE_DEFAULT(XIoPoint);
    return XVTABLE_DEFAULT;
}

void XIoPoint_init(XIoPoint* point)
{
    if (!point) return;
    XClass_init((XClass*)point);
    XClassGetVtable(point) = XIoPoint_class_init();
    memset(((XClass*)point) + 1, 0, sizeof(XIoPoint) - sizeof(XClass));
}

/* ==================== 自动内存管理 ==================== */

void XIoPoint_setAutoDelete(XIoPoint* point, bool autoDelete)
{
    if (!point) return;
    point->m_flags.bits.m_autoDelete = autoDelete ? 1u : 0u;
}

bool XIoPoint_autoDelete(const XIoPoint* point)
{
    if (!point) return false;
    return (point->m_flags.bits.m_autoDelete != 0) ? true : false;
}

/* ==================== 成员访问 API ==================== */

const char* XIoPoint_tag(const XIoPoint* point)
{
    if (!point) return NULL;
    return point->m_tag;
}

XIoPointType XIoPoint_type(const XIoPoint* point)
{
    if (!point) return XIoPoint_DigitalInput;
    return (XIoPointType)point->m_type;
}

XIoChannelGroup* XIoPoint_group(const XIoPoint* point)
{
    if (!point) return NULL;
    return point->m_group;
}

int32_t XIoPoint_value(const XIoPoint* point)
{
    if (!point) return 0;
    return point->m_value;
}

bool XIoPoint_isValid(const XIoPoint* point)
{
    if (!point) return false;
    return (point->m_flags.bits.m_valid != 0) ? true : false;
}

void XIoPoint_setValid(XIoPoint* point, bool valid)
{
    if (!point) return;
    point->m_flags.bits.m_valid = valid ? 1u : 0u;
}

bool XIoPoint_hasValue(const XIoPoint* point)
{
    if (!point) return false;
    return (point->m_flags.bits.m_hasValue != 0) ? true : false;
}

bool XIoPoint_useHolding(const XIoPoint* point)
{
    if (!point) return false;
    return (point->m_flags.bits.m_useHolding != 0) ? true : false;
}

void XIoPoint_setUseHolding(XIoPoint* point, bool useHolding)
{
    if (!point) return;
    point->m_flags.bits.m_useHolding = useHolding ? 1u : 0u;
}

/* ==================== 虚槽直调入口（经虚表分派，子类实现生效） ==================== */

bool XIoPoint_readRaw_base(XIoPoint* self, int32_t* outValue)
{
    if (ISNULL(self, "XIoPoint") || ISNULL(XClassGetVtable(self), "Vtable"))
        return false;
    return XClassGetVirtualFunc(self, EXIoPoint_ReadRaw,
                                bool (*)(XIoPoint*, int32_t*))(self, outValue);
}

bool XIoPoint_writeRaw_base(XIoPoint* self, int32_t value)
{
    if (ISNULL(self, "XIoPoint") || ISNULL(XClassGetVtable(self), "Vtable"))
        return false;
    return XClassGetVirtualFunc(self, EXIoPoint_WriteRaw,
                                bool (*)(XIoPoint*, int32_t))(self, value);
}

/* ==================== 组对接助手 ==================== */

bool xIoPointFeedValue(XIoPoint* point, int32_t value)
{
    if (!point) return false;
    point->m_flags.bits.m_valid = 1;
    if (point->m_flags.bits.m_hasValue && point->m_value == value) {
        return false; /* 值未变：静默（组 tick 再拉同值亦静默）。 */
    }
    point->m_value = value;
    point->m_flags.bits.m_hasValue = 1;
    /* DI/DO 同步逻辑值位（readDigitalInput 的快速读口径；AI/AO 不用）。 */
    if (point->m_type == (uint16_t)XIoPoint_DigitalInput ||
        point->m_type == (uint16_t)XIoPoint_DigitalOutput) {
        point->m_flags.bits.m_valueB = (value != 0) ? 1u : 0u;
    }
    if (point->m_group) {
        XIoChannelGroup_inputChanged_signal(point->m_group, point,
                                            point->m_value);
    }
    return true;
}

bool xIoPointWriteDone(XIoPoint* point, bool ok)
{
    if (!point || !point->m_group) return false;
    if (ok) point->m_flags.bits.m_valid = 1;
    XIoChannelGroup_writeDone_signal(point->m_group, point, ok);
    return true;
}

#endif /* XINDUSTRY_ON && XINDUSTRY_CHANNEL_ON */
