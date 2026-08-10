/**
 * @file XConsoleShellTest.c
 * @brief XConsoleShell 全量核心回归测试。
 * @details
 * 模拟传输只使用固定数组，write 每次故意短写以验证 Shell 的背压循环。
 * 文件测试通过 XFileSystem 公共 API 创建和清理测试文件，不使用平台文件接口。
 */

#include "XConsoleShellTest.h"
#include "CXinYueConfig.h"

#if XCONSOLE_SHELL_ON

#include "XConsoleShell.h"
#include "XFileSystem.h"
#include "XString.h"
#include "XMemory.h"
#include "XPrintf.h"
#include "XDateTime.h"
#if XCONSOLE_SHELL_ASYNC_ON
#include "XCoreApplication.h"
#include "XEventLoop.h"
#endif
#if XCONSOLE_SHELL_RESET_ON || XCONSOLE_SHELL_REBOOT_ON || XCONSOLE_SHELL_SHUTDOWN_ON
#include "XSystem.h"
#endif
#if XCONSOLE_SHELL_NETWORK_ON
#include "XNetwork.h"
#endif
#if XCONSOLE_SHELL_MEMORY_ON && XCONSOLE_SHELL_MEMORY_POOL_ON
#include "XMultiPool.h"
#endif
#if XCONSOLE_SHELL_TELNET_PROTOCOL_ON
#include "XConsoleShell_Telnet.h"
#endif
#if XCONSOLE_SHELL_XSERIALPORT_BACKEND_ON
#include "XConsoleShell_XSerialPort.h"
#endif
#if XCONSOLE_SHELL_XTCPSOCKET_BACKEND_ON
#include "XConsoleShell_XTcpSocket.h"
#endif
#if XCONSOLE_SHELL_XTCPSERVER_BACKEND_ON
#include "XConsoleShell_XTcpServer.h"
#endif
#include <string.h>

typedef struct XConsoleShellTestTransport {
    char output[4096];
    size_t length;
    uint8_t input[512];
    size_t inputLength;
    size_t inputPosition;
#if XCONSOLE_SHELL_LOG_ON
    size_t logCount;
    char lastLog[128];
#endif
#if XCONSOLE_SHELL_AUDIT_ON
    size_t auditCount;
    int lastAuditResult;
    const XConsoleCommand* lastAuditCommand;
#endif
#if XCONSOLE_SHELL_RESET_ON
    size_t resetCount;
    XSystemResetReason resetReason;
    XSystemResult resetResult;
#endif
#if XCONSOLE_SHELL_REBOOT_ON
    size_t rebootCount;
    XSystemRebootMode rebootMode;
    XSystemResult rebootResult;
#endif
#if XCONSOLE_SHELL_SHUTDOWN_ON
    size_t shutdownCount;
    XSystemResult shutdownResult;
#endif
#if XCONSOLE_SHELL_GPIO_ON
    size_t gpioAuthorizeCount;
    bool gpioAllow;
#endif
#if XCONSOLE_SHELL_CAN_ON
    size_t canAuthorizeCount;
    bool canAllow;
#endif
#if XCONSOLE_SHELL_ADC_ON
    size_t adcAuthorizeCount;
    bool adcAllow;
#endif
#if XCONSOLE_SHELL_PWM_ON
    size_t pwmAuthorizeCount;
    bool pwmAllow;
#endif
#if XCONSOLE_SHELL_I2C_ON
    size_t i2cAuthorizeCount;
    bool i2cAllow;
#endif
#if XCONSOLE_SHELL_SPI_ON
    size_t spiAuthorizeCount;
    bool spiAllow;
#endif
} XConsoleShellTestTransport;

#if XCONSOLE_SHELL_GPIO_ON
static bool XConsoleShellTest_gpioAuthorize(
    void* userData, const XConsoleShellSession* session,
    const XGpioPin* pin, XConsoleShellGpioOperation operation)
{
    XConsoleShellTestTransport* transport =
        (XConsoleShellTestTransport*)userData;
    (void)operation;
    if (!transport || !session || !pin) return false;
    ++transport->gpioAuthorizeCount;
    return transport->gpioAllow;
}
#endif

#if XCONSOLE_SHELL_CAN_ON
static bool XConsoleShellTest_canAuthorize(
    void* userData, const XConsoleShellSession* session,
    const XCanChannel* channel, XConsoleShellCanOperation operation)
{
    XConsoleShellTestTransport* transport =
        (XConsoleShellTestTransport*)userData;
    (void)operation;
    if (!transport || !session || !channel) return false;
    ++transport->canAuthorizeCount;
    return transport->canAllow;
}
#endif

#if XCONSOLE_SHELL_ADC_ON
static bool XConsoleShellTest_adcAuthorize(
    void* userData, const XConsoleShellSession* session,
    const XAdcChannel* channel, XConsoleShellAdcOperation operation)
{
    XConsoleShellTestTransport* transport =
        (XConsoleShellTestTransport*)userData;
    (void)operation;
    if (!transport || !session || !channel) return false;
    ++transport->adcAuthorizeCount;
    return transport->adcAllow;
}
#endif

#if XCONSOLE_SHELL_PWM_ON
static bool XConsoleShellTest_pwmAuthorize(
    void* userData, const XConsoleShellSession* session,
    const XPwmChannel* channel, XConsoleShellPwmOperation operation)
{
    XConsoleShellTestTransport* transport =
        (XConsoleShellTestTransport*)userData;
    (void)operation;
    if (!transport || !session || !channel) return false;
    ++transport->pwmAuthorizeCount;
    return transport->pwmAllow;
}
#endif

#if XCONSOLE_SHELL_I2C_ON
static bool XConsoleShellTest_i2cAuthorize(
    void* userData, const XConsoleShellSession* session,
    const XI2cTarget* target, XConsoleShellI2cOperation operation)
{
    XConsoleShellTestTransport* transport =
        (XConsoleShellTestTransport*)userData;
    (void)operation;
    if (!transport || !session || !target) return false;
    ++transport->i2cAuthorizeCount;
    return transport->i2cAllow;
}
#endif

#if XCONSOLE_SHELL_SPI_ON
static bool XConsoleShellTest_spiAuthorize(
    void* userData, const XConsoleShellSession* session,
    const XSpiConfig* config, XConsoleShellSpiOperation operation)
{
    XConsoleShellTestTransport* transport =
        (XConsoleShellTestTransport*)userData;
    (void)operation;
    if (!transport || !session || !config) return false;
    ++transport->spiAuthorizeCount;
    return transport->spiAllow;
}
#endif

#if XCONSOLE_SHELL_RESET_ON
static XSystemResult XConsoleShellTest_resetHandler(void* userData,
                                                    XSystemResetReason reason)
{
    XConsoleShellTestTransport* transport =
        (XConsoleShellTestTransport*)userData;
    if (!transport) return XSystemResult_Failed;
    ++transport->resetCount;
    transport->resetReason = reason;
    return transport->resetResult;
}
#endif

#if XCONSOLE_SHELL_REBOOT_ON
static XSystemResult XConsoleShellTest_rebootHandler(void* userData,
                                                     XSystemRebootMode mode)
{
    XConsoleShellTestTransport* transport =
        (XConsoleShellTestTransport*)userData;
    if (!transport) return XSystemResult_Failed;
    ++transport->rebootCount;
    transport->rebootMode = mode;
    return transport->rebootResult;
}
#endif

#if XCONSOLE_SHELL_SHUTDOWN_ON
static XSystemResult XConsoleShellTest_shutdownHandler(void* userData)
{
    XConsoleShellTestTransport* transport =
        (XConsoleShellTestTransport*)userData;
    if (!transport) return XSystemResult_Failed;
    ++transport->shutdownCount;
    return transport->shutdownResult;
}
#endif

static int64_t XConsoleShellTest_read(void* userData, void* data, size_t size)
{
    XConsoleShellTestTransport* transport = (XConsoleShellTestTransport*)userData;
    size_t count;
    if (!transport || (!data && size)) return -1;
    if (transport->inputPosition >= transport->inputLength) return 0;
    count = transport->inputLength - transport->inputPosition;
    if (count > size) count = size;
    if (count > 3) count = 3;
    memcpy(data, transport->input + transport->inputPosition, count);
    transport->inputPosition += count;
    return (int64_t)count;
}

static int64_t XConsoleShellTest_write(void* userData, const void* data, size_t size)
{
    XConsoleShellTestTransport* transport = (XConsoleShellTestTransport*)userData;
    size_t count = size > 3 ? 3 : size;
    if (!transport || count > sizeof(transport->output) - transport->length - 1) return -1;
    memcpy(transport->output + transport->length, data, count);
    transport->length += count;
    transport->output[transport->length] = '\0';
    return (int64_t)count;
}

static bool XConsoleShellTest_flush(void* userData)
{
    return userData != NULL;
}

#if XCONSOLE_SHELL_LOG_ON
static void XConsoleShellTest_log(void* userData, const XConsoleShellSession* session,
                                  const void* data, size_t size)
{
    XConsoleShellTestTransport* transport = (XConsoleShellTestTransport*)userData;
    size_t count;
    (void)session;
    if (!transport || !data) return;
    count = size < sizeof(transport->lastLog) - 1u ? size : sizeof(transport->lastLog) - 1u;
    memcpy(transport->lastLog, data, count);
    transport->lastLog[count] = '\0';
    ++transport->logCount;
}
#endif

#if XCONSOLE_SHELL_AUDIT_ON
static void XConsoleShellTest_audit(void* userData,
                                    const XConsoleShellSession* session,
                                    const XConsoleCommand* command,
                                    int result)
{
    XConsoleShellTestTransport* transport = (XConsoleShellTestTransport*)userData;
    (void)session;
    if (!transport) return;
    ++transport->auditCount;
    transport->lastAuditResult = result;
    transport->lastAuditCommand = command;
}
#endif

static int XConsoleShellTest_custom(XConsoleShell* shell, XConsoleShellSession* session,
                                    int argc, const char* const* argv, void* userData)
{
    (void)session;
    (void)userData;
    if (argc != 1) return XConsoleResult_InvalidArgument;
    return XConsoleShell_writeUtf8(shell, argv[0]) ? XConsoleResult_Ok : XConsoleResult_IoError;
}

static const XConsoleCommand g_custom = {
    "custom", "c", "测试命令", "custom <value>", 1, 1, 0,
    XConsoleShellTest_custom, NULL, 0, NULL
};

#if XCONSOLE_SHELL_TASKS_ON
static XConsoleResult XConsoleShellTest_taskProvider(
    void* userData, XConsoleShell* shell, XConsoleShellSession* session,
    XConsoleShellTaskEmitFn emit, void* emitUserData)
{
    XConsoleShellTaskInfo info;
    XConsoleResult result;
    (void)userData;
    (void)shell;
    (void)session;
    if (!emit) return XConsoleResult_InvalidArgument;
    info.name = "shell-main";
    info.id = 1u;
    info.priority = 4;
    info.stackSize = 2048u;
    info.stackFree = 1024u;
    info.state = XConsoleShellTaskState_Running;
    result = emit(emitUserData, &info);
    if (result != XConsoleResult_Ok) return result;
    info.name = "shell-worker";
    info.id = 2u;
    info.priority = 3;
    info.stackSize = 4096u;
    info.stackFree = 3072u;
    info.state = XConsoleShellTaskState_Blocked;
    return emit(emitUserData, &info);
}
#endif

static int XConsoleShellTest_selfUnregister(XConsoleShell* shell,
                                            XConsoleShellSession* session,
                                            int argc, const char* const* argv,
                                            void* userData)
{
    (void)session;
    (void)argc;
    (void)argv;
    (void)userData;
#if XCONSOLE_SHELL_DYNAMIC_REGISTER_ON
    if (!XConsoleShell_unregisterCommand(shell, "selfremove"))
        return XConsoleResult_Failed;
#endif
    return XConsoleResult_Ok;
}

#if XCONSOLE_SHELL_MULTI_SESSION_ON && XCONSOLE_SHELL_ASYNC_OUTPUT_ON && \
    XCONSOLE_SHELL_DYNAMIC_REGISTER_ON
static int XConsoleShellTest_outputThenFail(XConsoleShell* shell,
                                             XConsoleShellSession* session,
                                             int argc, const char* const* argv,
                                             void* userData)
{
    (void)session;
    (void)argc;
    (void)argv;
    (void)userData;
    if (!XConsoleShell_writeUtf8(shell, "secondary-failure"))
        return XConsoleResult_IoError;
    return XConsoleResult_Failed;
}
#endif

#define XCS_TEST_CHECK(condition, text) \
    do { if (!(condition)) { XPrintf("[FAIL] XConsoleShell: %s\n", text); return false; } } while (0)

static bool XConsoleShellTest_runLine(XConsoleShell* shell,
                                      XConsoleShellTestTransport* transport,
                                      const char* line,
                                      XConsoleResult expected,
                                      const char* expectedText)
{
    XConsoleResult result;
    if (!shell || !transport || !line) return false;
    transport->length = 0;
    transport->output[0] = '\0';
    result = XConsoleShell_processLine(shell, line, strlen(line));
#if XCONSOLE_SHELL_ASYNC_OUTPUT_ON
    if (!XConsoleShell_flushOutput(shell)) return false;
#endif
    if (result != expected || (expectedText && !strstr(transport->output, expectedText))) {
        XPrintf("[FAIL] Shell 命令: %s, result=%d, expected=%d, output=%s\n",
                line, result, expected, transport->output);
        return false;
    }
    return true;
}

/**
 * @brief 验证固定缓冲命令循环和对象生命周期在长时间运行后仍保持可用。
 * @details
 * 每轮清空模拟传输，避免测试缓存本身限制循环次数；对象循环同时覆盖内置命令
 * 注册、可选动态存储清理和 XObject 析构链。该测试不创建线程或调用平台 API。
 */
static bool XConsoleShellTest_runStress(XConsoleShell* shell,
                                        XConsoleShellTestTransport* transport,
                                        const XConsoleShellIo* io)
{
    size_t i;
    if (!shell || !transport || !io) return false;
    for (i = 0; i < 100000u; ++i) {
        if (!XConsoleShellTest_runLine(shell, transport, "echo load",
                                       XConsoleResult_Ok, "load"))
            return false;
    }
    for (i = 0; i < 10000u; ++i) {
        XConsoleShell* temporary = XConsoleShell_create(io);
        if (!temporary) return false;
        XConsoleShell_delete_base(temporary);
    }
    return true;
}

#if XCONSOLE_SHELL_ASYNC_OUTPUT_ON
/* 异步输出配置下，测试调用方负责驱动发送队列。 */
static XConsoleResult XConsoleShellTest_processLine(XConsoleShell* shell,
                                                     const char* line,
                                                     size_t length)
{
    XConsoleResult result = XConsoleShell_processLine(shell, line, length);
    if (!XConsoleShell_flushOutput(shell)) return XConsoleResult_IoError;
    return result;
}

static XConsoleResult XConsoleShellTest_feedData(XConsoleShell* shell,
                                                  const void* data,
                                                  size_t size)
{
    XConsoleResult result = XConsoleShell_feedData(shell, data, size);
    if (!XConsoleShell_flushOutput(shell)) return XConsoleResult_IoError;
    return result;
}

#define XConsoleShell_processLine XConsoleShellTest_processLine
#define XConsoleShell_feedData XConsoleShellTest_feedData
#endif

