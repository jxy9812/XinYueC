/**
 * @file       XDeviceWiFi.c
 * @brief      无线设备类（XDevice 子类）的通用实现、注册与便捷 API 转发。
 * @details    契约见 Src/XDevice/XDeviceWiFi.h；平台叶子实现见各平台后端
 *             （Drive/windows/Device/Network/XDeviceWiFi_win32.c、
 *             Drive/Posix/Device/Network/XDeviceWiFi_posix.c、
 *             Library/lwip/platform/XDeviceWiFi_lwip.c 等）。
 *
 *             分层（与 XDeviceNetwork.c 同构）：
 *              1. 设备类骨架（虚函数表 / init / create），类名 "wifi"；
 *              2. **功能落点**：GetProperty / Control 两个虚槽承载无线专有
 *                 能力，按父类的统一调度分派（属性走 XVariant，命令走
 *                 XVarList 载荷）——业务层只见父类 API，不见平台实现；
 *              3. 便捷 API（XDeviceWiFi_startScan 等）**只做转发**：把参数
 *                 打包成 XVarList 后交父类的 XDevice_control / XDevice_
 *                 getProperty 处理，不含任何平台逻辑；
 *              4. 平台资源（WLAN 客户端句柄等）挂在设备打开/关闭上：打开
 *                 时向平台申请，关闭时归还——不另设模块级 deinit；
 *              5. 注册到 XDevice 注册表（XDevice_openClass("wifi", ...) 会
 *                 懒注册）。
 */
#include "XNetwork_config.h"

#if XDEVICE_ON && XDEVICE_WIFI_ON

#include "XDeviceWiFi.h"
#include "XDevice.h"
#include "XMemory.h"
#include "XVariant.h"
#include "XVarList.h"

#include <string.h>

/* =========================================================================
 * 设备上下文
 * ========================================================================= */

/**
 * @brief 无线设备的打开上下文。
 * @details 基类必须是第一个成员；无线设备无字节流 I/O，句柄仅用于承载
 *          生命周期与平台资源归属。
 */
typedef struct XDeviceWiFiContext
{
    XDeviceContext m_base;
} XDeviceWiFiContext;

/** @brief 打开上下文转型（基类为首成员，可直接强转）。 */
static XDeviceWiFiContext* wifiCtx(XDeviceContext* handle)
{
    return (XDeviceWiFiContext*)handle;
}

/** @brief 在上下文中记录框架错误码并返回 false。 */
static bool wifiSetError(XDeviceWiFiContext* ctx, XDeviceError error)
{
    if (ctx) ctx->m_base.m_lastError = (int16_t)error;
    return false;
}

/* =========================================================================
 * 虚函数实现：Open / Close
 * ========================================================================= */

/**
 * @brief 打开无线设备：申请平台无线资源并建立打开上下文。
 * @param self 设备类；@param opts 打开选项（借用，可为 NULL）；
 *        @param err 错误码输出（可为 NULL）。
 * @return 新上下文；失败返回 NULL。
 */
static XDeviceContext* VXDeviceWiFi_open(XDevice* self,
                                          const XDeviceOpenOptions* opts,
                                          int* err)
{
    XDeviceWiFiContext* ctx;
    (void)opts;
    if (!XDeviceWiFi_platformOpen()) {
        if (err) *err = (int)XDeviceError_IoFail;
        return NULL;
    }
    ctx = (XDeviceWiFiContext*)XCalloc_System(1, sizeof(XDeviceWiFiContext));
    if (!ctx) {
        XDeviceWiFi_platformClose();
        if (err) *err = (int)XDeviceError_OutOfMemory;
        return NULL;
    }
    ctx->m_base.m_device = self;
    ctx->m_base.m_fd = XFD_INVALID;
    ctx->m_base.m_state = (uint16_t)XDeviceState_Active;
    ctx->m_base.m_ioMode = (uint16_t)XDeviceIoMode_Sync;
    ctx->m_base.m_pendingOps = 0;
    ctx->m_base.m_lastError = (int16_t)XDeviceError_None;
    if (err) *err = (int)XDeviceError_None;
    return &ctx->m_base;
}

/**
 * @brief 关闭无线设备：归还平台无线资源并释放打开上下文。
 * @param self 设备类；@param handle 待关闭上下文（由设备拥有）。
 */
