/**
 * @file XConsoleShellTest.c
 * @brief XConsoleShell 全量核心回归测试。
 * @details
 * 模拟传输只使用固定数组，write 每次故意短写以验证 Shell 的背压循环。
 * 文件测试通过 XDeviceFile 公共 API 创建和清理测试文件，不使用平台文件接口。
 */

#include "XConsoleShellTest.h"
#include "CXinYueConfig.h"

#if XCONSOLE_SHELL_ON

#include "XConsoleShell.h"
#include "XDeviceFile.h"
#include "XString.h"
#include "XMemory.h"
#include "XPrintf.h"
#include "XDateTime.h"
#if XTUI_ON && XTUI_WIDGET_ON && XTUI_VIM_ON
#include "XTuiVim.h"
#include "XTuiScreen.h"
#include "XTuiTypes.h"
#endif
#if XCONSOLE_SHELL_ASYNC_ON
#include "XCoreApplication.h"
#include "XEventLoop.h"
#endif
#if XCONSOLE_SHELL_RESET_ON || XCONSOLE_SHELL_REBOOT_ON || XCONSOLE_SHELL_SHUTDOWN_ON
#include "XSystem.h"
#endif
#if XCONSOLE_SHELL_NETWORK_ON
#include "XDeviceNetwork.h"
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

static XFd xcs_test_open_file(const XString* path, int mode, int* error)
{
    XDeviceOpenOptions options;
    memset(&options, 0, sizeof(options));
    options.m_openMode = mode;
    options.m_target = path;
    return XDevice_open(XDeviceType_File, &options, error);
}

