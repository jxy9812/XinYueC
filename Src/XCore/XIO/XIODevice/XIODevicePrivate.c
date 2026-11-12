#include "XIODevicePrivate.h"
#include "XIODevice.h"
#include "XIODevice_Protected.h"
#include "XRingChunk.h"
#include "XRingBuffer.h"
#include "XVector.h"
#include "XMemory.h"
#include "XString.h"
#include <string.h>

#ifndef DEFAULT_CHUNK_SIZE
#define DEFAULT_CHUNK_SIZE 4096   /* ESP8266 合体固件以 -DDEFAULT_CHUNK_SIZE=1024 覆盖：
                                   * 4KB chunk 在 console init 后仅剩 ~2.6KB 堆时创建
                                   * 失败（真机 post-init 崩溃链一环），1024 头寸足够 */
#endif

/* 丢弃环形缓冲数据时的中转块大小（XRingBuffer 没有「跳过」原语）。 */
#define XIODEV_SKIP_CHUNK 512

// --- Helper Functions ---

/**
 * @brief 辅助函数：确保 XVector 在指定索引处有足够的容量，并用 NULL 填充空缺。
 * @param vec 要操作的 XVector。
 * @param index 目标索引。
 * @return 成功返回 true，失败返回 false。
 */
static bool ensureVectorCapacity(struct XVector* vec, int index) {
    size_t currentSize = XVector_size_base(vec);
    if ((size_t)index >= currentSize) {
        // 需要扩容
        if (!XVector_resize_base(vec, index + 1)) {
            return false;
        }
        // 将新分配的空间初始化为 NULL
        for (size_t i = currentSize; i <= (size_t)index; ++i) {
            //XRingBuffer* nullBuf = NULL;
            XVector_At_Base(vec, i, XRingBuffer*)=NULL;
        }
    }
    return true;
}

/**
 * @brief 辅助函数：获取或创建指定通道的缓冲区。
 * @param buffers 缓冲区池 (XVector<XRingBuffer*>*)。
 * @param channelId 通道ID。
 * @return 成功时返回 XRingBuffer*，失败时返回 NULL。
 */
static struct XRingBuffer* getOrCreateBuffer(struct XVector* buffers, int channelId) {
    if (channelId < 0) {
        return NULL;
    }

    if (!ensureVectorCapacity(buffers, channelId)) {
        return NULL;
    }

    XRingBuffer** existingBufPtr = (XRingBuffer**)XVector_at_base(buffers, channelId);
    if (existingBufPtr && *existingBufPtr) {
        // 缓冲区已存在
        return *existingBufPtr;
    }

    // 创建新缓冲区
    struct XRingBuffer* newBuf = XRingBuffer_create(DEFAULT_CHUNK_SIZE);
    if (!newBuf) {
        return NULL;
    }

    // 存入 vector
    //XVector_set_base(buffers, channelId, &newBuf);
    XVector_At_Base(buffers, channelId, XRingBuffer*) = newBuf;
    return newBuf;
}

/**
 * @brief 释放缓冲区池中 [count, size) 区间内的缓冲区（对齐 Qt 的 resize 缩容语义）。
 */
static void destroyBuffersFrom(struct XVector* buffers, size_t count) {
    size_t size;
    if (!buffers) return;
    size = XVector_size_base(buffers);
    for (size_t i = count; i < size; ++i) {
        XRingBuffer** bufPtr = (XRingBuffer**)XVector_at_base(buffers, i);
        if (bufPtr && *bufPtr) {
            XClassDelete(*bufPtr);
            *bufPtr = NULL;
        }
    }
    if (count < size)
        XVector_resize_base(buffers, count);
}

/**
 * @brief 当前通道的读缓冲环形缓冲（不存在则创建）。
 */
static struct XRingBuffer* readRing(const XIODevicePrivate* d, int channel) {
    if (!d || !d->readBuffers) return NULL;
    return getOrCreateBuffer(d->readBuffers, channel);
}

/**
 * @brief 只查不建的缓冲区查找（对齐 Qt `QIODevicePrivate::buffer` 的只读语义）。
 * @details `xioReadRingConst`：**const 查询路径专用**。修复前
 *          `XIODevicePrivate_bufferedBytes` 走 `readRing` → `getOrCreateBuffer`
 *          → `XRingBuffer_create(4096)`：一次 `bytesAvailable()` / `atEnd()` /
 *          `bytesToWrite()` 查询就给空设备分配一块 4KB chunk（嵌入式 ESP8266
 *          在 console init 后仅剩 ~2.6KB 堆时直接 malloc 失败）。Qt 的查询
 *          一律只读不分配。
 */
