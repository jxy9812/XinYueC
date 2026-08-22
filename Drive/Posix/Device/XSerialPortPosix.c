#include "XDeviceSerialPort.h"

#if defined(XSERIALPORT_USE_PLATFORM_API) && (defined(__linux__) || defined(__unix__) || defined(__APPLE__) || \
    defined(__FreeBSD__) || defined(__OpenBSD__) || defined(__NetBSD__) || \
    defined(__DragonFly__) || defined(__sun))

#include "XMemory.h"
#include "XVariant.h"
#include "XVarList.h"
#include "XString.h"
#include "XSerialPort.h"
#include "XRingBuffer.h"
#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <string.h>
#include <termios.h>
#include <unistd.h>
#include <sys/ioctl.h>

#ifndef CRTSCTS
#define CRTSCTS 0
#endif
#ifndef CMSPAR
#define CMSPAR 0
#endif

typedef struct XDeviceSerialPortPosixContext
{
    XDeviceSerialPortContext m_base;
    int m_nativeFd;
    struct termios m_originalTermios;
    bool m_haveOriginalTermios;
} XDeviceSerialPortPosixContext;

static XDeviceSerialPortPosixContext* posixContext(XFd fd)
{
    return (XDeviceSerialPortPosixContext*)XDevice_handle(fd);
}

static speed_t posixBaud(int32_t baudRate)
{
    switch (baudRate) {
    case 0: return B0;
    case 50: return B50;
    case 75: return B75;
    case 110: return B110;
    case 134: return B134;
    case 150: return B150;
    case 200: return B200;
    case 300: return B300;
    case 600: return B600;
    case 1200: return B1200;
    case 1800: return B1800;
    case 2400: return B2400;
    case 4800: return B4800;
    case 9600: return B9600;
    case 19200: return B19200;
    case 38400: return B38400;
    case 57600: return B57600;
    case 115200: return B115200;
#ifdef B230400
    case 230400: return B230400;
#endif
#ifdef B460800
    case 460800: return B460800;
#endif
#ifdef B921600
    case 921600: return B921600;
#endif
    default: return 0;
    }
}

static bool posixApplyConfig(XDeviceSerialPortPosixContext* context)
{
    struct termios termiosValue;
    speed_t baud;
    if (!context || context->m_nativeFd < 0 || !context->m_haveOriginalTermios) return false;
    if (context->m_base.m_dataBits == XSerialPort_Data9 ||
        context->m_base.m_stopBits == XSerialPort_OneAndHalfStop) return false;
    baud = posixBaud(context->m_base.m_baudRate);
    if (baud == 0) return false;

    termiosValue = context->m_originalTermios;
    cfmakeraw(&termiosValue);
    termiosValue.c_cflag |= CLOCAL | CREAD;
    termiosValue.c_cflag &= ~(CSIZE | PARENB | PARODD | CSTOPB | CRTSCTS);
    termiosValue.c_iflag &= ~(IXON | IXOFF | IXANY);

    switch (context->m_base.m_dataBits) {
    case XSerialPort_Data5: termiosValue.c_cflag |= CS5; break;
    case XSerialPort_Data6: termiosValue.c_cflag |= CS6; break;
    case XSerialPort_Data7: termiosValue.c_cflag |= CS7; break;
    default: termiosValue.c_cflag |= CS8; break;
    }
    switch (context->m_base.m_parity) {
    case XSerialPort_EvenParity: termiosValue.c_cflag |= PARENB; break;
    case XSerialPort_OddParity: termiosValue.c_cflag |= PARENB | PARODD; break;
    case XSerialPort_SpaceParity:
        if (!CMSPAR) return false;
        termiosValue.c_cflag |= PARENB | CMSPAR;
        break;
    case XSerialPort_MarkParity:
        if (!CMSPAR) return false;
        termiosValue.c_cflag |= PARENB | PARODD | CMSPAR;
        break;
    default: break;
    }
    if (context->m_base.m_stopBits == XSerialPort_TwoStop) termiosValue.c_cflag |= CSTOPB;
    if (context->m_base.m_flowControl == XSerialPort_HardwareControl ||
        context->m_base.m_flowControl == XSerialPort_BothControl) {
        if (!CRTSCTS) return false;
        termiosValue.c_cflag |= CRTSCTS;
    }
    if (context->m_base.m_flowControl == XSerialPort_SoftwareControl ||
        context->m_base.m_flowControl == XSerialPort_BothControl)
        termiosValue.c_iflag |= IXON | IXOFF | IXANY;
    cfsetispeed(&termiosValue, baud);
    cfsetospeed(&termiosValue, baud);
    return tcsetattr(context->m_nativeFd, TCSANOW, &termiosValue) == 0;
}

