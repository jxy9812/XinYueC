/**
 * @file XSerialPortPosix.c
 * @brief XSerialPort POSIX 平台实现（Linux/macOS/BSD，支持 io_uring 异步 I/O）
 * 保持与 Windows 平台一致的 API 签名
 */

#if defined(__linux__) || defined(__unix__) || defined(__APPLE__) || defined(__FreeBSD__) || defined(__OpenBSD__) || defined(__NetBSD__) || defined(__DragonFly__) || defined(__sun)

#include "XSerialPort.h"
#include "XRingBuffer.h"
#include "XMemory.h"
#include "XMutex.h"
#include "XWaitCondition.h"
#include "XSocketNotifier.h"
#include "XCoreApplication.h"
#include "XAbstractEventDispatcher.h"
#include "XIODevicePrivate.h"
#include "XIODevice_Protected.h"
#include "XFileDescriptor.h"
#include "XAbstractNetIoRing.h"
#include "XNetIoRingPosix.h"

#include <termios.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/select.h>
#include <errno.h>
#include <string.h>
#include <poll.h>
#ifdef __linux__
#include <linux/serial.h>
#include <linux/io_uring.h>
#endif

/* =========================================================================
 * 平台兼容性宏
 * ========================================================================= */
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
#if !defined(TIOCMBIS) || !defined(TIOCMBIC)
#define USE_TIOCMGET_SET_FOR_DTR_RTS
#endif
#ifdef __sun
#define USE_CBAUD_HACK
#endif

#define BUFFSIZE 2048

/* =========================================================================
 * 平台私有数据（继承 XIODevicePrivate）
 * 对标 Windows XSerialPortPrivate（含 XEventContext_IOCP read/write）
 * ========================================================================= */
typedef struct XSerialPortPrivate {
    XIODevicePrivate base;
    int fd;
    struct termios originalTermios;
    bool isCustomBaud;
    int32_t customBaudRate;

    /* io_uring 异步 I/O 上下文（对标 Windows XEventContext_IOCP read/write） */
    XEventContext_IO read;
    XEventContext_IO write;
    char readBuff[BUFFSIZE];
    char writeBuff[BUFFSIZE];
} XSerialPortPrivate;

/* 便捷转换宏 */
#define SPP(p) ((XSerialPortPrivate*)((XIODevice*)(p))->m_d)

/* =========================================================================
 * 前向声明
 * ========================================================================= */
bool XSerialPort_platform_applyConfig(XSerialPort* port);

/* =========================================================================
 * 波特率映射
 * ========================================================================= */
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
        return B38400;
    }
}

/* =========================================================================
 * 自定义波特率（仅 Linux）
 * ========================================================================= */
static bool setCustomBaudRate(int fd, int32_t baud) {
#ifdef __linux__
    struct serial_struct serinfo;
    if (ioctl(fd, TIOCGSERIAL, &serinfo) == 0) {
        serinfo.flags &= ~ASYNC_SPD_MASK;
        serinfo.flags |= ASYNC_SPD_CUST;
        serinfo.custom_divisor = (serinfo.baud_base + (baud / 2)) / baud;
        if (serinfo.custom_divisor < 1) serinfo.custom_divisor = 1;
        if (ioctl(fd, TIOCSSERIAL, &serinfo) == 0) {
            return true;
        }
    }
#endif
    (void)fd; (void)baud;
    return false;
}

/* =========================================================================
 * 应用配置到 termios
 * ========================================================================= */
