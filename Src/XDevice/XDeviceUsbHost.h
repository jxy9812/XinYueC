/**
 * @file       XDeviceUsbHost.h
 * @brief      USB Host 外接设备统一设备接口（XDeviceUsbHost）。
 * @details    XDeviceUsbHost 是 XDevice 的 USB Host 子类，继承 XDevice、注册类别
 *             "usbhost"，所有打开的外接 USB 设备以 XFd 句柄统一暴露。本文件同时
 *             是 USB 标准类型、匹配条件、描述符、控制传输、端点传输和热插拔
 *             的公共类型定义源，替代原 XPlatform/XUsb/XUsb.h。平台后端（Linux/
 *             Windows/STM32/ESP32）通过 Src/XDevice/XDeviceUsbHost.c 声明的平台入口
 *             接入，公共层只依赖 USB 标准描述符与端点传输，不包含 libusb、
 *             WinUSB、SetupAPI、STM32 HAL 或 ESP-IDF 头文件。
 *             USB Device/Gadget 从机模式请使用 XDeviceUsbGadget.h。
 */
#ifndef XDEVICEUSBHOST_H
#define XDEVICEUSBHOST_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "XDevice.h"

struct XVarList;
typedef struct XVarList XVarList;

/* ============================================================================
 * USB 公共标准类型
 * ============================================================================ */
/** @brief USB 端点地址；低 4 位是端点号，bit7 是方向位。 */
typedef uint8_t XDeviceUsbEndpointAddress;

/** @brief 异步传输标识；只在所属设备句柄生命周期内有效。 */
typedef uint64_t XDeviceUsbTransferId;

/** @brief 无效的异步传输标识。 */
#define XDEVICE_USB_INVALID_TRANSFER_ID UINT64_C(0)

/** @brief 取端点号（0-15）。 */
#define XDEVICE_USB_ENDPOINT_NUMBER(address) ((uint8_t)((address) & 0x0fu))
/** @brief 判断端点是否为 IN（设备到主机）方向。 */
#define XDEVICE_USB_ENDPOINT_IS_IN(address) (((address) & 0x80u) != 0u)
/** @brief 构造主机到设备方向的端点地址。 */
#define XDEVICE_USB_ENDPOINT_OUT(number) ((XDeviceUsbEndpointAddress)((number) & 0x0fu))
/** @brief 构造设备到主机方向的端点地址。 */
#define XDEVICE_USB_ENDPOINT_IN(number) ((XDeviceUsbEndpointAddress)(0x80u | ((number) & 0x0fu)))

/** @brief USB 连接速度。 */
typedef enum XDeviceUsbSpeed
{
    XDeviceUsbSpeed_Unknown = 0, /**< 后端无法确定速度。 */
    XDeviceUsbSpeed_Low,         /**< USB Low-Speed。 */
    XDeviceUsbSpeed_Full,        /**< USB Full-Speed，12 Mbps。 */
    XDeviceUsbSpeed_High,        /**< USB High-Speed，480 Mbps。 */
    XDeviceUsbSpeed_Super,       /**< USB SuperSpeed，5 Gbps。 */
    XDeviceUsbSpeed_SuperPlus    /**< USB SuperSpeedPlus，USB 3.x 及以上。 */
} XDeviceUsbSpeed;

/** @brief USB 端点传输类型。 */
typedef enum XDeviceUsbTransferType
{
    XDeviceUsbTransferType_Control = 0, /**< 控制传输；端点 0 专用。 */
    XDeviceUsbTransferType_Isochronous, /**< 等时传输。 */
    XDeviceUsbTransferType_Bulk,        /**< 批量传输。 */
    XDeviceUsbTransferType_Interrupt    /**< 中断传输。 */
} XDeviceUsbTransferType;

/** @brief USB 标准描述符类型。 */
typedef enum XDeviceUsbDescriptorType
{
    XDeviceUsbDescriptorType_Device = 0x01,         /**< 设备描述符。 */
    XDeviceUsbDescriptorType_Configuration = 0x02,  /**< 配置描述符。 */
    XDeviceUsbDescriptorType_String = 0x03,         /**< 字符串描述符。 */
    XDeviceUsbDescriptorType_Interface = 0x04,      /**< 接口描述符。 */
    XDeviceUsbDescriptorType_Endpoint = 0x05,       /**< 端点描述符。 */
    XDeviceUsbDescriptorType_DeviceQualifier = 0x06,/**< 设备限定描述符。 */
    XDeviceUsbDescriptorType_OtherSpeed = 0x07,     /**< 其他速度配置。 */
    XDeviceUsbDescriptorType_InterfacePower = 0x08, /**< 接口功率。 */
    XDeviceUsbDescriptorType_Bos = 0x0f,            /**< BOS 描述符。 */
    XDeviceUsbDescriptorType_DeviceCapability = 0x10/**< 设备能力。 */
} XDeviceUsbDescriptorType;