static struct XRingBuffer* xioRingConst(struct XVector* buffers, int channel) {
    XRingBuffer** bufPtr;
    if (!buffers || channel < 0) return NULL;
    if ((size_t)channel >= XVector_size_base(buffers)) return NULL;
    bufPtr = (XRingBuffer**)XVector_at_base(buffers, channel);
    return (bufPtr && *bufPtr) ? *bufPtr : NULL;
}

static struct XRingBuffer* xioReadRingConst(const XIODevicePrivate* d, int channel) {
    return d ? xioRingConst(d->readBuffers, channel) : NULL;
}

struct XRingBuffer* XIODevicePrivate_readBufferConst(const XIODevicePrivate* d, int channelId) {
    return xioReadRingConst(d, channelId);
}

struct XRingBuffer* XIODevicePrivate_writeBufferConst(const XIODevicePrivate* d, int channelId) {
    return d ? xioRingConst(d->writeBuffers, channelId) : NULL;
}

/**
 * @brief 从环形缓冲的指定偏移处预读（XRingBuffer 只支持从队首预览，故按需取临时块）。
 */
static int64_t ringPeekAt(struct XRingBuffer* ring, char* out, int64_t maxlen, int64_t offset) {
    int64_t avail, want, got, n;
    char* tmp;
    if (!ring || !out || maxlen <= 0 || offset < 0)
        return 0;
    avail = (int64_t)XRingBuffer_available(ring);
    if (offset >= avail)
        return 0;
    want = offset + maxlen;
    if (want > avail) want = avail;
    if (want <= 0) return 0;
    if (offset == 0)
        return (int64_t)XRingBuffer_peek(ring, out, (size_t)want);
    tmp = (char*)XMalloc_System((size_t)want);
    if (!tmp) return 0;
    got = (int64_t)XRingBuffer_peek(ring, tmp, (size_t)want);
    n = 0;
    if (got > offset) {
        n = got - offset;
        if (n > maxlen) n = maxlen;
        memcpy(out, tmp + offset, (size_t)n);
    }
    XFree_System(tmp);
    return n;
}

/**
 * @brief 从环形缓冲头部丢弃最多 maxlen 字节（XRingBuffer 无跳过原语，按块读出丢弃）。
 */
static int64_t ringSkip(struct XRingBuffer* ring, int64_t maxlen) {
    char tmp[XIODEV_SKIP_CHUNK];
    int64_t done = 0;
    if (!ring || maxlen <= 0) return 0;
    while (done < maxlen) {
        int64_t want = maxlen - done;
        if (want > XIODEV_SKIP_CHUNK) want = XIODEV_SKIP_CHUNK;
        size_t got = XRingBuffer_read(ring, tmp, (size_t)want);
        if (got == 0) break;
        done += (int64_t)got;
        if ((int64_t)got < want) break;
    }
    return done;
}

// --- Public API Implementation ---

void XIODevicePrivate_init(XIODevicePrivate* d, struct XIODevice* q)
{
    if (!d) return;
    memset(d, 0, sizeof(XIODevicePrivate));
    d->readBuffers = XVector_Create(struct XRingBuffer*);
    d->writeBuffers = XVector_Create(struct XRingBuffer*);
    d->q_ptr = q;
    d->pos = 0;
    d->devicePos = 0;
    d->transactionReadPos = 0;
    d->readChannelCount = 0;
    d->writeChannelCount = 0;
    d->ungetCount = 0;
    memset(d->ungetStack, 0, sizeof(d->ungetStack));
    d->baseReadLineDataCalled = false;
}

void XIODevicePrivate_deinit(XIODevicePrivate* d)
{
    if (!d) return;

    if (d->readBuffers) {
        for (size_t i = 0; i < XVector_size_base(d->readBuffers); ++i) {
            XRingBuffer** bufPtr = (XRingBuffer**)XVector_at_base(d->readBuffers, i);
            if (bufPtr && *bufPtr) {
                XClassDelete(*bufPtr);
            }
        }
        XClassDelete(d->readBuffers);
        d->readBuffers = NULL;
    }

    if (d->writeBuffers) {
        for (size_t i = 0; i < XVector_size_base(d->writeBuffers); ++i) {
            XRingBuffer** bufPtr = (XRingBuffer**)XVector_at_base(d->writeBuffers, i);
            if (bufPtr && *bufPtr) {
                XClassDelete(*bufPtr);
            }
        }
        XClassDelete(d->writeBuffers);
        d->writeBuffers = NULL;
    }

    if (d->errorString) {
        XClassDelete(d->errorString);
        d->errorString = NULL;
    }
}

