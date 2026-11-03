/**
 * @file       XIoPoint.c
 * @brief      工业 IO 点实现（纯数据容器，无虚槽）。
 * @details    [2026-11-02 XChannel 重写] 本类不再是"会读写"的对象，只是
 *             「语义槽号 + 值与状态 + 标度」的数据载体。链路能力全部在
 *             XIoLink，组负责分发，设备层按 role 取槽。
 *
 *             零遍历：m_role 是组内下标，组按 role 直接放点、直接取点；
 *             本文件内没有任何按点查找的循环。
 */
#include "XIoPoint.h"

#if XINDUSTRY_ON && XINDUSTRY_CHANNEL_ON

#include "XIoAnalogPoint.h"
#include <string.h>

/* ==================== 虚函数表（无自定义虚槽） ==================== */

XVtable* XIoPoint_class_init(void)
{
    /* 本类无自定义虚槽：虚表只有 XClass 基段（init/deinit 等）。
     * 用 EXTEND_END 生成空扩展表，避免 BEGING/END 组合在无枚举时的空数组问题。 */
    XVTABLE_INIT_DEFAULT(XIoPoint)
    XVTABLE_INHERIT_XCLASS(XClass);
    XCLASS_SHOW_SIZE_DEFAULT(XIoPoint);
    return XVTABLE_DEFAULT;
}

/* ==================== 生命周期 ==================== */

void XIoPoint_init(XIoPoint* point)
{
    if (!point) return;
    XClass_init((XClass*)point);
    XClassGetVtable(point) = XIoPoint_class_init();
    /* 尾段清零：锚定首个派生成员之后的区域（XClass 段已由 XClass_init 处理）。 */
    memset((char*)point + sizeof(XClass), 0, sizeof(XIoPoint) - sizeof(XClass));
    point->m_type = (uint16_t)XIoPoint_DigitalInput;
}

XIoPoint* XIoPoint_create_ex(XMemoryType memory)
{
    XIoPoint* point = (XIoPoint*)XMemory_malloc(sizeof(XIoPoint), memory);
    if (!point) return NULL;
    XIoPoint_init(point);
    Set_Class_Memory(point, memory);
    Set_Class_IsHeap(point, true);
    return point;
}

/* ==================== 成员访问 ==================== */

bool XIoPoint_setRole(XIoPoint* point, uint16_t role)
{
    if (!point) return false;
    if (role >= (uint16_t)XIOCHANNELGROUP_ROLE_CAP) return false;
    point->m_role = role;
    return true;
}

uint16_t XIoPoint_role(const XIoPoint* point)
{
    return point ? point->m_role : 0;
}

void XIoPoint_setType(XIoPoint* point, XIoPointType type)
{
    if (!point) return;
    point->m_type = (uint16_t)type;
}

XIoPointType XIoPoint_type(const XIoPoint* point)
{
    return point ? (XIoPointType)point->m_type : XIoPoint_DigitalInput;
}

void XIoPoint_setTag(XIoPoint* point, const char* tag)
{
    if (!point) return;
    point->m_tag = tag;
}

const char* XIoPoint_tag(const XIoPoint* point)
{
    return point ? point->m_tag : NULL;
}

void XIoPoint_setSigned(XIoPoint* point, bool signedValue)
{
    if (!point) return;
    point->m_flags.bits.m_signed = signedValue ? 1u : 0u;
}

bool XIoPoint_isSigned(const XIoPoint* point)
{
    return point ? (point->m_flags.bits.m_signed != 0) : false;
}

void XIoPoint_setUseHolding(XIoPoint* point, bool useHolding)
{
    if (!point) return;
    point->m_flags.bits.m_useHolding = useHolding ? 1u : 0u;
}

bool XIoPoint_useHolding(const XIoPoint* point)
{
    return point ? (point->m_flags.bits.m_useHolding != 0) : false;
}

void XIoPoint_setAutoDelete(XIoPoint* point, bool autoDelete)
{
    if (!point) return;
    point->m_flags.bits.m_autoDelete = autoDelete ? 1u : 0u;
}

bool XIoPoint_autoDelete(const XIoPoint* point)
{
    return point ? (point->m_flags.bits.m_autoDelete != 0) : false;
}

/* ==================== 值查询 ==================== */

bool XIoPoint_value(const XIoPoint* point, int32_t* outValue)
{
    if (!point || !outValue) return false;
    if (!point->m_flags.bits.m_hasValue && !point->m_flags.bits.m_valid) return false;
    *outValue = point->m_value;
    return true;
}

bool XIoPoint_isValid(const XIoPoint* point)
{
    return point ? (point->m_flags.bits.m_valid != 0) : false;
}

/* ==================== 标度换算 ==================== */

int32_t XIoPoint_toEngineering(const XIoPoint* point, int32_t raw)
{
    if (!point) return raw;
    /* [2026-11-03 拆分] 数字量（DI/DO）无标度：0/1 直通，恒等返回。
     * 模拟量（AI/AO）标度在 XIoAnalogPoint，按虚表指针委托；
     * 类型与实例不符（误用，装配闸口会拒绝）按恒等返回兜底。 */
    if (point->m_type != (uint16_t)XIoPoint_AnalogInput &&
        point->m_type != (uint16_t)XIoPoint_AnalogOutput) {
        return raw;
    }
    if (!XIoAnalogPoint_isInstance(point)) return raw;
    return XIoAnalogPoint_toEngineering((const XIoAnalogPoint*)point, raw);
}

#endif /* XINDUSTRY_ON && XINDUSTRY_CHANNEL_ON */
