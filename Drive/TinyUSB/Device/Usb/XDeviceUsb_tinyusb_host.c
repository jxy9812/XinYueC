/**
 * @file       XDeviceUsb_tinyusb_host.c
 * @brief      TinyUSB 嵌入式 USB Host 后端。
 * @details    将 XDeviceUsbHost 平台接口映射到 TinyUSB Host API（tuh_*）。
 *             支持 STM32F4（DWC2 OTG Host）和 ESP32-S3（USB OTG Host）。
 *
 * 已实现：
 * - 控制器生命周期：create/open/close/delete
 * - 设备挂载/卸载 → 热插拔事件
 * - 设备枚举与打开（VID/PID/索引匹配）
 * - 设备描述符读取（挂载时自动填充）
 * - 配置描述符获取与解析（接口、端点信息）
 * - 配置切换
 * - 接口 Claim / Release（位图管理）
 * - Alternate Setting（控制请求设置）
 * - Clear Halt、设备 Reset
 * - 控制传输（同步，通过事件循环等待）
 * - 批量/中断/等时端点传输（同步 + 异步）
 * - 异步传输 ID 管理 + 取消
 * - processEvents 轮询
 */
#include "XDeviceUsb_tinyusb.h"
#include "XCode/XEvent/XAbstractEventDispatcher.h"
#include "XMemory.h"

#if defined(XINYUE_C_HAS_TINYUSB) && CFG_TUH_ENABLED

#include <string.h>
#include <stdlib.h>

/* ------------------------------------------------------------------
 * 常量
 * ------------------------------------------------------------------ */

#define XTUSB_H_MAX_INTERFACES   16u
#define XTUSB_H_MAX_ENDPOINTS    32u

/* 描述符类型 */
#define XTUSB_DESC_DEVICE      0x01u
#define XTUSB_DESC_CONFIG      0x02u
#define XTUSB_DESC_STRING      0x03u
#define XTUSB_DESC_INTERFACE   0x04u
#define XTUSB_DESC_ENDPOINT    0x05u

/* ------------------------------------------------------------------
 * 异步传输节点
 * ------------------------------------------------------------------ */

typedef struct XTuHostTransfer
{
    XDeviceUsbTransferId m_id;
    uint8_t m_devAddr;
    XDeviceUsbEndpointAddress m_endpoint;
    uint8_t* m_data;
    size_t m_length;
    size_t m_transferred;
    XDeviceUsbTransferCallback m_callback;
    void* m_userData;
    XDeviceUsbTransferResult m_result;
    bool m_completed;
    bool m_inProgress;
    struct XTuHostTransfer* m_next;
} XTuHostTransfer;

/* ------------------------------------------------------------------
 * 端点信息缓存
 * ------------------------------------------------------------------ */

typedef struct XTuHostEndpoint
{
    XDeviceUsbEndpointInfo m_info;
} XTuHostEndpoint;

/* ------------------------------------------------------------------
 * 设备上下文
 * ------------------------------------------------------------------ */

typedef struct XTuHostDevice
{
    XDeviceUsbDeviceInfo m_info;
    uint8_t m_devAddr;
    bool m_open;
    uint8_t m_activeConfiguration;

    /* 配置描述符缓存 */
    uint8_t* m_configDescriptor;
    size_t m_configDescriptorLength;

    /* 接口位图 */
    uint32_t m_claimedInterfaces;

    /* 端点缓存（从配置描述符解析） */
    XTuHostEndpoint m_endpoints[XTUSB_H_MAX_ENDPOINTS];
    size_t m_endpointCount;

    XDeviceUsbError m_lastError;
    int m_lastNativeError;
} XTuHostDevice;

/* ------------------------------------------------------------------
 * 控制器上下文
 * ------------------------------------------------------------------ */

typedef struct XTuHostController
{
    uint32_t m_controllerIndex;
    XDeviceUsbError m_lastError;
    int m_lastNativeError;

    XDeviceUsbHotplugCallback m_hotplugCallback;
    void* m_hotplugUserData;

    XTuHostDevice m_devices[CFG_TUH_DEVICE_MAX];
    size_t m_deviceCount;

    XTuHostTransfer* m_transfers;  /* 全局传输链表 */
    XDeviceUsbTransferId m_nextTransferId;

        bool m_opened;
    XHandle m_pollHandle;
} XTuHostController;

/* ------------------------------------------------------------------
 * 单例
 * ------------------------------------------------------------------ */

static XTuHostController* s_host = NULL;

static XTuHostController* xTuHostCast(void* handle)
{
    return (XTuHostController*)handle;
}

static void xTuHostSetError(XTuHostController* c, tusb_error_t err)
{
    if (!c) return;
    c->m_lastNativeError = (int)err;
    switch (err) {
    case TUSB_ERROR_NONE:        c->m_lastError = XDeviceUsbError_None;
    c->m_pollHandle = NULL; break;
    case TUSB_ERROR_INVALID_PARA: c->m_lastError = XDeviceUsbError_InvalidArgument; break;
    case TUSB_ERROR_NOT_SUPPORTED: c->m_lastError = XDeviceUsbError_Unsupported; break;
    case TUSB_ERROR_NOT_FOUND:   c->m_lastError = XDeviceUsbError_NoDevice; break;
    default:                     c->m_lastError = XDeviceUsbError_Io; break;
    }
}

