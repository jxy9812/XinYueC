/**
 * @file       XUsbDeviceController.h
 * @brief      USB Device/Gadget 从机平台抽象接口。
 * @details    本文件定义 STM32、ESP32、Linux Gadget 和具备 USB Device
 *             控制器的 Windows 硬件后端共同遵守的纯函数式接口。公共层只
 *             依赖 USB 设备描述符、配置描述符、Setup 请求、端点传输和
 *             不透明句柄，不包含 STM32 HAL、ESP-IDF TinyUSB、Linux ConfigFS
 *             或 Windows 厂商 USB 驱动头文件。
 *
 *             本接口与 XUsb.h 中的 USB Host 接口相互独立。XUsb.h 的
 *             XUsbDevice 表示“主机打开的外部设备”；本文件的
 *             XUsbDeviceController 表示“作为 USB 从机被主机枚举的设备控制器”。
 *             不应将两种句柄混用，也不应把从机描述符和 Host 枚举接口混在一起。
 *
 *             设备模式的具体能力由后端通过 XUsbDeviceController_features 报告。
 *             普通桌面 Windows/Linux 主机硬件通常只能使用 Host 模式；如果目标
 *             硬件或驱动没有 USB Device/Gadget 能力，后端必须返回
 *             XUsbError_Unsupported，不能伪装成已经连接的从机。
 *
 *             配置描述符使用原始字节数组表达，以支持 CDC、HID、MSC、MIDI、
 *             自定义复合设备和厂商类，而不让平台抽象层绑定某一种 USB 类。
 *             描述符数组和字符串内容由调用者持有，必须保持到控制器删除；
 *             后端如果需要异步使用，必须在创建或配置时复制它们。
 */
#ifndef XUSBDEVICECONTROLLER_H
#define XUSBDEVICECONTROLLER_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "XUsb.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief USB Device/Gadget 控制器不透明句柄；具体结构由平台后端定义。 */
typedef struct XUsbDeviceController XUsbDeviceController;

/**
 * @brief USB Device/Gadget 控制器生命周期状态。
 */
typedef enum XUsbDeviceControllerState {
    XUsbDeviceControllerState_Closed = 0, /**< 控制器尚未打开。 */
    XUsbDeviceControllerState_Open,        /**< 控制器已打开但尚未连接到主机。 */
    XUsbDeviceControllerState_Started,     /**< 已启动并等待主机枚举。 */
    XUsbDeviceControllerState_Configured,  /**< 主机已设置有效配置。 */
    XUsbDeviceControllerState_Suspended,   /**< 主机已挂起设备。 */
    XUsbDeviceControllerState_Error        /**< 控制器发生不可恢复错误。 */
} XUsbDeviceControllerState;

/**
 * @brief USB Device/Gadget 控制器配置标志。
 * @details 标志可以按位组合；后端不支持调用者请求的标志时必须返回
 *          XUsbError_Unsupported。
 */
typedef enum XUsbDeviceControllerFlag {
    XUsbDeviceControllerFlag_None = 0,              /**< 无特殊配置。 */
    XUsbDeviceControllerFlag_SelfPowered = 1u << 0, /**< 设备由外部电源供电。 */
    XUsbDeviceControllerFlag_RemoteWakeup = 1u << 1,/**< 允许主机启用远程唤醒。 */
    XUsbDeviceControllerFlag_Composite = 1u << 2,   /**< 设备包含多个 USB 接口。 */
    XUsbDeviceControllerFlag_KeepDescriptors = 1u << 3, /**< 后端保留描述符副本。 */
    XUsbDeviceControllerFlag_AutoConnect = 1u << 4, /**< open 后自动启动连接。 */
    XUsbDeviceControllerFlag_UseEventThread = 1u << 5 /**< 后端使用独立事件线程。 */
} XUsbDeviceControllerFlag;
typedef uint32_t XUsbDeviceControllerFlags;

/**
 * @brief USB Device/Gadget 控制器能力位标志。
 * @details 后端必须报告当前硬件和驱动实际支持的能力。能力位可以按位组合。
 */
