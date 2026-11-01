/**
 * @file       XNetworkWiFi.h
 * @brief      无线网络业务层公开 API（值语义 + 设备会话管理）。
 * @details    位于 L0（设备层契约 Src/XDevice/XDeviceWiFi.h）与 L2（界面）之间：
 *              - **L0** 只提供平台能力（适配器枚举 / 扫描 / 连接），载体是 C
 *                结构体 + 设备句柄；
 *              - **L1（本层）** 把它包装成值语义的 XClass 对象，并持有无线
 *                设备句柄的生命周期（打开/关闭），业务层只与本层打交道；
 *              - **L2**（界面）只用本层 API，不见设备层、不见平台实现。
 *
 *             三个值对象（与 XNetworkInterface.h 把接口类与配置类放同一头
 *             文件的惯例一致）：
 *               - XNetworkWiFiAdapter   无线适配器（网卡）快照
 *               - XNetworkWiFiNetwork   扫描到的可见网络（AP）条目
 *               - XNetworkWiFiConnection 当前连接快照
 *
 *             异步口径（与 L0 一致，**不依赖线程**）：扫描是"发起一次、
 *             立即返回"的形态，本层原样透出两台状态机——
 *               startScan() → 发起；scanState() → 轮询；takeScanResults() →
 *               取走结果并复位。界面用事件循环定时器轮询即可。
 *
 *             枚举沿用设备层契约的取值域（XDeviceWiFiSecurity 等，见
 *             Src/XDevice/XDeviceWiFi.h）：无线域的词汇表只有一份，避免
 *             上下层转换出错。
 */
#ifndef XNETWORKWIFI_H
#define XNETWORKWIFI_H

#include "XNetwork_config.h"
#include "XDevice_config.h"