static void VXDeviceWiFi_close(XDevice* self, XDeviceContext* handle)
{
    XDeviceWiFiContext* ctx = wifiCtx(handle);
    (void)self;
    if (!ctx) return;
    XDeviceWiFi_platformClose();
    XFree_System(ctx);
}

/* =========================================================================
 * 虚函数实现：属性（父类统一调度入口之一）
 * ========================================================================= */

/**
 * @brief 读取设备属性。
 * @param self 设备类；@param handle 打开上下文；@param property 属性号；
 *        @param value 输出（由本函数填充）。
 * @return 成功返回 true；属性不支持返回 false。
 * @details 除父类通用属性外，仅 XDeviceWiFiProperty_Capabilities 一项
 *          设备级无线属性；按适配器区分的查询走命令通道。
 */
static bool VXDeviceWiFi_getProperty(XDevice* self, XDeviceContext* handle,
                                      uint32_t property, XVariant* value)
{
    XDeviceWiFiContext* ctx = wifiCtx(handle);
    uint32_t capabilities = 0;
    (void)self;
    if (!ctx || !value) return false;
    switch (property) {
    case XDeviceProperty_OpenMode:
        XVariant_setValue_int(value, 0);
        return true;
    case XDeviceProperty_NonBlocking:
        XVariant_setValue_bool(value, true);
        return true;
    case XDeviceProperty_IoMode:
        XVariant_setValue_int(value, (int)ctx->m_base.m_ioMode);
        return true;
    case XDeviceProperty_State:
        XVariant_setValue_int(value, (int)ctx->m_base.m_state);
        return true;
    case XDeviceProperty_LastError:
        XVariant_setValue_int(value, (int)ctx->m_base.m_lastError);
        return true;
    case XDeviceWiFiProperty_Capabilities:
        if (!XDeviceWiFi_platformCapabilities(&capabilities))
            return wifiSetError(ctx, XDeviceError_IoFail);
        XVariant_setValue_uint32(value, capabilities);
        return true;
    default:
        return false;
    }
}

/**
 * @brief 查询设备属性（与读取同义：本类属性均无副作用）。
 * @param self 设备类；@param handle 打开上下文；@param property 属性号；
 *        @param value 输出（由本函数填充）。
 * @return 成功返回 true。
 */
static bool VXDeviceWiFi_queryProperty(XDevice* self, XDeviceContext* handle,
                                        uint32_t property, XVariant* value)
{
    return VXDeviceWiFi_getProperty(self, handle, property, value);
}

/**
 * @brief 写入设备属性。
 * @param self 设备类；@param handle 打开上下文；@param property 属性号；
 *        @param value 输入值（借用）。
 * @return 成功返回 true。
 * @details 无线设备属性均为只读，写入一律失败。
 */
static bool VXDeviceWiFi_setProperty(XDevice* self, XDeviceContext* handle,
                                      uint32_t property, const XVariant* value)
{
    XDeviceWiFiContext* ctx = wifiCtx(handle);
    (void)self;
    (void)property;
    (void)value;
    return wifiSetError(ctx, XDeviceError_InvalidArgument);
}

/* =========================================================================
 * 虚函数实现：命令（父类统一调度入口之二）
 * ========================================================================= */

/**
 * @brief 执行设备专有命令。
 * @param self 设备类；@param handle 打开上下文；@param command 命令号；
 *        @param in 输入参数表（XVarList 载荷，可为 NULL）；
 *        @param out 输出参数表（XVarList 载荷，可为 NULL）。
 * @return 成功返回 true；失败返回 false。
 */