static bool readBool(const XVariant* value)
{
    return XVariant_toBool(value);
}

XDeviceSerialPortContext* XDeviceSerialPort_platformCreateContext(void)
{
    XDeviceSerialPortPosixContext* context =
        (XDeviceSerialPortPosixContext*)XCalloc_System(1, sizeof(*context));
    if (context) context->m_nativeFd = -1;
    return context ? &context->m_base : NULL;
}

void XDeviceSerialPort_platformDeleteContext(XDeviceSerialPortContext* base)
{
    XFree_System(base);
}

bool XDeviceSerialPort_platformOpen(XFd fd, const XString* portName)
{
    XDeviceSerialPortPosixContext* context = posixContext(fd);
    int flags = O_NOCTTY;
    int nativeFd;
    const char* path;
    if (!context || !portName) return false;
    switch (context->m_base.m_openMode & (XIODevice_ReadOnly | XIODevice_WriteOnly)) {
    case XIODevice_ReadOnly: flags |= O_RDONLY; break;
    case XIODevice_WriteOnly: flags |= O_WRONLY; break;
    default: flags |= O_RDWR; break;
    }
    if (context->m_base.m_flags & XDeviceOpenFlag_NonBlocking) flags |= O_NONBLOCK;
    path = XString_toUtf8(portName);
    nativeFd = open(path ? path : "", flags);
    if (nativeFd < 0) return false;
    context->m_nativeFd = nativeFd;
    if (tcgetattr(nativeFd, &context->m_originalTermios) == 0) {
        context->m_haveOriginalTermios = true;
        if (!posixApplyConfig(context)) {
            close(nativeFd);
            context->m_nativeFd = -1;
            return false;
        }
    } else {
        close(nativeFd);
        context->m_nativeFd = -1;
        return false;
    }
    return true;
}

void XDeviceSerialPort_platformClose(XFd fd)
{
    XDeviceSerialPortPosixContext* context = posixContext(fd);
    if (!context) return;
    if (context->m_nativeFd >= 0) {
        if (context->m_haveOriginalTermios)
            tcsetattr(context->m_nativeFd, TCSANOW, &context->m_originalTermios);
        close(context->m_nativeFd);
        context->m_nativeFd = -1;
    }
}

int64_t XDeviceSerialPort_platformRead(XFd fd, void* buffer, int64_t size)
{
    XDeviceSerialPortPosixContext* context = posixContext(fd);
    ssize_t result;
    if (!context || context->m_nativeFd < 0 || !buffer || size <= 0) return -1;
    if (context->m_base.m_readBuffer && XRingBuffer_available(context->m_base.m_readBuffer) > 0)
        return (int64_t)XRingBuffer_read(context->m_base.m_readBuffer, buffer, (size_t)size);
    result = read(context->m_nativeFd, buffer, (size_t)size);
    if (result >= 0) return (int64_t)result;
    if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR) return 0;
    return -1;
}

int64_t XDeviceSerialPort_platformWrite(XFd fd, const void* data, int64_t size)
{
    XDeviceSerialPortPosixContext* context = posixContext(fd);
    ssize_t result;
    if (!context || context->m_nativeFd < 0 || !data || size <= 0) return -1;
    result = write(context->m_nativeFd, data, (size_t)size);
    if (result >= 0) return (int64_t)result;
    if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR) return 0;
    return -1;
}

bool XDeviceSerialPort_platformFlush(XFd fd)
{
    XDeviceSerialPortPosixContext* context = posixContext(fd);
    return context && context->m_nativeFd >= 0 && tcdrain(context->m_nativeFd) == 0;
}

