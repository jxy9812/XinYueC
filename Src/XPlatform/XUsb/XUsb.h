/**
 * @file       XUsb.h
 * @brief      USB Host 外接设备平台抽象接口。
 * @details    本文件定义 Linux、Windows、STM32 和 ESP32 USB Host 后端共同
 *             遵守的纯函数式接口。公共层只依赖 USB 标准描述符、控制传输、
 *             端点传输和不透明句柄，不包含 libusb、WinUSB、SetupAPI、
 *             STM32 USB Host 或 ESP-IDF 头文件。
 *
 *             本接口面向“主机访问外接 USB 设备”的场景：Linux/Windows 后端
 *             可以使用系统 USB 栈或用户态 USB 库，STM32/ESP32 后端可以使用
 *             USB Host/OTG 栈。CDC、HID、MSC 等设备类驱动应放在 Src/XCode
 *             或上层模块中，只通过本文件的控制传输和端点传输访问设备。
 *
 *             USB Device/Gadget 模式是主机枚举、设备描述符发布和端点事件
 *             回调的另一套生命周期，不由本文件伪装成 Host API；设备模式请
 *             使用同目录的 XUsbDeviceController.h 独立抽象。
 */
#ifndef XUSB_H
#define XUSB_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** @brief USB 控制器不透明句柄；由平台后端定义，调用者不能访问其成员。 */
typedef struct XUsbController XUsbController;

/** @brief 已打开的 USB 外接设备不透明句柄；由 XUsbDevice_open 返回。 */
typedef struct XUsbDevice XUsbDevice;

/** @brief 异步传输标识；只在所属设备句柄生命周期内有效。 */
typedef uint64_t XUsbTransferId;

/** @brief 无效的异步传输标识。 */
#define XUSB_INVALID_TRANSFER_ID UINT64_C(0)

/** @brief USB 端点地址类型，低 4 位是端点号，第 7 位是方向位。 */
typedef uint8_t XUsbEndpointAddress;

/**
 * @brief USB 端点地址最大编号。
 * @param address USB 端点地址。
 * @return 端点号，范围为 0 到 15。
 */
#define XUSB_ENDPOINT_NUMBER(address) ((uint8_t)((address) & 0x0fu))

/**
 * @brief 判断 USB 端点是否为设备到主机方向。
 * @param address USB 端点地址。
 * @return 第 7 位为 1 时返回 true，否则返回 false。
 */
#define XUSB_ENDPOINT_IS_IN(address) (((address) & 0x80u) != 0u)

/**
 * @brief 构造主机到设备方向的 USB 端点地址。
 * @param number 端点号；只使用低 4 位，必须小于等于 15。
 * @return 清除方向位后的端点地址。
 */
#define XUSB_ENDPOINT_OUT(number) ((XUsbEndpointAddress)((number) & 0x0fu))

/**
 * @brief 构造设备到主机方向的 USB 端点地址。
 * @param number 端点号；只使用低 4 位，必须小于等于 15。
 * @return 设置方向位后的端点地址。
 */
#define XUSB_ENDPOINT_IN(number) ((XUsbEndpointAddress)(0x80u | ((number) & 0x0fu)))

/**
 * @brief USB 设备速度。
 */
typedef enum XUsbSpeed {
    XUsbSpeed_Unknown = 0, /**< 后端无法确定设备速度。 */
    XUsbSpeed_Low,         /**< USB Low-Speed，通常用于低速 HID。 */
    XUsbSpeed_Full,        /**< USB Full-Speed，12 Mbps。 */
    XUsbSpeed_High,        /**< USB High-Speed，480 Mbps。 */
    XUsbSpeed_Super,       /**< USB SuperSpeed，USB 3.x 5 Gbps。 */
    XUsbSpeed_SuperPlus    /**< USB SuperSpeedPlus，USB 3.x 及更高速度。 */
} XUsbSpeed;

/**
 * @brief USB 端点传输类型。
 */
typedef enum XUsbTransferType {
    XUsbTransferType_Control = 0, /**< 控制传输；端点 0 专用。 */
    XUsbTransferType_Isochronous,  /**< 等时传输；强调时序，不保证重传。 */
    XUsbTransferType_Bulk,         /**< 批量传输；强调可靠性和吞吐量。 */
    XUsbTransferType_Interrupt     /**< 中断传输；由主机按间隔轮询。 */
} XUsbTransferType;

/**
 * @brief USB 标准描述符类型。
 */
