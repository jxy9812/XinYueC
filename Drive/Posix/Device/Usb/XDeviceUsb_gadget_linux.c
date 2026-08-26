/**
 * @file       XDeviceUsb_gadget_linux.c
 * @brief      Linux USB Device/Gadget 后端，基于 FunctionFS。
 * @details    FunctionFS 需要由系统管理员预先创建 configfs Gadget、挂载
 *             FunctionFS，并把挂载目录名放入 XDeviceUsbGadgetChannel.m_name。
 *             本文件不修改 configfs 拓扑，避免库擅自接管系统 USB Gadget。
 */
#if defined(__linux__)

#include "XDeviceUsbGadget.h"
#include <linux/usb/functionfs.h>
#include <endian.h>
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <time.h>
#include <unistd.h>

#ifndef FUNCTIONFS_DESCRIPTORS_MAGIC_V2
#define FUNCTIONFS_DESCRIPTORS_MAGIC_V2 3u
#endif
#ifndef FUNCTIONFS_STRINGS_MAGIC
#define FUNCTIONFS_STRINGS_MAGIC 2u
#endif
#ifndef FUNCTIONFS_HAS_FS_DESC
#define FUNCTIONFS_HAS_FS_DESC 1u
#endif
#ifndef FUNCTIONFS_HAS_HS_DESC
#define FUNCTIONFS_HAS_HS_DESC 2u
#endif
#ifndef FUNCTIONFS_HAS_SS_DESC
#define FUNCTIONFS_HAS_SS_DESC 4u
#endif
#ifndef O_CLOEXEC
#define O_CLOEXEC 0
#endif

#define XLINUX_GADGET_MAX_ENDPOINTS 32u

typedef struct XLinuxGadgetEndpoint XLinuxGadgetEndpoint;
typedef struct XLinuxGadgetTransfer XLinuxGadgetTransfer;

struct XLinuxGadgetEndpoint
{
    XDeviceUsbGadgetEndpointInfo m_info;
    int m_fd;
    uint8_t m_ffsIndex;
};

struct XLinuxGadgetTransfer
{
    XLinuxGadgetTransfer* m_next;
    struct XLinuxGadgetController* m_controller;
    XDeviceUsbTransferId m_id;
    XDeviceUsbEndpointAddress m_endpoint;
    void* m_data;
    size_t m_length;
    int32_t m_timeoutMs;
    uint64_t m_deadline;
    XDeviceUsbGadgetTransferCallback m_callback;
    void* m_userData;
};

typedef struct XLinuxGadgetController
{
    int m_ep0;
    char* m_basePath;
    XDeviceUsbGadgetConfig m_config;
    XDeviceUsbGadgetState m_state;
    XDeviceUsbGadgetState m_stateBeforeSuspend;
    XDeviceUsbGadgetSetupCallback m_setupCallback;
    void* m_setupUserData;
    XDeviceUsbGadgetEventCallback m_eventCallback;
    void* m_eventUserData;
    XLinuxGadgetEndpoint m_endpoints[XLINUX_GADGET_MAX_ENDPOINTS];
    size_t m_endpointCount;
    XLinuxGadgetTransfer* m_transfers;
    XDeviceUsbTransferId m_nextTransferId;
    XDeviceUsbError m_lastError;
    int m_lastNativeError;
} XLinuxGadgetController;

static uint64_t xlinuxGadgetNow(void)
{
    struct timespec value;
    if (clock_gettime(CLOCK_MONOTONIC, &value) != 0) return 0;
    return (uint64_t)value.tv_sec * UINT64_C(1000) +
           (uint64_t)value.tv_nsec / UINT64_C(1000000);
}

static void xlinuxGadgetSetError(XLinuxGadgetController* controller, int nativeError)
{
    if (!controller) return;
    controller->m_lastNativeError = nativeError;
    switch (nativeError) {
    case 0: controller->m_lastError = XDeviceUsbError_None; break;
    case EINVAL: controller->m_lastError = XDeviceUsbError_InvalidArgument; break;
    case EBUSY: controller->m_lastError = XDeviceUsbError_Busy; break;
    case EACCES: case EPERM: controller->m_lastError = XDeviceUsbError_PermissionDenied; break;
    case ETIMEDOUT: controller->m_lastError = XDeviceUsbError_Timeout; break;
    case ENODEV: case ENOENT: controller->m_lastError = XDeviceUsbError_NoDevice; break;
    case ENOMEM: controller->m_lastError = XDeviceUsbError_Resource; break;
    case ENOTSUP: controller->m_lastError = XDeviceUsbError_Unsupported; break;
    default: controller->m_lastError = XDeviceUsbError_Io; break;
    }
}

static XDeviceUsbTransferResult xlinuxGadgetMapTransferError(int error)
{
    switch (error) {
    case 0: return XDeviceUsbTransferResult_Ok;
    case ETIMEDOUT: return XDeviceUsbTransferResult_Timeout;
    case ECANCELED: return XDeviceUsbTransferResult_Cancelled;
    case EPIPE: return XDeviceUsbTransferResult_Stall;
    case ENODEV: case ENOENT: return XDeviceUsbTransferResult_NoDevice;
    case EINVAL: return XDeviceUsbTransferResult_InvalidArgument;
    case ENOTSUP: return XDeviceUsbTransferResult_Unsupported;
    case ENOMEM: return XDeviceUsbTransferResult_ResourceError;
    default: return XDeviceUsbTransferResult_IoError;
    }
}

