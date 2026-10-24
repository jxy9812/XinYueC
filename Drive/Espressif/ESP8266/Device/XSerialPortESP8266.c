/**
 * @file XSerialPortESP8266.c
 * @brief ESP8266 RTOS SDK 的串口设备后端实现。
 * @details
 * 提供 XDeviceSerialPort 的十个平台钩子，形态对齐 STM32 后端。收发直接操作
 * UART0 寄存器——SDK 的 uart.h 只到寄存器级，没有 uart_write_bytes 这类带
 * 缓冲的高层接口。
 *
 * 读路径是非阻塞的：只取走此刻已在 RX FIFO 里的字节就返回，FIFO 为空时
 * 返回 0。上层（XConsoleShell 的轮询定时器）按固定节奏重试，不依赖阻塞。
 */

#include "XSerialPortESP8266.h"

#if defined(__ESP8266__) || defined(ESP8266)

#include "XDeviceSerialPort.h"
#include "XDevice.h"
#include "XMemory.h"
#include "XString.h"
#include "XVariant.h"
#include "XVarList.h"
#include <string.h>

/* SDK 的 uart.h 既不含任何头文件，也不引入 <stdint.h>，却在自身里直接用
 * uint8/uint32；因此这两步顺序不能调换：先补齐定长整型，再引入寄存器定义
 * （UART_RXD_INV 等枚举、REG_UART_BASE / UART_FIFO / UART_STATUS 偏移宏），
 * 最后才是 uart.h。 */
#include <stdint.h>
#include "c_types.h"          /* BIT() 宏的来源（uart_register.h 依赖它） */
/* uart_register.h 的位置随 SDK 布局而异：
 *   - ESP8266 RTOS SDK / 部分 Arduino 包：esp8266/uart_register.h（子目录）
 *   - Arduino NONOS 平面 SDK（本工程）：include/uart_register.h（扁平）
 * 用 __has_include 自动选择，避免依赖构建脚本里额外放一个空壳 shim。 */
#if defined(__has_include)
#  if __has_include("esp8266/uart_register.h")
#    include "esp8266/uart_register.h"
#  else
#    include "uart_register.h"
#  endif
#else
#  include "esp8266/uart_register.h"
#endif
#include "uart.h"

/* offset 必须是相对 UART0 基址的纯偏移。sdk 的 uart_register.h 里
 * UART_FIFO(i)/UART_STATUS(i) 等已经含了 REG_UART_BASE(i)，再包一层
 * XESP8266_UART_REG 会把基址加两次（写到 0xC00000xx），启动即崩。 */
#define XESP8266_UART_REG(offset) \
    (*(volatile uint32_t *)(REG_UART_BASE(UART0) + (offset)))

/** @brief 平台私有上下文；m_base 必须是首成员，供基类回读。 */
typedef struct XDeviceSerialPortEsp8266Context
{
    XDeviceSerialPortContext m_base; /**< 设备层上下文，必须是第一个成员。 */
    int32_t m_appliedBaud;           /**< 最近一次实际写入寄存器的波特率。 */
} XDeviceSerialPortEsp8266Context;

static XDeviceSerialPortEsp8266Context* esp8266Context(XFd fd)
{
    return (XDeviceSerialPortEsp8266Context*)XDevice_handle(fd);
}

/* ==================================================================== */
/* UART0 底层函数：本地自实现                                            */
/* ==================================================================== */
/*
 * driver_lib/include/uart.h 声明了下面六个函数，但 SDK 的预编译库里没有任何
 * 一个导出它们（libhal.a / libmain.a / libgcc.a 中 UART_* 计数均为 0），
 * 因此链接期报 undefined。这里按 ESP8266 寄存器手册自行实现，签名与 uart.h
 * 中的声明保持一致。
 *
 * ⚠ 2026-10-22 按手册修正位布局（真机实测：串口收到的字节全部错乱，
 *   寄存器现场 CONF0=0x79 = 7 位数据 + 停止位错码）。此前版本把字长写进
 *   UART_CONF1[1:0]、停止位写 CONF0[14:13]、清校验清 BIT(12)，并给 CONF0
 *   OR 上了臆造的 TX_EN/RX_EN 位——全部错位。ESP8266 手册的 CONF0(0x20)：
 *     bit0     UART_PARITY      （0=偶 1=奇；PARITY_EN=1 时才生效）
 *     bit1     UART_PARITY_EN
 *     bit3:2   UART_BIT_NUM     （0=5b 1=6b 2=7b 3=8b）
 *     bit5:4   UART_STOP_BIT_NUM（0=1 位 1=1.5 位 2=2 位）
 *     bit17    UART_RXFIFO_RST  bit18 UART_TXFIFO_RST（SDK 头文件给出）
 *   uart_register.h 只给了 TXFIFO_RST / RXFIFO_RST 两枚位定义，其余按手册。
 *   波特率分频在 UART_CLKDIV(0x14)。CONF1(0x24) 是 RX FIFO 阈值，与帧格式
 *   无关——之前把字长写到那里完全无效，帧格式停留在 SDK 复位残留值上。
 */
