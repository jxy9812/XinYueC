/* =============================================================================
 * hsf_bridge —— 桌面客户端**本机能力**（WiFi + 屏幕电源）的扁平 C ABI，
 * 给 Python ctypes 直接调。
 *
 * [为什么存在] 「WiFi 网络设置」与「窗口设置·屏幕休眠」两块界面只在本机
 * 生效：WiFi 要调平台无线 API（Windows = Native Wifi / Linux = nmcli），
 * 熄屏/唤醒要调平台电源 API（Win32 = SC_MONITORPOWER / Linux = DPMS）。
 * 仓库里这两块能力都已经在框架层落地：
 *   - 无线业务层：Src/XCore/XCode/XNetwork/XNetworkWiFi/XNetworkWiFi.h
 *   - 平台电源面：Src/XGui/Platform/XPlatformPower.h
 * 本文件把这两层的**对象式** API 折成一组**扁平** C 函数（ctypes 可直接
 * 调、无需 C++、不暴露结构体布局），并编成 hsb_bridge.dll / hsb_bridge.so。
 *
 * [ABI 约定]
 *   - 所有 char* 返回值都是 **新分配** 的 UTF-8 文本（malloc），调用方必须
 *     用 hsb_free() 释放；NULL 表示失败，失败原因看 hsb_last_error()。
 *   - int 返回值：1 = 成功，0 = 失败（唯一例外是 hsb_wifi_scan_state()，
 *     它返回状态码 0/1/2/3，见下）。
 *   - 错误文本存在**线程局部**缓冲里（hsb_last_error()），下一次出错会覆盖
 *     ——调用方要立刻取走。
 *
 * [JSON] 本文件**自己**拼 JSON（snprintf + 转义），不引框架的 XJsonDocument：
 * 桥接层只想要「扁平、无依赖、可被 ctypes 解析」的最小面，多引一层 JSON
 * 栈会把动态库的依赖面撑大。
 *
 * [编译] 目标 hsb_bridge 定义在 HighSpeedFilter/CMakeLists.txt（与
 * xlsx_bridge 同款：SHARED + PREFIX "" + DEBUG_POSTFIX ""），产出自动拷到
 * client_py/hsfsrv_client/bin/hsb_bridge.dll。
 *
 * [平台差异] 能力探测全走框架（XPlatformPower_capabilities /
 * XNetworkWiFi_open 的错误码），**平台不支持时如实返回失败/0，绝不崩**。
 * ============================================================================ */
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* [必须最先包含] XNetworkWiFi.h / XPlatformPower.h 的可见性都挂在
 * CXinYueConfig.h 定义的宏上（XNETWORK_ON / XDEVICE_WIFI_ON / XGUI_ON），
 * 头文件自己的 include 链在宏判断**之后**才建立 —— 直接先包含会被裁成空。 */
#include "CXinYueConfig.h"
#include "XMemory.h"

#include "XClass.h"
#include "XString.h"
#include "XVector.h"

#if XNETWORK_ON && XDEVICE_WIFI_ON
#include "XNetworkWiFi.h"
#endif

#if XGUI_ON
#include "XPlatformPower.h"
#endif

#ifdef _WIN32
#define HSB_API __declspec(dllexport)
#else
#define HSB_API
#endif

/* 线程局部存储：MSVC 用 __declspec(thread)，GCC/Clang 用 __thread。 */
#if defined(_MSC_VER)
#define HSB_TLS __declspec(thread)
#elif defined(__GNUC__) || defined(__clang__)
#define HSB_TLS __thread
#else
#define HSB_TLS
#endif

#define HSB_ERROR_CAP 512

/* ==================== 错误文本（线程局部） ==================== */

static HSB_TLS char g_hsb_error[HSB_ERROR_CAP];

/**
 * @brief 记录最近一次错误（线程局部；格式化后截断到缓冲容量）。
 */
static void hsb_setError(const char* fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    vsnprintf(g_hsb_error, sizeof(g_hsb_error), fmt, args);
    va_end(args);
}

/**
 * @brief 清空错误文本。
 */
static void hsb_clearError(void)
{
    g_hsb_error[0] = '\0';
}

/**
 * @brief 取最近一次错误文本（线程局部；UTF-8）。
 * @return 错误文本；无错误返回空串（不是 NULL，调用方不必判空）。
 */
HSB_API const char* hsb_last_error(void)
{
    return g_hsb_error;
}

/**
 * @brief 释放 hsb_* 返回的文本（malloc 出来的）。
 * @param p 指针；传 NULL 安全。
 */
HSB_API void hsb_free(char* p)
{
    if (p) {
        free(p);
    }
}

/**
 * @brief 复制一份新分配的空串结尾副本（NULL → NULL）。
 */
static char* hsb_strdup(const char* text)
{
    size_t len;
    char* out;
    if (!text) {
        return NULL;
    }
    len = strlen(text);
    out = (char*)malloc(len + 1);
    if (!out) {
        return NULL;
    }
    memcpy(out, text, len + 1);
    return out;
}

/* ==================== 动态字符串缓冲（拼 JSON 用） ==================== */

typedef struct HsbBuf {
    char*  m_data;
    size_t m_len;
    size_t m_cap;
} HsbBuf;

static void hsb_bufInit(HsbBuf* buf)
{
    buf->m_data = NULL;
    buf->m_len = 0;
    buf->m_cap = 0;
}

static void hsb_bufFree(HsbBuf* buf)
{
    if (buf->m_data) {
        free(buf->m_data);
    }
    hsb_bufInit(buf);
}

/**
 * @brief 预留容量（失败时缓冲保持原样，调用方判 m_data==NULL）。
 */
static bool hsb_bufReserve(HsbBuf* buf, size_t need)
{
    size_t cap;
    char* data;
    if (buf->m_cap - buf->m_len >= need + 1u) {
        return true;
    }
    cap = buf->m_cap ? buf->m_cap : 256u;
    while (cap - buf->m_len < need + 1u) {
        cap *= 2u;
    }
    data = (char*)realloc(buf->m_data, cap);
    if (!data) {
        return false;
    }
    buf->m_data = data;
    buf->m_cap = cap;
    return true;
}