static void xlinuxGadgetEmit(XLinuxGadgetController* controller,
                             XDeviceUsbGadgetEventType type,
                             XDeviceUsbEndpointAddress endpoint)
{
    XDeviceUsbGadgetEvent event;
    if (!controller || !controller->m_eventCallback) return;
    memset(&event, 0, sizeof(event));
    event.m_type = type;
    event.m_context = XDeviceUsbCallbackContext_Process;
    event.m_state = controller->m_state;
    event.m_speed = controller->m_config.m_speed;
    event.m_configurationValue = controller->m_state == XDeviceUsbGadgetState_Configured
        ? controller->m_config.m_configurations[0].m_value : 0u;
    event.m_interfaceNumber = 0xffu;
    event.m_alternateSetting = 0xffu;
    event.m_endpoint = endpoint;
    event.m_error = XDeviceUsbError_None;
    controller->m_eventCallback(controller, &event, controller->m_eventUserData);
}

static bool xlinuxGadgetGetConfigBytes(const XLinuxGadgetController* controller,
                                       const uint8_t** bytes, size_t* length)
{
    const XDeviceUsbGadgetConfigurationDescriptor* configuration;
    if (!controller || !bytes || !length || controller->m_config.m_configurationCount != 1u ||
        !controller->m_config.m_configurations) return false;
    configuration = &controller->m_config.m_configurations[0];
    if (!configuration->m_descriptor || configuration->m_descriptorLength < 9u ||
        configuration->m_descriptor[1] != 2u || configuration->m_descriptor[0] < 9u ||
        configuration->m_descriptor[0] > configuration->m_descriptorLength)
        return false;
    if (configuration->m_descriptorLength < 9u ||
        ((size_t)configuration->m_descriptor[2] |
         ((size_t)configuration->m_descriptor[3] << 8u)) >
            configuration->m_descriptorLength)
        return false;
    *bytes = configuration->m_descriptor;
    *length = (size_t)configuration->m_descriptor[2] |
              ((size_t)configuration->m_descriptor[3] << 8u);
    return true;
}

static bool xlinuxGadgetConfigSupported(const XLinuxGadgetController* controller)
{
    const XDeviceUsbGadgetConfigurationDescriptor* configuration;
    const uint8_t* bytes;
    size_t length;
    if (!controller || controller->m_config.m_configurationCount != 1u ||
        controller->m_config.m_deviceDescriptor.m_numConfigurations != 1u ||
        controller->m_config.m_speed < XDeviceUsbSpeed_Full ||
        controller->m_config.m_speed >= XDeviceUsbSpeed_Super ||
        controller->m_config.m_bosDescriptor || controller->m_config.m_bosDescriptorLength ||
        controller->m_config.m_reserved[0] || controller->m_config.m_reserved[1] ||
        !xlinuxGadgetGetConfigBytes(controller, &bytes, &length))
        return false;
    configuration = &controller->m_config.m_configurations[0];
    return configuration->m_value != 0u && bytes[5] == configuration->m_value &&
           bytes[7] == configuration->m_attributes &&
           (uint16_t)bytes[8] * 2u == configuration->m_maxPowerMa;
}

static bool xlinuxGadgetParseEndpoints(XLinuxGadgetController* controller)
{
    const uint8_t* bytes;
    size_t length;
    size_t offset;
    uint8_t interfaceNumber = 0xffu;
    uint8_t alternate = 0xffu;
    if (!xlinuxGadgetGetConfigBytes(controller, &bytes, &length)) return false;
    controller->m_endpointCount = 0;
    memset(controller->m_endpoints, 0, sizeof(controller->m_endpoints));
    for (offset = bytes[0]; offset + 2u <= length; ) {
        uint8_t descriptorLength = bytes[offset];
        uint8_t descriptorType = bytes[offset + 1u];
        size_t i;
        if (descriptorLength < 2u || offset + descriptorLength > length) return false;
        if (descriptorType == 4u && descriptorLength >= 9u) {
            interfaceNumber = bytes[offset + 2u];
            alternate = bytes[offset + 3u];
        } else if (descriptorType == 5u && descriptorLength >= 7u &&
                   bytes[offset + 2u] != 0u) {
            XDeviceUsbEndpointAddress address = bytes[offset + 2u];
            bool duplicate = false;
            for (i = 0; i < controller->m_endpointCount; ++i)
                if (controller->m_endpoints[i].m_info.m_address == address)
                    duplicate = true;
            if (!duplicate) {
                XLinuxGadgetEndpoint* endpoint;
                if (controller->m_endpointCount >= XLINUX_GADGET_MAX_ENDPOINTS) return false;
                endpoint = &controller->m_endpoints[controller->m_endpointCount];
                memset(endpoint, 0, sizeof(*endpoint));
                endpoint->m_fd = -1;
                endpoint->m_ffsIndex = (uint8_t)controller->m_endpointCount + 1u;
                endpoint->m_info.m_address = address;
                endpoint->m_info.m_transferType = (XDeviceUsbTransferType)(bytes[offset + 3u] & 3u);
                endpoint->m_info.m_maxPacketSize = (uint16_t)bytes[offset + 4u] |
                    ((uint16_t)bytes[offset + 5u] << 8u);
                endpoint->m_info.m_interval = bytes[offset + 6u];
                endpoint->m_info.m_interfaceNumber = interfaceNumber;
                endpoint->m_info.m_alternateSetting = alternate;
                endpoint->m_info.m_maxTransferSize = 0u;
                ++controller->m_endpointCount;
            }
        }
        offset += descriptorLength;
    }
    return controller->m_endpointCount != 0u;
}

