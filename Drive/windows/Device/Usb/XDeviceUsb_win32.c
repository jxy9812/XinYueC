/**
 * @file       XDeviceUsb_win32.c
 * @brief      Windows USB Host 平台实现。
 * @details    使用 SetupAPI 枚举 USB 设备接口，使用 WinUSB 完成设备打开、
 *             描述符读取、控制传输和 Bulk/Interrupt 端点传输。
 *             设备必须已经绑定 WinUSB 驱动。HID 保持 Windows 系统 HID 驱动，
 *             其 Input/Output Report 映射为合成 Interrupt 端点，Feature Report
 *             映射为受限的 HID 类请求；CDC、MSC 等仍由各自专用后端处理。
 */
#ifdef _WIN32

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0602
#endif

#include <windows.h>
#include <setupapi.h>
#include <winusb.h>
#include <initguid.h>
#include <hidsdi.h>
#include <hidpi.h>
#include <ntddser.h>
#include <usbiodef.h>
#include <usb.h>
#include <winusbio.h>
#include <cfgmgr32.h>
#include <stdlib.h>
#include <string.h>

#include "XDeviceUsbHost.h"
#include "XDeviceUsbGadget.h"
#include "XAbstractNetIoRing.h"
#include "XNetIoRingWin32.h"

#define XDEVICE_USB_PLATFORM_STUB_GADGET_ONLY
#include "../../../XDeviceUsb_platform_stub.inc"
#undef XDEVICE_USB_PLATFORM_STUB_GADGET_ONLY

#define XWINUSB_MAX_INTERFACES 32u

typedef struct XWinUsbTransfer XWinUsbTransfer;
typedef struct XWinUsbDevice XWinUsbDevice;

typedef struct XWinUsbHotplugEntry
{
    wchar_t* m_path;
    XDeviceUsbDeviceInfo m_info;
} XWinUsbHotplugEntry;

typedef struct XWinUsbController
{
    HDEVINFO m_deviceInfo;
    HDEVINFO m_hidDeviceInfo;
    HDEVINFO m_cdcDeviceInfo;
    GUID m_hidInterfaceGuid;
    DWORD m_lastError;
    uint32_t m_flags;
    XDeviceUsbHotplugCallback m_hotplugCallback;
    void* m_hotplugUserData;
    XWinUsbHotplugEntry* m_snapshot;
    size_t m_snapshotCount;
    bool m_snapshotReady;
    XWinUsbDevice* m_devices;
    HCMNOTIFICATION m_notification;
    HCMNOTIFICATION m_hidNotification;
    HCMNOTIFICATION m_cdcNotification;
    volatile LONG m_nativeChanged;
} XWinUsbController;

struct XWinUsbDevice
{
    HANDLE m_file;
    WINUSB_INTERFACE_HANDLE m_interfaces[XWINUSB_MAX_INTERFACES];
    uint8_t m_interfaceCount;
    wchar_t* m_path;
    USB_DEVICE_DESCRIPTOR m_deviceDescriptor;
    USB_CONFIGURATION_DESCRIPTOR m_configurationDescriptor;
    uint8_t* m_configuration;
    ULONG m_configurationLength;
    XDeviceUsbDeviceInfo m_info;
    DWORD m_lastError;
    CRITICAL_SECTION m_transferLock;
    bool m_transferLockInitialized;
    bool m_closing;
    bool m_requiresReopen;
    bool m_isHid;
    bool m_isCdc;
    HIDP_CAPS m_hidCaps;
    bool m_iocpAssociated;
    XWinUsbController* m_controller;
    XWinUsbDevice* m_nextControllerDevice;
    uint32_t m_claimedInterfaces;
    uint8_t m_currentAlternate[XWINUSB_MAX_INTERFACES];
    XWinUsbTransfer* m_transfers;
    XDeviceUsbTransferId m_nextTransferId;
};

struct XWinUsbTransfer
{
    XEventContext_IOCP m_io;
    XWinUsbTransfer* m_next;
    XWinUsbDevice* m_device;
    XDeviceUsbTransferId m_id;
    XDeviceUsbTransferRequest m_request;
    XDeviceUsbTransferCallback m_callback;
    void* m_userData;
    DWORD m_submitError;
    bool m_forcedFailure;
    DWORD m_completionErrorOverride;
    ULONGLONG m_deadlineTick;
    WINUSB_ISOCH_BUFFER_HANDLE m_isochBuffer;
    PUSBD_ISO_PACKET_DESCRIPTOR m_isochPackets;
    ULONG m_isochPacketCount;
    bool m_isochRead;
    bool m_active;
};

static bool xwinusbIoCompletion(XEventContext_IOCP* context, void* userData);
static void xwinusbDrainTransfers(XWinUsbDevice* device);

static void xwinusbSetControllerError(XWinUsbController* controller, DWORD error)
{
    if (controller) controller->m_lastError = error;
}

static void xwinusbSetDeviceError(XWinUsbDevice* device, DWORD error)
{
    if (device) device->m_lastError = error;
}

static DWORD CALLBACK xwinusbDeviceNotification(HCMNOTIFICATION notification,
                                                PVOID context,
                                                CM_NOTIFY_ACTION action,
                                                PCM_NOTIFY_EVENT_DATA eventData,
                                                DWORD eventDataSize)
{
    XWinUsbController* controller = (XWinUsbController*)context;
    (void)notification;
    (void)eventData;
    (void)eventDataSize;
    if (controller && (action == CM_NOTIFY_ACTION_DEVICEINTERFACEARRIVAL ||
                       action == CM_NOTIFY_ACTION_DEVICEINTERFACEREMOVAL))
        InterlockedExchange(&controller->m_nativeChanged, 1);
    return ERROR_SUCCESS;
}

static XWinUsbDevice* xwinusbOpenPath(const wchar_t* path);
static XWinUsbDevice* xwinusbOpenHidPath(const wchar_t* path);
static XWinUsbDevice* xwinusbOpenCdcPath(HDEVINFO deviceSet, const wchar_t* path);
static bool xwinusbGetInterfacePath(HDEVINFO deviceInfo, const GUID* interfaceGuid,
                                    DWORD index, wchar_t** path);
static bool xwinusbFindDeviceInfo(HDEVINFO deviceSet, const GUID* interfaceGuid,
                                  const wchar_t* path,
                                  SP_DEVINFO_DATA* deviceInfo);
static void xwinusbCloseDevice(XWinUsbDevice* device);
static void xwinusbExpireTransfers(XWinUsbDevice* device);
static DWORD CALLBACK xwinusbDeviceNotification(HCMNOTIFICATION notification,
                                                PVOID context,
                                                CM_NOTIFY_ACTION action,
                                                PCM_NOTIFY_EVENT_DATA eventData,
                                                DWORD eventDataSize);

static wchar_t* xwinusbDuplicatePath(const wchar_t* path)
{
    size_t length;
    wchar_t* copy;
    if (!path) return NULL;
    length = wcslen(path) + 1u;
    copy = (wchar_t*)calloc(length, sizeof(wchar_t));
    if (copy) memcpy(copy, path, length * sizeof(wchar_t));
    return copy;
}

static XDeviceUsbError xwinusbMapError(DWORD error)
{
    switch (error) {
    case ERROR_SUCCESS:             return XDeviceUsbError_None;
    case ERROR_INVALID_PARAMETER:   return XDeviceUsbError_InvalidArgument;
    case ERROR_ACCESS_DENIED:       return XDeviceUsbError_PermissionDenied;
    case ERROR_BUSY:                return XDeviceUsbError_Busy;
    case ERROR_SEM_TIMEOUT:
    case WAIT_TIMEOUT:              return XDeviceUsbError_Timeout;
    case ERROR_OPERATION_ABORTED:   return XDeviceUsbError_Interrupted;
    case ERROR_FILE_NOT_FOUND:
    case ERROR_PATH_NOT_FOUND:
    case ERROR_DEVICE_NOT_CONNECTED:
    case ERROR_NO_SUCH_DEVICE:      return XDeviceUsbError_NoDevice;
    case ERROR_NOT_SUPPORTED:
    case ERROR_CALL_NOT_IMPLEMENTED:return XDeviceUsbError_Unsupported;
    case ERROR_NOT_ENOUGH_MEMORY:
    case ERROR_OUTOFMEMORY:         return XDeviceUsbError_Resource;
    default:                        return XDeviceUsbError_Io;
    }
}

static XDeviceUsbTransferResult xwinusbMapTransferError(DWORD error)
{
    switch (error) {
    case ERROR_SUCCESS:             return XDeviceUsbTransferResult_Ok;
    case ERROR_SEM_TIMEOUT:
    case WAIT_TIMEOUT:              return XDeviceUsbTransferResult_Timeout;
    case ERROR_OPERATION_ABORTED:   return XDeviceUsbTransferResult_Cancelled;
    case ERROR_ACCESS_DENIED:       return XDeviceUsbTransferResult_PermissionDenied;
    case ERROR_FILE_NOT_FOUND:
    case ERROR_PATH_NOT_FOUND:
    case ERROR_DEVICE_NOT_CONNECTED:
    case ERROR_NO_SUCH_DEVICE:      return XDeviceUsbTransferResult_NoDevice;
    case ERROR_INVALID_PARAMETER:   return XDeviceUsbTransferResult_InvalidArgument;
    case ERROR_NOT_SUPPORTED:
    case ERROR_CALL_NOT_IMPLEMENTED:return XDeviceUsbTransferResult_Unsupported;
    case ERROR_NOT_ENOUGH_MEMORY:
    case ERROR_OUTOFMEMORY:         return XDeviceUsbTransferResult_ResourceError;
    case ERROR_GEN_FAILURE:         return XDeviceUsbTransferResult_Stall;
    default:                        return XDeviceUsbTransferResult_IoError;
    }
}

static uint64_t xwinusbHashPath(const wchar_t* path)
{
    uint64_t hash = UINT64_C(1469598103934665603);
    if (!path) return 0;
    while (*path) {
        hash ^= (uint16_t)*path++;
        hash *= UINT64_C(1099511628211);
    }
    return hash;
}

static void xwinusbFreeSnapshot(XWinUsbController* controller)
{
    size_t i;
    if (!controller) return;
    for (i = 0; i < controller->m_snapshotCount; ++i)
        free(controller->m_snapshot[i].m_path);
    free(controller->m_snapshot);
    controller->m_snapshot = NULL;
    controller->m_snapshotCount = 0;
}

static void xwinusbFreeSnapshotEntries(XWinUsbHotplugEntry* entries, size_t count)
{
    size_t i;
    for (i = 0; i < count; ++i) free(entries[i].m_path);
    free(entries);
}

static bool xwinusbAppendSnapshot(HDEVINFO deviceInfo, const GUID* interfaceGuid,
                                  uint8_t backend, XWinUsbHotplugEntry** entries,
                                  size_t* entryCount)
{
    DWORD index;
    if (deviceInfo == INVALID_HANDLE_VALUE || !interfaceGuid || !entries || !entryCount)
        return false;
    for (index = 0; ; ++index) {
        wchar_t* path = NULL;
        XWinUsbDevice* device;
        XWinUsbHotplugEntry* resized;
        if (!xwinusbGetInterfacePath(deviceInfo, interfaceGuid, index, &path)) break;
        device = backend == 1u ? xwinusbOpenHidPath(path)
             : (backend == 2u ? xwinusbOpenCdcPath(deviceInfo, path)
                               : xwinusbOpenPath(path));
        if (!device) {
            free(path);
            continue;
        }
        resized = (XWinUsbHotplugEntry*)realloc(*entries,
                    (*entryCount + 1u) * sizeof(**entries));
        if (!resized) {
            xwinusbCloseDevice(device);
            free(path);
            return false;
        }
        *entries = resized;
        (*entries)[*entryCount].m_path = path;
        (*entries)[*entryCount].m_info = device->m_info;
        (*entries)[*entryCount].m_info.m_controller = 0u;
        ++*entryCount;
        xwinusbCloseDevice(device);
    }
    return true;
}

static bool xwinusbBuildSnapshot(XWinUsbController* controller,
                                 XWinUsbHotplugEntry** snapshot,
                                 size_t* count)
{
    HDEVINFO deviceInfo;
    HDEVINFO hidDeviceInfo;
    XWinUsbHotplugEntry* entries = NULL;
    size_t entryCount = 0;
    if (!controller || !snapshot || !count) return false;
    *snapshot = NULL;
    *count = 0;
    deviceInfo = SetupDiGetClassDevsW(&GUID_DEVINTERFACE_USB_DEVICE, NULL, NULL,
                                      DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);
    if (deviceInfo == INVALID_HANDLE_VALUE) {
        xwinusbSetControllerError(controller, GetLastError());
        return false;
    }
    if (!xwinusbAppendSnapshot(deviceInfo, &GUID_DEVINTERFACE_USB_DEVICE, false,
                                &entries, &entryCount)) {
        SetupDiDestroyDeviceInfoList(deviceInfo);
        xwinusbFreeSnapshotEntries(entries, entryCount);
        return false;
    }
    SetupDiDestroyDeviceInfoList(deviceInfo);

    hidDeviceInfo = SetupDiGetClassDevsW(&controller->m_hidInterfaceGuid, NULL, NULL,
                                         DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);
    if (hidDeviceInfo != INVALID_HANDLE_VALUE) {
        if (!xwinusbAppendSnapshot(hidDeviceInfo, &controller->m_hidInterfaceGuid, 1u,
                                    &entries, &entryCount)) {
            SetupDiDestroyDeviceInfoList(hidDeviceInfo);
            xwinusbFreeSnapshotEntries(entries, entryCount);
            return false;
        }
        SetupDiDestroyDeviceInfoList(hidDeviceInfo);
    }
    hidDeviceInfo = SetupDiGetClassDevsW(&GUID_DEVINTERFACE_COMPORT, NULL, NULL,
                                         DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);
    if (hidDeviceInfo != INVALID_HANDLE_VALUE) {
        if (!xwinusbAppendSnapshot(hidDeviceInfo, &GUID_DEVINTERFACE_COMPORT, 2u,
                                    &entries, &entryCount)) {
            SetupDiDestroyDeviceInfoList(hidDeviceInfo);
            xwinusbFreeSnapshotEntries(entries, entryCount);
            return false;
        }
        SetupDiDestroyDeviceInfoList(hidDeviceInfo);
    }
    *snapshot = entries;
    *count = entryCount;
    return true;
}