/* 以下均为相对 REG_UART_BASE(UART0) 的纯偏移（手册 uart_register.h）：
 *   CLKDIV 0x14  STATUS 0x1C  CONF0 0x20  CONF1 0x24
 * 注意不是 0x09——曾把波特率写到 0x09（落在中断状态寄存器附近）。 */
#define XUART_CLKDIV 0x14
#define XUART_STATUS 0x1C
#define XUART_CONF0  0x20
#define XUART_CONF1  0x24

#define XUART_PARITY_SEL  BIT(0)
#define XUART_PARITY_EN   BIT(1)
#define XUART_BITNUM_SHIFT 2
#define XUART_STOP_SHIFT   4
/* TX FIFO 计数在 STATUS[23:16]。 */
#define XUART_TXFIFO_CNT_MASK 0x00FF0000u

void UART_SetBaudrate(UART_Port uart_no, uint32 baud_rate)
{
    (void)uart_no;
    if (baud_rate == 0)
        baud_rate = 115200;
    XESP8266_UART_REG(XUART_CLKDIV) = F_CPU / baud_rate;
}

void UART_SetWordLength(UART_Port uart_no, UART_WordLength len)
{
    uint32_t conf0 = XESP8266_UART_REG(XUART_CONF0);
    (void)uart_no;
    conf0 = (conf0 & ~(0x3u << XUART_BITNUM_SHIFT)) |
            (((uint32_t)len & 0x3u) << XUART_BITNUM_SHIFT);
    XESP8266_UART_REG(XUART_CONF0) = conf0;
}

void UART_SetStopBits(UART_Port uart_no, UART_StopBits bit_num)
{
    uint32_t conf0 = XESP8266_UART_REG(XUART_CONF0);
    (void)uart_no;
    /* 停止位编码（CONF0[5:4]）：1 -> 0，1.5 -> 1，2 -> 2。 */
    conf0 &= ~(0x3u << XUART_STOP_SHIFT);
    conf0 |= ((uint32_t)bit_num & 0x3u) << XUART_STOP_SHIFT;
    XESP8266_UART_REG(XUART_CONF0) = conf0;
}

void UART_SetParity(UART_Port uart_no, UART_ParityMode parity_mode)
{
    uint32_t conf0 = XESP8266_UART_REG(XUART_CONF0);
    (void)uart_no;
    /* CONF0[1] = PARITY_EN，CONF0[0] = 奇偶选择（0=偶 1=奇）。 */
    if (parity_mode == USART_Parity_Even || parity_mode == USART_Parity_Odd) {
        conf0 |= XUART_PARITY_EN;
        if (parity_mode == USART_Parity_Odd)
            conf0 |= XUART_PARITY_SEL;
        else
            conf0 &= ~XUART_PARITY_SEL;
    }
    else {
        conf0 &= ~(XUART_PARITY_EN | XUART_PARITY_SEL);
    }
    XESP8266_UART_REG(XUART_CONF0) = conf0;
}

