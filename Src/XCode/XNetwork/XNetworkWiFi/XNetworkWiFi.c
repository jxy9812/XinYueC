/**
 * @file       XNetworkWiFi.c
 * @brief      无线网络业务层实现（值语义包装 + 设备会话管理）。
 * @details    契约见 Src/XCode/XNetwork/XNetworkWiFi/XNetworkWiFi.h。
 *
 *             分层纪律：本层**只经 L0 的设备层便捷 API**（XDeviceWiFi_*，
 *             均带设备句柄）访问无线能力，不碰平台实现、不出现任何平台
 *             API；L0 的 C 结构体结果在此转换为值语义的 XClass 对象。
 */
#include "XNetworkWiFi.h"

#if XNETWORK_ON
#if XDEVICE_WIFI_ON

#include "XDeviceWiFi.h"
#include "XMemory.h"
#include "XContainer.h"

#include <string.h>

/* =========================================================================
 * 内部辅助
 * ========================================================================= */

/** @brief 字符串成员深拷贝（dest 已有串则原地拷贝，否则新建）。 */
static void wifiCopyMember(XString** dest, const XString* src)
{
    if (!dest) return;
    if (*dest) {
        if (src)
            XClassCopy(*dest, src);
        else {
            XClassDelete(*dest);
            *dest = NULL;
        }
        return;
    }
    if (src)
        *dest = XString_create_copy(src);
}

/** @brief 字符串成员所有权移交（dest 旧串释放，接管 src）。 */
static void wifiMoveMember(XString** dest, XString** src)
{
    if (!dest || !src) return;
    if (*dest) {
        XClassDelete(*dest);
        *dest = NULL;
    }
    *dest = *src;
    *src = NULL;
}

/* =========================================================================
 * 值对象：XNetworkWiFiAdapter
 * ========================================================================= */

static void VXNetworkWiFiAdapter_deinit(XNetworkWiFiAdapter* adapter);
static void VXNetworkWiFiAdapter_copy(XNetworkWiFiAdapter* dest,
                                       const XNetworkWiFiAdapter* src);
static void VXNetworkWiFiAdapter_move(XNetworkWiFiAdapter* dest,
                                       XNetworkWiFiAdapter* src);

XVtable* XNetworkWiFiAdapter_class_init(void)
{
    XVTABLE_INIT_DEFAULT(XNetworkWiFiAdapter)
        XVTABLE_INHERIT_XCLASS(XClass);
        XVTABLE_OVERLOAD_DEFAULT(EXClass_Deinit, VXNetworkWiFiAdapter_deinit);
        XVTABLE_OVERLOAD_DEFAULT(EXClass_Copy, VXNetworkWiFiAdapter_copy);
        XVTABLE_OVERLOAD_DEFAULT(EXClass_Move, VXNetworkWiFiAdapter_move);
        XCLASS_SHOW_SIZE_DEFAULT(XNetworkWiFiAdapter);
        return XVTABLE_DEFAULT;
}

static void VXNetworkWiFiAdapter_deinit(XNetworkWiFiAdapter* adapter)
{
    if (!adapter) return;
    if (adapter->m_id) {
        XClassDelete(adapter->m_id);
        adapter->m_id = NULL;
    }
    if (adapter->m_name) {
        XClassDelete(adapter->m_name);
        adapter->m_name = NULL;
    }
    if (adapter->m_description) {
        XClassDelete(adapter->m_description);
        adapter->m_description = NULL;
    }
}

static void VXNetworkWiFiAdapter_copy(XNetworkWiFiAdapter* dest,
                                       const XNetworkWiFiAdapter* src)
{
    if (!dest || !src || dest == src) return;
    if (XClassIsVtableNull(dest))
        XNetworkWiFiAdapter_init(dest);
    dest->m_ifIndex = src->m_ifIndex;
    dest->m_capabilities = src->m_capabilities;
    dest->m_mode = src->m_mode;
    wifiCopyMember(&dest->m_id, src->m_id);
    wifiCopyMember(&dest->m_name, src->m_name);
    wifiCopyMember(&dest->m_description, src->m_description);
}

