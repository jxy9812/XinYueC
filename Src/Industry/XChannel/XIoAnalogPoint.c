/**
 * @file       XIoAnalogPoint.c
 * @brief      工业模拟量 IO 点实现（纯数据子类，无虚槽）。
 * @details    [2026-11-03 数字/模拟拆分] 承载从 XIoPoint 拆出的标度四件套。
 *             零遍历、无链路指针；换算函数只做算术，不产生任何 I/O。
 */
#include "XIoAnalogPoint.h"

#if XINDUSTRY_ON && XINDUSTRY_CHANNEL_ON

#include <string.h>

/* ==================== 虚函数表（无自定义虚槽） ==================== */

XVtable* XIoAnalogPoint_class_init(void)
{
    /* 纯数据子类：虚表 = XIoPoint 基段 + 空扩展段。
     * 用 EXTEND_END 生成空扩展枚举，避免无枚举时的空数组问题。 */
    XVTABLE_INIT_DEFAULT(XIoAnalogPoint)
    XVTABLE_INHERIT_XCLASS(XIoPoint);
    XCLASS_SHOW_SIZE_DEFAULT(XIoAnalogPoint);
    return XVTABLE_DEFAULT;
}

/* ==================== 生命周期 ==================== */

void XIoAnalogPoint_init(XIoAnalogPoint* point)
{
    if (!point) return;
    /* 先按基类初始化（清零基类段 + 基类虚表 + 默认类型），再换成本类虚表
     * 并清掉派生段：与基类 init 的「尾段清零」同一配方，锚定派生成员区域。 */
    XIoPoint_init(&point->m_base);
    memset((char*)point + sizeof(XIoPoint), 0, sizeof(XIoAnalogPoint) - sizeof(XIoPoint));
    XClassGetVtable(point) = XIoAnalogPoint_class_init();
}

XIoAnalogPoint* XIoAnalogPoint_create_ex(XMemoryType memory)
{
    XIoAnalogPoint* point = (XIoAnalogPoint*)XMemory_malloc(sizeof(XIoAnalogPoint), memory);
    if (!point) return NULL;
    XIoAnalogPoint_init(point);
    Set_Class_Memory(point, memory);
    Set_Class_IsHeap(point, true);
    return point;
}

/* ==================== isa 查询 ==================== */

bool XIoAnalogPoint_isInstance(const XIoPoint* point)
{
    return point ? (XClassGetVtable(point) == XIoAnalogPoint_class_init()) : false;
}

/* ==================== 成员访问 ==================== */

void XIoAnalogPoint_setScale(XIoAnalogPoint* point, int32_t scaleNum,
                             int32_t scaleDen, int32_t offset, int32_t deadband)
{
    if (!point) return;
    point->m_scaleNum = scaleNum;
    point->m_scaleDen = scaleDen;
    point->m_offset = offset;
    point->m_deadband = (deadband < 0) ? 0 : deadband;
}

/* ==================== 标度换算 ==================== */

int32_t XIoAnalogPoint_toEngineering(const XIoAnalogPoint* point, int32_t raw)
{
    int64_t num;
    int64_t den;
    if (!point) return raw;
    num = (int64_t)raw * (int64_t)(point->m_scaleNum == 0 ? 1 : point->m_scaleNum);
    den = (int64_t)(point->m_scaleDen == 0 ? 1 : point->m_scaleDen);
    /* 向零取整：C 的整型除法对负数即向零截断。 */
    return (int32_t)(num / den) + point->m_offset;
}

uint16_t XIoAnalogPoint_toRaw(const XIoAnalogPoint* point, int32_t value)
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
    if (point->m_base.m_flags.bits.m_signed) {
        if (raw > 32767) raw = ((raw + 32768) % 65536) - 32768;
        if (raw < -32768) raw = ((raw + 32768) % 65536) - 32768;
        return (uint16_t)((uint32_t)raw & 0xFFFFu);
    }
    if (raw < 0) raw = 0;
    if (raw > 65535) raw = 65535;
    return (uint16_t)raw;
}

#endif /* XINDUSTRY_ON && XINDUSTRY_CHANNEL_ON */
