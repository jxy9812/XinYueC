/**
 * @file       XSerialPortInfo.h
 * @brief      串口信息类公开 API，对齐 Qt QSerialPortInfo。
 * @details    XSerialPortInfo 是值语义对象（继承 XClass，非 QObject），描述本机
 *          一个串口的静态信息：端口名、描述、厂商、序列号、USB VID/PID 以及
 *          占用状态。枚举入口 XSerialPortInfo_availablePorts() 返回端口列表，
 *          单端口查询入口 XSerialPortInfo_fromName() 按端口名取一条记录。
 *
 *          平台差异经设备层适配：查询以枚举模式打开串口设备，再经
 *          XDeviceSerialPortProperty_AvailablePorts 属性取得端口清单（便捷 API
 *          XDeviceSerialPort_availablePorts 内部即读该属性），本模块实现不直接
 *          调用 Win32/POSIX/STM32 平台 API。
 */
#ifndef XSERIALPORTINFO_H
#define XSERIALPORTINFO_H
#ifdef __cplusplus
extern "C" {
#endif
#include <stdint.h>
#include <stdbool.h>
#include "XClass.h"
#include "XString.h"
#include "XVector.h"
#include "XDeviceSerialPort.h"
#if XDEVICE_SERIALPORT_ON

/*XSerialPortInfo虚函数表*/
//XSerialPortInfo 继承 XClass，未新增虚函数，仅重载 XClass 的 Copy/Move/Deinit
XCLASS_DEFINE_BEGING(XSerialPortInfo)
XCLASS_DEFINE_EXTEND_END(XSerialPortInfo, XClass)

/**
 * @brief 串口信息类，对齐 Qt QSerialPortInfo。
 * @details 堆对象经 XSerialPortInfo_create* 创建并用 XClassDelete 释放，栈对象经
 *          XSerialPortInfo_init 初始化并用 XClassDeinit 反初始化。
 */
typedef struct XSerialPortInfo
{
    XClass m_class;                   /**< 基类子对象；第一个成员，由 XClass 管理，禁止手工修改。 */
    XString* m_portName;              /**< 端口名（如 "COM3"、"/dev/ttyUSB0"）；由对象拥有，NULL 表示未设置。 */
    XString* m_description;           /**< 人类可读描述；由对象拥有，可为 NULL。 */
    XString* m_manufacturer;          /**< 厂商名；由对象拥有，可为 NULL。 */
    XString* m_serialNumber;          /**< 序列号；由对象拥有，可为 NULL。 */
    uint32_t m_vendorId;              /**< USB 厂商 ID；0 表示未知或非 USB 设备。 */
    uint32_t m_productId;             /**< USB 产品 ID；0 表示未知或非 USB 设备。 */
    uint16_t m_busy          : 1;     /**< 端口是否已被占用。 */
    uint16_t m_valid         : 1;     /**< 记录是否有效（由枚举产生或按名查得）。 */
    uint16_t _reserved       : 14;    /**< 保留位，必须为 0。 */
} XSerialPortInfo;

/**
 * @brief 串口信息列表。
 * @details 元素为 XSerialPortInfo* 指针（sizeof(XSerialPortInfo*)），不是结构体值。
 *          列表与其中的元素均由调用方拥有：列表用 XClassDelete 释放，每个元素
 *          需逐个用 XClassDelete 释放。
 */
typedef XVector XSerialPortInfoList;

/**
 * @brief 初始化串口信息类的虚函数表。
 * @return 虚函数表指针，失败返回 NULL。
 */
XVtable* XSerialPortInfo_class_init(void);

/**
 * @brief 初始化已分配的串口信息对象（栈对象或内存池对象）。
 * @param info 指向未初始化的 XSerialPortInfo 实例；不可为 NULL。
 */
void XSerialPortInfo_init(XSerialPortInfo* info);

/**
 * @brief 创建串口信息对象。
 * @param memory 内存分配类型。
 * @return 新对象，需用 XClassDelete 释放；失败返回 NULL。
 */
XSerialPortInfo* XSerialPortInfo_create_ex(XMemoryType memory);

/**
 * @brief 拷贝构造串口信息对象。
 * @param other 源对象；不可为 NULL。
 * @return 新对象，需用 XClassDelete 释放；失败返回 NULL。
 */
XSerialPortInfo* XSerialPortInfo_create_copy(const XSerialPortInfo* other);

/**
 * @brief 获取端口名。
 * @param info 串口信息对象；不可为 NULL。
 * @return 端口名字符串（借用，指向对象内部）；未设置时返回 NULL。
 */
const XString* XSerialPortInfo_portName(const XSerialPortInfo* info);

/**
 * @brief 获取人类可读描述。
 * @param info 串口信息对象；不可为 NULL。
 * @return 描述字符串（借用）；平台未提供时返回 NULL。
 */
const XString* XSerialPortInfo_description(const XSerialPortInfo* info);

/**
 * @brief 获取厂商名。
 * @param info 串口信息对象；不可为 NULL。
 * @return 厂商名字符串（借用）；平台未提供时返回 NULL。
 */
const XString* XSerialPortInfo_manufacturer(const XSerialPortInfo* info);

/**
 * @brief 获取序列号。
 * @param info 串口信息对象；不可为 NULL。
 * @return 序列号字符串（借用）；平台未提供时返回 NULL。
 */
const XString* XSerialPortInfo_serialNumber(const XSerialPortInfo* info);

/**
 * @brief 获取 USB 厂商 ID。
 * @param info 串口信息对象；不可为 NULL。
 * @return 厂商 ID；未知或非 USB 设备时返回 0。
 */
uint32_t XSerialPortInfo_vendorId(const XSerialPortInfo* info);

/**
 * @brief 获取 USB 产品 ID。
 * @param info 串口信息对象；不可为 NULL。
 * @return 产品 ID；未知或非 USB 设备时返回 0。
 */
uint32_t XSerialPortInfo_productId(const XSerialPortInfo* info);

/**
 * @brief 查询端口是否已被占用。
 * @param info 串口信息对象；不可为 NULL。
 * @return 已被其它进程占用返回 true。
 * @note 平台不支持占用探测时恒返回 false。
 */
bool XSerialPortInfo_isBusy(const XSerialPortInfo* info);

/**
 * @brief 查询记录是否有效。
 * @param info 串口信息对象；不可为 NULL。
 * @return 有效返回 true。
 */
bool XSerialPortInfo_isValid(const XSerialPortInfo* info);

/**
 * @brief 枚举本机当前可用的串口。
 * @return 端口列表；调用方拥有，须用 XClassDelete 释放列表并逐个释放元素；
 *         枚举失败（平台枚举出错）时返回 NULL；本机无串口时返回空列表。
 */
XSerialPortInfoList* XSerialPortInfo_availablePorts(void);

/**
 * @brief 按端口名查找串口信息。
 * @param name 端口名（UTF-8，如 "COM3"、"/dev/ttyUSB0"）；不可为 NULL 或空串。
 * @return 匹配的信息对象，需用 XClassDelete 释放；未找到时返回 NULL。
 */
XSerialPortInfo* XSerialPortInfo_fromName(const char* name);

#endif /* XDEVICE_SERIALPORT_ON */

#ifdef __cplusplus
}
#endif

/* XClass create API default-memory wrappers. */
#undef XSerialPortInfo_create
#define XSerialPortInfo_create() XSerialPortInfo_create_ex(XCLASS_DEFAULT_MEMORY_TYPE)

#endif /* XSERIALPORTINFO_H */