static bool VXDeviceWiFi_control(XDevice* self, XDeviceContext* handle,
                                 uint32_t command, const XVarList* in,
                                 XVarList* out)
{
    XDeviceWiFiContext* ctx = wifiCtx(handle);
    const XVarList* arguments = in;
    (void)self;
    if (!ctx) return false;

    switch (command) {
    case XDeviceWiFiCommand_EnumAdapters:
    {
        XVector** adapters;
        if (in || !out || out->m_size != sizeof(adapters))
            return wifiSetError(ctx, XDeviceError_InvalidArgument);
        XVarList_start(out);
        adapters = XVarList_arg(out, XVector**);
        if (!adapters)
            return wifiSetError(ctx, XDeviceError_InvalidArgument);
        if (!XDeviceWiFi_platformEnumAdapters(adapters))
            return wifiSetError(ctx, XDeviceError_IoFail);
        return true;
    }
    case XDeviceWiFiCommand_GetScanState:
    {
        const char* adapterId;
        int state = 0;
        if (!arguments || arguments->m_size != sizeof(adapterId) ||
            !out || out->m_size != sizeof(state))
            return wifiSetError(ctx, XDeviceError_InvalidArgument);
        XVarList_start(arguments);
        adapterId = XVarList_arg(arguments, const char*);
        if (!adapterId)
            return wifiSetError(ctx, XDeviceError_InvalidArgument);
        state = (int)XDeviceWiFi_platformScanState(adapterId);
        XVarList_start(out);
        memcpy(out->data, &state, sizeof(state));
        XVarList_start(out);
        return true;
    }
    case XDeviceWiFiCommand_StartScan:
    {
        const char* adapterId;
        if (!arguments || arguments->m_size != sizeof(adapterId))
            return wifiSetError(ctx, XDeviceError_InvalidArgument);
        XVarList_start(arguments);
        adapterId = XVarList_arg(arguments, const char*);
        if (!adapterId)
            return wifiSetError(ctx, XDeviceError_InvalidArgument);
        if (!XDeviceWiFi_platformStartScan(adapterId))
            return wifiSetError(ctx, XDeviceError_IoFail);
        return true;
    }
    case XDeviceWiFiCommand_TakeScanResults:
    {
        const char* adapterId;
        XVector** networks;
        if (!arguments || arguments->m_size != sizeof(adapterId) ||
            !out || out->m_size != sizeof(networks))
            return wifiSetError(ctx, XDeviceError_InvalidArgument);
        XVarList_start(arguments);
        adapterId = XVarList_arg(arguments, const char*);
        XVarList_start(out);
        networks = XVarList_arg(out, XVector**);
        if (!adapterId || !networks)
            return wifiSetError(ctx, XDeviceError_InvalidArgument);
        if (!XDeviceWiFi_platformTakeScanResults(adapterId, networks))
            return wifiSetError(ctx, XDeviceError_IoFail);
        return true;
    }
    case XDeviceWiFiCommand_StartConnect:
    {
        const char* adapterId;
        const char* ssid;
        const char* passphrase;
        int security;
        if (!arguments ||
            arguments->m_size != sizeof(adapterId) + sizeof(ssid) +
                               sizeof(passphrase) + sizeof(security))
            return wifiSetError(ctx, XDeviceError_InvalidArgument);
        XVarList_start(arguments);
        adapterId = XVarList_arg(arguments, const char*);
        ssid = XVarList_arg(arguments, const char*);
        passphrase = XVarList_arg(arguments, const char*);
        security = XVarList_arg(arguments, int);
        if (!adapterId || !ssid)
            return wifiSetError(ctx, XDeviceError_InvalidArgument);
        if (!XDeviceWiFi_platformStartConnect(adapterId, ssid, passphrase,
                                             (XDeviceWiFiSecurity)security))
            return wifiSetError(ctx, XDeviceError_IoFail);
        return true;
    }
    case XDeviceWiFiCommand_GetConnectState:
    {
        const char* adapterId;
        int state = 0;
        if (!arguments || arguments->m_size != sizeof(adapterId) ||
            !out || out->m_size != sizeof(state))
            return wifiSetError(ctx, XDeviceError_InvalidArgument);
        XVarList_start(arguments);
        adapterId = XVarList_arg(arguments, const char*);
        if (!adapterId)
            return wifiSetError(ctx, XDeviceError_InvalidArgument);
        state = (int)XDeviceWiFi_platformConnectState(adapterId);
        XVarList_start(out);
        memcpy(out->data, &state, sizeof(state));
        XVarList_start(out);
        return true;
    }
    case XDeviceWiFiCommand_QueryConnection:
    {
        const char* adapterId;
        XDeviceWiFiConnectionInfo* info;
        if (!arguments || arguments->m_size != sizeof(adapterId) ||
            !out || out->m_size != sizeof(info))
            return wifiSetError(ctx, XDeviceError_InvalidArgument);
        XVarList_start(arguments);
        adapterId = XVarList_arg(arguments, const char*);
        XVarList_start(out);
        info = XVarList_arg(out, XDeviceWiFiConnectionInfo*);
        if (!adapterId || !info)
            return wifiSetError(ctx, XDeviceError_InvalidArgument);
        if (!XDeviceWiFi_platformQueryConnection(adapterId, info))
            return wifiSetError(ctx, XDeviceError_IoFail);
        return true;
    }
    case XDeviceWiFiCommand_Disconnect:
    {
        const char* adapterId;
        if (!arguments || arguments->m_size != sizeof(adapterId))
            return wifiSetError(ctx, XDeviceError_InvalidArgument);
        XVarList_start(arguments);
        adapterId = XVarList_arg(arguments, const char*);
        if (!adapterId)
            return wifiSetError(ctx, XDeviceError_InvalidArgument);
        if (!XDeviceWiFi_platformDisconnect(adapterId))
            return wifiSetError(ctx, XDeviceError_IoFail);
        return true;
    }
    case XDeviceWiFiCommand_ForgetProfile:
    {
        const char* adapterId;
        const char* ssid;
        if (!arguments ||
            arguments->m_size != sizeof(adapterId) + sizeof(ssid))
            return wifiSetError(ctx, XDeviceError_InvalidArgument);
        XVarList_start(arguments);
        adapterId = XVarList_arg(arguments, const char*);
        ssid = XVarList_arg(arguments, const char*);
        if (!adapterId || !ssid)
            return wifiSetError(ctx, XDeviceError_InvalidArgument);
        if (!XDeviceWiFi_platformForgetProfile(adapterId, ssid))
            return wifiSetError(ctx, XDeviceError_IoFail);
        return true;
    }
    default:
        return false;
    }
}

