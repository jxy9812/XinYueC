/**
 * @file       XGuiRemoteLoopback.c
 * @brief      XGuiRemoteLoopbackDevice 内存回环设备对实现。
 * @details    契约见冻结头 XGuiRemoteLoopback.h, 语义见 XGuiRemote.md §4.5。
 *             实现要点:
 *               - 虚表扩展照 XFileDevice 模式(XFileDevice.c:418-443):
 *                 XVTABLE_INIT_DEFAULT + XVTABLE_INHERIT_XCLASS(XIODevice) +
 *                 覆写 ReadData/WriteData/IsSequential/BytesAvailable/Close;
 *               - 共享对偶块(两环形缓冲 + 互斥锁 + 双端存活标志)由对偶
 *                 引用计数持有: 任一端 delete/close 原子清本端存活标志,
 *                 末删者释放共享块——对端对象不悬垂, peerAlive 经共享块
 *                 标志判定, 本端不持有对端对象指针;
 *               - 全有全无写: 对端接收环剩余空间不足整块时返回 0;
 *               - 线程约定: 每端的读写/delete 限本端属主线程, 跨端并发
 *                 仅经由共享块内的锁与存活标志。
 * @author     XinYueC 团队
 */
#include "XGuiRemoteLoopback.h"
#if XGUI_REMOTE_ON

#include "XMutex.h"
#include "XMemory.h"
#include <string.h>

/* ==================== 内部数据结构 ==================== */

/** @brief 固定容量环形缓冲(不扩容; 柔性数组与控制块一次分配)。 */
typedef struct XGuiRemoteLoopbackRing {
    size_t  cap;    /**< 总容量(字节, 创建时固定)。 */
    size_t  head;   /**< 读位置(相对 data 的偏移)。 */
    size_t  count;  /**< 已占用字节数。 */
    uint8_t data[]; /**< 环形存储区。 */
} LoopbackRing;

/**
 * @brief 共享对偶块(两端各持一个引用; 引用计数归零时释放)。
 * @note  owner[i] 仅作"设备→槽位"身份比较, 任何路径都不解引用, 且在
 *        锁内随 detach 清空——不构成对对端对象的存活依赖(peerAlive
 *        冻结语义: 只读 alive[] 标志)。
 */
typedef struct XGuiRemoteLoopbackShared {
    XMutex*  lock;      /**< 两端共享互斥锁(跨端并发唯一同步点)。 */
    int      refCount;  /**< 对偶引用计数(锁内读写)。 */
    bool     alive[2];  /**< 双端存活标志(锁内读写; false=已关/已删)。 */
    LoopbackRing* ring[2]; /**< ring[i] = 端 i 的接收环(端 i 写者=对端)。 */
    void*    owner[2];  /**< 端 i 绑定的设备对象(仅身份比较)。 */
    XMemoryType memType; /**< 共享块与环形缓冲的内存类型。 */
} LoopbackShared;

/* ==================== 环形缓冲 ==================== */

static LoopbackRing* loopbackRingCreate(size_t cap, XMemoryType memType)
{
    LoopbackRing* r =
        (LoopbackRing*)XMemory_malloc(sizeof(LoopbackRing) + cap, memType);
    if (!r) return NULL;
    r->cap = cap;
    r->head = 0;
    r->count = 0;
    return r;
}

static void loopbackRingFree(LoopbackRing* r, XMemoryType memType)
{
    if (r) XMemory_free(r, memType);
}

static void loopbackRingClear(LoopbackRing* r)
{
    if (r) {
        r->head = 0;
        r->count = 0;
    }
}

/* 拷入 n 字节(调用方保证 n ≤ cap - count)。 */
static void loopbackRingWrite(LoopbackRing* r, const uint8_t* src, size_t n)
{
    size_t pos = (r->head + r->count) % r->cap;
    size_t tail = r->cap - pos;
    size_t first = n < tail ? n : tail;
    memcpy(r->data + pos, src, first);
    if (n > first) memcpy(r->data, src + first, n - first);
    r->count += n;
}

/* 拷出至多 n 字节, 返回实际拷出数。 */
static size_t loopbackRingRead(LoopbackRing* r, uint8_t* dst, size_t n)
{
    if (n > r->count) n = r->count;
    size_t tail = r->cap - r->head;
    size_t first = n < tail ? n : tail;
    memcpy(dst, r->data + r->head, first);
    if (n > first) memcpy(dst + first, r->data, n - first);
    r->head = (r->head + n) % r->cap;
    r->count -= n;
    return n;
}