static bool applyTermios(XSerialPort* port) {
    XSerialPortPrivate* priv = SPP(port);
    if (!priv || priv->fd < 0) return false;
    
    struct termios tio = priv->originalTermios;
    cfmakeraw(&tio);
    tio.c_cflag |= (CLOCAL | CREAD);
    tio.c_cflag &= ~(CSIZE | PARENB | PARODD | CMSPAR | CSTOPB | CRTSCTS);

    switch (port->dataBits) {
    case XSerialPort_Data5: tio.c_cflag |= CS5; break;
    case XSerialPort_Data6: tio.c_cflag |= CS6; break;
    case XSerialPort_Data7: tio.c_cflag |= CS7; break;
    case XSerialPort_Data8:
    default:                tio.c_cflag |= CS8; break;
    }

    switch (port->parity) {
    case XSerialPort_NoParity:   break;
    case XSerialPort_EvenParity: tio.c_cflag |= PARENB; break;
    case XSerialPort_OddParity:  tio.c_cflag |= (PARENB | PARODD); break;
    case XSerialPort_SpaceParity:
        if (CMSPAR) tio.c_cflag |= (PARENB | CMSPAR); break;
    case XSerialPort_MarkParity:
        if (CMSPAR) tio.c_cflag |= (PARENB | PARODD | CMSPAR); break;
    }

    if (port->stopBits == XSerialPort_TwoStop)
        tio.c_cflag |= CSTOPB;

    if (port->flowControl == XSerialPort_HardwareControl && CRTSCTS) {
        tio.c_cflag |= CRTSCTS;
    } else if (port->flowControl == XSerialPort_SoftwareControl) {
        tio.c_iflag |= (IXON | IXOFF | IXANY);
    }

    bool isCustom = false;
    speed_t stdBaud = toTermiosBaud(port->baudRate, &isCustom);

#ifdef USE_CBAUD_HACK
    tio.c_cflag &= ~CBAUD;
    tio.c_cflag |= (stdBaud & CBAUD);
#else
    cfsetispeed(&tio, stdBaud);
    cfsetospeed(&tio, stdBaud);
#endif

    if (tcsetattr(priv->fd, TCSANOW, &tio) != 0)
        return false;

    if (isCustom) {
        priv->customBaudRate = port->baudRate;
        priv->isCustomBaud = true;
        setCustomBaudRate(priv->fd, port->baudRate);
    } else {
        priv->isCustomBaud = false;
    }

    return true;
}

/* =========================================================================
 * 调制解调器控制线
 * ========================================================================= */
static bool setModemControlLine(int fd, int line, bool set) {
#ifdef USE_TIOCMGET_SET_FOR_DTR_RTS
    int status = 0;
    if (ioctl(fd, TIOCMGET, &status) != 0) return false;
    if (set) status |= line;
    else status &= ~line;
    return ioctl(fd, TIOCMSET, &status) == 0;
#else
    int cmd = set ? TIOCMBIS : TIOCMBIC;
    return ioctl(fd, cmd, &line) == 0;
#endif
}

/* =========================================================================
 * io_uring 异步 I/O 辅助函数
 * ========================================================================= */

/* 提交 io_uring 异步读请求（对标 Windows ReadFile + OVERLAPPED） */
static bool submitReadSqe(XSerialPortPrivate* priv) {
#ifdef __linux__
    XNetIoRingPosix* ring = (XNetIoRingPosix*)XAbstractNetIoRing_global();
    if (!ring) return false;

    struct io_uring_sqe* sqe = XNetIoRingPosix_getSqe(ring);
    if (!sqe) return false;

    memset(sqe, 0, sizeof(*sqe));
    sqe->opcode = IORING_OP_READ;
    sqe->fd = priv->fd;
    sqe->addr = (uint64_t)(uintptr_t)priv->readBuff;
    sqe->len = BUFFSIZE;
    sqe->user_data = (uint64_t)(uintptr_t)&priv->read.base;

    XNetIoRingPosix_submitSqe(ring, 1);
    return true;
#else
    (void)priv;
    return false;
#endif
}

/* 提交 io_uring 异步写请求（对标 Windows WriteFile + OVERLAPPED） */
static bool submitWriteSqe(XSerialPortPrivate* priv, size_t len) {
#ifdef __linux__
    XNetIoRingPosix* ring = (XNetIoRingPosix*)XAbstractNetIoRing_global();
    if (!ring) return false;

    struct io_uring_sqe* sqe = XNetIoRingPosix_getSqe(ring);
    if (!sqe) return false;

    memset(sqe, 0, sizeof(*sqe));
    sqe->opcode = IORING_OP_WRITE;
    sqe->fd = priv->fd;
    sqe->addr = (uint64_t)(uintptr_t)priv->writeBuff;
    sqe->len = (unsigned)len;
    sqe->user_data = (uint64_t)(uintptr_t)&priv->write.base;

    XNetIoRingPosix_submitSqe(ring, 1);
    return true;
#else
    (void)priv; (void)len;
    return false;
#endif
}

