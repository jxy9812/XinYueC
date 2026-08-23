/**
 * @file       XDeviceUsb_posix.c
 * @brief      POSIX USB Host 后端。
 * @details    检测到 libusb-1.0 时提供真实 Host 实现；否则使用统一存根。
 */
#include "XDeviceUsbHost.h"
#include "XDeviceUsbGadget.h"

#if defined(__linux__) || defined(__APPLE__) || defined(__BSD__) || \
    defined(__FreeBSD__) || defined(__NetBSD__) || defined(__OpenBSD__)

#if defined(XINYUE_C_HAS_LIBUSB)

#include <libusb-1.0/libusb.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>

typedef struct XPosixUsbController XPosixUsbController;
typedef struct XPosixUsbDevice XPosixUsbDevice;
typedef struct XPosixUsbTransfer XPosixUsbTransfer;
typedef struct XPosixUsbSnapshot XPosixUsbSnapshot;

struct XPosixUsbSnapshot { XDeviceUsbDeviceInfo m_info; };

struct XPosixUsbController {
    libusb_context* m_context;
    uint32_t m_controller;
    int m_lastNativeError;
    XDeviceUsbError m_lastError;
    libusb_hotplug_callback_handle m_hotplugHandle;
    bool m_hotplugRegistered;
    volatile int m_nativeChanged;
    XDeviceUsbHotplugCallback m_hotplugCallback;
    void* m_hotplugUserData;
    XPosixUsbSnapshot* m_snapshot;
    size_t m_snapshotCount;
};

struct XPosixUsbDevice {
    XPosixUsbController* m_controller;
    libusb_device* m_device;
    libusb_device_handle* m_handle;
    struct libusb_device_descriptor m_descriptor;
    struct libusb_config_descriptor* m_configuration;
    int m_activeConfiguration;
    uint32_t m_claimedInterfaces;
    uint8_t m_alternate[32];
    int m_lastNativeError;
    XDeviceUsbError m_lastError;
    XPosixUsbTransfer* m_transfers;
    XDeviceUsbTransferId m_nextTransferId;
};

struct XPosixUsbTransfer {
    XPosixUsbDevice* m_device;
    struct libusb_transfer* m_native;
    XDeviceUsbTransferId m_id;
    XDeviceUsbTransferCallback m_callback;
    void* m_userData;
    XPosixUsbTransfer* m_next;
};

static void xusbSetControllerError(XPosixUsbController* c, int e)
{
    if (!c) return;
    c->m_lastNativeError = e;
    switch (e) {
    case LIBUSB_SUCCESS: c->m_lastError = XDeviceUsbError_None; break;
    case LIBUSB_ERROR_INVALID_PARAM: c->m_lastError = XDeviceUsbError_InvalidArgument; break;
    case LIBUSB_ERROR_NO_DEVICE: case LIBUSB_ERROR_NOT_FOUND: c->m_lastError = XDeviceUsbError_NoDevice; break;
    case LIBUSB_ERROR_ACCESS: c->m_lastError = XDeviceUsbError_PermissionDenied; break;
    case LIBUSB_ERROR_BUSY: c->m_lastError = XDeviceUsbError_Busy; break;
    case LIBUSB_ERROR_TIMEOUT: c->m_lastError = XDeviceUsbError_Timeout; break;
    case LIBUSB_ERROR_NO_MEM: c->m_lastError = XDeviceUsbError_Resource; break;
    case LIBUSB_ERROR_INTERRUPTED: c->m_lastError = XDeviceUsbError_Interrupted; break;
    case LIBUSB_ERROR_NOT_SUPPORTED: c->m_lastError = XDeviceUsbError_Unsupported; break;
    case LIBUSB_ERROR_PIPE: c->m_lastError = XDeviceUsbError_Stall; break;
    default: c->m_lastError = XDeviceUsbError_Io; break;
    }
}

static void xusbSetDeviceError(XPosixUsbDevice* d, int e)
{
    if (!d) return;
    d->m_lastNativeError = e;
    switch (e) {
    case LIBUSB_SUCCESS: d->m_lastError = XDeviceUsbError_None; break;
    case LIBUSB_ERROR_INVALID_PARAM: d->m_lastError = XDeviceUsbError_InvalidArgument; break;
    case LIBUSB_ERROR_NO_DEVICE: d->m_lastError = XDeviceUsbError_Disconnected; break;
    case LIBUSB_ERROR_ACCESS: d->m_lastError = XDeviceUsbError_PermissionDenied; break;
    case LIBUSB_ERROR_BUSY: d->m_lastError = XDeviceUsbError_Busy; break;
    case LIBUSB_ERROR_TIMEOUT: d->m_lastError = XDeviceUsbError_Timeout; break;
    case LIBUSB_ERROR_NO_MEM: d->m_lastError = XDeviceUsbError_Resource; break;
    case LIBUSB_ERROR_INTERRUPTED: d->m_lastError = XDeviceUsbError_Interrupted; break;
    case LIBUSB_ERROR_NOT_SUPPORTED: d->m_lastError = XDeviceUsbError_Unsupported; break;
    case LIBUSB_ERROR_PIPE: d->m_lastError = XDeviceUsbError_Stall; break;
    default: d->m_lastError = XDeviceUsbError_Io; break;
    }
}

