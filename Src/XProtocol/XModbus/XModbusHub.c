/**
 * @file       XModbusHub.c
 * @brief      Modbus 聚合设备实现（多从站虚拟合一 + 每设备缓存层）。
 * @details    结构总览：
 *               - m_devices：XModbusHubDevice 值数组（XVector），设备句柄=id
 *                 字段线性查找（句柄不复用，删除即摘除槽位）。
 *               - 所有权走 XObject 父子机制（框架惯例，析构自动删 children）：
 *                 addDevice 把 client reparent 到 hub；每次发起请求把 Reply
 *                 parent 到 hub。设备槽内只挂借用指针与非 XObject 资源。
 *               - [2026-10-29 缓存层重构] 缓存坐标从「全局虚拟地址连续图」
 *                 换成「每设备缓存块」：块 = 同 (寄存器区, 从站) 下远端
 *                 连续的映射段合并（块内无洞）。上层 API 仍是虚拟地址：
 *                 m_view[4] 是按 virtualStart 有序的视图切片表（二分定位
 *                 → 块内偏移），读写都经它翻译。
 *               - 读：每设备一张读组表（同区同从站、远端间隔 ≤
 *                 setReadMergeGap 的相邻块并成一帧，按协议上限切分）；设备
 *                 周期定时器逐组单飞，应答按块交集回填，valueChanged 按
 *                 块内映射切片逐段发射（首次回填恒发一次，供上层播种）。
 *               - 写：受理即写块缓存（乐观回显，微秒级）+ 标脏区间，返回
 *                 true；设备空闲时把脏区间按 FC15/FC16 一帧下发（单帧按
 *                 协议上限切分），失败保留脏区间周期重发；attempts 达阈值
 *                 发一次 writeStalled 与一次 writeCompleted(ok=false)。
 *                 写合并窗口 setWriteCoalesceMs 内的多次写只留最终值。
 *               - 质量：块级 lastOkMs 支撑 valueAge/pointValid 点级新鲜度。
 *               - 设备状态：connect_2 跟踪（connect_2 第一参=真 sender=client，
 *                 经 parent 反查 hub 后按指针定设备），恢复 Connected 时
 *                 reconnectCount++ 并立即泵轮询/补写滞留写——底层断线重连
 *                 对上层透明。
 *             应答→设备归属：发起时记入 dev->inflight，完成槽按指针扫描匹配。
 */
#include "XModbus_config.h"
#if XPROTOCOL_ON
#if XMODBUS_ON
#if XMODBUS_CLIENT_ON
#if XMODBUS_HUB_ON

#include "XModbusHub.h"
#include "XModbusClient_Protected.h"
#include "XModbusDevice_Protected.h"
#include "XMemory.h"
#include "XVector.h"
#include "XString.h"
#include "XDateTime.h"
#include <stdio.h>
#include <string.h>

/* ==================== 槽前置声明 ==================== */
static void XModbusHub_onDeviceStateChanged(XObject* sender, XVarList* args);
static void XModbusHub_onReplyFinished(XObject* sender, XVarList* args);
static void hubPumpDevice(XModbusHub* hub, XModbusHubDevice* dev);
static void hubPumpWriteQueue(XModbusHub* hub);
static void hubRebuildUnitSched(XModbusHub* hub, XModbusHubDevice* dev);

/* [A3 时序缺口兜底] 同步终态收口重入护栏：收口尾部续泵理论上可经防御性
 * 忙分支再次同步终态（可达路径均被连接态门槛挡住），护栏兜底防栈爆；
 * 重入层改为就地释放+清 inflight（语义良性：脏区间留表下拍重试）。 */
static int s_hubInSyncFinish = 0;

/* ==================== 虚函数前置声明（class_init 在文件前部引用） ==================== */
static void VXModbusHub_deinit(XModbusHub* hub);
static void VXModbusHub_timerEvent(XObject* obj, XTimerEvent* event);

/* ==================== 内部辅助 ==================== */

/** @brief 按句柄查设备槽下标；未命中返回 -1。 */
static int hubDeviceIndexById(const XModbusHub* hub, int deviceId)
{
    if (!hub || !hub->m_devices || deviceId < 0) return -1;
    const size_t n = XVector_size_base(hub->m_devices);
    for (size_t i = 0; i < n; ++i) {
        const XModbusHubDevice* d =
            (const XModbusHubDevice*)XVector_at_base(hub->m_devices, (int64_t)i);
        if (d && d->id == deviceId) return (int)i;
    }
    return -1;
}

static XModbusHubDevice* hubDeviceById(XModbusHub* hub, int deviceId)
{
    const int idx = hubDeviceIndexById(hub, deviceId);
    if (idx < 0) return NULL;
    return (XModbusHubDevice*)XVector_at_base(hub->m_devices, idx);
}

/** @brief 停某设备轮询定时器（kill + 复位；周期定时器泄漏纪律）。 */
static void hubStopDevicePollTimer(XModbusHub* hub, XModbusHubDevice* dev)
{
    if (!hub || !dev) return;
    if (dev->pollTimer != XTIMER_INVALID_ID) {
        XObject_killTimer((XObject*)hub, dev->pollTimer);
        dev->pollTimer = XTIMER_INVALID_ID;
    }
}

/* [2026-10-26 越界根修] 枚举含 Invalid=0，四类区=1..4——数组下标一律经
 * 本函数取稠密 0..3（直接拿枚举当下标对 HR=4 越界一字）。 */
static inline int hubTypeIndex(int type)
{
    if (type < XModbusDiscreteInputs || type > XModbusHoldingRegisters) return -1;
    return type - (int)XModbusDiscreteInputs;
}
/** @brief 稠密下标(0..3)转回枚举值。 */
static inline int hubEnumOfType(int denseIdx)
{
    return denseIdx + (int)XModbusDiscreteInputs;
}
/** @brief 位区判定（位区 1bit/点，寄存器区 2B/点）。 */
static inline bool hubTypeIsBit(uint16_t type)
{
    return type == (uint16_t)XModbusDiscreteInputs || type == (uint16_t)XModbusCoils;
}
/** @brief 单帧写点数上限（位区 FC15=1968，寄存器区 FC16=123）。 */
static inline uint16_t hubWriteLimitOfType(uint16_t type)
{
    return hubTypeIsBit(type) ? (uint16_t)XMODBUS_HUB_WRITE_MAX_BITS
                              : (uint16_t)XMODBUS_HUB_WRITE_MAX_REGISTERS;
}
/** @brief 单帧读点数上限（位区 2000，寄存器区 125）。 */
static inline uint16_t hubReadLimitOfType(uint16_t type)
{
    return hubTypeIsBit(type) ? (uint16_t)XMODBUS_HUB_READ_MAX_BITS
                              : (uint16_t)XMODBUS_HUB_READ_MAX_REGISTERS;
}

/* ==================== 缓存块生命周期 ==================== */

/** @brief 释放一个缓存块（数据单元 + 切片表 + 脏区间表 + 结构体）。 */
static void hubBlockFree(XModbusHubCacheBlock* block)
{
    if (!block) return;
    if (block->data) XClassDelete(block->data);
    if (block->segs) XClassDelete(block->segs);
    if (block->dirty) XClassDelete(block->dirty);
    XFree_System(block);
}

/** @brief 释放块表（XVector<XModbusHubCacheBlock*>，元素本身由块持有）。 */
static void hubBlocksFreeVector(struct XVector* blocks)
{
    if (!blocks) return;
    const size_t n = XVector_size_base(blocks);
    for (size_t i = 0; i < n; ++i) {
        XModbusHubCacheBlock* b =
            *(XModbusHubCacheBlock**)XVector_at_base(blocks, (int64_t)i);
        hubBlockFree(b);
    }
    XClassDelete(blocks);
}

/**
 * @brief 建一个缓存块（数据按区初始化哨兵：位区 0、寄存器区 0xFFFF）。
 * @details 位区初值必须 0：setValue 对位单元只留 bit0，0xFFFF 会截断成 1
 *          ——「全 1 初值」曾伪装成已回填数据（2026-10-26 闭环实证）。
 */
static XModbusHubCacheBlock* hubBlockCreate(uint16_t type, int unitId,
                                            uint16_t remoteStart, uint16_t count)
{
    XModbusHubCacheBlock* b;
    const uint16_t initVal = hubTypeIsBit(type) ? 0u : 0xFFFFu;
    if (count == 0) return NULL;
    b = (XModbusHubCacheBlock*)XMalloc_System(sizeof(XModbusHubCacheBlock));
    if (!b) return NULL;
    memset(b, 0, sizeof(*b));
    b->type = type;
    b->unitId = unitId;
    b->remoteStart = remoteStart;
    b->count = count;
    b->data = XModbusDataUnit_create_ex(XCLASS_DEFAULT_MEMORY_TYPE,
                                        (XModbusRegisterType)type, remoteStart, count);
    b->segs = XVector_create_ex(XCLASS_DEFAULT_MEMORY_TYPE,
                                sizeof(XModbusHubCacheSeg), false);
    b->dirty = XVector_create_ex(XCLASS_DEFAULT_MEMORY_TYPE,
                                 sizeof(XModbusHubDirtyRange), false);
    if (!b->data || !b->segs || !b->dirty) {
        hubBlockFree(b);
        return NULL;
    }
    for (uint16_t i = 0; i < count; ++i)
        XModbusDataUnit_setValue(b->data, i, initVal);
    return b;
}

/** @brief 取设备块表中某类型某从站下覆盖 remote 地址的块（借用；未命中 NULL）。 */
static XModbusHubCacheBlock* hubFindBlockAt(XModbusHubDevice* dev, uint16_t type,
                                            int unitId, uint32_t remote)
{
    if (!dev || !dev->blocks) return NULL;
    const size_t n = XVector_size_base(dev->blocks);
    for (size_t i = 0; i < n; ++i) {
        XModbusHubCacheBlock* b =
            *(XModbusHubCacheBlock**)XVector_at_base(dev->blocks, (int64_t)i);
        if (!b || b->type != type || b->unitId != unitId) continue;
        if (remote < b->remoteStart ||
            remote >= (uint32_t)b->remoteStart + b->count) continue;
        return b;
    }
    return NULL;
}

/* ==================== 脏区间簿记 ==================== */

/** @brief 脏区间是否覆盖块内偏移 offset（轮询回填跳过未确认写点）。 */
static bool hubDirtyHasOffset(const XModbusHubCacheBlock* block, uint16_t offset)
{
    if (!block || !block->dirty) return false;
    const size_t n = XVector_size_base(block->dirty);
    for (size_t i = 0; i < n; ++i) {
        const XModbusHubDirtyRange* d =
            (const XModbusHubDirtyRange*)XVector_at_base(block->dirty, (int64_t)i);
        if (d && offset >= d->offset &&
            (uint32_t)offset < (uint32_t)d->offset + d->count) return true;
    }
    return false;
}

/** @brief 按 offset 升序插入一条脏区间（不合并；内部/迁移用）。 */
static void hubDirtyPushRaw(XModbusHubCacheBlock* block,
                            const XModbusHubDirtyRange* range)
{
    size_t i;
    size_t n;
    if (!block || !block->dirty || !range || range->count == 0) return;
    n = XVector_size_base(block->dirty);
    for (i = 0; i < n; ++i) {
        const XModbusHubDirtyRange* d =
            (const XModbusHubDirtyRange*)XVector_at_base(block->dirty, (int64_t)i);
        if (d && range->offset < d->offset) break;
    }
    XVector_insert_1_base(block->dirty, (int64_t)i, range, 1);
}

/**
 * @brief 标脏：与既有脏区间重叠或相邻即合并（合并窗口内多次写只留最终值）。
 * @note  并入既有区间视为「新意图」：attempts 与两个提示沿一并复位。
 */