/** @brief USB 传输结果。 */
typedef enum XDeviceUsbTransferResult
{
    XDeviceUsbTransferResult_Ok = 0,            /**< 成功。 */
    XDeviceUsbTransferResult_Timeout,           /**< 超时。 */
    XDeviceUsbTransferResult_Stall,             /**< 设备返回 STALL。 */
    XDeviceUsbTransferResult_Cancelled,         /**< 传输被取消。 */
    XDeviceUsbTransferResult_Overflow,          /**< 接收数据超过缓冲区。 */
    XDeviceUsbTransferResult_InvalidArgument,   /**< 参数无效。 */
    XDeviceUsbTransferResult_Unsupported,       /**< 不支持该传输。 */
    XDeviceUsbTransferResult_Busy,              /**< 端点冲突操作。 */
    XDeviceUsbTransferResult_PermissionDenied,  /**< 权限不足。 */
    XDeviceUsbTransferResult_NoDevice,          /**< 设备不存在。 */
    XDeviceUsbTransferResult_IoError,           /**< 底层 I/O 失败。 */
    XDeviceUsbTransferResult_ControllerError,   /**< USB 控制器错误。 */
    XDeviceUsbTransferResult_ResourceError,     /**< 资源不足。 */
    XDeviceUsbTransferResult_Interrupted,       /**< 操作被中断。 */
    XDeviceUsbTransferResult_Unknown            /**< 未分类错误。 */
} XDeviceUsbTransferResult;

/** @brief USB 控制器事件处理结果。 */
typedef enum XDeviceUsbProcessResult
{
    XDeviceUsbProcessResult_Error = -1,  /**< 事件处理失败。 */
    XDeviceUsbProcessResult_Timeout = 0, /**< 指定时间内没有事件。 */
    XDeviceUsbProcessResult_Event = 1    /**< 至少分发了一个事件。 */
} XDeviceUsbProcessResult;

/** @brief USB 控制器和设备的通用错误。 */
typedef enum XDeviceUsbError
{
    XDeviceUsbError_None = 0,            /**< 无错误。 */
    XDeviceUsbError_InvalidArgument,     /**< 参数非法。 */
    XDeviceUsbError_NotOpen,             /**< 尚未打开。 */
    XDeviceUsbError_AlreadyOpen,         /**< 已经打开。 */
    XDeviceUsbError_NoDevice,            /**< 没有匹配的设备。 */
    XDeviceUsbError_Busy,                /**< 资源被占用。 */
    XDeviceUsbError_PermissionDenied,    /**< 权限不足。 */
    XDeviceUsbError_Timeout,             /**< 操作超时。 */
    XDeviceUsbError_Stall,               /**< 设备返回 STALL。 */
    XDeviceUsbError_Disconnected,        /**< 设备已断开。 */
    XDeviceUsbError_Unsupported,         /**< 不支持该操作。 */
    XDeviceUsbError_Resource,            /**< 内部资源不足。 */
    XDeviceUsbError_Controller,          /**< USB 控制器错误。 */
    XDeviceUsbError_Io,                  /**< 底层 I/O 错误。 */
    XDeviceUsbError_Interrupted,         /**< 操作被中断。 */
    XDeviceUsbError_Unknown              /**< 未分类错误。 */
} XDeviceUsbError;

/** @brief USB 回调执行上下文。 */
typedef enum XDeviceUsbCallbackContext
{
    XDeviceUsbCallbackContext_Unknown = 0, /**< 后端无法说明。 */
    XDeviceUsbCallbackContext_Interrupt,   /**< 中断上下文，禁止阻塞或分配。 */
    XDeviceUsbCallbackContext_Task,        /**< RTOS 任务或线程上下文。 */
    XDeviceUsbCallbackContext_Process      /**< processEvents 调用线程。 */
} XDeviceUsbCallbackContext;

/** @brief USB 热插拔事件类型。 */
typedef enum XDeviceUsbHotplugEventType
{
    XDeviceUsbHotplugEventType_Arrived = 0, /**< 设备已枚举并可打开。 */
    XDeviceUsbHotplugEventType_Removed,     /**< 设备已移除。 */
    XDeviceUsbHotplugEventType_Changed      /**< 描述符或配置发生变化。 */
} XDeviceUsbHotplugEventType;

