/**
 * @file       XModbusMapping.c
 * @brief      Modbus 聚合设备映射管理器实现。
 * @details    每寄存器类型一张 XVector< XModbusMapRange > 段表，插入时按
 *             virtualStart 升序定位并做重叠校验；翻译（resolveSegments /
 *             segmentsForDevice）线性扫描段表——段表规模预期为十到千级，
 *             且只发生在构建轮询计划与读写翻译时，线性足够。
 */
#include "XModbus_config.h"
#if XPROTOCOL_ON
#if XMODBUS_ON
#if XMODBUS_CLIENT_ON
#if XMODBUS_MAPPING_ON

#include "XModbusMapping.h"
#include "XMemory.h"
#include "XVector.h"
#include <string.h>

/* ==================== 内部辅助 ==================== */

/* [2026-10-26 越界根修] XModbusRegisterType 枚举含 XModbusInvalid=0，
 * 四类区实值 1..4——段表 m_ranges[4] 必须经本函数转稠密下标 0..3，
 * 直接拿枚举当数组下标对 HoldingRegisters(=4) 越界一字（现场实证段错误）。 */
static inline int mappingTypeIndex(XModbusRegisterType type)
{
    if (type < XModbusDiscreteInputs || type > XModbusHoldingRegisters) return -1;
    return (int)type - (int)XModbusDiscreteInputs;
}

/** @brief 区间 [a, a+an) 与 [b, b+bn) 是否相交（16 位无回绕区间）。 */
static inline bool rangesOverlap(uint16_t a, uint16_t an, uint16_t b, uint16_t bn)
{
    return (an > 0 && bn > 0 &&
            (uint32_t)a < (uint32_t)b + bn && (uint32_t)b < (uint32_t)a + an);
}

/* ==================== 虚函数 ==================== */
static void VXModbusMapping_deinit(XModbusMapping* map)
{
    if (!map) return;
    for (int i = 0; i < 4; ++i) {
        if (map->m_ranges[i]) {
            XClassDelete(map->m_ranges[i]);
            map->m_ranges[i] = NULL;
        }
    }
    XClass_Deinit_Parent(XClass, map);
}

XVtable* XModbusMapping_class_init(void)
{
    XVTABLE_INIT_DEFAULT(XModbusMapping)
    XVTABLE_INHERIT_XCLASS(XClass);
    /* 无新增虚函数：仅重载既有槽（deinit），不追加函数表（追加超容量）。 */
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Deinit, VXModbusMapping_deinit);
    XCLASS_SHOW_SIZE_DEFAULT(XModbusMapping);
    return XVTABLE_DEFAULT;
}

void XModbusMapping_init(XModbusMapping* map)
{
    if (!map) return;
    XClass_init(map);
    XClassGetVtable(map) = XModbusMapping_class_init();
    memset(((XClass*)map) + 1, 0, sizeof(XModbusMapping) - sizeof(XClass));
    for (int i = 0; i < 4; ++i) {
        map->m_ranges[i] = XVector_create_ex(XCLASS_DEFAULT_MEMORY_TYPE,
                                             sizeof(XModbusMapRange), false);
    }
}

XModbusMapping* XModbusMapping_create_ex(XMemoryType memory)
{
    XModbusMapping* map = (XModbusMapping*)XMemory_malloc(sizeof(XModbusMapping), memory);
    if (!map) return NULL;
    XModbusMapping_init(map);
    Set_Class_Memory(map, memory); Set_Class_IsHeap(map, true);
    return map;
}

/* ==================== 段表管理 ==================== */

