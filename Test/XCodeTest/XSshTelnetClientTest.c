#include "XSshTelnetClientTest.h"
#include "XSshClient.h"
#include "XSshServer.h"
#include "XTelnetClient.h"
#include "XTelnetServer.h"
#include "XIODevice.h"
#include "XByteArray.h"
#include "XMemory.h"
#include "XPrintf.h"
#include <string.h>

XCLASS_DEFINE_BEGING(XSshTelnetTestDevice)
XCLASS_DEFINE_EXTEND_END(XSshTelnetTestDevice, XIODevice)

typedef struct XSshTelnetTestDevice {
    XIODevice m_base;
    XByteArray* output;
} XSshTelnetTestDevice;

static int64_t xshtelnet_test_read(const XIODevice* device, char* data,
                                   int64_t maxlen)
{
    (void)device;
    (void)data;
    (void)maxlen;
    return 0;
}

static int64_t xshtelnet_test_available(const XIODevice* device)
{
    (void)device;
    return 0;
}

static int64_t xshtelnet_test_write(XIODevice* device, const char* data,
                                    int64_t len)
{
    XSshTelnetTestDevice* test = (XSshTelnetTestDevice*)device;
    if (!test || !test->output || !data || len < 0) return -1;
    return XVector_push_back_2((XVector*)test->output, data, (size_t)len) ? len : -1;
}

static void xshtelnet_test_deinit(XSshTelnetTestDevice* device)
{
    if (!device) return;
    if (device->output) XByteArray_delete_base(device->output);
    XClass_Deinit_Parent(XIODevice, device);
}

static XVtable* xshtelnet_test_device_class_init(void)
{
    XVTABLE_INIT_DEFAULT(XSshTelnetTestDevice)
    XVTABLE_INHERIT_XCLASS(XIODevice);
    XVTABLE_OVERLOAD_DEFAULT(EXIODevice_BytesAvailable, xshtelnet_test_available);
    XVTABLE_OVERLOAD_DEFAULT(EXIODevice_ReadData, xshtelnet_test_read);
    XVTABLE_OVERLOAD_DEFAULT(EXIODevice_WriteData, xshtelnet_test_write);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Deinit, xshtelnet_test_deinit);
    return XVTABLE_DEFAULT;
}

static XSshTelnetTestDevice* xshtelnet_test_device_create_ex(XMemoryType memory)
{
    XSshTelnetTestDevice* device = (XSshTelnetTestDevice*)XMemory_malloc(sizeof(XSshTelnetTestDevice), memory);
    if (!device) return NULL;
    memset(device, 0, sizeof(*device));
    XIODevice_init(&device->m_base);
    XClassGetVtable(device) = xshtelnet_test_device_class_init();
    device->output = XByteArray_create();
    Set_Class_Memory(device, memory); Set_Class_IsHeap(device, true);
    if (!device->output || !XIODevice_open_base((XIODevice*)device, XIODevice_ReadWrite)) {
        XClass_delete_base((XClass*)device);
        return NULL;
    }
    return device;
}

static XSshTelnetTestDevice* xshtelnet_test_device_create(void)
{
    return xshtelnet_test_device_create_ex(XCLASS_DEFAULT_MEMORY_TYPE);
}

static size_t xshtelnet_test_output_size(const XSshTelnetTestDevice* device)
{
    return device && device->output ? XByteArray_size_base(device->output) : 0;
}

static const uint8_t* xshtelnet_test_output_data(const XSshTelnetTestDevice* device)
{
    return device && device->output ? (const uint8_t*)XByteArray_constData(device->output) : NULL;
}

static int xshtelnet_server_auth_count;
static int xshtelnet_server_data_count;
static int xshtelnet_server_tab_count;
static size_t xshtelnet_server_line_length;
static size_t xshtelnet_client_data_len;
static uint8_t xshtelnet_client_payload[128];
static bool xshtelnet_host_key_accept;

static void xshtelnet_server_auth(XObject* receiver, XVarList* args)
{
    (void)args;
    ++xshtelnet_server_auth_count;
    XSshServer_setAuthenticateResult((XSshServer*)receiver, true);
}

static void xshtelnet_server_running(XObject* receiver, XVarList* args)
{
    (void)args;
    XSshServer_setIsRunningResult((XSshServer*)receiver, true);
}

static void xshtelnet_server_close_requested(XObject* receiver, XVarList* args)
{
    (void)args;
    XSshServer_setCloseRequestedResult((XSshServer*)receiver, false);
}

