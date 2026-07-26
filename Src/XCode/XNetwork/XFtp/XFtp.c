/**
 * @file        XFtp.c
 * @brief       FTP 客户端实现（对标 Qt 6.8 QFtp）
 * @note        基于 XinYueC 信号/槽系统；事件由 XAbstractSocket 内部触发
 */

#include "XFtp.h"
#include "XMemory.h"
#include "XFile.h"
#include "XDir.h"
#include "XObject.h"
#include "XThread.h"
#include "XTcpServer.h"
#include "XHostAddress.h"
#include "XSslSocket.h"
#include "XNetworkProxy.h"
#include "XUrl.h"
#include "XCoreApplication.h"
#include "XFileInfo.h"
#include "XFtpCommand.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include <time.h>

// 自实现跨平台大小写不敏感字符串比较（避免依赖 strncasecmp/_strnicmp 等 platform API）
static int xftp_stricmp_n(const char* a, const char* b, size_t n)
{
    if (!a || !b) return (int)(intptr_t)a - (int)(intptr_t)b;
    for (size_t i = 0; i < n; i++) {
        unsigned char ca = (unsigned char)a[i];
        unsigned char cb = (unsigned char)b[i];
        int la = (ca >= 'A' && ca <= 'Z') ? (ca + 32) : ca;
        int lb = (cb >= 'A' && cb <= 'Z') ? (cb + 32) : cb;
        if (la != lb) return la - lb;
        if (la == 0) return 0;  // 双双到 '\0'
    }
    return 0;
}

#if defined(XNETWORK_ON) || 1

// =============== 私有结构体（位域+union 优化） ===============
typedef struct XFtpPrivate {
    // 内部标志（位域 1 字节）
    uint8_t m_waitForDtpToConnect : 1;  // 等待 DTP 连接
    uint8_t m_waitForDtpToClose   : 1;  // 等待 DTP 关闭
    uint8_t m_rawCommand          : 1;  // 当前为 rawCommand
    uint8_t m_append              : 1;  // append 模式
    uint8_t m_waitForGreeting     : 1;  // 等待服务器 220 问候语（ConnectToHost 未完成）
    uint8_t m_piAckedTransfer     : 1;  // PI 已 ACK 226/227，但 DTP 数据可能未读完；等 read=0 统一发 commandFinished
    uint8_t m_reserved            : 2;

    // 带外响应计数（FEAT/OPTS/MODE Z 的应答不属于命令队列，需单独消化）
    uint8_t m_outOfBandPending;

    // 主动模式(PORT)握手计数：发出 TYPE + PORT 后期望 2 个 2xx 应答，
    // 收齐后才发数据命令（RETR/STOR/LIST），避免"等连接才发命令、服务器等命令才连接"的死锁
    uint8_t m_activeAcks;

    // 互斥的状态机共享 m_sm
    union {
        uint8_t m_dtpState;             // DTP 连接状态
        struct {
            XString* m_dtpHost;
            uint16_t m_dtpPort;
            uint16_t _pad;
        } m_dtpTarget;
    };
    struct {
        uint8_t m_sslState;             // 0=off,1=AUTH,2=handshake,3=PBSZ,4=PROT,5=ready
        uint8_t m_restState;            // REST 状态机 0/1/2
        uint8_t m_mlstState;            // MLST 状态机 0=idle, 1=receiving 250-.../facts, 2=expecting 250 End
    } m_sm;

    // 完整响应文本
    XString* m_replyText;

    // 命令队列
    XVector* m_pendingRawCmds;          // 备用的 char* 列表（兼容旧调用）

    // 数据传输设备
    void* m_transferDevice;

    // Put 分块写偏移（已提交到 socket 的字节数，区别于 m_transferCurrent 的"已确认"）
    int64_t m_putWriteOffset;
} XFtpPrivate;

// =============== 静态变量 ===============
static bool s_classInitialized = false;
static XVtable* s_vtable = NULL;

/* XVector_at_base 返回的是元素存储位置的地址，需要再解一层拿到实际值。
 * 这里定义几个辅助宏，把 vector 中第 i 个元素（T*）正确解出来。 */
#define XVEC_POP_FIRST(vec, T) \
    ((T*)XVector_at_base((vec), 0) ? *(T**)XVector_at_base((vec), 0) : (T*)NULL)
#define XVEC_GET(vec, i, T) \
    (*(T**)XVector_at_base((vec), (i)))
#define XVEC_STR_AT(vec, i) \
    (*(const char**)XVector_at_base((vec), (i)))

// =============== 静态函数声明 ===============
static void VXFtp_deinit(XFtp* ftp);

static void ftp_pi_connected_handler(XObject* receiver, XVarList* args);
static void ftp_pi_disconnected_handler(XObject* receiver, XVarList* args);
static void ftp_pi_error_handler(XObject* receiver, XVarList* args);
static void ftp_pi_readyRead_handler(XObject* receiver, XVarList* args);
static void ftp_pi_ssl_encrypted_handler(XObject* receiver, XVarList* args);

static void ftp_dtp_connected_handler(XObject* receiver, XVarList* args);
static void ftp_dtp_disconnected_handler(XObject* receiver, XVarList* args);
static void ftp_dtp_error_handler(XObject* receiver, XVarList* args);
static void ftp_dtp_readyRead_handler(XObject* receiver, XVarList* args);
static void ftp_dtp_bytesWritten_handler(XObject* receiver, XVarList* args);
static void ftp_dtp_server_newConnection_handler(XObject* receiver, XVarList* args);
static void ftp_dtp_ssl_encrypted_handler(XObject* receiver, XVarList* args);

static void ftp_pi_sendCommand(XFtp* ftp, const char* cmd);

static void ftp_pi_sendCommand(XFtp* ftp, const char* cmd)
{
    if (!ftp || !cmd || !ftp->m_piSocket) return;
    int64_t len = (int64_t)strlen(cmd);
    XAbstractSocket_write(ftp->m_piSocket, cmd, len);
}


static void ftp_pi_startNextCommand(XFtp* ftp);
static void ftp_pi_finishCommand(XFtp* ftp, int code, const char* text);
static void ftp_pi_handleIntermediateReply(XFtp* ftp, int code, const char* text);
static void ftp_pi_handleError(XFtp* ftp, int code, const char* text);
static void ftp_processReply(XFtp* ftp, const char* reply);

static void ftp_dtp_connect(XFtp* ftp, const char* host, uint16_t port);
static void ftp_dtp_startTransfer(XFtp* ftp);
static void ftp_dtp_finishTransfer(XFtp* ftp);
static int  ftp_dtp_startListen(XFtp* ftp, char* portCmd, size_t portCmdSize);

static void ftp_connectPiSocketSignals(XFtp* ftp);
static void ftp_connectDtpSocketSignals(XFtp* ftp);

static void ftp_parse_list_line(XFtp* ftp, const char* line);
static XFtpCommand_Type ftp_get_command_type(const char* name);

// 工具：移除字符串尾的 \r\n
static void trim_crlf(char* s)
{
    if (!s) return;
    size_t len = strlen(s);
    while (len > 0 && (s[len-1] == '\r' || s[len-1] == '\n' || s[len-1] == ' ')) {
        s[--len] = '\0';
    }
}

// =============== Vtable / class init ===============
XVtable* XFtp_class_init(void)
{
    if (s_classInitialized && s_vtable) return s_vtable;
    // 简化的 vtable 初始化：直接继承 XObject
    XVtable_init(s_vtable = XVtable_create());
    if (s_vtable) {
        XVtable_append_vtable(s_vtable, XObject_class_init());
        XVTABLE_OVERLOAD(s_vtable, EXClass_Deinit, VXFtp_deinit);
    }
    s_classInitialized = true;
    return s_vtable;
}

// =============== 构造/析构 ===============

void XFtp_init(XFtp* ftp)
{
    if (!ftp) return;
    memset(ftp, 0, sizeof(XFtp));
    XObject_init((XObject*)ftp);
    XClassSetVtable(ftp, XFtp);

    ftp->m_d = (XFtpPrivate*)XMemory_calloc(1, sizeof(XFtpPrivate), XMEMORY_TYPE_SYSTEM);
    if (ftp->m_d) {
        ftp->m_d->m_replyText = XString_create();
        ftp->m_d->m_pendingRawCmds = XVector_Create(const char*);
    }
    ftp->m_piBuffer = XByteArray_create();
    ftp->m_dtpBuffer = XByteArray_create();
    ftp->m_readBuffer = XByteArray_create();
    ftp->m_pendingCommands = XVector_Create(XFtpCommand*);
    ftp->m_commandMutex = XMutex_create(XLock_NonRecursive);
    ftp->m_errorString = XString_create();
    ftp->m_listInfo = XVector_Create(XFileInfo*);
    // 默认状态
    ftp->m_state = XFtp_State_Unconnected;
    ftp->m_error = XFtp_Error_NoError;
    ftp->m_transferMode = XFtp_TransferMode_Passive;
    ftp->m_transferType = XFtp_DataType_Binary;
    ftp->m_proxyType = XFtp_ProxyType_None;
    ftp->m_port = 21;
    ftp->m_reconnectInterval = 5000;
    ftp->m_maxReconnectAttempts = 3;
    ftp->m_reconnectAttempts = 0;
    ftp->m_currentId = 0;
    ftp->m_features = 0;
    ftp->m_useMlsd = 1;        // 默认优先 MLSD（如果服务器支持）
    ftp->m_piSocket = (XAbstractSocket*)XSslSocket_create();
    if (ftp->m_piSocket) ftp_connectPiSocketSignals(ftp);
}

void XFtp_deinit_base(XFtp* ftp)
{
    if (!ftp) return;
    VXFtp_deinit(ftp);
}

XFtp* XFtp_create(void)
{
    XFtp* ftp = (XFtp*)XMalloc(sizeof(XFtp), XMEMORY_TYPE_SYSTEM);
    if (!ftp) return NULL;
    XFtp_init(ftp);
    Set_Class_MemoryFree(ftp, XFree_System);
    if (!ftp->m_d || !ftp->m_piSocket || !ftp->m_pendingCommands || !ftp->m_commandMutex) {
        XClass_delete_base((XClass*)ftp);
        return NULL;
    }
    return ftp;
}

void XFtp_delete(XFtp* ftp)
{
    if (!ftp) return;
    XClass_delete_base((XClass*)ftp);
}

static void VXFtp_deinit(XFtp* ftp)
{
    if (!ftp) return;

    // 先断开信号（避免回调到已释放的对象）
    if (ftp->m_piSocket) {
        // 信号会在对象析构时由信号系统自动断开
    }
    if (ftp->m_dtpSocket) {
        // 信号会在对象析构时由信号系统自动断开
    }
    if (ftp->m_dtpServer) {
        // 信号会在对象析构时由信号系统自动断开
    }

    // 关闭 socket
    if (ftp->m_piSocket) {
        XClass_delete_base((XClass*)ftp->m_piSocket);
        ftp->m_piSocket = NULL;
    }
    if (ftp->m_dtpSocket) {
        XClass_delete_base((XClass*)ftp->m_dtpSocket);
        ftp->m_dtpSocket = NULL;
    }
    if (ftp->m_dtpServer) {
        XTcpServer_close(ftp->m_dtpServer);
        XClass_delete_base((XClass*)ftp->m_dtpServer);
        ftp->m_dtpServer = NULL;
    }

    // 释放缓冲
    if (ftp->m_piBuffer) {
        XClass_delete_base((XClass*)ftp->m_piBuffer);
        ftp->m_piBuffer = NULL;
    }
    if (ftp->m_dtpBuffer) {
        XClass_delete_base((XClass*)ftp->m_dtpBuffer);
        ftp->m_dtpBuffer = NULL;
    }
    if (ftp->m_readBuffer) {
        XClass_delete_base((XClass*)ftp->m_readBuffer);
        ftp->m_readBuffer = NULL;
    }

    // 释放命令队列
    if (ftp->m_pendingCommands) {
        for (size_t i = 0; i < XVector_size_base(ftp->m_pendingCommands); i++) {
            XFtpCommand* cmd = XVEC_GET(ftp->m_pendingCommands, i, XFtpCommand);
            if (cmd) XFtpCommand_delete(cmd);
        }
        XClass_delete_base((XClass*)ftp->m_pendingCommands);
        ftp->m_pendingCommands = NULL;
    }

    // 释放互斥
    if (ftp->m_commandMutex) {
        XClass_delete_base((XClass*)ftp->m_commandMutex);
        ftp->m_commandMutex = NULL;
    }

    // 释放字符串
    if (ftp->m_host) { XClass_delete_base((XClass*)ftp->m_host); ftp->m_host = NULL; }
    if (ftp->m_user) { XClass_delete_base((XClass*)ftp->m_user); ftp->m_user = NULL; }
    if (ftp->m_password) { XClass_delete_base((XClass*)ftp->m_password); ftp->m_password = NULL; }
    if (ftp->m_proxyHost) { XClass_delete_base((XClass*)ftp->m_proxyHost); ftp->m_proxyHost = NULL; }
    if (ftp->m_proxyUser) { XClass_delete_base((XClass*)ftp->m_proxyUser); ftp->m_proxyUser = NULL; }
    if (ftp->m_proxyPass) { XClass_delete_base((XClass*)ftp->m_proxyPass); ftp->m_proxyPass = NULL; }
    if (ftp->m_errorString) { XClass_delete_base((XClass*)ftp->m_errorString); ftp->m_errorString = NULL; }

    // 释放列表
    if (ftp->m_listInfo) {
        for (size_t i = 0; i < XVector_size_base(ftp->m_listInfo); i++) {
            XFileInfo* info = *(XFileInfo**)XVector_at_base(ftp->m_listInfo, i);
            if (info) XClass_delete_base((XClass*)info);
        }
        XClass_delete_base((XClass*)ftp->m_listInfo);
        ftp->m_listInfo = NULL;
    }

    // 释放私有数据
    if (ftp->m_d) {
        if (ftp->m_d->m_replyText) {
            XClass_delete_base((XClass*)ftp->m_d->m_replyText);
        }
        if (ftp->m_d->m_pendingRawCmds) {
            for (size_t i = 0; i < XVector_size_base(ftp->m_d->m_pendingRawCmds); i++) {
                char* s = *(char**)XVector_at_base(ftp->m_d->m_pendingRawCmds, i);
                if (s) XFree(s, XMEMORY_TYPE_SYSTEM);
            }
            XClass_delete_base((XClass*)ftp->m_d->m_pendingRawCmds);
        }
        // DTP target 字符串
        if (ftp->m_d->m_dtpTarget.m_dtpHost) {
            XClass_delete_base((XClass*)ftp->m_d->m_dtpTarget.m_dtpHost);
        }
        XMemory_free(ftp->m_d, XMEMORY_TYPE_SYSTEM);
        ftp->m_d = NULL;
    }

    ftp->m_currentCommand = NULL;
    ftp->m_transferDevice = NULL;
    XClass_Deinit_Parent(XObject, ftp);
}

