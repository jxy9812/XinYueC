/**
 * @file XConsoleShell.h
 * @brief 轻量、可裁剪的嵌入式控制台 Shell 公共 API。
 * @details
 * XConsoleShell 是 XObject 派生对象，使用固定行缓冲和固定命令指针表，
 * 支持 UART、USB CDC、RTT、TCP 或自定义传输的回调接入。Shell 不调用平台
 * 文件、进程、终端或网络接口；文件命令通过 XFileSystem，外部进程仅在
 * XCONSOLE_SHELL_EXTERNAL_PROCESS_ON 且 XProcess_ON 时通过 XProcess 公共 API。
 * 对象默认非线程安全，userData、命令字符串和静态命令表均由调用方拥有。
 */

#ifndef XCONSOLE_SHELL_H
#define XCONSOLE_SHELL_H

#include "XConsoleShellConfig.h"

#if XCONSOLE_SHELL_ON && XCONSOLE_SHELL_COMMAND_ON && XCONSOLE_SHELL_IO_ON

#include "XObject.h"
#include "XConsoleShellCommand.h"
#include "XConsoleShellIo.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** @brief 单个 Shell 会话的目录、权限和取消状态；Shell 内嵌拥有。 */
typedef struct XConsoleShellSession {
    uint32_t id;                                      /**< 会话标识。 */
    char currentPath[XCONSOLE_SHELL_MAX_PATH];       /**< 逻辑当前目录。 */
    uint32_t permissionMask;                         /**< 产品定义权限位。 */
    void* userData;                                   /**< 会话私有数据；不释放。 */
    bool authenticated;                               /**< 是否已通过产品认证。 */
    bool cancelled;                                   /**< 当前命令是否取消。 */
#if XCONSOLE_SHELL_MULTI_SESSION_ON
    XConsoleShellIo m_io;                             /**< 会话借用的传输回调集合。 */
    char m_lineBuffer[XCONSOLE_SHELL_LINE_BUFFER_SIZE]; /**< 会话独立输入行缓冲。 */
    size_t m_lineLength;                              /**< 会话当前输入长度。 */
#if XCONSOLE_SHELL_HISTORY_ON
    char m_history[XCONSOLE_SHELL_HISTORY_CAPACITY][XCONSOLE_SHELL_LINE_BUFFER_SIZE]; /**< 会话历史环形缓冲。 */
    size_t m_historyCount;                            /**< 会话历史条目数。 */
    size_t m_historyNext;                             /**< 会话下一条历史写入位置。 */
    size_t m_historyCursor;                           /**< 会话历史浏览位置。 */
#endif
#if XCONSOLE_SHELL_LINE_EDITOR_ON
    size_t m_lineCursor;                              /**< 会话行编辑光标位置。 */
    uint8_t m_escapeState;                            /**< 会话 ANSI 转义解析状态。 */
#endif
    bool m_open;                                      /**< 是否为已打开的附加会话。 */
    bool m_discardLine;                               /**< 当前输入行已超长，直到换行前丢弃。 */
#endif
} XConsoleShellSession;

#if XCONSOLE_SHELL_STATS_ON
/** @brief Shell 运行统计快照；所有字段均为当前对象的只读副本。 */
typedef struct XConsoleShellStats {
    uint64_t processedLines;       /**< 已处理的完整输入行数。 */
    uint64_t successfulCommands;   /**< 返回成功结果的命令数。 */
    uint64_t failedCommands;       /**< 返回失败结果的命令数。 */
    uint64_t inputBytes;           /**< 通过 feedByte/feedData 接收的字节数。 */
    uint64_t outputBytes;          /**< 通过 Shell 输出回调提交的字节数。 */
    size_t registeredCommands;     /**< 当前注册的根命令数。 */
} XConsoleShellStats;
#endif

/** @brief XConsoleShell 虚函数表布局；当前只重载 XObject 析构槽。 */
XCLASS_DEFINE_BEGING(XConsoleShell)
XCLASS_DEFINE_EXTEND_END(XConsoleShell, XObject)

