#include "XDeviceSerialPort.h"

#if defined(_WIN32) && defined(XSERIALPORT_USE_PLATFORM_API)

#include "XMemory.h"
#include "XVariant.h"
#include "XVarList.h"
#include "XString.h"
#include "XSerialPort.h"
#include <windows.h>
#include <devpropdef.h>
#include <setupapi.h>
#include <stdint.h>
#include <stdio.h>
#include <stdio.h>
#include <string.h>
#include <wchar.h>

typedef struct XDeviceSerialPortWin32Context
{
    XDeviceSerialPortContext m_base;
    HANDLE m_handle;
    COMMTIMEOUTS m_originalTimeouts;
    bool m_haveOriginalTimeouts;
} XDeviceSerialPortWin32Context;

static XDeviceSerialPortWin32Context* win32Context(XFd fd)
{
    return (XDeviceSerialPortWin32Context*)XDevice_handle(fd);
}

static BYTE win32DataBits(XSerialPort_DataBits value)
{
    return value >= XSerialPort_Data5 && value <= XSerialPort_Data8 ? (BYTE)value : 8;
}

static BYTE win32Parity(XSerialPort_Parity value)
{
    switch (value) {
    case XSerialPort_EvenParity: return EVENPARITY;
    case XSerialPort_OddParity: return ODDPARITY;
    case XSerialPort_MarkParity: return MARKPARITY;
    case XSerialPort_SpaceParity: return SPACEPARITY;
    default: return NOPARITY;
    }
}

static BYTE win32StopBits(XSerialPort_StopBits value)
{
    return value == XSerialPort_TwoStop ? TWOSTOPBITS : ONESTOPBIT;
}

static bool win32ApplyConfig(XDeviceSerialPortWin32Context* context)
{
    DCB dcb;
    if (!context || context->m_handle == INVALID_HANDLE_VALUE) return false;
    memset(&dcb, 0, sizeof(dcb));
    dcb.DCBlength = sizeof(dcb);
    if (!GetCommState(context->m_handle, &dcb)) return false;
    dcb.BaudRate = context->m_base.m_baudRate;
    dcb.ByteSize = win32DataBits(context->m_base.m_dataBits);
    dcb.Parity = win32Parity(context->m_base.m_parity);
    dcb.StopBits = win32StopBits(context->m_base.m_stopBits);
    dcb.fParity = context->m_base.m_parity != XSerialPort_NoParity;
    dcb.fOutxCtsFlow = FALSE;
    dcb.fRtsControl = RTS_CONTROL_DISABLE;
    dcb.fInX = FALSE;
    dcb.fOutX = FALSE;
    if (context->m_base.m_flowControl == XSerialPort_HardwareControl ||
        context->m_base.m_flowControl == XSerialPort_BothControl) {
        dcb.fOutxCtsFlow = TRUE;
        dcb.fRtsControl = RTS_CONTROL_HANDSHAKE;
    }
    if (context->m_base.m_flowControl == XSerialPort_SoftwareControl ||
        context->m_base.m_flowControl == XSerialPort_BothControl) {
        dcb.fInX = TRUE;
        dcb.fOutX = TRUE;
    }
    return SetCommState(context->m_handle, &dcb) != FALSE;
}

XDeviceSerialPortContext* XDeviceSerialPort_platformCreateContext(void)
{
    XDeviceSerialPortWin32Context* context =
        (XDeviceSerialPortWin32Context*)XCalloc_System(1, sizeof(*context));
    if (context) context->m_handle = INVALID_HANDLE_VALUE;
    return context ? &context->m_base : NULL;
}

void XDeviceSerialPort_platformDeleteContext(XDeviceSerialPortContext* base)
{
    XFree_System(base);
}