// =============== 信号连接 ===============
static void ftp_connectPiSocketSignals(XFtp* ftp)
{
    if (!ftp || !ftp->m_piSocket) return;
    XObject_connect_1((XObject*)ftp->m_piSocket, XAbstractSocket_connected_signal,
                      (XObject*)ftp, ftp_pi_connected_handler, XConnectionType_Direct);
    XObject_connect_1((XObject*)ftp->m_piSocket, XAbstractSocket_disconnected_signal,
                      (XObject*)ftp, ftp_pi_disconnected_handler, XConnectionType_Direct);
    XObject_connect_1((XObject*)ftp->m_piSocket, XAbstractSocket_errorOccurred_signal,
                      (XObject*)ftp, ftp_pi_error_handler, XConnectionType_Direct);
    XObject_connect_1((XObject*)ftp->m_piSocket, XIODevice_readyRead_signal,
                      (XObject*)ftp, ftp_pi_readyRead_handler, XConnectionType_Direct);

    XObject_connect_1((XObject*)ftp->m_piSocket, XSslSocket_encrypted_signal,
                      (XObject*)ftp, ftp_pi_ssl_encrypted_handler, XConnectionType_Direct);
}

static void ftp_connectDtpSocketSignals(XFtp* ftp)
{
    if (!ftp || !ftp->m_dtpSocket) return;
    XObject_connect_1((XObject*)ftp->m_dtpSocket, XAbstractSocket_connected_signal,
                      (XObject*)ftp, ftp_dtp_connected_handler, XConnectionType_Direct);
    XObject_connect_1((XObject*)ftp->m_dtpSocket, XAbstractSocket_disconnected_signal,
                      (XObject*)ftp, ftp_dtp_disconnected_handler, XConnectionType_Direct);
    XObject_connect_1((XObject*)ftp->m_dtpSocket, XAbstractSocket_errorOccurred_signal,
                      (XObject*)ftp, ftp_dtp_error_handler, XConnectionType_Direct);
    XObject_connect_1((XObject*)ftp->m_dtpSocket, XIODevice_readyRead_signal,
                      (XObject*)ftp, ftp_dtp_readyRead_handler, XConnectionType_Direct);
    XObject_connect_1((XObject*)ftp->m_dtpSocket, XIODevice_bytesWritten_signal,
                      (XObject*)ftp, ftp_dtp_bytesWritten_handler, XConnectionType_Direct);
    XObject_connect_1((XObject*)ftp->m_dtpSocket, XSslSocket_encrypted_signal,
                      (XObject*)ftp, ftp_dtp_ssl_encrypted_handler, XConnectionType_Direct);
}

static void ftp_connectDtpServerSignals(XFtp* ftp)
{
    if (!ftp || !ftp->m_dtpServer) return;
    XObject_connect_1((XObject*)ftp->m_dtpServer, XTcpServer_newConnection_signal,
                      (XObject*)ftp, ftp_dtp_server_newConnection_handler, XConnectionType_Direct);
}

// =============== PI socket handlers ===============
// 解析 FEAT multi-line 响应中的一行（211 起到 215 结束）
static void ftp_parse_feat_line(XFtp* ftp, const char* line)
{
    if (!ftp || !line) return;
    const char* p = line;
    // 跳过可能的响应码前缀 "211-"
    if (p[0] >= '0' && p[0] <= '9' && p[1] >= '0' && p[1] <= '9' && p[2] >= '0' && p[2] <= '9' &&
        (p[3] == ' ' || p[3] == '-')) {
        p += (p[3] == ' ') ? 4 : 4;
    }
    // 跳过前导空格
    while (*p == ' ') p++;
    // 转大写比较
    if (xftp_stricmp_n(p, "UTF8", 4) == 0)         ftp->m_features |= XFtp_Feature_UTF8;
    else if (xftp_stricmp_n(p, "MLSD", 4) == 0)    ftp->m_features |= XFtp_Feature_MLSD;
    else if (xftp_stricmp_n(p, "MODE Z", 6) == 0) ftp->m_features |= XFtp_Feature_MODEZ;
    else if (xftp_stricmp_n(p, "EPSV", 4) == 0)   ftp->m_features |= XFtp_Feature_EPSV;
    else if (xftp_stricmp_n(p, "EPRT", 4) == 0)   ftp->m_features |= XFtp_Feature_EPSV;
    else if (xftp_stricmp_n(p, "REST STREAM", 11) == 0) ftp->m_features |= XFtp_Feature_REST_STREAM;
    else if (xftp_stricmp_n(p, "TVFS", 4) == 0)   ftp->m_features |= XFtp_Feature_TVFS;
    else if (xftp_stricmp_n(p, "ABOR", 4) == 0)   ftp->m_features |= XFtp_Feature_ABOR;
    else if (xftp_stricmp_n(p, "SIZE", 4) == 0)   ftp->m_features |= XFtp_Feature_SIZE;
}

static void ftp_pi_connected_handler(XObject* receiver, XVarList* args)
{
    (void)args;
    if (!receiver) return;
    XFtp* ftp = (XFtp*)receiver;

    ftp->m_state = XFtp_State_Connected;
    XFtp_stateChanged_signal(ftp, ftp->m_state);

    // TCP 已连上，但 FTP 握手要等服务器的 220 问候语到达才算完成。
    // 不能在此处 finishCommand(ConnectToHost)，否则迟到的 220 问候会被
    // 误当成后续命令（Login）的应答，造成"假登录"（USER/PASS 从未真正走完）。
    // FEAT / OPTS / MODE Z / AUTH TLS 都推迟到 processReply 收到 220 后再发。
    if (ftp->m_d) ftp->m_d->m_waitForGreeting = 1;
}

static void ftp_pi_disconnected_handler(XObject* receiver, XVarList* args)
{
    (void)args;
    if (!receiver) return;
    XFtp* ftp = (XFtp*)receiver;

    if (ftp->m_state != XFtp_State_Closing) {
        ftp->m_state = XFtp_State_Unconnected;
        XFtp_stateChanged_signal(ftp, ftp->m_state);
    }

    // 自动重连
    if (ftp->m_autoReconnect && ftp->m_host) {
        if (ftp->m_maxReconnectAttempts == 0 || ftp->m_reconnectAttempts < ftp->m_maxReconnectAttempts) {
            ftp->m_reconnectAttempts++;
            // 延迟重连
            XThread_msleep((unsigned int)ftp->m_reconnectInterval);
            if (ftp->m_host) {
                const char* host = XString_toUtf8(ftp->m_host);
                XAbstractSocket_connectToHost_base(ftp->m_piSocket, host, ftp->m_port,
                    XIODevice_ReadWrite, XAbstractSocket_AnyIPProtocol);
            }
        }
    }
}

static void ftp_pi_error_handler(XObject* receiver, XVarList* args)
{
    if (!receiver || !args) return;
    XFtp* ftp = (XFtp*)receiver;
    XVarList_start(args);
    XAbstractSocket_SocketError error = XVarList_arg(args, XAbstractSocket_SocketError);

    switch (error) {
    case XAbstractSocket_HostNotFoundError:
        ftp->m_error = XFtp_Error_HostNotFound;
        break;
    case XAbstractSocket_ConnectionRefusedError:
        ftp->m_error = XFtp_Error_ConnectionRefused;
        break;
    case XAbstractSocket_SocketTimeoutError:
        ftp->m_error = XFtp_Error_Timeout;
        break;
    case XAbstractSocket_NetworkError:
        ftp->m_error = XFtp_Error_NetworkError;
        break;
    default:
        ftp->m_error = XFtp_Error_Unknown;
        break;
    }

    // 获取错误信息
    XString* errStr = XAbstractSocket_errorString(ftp->m_piSocket);
    if (errStr) {
        XString_assign_utf8(ftp->m_errorString, XString_toUtf8(errStr));
    }

    ftp->m_state = XFtp_State_Unconnected;
    XFtp_stateChanged_signal(ftp, ftp->m_state);

    /* A transport error is terminal for the in-flight command.  Complete it
     * here so callers are not left waiting forever and the queue can advance. */
    if (ftp->m_currentCommand) {
        ftp_pi_handleError(ftp, 0,
            ftp->m_errorString ? XString_toUtf8(ftp->m_errorString) : "Control connection error");
    }
}

static void ftp_pi_readyRead_handler(XObject* receiver, XVarList* args)
{
    (void)args;
    if (!receiver) return;
    XFtp* ftp = (XFtp*)receiver;
    if (!ftp->m_piSocket || !ftp->m_piBuffer) return;

    // 读取数据
    char buf[2048];
    int64_t len = XAbstractSocket_read((XIODevice*)ftp->m_piSocket, buf, sizeof(buf) - 1);
    if (len <= 0) return;
    buf[len] = '\0';
    // 追加到缓冲
    int64_t oldSize = XByteArray_size_base(ftp->m_piBuffer);
    XByteArray_resize_base(ftp->m_piBuffer, oldSize + len);
    memcpy(XByteArray_data(ftp->m_piBuffer) + oldSize, buf, len);

    // 解析完整行（以 \r\n 结束）
    const char* data = (const char*)XByteArray_data(ftp->m_piBuffer);
    int64_t size = XByteArray_size_base(ftp->m_piBuffer);

    char line[2048];
    int64_t consume = 0;
    for (int64_t i = 0; i < size; i++) {
        // 检测行结束符：\n 或 \r\n
        if (data[i] == '\n') {
            // 计算行长度（不含行结束符）
            int lineLen = (int)(i - consume);
            // 如果行尾是 \r\n，去掉 \r
            if (lineLen > 0 && data[i-1] == '\r') lineLen--;
            if (lineLen >= (int)sizeof(line)) lineLen = (int)sizeof(line) - 1;
            if (lineLen > 0) {
                memcpy(line, data + consume, lineLen);
            }
            line[lineLen] = '\0';
            ftp_processReply(ftp, line);
            // 移动到下一行开始
            consume = i + 1;
        }
    }

    // 移除已消费的数据
    if (consume > 0) {
        int64_t remain = size - consume;
        if (remain > 0) {
            memmove(XByteArray_data(ftp->m_piBuffer), data + consume, remain);
        }
        XByteArray_resize_base(ftp->m_piBuffer, remain);
    }
}

static void ftp_pi_ssl_encrypted_handler(XObject* receiver, XVarList* args)
{
    (void)args;
    if (!receiver) return;
    XFtp* ftp = (XFtp*)receiver;

    if (ftp->m_d) {
        ftp->m_d->m_sm.m_sslState = 3;  // 等待 PBSZ 响应
    }

    // RFC 4217 Explicit SSL: TLS 完成后发 PBSZ 0 + PROT P
    // (Protection Buffer Size + Data Channel Protection level)
    ftp_pi_sendCommand(ftp, "PBSZ 0\r\n");
    // PROT P 等 PBSZ 200 后再发（在 ftp_pi_handleIntermediateReply 里）

}

// =============== DTP socket handlers ===============
static void ftp_dtp_connected_handler(XObject* receiver, XVarList* args)
{
    (void)args;
    if (!receiver) return;
    XFtp* ftp = (XFtp*)receiver;

    if (ftp->m_useSsl) {
        XSslSocket_startClientEncryption((XSslSocket*)ftp->m_dtpSocket);
        return;
    }
    if (ftp->m_d) ftp->m_d->m_dtpState = 1;

    if (ftp->m_d && ftp->m_d->m_waitForDtpToConnect) {
        ftp_dtp_startTransfer(ftp);
    }
}

static void ftp_dtp_ssl_encrypted_handler(XObject* receiver, XVarList* args)
{
    (void)args;
    XFtp* ftp = (XFtp*)receiver;
    if (!ftp || !ftp->m_d) return;
    ftp->m_d->m_dtpState = 1;
    if (ftp->m_d->m_waitForDtpToConnect) ftp_dtp_startTransfer(ftp);
}

static void ftp_dtp_disconnected_handler(XObject* receiver, XVarList* args)
{
    (void)args;
    if (!receiver) return;
    XFtp* ftp = (XFtp*)receiver;

    /* 只在 1→0 转换时调 finishTransfer。disconnected 事件可能多次触发
     * （XObject_connect_1 重复 listener），不加 guard 会在 256KB 大文件传输中
     * 触发 30+ 次冗余 finishTransfer 调用。 */
    bool was_open = false;
    if (ftp->m_d) {
        was_open = (ftp->m_d->m_dtpState != 0);
        ftp->m_d->m_dtpState = 0;
    }

    /* 触发 finishTransfer：让 DTP 关闭成为"可 finish"信号。
     * - 若 m_piAckedTransfer=0（226 还没到）：finishTransfer return
     * - 若 m_currentCommand==null（已 finish）：整个 if 块跳过，did_finish=false
     * - 关键：Test 10 GET 256KB，226 到达时 DTP 还有 248KB 数据未读。
     *   现在 finishTransfer 看到 m_dtpState=1 时 return，等 DTP 关了（m_dtpState=0）才真正 finish。 */
    if (was_open) {
        ftp_dtp_finishTransfer(ftp);
    }
}

static void ftp_dtp_error_handler(XObject* receiver, XVarList* args)
{
    (void)args;
    if (!receiver) return;
    XFtp* ftp = (XFtp*)receiver;
    ftp->m_error = XFtp_Error_TransferFailed;
    if (ftp->m_dtpSocket && ftp->m_errorString) {
        XString* errStr = XAbstractSocket_errorString(ftp->m_dtpSocket);
        XString_assign_utf8(ftp->m_errorString,
            errStr ? XString_toUtf8(errStr) : "Data connection error");
    }
    if (ftp->m_currentCommand) {
        ftp_pi_handleError(ftp, 0,
            ftp->m_errorString ? XString_toUtf8(ftp->m_errorString) : "Data connection error");
    }
}