/* =========================================================================
 * 私有数据管理
 * ========================================================================= */
static XSerialPortPrivate* XSerialPortPrivate_create(void)
{
    XSerialPortPrivate* priv = (XSerialPortPrivate*)XCalloc_System(1, sizeof(XSerialPortPrivate));
    if (!priv) return NULL;
    XIODevicePrivate_init(&priv->base, NULL);
    priv->fd = -1;
    return priv;
}

static void XSerialPortPrivate_delete(XSerialPortPrivate* priv)
{
    if (!priv) return;
    XIODevicePrivate_deinit(&priv->base);
    XFree_System(priv);
}

/* =========================================================================
 * 构造 / 析构
 * ========================================================================= */
size_t XSerialPort_typetSize(void)
{
    return sizeof(XSerialPort);
}

void XSerialPort_init(XSerialPort* serial)
{
    if (serial == NULL) return;
    memset(((XIODevice*)serial) + 1, 0, sizeof(XSerialPort) - sizeof(XIODevice));
    XIODevice_init(serial);

    XSerialPortPrivate* priv = XSerialPortPrivate_create();
    if (!priv) return;
    if (((XIODevice*)serial)->m_d)
        XIODevicePrivate_delete(((XIODevice*)serial)->m_d);
    ((XIODevice*)serial)->m_d = (XIODevicePrivate*)priv;
    priv->base.q_ptr = (XIODevice*)serial;

    XClassGetVtable(serial) = XSerialPort_class_init();
    serial->baudRate = XSerialPort_Baud9600;
    serial->dataBits = XSerialPort_Data8;
    serial->parity = XSerialPort_NoParity;
    serial->stopBits = XSerialPort_OneStop;
    serial->flowControl = XSerialPort_NoFlowControl;
    serial->readyReadTriggered = false;
    serial->bytesWrittenTriggered = true;
    serial->readBufferSize = 512 * 1024;
}

XSerialPort* XSerialPort_create(void)
{
    XSerialPort* port = XNew(XSerialPort);
    if (!port) return NULL;
    XSerialPort_init(port);
    Set_Class_MemoryFree(port, XFree_System);
    return port;
}

/* =========================================================================
 * 事件处理（io_uring 异步完成事件）
 * 对标 Windows XSerialPort_platform_XChildEvent_handler
 * ========================================================================= */
void XSerialPort_platform_XChildEvent_handler(XEventSockAct* event, XSerialPort* receiver)
{
    XSerialPortPrivate* priv = SPP(receiver);
    if (!event || !priv || priv->fd < 0) return;

    int currentReadChannel = XIODevice_currentReadChannel(receiver);

    if (event->actType & XSocketAct_Read) {
        /* 读完成：对标 Windows 的 priv->read.finishedBytes 处理 */
        if (priv->read.base.finishedBytes) {
            XIODevicePrivate* d = ((XIODevice*)receiver)->m_d;
            struct XRingBuffer* readBuf = XIODevicePrivate_getOrCreateReadBuffer(d, currentReadChannel);
            if (readBuf) {
                int64_t bytesFromBuffer = XRingBuffer_write(readBuf, priv->readBuff,
                                                            priv->read.base.finishedBytes);
                if (bytesFromBuffer) {
                    receiver->readyReadTriggered = true;
                    XIODevice_readyRead_signal(receiver);
                    XIODevice_channelReadyRead_signal(receiver, currentReadChannel);
                }
            }
        }
        priv->read.base.finishedBytes = 0;
        /* 重新发起异步读（对标 Windows 的 ReadFile） */
        submitReadSqe(priv);
    }

    if (event->actType & XSocketAct_Write) {
        /* 写完成：对标 Windows 的 priv->write.finishedBytes 处理 */
        if (priv->write.base.finishedBytes) {
            XIODevice_bytesWritten_signal(receiver, (int64_t)priv->write.base.finishedBytes);
            XIODevice_channelBytesWritten_signal(receiver, currentReadChannel,
                                                 (int64_t)priv->write.base.finishedBytes);
        }
        priv->write.base.finishedBytes = 0;

        XIODevicePrivate* d = ((XIODevice*)receiver)->m_d;
        struct XRingBuffer* writeBuf = XIODevicePrivate_getOrCreateWriteBuffer(d, currentReadChannel);
        if (writeBuf) {
            int64_t bytesFromBuffer = XRingBuffer_read(writeBuf, priv->writeBuff,
                                                       (size_t)priv->write.base.bufferSize);
            if (bytesFromBuffer) {
                /* 设置写上下文（对标 Windows 的 WriteFile） */
                submitWriteSqe(priv, (size_t)bytesFromBuffer);
                receiver->bytesWrittenTriggered = false;
            } else {
                receiver->bytesWrittenTriggered = true;
            }
        }
    }

    XEvent_setAccepted_base(event, true);
}