/**
 * @brief 追加一段文本（自动扩容）。
 */
static bool hsb_bufAppend(HsbBuf* buf, const char* text)
{
    size_t len;
    if (!text) {
        return true;
    }
    len = strlen(text);
    if (!hsb_bufReserve(buf, len)) {
        return false;
    }
    memcpy(buf->m_data + buf->m_len, text, len);
    buf->m_len += len;
    buf->m_data[buf->m_len] = '\0';
    return true;
}

/**
 * @brief 追加格式化文本。
 */
static bool hsb_bufAppendFmt(HsbBuf* buf, const char* fmt, ...)
{
    char tmp[512];
    va_list args;
    va_start(args, fmt);
    vsnprintf(tmp, sizeof(tmp), fmt, args);
    va_end(args);
    return hsb_bufAppend(buf, tmp);
}

/**
 * @brief 追加一个 JSON 字符串（含引号；文本按 JSON 规则转义）。
 * @param text 原始 UTF-8 文本；NULL 当空串。
 */
static bool hsb_bufAppendJsonString(HsbBuf* buf, const char* text)
{
    const unsigned char* p;
    char esc[8];
    if (!hsb_bufAppend(buf, "\"")) {
        return false;
    }
    if (!text) {
        return hsb_bufAppend(buf, "\"");
    }
    for (p = (const unsigned char*)text; *p; ++p) {
        switch (*p) {
        case '"':  if (!hsb_bufAppend(buf, "\\\"")) return false; break;
        case '\\': if (!hsb_bufAppend(buf, "\\\\")) return false; break;
        case '\b': if (!hsb_bufAppend(buf, "\\b")) return false; break;
        case '\f': if (!hsb_bufAppend(buf, "\\f")) return false; break;
        case '\n': if (!hsb_bufAppend(buf, "\\n")) return false; break;
        case '\r': if (!hsb_bufAppend(buf, "\\r")) return false; break;
        case '\t': if (!hsb_bufAppend(buf, "\\t")) return false; break;
        default:
            if (*p < 0x20u) {
                snprintf(esc, sizeof(esc), "\\u%04x", (unsigned)*p);
                if (!hsb_bufAppend(buf, esc)) return false;
            } else {
                esc[0] = (char)*p;
                esc[1] = '\0';
                if (!hsb_bufAppend(buf, esc)) return false;
            }
            break;
        }
    }
    return hsb_bufAppend(buf, "\"");
}

/**
 * @brief 收尾：把缓冲交出去（malloc 副本；失败返回 NULL）。
 */
static char* hsb_bufRelease(HsbBuf* buf)
{
    char* out;
    if (!buf->m_data) {
        return NULL;
    }
    out = hsb_strdup(buf->m_data);
    hsb_bufFree(buf);
    return out;
}

/* ==================== 枚举值 → 可读文本 ==================== */

#if XNETWORK_ON && XDEVICE_WIFI_ON
/**
 * @brief 安全方式 → 文本（WPA2-PSK 之类；给界面下拉用）。
 */
static const char* hsb_securityName(XDeviceWiFiSecurity security)
{
    switch (security) {
    case XDeviceWiFi_SecurityOpen:          return "开放";
    case XDeviceWiFi_SecurityWep:           return "WEP";
    case XDeviceWiFi_SecurityWpaPsk:        return "WPA-PSK";
    case XDeviceWiFi_SecurityWpa2Psk:       return "WPA2-PSK";
    case XDeviceWiFi_SecurityWpa2Enterprise: return "WPA2-企业";
    case XDeviceWiFi_SecurityWpa3Sae:       return "WPA3-SAE";
    default:                                return "未知";
    }
}

/**
 * @brief 连接状态 → 文本。
 */
static const char* hsb_connectStateName(int state)
{
    switch (state) {
    case 1: return "connecting";
    case 2: return "connected";
    case 3: return "failed";
    default: return "idle";
    }
}
#endif /* XNETWORK_ON && XDEVICE_WIFI_ON */

/* ==================== WiFi 会话 ==================== */

#if XNETWORK_ON && XDEVICE_WIFI_ON

/** @brief 无线会话（对外就是一个不透明句柄）。 */
typedef struct HsbWifiSession {
    XNetworkWiFi* m_wifi;  /**< 框架层无线会话。 */
    XVector* m_adapters;   /**< **缓存**的适配器列表（借用给各调用点）。 */
    char** m_ssid;         /**< 最近一次扫描结果的 SSID 缓存（查安全方式）。 */
    int*  m_security;      /**< 与 m_ssid 一一对应的安全方式。 */
    int   m_count;         /**< 缓存条目数。 */
} HsbWifiSession;

/**
 * @brief 取适配器列表（**带缓存**）。
 * @param session 会话；@param refresh true=强制重新枚举。
 * @return 借用指针（列表归会话所有，调用方**不得**释放）；失败返回 NULL。
 *
 * @details [为什么必须缓存] 平台的适配器枚举会**重建**平台侧的适配器槽
 *          （Windows 驱动：wifiRefreshAdapters → wifiClearSlots → 每个
 *          槽的 scanState 复位成 Idle，Drive/windows/Device/Network/
 *          XDeviceWiFi_win32.c:259）。若每次轮询都重新枚举，**轮询本身
 *          就把扫描状态抹了**：startScan 置 Running，紧接着的 scanState
 *          查询因重新枚举而看到 Idle ⇒ 扫描永远收不到 Completed ⇒ 界面
 *          只能等超时。缓存一份列表复用，扫描状态才活得到取结果那一刻。
 */
static XVector* hsb_wifiAdapters(HsbWifiSession* session, bool refresh)
{
    XVector* adapters;
    if (!session || !session->m_wifi) {
        return NULL;
    }
    if (session->m_adapters && !refresh) {
        return session->m_adapters;
    }
    adapters = XNetworkWiFi_adapters(session->m_wifi);
    if (!adapters) {
        return NULL;
    }
    if (session->m_adapters) {
        XClassDelete((XClass*)session->m_adapters);
    }
    session->m_adapters = adapters;
    return adapters;
}