static size_t xwinusbFindSnapshotPath(const XWinUsbHotplugEntry* snapshot,
                                      size_t count, const wchar_t* path)
{
    size_t i;
    if (!path) return count;
    for (i = 0; i < count; ++i)
        if (_wcsicmp(snapshot[i].m_path, path) == 0) return i;
    return count;
}

static bool xwinusbSnapshotInfoEqual(const XDeviceUsbDeviceInfo* left,
                                     const XDeviceUsbDeviceInfo* right)
{
    return left && right && memcmp(left, right, sizeof(*left)) == 0;
}

static bool xwinusbSelectorMatches(const XDeviceUsbDeviceSelector* selector,
                                   const XDeviceUsbDeviceInfo* info)
{
    if (!selector || !info) return false;
    if (selector->m_vendorId && selector->m_vendorId != info->m_vendorId) return false;
    if (selector->m_productId && selector->m_productId != info->m_productId) return false;
    if (selector->m_bcdDevice && selector->m_bcdDevice != info->m_bcdDevice) return false;
    if (selector->m_deviceClass != 0xffu && selector->m_deviceClass != info->m_deviceClass) return false;
    if (selector->m_deviceSubClass != 0xffu && selector->m_deviceSubClass != info->m_deviceSubClass) return false;
    if (selector->m_deviceProtocol != 0xffu && selector->m_deviceProtocol != info->m_deviceProtocol) return false;
    return true;
}

static bool xwinusbUtf16MatchesUtf8(const WCHAR* value, int length, const char* wanted)
{
    int utf8Length;
    char* utf8;
    bool matches;
    if (!wanted) return true;
    if (!value || length <= 0) return false;
    utf8Length = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, value, length,
                                     NULL, 0, NULL, NULL);
    if (utf8Length <= 0) return false;
    utf8 = (char*)calloc((size_t)utf8Length + 1u, sizeof(char));
    if (!utf8) return false;
    if (WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, value, length, utf8,
                            utf8Length, NULL, NULL) != utf8Length) {
        free(utf8);
        return false;
    }
    matches = strlen(wanted) == (size_t)utf8Length &&
              memcmp(wanted, utf8, (size_t)utf8Length) == 0;
    free(utf8);
    return matches;
}

static bool xwinusbDeviceMatchesSerial(const XWinUsbDevice* device, const char* wanted)
{
    WCHAR hidSerial[256];
    uint8_t languages[256];
    uint8_t descriptor[256];
    ULONG length = 0;
    uint16_t language;
    if (!wanted) return true;
    if (!device || device->m_isCdc) return false;
    if (device->m_isHid) {
        if (!HidD_GetSerialNumberString(device->m_file, hidSerial, sizeof(hidSerial)))
            return false;
        return xwinusbUtf16MatchesUtf8(hidSerial, (int)wcslen(hidSerial), wanted);
    }
    if (!device->m_deviceDescriptor.iSerialNumber || !device->m_interfaces[0]) return false;
    if (!WinUsb_GetDescriptor(device->m_interfaces[0], USB_STRING_DESCRIPTOR_TYPE, 0u,
                              0u, languages, sizeof(languages), &length) || length < 4u)
        return false;
    language = (uint16_t)languages[2] | ((uint16_t)languages[3] << 8u);
    if (!WinUsb_GetDescriptor(device->m_interfaces[0], USB_STRING_DESCRIPTOR_TYPE,
                              device->m_deviceDescriptor.iSerialNumber, language,
                              descriptor, sizeof(descriptor), &length) ||
        length < 2u || descriptor[1] != USB_STRING_DESCRIPTOR_TYPE ||
        descriptor[0] < 2u || descriptor[0] > length || (descriptor[0] & 1u) != 0u)
        return false;
    return xwinusbUtf16MatchesUtf8((const WCHAR*)(descriptor + 2u),
                                   ((int)descriptor[0] - 2) / 2,
                                   wanted);
}

static bool xwinusbDeviceMatchesSelector(const XWinUsbDevice* device,
                                          const XDeviceUsbDeviceSelector* selector)
{
    return device && xwinusbSelectorMatches(selector, &device->m_info) &&
           xwinusbDeviceMatchesSerial(device, selector->m_serialNumber_utf8);
}

static XWinUsbDevice* xwinusbOpenMatchingDevice(HDEVINFO deviceInfo,
                                                  const GUID* interfaceGuid,
                                                  uint8_t backend,
                                                  const XDeviceUsbDeviceSelector* selector,
                                                  uint32_t* matchIndex)
{
    DWORD index;
    if (deviceInfo == INVALID_HANDLE_VALUE || !interfaceGuid || !selector || !matchIndex)
        return NULL;
    for (index = 0; ; ++index) {
        wchar_t* path = NULL;
        XWinUsbDevice* device;
        if (!xwinusbGetInterfacePath(deviceInfo, interfaceGuid, index, &path)) break;
        device = backend == 1u ? xwinusbOpenHidPath(path)
             : (backend == 2u ? xwinusbOpenCdcPath(deviceInfo, path)
                               : xwinusbOpenPath(path));
        free(path);
        if (!device) continue;
        if (xwinusbDeviceMatchesSelector(device, selector) &&
            (*matchIndex)++ == selector->m_index)
            return device;
        xwinusbCloseDevice(device);
    }
    return NULL;
}

static bool xwinusbGetInterfacePath(HDEVINFO deviceInfo, const GUID* interfaceGuid,
                                    DWORD index, wchar_t** path)
{
    SP_DEVICE_INTERFACE_DATA interfaceData;
    SP_DEVICE_INTERFACE_DETAIL_DATA_W* detail;
    DWORD required = 0;
    DWORD detailSize;
    bool ok = false;

    if (!path) return false;
    *path = NULL;
    memset(&interfaceData, 0, sizeof(interfaceData));
    interfaceData.cbSize = sizeof(interfaceData);
    if (!interfaceGuid || !SetupDiEnumDeviceInterfaces(deviceInfo, NULL, interfaceGuid,
                                     index, &interfaceData))
        return false;
    SetupDiGetDeviceInterfaceDetailW(deviceInfo, &interfaceData, NULL, 0, &required, NULL);
    if (!required) return false;
    detailSize = required;
    detail = (SP_DEVICE_INTERFACE_DETAIL_DATA_W*)calloc(1, detailSize);
    if (!detail) return false;
    detail->cbSize = sizeof(*detail);
    if (SetupDiGetDeviceInterfaceDetailW(deviceInfo, &interfaceData, detail,
                                         detailSize, &required, NULL)) {
        size_t length = wcslen(detail->DevicePath) + 1u;
        *path = (wchar_t*)calloc(length, sizeof(wchar_t));
        if (*path) {
            memcpy(*path, detail->DevicePath, length * sizeof(wchar_t));
            ok = true;
        }
    }
    free(detail);
    return ok;
}

static bool xwinusbFindDeviceInfo(HDEVINFO deviceSet, const GUID* interfaceGuid,
                                  const wchar_t* path,
                                  SP_DEVINFO_DATA* deviceInfo)
{
    DWORD index;
    if (deviceSet == INVALID_HANDLE_VALUE || !interfaceGuid || !path || !deviceInfo)
        return false;
    for (index = 0; ; ++index) {
        SP_DEVICE_INTERFACE_DATA interfaceData;
        SP_DEVICE_INTERFACE_DETAIL_DATA_W* detail;
        SP_DEVINFO_DATA candidate;
        DWORD required = 0;
        bool match = false;
        memset(&interfaceData, 0, sizeof(interfaceData));
        interfaceData.cbSize = sizeof(interfaceData);
        if (!SetupDiEnumDeviceInterfaces(deviceSet, NULL, interfaceGuid,
                                         index, &interfaceData)) break;
        SetupDiGetDeviceInterfaceDetailW(deviceSet, &interfaceData,
                                         NULL, 0, &required, NULL);
        if (!required) continue;
        detail = (SP_DEVICE_INTERFACE_DETAIL_DATA_W*)calloc(1, required);
        if (!detail) return false;
        detail->cbSize = sizeof(*detail);
        memset(&candidate, 0, sizeof(candidate));
        candidate.cbSize = sizeof(candidate);
        if (SetupDiGetDeviceInterfaceDetailW(deviceSet, &interfaceData,
                                             detail, required, &required, &candidate) &&
            _wcsicmp(detail->DevicePath, path) == 0)
            match = true;
        free(detail);
        if (match) {
            *deviceInfo = candidate;
            return true;
        }
    }
    return false;
}

static bool xwinusbRestartDevice(XWinUsbDevice* device)
{
    SP_DEVINFO_DATA deviceInfo;
    SP_PROPCHANGE_PARAMS change;
    HDEVINFO deviceSet;
    const GUID* interfaceGuid;
    if (!device || !device->m_controller || !device->m_path) {
        if (device) xwinusbSetDeviceError(device, ERROR_NOT_FOUND);
        return false;
    }
    deviceSet = device->m_isHid ? device->m_controller->m_hidDeviceInfo
              : (device->m_isCdc ? device->m_controller->m_cdcDeviceInfo
                                 : device->m_controller->m_deviceInfo);
    interfaceGuid = device->m_isHid ? &device->m_controller->m_hidInterfaceGuid
                  : (device->m_isCdc ? &GUID_DEVINTERFACE_COMPORT
                                     : &GUID_DEVINTERFACE_USB_DEVICE);
    if (!xwinusbFindDeviceInfo(deviceSet, interfaceGuid, device->m_path, &deviceInfo)) {
        xwinusbSetDeviceError(device, ERROR_NOT_FOUND);
        return false;
    }
    memset(&change, 0, sizeof(change));
    change.ClassInstallHeader.cbSize = sizeof(change.ClassInstallHeader);
    change.ClassInstallHeader.InstallFunction = DIF_PROPERTYCHANGE;
    change.Scope = DICS_FLAG_GLOBAL;
    change.StateChange = DICS_DISABLE;
    if (!SetupDiSetClassInstallParamsW(deviceSet, &deviceInfo,
                                       &change.ClassInstallHeader, sizeof(change)) ||
        !SetupDiCallClassInstaller(DIF_PROPERTYCHANGE, deviceSet,
                                   &deviceInfo)) {
        xwinusbSetDeviceError(device, GetLastError());
        return false;
    }
    change.StateChange = DICS_ENABLE;
    if (!SetupDiSetClassInstallParamsW(deviceSet, &deviceInfo,
                                       &change.ClassInstallHeader, sizeof(change)) ||
        !SetupDiCallClassInstaller(DIF_PROPERTYCHANGE, deviceSet,
                                   &deviceInfo)) {
        xwinusbSetDeviceError(device, GetLastError());
        return false;
    }
    device->m_requiresReopen = true;
    xwinusbSetDeviceError(device, ERROR_DEVICE_NOT_CONNECTED);
    return true;
}

static void xwinusbFreeInterfaces(XWinUsbDevice* device)
{
    uint8_t i;
    if (!device) return;
    for (i = device->m_interfaceCount; i > 0; --i) {
        if (device->m_interfaces[i - 1u]) {
            WinUsb_Free(device->m_interfaces[i - 1u]);
            device->m_interfaces[i - 1u] = NULL;
        }
    }
    device->m_interfaceCount = 0;
}

static void xwinusbCloseDevice(XWinUsbDevice* device)
{
    XWinUsbTransfer* transfer;
    XWinUsbTransfer* next;
    if (!device) return;
    if (device->m_controller) {
        XWinUsbDevice** cursor = &device->m_controller->m_devices;
        while (*cursor && *cursor != device) cursor = &(*cursor)->m_nextControllerDevice;
        if (*cursor == device) *cursor = device->m_nextControllerDevice;
        device->m_controller = NULL;
        device->m_nextControllerDevice = NULL;
    }
    if (device->m_transferLockInitialized) {
        EnterCriticalSection(&device->m_transferLock);
        device->m_closing = true;
        for (transfer = device->m_transfers; transfer; transfer = transfer->m_next) {
            if (transfer->m_active)
                CancelIoEx(device->m_file, &transfer->m_io.base.overlapped);
        }
        LeaveCriticalSection(&device->m_transferLock);
        xwinusbDrainTransfers(device);
        EnterCriticalSection(&device->m_transferLock);
        transfer = device->m_transfers;
        device->m_transfers = NULL;
        LeaveCriticalSection(&device->m_transferLock);
        while (transfer) {
            next = transfer->m_next;
            if (transfer->m_isochBuffer)
                WinUsb_UnregisterIsochBuffer(transfer->m_isochBuffer);
            free(transfer->m_isochPackets);
            free(transfer);
            transfer = next;
        }
        DeleteCriticalSection(&device->m_transferLock);
        device->m_transferLockInitialized = false;
    }
    xwinusbFreeInterfaces(device);
    if (device->m_file && device->m_file != INVALID_HANDLE_VALUE) {
        CloseHandle(device->m_file);
        device->m_file = INVALID_HANDLE_VALUE;
    }
    free(device->m_path);
    free(device->m_configuration);
    free(device);
}