/* ==================== 共享对偶块 ==================== */

static int loopbackSlotOf(const LoopbackShared* shared, const void* dev)
{
    if (shared->owner[0] == dev) return 0;
    if (shared->owner[1] == dev) return 1;
    return -1;
}

static LoopbackShared* loopbackSharedCreate(XMemoryType memType)
{
    LoopbackShared* shared =
        (LoopbackShared*)XMemory_malloc(sizeof(LoopbackShared), memType);
    if (!shared) return NULL;
    memset(shared, 0, sizeof(*shared));
    shared->lock = XMutex_create(XLock_NonRecursive);
    if (!shared->lock) {
        XMemory_free(shared, memType);
        return NULL;
    }
    shared->memType = memType;
    shared->alive[0] = false;
    shared->alive[1] = false;
    return shared;
}

static void loopbackSharedFree(LoopbackShared* shared)
{
    if (!shared) return;
    if (shared->lock) {
        XMutex_delete(shared->lock);
        shared->lock = NULL;
    }
    XMemory_free(shared, shared->memType);
}

/**
 * @brief 本端解绑: 锁内清存活标志、释放本端接收环、减引用;
 *        末删者释放共享块。幂等(m_d 置 NULL 后再调为空操作)。
 */
static void loopbackDetach(XGuiRemoteLoopbackDevice* self)
{
    LoopbackShared* shared = (LoopbackShared*)self->m_d;
    if (!shared) return;
    XMutex_lock(shared->lock);
    int i = loopbackSlotOf(shared, self);
    if (i >= 0) {
        shared->alive[i] = false;
        if (shared->ring[i]) {
            loopbackRingFree(shared->ring[i], shared->memType);
            shared->ring[i] = NULL;
        }
        shared->owner[i] = NULL;
    }
    shared->refCount--;
    bool last = (shared->refCount <= 0);
    XMutex_unlock(shared->lock);

    self->m_d = NULL;
    if (last) loopbackSharedFree(shared);
}

/** @brief 关机语义: 清空本端接收环并清本端存活标志(对端随之 EOF/写 0)。 */
static void loopbackShutdown(XGuiRemoteLoopbackDevice* self)
{
    LoopbackShared* shared = (LoopbackShared*)self->m_d;
    if (!shared) return;
    XMutex_lock(shared->lock);
    int i = loopbackSlotOf(shared, self);
    if (i >= 0) {
        loopbackRingClear(shared->ring[i]);
        shared->alive[i] = false;
    }
    XMutex_unlock(shared->lock);
}

/* ==================== 虚函数实现 ==================== */

/** @brief 虚函数: 析构——解绑共享块后走基类 deinit(close/私有块/fd)。 */
static void VLoopback_deinit(XGuiRemoteLoopbackDevice* self)
{
    if (!self) return;
    loopbackDetach(self);
    XClass_Deinit_Parent(XIODevice, (XIODevice*)self);
}

/** @brief 虚函数: 顺序设备(恒 true)。 */
static bool VLoopback_isSequential(const XIODevice* io)
{
    (void)io;
    return true;
}

/** @brief 虚函数: 从本端接收环拷出; 对端不存活=EOF(返回 0)。 */
static int64_t VLoopback_readData(XIODevice* io, char* data, int64_t maxlen)
{
    XGuiRemoteLoopbackDevice* self = (XGuiRemoteLoopbackDevice*)io;
    if (!self || !data || maxlen <= 0) return -1;
    LoopbackShared* shared = (LoopbackShared*)self->m_d;
    if (!shared) return -1;

    XMutex_lock(shared->lock);
    int i = loopbackSlotOf(shared, self);
    if (i < 0) {
        XMutex_unlock(shared->lock);
        return -1;
    }
    if (!shared->alive[i ^ 1]) { /* 对端已关: 读 EOF */
        XMutex_unlock(shared->lock);
        return 0;
    }
    size_t n = loopbackRingRead(shared->ring[i], (uint8_t*)data,
                                (size_t)maxlen);
    XMutex_unlock(shared->lock);
    return (int64_t)n;
}

/**
 * @brief 虚函数: 本端写入=入对端接收环。全有全无——对端环剩余空间
 *        不足整块时返回 0(一个字节都不写); 对端不存活/本端已关=恒 0。
 */