/**
 * @brief 清空安全方式缓存。
 */
static void hsb_wifiClearCache(HsbWifiSession* session)
{
    int i;
    if (!session) {
        return;
    }
    for (i = 0; i < session->m_count; ++i) {
        if (session->m_ssid && session->m_ssid[i]) {
            free(session->m_ssid[i]);
        }
    }
    if (session->m_ssid) {
        free(session->m_ssid);
    }
    if (session->m_security) {
        free(session->m_security);
    }
    session->m_ssid = NULL;
    session->m_security = NULL;
    session->m_count = 0;
}

/**
 * @brief 写入安全方式缓存（SSID → 安全方式）。
 * @details [所有权] 缓存里的 SSID 文本由本缓存**独占**：本函数把旧数组里
 *          的字符串指针**转移**到新数组（只释放旧数组的壳，不碰里面的
 *          字符串），旧的整表释放交给 hsb_wifiClearCache。
 *
 *          [旧实现的坑（2026-11-13 实锅）] 旧写法是"拷指针 → 再调
 *          hsb_wifiClearCache 释放旧字符串" ⇒ 新数组里留着**已释放**的
 *          指针（use-after-free），第二次插入还会对同一个字符串二次
 *          free（double-free）⇒ 堆破坏 ⇒ 扫描结果一取就卡死/崩。
 *          同时 m_count 被 clearCache 清零后再 +1，恒为 1 ⇒ 缓存里
 *          只记得第一个网络的安全方式（连接时加密方式判错）。
 */
static void hsb_wifiCacheSecurity(HsbWifiSession* session, const char* ssid,
                                  XDeviceWiFiSecurity security)
{
    char** ssids;
    int* securities;
    int count;
    int i;
    if (!session || !ssid || !ssid[0]) {
        return;
    }
    count = session->m_count;
    ssids = (char**)calloc((size_t)count + 1u, sizeof(char*));
    securities = (int*)calloc((size_t)count + 1u, sizeof(int));
    if (!ssids || !securities) {
        if (ssids) free(ssids);
        if (securities) free(securities);
        return;
    }
    /* 转移所有权：旧字符串指针搬进新数组（旧数组壳随后单独释放）。 */
    for (i = 0; i < count; ++i) {
        ssids[i] = session->m_ssid ? session->m_ssid[i] : NULL;
        securities[i] = session->m_security ? session->m_security[i] : 0;
    }
    ssids[count] = hsb_strdup(ssid);
    securities[count] = (int)security;
    if (session->m_ssid) {
        free(session->m_ssid);
    }
    if (session->m_security) {
        free(session->m_security);
    }
    session->m_ssid = ssids;
    session->m_security = securities;
    session->m_count = count + 1;
}

/**
 * @brief 查 SSID 的安全方式（未命中 → Unknown）。
 */
static XDeviceWiFiSecurity hsb_wifiLookupSecurity(const HsbWifiSession* session,
                                                   const char* ssid)
{
    int i;
    if (!session || !ssid) {
        return XDeviceWiFi_SecurityUnknown;
    }
    for (i = 0; i < session->m_count; ++i) {
        if (session->m_ssid[i] && strcmp(session->m_ssid[i], ssid) == 0) {
            return (XDeviceWiFiSecurity)session->m_security[i];
        }
    }
    return XDeviceWiFi_SecurityUnknown;
}

/**
 * @brief 取适配器 id 文本（借用）。
 */
static const char* hsb_adapterId(const XNetworkWiFiAdapter* adapter)
{
    const XString* id;
    if (!adapter) {
        return NULL;
    }
    id = XNetworkWiFiAdapter_id(adapter);
    return id ? XString_toUtf8(id) : NULL;
}

/**
 * @brief 在适配器列表里按 id 找适配器（借用；列表由调用方负责释放）。
 */
static const XNetworkWiFiAdapter* hsb_findAdapter(XVector* adapters,
                                                  const char* adapterId)
{
    size_t count;
    size_t i;
    if (!adapters || !adapterId) {
        return NULL;
    }
    /* [显式转型] XVector_size_base 是 XContainer_size_base 的别名，形参是
     * const XContainer*；C 语言没有继承，必须显式转型。布局安全：XVector 的
     * 第一个成员就是 XContainer m_class（XVector.h:45）。 */
    count = XVector_size_base((const XContainer*)adapters);
    for (i = 0; i < count; ++i) {
        const XNetworkWiFiAdapter* adapter =
            (const XNetworkWiFiAdapter*)XVector_at_base(adapters, (int64_t)i);
        const char* id = hsb_adapterId(adapter);
        if (id && strcmp(id, adapterId) == 0) {
            return adapter;
        }
    }
    return NULL;
}

/**
 * @brief 打开无线会话。
 * @param out 会话句柄输出（不可为 NULL）；失败时置 NULL。
 * @return 1=成功，0=失败（原因见 hsb_last_error）。
 */
HSB_API int hsb_wifi_open(void** out)
{
    HsbWifiSession* session;
    int error = 0;
    if (!out) {
        hsb_setError("参数非法：out 为空");
        return 0;
    }
    *out = NULL;
    hsb_clearError();
    session = (HsbWifiSession*)calloc(1u, sizeof(HsbWifiSession));
    if (!session) {
        hsb_setError("内存不足");
        return 0;
    }
    session->m_wifi = XNetworkWiFi_create();
    if (!session->m_wifi) {
        free(session);
        hsb_setError("创建无线会话失败");
        return 0;
    }
    if (!XNetworkWiFi_open(session->m_wifi, &error)) {
        hsb_setError("无线会话打开失败（错误码 %d）：本平台可能不支持无线",
                     error);
        XClassDelete((XClass*)session->m_wifi);
        free(session);
        return 0;
    }
    *out = session;
    return 1;
}