/** @brief USB 控制器能力位标志；可组合。 */
typedef enum XDeviceUsbFeature
{
    XDeviceUsbFeature_None = 0,             /**< 无额外能力。 */
    XDeviceUsbFeature_Hotplug = 1u << 0,    /**< 支持热插拔事件。 */
    XDeviceUsbFeature_AsyncTransfer = 1u << 1, /**< 支持异步传输。 */
    XDeviceUsbFeature_Isochronous = 1u << 2, /**< 支持等时传输。 */
    XDeviceUsbFeature_HighSpeed = 1u << 3,  /**< 支持 High-Speed。 */
    XDeviceUsbFeature_SuperSpeed = 1u << 4, /**< 支持 SuperSpeed。 */
    XDeviceUsbFeature_Reset = 1u << 5,      /**< 支持复位设备。 */
    XDeviceUsbFeature_DescriptorCache = 1u << 6, /**< 支持描述符缓存。 */
    XDeviceUsbFeature_ProcessEvents = 1u << 7 /**< 支持 processEvents。 */
} XDeviceUsbFeature;
typedef uint32_t XDeviceUsbFeatures;

/** @brief 控制器创建配置标志；可组合。 */
typedef enum XDeviceUsbControllerFlag
{
    XDeviceUsbControllerFlag_None = 0,             /**< 后端默认行为。 */
    XDeviceUsbControllerFlag_Exclusive = 1u << 0,  /**< 独占访问。 */
    XDeviceUsbControllerFlag_EnableHotplug = 1u << 1, /**< 启用热插拔。 */
    XDeviceUsbControllerFlag_EnableAsync = 1u << 2 /**< 启用异步传输。 */
} XDeviceUsbControllerFlag;

/** @brief USB Host 控制器配置。 */
typedef struct XDeviceUsbControllerConfig
{
    uint32_t m_controller; /**< 逻辑控制器编号。 */
    uint32_t m_flags;      /**< XDeviceUsbControllerFlag 按位组合。 */
} XDeviceUsbControllerConfig;

/** @brief 控制器配置安全默认值。 */
#define XDEVICE_USB_CONTROLLER_CONFIG_INIT { 0u, XDeviceUsbControllerFlag_None }

/** @brief USB 设备匹配条件。 */
typedef struct XDeviceUsbDeviceSelector
{
    uint16_t m_vendorId;            /**< Vendor ID；0 表示任意。 */
    uint16_t m_productId;           /**< Product ID；0 表示任意。 */
    uint16_t m_bcdDevice;           /**< 设备版本；0 表示任意。 */
    uint8_t m_deviceClass;          /**< 设备类；0xff 表示任意。 */
    uint8_t m_deviceSubClass;       /**< 子类；0xff 表示任意。 */
    uint8_t m_deviceProtocol;       /**< 协议；0xff 表示任意。 */
    uint32_t m_index;               /**< 第 N 个匹配项，从 0 开始。 */
    const char* m_serialNumber_utf8;/**< UTF-8 序列号；借用，可为 NULL。 */
} XDeviceUsbDeviceSelector;

/** @brief 设备匹配条件安全默认值。 */
#define XDEVICE_USB_DEVICE_SELECTOR_INIT \
    { 0u, 0u, 0u, 0xffu, 0xffu, 0xffu, 0u, (const char*)0 }

/** @brief 已枚举设备的稳定信息快照。 */
typedef struct XDeviceUsbDeviceInfo
{
    uint32_t m_controller;          /**< 逻辑控制器编号。 */
    uint32_t m_bus;                 /**< 总线编号；未知为 0。 */
    uint32_t m_address;             /**< 枚举地址；未知为 0。 */
    uint64_t m_backendId;           /**< 后端打开令牌；仅同一控制器有效。 */
    uint16_t m_vendorId;            /**< Vendor ID。 */
    uint16_t m_productId;           /**< Product ID。 */
    uint16_t m_bcdDevice;           /**< 设备版本，BCD 编码。 */
    uint8_t m_deviceClass;          /**< 设备类。 */
    uint8_t m_deviceSubClass;       /**< 设备子类。 */
    uint8_t m_deviceProtocol;       /**< 设备协议。 */
    uint8_t m_configurationCount;   /**< 配置数量。 */
    uint8_t m_activeConfiguration;  /**< 当前配置值；未配置为 0。 */
    XDeviceUsbSpeed m_speed;        /**< 当前连接速度。 */
} XDeviceUsbDeviceInfo;