static void ftp_dtp_readyRead_handler(XObject* receiver, XVarList* args)
{
    (void)args;
    if (!receiver) return;
    XFtp* ftp = (XFtp*)receiver;
    if (!ftp->m_dtpSocket || !ftp->m_readBuffer) return;

    char buf[8192];
    int64_t len;
    int64_t total_this = 0;
    /* 一次性读光当前内核缓冲；read 返 0 表示 EOF（server 关了 DTP），
     * 这时候 readBuffer 已读完所有数据，可以安全 finishTransfer */
    int64_t bufStart = XByteArray_size_base(ftp->m_readBuffer);  /* 本轮起点，写 device 用 */
    while ((len = XAbstractSocket_read((XIODevice*)ftp->m_dtpSocket, buf, sizeof(buf))) > 0) {
        int64_t oldSize = XByteArray_size_base(ftp->m_readBuffer);
        XByteArray_resize_base(ftp->m_readBuffer, oldSize + len);
        memcpy(XByteArray_data(ftp->m_readBuffer) + oldSize, buf, len);
        total_this += len;
    }

    ftp->m_transferCurrent += total_this;
    XFtp_dataTransferProgress_signal(ftp, ftp->m_transferCurrent, ftp->m_transferTotal);

    /* 写文件（Get 命令，m_transferDevice 可能是 XFile/XBuffer/用户自定义）——
     * 数据在 m_readBuffer 中，从 bufStart 开始是本轮新追加的 total_this 字节。
     * 之前版本错误地用了 while 循环最后一次的 buf/len（EOF 时 len=0，导致 0 字节），
     * 所以下载到 device 的文件一直是空的。
     *
     * 必须在 finishTransfer 之前写：finishTransfer 会清 m_currentCommand，顺序反了
     * 就拿不到 cmd->m_device，导致下载到 device 的文件是 0 字节。 */
    if (ftp->m_transferDevice && ftp->m_currentCommand && ftp->m_currentCommand->m_command == XFtpCommand_Get
        && ftp->m_currentCommand->m_device && total_this > 0) {
        const char* data = (const char*)XByteArray_data(ftp->m_readBuffer);
        int64_t n = XIODevice_write_1((XIODevice*)ftp->m_currentCommand->m_device,
                                       data + bufStart, total_this);
    }

    /* read 返回 0（EOF，server 主动关 DTP）或 < 0（错误，本地关 DTP）：
     * 内核缓冲已读完 / socket 已断开。
     * 如果 m_waitForDtpToClose=1 表示 GET/LIST/PUT 的传输已完成等关闭 DTP，
     * 即可 finishTransfer 关闭 DTP 资源。
     * 226 可能在剩余 DTP 数据还没读完时就到达，所以 finishCommand 不清 m_waitForDtpToClose，
     * 而由 read<=0 在这里触发真正的清理。m_currentCommand 可能已被 finishCommand 清掉，
     * 所以这里只看 m_waitForDtpToClose，不再要求 m_currentCommand 还在。
     *
     * 注意：判断条件用 `len <= 0`（最后那次 read 返 0 表示 EOF，< 0 表示本地主动断开错误），
     * 不是 `total_this == 0`——之前版本的 `total_this==0` 永远不会触发，因为 LIST/RETR
     * 都是先读到数据再读返 0，total_this 必然 > 0，导致 finishTransfer 永远不调用，
     * LIST 输出没解析、大文件下载只写一半就丢失。 */
    if (len <= 0 && ftp->m_d && ftp->m_d->m_waitForDtpToClose) {
        ftp->m_d->m_waitForDtpToClose = 0;
        ftp_dtp_finishTransfer(ftp);
    }

    // 触发 readyRead 信号（用户可主动读缓冲）
    XFtp_readyRead_signal(ftp);
}

static void ftp_dtp_bytesWritten_handler(XObject* receiver, XVarList* args)
{
    if (!receiver) return;
    XFtp* ftp = (XFtp*)receiver;
    int64_t bytes = 0;
    if (args) {
        XVarList_start(args);
        bytes = XVarList_arg(args, int64_t);
    }
    XFtp_dataTransferProgress_signal(ftp, ftp->m_transferCurrent, ftp->m_transferTotal);

    if (!ftp->m_currentCommand || ftp->m_currentCommand->m_command != XFtpCommand_Put
        || !ftp->m_dtpSocket) return;

    // 内存缓冲 Put：分块续写
    if (ftp->m_currentCommand->m_data && ftp->m_d) {
        int64_t total = XByteArray_size_base(ftp->m_currentCommand->m_data);
        if (ftp->m_d->m_putWriteOffset < total) {
            // 还在写过程中：统计实际写入 + 续写下一块
            ftp->m_transferCurrent += bytes;
            const char* data = (const char*)XByteArray_data(ftp->m_currentCommand->m_data);
            int64_t remain = total - ftp->m_d->m_putWriteOffset;
            int64_t chunk = remain < 8192 ? remain : 8192;
            XAbstractSocket_write(ftp->m_dtpSocket, data + ftp->m_d->m_putWriteOffset, chunk);
            ftp->m_d->m_putWriteOffset += chunk;
        }
        // 所有数据已排队：主动断开 DTP 通知服务器上传完成
        // 注：因 CancelIo 会触发多个"假"bytesWritten，但后续事件进不来 if 分支，
        // 不会重复 disconnect（XAbstractSocket_disconnectFromHost_base 对已关闭 socket 是 no-op）
        if (ftp->m_d->m_putWriteOffset == total) {
            XAbstractSocket_disconnectFromHost_base(ftp->m_dtpSocket);
        }
    }
    // device 流式 Put：续读下一块
    else if (ftp->m_currentCommand->m_device) {
        char buf[8192];
        int64_t n = XIODevice_read_1((XIODevice*)ftp->m_currentCommand->m_device, buf, sizeof(buf));
        if (n > 0) {
            XAbstractSocket_write(ftp->m_dtpSocket, buf, n);
            ftp->m_transferTotal += n;
        } else {
            // device 读到 0/-1（EOF 或错误），关闭 DTP 通知服务器上传完成
            XAbstractSocket_disconnectFromHost_base(ftp->m_dtpSocket);
        }
    }
}

static void ftp_dtp_server_newConnection_handler(XObject* receiver, XVarList* args)
{
    (void)args;
    if (!receiver) return;
    XFtp* ftp = (XFtp*)receiver;

    if (!ftp->m_dtpServer) return;

    // 取出已接受的入站连接
    XTcpSocket* incoming = XTcpServer_nextPendingConnection_base(ftp->m_dtpServer);
    if (!incoming) return;

    // 把入站连接接管为 DTP socket
    if (ftp->m_dtpSocket) {
        XAbstractSocket_disconnectFromHost_base(ftp->m_dtpSocket);
        ftp->m_dtpSocket = NULL;
    }
    ftp->m_dtpSocket = (XAbstractSocket*)incoming;
    XAbstractSocket_setProtocolTag(ftp->m_dtpSocket, "ftp-dtp-active");
    ftp_connectDtpSocketSignals(ftp);

    if (ftp->m_d && ftp->m_d->m_waitForDtpToConnect) {
        ftp->m_d->m_waitForDtpToConnect = 0;
        ftp_dtp_startTransfer(ftp);
    }
}

// =============== 命令处理 ===============

static void ftp_pi_startNextCommand(XFtp* ftp)
{
    if (!ftp) return;
    /* 已有命令在执行中，不启动下一个（等 finishCommand 来驱动） */
    if (ftp->m_currentCommand) return;
    if (!ftp->m_pendingCommands) return;

    XMutex_lock(ftp->m_commandMutex);
    if (XVector_size_base(ftp->m_pendingCommands) == 0) {
        XMutex_unlock(ftp->m_commandMutex);
        return;
    }

    /* XVector_at_base 返回的是存储位置的地址（vector 存的是 XFtpCommand* 指针），
     * 要再解一层拿到真实对象指针 */
    XFtpCommand* cmd = XVEC_POP_FIRST(ftp->m_pendingCommands, XFtpCommand);
    if (!cmd) {
        XMutex_unlock(ftp->m_commandMutex);
        return;
    }


    // 发射命令开始信号
    XFtp_commandStarted_signal(ftp, cmd->m_id);
    ftp->m_currentCommand = cmd;
    ftp->m_currentId = cmd->m_id;

    XMutex_unlock(ftp->m_commandMutex);

    // SSL 未完成时不执行 ConnectToHost 之外的命令
    if (ftp->m_useSsl && ftp->m_d && ftp->m_d->m_sm.m_sslState != 5
        && cmd->m_command != XFtpCommand_ConnectToHost) {
        return;  // 等 encrypted 信号
    }

    // 重置错误
    ftp->m_error = XFtp_Error_NoError;
    XString_assign_utf8(ftp->m_errorString, "Unknown error");

    // 根据命令类型发送 FTP 协议命令
    switch (cmd->m_command) {
    case XFtpCommand_ConnectToHost: {
        const char* host = (cmd->m_rawCmds && XVector_size_base(cmd->m_rawCmds) > 0)
            ? XVEC_STR_AT(cmd->m_rawCmds, 0) : "127.0.0.1";
        uint16_t port = 21;
        if (cmd->m_rawCmds && XVector_size_base(cmd->m_rawCmds) > 1) {
            port = (uint16_t)atoi(XVEC_STR_AT(cmd->m_rawCmds, 1));
        }
        XAbstractSocket_connectToHost_base(ftp->m_piSocket, host, port,
            XIODevice_ReadWrite, XAbstractSocket_AnyIPProtocol);
        // 连接完成后，connected_handler 会启动 startNextCommand
        break;
    }
    case XFtpCommand_Login: {
        const char* user = (cmd->m_rawCmds && XVector_size_base(cmd->m_rawCmds) > 0)
            ? XVEC_STR_AT(cmd->m_rawCmds, 0) : "anonymous";
        const char* pass = (cmd->m_rawCmds && XVector_size_base(cmd->m_rawCmds) > 1)
            ? XVEC_STR_AT(cmd->m_rawCmds, 1) : "";
        char buf[512];
        snprintf(buf, sizeof(buf), "USER %s\r\n", user);
        ftp_pi_sendCommand(ftp, buf);
        break;
    }
    case XFtpCommand_List: {
        // TYPE A (ASCII) 用于 LIST/MLSD
        ftp_pi_sendCommand(ftp, "TYPE A\r\n");
        // 优先用 MLSD（如果 server 支持 + 用户启用）
        bool useMlsd = ftp->m_useMlsd && (ftp->m_features & XFtp_Feature_MLSD);
        if (cmd->m_device) {
            // device 模式下从 device 流式读——由 ftp_dtp_readyRead_handler 负责
            ftp->m_transferDevice = cmd->m_device;
        }
        // PASV / EPSV 优先
        if (ftp->m_transferMode == XFtp_TransferMode_Passive) {
            if (ftp->m_features & XFtp_Feature_EPSV) {
                ftp_pi_sendCommand(ftp, "EPSV\r\n");
            } else {
                ftp_pi_sendCommand(ftp, "PASV\r\n");
            }
        } else {
            if (!ftp->m_dtpServer) {
                ftp->m_dtpServer = XTcpServer_create();
                if (ftp->m_dtpServer) {
                    XTcpServer_init(ftp->m_dtpServer);
                    ftp_connectDtpServerSignals(ftp);
                }
            }
            if (ftp->m_dtpServer) {
                char portCmd[64];
                if (ftp_dtp_startListen(ftp, portCmd, sizeof(portCmd)) == 0) {
                    ftp_pi_sendCommand(ftp, portCmd);
                    if (ftp->m_d) {
                        ftp->m_d->m_waitForDtpToConnect = 1;  // 等服务器回连
                    }
                }
            }
        }
        // 把"用 MLSD 还是 LIST"以及目录路径存在命令的 rawCmds 末尾供 227 响应时读
        char* chosen = useMlsd ? "MLSD" : "LIST";
        if (cmd->m_rawCmds && XVector_size_base(cmd->m_rawCmds) > 0) {
            const char* dir = XVEC_STR_AT(cmd->m_rawCmds, 0);
            char buf[512];
            if (dir && dir[0]) snprintf(buf, sizeof(buf), "%s %s", chosen, dir);
            else              snprintf(buf, sizeof(buf), "%s", chosen);
            cmd->m_data = (void*)XString_create_utf8(buf);
        } else {
            cmd->m_data = (void*)XString_create_utf8(chosen);
        }
        ftp->m_dirListingActive = 1;
        // 实际发送在 PASV 响应后由 227 中间响应处理器进行
        break;
    }
    case XFtpCommand_Cd: {
        const char* dir = (cmd->m_rawCmds && XVector_size_base(cmd->m_rawCmds) > 0)
            ? XVEC_STR_AT(cmd->m_rawCmds, 0) : "/";
        char buf[512];
        snprintf(buf, sizeof(buf), "CWD %s\r\n", dir);
        ftp_pi_sendCommand(ftp, buf);
        break;
    }
    case XFtpCommand_Get: {
        // TYPE I（二进制）或 TYPE A
        const char* typeCmd = (ftp->m_transferType == XFtp_DataType_Ascii) ? "TYPE A\r\n" : "TYPE I\r\n";
        ftp_pi_sendCommand(ftp, typeCmd);
        // device 流式 get：保存 device 引用
        if (cmd->m_device) {
            ftp->m_transferDevice = cmd->m_device;
        }
        // PASV / EPSV（RETR 在 DTP 连接建立后由 ftp_dtp_startTransfer 发送）
        if (ftp->m_transferMode == XFtp_TransferMode_Passive) {
            if (ftp->m_features & XFtp_Feature_EPSV) {
                ftp_pi_sendCommand(ftp, "EPSV\r\n");
            } else {
                ftp_pi_sendCommand(ftp, "PASV\r\n");
            }
        } else {
            if (!ftp->m_dtpServer) {
                ftp->m_dtpServer = XTcpServer_create();
                if (ftp->m_dtpServer) {
                    XTcpServer_init(ftp->m_dtpServer);
                    ftp_connectDtpServerSignals(ftp);
                }
            }
            if (ftp->m_dtpServer) {
                char portCmd[64];
                if (ftp_dtp_startListen(ftp, portCmd, sizeof(portCmd)) == 0) {
                    ftp_pi_sendCommand(ftp, portCmd);
                    if (ftp->m_d) {
                        ftp->m_d->m_waitForDtpToConnect = 1;  // 等服务器回连
                    }
                }
            }
        }
        // REST + RETR 均在 ftp_dtp_startTransfer 中发送
        break;
    }
    case XFtpCommand_Put: {
        // TYPE I（按 transferType）
        const char* typeCmd = (ftp->m_transferType == XFtp_DataType_Ascii) ? "TYPE A\r\n" : "TYPE I\r\n";
        ftp_pi_sendCommand(ftp, typeCmd);
        // device 流式 put：保存 device 引用
        if (cmd->m_device) {
            ftp->m_transferDevice = cmd->m_device;
        }
        // ALLO（可选）
        // PASV / EPSV / PORT
        if (ftp->m_transferMode == XFtp_TransferMode_Passive) {
            if (ftp->m_features & XFtp_Feature_EPSV) {
                ftp_pi_sendCommand(ftp, "EPSV\r\n");
            } else {
                ftp_pi_sendCommand(ftp, "PASV\r\n");
            }
        } else {
            if (!ftp->m_dtpServer) {
                ftp->m_dtpServer = XTcpServer_create();
                if (ftp->m_dtpServer) {
                    XTcpServer_init(ftp->m_dtpServer);
                    ftp_connectDtpServerSignals(ftp);
                }
            }
            if (ftp->m_dtpServer) {
                char portCmd[64];
                if (ftp_dtp_startListen(ftp, portCmd, sizeof(portCmd)) == 0) {
                    ftp_pi_sendCommand(ftp, portCmd);
                    if (ftp->m_d) {
                        ftp->m_d->m_waitForDtpToConnect = 1;  // 等服务器回连
                    }
                }
            }
        }
        // STOR/APPE 命令在 PASV 响应后发送
        if (cmd->m_rawCmds && XVector_size_base(cmd->m_rawCmds) > 0) {
            const char* file = XVEC_STR_AT(cmd->m_rawCmds, 0);
            // 仅记录文件名，命令在 150 响应后发
        }
        break;
    }
    case XFtpCommand_Remove: {
        if (cmd->m_rawCmds && XVector_size_base(cmd->m_rawCmds) > 0) {
            const char* file = XVEC_STR_AT(cmd->m_rawCmds, 0);
            char buf[512];
            snprintf(buf, sizeof(buf), "DELE %s\r\n", file);
            ftp_pi_sendCommand(ftp, buf);
        }
        break;
    }
    case XFtpCommand_Rename: {
        if (cmd->m_rawCmds && XVector_size_base(cmd->m_rawCmds) > 1) {
            const char* from = XVEC_STR_AT(cmd->m_rawCmds, 0);
            const char* to   = XVEC_STR_AT(cmd->m_rawCmds, 1);
            char buf[1024];
            snprintf(buf, sizeof(buf), "RNFR %s\r\n", from);
            ftp_pi_sendCommand(ftp, buf);
            snprintf(buf, sizeof(buf), "RNTO %s\r\n", to);
            // 第二个命令在 350 响应后发
        }
        break;
    }
    case XFtpCommand_Mkdir: {
        if (cmd->m_rawCmds && XVector_size_base(cmd->m_rawCmds) > 0) {
            const char* dir = XVEC_STR_AT(cmd->m_rawCmds, 0);
            char buf[512];
            snprintf(buf, sizeof(buf), "MKD %s\r\n", dir);
            ftp_pi_sendCommand(ftp, buf);
        }
        break;
    }
    case XFtpCommand_Rmdir: {
        if (cmd->m_rawCmds && XVector_size_base(cmd->m_rawCmds) > 0) {
            const char* dir = XVEC_STR_AT(cmd->m_rawCmds, 0);
            char buf[512];
            snprintf(buf, sizeof(buf), "RMD %s\r\n", dir);
            ftp_pi_sendCommand(ftp, buf);
        }
        break;
    }
    case XFtpCommand_Close: {
        ftp->m_state = XFtp_State_Closing;
        XFtp_stateChanged_signal(ftp, ftp->m_state);
        ftp_pi_sendCommand(ftp, "QUIT\r\n");
        XAbstractSocket_disconnectFromHost_base(ftp->m_piSocket);
        break;
    }
    case XFtpCommand_RawCommand: {
        if (cmd->m_rawCmd) {
            const char* s = XString_toUtf8(cmd->m_rawCmd);
            if (s) ftp_pi_sendCommand(ftp, s);
        }
        break;
    }
    case XFtpCommand_Size: {
        if (cmd->m_rawCmds && XVector_size_base(cmd->m_rawCmds) > 0) {
            const char* file = XVEC_STR_AT(cmd->m_rawCmds, 0);
            char buf[512];
            snprintf(buf, sizeof(buf), "SIZE %s\r\n", file);
            ftp_pi_sendCommand(ftp, buf);
        }
        break;
    }
    case XFtpCommand_Mdtm: {
        if (cmd->m_rawCmds && XVector_size_base(cmd->m_rawCmds) > 0) {
            const char* file = XVEC_STR_AT(cmd->m_rawCmds, 0);
            char buf[512];
            snprintf(buf, sizeof(buf), "MDTM %s\r\n", file);
            ftp_pi_sendCommand(ftp, buf);
        }
        break;
    }
    case XFtpCommand_Mlst: {
        if (cmd->m_rawCmds && XVector_size_base(cmd->m_rawCmds) > 0) {
            const char* file = XVEC_STR_AT(cmd->m_rawCmds, 0);
            char buf[512];
            snprintf(buf, sizeof(buf), "MLST %s\r\n", file);
            ftp_pi_sendCommand(ftp, buf);
        } else {
            ftp_pi_sendCommand(ftp, "MLST\r\n");
        }
        // MLST 状态机 0→1：进入接收 250-Listing 状态
        if (ftp->m_d) ftp->m_d->m_sm.m_mlstState = 1;
        break;
    }
    default:
        break;
    }
}

