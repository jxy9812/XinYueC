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

void XIoPoint_setScale(XIoPoint* point, int32_t scaleNum, int32_t scaleDen,
                       int32_t offset, int32_t deadband)
{
    if (!point) return;
    point->m_scaleNum = scaleNum;
    point->m_scaleDen = scaleDen;
    point->m_offset = offset;
    point->m_deadband = (deadband < 0) ? 0 : deadband;
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
    int64_t num;
    int64_t den;
    if (!point) return raw;
    num = (int64_t)raw * (int64_t)(point->m_scaleNum == 0 ? 1 : point->m_scaleNum);
    den = (int64_t)(point->m_scaleDen == 0 ? 1 : point->m_scaleDen);
    /* 向零取整：C 的整型除法对负数即向零截断。 */
    return (int32_t)(num / den) + point->m_offset;
}

uint16_t XIoPoint_toRaw(const XIoPoint* point, int32_t value)
{
    int64_t num;
    int64_t den;
    int64_t raw;
    if (!point) return 0;

    num = (int64_t)value - (int64_t)point->m_offset;
    num *= (int64_t)(point->m_scaleDen == 0 ? 1 : point->m_scaleDen);
    den = (int64_t)(point->m_scaleNum == 0 ? 1 : point->m_scaleNum);
    if (den < 0) {
        /* 分母为负时把符号并进分子，保证除法的舍入方向一致。 */
        den = -den;
        num = -num;
    }
    /* 就近取整（.5 远离零），与旧实现保持一致。 */
    if (num >= 0) {
        raw = (num + den / 2) / den;
    } else {
        raw = -((-num + den / 2) / den);
    }

    /* [2026-11-02 根修] 符号语义只在这里定义一次：
     *   有符号点（m_signed=1）→ 负数按 16 位补码归一（-2000 → 0xF830），
     *                            正数上限 32767 之外仍按补码环绕；
     *   无符号点           → 负值钳 0，上限 65535。
     * 旧实现（XIoAnalogPoint.c）先取绝对值算商、恢复负号后又无条件
     * `if (quotient < 0) quotient = 0;`，把每一个负给定都静默吞成 0。 */
    if (point->m_flags.bits.m_signed) {
        if (raw > 32767) raw = ((raw + 32768) % 65536) - 32768;
        if (raw < -32768) raw = ((raw + 32768) % 65536) - 32768;
        return (uint16_t)((uint32_t)raw & 0xFFFFu);
    }
    if (raw < 0) raw = 0;
    if (raw > 65535) raw = 65535;
    return (uint16_t)raw;
}

#endif /* XINDUSTRY_ON && XINDUSTRY_CHANNEL_ON */