bool XDeviceSerialPort_platformSetProperty(XFd fd, uint32_t property, const XVariant* value)
{
    XDeviceSerialPortPosixContext* context = posixContext(fd);
    int modem = 0;
    bool enabled;
    if (!context || !value || context->m_nativeFd < 0) return false;
    switch (property) {
    case XDeviceSerialPortProperty_BaudRate:
    case XDeviceSerialPortProperty_DataBits:
    case XDeviceSerialPortProperty_Parity:
    case XDeviceSerialPortProperty_StopBits:
    case XDeviceSerialPortProperty_FlowControl:
        return posixApplyConfig(context);
    case XDeviceSerialPortProperty_ReadBufferSize:
        return XVariant_toInt64(value) > 0;
    case XDeviceSerialPortProperty_DataTerminalReady:
    case XDeviceSerialPortProperty_RequestToSend:
        enabled = readBool(value);
        if (ioctl(context->m_nativeFd, TIOCMGET, &modem) != 0) return false;
        if (property == XDeviceSerialPortProperty_DataTerminalReady) {
            if (enabled) modem |= TIOCM_DTR; else modem &= ~TIOCM_DTR;
        } else {
            if (enabled) modem |= TIOCM_RTS; else modem &= ~TIOCM_RTS;
        }
        return ioctl(context->m_nativeFd, TIOCMSET, &modem) == 0;
    case XDeviceSerialPortProperty_BreakEnabled:
        enabled = readBool(value);
        return ioctl(context->m_nativeFd, enabled ? TIOCSBRK : TIOCCBRK) == 0;
    default:
        return false;
    }
}

bool XDeviceSerialPort_platformGetProperty(XFd fd, uint32_t property, XVariant* value)
{
    XDeviceSerialPortPosixContext* context = posixContext(fd);
    int modem = 0;
    int available = 0;
    if (!context || !value || context->m_nativeFd < 0) return false;
    switch (property) {
    case XDeviceProperty_NativeHandle:
        XVariant_setValue_ptr(value, (void*)(intptr_t)context->m_nativeFd); return true;
    case XDeviceSerialPortProperty_BytesAvailable:
        if (ioctl(context->m_nativeFd, FIONREAD, &available) != 0) available = 0;
        XVariant_setValue_int64(value, available); return true;
    case XDeviceSerialPortProperty_PinoutSignals:
        if (ioctl(context->m_nativeFd, TIOCMGET, &modem) != 0) modem = 0;
        available = XSerialPort_NoSignal;
        if (modem & TIOCM_DTR) available |= XSerialPort_DataTerminalReadySignal;
        if (modem & TIOCM_CAR) available |= XSerialPort_DataCarrierDetectSignal;
        if (modem & TIOCM_DSR) available |= XSerialPort_DataSetReadySignal;
        if (modem & TIOCM_RI) available |= XSerialPort_RingIndicatorSignal;
        if (modem & TIOCM_RTS) available |= XSerialPort_RequestToSendSignal;
        if (modem & TIOCM_CTS) available |= XSerialPort_ClearToSendSignal;
        XVariant_setValue_int(value, available); return true;
    default:
        return false;
    }
}

bool XDeviceSerialPort_platformControl(XFd fd, uint32_t command,
                                       const XVarList* input, XVarList* output)
{
    XDeviceSerialPortPosixContext* context = posixContext(fd);
    XSerialPort_Direction directions;
    XVarList* arguments = (XVarList*)input;
    (void)output;
    if (!context || context->m_nativeFd < 0) return false;
    if (command == XDeviceSerialPortCommand_Clear) {
        if (!arguments || arguments->m_size != sizeof(directions)) return false;
        XVarList_start(arguments);
        directions = XVarList_arg(arguments, XSerialPort_Direction);
        return tcflush(context->m_nativeFd,
                       (directions & XSerialPort_AllDirections) == XSerialPort_AllDirections ? TCIOFLUSH :
                       (directions & XSerialPort_Input) ? TCIFLUSH : TCOFLUSH) == 0;
    }
    return command == XDeviceSerialPortCommand_HandleEvent ||
           command == XDeviceCommand_Cancel;
}

#endif