typedef struct XConsoleShellTestTransport {
    char output[4096];
    size_t length;
    bool inputEchoEnabled;
    size_t inputEchoChanges;
    int terminalColumns;
    int terminalRows;
    size_t terminalSizeQueries;
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

static bool XConsoleShellTest_inputEcho(void* userData, bool enabled)
{
    XConsoleShellTestTransport* transport =
        (XConsoleShellTestTransport*)userData;
    if (!transport) return false;
    transport->inputEchoEnabled = enabled;
    ++transport->inputEchoChanges;
    return true;
}

static bool XConsoleShellTest_terminalSize(void* userData, int* columns,
                                            int* rows)
{
    XConsoleShellTestTransport* transport =
        (XConsoleShellTestTransport*)userData;
    if (!transport || !columns || !rows || transport->terminalColumns <= 0 ||
        transport->terminalRows <= 0)
        return false;
    ++transport->terminalSizeQueries;
    *columns = transport->terminalColumns;
    *rows = transport->terminalRows;
    return true;
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

#if XTUI_ON && XTUI_WIDGET_ON && XTUI_VIM_ON
static bool XConsoleShellTest_vimKey(XTuiVim* vim, char key)
{
    XTuiKeyEvent event;
    XTuiKeyEvent_init(&event, XEVENT_TYPE_KEY_PRESS, XTuiKey_Char,
                      XKeyboardModifier_NoModifier);
    event.m_utf8[0] = key;
    event.m_utf8[1] = '\0';
    return XTuiWidget_keyPress_base((XTuiWidget*)vim, &event);
}

static bool XConsoleShellTest_vimEnter(XTuiVim* vim)
{
    XTuiKeyEvent event;
    XTuiKeyEvent_init(&event, XEVENT_TYPE_KEY_PRESS, XTuiKey_Enter,
                      XKeyboardModifier_NoModifier);
    return XTuiWidget_keyPress_base((XTuiWidget*)vim, &event);
}

static bool XConsoleShellTest_vimEscape(XTuiVim* vim)
{
    XTuiKeyEvent event;
    XTuiKeyEvent_init(&event, XEVENT_TYPE_KEY_PRESS, XTuiKey_Escape,
                      XKeyboardModifier_NoModifier);
    return XTuiWidget_keyPress_base((XTuiWidget*)vim, &event);
}

static bool XConsoleShellTest_vimSpecial(XTuiVim* vim, XTuiKeyType key)
{
    XTuiKeyEvent event;
    XTuiKeyEvent_init(&event, XEVENT_TYPE_KEY_PRESS, key,
                      XKeyboardModifier_NoModifier);
    return XTuiWidget_keyPress_base((XTuiWidget*)vim, &event);
}

static bool XConsoleShellTest_vimControl(XTuiVim* vim, char key)
{
    XTuiKeyEvent event;
    XTuiKeyEvent_init(&event, XEVENT_TYPE_KEY_PRESS, XTuiKey_Char,
                      XKeyboardModifier_ControlModifier);
    event.m_utf8[0] = key;
    event.m_utf8[1] = '\0';
    return XTuiWidget_keyPress_base((XTuiWidget*)vim, &event);
}

static bool XConsoleShellTest_runVimAdvanced(void)
{
    XTuiVim* vim;
    const char* initial[] = { "one two", "three two", "last" };
    bool ok = false;
    vim = XTuiVim_create();
    if (!vim) return false;
#if XTUI_VIM_HISTORY_ON
    XCS_TEST_CHECK(vim->m_commandHistoryState == NULL,
                   "Vim 初始状态不分配命令历史");
#if XTUI_VIM_SEARCH_ON
    XCS_TEST_CHECK(vim->m_searchHistoryState == NULL,
                   "Vim 初始状态不分配搜索历史");
#endif
#endif
#if XTUI_VIM_REGISTER_ON
    XCS_TEST_CHECK(vim->m_registerState == NULL,
                   "Vim 初始状态不分配寄存器表");
#endif
#if XTUI_VIM_MACRO_ON
    XCS_TEST_CHECK(vim->m_macroState == NULL,
                   "Vim 初始状态不分配宏表");
#endif
#if XTUI_VIM_MARK_ON
    XCS_TEST_CHECK(vim->m_markState == NULL,
                   "Vim 初始状态不分配标记表");
#endif
#if XTUI_VIM_JUMPLIST_ON
    XCS_TEST_CHECK(vim->m_jumpState == NULL,
                   "Vim 初始状态不分配跳转表");
#endif
    {
        XTuiScreen* screen = XTuiScreen_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, 40, 3);
        XRect rect = { 0, 0, 40, 3 };
        bool foundChinese = false;
        bool replacementCharacter = false;
        int x;
        XTuiWidget_setRect((XTuiWidget*)vim, &rect);
        if (!screen || !XTuiWidget_render_base((XTuiWidget*)vim, screen)) {
            if (screen) XTuiScreen_delete_base(screen);
            XTuiVim_delete_base(vim);
            return false;
        }
        for (x = 0; x < 40; ++x) {
            const XTuiCell* cell = XTuiScreen_cell(screen, x, 2);
            if (!cell) continue;
            if (strcmp(cell->m_utf8, "\xe5\x91\xbd") == 0) foundChinese = true;
            if (strcmp(cell->m_utf8, "\xef\xbf\xbd") == 0) replacementCharacter = true;
        }
        XCS_TEST_CHECK(foundChinese && !replacementCharacter,
                       "vim status renders complete utf8 cells");
        {
            const char* shortLines[] = { "one", "two", "three" };
            int statusColumn = -1;
            const XTuiCell* cell;
            XTuiVim_setLines(vim, shortLines, 3);
            rect.height = 5;
            XTuiWidget_setRect((XTuiWidget*)vim, &rect);
            if (!XTuiScreen_resize(screen, 40, 5)) {
                XTuiScreen_delete_base(screen);
                XTuiVim_delete_base(vim);
                return false;
            }
            XTuiScreen_clear(screen);
            if (!XTuiWidget_render_base((XTuiWidget*)vim, screen)) {
                XTuiScreen_delete_base(screen);
                XTuiVim_delete_base(vim);
                return false;
            }
            cell = XTuiScreen_cell(screen, 0, 3);
            XCS_TEST_CHECK(cell && strcmp(cell->m_utf8, " ") == 0,
                           "vim does not number rows after end of file");
            for (x = 0; x < 40; ++x) {
                cell = XTuiScreen_cell(screen, x, 4);
                if (cell && strcmp(cell->m_utf8, "\xe8\xa1\x8c") == 0) {
                    statusColumn = x;
                    break;
                }
            }
            cell = statusColumn >= 0 ? XTuiScreen_cell(screen, statusColumn + 2, 4) : NULL;
            XCS_TEST_CHECK(cell && strcmp(cell->m_utf8, "1") == 0,
                           "vim status shows current line number");
        }
        XTuiScreen_delete_base(screen);
    }
#if XTUI_VIM_ADVANCED_MOTION_ON && XTUI_VIM_REPLACE_ON
    XTuiVim_setLines(vim, initial, 3);
    XCS_TEST_CHECK(XConsoleShellTest_vimKey(vim, 'w') && vim->m_cursorColumn == 4,
                   "vim word forward");
    XCS_TEST_CHECK(XConsoleShellTest_vimKey(vim, 'e') && vim->m_cursorColumn == 6,
                   "vim word end");
    XCS_TEST_CHECK(XConsoleShellTest_vimKey(vim, 'b') && vim->m_cursorColumn == 4,
                   "vim word backward");
    XCS_TEST_CHECK(XConsoleShellTest_vimKey(vim, 'f') &&
                       XConsoleShellTest_vimKey(vim, 'o') && vim->m_cursorColumn == 6,
                   "vim find character");
    XTuiVim_setLines(vim, initial, 3);
    XConsoleShellTest_vimKey(vim, 'c'); XConsoleShellTest_vimKey(vim, 'i');
    XConsoleShellTest_vimKey(vim, 'w');
    XConsoleShellTest_vimKey(vim, 'O'); XConsoleShellTest_vimKey(vim, 'N');
    XConsoleShellTest_vimKey(vim, 'E'); XConsoleShellTest_vimEscape(vim);
    XCS_TEST_CHECK(strcmp(XTuiVim_line(vim, 0), "ONE two") == 0, "vim ciw");
    {
        const char* motion[] = { "abc abc", "tail" };
        XTuiVim_setLines(vim, motion, 2);
        XConsoleShellTest_vimKey(vim, 'W');
        XCS_TEST_CHECK(vim->m_cursorColumn == 4, "vim W word forward");
        XConsoleShellTest_vimKey(vim, 'E');
        XCS_TEST_CHECK(vim->m_cursorColumn == 6, "vim E word end");
        XConsoleShellTest_vimKey(vim, 'B');
        XCS_TEST_CHECK(vim->m_cursorColumn == 4, "vim B word backward");
        XTuiVim_setLines(vim, motion, 2);
        XConsoleShellTest_vimKey(vim, 'f'); XConsoleShellTest_vimKey(vim, 'c');
        XCS_TEST_CHECK(vim->m_cursorColumn == 2, "vim f forward");
        XConsoleShellTest_vimKey(vim, ';');
        XCS_TEST_CHECK(vim->m_cursorColumn == 6, "vim find repeat forward");
        XConsoleShellTest_vimKey(vim, ',');
        XCS_TEST_CHECK(vim->m_cursorColumn == 2, "vim find repeat reverse");
        XConsoleShellTest_vimKey(vim, 'F'); XConsoleShellTest_vimKey(vim, 'a');
        XCS_TEST_CHECK(vim->m_cursorColumn == 0, "vim F backward");
        XTuiVim_setLines(vim, motion, 2);
        XConsoleShellTest_vimKey(vim, 't'); XConsoleShellTest_vimKey(vim, 'c');
        XCS_TEST_CHECK(vim->m_cursorColumn == 1, "vim t till forward");
        XConsoleShellTest_vimKey(vim, 'T'); XConsoleShellTest_vimKey(vim, 'a');
        XCS_TEST_CHECK(vim->m_cursorColumn == 1, "vim T till backward");
        XTuiVim_setLines(vim, motion, 2);
        XConsoleShellTest_vimKey(vim, '2'); XConsoleShellTest_vimKey(vim, 'l');
        XCS_TEST_CHECK(vim->m_cursorColumn == 2, "vim counted l motion");
        XConsoleShellTest_vimKey(vim, '2'); XConsoleShellTest_vimKey(vim, 'h');
        XCS_TEST_CHECK(vim->m_cursorColumn == 0, "vim counted h motion");
    }
#endif
    {
        const char* lines[] = { "base" };
        XTuiVim_setLines(vim, lines, 1);
        XConsoleShellTest_vimKey(vim, 'o');
        XConsoleShellTest_vimKey(vim, 'n'); XConsoleShellTest_vimKey(vim, 'e');
        XConsoleShellTest_vimEscape(vim);
        XCS_TEST_CHECK(XTuiVim_lineCount(vim) == 2 &&
                           strcmp(XTuiVim_line(vim, 1), "ne") == 0,
                       "vim o new line");
        XConsoleShellTest_vimKey(vim, 'O');
        XConsoleShellTest_vimKey(vim, 't'); XConsoleShellTest_vimKey(vim, 'o');
        XConsoleShellTest_vimEscape(vim);
        XCS_TEST_CHECK(XTuiVim_lineCount(vim) == 3 &&
                           strcmp(XTuiVim_line(vim, 1), "to") == 0,
                       "vim O new line above");
#if XTUI_VIM_ADVANCED_MOTION_ON
        XTuiVim_setLines(vim, (const char*[]){ "abcdef" }, 1);
        XConsoleShellTest_vimKey(vim, '2'); XConsoleShellTest_vimKey(vim, 'x');
        XCS_TEST_CHECK(strcmp(XTuiVim_line(vim, 0), "cdef") == 0,
                       "vim counted x delete");
#endif
    }
    {
        const char* text[] = { "  abc" };
        XTuiVim_setLines(vim, text, 1);
        XConsoleShellTest_vimKey(vim, 'I'); XConsoleShellTest_vimKey(vim, 'X');
        XConsoleShellTest_vimEscape(vim);
        XCS_TEST_CHECK(strcmp(XTuiVim_line(vim, 0), "  Xabc") == 0, "vim I");
        XTuiVim_setLines(vim, text, 1);
        XConsoleShellTest_vimKey(vim, 'A'); XConsoleShellTest_vimKey(vim, '!');
        XConsoleShellTest_vimEscape(vim);
        XCS_TEST_CHECK(strcmp(XTuiVim_line(vim, 0), "  abc!") == 0, "vim A");
    }
#if XTUI_VIM_REPLACE_ON
    {
        const char* text[] = { "abc" };
        XTuiVim_setLines(vim, text, 1);
        XConsoleShellTest_vimKey(vim, 'r'); XConsoleShellTest_vimKey(vim, 'X');
        XCS_TEST_CHECK(strcmp(XTuiVim_line(vim, 0), "Xbc") == 0, "vim r");
        XTuiVim_setLines(vim, text, 1);
        XConsoleShellTest_vimKey(vim, 's'); XConsoleShellTest_vimKey(vim, 'Z');
        XConsoleShellTest_vimEscape(vim);
        XCS_TEST_CHECK(strcmp(XTuiVim_line(vim, 0), "Zbc") == 0, "vim s");
        XTuiVim_setLines(vim, text, 1);
        XConsoleShellTest_vimKey(vim, 'S'); XConsoleShellTest_vimKey(vim, 'l');
        XConsoleShellTest_vimKey(vim, 'i'); XConsoleShellTest_vimKey(vim, 'n');
        XConsoleShellTest_vimKey(vim, 'e'); XConsoleShellTest_vimEscape(vim);
        XCS_TEST_CHECK(strcmp(XTuiVim_line(vim, 0), "line") == 0, "vim S");
    }
#endif
#if XTUI_VIM_SEARCH_ON && XTUI_VIM_EX_ON
    {
        const char* text[] = { "  word word" };
        XTuiVim_setLines(vim, text, 1);
        XConsoleShellTest_vimKey(vim, '^');
        XCS_TEST_CHECK(vim->m_cursorColumn == 2, "vim first nonblank");
        XConsoleShellTest_vimKey(vim, 'A');
        XConsoleShellTest_vimControl(vim, 23);
        XCS_TEST_CHECK(strcmp(XTuiVim_line(vim, 0), "  word ") == 0, "vim insert Ctrl-W");
        XConsoleShellTest_vimControl(vim, 21);
        XConsoleShellTest_vimEscape(vim);
        XCS_TEST_CHECK(strcmp(XTuiVim_line(vim, 0), "") == 0, "vim insert Ctrl-U");
    }
    {
        const char* lines[] = { "0", "1", "2", "3", "4", "5" };
        XRect rect = { 0, 0, 20, 3 };
        XTuiVim_setLines(vim, lines, 6);
        XTuiWidget_setRect((XTuiWidget*)vim, &rect);
        XConsoleShellTest_vimSpecial(vim, XTuiKey_PageDown);
        XCS_TEST_CHECK(vim->m_cursorLine == 2 && vim->m_topLine == 1,
                       "vim page down viewport");
        XConsoleShellTest_vimSpecial(vim, XTuiKey_PageUp);
        XCS_TEST_CHECK(vim->m_cursorLine == 0 && vim->m_topLine == 0,
                       "vim page up viewport");
    }
    {
        const char* text[] = { "word word" };
        const char* p;
        XTuiVim_setLines(vim, text, 1);
        XConsoleShellTest_vimKey(vim, '/'); XConsoleShellTest_vimKey(vim, 'w');
        XConsoleShellTest_vimKey(vim, 'o'); XConsoleShellTest_vimKey(vim, 'r');
        XConsoleShellTest_vimKey(vim, 'd'); XConsoleShellTest_vimEnter(vim);
        XConsoleShellTest_vimKey(vim, ':');
        {
            const char* command = "nohlsearch";
            const char* p = command;
            while (*p) XConsoleShellTest_vimKey(vim, *p++);
        }
        XConsoleShellTest_vimEnter(vim);
        XCS_TEST_CHECK(!vim->m_searchHighlight &&
                           strcmp(vim->m_lastSearch, "word") == 0,
                       "vim nohlsearch keeps pattern");
        XConsoleShellTest_vimKey(vim, 'n');
        XCS_TEST_CHECK(vim->m_cursorColumn == 0, "vim n wraps after nohlsearch");
#if XTUI_VIM_HISTORY_ON
        XConsoleShellTest_vimKey(vim, ':');
        for (p = "set nu"; *p; ++p) XConsoleShellTest_vimKey(vim, *p);
        XConsoleShellTest_vimEnter(vim);
        XConsoleShellTest_vimKey(vim, ':');
        XConsoleShellTest_vimSpecial(vim, XTuiKey_ArrowUp);
        XCS_TEST_CHECK(strcmp(vim->m_command, "set nu") == 0,
                       "vim command history up");
        XConsoleShellTest_vimSpecial(vim, XTuiKey_ArrowDown);
        XCS_TEST_CHECK(vim->m_commandLen == 0 && vim->m_command[0] == '\0',
                       "vim command history down");
        XConsoleShellTest_vimEscape(vim);
        XConsoleShellTest_vimKey(vim, '/');
        XConsoleShellTest_vimSpecial(vim, XTuiKey_ArrowUp);
        XCS_TEST_CHECK(strcmp(vim->m_search, "word") == 0,
                       "vim search history up");
        XConsoleShellTest_vimEscape(vim);
#endif
#if XTUI_VIM_SEARCH_ON
        XTuiVim_setLines(vim, (const char*[]){ "one", "two" }, 2);
        XConsoleShellTest_vimKey(vim, '?');
        for (p = "two"; *p; ++p) XConsoleShellTest_vimKey(vim, *p);
        XConsoleShellTest_vimEnter(vim);
        XCS_TEST_CHECK(strcmp(vim->m_lastSearch, "two") == 0 &&
                           !vim->m_searchMode,
                       "vim reverse search");
#endif
    }
#endif
#if XRegularExpression_ON && XTUI_VIM_SEARCH_ON && \
    XTUI_VIM_SUBSTITUTE_ON
#if XTUI_VIM_MULTIBUFFER_ON
    {
        const char* lines[] = { "id-12", "id-34", "\xe4\xb8\xad id-56" };
        const char* command = "%s/(id)-([0-9]+)/\\2:\\1/g";
        const char* p;
        XTuiVim_setLines(vim, lines, 3);
        XConsoleShellTest_vimKey(vim, '/');
        for (p = "id-[0-9]+"; *p; ++p) XConsoleShellTest_vimKey(vim, *p);
        XConsoleShellTest_vimEnter(vim);
        XCS_TEST_CHECK(vim->m_cursorLine == 1 && vim->m_cursorColumn == 0,
                       "vim regular expression search");
        XConsoleShellTest_vimKey(vim, 'n');
        XCS_TEST_CHECK(vim->m_cursorLine == 2 && vim->m_cursorColumn == 2,
                       "vim regular expression search utf8 column");
        XConsoleShellTest_vimKey(vim, 'N');
        XCS_TEST_CHECK(vim->m_cursorLine == 1 && vim->m_cursorColumn == 0,
                       "vim regular expression reverse repeat");
        XConsoleShellTest_vimKey(vim, ':');
        for (p = command; *p; ++p) XConsoleShellTest_vimKey(vim, *p);
        XConsoleShellTest_vimEnter(vim);
        XCS_TEST_CHECK(strcmp(XTuiVim_line(vim, 0), "12:id") == 0 &&
                           strcmp(XTuiVim_line(vim, 1), "34:id") == 0 &&
                           strcmp(XTuiVim_line(vim, 2), "\xe4\xb8\xad 56:id") == 0,
                       "vim regular expression substitution captures");
        XTuiVim_setLines(vim, lines, 3);
        XConsoleShellTest_vimKey(vim, '/');
        XConsoleShellTest_vimKey(vim, '['); XConsoleShellTest_vimEnter(vim);
        XCS_TEST_CHECK(strstr(vim->m_status, "\xe6\x97\xa0\xe6\x95\x88\xe6\xad\xa3\xe5\x88\x99") != NULL,
                       "vim regular expression syntax error");
    }
#endif
    {
        const char* line[] = { "buffer" };
        const char* p;
        XTuiVim_setLines(vim, line, 1);
        XConsoleShellTest_vimKey(vim, ':');
        for (p = "e next.txt"; *p; ++p) XConsoleShellTest_vimKey(vim, *p);
        XConsoleShellTest_vimEnter(vim);
        XCS_TEST_CHECK(XTuiVim_wantEdit(vim) &&
                           strcmp(XTuiVim_actionPath(vim), "next.txt") == 0 &&
                           !XTuiVim_wantForce(vim),
                       "vim edit buffer action");
        XTuiVim_ackAction(vim);
        XConsoleShellTest_vimKey(vim, 'i'); XConsoleShellTest_vimKey(vim, 'X');
        XConsoleShellTest_vimEscape(vim);
        XConsoleShellTest_vimKey(vim, ':');
        for (p = "e blocked.txt"; *p; ++p) XConsoleShellTest_vimKey(vim, *p);
        XConsoleShellTest_vimEnter(vim);
        XCS_TEST_CHECK(!XTuiVim_wantEdit(vim) &&
                           strstr(vim->m_status, "\xe6\x9c\xaa\xe4\xbf\x9d\xe5\xad\x98") != NULL,
                       "vim edit protects modified buffer");
        XConsoleShellTest_vimKey(vim, ':');
        for (p = "bn"; *p; ++p) XConsoleShellTest_vimKey(vim, *p);
        XConsoleShellTest_vimEnter(vim);
        XCS_TEST_CHECK(XTuiVim_wantBufferNext(vim), "vim buffer next action");
        XTuiVim_ackAction(vim);
        XConsoleShellTest_vimKey(vim, ':');
        for (p = "b 3"; *p; ++p) XConsoleShellTest_vimKey(vim, *p);
        XConsoleShellTest_vimEnter(vim);
        XCS_TEST_CHECK(XTuiVim_wantBufferIndex(vim) == 3, "vim buffer index action");
        XTuiVim_ackAction(vim);
        XConsoleShellTest_vimKey(vim, ':');
        for (p = "buffers"; *p; ++p) XConsoleShellTest_vimKey(vim, *p);
        XConsoleShellTest_vimEnter(vim);
        XCS_TEST_CHECK(XTuiVim_wantBufferList(vim), "vim buffer list action");
        XTuiVim_ackAction(vim);
#if XTUI_VIM_EX_ON
        XConsoleShellTest_vimKey(vim, ':');
        for (p = "w copy.txt"; *p; ++p) XConsoleShellTest_vimKey(vim, *p);
        XConsoleShellTest_vimEnter(vim);
        XCS_TEST_CHECK(XTuiVim_wantWritePath(vim) &&
                           !XTuiVim_wantSaveAs(vim) &&
                           strcmp(XTuiVim_actionPath(vim), "copy.txt") == 0,
                       "vim write path action");
        XTuiVim_ackAction(vim);
        XConsoleShellTest_vimKey(vim, ':');
        for (p = "saveas renamed.txt"; *p; ++p) XConsoleShellTest_vimKey(vim, *p);
        XConsoleShellTest_vimEnter(vim);
        XCS_TEST_CHECK(XTuiVim_wantSaveAs(vim) &&
                           strcmp(XTuiVim_actionPath(vim), "renamed.txt") == 0,
                       "vim saveas action");
        XTuiVim_ackAction(vim);
#endif
        XConsoleShellTest_vimKey(vim, ':');
        for (p = "wa"; *p; ++p) XConsoleShellTest_vimKey(vim, *p);
        XConsoleShellTest_vimEnter(vim);
        XCS_TEST_CHECK(XTuiVim_wantWriteAll(vim) && !XTuiVim_wantQuitAll(vim),
                       "vim write all action");
        XTuiVim_ackAction(vim);
        XConsoleShellTest_vimKey(vim, ':');
        for (p = "wqa"; *p; ++p) XConsoleShellTest_vimKey(vim, *p);
        XConsoleShellTest_vimEnter(vim);
        XCS_TEST_CHECK(XTuiVim_wantWriteAll(vim) && XTuiVim_wantQuitAll(vim),
                       "vim write quit all action");
        XTuiVim_ackAction(vim);
        XConsoleShellTest_vimKey(vim, ':');
        for (p = "bd!"; *p; ++p) XConsoleShellTest_vimKey(vim, *p);
        XConsoleShellTest_vimEnter(vim);
        XCS_TEST_CHECK(XTuiVim_wantBufferClose(vim) && XTuiVim_wantForce(vim),
                       "vim force buffer close action");
        XTuiVim_ackAction(vim);
    }
#endif
#if XTUI_VIM_SUBSTITUTE_ON
    {
        const char* lines[] = { "cat CAT cat", "cat", "CAT" };
        const char* p;
        XTuiVim_setLines(vim, lines, 3);
        XConsoleShellTest_vimKey(vim, ':');
        for (p = "2,3s#cat#dog#gi"; *p; ++p) XConsoleShellTest_vimKey(vim, *p);
        XConsoleShellTest_vimEnter(vim);
        XCS_TEST_CHECK(strcmp(XTuiVim_line(vim, 0), "cat CAT cat") == 0 &&
                           strcmp(XTuiVim_line(vim, 1), "dog") == 0 &&
                           strcmp(XTuiVim_line(vim, 2), "dog") == 0,
                       "vim substitute range delimiter flags");
        XConsoleShellTest_vimKey(vim, 'j');
        XConsoleShellTest_vimKey(vim, ':');
        for (p = ".s/dog/bird/I"; *p; ++p) XConsoleShellTest_vimKey(vim, *p);
        XConsoleShellTest_vimEnter(vim);
        XCS_TEST_CHECK(strcmp(XTuiVim_line(vim, 1), "bird") == 0,
                       "vim substitute current line range");
#if XTUI_VIM_SUBSTITUTE_CONFIRM_ON
        {
            const char* confirmLine[] = { "a a" };
            XTuiVim_setLines(vim, confirmLine, 1);
            XConsoleShellTest_vimKey(vim, ':');
            for (p = "s/a/X/gc"; *p; ++p) XConsoleShellTest_vimKey(vim, *p);
            XConsoleShellTest_vimEnter(vim);
            XCS_TEST_CHECK(vim->m_substituteConfirm, "vim substitute confirmation starts");
            XConsoleShellTest_vimKey(vim, 'y');
            XCS_TEST_CHECK(vim->m_substituteConfirm &&
                               strcmp(XTuiVim_line(vim, 0), "X a") == 0,
                           "vim substitute confirmation accepts");
            XConsoleShellTest_vimKey(vim, 'n');
            XCS_TEST_CHECK(!vim->m_substituteConfirm &&
                               strcmp(XTuiVim_line(vim, 0), "X a") == 0,
                           "vim substitute confirmation skips");
        }
#endif
    }
#endif
#if XTUI_VIM_EX_ON
    {
        const char* exLines[] = { "one", "two", "three" };
        const char* p;
        XTuiVim_setLines(vim, exLines, 3);
#if XTUI_VIM_YANK_PASTE_ON
        XConsoleShellTest_vimKey(vim, ':');
        for (p = "yank"; *p; ++p) XConsoleShellTest_vimKey(vim, *p);
        XConsoleShellTest_vimEnter(vim);
        XCS_TEST_CHECK(vim->m_regCount == 1 && vim->m_regLineWise,
                       "vim Ex yank");
        XConsoleShellTest_vimKey(vim, ':');
        for (p = "put"; *p; ++p) XConsoleShellTest_vimKey(vim, *p);
        XConsoleShellTest_vimEnter(vim);
        XCS_TEST_CHECK(vim->m_lineCount == 4 &&
                           strcmp(XTuiVim_line(vim, 1), "one") == 0,
                       "vim Ex put");
        XConsoleShellTest_vimKey(vim, ':');
        for (p = "delete"; *p; ++p) XConsoleShellTest_vimKey(vim, *p);
        XConsoleShellTest_vimEnter(vim);
        XCS_TEST_CHECK(vim->m_lineCount == 3 &&
                           strcmp(XTuiVim_line(vim, 1), "two") == 0,
                       "vim Ex delete");
#endif
        XConsoleShellTest_vimKey(vim, ':');
        for (p = "set nonu"; *p; ++p) XConsoleShellTest_vimKey(vim, *p);
        XConsoleShellTest_vimEnter(vim);
        XCS_TEST_CHECK(!vim->m_showLineNumbers, "vim set nonumber");
        XConsoleShellTest_vimKey(vim, ':');
        for (p = "set number"; *p; ++p) XConsoleShellTest_vimKey(vim, *p);
        XConsoleShellTest_vimEnter(vim);
        XCS_TEST_CHECK(vim->m_showLineNumbers, "vim set number");
    }
#endif
#if XTUI_VIM_VISUAL_ON && XTUI_VIM_YANK_PASTE_ON
    {
        const char* lines[] = { "abc", "def", "ghi" };
        XTuiVim_setLines(vim, lines, 3);
        XConsoleShellTest_vimControl(vim, 22);
        XConsoleShellTest_vimKey(vim, 'l'); XConsoleShellTest_vimKey(vim, 'j');
        XConsoleShellTest_vimKey(vim, 'y');
        XCS_TEST_CHECK(vim->m_regCount == 2 && strcmp(vim->m_regLines[0], "ab") == 0 &&
                           strcmp(vim->m_regLines[1], "de") == 0,
                       "vim visual block yank");
        XConsoleShellTest_vimKey(vim, 'p');
        XCS_TEST_CHECK(strcmp(XTuiVim_line(vim, 1), "deabf") == 0 &&
                           strcmp(XTuiVim_line(vim, 2), "ghdei") == 0,
                       "vim visual block paste");
        XTuiVim_setLines(vim, lines, 3);
        XConsoleShellTest_vimControl(vim, 22);
        XConsoleShellTest_vimKey(vim, 'l'); XConsoleShellTest_vimKey(vim, 'j');
        XConsoleShellTest_vimKey(vim, 'd');
        XCS_TEST_CHECK(strcmp(XTuiVim_line(vim, 0), "c") == 0 &&
                           strcmp(XTuiVim_line(vim, 1), "f") == 0,
                       "vim visual block delete");
#if XTUI_VIM_REPLACE_ON
        {
            const char* blockLines[] = { "ab", "cd" };
            XTuiVim_setLines(vim, blockLines, 2);
            XConsoleShellTest_vimControl(vim, 22); XConsoleShellTest_vimKey(vim, 'j');
            XConsoleShellTest_vimKey(vim, 'I'); XConsoleShellTest_vimKey(vim, 'X');
            XConsoleShellTest_vimEscape(vim);
            XCS_TEST_CHECK(strcmp(XTuiVim_line(vim, 0), "Xab") == 0 &&
                               strcmp(XTuiVim_line(vim, 1), "Xcd") == 0,
                           "vim visual block I");
            XTuiVim_setLines(vim, blockLines, 2);
            XConsoleShellTest_vimControl(vim, 22); XConsoleShellTest_vimKey(vim, 'j');
            XConsoleShellTest_vimKey(vim, 'A'); XConsoleShellTest_vimKey(vim, 'X');
            XConsoleShellTest_vimEscape(vim);
            XCS_TEST_CHECK(strcmp(XTuiVim_line(vim, 0), "aXb") == 0 &&
                               strcmp(XTuiVim_line(vim, 1), "cXd") == 0,
                           "vim visual block A");
            {
                const char* changeLines[] = { "abc", "def" };
                XTuiVim_setLines(vim, changeLines, 2);
                XConsoleShellTest_vimControl(vim, 22); XConsoleShellTest_vimKey(vim, 'l');
                XConsoleShellTest_vimKey(vim, 'j'); XConsoleShellTest_vimKey(vim, 'c');
                XConsoleShellTest_vimKey(vim, 'X'); XConsoleShellTest_vimEscape(vim);
                XCS_TEST_CHECK(strcmp(XTuiVim_line(vim, 0), "Xc") == 0 &&
                                   strcmp(XTuiVim_line(vim, 1), "Xf") == 0,
                               "vim visual block change");
            }
        }
#endif
    }
#endif
#if XTUI_VIM_VISUAL_ON && XTUI_VIM_REPLACE_ON
    {
        const char* visualLines[] = { "alpha", "beta", "gamma" };
        XTuiVim_setLines(vim, visualLines, 3);
        XConsoleShellTest_vimKey(vim, 'v'); XConsoleShellTest_vimKey(vim, 'l');
        XConsoleShellTest_vimKey(vim, 'c'); XConsoleShellTest_vimKey(vim, 'X');
        XConsoleShellTest_vimEscape(vim);
        XCS_TEST_CHECK(strcmp(XTuiVim_line(vim, 0), "Xpha") == 0,
                       "vim visual character change");
        XTuiVim_setLines(vim, visualLines, 3);
        XConsoleShellTest_vimKey(vim, 'V'); XConsoleShellTest_vimKey(vim, 'j');
        XConsoleShellTest_vimKey(vim, 'd');
        XCS_TEST_CHECK(XTuiVim_lineCount(vim) == 1 &&
                           strcmp(XTuiVim_line(vim, 0), "gamma") == 0,
                       "vim visual line delete");
    }
#endif
#if XTUI_VIM_SEARCH_ON && XTUI_VIM_ADVANCED_MOTION_ON
    {
        const char* lines[] = { "word x word", "(a[b])", "last" };
        XRect rect = { 0, 0, 20, 3 };
        XTuiVim_setLines(vim, lines, 3);
        XTuiWidget_setRect((XTuiWidget*)vim, &rect);
        XConsoleShellTest_vimKey(vim, '*');
        XCS_TEST_CHECK(vim->m_cursorColumn == 7, "vim star word search");
        XConsoleShellTest_vimKey(vim, '#');
        XCS_TEST_CHECK(vim->m_cursorColumn == 0, "vim hash word search");
        XConsoleShellTest_vimKey(vim, 'j'); XConsoleShellTest_vimKey(vim, '%');
        XCS_TEST_CHECK(vim->m_cursorLine == 1 && vim->m_cursorColumn == 5,
                       "vim percent bracket match");
        XConsoleShellTest_vimKey(vim, 'z'); XConsoleShellTest_vimKey(vim, 't');
        XCS_TEST_CHECK(vim->m_topLine == 1, "vim zt viewport");
        XConsoleShellTest_vimKey(vim, 'z'); XConsoleShellTest_vimKey(vim, 'b');
        XCS_TEST_CHECK(vim->m_topLine == 0, "vim zb viewport");
    }
#endif
#if XTUI_VIM_ADVANCED_MOTION_ON && XTUI_VIM_REPLACE_ON
    {
        const char* line[] = { "call(alpha)" };
        XTuiVim_setLines(vim, line, 1);
        XConsoleShellTest_vimKey(vim, 'w');
        XConsoleShellTest_vimKey(vim, 'c'); XConsoleShellTest_vimKey(vim, 'i');
        XConsoleShellTest_vimKey(vim, '('); XConsoleShellTest_vimKey(vim, 'X');
        XConsoleShellTest_vimEscape(vim);
        XCS_TEST_CHECK(strcmp(XTuiVim_line(vim, 0), "call(X)") == 0, "vim ci parenthesis");
    }
#endif
#if XTUI_VIM_ADVANCED_MOTION_ON && XTUI_VIM_YANK_PASTE_ON
    {
        const char* line[] = { "{ alpha }", "[beta]" };
        XTuiVim_setLines(vim, line, 2);
        XConsoleShellTest_vimKey(vim, 'd'); XConsoleShellTest_vimKey(vim, 'i');
        XConsoleShellTest_vimKey(vim, '{');
        XCS_TEST_CHECK(strcmp(XTuiVim_line(vim, 0), "{}") == 0, "vim di brace");
        XConsoleShellTest_vimKey(vim, 'j'); XConsoleShellTest_vimKey(vim, 'y');
        XConsoleShellTest_vimKey(vim, 'i'); XConsoleShellTest_vimKey(vim, '[');
        XCS_TEST_CHECK(vim->m_regCount == 1 && strcmp(vim->m_regLines[0], "beta") == 0,
                       "vim yi bracket");
    }
#endif
#if XTUI_VIM_ADVANCED_MOTION_ON
    {
        const char* lines[] = { "0", "1", "2", "3", "4", "5", "6", "7" };
        XRect rect = { 0, 0, 20, 5 };
        XTuiVim_setLines(vim, lines, 8);
        XTuiWidget_setRect((XTuiWidget*)vim, &rect);
        vim->m_topLine = 2; vim->m_cursorLine = 3;
        XConsoleShellTest_vimKey(vim, 'H');
        XCS_TEST_CHECK(vim->m_cursorLine == 2, "vim H");
        XConsoleShellTest_vimKey(vim, 'M');
        XCS_TEST_CHECK(vim->m_cursorLine == 4, "vim M");
        XConsoleShellTest_vimKey(vim, 'L');
        XCS_TEST_CHECK(vim->m_cursorLine == 5, "vim L");
        XConsoleShellTest_vimControl(vim, 5);
        XCS_TEST_CHECK(vim->m_topLine == 3, "vim Ctrl-E viewport");
        XConsoleShellTest_vimControl(vim, 25);
        XCS_TEST_CHECK(vim->m_topLine == 2, "vim Ctrl-Y viewport");
    }
    {
        const char* lines[] = { "left", "right" };
        XTuiVim_setLines(vim, lines, 2);
        XConsoleShellTest_vimKey(vim, 'J');
        XCS_TEST_CHECK(XTuiVim_lineCount(vim) == 1 &&
                       strcmp(XTuiVim_line(vim, 0), "left right") == 0,
                       "vim J inserts separator");
    }
    {
        const char* lines[] = { "abcd", "efgh" };
        XTuiVim_setLines(vim, lines, 2);
        XConsoleShellTest_vimKey(vim, 'x');
        XConsoleShellTest_vimKey(vim, 'j');
        XConsoleShellTest_vimKey(vim, '.');
        XCS_TEST_CHECK(strcmp(XTuiVim_line(vim, 0), "bcd") == 0 &&
                           strcmp(XTuiVim_line(vim, 1), "fgh") == 0,
                       "vim dot repeats delete");
        XTuiVim_setLines(vim, lines, 2);
        XConsoleShellTest_vimKey(vim, 'D');
        XConsoleShellTest_vimKey(vim, 'j');
        XConsoleShellTest_vimKey(vim, '.');
        XCS_TEST_CHECK(strcmp(XTuiVim_line(vim, 0), "") == 0 &&
                           strcmp(XTuiVim_line(vim, 1), "") == 0,
                       "vim dot repeats D");
    }
    {
        const char* lines[] = { "abcd", "tail" };
        XTuiVim_setLines(vim, lines, 2);
        XConsoleShellTest_vimKey(vim, 's'); XConsoleShellTest_vimKey(vim, 'X');
        XConsoleShellTest_vimEscape(vim); XConsoleShellTest_vimKey(vim, 'j');
        XConsoleShellTest_vimKey(vim, '.');
        XCS_TEST_CHECK(strcmp(XTuiVim_line(vim, 0), "Xbcd") == 0 &&
                           strcmp(XTuiVim_line(vim, 1), "tXil") == 0,
                       "vim dot repeats s");
        XTuiVim_setLines(vim, lines, 2);
        XConsoleShellTest_vimKey(vim, 'C'); XConsoleShellTest_vimKey(vim, 'Z');
        XConsoleShellTest_vimEscape(vim); XConsoleShellTest_vimKey(vim, 'j');
        XConsoleShellTest_vimKey(vim, '.');
        XCS_TEST_CHECK(strcmp(XTuiVim_line(vim, 0), "Z") == 0 &&
                           strcmp(XTuiVim_line(vim, 1), "Z") == 0,
                       "vim dot repeats C");
    }
#if XTUI_VIM_JUMPLIST_ON
    {
        const char* lines[] = { "first", "middle", "last" };
        XTuiVim_setLines(vim, lines, 3);
        XConsoleShellTest_vimKey(vim, 'G');
        XCS_TEST_CHECK(vim->m_cursorLine == 2, "vim jump list destination");
        XConsoleShellTest_vimControl(vim, 15);
        XCS_TEST_CHECK(vim->m_cursorLine == 0, "vim Ctrl-O older jump");
        XConsoleShellTest_vimControl(vim, 9);
        XCS_TEST_CHECK(vim->m_cursorLine == 2, "vim Ctrl-I newer jump");
    }
#endif
#endif
#if XTUI_VIM_ADVANCED_MOTION_ON && XTUI_VIM_YANK_PASTE_ON
    XTuiVim_setLines(vim, initial, 3);
    XConsoleShellTest_vimKey(vim, 'G');
    XCS_TEST_CHECK(vim->m_cursorLine == 2, "vim G");
    XConsoleShellTest_vimKey(vim, 'g'); XConsoleShellTest_vimKey(vim, 'g');
    XCS_TEST_CHECK(vim->m_cursorLine == 0, "vim gg");
    XConsoleShellTest_vimKey(vim, 'y'); XConsoleShellTest_vimKey(vim, 'y');
    XConsoleShellTest_vimKey(vim, 'p');
    XCS_TEST_CHECK(XTuiVim_lineCount(vim) == 4 &&
                       strcmp(XTuiVim_line(vim, 1), "one two") == 0,
                   "vim yank paste");
    XConsoleShellTest_vimKey(vim, 'd'); XConsoleShellTest_vimKey(vim, 'w');
    XCS_TEST_CHECK(strcmp(XTuiVim_line(vim, 1), "two") == 0, "vim dw");
    {
        const char* beforeLines[] = { "one", "two" };
        XTuiVim_setLines(vim, beforeLines, 2);
        XConsoleShellTest_vimKey(vim, 'y'); XConsoleShellTest_vimKey(vim, 'y');
        XConsoleShellTest_vimKey(vim, 'j'); XConsoleShellTest_vimKey(vim, 'P');
        XCS_TEST_CHECK(XTuiVim_lineCount(vim) == 3 &&
                           strcmp(XTuiVim_line(vim, 1), "one") == 0 &&
                           strcmp(XTuiVim_line(vim, 2), "two") == 0,
                       "vim paste before");
    }
    {
        const char* counted[] = { "one two three" };
        XTuiVim_setLines(vim, counted, 1);
        XConsoleShellTest_vimKey(vim, 'd'); XConsoleShellTest_vimKey(vim, '2');
        XConsoleShellTest_vimKey(vim, 'w');
        XCS_TEST_CHECK(strcmp(XTuiVim_line(vim, 0), "three") == 0,
                       "vim counted operator motion");
    }
#endif
#if XTUI_VIM_REGISTER_ON
    {
        const char* lines[] = { "one", "two" };
        XTuiVim_setLines(vim, lines, 2);
        XConsoleShellTest_vimKey(vim, '"'); XConsoleShellTest_vimKey(vim, 'a');
        XConsoleShellTest_vimKey(vim, 'y'); XConsoleShellTest_vimKey(vim, 'y');
        XConsoleShellTest_vimKey(vim, 'j');
        XConsoleShellTest_vimKey(vim, '"'); XConsoleShellTest_vimKey(vim, 'a');
        XConsoleShellTest_vimKey(vim, 'p');
        XCS_TEST_CHECK(XTuiVim_lineCount(vim) == 3 &&
                           strcmp(XTuiVim_line(vim, 2), "one") == 0,
                       "vim named register paste");
        {
            const char* appendLines[] = { "one", "two", "three" };
            XTuiVim_setLines(vim, appendLines, 3);
            XConsoleShellTest_vimKey(vim, '"'); XConsoleShellTest_vimKey(vim, 'a');
            XConsoleShellTest_vimKey(vim, 'y'); XConsoleShellTest_vimKey(vim, 'y');
            XConsoleShellTest_vimKey(vim, 'j');
            XConsoleShellTest_vimKey(vim, '"'); XConsoleShellTest_vimKey(vim, 'A');
            XConsoleShellTest_vimKey(vim, 'y'); XConsoleShellTest_vimKey(vim, 'y');
            XConsoleShellTest_vimKey(vim, 'k');
            XConsoleShellTest_vimKey(vim, '"'); XConsoleShellTest_vimKey(vim, 'a');
            XConsoleShellTest_vimKey(vim, 'p');
            XCS_TEST_CHECK(XTuiVim_lineCount(vim) == 5 &&
                               strcmp(XTuiVim_line(vim, 1), "one") == 0 &&
                               strcmp(XTuiVim_line(vim, 2), "two") == 0 &&
                               strcmp(XTuiVim_line(vim, 3), "two") == 0,
                           "vim uppercase named register append");
        }
        {
            const char* yankLines[] = { "alpha", "beta" };
            XTuiVim_setLines(vim, yankLines, 2);
            XConsoleShellTest_vimKey(vim, 'y'); XConsoleShellTest_vimKey(vim, 'y');
            XConsoleShellTest_vimKey(vim, '"'); XConsoleShellTest_vimKey(vim, '0');
            XConsoleShellTest_vimKey(vim, 'p');
            XCS_TEST_CHECK(XTuiVim_lineCount(vim) == 3 &&
                               strcmp(XTuiVim_line(vim, 1), "alpha") == 0,
                           "vim numbered yank register zero");
        }
        XTuiVim_setLines(vim, lines, 2);
        XConsoleShellTest_vimKey(vim, 'd'); XConsoleShellTest_vimKey(vim, 'd');
        XConsoleShellTest_vimKey(vim, '"'); XConsoleShellTest_vimKey(vim, '1');
        XConsoleShellTest_vimKey(vim, 'p');
        XCS_TEST_CHECK(strcmp(XTuiVim_line(vim, 1), "one") == 0,
                       "vim numbered delete register");
        XConsoleShellTest_vimKey(vim, '"'); XConsoleShellTest_vimKey(vim, '_');
        XConsoleShellTest_vimKey(vim, 'y'); XConsoleShellTest_vimKey(vim, 'y');
        XConsoleShellTest_vimKey(vim, '"'); XConsoleShellTest_vimKey(vim, '1');
        XConsoleShellTest_vimKey(vim, 'p');
        XCS_TEST_CHECK(strcmp(XTuiVim_line(vim, 2), "one") == 0,
                       "vim blackhole register preserves numbered register");
    }
#endif
#if XTUI_VIM_MARK_ON && XTUI_VIM_ADVANCED_MOTION_ON
    {
        const char* lines[] = { "first", "  second" };
        XTuiVim_setLines(vim, lines, 2);
        XConsoleShellTest_vimKey(vim, 'j');
        XConsoleShellTest_vimKey(vim, 'm'); XConsoleShellTest_vimKey(vim, 'a');
        XConsoleShellTest_vimKey(vim, 'g'); XConsoleShellTest_vimKey(vim, 'g');
        XConsoleShellTest_vimKey(vim, '\''); XConsoleShellTest_vimKey(vim, 'a');
        XCS_TEST_CHECK(vim->m_cursorLine == 1 && vim->m_cursorColumn == 2,
                       "vim line mark jump");
        XConsoleShellTest_vimKey(vim, 'g'); XConsoleShellTest_vimKey(vim, 'g');
        XConsoleShellTest_vimKey(vim, '`'); XConsoleShellTest_vimKey(vim, 'a');
        XCS_TEST_CHECK(vim->m_cursorLine == 1 && vim->m_cursorColumn == 0,
                       "vim exact mark jump");
    }
#endif
#if XTUI_VIM_MACRO_ON
    {
        const char* lines[] = { "abc" };
        XTuiVim_setLines(vim, lines, 1);
        XConsoleShellTest_vimKey(vim, 'q'); XConsoleShellTest_vimKey(vim, 'a');
        XConsoleShellTest_vimKey(vim, 'i'); XConsoleShellTest_vimKey(vim, 'X');
        XConsoleShellTest_vimEscape(vim); XConsoleShellTest_vimKey(vim, 'q');
        XConsoleShellTest_vimKey(vim, '@'); XConsoleShellTest_vimKey(vim, 'a');
        XCS_TEST_CHECK(strcmp(XTuiVim_line(vim, 0), "XXabc") == 0,
                       "vim macro record and playback");
        XConsoleShellTest_vimKey(vim, '@'); XConsoleShellTest_vimKey(vim, '@');
        XCS_TEST_CHECK(strcmp(XTuiVim_line(vim, 0), "XXXabc") == 0,
                       "vim repeat last macro");
    }
#endif
#if XTUI_VIM_VISUAL_ON && XTUI_VIM_YANK_PASTE_ON && \
    XTUI_VIM_SEARCH_ON && XTUI_VIM_SUBSTITUTE_ON && \
    XTUI_VIM_UNDO_REDO_ON
    XTuiVim_setLines(vim, initial, 3);
    XConsoleShellTest_vimKey(vim, 'v'); XConsoleShellTest_vimKey(vim, 'l');
    XConsoleShellTest_vimKey(vim, 'y'); XConsoleShellTest_vimKey(vim, 'p');
    XConsoleShellTest_vimKey(vim, '/'); XConsoleShellTest_vimKey(vim, 't');
    XConsoleShellTest_vimKey(vim, 'w'); XConsoleShellTest_vimKey(vim, 'o');
    XConsoleShellTest_vimEnter(vim);
    XConsoleShellTest_vimKey(vim, ':');
    {
        const char* command = "%s/two/TWO/g";
        const char* p = command;
        while (*p) XConsoleShellTest_vimKey(vim, *p++);
    }
    XConsoleShellTest_vimEnter(vim);
    XCS_TEST_CHECK(strstr(XTuiVim_line(vim, 0), "TWO") != NULL,
                   "vim substitute");
    XConsoleShellTest_vimKey(vim, 'u');
    XCS_TEST_CHECK(strstr(XTuiVim_line(vim, 0), "TWO") == NULL, "vim undo");
    {
        XTuiKeyEvent event;
        XTuiKeyEvent_init(&event, XEVENT_TYPE_KEY_PRESS, XTuiKey_Char,
                          XKeyboardModifier_ControlModifier);
        event.m_utf8[0] = 18; event.m_utf8[1] = '\0';
        XTuiWidget_keyPress_base((XTuiWidget*)vim, &event);
    }
    XCS_TEST_CHECK(strstr(XTuiVim_line(vim, 0), "TWO") != NULL, "vim redo");
    XTuiVim_setLines(vim, (const char*[]){ "abc" }, 1);
    XConsoleShellTest_vimKey(vim, 'i'); XConsoleShellTest_vimKey(vim, 'X');
    XConsoleShellTest_vimEscape(vim);
    XConsoleShellTest_vimKey(vim, 'i'); XConsoleShellTest_vimKey(vim, 'Y');
    XConsoleShellTest_vimEscape(vim);
    XCS_TEST_CHECK(strcmp(XTuiVim_line(vim, 0), "XYabc") == 0,
                   "vim two changes before undo");
    XConsoleShellTest_vimKey(vim, 'u');
    XCS_TEST_CHECK(strcmp(XTuiVim_line(vim, 0), "Xabc") == 0,
                   "vim first undo level");
    XConsoleShellTest_vimKey(vim, 'u');
    XCS_TEST_CHECK(strcmp(XTuiVim_line(vim, 0), "abc") == 0,
                   "vim second undo level");
    XConsoleShellTest_vimControl(vim, 18);
    XCS_TEST_CHECK(strcmp(XTuiVim_line(vim, 0), "Xabc") == 0,
                   "vim first redo level");
    XConsoleShellTest_vimControl(vim, 18);
    XCS_TEST_CHECK(strcmp(XTuiVim_line(vim, 0), "XYabc") == 0,
                   "vim second redo level");
#endif
    /* 各可选状态只应在首次执行对应功能时创建。 */
    XTuiVim_setLines(vim, (const char*[]){ "alpha", "beta" }, 2);
#if XTUI_VIM_HISTORY_ON
    XConsoleShellTest_vimKey(vim, ':');
    XConsoleShellTest_vimKey(vim, 'w');
    XConsoleShellTest_vimEnter(vim);
    XCS_TEST_CHECK(vim->m_commandHistoryState != NULL,
                   "Vim 命令历史按需创建");
    XTuiVim_ackAction(vim);
#if XTUI_VIM_SEARCH_ON
    XConsoleShellTest_vimKey(vim, '/');
    XConsoleShellTest_vimKey(vim, 'a');
    XConsoleShellTest_vimEnter(vim);
    XCS_TEST_CHECK(vim->m_searchHistoryState != NULL,
                   "Vim 搜索历史按需创建");
#endif
#endif
#if XTUI_VIM_REGISTER_ON && XTUI_VIM_YANK_PASTE_ON
    XConsoleShellTest_vimKey(vim, 'y');
    XConsoleShellTest_vimKey(vim, 'y');
    XCS_TEST_CHECK(vim->m_registerState != NULL,
                   "Vim 寄存器表按需创建");
#endif
#if XTUI_VIM_MACRO_ON
    XConsoleShellTest_vimKey(vim, 'q');
    XConsoleShellTest_vimKey(vim, 'a');
    XConsoleShellTest_vimKey(vim, 'q');
    XCS_TEST_CHECK(vim->m_macroState != NULL,
                   "Vim 宏表按需创建");
#endif
#if XTUI_VIM_MARK_ON
    XConsoleShellTest_vimKey(vim, 'm');
    XConsoleShellTest_vimKey(vim, 'a');
    XCS_TEST_CHECK(vim->m_markState != NULL,
                   "Vim 标记表按需创建");
#endif
#if XTUI_VIM_JUMPLIST_ON && XTUI_VIM_ADVANCED_MOTION_ON
    XConsoleShellTest_vimKey(vim, 'g');
    XConsoleShellTest_vimKey(vim, 'g');
    XCS_TEST_CHECK(vim->m_jumpState != NULL,
                   "Vim 跳转表按需创建");
#endif
    {
        XTuiVim copy;
        XTuiVim moved;
        memset(&copy, 0, sizeof(copy));
        memset(&moved, 0, sizeof(moved));
        XCopy((XClass*)&copy, (const XClass*)vim);
        XCS_TEST_CHECK(copy.m_lines &&
                       strcmp(XTuiVim_line(&copy, 0), XTuiVim_line(vim, 0)) == 0,
                       "Vim 复制保留文本缓冲");
#if XTUI_VIM_HISTORY_ON
        XCS_TEST_CHECK(copy.m_commandHistoryState &&
                       copy.m_commandHistoryState != vim->m_commandHistoryState,
                       "Vim 复制深拷贝命令历史");
#endif
#if XTUI_VIM_REGISTER_ON && XTUI_VIM_YANK_PASTE_ON
        XCS_TEST_CHECK(copy.m_registerState &&
                       copy.m_registerState != vim->m_registerState,
                       "Vim 复制深拷贝寄存器表");
#endif
#if XTUI_VIM_MACRO_ON
        XCS_TEST_CHECK(copy.m_macroState && copy.m_macroState != vim->m_macroState,
                       "Vim 复制深拷贝宏表");
#endif
        XMove((XClass*)&moved, (XClass*)&copy);
        XCS_TEST_CHECK(moved.m_lines && copy.m_lines == NULL,
                       "Vim 同内存池移动转移文本缓冲");
#if XTUI_VIM_HISTORY_ON
        XCS_TEST_CHECK(moved.m_commandHistoryState &&
                       copy.m_commandHistoryState == NULL,
                       "Vim 同内存池移动转移命令历史");
#endif
#if XTUI_VIM_REGISTER_ON && XTUI_VIM_YANK_PASTE_ON
        XCS_TEST_CHECK(moved.m_registerState && copy.m_registerState == NULL,
                       "Vim 同内存池移动转移寄存器表");
#endif
        XClass_deinit_base((XClass*)&moved);
        XClass_deinit_base((XClass*)&copy);
    }
#if XTUI_VIM_HISTORY_ON
    {
        XTuiVim* source = XTuiVim_create_ex(XMEMORY_TYPE_SYSTEM);
        XTuiVim* target = XTuiVim_create_ex(XMEMORY_TYPE_HYBRID);
        XCS_TEST_CHECK(source && target, "Vim 跨内存池移动对象创建");
        XCS_TEST_CHECK(Class_Memory(target) == XMemory_method(XMEMORY_TYPE_HYBRID),
                       "Vim 可选状态使用所属内存池");
        XConsoleShellTest_vimKey(source, ':');
        XConsoleShellTest_vimKey(source, 'w');
        XConsoleShellTest_vimEnter(source);
        XMove((XClass*)target, (XClass*)source);
        XCS_TEST_CHECK(target->m_commandHistoryState &&
                       source->m_commandHistoryState == NULL,
                       "Vim 跨内存池移动深拷贝历史状态");
        XTuiVim_delete_base(source);
        XTuiVim_delete_base(target);
    }
#endif
    ok = true;
cleanup:
    XTuiVim_delete_base(vim);
    return ok;
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
        !XDeviceFile_getSpecialPath(XSpecialPath_Temp, temp) ||
        !XString_assign_fmt_utf8(root, "%s/xconsole-shell-linux-%lld",
                                 XString_toUtf8(temp),
                                 (long long)XDateTime_currentMSecsSinceEpoch())) {
        goto cleanup;
    }
    if (XDeviceFile_exists(root) || !XDeviceFile_mkdir(root, false)) goto cleanup;
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
    longFd = xcs_test_open_file(longPath, XDeviceFile_WriteOnly | XDeviceFile_Create |
                              XDeviceFile_Truncate, &longError);
    if (longFd == XFD_INVALID) goto cleanup;
    if (XDeviceFile_write(longFd, "x", 1) != 1) {
        XDeviceFile_close(longFd);
        goto cleanup;
    }
    XDeviceFile_close(longFd);
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
        !XDeviceFile_stat(nested, &stat) || !stat.isDir) goto cleanup;
    if (!XConsoleShellTest_runLine(shell, transport,
                                   "fs write source.txt alpha beta gamma",
                                   XConsoleResult_Ok, NULL)) goto cleanup;
    if (!XDeviceFile_stat(source, &stat) || !stat.isFile || stat.size != 16) goto cleanup;
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
        !XDeviceFile_stat(copy, &stat) || !stat.isFile) goto cleanup;
    if (!XConsoleShellTest_runLine(shell, transport, "fs mv copy.txt moved.txt",
                                   XConsoleResult_Ok, NULL) ||
        XDeviceFile_exists(copy) || !XDeviceFile_exists(moved)) goto cleanup;
    if (!XConsoleShellTest_runLine(shell, transport, "fs link source.txt link.txt",
                                   XConsoleResult_Ok, NULL) ||
        !XDeviceFile_exists(link)) goto cleanup;
    if (!XConsoleShellTest_runLine(shell, transport, "fs cat link.txt",
                                   XConsoleResult_Ok, "alpha beta gamma")) goto cleanup;
    if (!XConsoleShellTest_runLine(shell, transport, "fs rm link.txt",
                                   XConsoleResult_Ok, NULL) || XDeviceFile_exists(link)) goto cleanup;