static XLinuxGadgetEndpoint* xlinuxGadgetFindEndpoint(XLinuxGadgetController* controller,
                                                       XDeviceUsbEndpointAddress address)
{
    size_t i;
    if (!controller || address == 0u) return NULL;
    for (i = 0; i < controller->m_endpointCount; ++i)
        if (controller->m_endpoints[i].m_info.m_address == address)
            return &controller->m_endpoints[i];
    return NULL;
}

static bool xlinuxGadgetWriteAll(int fd, const void* data, size_t length)
{
    const uint8_t* bytes = (const uint8_t*)data;
    while (length != 0u) {
        ssize_t result = write(fd, bytes, length);
        if (result > 0) { bytes += result; length -= (size_t)result; continue; }
        if (result < 0 && errno == EINTR) continue;
        return false;
    }
    return true;
}

static bool xlinuxGadgetBuildDescriptors(XLinuxGadgetController* controller,
                                         uint8_t** output, size_t* outputLength)
{
    const uint8_t* bytes;
    size_t length;
    size_t bodyLength;
    size_t headerLength;
    uint32_t count = 0;
    size_t offset;
    struct usb_functionfs_descs_head_v2* head;
    uint8_t* result;
    uint8_t* cursor;
    uint32_t flags = FUNCTIONFS_HAS_FS_DESC;
    if (!output || !outputLength || !xlinuxGadgetGetConfigBytes(controller, &bytes, &length))
        return false;
    bodyLength = length - bytes[0];
    for (offset = bytes[0]; offset + 2u <= length; ) {
        uint8_t descriptorLength = bytes[offset];
        if (descriptorLength < 2u || offset + descriptorLength > length) return false;
        ++count;
        offset += descriptorLength;
    }
    if (controller->m_config.m_speed >= XDeviceUsbSpeed_High)
        flags |= FUNCTIONFS_HAS_HS_DESC;
    headerLength = sizeof(*head) + sizeof(uint32_t);
    if (flags & FUNCTIONFS_HAS_HS_DESC) headerLength += sizeof(uint32_t);
    result = (uint8_t*)calloc(1, headerLength + bodyLength +
                              ((flags & FUNCTIONFS_HAS_HS_DESC) ? bodyLength : 0u));
    if (!result) return false;
    head = (struct usb_functionfs_descs_head_v2*)result;
    head->magic = htole32(FUNCTIONFS_DESCRIPTORS_MAGIC_V2);
    head->length = htole32((uint32_t)(headerLength + bodyLength +
                                      ((flags & FUNCTIONFS_HAS_HS_DESC) ? bodyLength : 0u)));
    head->flags = htole32(flags);
    cursor = result + sizeof(*head);
    {
        uint32_t descriptorCount = htole32(count);
        memcpy(cursor, &descriptorCount, sizeof(descriptorCount));
        cursor += sizeof(descriptorCount);
        if (flags & FUNCTIONFS_HAS_HS_DESC) {
            memcpy(cursor, &descriptorCount, sizeof(descriptorCount));
            cursor += sizeof(descriptorCount);
        }
    }
    memcpy(cursor, bytes + bytes[0], bodyLength);
    cursor += bodyLength;
    if (flags & FUNCTIONFS_HAS_HS_DESC)
        memcpy(cursor, bytes + bytes[0], bodyLength);
    *output = result;
    *outputLength = headerLength + bodyLength +
                    ((flags & FUNCTIONFS_HAS_HS_DESC) ? bodyLength : 0u);
    return true;
}

static bool xlinuxGadgetBuildStrings(XLinuxGadgetController* controller,
                                     uint8_t** output, size_t* outputLength)
{
    uint8_t* result;
    uint8_t indexes[256];
    size_t i;
    size_t maxIndex = 0;
    size_t length = sizeof(struct usb_functionfs_strings_head) + sizeof(uint16_t);
    uint16_t language = 0u;
    if (!output || !outputLength) return false;
    *output = NULL;
    *outputLength = 0;
    if (!controller->m_config.m_stringCount) return true;
    if (!controller->m_config.m_strings) return false;
    memset(indexes, 0, sizeof(indexes));
    for (i = 0; i < controller->m_config.m_stringCount; ++i) {
        const XDeviceUsbGadgetStringDescriptor* string = &controller->m_config.m_strings[i];
        if (!string->m_index || !string->m_utf8 || indexes[string->m_index]) return false;
        indexes[string->m_index] = 1u;
        if (string->m_languageId) {
            if (language != 0u && language != string->m_languageId) return false;
            language = string->m_languageId;
        }
        if (string->m_index > maxIndex) maxIndex = string->m_index;
    }
    if (language == 0u) language = 0x0409u;
    for (i = 1; i <= maxIndex; ++i) {
        const char* value = "";
        size_t j;
        for (j = 0; j < controller->m_config.m_stringCount; ++j)
            if (controller->m_config.m_strings[j].m_index == i)
                value = controller->m_config.m_strings[j].m_utf8;
        /* FunctionFS strings are NUL-terminated userspace strings.  They are
         * converted to USB string descriptors by the FunctionFS layer; do not
         * write UTF-16 code units here. */
        length += strlen(value) + 1u;
    }
    result = (uint8_t*)calloc(1, length);
    if (!result) return false;
    ((struct usb_functionfs_strings_head*)result)->magic = htole32(FUNCTIONFS_STRINGS_MAGIC);
    ((struct usb_functionfs_strings_head*)result)->length = htole32((uint32_t)length);
    ((struct usb_functionfs_strings_head*)result)->str_count = htole32((uint32_t)maxIndex);
    ((struct usb_functionfs_strings_head*)result)->lang_count = htole32(1u);
    language = htole16(language);
    memcpy(result + sizeof(struct usb_functionfs_strings_head), &language, sizeof(language));
    {
        uint8_t* cursor = result + sizeof(struct usb_functionfs_strings_head) + sizeof(language);
        for (i = 1; i <= maxIndex; ++i) {
            const char* value = "";
            size_t j;
            for (j = 0; j < controller->m_config.m_stringCount; ++j)
                if (controller->m_config.m_strings[j].m_index == i)
                    value = controller->m_config.m_strings[j].m_utf8;
            memcpy(cursor, value, strlen(value) + 1u);
            cursor += strlen(value) + 1u;
        }
    }
    *output = result;
    *outputLength = length;
    return true;
}