typedef enum XUsbDeviceControllerFeature {
    XUsbDeviceControllerFeature_None = 0,             /**< 没有可报告的能力。 */
    XUsbDeviceControllerFeature_DeviceMode = 1u << 0, /**< 支持 USB Device 模式。 */
    XUsbDeviceControllerFeature_FullSpeed = 1u << 1,  /**< 支持 USB Full-Speed。 */
    XUsbDeviceControllerFeature_HighSpeed = 1u << 2,  /**< 支持 USB High-Speed。 */
    XUsbDeviceControllerFeature_SuperSpeed = 1u << 3,/**< 支持 USB SuperSpeed。 */
    XUsbDeviceControllerFeature_Composite = 1u << 4, /**< 支持复合设备。 */
    XUsbDeviceControllerFeature_BosDescriptor = 1u << 5, /**< 支持 BOS 描述符。 */
    XUsbDeviceControllerFeature_MultipleConfigurations = 1u << 6, /**< 支持多配置。 */
    XUsbDeviceControllerFeature_ControlCallback = 1u << 7, /**< 支持类/厂商请求回调。 */
    XUsbDeviceControllerFeature_EndpointTransfer = 1u << 8, /**< 支持端点数据传输。 */
    XUsbDeviceControllerFeature_AsyncTransfer = 1u << 9, /**< 支持异步端点传输。 */
    XUsbDeviceControllerFeature_EndpointStall = 1u << 10, /**< 支持端点 STALL 控制。 */
    XUsbDeviceControllerFeature_RemoteWakeup = 1u << 11, /**< 支持远程唤醒。 */
    XUsbDeviceControllerFeature_SuspendResume = 1u << 12, /**< 支持挂起/恢复事件。 */
    XUsbDeviceControllerFeature_EventCallback = 1u << 13, /**< 支持事件回调。 */
    XUsbDeviceControllerFeature_ProcessEvents = 1u << 14, /**< 支持 processEvents。 */
    XUsbDeviceControllerFeature_NativeHandle = 1u << 15, /**< 支持原生句柄访问。 */
    XUsbDeviceControllerFeature_DynamicDescriptors = 1u << 16 /**< 支持运行时描述符更新。 */
} XUsbDeviceControllerFeature;
typedef uint32_t XUsbDeviceControllerFeatures;

/**
 * @brief USB Device/Gadget 逻辑控制器通道。
 * @details controller 和 channel 由板级配置或平台后端定义。Linux Gadget
 *          可以将 name 映射为 ConfigFS/FunctionFS 实例，STM32/ESP32 通常使用
 *          controller 和 channel 数字。name 只在 create 调用期间借用，后端
 *          如需保存必须复制内容。
 */
typedef struct XUsbDeviceControllerChannel {
    uint32_t m_controller; /**< USB Device 控制器逻辑编号。 */
    uint32_t m_channel;    /**< 控制器内通道逻辑编号。 */
    const char* m_name;    /**< 可选 UTF-8 平台名称；可为 NULL。 */
} XUsbDeviceControllerChannel;

/**
 * @brief USB 标准设备描述符的字段。
 * @details 后端负责将这些字段编码为 USB 小端序设备描述符。字符串字段使用
 *          descriptor index，实际 UTF-8 内容通过 XUsbDeviceStringDescriptor 提供。
 */
typedef struct XUsbDeviceDescriptor {
    uint16_t m_bcdUSB;              /**< bcdUSB，例如 USB 2.0 使用 0x0200。 */
    uint8_t m_deviceClass;          /**< bDeviceClass。 */
    uint8_t m_deviceSubclass;       /**< bDeviceSubClass。 */
    uint8_t m_deviceProtocol;       /**< bDeviceProtocol。 */
    uint8_t m_maxPacketSize0;       /**< EP0 最大包大小，必须符合当前速度。 */
    uint16_t m_vendorId;            /**< idVendor。 */
    uint16_t m_productId;           /**< idProduct。 */
    uint16_t m_bcdDevice;           /**< bcdDevice。 */
    uint8_t m_manufacturerIndex;    /**< iManufacturer，0 表示没有该字符串。 */
    uint8_t m_productIndex;         /**< iProduct，0 表示没有该字符串。 */
    uint8_t m_serialNumberIndex;   /**< iSerialNumber，0 表示没有该字符串。 */
    uint8_t m_numConfigurations;    /**< bNumConfigurations，必须与配置数量一致。 */
} XUsbDeviceDescriptor;

/**
 * @brief USB 配置描述符集合中的一个配置。
 * @details m_descriptor 必须从配置描述符头开始，并包含该配置的完整描述符
 *          集合，包括接口、备用设置和端点描述符。m_descriptorLength 必须
 *          与配置描述符中的 wTotalLength 一致。m_attributes 的 bit7 必须为 1。
 */