/* =========================================================================
 * 平台函数实现
 * ========================================================================= */
bool XSerialPort_platform_open(XSerialPort* port, XIODeviceBaseMode mode) {
    if (!port->portName) return false;
    XSerialPortPrivate* priv = SPP(port);
    if (!priv) return false;

    int flags = O_NOCTTY | O_NONBLOCK;
    if ((mode & XIODevice_ReadOnly) && (mode & XIODevice_WriteOnly))
        flags |= O_RDWR;
    else if (mode & XIODevice_ReadOnly)
        flags |= O_RDONLY;
    else if (mode & XIODevice_WriteOnly)
        flags |= O_WRONLY;
    else
        return false;

    int fd = open(port->portName, flags);
    if (fd == -1) return false;

    if (tcgetattr(fd, &priv->originalTermios) != 0) {
        close(fd);
        return false;
    }

    priv->fd = fd;

    if (!XSerialPort_platform_applyConfig(port)) {
        close(fd);
        priv->fd = -1;
        return false;
    }

    port->isOpen = true;

    /* 分配 XFileDescriptor 统一标识符 */
    if (XIODevice_fd((XIODevice*)port) == XFD_INVALID) {
        XIODevice_setFd((XIODevice*)port, XFd_alloc(XFD_TYPE_SERIAL, priv, (XIODevice*)port));
    }

    /* 初始化 io_uring 异步读上下文（对标 Windows 的 ReadFile + OVERLAPPED） */
    memset(&priv->read, 0, sizeof(priv->read));
    priv->read.base.type = XEventContextType_Type_File;
    priv->read.base.fd = XIODevice_fd((XIODevice*)port);
    priv->read.base.buffer = priv->readBuff;
    priv->read.base.bufferSize = BUFFSIZE;
    priv->read.base.eventMask = XSocketAct_Read;
    priv->read.socket = XSocketDescriptor_fromIntptr(priv->fd);
    priv->read.base.finishedBytes = 0;

    /* 初始化 io_uring 异步写上下文 */
    memset(&priv->write, 0, sizeof(priv->write));
    priv->write.base.type = XEventContextType_Type_File;
    priv->write.base.fd = XIODevice_fd((XIODevice*)port);
    priv->write.base.buffer = priv->writeBuff;
    priv->write.base.bufferSize = BUFFSIZE;
    priv->write.base.eventMask = XSocketAct_Write;
    priv->write.socket = XSocketDescriptor_fromIntptr(priv->fd);
    priv->write.base.finishedBytes = 0;

    /* 发起首次异步读（对标 Windows 的 ReadFile） */
    submitReadSqe(priv);

    return true;
}

void XSerialPort_platform_close(XSerialPort* port) {
    XSerialPortPrivate* priv = SPP(port);
    if (!port->isOpen || !priv || priv->fd < 0) return;

    tcsetattr(priv->fd, TCSANOW, &priv->originalTermios);
    close(priv->fd);
    priv->fd = -1;
    port->isOpen = false;
}

