/**
 * @file       XConsoleShell.h
 * @brief      轻量、可裁剪的嵌入式控制台 Shell 公共 API。
 * @details
 * XConsoleShell 没有直接对应的 Qt 类；对象生命周期和虚函数表遵循本库
 * XObject 约定，命令组织方式参考嵌入式 Shell 的固定容量设计。
 *
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
#if XCONSOLE_SHELL_NETWORK_ON && XCONSOLE_SHELL_NET_PING_ON
#include "XConsoleShellNetwork.h"
#endif
#if XCONSOLE_SHELL_LOGIN_ON
#include "XConsoleShellLogin.h"
#endif
#if XCONSOLE_SHELL_TASKS_ON
#include "XConsoleShellTasks.h"
#endif
#if XCONSOLE_SHELL_GPIO_ON
#include "XConsoleShellGpio.h"
#endif
#if XCONSOLE_SHELL_I2C_ON
#include "XConsoleShellI2c.h"
#endif
#if XCONSOLE_SHELL_SPI_ON
#include "XConsoleShellSpi.h"
#endif
#if XCONSOLE_SHELL_CAN_ON
#include "XConsoleShellCan.h"
#endif
#if XCONSOLE_SHELL_ADC_ON
#include "XConsoleShellAdc.h"
#endif
#if XCONSOLE_SHELL_PWM_ON
#include "XConsoleShellPwm.h"
#endif
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#if XCONSOLE_SHELL_ASYNC_ON
#include "XAtomic.h"
#include "XEvent.h"
#include "XThread.h"
#endif

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 单个 Shell 会话的目录、权限和取消状态。
 * @details 会话存储由所属 XConsoleShell 内嵌拥有，调用者不得释放、移动或
 *          复制该结构。除非具体成员注释允许，调用者也不应直接修改成员。
 *          会话及其借用指针默认非线程安全，只能在所属 Shell 的执行线程使用。
 */
typedef struct XConsoleShellSession {
    uint32_t id;                                      /**< 会话标识；默认会话固定为 1，0 表示无效。 */
    char currentPath[XCONSOLE_SHELL_MAX_PATH];       /**< NUL 结尾 UTF-8 逻辑当前目录；最长为容量减 1 字节。 */
#if XCONSOLE_SHELL_FS_CD_ON
    char previousPath[XCONSOLE_SHELL_MAX_PATH];      /**< cd 切换前的逻辑当前目录，用于 cd -；首次为空串。 */
#endif
    uint32_t permissionMask;                         /**< 产品定义权限位；可由标准权限位按位组合。 */
    void* userData;                                   /**< 会话私有数据；不释放。 */
    bool authenticated;                               /**< 是否已通过产品认证。 */
    bool cancelled;                                   /**< 当前命令是否取消。 */
#if XCONSOLE_SHELL_REMOTE_OUTPUT_REDIRECT_ON
    XConsoleShellInputLineHandler inputLineHandler;   /**< 后续整行输入处理器；NULL 表示走普通命令解析。 */
    void* inputLineHandlerUserData;                   /**< 后续整行输入处理器上下文；Shell 不释放。 */
#endif
#if XCONSOLE_SHELL_LOGIN_ON
    char userName[XCONSOLE_SHELL_LOGIN_NAME_SIZE];    /**< 当前登录用户名；未登录时为空字符串。 */
    uint32_t uid;                                     /**< 当前账户 UID；未登录时为 UINT32_MAX。 */
    uint32_t gid;                                     /**< 当前账户主 GID；未登录时为 UINT32_MAX。 */
    uint32_t groups[XCONSOLE_SHELL_LOGIN_GROUP_CAPACITY]; /**< 当前账户附加组 ID。 */
    size_t groupCount;                                /**< groups 中有效元素数量。 */
    XConsoleShellLoginInputMode loginInputMode;       /**< 当前等待的登录或改密输入类型。 */
    char loginInputUser[XCONSOLE_SHELL_LOGIN_NAME_SIZE]; /**< 等待输入对应的目标用户名。 */
    char loginInputPassword[XCONSOLE_SHELL_LOGIN_PASSWORD_SIZE + 1u]; /**< 改密确认用的临时密码。 */
#endif
#if XCONSOLE_SHELL_LOGIN_ON || XCONSOLE_SHELL_EDITOR_ON
    bool suppressPrompt;                              /**< 是否暂时抑制普通 Shell 提示符。 */
#endif
#if XCONSOLE_SHELL_EDITOR_ON
    bool editorActive;                                /**< 当前会话是否处于 vi/vim 编辑状态。 */
    bool editorInsertMode;                            /**< 当前是否处于插入模式。 */
    bool editorModified;                              /**< 编辑缓冲相对磁盘是否已修改。 */
    bool editorInsertAfter;                           /**< 插入模式是否在当前行之后追加。 */
    char editorPath[XCONSOLE_SHELL_MAX_PATH];         /**< 编辑目标文件路径；NUL 结尾。 */
    char editorLines[XCONSOLE_SHELL_EDITOR_MAX_LINES][XCONSOLE_SHELL_LINE_BUFFER_SIZE]; /**< 编辑行缓冲。 */
    size_t editorLineCount;                           /**< 编辑缓冲有效行数。 */
    size_t editorCursorLine;                          /**< 插入模式插入位置所在行；0 起。 */
    size_t editorCursorColumn;                        /**< 插入模式插入位置所在列；0 起。 */
    char editorInsertBuf[XCONSOLE_SHELL_LINE_BUFFER_SIZE]; /**< 插入模式当前正在编辑的一行。 */
    size_t editorInsertLen;                           /**< 插入模式当前行的长度。 */
    size_t editorInsertCursor;                        /**< 插入模式当前行的光标列。 */
    bool editorInsertPendingCr;                       /**< 插入模式已收到 \r，等待合并其后的 \n。 */
    unsigned char editorInsertEscape;                 /**< 插入模式 ESC/方向键状态；0 无，1 收到 ESC，2 收到 ESC [。 */
    void* editorTui;                                  /**< XConsoleShellViTui 私有 TUI 会话；由编辑器拥有，Shell 不释放。 */
#endif
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
#endif
    uint8_t m_escapeState;                            /**< 会话 ANSI 转义状态：0 普通、1 ESC、2 CSI、3 SS3。 */
    bool m_closeRequested;                            /**< 附加会话已请求退出；所属 Shell 继续运行。 */
    bool m_open;                                      /**< 是否为已打开的附加会话。 */
    bool m_discardLine;                               /**< 当前输入行已超长，直到换行前丢弃。 */
    bool m_lastByteCR;                                /**< 上一个输入字节是否为 \r，用于合并 CRLF 的 \n，避免空行被提交。 */
#endif
} XConsoleShellSession;