#if XCONSOLE_SHELL_FS_LN_ON && XCONSOLE_SHELL_FS_UNLINK_ON
    if (!XConsoleShellTest_runLine(shell, transport, "ln -s source.txt link.txt",
                                   XConsoleResult_Ok, NULL) ||
        !XConsoleShellTest_runLine(shell, transport, "unlink link.txt",
                                   XConsoleResult_Ok, NULL) || XDeviceFile_exists(link)) goto cleanup;
#endif
    if (!XConsoleShellTest_runLine(shell, transport, "fs ls .",
                                   XConsoleResult_Ok, "source.txt")) goto cleanup;
    if (strstr(transport->output, "link.txt")) goto cleanup;
    if (!XConsoleShellTest_runLine(shell, transport, "rm -rf nested",
                                   XConsoleResult_Ok, NULL) || XDeviceFile_exists(nested)) goto cleanup;
    if (!XConsoleShellTest_runLine(shell, transport, "fs mkdir nested/deep -p",
                                   XConsoleResult_Ok, NULL) ||
        !XConsoleShellTest_runLine(shell, transport, "rm -rf nested",
                                   XConsoleResult_Ok, NULL) || XDeviceFile_exists(nested)) goto cleanup;
    if (!XConsoleShellTest_runLine(shell, transport, "fs ls .",
                                   XConsoleResult_Ok, "source.txt")) goto cleanup;
    if (!XDeviceFile_stat(source, &stat)) {
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
        XDeviceFile_exists(source) || XDeviceFile_exists(moved)) goto cleanup;