typedef struct XUsbDeviceConfigurationDescriptor {
    uint8_t m_value;                /**< bConfigurationValue，不能为 0。 */
    uint8_t m_attributes;           /**< bmAttributes，bit7 必须为 1。 */
    uint16_t m_maxPowerMa;           /**< 最大总线电流，单位毫安。 */
    const uint8_t* m_descriptor;    /**< 完整原始配置描述符；由调用者借用。 */
    size_t m_descriptorLength;      /**< 原始配置描述符字节数。 */
} XUsbDeviceConfigurationDescriptor;

/**
 * @brief USB 字符串描述符的 UTF-8 输入值。
 * @details 后端负责按照 languageId 将 UTF-8 转换为 USB 要求的 UTF-16LE
 *          字符串描述符。languageId 为 0 时使用后端默认语言。
 */
typedef struct XUsbDeviceStringDescriptor {
    uint8_t m_index;                /**< 字符串描述符索引，不能为 0。 */
    uint16_t m_languageId;          /**< USB LANGID；0 表示后端默认语言。 */
    const char* m_utf8;             /**< UTF-8 字符串；由调用者借用，不能为 NULL。 */
} XUsbDeviceStringDescriptor;

/**
 * @brief USB Device/Gadget 控制器配置。
 * @details create 会复制结构体，但默认不深拷贝描述符、字符串数组和名称。
 *          这些外部数据必须保持到 XUsbDeviceController_delete；设置
 *          XUsbDeviceControllerFlag_KeepDescriptors 后，后端必须在 create 或
 *          configure 返回前完成深拷贝，并在后续使用自己的副本。
 */
typedef struct XUsbDeviceControllerConfig {
    XUsbDeviceControllerChannel m_channel; /**< 逻辑控制器、通道和可选名称。 */
    XUsbSpeed m_speed;                     /**< 请求的 USB 速度。 */
    XUsbDeviceDescriptor m_deviceDescriptor; /**< 设备描述符字段。 */
    const XUsbDeviceConfigurationDescriptor* m_configurations; /**< 配置描述符数组。 */
    size_t m_configurationCount;            /**< 配置描述符数组长度。 */
    const uint8_t* m_bosDescriptor;         /**< 可选完整 BOS 描述符。 */
    size_t m_bosDescriptorLength;           /**< BOS 描述符字节数，0 表示没有。 */
    const XUsbDeviceStringDescriptor* m_strings; /**< 字符串描述符输入数组。 */
    size_t m_stringCount;                   /**< 字符串描述符数组长度。 */
    uint32_t m_rxQueueLength;               /**< OUT 端点接收队列长度，0 使用默认值。 */
    uint32_t m_txQueueLength;               /**< IN 端点发送队列长度，0 使用默认值。 */
    XUsbDeviceControllerFlags m_flags;      /**< XUsbDeviceControllerFlag 按位组合。 */
    uint32_t m_reserved[2];                 /**< 保留字段，当前必须全部为 0。 */
} XUsbDeviceControllerConfig;

/**
 * @brief USB Device/Gadget 端点信息。
 * @details endpoint 地址的 bit7 表示方向：IN 是设备发送给主机，OUT 是主机
 *          发送给设备。端点 0 是控制端点，由控制器和 Setup 回调共同处理。
 */
typedef struct XUsbDeviceEndpointInfo {
    XUsbEndpointAddress m_address; /**< 端点地址，包含方向位。 */
    XUsbTransferType m_transferType; /**< 控制、等时、批量或中断传输类型。 */
    uint16_t m_maxPacketSize;      /**< 当前速度下端点最大包大小。 */
    uint8_t m_interval;            /**< bInterval；非周期端点由描述符决定。 */
    uint8_t m_interfaceNumber;     /**< 所属接口编号；未知时为 0xff。 */
    uint8_t m_alternateSetting;    /**< 所属备用设置；未知时为 0xff。 */
    uint8_t m_reserved;            /**< 保留字段，当前必须为 0。 */
    uint32_t m_maxTransferSize;    /**< 单次传输上限；0 表示未知。 */
} XUsbDeviceEndpointInfo;

/**
 * @brief USB Host 发给 Device 的 Setup 请求。
 * @details 字段直接对应 USB 标准 Setup 包。标准设备请求由后端自动处理；
 *          类请求和厂商请求通过 XUsbDeviceControllerSetupCallback 交给上层。
 */
typedef struct XUsbDeviceSetupRequest {
    uint8_t m_requestType; /**< bmRequestType。 */
    uint8_t m_request;     /**< bRequest。 */
    uint16_t m_value;      /**< wValue。 */
    uint16_t m_index;      /**< wIndex。 */
    uint16_t m_length;     /**< wLength，单位字节。 */
} XUsbDeviceSetupRequest;