#if XCONSOLE_SHELL_STATS_ON
/** @brief Shell 运行统计快照；所有字段均为获取时当前对象的只读副本。 */
typedef struct XConsoleShellStats {
    uint64_t processedLines;       /**< 已处理的完整输入行数。 */
    uint64_t successfulCommands;   /**< 返回成功结果的命令数。 */
    uint64_t failedCommands;       /**< 返回失败结果的命令数。 */
    uint64_t inputBytes;           /**< 通过 feedByte/feedData 接收的字节数。 */
    uint64_t outputBytes;          /**< 通过 Shell 输出回调提交的字节数。 */
    size_t registeredCommands;     /**< 当前注册的根命令数。 */
} XConsoleShellStats;
#endif

/** @brief XConsoleShell 虚函数表布局；异步模式下额外重载 XObject 事件槽。 */
XCLASS_DEFINE_BEGING(XConsoleShell)
#if XCONSOLE_SHELL_ASYNC_ON
XCLASS_DEFINE_ENUM(XConsoleShell, Event) = XCLASS_VTABLE_GET_SIZE(XObject),
#endif
XCLASS_DEFINE_EXTEND_END(XConsoleShell, XObject)

/** @brief 动态命令所有者的内部前置声明；仅供实现使用。 */
struct XConsoleShellDynamicCommand;
/** @brief 异步进程槽位的内部前置声明；仅供实现使用。 */
struct XConsoleShellAsyncProcess;
/**
 * @brief XConsoleShell 对象。
 * @details base 是第一个成员，由 XClass 管理，禁止手工修改。其余成员公开
 *          仅为保持 C 对象布局可见，均应通过本文件 API 操作。对象默认非
 *          线程安全，不支持按值复制或移动。
 */