static void VXNetworkWiFiAdapter_move(XNetworkWiFiAdapter* dest,
                                       XNetworkWiFiAdapter* src)
{
    if (!dest || !src || dest == src) return;
    if (XClassIsVtableNull(dest))
        XNetworkWiFiAdapter_init(dest);
    dest->m_ifIndex = src->m_ifIndex;
    dest->m_capabilities = src->m_capabilities;
    dest->m_mode = src->m_mode;
    wifiMoveMember(&dest->m_id, &src->m_id);
    wifiMoveMember(&dest->m_name, &src->m_name);
    wifiMoveMember(&dest->m_description, &src->m_description);
}

void XNetworkWiFiAdapter_init(XNetworkWiFiAdapter* adapter)
{
    if (!adapter) return;
    memset(((XClass*)adapter) + 1, 0,
           sizeof(XNetworkWiFiAdapter) - sizeof(XClass));
    XClass_init((XClass*)adapter);
    XClassGetVtable(adapter) = XNetworkWiFiAdapter_class_init();
    adapter->m_id = NULL;
    adapter->m_name = NULL;
    adapter->m_description = NULL;
    adapter->m_ifIndex = 0;
    adapter->m_capabilities = 0;
    adapter->m_mode = XDeviceWiFi_ModeUnknown;
}

XNetworkWiFiAdapter* XNetworkWiFiAdapter_create_ex(XMemoryType memory)
{
    XNetworkWiFiAdapter* adapter =
        (XNetworkWiFiAdapter*)XMemory_malloc(sizeof(XNetworkWiFiAdapter), memory);
    if (!adapter) return NULL;
    XNetworkWiFiAdapter_init(adapter);
    Set_Class_Memory(adapter, memory);
    Set_Class_IsHeap(adapter, true);
    return adapter;
}

const XString* XNetworkWiFiAdapter_id(const XNetworkWiFiAdapter* adapter)
{
    return adapter ? adapter->m_id : NULL;
}

const XString* XNetworkWiFiAdapter_name(const XNetworkWiFiAdapter* adapter)
{
    return adapter ? adapter->m_name : NULL;
}

const XString* XNetworkWiFiAdapter_description(const XNetworkWiFiAdapter* adapter)
{
    return adapter ? adapter->m_description : NULL;
}

uint32_t XNetworkWiFiAdapter_ifIndex(const XNetworkWiFiAdapter* adapter)
{
    return adapter ? adapter->m_ifIndex : 0;
}

uint32_t XNetworkWiFiAdapter_capabilities(const XNetworkWiFiAdapter* adapter)
{
    return adapter ? adapter->m_capabilities : 0;
}

XDeviceWiFiMode XNetworkWiFiAdapter_mode(const XNetworkWiFiAdapter* adapter)
{
    return adapter ? adapter->m_mode : XDeviceWiFi_ModeUnknown;
}

/* =========================================================================
 * 值对象：XNetworkWiFiNetwork
 * ========================================================================= */

static void VXNetworkWiFiNetwork_deinit(XNetworkWiFiNetwork* network);
static void VXNetworkWiFiNetwork_copy(XNetworkWiFiNetwork* dest,
                                       const XNetworkWiFiNetwork* src);
static void VXNetworkWiFiNetwork_move(XNetworkWiFiNetwork* dest,
                                       XNetworkWiFiNetwork* src);

XVtable* XNetworkWiFiNetwork_class_init(void)
{
    XVTABLE_INIT_DEFAULT(XNetworkWiFiNetwork)
        XVTABLE_INHERIT_XCLASS(XClass);
        XVTABLE_OVERLOAD_DEFAULT(EXClass_Deinit, VXNetworkWiFiNetwork_deinit);
        XVTABLE_OVERLOAD_DEFAULT(EXClass_Copy, VXNetworkWiFiNetwork_copy);
        XVTABLE_OVERLOAD_DEFAULT(EXClass_Move, VXNetworkWiFiNetwork_move);
        XCLASS_SHOW_SIZE_DEFAULT(XNetworkWiFiNetwork);
        return XVTABLE_DEFAULT;
}