static void xwinusbClearConfiguration(XWinUsbDevice* device)
{
    if (!device) return;
    free(device->m_configuration);
    device->m_configuration = NULL;
    device->m_configurationLength = 0u;
    memset(&device->m_configurationDescriptor, 0, sizeof(device->m_configurationDescriptor));
}

static bool xwinusbReadConfiguration(XWinUsbDevice* device, uint8_t descriptorIndex)
{
    ULONG transferred = 0;
    ULONG totalLength;
    uint8_t header[sizeof(USB_CONFIGURATION_DESCRIPTOR)];
    USB_CONFIGURATION_DESCRIPTOR descriptor;
    uint8_t* configuration;

    if (!WinUsb_GetDescriptor(device->m_interfaces[0], USB_CONFIGURATION_DESCRIPTOR_TYPE,
                              descriptorIndex, 0, header, sizeof(header), &transferred))
        return false;
    if (transferred < sizeof(USB_CONFIGURATION_DESCRIPTOR)) {
        SetLastError(ERROR_INVALID_DATA);
        return false;
    }
    memcpy(&descriptor, header, sizeof(descriptor));
    totalLength = descriptor.wTotalLength;
    if (totalLength < sizeof(USB_CONFIGURATION_DESCRIPTOR) || totalLength > 65535u) {
        SetLastError(ERROR_INVALID_DATA);
        return false;
    }
    configuration = (uint8_t*)calloc(1, totalLength);
    if (!configuration) {
        SetLastError(ERROR_NOT_ENOUGH_MEMORY);
        return false;
    }
    if (!WinUsb_GetDescriptor(device->m_interfaces[0], USB_CONFIGURATION_DESCRIPTOR_TYPE,
                              descriptorIndex, 0, configuration, totalLength, &transferred) ||
        transferred < totalLength) {
        free(configuration);
        return false;
    }
    free(device->m_configuration);
    device->m_configuration = configuration;
    device->m_configurationDescriptor = descriptor;
    device->m_configurationLength = totalLength;
    return true;
}

static bool xwinusbReadConfigurationByValue(XWinUsbDevice* device, uint8_t value)
{
    uint8_t index;
    if (!device) return false;
    if (value == 0u) {
        xwinusbClearConfiguration(device);
        return true;
    }
    for (index = 0u; index < device->m_deviceDescriptor.bNumConfigurations; ++index) {
        if (!xwinusbReadConfiguration(device, index)) return false;
        if (device->m_configurationDescriptor.bConfigurationValue == value) return true;
    }
    xwinusbClearConfiguration(device);
    SetLastError(ERROR_NOT_FOUND);
    return false;
}

static bool xwinusbFillInfo(XWinUsbDevice* device)
{
    ULONG length = sizeof(device->m_deviceDescriptor);
    ULONG speed = 0;
    ULONG speedLength = sizeof(speed);
    WINUSB_SETUP_PACKET setup;
    UCHAR activeConfiguration = 0;

    memset(&device->m_deviceDescriptor, 0, sizeof(device->m_deviceDescriptor));
    if (!WinUsb_GetDescriptor(device->m_interfaces[0], USB_DEVICE_DESCRIPTOR_TYPE, 0, 0,
                              (PUCHAR)&device->m_deviceDescriptor,
                              sizeof(device->m_deviceDescriptor), &length))
        return false;
    memset(&setup, 0, sizeof(setup));
    setup.RequestType = 0x80u;
    setup.Request = 8u; /* GET_CONFIGURATION */
    setup.Length = 1u;
    if (!WinUsb_ControlTransfer(device->m_interfaces[0], setup, &activeConfiguration,
                                sizeof(activeConfiguration), &length, NULL) || length != 1u)
        return false;
    if (!xwinusbReadConfigurationByValue(device, activeConfiguration)) return false;
    memset(&device->m_info, 0, sizeof(device->m_info));
    device->m_info.m_backendId = xwinusbHashPath(device->m_path);
    device->m_info.m_vendorId = device->m_deviceDescriptor.idVendor;
    device->m_info.m_productId = device->m_deviceDescriptor.idProduct;
    device->m_info.m_bcdDevice = device->m_deviceDescriptor.bcdDevice;
    device->m_info.m_deviceClass = device->m_deviceDescriptor.bDeviceClass;
    device->m_info.m_deviceSubClass = device->m_deviceDescriptor.bDeviceSubClass;
    device->m_info.m_deviceProtocol = device->m_deviceDescriptor.bDeviceProtocol;
    device->m_info.m_configurationCount = device->m_deviceDescriptor.bNumConfigurations;
    device->m_info.m_activeConfiguration = activeConfiguration;
    device->m_info.m_speed = XDeviceUsbSpeed_Unknown;
    if (WinUsb_QueryDeviceInformation(device->m_interfaces[0], DEVICE_SPEED,
                                      &speedLength, &speed)) {
        if (speed == 1u) device->m_info.m_speed = XDeviceUsbSpeed_Low;
        else if (speed == 2u) device->m_info.m_speed = XDeviceUsbSpeed_Full;
        else if (speed == 3u) device->m_info.m_speed = XDeviceUsbSpeed_High;
        else if (speed >= 4u) device->m_info.m_speed = XDeviceUsbSpeed_Super;
    }
    return true;
}

static XWinUsbDevice* xwinusbOpenPath(const wchar_t* path)
{
    XWinUsbDevice* device;
    HANDLE file;
    DWORD error;

    if (!path) return NULL;
    file = CreateFileW(path, GENERIC_READ | GENERIC_WRITE,
                       FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING,
                       FILE_FLAG_OVERLAPPED, NULL);
    if (file == INVALID_HANDLE_VALUE) return NULL;
    device = (XWinUsbDevice*)calloc(1, sizeof(*device));
    if (!device) {
        CloseHandle(file);
        SetLastError(ERROR_NOT_ENOUGH_MEMORY);
        return NULL;
    }
    device->m_file = file;
    InitializeCriticalSection(&device->m_transferLock);
    device->m_transferLockInitialized = true;
    device->m_nextTransferId = 1;
    device->m_path = (wchar_t*)calloc(wcslen(path) + 1u, sizeof(wchar_t));
    if (!device->m_path) {
        xwinusbCloseDevice(device);
        SetLastError(ERROR_NOT_ENOUGH_MEMORY);
        return NULL;
    }
    memcpy(device->m_path, path, (wcslen(path) + 1u) * sizeof(wchar_t));
    if (!WinUsb_Initialize(file, &device->m_interfaces[0])) {
        error = GetLastError();
        xwinusbCloseDevice(device);
        SetLastError(error);
        return NULL;
    }
    device->m_interfaceCount = 1;
    while (device->m_interfaceCount < XWINUSB_MAX_INTERFACES) {
        WINUSB_INTERFACE_HANDLE associated = NULL;
        if (!WinUsb_GetAssociatedInterface(device->m_interfaces[0],
                                           (UCHAR)(device->m_interfaceCount - 1u),
                                           &associated))
            break;
        device->m_interfaces[device->m_interfaceCount++] = associated;
    }
    if (!xwinusbFillInfo(device)) {
        error = GetLastError();
        xwinusbCloseDevice(device);
        SetLastError(error);
        return NULL;
    }
    return device;
}

static XWinUsbDevice* xwinusbOpenHidPath(const wchar_t* path)
{
    XWinUsbDevice* device;
    HANDLE file;
    HIDD_ATTRIBUTES attributes;
    PHIDP_PREPARSED_DATA preparsed = NULL;
    NTSTATUS status;
    DWORD error;

    if (!path) return NULL;
    file = CreateFileW(path, GENERIC_READ | GENERIC_WRITE,
                       FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING,
                       FILE_FLAG_OVERLAPPED, NULL);
    if (file == INVALID_HANDLE_VALUE) {
        file = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
                           NULL, OPEN_EXISTING, FILE_FLAG_OVERLAPPED, NULL);
    }
    if (file == INVALID_HANDLE_VALUE) return NULL;
    device = (XWinUsbDevice*)calloc(1, sizeof(*device));
    if (!device) {
        CloseHandle(file);
        SetLastError(ERROR_NOT_ENOUGH_MEMORY);
        return NULL;
    }
    device->m_file = file;
    device->m_isHid = true;
    InitializeCriticalSection(&device->m_transferLock);
    device->m_transferLockInitialized = true;
    device->m_nextTransferId = 1;
    device->m_path = xwinusbDuplicatePath(path);
    if (!device->m_path) {
        xwinusbCloseDevice(device);
        SetLastError(ERROR_NOT_ENOUGH_MEMORY);
        return NULL;
    }
    memset(&attributes, 0, sizeof(attributes));
    attributes.Size = sizeof(attributes);
    if (!HidD_GetAttributes(file, &attributes) ||
        !HidD_GetPreparsedData(file, &preparsed)) {
        error = GetLastError();
        xwinusbCloseDevice(device);
        SetLastError(error);
        return NULL;
    }
    status = HidP_GetCaps(preparsed, &device->m_hidCaps);
    HidD_FreePreparsedData(preparsed);
    if (status != HIDP_STATUS_SUCCESS) {
        xwinusbCloseDevice(device);
        SetLastError(ERROR_INVALID_DATA);
        return NULL;
    }
    memset(&device->m_info, 0, sizeof(device->m_info));
    device->m_info.m_backendId = xwinusbHashPath(device->m_path);
    device->m_info.m_vendorId = attributes.VendorID;
    device->m_info.m_productId = attributes.ProductID;
    device->m_info.m_bcdDevice = attributes.VersionNumber;
    device->m_info.m_deviceClass = 3u;
    device->m_info.m_configurationCount = 1u;
    device->m_info.m_activeConfiguration = 1u;
    device->m_info.m_speed = XDeviceUsbSpeed_Unknown;
    return device;
}

static bool xwinusbGetDeviceProperty(HDEVINFO deviceSet, SP_DEVINFO_DATA* deviceInfo,
                                     DWORD property, wchar_t** value)
{
    DWORD required = 0;
    DWORD type = 0;
    wchar_t* buffer;
    if (!deviceSet || deviceSet == INVALID_HANDLE_VALUE || !deviceInfo || !value) return false;
    *value = NULL;
    SetupDiGetDeviceRegistryPropertyW(deviceSet, deviceInfo, property, &type,
                                      NULL, 0, &required);
    if (required == 0u) return false;
    buffer = (wchar_t*)calloc(1, required + sizeof(wchar_t));
    if (!buffer) return false;
    if (!SetupDiGetDeviceRegistryPropertyW(deviceSet, deviceInfo, property, &type,
                                           (PBYTE)buffer, required, &required)) {
        free(buffer);
        return false;
    }
    *value = buffer;
    return true;
}

static bool xwinusbIsCdcDevice(HDEVINFO deviceSet, SP_DEVINFO_DATA* deviceInfo)
{
    wchar_t* compatibleIds = NULL;
    const wchar_t* item;
    bool isCdc = false;
    if (!xwinusbGetDeviceProperty(deviceSet, deviceInfo, SPDRP_COMPATIBLEIDS,
                                  &compatibleIds))
        return false;
    for (item = compatibleIds; *item; item += wcslen(item) + 1u) {
        if (_wcsnicmp(item, L"USB\\Class_02", 12u) == 0) {
            isCdc = true;
            break;
        }
    }
    free(compatibleIds);
    return isCdc;
}

static bool xwinusbParseHex16(const wchar_t* text, uint16_t* value)
{
    uint16_t result = 0;
    size_t i;
    if (!text || !value) return false;
    for (i = 0; i < 4u; ++i) {
        wchar_t c = text[i];
        uint16_t digit;
        if (c >= L'0' && c <= L'9') digit = (uint16_t)(c - L'0');
        else if (c >= L'a' && c <= L'f') digit = (uint16_t)(c - L'a' + 10u);
        else if (c >= L'A' && c <= L'F') digit = (uint16_t)(c - L'A' + 10u);
        else return false;
        result = (uint16_t)((result << 4u) | digit);
    }
    *value = result;
    return true;
}

static void xwinusbGetCdcHardwareIds(HDEVINFO deviceSet, SP_DEVINFO_DATA* deviceInfo,
                                     XDeviceUsbDeviceInfo* info)
{
    wchar_t* hardwareIds = NULL;
    const wchar_t* item;
    if (!info || !xwinusbGetDeviceProperty(deviceSet, deviceInfo, SPDRP_HARDWAREID,
                                           &hardwareIds))
        return;
    for (item = hardwareIds; *item; item += wcslen(item) + 1u) {
        const wchar_t* marker;
        marker = wcsstr(item, L"VID_");
        if (marker) (void)xwinusbParseHex16(marker + 4u, &info->m_vendorId);
        marker = wcsstr(item, L"PID_");
        if (marker) (void)xwinusbParseHex16(marker + 4u, &info->m_productId);
        marker = wcsstr(item, L"REV_");
        if (marker) (void)xwinusbParseHex16(marker + 4u, &info->m_bcdDevice);
        if (info->m_vendorId != 0u || info->m_productId != 0u) break;
    }
    free(hardwareIds);
}