XIODevicePrivate* XIODevicePrivate_create(XIODevice* q) {
    XIODevicePrivate* d = (XIODevicePrivate*)XMalloc_System(sizeof(XIODevicePrivate));
    if (!d) return NULL;
    XIODevicePrivate_init(d, q);
    if (!d->readBuffers || !d->writeBuffers) {
        XIODevicePrivate_deinit(d);
        XFree_System(d);
        return NULL;
    }
    return d;
}

void XIODevicePrivate_delete(XIODevicePrivate* d) {
    if (!d) return;
    XIODevicePrivate_deinit(d);
    XFree_System(d);
}

struct XRingBuffer* XIODevicePrivate_getOrCreateReadBuffer(XIODevicePrivate* d, int channelId) {
    if (!d || !d->readBuffers) return NULL;
    return getOrCreateBuffer(d->readBuffers, channelId);
}

struct XRingBuffer* XIODevicePrivate_getOrCreateWriteBuffer(XIODevicePrivate* d, int channelId) {
    if (!d || !d->writeBuffers) return NULL;
    return getOrCreateBuffer(d->writeBuffers, channelId);
}

// --- 通道计数 ---

void XIODevicePrivate_setReadChannelCount(XIODevicePrivate* d, int count)
{
    if (!d) return;
    if (count < 0) count = 0;
    if (d->readBuffers && (size_t)count < XVector_size_base(d->readBuffers))
        destroyBuffersFrom(d->readBuffers, (size_t)count);
    d->readChannelCount = count;
}

void XIODevicePrivate_setWriteChannelCount(XIODevicePrivate* d, int count)
{
    if (!d) return;
    if (count < 0) count = 0;
    if (d->writeBuffers && (size_t)count < XVector_size_base(d->writeBuffers))
        destroyBuffersFrom(d->writeBuffers, (size_t)count);
    d->writeChannelCount = count;
}

void XIODevicePrivate_clearReadBuffers(XIODevicePrivate* d)
{
    size_t size;
    if (!d) return;
    d->ungetCount = 0;
    if (!d->readBuffers) return;
    size = XVector_size_base(d->readBuffers);
    for (size_t i = 0; i < size; ++i) {
        XRingBuffer** bufPtr = (XRingBuffer**)XVector_at_base(d->readBuffers, i);
        if (bufPtr && *bufPtr)
            XRingBuffer_reset(*bufPtr);
    }
}

// --- 读缓冲统一视图（unget 栈 + 环形缓冲） ---

int64_t XIODevicePrivate_bufferedBytes(const XIODevicePrivate* d, int channel)
{
    struct XRingBuffer* ring;
    int64_t bytes;
    if (!d) return 0;
    bytes = (int64_t)d->ungetCount;
    /* 查询路径只读不建（见 xioReadRingConst 的说明）。 */
    ring = xioReadRingConst(d, channel);
    if (ring)
        bytes += (int64_t)XRingBuffer_available(ring);
    return bytes;
}

int64_t XIODevicePrivate_bufferPeek(const XIODevicePrivate* d, int channel, char* data,
                                   int64_t maxlen, int64_t offset)
{
    struct XRingBuffer* ring;
    int64_t n = 0;
    int64_t off;
    if (!d || !data || maxlen <= 0 || offset < 0)
        return 0;
    off = offset;
    /* 段一：unget 栈本身就是逻辑流的最前端（后放回的先读出）。 */
    if (off < (int64_t)d->ungetCount) {
        int64_t segLen = (int64_t)d->ungetCount - off;
        int64_t take = (segLen < maxlen) ? segLen : maxlen;
        for (int64_t i = 0; i < take; ++i)
            data[i] = d->ungetStack[d->ungetCount - 1 - (int)(off + i)];
        n = take;
        off = 0;
        if (n >= maxlen)
            return n;
    } else {
        off -= (int64_t)d->ungetCount;
    }
    /* 段二：当前通道的环形缓冲（预读是查询路径：不存在就不建）。 */
    ring = xioReadRingConst(d, channel);
    if (ring)
        n += ringPeekAt(ring, data + n, maxlen - n, off);
    return n;
}