static bool xlinuxGadgetPath(XLinuxGadgetController* controller,
                             const char* name, char* path, size_t capacity)
{
    const char* base;
    if (!controller || !name || !path || capacity == 0u) return false;
    base = controller->m_config.m_channel.m_name;
    if (!base || !*base) base = "/dev/ffs/xinyuec";
    if (snprintf(path, capacity, "%s/%s", base, name) < 0 ||
        strlen(path) >= capacity) return false;
    return true;
}

static bool xlinuxGadgetOpenEndpoints(XLinuxGadgetController* controller)
{
    size_t i;
    char path[512];
    for (i = 0; i < controller->m_endpointCount; ++i) {
        int flags = XDEVICE_USB_ENDPOINT_IS_IN(controller->m_endpoints[i].m_info.m_address)
            ? O_WRONLY : O_RDONLY;
        {
            char endpointName[32];
            snprintf(endpointName, sizeof(endpointName), "ep%u",
                     controller->m_endpoints[i].m_ffsIndex);
            if (!xlinuxGadgetPath(controller, endpointName, path, sizeof(path))) return false;
        }
        controller->m_endpoints[i].m_fd = open(path, flags | O_NONBLOCK | O_CLOEXEC);
        if (controller->m_endpoints[i].m_fd < 0) return false;
    }
    return true;
}

static void xlinuxGadgetCloseEndpoints(XLinuxGadgetController* controller)
{
    size_t i;
    if (!controller) return;
    for (i = 0; i < controller->m_endpointCount; ++i) {
        if (controller->m_endpoints[i].m_fd >= 0) close(controller->m_endpoints[i].m_fd);
        controller->m_endpoints[i].m_fd = -1;
    }
}

static void xlinuxGadgetCompleteTransfer(XLinuxGadgetController* controller,
                                         XLinuxGadgetTransfer* transfer,
                                         XDeviceUsbTransferResult result,
                                         size_t transferred)
{
    XLinuxGadgetTransfer** cursor = &controller->m_transfers;
    XDeviceUsbGadgetTransferEvent event;
    while (*cursor && *cursor != transfer) cursor = &(*cursor)->m_next;
    if (*cursor == transfer) *cursor = transfer->m_next;
    memset(&event, 0, sizeof(event));
    event.m_endpoint = transfer->m_endpoint;
    event.m_transferId = transfer->m_id;
    event.m_result = result;
    event.m_transferred = transferred;
    event.m_context = XDeviceUsbCallbackContext_Process;
    if (transfer->m_callback) transfer->m_callback(controller, transfer->m_id, &event,
                                                    transfer->m_userData);
    if (controller->m_eventCallback) {
        XDeviceUsbGadgetEvent gadgetEvent;
        memset(&gadgetEvent, 0, sizeof(gadgetEvent));
        gadgetEvent.m_type = XDeviceUsbGadgetEventType_Transfer;
        gadgetEvent.m_context = XDeviceUsbCallbackContext_Process;
        gadgetEvent.m_state = controller->m_state;
        gadgetEvent.m_speed = controller->m_config.m_speed;
        gadgetEvent.m_endpoint = transfer->m_endpoint;
        gadgetEvent.m_transfer = event;
        controller->m_eventCallback(controller, &gadgetEvent, controller->m_eventUserData);
    }
    free(transfer);
}

static bool xlinuxGadgetHandleSetup(XLinuxGadgetController* controller,
                                    const struct usb_functionfs_event* nativeEvent)
{
    XDeviceUsbGadgetSetupRequest request;
    uint8_t* data = NULL;
    size_t length;
    size_t actual = 0;
    XDeviceUsbGadgetSetupResult setupResult = XDeviceUsbGadgetSetupResult_NotHandled;
    if (!controller || !nativeEvent) return false;
    memset(&request, 0, sizeof(request));
    request.m_requestType = nativeEvent->u.setup.bRequestType;
    request.m_request = nativeEvent->u.setup.bRequest;
    request.m_value = le16toh(nativeEvent->u.setup.wValue);
    request.m_index = le16toh(nativeEvent->u.setup.wIndex);
    request.m_length = le16toh(nativeEvent->u.setup.wLength);
    length = request.m_length;
    if (controller->m_eventCallback) {
        XDeviceUsbGadgetEvent event;
        memset(&event, 0, sizeof(event));
        event.m_type = XDeviceUsbGadgetEventType_Setup;
        event.m_context = XDeviceUsbCallbackContext_Process;
        event.m_state = controller->m_state;
        event.m_speed = controller->m_config.m_speed;
        event.m_setup = request;
        controller->m_eventCallback(controller, &event, controller->m_eventUserData);
    }
    if ((request.m_requestType & XDEVICE_USB_SETUP_REQUEST_TYPE_MASK) ==
            XDEVICE_USB_SETUP_REQUEST_TYPE_STANDARD)
        return true;
    if (length) {
        data = (uint8_t*)calloc(1, length);
        if (!data) return false;
        if (!XDEVICE_USB_SETUP_REQUEST_IS_IN(request.m_requestType)) {
            ssize_t readLength = read(controller->m_ep0, data, length);
            if (readLength < 0 || (size_t)readLength != length) { free(data); return false; }
        }
    }
    if (controller->m_setupCallback) {
        setupResult = controller->m_setupCallback(controller, &request, data, length,
                                                   &actual, controller->m_setupUserData);
    }
    if (setupResult != XDeviceUsbGadgetSetupResult_Handled) actual = 0;
    if (actual > length) actual = length;
    if (XDEVICE_USB_SETUP_REQUEST_IS_IN(request.m_requestType)) {
        if (!xlinuxGadgetWriteAll(controller->m_ep0, data, actual)) {
            xlinuxGadgetSetError(controller, errno);
            free(data);
            return false;
        }
    } else {
        /* FunctionFS uses a zero-length ep0 write to complete a handled OUT
         * request and to STALL an unhandled class/vendor request. */
        if (!xlinuxGadgetWriteAll(controller->m_ep0, NULL, 0u)) {
            xlinuxGadgetSetError(controller, errno);
            free(data);
            return false;
        }
    }
    free(data);
    return true;
}