/** @brief XConsoleShell 对象；首成员必须为 XObject，成员不得手工改写。 */
struct XConsoleShellDynamicCommand;
struct XConsoleShellAsyncProcess;
typedef struct XConsoleShell {
    XObject base;                                     /**< XObject 基类成员。 */
    XConsoleShellIo m_io;                             /**< 借用的传输回调。 */
    const XConsoleCommand* m_commands[XCONSOLE_SHELL_COMMAND_CAPACITY]; /**< 命令指针表。 */
    size_t m_commandCount;                            /**< 当前命令数量。 */
#if XCONSOLE_SHELL_DYNAMIC_REGISTER_ON
    struct XConsoleShellDynamicCommand*
        m_dynamicCommands[XCONSOLE_SHELL_DYNAMIC_COMMAND_CAPACITY]; /**< Shell 拥有的动态命令根节点。 */
    size_t m_dynamicCommandCount;                     /**< 已注册动态命令数量。 */
    struct XConsoleShellDynamicCommand*
        m_retiredDynamicCommands;                     /**< 执行期间注销、等待安全释放的命令链。 */
    size_t m_commandExecutionDepth;                  /**< 当前命令回调嵌套深度。 */
#endif
    XConsoleShellSession m_session;                   /**< 默认会话状态。 */
#if XCONSOLE_SHELL_MULTI_SESSION_ON
    XConsoleShellSession m_sessions[XCONSOLE_SHELL_MAX_SESSIONS - 1u]; /**< 固定附加会话槽位。 */
    size_t m_sessionCount;                            /**< 包含默认会话的已打开会话数量。 */
    uint32_t m_nextSessionId;                         /**< 下一个附加会话的单调 ID，避免复用旧任务目标。 */
#endif
#if XCONSOLE_SHELL_PROCESS_ASYNC_ON
    struct XConsoleShellAsyncProcess*
        m_asyncProcesses[XCONSOLE_SHELL_ASYNC_PROCESS_CAPACITY]; /**< Executor 拥有的异步任务槽。 */
#endif
    char m_lineBuffer[XCONSOLE_SHELL_LINE_BUFFER_SIZE]; /**< tokenizer 工作缓冲。 */
    const char* m_arguments[XCONSOLE_SHELL_MAX_ARGUMENTS]; /**< 借用 token 指针。 */
    size_t m_lineLength;                              /**< 当前输入行长度。 */
    size_t m_argumentCount;                           /**< 当前 token 数量。 */
    bool m_discardLine;                               /**< 当前输入行已超长，直到换行前丢弃。 */
#if XCONSOLE_SHELL_HISTORY_ON
    char m_history[XCONSOLE_SHELL_HISTORY_CAPACITY][XCONSOLE_SHELL_LINE_BUFFER_SIZE]; /**< 固定历史环形缓冲。 */
    size_t m_historyCount;                            /**< 当前保存的历史条目数。 */
    size_t m_historyNext;                             /**< 下一条历史写入位置。 */
    size_t m_historyCursor;                           /**< 行编辑历史浏览位置。 */
#endif
#if XCONSOLE_SHELL_LINE_EDITOR_ON
    size_t m_lineCursor;                               /**< 行编辑光标位置。 */
    uint8_t m_escapeState;                             /**< ANSI 转义序列解析状态。 */
#endif
#if XCONSOLE_SHELL_STATS_ON
    uint64_t m_processedLines;                         /**< 已处理完整输入行数。 */
    uint64_t m_successfulCommands;                     /**< 成功命令数。 */
    uint64_t m_failedCommands;                         /**< 失败命令数。 */
    uint64_t m_inputBytes;                             /**< 输入字节数。 */
    uint64_t m_outputBytes;                            /**< 输出字节数。 */
#endif
#if XCONSOLE_SHELL_SCRIPT_ON
    size_t m_scriptDepth;                              /**< source 嵌套深度。 */
#endif
#if XCONSOLE_SHELL_ASYNC_OUTPUT_ON
    uint8_t m_asyncOutput[XCONSOLE_SHELL_ASYNC_OUTPUT_CAPACITY]; /**< 固定异步输出环形缓冲。 */
    size_t m_asyncOutputHead;                           /**< 异步输出读取位置。 */
    size_t m_asyncOutputTail;                           /**< 异步输出写入位置。 */
    size_t m_asyncOutputSize;                           /**< 异步输出待发送字节数。 */
#endif
    bool m_running;                                   /**< 是否接受输入。 */
} XConsoleShell;