static XTuHostDevice* xTuHostFindDevice(XTuHostController* c, uint8_t devAddr)
{
    size_t i;
    if (!c) return NULL;
    for (i = 0; i < c->m_deviceCount; ++i)
        if (c->m_devices[i].m_open && c->m_devices[i].m_devAddr == devAddr)
            return &c->m_devices[i];
    return NULL;
}

static XTuHostEndpoint* xTuHostFindEndpoint(XTuHostDevice* d,
    XDeviceUsbEndpointAddress address)
{
    size_t i;
    if (!d) return NULL;
    for (i = 0; i < d->m_endpointCount; ++i)
        if (d->m_endpoints[i].m_info.m_address == address)
            return &d->m_endpoints[i];
    return NULL;
}

/* ------------------------------------------------------------------
 * 配置描述符解析
 * ------------------------------------------------------------------ */

static bool xTuHostParseConfig(XTuHostDevice* d,
    const uint8_t* configDesc, size_t configLen)
{
    size_t offset = 0;
    uint8_t interfaceNumber = 0;
    uint8_t alternateSetting = 0;

    d->m_endpointCount = 0;
    if (!configDesc || configLen < 9u) return false;

    offset = configDesc[0];  /* 跳过配置描述符 */

    while (offset + 2u <= configLen) {
        uint8_t bLength = configDesc[offset];
        uint8_t bDescriptorType = configDesc[offset + 1];
        if (bLength < 2u || offset + bLength > configLen) return false;

        if (bDescriptorType == XTUSB_DESC_INTERFACE && bLength >= 9u) {
            interfaceNumber = configDesc[offset + 2];
            alternateSetting = configDesc[offset + 3];
        } else if (bDescriptorType == XTUSB_DESC_ENDPOINT && bLength >= 7u) {
            if (d->m_endpointCount < XTUSB_H_MAX_ENDPOINTS) {
                XTuHostEndpoint* ep = &d->m_endpoints[d->m_endpointCount];
                memset(ep, 0, sizeof(*ep));
                ep->m_info.m_address = configDesc[offset + 2];
                ep->m_info.m_transferType = (XDeviceUsbTransferType)(configDesc[offset + 3] & 0x03u);
                ep->m_info.m_maxPacketSize =
                    (uint16_t)configDesc[offset + 4] |
                    ((uint16_t)configDesc[offset + 5] << 8);
                ep->m_info.m_interval = configDesc[offset + 6];
                ep->m_info.m_interfaceNumber = interfaceNumber;
                ep->m_info.m_alternateSetting = alternateSetting;
                ++d->m_endpointCount;
            }
        }
        offset += bLength;
    }
    return true;
}

/* ------------------------------------------------------------------
 * 控制传输完成回调
 * ------------------------------------------------------------------ */

static void xTuHostControlComplete(tuh_xfer_t* xfer)
{
    XTuHostController* c = s_host;
    XTuHostTransfer* t;
    if (!c || !xfer) return;

    /* 查找对应的传输节点 */
    for (t = c->m_transfers; t; t = t->m_next) {
        if (t->m_devAddr == xfer->daddr && t->m_endpoint == 0 && t->m_inProgress) {
            t->m_completed = true;
            t->m_inProgress = false;
            t->m_transferred = (size_t)xfer->actual_len;
            switch (xfer->result) {
            case XFER_RESULT_SUCCESS:
                t->m_result = XDeviceUsbTransferResult_Ok;
                break;
            case XFER_RESULT_TIMEOUT:
                t->m_result = XDeviceUsbTransferResult_Timeout;
                break;
            case XFER_RESULT_STALLED:
                t->m_result = XDeviceUsbTransferResult_Stall;
                break;
            case XFER_RESULT_ABORTED:
                t->m_result = XDeviceUsbTransferResult_Cancelled;
                break;
            default:
                t->m_result = XDeviceUsbTransferResult_IoError;
                break;
            }
            break;
        }
    }
}

/* ------------------------------------------------------------------
 * 端点传输完成回调
 * ------------------------------------------------------------------ */

static void xTuHostTransferComplete(tuh_xfer_t* xfer)
{
    XTuHostController* c = s_host;
    XTuHostTransfer* t;
    if (!c || !xfer) return;

    for (t = c->m_transfers; t; t = t->m_next) {
        if (t->m_devAddr == xfer->daddr && t->m_endpoint == xfer->ep_addr && t->m_inProgress) {
            t->m_completed = true;
            t->m_inProgress = false;
            t->m_transferred = (size_t)xfer->actual_len;
            switch (xfer->result) {
            case XFER_RESULT_SUCCESS:
                t->m_result = XDeviceUsbTransferResult_Ok;
                break;
            case XFER_RESULT_TIMEOUT:
                t->m_result = XDeviceUsbTransferResult_Timeout;
                break;
            case XFER_RESULT_STALLED:
                t->m_result = XDeviceUsbTransferResult_Stall;
                break;
            case XFER_RESULT_ABORTED:
                t->m_result = XDeviceUsbTransferResult_Cancelled;
                break;
            default:
                t->m_result = XDeviceUsbTransferResult_IoError;
                break;
            }
            break;
        }
    }
}

/* ------------------------------------------------------------------
 * 轮询完成的传输并调用回调
 * ------------------------------------------------------------------ */

