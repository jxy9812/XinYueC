/** @file XTelnetClient.h
 * @brief Telnet 客户端协议栈：XTelnetClient。
 * @details
 * 实现 Telnet NVT 文本规范化、IAC 转义、ECHO/SUPPRESS-GO-AHEAD 协商、
 * 子协商跳过和 CR-NUL/CR-LF 输入处理。客户端不创建线程或套接字；调用方
 * 将已连接的 XIODevice（例如 XTcpSocket）绑定后，由 readyRead 信号或
 * feedData 驱动状态机。实现只依赖 XinYueC 抽象层，禁止调用 Win32、
 * POSIX、Qt 或其他平台 API。
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
    XTelnetClientState_Data = 0,         /**< 普通文本数据。 */
    XTelnetClientState_Iac,              /**< 已收到 IAC，等待命令字节。 */
    XTelnetClientState_Option,           /**< 等待协商选项号。 */
    XTelnetClientState_Subnegotiation,   /**< 跳过子协商负载。 */
    XTelnetClientState_SubnegotiationIac /**< 子协商中的 IAC，等待 SE 结束。 */
} XTelnetClientState;

/** @brief Telnet 客户端。 */
typedef struct XTelnetClient {
    XObject m_class;                /**< 第一个成员；由 XClass 管理，禁止手工修改。 */
    XIODevice* m_device;            /**< 借用的已连接字节流设备。 */
    XConnection* m_readyRead;       /**< 设备 readyRead 信号连接。 */
    XTelnetClientState m_state;     /**< 入站 Telnet 解析状态。 */
    uint8_t m_negotiation;          /**< 等待选项号的 IAC 协商命令。 */
    struct {
        uint8_t m_afterCarriageReturn : 1; /**< 已收到 CR；吞掉随后的 NUL/LF。 */
        uint8_t m_localEchoEnabled : 1;    /**< 是否由本地终端回显用户输入。 */
        uint8_t m_closed : 1;              /**< 协议栈是否已关闭。 */
    };
} XTelnetClient;

/**
 * @brief 初始化 Telnet 客户端虚表。
 * @return Telnet 客户端虚表指针；指向类内部静态存储，调用方不得释放或修改。
 */
XVtable* XTelnetClient_class_init(void);
/**
 * @brief 初始化 Telnet 客户端对象。
 * @details 对象先清零再完成 XObject 基类初始化，默认开启本地回显；
 *          仅用于栈上或自管内存对象，堆对象请用 XTelnetClient_create_ex。
 * @param self 待初始化的 Telnet 客户端；不能为 NULL。
 * @return 无。
 * @note 初始化完成后由 XClassDeinit 反初始化；不得对同一对象重复初始化。
 */
void XTelnetClient_init(XTelnetClient* self);
/**
 * @brief 创建 Telnet 客户端。
 * @param memory 内存类型，取 XMemoryType 枚举值。
 * @return 新建的 Telnet 客户端；失败返回 NULL。调用方使用 XClassDelete
 *         释放，或在信号/回调上下文中用 XTelnetClient_deleteLater 延迟释放。
 */
XTelnetClient* XTelnetClient_create_ex(XMemoryType memory);
/**
 * @brief 延迟销毁 Telnet 客户端（对标 Qt deleteLater 语义）。
 * @note  在信号/回调上下文中必须用 deleteLater 释放，DeferredDelete
 *        经事件循环回收；同步删除可能命中自身事件/信号发射帧导致 UAF。
 */
#define XTelnetClient_deleteLater XObject_deleteLater

/**
 * @brief 绑定已连接的字节流设备，并接管其 readyRead 驱动。
 * @param self Telnet 客户端；不能为 NULL。
 * @param device 已连接的字节流 XIODevice；借用，不取得所有权；不能为 NULL。
 * @return 绑定成功或重复绑定同一设备返回 true；self 或 device 为空返回
 *         false，此时对象保持不变。
 * @note 绑定会先停止旧连接，再把解析结果经 dataReceived 信号交给宿主。
 */
