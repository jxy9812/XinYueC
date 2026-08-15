/** @file XTelnetServer.h
 * @brief Telnet 服务器协议栈：XTelnetServer。
 * @details
 * 移植自 XConsoleShell 的轻量 Telnet 字节过滤适配器，改为独立协议栈，
 * 继承 XObject 获得信号与槽能力，内部包含 XIODevice 作为数据来源，
 * 可接入 TCP/串口等任何字节流设备，并兼容原 Telnet NVT/协商逻辑。
 *
 * 处理 IAC 转义、DO/DONT/WILL/WONT 协商、子协商跳过和 CR-NUL 规则，
 * 输入字节经 bytesReceived_signal 交给宿主，宿主可通过 setter 回填查询结果，
 * 输出/回显/提示符等操作通过公共方法 write/flush/
 * setInputEcho/emitPrompt 完成。
 *
 * @note 使用前必须先调用 setDevice/setHostContext 完成绑定，
 *       协议投入使用前再调用 start 启动状态机。
 *
 * 文件编码：UTF-8 带 BOM，中文注释。
 */

#ifndef XTELNETSERVER_H
#define XTELNETSERVER_H

#include "XTelnet_config.h"

#ifdef __cplusplus
extern "C" {
#endif

#if XPROTOCOL_ON && XTELNET_ON && XTELNET_SERVER_ON

#include "XObject.h"
#include "XIODevice.h"
#include "XProtocolIo.h"
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

XCLASS_DEFINE_BEGING(XTelnetServer)
XCLASS_DEFINE_EXTEND_END(XTelnetServer, XObject)

/** @brief Telnet 输入状态；仅协议栈内部状态机使用。 */
typedef enum XTelnetServerState {
    XTelnetServerState_Data = 0,        /**< 普通文本数据。 */
    XTelnetServerState_Iac,             /**< 已收到 IAC。 */
    XTelnetServerState_Option,          /**< 等待协商选项号。 */
    XTelnetServerState_Subnegotiation,  /**< 跳过子协商负载。 */
    XTelnetServerState_SubnegotiationIac /**< 子协商中的 IAC。 */
} XTelnetServerState;

/** @brief Telnet 服务器协议栈。 */
typedef struct XTelnetServer {
    XObject m_class;                    /**< 基类 XObject，提供信号槽机制。 */
    XIODevice* m_device;                /**< 借用的 XIODevice 字节流设备。 */
    void* m_hostContext;                /**< 借用宿主上下文，供槽函数使用。 */
    XConnection* m_readyRead;           /**< 设备 readyRead 信号连接。 */
    XTelnetServerState m_state;         /**< 输入协议状态。 */
    uint8_t m_negotiation;              /**< 等待选项号的协商命令。 */
    /* 输入解析与回显状态；用位域压缩。 */
    struct {
        uint32_t m_afterCarriageReturn : 1; /**< 用于吞掉 CR 后的 NUL。 */
        uint32_t m_echoEnabled : 1;         /**< 服务端是否回显普通输入；密码输入时由 Shell 关闭。 */
        uint32_t m_echoPendingCr : 1;       /**< 已回显 CR，等待吞掉 LF 以避免重复。 */
        uint32_t m_echoEscape : 2;          /**< 回显转义状态：0 普通、1 ESC、2 CSI/SS3。 */
        uint32_t m_closed : 1;              /**< 是否已进入关闭状态。 */
    };
    /* 查询请求槽结果；同一时刻仅一个查询在途，故用联合压缩，布尔结果用位域。 */
    union {
        int m_bytesReceivedResult;      /**< bytesReceived 信号处理结果。 */
        uint32_t m_isRunningResult : 1; /**< isRunning 查询结果。 */
        uint32_t m_closeRequestedResult : 1; /**< closeRequested 查询结果。 */
        uint32_t m_suppressPromptResult : 1; /**< suppressPrompt 查询结果。 */
        uint32_t m_canBackspaceResult : 1; /**< 当前输入行是否允许退格。 */
        struct {
            char m_userNameResult[XTELNET_LOGIN_NAME_SIZE]; /**< userName 查询结果。 */
            size_t m_userNameResultLen; /**< userName 查询结果长度。 */
        };
    };
} XTelnetServer;

/** @brief 初始化 Telnet 服务器虚表。 */
XVtable* XTelnetServer_class_init(void);
/** @brief 初始化 Telnet 服务器对象。 */
void XTelnetServer_init(XTelnetServer* self);
/** @brief 创建 Telnet 服务器；销毁时使用 XTelnetServer_delete_base 释放。 */
XTelnetServer* XTelnetServer_create_ex(XMemoryType memory);
#define XTelnetServer_deinit_base XClass_deinit_base
#define XTelnetServer_delete_base XClass_delete_base
#define XTelnetServer_deleteLater XObject_deleteLater

/**
 * @brief 绑定底层字节流设备。
 * @param self Telnet 服务器；不能为 NULL。
 * @param device 字节流 XIODevice；不能为 NULL。
 * @return 绑定成功返回 true；device 为空返回 false。
 * @note 绑定过程会接管设备并连接 readyRead 信号。
 */
bool XTelnetServer_setDevice(XTelnetServer* self, XIODevice* device);

/**
 * @brief 绑定宿主上下文。
 * @param self Telnet 服务器；不能为 NULL。
 * @param context 宿主上下文指针；不能为 NULL。
 * @note 槽函数通过 server->m_hostContext 获取宿主绑定。
 */
void XTelnetServer_setHostContext(XTelnetServer* self, void* context);

/**
 * @brief 启动协议栈并等待输入。
 * @param self Telnet 服务器；不能为 NULL。
 * @return 启动成功返回 true。
 */
bool XTelnetServer_start(XTelnetServer* self);

/**
 * @brief 停止协议栈并发送关闭信号。
 * @param self Telnet 服务器；可为 NULL。
 */
void XTelnetServer_stop(XTelnetServer* self);

/**
 * @brief 投入一段字节到 Telnet 协议状态机。
 * @param self Telnet 服务器；不能为 NULL。
 * @param data 收到的字节流；NULL 且 size 为 0 时无效。
 * @param size 字节数。
 * @return 处理结果，出错时返回错误码。
 */
XProtocolResult XTelnetServer_feedData(XTelnetServer* self, const void* data, size_t size);

/**
 * @brief 向设备写入 Shell 输出，将 LF 规范化为 CR/LF 并转义 IAC。
 * @param self Telnet 服务器；不能为 NULL。
 * @param data 输出数据；NULL 且 size 为 0 时无效。
 * @param size 字节数。
 * @return 已写入字节数；出错返回 -1。
 */
int64_t XTelnetServer_write(XTelnetServer* self, const void* data, size_t size);

/**
 * @brief 刷新底层设备待写缓冲。
 * @param self Telnet 服务器；不能为 NULL。
 * @return 成功返回 true。
 */
bool XTelnetServer_flush(XTelnetServer* self);

/**
 * @brief 查询协议栈是否已关闭。
 * @param self Telnet 服务器；可为 NULL。
 * @return 已关闭或参数为空返回 true。
 */
bool XTelnetServer_isClosed(const XTelnetServer* self);

/**
 * @brief 设置输入回显开关。
 * @param self Telnet 服务器；不能为 NULL。
 * @param enabled true 开启回显；false 关闭回显。
 * @return 设置成功返回 true。
 */
bool XTelnetServer_setInputEcho(XTelnetServer* self, bool enabled);

/**
 * @brief 向设备发送当前会话提示符。
 * @param self Telnet 服务器；不能为 NULL。
 * @return 写入成功返回 true。
 */
bool XTelnetServer_emitPrompt(XTelnetServer* self);

/* ==================== 信号定义 -> 宿主 ==================== */

/**
 * @brief 输入字节信号。
 * @param self Telnet 服务器。
 * @param data 输入字节缓冲区。
 * @param size 字节数。
 * @return 信号返回值。
 * @note 槽函数处理后应调用 XTelnetServer_setBytesReceivedResult 回填，
 *       默认结果视为 Ok。
 */
void* XTelnetServer_bytesReceived_signal(XTelnetServer* self,
                                         const void* data, size_t size);
/**
 * @brief 查询是否运行中的信号；槽函数调用 setIsRunningResult 回填。
 * @param self Telnet 服务器。
 * @return 信号返回值。
 */
void* XTelnetServer_isRunningRequested_signal(XTelnetServer* self);
/**
 * @brief 查询是否请求关闭的信号；槽函数调用 setCloseRequestedResult 回填。
 * @param self Telnet 服务器。
 * @return 信号返回值。
 */
void* XTelnetServer_closeRequested_signal(XTelnetServer* self);
/**
 * @brief 查询是否抑制提示符的信号；槽函数调用 setSuppressPromptResult 回填。
 * @param self Telnet 服务器。
 * @return 信号返回值。
 */
void* XTelnetServer_suppressPromptRequested_signal(XTelnetServer* self);
/**
 * @brief 查询当前输入行是否允许退格；槽函数调用 setCanBackspaceResult 回填。
 * @param self Telnet 服务器。
 * @return 信号返回值。
 */
void* XTelnetServer_canBackspaceRequested_signal(XTelnetServer* self);
/**
 * @brief 查询用户名的信号；槽函数调用 setUserNameResult 回填。
 * @param self Telnet 服务器。
 * @param buffer 输出缓冲区，具有 capacity 容量。
 * @param capacity 缓冲区容量。
 * @return 信号返回值。
 */
void* XTelnetServer_userNameRequested_signal(XTelnetServer* self,
                                             char* buffer, size_t capacity);
/**
 * @brief 协议栈已关闭信号。
 * @param self Telnet 服务器。
 * @return 信号返回值。
 */
void* XTelnetServer_closed_signal(XTelnetServer* self);
/**
 * @brief 错误信号。
 * @param self Telnet 服务器。
 * @param error 错误码，取 XProtocolResult 枚举值。
 * @return 信号返回值。
 */
void* XTelnetServer_errorOccurred_signal(XTelnetServer* self, int error);

/* ==================== setter 回填 -> 协议栈 ==================== */

/** @brief 回填 bytesReceived 结果 @param self Telnet 服务器 @param result 处理结果 */
void XTelnetServer_setBytesReceivedResult(XTelnetServer* self, int result);
/** @brief 回填 isRunning 结果 @param self Telnet 服务器 @param result 是否运行 */
void XTelnetServer_setIsRunningResult(XTelnetServer* self, bool result);
/** @brief 回填 closeRequested 结果 @param self Telnet 服务器 @param result 是否请求关闭 */
void XTelnetServer_setCloseRequestedResult(XTelnetServer* self, bool result);
/** @brief 回填 suppressPrompt 结果 @param self Telnet 服务器 @param result 是否抑制提示符 */
void XTelnetServer_setSuppressPromptResult(XTelnetServer* self, bool result);
/** @brief 回填 canBackspace 结果 @param self Telnet 服务器 @param result 是否允许退格 */
void XTelnetServer_setCanBackspaceResult(XTelnetServer* self, bool result);
/** @brief 回填 userName 结果 @param self Telnet 服务器 @param name 用户名；可为 NULL @param len 长度（不含 NUL） */
void XTelnetServer_setUserNameResult(XTelnetServer* self,
                                     const char* name, size_t len);

#endif /* XPROTOCOL_ON && XTELNET_ON && XTELNET_SERVER_ON */

#ifdef __cplusplus
}
#endif


/* XClass create API default-memory wrappers. */
#undef XTelnetServer_create
#define XTelnetServer_create() XTelnetServer_create_ex(XCLASS_DEFAULT_MEMORY_TYPE)

#endif /* XTELNETSERVER_H */