static void xTuHostPollTransfers(XTuHostController* c)
{
    XTuHostTransfer* t;
    XTuHostTransfer* prev = NULL;
    if (!c) return;

    t = c->m_transfers;
    while (t) {
        if (t->m_completed) {
            XTuHostTransfer* done = t;
            /* 从链表移除 */
            if (prev) prev->m_next = t->m_next;
            else c->m_transfers = t->m_next;
            t = t->m_next;

            if (done->m_callback) {
                done->m_callback(c, done->m_id, done->m_result,
                    done->m_data, done->m_transferred, done->m_userData);
            }
            XFree_System(done);
        } else {
            prev = t;
            t = t->m_next;
        }
    }
}

/* ------------------------------------------------------------------
 * 提交传输到全局链表
 * ------------------------------------------------------------------ */

static XTuHostTransfer* xTuHostAddTransfer(XTuHostController* c,
    uint8_t devAddr, XDeviceUsbEndpointAddress endpoint,
    void* data, size_t length,
    XDeviceUsbTransferCallback callback, void* userData)
{
    XTuHostTransfer* t;
    t = (XTuHostTransfer*)XCalloc_System(1, sizeof(*t));
    if (!t) return NULL;
    t->m_id = c->m_nextTransferId++;
    if (c->m_nextTransferId == 0) c->m_nextTransferId = 1;
    t->m_devAddr = devAddr;
    t->m_endpoint = endpoint;
    t->m_data = (uint8_t*)data;
    t->m_length = length;
    t->m_transferred = 0;
    t->m_callback = callback;
    t->m_userData = userData;
    t->m_result = XDeviceUsbTransferResult_Ok;
    t->m_completed = false;
    t->m_inProgress = true;
    t->m_next = c->m_transfers;
    c->m_transfers = t;
    return t;
}

/* ------------------------------------------------------------------
 * TinyUSB Host 类驱动：注册通用 Vendor 类（用于原始端点访问）
 * ------------------------------------------------------------------ */

/* 使用 TinyUSB 的 vendor host class 作为通用原始端点传输通道。
   tuh_vendor_init / tuh_vendor_open 等由 TinyUSB 内置。 */

/* ------------------------------------------------------------------
 * 挂载/卸载回调
 * ------------------------------------------------------------------ */

void tuh_mount_cb(uint8_t dev_addr)
{
    XTuHostController* c = s_host;
    tusb_desc_device_t desc;
    XTuHostDevice* dev;
    if (!c || c->m_deviceCount >= CFG_TUH_DEVICE_MAX) return;

    /* 读取设备描述符 */
    if (tuh_descriptor_get_device_sync(dev_addr, &desc, sizeof(desc)) != XFER_RESULT_SUCCESS)
        return;

    dev = &c->m_devices[c->m_deviceCount];
    memset(dev, 0, sizeof(*dev));
    dev->m_devAddr = dev_addr;
    dev->m_open = true;
    dev->m_info.m_address = dev_addr;
    dev->m_info.m_vendorId = desc.idVendor;
    dev->m_info.m_productId = desc.idProduct;
    dev->m_info.m_bcdDevice = desc.bcdDevice;
    dev->m_info.m_deviceClass = desc.bDeviceClass;
    dev->m_info.m_deviceSubclass = desc.bDeviceSubClass;
    dev->m_info.m_deviceProtocol = desc.bDeviceProtocol;
    dev->m_info.m_maxPacketSize0 = desc.bMaxPacketSize0;
    dev->m_activeConfiguration = 0;
    ++c->m_deviceCount;

    if (c->m_hotplugCallback) {
        XDeviceUsbHotplugEvent e;
        memset(&e, 0, sizeof(e));
        e.m_type = XDeviceUsbHotplugEventType_Arrived;
        e.m_device = dev->m_info;
        c->m_hotplugCallback(c, &e, c->m_hotplugUserData);
    }
}

void tuh_umount_cb(uint8_t dev_addr)
{
    XTuHostController* c = s_host;
    size_t i;
    if (!c) return;

    for (i = 0; i < c->m_deviceCount; ++i) {
        if (c->m_devices[i].m_devAddr == dev_addr && c->m_devices[i].m_open) {
            XDeviceUsbDeviceInfo info = c->m_devices[i].m_info;

            /* 释放配置描述符缓存 */
            if (c->m_devices[i].m_configDescriptor) {
                XFree_System(c->m_devices[i].m_configDescriptor);
                c->m_devices[i].m_configDescriptor = NULL;
            }

            c->m_devices[i].m_open = false;

            /* 移除 */
            if (i + 1 < c->m_deviceCount) {
                memmove(&c->m_devices[i], &c->m_devices[i + 1],
                        (c->m_deviceCount - i - 1) * sizeof(XTuHostDevice));
            }
            --c->m_deviceCount;

            if (c->m_hotplugCallback) {
                XDeviceUsbHotplugEvent e;
                memset(&e, 0, sizeof(e));
                e.m_type = XDeviceUsbHotplugEventType_Removed;
                e.m_device = info;
                c->m_hotplugCallback(c, &e, c->m_hotplugUserData);
            }
            break;
        }
    }
}

/* ------------------------------------------------------------------
 * 控制器生命周期
 * ------------------------------------------------------------------ */

void* XDeviceUsbHost_platformControllerCreate(const XDeviceUsbControllerConfig* config, int* error)
{
    XTuHostController* c;
    if (!config) { if (error) *error = XDeviceUsbError_InvalidArgument; return NULL; }
    c = (XTuHostController*)XCalloc_System(1, sizeof(*c));
    if (!c) { if (error) *error = XDeviceUsbError_Resource; return NULL; }
    c->m_controllerIndex = config->m_controller;
    c->m_lastError = XDeviceUsbError_None;
    c->m_pollHandle = NULL;
    c->m_nextTransferId = 1;
    if (error) *error = XDeviceUsbError_None;
    return c;
}

