#ifndef XESP8266DEVICE_H
#define XESP8266DEVICE_H

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
XCLASS_DEFINE_BEGING(XESP8266Device)
XCLASS_DEFINE_ENUM(XESP8266Device, Open) = XCLASS_VTABLE_GET_SIZE(XIODeviceBase),
XCLASS_DEFINE_ENUM(XESP8266Device, Close),
XCLASS_DEFINE_ENUM(XESP8266Device, Write),
XCLASS_DEFINE_ENUM(XESP8266Device, Read),
XCLASS_DEFINE_ENUM(XESP8266Device, GetBytesAvailable),
XCLASS_DEFINE_ENUM(XESP8266Device, ProcessResponse), // 处理AT指令响应
XCLASS_DEFINE_END(XESP8266Device)

/**
 * @brief ESP8266工作模式枚举
 */
typedef enum 
{
    XESP8266_Mode_STA = 1,        // 客户端模式（连接外部WiFi）
    XESP8266_Mode_AP = 2,         // 接入点模式（自身作为WiFi热点）
    XESP8266_Mode_STA_AP = 3      // 混合模式（同时作为客户端和接入点）
}XESP8266Mode;

/**
 * @brief 连接状态枚举
 */
typedef enum {
    XESP8266_Status_Disconnected = 0,  // 未连接
    XESP8266_Status_Connecting = 1,    // 连接中
    XESP8266_Status_Connected = 2,     // 已连接
    XESP8266_Status_Error = 3          // 错误状态
} XESP8266Status;

/**
 * @brief 网络协议类型
 */
typedef enum {
    XESP8266_Protocol_TCP = 0,    // TCP协议
    XESP8266_Protocol_UDP = 1     // UDP协议
} XESP8266Protocol;

/**
 * @brief AT指令操作类型
 */
typedef enum {
    XESP8266_Op_None,
    XESP8266_Op_TestAT,
    XESP8266_Op_Reset,
    XESP8266_Op_SetMode,
    XESP8266_Op_ConnectWiFi,
    XESP8266_Op_DisconnectWiFi,
    XESP8266_Op_ConfigAP,
    XESP8266_Op_ConnectServer,
    XESP8266_Op_DisconnectServer,
    XESP8266_Op_StartServer,
    XESP8266_Op_StopServer,
    XESP8266_Op_EnterTransparent,
    XESP8266_Op_ExitTransparent
} XESP8266OpType;

/**
 * @brief ESP8266设备结构体
 */
typedef struct XESP8266Device {
    XIODeviceBase m_class;                // 继承XIODeviceBase
    XIODeviceBase* m_io;                  // 底层IO设备(外部传入)
    XESP8266Status m_wifiStatus;          // WiFi连接状态
    XESP8266Status m_serverStatus;        // 服务器连接状态
    XTimerBase* m_timeoutTimer;           // 超时定时器
    XString* m_ssid;                      // WiFi名称缓存
    XString* m_password;                  // WiFi密码缓存
    XString* m_serverIP;                  // 服务器IP缓存
    uint16_t m_serverPort;                // 服务器端口缓存
    XESP8266Protocol m_protocol;          // 协议类型缓存
    bool m_transparentMode;               // 透传模式标志
    XEvent* m_waitEvent;                  // 同步等待事件
    bool m_operationResult;               // 操作结果
    XESP8266OpType m_currentOp;           // 当前操作类型
    char m_responseBuffer[512];           // 响应临时缓冲区
    size_t m_responseLen;                 // 响应数据长度
} XESP8266Device;

/**
 * @brief 初始化ESP8266设备
 * @param device XESP8266Device对象指针
 * @param io 底层IO设备指针(外部提供，必须已初始化)
 */
void XESP8266Device_init(XESP8266Device* device, XIODeviceBase* io);

/**
 * @brief 创建ESP8266设备实例
 * @param io 底层IO设备指针(外部提供，必须已初始化)
 * @return 成功返回XESP8266Device对象指针，失败返回NULL
 */
XESP8266Device* XESP8266Device_create(XIODeviceBase* io);

/**
 * @brief 测试AT指令是否正常工作（异步）
 * @details 发送"AT"指令，成功会触发atResponse信号返回"OK"
 * @param device XESP8266Device对象指针
 * @return 操作是否成功发起
 */
bool XESP8266Device_testAT(XESP8266Device* device);

/**
 * @brief 重启模块（异步）
 * @details 发送"AT+RST"指令，模块重启后会触发atResponse信号
 * @param device XESP8266Device对象指针
 * @return 操作是否成功发起
 */
bool XESP8266Device_reset(XESP8266Device* device);

/**
 * @brief 设置工作模式（异步）
 * @param device XESP8266Device对象指针
 * @param mode 工作模式(XESP8266Mode)
 * @return 操作是否成功发起
 */
bool XESP8266Device_setMode(XESP8266Device* device, XESP8266Mode mode);

/**
 * @brief 连接到WiFi网络（异步）
 * @details 发送"AT+CWJAP"指令，连接结果通过wifiStatusChanged信号通知
 * @param device XESP8266Device对象指针
 * @param ssid WiFi名称
 * @param password WiFi密码
 * @return 操作是否成功发起
 */
bool XESP8266Device_connectWiFi(XESP8266Device* device, const char* ssid, const char* password);

/**
 * @brief 断开WiFi连接（异步）
 * @details 发送"AT+CWQAP"指令，断开结果通过wifiStatusChanged信号通知
 * @param device XESP8266Device对象指针
 * @return 操作是否成功发起
 */
bool XESP8266Device_disconnectWiFi(XESP8266Device* device);