bool XDeviceUsbGadget_platformStart(void* controller);
bool XDeviceUsbGadget_platformStop(void* controller);

void* XDeviceUsbGadget_platformCreate(const XDeviceUsbGadgetConfig* config, int* error)
{
    XLinuxGadgetController* controller;
    if (!config) { if (error) *error = XDeviceUsbError_InvalidArgument; return NULL; }
    controller = (XLinuxGadgetController*)calloc(1, sizeof(*controller));
    if (!controller) { if (error) *error = XDeviceUsbError_Resource; return NULL; }
    controller->m_ep0 = -1;
    controller->m_nextTransferId = 1;
    controller->m_lastError = XDeviceUsbError_None;
    controller->m_config = *config;
    if (error) *error = XDeviceUsbError_None;
    return controller;
}

bool XDeviceUsbGadget_platformOpen(void* handle, const XDeviceUsbGadgetConfig* config, int* error)
{
    XLinuxGadgetController* controller = (XLinuxGadgetController*)handle;
    if (!controller || !config) { if (error) *error = XDeviceUsbError_InvalidArgument; return false; }
    controller->m_config = *config;
    controller->m_state = XDeviceUsbGadgetState_Open;
    if (error) *error = XDeviceUsbError_None;
    if ((config->m_flags & XDeviceUsbGadgetFlag_AutoConnect) &&
        !XDeviceUsbGadget_platformStart(controller)) {
        if (error) *error = controller->m_lastError;
        return false;
    }
    return true;
}

void XDeviceUsbGadget_platformClose(void* handle)
{
    XLinuxGadgetController* controller = (XLinuxGadgetController*)handle;
    if (!controller) return;
    XDeviceUsbGadget_platformStop(controller);
    controller->m_state = XDeviceUsbGadgetState_Closed;
}

void XDeviceUsbGadget_platformDelete(void* handle)
{
    XLinuxGadgetController* controller = (XLinuxGadgetController*)handle;
    if (!controller) return;
    XDeviceUsbGadget_platformClose(controller);
    free(controller);
}

bool XDeviceUsbGadget_platformIsStarted(void* handle)
{ XLinuxGadgetController* c = (XLinuxGadgetController*)handle; return c && c->m_state >= XDeviceUsbGadgetState_Started; }
bool XDeviceUsbGadget_platformIsConfigured(void* handle)
{ XLinuxGadgetController* c = (XLinuxGadgetController*)handle; return c && c->m_state == XDeviceUsbGadgetState_Configured; }
bool XDeviceUsbGadget_platformGetConfig(void* handle, XDeviceUsbGadgetConfig* config)
{ XLinuxGadgetController* c = (XLinuxGadgetController*)handle; if (!c || !config) return false; *config = c->m_config; return true; }

bool XDeviceUsbGadget_platformConfigure(void* handle, const XDeviceUsbGadgetConfig* config)
{
    XLinuxGadgetController* controller = (XLinuxGadgetController*)handle;
    XDeviceUsbGadgetConfig previous;
    if (!controller || !config || controller->m_state >= XDeviceUsbGadgetState_Started) {
        if (controller) xlinuxGadgetSetError(controller, EINVAL);
        return false;
    }
    previous = controller->m_config;
    controller->m_config = *config;
    if (!xlinuxGadgetConfigSupported(controller) || !xlinuxGadgetParseEndpoints(controller)) {
        controller->m_config = previous;
        xlinuxGadgetSetError(controller, EINVAL);
        return false;
    }
    return true;
}

XDeviceUsbGadgetState XDeviceUsbGadget_platformStatus(void* handle)
{ XLinuxGadgetController* c = (XLinuxGadgetController*)handle; return c ? c->m_state : XDeviceUsbGadgetState_Closed; }