static void VXNetworkWiFiNetwork_deinit(XNetworkWiFiNetwork* network)
{
    if (!network) return;
    if (network->m_ssid) {
        XClassDelete(network->m_ssid);
        network->m_ssid = NULL;
    }
    if (network->m_bssid) {
        XClassDelete(network->m_bssid);
        network->m_bssid = NULL;
    }
}

static void VXNetworkWiFiNetwork_copy(XNetworkWiFiNetwork* dest,
                                       const XNetworkWiFiNetwork* src)
{
    if (!dest || !src || dest == src) return;
    if (XClassIsVtableNull(dest))
        XNetworkWiFiNetwork_init(dest);
    dest->m_signalStrength = src->m_signalStrength;
    dest->m_channel = src->m_channel;
    dest->m_security = src->m_security;
    dest->m_secured = src->m_secured;
    wifiCopyMember(&dest->m_ssid, src->m_ssid);
    wifiCopyMember(&dest->m_bssid, src->m_bssid);
}

static void VXNetworkWiFiNetwork_move(XNetworkWiFiNetwork* dest,
                                       XNetworkWiFiNetwork* src)
{
    if (!dest || !src || dest == src) return;
    if (XClassIsVtableNull(dest))
        XNetworkWiFiNetwork_init(dest);
    dest->m_signalStrength = src->m_signalStrength;
    dest->m_channel = src->m_channel;
    dest->m_security = src->m_security;
    dest->m_secured = src->m_secured;
    wifiMoveMember(&dest->m_ssid, &src->m_ssid);
    wifiMoveMember(&dest->m_bssid, &src->m_bssid);
}

void XNetworkWiFiNetwork_init(XNetworkWiFiNetwork* network)
{
    if (!network) return;
    memset(((XClass*)network) + 1, 0,
           sizeof(XNetworkWiFiNetwork) - sizeof(XClass));
    XClass_init((XClass*)network);
    XClassGetVtable(network) = XNetworkWiFiNetwork_class_init();
    network->m_ssid = NULL;
    network->m_bssid = NULL;
    network->m_signalStrength = -1;
    network->m_channel = 0;
    network->m_security = XDeviceWiFi_SecurityUnknown;
    network->m_secured = false;
}

XNetworkWiFiNetwork* XNetworkWiFiNetwork_create_ex(XMemoryType memory)
{
    XNetworkWiFiNetwork* network =
        (XNetworkWiFiNetwork*)XMemory_malloc(sizeof(XNetworkWiFiNetwork), memory);
    if (!network) return NULL;
    XNetworkWiFiNetwork_init(network);
    Set_Class_Memory(network, memory);
    Set_Class_IsHeap(network, true);
    return network;
}

const XString* XNetworkWiFiNetwork_ssid(const XNetworkWiFiNetwork* network)
{
    return network ? network->m_ssid : NULL;
}

const XString* XNetworkWiFiNetwork_bssid(const XNetworkWiFiNetwork* network)
{
    return network ? network->m_bssid : NULL;
}

int XNetworkWiFiNetwork_signalStrength(const XNetworkWiFiNetwork* network)
{
    return network ? network->m_signalStrength : -1;
}

int XNetworkWiFiNetwork_channel(const XNetworkWiFiNetwork* network)
{
    return network ? network->m_channel : 0;
}

XDeviceWiFiSecurity XNetworkWiFiNetwork_security(const XNetworkWiFiNetwork* network)
{
    return network ? network->m_security : XDeviceWiFi_SecurityUnknown;
}

bool XNetworkWiFiNetwork_isSecured(const XNetworkWiFiNetwork* network)
{
    return network ? network->m_secured : false;
}

/* =========================================================================
 * 值对象：XNetworkWiFiConnection
 * ========================================================================= */