bool XModbusMapping_addRange(XModbusMapping* map, XModbusRegisterType type,
                             uint16_t virtualStart, uint16_t count,
                             int deviceId, int unitId, uint16_t remoteStart)
{
    if (!map || !XClassGetVtable(map)) return false;
    const int idx = mappingTypeIndex(type);
    /* 跨 0xFFFF 回绕拒绝：段必须完整落在 16 位地址空间内。 */
    if (idx < 0 || count == 0 || deviceId < 0 ||
        (uint32_t)virtualStart + count > 0x10000u ||
        (uint32_t)remoteStart + count > 0x10000u) return false;

    struct XVector* vec = map->m_ranges[idx];
    if (!vec) return false;
    const size_t n = XVector_size_base(vec);
    /* 重叠校验 + 有序插入位定位（线性）。 */
    size_t insertAt = n;
    for (size_t i = 0; i < n; ++i) {
        const XModbusMapRange* r =
            (const XModbusMapRange*)XVector_at_base(vec, i);
        if (!r) return false;
        if (rangesOverlap(virtualStart, count, r->virtualStart, r->count))
            return false;
        if (insertAt == n && virtualStart < r->virtualStart) insertAt = i;
    }
    XModbusMapRange added;
    added.virtualStart = virtualStart;
    added.count = count;
    added.deviceId = deviceId;
    added.unitId = unitId;
    added.remoteStart = remoteStart;
    if (!XVector_insert_1_base(vec, insertAt, &added, 1)) return false;
    return true;
}

bool XModbusMapping_removeRange(XModbusMapping* map, XModbusRegisterType type,
                                uint16_t virtualStart)
{
    if (!map) return false;
    const int idx = mappingTypeIndex(type);
    if (idx < 0) return false;
    struct XVector* vec = map->m_ranges[idx];
    if (!vec) return false;
    const size_t n = XVector_size_base(vec);
    for (size_t i = 0; i < n; ++i) {
        const XModbusMapRange* r = (const XModbusMapRange*)XVector_at_base(vec, i);
        if (r && r->virtualStart == virtualStart) {
            XVector_remove_base(vec, (int64_t)i, 1);
            return true;
        }
    }
    return false;
}

void XModbusMapping_clearAreas(XModbusMapping* map, XModbusRegisterType type)
{
    if (!map) return;
    if (type == XModbusInvalid) {
        for (int i = 0; i < 4; ++i) {
            if (map->m_ranges[i]) XVector_clear_base(map->m_ranges[i]);
        }
        return;
    }
    const int idx = mappingTypeIndex(type);
    if (idx >= 0 && map->m_ranges[idx]) XVector_clear_base(map->m_ranges[idx]);
}

size_t XModbusMapping_rangeCount_const(const XModbusMapping* map, XModbusRegisterType type)
{
    if (!map) return 0;
    const int idx = mappingTypeIndex(type);
    if (idx < 0 || !map->m_ranges[idx]) return 0;
    return XVector_size_base(map->m_ranges[idx]);
}

const XModbusMapRange* XModbusMapping_rangeAt_const(const XModbusMapping* map,
                                                    XModbusRegisterType type, size_t index)
{
    if (!map) return NULL;
    const int idx = mappingTypeIndex(type);
    if (idx < 0 || !map->m_ranges[idx]) return NULL;
    if (index >= XVector_size_base(map->m_ranges[idx])) return NULL;
    return (const XModbusMapRange*)XVector_at_base(map->m_ranges[idx], index);
}

const XModbusMapRange* XModbusMapping_rangeAtVirtual_const(const XModbusMapping* map,
                                                           XModbusRegisterType type,
                                                           uint16_t virtualStart)
{
    if (!map) return NULL;
    const int idx = mappingTypeIndex(type);
    if (idx < 0 || !map->m_ranges[idx]) return NULL;
    struct XVector* vec = map->m_ranges[idx];
    size_t lo = 0;
    size_t hi = XVector_size_base(vec);
    while (lo < hi) {
        const size_t mid = lo + (hi - lo) / 2;
        const XModbusMapRange* r = (const XModbusMapRange*)XVector_at_base(vec, mid);
        if (!r) return NULL;
        if (virtualStart < r->virtualStart) {
            hi = mid;
        } else if ((uint32_t)virtualStart >= (uint32_t)r->virtualStart + r->count) {
            lo = mid + 1;
        } else {
            return r; /* virtualStart ∈ [r->virtualStart, +count) */
        }
    }
    return NULL;
}

