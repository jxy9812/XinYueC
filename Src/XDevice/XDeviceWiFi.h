/**
 * @file       XDeviceWiFi.h
 * @brief      无线网络（Wi-Fi）设备契约：设备类 + 适配器枚举 / 扫描 / 连接 /
 *             连接状态。
 * @details    与 XDeviceNetwork.h 同族，遵循统一设备抽象（见
 *             Src/XDevice/XDevice统一设备抽象设计.md）：
 *              - 设备类 XDeviceWiFi 继承 XDevice，类名 "wifi"，经
 *                XDevice_openClass("wifi", ...) 打开；公共门面（打开/关闭/
 *                最近错误）**一律复用父类**，以宏别名给出，不另立实现；
 *              - **功能实现一律经父类统一调度落地**：无线专有能力挂在
 *                GetProperty / Control 两个虚槽上（XDeviceWiFiProperty_* /
 *                XDeviceWiFiCommand_*），由父类的 XDevice_getProperty /
 *                XDevice_control 派发；模块对外的便捷 API 只是**薄转发**
 *                （与 XDeviceNetwork_getSocketType / _setSocketOption 同
 *                构），业务层不得越过父类直接调平台实现；
 *              - 平台资源（WLAN 客户端句柄等）的生命周期由**设备打开/关闭**
 *                承载：XDeviceWiFi_open 就绪、XDeviceWiFi_close 释放，
 *                不另设模块级 deinit 自由函数。
 *
 *             设计口径（与「网络设置」抽象同构）：
 *              - 一切操作都是**发起一次、立即返回**的形态，不阻塞调用者。
 *                平台原生 API 本身即如此（Windows WlanScan 文档原文
 *                "The WlanScan function returns immediately."；ESP8266 SDK
 *                的 wifi_station_scan 返回 bool 表示是否发起成功，结果由
 *                回调稍后送达），契约不做任何包装性阻塞；
 *              - 因此引入**两台状态机**（扫描 / 连接）供上层轮询：
 *                    startX()   → 发起（返回是否成功发起）
 *                    xState()   → 轮询当前状态（立即返回）
 *                    takeX()    → 取走结果（取走后状态复位）
 *                上层用事件循环定时器轮询即可完成"扫描中…"的异步体验，
 *                **不依赖线程**（XTHREAD_ON=0 的裁剪构建同样可用）；
 *              - 平台"完成信号"（Windows 通知回调 / ESP8266 SDK 任务回调）
 *                只允许写一个状态标志，**不得跨线程投递事件、不得触碰
 *                界面对象**；
 *              - 一切超时都必须有兜底收口，否则状态机永远停在 Running。
 *
 *             适配器标识（adapterId）是平台相关的稳定字符串：
 *              - Windows：网卡 GUID 串（形如 "{6B1F9E0C-...}"，与 IP Helper
 *                的 AdapterName 同源，故可与 XNetworkInterface 的 index
 *                对齐，实现 IP 配置通路复用）；
 *              - POSIX：设备路径（如 "/dev/wlan0"）；
 *              - ESP8266："STATION" / "SOFTAP"。
 *
 * @note       Qt 6.8.3 无线侧只有只读能力（QNetworkInterface::InterfaceType
 *             的 Wifi 标签、QNetworkInformation::TransportMedium 的 WiFi），
 *             **没有任何设置/扫描/连接 API**，故本契约不对齐 Qt，自成一套。
 */
#ifndef XDEVICEWIFI_H
#define XDEVICEWIFI_H

#include "XNetwork_config.h"

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#include "XDevice.h"
#include "XDevice_config.h"
#include "XString.h"
#include "XVector.h"