static void ftp_processReply(XFtp* ftp, const char* reply)
{
    if (!ftp || !reply) return;

    // 解析回复码
    int code = 0;
    char codeStr[4] = {0};
    if (strlen(reply) >= 3 &&
        isdigit((unsigned char)reply[0]) &&
        isdigit((unsigned char)reply[1]) &&
        isdigit((unsigned char)reply[2])) {
        codeStr[0] = reply[0];
        codeStr[1] = reply[1];
        codeStr[2] = reply[2];
        code = atoi(codeStr);
    }


    // 获取回复文本
    const char* text = reply;
    if (strlen(reply) > 4) {
        text = reply + 4;
        if (text[0] == '-' || text[0] == ' ') text++;
    }

    // ===== 服务器 220 问候语：ConnectToHost 的真正完成信号 =====
    // TCP 连上后服务器首先发 220 问候，收到后才能结束 ConnectToHost 并
    // 启动命令队列（Login）。FEAT 等特性探测推迟到登录成功后再发，
    // 避免带外应答与登录的 331/230 在时序上互相干扰。
    if (code == 220 && ftp->m_d && ftp->m_d->m_waitForGreeting
        && (strlen(reply) <= 3 || reply[3] != '-')) {
        ftp->m_d->m_waitForGreeting = 0;
        // 清理多行缓冲（防止多行 220 问候残留影响后续解析）
        if (ftp->m_d->m_replyText) XString_assign_utf8(ftp->m_d->m_replyText, "");

        // Explicit SSL：AUTH TLS 启动加密，ConnectToHost 在 TLS 完成后结束
        if (ftp->m_useSsl) {
            ftp->m_d->m_sm.m_sslState = 1;
            ftp_pi_sendCommand(ftp, "AUTH TLS\r\n");
            return;
        }

        // 结束 ConnectToHost，启动队列里的下一个命令（通常是 Login）
        ftp_pi_finishCommand(ftp, 220, text);
        return;
    }

    // Explicit SSL：收到 234 响应（AUTH TLS 接受），启动 TLS 握手
    if (code == 234 && ftp->m_d && ftp->m_d->m_sm.m_sslState == 1) {
        ftp->m_d->m_sm.m_sslState = 2;
        if (ftp->m_piSocket) {
            XSslSocket_startClientEncryption((XSslSocket*)ftp->m_piSocket);
        }
        return;
    }

    if (code == 200 && ftp->m_d && ftp->m_d->m_sm.m_sslState == 3) {
        ftp->m_d->m_sm.m_sslState = 4;
        ftp_pi_sendCommand(ftp, "PROT P\r\n");
        return;
    }
    if (code == 200 && ftp->m_d && ftp->m_d->m_sm.m_sslState == 4) {
        ftp->m_d->m_sm.m_sslState = 5;
        ftp_pi_finishCommand(ftp, code, text);
        return;
    }

    // 多行响应：以 xxx- 开头表示还有后续
    if (strlen(reply) > 3 && reply[3] == '-') {
        // 如果这是 FEAT 多行响应（211-...），首行也可能是 "211-UTF8" 这类带特性的格式
        if (code == 211) {
            ftp_parse_feat_line(ftp, text);
        }
        // 250-Listing ... 标识 MLST multi-line 开始
        if (code == 250 && ftp->m_d && ftp->m_d->m_sm.m_mlstState == 0
            && ftp->m_currentCommand && ftp->m_currentCommand->m_command == XFtpCommand_Mlst) {
            ftp->m_d->m_sm.m_mlstState = 1;
        }
        if (ftp->m_d && ftp->m_d->m_replyText) {
            XString_append_utf8(ftp->m_d->m_replyText, text);
            XString_append_utf8(ftp->m_d->m_replyText, "\n");
        }
        return;
    }

    // 多行响应中间行：没有响应码前缀（如 " UTF8"），但当前在多行响应中
    // 标准 FTP 多行响应中间行格式：无响应码前缀，前面有空格
    if (code == 0 && ftp->m_d && ftp->m_d->m_replyText
        && XString_toUtf8(ftp->m_d->m_replyText)
        && strlen(XString_toUtf8(ftp->m_d->m_replyText)) > 0) {
        // MLST 多行响应：当前 m_mlstState==1 时，解析 facts 行（带分号）
        if (ftp->m_d->m_sm.m_mlstState == 1 && ftp->m_currentCommand
            && ftp->m_currentCommand->m_command == XFtpCommand_Mlst) {
            const char* p = reply;
            while (*p == ' ') p++;
            if (*p) ftp_parse_list_line(ftp, p);  // 复用 MLSD 解析器
        } else {
            // 当作 FEAT 中间行解析：跳过前导空格
            const char* p = reply;
            while (*p == ' ') p++;
            if (*p) ftp_parse_feat_line(ftp, p);
        }
        if (ftp->m_d->m_replyText) {
            XString_append_utf8(ftp->m_d->m_replyText, reply);
            XString_append_utf8(ftp->m_d->m_replyText, "\n");
        }
        return;
    }

    // ===== 带外探测应答消化（登录后的 FEAT 211 / OPTS 200）=====
    // 此时命令队列已空闲（m_currentCommand == NULL），这些应答不属于任何
    // 排队命令，直接消化即可。仅匹配 211/200，绝不会误吞 230 这类命令应答。
    if (ftp->m_d && ftp->m_d->m_outOfBandPending > 0
        && !ftp->m_currentCommand && (code == 211 || code == 200)) {
        if (code == 211) ftp_parse_feat_line(ftp, text);  // 解析 "211 End" 行
        ftp->m_d->m_outOfBandPending--;
        if (ftp->m_d->m_replyText) XString_assign_utf8(ftp->m_d->m_replyText, "");
        return;
    }

    // 保存最终响应
    if (ftp->m_d && ftp->m_d->m_replyText) {
        XString_assign_utf8(ftp->m_d->m_replyText, text);
    }

    // 提取百位分类
    int codeClass = code / 100;

    // 发射 rawCommandReply 信号
    XFtp_rawCommandReply_signal(ftp, code, reply);

    // 主动模式(PORT)：TYPE 200 和 PORT 200 由 ftp_pi_finishCommand 的
    // "code==200 且 Put/Get/List" 规则吞掉，不结束命令。
    // 真正的数据命令在服务器回连后由 ftp_dtp_server_newConnection_handler 触发
    // （通过 m_waitForDtpToConnect 标志）。

    // 根据分类处理
    switch (codeClass) {
    case 1:  // 积极初步应答（150/125：数据传输开始）
        if ((code == 150 || code == 125) && ftp->m_currentCommand) {
            if (ftp->m_d) ftp->m_d->m_waitForDtpToClose = 1;
            /* Get/List：重置 transfer 计数。m_transferTotal = -1 表示"未知"，
             * 避免 finishTransfer 误以为 m_transferCurrent 已满（GET 不知道文件大小）。 */
            if (ftp->m_currentCommand->m_command == XFtpCommand_Get ||
                ftp->m_currentCommand->m_command == XFtpCommand_List) {
                ftp->m_transferTotal = -1;
                ftp->m_transferCurrent = 0;
            }
            // Put：150 响应后分块写数据到 DTP（避免单次大缓冲溢出）
            if (ftp->m_currentCommand->m_command == XFtpCommand_Put && ftp->m_dtpSocket) {
                if (ftp->m_currentCommand->m_data) {
                    int64_t size = XByteArray_size_base(ftp->m_currentCommand->m_data);
                    ftp->m_transferTotal = size;
                    ftp->m_transferCurrent = 0;  /* 显式清零，确保 m_transferCurrent 起始为 0 */
                    ftp->m_d->m_putWriteOffset = 0;
                    // 写第一块（≤8KB），后续由 bytesWritten handler 续写
                    const char* data = (const char*)XByteArray_data(ftp->m_currentCommand->m_data);
                    int64_t chunk = size < 8192 ? size : 8192;
                    if (chunk > 0) {
                        XAbstractSocket_write(ftp->m_dtpSocket, data, chunk);
                        ftp->m_d->m_putWriteOffset = chunk;
                        /* 关键：m_transferCurrent 也要累加，否则 disconnected_handler 调 finishTransfer
                         * 时 m_transferCurrent < m_transferTotal 会误判为"未完成"。
                         * 之前 bytesWritten handler 在 m_putWriteOffset < total 条件内累加 m_transferCurrent，
                         * 但如果整个数据 ≤ 8KB（一次 write 完），bytesWritten handler 走 m_putWriteOffset==total
                         * 分支不会累加 m_transferCurrent。这里直接在第一块 write 后累加，保证 m_transferCurrent
                         * 反映已提交字节数。 */
                        ftp->m_transferCurrent = chunk;
                    } else {
                        // 零字节数据：直接关闭 DTP 通知服务器上传完成
                        XAbstractSocket_disconnectFromHost_base(ftp->m_dtpSocket);
                    }
                } else if (ftp->m_currentCommand->m_device) {
                    // device 流式 put：读一块写一块，总量由 bytesWritten 跟踪
                    char buf[8192];
                    int64_t n = XIODevice_read_1((XIODevice*)ftp->m_currentCommand->m_device,
                                                 buf, sizeof(buf));
                    if (n > 0) {
                        XAbstractSocket_write(ftp->m_dtpSocket, buf, n);
                        ftp->m_transferTotal = n;  // 先设第一块，后续累加
                    } else {
                        ftp->m_transferTotal = 0;
                        XAbstractSocket_disconnectFromHost_base(ftp->m_dtpSocket);
                    }
                }
            }
        }
        break;
    case 2:  // 积极完成应答
        if (ftp->m_state == XFtp_State_Closing) {
            ftp->m_state = XFtp_State_Unconnected;
            XFtp_stateChanged_signal(ftp, ftp->m_state);
        }
        // 227/229：PASV/EPSV 响应，处理 DTP 连接
        if (code == 227 && ftp->m_transferMode == XFtp_TransferMode_Passive) {
            const char* p = strchr(text, '(');
            if (p) {
                int h1, h2, h3, h4, p1, p2;
                if (sscanf(p, "(%d,%d,%d,%d,%d,%d)", &h1, &h2, &h3, &h4, &p1, &p2) == 6) {
                    char ip[64];
                    snprintf(ip, sizeof(ip), "%d.%d.%d.%d", h1, h2, h3, h4);
                    uint16_t port = (uint16_t)((p1 << 8) | p2);
                    ftp_dtp_connect(ftp, ip, port);
                }
            }
            break;
        }
        if (code == 229 && ftp->m_transferMode == XFtp_TransferMode_Passive) {
            const char* p = strchr(text, '(');
            if (p) {
                int d1;
                if (sscanf(p, "(|||%d|)", &d1) == 1) {
                    ftp_dtp_connect(ftp, NULL, (uint16_t)d1);
                }
            }
            break;
        }
        ftp_pi_finishCommand(ftp, code, text);
        break;
    case 3:  // 积极中间应答
        ftp_pi_handleIntermediateReply(ftp, code, text);
        break;
    case 4:  // 暂时否定完成应答
    case 5:  // 永久否定完成应答
        ftp_pi_handleError(ftp, code, text);
        break;
    default:
        break;
    }
}

