#ifdef __linux__ (defined(__unix__) || defined(__APPLE__) || defined(__FreeBSD__) || defined(__OpenBSD__) || defined(__NetBSD__) || defined(__DragonFly__) || defined(__sun))
// XSerialPortPosix.c
#include "XSerialPort_p.h"
#include "XMemory.h"

#include <termios.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <errno.h>
#include <string.h>
#include <poll.h>

// ========== 平台兼容性宏 ==========
#ifndef IEXTEN
#define IEXTEN 0
#endif

#ifndef CMSPAR
#define CMSPAR 0
#endif

#ifndef CRTSCTS
#ifdef CCTS_OFLOW
#define CRTSCTS (CCTS_OFLOW | CRTS_IFLOW)
#else
#define CRTSCTS 0
#endif
#endif

// 某些系统（如旧 macOS、Solaris）没有 TIOCMBIS/TIOCMBIC
#if !defined(TIOCMBIS) || !defined(TIOCMBIC)
#define USE_TIOCMGET_SET_FOR_DTR_RTS
#endif

// Solaris 使用 CBAUD 而非 cfsetispeed
#ifdef __sun
#define USE_CBAUD_HACK
#endif

// 平台私有数据
struct PlatformData {
    int fd;
    struct termios originalTermios;
    bool isCustomBaud;
    int32_t customBaudRate;
};

// ========== 波特率映射（覆盖 Qt 支持的所有标准速率） ==========
static speed_t toTermiosBaud(int32_t rate, bool* isCustom) {
    *isCustom = false;
    switch (rate) {
    case 0:       return B0;
    case 50:      return B50;
    case 75:      return B75;
    case 110:     return B110;
    case 134:     return B134;
    case 150:     return B150;
    case 200:     return B200;
    case 300:     return B300;
    case 600:     return B600;
    case 1200:    return B1200;
    case 1800:    return B1800;
    case 2400:    return B2400;
    case 4800:    return B4800;
    case 9600:    return B9600;
    case 19200:   return B19200;
    case 38400:   return B38400;
    case 57600:   return B57600;
    case 115200:  return B115200;
#ifdef B230400
    case 230400:  return B230400;
#endif
#ifdef B460800
    case 460800:  return B460800;
#endif
#ifdef B500000
    case 500000:  return B500000;
#endif
#ifdef B576000
    case 576000:  return B576000;
#endif
#ifdef B921600
    case 921600:  return B921600;
#endif
#ifdef B1000000
    case 1000000: return B1000000;
#endif
#ifdef B1152000
    case 1152000: return B1152000;
#endif
#ifdef B1500000
    case 1500000: return B1500000;
#endif
#ifdef B2000000
    case 2000000: return B2000000;
#endif
#ifdef B2500000
    case 2500000: return B2500000;
#endif
#ifdef B3000000
    case 3000000: return B3000000;
#endif
#ifdef B3500000
    case 3500000: return B3500000;
#endif
#ifdef B4000000
    case 4000000: return B4000000;
#endif
    default:
        *isCustom = true;
        return B38400; // 占位符
    }
}

// ========== 自定义波特率（仅 Linux 尝试） ==========
static bool setCustomBaudRate(PlatformData* pd, int32_t baud) {
#ifdef __linux__
    struct serial_struct serinfo;
    if (ioctl(pd->fd, TIOCGSERIAL, &serinfo) == 0) {
        serinfo.flags &= ~ASYNC_SPD_MASK;
        serinfo.flags |= ASYNC_SPD_CUST;
        serinfo.custom_divisor = (serinfo.baud_base + (baud / 2)) / baud;
        if (serinfo.custom_divisor < 1) serinfo.custom_divisor = 1;
        if (ioctl(pd->fd, TIOCSSERIAL, &serinfo) == 0) {
            return true;
        }
    }
#endif
    return false;
}

