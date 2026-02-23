#include "XIODevicePrivate.h"
#include "XIODevice.h"
#include "XMemory.h"
#include <string.h>

static void ensureReadChannel(XIODevicePrivate* d, int channel) {
    if (channel < 0) return;
    if (channel >= d->maxReadChannels) {
        int newCount = channel + 1;
        d->readBuffers = (XByteArray**)XMemory_realloc(d->readBuffers, sizeof(XByteArray*) * newCount);
        d->ungetBuffers = (XByteArray**)XMemory_realloc(d->ungetBuffers, sizeof(XByteArray*) * newCount);
        for (int i = d->maxReadChannels; i < newCount; ++i) {
            d->readBuffers[i] = XByteArray_create();
            d->ungetBuffers[i] = XByteArray_create();
        }
        d->maxReadChannels = newCount;
    }
}

XIODevicePrivate* XIODevicePrivate_create(XIODevice* q) {
    XIODevicePrivate* d = (XIODevicePrivate*)XMemory_malloc(sizeof(XIODevicePrivate));
    if (!d) return NULL;
    memset(d, 0, sizeof(XIODevicePrivate));

    d->maxReadChannels = 1;
    d->readBuffers = (XByteArray**)XMemory_malloc(sizeof(XByteArray*));
    d->ungetBuffers = (XByteArray**)XMemory_malloc(sizeof(XByteArray*));
    d->readBuffers[0] = XByteArray_create();
    d->ungetBuffers[0] = XByteArray_create();

    d->writeBuffer = XByteArray_create();
    d->transactionBuffer = XByteArray_create();
    d->errorString = NULL;
    d->transactionStarted = false;
    d->aboutToCloseEmitted = false;
    d->q_ptr = q;
    return d;
}

void XIODevicePrivate_delete(XIODevicePrivate* d) {
    if (!d) return;
    for (int i = 0; i < d->maxReadChannels; ++i) {
        XByteArray_delete_base(d->readBuffers[i]);
        XByteArray_delete_base(d->ungetBuffers[i]);
    }
    XMemory_free(d->readBuffers);
    XMemory_free(d->ungetBuffers);
    XByteArray_delete_base(d->writeBuffer);
    XByteArray_delete_base(d->transactionBuffer);
    if (d->errorString) XString_delete_base(d->errorString);
    XMemory_free(d);
}

void XIODevicePrivate_putChar(XIODevicePrivate* d, char c, int channel) {
    ensureReadChannel(d, channel);
    XByteArray_prepend_base(d->ungetBuffers[channel], &c);
}

bool XIODevicePrivate_getChar(XIODevicePrivate* d, char* c, int channel) {
    if (!c) return false;
    ensureReadChannel(d, channel);
    XByteArray* buf = d->ungetBuffers[channel];
    if (XByteArray_size_base(buf) > 0) {
        *c = XByteArray_At_Base(buf, 0);
        XByteArray_remove_base(buf, 0, 1);
        return true;
    }
    return false;
}

int64_t XIODevicePrivate_peek(XIODevicePrivate* d, char* data, int64_t maxlen, XIODevice* device, int channel) {
    if (!data || maxlen <= 0) return 0;
    ensureReadChannel(d, channel);
    XByteArray* buf = d->readBuffers[channel];

    int64_t available = XByteArray_size_base(buf);
    if (available < maxlen) {
        int64_t toRead = maxlen - available;
        char* temp = (char*)XMemory_malloc(toRead);
        if (temp) {
            int64_t n = XIODevice_read(device, temp, toRead);
            if (n > 0) {
                XByteArray_append_array_base(buf, temp, n);
            }
            XMemory_free(temp);
        }
    }

    int64_t copyLen = XByteArray_size_base(buf);
    if (copyLen > maxlen) copyLen = maxlen;
    memcpy(data, XContainerDataPtr(buf), copyLen);
    return copyLen;
}

int64_t XIODevicePrivate_readLineFromBuffer(XIODevicePrivate* d, char* data, int64_t maxlen, int channel) {
    if (!data || maxlen <= 0) return 0;
    ensureReadChannel(d, channel);
    XByteArray* buf = d->readBuffers[channel];
    const char* p = XContainerDataPtr(buf);
    int64_t len = XByteArray_size_base(buf);
    int64_t lineEnd = -1;

    for (int64_t i = 0; i < len && i < maxlen - 1; ++i) {
        if (p[i] == '\n') {
            lineEnd = i + 1;
            break;
        }
    }

    if (lineEnd == -1) return 0;

    memcpy(data, p, lineEnd);
    XByteArray_remove_base(buf, 0, lineEnd);
    return lineEnd;
}

bool XIODevicePrivate_canReadLineFromBuffer(const XIODevicePrivate* d, int channel) {
    ensureReadChannel((XIODevicePrivate*)d, channel);
    const char* p = XContainerDataPtr(d->readBuffers[channel]);
    int64_t len = XByteArray_size_base(d->readBuffers[channel]);
    for (int64_t i = 0; i < len; ++i) {
        if (p[i] == '\n') return true;
    }
    return false;
}

void XIODevicePrivate_startTransaction(XIODevicePrivate* d) {
    d->transactionStarted = true;
    XByteArray_clear_base(d->transactionBuffer);
}

void XIODevicePrivate_commitTransaction(XIODevicePrivate* d) {
    d->transactionStarted = false;
    XByteArray_clear_base(d->transactionBuffer);
}

void XIODevicePrivate_rollbackTransaction(XIODevicePrivate* d) {
    if (d->transactionStarted) {
        int ch = d->q_ptr ? d->q_ptr->m_currentReadChannel : 0;
        ensureReadChannel(d, ch);
        XByteArray_clear_base(d->readBuffers[ch]);
        XByteArray_clear_base(d->ungetBuffers[ch]);
        d->transactionStarted = false;
    }
}