static void hubMarkDirty(XModbusHubCacheBlock* block, uint16_t offset, uint16_t count)
{
    if (!block || !block->dirty || count == 0) return;
    const uint32_t newEnd = (uint32_t)offset + count;
    const size_t n = XVector_size_base(block->dirty);
    for (size_t i = 0; i < n; ++i) {
        XModbusHubDirtyRange* d =
            (XModbusHubDirtyRange*)XVector_at_base(block->dirty, (int64_t)i);
        const uint32_t dEnd = (uint32_t)d->offset + d->count;
        if (d && (uint32_t)offset <= dEnd && (uint32_t)d->offset <= newEnd) {
            const uint32_t lo = ((uint32_t)d->offset < offset) ? d->offset : offset;
            const uint32_t hi = (dEnd > newEnd) ? dEnd : newEnd;
            d->offset = (uint16_t)lo;
            d->count = (uint16_t)(hi - lo);
            d->attempts = 0;
            d->stallNotified = false;
            d->failNotified = false;
            return;
        }
    }
    {
        XModbusHubDirtyRange r;
        memset(&r, 0, sizeof(r));
        r.offset = offset;
        r.count = count;
        hubDirtyPushRaw(block, &r);
    }
}

/** @brief 摘除/切分脏区间：从 [offset, offset+count) 收口（写成功）。 */
static void hubDirtySubtract(XModbusHubCacheBlock* block, uint16_t offset, uint16_t count)
{
    uint32_t newEnd;
    size_t i;
    if (!block || !block->dirty || count == 0) return;
    newEnd = (uint32_t)offset + count;
    for (i = XVector_size_base(block->dirty); i > 0; --i) {
        XModbusHubDirtyRange* d =
            (XModbusHubDirtyRange*)XVector_at_base(block->dirty, (int64_t)(i - 1));
        const uint32_t dStart = d->offset;
        const uint32_t dEnd = dStart + d->count;
        if (!d) continue;
        if (dEnd <= offset || dStart >= newEnd) continue;      /* 无交集。 */
        if (dStart >= offset && dEnd <= newEnd) {              /* 完全覆盖：删。 */
            XVector_remove_base(block->dirty, (int64_t)(i - 1), 1);
            continue;
        }
        if (dStart < offset && dEnd > newEnd) {                /* 中间挖空：切两段。 */
            XModbusHubDirtyRange right;
            right.offset = (uint16_t)newEnd;
            right.count = (uint16_t)(dEnd - newEnd);
            right.attempts = d->attempts;
            right.stallNotified = d->stallNotified;
            right.failNotified = d->failNotified;
            d->count = (uint16_t)(offset - dStart);
            hubDirtyPushRaw(block, &right);
            continue;
        }
        if (dStart < offset) {                                 /* 掐尾。 */
            d->count = (uint16_t)(offset - dStart);
        } else {                                              /* 掐头。 */
            d->offset = (uint16_t)newEnd;
            d->count = (uint16_t)(dEnd - newEnd);
        }
    }
}

/** @brief 设备是否还有待写脏区间。 */
static bool hubDeviceHasDirty(const XModbusHubDevice* dev)
{
    size_t i;
    size_t n;
    if (!dev || !dev->blocks) return false;
    n = XVector_size_base(dev->blocks);
    for (i = 0; i < n; ++i) {
        const XModbusHubCacheBlock* b =
            *(XModbusHubCacheBlock* const*)XVector_at_base(dev->blocks, (int64_t)i);
        if (b && b->dirty && XVector_size_base(b->dirty) > 0) return true;
    }
    return false;
}

/** @brief 设备脏区间总数（pendingWriteCount 口径=合并后区间数）。 */
static int hubDeviceDirtyCount(const XModbusHubDevice* dev)
{
    size_t i;
    size_t n;
    int total = 0;
    if (!dev || !dev->blocks) return 0;
    n = XVector_size_base(dev->blocks);
    for (i = 0; i < n; ++i) {
        const XModbusHubCacheBlock* b =
            *(XModbusHubCacheBlock* const*)XVector_at_base(dev->blocks, (int64_t)i);
        if (b && b->dirty) total += (int)XVector_size_base(b->dirty);
    }
    return total;
}

/* ==================== 重建：块 / 视图 / 读组 ==================== */

/** @brief 切片按 virtualStart 升序排序（插入排序；段数为十级）。 */
static void hubSortSegs(struct XVector* segs)
{
    size_t i;
    const size_t n = segs ? XVector_size_base(segs) : 0;
    for (i = 1; i < n; ++i) {
        XModbusHubCacheSeg key =
            *(XModbusHubCacheSeg*)XVector_at_base(segs, (int64_t)i);
        int64_t j = (int64_t)i - 1;
        while (j >= 0) {
            const XModbusHubCacheSeg* cur =
                (const XModbusHubCacheSeg*)XVector_at_base(segs, j);
            if (cur->virtualStart <= key.virtualStart) break;
            *(XModbusHubCacheSeg*)XVector_at_base(segs, j + 1) = *cur;
            --j;
        }
        *(XModbusHubCacheSeg*)XVector_at_base(segs, j + 1) = key;
    }
}

/** @brief 视图切片按 virtualStart 升序排序。 */
static void hubSortView(struct XVector* view)
{
    size_t i;
    const size_t n = view ? XVector_size_base(view) : 0;
    for (i = 1; i < n; ++i) {
        XModbusHubCacheSlice key =
            *(XModbusHubCacheSlice*)XVector_at_base(view, (int64_t)i);
        int64_t j = (int64_t)i - 1;
        while (j >= 0) {
            const XModbusHubCacheSlice* cur =
                (const XModbusHubCacheSlice*)XVector_at_base(view, j);
            if (cur->virtualStart <= key.virtualStart) break;
            *(XModbusHubCacheSlice*)XVector_at_base(view, j + 1) = *cur;
            --j;
        }
        *(XModbusHubCacheSlice*)XVector_at_base(view, j + 1) = key;
    }
}

/** @brief 二分定位覆盖 virtualAddr 的视图切片下标；未命中返回 -1。 */
static int hubViewFind(const struct XVector* view, uint16_t virtualAddr)
{
    size_t lo = 0;
    size_t hi = view ? XVector_size_base(view) : 0;
    while (lo < hi) {
        const size_t mid = lo + (hi - lo) / 2;
        const XModbusHubCacheSlice* s =
            (const XModbusHubCacheSlice*)XVector_at_base(view, (int64_t)mid);
        if (!s) return -1;
        if (virtualAddr < s->virtualStart) {
            hi = mid;
        } else if ((uint32_t)virtualAddr >= (uint32_t)s->virtualStart + s->count) {
            lo = mid + 1;
        } else {
            return (int)mid;
        }
    }
    return -1;
}

/**
 * @brief 把旧块缓存值/脏区间按远端交集迁入新块。
 * @details 映射刷新（addRange/removeRange/换 mapping）时值不能丢：新块逐点
 *          按 (区, 从站, 远端地址) 回查旧块；脏区间只在完整覆盖时迁入
 *          （部分覆盖的写已被裁剪，按新意图重写即可）。
 */
static void hubMigrateBlockState(const XModbusHubDevice* dev,
                                 XModbusHubCacheBlock* newBlock)
{
    size_t i;
    size_t n;
    if (!dev || !dev->blocks || !newBlock) return;
    /* 值：逐点回查旧块。 */
    for (uint16_t k = 0; k < newBlock->count; ++k) {
        const uint32_t remote = (uint32_t)newBlock->remoteStart + k;
        const XModbusHubCacheBlock* old =
            hubFindBlockAt((XModbusHubDevice*)dev, newBlock->type,
                           newBlock->unitId, remote);
        if (old) {
            XModbusDataUnit_setValue(newBlock->data, k,
                                     XModbusDataUnit_value(old->data,
                                                           remote - old->remoteStart));
            /* 质量时间戳同样要迁：[2026-10-29] 重建（startPolling/
             * stopPolling/setPollAreas/映射刷新都会触发）不迁 lastOkMs 会让
             * valueAge/pointValid 在每次启停后失真（恒 0 = 从未读回）。 */
            if (old->lastOkMs > newBlock->lastOkMs)
                newBlock->lastOkMs = old->lastOkMs;
            if (old->failCount > newBlock->failCount)
                newBlock->failCount = old->failCount;
        }
    }
    /* 脏区间：完整落在新块内的迁入。 */
    n = XVector_size_base(dev->blocks);
    for (i = 0; i < n; ++i) {
        const XModbusHubCacheBlock* old =
            *(XModbusHubCacheBlock* const*)XVector_at_base(dev->blocks, (int64_t)i);
        size_t k;
        if (!old || old->type != newBlock->type || old->unitId != newBlock->unitId) continue;
        for (k = 0; k < XVector_size_base(old->dirty); ++k) {
            const XModbusHubDirtyRange* d =
                (const XModbusHubDirtyRange*)XVector_at_base(old->dirty, (int64_t)k);
            const uint32_t start = (uint32_t)old->remoteStart + d->offset;
            if (start < newBlock->remoteStart) continue;
            if (start + d->count > (uint32_t)newBlock->remoteStart + newBlock->count) continue;
            {
                XModbusHubDirtyRange r = *d;
                r.offset = (uint16_t)(start - newBlock->remoteStart);
                hubDirtyPushRaw(newBlock, &r);
            }
        }
    }
}

/**
 * @brief 重建某设备的缓存块表（同区同从站远端连续的映射段合并成一块）。
 * @return true=成功（失败保留旧表）。
 */
static bool hubRebuildDeviceBlocks(XModbusHub* hub, XModbusHubDevice* dev)
{
    struct XVector* newBlocks;
    bool ok = true;
    if (!hub || !dev) return false;
    newBlocks = XVector_create_ex(XCLASS_DEFAULT_MEMORY_TYPE,
                                  sizeof(XModbusHubCacheBlock*), false);
    if (!newBlocks) return false;
    if (hub->m_mapping) {
        for (int t = 0; t < 4 && ok; ++t) {
            const XModbusRegisterType enumType = (XModbusRegisterType)hubEnumOfType(t);
            const size_t total = XModbusMapping_rangeCount_const(hub->m_mapping, enumType);
            XModbusMapRange* arr;
            int got;
            int i;
            if (total == 0) continue;
            arr = (XModbusMapRange*)XMalloc_System(sizeof(XModbusMapRange) * total);
            if (!arr) { ok = false; break; }
            got = (int)XModbusMapping_segmentsForDevice_const(hub->m_mapping, dev->id,
                                                              enumType, arr, (int)total);
            if (got < 0) got = 0;
            /* 插入排序：从站升序、远端起点升序（块/读组有序性的前提）。 */
            for (i = 1; i < got; ++i) {
                const XModbusMapRange key = arr[i];
                int j = i - 1;
                while (j >= 0 && (arr[j].unitId > key.unitId ||
                       (arr[j].unitId == key.unitId && arr[j].remoteStart > key.remoteStart))) {
                    arr[j + 1] = arr[j];
                    --j;
                }
                arr[j + 1] = key;
            }
            i = 0;
            while (i < got && ok) {
                const int unit = arr[i].unitId;
                const uint32_t start = arr[i].remoteStart;
                uint32_t end = start + arr[i].count;
                int j = i + 1;
                while (j < got && arr[j].unitId == unit &&
                       (uint32_t)arr[j].remoteStart == end) {
                    end += arr[j].count;
                    ++j;
                }
                {
                    XModbusHubCacheBlock* b = hubBlockCreate((uint16_t)enumType, unit,
                                                             (uint16_t)start,
                                                             (uint16_t)(end - start));
                    if (!b) { ok = false; break; }
                    for (int k = i; k < j; ++k) {
                        XModbusHubCacheSeg seg;
                        seg.virtualStart = arr[k].virtualStart;
                        seg.remoteStart = arr[k].remoteStart;
                        seg.count = arr[k].count;
                        XVector_push_back_1_base(b->segs, &seg);
                    }
                    hubSortSegs(b->segs);
                    hubMigrateBlockState(dev, b);
                    XVector_push_back_1_base(newBlocks, &b);
                }
                i = j;
            }
            XFree_System(arr);
        }
    }
    if (!ok) {
        hubBlocksFreeVector(newBlocks);
        return false;
    }
    if (dev->blocks) hubBlocksFreeVector(dev->blocks);
    dev->blocks = newBlocks;
    return true;
}