/**
 * @brief 关闭无线会话（释放缓存与会话本体）。
 * @param h 会话句柄；传 NULL 安全。
 */
HSB_API void hsb_wifi_close(void* h)
{
    HsbWifiSession* session = (HsbWifiSession*)h;
    if (!session) {
        return;
    }
    hsb_wifiClearCache(session);
    if (session->m_adapters) {
        XClassDelete((XClass*)session->m_adapters);
        session->m_adapters = NULL;
    }
    if (session->m_wifi) {
        XNetworkWiFi_close(session->m_wifi);
        XClassDelete((XClass*)session->m_wifi);
        session->m_wifi = NULL;
    }
    free(session);
}

/**
 * @brief 枚举无线适配器。
 * @param h 会话句柄。
 * @return 新分配的 JSON 数组文本（调用方用 hsb_free 释放）；失败返回 NULL。
 */
/**
 * @brief 把适配器列表拼成 JSON 数组文本。
 * @param adapters 适配器列表（借用）。
 * @return 新分配的 JSON 文本（调用方用 hsb_free 释放）；失败返回 NULL。
 */
static char* hsb_wifiAdaptersJson(XVector* adapters)
{
    HsbBuf buf;
    size_t count;
    size_t i;
    hsb_bufInit(&buf);
    if (!hsb_bufAppend(&buf, "[")) {
        hsb_bufFree(&buf);
        hsb_setError("内存不足");
        return NULL;
    }
    count = XVector_size_base((const XContainer*)adapters);
    for (i = 0; i < count; ++i) {
        const XNetworkWiFiAdapter* adapter =
            (const XNetworkWiFiAdapter*)XVector_at_base(adapters, (int64_t)i);
        const XString* name;
        const XString* desc;
        if (i > 0) {
            hsb_bufAppend(&buf, ",");
        }
        hsb_bufAppend(&buf, "{\"id\":");
        hsb_bufAppendJsonString(&buf, hsb_adapterId(adapter));
        hsb_bufAppend(&buf, ",\"name\":");
        name = XNetworkWiFiAdapter_name(adapter);
        hsb_bufAppendJsonString(&buf, name ? XString_toUtf8(name) : NULL);
        hsb_bufAppend(&buf, ",\"desc\":");
        desc = XNetworkWiFiAdapter_description(adapter);
        hsb_bufAppendJsonString(&buf, desc ? XString_toUtf8(desc) : NULL);
        hsb_bufAppendFmt(&buf, ",\"index\":%u,\"mode\":%d}",
                         (unsigned)XNetworkWiFiAdapter_ifIndex(adapter),
                         (int)XNetworkWiFiAdapter_mode(adapter));
    }
    hsb_bufAppend(&buf, "]");
    {
        char* out = hsb_bufRelease(&buf);
        if (!out) {
            hsb_setError("内存不足");
        }
        return out;
    }
}

HSB_API char* hsb_wifi_adapters(void* h)
{
    HsbWifiSession* session = (HsbWifiSession*)h;
    XVector* adapters;
    if (!session || !session->m_wifi) {
        hsb_setError("无线会话未打开");
        return NULL;
    }
    hsb_clearError();
    adapters = hsb_wifiAdapters(session, false);
    if (!adapters) {
        hsb_setError("枚举无线适配器失败（本平台可能不支持）");
        return NULL;
    }
    return hsb_wifiAdaptersJson(adapters);
}

/**
 * @brief 强制重新枚举适配器（丢弃缓存）。
 * @param h 会话句柄。
 * @return 新分配的 JSON 数组文本（调用方用 hsb_free 释放）；失败返回 NULL。
 *
 * @details [用途] 界面「刷新适配器」入口。**不要**在扫描轮询里调用它
 *          —— 平台侧枚举会复位扫描状态（见 hsb_wifiAdapters 的说明）。
 */
HSB_API char* hsb_wifi_refresh_adapters(void* h)
{
    HsbWifiSession* session = (HsbWifiSession*)h;
    XVector* adapters;
    if (!session || !session->m_wifi) {
        hsb_setError("无线会话未打开");
        return NULL;
    }
    hsb_clearError();
    adapters = hsb_wifiAdapters(session, true);
    if (!adapters) {
        hsb_setError("枚举无线适配器失败（本平台可能不支持）");
        return NULL;
    }
    return hsb_wifiAdaptersJson(adapters);
}

/**
 * @brief 发起一次扫描（发起即返回，结果靠轮询取）。
 * @param h 会话句柄；@param adapter_id 适配器 id。
 * @return 1=已发起，0=失败。
 */
HSB_API int hsb_wifi_start_scan(void* h, const char* adapter_id)
{
    HsbWifiSession* session = (HsbWifiSession*)h;
    XVector* adapters;
    const XNetworkWiFiAdapter* adapter;
    bool ok;
    if (!session || !session->m_wifi || !adapter_id) {
        hsb_setError("参数非法：无线会话未打开或适配器 id 为空");
        return 0;
    }
    hsb_clearError();
    adapters = hsb_wifiAdapters(session, false);
    if (!adapters) {
        hsb_setError("枚举无线适配器失败");
        return 0;
    }
    adapter = hsb_findAdapter(adapters, adapter_id);
    if (!adapter) {
        hsb_setError("找不到适配器：%s", adapter_id);
        return 0;
    }
    ok = XNetworkWiFi_startScan(session->m_wifi, adapter);
    if (!ok) {
        hsb_setError("扫描发起失败");
        return 0;
    }
    return 1;
}

/**
 * @brief 轮询扫描状态。
 * @param h 会话句柄；@param adapter_id 适配器 id。
 * @return 0=空闲 1=扫描中 2=完成 3=失败；参数非法时返回 0。
 */