static XDeviceUsbTransferResult xusbMapError(int e)
{
    switch (e) {
    case LIBUSB_SUCCESS: return XDeviceUsbTransferResult_Ok;
    case LIBUSB_ERROR_TIMEOUT: return XDeviceUsbTransferResult_Timeout;
    case LIBUSB_ERROR_PIPE: return XDeviceUsbTransferResult_Stall;
    case LIBUSB_ERROR_OVERFLOW: return XDeviceUsbTransferResult_Overflow;
    case LIBUSB_ERROR_NO_DEVICE: return XDeviceUsbTransferResult_NoDevice;
    case LIBUSB_ERROR_BUSY: return XDeviceUsbTransferResult_Busy;
    case LIBUSB_ERROR_ACCESS: return XDeviceUsbTransferResult_PermissionDenied;
    case LIBUSB_ERROR_NO_MEM: return XDeviceUsbTransferResult_ResourceError;
    case LIBUSB_ERROR_INTERRUPTED: return XDeviceUsbTransferResult_Interrupted;
    case LIBUSB_ERROR_NOT_SUPPORTED: return XDeviceUsbTransferResult_Unsupported;
    case LIBUSB_ERROR_INVALID_PARAM: return XDeviceUsbTransferResult_InvalidArgument;
    default: return XDeviceUsbTransferResult_IoError;
    }
}

static XDeviceUsbTransferResult xusbMapStatus(enum libusb_transfer_status status)
{
    switch (status) {
    case LIBUSB_TRANSFER_COMPLETED: return XDeviceUsbTransferResult_Ok;
    case LIBUSB_TRANSFER_TIMED_OUT: return XDeviceUsbTransferResult_Timeout;
    case LIBUSB_TRANSFER_CANCELLED: return XDeviceUsbTransferResult_Cancelled;
    case LIBUSB_TRANSFER_STALL: return XDeviceUsbTransferResult_Stall;
    case LIBUSB_TRANSFER_NO_DEVICE: return XDeviceUsbTransferResult_NoDevice;
    case LIBUSB_TRANSFER_OVERFLOW: return XDeviceUsbTransferResult_Overflow;
    default: return XDeviceUsbTransferResult_IoError;
    }
}

static XDeviceUsbSpeed xusbSpeed(int speed)
{
    switch (speed) {
    case LIBUSB_SPEED_LOW: return XDeviceUsbSpeed_Low;
    case LIBUSB_SPEED_FULL: return XDeviceUsbSpeed_Full;
    case LIBUSB_SPEED_HIGH: return XDeviceUsbSpeed_High;
    case LIBUSB_SPEED_SUPER: return XDeviceUsbSpeed_Super;
#ifdef LIBUSB_SPEED_SUPER_PLUS
    case LIBUSB_SPEED_SUPER_PLUS: return XDeviceUsbSpeed_SuperPlus;
#endif
    default: return XDeviceUsbSpeed_Unknown;
    }
}

static void xusbFillInfo(libusb_device* device, uint32_t controller, XDeviceUsbDeviceInfo* info)
{
    struct libusb_device_descriptor d;
    memset(info, 0, sizeof(*info));
    info->m_controller = controller;
    info->m_bus = libusb_get_bus_number(device);
    info->m_address = libusb_get_device_address(device);
    info->m_backendId = (uint64_t)(uintptr_t)device;
    if (libusb_get_device_descriptor(device, &d) != LIBUSB_SUCCESS) return;
    info->m_vendorId = d.idVendor; info->m_productId = d.idProduct;
    info->m_bcdDevice = d.bcdDevice; info->m_deviceClass = d.bDeviceClass;
    info->m_deviceSubClass = d.bDeviceSubClass; info->m_deviceProtocol = d.bDeviceProtocol;
    info->m_configurationCount = d.bNumConfigurations;
    info->m_speed = xusbSpeed(libusb_get_device_speed(device));
}

static bool xusbMatch(libusb_device* device, const XDeviceUsbDeviceSelector* s)
{
    struct libusb_device_descriptor d;
    if (libusb_get_device_descriptor(device, &d) != LIBUSB_SUCCESS) return false;
    return (!s->m_vendorId || s->m_vendorId == d.idVendor) &&
           (!s->m_productId || s->m_productId == d.idProduct) &&
           (!s->m_bcdDevice || s->m_bcdDevice == d.bcdDevice) &&
           (s->m_deviceClass == 0xffu || s->m_deviceClass == d.bDeviceClass) &&
           (s->m_deviceSubClass == 0xffu || s->m_deviceSubClass == d.bDeviceSubClass) &&
           (s->m_deviceProtocol == 0xffu || s->m_deviceProtocol == d.bDeviceProtocol);
}

static bool xusbMatchSerial(libusb_device_handle* handle, uint8_t index, const char* wanted)
{
    unsigned char buffer[256];
    int length;
    if (!wanted) return true;
    if (!handle || !index) return false;
    length = libusb_get_string_descriptor_ascii(handle, index, buffer, sizeof(buffer));
    return length >= 0 && strlen(wanted) == (size_t)length && memcmp(wanted, buffer, (size_t)length) == 0;
}

static bool xusbRefreshConfig(XPosixUsbDevice* d)
{
    struct libusb_config_descriptor* config = NULL;
    int result;
    if (d->m_configuration) libusb_free_config_descriptor(d->m_configuration);
    d->m_configuration = NULL;
    if (!d->m_activeConfiguration) { memset(d->m_alternate, 0, sizeof(d->m_alternate)); return true; }
    result = libusb_get_config_descriptor_by_value(d->m_device,
                                                   (uint8_t)d->m_activeConfiguration, &config);
    if (result != LIBUSB_SUCCESS) { xusbSetDeviceError(d, result); return false; }
    d->m_configuration = config;
    memset(d->m_alternate, 0, sizeof(d->m_alternate));
    return true;
}