static void xshtelnet_server_suppress_prompt(XObject* receiver, XVarList* args)
{
    (void)args;
    XSshServer_setSuppressPromptResult((XSshServer*)receiver, true);
}

static void xshtelnet_server_can_backspace(XObject* receiver, XVarList* args)
{
    (void)args;
    XSshServer_setCanBackspaceResult((XSshServer*)receiver,
                                     xshtelnet_server_line_length > 0);
}

static void xshtelnet_server_data(XObject* receiver, XVarList* args)
{
    const uint8_t* bytes;
    size_t i;
    (void)receiver;
    XVarList_args_2(args, const void*, data, size_t, size);
    if (!data || !size) return;
    ++xshtelnet_server_data_count;
    bytes = (const uint8_t*)data;
    for (i = 0; i < size; ++i) {
        if (bytes[i] == '\t') ++xshtelnet_server_tab_count;
        if (bytes[i] == '\r' || bytes[i] == '\n') {
            xshtelnet_server_line_length = 0;
        } else if (bytes[i] == '\b' || bytes[i] == 0x7f) {
            if (xshtelnet_server_line_length > 0)
                --xshtelnet_server_line_length;
        } else if (bytes[i] >= 0x20 && bytes[i] != '\t') {
            ++xshtelnet_server_line_length;
        }
    }
}

static void xshtelnet_client_host_key(XObject* receiver, XVarList* args)
{
    (void)args;
    xshtelnet_host_key_accept = true;
    XSshClient_setHostKeyAccepted((XSshClient*)receiver, true);
}

static void xshtelnet_client_data(XObject* receiver, XVarList* args)
{
    (void)receiver;
    XVarList_args_2(args, const void*, data, size_t, size);
    if (!data) return;
    {
        size_t copySize = size;
        if (copySize > sizeof(xshtelnet_client_payload) - xshtelnet_client_data_len)
            copySize = sizeof(xshtelnet_client_payload) - xshtelnet_client_data_len;
        memcpy(xshtelnet_client_payload + xshtelnet_client_data_len, data, copySize);
        xshtelnet_client_data_len += copySize;
    }
}

static bool xshtelnet_test_pump(XSshTelnetTestDevice* from, size_t* offset,
                                XProtocolResult (*feed)(void*, const void*, size_t),
                                void* target)
{
    size_t total = xshtelnet_test_output_size(from);
    const uint8_t* data = xshtelnet_test_output_data(from);
    if (!offset || total < *offset) return false;
    if (total == *offset) return true;
    if (feed(target, data + *offset, total - *offset) < XProtocolResult_Ok) return false;
    *offset = total;
    return true;
}

static XProtocolResult xshtelnet_feed_ssh_server(void* target,
                                                 const void* data, size_t size)
{
    return XSshServer_feedData((XSshServer*)target, data, size);
}

static XProtocolResult xshtelnet_feed_ssh_client(void* target,
                                                 const void* data, size_t size)
{
    return XSshClient_feedData((XSshClient*)target, data, size);
}