/** @brief Setup 请求 bit7：1 表示 Device 向 Host 返回数据。 */
#define XUSB_SETUP_REQUEST_IS_IN(requestType) (((requestType) & 0x80u) != 0u)

/** @brief Setup 请求 bmRequestType 的类型掩码。 */
#define XUSB_SETUP_REQUEST_TYPE_MASK UINT8_C(0x60)

/** @brief Setup 请求为标准请求。 */
#define XUSB_SETUP_REQUEST_TYPE_STANDARD UINT8_C(0x00)

/** @brief Setup 请求为类请求。 */
#define XUSB_SETUP_REQUEST_TYPE_CLASS UINT8_C(0x20)

/** @brief Setup 请求为厂商请求。 */
#define XUSB_SETUP_REQUEST_TYPE_VENDOR UINT8_C(0x40)

/**
 * @brief 类请求或厂商请求的处理结果。
 */
typedef enum XUsbDeviceSetupResult {
    XUsbDeviceSetupResult_Handled = 0, /**< 已处理，后端发送正常状态阶段。 */
    XUsbDeviceSetupResult_Stall,        /**< 拒绝请求，后端对控制端点执行 STALL。 */
    XUsbDeviceSetupResult_NotHandled    /**< 未处理，由后端按未支持请求处理。 */
} XUsbDeviceSetupResult;

/**
 * @brief 设备端点传输完成事件。
 * @details event 只在回调执行期间有效，回调返回后不能保存该指针。
 */
typedef struct XUsbDeviceTransferEvent {
    XUsbEndpointAddress m_endpoint; /**< 完成传输的端点地址。 */
    XUsbTransferId m_transferId;     /**< 异步提交时返回的传输标识。 */
    XUsbTransferResult m_result;     /**< 最终传输结果。 */
    size_t m_transferred;            /**< 实际传输字节数。 */
    XUsbCallbackContext m_context;   /**< 当前回调执行上下文。 */
} XUsbDeviceTransferEvent;

/**
 * @brief USB Device/Gadget 事件类型。
 */
typedef enum XUsbDeviceControllerEventType {
    XUsbDeviceControllerEventType_Connected = 0, /**< 已连接或拉起 D+/D-。 */
    XUsbDeviceControllerEventType_Disconnected,   /**< 已断开或撤销连接。 */
    XUsbDeviceControllerEventType_Reset,          /**< 主机执行 USB Reset。 */
    XUsbDeviceControllerEventType_Configured,     /**< 主机设置了有效配置。 */
    XUsbDeviceControllerEventType_Deconfigured,   /**< 主机取消当前配置。 */
    XUsbDeviceControllerEventType_SetInterface,   /**< 主机切换接口备用设置。 */
    XUsbDeviceControllerEventType_Suspend,        /**< 主机挂起设备。 */
    XUsbDeviceControllerEventType_Resume,         /**< 设备从挂起状态恢复。 */
    XUsbDeviceControllerEventType_Setup,          /**< 收到类或厂商 Setup 请求。 */
    XUsbDeviceControllerEventType_Transfer,       /**< 端点传输完成。 */
    XUsbDeviceControllerEventType_Error           /**< 控制器或协议错误。 */
} XUsbDeviceControllerEventType;

/**
 * @brief USB Device/Gadget 事件快照。
 * @details 未使用的字段由后端置零。Setup 事件使用 m_setup，Transfer 事件
 *          使用 m_transfer；其他事件使用 m_state、m_configurationValue 和
 *          m_error。event 只在回调执行期间有效。
 */
typedef struct XUsbDeviceControllerEvent {
    XUsbDeviceControllerEventType m_type; /**< 事件类型。 */
    XUsbCallbackContext m_context;        /**< 当前回调执行上下文。 */
    XUsbDeviceControllerState m_state;    /**< 事件发生后的控制器状态。 */
    XUsbSpeed m_speed;                    /**< 当前协商速度。 */
    uint8_t m_configurationValue;         /**< 当前配置值，未配置时为 0。 */
    uint8_t m_interfaceNumber;            /**< 接口编号；无接口时为 0xff。 */
    uint8_t m_alternateSetting;           /**< 备用设置；无接口时为 0xff。 */
    uint8_t m_reserved;                   /**< 保留字段，当前必须为 0。 */
    XUsbEndpointAddress m_endpoint;       /**< 相关端点；非端点事件为 0。 */
    XUsbDeviceSetupRequest m_setup;       /**< Setup 事件对应的请求。 */
    XUsbDeviceTransferEvent m_transfer;   /**< Transfer 事件对应的结果。 */
    XUsbError m_error;                    /**< 通用错误；无错误时为 None。 */
    int32_t m_nativeError;                /**< 平台原生错误码；无错误时为 0。 */
} XUsbDeviceControllerEvent;