typedef enum XUsbDescriptorType {
    XUsbDescriptorType_Device = 0x01,        /**< 设备描述符。 */
    XUsbDescriptorType_Configuration = 0x02, /**< 配置描述符。 */
    XUsbDescriptorType_String = 0x03,        /**< 字符串描述符。 */
    XUsbDescriptorType_Interface = 0x04,     /**< 接口描述符。 */
    XUsbDescriptorType_Endpoint = 0x05,      /**< 端点描述符。 */
    XUsbDescriptorType_DeviceQualifier = 0x06,/**< 设备限定描述符。 */
    XUsbDescriptorType_OtherSpeed = 0x07,    /**< 其他速度配置描述符。 */
    XUsbDescriptorType_InterfacePower = 0x08,/**< 接口功率描述符。 */
    XUsbDescriptorType_Bos = 0x0f,            /**< BOS 描述符。 */
    XUsbDescriptorType_DeviceCapability = 0x10/**< 设备能力描述符。 */
} XUsbDescriptorType;

/**
 * @brief USB 传输结果。
 * @details 同步传输和异步传输回调都使用该结果。成功时为 Ok；失败时
 *          后端还应通过所属句柄的 XUsbDevice_lastError 或
 *          XUsbController_lastError 提供更具体的错误信息。
 */
typedef enum XUsbTransferResult {
    XUsbTransferResult_Ok = 0,             /**< 传输成功。 */
    XUsbTransferResult_Timeout,             /**< 在指定时间内未完成。 */
    XUsbTransferResult_Stall,               /**< 端点或控制请求被设备拒绝。 */
    XUsbTransferResult_Disconnected,       /**< 设备在传输期间断开。 */
    XUsbTransferResult_Cancelled,           /**< 传输被调用者或后端取消。 */
    XUsbTransferResult_Overflow,            /**< 接收数据超过调用者提供的缓冲区。 */
    XUsbTransferResult_InvalidArgument,     /**< 传输参数或端点地址无效。 */
    XUsbTransferResult_Unsupported,         /**< 当前后端或设备不支持该传输。 */
    XUsbTransferResult_Busy,                /**< 设备、接口或端点正在执行冲突操作。 */
    XUsbTransferResult_PermissionDenied,    /**< 当前进程没有访问设备的权限。 */
    XUsbTransferResult_NoDevice,            /**< 目标设备不存在。 */
    XUsbTransferResult_IoError,             /**< 底层 I/O 失败。 */
    XUsbTransferResult_ControllerError,     /**< USB 控制器发生错误。 */
    XUsbTransferResult_ResourceError,       /**< 内存、队列或端点资源不足。 */
    XUsbTransferResult_Interrupted,         /**< 等待被系统或后端中断。 */
    XUsbTransferResult_Unknown              /**< 未分类传输错误。 */
} XUsbTransferResult;

/**
 * @brief USB 控制器事件处理结果。
 * @details 与单次传输结果不同，该结果需要区分“本次等待超时”和“处理失败”。
 */
typedef enum XUsbProcessResult {
    XUsbProcessResult_Error = -1,  /**< 事件处理失败。 */
    XUsbProcessResult_Timeout = 0, /**< 在指定时间内没有待处理事件。 */
    XUsbProcessResult_Event = 1    /**< 至少分发了一个热插拔或传输事件。 */
} XUsbProcessResult;

/**
 * @brief USB 控制器和设备的通用错误。
 */
typedef enum XUsbError {
    XUsbError_None = 0,             /**< 没有错误。 */
    XUsbError_InvalidArgument,      /**< 参数为 NULL、越界或组合无效。 */
    XUsbError_NotOpen,              /**< 控制器或设备尚未打开。 */
    XUsbError_AlreadyOpen,          /**< 控制器或设备已经打开。 */
    XUsbError_NoDevice,             /**< 没有匹配的 USB 设备。 */
    XUsbError_Busy,                /**< 资源已被其他对象占用。 */
    XUsbError_PermissionDenied,    /**< 当前进程没有访问权限。 */
    XUsbError_Timeout,             /**< 等待操作超时。 */
    XUsbError_Stall,               /**< 设备返回协议 STALL。 */
    XUsbError_Disconnected,        /**< 设备已经断开。 */
    XUsbError_Unsupported,         /**< 当前后端或设备不支持该操作。 */
    XUsbError_Resource,            /**< 内部资源不足。 */
    XUsbError_Controller,          /**< USB 控制器错误。 */
    XUsbError_Io,                  /**< 底层 I/O 错误。 */
    XUsbError_Interrupted,         /**< 操作被系统或后端中断。 */
    XUsbError_Unknown              /**< 未分类错误。 */
} XUsbError;

/**
 * @brief USB 回调执行上下文。
 */
typedef enum XUsbCallbackContext {
    XUsbCallbackContext_Unknown = 0, /**< 后端无法说明执行上下文。 */
    XUsbCallbackContext_Interrupt,   /**< 中断上下文；禁止阻塞或分配内存。 */
    XUsbCallbackContext_Task,        /**< RTOS 任务或普通线程上下文。 */
    XUsbCallbackContext_Process      /**< XUsbController_processEvents 调用线程。 */
} XUsbCallbackContext;

/**
 * @brief USB 热插拔事件类型。
 */
