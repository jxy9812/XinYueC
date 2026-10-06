/* ==================== 神舟号 固件平台符号兜底 ====================
 * 库全量 GLOB 编译时，少数平台契约钩子只有 win32/posix 实现而嵌入式
 * 无对应驱动；本文件提供安全降级（无文件系统/无网络/无软件串口栈）：
 * - 文件 legacy 路径与目录/属性操作：一律失败（固件不落盘）；
 * - 串口平台层：创建失败（XSerialPortSTM32 为真机实现，本固件未接）；
 * - 面板铺底 fillPanelRects：no-op（板级 main 启动时已整屏填底色）。
 */
#include "XDeviceFile.h"
#include "XDeviceSerialPort.h"
#include "XAbstractNetIoRing.h"

/* mbedtls 不透明结构前置声明（存根不解引用）。 */
typedef struct mbedtls_x509_crt mbedtls_x509_crt;
typedef struct mbedtls_x509_crl mbedtls_x509_crl;
#include "XPlatformBackingStore.h"
#include "XPlatformNativeWindow.h"
#include "XDeviceNetwork.h"
#include "XNetworkInterface.h"
#include "XTypes.h"

/* ---------------- XDeviceFile legacy 平台钩子 ---------------- */
void XDeviceFile_legacyClose(XFd fd) { (void)fd; }

int64_t XDeviceFile_legacyRead(XFd fd, void* buffer, int64_t size)
{
    (void)fd; (void)buffer;
    return size ? (int64_t)-1 : 0;
}

int64_t XDeviceFile_legacyWrite(XFd fd, const void* data, int64_t size)
{
    (void)fd; (void)data;
    return size ? (int64_t)-1 : 0;
}

int64_t XDeviceFile_legacySeek(XFd fd, int64_t offset, XSeekWhence whence)
{
    (void)fd; (void)offset; (void)whence;
    return (int64_t)-1;
}

bool XDeviceFile_legacyFlush(XFd fd) { (void)fd; return false; }

bool XDeviceFile_legacyResize(XFd fd, int64_t size)
{
    (void)fd; (void)size;
    return false;
}

XFd XDeviceFile_legacyOpen(const XString* path, int mode, uint32_t flags,
                           int* error)
{
    (void)path; (void)mode; (void)flags;
    if (error) *error = -1;
    return XFD_INVALID;
}

bool XDeviceFile_legacyFstat(XFd fd, XFileStat* stat)
{
    (void)fd; (void)stat;
    return false;
}

bool XDeviceFile_legacySetFileTime(XFd fd, XFileTime timeType,
                                   int64_t timeValue)
{
    (void)fd; (void)timeType; (void)timeValue;
    return false;
}

void* XDeviceFile_legacyMap(XFd fd, int64_t offset, int64_t size, int flags)
{
    (void)fd; (void)offset; (void)size; (void)flags;
    return NULL;
}

bool XDeviceFile_legacyUnmap(void* addr, int64_t size)
{
    (void)addr; (void)size;
    return false;
}

/* ---------------- XDeviceFile 公共路径/目录操作（无文件系统） ---------------- */
XFd XDeviceFile_openStandardInput(int* error)
{
    if (error) *error = -1;
    return XFD_INVALID;
}

bool XDeviceFile_stat(const XString* path, XFileStat* stat)
{
    (void)path; (void)stat;
    return false;
}

bool XDeviceFile_remove(const XString* path, XRemoveMode mode,
                        XString* trashPath)
{
    (void)path; (void)mode; (void)trashPath;
    return false;
}

bool XDeviceFile_mkdir(const XString* path, bool recursive)
{
    (void)path; (void)recursive;
    return false;
}

bool XDeviceFile_resolvePath(const XString* path, XString* result,
                             XPathStyle style)
{
    (void)path; (void)result; (void)style;
    return false;
}

bool XDeviceFile_getSpecialPath(XSpecialPath type, XString* path)
{
    (void)type; (void)path;
    return false;
}