static bool xshtelnet_test_ssh(void)
{
    XSshTelnetTestDevice* serverDevice = xshtelnet_test_device_create();
    XSshTelnetTestDevice* clientDevice = xshtelnet_test_device_create();
    XSshServer* server = XSshServer_create();
    XSshClient* client = XSshClient_create();
    size_t serverOffset = 0;
    size_t clientOffset = 0;
    bool ready = false;
    unsigned i;
    if (!serverDevice || !clientDevice || !server || !client) goto cleanup;
    XObject_connect_1((XObject*)server, XSignal(XSshServer_authenticateRequested_signal),
                      (XObject*)server, xshtelnet_server_auth, XConnectionType_Direct);
    XObject_connect_1((XObject*)server, XSignal(XSshServer_isRunningRequested_signal),
                      (XObject*)server, xshtelnet_server_running, XConnectionType_Direct);
    XObject_connect_1((XObject*)server, XSignal(XSshServer_closeRequested_signal),
                      (XObject*)server, xshtelnet_server_close_requested, XConnectionType_Direct);
    XObject_connect_1((XObject*)server, XSignal(XSshServer_suppressPromptRequested_signal),
                      (XObject*)server, xshtelnet_server_suppress_prompt, XConnectionType_Direct);
    XObject_connect_1((XObject*)server, XSignal(XSshServer_canBackspaceRequested_signal),
                      (XObject*)server, xshtelnet_server_can_backspace, XConnectionType_Direct);
    XObject_connect_1((XObject*)server, XSignal(XSshServer_bytesReceived_signal),
                      (XObject*)server, xshtelnet_server_data, XConnectionType_Direct);
    XObject_connect_1((XObject*)client, XSignal(XSshClient_hostKeyVerificationRequested_signal),
                      (XObject*)client, xshtelnet_client_host_key, XConnectionType_Direct);
    XObject_connect_1((XObject*)client, XSignal(XSshClient_dataReceived_signal),
                      (XObject*)client, xshtelnet_client_data, XConnectionType_Direct);
    if (!XSshServer_setDevice(server, (XIODevice*)serverDevice) ||
        !XSshClient_setDevice(client, (XIODevice*)clientDevice) ||
        !XSshClient_setCredentials(client, "test", "password") ||
        !XSshServer_start(server) || !XSshClient_start(client)) goto cleanup;
    for (i = 0; i < 200 && !XSshClient_isReady(client); ++i) {
        if (!xshtelnet_test_pump(serverDevice, &serverOffset,
                                 xshtelnet_feed_ssh_client, client) ||
            !xshtelnet_test_pump(clientDevice, &clientOffset,
                                 xshtelnet_feed_ssh_server, server)) goto cleanup;
    }
    ready = XSshClient_isReady(client) && XSshClient_isAuthenticated(client) &&
            xshtelnet_host_key_accept && xshtelnet_server_auth_count == 1;
    if (ready) {
        xshtelnet_client_data_len = 0;
        xshtelnet_server_data_count = 0;
        xshtelnet_server_tab_count = 0;
        if (XSshClient_write(client, "input\n", 6) != 6) ready = false;
        if (ready && !xshtelnet_test_pump(clientDevice, &clientOffset,
                                          xshtelnet_feed_ssh_server, server)) ready = false;
        if (ready && XSshServer_write(server, "output\n", 7) != 7) ready = false;
        if (ready && !xshtelnet_test_pump(serverDevice, &serverOffset,
                                          xshtelnet_feed_ssh_client, client)) ready = false;
        ready = ready && xshtelnet_server_data_count > 0 &&
                xshtelnet_client_data_len == 15 &&
                memcmp(xshtelnet_client_payload, "input\r\noutput\r\n", 15) == 0;
    }
    if (ready) {
        /* SSH 客户端会把一个终端按键拆成多个 SSH 数据包；回显过滤器
           必须跨包保留 ESC/CSI 状态，Tab 也只能交给 Shell 补全，不能
           在终端上形成不可见的空白位移。 */
        static const uint8_t terminalInput[] = {
            0x1b, '[', 'A', 0x1b, '[', 'B', 0x1b, '[', 'C', 0x1b, '[', 'D',
            0x1b, 'O', 'A', 0x1b, 'O', 'B',
            0x1b, '[', '<', '6', '4', ';', '1', '0', ';', '1', '0', 'M', '\t'
        };
        xshtelnet_client_data_len = 0;
        xshtelnet_server_data_count = 0;
        memset(xshtelnet_client_payload, 0, sizeof(xshtelnet_client_payload));
        for (i = 0; i < sizeof(terminalInput) && ready; ++i) {
            ready = XSshClient_write(client, terminalInput + i, 1) == 1;
            if (ready) ready = xshtelnet_test_pump(clientDevice, &clientOffset,
                                                   xshtelnet_feed_ssh_server,
                                                   server);
            if (ready) ready = xshtelnet_test_pump(serverDevice, &serverOffset,
                                                   xshtelnet_feed_ssh_client,
                                                   client);
        }
        ready = ready && xshtelnet_server_data_count > 0 &&
                xshtelnet_server_tab_count == 1 &&
                xshtelnet_client_data_len == 0;
    }
    if (ready) {
        /* 空输入行收到退格时，服务端不能把 Shell 提示符当作输入擦除；
           `a<退格>` 合并在同一 SSH 数据包时，查询还必须发生在 a 已投喂之后。 */
        xshtelnet_client_data_len = 0;
        xshtelnet_server_line_length = 0;
        memset(xshtelnet_client_payload, 0, sizeof(xshtelnet_client_payload));
        if (XSshClient_write(client, "\x7f", 1) != 1 ||
            !xshtelnet_test_pump(clientDevice, &clientOffset,
                                 xshtelnet_feed_ssh_server, server) ||
            !xshtelnet_test_pump(serverDevice, &serverOffset,
                                 xshtelnet_feed_ssh_client, client) ||
            xshtelnet_client_data_len != 0)
            ready = false;
    }
    if (ready) {
        xshtelnet_client_data_len = 0;
        xshtelnet_server_line_length = 0;
        memset(xshtelnet_client_payload, 0, sizeof(xshtelnet_client_payload));
        if (XSshClient_write(client, "a\x7f", 2) != 2 ||
            !xshtelnet_test_pump(clientDevice, &clientOffset,
                                 xshtelnet_feed_ssh_server, server) ||
            !xshtelnet_test_pump(serverDevice, &serverOffset,
                                 xshtelnet_feed_ssh_client, client) ||
            xshtelnet_client_data_len != 4 ||
            memcmp(xshtelnet_client_payload, "a\b \b", 4) != 0 ||
            xshtelnet_server_line_length != 0)
            ready = false;
    }
    if (ready) {
        /* 全屏编辑器期间即使收到普通字符也不能由 SSH 服务端回显；
           Vim 的屏幕刷新是唯一允许把内容写回终端的路径。 */
        xshtelnet_client_data_len = 0;
        if (!XSshServer_setInputEcho(server, false) ||
            XSshClient_write(client, "abc", 3) != 3 ||
            !xshtelnet_test_pump(clientDevice, &clientOffset,
                                 xshtelnet_feed_ssh_server, server) ||
            !xshtelnet_test_pump(serverDevice, &serverOffset,
                                 xshtelnet_feed_ssh_client, client) ||
            xshtelnet_client_data_len != 0)
            ready = false;
        (void)XSshServer_setInputEcho(server, true);
    }
cleanup:
    if (client) XClass_delete_base((XClass*)client);
    if (server) XClass_delete_base((XClass*)server);
    if (clientDevice) XClass_delete_base((XClass*)clientDevice);
    if (serverDevice) XClass_delete_base((XClass*)serverDevice);
    return ready;
}