#if XCONSOLE_SHELL_EDITOR_ON
/** @brief 全屏 TUI 编辑器测试：逐字节喂给 Shell 并验证返回结果与输出内容。 */
static bool XConsoleShellTest_feedEditor(XConsoleShell* shell,
                                         XConsoleShellTestTransport* transport,
                                         const char* data, size_t length,
                                         XConsoleResult expected,
                                         const char* expectedText)
{
    XConsoleResult result;
    if (!shell || !transport || (!data && length)) return false;
    transport->length = 0;
    transport->output[0] = '\0';
    result = XConsoleShell_feedData(shell, data, length);
#if XCONSOLE_SHELL_ASYNC_OUTPUT_ON
    if (!XConsoleShell_flushOutput(shell)) return false;
#endif
    if (result != expected || (expectedText && !strstr(transport->output, expectedText))) {
        XPrintf("[FAIL] Shell 编辑器字节输入长度=%zu, result=%d, expected=%d, output=%s\n",
                length, result, expected, transport->output);
        return false;
    }
    return true;
}
#endif

static bool XConsoleShellTest_runFileCommands(
    XConsoleShell* shell, XConsoleShellTestTransport* transport)
{
    XString* temp = XString_create();
    XString* root = XString_create();
    XString* source = NULL;
    XString* copy = NULL;
    XString* moved = NULL;
    XString* link = NULL;
    XString* linkTarget = NULL;
    XString* nested = NULL;
#if XCONSOLE_SHELL_FS_LS_ON
    XString* longName = NULL;
    XString* longPath = NULL;
    char longNameText[161];
    XFd longFd;
    int longError = 0;
#endif
    XFileStat stat;
    bool ok = false;
    bool rootCreated = false;
#if XCONSOLE_SHELL_PERMISSION_ON
    XConsoleShellSession* permissionSession;
    uint32_t savedPermissionMask;
#endif

    if (!temp || !root ||
        !XFileSystem_getSpecialPath(XSpecialPath_Temp, temp) ||
        !XString_assign_fmt_utf8(root, "%s/xconsole-shell-linux-%lld",
                                 XString_toUtf8(temp),
                                 (long long)XDateTime_currentMSecsSinceEpoch())) {
        goto cleanup;
    }
    if (XFileSystem_exists(root) || !XFileSystem_mkdir(root, false)) goto cleanup;
    rootCreated = true;

    source = XString_create_fmt_utf8("%s/source.txt", XString_toUtf8(root));
    copy = XString_create_fmt_utf8("%s/copy.txt", XString_toUtf8(root));
    moved = XString_create_fmt_utf8("%s/moved.txt", XString_toUtf8(root));
    link = XString_create_fmt_utf8("%s/link.txt", XString_toUtf8(root));
    linkTarget = XString_create();
    nested = XString_create_fmt_utf8("%s/nested/deep", XString_toUtf8(root));
    if (!source || !copy || !moved || !link || !linkTarget || !nested) goto cleanup;
#if XCONSOLE_SHELL_FS_LS_ON
    /* 160 字节名称可覆盖 192 字节旧缓冲区，同时仍符合常见后端路径上限。 */
    memset(longNameText, 'l', sizeof(longNameText) - 1u);
    longNameText[sizeof(longNameText) - 1u] = '\0';
    longName = XString_create_utf8(longNameText);
    longPath = XString_create_fmt_utf8("%s/%s", XString_toUtf8(root), longNameText);
    if (!longName || !longPath) goto cleanup;
#endif

    {
        XString* command = XString_create_fmt_utf8("fs cd %s", XString_toUtf8(root));
        if (!command || !XConsoleShellTest_runLine(shell, transport,
                XString_toUtf8(command), XConsoleResult_Ok, NULL)) {
            if (command) XString_delete_base(command);
            goto cleanup;
        }
        XString_delete_base(command);
    }
    if (!XConsoleShellTest_runLine(shell, transport, "fs pwd", XConsoleResult_Ok,
                                   XString_toUtf8(root))) goto cleanup;
#if XCONSOLE_SHELL_FS_LS_ON
    longFd = XFileSystem_open(longPath, XFileSystem_WriteOnly | XFileSystem_Create |
                              XFileSystem_Truncate, &longError);
    if (longFd == XFD_INVALID) goto cleanup;
    if (XFileSystem_write(longFd, "x", 1) != 1) {
        XFileSystem_close(longFd);
        goto cleanup;
    }
    XFileSystem_close(longFd);
    {
        XString* command = XString_create_fmt_utf8("ls %s", XString_toUtf8(longName));
        if (!command || !XConsoleShellTest_runLine(shell, transport,
                XString_toUtf8(command), XConsoleResult_Ok,
                XString_toUtf8(longName))) {
            if (command) XString_delete_base(command);
            goto cleanup;
        }
        XString_delete_base(command);
        command = XString_create_fmt_utf8("ls -l %s", XString_toUtf8(longName));
        if (!command || !XConsoleShellTest_runLine(shell, transport,
                XString_toUtf8(command), XConsoleResult_Ok,
                XString_toUtf8(longName))) {
            if (command) XString_delete_base(command);
            goto cleanup;
        }
        XString_delete_base(command);
    }
#endif
    if (!XConsoleShellTest_runLine(shell, transport, "fs mkdir nested/deep --parents",
                                   XConsoleResult_Ok, NULL) ||
        !XFileSystem_stat(nested, &stat) || !stat.isDir) goto cleanup;
    if (!XConsoleShellTest_runLine(shell, transport,
                                   "fs write source.txt alpha beta gamma",
                                   XConsoleResult_Ok, NULL)) goto cleanup;
    if (!XFileSystem_stat(source, &stat) || !stat.isFile || stat.size != 16) goto cleanup;
    if (!XConsoleShellTest_runLine(shell, transport, "fs cat source.txt",
                                   XConsoleResult_Ok, "alpha beta gamma")) goto cleanup;
    if (!XConsoleShellTest_runLine(shell, transport,
                                   "fs cat source.txt --offset 6 --length 4",
                                   XConsoleResult_Ok, "beta")) goto cleanup;
    if (!XConsoleShellTest_runLine(shell, transport, "fs stat source.txt",
                                   XConsoleResult_Ok, "类型=文件")) goto cleanup;
    if (!XConsoleShellTest_runLine(shell, transport, "fs ls .",
                                   XConsoleResult_Ok, "source.txt")) goto cleanup;
    if (!XConsoleShellTest_runLine(shell, transport, "fs cp source.txt copy.txt",
                                   XConsoleResult_Ok, NULL) ||
        !XFileSystem_stat(copy, &stat) || !stat.isFile) goto cleanup;
    if (!XConsoleShellTest_runLine(shell, transport, "fs mv copy.txt moved.txt",
                                   XConsoleResult_Ok, NULL) ||
        XFileSystem_exists(copy) || !XFileSystem_exists(moved)) goto cleanup;
    if (!XConsoleShellTest_runLine(shell, transport, "fs link source.txt link.txt",
                                   XConsoleResult_Ok, NULL) ||
        !XFileSystem_exists(link)) goto cleanup;
    if (!XConsoleShellTest_runLine(shell, transport, "fs cat link.txt",
                                   XConsoleResult_Ok, "alpha beta gamma")) goto cleanup;
    if (!XConsoleShellTest_runLine(shell, transport, "fs rm link.txt",
                                   XConsoleResult_Ok, NULL) || XFileSystem_exists(link)) goto cleanup;
#if XCONSOLE_SHELL_FS_LN_ON && XCONSOLE_SHELL_FS_UNLINK_ON
    if (!XConsoleShellTest_runLine(shell, transport, "ln -s source.txt link.txt",
                                   XConsoleResult_Ok, NULL) ||
        !XConsoleShellTest_runLine(shell, transport, "unlink link.txt",
                                   XConsoleResult_Ok, NULL) || XFileSystem_exists(link)) goto cleanup;
#endif
    if (!XConsoleShellTest_runLine(shell, transport, "fs ls .",
                                   XConsoleResult_Ok, "source.txt")) goto cleanup;
    if (strstr(transport->output, "link.txt")) goto cleanup;
    if (!XConsoleShellTest_runLine(shell, transport, "rm -rf nested",
                                   XConsoleResult_Ok, NULL) || XFileSystem_exists(nested)) goto cleanup;
    if (!XConsoleShellTest_runLine(shell, transport, "fs mkdir nested/deep -p",
                                   XConsoleResult_Ok, NULL) ||
        !XConsoleShellTest_runLine(shell, transport, "rm -rf nested",
                                   XConsoleResult_Ok, NULL) || XFileSystem_exists(nested)) goto cleanup;
    if (!XConsoleShellTest_runLine(shell, transport, "fs ls .",
                                   XConsoleResult_Ok, "source.txt")) goto cleanup;
    if (!XFileSystem_stat(source, &stat)) {
        XPrintf("[FAIL] 删除前源文件不存在: %s\n", XString_toUtf8(source));
        goto cleanup;
    }
#if XCONSOLE_SHELL_PERMISSION_ON
    permissionSession = XConsoleShell_session(shell);
    if (!permissionSession) goto cleanup;
    savedPermissionMask = permissionSession->permissionMask;
    permissionSession->permissionMask = 0;
    if (!XConsoleShellTest_runLine(shell, transport, "fs rm source.txt",
                                   XConsoleResult_PermissionDenied, NULL)) {
        permissionSession->permissionMask = savedPermissionMask;
        goto cleanup;
    }
    permissionSession->permissionMask = savedPermissionMask;
#endif
    if (!XConsoleShellTest_runLine(shell, transport, "fs rm source.txt",
                                   XConsoleResult_Ok, NULL) ||
        !XConsoleShellTest_runLine(shell, transport, "fs rm moved.txt",
                                   XConsoleResult_Ok, NULL) ||
        XFileSystem_exists(source) || XFileSystem_exists(moved)) goto cleanup;
#if XCONSOLE_SHELL_FS_FORMAT_ON
    if (!XConsoleShellTest_runLine(shell, transport, "fs format .",
                                   XConsoleResult_Failed, NULL)) goto cleanup;
#endif
    ok = true;

cleanup:
    if (rootCreated && root) XFileSystem_rmdir(root, true);
    if (source) XString_delete_base(source);
    if (copy) XString_delete_base(copy);
    if (moved) XString_delete_base(moved);
    if (link) XString_delete_base(link);
    if (linkTarget) XString_delete_base(linkTarget);
    if (nested) XString_delete_base(nested);
#if XCONSOLE_SHELL_FS_LS_ON
    if (longName) XString_delete_base(longName);
    if (longPath) XString_delete_base(longPath);
#endif
    if (root) XString_delete_base(root);
    if (temp) XString_delete_base(temp);
    return ok;
}

#if XCONSOLE_SHELL_EDITOR_ON
/**
 * @brief 验证 vi/vim 行编辑命令的插入、保存、未修改退出和放弃退出。
 * @details
 * 使用临时目录创建/清理测试文件，通过 Shell 输入状态机逐行驱动编辑器。
 */
static bool XConsoleShellTest_runEditorCommands(
    XConsoleShell* shell, XConsoleShellTestTransport* transport)
{
    XString* temp = XString_create();
    XString* root = XString_create();
    XString* file = NULL;
    XString* command = NULL;
    char content[512];
    XFd fd;
    int error = 0;
    int64_t size;
    bool ok = false;
    bool rootCreated = false;

    if (!temp || !root ||
        !XFileSystem_getSpecialPath(XSpecialPath_Temp, temp) ||
        !XString_assign_fmt_utf8(root, "%s/xconsole-shell-vi-%lld",
                                 XString_toUtf8(temp),
                                 (long long)XDateTime_currentMSecsSinceEpoch()))
        goto cleanup;
    if (XFileSystem_exists(root) || !XFileSystem_mkdir(root, false)) goto cleanup;
    rootCreated = true;
    file = XString_create_fmt_utf8("%s/edit.txt", XString_toUtf8(root));
    if (!file) goto cleanup;
    command = XString_create_fmt_utf8("vi %s", XString_toUtf8(file));
    if (!command) goto cleanup;

#if XCONSOLE_SHELL_EDITOR_TUI_ON && XTUI_ON && XTUI_VIM_ON
    /* 打开新文件进入全屏 vim，随后用字节流驱动 XTui 状态机。 */
    if (!XConsoleShellTest_runLine(shell, transport, XString_toUtf8(command),
                                   XConsoleResult_MoreOutput, NULL))
        goto cleanup;
    XString_delete_base(command);
    command = NULL;
    /* i 进入插入模式，输入内容后 ESC 返回命令模式。 */
    if (!XConsoleShellTest_feedEditor(shell, transport, "i", 1,
                                      XConsoleResult_MoreOutput, NULL))
        goto cleanup;
    /* TUI 输出带 ANSI 光标控制序列，字符间不连续；文本正确性由
       保存后的文件内容断言保证。 */
    if (!XConsoleShellTest_feedEditor(shell, transport, "hello", 5,
                                      XConsoleResult_MoreOutput, NULL))
        goto cleanup;
    /* 插入模式方向键：只移动光标，不得输出 abcd 或退出插入模式。 */
    if (!XConsoleShellTest_feedEditor(shell, transport,
                                      "\x1b[D\x1b[C\x1b[A\x1b[B", 12,
                                      XConsoleResult_MoreOutput, NULL))
        goto cleanup;
    if (!XConsoleShellTest_feedEditor(shell, transport, "\x1b", 1,
                                      XConsoleResult_MoreOutput, NULL))
        goto cleanup;
    /* :wq 保存并退出。 */
    if (!XConsoleShellTest_feedEditor(shell, transport, ":wq\n", 4,
                                      XConsoleResult_Ok, NULL))
        goto cleanup;
    fd = XFileSystem_open(file, XFileSystem_ReadOnly, &error);
    if (fd == XFD_INVALID) goto cleanup;
    size = XFileSystem_read(fd, content, (int64_t)sizeof(content) - 1);
    XFileSystem_close(fd);
    if (size <= 0) goto cleanup;
    content[size] = '\0';
    if (strstr(content, "hello") == NULL || strstr(content, "abcd") != NULL)
        goto cleanup;

    /* 重新打开未修改文件，:q 直接退出。 */
    command = XString_create_fmt_utf8("vim %s", XString_toUtf8(file));
    if (!command) goto cleanup;
    if (!XConsoleShellTest_runLine(shell, transport, XString_toUtf8(command),
                                   XConsoleResult_MoreOutput, NULL))
        goto cleanup;
    XString_delete_base(command);
    command = NULL;
    if (!XConsoleShellTest_feedEditor(shell, transport, ":q\n", 3,
                                      XConsoleResult_Ok, NULL))
        goto cleanup;

    /* 再次打开并修改后，:q! 放弃修改，磁盘内容保持不变。 */
    command = XString_create_fmt_utf8("vi %s", XString_toUtf8(file));
    if (!command) goto cleanup;
    if (!XConsoleShellTest_runLine(shell, transport, XString_toUtf8(command),
                                   XConsoleResult_MoreOutput, NULL))
        goto cleanup;
    XString_delete_base(command);
    command = NULL;
    if (!XConsoleShellTest_feedEditor(shell, transport, "i", 1,
                                      XConsoleResult_MoreOutput, NULL))
        goto cleanup;
    if (!XConsoleShellTest_feedEditor(shell, transport, "discarded", 9,
                                      XConsoleResult_MoreOutput, NULL))
        goto cleanup;
    if (!XConsoleShellTest_feedEditor(shell, transport, "\x1b", 1,
                                      XConsoleResult_MoreOutput, NULL))
        goto cleanup;
    /* 有未保存修改时 :q 应拒绝退出并留在 TUI。 */
    if (!XConsoleShellTest_feedEditor(shell, transport, ":q\n", 3,
                                      XConsoleResult_MoreOutput, NULL))
        goto cleanup;
    if (!XConsoleShellTest_feedEditor(shell, transport, ":q!\n", 4,
                                      XConsoleResult_Ok, NULL))
        goto cleanup;
    fd = XFileSystem_open(file, XFileSystem_ReadOnly, &error);
    if (fd == XFD_INVALID) goto cleanup;
    size = XFileSystem_read(fd, content, (int64_t)sizeof(content) - 1);
    XFileSystem_close(fd);
    if (size <= 0) goto cleanup;
    content[size] = '\0';
    if (strstr(content, "discarded") != NULL)
        goto cleanup;
#else
    /* 行式状态机：新建文件打开后进入命令模式。 */
    if (!XConsoleShellTest_runLine(shell, transport, XString_toUtf8(command),
                                   XConsoleResult_MoreOutput, "vi 命令模式"))
        goto cleanup;
    XString_delete_base(command);
    command = NULL;
    /* i 1：在第 1 行前插入两行内容。 */
    if (!XConsoleShellTest_runLine(shell, transport, "i 1",
                                   XConsoleResult_MoreOutput, "插入模式"))
        goto cleanup;
    if (!XConsoleShellTest_runLine(shell, transport, "first line",
                                   XConsoleResult_MoreOutput, "1:\tfirst line"))
        goto cleanup;
    if (!XConsoleShellTest_runLine(shell, transport, "second line",
                                   XConsoleResult_MoreOutput, "2:\tsecond line"))
        goto cleanup;
    if (!XConsoleShellTest_runLine(shell, transport, ".",
                                   XConsoleResult_MoreOutput, "命令模式"))
        goto cleanup;
    /* :wq 保存并退出。 */
    if (!XConsoleShellTest_runLine(shell, transport, ":wq",
                                   XConsoleResult_Ok, "已保存"))
        goto cleanup;
    fd = XFileSystem_open(file, XFileSystem_ReadOnly, &error);
    if (fd == XFD_INVALID) goto cleanup;
    size = XFileSystem_read(fd, content, (int64_t)sizeof(content) - 1);
    XFileSystem_close(fd);
    if (size <= 0) goto cleanup;
    content[size] = '\0';
    if (strstr(content, "first line") == NULL ||
        strstr(content, "second line") == NULL)
        goto cleanup;

    /* 重新打开未修改文件，:q 直接退出。 */
    command = XString_create_fmt_utf8("vim %s", XString_toUtf8(file));
    if (!command) goto cleanup;
    if (!XConsoleShellTest_runLine(shell, transport, XString_toUtf8(command),
                                   XConsoleResult_MoreOutput, "命令模式"))
        goto cleanup;
    XString_delete_base(command);
    command = NULL;
    if (!XConsoleShellTest_runLine(shell, transport, ":q",
                                   XConsoleResult_Ok, NULL))
        goto cleanup;

    /* 再次打开并修改后，:q! 放弃修改，磁盘内容保持不变。 */
    command = XString_create_fmt_utf8("vi %s", XString_toUtf8(file));
    if (!command) goto cleanup;
    if (!XConsoleShellTest_runLine(shell, transport, XString_toUtf8(command),
                                   XConsoleResult_MoreOutput, "命令模式"))
        goto cleanup;
    XString_delete_base(command);
    command = NULL;
    if (!XConsoleShellTest_runLine(shell, transport, "i 1",
                                   XConsoleResult_MoreOutput, "插入模式"))
        goto cleanup;
    if (!XConsoleShellTest_runLine(shell, transport, "discarded",
                                   XConsoleResult_MoreOutput, "discarded"))
        goto cleanup;
    if (!XConsoleShellTest_runLine(shell, transport, ".",
                                   XConsoleResult_MoreOutput, "命令模式"))
        goto cleanup;
    if (!XConsoleShellTest_runLine(shell, transport, ":q!",
                                   XConsoleResult_Ok, NULL))
        goto cleanup;
    fd = XFileSystem_open(file, XFileSystem_ReadOnly, &error);
    if (fd == XFD_INVALID) goto cleanup;
    size = XFileSystem_read(fd, content, (int64_t)sizeof(content) - 1);
    XFileSystem_close(fd);
    if (size <= 0) goto cleanup;
    content[size] = '\0';
    if (strstr(content, "discarded") != NULL)
        goto cleanup;
#endif

    ok = true;

cleanup:
    if (rootCreated && root) XFileSystem_rmdir(root, true);
    if (file) XString_delete_base(file);
    if (command) XString_delete_base(command);
    if (root) XString_delete_base(root);
    if (temp) XString_delete_base(temp);
    return ok;
}
#endif