bool XDeviceFile_setPermissions(const XString* path,
                                XFilePermissions permissions)
{
    (void)path; (void)permissions;
    return false;
}


/* ---------------- XDeviceNetwork 平台钩子（无网络栈，全失败/空实现） ------ */
XDeviceNetworkContext* XDeviceNetwork_createContext(void) { return NULL; }

void XDeviceNetwork_deleteContext(XDeviceNetworkContext* context)
{
    (void)context;
}

void XDeviceNetwork_ensureInit(void) {}
void XDeviceNetwork_cleanup(void) {}

intptr_t XDeviceNetwork_socketDescriptor(XFd fd)
{
    (void)fd;
    return (intptr_t)-1;
}

uint16_t XDeviceNetwork_socketBind(XFd fd, const XHostAddress* address,
    uint16_t port, bool reuseAddr, bool shareAddr,
    XDeviceNetworkSocketType socketType)
{
    (void)fd; (void)address; (void)port; (void)reuseAddr; (void)shareAddr;
    (void)socketType;
    return 0;
}

bool XDeviceNetwork_socketConnect(XFd fd, const XString* hostName,
    uint16_t port, XDeviceNetworkProtocol protocol,
    XDeviceNetworkSocketType socketType)
{
    (void)fd; (void)hostName; (void)port; (void)protocol; (void)socketType;
    return false;
}

bool XDeviceNetwork_socketConnectLocal(XFd fd, const XString* endpoint,
    XDeviceNetworkLocalStreamType streamType, int timeoutMs,
    XDeviceNetworkSocketType socketType)
{
    (void)fd; (void)endpoint; (void)streamType; (void)timeoutMs;
    (void)socketType;
    return false;
}

void XDeviceNetwork_socketDisconnect(XFd fd) { (void)fd; }

int64_t XDeviceNetwork_socketRead(XFd fd, void* buffer, int64_t size,
    XDeviceNetworkSocketType socketType, void* ringBuffer)
{
    (void)fd; (void)buffer; (void)size; (void)socketType; (void)ringBuffer;
    return (int64_t)-1;
}

int64_t XDeviceNetwork_socketWrite(XFd fd, const void* data, int64_t size,
    XDeviceNetworkSocketType socketType, const XHostAddress* destination,
    uint16_t destinationPort, void* ringBuffer)
{
    (void)fd; (void)data; (void)size; (void)socketType; (void)destination;
    (void)destinationPort; (void)ringBuffer;
    return (int64_t)-1;
}

bool XDeviceNetwork_socketHandleEvent(XFd fd, void* event)
{
    (void)fd; (void)event;
    return false;
}

bool XDeviceNetwork_socketSetDescriptor(XFd fd, intptr_t socketDescriptor,
    int state, int openMode)
{
    (void)fd; (void)socketDescriptor; (void)state; (void)openMode;
    return false;
}

bool XDeviceNetwork_serverSetDescriptor(XFd fd, intptr_t socketDescriptor)
{
    (void)fd; (void)socketDescriptor;
    return false;
}

bool XDeviceNetwork_socketSetOption(XFd fd, int option, const void* value)
{
    (void)fd; (void)option; (void)value;
    return false;
}

void* XDeviceNetwork_socketGetOption(XFd fd, int option)
{
    (void)fd; (void)option;
    return NULL;
}

void XDeviceNetwork_socketSetReadBufferSize(XFd fd, int64_t size)
{
    (void)fd; (void)size;
}

const char* XDeviceNetwork_socketReadBuffer(XFd fd)
{
    (void)fd;
    return NULL;
}

size_t XDeviceNetwork_socketReadFinishedBytes(XFd fd)
{
    (void)fd;
    return 0;
}

size_t XDeviceNetwork_socketWriteFinishedBytes(XFd fd)
{
    (void)fd;
    return 0;
}

bool XDeviceNetwork_socketWritePending(XFd fd)
{
    (void)fd;
    return false;
}

void XDeviceNetwork_socketContinueRead(XFd fd, bool isUdp)
{
    (void)fd; (void)isUdp;
}