static const struct libusb_interface_descriptor* xusbInterface(XPosixUsbDevice* d,
                                                               uint8_t number, uint8_t alternate)
{
    int i, j;
    if (!d || !d->m_configuration) return NULL;
    for (i = 0; i < d->m_configuration->bNumInterfaces; ++i)
        for (j = 0; j < d->m_configuration->interface[i].num_altsetting; ++j) {
            const struct libusb_interface_descriptor* setting =
                &d->m_configuration->interface[i].altsetting[j];
            if (setting->bInterfaceNumber == number && setting->bAlternateSetting == alternate)
                return setting;
        }
    return NULL;
}

static const struct libusb_endpoint_descriptor* xusbEndpoint(XPosixUsbDevice* d,
                                                              uint8_t number, uint8_t alternate,
                                                              size_t index)
{
    const struct libusb_interface_descriptor* setting = xusbInterface(d, number, alternate);
    return setting && index < setting->bNumEndpoints ? &setting->endpoint[index] : NULL;
}

static const struct libusb_endpoint_descriptor* xusbFindEndpoint(XPosixUsbDevice* d,
                                                                 uint8_t address,
                                                                 uint8_t* interfaceNumber)
{
    int i, j, k;
    if (!d || !d->m_configuration) return NULL;
    for (i = 0; i < d->m_configuration->bNumInterfaces; ++i)
        for (j = 0; j < d->m_configuration->interface[i].num_altsetting; ++j) {
            const struct libusb_interface_descriptor* setting =
                &d->m_configuration->interface[i].altsetting[j];
            if (setting->bAlternateSetting != d->m_alternate[setting->bInterfaceNumber]) continue;
            for (k = 0; k < setting->bNumEndpoints; ++k)
                if (setting->endpoint[k].bEndpointAddress == address) {
                    if (interfaceNumber) *interfaceNumber = setting->bInterfaceNumber;
                    return &setting->endpoint[k];
                }
        }
    return NULL;
}

static void xusbRemoveTransfer(XPosixUsbDevice* d, XPosixUsbTransfer* transfer)
{
    XPosixUsbTransfer** p = &d->m_transfers;
    while (*p && *p != transfer) p = &(*p)->m_next;
    if (*p == transfer) *p = transfer->m_next;
}

static void LIBUSB_CALL xusbAsyncCallback(struct libusb_transfer* native)
{
    XPosixUsbTransfer* transfer = (XPosixUsbTransfer*)native->user_data;
    XDeviceUsbTransferEvent event;
    XDeviceUsbTransferResult result = xusbMapStatus(native->status);
    size_t transferred = native->actual_length;
    int i;
    if (!transfer || !transfer->m_device) return;
    if (native->type == LIBUSB_TRANSFER_TYPE_ISOCHRONOUS) {
        transferred = 0; result = XDeviceUsbTransferResult_Ok;
        for (i = 0; i < native->num_iso_packets; ++i) {
            transferred += native->iso_packet_desc[i].actual_length;
            if (native->iso_packet_desc[i].status != LIBUSB_TRANSFER_COMPLETED)
                result = xusbMapStatus(native->iso_packet_desc[i].status);
        }
    }
    xusbRemoveTransfer(transfer->m_device, transfer);
    memset(&event, 0, sizeof(event));
    event.m_result = result; event.m_transferred = transferred;
    event.m_context = XDeviceUsbCallbackContext_Process;
    transfer->m_callback(transfer->m_device, transfer->m_id, &event, transfer->m_userData);
    libusb_free_transfer(native); free(transfer);
}

static int xusbSubmit(XPosixUsbDevice* d, const XDeviceUsbTransferRequest* request,
                      XDeviceUsbTransferCallback callback, void* userData,
                      XDeviceUsbTransferId* id)
{
    const struct libusb_endpoint_descriptor* endpoint;
    XPosixUsbTransfer* transfer;
    unsigned int timeout = request->m_timeoutMs > 0 ? (unsigned int)request->m_timeoutMs : 0u;
    int type;
    int packets = 0;
    int result;
    if (!d || !request || !callback || request->m_endpoint == 0 || !request->m_data ||
        request->m_length > INT_MAX || request->m_flags != 0u) return LIBUSB_ERROR_INVALID_PARAM;
    endpoint = xusbFindEndpoint(d, request->m_endpoint, NULL);
    if (!endpoint) return LIBUSB_ERROR_NOT_FOUND;
    type = endpoint->bmAttributes & 3u;
    if ((request->m_transferType == XDeviceUsbTransferType_Bulk && type != 2) ||
        (request->m_transferType == XDeviceUsbTransferType_Interrupt && type != 3) ||
        (request->m_transferType == XDeviceUsbTransferType_Isochronous && type != 1))
        return LIBUSB_ERROR_INVALID_PARAM;
    if (type == 1) {
        int packet = endpoint->wMaxPacketSize & 0x07ff;
        if (!packet) return LIBUSB_ERROR_INVALID_PARAM;
        packets = (int)((request->m_length + (size_t)packet - 1u) / (size_t)packet);
        if (packets <= 0 || packets > 1024) return LIBUSB_ERROR_INVALID_PARAM;
    }
    transfer = (XPosixUsbTransfer*)calloc(1, sizeof(*transfer));
    if (!transfer) return LIBUSB_ERROR_NO_MEM;
    transfer->m_native = libusb_alloc_transfer(packets);
    if (!transfer->m_native) { free(transfer); return LIBUSB_ERROR_NO_MEM; }
    transfer->m_device = d; transfer->m_callback = callback; transfer->m_userData = userData;
    transfer->m_id = d->m_nextTransferId++;
    if (transfer->m_id == XDEVICE_USB_INVALID_TRANSFER_ID) transfer->m_id = d->m_nextTransferId++;
    if (type == 2) libusb_fill_bulk_transfer(transfer->m_native, d->m_handle, request->m_endpoint,
        (unsigned char*)request->m_data, (int)request->m_length, xusbAsyncCallback, transfer, timeout);
    else if (type == 3) libusb_fill_interrupt_transfer(transfer->m_native, d->m_handle, request->m_endpoint,
        (unsigned char*)request->m_data, (int)request->m_length, xusbAsyncCallback, transfer, timeout);
    else {
        int packet = endpoint->wMaxPacketSize & 0x07ff;
        libusb_fill_iso_transfer(transfer->m_native, d->m_handle, request->m_endpoint,
            (unsigned char*)request->m_data, (int)request->m_length, packets,
            xusbAsyncCallback, transfer, timeout);
        libusb_set_iso_packet_lengths(transfer->m_native, (unsigned int)packet);
    }
    transfer->m_next = d->m_transfers; d->m_transfers = transfer;
    result = libusb_submit_transfer(transfer->m_native);
    if (result != LIBUSB_SUCCESS) {
        xusbRemoveTransfer(d, transfer); libusb_free_transfer(transfer->m_native); free(transfer);
        return result;
    }
    if (id) *id = transfer->m_id;
    return LIBUSB_SUCCESS;
}