#ifdef __cplusplus
extern "C" {
#endif

#if XDEVICE_ON && XDEVICE_WIFI_ON

/* =========================================================================
 * 一、枚举定义
 * ========================================================================= */

/**
 * @brief 无线网络安全方式。
 * @details 枚举值互斥、不可按位组合；Unknown 表示平台未能判定。
 */
typedef enum XDeviceWiFiSecurity {
    XDeviceWiFi_SecurityUnknown = 0, /**< 未能判定。 */
    XDeviceWiFi_SecurityOpen,        /**< 开放网络（无口令）。 */
    XDeviceWiFi_SecurityWep,         /**< WEP。 */
    XDeviceWiFi_SecurityWpaPsk,      /**< WPA-PSK（TKIP）。 */
    XDeviceWiFi_SecurityWpa2Psk,     /**< WPA2-PSK（RSNA-PSK / CCMP）。 */
    XDeviceWiFi_SecurityWpa2Enterprise, /**< WPA2-企业（802.1X）。 */
    XDeviceWiFi_SecurityWpa3Sae      /**< WPA3-SAE。 */
} XDeviceWiFiSecurity;

/**
 * @brief 扫描状态机状态。
 * @details 迁移：Idle --startScan()--> Running --> Completed / Failed；
 *          takeScanResults() 取走结果后复位为 Idle。
 */
typedef enum XDeviceWiFiScanState {
    XDeviceWiFi_ScanIdle = 0,  /**< 空闲（无扫描在飞）。 */
    XDeviceWiFi_ScanRunning,   /**< 已发起，扫描进行中。 */
    XDeviceWiFi_ScanCompleted, /**< 扫描完成，结果待取。 */
    XDeviceWiFi_ScanFailed     /**< 发起失败或超时失败，结果不可用。 */
} XDeviceWiFiScanState;

/**
 * @brief 连接状态机状态。
 * @details 迁移：Idle --startConnect()--> Connecting --> Connected / Failed。
 */
typedef enum XDeviceWiFiConnectState {
    XDeviceWiFi_ConnectIdle = 0, /**< 空闲（未发起连接）。 */
    XDeviceWiFi_ConnectConnecting, /**< 正在连接。 */
    XDeviceWiFi_ConnectConnected, /**< 已连接。 */
    XDeviceWiFi_ConnectFailed      /**< 连接失败。 */
} XDeviceWiFiConnectState;

/**
 * @brief 适配器工作模式。
 */
typedef enum XDeviceWiFiMode {
    XDeviceWiFi_ModeUnknown = 0, /**< 未知。 */
    XDeviceWiFi_ModeStation,     /**< 客户端（连别人）。 */
    XDeviceWiFi_ModeAccessPoint  /**< 热点（AP/SoftAP）。 */
} XDeviceWiFiMode;

/* =========================================================================
 * 二、能力位（按位组合）
 * ========================================================================= */

/** @brief 支持扫描周边 AP。 */
#define XDeviceWiFi_CapScan         0x0001u
/** @brief 支持发起连接（配置口令 / 选网）。 */
#define XDeviceWiFi_CapConnect      0x0002u
/** @brief 支持断开连接。 */
#define XDeviceWiFi_CapDisconnect   0x0004u
/** @brief 支持增删已保存的配置（profile）。 */
#define XDeviceWiFi_CapProfile      0x0008u
/** @brief 支持热点（AP）模式。 */
#define XDeviceWiFi_CapHostedNetwork 0x0010u

/* =========================================================================
 * 三、数据结构
 * ========================================================================= */

/**
 * @brief 无线适配器信息（一个物理无线网卡一行）。
 * @details 整组字符串由 XDeviceWiFi_freeAdapterInfo 统一释放。
 */
typedef struct XDeviceWiFiAdapterInfo {
    XString* id;          /**< 平台适配器标识（见文件头说明）；不可为空。 */
    XString* name;        /**< 友好名（如 "Wi-Fi"）；可为 NULL。 */
    XString* description; /**< 硬件描述（如 "Intel(R) Wi-Fi 6E AX211"）；可为 NULL。 */
    uint32_t ifIndex;     /**< 与 XNetworkInterface index 同源；0=不可用。 */
    uint32_t capabilities; /**< 该适配器的能力位（XDeviceWiFi_Cap*）。 */
    XDeviceWiFiMode mode; /**< 当前工作模式。 */
} XDeviceWiFiAdapterInfo;

/**
 * @brief 扫描结果条目（一条 = 一个可见网络）。
 * @details 整组字符串由 XDeviceWiFi_freeNetworkInfo 统一释放。
 */
typedef struct XDeviceWiFiNetworkInfo {
    XString* ssid;             /**< 网络名；隐藏网络为 NULL。 */
    XString* bssid;            /**< 接入点 MAC；可为 NULL。 */
    int signalStrength;        /**< 信号强度 0..100；未知为 -1。 */
    int channel;               /**< 信道号；未知为 0。 */
    XDeviceWiFiSecurity security; /**< 安全方式。 */
    bool secured;              /**< true=需要口令。 */
} XDeviceWiFiNetworkInfo;

/**
 * @brief 当前连接信息。
 * @details 整组字符串由 XDeviceWiFi_freeConnectionInfo 统一释放。
 */
typedef struct XDeviceWiFiConnectionInfo {
    XString* ssid;             /**< 已连接网络名；未连接为 NULL。 */
    XString* bssid;            /**< 接入点 MAC；未连接为 NULL。 */
    int signalStrength;        /**< 信号强度 0..100；未知为 -1。 */
    XDeviceWiFiSecurity security; /**< 安全方式。 */
    XDeviceWiFiConnectState state; /**< 连接状态。 */
} XDeviceWiFiConnectionInfo;

/* =========================================================================
 * 四、设备类（继承 XDevice，类名 "wifi"）
 * ========================================================================= */

/** @brief XDeviceWiFi 类虚函数表；继承 XDevice。 */
XCLASS_DEFINE_BEGING(XDeviceWiFi)
XCLASS_DEFINE_EXTEND_END(XDeviceWiFi, XDevice)

/** @brief 无线设备类对象；基类必须是第一个成员。 */
typedef struct XDeviceWiFi
{
    XDevice m_base;
} XDeviceWiFi;

/**
 * @brief 初始化无线设备类虚函数表。
 * @return 共享虚函数表，失败返回 NULL。
 */
XVtable* XDeviceWiFi_class_init(void);

/**
 * @brief 初始化已分配的无线设备对象。
 * @param self 待初始化对象，不能为 NULL。
 */
void XDeviceWiFi_init(XDeviceWiFi* self);

/**
 * @brief 创建无线设备类对象。
 * @return 新对象，失败返回 NULL；调用方负责释放（XClassDelete）。
 */
XDeviceWiFi* XDeviceWiFi_create(void);

/**
 * @brief 把无线设备类注册到 XDevice 注册表（类名 "wifi"）。
 * @details 幂等：首次调用完成注册并返回 true，后续调用直接返回 true。
 * @return 注册成功返回 true；失败返回 false。
 * @note XDevice_openClass("wifi", ...) 会自动懒注册，一般无需手工调用。
 */
bool XDeviceWiFi_register(void);

/* =========================================================================
 * 五、统一公共门面（复用父类，与 XDeviceNetwork 同口径）
 * ========================================================================= */

/**
 * @brief 打开无线设备（平台资源就绪）。
 * @param options 打开选项；借用指针，可为 NULL（NULL=默认选项）。
 * @param error 错误码输出；借用指针，可为 NULL。
 * @return 设备句柄；失败返回 XFD_INVALID。
 * @note 与父类 API 参数契约相同；打开失败多为平台无线服务未就绪。
 */
#define XDeviceWiFi_open(options, error) \
    XDevice_openClass("wifi", (const XDeviceOpenOptions*)(options), (error))
/** @brief 关闭无线设备并释放平台资源；参数契约与父类 API 相同。 */
#define XDeviceWiFi_close   XDevice_close
/**
 * @brief 读取设备最近一次错误码；复用父类门面。
 * @details 值为双段编号：平台原生码（<XDEVICE_ERROR_FRAMEWORK_BASE）或
 *          XDeviceError 枚举。
 */
#define XDeviceWiFi_lastError XDevice_lastError

/* =========================================================================
 * 六、设备专有属性与命令（父类统一调度的落点）
 * ========================================================================= */

/** @brief 无线设备专有属性编号，从 XDeviceProperty_Count 开始分配。
 *  @note 属性是无载荷的设备级标量；**按适配器区分**的查询（扫描/连接状态）
 *        走命令通道（父类只有命令带 XVarList 载荷）。 */
typedef enum XDeviceWiFiProperty
{
    XDeviceWiFiProperty_Capabilities = XDeviceProperty_Count
        /**< 平台能力位（XDeviceWiFi_Cap* 按位组合），值为 uint32。 */,
    XDeviceWiFiProperty_Count /**< 无线属性数量边界，不是有效属性。 */
} XDeviceWiFiProperty;

/** @brief 无线设备专有控制命令。 */
typedef enum XDeviceWiFiCommand
{
    XDeviceWiFiCommand_EnumAdapters = XDeviceCommand_Count
        /**< 枚举无线适配器；in 为 NULL，out 为 XVarList(XVector** adapters)。 */,
    XDeviceWiFiCommand_GetScanState
        /**< 读取扫描状态；in 为 XVarList(const char* adapterId)，out 为 XVarList(int state)。 */,
    XDeviceWiFiCommand_StartScan
        /**< 发起扫描；in 为 XVarList(const char* adapterId)，out 为 NULL。 */,
    XDeviceWiFiCommand_TakeScanResults
        /**< 取走扫描结果；in 为 XVarList(const char* adapterId)，out 为 XVarList(XVector** networks)。 */,
    XDeviceWiFiCommand_StartConnect
        /**< 发起连接；in 为 XVarList(const char* adapterId, const char* ssid, const char* passphrase, int security)，out 为 NULL。 */,
    XDeviceWiFiCommand_GetConnectState
        /**< 读取连接状态；in 为 XVarList(const char* adapterId)，out 为 XVarList(int state)。 */,
    XDeviceWiFiCommand_QueryConnection
        /**< 查询当前连接；in 为 XVarList(const char* adapterId)，out 为 XVarList(XDeviceWiFiConnectionInfo* info)。 */,
    XDeviceWiFiCommand_Disconnect
        /**< 断开连接；in 为 XVarList(const char* adapterId)，out 为 NULL。 */,
    XDeviceWiFiCommand_ForgetProfile
        /**< 删除已保存的网络配置；in 为 XVarList(const char* adapterId, const char* ssid)，out 为 NULL。 */
} XDeviceWiFiCommand;

/* =========================================================================
 * 七、便捷 API（薄转发：经父类 XDevice_getProperty / XDevice_control）
 * ========================================================================= */

/**
 * @brief 查询平台无线能力位。
 * @param fd 无线设备句柄；必须由 XDeviceWiFi_open 返回。
 * @return 能力位掩码（XDeviceWiFi_Cap* 按位组合）；查询失败返回 0。
 */
uint32_t XDeviceWiFi_capabilities(XFd fd);

/**
 * @brief 枚举本机全部无线适配器。
 * @param fd 无线设备句柄。
 * @param outAdapters 输出；成功时指向新建的 XVector*（元素为
 *        XDeviceWiFiAdapterInfo* 指针，容器拥有元素），调用方须用
 *        XDeviceWiFi_freeAdapters 释放；不能为 NULL。
 * @return 成功返回 true；失败返回 false（此时 *outAdapters 为 NULL）。
 */
bool XDeviceWiFi_enumAdapters(XFd fd, XVector** outAdapters);

/**
 * @brief 发起一次扫描（**立即返回，不等待结果**）。
 * @param fd 无线设备句柄。
 * @param adapterId 适配器标识；不能为 NULL/空。
 * @return true=已成功发起；false=发起失败（无此适配器 / 驱动拒绝 / 已有
 *         扫描在飞）。返回 true 不代表扫描已完成。
 */
bool XDeviceWiFi_startScan(XFd fd, const char* adapterId);

/**
 * @brief 轮询扫描状态（立即返回，无副作用）。
 * @param fd 无线设备句柄。
 * @param adapterId 适配器标识；不能为 NULL/空。
 * @return 当前扫描状态；无此适配器返回 XDeviceWiFi_ScanIdle。
 */
XDeviceWiFiScanState XDeviceWiFi_scanState(XFd fd, const char* adapterId);

/**
 * @brief 取走扫描结果，并把扫描状态复位为 Idle。
 * @param fd 无线设备句柄。
 * @param adapterId 适配器标识；不能为 NULL/空。
 * @param outNetworks 输出；成功时指向新建的 XVector*（元素为
 *        XDeviceWiFiNetworkInfo* 指针），调用方须用 XDeviceWiFi_freeNetworks
 *        释放；不能为 NULL。
 * @return 成功返回 true；扫描未完成或失败返回 false。
 */
bool XDeviceWiFi_takeScanResults(XFd fd, const char* adapterId,
                                  XVector** outNetworks);

/**
 * @brief 发起一次连接（**立即返回，不等待连接建立**）。
 * @param fd 无线设备句柄。
 * @param adapterId 适配器标识；不能为 NULL/空。
 * @param ssid 目标网络名；不能为 NULL/空。
 * @param passphrase 口令；开放网络传 NULL 或空串；WEP 传十六进制串。
 * @param security 目标网络安全方式。
 * @return true=已受理（已建配置并提交连接请求）；false=失败。
 * @note Windows 走 Native WiFi：口令以 profile 形式写入（写系统级 profile
 *       需管理员权限），连接结果经 connectState 轮询。
 */
bool XDeviceWiFi_startConnect(XFd fd, const char* adapterId, const char* ssid,
                              const char* passphrase,
                              XDeviceWiFiSecurity security);

/**
 * @brief 轮询连接状态（立即返回，无副作用）。
 * @param fd 无线设备句柄。
 * @param adapterId 适配器标识；不能为 NULL/空。
 * @return 当前连接状态；无此适配器返回 XDeviceWiFi_ConnectIdle。
 */
XDeviceWiFiConnectState XDeviceWiFi_connectState(XFd fd, const char* adapterId);

/**
 * @brief 查询当前连接详情。
 * @param fd 无线设备句柄。
 * @param adapterId 适配器标识；不能为 NULL/空。
 * @param outInfo 输出；成功时由本函数填充（字符串需用
 *                XDeviceWiFi_freeConnectionInfo 释放）；不能为 NULL。
 * @return 成功返回 true；未连接或查询失败返回 false。
 */
bool XDeviceWiFi_queryConnection(XFd fd, const char* adapterId,
                                 XDeviceWiFiConnectionInfo* outInfo);

/**
 * @brief 断开当前连接。
 * @param fd 无线设备句柄。
 * @param adapterId 适配器标识；不能为 NULL/空。
 * @return 平台已受理返回 true；失败返回 false。
 */
bool XDeviceWiFi_disconnect(XFd fd, const char* adapterId);

/**
 * @brief 删除已保存的网络配置（profile）。
 * @param fd 无线设备句柄。
 * @param adapterId 适配器标识；不能为 NULL/空。
 * @param ssid 目标网络名；不能为 NULL/空。
 * @return 成功返回 true；失败返回 false。
 */
bool XDeviceWiFi_forgetProfile(XFd fd, const char* adapterId, const char* ssid);

/* =========================================================================
 * 八、释放接口
 * ========================================================================= */

/**
 * @brief 释放单个适配器信息（含其内部字符串）。
 * @param info 待释放对象；NULL 安全；释放后字段全部置 NULL。
 */
void XDeviceWiFi_freeAdapterInfo(XDeviceWiFiAdapterInfo* info);

/**
 * @brief 释放单个扫描结果条目（含其内部字符串）。
 * @param info 待释放对象；NULL 安全。
 */
void XDeviceWiFi_freeNetworkInfo(XDeviceWiFiNetworkInfo* info);

/**
 * @brief 释放单个连接信息（含其内部字符串）。
 * @param info 待释放对象；NULL 安全。
 */
void XDeviceWiFi_freeConnectionInfo(XDeviceWiFiConnectionInfo* info);

/**
 * @brief 释放适配器容器（逐个释放元素后释放容器）。
 * @param adapters XDeviceWiFi_enumAdapters 的返回值；NULL 安全。
 */
void XDeviceWiFi_freeAdapters(XVector* adapters);

/**
 * @brief 释放扫描结果容器（逐个释放元素后释放容器）。
 * @param networks XDeviceWiFi_takeScanResults 的返回值；NULL 安全。
 */
void XDeviceWiFi_freeNetworks(XVector* networks);

/* =========================================================================
 * 九、平台后端实现契约
 * =========================================================================
 * 以下函数由各平台后端实现（Drive/windows/Device/Network/XDeviceWiFi_win32.c
 * 等），是设备虚槽的叶子实现。**业务层不得直接调用**——功能入口一律经
 * 父类的 XDevice_control / XDevice_getProperty 走统一调度，模块对外只暴露
 * 第七节的便捷 API（薄转发）。
 */

/**
 * @brief 向平台申请无线资源（如 Native WiFi 的客户端句柄）。
 * @return 成功返回 true；平台不支持或申请失败返回 false。
 */
bool XDeviceWiFi_platformOpen(void);

/**
 * @brief 归还平台无线资源。
 * @note 允许重复调用；未申请过资源时为无操作。
 */
void XDeviceWiFi_platformClose(void);

/**
 * @brief 读取平台无线能力位。
 * @param outCapabilities 输出能力位；不能为 NULL。
 * @return 成功返回 true；平台不支持返回 false。
 */
bool XDeviceWiFi_platformCapabilities(uint32_t* outCapabilities);

/**
 * @brief 枚举平台无线适配器。
 * @param outAdapters 输出；成功时指向新建的 XVector*（元素为
 *        XDeviceWiFiAdapterInfo* 指针），调用方负责释放；不能为 NULL。
 * @return 成功返回 true；失败返回 false。
 */
bool XDeviceWiFi_platformEnumAdapters(XVector** outAdapters);

/**
 * @brief 发起一次扫描（立即返回，不等待结果）。
 * @param adapterId 适配器标识；不能为 NULL/空。
 * @return true=已成功发起；false=发起失败。
 */
bool XDeviceWiFi_platformStartScan(const char* adapterId);

/**
 * @brief 轮询扫描状态。
 * @param adapterId 适配器标识；不能为 NULL/空。
 * @return 当前扫描状态。
 */
XDeviceWiFiScanState XDeviceWiFi_platformScanState(const char* adapterId);

/**
 * @brief 取走扫描结果并复位扫描状态。
 * @param adapterId 适配器标识；不能为 NULL/空。
 * @param outNetworks 输出；成功时指向新建的 XVector*（元素为
 *        XDeviceWiFiNetworkInfo* 指针）；不能为 NULL。
 * @return 成功返回 true；扫描未完成或失败返回 false。
 */
bool XDeviceWiFi_platformTakeScanResults(const char* adapterId,
                                         XVector** outNetworks);

/**
 * @brief 发起一次连接（立即返回，不等待连接建立）。
 * @param adapterId 适配器标识；@param ssid 目标网络名；
 *        @param passphrase 口令（开放网络可为 NULL）；
 *        @param security 安全方式。
 * @return true=已受理；false=失败。
 */
bool XDeviceWiFi_platformStartConnect(const char* adapterId, const char* ssid,
                                      const char* passphrase,
                                      XDeviceWiFiSecurity security);

/**
 * @brief 轮询连接状态。
 * @param adapterId 适配器标识；不能为 NULL/空。
 * @return 当前连接状态。
 */
XDeviceWiFiConnectState XDeviceWiFi_platformConnectState(const char* adapterId);

/**
 * @brief 查询当前连接详情。
 * @param adapterId 适配器标识；@param outInfo 输出（由本函数填充）。
 * @return 成功返回 true；未连接或失败返回 false。
 */
bool XDeviceWiFi_platformQueryConnection(const char* adapterId,
                                         XDeviceWiFiConnectionInfo* outInfo);

/**
 * @brief 断开当前连接。
 * @param adapterId 适配器标识；不能为 NULL/空。
 * @return 平台已受理返回 true；失败返回 false。
 */
bool XDeviceWiFi_platformDisconnect(const char* adapterId);

/**
 * @brief 删除已保存的网络配置（profile）。
 * @param adapterId 适配器标识；@param ssid 目标网络名。
 * @return 成功返回 true；失败返回 false。
 */
bool XDeviceWiFi_platformForgetProfile(const char* adapterId, const char* ssid);

#endif /* XNETWORK_ON && XNETWORK_WIFI_ON */

#ifdef __cplusplus
}
#endif

#endif /* XDEVICEWIFI_H */