/**
 * @brief 初始化 XConsoleShell 虚函数表。
 * @return 全局只读虚函数表；初始化失败返回 NULL。
 */
XVtable* XConsoleShell_class_init(void);
/**
 * @brief 初始化栈上 Shell。
 * @param self 待初始化对象；不能为空。
 * @param io 传输回调集合；只在调用期间读取，NULL 表示无输入输出设备。
 */
void XConsoleShell_init(XConsoleShell* self, const XConsoleShellIo* io);
/**
 * @brief 创建堆上 Shell。
 * @param io 传输回调集合；Shell 不取得其 userData 所有权，可为 NULL。
 * @return 新对象；分配失败返回 NULL，调用方必须调用 XConsoleShell_delete_base。
 */
XConsoleShell* XConsoleShell_create(const XConsoleShellIo* io);
/** @brief 释放栈对象内部资源但保留对象存储；self 可为 NULL。 */
void XConsoleShell_deinit_base(XConsoleShell* self);
/** @brief 删除由 create 返回的堆对象；self 可为 NULL。 */
void XConsoleShell_delete_base(XConsoleShell* self);

/**
 * @brief 注册一组静态根命令。
 * @param self Shell 对象；不能为空。
 * @param commands 静态命令数组；Shell 只保存指针，必须持续有效。
 * @param count 数组元素数量；不能超过 XCONSOLE_SHELL_COMMAND_CAPACITY。
 * @return 全部注册成功返回 true；参数无效、重复或容量不足返回 false。
 */
bool XConsoleShell_registerStaticCommands(XConsoleShell* self,
                                          const XConsoleCommand* commands,
                                          size_t count);
/**
 * @brief 按指针序列注销静态根命令。
 * @param self Shell 对象；不能为空。
 * @param commands 与注册时相同的静态命令数组。
 * @param count 数组元素数量。
 * @return 全部找到并注销返回 true，否则返回 false。
 */
bool XConsoleShell_unregisterStaticCommands(XConsoleShell* self,
                                            const XConsoleCommand* commands,
                                            size_t count);
#if XCONSOLE_SHELL_DYNAMIC_REGISTER_ON
/**
 * @brief 深复制注册一个动态根命令。
 * @param self Shell 对象；不能为空。
 * @param command 源命令描述；名称、别名和说明会复制，回调与 userData 借用。
 * @return 注册成功返回 true；参数无效、重复或容量不足返回 false。
 */
bool XConsoleShell_registerCommand(XConsoleShell* self,
                                   const XConsoleCommand* command);
/**
 * @brief 按主名称注销动态命令。
 * @param self Shell 对象；不能为空。
 * @param name UTF-8 主命令名；只在调用期间借用。
 * @return 找到并注销返回 true；命令不存在或参数无效返回 false。
 */
bool XConsoleShell_unregisterCommand(XConsoleShell* self, const char* name);
#endif
/**
 * @brief 查找根命令或子命令路径。
 * @param self Shell 对象；不能为空。
 * @param path 以空格分隔的 UTF-8 命令路径；只在调用期间借用。
 * @param result 输出找到的静态或动态命令借用指针，可为 NULL。
 * @return 找到命令返回 true；路径无效或命令不存在返回 false。
 */
bool XConsoleShell_findCommand(const XConsoleShell* self,
                               const char* path,
                               const XConsoleCommand** result);

/**
 * @brief 输入一个字节。
 * @param self Shell 对象；不能为空。
 * @param byte 输入字节；CR/LF 提交当前行，Ctrl-C 按配置设置取消标志。
 * @return 字节处理结果；成功、语法错误或 I/O 错误由结果码表示。
 */
XConsoleResult XConsoleShell_feedByte(XConsoleShell* self, uint8_t byte);
/**
 * @brief 输入一段连续字节。
 * @param self Shell 对象；不能为空。
 * @param data 输入缓冲；size 非零时不能为空，只在调用期间借用。
 * @param size 输入字节数。
 * @return 最后一个字节对应的结果；空输入返回 XConsoleResult_Ok。
 */
XConsoleResult XConsoleShell_feedData(XConsoleShell* self,
                                      const void* data, size_t size);