/* =========================================================================
 * 便捷 API：薄转发（只经父类 XDevice_getProperty / XDevice_control）
 * ========================================================================= */

/**
 * @brief 读取属性到 XVariant（内部工具）。
 * @param fd 设备句柄；@param property 属性号；@param value 输出。
 * @return 成功返回 true。
 */
static bool wifiGetPropertyVariant(XFd fd, XDeviceWiFiProperty property,
                                   XVariant* value)
{
    bool result;
    memset(value, 0, sizeof(*value));
    XVariant_init(value, NULL, 0, XVariantType_NULL);
    result = XDevice_getProperty(fd, (XDeviceProperty)property, value);
    if (!result)
        XClassDeinit((XClass*)value);
    return result;
}

uint32_t XDeviceWiFi_capabilities(XFd fd)
{
    XVariant variant;
    uint32_t capabilities = 0;
    if (!wifiGetPropertyVariant(fd, XDeviceWiFiProperty_Capabilities, &variant))
        return 0;
    capabilities = XVariant_toUint32(&variant);
    XClassDeinit((XClass*)&variant);
    return capabilities;
}

bool XDeviceWiFi_enumAdapters(XFd fd, XVector** outAdapters)
{
    XVarList* output;
    bool result;
    if (!outAdapters) return false;
    *outAdapters = NULL;
    output = XVarList_Create(XVar(XVector**, outAdapters));
    if (!output) return false;
    result = XDevice_control(fd, XDeviceWiFiCommand_EnumAdapters, NULL, output);
    XVarList_delete(output);
    return result && *outAdapters != NULL;
}

bool XDeviceWiFi_startScan(XFd fd, const char* adapterId)
{
    XVarList* input;
    bool result;
    if (!adapterId) return false;
    input = XVarList_Create(XVar(const char*, adapterId));
    if (!input) return false;
    result = XDevice_control(fd, XDeviceWiFiCommand_StartScan, input, NULL);
    XVarList_delete(input);
    return result;
}

XDeviceWiFiScanState XDeviceWiFi_scanState(XFd fd, const char* adapterId)
{
    XVarList* input;
    XVarList* output;
    int state = 0;
    bool result;
    if (!adapterId) return XDeviceWiFi_ScanIdle;
    input = XVarList_Create(XVar(const char*, adapterId));
    output = XVarList_Create(XVar(int, state));
    if (!input || !output) {
        if (input) XVarList_delete(input);
        if (output) XVarList_delete(output);
        return XDeviceWiFi_ScanIdle;
    }
    result = XDevice_control(fd, XDeviceWiFiCommand_GetScanState, input, output);
    if (result) {
        XVarList_start(output);
        state = XVarList_arg(output, int);
    }
    XVarList_delete(input);
    XVarList_delete(output);
    return (XDeviceWiFiScanState)state;
}

