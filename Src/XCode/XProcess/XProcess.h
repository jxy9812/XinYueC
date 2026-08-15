/**
 * @file XProcess.h
 * @brief 子进程控制对象，对齐 Qt 6.8 QProcess 的公开 API。
 * @details
 * XProcess 继承 XIODevice，提供程序、参数、环境、工作目录、标准输入/输出/
 * 错误通道、同步等待、终止、分离启动和 XObject 信号。Qt 的私有头文件和
 * 私有结构不属于本接口；进程状态机、管道缓冲、回收和错误转换由 XinYueC
 * 实现直接管理，真正的平台创建细节只允许位于现有 Drive 后端。
 */

#ifndef XPROCESS_H
#define XPROCESS_H

#include "XProcessConfig.h"

#if XProcess_ON

#include <stdint.h>
#include <stdbool.h>
#include "XIODevice.h"
#include "XByteArray.h"
#include "XProcessEnvironment.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief XProcess 继承 XIODevice 且只重载已有虚函数槽。 */
XCLASS_DEFINE_BEGING(XProcess)
XCLASS_DEFINE_EXTEND_END(XProcess, XIODevice)

/* ============================== 类型定义 ============================== */

/** @brief 公开进程 ID 类型；未运行时返回 0，分离启动失败时输出 -1。 */
typedef int64_t XProcessId;

/** @brief 进程启动、运行和退出错误类型，对齐 QProcess::ProcessError。 */
typedef enum XProcessError {
    XProcessError_FailedToStart = 0, /**< 程序不存在、权限不足或管道创建失败。 */
    XProcessError_Crashed = 1,       /**< 进程被信号或等效异常终止。 */
    XProcessError_Timedout = 2,      /**< 最近一次 waitFor* 操作超时。 */
    XProcessError_ReadError = 3,     /**< 读取标准输出或标准错误失败。 */
    XProcessError_WriteError = 4,    /**< 写入标准输入失败。 */
    XProcessError_UnknownError = 5   /**< 未归类错误；初始值和正常退出保持此值。 */
} XProcessError;

/** @brief 进程生命周期状态，对齐 QProcess::ProcessState。 */
typedef enum XProcessState {
    XProcessState_NotRunning = 0, /**< 没有活动子进程。 */
    XProcessState_Starting = 1,   /**< 已请求启动，尚未确认 exec 成功。 */
    XProcessState_Running = 2     /**< 子进程已创建并可进行 I/O。 */
} XProcessState;

/** @brief 当前读取的标准通道，对齐 QProcess::ProcessChannel。 */
typedef enum XProcessChannel {
    XProcessChannel_StandardOutput = 0, /**< 标准输出通道。 */
    XProcessChannel_StandardError = 1   /**< 标准错误通道。 */
} XProcessChannel;

/** @brief 标准输出和标准错误的连接方式，对齐 QProcess::ProcessChannelMode。 */
typedef enum XProcessChannelMode {
    XProcessChannelMode_SeparateChannels = 0,       /**< 分开保存 stdout/stderr。 */
    XProcessChannelMode_MergedChannels = 1,          /**< stderr 合并到 stdout。 */
    XProcessChannelMode_ForwardedChannels = 2,       /**< 两个通道转发到父端。 */
    XProcessChannelMode_ForwardedOutputChannel = 3,  /**< 只转发 stdout。 */
    XProcessChannelMode_ForwardedErrorChannel = 4    /**< 只转发 stderr。 */
} XProcessChannelMode;

/** @brief 标准输入连接方式，对齐 QProcess::InputChannelMode。 */
typedef enum XProcessInputChannelMode {
    XProcessInputChannelMode_ManagedInputChannel = 0,  /**< 由 XProcess 写入 stdin。 */
    XProcessInputChannelMode_ForwardedInputChannel = 1 /**< 转发父端输入。 */
} XProcessInputChannelMode;

/** @brief 进程退出状态，对齐 QProcess::ExitStatus。 */
typedef enum XProcessExitStatus {
    XProcessExitStatus_NormalExit = 0, /**< 进程正常返回或等效正常退出。 */
    XProcessExitStatus_CrashExit = 1   /**< 进程被信号或异常终止。 */
} XProcessExitStatus;