typedef struct XPosixSync { bool done; XDeviceUsbTransferResult result; size_t transferred; } XPosixSync;
static void LIBUSB_CALL xusbSyncCallback(struct libusb_transfer* native)
{
    XPosixSync* sync = (XPosixSync*)native->user_data;
    int i;
    sync->done = true; sync->result = xusbMapStatus(native->status);
    sync->transferred = native->actual_length;
    if (native->type == LIBUSB_TRANSFER_TYPE_ISOCHRONOUS) {
        sync->transferred = 0; sync->result = XDeviceUsbTransferResult_Ok;
        for (i = 0; i < native->num_iso_packets; ++i) {
            sync->transferred += native->iso_packet_desc[i].actual_length;
            if (native->iso_packet_desc[i].status != LIBUSB_TRANSFER_COMPLETED)
                sync->result = xusbMapStatus(native->iso_packet_desc[i].status);
        }
    }
}

static XDeviceUsbTransferResult xusbSyncIso(XPosixUsbDevice* d, uint8_t endpoint,
                                            void* data, size_t length, int32_t timeoutMs,
                                            const struct libusb_endpoint_descriptor* descriptor,
                                            size_t* transferred)
{
    struct libusb_transfer* transfer;
    XPosixSync sync;
    int packet = descriptor->wMaxPacketSize & 0x07ff;
    int packets = packet ? (int)((length + packet - 1u) / packet) : 0;
    int result;
    if (packets <= 0 || packets > 1024) return XDeviceUsbTransferResult_InvalidArgument;
    transfer = libusb_alloc_transfer(packets);
    if (!transfer) return XDeviceUsbTransferResult_ResourceError;
    memset(&sync, 0, sizeof(sync));
    libusb_fill_iso_transfer(transfer, d->m_handle, endpoint, (unsigned char*)data, (int)length,
                             packets, xusbSyncCallback, &sync,
                             timeoutMs > 0 ? (unsigned int)timeoutMs : 0u);
    libusb_set_iso_packet_lengths(transfer, (unsigned int)packet);
    result = libusb_submit_transfer(transfer);
    while (result == LIBUSB_SUCCESS && !sync.done) {
        struct timeval timeout = { timeoutMs > 0 ? timeoutMs / 1000 : 1,
                                    timeoutMs > 0 ? (timeoutMs % 1000) * 1000 : 0 };
        result = libusb_handle_events_timeout(d->m_controller->m_context, &timeout);
    }
    if (result != LIBUSB_SUCCESS && !sync.done) {
        libusb_cancel_transfer(transfer);
        while (!sync.done) {
            struct timeval timeout = { 1, 0 };
            if (libusb_handle_events_timeout(d->m_controller->m_context, &timeout) != LIBUSB_SUCCESS) break;
        }
    }
    if (transferred) *transferred = sync.transferred;
    if (!sync.done) { xusbSetDeviceError(d, result); libusb_free_transfer(transfer); return xusbMapError(result); }
    result = sync.result == XDeviceUsbTransferResult_Ok ? LIBUSB_SUCCESS : LIBUSB_ERROR_IO;
    libusb_free_transfer(transfer);
    if (sync.result != XDeviceUsbTransferResult_Ok) xusbSetDeviceError(d, result);
    return sync.result;
}

static int LIBUSB_CALL xusbHotplug(libusb_context* context, libusb_device* device,
                                   libusb_hotplug_event event, void* userData)
{ XPosixUsbController* c = (XPosixUsbController*)userData; (void)context; (void)device; (void)event; if (c) c->m_nativeChanged = 1; return 0; }

static void xusbFreeSnapshot(XPosixUsbController* c) { if (c) { free(c->m_snapshot); c->m_snapshot = NULL; c->m_snapshotCount = 0; } }
static bool xusbBuildSnapshot(XPosixUsbController* c, XPosixUsbSnapshot** output, size_t* count)
{
    libusb_device** list = NULL; ssize_t length; size_t i; XPosixUsbSnapshot* snapshot;
    *output = NULL; *count = 0; length = libusb_get_device_list(c->m_context, &list);
    if (length < 0) { xusbSetControllerError(c, (int)length); return false; }
    snapshot = length ? (XPosixUsbSnapshot*)calloc((size_t)length, sizeof(*snapshot)) : NULL;
    if (length && !snapshot) { libusb_free_device_list(list, 1); xusbSetControllerError(c, LIBUSB_ERROR_NO_MEM); return false; }
    for (i = 0; i < (size_t)length; ++i) xusbFillInfo(list[i], c->m_controller, &snapshot[i].m_info);
    libusb_free_device_list(list, 1); *output = snapshot; *count = (size_t)length; return true;
}
static size_t xusbFindSnapshot(XPosixUsbSnapshot* list, size_t count, XDeviceUsbDeviceInfo* info)
{
    size_t i; for (i = 0; i < count; ++i) if (list[i].m_info.m_bus == info->m_bus && list[i].m_info.m_address == info->m_address) return i; return count;
}
static bool xusbSameInfo(XDeviceUsbDeviceInfo* a, XDeviceUsbDeviceInfo* b)
{ return a->m_bus == b->m_bus && a->m_address == b->m_address && a->m_vendorId == b->m_vendorId && a->m_productId == b->m_productId && a->m_bcdDevice == b->m_bcdDevice && a->m_speed == b->m_speed; }