/** @brief 重建虚拟视图切片表与各类型虚拟跨度（cacheCount 口径）。 */
static void hubRebuildView(XModbusHub* hub)
{
    int t;
    if (!hub) return;
    for (t = 0; t < 4; ++t) {
        size_t i;
        size_t n;
        if (!hub->m_view[t]) {
            hub->m_view[t] = XVector_create_ex(XCLASS_DEFAULT_MEMORY_TYPE,
                                               sizeof(XModbusHubCacheSlice), false);
        }
        if (!hub->m_view[t]) continue;
        XVector_clear_base(hub->m_view[t]);
        hub->m_spanStart[t] = 0;
        hub->m_spanCount[t] = 0;
        if (hub->m_mapping) {
            const XModbusRegisterType enumType = (XModbusRegisterType)hubEnumOfType(t);
            const size_t rn = XModbusMapping_rangeCount_const(hub->m_mapping, enumType);
            uint32_t minV = 0x10000u;
            uint32_t maxV = 0;
            for (i = 0; i < rn; ++i) {
                const XModbusMapRange* r =
                    XModbusMapping_rangeAt_const(hub->m_mapping, enumType, i);
                if (!r) continue;
                if (r->virtualStart < minV) minV = r->virtualStart;
                if ((uint32_t)r->virtualStart + r->count > maxV)
                    maxV = (uint32_t)r->virtualStart + r->count;
            }
            if (maxV > minV) {
                hub->m_spanStart[t] = (uint16_t)minV;
                hub->m_spanCount[t] = (uint16_t)(maxV - minV);
            }
        }
        /* 切片一律从块内的映射切片生成：视图与块坐标天然一致。 */
        if (hub->m_devices) {
            const size_t dn = XVector_size_base(hub->m_devices);
            for (i = 0; i < dn; ++i) {
                const XModbusHubDevice* dev =
                    (const XModbusHubDevice*)XVector_at_base(hub->m_devices, (int64_t)i);
                size_t bi;
                if (!dev || !dev->blocks) continue;
                for (bi = 0; bi < XVector_size_base(dev->blocks); ++bi) {
                    const XModbusHubCacheBlock* b =
                        *(XModbusHubCacheBlock* const*)XVector_at_base(dev->blocks, (int64_t)bi);
                    size_t si;
                    if (!b || b->type != (uint16_t)hubEnumOfType(t)) continue;
                    for (si = 0; si < XVector_size_base(b->segs); ++si) {
                        const XModbusHubCacheSeg* seg =
                            (const XModbusHubCacheSeg*)XVector_at_base(b->segs, (int64_t)si);
                        XModbusHubCacheSlice s;
                        if (!seg) continue;
                        s.virtualStart = seg->virtualStart;
                        s.count = seg->count;
                        s.deviceId = dev->id;
                        s.unitId = b->unitId;
                        s.blockOffset = (uint16_t)(seg->remoteStart - b->remoteStart);
                        s.block = (XModbusHubCacheBlock*)b;
                        XVector_push_back_1_base(hub->m_view[t], &s);
                    }
                }
            }
            hubSortView(hub->m_view[t]);
        }
        /* 剩余 n 仅供调试观察。 */
        n = XVector_size_base(hub->m_view[t]);
        (void)n;
    }
}

/**
 * @brief 重建某设备的读组表（合并后的轮询帧计划）。
 * @details 块表已按 (区, 从站, 远端) 有序：同区同从站、远端间隔 ≤
 *          m_readMergeGap 的相邻块并组，组内总长受协议单帧上限约束；
 *          单块超限时拆成多组（同块多帧）。读组合并只影响读——写不跨洞。
 */
static void hubRebuildGroups(XModbusHub* hub, XModbusHubDevice* dev)
{
    size_t first = 0;
    size_t total;
    if (!hub || !dev) return;
    if (!dev->groups) {
        dev->groups = XVector_create_ex(XCLASS_DEFAULT_MEMORY_TYPE,
                                        sizeof(XModbusHubReadGroup), false);
    }
    if (!dev->groups) return;
    XVector_clear_base(dev->groups);
    dev->groupCursor = 0;
    if (!dev->blocks) return;
    total = XVector_size_base(dev->blocks);
    while (first < total) {
        XModbusHubCacheBlock* b =
            *(XModbusHubCacheBlock**)XVector_at_base(dev->blocks, (int64_t)first);
        XModbusHubReadGroup g;
        size_t next;
        if (!b) { ++first; continue; }
        memset(&g, 0, sizeof(g));
        g.type = b->type;
        g.unitId = b->unitId;
        g.remoteStart = b->remoteStart;
        g.count = b->count;
        g.firstBlock = (uint32_t)first;
        g.blockCount = 1;
        /* 单块超单帧上限：同块拆多组。 */
        if (b->count > hubReadLimitOfType(b->type)) {
            uint32_t off = 0;
            while (off < b->count) {
                const uint32_t n = (uint32_t)hubReadLimitOfType(b->type);
                const uint32_t take = (b->count - off > n) ? n : (b->count - off);
                XModbusHubReadGroup part = g;
                part.remoteStart = (uint16_t)(b->remoteStart + off);
                part.count = (uint16_t)take;
                XVector_push_back_1_base(dev->groups, &part);
                off += take;
            }
            ++first;
            continue;
        }
        next = first + 1;
        while (next < total) {
            XModbusHubCacheBlock* nb =
                *(XModbusHubCacheBlock**)XVector_at_base(dev->blocks, (int64_t)next);
            uint32_t gEnd;
            uint32_t gap;
            uint32_t newCount;
            if (!nb || nb->type != g.type || nb->unitId != g.unitId) break;
            gEnd = (uint32_t)g.remoteStart + g.count;
            if (nb->remoteStart < gEnd) break;              /* 重叠不该发生：保守断开。 */
            gap = (uint32_t)nb->remoteStart - gEnd;
            if (gap > (uint32_t)hub->m_readMergeGap) break;
            newCount = (uint32_t)nb->remoteStart + nb->count - g.remoteStart;
            if (newCount > hubReadLimitOfType(g.type)) break;
            g.count = (uint16_t)newCount;
            ++g.blockCount;
            ++next;
        }
        XVector_push_back_1_base(dev->groups, &g);
        first = next;
    }
}

/** @brief 重建全部：设备块表 → 虚拟视图 → 读组/退避表。 */
static void hubRebuildAll(XModbusHub* hub)
{
    size_t i;
    if (!hub) return;
    if (hub->m_devices) {
        for (i = 0; i < XVector_size_base(hub->m_devices); ++i) {
            XModbusHubDevice* d =
                (XModbusHubDevice*)XVector_at_base(hub->m_devices, (int64_t)i);
            if (d) (void)hubRebuildDeviceBlocks(hub, d);
        }
    }
    hubRebuildView(hub);
    if (!hub->m_devices) return;
    for (i = 0; i < XVector_size_base(hub->m_devices); ++i) {
        XModbusHubDevice* d =
            (XModbusHubDevice*)XVector_at_base(hub->m_devices, (int64_t)i);
        if (!d) continue;
        hubRebuildGroups(hub, d);
        hubRebuildUnitSched(hub, d);
    }
}

/* ==================== 信号辅助 ==================== */

/**
 * @brief 回填一帧读应答到读组覆盖的各块（按远端交集切分）。
 * @details 变化检测在回填前完成（逐切片比对旧值）；首次回填（块从未成功
 *          读过）**恒发**一次 valueChanged——否则首采为 0 的点永远收不到
 *          初值，违反「挂接后至少推送一次供播种」的通道契约。
 *          写未确认的点（脏区间内）不被轮询覆写。
 */
static void hubFillGroupResult(XModbusHub* hub, XModbusHubDevice* dev,
                               const XModbusHubReadGroup* group,
                               const XModbusDataUnit* result)
{
    const uint64_t now = XDateTime_currentMSecsSinceEpoch();
    uint32_t bi;
    if (!hub || !dev || !group || !result || !dev->blocks) return;
    for (bi = group->firstBlock; bi < group->firstBlock + group->blockCount; ++bi) {
        XModbusHubCacheBlock* b;
        uint32_t lo, hi;
        bool firstFill;
        size_t si;
        if (bi >= XVector_size_base(dev->blocks)) break;
        b = *(XModbusHubCacheBlock**)XVector_at_base(dev->blocks, (int64_t)bi);
        if (!b || b->type != group->type || b->unitId != group->unitId) continue;
        lo = ((uint32_t)b->remoteStart > group->remoteStart)
                 ? b->remoteStart : group->remoteStart;
        hi = ((uint32_t)b->remoteStart + b->count <
              (uint32_t)group->remoteStart + group->count)
                 ? (uint32_t)b->remoteStart + b->count
                 : (uint32_t)group->remoteStart + group->count;
        if (hi <= lo) continue;
        firstFill = (b->lastOkMs == 0);

        /* ① 变化检测（按块内映射切片；脏点跳过）。 */
        for (si = 0; si < XVector_size_base(b->segs); ++si) {
            const XModbusHubCacheSeg* seg =
                (const XModbusHubCacheSeg*)XVector_at_base(b->segs, (int64_t)si);
            uint32_t sStart, sEnd, cLo, cHi;
            bool changed = false;
            if (!seg) continue;
            sStart = seg->remoteStart;
            sEnd = sStart + seg->count;
            cLo = (sStart > lo) ? sStart : lo;
            cHi = (sEnd < hi) ? sEnd : hi;
            if (cHi <= cLo) continue;
            for (uint32_t r = cLo; r < cHi; ++r) {
                const uint16_t off = (uint16_t)(r - b->remoteStart);
                const uint16_t newVal =
                    XModbusDataUnit_value(result, (size_t)(r - group->remoteStart));
                if (hubDirtyHasOffset(b, off)) continue;
                if (newVal != XModbusDataUnit_value(b->data, off)) {
                    changed = true;
                    break;
                }
            }
            if (changed || firstFill) {
                XModbusHub_valueChanged_signal(
                    hub, (int)b->type,
                    (int)(seg->virtualStart + (cLo - sStart)), (int)(cHi - cLo));
            }
        }
        /* ② 写入（脏点保留写意图值）。 */
        for (uint32_t r = lo; r < hi; ++r) {
            const uint16_t off = (uint16_t)(r - b->remoteStart);
            const uint16_t newVal =
                XModbusDataUnit_value(result, (size_t)(r - group->remoteStart));
            if (hubDirtyHasOffset(b, off)) continue;
            if (newVal != XModbusDataUnit_value(b->data, off))
                XModbusDataUnit_setValue(b->data, off, newVal);
        }
        b->lastOkMs = now;
        b->failCount = 0;
        b->refreshRequested = false;
    }
}