/** @brief Unix 公开进程参数标志；非 Unix 后端可忽略不支持的标志。 */
typedef enum XProcessUnixProcessFlag {
    XProcessUnixProcessFlag_ResetSignalHandlers = 0x0001,
    XProcessUnixProcessFlag_IgnoreSigPipe = 0x0002,
    XProcessUnixProcessFlag_CloseFileDescriptors = 0x0010,
    XProcessUnixProcessFlag_UseVFork = 0x0020,
    XProcessUnixProcessFlag_CreateNewSession = 0x0040,
    XProcessUnixProcessFlag_DisconnectControllingTerminal = 0x0080,
    XProcessUnixProcessFlag_ResetIds = 0x0100
} XProcessUnixProcessFlag;

/** @brief Unix 公开进程参数；保留字段必须保持为 0。 */
typedef struct XProcessUnixProcessParameters {
    uint32_t flags;                    /**< XProcessUnixProcessFlag 按位组合。 */
    int32_t lowestFileDescriptorToClose; /**< 关闭文件描述符的起始值。 */
    uint32_t reserved[6];              /**< 预留字段；调用方必须清零。 */
} XProcessUnixProcessParameters;

/** @brief XProcess 对象，首成员为 XIODevice，内部状态由本模块管理。 */
typedef struct XProcess XProcess;

struct XProcess {
    XIODevice base;                    /**< 第一个成员；继承 XIODevice，禁止手工修改。 */
    XString* m_program;                /**< 当前程序；对象拥有。 */
    XStringList* m_arguments;          /**< 参数列表；对象拥有。 */
    XProcessEnvironment m_environment;/**< 子进程环境；对象内嵌并拥有。 */
    XString* m_workingDirectory;       /**< 工作目录；对象拥有，空值表示继承。 */
    XString* m_standardInputFile;      /**< 标准输入重定向文件；对象拥有。 */
    XString* m_standardOutputFile;     /**< 标准输出重定向文件；对象拥有。 */
    XString* m_standardErrorFile;      /**< 标准错误重定向文件；对象拥有。 */
    XProcess* m_standardOutputProcess; /**< 管道目标；借用，不能形成环。 */
    XProcess* m_standardOutputSource;  /**< 管道源反向关联；借用，用于析构时解除关系。 */
    XProcessChannelMode m_channelMode;/**< 下次启动使用的输出通道模式。 */
    XProcessInputChannelMode m_inputMode; /**< 下次启动使用的输入模式。 */
    XProcessChannel m_readChannel;     /**< 当前读取通道。 */
    XProcessError m_error;             /**< 最近一次错误。 */
    XProcessState m_state;             /**< 当前进程状态。 */
    XProcessExitStatus m_exitStatus;   /**< 最近一次退出状态。 */
    int m_exitCode;                    /**< 最近一次退出码。 */
    XProcessId m_processId;            /**< 活动进程 ID；未运行时为 0。 */
    bool m_stdoutAppend;               /**< 标准输出文件是否追加。 */
    bool m_stderrAppend;               /**< 标准错误文件是否追加。 */
    bool m_stdoutClosed;               /**< 是否关闭 stdout 接收。 */
    bool m_stderrClosed;               /**< 是否关闭 stderr 接收。 */
    bool m_stdinClosed;                /**< 是否请求关闭 stdin。 */
    XString* m_errorString;             /**< 最近一次错误文本；对象拥有。 */
    XProcessUnixProcessParameters m_unixParameters; /**< Unix 参数；保留公开值。 */
    void* m_backend;                   /**< Drive 后端状态；仅本模块访问，不转移所有权。 */
};

/* ============================== 生命周期 ============================== */

/** @brief 初始化 XProcess 虚函数表；返回单例表，失败返回 NULL。 */
XVtable* XProcess_class_init(void);

/** @brief 初始化栈上 XProcess；@param self 待初始化对象，必须保持有效。 */
void XProcess_init(XProcess* self);

/** @brief 创建堆上 XProcess；失败返回 NULL，调用方必须 delete_base。 */
XProcess* XProcess_create_ex(XMemoryType memory);