typedef struct XConsoleShell {
    XObject base;                                     /**< 第一个成员，由 XClass 管理，禁止手工修改。 */
    XConsoleShellIo m_io;                             /**< 传输回调值副本；其中 userData 由调用者拥有。 */
    const XConsoleCommand* m_commands[XCONSOLE_SHELL_COMMAND_CAPACITY]; /**< 借用命令指针表；Shell 不释放静态项。 */
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
#if XCONSOLE_SHELL_LOGIN_ON
    char m_loginDatabasePath[XCONSOLE_SHELL_MAX_PATH]; /**< 用户 JSON 配置路径；Shell 自有文本副本。 */
#endif
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
    bool m_lastByteCR;                                /**< 上一个输入字节是否为 \r，用于合并 CRLF 的 \n，避免空行被提交。 */
#if XCONSOLE_SHELL_HISTORY_ON
    char m_history[XCONSOLE_SHELL_HISTORY_CAPACITY][XCONSOLE_SHELL_LINE_BUFFER_SIZE]; /**< 固定历史环形缓冲。 */
    size_t m_historyCount;                            /**< 当前保存的历史条目数。 */
    size_t m_historyNext;                             /**< 下一条历史写入位置。 */
    size_t m_historyCursor;                           /**< 行编辑历史浏览位置。 */
#endif
#if XCONSOLE_SHELL_LINE_EDITOR_ON
    size_t m_lineCursor;                               /**< 行编辑光标位置。 */
#endif
    uint8_t m_escapeState;                             /**< ANSI 转义状态：0 普通、1 ESC、2 CSI、3 SS3。 */
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
#if XCONSOLE_SHELL_UPTIME_ON
    int64_t m_startTimeMsecs;                         /**< Shell 初始化时的日历毫秒时间戳；无效时为负数。 */
#endif
#if XCONSOLE_SHELL_TASKS_ON
    XConsoleShellTaskProviderFn m_taskProvider;       /**< 产品任务快照提供者；由产品拥有。 */
    void* m_taskProviderUserData;                     /**< 提供者上下文；Shell 不释放。 */
#endif
#if XCONSOLE_SHELL_GPIO_ON
    XConsoleShellGpioSlot
        m_gpioSlots[XCONSOLE_SHELL_GPIO_SLOT_CAPACITY]; /**< Shell 拥有的固定 GPIO 槽位。 */
    XConsoleShellGpioAuthorizeFn m_gpioAuthorize;     /**< 产品引脚授权策略；Shell 不释放。 */
    void* m_gpioAuthorizeUserData;                    /**< 引脚策略上下文；Shell 不释放。 */
#endif
#if XCONSOLE_SHELL_I2C_ON
    XConsoleShellI2cSlot
        m_i2cSlots[XCONSOLE_SHELL_I2C_SLOT_CAPACITY]; /**< Shell 拥有的固定 I2C 槽位。 */
    XConsoleShellI2cAuthorizeFn m_i2cAuthorize;       /**< 产品 I2C 访问策略。 */
    void* m_i2cAuthorizeUserData;                      /**< I2C 策略上下文；Shell 不释放。 */
#endif
#if XCONSOLE_SHELL_SPI_ON
    XConsoleShellSpiSlot
        m_spiSlots[XCONSOLE_SHELL_SPI_SLOT_CAPACITY]; /**< Shell 拥有的固定 SPI 槽位。 */
    XConsoleShellSpiAuthorizeFn m_spiAuthorize;       /**< 产品 SPI 访问策略。 */
    void* m_spiAuthorizeUserData;                     /**< SPI 策略上下文；Shell 不释放。 */
#endif
#if XCONSOLE_SHELL_CAN_ON
    XConsoleShellCanSlot
        m_canSlots[XCONSOLE_SHELL_CAN_SLOT_CAPACITY]; /**< Shell 拥有的固定 CAN 槽位。 */
    XConsoleShellCanAuthorizeFn m_canAuthorize;       /**< CAN 授权策略；Shell 不释放。 */
    void* m_canAuthorizeUserData;                      /**< CAN 策略上下文；Shell 不释放。 */
#endif
#if XCONSOLE_SHELL_ADC_ON
    XConsoleShellAdcSlot
        m_adcSlots[XCONSOLE_SHELL_ADC_SLOT_CAPACITY]; /**< Shell 拥有的固定 ADC 槽位。 */
    XConsoleShellAdcAuthorizeFn m_adcAuthorize;       /**< ADC 授权策略；Shell 不释放。 */
    void* m_adcAuthorizeUserData;                     /**< ADC 策略上下文；Shell 不释放。 */
#endif
#if XCONSOLE_SHELL_PWM_ON
    XConsoleShellPwmSlot
        m_pwmSlots[XCONSOLE_SHELL_PWM_SLOT_CAPACITY]; /**< Shell 拥有的固定 PWM 槽位。 */
    XConsoleShellPwmAuthorizeFn m_pwmAuthorize;       /**< PWM 授权策略；Shell 不释放。 */
    void* m_pwmAuthorizeUserData;                     /**< PWM 策略上下文；Shell 不释放。 */
#endif
    bool m_running;                                   /**< 是否接受输入。 */
#if XCONSOLE_SHELL_ASYNC_ON
    XAtomic_bool m_asyncRunning;                      /**< 异步输入调度是否已启动。 */
    XAtomic_bool m_asyncInputPosted;                 /**< 是否已有输入事件在队列中。 */
    XEventType m_asyncEventType;                      /**< Shell 私有输入事件类型。 */
    XThread* m_asyncOwnerThread;                      /**< 事件模式下创建 Shell 的线程借用指针。 */
    XThread* m_asyncThread;                           /**< 线程模式下 Shell 独占的线程对象。 */
    XAtomic_bool m_asyncWorkerReady;                  /**< 线程模式下专用事件循环是否已就绪。 */
    size_t m_asyncLastReadBytes;                     /**< 最近一次 pump 读取的字节数。 */
    XTimerId m_asyncPollTimer;                       /**< 事件模式下非阻塞输入轮询定时器。 */
    XAtomic_bool m_asyncInputAttached;               /**< 是否已调用输入源附加回调。 */
    bool m_asyncInputEventDriven;                    /**< 输入源是否通过 I/O 事件环通知。 */
#endif
#if XCONSOLE_SHELL_NETWORK_ON && XCONSOLE_SHELL_NET_PING_ON
    XConsoleShellPingState m_ping;                    /**< 顶层 ping 异步状态。 */
    XTimerId m_pingTimer;                             /**< ping 定时器；无效时为 XTIMER_INVALID_ID。 */
#endif
} XConsoleShell;