static int xshtelnet_client_data_count;
static char xshtelnet_telnet_text[64];
static size_t xshtelnet_telnet_text_len;
static size_t xshtelnet_telnet_server_data_len;
static size_t xshtelnet_telnet_server_line_length;
static int xshtelnet_telnet_server_tab_count;

static void xshtelnet_telnet_data(XObject* receiver, XVarList* args)
{
    (void)receiver;
    XVarList_args_2(args, const void*, data, size_t, size);
    ++xshtelnet_client_data_count;
    if (data && size <= sizeof(xshtelnet_telnet_text) - xshtelnet_telnet_text_len) {
        memcpy(xshtelnet_telnet_text + xshtelnet_telnet_text_len, data, size);
        xshtelnet_telnet_text_len += size;
    }
}

static void xshtelnet_telnet_server_data(XObject* receiver, XVarList* args)
{
    const uint8_t* bytes;
    size_t i;
    XVarList_args_2(args, const void*, data, size_t, size);
    if (data) {
        xshtelnet_telnet_server_data_len += size;
        bytes = (const uint8_t*)data;
        for (i = 0; i < size; ++i) {
            if (bytes[i] == '\t') ++xshtelnet_telnet_server_tab_count;
            if (bytes[i] == '\r' || bytes[i] == '\n') {
                xshtelnet_telnet_server_line_length = 0;
            } else if (bytes[i] == '\b' || bytes[i] == 0x7f) {
                if (xshtelnet_telnet_server_line_length > 0)
                    --xshtelnet_telnet_server_line_length;
            } else if (bytes[i] >= 0x20 && bytes[i] != '\t') {
                ++xshtelnet_telnet_server_line_length;
            }
        }
    }
    XTelnetServer_setBytesReceivedResult((XTelnetServer*)receiver,
                                         (int)XProtocolResult_Ok);
}

static void xshtelnet_telnet_server_can_backspace(XObject* receiver,
                                                  XVarList* args)
{
    (void)args;
    XTelnetServer_setCanBackspaceResult(
        (XTelnetServer*)receiver, xshtelnet_telnet_server_line_length > 0);
}