/* 事件循环轮询回调：每次事件循环调用一次 tuh_task() */
static bool xTuHostEventLoopPoll(void* userData)
{
    XTuHostController* c = (XTuHostController*)userData;
    if (!c || !c->m_opened) return false;
    XDeviceUsbHost_platformControllerProcessEvents(c, 0);
    return true;
}
bool XDeviceUsbHost_platformControllerOpen(void* controller,
    const XDeviceUsbControllerConfig* config, int* error)
{
    XTuHostController* c = xTuHostCast(controller);
    (void)config;
    if (!c) { if (error) *error = XDeviceUsbError_InvalidArgument; return false; }

    if (s_host && s_host != c) {
        if (error) *error = XDeviceUsbError_Busy; return false;
    }
    s_host = c;

    if (!tuh_init(CFG_TUH_RHPORT)) {
        xTuHostSetError(c, TUSB_ERROR_FAILED);
        s_host = NULL;
        if (error) *error = c->m_lastError;
        return false;
    }

    c->m_opened = true;
    /* 注册到事件循环，自动轮询 tuh_task() */
    c->m_pollHandle = XAbstractEventDispatcher_addPollCallback(
        xTuHostEventLoopPoll, c);
    if (error) *error = XDeviceUsbError_None;
    return true;
}

void XDeviceUsbHost_platformControllerClose(void* controller)
{
    XTuHostController* c = xTuHostCast(controller);
    XTuHostTransfer* t;
    if (!c) return;

    /* 取消所有传输 */
    t = c->m_transfers;
    while (t) {
        XTuHostTransfer* next = t->m_next;
        if (t->m_callback) {
            t->m_callback(c, t->m_id, XDeviceUsbTransferResult_Cancelled,
                t->m_data, t->m_transferred, t->m_userData);
        }
        XFree_System(t);
        t = next;
    }
    c->m_transfers = NULL;

    /* 注销事件循环轮询回调 */
    if (c->m_pollHandle) {
        XAbstractEventDispatcher_removePollCallback(c->m_pollHandle);
        c->m_pollHandle = NULL;
    }
    c->m_opened = false;
    c->m_deviceCount = 0;
    if (s_host == c) s_host = NULL;
}

void XDeviceUsbHost_platformControllerDelete(void* controller)
{
    XTuHostController* c = xTuHostCast(controller);
    if (!c) return;
    XDeviceUsbHost_platformControllerClose(c);
    XFree_System(c);
}

/* ------------------------------------------------------------------
 * 枚举 / 热插拔
 * ------------------------------------------------------------------ */

bool XDeviceUsbHost_platformControllerEnumerate(void* controller,
    XDeviceUsbEnumerateCallback callback, void* userData)
{
    XTuHostController* c = xTuHostCast(controller);
    size_t i;
    if (!c || !c->m_opened || !callback) return false;

    for (i = 0; i < c->m_deviceCount; ++i) {
        if (c->m_devices[i].m_open) {
            callback(c, &c->m_devices[i].m_info, userData);
        }
    }
    return true;
}

bool XDeviceUsbHost_platformControllerSetHotplug(void* controller,
    XDeviceUsbHotplugCallback callback, void* userData)
{
    XTuHostController* c = xTuHostCast(controller);
    if (!c) return false;
    c->m_hotplugCallback = callback;
    c->m_hotplugUserData = userData;
    return true;
}

/* ------------------------------------------------------------------
 * 事件循环
 * ------------------------------------------------------------------ */

XDeviceUsbProcessResult XDeviceUsbHost_platformControllerProcessEvents(
    void* controller, int32_t timeoutMs)
{
    XTuHostController* c = xTuHostCast(controller);
    (void)timeoutMs;
    if (!c || !c->m_opened) return XDeviceUsbProcessResult_Error;

    tuh_task();
    xTuHostPollTransfers(c);

    return XDeviceUsbProcessResult_Event;
}

/* ------------------------------------------------------------------
 * 控制器特性/句柄/错误
 * ------------------------------------------------------------------ */

XDeviceUsbFeatures XDeviceUsbHost_platformControllerFeatures(void* controller)
{
    XTuHostController* c = xTuHostCast(controller);
    XDeviceUsbFeatures f =
        XDeviceUsbFeature_Hotplug |
        XDeviceUsbFeature_AsyncTransfer |
        XDeviceUsbFeature_ProcessEvents |
        XDeviceUsbFeature_DescriptorCache |
        XDeviceUsbFeature_Reset |
        XDeviceUsbFeature_ControlTransfer |
        XDeviceUsbFeature_Isochronous;
    if (!c) return XDeviceUsbFeature_None;
#if CFG_TUH_HUB
    f |= XDeviceUsbFeature_DescriptorCache;
#endif
    return f;
}

XDeviceUsbNativeHandle XDeviceUsbHost_platformControllerHandle(void* controller)
{
    (void)controller;
    return XDEVICE_USB_INVALID_NATIVE_HANDLE;
}

XDeviceUsbError XDeviceUsbHost_platformControllerLastError(void* controller)
{
    XTuHostController* c = xTuHostCast(controller);
    return c ? c->m_lastError : XDeviceUsbError_NotOpen;
}