#if XCONSOLE_SHELL_LOGIN_ON
/* Windows/CRLF terminal sends \r\n on Enter; the LF must be skipped as part of the
   newline so an empty line is not submitted to the pending login/password input. */
static bool XConsoleShellTest_runCrlfLoginFlow(void)
{
    XConsoleShellTestTransport crlfTransport;
    XConsoleShellIo crlfIo;
    XConsoleShell* crlfShell;
    XString* crlfPath;
    static const uint8_t crlfInput[] =
        "useradd root\r\n"
        "passwd root\r\n"
        "123456\r\n"
        "123456\r\n"
        "login root\r\n"
        "123456\r\n";
    bool ok = false;

    memset(&crlfTransport, 0, sizeof(crlfTransport));
    memset(&crlfIo, 0, sizeof(crlfIo));
    crlfIo.read = XConsoleShellTest_read;
    crlfIo.write = XConsoleShellTest_write;
    crlfIo.flush = XConsoleShellTest_flush;
#if XCONSOLE_SHELL_LOG_ON
    crlfIo.log = XConsoleShellTest_log;
#endif
#if XCONSOLE_SHELL_AUDIT_ON
    crlfIo.audit = XConsoleShellTest_audit;
#endif
    crlfIo.userData = &crlfTransport;

    crlfPath = XString_create_utf8("xconsole_shell_crlf_users_test.json");
    XCS_TEST_CHECK(crlfPath != NULL, "crlf login path");
    XFileSystem_removePermanent(crlfPath);
    crlfShell = XConsoleShell_create(&crlfIo);
    XCS_TEST_CHECK(crlfShell != NULL, "crlf shell create");
    XCS_TEST_CHECK(XConsoleShellLogin_setDatabasePath(
                       crlfShell, "xconsole_shell_crlf_users_test.json"),
                   "crlf set login database path");
    crlfTransport.length = 0;
    crlfTransport.output[0] = '\0';
    XCS_TEST_CHECK(XConsoleShell_feedData(crlfShell, crlfInput,
                                          sizeof(crlfInput) - 1) ==
                       XConsoleResult_Ok,
                   "crlf feed byte stream");
#if XCONSOLE_SHELL_ASYNC_OUTPUT_ON
    XCS_TEST_CHECK(XConsoleShell_flushOutput(crlfShell), "crlf flush output");
#endif
    XCS_TEST_CHECK(strstr(crlfTransport.output,
                          "\xe5\xaf\x86\xe7\xa0\x81\xe5\xb7\xb2\xe6\x9b\xb4\xe6\x96\xb0") != NULL,
                   "crlf passwd updated");
    XCS_TEST_CHECK(strstr(crlfTransport.output, "login: root ") != NULL &&
                       strstr(crlfTransport.output,
                              "\xe6\x88\x90\xe5\x8a\x9f") != NULL &&
                       !strstr(crlfTransport.output,
                               "\xe8\xb4\xa6\xe6\x88\xb7\xe5\xb0\x9a\xe6\x9c\xaa\xe8\xae\xbe\xe7\xbd\xae\xe5\xaf\x86\xe7\xa0\x81"),
                   "crlf login root success");
    XCS_TEST_CHECK(!strstr(crlfTransport.output,
                           "\xe6\x96\xb0\xe5\xaf\x86\xe7\xa0\x81: \r\nXinYueC> ") &&
                       !strstr(crlfTransport.output,
                               "\xe6\x96\xb0\xe5\xaf\x86\xe7\xa0\x81: \nXinYueC> "),
                   "crlf password prompt not interrupted by empty line");
    XCS_TEST_CHECK(strstr(crlfTransport.output,
                          "\xe6\x96\xb0\xe5\xaf\x86\xe7\xa0\x81: \n\xe9\x87\x8d\xe6\x96\xb0\xe8\xbe\x93\xe5\x85\xa5\xe6\x96\xb0\xe5\xaf\x86\xe7\xa0\x81: ") != NULL,
                   "password prompts separated by newline");
    ok = true;

    XFileSystem_removePermanent(crlfPath);
    if (crlfShell) XConsoleShell_delete_base(crlfShell);
    XString_delete_base(crlfPath);
    return ok;
}
#endif