/** @brief USB 端点描述符通用快照。 */
typedef struct XDeviceUsbEndpointInfo
{
    XDeviceUsbEndpointAddress m_address;   /**< 端点地址，含方向位。 */
    XDeviceUsbTransferType m_transferType; /**< 传输类型。 */
    uint16_t m_maxPacketSize;              /**< 最大包大小。 */
    uint8_t m_interval;                    /**< 主机轮询间隔。 */
    uint8_t m_interfaceNumber;             /**< 所属接口编号。 */
    uint8_t m_alternateSetting;            /**< 备用设置编号。 */
    uint32_t m_maxTransferSize;            /**< 单次传输上限；0 表示未知。 */
} XDeviceUsbEndpointInfo;

/** @brief USB 控制传输 Setup 包字段。 */
typedef struct XDeviceUsbControlRequest
{
    uint8_t m_requestType; /**< bmRequestType。 */
    uint8_t m_request;     /**< bRequest。 */
    uint16_t m_value;      /**< wValue。 */
    uint16_t m_index;      /**< wIndex。 */
    uint16_t m_length;     /**< wLength。 */
} XDeviceUsbControlRequest;

/** @brief USB 异步传输请求。 */
typedef struct XDeviceUsbTransferRequest
{
    XDeviceUsbEndpointAddress m_endpoint;  /**< 目标端点地址。 */
    XDeviceUsbTransferType m_transferType; /**< 传输类型。 */
    void* m_data;                          /**< IN 输出，OUT 输入。 */
    size_t m_length;                       /**< 容量或发送长度。 */
    int32_t m_timeoutMs;                   /**< 超时；0 默认，负数不限。 */
    uint32_t m_flags;                      /**< 后端扩展标志；当前必须为 0。 */
} XDeviceUsbTransferRequest;

/** @brief USB 异步传输完成事件。 */
typedef struct XDeviceUsbTransferEvent
{
    XDeviceUsbTransferResult m_result;   /**< 最终结果。 */
    size_t m_transferred;                /**< 实际传输字节数。 */
    XDeviceUsbCallbackContext m_context; /**< 回调上下文。 */
} XDeviceUsbTransferEvent;

/** @brief USB 热插拔事件快照。 */
typedef struct XDeviceUsbHotplugEvent
{
    XDeviceUsbHotplugEventType m_type; /**< 事件类型。 */
    XDeviceUsbDeviceInfo m_device;     /**< 设备信息副本。 */
} XDeviceUsbHotplugEvent;

/** @brief 枚举回调。 */
typedef void (*XDeviceUsbEnumerateCallback)(void* controller,
                                            const XDeviceUsbDeviceInfo* device,
                                            void* userData);
/** @brief 热插拔回调。 */
typedef void (*XDeviceUsbHotplugCallback)(void* controller,
                                          const XDeviceUsbHotplugEvent* event,
                                          void* userData);
/** @brief 异步传输完成回调。 */
typedef void (*XDeviceUsbTransferCallback)(void* device,
                                           XDeviceUsbTransferId transferId,
                                           const XDeviceUsbTransferEvent* event,
                                           void* userData);

/** @brief USB 原生句柄类型；返回值为借用值。 */
typedef intptr_t XDeviceUsbNativeHandle;

/** @brief 无效 USB 原生句柄。 */
#define XDEVICE_USB_INVALID_NATIVE_HANDLE ((XDeviceUsbNativeHandle)-1)

/* ============================================================================
 * XDeviceUsbHost 打开选项 / 打开上下文
 * ============================================================================ */
/** @brief USB Host 设备打开选项；首成员必须为 XDeviceOpenOptions。 */
typedef struct XDeviceUsbOpenOptions
{
    XDeviceOpenOptions m_base;            /**< 通用打开选项。 */
    XDeviceUsbControllerConfig m_controller; /**< Host 控制器配置。 */
    XDeviceUsbDeviceSelector m_selector;  /**< 外接设备匹配条件。 */
    XDeviceUsbEndpointAddress m_ioEndpointIn;  /**< read 默认使用的 IN 端点。 */
    XDeviceUsbEndpointAddress m_ioEndpointOut; /**< write 默认使用的 OUT 端点。 */
    int32_t m_transferTimeoutMs;          /**< 通用传输超时；0 默认，负数不限。 */
} XDeviceUsbOpenOptions;

