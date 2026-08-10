#include "XSshTelnetClientTest.h"
#include "XSshClient.h"
#include "XSshServer.h"
#include "XTelnetClient.h"
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

static XSshTelnetTestDevice* xshtelnet_test_device_create(void)
{
    XSshTelnetTestDevice* device = (XSshTelnetTestDevice*)XMalloc_System(sizeof(*device));
    if (!device) return NULL;
    memset(device, 0, sizeof(*device));
    XIODevice_init(&device->m_base);
    XClassGetVtable(device) = xshtelnet_test_device_class_init();
    device->output = XByteArray_create();
    Set_Class_MemoryFree(device, XFree_System);
    if (!device->output || !XIODevice_open_base((XIODevice*)device, XIODevice_ReadWrite)) {
        XClass_delete_base((XClass*)device);
        return NULL;
    }
    return device;
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

static void xshtelnet_server_data(XObject* receiver, XVarList* args)
{
    (void)receiver;
    XVarList_args_2(args, const void*, data, size_t, size);
    if (data && size) ++xshtelnet_server_data_count;
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
    XPrintf("SSH 客户端内存端到端测试: %s\n", ssh ? "通过" : "失败");
    XPrintf("Telnet 客户端协议测试: %s\n", telnet ? "通过" : "失败");
    return ssh && telnet;
}