void* XDeviceUsbHost_platformControllerCreate(const XDeviceUsbControllerConfig* config, int* error)
{
    XPosixUsbController* c;
    if (!config) { if (error) *error = XDeviceUsbError_InvalidArgument; return NULL; }
    c = (XPosixUsbController*)calloc(1, sizeof(*c)); if (!c) { if (error) *error = XDeviceUsbError_Resource; return NULL; }
    c->m_controller = config->m_controller; c->m_lastError = XDeviceUsbError_None; c->m_hotplugHandle = -1;
    if (error) *error = XDeviceUsbError_None; return c;
}
bool XDeviceUsbHost_platformControllerOpen(void* handle, const XDeviceUsbControllerConfig* config, int* error)
{ XPosixUsbController* c = (XPosixUsbController*)handle; int result; (void)config; if (!c) return false; result = libusb_init(&c->m_context); if (result != 0) { xusbSetControllerError(c, result); if (error) *error = c->m_lastError; return false; } if (error) *error = XDeviceUsbError_None; return true; }
void XDeviceUsbHost_platformControllerClose(void* handle)
{ XPosixUsbController* c = (XPosixUsbController*)handle; if (!c) return; if (c->m_hotplugRegistered) libusb_hotplug_deregister_callback(c->m_context, c->m_hotplugHandle); c->m_hotplugRegistered = false; xusbFreeSnapshot(c); if (c->m_context) libusb_exit(c->m_context); c->m_context = NULL; }
void XDeviceUsbHost_platformControllerDelete(void* handle) { free(handle); }
bool XDeviceUsbHost_platformControllerEnumerate(void* handle, XDeviceUsbEnumerateCallback callback, void* userData)
{
    XPosixUsbController* c = (XPosixUsbController*)handle; libusb_device** list = NULL; ssize_t length, i;
    if (!c || !callback) return false; length = libusb_get_device_list(c->m_context, &list); if (length < 0) { xusbSetControllerError(c, (int)length); return false; }
    for (i = 0; i < length; ++i) { XDeviceUsbDeviceInfo info; xusbFillInfo(list[i], c->m_controller, &info); callback(c, &info, userData); }
    libusb_free_device_list(list, 1); return true;
}
bool XDeviceUsbHost_platformControllerSetHotplug(void* handle, XDeviceUsbHotplugCallback callback, void* userData)
{
    XPosixUsbController* c = (XPosixUsbController*)handle; int result;
    if (!c || !c->m_context) return false; if (c->m_hotplugRegistered) libusb_hotplug_deregister_callback(c->m_context, c->m_hotplugHandle);
    c->m_hotplugRegistered = false; xusbFreeSnapshot(c); c->m_hotplugCallback = callback; c->m_hotplugUserData = userData; c->m_nativeChanged = 0;
    if (!callback) return true; if (!libusb_has_capability(LIBUSB_CAP_HAS_HOTPLUG)) { xusbSetControllerError(c, LIBUSB_ERROR_NOT_SUPPORTED); return false; }
    if (!xusbBuildSnapshot(c, &c->m_snapshot, &c->m_snapshotCount)) return false;
    result = libusb_hotplug_register_callback(c->m_context, LIBUSB_HOTPLUG_EVENT_DEVICE_ARRIVED | LIBUSB_HOTPLUG_EVENT_DEVICE_LEFT, 0, LIBUSB_HOTPLUG_MATCH_ANY, LIBUSB_HOTPLUG_MATCH_ANY, LIBUSB_HOTPLUG_MATCH_ANY, xusbHotplug, c, &c->m_hotplugHandle);
    if (result != LIBUSB_SUCCESS) { xusbFreeSnapshot(c); xusbSetControllerError(c, result); c->m_hotplugCallback = NULL; return false; }
    c->m_hotplugRegistered = true; return true;
}
XDeviceUsbProcessResult XDeviceUsbHost_platformControllerProcessEvents(void* handle, int32_t timeoutMs)
{
    XPosixUsbController* c = (XPosixUsbController*)handle; struct timeval timeout; int result, completed = 0; XPosixUsbSnapshot* current = NULL; size_t currentCount = 0, i; bool changed = false;
    if (!c || !c->m_context) return XDeviceUsbProcessResult_Error; if (timeoutMs < 0) timeoutMs = 100; if (timeoutMs > 100) timeoutMs = 100;
    timeout.tv_sec = timeoutMs / 1000; timeout.tv_usec = (timeoutMs % 1000) * 1000; result = libusb_handle_events_timeout_completed(c->m_context, &timeout, &completed); if (result != 0) { xusbSetControllerError(c, result); return XDeviceUsbProcessResult_Error; }
    if (!c->m_hotplugCallback) return completed ? XDeviceUsbProcessResult_Event : XDeviceUsbProcessResult_Timeout;
    if (!xusbBuildSnapshot(c, &current, &currentCount)) return XDeviceUsbProcessResult_Error;
    for (i = 0; i < c->m_snapshotCount; ++i) if (xusbFindSnapshot(current, currentCount, &c->m_snapshot[i].m_info) == currentCount) { XDeviceUsbHotplugEvent e; memset(&e, 0, sizeof(e)); e.m_type = XDeviceUsbHotplugEventType_Removed; e.m_device = c->m_snapshot[i].m_info; c->m_hotplugCallback(c, &e, c->m_hotplugUserData); changed = true; }
    for (i = 0; i < currentCount; ++i) { size_t old = xusbFindSnapshot(c->m_snapshot, c->m_snapshotCount, &current[i].m_info); if (old == c->m_snapshotCount || !xusbSameInfo(&c->m_snapshot[old].m_info, &current[i].m_info)) { XDeviceUsbHotplugEvent e; memset(&e, 0, sizeof(e)); e.m_type = old == c->m_snapshotCount ? XDeviceUsbHotplugEventType_Arrived : XDeviceUsbHotplugEventType_Changed; e.m_device = current[i].m_info; c->m_hotplugCallback(c, &e, c->m_hotplugUserData); changed = true; } }
    xusbFreeSnapshot(c); c->m_snapshot = current; c->m_snapshotCount = currentCount; c->m_nativeChanged = 0; return changed ? XDeviceUsbProcessResult_Event : XDeviceUsbProcessResult_Timeout;
}
XDeviceUsbFeatures XDeviceUsbHost_platformControllerFeatures(void* handle)
{ XPosixUsbController* c = (XPosixUsbController*)handle; XDeviceUsbFeatures f = XDeviceUsbFeature_ProcessEvents | XDeviceUsbFeature_AsyncTransfer; if (!c) return XDeviceUsbFeature_None; if (libusb_has_capability(LIBUSB_CAP_HAS_HOTPLUG)) f |= XDeviceUsbFeature_Hotplug; return f; }
XDeviceUsbNativeHandle XDeviceUsbHost_platformControllerHandle(void* handle) { (void)handle; return XDEVICE_USB_INVALID_NATIVE_HANDLE; }
XDeviceUsbError XDeviceUsbHost_platformControllerLastError(void* handle) { XPosixUsbController* c = (XPosixUsbController*)handle; return c ? c->m_lastError : XDeviceUsbError_NotOpen; }
int32_t XDeviceUsbHost_platformControllerNativeError(void* handle) { XPosixUsbController* c = (XPosixUsbController*)handle; return c ? c->m_lastNativeError : LIBUSB_ERROR_INVALID_PARAM; }
void XDeviceUsbHost_platformControllerClearError(void* handle) { if (handle) xusbSetControllerError((XPosixUsbController*)handle, LIBUSB_SUCCESS); }