/**
 * @brief 从默认会话 read 回调读取并处理一批输入。
 * @param self Shell 对象；不能为空且必须配置 read 回调。
 * @param maxBytes 本轮最多读取的字节数；0 表示使用实现默认上限。
 * @return 本轮最后一字节的结果；无数据时返回 XConsoleResult_Ok。
 */
XConsoleResult XConsoleShell_pump(XConsoleShell* self, size_t maxBytes);
/**
 * @brief 直接解析并执行一行。
 * @param self Shell 对象；不能为空。
 * @param line UTF-8 行内容，可不含换行符，只在调用期间借用。
 * @param length 行字节数；不能超过 XCONSOLE_SHELL_LINE_BUFFER_SIZE - 1。
 * @return 命令处理结果码。
 */
XConsoleResult XConsoleShell_processLine(XConsoleShell* self,
                                         const char* line, size_t length);
/** @param self Shell 对象；不能为空。 @param running 是否接受新的输入字节。 */
void XConsoleShell_setRunning(XConsoleShell* self, bool running);
/** @param self Shell 对象；不能为空。 @return 默认会话只读借用指针。 */
const XConsoleShellSession* XConsoleShell_session_const(const XConsoleShell* self);
/** @param self Shell 对象；不能为空。 @return 默认会话可写借用指针，不得释放。 */
XConsoleShellSession* XConsoleShell_session(XConsoleShell* self);
/** @param self Shell 对象；不能为空。 @param authenticated 产品层认证是否通过。 */
void XConsoleShell_setAuthenticated(XConsoleShell* self, bool authenticated);

#if XCONSOLE_SHELL_MULTI_SESSION_ON
/**
 * @brief 打开一个附加会话。
 * @param self 启用多会话的 Shell；不能为空。
 * @param io 新会话传输回调；由 Shell 借用，不复制 userData。
 * @return 新会话借用指针；槽位不足或参数无效返回 NULL。
 */
XConsoleShellSession* XConsoleShell_openSession(XConsoleShell* self,
                                                 const XConsoleShellIo* io);
/**
 * @brief 关闭一个附加会话。
 * @param self Shell 对象；不能为空。
 * @param session 由 openSession 返回的附加会话；默认会话不能关闭。
 * @return 成功关闭返回 true；会话不属于 self 或已关闭返回 false。
 */
bool XConsoleShell_closeSession(XConsoleShell* self, XConsoleShellSession* session);
/** @param self Shell 对象；可为 NULL。 @return 当前打开会话数量，包含默认会话。 */
size_t XConsoleShell_sessionCount(const XConsoleShell* self);
/** @param self Shell 对象；不能为空。 @param id 稳定会话 ID。 @return 会话借用指针，未找到返回 NULL。 */
XConsoleShellSession* XConsoleShell_findSession(XConsoleShell* self, uint32_t id);
/**
 * @brief 向指定会话输入一个字节。
 * @param self Shell 对象；不能为空。
 * @param session self 管理的附加会话。
 * @param byte 输入字节；行为与默认会话 feedByte 相同。
 * @return 字节处理结果码。
 */
XConsoleResult XConsoleShell_feedByteForSession(XConsoleShell* self,
                                                XConsoleShellSession* session,
                                                uint8_t byte);
/**
 * @brief 向指定会话输入连续字节。
 * @param self Shell 对象；不能为空。
 * @param session self 管理的附加会话。
 * @param data 输入缓冲；只在调用期间借用。
 * @param size 输入字节数。
 * @return 最后一个字节对应的结果码。
 */
XConsoleResult XConsoleShell_feedDataForSession(XConsoleShell* self,
                                                XConsoleShellSession* session,
                                                const void* data, size_t size);
/**
 * @brief 从指定会话的 read 回调读取并处理一批输入。
 * @param self Shell 对象；不能为空。
 * @param session self 管理的附加会话。
 * @param maxBytes 本轮最多读取的字节数。
 * @return 本轮最后一个字节的结果码；暂无数据返回成功。
 */
XConsoleResult XConsoleShell_pumpForSession(XConsoleShell* self,
                                            XConsoleShellSession* session,
                                            size_t maxBytes);