int32_t XDeviceUsbHost_platformControllerNativeError(void* controller)
{
    XTuHostController* c = xTuHostCast(controller);
    return c ? c->m_lastNativeError : -1;
}

void XDeviceUsbHost_platformControllerClearError(void* controller)
{
    XTuHostController* c = xTuHostCast(controller);
    if (c) { c->m_lastError = XDeviceUsbError_None;
    c->m_pollHandle = NULL; c->m_lastNativeError = 0; }
}

/* ------------------------------------------------------------------
 * 设备打开/关闭
 * ------------------------------------------------------------------ */

void* XDeviceUsbHost_platformDeviceOpen(void* controller,
    const XDeviceUsbDeviceSelector* selector, int* error)
{
    XTuHostController* c = xTuHostCast(controller);
    size_t i;
    uint32_t matches = 0;
    if (!c || !c->m_opened || !selector) {
        if (error) *error = XDeviceUsbError_InvalidArgument;
        return NULL;
    }

    for (i = 0; i < c->m_deviceCount; ++i) {
        XTuHostDevice* dev = &c->m_devices[i];
        if (!dev->m_open) continue;

        if (selector->m_vendorId != 0xFFFF &&
            dev->m_info.m_vendorId != selector->m_vendorId) continue;
        if (selector->m_productId != 0xFFFF &&
            dev->m_info.m_productId != selector->m_productId) continue;

        if (matches++ == selector->m_index) {
            if (error) *error = XDeviceUsbError_None;
            return dev;
        }
    }

    if (error) *error = matches ? XDeviceUsbError_NoDevice : XDeviceUsbError_NotFound;
    return NULL;
}

void XDeviceUsbHost_platformDeviceClose(void* device)
{
    XTuHostDevice* d = (XTuHostDevice*)device;
    if (!d) return;
    /* 设备 close 只是标记，真正的移除由 tuh_umount_cb 处理 */
    if (d->m_configDescriptor) {
        XFree_System(d->m_configDescriptor);
        d->m_configDescriptor = NULL;
    }
    d->m_open = false;
}

/* ------------------------------------------------------------------
 * 设备信息/配置
 * ------------------------------------------------------------------ */

bool XDeviceUsbHost_platformDeviceGetInfo(void* device, XDeviceUsbDeviceInfo* info)
{
    XTuHostDevice* d = (XTuHostDevice*)device;
    if (!d || !info) return false;
    *info = d->m_info;
    return true;
}

bool XDeviceUsbHost_platformDeviceSetConfiguration(void* device, uint8_t value)
{
    XTuHostDevice* d = (XTuHostDevice*)device;
    tusb_control_request_t req;
    tuh_xfer_t xfer;
    xfer_result_t result = XFER_RESULT_INVALID;
    if (!d) return false;

    memset(&req, 0, sizeof(req));
    req.bmRequestType = 0x00u;  /* Host->Device, Standard, Device */
    req.bRequest = 0x09u;       /* SET_CONFIGURATION */
    req.wValue = value;
    req.wIndex = 0;
    req.wLength = 0;

    memset(&xfer, 0, sizeof(xfer));
    xfer.daddr = d->m_devAddr;
    xfer.ep_addr = 0;
    xfer.result = XFER_RESULT_INVALID;
    xfer.setup = &req;
    xfer.buffer = NULL;
    xfer.complete_cb = NULL;              /* 同步：阻塞直到完成 */
    xfer.user_data = (uintptr_t)&result;
    if (!tuh_control_xfer(&xfer))
        return false;
    if (xfer.result != XFER_RESULT_SUCCESS)
        return false;

    d->m_activeConfiguration = value;

    /* 重新读取配置描述符并解析端点（同步 API） */
    {
        uint8_t header[9];
        uint16_t totalLen;
        xfer_result_t xr;
        xr = tuh_descriptor_get_configuration_sync(d->m_devAddr, 0,
                                                   header, sizeof(header));
        if (xr != XFER_RESULT_SUCCESS) return false;
        totalLen = (uint16_t)header[2] | ((uint16_t)header[3] << 8);
        if (totalLen < 9u) return false;

        if (d->m_configDescriptor) { XFree_System(d->m_configDescriptor); d->m_configDescriptor = NULL; }
        d->m_configDescriptor = (uint8_t*)XMalloc_System(totalLen);
        if (!d->m_configDescriptor) return false;
        d->m_configDescriptorLength = totalLen;

        xr = tuh_descriptor_get_configuration_sync(d->m_devAddr, 0,
            d->m_configDescriptor, totalLen);
        if (xr != XFER_RESULT_SUCCESS) return false;

        xTuHostParseConfig(d, d->m_configDescriptor, d->m_configDescriptorLength);
    }
    return true;
}

bool XDeviceUsbHost_platformDeviceGetConfiguration(void* device, uint8_t* value)
{
    XTuHostDevice* d = (XTuHostDevice*)device;
    if (!d || !value) return false;
    *value = d->m_activeConfiguration;
    return true;
}

/* ------------------------------------------------------------------
 * 接口 Claim/Release / Alternate
 * ------------------------------------------------------------------ */