/** @brief USB Host 设备打开上下文；后端句柄不对外暴露。 */
typedef struct XDeviceUsbContext
{
    XDeviceContext m_base;              /**< 通用设备上下文，首成员。 */
    void* m_backendController;          /**< 平台 Host 控制器句柄。 */
    void* m_backendDevice;              /**< 平台外接设备句柄。 */
    XDeviceUsbDeviceInfo m_info;        /**< 最近一次设备信息快照。 */
    XDeviceUsbEndpointAddress m_ioEndpointIn;  /**< read 端点。 */
    XDeviceUsbEndpointAddress m_ioEndpointOut; /**< write 端点。 */
    int32_t m_transferTimeoutMs;        /**< 通用传输超时。 */
} XDeviceUsbContext;

/* ============================================================================
 * XDeviceUsbHost 设备类
 * ============================================================================ */
/** @brief XDeviceUsbHost 类虚函数表；继承 XDevice，不新增虚槽。 */
XCLASS_DEFINE_BEGING(XDeviceUsbHost)
XCLASS_DEFINE_EXTEND_END(XDeviceUsbHost, XDevice)

/** @brief USB Host 设备类对象；基类必须是第一个成员。 */
typedef struct XDeviceUsbHost
{
    XDevice m_base;
} XDeviceUsbHost;

/** @brief 初始化 USB Host 设备类虚函数表。 @return 共享虚函数表，失败返回 NULL。 */
XVtable* XDeviceUsbHost_class_init(void);
/** @brief 初始化已分配的 USB Host 设备对象。 @param self 待初始化对象，不能为 NULL。 */
void XDeviceUsbHost_init(XDeviceUsbHost* self);
/** @brief 创建 USB Host 设备类对象。 @return 新对象，失败返回 NULL；调用方负责释放。 */
XDeviceUsbHost* XDeviceUsbHost_create(void);
/** @brief 注册 USB Host 设备类别。 @return 首次注册或已注册返回 true，失败返回 false。 */
bool XDeviceUsbHost_register(void);

/** @brief 复用 XDevice 的打开、关闭和控制门面；参数契约与父类 API 相同。 */
#define XDeviceUsbHost_open(options, error) \
    XDevice_openClass("usbhost", (const XDeviceOpenOptions*)(options), (error))
#define XDeviceUsbHost_close   XDevice_close
#define XDeviceUsbHost_control XDevice_control
#define XDeviceUsbHost_read    XDevice_read
#define XDeviceUsbHost_write   XDevice_write
/* ============================================================================
 * XDeviceUsbHost 命令与属性
 * ============================================================================ */
/** @brief USB Host 设备专有属性编号；子类属性从父类 Count 值起编号。 */
typedef enum XDeviceUsbProperty
{
    XDeviceUsbProperty_Info = XDeviceProperty_Count, /**< 设备信息快照，值为 XDeviceUsbDeviceInfo。 */
    XDeviceUsbProperty_Speed,                        /**< 当前连接速度，值为 XDeviceUsbSpeed。 */
    XDeviceUsbProperty_Configuration,                /**< 当前配置值，值为 uint8_t。 */
    XDeviceUsbProperty_Features,                     /**< 控制器能力，值为 XDeviceUsbFeatures。 */
    XDeviceUsbProperty_NativeHandle,                 /**< 后端原生句柄，值为 XDeviceUsbNativeHandle。 */
    XDeviceUsbProperty_Count                         /**< 属性数量边界，不是有效属性。 */
} XDeviceUsbProperty;