bool XDeviceSerialPort_platformOpen(XFd fd, const XString* portName)
{
    XDeviceSerialPortWin32Context* context = win32Context(fd);
    const wchar_t* path;
    wchar_t fullPath[256];
    DWORD access = 0;
    COMMTIMEOUTS timeouts;
    if (!context || !portName) return false;
    path = (const wchar_t*)XString_toUtf16(portName);
    if (!path) return false;
    if (context->m_base.m_openMode & XIODevice_ReadOnly) access |= GENERIC_READ;
    if (context->m_base.m_openMode & XIODevice_WriteOnly) access |= GENERIC_WRITE;
    if (wcsncmp(path, L"\\\\.\\", 4) == 0)
        wcsncpy(fullPath, path, sizeof(fullPath) / sizeof(fullPath[0]) - 1);
    else {
        fullPath[0] = L'\\'; fullPath[1] = L'\\'; fullPath[2] = L'.'; fullPath[3] = L'\\';
        wcsncpy(fullPath + 4, path, sizeof(fullPath) / sizeof(fullPath[0]) - 5);
    }
    fullPath[sizeof(fullPath) / sizeof(fullPath[0]) - 1] = L'\0';
    context->m_handle = CreateFileW(fullPath, access, 0, NULL, OPEN_EXISTING,
                                    FILE_ATTRIBUTE_NORMAL, NULL);
    if (context->m_handle == INVALID_HANDLE_VALUE) return false;
    memset(&context->m_originalTimeouts, 0, sizeof(context->m_originalTimeouts));
    context->m_haveOriginalTimeouts = GetCommTimeouts(context->m_handle,
                                                       &context->m_originalTimeouts) != FALSE;
    memset(&timeouts, 0, sizeof(timeouts));
    timeouts.ReadIntervalTimeout = MAXDWORD;
    if (!SetCommTimeouts(context->m_handle, &timeouts) ||
        !SetupComm(context->m_handle, (DWORD)context->m_base.m_readBufferSize,
                   (DWORD)context->m_base.m_readBufferSize) || !win32ApplyConfig(context)) {
        CloseHandle(context->m_handle);
        context->m_handle = INVALID_HANDLE_VALUE;
        return false;
    }
    PurgeComm(context->m_handle, PURGE_RXCLEAR | PURGE_TXCLEAR);
    return true;
}

void XDeviceSerialPort_platformClose(XFd fd)
{
    XDeviceSerialPortWin32Context* context = win32Context(fd);
    if (!context) return;
    if (context->m_handle != INVALID_HANDLE_VALUE) {
        if (context->m_haveOriginalTimeouts)
            SetCommTimeouts(context->m_handle, &context->m_originalTimeouts);
        CloseHandle(context->m_handle);
        context->m_handle = INVALID_HANDLE_VALUE;
    }
}

int64_t XDeviceSerialPort_platformRead(XFd fd, void* buffer, int64_t size)
{
    XDeviceSerialPortWin32Context* context = win32Context(fd);
    DWORD done = 0;
    if (!context || context->m_handle == INVALID_HANDLE_VALUE || !buffer || size <= 0) return -1;
    if (!ReadFile(context->m_handle, buffer, (DWORD)size, &done, NULL)) return -1;
    return (int64_t)done;
}

int64_t XDeviceSerialPort_platformWrite(XFd fd, const void* data, int64_t size)
{
    XDeviceSerialPortWin32Context* context = win32Context(fd);
    DWORD done = 0;
    if (!context || context->m_handle == INVALID_HANDLE_VALUE || !data || size <= 0) return -1;
    if (!WriteFile(context->m_handle, data, (DWORD)size, &done, NULL)) return -1;
    return (int64_t)done;
}

bool XDeviceSerialPort_platformFlush(XFd fd)
{
    XDeviceSerialPortWin32Context* context = win32Context(fd);
    return context && context->m_handle != INVALID_HANDLE_VALUE &&
           FlushFileBuffers(context->m_handle) != FALSE;
}