/**
 * @brief      初始化并取得 XConsoleShell 虚函数表。
 * @return     进程内共享的虚函数表借用指针；调用者不得释放或修改；初始化
 *             失败返回 NULL。
 */
XVtable* XConsoleShell_class_init(void);
/**
 * @brief      初始化调用方提供存储的 Shell 对象。
 * @details    self 必须指向尚未初始化的存储；使用结束后必须调用
 *             XConsoleShell_deinit_base。内置命令注册失败时对象仍完成初始化，
 *             但会停止接收输入且命令表为空。
 * @param      self 待初始化对象；调用方提供可写存储；传入 NULL 时不执行操作。
 * @param      io 传输回调集合；只在调用期间读取并复制回调值，可为 NULL；
 *             其中 userData 始终由调用者管理。
 * @return     无。self 为 NULL 时不修改任何状态。
 */
void XConsoleShell_init(XConsoleShell* self, const XConsoleShellIo* io);
/**
 * @brief      创建并初始化堆上 Shell 对象。
 * @param      io 传输回调集合；只在调用期间读取并复制回调值，可为 NULL；
 *             Shell 不取得其中 userData 的所有权。
 * @return     新对象的拥有指针；分配失败返回 NULL。调用方必须使用
 *             XConsoleShell_delete_base 释放非 NULL 返回值。
 */
XConsoleShell* XConsoleShell_create_ex(XMemoryType memory,  const XConsoleShellIo* io);
/**
 * @brief      释放 Shell 内部资源但保留调用方提供的对象存储。
 * @param      self 已由 XConsoleShell_init 初始化的对象；可为 NULL；函数不
 *             释放 self 本身，非 NULL 对象不得重复反初始化。
 * @return     无。self 为 NULL 时不执行操作。
 */
void XConsoleShell_deinit_base(XConsoleShell* self);
/**
 * @brief      反初始化并删除由 XConsoleShell_create 返回的堆对象。
 * @param      self 待删除对象的拥有指针；可为 NULL；调用后该指针失效。
 * @return     无。self 为 NULL 时不执行操作。
 */
void XConsoleShell_delete_base(XConsoleShell* self);

#if XCONSOLE_SHELL_TASKS_ON
/**
 * @brief      设置 `tasks` 命令的产品任务快照提供者。
 * @param      self Shell 对象；不能为空。
 * @param      provider 提供者回调；Shell 只借用，传 NULL 表示清除提供者。
 * @param      userData 产品上下文；Shell 只借用且不释放；provider 为 NULL
 *             时忽略该参数并清除已保存的上下文。
 * @return     设置成功返回 true；self 为 NULL 返回 false 且不修改原状态。
 */
bool XConsoleShell_setTaskProvider(XConsoleShell* self,
                                   XConsoleShellTaskProviderFn provider,
                                   void* userData);
#endif

/**
 * @brief      注册一组静态根命令。
 * @param      self Shell 对象；不能为空。
 * @param      commands 静态命令数组；count 非零时不能为空；Shell 只保存
 *             指针且不取得所有权，数组及其引用内容必须在注销前持续有效。
 * @param      count 数组元素数量；不能超过剩余命令容量，0 表示空操作。
 * @return     全部注册成功返回 true；参数无效、名称重复、容量不足或静态
 *             回调命令功能关闭时返回 false；失败时命令表保持不变。
 */
bool XConsoleShell_registerStaticCommands(XConsoleShell* self,
                                          const XConsoleCommand* commands,
                                          size_t count);
/**
 * @brief      按指针序列注销静态根命令。
 * @param      self Shell 对象；不能为空。
 * @param      commands 与注册时相同地址的静态命令数组；count 非零时不能为空；
 *             只在调用期间借用，函数不释放数组。
 * @param      count 数组元素数量；0 表示空操作。
 * @return     全部找到并注销返回 true；参数无效、存在未注册项、数组内指针
 *             重复或静态回调命令功能关闭时返回 false；失败时命令表保持不变。
 */
bool XConsoleShell_unregisterStaticCommands(XConsoleShell* self,
                                            const XConsoleCommand* commands,
                                            size_t count);
#if XCONSOLE_SHELL_DYNAMIC_REGISTER_ON
/**
 * @brief      深复制注册一个动态根命令及其子命令树。
 * @param      self Shell 对象；不能为空。
 * @param      command 源命令描述；只在调用期间借用。名称、别名、说明、用法
 *             和子命令描述会复制，处理回调与 userData 仍由调用者拥有。
 * @return     注册成功返回 true；参数无效、名称重复、深度或容量超限以及
 *             内存分配失败时返回 false；失败时命令表保持不变。
 */
bool XConsoleShell_registerCommand(XConsoleShell* self,
                                   const XConsoleCommand* command);
/**
 * @brief      按主名称注销动态命令。
 * @param      self Shell 对象；不能为空。
 * @param      name UTF-8 主命令名；不能为空或空字符串，只在调用期间借用。
 * @return     找到并注销返回 true；命令不存在或参数无效返回 false；失败时
 *             命令表保持不变。执行中的命令会延迟到回调返回后释放。
 */