void UART_ResetFifo(UART_Port uart_no)
{
    /* 复位位是脉冲：置 1 后必须清 0，否则 FIFO 一直处于复位、永远发不出去。
     * ⚠ 只动两枚复位位，不碰 CONF0 的帧格式位——旧版在这里 OR 臆造的
     * TX_EN/RX_EN/OUT_EN（落在 PARITY 选择/使能位上），把帧格式写坏。 */
    uint32_t conf0 = XESP8266_UART_REG(XUART_CONF0);
    (void)uart_no;
    conf0 |= UART_TXFIFO_RST | UART_RXFIFO_RST;
    XESP8266_UART_REG(XUART_CONF0) = conf0;
    conf0 &= ~(UART_TXFIFO_RST | UART_RXFIFO_RST);
    XESP8266_UART_REG(XUART_CONF0) = conf0;
}

void UART_WaitTxFifoEmpty(UART_Port uart_no)
{
    (void)uart_no;
    /* 轮询 TX FIFO 计数归零；不设超时，与 SDK 原实现语义一致。 */
    while ((XESP8266_UART_REG(XUART_STATUS) & XUART_TXFIFO_CNT_MASK) != 0) {
    }
}

void XSerialPortESP8266_configureUart(int32_t baudRate)
{
    if (baudRate <= 0)
        baudRate = 115200;

    UART_SetBaudrate(UART0, (uint32_t)baudRate);
    UART_SetWordLength(UART0, UART_WordLength_8b);
    UART_SetStopBits(UART0, USART_StopBits_1);
    UART_SetParity(UART0, USART_Parity_None);
    UART_ResetFifo(UART0);
}

XDeviceSerialPortContext* XDeviceSerialPort_platformCreateContext(void)
{
    XDeviceSerialPortEsp8266Context* context =
        (XDeviceSerialPortEsp8266Context*)XCalloc_System(1, sizeof(*context));
    return context ? &context->m_base : NULL;
}

void XDeviceSerialPort_platformDeleteContext(XDeviceSerialPortContext* context)
{
    XFree_System(context);
}

bool XDeviceSerialPort_platformOpen(XFd fd, const XString* portName)
{
    XDeviceSerialPortEsp8266Context* context = esp8266Context(fd);

    /* 板上只有一路可用串口（CH340 硬连 UART0），端口名无选择余地。 */
    (void)portName;
    if (!context)
        return false;

    XSerialPortESP8266_configureUart(context->m_base.m_baudRate);
    context->m_appliedBaud = context->m_base.m_baudRate;
    return true;
}

void XDeviceSerialPort_platformClose(XFd fd)
{
    XDeviceSerialPortEsp8266Context* context = esp8266Context(fd);
    /* UART0 由 user_init 全程持有：SDK 的下载口与运行期日志共用它，
     * 关闭串口设备不应把 UART 拆掉。这里只丢弃 FIFO 里的残留字节。 */
    if (context)
        UART_ResetFifo(UART0);
}

int64_t XDeviceSerialPort_platformRead(XFd fd, void* buffer, int64_t size)
{
    XDeviceSerialPortEsp8266Context* context = esp8266Context(fd);
    uint8_t* output = (uint8_t*)buffer;
    uint32_t available;
    int64_t count = 0;

    if (!context || !output || size <= 0)
        return -1;

    available = XESP8266_UART_REG(XUART_STATUS) & UART_RXFIFO_CNT;
    if (available > (uint32_t)size)
        available = (uint32_t)size;
    if (available > XESP8266_SERIAL_RX_BURST_MAX)
        available = XESP8266_SERIAL_RX_BURST_MAX;

    while (count < (int64_t)available)
        output[count++] = (uint8_t)(XESP8266_UART_REG(0x00) & UART_RXFIFO_RD_BYTE);

    return count;
}

int64_t XDeviceSerialPort_platformWrite(XFd fd, const void* data, int64_t size)
{
    XDeviceSerialPortEsp8266Context* context = esp8266Context(fd);
    const uint8_t* input = (const uint8_t*)data;
    int64_t count = 0;

    if (!context || !input || size < 0)
        return -1;

    while (count < size) {
        /* 逐字节写 FIFO，写满时等待其排空。控制台输出量小，用 SDK 的
         * 等待原语换正确性，优先保证一次点亮可用。 */
        XESP8266_UART_REG(0x00) = input[count];
        UART_WaitTxFifoEmpty(UART0);
        ++count;
    }
    return count;
}