/* ==================== 翻译 ==================== */

int XModbusMapping_resolveSegments_const(const XModbusMapping* map,
                                         XModbusRegisterType type,
                                         uint16_t virtualStart, uint16_t count,
                                         XModbusMapSegment* out, int outCap)
{
    if (!map || !out || count == 0) return 0;
    const int idx = mappingTypeIndex(type);
    if (idx < 0 || !map->m_ranges[idx]) return 0;
    struct XVector* vec = map->m_ranges[idx];
    const size_t n = XVector_size_base(vec);

    int produced = 0;
    int needed = 0;
    uint32_t cursor = virtualStart;                  /* 尚未覆盖的区间游标（虚拟地址）。 */
    const uint32_t end = (uint32_t)virtualStart + count;
    for (size_t i = 0; i < n && cursor < end; ++i) {
        const XModbusMapRange* r = (const XModbusMapRange*)XVector_at_base(vec, i);
        if (!r) break;
        const uint32_t rEnd = (uint32_t)r->virtualStart + r->count;
        if (rEnd <= cursor) continue;                /* 段整体在游标左侧。 */
        if ((uint32_t)r->virtualStart >= end) break; /* 段整体在区间右侧。 */
        const uint32_t segStart = (cursor > (uint32_t)r->virtualStart)
                                      ? cursor : (uint32_t)r->virtualStart;
        const uint32_t segEnd = (end < rEnd) ? end : rEnd;
        if (segEnd <= segStart) continue;
        needed++;
        if (produced < outCap) {
            XModbusMapSegment* s = &out[produced++];
            memset(s, 0, sizeof(*s));
            s->type = (uint16_t)type;
            s->range = *r;
            s->virtualStart = (uint16_t)segStart;
            s->remoteStart = (uint16_t)(r->remoteStart + (segStart - r->virtualStart));
            s->count = (uint16_t)(segEnd - segStart);
        }
        cursor = segEnd;                             /* 段间空洞自然跳过。 */
    }
    if (needed > outCap) return -needed;             /* 容量不足：负需求。 */
    return produced;
}

int XModbusMapping_segmentsForDevice_const(const XModbusMapping* map,
                                           int deviceId, XModbusRegisterType type,
                                           XModbusMapRange* out, int outCap)
{
    if (!map || !out || deviceId < 0) return 0;
    const int idx = mappingTypeIndex(type);
    if (idx < 0 || !map->m_ranges[idx]) return 0;
    struct XVector* vec = map->m_ranges[idx];
    const size_t n = XVector_size_base(vec);
    int produced = 0;
    int needed = 0;
    for (size_t i = 0; i < n; ++i) {
        const XModbusMapRange* r = (const XModbusMapRange*)XVector_at_base(vec, i);
        if (!r || r->deviceId != deviceId) continue;
        needed++;
        if (produced < outCap) out[produced++] = *r;
    }
    if (needed > outCap) return -needed;
    return produced;
}

bool XModbusMapping_deviceHasArea_const(const XModbusMapping* map,
                                        int deviceId, XModbusRegisterType type)
{
    if (!map || deviceId < 0) return false;
    const int idx = mappingTypeIndex(type);
    if (idx < 0 || !map->m_ranges[idx]) return false;
    struct XVector* vec = map->m_ranges[idx];
    const size_t n = XVector_size_base(vec);
    for (size_t i = 0; i < n; ++i) {
        const XModbusMapRange* r = (const XModbusMapRange*)XVector_at_base(vec, i);
        if (r && r->deviceId == deviceId) return true;
    }
    return false;
}

#endif /* XMODBUS_MAPPING_ON */
#endif /* XMODBUS_CLIENT_ON */
#endif /* XMODBUS_ON */
#endif /* XPROTOCOL_ON */
