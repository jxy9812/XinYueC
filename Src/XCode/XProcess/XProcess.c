/**
 * @file XProcess.c
 * @brief XProcess 公共状态机、QProcess 公开 API 映射和 XIODevice 重载实现。
 * @details
 * 本文件直接管理程序、参数、环境、通道、错误、状态和等待语义；不包含
 * Qt 私有结构，也不调用 POSIX/Win32/RTOS 原生接口。创建子进程、管道和
 * 回收由 XProcess_Protected.h 规定的 XinYueC Drive 后端提供。
 */

#include "XProcess.h"

#if XProcess_ON

#include "XProcess_Protected.h"
#include "XMemory.h"
#include "XDateTime.h"
#include "XEventLoop.h"
#include "XVarList.h"
#include <string.h>
#include <ctype.h>

static void VXProcess_deinit(XObject* object);
static bool VXProcess_open(XIODevice* io, XIODeviceBaseMode mode);
static void VXProcess_close(XIODevice* io);
static bool VXProcess_isSequential(const XIODevice* io);
static int64_t VXProcess_bytesAvailable(const XIODevice* io);
static int64_t VXProcess_bytesToWrite(const XIODevice* io);
static bool VXProcess_waitForReadyRead(XIODevice* io, int msecs);
static bool VXProcess_waitForBytesWritten(XIODevice* io, int msecs);
static int64_t VXProcess_readData(XIODevice* io, char* data, int64_t maxlen);
static int64_t VXProcess_writeData(XIODevice* io, const char* data, int64_t len);
static void xprocess_close_impl(XProcess* self);
static bool xprocess_waitForReadyRead_impl(XProcess* self, int msecs);
static bool xprocess_waitForBytesWritten_impl(XProcess* self, int msecs);

static bool xprocess_set_string(XString** target, const XString* value)
{
    XString* copy = value ? XString_create_copy(value) : XString_create();
    if (!copy) return false;
    if (*target) XString_delete_base((XString*)*target);
    *target = copy;
    return true;
}

/* XPROCESS_MAX_PATH 以 UTF-8 字节数限制程序、工作目录和重定向路径。 */
static bool xprocess_path_allowed(const XString* value)
{
    const char* utf8;
    if (!value || XPROCESS_MAX_PATH == 0) return true;
    utf8 = XString_toUtf8(value);
    return utf8 && strlen(utf8) <= (size_t)XPROCESS_MAX_PATH;
}

/* 从 QProcess::setEnvironment 使用的 name=value 字符串创建独立名称。 */
static bool xprocess_insert_environment_entry(XProcessEnvironment* environment,
                                              const char* entry)
{
    const char* equal;
    XString* name;
    bool result;
    if (!environment || !entry) return false;
    equal = strchr(entry, '=');
    if (!equal || equal == entry) return true;
    name = XString_create_with_length_utf8(entry, (size_t)(equal - entry));
    if (!name) return false;
    result = XProcessEnvironment_insert_utf8(environment, XString_toUtf8(name), equal + 1);
    XString_delete_base(name);
    return result;
}

static void xprocess_set_error(XProcess* self, XProcessError error, const char* text)
{
    if (!self) return;
    self->m_error = error;
    if (!self->m_errorString) self->m_errorString = XString_create();
    if (self->m_errorString) XString_assign_utf8(self->m_errorString, text ? text : "");
    XProcess_errorOccurred_signal(self, error);
}

static void xprocess_set_state(XProcess* self, XProcessState state)
{
    if (!self || self->m_state == state) return;
    self->m_state = state;
    XProcess_stateChanged_signal(self, state);
}

XVtable* XProcess_class_init(void)
{
    XVTABLE_INIT_DEFAULT(XProcess);
    XCLASS_SET_CLASS_NAME_DEFAULT("XProcess");
    XVTABLE_INHERIT_XCLASS(XIODevice);
    /* QProcess::open() 是虚函数：打开即启动当前已配置程序和参数。 */
    XVTABLE_OVERLOAD_DEFAULT(EXIODevice_Open, VXProcess_open);
    XVTABLE_OVERLOAD_DEFAULT(EXIODevice_Close, VXProcess_close);
    XVTABLE_OVERLOAD_DEFAULT(EXIODevice_IsSequential, VXProcess_isSequential);
    XVTABLE_OVERLOAD_DEFAULT(EXIODevice_BytesAvailable, VXProcess_bytesAvailable);
    XVTABLE_OVERLOAD_DEFAULT(EXIODevice_BytesToWrite, VXProcess_bytesToWrite);
    XVTABLE_OVERLOAD_DEFAULT(EXIODevice_WaitForReadyRead, VXProcess_waitForReadyRead);
    XVTABLE_OVERLOAD_DEFAULT(EXIODevice_WaitForBytesWritten, VXProcess_waitForBytesWritten);
    XVTABLE_OVERLOAD_DEFAULT(EXIODevice_ReadData, VXProcess_readData);
    XVTABLE_OVERLOAD_DEFAULT(EXIODevice_WriteData, VXProcess_writeData);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Deinit, VXProcess_deinit);
    XCLASS_SHOW_SIZE_DEFAULT(XProcess);
    return XVTABLE_DEFAULT;
}