static void VXNetworkWiFiConnection_deinit(XNetworkWiFiConnection* connection);
static void VXNetworkWiFiConnection_copy(XNetworkWiFiConnection* dest,
                                          const XNetworkWiFiConnection* src);
static void VXNetworkWiFiConnection_move(XNetworkWiFiConnection* dest,
                                          XNetworkWiFiConnection* src);

XVtable* XNetworkWiFiConnection_class_init(void)
{
    XVTABLE_INIT_DEFAULT(XNetworkWiFiConnection)
        XVTABLE_INHERIT_XCLASS(XClass);
        XVTABLE_OVERLOAD_DEFAULT(EXClass_Deinit, VXNetworkWiFiConnection_deinit);
        XVTABLE_OVERLOAD_DEFAULT(EXClass_Copy, VXNetworkWiFiConnection_copy);
        XVTABLE_OVERLOAD_DEFAULT(EXClass_Move, VXNetworkWiFiConnection_move);
        XCLASS_SHOW_SIZE_DEFAULT(XNetworkWiFiConnection);
        return XVTABLE_DEFAULT;
}

static void VXNetworkWiFiConnection_deinit(XNetworkWiFiConnection* connection)
{
    if (!connection) return;
    if (connection->m_ssid) {
        XClassDelete(connection->m_ssid);
        connection->m_ssid = NULL;
    }
    if (connection->m_bssid) {
        XClassDelete(connection->m_bssid);
        connection->m_bssid = NULL;
    }
}

static void VXNetworkWiFiConnection_copy(XNetworkWiFiConnection* dest,
                                          const XNetworkWiFiConnection* src)
{
    if (!dest || !src || dest == src) return;
    if (XClassIsVtableNull(dest))
        XNetworkWiFiConnection_init(dest);
    dest->m_signalStrength = src->m_signalStrength;
    dest->m_security = src->m_security;
    dest->m_state = src->m_state;
    wifiCopyMember(&dest->m_ssid, src->m_ssid);
    wifiCopyMember(&dest->m_bssid, src->m_bssid);
}

static void VXNetworkWiFiConnection_move(XNetworkWiFiConnection* dest,
                                          XNetworkWiFiConnection* src)
{
    if (!dest || !src || dest == src) return;
    if (XClassIsVtableNull(dest))
        XNetworkWiFiConnection_init(dest);
    dest->m_signalStrength = src->m_signalStrength;
    dest->m_security = src->m_security;
    dest->m_state = src->m_state;
    wifiMoveMember(&dest->m_ssid, &src->m_ssid);
    wifiMoveMember(&dest->m_bssid, &src->m_bssid);
}

void XNetworkWiFiConnection_init(XNetworkWiFiConnection* connection)
{
    if (!connection) return;
    memset(((XClass*)connection) + 1, 0,
           sizeof(XNetworkWiFiConnection) - sizeof(XClass));
    XClass_init((XClass*)connection);
    XClassGetVtable(connection) = XNetworkWiFiConnection_class_init();
    connection->m_ssid = NULL;
    connection->m_bssid = NULL;
    connection->m_signalStrength = -1;
    connection->m_security = XDeviceWiFi_SecurityUnknown;
    connection->m_state = XDeviceWiFi_ConnectIdle;
}

XNetworkWiFiConnection* XNetworkWiFiConnection_create_ex(XMemoryType memory)
{
    XNetworkWiFiConnection* connection =
        (XNetworkWiFiConnection*)XMemory_malloc(sizeof(XNetworkWiFiConnection),
                                                memory);
    if (!connection) return NULL;
    XNetworkWiFiConnection_init(connection);
    Set_Class_Memory(connection, memory);
    Set_Class_IsHeap(connection, true);
    return connection;
}

const XString* XNetworkWiFiConnection_ssid(const XNetworkWiFiConnection* connection)
{
    return connection ? connection->m_ssid : NULL;
}

const XString* XNetworkWiFiConnection_bssid(const XNetworkWiFiConnection* connection)
{
    return connection ? connection->m_bssid : NULL;
}