HSB_API int hsb_wifi_scan_state(void* h, const char* adapter_id)
{
    HsbWifiSession* session = (HsbWifiSession*)h;
    XVector* adapters;
    const XNetworkWiFiAdapter* adapter;
    XDeviceWiFiScanState state;
    if (!session || !session->m_wifi || !adapter_id) {
        hsb_setError("参数非法：无线会话未打开或适配器 id 为空");
        return 0;
    }
    hsb_clearError();
    adapters = hsb_wifiAdapters(session, false);
    if (!adapters) {
        hsb_setError("枚举无线适配器失败");
        return 0;
    }
    adapter = hsb_findAdapter(adapters, adapter_id);
    if (!adapter) {
        hsb_setError("找不到适配器：%s", adapter_id);
        return 0;
    }
    state = XNetworkWiFi_scanState(session->m_wifi, adapter);
    return (int)state;
}

/**
 * @brief 取走扫描结果（取走后扫描状态复位为空闲）。
 * @param h 会话句柄；@param adapter_id 适配器 id。
 * @return 新分配的 JSON 数组文本；失败返回 NULL。
 */
HSB_API char* hsb_wifi_take_scan_results(void* h, const char* adapter_id)
{
    HsbWifiSession* session = (HsbWifiSession*)h;
    XVector* adapters;
    XVector* networks;
    const XNetworkWiFiAdapter* adapter;
    HsbBuf buf;
    size_t count;
    size_t i;
    if (!session || !session->m_wifi || !adapter_id) {
        hsb_setError("参数非法：无线会话未打开或适配器 id 为空");
        return NULL;
    }
    hsb_clearError();
    adapters = hsb_wifiAdapters(session, false);
    if (!adapters) {
        hsb_setError("枚举无线适配器失败");
        return NULL;
    }
    adapter = hsb_findAdapter(adapters, adapter_id);
    if (!adapter) {
        hsb_setError("找不到适配器：%s", adapter_id);
        return NULL;
    }
    networks = XNetworkWiFi_takeScanResults(session->m_wifi, adapter);
    if (!networks) {
        hsb_setError("扫描未完成或没有结果");
        return NULL;
    }
    hsb_bufInit(&buf);
    hsb_bufAppend(&buf, "[");
    count = XVector_size_base((const XContainer*)networks);
    for (i = 0; i < count; ++i) {
        const XNetworkWiFiNetwork* network =
            (const XNetworkWiFiNetwork*)XVector_at_base(networks, (int64_t)i);
        const XString* ssid = XNetworkWiFiNetwork_ssid(network);
        const XString* bssid = XNetworkWiFiNetwork_bssid(network);
        const char* ssidText = ssid ? XString_toUtf8(ssid) : NULL;
        XDeviceWiFiSecurity security = XNetworkWiFiNetwork_security(network);
        bool needPass = XNetworkWiFiNetwork_isSecured(network);
        if (i > 0) {
            hsb_bufAppend(&buf, ",");
        }
        hsb_bufAppend(&buf, "{\"ssid\":");
        hsb_bufAppendJsonString(&buf, ssidText);
        hsb_bufAppend(&buf, ",\"mac\":");
        hsb_bufAppendJsonString(&buf, bssid ? XString_toUtf8(bssid) : NULL);
        hsb_bufAppendFmt(&buf, ",\"signal\":%d,\"channel\":%d",
                         XNetworkWiFiNetwork_signalStrength(network),
                         XNetworkWiFiNetwork_channel(network));
        hsb_bufAppend(&buf, ",\"secure\":");
        hsb_bufAppendJsonString(&buf, hsb_securityName(security));
        hsb_bufAppendFmt(&buf, ",\"needPass\":%s,\"securityCode\":%d}",
                         needPass ? "true" : "false", (int)security);
        if (ssidText) {
            hsb_wifiCacheSecurity(session, ssidText, security);
        }
    }
    hsb_bufAppend(&buf, "]");
    XClassDelete((XClass*)networks);
    {
        char* out = hsb_bufRelease(&buf);
        if (!out) {
            hsb_setError("内存不足");
        }
        return out;
    }
}

/**
 * @brief 发起一次连接（发起即返回；结果靠轮询连接状态收口）。
 * @param h 会话句柄；@param adapter_id 适配器 id；@param ssid 网络名；
 *        @param pass 口令（开放网络可为 NULL/空）。
 * @return 1=已受理，0=失败。
 */
HSB_API int hsb_wifi_connect(void* h, const char* adapter_id, const char* ssid,
                             const char* pass)
{
    HsbWifiSession* session = (HsbWifiSession*)h;
    XVector* adapters;
    const XNetworkWiFiAdapter* adapter;
    XDeviceWiFiSecurity security;
    bool ok;
    if (!session || !session->m_wifi || !adapter_id || !ssid || !ssid[0]) {
        hsb_setError("参数非法：无线会话未打开或适配器 id / 网络名为空");
        return 0;
    }
    hsb_clearError();
    adapters = hsb_wifiAdapters(session, false);
    if (!adapters) {
        hsb_setError("枚举无线适配器失败");
        return 0;
    }
    adapter = hsb_findAdapter(adapters, adapter_id);
    if (!adapter) {
        hsb_setError("找不到适配器：%s", adapter_id);
        return 0;
    }
    /* 安全方式：优先取扫描缓存里该 SSID 的实际加密方式；缓存未命中时
     * 按「有口令 ⇒ WPA2-PSK、无口令 ⇒ 开放」推断（Windows 侧据此写
     * profile XML，填错会导致连接必失败）。 */
    security = hsb_wifiLookupSecurity(session, ssid);
    if (security == XDeviceWiFi_SecurityUnknown) {
        security = (pass && pass[0]) ? XDeviceWiFi_SecurityWpa2Psk
                                     : XDeviceWiFi_SecurityOpen;
    }
    ok = XNetworkWiFi_connect(session->m_wifi, adapter, ssid, pass, security);
    if (!ok) {
        if (security != XDeviceWiFi_SecurityOpen && (!pass || !pass[0])) {
            hsb_setError("连接发起失败：加密网络需要口令");
        } else {
            hsb_setError("连接发起失败");
        }
        return 0;
    }
    return 1;
}

/**
 * @brief 断开当前连接。
 * @param h 会话句柄；@param adapter_id 适配器 id。
 * @return 1=平台已受理，0=失败。
 */