static bool xwinusbGetCdcPortName(HDEVINFO deviceSet, SP_DEVINFO_DATA* deviceInfo,
                                  wchar_t** portName)
{
    HKEY key;
    DWORD type = 0;
    DWORD size = 0;
    wchar_t* value;
    LONG result;
    if (!deviceSet || deviceSet == INVALID_HANDLE_VALUE || !deviceInfo || !portName) return false;
    *portName = NULL;
    key = SetupDiOpenDevRegKey(deviceSet, deviceInfo, DICS_FLAG_GLOBAL, 0u,
                               DIREG_DEV, KEY_QUERY_VALUE);
    if (key == INVALID_HANDLE_VALUE) return false;
    result = RegQueryValueExW(key, L"PortName", NULL, &type, NULL, &size);
    if (result != ERROR_SUCCESS || type != REG_SZ || size < sizeof(wchar_t)) {
        RegCloseKey(key);
        return false;
    }
    value = (wchar_t*)calloc(1, size + sizeof(wchar_t));
    if (!value) {
        RegCloseKey(key);
        return false;
    }
    result = RegQueryValueExW(key, L"PortName", NULL, &type, (BYTE*)value, &size);
    RegCloseKey(key);
    if (result != ERROR_SUCCESS) {
        free(value);
        return false;
    }
    *portName = value;
    return true;
}

static XWinUsbDevice* xwinusbOpenCdcPath(HDEVINFO deviceSet, const wchar_t* path)
{
    SP_DEVINFO_DATA deviceInfo;
    wchar_t* portName = NULL;
    wchar_t* openPath = NULL;
    XWinUsbDevice* device;
    HANDLE file;
    DCB dcb;
    size_t length;
    if (!path || !xwinusbFindDeviceInfo(deviceSet, &GUID_DEVINTERFACE_COMPORT,
                                        path, &deviceInfo) ||
        !xwinusbIsCdcDevice(deviceSet, &deviceInfo) ||
        !xwinusbGetCdcPortName(deviceSet, &deviceInfo, &portName))
        return NULL;
    length = wcslen(portName) + 5u;
    openPath = (wchar_t*)calloc(length, sizeof(wchar_t));
    if (!openPath) {
        free(portName);
        SetLastError(ERROR_NOT_ENOUGH_MEMORY);
        return NULL;
    }
    swprintf(openPath, length, L"\\\\.\\%s", portName);
    file = CreateFileW(openPath, GENERIC_READ | GENERIC_WRITE, 0u, NULL, OPEN_EXISTING,
                       FILE_FLAG_OVERLAPPED, NULL);
    free(openPath);
    if (file == INVALID_HANDLE_VALUE) {
        free(portName);
        return NULL;
    }
    device = (XWinUsbDevice*)calloc(1, sizeof(*device));
    if (!device) {
        CloseHandle(file);
        free(portName);
        SetLastError(ERROR_NOT_ENOUGH_MEMORY);
        return NULL;
    }
    device->m_file = file;
    device->m_isCdc = true;
    InitializeCriticalSection(&device->m_transferLock);
    device->m_transferLockInitialized = true;
    device->m_nextTransferId = 1;
    device->m_path = xwinusbDuplicatePath(path);
    if (!device->m_path) {
        free(portName);
        xwinusbCloseDevice(device);
        SetLastError(ERROR_NOT_ENOUGH_MEMORY);
        return NULL;
    }
    memset(&dcb, 0, sizeof(dcb));
    dcb.DCBlength = sizeof(dcb);
    if (!GetCommState(file, &dcb)) {
        DWORD error = GetLastError();
        free(portName);
        xwinusbCloseDevice(device);
        SetLastError(error);
        return NULL;
    }
    SetupComm(file, 4096u, 4096u);
    memset(&device->m_info, 0, sizeof(device->m_info));
    device->m_info.m_backendId = xwinusbHashPath(device->m_path);
    device->m_info.m_deviceClass = 2u;
    device->m_info.m_deviceSubClass = 2u;
    device->m_info.m_deviceProtocol = 1u;
    device->m_info.m_configurationCount = 1u;
    device->m_info.m_activeConfiguration = 1u;
    xwinusbGetCdcHardwareIds(deviceSet, &deviceInfo, &device->m_info);
    free(portName);
    return device;
}

static bool xwinusbGetInterfaceOrdinal(XWinUsbDevice* device, uint8_t interfaceNumber,
                                        uint8_t* ordinal)
{
    size_t offset = 0;
    uint8_t nextOrdinal = 0;
    uint8_t lastNumber = 0xffu;
    bool found = false;
    if (!device || !ordinal) return false;
    if (device->m_isHid || device->m_isCdc) {
        if (interfaceNumber != 0u) return false;
        *ordinal = 0u;
        return true;
    }
    if (!device->m_configuration) return false;
    while (offset + 2u <= device->m_configurationLength) {
        uint8_t length = device->m_configuration[offset];
        uint8_t type = device->m_configuration[offset + 1u];
        if (length < 2u || offset + length > device->m_configurationLength) break;
        if (type == USB_INTERFACE_DESCRIPTOR_TYPE && length >= sizeof(USB_INTERFACE_DESCRIPTOR)) {
            uint8_t number = device->m_configuration[offset + 2u];
            uint8_t alternate = device->m_configuration[offset + 3u];
            if (alternate == 0u && number != lastNumber) {
                if (number == interfaceNumber) {
                    *ordinal = nextOrdinal;
                    found = true;
                }
                lastNumber = number;
                ++nextOrdinal;
            }
        }
        offset += length;
    }
    return found && *ordinal < device->m_interfaceCount;
}

static WINUSB_INTERFACE_HANDLE xwinusbGetInterface(XWinUsbDevice* device,
                                                   uint8_t interfaceNumber)
{
    uint8_t ordinal;
    if (!xwinusbGetInterfaceOrdinal(device, interfaceNumber, &ordinal)) return NULL;
    return device->m_interfaces[ordinal];
}

static bool xwinusbGetInterfaceIndex(XWinUsbDevice* device, uint8_t interfaceNumber,
                                     uint8_t* index)
{
    return xwinusbGetInterfaceOrdinal(device, interfaceNumber, index);
}

static bool xwinusbFindPipe(XWinUsbDevice* device, XDeviceUsbEndpointAddress endpoint,
                            WINUSB_INTERFACE_HANDLE* interfaceHandle,
                            WINUSB_PIPE_INFORMATION* pipe)
{
    uint8_t interfaceIndex;
    if (!device || !interfaceHandle || !pipe) return false;
    for (interfaceIndex = 0; interfaceIndex < device->m_interfaceCount; ++interfaceIndex) {
        USB_INTERFACE_DESCRIPTOR descriptor;
        UCHAR pipeIndex;
        UCHAR alternate = device->m_currentAlternate[interfaceIndex];
        if (!WinUsb_QueryInterfaceSettings(device->m_interfaces[interfaceIndex], alternate,
                                           &descriptor))
            continue;
        for (pipeIndex = 0; pipeIndex < descriptor.bNumEndpoints; ++pipeIndex) {
            WINUSB_PIPE_INFORMATION candidate;
            if (WinUsb_QueryPipe(device->m_interfaces[interfaceIndex], alternate, pipeIndex, &candidate) &&
                candidate.PipeId == endpoint) {
                *interfaceHandle = device->m_interfaces[interfaceIndex];
                *pipe = candidate;
                return true;
            }
        }
    }
    return false;
}

static size_t xwinusbHidEndpointCount(const XWinUsbDevice* device)
{
    size_t count = 0;
    if (!device || !device->m_isHid) return 0;
    if (device->m_hidCaps.InputReportByteLength != 0u) ++count;
    if (device->m_hidCaps.OutputReportByteLength != 0u) ++count;
    return count;
}

static bool xwinusbHidEndpointInfo(const XWinUsbDevice* device, size_t index,
                                   XDeviceUsbEndpointInfo* endpoint)
{
    bool hasInput;
    if (!device || !device->m_isHid || !endpoint) return false;
    hasInput = device->m_hidCaps.InputReportByteLength != 0u;
    memset(endpoint, 0, sizeof(*endpoint));
    endpoint->m_transferType = XDeviceUsbTransferType_Interrupt;
    endpoint->m_interfaceNumber = 0u;
    endpoint->m_alternateSetting = 0u;
    if (hasInput && index == 0u) {
        endpoint->m_address = 0x81u;
        endpoint->m_maxPacketSize = device->m_hidCaps.InputReportByteLength;
        endpoint->m_maxTransferSize = device->m_hidCaps.InputReportByteLength;
        return true;
    }
    if (index == (hasInput ? 1u : 0u) && device->m_hidCaps.OutputReportByteLength != 0u) {
        endpoint->m_address = 0x01u;
        endpoint->m_maxPacketSize = device->m_hidCaps.OutputReportByteLength;
        endpoint->m_maxTransferSize = device->m_hidCaps.OutputReportByteLength;
        return true;
    }
    return false;
}

static XDeviceUsbTransferResult xwinusbHidControlTransfer(
    XWinUsbDevice* device, const XDeviceUsbControlRequest* request, void* data,
    size_t capacity, size_t* transferred)
{
    bool input;
    bool ok;
    USHORT reportLength;
    if (!device || !request || !data || request->m_index != 0u ||
        (request->m_requestType & 0x60u) != 0x20u ||
        (request->m_requestType & 0x1fu) != 0x01u ||
        (request->m_value >> 8u) != 3u || request->m_length == 0u ||
        request->m_length > capacity || ((uint8_t*)data)[0] != (uint8_t)request->m_value) {
        return XDeviceUsbTransferResult_InvalidArgument;
    }
    input = (request->m_requestType & 0x80u) != 0u;
    if ((input && request->m_request != 1u) || (!input && request->m_request != 9u)) {
        xwinusbSetDeviceError(device, ERROR_NOT_SUPPORTED);
        return XDeviceUsbTransferResult_Unsupported;
    }
    reportLength = device->m_hidCaps.FeatureReportByteLength;
    if (reportLength == 0u || request->m_length != reportLength) {
        xwinusbSetDeviceError(device, ERROR_INVALID_PARAMETER);
        return XDeviceUsbTransferResult_InvalidArgument;
    }
    ok = input ? HidD_GetFeature(device->m_file, data, reportLength) != FALSE
               : HidD_SetFeature(device->m_file, data, reportLength) != FALSE;
    if (!ok) {
        DWORD error = GetLastError();
        xwinusbSetDeviceError(device, error);
        return xwinusbMapTransferError(error);
    }
    if (transferred) *transferred = reportLength;
    return XDeviceUsbTransferResult_Ok;
}

static XDeviceUsbTransferResult xwinusbHidTransfer(XWinUsbDevice* device,
                                                   XDeviceUsbEndpointAddress endpoint,
                                                   void* data, size_t length,
                                                   size_t* transferred,
                                                   int32_t timeoutMs)
{
    DWORD actual = 0;
    DWORD expected;
    BOOL ok;
    if (!device || device->m_requiresReopen || !data) return XDeviceUsbTransferResult_InvalidArgument;
    if (timeoutMs > 0) {
        xwinusbSetDeviceError(device, ERROR_NOT_SUPPORTED);
        return XDeviceUsbTransferResult_Unsupported;
    }
    if (endpoint == 0x81u)
        expected = device->m_hidCaps.InputReportByteLength;
    else if (endpoint == 0x01u)
        expected = device->m_hidCaps.OutputReportByteLength;
    else {
        xwinusbSetDeviceError(device, ERROR_FILE_NOT_FOUND);
        return XDeviceUsbTransferResult_NoDevice;
    }
    if (expected == 0u || length != expected) {
        xwinusbSetDeviceError(device, ERROR_INVALID_PARAMETER);
        return XDeviceUsbTransferResult_InvalidArgument;
    }
    ok = endpoint == 0x81u
        ? ReadFile(device->m_file, data, expected, &actual, NULL)
        : WriteFile(device->m_file, data, expected, &actual, NULL);
    if (!ok) {
        DWORD error = GetLastError();
        xwinusbSetDeviceError(device, error);
        return xwinusbMapTransferError(error);
    }
    if (transferred) *transferred = actual;
    return XDeviceUsbTransferResult_Ok;
}

static XDeviceUsbTransferResult xwinusbCdcControlTransfer(
    XWinUsbDevice* device, const XDeviceUsbControlRequest* request, void* data,
    size_t capacity, size_t* transferred)
{
    DCB dcb;
    uint8_t* bytes = (uint8_t*)data;
    if (!device || !request || (request->m_requestType & 0x60u) != 0x20u ||
        (request->m_requestType & 0x1fu) != 0x01u || request->m_index != 0u) {
        return XDeviceUsbTransferResult_InvalidArgument;
    }
    if (request->m_request == 0x20u) {
        if ((request->m_requestType & 0x80u) != 0u || !data || request->m_length != 7u ||
            capacity < 7u) return XDeviceUsbTransferResult_InvalidArgument;
        memset(&dcb, 0, sizeof(dcb));
        dcb.DCBlength = sizeof(dcb);
        if (!GetCommState(device->m_file, &dcb)) goto error;
        dcb.BaudRate = (DWORD)bytes[0] | ((DWORD)bytes[1] << 8u) |
                       ((DWORD)bytes[2] << 16u) | ((DWORD)bytes[3] << 24u);
        if (bytes[1] > 2u || bytes[2] > 4u || bytes[3] < 5u || bytes[3] > 8u) {
            xwinusbSetDeviceError(device, ERROR_INVALID_PARAMETER);
            return XDeviceUsbTransferResult_InvalidArgument;
        }
        dcb.StopBits = bytes[1] == 0u ? ONESTOPBIT :
                       (bytes[1] == 1u ? ONE5STOPBITS : TWOSTOPBITS);
        dcb.Parity = bytes[2];
        dcb.ByteSize = bytes[3];
        if (!SetCommState(device->m_file, &dcb)) goto error;
        if (transferred) *transferred = 7u;
        return XDeviceUsbTransferResult_Ok;
    }
    if (request->m_request == 0x21u) {
        if ((request->m_requestType & 0x80u) == 0u || !data || request->m_length != 7u ||
            capacity < 7u) return XDeviceUsbTransferResult_InvalidArgument;
        memset(&dcb, 0, sizeof(dcb));
        dcb.DCBlength = sizeof(dcb);
        if (!GetCommState(device->m_file, &dcb)) goto error;
        bytes[0] = (uint8_t)dcb.BaudRate;
        bytes[1] = (uint8_t)(dcb.BaudRate >> 8u);
        bytes[2] = (uint8_t)(dcb.BaudRate >> 16u);
        bytes[3] = (uint8_t)(dcb.BaudRate >> 24u);
        bytes[4] = dcb.StopBits == ONESTOPBIT ? 0u :
                   (dcb.StopBits == ONE5STOPBITS ? 1u : 2u);
        bytes[5] = dcb.Parity;
        bytes[6] = dcb.ByteSize;
        if (transferred) *transferred = 7u;
        return XDeviceUsbTransferResult_Ok;
    }
    if (request->m_request == 0x22u) {
        bool dtr;
        bool rts;
        if ((request->m_requestType & 0x80u) != 0u || request->m_length != 0u)
            return XDeviceUsbTransferResult_InvalidArgument;
        dtr = (request->m_value & 0x01u) != 0u;
        rts = (request->m_value & 0x02u) != 0u;
        if (!EscapeCommFunction(device->m_file, dtr ? SETDTR : CLRDTR) ||
            !EscapeCommFunction(device->m_file, rts ? SETRTS : CLRRTS)) goto error;
        return XDeviceUsbTransferResult_Ok;
    }
    if (request->m_request == 0x23u) {
        if ((request->m_requestType & 0x80u) != 0u || request->m_length != 0u)
            return XDeviceUsbTransferResult_InvalidArgument;
        if (!(request->m_value == 0u ? ClearCommBreak(device->m_file)
                                     : SetCommBreak(device->m_file))) goto error;
        return XDeviceUsbTransferResult_Ok;
    }
    xwinusbSetDeviceError(device, ERROR_NOT_SUPPORTED);
    return XDeviceUsbTransferResult_Unsupported;
error:
    xwinusbSetDeviceError(device, GetLastError());
    return xwinusbMapTransferError(device->m_lastError);
}