int XNetworkWiFiConnection_signalStrength(const XNetworkWiFiConnection* connection)
{
    return connection ? connection->m_signalStrength : -1;
}

XDeviceWiFiSecurity XNetworkWiFiConnection_security(
    const XNetworkWiFiConnection* connection)
{
    return connection ? connection->m_security : XDeviceWiFi_SecurityUnknown;
}

XDeviceWiFiConnectState XNetworkWiFiConnection_state(
    const XNetworkWiFiConnection* connection)
{
    return connection ? connection->m_state : XDeviceWiFi_ConnectIdle;
}

/* =========================================================================
 * 门面：XNetworkWiFi
 * ========================================================================= */

static void VXNetworkWiFi_deinit(XNetworkWiFi* wifi);

XVtable* XNetworkWiFi_class_init(void)
{
    XVTABLE_INIT_DEFAULT(XNetworkWiFi)
        XVTABLE_INHERIT_XCLASS(XClass);
        XVTABLE_OVERLOAD_DEFAULT(EXClass_Deinit, VXNetworkWiFi_deinit);
        XCLASS_SHOW_SIZE_DEFAULT(XNetworkWiFi);
        return XVTABLE_DEFAULT;
}

/**
 * @brief 会话析构：关闭无线设备（归还平台资源），随后由基类释放自身。
 * @param wifi 会话对象。
 */
static void VXNetworkWiFi_deinit(XNetworkWiFi* wifi)
{
    if (!wifi) return;
    XNetworkWiFi_close(wifi);
}

void XNetworkWiFi_init(XNetworkWiFi* wifi)
{
    if (!wifi) return;
    memset(((XClass*)wifi) + 1, 0, sizeof(XNetworkWiFi) - sizeof(XClass));
    XClass_init((XClass*)wifi);
    XClassGetVtable(wifi) = XNetworkWiFi_class_init();
    wifi->m_fd = XFD_INVALID;
    wifi->m_lastError = 0;
}

XNetworkWiFi* XNetworkWiFi_create_ex(XMemoryType memory)
{
    XNetworkWiFi* wifi =
        (XNetworkWiFi*)XMemory_malloc(sizeof(XNetworkWiFi), memory);
    if (!wifi) return NULL;
    XNetworkWiFi_init(wifi);
    Set_Class_Memory(wifi, memory);
    Set_Class_IsHeap(wifi, true);
    return wifi;
}

bool XNetworkWiFi_open(XNetworkWiFi* wifi, int* error)
{
    int err = 0;
    XFd fd;
    if (!wifi) {
        if (error) *error = (int)XDeviceError_InvalidArgument;
        return false;
    }
    if (wifi->m_fd != XFD_INVALID) return true;
    fd = XDeviceWiFi_open(NULL, &err);
    if (fd == XFD_INVALID) {
        wifi->m_lastError = err;
        if (error) *error = err;
        return false;
    }
    wifi->m_fd = fd;
    wifi->m_lastError = 0;
    if (error) *error = 0;
    return true;
}

void XNetworkWiFi_close(XNetworkWiFi* wifi)
{
    if (!wifi) return;
    if (wifi->m_fd != XFD_INVALID) {
        XDeviceWiFi_close(wifi->m_fd);
        wifi->m_fd = XFD_INVALID;
    }
}

bool XNetworkWiFi_isOpen(const XNetworkWiFi* wifi)
{
    return wifi && wifi->m_fd != XFD_INVALID;
}

int XNetworkWiFi_lastError(const XNetworkWiFi* wifi)
{
    if (!wifi) return (int)XDeviceError_InvalidArgument;
    if (wifi->m_lastError) return wifi->m_lastError;
    if (wifi->m_fd == XFD_INVALID) return 0;
    return XDeviceWiFi_lastError(wifi->m_fd);
}