void XProcess_init(XProcess* self)
{
    if (!self) return;
    XIODevice_init(&self->base);
    XClassGetVtable(self) = XProcess_class_init();
    self->m_program = XString_create();
    self->m_arguments = XStringList_create();
    XProcessEnvironment_initInherit(&self->m_environment);
    self->m_workingDirectory = XString_create();
    self->m_standardInputFile = XString_create();
    self->m_standardOutputFile = XString_create();
    self->m_standardErrorFile = XString_create();
    self->m_errorString = XString_create();
    self->m_standardOutputProcess = NULL;
    self->m_standardOutputSource = NULL;
    self->m_channelMode = XProcessChannelMode_SeparateChannels;
    self->m_inputMode = XProcessInputChannelMode_ManagedInputChannel;
    self->m_readChannel = XProcessChannel_StandardOutput;
    self->m_error = XProcessError_UnknownError;
    self->m_state = XProcessState_NotRunning;
    self->m_exitStatus = XProcessExitStatus_NormalExit;
    self->m_exitCode = 0;
    self->m_processId = 0;
    self->m_stdoutAppend = false;
    self->m_stderrAppend = false;
    self->m_stdoutClosed = false;
    self->m_stderrClosed = false;
    self->m_stdinClosed = false;
    self->m_backend = NULL;
    memset(&self->m_unixParameters, 0, sizeof(self->m_unixParameters));
}

XProcess* XProcess_create(void)
{
    XProcess* self = (XProcess*)XMalloc_System(sizeof(*self));
    if (!self) return NULL;
    XProcess_init(self);
    if (!self->m_program || !self->m_arguments || !self->m_workingDirectory ||
        !self->m_standardInputFile || !self->m_standardOutputFile ||
        !self->m_standardErrorFile || !self->m_errorString) {
        XProcess_deinit_base(self);
        XFree_System(self);
        return NULL;
    }
    Set_Class_MemoryFree(self, XFree_System);
    return self;
}

static void VXProcess_deinit(XObject* object)
{
    XProcess* self = (XProcess*)object;
    if (!self) return;
    if (self->m_standardOutputProcess &&
        self->m_standardOutputProcess->m_standardOutputSource == self)
        self->m_standardOutputProcess->m_standardOutputSource = NULL;
    if (self->m_standardOutputSource &&
        self->m_standardOutputSource->m_standardOutputProcess == self)
        self->m_standardOutputSource->m_standardOutputProcess = NULL;
    if (self->m_state != XProcessState_NotRunning) {
        XProcess_kill(self);
        XProcess_waitForFinished(self, -1);
    }
    XProcess_backend_deinit(self);
    if (self->m_program) XString_delete_base((XString*)self->m_program);
    if (self->m_arguments) XStringList_delete_base((XStringList*)self->m_arguments);
    if (self->m_workingDirectory) XString_delete_base((XString*)self->m_workingDirectory);
    if (self->m_standardInputFile) XString_delete_base((XString*)self->m_standardInputFile);
    if (self->m_standardOutputFile) XString_delete_base((XString*)self->m_standardOutputFile);
    if (self->m_standardErrorFile) XString_delete_base((XString*)self->m_standardErrorFile);
    if (self->m_errorString) XString_delete_base((XString*)self->m_errorString);
    XProcessEnvironment_deinit(&self->m_environment);
    XClass_Deinit_Parent(XIODevice, (XIODevice*)self);
}

bool XProcess_setProgram(XProcess* self, const XString* program)
{
    return self && program && xprocess_path_allowed(program) &&
           self->m_state == XProcessState_NotRunning &&
           xprocess_set_string(&self->m_program, program);
}

bool XProcess_setProgram_utf8(XProcess* self, const char* program)
{
    XString* value;
    bool result;
    if (!program) return false;
    value = XString_create_utf8(program);
    if (!value) return false;
    result = XProcess_setProgram(self, value);
    XString_delete_base(value);
    return result;
}

const XString* XProcess_program_const(const XProcess* self)
{
    return self ? self->m_program : NULL;
}

XString* XProcess_program(const XProcess* self)
{
    return self && self->m_program ? XString_create_copy(self->m_program) : XString_create();
}

bool XProcess_setArguments(XProcess* self, const XStringList* arguments)
{
    XStringList* copy;
    if (!self || !arguments || self->m_state != XProcessState_NotRunning) return false;
    if (XPROCESS_MAX_ARGUMENTS && XStringList_size_base(arguments) > XPROCESS_MAX_ARGUMENTS)
        return false;
    copy = XStringList_create_copy(arguments);
    if (!copy) return false;
    if (self->m_arguments) XStringList_delete_base(self->m_arguments);
    self->m_arguments = copy;
    return true;
}