// ========== 应用配置（核心） ==========
static bool applyTermios(XSerialPortPrivate* d) {
    PlatformData* pd = d->platform;
    struct termios tio = pd->originalTermios;

    // 进入 raw 模式（Qt 行为）
    cfmakeraw(&tio);

    // 重新设置关键字段（cfmakeraw 可能覆盖部分）
    tio.c_cflag |= (CLOCAL | CREAD);
    tio.c_cflag &= ~(CSIZE | PARENB | PARODD | CMSPAR | CSTOPB | CRTSCTS);

    // 数据位
    switch (d->dataBits) {
    case XSerialPort_Data5: tio.c_cflag |= CS5; break;
    case XSerialPort_Data6: tio.c_cflag |= CS6; break;
    case XSerialPort_Data7: tio.c_cflag |= CS7; break;
    case XSerialPort_Data8:
    default:                tio.c_cflag |= CS8; break;
    }

    // 校验位
    switch (d->parity) {
    case XSerialPort_NoParity:
        break;
    case XSerialPort_EvenParity:
        tio.c_cflag |= PARENB;
        break;
    case XSerialPort_OddParity:
        tio.c_cflag |= (PARENB | PARODD);
        break;
    case XSerialPort_SpaceParity:
        if (CMSPAR) tio.c_cflag |= (PARENB | CMSPAR);
        else { /* 降级为 NoParity */ }
        break;
    case XSerialPort_MarkParity:
        if (CMSPAR) tio.c_cflag |= (PARENB | PARODD | CMSPAR);
        else { /* 降级为 OddParity */ }
        break;
    }

    // 停止位
    if (d->stopBits == XSerialPort_TwoStop)
        tio.c_cflag |= CSTOPB;

    // 流控
    if (d->flowControl == XSerialPort_HardwareControl && CRTSCTS) {
        tio.c_cflag |= CRTSCTS;
    }
    else if (d->flowControl == XSerialPort_SoftwareControl) {
        tio.c_iflag |= (IXON | IXOFF | IXANY);
    }

    // 波特率
    bool isCustom = false;
    speed_t stdBaud = toTermiosBaud(d->baudRate, &isCustom);

#ifdef USE_CBAUD_HACK
    // Solaris 风格
    tio.c_cflag &= ~CBAUD;
    tio.c_cflag |= (stdBaud & CBAUD);
    tio.c_cflag |= (CBAUDEXT & (stdBaud << 16));
#else
    cfsetispeed(&tio, stdBaud);
    cfsetospeed(&tio, stdBaud);
#endif

    if (tcsetattr(pd->fd, TCSANOW, &tio) != 0)
        return false;

    if (isCustom) {
        pd->customBaudRate = d->baudRate;
        pd->isCustomBaud = true;
        if (!setCustomBaudRate(pd, d->baudRate)) {
            // 自定义失败，但不报错（Qt 也如此）
            return true;
        }
    }
    else {
        pd->isCustomBaud = false;
    }

    return true;
}

// ========== DTR/RTS 跨平台控制 ==========
static bool setModemControlLine(PlatformData* pd, int line, bool set) {
#ifdef USE_TIOCMGET_SET_FOR_DTR_RTS
    int status = 0;
    if (ioctl(pd->fd, TIOCMGET, &status) != 0)
        return false;
    if (set)
        status |= line;
    else
        status &= ~line;
    return ioctl(pd->fd, TIOCMSET, &status) == 0;
#else
    int cmd = set ? TIOCMBIS : TIOCMBIC;
    return ioctl(pd->fd, cmd, &line) == 0;
#endif
}

// ========== 错误码映射 ==========
static XSerialPort_Error errnoToSerialError(int err) {
    switch (err) {
    case ENOENT:
    case ENODEV:
        return XSerialPort_DeviceNotFoundError;
    case EACCES:
    case EPERM:
        return XSerialPort_PermissionError;
    case EBUSY:
        return XSerialPort_ResourceBusyError;
    case EINVAL:
        return XSerialPort_UnknownError;
    default:
        return XSerialPort_ReadError; // 通用回退
    }
}

// ========== 平台函数实现 ==========

bool XSerialPort_platform_open(XSerialPortPrivate* d, XSerialPort* owner, const char* portName, XIODeviceBaseMode mode) {
    if (!portName || !d || !owner) return false;

    int flags = O_NOCTTY | O_NONBLOCK;
    if ((mode & XIODevice_ReadOnly) && (mode & XIODevice_WriteOnly))
        flags |= O_RDWR;
    else if (mode & XIODevice_ReadOnly)
        flags |= O_RDONLY;
    else if (mode & XIODevice_WriteOnly)
        flags |= O_WRONLY;
    else
        return false;

    int fd = open(portName, flags);
    if (fd == -1) {
        d->error = errnoToSerialError(errno);
        return false;
    }

    PlatformData* pd = (PlatformData*)XCalloc_System(1, sizeof(PlatformData));
    if (!pd) {
        close(fd);
        d->error = XSerialPort_ResourceError;
        return false;
    }

    if (tcgetattr(fd, &pd->originalTermios) != 0) {
        XFree_System(pd);
        close(fd);
        d->error = XSerialPort_OpenError;
        return false;
    }

    pd->fd = fd;
    d->platform = pd;
    d->isOpen = true;

    if (!applyTermios(d)) {
        XSerialPort_platform_close(d);
        d->error = XSerialPort_OpenError;
        return false;
    }

    return true;
}

void XSerialPort_platform_close(XSerialPortPrivate* d) {
    if (!d->isOpen || !d->platform) return;
    PlatformData* pd = d->platform;
    tcsetattr(pd->fd, TCSANOW, &pd->originalTermios);
    close(pd->fd);
    XFree_System(pd);
    d->platform = NULL;
    d->isOpen = false;
}