bool XDeviceUsbGadget_platformStart(void* handle)
{
    XLinuxGadgetController* controller = (XLinuxGadgetController*)handle;
    uint8_t* descriptors = NULL;
    uint8_t* strings = NULL;
    size_t descriptorLength = 0;
    size_t stringLength = 0;
    char path[512];
    if (!controller || controller->m_state == XDeviceUsbGadgetState_Closed) {
        if (controller) xlinuxGadgetSetError(controller, EOPNOTSUPP);
        return false;
    }
    if (!xlinuxGadgetConfigSupported(controller)) {
        xlinuxGadgetSetError(controller, EINVAL);
        return false;
    }
    if (!xlinuxGadgetParseEndpoints(controller) ||
        !xlinuxGadgetBuildDescriptors(controller, &descriptors, &descriptorLength) ||
        !xlinuxGadgetBuildStrings(controller, &strings, &stringLength) ||
        !xlinuxGadgetPath(controller, "ep0", path, sizeof(path))) {
        xlinuxGadgetSetError(controller, EINVAL);
        free(descriptors); free(strings);
        return false;
    }
    controller->m_ep0 = open(path, O_RDWR | O_NONBLOCK | O_CLOEXEC);
    if (controller->m_ep0 < 0 || !xlinuxGadgetWriteAll(controller->m_ep0, descriptors, descriptorLength) ||
        (strings && !xlinuxGadgetWriteAll(controller->m_ep0, strings, stringLength)) ||
        !xlinuxGadgetOpenEndpoints(controller)) {
        xlinuxGadgetSetError(controller, errno);
        if (controller->m_ep0 >= 0) close(controller->m_ep0);
        controller->m_ep0 = -1;
        xlinuxGadgetCloseEndpoints(controller);
        free(descriptors); free(strings);
        return false;
    }
    free(descriptors); free(strings);
    controller->m_state = XDeviceUsbGadgetState_Started;
    return true;
}

bool XDeviceUsbGadget_platformStop(void* handle)
{
    XLinuxGadgetController* controller = (XLinuxGadgetController*)handle;
    XLinuxGadgetTransfer* transfer;
    if (!controller) return false;
    transfer = controller->m_transfers;
    while (transfer) {
        XLinuxGadgetTransfer* next = transfer->m_next;
        xlinuxGadgetCompleteTransfer(controller, transfer,
                                     XDeviceUsbTransferResult_Cancelled, 0u);
        transfer = next;
    }
    xlinuxGadgetCloseEndpoints(controller);
    if (controller->m_ep0 >= 0) close(controller->m_ep0);
    controller->m_ep0 = -1;
    if (controller->m_state != XDeviceUsbGadgetState_Closed)
        controller->m_state = XDeviceUsbGadgetState_Open;
    return true;
}

bool XDeviceUsbGadget_platformSetSetupCallback(void* handle,
                                                XDeviceUsbGadgetSetupCallback callback,
                                                void* userData)
{ XLinuxGadgetController* c = (XLinuxGadgetController*)handle; if (!c) return false; c->m_setupCallback = callback; c->m_setupUserData = userData; return true; }
bool XDeviceUsbGadget_platformSetEventCallback(void* handle,
                                                XDeviceUsbGadgetEventCallback callback,
                                                void* userData)
{ XLinuxGadgetController* c = (XLinuxGadgetController*)handle; if (!c) return false; c->m_eventCallback = callback; c->m_eventUserData = userData; return true; }

static int xlinuxGadgetPollTimeout(XLinuxGadgetController* controller, int32_t timeoutMs)
{
    uint64_t now = xlinuxGadgetNow();
    uint64_t deadline = UINT64_MAX;
    XLinuxGadgetTransfer* transfer;
    int result;
    for (transfer = controller->m_transfers; transfer; transfer = transfer->m_next)
        if (transfer->m_deadline && transfer->m_deadline < deadline) deadline = transfer->m_deadline;
    if (deadline == UINT64_MAX) return timeoutMs < 0 ? -1 : timeoutMs;
    if (deadline <= now) return 0;
    result = (int)(deadline - now);
    if (timeoutMs >= 0 && result > timeoutMs) result = timeoutMs;
    return result;
}