static XDeviceUsbTransferResult xwinusbCdcTransfer(XWinUsbDevice* device,
                                                   XDeviceUsbEndpointAddress endpoint,
                                                   void* data, size_t length,
                                                   size_t* transferred,
                                                   int32_t timeoutMs)
{
    DWORD actual = 0;
    BOOL ok;
    if (!device || device->m_requiresReopen || !data || length == 0u || length > MAXDWORD)
        return XDeviceUsbTransferResult_InvalidArgument;
    if (timeoutMs > 0) {
        xwinusbSetDeviceError(device, ERROR_NOT_SUPPORTED);
        return XDeviceUsbTransferResult_Unsupported;
    }
    if (endpoint != 0x81u && endpoint != 0x01u) {
        xwinusbSetDeviceError(device, ERROR_FILE_NOT_FOUND);
        return XDeviceUsbTransferResult_NoDevice;
    }
    ok = endpoint == 0x81u ? ReadFile(device->m_file, data, (DWORD)length, &actual, NULL)
                            : WriteFile(device->m_file, data, (DWORD)length, &actual, NULL);
    if (!ok) {
        xwinusbSetDeviceError(device, GetLastError());
        return xwinusbMapTransferError(device->m_lastError);
    }
    if (transferred) *transferred = actual;
    return XDeviceUsbTransferResult_Ok;
}

static XDeviceUsbTransferResult xwinusbControlTransfer(XWinUsbDevice* device,
                                                       const XDeviceUsbControlRequest* request,
                                                       void* data, size_t capacity,
                                                       size_t* transferred, int32_t timeoutMs)
{
    WINUSB_SETUP_PACKET setup;
    ULONG actual = 0;
    BOOL result;
    if (!device || device->m_requiresReopen || !request || capacity > 0xffffu ||
        (data == NULL && capacity != 0))
        return XDeviceUsbTransferResult_InvalidArgument;
    if (timeoutMs > 0) {
        xwinusbSetDeviceError(device, ERROR_NOT_SUPPORTED);
        return XDeviceUsbTransferResult_Unsupported;
    }
    if (device->m_isHid)
        return xwinusbHidControlTransfer(device, request, data, capacity, transferred);
    if (device->m_isCdc)
        return xwinusbCdcControlTransfer(device, request, data, capacity, transferred);
    memset(&setup, 0, sizeof(setup));
    setup.RequestType = request->m_requestType;
    setup.Request = request->m_request;
    setup.Value = request->m_value;
    setup.Index = request->m_index;
    setup.Length = request->m_length;
    if (setup.Length > capacity) setup.Length = (USHORT)capacity;
    result = WinUsb_ControlTransfer(device->m_interfaces[0], setup, (PUCHAR)data,
                                    setup.Length, &actual, NULL);
    if (!result) {
        DWORD error = GetLastError();
        xwinusbSetDeviceError(device, error);
        return xwinusbMapTransferError(error);
    }
    if (transferred) *transferred = actual;
    return XDeviceUsbTransferResult_Ok;
}

static XDeviceUsbTransferResult xwinusbPipeTransfer(XWinUsbDevice* device,
                                                    XDeviceUsbEndpointAddress endpoint,
                                                    void* data, size_t length,
                                                    size_t* transferred, int32_t timeoutMs)
{
    WINUSB_INTERFACE_HANDLE interfaceHandle;
    WINUSB_PIPE_INFORMATION pipe;
    ULONG actual = 0;
    BOOL result;
    UCHAR pipeId = endpoint;
    if (!device || endpoint == 0 || length > ULONG_MAX || (data == NULL && length != 0))
        return XDeviceUsbTransferResult_InvalidArgument;
    if (timeoutMs > 0) {
        xwinusbSetDeviceError(device, ERROR_NOT_SUPPORTED);
        return XDeviceUsbTransferResult_Unsupported;
    }
    if (!xwinusbFindPipe(device, endpoint, &interfaceHandle, &pipe)) {
        xwinusbSetDeviceError(device, ERROR_FILE_NOT_FOUND);
        return XDeviceUsbTransferResult_NoDevice;
    }
    if (pipe.PipeType != UsbdPipeTypeBulk && pipe.PipeType != UsbdPipeTypeInterrupt)
        return XDeviceUsbTransferResult_Unsupported;
    if (endpoint & 0x80u)
        result = WinUsb_ReadPipe(interfaceHandle, pipeId, (PUCHAR)data, (ULONG)length,
                                 &actual, NULL);
    else
        result = WinUsb_WritePipe(interfaceHandle, pipeId, (PUCHAR)data, (ULONG)length,
                                  &actual, NULL);
    if (!result) {
        DWORD error = GetLastError();
        xwinusbSetDeviceError(device, error);
        return xwinusbMapTransferError(error);
    }
    if (transferred) *transferred = actual;
    return XDeviceUsbTransferResult_Ok;
}

static XDeviceUsbTransferResult xwinusbIsochTransfer(XWinUsbDevice* device,
                                                     XDeviceUsbEndpointAddress endpoint,
                                                     void* data, size_t length,
                                                     size_t* transferred,
                                                     int32_t timeoutMs)
{
    WINUSB_INTERFACE_HANDLE interfaceHandle;
    WINUSB_PIPE_INFORMATION pipe;
    WINUSB_ISOCH_BUFFER_HANDLE bufferHandle = NULL;
    PUSBD_ISO_PACKET_DESCRIPTOR packets = NULL;
    ULONG packetCount;
    ULONG actual = 0;
    ULONG i;
    bool ok;
    if (!device || endpoint == 0 || length > ULONG_MAX ||
        (data == NULL && length != 0))
        return XDeviceUsbTransferResult_InvalidArgument;
    if (timeoutMs > 0) {
        xwinusbSetDeviceError(device, ERROR_NOT_SUPPORTED);
        return XDeviceUsbTransferResult_Unsupported;
    }
    if (!xwinusbFindPipe(device, endpoint, &interfaceHandle, &pipe) ||
        pipe.PipeType != UsbdPipeTypeIsochronous) {
        xwinusbSetDeviceError(device, ERROR_FILE_NOT_FOUND);
        return XDeviceUsbTransferResult_NoDevice;
    }
    if (pipe.MaximumPacketSize == 0) {
        xwinusbSetDeviceError(device, ERROR_INVALID_DATA);
        return XDeviceUsbTransferResult_InvalidArgument;
    }
    packetCount = (ULONG)((length + pipe.MaximumPacketSize - 1u) /
                          pipe.MaximumPacketSize);
    if (packetCount == 0) packetCount = 1;
    packets = (PUSBD_ISO_PACKET_DESCRIPTOR)calloc(packetCount, sizeof(*packets));
    if (!packets) return XDeviceUsbTransferResult_ResourceError;
    if (!WinUsb_RegisterIsochBuffer(interfaceHandle, endpoint, (PUCHAR)data,
                                    (ULONG)length, &bufferHandle)) {
        DWORD error = GetLastError();
        free(packets);
        xwinusbSetDeviceError(device, error);
        return xwinusbMapTransferError(error);
    }
    if (XDEVICE_USB_ENDPOINT_IS_IN(endpoint)) {
        ULONG frame = 0;
        ok = WinUsb_ReadIsochPipe(bufferHandle, 0, (ULONG)length, &frame,
                                  packetCount, packets, NULL) != FALSE;
        for (i = 0; ok && i < packetCount; ++i)
            actual += packets[i].Length;
    } else {
        ok = WinUsb_WriteIsochPipeAsap(bufferHandle, 0, (ULONG)length, FALSE,
                                       NULL) != FALSE;
        actual = ok ? (ULONG)length : 0;
    }
    WinUsb_UnregisterIsochBuffer(bufferHandle);
    free(packets);
    if (!ok) {
        DWORD error = GetLastError();
        xwinusbSetDeviceError(device, error);
        return xwinusbMapTransferError(error);
    }
    if (transferred) *transferred = actual;
    return XDeviceUsbTransferResult_Ok;
}

static bool xwinusbEnsureIocp(XWinUsbDevice* device)
{
    XAbstractNetIoRing* ring;
    if (!device) return false;
    if (device->m_iocpAssociated) return true;
    ring = XAbstractNetIoRing_global();
    if (!ring || !XAbstractNetIoRing_isEnabled(ring)) return false;
    if (!XNetIoRingWin32_assocHandle((XNetIoRingWin32*)ring, device->m_file,
                                     (ULONG_PTR)device)) return false;
    device->m_iocpAssociated = true;
    return true;
}

static bool xwinusbIoCompletion(XEventContext_IOCP* context, void* userData)
{
    XWinUsbTransfer* transfer = (XWinUsbTransfer*)userData;
    XWinUsbDevice* device;
    XWinUsbTransfer** cursor;
    XDeviceUsbTransferCallback callback;
    XDeviceUsbTransferId transferId;
    void* callbackUserData;
    XDeviceUsbTransferEvent event;
    bool closing;
    ULONG i;

    if (!transfer || !(device = transfer->m_device)) return false;
    if (transfer->m_isochRead && transfer->m_isochPackets) {
        context->finishedBytes = 0;
        for (i = 0; i < transfer->m_isochPacketCount; ++i)
            if (transfer->m_isochPackets[i].Status == USBD_STATUS_SUCCESS)
                context->finishedBytes += transfer->m_isochPackets[i].Length;
    }
    event.m_result = transfer->m_forcedFailure
        ? xwinusbMapTransferError(transfer->m_submitError)
        : (transfer->m_completionErrorOverride != ERROR_SUCCESS
            ? xwinusbMapTransferError(transfer->m_completionErrorOverride)
            : (context->completed ? XDeviceUsbTransferResult_Ok
                                   : xwinusbMapTransferError(context->nativeError)));
    event.m_transferred = context->finishedBytes;
    event.m_context = XDeviceUsbCallbackContext_Process;

    EnterCriticalSection(&device->m_transferLock);
    transfer->m_active = false;
    callback = transfer->m_callback;
    transferId = transfer->m_id;
    callbackUserData = transfer->m_userData;
    closing = device->m_closing;
    cursor = &device->m_transfers;
    while (*cursor && *cursor != transfer) cursor = &(*cursor)->m_next;
    if (*cursor == transfer) *cursor = transfer->m_next;
    LeaveCriticalSection(&device->m_transferLock);

    if (!closing && callback) callback(device, transferId, &event, callbackUserData);
    if (transfer->m_isochBuffer)
        WinUsb_UnregisterIsochBuffer(transfer->m_isochBuffer);
    free(transfer->m_isochPackets);
    free(transfer);
    return false;
}

static void xwinusbDrainTransfers(XWinUsbDevice* device)
{
    XAbstractNetIoRing* ring;
    bool hasActive;
    if (!device || !device->m_iocpAssociated) return;
    ring = XAbstractNetIoRing_global();
    if (!ring) return;
    do {
        hasActive = false;
        EnterCriticalSection(&device->m_transferLock);
        {
            XWinUsbTransfer* transfer;
            for (transfer = device->m_transfers; transfer; transfer = transfer->m_next) {
                if (transfer->m_active) {
                    hasActive = true;
                    break;
                }
            }
        }
        LeaveCriticalSection(&device->m_transferLock);
        if (hasActive) {
            XAbstractNetIoRing_pollPlatform_base(ring);
            Sleep(1);
        }
    } while (hasActive);
}

static bool xwinusbPostFailedTransfer(XWinUsbTransfer* transfer, DWORD error)
{
    XAbstractNetIoRing* ring;
    if (!transfer || !transfer->m_device) return false;
    ring = XAbstractNetIoRing_global();
    transfer->m_submitError = error;
    transfer->m_forcedFailure = true;
    if (!ring || !XNetIoRingWin32_postCompletion((XNetIoRingWin32*)ring, 0,
                                                  (ULONG_PTR)transfer->m_device,
                                                  &transfer->m_io.base.overlapped))
        return false;
    return true;
}