/**
 * @brief 类请求或厂商请求回调函数类型。
 * @param controller 产生请求的 Device 控制器；借用，不能释放。
 * @param request Setup 请求；借用，仅在回调期间有效。
 * @param data 控制传输数据区。IN 请求是待填写的输出缓冲区，OUT 请求是已接收的输入缓冲区；无数据时可为 NULL。
 * @param capacity data 缓冲区容量，单位字节；IN 请求通常等于 wLength。
 * @param length IN 请求由回调写入实际返回长度，OUT 请求由后端写入实际接收长度；不能为 NULL。
 * @param userData 注册回调时传入的用户数据；后端只借用，不负责释放。
 * @return Handled 表示正常完成；Stall 表示拒绝；NotHandled 表示后端按未支持请求处理。
 * @note 标准设备请求由后端自动处理。回调通常运行在协议栈任务或 processEvents
 *       线程中；如果 context 为 Interrupt，则不得阻塞或分配内存。
 */
typedef XUsbDeviceSetupResult (*XUsbDeviceControllerSetupCallback)(
    XUsbDeviceController* controller,
    const XUsbDeviceSetupRequest* request,
    void* data,
    size_t capacity,
    size_t* length,
    void* userData);

/**
 * @brief 异步端点传输完成回调函数类型。
 * @param controller 所属 Device 控制器；借用，不能释放。
 * @param transferId XUsbDeviceController_submitTransfer 返回的标识。
 * @param event 完成事件；借用，仅在回调期间有效。
 * @param userData 提交传输时传入的用户数据；后端只借用。
 * @return 无。收到最终回调前不能释放或重用传输缓冲区。
 */
typedef void (*XUsbDeviceControllerTransferCallback)(
    XUsbDeviceController* controller,
    XUsbTransferId transferId,
    const XUsbDeviceTransferEvent* event,
    void* userData);

/**
 * @brief Device/Gadget 事件回调函数类型。
 * @param controller 产生事件的控制器；借用，不能释放。
 * @param event 事件快照；借用，仅在回调期间有效。
 * @param userData 注册回调时传入的用户数据；后端只借用。
 * @return 无。回调中不要释放 controller 或修改描述符配置。
 */
typedef void (*XUsbDeviceControllerEventCallback)(
    XUsbDeviceController* controller,
    const XUsbDeviceControllerEvent* event,
    void* userData);

/**
 * @brief Device/Gadget 控制器默认配置。
 * @details 默认请求 Full-Speed、USB 2.0、EP0 64 字节和未连接状态。使用该宏
 *          后仍必须填写 VID、PID、至少一个完整配置描述符和必要字符串。
 */
#define XUSB_DEVICE_CONTROLLER_CONFIG_INIT                                  \
    {                                                                       \
        { 0u, 0u, NULL }, XUsbSpeed_Full,                                  \
        { 0x0200u, 0u, 0u, 0u, 64u, 0u, 0u, 0u, 0u, 0u, 0u, 0u },            \
        NULL, 0u, NULL, 0u, NULL, 0u, 0u, 0u,                                \
        XUsbDeviceControllerFlag_None, { 0u, 0u }                           \
    }

/* =========================================================================
 * 生命周期和描述符配置
 * ========================================================================= */

/**
 * @brief 创建尚未打开的 USB Device/Gadget 控制器句柄。
 * @param config 初始配置；借用，不能为 NULL。
 * @return 新控制器句柄；失败返回 NULL，成功后必须使用
 *         XUsbDeviceController_delete 释放。
 * @note create 只创建软件句柄，不保证已经占用 USB 控制器或连接主机。
 */
XUsbDeviceController* XUsbDeviceController_create(
    const XUsbDeviceControllerConfig* config);

/**
 * @brief 打开 USB Device/Gadget 控制器资源。
 * @param controller 控制器句柄；不能为 NULL，且不能已经打开。
 * @return 成功返回 true；设备模式不存在、权限不足、描述符非法或硬件失败返回 false。
 * @note 打开成功后控制器处于 Open 状态；除非配置设置了 AutoConnect，否则应
 *       显式调用 XUsbDeviceController_start 连接并等待主机枚举。
 */
bool XUsbDeviceController_open(XUsbDeviceController* controller);