XDeviceUsbProcessResult XDeviceUsbGadget_platformProcessEvents(void* handle, int32_t timeoutMs)
{
    XLinuxGadgetController* controller = (XLinuxGadgetController*)handle;
    struct pollfd pollfds[XLINUX_GADGET_MAX_ENDPOINTS + 1u];
    XLinuxGadgetTransfer* transfer;
    nfds_t count = 1;
    int result;
    bool eventSeen = false;
    if (!controller || controller->m_ep0 < 0) return XDeviceUsbProcessResult_Error;
    memset(pollfds, 0, sizeof(pollfds));
    pollfds[0].fd = controller->m_ep0;
    pollfds[0].events = POLLIN;
    for (transfer = controller->m_transfers; transfer && count < sizeof(pollfds) / sizeof(pollfds[0]); transfer = transfer->m_next) {
        XLinuxGadgetEndpoint* endpoint = xlinuxGadgetFindEndpoint(controller, transfer->m_endpoint);
        if (!endpoint || endpoint->m_fd < 0) continue;
        pollfds[count].fd = endpoint->m_fd;
        pollfds[count].events = XDEVICE_USB_ENDPOINT_IS_IN(transfer->m_endpoint) ? POLLOUT : POLLIN;
        ++count;
    }
    result = poll(pollfds, count, xlinuxGadgetPollTimeout(controller, timeoutMs));
    if (result < 0) { if (errno == EINTR) return XDeviceUsbProcessResult_Timeout; xlinuxGadgetSetError(controller, errno); return XDeviceUsbProcessResult_Error; }
    if (pollfds[0].revents & POLLIN) {
        struct usb_functionfs_event events[8];
        ssize_t length = read(controller->m_ep0, events, sizeof(events));
        size_t i;
        if (length < 0 && errno != EAGAIN) { xlinuxGadgetSetError(controller, errno); return XDeviceUsbProcessResult_Error; }
        for (i = 0; length > 0 && i < (size_t)length / sizeof(events[0]); ++i) {
            switch (events[i].type) {
            case FUNCTIONFS_BIND: xlinuxGadgetEmit(controller, XDeviceUsbGadgetEventType_Connected, 0u); eventSeen = true; break;
            case FUNCTIONFS_UNBIND: controller->m_state = XDeviceUsbGadgetState_Started; xlinuxGadgetEmit(controller, XDeviceUsbGadgetEventType_Disconnected, 0u); eventSeen = true; break;
            case FUNCTIONFS_ENABLE: controller->m_state = XDeviceUsbGadgetState_Configured; xlinuxGadgetEmit(controller, XDeviceUsbGadgetEventType_Configured, 0u); eventSeen = true; break;
            case FUNCTIONFS_DISABLE: controller->m_state = XDeviceUsbGadgetState_Started; xlinuxGadgetEmit(controller, XDeviceUsbGadgetEventType_Deconfigured, 0u); eventSeen = true; break;
            case FUNCTIONFS_SETUP: if (!xlinuxGadgetHandleSetup(controller, &events[i])) return XDeviceUsbProcessResult_Error; eventSeen = true; break;
            case FUNCTIONFS_SUSPEND:
                controller->m_stateBeforeSuspend = controller->m_state;
                controller->m_state = XDeviceUsbGadgetState_Suspended;
                xlinuxGadgetEmit(controller, XDeviceUsbGadgetEventType_Suspend, 0u);
                eventSeen = true;
                break;
            case FUNCTIONFS_RESUME:
                controller->m_state = controller->m_stateBeforeSuspend == XDeviceUsbGadgetState_Suspended ||
                    controller->m_stateBeforeSuspend == XDeviceUsbGadgetState_Closed
                    ? XDeviceUsbGadgetState_Started : controller->m_stateBeforeSuspend;
                xlinuxGadgetEmit(controller, XDeviceUsbGadgetEventType_Resume, 0u);
                eventSeen = true;
                break;
            default: break;
            }
        }
    }
    transfer = controller->m_transfers;
    while (transfer) {
        XLinuxGadgetTransfer* next = transfer->m_next;
        XLinuxGadgetEndpoint* endpoint = xlinuxGadgetFindEndpoint(controller, transfer->m_endpoint);
        ssize_t ioResult = -1;
        if (transfer->m_deadline && xlinuxGadgetNow() >= transfer->m_deadline)
            xlinuxGadgetCompleteTransfer(controller, transfer, XDeviceUsbTransferResult_Timeout, 0u);
        else if (endpoint && endpoint->m_fd >= 0) {
            ioResult = XDEVICE_USB_ENDPOINT_IS_IN(transfer->m_endpoint)
                ? write(endpoint->m_fd, transfer->m_data, transfer->m_length)
                : read(endpoint->m_fd, transfer->m_data, transfer->m_length);
            if (ioResult >= 0) {
                xlinuxGadgetCompleteTransfer(controller, transfer,
                    XDeviceUsbTransferResult_Ok, (size_t)ioResult);
                eventSeen = true;
            } else if (errno != EAGAIN && errno != EINTR) {
                xlinuxGadgetSetError(controller, errno);
                xlinuxGadgetCompleteTransfer(controller, transfer,
                    xlinuxGadgetMapTransferError(errno), 0u);
                eventSeen = true;
            }
        }
        transfer = next;
    }
    return eventSeen ? XDeviceUsbProcessResult_Event : XDeviceUsbProcessResult_Timeout;
}

bool XDeviceUsbGadget_platformGetEndpointInfo(void* handle, XDeviceUsbGadgetEndpointInfo* endpoint)
{
    XLinuxGadgetController* c = (XLinuxGadgetController*)handle;
    XLinuxGadgetEndpoint* value;
    if (!c || !endpoint) return false;
    value = xlinuxGadgetFindEndpoint(c, endpoint->m_address);
    if (!value) return false;
    *endpoint = value->m_info;
    return true;
}

XDeviceUsbTransferResult XDeviceUsbGadget_platformTransfer(void* handle,
    XDeviceUsbEndpointAddress endpoint, void* data, size_t length,
    size_t* transferred, int32_t timeoutMs)
{
    XLinuxGadgetController* c = (XLinuxGadgetController*)handle;
    XLinuxGadgetEndpoint* value;
    struct pollfd pollfd;
    ssize_t result;
    if (transferred) *transferred = 0;
    if (!c || !data || !length || !(value = xlinuxGadgetFindEndpoint(c, endpoint)) || value->m_fd < 0)
        return XDeviceUsbTransferResult_InvalidArgument;
    memset(&pollfd, 0, sizeof(pollfd));
    pollfd.fd = value->m_fd;
    pollfd.events = XDEVICE_USB_ENDPOINT_IS_IN(endpoint) ? POLLOUT : POLLIN;
    if (poll(&pollfd, 1, timeoutMs < 0 ? -1 : timeoutMs) <= 0)
        return timeoutMs == 0 ? XDeviceUsbTransferResult_Timeout : xlinuxGadgetMapTransferError(errno ? errno : ETIMEDOUT);
    result = XDEVICE_USB_ENDPOINT_IS_IN(endpoint) ? write(value->m_fd, data, length) : read(value->m_fd, data, length);
    if (result < 0) { xlinuxGadgetSetError(c, errno); return xlinuxGadgetMapTransferError(errno); }
    if (transferred) *transferred = (size_t)result;
    return XDeviceUsbTransferResult_Ok;
}