static int64_t VLoopback_writeData(XIODevice* io, const char* data, int64_t len)
{
    XGuiRemoteLoopbackDevice* self = (XGuiRemoteLoopbackDevice*)io;
    if (!self || !data || len <= 0) return 0;
    LoopbackShared* shared = (LoopbackShared*)self->m_d;
    if (!shared) return -1;

    XMutex_lock(shared->lock);
    int i = loopbackSlotOf(shared, self);
    if (i < 0) {
        XMutex_unlock(shared->lock);
        return -1;
    }
    if (!shared->alive[i] || !shared->alive[i ^ 1] || !shared->ring[i ^ 1]) {
        XMutex_unlock(shared->lock);
        return 0; /* 本端已关或对端不存活: 写恒 0 */
    }
    LoopbackRing* peerRing = shared->ring[i ^ 1];
    size_t want = (size_t)len;
    if (peerRing->cap - peerRing->count < want) {
        XMutex_unlock(shared->lock);
        return 0; /* 全有全无: 空间不足整块, 一个字节都不写 */
    }
    loopbackRingWrite(peerRing, (const uint8_t*)data, want);
    XMutex_unlock(shared->lock);
    return len;
}

/** @brief 虚函数: 本端接收环占用; 对端不存活=0(EOF 视图)。 */
static int64_t VLoopback_bytesAvailable(const XIODevice* io)
{
    XGuiRemoteLoopbackDevice* self = (XGuiRemoteLoopbackDevice*)io;
    if (!self) return 0;
    LoopbackShared* shared = (LoopbackShared*)self->m_d;
    if (!shared) return 0;

    XMutex_lock(shared->lock);
    int i = loopbackSlotOf(shared, self);
    int64_t n = 0;
    if (i >= 0 && shared->alive[i ^ 1] && shared->ring[i]) {
        n = (int64_t)shared->ring[i]->count;
    }
    XMutex_unlock(shared->lock);
    return n;
}

/** @brief 虚函数: 关闭本端(清空缓冲+置对端 EOF)后走基类 close。 */
static void VLoopback_close(XIODevice* io)
{
    XGuiRemoteLoopbackDevice* self = (XGuiRemoteLoopbackDevice*)io;
    if (!self) return;
    loopbackShutdown(self);
    XClass_Parent(XIODevice, EXIODevice_Close, void (*)(XIODevice*))(io);
}

/* ==================== 类虚函数表 ==================== */

XVtable* XGuiRemoteLoopbackDevice_class_init(void)
{
    XVTABLE_INIT_DEFAULT(XGuiRemoteLoopbackDevice)
    XVTABLE_INHERIT_XCLASS(XIODevice);

    /* 覆写 XIODevice 槽位(照 XFileDevice 模式) */
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Deinit, VLoopback_deinit);
    XVTABLE_OVERLOAD_DEFAULT(EXIODevice_Close, VLoopback_close);
    XVTABLE_OVERLOAD_DEFAULT(EXIODevice_IsSequential, VLoopback_isSequential);
    XVTABLE_OVERLOAD_DEFAULT(EXIODevice_BytesAvailable, VLoopback_bytesAvailable);
    XVTABLE_OVERLOAD_DEFAULT(EXIODevice_ReadData, VLoopback_readData);
    XVTABLE_OVERLOAD_DEFAULT(EXIODevice_WriteData, VLoopback_writeData);

    XCLASS_SHOW_SIZE_DEFAULT(XGuiRemoteLoopbackDevice);
    return XVTABLE_DEFAULT;
}

/* ==================== 构造与析构 ==================== */

