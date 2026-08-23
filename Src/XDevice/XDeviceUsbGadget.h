/**
 * @file       XDeviceUsbGadget.h
 * @brief      USB Device/Gadget 从机统一设备接口（XDeviceUsbGadget）。
 * @details    XDeviceUsbGadget 是 XDevice 的 USB Device/Gadget 子类，注册类别
 *              usbgadget，作为 USB 从机被主机枚举的设备控制器。打开后统一以
 *             XFd 句柄暴露，描述符、Setup 请求、端点传输和事件通过控制命令
 *             或便捷 API 操作。
 *
 *             本文件合并原 XPlatform/XUsb/XUsbDeviceController.h 的功能，但把
 *             控制器生命周期、配置和端点传输收敛到统一设备门面。普通桌面
 *             Windows/Linux 主机硬件通常只有 Host 能力；如果目标硬件或驱动
 *             没有 USB Device/Gadget 能力，平台后端必须报告不支持，不能伪装
 *             成已经连接的从机。
 *
 *             配置描述符使用原始字节数组表达，以支持 CDC、HID、MSC、MIDI、
 *             自定义复合设备和厂商类。描述符数组和字符串内容由调用者持有，
 *             平台后端按需复制，不能擅自修改调用者数据。
 */
#ifndef XDEVICEUSBGADGET_H
#define XDEVICEUSBGADGET_H