typedef enum XUsbHotplugEventType {
    XUsbHotplugEventType_Arrived = 0, /**< 设备已枚举并可打开。 */
    XUsbHotplugEventType_Removed,     /**< 设备已移除，旧句柄不可继续使用。 */
    XUsbHotplugEventType_Changed      /**< 设备描述符或配置状态发生变化。 */
} XUsbHotplugEventType;

/**
 * @brief USB 控制器能力位标志。
 * @details 每个平台只报告实际可用能力；能力位可以按位组合。
 */
typedef enum XUsbFeature {
    XUsbFeature_None = 0,             /**< 不具备额外能力。 */
    XUsbFeature_Hotplug = 1u << 0,    /**< 支持热插拔事件。 */
    XUsbFeature_AsyncTransfer = 1u << 1,/**< 支持异步端点传输。 */
    XUsbFeature_Isochronous = 1u << 2,/**< 支持等时传输。 */
    XUsbFeature_HighSpeed = 1u << 3,  /**< 支持 High-Speed。 */
    XUsbFeature_SuperSpeed = 1u << 4, /**< 支持 SuperSpeed。 */
    XUsbFeature_Reset = 1u << 5,      /**< 支持复位已打开的设备。 */
    XUsbFeature_DescriptorCache = 1u << 6,/**< 支持缓存和查询端点描述符。 */
    XUsbFeature_ProcessEvents = 1u << 7/**< 支持调用线程处理事件。 */
} XUsbFeature;

/**
 * @brief USB 控制器能力位集合。
 * @details 由一个或多个 XUsbFeature 值按位组合得到。
 */
typedef uint32_t XUsbFeatures;

/**
 * @brief USB 控制器创建配置。
 * @details controller 是逻辑控制器编号，由后端映射到 Linux 主机控制器、
 *          Windows USB 栈、STM32 USB OTG 控制器或 ESP32 USB Host 控制器。
 */
typedef struct XUsbControllerConfig {
    uint32_t m_controller; /**< 逻辑 USB 控制器编号。 */
    uint32_t m_flags;      /**< XUsbControllerFlag 的按位组合。 */
} XUsbControllerConfig;

/**
 * @brief USB 控制器创建配置标志。
 */
typedef enum XUsbControllerFlag {
    XUsbControllerFlag_None = 0,             /**< 使用后端默认行为。 */
    XUsbControllerFlag_Exclusive = 1u << 0,  /**< 独占控制器或设备访问。 */
    XUsbControllerFlag_EnableHotplug = 1u << 1,/**< 请求启用热插拔通知。 */
    XUsbControllerFlag_EnableAsync = 1u << 2 /**< 请求启用异步传输。 */
} XUsbControllerFlag;

/**
 * @brief USB 控制器配置的安全默认值。
 * @details 默认选择逻辑控制器 0，使用后端默认的独占、热插拔和异步策略。
 */
#define XUSB_CONTROLLER_CONFIG_INIT { 0u, XUsbControllerFlag_None }

/**
 * @brief USB 设备匹配条件。
 * @details vendorId/productId/bcdDevice 为 0 表示不限制；设备类字段为
 *          0xff 表示不限制；index 用于选择多个相同设备中的第 N 个，起始为 0。
 */
typedef struct XUsbDeviceSelector {
    uint16_t m_vendorId;           /**< USB Vendor ID；0 表示匹配任意厂商。 */
    uint16_t m_productId;          /**< USB Product ID；0 表示匹配任意产品。 */
    uint16_t m_bcdDevice;          /**< 设备版本；0 表示匹配任意版本。 */
    uint8_t m_deviceClass;         /**< 设备类；0xff 表示匹配任意类别。 */
    uint8_t m_deviceSubClass;      /**< 设备子类；0xff 表示匹配任意子类。 */
    uint8_t m_deviceProtocol;      /**< 设备协议；0xff 表示匹配任意协议。 */
    uint32_t m_index;              /**< 相同条件下的匹配序号，从 0 开始。 */
    const char* m_serialNumber_utf8;/**< UTF-8 序列号；借用，可为 NULL。 */
} XUsbDeviceSelector;

/**
 * @brief USB 设备匹配条件的安全默认值。
 * @details 默认匹配所有设备，设备类字段使用 0xff 表示通配，选择第一个匹配项。
 */
#define XUSB_DEVICE_SELECTOR_INIT \
    { 0u, 0u, 0u, 0xffu, 0xffu, 0xffu, 0u, (const char*)0 }

/**
 * @brief 已枚举 USB 设备的稳定信息快照。
 * @details 结构体内不包含平台指针。backendId 是后端用于重新打开该设备的
 *          不透明标识，只能通过 XUsbDevice_openInfo 传回同一个控制器，不能
 *          跨控制器、跨进程或跨设备保存使用。
 */