HSB_API int hsb_wifi_disconnect(void* h, const char* adapter_id)
{
    HsbWifiSession* session = (HsbWifiSession*)h;
    XVector* adapters;
    const XNetworkWiFiAdapter* adapter;
    bool ok;
    if (!session || !session->m_wifi || !adapter_id) {
        hsb_setError("参数非法：无线会话未打开或适配器 id 为空");
        return 0;
    }
    hsb_clearError();
    adapters = hsb_wifiAdapters(session, false);
    if (!adapters) {
        hsb_setError("枚举无线适配器失败");
        return 0;
    }
    adapter = hsb_findAdapter(adapters, adapter_id);
    if (!adapter) {
        hsb_setError("找不到适配器：%s", adapter_id);
        return 0;
    }
    ok = XNetworkWiFi_disconnect(session->m_wifi, adapter);
    if (!ok) {
        hsb_setError("断开失败");
        return 0;
    }
    return 1;
}

/**
 * @brief 查询当前连接快照。
 * @param h 会话句柄；@param adapter_id 适配器 id。
 * @return 新分配的 JSON 对象文本；失败返回 NULL。
 */
HSB_API char* hsb_wifi_connection(void* h, const char* adapter_id)
{
    HsbWifiSession* session = (HsbWifiSession*)h;
    XVector* adapters;
    const XNetworkWiFiAdapter* adapter;
    XNetworkWiFiConnection* connection;
    const XString* ssid;
    const XString* bssid;
    XDeviceWiFiConnectState state;
    HsbBuf buf;
    char* out;
    if (!session || !session->m_wifi || !adapter_id) {
        hsb_setError("参数非法：无线会话未打开或适配器 id 为空");
        return NULL;
    }
    hsb_clearError();
    adapters = hsb_wifiAdapters(session, false);
    if (!adapters) {
        hsb_setError("枚举无线适配器失败");
        return NULL;
    }
    adapter = hsb_findAdapter(adapters, adapter_id);
    if (!adapter) {
        hsb_setError("找不到适配器：%s", adapter_id);
        return NULL;
    }
    connection = XNetworkWiFi_queryConnection(session->m_wifi, adapter);
    if (!connection) {
        /* 未连接（或平台读不回）——这不是错误：给出「未连接」对象，让
         * 界面有确定的终态可显示，而不是把「没连」当成「出错了」。 */
        hsb_bufInit(&buf);
        hsb_bufAppend(&buf, "{\"ssid\":null,\"mac\":null,\"signal\":-1,"
                            "\"secure\":\"\",\"state\":\"idle\",\"stateCode\":0}");
        out = hsb_bufRelease(&buf);
        if (!out) {
            hsb_setError("内存不足");
        }
        return out;
    }
    ssid = XNetworkWiFiConnection_ssid(connection);
    bssid = XNetworkWiFiConnection_bssid(connection);
    state = XNetworkWiFiConnection_state(connection);
    hsb_bufInit(&buf);
    hsb_bufAppend(&buf, "{\"ssid\":");
    hsb_bufAppendJsonString(&buf, ssid ? XString_toUtf8(ssid) : NULL);
    hsb_bufAppend(&buf, ",\"mac\":");
    hsb_bufAppendJsonString(&buf, bssid ? XString_toUtf8(bssid) : NULL);
    hsb_bufAppendFmt(&buf, ",\"signal\":%d",
                     XNetworkWiFiConnection_signalStrength(connection));
    hsb_bufAppend(&buf, ",\"secure\":");
    hsb_bufAppendJsonString(&buf,
                            hsb_securityName(XNetworkWiFiConnection_security(connection)));
    hsb_bufAppendFmt(&buf, ",\"state\":\"%s\",\"stateCode\":%d}",
                     hsb_connectStateName((int)state), (int)state);
    XClassDelete((XClass*)connection);
    out = hsb_bufRelease(&buf);
    if (!out) {
        hsb_setError("内存不足");
    }
    return out;
}

#else /* !(XNETWORK_ON && XDEVICE_WIFI_ON) */

/* 无线能力未编译进来（嵌入式裁剪构建）：接口仍在，返回失败而不是崩。 */

HSB_API int hsb_wifi_open(void** out)
{
    if (out) {
        *out = NULL;
    }
    hsb_setError("本构建未包含无线能力（XDEVICE_WIFI_ON=0）");
    return 0;
}

HSB_API void hsb_wifi_close(void* h)
{
    (void)h;
}

HSB_API char* hsb_wifi_adapters(void* h)
{
    (void)h;
    hsb_setError("本构建未包含无线能力（XDEVICE_WIFI_ON=0）");
    return NULL;
}

HSB_API char* hsb_wifi_refresh_adapters(void* h)
{
    (void)h;
    hsb_setError("本构建未包含无线能力（XDEVICE_WIFI_ON=0）");
    return NULL;
}

HSB_API int hsb_wifi_start_scan(void* h, const char* adapter_id)
{
    (void)h; (void)adapter_id;
    hsb_setError("本构建未包含无线能力（XDEVICE_WIFI_ON=0）");
    return 0;
}

HSB_API int hsb_wifi_scan_state(void* h, const char* adapter_id)
{
    (void)h; (void)adapter_id;
    hsb_setError("本构建未包含无线能力（XDEVICE_WIFI_ON=0）");
    return 0;
}

HSB_API char* hsb_wifi_take_scan_results(void* h, const char* adapter_id)
{
    (void)h; (void)adapter_id;
    hsb_setError("本构建未包含无线能力（XDEVICE_WIFI_ON=0）");
    return NULL;
}

HSB_API int hsb_wifi_connect(void* h, const char* adapter_id, const char* ssid,
                             const char* pass)
{
    (void)h; (void)adapter_id; (void)ssid; (void)pass;
    hsb_setError("本构建未包含无线能力（XDEVICE_WIFI_ON=0）");
    return 0;
}