int64_t XSerialPort_platform_read(XSerialPort* port, char* data, int64_t maxSize) {
    if (!port || !data || maxSize <= 0) return -1;
    XSerialPortPrivate* priv = SPP(port);
    if (!priv || priv->fd < 0) return -1;

    XIODevicePrivate* d = (XIODevicePrivate*)priv;
    int currentReadChannel = XIODevice_currentReadChannel(port);
    struct XRingBuffer* readBuf = XIODevicePrivate_getOrCreateReadBuffer(d, currentReadChannel);
    if (!readBuf) return -1;

    /* 等待缓冲区有足够数据（对标 Windows 的 processEvents 循环） */
    while (XRingBuffer_size_base(readBuf) < (size_t)maxSize) {
        XCoreApplication_processEvents(XEventLoop_AllEvents);
    }
    return XRingBuffer_read(readBuf, data, (size_t)maxSize);
}

int64_t XSerialPort_platform_write(XSerialPort* port, const char* data, int64_t len) {
    if (!port || !data || len <= 0) return -1;
    XSerialPortPrivate* priv = SPP(port);
    if (!priv || priv->fd < 0) return -1;

    XIODevicePrivate* d = (XIODevicePrivate*)priv;
    int currentWriteChannel = XIODevice_currentWriteChannel(port);
    struct XRingBuffer* writeBuf = XIODevicePrivate_getOrCreateWriteBuffer(d, currentWriteChannel);
    int64_t written = 0;

    if (!port->bytesWrittenTriggered && writeBuf) {
        /* 写入缓冲区 */
        written = XRingBuffer_write(writeBuf, data, (size_t)len);
    } else {
        /* 发起 io_uring 异步写（对标 Windows 的 WriteFile + OVERLAPPED） */
        memset(&priv->write, 0, sizeof(priv->write.base));
        priv->write.base.type = XEventContextType_Type_File;
        priv->write.base.fd = XIODevice_fd((XIODevice*)port);
        priv->write.base.buffer = priv->writeBuff;
        priv->write.base.bufferSize = BUFFSIZE;
        priv->write.base.eventMask = XSocketAct_Write;
        priv->write.socket = XSocketDescriptor_fromIntptr(priv->fd);
        priv->write.base.finishedBytes = 0;
        port->bytesWrittenTriggered = false;

        size_t toWrite = ((size_t)len <= BUFFSIZE) ? (size_t)len : BUFFSIZE;
        memcpy(priv->writeBuff, data, toWrite);
        if (submitWriteSqe(priv, toWrite)) {
            written = (int64_t)toWrite;
        } else {
            port->bytesWrittenTriggered = true;
            return -1;
        }

        if ((size_t)len > BUFFSIZE) {
            written += XRingBuffer_write(writeBuf, data + BUFFSIZE, (size_t)len - BUFFSIZE);
        }
    }
    return written;
}

bool XSerialPort_platform_applyConfig(XSerialPort* port) {
    return applyTermios(port);
}

XHandle XSerialPort_platform_handle(const XSerialPort* port) {
    XSerialPortPrivate* priv = SPP(port);
    return (priv && priv->fd >= 0) ? priv->fd : -1;
}

/* =========================================================================
 * 公共 API（平台相关实现）
 * ========================================================================= */
bool XSerialPort_setDataBits(XSerialPort* port, XSerialPort_DataBits dataBits) {
    if (!port) return false;
    if (port->dataBits == dataBits) return true;
    port->dataBits = dataBits;
    bool ok = true;
    if (XSerialPort_isOpen(port)) {
        ok = XSerialPort_platform_applyConfig(port);
    }
    if (ok) {
        XSerialPort_dataBitsChanged_signal(port, dataBits);
    }
    return ok;
}

bool XSerialPort_setParity(XSerialPort* port, XSerialPort_Parity parity) {
    if (!port) return false;
    if (port->parity == parity) return true;
    port->parity = parity;
    bool ok = true;
    if (XSerialPort_isOpen(port)) {
        ok = XSerialPort_platform_applyConfig(port);
    }
    if (ok) {
        XSerialPort_parityChanged_signal(port, parity);
    }
    return ok;
}