/**
 * @brief 关闭 Device/Gadget 控制器资源但保留软件句柄。
 * @param controller 控制器句柄；可为 NULL。
 * @return 无。关闭前后端必须停止传输、撤销设备连接并释放平台资源。
 * @note 关闭后的句柄可以再次调用 XUsbDeviceController_open。
 */
void XUsbDeviceController_close(XUsbDeviceController* controller);

/**
 * @brief 删除 Device/Gadget 控制器句柄并释放资源。
 * @param controller 控制器句柄；可为 NULL。
 * @return 无。函数会先关闭仍处于打开状态的控制器。
 * @warning 删除后不能继续使用 controller，也不能重复删除。
 */
void XUsbDeviceController_delete(XUsbDeviceController* controller);

/**
 * @brief 判断 Device/Gadget 控制器是否已打开。
 * @param controller 控制器句柄；NULL 返回 false。
 * @return 已打开返回 true，否则返回 false。
 */
bool XUsbDeviceController_isOpen(const XUsbDeviceController* controller);

/**
 * @brief 启动 USB Device/Gadget 连接。
 * @param controller 已打开且尚未启动的控制器；不能为 NULL。
 * @return 成功返回 true；已经启动、硬件失败或后端不支持返回 false。
 * @note 启动通常会使能 USB 收发器或拉起 D+/D-，之后主机才可以执行枚举。
 */
bool XUsbDeviceController_start(XUsbDeviceController* controller);

/**
 * @brief 停止 USB Device/Gadget 连接但保留控制器资源。
 * @param controller 控制器句柄；可为 NULL。
 * @return 无。重复停止安全；失败时错误可通过 XUsbDeviceController_lastError 获取。
 */
void XUsbDeviceController_stop(XUsbDeviceController* controller);

/**
 * @brief 判断设备是否已启动并连接到 USB 总线。
 * @param controller 控制器句柄；NULL 返回 false。
 * @return 已启动返回 true，否则返回 false。
 */
bool XUsbDeviceController_isStarted(const XUsbDeviceController* controller);

/**
 * @brief 判断主机是否已经完成设备配置。
 * @param controller 控制器句柄；NULL 返回 false。
 * @return 主机发送有效 SET_CONFIGURATION 后返回 true，否则返回 false。
 */
bool XUsbDeviceController_isConfigured(const XUsbDeviceController* controller);

/**
 * @brief 获取当前配置的副本。
 * @param controller 控制器句柄；不能为 NULL。
 * @param config 输出配置空间；不能为 NULL。
 * @return 成功返回 true；参数非法返回 false，config 保持不变。
 * @note 返回结构中的描述符指针仍然是控制器内部或调用者提供的借用指针，
 *       不转移所有权。
 */
bool XUsbDeviceController_getConfig(
    const XUsbDeviceController* controller,
    XUsbDeviceControllerConfig* config);

/**
 * @brief 修改描述符和队列配置。
 * @param controller 控制器句柄；不能为 NULL。
 * @param config 新配置；借用，不能为 NULL。
 * @return 成功返回 true；控制器已启动、描述符非法或后端不支持动态配置返回 false。
 * @note 后端不支持运行时修改时，应要求调用者先 close 再 open，而不是部分应用配置。
 */
bool XUsbDeviceController_configure(
    XUsbDeviceController* controller,
    const XUsbDeviceControllerConfig* config);

/**
 * @brief 获取当前 Device/Gadget 状态快照。
 * @param controller 控制器句柄；不能为 NULL。
 * @param state 输出状态；不能为 NULL。
 * @param speed 输出当前速度；可为 NULL。
 * @param configurationValue 输出当前配置值，未配置时为 0；可为 NULL。
 * @return 成功返回 true；控制器无效或状态查询失败返回 false。
 */
bool XUsbDeviceController_status(
    const XUsbDeviceController* controller,
    XUsbDeviceControllerState* state,
    XUsbSpeed* speed,
    uint8_t* configurationValue);

/* =========================================================================
 * Setup 请求和事件
 * ========================================================================= */

/**
 * @brief 注册或清除类请求/厂商请求回调。
 * @param controller 控制器句柄；不能为 NULL。
 * @param callback 回调函数；可为 NULL，NULL 表示清除回调。
 * @param userData 用户数据；后端只借用，可为 NULL。
 * @return 成功返回 true；后端不支持控制请求回调返回 false。
 * @note 标准 USB 设备请求由后端根据描述符自动处理。回调收到的通常是类
 *       请求或厂商请求；回调必须同步返回处理结果。
 */