HSB_API int hsb_wifi_disconnect(void* h, const char* adapter_id)
{
    (void)h; (void)adapter_id;
    hsb_setError("本构建未包含无线能力（XDEVICE_WIFI_ON=0）");
    return 0;
}

HSB_API char* hsb_wifi_connection(void* h, const char* adapter_id)
{
    (void)h; (void)adapter_id;
    hsb_setError("本构建未包含无线能力（XDEVICE_WIFI_ON=0）");
    return NULL;
}

#endif /* XNETWORK_ON && XDEVICE_WIFI_ON */

/* ==================== 屏幕电源 ==================== */

#if XGUI_ON

/**
 * @brief 本平台支持的电源能力位（位掩码）。
 * @return 0=不具备任何电源管理能力。
 */
HSB_API unsigned hsb_power_capabilities(void)
{
    hsb_clearError();
    return (unsigned)XPlatformPower_capabilities();
}

/**
 * @brief 当前屏幕是否处于熄屏状态。
 * @return 1=已熄屏，0=未熄屏或读不到。
 */
HSB_API int hsb_power_is_screen_off(void)
{
    hsb_clearError();
    return XPlatformPower_isScreenOff() ? 1 : 0;
}

/**
 * @brief 立即熄屏 / 唤醒。
 * @param off 1=熄屏，0=点亮。
 * @return 1=成功，0=失败（原因见 hsb_last_error）。
 */
HSB_API int hsb_power_set_screen_off(int off)
{
    bool ok;
    hsb_clearError();
    ok = XPlatformPower_setScreenState(off ? XPlatformPowerScreenState_Off
                                           : XPlatformPowerScreenState_On);
    if (!ok) {
        hsb_setError("屏幕开关失败：本平台不支持主动熄屏/唤醒");
        return 0;
    }
    return 1;
}

/**
 * @brief 读回当前电源策略（JSON）。
 * @param buf 输出缓冲；@param len 缓冲字节数。
 * @return 1=成功（JSON 已写入、以 NUL 结尾）；0=失败（缓冲不足时错误
 *         文本里给出所需字节数）。
 */
HSB_API int hsb_power_get_policy(char* buf, int len)
{
    XPlatformPowerPolicy policy;
    char text[256];
    int need;
    if (!buf || len <= 0) {
        hsb_setError("参数非法：缓冲为空或长度不足");
        return 0;
    }
    hsb_clearError();
    memset(&policy, 0, sizeof(policy));
    XPlatformPower_policy(&policy);
    snprintf(text, sizeof(text),
             "{\"screenOffMs\":%lld,\"systemSuspendMs\":%lld,"
             "\"wakeInputs\":%u}",
             (long long)policy.m_screenOffMs,
             (long long)policy.m_systemSuspendMs,
             (unsigned)policy.m_wakeInputs);
    need = (int)strlen(text) + 1;
    if (need > len) {
        hsb_setError("策略缓冲不足：需要 %d 字节，实际 %d 字节", need, len);
        return 0;
    }
    memcpy(buf, text, (size_t)need);
    return 1;
}

/**
 * @brief 设置电源策略（一次性提交两个阈值 + 唤醒来源）。
 * @param screen_off_ms 熄屏阈值（毫秒；0=从不）；@param system_suspend_ms
 *        系统挂起阈值（毫秒；0=从不）；@param wake_inputs 唤醒来源位集
 *        （1=鼠标 2=键盘 4=触摸；0=按缺省「全部」处理）。
 * @return 1=已生效，0=失败。
 */
HSB_API int hsb_power_set_policy(long long screen_off_ms,
                                 long long system_suspend_ms,
                                 unsigned wake_inputs)
{
    XPlatformPowerPolicy policy;
    bool ok;
    hsb_clearError();
    memset(&policy, 0, sizeof(policy));
    policy.m_screenOffMs = (int64_t)(screen_off_ms < 0 ? 0 : screen_off_ms);
    policy.m_systemSuspendMs =
        (int64_t)(system_suspend_ms < 0 ? 0 : system_suspend_ms);
    policy.m_wakeInputs = wake_inputs;
    ok = XPlatformPower_setPolicy(&policy);
    if (!ok) {
        hsb_setError("电源策略设置失败：本平台不支持委托计时或设置被拒绝");
        return 0;
    }
    return 1;
}

/**
 * @brief 请求「设置自动熄屏时长」所需的平台授权（安卓引导到系统设置）。
 * @return 1=已发起授权流程，0=无需授权或发起失败。
 */
HSB_API int hsb_power_request_permission(void)
{
    bool ok;
    hsb_clearError();
    ok = XPlatformPower_requestScreenTimeoutPermission();
    if (!ok) {
        hsb_setError("本平台无需授权（自行计时），或授权发起失败");
        return 0;
    }
    return 1;
}

/**
 * @brief 推进一次电源巡检（空闲超时 → 熄屏 / 挂起）。
 * @details 框架把「空闲多久做什么」的判定放在 XPlatformPower_tick()，
 *          由**应用层的周期定时器**调用（C 端是 XGuiApplication 的 1Hz
 *          定时器，Src/XGui/Application/XGuiApplication.c:120）。桌面
 *          Python 客户端不跑 XGuiApplication ⇒ 若没人调它，「自动熄屏
 *          时长」就是**选了不兑现**。本函数把该原语原样暴露出去，由
 *          Python 端的 1Hz 定时器调用（判定逻辑仍在框架内，不重搓）。
 * @return 1=已推进，0=失败（本构建无电源能力）。
 */
HSB_API int hsb_power_tick(void)
{
    hsb_clearError();
    XPlatformPower_tick();
    return 1;
}

/**
 * @brief 上报一次用户活动（重置空闲计时；熄屏态下按唤醒来源决定是否唤醒）。
 * @param input_mask 输入来源位集（1=鼠标 2=键盘 4=触摸，可按位或）。
 * @return 1=已上报，0=失败（本构建无电源能力）。
 */
HSB_API int hsb_power_note_activity(unsigned input_mask)
{
    hsb_clearError();
    XPlatformPower_noteActivity(input_mask);
    return 1;
}