#if XCONSOLE_SHELL_FS_FORMAT_ON
    if (!XConsoleShellTest_runLine(shell, transport, "fs format .",
                                   XConsoleResult_Failed, NULL)) goto cleanup;
#endif
    ok = true;

cleanup:
    if (rootCreated && root) XDeviceFile_rmdir(root, true);
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
    XString* other = NULL;
    XString* command = NULL;
    char content[512];
    XFd fd;
    int error = 0;
    int64_t size;
    bool ok = false;
    bool rootCreated = false;

    if (!temp || !root ||
        !XDeviceFile_getSpecialPath(XSpecialPath_Temp, temp) ||
        !XString_assign_fmt_utf8(root, "%s/xconsole-shell-vi-%lld",
                                 XString_toUtf8(temp),
                                 (long long)XDateTime_currentMSecsSinceEpoch()))
        goto cleanup;
    if (XDeviceFile_exists(root) || !XDeviceFile_mkdir(root, false)) goto cleanup;
    rootCreated = true;
    file = XString_create_fmt_utf8("%s/edit.txt", XString_toUtf8(root));
    if (!file) goto cleanup;
    other = XString_create_fmt_utf8("%s/other.txt", XString_toUtf8(root));
    if (!other) goto cleanup;
    command = XString_create_fmt_utf8("vi %s", XString_toUtf8(file));
    if (!command) goto cleanup;