static bool xwinusbSubmitPipe(XWinUsbTransfer* transfer)
{
    WINUSB_INTERFACE_HANDLE interfaceHandle;
    WINUSB_PIPE_INFORMATION pipe;
    BOOL submitted;
    DWORD error;
    XWinUsbDevice* device = transfer->m_device;

    if (device->m_isCdc) {
        if (transfer->m_request.m_transferType != XDeviceUsbTransferType_Bulk ||
            transfer->m_request.m_length == 0u || transfer->m_request.m_length > MAXDWORD ||
            (transfer->m_request.m_endpoint != 0x81u && transfer->m_request.m_endpoint != 0x01u)) {
            xwinusbSetDeviceError(device, ERROR_INVALID_PARAMETER);
            return xwinusbPostFailedTransfer(transfer, ERROR_INVALID_PARAMETER);
        }
        submitted = transfer->m_request.m_endpoint == 0x81u
            ? ReadFile(device->m_file, transfer->m_request.m_data,
                       (DWORD)transfer->m_request.m_length, NULL,
                       &transfer->m_io.base.overlapped)
            : WriteFile(device->m_file, transfer->m_request.m_data,
                        (DWORD)transfer->m_request.m_length, NULL,
                        &transfer->m_io.base.overlapped);
        if (submitted || GetLastError() == ERROR_IO_PENDING) return true;
        error = GetLastError();
        xwinusbSetDeviceError(device, error);
        return xwinusbPostFailedTransfer(transfer, error);
    }
    if (device->m_isHid) {
        DWORD expected;
        if (transfer->m_request.m_transferType != XDeviceUsbTransferType_Interrupt) {
            xwinusbSetDeviceError(device, ERROR_NOT_SUPPORTED);
            return xwinusbPostFailedTransfer(transfer, ERROR_NOT_SUPPORTED);
        }
        if (transfer->m_request.m_endpoint == 0x81u)
            expected = device->m_hidCaps.InputReportByteLength;
        else if (transfer->m_request.m_endpoint == 0x01u)
            expected = device->m_hidCaps.OutputReportByteLength;
        else {
            xwinusbSetDeviceError(device, ERROR_FILE_NOT_FOUND);
            return xwinusbPostFailedTransfer(transfer, ERROR_FILE_NOT_FOUND);
        }
        if (expected == 0u || transfer->m_request.m_length != expected) {
            xwinusbSetDeviceError(device, ERROR_INVALID_PARAMETER);
            return xwinusbPostFailedTransfer(transfer, ERROR_INVALID_PARAMETER);
        }
        submitted = transfer->m_request.m_endpoint == 0x81u
            ? ReadFile(device->m_file, transfer->m_request.m_data, expected, NULL,
                       &transfer->m_io.base.overlapped)
            : WriteFile(device->m_file, transfer->m_request.m_data, expected, NULL,
                        &transfer->m_io.base.overlapped);
        if (submitted || GetLastError() == ERROR_IO_PENDING) return true;
        error = GetLastError();
        xwinusbSetDeviceError(device, error);
        return xwinusbPostFailedTransfer(transfer, error);
    }

    if (!xwinusbFindPipe(device, transfer->m_request.m_endpoint,
                         &interfaceHandle, &pipe)) {
        xwinusbSetDeviceError(device, ERROR_FILE_NOT_FOUND);
        return xwinusbPostFailedTransfer(transfer, ERROR_FILE_NOT_FOUND);
    }
    if (transfer->m_request.m_transferType == XDeviceUsbTransferType_Isochronous) {
        ULONG packetCount;
        if (pipe.PipeType != UsbdPipeTypeIsochronous || pipe.MaximumPacketSize == 0) {
            xwinusbSetDeviceError(device, ERROR_NOT_SUPPORTED);
            return xwinusbPostFailedTransfer(transfer, ERROR_NOT_SUPPORTED);
        }
        packetCount = (ULONG)((transfer->m_request.m_length + pipe.MaximumPacketSize - 1u) /
                              pipe.MaximumPacketSize);
        if (packetCount == 0) packetCount = 1;
        transfer->m_isochPackets = (PUSBD_ISO_PACKET_DESCRIPTOR)
            calloc(packetCount, sizeof(*transfer->m_isochPackets));
        if (!transfer->m_isochPackets) {
            xwinusbSetDeviceError(device, ERROR_NOT_ENOUGH_MEMORY);
            return xwinusbPostFailedTransfer(transfer, ERROR_NOT_ENOUGH_MEMORY);
        }
        transfer->m_isochPacketCount = packetCount;
        transfer->m_isochRead = XDEVICE_USB_ENDPOINT_IS_IN(transfer->m_request.m_endpoint);
        if (!WinUsb_RegisterIsochBuffer(interfaceHandle, transfer->m_request.m_endpoint,
                                        (PUCHAR)transfer->m_request.m_data,
                                        (ULONG)transfer->m_request.m_length,
                                        &transfer->m_isochBuffer)) {
            error = GetLastError();
            xwinusbSetDeviceError(device, error);
            return xwinusbPostFailedTransfer(transfer, error);
        }
        if (transfer->m_isochRead)
            submitted = WinUsb_ReadIsochPipeAsap(
                transfer->m_isochBuffer, 0, (ULONG)transfer->m_request.m_length,
                FALSE, packetCount, transfer->m_isochPackets,
                &transfer->m_io.base.overlapped);
        else
            submitted = WinUsb_WriteIsochPipeAsap(
                transfer->m_isochBuffer, 0, (ULONG)transfer->m_request.m_length,
                FALSE, &transfer->m_io.base.overlapped);
        if (submitted || GetLastError() == ERROR_IO_PENDING) return true;
        error = GetLastError();
        xwinusbSetDeviceError(device, error);
        return xwinusbPostFailedTransfer(transfer, error);
    }
    if ((transfer->m_request.m_transferType != XDeviceUsbTransferType_Bulk &&
         transfer->m_request.m_transferType != XDeviceUsbTransferType_Interrupt) ||
        (pipe.PipeType != UsbdPipeTypeBulk && pipe.PipeType != UsbdPipeTypeInterrupt)) {
        xwinusbSetDeviceError(device, ERROR_NOT_SUPPORTED);
        return xwinusbPostFailedTransfer(transfer, ERROR_NOT_SUPPORTED);
    }
    if (XDEVICE_USB_ENDPOINT_IS_IN(transfer->m_request.m_endpoint))
        submitted = WinUsb_ReadPipe(interfaceHandle, transfer->m_request.m_endpoint,
                                    (PUCHAR)transfer->m_request.m_data,
                                    (ULONG)transfer->m_request.m_length, NULL,
                                    &transfer->m_io.base.overlapped);
    else
        submitted = WinUsb_WritePipe(interfaceHandle, transfer->m_request.m_endpoint,
                                     (PUCHAR)transfer->m_request.m_data,
                                     (ULONG)transfer->m_request.m_length, NULL,
                                     &transfer->m_io.base.overlapped);
    if (submitted) return true;
    error = GetLastError();
    if (error == ERROR_IO_PENDING) return true;
    xwinusbSetDeviceError(device, error);
    return xwinusbPostFailedTransfer(transfer, error);
}

static void xwinusbExpireTransfers(XWinUsbDevice* device)
{
    XWinUsbTransfer* transfer;
    ULONGLONG now;
    if (!device || !device->m_transferLockInitialized) return;
    now = GetTickCount64();
    EnterCriticalSection(&device->m_transferLock);
    for (transfer = device->m_transfers; transfer; transfer = transfer->m_next) {
        if (transfer->m_active && transfer->m_deadlineTick != 0 &&
            now >= transfer->m_deadlineTick) {
            transfer->m_completionErrorOverride = WAIT_TIMEOUT;
            CancelIoEx(device->m_file, &transfer->m_io.base.overlapped);
        }
    }
    LeaveCriticalSection(&device->m_transferLock);
}

void* XDeviceUsbHost_platformControllerCreate(const XDeviceUsbControllerConfig* config, int* error)
{
    XWinUsbController* controller = (XWinUsbController*)calloc(1, sizeof(*controller));
    if (!controller) {
        if (error) *error = (int)XDeviceUsbError_Resource;
        return NULL;
    }
    controller->m_deviceInfo = INVALID_HANDLE_VALUE;
    controller->m_hidDeviceInfo = INVALID_HANDLE_VALUE;
    controller->m_cdcDeviceInfo = INVALID_HANDLE_VALUE;
    HidD_GetHidGuid(&controller->m_hidInterfaceGuid);
    controller->m_flags = config ? config->m_flags : 0u;
    if (error) *error = (int)XDeviceUsbError_None;
    return controller;
}

bool XDeviceUsbHost_platformControllerOpen(void* handle,
                                           const XDeviceUsbControllerConfig* config,
                                           int* error)
{
    XWinUsbController* controller = (XWinUsbController*)handle;
    (void)config;
    if (!controller) {
        if (error) *error = (int)XDeviceUsbError_InvalidArgument;
        return false;
    }
    controller->m_deviceInfo = SetupDiGetClassDevsW(&GUID_DEVINTERFACE_USB_DEVICE, NULL, NULL,
                                                     DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);
    if (controller->m_deviceInfo == INVALID_HANDLE_VALUE) {
        DWORD lastError = GetLastError();
        xwinusbSetControllerError(controller, lastError);
        if (error) *error = (int)xwinusbMapError(lastError);
        return false;
    }
    controller->m_hidDeviceInfo = SetupDiGetClassDevsW(&controller->m_hidInterfaceGuid,
                                                        NULL, NULL,
                                                        DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);
    controller->m_cdcDeviceInfo = SetupDiGetClassDevsW(&GUID_DEVINTERFACE_COMPORT,
                                                        NULL, NULL,
                                                        DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);
    {
        CM_NOTIFY_FILTER filter;
        CONFIGRET configResult;
        memset(&filter, 0, sizeof(filter));
        filter.cbSize = sizeof(filter);
        filter.FilterType = CM_NOTIFY_FILTER_TYPE_DEVICEINTERFACE;
        filter.u.DeviceInterface.ClassGuid = GUID_DEVINTERFACE_USB_DEVICE;
        configResult = CM_Register_Notification(&filter, controller,
                                                 xwinusbDeviceNotification,
                                                 &controller->m_notification);
        if (configResult != CR_SUCCESS)
            controller->m_notification = NULL;
    }
    {
        CM_NOTIFY_FILTER filter;
        CONFIGRET configResult;
        memset(&filter, 0, sizeof(filter));
        filter.cbSize = sizeof(filter);
        filter.FilterType = CM_NOTIFY_FILTER_TYPE_DEVICEINTERFACE;
        filter.u.DeviceInterface.ClassGuid = controller->m_hidInterfaceGuid;
        configResult = CM_Register_Notification(&filter, controller,
                                                 xwinusbDeviceNotification,
                                                 &controller->m_hidNotification);
        if (configResult != CR_SUCCESS)
            controller->m_hidNotification = NULL;
    }
    {
        CM_NOTIFY_FILTER filter;
        CONFIGRET configResult;
        memset(&filter, 0, sizeof(filter));
        filter.cbSize = sizeof(filter);
        filter.FilterType = CM_NOTIFY_FILTER_TYPE_DEVICEINTERFACE;
        filter.u.DeviceInterface.ClassGuid = GUID_DEVINTERFACE_COMPORT;
        configResult = CM_Register_Notification(&filter, controller,
                                                 xwinusbDeviceNotification,
                                                 &controller->m_cdcNotification);
        if (configResult != CR_SUCCESS)
            controller->m_cdcNotification = NULL;
    }
    if (error) *error = (int)XDeviceUsbError_None;
    return true;
}

void XDeviceUsbHost_platformControllerClose(void* handle)
{
    XWinUsbController* controller = (XWinUsbController*)handle;
    if (!controller) return;
    xwinusbFreeSnapshot(controller);
    controller->m_snapshotReady = false;
    if (controller->m_notification) {
        CM_Unregister_Notification(controller->m_notification);
        controller->m_notification = NULL;
    }
    if (controller->m_hidNotification) {
        CM_Unregister_Notification(controller->m_hidNotification);
        controller->m_hidNotification = NULL;
    }
    if (controller->m_cdcNotification) {
        CM_Unregister_Notification(controller->m_cdcNotification);
        controller->m_cdcNotification = NULL;
    }
    if (controller->m_deviceInfo != INVALID_HANDLE_VALUE) {
        SetupDiDestroyDeviceInfoList(controller->m_deviceInfo);
        controller->m_deviceInfo = INVALID_HANDLE_VALUE;
    }
    if (controller->m_hidDeviceInfo != INVALID_HANDLE_VALUE) {
        SetupDiDestroyDeviceInfoList(controller->m_hidDeviceInfo);
        controller->m_hidDeviceInfo = INVALID_HANDLE_VALUE;
    }
    if (controller->m_cdcDeviceInfo != INVALID_HANDLE_VALUE) {
        SetupDiDestroyDeviceInfoList(controller->m_cdcDeviceInfo);
        controller->m_cdcDeviceInfo = INVALID_HANDLE_VALUE;
    }
}

void XDeviceUsbHost_platformControllerDelete(void* handle)
{
    XWinUsbController* controller = (XWinUsbController*)handle;
    if (!controller) return;
    XDeviceUsbHost_platformControllerClose(controller);
    free(controller);
}

