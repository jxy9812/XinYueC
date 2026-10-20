/**
 * @file XPlatformStub_esp8266.c
 * @brief ESP8266 最小控制台构建里尚未落地的平台函数存根。
 * @details
 * 这批函数是链接期才发现缺失的：库内多个模块无条件引用它们，但各自的平台
 * 实现只存在于 POSIX / Windows / STM32 侧。ESP8266 最小构建既没有文件系统
 * 也没有 RTC，因此这里一律返回"不支持/失败"，而不是编造行为：
 *
 *   - 时间：XDateTime_current*。ESP8266 无 RTC 时钟，FreeRTOS 的 tick 只
 *     提供相对时间。可靠的时间来源是 SNTP，但 NTP 模块已随 XProtocol 一并
 *     裁掉，故此处报失败而不是返回假的绝对时间。
 *   - 文件与目录：XDeviceFile_legacyOpen/Seek/Resize/Fstat/Map/Unmap/
 *     SetFileTime 与 XDeviceDir_platform*。板子上没有挂载任何文件系统，
 *     SPIFFS 也未启用，这些接口一律拒绝。
 *
 * 一旦产品启用 SPIFFS（SDK 自带 libspiffs）或 NTP，应把对应实现替换掉，
 * 届时这些存根需从构建中移除以免重复定义。
 */

#include "CXinYueConfig.h"   /* XPLATFORM_HAS_OS 守卫 */
#include "XDateTime.h"
#include "XDevice.h"
#include "XDeviceFile.h"
#include "XDeviceDir.h"
#include "XString.h"
#include "XAbstractNetIoRing.h"

#if defined(__ESP8266__) || defined(ESP8266)

/* ==================== 时间 ==================== */

/* 无 RTC 也无 NTP 时给不出可信的绝对时间，统一返回全零，由上层判为无效。
   XDateTime 是纯 POD，这里直接清零即可——库内没有 XDateTime_init 可用。 */
static XDateTime esp8266ZeroDateTime(void)
{
    XDateTime out;
    unsigned char* bytes = (unsigned char*)&out;
    size_t i;
    for (i = 0; i < sizeof(out); ++i)
        bytes[i] = 0;
    return out;
}

XDateTime XDateTime_currentDateTime(void)
{
    return esp8266ZeroDateTime();
}

XDateTime XDateTime_currentDateTimeUtc(void)
{
    return esp8266ZeroDateTime();
}

int64_t XDateTime_currentNSecsSinceEpoch(void)
{
    /* 返回 -1 表示"不可用"，与库内其它未实现的取时接口保持一致。 */
    return -1;
}

int64_t XDateTime_currentMSecsSinceEpoch(void)
{
    return -1;
}

/* ==================== 文件 ==================== */

XFd XDeviceFile_legacyOpen(const XString* path, int mode, uint32_t openFlags, int* error)
{
    (void)path; (void)mode; (void)openFlags;
    if (error) *error = -1;          /* -1：平台无标准文件后端 */
    return XFD_INVALID;
}

int64_t XDeviceFile_legacySeek(XFd fd, int64_t offset, XSeekWhence whence)
{
    (void)fd; (void)offset; (void)whence;
    return -1;
}

bool XDeviceFile_legacyResize(XFd fd, int64_t size)
{
    (void)fd; (void)size;
    return false;
}

bool XDeviceFile_legacyFstat(XFd fd, XFileStat* out)
{
    (void)fd; (void)out;
    return false;
}

void* XDeviceFile_legacyMap(XFd fd, int64_t offset, int64_t size)
{
    (void)fd; (void)offset; (void)size;
    return NULL;
}

bool XDeviceFile_legacyUnmap(void* addr, int64_t size)
{
    (void)addr; (void)size;
    return false;
}

bool XDeviceFile_legacySetFileTime(XFd fd, XFileTime timeType, int64_t timeValue)
{
    (void)fd; (void)timeType; (void)timeValue;
    return false;
}

/* ==================== 目录 ==================== */

XFd XDeviceDir_platformOpen(const XString* path)
{
    (void)path;
    return XFD_INVALID;
}

bool XDeviceDir_platformClose(void* backendHandle)
{
    (void)backendHandle;
    return false;
}

int64_t XDeviceDir_platformRead(void* backendHandle, XByteArray* buffer)
{
    (void)backendHandle; (void)buffer;
    return -1;
}

/* ==================== I/O 事件环 ==================== */

/* 只在「有 OS」口径下由平台侧提供。ESP8266 裸机单线程固件（本轮去掉
 * FreeRTOS 后 XPLATFORM_FREERTOS=0 ⇒ XPLATFORM_HAS_OS=0）由基类
 * XAbstractNetIoRing.c:411 提供同一符号；两侧同时定义会撞重复符号，
 * 故用同一个守卫口径。返回值语义见下。 */
#if XPLATFORM_HAS_OS
XAbstractNetIoRing* XAbstractNetIoRing_createPlatform(void)
{
    /* ESP8266 没有 epoll / IOCP / kqueue 的对应物，SDK 的事件循环靠 os_timer
     * 与队列驱动，不依赖 I/O 事件环。返回 NULL 让 XAbstractEventDispatcher
     * 走无事件环分支（它对 NULL 已做判空降级）。 */
    return NULL;
}
#endif /* XPLATFORM_HAS_OS */

#endif /* defined(__ESP8266__) || defined(ESP8266) */
