#ifndef XESP8266WIFI_H
#define XESP8266WIFI_H

#ifdef __cplusplus
extern "C" {
#endif

#include "XIODeviceBase.h"
#include "XString.h"
#include "XEvent.h"
#include "XTimerBase.h"
#include "XCircularQueue.h"

/**
 * @brief ESP8266-01 WiFi模块AT指令操作类
 * @details 继承自XIODeviceBase，通过外部传入的XIODeviceBase接口与模块通信，
 * 支持所有ESP8266-01S的AT指令操作。所有操作默认异步执行，通过信号通知结果，
 * 同时提供同步等待方法。不维护独立接收缓冲区，直接使用底层IO设备的缓冲区
 */
XCLASS_DEFINE_BEGING(XESP8266Wifi)
XCLASS_DEFINE_ENUM(XESP8266Wifi, ProcessResponse) = XCLASS_VTABLE_GET_SIZE(XIODeviceBase), // 处理AT指令响应
XCLASS_DEFINE_END(XESP8266Wifi)

/**
 * @brief ESP8266工作模式枚举
 */
typedef enum 
{
    XESP8266_Mode_STA = 1,        // 客户端模式（连接外部WiFi）
    XESP8266_Mode_AP = 2,         // 接入点模式（自身作为WiFi热点）
    XESP8266_Mode_STA_AP = 3      // 混合模式（同时作为客户端和接入点）
}XESP8266WifiMode;

/**
 * @brief ESP8266 AP模式加密方式枚举
 * @details 对应AT+CWSAP指令的加密方式参数，不同值代表不同的WiFi加密协议
 */
typedef enum {
    XESP8266_Encrypt_None = 0,        // 不加密 (无密码)
    XESP8266_Encrypt_WEP = 1,         // WEP加密 (有线等效加密，安全性较低，不推荐)
    XESP8266_Encrypt_WPA_PSK = 2,     // WPA-PSK加密 (WiFi Protected Access - Pre-Shared Key，较安全)
    XESP8266_Encrypt_WPA2_PSK = 3,    // WPA2-PSK加密 (WPA2增强版，安全性更高，推荐使用)
    XESP8266_Encrypt_WPA_WPA2_PSK = 4 // WPA/WPA2混合模式 (同时支持WPA和WPA2，兼容性更好)
} XESP8266WifiEncryption;
/**
 * @brief 连接状态枚举
 */
typedef enum {
    XESP8266_Status_Disconnected = 0,  // 未连接
    XESP8266_Status_Connecting = 1,    // 连接中
    XESP8266_Status_Connected = 2,     // 已连接
    XESP8266_Status_Error = 3          // 错误状态
} XESP8266WifiStatus;

/**
 * @brief 网络协议类型
 */
typedef enum {
    XESP8266_Protocol_TCP = 0,    // TCP协议
    XESP8266_Protocol_UDP = 1     // UDP协议
} XESP8266WifiProtocol;

/**
 * @brief AT指令操作类型
 */
typedef enum {
    XESP8266_Op_None,               // 无操作
    XESP8266_Op_TestAT,             // 测试AT指令
    XESP8266_Op_Reset,              // 重置模块
    XESP8266_Op_SetMode,            // 设置工作模式
    XESP8266_Op_ConnectWiFi,        // 连接WiFi
    XESP8266_Op_DisconnectWiFi,     // 断开WiFi连接
    XESP8266_Op_ConfigAP,           // 配置AP模式
    XESP8266_Op_ConnectServer,      // 连接服务器
    XESP8266_Op_DisconnectServer,   // 断开服务器连接
    XESP8266_Op_WriteData,          // 写入数据
    XESP8266_Op_StartServer,        // 启动服务器
    XESP8266_Op_StopServer,         // 停止服务器
    XESP8266_Op_EnterTransparent,   // 进入透传模式
    XESP8266_Op_ExitTransparent     // 退出透传模式
} XESP8266WifiOpType;

/**
 * @brief ESP8266设备结构体
 */
typedef struct XESP8266Wifi {
    XIODeviceBase m_class;                // 继承XIODeviceBase
    XIODeviceBase* m_io;                  // 底层IO设备(外部传入)
    XESP8266WifiStatus m_wifiStatus;          // WiFi连接状态
    XESP8266WifiStatus m_serverStatus;        // 服务器连接状态
    XTimerBase* m_timeoutTimer;           // 超时定时器
    XString* m_ssid;                      // WiFi名称缓存
    XString* m_password;                  // WiFi密码缓存
    XString* m_serverIP;                  // 服务器IP缓存
    uint16_t m_serverPort;                // 服务器端口缓存
    bool m_transparentMode;               // 透传模式标志
    bool m_operationResult;               // 操作结果
    XESP8266WifiProtocol m_protocol;          // 协议类型缓存
    XEventLoop* m_loop;
    XESP8266WifiOpType m_currentOp;           // 当前操作类型
    char m_responseBuffer[512];           // 响应临时缓冲区
    //char m_cmd[128];//cmd缓冲区
    size_t m_responseLen;                 // 响应数据长度
} XESP8266Wifi;

/**
 * @brief 初始化ESP8266设备
 * @param device XESP8266Wifi对象指针
 * @param io 底层IO设备指针(外部提供，必须已初始化)
 */
void XESP8266Wifi_init(XESP8266Wifi* device, XIODeviceBase* io);

/**
 * @brief 创建ESP8266设备实例
 * @param io 底层IO设备指针(外部提供，必须已初始化)
 * @return 成功返回XESP8266Wifi对象指针，失败返回NULL
 */
XESP8266Wifi* XESP8266Wifi_create(XIODeviceBase* io);

/**
 * @brief 测试AT指令是否正常工作（异步）
 * @details 发送"AT"指令，成功会触发atResponse信号返回"OK"
 * @param device XESP8266Wifi对象指针
 * @return 操作是否成功发起
 */
bool XESP8266Wifi_testAT(XESP8266Wifi* device);

/**
 * @brief 重启模块（异步）
 * @details 发送"AT+RST"指令，模块重启后会触发atResponse信号
 * @param device XESP8266Wifi对象指针
 * @return 操作是否成功发起
 */
bool XESP8266Wifi_reset(XESP8266Wifi* device);

/**
 * @brief 设置工作模式（异步）
 * @param device XESP8266Wifi对象指针
 * @param mode 工作模式(XESP8266WifiMode)
 * @return 操作是否成功发起
 */
bool XESP8266Wifi_setMode(XESP8266Wifi* device, XESP8266WifiMode mode);

/**
 * @brief 连接到WiFi网络（异步）
 * @details 发送"AT+CWJAP"指令，连接结果通过wifiStatusChanged信号通知
 * @param device XESP8266Wifi对象指针
 * @param ssid WiFi名称
 * @param password WiFi密码
 * @return 操作是否成功发起
 */
bool XESP8266Wifi_connectWiFi(XESP8266Wifi* device, const char* ssid, const char* password);

/**
 * @brief 断开WiFi连接（异步）
 * @details 发送"AT+CWQAP"指令，断开结果通过wifiStatusChanged信号通知
 * @param device XESP8266Wifi对象指针
 * @return 操作是否成功发起
 */
bool XESP8266Wifi_disconnectWiFi(XESP8266Wifi* device);

/**
 * @brief 配置AP模式参数（异步）
 * @details 发送"AT+CWSAP"指令，配置AP名称、密码等参数
 * @param device XESP8266Wifi对象指针
 * @param ssid AP名称（字符串，最大长度32字节）
 * @param password AP密码(至少8位，当加密方式为None时可忽略)
 * @param channel 信道(1-13，常用1、6、11以避免信道干扰)
 * @param encrypt 加密方式(XESP8266WifiEncryption枚举，指定WiFi加密协议)
 * @return 操作是否成功发起（true表示指令已发送，false表示发送失败）
 */
bool XESP8266Wifi_configAP(XESP8266Wifi* device, const char* ssid, const char* password,
    int channel, XESP8266WifiEncryption encrypt);

/**
 * @brief 连接到服务器（异步）
 * @details 发送"AT+CIPSTART"指令，连接结果通过serverStatusChanged信号通知
 * @param device XESP8266Wifi对象指针
 * @param protocol 协议类型(TCP/UDP)
 * @param ip 服务器IP地址
 * @param port 服务器端口
 * @return 操作是否成功发起
 */
bool XESP8266Wifi_connectServer(XESP8266Wifi* device, XESP8266WifiProtocol protocol,
    const char* ip, uint16_t port);

/**
 * @brief 断开服务器连接（异步）
 * @details 发送"AT+CIPCLOSE"指令，断开结果通过serverStatusChanged信号通知
 * @param device XESP8266Wifi对象指针
 * @return 操作是否成功发起
 */
bool XESP8266Wifi_disconnectServer(XESP8266Wifi* device);

/**
 * @brief 开启服务器模式（异步）
 * @details 发送"AT+CIPSERVER"指令，开启TCP/UDP服务器
 * @param device XESP8266Wifi对象指针
 * @param protocol 协议类型(TCP/UDP)
 * @param port 服务器端口
 * @return 操作是否成功发起
 */
bool XESP8266Wifi_startServer(XESP8266Wifi* device, XESP8266WifiProtocol protocol, uint16_t port);

/**
 * @brief 关闭服务器模式（异步）
 * @details 发送"AT+CIPSERVER=0"指令，关闭服务器
 * @param device XESP8266Wifi对象指针
 * @return 操作是否成功发起
 */
bool XESP8266Wifi_stopServer(XESP8266Wifi* device);

/**
 * @brief 发送数据（异步）
 * @details 透传模式下直接发送数据；非透传模式下使用"AT+CIPSEND"指令发送
 * @param device XESP8266Wifi对象指针
 * @param data 要发送的数据
 * @param size 数据大小
 * @return 操作是否成功发起
 */
bool XESP8266Wifi_sendData(XESP8266Wifi* device, const void* data, size_t size);

/**
 * @brief 进入透传模式（异步）
 * @details 发送"AT+CIPMODE=1"和"AT+CIPSEND"指令
 * @param device XESP8266Wifi对象指针
 * @return 操作是否成功发起
 */
bool XESP8266Wifi_enterTransparentMode(XESP8266Wifi* device);

/**
 * @brief 退出透传模式（异步）
 * @details 发送"+++"指令(无需加回车)
 * @param device XESP8266Wifi对象指针
 * @return 操作是否成功发起
 */
bool XESP8266Wifi_exitTransparentMode(XESP8266Wifi* device);

/**
 * @brief 获取当前WiFi连接状态
 * @param device XESP8266Wifi对象指针
 * @return 当前连接状态(XESP8266WifiStatus)
 */
XESP8266WifiStatus XESP8266Wifi_getWiFiStatus(XESP8266Wifi* device);

/**
 * @brief 获取当前服务器连接状态
 * @param device XESP8266Wifi对象指针
 * @return 当前服务器连接状态(XESP8266WifiStatus)
 */
XESP8266WifiStatus XESP8266Wifi_getServerStatus(XESP8266Wifi* device);

/**
 * @brief 同步等待WiFi连接完成
 * @param device XESP8266Wifi对象指针
 * @param msecs 最大等待时间(毫秒，-1表示无限等待)
 * @return 成功返回true，超时或失败返回false
 */
bool XESP8266Wifi_waitForWiFiConnected(XESP8266Wifi* device, int msecs);

/**
 * @brief 同步等待服务器连接完成
 * @param device XESP8266Wifi对象指针
 * @param msecs 最大等待时间(毫秒，-1表示无限等待)
 * @return 成功返回true，超时或失败返回false
 */
bool XESP8266Wifi_waitForServerConnected(XESP8266Wifi* device, int msecs);

/**
 * @brief 信号：WiFi连接状态变化
 * @param device XESP8266Wifi对象指针
 * @param status 新的连接状态
 * @return 信号标识
 */
void* XESP8266Wifi_wifiStatusChanged_signal(XESP8266Wifi* device, XESP8266WifiStatus status);

/**
 * @brief 信号：服务器连接状态变化
 * @param device XESP8266Wifi对象指针
 * @param status 新的连接状态
 * @return 信号标识
 */
void* XESP8266Wifi_serverStatusChanged_signal(XESP8266Wifi* device, XESP8266WifiStatus status);

/**
 * @brief 信号：接收到数据
 * @param device XESP8266Wifi对象指针
 * @param data 接收到的数据
 * @param size 数据大小
 * @return 信号标识
 */
void* XESP8266Wifi_dataReceived_signal(XESP8266Wifi* device, const char* data, size_t size);

/**
 * @brief 信号：AT指令响应
 * @param device XESP8266Wifi对象指针
 * @param response 响应内容
 * @return 信号标识
 */
void* XESP8266Wifi_atResponse_signal(XESP8266Wifi* device, const char* response);

/**
 * @brief 信号：错误通知
 * @param device XESP8266Wifi对象指针
 * @param errorCode 错误代码
 * @param errorMsg 错误信息
 * @return 信号标识
 */
void* XESP8266Wifi_error_signal(XESP8266Wifi* device, int errorCode, const char* errorMsg);

void* XESP8266Wifi_ok_signal(XESP8266Wifi* device);
void* XESP8266Wifi_connect_signal(XESP8266Wifi* device);
void* XESP8266Wifi_disconnect_signal(XESP8266Wifi* device);
// 基础方法宏定义
#define XESP8266Wifi_delete_base        XIODeviceBase_delete_base
#define XESP8266Wifi_poll_base          XObject_poll_base

#ifdef __cplusplus
}
#endif

#endif // XESP8266Wifi_H