typedef struct XUsbDeviceInfo {
    uint32_t m_controller;       /**< 所属逻辑 USB 控制器编号。 */
    uint32_t m_bus;              /**< USB 总线逻辑编号；未知时为 0。 */
    uint32_t m_address;          /**< 枚举地址；未知时为 0。 */
    uint64_t m_backendId;        /**< 后端打开令牌；仅供同一控制器使用。 */
    uint16_t m_vendorId;         /**< USB Vendor ID。 */
    uint16_t m_productId;        /**< USB Product ID。 */
    uint16_t m_bcdDevice;         /**< 设备版本号，BCD 编码。 */
    uint8_t m_deviceClass;        /**< USB 设备类。 */
    uint8_t m_deviceSubClass;     /**< USB 设备子类。 */
    uint8_t m_deviceProtocol;     /**< USB 设备协议。 */
    uint8_t m_configurationCount; /**< 配置描述符数量。 */
    uint8_t m_activeConfiguration;/**< 当前配置值；未配置时为 0。 */
    XUsbSpeed m_speed;             /**< 当前 USB 连接速度。 */
} XUsbDeviceInfo;

/**
 * @brief USB 端点描述符的通用快照。
 * @details maxPacketSize 和 interval 的含义遵循 USB 规范；调用者不能修改
 *          后端缓存，需通过 XUsbDevice_getEndpointInfo 获取副本。
 */
typedef struct XUsbEndpointInfo {
    XUsbEndpointAddress m_address; /**< 端点地址，包含方向位。 */
    XUsbTransferType m_transferType;/**< 端点传输类型。 */
    uint16_t m_maxPacketSize;      /**< 端点最大包大小，单位字节。 */
    uint8_t m_interval;            /**< 主机轮询间隔，单位按 USB 速度解释。 */
    uint8_t m_interfaceNumber;     /**< 所属接口编号。 */
    uint8_t m_alternateSetting;    /**< 所属接口备用设置编号。 */
    uint32_t m_maxTransferSize;    /**< 后端单次传输上限；0 表示未知。 */
} XUsbEndpointInfo;

/**
 * @brief USB 控制传输 Setup 包字段。
 * @details requestType、request、value、index 和 length 直接对应 USB 标准
 *          Setup 包字段；length 必须与数据缓冲区方向和长度一致。
 */
typedef struct XUsbControlRequest {
    uint8_t m_requestType; /**< bmRequestType。 */
    uint8_t m_request;     /**< bRequest。 */
    uint16_t m_value;      /**< wValue。 */
    uint16_t m_index;      /**< wIndex。 */
    uint16_t m_length;     /**< wLength，单位字节。 */
} XUsbControlRequest;

/**
 * @brief USB 异步传输请求。
 * @details data 必须在回调执行前保持有效且地址稳定；请求提交成功后，调用者
 *          不能释放、重用或修改该缓冲区，直到回调收到最终结果。
 */
typedef struct XUsbTransferRequest {
    XUsbEndpointAddress m_endpoint; /**< 目标端点地址。 */
    XUsbTransferType m_transferType;/**< 传输类型，必须与端点描述符一致。 */
    void* m_data;                   /**< IN 为输出缓冲区，OUT 为输入缓冲区。 */
    size_t m_length;                /**< 缓冲区容量或发送长度，单位字节。 */
    int32_t m_timeoutMs;            /**< 超时时间；0 由后端解释为默认，负数不限制。 */
    uint32_t m_flags;               /**< 后端扩展标志，公共层当前必须为 0。 */
} XUsbTransferRequest;

/**
 * @brief USB 异步传输完成事件。
 * @details event 只在回调期间有效，回调返回后不能保存该指针。
 */
typedef struct XUsbTransferEvent {
    XUsbTransferResult m_result; /**< 最终传输结果。 */
    size_t m_transferred;        /**< 实际传输字节数；失败时可能小于请求长度。 */
    XUsbCallbackContext m_context;/**< 当前回调的执行上下文。 */
} XUsbTransferEvent;

/**
 * @brief USB 热插拔事件快照。
 */
typedef struct XUsbHotplugEvent {
    XUsbHotplugEventType m_type; /**< 事件类型。 */
    XUsbDeviceInfo m_device;     /**< 设备信息副本，不包含借用指针。 */
} XUsbHotplugEvent;

/**
 * @brief USB 枚举回调函数类型。
 * @param controller 执行枚举的控制器；借用，不能释放。
 * @param device 枚举到的设备信息副本；回调返回后该指针失效。
 * @param userData 注册枚举回调时传入的用户数据；后端不释放。
 * @return 无。若需打开设备，应在回调中复制 device 后调用 XUsbDevice_openInfo。
 */
typedef void (*XUsbEnumerateCallback)(XUsbController* controller,
                                      const XUsbDeviceInfo* device,
                                      void* userData);

/**
 * @brief USB 热插拔回调函数类型。
 * @param controller 产生事件的控制器；借用，不能释放。
 * @param event 热插拔事件副本；回调返回后该指针失效。
 * @param userData 注册回调时传入的用户数据；后端不释放。
 * @return 无。Removed 事件发生后，旧的 XUsbDevice 句柄不能继续传输。
 */