/** @brief 复用 XClass 虚析构入口；会调用 XProcess 自己注册的 deinit 槽。 */
#define XProcess_deinit_base XClass_deinit_base
/** @brief 复用 XClass 堆对象删除入口；NULL 安全。 */
#define XProcess_delete_base XClass_delete_base

/* ============================== 属性 ============================== */

/** @brief 设置程序名称；@param self 进程对象；@param program 程序名对象，调用期间借用。 */
bool XProcess_setProgram(XProcess* self, const XString* program);
/** @brief UTF-8 设置程序名称；@param self 进程对象；@param program NUL 结尾的 UTF-8 程序名。 */
bool XProcess_setProgram_utf8(XProcess* self, const char* program);
/** @brief 获取程序内部只读引用。
 * @param self 进程对象；可为 NULL。
 * @return 内部借用字符串；对象销毁或下一次设置后失效。
 */
const XString* XProcess_program_const(const XProcess* self);
/** @brief 返回程序副本。
 * @param self 进程对象；可为 NULL。
 * @return 新 XString；失败返回 NULL，调用方必须释放。
 */
XString* XProcess_program(const XProcess* self);

/** @brief 深复制设置参数列表；@param self 进程对象；@param arguments 参数列表，调用期间借用。 */
bool XProcess_setArguments(XProcess* self, const XStringList* arguments);
/** @brief 从 UTF-8 数组设置参数；@param self 进程对象；@param arguments 参数数组；@param count 数量。 */
bool XProcess_setArguments_utf8(XProcess* self, const char* const* arguments, size_t count);
/** @brief 返回参数列表副本。
 * @param self 进程对象；可为 NULL。
 * @return 新 XStringList；失败返回 NULL，调用方必须释放。
 */
XStringList* XProcess_arguments(const XProcess* self);

/** @brief 获取输出通道模式；@param self 进程对象，可为 NULL。 */
XProcessChannelMode XProcess_processChannelMode(const XProcess* self);
/** @brief 设置下次启动的输出通道模式；@param self 进程对象；@param mode 通道模式。 */
void XProcess_setProcessChannelMode(XProcess* self, XProcessChannelMode mode);
/** @brief 获取输入通道模式；@param self 进程对象，可为 NULL。 */
XProcessInputChannelMode XProcess_inputChannelMode(const XProcess* self);
/** @brief 设置下次启动的输入通道模式；@param self 进程对象；@param mode 通道模式。 */
void XProcess_setInputChannelMode(XProcess* self, XProcessInputChannelMode mode);
/** @brief 获取当前读通道；@param self 进程对象，可为 NULL。 */
XProcessChannel XProcess_readChannel(const XProcess* self);
/** @brief 设置当前读通道；@param self 进程对象；@param channel 标准输出或错误通道。 */
void XProcess_setReadChannel(XProcess* self, XProcessChannel channel);