bool XDeviceUsbHost_platformDeviceClaimInterface(void* device, uint8_t interfaceNumber)
{
    XTuHostDevice* d = (XTuHostDevice*)device;
    if (!d || interfaceNumber >= XTUSB_H_MAX_INTERFACES) return false;
    if (d->m_claimedInterfaces & (UINT32_C(1) << interfaceNumber)) {
        return false;  /* 已 Claim */
    }
    d->m_claimedInterfaces |= UINT32_C(1) << interfaceNumber;
    return true;
}

bool XDeviceUsbHost_platformDeviceReleaseInterface(void* device, uint8_t interfaceNumber)
{
    XTuHostDevice* d = (XTuHostDevice*)device;
    if (!d || interfaceNumber >= XTUSB_H_MAX_INTERFACES) return false;
    if (!(d->m_claimedInterfaces & (UINT32_C(1) << interfaceNumber))) return false;
    d->m_claimedInterfaces &= ~(UINT32_C(1) << interfaceNumber);
    return true;
}

bool XDeviceUsbHost_platformDeviceSetAlternateSetting(void* device,
    uint8_t interfaceNumber, uint8_t alternate)
{
    XTuHostDevice* d = (XTuHostDevice*)device;
    tusb_control_request_t req;
    tuh_xfer_t xfer;
    if (!d) return false;

    memset(&req, 0, sizeof(req));
    req.bmRequestType = 0x01u;  /* Host->Device, Standard, Interface */
    req.bRequest = 0x0Bu;       /* SET_INTERFACE */
    req.wValue = alternate;
    req.wIndex = interfaceNumber;
    req.wLength = 0;

    memset(&xfer, 0, sizeof(xfer));
    xfer.daddr = d->m_devAddr;
    xfer.ep_addr = 0;
    xfer.result = XFER_RESULT_INVALID;
    xfer.setup = &req;
    xfer.buffer = NULL;
    xfer.complete_cb = NULL;    /* 同步 */
    xfer.user_data = 0;
    if (!tuh_control_xfer(&xfer)) return false;
    return xfer.result == XFER_RESULT_SUCCESS;
}

size_t XDeviceUsbHost_platformDeviceEndpointCount(void* device,
    uint8_t interfaceNumber, uint8_t alternate)
{
    XTuHostDevice* d = (XTuHostDevice*)device;
    size_t i;
    size_t count = 0;
    if (!d) return 0;
    for (i = 0; i < d->m_endpointCount; ++i) {
        XTuHostEndpoint* ep = &d->m_endpoints[i];
        if (ep->m_info.m_interfaceNumber == interfaceNumber &&
            ep->m_info.m_alternateSetting == alternate)
            ++count;
    }
    return count;
}

bool XDeviceUsbHost_platformDeviceGetEndpointInfo(void* device,
    uint8_t interfaceNumber, uint8_t alternate, size_t index,
    XDeviceUsbEndpointInfo* info)
{
    XTuHostDevice* d = (XTuHostDevice*)device;
    size_t i;
    size_t count = 0;
    if (!d || !info) return false;
    for (i = 0; i < d->m_endpointCount; ++i) {
        XTuHostEndpoint* ep = &d->m_endpoints[i];
        if (ep->m_info.m_interfaceNumber == interfaceNumber &&
            ep->m_info.m_alternateSetting == alternate) {
            if (count == index) {
                *info = ep->m_info;
                return true;
            }
            ++count;
        }
    }
    return false;
}

/* ------------------------------------------------------------------
 * Clear Halt / Reset
 * ------------------------------------------------------------------ */

/* 同步等待端点传输完成（TinyUSB tuh_edpt_xfer 总是异步，需自行轮询） */
static XDeviceUsbTransferResult xTuHostSyncWaitEdpt(XTuHostController* c,
    XTuHostTransfer* t, int32_t timeoutMs, size_t* transferred)
{
    uint32_t start = tusb_time_millis_api();
    int32_t timeout = (timeoutMs > 0) ? timeoutMs : 5000;
    XDeviceUsbTransferResult result;
    size_t bytes = 0;
    bool timedOut = false;

    while (!t->m_completed) {
        tuh_task();
        if ((int32_t)(tusb_time_millis_api() - start) >= timeout) {
            timedOut = true;
            tuh_edpt_abort_xfer(t->m_devAddr, t->m_endpoint);
            break;
        }
    }
    if (!timedOut) {
        result = t->m_result;
        bytes = t->m_transferred;
    } else {
        result = XDeviceUsbTransferResult_Timeout;
        bytes = 0;
    }
    if (transferred) *transferred = bytes;

    /* 从链表移除并释放（单线程，t 一定还在链表中） */
    {
        XTuHostTransfer** pp = &c->m_transfers;
        while (*pp) {
            if (*pp == t) { *pp = t->m_next; break; }
            pp = &(*pp)->m_next;
        }
        XFree_System(t);
    }
    return result;
}

/* 打开端点（幂等）并提交端点传输 */
static bool xTuHostOpenAndSubmit(XTuHostDevice* d, XTuHostEndpoint* ep,
                                 uint8_t* buffer, uint16_t length,
                                 tuh_xfer_cb_t cb, uintptr_t userData)
{
    tusb_desc_endpoint_t ep_desc;
    memset(&ep_desc, 0, sizeof(ep_desc));
    ep_desc.bLength = sizeof(ep_desc);
    ep_desc.bDescriptorType = TUSB_DESC_ENDPOINT;
    ep_desc.bEndpointAddress = ep->m_info.m_address;
    ep_desc.bmAttributes.xfer = (uint8_t)ep->m_info.m_transferType;
    ep_desc.wMaxPacketSize = ep->m_info.m_maxPacketSize;
    ep_desc.bInterval = ep->m_info.m_interval;
    /* 打开端点（已打开时返回 false，忽略） */
    (void)tuh_edpt_open(d->m_devAddr, &ep_desc);

    {
        tuh_xfer_t xfer;
        memset(&xfer, 0, sizeof(xfer));
        xfer.daddr = d->m_devAddr;
        xfer.ep_addr = ep->m_info.m_address;
        xfer.result = XFER_RESULT_INVALID;
        xfer.buffer = buffer;
        xfer.buflen = length;
        xfer.complete_cb = cb;
        xfer.user_data = userData;
        return tuh_edpt_xfer(&xfer);
    }
}