bool XUsbDeviceController_setSetupCallback(
    XUsbDeviceController* controller,
    XUsbDeviceControllerSetupCallback callback,
    void* userData);

/**
 * @brief 注册或清除 Device/Gadget 事件回调。
 * @param controller 控制器句柄；不能为 NULL。
 * @param callback 回调函数；可为 NULL，NULL 表示清除回调。
 * @param userData 用户数据；后端只借用，可为 NULL。
 * @return 成功返回 true；后端不支持事件回调返回 false。
 * @note 清除回调后，后端必须保证不会向该地址投递新的事件；正在执行的回调
 *       由具体后端的同步规则决定。
 */
bool XUsbDeviceController_setEventCallback(
    XUsbDeviceController* controller,
    XUsbDeviceControllerEventCallback callback,
    void* userData);

/**
 * @brief 在调用线程中处理 USB Device/Gadget 事件和异步传输回调。
 * @param controller 已打开的控制器；不能为 NULL。
 * @param timeoutMs 等待时间，0 表示非阻塞，正数表示最多等待指定毫秒，负数表示一直等待。
 * @return Event 表示至少分发一个事件；Timeout 表示没有事件；Error 表示失败。
 * @note 只有报告 XUsbDeviceControllerFeature_ProcessEvents 的后端才保证支持。
 *       STM32 ISR、ESP32 USB 任务或 Linux Gadget 独立线程直接回调时，不要求调用本函数。
 */
XUsbProcessResult XUsbDeviceController_processEvents(
    XUsbDeviceController* controller,
    int32_t timeoutMs);

/* =========================================================================
 * 端点和同步传输
 * ========================================================================= */

/**
 * @brief 获取当前配置中的端点描述符信息。
 * @param controller 已打开的控制器；不能为 NULL。
 * @param endpoint 端点地址；端点 0 也可以查询。
 * @param info 输出端点信息；不能为 NULL。
 * @return 成功返回 true；端点不存在、尚未配置或描述符不可解析返回 false。
 */
bool XUsbDeviceController_getEndpointInfo(
    const XUsbDeviceController* controller,
    XUsbEndpointAddress endpoint,
    XUsbDeviceEndpointInfo* info);

/**
 * @brief 执行一次同步设备端点传输。
 * @param controller 已配置的控制器；不能为 NULL。
 * @param endpoint 端点地址；IN 表示设备发送给主机，OUT 表示主机发送给设备；不能为端点 0。
 * @param data 数据缓冲区；IN 为输入数据，OUT 为输出缓冲区；不能为 NULL。
 * @param length IN 为发送长度，OUT 为接收缓冲区容量，单位字节。
 * @param transferred 输出实际传输字节数；可为 NULL。
 * @param timeoutMs 等待时间，0 使用后端默认，正数表示最多等待指定毫秒，负数表示一直等待。
 * @return XUsbTransferResult_Ok 表示成功；失败返回具体传输错误。
 * @note 调用者必须根据端点方向保证 data 的语义正确。同步传输返回后 data
 *       可以复用；某些中断/等时端点只能使用异步接口。
 */
XUsbTransferResult XUsbDeviceController_transfer(
    XUsbDeviceController* controller,
    XUsbEndpointAddress endpoint,
    void* data,
    size_t length,
    size_t* transferred,
    int32_t timeoutMs);

/**
 * @brief 提交一次异步设备端点传输。
 * @param controller 已配置的控制器；不能为 NULL。
 * @param endpoint 端点地址；不能为端点 0。
 * @param data IN 为待发送数据，OUT 为接收缓冲区；提交期间必须保持有效。
 * @param length IN 为发送长度，OUT 为缓冲区容量，单位字节。
 * @param timeoutMs 超时时间；0 使用后端默认，负数表示不限制。
 * @param callback 完成回调；不能为 NULL。
 * @param userData 回调用户数据；后端只借用，可为 NULL。
 * @return 非 XUSB_INVALID_TRANSFER_ID 的传输标识；提交失败返回无效标识。
 * @note 收到最终回调前，调用者不能释放、重用或修改 data。
 */
XUsbTransferId XUsbDeviceController_submitTransfer(
    XUsbDeviceController* controller,
    XUsbEndpointAddress endpoint,
    void* data,
    size_t length,
    int32_t timeoutMs,
    XUsbDeviceControllerTransferCallback callback,
    void* userData);