void* XDeviceUsbHost_platformDeviceOpen(void* controllerHandle,
                                        const XDeviceUsbDeviceSelector* selector,
                                        int* error)
{
    XWinUsbController* controller = (XWinUsbController*)controllerHandle;
    HDEVINFO deviceInfo;
    XWinUsbDevice* device = NULL;
    uint32_t matchIndex = 0;
    if (!controller || !selector) {
        if (error) *error = (int)XDeviceUsbError_InvalidArgument;
        return NULL;
    }
    deviceInfo = SetupDiGetClassDevsW(&GUID_DEVINTERFACE_USB_DEVICE, NULL, NULL,
                                      DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);
    if (deviceInfo != INVALID_HANDLE_VALUE) {
        device = xwinusbOpenMatchingDevice(deviceInfo, &GUID_DEVINTERFACE_USB_DEVICE,
                                            0u, selector, &matchIndex);
        SetupDiDestroyDeviceInfoList(deviceInfo);
    }
    if (!device) {
        deviceInfo = SetupDiGetClassDevsW(&controller->m_hidInterfaceGuid, NULL, NULL,
                                          DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);
        if (deviceInfo != INVALID_HANDLE_VALUE) {
            device = xwinusbOpenMatchingDevice(deviceInfo, &controller->m_hidInterfaceGuid,
                                                1u, selector, &matchIndex);
            SetupDiDestroyDeviceInfoList(deviceInfo);
        }
    }
    if (!device) {
        deviceInfo = SetupDiGetClassDevsW(&GUID_DEVINTERFACE_COMPORT, NULL, NULL,
                                          DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);
        if (deviceInfo != INVALID_HANDLE_VALUE) {
            device = xwinusbOpenMatchingDevice(deviceInfo, &GUID_DEVINTERFACE_COMPORT,
                                                2u, selector, &matchIndex);
            SetupDiDestroyDeviceInfoList(deviceInfo);
        }
    }
    if (device) {
        device->m_info.m_controller = 0u;
        device->m_controller = controller;
        device->m_nextControllerDevice = controller->m_devices;
        controller->m_devices = device;
        if (error) *error = (int)XDeviceUsbError_None;
        return device;
    }
    xwinusbSetControllerError(controller, ERROR_FILE_NOT_FOUND);
    if (error) *error = (int)XDeviceUsbError_NoDevice;
    return NULL;
}

void XDeviceUsbHost_platformDeviceClose(void* handle)
{
    xwinusbCloseDevice((XWinUsbDevice*)handle);
}

bool XDeviceUsbHost_platformDeviceGetInfo(void* handle, XDeviceUsbDeviceInfo* info)
{
    XWinUsbDevice* device = (XWinUsbDevice*)handle;
    if (!device || !info) return false;
    *info = device->m_info;
    return true;
}

bool XDeviceUsbHost_platformDeviceSetConfiguration(void* handle, uint8_t value)
{
    XWinUsbDevice* device = (XWinUsbDevice*)handle;
    XDeviceUsbControlRequest request;
    size_t transferred = 0;
    XDeviceUsbTransferResult result;
    if (!device) return false;
    if (device->m_isHid || device->m_isCdc) {
        if (value == 1u) return true;
        xwinusbSetDeviceError(device, ERROR_NOT_SUPPORTED);
        return false;
    }
    if (value == device->m_info.m_activeConfiguration) return true;
    memset(&request, 0, sizeof(request));
    request.m_requestType = 0x00u;
    request.m_request = 9u;
    request.m_value = value;
    result = xwinusbControlTransfer(device, &request, NULL, 0, &transferred, 0);
    if (result != XDeviceUsbTransferResult_Ok) return false;
    xwinusbFreeInterfaces(device);
    if (!WinUsb_Initialize(device->m_file, &device->m_interfaces[0])) {
        xwinusbSetDeviceError(device, GetLastError());
        return false;
    }
    device->m_interfaceCount = 1;
    while (device->m_interfaceCount < XWINUSB_MAX_INTERFACES) {
        WINUSB_INTERFACE_HANDLE associated = NULL;
        if (!WinUsb_GetAssociatedInterface(device->m_interfaces[0],
                                           (UCHAR)(device->m_interfaceCount - 1u),
                                           &associated)) break;
        device->m_interfaces[device->m_interfaceCount++] = associated;
    }
    if (!xwinusbReadConfigurationByValue(device, value)) {
        xwinusbSetDeviceError(device, GetLastError());
        return false;
    }
    memset(device->m_currentAlternate, 0, sizeof(device->m_currentAlternate));
    device->m_info.m_activeConfiguration = value;
    return true;
}

bool XDeviceUsbHost_platformDeviceGetConfiguration(void* handle, uint8_t* value)
{
    XWinUsbDevice* device = (XWinUsbDevice*)handle;
    if (!device || !value) return false;
    *value = device->m_info.m_activeConfiguration;
    return true;
}

bool XDeviceUsbHost_platformDeviceClaimInterface(void* handle, uint8_t interfaceNumber)
{
    XWinUsbDevice* device = (XWinUsbDevice*)handle;
    uint8_t index;
    if (!device || !xwinusbGetInterfaceIndex(device, interfaceNumber, &index)) return false;
    if (interfaceNumber >= 32u) return false;
    if (device->m_claimedInterfaces & (UINT32_C(1) << interfaceNumber)) {
        xwinusbSetDeviceError(device, ERROR_BUSY);
        return false;
    }
    device->m_claimedInterfaces |= UINT32_C(1) << interfaceNumber;
    return true;
}

bool XDeviceUsbHost_platformDeviceReleaseInterface(void* handle, uint8_t interfaceNumber)
{
    XWinUsbDevice* device = (XWinUsbDevice*)handle;
    uint8_t index;
    if (!device || interfaceNumber >= 32u ||
        !xwinusbGetInterfaceIndex(device, interfaceNumber, &index)) return false;
    if ((device->m_claimedInterfaces & (UINT32_C(1) << interfaceNumber)) == 0) {
        xwinusbSetDeviceError(device, ERROR_INVALID_PARAMETER);
        return false;
    }
    if (device->m_isHid || device->m_isCdc) {
        device->m_claimedInterfaces &= ~(UINT32_C(1) << interfaceNumber);
        return true;
    }
    {
        USB_INTERFACE_DESCRIPTOR descriptor;
        UCHAR pipeIndex;
        UCHAR alternate = device->m_currentAlternate[index];
        if (!WinUsb_QueryInterfaceSettings(device->m_interfaces[index], alternate,
                                           &descriptor)) {
            xwinusbSetDeviceError(device, GetLastError());
            return false;
        }
        for (pipeIndex = 0; pipeIndex < descriptor.bNumEndpoints; ++pipeIndex) {
            WINUSB_PIPE_INFORMATION pipe;
            if (WinUsb_QueryPipe(device->m_interfaces[index], alternate, pipeIndex, &pipe) &&
                !WinUsb_AbortPipe(device->m_interfaces[index], pipe.PipeId)) {
                xwinusbSetDeviceError(device, GetLastError());
                return false;
            }
        }
    }
    device->m_claimedInterfaces &= ~(UINT32_C(1) << interfaceNumber);
    return true;
}

bool XDeviceUsbHost_platformDeviceSetAlternateSetting(void* handle,
                                                       uint8_t interfaceNumber,
                                                       uint8_t alternate)
{
    XWinUsbDevice* device = (XWinUsbDevice*)handle;
    WINUSB_INTERFACE_HANDLE interfaceHandle = xwinusbGetInterface(device, interfaceNumber);
    uint8_t index;
    if (device && (device->m_isHid || device->m_isCdc)) {
        if (interfaceNumber == 0u && alternate == 0u) return true;
        xwinusbSetDeviceError(device, ERROR_NOT_SUPPORTED);
        return false;
    }
    if (device && xwinusbGetInterfaceIndex(device, interfaceNumber, &index))
        interfaceHandle = device->m_interfaces[index];
    if (!interfaceHandle || !WinUsb_SetCurrentAlternateSetting(interfaceHandle, alternate)) {
        if (device) xwinusbSetDeviceError(device, GetLastError());
        return false;
    }
    if (device && index < XWINUSB_MAX_INTERFACES)
        device->m_currentAlternate[index] = alternate;
    return true;
}

size_t XDeviceUsbHost_platformDeviceEndpointCount(void* handle,
                                                  uint8_t interfaceNumber,
                                                  uint8_t alternate)
{
    XWinUsbDevice* device = (XWinUsbDevice*)handle;
    WINUSB_INTERFACE_HANDLE interfaceHandle = xwinusbGetInterface(device, interfaceNumber);
    USB_INTERFACE_DESCRIPTOR descriptor;
    uint8_t index;
    if (device && device->m_isHid)
        return interfaceNumber == 0u && alternate == 0u ? xwinusbHidEndpointCount(device) : 0u;
    if (device && device->m_isCdc)
        return interfaceNumber == 0u && alternate == 0u ? 2u : 0u;
    if (!device || !xwinusbGetInterfaceIndex(device, interfaceNumber, &index)) return 0;
    interfaceHandle = device->m_interfaces[index];
    if (!interfaceHandle || !WinUsb_QueryInterfaceSettings(interfaceHandle, alternate, &descriptor))
        return 0;
    return descriptor.bNumEndpoints;
}

bool XDeviceUsbHost_platformDeviceGetEndpointInfo(void* handle,
                                                  uint8_t interfaceNumber,
                                                  uint8_t alternate, size_t index,
                                                  XDeviceUsbEndpointInfo* endpoint)
{
    XWinUsbDevice* device = (XWinUsbDevice*)handle;
    WINUSB_INTERFACE_HANDLE interfaceHandle = xwinusbGetInterface(device, interfaceNumber);
    USB_INTERFACE_DESCRIPTOR descriptor;
    WINUSB_PIPE_INFORMATION pipe;
    uint8_t interfaceIndex;
    if (device && device->m_isHid) {
        if (interfaceNumber != 0u || alternate != 0u) return false;
        return xwinusbHidEndpointInfo(device, index, endpoint);
    }
    if (device && device->m_isCdc) {
        if (!endpoint || interfaceNumber != 0u || alternate != 0u || index >= 2u) return false;
        memset(endpoint, 0, sizeof(*endpoint));
        endpoint->m_address = index == 0u ? 0x81u : 0x01u;
        endpoint->m_transferType = XDeviceUsbTransferType_Bulk;
        endpoint->m_maxPacketSize = 64u;
        endpoint->m_interfaceNumber = 0u;
        return true;
    }
    if (!device || !xwinusbGetInterfaceIndex(device, interfaceNumber, &interfaceIndex)) return false;
    interfaceHandle = device->m_interfaces[interfaceIndex];
    if (!interfaceHandle || !endpoint || index > 255u ||
        !WinUsb_QueryInterfaceSettings(interfaceHandle, alternate, &descriptor) ||
        index >= descriptor.bNumEndpoints ||
        !WinUsb_QueryPipe(interfaceHandle, alternate, (UCHAR)index, &pipe))
        return false;
    memset(endpoint, 0, sizeof(*endpoint));
    endpoint->m_address = pipe.PipeId;
    endpoint->m_maxPacketSize = pipe.MaximumPacketSize;
    endpoint->m_interval = pipe.Interval;
    endpoint->m_interfaceNumber = interfaceNumber;
    endpoint->m_alternateSetting = alternate;
    switch (pipe.PipeType) {
    case UsbdPipeTypeBulk:        endpoint->m_transferType = XDeviceUsbTransferType_Bulk; break;
    case UsbdPipeTypeInterrupt:   endpoint->m_transferType = XDeviceUsbTransferType_Interrupt; break;
    case UsbdPipeTypeIsochronous: endpoint->m_transferType = XDeviceUsbTransferType_Isochronous; break;
    default:                      endpoint->m_transferType = XDeviceUsbTransferType_Control; break;
    }
    return true;
}

bool XDeviceUsbHost_platformDeviceClearHalt(void* handle,
                                            XDeviceUsbEndpointAddress endpoint)
{
    XWinUsbDevice* device = (XWinUsbDevice*)handle;
    WINUSB_INTERFACE_HANDLE interfaceHandle;
    WINUSB_PIPE_INFORMATION pipe;
    if (!device || endpoint == 0) return false;
    if (device->m_isHid || device->m_isCdc) {
        xwinusbSetDeviceError(device, ERROR_NOT_SUPPORTED);
        return false;
    }
    if (!xwinusbFindPipe(device, endpoint, &interfaceHandle, &pipe)) {
        xwinusbSetDeviceError(device, ERROR_FILE_NOT_FOUND);
        return false;
    }
    if (WinUsb_ResetPipe(interfaceHandle, endpoint)) return true;
    xwinusbSetDeviceError(device, GetLastError());
    return false;
}

bool XDeviceUsbHost_platformDeviceReset(void* handle)
{
    XWinUsbDevice* device = (XWinUsbDevice*)handle;
    return xwinusbRestartDevice(device);
}

XDeviceUsbTransferResult XDeviceUsbHost_platformDeviceControlTransfer(
    void* handle, const XDeviceUsbControlRequest* request, void* data,
    size_t capacity, size_t* transferred, int32_t timeoutMs)
{
    return xwinusbControlTransfer((XWinUsbDevice*)handle, request, data, capacity,
                                  transferred, timeoutMs);
}

XDeviceUsbTransferResult XDeviceUsbHost_platformDeviceTransfer(
    void* handle, XDeviceUsbEndpointAddress endpoint, void* data,
    size_t length, size_t* transferred, int32_t timeoutMs)
{
    XWinUsbDevice* device = (XWinUsbDevice*)handle;
    WINUSB_INTERFACE_HANDLE interfaceHandle;
    WINUSB_PIPE_INFORMATION pipe;
    if (device && device->m_requiresReopen)
        return XDeviceUsbTransferResult_NoDevice;
    if (device && device->m_isHid)
        return xwinusbHidTransfer(device, endpoint, data, length, transferred, timeoutMs);
    if (device && device->m_isCdc)
        return xwinusbCdcTransfer(device, endpoint, data, length, transferred, timeoutMs);
    if (device && xwinusbFindPipe(device, endpoint, &interfaceHandle, &pipe) &&
        pipe.PipeType == UsbdPipeTypeIsochronous)
        return xwinusbIsochTransfer(device, endpoint, data, length,
                                    transferred, timeoutMs);
    return xwinusbPipeTransfer(device, endpoint, data, length, transferred, timeoutMs);
}