int64_t XIODevicePrivate_bufferRead(XIODevicePrivate* d, int channel, char* data, int64_t maxlen)
{
    struct XRingBuffer* ring;
    int64_t n = 0;
    if (!d || !data || maxlen <= 0)
        return 0;
    while (n < maxlen && d->ungetCount > 0)
        data[n++] = d->ungetStack[--d->ungetCount];
    if (n >= maxlen)
        return n;
    ring = readRing(d, channel);
    if (ring)
        n += (int64_t)XRingBuffer_read(ring, data + n, (size_t)(maxlen - n));
    return n;
}

int64_t XIODevicePrivate_bufferIndexOf(const XIODevicePrivate* d, int channel, char c,
                                       int64_t maxlen, int64_t offset)
{
    char tmp[256];
    int64_t pos;
    int64_t limit;
    if (!d || maxlen <= 0 || offset < 0)
        return -1;
    pos = offset;
    limit = offset + maxlen;
    while (pos < limit) {
        int64_t want = limit - pos;
        int64_t got;
        if (want > (int64_t)sizeof(tmp)) want = (int64_t)sizeof(tmp);
        got = XIODevicePrivate_bufferPeek(d, channel, tmp, want, pos);
        if (got <= 0) break;
        for (int64_t i = 0; i < got; ++i) {
            if (tmp[i] == c)
                return pos + i;
        }
        pos += got;
    }
    return -1;
}

int64_t XIODevicePrivate_bufferSkip(XIODevicePrivate* d, int channel, int64_t maxlen)
{
    struct XRingBuffer* ring;
    int64_t done = 0;
    if (!d || maxlen <= 0)
        return 0;
    while (done < maxlen && d->ungetCount > 0) {
        --d->ungetCount;
        ++done;
    }
    if (done >= maxlen)
        return done;
    ring = readRing(d, channel);
    if (ring)
        done += ringSkip(ring, maxlen - done);
    return done;
}

bool XIODevicePrivate_isBufferEmpty(const XIODevicePrivate* d, int channel)
{
    int64_t bytes;
    if (!d) return true;
    bytes = XIODevicePrivate_bufferedBytes(d, channel);
    if (bytes == 0) return true;
    /* 顺序设备在事务中：事务偏移之前的数据视为「已消费」。 */
    if (d->transactionStarted && d->q_ptr && XIODevice_isSequential_base(d->q_ptr)
        && d->transactionReadPos >= bytes)
        return true;
    return false;
}

void XIODevicePrivate_seekBuffer(XIODevicePrivate* d, int channel, int64_t newPos)
{
    int64_t offset;
    if (!d) return;
    offset = newPos - d->pos;
    d->pos = newPos;
    if (offset < 0 || offset >= XIODevicePrivate_bufferedBytes(d, channel)) {
        /* 回退或越界：预读内容全部作废，下次读重新向设备取。 */
        XIODevicePrivate_clearReadBuffers(d);
    } else {
        /* 前移仍在预读范围内：只需丢掉前缀。 */
        XIODevicePrivate_bufferSkip(d, channel, offset);
    }
}

// --- 单通道兼容层 ---

void XIODevicePrivate_ungetChar(XIODevicePrivate* d, char c)
{
    struct XRingBuffer* ring;
    XRingChunk* chunk;
    if (!d) return;
    if (d->ungetCount < XIODevice_UngetCapacity) {
        d->ungetStack[d->ungetCount++] = c;
        return;
    }
    /* 栈满：退回环形缓冲前端（要求队首 chunk 前一字节刚被消费过）。 */
    ring = readRing(d, d->q_ptr ? d->q_ptr->m_currentReadChannel : 0);
    if (ring && ring->m_currentReadChunk < XVector_size_base(ring->m_chunks)) {
        XRingChunk** chunkPtr = (XRingChunk**)XVector_at_base(ring->m_chunks, ring->m_currentReadChunk);
        if (chunkPtr && *chunkPtr)
            chunk = *chunkPtr;
        else
            chunk = NULL;
        if (chunk && XRingChunk_unget(chunk, &c, 1) == 1) {
            XContainerSize(ring) += 1;
            if (d->q_ptr && !XIODevice_isSequential_base(d->q_ptr))
                --d->pos;
            return;
        }
    }
    /* 无处可放：按 Qt 契约「放回的字符必须是刚读出的」，此时丢弃并记录。 */
}