typedef void (*XUsbHotplugCallback)(XUsbController* controller,
                                    const XUsbHotplugEvent* event,
                                    void* userData);

/**
 * @brief USB 异步传输完成回调函数类型。
 * @param device 完成传输的设备；借用，回调期间有效。
 * @param transferId 完成的异步传输标识。
 * @param event 完成结果；借用，仅在回调期间有效。
 * @param userData 提交请求时传入的用户数据；后端不释放。
 * @return 无。回调中不要释放 device，也不要再次提交同一请求的缓冲区。
 */
typedef void (*XUsbTransferCallback)(XUsbDevice* device,
                                     XUsbTransferId transferId,
                                     const XUsbTransferEvent* event,
                                     void* userData);

/** @brief USB 控制器原生句柄类型；返回值始终是借用值。 */
typedef intptr_t XUsbNativeHandle;

/** @brief 无效 USB 原生句柄。 */
#define XUSB_INVALID_NATIVE_HANDLE ((XUsbNativeHandle)-1)

/* =========================================================================
 * 控制器生命周期、枚举和事件
 * ========================================================================= */

/**
 * @brief 创建尚未打开的 USB Host 控制器句柄。
 * @param config 控制器配置；借用，函数会复制内容，不能为 NULL。
 * @return 新控制器句柄；失败返回 NULL，成功后必须调用 XUsbController_delete。
 * @note create 只创建软件对象，不保证已经初始化操作系统或 USB 控制器资源。
 */
XUsbController* XUsbController_create(const XUsbControllerConfig* config);

/**
 * @brief 打开 USB Host 控制器。
 * @param controller 控制器句柄；不能为 NULL，且不能已经打开。
 * @return 成功返回 true；控制器不存在、权限不足或初始化失败返回 false。
 */
bool XUsbController_open(XUsbController* controller);

/**
 * @brief 关闭 USB Host 控制器。
 * @param controller 控制器句柄；可为 NULL。
 * @return 无。关闭前后端必须取消或完成所有未完成传输，并关闭关联设备。
 */
void XUsbController_close(XUsbController* controller);

/**
 * @brief 删除 USB Host 控制器句柄。
 * @param controller 控制器句柄；可为 NULL。
 * @return 无。函数会先关闭控制器，再释放后端资源。
 * @warning 删除后不能继续使用 controller，也不能重复删除。
 */
void XUsbController_delete(XUsbController* controller);

/**
 * @brief 判断 USB Host 控制器是否已打开。
 * @param controller 控制器句柄；NULL 返回 false。
 * @return 已打开返回 true，否则返回 false。
 */
bool XUsbController_isOpen(const XUsbController* controller);

/**
 * @brief 枚举当前控制器下的 USB 设备。
 * @param controller 已打开的控制器；不能为 NULL。
 * @param callback 枚举回调；不能为 NULL。
 * @param userData 回调用户数据；后端不取得所有权，可为 NULL。
 * @return 枚举过程完整执行返回 true；控制器未打开或枚举失败返回 false。
 * @note callback 收到的是设备信息快照；回调返回后不能保存 device 指针。
 */
bool XUsbController_enumerate(XUsbController* controller,
                              XUsbEnumerateCallback callback,
                              void* userData);

/**
 * @brief 注册或清除热插拔回调。
 * @param controller 控制器句柄；不能为 NULL。
 * @param callback 回调函数；可为 NULL，NULL 表示清除回调。
 * @param userData 回调用户数据；后端只借用，可为 NULL。
 * @return 成功返回 true；当前平台不支持热插拔返回 false，并设置 Unsupported。
 * @note 清除回调前应先停止事件处理；具体后端可能要求控制器重新打开。
 */
bool XUsbController_setHotplugCallback(XUsbController* controller,
                                        XUsbHotplugCallback callback,
                                        void* userData);

/**
 * @brief 在调用线程中处理 USB 控制器事件和异步传输回调。
 * @param controller 已打开的控制器；不能为 NULL。
 * @param timeoutMs 等待时间，0 表示非阻塞，正数表示最多等待指定毫秒，负数表示一直等待。
 * @return Event 表示分发了至少一个事件；Timeout 表示没有事件；Error 表示处理失败。
 * @note 只有报告 XUsbFeature_ProcessEvents 的后端才保证支持；任务型后端可以
 *       在内部线程直接回调，此时不要求调用本函数。
 */
XUsbProcessResult XUsbController_processEvents(XUsbController* controller,
                                               int32_t timeoutMs);

/**
 * @brief 获取 USB 控制器能力位。
 * @param controller 控制器句柄；NULL 返回 XUsbFeature_None。
 * @return 当前控制器实际支持的能力组合。
 */
XUsbFeatures XUsbController_features(const XUsbController* controller);