bool XProcess_setArguments_utf8(XProcess* self, const char* const* arguments, size_t count)
{
    XStringList* list;
    size_t i;
    bool result;
    if (!self || (!arguments && count) || (XPROCESS_MAX_ARGUMENTS && count > XPROCESS_MAX_ARGUMENTS))
        return false;
    list = XStringList_create();
    if (!list) return false;
    for (i = 0; i < count; ++i) XStringList_push_back_utf8(list, arguments[i] ? arguments[i] : "");
    result = XProcess_setArguments(self, list);
    XStringList_delete_base(list);
    return result;
}

XStringList* XProcess_arguments(const XProcess* self)
{
    return self && self->m_arguments ? XStringList_create_copy(self->m_arguments) : XStringList_create();
}

XProcessChannelMode XProcess_processChannelMode(const XProcess* self)
{
    return self ? self->m_channelMode : XProcessChannelMode_SeparateChannels;
}

void XProcess_setProcessChannelMode(XProcess* self, XProcessChannelMode mode)
{
    if (self && mode >= XProcessChannelMode_SeparateChannels &&
        mode <= XProcessChannelMode_ForwardedErrorChannel)
        self->m_channelMode = mode;
}

XProcessInputChannelMode XProcess_inputChannelMode(const XProcess* self)
{
    return self ? self->m_inputMode : XProcessInputChannelMode_ManagedInputChannel;
}

void XProcess_setInputChannelMode(XProcess* self, XProcessInputChannelMode mode)
{
    if (self && mode <= XProcessInputChannelMode_ForwardedInputChannel)
        self->m_inputMode = mode;
}

XProcessChannel XProcess_readChannel(const XProcess* self)
{
    return self ? self->m_readChannel : XProcessChannel_StandardOutput;
}

void XProcess_setReadChannel(XProcess* self, XProcessChannel channel)
{
    if (self && channel <= XProcessChannel_StandardError) self->m_readChannel = channel;
}

void XProcess_closeReadChannel(XProcess* self, XProcessChannel channel)
{
    if (!self) return;
    if (channel == XProcessChannel_StandardOutput) self->m_stdoutClosed = true;
    else if (channel == XProcessChannel_StandardError) self->m_stderrClosed = true;
    XProcess_backend_closeReadChannel(self, channel);
}

void XProcess_closeWriteChannel(XProcess* self)
{
    if (!self) return;
    self->m_stdinClosed = true;
    if (XProcess_backend_bytesToWrite(self) == 0) XProcess_backend_closeWriteChannel(self);
}

bool XProcess_setStandardInputFile(XProcess* self, const XString* fileName)
{
#if !XPROCESS_REDIRECT_ON
    (void)self;
    (void)fileName;
    return false;
#else
    return self && xprocess_path_allowed(fileName) &&
           self->m_state == XProcessState_NotRunning &&
           xprocess_set_string(&self->m_standardInputFile, fileName);
#endif
}

bool XProcess_setStandardInputFile_utf8(XProcess* self, const char* fileName)
{
    XString* value = fileName ? XString_create_utf8(fileName) : XString_create();
    bool result;
    if (!value) return false;
    result = XProcess_setStandardInputFile(self, value);
    XString_delete_base(value);
    return result;
}

bool XProcess_setStandardOutputFile(XProcess* self, const XString* fileName, bool append)
{
#if !XPROCESS_REDIRECT_ON
    (void)self;
    (void)fileName;
    (void)append;
    return false;
#else
    if (!self || !xprocess_path_allowed(fileName) ||
        self->m_state != XProcessState_NotRunning ||
        !xprocess_set_string(&self->m_standardOutputFile, fileName))
        return false;
    self->m_stdoutAppend = append;
    return true;
#endif
}

bool XProcess_setStandardOutputFile_utf8(XProcess* self, const char* fileName, bool append)
{
    XString* value = fileName ? XString_create_utf8(fileName) : XString_create();
    bool result;
    if (!value) return false;
    result = XProcess_setStandardOutputFile(self, value, append);
    XString_delete_base(value);
    return result;
}

bool XProcess_setStandardErrorFile(XProcess* self, const XString* fileName, bool append)
{
#if !XPROCESS_REDIRECT_ON
    (void)self;
    (void)fileName;
    (void)append;
    return false;
#else
    if (!self || !xprocess_path_allowed(fileName) ||
        self->m_state != XProcessState_NotRunning ||
        !xprocess_set_string(&self->m_standardErrorFile, fileName))
        return false;
    self->m_stderrAppend = append;
    return true;
#endif
}

