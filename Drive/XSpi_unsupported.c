/**
 * @file       XSpi_unsupported.c
 * @brief      无 SPI 驱动平台的安全存根。
 * @details    硬件打开和事务明确返回 XSpiError_Unsupported；定义
 *             XSPI_TEST_BACKEND 时由测试模拟后端提供实现。
 */
#include "XSpi.h"

#if !(defined(XSPI_TEST_BACKEND) && XSPI_TEST_BACKEND)

#include "XMemory.h"

struct XSpi {
    XSpiConfig m_config;
    XSpiError m_error;
    int32_t m_nativeError;
    bool m_open;
};

static bool xspi_valid(const XSpiConfig* config)
{
    return config && config->m_mode <= 3u &&
           (config->m_bitsPerWord == 8u || config->m_bitsPerWord == 16u) &&
           config->m_flags == 0u;
}

static void xspi_error(XSpi* spi, XSpiError error)
{
    if (spi) { spi->m_error = error; spi->m_nativeError = 0; }
}

XSpi* XSpi_create(const XSpiConfig* config)
{
    XSpi* spi;
    if (!xspi_valid(config)) return NULL;
    spi = (XSpi*)XCalloc_System(1u, sizeof(*spi));
    if (!spi) return NULL;
    spi->m_config = *config;
    spi->m_error = XSpiError_None;
    return spi;
}
bool XSpi_open(XSpi* spi)
{
    if (!spi) return false;
    if (spi->m_open) { xspi_error(spi, XSpiError_AlreadyOpen); return false; }
    xspi_error(spi, XSpiError_Unsupported);
    return false;
}
void XSpi_close(XSpi* spi) { if (spi) { spi->m_open = false; xspi_error(spi, XSpiError_None); } }
void XSpi_delete(XSpi* spi) { if (spi) { XSpi_close(spi); XFree_System(spi); } }
bool XSpi_isOpen(const XSpi* spi) { return spi && spi->m_open; }
bool XSpi_getConfig(const XSpi* spi, XSpiConfig* config)
{
    if (!spi || !config) return false;
    *config = spi->m_config;
    return true;
}
bool XSpi_configure(XSpi* spi, const XSpiConfig* config)
{
    if (!spi || !xspi_valid(config)) { if (spi) xspi_error(spi, XSpiError_InvalidArgument); return false; }
    if (spi->m_open) { xspi_error(spi, XSpiError_Unsupported); return false; }
    spi->m_config = *config;
    xspi_error(spi, XSpiError_None);
    return true;
}
bool XSpi_transfer(XSpi* spi, const uint8_t* tx, uint8_t* rx,
                   size_t length, int32_t timeoutMs)
{
    (void)timeoutMs;
    if (!spi || (!tx && !rx && length)) return false;
    if (!spi->m_open) { xspi_error(spi, XSpiError_NotOpen); return false; }
    xspi_error(spi, XSpiError_Unsupported);
    return false;
}
XSpiFeatures XSpi_features(const XSpi* spi) { (void)spi; return XSpiFeature_None; }
bool XSpi_hasFeature(const XSpi* spi, XSpiFeature feature)
{
    return spi && feature != XSpiFeature_None && (XSpi_features(spi) & feature) != 0u;
}
XSpiError XSpi_lastError(const XSpi* spi)
{
    return spi ? spi->m_error : XSpiError_InvalidArgument;
}
int32_t XSpi_nativeError(const XSpi* spi) { return spi ? spi->m_nativeError : 0; }
void XSpi_clearError(XSpi* spi) { if (spi) xspi_error(spi, XSpiError_None); }
const char* XSpi_errorString(XSpiError error)
{
    switch (error) {
    case XSpiError_None: return "none";
    case XSpiError_InvalidArgument: return "invalid-argument";
    case XSpiError_NotOpen: return "not-open";
    case XSpiError_AlreadyOpen: return "already-open";
    case XSpiError_Unsupported: return "unsupported";
    case XSpiError_Busy: return "busy";
    case XSpiError_Timeout: return "timeout";
    case XSpiError_PermissionDenied: return "permission-denied";
    case XSpiError_Interrupted: return "interrupted";
    case XSpiError_Hardware: return "hardware";
    case XSpiError_Closed: return "closed";
    default: return "unknown";
    }
}

#endif