uint32_t XNetworkWiFi_capabilities(const XNetworkWiFi* wifi)
{
    if (!wifi || wifi->m_fd == XFD_INVALID) return 0;
    return XDeviceWiFi_capabilities(wifi->m_fd);
}

/**
 * @brief 取适配器标识文本（借用指针）。
 * @param adapter 适配器对象。
 * @return 标识文本；不可用时返回 NULL。
 */
static const char* wifiAdapterIdText(const XNetworkWiFiAdapter* adapter)
{
    const XString* id;
    if (!adapter) return NULL;
    id = adapter->m_id;
    return id ? XString_toUtf8(id) : NULL;
}

XVector* XNetworkWiFi_adapters(XNetworkWiFi* wifi)
{
    XVector* raw = NULL;
    XVector* result;
    size_t count;
    size_t i;
    if (!wifi || wifi->m_fd == XFD_INVALID) return NULL;
    if (!XDeviceWiFi_enumAdapters(wifi->m_fd, &raw) || !raw) return NULL;
    result = XVector_create(sizeof(XNetworkWiFiAdapter));
    if (!result) {
        XDeviceWiFi_freeAdapters(raw);
        return NULL;
    }
    XContainerSetDataCopyMethod(result, XClass_copy_base);
    XContainerSetDataMoveMethod(result, XClass_move_base);
    XContainerSetDataDeinitMethod(result, XClass_deinit_base);
    count = XVector_size_base(raw);
    for (i = 0; i < count; ++i) {
        XDeviceWiFiAdapterInfo** slot =
            (XDeviceWiFiAdapterInfo**)XVector_at_base(raw, (int64_t)i);
        XNetworkWiFiAdapter value;
        if (!slot || !*slot) continue;
        XNetworkWiFiAdapter_init(&value);
        value.m_id = (*slot)->id ? XString_create_copy((*slot)->id) : NULL;
        value.m_name = (*slot)->name ? XString_create_copy((*slot)->name) : NULL;
        value.m_description = (*slot)->description
                                  ? XString_create_copy((*slot)->description)
                                  : NULL;
        value.m_ifIndex = (*slot)->ifIndex;
        value.m_capabilities = (*slot)->capabilities;
        value.m_mode = (*slot)->mode;
        XVector_push_back_move_1_base(result, &value);
    }
    XDeviceWiFi_freeAdapters(raw);
    return result;
}

bool XNetworkWiFi_startScan(XNetworkWiFi* wifi, const XNetworkWiFiAdapter* adapter)
{
    const char* id;
    if (!wifi || wifi->m_fd == XFD_INVALID) return false;
    id = wifiAdapterIdText(adapter);
    if (!id) return false;
    return XDeviceWiFi_startScan(wifi->m_fd, id);
}

XDeviceWiFiScanState XNetworkWiFi_scanState(XNetworkWiFi* wifi,
                                             const XNetworkWiFiAdapter* adapter)
{
    const char* id;
    if (!wifi || wifi->m_fd == XFD_INVALID) return XDeviceWiFi_ScanIdle;
    id = wifiAdapterIdText(adapter);
    if (!id) return XDeviceWiFi_ScanIdle;
    return XDeviceWiFi_scanState(wifi->m_fd, id);
}

