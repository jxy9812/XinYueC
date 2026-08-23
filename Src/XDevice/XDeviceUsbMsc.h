/**
 * @file       XDeviceUsbMsc.h
 * @brief      USB Mass Storage Bulk-Only Transport（BOT）协议适配。
 * @details    该模块建立在 XDeviceUsbHost 的原始控制/批量传输之上，识别
 *             MSC BOT 接口并提供 SCSI Inquiry、容量查询以及块读写。它不
 *             接管操作系统存储栈，也不把 Windows PhysicalDrive 暴露给应用。
 */
#ifndef XDEVICEUSBMSC_H
#define XDEVICEUSBMSC_H

#ifdef __cplusplus
extern "C" {
#endif

#include "XDeviceUsbHost.h"

/** @brief MSC BOT 命令完成状态，来自 Command Status Wrapper。 */
typedef enum XDeviceUsbMscCommandStatus
{
    XDeviceUsbMscCommandStatus_Passed = 0,
    XDeviceUsbMscCommandStatus_Failed = 1,
    XDeviceUsbMscCommandStatus_PhaseError = 2,
    XDeviceUsbMscCommandStatus_Invalid = 3
} XDeviceUsbMscCommandStatus;

/** @brief 被识别的 MSC BOT 接口和端点。 */
typedef struct XDeviceUsbMscDevice
{
    XFd m_hostFd;                         /**< 所属 XDeviceUsbHost 句柄。 */
    uint8_t m_interfaceNumber;            /**< MSC 接口编号。 */
    uint8_t m_alternateSetting;           /**< 已选择的备用设置。 */
    XDeviceUsbEndpointAddress m_bulkIn;   /**< CSW/数据 IN 端点。 */
    XDeviceUsbEndpointAddress m_bulkOut;  /**< CBW/数据 OUT 端点。 */
    uint16_t m_bulkInPacketSize;          /**< IN 端点最大包长。 */
    uint16_t m_bulkOutPacketSize;         /**< OUT 端点最大包长。 */
    uint8_t m_lun;                        /**< 当前使用的逻辑单元号。 */
    uint32_t m_tag;                       /**< 下一个 BOT 命令标签。 */
    uint32_t m_blockSize;                 /**< 逻辑块大小，未知时为 0。 */
    uint64_t m_blockCount;                /**< 逻辑块数量，未知时为 0。 */
    bool m_interfaceClaimed;              /**< 是否由此对象 Claim。 */
} XDeviceUsbMscDevice;

/** @brief 初始化 MSC 会话；不会访问设备。 */
void XDeviceUsbMsc_init(XDeviceUsbMscDevice* device);

/**
 * @brief 从当前配置描述符中查找并 Claim 一个 MSC BOT 接口。
 * @details 只接受接口类 08h、子类 06h、协议 50h，并要求一个 Bulk IN
 *          与一个 Bulk OUT 端点。成功后会切换到接口所在配置和备用设置。
 * @param hostFd 已打开的 XDeviceUsbHost 设备句柄。
 * @param device 输出会话；不能为 NULL，调用前应由 XDeviceUsbMsc_init 初始化。
 * @param timeoutMs 描述符和控制请求超时；0 使用后端默认值，负数不限时。
 * @return 成功返回 true；未找到、Claim 失败或描述符无效返回 false。
 */
bool XDeviceUsbMsc_discover(XFd hostFd, XDeviceUsbMscDevice* device,
                            int32_t timeoutMs);

/** @brief 释放 MSC 接口 Claim；不会关闭 hostFd。 */
void XDeviceUsbMsc_close(XDeviceUsbMscDevice* device);

/** @brief 设置 BOT 命令使用的逻辑单元号。 */
bool XDeviceUsbMsc_setLun(XDeviceUsbMscDevice* device, uint8_t lun);

/**
 * @brief 执行一个 SCSI 命令的 BOT 传输。
 * @param cdb SCSI 命令块，长度为 1 到 16 字节。
 * @param cdbLength SCSI 命令块长度。
 * @param data 数据阶段缓冲区；IN 为输出，OUT 为输入，无数据可为 NULL。
 * @param dataLength 数据阶段长度。
 * @param dataIn true 表示设备到主机，false 表示主机到设备。
 * @param transferred 输出数据阶段实际字节数，可为 NULL。
 * @param status 输出 CSW 状态，可为 NULL。
 * @param timeoutMs 每个 BOT 阶段的超时。
 * @return USB 传输结果；SCSI 命令拒绝本身返回 Ok，并通过 status 报告。
 */
XDeviceUsbTransferResult XDeviceUsbMsc_execute(
    XDeviceUsbMscDevice* device, const uint8_t* cdb, uint8_t cdbLength,
    void* data, size_t dataLength, bool dataIn, size_t* transferred,
    XDeviceUsbMscCommandStatus* status, int32_t timeoutMs);

/** @brief 执行 SCSI INQUIRY，返回标准 36 字节基本响应。 */
XDeviceUsbTransferResult XDeviceUsbMsc_inquiry(
    XDeviceUsbMscDevice* device, uint8_t inquiryData[36], int32_t timeoutMs);

/**
 * @brief 读取介质容量并更新会话中的块参数。
 * @details 普通设备使用 READ CAPACITY(10)，大于 2 TiB 的设备自动使用
 *          READ CAPACITY(16)。
 */
XDeviceUsbTransferResult XDeviceUsbMsc_readCapacity(
    XDeviceUsbMscDevice* device, uint32_t* blockSize, uint64_t* blockCount,
    int32_t timeoutMs);

/** @brief 使用 READ(10/16) 读取逻辑块。要求已知块大小。 */
XDeviceUsbTransferResult XDeviceUsbMsc_readBlocks(
    XDeviceUsbMscDevice* device, uint64_t firstBlock, uint32_t blockCount,
    void* data, size_t capacity, size_t* transferred, int32_t timeoutMs);

/** @brief 使用 WRITE(10/16) 写入逻辑块。要求已知块大小。 */
XDeviceUsbTransferResult XDeviceUsbMsc_writeBlocks(
    XDeviceUsbMscDevice* device, uint64_t firstBlock, uint32_t blockCount,
    const void* data, size_t length, size_t* transferred, int32_t timeoutMs);

/** @brief 执行 BOT Reset Recovery（Mass Storage Reset + 清除两个 Bulk Halt）。 */
bool XDeviceUsbMsc_resetRecovery(XDeviceUsbMscDevice* device, int32_t timeoutMs);

/** @brief 执行 TEST UNIT READY，设备就绪返回 Ok，否则通过 status 报告。 */
XDeviceUsbTransferResult XDeviceUsbMsc_testUnitReady(
    XDeviceUsbMscDevice* device, XDeviceUsbMscCommandStatus* status,
    int32_t timeoutMs);

/**
 * @brief 执行 REQUEST SENSE，读取固定格式感知数据（18 字节）。
 * @param senseData 至少 18 字节缓冲区，输出固定格式 SENSE 数据。
 * @param senseKey 输出 Sense Key，可为 NULL。
 * @param asc 输出 Additional Sense Code，可为 NULL。
 * @param ascq 输出 Additional Sense Code Qualifier，可为 NULL。
 */
XDeviceUsbTransferResult XDeviceUsbMsc_requestSense(
    XDeviceUsbMscDevice* device, uint8_t senseData[18],
    uint8_t* senseKey, uint8_t* asc, uint8_t* ascq, int32_t timeoutMs);

/** @brief PREVENT ALLOW MEDIUM REMOVAL，prevent 为 true 锁定介质。 */
XDeviceUsbTransferResult XDeviceUsbMsc_preventAllowRemoval(
    XDeviceUsbMscDevice* device, bool prevent, int32_t timeoutMs);

/**
 * @brief START STOP UNIT。
 * @param start true = 启动，false = 停止。
 * @param loadEject true = 加载/弹出（与 start 组合使用）。
 */
XDeviceUsbTransferResult XDeviceUsbMsc_startStopUnit(
    XDeviceUsbMscDevice* device, bool start, bool loadEject, int32_t timeoutMs);

/**
 * @brief 按字节偏移读取（基于块大小自动计算块地址和分块读取）。
 * @details 要求已通过 readCapacity 获知块大小。跨块读取会自动拆分为
 *          多次 READ(10/16)，单次最多 65535 块。
 */
XDeviceUsbTransferResult XDeviceUsbMsc_readBytes(
    XDeviceUsbMscDevice* device, uint64_t byteOffset,
    void* data, size_t byteCount, size_t* transferred, int32_t timeoutMs);

/**
 * @brief 按字节偏移写入（基于块大小自动计算块地址和分块写入）。
 * @details 要求已通过 readCapacity 获知块大小。偏移和长度必须是块大小
 *          的整数倍（不支持部分块写入）。
 */
XDeviceUsbTransferResult XDeviceUsbMsc_writeBytes(
    XDeviceUsbMscDevice* device, uint64_t byteOffset,
    const void* data, size_t byteCount, size_t* transferred, int32_t timeoutMs);

#ifdef __cplusplus
}
#endif

#endif /* XDEVICEUSBMSC_H */