void* XDeviceUsbHost_platformDeviceOpen(void* handle, const XDeviceUsbDeviceSelector* s, int* error)
{
    XPosixUsbController* c = (XPosixUsbController*)handle; libusb_device** list = NULL; ssize_t length, i; uint32_t matches = 0; int result = LIBUSB_ERROR_NOT_FOUND;
    if (!c || !s) { if (error) *error = XDeviceUsbError_InvalidArgument; return NULL; }
    length = libusb_get_device_list(c->m_context, &list); if (length < 0) { xusbSetControllerError(c, (int)length); if (error) *error = c->m_lastError; return NULL; }
    for (i = 0; i < length; ++i) {
        struct libusb_device_descriptor descriptor; libusb_device_handle* native = NULL; XPosixUsbDevice* d;
        if (!xusbMatch(list[i], s) || libusb_get_device_descriptor(list[i], &descriptor) != 0) continue;
        result = libusb_open(list[i], &native); if (result != 0) break;
        if (!xusbMatchSerial(native, descriptor.iSerialNumber, s->m_serialNumber_utf8)) { libusb_close(native); continue; }
        if (matches++ != s->m_index) { libusb_close(native); continue; }
        d = (XPosixUsbDevice*)calloc(1, sizeof(*d)); if (!d) { libusb_close(native); result = LIBUSB_ERROR_NO_MEM; break; }
        d->m_controller = c; d->m_device = libusb_ref_device(list[i]); d->m_handle = native; d->m_descriptor = descriptor; d->m_nextTransferId = 1; d->m_lastError = XDeviceUsbError_None;
#if defined(LIBUSB_API_VERSION) && LIBUSB_API_VERSION >= 0x01000102
        { int ar = libusb_set_auto_detach_kernel_driver(native, 1); (void)ar; }
#endif
        result = libusb_get_configuration(native, &d->m_activeConfiguration); if (result == 0) xusbRefreshConfig(d);
        libusb_free_device_list(list, 1); if (error) *error = XDeviceUsbError_None; return d;
    }
    libusb_free_device_list(list, 1); if (!matches) xusbSetControllerError(c, LIBUSB_ERROR_NOT_FOUND); if (error) *error = matches ? XDeviceUsbError_NoDevice : c->m_lastError; return NULL;
}
void XDeviceUsbHost_platformDeviceClose(void* handle)
{
    XPosixUsbDevice* d = (XPosixUsbDevice*)handle; if (!d) return;
    while (d->m_transfers) { XPosixUsbTransfer* t = d->m_transfers; libusb_cancel_transfer(t->m_native); { struct timeval tv = { 0, 10000 }; libusb_handle_events_timeout(d->m_controller->m_context, &tv); } }
    if (d->m_handle) { uint8_t i; for (i = 0; i < 32; ++i) if (d->m_claimedInterfaces & (UINT32_C(1) << i)) libusb_release_interface(d->m_handle, i); libusb_close(d->m_handle); }
    if (d->m_configuration) libusb_free_config_descriptor(d->m_configuration); if (d->m_device) libusb_unref_device(d->m_device); free(d);
}
bool XDeviceUsbHost_platformDeviceGetInfo(void* handle, XDeviceUsbDeviceInfo* info)
{ XPosixUsbDevice* d = (XPosixUsbDevice*)handle; if (!d || !info) return false; xusbFillInfo(d->m_device, d->m_controller->m_controller, info); info->m_activeConfiguration = (uint8_t)d->m_activeConfiguration; return true; }
bool XDeviceUsbHost_platformDeviceSetConfiguration(void* handle, uint8_t value)
{ XPosixUsbDevice* d = (XPosixUsbDevice*)handle; int result; uint8_t i; if (!d) return false; for (i = 0; i < 32; ++i) if (d->m_claimedInterfaces & (UINT32_C(1) << i)) { result = libusb_release_interface(d->m_handle, i); if (result != 0) { xusbSetDeviceError(d, result); return false; } } result = libusb_set_configuration(d->m_handle, value); if (result != 0) { xusbSetDeviceError(d, result); return false; } d->m_activeConfiguration = value; d->m_claimedInterfaces = 0; return xusbRefreshConfig(d); }
bool XDeviceUsbHost_platformDeviceGetConfiguration(void* handle, uint8_t* value)
{ XPosixUsbDevice* d = (XPosixUsbDevice*)handle; int result; if (!d || !value) return false; result = libusb_get_configuration(d->m_handle, &d->m_activeConfiguration); if (result != 0) { xusbSetDeviceError(d, result); return false; } *value = (uint8_t)d->m_activeConfiguration; return true; }
bool XDeviceUsbHost_platformDeviceClaimInterface(void* handle, uint8_t number)
{ XPosixUsbDevice* d = (XPosixUsbDevice*)handle; int result; if (!d || number >= 32 || !xusbInterface(d, number, d->m_alternate[number])) return false; if (d->m_claimedInterfaces & (UINT32_C(1) << number)) { xusbSetDeviceError(d, LIBUSB_ERROR_BUSY); return false; }
#if !defined(LIBUSB_API_VERSION) || LIBUSB_API_VERSION < 0x01000102
  #if defined(__linux__)
    { int dr = libusb_detach_kernel_driver(d->m_handle, number); (void)dr; }
  #endif
#endif
  result = libusb_claim_interface(d->m_handle, number); if (result != 0) { xusbSetDeviceError(d, result); return false; } d->m_claimedInterfaces |= UINT32_C(1) << number; return true; }