XVector* XNetworkWiFi_takeScanResults(XNetworkWiFi* wifi,
                                      const XNetworkWiFiAdapter* adapter)
{
    const char* id;
    XVector* raw = NULL;
    XVector* result;
    size_t count;
    size_t i;
    if (!wifi || wifi->m_fd == XFD_INVALID) return NULL;
    id = wifiAdapterIdText(adapter);
    if (!id) return NULL;
    if (!XDeviceWiFi_takeScanResults(wifi->m_fd, id, &raw) || !raw) return NULL;
    result = XVector_create(sizeof(XNetworkWiFiNetwork));
    if (!result) {
        XDeviceWiFi_freeNetworks(raw);
        return NULL;
    }
    XContainerSetDataCopyMethod(result, XClass_copy_base);
    XContainerSetDataMoveMethod(result, XClass_move_base);
    XContainerSetDataDeinitMethod(result, XClass_deinit_base);
    count = XVector_size_base(raw);
    for (i = 0; i < count; ++i) {
        XDeviceWiFiNetworkInfo** slot =
            (XDeviceWiFiNetworkInfo**)XVector_at_base(raw, (int64_t)i);
        XNetworkWiFiNetwork value;
        if (!slot || !*slot) continue;
        XNetworkWiFiNetwork_init(&value);
        value.m_ssid = (*slot)->ssid ? XString_create_copy((*slot)->ssid) : NULL;
        value.m_bssid = (*slot)->bssid ? XString_create_copy((*slot)->bssid) : NULL;
        value.m_signalStrength = (*slot)->signalStrength;
        value.m_channel = (*slot)->channel;
        value.m_security = (*slot)->security;
        value.m_secured = (*slot)->secured;
        XVector_push_back_move_1_base(result, &value);
    }
    XDeviceWiFi_freeNetworks(raw);
    return result;
}

bool XNetworkWiFi_connect(XNetworkWiFi* wifi, const XNetworkWiFiAdapter* adapter,
                          const char* ssid, const char* passphrase,
                          XDeviceWiFiSecurity security)
{
    const char* id;
    if (!wifi || wifi->m_fd == XFD_INVALID) return false;
    id = wifiAdapterIdText(adapter);
    if (!id) return false;
    return XDeviceWiFi_startConnect(wifi->m_fd, id, ssid, passphrase, security);
}

XDeviceWiFiConnectState XNetworkWiFi_connectState(
    XNetworkWiFi* wifi, const XNetworkWiFiAdapter* adapter)
{
    const char* id;
    if (!wifi || wifi->m_fd == XFD_INVALID) return XDeviceWiFi_ConnectIdle;
    id = wifiAdapterIdText(adapter);
    if (!id) return XDeviceWiFi_ConnectIdle;
    return XDeviceWiFi_connectState(wifi->m_fd, id);
}

XNetworkWiFiConnection* XNetworkWiFi_queryConnection(
    XNetworkWiFi* wifi, const XNetworkWiFiAdapter* adapter)
{
    const char* id;
    XDeviceWiFiConnectionInfo info;
    XNetworkWiFiConnection* connection;
    if (!wifi || wifi->m_fd == XFD_INVALID) return NULL;
    id = wifiAdapterIdText(adapter);
    if (!id) return NULL;
    memset(&info, 0, sizeof(info));
    if (!XDeviceWiFi_queryConnection(wifi->m_fd, id, &info)) return NULL;
    connection = XNetworkWiFiConnection_create();
    if (!connection) {
        XDeviceWiFi_freeConnectionInfo(&info);
        return NULL;
    }
    connection->m_ssid = info.ssid ? XString_create_copy(info.ssid) : NULL;
    connection->m_bssid = info.bssid ? XString_create_copy(info.bssid) : NULL;
    connection->m_signalStrength = info.signalStrength;
    connection->m_security = info.security;
    connection->m_state = info.state;
    XDeviceWiFi_freeConnectionInfo(&info);
    return connection;
}

bool XNetworkWiFi_disconnect(XNetworkWiFi* wifi, const XNetworkWiFiAdapter* adapter)
{
    const char* id;
    if (!wifi || wifi->m_fd == XFD_INVALID) return false;
    id = wifiAdapterIdText(adapter);
    if (!id) return false;
    return XDeviceWiFi_disconnect(wifi->m_fd, id);
}

bool XNetworkWiFi_forgetProfile(XNetworkWiFi* wifi,
                                const XNetworkWiFiAdapter* adapter,
                                const char* ssid)
{
    const char* id;
    if (!wifi || wifi->m_fd == XFD_INVALID) return false;
    id = wifiAdapterIdText(adapter);
    if (!id) return false;
    return XDeviceWiFi_forgetProfile(wifi->m_fd, id, ssid);
}

#endif // XNETWORK_WIFI_ON
#endif /* XNETWORK_ON */
