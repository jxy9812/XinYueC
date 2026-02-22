#ifdef __linux__ || defined(__APPLE__) || defined(__BSD__)
#include "XSerialPortPosix.h"
#include "XCircularQueue.h"
#include "XMemory.h"
#include "XPrintf.h"
#include <unistd.h>
#include <fcntl.h>
#include <termios.h>
#include <sys/ioctl.h>
#include <errno.h>
#include <string.h>
// 若系统未定义 CRTSCTS，则手动定义（Linux 标准值）
#ifndef CRTSCTS
#define CRTSCTS 020000000000  // 八进制，对应硬件流控制使能
#endif
// 前向声明虚函数
static void VXSerialPort_deinit(XSerialPort* serial);
static bool VXSerialPort_open(XSerialPort* serial, XIODeviceBaseMode mode);
static size_t VXIODevice_write(XSerialPort* serial, const char* data, size_t maxSize);
static size_t VXIODevice_writeFull(XSerialPort* serial);
static size_t VXIODevice_read(XSerialPort* serial, char* data, size_t maxSize);
static void VXIODevice_close(XSerialPort* serial);
static void VXIODevice_poll(XSerialPort* serial);
static void VXIODevice_setWriteBuffer(XSerialPort* serial, size_t count);
static void VXIODevice_setReadBuffer(XSerialPort* serial, size_t count);
static size_t VXIODevice_getBytesAvailable(XSerialPort* serial);

// 波特率映射表（Posix 波特率常量与数值对应）
static speed_t get_baud_rate(uint32_t baud) {
    switch (baud) {
        case 9600: return B9600;
        case 19200: return B19200;
        case 38400: return B38400;
        case 57600: return B57600;
        case 115200: return B115200;
        case 230400: return B230400;
        case 460800: return B460800;
        case 921600: return B921600;
        default: return B0; // 无效波特率
    }
}

// 虚函数表初始化
XVtable* XSerialPort_class_init() {
    XVTABLE_CREAT_DEFAULT
#if VTABLE_ISSTACK
    XVTABLE_STACK_INIT_DEFAULT(XSERIALPORT_VTABLE_SIZE)
#else
    XVTABLE_HEAP_INIT_DEFAULT
#endif
    // 继承父类虚函数表
    XVTABLE_INHERIT_DEFAULT(XIODeviceBase_class_init());
    // 重载虚函数
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Deinit, VXSerialPort_deinit);
    XVTABLE_OVERLOAD_DEFAULT(EXIODeviceBase_Open, VXSerialPort_open);
    XVTABLE_OVERLOAD_DEFAULT(EXIODeviceBase_Write, VXIODevice_write);
    XVTABLE_OVERLOAD_DEFAULT(EXIODeviceBase_WriteFull, VXIODevice_writeFull);
    XVTABLE_OVERLOAD_DEFAULT(EXIODeviceBase_Read, VXIODevice_read);
    XVTABLE_OVERLOAD_DEFAULT(EXIODeviceBase_Close, VXIODevice_close);
    XVTABLE_OVERLOAD_DEFAULT(EXObject_Poll, VXIODevice_poll);
    XVTABLE_OVERLOAD_DEFAULT(EXIODeviceBase_SetWriteBuffer, VXIODevice_setWriteBuffer);
    XVTABLE_OVERLOAD_DEFAULT(EXIODeviceBase_SetReadBuffer, VXIODevice_setReadBuffer);
    XVTABLE_OVERLOAD_DEFAULT(EXIODeviceBase_GetBytesAvailable, VXIODevice_getBytesAvailable);

#if SHOWCONTAINERSIZE
    XPrintf("XSerialPort(Posix) size:%d\n", XVtable_size(XVTABLE_DEFAULT));
#endif
    return XVTABLE_DEFAULT;
}

// 创建串口对象
XSerialPort* XSerialPort_create() {
    XSerialPort* serial = XMemory_malloc(sizeof(XSerialPort));
    if (serial) {
        XSerialPort_init(serial);
    }
    return serial;
}