bool XProcess_setStandardErrorFile_utf8(XProcess* self, const char* fileName, bool append)
{
    XString* value = fileName ? XString_create_utf8(fileName) : XString_create();
    bool result;
    if (!value) return false;
    result = XProcess_setStandardErrorFile(self, value, append);
    XString_delete_base(value);
    return result;
}

bool XProcess_setStandardOutputProcess(XProcess* self, XProcess* destination)
{
#if !XPROCESS_PIPE_ON
    (void)self;
    (void)destination;
    return false;
#else
    if (!self || !destination || self == destination || self->m_state != XProcessState_NotRunning ||
        (destination->m_state != XProcessState_NotRunning &&
         destination->m_state != XProcessState_Running))
        return false;
    if (destination->m_standardOutputSource && destination->m_standardOutputSource != self)
        return false;
    if (self->m_standardOutputProcess &&
        self->m_standardOutputProcess->m_standardOutputSource == self)
        self->m_standardOutputProcess->m_standardOutputSource = NULL;
    self->m_standardOutputProcess = destination;
    destination->m_standardOutputSource = self;
    return true;
#endif
}

const XString* XProcess_workingDirectory_const(const XProcess* self)
{
    return self ? self->m_workingDirectory : NULL;
}

bool XProcess_setWorkingDirectory(XProcess* self, const XString* directory)
{
    return self && xprocess_path_allowed(directory) &&
           self->m_state == XProcessState_NotRunning &&
           xprocess_set_string(&self->m_workingDirectory, directory);
}

bool XProcess_setWorkingDirectory_utf8(XProcess* self, const char* directory)
{
    XString* value = directory ? XString_create_utf8(directory) : XString_create();
    bool result;
    if (!value) return false;
    result = XProcess_setWorkingDirectory(self, value);
    XString_delete_base(value);
    return result;
}

XString* XProcess_workingDirectory(const XProcess* self)
{
    return self && self->m_workingDirectory ? XString_create_copy(self->m_workingDirectory) : XString_create();
}

bool XProcess_setProcessEnvironment(XProcess* self, const XProcessEnvironment* environment)
{
    XProcessEnvironment* copy;
    if (!self || !environment || self->m_state != XProcessState_NotRunning) return false;
    copy = XProcessEnvironment_createCopy(environment);
    if (!copy) return false;
    XProcessEnvironment_deinit(&self->m_environment);
    self->m_environment = *copy;
    XFree_System(copy);
    return true;
}

XProcessEnvironment* XProcess_processEnvironment(const XProcess* self)
{
    return self ? XProcessEnvironment_createCopy(&self->m_environment) : NULL;
}

bool XProcess_setEnvironment(XProcess* self, const XStringList* environment)
{
    XProcessEnvironment env;
    size_t i;
    bool result;
    if (!self || !environment) return false;
    XProcessEnvironment_init(&env);
    for (i = 0; i < XStringList_size_base(environment); ++i) {
        const XString* item = XStringList_at_base(environment, i);
        const char* text = item ? XString_toUtf8(item) : NULL;
        if (text && !xprocess_insert_environment_entry(&env, text)) {
            XProcessEnvironment_deinit(&env);
            return false;
        }
    }
    result = XProcess_setProcessEnvironment(self, &env);
    XProcessEnvironment_deinit(&env);
    return result;
}

XStringList* XProcess_environment(const XProcess* self)
{
    return self ? XProcessEnvironment_toStringList(&self->m_environment) : XStringList_create();
}

XProcessError XProcess_error(const XProcess* self)
{
    return self ? self->m_error : XProcessError_UnknownError;
}

XProcessState XProcess_state(const XProcess* self)
{
    return self ? self->m_state : XProcessState_NotRunning;
}

const XString* XProcess_errorString_const(const XProcess* self)
{
    return self ? self->m_errorString : NULL;
}

XString* XProcess_errorString(const XProcess* self)
{
    return self && self->m_errorString ? XString_create_copy(self->m_errorString) : XString_create();
}

XProcessId XProcess_processId(const XProcess* self)
{
    return self ? self->m_processId : 0;
}

int XProcess_exitCode(const XProcess* self)
{
    return self ? self->m_exitCode : 0;
}

XProcessExitStatus XProcess_exitStatus(const XProcess* self)
{
    return self ? self->m_exitStatus : XProcessExitStatus_NormalExit;
}

static void xprocess_reset_start_state(XProcess* self)
{
    self->m_error = XProcessError_UnknownError;
    self->m_exitCode = 0;
    self->m_exitStatus = XProcessExitStatus_NormalExit;
    self->m_processId = 0;
    self->m_stdoutClosed = false;
    self->m_stderrClosed = false;
    self->m_stdinClosed = false;
    if (self->m_errorString) XString_clear_base(self->m_errorString);
}