bool XDeviceSerialPort_platformSetProperty(XFd fd, uint32_t property, const XVariant* value)
{
    XDeviceSerialPortWin32Context* context = win32Context(fd);
    bool enabled;
    if (!context || !value || context->m_handle == INVALID_HANDLE_VALUE) return false;
    switch (property) {
    case XDeviceSerialPortProperty_BaudRate:
    case XDeviceSerialPortProperty_DataBits:
    case XDeviceSerialPortProperty_Parity:
    case XDeviceSerialPortProperty_StopBits:
    case XDeviceSerialPortProperty_FlowControl:
        return win32ApplyConfig(context);
    case XDeviceSerialPortProperty_ReadBufferSize:
        return SetupComm(context->m_handle, (DWORD)XVariant_toInt64(value),
                         (DWORD)XVariant_toInt64(value)) != FALSE;
    case XDeviceSerialPortProperty_DataTerminalReady:
        enabled = XVariant_toBool(value);
        return EscapeCommFunction(context->m_handle, enabled ? SETDTR : CLRDTR) != FALSE;
    case XDeviceSerialPortProperty_RequestToSend:
        enabled = XVariant_toBool(value);
        return EscapeCommFunction(context->m_handle, enabled ? SETRTS : CLRRTS) != FALSE;
    case XDeviceSerialPortProperty_BreakEnabled:
        enabled = XVariant_toBool(value);
        return EscapeCommFunction(context->m_handle, enabled ? SETBREAK : CLRBREAK) != FALSE;
    default:
        return false;
    }
}

bool XDeviceSerialPort_platformGetProperty(XFd fd, uint32_t property, XVariant* value)
{
    XDeviceSerialPortWin32Context* context = win32Context(fd);
    COMSTAT status;
    DWORD errors = 0;
    DWORD modem = 0;
    int signals;
    if (!context || !value || context->m_handle == INVALID_HANDLE_VALUE) return false;
    switch (property) {
    case XDeviceProperty_NativeHandle:
        XVariant_setValue_ptr(value, context->m_handle); return true;
    case XDeviceSerialPortProperty_BytesAvailable:
        if (!ClearCommError(context->m_handle, &errors, &status)) return false;
        XVariant_setValue_int64(value, status.cbInQue); return true;
    case XDeviceSerialPortProperty_PinoutSignals:
        if (!GetCommModemStatus(context->m_handle, &modem)) return false;
        signals = XSerialPort_NoSignal;
        if (modem & MS_CTS_ON) signals |= XSerialPort_ClearToSendSignal;
        if (modem & MS_DSR_ON) signals |= XSerialPort_DataSetReadySignal;
        if (modem & MS_RING_ON) signals |= XSerialPort_RingIndicatorSignal;
        if (modem & MS_RLSD_ON) signals |= XSerialPort_DataCarrierDetectSignal;
        XVariant_setValue_int(value, signals); return true;
    default:
        return false;
    }
}

bool XDeviceSerialPort_platformControl(XFd fd, uint32_t command,
                                       const XVarList* input, XVarList* output)
{
    XDeviceSerialPortWin32Context* context = win32Context(fd);
    XSerialPort_Direction directions;
    XVarList* arguments = (XVarList*)input;
    DWORD flags = 0;
    (void)output;
    if (!context || context->m_handle == INVALID_HANDLE_VALUE) return false;
    if (command == XDeviceSerialPortCommand_Clear) {
        if (!arguments || arguments->m_size != sizeof(directions)) return false;
        XVarList_start(arguments);
        directions = XVarList_arg(arguments, XSerialPort_Direction);
        if (directions & XSerialPort_Input) flags |= PURGE_RXCLEAR;
        if (directions & XSerialPort_Output) flags |= PURGE_TXCLEAR;
        return PurgeComm(context->m_handle, flags) != FALSE;
    }
    return command == XDeviceSerialPortCommand_HandleEvent ||
           command == XDeviceCommand_Cancel;
}

/* ==================================================================== */
/* 串口枚举：SetupAPI 枚举串口设备接口                                    */
/* ==================================================================== */