void XGuiRemoteLoopbackDevice_init(XGuiRemoteLoopbackDevice* self,
                                   XGuiRemoteLoopbackDevice* peer,
                                   size_t ringCapacity)
{
    if (!self) return;
    if (self->m_d) return; /* 已绑定: 幂等保护 */

    /* 基类初始化(内部会把 vtable 设为 XIODevice, 随后覆写为本类)。 */
    XIODevice_init(&self->m_parent);
    self->m_d = NULL;
    XClassSetVtable(self, XGuiRemoteLoopbackDevice);

    XMemoryType memType = XCLASS_DEFAULT_MEMORY_TYPE;
    LoopbackShared* shared = NULL;

    if (peer && peer != self && peer->m_d) {
        /* 加入对端已持有的共享对偶块(占对端的空闲对侧槽位)。 */
        shared = (LoopbackShared*)peer->m_d;
        XMutex_lock(shared->lock);
        int j = loopbackSlotOf(shared, peer);
        int i = (j >= 0) ? (j ^ 1) : -1;
        if (i >= 0 && !shared->owner[i] && !shared->alive[i]) {
            LoopbackRing* ring = loopbackRingCreate(ringCapacity, memType);
            if (ring) {
                shared->ring[i] = ring;
                shared->owner[i] = self;
                shared->alive[i] = true;
                shared->refCount++;
                self->m_d = shared;
            }
        }
        XMutex_unlock(shared->lock);
    } else {
        /* 新建共享对偶块。对侧槽位保持未绑定(存活=false):
         * 单端语义=写恒背压 0、读恒 EOF; 对端稍后经 init 加入。 */
        shared = loopbackSharedCreate(memType);
        if (shared) {
            LoopbackRing* ring = loopbackRingCreate(ringCapacity, memType);
            if (ring) {
                shared->ring[0] = ring;
                shared->owner[0] = self;
                shared->alive[0] = true;
                shared->refCount = 1;
                self->m_d = shared;
            } else {
                loopbackSharedFree(shared);
            }
        }
    }

    /* 读写双开(冻结语义: 顺序设备, 读写双开)。 */
    XIODevice_open_base(&self->m_parent, XIODevice_ReadWrite);
}

XGuiRemoteLoopbackDevice* XGuiRemoteLoopbackDevice_createPair_ex(
        XMemoryType memory, size_t ringCapacity,
        XGuiRemoteLoopbackDevice** peerOut)
{
    if (peerOut) *peerOut = NULL;

    XGuiRemoteLoopbackDevice* a =
        (XGuiRemoteLoopbackDevice*)XMemory_malloc(
            sizeof(XGuiRemoteLoopbackDevice), memory);
    if (!a) return NULL;
    memset(a, 0, sizeof(*a));

    XGuiRemoteLoopbackDevice* b = NULL;
    if (peerOut) {
        b = (XGuiRemoteLoopbackDevice*)XMemory_malloc(
            sizeof(XGuiRemoteLoopbackDevice), memory);
        if (!b) {
            XMemory_free(a, memory);
            return NULL;
        }
        memset(b, 0, sizeof(*b));
    }

    /* 端 A 建块, 端 B 加入(对端指针未初始化时 A 先落槽 0)。 */
    XGuiRemoteLoopbackDevice_init(a, b, ringCapacity);
    if (!a->m_d) {
        /* 绑定失败(分配失败): a 已完成对象初始化, 走正规析构。 */
        XClassDeinit((XClass*)a);
        XMemory_free(a, memory);
        if (b) XMemory_free(b, memory); /* b 未初始化, 直接释放 */
        return NULL;
    }
    if (b) {
        XGuiRemoteLoopbackDevice_init(b, a, ringCapacity);
        if (!b->m_d) {
            XClassDeinit((XClass*)a);
            XMemory_free(a, memory);
            XClassDeinit((XClass*)b);
            XMemory_free(b, memory);
            return NULL;
        }
    }

    Set_Class_Memory(a, memory); Set_Class_IsHeap(a, true);
    if (b) { Set_Class_Memory(b, memory); Set_Class_IsHeap(b, true); }

    if (peerOut) *peerOut = b;
    return a;
}

/* ==================== 访问与语义 ==================== */

bool XGuiRemoteLoopbackDevice_peerAlive(const XGuiRemoteLoopbackDevice* self)
{
    if (!self) return false;
    LoopbackShared* shared = (LoopbackShared*)self->m_d;
    if (!shared) return false;

    XMutex_lock(shared->lock);
    int i = loopbackSlotOf(shared, self);
    bool alive = (i >= 0) && shared->alive[i ^ 1];
    XMutex_unlock(shared->lock);
    return alive;
}

size_t XGuiRemoteLoopbackDevice_bufferedBytes(
        const XGuiRemoteLoopbackDevice* self)
{
    if (!self) return 0;
    /* 等价 XIODevice_bytesAvailable 视图(对端不存活=0)。 */
    return (size_t)VLoopback_bytesAvailable((const XIODevice*)self);
}

void XGuiRemoteLoopbackDevice_shutdown(XGuiRemoteLoopbackDevice* self)
{
    if (!self) return;
    loopbackShutdown(self); /* 幂等 */
}

#endif /* XGUI_REMOTE_ON */