/**
 * @brief 配置AP模式参数（异步）
 * @details 发送"AT+CWSAP"指令，配置AP名称、密码等参数
 * @param device XESP8266Device对象指针
 * @param ssid AP名称
 * @param password AP密码(至少8位)
 * @param channel 信道(1-13)
 * @param encrypt 加密方式(0-4)
 * @return 操作是否成功发起
 */
bool XESP8266Device_configAP(XESP8266Device* device, const char* ssid, const char* password,
    int channel, int encrypt);

/**
 * @brief 连接到服务器（异步）
 * @details 发送"AT+CIPSTART"指令，连接结果通过serverStatusChanged信号通知
 * @param device XESP8266Device对象指针
 * @param protocol 协议类型(TCP/UDP)
 * @param ip 服务器IP地址
 * @param port 服务器端口
 * @return 操作是否成功发起
 */
bool XESP8266Device_connectServer(XESP8266Device* device, XESP8266Protocol protocol,
    const char* ip, uint16_t port);

/**
 * @brief 断开服务器连接（异步）
 * @details 发送"AT+CIPCLOSE"指令，断开结果通过serverStatusChanged信号通知
 * @param device XESP8266Device对象指针
 * @return 操作是否成功发起
 */
bool XESP8266Device_disconnectServer(XESP8266Device* device);

/**
 * @brief 开启服务器模式（异步）
 * @details 发送"AT+CIPSERVER"指令，开启TCP/UDP服务器
 * @param device XESP8266Device对象指针
 * @param protocol 协议类型(TCP/UDP)
 * @param port 服务器端口
 * @return 操作是否成功发起
 */
bool XESP8266Device_startServer(XESP8266Device* device, XESP8266Protocol protocol, uint16_t port);

/**
 * @brief 关闭服务器模式（异步）
 * @details 发送"AT+CIPSERVER=0"指令，关闭服务器
 * @param device XESP8266Device对象指针
 * @return 操作是否成功发起
 */
bool XESP8266Device_stopServer(XESP8266Device* device);

/**
 * @brief 发送数据（异步）
 * @details 透传模式下直接发送数据；非透传模式下使用"AT+CIPSEND"指令发送
 * @param device XESP8266Device对象指针
 * @param data 要发送的数据
 * @param size 数据大小
 * @return 操作是否成功发起
 */
bool XESP8266Device_sendData(XESP8266Device* device, const void* data, size_t size);

/**
 * @brief 进入透传模式（异步）
 * @details 发送"AT+CIPMODE=1"和"AT+CIPSEND"指令
 * @param device XESP8266Device对象指针
 * @return 操作是否成功发起
 */
bool XESP8266Device_enterTransparentMode(XESP8266Device* device);

/**
 * @brief 退出透传模式（异步）
 * @details 发送"+++"指令(无需加回车)
 * @param device XESP8266Device对象指针
 * @return 操作是否成功发起
 */
bool XESP8266Device_exitTransparentMode(XESP8266Device* device);

/**
 * @brief 获取当前WiFi连接状态
 * @param device XESP8266Device对象指针
 * @return 当前连接状态(XESP8266Status)
 */
XESP8266Status XESP8266Device_getWiFiStatus(XESP8266Device* device);

/**
 * @brief 获取当前服务器连接状态
 * @param device XESP8266Device对象指针
 * @return 当前服务器连接状态(XESP8266Status)
 */
XESP8266Status XESP8266Device_getServerStatus(XESP8266Device* device);

/**
 * @brief 同步等待WiFi连接完成
 * @param device XESP8266Device对象指针
 * @param msecs 最大等待时间(毫秒，-1表示无限等待)
 * @return 成功返回true，超时或失败返回false
 */
bool XESP8266Device_waitForWiFiConnected(XESP8266Device* device, int msecs);

/**
 * @brief 同步等待服务器连接完成
 * @param device XESP8266Device对象指针
 * @param msecs 最大等待时间(毫秒，-1表示无限等待)
 * @return 成功返回true，超时或失败返回false
 */
bool XESP8266Device_waitForServerConnected(XESP8266Device* device, int msecs);

/**
 * @brief 信号：WiFi连接状态变化
 * @param device XESP8266Device对象指针
 * @param status 新的连接状态
 * @return 信号标识
 */
void* XESP8266Device_wifiStatusChanged_signal(XESP8266Device* device, XESP8266Status status);

/**
 * @brief 信号：服务器连接状态变化
 * @param device XESP8266Device对象指针
 * @param status 新的连接状态
 * @return 信号标识
 */
void* XESP8266Device_serverStatusChanged_signal(XESP8266Device* device, XESP8266Status status);

/**
 * @brief 信号：接收到数据
 * @param device XESP8266Device对象指针
 * @param data 接收到的数据
 * @param size 数据大小
 * @return 信号标识
 */
void* XESP8266Device_dataReceived_signal(XESP8266Device* device, const char* data, size_t size);

/**
 * @brief 信号：AT指令响应
 * @param device XESP8266Device对象指针
 * @param response 响应内容
 * @return 信号标识
 */
void* XESP8266Device_atResponse_signal(XESP8266Device* device, const char* response);

/**
 * @brief 信号：错误通知
 * @param device XESP8266Device对象指针
 * @param errorCode 错误代码
 * @param errorMsg 错误信息
 * @return 信号标识
 */
void* XESP8266Device_error_signal(XESP8266Device* device, int errorCode, const char* errorMsg);

// 基础方法宏定义
#define XESP8266Device_delete_base        XIODeviceBase_delete_base
#define XESP8266Device_poll_base          XObject_poll_base

#ifdef __cplusplus
}
#endif

#endif // XESP8266DEVICE_H