#include "XIODevicePrivate.h"
#include "XIODevice.h"
#include "XRingChunk.h"
#include "XVector.h"
#include "XMemory.h"
#include <string.h>

#define DEFAULT_CHUNK_SIZE 4096

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

// --- Public API Implementation ---

XIODevicePrivate* XIODevicePrivate_create(XIODevice* q) {
    XIODevicePrivate* d = (XIODevicePrivate*)XMalloc_System(sizeof(XIODevicePrivate));
    if (!d) return NULL;
    memset(d, 0, sizeof(XIODevicePrivate));

    d->readBuffers = XVector_Create(struct XRingBuffer*);
    d->writeBuffers = XVector_Create(struct XRingBuffer*);
    d->errorString = NULL;
    d->transactionStarted = false;
    d->aboutToCloseEmitted = false;
    d->q_ptr = q;

    if (!d->readBuffers || !d->writeBuffers) {
        XIODevicePrivate_delete(d);
        return NULL;
    }

    return d;
}

void XIODevicePrivate_delete(XIODevicePrivate* d) {
    if (!d) return;

    // 销毁所有读缓冲区
    if (d->readBuffers) {
        for (size_t i = 0; i < XVector_size_base(d->readBuffers); ++i) {
            XRingBuffer** bufPtr = (XRingBuffer**)XVector_at_base(d->readBuffers, i);
            if (bufPtr && *bufPtr) {
                XRingBuffer_delete_base(*bufPtr);
            }
        }
        XVector_delete_base(d->readBuffers);
    }

    // 销毁所有写缓冲区
    if (d->writeBuffers) {
        for (size_t i = 0; i < XVector_size_base(d->writeBuffers); ++i) {
            XRingBuffer** bufPtr = (XRingBuffer**)XVector_at_base(d->writeBuffers, i);
            if (bufPtr && *bufPtr) {
                XRingBuffer_delete_base(*bufPtr);
            }
        }
        XVector_delete_base(d->writeBuffers);
    }

    if (d->errorString) XString_delete_base(d->errorString);
    XFree_System(d);
}

struct XRingBuffer* XIODevicePrivate_getOrCreateReadBuffer(XIODevicePrivate* d, int channelId) {
    return getOrCreateBuffer(d->readBuffers, channelId);
}

struct XRingBuffer* XIODevicePrivate_getOrCreateWriteBuffer(XIODevicePrivate* d, int channelId) {
    return getOrCreateBuffer(d->writeBuffers, channelId);
}

// --- Single-Channel Compatibility Layer ---

void XIODevicePrivate_putChar(XIODevicePrivate* d, char c) {
    struct XRingBuffer* defaultReadBuf = XIODevicePrivate_getOrCreateReadBuffer(d, d->q_ptr->m_currentReadChannel);
    if (defaultReadBuf) {
        if (defaultReadBuf->m_currentReadChunk < XVector_size_base(defaultReadBuf->m_chunks)) {
            XRingChunk** chunkPtr = (XRingChunk**)XVector_at_base(defaultReadBuf->m_chunks, defaultReadBuf->m_currentReadChunk);
            if (chunkPtr && *chunkPtr) {
                XRingChunk_unget(*chunkPtr, &c, 1);
                //defaultReadBuf->m_totalSize += 1;
                XContainerSize(defaultReadBuf) += 1;
            }
        }
    }
}

bool XIODevicePrivate_getChar(XIODevicePrivate* d, char* c) {
    if (!c) return false;
    struct XRingBuffer* defaultReadBuf = XIODevicePrivate_getOrCreateReadBuffer(d, d->q_ptr->m_currentReadChannel);
    if (defaultReadBuf && XRingBuffer_available(defaultReadBuf) > 0) {
        return XRingBuffer_read(defaultReadBuf, c, 1) == 1;
    }
    return false;
}

int64_t XIODevicePrivate_peek(XIODevicePrivate* d, char* data, int64_t maxlen, XIODevice* device) {
    if (!data || maxlen <= 0) return 0;
    struct XRingBuffer* defaultReadBuf = XIODevicePrivate_getOrCreateReadBuffer(d, d->q_ptr->m_currentReadChannel);

    int64_t available = XRingBuffer_available(defaultReadBuf);
    if (available < maxlen) {
        int64_t toRead = maxlen - available;
        char* temp = (char*)XMalloc_System(toRead);
        if (temp) {
            int64_t n = XIODevice_read_1(device, temp, toRead);
            if (n > 0) {
                XRingBuffer_write(defaultReadBuf, temp, n);
            }
            XFree_System(temp);
        }
    }

    int64_t peekLen = XRingBuffer_available(defaultReadBuf);
    if (peekLen > maxlen) peekLen = maxlen;
    return XRingBuffer_peek(defaultReadBuf, data, peekLen);
}

int64_t XIODevicePrivate_readLineFromBuffer(XIODevicePrivate* d, char* data, int64_t maxlen) {
    if (!data || maxlen <= 0) return 0;
    struct XRingBuffer* defaultReadBuf = XIODevicePrivate_getOrCreateReadBuffer(d, d->q_ptr->m_currentReadChannel);

    size_t available = XRingBuffer_available(defaultReadBuf);
    if (available == 0) return 0;

    char* temp = (char*)XMalloc_System(available);
    if (!temp) return 0;

    size_t peeked = XRingBuffer_peek(defaultReadBuf, temp, available);
    int64_t lineEnd = -1;
    for (size_t i = 0; i < peeked && i < (size_t)(maxlen - 1); ++i) {
        if (temp[i] == '\n') {
            lineEnd = (int64_t)(i + 1);
            break;
        }
    }

    XFree_System(temp);

    if (lineEnd == -1) return 0;
    return XRingBuffer_read(defaultReadBuf, data, lineEnd);
}

bool XIODevicePrivate_canReadLineFromBuffer(const XIODevicePrivate* d) {
    struct XRingBuffer* defaultReadBuf = XIODevicePrivate_getOrCreateReadBuffer((XIODevicePrivate*)d, d->q_ptr->m_currentReadChannel);
    size_t available = XRingBuffer_available(defaultReadBuf);
    if (available == 0) return false;

    char* temp = (char*)XMalloc_System(available);
    if (!temp) return false;

    bool found = false;
    size_t peeked = XRingBuffer_peek(defaultReadBuf, temp, available);
    for (size_t i = 0; i < peeked; ++i) {
        if (temp[i] == '\n') {
            found = true;
            break;
        }
    }
    XFree_System(temp);
    return found;
}

void XIODevicePrivate_startTransaction(XIODevicePrivate* d) {
    d->transactionStarted = true;
    struct XRingBuffer* defaultReadBuf = XIODevicePrivate_getOrCreateReadBuffer(d, 0);
    if (defaultReadBuf) {
        XRingBuffer_mark(defaultReadBuf);
    }
}

void XIODevicePrivate_commitTransaction(XIODevicePrivate* d) {
    if (d->transactionStarted) {
        d->transactionStarted = false;
    }
}

void XIODevicePrivate_rollbackTransaction(XIODevicePrivate* d) {
    if (d->transactionStarted) {
        struct XRingBuffer* defaultReadBuf = XIODevicePrivate_getOrCreateReadBuffer(d, 0);
        if (defaultReadBuf) {
            XRingBuffer_resetToMark(defaultReadBuf);
        }
        d->transactionStarted = false;
    }
}