// 初始化串口对象
void XSerialPort_init(XSerialPort* serial) {
    if (!serial) return;
    // 初始化父类及成员
    memset(((XSerialPortBase*)serial) + 1, 0, sizeof(XSerialPort) - sizeof(XSerialPortBase));
    XSerialPortBase_init(&serial->m_class);
    XClassGetVtable(serial) = XSerialPort_class_init();
    serial->m_fd = -1; // 初始化为无效文件描述符
    serial->m_readBufferSize = 1024;  // 默认读缓冲区大小
    serial->m_writeBufferSize = 1024; // 默认写缓冲区大小
}

// 释放资源
static void VXSerialPort_deinit(XSerialPort* serial) {
    if (!serial) return;
    VXIODevice_close(serial); // 确保关闭串口
    // 释放父类资源
    XVtableGetFunc(XIODeviceBase_class_init(), EXClass_Deinit, void(*)(XIODeviceBase*))(serial);
}

// 打开并配置串口
static bool VXSerialPort_open(XSerialPort* serial, XIODeviceBaseMode mode) {
    if (!serial) return false;
    XSerialPortBase* m_class = &serial->m_class;

    // 生成串口设备路径（如 /dev/ttyUSB0 或 /dev/ttyS0）
    char portPath[32];
    snprintf(portPath, sizeof(portPath), "/dev/ttyUSB%d", m_class->m_portNum);

    // 关闭已打开的串口
    if (serial->m_fd != -1) {
        close(serial->m_fd);
        serial->m_fd = -1;
    }

    // 打开串口设备（读写、非阻塞模式）
    int flags = O_NOCTTY;  // 先初始化基础标志（不预先设置读写标志）

    // 根据模式设置读写标志（互斥逻辑）
    if (mode & XIODeviceBase_ReadWrite) {
        // 读写模式（同时包含 ReadOnly 和 WriteOnly）
        flags |= O_RDWR;
    } else if (mode & XIODeviceBase_ReadOnly) {
        // 只读模式
        flags |= O_RDONLY;
    } else if (mode & XIODeviceBase_WriteOnly) {
        // 只写模式
        flags |= O_WRONLY;
    } else {
        // 无效模式（至少需要读或写权限）
        // 可根据需求添加错误处理，例如 return -1 或设置默认值
        return false;
    }

    flags |= O_NONBLOCK;  // 非阻塞模式
    serial->m_fd = open(portPath, flags);
    if (serial->m_fd == -1) {
        XPrintf("无法打开串口 %s，错误: %s\n", portPath, strerror(errno));
        return false;
    }

    // 保存原始配置（用于关闭时恢复）
    if (tcgetattr(serial->m_fd, &serial->m_oldTios) != 0) {
        XPrintf("获取串口配置失败，错误: %s\n", strerror(errno));
        close(serial->m_fd);
        serial->m_fd = -1;
        return false;
    }

    // 配置新参数
    struct termios tios = serial->m_oldTios;
    speed_t baud = get_baud_rate(m_class->m_baudRate);
    if (baud == B0) {
        XPrintf("不支持的波特率: %u\n", m_class->m_baudRate);
        close(serial->m_fd);
        serial->m_fd = -1;
        return false;
    }

    // 设置波特率
    cfsetispeed(&tios, baud);
    cfsetospeed(&tios, baud);

    // 配置数据位、停止位、校验位
    tios.c_cflag &= ~(CSIZE | PARENB | PARODD | CSTOPB); // 清除现有设置
    switch (m_class->m_dataBits) {
        case XSerialPort_Data5: tios.c_cflag |= CS5; break;
        case XSerialPort_Data6: tios.c_cflag |= CS6; break;
        case XSerialPort_Data7: tios.c_cflag |= CS7; break;
        case XSerialPort_Data8: tios.c_cflag |= CS8; break;
        default: 
            XPrintf("不支持的数据位: %d\n", m_class->m_dataBits);
            close(serial->m_fd);
            serial->m_fd = -1;
            return false;
    }

    // 校验位设置
    switch (m_class->m_parity) {
        case XSerialPort_NoParity:
            tios.c_cflag &= ~PARENB; // 无校验
            break;
        case XSerialPort_OddParity:
            tios.c_cflag |= (PARENB | PARODD); // 奇校验
            break;
        case XSerialPort_EvenParity:
            tios.c_cflag |= PARENB; // 偶校验（不设置PARODD）
            break;
        default:
            XPrintf("不支持的校验位: %d\n", m_class->m_parity);
            close(serial->m_fd);
            serial->m_fd = -1;
            return false;
    }

    // 停止位设置
    switch (m_class->m_stopBits) {
        case XSerialPort_OneStop:
            tios.c_cflag &= ~CSTOPB; // 1位停止位
            break;
        case XSerialPort_TwoStop:
            tios.c_cflag |= CSTOPB; // 2位停止位
            break;
        default:
            XPrintf("不支持的停止位: %d\n", m_class->m_stopBits);
            close(serial->m_fd);
            serial->m_fd = -1;
            return false;
    }

    // 流控制设置
    tios.c_cflag &= ~CRTSCTS; // 清除硬件流控制
    tios.c_iflag &= ~(IXON | IXOFF | IXANY); // 清除软件流控制
    switch (m_class->m_flowControl) {
        case XSerialPort_HardwareControl:
            tios.c_cflag |= CRTSCTS; // 硬件流控制（RTS/CTS）
            break;
        case XSerialPort_SoftwareControl:
            tios.c_iflag |= (IXON | IXOFF | IXANY); // 软件流控制（XON/XOFF）
            break;
        case XSerialPort_NoFlowControl:
        default:
            break; // 无流控制
    }

    // 启用接收器，设置本地模式
    tios.c_cflag |= (CLOCAL | CREAD);

    // 禁用特殊处理（不转换回车换行等）
    tios.c_oflag &= ~OPOST;
    tios.c_lflag &= ~(ICANON | ECHO | ECHOE | ISIG);
    tios.c_iflag &= ~(IGNBRK | BRKINT | PARMRK | ISTRIP | INLCR | IGNCR | ICRNL);

    // 设置超时（读取时无数据立即返回）
    tios.c_cc[VTIME] = 0; // 读取超时（百毫秒）
    tios.c_cc[VMIN] = 0;  // 最小读取字节数

    // 应用配置
    if (tcsetattr(serial->m_fd, TCSANOW, &tios) != 0) {
        XPrintf("设置串口配置失败，错误: %s\n", strerror(errno));
        close(serial->m_fd);
        serial->m_fd = -1;
        return false;
    }

    m_class->m_class.m_mode = mode;
    return true;
}