#ifdef __cplusplus
extern "C" {
#endif

#if XNETWORK_ON
/* 无线业务层随无线设备开关裁剪（设备开关见 Src/XDevice/XDevice_config.h）。 */
#if XDEVICE_WIFI_ON

#include "XClass.h"
#include "XDeviceWiFi.h"
#include "XString.h"
#include "XVector.h"

#include <stdbool.h>
#include <stdint.h>

/* =============== 值对象：无线适配器 =============== */

XCLASS_DEFINE_BEGING(XNetworkWiFiAdapter)
XCLASS_DEFINE_EXTEND_END(XNetworkWiFiAdapter, XClass);

/**
 * @brief 无线适配器值对象（一个物理无线网卡的快照）。
 */
typedef struct XNetworkWiFiAdapter {
    XClass m_class;             /**< 第一个成员，由 XClass 管理，禁止手工修改。 */
    XString* m_id;              /**< 平台适配器标识（Windows=网卡 GUID 串）。 */
    XString* m_name;            /**< 友好名（如 "Wi-Fi"）；可为 NULL。 */
    XString* m_description;     /**< 硬件描述（如 "Intel(R) Wi-Fi 6E AX211"）；可为 NULL。 */
    uint32_t m_ifIndex;         /**< 与 XNetworkInterface index 同源；0=不可用。 */
    uint32_t m_capabilities;    /**< 能力位（XDeviceWiFi_Cap* 按位组合）。 */
    XDeviceWiFiMode m_mode;     /**< 工作模式（客户端/热点）。 */
} XNetworkWiFiAdapter;

/**
 * @brief 初始化已分配的无线适配器对象。
 * @param adapter 指向未初始化的实例；不能为 NULL。
 */
void XNetworkWiFiAdapter_init(XNetworkWiFiAdapter* adapter);

/**
 * @brief 初始化无线适配器类虚函数表。
 * @return 虚函数表指针。
 */
XVtable* XNetworkWiFiAdapter_class_init(void);

/**
 * @brief 在堆上创建无线适配器对象。
 * @param memory 内存类型。
 * @return 新对象，失败返回 NULL；调用方负责释放（XClassDelete）。
 */
XNetworkWiFiAdapter* XNetworkWiFiAdapter_create_ex(XMemoryType memory);

/**
 * @brief 获取平台适配器标识（借用指针，不应被释放）。
 * @param adapter 实例指针；可为 NULL。
 * @return 标识字符串；对象不存在时返回 NULL。
 */
const XString* XNetworkWiFiAdapter_id(const XNetworkWiFiAdapter* adapter);

/**
 * @brief 获取友好名（借用指针）。
 * @param adapter 实例指针；可为 NULL。
 * @return 友好名；未知时返回 NULL。
 */
const XString* XNetworkWiFiAdapter_name(const XNetworkWiFiAdapter* adapter);

/**
 * @brief 获取硬件描述（借用指针）。
 * @param adapter 实例指针；可为 NULL。
 * @return 硬件描述；未知时返回 NULL。
 */
const XString* XNetworkWiFiAdapter_description(const XNetworkWiFiAdapter* adapter);

/**
 * @brief 获取接口索引。
 * @param adapter 实例指针；可为 NULL。
 * @return 接口索引；不可用时返回 0。
 */
uint32_t XNetworkWiFiAdapter_ifIndex(const XNetworkWiFiAdapter* adapter);

/**
 * @brief 获取适配器能力位。
 * @param adapter 实例指针；可为 NULL。
 * @return 能力位掩码（XDeviceWiFi_Cap*）；不可用时返回 0。
 */
uint32_t XNetworkWiFiAdapter_capabilities(const XNetworkWiFiAdapter* adapter);

/**
 * @brief 获取适配器工作模式。
 * @param adapter 实例指针；可为 NULL。
 * @return 工作模式；未知时返回 XDeviceWiFi_ModeUnknown。
 */
XDeviceWiFiMode XNetworkWiFiAdapter_mode(const XNetworkWiFiAdapter* adapter);

/* =============== 值对象：扫描到的网络 =============== */

XCLASS_DEFINE_BEGING(XNetworkWiFiNetwork)
XCLASS_DEFINE_EXTEND_END(XNetworkWiFiNetwork, XClass);

/**
 * @brief 扫描结果条目值对象（一条 = 一个可见网络）。
 */
typedef struct XNetworkWiFiNetwork {
    XClass m_class;             /**< 第一个成员，由 XClass 管理，禁止手工修改。 */
    XString* m_ssid;            /**< 网络名；隐藏网络为 NULL。 */
    XString* m_bssid;           /**< 接入点 MAC；可为 NULL。 */
    int m_signalStrength;       /**< 信号强度 0..100；未知为 -1。 */
    int m_channel;              /**< 信道号；未知为 0。 */
    XDeviceWiFiSecurity m_security; /**< 安全方式。 */
    bool m_secured;             /**< true=需要口令。 */
} XNetworkWiFiNetwork;

/**
 * @brief 初始化已分配的扫描条目对象。
 * @param network 指向未初始化的实例；不能为 NULL。
 */
void XNetworkWiFiNetwork_init(XNetworkWiFiNetwork* network);

/**
 * @brief 初始化扫描条目类虚函数表。
 * @return 虚函数表指针。
 */
XVtable* XNetworkWiFiNetwork_class_init(void);

/**
 * @brief 在堆上创建扫描条目对象。
 * @param memory 内存类型。
 * @return 新对象，失败返回 NULL；调用方负责释放（XClassDelete）。
 */
XNetworkWiFiNetwork* XNetworkWiFiNetwork_create_ex(XMemoryType memory);

/**
 * @brief 获取网络名（借用指针）。
 * @param network 实例指针；可为 NULL。
 * @return 网络名；隐藏网络或对象不存在时返回 NULL。
 */
const XString* XNetworkWiFiNetwork_ssid(const XNetworkWiFiNetwork* network);

/**
 * @brief 获取接入点 MAC（借用指针）。
 * @param network 实例指针；可为 NULL。
 * @return MAC 文本；未知时返回 NULL。
 */
const XString* XNetworkWiFiNetwork_bssid(const XNetworkWiFiNetwork* network);

/**
 * @brief 获取信号强度。
 * @param network 实例指针；可为 NULL。
 * @return 信号强度 0..100；未知时返回 -1。
 */
int XNetworkWiFiNetwork_signalStrength(const XNetworkWiFiNetwork* network);

/**
 * @brief 获取信道号。
 * @param network 实例指针；可为 NULL。
 * @return 信道号；未知时返回 0。
 */
int XNetworkWiFiNetwork_channel(const XNetworkWiFiNetwork* network);

/**
 * @brief 获取安全方式。
 * @param network 实例指针；可为 NULL。
 * @return 安全方式；未知时返回 XDeviceWiFi_SecurityUnknown。
 */
XDeviceWiFiSecurity XNetworkWiFiNetwork_security(const XNetworkWiFiNetwork* network);

/**
 * @brief 判断该网络是否需要口令。
 * @param network 实例指针；可为 NULL。
 * @return 需要口令返回 true。
 */
bool XNetworkWiFiNetwork_isSecured(const XNetworkWiFiNetwork* network);

/* =============== 值对象：当前连接 =============== */

XCLASS_DEFINE_BEGING(XNetworkWiFiConnection)
XCLASS_DEFINE_EXTEND_END(XNetworkWiFiConnection, XClass);

/**
 * @brief 当前连接快照值对象。
 */
typedef struct XNetworkWiFiConnection {
    XClass m_class;             /**< 第一个成员，由 XClass 管理，禁止手工修改。 */
    XString* m_ssid;            /**< 已连接网络名；未连接为 NULL。 */
    XString* m_bssid;           /**< 接入点 MAC；未连接为 NULL。 */
    int m_signalStrength;       /**< 信号强度 0..100；未知为 -1。 */
    XDeviceWiFiSecurity m_security; /**< 安全方式。 */
    XDeviceWiFiConnectState m_state; /**< 连接状态。 */
} XNetworkWiFiConnection;

/**
 * @brief 初始化已分配的连接快照对象。
 * @param connection 指向未初始化的实例；不能为 NULL。
 */
void XNetworkWiFiConnection_init(XNetworkWiFiConnection* connection);

/**
 * @brief 初始化连接快照类虚函数表。
 * @return 虚函数表指针。
 */
XVtable* XNetworkWiFiConnection_class_init(void);

/**
 * @brief 在堆上创建连接快照对象。
 * @param memory 内存类型。
 * @return 新对象，失败返回 NULL；调用方负责释放（XClassDelete）。
 */
XNetworkWiFiConnection* XNetworkWiFiConnection_create_ex(XMemoryType memory);

/**
 * @brief 获取已连接网络名（借用指针）。
 * @param connection 实例指针；可为 NULL。
 * @return 网络名；未连接时返回 NULL。
 */
const XString* XNetworkWiFiConnection_ssid(const XNetworkWiFiConnection* connection);

/**
 * @brief 获取接入点 MAC（借用指针）。
 * @param connection 实例指针；可为 NULL。
 * @return MAC 文本；未连接时返回 NULL。
 */
const XString* XNetworkWiFiConnection_bssid(const XNetworkWiFiConnection* connection);

/**
 * @brief 获取信号强度。
 * @param connection 实例指针；可为 NULL。
 * @return 信号强度 0..100；未知时返回 -1。
 */
int XNetworkWiFiConnection_signalStrength(const XNetworkWiFiConnection* connection);

/**
 * @brief 获取安全方式。
 * @param connection 实例指针；可为 NULL。
 * @return 安全方式；未知时返回 XDeviceWiFi_SecurityUnknown。
 */
XDeviceWiFiSecurity XNetworkWiFiConnection_security(const XNetworkWiFiConnection* connection);

/**
 * @brief 获取连接状态。
 * @param connection 实例指针；可为 NULL。
 * @return 连接状态；未连接时返回 XDeviceWiFi_ConnectIdle。
 */
XDeviceWiFiConnectState XNetworkWiFiConnection_state(const XNetworkWiFiConnection* connection);

/* =============== 门面：无线会话 =============== */

XCLASS_DEFINE_BEGING(XNetworkWiFi)
XCLASS_DEFINE_EXTEND_END(XNetworkWiFi, XClass);

/**
 * @brief 无线会话对象（持有无线设备句柄）。
 * @details 一个会话对应一个已打开的无线设备（类名 "wifi"）；会话析构时
 *          自动关闭设备，调用方无需再手工关闭。
 */
typedef struct XNetworkWiFi {
    XClass m_class;             /**< 第一个成员，由 XClass 管理，禁止手工修改。 */
    XFd m_fd;                   /**< 无线设备句柄；未打开时为 XFD_INVALID。 */
    int m_lastError;            /**< 最近一次错误码（设备层错误码口径）。 */
} XNetworkWiFi;

/**
 * @brief 初始化已分配的无线会话对象。
 * @param wifi 指向未初始化的实例；不能为 NULL。
 */
void XNetworkWiFi_init(XNetworkWiFi* wifi);

/**
 * @brief 初始化无线会话类虚函数表。
 * @return 虚函数表指针。
 */
XVtable* XNetworkWiFi_class_init(void);

/**
 * @brief 在堆上创建无线会话对象（**不打开设备**）。
 * @param memory 内存类型。
 * @return 新对象，失败返回 NULL；调用方负责释放（XClassDelete）。
 */
XNetworkWiFi* XNetworkWiFi_create_ex(XMemoryType memory);

/**
 * @brief 打开无线设备（申请平台无线资源）。
 * @param wifi 会话对象；不能为 NULL。
 * @param error 错误码输出；可为 NULL。
 * @return 成功返回 true；失败返回 false（平台不支持或资源申请失败）。
 */
bool XNetworkWiFi_open(XNetworkWiFi* wifi, int* error);

/**
 * @brief 关闭无线设备（归还平台无线资源）。
 * @param wifi 会话对象；可为 NULL。
 * @note 未打开时为无操作；重复调用安全。
 */
void XNetworkWiFi_close(XNetworkWiFi* wifi);

/**
 * @brief 判断会话是否已打开设备。
 * @param wifi 会话对象；可为 NULL。
 * @return 已打开返回 true。
 */
bool XNetworkWiFi_isOpen(const XNetworkWiFi* wifi);

/**
 * @brief 读取最近一次错误码。
 * @param wifi 会话对象；可为 NULL。
 * @return 错误码（设备层 XDeviceError 口径）；无错误返回 0。
 */
int XNetworkWiFi_lastError(const XNetworkWiFi* wifi);

/**
 * @brief 查询平台无线能力位。
 * @param wifi 会话对象；不能为 NULL。
 * @return 能力位掩码（XDeviceWiFi_Cap*）；未打开或平台不支持返回 0。
 */
uint32_t XNetworkWiFi_capabilities(const XNetworkWiFi* wifi);

/**
 * @brief 枚举本机全部无线适配器。
 * @param wifi 会话对象；不能为 NULL。
 * @return 新建的 XVector*（元素为 XNetworkWiFiAdapter 值，容器拥有元素，
 *         调用方用 XClassDelete 释放容器即可）；失败返回 NULL。
 */
XVector* XNetworkWiFi_adapters(XNetworkWiFi* wifi);

/**
 * @brief 发起一次扫描（**立即返回，不等待结果**）。
 * @param wifi 会话对象；@param adapter 目标适配器；不能为 NULL。
 * @return true=已成功发起；false=发起失败。
 */
bool XNetworkWiFi_startScan(XNetworkWiFi* wifi,
                             const XNetworkWiFiAdapter* adapter);

/**
 * @brief 轮询扫描状态（立即返回，无副作用）。
 * @param wifi 会话对象；@param adapter 目标适配器；不能为 NULL。
 * @return 当前扫描状态；参数非法时返回 XDeviceWiFi_ScanIdle。
 */
XDeviceWiFiScanState XNetworkWiFi_scanState(XNetworkWiFi* wifi,
                                             const XNetworkWiFiAdapter* adapter);

/**
 * @brief 取走扫描结果，并把扫描状态复位为 Idle。
 * @param wifi 会话对象；@param adapter 目标适配器；不能为 NULL。
 * @return 新建的 XVector*（元素为 XNetworkWiFiNetwork 值，容器拥有元素，
 *         调用方用 XClassDelete 释放容器即可）；扫描未完成返回 NULL。
 */
XVector* XNetworkWiFi_takeScanResults(XNetworkWiFi* wifi,
                                      const XNetworkWiFiAdapter* adapter);

/**
 * @brief 发起一次连接（**立即返回，不等待连接建立**）。
 * @param wifi 会话对象；@param adapter 目标适配器；
 *        @param ssid 目标网络名；@param passphrase 口令（开放网络可为 NULL）；
 *        @param security 安全方式。
 * @return true=已受理；false=失败。
 */
bool XNetworkWiFi_connect(XNetworkWiFi* wifi, const XNetworkWiFiAdapter* adapter,
                          const char* ssid, const char* passphrase,
                          XDeviceWiFiSecurity security);

/**
 * @brief 轮询连接状态（立即返回，无副作用）。
 * @param wifi 会话对象；@param adapter 目标适配器；不能为 NULL。
 * @return 当前连接状态；参数非法时返回 XDeviceWiFi_ConnectIdle。
 */
XDeviceWiFiConnectState XNetworkWiFi_connectState(XNetworkWiFi* wifi,
                                                  const XNetworkWiFiAdapter* adapter);

/**
 * @brief 查询当前连接快照。
 * @param wifi 会话对象；@param adapter 目标适配器；不能为 NULL。
 * @return 新建的 XNetworkWiFiConnection*（调用方负责释放）；未连接返回 NULL。
 */
XNetworkWiFiConnection* XNetworkWiFi_queryConnection(
    XNetworkWiFi* wifi, const XNetworkWiFiAdapter* adapter);

/**
 * @brief 断开当前连接。
 * @param wifi 会话对象；@param adapter 目标适配器；不能为 NULL。
 * @return 平台已受理返回 true；失败返回 false。
 */
bool XNetworkWiFi_disconnect(XNetworkWiFi* wifi,
                             const XNetworkWiFiAdapter* adapter);

/**
 * @brief 删除已保存的网络配置（profile）。
 * @param wifi 会话对象；@param adapter 目标适配器；
 *        @param ssid 目标网络名。
 * @return 成功返回 true；失败返回 false。
 */
bool XNetworkWiFi_forgetProfile(XNetworkWiFi* wifi,
                                const XNetworkWiFiAdapter* adapter,
                                const char* ssid);

#endif // XNETWORK_WIFI_ON
#endif /* XNETWORK_ON */

#ifdef __cplusplus
}
#endif

/* XClass create API default-memory wrappers. */
#undef XNetworkWiFiAdapter_create
#define XNetworkWiFiAdapter_create() \
    XNetworkWiFiAdapter_create_ex(XCLASS_DEFAULT_MEMORY_TYPE)
#undef XNetworkWiFiNetwork_create
#define XNetworkWiFiNetwork_create() \
    XNetworkWiFiNetwork_create_ex(XCLASS_DEFAULT_MEMORY_TYPE)
#undef XNetworkWiFiConnection_create
#define XNetworkWiFiConnection_create() \
    XNetworkWiFiConnection_create_ex(XCLASS_DEFAULT_MEMORY_TYPE)
#undef XNetworkWiFi_create
#define XNetworkWiFi_create() XNetworkWiFi_create_ex(XCLASS_DEFAULT_MEMORY_TYPE)

#endif /* XNETWORKWIFI_H */
