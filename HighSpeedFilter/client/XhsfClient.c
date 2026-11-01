/**
 * @file       XhsfClient.c
 * @brief      高速过滤器客户端实现（GUI 窗口 + TCP 上报）。
 * @details    XGuiApplication GUI 事件循环驱动；XTcpSocket 异步收发。
 *             接收缓冲按 '\n' 切行取 "r <值>" 更新结果标签。
 */
#include "XhsfClient.h"
#include "XhsfProtocol.h"

#include "XGuiApplication.h"
#include "XWidget.h"
#include "XLabel.h"
#include "XLineEdit.h"
#include "XPushButton.h"
#include "XTcpSocket.h"
#include "XObject.h"
#include "XPrintf.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

/* GUI 句柄（单窗口模板：模块级静态即可）。 */
static XWidget*    g_win;
static XLineEdit*  g_input;     /**< 采样值输入框。 */
static XLabel*     g_outLabel;  /**< 滤波结果显示。 */
static XLabel*     g_status;    /**< 连接状态。 */
static XTcpSocket* g_sock;      /**< 到服务端的连接。 */
static char        g_rxBuf[XHSF_CLIENT_RXBUF_SIZE]; /**< 接收行缓冲。 */
static int         g_rxLen;     /**< 行缓冲已积压字节数。 */

/** @brief 更新状态标签（UTF-8 文本）。 */
static void hsfSetStatus(const char* text)
{
    char buf[96];
    snprintf(buf, sizeof(buf), "状态: %s", text);
    XLabel_setText_2(g_status, buf);
}

/** @brief 更新滤波结果标签。 */
static void hsfShowResult(const char* valueText)
{
    char buf[96];
    snprintf(buf, sizeof(buf), "滤波值: %s", valueText);
    XLabel_setText_2(g_outLabel, buf);
}

/** @brief 行处理：解析 "r <值>" 更新结果标签，其余忽略。 */
static void hsfProcessLine(char* line)
{
    if (line[0] == 'r') hsfShowResult(line + 1);
}

/** @brief 行切分：与服务器同款增量缓冲。 */
static void hsfProcessBuffer(void)
{
    int start = 0;
    int i;
    for (i = 0; i < g_rxLen; ++i) {
        if (g_rxBuf[i] != '\n') continue;
        g_rxBuf[i] = '\0';
        if (i > start) hsfProcessLine(g_rxBuf + start);
        start = i + 1;
    }
    if (start > 0) {
        memmove(g_rxBuf, g_rxBuf + start, (size_t)(g_rxLen - start));
        g_rxLen -= start;
    }
}

/* ==================== 信号槽（connect_2：第 1 参=真 sender） ==================== */

static void hsfOnReadyRead(XObject* sender, XVarList* args)
{
    int64_t n;
    (void)sender;
    (void)args;
    for (;;) {
        int room = XHSF_CLIENT_RXBUF_SIZE - g_rxLen;
        if (room <= 0) { g_rxLen = 0; room = XHSF_CLIENT_RXBUF_SIZE; }
        n = XTcpSocket_read_1(g_sock, g_rxBuf + g_rxLen, room);
        if (n <= 0) break;
        g_rxLen += (int)n;
        hsfProcessBuffer();
    }
}

static void hsfOnConnected(XObject* sender, XVarList* args)
{
    (void)sender; (void)args;
    hsfSetStatus("已连接");
}

static void hsfOnDisconnected(XObject* sender, XVarList* args)
{
    (void)sender; (void)args;
    hsfSetStatus("已断开");
}

static void hsfOnError(XObject* sender, XVarList* args)
{
    (void)sender; (void)args;
    hsfSetStatus("连接错误");
}

/* 发送按钮：读输入框 → "d <值>" 上报。 */
static void hsfOnSendClicked(XObject* sender, XVarList* args)
{
    const char* txt = XLineEdit_text(g_input);
    char line[64];
    int len;
    (void)sender; (void)args;
    if (!txt || !txt[0] || !g_sock) return;
    len = snprintf(line, sizeof(line), "d %s\n", txt);
    if (XTcpSocket_write_1(g_sock, line, len) > 0) {
        XTcpSocket_flush(g_sock);
        XLineEdit_setText(g_input, "");
    }
    else {
        hsfSetStatus("发送失败");
    }
}

/* ==================== 窗口组装与启动 ==================== */

int XhsfClient_start(const char* host, unsigned port)
{
    XPushButton* sendBtn;

    g_win = XWidget_create(NULL, 0);
    if (!g_win) {
        XPrintf("HsfClient: 窗口创建失败\n");
        return 1;
    }
    XWidget_resize(g_win, 420, 180);
    XWidget_setWindowTitle(g_win, "高速过滤器 - 客户端");

    g_input = XLineEdit_create(g_win, 0);
    XWidget_move((XWidget*)g_input, 20, 20);
    XWidget_resize((XWidget*)g_input, 200, 28);
    XLineEdit_setText(g_input, "10");
    XWidget_show((XWidget*)g_input);

    sendBtn = XPushButton_create(g_win, 0);
    XWidget_move((XWidget*)sendBtn, 240, 20);
    XWidget_resize((XWidget*)sendBtn, 100, 28);
    XPushButton_setText_2(sendBtn, "发送采样");
    XWidget_show((XWidget*)sendBtn);
    XObject_connect_2((XObject*)sendBtn,
                      XSignal(XAbstractButton_clicked_signal), hsfOnSendClicked);

    g_outLabel = XLabel_create(g_win, 0);
    XWidget_move((XWidget*)g_outLabel, 20, 70);
    XWidget_resize((XWidget*)g_outLabel, 360, 28);
    XLabel_setText_2(g_outLabel, "滤波值: --");
    XWidget_show((XWidget*)g_outLabel);

    g_status = XLabel_create(g_win, 0);
    XWidget_move((XWidget*)g_status, 20, 110);
    XWidget_resize((XWidget*)g_status, 360, 28);
    XLabel_setText_2(g_status, "状态: 连接中...");
    XWidget_show((XWidget*)g_status);

    XWidget_show(g_win); /* 窗口整体最后显示。 */

    /* 连接服务端。 */
    g_sock = XTcpSocket_create();
    if (!g_sock) {
        hsfSetStatus("套接字创建失败");
        return XCoreApplication_exec();
    }
    XObject_connect_2((XObject*)g_sock, XSignal(XTcpSocket_readyRead_signal),
                      hsfOnReadyRead);
    XObject_connect_2((XObject*)g_sock, XSignal(XTcpSocket_connected_signal),
                      hsfOnConnected);
    XObject_connect_2((XObject*)g_sock, XSignal(XTcpSocket_disconnected_signal),
                      hsfOnDisconnected);
    XObject_connect_2((XObject*)g_sock, XSignal(XTcpSocket_errorOccurred_signal),
                      hsfOnError);
    XAbstractSocket_connectToHost_base((XAbstractSocket*)g_sock, host,
                                       (uint16_t)port, XIODevice_ReadWrite,
                                       XAbstractSocket_AnyIPProtocol);

    return XCoreApplication_exec(); /* GUI 事件循环（阻塞至窗口关闭）。 */
}