bool XConsoleShellTest_runAll(void)
{
    XConsoleShellTestTransport transport;
    XConsoleShellIo io;
    XConsoleShell* shell;
    XConsoleShellSession* session;
    XString* filePath;
    XFd fd;
    int error = 0;
    const char fileText[] = "shell-cat";
#if XCONSOLE_SHELL_FS_CAT_ON
    bool result;
#endif
    memset(&transport, 0, sizeof(transport));
    memset(&io, 0, sizeof(io));
    io.read = XConsoleShellTest_read;
    io.write = XConsoleShellTest_write;
    io.flush = XConsoleShellTest_flush;
#if XCONSOLE_SHELL_LOG_ON
    io.log = XConsoleShellTest_log;
#endif
#if XCONSOLE_SHELL_AUDIT_ON
    io.audit = XConsoleShellTest_audit;
#endif
    io.userData = &transport;
    shell = XConsoleShell_create(&io);
    XCS_TEST_CHECK(shell != NULL, "create");
#if XCONSOLE_SHELL_LOGIN_ON
    {
        XString* loginPath = XString_create_utf8("xconsole_shell_users_test.json");
        XCS_TEST_CHECK(loginPath != NULL, "login test path");
        XFileSystem_removePermanent(loginPath);
        XCS_TEST_CHECK(XConsoleShellLogin_setDatabasePath(
                           shell, "xconsole_shell_users_test.json"),
                       "set login database path");
#if XCONSOLE_SHELL_LOGIN_REQUIRED_ON
        XCS_TEST_CHECK(XConsoleShellTest_runLine(shell, &transport,
                                                 "echo before-login",
                                                 XConsoleResult_PermissionDenied,
                                                 "权限不足"),
                       "login is required for ordinary commands");
#endif
        XCS_TEST_CHECK(XConsoleShellTest_runLine(shell, &transport,
                                                 "useradd testadmin --admin",
                                                 XConsoleResult_Ok,
                                                 "请使用 passwd"),
                       "bootstrap useradd");
        XCS_TEST_CHECK(XConsoleShellTest_runLine(shell, &transport,
                                                 "password testadmin",
                                                 XConsoleResult_MoreOutput,
                                                 "新密码: "),
                       "bootstrap password prompt");
        XCS_TEST_CHECK(XConsoleShellTest_runLine(shell, &transport,
                                                 "secret",
                                                 XConsoleResult_MoreOutput,
                                                 "重新输入新密码: "),
                       "bootstrap password confirmation prompt");
        XCS_TEST_CHECK(XConsoleShellTest_runLine(shell, &transport,
                                                 "secret",
                                                 XConsoleResult_Ok,
                                                 "密码已更新"),
                       "bootstrap password");
        {
            XFd loginFd;
            char loginJson[XCONSOLE_SHELL_LOGIN_CONFIG_MAX_BYTES + 1u];
            int loginError = 0;
            int64_t loginSize;
            loginFd = XFileSystem_open(loginPath, XFileSystem_ReadOnly, &loginError);
            XCS_TEST_CHECK(loginFd != XFD_INVALID, "login database persisted");
            loginSize = XFileSystem_read(loginFd, loginJson,
                                         (int64_t)sizeof(loginJson) - 1);
            XFileSystem_close(loginFd);
            XCS_TEST_CHECK(loginSize > 0 && loginSize < (int64_t)sizeof(loginJson),
                           "login database readable");
            loginJson[loginSize] = '\0';
            XCS_TEST_CHECK(strstr(loginJson, "\"hash\"") != NULL &&
                           strstr(loginJson, "\"salt\"") != NULL &&
                           strstr(loginJson, "secret") == NULL,
                           "password is stored as hash only");
        }
        XCS_TEST_CHECK(XConsoleShellTest_runLine(shell, &transport,
                                                 "login testadmin",
                                                 XConsoleResult_MoreOutput,
                                                 "密码: "),
                       "login prompt");
        XCS_TEST_CHECK(XConsoleShellTest_runLine(shell, &transport,
                                                 "wrong-password",
                                                 XConsoleResult_PermissionDenied,
                                                 "用户名或密码错误"),
                       "login incorrect password");
        XCS_TEST_CHECK(XConsoleShellTest_runLine(shell, &transport,
                                                 "login testadmin",
                                                 XConsoleResult_MoreOutput,
                                                 "密码: "),
                       "login retry prompt");
        XCS_TEST_CHECK(XConsoleShellTest_runLine(shell, &transport,
                                                 "secret",
                                                 XConsoleResult_Ok,
                                                 "testadmin"),
                       "login command");
        XCS_TEST_CHECK(XConsoleShellLogin_userName(shell) != NULL &&
                       strcmp(XConsoleShellLogin_userName(shell), "testadmin") == 0,
                       "logged-in user name");
#if XCONSOLE_SHELL_HISTORY_ON
        {
            size_t historyIndex;
            for (historyIndex = 0;
                 historyIndex < XConsoleShell_historyCount(shell);
                 ++historyIndex) {
                const char* entry = XConsoleShell_historyAt(shell, historyIndex);
                XCS_TEST_CHECK(!entry || (!strstr(entry, "secret") &&
                                          !strstr(entry, "wrong-password")),
                               "login password is not retained in history");
            }
        }
#endif
        XCS_TEST_CHECK(XConsoleShellTest_runLine(shell, &transport,
                                                 "useradd testuser",
                                                 XConsoleResult_Ok,
                                                 "请使用 passwd"),
                       "useradd command");
        XCS_TEST_CHECK(XConsoleShellTest_runLine(shell, &transport,
                                                 "login testuser",
                                                 XConsoleResult_PermissionDenied,
                                                 "尚未设置密码"),
                       "passwordless user login denied");
        XCS_TEST_CHECK(XConsoleShellTest_runLine(shell, &transport, "userlist",
                                                 XConsoleResult_Ok, "testuser"),
                       "userlist command");
        XCS_TEST_CHECK(XConsoleShellTest_runLine(shell, &transport, "users",
                                                 XConsoleResult_Ok, "testadmin"),
                       "users session command");
        XCS_TEST_CHECK(XConsoleShellTest_runLine(shell, &transport,
                                                 "usermod testuser --permissions 1",
                                                 XConsoleResult_Ok,
                                                 "用户已更新"),
                       "usermod command");
        XCS_TEST_CHECK(XConsoleShellTest_runLine(shell, &transport,
                                                 "password testuser",
                                                 XConsoleResult_MoreOutput,
                                                 "新密码: "),
                       "passwd prompt");
        XCS_TEST_CHECK(XConsoleShellTest_runLine(shell, &transport,
                                                 "changed",
                                                 XConsoleResult_MoreOutput,
                                                 "重新输入新密码: "),
                       "passwd confirmation prompt");
        XCS_TEST_CHECK(XConsoleShellTest_runLine(shell, &transport,
                                                 "changed",
                                                 XConsoleResult_Ok,
                                                 "密码已更新"),
                       "passwd command");
        XCS_TEST_CHECK(XConsoleShellTest_runLine(shell, &transport,
                                                 "logout",
                                                 XConsoleResult_Ok,
                                                 "已注销"),
                       "logout before non-admin query");
        XCS_TEST_CHECK(XConsoleShellTest_runLine(shell, &transport,
                                                 "login testuser",
                                                 XConsoleResult_MoreOutput,
                                                 "密码: "),
                       "non-admin login prompt");
        XCS_TEST_CHECK(XConsoleShellTest_runLine(shell, &transport,
                                                 "changed",
                                                 XConsoleResult_Ok,
                                                 "testuser"),
                       "non-admin login");
        XCS_TEST_CHECK(XConsoleShellTest_runLine(shell, &transport,
                                                 "id testadmin",
                                                 XConsoleResult_Ok,
                                                 "uid=0(testadmin)"),
                       "non-admin can query another user id");
        XCS_TEST_CHECK(XConsoleShellTest_runLine(shell, &transport,
                                                 "groups testadmin",
                                                 XConsoleResult_Ok,
                                                 "testadmin :"),
                       "non-admin can query another user groups");
        XCS_TEST_CHECK(XConsoleShellTest_runLine(shell, &transport,
                                                 "passwd",
                                                 XConsoleResult_MoreOutput,
                                                 "当前密码: "),
                       "non-admin passwd current prompt");
        XCS_TEST_CHECK(XConsoleShellTest_runLine(shell, &transport,
                                                 "wrong-old",
                                                 XConsoleResult_PermissionDenied,
                                                 "当前密码错误"),
                       "non-admin passwd wrong old password");
        XCS_TEST_CHECK(XConsoleShellTest_runLine(shell, &transport,
                                                 "passwd",
                                                 XConsoleResult_MoreOutput,
                                                 "当前密码: "),
                       "non-admin passwd current prompt retry");
        XCS_TEST_CHECK(XConsoleShellTest_runLine(shell, &transport,
                                                 "changed",
                                                 XConsoleResult_MoreOutput,
                                                 "新密码: "),
                       "non-admin passwd old ok");
        XCS_TEST_CHECK(XConsoleShellTest_runLine(shell, &transport,
                                                 "newpass",
                                                 XConsoleResult_MoreOutput,
                                                 "重新输入新密码: "),
                       "non-admin passwd new prompt");
        XCS_TEST_CHECK(XConsoleShellTest_runLine(shell, &transport,
                                                 "newpass",
                                                 XConsoleResult_Ok,
                                                 "密码已更新"),
                       "non-admin passwd change");
        XCS_TEST_CHECK(XConsoleShellTest_runLine(shell, &transport,
                                                 "logout",
                                                 XConsoleResult_Ok,
                                                 "已注销"),
                       "logout after non-admin");
        XCS_TEST_CHECK(XConsoleShellTest_runLine(shell, &transport,
                                                 "login testadmin",
                                                 XConsoleResult_MoreOutput,
                                                 "密码: "),
                       "admin relogin prompt");
        XCS_TEST_CHECK(XConsoleShellTest_runLine(shell, &transport,
                                                 "secret",
                                                 XConsoleResult_Ok,
                                                 "testadmin"),
                       "admin relogin");
        XCS_TEST_CHECK(XConsoleShellTest_runLine(shell, &transport,
                                                 "useradd -u 2001 -g 2002 -G 2002,2003 linuxuser",
                                                 XConsoleResult_Ok,
                                                 "用户已创建"),
                       "Linux useradd options");
        XCS_TEST_CHECK(XConsoleShellTest_runLine(shell, &transport,
                                                 "useradd -l renamed bogus",
                                                 XConsoleResult_InvalidArgument,
                                                 "用法"),
                       "useradd rejects usermod rename");
        XCS_TEST_CHECK(XConsoleShellTest_runLine(shell, &transport,
                                                 "useradd -aG 3000 bogus",
                                                 XConsoleResult_InvalidArgument,
                                                 "用法"),
                       "useradd rejects append groups");
        XCS_TEST_CHECK(XConsoleShellTest_runLine(shell, &transport,
                                                 "useradd -L bogus",
                                                 XConsoleResult_InvalidArgument,
                                                 "用法"),
                       "useradd rejects lock option");
        XCS_TEST_CHECK(XConsoleShellTest_runLine(shell, &transport,
                                                 "useradd -U bogus",
                                                 XConsoleResult_InvalidArgument,
                                                 "用法"),
                       "useradd rejects unlock option");
        XCS_TEST_CHECK(XConsoleShellTest_runLine(shell, &transport,
                                                 "id linuxuser",
                                                 XConsoleResult_Ok,
                                                 "uid=2001(linuxuser) gid=2002"),
                       "id named user");
        XCS_TEST_CHECK(XConsoleShellTest_runLine(shell, &transport,
                                                 "groups linuxuser",
                                                 XConsoleResult_Ok,
                                                 "linuxuser : 2002 2003"),
                       "groups named user");
        XCS_TEST_CHECK(XConsoleShellTest_runLine(shell, &transport,
                                                 "usermod -aG 2004 -L -U -l renamed linuxuser",
                                                 XConsoleResult_Ok,
                                                 "用户已更新"),
                       "Linux usermod options");
        XCS_TEST_CHECK(XConsoleShellTest_runLine(shell, &transport,
                                                 "groups renamed",
                                                 XConsoleResult_Ok,
                                                 "renamed : 2002 2003 2004"),
                       "usermod appended groups");
        XCS_TEST_CHECK(XConsoleShellTest_runLine(shell, &transport,
                                                 "userdel -r renamed",
                                                 XConsoleResult_NotSupported,
                                                 "-r 不支持"),
                       "userdel remove home unsupported");
        XCS_TEST_CHECK(XConsoleShellTest_runLine(shell, &transport,
                                                 "userdel renamed",
                                                 XConsoleResult_Ok,
                                                 "用户已删除"),
                       "Linux userdel");
        XCS_TEST_CHECK(XConsoleShellTest_runLine(shell, &transport,
                                                 "userdel testuser",
                                                 XConsoleResult_Ok,
                                                 "用户已删除"),
                       "userdel command");
        XCS_TEST_CHECK(XConsoleShellTest_runLine(shell, &transport, "logout",
                                                 XConsoleResult_Ok, "已注销"),
                       "logout command");
        XCS_TEST_CHECK(XConsoleShellTest_runLine(shell, &transport, "login",
                                                 XConsoleResult_MoreOutput,
                                                 "用户名: "),
                       "login username prompt");
        XCS_TEST_CHECK(XConsoleShellTest_runLine(shell, &transport, "testadmin",
                                                 XConsoleResult_MoreOutput,
                                                 "密码: "),
                       "login username input");
        XCS_TEST_CHECK(XConsoleShellTest_runLine(shell, &transport, "secret",
                                                 XConsoleResult_Ok,
                                                 "testadmin"),
                       "login username flow");
        XCS_TEST_CHECK(XConsoleShellTest_runLine(shell, &transport, "logout",
                                                 XConsoleResult_Ok, "已注销"),
                       "second logout command");
#if XCONSOLE_SHELL_LOGIN_REQUIRED_ON
        XCS_TEST_CHECK(XConsoleShellTest_runLine(shell, &transport,
                                                 "echo after-logout",
                                                 XConsoleResult_PermissionDenied,
                                                 "权限不足"),
                       "logout requires login again");
#endif
        XFileSystem_removePermanent(loginPath);
        XString_delete_base(loginPath);
        XConsoleShell_setAuthenticated(shell, true);
        XConsoleShell_session(shell)->permissionMask = UINT32_MAX;
    }
#endif
#if XCONSOLE_SHELL_LOGIN_ON
        XCS_TEST_CHECK(XConsoleShellTest_runCrlfLoginFlow(), "crlf login flow");
#endif

#if XCONSOLE_SHELL_GPIO_ON
    transport.gpioAllow = true;
    XCS_TEST_CHECK(XConsoleShell_setGpioAuthorizeCallback(
                       shell, XConsoleShellTest_gpioAuthorize, &transport),
                   "set gpio authorize callback");
#endif
#if XCONSOLE_SHELL_CAN_ON
    transport.canAllow = true;
    XCS_TEST_CHECK(XConsoleShell_setCanAuthorizeCallback(
                       shell, XConsoleShellTest_canAuthorize, &transport),
                   "set can authorize callback");
#endif
#if XCONSOLE_SHELL_ADC_ON
    transport.adcAllow = true;
    XCS_TEST_CHECK(XConsoleShell_setAdcAuthorizeCallback(
                       shell, XConsoleShellTest_adcAuthorize, &transport),
                   "set adc authorize callback");
#endif
#if XCONSOLE_SHELL_PWM_ON
    transport.pwmAllow = true;
    XCS_TEST_CHECK(XConsoleShell_setPwmAuthorizeCallback(
                       shell, XConsoleShellTest_pwmAuthorize, &transport),
                   "set pwm authorize callback");
#endif
#if XCONSOLE_SHELL_I2C_ON
    transport.i2cAllow = true;
    XCS_TEST_CHECK(XConsoleShell_setI2cAuthorizeCallback(
                       shell, XConsoleShellTest_i2cAuthorize, &transport),
                   "set i2c authorize callback");
#endif
#if XCONSOLE_SHELL_SPI_ON
    transport.spiAllow = true;
    XCS_TEST_CHECK(XConsoleShell_setSpiAuthorizeCallback(
                       shell, XConsoleShellTest_spiAuthorize, &transport),
                   "set spi authorize callback");
#endif
#if XCONSOLE_SHELL_TASKS_ON
    XCS_TEST_CHECK(XConsoleShell_setTaskProvider(shell,
                                                  XConsoleShellTest_taskProvider,
                                                  NULL),
                   "set tasks provider");
#endif
    XCS_TEST_CHECK(XConsoleShellTest_runStress(shell, &transport, &io),
                   "100000 command and 10000 lifecycle stress");
#if XCONSOLE_SHELL_ASYNC_OUTPUT_ON
    transport.length = 0;
    transport.output[0] = '\0';
    XCS_TEST_CHECK(XConsoleShell_writeUtf8(shell, "async") && transport.length == 0,
                   "async output queue");
    XCS_TEST_CHECK(XConsoleShell_flushOutput(shell) &&
                       strcmp(transport.output, "async") == 0,
                   "async output flush");
#endif
#if XCONSOLE_SHELL_LOG_ON
    {
        size_t logCount = transport.logCount;
        XCS_TEST_CHECK(XConsoleShellTest_runLine(shell, &transport, "echo logged",
                                                 XConsoleResult_Ok, "logged") &&
                           transport.logCount > logCount &&
                           strcmp(transport.lastLog, "\n") == 0,
                       "output log callback");
    }
#endif
#if XCONSOLE_SHELL_MULTI_SESSION_ON
    {
        XConsoleShellTestTransport secondaryTransport;
        XConsoleShellIo secondaryIo = io;
        XConsoleShellSession* secondary;
        memset(&secondaryTransport, 0, sizeof(secondaryTransport));
        secondaryIo.userData = &secondaryTransport;
        secondary = XConsoleShell_openSession(shell, &secondaryIo);
        XCS_TEST_CHECK(secondary != NULL && XConsoleShell_sessionCount(shell) == 2,
                       "open secondary session");
#if XCONSOLE_SHELL_AUTH_ON
        /* 多会话测试验证的是会话切换与 I/O 路由，先让附加会话与主会话一样通过认证。 */
        secondary->authenticated = true;
        secondary->permissionMask = UINT32_MAX;
#endif
        XCS_TEST_CHECK(XConsoleShell_processLineForSession(
                           shell, secondary, "echo secondary", 14) == XConsoleResult_Ok &&
                           strstr(secondaryTransport.output, "secondary"),
                       "secondary session output");
        secondaryTransport.length = 0;
        secondaryTransport.output[0] = '\0';
        XCS_TEST_CHECK(XConsoleShell_feedDataForSession(shell, secondary, "echo buffered", 13) ==
                           XConsoleResult_Ok && secondaryTransport.length == 0,
                       "secondary buffered input");
        XCS_TEST_CHECK(XConsoleShell_processLine(shell, "echo primary", 12) ==
                           XConsoleResult_Ok && strstr(transport.output, "primary"),
                       "primary session remains active");
        XCS_TEST_CHECK(XConsoleShell_feedByteForSession(shell, secondary, '\n') ==
                           XConsoleResult_Ok && strstr(secondaryTransport.output, "buffered"),
                       "secondary buffered execute");
#if XCONSOLE_SHELL_ASYNC_OUTPUT_ON && XCONSOLE_SHELL_DYNAMIC_REGISTER_ON
        {
            XConsoleCommand outputThenFail = {
                "output-fail", NULL, "输出后失败", "output-fail", 0, 0, 0,
                XConsoleShellTest_outputThenFail, NULL, 0, NULL
            };
            XCS_TEST_CHECK(XConsoleShell_registerCommand(shell, &outputThenFail),
                           "async output failure register");
            secondaryTransport.length = 0;
            secondaryTransport.output[0] = '\0';
            XCS_TEST_CHECK(XConsoleShell_processLineForSession(
                               shell, secondary, "output-fail", 11) ==
                               XConsoleResult_Failed &&
                           strstr(secondaryTransport.output, "secondary-failure"),
                           "failed secondary command keeps output session");
            XCS_TEST_CHECK(XConsoleShell_unregisterCommand(shell, "output-fail"),
                           "async output failure unregister");
        }
#endif
        strcpy(secondary->currentPath, "/tmp");
        XCS_TEST_CHECK(strcmp(XConsoleShell_session_const(shell)->currentPath,
                              secondary->currentPath) != 0,
                       "session path isolation");
        XCS_TEST_CHECK(XConsoleShell_closeSession(shell, secondary) &&
                           XConsoleShell_sessionCount(shell) == 1,
                       "close secondary session");
        secondary = XConsoleShell_openSession(shell, &secondaryIo);
        XCS_TEST_CHECK(secondary != NULL && secondary->id != 2,
                       "session id is not reused after close");
        XCS_TEST_CHECK(XConsoleShell_closeSession(shell, secondary),
                       "close reopened session");
    }
#endif
#if XCONSOLE_SHELL_TELNET_PROTOCOL_ON
    {
        XConsoleShellTestTransport telnetTransport;
        XConsoleShellIo telnetTransportIo = io;
        XConsoleShellIo telnetIo;
        XConsoleShellTelnetAdapter adapter;
        XConsoleShell* telnetShell;
        bool sawEscapedIac = false;
        size_t telnetIndex;
        const uint8_t stream[] = {
            255, 253, 1, 'e', 'c', 'h', 'o', ' ', 't', 'e', 'l', 'n', 'e', 't',
            255, 255, '\r', '\n'
        };
        memset(&telnetTransport, 0, sizeof(telnetTransport));
        telnetTransportIo.userData = &telnetTransport;
        XConsoleShellTelnetAdapter_init(&adapter, &telnetTransportIo);
        XCS_TEST_CHECK(XConsoleShellTelnetAdapter_makeIo(&adapter, &telnetIo),
                       "telnet make io");
        telnetShell = XConsoleShell_create(&telnetIo);
        XCS_TEST_CHECK(telnetShell != NULL, "telnet shell create");
#if XCONSOLE_SHELL_AUTH_ON
        /* Telnet 测试验证协议过滤与 I/O 路由，先让会话通过认证。 */
        XConsoleShell_session(telnetShell)->authenticated = true;
        XConsoleShell_session(telnetShell)->permissionMask = UINT32_MAX;
#endif
        XConsoleResult telnetFeedResult = XConsoleShellTelnetAdapter_feedData(
            &adapter, telnetShell, NULL, stream, sizeof(stream));
#if XCONSOLE_SHELL_ASYNC_OUTPUT_ON
        bool telnetFlushed = XConsoleShell_flushOutput(telnetShell);
#else
        bool telnetFlushed = true;
#endif
        XCS_TEST_CHECK(telnetFlushed && telnetFeedResult == XConsoleResult_Ok &&
                           telnetTransport.length >= 3 &&
                           (uint8_t)telnetTransport.output[0] == 255 &&
                           (uint8_t)telnetTransport.output[1] == 251 &&
                           (uint8_t)telnetTransport.output[2] == 1 &&
                           strstr(telnetTransport.output + 3, "telnet"),
                       "telnet negotiate and command");
        for (telnetIndex = 3; telnetIndex + 1u < telnetTransport.length; ++telnetIndex) {
            if ((uint8_t)telnetTransport.output[telnetIndex] == 255 &&
                (uint8_t)telnetTransport.output[telnetIndex + 1u] == 255) {
                sawEscapedIac = true;
                break;
            }
        }
        XCS_TEST_CHECK(sawEscapedIac, "telnet literal IAC escaping and CRLF");
        XConsoleShell_delete_base(telnetShell);
    }
#endif
#if XCONSOLE_SHELL_XSERIALPORT_BACKEND_ON
    {
        XSerialPort port;
        XConsoleShellXSerialPortAdapter adapter;
        XConsoleShellIo adapterIo;
        memset(&port, 0, sizeof(port));
        XConsoleShellXSerialPortAdapter_init(&adapter, &port);
        XCS_TEST_CHECK(XConsoleShellXSerialPortAdapter_makeIo(&adapter, &adapterIo) &&
                           adapterIo.userData == &adapter.ioDevice,
                       "serial adapter make io");
    }
#endif
#if XCONSOLE_SHELL_XTCPSOCKET_BACKEND_ON
    {
        XTcpSocket socket;
        XConsoleShellXTcpSocketAdapter adapter;
        XConsoleShellIo adapterIo;
        memset(&socket, 0, sizeof(socket));
        XConsoleShellXTcpSocketAdapter_init(&adapter, &socket);
        XCS_TEST_CHECK(XConsoleShellXTcpSocketAdapter_makeIo(&adapter, &adapterIo) &&
                           adapterIo.userData == &adapter.ioDevice,
                       "tcp socket adapter make io");
    }
#endif
#if XCONSOLE_SHELL_XTCPSERVER_BACKEND_ON
    {
        XTcpServer server;
        XConsoleShellXTcpServerAdapter adapter;
        XTcpServer_init(&server);
        XConsoleShellXTcpServerAdapter_init(&adapter, shell, &server);
        XCS_TEST_CHECK(XConsoleShellXTcpServerAdapter_acceptPending(&adapter) == 0 &&
                           XConsoleShellXTcpServerAdapter_pump(&adapter, 64) == 0,
                       "tcp server adapter idle");
        XTcpServer_close(&server);
        XClass_deinit_base((XClass*)&server);
    }
#endif
#if XCONSOLE_SHELL_AUTH_ON && XCONSOLE_SHELL_FS_FORMAT_ON
    /* 前面的登录块已认证主会话，这里临时注销以验证危险命令的权限门槛。 */
    XConsoleShell_setAuthenticated(shell, false);
    XCS_TEST_CHECK(XConsoleShell_processLine(shell, "fs format .", 11) ==
                       XConsoleResult_PermissionDenied,
                   "unauthenticated dangerous command");
#endif
#if XCONSOLE_SHELL_AUTH_ON
    XConsoleShell_setAuthenticated(shell, true);
#endif
#if XCONSOLE_SHELL_AUDIT_ON
    {
        size_t auditCount = transport.auditCount;
        XCS_TEST_CHECK(XConsoleShell_processLine(shell, "echo audit", 10) ==
                           XConsoleResult_Ok &&
                       transport.auditCount == auditCount + 1u &&
                       transport.lastAuditResult == XConsoleResult_Ok &&
                       transport.lastAuditCommand != NULL,
                       "audit callback");
    }
#endif
    XCS_TEST_CHECK(XConsoleShell_processLine(shell, "echo \"hello world\"", 18) ==
                       XConsoleResult_Ok && strstr(transport.output, "hello world\n"),
                   "quoted echo and short write");
    transport.length = 0;
    transport.output[0] = '\0';
    XCS_TEST_CHECK(XConsoleShell_processLine(shell, "unknown", 7) ==
                       XConsoleResult_UnknownCommand &&
                       strstr(transport.output, "\xe9\x94\x99\xe8\xaf\xaf: \xe6\x9c\xaa\xe7\x9f\xa5\xe5\x91\xbd\xe4\xbb\xa4") != NULL,
                       "unknown command error output");
    XCS_TEST_CHECK(XConsoleShell_processLine(shell, "echo \"", 6) ==
                       XConsoleResult_InvalidSyntax, "invalid quote");
#if XCONSOLE_SHELL_CALLBACK_COMMAND_ON
    {
        const XConsoleCommand duplicateBatch[2] = {g_custom, g_custom};
        XCS_TEST_CHECK(!XConsoleShell_registerStaticCommands(shell, duplicateBatch, 2),
                       "duplicate batch registration rejected atomically");
    }
#endif
    XCS_TEST_CHECK(XConsoleShell_registerStaticCommands(shell, &g_custom, 1),
                   "register command");
    XCS_TEST_CHECK(XConsoleShell_processLine(shell, "c value", 7) == XConsoleResult_Ok &&
                       strstr(transport.output, "value"), "alias command");
    XConsoleShell_unregisterStaticCommands(shell, &g_custom, 1);
#if XCONSOLE_SHELL_DYNAMIC_REGISTER_ON
    {
        char name[] = "dynamic";
        char description[] = "动态命令";
        XConsoleCommand command = {
            name, NULL, description, "dynamic <value>", 1, 1, 0,
            XConsoleShellTest_custom, NULL, 0, NULL
        };
        XCS_TEST_CHECK(XConsoleShell_registerCommand(shell, &command), "dynamic register");
        strcpy(name, "changed");
        strcpy(description, "changed");
        XCS_TEST_CHECK(XConsoleShellTest_runLine(shell, &transport, "dynamic copied",
                                                 XConsoleResult_Ok, "copied"),
                       "dynamic deep copy");
        XCS_TEST_CHECK(XConsoleShell_unregisterCommand(shell, "dynamic"),
                       "dynamic unregister");
        XCS_TEST_CHECK(XConsoleShell_processLine(shell, "dynamic copied", 14) ==
                           XConsoleResult_UnknownCommand,
                       "dynamic command removed");
        {
            XConsoleCommand selfRemoving = {
                "selfremove", NULL, "自注销命令", "selfremove", 0, 0, 0,
                XConsoleShellTest_selfUnregister, NULL, 0, NULL
            };
            XCS_TEST_CHECK(XConsoleShell_registerCommand(shell, &selfRemoving),
                           "self unregister register");
            XCS_TEST_CHECK(XConsoleShell_processLine(shell, "selfremove", 10) ==
                               XConsoleResult_Ok,
                           "self unregister execution");
            XCS_TEST_CHECK(XConsoleShell_processLine(shell, "selfremove", 10) ==
                               XConsoleResult_UnknownCommand,
                           "self unregister removed");
        }
    }
#endif
    {
        XConsoleResult feedResult = XConsoleShell_feedData(shell,
                                                            "unknown\necho byte\n",
                                                            strlen("unknown\necho byte\n"));
        XCS_TEST_CHECK(feedResult == XConsoleResult_Ok,
                   "feed data consumes all commands");
        XCS_TEST_CHECK(strstr(transport.output, "byte"),
                       "feed data executes command after failure");
    }
    memcpy(transport.input, "echo pump\n", sizeof("echo pump\n") - 1u);
    transport.inputLength = sizeof("echo pump\n") - 1u;
    transport.inputPosition = 0;
    transport.length = 0;
    transport.output[0] = '\0';
    while (transport.inputPosition < transport.inputLength)
        XCS_TEST_CHECK(XConsoleShell_pump(shell, 3) == XConsoleResult_Ok,
                       "callback transport pump");
#if XCONSOLE_SHELL_ASYNC_OUTPUT_ON
    XCS_TEST_CHECK(XConsoleShell_flushOutput(shell),
                   "callback transport pump flush");
#endif
    XCS_TEST_CHECK(strstr(transport.output, "pump") != NULL,
                   "callback transport pump output");
    {
        char oversized[XCONSOLE_SHELL_LINE_BUFFER_SIZE];
        const char suffix[] = "\necho after-overflow\n";
        memset(oversized, 'x', sizeof(oversized));
        memcpy(oversized, "echo ", 5);
        XCS_TEST_CHECK(XConsoleShell_feedData(shell, oversized, sizeof(oversized)) ==
                           XConsoleResult_ResourceLimit,
                       "oversized line enters discard state");
        transport.length = 0;
        transport.output[0] = '\0';
        XCS_TEST_CHECK(XConsoleShell_feedData(shell, suffix, sizeof(suffix) - 1u) ==
                           XConsoleResult_Ok &&
                           strstr(transport.output, "after-overflow") &&
                           !strstr(transport.output, "xxxxxxxx"),
                       "oversized line prefix is never executed");
    }
#if XCONSOLE_SHELL_HISTORY_ON
    XConsoleShell_clearHistory(shell);
    XCS_TEST_CHECK(XConsoleShellTest_runLine(shell, &transport, "echo first",
                                             XConsoleResult_Ok, "first"),
                   "history first command");
    XCS_TEST_CHECK(XConsoleShellTest_runLine(shell, &transport, "echo second",
                                             XConsoleResult_Ok, "second"),
                   "history second command");
    XCS_TEST_CHECK(XConsoleShellTest_runLine(shell, &transport, "history",
                                             XConsoleResult_Ok, "echo first"),
                   "history command");
    XCS_TEST_CHECK(XConsoleShell_historyCount(shell) == 3 &&
                       strcmp(XConsoleShell_historyAt(shell, 0), "echo first") == 0 &&
                       strcmp(XConsoleShell_historyAt(shell, 1), "echo second") == 0,
                   "history order");
#if XCONSOLE_SHELL_LINE_EDITOR_ON
    XConsoleShell_clearHistory(shell);
    XCS_TEST_CHECK(XConsoleShell_feedData(shell, "echo browse\n", 12) == XConsoleResult_Ok,
                   "history browse seed");
    transport.length = 0;
    transport.output[0] = '\0';
    XCS_TEST_CHECK(XConsoleShell_feedData(shell, "\x1b[A\n", 4) == XConsoleResult_Ok &&
                       strstr(transport.output, "browse"),
                   "history up arrow");
#endif
    XConsoleShell_clearHistory(shell);
    XCS_TEST_CHECK(XConsoleShell_historyCount(shell) == 0, "history clear");
#endif
#if XCONSOLE_SHELL_STATS_ON
    {
        XConsoleShellStats stats;
        XConsoleShell_clearStats(shell);
        XCS_TEST_CHECK(XConsoleShellTest_runLine(shell, &transport, "echo counted",
                                                 XConsoleResult_Ok, "counted"),
                       "stats counted command");
        XCS_TEST_CHECK(XConsoleShellTest_runLine(shell, &transport, "stats",
                                                 XConsoleResult_Ok, "行数="),
                       "stats command");
        XCS_TEST_CHECK(XConsoleShell_stats(shell, &stats) &&
                       stats.processedLines == 2 && stats.successfulCommands == 2 &&
                       stats.failedCommands == 0 && stats.outputBytes > 0 &&
                       stats.registeredCommands > 0, "stats snapshot");
        XConsoleShell_clearStats(shell);
        XCS_TEST_CHECK(XConsoleShell_stats(shell, &stats) &&
                       stats.processedLines == 0 && stats.outputBytes == 0,
                       "stats clear");
    }
#endif
#if XCONSOLE_SHELL_CLEAR_ON
    XCS_TEST_CHECK(XConsoleShellTest_runLine(shell, &transport, "clear",
                                             XConsoleResult_Ok, "\x1b[2J\x1b[H"),
                   "clear command");
#endif
#if XCONSOLE_SHELL_RESET_ON
    transport.resetResult = XSystemResult_Ok;
    XSystem_setResetHandler(XConsoleShellTest_resetHandler, &transport);
    XCS_TEST_CHECK(XConsoleShellTest_runLine(shell, &transport, "reset",
                                             XConsoleResult_Ok,
                                             "reset requested") &&
                       transport.resetCount == 1u &&
                       transport.resetReason == XSystemResetReason_Shell,
                   "reset command");
    XCS_TEST_CHECK(XConsoleShell_processLine(shell, "reset extra",
                                             strlen("reset extra")) ==
                       XConsoleResult_InvalidArgument,
                   "reset invalid argument");
    XCS_TEST_CHECK(XSystem_reset((XSystemResetReason)99) ==
                       XSystemResult_InvalidArgument,
                   "reset invalid reason");
    transport.resetResult = XSystemResult_PermissionDenied;
    XCS_TEST_CHECK(XConsoleShellTest_runLine(shell, &transport, "reset",
                                             XConsoleResult_PermissionDenied,
                                             "权限不足"),
                   "reset permission denied");
    transport.resetResult = XSystemResult_NotSupported;
    XCS_TEST_CHECK(XConsoleShellTest_runLine(shell, &transport, "reset",
                                             XConsoleResult_NotSupported,
                                             "backend unavailable"),
                   "reset unavailable backend");
    XSystem_setResetHandler(NULL, NULL);
#endif
#if XCONSOLE_SHELL_REBOOT_ON
    transport.rebootResult = XSystemResult_Ok;
    XSystem_setRebootHandler(XConsoleShellTest_rebootHandler, &transport);
    XCS_TEST_CHECK(XConsoleShellTest_runLine(shell, &transport, "reboot",
                                             XConsoleResult_Ok,
                                             "reboot requested") &&
                       transport.rebootCount == 1u &&
                       transport.rebootMode == XSystemRebootMode_Normal,
                   "reboot command");
    XCS_TEST_CHECK(XConsoleShell_processLine(shell, "reboot extra",
                                             strlen("reboot extra")) ==
                       XConsoleResult_InvalidArgument,
                   "reboot invalid argument");
    XCS_TEST_CHECK(XSystem_reboot((XSystemRebootMode)99) ==
                       XSystemResult_InvalidArgument,
                   "reboot invalid mode");
    transport.rebootResult = XSystemResult_PermissionDenied;
    XCS_TEST_CHECK(XConsoleShellTest_runLine(shell, &transport, "reboot",
                                             XConsoleResult_PermissionDenied,
                                             "权限不足"),
                   "reboot permission denied");
    transport.rebootResult = XSystemResult_NotSupported;
    XCS_TEST_CHECK(XConsoleShellTest_runLine(shell, &transport, "reboot",
                                             XConsoleResult_NotSupported,
                                             "backend unavailable"),
                   "reboot unavailable backend");
    XSystem_setRebootHandler(NULL, NULL);
#endif
#if XCONSOLE_SHELL_SHUTDOWN_ON
    transport.shutdownResult = XSystemResult_Ok;
    XSystem_setShutdownHandler(XConsoleShellTest_shutdownHandler, &transport);
    XCS_TEST_CHECK(XConsoleShellTest_runLine(shell, &transport, "shutdown",
                                             XConsoleResult_Ok,
                                             "已请求关闭") &&
                       transport.shutdownCount == 1u &&
                       !XConsoleShell_isRunning(shell),
                   "shutdown command");
    XConsoleShell_setRunning(shell, true);
    XCS_TEST_CHECK(XConsoleShell_processLine(shell, "shutdown extra",
                                             strlen("shutdown extra")) ==
                       XConsoleResult_InvalidArgument,
                   "shutdown invalid argument");
    transport.shutdownResult = XSystemResult_PermissionDenied;
    XCS_TEST_CHECK(XConsoleShellTest_runLine(shell, &transport, "shutdown",
                                             XConsoleResult_PermissionDenied,
                                             "权限不足") &&
                       XConsoleShell_isRunning(shell),
                   "shutdown permission denied");
    transport.shutdownResult = XSystemResult_NotSupported;
    XCS_TEST_CHECK(XConsoleShellTest_runLine(shell, &transport, "shutdown",
                                             XConsoleResult_Ok,
                                             "已请求关闭") &&
                       !XConsoleShell_isRunning(shell),
                   "shutdown unavailable backend exits Shell");
    XConsoleShell_setRunning(shell, true);
    XSystem_setShutdownHandler(NULL, NULL);
#endif
#if XCONSOLE_SHELL_EXIT_ON
    XCS_TEST_CHECK(XConsoleShellTest_runLine(shell, &transport, "exit",
                                             XConsoleResult_Ok,
                                             "已请求退出") &&
                       !XConsoleShell_isRunning(shell),
                   "exit command");
    XConsoleShell_setRunning(shell, true);
    XCS_TEST_CHECK(XConsoleShell_processLine(shell, "exit extra",
                                             strlen("exit extra")) ==
                       XConsoleResult_InvalidArgument &&
                       XConsoleShell_isRunning(shell),
                   "exit invalid argument");
#endif
#if XCONSOLE_SHELL_ASYNC_ON
    {
        const char asyncInput[] = "echo event-input\n";
        transport.inputPosition = 0;
        transport.inputLength = sizeof(asyncInput) - 1u;
        memcpy(transport.input, asyncInput, transport.inputLength);
        transport.length = 0;
        transport.output[0] = '\0';
        XCS_TEST_CHECK(XConsoleShell_startAsync(shell) &&
                           XConsoleShell_isAsyncRunning(shell),
                       "start event async Shell");
        XCS_TEST_CHECK(XConsoleShell_notifyInput(shell),
                       "post event async input");
#if XCONSOLE_SHELL_ASYNC_RUN_MODE == XCONSOLE_SHELL_ASYNC_MODE_EVENT_DISPATCHER
        XCoreApplication_processEvents(XEventLoop_AllEvents);
#else
        {
            size_t waitCount = 0;
            while (transport.inputPosition < transport.inputLength && waitCount++ < 100u)
                XThread_usleep(1000);
        }
#endif
#if XCONSOLE_SHELL_ASYNC_OUTPUT_ON
        XCS_TEST_CHECK(XConsoleShell_flushOutput(shell),
                       "flush event async output");
#endif
        XCS_TEST_CHECK(strstr(transport.output, "event-input") != NULL,
                       "event async input handled");
        XCS_TEST_CHECK(XConsoleShell_stopAsync(shell, 0) &&
                           !XConsoleShell_isAsyncRunning(shell),
                       "stop event async Shell");
    }
#endif
#if XCONSOLE_SHELL_GPIO_ON
#if XCONSOLE_SHELL_GPIO_LIST_ON
    XCS_TEST_CHECK(XConsoleShellTest_runLine(shell, &transport, "gpio list",
                                             XConsoleResult_Ok,
                                             "没有打开的引脚"),
                   "gpio empty list");
#endif
#if XCONSOLE_SHELL_GPIO_OPEN_ON && XCONSOLE_SHELL_GPIO_CLOSE_ON
    transport.gpioAllow = false;
    XCS_TEST_CHECK(XConsoleShellTest_runLine(
                       shell, &transport,
                       "gpio open 0 13 output --initial 0",
                       XConsoleResult_PermissionDenied, "策略拒绝"),
                   "gpio policy denied");
    transport.gpioAllow = true;
    XCS_TEST_CHECK(XConsoleShellTest_runLine(
                       shell, &transport,
                       "gpio open 0 13 output --pull up --drive open-drain "
                       "--active low --initial 0",
                       XConsoleResult_Ok, "打开: 成功"),
                   "gpio open");
    XCS_TEST_CHECK(XConsoleShell_processLine(
                       shell, "gpio open 0 13 output",
                       strlen("gpio open 0 13 output")) ==
                       XConsoleResult_ResourceLimit,
                   "gpio duplicate open");
#if XCONSOLE_SHELL_GPIO_LIST_ON
    XCS_TEST_CHECK(XConsoleShellTest_runLine(shell, &transport, "gpio list",
                                             XConsoleResult_Ok,
                                             "open-drain"),
                   "gpio populated list");
#endif
#if XCONSOLE_SHELL_GPIO_INFO_ON
    XCS_TEST_CHECK(XConsoleShellTest_runLine(shell, &transport,
                                             "gpio info 0 13",
                                             XConsoleResult_Ok,
                                             "controller=0 line=13"),
                   "gpio info");
#endif
#if XCONSOLE_SHELL_GPIO_WRITE_ON
    XCS_TEST_CHECK(XConsoleShellTest_runLine(shell, &transport,
                                             "gpio write 0 13 1",
                                             XConsoleResult_Ok, "写入: 成功"),
                   "gpio write");
#endif
#if XCONSOLE_SHELL_GPIO_READ_ON
    XCS_TEST_CHECK(XConsoleShellTest_runLine(shell, &transport,
                                             "gpio read 0 13",
                                             XConsoleResult_Ok, "level=1"),
                   "gpio physical read");
    XCS_TEST_CHECK(XConsoleShellTest_runLine(shell, &transport,
                                             "gpio read 0 13 --active",
                                             XConsoleResult_Ok, "active=no"),
                   "gpio active read");
#endif
#if XCONSOLE_SHELL_GPIO_TOGGLE_ON
    XCS_TEST_CHECK(XConsoleShellTest_runLine(shell, &transport,
                                             "gpio toggle 0 13",
                                             XConsoleResult_Ok, "翻转: 成功"),
                   "gpio toggle");
#endif
#if XCONSOLE_SHELL_GPIO_CONFIGURE_ON
    XCS_TEST_CHECK(XConsoleShellTest_runLine(
                       shell, &transport,
                       "gpio configure 0 13 --direction input --pull down "
                       "--drive push-pull --initial keep --active high "
                       "--debounce 10",
                       XConsoleResult_Ok, "配置: 成功"),
                   "gpio configure");
#endif
#if XCONSOLE_SHELL_GPIO_INTERRUPT_ON && XCONSOLE_SHELL_GPIO_CONFIGURE_ON
    XCS_TEST_CHECK(XConsoleShellTest_runLine(shell, &transport,
                                             "gpio irq set 0 13 rising",
                                             XConsoleResult_Ok, "中断设置: 成功"),
                   "gpio irq edge");
    XCS_TEST_CHECK(XConsoleShellTest_runLine(shell, &transport,
                                             "gpio irq enable 0 13",
                                             XConsoleResult_Ok,
                                             "中断使能: 成功"),
                   "gpio irq enable");
    XCS_TEST_CHECK(XConsoleShellTest_runLine(shell, &transport,
                                             "gpio irq status 0 13",
                                             XConsoleResult_Ok,
                                             "irq=enabled"),
                   "gpio irq status");
    XCS_TEST_CHECK(XConsoleShellTest_runLine(
                       shell, &transport,
                       "gpio irq wait 0 13 --count 1 --timeout 100",
                       XConsoleResult_Ok, "events=1 timeout=no"),
                   "gpio irq wait");
    XCS_TEST_CHECK(XConsoleShellTest_runLine(shell, &transport,
                                             "gpio irq disable 0 13",
                                             XConsoleResult_Ok,
                                             "中断禁用: 成功"),
                   "gpio irq disable");
#endif
    XCS_TEST_CHECK(XConsoleShellTest_runLine(shell, &transport,
                                             "gpio close 0 13",
                                             XConsoleResult_Ok, "关闭: 成功"),
                   "gpio close");
#if XCONSOLE_SHELL_GPIO_INFO_ON
    XCS_TEST_CHECK(XConsoleShellTest_runLine(shell, &transport,
                                             "gpio info 0 13",
                                             XConsoleResult_InvalidArgument,
                                             "not open"),
                   "gpio closed info");
#endif
    XCS_TEST_CHECK(transport.gpioAuthorizeCount > 0u,
                   "gpio authorize callback used");
#endif
#endif
#if XCONSOLE_SHELL_ADC_ON
#if XCONSOLE_SHELL_ADC_LIST_ON
    XCS_TEST_CHECK(XConsoleShellTest_runLine(shell, &transport, "adc list",
                                             XConsoleResult_Ok,
                                             "没有打开的通道"),
                   "adc empty list");
#endif
#if XCONSOLE_SHELL_ADC_OPEN_ON
    transport.adcAllow = false;
    XCS_TEST_CHECK(XConsoleShellTest_runLine(
                       shell, &transport, "adc open 0 0",
                       XConsoleResult_PermissionDenied, "策略拒绝"),
                   "adc policy denied");
    transport.adcAllow = true;
    XCS_TEST_CHECK(XConsoleShellTest_runLine(
                       shell, &transport,
                       "adc open 0 0 --resolution 12 --reference 3300",
                       XConsoleResult_Ok, NULL),
                   "adc open");
#endif
#if XCONSOLE_SHELL_ADC_INFO_ON
    XCS_TEST_CHECK(XConsoleShellTest_runLine(shell, &transport, "adc info 0 0",
                                             XConsoleResult_Ok,
                                             "resolution=12"),
                   "adc info");
#endif
#if XCONSOLE_SHELL_ADC_READ_ON
    XCS_TEST_CHECK(XConsoleShellTest_runLine(shell, &transport, "adc read 0 0",
                                             XConsoleResult_Ok, "raw=1234"),
                   "adc raw read");
    XCS_TEST_CHECK(XConsoleShellTest_runLine(shell, &transport,
                                             "adc read 0 0 --mv",
                                             XConsoleResult_Ok, "994 mV"),
                   "adc millivolt read");
#endif
#if XCONSOLE_SHELL_ADC_CONFIGURE_ON
    XCS_TEST_CHECK(XConsoleShellTest_runLine(
                       shell, &transport, "adc configure 0 0 --sample-us 5",
                       XConsoleResult_Ok, NULL),
                   "adc configure");
#endif
#if XCONSOLE_SHELL_ADC_CLOSE_ON
    XCS_TEST_CHECK(XConsoleShellTest_runLine(shell, &transport, "adc close 0 0",
                                             XConsoleResult_Ok, NULL),
                   "adc close");
#endif
    XCS_TEST_CHECK(transport.adcAuthorizeCount > 0u,
                   "adc authorize callback used");
#endif
#if XCONSOLE_SHELL_PWM_ON
#if XCONSOLE_SHELL_PWM_LIST_ON
    XCS_TEST_CHECK(XConsoleShellTest_runLine(shell, &transport, "pwm list",
                                             XConsoleResult_Ok,
                                             "没有打开的通道"),
                   "pwm empty list");
#endif
#if XCONSOLE_SHELL_PWM_OPEN_ON
    transport.pwmAllow = false;
    XCS_TEST_CHECK(XConsoleShellTest_runLine(
                       shell, &transport, "pwm open 0 1",
                       XConsoleResult_PermissionDenied, "策略拒绝"),
                   "pwm policy denied");
    transport.pwmAllow = true;
    XCS_TEST_CHECK(XConsoleShellTest_runLine(
                       shell, &transport,
                       "pwm open 0 1 --frequency 1000 --duty 5000",
                       XConsoleResult_Ok, NULL),
                   "pwm open");
#endif
#if XCONSOLE_SHELL_PWM_INFO_ON
    XCS_TEST_CHECK(XConsoleShellTest_runLine(shell, &transport, "pwm info 0 1",
                                             XConsoleResult_Ok,
                                             "frequency=1000"),
                   "pwm info");
#endif
#if XCONSOLE_SHELL_PWM_CONFIGURE_ON
    XCS_TEST_CHECK(XConsoleShellTest_runLine(
                       shell, &transport, "pwm configure 0 1 --polarity inverted",
                       XConsoleResult_Ok, NULL),
                   "pwm configure");
#endif
#if XCONSOLE_SHELL_PWM_START_ON
    XCS_TEST_CHECK(XConsoleShellTest_runLine(shell, &transport, "pwm start 0 1",
                                             XConsoleResult_Ok, NULL),
                   "pwm start");
#endif
#if XCONSOLE_SHELL_PWM_SET_FREQUENCY_ON
    XCS_TEST_CHECK(XConsoleShellTest_runLine(
                       shell, &transport, "pwm set-frequency 0 1 2000",
                       XConsoleResult_Ok, NULL),
                   "pwm set frequency");
#endif
#if XCONSOLE_SHELL_PWM_SET_DUTY_ON
    XCS_TEST_CHECK(XConsoleShellTest_runLine(
                       shell, &transport, "pwm set-duty 0 1 7500",
                       XConsoleResult_Ok, NULL),
                   "pwm set duty");
#endif
#if XCONSOLE_SHELL_PWM_STOP_ON
    XCS_TEST_CHECK(XConsoleShellTest_runLine(shell, &transport, "pwm stop 0 1",
                                             XConsoleResult_Ok, NULL),
                   "pwm stop");
#endif
#if XCONSOLE_SHELL_PWM_CLOSE_ON
    XCS_TEST_CHECK(XConsoleShellTest_runLine(shell, &transport, "pwm close 0 1",
                                             XConsoleResult_Ok, NULL),
                   "pwm close");
#endif
    XCS_TEST_CHECK(transport.pwmAuthorizeCount > 0u,
                   "pwm authorize callback used");
#endif
#if XCONSOLE_SHELL_I2C_ON
#if XCONSOLE_SHELL_I2C_LIST_ON
    XCS_TEST_CHECK(XConsoleShellTest_runLine(shell, &transport, "i2c list",
                                             XConsoleResult_Ok,
                                             "没有打开的从机"),
                   "i2c empty list");
#endif
#if XCONSOLE_SHELL_I2C_OPEN_ON
    transport.i2cAllow = false;
    XCS_TEST_CHECK(XConsoleShellTest_runLine(
                       shell, &transport, "i2c open 0 0x50",
                       XConsoleResult_PermissionDenied, "策略拒绝"),
                   "i2c policy denied");
    transport.i2cAllow = true;
    XCS_TEST_CHECK(XConsoleShellTest_runLine(
                       shell, &transport, "i2c open 0 0x50 --speed 400000",
                       XConsoleResult_Ok, "打开: 成功"),
                   "i2c open");
#endif
#if XCONSOLE_SHELL_I2C_INFO_ON
    XCS_TEST_CHECK(XConsoleShellTest_runLine(shell, &transport, "i2c info 0 0x50",
                                             XConsoleResult_Ok, "speed=400000"),
                   "i2c info");
#endif
#if XCONSOLE_SHELL_I2C_WRITE_ON
    XCS_TEST_CHECK(XConsoleShellTest_runLine(shell, &transport,
                                             "i2c write 0 0x50 001122",
                                             XConsoleResult_Ok, "写入: 成功"),
                   "i2c write");
#endif
#if XCONSOLE_SHELL_I2C_READ_ON
    XCS_TEST_CHECK(XConsoleShellTest_runLine(shell, &transport,
                                             "i2c read 0 0x50 2",
                                             XConsoleResult_Ok, "i2c: 读取:"),
                   "i2c read");
#endif
#if XCONSOLE_SHELL_I2C_WRITEREAD_ON
    XCS_TEST_CHECK(XConsoleShellTest_runLine(shell, &transport,
                                             "i2c writeread 0 0x50 0001 2",
                                             XConsoleResult_Ok, "i2c: 读取:"),
                   "i2c write-read");
#endif
#if XCONSOLE_SHELL_I2C_CLOSE_ON
    XCS_TEST_CHECK(XConsoleShellTest_runLine(shell, &transport, "i2c close 0 0x50",
                                             XConsoleResult_Ok, "关闭: 成功"),
                   "i2c close");
#endif
#if XCONSOLE_SHELL_I2C_OPEN_ON
    XCS_TEST_CHECK(XConsoleShellTest_runLine(
                       shell, &transport, "i2c open 0 0x180",
                       XConsoleResult_InvalidArgument, NULL),
                   "i2c reject ten-bit address without mode");
    XCS_TEST_CHECK(XConsoleShellTest_runLine(
                       shell, &transport, "i2c open 0 0x180 --ten-bit",
                       XConsoleResult_Ok, "打开: 成功"),
                   "i2c ten-bit open");
#endif
#if XCONSOLE_SHELL_I2C_OPEN_ON && XCONSOLE_SHELL_I2C_INFO_ON
    XCS_TEST_CHECK(XConsoleShellTest_runLine(
                       shell, &transport, "i2c info 0 0x180 --ten-bit",
                       XConsoleResult_Ok, "mode=10bit"),
                   "i2c ten-bit info");
#endif
#if XCONSOLE_SHELL_I2C_OPEN_ON && XCONSOLE_SHELL_I2C_WRITE_ON
    XCS_TEST_CHECK(XConsoleShellTest_runLine(
                       shell, &transport, "i2c write 0 0x180 001122 --ten-bit",
                       XConsoleResult_Ok, "写入: 成功"),
                   "i2c ten-bit write");
#endif
#if XCONSOLE_SHELL_I2C_OPEN_ON && XCONSOLE_SHELL_I2C_READ_ON
    XCS_TEST_CHECK(XConsoleShellTest_runLine(
                       shell, &transport, "i2c read 0 0x180 2 --ten-bit",
                       XConsoleResult_Ok, "i2c: 读取:"),
                   "i2c ten-bit read");
#endif
#if XCONSOLE_SHELL_I2C_OPEN_ON && XCONSOLE_SHELL_I2C_WRITEREAD_ON
    XCS_TEST_CHECK(XConsoleShellTest_runLine(
                       shell, &transport,
                       "i2c writeread 0 0x180 0001 2 --ten-bit",
                       XConsoleResult_Ok, "i2c: 读取:"),
                   "i2c ten-bit write-read");
#endif
#if XCONSOLE_SHELL_I2C_OPEN_ON && XCONSOLE_SHELL_I2C_CLOSE_ON
    XCS_TEST_CHECK(XConsoleShellTest_runLine(
                       shell, &transport, "i2c close 0 0x180 --ten-bit",
                       XConsoleResult_Ok, "关闭: 成功"),
                   "i2c ten-bit close");
#endif
    XCS_TEST_CHECK(transport.i2cAuthorizeCount > 0u,
                   "i2c authorize callback used");
#endif
#if XCONSOLE_SHELL_SPI_ON
#if XCONSOLE_SHELL_SPI_LIST_ON
    XCS_TEST_CHECK(XConsoleShellTest_runLine(shell, &transport, "spi list",
                                             XConsoleResult_Ok,
                                             "没有打开的从机"),
                   "spi empty list");
#endif
#if XCONSOLE_SHELL_SPI_OPEN_ON
    transport.spiAllow = false;
    XCS_TEST_CHECK(XConsoleShellTest_runLine(
                       shell, &transport, "spi open 0 0",
                       XConsoleResult_PermissionDenied, "策略拒绝"),
                   "spi policy denied");
    transport.spiAllow = true;
    XCS_TEST_CHECK(XConsoleShellTest_runLine(
                       shell, &transport, "spi open 0 0 --speed 2000000 --mode 3",
                       XConsoleResult_Ok, "打开: 成功"),
                   "spi open");
#endif
#if XCONSOLE_SHELL_SPI_INFO_ON
    XCS_TEST_CHECK(XConsoleShellTest_runLine(shell, &transport, "spi info 0 0",
                                             XConsoleResult_Ok, "mode=3"),
                   "spi info");
#endif
#if XCONSOLE_SHELL_SPI_TRANSFER_ON
    XCS_TEST_CHECK(XConsoleShellTest_runLine(shell, &transport,
                                             "spi transfer 0 0 0102",
                                             XConsoleResult_Ok, "spi: 接收: 01 02"),
                   "spi transfer");
#endif
#if XCONSOLE_SHELL_SPI_CLOSE_ON
    XCS_TEST_CHECK(XConsoleShellTest_runLine(shell, &transport, "spi close 0 0",
                                             XConsoleResult_Ok, "关闭: 成功"),
                   "spi close");
#endif
    XCS_TEST_CHECK(transport.spiAuthorizeCount > 0u,
                   "spi authorize callback used");
#endif
#if XCONSOLE_SHELL_CAN_ON
#if XCONSOLE_SHELL_CAN_LIST_ON
    XCS_TEST_CHECK(XConsoleShellTest_runLine(shell, &transport, "can list",
                                             XConsoleResult_Ok,
                                             "没有打开的通道"),
                   "can empty list");
#endif
#if XCONSOLE_SHELL_CAN_OPEN_ON
    transport.canAllow = false;
    XCS_TEST_CHECK(XConsoleShellTest_runLine(
                       shell, &transport, "can open 0 0 --name testcan",
                       XConsoleResult_PermissionDenied, "策略拒绝"),
                   "can policy denied");
    transport.canAllow = true;
    XCS_TEST_CHECK(XConsoleShellTest_runLine(
                       shell, &transport,
                       "can open 0 0 --name testcan --bitrate 500000 --fd "
                       "--data-bitrate 1000000 --loopback",
                       XConsoleResult_Ok, "打开: 成功"),
                   "can open");
#endif
#if XCONSOLE_SHELL_CAN_OPEN_ON
#if XCONSOLE_SHELL_CAN_INFO_ON
    XCS_TEST_CHECK(XConsoleShellTest_runLine(shell, &transport, "can info 0 0",
                                             XConsoleResult_Ok,
                                             "format=fd"),
                   "can info");
#endif
#if XCONSOLE_SHELL_CAN_STATUS_ON
    XCS_TEST_CHECK(XConsoleShellTest_runLine(shell, &transport,
                                             "can status 0 0",
                                             XConsoleResult_Ok,
                                             "state=stopped"),
                   "can stopped status");
#endif
#if XCONSOLE_SHELL_CAN_START_ON
    XCS_TEST_CHECK(XConsoleShellTest_runLine(shell, &transport, "can start 0 0",
                                             XConsoleResult_Ok, "启动: 成功"),
                   "can start");
#endif
#if XCONSOLE_SHELL_CAN_SEND_ON
    XCS_TEST_CHECK(XConsoleShellTest_runLine(
                       shell, &transport, "can send 0 0 0x123 0x11 0xaa",
                       XConsoleResult_Ok, "发送: 成功"),
                   "can send");
#endif
#if XCONSOLE_SHELL_CAN_RECEIVE_ON
    XCS_TEST_CHECK(XConsoleShellTest_runLine(
                       shell, &transport,
                       "can receive 0 0 --count 1 --timeout 10",
                       XConsoleResult_Ok, "id=0x00000123"),
                   "can receive");
#endif
#if XCONSOLE_SHELL_CAN_FILTER_ON
    XCS_TEST_CHECK(XConsoleShellTest_runLine(
                       shell, &transport,
                       "can filter add 0 0 0x100 0x700 --type data",
                       XConsoleResult_Ok, "过滤器添加: id=1"),
                   "can filter add");
    XCS_TEST_CHECK(XConsoleShellTest_runLine(
                       shell, &transport, "can filter remove 0 0 1",
                       XConsoleResult_Ok, "过滤器移除: 成功"),
                   "can filter remove");
    XCS_TEST_CHECK(XConsoleShellTest_runLine(
                       shell, &transport, "can filter clear 0 0",
                       XConsoleResult_Ok, "过滤器清空: 成功"),
                   "can filter clear");
#endif
#if XCONSOLE_SHELL_CAN_STATUS_ON
    XCS_TEST_CHECK(XConsoleShellTest_runLine(shell, &transport,
                                             "can status 0 0",
                                             XConsoleResult_Ok,
                                             "state=error-active"),
                   "can active status");
#endif
#if XCONSOLE_SHELL_CAN_STOP_ON
    XCS_TEST_CHECK(XConsoleShellTest_runLine(shell, &transport, "can stop 0 0",
                                             XConsoleResult_Ok, "停止: 成功"),
                   "can stop");
#endif
#if XCONSOLE_SHELL_CAN_CLOSE_ON
    XCS_TEST_CHECK(XConsoleShellTest_runLine(shell, &transport, "can close 0 0",
                                             XConsoleResult_Ok, "关闭: 成功"),
                   "can close");
#endif
    XCS_TEST_CHECK(transport.canAuthorizeCount > 0u,
                   "can authorize callback used");
#endif
#endif
#if XCONSOLE_SHELL_COMPLETION_ON
    transport.length = 0;
    transport.output[0] = '\0';
    XCS_TEST_CHECK(XConsoleShell_feedData(shell, "ec\t completed\n", 14) == XConsoleResult_Ok &&
                       strstr(transport.output, "completed"), "tab completion");
#endif
    session = XConsoleShell_session(shell);
    XCS_TEST_CHECK(session && session->currentPath[0] != '\0', "session cwd");
#if XCONSOLE_SHELL_DATETIME_ON && XCONSOLE_SHELL_DATE_ON
    XCS_TEST_CHECK(XConsoleShellTest_runLine(shell, &transport, "date",
                                             XConsoleResult_Ok, "-"),
                   "date local command");
    XCS_TEST_CHECK(XConsoleShellTest_runLine(shell, &transport, "date -u",
                                             XConsoleResult_Ok, "-"),
                   "date UTC command");
    XCS_TEST_CHECK(XConsoleShellTest_runLine(shell, &transport, "date -I",
                                             XConsoleResult_Ok, "-"),
                   "date ISO command");
    XCS_TEST_CHECK(XConsoleShellTest_runLine(shell, &transport,
                                             "date +%Y-%m-%dT%H:%M:%S",
                                             XConsoleResult_Ok, "T"),
                   "date custom format command");
    XCS_TEST_CHECK(XConsoleShellTest_runLine(shell, &transport, "date +%Q",
                                             XConsoleResult_Failed, "date:"),
                   "date unsupported format command");
#endif
#if XCONSOLE_SHELL_MEMORY_ON && XCONSOLE_SHELL_MEMORY_POOL_ON
    {
        void* memoryBlock = XMultiPool_global_malloc(16u);
        bool memoryCommandOk;
        XCS_TEST_CHECK(memoryBlock != NULL, "memory pool test allocation");
        memoryCommandOk = XConsoleShellTest_runLine(shell, &transport, "mem",
                                                     XConsoleResult_Ok, "XMultiPool");
        memoryCommandOk = memoryCommandOk &&
            XConsoleShellTest_runLine(shell, &transport, "mem -v",
                                       XConsoleResult_Ok, "子池[0]");
        XMultiPool_global_free(memoryBlock);
        XCS_TEST_CHECK(memoryCommandOk, "memory pool usage command");
    }
#endif
#if XCONSOLE_SHELL_INFO_ON
    XCS_TEST_CHECK(XConsoleShellTest_runLine(shell, &transport, "info",
                                             XConsoleResult_Ok, "XConsoleShell"),
                   "info command");
    XCS_TEST_CHECK(XConsoleShell_processLine(shell, "info extra", strlen("info extra")) ==
                       XConsoleResult_InvalidArgument,
                   "info invalid argument");
#endif
#if XCONSOLE_SHELL_UPTIME_ON
    XCS_TEST_CHECK(XConsoleShellTest_runLine(shell, &transport, "uptime",
                                             XConsoleResult_Ok, "uptime:"),
                   "uptime command");
    XCS_TEST_CHECK(XConsoleShell_processLine(shell, "uptime extra", strlen("uptime extra")) ==
                       XConsoleResult_InvalidArgument,
                   "uptime invalid argument");
#endif
#if XCONSOLE_SHELL_TASKS_ON
    XCS_TEST_CHECK(XConsoleShellTest_runLine(shell, &transport, "tasks",
                                             XConsoleResult_Ok, "shell-main"),
                   "tasks command");
    XCS_TEST_CHECK(strstr(transport.output, "阻塞") != NULL,
                   "tasks state output");
    XCS_TEST_CHECK(XConsoleShell_setTaskProvider(shell, NULL, NULL),
                   "clear tasks provider");
    XCS_TEST_CHECK(XConsoleShellTest_runLine(shell, &transport, "tasks",
                                             XConsoleResult_NotSupported,
                                             "提供者不可用"),
                   "tasks unavailable command");
#endif
#if XCONSOLE_SHELL_NETWORK_ON
    {
        XConsoleResult networkResult;
        XCS_TEST_CHECK(XConsoleShellTest_runLine(shell, &transport, "net hostname",
                                                 XConsoleResult_Ok, NULL) &&
                           transport.length > 1u,
                       "network hostname command");
        transport.length = 0;
        transport.output[0] = '\0';
        networkResult = XConsoleShell_processLine(shell, "net ifconfig", strlen("net ifconfig"));
        XCS_TEST_CHECK(networkResult != XConsoleResult_UnknownCommand &&
                           networkResult != XConsoleResult_InvalidSyntax,
                       "network ifconfig command");
        transport.length = 0;
        transport.output[0] = '\0';
        networkResult = XConsoleShell_processLine(shell, "net resolve localhost",
                                                   strlen("net resolve localhost"));
        XCS_TEST_CHECK(networkResult != XConsoleResult_UnknownCommand &&
                           networkResult != XConsoleResult_InvalidSyntax,
                       "network resolve command");
#if XCONSOLE_SHELL_NET_PING_ON
        transport.length = 0;
        transport.output[0] = '\0';
        networkResult = XConsoleShell_processLine(shell, "net ping 127.0.0.1 -c 1 -W 500",
                                                   strlen("net ping 127.0.0.1 -c 1 -W 500"));
        XCS_TEST_CHECK(networkResult != XConsoleResult_UnknownCommand &&
                           networkResult != XConsoleResult_InvalidSyntax,
                       "network ping command");
        transport.length = 0;
        transport.output[0] = '\0';
        networkResult = XConsoleShell_processLine(shell, "net ping ::1 -c 1 -W 500",
                                                   strlen("net ping ::1 -c 1 -W 500"));
        XCS_TEST_CHECK(networkResult != XConsoleResult_UnknownCommand &&
                           networkResult != XConsoleResult_InvalidSyntax,
                       "network ping IPv6 command");
#endif

        /* Linux-style top-level ping must resolve too (reuses net ping handler). */
        transport.length = 0;
        transport.output[0] = '\0';
        networkResult = XConsoleShell_processLine(shell, "ping 127.0.0.1 -c 1 -W 500",
                                                   strlen("ping 127.0.0.1 -c 1 -W 500"));
        XCS_TEST_CHECK(networkResult != XConsoleResult_UnknownCommand &&
                           networkResult != XConsoleResult_InvalidSyntax,
                       "top-level ping command");
    }
#endif
#if XCONSOLE_SHELL_FS_PWD_ON
    XCS_TEST_CHECK(XConsoleShell_processLine(shell, "fs pwd", 6) == XConsoleResult_Ok,
                   "pwd command");
#endif
    filePath = XString_create_utf8("xconsole_shell_test.txt");
    XCS_TEST_CHECK(filePath != NULL, "file path");
    XFileSystem_removePermanent(filePath);
    fd = XFileSystem_open(filePath, XFileSystem_WriteOnly | XFileSystem_Create |
                          XFileSystem_Truncate, &error);
    XCS_TEST_CHECK(fd != XFD_INVALID, "create shell file");
    XCS_TEST_CHECK(XFileSystem_write(fd, fileText, sizeof(fileText) - 1) ==
                       (int64_t)(sizeof(fileText) - 1), "write shell file");
    XFileSystem_close(fd);
#if XCONSOLE_SHELL_SCRIPT_ON
    {
        XString* scriptPath = XString_create_utf8("xconsole_shell_script.txt");
        const char scriptText[] = "echo scripted\n";
        XCS_TEST_CHECK(scriptPath != NULL, "script path");
        fd = XFileSystem_open(scriptPath, XFileSystem_WriteOnly | XFileSystem_Create |
                              XFileSystem_Truncate, &error);
        XCS_TEST_CHECK(fd != XFD_INVALID, "create script file");
        XCS_TEST_CHECK(XFileSystem_write(fd, scriptText, sizeof(scriptText) - 1) ==
                           (int64_t)(sizeof(scriptText) - 1), "write script file");
        XFileSystem_close(fd);
        XCS_TEST_CHECK(XConsoleShell_processLine(shell, "source xconsole_shell_script.txt",
                                                 strlen("source xconsole_shell_script.txt")) ==
                           XConsoleResult_Ok && strstr(transport.output, "scripted"),
                       "source command");
        XFileSystem_removePermanent(scriptPath);
        XString_delete_base(scriptPath);
    }
#endif
#if XCONSOLE_SHELL_FS_CAT_ON
    result = XConsoleShell_processLine(shell, "fs cat xconsole_shell_test.txt", 31) ==
             XConsoleResult_Ok;
    XCS_TEST_CHECK(result && strstr(transport.output, fileText), "cat command");
#endif
#if XCONSOLE_SHELL_FS_HEXDUMP_ON
    XCS_TEST_CHECK(XConsoleShellTest_runLine(shell, &transport,
                                             "hexdump -C -n 16 xconsole_shell_test.txt",
                                             XConsoleResult_Ok, "73 68 65 6c 6c"),
                   "hexdump command");
    XCS_TEST_CHECK(XConsoleShellTest_runLine(shell, &transport,
                                             "fs hexdump -s 2 -n 4 xconsole_shell_test.txt",
                                             XConsoleResult_Ok, "65 6c 6c 2d"),
                   "fs hexdump options");
#endif
#if XCONSOLE_SHELL_FS_STAT_ON
    XCS_TEST_CHECK(XConsoleShell_processLine(shell, "fs stat xconsole_shell_test.txt", 31) ==
                       XConsoleResult_Ok && strstr(transport.output, "类型=文件"),
                   "stat command");
#endif

#if XCONSOLE_SHELL_FS_CHMOD_ON
    XCS_TEST_CHECK(XConsoleShellTest_runLine(
                       shell, &transport, "chmod 600 xconsole_shell_test.txt",
                       XConsoleResult_Ok, NULL),
                   "chmod octal");
    {
        XFileStat st;
        if (XFileSystem_stat(filePath, &st)) {
            XCS_TEST_CHECK((st.permissions & XFile_ReadOwner) != 0 &&
                               (st.permissions & XFile_WriteOwner) != 0 &&
                               (st.permissions &
                                (XFile_ReadGroup | XFile_WriteGroup |
                                 XFile_ReadOther | XFile_WriteOther)) == 0,
                           "chmod octal permissions");
        }
    }
    XCS_TEST_CHECK(XConsoleShellTest_runLine(
                       shell, &transport, "chmod u+x xconsole_shell_test.txt",
                       XConsoleResult_Ok, NULL),
                   "chmod symbolic add");
    {
        XFileStat st;
        if (XFileSystem_stat(filePath, &st)) {
            XCS_TEST_CHECK((st.permissions & XFile_ExeOwner) != 0,
                           "chmod symbolic owner execute");
        }
    }
    XCS_TEST_CHECK(XConsoleShellTest_runLine(
                       shell, &transport, "chmod a-w xconsole_shell_test.txt",
                       XConsoleResult_Ok, NULL),
                   "chmod symbolic clear write");
    {
        XFileStat st;
        if (XFileSystem_stat(filePath, &st)) {
            XCS_TEST_CHECK((st.permissions &
                            (XFile_WriteOwner | XFile_WriteGroup |
                             XFile_WriteOther)) == 0,
                           "chmod symbolic clear all write");
        }
    }
    XCS_TEST_CHECK(XConsoleShellTest_runLine(
                       shell, &transport,
                       "chmod 644 xconsole_shell_test.txt",
                       XConsoleResult_Ok, NULL),
                   "chmod reset to no exec");
    XCS_TEST_CHECK(XConsoleShellTest_runLine(
                       shell, &transport,
                       "chmod a+X xconsole_shell_test.txt",
                       XConsoleResult_Ok, NULL),
                   "chmod symbolic X on plain file");
    {
        XFileStat st;
        if (XFileSystem_stat(filePath, &st)) {
            XCS_TEST_CHECK((st.permissions &
                            (XFile_ExeOwner | XFile_ExeGroup |
                             XFile_ExeOther)) == 0,
                           "chmod symbolic X no exec on plain file");
        }
    }
    XCS_TEST_CHECK(XConsoleShellTest_runLine(
                       shell, &transport,
                       "chmod u+x xconsole_shell_test.txt",
                       XConsoleResult_Ok, NULL),
                   "chmod set one exec bit");
    XCS_TEST_CHECK(XConsoleShellTest_runLine(
                       shell, &transport,
                       "chmod a+X xconsole_shell_test.txt",
                       XConsoleResult_Ok, NULL),
                   "chmod symbolic X propagates exec");
    {
        XFileStat st;
        if (XFileSystem_stat(filePath, &st)) {
            XCS_TEST_CHECK((st.permissions & XFile_ExeOwner) != 0 &&
                               (st.permissions & XFile_ExeGroup) != 0 &&
                               (st.permissions & XFile_ExeOther) != 0,
                           "chmod symbolic X exec propagation");
        }
    }
    XCS_TEST_CHECK(XConsoleShellTest_runLine(
                       shell, &transport, "fs mkdir xconsole_shell_test_dir",
                       XConsoleResult_Ok, NULL),
                   "create chmod X test dir");
    XCS_TEST_CHECK(XConsoleShellTest_runLine(
                       shell, &transport, "chmod a+X xconsole_shell_test_dir",
                       XConsoleResult_Ok, NULL),
                   "chmod symbolic X on directory");
    {
        XString* dir = XString_create_utf8("xconsole_shell_test_dir");
        XFileStat st;
        if (dir && XFileSystem_stat(dir, &st)) {
            XCS_TEST_CHECK((st.permissions & XFile_ExeOwner) != 0 &&
                               (st.permissions & XFile_ExeGroup) != 0 &&
                               (st.permissions & XFile_ExeOther) != 0,
                           "chmod symbolic X exec on directory");
        }
        if (dir) XFileSystem_removePermanent(dir);
        XString_delete_base(dir);
    }
    XCS_TEST_CHECK(XConsoleShellTest_runLine(
                       shell, &transport, "chmod badmode xconsole_shell_test.txt",
                       XConsoleResult_InvalidArgument, NULL),
                   "chmod invalid mode");
#endif
#if XCONSOLE_SHELL_FS_LS_ON
    XCS_TEST_CHECK(XConsoleShellTest_runLine(shell, &transport, "fs ls .",
                                             XConsoleResult_Ok, "xconsole_shell_test.txt"),
                   "ls command");
    transport.length = 0;
    transport.output[0] = '\0';
    XCS_TEST_CHECK(XConsoleShell_processLine(shell, "ls", 2) ==
                       XConsoleResult_Ok && strstr(transport.output, "xconsole_shell_test.txt"),
                       "root ls command");
    XCS_TEST_CHECK(XConsoleShellTest_runLine(shell, &transport,
                                             "ls -l -- xconsole_shell_test.txt",
                                             XConsoleResult_Ok, "xconsole_shell_test.txt"),
                   "root ls options");
#endif
#if XCONSOLE_SHELL_FS_DF_ON
    XCS_TEST_CHECK(XConsoleShellTest_runLine(shell, &transport, "df .",
                                             XConsoleResult_Ok, "总计="),
                   "root df command");
#endif
#if XCONSOLE_SHELL_FS_DU_ON
    XCS_TEST_CHECK(XConsoleShellTest_runLine(shell, &transport, "fs du .",
                                             XConsoleResult_Ok, "  "),
                   "du command");
#endif
#if XCONSOLE_SHELL_FS_WC_ON
    XCS_TEST_CHECK(XConsoleShellTest_runLine(shell, &transport, "wc xconsole_shell_test.txt",
                                             XConsoleResult_Ok, "xconsole_shell_test.txt"),
                   "root wc command");
#endif
#if XCONSOLE_SHELL_FS_HEAD_ON
    XCS_TEST_CHECK(XConsoleShellTest_runLine(shell, &transport,
                                             "head -n 1 xconsole_shell_test.txt",
                                             XConsoleResult_Ok, fileText),
                   "head options");
    XCS_TEST_CHECK(XConsoleShellTest_runLine(shell, &transport,
                                             "head -n 0 xconsole_shell_test.txt",
                                             XConsoleResult_Ok, NULL) &&
                       transport.length == 0,
                   "head zero lines");
#endif
#if XCONSOLE_SHELL_FS_TAIL_ON
    XCS_TEST_CHECK(XConsoleShellTest_runLine(shell, &transport,
                                             "fs tail --lines 1 xconsole_shell_test.txt",
                                             XConsoleResult_Ok, fileText),
                   "tail options");
#endif
#if XCONSOLE_SHELL_FS_FIND_ON
    XCS_TEST_CHECK(XConsoleShellTest_runLine(shell, &transport,
                                             "find . -name xconsole_shell_test.txt",
                                             XConsoleResult_Ok, "xconsole_shell_test.txt"),
                   "find options");
#endif
#if XCONSOLE_SHELL_FS_FILE_ON
    XCS_TEST_CHECK(XConsoleShellTest_runLine(shell, &transport, "file xconsole_shell_test.txt",
                                             XConsoleResult_Ok, "常规文件"),
                   "file command");
#endif
#if XCONSOLE_SHELL_FS_CMP_ON
    XCS_TEST_CHECK(XConsoleShellTest_runLine(shell, &transport,
                                             "cmp xconsole_shell_test.txt xconsole_shell_test.txt",
                                             XConsoleResult_Ok, NULL),
                   "cmp command");
#endif
#if XCONSOLE_SHELL_FS_BASENAME_ON
    XCS_TEST_CHECK(XConsoleShellTest_runLine(shell, &transport, "basename /tmp/xin-shell-file",
                                             XConsoleResult_Ok, "xin-shell-file"),
                   "basename command");
#endif
#if XCONSOLE_SHELL_FS_DIRNAME_ON
    XCS_TEST_CHECK(XConsoleShellTest_runLine(shell, &transport, "dirname /tmp/xin-shell-file",
                                             XConsoleResult_Ok, "/tmp"),
                   "dirname command");
#endif
#if XCONSOLE_SHELL_EXTERNAL_PROCESS_ON && XProcess_ON
    XCS_TEST_CHECK(XConsoleShell_processLine(shell, "exec sh -c 'printf exec'", 24) ==
                       XConsoleResult_Ok && strstr(transport.output, "exec"),
                   "external process command");
#if XCONSOLE_SHELL_PIPE_ON
    XCS_TEST_CHECK(XConsoleShell_processLine(
                       shell, "exec --pipe sh -c 'printf piped' -- sh -c cat",
                       strlen("exec --pipe sh -c 'printf piped' -- sh -c cat")) ==
                       XConsoleResult_Ok && strstr(transport.output, "piped"),
                   "process pipe command");
#endif
#if XCONSOLE_SHELL_PROCESS_ASYNC_ON
    {
        size_t completed = 0;
        size_t attempts;
        transport.length = 0;
        transport.output[0] = '\0';
        XCS_TEST_CHECK(XConsoleShell_processLine(
                           shell, "exec --async sh -c 'printf async'",
                           strlen("exec --async sh -c 'printf async'")) ==
                           XConsoleResult_MoreOutput,
                       "async process start");
        for (attempts = 0; attempts < 300 && completed == 0; ++attempts)
            completed += XConsoleShell_pollProcesses(shell, 10);
#if XCONSOLE_SHELL_ASYNC_OUTPUT_ON
        XCS_TEST_CHECK(XConsoleShell_flushOutput(shell),
                       "async process output flush");
#endif
        XCS_TEST_CHECK(completed == 1 && strstr(transport.output, "async"),
                       "async process output");
        /* 输出超过系统管道容量时仍必须完成，验证轮询期间持续排空通道。 */
        completed = 0;
        transport.length = 0;
        transport.output[0] = '\0';
        XCS_TEST_CHECK(XConsoleShell_processLine(
                           shell,
                           "exec --async sh -c 'dd if=/dev/zero bs=8192 count=8 2>/dev/null'",
                           strlen("exec --async sh -c 'dd if=/dev/zero bs=8192 count=8 2>/dev/null'")) ==
                           XConsoleResult_MoreOutput,
                       "async large process start");
        for (attempts = 0; attempts < 200 && completed == 0; ++attempts)
            completed += XConsoleShell_pollProcesses(shell, 10);
        XCS_TEST_CHECK(completed == 1, "async large output does not deadlock");
#if XCONSOLE_SHELL_ASYNC_OUTPUT_ON
        /* 64KB 输出已超出 4KB 测试传输缓冲，写失败属预期；清空缓冲并排空残留队列，
           避免残留输出影响后续 redirect 等用例。 */
        transport.length = 0;
        transport.output[0] = '\0';
        (void)XConsoleShell_flushOutput(shell);
#endif
    }
#endif
#if XCONSOLE_SHELL_REDIRECT_ON && \
    !(defined(XFILE_USE_FATFS) && !defined(XFILE_USE_PLATFORM_API))
    {
        XString* redirectPath = XString_create_utf8("xconsole_shell_redirect.txt");
        XFd redirectFd;
        char redirectText[32] = {0};
        int redirectError = 0;
        XCS_TEST_CHECK(redirectPath != NULL, "redirect path");
        XFileSystem_removePermanent(redirectPath);
        {
            XConsoleResult redirectResult = XConsoleShell_processLine(
                shell,
                "exec sh -c 'printf redirected' --stdout xconsole_shell_redirect.txt",
                strlen("exec sh -c 'printf redirected' --stdout xconsole_shell_redirect.txt"));
            XCS_TEST_CHECK(redirectResult == XConsoleResult_Ok,
                           "redirect command");
        }
        redirectFd = XFileSystem_open(redirectPath, XFileSystem_ReadOnly, &redirectError);
        XCS_TEST_CHECK(redirectFd != XFD_INVALID, "redirect output file");
        XCS_TEST_CHECK(XFileSystem_read(redirectFd, redirectText,
                                        (int64_t)sizeof(redirectText) - 1) == 10 &&
                           strcmp(redirectText, "redirected") == 0,
                       "redirect output content");
        XFileSystem_close(redirectFd);
        XFileSystem_removePermanent(redirectPath);
        XString_delete_base(redirectPath);
    }
#endif
#endif
#if XCONSOLE_SHELL_FS_RM_ON && XCONSOLE_SHELL_FS_MKDIR_ON && \
    XCONSOLE_SHELL_FS_RMDIR_ON && XCONSOLE_SHELL_FS_CP_ON && \
    XCONSOLE_SHELL_FS_MV_ON && XCONSOLE_SHELL_FS_WRITE_ON && \
    XCONSOLE_SHELL_FS_LINK_ON
    XCS_TEST_CHECK(XConsoleShellTest_runFileCommands(shell, &transport),
                   "Linux fs commands");
#endif
#if XCONSOLE_SHELL_EDITOR_ON
    XCS_TEST_CHECK(XConsoleShellTest_runEditorCommands(shell, &transport),
                   "vi/vim editor commands");
#endif
    XFileSystem_removePermanent(filePath);
    XString_delete_base(filePath);
    XConsoleShell_delete_base(shell);
    for (size_t i = 0; i < 10000; ++i) {
        shell = XConsoleShell_create(&io);
        XCS_TEST_CHECK(shell != NULL, "lifecycle stress create");
        XConsoleShell_delete_base(shell);
    }
    XPrintf("[PASS] XConsoleShell 全量测试\n");
    return true;
}

#else

bool XConsoleShellTest_runAll(void)
{
    return true;
}

#endif /* XCONSOLE_SHELL_ON */
