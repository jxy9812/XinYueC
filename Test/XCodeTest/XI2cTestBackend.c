/**
 * @file       XI2cTestBackend.c
 * @brief      I2C 命令测试用确定性模拟后端。
 * @details    仅在 XI2C_TEST_BACKEND 非零时编译。模拟器使用固定 256 字节
 *             存储和寄存器指针，覆盖打开、关闭、读、写和写后读事务；不调用
 *             操作系统 I/O，便于在 ASan/UBSan 下重复运行 Shell 全量测试。
 */
#include "XI2c.h"

#if defined(XI2C_TEST_BACKEND) && XI2C_TEST_BACKEND

#include "XMemory.h"
#include <string.h>

struct XI2c {
    XI2cConfig m_config;
    XI2cFeatures m_features;
    XI2cError m_error;
    int32_t m_nativeError;
    uint8_t m_memory[256];
    uint8_t m_register;
    bool m_open;
};

static bool xi2c_valid(const XI2cConfig* config)
{
    return config && config->m_target.m_addressMode <= XI2cAddressMode_TenBit &&
           config->m_flags == 0u &&
           ((config->m_target.m_addressMode == XI2cAddressMode_SevenBit && config->m_target.m_address <= 0x7fu) ||
            (config->m_target.m_addressMode == XI2cAddressMode_TenBit && config->m_target.m_address <= 0x3ffu));
}
static void xi2c_set_error(XI2c* bus, XI2cError error)
{
    if (bus) { bus->m_error = error; bus->m_nativeError = 0; }
}
static bool xi2c_ready(XI2c* bus, size_t length, const void* data)
{
    if (!bus || (length && !data)) return false;
    if (!bus->m_open) { xi2c_set_error(bus, XI2cError_NotOpen); return false; }
    return true;
}

XI2c* XI2c_create(const XI2cConfig* config)
{
    XI2c* bus;
    if (!xi2c_valid(config)) return NULL;
    bus = (XI2c*)XCalloc_System(1u, sizeof(*bus));
    if (!bus) return NULL;
    bus->m_config = *config;
    bus->m_features = XI2cFeature_Read | XI2cFeature_Write |
                      XI2cFeature_WriteRead | XI2cFeature_TenBitAddress |
                      XI2cFeature_Frequency;
    bus->m_error = XI2cError_None;
    return bus;
}
bool XI2c_open(XI2c* bus)
{
    if (!bus) return false;
    if (bus->m_open) { xi2c_set_error(bus, XI2cError_AlreadyOpen); return false; }
    bus->m_open = true;
    xi2c_set_error(bus, XI2cError_None);
    return true;
}
void XI2c_close(XI2c* bus) { if (bus) { bus->m_open = false; xi2c_set_error(bus, XI2cError_None); } }
void XI2c_delete(XI2c* bus) { if (bus) { XI2c_close(bus); XFree_System(bus); } }
bool XI2c_isOpen(const XI2c* bus) { return bus && bus->m_open; }
bool XI2c_getConfig(const XI2c* bus, XI2cConfig* config)
{
    if (!bus || !config) return false;
    *config = bus->m_config;
    return true;
}
bool XI2c_configure(XI2c* bus, const XI2cConfig* config)
{
    if (!bus || !xi2c_valid(config)) { if (bus) xi2c_set_error(bus, XI2cError_InvalidArgument); return false; }
    if (bus->m_open) { xi2c_set_error(bus, XI2cError_Busy); return false; }
    bus->m_config = *config;
    xi2c_set_error(bus, XI2cError_None);
    return true;
}
bool XI2c_write(XI2c* bus, const uint8_t* data, size_t length, int32_t timeoutMs)
{
    size_t i;
    (void)timeoutMs;
    if (!xi2c_ready(bus, length, data)) return false;
    if (length == 0u) { xi2c_set_error(bus, XI2cError_None); return true; }
    bus->m_register = data[0];
    for (i = 1u; i < length; ++i) bus->m_memory[(uint8_t)(bus->m_register + i - 1u)] = data[i];
    if (length > 1u) bus->m_register = (uint8_t)(bus->m_register + length - 1u);
    xi2c_set_error(bus, XI2cError_None);
    return true;
}
bool XI2c_read(XI2c* bus, uint8_t* data, size_t length, int32_t timeoutMs)
{
    size_t i;
    (void)timeoutMs;
    if (!xi2c_ready(bus, length, data)) return false;
    for (i = 0u; i < length; ++i) data[i] = bus->m_memory[(uint8_t)(bus->m_register + i)];
    bus->m_register = (uint8_t)(bus->m_register + length);
    xi2c_set_error(bus, XI2cError_None);
    return true;
}
bool XI2c_writeRead(XI2c* bus, const uint8_t* writeData, size_t writeLength,
                    uint8_t* readData, size_t readLength, int32_t timeoutMs)
{
    if (!xi2c_ready(bus, writeLength, writeData) || (readLength && !readData)) return false;
    if (!XI2c_write(bus, writeData, writeLength, timeoutMs)) return false;
    return XI2c_read(bus, readData, readLength, timeoutMs);
}
XI2cFeatures XI2c_features(const XI2c* bus) { return bus ? bus->m_features : XI2cFeature_None; }
bool XI2c_hasFeature(const XI2c* bus, XI2cFeature feature)
{
    return bus && feature != XI2cFeature_None && (bus->m_features & feature) != 0u;
}
XI2cError XI2c_lastError(const XI2c* bus) { return bus ? bus->m_error : XI2cError_InvalidArgument; }
int32_t XI2c_nativeError(const XI2c* bus) { return bus ? bus->m_nativeError : 0; }
void XI2c_clearError(XI2c* bus) { if (bus) xi2c_set_error(bus, XI2cError_None); }
const char* XI2c_errorString(XI2cError error)
{
    switch (error) {
    case XI2cError_None: return "none"; case XI2cError_InvalidArgument: return "invalid-argument";
    case XI2cError_NotOpen: return "not-open"; case XI2cError_AlreadyOpen: return "already-open";
    case XI2cError_Unsupported: return "unsupported"; case XI2cError_Busy: return "busy";
    case XI2cError_Timeout: return "timeout"; case XI2cError_Nack: return "nack";
    case XI2cError_ArbitrationLost: return "arbitration-lost"; case XI2cError_Bus: return "bus-error";
    case XI2cError_PermissionDenied: return "permission-denied"; case XI2cError_Interrupted: return "interrupted";
    case XI2cError_Hardware: return "hardware"; case XI2cError_Closed: return "closed";
    default: return "unknown";
    }
}

#endif