/**
 * @brief 脏区间块内偏移 → 统一空间虚拟起始地址（按块段表反查）。
 * @details writeStalled/writeCompleted 的契约参数是 virtualStart；失败沿
 *          发射点只有块内偏移，必须经段表翻译——远端≠虚拟的映射下直发
 *          远端地址会让上层把故障挂到错误的点位上。段表未命中属不变量
 *          破坏（脏区间恒在块内、块恒有段覆盖），退化为首段虚拟起点。
 */
static int hubDirtyVirtualStart(const XModbusHubCacheBlock* b,
                                const XModbusHubDirtyRange* d)
{
    const uint32_t r = (uint32_t)b->remoteStart + d->offset;
    size_t si;
    for (si = 0; si < XVector_size_base(b->segs); ++si) {
        const XModbusHubCacheSeg* seg =
            (const XModbusHubCacheSeg*)XVector_at_base(b->segs, (int64_t)si);
        if (seg && r >= seg->remoteStart &&
            r < (uint32_t)seg->remoteStart + seg->count)
            return (int)(seg->virtualStart + (r - seg->remoteStart));
    }
    if (XVector_size_base(b->segs) > 0) {
        const XModbusHubCacheSeg* seg0 =
            (const XModbusHubCacheSeg*)XVector_at_base(b->segs, 0);
        if (seg0) return (int)seg0->virtualStart;
    }
    return 0;
}

/** @brief 按视图切片逐段发 writeCompleted（上层看到的粒度=映射段）。 */
static void hubEmitWriteDone(XModbusHub* hub, uint16_t type,
                             uint16_t virtualStart, uint16_t count, bool ok)
{
    const int t = hubTypeIndex((int)type);
    const uint32_t end = (uint32_t)virtualStart + count;
    bool emitted = false;
    size_t i;
    if (!hub || t < 0 || !hub->m_view[t] || count == 0) return;
    for (i = 0; i < XVector_size_base(hub->m_view[t]); ++i) {
        const XModbusHubCacheSlice* s =
            (const XModbusHubCacheSlice*)XVector_at_base(hub->m_view[t], (int64_t)i);
        uint32_t lo, hi;
        if (!s) continue;
        lo = ((uint32_t)s->virtualStart > virtualStart) ? s->virtualStart : virtualStart;
        hi = ((uint32_t)s->virtualStart + s->count < end)
                 ? (uint32_t)s->virtualStart + s->count : end;
        if (hi <= lo) continue;
        XModbusHub_writeCompleted_signal(hub, (int)type, (int)lo, (int)(hi - lo), ok);
        emitted = true;
    }
    if (!emitted)
        XModbusHub_writeCompleted_signal(hub, (int)type, (int)virtualStart,
                                         (int)count, ok);
}

/* ==================== 点位翻译 ==================== */

/** @brief 虚拟地址 → (设备槽下标, 块, 块内偏移)；命中返回 true。 */
static bool hubTranslateVirtual(const XModbusHub* hub, XModbusRegisterType type,
                                uint16_t virtualAddr, int* outDevIndex,
                                XModbusHubCacheBlock** outBlock,
                                uint16_t* outBlockOffset)
{
    const int t = hubTypeIndex((int)type);
    const int vi = (t < 0 || !hub->m_view[t]) ? -1 : hubViewFind(hub->m_view[t], virtualAddr);
    const XModbusHubCacheSlice* s;
    int di;
    if (vi < 0) return false;
    s = (const XModbusHubCacheSlice*)XVector_at_base(hub->m_view[t], (int64_t)vi);
    if (!s || !s->block) return false;
    di = hubDeviceIndexById(hub, s->deviceId);
    if (di < 0) return false;
    if (outDevIndex) *outDevIndex = di;
    if (outBlock) *outBlock = s->block;
    if (outBlockOffset) *outBlockOffset = (uint16_t)(s->blockOffset +
                                                     (virtualAddr - s->virtualStart));
    return true;
}

/* ==================== 发送（轮询/写） ==================== */

/**
 * @brief [2026-10-28 单元退避] 从站是否静默中（退避表查询）。
 * @return 静默返回 true；未登记返回 false（不拦截）。
 */
static bool hubUnitMuted(XModbusHubDevice* dev, int unitId, uint64_t nowMs)
{
    if (!dev || unitId < 0) return false;
    return XPollScheduler_isMutedKey(&dev->unitSched, unitId, nowMs);
}

/**
 * @brief [2026-10-28 单元退避] 块表重建后同步设备退避表：清表重建，按
 *        块内 unitId 去重登记簿记条目（阈值/时长沿用 Hub 配置）。
 */
static void hubRebuildUnitSched(XModbusHub* hub, XModbusHubDevice* dev)
{
    size_t i;
    size_t n;
    if (!hub || !dev) return;
    XPollScheduler_deinit(&dev->unitSched);
    XPollScheduler_init(&dev->unitSched);
    XPollScheduler_setBackoff(&dev->unitSched, hub->m_unitMaxFails, hub->m_unitMuteMs);
    if (!dev->blocks) return;
    n = XVector_size_base(dev->blocks);
    for (i = 0; i < n; ++i) {
        const XModbusHubCacheBlock* b =
            *(XModbusHubCacheBlock* const*)XVector_at_base(dev->blocks, (int64_t)i);
        if (b) (void)XPollScheduler_registerKey(&dev->unitSched, b->unitId, 0);
    }
}

/** @brief 读组是否该发：区域在轮询掩码内，或有块被 requestRefresh 标脏。 */
static bool hubGroupReadable(const XModbusHub* hub, const XModbusHubDevice* dev,
                             const XModbusHubReadGroup* group)
{
    uint32_t bi;
    if (!hub || !dev || !group) return false;
    if (hub->m_pollAreas & (1u << group->type)) return true;
    if (!dev->blocks) return false;
    for (bi = group->firstBlock; bi < group->firstBlock + group->blockCount; ++bi) {
        const XModbusHubCacheBlock* b;
        if (bi >= XVector_size_base(dev->blocks)) break;
        b = *(XModbusHubCacheBlock* const*)XVector_at_base(dev->blocks, (int64_t)bi);
        if (b && b->refreshRequested) return true;
    }
    return false;
}

/**
 * @brief 发一个读组。请求单元为栈上临时对象——sendReadRequest 经
 *        createReply 深拷贝收编，返回后即可析构。
 * @return true=已上线（应答异步回 onReplyFinished）；false=未受理。
 */
static bool hubSendPollGroup(XModbusHub* hub, XModbusHubDevice* dev,
                             const XModbusHubReadGroup* group)
{
    XModbusDataUnit unit;
    XModbusReply* reply;
    uint32_t bi;
    if (!hub || !dev || !dev->client || !group || group->count == 0) return false;
    XModbusDataUnit_init_ex(&unit, (XModbusRegisterType)group->type,
                            group->remoteStart, group->count);
    reply = XModbusClient_sendReadRequest(dev->client, &unit, group->unitId,
                                          XModbusRequestPriority_Normal);
    XClassDeinit(&unit);
    if (!reply) return false;
    /* 刷新标记在发送时清除（一次尝试即算响应请求；失败由退避/重试兜底）。 */
    for (bi = group->firstBlock; bi < group->firstBlock + group->blockCount; ++bi) {
        XModbusHubCacheBlock* b;
        if (bi >= XVector_size_base(dev->blocks)) break;
        b = *(XModbusHubCacheBlock**)XVector_at_base(dev->blocks, (int64_t)bi);
        if (b) b->refreshRequested = false;
    }

    XObject_setParent((XObject*)reply, (XObject*)hub);
    dev->inflight = reply;
    dev->inflightKind = XModbusHubInflight_Poll;
    dev->inflightGroup = *group;
    /* [A3 时序缺口兜底] reply 可能在 send 内部即同步终态（未连接/io 关闭/
     * 忙/空 PDU/发送失败等收口分支，setError 同步发射 finished）——此刻
     * finished 已发射给空监听集，connect_2 再挂也收不到。若不就地收口，
     * dev->inflight 永卡：状态变化槽不清 inflight、重连续泵被单飞闸挡死
     * →该设备轮询永久停摆。先登记再直调收口槽＝与异步收口完全等价的路径
     * （退避记账/回填/统计/续泵全不走样）。递归有界：首个同步写/发失败已
     * 把设备拉成 Unconnected，收口尾部续泵会被 hubPumpDevice 的连接态
     * 门槛挡住。 */
    if (XModbusReply_isFinished(reply)) {
        if (!s_hubInSyncFinish) {
            s_hubInSyncFinish = 1;
            XModbusHub_onReplyFinished((XObject*)reply, NULL);
            s_hubInSyncFinish = 0;
        } else {
            dev->inflight = NULL;
            dev->inflightKind = XModbusHubInflight_None;
            XObject_deleteLater((XObject*)reply);
        }
        return true;
    }
    XObject_connect_2((XObject*)reply,
                      XSignal(XModbusReply_finished_signal),
                      XModbusHub_onReplyFinished);
    return true;
}

/** @brief 发一条脏区间（FC15/FC16；单帧按协议上限切分）。 */
static bool hubSendDirtyRange(XModbusHub* hub, XModbusHubDevice* dev,
                              XModbusHubCacheBlock* block,
                              const XModbusHubDirtyRange* dirty)
{
    XModbusDataUnit unit;
    XModbusReply* reply;
    uint16_t limit;
    uint16_t n;
    uint16_t virtualStart = 0;
    size_t si;
    if (!hub || !dev || !dev->client || !block || !dirty || dirty->count == 0) return false;
    limit = hubWriteLimitOfType(block->type);
    n = (dirty->count > limit) ? limit : dirty->count;

    /* 统一空间起点：块内远端 → 切片 → 虚拟地址。 */
    for (si = 0; si < XVector_size_base(block->segs); ++si) {
        const XModbusHubCacheSeg* seg =
            (const XModbusHubCacheSeg*)XVector_at_base(block->segs, (int64_t)si);
        const uint32_t r = (uint32_t)block->remoteStart + dirty->offset;
        if (!seg) continue;
        if (r >= seg->remoteStart && r < (uint32_t)seg->remoteStart + seg->count) {
            virtualStart = (uint16_t)(seg->virtualStart + (r - seg->remoteStart));
            break;
        }
    }

    XModbusDataUnit_init_ex(&unit, (XModbusRegisterType)block->type,
                            (uint16_t)(block->remoteStart + dirty->offset), n);
    for (uint16_t k = 0; k < n; ++k)
        XModbusDataUnit_setValue(&unit, k,
                                 XModbusDataUnit_value(block->data, dirty->offset + k));
    /* [2026-10-28 写优先级] 写走 High 队列在传输层插队（轮询读保持 Normal）；
     * 即时下发与滞留重发共用本入口，一处收口。 */
    reply = XModbusClient_sendWriteRequest(dev->client, &unit, block->unitId,
                                           XModbusRequestPriority_High);
    XClassDeinit(&unit);
    if (!reply) return false;

    XObject_setParent((XObject*)reply, (XObject*)hub);
    /* 单飞登记：快照意图（脏区间合并/重建不影响收口）。 */
    if (dev->inflightWrite.values) {
        XFree_System(dev->inflightWrite.values);
        dev->inflightWrite.values = NULL;
    }
    memset(&dev->inflightWrite, 0, sizeof(dev->inflightWrite));
    dev->inflightWrite.type = block->type;
    dev->inflightWrite.unitId = block->unitId;
    dev->inflightWrite.remoteStart = (uint16_t)(block->remoteStart + dirty->offset);
    dev->inflightWrite.count = n;
    dev->inflightWrite.virtualStart = virtualStart;
    dev->inflightWrite.virtualCount = n;
    dev->inflightWrite.attempts = dirty->attempts;
    dev->inflightWrite.values = (uint16_t*)XMalloc_System(sizeof(uint16_t) * n);
    if (dev->inflightWrite.values) {
        for (uint16_t k = 0; k < n; ++k)
            dev->inflightWrite.values[k] =
                XModbusDataUnit_value(block->data, dirty->offset + k);
    }
    dev->inflight = reply;
    dev->inflightKind = XModbusHubInflight_Write;
    /* [A3 时序缺口兜底] 同上：send 内部同步终态就地带 inflight 登记直调
     * 收口槽（脏区间留表 + 续泵等价），防 dev->inflight 永卡单飞。 */
    if (XModbusReply_isFinished(reply)) {
        if (!s_hubInSyncFinish) {
            s_hubInSyncFinish = 1;
            XModbusHub_onReplyFinished((XObject*)reply, NULL);
            s_hubInSyncFinish = 0;
        } else {
            dev->inflight = NULL;
            dev->inflightKind = XModbusHubInflight_None;
            XObject_deleteLater((XObject*)reply);
        }
        return true;
    }
    XObject_connect_2((XObject*)reply,
                      XSignal(XModbusReply_finished_signal),
                      XModbusHub_onReplyFinished);
    return true;
}