/**
 * @brief 在指定会话上下文中解析并执行一行。
 * @param self Shell 对象；不能为空。
 * @param session self 管理的附加会话。
 * @param line UTF-8 行内容，只在调用期间借用。
 * @param length 行字节数，不含结尾 NUL。
 * @return 命令处理结果码。
 */
XConsoleResult XConsoleShell_processLineForSession(XConsoleShell* self,
                                                   XConsoleShellSession* session,
                                                   const char* line, size_t length);
/**
 * @brief 向指定会话输出二进制数据。
 * @param self Shell 对象；不能为空。
 * @param session self 管理的会话。
 * @param data 输出缓冲；size 非零时不能为空，只在调用期间借用。
 * @param size 输出字节数。
 * @return 全部数据已提交或排入队列返回 true。
 */
bool XConsoleShell_writeForSession(XConsoleShell* self, XConsoleShellSession* session,
                                   const void* data, size_t size);
#endif

#if XCONSOLE_SHELL_PROCESS_ASYNC_ON
/**
 * @brief 驱动所有异步 exec 任务。
 * @param self Shell 对象；不能为空。
 * @param timeoutMsecs 单次进程轮询等待毫秒数，负数表示后端默认等待。
 * @return 本轮已完成并回收的任务数量。
 */
size_t XConsoleShell_pollProcesses(XConsoleShell* self, int timeoutMsecs);
#endif

#if XCONSOLE_SHELL_HISTORY_ON
/** @param self Shell 对象；可为 NULL。 @return 当前保存的历史条目数量。 */
size_t XConsoleShell_historyCount(const XConsoleShell* self);
/** @param self Shell 对象；不能为空。 @param index 从最旧到最新的索引。 @return 借用文本，越界返回 NULL。 */
const char* XConsoleShell_historyAt(const XConsoleShell* self, size_t index);
/** @param self Shell 对象；不能为空。 @brief 清空当前 Shell 的历史缓冲。 */
void XConsoleShell_clearHistory(XConsoleShell* self);
#endif

#if XCONSOLE_SHELL_STATS_ON
/**
 * @brief 获取当前 Shell 的统计快照。
 * @param self Shell 对象；不能为空。
 * @param stats 输出统计结构；不能为空，由调用方提供存储。
 * @return 成功复制返回 true；参数无效返回 false。
 */
bool XConsoleShell_stats(const XConsoleShell* self, XConsoleShellStats* stats);
/** @param self Shell 对象；不能为空。 @brief 清零运行统计，不影响命令表和历史。 */
void XConsoleShell_clearStats(XConsoleShell* self);
#endif

/**
 * @brief 以短写重试方式输出二进制数据。
 * @param self Shell 对象；不能为空。
 * @param data 输出缓冲；size 非零时不能为空，只在调用期间借用。
 * @param size 输出字节数。
 * @return 全部数据写出或排队返回 true；回调失败返回 false。
 */
bool XConsoleShell_write(XConsoleShell* self, const void* data, size_t size);
/** @param self Shell 对象；不能为空。 @param text UTF-8 文本，只在调用期间借用。 @return 输出成功返回 true。 */
bool XConsoleShell_writeUtf8(XConsoleShell* self, const char* text);
#if XCONSOLE_SHELL_ASYNC_OUTPUT_ON
/** @param self Shell 对象；不能为空。 @return 队列已清空或无待发数据返回 true。 */
bool XConsoleShell_flushOutput(XConsoleShell* self);
#endif
/** @param self Shell 对象；不能为空。 @param result 错误结果码。 @param detail 可选 UTF-8 详细信息。 @return 输出成功返回 true。 */
bool XConsoleShell_writeError(XConsoleShell* self, XConsoleResult result,
                              const char* detail);
/** @param self Shell 对象；不能为空。 @return 当前会话取消标志或取消回调结果。 */
bool XConsoleShell_isCancelled(const XConsoleShell* self);
/** @param self Shell 对象；不能为空。 @brief 清除当前默认会话取消标志。 */
void XConsoleShell_clearCancelled(XConsoleShell* self);

#ifdef __cplusplus
}
#endif

#endif /* XCONSOLE_SHELL_ON && XCONSOLE_SHELL_COMMAND_ON && XCONSOLE_SHELL_IO_ON */
#endif /* XCONSOLE_SHELL_H */