bool XSerialPort_setStopBits(XSerialPort* port, XSerialPort_StopBits stopBits) {
    if (!port) return false;
    if (port->stopBits == stopBits) return true;
    port->stopBits = stopBits;
    bool ok = true;
    if (XSerialPort_isOpen(port)) {
        ok = XSerialPort_platform_applyConfig(port);
    }
    if (ok) {
        XSerialPort_stopBitsChanged_signal(port, stopBits);
    }
    return ok;
}

bool XSerialPort_setFlowControl(XSerialPort* port, XSerialPort_FlowControl flowControl) {
    if (!port) return false;
    if (port->flowControl == flowControl) return true;
    port->flowControl = flowControl;
    bool ok = true;
    if (XSerialPort_isOpen(port)) {
        ok = XSerialPort_platform_applyConfig(port);
    }
    if (ok) {
        XSerialPort_flowControlChanged_signal(port, flowControl);
    }
    return ok;
}

bool XSerialPort_setDataTerminalReady(XSerialPort* port, bool set) {
    if (!port) return false;
    XSerialPortPrivate* priv = SPP(port);
    if (!port->isOpen || !priv || priv->fd < 0) return false;
    bool ok = setModemControlLine(priv->fd, TIOCM_DTR, set);
    if (ok) {
        port->dataTerminalReady = set;
        XSerialPort_dataTerminalReadyChanged_signal(port, set);
    }
    return ok;
}

bool XSerialPort_setRequestToSend(XSerialPort* port, bool set) {
    if (!port) return false;
    XSerialPortPrivate* priv = SPP(port);
    if (!port->isOpen || !priv || priv->fd < 0) return false;
    bool ok = setModemControlLine(priv->fd, TIOCM_RTS, set);
    if (ok) {
        port->requestToSend = set;
        XSerialPort_requestToSendChanged_signal(port, set);
    }
    return ok;
}

bool XSerialPort_setBreakEnabled(XSerialPort* port, bool set) {
    if (!port) return false;
    XSerialPortPrivate* priv = SPP(port);
    if (!port->isOpen || !priv || priv->fd < 0) return false;
    bool ok = (ioctl(priv->fd, set ? TIOCSBRK : TIOCCBRK, 0) == 0);
    if (ok) {
        port->breakEnabled = set;
        XSerialPort_breakEnabledChanged_signal(port, set);
    }
    return ok;
}

bool XSerialPort_flush(XSerialPort* port) {
    if (!port) return false;
    XSerialPortPrivate* priv = SPP(port);
    if (!port->isOpen || !priv || priv->fd < 0) return false;
    return tcdrain(priv->fd) == 0;
}

bool XSerialPort_clear(XSerialPort* port, XSerialPort_Direction dir) {
    if (!port) return false;
    XSerialPortPrivate* priv = SPP(port);
    if (!port->isOpen || !priv || priv->fd < 0) return false;
    int queue = TCIOFLUSH;
    if (dir == XSerialPort_Input) queue = TCIFLUSH;
    else if (dir == XSerialPort_Output) queue = TCOFLUSH;
    return tcflush(priv->fd, queue) == 0;
}

uint32_t XSerialPort_pinoutSignals(const XSerialPort* port) {
    if (!port || !port->isOpen) return 0;
    XSerialPortPrivate* priv = SPP(port);
    if (!priv || priv->fd < 0) return 0;
    int status = 0;
    if (ioctl(priv->fd, TIOCMGET, &status) != 0) return 0;
    uint32_t signals = 0;
    if (status & TIOCM_DTR) signals |= 0x04;
    if (status & TIOCM_RTS) signals |= 0x40;
    if (status & TIOCM_CTS) signals |= 0x80;
    if (status & TIOCM_DSR) signals |= 0x10;
    if (status & TIOCM_RI)  signals |= 0x20;
    if (status & TIOCM_CD)  signals |= 0x08;
    return signals;
}

#endif /* POSIX 平台 */