/**
 * @brief 取该设备首条脏区间并下发（一次一帧，单飞）。
 * @return true=已发（或同步终态已收口）；false=无可发。
 */
static bool hubSendNextWrite(XModbusHub* hub, XModbusHubDevice* dev)
{
    size_t i;
    if (!hub || !dev || !dev->blocks) return false;
    for (i = 0; i < XVector_size_base(dev->blocks); ++i) {
        XModbusHubCacheBlock* b =
            *(XModbusHubCacheBlock**)XVector_at_base(dev->blocks, (int64_t)i);
        XModbusHubDirtyRange* d;
        if (!b || !b->dirty || XVector_size_base(b->dirty) == 0) continue;
        d = (XModbusHubDirtyRange*)XVector_at_base(b->dirty, 0);
        if (!d) continue;
        ++d->attempts;                                  /* 下发次数（失败重发累计）。 */
        if (hub->m_writeStallNoticeAt >= 1 &&
            d->attempts >= (uint32_t)hub->m_writeStallNoticeAt) {
            const int virtualStart = hubDirtyVirtualStart(b, d);
            if (!d->stallNotified) {
                d->stallNotified = true;
                XModbusHub_writeStalled_signal(hub, (int)b->type,
                                               virtualStart,
                                               (int)d->count, (int)d->attempts);
            }
            if (!d->failNotified) {
                d->failNotified = true;
                /* [2026-10-29 失败沿] 写失败对上层可见：通道层
                 * XDigitalOutput_writeDone(false) 的故障判据。仍继续重发。 */
                hubEmitWriteDone(hub, b->type, (uint16_t)virtualStart,
                                 d->count, false);
            }
        }
        return hubSendDirtyRange(hub, dev, b, d);
    }
    return false;
}

/**
 * @brief 统一泵：设备空闲（单飞未占用且在线）时——先写后轮询（写延迟优先）。
 *        轮询拍/重连恢复/写入队/重发拍/应答收口共用此入口。
 */
static void hubPumpDevice(XModbusHub* hub, XModbusHubDevice* dev)
{
    if (!hub || !dev || !dev->client) return;
    if (dev->inflight) return;                                   /* 单飞占用。 */
    if (dev->state != XModbusDevice_ConnectedState) return;      /* 离线安静跳过。 */

    /* ① 写优先：该设备有脏区间；合并窗口未到期时等窗口（flush 定时器）。 */
    if (hubDeviceHasDirty(dev)) {
        const bool windowOpen = (hub->m_writeCoalesceMs > 0 &&
                                 hub->m_flushTimer != XTIMER_INVALID_ID);
        if (!windowOpen) {
            if (hubSendNextWrite(hub, dev)) return;
        }
    }
    /* ② 轮询：总闸开且读组非空；groupCursor 回卷。[2026-10-28 单元退避]
     *    静默从站的组跳过——RTU 共口多从站时把总线让给活从站（死从站
     *    不再占用单飞槽吃超时），静默到期自动恢复探活；全部静默则本拍
     *    让总线。 */
    if (!hub->m_polling || !dev->groups) return;
    {
        const size_t gn = XVector_size_base(dev->groups);
        const uint64_t nowMs = XDateTime_currentMSecsSinceEpoch();
        if (gn == 0) return;
        if (dev->groupCursor >= gn) dev->groupCursor = 0;
        for (size_t scanned = 0; scanned < gn; ++scanned) {
            const size_t idx = (dev->groupCursor + scanned) % gn;
            const XModbusHubReadGroup* g =
                (const XModbusHubReadGroup*)XVector_at_base(dev->groups, (int64_t)idx);
            if (!g) continue;
            if (hubUnitMuted(dev, g->unitId, nowMs)) continue;
            if (!hubGroupReadable(hub, dev, g)) continue;
            if (hubSendPollGroup(hub, dev, g)) {
                dev->groupCursor = (idx + 1) % gn;
                return;
            }
        }
    }
}

/** @brief 写队列泵：逐设备尝试下发（空闲设备才发）。 */
static void hubPumpWriteQueue(XModbusHub* hub)
{
    size_t i;
    if (!hub || !hub->m_devices) return;
    for (i = 0; i < XVector_size_base(hub->m_devices); ++i) {
        XModbusHubDevice* d =
            (XModbusHubDevice*)XVector_at_base(hub->m_devices, (int64_t)i);
        if (!d) continue;
        if (d->inflight) continue;
        if (d->state != XModbusDevice_ConnectedState) continue;
        if (hubDeviceHasDirty(d)) hubPumpDevice(hub, d);
    }
}

/* ==================== 虚函数 ==================== */

static void VXModbusHub_deinit(XModbusHub* hub)
{
    if (!hub) return;
    hub->m_polling = false;
    /* 定时器全部 kill（周期定时器泄漏纪律）；client/reply 是 hub 的
     * XObject 子对象，由父类析构链统一删除，这里只清自身容器。 */
    if (hub->m_retryTimer != XTIMER_INVALID_ID) {
        XObject_killTimer((XObject*)hub, hub->m_retryTimer);
        hub->m_retryTimer = XTIMER_INVALID_ID;
    }
    if (hub->m_flushTimer != XTIMER_INVALID_ID) {
        XObject_killTimer((XObject*)hub, hub->m_flushTimer);
        hub->m_flushTimer = XTIMER_INVALID_ID;
    }
    if (hub->m_devices) {
        const size_t n = XVector_size_base(hub->m_devices);
        for (size_t i = 0; i < n; ++i) {
            XModbusHubDevice* d =
                (XModbusHubDevice*)XVector_at_base(hub->m_devices, (int64_t)i);
            if (!d) continue;
            hubStopDevicePollTimer(hub, d);
            d->inflight = NULL;          /* Reply 是 hub 子对象，父析构统一收。 */
            d->client = NULL;            /* client 是 hub 子对象，父析构统一收。 */
            if (d->inflightWrite.values) {
                XFree_System(d->inflightWrite.values);
                d->inflightWrite.values = NULL;
            }
            if (d->name) {
                XClassDelete(d->name);
                d->name = NULL;
            }
            if (d->blocks) {
                hubBlocksFreeVector(d->blocks);
                d->blocks = NULL;
            }
            if (d->groups) {
                XClassDelete(d->groups);
                d->groups = NULL;
            }
            XPollScheduler_deinit(&d->unitSched);
        }
        XClassDelete(hub->m_devices);
        hub->m_devices = NULL;
    }
    if (hub->m_mapping) {
        XClassDelete(hub->m_mapping);
        hub->m_mapping = NULL;
    }
    for (int t = 0; t < 4; ++t) {
        if (hub->m_view[t]) {
            XClassDelete(hub->m_view[t]);
            hub->m_view[t] = NULL;
        }
    }
    XClass_Deinit_Parent(XObject, hub);
}

XVtable* XModbusHub_class_init(void)
{
    XVTABLE_INIT_DEFAULT(XModbusHub)
    XVTABLE_INHERIT_XCLASS(XObject);
    /* 无新增虚函数：仅重载既有槽（deinit/timerEvent），不追加函数表。 */
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Deinit, VXModbusHub_deinit);
    XVTABLE_OVERLOAD_DEFAULT(EXObject_TimerEvent, VXModbusHub_timerEvent);
    XCLASS_SHOW_SIZE_DEFAULT(XModbusHub);
    return XVTABLE_DEFAULT;
}

void XModbusHub_init(XModbusHub* hub)
{
    if (!hub) return;
    XObject_init((XObject*)hub);
    XClassGetVtable(hub) = XModbusHub_class_init();

    hub->m_mapping = NULL;
    hub->m_devices = XVector_create_ex(XCLASS_DEFAULT_MEMORY_TYPE,
                                       sizeof(XModbusHubDevice), false);
    hub->m_nextDeviceId = 1;
    for (int t = 0; t < 4; ++t) {
        hub->m_view[t] = XVector_create_ex(XCLASS_DEFAULT_MEMORY_TYPE,
                                           sizeof(XModbusHubCacheSlice), false);
        hub->m_spanStart[t] = 0;
        hub->m_spanCount[t] = 0;
    }
    hub->m_pollIntervalDefaultMs = 1000;
    /* 默认只轮询输入区（DI|IR）——效率最大化：输出区以写回显为准，
     * 不花流量读回。setPollAreas 可放宽。 */
    hub->m_pollAreas = (1u << XModbusDiscreteInputs) | (1u << XModbusInputRegisters);
    hub->m_polling = false;
    hub->m_retryTimer = XTIMER_INVALID_ID;
    hub->m_writeRetryIntervalMs = 1000;
    hub->m_changedOnlySignals = true;
    /* [2026-10-28 单元退避/写滞留可见性] 缺省对齐 XPollScheduler 缺省值。 */
    hub->m_unitMaxFails = XPOLL_SCHED_DEFAULT_MAX_FAILS;
    hub->m_unitMuteMs = XPOLL_SCHED_DEFAULT_MUTE_MS;
    hub->m_writeStallNoticeAt = 3;
    /* [2026-10-29 缓存层] 读合并缺口默认 0（行为与旧版一致，现场可调 4~8）；
     * 写合并窗口默认 0（受理即下发，行为与旧版一致）。 */
    hub->m_readMergeGap = 0;
    hub->m_writeCoalesceMs = 0;
    hub->m_flushTimer = XTIMER_INVALID_ID;
    hub->m_lastWriteMs = 0;
}

XModbusHub* XModbusHub_create_ex(XMemoryType memory)
{
    XModbusHub* hub = (XModbusHub*)XMemory_malloc(sizeof(XModbusHub), memory);
    if (!hub) return NULL;
    XModbusHub_init(hub);
    Set_Class_Memory(hub, memory); Set_Class_IsHeap(hub, true);
    return hub;
}

/* ==================== 设备管理 ==================== */

