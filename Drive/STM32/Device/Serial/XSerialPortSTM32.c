#include "XDeviceSerialPort.h"
#include "XSerialPortSTM32.h"
#include "XIODevice.h"
#include "XMemory.h"
#include "XVariant.h"
#include "XVarList.h"
#include "XString.h"
#include "XRingBuffer.h"
#include <string.h>

#if defined(USE_STDPERIPH_DRIVER) && defined(STM32F40_41xxx)

#include "stm32f4xx.h"

typedef struct XDeviceSerialPortStm32Context
{
    XDeviceSerialPortContext m_base;
    USART_TypeDef* m_usart;
    XDeviceSerialPortStm32Config m_config;
} XDeviceSerialPortStm32Context;

static XDeviceSerialPortStm32Context* stm32Context(XFd fd)
{
    return (XDeviceSerialPortStm32Context*)XDevice_handle(fd);
}

static bool stm32ApplyConfig(XDeviceSerialPortStm32Context* context)
{
    USART_InitTypeDef config;
    if (!context || !context->m_usart) return false;
    if (context->m_base.m_dataBits < XSerialPort_Data8 ||
        context->m_base.m_parity == XSerialPort_SpaceParity ||
        context->m_base.m_parity == XSerialPort_MarkParity ||
        context->m_base.m_flowControl == XSerialPort_SoftwareControl ||
        context->m_base.m_flowControl == XSerialPort_BothControl) return false;
    USART_StructInit(&config);
    config.USART_BaudRate = context->m_base.m_baudRate;
    config.USART_WordLength = context->m_base.m_dataBits == XSerialPort_Data9 ?
                              USART_WordLength_9b : USART_WordLength_8b;
    config.USART_StopBits = context->m_base.m_stopBits == XSerialPort__ZeroPointFive ?
                            USART_StopBits_0_5 :
                            context->m_base.m_stopBits == XSerialPort_OneAndHalfStop ?
                            USART_StopBits_1_5 :
                            context->m_base.m_stopBits == XSerialPort_TwoStop ?
                            USART_StopBits_2 : USART_StopBits_1;
    config.USART_Parity = context->m_base.m_parity == XSerialPort_EvenParity ?
                          USART_Parity_Even :
                          context->m_base.m_parity == XSerialPort_OddParity ?
                          USART_Parity_Odd : USART_Parity_No;
    config.USART_HardwareFlowControl = context->m_base.m_flowControl == XSerialPort_HardwareControl ?
                                       USART_HardwareFlowControl_RTS_CTS :
                                       USART_HardwareFlowControl_None;
    config.USART_Mode = ((context->m_base.m_openMode & XIODevice_ReadOnly) ? USART_Mode_Rx : 0) |
                        ((context->m_base.m_openMode & XIODevice_WriteOnly) ? USART_Mode_Tx : 0);
    USART_Init(context->m_usart, &config);
    USART_Cmd(context->m_usart, ENABLE);
    return true;
}

static bool stm32ConfigurePins(XDeviceSerialPortStm32Context* context)
{
    GPIO_InitTypeDef gpio;
    uint8_t af;
    uint32_t clock;
    bool apb2;
    if (!context) return false;
    switch (context->m_config.m_portNumber) {
    case 1: af = GPIO_AF_USART1; clock = RCC_APB2Periph_USART1; apb2 = true; break;
    case 2: af = GPIO_AF_USART2; clock = RCC_APB1Periph_USART2; apb2 = false; break;
    case 3: af = GPIO_AF_USART3; clock = RCC_APB1Periph_USART3; apb2 = false; break;
    case 6: af = GPIO_AF_USART6; clock = RCC_APB2Periph_USART6; apb2 = true; break;
    default: return false;
    }
    RCC_AHB1PeriphClockCmd(context->m_config.m_tx.m_gpioClock, ENABLE);
    RCC_AHB1PeriphClockCmd(context->m_config.m_rx.m_gpioClock, ENABLE);
    if (apb2) RCC_APB2PeriphClockCmd(clock, ENABLE);
    else RCC_APB1PeriphClockCmd(clock, ENABLE);
    GPIO_PinAFConfig((GPIO_TypeDef*)(uintptr_t)context->m_config.m_tx.m_gpio,
                     context->m_config.m_tx.m_pinSource, af);
    GPIO_PinAFConfig((GPIO_TypeDef*)(uintptr_t)context->m_config.m_rx.m_gpio,
                     context->m_config.m_rx.m_pinSource, af);
    GPIO_StructInit(&gpio);
    gpio.GPIO_Pin = context->m_config.m_tx.m_pin;
    gpio.GPIO_Mode = GPIO_Mode_AF;
    gpio.GPIO_Speed = GPIO_Speed_100MHz;
    gpio.GPIO_OType = GPIO_OType_PP;
    gpio.GPIO_PuPd = GPIO_PuPd_UP;
    GPIO_Init((GPIO_TypeDef*)(uintptr_t)context->m_config.m_tx.m_gpio, &gpio);
    gpio.GPIO_Pin = context->m_config.m_rx.m_pin;
    GPIO_Init((GPIO_TypeDef*)(uintptr_t)context->m_config.m_rx.m_gpio, &gpio);
    return true;
}

XDeviceSerialPortContext* XDeviceSerialPort_platformCreateContext(void)
{
    XDeviceSerialPortStm32Context* context =
        (XDeviceSerialPortStm32Context*)XCalloc_System(1, sizeof(*context));
    return context ? &context->m_base : NULL;
}

void XDeviceSerialPort_platformDeleteContext(XDeviceSerialPortContext* context)
{
    XFree_System(context);
}