#if XCONSOLE_SHELL_EDITOR_TUI_ON && XTUI_ON && XTUI_VIM_ON
    /* 打开新文件进入全屏 vim，随后用字节流驱动 XTui 状态机。 */
    {
        size_t queries = transport->terminalSizeQueries;
        if (!XConsoleShellTest_runLine(shell, transport, XString_toUtf8(command),
                                       XConsoleResult_MoreOutput, NULL))
            goto cleanup;
        XCS_TEST_CHECK(transport->terminalSizeQueries > queries,
                       "全屏 vim 查询会话终端尺寸");
    }
    XCS_TEST_CHECK(!transport->inputEchoEnabled,
                   "全屏 vim 期间关闭输入回显");
    /* 模拟 SSH window-change：尺寸变化必须先清理物理终端，再按新尺寸
       完整绘制，不能只依赖差异快照留下旧窗口中的内容。 */
    transport->length = 0;
    transport->output[0] = '\0';
    transport->terminalColumns = 60;
    transport->terminalRows = 12;
    XCS_TEST_CHECK(XConsoleShell_refreshForSession(
                       shell, XConsoleShell_session(shell)),
                   "vim 尺寸变化后刷新");
    XCS_TEST_CHECK(transport->terminalSizeQueries > 0,
                   "vim 尺寸变化重新查询终端尺寸");
    XCS_TEST_CHECK(strstr(transport->output, "\x1b[2J\x1b[H") != NULL,
                   "vim 尺寸变化发送完整清屏序列");
    XString_delete_base(command);
    command = NULL;
    /* i 进入插入模式，输入内容后 ESC 返回命令模式。 */
    if (!XConsoleShellTest_feedEditor(shell, transport, "i", 1,
                                      XConsoleResult_MoreOutput, NULL))
        goto cleanup;
    /* TUI 输出带 ANSI 光标控制序列，字符间不连续；文本正确性由
       保存后的文件内容断言保证。 */
    XCS_TEST_CHECK(!XConsoleShell_canBackspace(shell,
                                               XConsoleShell_session(shell)),
                   "vim empty insert backspace boundary");
    if (!XConsoleShellTest_feedEditor(shell, transport, "h\x7f", 2,
                                      XConsoleResult_MoreOutput, NULL))
        goto cleanup;
    XCS_TEST_CHECK(!XConsoleShell_canBackspace(shell,
                                               XConsoleShell_session(shell)),
                   "vim backspace returns to empty input");
    if (!XConsoleShellTest_feedEditor(shell, transport, "hello", 5,
                                      XConsoleResult_MoreOutput, NULL))
        goto cleanup;
    /* 插入模式方向键：只移动光标，不得产生任何输入回显或退出插入模式。 */
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
    XCS_TEST_CHECK(transport->inputEchoEnabled,
                   "vim 退出后恢复输入回显");
    fd = xcs_test_open_file(file, XDeviceFile_ReadOnly, &error);
    if (fd == XFD_INVALID) goto cleanup;
    size = XDeviceFile_read(fd, content, (int64_t)sizeof(content) - 1);
    XDeviceFile_close(fd);
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