bool XProcess_start(XProcess* self, const XString* program,
                    const XStringList* arguments, XIODeviceBaseMode mode)
{
    if (!self || !program) return false;
    if (!XProcess_setProgram(self, program)) return false;
    if (arguments) {
        if (!XProcess_setArguments(self, arguments)) return false;
    } else if (!XProcess_setArguments_utf8(self, NULL, 0)) {
        return false;
    }
    return XProcess_start_2(self, mode);
}

bool XProcess_start_2(XProcess* self, XIODeviceBaseMode mode)
{
    if (!self || self->m_state != XProcessState_NotRunning) return false;
    if (!self->m_program || XString_isEmpty_base(self->m_program)) {
        xprocess_set_error(self, XProcessError_FailedToStart, "No program defined");
        return false;
    }
    /* 已结束进程仍保留输出缓冲；再次启动前必须释放旧后端及其文件描述符。 */
    XProcess_backend_deinit(self);
    xprocess_reset_start_state(self);
    xprocess_set_state(self, XProcessState_Starting);
    if (!XProcess_backend_start(self, mode, false)) {
        xprocess_set_state(self, XProcessState_NotRunning);
        if (self->m_error == XProcessError_UnknownError)
            xprocess_set_error(self, XProcessError_FailedToStart, "Failed to start process");
        return false;
    }
    self->base.m_openMode = mode ? mode : XIODevice_Unbuffered;
    xprocess_set_state(self, XProcessState_Running);
    XProcess_started_signal(self);
    return true;
}

bool XProcess_open(XProcess* self, XIODeviceBaseMode mode)
{
    return self && XIODevice_open_base((XIODevice*)self, mode);
}

bool XProcess_start_utf8(XProcess* self, const char* program,
                         const char* const* arguments, size_t count,
                         XIODeviceBaseMode mode)
{
    XString* p;
    bool result;
    if (!program) return false;
    p = XString_create_utf8(program);
    if (!p) return false;
    result = XProcess_setProgram(self, p) &&
             XProcess_setArguments_utf8(self, arguments, count) &&
             XProcess_start_2(self, mode);
    XString_delete_base(p);
    return result;
}

bool XProcess_startCommand(XProcess* self, const XString* command, XIODeviceBaseMode mode)
{
    XStringList* args;
    XString* program;
    XStringList* rest;
    size_t i;
    bool result;
    if (!self || !command) return false;
    args = XProcess_splitCommand_static(command);
    if (!args || XStringList_size_base(args) == 0) {
        if (args) XStringList_delete_base(args);
        return false;
    }
    program = XString_create_copy(XStringList_at_base(args, 0));
    rest = XStringList_create();
    if (!program || !rest) {
        if (program) XString_delete_base(program);
        if (rest) XStringList_delete_base(rest);
        XStringList_delete_base(args);
        return false;
    }
    for (i = 1; i < XStringList_size_base(args); ++i)
        XStringList_push_back_base(rest, XStringList_at_base(args, i));
    result = XProcess_start(self, program, rest, mode);
    XString_delete_base(program);
    XStringList_delete_base(rest);
    XStringList_delete_base(args);
    return result;
}

bool XProcess_startDetached(XProcess* self, XProcessId* pid)
{
#if !XPROCESS_DETACHED_ON
    if (pid) *pid = -1;
    (void)self;
    return false;
#else
    if (pid) *pid = -1;
    if (!self || self->m_state != XProcessState_NotRunning || !self->m_program ||
        XString_isEmpty_base(self->m_program)) {
        if (self) xprocess_set_error(self, XProcessError_FailedToStart, "No program defined");
        return false;
    }
    /* 实例版本须保留环境、重定向、通道和 Unix 参数，不能退化为三参数静态启动。 */
    XProcess_backend_deinit(self);
    xprocess_reset_start_state(self);
    if (!XProcess_backend_start(self, XIODevice_NotOpen, true)) return false;
    if (pid) *pid = self->m_processId;
    return true;
#endif
}

bool XProcess_startDetached_static(const XString* program,
                                   const XStringList* arguments,
                                   const XString* workingDirectory,
                                   XProcessId* pid)
{
#if !XPROCESS_DETACHED_ON
    if (pid) *pid = -1;
    (void)program;
    (void)arguments;
    (void)workingDirectory;
    return false;
#else
    XStringList* emptyArguments = NULL;
    const XStringList* actualArguments = arguments;
    bool result;
    if (pid) *pid = -1;
    if (!program || XString_isEmpty_base(program)) return false;
    if (!actualArguments) {
        emptyArguments = XStringList_create();
        if (!emptyArguments) return false;
        actualArguments = emptyArguments;
    }
    result = XProcess_backend_startDetached(program, actualArguments, workingDirectory, pid);
    if (emptyArguments) XStringList_delete_base(emptyArguments);
    return result;
#endif
}