bool XDeviceUsbHost_platformDeviceReleaseInterface(void* handle, uint8_t number)
{ XPosixUsbDevice* d = (XPosixUsbDevice*)handle; int result; if (!d || number >= 32 || !(d->m_claimedInterfaces & (UINT32_C(1) << number))) return false; result = libusb_release_interface(d->m_handle, number); if (result != 0) { xusbSetDeviceError(d, result); return false; } d->m_claimedInterfaces &= ~(UINT32_C(1) << number); return true; }
bool XDeviceUsbHost_platformDeviceSetAlternateSetting(void* handle, uint8_t number, uint8_t alternate)
{ XPosixUsbDevice* d = (XPosixUsbDevice*)handle; int result; if (!d || number >= 32 || !xusbInterface(d, number, alternate)) return false; result = libusb_set_interface_alt_setting(d->m_handle, number, alternate); if (result != 0) { xusbSetDeviceError(d, result); return false; } d->m_alternate[number] = alternate; return true; }
size_t XDeviceUsbHost_platformDeviceEndpointCount(void* handle, uint8_t number, uint8_t alternate)
{ const struct libusb_interface_descriptor* s = xusbInterface((XPosixUsbDevice*)handle, number, alternate); return s ? s->bNumEndpoints : 0; }
bool XDeviceUsbHost_platformDeviceGetEndpointInfo(void* handle, uint8_t number, uint8_t alternate, size_t index, XDeviceUsbEndpointInfo* info)
{ const struct libusb_endpoint_descriptor* e = xusbEndpoint((XPosixUsbDevice*)handle, number, alternate, index); if (!e || !info) return false; memset(info, 0, sizeof(*info)); info->m_address = e->bEndpointAddress; info->m_transferType = (XDeviceUsbTransferType)(e->bmAttributes & 3u); info->m_maxPacketSize = e->wMaxPacketSize; info->m_interval = e->bInterval; info->m_interfaceNumber = number; info->m_alternateSetting = alternate; return true; }
bool XDeviceUsbHost_platformDeviceClearHalt(void* handle, XDeviceUsbEndpointAddress endpoint)
{ XPosixUsbDevice* d = (XPosixUsbDevice*)handle; int result = d ? libusb_clear_halt(d->m_handle, endpoint) : LIBUSB_ERROR_INVALID_PARAM; if (result != 0 && d) xusbSetDeviceError(d, result); return result == 0; }
bool XDeviceUsbHost_platformDeviceReset(void* handle)
{ XPosixUsbDevice* d = (XPosixUsbDevice*)handle; int result = d ? libusb_reset_device(d->m_handle) : LIBUSB_ERROR_INVALID_PARAM; if (result != 0 && d) xusbSetDeviceError(d, result); return result == 0; }