static void ftp_pi_finishCommand(XFtp* ftp, int code, const char* text)
{
    if (!ftp) return;
    /* 没有正在执行的命令（如 ABOR 后重复到达的 226），直接忽略 */
    if (!ftp->m_currentCommand) return;

    // 对于需要传输的命令（Put/Get/List），TYPE 200 / PORT 200 不应结束命令。
    // 注意：必须在重置 DTP 标志之前判断，否则主动模式的 m_waitForDtpToConnect
    // 会被 TYPE 200 提前清掉，导致 newConnection 无法触发 startTransfer。
    if (code == 200) {
        XFtpCommand_Type cmdType = ftp->m_currentCommand->m_command;
        if (cmdType == XFtpCommand_Put || cmdType == XFtpCommand_Get || cmdType == XFtpCommand_List) {
            return;  // 前导应答，不结束命令，不清状态
        }
    }

    // 主动模式(PORT)传输完成时（Put/Get/List 收到 226/227），不要在这里 finishTransfer。
    // 226 可能在剩余 DTP 数据还没读完时就到达（server sendall+close_data+226 几乎同时），
    // 这里只重置标志和状态，让 readyRead 继续读完 m_dtpSocket 缓冲，最后由 read=0 触发 finishTransfer。
    // 主动模式(PORT)传输完成时（Put/Get/List 收到 226/227），不要在这里 finishTransfer。
    // 226 可能在剩余 DTP 数据还没读完时就到达（server sendall+close_data+226 几乎同时），
    // 这里只重置标志和状态，让 readyRead 继续读完 m_dtpSocket 缓冲，最后由 read=0 触发 finishTransfer。
    if (ftp->m_d) {
        ftp->m_d->m_sm.m_restState = 0;
        ftp->m_d->m_sm.m_mlstState = 0;  // MLST 完成
        ftp->m_d->m_activeAcks = 0;
        ftp->m_d->m_waitForDtpToConnect = 0;
        // 注意：不清 m_waitForDtpToClose，保留给 readyRead 的 read=0 路径触发 finishTransfer
    }

    /* Put/Get/List 传输完成时（226）：PI 已 ACK，但 DTP 可能还有数据未读 / DTP 还没关。
     * 置 m_piAckedTransfer=1 后调 ftp_dtp_finishTransfer：
     *   - 若 DTP 已关（disconnected/readyRead 已触发过 finishTransfer，但被 m_piAckedTransfer=0 阻塞），
     *     本次 finishTransfer 看到 m_piAckedTransfer=1，正常 finish。
     *   - 若 DTP 还没关，finishTransfer 检查 m_dtpState：DTP 未关时 return，等 DTP 关闭时再触发。 */
    if (ftp->m_d && ftp->m_currentCommand) {
        XFtpCommand_Type ct = ftp->m_currentCommand->m_command;
        if (ct == XFtpCommand_Put || ct == XFtpCommand_Get || ct == XFtpCommand_List) {
            ftp->m_d->m_piAckedTransfer = 1;
            /* 关键：226 到达时调 finishTransfer 完成真正的收尾。
             * 若 DTP 已关，finishTransfer 走 finish 路径；
             * 若 DTP 未关，finishTransfer 检查 m_dtpState 后 return。
             * 注意：这里必须先于 "commandFinished + 清 m_currentCommand" 路径之前执行——不能让 226 走 finishCommand 主路径。 */
            ftp_dtp_finishTransfer(ftp);
            return;
        }
    }

    // Explicit SSL (RFC 4217): PBSZ 200 之后发 PROT P 声明数据通道保护
    if (code == 200 && ftp->m_d && ftp->m_d->m_sm.m_sslState == 3
        && ftp->m_currentCommand && ftp->m_currentCommand->m_command == XFtpCommand_RawCommand) {
        const char* cmdline = text ? text : "";
        // PBSZ 200 → 发 PROT P
        // 简单判断：刚才发的就是 PBSZ 0（但 rawCommandReply_signal 已经发了），
        // 这里我们用"sslState 已 3 + 紧跟登录前"启发式：发 PROT P
        ftp_pi_sendCommand(ftp, "PROT P\r\n");
    }

    // 登录成功后切换到 LoggedIn 状态，并探测服务器特性（FEAT）
    if (ftp->m_currentCommand && ftp->m_currentCommand->m_command == XFtpCommand_Login
        && ftp->m_state == XFtp_State_Connected) {
        ftp->m_state = XFtp_State_LoggedIn;
        XFtp_stateChanged_signal(ftp, ftp->m_state);
        // 登录后发 FEAT 探测特性（带外，211 应答由 processReply 单独消化，
        // 此时命令队列已空闲，不会与任何命令应答冲突）
        if (ftp->m_d) {
            ftp->m_d->m_outOfBandPending = 1;
            ftp_pi_sendCommand(ftp, "FEAT\r\n");
            if (ftp->m_useUtf8) {
                ftp_pi_sendCommand(ftp, "OPTS UTF8 ON\r\n");
            }
        }
    }

    // 发射命令完成信号（error 反映 m_error 状态，ABOR/传输失败时为 true）
    XFtp_commandFinished_signal(ftp, ftp->m_currentId, ftp->m_error != XFtp_Error_NoError);

    // 从队列移除第一个命令
    XMutex_lock(ftp->m_commandMutex);
    if (ftp->m_pendingCommands && XVector_size_base(ftp->m_pendingCommands) > 0) {
        XFtpCommand* cmd = XVEC_GET(ftp->m_pendingCommands, 0, XFtpCommand);
        if (cmd) XFtpCommand_delete(cmd);
        XVector_remove_base(ftp->m_pendingCommands, 0, 1);
    }
    ftp->m_currentCommand = NULL;
    XMutex_unlock(ftp->m_commandMutex);

    (void)text;

    // 检查是否所有命令完成
    if (!ftp->m_pendingCommands || XVector_size_base(ftp->m_pendingCommands) == 0) {
        XFtp_done_signal(ftp, ftp->m_error != XFtp_Error_NoError);
    } else {
        ftp_pi_startNextCommand(ftp);
    }
}

static void ftp_pi_handleIntermediateReply(XFtp* ftp, int code, const char* text)
{
    if (!ftp) return;
    if (!ftp->m_currentCommand) return;

    // 211 multi-line FEAT 响应：最后一行（不带 `-`）也解析特性
    if (code == 211) {
        ftp_parse_feat_line(ftp, text);
        return;
    }

    // 解析 PASV 响应：227 Entering Passive Mode (h1,h2,h3,h4,p1,p2)
    if (code == 227 && ftp->m_transferMode == XFtp_TransferMode_Passive) {
        const char* p = strchr(text, '(');
        if (p) {
            int h1, h2, h3, h4, p1, p2;
            if (sscanf(p, "(%d,%d,%d,%d,%d,%d)", &h1, &h2, &h3, &h4, &p1, &p2) == 6) {
                char ip[64];
                snprintf(ip, sizeof(ip), "%d.%d.%d.%d", h1, h2, h3, h4);
                uint16_t port = (uint16_t)((p1 << 8) | p2);
                ftp_dtp_connect(ftp, ip, port);
            }
        }
        return;
    }

    // 解析 EPSV 响应：229 Entering Extended Passive Mode (|||port|)
    if (code == 229 && ftp->m_transferMode == XFtp_TransferMode_Passive) {
        const char* p = strchr(text, '(');
        if (p) {
            // 格式 (|||port|) 或 (|proto|port|)
            int d1, d2, d3, d4;
            if (sscanf(p, "(|||%d|)", &d1) == 1) {
                ftp_dtp_connect(ftp, NULL, (uint16_t)d1);
            } else if (sscanf(p, "(%d|%d|%d|%d)", &d1, &d2, &d3, &d4) == 4) {
                ftp_dtp_connect(ftp, NULL, (uint16_t)d4);
            }
        }
        return;
    }

    // 150 / 125：数据传输开始
    if ((code == 150 || code == 125) && ftp->m_currentCommand) {
        if (ftp->m_d) ftp->m_d->m_waitForDtpToClose = 1;

        // List：实际 LIST/MLSD 命令在 PASV 响应后、150 之前发
        if (ftp->m_currentCommand->m_command == XFtpCommand_List &&
            ftp->m_currentCommand->m_data) {
            char buf[600];
            const char* cmdline = XString_toUtf8((XString*)ftp->m_currentCommand->m_data);
            if (cmdline) {
                size_t len = strlen(cmdline);
                if (len > sizeof(buf) - 4) len = sizeof(buf) - 4;
                memcpy(buf, cmdline, len);
                buf[len] = '\r';
                buf[len+1] = '\n';
                buf[len+2] = '\0';
                ftp_pi_sendCommand(ftp, buf);
            }
        }

        // Get/Retr 等待 DTP 收到数据
        // Put/Stor 等待 DTP 可写
        if (ftp->m_currentCommand->m_command == XFtpCommand_Put) {
            if (ftp->m_dtpSocket) {
                if (ftp->m_currentCommand->m_data) {
                    // 直接写整个 m_data 缓冲
                    const char* data = (const char*)XByteArray_data(ftp->m_currentCommand->m_data);
                    int64_t size = XByteArray_size_base(ftp->m_currentCommand->m_data);
                    ftp->m_transferTotal = size;
                    XAbstractSocket_write(ftp->m_dtpSocket, data, size);
                } else if (ftp->m_currentCommand->m_device) {
                    // device 流式 put：分块从 device 读再写到 DTP
                    char buf[8192];
                    int64_t totalRead = 0;
                    while (1) {
                        int64_t n = XIODevice_read_1((XIODevice*)ftp->m_currentCommand->m_device,
                                                     buf, sizeof(buf));
                        if (n <= 0) break;
                        XAbstractSocket_write(ftp->m_dtpSocket, buf, n);
                        totalRead += n;
                    }
                    ftp->m_transferTotal = totalRead;
                    // 关闭写半部：通知 server 传输完成
                    XAbstractSocket_disconnectFromHost_base(ftp->m_dtpSocket);
                }
            }
        }
    }

    // 331：User name ok, need password
    if (code == 331) {
        if (ftp->m_currentCommand && ftp->m_currentCommand->m_command == XFtpCommand_Login) {
            const char* pass = (ftp->m_currentCommand->m_rawCmds
                && XVector_size_base(ftp->m_currentCommand->m_rawCmds) > 1)
                ? XVEC_STR_AT(ftp->m_currentCommand->m_rawCmds, 1) : "";
            char buf[512];
            snprintf(buf, sizeof(buf), "PASS %s\r\n", pass);
            ftp_pi_sendCommand(ftp, buf);
        }
        return;
    }

    // 350：RNFR 接受 / REST 接受
    if (code == 350) {
        if (ftp->m_d && ftp->m_d->m_sm.m_restState == 1) {
            ftp->m_d->m_sm.m_restState = 2;
            // REST 接受后发 RETR（不需要检查 m_transferDevice，RETR 与 device 无关）
            if (ftp->m_currentCommand && ftp->m_currentCommand->m_command == XFtpCommand_Get
                && ftp->m_currentCommand->m_rawCmds
                && XVector_size_base(ftp->m_currentCommand->m_rawCmds) > 0) {
                const char* file = XVEC_STR_AT(ftp->m_currentCommand->m_rawCmds, 0);
                char buf[512];
                snprintf(buf, sizeof(buf), "RETR %s\r\n", file);
                ftp_pi_sendCommand(ftp, buf);
            }
            return;
        }
        // RNFR -> RNTO
        if (ftp->m_currentCommand && ftp->m_currentCommand->m_command == XFtpCommand_Rename
            && ftp->m_currentCommand->m_rawCmds
            && XVector_size_base(ftp->m_currentCommand->m_rawCmds) > 1) {
            const char* to = XVEC_STR_AT(ftp->m_currentCommand->m_rawCmds, 1);
            char buf[512];
            snprintf(buf, sizeof(buf), "RNTO %s\r\n", to);
            ftp_pi_sendCommand(ftp, buf);
        }
    }

    // 230：Login 成功
    if (code == 230) {
        if (ftp->m_currentCommand && ftp->m_currentCommand->m_command == XFtpCommand_Login) {
            ftp->m_state = XFtp_State_LoggedIn;
            XFtp_stateChanged_signal(ftp, ftp->m_state);
            ftp_pi_finishCommand(ftp, code, text);
        }
    }
}

static void ftp_pi_handleError(XFtp* ftp, int code, const char* text)
{
    if (!ftp) return;
    if (!ftp->m_currentCommand) return;  // 无命令在执行（如 ABOR 后的迟到 4xx/5xx）

    // 按响应码细化错误分类（RFC 959 + 常见扩展）
    // 4xx: 暂时否定完成（未完成，可重试）
    //   421 Service not available / 425 Can't open data connection
    //   426 Connection closed; transfer aborted / 430 Invalid username or password
    //   434 Requested host unavailable / 450 Requested file action not taken
    //   451/452 暂时错误，重试可能成功
    // 5xx: 永久否定完成（命令失败，不要重试）
    //   500/501 语法错 / 502/504 命令未实现 / 503 顺序错
    //   530 Not logged in / 532 Need account / 550-553 各种文件/动作失败
    int codeClass = code / 100;
    switch (code) {
    case 421: ftp->m_error = XFtp_Error_NetworkError; break;
    case 425: ftp->m_error = XFtp_Error_ActiveModeFailed; break;
    case 426: ftp->m_error = XFtp_Error_TransferAborted; break;
    case 430: ftp->m_error = XFtp_Error_AuthenticationError; break;
    case 434: ftp->m_error = XFtp_Error_HostNotFound; break;
    case 450:
    case 451:
    case 452: ftp->m_error = XFtp_Error_TransferFailed; break;
    case 500:
    case 501: ftp->m_error = XFtp_Error_InvalidResponse; break;
    case 502:
    case 503:
    case 504: ftp->m_error = XFtp_Error_CommandFailed; break;
    case 530: ftp->m_error = XFtp_Error_AuthenticationError; break;
    case 532: ftp->m_error = XFtp_Error_NotLoggedIn; break;
    case 550:
    case 551:
    case 552:
    case 553: ftp->m_error = XFtp_Error_CommandFailed; break;
    default:
        /* Socket handlers classify the error before entering this common
         * completion path.  Preserve that more specific classification. */
        if (code == 0 && ftp->m_error != XFtp_Error_NoError) break;
        if (codeClass == 4) ftp->m_error = XFtp_Error_TransferFailed;
        else if (codeClass == 5) ftp->m_error = XFtp_Error_CommandFailed;
        else ftp->m_error = XFtp_Error_ProtocolError;
        break;
    }
    if (text) {
        XString_assign_utf8(ftp->m_errorString, text);
    }

    // 重置 DTP 等待标志（错误可能发生在传输中途）
    if (ftp->m_d) {
        ftp->m_d->m_sm.m_restState = 0;
        ftp->m_d->m_sm.m_mlstState = 0;
        ftp->m_d->m_activeAcks = 0;
        ftp->m_d->m_waitForDtpToConnect = 0;
        ftp->m_d->m_waitForDtpToClose = 0;
    }

    // 发射命令完成信号（error=true）
    XFtp_commandFinished_signal(ftp, ftp->m_currentId, true);

    // 从队列移除当前命令并推进到下一个（否则队列卡死）
    XMutex_lock(ftp->m_commandMutex);
    if (ftp->m_pendingCommands && XVector_size_base(ftp->m_pendingCommands) > 0) {
        XFtpCommand* cmd = XVEC_GET(ftp->m_pendingCommands, 0, XFtpCommand);
        if (cmd) XFtpCommand_delete(cmd);
        XVector_remove_base(ftp->m_pendingCommands, 0, 1);
    }
    ftp->m_currentCommand = NULL;
    XMutex_unlock(ftp->m_commandMutex);

    if (!ftp->m_pendingCommands || XVector_size_base(ftp->m_pendingCommands) == 0) {
        XFtp_done_signal(ftp, true);
    } else {
        ftp_pi_startNextCommand(ftp);
    }
}