bool XSerialPort_platform_isOpen(const XSerialPortPrivate* d) {
    return d && d->isOpen;
}

int64_t XSerialPort_platform_read(XSerialPortPrivate* d, char* data, int64_t maxSize) {
    if (!d || !data || maxSize <= 0 || !d->platform) return -1;
    ssize_t r = read(d->platform->fd, data, (size_t)maxSize);
    if (r == -1) {
        if (errno == EAGAIN || errno == EWOULDBLOCK) return 0;
        d->error = XSerialPort_ReadError;
        return -1;
    }
    return (int64_t)r;
}

int64_t XSerialPort_platform_write(XSerialPortPrivate* d, const char* data, int64_t len) {
    if (!d || !data || len <= 0 || !d->platform) return -1;
    ssize_t r = write(d->platform->fd, data, (size_t)len);
    if (r == -1) {
        if (errno == EAGAIN || errno == EWOULDBLOCK) return 0;
        d->error = XSerialPort_WriteError;
        return -1;
    }
    return (int64_t)r;
}

int64_t XSerialPort_platform_bytesAvailable(const XSerialPortPrivate* d) {
    if (!d->platform) return 0;
    int bytes = 0;
    if (ioctl(d->platform->fd, FIONREAD, &bytes) == 0)
        return (int64_t)bytes;
    return 0;
}

int64_t XSerialPort_platform_bytesToWrite(const XSerialPortPrivate* d) {
    (void)d;
    return 0; // POSIX 无标准方法，Qt 也返回 0
}

void XSerialPort_platform_poll(XSerialPortPrivate* d) {
    (void)d; // 事件驱动由上层处理
}

uint32_t XSerialPort_platform_pinoutSignals(const XSerialPortPrivate* d) {
    if (!d || !d->isOpen) return 0;
    int status = 0;
    if (ioctl(d->platform->fd, TIOCMGET, &status) != 0)
        return 0;

    // 映射到 Qt 的信号位（与 Windows 一致）
    uint32_t signals = 0;
    if (status & TIOCM_DTR) signals |= 0x04; // DataTerminalReady
    if (status & TIOCM_RTS) signals |= 0x40; // RequestToSend
    if (status & TIOCM_CTS) signals |= 0x80; // ClearToSend
    if (status & TIOCM_DSR) signals |= 0x10; // DataSetReady
    if (status & TIOCM_RI)  signals |= 0x20; // RingIndicator
    if (status & TIOCM_CD)  signals |= 0x08; // CarrierDetect
    return signals;
}

bool XSerialPort_platform_applyConfig(XSerialPortPrivate* d) {
    return applyTermios(d);
}

bool XSerialPort_platform_setDataTerminalReady(XSerialPortPrivate* d, bool set) {
    if (!d->isOpen) return false;
    return setModemControlLine(d->platform, TIOCM_DTR, set);
}

bool XSerialPort_platform_setRequestToSend(XSerialPortPrivate* d, bool set) {
    if (!d->isOpen) return false;
    return setModemControlLine(d->platform, TIOCM_RTS, set);
}

bool XSerialPort_platform_setBreakEnabled(XSerialPortPrivate* d, bool set) {
    if (!d->isOpen) return false;
    return ioctl(d->platform->fd, set ? TIOCSBRK : TIOCCBRK, 0) == 0;
}

bool XSerialPort_platform_flush(XSerialPortPrivate* d) {
    if (!d->isOpen) return false;
    return tcdrain(d->platform->fd) == 0;
}

bool XSerialPort_platform_clear(XSerialPortPrivate* d, XSerialPort_Direction dir) {
    if (!d->isOpen) return false;
    int queue = TCIOFLUSH;
    if (dir == XSerialPort_Input) queue = TCIFLUSH;
    else if (dir == XSerialPort_Output) queue = TCOFLUSH;
    return tcflush(d->platform->fd, queue) == 0;
}

bool XSerialPort_platform_waitForReadyRead(XSerialPortPrivate* d, int msecs) {
    if (!d->isOpen) return false;
    struct pollfd pfd = { .fd = d->platform->fd, .events = POLLIN };
    int ret = poll(&pfd, 1, msecs);
    if (ret > 0 && (pfd.revents & POLLIN))
        return true;
    if (ret == 0)
        d->error = XSerialPort_TimeoutError;
    return false;
}

bool XSerialPort_platform_waitForBytesWritten(XSerialPortPrivate* d, int msecs) {
    // Qt 在 Unix 上也简单等待
    (void)d; (void)msecs;
    usleep(1000); // 1ms
    return true;
}

#endif // Posix 平台