void XDeviceNetwork_socketContinueWrite(XFd fd, XRingBuffer* ringBuffer,
    bool isUdp)
{
    (void)fd; (void)ringBuffer; (void)isUdp;
}

XDeviceNetworkServerHandle XDeviceNetwork_serverCreate(XFd fd,
    const XHostAddress* address, uint16_t port, int backlog,
    bool reuseAddress)
{
    (void)fd; (void)address; (void)port; (void)backlog; (void)reuseAddress;
    return 0;
}

bool XDeviceNetwork_serverAccept(XFd fd)
{
    (void)fd;
    return false;
}

void XDeviceNetwork_serverClose(XFd fd, XDeviceNetworkServerHandle server)
{
    (void)fd; (void)server;
}

uint16_t XDeviceNetwork_serverPort(XDeviceNetworkServerHandle server)
{
    (void)server;
    return 0;
}

XDeviceNetworkSocketHandle XDeviceNetwork_serverGetAcceptedSocket(XFd fd,
    XHostAddress* clientAddress, uint16_t* clientPort)
{
    (void)fd; (void)clientAddress; (void)clientPort;
    return 0;
}

bool XDeviceNetwork_platformGetLastDatagramSender(XFd fd,
    XHostAddress* sourceAddress, uint16_t* sourcePort)
{
    (void)fd; (void)sourceAddress; (void)sourcePort;
    return false;
}

bool XDeviceNetwork_multicastGroup(XDeviceNetworkSocketHandle socketHandle,
    bool join, const XHostAddress* groupAddress, uint32_t interfaceIndex)
{
    (void)socketHandle; (void)join; (void)groupAddress; (void)interfaceIndex;
    return false;
}

/* XMulticastOp 为 XDeviceNetwork.c 私有枚举（ABI 等价 int）。 */
int XDeviceNetwork_multicastOp(XDeviceNetworkSocketHandle socketHandle,
    int operation, void* argument)
{
    (void)socketHandle; (void)operation; (void)argument;
    return -1;
}

XDeviceNetworkInterfaceIterator XDeviceNetwork_enumInterfacesBegin(void)
{
    return NULL;
}

struct XNetworkInterface* XDeviceNetwork_enumInterfacesNext(
    XDeviceNetworkInterfaceIterator iter)
{
    (void)iter;
    return NULL;
}

void XDeviceNetwork_enumInterfacesEnd(XDeviceNetworkInterfaceIterator iter)
{
    (void)iter;
}

/* ---------------- XDeviceNetwork 网卡配置（无承载系统网络栈：不支持） ------- */
bool XDeviceNetwork_interfaceConfigSupported(void)
{
    return false;
}

bool XDeviceNetwork_queryInterfaceConfig(uint32_t ifIndex,
                                         XDeviceNetworkInterfaceConfig* outConfig)
{
    (void)ifIndex;
    if (outConfig) memset(outConfig, 0, sizeof(*outConfig));
    return false;
}

void XDeviceNetwork_freeInterfaceConfig(XDeviceNetworkInterfaceConfig* config)
{
    if (!config) return;
    if (config->friendlyName) XClassDelete(config->friendlyName);
    XClassDeinit(&config->ipv4Address);
    XClassDeinit(&config->ipv4Netmask);
    XClassDeinit(&config->ipv4Gateway);
    XClassDeinit(&config->ipv6Address);
    XClassDeinit(&config->ipv6Gateway);
    XClassDeinit(&config->dhcpServer);
    XClassDeinit(&config->dnsPrimary);
    XClassDeinit(&config->dnsSecondary);
    memset(config, 0, sizeof(*config));
}

bool XDeviceNetwork_setInterfaceDhcp(uint32_t ifIndex)
{
    (void)ifIndex;
    return false;
}

bool XDeviceNetwork_setInterfaceStatic(uint32_t ifIndex, const char* ipv4Address,
                                       const char* ipv4Netmask,
                                       const char* ipv4Gateway,
                                       const char* dnsPrimary,
                                       const char* dnsSecondary)
{
    (void)ifIndex; (void)ipv4Address; (void)ipv4Netmask;
    (void)ipv4Gateway; (void)dnsPrimary; (void)dnsSecondary;
    return false;
}