// 写入数据（带缓冲区处理）
static size_t VXIODevice_write(XSerialPort* serial, const char* data, size_t maxSize) {
    if (!serial || !data || maxSize == 0 || serial->m_fd == -1) {
        return 0;
    }
    XIODeviceBase* io = (XIODeviceBase*)serial;
    if (!(io->m_mode & XIODeviceBase_WriteOnly)) {
        return 0;
    }

    // 无缓冲区直接写入
    if (!io->m_writeBuffer) {
        return write(serial->m_fd, data, maxSize);
    }

    // 有缓冲区时先写入队列
    size_t count = 0;
    do {
        // 填充缓冲区
        while (count < maxSize && XCircularQueue_push_base(io->m_writeBuffer, data + count)) {
            count++;
        }
        // 缓冲区满时刷写
        if (XCircularQueue_isFull_base(io->m_writeBuffer)) {
            VXIODevice_writeFull(serial);
        }
    } while (count < maxSize && !XCircularQueue_isFull_base(io->m_writeBuffer));

    return count;
}

// 刷写缓冲区数据
static size_t VXIODevice_writeFull(XSerialPort* serial) {
    if (!serial || serial->m_fd == -1) {
        return 0;
    }
    XIODeviceBase* io = (XIODeviceBase*)serial;
    if (!(io->m_mode & XIODeviceBase_WriteOnly) || !io->m_writeBuffer) {
        return 0;
    }

    size_t count = 0;
    char c;
    while (XCircularQueue_receive_base(io->m_writeBuffer, &c)) {
        if (write(serial->m_fd, &c, 1) == 1) {
            count++;
        } else {
            // 写入失败时将数据放回缓冲区
            //XCircularQueue_unshift_base(io->m_writeBuffer, &c);
            break;
        }
    }
    return count;
}

