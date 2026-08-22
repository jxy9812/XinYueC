/**
 * @file       XI2c_unsupported.c
 * @brief      无 I2C 驱动平台的安全存根。
 * @details    该文件只提供生命周期和错误查询符号，硬件打开及事务明确返回
 *             XI2cError_Unsupported。测试构建定义 XI2C_TEST_BACKEND 时由模拟
 *             后端替换本文件，避免与测试实现重复定义。
 */
#include "XI2c.h"

#if !(defined(XI2C_TEST_BACKEND) && XI2C_TEST_BACKEND)

#include "XMemory.h"

struct XI2c {
    XI2cConfig m_config;
    XI2cError m_error;
    int32_t m_nativeError;
    bool m_open;
};

static bool xi2c_valid(const XI2cConfig* config)
{
    return config && config->m_target.m_addressMode <= XI2cAddressMode_TenBit &&
           config->m_flags == 0u &&
           ((config->m_target.m_addressMode == XI2cAddressMode_SevenBit &&
             config->m_target.m_address <= 0x7fu) ||
            (config->m_target.m_addressMode == XI2cAddressMode_TenBit &&
             config->m_target.m_address <= 0x3ffu));
}

static void xi2c_error(XI2c* bus, XI2cError error)
{
    if (bus) {
        bus->m_error = error;
        bus->m_nativeError = 0;
    }
}

XI2c* XI2c_create(const XI2cConfig* config)
{
    XI2c* bus;
    if (!xi2c_valid(config)) return NULL;
    bus = (XI2c*)XCalloc_System(1u, sizeof(*bus));
    if (!bus) return NULL;
    bus->m_config = *config;
    bus->m_error = XI2cError_None;
    return bus;
}

bool XI2c_open(XI2c* bus)
{
    if (!bus) return false;
    if (bus->m_open) { xi2c_error(bus, XI2cError_AlreadyOpen); return false; }
    xi2c_error(bus, XI2cError_Unsupported);
    return false;
}

void XI2c_close(XI2c* bus)
{
    if (!bus) return;
    bus->m_open = false;
    xi2c_error(bus, XI2cError_None);
}

void XI2c_delete(XI2c* bus)
{
    if (!bus) return;
    XI2c_close(bus);
    XFree_System(bus);
}

bool XI2c_isOpen(const XI2c* bus) { return bus && bus->m_open; }

bool XI2c_getConfig(const XI2c* bus, XI2cConfig* config)
{
    if (!bus || !config) return false;
    *config = bus->m_config;
    return true;
}

bool XI2c_configure(XI2c* bus, const XI2cConfig* config)
{
    if (!bus || !xi2c_valid(config)) {
        if (bus) xi2c_error(bus, XI2cError_InvalidArgument);
        return false;
    }
    if (bus->m_open) { xi2c_error(bus, XI2cError_Unsupported); return false; }
    bus->m_config = *config;
    xi2c_error(bus, XI2cError_None);
    return true;
}

static bool xi2c_unsupported(XI2c* bus, const void* data, size_t length)
{
    if (!bus || (length && !data)) return false;
    if (!bus->m_open) { xi2c_error(bus, XI2cError_NotOpen); return false; }
    xi2c_error(bus, XI2cError_Unsupported);
    return false;
}

bool XI2c_write(XI2c* bus, const uint8_t* data, size_t length, int32_t timeoutMs)
{
    (void)timeoutMs;
    return xi2c_unsupported(bus, data, length);
}

bool XI2c_read(XI2c* bus, uint8_t* data, size_t length, int32_t timeoutMs)
{
    (void)timeoutMs;
    return xi2c_unsupported(bus, data, length);
}

bool XI2c_writeRead(XI2c* bus, const uint8_t* writeData, size_t writeLength,
                    uint8_t* readData, size_t readLength, int32_t timeoutMs)
{
    (void)timeoutMs;
    if (!bus || (writeLength && !writeData) || (readLength && !readData)) return false;
    return xi2c_unsupported(bus, writeData, writeLength + readLength);
}

XI2cFeatures XI2c_features(const XI2c* bus) { (void)bus; return XI2cFeature_None; }
bool XI2c_hasFeature(const XI2c* bus, XI2cFeature feature)
{
    return bus && feature != XI2cFeature_None && (XI2c_features(bus) & feature) != 0u;
}
XI2cError XI2c_lastError(const XI2c* bus)
{
    return bus ? bus->m_error : XI2cError_InvalidArgument;
}
int32_t XI2c_nativeError(const XI2c* bus) { return bus ? bus->m_nativeError : 0; }
void XI2c_clearError(XI2c* bus) { if (bus) xi2c_error(bus, XI2cError_None); }
const char* XI2c_errorString(XI2cError error)
{
    switch (error) {
    case XI2cError_None: return "none";
    case XI2cError_InvalidArgument: return "invalid-argument";
    case XI2cError_NotOpen: return "not-open";
    case XI2cError_AlreadyOpen: return "already-open";
    case XI2cError_Unsupported: return "unsupported";
    case XI2cError_Busy: return "busy";
    case XI2cError_Timeout: return "timeout";
    case XI2cError_Nack: return "nack";
    case XI2cError_ArbitrationLost: return "arbitration-lost";
    case XI2cError_Bus: return "bus-error";
    case XI2cError_PermissionDenied: return "permission-denied";
    case XI2cError_Interrupted: return "interrupted";
    case XI2cError_Hardware: return "hardware";
    case XI2cError_Closed: return "closed";
    default: return "unknown";
    }
}

#endif
