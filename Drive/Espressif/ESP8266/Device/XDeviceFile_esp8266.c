/**
 * @file XDeviceFile_esp8266.c
 * @brief ESP8266 的标准输入/输出后端。
 * @details
 * XConsoleShell 的控制台传输最终落在这五个函数上：XDeviceConsole 把它们
 * 包装成 XFd，对上层暴露统一的 XDevice 接口；控制台的 write 走 newlib 的
 * stdout，read 走 XDevice_read → 本文件的 legacyRead。
 *
 * 因此把这五个函数接到串口上，桌面那条控制台路径（XCoreApplication → 事件
 * 分发器 → 默认 Shell → stdin/stdout）在 ESP8266 上可以原样复用，无需改动
 * 任何共享代码。
 *
 * 传输本身不直接碰 UART 寄存器，而是持有一个 XSerialPort 对象，由
 * XSerialPortESP8266 后端落到 UART0——这样串口格式、波特率、错误处理都
 * 走库内既有的设备层，与桌面/POSIX/STM32 的组织方式一致。
 *
 * 句柄模型：ESP8266 只有一路控制台，没有多个 stdin。用静态哨兵值作为 XFd
 * 的原生 handle，legacy* 系列只认这个哨兵，其余句柄一律拒绝。
 */

#include "XFileDescriptor.h"
#include "XDeviceFile.h"
#include "XSerialPort.h"
#include "XSerialPortESP8266.h"

#if defined(__ESP8266__) || defined(ESP8266)

/* 标准输入后端的哨兵 handle；0 不与真实文件描述符混淆。 */
#define XESP8266_STDIN_HANDLE ((void*)(intptr_t)0)

/* 控制台波特率。SDK 上电默认 74880，与常见串口终端的 115200 不一致，
 * 必须在任何输出之前改掉。 */
#define XESP8266_STDIN_BAUD 115200

/* 进程级唯一的控制台串口。ESP8266 无进程模型，静态单例即可，
 * 也避免了 Shell 反复 attach/detach 时反复开关 UART。 */
static XSerialPort g_stdinPort;
static bool g_stdinPortReady = false;
static XFd g_stdinFd = XFD_INVALID;

/** @brief 懒初始化控制台串口：配置波特率后打开 UART0。 */
static bool esp8266EnsureStdinPort(void)
{
    if (!g_stdinPortReady) {
        XSerialPort_init(&g_stdinPort);
        XSerialPort_setPortName(&g_stdinPort, "UART0");
        /* 波特率在设备层仍是默认值，这里直接下发硬件配置：
         * 事件分发器创建 Shell 时可能早于任何 XSerialPort 属性调用。 */
        XSerialPortESP8266_configureUart(XESP8266_STDIN_BAUD);
        g_stdinPortReady = true;
    }
    if (!XIODevice_isOpen((XIODevice*)&g_stdinPort))
        XIODevice_open_base((XIODevice*)&g_stdinPort, XIODevice_ReadWrite);
    return XIODevice_isOpen((XIODevice*)&g_stdinPort);
}

XFd XDeviceFile_openStandardInput(int* error)
{
    XFd fd = XFd_alloc(XFD_TYPE_FILE, XESP8266_STDIN_HANDLE, NULL);
    if (fd == XFD_INVALID) {
        if (error) *error = 1;
        return XFD_INVALID;
    }
    g_stdinFd = fd;
    esp8266EnsureStdinPort();
    if (error) *error = 0;
    return fd;
}

void XDeviceFile_legacyClose(XFd fd)
{
    /* UART0 由 SDK 与 user_init 全程持有：关闭标准输入不应拆掉串口，
     * 否则 SDK 自身的日志会失去输出。这里只释放句柄登记。 */
    if (fd != g_stdinFd) return;
    g_stdinFd = XFD_INVALID;
}

int64_t XDeviceFile_legacyRead(XFd fd, void* buffer, int64_t size)
{
    if (fd != g_stdinFd || !buffer || size <= 0) return -1;
    /* 非阻塞：RX FIFO 空时立刻返回 0，交由 Shell 的轮询定时器重试。 */
    return XIODevice_read_1((XIODevice*)&g_stdinPort, (char*)buffer, size);
}

int64_t XDeviceFile_legacyWrite(XFd fd, const void* data, int64_t size)
{
    if (fd != g_stdinFd || !data || size < 0) return -1;
    return XIODevice_write_1((XIODevice*)&g_stdinPort, (const char*)data, size);
}

bool XDeviceFile_legacyFlush(XFd fd)
{
    if (fd != g_stdinFd) return false;
    return XIODevice_flush((XIODevice*)&g_stdinPort);
}

bool XDeviceFile_legacySetStandardInputEcho(XFd fd, bool enabled)
{
    (void)enabled;
    /* 硬件层无回显控制；串口终端（PuTTY / minicom / Windows 终端）
     * 默认本地回显，这里恒真。 */
    return fd == g_stdinFd;
}

#endif /* defined(__ESP8266__) || defined(ESP8266) */