#ifdef __cplusplus
extern C {
#endif

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "XDeviceUsbHost.h"

/* ============================================================================
 * 控制器状态 / 配置标志 / 能力
 * ============================================================================ */
/** @brief USB Device/Gadget 生命周期状态。 */
typedef enum XDeviceUsbGadgetState
{
    XDeviceUsbGadgetState_Closed = 0,   /**< 控制器尚未打开。 */
    XDeviceUsbGadgetState_Open,         /**< 控制器已打开但尚未连接主机。 */
    XDeviceUsbGadgetState_Started,      /**< 已启动并等待主机枚举。 */
    XDeviceUsbGadgetState_Configured,   /**< 主机已设置有效配置。 */
    XDeviceUsbGadgetState_Suspended,    /**< 主机已挂起设备。 */
    XDeviceUsbGadgetState_Error         /**< 控制器发生不可恢复错误。 */
} XDeviceUsbGadgetState;

/** @brief USB Device/Gadget 配置标志；可组合，后端不支持时返回 Unsupported。 */
typedef enum XDeviceUsbGadgetFlag
{
    XDeviceUsbGadgetFlag_None = 0,               /**< 无特殊配置。 */
    XDeviceUsbGadgetFlag_SelfPowered = 1u << 0,   /**< 设备由外部电源供电。 */
    XDeviceUsbGadgetFlag_RemoteWakeup = 1u << 1,  /**< 允许主机启用远程唤醒。 */
    XDeviceUsbGadgetFlag_Composite = 1u << 2,     /**< 设备包含多个 USB 接口。 */
    XDeviceUsbGadgetFlag_KeepDescriptors = 1u << 3, /**< 后端保留描述符副本。 */
    XDeviceUsbGadgetFlag_AutoConnect = 1u << 4,   /**< open 后自动启动连接。 */
    XDeviceUsbGadgetFlag_UseEventThread = 1u << 5 /**< 后端使用独立事件线程。 */
} XDeviceUsbGadgetFlag;
typedef uint32_t XDeviceUsbGadgetFlags;

/** @brief USB Device/Gadget 能力位；可组合，后端报告实际硬件支持。 */
typedef enum XDeviceUsbGadgetFeature
{
    XDeviceUsbGadgetFeature_None = 0,               /**< 没有可报告的能力。 */
    XDeviceUsbGadgetFeature_DeviceMode = 1u << 0,   /**< 支持 USB Device 模式。 */
    XDeviceUsbGadgetFeature_FullSpeed = 1u << 1,    /**< 支持 Full-Speed。 */
    XDeviceUsbGadgetFeature_HighSpeed = 1u << 2,    /**< 支持 High-Speed。 */
    XDeviceUsbGadgetFeature_SuperSpeed = 1u << 3,   /**< 支持 SuperSpeed。 */
    XDeviceUsbGadgetFeature_Composite = 1u << 4,    /**< 支持复合设备。 */
    XDeviceUsbGadgetFeature_BosDescriptor = 1u << 5, /**< 支持 BOS 描述符。 */
    XDeviceUsbGadgetFeature_MultipleConfigurations = 1u << 6, /**< 支持多配置。 */
    XDeviceUsbGadgetFeature_ControlCallback = 1u << 7, /**< 支持类/厂商请求回调。 */
    XDeviceUsbGadgetFeature_EndpointTransfer = 1u << 8, /**< 支持端点数据传输。 */
    XDeviceUsbGadgetFeature_AsyncTransfer = 1u << 9,  /**< 支持异步端点传输。 */
    XDeviceUsbGadgetFeature_EndpointStall = 1u << 10, /**< 支持端点 STALL 控制。 */
    XDeviceUsbGadgetFeature_RemoteWakeup = 1u << 11,  /**< 支持远程唤醒。 */
    XDeviceUsbGadgetFeature_SuspendResume = 1u << 12, /**< 支持挂起/恢复事件。 */
    XDeviceUsbGadgetFeature_EventCallback = 1u << 13, /**< 支持事件回调。 */
    XDeviceUsbGadgetFeature_ProcessEvents = 1u << 14, /**< 支持 processEvents。 */
    XDeviceUsbGadgetFeature_NativeHandle = 1u << 15,  /**< 支持原生句柄访问。 */
    XDeviceUsbGadgetFeature_DynamicDescriptors = 1u << 16 /**< 支持运行时描述符更新。 */
} XDeviceUsbGadgetFeature;
typedef uint32_t XDeviceUsbGadgetFeatures;

/* ============================================================================
 * 控制器通道与描述符配置
 * ============================================================================ */
/** @brief 逻辑控制器通道。 */
typedef struct XDeviceUsbGadgetChannel
{
    uint32_t m_controller; /**< USB Device 控制器逻辑编号。 */
    uint32_t m_channel;    /**< 控制器内通道逻辑编号。 */
    const char* m_name;    /**< 可选 UTF-8 平台名称；可为 NULL。 */
} XDeviceUsbGadgetChannel;

/** @brief USB 标准设备描述符字段。字符串使用索引，内容由 StringDescriptor 提供。 */
typedef struct XDeviceUsbGadgetDescriptor
{
    uint16_t m_bcdUSB;             /**< bcdUSB，例如 USB 2.0 使用 0x0200。 */
    uint8_t m_deviceClass;         /**< bDeviceClass。 */
    uint8_t m_deviceSubclass;      /**< bDeviceSubClass。 */
    uint8_t m_deviceProtocol;      /**< bDeviceProtocol。 */
    uint8_t m_maxPacketSize0;      /**< EP0 最大包大小，必须符合当前速度。 */
    uint16_t m_vendorId;           /**< idVendor。 */
    uint16_t m_productId;          /**< idProduct。 */
    uint16_t m_bcdDevice;          /**< bcdDevice。 */
    uint8_t m_manufacturerIndex;   /**< iManufacturer，0 表示无字符串。 */
    uint8_t m_productIndex;        /**< iProduct。 */
    uint8_t m_serialNumberIndex;   /**< iSerialNumber。 */
    uint8_t m_numConfigurations;   /**< bNumConfigurations。 */
} XDeviceUsbGadgetDescriptor;

/** @brief 配置描述符集合中的一个配置。 */
typedef struct XDeviceUsbGadgetConfigurationDescriptor
{
    uint8_t m_value;               /**< bConfigurationValue，不能为 0。 */
    uint8_t m_attributes;          /**< bmAttributes，bit7 必须为 1。 */
    uint16_t m_maxPowerMa;         /**< 最大总线电流，单位毫安。 */
    const uint8_t* m_descriptor;   /**< 完整原始配置描述符；借用的数组。 */
    size_t m_descriptorLength;     /**< 原始配置描述符字节数。 */
} XDeviceUsbGadgetConfigurationDescriptor;

/** @brief USB 字符串描述符的 UTF-8 输入值。 */
typedef struct XDeviceUsbGadgetStringDescriptor
{
    uint8_t m_index;         /**< 字符串描述符索引，不能为 0。 */
    uint16_t m_languageId;   /**< USB LANGID；0 表示后端默认语言。 */
    const char* m_utf8;      /**< UTF-8 字符串；借用的指针，不能为 NULL。 */
} XDeviceUsbGadgetStringDescriptor;

/** @brief USB Device/Gadget 控制器配置。 */
typedef struct XDeviceUsbGadgetConfig
{
    XDeviceUsbGadgetChannel m_channel;               /**< 逻辑控制器、通道和可选名称。 */
    XDeviceUsbSpeed m_speed;                         /**< 请求的 USB 速度。 */
    XDeviceUsbGadgetDescriptor m_deviceDescriptor;   /**< 设备描述符字段。 */
    const XDeviceUsbGadgetConfigurationDescriptor* m_configurations; /**< 配置描述符数组。 */
    size_t m_configurationCount;                     /**< 配置描述符数组长度。 */
    const uint8_t* m_bosDescriptor;                  /**< 可选完整 BOS 描述符。 */
    size_t m_bosDescriptorLength;                    /**< BOS 描述符字节数，0 表示没有。 */
    const XDeviceUsbGadgetStringDescriptor* m_strings; /**< 字符串描述符输入数组。 */
    size_t m_stringCount;                            /**< 字符串描述符数组长度。 */
    uint32_t m_rxQueueLength;                        /**< OUT 端点接收队列长度，0 使用默认值。 */
    uint32_t m_txQueueLength;                        /**< IN 端点发送队列长度，0 使用默认值。 */
    XDeviceUsbGadgetFlags m_flags;                   /**< XDeviceUsbGadgetFlag 按位组合。 */
    uint32_t m_reserved[2];                          /**< 保留字段，当前必须全部为 0。 */
} XDeviceUsbGadgetConfig;

/** @brief 设备控制器默认配置；Full-Speed、USB 2.0、EP0 64 字节。 */
#define XDEVICE_USB_GADGET_CONFIG_INIT                                            \
    {                                                                             \
        { 0u, 0u, (const char*)0 }, XDeviceUsbSpeed_Full,                         \
        { 0x0200u, 0u, 0u, 0u, 64u, 0u, 0u, 0u, 0u, 0u, 0u, 0u },                 \
        (const XDeviceUsbGadgetConfigurationDescriptor*)0, 0u, (const uint8_t*)0, \
        0u, (const XDeviceUsbGadgetStringDescriptor*)0, 0u, 0u, 0u,               \
        XDeviceUsbGadgetFlag_None, { 0u, 0u }                                     \
    }

/* ============================================================================
 * 端点 / Setup / 传输事件
 * ============================================================================ */
/** @brief USB Device/Gadget 端点信息。 */
typedef struct XDeviceUsbGadgetEndpointInfo
{
    XDeviceUsbEndpointAddress m_address;    /**< 端点地址，含方向位。 */
    XDeviceUsbTransferType m_transferType;  /**< 传输类型。 */
    uint16_t m_maxPacketSize;               /**< 当前速度下最大包大小。 */
    uint8_t m_interval;                     /**< bInterval。 */
    uint8_t m_interfaceNumber;              /**< 所属接口编号；未知为 0xff。 */
    uint8_t m_alternateSetting;             /**< 所属备用设置；未知为 0xff。 */
    uint8_t m_reserved;                     /**< 保留字段，必须为 0。 */
    uint32_t m_maxTransferSize;             /**< 单次传输上限；0 表示未知。 */
} XDeviceUsbGadgetEndpointInfo;

/** @brief 主机发给 Device 的 Setup 请求。 */
typedef struct XDeviceUsbGadgetSetupRequest
{
    uint8_t m_requestType; /**< bmRequestType。 */
    uint8_t m_request;     /**< bRequest。 */
    uint16_t m_value;      /**< wValue。 */
    uint16_t m_index;      /**< wIndex。 */
    uint16_t m_length;     /**< wLength，单位字节。 */
} XDeviceUsbGadgetSetupRequest;

/** @brief Setup 请求 bit7：1 表示 Device 向 Host 返回数据。 */
#define XDEVICE_USB_SETUP_REQUEST_IS_IN(requestType) (((requestType) & 0x80u) != 0u)
/** @brief Setup 请求 bmRequestType 的类型掩码。 */
#define XDEVICE_USB_SETUP_REQUEST_TYPE_MASK UINT8_C(0x60)
/** @brief Setup 请求为标准请求。 */
#define XDEVICE_USB_SETUP_REQUEST_TYPE_STANDARD UINT8_C(0x00)
/** @brief Setup 请求为类请求。 */
#define XDEVICE_USB_SETUP_REQUEST_TYPE_CLASS UINT8_C(0x20)
/** @brief Setup 请求为厂商请求。 */
#define XDEVICE_USB_SETUP_REQUEST_TYPE_VENDOR UINT8_C(0x40)

/** @brief 类请求或厂商请求的处理结果。 */
typedef enum XDeviceUsbGadgetSetupResult
{
    XDeviceUsbGadgetSetupResult_Handled = 0, /**< 已处理，后端发送正常状态阶段。 */
    XDeviceUsbGadgetSetupResult_Stall,       /**< 拒绝请求，后端对控制端点执行 STALL。 */
    XDeviceUsbGadgetSetupResult_NotHandled   /**< 未处理，由后端按未支持请求处理。 */
} XDeviceUsbGadgetSetupResult;

/** @brief 设备端点传输完成事件。 */
typedef struct XDeviceUsbGadgetTransferEvent
{
    XDeviceUsbEndpointAddress m_endpoint; /**< 完成传输的端点地址。 */
    XDeviceUsbTransferId m_transferId;    /**< 异步提交时返回的传输标识。 */
    XDeviceUsbTransferResult m_result;    /**< 最终传输结果。 */
    size_t m_transferred;                 /**< 实际传输字节数。 */
    XDeviceUsbCallbackContext m_context;  /**< 回调执行上下文。 */
} XDeviceUsbGadgetTransferEvent;

/** @brief USB Device/Gadget 事件类型。 */
typedef enum XDeviceUsbGadgetEventType
{
    XDeviceUsbGadgetEventType_Connected = 0, /**< 已连接或拉起 D+/D-。 */
    XDeviceUsbGadgetEventType_Disconnected,  /**< 已断开或撤销连接。 */
    XDeviceUsbGadgetEventType_Reset,         /**< 主机执行 USB Reset。 */
    XDeviceUsbGadgetEventType_Configured,    /**< 主机设置了有效配置。 */
    XDeviceUsbGadgetEventType_Deconfigured,  /**< 主机取消当前配置。 */
    XDeviceUsbGadgetEventType_SetInterface,  /**< 主机切换接口备用设置。 */
    XDeviceUsbGadgetEventType_Suspend,       /**< 主机挂起设备。 */
    XDeviceUsbGadgetEventType_Resume,        /**< 设备从挂起状态恢复。 */
    XDeviceUsbGadgetEventType_Setup,         /**< 收到类或厂商 Setup 请求。 */
    XDeviceUsbGadgetEventType_Transfer,      /**< 端点传输完成。 */
    XDeviceUsbGadgetEventType_Error          /**< 控制器或协议错误。 */
} XDeviceUsbGadgetEventType;

/** @brief USB Device/Gadget 事件快照。 */
typedef struct XDeviceUsbGadgetEvent
{
    XDeviceUsbGadgetEventType m_type;   /**< 事件类型。 */
    XDeviceUsbCallbackContext m_context;/**< 回调执行上下文。 */
    XDeviceUsbGadgetState m_state;      /**< 事件发生后的控制器状态。 */
    XDeviceUsbSpeed m_speed;            /**< 当前协商速度。 */
    uint8_t m_configurationValue;       /**< 当前配置值，未配置为 0。 */
    uint8_t m_interfaceNumber;          /**< 接口编号；无接口为 0xff。 */
    uint8_t m_alternateSetting;         /**< 备用设置；无接口为 0xff。 */
    uint8_t m_reserved;                 /**< 保留字段，必须为 0。 */
    XDeviceUsbEndpointAddress m_endpoint; /**< 相关端点；非端点事件为 0。 */
    XDeviceUsbGadgetSetupRequest m_setup;/**< Setup 事件对应的请求。 */
    XDeviceUsbGadgetTransferEvent m_transfer; /**< Transfer 事件对应的结果。 */
    XDeviceUsbError m_error;             /**< 通用错误；无错误为 None。 */
    int32_t m_nativeError;               /**< 平台原生错误码；无错误为 0。 */
} XDeviceUsbGadgetEvent;

/* ============================================================================
 * 回调类型
 * ============================================================================ */
/** @brief 类请求或厂商请求回调；标准设备请求由后端自动处理。 */
typedef XDeviceUsbGadgetSetupResult (*XDeviceUsbGadgetSetupCallback)(
    void* controller,
    const XDeviceUsbGadgetSetupRequest* request,
    void* data,
    size_t capacity,
    size_t* length,
    void* userData);

/** @brief 异步端点传输完成回调。 */
typedef void (*XDeviceUsbGadgetTransferCallback)(
    void* controller,
    XDeviceUsbTransferId transferId,
    const XDeviceUsbGadgetTransferEvent* event,
    void* userData);

/** @brief Device/Gadget 事件回调。 */
typedef void (*XDeviceUsbGadgetEventCallback)(
    void* controller,
    const XDeviceUsbGadgetEvent* event,
    void* userData);

/* ============================================================================
 * 打开选项 / 打开上下文 / 设备类
 * ============================================================================ */
/** @brief USB Device/Gadget 打开选项；首成员必须为 XDeviceOpenOptions。 */
typedef struct XDeviceUsbGadgetOpenOptions
{
    XDeviceOpenOptions m_base;   /**< 通用打开选项。 */
    XDeviceUsbGadgetConfig m_config; /**< 设备控制器配置。 */
} XDeviceUsbGadgetOpenOptions;

/** @brief USB Device/Gadget 打开上下文；后端句柄不对外暴露。 */
typedef struct XDeviceUsbGadgetContext
{
    XDeviceContext m_base;       /**< 通用设备上下文，首成员。 */
    void* m_backendController;   /**< 平台 Device 控制器句柄。 */
    XDeviceUsbGadgetState m_state; /**< 当前控制器状态。 */
} XDeviceUsbGadgetContext;

/** @brief XDeviceUsbGadget 类虚函数表；继承 XDevice，不新增虚槽。 */
XCLASS_DEFINE_BEGING(XDeviceUsbGadget)
XCLASS_DEFINE_EXTEND_END(XDeviceUsbGadget, XDevice)

/** @brief USB Device/Gadget 设备类对象；基类必须是第一个成员。 */
typedef struct XDeviceUsbGadget
{
    XDevice m_base;
} XDeviceUsbGadget;

/** @brief 初始化 USB Device/Gadget 设备类虚函数表。 @return 共享虚函数表，失败返回 NULL。 */
XVtable* XDeviceUsbGadget_class_init(void);
/** @brief 初始化已分配的 USB Device/Gadget 设备对象。 @param self 待初始化对象，不能为 NULL。 */
void XDeviceUsbGadget_init(XDeviceUsbGadget* self);
/** @brief 创建 USB Device/Gadget 设备类对象。 @return 新对象，失败返回 NULL；调用方负责释放。 */
XDeviceUsbGadget* XDeviceUsbGadget_create(void);
/** @brief 注册 USB Device/Gadget 设备类别。 @return 首次注册或已注册返回 true，失败返回 false。 */
bool XDeviceUsbGadget_register(void);

/** @brief 复用 XDevice 的打开、关闭和控制门面；参数契约与父类 API 相同。 */
#define XDeviceUsbGadget_open(options, error) \
    XDevice_openClass("usbgadget", (const XDeviceOpenOptions*)(options), (error))
#define XDeviceUsbGadget_close   XDevice_close
#define XDeviceUsbGadget_control XDevice_control

/* ============================================================================
 * 设备专有属性 / 命令
 * ============================================================================ */
/** @brief USB Device/Gadget 设备专有属性。 */
typedef enum XDeviceUsbGadgetProperty
{
    XDeviceUsbGadgetProperty_State = XDeviceProperty_Count, /**< 控制器状态，值为 XDeviceUsbGadgetState。 */
    XDeviceUsbGadgetProperty_Features,                      /**< 控制器能力，值为 XDeviceUsbGadgetFeatures。 */
    XDeviceUsbGadgetProperty_NativeHandle,                  /**< 后端原生句柄，值为 XDeviceUsbNativeHandle。 */
    XDeviceUsbGadgetProperty_Count                          /**< 属性数量边界。 */
} XDeviceUsbGadgetProperty;

/** @brief USB Device/Gadget 设备专有控制命令。 */
typedef enum XDeviceUsbGadgetCommand
{
    XDeviceUsbGadgetCommand_IsStarted = XDeviceCommand_Count, /**< 查询是否已启动。out: bool*。 */
    XDeviceUsbGadgetCommand_IsConfigured,                     /**< 查询是否已配置。out: bool*。 */
    XDeviceUsbGadgetCommand_GetConfig,                        /**< 获取配置副本。out: XDeviceUsbGadgetConfig*。 */
    XDeviceUsbGadgetCommand_Configure,                        /**< 配置描述符。in: const XDeviceUsbGadgetConfig*。 */
    XDeviceUsbGadgetCommand_Status,                           /**< 查询状态。out: XDeviceUsbGadgetState*。 */
    XDeviceUsbGadgetCommand_Start,                            /**< 启动连接。in/out 均为 NULL。 */
    XDeviceUsbGadgetCommand_Stop,                             /**< 停止连接。in/out 均为 NULL。 */
    XDeviceUsbGadgetCommand_SetSetupCallback,                 /**< 设置 Setup 回调。in: 回调, 用户数据。 */
    XDeviceUsbGadgetCommand_SetEventCallback,                 /**< 设置事件回调。in: 回调, 用户数据。 */
    XDeviceUsbGadgetCommand_ProcessEvents,                    /**< 处理事件。in: int32_t 超时；out: XDeviceUsbProcessResult*。 */
    XDeviceUsbGadgetCommand_GetEndpointInfo,                  /**< 获取端点信息。in: XDeviceUsbGadgetEndpointInfo*（借用）。 */
    XDeviceUsbGadgetCommand_Transfer,                         /**< 同步端点传输。in: 端点,数据,长度,transferred*,超时；out: XDeviceUsbTransferResult*。 */
    XDeviceUsbGadgetCommand_SubmitTransfer,                   /**< 提交异步传输。in: 端点,数据,长度,超时,回调,用户数据；out: XDeviceUsbTransferId*。 */
    XDeviceUsbGadgetCommand_CancelTransfer,                   /**< 取消异步传输。in: XDeviceUsbTransferId；out: XDeviceUsbTransferResult*。 */
    XDeviceUsbGadgetCommand_SetEndpointStalled,               /**< 设置端点 STALL。in: 端点地址, bool。 */
    XDeviceUsbGadgetCommand_ClearEndpointQueue,               /**< 清空端点队列。in: 端点地址。 */
    XDeviceUsbGadgetCommand_RemoteWakeup,                     /**< 请求远程唤醒。in/out 均为 NULL。 */
    XDeviceUsbGadgetCommand_Features,                         /**< 查询能力。out: XDeviceUsbGadgetFeatures*。 */
    XDeviceUsbGadgetCommand_Handle,                           /**< 查询原生句柄。out: XDeviceUsbNativeHandle*。 */
    XDeviceUsbGadgetCommand_LastError,                        /**< 查询最近错误。out: XDeviceUsbError*。 */
    XDeviceUsbGadgetCommand_NativeError,                      /**< 查询平台原生错误码。out: int32_t*。 */
    XDeviceUsbGadgetCommand_ClearError,                       /**< 清除错误。in/out 均为 NULL。 */
    XDeviceUsbGadgetCommand_Count                             /**< 命令数量边界。 */
} XDeviceUsbGadgetCommand;

/* ============================================================================
 * 便捷 API（统一通过 XFd 门面）
 * ============================================================================ */
/** @brief 查询控制器是否已由调用方配置。 @param fd 已打开的 USB Device/Gadget 描述符。 @return true 表示控制器已启动。 */
bool XDeviceUsbGadget_isStarted(XFd fd);
/** @brief 查询控制器是否已配置有效配置。 @param fd 已打开的 USB Device/Gadget 描述符。 @return true 表示已配置。 */
bool XDeviceUsbGadget_isConfigured(XFd fd);
/** @brief 获取当前配置副本。 @param fd 已打开的 USB Device/Gadget 描述符。 @param config 输出空间；不能为 NULL。 @return 成功返回 true。 */
bool XDeviceUsbGadget_getConfig(XFd fd, XDeviceUsbGadgetConfig* config);
/** @brief 设置或更新配置（描述符必须一次性提供）。 @param fd 已打开的 USB Device/Gadget 描述符。 @param config 配置输入；借用，不能为 NULL。 @return 成功返回 true。 */
bool XDeviceUsbGadget_configure(XFd fd, const XDeviceUsbGadgetConfig* config);
/** @brief 查询控制器生命周期状态。 @param fd 已打开的 USB Device/Gadget 描述符。 @return 当前状态；句柄无效返回 Closed。 */
XDeviceUsbGadgetState XDeviceUsbGadget_status(XFd fd);
/** @brief 启动控制器并等待主机枚举。 @param fd 已打开的 USB Device/Gadget 描述符。 @return 成功返回 true。 */
bool XDeviceUsbGadget_start(XFd fd);
/** @brief 停止控制器并断开连接。 @param fd 已打开的 USB Device/Gadget 描述符。 @return 成功返回 true。 */
bool XDeviceUsbGadget_stop(XFd fd);
/** @brief 设置类/厂商 Setup 回调；NULL 表示清除。 @param fd 已打开的 USB Device/Gadget 描述符。 @param callback 回调；可为 NULL。 @param userData 用户数据；可为 NULL。 @return 成功返回 true。 */
bool XDeviceUsbGadget_setSetupCallback(XFd fd, XDeviceUsbGadgetSetupCallback callback, void* userData);
/** @brief 设置事件回调；NULL 表示清除。 @param fd 已打开的 USB Device/Gadget 描述符。 @param callback 回调；可为 NULL。 @param userData 用户数据；可为 NULL。 @return 成功返回 true。 */
bool XDeviceUsbGadget_setEventCallback(XFd fd, XDeviceUsbGadgetEventCallback callback, void* userData);
/** @brief 在调用线程中处理事件和异步传输回调。 @param fd 已打开的 USB Device/Gadget 描述符。 @param timeoutMs 等待时间；0 非阻塞，负数一直等待。 @return 事件处理结果。 */
XDeviceUsbProcessResult XDeviceUsbGadget_processEvents(XFd fd, int32_t timeoutMs);
/** @brief 获取指定端点信息。 @param fd 已打开的 USB Device/Gadget 描述符。 @param endpoint 输出空间；不能为 NULL。 @return 成功返回 true。 */
bool XDeviceUsbGadget_getEndpointInfo(XFd fd, XDeviceUsbGadgetEndpointInfo* endpoint);
/** @brief 执行同步端点传输。 @param fd 已打开的 USB Device/Gadget 描述符。 @param endpoint 端点地址，不能为控制端点 0。 @param data IN 为输出，OUT 为输入。 @param length IN 为容量，OUT 为发送长度。 @param transferred 输出实际传输字节数；可为 NULL。 @param timeoutMs 超时；0 默认，负数不限。 @return 传输结果。 */
XDeviceUsbTransferResult XDeviceUsbGadget_transfer(XFd fd, XDeviceUsbEndpointAddress endpoint,
    void* data, size_t length, size_t* transferred, int32_t timeoutMs);
/** @brief 提交异步端点传输。 @param fd 已打开的 USB Device/Gadget 描述符。 @param endpoint 端点地址，不能为控制端点 0。 @param data IN 为输出，OUT 为输入。 @param length IN 为容量，OUT 为发送长度。 @param timeoutMs 超时。 @param callback 完成回调；不能为 NULL。 @param userData 用户数据；可为 NULL。 @return 传输标识；失败返回 XDEVICE_USB_INVALID_TRANSFER_ID。 */
XDeviceUsbTransferId XDeviceUsbGadget_submitTransfer(XFd fd, XDeviceUsbEndpointAddress endpoint,
    void* data, size_t length, int32_t timeoutMs,
    XDeviceUsbGadgetTransferCallback callback, void* userData);
/** @brief 取消未完成的异步传输。 @param fd 已打开的 USB Device/Gadget 描述符。 @param transferId 传输标识。 @return 取消请求结果。 */
XDeviceUsbTransferResult XDeviceUsbGadget_cancelTransfer(XFd fd, XDeviceUsbTransferId transferId);
/** @brief 设置或清除端点 STALL 状态。 @param fd 已打开的 USB Device/Gadget 描述符。 @param endpoint 端点地址。 @param stalled true 设置，false 清除。 @return 成功返回 true。 */
bool XDeviceUsbGadget_setEndpointStalled(XFd fd, XDeviceUsbEndpointAddress endpoint, bool stalled);
/** @brief 清空指定端点的待处理传输队列。 @param fd 已打开的 USB Device/Gadget 描述符。 @param endpoint 端点地址。 @return 成功返回 true。 */
bool XDeviceUsbGadget_clearEndpointQueue(XFd fd, XDeviceUsbEndpointAddress endpoint);
/** @brief 请求设备执行远程唤醒。 @param fd 已打开的 USB Device/Gadget 描述符。 @return 成功返回 true。 */
bool XDeviceUsbGadget_remoteWakeup(XFd fd);
/** @brief 获取控制器能力位。 @param fd 已打开的 USB Device/Gadget 描述符。 @return 能力位组合；句柄无效返回 None。 */
XDeviceUsbGadgetFeatures XDeviceUsbGadget_features(XFd fd);
/** @brief 判断控制器是否支持指定能力。 @param fd 已打开的 USB Device/Gadget 描述符。 @param feature 单个能力标志。 @return 支持返回 true。 */
bool XDeviceUsbGadget_hasFeature(XFd fd, XDeviceUsbGadgetFeature feature);
/** @brief 获取后端原生句柄。 @param fd 已打开的 USB Device/Gadget 描述符。 @return 借用句柄；无效返回 XDEVICE_USB_INVALID_NATIVE_HANDLE。 */
XDeviceUsbNativeHandle XDeviceUsbGadget_handle(XFd fd);
/** @brief 获取控制器最近一次通用错误。 @param fd 已打开的 USB Device/Gadget 描述符。 @return 错误值。 */
XDeviceUsbError XDeviceUsbGadget_lastError(XFd fd);
/** @brief 获取控制器最近一次平台原生错误码。 @param fd 已打开的 USB Device/Gadget 描述符。 @return 平台错误码；无错误返回 0。 */
int32_t XDeviceUsbGadget_nativeError(XFd fd);
/** @brief 清除控制器错误状态。 @param fd 已打开的 USB Device/Gadget 描述符。 @return 无。 */
void XDeviceUsbGadget_clearError(XFd fd);
/** @brief 获取 USB 通用错误的 UTF-8 描述。 @param error 错误值。 @return 静态 UTF-8 字符串；未知值返回 Unknown。 */
const char* XDeviceUsbGadget_errorString(XDeviceUsbError error);

#ifdef __cplusplus
}
#endif

#endif /* XDEVICEUSBGADGET_H */
