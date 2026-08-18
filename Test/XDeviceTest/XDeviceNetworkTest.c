#include "XDeviceNetworkTest.h"
#include "XDeviceNetwork.h"
#include "XDevice.h"
#include "XHostAddress.h"
#include "XIODevice.h"
#include "XTcpServer.h"
#include "XUdpSocket.h"
#include "XString.h"
#include "XVariant.h"
#include "XVarList.h"
#include <stdio.h>
#include <string.h>

bool XDeviceNetworkTest_runAll(void)
{
#if !XNETWORK_ON || !XNETWORK_ABSTRACT_SOCKET_ON
    puts("XDeviceNetwork test: SKIP");
    return true;
#else
    XHostAddress address;
    XDeviceNetworkOpenOptions options;
    XDeviceNetworkOpenOptions senderOptions;
    XVariant value;
    XDeviceContext* context = NULL;
    XVarList* pollResult = NULL;
    XFd fd = XFD_INVALID;
    XFd senderFd = XFD_INVALID;
    XString* loopback = NULL;
#if XNETWORK_TCPSERVER_ON
    XTcpServer server;
    bool serverInitialized = false;
#endif
#if XNETWORK_UDPSOCKET_ON
    XUdpSocket highLevelReceiver;
    XUdpSocket highLevelSender;
    bool highLevelReceiverInitialized = false;
    bool highLevelSenderInitialized = false;
#endif
    int error = 0;
    uint16_t receiverPort;
    static const char packet[] = "ping";
    bool ok = false;

    memset(&value, 0, sizeof(value));
    XVariant_init(&value, NULL, 0, XVariantType_NULL);
    XHostAddress_init(&address);
    XHostAddress_setAddressSpecial(&address, XHostAddress_LocalHostSpecial);
    memset(&options, 0, sizeof(options));
    options.m_base.m_openMode = XIODevice_ReadWrite;
    options.m_socketType = XDeviceNetwork_Udp;
    options.m_protocol = XDeviceNetwork_IPv4;
    options.m_operation = XDeviceNetworkOpen_Bind;
    options.m_address = &address;
    options.m_reuseAddress = true;
    options.m_shareAddress = true;

    fd = XDevice_open(XDeviceType_Socket, &options.m_base, &error);
    if (fd == XFD_INVALID || error != XDeviceError_None)
        goto cleanup;
    context = XDevice_handle(fd);
    if (!context || context->m_fd != fd || XFd_handle(fd) != context ||
        XFd_type(fd) != XFD_TYPE_CLASS)
        goto cleanup;

    if (!XDevice_queryProperty(fd, (XDeviceProperty)XDeviceNetworkProperty_SocketType, &value) ||
        XVariant_toInt(&value) != XDeviceNetwork_Udp)
        goto cleanup;
    XVariant_setValue_int(&value, XDeviceState_Active);
    if (!XDevice_queryProperty(fd, XDeviceProperty_State, &value) ||
        XVariant_toInt(&value) != XDeviceState_Active)
        goto cleanup;
    XVariant_setValue_int(&value, XDeviceIoMode_Sync);
    if (!XDevice_getProperty(fd, XDeviceProperty_IoMode, &value) ||
        XVariant_toInt(&value) != XDeviceIoMode_Async)
        goto cleanup;
    XVariant_setValue_bool(&value, false);
    if (!XDevice_getProperty(fd, (XDeviceProperty)XDeviceNetworkProperty_Connected, &value) ||
        !XVariant_toBool(&value))
        goto cleanup;
    {
        bool connected = false;
        pollResult = XVarList_Create(XVar(bool, connected));
        if (!pollResult || !XDevice_control(fd, XDeviceCommand_Poll, NULL, pollResult))
            goto cleanup;
        XVarList_start(pollResult);
        connected = XVarList_arg(pollResult, bool);
        if (!connected)
            goto cleanup;
        XVarList_delete(pollResult);
        pollResult = NULL;
    }
    XVariant_setValue_int(&value, 0);
    if (!XDevice_getProperty(fd, (XDeviceProperty)XDeviceNetworkProperty_LocalPort, &value) ||
        XVariant_toInt(&value) == 0)
        goto cleanup;
    XVariant_setValue_ptr(&value, NULL);
    if (!XDevice_getProperty(fd, XDeviceProperty_NativeHandle, &value))
        goto cleanup;
#if !defined(XNETWORK_USE_LWIP)
    /* lwIP exposes a valid XFd table index; index 0 is represented as NULL by
     * the legacy pointer-valued property and must not be treated as failure. */
    if (XVariant_toPtr(&value) == NULL)
        goto cleanup;
#endif

#if XNETWORK_UDPSOCKET_ON
    XUdpSocket_init(&highLevelReceiver);
    highLevelReceiverInitialized = true;
    XUdpSocket_init(&highLevelSender);
    highLevelSenderInitialized = true;
    if (!XUdpSocket_bind_base(&highLevelReceiver.base, &address, 0, XAbstractSocket_DefaultForPlatform) ||
        !XUdpSocket_bind_base(&highLevelSender.base, &address, 0, XAbstractSocket_DefaultForPlatform) ||
        highLevelReceiver.base.m_deviceFd == XFD_INVALID ||
        highLevelSender.base.m_deviceFd == XFD_INVALID ||
        XUdpSocket_writeDatagram(&highLevelSender, packet, (int64_t)strlen(packet), &address,
                                  XAbstractSocket_localPort(&highLevelReceiver.base)) != (int64_t)strlen(packet))
        goto cleanup;
    XClass_deinit_base((XClass*)&highLevelSender);
    highLevelSenderInitialized = false;
    XClass_deinit_base((XClass*)&highLevelReceiver);
    highLevelReceiverInitialized = false;
#endif
    XVariant_setValue_int(&value, 0);
    if (!XDevice_getProperty(fd, (XDeviceProperty)XDeviceNetworkProperty_LocalPort, &value))
        goto cleanup;
    receiverPort = (uint16_t)XVariant_toInt(&value);
    if (receiverPort == 0)
        goto cleanup;

    memset(&senderOptions, 0, sizeof(senderOptions));
    loopback = XString_create_utf8("127.0.0.1");
    if (!loopback)
        goto cleanup;
    senderOptions.m_base.m_openMode = XIODevice_ReadWrite;
    senderOptions.m_socketType = XDeviceNetwork_Udp;
    senderOptions.m_protocol = XDeviceNetwork_IPv4;
    senderOptions.m_operation = XDeviceNetworkOpen_Connect;
    senderOptions.m_base.m_target = loopback;
    senderOptions.m_peerAddress = &address;
    senderOptions.m_peerPort = receiverPort;
    senderFd = XDevice_open(XDeviceType_Socket, &senderOptions.m_base, &error);
    if (senderFd == XFD_INVALID || error != XDeviceError_None)
        goto cleanup;
    if (XDevice_write(senderFd, packet, (int64_t)strlen(packet)) != (int64_t)strlen(packet))
        goto cleanup;
    XVariant_setValue_int64(&value, 4096);
    if (!XDevice_setProperty(fd, (XDeviceProperty)XDeviceNetworkProperty_ReadBufferSize,
                             &value))
        goto cleanup;
    XVariant_setValue_int64(&value, 0);
    if (!XDevice_getProperty(fd, (XDeviceProperty)XDeviceNetworkProperty_ReadBufferSize,
                             &value) || XVariant_toInt64(&value) != 4096)
        goto cleanup;
    if (!XDevice_control(fd, XDeviceNetworkCommand_ContinueRead, NULL, NULL))
        goto cleanup;
    if (!XDevice_flush(fd))
        goto cleanup;
    XVariant_setValue_int(&value, receiverPort);
    if (!XDevice_setProperty(senderFd, (XDeviceProperty)XDeviceNetworkProperty_PeerPort, &value))
        goto cleanup;
    if (!XDevice_control(fd, XDeviceCommand_Cancel, NULL, NULL))
        goto cleanup;

#if XNETWORK_TCPSERVER_ON
    XTcpServer_init(&server);
    serverInitialized = true;
    if (!XTcpServer_listen(&server, &address, 0) ||
        XTcpServer_serverPort(&server) == 0 ||
        XTcpServer_socketDescriptor(&server) < 0)
        goto cleanup;
    XTcpServer_close(&server);
    if (XTcpServer_isListening(&server))
        goto cleanup;
    XClass_deinit_base((XClass*)&server);
    serverInitialized = false;
#endif

    ok = true;

cleanup:
    if (pollResult) XVarList_delete(pollResult);
#if XNETWORK_UDPSOCKET_ON
    if (highLevelSenderInitialized) XClass_deinit_base((XClass*)&highLevelSender);
    if (highLevelReceiverInitialized) XClass_deinit_base((XClass*)&highLevelReceiver);
#endif
#if XNETWORK_TCPSERVER_ON
    if (serverInitialized) XClass_deinit_base((XClass*)&server);
#endif
    if (senderFd != XFD_INVALID) XDevice_close(senderFd);
    if (fd != XFD_INVALID) XDevice_close(fd);
    if (loopback) XClass_delete_base((XClass*)loopback);
    XVariant_deinit_base((XClass*)&value);
    XHostAddress_deinit_base((XClass*)&address);
    puts(ok ? "XDeviceNetwork test: PASS" : "XDeviceNetwork test: FAIL");
    return ok;
#endif
}