/** @brief 关闭指定读通道；@param self 进程对象；@param channel 要关闭的标准通道。 */
void XProcess_closeReadChannel(XProcess* self, XProcessChannel channel);
/** @brief 请求在已写数据发送完后关闭标准输入；@param self 进程对象。 */
void XProcess_closeWriteChannel(XProcess* self);
/** @brief 设置标准输入重定向文件；@param self 进程对象；@param fileName 文件名对象。 */
bool XProcess_setStandardInputFile(XProcess* self, const XString* fileName);
/** @brief 设置 UTF-8 标准输入重定向文件；@param self 进程对象；@param fileName UTF-8 路径。 */
bool XProcess_setStandardInputFile_utf8(XProcess* self, const char* fileName);
/** @brief 设置标准输出重定向文件；@param self 进程对象；@param fileName 文件名；@param append 是否追加。 */
bool XProcess_setStandardOutputFile(XProcess* self, const XString* fileName, bool append);
/** @brief 设置 UTF-8 标准输出重定向文件；@param self 进程对象；@param fileName UTF-8 路径；@param append 是否追加。 */
bool XProcess_setStandardOutputFile_utf8(XProcess* self, const char* fileName, bool append);
/** @brief 设置标准错误重定向文件；@param self 进程对象；@param fileName 文件名；@param append 是否追加。 */
bool XProcess_setStandardErrorFile(XProcess* self, const XString* fileName, bool append);
/** @brief 设置 UTF-8 标准错误重定向文件；@param self 进程对象；@param fileName UTF-8 路径；@param append 是否追加。 */
bool XProcess_setStandardErrorFile_utf8(XProcess* self, const char* fileName, bool append);
/** @brief 将 stdout 连接到目标 stdin；@param self 源进程；@param destination 目标进程，均为借用。 */
bool XProcess_setStandardOutputProcess(XProcess* self, XProcess* destination);
/** @brief 获取工作目录内部只读引用；空引用表示继承调用方目录。 */
const XString* XProcess_workingDirectory_const(const XProcess* self);
/** @brief 设置工作目录；@param self 进程对象；@param directory 目录对象，调用期间借用。 */
bool XProcess_setWorkingDirectory(XProcess* self, const XString* directory);
/** @brief 设置 UTF-8 工作目录；@param self 进程对象；@param directory UTF-8 目录路径。 */
bool XProcess_setWorkingDirectory_utf8(XProcess* self, const char* directory);
/** @brief 返回工作目录副本；调用方必须释放。 */
XString* XProcess_workingDirectory(const XProcess* self);

/** @brief 设置显式环境的深拷贝；@param self 进程对象；@param environment 环境对象，调用期间借用。 */
bool XProcess_setProcessEnvironment(XProcess* self, const XProcessEnvironment* environment);
/** @brief 返回环境对象副本；调用方必须调用 XProcessEnvironment_delete。 */
XProcessEnvironment* XProcess_processEnvironment(const XProcess* self);
/** @brief 设置 name=value 环境列表；@param self 进程对象；@param environment 列表，调用期间借用。 */
bool XProcess_setEnvironment(XProcess* self, const XStringList* environment);
/** @brief 兼容 QProcess::environment；返回列表副本。 */
XStringList* XProcess_environment(const XProcess* self);

/** @brief 获取最近一次错误类型。
 * @param self 进程对象；可为 NULL。
 * @return 错误枚举；self 为空返回 UnknownError。
 */
XProcessError XProcess_error(const XProcess* self);
/** @brief 获取当前状态。
 * @param self 进程对象；可为 NULL。
 * @return 状态枚举；self 为空返回 NotRunning。
 */
XProcessState XProcess_state(const XProcess* self);
/** @brief 获取最近一次错误文本内部只读引用。
 * @param self 进程对象；可为 NULL。
 * @return 借用的 XString；无错误或参数无效返回 NULL。
 */
const XString* XProcess_errorString_const(const XProcess* self);
/** @brief 返回最近一次错误文本副本。
 * @param self 进程对象；可为 NULL。
 * @return 新 XString；失败返回 NULL，调用方必须释放。
 */
XString* XProcess_errorString(const XProcess* self);
/** @brief 获取活动进程 ID。
 * @param self 进程对象；可为 NULL。
 * @return 活动进程 ID；未运行或参数无效返回 0。
 */
XProcessId XProcess_processId(const XProcess* self);
/** @brief 获取最近一次退出码。
 * @param self 进程对象；可为 NULL。
 * @return 进程退出码；尚未退出时返回后端默认值。
 */
int XProcess_exitCode(const XProcess* self);
/** @brief 获取最近一次退出状态。
 * @param self 进程对象；可为 NULL。
 * @return 正常退出或崩溃状态。
 */
XProcessExitStatus XProcess_exitStatus(const XProcess* self);

/* ============================== 启动与等待 ============================== */

/** @brief 使用指定程序和参数启动；@param self 进程对象；@param program 程序名；@param arguments 参数列表；@param mode XIODevice 打开模式。 */
bool XProcess_start(XProcess* self, const XString* program,
                    const XStringList* arguments, XIODeviceBaseMode mode);