/** @brief USB Host 设备专有控制命令；从父类命令数量起编号。 */
typedef enum XDeviceUsbCommand
{
    XDeviceUsbCommand_GetInfo = XDeviceCommand_Count, /**< 获取设备信息。out: XDeviceUsbDeviceInfo*。 */
    XDeviceUsbCommand_SetIoEndpoint,                  /**< 设置 read/write 端点。in: IN 端点, OUT 端点。 */
    XDeviceUsbCommand_GetDescriptor,                  /**< 读取原始描述符。in: 类型,索引,语言,数据,容量,transferred*,超时。 */
    XDeviceUsbCommand_GetStringDescriptor,            /**< 读取字符串描述符为 UTF-8。in: 索引,语言,数据,容量,length*,超时。 */
    XDeviceUsbCommand_SetConfiguration,               /**< 设置配置。in: uint8_t 配置值。 */
    XDeviceUsbCommand_GetConfiguration,               /**< 获取配置。out: uint8_t*。 */
    XDeviceUsbCommand_ClaimInterface,                 /**< 声明接口。in: uint8_t 接口号。 */
    XDeviceUsbCommand_ReleaseInterface,               /**< 释放接口。in: uint8_t 接口号。 */
    XDeviceUsbCommand_SetAlternateSetting,            /**< 设置备用设置。in: 接口号, 备用设置。 */
    XDeviceUsbCommand_EndpointCount,                  /**< 查询端点数量。in: 接口号, 备用设置；out: size_t*。 */
    XDeviceUsbCommand_GetEndpointInfo,                /**< 获取端点副本。in: 接口号, 备用设置, 索引；out: XDeviceUsbEndpointInfo*。 */
    XDeviceUsbCommand_ClearHalt,                      /**< 清除端点 Halt。in: 端点地址。 */
    XDeviceUsbCommand_Reset,                          /**< 复位设备。in/out 均为 NULL。 */
    XDeviceUsbCommand_ControlTransfer,                /**< 控制传输。in: 请求*, 数据, 容量, transferred*, 超时。 */
    XDeviceUsbCommand_Transfer,                       /**< 同步端点传输。in: 端点, 数据, 长度, transferred*, 超时。 */
    XDeviceUsbCommand_SubmitTransfer,                 /**< 提交异步传输。in: 请求*, 回调, 用户数据。 */
    XDeviceUsbCommand_CancelTransfer,                 /**< 取消异步传输。in: XDeviceUsbTransferId。 */
    XDeviceUsbCommand_Enumerate,                      /**< 枚举设备。in: 枚举回调, 用户数据。 */
    XDeviceUsbCommand_SetHotplugCallback,             /**< 设置热插拔回调。in: 回调, 用户数据。 */
    XDeviceUsbCommand_ProcessEvents,                  /**< 处理事件。in: int32_t 超时。 */
    XDeviceUsbCommand_Features,                       /**< 查询能力。out: XDeviceUsbFeatures*。 */
    XDeviceUsbCommand_Handle,                         /**< 查询原生句柄。out: XDeviceUsbNativeHandle*。 */
    XDeviceUsbCommand_LastError,                      /**< 查询最近一次错误。out: XDeviceUsbError*。 */
    XDeviceUsbCommand_NativeError,                    /**< 查询平台原生错误码。out: int32_t*。 */
    XDeviceUsbCommand_ClearError,                     /**< 清除错误状态。in/out 均为 NULL。 */
    XDeviceUsbCommand_Count                           /**< 命令数量边界，不是有效命令。 */
} XDeviceUsbCommand;

/* ============================================================================
 * XDeviceUsbHost 便捷 API（统一通过 XFd 门面）
 * ============================================================================ */

/**
 * @brief 获取 USB Host 设备信息快照。
 * @param fd 已打开的 USB Host 设备描述符。
 * @param info 调用方提供的输出空间；不能为 NULL。
 * @return 成功返回 true；参数非法或设备已断开返回 false。
 */
bool XDeviceUsbHost_getInfo(XFd fd, XDeviceUsbDeviceInfo* info);

/**
 * @brief 设置 read/write 门面使用的 IN 与 OUT 端点。
 * @param fd 已打开的 USB Host 设备描述符。
 * @param endpointIn read 使用的 IN 端点；0 表示保持原值。
 * @param endpointOut write 使用的 OUT 端点；0 表示保持原值。
 * @return 成功返回 true；参数非法返回 false。
 */
bool XDeviceUsbHost_setIoEndpoint(XFd fd, XDeviceUsbEndpointAddress endpointIn,
                              XDeviceUsbEndpointAddress endpointOut);

/**
 * @brief 读取 USB 原始描述符。
 * @param fd 已打开的 USB Host 设备描述符。
 * @param descriptorType 描述符类型。
 * @param descriptorIndex 同一类型中的描述符索引。
 * @param languageId 字符串描述符语言 ID；非字符串描述符传 0。
 * @param data 输出缓冲区；不能为 NULL。
 * @param capacity 缓冲区容量。
 * @param transferred 输出实际写入字节数；可为 NULL。
 * @param timeoutMs 超时；0 默认，负数不限。
 * @return 传输结果。
 */
XDeviceUsbTransferResult XDeviceUsbHost_getDescriptor(
    XFd fd, XDeviceUsbDescriptorType descriptorType, uint8_t descriptorIndex,
    uint16_t languageId, void* data, size_t capacity, size_t* transferred,
    int32_t timeoutMs);

/**
 * @brief 读取 USB 字符串描述符并转换为 UTF-8。
 * @param fd 已打开的 USB Host 设备描述符。
 * @param descriptorIndex 字符串描述符索引；0 表示语言列表。
 * @param languageId 语言 ID。
 * @param utf8 UTF-8 输出缓冲区；可为 NULL 查询容量。
 * @param capacity 缓冲区容量（含结尾 '\0'）。
 * @param length 输出字节数（不含结尾 '\0'）；可为 NULL。
 * @param timeoutMs 超时；0 默认，负数不限。
 * @return 传输结果；缓冲区不足返回 Overflow。
 */