XDeviceUsbTransferId XDeviceUsbGadget_platformSubmitTransfer(void* handle,
    XDeviceUsbEndpointAddress endpoint, void* data, size_t length, int32_t timeoutMs,
    XDeviceUsbGadgetTransferCallback callback, void* userData)
{
    XLinuxGadgetController* c = (XLinuxGadgetController*)handle;
    XLinuxGadgetEndpoint* endpointInfo;
    XLinuxGadgetTransfer* transfer;
    if (!c || !data || !length || !callback || !(endpointInfo = xlinuxGadgetFindEndpoint(c, endpoint)) || endpointInfo->m_fd < 0)
        return XDEVICE_USB_INVALID_TRANSFER_ID;
    transfer = (XLinuxGadgetTransfer*)calloc(1, sizeof(*transfer));
    if (!transfer) { xlinuxGadgetSetError(c, ENOMEM); return XDEVICE_USB_INVALID_TRANSFER_ID; }
    transfer->m_controller = c;
    transfer->m_endpoint = endpoint;
    transfer->m_data = data;
    transfer->m_length = length;
    transfer->m_timeoutMs = timeoutMs;
    transfer->m_deadline = timeoutMs > 0 ? xlinuxGadgetNow() + (uint64_t)timeoutMs : 0u;
    transfer->m_callback = callback;
    transfer->m_userData = userData;
    transfer->m_id = c->m_nextTransferId++;
    if (!transfer->m_id) transfer->m_id = c->m_nextTransferId++;
    transfer->m_next = c->m_transfers;
    c->m_transfers = transfer;
    return transfer->m_id;
}

XDeviceUsbTransferResult XDeviceUsbGadget_platformCancelTransfer(void* handle,
                                                                  XDeviceUsbTransferId id)
{
    XLinuxGadgetController* c = (XLinuxGadgetController*)handle;
    XLinuxGadgetTransfer* transfer;
    if (!c || !id) return XDeviceUsbTransferResult_InvalidArgument;
    for (transfer = c->m_transfers; transfer; transfer = transfer->m_next)
        if (transfer->m_id == id) {
            xlinuxGadgetCompleteTransfer(c, transfer, XDeviceUsbTransferResult_Cancelled, 0u);
            return XDeviceUsbTransferResult_Cancelled;
        }
    return XDeviceUsbTransferResult_NoDevice;
}

bool XDeviceUsbGadget_platformSetEndpointStalled(void* handle,
                                                  XDeviceUsbEndpointAddress endpoint, bool stalled)
{ (void)handle; (void)endpoint; (void)stalled; return false; }

bool XDeviceUsbGadget_platformClearEndpointQueue(void* handle,
                                                  XDeviceUsbEndpointAddress endpoint)
{
    XLinuxGadgetController* c = (XLinuxGadgetController*)handle;
    XLinuxGadgetEndpoint* value = c ? xlinuxGadgetFindEndpoint(c, endpoint) : NULL;
    XLinuxGadgetTransfer* transfer;
    if (!value || value->m_fd < 0) return false;
    transfer = c->m_transfers;
    while (transfer) {
        XLinuxGadgetTransfer* next = transfer->m_next;
        if (transfer->m_endpoint == endpoint)
            xlinuxGadgetCompleteTransfer(c, transfer,
                                         XDeviceUsbTransferResult_Cancelled, 0u);
        transfer = next;
    }
    if (ioctl(value->m_fd, FUNCTIONFS_FIFO_FLUSH) != 0) {
        xlinuxGadgetSetError(c, errno);
        return false;
    }
    return true;
}

bool XDeviceUsbGadget_platformRemoteWakeup(void* handle)
{ (void)handle; return false; }

XDeviceUsbGadgetFeatures XDeviceUsbGadget_platformFeatures(void* handle)
{
    XLinuxGadgetController* c = (XLinuxGadgetController*)handle;
    if (!c) return XDeviceUsbGadgetFeature_None;
    return XDeviceUsbGadgetFeature_DeviceMode |
           XDeviceUsbGadgetFeature_FullSpeed |
           XDeviceUsbGadgetFeature_HighSpeed |
           XDeviceUsbGadgetFeature_EndpointTransfer |
           XDeviceUsbGadgetFeature_AsyncTransfer |
           XDeviceUsbGadgetFeature_ControlCallback |
           XDeviceUsbGadgetFeature_EventCallback |
           XDeviceUsbGadgetFeature_SuspendResume |
           XDeviceUsbGadgetFeature_ProcessEvents |
           XDeviceUsbGadgetFeature_NativeHandle;
}

XDeviceUsbNativeHandle XDeviceUsbGadget_platformHandle(void* handle)
{ XLinuxGadgetController* c = (XLinuxGadgetController*)handle; return c && c->m_ep0 >= 0 ? (XDeviceUsbNativeHandle)c->m_ep0 : XDEVICE_USB_INVALID_NATIVE_HANDLE; }
XDeviceUsbError XDeviceUsbGadget_platformLastError(void* handle)
{ XLinuxGadgetController* c = (XLinuxGadgetController*)handle; return c ? c->m_lastError : XDeviceUsbError_NotOpen; }
int32_t XDeviceUsbGadget_platformNativeError(void* handle)
{ XLinuxGadgetController* c = (XLinuxGadgetController*)handle; return c ? c->m_lastNativeError : ENODEV; }
void XDeviceUsbGadget_platformClearError(void* handle)
{ if (handle) xlinuxGadgetSetError((XLinuxGadgetController*)handle, 0); }

#endif /* __linux__ */