bool XDeviceSerialPort_platformFlush(XFd fd)
{
    XDeviceSerialPortEsp8266Context* context = esp8266Context(fd);
    if (!context)
        return false;
    UART_WaitTxFifoEmpty(UART0);
    return true;
}

/* 波特率以外的格式属性（数据位、校验、停止位、流控）在本后端固定为 8N1
 * 无流控：UART0 由 SDK 与应用共同使用，改动格式会破坏 SDK 自身的日志输出。
 * 因此格式类属性只做「校验后接受」，不重新下发硬件配置。 */
bool XDeviceSerialPort_platformSetProperty(XFd fd, uint32_t property,
                                           const XVariant* value)
{
    XDeviceSerialPortEsp8266Context* context = esp8266Context(fd);
    if (!context || !value)
        return false;

    switch (property) {
    case XDeviceSerialPortProperty_BaudRate: {
        /* 波特率是唯一真正下发给硬件的属性：变更时必须重配 UART 并
         * 清空 FIFO，否则半旧速率的残留字节会混进新会话。 */
        int64_t baud = XVariant_toInt64(value);
        if (baud <= 0)
            return false;
        XSerialPortESP8266_configureUart((int32_t)baud);
        context->m_appliedBaud = (int32_t)baud;
        context->m_base.m_baudRate = (int32_t)baud;
        return true;
    }
    case XDeviceSerialPortProperty_DataBits:
    case XDeviceSerialPortProperty_Parity:
    case XDeviceSerialPortProperty_StopBits:
    case XDeviceSerialPortProperty_FlowControl:
        return false;   /* 固定 8N1 无流控，拒绝改动 */
    case XDeviceSerialPortProperty_ReadBufferSize:
        return XVariant_toInt64(value) > 0;
    default:
        return false;
    }
}

bool XDeviceSerialPort_platformGetProperty(XFd fd, uint32_t property,
                                           XVariant* value)
{
    XDeviceSerialPortEsp8266Context* context = esp8266Context(fd);
    if (!context || !value)
        return false;

    switch (property) {
    case XDeviceProperty_NativeHandle:
        /* UART0 在 ESP8266 上是固定编号，向上层暴露编号本身即可。 */
        XVariant_setValue_ptr(value, (void*)(intptr_t)0);
        return true;
    case XDeviceSerialPortProperty_BytesAvailable:
        /* Shell 用它判断是否值得调用 read，必须反映真实 RX FIFO 计数。 */
        XVariant_setValue_int64(
            value, (int64_t)(XESP8266_UART_REG(XUART_STATUS) & UART_RXFIFO_CNT));
        return true;
    case XDeviceSerialPortProperty_PinoutSignals:
        /* UART0 无 RTS/DTR 可供查询。 */
        XVariant_setValue_int(value, XSerialPort_NoSignal);
        return true;
    default:
        return false;
    }
}

bool XDeviceSerialPort_platformControl(XFd fd, uint32_t command,
                                       const XVarList* input, XVarList* output)
{
    XDeviceSerialPortEsp8266Context* context = esp8266Context(fd);
    (void)input; (void)output;
    if (!context)
        return false;

    if (command == XDeviceSerialPortCommand_Clear) {
        /* 丢弃 FIFO 中残留的字节，避免上一次的半个命令串污染新会话。 */
        UART_ResetFifo(UART0);
        return true;
    }
    /* UART0 无 DTR/RTS 硬件流控；串口终端自己做本地回显。 */
    return command == XDeviceSerialPortCommand_HandleEvent ||
           command == XDeviceCommand_Cancel;
}

/* ESP8266 板上只有一路可用串口（CH340 硬连 UART0），端口名无选择余地，
 * 因此枚举结果是固定的一条。 */
bool XDeviceSerialPort_platformEnumeratePorts(XDeviceSerialPortPortCollector collector, void* userData)
{
    XDeviceSerialPortPortInfo info;
    if (!collector) return false;
    memset(&info, 0, sizeof(info));
    info.m_portName = "UART0";
    info.m_description = "ESP8266 UART0 (GPIO1/GPIO3)";
    info.m_busy = false;
    if (!collector(userData, &info)) return false;
    return true;
}

#endif /* defined(__ESP8266__) || defined(ESP8266) */