XDeviceUsbTransferResult XDeviceUsbHost_getStringDescriptor_utf8(
    XFd fd, uint8_t descriptorIndex, uint16_t languageId, char* utf8,
    size_t capacity, size_t* length, int32_t timeoutMs);

/**
 * @brief 设置设备当前配置。
 * @param fd 已打开的 USB Host 设备描述符。
 * @param configurationValue 配置值；0 表示取消配置。
 * @return 成功返回 true；失败返回 false。
 */
bool XDeviceUsbHost_setConfiguration(XFd fd, uint8_t configurationValue);

/**
 * @brief 获取设备当前配置值。
 * @param fd 已打开的 USB Host 设备描述符。
 * @param configurationValue 输出配置值；不能为 NULL。
 * @return 成功返回 true；失败返回 false。
 */
bool XDeviceUsbHost_getConfiguration(XFd fd, uint8_t* configurationValue);

/**
 * @brief 声明并独占一个 USB 接口。
 * @param fd 已打开的 USB Host 设备描述符。
 * @param interfaceNumber 接口编号。
 * @return 成功返回 true；失败返回 false。
 */
bool XDeviceUsbHost_claimInterface(XFd fd, uint8_t interfaceNumber);

/**
 * @brief 释放一个已声明的 USB 接口。
 * @param fd 已打开的 USB Host 设备描述符。
 * @param interfaceNumber 接口编号。
 * @return 成功返回 true；失败返回 false。
 */
bool XDeviceUsbHost_releaseInterface(XFd fd, uint8_t interfaceNumber);

/**
 * @brief 设置接口备用设置。
 * @param fd 已打开的 USB Host 设备描述符。
 * @param interfaceNumber 接口编号。
 * @param alternateSetting 备用设置编号。
 * @return 成功返回 true；失败返回 false。
 */
bool XDeviceUsbHost_setAlternateSetting(XFd fd, uint8_t interfaceNumber,
                                    uint8_t alternateSetting);

/**
 * @brief 获取指定接口备用设置中的端点数量。
 * @param fd 已打开的 USB Host 设备描述符。
 * @param interfaceNumber 接口编号。
 * @param alternateSetting 备用设置编号。
 * @return 端点数量；失败或没有端点返回 0。
 */
size_t XDeviceUsbHost_endpointCount(XFd fd, uint8_t interfaceNumber,
                                uint8_t alternateSetting);

/**
 * @brief 获取指定接口备用设置中的端点描述符副本。
 * @param fd 已打开的 USB Host 设备描述符。
 * @param interfaceNumber 接口编号。
 * @param alternateSetting 备用设置编号。
 * @param index 端点索引，从 0 开始。
 * @param endpoint 输出空间；不能为 NULL。
 * @return 成功返回 true；失败返回 false。
 */
bool XDeviceUsbHost_getEndpointInfo(XFd fd, uint8_t interfaceNumber,
                                uint8_t alternateSetting, size_t index,
                                XDeviceUsbEndpointInfo* endpoint);

/**
 * @brief 清除指定端点的 Halt 状态。
 * @param fd 已打开的 USB Host 设备描述符。
 * @param endpoint 端点地址，不能为控制端点 0。
 * @return 成功返回 true；失败返回 false。
 */
bool XDeviceUsbHost_clearHalt(XFd fd, XDeviceUsbEndpointAddress endpoint);

/**
 * @brief 复位已打开的 USB 设备。
 * @param fd 已打开的 USB Host 设备描述符。
 * @return 成功返回 true；失败返回 false。
 */
bool XDeviceUsbHost_reset(XFd fd);

/**
 * @brief 执行一次 USB 控制传输。
 * @param fd 已打开的 USB Host 设备描述符。
 * @param request Setup 包字段；借用，不能为 NULL。
 * @param data 数据缓冲区；IN 为输出，OUT 为输入；无数据可为 NULL。
 * @param capacity data 容量。
 * @param transferred 输出实际传输字节数；可为 NULL。
 * @param timeoutMs 超时；0 默认，负数不限。
 * @return 传输结果。
 */
XDeviceUsbTransferResult XDeviceUsbHost_controlTransfer(
    XFd fd, const XDeviceUsbControlRequest* request, void* data,
    size_t capacity, size_t* transferred, int32_t timeoutMs);