/** @brief 使用已设置程序和参数启动；@param self 进程对象；@param mode XIODevice 打开模式。 */
bool XProcess_start_2(XProcess* self, XIODeviceBaseMode mode);
/** @brief UTF-8 程序和参数数组启动；@param self 进程对象；@param program 程序名；@param arguments 参数数组；@param count 数量；@param mode 打开模式。 */
bool XProcess_start_utf8(XProcess* self, const char* program,
                         const char* const* arguments, size_t count,
                         XIODeviceBaseMode mode);
/** @brief 按 splitCommand 规则分词后启动；@param self 进程对象；@param command UTF-8 命令行；@param mode 打开模式。 */
bool XProcess_startCommand(XProcess* self, const XString* command,
                           XIODeviceBaseMode mode);
/** @brief 通过 XIODevice 虚函数打开当前程序；@param self 进程对象；@param mode 打开模式。 */
bool XProcess_open(XProcess* self, XIODeviceBaseMode mode);
/** @brief 复用 XIODevice::open 的父类调度入口。 */
#define XProcess_open_base(self, mode) \
    XIODevice_open_base((XIODevice*)(self), (mode))
/** @brief 启动并从对象分离；@param self 进程对象；@param pid 输出 PID，可为 NULL，失败写入 -1。 */
bool XProcess_startDetached(XProcess* self, XProcessId* pid);
/** @brief 静态分离启动；@param program 程序名；@param arguments 参数列表；@param workingDirectory 工作目录；@param pid 输出 PID。 */
bool XProcess_startDetached_static(const XString* program,
                                   const XStringList* arguments,
                                   const XString* workingDirectory,
                                   XProcessId* pid);
/** @brief 等待进入 Running；@param self 进程对象；@param msecs 等待毫秒数，-1 表示无限等待。 */
bool XProcess_waitForStarted(XProcess* self, int msecs);
/** @brief XIODevice::waitForReadyRead 的虚函数调度入口。 */
#define XProcess_waitForReadyRead_base(self, msecs) \
    XIODevice_waitForReadyRead_base((XIODevice*)(self), (msecs))
/** @brief XIODevice::waitForBytesWritten 的虚函数调度入口。 */
#define XProcess_waitForBytesWritten_base(self, msecs) \
    XIODevice_waitForBytesWritten_base((XIODevice*)(self), (msecs))
/** @brief 等待进程退出并发出 finished；@param self 进程对象；@param msecs 等待毫秒数，-1 表示无限等待。 */
bool XProcess_waitForFinished(XProcess* self, int msecs);
/** @brief 主动推进状态机；@param self 进程对象；@param timeoutMsecs 后端轮询等待毫秒数。 */
bool XProcess_poll(XProcess* self, int timeoutMsecs);

/* ============================== I/O 与控制 ============================== */

/** @brief 读取全部 stdout 缓冲；@param self 进程对象；返回新 XByteArray，调用方负责释放。 */
XByteArray* XProcess_readAllStandardOutput(XProcess* self);
/** @brief 读取全部 stderr 缓冲；@param self 进程对象；合并通道时返回空对象。 */
XByteArray* XProcess_readAllStandardError(XProcess* self);
/** @brief XIODevice::close 的虚函数调度入口，随后 kill 并等待退出。 */
#define XProcess_close_base(self) \
    XIODevice_close_base((XIODevice*)(self))
/** @brief XIODevice::isSequential 的虚函数调度入口；XProcess 始终顺序访问。 */
#define XProcess_isSequential_base(self) \
    XIODevice_isSequential_base((const XIODevice*)(self))
/** @brief 复用 XIODevice::bytesAvailable 的父类调度入口。 */
#define XProcess_bytesAvailable_base(self) \
    XIODevice_bytesAvailable_base((const XIODevice*)(self))
/** @brief XIODevice::bytesToWrite 的虚函数调度入口。 */
#define XProcess_bytesToWrite_base(self) \
    XIODevice_bytesToWrite_base((const XIODevice*)(self))
/** @brief XIODevice::readData 的虚函数调度入口。 */
#define XProcess_readData_base(self, data, maxlen) \
    XIODevice_readData_base((XIODevice*)(self), (data), (maxlen))
/** @brief XIODevice::writeData 的虚函数调度入口。 */
#define XProcess_writeData_base(self, data, len) \
    XIODevice_writeData_base((XIODevice*)(self), (data), (len))