/**
 * @brief 获取 USB 控制器原生句柄。
 * @param controller 控制器句柄；NULL 返回 XUSB_INVALID_NATIVE_HANDLE。
 * @return 后端借用的原生控制器句柄；调用者不能关闭或释放。
 */
XUsbNativeHandle XUsbController_handle(const XUsbController* controller);

/**
 * @brief 获取控制器最近一次通用错误。
 * @param controller 控制器句柄；NULL 返回 XUsbError_InvalidArgument。
 * @return 最近一次错误；成功操作后应清除为 XUsbError_None。
 */
XUsbError XUsbController_lastError(const XUsbController* controller);

/**
 * @brief 获取控制器最近一次平台原生错误码。
 * @param controller 控制器句柄；NULL 返回 0。
 * @return 平台原生错误码；无原生错误时返回 0。
 */
int32_t XUsbController_nativeError(const XUsbController* controller);

/**
 * @brief 清除控制器错误状态。
 * @param controller 控制器句柄；可为 NULL。
 * @return 无。
 */
void XUsbController_clearError(XUsbController* controller);

/* =========================================================================
 * 外接设备生命周期和标准描述符
 * ========================================================================= */

/**
 * @brief 按匹配条件打开一个 USB 外接设备。
 * @param controller 已打开的 Host 控制器；不能为 NULL。
 * @param selector 匹配条件；借用，不能为 NULL。
 * @return 新设备句柄；找不到设备或打开失败返回 NULL，成功后必须调用
 *         XUsbDevice_delete 释放。
 * @note 打开设备通常会取得设备或接口的独占访问权；设备类驱动应在使用完成
 *       后及时释放句柄，避免阻塞其他后端。
 */
XUsbDevice* XUsbDevice_open(XUsbController* controller,
                             const XUsbDeviceSelector* selector);

/**
 * @brief 按枚举快照打开指定 USB 外接设备。
 * @param controller 已打开的 Host 控制器；不能为 NULL。
 * @param info 同一控制器最近一次枚举得到的设备信息副本；不能为 NULL。
 * @return 新设备句柄；设备已经移除或令牌失效时返回 NULL。
 */
XUsbDevice* XUsbDevice_openInfo(XUsbController* controller,
                                 const XUsbDeviceInfo* info);

/**
 * @brief 关闭 USB 设备句柄但保留对象。
 * @param device USB 设备句柄；可为 NULL。
 * @return 无。关闭前端点传输必须完成或取消，接口占用必须释放。
 */
void XUsbDevice_close(XUsbDevice* device);

/**
 * @brief 删除 USB 设备句柄。
 * @param device USB 设备句柄；可为 NULL。
 * @return 无。函数会先关闭设备，再释放后端资源。
 * @warning 删除后不能继续使用 device，也不能重复删除。
 */
void XUsbDevice_delete(XUsbDevice* device);

/**
 * @brief 判断 USB 设备句柄是否仍然打开。
 * @param device USB 设备句柄；NULL 返回 false。
 * @return 句柄打开且设备仍连接返回 true，否则返回 false。
 */
bool XUsbDevice_isOpen(const XUsbDevice* device);

/**
 * @brief 获取 USB 设备信息快照。
 * @param device 已打开的设备句柄；不能为 NULL。
 * @param info 调用者提供的输出空间；不能为 NULL。
 * @return 成功返回 true；句柄无效或设备已断开返回 false，info 保持不变。
 */
bool XUsbDevice_getInfo(const XUsbDevice* device, XUsbDeviceInfo* info);

/**
 * @brief 读取 USB 原始描述符。
 * @param device 已打开的设备句柄；不能为 NULL。
 * @param descriptorType 描述符类型。
 * @param descriptorIndex 同一类型中的描述符索引。
 * @param languageId 字符串描述符语言 ID；非字符串描述符传 0。
 * @param data 调用者提供的输出缓冲区；不能为 NULL。
 * @param capacity 缓冲区容量，单位字节。
 * @param transferred 输出实际写入字节数；可为 NULL。
 * @param timeoutMs 超时时间；0 使用后端默认，负数表示不限制。
 * @return 传输结果；成功时 data 保存设备返回的原始描述符。
 */
XUsbTransferResult XUsbDevice_getDescriptor(XUsbDevice* device,
                                            XUsbDescriptorType descriptorType,
                                            uint8_t descriptorIndex,
                                            uint16_t languageId,
                                            void* data,
                                            size_t capacity,
                                            size_t* transferred,
                                            int32_t timeoutMs);