/*
 * GUID 与 DEVPROPKEY 在此以文件局部常量给出，不引用 SDK 的 <initguid.h> /
 * <devguid.h> / <devpkey.h> 宏：
 *   - <initguid.h> 会把 DEFINE_GUID 展开为带外部链接的定义，本工程另有翻译
 *     单元（Drive/windows/Device/Usb/XDeviceUsb_win32.c）同样实例化了
 *     GUID_DEVINTERFACE_COMPORT，两处定义会撞 LNK2005 重复符号；
 *   - DEVPROPKEY 走 DECLSPEC_SELECTANY，多翻译单元重复定义本可合并，但一并
 *     局部化可省去 initguid 的宏开关 juggling。
 * 取值均来自 Windows SDK：
 *   GUID_DEVCLASS_PORTS                          {4D36E978-E325-11CE-BFC1-08002BE10318}
 *   DEVPKEY_Device_Manufacturer                  {A45C254E-DF1C-4EFD-8020-67D146A850E0},13
 */

/**
 * @brief 串口设备类别 GUID（GUID_DEVCLASS_PORTS，"端口 (COM 和 LPT)" 类）。
 * @details SetupDiGetClassDevsW 的首参是"设备类"而非"设备接口"，串口设备全部
 *          归属本类；类下每个设备的 COM 端口名取自其 "Device" 键的 PortName 值。
 */
static const GUID kSerialPortClassGuid =
    {0x4d36e978L, 0xe325, 0x11ce, {0xbf, 0xc1, 0x08, 0x00, 0x2b, 0xe1, 0x03, 0x18}};

/**
 * @brief 设备属性键的格式 GUID（DEVPKEY_Device_*）。
 */
static const GUID kDevicePropertyFmtId =
    {0xa45c254eL, 0xdf1c, 0x4efd, {0x80, 0x20, 0x67, 0xd1, 0x46, 0xa8, 0x50, 0xe0}};

/**
 * @brief 读取设备的注册表字符串属性。
 * @param deviceInfoSet 设备集。
 * @param deviceInfoData 设备数据。
 * @param property 注册表属性码（SPDRP_*）。
 * @param buffer 接收缓冲（宽字符）。
 * @param capacity 缓冲容量（字符数）。
 * @return 读到非空字符串返回 true。
 */
static bool win32GetRegistryString(HDEVINFO deviceInfoSet,
                                   SP_DEVINFO_DATA* deviceInfoData, DWORD property,
                                   wchar_t* buffer, DWORD capacity)
{
    DWORD required = 0;
    if (capacity == 0) return false;
    buffer[0] = L'\0';
    if (!SetupDiGetDeviceRegistryPropertyW(deviceInfoSet, deviceInfoData, property, NULL,
                                           (PBYTE)buffer, (DWORD)(capacity * sizeof(wchar_t)),
                                           &required))
        return false;
    buffer[capacity - 1] = L'\0';
    return buffer[0] != L'\0';
}

/**
 * @brief 读取设备的 DEVPKEY 字符串属性。
 * @param deviceInfoSet 设备集。
 * @param deviceInfoData 设备数据。
 * @param key 属性键。
 * @param buffer 接收缓冲（宽字符）。
 * @param capacity 缓冲容量（字符数）。
 * @return 读到非空字符串返回 true。
 */
static bool win32GetDeviceString(HDEVINFO deviceInfoSet,
                                 SP_DEVINFO_DATA* deviceInfoData, const DEVPROPKEY* key,
                                 wchar_t* buffer, DWORD capacity)
{
    DEVPROPTYPE type = 0;
    DWORD required = 0;
    if (capacity == 0) return false;
    buffer[0] = L'\0';
    if (!SetupDiGetDevicePropertyW(deviceInfoSet, deviceInfoData, key, &type,
                                   (PBYTE)buffer, (DWORD)(capacity * sizeof(wchar_t)),
                                   &required, 0))
        return false;
    if (type != DEVPROP_TYPE_STRING) return false;
    buffer[capacity - 1] = L'\0';
    return buffer[0] != L'\0';
}