bool XTelnetClient_setDevice(XTelnetClient* self, XIODevice* device);
/**
 * @brief 发送基础协商序列并启动客户端。
 * @param self Telnet 客户端；不能为 NULL。
 * @return 协商发送并刷新成功返回 true；未绑定设备、已关闭或参数为空
 *         返回 false；发送失败时置为已关闭并发出 errorOccurred 信号。
 */
bool XTelnetClient_start(XTelnetClient* self);
/**
 * @brief 停止协议栈并断开 readyRead 连接；不关闭借用的设备。
 * @param self Telnet 客户端；可为 NULL。
 * @return 无。首次停止时发出 closed_signal 信号。
 */
void XTelnetClient_stop(XTelnetClient* self);
/**
 * @brief 投入收到的 Telnet 字节。
 * @param self Telnet 客户端；不能为 NULL。
 * @param data 收到的字节流；借用，不会取得所有权；NULL 且 size 为 0 时无效。
 * @param size 字节数。
 * @return 解析成功返回 XProtocolResult_Ok；self 为空或 data 与 size 不匹配
 *         返回 InvalidArgument；已关闭返回 Failed；设备写失败返回 IoError。
 */
XProtocolResult XTelnetClient_feedData(XTelnetClient* self,
                                       const void* data, size_t size);
/**
 * @brief 发送用户数据，自动执行 LF->CRLF 与 IAC 转义。
 * @param self Telnet 客户端；不能为 NULL。
 * @param data 待发送数据；借用，不会取得所有权；NULL 且 size 为 0 时无效。
 * @param size 字节数。
 * @return 已写入字节数；参数非法、已关闭或设备写失败返回 -1。
 */
int64_t XTelnetClient_write(XTelnetClient* self, const void* data, size_t size);
/**
 * @brief 刷新底层设备。
 * @param self Telnet 客户端；不能为 NULL。
 * @return 刷新成功返回 true；未绑定设备、已关闭或参数为空返回 false。
 */
bool XTelnetClient_flush(XTelnetClient* self);
/**
 * @brief 查询客户端是否已经关闭。
 * @param self Telnet 客户端；可为 NULL。
 * @return 已关闭或 self 为 NULL 返回 true。
 */
bool XTelnetClient_isClosed(const XTelnetClient* self);
/**
 * @brief 查询当前是否应由本地终端回显用户输入。
 * @param self Telnet 客户端；可为 NULL。
 * @return 由本地终端回显返回 true；服务端回显或 self 为 NULL 返回 false。
 */
bool XTelnetClient_localEchoEnabled(const XTelnetClient* self);

/**
 * @brief 收到远端应用数据信号。
 * @param self Telnet 客户端。
 * @param data 应用数据；借用，仅槽函数调用期间有效。
 * @param size 字节数。
 * @return 信号返回值。
 */
void* XTelnetClient_dataReceived_signal(XTelnetClient* self,
                                        const void* data, size_t size);
/**
 * @brief 协议栈关闭信号。
 * @param self Telnet 客户端。
 * @return 信号返回值。
 */
void* XTelnetClient_closed_signal(XTelnetClient* self);
/**
 * @brief 协议或传输错误信号。
 * @param self Telnet 客户端。
 * @param error 错误码，取 XProtocolResult 枚举值。
 * @return 信号返回值。
 */
void* XTelnetClient_errorOccurred_signal(XTelnetClient* self, int error);

#endif /* XPROTOCOL_ON && XTELNET_ON && XTELNET_CLIENT_ON */

#ifdef __cplusplus
}
#endif


/* XClass create API default-memory wrappers. */
#undef XTelnetClient_create
#define XTelnetClient_create() XTelnetClient_create_ex(XCLASS_DEFAULT_MEMORY_TYPE)

#endif /* XTELNETCLIENT_H */