int XModbusHub_addDevice(XModbusHub* hub, XModbusClient* client, const char* name)
{
    if (!hub || !client || !hub->m_devices) return -1;
    XModbusHubDevice dev;
    memset(&dev, 0, sizeof(dev));
    dev.client = client;
    dev.id = hub->m_nextDeviceId++;
    dev.state = XModbusDevice_state((XModbusDevice*)client);
    dev.pollIntervalMs = hub->m_pollIntervalDefaultMs;
    dev.pollTimer = XTIMER_INVALID_ID;
    dev.inflight = NULL;
    dev.inflightKind = XModbusHubInflight_None;
    dev.blocks = NULL;
    dev.groups = XVector_create_ex(XCLASS_DEFAULT_MEMORY_TYPE,
                                   sizeof(XModbusHubReadGroup), false);
    if (name && name[0]) dev.name = XString_create_utf8(name);

    /* 所有权：client reparent 到 hub（XObject 父子析构链统一收尾）。
     * 底层断线自动重连常开（聚合设备对底层透明是核心承诺）。 */
    XObject_setParent((XObject*)client, (XObject*)hub);
    XModbusClient_setAutoReconnect(client, true);
    /* stateChanged：connect_2 → 槽第一参=真 sender=client（约束文档
     * 「信号与槽规范」：connect_1 第一参是 receiver，误读即错位）。 */
    XObject_connect_2((XObject*)client,
                      XSignal(XModbusDevice_stateChanged_signal),
                      XModbusHub_onDeviceStateChanged);

    if (!XVector_push_back_1_base(hub->m_devices, &dev)) {
        if (dev.name) XClassDelete(dev.name);
        if (dev.groups) XClassDelete(dev.groups);
        return -1;
    }
    hubRebuildAll(hub);
    return dev.id;
}

bool XModbusHub_removeDevice(XModbusHub* hub, int deviceId)
{
    XModbusHubDevice* dev = hubDeviceById(hub, deviceId);
    size_t bi;
    if (!dev) return false;
    hubStopDevicePollTimer(hub, dev);
    /* 滞留写意图作废：writeCompleted(ok=false) 告知调用方（逐脏区间）。 */
    if (dev->blocks) {
        for (bi = 0; bi < XVector_size_base(dev->blocks); ++bi) {
            XModbusHubCacheBlock* b =
                *(XModbusHubCacheBlock**)XVector_at_base(dev->blocks, (int64_t)bi);
            size_t di;
            if (!b || !b->dirty) continue;
            for (di = 0; di < XVector_size_base(b->dirty); ++di) {
                const XModbusHubDirtyRange* d =
                    (const XModbusHubDirtyRange*)XVector_at_base(b->dirty, (int64_t)di);
                if (!d) continue;
                hubEmitWriteDone(hub, b->type,
                                 (uint16_t)hubDirtyVirtualStart(b, d),
                                 d->count, false);
            }
        }
    }
    if (dev->inflight) {
        XObject_deleteLater((XObject*)dev->inflight);
        dev->inflight = NULL;
        dev->inflightKind = XModbusHubInflight_None;
    }
    if (dev->inflightWrite.values) {
        XFree_System(dev->inflightWrite.values);
        dev->inflightWrite.values = NULL;
    }
    if (dev->client) {
        XModbusDevice_disconnectDevice((XModbusDevice*)dev->client);
        XObject_deleteLater((XObject*)dev->client);   /* 自动脱离 hub 子列表。 */
        dev->client = NULL;
    }
    if (dev->name) {
        XClassDelete(dev->name);
        dev->name = NULL;
    }
    if (dev->blocks) {
        hubBlocksFreeVector(dev->blocks);
        dev->blocks = NULL;
    }
    if (dev->groups) {
        XClassDelete(dev->groups);
        dev->groups = NULL;
    }
    XPollScheduler_deinit(&dev->unitSched); /* 槽位摘除前清表。 */
    {
        const int idx = hubDeviceIndexById(hub, deviceId);
        if (idx >= 0) XVector_remove_base(hub->m_devices, idx, 1);
    }
    hubRebuildView(hub);                    /* 视图切片指向已释放块：必须重建。 */
    return true;
}

int XModbusHub_deviceCount(const XModbusHub* hub)
{
    if (!hub || !hub->m_devices) return 0;
    return (int)XVector_size_base(hub->m_devices);
}

XModbusClient* XModbusHub_deviceClient(const XModbusHub* hub, int deviceId)
{
    XModbusHubDevice* dev = hubDeviceById((XModbusHub*)hub, deviceId);
    return dev ? dev->client : NULL;
}

const char* XModbusHub_deviceName_const(const XModbusHub* hub, int deviceId)
{
    XModbusHubDevice* dev = hubDeviceById((XModbusHub*)hub, deviceId);
    return (dev && dev->name) ? XString_toUtf8(dev->name) : NULL;
}

XModbusDevice_State XModbusHub_deviceState(const XModbusHub* hub, int deviceId)
{
    XModbusHubDevice* dev = hubDeviceById((XModbusHub*)hub, deviceId);
    return dev ? dev->state : XModbusDevice_UnconnectedState;
}

bool XModbusHub_isOnline(const XModbusHub* hub)
{
    if (!hub || !hub->m_devices) return false;
    {
        const size_t n = XVector_size_base(hub->m_devices);
        if (n == 0) return false;
        for (size_t i = 0; i < n; ++i) {
            const XModbusHubDevice* d =
                (const XModbusHubDevice*)XVector_at_base(hub->m_devices, (int64_t)i);
            if (!d || d->state != XModbusDevice_ConnectedState) return false;
        }
    }
    return true;
}

bool XModbusHub_connectAll(XModbusHub* hub)
{
    bool all = true;
    if (!hub || !hub->m_devices) return false;
    {
        const size_t n = XVector_size_base(hub->m_devices);
        for (size_t i = 0; i < n; ++i) {
            XModbusHubDevice* d =
                (XModbusHubDevice*)XVector_at_base(hub->m_devices, (int64_t)i);
            if (!d || !d->client) continue;
            if (!XModbusDevice_connectDevice((XModbusDevice*)d->client)) all = false;
        }
    }
    return all;
}

void XModbusHub_disconnectAll(XModbusHub* hub)
{
    if (!hub || !hub->m_devices) return;
    XModbusHub_stopPolling(hub);
    {
        const size_t n = XVector_size_base(hub->m_devices);
        for (size_t i = 0; i < n; ++i) {
            XModbusHubDevice* d =
                (XModbusHubDevice*)XVector_at_base(hub->m_devices, (int64_t)i);
            if (d && d->client)
                XModbusDevice_disconnectDevice((XModbusDevice*)d->client);
        }
    }
}

/* ==================== 映射 ==================== */

void XModbusHub_setMapping(XModbusHub* hub, XModbusMapping* ownedMapping)
{
    if (!hub) {
        if (ownedMapping) XClassDelete(ownedMapping);
        return;
    }
    if (hub->m_mapping) XClassDelete(hub->m_mapping);
    hub->m_mapping = ownedMapping;
    hubRebuildAll(hub);
}

XModbusMapping* XModbusHub_mapping(const XModbusHub* hub)
{
    return hub ? hub->m_mapping : NULL;
}

void XModbusHub_refreshMapping(XModbusHub* hub)
{
    if (!hub) return;
    hubRebuildAll(hub);
}

/* ==================== 轮询 ==================== */

void XModbusHub_setPollIntervalDefault(XModbusHub* hub, size_t ms)
{
    if (hub) hub->m_pollIntervalDefaultMs = ms;
}

bool XModbusHub_setPollInterval(XModbusHub* hub, int deviceId, size_t ms)
{
    XModbusHubDevice* dev = hubDeviceById(hub, deviceId);
    if (!dev) return false;
    dev->pollIntervalMs = ms;
    if (hub->m_polling) {
        hubStopDevicePollTimer(hub, dev);
        if (ms > 0)
            dev->pollTimer = XObject_startTimer_ms((XObject*)hub,
                                                   (int)ms, XTimerType_CoarseTimer);
    }
    return true;
}

void XModbusHub_setPollAreas(XModbusHub* hub, unsigned areaMask)
{
    if (!hub) return;
    if (hub->m_pollAreas == areaMask) return;
    hub->m_pollAreas = areaMask;
    hubRebuildAll(hub);
}

void XModbusHub_setReadMergeGap(XModbusHub* hub, uint16_t gap)
{
    size_t i;
    if (!hub) return;
    if (hub->m_readMergeGap == gap) return;
    hub->m_readMergeGap = gap;
    if (!hub->m_devices) return;
    for (i = 0; i < XVector_size_base(hub->m_devices); ++i) {
        XModbusHubDevice* d =
            (XModbusHubDevice*)XVector_at_base(hub->m_devices, (int64_t)i);
        if (d) hubRebuildGroups(hub, d);
    }
}

void XModbusHub_setUnitBackoff(XModbusHub* hub, uint32_t maxFails, uint32_t muteMs)
{
    if (!hub) return;
    /* 门槛与 XPollScheduler_setBackoff 一致：>=1 / >=1000 才生效。 */
    if (maxFails >= 1) hub->m_unitMaxFails = maxFails;
    if (muteMs >= 1000) hub->m_unitMuteMs = muteMs;
    /* 已建设备即时同步（退避表在下次块表重建时也会按新配置重建）。 */
    if (hub->m_devices) {
        const size_t n = XVector_size_base(hub->m_devices);
        for (size_t i = 0; i < n; ++i) {
            XModbusHubDevice* d =
                (XModbusHubDevice*)XVector_at_base(hub->m_devices, (int64_t)i);
            if (d) XPollScheduler_setBackoff(&d->unitSched,
                                             hub->m_unitMaxFails, hub->m_unitMuteMs);
        }
    }
}

void XModbusHub_startPolling(XModbusHub* hub)
{
    if (!hub || hub->m_polling) return;
    hub->m_polling = true;
    /* 缓存/读组兜底重建（映射可能在 attach 后被直接编辑过）。 */
    hubRebuildAll(hub);
    if (hub->m_devices) {
        const size_t n = XVector_size_base(hub->m_devices);
        for (size_t i = 0; i < n; ++i) {
            XModbusHubDevice* d =
                (XModbusHubDevice*)XVector_at_base(hub->m_devices, (int64_t)i);
            if (!d) continue;
            hubStopDevicePollTimer(hub, d);
            if (d->pollIntervalMs > 0) {
                d->pollTimer = XObject_startTimer_ms((XObject*)hub,
                                                     (int)d->pollIntervalMs,
                                                     XTimerType_CoarseTimer);
            }
        }
    }
    /* 写重发定时器随总闸拉起（滞留脏区间恢复补发）。 */
    if (hub->m_retryTimer == XTIMER_INVALID_ID) {
        hub->m_retryTimer = XObject_startTimer_ms((XObject*)hub,
                                                  (int)hub->m_writeRetryIntervalMs,
                                                  XTimerType_CoarseTimer);
    }
    hubPumpWriteQueue(hub);
}

void XModbusHub_stopPolling(XModbusHub* hub)
{
    if (!hub) return;
    hub->m_polling = false;
    if (hub->m_devices) {
        const size_t n = XVector_size_base(hub->m_devices);
        for (size_t i = 0; i < n; ++i) {
            XModbusHubDevice* d =
                (XModbusHubDevice*)XVector_at_base(hub->m_devices, (int64_t)i);
            hubStopDevicePollTimer(hub, d);
        }
    }
    if (hub->m_retryTimer != XTIMER_INVALID_ID) {
        XObject_killTimer((XObject*)hub, hub->m_retryTimer);
        hub->m_retryTimer = XTIMER_INVALID_ID;
    }
    if (hub->m_flushTimer != XTIMER_INVALID_ID) {
        XObject_killTimer((XObject*)hub, hub->m_flushTimer);
        hub->m_flushTimer = XTIMER_INVALID_ID;
    }
}

bool XModbusHub_isPolling(const XModbusHub* hub)
{
    return hub ? hub->m_polling : false;
}

/* ==================== 统一缓存读 ==================== */