/**
 * @brief 执行一次同步端点传输。
 * @param fd 已打开的 USB Host 设备描述符。
 * @param endpoint 端点地址，不能为控制端点 0。
 * @param data 数据缓冲区；IN 为输出，OUT 为输入。
 * @param length IN 为容量，OUT 为发送长度。
 * @param transferred 输出实际传输字节数；可为 NULL。
 * @param timeoutMs 超时；0 默认，负数不限。
 * @return 传输结果。
 */
XDeviceUsbTransferResult XDeviceUsbHost_transfer(
    XFd fd, XDeviceUsbEndpointAddress endpoint, void* data, size_t length,
    size_t* transferred, int32_t timeoutMs);

/**
 * @brief 提交一次异步端点传输。
 * @param fd 已打开的 USB Host 设备描述符。
 * @param request 异步请求；借用，提交时复制请求字段。
 * @param callback 完成回调；不能为 NULL。
 * @param userData 回调用户数据；后端只借用。
 * @return 异步传输标识；提交失败返回 XDEVICE_USB_INVALID_TRANSFER_ID。
 */
XDeviceUsbTransferId XDeviceUsbHost_submitTransfer(
    XFd fd, const XDeviceUsbTransferRequest* request,
    XDeviceUsbTransferCallback callback, void* userData);

/**
 * @brief 取消一个未完成的异步传输。
 * @param fd 已打开的 USB Host 设备描述符。
 * @param transferId 异步传输标识。
 * @return 取消请求结果。
 */
XDeviceUsbTransferResult XDeviceUsbHost_cancelTransfer(XFd fd,
                                                   XDeviceUsbTransferId transferId);

/**
 * @brief 枚举当前控制器下的 USB 设备。
 * @param fd 已打开的 USB Host 设备描述符。
 * @param callback 枚举回调；不能为 NULL。
 * @param userData 回调用户数据；可为 NULL。
 * @return 枚举正常结束返回 true；失败返回 false。
 */
bool XDeviceUsbHost_enumerate(XFd fd, XDeviceUsbEnumerateCallback callback,
                          void* userData);

/**
 * @brief 注册或清除热插拔回调。
 * @param fd 已打开的 USB Host 设备描述符。
 * @param callback 回调；NULL 表示清除。
 * @param userData 回调用户数据；可为 NULL。
 * @return 支持并设置成功返回 true；失败返回 false。
 */
bool XDeviceUsbHost_setHotplugCallback(XFd fd, XDeviceUsbHotplugCallback callback,
                                   void* userData);

/**
 * @brief 在调用线程中处理事件和异步传输回调。
 * @param fd 已打开的 USB Host 设备描述符。
 * @param timeoutMs 等待时间；0 非阻塞，正数毫秒，负数一直等待。
 * @return 事件处理结果。
 */
XDeviceUsbProcessResult XDeviceUsbHost_processEvents(XFd fd, int32_t timeoutMs);

/**
 * @brief 获取控制器能力位。
 * @param fd 已打开的 USB Host 设备描述符。
 * @return 能力位组合；句柄无效返回 XDeviceUsbFeature_None。
 */
XDeviceUsbFeatures XDeviceUsbHost_features(XFd fd);

/**
 * @brief 获取后端原生句柄。
 * @param fd 已打开的 USB Host 设备描述符。
 * @return 借用句柄；无效返回 XDEVICE_USB_INVALID_NATIVE_HANDLE。
 */
XDeviceUsbNativeHandle XDeviceUsbHost_handle(XFd fd);

/**
 * @brief 获取设备最近一次通用错误。
 * @param fd 已打开的 USB Host 设备描述符。
 * @return 错误值；句柄无效返回 XDeviceUsbError_InvalidArgument。
 */
XDeviceUsbError XDeviceUsbHost_lastError(XFd fd);

/**
 * @brief 获取设备最近一次平台原生错误码。
 * @param fd 已打开的 USB Host 设备描述符。
 * @return 平台错误码；无错误或句柄无效返回 0。
 */
int32_t XDeviceUsbHost_nativeError(XFd fd);

/**
 * @brief 清除设备错误状态。
 * @param fd 已打开的 USB Host 设备描述符。
 * @return 无。
 */
void XDeviceUsbHost_clearError(XFd fd);

/**
 * @brief 获取 USB 通用错误的 UTF-8 描述。
 * @param error 错误值。
 * @return 静态 UTF-8 字符串；未知值返回 "Unknown"。
 */
const char* XDeviceUsbHost_errorString(XDeviceUsbError error);

#ifdef __cplusplus
}
#endif

#endif /* XDEVICEUSBHOST_H */