bool XDeviceNetwork_setInterfaceStaticIpv6(uint32_t ifIndex,
                                           const char* ipv6Address,
                                           int prefixLength,
                                           const char* ipv6Gateway)
{
    (void)ifIndex; (void)ipv6Address; (void)prefixLength; (void)ipv6Gateway;
    return false;
}

int XDeviceNetwork_gssapiAuth(const XString* serviceName,
                              const XByteArray* inputToken,
                              XByteArray* outputToken, void** context)
{
    (void)serviceName; (void)inputToken; (void)outputToken; (void)context;
    return -1;
}

/* ---------------- XDeviceNetwork 计数器（无网络栈：恒零） ---------------- */
bool XDeviceNetwork_getNetworkCounters(uint64_t* rxBytes, uint64_t* txBytes)
{
    if (rxBytes) *rxBytes = 0;
    if (txBytes) *txBytes = 0;
    return true;
}

/* ---------------- XDeviceSerialPort 平台层（无串口设备栈） ---------------- */
XDeviceSerialPortContext* XDeviceSerialPort_platformCreateContext(void)
{
    return NULL;
}

void XDeviceSerialPort_platformDeleteContext(XDeviceSerialPortContext* context)
{
    (void)context;
}

bool XDeviceSerialPort_platformOpen(XFd fd, const XString* portName)
{
    (void)fd; (void)portName;
    return false;
}

void XDeviceSerialPort_platformClose(XFd fd) { (void)fd; }

int64_t XDeviceSerialPort_platformRead(XFd fd, void* buffer, int64_t size)
{
    (void)fd; (void)buffer;
    return size ? (int64_t)-1 : 0;
}

int64_t XDeviceSerialPort_platformWrite(XFd fd, const void* data,
                                        int64_t size)
{
    (void)fd; (void)data;
    return size ? (int64_t)-1 : 0;
}

bool XDeviceSerialPort_platformFlush(XFd fd) { (void)fd; return false; }

bool XDeviceSerialPort_platformGetProperty(XFd fd, XDeviceSerialPortProperty property,
                                           XVariant* value)
{
    (void)fd; (void)property; (void)value;
    return false;
}

bool XDeviceSerialPort_platformSetProperty(XFd fd, XDeviceSerialPortProperty property,
                                           const XVariant* value)
{
    (void)fd; (void)property; (void)value;
    return false;
}

bool XDeviceSerialPort_platformControl(XFd fd, int command, const XVariant* input,
                                       XVariant* output)
{
    (void)fd; (void)command; (void)input; (void)output;
    return false;
}

/* ---------------- XAbstractNetIoRing 平台工厂（无 IO 环实现） ------------ */
XAbstractNetIoRing* XAbstractNetIoRing_createPlatform(void)
{
    return NULL;
}

/* ---------------- mbedtls 文件型 X509（库裁剪 FS-IO，恒失败） ------------ */
int mbedtls_x509_crt_parse_path(mbedtls_x509_crt* chain, const char* path)
{
    (void)chain; (void)path;
    return -1;
}

int mbedtls_x509_crl_parse_file(mbedtls_x509_crl* chain, const char* path)
{
    (void)chain; (void)path;
    return -1;
}

/* ---------------- XPlatformNativeWindow 系统交互（头less：不支持） ------- */
/* 标题栏拖拽/停靠对齐在头less 下回退应用层实现（返回 false 即可）。 */
bool XPlatformNativeWindow_startSystemMove(XWindow* window)
{
    (void)window;
    return false;
}

bool XPlatformNativeWindow_dragFullWindows(void)
{
    return false;
}

/* ---------------- XPlatformBackingStore 面板铺底（启动已整屏填色） ------- */
void XPlatformBackingStore_fillPanelRects(const XRect* rects, int count,
                                          uint32_t nativePixel)
{
    (void)rects; (void)count; (void)nativePixel;
}