bool XConsoleShell_unregisterCommand(XConsoleShell* self, const char* name);
#endif
/**
 * @brief      查找根命令或子命令路径。
 * @param      self Shell 对象；不能为空，只读借用。
 * @param      path 以空格分隔的 UTF-8 命令路径；不能为空，只在调用期间借用。
 * @param      result 调用方提供的可选输出存储；可为 NULL。成功时写入命令
 *             的借用指针，失败时写入 NULL；返回指针不得释放或修改。
 * @return     完整路径找到命令返回 true；路径无效、解析失败或命令不存在
 *             返回 false，且不修改 Shell。
 */
bool XConsoleShell_findCommand(const XConsoleShell* self,
                               const char* path,
                               const XConsoleCommand** result);

/**
 * @brief      向默认会话输入一个字节。
 * @param      self Shell 对象；不能为空；函数可能更新行缓冲和会话状态。
 * @param      byte 输入字节；CR/LF 提交当前行，Ctrl-C 按配置设置取消标志。
 * @return     字节接收成功返回 XConsoleResult_Ok；提交命令时返回命令结果；
 *             参数无效、行超限、取消或 I/O 失败返回对应错误码。
 */
XConsoleResult XConsoleShell_feedByte(XConsoleShell* self, uint8_t byte);
/**
 * @brief      向默认会话输入一段连续字节。
 * @param      self Shell 对象；不能为空；函数可能更新行缓冲和会话状态。
 * @param      data 输入缓冲；size 非零时不能为空，只在调用期间借用且不会修改。
 * @param      size 输入字节数；0 表示空输入，此时 data 可为 NULL。
 * @return     最后一个字节对应的处理结果；空输入返回 XConsoleResult_Ok；
 *             参数无效返回 XConsoleResult_InvalidArgument。
 */
XConsoleResult XConsoleShell_feedData(XConsoleShell* self,
                                      const void* data, size_t size);
/**
 * @brief      判断指定会话当前是否可由退格键删除一个输入字符。
 * @details    该查询不改变 Shell 状态。远程终端适配器应仅在返回 true 时
 *             回显 `\b \b`，以免空命令行的退格键擦除提示符。
 * @param      self Shell 对象；不能为空。
 * @param      session 目标会话；NULL 或默认会话表示默认输入行。
 * @return     退格会实际删除输入返回 true，否则返回 false。
 */
bool XConsoleShell_canBackspace(const XConsoleShell* self,
                                const XConsoleShellSession* session);
/**
 * @brief      从默认会话 read 回调读取并处理一批输入。
 * @param      self Shell 对象；不能为空且必须配置 read 回调。
 * @param      maxBytes 本轮最多读取的字节数；0 或超过内部临时缓冲容量时
 *             使用实现上限 128 字节。
 * @return     本轮最后一字节的结果；无数据返回 XConsoleResult_Ok；未配置
 *             read 回调返回 XConsoleResult_NotSupported；回调结果非法返回
 *             XConsoleResult_IoError。
 */
XConsoleResult XConsoleShell_pump(XConsoleShell* self, size_t maxBytes);
/**
 * @brief      直接在默认会话中解析并执行一行。
 * @param      self Shell 对象；不能为空；函数会更新历史、统计和取消状态。
 * @param      line UTF-8 行内容，可不含换行符；length 非零时不能为空，只在
 *             调用期间借用且不会修改。
 * @param      length 行字节数，不含结尾 NUL；必须小于行缓冲容量。
 * @return     命令成功返回 XConsoleResult_Ok 或 XConsoleResult_MoreOutput；
 *             参数、语法、权限、容量、命令查找和 I/O 失败返回对应错误码。
 */
XConsoleResult XConsoleShell_processLine(XConsoleShell* self,
                                         const char* line, size_t length);
/**
 * @brief      设置默认会话是否接受并执行新输入。
 * @param      self Shell 对象；传入 NULL 时不执行操作。
 * @param      running true 表示接受输入，false 表示 processLine 拒绝执行。
 * @return     无。self 为 NULL 时不修改任何状态。
 */
void XConsoleShell_setRunning(XConsoleShell* self, bool running);
/**
 * @brief      查询默认会话是否仍在运行。
 * @param      self Shell 对象；可为 NULL。
 * @return     self 非 NULL 且允许继续处理输入时返回 true，否则返回 false。
 */
bool XConsoleShell_isRunning(const XConsoleShell* self);
#if XCONSOLE_SHELL_ASYNC_ON
/**
 * @brief      启动 Shell 的事件异步输入模式。
 * @param      self Shell 对象；不能为空；必须在其所属线程调用。
 * @return     成功启动或已经启动返回 true；没有事件调度器、模式不支持或
 *             参数无效返回 false。
 * @note      启动不会主动读取输入；传输层确认可读后必须调用
 *            XConsoleShell_notifyInput()。read 回调在异步模式必须非阻塞。
 */
bool XConsoleShell_startAsync(XConsoleShell* self);
/**
 * @brief      停止 Shell 的事件异步输入模式。
 * @param      self Shell 对象；可为 NULL。
 * @param      timeoutMs 预留的停止等待时间；事件调度器模式不会阻塞等待。
 * @return     停止成功或本来未启动返回 true；参数无效返回 false。
 */