/**
 * @brief 取消一个未完成的异步设备端点传输。
 * @param controller 所属控制器；不能为 NULL。
 * @param transferId XUsbDeviceController_submitTransfer 返回的标识。
 * @return 取消请求成功返回 XUsbTransferResult_Cancelled；标识无效、已完成或
 *         后端不支持取消返回对应错误。
 * @note 取消成功后仍可能收到一次 Cancelled 回调；必须等待最终回调后才能复用 data。
 */
XUsbTransferResult XUsbDeviceController_cancelTransfer(
    XUsbDeviceController* controller,
    XUsbTransferId transferId);

/**
 * @brief 设置或清除端点 STALL 状态。
 * @param controller 已打开的控制器；不能为 NULL。
 * @param endpoint 端点地址；不能为端点 0。
 * @param stalled true 设置 STALL，false 清除 STALL。
 * @return 成功返回 true；端点不存在或后端不支持返回 false。
 */
bool XUsbDeviceController_setEndpointStalled(
    XUsbDeviceController* controller,
    XUsbEndpointAddress endpoint,
    bool stalled);

/**
 * @brief 清空指定端点的待处理传输队列。
 * @param controller 已打开的控制器；不能为 NULL。
 * @param endpoint 端点地址；不能为端点 0。
 * @return 成功返回 true；硬件不支持或端点无效返回 false。
 * @note 已经进入 USB 控制器硬件的传输不一定能够撤销。
 */
bool XUsbDeviceController_clearEndpointQueue(
    XUsbDeviceController* controller,
    XUsbEndpointAddress endpoint);

/* =========================================================================
 * 总线控制、能力和错误
 * ========================================================================= */

/**
 * @brief 请求设备执行远程唤醒。
 * @param controller 已启动且处于 Suspended 状态的控制器；不能为 NULL。
 * @return 成功发出唤醒信号返回 true；主机未启用远程唤醒、当前未挂起或后端不支持返回 false。
 * @note 调用者必须等待主机恢复事件，不能把返回 true 解释为枚举状态已经恢复。
 */
bool XUsbDeviceController_remoteWakeup(XUsbDeviceController* controller);

/**
 * @brief 获取 Device/Gadget 控制器能力位。
 * @param controller 控制器句柄；NULL 返回 XUsbDeviceControllerFeature_None。
 * @return 当前硬件和后端实际支持的能力组合。
 */
XUsbDeviceControllerFeatures XUsbDeviceController_features(
    const XUsbDeviceController* controller);

/**
 * @brief 判断 Device/Gadget 控制器是否支持指定能力。
 * @param controller 控制器句柄；NULL 返回 false。
 * @param feature 要检查的单个能力标志。
 * @return 支持返回 true；feature 为 0、句柄无效或不支持返回 false。
 */
bool XUsbDeviceController_hasFeature(
    const XUsbDeviceController* controller,
    XUsbDeviceControllerFeature feature);

/**
 * @brief 获取 Device/Gadget 后端原生句柄。
 * @param controller 控制器句柄；NULL 返回 XUSB_INVALID_NATIVE_HANDLE。
 * @return 后端借用的原生句柄；调用者不能关闭、释放或修改。
 */
XUsbNativeHandle XUsbDeviceController_handle(
    const XUsbDeviceController* controller);

/**
 * @brief 获取控制器最近一次通用错误。
 * @param controller 控制器句柄；NULL 返回 XUsbError_InvalidArgument。
 * @return 最近一次错误；成功操作后后端应清除为 XUsbError_None。
 */
XUsbError XUsbDeviceController_lastError(
    const XUsbDeviceController* controller);

/**
 * @brief 获取控制器最近一次平台原生错误码。
 * @param controller 控制器句柄；NULL 返回 0。
 * @return 平台原生错误码；无原生错误或句柄无效时返回 0。
 */
int32_t XUsbDeviceController_nativeError(
    const XUsbDeviceController* controller);

/**
 * @brief 清除控制器错误状态。
 * @param controller 控制器句柄；可为 NULL。
 * @return 无。清除后 XUsbDeviceController_lastError 返回 XUsbError_None。
 */
void XUsbDeviceController_clearError(XUsbDeviceController* controller);

/**
 * @brief 获取 USB 通用错误的 UTF-8 描述。
 * @param error 通用错误值。
 * @return 静态 UTF-8 字符串；调用者不能释放，未知值返回 "Unknown"。
 * @note 该函数与 XUsb_errorString 使用同一组 XUsbError 枚举语义。
 */
const char* XUsbDeviceController_errorString(XUsbError error);

#ifdef __cplusplus
}
#endif

#endif /* XUSBDEVICECONTROLLER_H */