bool XProcess_poll(XProcess* self, int timeoutMsecs)
{
    bool result;
    if (!self) return false;
    result = XProcess_backend_poll(self, timeoutMsecs);
    if (!result && self->m_error == XProcessError_FailedToStart &&
        self->m_state != XProcessState_NotRunning) {
        self->m_processId = 0;
        xprocess_set_state(self, XProcessState_NotRunning);
    }
    return result;
}

static bool xprocess_wait_deadline(XProcess* self, int msecs, bool waitFinished)
{
    uint64_t start = XDateTime_currentMSecsSinceEpoch();
    uint64_t deadline = (msecs < 0) ? UINT64_MAX : start + (uint64_t)msecs;
    if (!self) return false;
    for (;;) {
        if (waitFinished && self->m_state == XProcessState_NotRunning) return false;
        if (!waitFinished && XProcess_backend_bytesAvailable(self, self->m_readChannel) > 0)
            return true;
        if (!waitFinished && self->m_state == XProcessState_NotRunning) return false;
        if (deadline != UINT64_MAX && XDateTime_currentMSecsSinceEpoch() >= deadline) {
            xprocess_set_error(self, XProcessError_Timedout, "Process operation timed out");
            return false;
        }
        XProcess_poll(self, 10);
        if (deadline == UINT64_MAX || XDateTime_currentMSecsSinceEpoch() < deadline)
            XEventLoop_delay(1);
        if (waitFinished && self->m_state == XProcessState_NotRunning) return true;
    }
}

bool XProcess_waitForStarted(XProcess* self, int msecs)
{
#if !XPROCESS_SYNC_ON
    (void)self;
    (void)msecs;
    return false;
#else
    if (!self) return false;
    if (self->m_state == XProcessState_Running) {
        XProcess_poll(self, 0);
        return self->m_state == XProcessState_Running ||
               self->m_error != XProcessError_FailedToStart;
    }
    if (self->m_state == XProcessState_NotRunning) return false;
    return xprocess_wait_deadline(self, msecs, false);
#endif
}

static bool xprocess_waitForReadyRead_impl(XProcess* self, int msecs)
{
#if !XPROCESS_SYNC_ON
    (void)self;
    (void)msecs;
    return false;
#else
    if (!self || self->m_state == XProcessState_NotRunning) return false;
    return xprocess_wait_deadline(self, msecs, false);
#endif
}

static bool xprocess_waitForBytesWritten_impl(XProcess* self, int msecs)
{
#if !XPROCESS_SYNC_ON
    (void)self;
    (void)msecs;
    return false;
#else
    uint64_t start;
    if (!self || self->m_state == XProcessState_NotRunning) return false;
    start = XDateTime_currentMSecsSinceEpoch();
    while (XProcess_backend_bytesToWrite(self) > 0) {
        if (!XProcess_backend_waitForBytesWritten(self, msecs)) return false;
        if (msecs >= 0 && XDateTime_currentMSecsSinceEpoch() - start >= (uint64_t)msecs) {
            xprocess_set_error(self, XProcessError_Timedout, "Write operation timed out");
            return false;
        }
    }
    return true;
#endif
}

bool XProcess_waitForFinished(XProcess* self, int msecs)
{
#if !XPROCESS_SYNC_ON
    (void)self;
    (void)msecs;
    return false;
#else
    if (!self || self->m_state == XProcessState_NotRunning) return false;
    return xprocess_wait_deadline(self, msecs, true);
#endif
}

static XByteArray* xprocess_read_all_channel(XProcess* self, XProcessChannel channel)
{
    XByteArray* result = XByteArray_create();
    char buffer[XPROCESS_IO_BUFFER_SIZE];
    int64_t n;
    if (!result || !self) return result;
    while ((n = XProcess_backend_read(self, channel, buffer, sizeof(buffer))) > 0)
        XByteArray_append_2(result, buffer, (size_t)n);
    return result;
}

XByteArray* XProcess_readAllStandardOutput(XProcess* self)
{
    return xprocess_read_all_channel(self, XProcessChannel_StandardOutput);
}

XByteArray* XProcess_readAllStandardError(XProcess* self)
{
    if (!self || self->m_channelMode == XProcessChannelMode_MergedChannels)
        return XByteArray_create();
    return xprocess_read_all_channel(self, XProcessChannel_StandardError);
}

static void xprocess_close_impl(XProcess* self)
{
    if (!self) return;
    if (self->m_state != XProcessState_NotRunning) {
        xprocess_waitForBytesWritten_impl(self, -1);
        XProcess_kill(self);
        XProcess_waitForFinished(self, -1);
    }
    XClass_Parent(XIODevice, EXIODevice_Close, void(*)(XIODevice*))(&self->base);
}

void XProcess_terminate(XProcess* self)
{
    if (self && self->m_state != XProcessState_NotRunning) XProcess_backend_terminate(self);
}