bool XModbusHub_value(const XModbusHub* hub, XModbusRegisterType type,
                      uint16_t virtualAddr, uint16_t* outValue)
{
    const int t = hubTypeIndex((int)type);
    int vi;
    const XModbusHubCacheSlice* s;
    if (!outValue) return false;
    *outValue = 0xFFFFu;
    if (!hub || t < 0 || !hub->m_view[t]) return false;
    vi = hubViewFind(hub->m_view[t], virtualAddr);
    if (vi < 0) return false;
    s = (const XModbusHubCacheSlice*)XVector_at_base(hub->m_view[t], (int64_t)vi);
    if (!s || !s->block) return false;
    *outValue = XModbusDataUnit_value(s->block->data,
                                      (size_t)s->blockOffset +
                                      (virtualAddr - s->virtualStart));
    return true;
}

bool XModbusHub_values(const XModbusHub* hub, XModbusRegisterType type,
                       uint16_t virtualStart, uint16_t count, uint16_t* outValues)
{
    bool all = true;
    if (!outValues) return false;
    for (uint16_t i = 0; i < count; ++i) {
        if (!XModbusHub_value(hub, type, (uint16_t)(virtualStart + i), &outValues[i])) {
            outValues[i] = 0xFFFFu; /* 未映射洞哨兵 */
            all = false;
        }
    }
    return all;
}

uint16_t XModbusHub_cacheCount(const XModbusHub* hub, XModbusRegisterType type)
{
    const int t = hubTypeIndex((int)type);
    if (!hub || t < 0) return 0;
    return hub->m_spanCount[t];
}

uint64_t XModbusHub_lastUpdateMs(const XModbusHub* hub, XModbusRegisterType type)
{
    uint64_t latest = 0;
    size_t i;
    if (!hub || !hub->m_devices) return 0;
    for (i = 0; i < XVector_size_base(hub->m_devices); ++i) {
        const XModbusHubDevice* d =
            (const XModbusHubDevice*)XVector_at_base(hub->m_devices, (int64_t)i);
        size_t bi;
        if (!d || !d->blocks) continue;
        for (bi = 0; bi < XVector_size_base(d->blocks); ++bi) {
            const XModbusHubCacheBlock* b =
                *(XModbusHubCacheBlock* const*)XVector_at_base(d->blocks, (int64_t)bi);
            if (!b || b->type != (uint16_t)type) continue;
            if (b->lastOkMs > latest) latest = b->lastOkMs;
        }
    }
    return latest;
}

/* ==================== 点级质量 ==================== */

bool XModbusHub_valueAge(const XModbusHub* hub, XModbusRegisterType type,
                         uint16_t virtualAddr, uint64_t* ageMs)
{
    const int t = hubTypeIndex((int)type);
    int vi;
    const XModbusHubCacheSlice* s;
    if (!hub || t < 0 || !hub->m_view[t]) return false;
    vi = hubViewFind(hub->m_view[t], virtualAddr);
    if (vi < 0) return false;
    s = (const XModbusHubCacheSlice*)XVector_at_base(hub->m_view[t], (int64_t)vi);
    if (!s || !s->block) return false;
    if (ageMs) {
        if (s->block->lastOkMs == 0) {
            *ageMs = UINT64_MAX;                 /* 从未成功读回。 */
        } else {
            const uint64_t now = XDateTime_currentMSecsSinceEpoch();
            *ageMs = (now > s->block->lastOkMs) ? (now - s->block->lastOkMs) : 0;
        }
    }
    return true;
}

bool XModbusHub_pointValid(const XModbusHub* hub, XModbusRegisterType type,
                           uint16_t virtualAddr, uint64_t maxAgeMs)
{
    uint64_t age = 0;
    if (!XModbusHub_valueAge(hub, type, virtualAddr, &age)) return false;
    return age != UINT64_MAX && age <= maxAgeMs;
}

bool XModbusHub_requestRefresh(XModbusHub* hub, XModbusRegisterType type,
                               uint16_t virtualStart, uint16_t count)
{
    const int t = hubTypeIndex((int)type);
    uint32_t end;
    bool hit = false;
    if (!hub || !hub->m_view || t < 0 || !hub->m_view[t] || count == 0) return false;
    end = (uint32_t)virtualStart + count;
    for (size_t i = 0; i < XVector_size_base(hub->m_view[t]); ++i) {
        const XModbusHubCacheSlice* s =
            (const XModbusHubCacheSlice*)XVector_at_base(hub->m_view[t], (int64_t)i);
        if (!s || !s->block) continue;
        if ((uint32_t)s->virtualStart >= end) break;
        if ((uint32_t)s->virtualStart + s->count <= virtualStart) continue;
        s->block->refreshRequested = true;
        hit = true;
    }
    if (!hit) return false;
    /* 立即泵一次（未连接/在途时由定时器与收口续泵接管）。 */
    if (hub->m_devices) {
        for (size_t i = 0; i < XVector_size_base(hub->m_devices); ++i) {
            XModbusHubDevice* d =
                (XModbusHubDevice*)XVector_at_base(hub->m_devices, (int64_t)i);
            if (d) hubPumpDevice(hub, d);
        }
    }
    return true;
}

/* ==================== 输出写 ==================== */

void XModbusHub_setWriteRetryInterval(XModbusHub* hub, size_t ms)
{
    if (!hub || ms == 0) return;
    hub->m_writeRetryIntervalMs = ms;
    if (hub->m_retryTimer != XTIMER_INVALID_ID) {
        XObject_killTimer((XObject*)hub, hub->m_retryTimer);
        hub->m_retryTimer = XObject_startTimer_ms((XObject*)hub,
                                                  (int)ms, XTimerType_CoarseTimer);
    }
}

void XModbusHub_setWriteCoalesceMs(XModbusHub* hub, size_t ms)
{
    if (hub) hub->m_writeCoalesceMs = ms;
}

void XModbusHub_setWriteStallNotice(XModbusHub* hub, int attempts)
{
    if (hub) hub->m_writeStallNoticeAt = attempts; /* <1=关闭提示；只影响提示不改重发。 */
}

void XModbusHub_flush(XModbusHub* hub)
{
    if (!hub) return;
    if (hub->m_flushTimer != XTIMER_INVALID_ID) {
        XObject_killTimer((XObject*)hub, hub->m_flushTimer);
        hub->m_flushTimer = XTIMER_INVALID_ID;
    }
    hubPumpWriteQueue(hub);
}

/** @brief 写受理后调度刷写：窗口 0=立即泵；否则首笔拉起窗口定时器。 */
static void hubScheduleFlush(XModbusHub* hub, int deviceId)
{
    if (hub->m_writeCoalesceMs == 0) {
        XModbusHubDevice* dev = hubDeviceById(hub, deviceId);
        if (dev) hubPumpDevice(hub, dev);
        return;
    }
    if (hub->m_flushTimer == XTIMER_INVALID_ID) {
        hub->m_flushTimer = XObject_startTimer_ms((XObject*)hub,
                                                  (int)hub->m_writeCoalesceMs,
                                                  XTimerType_CoarseTimer);
    }
}

bool XModbusHub_writeValue(XModbusHub* hub, XModbusRegisterType type,
                           uint16_t virtualAddr, uint16_t value)
{
    return XModbusHub_writeValues(hub, type, virtualAddr, 1, &value);
}

bool XModbusHub_writeValues(XModbusHub* hub, XModbusRegisterType type,
                            uint16_t virtualStart, uint16_t count,
                            const uint16_t* values)
{
    const int t = hubTypeIndex((int)type);
    int vi;
    XModbusHubCacheSlice* s;
    XModbusHubCacheBlock* block;
    uint16_t blockOffset;
    uint32_t end;
    bool changed = false;
    if (!hub || !values || count == 0) return false;
    if (type != XModbusCoils && type != XModbusHoldingRegisters) return false;
    if (t < 0 || !hub->m_view || !hub->m_view[t]) return false;
    vi = hubViewFind(hub->m_view[t], virtualStart);
    if (vi < 0) return false;
    s = (XModbusHubCacheSlice*)XVector_at_base(hub->m_view[t], (int64_t)vi);
    if (!s || !s->block) return false;
    block = s->block;
    blockOffset = (uint16_t)(s->blockOffset + (virtualStart - s->virtualStart));
    /* 整段必须落在同一块内且无空洞（块=远端连续段=写安全边界）。 */
    end = (uint32_t)virtualStart + count;
    {
        uint32_t covered = (uint32_t)virtualStart;
        for (size_t k = (size_t)vi; k < XVector_size_base(hub->m_view[t]); ++k) {
            const XModbusHubCacheSlice* s2 =
                (const XModbusHubCacheSlice*)XVector_at_base(hub->m_view[t], (int64_t)k);
            if (!s2 || s2->block != block) return false;
            if ((uint32_t)s2->virtualStart + s2->count <= covered) continue;
            if ((uint32_t)s2->virtualStart > covered) return false;   /* 洞：拒绝。 */
            covered = (uint32_t)s2->virtualStart + s2->count;
            if (covered >= end) break;
        }
        if (covered < end) return false;
    }
    if ((uint32_t)blockOffset + count > block->count) return false;

    /* ① 乐观写入块缓存（内存写，微秒级受理）+ 变化发射。 */
    for (uint16_t i = 0; i < count; ++i) {
        const uint16_t v = hubTypeIsBit(block->type) ? (values[i] ? 1u : 0u) : values[i];
        if (XModbusDataUnit_value(block->data, blockOffset + i) != v) {
            XModbusDataUnit_setValue(block->data, blockOffset + i, v);
            changed = true;
        }
    }
    if (!hub->m_changedOnlySignals || changed) {
        const uint32_t lo = (uint32_t)block->remoteStart + blockOffset;
        const uint32_t hi = lo + count;
        for (size_t k = (size_t)vi; k < XVector_size_base(hub->m_view[t]); ++k) {
            const XModbusHubCacheSlice* s2 =
                (const XModbusHubCacheSlice*)XVector_at_base(hub->m_view[t], (int64_t)k);
            uint32_t sStart, sEnd, cLo, cHi;
            if (!s2 || s2->block != block) break;
            sStart = (uint32_t)block->remoteStart + s2->blockOffset;
            sEnd = sStart + s2->count;
            cLo = (sStart > lo) ? sStart : lo;
            cHi = (sEnd < hi) ? sEnd : hi;
            if (cHi <= cLo) continue;
            XModbusHub_valueChanged_signal(hub, (int)block->type,
                                           (int)(s2->virtualStart + (cLo - sStart)),
                                           (int)(cHi - cLo));
        }
    }
    /* ② 标脏（合并窗口内多次写只留最终值）+ 调度刷写。 */
    hubMarkDirty(block, blockOffset, count);
    hub->m_lastWriteMs = XDateTime_currentMSecsSinceEpoch();
    hubScheduleFlush(hub, s->deviceId);
    return true;
}

int XModbusHub_pendingWriteCount(const XModbusHub* hub)
{
    int total = 0;
    if (!hub || !hub->m_devices) return 0;
    for (size_t i = 0; i < XVector_size_base(hub->m_devices); ++i) {
        const XModbusHubDevice* d =
            (const XModbusHubDevice*)XVector_at_base(hub->m_devices, (int64_t)i);
        total += hubDeviceDirtyCount(d);
    }
    return total;
}

/* ==================== 统计 ==================== */

uint32_t XModbusHub_pollOkCount(const XModbusHub* hub, int deviceId)
{
    XModbusHubDevice* dev = hubDeviceById((XModbusHub*)hub, deviceId);
    return dev ? dev->pollOkCount : 0;
}

uint32_t XModbusHub_pollErrCount(const XModbusHub* hub, int deviceId)
{
    XModbusHubDevice* dev = hubDeviceById((XModbusHub*)hub, deviceId);
    return dev ? dev->pollErrCount : 0;
}

uint32_t XModbusHub_reconnectCount(const XModbusHub* hub, int deviceId)
{
    XModbusHubDevice* dev = hubDeviceById((XModbusHub*)hub, deviceId);
    return dev ? dev->reconnectCount : 0;
}