/**
 * @brief 读取 USB 字符串描述符并转换为 UTF-8。
 * @param device 已打开的设备句柄；不能为 NULL。
 * @param descriptorIndex 字符串描述符索引，0 表示语言列表。
 * @param languageId 语言 ID；读取语言列表时传 0。
 * @param utf8 调用者提供的 UTF-8 输出缓冲区；可为 NULL 以查询所需容量。
 * @param capacity utf8 缓冲区容量，包含结尾的 '\0'，单位字节。
 * @param length 输出 UTF-8 字节数，不含结尾 '\0'；可为 NULL。
 * @param timeoutMs 超时时间；0 使用后端默认，负数表示不限制。
 * @return 传输结果；缓冲区不足返回 Overflow，utf8 内容保持不变。
 * @note 后端必须正确处理 USB UTF-16LE 字符串并输出 UTF-8；调用者负责提供
 *       足够的缓冲区，不得释放后端内部临时存储。
 */
XUsbTransferResult XUsbDevice_getStringDescriptor_utf8(XUsbDevice* device,
                                                       uint8_t descriptorIndex,
                                                       uint16_t languageId,
                                                       char* utf8,
                                                       size_t capacity,
                                                       size_t* length,
                                                       int32_t timeoutMs);

/* =========================================================================
 * 配置、接口和端点描述符
 * ========================================================================= */

/**
 * @brief 设置设备当前配置。
 * @param device 已打开的设备句柄；不能为 NULL。
 * @param configurationValue 配置描述符中的 bConfigurationValue；0 表示取消配置。
 * @return 成功返回 true；配置不存在、接口忙或设备拒绝返回 false。
 * @note 切换配置会使已声明的接口和端点状态失效；调用者应重新获取端点信息。
 */
bool XUsbDevice_setConfiguration(XUsbDevice* device, uint8_t configurationValue);

/**
 * @brief 获取设备当前配置值。
 * @param device 已打开的设备句柄；不能为 NULL。
 * @param configurationValue 输出配置值；未配置时写入 0，不能为 NULL。
 * @return 成功返回 true；设备已断开或参数非法返回 false，输出值不变。
 */
bool XUsbDevice_getConfiguration(const XUsbDevice* device,
                                 uint8_t* configurationValue);

/**
 * @brief 声明并独占一个 USB 接口。
 * @param device 已打开的设备句柄；不能为 NULL。
 * @param interfaceNumber 接口编号。
 * @return 成功返回 true；接口不存在、已被占用或内核驱动未释放返回 false。
 */
bool XUsbDevice_claimInterface(XUsbDevice* device, uint8_t interfaceNumber);

/**
 * @brief 释放一个已声明的 USB 接口。
 * @param device 已打开的设备句柄；可为 NULL。
 * @param interfaceNumber 接口编号。
 * @return 成功返回 true；接口未声明或设备已断开返回 false。
 */
bool XUsbDevice_releaseInterface(XUsbDevice* device, uint8_t interfaceNumber);

/**
 * @brief 设置接口的备用设置。
 * @param device 已打开且已声明接口的设备句柄；不能为 NULL。
 * @param interfaceNumber 接口编号。
 * @param alternateSetting 备用设置编号。
 * @return 成功返回 true；接口或备用设置不存在返回 false。
 */
bool XUsbDevice_setAlternateSetting(XUsbDevice* device,
                                    uint8_t interfaceNumber,
                                    uint8_t alternateSetting);

/**
 * @brief 获取指定接口备用设置中的端点数量。
 * @param device 已打开的设备句柄；不能为 NULL。
 * @param interfaceNumber 接口编号。
 * @param alternateSetting 备用设置编号。
 * @return 端点数量；参数无效、设备断开或后端没有描述符缓存时返回 0。
 */
size_t XUsbDevice_endpointCount(const XUsbDevice* device,
                                uint8_t interfaceNumber,
                                uint8_t alternateSetting);

/**
 * @brief 获取指定接口备用设置中的端点描述符副本。
 * @param device 已打开的设备句柄；不能为 NULL。
 * @param interfaceNumber 接口编号。
 * @param alternateSetting 备用设置编号。
 * @param index 端点在该备用设置中的索引，从 0 开始。
 * @param endpoint 调用者提供的输出空间；不能为 NULL。
 * @return 成功返回 true；索引越界或描述符不可用返回 false，endpoint 不变。
 */
bool XUsbDevice_getEndpointInfo(const XUsbDevice* device,
                                uint8_t interfaceNumber,
                                uint8_t alternateSetting,
                                size_t index,
                                XUsbEndpointInfo* endpoint);

/**
 * @brief 清除指定端点的 Halt 状态。
 * @param device 已打开的设备句柄；不能为 NULL。
 * @param endpoint 端点地址，不能为控制端点 0。
 * @return 成功返回 true；设备不支持或端点无效返回 false。
 */
bool XUsbDevice_clearHalt(XUsbDevice* device, XUsbEndpointAddress endpoint);

/**
 * @brief 复位已打开的 USB 设备。
 * @param device 已打开的设备句柄；不能为 NULL。
 * @return 成功返回 true；后端不支持、设备断开或复位失败返回 false。
 * @note 复位可能导致设备重新枚举，调用者必须重新获取配置、接口和端点状态。
 */
