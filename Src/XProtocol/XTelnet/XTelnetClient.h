/** @file XTelnetClient.h
 * @brief Telnet 客户端协议栈：XTelnetClient。
 * @details
 * 实现 Telnet NVT 文本规范化、IAC 转义、ECHO/SUPPRESS-GO-AHEAD 协商、
 * 子协商跳过和 CR-NUL/CR-LF 输入处理。客户端不创建线程或套接字；调用方
 * 将已连接的 XIODevice（例如 XTcpSocket）绑定后，由 readyRead 信号或
 * feedData 驱动状态机。
 *
 * 文件编码：UTF-8 带 BOM，中文注释。
 */

#ifndef XTELNETCLIENT_H
#define XTELNETCLIENT_H

#include "XTelnet_config.h"

#ifdef __cplusplus
extern "C" {
#endif

#if XPROTOCOL_ON && XTELNET_ON && XTELNET_CLIENT_ON

#include "XObject.h"
#include "XIODevice.h"
#include "XProtocolIo.h"
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

XCLASS_DEFINE_BEGING(XTelnetClient)
XCLASS_DEFINE_EXTEND_END(XTelnetClient, XObject)

/** @brief Telnet 客户端输入解析状态。 */
typedef enum XTelnetClientState {
    XTelnetClientState_Data = 0,
    XTelnetClientState_Iac,
    XTelnetClientState_Option,
    XTelnetClientState_Subnegotiation,
    XTelnetClientState_SubnegotiationIac
} XTelnetClientState;

/** @brief Telnet 客户端。 */
typedef struct XTelnetClient {
    XObject m_class;
    XIODevice* m_device;            /**< 借用的已连接字节流设备。 */
    XConnection* m_readyRead;       /**< 设备 readyRead 信号连接。 */
    XTelnetClientState m_state;     /**< 入站 Telnet 解析状态。 */
    uint8_t m_negotiation;          /**< 等待选项号的 IAC 协商命令。 */
    struct {
        uint32_t m_afterCarriageReturn : 1;
        uint32_t m_localEchoEnabled : 1;
        uint32_t m_closed : 1;
    };
} XTelnetClient;

XVtable* XTelnetClient_class_init(void);
void XTelnetClient_init(XTelnetClient* self);
XTelnetClient* XTelnetClient_create_ex(XMemoryType memory);
#define XTelnetClient_deinit_base XClass_deinit_base
#define XTelnetClient_delete_base XClass_delete_base
#define XTelnetClient_deleteLater XObject_deleteLater

/** @brief 绑定已连接的字节流设备，并接管其 readyRead 驱动。 */
bool XTelnetClient_setDevice(XTelnetClient* self, XIODevice* device);
/** @brief 发送基础协商序列并启动客户端。 */
bool XTelnetClient_start(XTelnetClient* self);
/** @brief 停止协议栈并断开 readyRead 连接；不关闭借用的设备。 */
void XTelnetClient_stop(XTelnetClient* self);
/** @brief 投入收到的 Telnet 字节。 */
XProtocolResult XTelnetClient_feedData(XTelnetClient* self,
                                       const void* data, size_t size);
/** @brief 发送用户数据，自动执行 LF->CRLF 与 IAC 转义。 */
int64_t XTelnetClient_write(XTelnetClient* self, const void* data, size_t size);
/** @brief 刷新底层设备。 */
bool XTelnetClient_flush(XTelnetClient* self);
/** @brief 查询客户端是否已经关闭。 */
bool XTelnetClient_isClosed(const XTelnetClient* self);
/** @brief 查询当前是否应由本地终端回显用户输入。 */
bool XTelnetClient_localEchoEnabled(const XTelnetClient* self);

/** @brief 收到远端应用数据。参数依次为 data、size。 */
void* XTelnetClient_dataReceived_signal(XTelnetClient* self,
                                        const void* data, size_t size);
/** @brief 协议栈关闭。 */
void* XTelnetClient_closed_signal(XTelnetClient* self);
/** @brief 协议或传输错误。参数为 XProtocolResult。 */
void* XTelnetClient_errorOccurred_signal(XTelnetClient* self, int error);

#endif /* XPROTOCOL_ON && XTELNET_ON && XTELNET_CLIENT_ON */

#ifdef __cplusplus
}
#endif


/* XClass create API default-memory wrappers. */
#undef XTelnetClient_create
#define XTelnetClient_create(...) XTelnetClient_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, ##__VA_ARGS__)

#endif /* XTELNETCLIENT_H */