/**
 * @brief 从设备的 "Device" 注册表键读取串口名（PortName）。
 * @param deviceInfoSet 设备集。
 * @param deviceInfoData 设备数据。
 * @param buffer 接收缓冲（宽字符）。
 * @param capacity 缓冲容量（字符数）。
 * @return 读到端口名返回 true。
 * @note 串口类设备的端口名（COMx）存放在设备枚举子键的 "Device" 键下的
 *       "PortName" 值里；这是把 PnP 端口实例对应到 COM 端口名的权威来源。
 */
static bool win32ReadPortName(HDEVINFO deviceInfoSet, SP_DEVINFO_DATA* deviceInfoData,
                              wchar_t* buffer, DWORD capacity)
{
    HKEY key;
    DWORD type = 0;
    DWORD size;
    bool ok = false;
    if (capacity == 0) return false;
    buffer[0] = L'\0';
    key = SetupDiOpenDevRegKey(deviceInfoSet, deviceInfoData, DICS_FLAG_GLOBAL, 0,
                               DIREG_DEV, KEY_READ);
    if (!key) return false;
    size = (DWORD)(capacity * sizeof(wchar_t));
    if (RegQueryValueExW(key, L"PortName", NULL, &type, (LPBYTE)buffer, &size) == ERROR_SUCCESS &&
        type == REG_SZ) {
        buffer[capacity - 1] = L'\0';
        ok = buffer[0] != L'\0';
    }
    RegCloseKey(key);
    return ok;
}

/**
 * @brief 解析十六进制数字串（最多 8 位）。
 * @param text 源字符串；不可为 NULL。
 * @param maxLength 最多解析的字符数。
 * @return 解析出的数值；无有效十六进制数字时返回 0。
 */
static uint32_t win32ParseHex(const wchar_t* text, size_t maxLength)
{
    uint32_t value = 0;
    size_t i;
    for (i = 0; i < maxLength; ++i) {
        wchar_t c = text[i];
        int digit;
        if (c >= L'0' && c <= L'9') digit = c - L'0';
        else if (c >= L'A' && c <= L'F') digit = c - L'A' + 10;
        else if (c >= L'a' && c <= L'f') digit = c - L'a' + 10;
        else break;
        value = value * 16 + (uint32_t)digit;
    }
    return value;
}

/**
 * @brief 从设备实例 ID 里解析 USB 厂商 ID 与产品 ID。
 * @param instanceId 设备实例 ID（如 "USB\VID_1A86&PID_7523\9&2952D8C4&0&4"）。
 * @param vendorId 接收厂商 ID。
 * @param productId 接收产品 ID。
 */
static void win32ParseUsbIds(const wchar_t* instanceId, uint32_t* vendorId, uint32_t* productId)
{
    const wchar_t* cursor;
    *vendorId = 0;
    *productId = 0;
    if (!instanceId) return;
    cursor = wcsstr(instanceId, L"VID_");
    if (cursor) *vendorId = win32ParseHex(cursor + 4, 4);
    cursor = wcsstr(instanceId, L"PID_");
    if (cursor) *productId = win32ParseHex(cursor + 4, 4);
}

/**
 * @brief 宽字符转 UTF-8。
 * @param wide 源宽字符串；不可为 NULL。
 * @param buffer 接收缓冲。
 * @param capacity 缓冲容量（字节）。
 * @return 转换成功返回 true。
 */
static bool win32WideToUtf8(const wchar_t* wide, char* buffer, size_t capacity)
{
    int written;
    if (!wide || !buffer || capacity == 0) return false;
    written = WideCharToMultiByte(CP_UTF8, 0, wide, -1, buffer, (int)capacity, NULL, NULL);
    return written > 0;
}

/**
 * @brief 探测端口是否已被占用。
 * @param portName 端口名（如 "COM3"）。
 * @return 已被占用返回 true。
 * @note 探测方式是以独占方式打开端口后立即关闭；若别的进程已持有该端口，
 *       独占打开会因共享冲突失败。
 */