// =============== DTP 传输 ===============
static void ftp_dtp_connect(XFtp* ftp, const char* host, uint16_t port)
{
    if (!ftp) return;
    if (ftp->m_dtpSocket) {
        XAbstractSocket_disconnectFromHost_base(ftp->m_dtpSocket);
        XClass_delete_base((XClass*)ftp->m_dtpSocket);
        ftp->m_dtpSocket = NULL;
    }
    ftp->m_dtpSocket = (XAbstractSocket*)XSslSocket_create();
    if (!ftp->m_dtpSocket) return;
    XAbstractSocket_setProtocolTag(ftp->m_dtpSocket, "ftp-dtp-passive");
    ftp_connectDtpSocketSignals(ftp);
    if (ftp->m_d) {
        ftp->m_d->m_waitForDtpToConnect = 1;
        ftp->m_d->m_dtpState = 2;  // 正在连接
    }

    // 获取目标 IP：EPSV 时 host 为 NULL，使用 PI 连接的 peer IP
    const char* targetHost = host;
    char peerIp[64] = {0};
    if (!targetHost && ftp->m_piSocket) {
        const XHostAddress* peerAddr = XAbstractSocket_peerAddress(ftp->m_piSocket);
        if (peerAddr) {
            XString* addrStr = XHostAddress_toString(peerAddr);
            if (addrStr) {
                const char* s = XString_toUtf8(addrStr);
                if (s) {
                    size_t slen = strlen(s);
                    size_t cpLen = slen < sizeof(peerIp) - 1 ? slen : sizeof(peerIp) - 1;
                    memcpy(peerIp, s, cpLen);
                    peerIp[cpLen] = '\0';
                }
                XClass_delete_base((XClass*)addrStr);
            }
        }
        targetHost = peerIp[0] ? peerIp : "127.0.0.1";
    }

    XAbstractSocket_connectToHost_base(ftp->m_dtpSocket, targetHost, port,
        XIODevice_ReadWrite, XAbstractSocket_AnyIPProtocol);
}

static void ftp_dtp_startTransfer(XFtp* ftp)
{
    if (!ftp) return;

    // 清空读取缓冲
    if (ftp->m_readBuffer) {
        XByteArray_resize_base(ftp->m_readBuffer, 0);
    }
    ftp->m_transferCurrent = 0;

    if (!ftp->m_currentCommand) return;

    // List：DTP 已连接，发送 LIST/MLSD 命令到 PI 控制通道
    if (ftp->m_currentCommand->m_command == XFtpCommand_List) {
        if (ftp->m_currentCommand->m_data) {
            char buf[600];
            const char* cmdline = XString_toUtf8((XString*)ftp->m_currentCommand->m_data);
            if (cmdline) {
                size_t len = strlen(cmdline);
                if (len > sizeof(buf) - 4) len = sizeof(buf) - 4;
                memcpy(buf, cmdline, len);
                buf[len] = '\r';
                buf[len+1] = '\n';
                buf[len+2] = '\0';
                ftp_pi_sendCommand(ftp, buf);
            }
        }
        return;
    }
// Put：DTP 已连接，发送 STOR/APPE 命令到 PI（数据在 150 响应后写入）（数据在 150 响应后写入）
    if (ftp->m_currentCommand->m_command == XFtpCommand_Put) {
        if (ftp->m_currentCommand->m_rawCmds && XVector_size_base(ftp->m_currentCommand->m_rawCmds) > 0) {
            const char* file = XVEC_STR_AT(ftp->m_currentCommand->m_rawCmds, 0);
            char cmdStr[512];
            const char* verb = ftp->m_currentCommand->m_append ? "APPE" : "STOR";
            snprintf(cmdStr, sizeof(cmdStr), "%s %s\r\n", verb, file);
            ftp_pi_sendCommand(ftp, cmdStr);
        }
        return;
    }
// Get：DTP 已连接，发送 REST（如有 offset）或直接发 RETR
    if (ftp->m_currentCommand->m_command == XFtpCommand_Get) {
        // 断点续传：先 REST，350 响应后由 ftp_pi_handleIntermediateReply 发 RETR
        if (ftp->m_currentCommand->m_restOffset > 0 && ftp->m_d) {
            if (ftp->m_d->m_sm.m_restState == 0) {
                char cmdStr[64];
                snprintf(cmdStr, sizeof(cmdStr), "REST %lld\r\n",
                         (long long)ftp->m_currentCommand->m_restOffset);
                ftp->m_d->m_sm.m_restState = 1;
                ftp_pi_sendCommand(ftp, cmdStr);
                return;  // 等 350 响应
            }
        }
        // 无 REST 或 REST 已完成，直接发 RETR
        if (ftp->m_currentCommand->m_rawCmds && XVector_size_base(ftp->m_currentCommand->m_rawCmds) > 0) {
            const char* file = XVEC_STR_AT(ftp->m_currentCommand->m_rawCmds, 0);
            if (file) {
                char cmdStr[512];
                snprintf(cmdStr, sizeof(cmdStr), "RETR %s\r\n", file);
                ftp_pi_sendCommand(ftp, cmdStr);
            }
        }
        return;
    }
}

static void ftp_dtp_finishTransfer(XFtp* ftp)
{
    if (!ftp) return;

    // ABOR 检查：用户主动中断
    if (ftp->m_abortRequested) {
        ftp->m_abortRequested = 0;
        ftp->m_error = XFtp_Error_TransferAborted;
        XString_assign_utf8(ftp->m_errorString, "Transfer aborted by user");
    }

    /* 226 vs DTP 关闭顺序处理：Put/Get/List 的完成需要"PI 226 + DTP 全部数据读完"两个条件都满足。
     * ftp_pi_finishCommand 收到 226 时只置 m_piAckedTransfer=1，并调本函数触发真正的 finish。
     * DTP 关闭（disconnected_handler 或 readyRead read=0）也会调本函数。
     * 真正 finish 条件：
     *   m_piAckedTransfer==1（226 已到） &&
     *   m_dtpState==0（DTP 已关） &&
     *   (m_transferTotal == -1 [GET/List 未知大小] ||
     *    m_transferCurrent >= m_transferTotal [PUT 已知大小])。
     *  - PUT 路径：client 写完主动 disconnect DTP，server 关 DTP 后立刻发 226，
     *    226 与 DTP 关闭几乎同时，三个条件都满足。
     *  - GET/List 路径：server sendall + close DTP + 226，
     *    226 到达时 m_transferCurrent 可能 < 实际 DTP 总字节数（client 还在读 DTP 数据），
     *    finishTransfer 等 DTP 关闭（disconnected_handler 设 m_dtpState=0）才 finish。
     *    m_dtpState==0 触发的 finishTransfer 调用是 m_dtpState 检查的源头，所以
     *    disconnected_handler 路径调用时 m_dtpState 已经被设 0，但 m_piAckedTransfer 可能还是 0
     *    （226 还没到），等 226 到达后 finishCommand 调 finishTransfer 才 finish。 */
    /* did_finish 标志：是否真的走完 finish 路径。
     * 关键：之前 finishTransfer 的 DTP disconnect 在 return 路径也执行（line 1786 不在 if 内），
     * 导致 GET 8KB 读完后 read=0 触发 finishTransfer return，return 之前 DTP 被关掉，
     * 后续 254KB 数据丢失。改为：只有 did_finish=true 才 close DTP。 */
    bool did_finish = false;
    if (ftp->m_currentCommand) {
        XFtpCommand_Type ct = ftp->m_currentCommand->m_command;
        if (ct == XFtpCommand_Put || ct == XFtpCommand_Get || ct == XFtpCommand_List) {
            if (ftp->m_d && ftp->m_d->m_piAckedTransfer == 0) {
                return;  /* 226 还没到，等 finishCommand 收到 226 后再调一次 */
            }
            /* 关键：Test 10 GET 256KB 修复。
             * 之前实现中 226 到达时 m_dtpState=1（DTP 还没关，248KB 数据未读）就直接 finish，
             * 导致 DTP 被关、剩余数据丢失。必须等 DTP 真正关闭（m_dtpState=0）才能 finish。
             * 三个触发点：readyRead read=0、disconnected_handler、ftp_pi_finishCommand 226 路径。 */
            if (ftp->m_d && ftp->m_d->m_dtpState != 0) {
                return;  /* DTP 还没关（disconnected 还没发生），等 disconnected_handler 触发 */
            }
            /* Get/List 路径：m_transferTotal=-1（未知），不检查 m_transferCurrent。*/
            if (ftp->m_transferTotal > 0 && ftp->m_transferCurrent < ftp->m_transferTotal) {
                return;  /* DTP 数据还没全写完（PUT 路径），等 readyRead/bytesWritten 累加到 total */
            }
            if (ftp->m_d) ftp->m_d->m_piAckedTransfer = 0;

            /* Parse LIST/MLSD while the command and its transfer buffer are
             * still alive.  Command completion below deletes both. */
            if (ct == XFtpCommand_List) {
                const char* data = (const char*)XByteArray_data(ftp->m_readBuffer);
                int64_t size = XByteArray_size_base(ftp->m_readBuffer);
                if (data && size > 0) {
                    int64_t start = 0;
                    for (int64_t i = 0; i <= size; i++) {
                        if (i == size || data[i] == '\n' || data[i] == '\0') {
                            if (i > start) {
                                char line[512];
                                int copyLen = (int)(i - start);
                                if (copyLen >= (int)sizeof(line)) copyLen = (int)sizeof(line) - 1;
                                memcpy(line, data + start, copyLen);
                                line[copyLen] = '\0';
                                trim_crlf(line);
                                if (line[0]) ftp_parse_list_line(ftp, line);
                            }
                            start = i + 1;
                        }
                    }
                }
                ftp->m_dirListingActive = 0;
            }

            /* 发射命令完成信号（与 ftp_pi_finishCommand 同样规则） */
            XFtp_commandFinished_signal(ftp, ftp->m_currentId,
                                        ftp->m_error != XFtp_Error_NoError);
            /* 从队列移除第一个命令 */
            XMutex_lock(ftp->m_commandMutex);
            if (ftp->m_pendingCommands && XVector_size_base(ftp->m_pendingCommands) > 0) {
                XFtpCommand* cmd = XVEC_GET(ftp->m_pendingCommands, 0, XFtpCommand);
                if (cmd) XFtpCommand_delete(cmd);
                XVector_remove_base(ftp->m_pendingCommands, 0, 1);
            }
            ftp->m_currentCommand = NULL;
            XMutex_unlock(ftp->m_commandMutex);
            /* 检查是否所有命令完成 */
            if (!ftp->m_pendingCommands || XVector_size_base(ftp->m_pendingCommands) == 0) {
                XFtp_done_signal(ftp, ftp->m_error != XFtp_Error_NoError);
            } else {
                ftp_pi_startNextCommand(ftp);
            }
            did_finish = true;  /* 走完 finish 路径，标志置 true，下面会关 DTP */
        }
    }

    // 关闭 DTP（只 disconnect 不 delete，避免在信号回调内析构导致堆破坏或死锁；
    // 真正的 delete 在下次 ftp_dtp_connect 时统一清理）
    // 关键：只有 did_finish=true 才关闭 DTP。否则 GET 8KB 读完后 finishTransfer return
    // 但 m_currentCommand 仍非 NULL（DTP 数据未读完），此时不能关 DTP（后续 254KB 数据丢失）。
    if (did_finish) {
        if (ftp->m_dtpSocket) {
            XAbstractSocket_disconnectFromHost_base(ftp->m_dtpSocket);
        }
        if (ftp->m_dtpServer) {
            // 注意：不能 XClass_delete_base m_dtpServer！它的 pendingConnections 中
            // 已经交给 m_dtpSocket 的连接会被联动清理，导致后续访问 m_dtpSocket
            // 时 vtable 失效 crash。只关闭监听端口，置 NULL，下次主动传输重建。
            XTcpServer_close(ftp->m_dtpServer);
            ftp->m_dtpServer = NULL;
        }
    }
}

static int ftp_dtp_startListen(XFtp* ftp, char* portCmd, size_t portCmdSize)
{
	if (!ftp || !ftp->m_dtpServer || !portCmd || portCmdSize < 64) return -1;

	/* Already listening: reuse existing port (no re-listen) */
	if (!XTcpServer_isListening(ftp->m_dtpServer)) {
		if (!XTcpServer_listen(ftp->m_dtpServer, NULL, 0)) {
			return -1;
		}
	}

	uint16_t port = XTcpServer_serverPort(ftp->m_dtpServer);
	if (port == 0) return -1;

	/* Get PI socket local IP for PORT command. Fallback 127.0.0.1 */
	char ipStr[16] = "127,0,0,1";
	if (ftp->m_piSocket) {
		const XHostAddress* localAddr = XAbstractSocket_localAddress(ftp->m_piSocket);
		if (localAddr) {
			XString* addrStr = XHostAddress_toString(localAddr);
			if (addrStr) {
				const char* utf8 = XString_toUtf8(addrStr);
				if (utf8 && utf8[0]) {
					strncpy(ipStr, utf8, sizeof(ipStr) - 1);
					ipStr[sizeof(ipStr) - 1] = '\0';
					/* "." -> "," (PORT format requires comma) */
					for (char* p = ipStr; *p; p++) {
						if (*p == '.') *p = ',';
					}
				}
				XClass_delete_base((XClass*)addrStr);
			}
		}
	}

	int p1 = (port >> 8) & 0xFF;
	int p2 = port & 0xFF;
	snprintf(portCmd, portCmdSize, "PORT %s,%d,%d\r\n", ipStr, p1, p2);
	return 0;
}

// =============== LIST 解析 ===============
// =============== LIST 解析 ===============
// 解析 MLSD facts 字符串 (RFC 3659)：modify=20240101120000.123;type=dir;size=1024; ...
// 返回是否成功解析。*pModifyTime 输出 Unix 时间戳（秒）
static bool ftp_parse_mlsd_facts(const char* facts, char* name, size_t nameSize,
                                  int64_t* pSize, uint8_t* pType,
                                  int64_t* pModifyTime)
{
    if (!facts || !name || nameSize == 0) return false;
    *pSize = -1;
    *pType = 0;  // 0=file
    *pModifyTime = 0;

    const char* p = facts;
    while (*p && *p != ' ') {
        const char* eq = strchr(p, '=');
        if (!eq) break;
        const char* semi = strchr(eq, ';');
        size_t keyLen = (size_t)(eq - p);
        size_t valLen = semi ? (size_t)(semi - eq - 1) : strlen(eq + 1);

        if (keyLen == 4 && memcmp(p, "name", 4) == 0) {
            size_t cpLen = valLen < nameSize - 1 ? valLen : nameSize - 1;
            memcpy(name, eq + 1, cpLen);
            name[cpLen] = '\0';
        } else if (keyLen == 4 && memcmp(p, "size", 4) == 0) {
            char tmp[32] = {0};
            size_t cp = valLen < 31 ? valLen : 31;
            memcpy(tmp, eq + 1, cp);
            *pSize = (int64_t)strtoll(tmp, NULL, 10);
        } else if (keyLen == 4 && memcmp(p, "type", 4) == 0) {
            if (valLen == 3 && memcmp(eq + 1, "dir", 3) == 0) *pType = 1;
            else if (valLen == 4 && memcmp(eq + 1, "cdir", 4) == 0) *pType = 1;
            else if (valLen == 4 && memcmp(eq + 1, "pdir", 4) == 0) *pType = 1;
            else if (valLen == 3 && memcmp(eq + 1, "OS.3", 3) == 0); // OS/2
        } else if (keyLen == 6 && memcmp(p, "modify", 6) == 0) {
            // YYYYMMDDHHMMSS[.sss]
            char tmp[20] = {0};
            size_t cp = valLen < 14 ? valLen : 14;
            memcpy(tmp, eq + 1, cp);
            // 简化：解析 YYYYMMDDHHMMSS
            if (cp >= 14) {
                struct tm t = {0};
                char year[5] = {0}, mon[3] = {0}, day[3] = {0};
                memcpy(year, tmp, 4);
                memcpy(mon, tmp + 4, 2);
                memcpy(day, tmp + 6, 2);
                t.tm_year = atoi(year) - 1900;
                t.tm_mon = atoi(mon) - 1;
                t.tm_mday = atoi(day);
                t.tm_hour = (tmp[8] - '0') * 10 + (tmp[9] - '0');
                t.tm_min = (tmp[10] - '0') * 10 + (tmp[11] - '0');
                t.tm_sec = (tmp[12] - '0') * 10 + (tmp[13] - '0');
                *pModifyTime = (int64_t)mktime(&t);
            }
        }

        if (!semi) break;
        p = semi + 1;
        while (*p == ' ') p++;
    }
    return name[0] != '\0';
}