#if XTUI_VIM_MULTIBUFFER_ON
    /* 多缓冲：在两个文件之间切换，分别修改并保存。 */
    command = XString_create_fmt_utf8("vi %s", XString_toUtf8(file));
    if (!command) goto cleanup;
    if (!XConsoleShellTest_runLine(shell, transport, XString_toUtf8(command),
                                   XConsoleResult_MoreOutput, NULL))
        goto cleanup;
    XString_delete_base(command);
    command = XString_create_fmt_utf8(":e %s\n", XString_toUtf8(other));
    if (!command) goto cleanup;
    if (!XConsoleShellTest_feedEditor(shell, transport, XString_toUtf8(command),
                                      XString_size_base(command),
                                      XConsoleResult_MoreOutput, NULL))
        goto cleanup;
    XString_delete_base(command);
    command = NULL;
    if (!XConsoleShellTest_feedEditor(shell, transport, "iB\x1b", 3,
                                      XConsoleResult_MoreOutput, NULL) ||
        !XConsoleShellTest_feedEditor(shell, transport, ":bprev\n", 7,
                                      XConsoleResult_MoreOutput, NULL) ||
        !XConsoleShellTest_feedEditor(shell, transport, "AA\x1b", 3,
                                      XConsoleResult_MoreOutput, NULL) ||
        !XConsoleShellTest_feedEditor(shell, transport, ":w\n", 3,
                                      XConsoleResult_MoreOutput, NULL) ||
        !XConsoleShellTest_feedEditor(shell, transport, ":bnext\n", 7,
                                      XConsoleResult_MoreOutput, NULL) ||
        !XConsoleShellTest_feedEditor(shell, transport, ":wq\n", 4,
                                      XConsoleResult_Ok, NULL))
        goto cleanup;
    fd = xcs_test_open_file(file, XDeviceFile_ReadOnly, &error);
    if (fd == XFD_INVALID) goto cleanup;
    size = XDeviceFile_read(fd, content, (int64_t)sizeof(content) - 1);
    XDeviceFile_close(fd);
    if (size <= 0) goto cleanup;
    content[size] = '\0';
    if (strstr(content, "helloA") == NULL) goto cleanup;
    fd = xcs_test_open_file(other, XDeviceFile_ReadOnly, &error);
    if (fd == XFD_INVALID) goto cleanup;
    size = XDeviceFile_read(fd, content, (int64_t)sizeof(content) - 1);
    XDeviceFile_close(fd);
    if (size <= 0) goto cleanup;
    content[size] = '\0';
    if (strstr(content, "B") == NULL) goto cleanup;
#endif

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
    fd = xcs_test_open_file(file, XDeviceFile_ReadOnly, &error);
    if (fd == XFD_INVALID) goto cleanup;
    size = XDeviceFile_read(fd, content, (int64_t)sizeof(content) - 1);
    XDeviceFile_close(fd);
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
    fd = xcs_test_open_file(file, XDeviceFile_ReadOnly, &error);
    if (fd == XFD_INVALID) goto cleanup;
    size = XDeviceFile_read(fd, content, (int64_t)sizeof(content) - 1);
    XDeviceFile_close(fd);
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
    fd = xcs_test_open_file(file, XDeviceFile_ReadOnly, &error);
    if (fd == XFD_INVALID) goto cleanup;
    size = XDeviceFile_read(fd, content, (int64_t)sizeof(content) - 1);
    XDeviceFile_close(fd);
    if (size <= 0) goto cleanup;
    content[size] = '\0';
    if (strstr(content, "discarded") != NULL)
        goto cleanup;
#endif

    ok = true;

cleanup:
    if (rootCreated && root) XDeviceFile_rmdir(root, true);
    if (file) XString_delete_base(file);
    if (other) XString_delete_base(other);
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
    XDeviceFile_removePermanent(crlfPath);
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

    XDeviceFile_removePermanent(crlfPath);
    if (crlfShell) XConsoleShell_delete_base(crlfShell);
    XString_delete_base(crlfPath);
    return ok;
}
#endif