static bool win32PortIsBusy(const wchar_t* portName)
{
    wchar_t path[64];
    HANDLE handle;
    bool busy = false;
    swprintf(path, sizeof(path) / sizeof(path[0]), L"\\\\.\\%s", portName);
    handle = CreateFileW(path, GENERIC_READ | GENERIC_WRITE, 0, NULL, OPEN_EXISTING,
                         FILE_ATTRIBUTE_NORMAL, NULL);
    if (handle == INVALID_HANDLE_VALUE) {
        busy = (GetLastError() == ERROR_ACCESS_DENIED || GetLastError() == ERROR_SHARING_VIOLATION);
    } else {
        CloseHandle(handle);
    }
    return busy;
}

bool XDeviceSerialPort_platformEnumeratePorts(XDeviceSerialPortPortCollector collector, void* userData)
{
    HDEVINFO deviceInfoSet;
    SP_DEVINFO_DATA deviceInfoData;
    DWORD index = 0;
    DEVPROPKEY manufacturerKey;

    if (!collector) return false;
    deviceInfoSet = SetupDiGetClassDevsW(&kSerialPortClassGuid, NULL, NULL, DIGCF_PRESENT);
    if (deviceInfoSet == INVALID_HANDLE_VALUE) return false;

    manufacturerKey.fmtid = kDevicePropertyFmtId;
    manufacturerKey.pid = 13;

    memset(&deviceInfoData, 0, sizeof(deviceInfoData));
    deviceInfoData.cbSize = sizeof(deviceInfoData);

    while (SetupDiEnumDeviceInfo(deviceInfoSet, index++, &deviceInfoData)) {
        wchar_t portName[64];
        wchar_t friendlyName[256];
        wchar_t manufacturer[128];
        wchar_t instanceId[512];
        char portNameUtf8[128];
        char friendlyNameUtf8[512];
        char manufacturerUtf8[256];
        uint32_t vendorId = 0;
        uint32_t productId = 0;
        XDeviceSerialPortPortInfo info;

        if (!win32ReadPortName(deviceInfoSet, &deviceInfoData, portName,
                               (DWORD)(sizeof(portName) / sizeof(portName[0]))))
            continue;
        if (!win32WideToUtf8(portName, portNameUtf8, sizeof(portNameUtf8)))
            continue;

        friendlyNameUtf8[0] = '\0';
        manufacturerUtf8[0] = '\0';
        if (win32GetRegistryString(deviceInfoSet, &deviceInfoData, SPDRP_FRIENDLYNAME,
                                   friendlyName, (DWORD)(sizeof(friendlyName) / sizeof(wchar_t))))
            win32WideToUtf8(friendlyName, friendlyNameUtf8, sizeof(friendlyNameUtf8));
        if (win32GetDeviceString(deviceInfoSet, &deviceInfoData, &manufacturerKey,
                                 manufacturer, (DWORD)(sizeof(manufacturer) / sizeof(wchar_t))))
            win32WideToUtf8(manufacturer, manufacturerUtf8, sizeof(manufacturerUtf8));

        instanceId[0] = L'\0';
        if (SetupDiGetDeviceInstanceIdW(deviceInfoSet, &deviceInfoData, instanceId,
                                        (DWORD)(sizeof(instanceId) / sizeof(wchar_t)), NULL))
            win32ParseUsbIds(instanceId, &vendorId, &productId);

        memset(&info, 0, sizeof(info));
        info.m_portName = portNameUtf8;
        info.m_description = friendlyNameUtf8[0] ? friendlyNameUtf8 : NULL;
        info.m_manufacturer = manufacturerUtf8[0] ? manufacturerUtf8 : NULL;
        info.m_serialNumber = NULL;
        info.m_vendorId = vendorId;
        info.m_productId = productId;
        info.m_busy = win32PortIsBusy(portName);

        if (!collector(userData, &info)) {
            SetupDiDestroyDeviceInfoList(deviceInfoSet);
            return false;
        }
    }
    SetupDiDestroyDeviceInfoList(deviceInfoSet);
    return true;
}

#endif