bool XConsoleShell_stopAsync(XConsoleShell* self, uint32_t timeoutMs);
/**
 * @brief      查询 Shell 是否正在事件异步输入模式中运行。
 * @param      self Shell 对象；可为 NULL。
 * @return     已启动返回 true，否则返回 false。
 */
bool XConsoleShell_isAsyncRunning(const XConsoleShell* self);
/**
 * @brief      向 Shell 所属事件调度器投递一次输入就绪通知。
 * @param      self Shell 对象；不能为空且必须已经启动异步模式。
 * @return     通知已合并或投递成功返回 true；Shell 未启动、事件创建失败或
 *             事件队列已满返回 false。
 * @note      函数只投递事件，不在调用线程执行 read、解析或命令回调；同一时刻
 *            最多保留一个未处理通知。中断服务程序不得直接调用本函数。
 */
bool XConsoleShell_notifyInput(XConsoleShell* self);
/**
 * @brief      处理 Shell 私有输入事件的虚函数基类入口。
 * @param      self Shell 对象；不能为空。
 * @param      event 待处理事件；不能为空。
 * @return     事件属于 Shell 并已处理返回 true，否则委托 XObject 父类。
 */
bool XConsoleShell_event_base(XConsoleShell* self, XEvent* event);
#define XConsoleShell_timerEvent_base XObject_timerEvent_base
#endif
/**
 * @brief      取得默认会话的只读视图。
 * @param      self Shell 对象；只读借用，可为 NULL。
 * @return     默认会话的只读借用指针；self 为 NULL 时返回 NULL。返回指针
 *             由 self 拥有，在 self 销毁后失效，调用者不得释放或修改。
 */
const XConsoleShellSession* XConsoleShell_session_const(const XConsoleShell* self);
/**
 * @brief      取得默认会话的可写视图。
 * @param      self Shell 对象；可为 NULL。
 * @return     默认会话的可写借用指针；self 为 NULL 时返回 NULL。返回指针
 *             由 self 拥有，在 self 销毁后失效，调用者不得释放。
 * @warning    直接修改会话内部状态可能绕过权限和状态约束，优先使用公开 API。
 */
XConsoleShellSession* XConsoleShell_session(XConsoleShell* self);
/**
 * @brief      设置默认会话的产品认证状态。
 * @param      self Shell 对象；传入 NULL 时不执行操作。
 * @param      authenticated true 表示认证通过，false 表示未认证。
 * @return     无。self 为 NULL 时不修改任何状态。
 */
void XConsoleShell_setAuthenticated(XConsoleShell* self, bool authenticated);

#if XCONSOLE_SHELL_REMOTE_OUTPUT_REDIRECT_ON
/**
 * @brief 设置或清除当前会话的后续整行输入处理器。
 * @details
 * 处理器启用后，后续完整输入行会在普通命令解析前转交给 handler，适合
 * SSH/Telnet 菜单、密码外的交互式子状态等非阻塞输入场景。handler 为 NULL
 * 时清除处理器及其上下文。Shell 只借用 handler 和 userData。
 * @param self Shell 对象；不能为空。
 * @param session self 管理的会话；不能为空，默认会话或已打开附加会话均可。
 * @param handler 行处理器；可为 NULL，表示清除当前处理器。
 * @param userData handler 的上下文；handler 为 NULL 时忽略并清除。
 * @return 设置成功返回 true；参数无效或会话不属于 self 返回 false。
 */
bool XConsoleShell_setInputLineHandler(
    XConsoleShell* self, XConsoleShellSession* session,
    XConsoleShellInputLineHandler handler, void* userData);
#endif

#if XCONSOLE_SHELL_MULTI_SESSION_ON
/**
 * @brief      打开一个附加会话。
 * @param      self 启用多会话的 Shell；不能为空。
 * @param      io 新会话传输回调；只在调用期间读取并复制回调值，可为 NULL；
 *             NULL 时复制默认会话传输，回调 userData 始终由调用者管理。
 * @return     新会话的可写借用指针；槽位不足或 self 为 NULL 时返回 NULL。
 *             返回指针由 self 拥有，在关闭会话或销毁 self 后失效，不能释放。
 */
XConsoleShellSession* XConsoleShell_openSession(XConsoleShell* self,
                                                 const XConsoleShellIo* io);
/**
 * @brief      关闭并清空一个附加会话槽位。
 * @param      self Shell 对象；不能为空。
 * @param      session 由 self 的 XConsoleShell_openSession 返回的借用指针；
 *             不能为空，默认会话不能关闭，函数不释放指针本身。
 * @return     成功关闭返回 true；参数无效、会话不属于 self、已关闭或为默认
 *             会话时返回 false；失败时会话状态保持不变。
 */
bool XConsoleShell_closeSession(XConsoleShell* self, XConsoleShellSession* session);
/**
 * @brief      获取当前打开的会话数量。
 * @param      self Shell 对象；只读借用，可为 NULL。
 * @return     当前打开会话数量，包含默认会话；self 为 NULL 时返回 0。
 */