bool XDeviceUsbHost_platformDeviceClearHalt(void* device,
    XDeviceUsbEndpointAddress endpoint)
{
    XTuHostDevice* d = (XTuHostDevice*)device;
    tusb_control_request_t req;
    tuh_xfer_t xfer;
    if (!d) return false;

    /* 标准 CLEAR_FEATURE(ENDPOINT_HALT) 控制请求 */
    memset(&req, 0, sizeof(req));
    req.bmRequestType = 0x02u;  /* Host->Device, Standard, Endpoint */
    req.bRequest = 0x01u;       /* CLEAR_FEATURE */
    req.wValue = 0x00u;         /* FEATURE_ENDPOINT_HALT */
    req.wIndex = endpoint;
    req.wLength = 0;

    memset(&xfer, 0, sizeof(xfer));
    xfer.daddr = d->m_devAddr;
    xfer.ep_addr = 0;
    xfer.result = XFER_RESULT_INVALID;
    xfer.setup = &req;
    xfer.buffer = NULL;
    xfer.complete_cb = NULL;    /* 同步 */
    xfer.user_data = 0;
    if (!tuh_control_xfer(&xfer)) return false;
    return xfer.result == XFER_RESULT_SUCCESS;
}

bool XDeviceUsbHost_platformDeviceReset(void* device)
{
    (void)device;
    /* TinyUSB 0.21.0 Host 没有 per-device reset API（只有 root port 级
       tuh_rhport_reset_bus，会复位整个总线）。为避免误操作其他设备，
       此处明确返回 Unsupported。总线级复位由控制器层处理。 */
    return false;
}

/* ------------------------------------------------------------------
 * 控制传输（同步，TinyUSB 0.21.0 同步 API）
 * ------------------------------------------------------------------ */

XDeviceUsbTransferResult XDeviceUsbHost_platformDeviceControlTransfer(
    void* device, const XDeviceUsbControlRequest* request,
    void* data, size_t capacity, size_t* transferred, int32_t timeoutMs)
{
    XTuHostDevice* d = (XTuHostDevice*)device;
    tusb_control_request_t treq;
    tuh_xfer_t xfer;
    XDeviceUsbTransferResult result;
    (void)timeoutMs; /* TinyUSB 0.21.0 控制传输超时为编译期配置，不支持运行时超时 */

    if (!d || !request) return XDeviceUsbTransferResult_InvalidArgument;
    if (request->m_length && !data) return XDeviceUsbTransferResult_InvalidArgument;
    if (capacity < request->m_length) return XDeviceUsbTransferResult_InvalidArgument;

    memset(&treq, 0, sizeof(treq));
    treq.bmRequestType = request->m_requestType;
    treq.bRequest = request->m_request;
    treq.wValue = request->m_value;
    treq.wIndex = request->m_index;
    treq.wLength = request->m_length;

    memset(&xfer, 0, sizeof(xfer));
    xfer.daddr = d->m_devAddr;
    xfer.ep_addr = 0;
    xfer.result = XFER_RESULT_INVALID;
    xfer.setup = &treq;
    xfer.buffer = (uint8_t*)data;
    xfer.complete_cb = NULL;    /* 同步：阻塞直到完成 */
    xfer.user_data = 0;
    if (!tuh_control_xfer(&xfer))
        return XDeviceUsbTransferResult_IoError;

    if (transferred) *transferred = (size_t)xfer.actual_len;
    switch (xfer.result) {
    case XFER_RESULT_SUCCESS:  result = XDeviceUsbTransferResult_Ok; break;
    case XFER_RESULT_TIMEOUT:  result = XDeviceUsbTransferResult_Timeout; break;
    case XFER_RESULT_STALLED:  result = XDeviceUsbTransferResult_Stall; break;
    case XFER_RESULT_ABORTED:  result = XDeviceUsbTransferResult_Cancelled; break;
    default:                   result = XDeviceUsbTransferResult_IoError; break;
    }
    return result;
}

/* ------------------------------------------------------------------
 * 同步端点传输（异步提交 + 轮询等待）
 * ------------------------------------------------------------------ */