// 读取数据
static size_t VXIODevice_read(XSerialPort* serial, char* data, size_t maxSize) {
    if (!serial || !data || maxSize == 0 || serial->m_fd == -1) {
        return 0;
    }
    XIODeviceBase* io = (XIODeviceBase*)serial;
    if (!(io->m_mode & XIODeviceBase_ReadOnly)) {
        return 0;
    }

    // 无缓冲区直接读取
    if (!io->m_readBuffer) {
        return read(serial->m_fd, data, maxSize);
    }

    // 有缓冲区时从队列读取
    size_t count = 0;
    while (count < maxSize && XCircularQueue_receive_base(io->m_readBuffer, data + count)) {
        count++;
    }
    return count;
}

// 关闭串口
static void VXIODevice_close(XSerialPort* serial) {
    if (!serial || serial->m_fd == -1) {
        return;
    }
    // 恢复原始配置
    tcsetattr(serial->m_fd, TCSANOW, &serial->m_oldTios);
    close(serial->m_fd);
    serial->m_fd = -1;
    serial->m_class.m_class.m_mode = 0; // 重置模式
}

// 轮询处理（用于非阻塞模式）
static void VXIODevice_poll(XSerialPort* serial) {
    if (!serial || serial->m_fd == -1) {
        return;
    }
    XIODeviceBase* io = (XIODeviceBase*)serial;

    // 读取数据到缓冲区
    if (io->m_readBuffer && (io->m_mode & XIODeviceBase_ReadOnly)) {
        char buf[128];
        ssize_t n = read(serial->m_fd, buf, sizeof(buf));
        if (n > 0) {
            for (ssize_t i = 0; i < n; i++) {
                XCircularQueue_push_base(io->m_readBuffer, &buf[i]);
            }
        }
    }

    // 处理写缓冲区
    if (io->m_writeBuffer && (io->m_mode & XIODeviceBase_WriteOnly)) {
        VXIODevice_writeFull(serial);
    }
}

// 设置写缓冲区大小
static void VXIODevice_setWriteBuffer(XSerialPort* serial, size_t count) {
    if (!serial) return;
    serial->m_writeBufferSize = count;
    XIODeviceBase_setWriteBuffer_base((XIODeviceBase*)serial, count);
}

// 设置读缓冲区大小
static void VXIODevice_setReadBuffer(XSerialPort* serial, size_t count) {
    if (!serial) return;
    serial->m_readBufferSize = count;
    XIODeviceBase_setReadBuffer_base((XIODeviceBase*)serial, count);
}

// 获取可用字节数（接收缓冲区）
static size_t VXIODevice_getBytesAvailable(XSerialPort* serial) {
    if (!serial || serial->m_fd == -1) {
        return 0;
    }
    // 无缓冲区时查询内核缓冲区
    if (!((XIODeviceBase*)serial)->m_readBuffer) {
        int bytes;
        ioctl(serial->m_fd, FIONREAD, &bytes);
        return (size_t)bytes;
    }
    // 有缓冲区时查询队列
    return XCircularQueue_size_base(((XIODeviceBase*)serial)->m_readBuffer);
}

#endif // Posix 平台