void XProcess_kill(XProcess* self)
{
    if (self && self->m_state != XProcessState_NotRunning) XProcess_backend_kill(self);
}

int XProcess_execute_static(const XString* program, const XStringList* arguments)
{
#if !XPROCESS_SYNC_ON
    (void)program;
    (void)arguments;
    return -2;
#else
    XProcess* self = XProcess_create();
    int result;
    if (!self || !XProcess_start(self, program, arguments, XIODevice_ReadWrite)) {
        if (self) XProcess_delete_base(self);
        return -2;
    }
    if (!XProcess_waitForFinished(self, -1)) {
        XProcess_delete_base(self);
        return -2;
    }
    result = self->m_exitStatus == XProcessExitStatus_CrashExit ? -1 : self->m_exitCode;
    XProcess_delete_base(self);
    return result;
#endif
}

XStringList* XProcess_systemEnvironment_static(void)
{
    XProcessEnvironment* env = XProcessEnvironment_systemEnvironment();
    XStringList* result = env ? XProcessEnvironment_toStringList(env) : NULL;
    if (env) XProcessEnvironment_delete(env);
    return result;
}

XString* XProcess_nullDevice_static(void)
{
    return XProcess_backend_nullDevice();
}

XStringList* XProcess_splitCommand_static(const XString* command)
{
    const char* text;
    XStringList* result;
    XString* token;
    bool inQuote = false;
    int quoteCount = 0;
    size_t i;
    if (!command) return NULL;
    text = XString_toUtf8(command);
    result = XStringList_create();
    token = XString_create();
    if (!result || !token) {
        if (result) XStringList_delete_base(result);
        if (token) XString_delete_base(token);
        return NULL;
    }
    for (i = 0; text && text[i]; ++i) {
        char c = text[i];
        if (c == '"') {
            ++quoteCount;
            if (quoteCount == 3) {
                XString_append_utf8(token, "\"");
                quoteCount = 0;
            }
            continue;
        }
        if (quoteCount) {
            if (quoteCount == 1) inQuote = !inQuote;
            quoteCount = 0;
        }
        if (!inQuote && isspace((unsigned char)c)) {
            if (!XString_isEmpty_base(token)) {
                XStringList_push_back_base(result, token);
                XString_clear_base(token);
            }
        } else {
            char one[2] = { c, '\0' };
            XString_append_utf8(token, one);
        }
    }
    if (!XString_isEmpty_base(token)) XStringList_push_back_base(result, token);
    XString_delete_base(token);
    return result;
}

XProcessUnixProcessParameters XProcess_unixProcessParameters(const XProcess* self)
{
    XProcessUnixProcessParameters result;
    memset(&result, 0, sizeof(result));
#if XPROCESS_UNIX_PARAMETERS_ON
    if (self) result = self->m_unixParameters;
#else
    (void)self;
#endif
    return result;
}

bool XProcess_setUnixProcessParameters(XProcess* self,
                                       const XProcessUnixProcessParameters* parameters)
{
#if !XPROCESS_UNIX_PARAMETERS_ON
    (void)self;
    (void)parameters;
    return false;
#else
    size_t i;
    if (!self || !parameters || self->m_state != XProcessState_NotRunning) return false;
    for (i = 0; i < sizeof(parameters->reserved) / sizeof(parameters->reserved[0]); ++i)
        if (parameters->reserved[i] != 0) return false;
    self->m_unixParameters = *parameters;
    return true;
#endif
}

bool XProcess_setUnixProcessParameters_flags(XProcess* self, uint32_t flags)
{
    XProcessUnixProcessParameters parameters;
    memset(&parameters, 0, sizeof(parameters));
    parameters.flags = flags;
    return XProcess_setUnixProcessParameters(self, &parameters);
}

void XProcess_backend_notifyFinished(XProcess* self, int exitCode,
                                     XProcessExitStatus status, bool crashed)
{
    if (!self || self->m_state == XProcessState_NotRunning) return;
    self->m_exitCode = exitCode;
    self->m_exitStatus = status;
    self->m_processId = 0;
    if (crashed) xprocess_set_error(self, XProcessError_Crashed, "Process crashed");
    xprocess_set_state(self, XProcessState_NotRunning);
    XProcess_finished_signal(self, exitCode, status);
}

void XProcess_backend_setError(XProcess* self, XProcessError error, const char* text)
{
    xprocess_set_error(self, error, text);
}

static void VXProcess_close(XIODevice* io)
{
    xprocess_close_impl((XProcess*)io);
}

static bool VXProcess_open(XIODevice* io, XIODeviceBaseMode mode)
{
    XProcess* self = (XProcess*)io;
    if (!self || mode == XIODevice_NotOpen) return false;
    return XProcess_start_2(self, mode);
}

static bool VXProcess_isSequential(const XIODevice* io)
{
    (void)io;
    return true;
}