#if XCONSOLE_SHELL_COMPLETION_ON
static bool XConsoleShellTest_runCompletion(XConsoleShell* shell,
                                            XConsoleShellTestTransport* transport)
{
    /* 1. 命令名唯一补全：ec<Tab> -> echo，随后 hello 作为参数执行 */
    memset(transport->output, 0, sizeof(transport->output));
    transport->length = 0;
    if (XConsoleShell_feedData(shell, "ec\t hello\n", 10) != XConsoleResult_Ok)
        return false;
#if XCONSOLE_SHELL_ASYNC_OUTPUT_ON
    if (!XConsoleShell_flushOutput(shell)) return false;
#endif
    if (!strstr(transport->output, "hello")) {
        XPrintf("completion command output missing: [%s]\n", transport->output);
        return false;
    }
    /* 2. 歧义命令列出候选：vi<Tab> 应同时列出 vi 与 vim */
    memset(transport->output, 0, sizeof(transport->output));
    transport->length = 0;
    if (XConsoleShell_feedData(shell, "vi\t", 3) != XConsoleResult_Ok)
        return false;
#if XCONSOLE_SHELL_ASYNC_OUTPUT_ON
    if (!XConsoleShell_flushOutput(shell)) return false;
#endif
    if (!strstr(transport->output, "vi ") || !strstr(transport->output, "vim ")) {
        XPrintf("completion ambiguous output missing: [%s]\n", transport->output);
        return false;
    }
    if (strncmp(transport->output, "\r\n", 2) != 0) {
        XPrintf("completion candidates did not start on a new line: [%s]\n",
                transport->output);
        return false;
    }
    /* 候选列出后行缓冲仍保留原词，用 Ctrl-C 清空后再测路径补全。 */
    memset(transport->output, 0, sizeof(transport->output));
    transport->length = 0;
    {
        XConsoleResult r = XConsoleShell_feedData(shell, "\x03", 1);
        /* Ctrl-C 清空活动行并返回已取消，属于正常清空路径。 */
        if (r != XConsoleResult_Ok && r != XConsoleResult_Cancelled)
            return false;
#if XCONSOLE_SHELL_ASYNC_OUTPUT_ON
        if (!XConsoleShell_flushOutput(shell)) return false;
#endif
    }
#if XCONSOLE_SHELL_FILESYSTEM_ON && XCONSOLE_SHELL_FS_CAT_ON
    /* 3. 文件路径补全：cat /tmp/xcs_complete_t<Tab> -> 完整路径并执行 */
    {
        const char* pathText = "/tmp/xcs_complete_test_file.txt";
        XString* testPath = XString_create_utf8(pathText);
        XFd wfd;
        int error = 0;
        bool ok = false;
        if (!testPath) return false;
        XDeviceFile_removePermanent(testPath);
        wfd = xcs_test_open_file(testPath, XDeviceFile_WriteOnly |
                               XDeviceFile_Truncate, &error);
        if (wfd != XFD_INVALID) {
            ok = XDeviceFile_write(wfd, "COMPLETE_OK", 11) == 11;
            XDeviceFile_close(wfd);
        }
        XString_delete_base(testPath);
        if (!ok) return false;
    }
    memset(transport->output, 0, sizeof(transport->output));
    transport->length = 0;
    if (XConsoleShell_feedData(shell, "cat /tmp/xcs_complete_t\t\n", 25) != XConsoleResult_Ok)
        return false;
#if XCONSOLE_SHELL_ASYNC_OUTPUT_ON
    if (!XConsoleShell_flushOutput(shell)) return false;
#endif
    if (!strstr(transport->output, "COMPLETE_OK")) {
        XPrintf("completion path output missing: [%s]\n", transport->output);
        return false;
    }
    {
        XString* cleanupPath = XString_create_utf8("/tmp/xcs_complete_test_file.txt");
        if (cleanupPath) {
            XDeviceFile_removePermanent(cleanupPath);
            XString_delete_base(cleanupPath);
        }
    }
#endif
#if XCONSOLE_SHELL_FILESYSTEM_ON && XCONSOLE_SHELL_FS_CAT_ON && \
    XCONSOLE_SHELL_FS_CD_ON && XCONSOLE_SHELL_SUBCOMMAND_ON
    /* 4. cd 目录专用补全：只列出目录，不列出同名前缀的文件 */
    {
        XString* dirA = XString_create_utf8("/tmp/xcs_complete_dir_a");
        XString* dirB = XString_create_utf8("/tmp/xcs_complete_dir_b");
        XString* fileN = XString_create_utf8("/tmp/xcs_complete_file_note.txt");
        bool ok = true;
        if (!dirA || !dirB || !fileN) { ok = false; }
        else {
            XDeviceFile_mkdir(dirA, false);
            XDeviceFile_mkdir(dirB, false);
            {
                XFd wfd;
                int e = 0;
                XDeviceFile_removePermanent(fileN);
                wfd = xcs_test_open_file(fileN, XDeviceFile_WriteOnly |
                                       XDeviceFile_Truncate, &e);
                if (wfd != XFD_INVALID) {
                    ok = XDeviceFile_write(wfd, "N", 1) == 1;
                    XDeviceFile_close(wfd);
                }
            }
        }
        if (ok) {
            memset(transport->output, 0, sizeof(transport->output));
            transport->length = 0;
            if (XConsoleShell_feedData(shell, "cd /tmp/xcs_complete_d\t", 23) != XConsoleResult_Ok)
                ok = false;
#if XCONSOLE_SHELL_ASYNC_OUTPUT_ON
            else if (!XConsoleShell_flushOutput(shell)) ok = false;
#endif
            else if (!strstr(transport->output, "/tmp/xcs_complete_dir_") ||
                     strstr(transport->output, "xcs_complete_file_note")) {
                XPrintf("cd dir-only common prefix missing: [%s]\n", transport->output);
                ok = false;
            }
        }
        if (ok) {
            memset(transport->output, 0, sizeof(transport->output));
            transport->length = 0;
            if (XConsoleShell_feedData(shell, "\x03", 1) != XConsoleResult_Ok &&
                XConsoleShell_feedData(shell, "\x03", 1) != XConsoleResult_Cancelled)
                ok = false;
        }
        if (ok) {
            memset(transport->output, 0, sizeof(transport->output));
            transport->length = 0;
            if (XConsoleShell_feedData(shell, "cd /tmp/xcs_complete_dir_a\t", 27) != XConsoleResult_Ok)
                ok = false;
#if XCONSOLE_SHELL_ASYNC_OUTPUT_ON
            else if (!XConsoleShell_flushOutput(shell)) ok = false;
#endif
            else if (!strstr(transport->output, "/tmp/xcs_complete_dir_a/")) {
                XPrintf("cd unique dir slash missing: [%s]\n", transport->output);
                ok = false;
            }
        }
        if (ok) {
            memset(transport->output, 0, sizeof(transport->output));
            transport->length = 0;
            if (XConsoleShell_feedData(shell, "\x03", 1) != XConsoleResult_Ok &&
                XConsoleShell_feedData(shell, "\x03", 1) != XConsoleResult_Cancelled)
                ok = false;
        }
        if (dirA) XDeviceFile_rmdir(dirA, false);
        if (dirB) XDeviceFile_rmdir(dirB, false);
        if (fileN) XDeviceFile_removePermanent(fileN);
        XString_delete_base(dirA);
        XString_delete_base(dirB);
        XString_delete_base(fileN);
        if (!ok) return false;
    }
    /* 5. 含空格文件名补全：补全后仍能通过 cat 正确读取 */
    {
        const char* spacedPath = "/tmp/xcs complete file.txt";
        XString* spaced = XString_create_utf8(spacedPath);
        XFd wfd;
        int error = 0;
        bool ok = false;
        if (!spaced) return false;
        XDeviceFile_removePermanent(spaced);
        wfd = xcs_test_open_file(spaced, XDeviceFile_WriteOnly |
                               XDeviceFile_Truncate, &error);
        if (wfd != XFD_INVALID) {
            ok = XDeviceFile_write(wfd, "SPACE_OK", 8) == 8;
            XDeviceFile_close(wfd);
        }
        if (!ok) {
            XString_delete_base(spaced);
            return false;
        }
        memset(transport->output, 0, sizeof(transport->output));
        transport->length = 0;
        if (XConsoleShell_feedData(shell, "cat /tmp/xcs\\ compl\t\n", 21) != XConsoleResult_Ok) {
            XDeviceFile_removePermanent(spaced);
            XString_delete_base(spaced);
            return false;
        }
#if XCONSOLE_SHELL_ASYNC_OUTPUT_ON
        if (!XConsoleShell_flushOutput(shell)) {
            XDeviceFile_removePermanent(spaced);
            XString_delete_base(spaced);
            return false;
        }
#endif
        if (!strstr(transport->output, "SPACE_OK")) {
            XPrintf("escaped-space path output missing: [%s]\n", transport->output);
            XDeviceFile_removePermanent(spaced);
            XString_delete_base(spaced);
            return false;
        }
        XDeviceFile_removePermanent(spaced);
        XString_delete_base(spaced);
    }
    /* 6. fs cd 子命令参数目录补全 */
    {
        XString* subDir = XString_create_utf8("/tmp/xcs_complete_dir_a");
        bool ok = true;
        XConsoleResult r6;
        if (!subDir) return false;
        XDeviceFile_mkdir(subDir, false);
        memset(transport->output, 0, sizeof(transport->output));
        transport->length = 0;
        r6 = XConsoleShell_feedData(shell, "fs cd /tmp/xcs_complete_dir_a\t", 30);
        if (r6 != XConsoleResult_Ok) ok = false;
#if XCONSOLE_SHELL_ASYNC_OUTPUT_ON
        if (ok && !XConsoleShell_flushOutput(shell)) ok = false;
#endif
        if (ok && !strstr(transport->output, "/tmp/xcs_complete_dir_a/")) {
            XPrintf("fs cd dir slash missing: [%s]\n", transport->output);
            ok = false;
        }
        if (ok) {
            memset(transport->output, 0, sizeof(transport->output));
            transport->length = 0;
            {
                XConsoleResult rc = XConsoleShell_feedData(shell, "\x03", 1);
                if (rc != XConsoleResult_Ok && rc != XConsoleResult_Cancelled)
                    ok = false;
            }
        }
        XDeviceFile_rmdir(subDir, false);
        XString_delete_base(subDir);
        if (!ok) return false;
    }
    /* 6b. 选项补全：fs cat --off<Tab> 唯一补全为 --offset */
    {
        memset(transport->output, 0, sizeof(transport->output));
        transport->length = 0;
        if (XConsoleShell_feedData(shell, "fs cat --off	", 13) != XConsoleResult_Ok)
            return false;
#if XCONSOLE_SHELL_ASYNC_OUTPUT_ON
        if (!XConsoleShell_flushOutput(shell)) return false;
#endif
        if (!strstr(transport->output, "--offset ")) {
            XPrintf("option completion missing: [%s]\n", transport->output);
            return false;
        }
        memset(transport->output, 0, sizeof(transport->output));
        transport->length = 0;
        {
            XConsoleResult rc = XConsoleShell_feedData(shell, "\x03", 1);
            if (rc != XConsoleResult_Ok && rc != XConsoleResult_Cancelled)
                return false;
#if XCONSOLE_SHELL_ASYNC_OUTPUT_ON
            if (!XConsoleShell_flushOutput(shell)) return false;
#endif
        }
    }
    /* 6c. 组合短选项补全：fs cat -n<Tab> 展开并唯一补全为 -n */
    {
        memset(transport->output, 0, sizeof(transport->output));
        transport->length = 0;
        if (XConsoleShell_feedData(shell, "fs cat -n	", 10) != XConsoleResult_Ok)
            return false;
#if XCONSOLE_SHELL_ASYNC_OUTPUT_ON
        if (!XConsoleShell_flushOutput(shell)) return false;
#endif
        if (!strstr(transport->output, "-n ")) {
            XPrintf("short option completion missing: [%s]\n", transport->output);
            return false;
        }
        memset(transport->output, 0, sizeof(transport->output));
        transport->length = 0;
        {
            XConsoleResult rc = XConsoleShell_feedData(shell, "\x03", 1);
            if (rc != XConsoleResult_Ok && rc != XConsoleResult_Cancelled)
                return false;
#if XCONSOLE_SHELL_ASYNC_OUTPUT_ON
            if (!XConsoleShell_flushOutput(shell)) return false;
#endif
        }
    }
    /* 7. 命令词路径补全：./ 前缀补全可执行脚本路径 */
    {
        XString* script = XString_create_utf8("/tmp/xcs_complete_script.sh");
        XFd wfd;
        int error = 0;
        bool ok = false;
        if (!script) return false;
        XDeviceFile_removePermanent(script);
        wfd = xcs_test_open_file(script, XDeviceFile_WriteOnly |
                               XDeviceFile_Truncate, &error);
        if (wfd != XFD_INVALID) {
            ok = XDeviceFile_write(wfd, "#! /bin/sh\n", 11) == 11;
            XDeviceFile_close(wfd);
        }
        if (!ok) {
            XString_delete_base(script);
            return false;
        }
        memset(transport->output, 0, sizeof(transport->output));
        transport->length = 0;
        if (XConsoleShell_feedData(shell, "cd /tmp\n", 8) != XConsoleResult_Ok) {
            XDeviceFile_removePermanent(script);
            XString_delete_base(script);
            return false;
        }
#if XCONSOLE_SHELL_ASYNC_OUTPUT_ON
        if (!XConsoleShell_flushOutput(shell)) {
            XDeviceFile_removePermanent(script);
            XString_delete_base(script);
            return false;
        }
#endif
        memset(transport->output, 0, sizeof(transport->output));
        transport->length = 0;
        if (XConsoleShell_feedData(shell, "./xcs_complete_sc\t", 18) != XConsoleResult_Ok) {
            XDeviceFile_removePermanent(script);
            XString_delete_base(script);
            return false;
        }
#if XCONSOLE_SHELL_ASYNC_OUTPUT_ON
        if (!XConsoleShell_flushOutput(shell)) {
            XDeviceFile_removePermanent(script);
            XString_delete_base(script);
            return false;
        }
#endif
        if (!strstr(transport->output, "./xcs_complete_script.sh")) {
            XPrintf("./ command path completion missing: [%s]\n", transport->output);
            XDeviceFile_removePermanent(script);
            XString_delete_base(script);
            return false;
        }
        XDeviceFile_removePermanent(script);
        XString_delete_base(script);
        /* 清空上一轮补全留下的活动行，避免影响引号补全用例。 */
        memset(transport->output, 0, sizeof(transport->output));
        transport->length = 0;
        {
            XConsoleResult r = XConsoleShell_feedData(shell, "\x03", 1);
            if (r != XConsoleResult_Ok && r != XConsoleResult_Cancelled)
                return false;
#if XCONSOLE_SHELL_ASYNC_OUTPUT_ON
            if (!XConsoleShell_flushOutput(shell)) return false;
#endif
        }
    }
    /* 8. 引号内路径补全：`cat "/tmp/xcs compl<Tab>` 保留引号并正确执行 */
    {
        const char* qPath = "/tmp/xcs complete file.txt";
        XString* q = XString_create_utf8(qPath);
        XFd wfd;
        int error = 0;
        bool ok = false;
        if (!q) return false;
        XDeviceFile_removePermanent(q);
        wfd = xcs_test_open_file(q, XDeviceFile_WriteOnly |
                               XDeviceFile_Truncate, &error);
        if (wfd != XFD_INVALID) {
            ok = XDeviceFile_write(wfd, "QUOTE_OK", 8) == 8;
            XDeviceFile_close(wfd);
        }
        if (!ok) {
            XString_delete_base(q);
            return false;
        }
        memset(transport->output, 0, sizeof(transport->output));
        transport->length = 0;
        {
            XConsoleResult rq = XConsoleShell_feedData(shell, "cat \"/tmp/xcs compl\t\n", 21);
            if (rq != XConsoleResult_Ok) {
                XDeviceFile_removePermanent(q);
                XString_delete_base(q);
                return false;
            }
        }
#if XCONSOLE_SHELL_ASYNC_OUTPUT_ON
        if (!XConsoleShell_flushOutput(shell)) {
            XDeviceFile_removePermanent(q);
            XString_delete_base(q);
            return false;
        }
#endif
        if (!strstr(transport->output, "QUOTE_OK")) {
            XPrintf("quoted path completion missing: [%s]\n", transport->output);
            XDeviceFile_removePermanent(q);
            XString_delete_base(q);
            return false;
        }
        XDeviceFile_removePermanent(q);
        XString_delete_base(q);
    }
#endif
    return true;
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
    transport.inputEchoEnabled = true;
    transport.terminalColumns = 80;
    transport.terminalRows = 24;
    memset(&io, 0, sizeof(io));
    io.read = XConsoleShellTest_read;
    io.write = XConsoleShellTest_write;
    io.flush = XConsoleShellTest_flush;
    io.inputEcho = XConsoleShellTest_inputEcho;
    io.terminalSize = XConsoleShellTest_terminalSize;
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
        XDeviceFile_removePermanent(loginPath);
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
            loginFd = xcs_test_open_file(loginPath, XDeviceFile_ReadOnly, &loginError);
            XCS_TEST_CHECK(loginFd != XFD_INVALID, "login database persisted");
            loginSize = XDeviceFile_read(loginFd, loginJson,
                                         (int64_t)sizeof(loginJson) - 1);
            XDeviceFile_close(loginFd);
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
        XDeviceFile_removePermanent(loginPath);
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
        {
            static const uint8_t fragmentedCsi[] = {
                0x1b, '[', '<', '6', '4', ';', '1', '0', ';', '1', '0', 'M',
                'e', 'c', 'h', 'o', ' ', 'c', 's', 'i', '-', 's', 'e', 's',
                's', 'i', 'o', 'n', '\n'
            };
            size_t i;
            bool csiOk = true;
            secondaryTransport.length = 0;
            secondaryTransport.output[0] = '\0';
            for (i = 0; i < sizeof(fragmentedCsi); ++i) {
                if (XConsoleShell_feedByteForSession(
                        shell, secondary, fragmentedCsi[i]) != XConsoleResult_Ok) {
                    csiOk = false;
                    break;
                }
            }
            XCS_TEST_CHECK(csiOk &&
                               strstr(secondaryTransport.output, "csi-session"),
                           "secondary fragmented CSI does not enter command line");
        }
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
        const uint8_t arrows[] = { 0x1b, '[', 'A', 0x1b, '[', 'B',
                                   0x1b, '[', 'C', 0x1b, '[', 'D' };
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
        telnetTransport.length = 0;
        telnetTransport.output[0] = '\0';
        XCS_TEST_CHECK(XConsoleShellTelnetAdapter_feedData(
                           &adapter, telnetShell, NULL, arrows, sizeof(arrows)) ==
                           XConsoleResult_Ok && adapter.echoEscape == 0 &&
                           !strstr(telnetTransport.output, "ABCD"),
                       "telnet arrow sequences are not echoed as letters");
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
    transport.length = 0;
    transport.output[0] = '\0';
    XCS_TEST_CHECK(XConsoleShell_feedData(shell, "move", 4) == XConsoleResult_Ok &&
                       XConsoleShell_feedData(shell, "\x1b[D", 3) == XConsoleResult_Ok &&
                       shell->m_lineCursor == 3u &&
                       strcmp(shell->m_lineBuffer, "move") == 0 &&
                       strstr(transport.output, "\x1b[1D") != NULL,
                   "line editor left arrow redraw");
    transport.length = 0;
    transport.output[0] = '\0';
    XCS_TEST_CHECK(XConsoleShell_feedData(shell, "X", 1) == XConsoleResult_Ok &&
                       shell->m_lineCursor == 4u &&
                       strcmp(shell->m_lineBuffer, "movXe") == 0 &&
                       strstr(transport.output, "movXe") != NULL &&
                       strstr(transport.output, "\x1b[1D") != NULL,
                   "line editor middle insert redraw");
    transport.length = 0;
    transport.output[0] = '\0';
    XCS_TEST_CHECK(XConsoleShell_feedData(shell, "\x04", 1) == XConsoleResult_Ok &&
                       shell->m_lineCursor == 4u &&
                       strcmp(shell->m_lineBuffer, "movX") == 0 &&
                       strstr(transport.output, "movX") != NULL,
                   "line editor delete redraw");
    XCS_TEST_CHECK(XConsoleShell_feedData(shell, "\n", 1) == XConsoleResult_UnknownCommand,
                   "line editor test line cleanup");
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
    {
        static const char standaloneEsc[] = "\x1b" "echo escape-clean\n";
        static const char mouseCsi[] = "\x1b[<64;10;10Mecho csi-clean\n";
        static const char functionSs3[] = "\x1bOPecho ss3-clean\n";
        transport.length = 0;
        transport.output[0] = '\0';
        XCS_TEST_CHECK(
            XConsoleShell_feedData(shell, standaloneEsc,
                                   sizeof(standaloneEsc) - 1u) == XConsoleResult_Ok &&
                strstr(transport.output, "escape-clean"),
            "standalone ESC does not swallow next command byte");
        transport.length = 0;
        transport.output[0] = '\0';
        XCS_TEST_CHECK(
            XConsoleShell_feedData(shell, mouseCsi,
                                   sizeof(mouseCsi) - 1u) == XConsoleResult_Ok &&
                strstr(transport.output, "csi-clean"),
            "mouse CSI sequence does not enter command line");
        transport.length = 0;
        transport.output[0] = '\0';
        XCS_TEST_CHECK(
            XConsoleShell_feedData(shell, functionSs3,
                                   sizeof(functionSs3) - 1u) == XConsoleResult_Ok &&
                strstr(transport.output, "ss3-clean"),
            "SS3 sequence does not enter command line");
    }
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
    XDeviceFile_removePermanent(filePath);
    fd = xcs_test_open_file(filePath, XDeviceFile_WriteOnly | XDeviceFile_Create |
                          XDeviceFile_Truncate, &error);
    XCS_TEST_CHECK(fd != XFD_INVALID, "create shell file");
    XCS_TEST_CHECK(XDeviceFile_write(fd, fileText, sizeof(fileText) - 1) ==
                       (int64_t)(sizeof(fileText) - 1), "write shell file");
    XDeviceFile_close(fd);
#if XCONSOLE_SHELL_SCRIPT_ON
    {
        XString* scriptPath = XString_create_utf8("xconsole_shell_script.txt");
        const char scriptText[] = "echo scripted\n";
        XCS_TEST_CHECK(scriptPath != NULL, "script path");
        fd = xcs_test_open_file(scriptPath, XDeviceFile_WriteOnly | XDeviceFile_Create |
                              XDeviceFile_Truncate, &error);
        XCS_TEST_CHECK(fd != XFD_INVALID, "create script file");
        XCS_TEST_CHECK(XDeviceFile_write(fd, scriptText, sizeof(scriptText) - 1) ==
                           (int64_t)(sizeof(scriptText) - 1), "write script file");
        XDeviceFile_close(fd);
        XCS_TEST_CHECK(XConsoleShell_processLine(shell, "source xconsole_shell_script.txt",
                                                 strlen("source xconsole_shell_script.txt")) ==
                           XConsoleResult_Ok && strstr(transport.output, "scripted"),
                       "source command");
        XDeviceFile_removePermanent(scriptPath);
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
        if (XDeviceFile_stat(filePath, &st)) {
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
        if (XDeviceFile_stat(filePath, &st)) {
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
        if (XDeviceFile_stat(filePath, &st)) {
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
        if (XDeviceFile_stat(filePath, &st)) {
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
        if (XDeviceFile_stat(filePath, &st)) {
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
        if (dir && XDeviceFile_stat(dir, &st)) {
            XCS_TEST_CHECK((st.permissions & XFile_ExeOwner) != 0 &&
                               (st.permissions & XFile_ExeGroup) != 0 &&
                               (st.permissions & XFile_ExeOther) != 0,
                           "chmod symbolic X exec on directory");
        }
        if (dir) XDeviceFile_removePermanent(dir);
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
        XDeviceFile_removePermanent(redirectPath);
        {
            XConsoleResult redirectResult = XConsoleShell_processLine(
                shell,
                "exec sh -c 'printf redirected' --stdout xconsole_shell_redirect.txt",
                strlen("exec sh -c 'printf redirected' --stdout xconsole_shell_redirect.txt"));
            XCS_TEST_CHECK(redirectResult == XConsoleResult_Ok,
                           "redirect command");
        }
        redirectFd = xcs_test_open_file(redirectPath, XDeviceFile_ReadOnly, &redirectError);
        XCS_TEST_CHECK(redirectFd != XFD_INVALID, "redirect output file");
        XCS_TEST_CHECK(XDeviceFile_read(redirectFd, redirectText,
                                        (int64_t)sizeof(redirectText) - 1) == 10 &&
                           strcmp(redirectText, "redirected") == 0,
                       "redirect output content");
        XDeviceFile_close(redirectFd);
        XDeviceFile_removePermanent(redirectPath);
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
#if XTUI_ON && XTUI_WIDGET_ON && XTUI_VIM_ON
    XCS_TEST_CHECK(XConsoleShellTest_runVimAdvanced(),
                   "vim Linux behavior commands");
#endif
    XDeviceFile_removePermanent(filePath);
    XString_delete_base(filePath);
#if XCONSOLE_SHELL_COMPLETION_ON
    XCS_TEST_CHECK(XConsoleShellTest_runCompletion(shell, &transport),
                   "shell completion");
#endif
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