static bool xshtelnet_test_telnet_server_echo(void)
{
    XSshTelnetTestDevice* device = xshtelnet_test_device_create();
    XTelnetServer* server = XTelnetServer_create();
    static const uint8_t terminalInput[] = {
        0x1b, '[', 'A', 0x1b, '[', 'B', 0x1b, 'O', 'C',
        0x1b, '[', '<', '6', '4', ';', '1', ';', '1', 'M', '\t'
    };
    size_t outputBefore;
    size_t i;
    bool ok = false;
    if (!device || !server) goto cleanup;
    XObject_connect_1((XObject*)server,
                      XSignal(XTelnetServer_bytesReceived_signal),
                      (XObject*)server, xshtelnet_telnet_server_data,
                      XConnectionType_Direct);
    XObject_connect_1((XObject*)server,
                      XSignal(XTelnetServer_canBackspaceRequested_signal),
                      (XObject*)server, xshtelnet_telnet_server_can_backspace,
                      XConnectionType_Direct);
    if (!XTelnetServer_setDevice(server, (XIODevice*)device) ||
        !XTelnetServer_start(server))
        goto cleanup;
    outputBefore = xshtelnet_test_output_size(device);
    xshtelnet_telnet_server_data_len = 0;
    xshtelnet_telnet_server_line_length = 0;
    xshtelnet_telnet_server_tab_count = 0;
    for (i = 0; i < sizeof(terminalInput); ++i) {
        if (XTelnetServer_feedData(server, terminalInput + i, 1) !=
            XProtocolResult_Ok)
            goto cleanup;
    }
    ok = xshtelnet_telnet_server_data_len == sizeof(terminalInput) &&
         xshtelnet_telnet_server_tab_count == 1 &&
         xshtelnet_test_output_size(device) == outputBefore;
    if (ok) {
        /* 空行退格不应产生任何设备输出；已有字符时才输出擦除序列。 */
        xshtelnet_telnet_server_line_length = 0;
        if (XTelnetServer_feedData(server, "\x7f", 1) != XProtocolResult_Ok ||
            xshtelnet_test_output_size(device) != outputBefore)
            ok = false;
    }
    if (ok) {
        xshtelnet_telnet_server_line_length = 0;
        if (XTelnetServer_feedData(server, "a\x7f", 2) != XProtocolResult_Ok ||
            xshtelnet_test_output_size(device) != outputBefore + 4 ||
            memcmp(xshtelnet_test_output_data(device) + outputBefore,
                   "a\b \b", 4) != 0 ||
            xshtelnet_telnet_server_line_length != 0)
            ok = false;
    }
cleanup:
    if (server) XClass_delete_base((XClass*)server);
    if (device) XClass_delete_base((XClass*)device);
    return ok;
}

static bool xshtelnet_test_telnet(void)
{
    XSshTelnetTestDevice* device = xshtelnet_test_device_create();
    XTelnetClient* client = XTelnetClient_create();
    const uint8_t inbound[] = { 255, 251, 1, 255, 251, 3,
                                'h', 'i', '\r', '\n', 'x' };
    const uint8_t* output;
    size_t outputLen;
    bool ok = false;
    if (!device || !client) goto cleanup;
    xshtelnet_client_data_count = 0;
    xshtelnet_telnet_text_len = 0;
    memset(xshtelnet_telnet_text, 0, sizeof(xshtelnet_telnet_text));
    XObject_connect_1((XObject*)client, XSignal(XTelnetClient_dataReceived_signal),
                      (XObject*)client, xshtelnet_telnet_data, XConnectionType_Direct);
    if (!XTelnetClient_setDevice(client, (XIODevice*)device) ||
        !XTelnetClient_start(client) ||
        XTelnetClient_feedData(client, inbound, sizeof(inbound)) != XProtocolResult_Ok ||
        XTelnetClient_localEchoEnabled(client)) goto cleanup;
    if (XTelnetClient_write(client, "a\n\xff", 3) != 3) goto cleanup;
    output = xshtelnet_test_output_data(device);
    outputLen = xshtelnet_test_output_size(device);
    if (outputLen < 5 || memcmp(output + outputLen - 5,
                                (const uint8_t*)"a\r\n\xff\xff", 5) != 0) goto cleanup;
    ok = xshtelnet_client_data_count >= 3 && xshtelnet_telnet_text_len == 4 &&
         memcmp(xshtelnet_telnet_text, "hi\nx", 4) == 0;
cleanup:
    if (client) XClass_delete_base((XClass*)client);
    if (device) XClass_delete_base((XClass*)device);
    return ok;
}

bool XSshTelnetClientTest_runAll(void)
{
    bool ssh = xshtelnet_test_ssh();
    bool telnet = xshtelnet_test_telnet();
    bool telnetServer = xshtelnet_test_telnet_server_echo();
    XPrintf("SSH 客户端内存端到端测试: %s\n", ssh ? "通过" : "失败");
    XPrintf("Telnet 客户端协议测试: %s\n", telnet ? "通过" : "失败");
    XPrintf("Telnet 服务端终端回显测试: %s\n",
            telnetServer ? "通过" : "失败");
    return ssh && telnet && telnetServer;
}