int64_t XIODevicePrivate_readLineFromBuffer(XIODevicePrivate* d, char* data, int64_t maxlen) {
    int channel;
    int64_t avail, limit, idx;
    if (!d || !data || maxlen <= 0) return 0;
    channel = d->q_ptr ? d->q_ptr->m_currentReadChannel : 0;
    avail = XIODevicePrivate_bufferedBytes(d, channel);
    if (avail == 0) return 0;
    limit = avail;
    if (limit > maxlen) limit = maxlen;
    idx = XIODevicePrivate_bufferIndexOf(d, channel, '\n', limit, 0);
    if (idx < 0) return 0;
    return XIODevicePrivate_bufferRead(d, channel, data, idx + 1);
}

bool XIODevicePrivate_canReadLineFromBuffer(const XIODevicePrivate* d) {
    int channel;
    int64_t avail, offset = 0;
    if (!d) return false;
    channel = d->q_ptr ? d->q_ptr->m_currentReadChannel : 0;
    avail = XIODevicePrivate_bufferedBytes(d, channel);
    if (avail == 0) return false;
    /* 对齐 Qt：顺序设备在事务中从事务偏移处开始找。 */
    if (d->transactionStarted && d->q_ptr && XIODevice_isSequential_base(d->q_ptr))
        offset = d->transactionReadPos;
    if (offset >= avail) return false;
    return XIODevicePrivate_bufferIndexOf(d, channel, '\n', avail - offset, offset) >= 0;
}

void XIODevicePrivate_startTransaction(XIODevicePrivate* d) {
    if (!d) return;
    /* 对齐 Qt：已有事务时只告警、不改状态（不覆盖原点）。 */
    if (d->transactionStarted) return;
    d->transactionReadPos = (d->q_ptr && XIODevice_isSequential_base(d->q_ptr)) ? 0 : d->pos;
    d->transactionStarted = true;
}

void XIODevicePrivate_commitTransaction(XIODevicePrivate* d) {
    int channel;
    if (!d) return;
    if (!d->transactionStarted) return;  /* 对齐 Qt：无事务时只告警 */
    channel = d->q_ptr ? d->q_ptr->m_currentReadChannel : 0;
    if (d->q_ptr && XIODevice_isSequential_base(d->q_ptr))
        XIODevicePrivate_bufferSkip(d, channel, d->transactionReadPos);
    d->transactionStarted = false;
    d->transactionReadPos = 0;
}

void XIODevicePrivate_rollbackTransaction(XIODevicePrivate* d) {
    int channel;
    if (!d) return;
    if (!d->transactionStarted) return;  /* 对齐 Qt：无事务时只告警 */
    channel = d->q_ptr ? d->q_ptr->m_currentReadChannel : 0;
    if (!(d->q_ptr && XIODevice_isSequential_base(d->q_ptr))) {
        /* 非顺序设备：回滚必须**把子类引擎也拉回事务原点**。
         * Qt 原文只调 `d->seekBuffer(d->transactionPos)`（qiodevice.cpp:1617-1618），
         * 因为 Qt 的 `pos()` 恒以 `d->pos` 为准，下一次 read 会因
         * `pos != devicePos` 触发 `seek(pos)` 把引擎拉回。本框架在基类读入口
         * 额外有「子类重载了 Pos 就按它校准 d->pos」的分支（xioRead 开头）——
         * XFileDevice 的 Pos 返回「描述符位置 − 未消费缓冲」，只 seekBuffer
         * 时引擎仍在回滚点之后 ⇒ 下一次读一进 xioRead 就把 d->pos 校准回
         * 读后位置，**回滚被静默撤销**，继续读到回滚点之后的数据。
         * 故这里显式走一次虚 seek（引擎 + d->pos + devicePos 三者一致）；
         * 失败（未 open / 顺序判定不一致等边界）再回落 Qt 的 seekBuffer。 */
        if (!XIODevice_seek_base(d->q_ptr, d->transactionReadPos))
            XIODevicePrivate_seekBuffer(d, channel, d->transactionReadPos);
    }
    d->transactionStarted = false;
    d->transactionReadPos = 0;
}