size_t XConsoleShell_sessionCount(const XConsoleShell* self);
/**
 * @brief      按稳定会话标识查找会话。
 * @param      self Shell 对象；不能为空。
 * @param      id 会话标识；0 为无效值，1 表示默认会话。
 * @return     匹配会话的可写借用指针；self 为 NULL 或未找到时返回 NULL。
 *             返回指针由 self 拥有，调用者不得释放。
 */
XConsoleShellSession* XConsoleShell_findSession(XConsoleShell* self, uint32_t id);
/**
 * @brief 刷新指定会话的交互编辑器。
 * @details 传输层收到终端 window-change 后可调用此函数，Shell 会切换到该
 *          会话并立即刷新全屏 Vim；非编辑会话调用时不产生输出。
 * @param self Shell 对象；不能为空。
 * @param session self 管理的已打开会话；不能为空。
 * @return 刷新成功返回 true；会话无效或刷新失败返回 false。
 */
bool XConsoleShell_refreshForSession(XConsoleShell* self,
                                     XConsoleShellSession* session);
/**
 * @brief      向指定会话输入一个字节。
 * @param      self Shell 对象；不能为空。
 * @param      session self 管理的已打开会话借用指针；不能为空。
 * @param      byte 输入字节；行为与 XConsoleShell_feedByte 相同。
 * @return     字节处理或命令执行结果；会话无效返回
 *             XConsoleResult_InvalidArgument，异步输出失败返回 XConsoleResult_IoError。
 */
XConsoleResult XConsoleShell_feedByteForSession(XConsoleShell* self,
                                                XConsoleShellSession* session,
                                                uint8_t byte);
/**
 * @brief      向指定会话输入连续字节。
 * @param      self Shell 对象；不能为空。
 * @param      session self 管理的已打开会话借用指针；不能为空。
 * @param      data 输入缓冲；size 非零时不能为空，只在调用期间借用且不会修改。
 * @param      size 输入字节数；0 表示空输入，此时 data 可为 NULL。
 * @return     最后一个字节对应的处理结果；会话或缓冲参数无效返回
 *             XConsoleResult_InvalidArgument，异步输出失败返回 XConsoleResult_IoError。
 */
XConsoleResult XConsoleShell_feedDataForSession(XConsoleShell* self,
                                                XConsoleShellSession* session,
                                                const void* data, size_t size);
/**
 * @brief      从指定会话的 read 回调读取并处理一批输入。
 * @param      self Shell 对象；不能为空。
 * @param      session self 管理的已打开会话借用指针；不能为空。
 * @param      maxBytes 本轮最多读取的字节数；0 或超过内部临时缓冲容量时
 *             使用实现上限 128 字节。
 * @return     本轮最后一个字节的结果；暂无数据返回 XConsoleResult_Ok；
 *             会话无效、未配置 read 回调或 I/O 失败时返回对应错误码。
 */
XConsoleResult XConsoleShell_pumpForSession(XConsoleShell* self,
                                            XConsoleShellSession* session,
                                            size_t maxBytes);
/**
 * @brief      在指定会话上下文中解析并执行一行。
 * @param      self Shell 对象；不能为空。
 * @param      session self 管理的已打开会话借用指针；不能为空。
 * @param      line UTF-8 行内容；length 非零时不能为空，只在调用期间借用且
 *             不会修改。
 * @param      length 行字节数，不含结尾 NUL；必须小于行缓冲容量。
 * @return     命令处理结果；会话或输入无效返回 XConsoleResult_InvalidArgument，
 *             异步输出失败返回 XConsoleResult_IoError。
 */
XConsoleResult XConsoleShell_processLineForSession(XConsoleShell* self,
                                                   XConsoleShellSession* session,
                                                   const char* line, size_t length);
/**
 * @brief      向指定会话输出二进制数据。
 * @param      self Shell 对象；不能为空。
 * @param      session self 管理的已打开会话借用指针；不能为空。
 * @param      data 输出缓冲；size 非零时不能为空，只在调用期间借用且不会修改。
 * @param      size 输出字节数；0 表示空输出，此时 data 可为 NULL。
 * @return     全部数据已提交或排入队列返回 true；会话无效、参数无效、未
 *             配置 write 回调或写入、刷新失败时返回 false。
 */
bool XConsoleShell_writeForSession(XConsoleShell* self, XConsoleShellSession* session,
                                   const void* data, size_t size);
#endif

#if XCONSOLE_SHELL_PROCESS_ASYNC_ON
/**
 * @brief      驱动所有异步 exec 任务并回收已完成任务。
 * @param      self Shell 对象；可为 NULL。
 * @param      timeoutMsecs 单次进程轮询等待时间，单位毫秒；负数表示使用后端
 *             默认等待语义。
 * @return     本轮已完成并回收的任务数量；self 为 NULL 时返回 0。
 */
size_t XConsoleShell_pollProcesses(XConsoleShell* self, int timeoutMsecs);
#endif