/** @brief 请求进程正常终止；@param self 进程对象。 */
void XProcess_terminate(XProcess* self);
/** @brief 强制终止进程；@param self 进程对象。 */
void XProcess_kill(XProcess* self);

/* ============================== 静态辅助 ============================== */

/** @brief 同步执行程序。
 * @param program 程序名对象；调用期间借用。
 * @param arguments 参数列表；调用期间借用，可为 NULL。
 * @return 进程退出码；启动失败返回 -2，崩溃返回 -1。
 */
int XProcess_execute_static(const XString* program, const XStringList* arguments);
/** @brief 返回当前系统环境列表副本。
 * @return 新 XStringList；失败返回 NULL，调用方必须释放。
 */
XStringList* XProcess_systemEnvironment_static(void);
/** @brief 返回当前平台空设备路径副本。
 * @return 新 XString；失败返回 NULL，调用方必须释放。
 */
XString* XProcess_nullDevice_static(void);
/** @brief 按 Qt 公开 splitCommand 规则分词。
 * @param command UTF-8 命令行；调用期间借用。
 * @return 新参数列表；语法错误或内存不足返回 NULL。
 */
XStringList* XProcess_splitCommand_static(const XString* command);

/* ============================== Unix 公开参数 ============================== */

/** @brief 获取当前 Unix 参数结构副本。
 * @param self 进程对象；可为 NULL。
 * @return 参数结构副本；self 为空返回全零结构。
 */
XProcessUnixProcessParameters XProcess_unixProcessParameters(const XProcess* self);
/** @brief 设置 Unix 参数。
 * @param self 进程对象；不能为空且不能处于运行状态。
 * @param parameters 参数结构；调用期间借用，保留字段必须为零。
 * @return 设置成功返回 true。
 */
bool XProcess_setUnixProcessParameters(XProcess* self,
                                       const XProcessUnixProcessParameters* parameters);
/** @brief 仅设置 Unix 参数标志。
 * @param self 进程对象；不能为空且不能处于运行状态。
 * @param flags Unix 参数标志组合。
 * @return 设置成功返回 true。
 */
bool XProcess_setUnixProcessParameters_flags(XProcess* self, uint32_t flags);

/* ============================== XObject 信号 ============================== */

/** @brief 进程确认启动后的 started 信号。
 * @param self 进程对象；不能为空。
 * @return 信号对象借用指针；失败返回 NULL。
 */
void* XProcess_started_signal(XProcess* self);
/** @brief 进程退出信号。
 * @param self 进程对象；不能为空。
 * @param exitCode 退出码参数，用于匹配信号签名。
 * @param status 退出状态参数，用于匹配信号签名。
 * @return 信号对象借用指针；失败返回 NULL。
 */
void* XProcess_finished_signal(XProcess* self, int exitCode, XProcessExitStatus status);
/** @brief 进程错误信号。
 * @param self 进程对象；不能为空。
 * @param error 错误参数，用于匹配信号签名。
 * @return 信号对象借用指针；失败返回 NULL。
 */
void* XProcess_errorOccurred_signal(XProcess* self, XProcessError error);
/** @brief 状态改变信号。
 * @param self 进程对象；不能为空。
 * @param state 状态参数，用于匹配信号签名。
 * @return 信号对象借用指针；失败返回 NULL。
 */
void* XProcess_stateChanged_signal(XProcess* self, XProcessState state);
/** @brief stdout 有新数据的信号。
 * @param self 进程对象；不能为空。
 * @return 信号对象借用指针；失败返回 NULL。
 */
void* XProcess_readyReadStandardOutput_signal(XProcess* self);
/** @brief stderr 有新数据的信号。
 * @param self 进程对象；不能为空。
 * @return 信号对象借用指针；失败返回 NULL。
 */
void* XProcess_readyReadStandardError_signal(XProcess* self);

#ifdef __cplusplus
}
#endif

#endif /* XProcess_ON */

/* XClass create API default-memory wrappers. */
#undef XProcess_create
#define XProcess_create() XProcess_create_ex(XCLASS_DEFAULT_MEMORY_TYPE)

#endif /* XPROCESS_H */