XDeviceUsbTransferId XDeviceUsbHost_platformDeviceSubmitTransfer(
    void* handle, const XDeviceUsbTransferRequest* request,
    XDeviceUsbTransferCallback callback, void* userData)
{
    XWinUsbDevice* device = (XWinUsbDevice*)handle;
    XWinUsbTransfer* transfer;
    if (!device || !request || !callback || request->m_endpoint == 0 ||
        device->m_requiresReopen ||
        !request->m_data || request->m_length > ULONG_MAX || request->m_flags != 0u ||
        (request->m_transferType != XDeviceUsbTransferType_Isochronous &&
         request->m_transferType != XDeviceUsbTransferType_Bulk &&
         request->m_transferType != XDeviceUsbTransferType_Interrupt)) {
        if (device) xwinusbSetDeviceError(device, ERROR_INVALID_PARAMETER);
        return XDEVICE_USB_INVALID_TRANSFER_ID;
    }
    transfer = (XWinUsbTransfer*)calloc(1, sizeof(*transfer));
    if (!transfer) {
        xwinusbSetDeviceError(device, ERROR_NOT_ENOUGH_MEMORY);
        return XDEVICE_USB_INVALID_TRANSFER_ID;
    }
    transfer->m_device = device;
    transfer->m_request = *request;
    transfer->m_callback = callback;
    transfer->m_userData = userData;
    if (request->m_timeoutMs > 0)
        transfer->m_deadlineTick = GetTickCount64() + (ULONGLONG)request->m_timeoutMs;
    if (!xwinusbEnsureIocp(device)) {
        free(transfer);
        xwinusbSetDeviceError(device, ERROR_NOT_SUPPORTED);
        return XDEVICE_USB_INVALID_TRANSFER_ID;
    }
    memset(&transfer->m_io, 0, sizeof(transfer->m_io));
    transfer->m_io.base.type = XEventContextType_Type_File;
    transfer->m_io.base.fd = XFD_INVALID;
    transfer->m_io.socket = XSocketDescriptor_fromIntptr((intptr_t)device->m_file);
    transfer->m_io.buffer = request->m_data;
    transfer->m_io.bufferSize = request->m_length;
    transfer->m_io.completionCallback = xwinusbIoCompletion;
    transfer->m_io.completionUserData = transfer;
    EnterCriticalSection(&device->m_transferLock);
    if (device->m_closing) {
        LeaveCriticalSection(&device->m_transferLock);
        free(transfer);
        return XDEVICE_USB_INVALID_TRANSFER_ID;
    }
    transfer->m_id = device->m_nextTransferId++;
    if (transfer->m_id == XDEVICE_USB_INVALID_TRANSFER_ID)
        transfer->m_id = device->m_nextTransferId++;
    transfer->m_active = true;
    transfer->m_next = device->m_transfers;
    device->m_transfers = transfer;
    if (!xwinusbSubmitPipe(transfer)) {
        XWinUsbTransfer** cursor = &device->m_transfers;
        while (*cursor && *cursor != transfer) cursor = &(*cursor)->m_next;
        if (*cursor == transfer) *cursor = transfer->m_next;
        transfer->m_active = false;
        LeaveCriticalSection(&device->m_transferLock);
        free(transfer);
        return XDEVICE_USB_INVALID_TRANSFER_ID;
    }
    LeaveCriticalSection(&device->m_transferLock);
    return transfer->m_id;
}

XDeviceUsbTransferResult XDeviceUsbHost_platformDeviceCancelTransfer(
    void* handle, XDeviceUsbTransferId transferId)
{
    XWinUsbDevice* device = (XWinUsbDevice*)handle;
    XWinUsbTransfer* transfer;
    if (!device || transferId == XDEVICE_USB_INVALID_TRANSFER_ID)
        return XDeviceUsbTransferResult_InvalidArgument;
    EnterCriticalSection(&device->m_transferLock);
    for (transfer = device->m_transfers; transfer; transfer = transfer->m_next) {
        if (transfer->m_id == transferId) {
            if (transfer->m_active) {
                if (!CancelIoEx(device->m_file, &transfer->m_io.base.overlapped)) {
                    DWORD error = GetLastError();
                    if (error != ERROR_NOT_FOUND) xwinusbSetDeviceError(device, error);
                }
                LeaveCriticalSection(&device->m_transferLock);
                return XDeviceUsbTransferResult_Cancelled;
            }
            LeaveCriticalSection(&device->m_transferLock);
            return XDeviceUsbTransferResult_Ok;
        }
    }
    LeaveCriticalSection(&device->m_transferLock);
    return XDeviceUsbTransferResult_NoDevice;
}

XDeviceUsbFeatures XDeviceUsbHost_platformDeviceFeatures(void* handle)
{
    XWinUsbDevice* device = (XWinUsbDevice*)handle;
    XDeviceUsbFeatures features = XDeviceUsbFeature_AsyncTransfer |
                                  XDeviceUsbFeature_ProcessEvents;
    if (!device) return XDeviceUsbFeature_None;
    if (device->m_isHid || device->m_isCdc)
        return features | XDeviceUsbFeature_Reset;
    features |= XDeviceUsbFeature_DescriptorCache;
    if (device->m_info.m_speed == XDeviceUsbSpeed_High) features |= XDeviceUsbFeature_HighSpeed;
    if (device->m_info.m_speed == XDeviceUsbSpeed_Super ||
        device->m_info.m_speed == XDeviceUsbSpeed_SuperPlus)
        features |= XDeviceUsbFeature_SuperSpeed;
    /* WinUSB exposes isochronous pipes only on Windows 8 and later. */
    features |= XDeviceUsbFeature_Isochronous;
    features |= XDeviceUsbFeature_Reset;
    return features;
}

XDeviceUsbNativeHandle XDeviceUsbHost_platformDeviceHandle(void* handle)
{
    XWinUsbDevice* device = (XWinUsbDevice*)handle;
    if (!device || device->m_file == INVALID_HANDLE_VALUE) return XDEVICE_USB_INVALID_NATIVE_HANDLE;
    return (XDeviceUsbNativeHandle)(intptr_t)device->m_file;
}

XDeviceUsbError XDeviceUsbHost_platformDeviceLastError(void* handle)
{
    XWinUsbDevice* device = (XWinUsbDevice*)handle;
    return device ? xwinusbMapError(device->m_lastError) : XDeviceUsbError_NotOpen;
}

int32_t XDeviceUsbHost_platformDeviceNativeError(void* handle)
{
    XWinUsbDevice* device = (XWinUsbDevice*)handle;
    return device ? (int32_t)device->m_lastError : (int32_t)ERROR_INVALID_HANDLE;
}

void XDeviceUsbHost_platformDeviceClearError(void* handle)
{
    xwinusbSetDeviceError((XWinUsbDevice*)handle, ERROR_SUCCESS);
}

bool XDeviceUsbHost_platformControllerEnumerate(void* handle,
                                                XDeviceUsbEnumerateCallback callback,
                                                void* userData)
{
    XWinUsbController* controller = (XWinUsbController*)handle;
    XWinUsbHotplugEntry* snapshot = NULL;
    size_t count = 0;
    size_t i;
    if (!controller || !callback || !xwinusbBuildSnapshot(controller, &snapshot, &count))
        return false;
    for (i = 0; i < count; ++i)
        callback(controller, &snapshot[i].m_info, userData);
    xwinusbFreeSnapshotEntries(snapshot, count);
    return true;
}

bool XDeviceUsbHost_platformControllerSetHotplug(void* handle,
                                                 XDeviceUsbHotplugCallback callback,
                                                 void* userData)
{
    XWinUsbController* controller = (XWinUsbController*)handle;
    if (!controller) return false;
    xwinusbFreeSnapshot(controller);
    controller->m_snapshotReady = false;
    controller->m_hotplugCallback = callback;
    controller->m_hotplugUserData = userData;
    if (!callback) return true;
    if (!xwinusbBuildSnapshot(controller, &controller->m_snapshot,
                              &controller->m_snapshotCount)) {
        controller->m_hotplugCallback = NULL;
        controller->m_hotplugUserData = NULL;
        return false;
    }
    controller->m_snapshotReady = true;
    return true;
}

XDeviceUsbProcessResult XDeviceUsbHost_platformControllerProcessEvents(void* handle,
                                                                        int32_t timeoutMs)
{
    XWinUsbController* controller = (XWinUsbController*)handle;
    XAbstractNetIoRing* ring;
    XWinUsbHotplugEntry* current = NULL;
    size_t currentCount = 0;
    size_t i;
    bool changed = false;
    int32_t waitMs;
    if (!controller) return XDeviceUsbProcessResult_Error;
    if (InterlockedExchange(&controller->m_nativeChanged, 0) != 0)
        changed = true;

    ring = XAbstractNetIoRing_global();
    {
        XWinUsbDevice* device;
        for (device = controller->m_devices; device; device = device->m_nextControllerDevice)
            xwinusbExpireTransfers(device);
    }
    if (ring && XAbstractNetIoRing_isEnabled(ring)) {
        /* Snapshot hotplug polling must remain bounded so a blocking caller
           can still observe device arrival/removal. */
        waitMs = timeoutMs < 0 ? 100 : (timeoutMs > 100 ? 100 : timeoutMs);
        if (waitMs != 0) XAbstractNetIoRing_waitForEvents_base(ring, waitMs);
        XAbstractNetIoRing_pollPlatform_base(ring);
    } else if (timeoutMs > 0) {
        Sleep((DWORD)(timeoutMs > 100 ? 100 : timeoutMs));
    }

    if (!controller->m_hotplugCallback) return XDeviceUsbProcessResult_Timeout;
    if (!xwinusbBuildSnapshot(controller, &current, &currentCount))
        return XDeviceUsbProcessResult_Error;
    if (!controller->m_snapshotReady) {
        controller->m_snapshot = current;
        controller->m_snapshotCount = currentCount;
        controller->m_snapshotReady = true;
        return changed ? XDeviceUsbProcessResult_Event : XDeviceUsbProcessResult_Timeout;
    }

    for (i = 0; i < controller->m_snapshotCount; ++i) {
        if (xwinusbFindSnapshotPath(current, currentCount,
                                    controller->m_snapshot[i].m_path) == currentCount) {
            XDeviceUsbHotplugEvent event;
            memset(&event, 0, sizeof(event));
            event.m_type = XDeviceUsbHotplugEventType_Removed;
            event.m_device = controller->m_snapshot[i].m_info;
            controller->m_hotplugCallback(controller, &event,
                                          controller->m_hotplugUserData);
            changed = true;
        }
    }
    for (i = 0; i < currentCount; ++i) {
        size_t oldIndex = xwinusbFindSnapshotPath(controller->m_snapshot,
                                                  controller->m_snapshotCount,
                                                  current[i].m_path);
        XDeviceUsbHotplugEvent event;
        if (oldIndex == controller->m_snapshotCount) {
            memset(&event, 0, sizeof(event));
            event.m_type = XDeviceUsbHotplugEventType_Arrived;
            event.m_device = current[i].m_info;
            controller->m_hotplugCallback(controller, &event,
                                          controller->m_hotplugUserData);
            changed = true;
        } else if (!xwinusbSnapshotInfoEqual(&controller->m_snapshot[oldIndex].m_info,
                                             &current[i].m_info)) {
            memset(&event, 0, sizeof(event));
            event.m_type = XDeviceUsbHotplugEventType_Changed;
            event.m_device = current[i].m_info;
            controller->m_hotplugCallback(controller, &event,
                                          controller->m_hotplugUserData);
            changed = true;
        }
    }
    xwinusbFreeSnapshot(controller);
    controller->m_snapshot = current;
    controller->m_snapshotCount = currentCount;
    controller->m_snapshotReady = true;
    return changed ? XDeviceUsbProcessResult_Event : XDeviceUsbProcessResult_Timeout;
}

XDeviceUsbFeatures XDeviceUsbHost_platformControllerFeatures(void* handle)
{
    return handle ? (XDeviceUsbFeature_DescriptorCache |
                     XDeviceUsbFeature_Hotplug |
                     XDeviceUsbFeature_AsyncTransfer |
                     XDeviceUsbFeature_ProcessEvents) : XDeviceUsbFeature_None;
}

XDeviceUsbNativeHandle XDeviceUsbHost_platformControllerHandle(void* handle)
{
    (void)handle;
    return XDEVICE_USB_INVALID_NATIVE_HANDLE;
}

XDeviceUsbError XDeviceUsbHost_platformControllerLastError(void* handle)
{
    XWinUsbController* controller = (XWinUsbController*)handle;
    return controller ? xwinusbMapError(controller->m_lastError) : XDeviceUsbError_NotOpen;
}

int32_t XDeviceUsbHost_platformControllerNativeError(void* handle)
{
    XWinUsbController* controller = (XWinUsbController*)handle;
    return controller ? (int32_t)controller->m_lastError : (int32_t)ERROR_INVALID_HANDLE;
}

void XDeviceUsbHost_platformControllerClearError(void* handle)
{
    xwinusbSetControllerError((XWinUsbController*)handle, ERROR_SUCCESS);
}

#endif /* _WIN32 */