bool XUsbDevice_reset(XUsbDevice* device);

/* =========================================================================
 * 控制传输和同步端点传输
 * ========================================================================= */

/**
 * @brief 执行一次 USB 控制传输。
 * @param device 已打开的设备句柄；不能为 NULL。
 * @param request Setup 包字段；借用，不能为 NULL。
 * @param data 数据缓冲区；IN 为输出缓冲区，OUT 为输入缓冲区；无数据时可为 NULL。
 * @param capacity data 缓冲区容量；应与 request->m_length 一致。
 * @param transferred 输出实际传输字节数；可为 NULL。
 * @param timeoutMs 超时时间；0 使用后端默认，负数表示不限制。
 * @return 传输结果；失败时 data 内容和 transferred 值由后端保持不变。
 */
XUsbTransferResult XUsbDevice_controlTransfer(XUsbDevice* device,
                                              const XUsbControlRequest* request,
                                              void* data,
                                              size_t capacity,
                                              size_t* transferred,
                                              int32_t timeoutMs);

/**
 * @brief 执行一次同步端点传输。
 * @param device 已打开且接口状态有效的设备句柄；不能为 NULL。
 * @param endpoint 端点地址；方向由第 7 位决定，不能使用控制端点 0。
 * @param data 数据缓冲区；IN 为输出缓冲区，OUT 为输入缓冲区；不能为 NULL。
 * @param length IN 为缓冲区容量，OUT 为发送长度，单位字节。
 * @param transferred 输出实际传输字节数；可为 NULL。
 * @param timeoutMs 超时时间；0 使用后端默认，负数表示不限制。
 * @return 传输结果；成功时 transferred 表示实际传输字节数。
 */
XUsbTransferResult XUsbDevice_transfer(XUsbDevice* device,
                                       XUsbEndpointAddress endpoint,
                                       void* data,
                                       size_t length,
                                       size_t* transferred,
                                       int32_t timeoutMs);

/**
 * @brief 提交一次异步端点传输。
 * @param device 已打开的设备句柄；不能为 NULL。
 * @param request 异步请求；借用，提交时复制请求字段，但不复制 data。
 * @param callback 完成回调；不能为 NULL。
 * @param userData 回调用户数据；后端只借用，可为 NULL。
 * @return 非 0 异步传输标识；提交失败返回 XUSB_INVALID_TRANSFER_ID。
 * @note data 必须保持有效直到 callback 收到最终结果；不能在提交返回后修改
 *       request 指针要求后端仍能看到的内容。
 */
XUsbTransferId XUsbDevice_submitTransfer(XUsbDevice* device,
                                          const XUsbTransferRequest* request,
                                          XUsbTransferCallback callback,
                                          void* userData);

/**
 * @brief 取消一个未完成的异步传输。
 * @param device 所属设备句柄；不能为 NULL。
 * @param transferId XUsbDevice_submitTransfer 返回的传输标识。
 * @return 取消请求成功返回 Ok；传输已完成、标识无效或后端不支持返回对应错误。
 * @note 取消成功后仍可能异步收到一次 Cancelled 回调；调用者必须等待最终回调
 *       后才能释放传输缓冲区。
 */
XUsbTransferResult XUsbDevice_cancelTransfer(XUsbDevice* device,
                                             XUsbTransferId transferId);

/* =========================================================================
 * 设备错误、能力和原生句柄
 * ========================================================================= */

/**
 * @brief 获取 USB 设备最近一次通用错误。
 * @param device USB 设备句柄；NULL 返回 XUsbError_InvalidArgument。
 * @return 最近一次错误；成功操作后后端应清除为 XUsbError_None。
 */
XUsbError XUsbDevice_lastError(const XUsbDevice* device);

/**
 * @brief 获取 USB 设备最近一次平台原生错误码。
 * @param device USB 设备句柄；NULL 返回 0。
 * @return 平台原生错误码；无原生错误时返回 0。
 */
int32_t XUsbDevice_nativeError(const XUsbDevice* device);

/**
 * @brief 清除 USB 设备错误状态。
 * @param device USB 设备句柄；可为 NULL。
 * @return 无。
 */
void XUsbDevice_clearError(XUsbDevice* device);

/**
 * @brief 获取 USB 设备后端原生句柄。
 * @param device USB 设备句柄；NULL 返回 XUSB_INVALID_NATIVE_HANDLE。
 * @return 后端借用的设备句柄；调用者不能关闭、释放或修改。
 */
XUsbNativeHandle XUsbDevice_handle(const XUsbDevice* device);

/**
 * @brief 获取 USB 通用错误的 UTF-8 描述。
 * @param error 通用错误值。
 * @return 静态 UTF-8 字符串；调用者不能释放，未知值返回 "Unknown"。
 */
const char* XUsb_errorString(XUsbError error);

#ifdef __cplusplus
}
#endif

#endif /* XUSB_H */