// 解析 EPLF 格式（|+12345|rwxr-xr-x|...|filename）
static bool ftp_parse_eplf(const char* line, char* name, size_t nameSize,
                            int64_t* pSize)
{
    if (!line || line[0] != '+' || line[1] == '|') return false;
    // 找第一个 |
    const char* p1 = strchr(line, '|');
    if (!p1) return false;
    // 第二个 | 之前的字段是 size（可选）
    if (p1[1] != '|' && p1[1] != ',') {
        char tmp[32] = {0};
        size_t len = (size_t)(p1 - line - 1);
        if (len > 31) len = 31;
        memcpy(tmp, line + 1, len);
        *pSize = (int64_t)strtoll(tmp, NULL, 10);
    }
    // 文件名在最后一个 | 之后
    const char* last = strrchr(line, '|');
    if (!last) return false;
    last++;
    if (*last == '\0') return false;
    size_t nlen = strlen(last);
    if (nlen >= nameSize) nlen = nameSize - 1;
    memcpy(name, last, nlen);
    name[nlen] = '\0';
    // 去掉 \r\n
    while (nlen > 0 && (name[nlen-1] == '\r' || name[nlen-1] == '\n')) {
        name[--nlen] = '\0';
    }
    return name[0] != '\0';
}

// 解析 Unix ls -l 格式：drwxr-xr-x  2 user group  1024 Jan 01 12:00 filename
static bool ftp_parse_unix_ls(const char* line, char* name, size_t nameSize,
                               int64_t* pSize, uint8_t* pType)
{
    char perm[16] = {0};
    int linkCount = 0;
    char owner[64] = {0};
    char group[64] = {0};
    int64_t size = 0;
    char month[16] = {0};
    char day[8] = {0};
    char yearOrTime[16] = {0};
    char tmpName[256] = {0};

    int fields = sscanf(line, "%15s %d %63s %63s %lld %15s %7s %15s %255s",
                        perm, &linkCount, owner, group, &size,
                        month, day, yearOrTime, tmpName);
    if (fields < 9) {
        fields = sscanf(line, "%15s %d %63s %63s %lld %255[^\n]",
                        perm, &linkCount, owner, group, &size, tmpName);
        if (fields < 6) return false;
    }
    size_t nlen = strlen(tmpName);
    if (nlen >= nameSize) nlen = nameSize - 1;
    memcpy(name, tmpName, nlen);
    name[nlen] = '\0';
    // 去掉 "link -> target" 中的 " -> xxx"
    char* arrow = strstr(name, " -> ");
    if (arrow) *arrow = '\0';
    // 去掉 \r
    nlen = strlen(name);
    while (nlen > 0 && (name[nlen-1] == '\r' || name[nlen-1] == '\n')) {
        name[--nlen] = '\0';
    }
    *pSize = size;
    if (perm[0] == 'd') *pType = 1;
    else if (perm[0] == 'l') *pType = 2;
    else *pType = 0;
    return true;
}

// 解析 Windows NT 格式：01-01-24  12:00AM       1024 filename
static bool ftp_parse_windows_nt(const char* line, char* name, size_t nameSize,
                                  int64_t* pSize, uint8_t* pType)
{
    char date[16] = {0};
    char time[16] = {0};
    char dirOrSize[8] = {0};
    int64_t size = 0;
    char tmpName[256] = {0};

    int fields = sscanf(line, "%15s %15s %7s %lld %255[^\n]",
                        date, time, dirOrSize, &size, tmpName);
    if (fields < 5) {
        // 简化版：directory 时 size 字段是 "<DIR>"
        fields = sscanf(line, "%15s %15s %7s %255[^\n]",
                        date, time, dirOrSize, tmpName);
        if (fields < 4) return false;
        size = -1;
    }
    size_t nlen = strlen(tmpName);
    if (nlen >= nameSize) nlen = nameSize - 1;
    memcpy(name, tmpName, nlen);
    name[nlen] = '\0';
    nlen = strlen(name);
    while (nlen > 0 && (name[nlen-1] == '\r' || name[nlen-1] == '\n')) {
        name[--nlen] = '\0';
    }
    *pSize = size;
    *pType = (dirOrSize[0] == '<' || dirOrSize[0] == 'D' || dirOrSize[0] == 'd') ? 1 : 0;
    return true;
}

static void ftp_parse_list_line(XFtp* ftp, const char* line)
{
    if (!ftp || !line || !ftp->m_listInfo) return;

    char name[256] = {0};
    int64_t size = 0;
    uint8_t type = 0;     // 0=file, 1=dir, 2=symlink
    int64_t modifyTime = 0;
    bool parsed = false;

    // MLSD 格式：facts 段在第一个空格前
    if (strchr(line, '=') && strchr(line, ';')) {
        const char* sp = strchr(line, ' ');
        if (sp) {
            char facts[1024] = {0};
            size_t flen = (size_t)(sp - line);
            if (flen > 1023) flen = 1023;
            memcpy(facts, line, flen);
            facts[flen] = '\0';
            while (*sp == ' ') sp++;
            size_t nameLen = strlen(sp);
            if (nameLen >= sizeof(name)) nameLen = sizeof(name) - 1;
            memcpy(name, sp, nameLen);
            name[nameLen] = '\0';
            trim_crlf(name);
            parsed = ftp_parse_mlsd_facts(facts, name, sizeof(name),
                                          &size, &type, &modifyTime);
        }
    }
    // EPLF 格式：以 + 开头
    if (!parsed && line[0] == '+') {
        parsed = ftp_parse_eplf(line, name, sizeof(name), &size);
    }
    // Unix ls 格式：以权限位开头
    if (!parsed && (line[0] == 'd' || line[0] == '-' || line[0] == 'l' ||
                    line[0] == 'b' || line[0] == 'c' || line[0] == 'p' ||
                    line[0] == 's')) {
        parsed = ftp_parse_unix_ls(line, name, sizeof(name), &size, &type);
    }
    // Windows NT 格式：MM-DD-YY 开头
    if (!parsed && line[0] >= '0' && line[0] <= '9') {
        parsed = ftp_parse_windows_nt(line, name, sizeof(name), &size, &type);
    }
    if (!parsed) return;

    XFileInfo* info = XFileInfo_create_1();
    if (!info) return;

    if (info->m_filePath) {
        XString_delete_base(info->m_filePath);
        info->m_filePath = NULL;
    }
    info->m_filePath = XString_create_utf8(name);
    info->m_stat.size = size >= 0 ? size : 0;
    info->m_stat.isFile = (type == 0) ? 1 : 0;
    info->m_stat.isDir  = (type == 1) ? 1 : 0;
    info->m_stat.isSymLink = (type == 2) ? 1 : 0;
    info->m_stat.exists = 1;
    if (modifyTime > 0) {
        info->m_stat.modificationTime = modifyTime;
    }

    XVector_push_back_1_base(ftp->m_listInfo, &info);
    XFtp_listInfo_signal(ftp, info);
}

// =============== 状态查询 ===============
XFtp_State XFtp_state(const XFtp* ftp)
{
    return ftp ? ftp->m_state : XFtp_State_Unconnected;
}

XFtp_Error XFtp_error(const XFtp* ftp)
{
    return ftp ? ftp->m_error : XFtp_Error_NoError;
}

const char* XFtp_errorString(const XFtp* ftp)
{
    if (!ftp || !ftp->m_errorString) return "";
    const char* s = XString_toUtf8(ftp->m_errorString);
    return s ? s : "";
}

int XFtp_currentId(const XFtp* ftp)
{
    return ftp ? ftp->m_currentId : 0;
}

XFtpCommand_Type XFtp_currentCommand(const XFtp* ftp)
{
    if (!ftp || !ftp->m_currentCommand) return XFtpCommand_None;
    return ftp->m_currentCommand->m_command;
}

bool XFtp_hasPendingCommands(const XFtp* ftp)
{
    if (!ftp || !ftp->m_pendingCommands) return false;
    return XVector_size_base(ftp->m_pendingCommands) > 0;
}

// =============== API ===============
static int XFtp_nextId(XFtp* ftp)
{
    static int s_id = 1;
    (void)ftp;
    return s_id++;
}

static void XFtp_enqueue(XFtp* ftp, XFtpCommand* cmd)
{
    if (!ftp || !cmd) return;
    XMutex_lock(ftp->m_commandMutex);
    /* 推入 cmd 指针本身（不是 &cmd，&cmd 是局部变量地址） */
    XVector_push_back_1_base(ftp->m_pendingCommands, &cmd);
    XMutex_unlock(ftp->m_commandMutex);
}

int XFtp_connectToHost(XFtp* ftp, const char* host, uint16_t port)
{
    if (!ftp || !host) return -1;
    int id = XFtp_nextId(ftp);
    XFtpCommand* cmd = XFtpCommand_create(id, XFtpCommand_ConnectToHost);
    if (!cmd) return -1;
    char portStr[16];
    snprintf(portStr, sizeof(portStr), "%u", port);
    XFtpCommand_addRawArg(cmd, host);
    XFtpCommand_addRawArg(cmd, portStr);
    if (ftp->m_host) XClass_delete_base((XClass*)ftp->m_host);
    ftp->m_host = XString_create_utf8(host);
    ftp->m_port = port;
    ftp->m_state = XFtp_State_Connecting;
    XFtp_stateChanged_signal(ftp, ftp->m_state);
    ftp->m_reconnectAttempts = 0;
    XFtp_enqueue(ftp, cmd);
    ftp_pi_startNextCommand(ftp);
    return id;
}

int XFtp_login(XFtp* ftp, const char* user, const char* password)
{
    if (!ftp) return -1;
    if (ftp->m_state == XFtp_State_Unconnected) return -1;
    int id = XFtp_nextId(ftp);
    XFtpCommand* cmd = XFtpCommand_create(id, XFtpCommand_Login);
    if (!cmd) return -1;
    XFtpCommand_addRawArg(cmd, user ? user : "anonymous");
    XFtpCommand_addRawArg(cmd, password ? password : "");
    if (ftp->m_user) XClass_delete_base((XClass*)ftp->m_user);
    ftp->m_user = XString_create_utf8(user ? user : "anonymous");
    if (password && ftp->m_password) XClass_delete_base((XClass*)ftp->m_password);
    if (password) ftp->m_password = XString_create_utf8(password);
    XFtp_enqueue(ftp, cmd);
    ftp_pi_startNextCommand(ftp);
    return id;
}

int XFtp_list(XFtp* ftp, const char* dir)
{
    if (!ftp) return -1;
    int id = XFtp_nextId(ftp);
    XFtpCommand* cmd = XFtpCommand_create(id, XFtpCommand_List);
    if (dir && dir[0]) XFtpCommand_addRawArg(cmd, dir);
    XFtp_enqueue(ftp, cmd);
    ftp_pi_startNextCommand(ftp);
    return id;
}

int XFtp_get(XFtp* ftp, const char* file, void* device, int type)
{
    if (!ftp || !file) return -1;
    int id = XFtp_nextId(ftp);
    XFtpCommand* cmd = XFtpCommand_create(id, XFtpCommand_Get);
    XFtpCommand_addRawArg(cmd, file);
    cmd->m_device = device;
    cmd->m_openMode = type;
    XFtp_enqueue(ftp, cmd);
    ftp_pi_startNextCommand(ftp);
    return id;
}

int XFtp_get_resume(XFtp* ftp, const char* file, void* device, int64_t offset, int type)
{
    if (!ftp || !file) return -1;
    int id = XFtp_nextId(ftp);
    XFtpCommand* cmd = XFtpCommand_create(id, XFtpCommand_Get);
    XFtpCommand_addRawArg(cmd, file);
    cmd->m_device = device;
    cmd->m_openMode = type;
    cmd->m_restOffset = offset;
    XFtp_enqueue(ftp, cmd);
    ftp_pi_startNextCommand(ftp);
    return id;
}

static int ftp_put_internal(XFtp* ftp, const char* file, const void* data, int64_t size, uint8_t append)
{
    if (!ftp || !file) return -1;
    int id = XFtp_nextId(ftp);
    XFtpCommand* cmd = XFtpCommand_create(id, XFtpCommand_Put);
    XFtpCommand_addRawArg(cmd, file);
    if (data && size > 0) {
        cmd->m_data = XByteArray_create();
        XByteArray_resize_base(cmd->m_data, size);
        memcpy(XByteArray_data(cmd->m_data), data, size);
    }
    cmd->m_append = append;
    XFtp_enqueue(ftp, cmd);
    ftp_pi_startNextCommand(ftp);
    return id;
}


int XFtp_put(XFtp* ftp, const char* file, const void* data, int64_t size)
{
    return ftp_put_internal(ftp, file, data, size, 0);
}

int XFtp_put_append(XFtp* ftp, const char* file, const void* data, int64_t size)
{
    return ftp_put_internal(ftp, file, data, size, 1);
}

int XFtp_remove(XFtp* ftp, const char* file)
{
    if (!ftp || !file) return -1;
    int id = XFtp_nextId(ftp);
    XFtpCommand* cmd = XFtpCommand_create(id, XFtpCommand_Remove);
    XFtpCommand_addRawArg(cmd, file);
    XFtp_enqueue(ftp, cmd);
    ftp_pi_startNextCommand(ftp);
    return id;
}

int XFtp_rename(XFtp* ftp, const char* oldname, const char* newname)
{
    if (!ftp || !oldname || !newname) return -1;
    int id = XFtp_nextId(ftp);
    XFtpCommand* cmd = XFtpCommand_create(id, XFtpCommand_Rename);
    XFtpCommand_addRawArg(cmd, oldname);
    XFtpCommand_addRawArg(cmd, newname);
    XFtp_enqueue(ftp, cmd);
    ftp_pi_startNextCommand(ftp);
    return id;
}

int XFtp_mkdir(XFtp* ftp, const char* dir)
{
    if (!ftp || !dir) return -1;
    int id = XFtp_nextId(ftp);
    XFtpCommand* cmd = XFtpCommand_create(id, XFtpCommand_Mkdir);
    XFtpCommand_addRawArg(cmd, dir);
    XFtp_enqueue(ftp, cmd);
    ftp_pi_startNextCommand(ftp);
    return id;
}