bool XDeviceSerialPort_platformOpen(XFd fd, const XString* portName)
{
    XDeviceSerialPortStm32Context* context = stm32Context(fd);
    const XDeviceSerialPortStm32Config* config;
    (void)portName;
    if (!context || !context->m_base.m_platformData) return false;
    config = (const XDeviceSerialPortStm32Config*)context->m_base.m_platformData;
    context->m_config = *config;
    context->m_usart = (USART_TypeDef*)(uintptr_t)config->m_usart;
    return stm32ConfigurePins(context) && stm32ApplyConfig(context);
}

void XDeviceSerialPort_platformClose(XFd fd)
{
    XDeviceSerialPortStm32Context* context = stm32Context(fd);
    if (context && context->m_usart) {
        USART_Cmd(context->m_usart, DISABLE);
        context->m_usart = NULL;
    }
}

int64_t XDeviceSerialPort_platformRead(XFd fd, void* buffer, int64_t size)
{
    XDeviceSerialPortStm32Context* context = stm32Context(fd);
    uint8_t* output = (uint8_t*)buffer;
    int64_t count = 0;
    if (!context || !context->m_usart || !output || size <= 0) return -1;
    while (count < size && USART_GetFlagStatus(context->m_usart, USART_FLAG_RXNE) != RESET)
        output[count++] = (uint8_t)USART_ReceiveData(context->m_usart);
    return count;
}

int64_t XDeviceSerialPort_platformWrite(XFd fd, const void* data, int64_t size)
{
    XDeviceSerialPortStm32Context* context = stm32Context(fd);
    const uint8_t* input = (const uint8_t*)data;
    int64_t count = 0;
    if (!context || !context->m_usart || !input || size <= 0) return -1;
    while (count < size && USART_GetFlagStatus(context->m_usart, USART_FLAG_TXE) != RESET)
        USART_SendData(context->m_usart, input[count++]);
    return count;
}

bool XDeviceSerialPort_platformFlush(XFd fd)
{
    XDeviceSerialPortStm32Context* context = stm32Context(fd);
    return context && context->m_usart &&
           USART_GetFlagStatus(context->m_usart, USART_FLAG_TC) != RESET;
}

bool XDeviceSerialPort_platformSetProperty(XFd fd, uint32_t property, const XVariant* value)
{
    XDeviceSerialPortStm32Context* context = stm32Context(fd);
    (void)value;
    if (!context || !context->m_usart || !value) return false;
    switch (property) {
    case XDeviceSerialPortProperty_BaudRate:
    case XDeviceSerialPortProperty_DataBits:
    case XDeviceSerialPortProperty_Parity:
    case XDeviceSerialPortProperty_StopBits:
    case XDeviceSerialPortProperty_FlowControl:
        return stm32ApplyConfig(context);
    case XDeviceSerialPortProperty_ReadBufferSize:
        return XVariant_toInt64(value) > 0;
    default:
        return false;
    }
}

bool XDeviceSerialPort_platformGetProperty(XFd fd, uint32_t property, XVariant* value)
{
    XDeviceSerialPortStm32Context* context = stm32Context(fd);
    if (!context || !context->m_usart || !value) return false;
    switch (property) {
    case XDeviceProperty_NativeHandle:
        XVariant_setValue_ptr(value, context->m_usart); return true;
    case XDeviceSerialPortProperty_BytesAvailable:
        XVariant_setValue_int64(value, USART_GetFlagStatus(context->m_usart, USART_FLAG_RXNE) != RESET ? 1 : 0); return true;
    case XDeviceSerialPortProperty_PinoutSignals:
        XVariant_setValue_int(value, XSerialPort_NoSignal); return true;
    default:
        return false;
    }
}

bool XDeviceSerialPort_platformControl(XFd fd, uint32_t command,
                                       const XVarList* input, XVarList* output)
{
    XDeviceSerialPortStm32Context* context = stm32Context(fd);
    (void)input; (void)output;
    if (!context || !context->m_usart) return false;
    if (command == XDeviceSerialPortCommand_Clear) {
        while (USART_GetFlagStatus(context->m_usart, USART_FLAG_RXNE) != RESET)
            (void)USART_ReceiveData(context->m_usart);
        return true;
    }
    return command == XDeviceSerialPortCommand_HandleEvent || command == XDeviceCommand_Cancel;
}

#elif defined(XSERIALPORT_USE_STM32)

XDeviceSerialPortContext* XDeviceSerialPort_platformCreateContext(void) { return NULL; }
void XDeviceSerialPort_platformDeleteContext(XDeviceSerialPortContext* context) { (void)context; }
bool XDeviceSerialPort_platformOpen(XFd fd, const XString* path) { (void)fd; (void)path; return false; }
void XDeviceSerialPort_platformClose(XFd fd) { (void)fd; }
int64_t XDeviceSerialPort_platformRead(XFd fd, void* buffer, int64_t size) { (void)fd; (void)buffer; (void)size; return -1; }
int64_t XDeviceSerialPort_platformWrite(XFd fd, const void* data, int64_t size) { (void)fd; (void)data; (void)size; return -1; }
bool XDeviceSerialPort_platformFlush(XFd fd) { (void)fd; return false; }
bool XDeviceSerialPort_platformSetProperty(XFd fd, uint32_t property, const XVariant* value) { (void)fd; (void)property; (void)value; return false; }
bool XDeviceSerialPort_platformGetProperty(XFd fd, uint32_t property, XVariant* value) { (void)fd; (void)property; (void)value; return false; }
bool XDeviceSerialPort_platformControl(XFd fd, uint32_t command, const XVarList* input, XVarList* output) { (void)fd; (void)command; (void)input; (void)output; return false; }

#endif