#else /* !XGUI_ON */

/* 电源能力未编译进来（嵌入式裁剪构建）：接口仍在，返回失败而不是崩。 */

HSB_API unsigned hsb_power_capabilities(void)
{
    return 0u;
}

HSB_API int hsb_power_is_screen_off(void)
{
    hsb_setError("本构建未包含电源能力（XGUI_ON=0）");
    return 0;
}

HSB_API int hsb_power_set_screen_off(int off)
{
    (void)off;
    hsb_setError("本构建未包含电源能力（XGUI_ON=0）");
    return 0;
}

HSB_API int hsb_power_get_policy(char* buf, int len)
{
    (void)buf; (void)len;
    hsb_setError("本构建未包含电源能力（XGUI_ON=0）");
    return 0;
}

HSB_API int hsb_power_set_policy(long long screen_off_ms,
                                 long long system_suspend_ms,
                                 unsigned wake_inputs)
{
    (void)screen_off_ms; (void)system_suspend_ms; (void)wake_inputs;
    hsb_setError("本构建未包含电源能力（XGUI_ON=0）");
    return 0;
}

HSB_API int hsb_power_request_permission(void)
{
    hsb_setError("本构建未包含电源能力（XGUI_ON=0）");
    return 0;
}

HSB_API int hsb_power_tick(void)
{
    hsb_setError("本构建未包含电源能力（XGUI_ON=0）");
    return 0;
}

HSB_API int hsb_power_note_activity(unsigned input_mask)
{
    (void)input_mask;
    hsb_setError("本构建未包含电源能力（XGUI_ON=0）");
    return 0;
}

#endif /* XGUI_ON */

/* ============================ 二维码（框架 XRcode） ============================
 *
 * [为什么在这里] Python 客户端「Web 设置」页要把网页访问地址画成二维码
 * （2026-11-16 用户口径：本地局域网地址 / 远程地址各一个，可切换）。
 * 仓库零点原则是「框架有的绝不手搓」——二维码编码在框架层已有完整实现
 * （Src/XCore/XCode/XRcode，含 L/M/H 三级纠错与不等长 RS 分块），且
 * hsb_bridge 已链接 XCore 门面静态库（XRCODE_ON 缺省 1）⇒ 本文件只做
 * 「折成扁平 C 函数」这一层，Python 侧 ctypes 直接取模块矩阵。
 *
 * [ABI]
 *   · hsb_qr_encode(text, level) → 矩阵边长（0 = 失败，原因看 hsb_last_error）
 *   · hsb_qr_size()              → 最近一次成功编码的边长（0 = 还没编过）
 *   · hsb_qr_matrix()            → 指向内部模块缓冲（每字节 0/1，**行优先**）
 *   · hsb_qr_destroy()           → 释放编码器（进程退出前可不调）
 *   矩阵指针在**下一次 encode 或 destroy 之前**有效 ⇒ 调用方（ctypes）
 *   要立刻拷走（Python 侧一次 `ctypes.string_at(ptr, n)` 取整块）。
 *   · level：0=L / 1=M / 3=H（2=Q 框架未内置，传入即失败——见 XRcode.h）。
 *
 * [线程] 编码器是**模块级单例**：绘图在 Qt UI 线程、切页/轮询也只在 UI
 * 线程 ⇒ 不引入锁（与 XRcode 内部状态一致；若将来多线程用需自行加锁）。
 */
#if XRCODE_ON
#include "XByteArray.h"
#include "XRcode.h"

static XRcode* g_hsb_qr = NULL;

HSB_API int hsb_qr_encode(const char* utf8, int level)
{
    XByteArray* data;
    int size;

    if (!utf8 || !utf8[0]) {
        hsb_setError("二维码内容为空");
        return 0;
    }
    if (!g_hsb_qr) {
        g_hsb_qr = XRcode_create();
        if (!g_hsb_qr) {
            hsb_setError("二维码编码器创建失败");
            return 0;
        }
    }
    data = XByteArray_create_utf8(utf8);
    if (!data) {
        hsb_setError("二维码内容转码失败");
        return 0;
    }
    /* reserved_blank=0（不挖中心洞：这里不嵌 logo）、version=0（自动选
     * 最小合适版本）、level = 调用方给的纠错级。 */
    if (!XRcode_encode_ex(g_hsb_qr, data, 0, 0, level)) {
        XClassDelete((XClass*)data);
        hsb_setError("二维码编码失败（内容过长或纠错级别不支持）：level=%d",
                     level);
        return 0;
    }
    XClassDelete((XClass*)data);
    size = XRcode_size(g_hsb_qr);
    if (size <= 0) {
        hsb_setError("二维码矩阵为尺寸非法（%d）", size);
        return 0;
    }
    return size;
}

HSB_API int hsb_qr_size(void)
{
    return g_hsb_qr ? XRcode_size(g_hsb_qr) : 0;
}

HSB_API const unsigned char* hsb_qr_matrix(void)
{
    XByteArray* m;      /* [非 const 局部] XByteArray_data 收非常量入参
                         * （const 入参会触发 MSVC C4090）；本函数只读。 */
    if (!g_hsb_qr) return NULL;
    m = (XByteArray*)XRcode_matrix(g_hsb_qr);
    if (!m) return NULL;
    return XByteArray_constData(m);
}

HSB_API void hsb_qr_destroy(void)
{
    if (g_hsb_qr) {
        XRcode_delete(g_hsb_qr);
        g_hsb_qr = NULL;
    }
}


#else /* !XRCODE_ON */

HSB_API int hsb_qr_encode(const char* utf8, int level)
{
    (void)utf8; (void)level;
    hsb_setError("本构建未包含二维码能力（XRCODE_ON=0）");
    return 0;
}

HSB_API int hsb_qr_size(void)
{
    return 0;
}

HSB_API const unsigned char* hsb_qr_matrix(void)
{
    return NULL;
}

HSB_API void hsb_qr_destroy(void)
{
}

#endif /* XRCODE_ON */
