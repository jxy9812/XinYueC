/**
 * @file       XDeviceUsbMsc.c
 * @brief      USB MSC BOT 与 SCSI 块命令实现。
 */
#include "XDeviceUsbMsc.h"
#include "XMemory.h"
#include <stdlib.h>
#include <string.h>

#define XUSB_MSC_CLASS 0x08u
#define XUSB_MSC_SUBCLASS_SCSI 0x06u
#define XUSB_MSC_PROTOCOL_BOT 0x50u
#define XUSB_MSC_DESC_CONFIGURATION 0x02u
#define XUSB_MSC_DESC_INTERFACE 0x04u
#define XUSB_MSC_DESC_ENDPOINT 0x05u
#define XUSB_MSC_ENDPOINT_BULK 0x02u
#define XUSB_MSC_CBW_SIGNATURE 0x43425355u
#define XUSB_MSC_CSW_SIGNATURE 0x53425355u

static uint16_t xmscLe16(const uint8_t* p)
{
    return (uint16_t)(p[0] | ((uint16_t)p[1] << 8));
}

static uint32_t xmscLe32(const uint8_t* p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static uint64_t xmscBe64(const uint8_t* p)
{
    uint64_t value = 0;
    size_t i;
    for (i = 0; i < 8; ++i) value = (value << 8) | p[i];
    return value;
}

static uint32_t xmscBe32(const uint8_t* p)
{
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
           ((uint32_t)p[2] << 8) | p[3];
}

static void xmscStoreLe32(uint8_t* p, uint32_t value)
{
    p[0] = (uint8_t)value;
    p[1] = (uint8_t)(value >> 8);
    p[2] = (uint8_t)(value >> 16);
    p[3] = (uint8_t)(value >> 24);
}

static void xmscStoreBe32(uint8_t* p, uint32_t value)
{
    p[0] = (uint8_t)(value >> 24);
    p[1] = (uint8_t)(value >> 16);
    p[2] = (uint8_t)(value >> 8);
    p[3] = (uint8_t)value;
}

static void xmscStoreBe64(uint8_t* p, uint64_t value)
{
    size_t i;
    for (i = 0; i < 8; ++i) p[7 - i] = (uint8_t)(value >> (i * 8));
}

static bool xmscTransfer(XFd fd, XDeviceUsbEndpointAddress endpoint,
                         void* data, size_t length, int32_t timeoutMs)
{
    size_t transferred = 0;
    XDeviceUsbTransferResult result;
    result = XDeviceUsbHost_transfer(fd, endpoint, data, length, &transferred,
                                     timeoutMs);
    return result == XDeviceUsbTransferResult_Ok && transferred == length;
}

static bool xmscFindEndpoints(XFd fd, uint8_t interfaceNumber,
                              uint8_t alternateSetting,
                              XDeviceUsbMscDevice* device)
{
    size_t count;
    size_t i;
    XDeviceUsbEndpointInfo endpoint;
    count = XDeviceUsbHost_endpointCount(fd, interfaceNumber, alternateSetting);
    for (i = 0; i < count; ++i) {
        if (!XDeviceUsbHost_getEndpointInfo(fd, interfaceNumber,
                                             alternateSetting, i, &endpoint))
            continue;
        if (endpoint.m_transferType != XDeviceUsbTransferType_Bulk) continue;
        if (XDEVICE_USB_ENDPOINT_IS_IN(endpoint.m_address)) {
            if (device->m_bulkIn == 0) {
                device->m_bulkIn = endpoint.m_address;
                device->m_bulkInPacketSize = endpoint.m_maxPacketSize;
            }
        } else if (device->m_bulkOut == 0) {
            device->m_bulkOut = endpoint.m_address;
            device->m_bulkOutPacketSize = endpoint.m_maxPacketSize;
        }
    }
    return device->m_bulkIn != 0 && device->m_bulkOut != 0;
}

static bool xmscFindInterfaceInConfig(const uint8_t* raw, size_t length,
                                      uint8_t* interfaceNumber,
                                      uint8_t* alternateSetting)
{
    size_t offset = 0;
    while (offset + 2 <= length) {
        uint8_t descriptorLength = raw[offset];
        uint8_t descriptorType = raw[offset + 1];
        if (descriptorLength < 2 || descriptorLength > length - offset) return false;
        if (descriptorType == XUSB_MSC_DESC_INTERFACE && descriptorLength >= 9 &&
            raw[offset + 5] == XUSB_MSC_CLASS &&
            raw[offset + 6] == XUSB_MSC_SUBCLASS_SCSI &&
            raw[offset + 7] == XUSB_MSC_PROTOCOL_BOT) {
            *interfaceNumber = raw[offset + 2];
            *alternateSetting = raw[offset + 3];
            return true;
        }
        offset += descriptorLength;
    }
    return false;
}

static bool xmscLoadConfiguration(XFd fd, uint8_t index, uint8_t* value,
                                  uint8_t* interfaceNumber,
                                  uint8_t* alternateSetting,
                                  int32_t timeoutMs)
{
    uint8_t header[9];
    uint8_t* raw;
    uint16_t totalLength;
    size_t transferred;
    XDeviceUsbTransferResult result;
    bool found;

    result = XDeviceUsbHost_getDescriptor(fd, XDeviceUsbDescriptorType_Configuration,
                                          index, 0, header, sizeof(header),
                                          &transferred, timeoutMs);
    if (result != XDeviceUsbTransferResult_Ok || transferred < sizeof(header) ||
        header[0] < 9 || header[1] != XUSB_MSC_DESC_CONFIGURATION)
        return false;
    totalLength = xmscLe16(header + 2);
    if (totalLength < 9 || totalLength > 65535u) return false;
    raw = (uint8_t*)XMalloc_System(totalLength);
    if (!raw) return false;
    result = XDeviceUsbHost_getDescriptor(fd, XDeviceUsbDescriptorType_Configuration,
                                          index, 0, raw, totalLength,
                                          &transferred, timeoutMs);
    if (result != XDeviceUsbTransferResult_Ok || transferred < 9 ||
        transferred < totalLength) {
        XFree_System(raw);
        return false;
    }
    found = raw[0] >= 9 && raw[1] == XUSB_MSC_DESC_CONFIGURATION &&
            xmscFindInterfaceInConfig(raw, totalLength, interfaceNumber,
                                       alternateSetting);
    if (found) *value = raw[5];
    XFree_System(raw);
    return found && *value != 0;
}

void XDeviceUsbMsc_init(XDeviceUsbMscDevice* device)
{
    if (!device) return;
    memset(device, 0, sizeof(*device));
    device->m_hostFd = XFD_INVALID;
    device->m_tag = 1u;
}

bool XDeviceUsbMsc_discover(XFd hostFd, XDeviceUsbMscDevice* device,
                            int32_t timeoutMs)
{
    XDeviceUsbDeviceInfo info;
    uint8_t index;
    uint8_t configurationValue = 0;
    uint8_t interfaceNumber = 0;
    uint8_t alternateSetting = 0;
    XDeviceUsbMscDevice candidate;

    if (!device || hostFd == XFD_INVALID) return false;
    XDeviceUsbMsc_close(device);
    if (!XDeviceUsbHost_getInfo(hostFd, &info) || info.m_configurationCount == 0)
        return false;
    for (index = 0; index < info.m_configurationCount; ++index) {
        XDeviceUsbMsc_init(&candidate);
        if (!xmscLoadConfiguration(hostFd, index, &configurationValue,
                                   &interfaceNumber, &alternateSetting,
                                   timeoutMs))
            continue;
        if (!XDeviceUsbHost_setConfiguration(hostFd, configurationValue) ||
            !XDeviceUsbHost_claimInterface(hostFd, interfaceNumber) ||
            !XDeviceUsbHost_setAlternateSetting(hostFd, interfaceNumber,
                                                alternateSetting)) {
            (void)XDeviceUsbHost_releaseInterface(hostFd, interfaceNumber);
            continue;
        }
        candidate.m_hostFd = hostFd;
        candidate.m_interfaceNumber = interfaceNumber;
        candidate.m_alternateSetting = alternateSetting;
        candidate.m_interfaceClaimed = true;
        if (!xmscFindEndpoints(hostFd, interfaceNumber, alternateSetting,
                               &candidate)) {
            XDeviceUsbMsc_close(&candidate);
            continue;
        }
        *device = candidate;
        return true;
    }
    return false;
}

void XDeviceUsbMsc_close(XDeviceUsbMscDevice* device)
{
    if (!device) return;
    if (device->m_interfaceClaimed && device->m_hostFd != XFD_INVALID)
        (void)XDeviceUsbHost_releaseInterface(device->m_hostFd,
                                               device->m_interfaceNumber);
    XDeviceUsbMsc_init(device);
}

bool XDeviceUsbMsc_setLun(XDeviceUsbMscDevice* device, uint8_t lun)
{
    if (!device || device->m_hostFd == XFD_INVALID || lun > 15u) return false;
    device->m_lun = lun;
    return true;
}

bool XDeviceUsbMsc_resetRecovery(XDeviceUsbMscDevice* device, int32_t timeoutMs)
{
    XDeviceUsbControlRequest request;
    if (!device || !device->m_interfaceClaimed) return false;
    memset(&request, 0, sizeof(request));
    request.m_requestType = 0x21u;
    request.m_request = 0xffu;
    request.m_index = device->m_interfaceNumber;
    if (XDeviceUsbHost_controlTransfer(device->m_hostFd, &request, NULL, 0,
                                       NULL, timeoutMs) != XDeviceUsbTransferResult_Ok)
        return false;
    return XDeviceUsbHost_clearHalt(device->m_hostFd, device->m_bulkIn) &&
           XDeviceUsbHost_clearHalt(device->m_hostFd, device->m_bulkOut);
}

XDeviceUsbTransferResult XDeviceUsbMsc_execute(
    XDeviceUsbMscDevice* device, const uint8_t* cdb, uint8_t cdbLength,
    void* data, size_t dataLength, bool dataIn, size_t* transferred,
    XDeviceUsbMscCommandStatus* status, int32_t timeoutMs)
{
    uint8_t cbw[31];
    uint8_t csw[13];
    uint32_t tag;
    uint32_t residue;
    size_t dataTransferred = 0;
    size_t cswTransferred = 0;
    XDeviceUsbTransferResult result;
    XDeviceUsbMscCommandStatus commandStatus;
    XDeviceUsbEndpointAddress dataEndpoint;
    bool dataPhaseHadStall = false;

    if (transferred) *transferred = 0;
    if (status) *status = XDeviceUsbMscCommandStatus_Invalid;
    if (!device || !device->m_interfaceClaimed || !cdb || cdbLength == 0 ||
        cdbLength > 16u || (dataLength != 0 && !data))
        return XDeviceUsbTransferResult_InvalidArgument;
    tag = device->m_tag++;
    if (device->m_tag == 0) device->m_tag = 1u;
    memset(cbw, 0, sizeof(cbw));
    xmscStoreLe32(cbw, XUSB_MSC_CBW_SIGNATURE);
    xmscStoreLe32(cbw + 4, tag);
    xmscStoreLe32(cbw + 8, dataLength > UINT32_MAX ? UINT32_MAX : (uint32_t)dataLength);
    cbw[12] = dataLength != 0 && dataIn ? 0x80u : 0;
    cbw[13] = device->m_lun;
    cbw[14] = cdbLength;
    memcpy(cbw + 15, cdb, cdbLength);

    /* 阶段 1：CBW → Bulk-OUT */
    if (!xmscTransfer(device->m_hostFd, device->m_bulkOut, cbw, sizeof(cbw),
                      timeoutMs)) {
        /* CBW 发送失败，端点可能已 STALL；执行完整恢复。 */
        (void)XDeviceUsbMsc_resetRecovery(device, timeoutMs);
        return XDeviceUsbTransferResult_IoError;
    }

    /* 阶段 2：数据阶段 */
    dataEndpoint = dataIn ? device->m_bulkIn : device->m_bulkOut;
    if (dataLength != 0) {
        result = XDeviceUsbHost_transfer(device->m_hostFd, dataEndpoint,
                                         data, dataLength, &dataTransferred,
                                         timeoutMs);
        if (result == XDeviceUsbTransferResult_Stall) {
            /* 数据阶段 STALL：设备拒绝数据长度，先清端点 Halt 再读 CSW。 */
            dataPhaseHadStall = true;
            (void)XDeviceUsbHost_clearHalt(device->m_hostFd, dataEndpoint);
        } else if (result != XDeviceUsbTransferResult_Ok) {
            /* 其他错误（超时、取消等）不强制 reset，直接上报。 */
            return result;
        }
    }

    /* 阶段 3：CSW ← Bulk-IN */
    result = XDeviceUsbHost_transfer(device->m_hostFd, device->m_bulkIn, csw,
                                     sizeof(csw), &cswTransferred, timeoutMs);
    if (result == XDeviceUsbTransferResult_Stall) {
        /* CSW 阶段 STALL：执行 Bulk-Only Mass Storage Reset。 */
        (void)XDeviceUsbMsc_resetRecovery(device, timeoutMs);
        return XDeviceUsbTransferResult_IoError;
    }
    if (result != XDeviceUsbTransferResult_Ok) return result;

    if (transferred) *transferred = dataTransferred;

    if (cswTransferred != sizeof(csw) ||
        xmscLe32(csw) != XUSB_MSC_CSW_SIGNATURE ||
        xmscLe32(csw + 4) != tag) {
        /* CSW 无效：重置总线恢复。 */
        (void)XDeviceUsbMsc_resetRecovery(device, timeoutMs);
        return XDeviceUsbTransferResult_IoError;
    }

    commandStatus = (XDeviceUsbMscCommandStatus)(csw[12] & 0x03u);
    residue = xmscLe32(csw + 8);
    if (status) *status = commandStatus;

    /* 校正 transferred：以实际数据 + CSW residue 为准。 */
    if (transferred && dataLength > 0) {
        if (residue <= dataLength && dataTransferred + residue == dataLength) {
            /* residue 与 transferred 一致，保持原样。 */
        } else if (residue < dataLength) {
            /* 以 residue 为准重新计算已传输字节数。 */
            *transferred = dataLength - (size_t)residue;
        }
    }

    if (commandStatus == XDeviceUsbMscCommandStatus_PhaseError) {
        /* Phase Error 必须执行 BOT Reset Recovery。 */
        (void)XDeviceUsbMsc_resetRecovery(device, timeoutMs);
    } else if (commandStatus == XDeviceUsbMscCommandStatus_Failed &&
               dataPhaseHadStall && dataIn) {
        /* 设备以 STALL + CSW(FAILED) 报告错误：已清 Halt，状态正常上报。 */
    }

    return XDeviceUsbTransferResult_Ok;
}

XDeviceUsbTransferResult XDeviceUsbMsc_inquiry(
    XDeviceUsbMscDevice* device, uint8_t inquiryData[36], int32_t timeoutMs)
{
    uint8_t cdb[6] = { 0x12u, 0, 0, 0, 36u, 0 };
    XDeviceUsbMscCommandStatus status;
    size_t transferred = 0;
    XDeviceUsbTransferResult result;
    if (!inquiryData) return XDeviceUsbTransferResult_InvalidArgument;
    result = XDeviceUsbMsc_execute(device, cdb, sizeof(cdb), inquiryData, 36,
                                   true, &transferred, &status, timeoutMs);
    if (result != XDeviceUsbTransferResult_Ok ||
        status != XDeviceUsbMscCommandStatus_Passed || transferred != 36)
        return result == XDeviceUsbTransferResult_Ok ?
            XDeviceUsbTransferResult_IoError : result;
    return result;
}

XDeviceUsbTransferResult XDeviceUsbMsc_readCapacity(
    XDeviceUsbMscDevice* device, uint32_t* blockSize, uint64_t* blockCount,
    int32_t timeoutMs)
{
    uint8_t cdb10[10] = { 0x25u };
    uint8_t data10[8];
    uint8_t cdb16[16] = { 0x9eu, 0x10u };
    uint8_t data16[32];
    uint32_t last10;
    uint32_t size;
    uint64_t count;
    size_t transferred = 0;
    XDeviceUsbMscCommandStatus status;
    XDeviceUsbTransferResult result;

    if (!device || !blockSize || !blockCount) return XDeviceUsbTransferResult_InvalidArgument;
    result = XDeviceUsbMsc_execute(device, cdb10, sizeof(cdb10), data10,
                                   sizeof(data10), true, &transferred, &status,
                                   timeoutMs);
    if (result != XDeviceUsbTransferResult_Ok ||
        status != XDeviceUsbMscCommandStatus_Passed || transferred != sizeof(data10))
        return result == XDeviceUsbTransferResult_Ok ? XDeviceUsbTransferResult_IoError : result;
    last10 = xmscBe32(data10);
    size = xmscBe32(data10 + 4);
    if (size == 0) return XDeviceUsbTransferResult_IoError;
    if (last10 == UINT32_MAX) {
        memset(data16, 0, sizeof(data16));
        xmscStoreBe32(cdb16 + 10, sizeof(data16));
        result = XDeviceUsbMsc_execute(device, cdb16, sizeof(cdb16), data16,
                                       sizeof(data16), true, &transferred, &status,
                                       timeoutMs);
        if (result != XDeviceUsbTransferResult_Ok ||
            status != XDeviceUsbMscCommandStatus_Passed || transferred < 12)
            return result == XDeviceUsbTransferResult_Ok ? XDeviceUsbTransferResult_IoError : result;
        count = xmscBe64(data16) + 1u;
        size = xmscBe32(data16 + 8);
    } else {
        count = (uint64_t)last10 + 1u;
    }
    if (size == 0) return XDeviceUsbTransferResult_IoError;
    device->m_blockSize = size;
    device->m_blockCount = count;
    *blockSize = size;
    *blockCount = count;
    return XDeviceUsbTransferResult_Ok;
}

static void xmscBuildRwCdb(uint8_t* cdb, bool write, uint64_t firstBlock,
                           uint32_t blockCount, bool use16)
{
    size_t i;
    memset(cdb, 0, use16 ? 16 : 10);
    if (use16) {
        cdb[0] = write ? 0x8au : 0x88u;
        for (i = 0; i < 8; ++i) cdb[9 - i] = (uint8_t)(firstBlock >> (i * 8));
        xmscStoreBe32(cdb + 10, blockCount);
    } else {
        cdb[0] = write ? 0x2au : 0x28u;
        xmscStoreBe32(cdb + 2, (uint32_t)firstBlock);
        cdb[7] = (uint8_t)(blockCount >> 8);
        cdb[8] = (uint8_t)blockCount;
    }
}

static XDeviceUsbTransferResult xmscReadWriteBlocks(
    XDeviceUsbMscDevice* device, uint64_t firstBlock, uint32_t blockCount,
    void* data, size_t length, size_t* transferred, int32_t timeoutMs, bool write)
{
    uint8_t cdb[16];
    bool use16;
    size_t expected;
    size_t dataTransferred = 0;
    XDeviceUsbMscCommandStatus status;
    XDeviceUsbTransferResult result;
    if (transferred) *transferred = 0;
    if (!device || !device->m_interfaceClaimed || device->m_blockSize == 0 ||
        blockCount == 0 || (!data && length != 0))
        return XDeviceUsbTransferResult_InvalidArgument;
    if ((size_t)blockCount > SIZE_MAX / device->m_blockSize)
        return XDeviceUsbTransferResult_InvalidArgument;
    expected = (size_t)blockCount * device->m_blockSize;
    if (length != expected) return XDeviceUsbTransferResult_InvalidArgument;
    if (device->m_blockCount != 0 &&
        (firstBlock >= device->m_blockCount ||
         (uint64_t)blockCount > device->m_blockCount - firstBlock))
        return XDeviceUsbTransferResult_InvalidArgument;
    use16 = firstBlock > UINT32_MAX || blockCount > 65535u;
    xmscBuildRwCdb(cdb, write, firstBlock, blockCount, use16);
    result = XDeviceUsbMsc_execute(device, cdb, use16 ? 16u : 10u, data, length,
                                   !write, &dataTransferred, &status, timeoutMs);
    if (transferred) *transferred = dataTransferred;
    if (result != XDeviceUsbTransferResult_Ok) return result;
    if (status != XDeviceUsbMscCommandStatus_Passed || dataTransferred != expected)
        return XDeviceUsbTransferResult_IoError;
    return XDeviceUsbTransferResult_Ok;
}

XDeviceUsbTransferResult XDeviceUsbMsc_readBlocks(
    XDeviceUsbMscDevice* device, uint64_t firstBlock, uint32_t blockCount,
    void* data, size_t capacity, size_t* transferred, int32_t timeoutMs)
{
    return xmscReadWriteBlocks(device, firstBlock, blockCount, data, capacity,
                                transferred, timeoutMs, false);
}

XDeviceUsbTransferResult XDeviceUsbMsc_writeBlocks(
    XDeviceUsbMscDevice* device, uint64_t firstBlock, uint32_t blockCount,
    const void* data, size_t length, size_t* transferred, int32_t timeoutMs)
{
    return xmscReadWriteBlocks(device, firstBlock, blockCount, (void*)data, length,
                                transferred, timeoutMs, true);
}

XDeviceUsbTransferResult XDeviceUsbMsc_testUnitReady(
    XDeviceUsbMscDevice* device, XDeviceUsbMscCommandStatus* status,
    int32_t timeoutMs)
{
    uint8_t cdb[6] = { 0x00u };
    XDeviceUsbMscCommandStatus localStatus;
    if (!device || !device->m_interfaceClaimed)
        return XDeviceUsbTransferResult_InvalidArgument;
    if (!status) status = &localStatus;
    return XDeviceUsbMsc_execute(device, cdb, sizeof(cdb), NULL, 0, true,
                                  NULL, status, timeoutMs);
}

XDeviceUsbTransferResult XDeviceUsbMsc_requestSense(
    XDeviceUsbMscDevice* device, uint8_t senseData[18],
    uint8_t* senseKey, uint8_t* asc, uint8_t* ascq, int32_t timeoutMs)
{
    uint8_t cdb[6] = { 0x03u, 0x00u, 0x00u, 0x00u, 18u, 0x00u };
    uint8_t buffer[18];
    size_t transferred = 0;
    XDeviceUsbMscCommandStatus status;
    XDeviceUsbTransferResult result;
    if (!device || !device->m_interfaceClaimed || !senseData)
        return XDeviceUsbTransferResult_InvalidArgument;
    memset(buffer, 0, sizeof(buffer));
    result = XDeviceUsbMsc_execute(device, cdb, sizeof(cdb), buffer,
                                   sizeof(buffer), true, &transferred, &status,
                                   timeoutMs);
    if (result != XDeviceUsbTransferResult_Ok) return result;
    if (status != XDeviceUsbMscCommandStatus_Passed || transferred < 18)
        return XDeviceUsbTransferResult_IoError;
    memcpy(senseData, buffer, 18);
    if (senseKey) *senseKey = buffer[2] & 0x0Fu;
    if (asc) *asc = buffer[12];
    if (ascq) *ascq = buffer[13];
    return XDeviceUsbTransferResult_Ok;
}

XDeviceUsbTransferResult XDeviceUsbMsc_preventAllowRemoval(
    XDeviceUsbMscDevice* device, bool prevent, int32_t timeoutMs)
{
    uint8_t cdb[6] = { 0x1Eu, 0x00u, 0x00u, 0x00u,
                       (uint8_t)(prevent ? 0x01u : 0x00u), 0x00u };
    XDeviceUsbMscCommandStatus status;
    XDeviceUsbTransferResult result;
    if (!device || !device->m_interfaceClaimed)
        return XDeviceUsbTransferResult_InvalidArgument;
    result = XDeviceUsbMsc_execute(device, cdb, sizeof(cdb), NULL, 0, true,
                                    NULL, &status, timeoutMs);
    if (result != XDeviceUsbTransferResult_Ok) return result;
    return status == XDeviceUsbMscCommandStatus_Passed ?
        XDeviceUsbTransferResult_Ok : XDeviceUsbTransferResult_IoError;
}

XDeviceUsbTransferResult XDeviceUsbMsc_startStopUnit(
    XDeviceUsbMscDevice* device, bool start, bool loadEject, int32_t timeoutMs)
{
    uint8_t powerCondition = 0x00u;
    uint8_t startByte = 0x00u;
    uint8_t cdb[6];
    XDeviceUsbMscCommandStatus status;
    XDeviceUsbTransferResult result;
    if (!device || !device->m_interfaceClaimed)
        return XDeviceUsbTransferResult_InvalidArgument;
    if (start) startByte |= 0x01u;
    if (loadEject) startByte |= 0x02u;
    cdb[0] = 0x1Bu;
    cdb[1] = 0x00u;
    cdb[2] = (uint8_t)(powerCondition << 4);
    cdb[3] = 0x00u;
    cdb[4] = startByte;
    cdb[5] = 0x00u;
    result = XDeviceUsbMsc_execute(device, cdb, sizeof(cdb), NULL, 0, true,
                                    NULL, &status, timeoutMs);
    if (result != XDeviceUsbTransferResult_Ok) return result;
    return status == XDeviceUsbMscCommandStatus_Passed ?
        XDeviceUsbTransferResult_Ok : XDeviceUsbTransferResult_IoError;
}

XDeviceUsbTransferResult XDeviceUsbMsc_readBytes(
    XDeviceUsbMscDevice* device, uint64_t byteOffset,
    void* data, size_t byteCount, size_t* transferred, int32_t timeoutMs)
{
    uint64_t firstBlock;
    uint32_t blockSize;
    uint64_t totalBlocks;
    uint32_t blocksDone = 0;
    uint8_t* ptr = (uint8_t*)data;
    size_t totalTransferred = 0;
    XDeviceUsbTransferResult result = XDeviceUsbTransferResult_Ok;
    if (transferred) *transferred = 0;
    if (!device || !device->m_interfaceClaimed || device->m_blockSize == 0)
        return XDeviceUsbTransferResult_InvalidArgument;
    if ((byteOffset % device->m_blockSize) != 0 ||
        (byteCount % device->m_blockSize) != 0)
        return XDeviceUsbTransferResult_InvalidArgument;
    blockSize = device->m_blockSize;
    firstBlock = byteOffset / blockSize;
    totalBlocks = byteCount / blockSize;
    if (totalBlocks > UINT32_MAX) return XDeviceUsbTransferResult_InvalidArgument;
    if (device->m_blockCount != 0 &&
        (firstBlock >= device->m_blockCount ||
         totalBlocks > (uint64_t)(device->m_blockCount - firstBlock)))
        return XDeviceUsbTransferResult_InvalidArgument;
    while (blocksDone < (uint32_t)totalBlocks) {
        uint32_t chunk = (uint32_t)totalBlocks - blocksDone;
        size_t chunkBytes;
        size_t chunkTransferred = 0;
        if (chunk > 65535u) chunk = 65535u;
        chunkBytes = (size_t)chunk * blockSize;
        result = XDeviceUsbMsc_readBlocks(device, firstBlock + blocksDone,
                                           chunk, ptr + totalTransferred,
                                           chunkBytes, &chunkTransferred,
                                           timeoutMs);
        totalTransferred += chunkTransferred;
        if (result != XDeviceUsbTransferResult_Ok) break;
        blocksDone += chunk;
    }
    if (transferred) *transferred = totalTransferred;
    return result;
}

XDeviceUsbTransferResult XDeviceUsbMsc_writeBytes(
    XDeviceUsbMscDevice* device, uint64_t byteOffset,
    const void* data, size_t byteCount, size_t* transferred, int32_t timeoutMs)
{
    uint64_t firstBlock;
    uint32_t blockSize;
    uint64_t totalBlocks;
    uint32_t blocksDone = 0;
    const uint8_t* ptr = (const uint8_t*)data;
    size_t totalTransferred = 0;
    XDeviceUsbTransferResult result = XDeviceUsbTransferResult_Ok;
    if (transferred) *transferred = 0;
    if (!device || !device->m_interfaceClaimed || device->m_blockSize == 0)
        return XDeviceUsbTransferResult_InvalidArgument;
    if ((byteOffset % device->m_blockSize) != 0 ||
        (byteCount % device->m_blockSize) != 0)
        return XDeviceUsbTransferResult_InvalidArgument;
    blockSize = device->m_blockSize;
    firstBlock = byteOffset / blockSize;
    totalBlocks = byteCount / blockSize;
    if (totalBlocks > UINT32_MAX) return XDeviceUsbTransferResult_InvalidArgument;
    if (device->m_blockCount != 0 &&
        (firstBlock >= device->m_blockCount ||
         totalBlocks > (uint64_t)(device->m_blockCount - firstBlock)))
        return XDeviceUsbTransferResult_InvalidArgument;
    while (blocksDone < (uint32_t)totalBlocks) {
        uint32_t chunk = (uint32_t)totalBlocks - blocksDone;
        size_t chunkBytes;
        size_t chunkTransferred = 0;
        if (chunk > 65535u) chunk = 65535u;
        chunkBytes = (size_t)chunk * blockSize;
        result = XDeviceUsbMsc_writeBlocks(device, firstBlock + blocksDone,
                                            chunk, ptr + totalTransferred,
                                            chunkBytes, &chunkTransferred,
                                            timeoutMs);
        totalTransferred += chunkTransferred;
        if (result != XDeviceUsbTransferResult_Ok) break;
        blocksDone += chunk;
    }
    if (transferred) *transferred = totalTransferred;
    return result;
}