#if XCONSOLE_SHELL_HISTORY_ON
/**
 * @brief      获取默认会话当前保存的历史条目数量。
 * @param      self Shell 对象；只读借用，可为 NULL。
 * @return     当前保存的历史条目数量；self 为 NULL 时返回 0。
 */
size_t XConsoleShell_historyCount(const XConsoleShell* self);
/**
 * @brief      按时间顺序取得一条默认会话历史文本。
 * @param      self Shell 对象；只读借用，可为 NULL。
 * @param      index 从最旧条目开始的零基索引；必须小于 historyCount。
 * @return     Shell 内部 NUL 结尾 UTF-8 文本的只读借用指针；self 为 NULL
 *             或 index 越界时返回 NULL。返回指针不得释放或修改，并可能在
 *             后续输入、清空历史或销毁 self 后失效。
 */
const char* XConsoleShell_historyAt(const XConsoleShell* self, size_t index);
/**
 * @brief      清空默认会话的历史缓冲。
 * @param      self Shell 对象；传入 NULL 时不执行操作。
 * @return     无。self 为 NULL 时不修改任何状态。
 */
void XConsoleShell_clearHistory(XConsoleShell* self);
#endif

#if XCONSOLE_SHELL_STATS_ON
/**
 * @brief      获取当前 Shell 的统计快照。
 * @param      self Shell 对象；不能为空，只读借用。
 * @param      stats 调用方提供的可写输出存储；不能为空；函数成功时覆盖整个
 *             结构，调用方拥有其生命周期。
 * @return     成功复制返回 true；任一参数为 NULL 时返回 false，且不修改
 *             stats 指向的原有内容。
 */
bool XConsoleShell_stats(const XConsoleShell* self, XConsoleShellStats* stats);
/**
 * @brief      清零运行统计，不影响命令表、会话和历史。
 * @param      self Shell 对象；传入 NULL 时不执行操作。
 * @return     无。self 为 NULL 时不修改任何状态。
 */
void XConsoleShell_clearStats(XConsoleShell* self);
#endif

/**
 * @brief      以短写重试方式输出二进制数据。
 * @param      self Shell 对象；不能为空且必须配置 write 回调。
 * @param      data 输出缓冲；size 非零时不能为空，只在调用期间借用且不会修改。
 * @param      size 输出字节数；0 表示空输出，此时 data 可为 NULL。
 * @return     全部数据写出或排入异步队列返回 true；参数无效、未配置 write
 *             回调、队列排空失败或 write/flush 回调失败时返回 false。
 */
bool XConsoleShell_write(XConsoleShell* self, const void* data, size_t size);
/**
 * @brief      输出一段 NUL 结尾的 UTF-8 文本。
 * @param      self Shell 对象；不能为空且必须配置 write 回调。
 * @param      text NUL 结尾 UTF-8 文本；不能为空，只在调用期间借用且不会修改。
 * @return     文本全部写出或排队返回 true；参数无效或底层输出失败返回 false。
 */
bool XConsoleShell_writeUtf8(XConsoleShell* self, const char* text);
#if XCONSOLE_SHELL_ASYNC_OUTPUT_ON
/**
 * @brief      排空异步输出队列并刷新传输。
 * @param      self Shell 对象；不能为空且必须配置 write 回调。
 * @return     队列全部写出且可选 flush 回调成功返回 true；参数无效、未配置
 *             write 回调或任一回调失败时返回 false；失败时队列不保证已全部发送。
 */
bool XConsoleShell_flushOutput(XConsoleShell* self);
#endif
/**
 * @brief      输出统一格式的 UTF-8 错误行。
 * @param      self Shell 对象；不能为空且必须配置 write 回调。
 * @param      result 错误结果码；detail 为 NULL 时用于选择内置错误说明。
 * @param      detail 可选 NUL 结尾 UTF-8 详细信息；只在调用期间借用；为 NULL
 *             时输出 result 对应的内置说明。
 * @return     完整错误行输出成功返回 true；参数无效或任一输出步骤失败返回 false。
 */
bool XConsoleShell_writeError(XConsoleShell* self, XConsoleResult result,
                              const char* detail);
/**
 * @brief      查询默认会话是否已收到取消请求。
 * @param      self Shell 对象；只读借用，可为 NULL。
 * @return     取消功能启用且默认会话已取消时返回 true；self 为 NULL、功能
 *             关闭或当前未取消时返回 false。
 */
bool XConsoleShell_isCancelled(const XConsoleShell* self);
/**
 * @brief      清除默认会话的取消标志。
 * @param      self Shell 对象；可为 NULL。
 * @return     无。self 为 NULL 或取消功能关闭时不执行操作。
 */
void XConsoleShell_clearCancelled(XConsoleShell* self);

#ifdef __cplusplus
}
#endif

#endif /* XCONSOLE_SHELL_ON && XCONSOLE_SHELL_COMMAND_ON && XCONSOLE_SHELL_IO_ON */

/* XClass create API default-memory wrappers. */
#undef XConsoleShell_create
#define XConsoleShell_create(...) XConsoleShell_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, __VA_ARGS__)

#endif /* XCONSOLE_SHELL_H */