bool XDeviceWiFi_takeScanResults(XFd fd, const char* adapterId,
                                  XVector** outNetworks)
{
    XVarList* input;
    XVarList* output;
    bool result;
    if (!adapterId || !outNetworks) return false;
    *outNetworks = NULL;
    input = XVarList_Create(XVar(const char*, adapterId));
    output = XVarList_Create(XVar(XVector**, outNetworks));
    if (!input || !output) {
        if (input) XVarList_delete(input);
        if (output) XVarList_delete(output);
        return false;
    }
    result = XDevice_control(fd, XDeviceWiFiCommand_TakeScanResults, input,
                             output);
    XVarList_delete(input);
    XVarList_delete(output);
    return result && *outNetworks != NULL;
}

bool XDeviceWiFi_startConnect(XFd fd, const char* adapterId, const char* ssid,
                              const char* passphrase,
                              XDeviceWiFiSecurity security)
{
    XVarList* input;
    bool result;
    int securityValue = (int)security;
    if (!adapterId || !ssid) return false;
    input = XVarList_Create(XVar(const char*, adapterId), XVar(const char*, ssid),
                            XVar(const char*, passphrase),
                            XVar(int, securityValue));
    if (!input) return false;
    result = XDevice_control(fd, XDeviceWiFiCommand_StartConnect, input, NULL);
    XVarList_delete(input);
    return result;
}

XDeviceWiFiConnectState XDeviceWiFi_connectState(XFd fd, const char* adapterId)
{
    XVarList* input;
    XVarList* output;
    int state = 0;
    bool result;
    if (!adapterId) return XDeviceWiFi_ConnectIdle;
    input = XVarList_Create(XVar(const char*, adapterId));
    output = XVarList_Create(XVar(int, state));
    if (!input || !output) {
        if (input) XVarList_delete(input);
        if (output) XVarList_delete(output);
        return XDeviceWiFi_ConnectIdle;
    }
    result = XDevice_control(fd, XDeviceWiFiCommand_GetConnectState, input,
                             output);
    if (result) {
        XVarList_start(output);
        state = XVarList_arg(output, int);
    }
    XVarList_delete(input);
    XVarList_delete(output);
    return (XDeviceWiFiConnectState)state;
}

bool XDeviceWiFi_queryConnection(XFd fd, const char* adapterId,
                                 XDeviceWiFiConnectionInfo* outInfo)
{
    XVarList* input;
    XVarList* output;
    bool result;
    if (!adapterId || !outInfo) return false;
    input = XVarList_Create(XVar(const char*, adapterId));
    output = XVarList_Create(XVar(XDeviceWiFiConnectionInfo*, outInfo));
    if (!input || !output) {
        if (input) XVarList_delete(input);
        if (output) XVarList_delete(output);
        return false;
    }
    result = XDevice_control(fd, XDeviceWiFiCommand_QueryConnection, input,
                             output);
    XVarList_delete(input);
    XVarList_delete(output);
    return result;
}

bool XDeviceWiFi_disconnect(XFd fd, const char* adapterId)
{
    XVarList* input;
    bool result;
    if (!adapterId) return false;
    input = XVarList_Create(XVar(const char*, adapterId));
    if (!input) return false;
    result = XDevice_control(fd, XDeviceWiFiCommand_Disconnect, input, NULL);
    XVarList_delete(input);
    return result;
}

bool XDeviceWiFi_forgetProfile(XFd fd, const char* adapterId, const char* ssid)
{
    XVarList* input;
    bool result;
    if (!adapterId || !ssid) return false;
    input = XVarList_Create(XVar(const char*, adapterId), XVar(const char*, ssid));
    if (!input) return false;
    result = XDevice_control(fd, XDeviceWiFiCommand_ForgetProfile, input, NULL);
    XVarList_delete(input);
    return result;
}

/* =========================================================================
 * 释放接口（与平台无关）
 * ========================================================================= */

void XDeviceWiFi_freeAdapterInfo(XDeviceWiFiAdapterInfo* info)
{
    if (!info) return;
    if (info->id) XClassDelete(info->id);
    if (info->name) XClassDelete(info->name);
    if (info->description) XClassDelete(info->description);
    info->id = NULL;
    info->name = NULL;
    info->description = NULL;
    XFree_System(info);
}