XDeviceUsbTransferResult XDeviceUsbHost_platformDeviceControlTransfer(void* handle, const XDeviceUsbControlRequest* r, void* data, size_t capacity, size_t* transferred, int32_t timeoutMs)
{ XPosixUsbDevice* d = (XPosixUsbDevice*)handle; int result; if (transferred) *transferred = 0; if (!d || !r || (r->m_length && !data) || capacity < r->m_length) return XDeviceUsbTransferResult_InvalidArgument; result = libusb_control_transfer(d->m_handle, r->m_requestType, r->m_request, r->m_value, r->m_index, (unsigned char*)data, r->m_length, timeoutMs > 0 ? (unsigned int)timeoutMs : 0u); if (result < 0) { xusbSetDeviceError(d, result); return xusbMapError(result); } if (transferred) *transferred = (size_t)result; xusbSetDeviceError(d, 0); return XDeviceUsbTransferResult_Ok; }
XDeviceUsbTransferResult XDeviceUsbHost_platformDeviceTransfer(void* handle, XDeviceUsbEndpointAddress endpoint, void* data, size_t length, size_t* transferred, int32_t timeoutMs)
{ XPosixUsbDevice* d = (XPosixUsbDevice*)handle; const struct libusb_endpoint_descriptor* e; int type; if (transferred) *transferred = 0; if (!d || (length && !data) || length > INT_MAX) return XDeviceUsbTransferResult_InvalidArgument; e = xusbFindEndpoint(d, endpoint, NULL); if (!e) return XDeviceUsbTransferResult_InvalidArgument; type = e->bmAttributes & 3u; if (type == 1) return xusbSyncIso(d, endpoint, data, length, timeoutMs, e, transferred); { int actual = 0; int result = type == 2 ? libusb_bulk_transfer(d->m_handle, endpoint, (unsigned char*)data, (int)length, &actual, timeoutMs > 0 ? (unsigned int)timeoutMs : 0u) : libusb_interrupt_transfer(d->m_handle, endpoint, (unsigned char*)data, (int)length, &actual, timeoutMs > 0 ? (unsigned int)timeoutMs : 0u); if (transferred && actual > 0) *transferred = (size_t)actual; xusbSetDeviceError(d, result); return xusbMapError(result); } }
XDeviceUsbTransferId XDeviceUsbHost_platformDeviceSubmitTransfer(void* handle, const XDeviceUsbTransferRequest* r, XDeviceUsbTransferCallback callback, void* userData)
{ XPosixUsbDevice* d = (XPosixUsbDevice*)handle; XDeviceUsbTransferId id = XDEVICE_USB_INVALID_TRANSFER_ID; int result = xusbSubmit(d, r, callback, userData, &id); if (result != 0) { if (d) xusbSetDeviceError(d, result); return XDEVICE_USB_INVALID_TRANSFER_ID; } return id; }
XDeviceUsbTransferResult XDeviceUsbHost_platformDeviceCancelTransfer(void* handle, XDeviceUsbTransferId id)
{ XPosixUsbDevice* d = (XPosixUsbDevice*)handle; XPosixUsbTransfer* t; int result; if (!d || !id) return XDeviceUsbTransferResult_InvalidArgument; for (t = d->m_transfers; t; t = t->m_next) if (t->m_id == id) { result = libusb_cancel_transfer(t->m_native); return result == LIBUSB_SUCCESS ? XDeviceUsbTransferResult_Cancelled : xusbMapError(result); } return XDeviceUsbTransferResult_NoDevice; }
XDeviceUsbFeatures XDeviceUsbHost_platformDeviceFeatures(void* handle)
{ XPosixUsbDevice* d = (XPosixUsbDevice*)handle; XDeviceUsbFeatures f = XDeviceUsbFeature_DescriptorCache | XDeviceUsbFeature_ProcessEvents | XDeviceUsbFeature_AsyncTransfer | XDeviceUsbFeature_Isochronous | XDeviceUsbFeature_Reset; if (!d) return XDeviceUsbFeature_None; if (d->m_controller && libusb_has_capability(LIBUSB_CAP_HAS_HOTPLUG)) f |= XDeviceUsbFeature_Hotplug; if (xusbSpeed(libusb_get_device_speed(d->m_device)) >= XDeviceUsbSpeed_High) f |= XDeviceUsbFeature_HighSpeed; if (xusbSpeed(libusb_get_device_speed(d->m_device)) >= XDeviceUsbSpeed_Super) f |= XDeviceUsbFeature_SuperSpeed; return f; }
XDeviceUsbNativeHandle XDeviceUsbHost_platformDeviceHandle(void* handle) { XPosixUsbDevice* d = (XPosixUsbDevice*)handle; return d ? (XDeviceUsbNativeHandle)(intptr_t)d->m_handle : XDEVICE_USB_INVALID_NATIVE_HANDLE; }
XDeviceUsbError XDeviceUsbHost_platformDeviceLastError(void* handle) { XPosixUsbDevice* d = (XPosixUsbDevice*)handle; return d ? d->m_lastError : XDeviceUsbError_NotOpen; }
int32_t XDeviceUsbHost_platformDeviceNativeError(void* handle) { XPosixUsbDevice* d = (XPosixUsbDevice*)handle; return d ? d->m_lastNativeError : LIBUSB_ERROR_INVALID_PARAM; }
void XDeviceUsbHost_platformDeviceClearError(void* handle) { if (handle) xusbSetDeviceError((XPosixUsbDevice*)handle, 0); }

#if !defined(__linux__)
#define XDEVICE_USB_PLATFORM_STUB_GADGET_ONLY
#include "../../../XDeviceUsb_platform_stub.inc"
#undef XDEVICE_USB_PLATFORM_STUB_GADGET_ONLY
#endif

#else
#if !defined(__linux__)
#include "../../../XDeviceUsb_platform_stub.inc"
#else
#define XDEVICE_USB_PLATFORM_STUB_HOST_ONLY
#include "../../../XDeviceUsb_platform_stub.inc"
#undef XDEVICE_USB_PLATFORM_STUB_HOST_ONLY
#endif
#endif

#endif /* POSIX platform */