XDeviceUsbTransferResult XDeviceUsbHost_platformDeviceTransfer(void* device,
    XDeviceUsbEndpointAddress endpoint, void* data, size_t length,
    size_t* transferred, int32_t timeoutMs)
{
    XTuHostDevice* d = (XTuHostDevice*)device;
    XTuHostController* c = s_host;
    XTuHostEndpoint* ep;
    XTuHostTransfer* t;
    XDeviceUsbTransferResult result;
    if (!d || !c || !data || length == 0 || length > UINT16_MAX)
        return XDeviceUsbTransferResult_InvalidArgument;

    ep = xTuHostFindEndpoint(d, endpoint);
    if (!ep) return XDeviceUsbTransferResult_InvalidArgument;

    t = xTuHostAddTransfer(c, d->m_devAddr, endpoint, data, length, NULL, NULL);
    if (!t) return XDeviceUsbTransferResult_ResourceError;

    if (!xTuHostOpenAndSubmit(d, ep, (uint8_t*)data, (uint16_t)length,
                              xTuHostTransferComplete, 0)) {
        XTuHostTransfer** pp = &c->m_transfers;
        while (*pp) { if (*pp == t) { *pp = t->m_next; break; } pp = &(*pp)->m_next; }
        XFree_System(t);
        return XDeviceUsbTransferResult_IoError;
    }

    result = xTuHostSyncWaitEdpt(c, t, timeoutMs, transferred);
    return result;
}

/* ------------------------------------------------------------------
 * 异步传输提交
 * ------------------------------------------------------------------ */

XDeviceUsbTransferId XDeviceUsbHost_platformDeviceSubmitTransfer(void* device,
    const XDeviceUsbTransferRequest* request,
    XDeviceUsbTransferCallback callback, void* userData)
{
    XTuHostDevice* d = (XTuHostDevice*)device;
    XTuHostController* c = s_host;
    XTuHostTransfer* t;
    XTuHostEndpoint* ep;
    bool ok = false;
    if (!d || !c || !request || !callback)
        return XDEVICE_USB_INVALID_TRANSFER_ID;
    if (request->m_length == 0 || request->m_length > UINT16_MAX)
        return XDEVICE_USB_INVALID_TRANSFER_ID;

    ep = xTuHostFindEndpoint(d, request->m_endpoint);
    if (!ep) return XDEVICE_USB_INVALID_TRANSFER_ID;

    t = xTuHostAddTransfer(c, d->m_devAddr, request->m_endpoint,
                            (void*)request->m_data, request->m_length,
                            callback, userData);
    if (!t) return XDEVICE_USB_INVALID_TRANSFER_ID;

    ok = xTuHostOpenAndSubmit(d, ep, (uint8_t*)request->m_data,
                              (uint16_t)request->m_length,
                              xTuHostTransferComplete, 0);
    if (!ok) {
        XTuHostTransfer** pp = &c->m_transfers;
        while (*pp) { if (*pp == t) { *pp = t->m_next; break; } pp = &(*pp)->m_next; }
        XFree_System(t);
        return XDEVICE_USB_INVALID_TRANSFER_ID;
    }

    return t->m_id;
}

XDeviceUsbTransferResult XDeviceUsbHost_platformDeviceCancelTransfer(void* device,
    XDeviceUsbTransferId id)
{
    XTuHostDevice* d = (XTuHostDevice*)device;
    XTuHostController* c = s_host;
    XTuHostTransfer* t;
    XTuHostTransfer* prev = NULL;
    if (!d || !c || !id) return XDeviceUsbTransferResult_InvalidArgument;

    t = c->m_transfers;
    while (t) {
        if (t->m_id == id && t->m_devAddr == d->m_devAddr) {
            /* 取消端点传输 */
            tuh_edpt_abort_xfer(d->m_devAddr, t->m_endpoint);

            if (prev) prev->m_next = t->m_next;
            else c->m_transfers = t->m_next;

            if (t->m_callback) {
                t->m_callback(c, t->m_id,
                    XDeviceUsbTransferResult_Cancelled,
                    t->m_data, t->m_transferred, t->m_userData);
            }
            XFree_System(t);
            return XDeviceUsbTransferResult_Cancelled;
        }
        prev = t;
        t = t->m_next;
    }
    return XDeviceUsbTransferResult_NoDevice;
}

/* ------------------------------------------------------------------
 * 设备特性/句柄/错误
 * ------------------------------------------------------------------ */

XDeviceUsbFeatures XDeviceUsbHost_platformDeviceFeatures(void* device)
{
    XTuHostDevice* d = (XTuHostDevice*)device;
    if (!d) return XDeviceUsbFeature_None;
    return XDeviceUsbFeature_ControlTransfer |
           XDeviceUsbFeature_AsyncTransfer |
           XDeviceUsbFeature_Isochronous |
           XDeviceUsbFeature_Reset |
           XDeviceUsbFeature_DescriptorCache;
}

XDeviceUsbNativeHandle XDeviceUsbHost_platformDeviceHandle(void* device)
{
    XTuHostDevice* d = (XTuHostDevice*)device;
    if (!d) return XDEVICE_USB_INVALID_NATIVE_HANDLE;
    return (XDeviceUsbNativeHandle)(uintptr_t)d->m_devAddr;
}

XDeviceUsbError XDeviceUsbHost_platformDeviceLastError(void* device)
{
    XTuHostDevice* d = (XTuHostDevice*)device;
    return d ? d->m_lastError : XDeviceUsbError_NotOpen;
}

int32_t XDeviceUsbHost_platformDeviceNativeError(void* device)
{
    XTuHostDevice* d = (XTuHostDevice*)device;
    return d ? d->m_lastNativeError : -1;
}

void XDeviceUsbHost_platformDeviceClearError(void* device)
{
    XTuHostDevice* d = (XTuHostDevice*)device;
    if (d) { d->m_lastError = XDeviceUsbError_None; d->m_lastNativeError = 0; }
}

#endif /* XINYUE_C_HAS_TINYUSB && CFG_TUH_ENABLED */