static int64_t VXProcess_bytesAvailable(const XIODevice* io)
{
    const XProcess* self = (const XProcess*)io;
    return self ? XProcess_backend_bytesAvailable(self, self->m_readChannel) : 0;
}

static int64_t VXProcess_bytesToWrite(const XIODevice* io)
{
    return io ? XProcess_backend_bytesToWrite((const XProcess*)io) : 0;
}

static bool VXProcess_waitForReadyRead(XIODevice* io, int msecs)
{
    return xprocess_waitForReadyRead_impl((XProcess*)io, msecs);
}

static bool VXProcess_waitForBytesWritten(XIODevice* io, int msecs)
{
    return xprocess_waitForBytesWritten_impl((XProcess*)io, msecs);
}

static int64_t VXProcess_readData(XIODevice* io, char* data, int64_t maxlen)
{
    XProcess* self = (XProcess*)io;
    if (!self || !data || maxlen <= 0 || !(self->base.m_openMode & XIODevice_ReadOnly)) return -1;
    return XProcess_backend_read(self, self->m_readChannel, data, maxlen);
}

static int64_t VXProcess_writeData(XIODevice* io, const char* data, int64_t len)
{
    XProcess* self = (XProcess*)io;
    if (!self || !data || len < 0 || self->m_stdinClosed ||
        !(self->base.m_openMode & XIODevice_WriteOnly)) return -1;
    return XProcess_backend_write(self, data, len);
}

void* XProcess_started_signal(XProcess* self)
{
#if !XPROCESS_SIGNAL_ON
    (void)self;
    return NULL;
#else
    if (!self) return NULL;
    if (((XObject*)self)->m_signalSlot) {
        XObject_emitSignal((XObject*)self, (size_t)XProcess_started_signal,
                           NULL, NULL, NULL, XEVENT_PRIORITY_NORMAL);
    }
    return (void*)(size_t)XProcess_started_signal;
#endif
}

void* XProcess_finished_signal(XProcess* self, int exitCode, XProcessExitStatus status)
{
#if !XPROCESS_SIGNAL_ON
    (void)self;
    (void)exitCode;
    (void)status;
    return NULL;
#else
    if (!self) return NULL;
    if (((XObject*)self)->m_signalSlot) {
        XObject_emitSignal((XObject*)self, (size_t)XProcess_finished_signal,
                           XVarList_Create(XVar(int, exitCode),
                                           XVar(XProcessExitStatus, status)),
                           NULL, NULL, XEVENT_PRIORITY_NORMAL);
    }
    return (void*)(size_t)XProcess_finished_signal;
#endif
}

void* XProcess_errorOccurred_signal(XProcess* self, XProcessError error)
{
#if !XPROCESS_SIGNAL_ON
    (void)self;
    (void)error;
    return NULL;
#else
    if (!self) return NULL;
    if (((XObject*)self)->m_signalSlot) {
        XObject_emitSignal((XObject*)self, (size_t)XProcess_errorOccurred_signal,
                           XVarList_Create(XVar(XProcessError, error)),
                           NULL, NULL, XEVENT_PRIORITY_NORMAL);
    }
    return (void*)(size_t)XProcess_errorOccurred_signal;
#endif
}

void* XProcess_stateChanged_signal(XProcess* self, XProcessState state)
{
#if !XPROCESS_SIGNAL_ON
    (void)self;
    (void)state;
    return NULL;
#else
    if (!self) return NULL;
    if (((XObject*)self)->m_signalSlot) {
        XObject_emitSignal((XObject*)self, (size_t)XProcess_stateChanged_signal,
                           XVarList_Create(XVar(XProcessState, state)),
                           NULL, NULL, XEVENT_PRIORITY_NORMAL);
    }
    return (void*)(size_t)XProcess_stateChanged_signal;
#endif
}

void* XProcess_readyReadStandardOutput_signal(XProcess* self)
{
#if !XPROCESS_SIGNAL_ON
    (void)self;
    return NULL;
#else
    if (!self) return NULL;
    if (((XObject*)self)->m_signalSlot) {
        XObject_emitSignal((XObject*)self, (size_t)XProcess_readyReadStandardOutput_signal,
                           NULL, NULL, NULL, XEVENT_PRIORITY_NORMAL);
    }
    return (void*)(size_t)XProcess_readyReadStandardOutput_signal;
#endif
}

void* XProcess_readyReadStandardError_signal(XProcess* self)
{
#if !XPROCESS_SIGNAL_ON
    (void)self;
    return NULL;
#else
    if (!self) return NULL;
    if (((XObject*)self)->m_signalSlot) {
        XObject_emitSignal((XObject*)self, (size_t)XProcess_readyReadStandardError_signal,
                           NULL, NULL, NULL, XEVENT_PRIORITY_NORMAL);
    }
    return (void*)(size_t)XProcess_readyReadStandardError_signal;
#endif
}

#endif /* XProcess_ON */