uint64_t XModbusHub_deviceLastSuccessMs(const XModbusHub* hub, int deviceId)
{
    XModbusHubDevice* dev = hubDeviceById((XModbusHub*)hub, deviceId);
    return dev ? dev->lastSuccessMs : 0;
}

int XModbusHub_readGroupCount(const XModbusHub* hub, int deviceId)
{
    XModbusHubDevice* dev = hubDeviceById((XModbusHub*)hub, deviceId);
    if (!dev || !dev->groups) return 0;
    return (int)XVector_size_base(dev->groups);
}

/* ==================== 定时器（XObject 自带机制，规范 2026-10-26） ==================== */

static void VXModbusHub_timerEvent(XObject* obj, XTimerEvent* event)
{
    XModbusHub* hub = (XModbusHub*)obj;
    if (!hub || !event) goto parent_call;
    XEvent_accept(event);
    {
        const XTimerId id = XTimerEvent_timerId(event);
        if (id == hub->m_retryTimer) {
            hubPumpWriteQueue(hub);
            return;
        }
        if (id == hub->m_flushTimer) {
            /* 合并窗口到期：停表并统一刷写（写窗口语义=首笔起算一个窗口）。 */
            XObject_killTimer(obj, hub->m_flushTimer);
            hub->m_flushTimer = XTIMER_INVALID_ID;
            hubPumpWriteQueue(hub);
            return;
        }
        if (hub->m_devices) {
            const size_t n = XVector_size_base(hub->m_devices);
            for (size_t i = 0; i < n; ++i) {
                XModbusHubDevice* d =
                    (XModbusHubDevice*)XVector_at_base(hub->m_devices, (int64_t)i);
                if (d && d->pollTimer == id) {
                    hubPumpDevice(hub, d);
                    return;
                }
            }
        }
    }
parent_call:
    XClass_Parent(XObject, EXObject_TimerEvent,
                  void (*)(XObject*, XTimerEvent*))(obj, event);
}

/* ==================== 槽 ==================== */

/** @brief 设备状态变化（connect_2：第一参=真 sender=client）。 */
static void XModbusHub_onDeviceStateChanged(XObject* sender, XVarList* args)
{
    if (!sender || !args) return;
    XVarList_args_1(args, int, newStateInt);
    {
        XModbusHub* hub = (XModbusHub*)XObject_parent(sender);
        if (!hub || !XClassGetVtable(hub) ||
            XClassGetVtable(hub) != XModbusHub_class_init()) return;
        if (!hub->m_devices) return;
        for (size_t i = 0; i < XVector_size_base(hub->m_devices); ++i) {
            XModbusHubDevice* d =
                (XModbusHubDevice*)XVector_at_base(hub->m_devices, (int64_t)i);
            const XModbusDevice_State newState = (XModbusDevice_State)newStateInt;
            if (!d || (XObject*)d->client != sender) continue;
            {
                const XModbusDevice_State old = d->state;
                if (old == newState) return;
                d->state = newState;
            }
            XModbusHub_deviceStateChanged_signal(hub, d->id, newStateInt);
            if (newState == XModbusDevice_ConnectedState) {
                ++d->reconnectCount;
                /* 恢复在线：立即续泵轮询 + 补写滞留脏区间（不等下一拍）。 */
                hubPumpDevice(hub, d);
                hubPumpWriteQueue(hub);
                XModbusHub_deviceStatsChanged_signal(hub, d->id);
            }
            return;
        }
    }
}

/**
 * @brief 应答完成（connect_2：第一参=真 sender=reply；经 parent 反查 hub，
 *        按 inflight 指针归属设备；单飞收口后续泵）。
 */
static void XModbusHub_onReplyFinished(XObject* sender, XVarList* args)
{
    XModbusReply* reply = (XModbusReply*)sender;
    XModbusHub* hub;
    XModbusHubDevice* dev = NULL;
    (void)args;
    if (!reply) return;
    hub = (XModbusHub*)XObject_parent((XObject*)reply);
    if (!hub || !XClassGetVtable(hub) ||
        XClassGetVtable(hub) != XModbusHub_class_init()) {
        XObject_deleteLater((XObject*)reply);
        return;
    }
    if (hub->m_devices) {
        const size_t n = XVector_size_base(hub->m_devices);
        for (size_t i = 0; i < n; ++i) {
            XModbusHubDevice* d =
                (XModbusHubDevice*)XVector_at_base(hub->m_devices, (int64_t)i);
            if (d && d->inflight == reply) { dev = d; break; }
        }
    }
    if (!dev) {
        /* 非 Hub 在途登记的 Reply（如设备被移除前的遗留）：直接回收。 */
        XObject_deleteLater((XObject*)reply);
        return;
    }

    {
        const uint64_t now = XDateTime_currentMSecsSinceEpoch();
        const bool ok = (XModbusReply_error(reply) == XModbusDevice_NoError);

        if (dev->inflightKind == XModbusHubInflight_Poll) {
            /* [2026-10-28 单元退避] 组/从站定位提前到成败分支之前，结果先
             * 回报退避表（lost 跃迁沿发 unitLost/unitAlive）。 */
            const XModbusHubReadGroup group = dev->inflightGroup;
            const int unitId = group.unitId;
            const bool wasLost = (unitId >= 0) &&
                XPollScheduler_isLostKey(&dev->unitSched, unitId);
            const bool enteredMute = (unitId >= 0) &&
                XPollScheduler_reportResultKey(&dev->unitSched, unitId, ok, now);
            if (ok) {
                const XModbusDataUnit* result = XModbusReply_result_const(reply);
                if (result)
                    hubFillGroupResult(hub, dev, &group, result);
                ++dev->pollOkCount;
                dev->lastSuccessMs = now;
                if (wasLost)
                    XModbusHub_unitAlive_signal(hub, dev->id, unitId);
            } else {
                ++dev->pollErrCount; /* 不风暴：静默计数，下轮计划重试该组。 */
                if (enteredMute && !wasLost)
                    XModbusHub_unitLost_signal(hub, dev->id, unitId);
                if (group.firstBlock < XVector_size_base(dev->blocks)) {
                    XModbusHubCacheBlock* b =
                        *(XModbusHubCacheBlock**)XVector_at_base(dev->blocks,
                                                                 (int64_t)group.firstBlock);
                    if (b) ++b->failCount;
                }
            }
            XModbusHub_deviceStatsChanged_signal(hub, dev->id);
        } else if (dev->inflightKind == XModbusHubInflight_Write) {
            const XModbusHubInflightWrite snap = dev->inflightWrite;
            XModbusHubCacheBlock* b = hubFindBlockAt(dev, snap.type, snap.unitId,
                                                     snap.remoteStart);
            if (ok) {
                dev->lastSuccessMs = now;
                if (b) {
                    /* 逐点比较：缓存值 != 已发快照 ⇒ 在途期间又被改过，重新标脏。 */
                    const uint16_t off = (uint16_t)(snap.remoteStart - b->remoteStart);
                    uint16_t i = 0;
                    hubDirtySubtract(b, off, snap.count);
                    while (i < snap.count) {
                        uint16_t j;
                        bool differs;
                        if (!snap.values) break;
                        differs = (XModbusDataUnit_value(b->data, off + i) != snap.values[i]);
                        if (!differs) { ++i; continue; }
                        j = i;
                        while (j < snap.count && snap.values &&
                               XModbusDataUnit_value(b->data, off + j) != snap.values[j])
                            ++j;
                        {
                            XModbusHubDirtyRange r;
                            memset(&r, 0, sizeof(r));
                            r.offset = (uint16_t)(off + i);
                            r.count = (uint16_t)(j - i);
                            hubDirtyPushRaw(b, &r);
                        }
                        i = j;
                    }
                }
                hubEmitWriteDone(hub, snap.type, snap.virtualStart,
                                 snap.virtualCount, true);
            } else if (b) {
                /* 失败：脏区间原样保留，重发定时器周期重试；提示沿由发送端
                 * 按 attempts 阈值发过一次，这里不重复。 */
                (void)0;
            }
            dev->inflightWrite.values = NULL;   /* 快照内存随收口释放。 */
            if (snap.values) XFree_System(snap.values);
            memset(&dev->inflightWrite, 0, sizeof(dev->inflightWrite));
            XModbusHub_deviceStatsChanged_signal(hub, dev->id);
        }

        dev->inflight = NULL;
        dev->inflightKind = XModbusHubInflight_None;
        XObject_deleteLater((XObject*)reply);
        /* [2026-10-26 节拍根修] 收口只补写、不续轮询：若在此续泵，计划轮跑完
         * 回卷后立刻又发下一轮（实测 2ms/请求、tid 数千的轮询风暴，回填在
         * 风暴中概率性失败）。轮询节奏一律交还设备周期定时器——pollInterval
         * 语义=段发送节拍。写意图仍即时续泵（写延迟优先）。 */
        if (hubDeviceHasDirty(dev))
            hubPumpDevice(hub, dev);
    }
}

/* ==================== 信号 ==================== */

void* XModbusHub_deviceStateChanged_signal(XModbusHub* hub, int deviceId, int state)
{
    XEmitSignal(hub, XModbusHub_deviceStateChanged_signal,
        XVarList_Create(XVar(int, deviceId), XVar(int, state)), NULL,
        NULL, XEVENT_PRIORITY_NORMAL);
}

void* XModbusHub_valueChanged_signal(XModbusHub* hub, int type,
                                     int virtualStart, int count)
{
    XEmitSignal(hub, XModbusHub_valueChanged_signal,
        XVarList_Create(XVar(int, type), XVar(int, virtualStart), XVar(int, count)),
        NULL, NULL, XEVENT_PRIORITY_NORMAL);
}

void* XModbusHub_writeCompleted_signal(XModbusHub* hub, int type,
                                       int virtualStart, int count, bool ok)
{
    XEmitSignal(hub, XModbusHub_writeCompleted_signal,
        XVarList_Create(XVar(int, type), XVar(int, virtualStart), XVar(int, count),
                        XVar(bool, ok)), NULL,
        NULL, XEVENT_PRIORITY_NORMAL);
}

void* XModbusHub_deviceStatsChanged_signal(XModbusHub* hub, int deviceId)
{
    XEmitSignal(hub, XModbusHub_deviceStatsChanged_signal,
        XVarList_Create(XVar(int, deviceId)), NULL, NULL, XEVENT_PRIORITY_NORMAL);
}

void* XModbusHub_unitLost_signal(XModbusHub* hub, int deviceId, int unitId)
{
    XEmitSignal(hub, XModbusHub_unitLost_signal,
        XVarList_Create(XVar(int, deviceId), XVar(int, unitId)), NULL,
        NULL, XEVENT_PRIORITY_NORMAL);
}

void* XModbusHub_unitAlive_signal(XModbusHub* hub, int deviceId, int unitId)
{
    XEmitSignal(hub, XModbusHub_unitAlive_signal,
        XVarList_Create(XVar(int, deviceId), XVar(int, unitId)), NULL,
        NULL, XEVENT_PRIORITY_NORMAL);
}

void* XModbusHub_writeStalled_signal(XModbusHub* hub, int type,
                                     int virtualStart, int count, int attempts)
{
    XEmitSignal(hub, XModbusHub_writeStalled_signal,
        XVarList_Create(XVar(int, type), XVar(int, virtualStart), XVar(int, count),
                        XVar(int, attempts)), NULL,
        NULL, XEVENT_PRIORITY_NORMAL);
}

#endif /* XMODBUS_HUB_ON */
#endif /* XMODBUS_CLIENT_ON */
#endif /* XMODBUS_ON */
#endif /* XPROTOCOL_ON */