int XFtp_rmdir(XFtp* ftp, const char* dir)
{
    if (!ftp || !dir) return -1;
    int id = XFtp_nextId(ftp);
    XFtpCommand* cmd = XFtpCommand_create(id, XFtpCommand_Rmdir);
    XFtpCommand_addRawArg(cmd, dir);
    XFtp_enqueue(ftp, cmd);
    ftp_pi_startNextCommand(ftp);
    return id;
}

int XFtp_cd(XFtp* ftp, const char* dir)
{
    if (!ftp || !dir) return -1;
    int id = XFtp_nextId(ftp);
    XFtpCommand* cmd = XFtpCommand_create(id, XFtpCommand_Cd);
    XFtpCommand_addRawArg(cmd, dir);
    XFtp_enqueue(ftp, cmd);
    ftp_pi_startNextCommand(ftp);
    return id;
}

int XFtp_cdup(XFtp* ftp)
{
    return XFtp_cd(ftp, "..");
}

int XFtp_size(XFtp* ftp, const char* file)
{
    if (!ftp || !file) return -1;
    if (ftp->m_state != XFtp_State_LoggedIn) return -1;
    int id = XFtp_nextId(ftp);
    XFtpCommand* cmd = XFtpCommand_create(id, XFtpCommand_Size);
    if (!cmd) return -1;
    XFtpCommand_addRawArg(cmd, file);
    XFtp_enqueue(ftp, cmd);
    ftp_pi_startNextCommand(ftp);
    return id;
}

int XFtp_mdtm(XFtp* ftp, const char* file)
{
    if (!ftp || !file) return -1;
    if (ftp->m_state != XFtp_State_LoggedIn) return -1;
    int id = XFtp_nextId(ftp);
    XFtpCommand* cmd = XFtpCommand_create(id, XFtpCommand_Mdtm);
    if (!cmd) return -1;
    XFtpCommand_addRawArg(cmd, file);
    XFtp_enqueue(ftp, cmd);
    ftp_pi_startNextCommand(ftp);
    return id;
}

int XFtp_mlst(XFtp* ftp, const char* file)
{
    if (!ftp) return -1;
    if (ftp->m_state != XFtp_State_LoggedIn) return -1;
    int id = XFtp_nextId(ftp);
    XFtpCommand* cmd = XFtpCommand_create(id, XFtpCommand_Mlst);
    if (!cmd) return -1;
    if (file && file[0]) XFtpCommand_addRawArg(cmd, file);
    XFtp_enqueue(ftp, cmd);
    ftp_pi_startNextCommand(ftp);
    return id;
}

int XFtp_rawCommand(XFtp* ftp, const char* command)
{
    if (!ftp || !command) return -1;
    int id = XFtp_nextId(ftp);
    XFtpCommand* cmd = XFtpCommand_create(id, XFtpCommand_RawCommand);
    if (cmd->m_rawCmd) {
        // 自动追加 \r\n（如果没有）
        XString_assign_utf8(cmd->m_rawCmd, command);
        size_t len = strlen(command);
        if (len < 2 || command[len-2] != '\r' || command[len-1] != '\n') {
            XString_append_utf8(cmd->m_rawCmd, "\r\n");
        }
    }
    if (ftp->m_d) ftp->m_d->m_rawCommand = 1;
    XFtp_enqueue(ftp, cmd);
    ftp_pi_startNextCommand(ftp);
    return id;
}

int XFtp_close(XFtp* ftp)
{
    if (!ftp) return -1;
    int id = XFtp_nextId(ftp);
    XFtpCommand* cmd = XFtpCommand_create(id, XFtpCommand_Close);
    XFtp_enqueue(ftp, cmd);
    ftp_pi_startNextCommand(ftp);
    return id;
}

void XFtp_abort(XFtp* ftp)
{
    if (!ftp) return;
    XMutex_lock(ftp->m_commandMutex);
    if (ftp->m_pendingCommands) {
        for (size_t i = 0; i < XVector_size_base(ftp->m_pendingCommands); i++) {
            XFtpCommand* cmd = XVEC_GET(ftp->m_pendingCommands, i, XFtpCommand);
            if (cmd) XFtpCommand_delete(cmd);
        }
        XVector_clear_base(ftp->m_pendingCommands);
    }
    ftp->m_currentCommand = NULL;
    XMutex_unlock(ftp->m_commandMutex);

    if (ftp->m_dtpSocket) XAbstractSocket_disconnectFromHost_base(ftp->m_dtpSocket);

    XFtp_done_signal(ftp, true);
}

int XFtp_connectToUrl(XFtp* ftp, const char* url)
{
    if (!ftp || !url) return -1;
    /* XUrl_create_ex 接收 const XString*，先把 char* 包成 XString 再传 */
    XString* urlStr = XString_create_utf8(url);
    if (!urlStr) return -1;
    XUrl* parsed = XUrl_create_ex(urlStr, XUrl_TolerantMode);
    XString_delete_base(urlStr);
    if (!parsed) return -1;

    const XString* schemeStr = XUrl_scheme_const(parsed);
    const char* scheme = schemeStr ? XString_toUtf8(schemeStr) : NULL;
    if (scheme && scheme[0] && strcmp(scheme, "ftp") != 0) {
        XClass_delete_base((XClass*)parsed);
        return -1;
    }

    const XString* hostStr = XUrl_host_const(parsed);
    const XString* userNameStr = XUrl_userName_const(parsed);
    const XString* passwordStr = XUrl_password_const(parsed);
    const XString* pathStr = XUrl_path_const(parsed);
    const char* host = hostStr ? XString_toUtf8(hostStr) : NULL;
    const char* userName = userNameStr ? XString_toUtf8(userNameStr) : NULL;
    const char* password = passwordStr ? XString_toUtf8(passwordStr) : NULL;
    const char* path = pathStr ? XString_toUtf8(pathStr) : NULL;
    int port = XUrl_port(parsed);

    if (!host || !host[0]) {
        XClass_delete_base((XClass*)parsed);
        return -1;
    }

    uint16_t actualPort = (port > 0) ? (uint16_t)port : 21;

    // 1. 连接主机
    int id = XFtp_connectToHost(ftp, host, actualPort);
    if (id < 0) {
        XClass_delete_base((XClass*)parsed);
        return -1;
    }

    // 2. 登录（如果有 user 信息）
    if (userName && userName[0]) {
        int loginId = XFtp_login(ftp, userName, password);
        if (loginId < 0) {
            XClass_delete_base((XClass*)parsed);
            return -1;
        }
    }

    // 3. 切到 path（如果有）
    if (path && path[0]) {
        const char* p = path;
        while (*p == '/') p++;
        if (*p) {
            int cdId = XFtp_cd(ftp, p);
            if (cdId < 0) {
                // CD 失败不影响主要流程
            }
        }
    }

    XClass_delete_base((XClass*)parsed);
    return id;
}

// =============== 配置 ===============
void XFtp_setTransferMode(XFtp* ftp, XFtp_TransferMode mode)
{
    if (ftp) ftp->m_transferMode = mode;
}

XFtp_TransferMode XFtp_transferMode(const XFtp* ftp)
{
    return ftp ? ftp->m_transferMode : XFtp_TransferMode_Passive;
}

XFtp_DataType XFtp_transferType(const XFtp* ftp)
{
    return ftp ? ftp->m_transferType : XFtp_DataType_Binary;
}

void XFtp_setTransferType(XFtp* ftp, XFtp_DataType type)
{
    if (!ftp) return;
    if (ftp->m_state != XFtp_State_Unconnected) return;
    ftp->m_transferType = type;
}

void XFtp_setUtf8(XFtp* ftp, bool enabled)
{
    if (!ftp) return;
    ftp->m_useUtf8 = enabled ? 1 : 0;
}

bool XFtp_isUtf8(const XFtp* ftp)
{
    return ftp && ftp->m_useUtf8;
}

void XFtp_setMlsdEnabled(XFtp* ftp, bool enabled)
{
    if (!ftp) return;
    ftp->m_useMlsd = enabled ? 1 : 0;
}

bool XFtp_supportsFeature(const XFtp* ftp, XFtp_Feature feature)
{
    if (!ftp) return false;
    return (ftp->m_features & (uint8_t)feature) != 0;
}

void XFtp_abortTransfer(XFtp* ftp)
{
    if (!ftp) return;
    if (ftp->m_dtpSocket || ftp->m_dtpServer) {
        ftp->m_abortRequested = 1;
        // 立即关闭 DTP socket，触发 ABOR 流程
        if (ftp->m_dtpSocket) {
            XAbstractSocket_abort(ftp->m_dtpSocket);
        }
        if (ftp->m_dtpServer) {
            XTcpServer_close(ftp->m_dtpServer);
        }
        // 在 PI 上发 ABOR
        if (ftp->m_piSocket) {
            XAbstractSocket_write(ftp->m_piSocket, "ABOR\r\n", 6);
        }
    }
}

void XFtp_setSsl(XFtp* ftp, bool useSsl)
{
    if (!ftp) return;
    if (ftp->m_state != XFtp_State_Unconnected) return;
    ftp->m_useSsl = useSsl;
}

void XFtp_setCompression(XFtp* ftp, bool useCompression)
{
    if (!ftp) return;
    if (ftp->m_state != XFtp_State_Unconnected) return;
    ftp->m_useCompression = useCompression;
}

void XFtp_setAutoReconnect(XFtp* ftp, bool autoReconnect, int intervalMs, int maxAttempts)
{
    if (!ftp) return;
    ftp->m_autoReconnect = autoReconnect;
    ftp->m_reconnectInterval = intervalMs > 0 ? intervalMs : 5000;
    ftp->m_maxReconnectAttempts = maxAttempts >= 0 ? maxAttempts : 3;
    ftp->m_reconnectAttempts = 0;
}

void XFtp_setProxy(XFtp* ftp, const char* host, uint16_t port)
{
    if (!ftp || !host) return;
    if (ftp->m_proxyHost) XClass_delete_base((XClass*)ftp->m_proxyHost);
    if (ftp->m_proxyUser) XClass_delete_base((XClass*)ftp->m_proxyUser);
    if (ftp->m_proxyPass) XClass_delete_base((XClass*)ftp->m_proxyPass);
    ftp->m_proxyHost = XString_create_utf8(host);
    ftp->m_proxyUser = NULL;
    ftp->m_proxyPass = NULL;
    ftp->m_proxyPort = port;
    ftp->m_proxyType = XFtp_ProxyType_Http;
    if (ftp->m_piSocket) {
        // XAbstractSocket_setProxy bug 已修：内部用 XString_create_copy 深拷贝
        XNetworkProxy* proxy = XNetworkProxy_create_2(XNetworkProxy_HttpProxy,
                                                       ftp->m_proxyHost, port, NULL, NULL);
        if (proxy) {
            XAbstractSocket_setProxy(ftp->m_piSocket, proxy);
            XClass_delete_base((XClass*)proxy);
        }
    }
}

void XFtp_setSocks5Proxy(XFtp* ftp, const char* host, uint16_t port,
                         const char* user, const char* password)
{
    if (!ftp || !host) return;
    if (ftp->m_proxyHost) XClass_delete_base((XClass*)ftp->m_proxyHost);
    if (ftp->m_proxyUser) XClass_delete_base((XClass*)ftp->m_proxyUser);
    if (ftp->m_proxyPass) XClass_delete_base((XClass*)ftp->m_proxyPass);
    ftp->m_proxyHost = XString_create_utf8(host);
    ftp->m_proxyUser = user ? XString_create_utf8(user) : NULL;
    ftp->m_proxyPass = password ? XString_create_utf8(password) : NULL;
    ftp->m_proxyPort = port;
    ftp->m_proxyType = XFtp_ProxyType_Socks5;
    if (ftp->m_piSocket) {
        XNetworkProxy* proxy = XNetworkProxy_create_2(XNetworkProxy_Socks5Proxy,
                                                       ftp->m_proxyHost, port,
                                                       ftp->m_proxyUser, ftp->m_proxyPass);
        if (proxy) {
            XAbstractSocket_setProxy(ftp->m_piSocket, proxy);
            XClass_delete_base((XClass*)proxy);
        }
    }
}

void XFtp_clearProxy(XFtp* ftp)
{
    if (!ftp) return;
    if (ftp->m_proxyHost) { XClass_delete_base((XClass*)ftp->m_proxyHost); ftp->m_proxyHost = NULL; }
    if (ftp->m_proxyUser) { XClass_delete_base((XClass*)ftp->m_proxyUser); ftp->m_proxyUser = NULL; }
    if (ftp->m_proxyPass) { XClass_delete_base((XClass*)ftp->m_proxyPass); ftp->m_proxyPass = NULL; }
    ftp->m_proxyPort = 0;
    ftp->m_proxyType = XFtp_ProxyType_None;
    if (ftp->m_piSocket) {
        // 修复后可以安全传 NULL
        XAbstractSocket_setProxy(ftp->m_piSocket, NULL);
    }
}

// =============== 信号 ===============
void* XFtp_stateChanged_signal(XFtp* ftp, XFtp_State state)
{
    XEmitSignal(ftp, XFtp_stateChanged_signal, XVarList_Create(XVar(XFtp_State, state)),
                NULL, NULL, XEVENT_PRIORITY_NORMAL);
}

void* XFtp_commandStarted_signal(XFtp* ftp, int id)
{
    XEmitSignal(ftp, XFtp_commandStarted_signal, XVarList_Create(XVar(int, id)),
                NULL, NULL, XEVENT_PRIORITY_NORMAL);
}

void* XFtp_commandFinished_signal(XFtp* ftp, int id, bool error)
{
    XEmitSignal(ftp, XFtp_commandFinished_signal,
                XVarList_Create(XVar(int, id), XVar(bool, error)),
                NULL, NULL, XEVENT_PRIORITY_NORMAL);
}

void* XFtp_listInfo_signal(XFtp* ftp, XFileInfo* info)
{
    XEmitSignal(ftp, XFtp_listInfo_signal,
                XVarList_Create(XVar(XFileInfo*, info)),
                NULL, NULL, XEVENT_PRIORITY_NORMAL);
}

void* XFtp_readyRead_signal(XFtp* ftp)
{
    XEmitSignal(ftp, XFtp_readyRead_signal, NULL, NULL, NULL, XEVENT_PRIORITY_NORMAL);
}

void* XFtp_dataTransferProgress_signal(XFtp* ftp, int64_t current, int64_t total)
{
    XEmitSignal(ftp, XFtp_dataTransferProgress_signal,
                XVarList_Create(XVar(int64_t, current), XVar(int64_t, total)),
                NULL, NULL, XEVENT_PRIORITY_NORMAL);
}

void* XFtp_rawCommandReply_signal(XFtp* ftp, int code, const char* reply)
{
    XEmitSignal(ftp, XFtp_rawCommandReply_signal,
                XVarList_Create(XVar(int, code), XVar(const char*, reply)),
                NULL, NULL, XEVENT_PRIORITY_NORMAL);
}

void* XFtp_done_signal(XFtp* ftp, bool error)
{
    XEmitSignal(ftp, XFtp_done_signal, XVarList_Create(XVar(bool, error)),
                NULL, NULL, XEVENT_PRIORITY_NORMAL);
}

#endif // XNETWORK_ON && XNETWORK_FTP_ON