void XDeviceWiFi_freeNetworkInfo(XDeviceWiFiNetworkInfo* info)
{
    if (!info) return;
    if (info->ssid) XClassDelete(info->ssid);
    if (info->bssid) XClassDelete(info->bssid);
    info->ssid = NULL;
    info->bssid = NULL;
    XFree_System(info);
}

void XDeviceWiFi_freeConnectionInfo(XDeviceWiFiConnectionInfo* info)
{
    if (!info) return;
    if (info->ssid) XClassDelete(info->ssid);
    if (info->bssid) XClassDelete(info->bssid);
    info->ssid = NULL;
    info->bssid = NULL;
}

void XDeviceWiFi_freeAdapters(XVector* adapters)
{
    size_t i;
    size_t count;
    if (!adapters) return;
    count = XVector_size_base(adapters);
    for (i = 0; i < count; ++i) {
        XDeviceWiFiAdapterInfo** slot =
            (XDeviceWiFiAdapterInfo**)XVector_at_base(adapters, (int64_t)i);
        if (slot && *slot) {
            XDeviceWiFi_freeAdapterInfo(*slot);
            *slot = NULL;
        }
    }
    XClassDelete(adapters);
}

void XDeviceWiFi_freeNetworks(XVector* networks)
{
    size_t i;
    size_t count;
    if (!networks) return;
    count = XVector_size_base(networks);
    for (i = 0; i < count; ++i) {
        XDeviceWiFiNetworkInfo** slot =
            (XDeviceWiFiNetworkInfo**)XVector_at_base(networks, (int64_t)i);
        if (slot && *slot) {
            XDeviceWiFi_freeNetworkInfo(*slot);
            *slot = NULL;
        }
    }
    XClassDelete(networks);
}

/* =========================================================================
 * 设备类
 * ========================================================================= */

XVtable* XDeviceWiFi_class_init(void)
{
    XVTABLE_INIT_DEFAULT(XDeviceWiFi);
    XVTABLE_INHERIT_XCLASS(XDevice);
    XVTABLE_OVERLOAD_DEFAULT(EXDevice_Open, VXDeviceWiFi_open);
    XVTABLE_OVERLOAD_DEFAULT(EXDevice_Close, VXDeviceWiFi_close);
    XVTABLE_OVERLOAD_DEFAULT(EXDevice_SetProperty, VXDeviceWiFi_setProperty);
    XVTABLE_OVERLOAD_DEFAULT(EXDevice_GetProperty, VXDeviceWiFi_getProperty);
    XVTABLE_OVERLOAD_DEFAULT(EXDevice_QueryProperty, VXDeviceWiFi_queryProperty);
    XVTABLE_OVERLOAD_DEFAULT(EXDevice_Control, VXDeviceWiFi_control);
    XCLASS_SET_CLASS_NAME_DEFAULT("wifi");
    XCLASS_SHOW_SIZE_DEFAULT(XDeviceWiFi);
    return XVTABLE_DEFAULT;
}

void XDeviceWiFi_init(XDeviceWiFi* self)
{
    if (!self) return;
    memset(((XDevice*)self) + 1, 0, sizeof(XDeviceWiFi) - sizeof(XDevice));
    XDevice_init(&self->m_base);
    XClassSetVtable(self, XDeviceWiFi);
    self->m_base.m_type = XDeviceType_Class;
    /* 无线设备无字节流 I/O 能力，仅承载生命周期与设备专有 API。 */
    self->m_base.m_capabilities = XDeviceCap_None;
}

XDeviceWiFi* XDeviceWiFi_create(void)
{
    XDeviceWiFi* self = (XDeviceWiFi*)XClass_Malloc(XDeviceWiFi);
    if (!self) return NULL;
    XDeviceWiFi_init(self);
    Set_Class_IsHeap(self, true);
    return self;
}

static XDeviceWiFi g_deviceWiFi;

bool XDeviceWiFi_register(void)
{
    static bool registered = false;
    if (registered) return true;
    XDeviceWiFi_init(&g_deviceWiFi);
    if (!XDevice_register(&g_deviceWiFi.m_base)) return false;
    registered = true;
    return true;
}

#endif /* XNETWORK_ON && XNETWORK_WIFI_ON */
