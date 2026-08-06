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
} XConsoleShellTestTransport;

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
                                   XConsoleResult_Ok, "type=file")) goto cleanup;
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
        !XFileSystem_readLink(link, linkTarget)) goto cleanup;
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
    if (!XConsoleShellTest_runLine(shell, transport, "fs rmdir nested --recursive",
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
    if (root) XString_delete_base(root);
    if (temp) XString_delete_base(temp);
    return ok;
}

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
        XCS_TEST_CHECK(XConsoleShellTelnetAdapter_feedData(&adapter, telnetShell, NULL,
                                                           stream, sizeof(stream)) ==
                           XConsoleResult_Ok &&
                           telnetTransport.length >= 3 &&
                           (uint8_t)telnetTransport.output[0] == 255 &&
                           (uint8_t)telnetTransport.output[1] == 252 &&
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
    XCS_TEST_CHECK(XConsoleShell_processLine(shell, "unknown", 7) ==
                       XConsoleResult_UnknownCommand, "unknown command");
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
                                                 XConsoleResult_Ok, "lines="),
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
#if XCONSOLE_SHELL_LINE_EDITOR_ON
    XCS_TEST_CHECK(XConsoleShellTest_runLine(shell, &transport, "clear",
                                             XConsoleResult_Ok, "\x1b[2J\x1b[H"),
                   "clear command");
#endif
#if XCONSOLE_SHELL_COMPLETION_ON
    transport.length = 0;
    transport.output[0] = '\0';
    XCS_TEST_CHECK(XConsoleShell_feedData(shell, "ec\t completed\n", 14) == XConsoleResult_Ok &&
                       strstr(transport.output, "completed"), "tab completion");
#endif
    session = XConsoleShell_session(shell);
    XCS_TEST_CHECK(session && session->currentPath[0] != '\0', "session cwd");
#if XCONSOLE_SHELL_FS_PWD_ON
    XCS_TEST_CHECK(XConsoleShell_processLine(shell, "fs pwd", 6) == XConsoleResult_Ok,
                   "pwd command");
#endif
    filePath = XString_create_utf8("xconsole_shell_test.txt");
    XCS_TEST_CHECK(filePath != NULL, "file path");
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
        XFileSystem_remove(scriptPath);
        XString_delete_base(scriptPath);
    }
#endif
#if XCONSOLE_SHELL_FS_CAT_ON
    result = XConsoleShell_processLine(shell, "fs cat xconsole_shell_test.txt", 31) ==
             XConsoleResult_Ok;
    XCS_TEST_CHECK(result && strstr(transport.output, fileText), "cat command");
#endif
#if XCONSOLE_SHELL_FS_STAT_ON
    XCS_TEST_CHECK(XConsoleShell_processLine(shell, "fs stat xconsole_shell_test.txt", 31) ==
                       XConsoleResult_Ok && strstr(transport.output, "type=file"),
                   "stat command");
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
                                             XConsoleResult_Ok, "total="),
                   "root df command");
#endif
#if XCONSOLE_SHELL_FS_DU_ON
    XCS_TEST_CHECK(XConsoleShellTest_runLine(shell, &transport, "fs du .",
                                             XConsoleResult_Ok, "  "),
                   "du command");
#endif
#if XCONSOLE_SHELL_FS_WC_ON
    XCS_TEST_CHECK(XConsoleShellTest_runLine(shell, &transport, "wc xconsole_shell_test.txt",
                                             XConsoleResult_Ok, "bytes="),
                   "root wc command");
#endif
#if XCONSOLE_SHELL_FS_HEAD_ON
    XCS_TEST_CHECK(XConsoleShellTest_runLine(shell, &transport,
                                             "head -n 1 xconsole_shell_test.txt",
                                             XConsoleResult_Ok, fileText),
                   "head options");
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
                                             XConsoleResult_Ok, "regular-file"),
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
        for (attempts = 0; attempts < 100 && completed == 0; ++attempts)
            completed += XConsoleShell_pollProcesses(shell, 10);
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
        XFileSystem_remove(redirectPath);
        XCS_TEST_CHECK(XConsoleShell_processLine(
                           shell,
                           "exec sh -c 'printf redirected' --stdout xconsole_shell_redirect.txt",
                           strlen("exec sh -c 'printf redirected' --stdout xconsole_shell_redirect.txt")) ==
                           XConsoleResult_Ok,
                       "redirect command");
        redirectFd = XFileSystem_open(redirectPath, XFileSystem_ReadOnly, &redirectError);
        XCS_TEST_CHECK(redirectFd != XFD_INVALID, "redirect output file");
        XCS_TEST_CHECK(XFileSystem_read(redirectFd, redirectText,
                                        (int64_t)sizeof(redirectText) - 1) == 10 &&
                           strcmp(redirectText, "redirected") == 0,
                       "redirect output content");
        XFileSystem_close(redirectFd);
        XFileSystem_remove(redirectPath);
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
    XFileSystem_remove(filePath);
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
