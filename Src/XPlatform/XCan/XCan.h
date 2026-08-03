/**
 * @file       XCan.h
 * @brief      CAN 总线平台抽象接口。
 * @details    本文件定义 STM32、ESP32、Windows 和 Linux CAN 后端共同遵守的
 *             纯函数式接口。公共层只依赖逻辑通道、CAN 帧、位时序、过滤器、
 *             总线状态和不透明句柄，不包含 STM32 HAL、ESP-IDF、SocketCAN、
 *             Win32 或第三方 CAN 适配器头文件。
 *
 *             STM32 后端可以映射到 bxCAN 或 FDCAN，ESP32 后端可以映射到
 *             TWAI/CAN 控制器，Linux 后端可以映射到 SocketCAN，Windows 后端
 *             可以映射到供应商 CAN 驱动或 CAN 适配器 SDK。具体硬件和驱动不
 *             支持的能力必须通过 XCan_features 报告，并返回
 *             XCanError_Unsupported，不能静默改变调用者请求。
 *
 *             本接口默认覆盖 Classical CAN 和 CAN FD。具体后端可只实现
 *             Classical CAN；CAN FD、位速率配置、硬件过滤器、事件回调等
 *             能力都必须由后端明确报告。句柄不支持拷贝、移动或直接释放，
 *             应始终使用 XCan_create、XCan_close 和 XCan_delete 管理生命周期。
 */
#ifndef XCAN_H
#define XCAN_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** @brief CAN 控制器不透明句柄；具体结构由平台后端定义。 */
typedef struct XCan XCan;

/**
 * @brief CAN 原生句柄类型。
 * @details Linux 通常返回 SocketCAN 文件描述符，Windows 通常返回供应商
 *          适配器句柄，STM32/ESP32 可以返回控制器或驱动对象的借用地址。
 *          调用者不能关闭、释放或修改该句柄。
 */
typedef intptr_t XCanNativeHandle;

/** @brief 无效的 CAN 原生句柄。 */
#define XCAN_INVALID_NATIVE_HANDLE ((XCanNativeHandle)-1)

/** @brief Classical CAN 最大有效负载长度，单位字节。 */
#define XCAN_CLASSIC_DATA_LENGTH 8u

/** @brief CAN FD 最大有效负载长度，单位字节。 */
#define XCAN_FD_DATA_LENGTH 64u

/** @brief CAN 标识符最大值，适用于 11 位标准帧。 */
#define XCAN_STANDARD_ID_MAX UINT32_C(0x000007ff)

/** @brief CAN 标识符最大值，适用于 29 位扩展帧。 */
#define XCAN_EXTENDED_ID_MAX UINT32_C(0x1fffffff)

/** @brief 接收时间戳不可用时使用的值。 */
#define XCAN_TIMESTAMP_INVALID UINT64_MAX

/** @brief 发送帧中表示由后端自动计算 DLC 的值。 */
#define XCAN_DLC_AUTO UINT8_C(0xff)

/** @brief 添加过滤器后端自动分配过滤器标识时返回的无效值。 */
#define XCAN_INVALID_FILTER_ID UINT32_MAX

/**
 * @brief CAN 帧标识符格式。
 */
typedef enum XCanIdFormat {
    XCanIdFormat_Standard = 0, /**< 11 位标准标识符。 */
    XCanIdFormat_Extended      /**< 29 位扩展标识符。 */
} XCanIdFormat;

/**
 * @brief CAN 帧类型。
 */
typedef enum XCanFrameType {
    XCanFrameType_Data = 0, /**< 数据帧。 */
    XCanFrameType_Remote,   /**< 远程帧，仅 Classical CAN 支持。 */
    XCanFrameType_Error     /**< 错误帧或控制器错误事件映射帧。 */
} XCanFrameType;

/**
 * @brief CAN 帧标志。
 * @details 标志可以按位组合。发送时调用者只能设置当前后端和帧格式支持
 *          的标志；接收时后端将总线上的控制位和本地回显状态写入这些标志。
 */
typedef enum XCanFrameFlag {
    XCanFrameFlag_None = 0,                    /**< 无特殊标志。 */
    XCanFrameFlag_FlexibleDataRate = 1u << 0,  /**< CAN FD 帧。 */
    XCanFrameFlag_BitrateSwitch = 1u << 1,     /**< CAN FD 数据段使用更高位速率。 */
    XCanFrameFlag_ErrorStateIndicator = 1u << 2, /**< CAN FD ESI 状态位。 */
    XCanFrameFlag_LocalEcho = 1u << 3,         /**< 本帧是本地发送回显。 */
    XCanFrameFlag_ErrorPassive = 1u << 4,      /**< 接收时表示控制器处于 Error Passive。 */
    XCanFrameFlag_BusOff = 1u << 5            /**< 接收时表示控制器处于 Bus Off。 */
} XCanFrameFlag;
typedef uint32_t XCanFrameFlags;

/**
 * @brief CAN 控制器工作模式。
 * @details 模式是互斥配置。额外行为使用 XCanConfigFlag 表达。
 */
typedef enum XCanMode {
    XCanMode_Normal = 0, /**< 正常收发模式。 */
    XCanMode_Loopback,   /**< 内部回环模式，不要求总线上有其他节点。 */
    XCanMode_ListenOnly, /**< 只监听总线，不主动发送 ACK 或数据。 */
    XCanMode_Restricted  /**< 受限正常模式，具体语义由后端和硬件定义。 */
} XCanMode;

/**
 * @brief CAN 帧格式配置。
 */
typedef enum XCanFrameFormat {
    XCanFrameFormat_Classical = 0, /**< 只允许 Classical CAN 帧。 */
    XCanFrameFormat_FlexibleDataRate, /**< 允许 CAN FD 帧。 */
    XCanFrameFormat_Auto             /**< 由后端按硬件默认值选择。 */
} XCanFrameFormat;

/**
 * @brief CAN 控制器配置标志。
 * @details 标志可以按位组合。后端不支持的标志必须使打开或配置操作失败。
 */
typedef enum XCanConfigFlag {
    XCanConfigFlag_None = 0,                    /**< 无特殊配置。 */
    XCanConfigFlag_AutoRestart = 1u << 0,       /**< Bus Off 后自动尝试恢复。 */
    XCanConfigFlag_OneShot = 1u << 1,           /**< 发送失败时不自动重试。 */
    XCanConfigFlag_ReceiveOwn = 1u << 2,        /**< 接收本控制器发送的本地回显。 */
    XCanConfigFlag_Timestamp = 1u << 3,         /**< 为接收帧提供硬件或软件时间戳。 */
    XCanConfigFlag_ErrorFrames = 1u << 4,       /**< 接收错误帧或错误事件。 */
    XCanConfigFlag_NonIsoCanFd = 1u << 5,       /**< 使用非 ISO CAN FD 格式。 */
    XCanConfigFlag_DisableAutomaticRetransmission = 1u << 6, /**< 禁用自动重发。 */
    XCanConfigFlag_ListenErrorFrames = 1u << 7 /**< 将总线错误作为事件回调。 */
} XCanConfigFlag;
typedef uint32_t XCanConfigFlags;

/**
 * @brief CAN 后端能力位标志。
 * @details 后端必须报告当前控制器和驱动实际支持的能力，而不是报告接口
 *          中存在的所有函数。能力位可以按位组合。
 */
typedef enum XCanFeature {
    XCanFeature_None = 0,                    /**< 没有可报告的能力。 */
    XCanFeature_ClassicalCan = 1u << 0,      /**< 支持 Classical CAN。 */
    XCanFeature_CanFd = 1u << 1,             /**< 支持 CAN FD。 */
    XCanFeature_BitrateConfiguration = 1u << 2, /**< 支持通过本接口配置位时序。 */
    XCanFeature_SamplePointConfiguration = 1u << 3, /**< 支持配置采样点。 */
    XCanFeature_Loopback = 1u << 4,          /**< 支持内部回环模式。 */
    XCanFeature_ListenOnly = 1u << 5,        /**< 支持只监听模式。 */
    XCanFeature_RestrictedMode = 1u << 6,   /**< 支持受限正常模式。 */
    XCanFeature_OneShot = 1u << 7,           /**< 支持单次发送模式。 */
    XCanFeature_ReceiveOwn = 1u << 8,        /**< 支持接收本地发送回显。 */
    XCanFeature_Filters = 1u << 9,           /**< 支持接收过滤器。 */
    XCanFeature_HardwareFilters = 1u << 10,  /**< 过滤器由硬件执行。 */
    XCanFeature_ErrorFrames = 1u << 11,      /**< 支持接收错误帧或错误事件。 */
    XCanFeature_BusStatus = 1u << 12,        /**< 支持读取总线状态和错误计数。 */
    XCanFeature_AutoRestart = 1u << 13,      /**< 支持 Bus Off 自动恢复。 */
    XCanFeature_Timestamp = 1u << 14,       /**< 支持接收时间戳。 */
    XCanFeature_EventCallback = 1u << 15,   /**< 支持事件回调。 */
    XCanFeature_ProcessEvents = 1u << 16,   /**< 支持由 processEvents 分发事件。 */
    XCanFeature_NativeHandle = 1u << 17,    /**< 支持获取借用的原生句柄。 */
    XCanFeature_MultipleFilters = 1u << 18, /**< 支持同时安装多个过滤器。 */
    XCanFeature_BusRecovery = 1u << 19      /**< 支持显式执行 Bus Off 恢复。 */
} XCanFeature;
typedef uint32_t XCanFeatures;

/**
 * @brief CAN 总线状态。
 */
typedef enum XCanBusState {
    XCanBusState_Unknown = 0, /**< 后端无法确定状态。 */
    XCanBusState_Stopped,     /**< 控制器已打开但尚未启动，或已经停止。 */
    XCanBusState_ErrorActive, /**< 错误主动状态。 */
    XCanBusState_ErrorWarning, /**< 错误警告状态。 */
    XCanBusState_ErrorPassive, /**< 错误被动状态。 */
    XCanBusState_BusOff,      /**< 控制器已经进入 Bus Off。 */
    XCanBusState_Recovering   /**< 后端正在执行 Bus Off 恢复。 */
} XCanBusState;

/**
 * @brief CAN 通用错误。
 * @details 后端可以同时保存原生错误码，调用者通过 XCan_nativeError 获取。
 */
typedef enum XCanError {
    XCanError_None = 0,             /**< 没有错误。 */
    XCanError_InvalidArgument,      /**< 参数为 NULL、越界或组合无效。 */
    XCanError_NotOpen,              /**< 句柄尚未打开控制器资源。 */
    XCanError_AlreadyOpen,          /**< 句柄已经打开。 */
    XCanError_NotStarted,           /**< 控制器尚未启动。 */
    XCanError_AlreadyStarted,       /**< 控制器已经启动。 */
    XCanError_Unsupported,          /**< 当前控制器或后端不支持该能力。 */
    XCanError_Busy,                 /**< 控制器、队列或过滤器资源正在使用。 */
    XCanError_Timeout,              /**< 等待发送、接收或状态变化超时。 */
    XCanError_WouldBlock,           /**< 非阻塞操作当前不能立即完成。 */
    XCanError_BusOff,               /**< 总线已经 Bus Off，不能继续发送。 */
    XCanError_ArbitrationLost,      /**< 发送仲裁丢失。 */
    XCanError_ErrorPassive,         /**< 控制器处于 Error Passive。 */
    XCanError_BusError,             /**< 检测到总线错误。 */
    XCanError_Overflow,             /**< 硬件或软件接收队列溢出。 */
    XCanError_Underrun,             /**< 发送队列或硬件资源不足。 */
    XCanError_PermissionDenied,     /**< 当前进程没有访问 CAN 设备的权限。 */
    XCanError_Interrupted,          /**< 等待操作被系统或后端中断。 */
    XCanError_Hardware,              /**< CAN 控制器或收发器发生硬件错误。 */
    XCanError_Io,                   /**< 底层驱动或设备 I/O 失败。 */
    XCanError_Closed,               /**< 资源已经关闭或设备已经断开。 */
    XCanError_Unknown               /**< 未分类错误。 */
} XCanError;

/**
 * @brief CAN 同步收发结果。
 * @details Success 表示完整发送或成功取出一帧。Timeout 表示在正数超时时间
 *          内没有完成；WouldBlock 表示 timeoutMs 为 0 且操作不能立即完成。
 *          更具体的失败原因通过 XCan_lastError 获取。
 */
typedef enum XCanIoResult {
    XCanIoResult_Error = -1,  /**< 操作失败，详细原因见 XCan_lastError。 */
    XCanIoResult_Timeout = 0, /**< 等待超时，未完成本次操作。 */
    XCanIoResult_Success = 1, /**< 操作成功。 */
    XCanIoResult_WouldBlock  /**< 非阻塞操作当前不能立即完成。 */
} XCanIoResult;

/**
 * @brief CAN 事件处理结果。
 */
typedef enum XCanProcessResult {
    XCanProcessResult_Error = -1,  /**< 事件处理失败。 */
    XCanProcessResult_Timeout = 0, /**< 在指定时间内没有待处理事件。 */
    XCanProcessResult_Event = 1    /**< 至少分发了一个事件。 */
} XCanProcessResult;

/**
 * @brief CAN 回调执行上下文。
 * @details STM32 裸机后端可能在 ISR 中回调，ESP32 后端可能在驱动任务中
 *          回调，Linux/Windows 后端也可能在 processEvents 调用线程中回调。
 */
typedef enum XCanCallbackContext {
    XCanCallbackContext_Unknown = 0, /**< 后端无法说明执行上下文。 */
    XCanCallbackContext_Interrupt,   /**< 中断上下文，禁止阻塞或分配内存。 */
    XCanCallbackContext_Task,        /**< RTOS 任务或普通线程上下文。 */
    XCanCallbackContext_Process      /**< XCan_processEvents 调用线程。 */
} XCanCallbackContext;

/**
 * @brief CAN 逻辑通道。
 * @details controller 和 channel 由板级配置或平台后端定义。Linux 可以将
 *          name 映射为 SocketCAN 接口名，例如 can0；Windows 可以将 name
 *          映射为供应商适配器通道；STM32/ESP32 通常使用 controller 和
 *          channel 数字。name 只在 XCan_create 调用期间借用，后端如需保存
 *          必须复制内容。
 */
typedef struct XCanChannel {
    uint32_t m_controller; /**< CAN 控制器逻辑编号。 */
    uint32_t m_channel;    /**< 控制器内通道逻辑编号。 */
    const char* m_name;    /**< 可选 UTF-8 平台名称；可为 NULL。 */
} XCanChannel;

/**
 * @brief CAN 单阶段位时序配置。
 * @details bitrate 以 bit/s 为单位。samplePointPermille 为千分比，例如
 *          875 表示 87.5%；填 0 表示由后端自动选择。其余字段为可选的
 *          精确时序约束，填 0 表示不指定。后端不支持精确时序时，可以只
 *          使用 bitrate 和 samplePointPermille。
 */
typedef struct XCanBitTiming {
    uint32_t m_bitrate;              /**< 目标位速率，单位 bit/s，0 表示自动。 */
    uint16_t m_samplePointPermille;  /**< 采样点千分比，0 表示自动。 */
    uint16_t m_syncJumpWidth;        /**< 同步跳转宽度，0 表示自动。 */
    uint16_t m_timeSegment1;         /**< 时间段 1，0 表示自动。 */
    uint16_t m_timeSegment2;         /**< 时间段 2，0 表示自动。 */
    uint16_t m_prescaler;            /**< 时钟预分频值，0 表示自动。 */
} XCanBitTiming;

/**
 * @brief CAN 控制器配置。
 * @details create 会复制结构体内容。m_name 是调用期间借用字符串；队列长度
 *          为 0 时使用后端默认值。nominalTiming 是 Classical CAN 和 CAN FD
 *          仲裁段位时序，dataTiming 只用于 CAN FD 数据段，Classic CAN 下必须
 *          保持全 0。flags 中的能力必须由后端通过 XCan_features 报告。
 */
typedef struct XCanConfig {
    XCanChannel m_channel;          /**< 逻辑控制器、通道和可选平台名称。 */
    XCanFrameFormat m_frameFormat;  /**< Classical CAN、CAN FD 或自动选择。 */
    XCanMode m_mode;                /**< 控制器工作模式。 */
    XCanBitTiming m_nominalTiming;  /**< 仲裁段位时序。 */
    XCanBitTiming m_dataTiming;     /**< CAN FD 数据段位时序。 */
    uint32_t m_rxQueueLength;       /**< 接收队列长度，0 表示后端默认值。 */
    uint32_t m_txQueueLength;       /**< 发送队列长度，0 表示后端默认值。 */
    uint32_t m_autoRestartDelayMs;  /**< 自动恢复延迟；0 使用后端默认值。 */
    XCanConfigFlags m_flags;        /**< XCanConfigFlag 按位组合。 */
    uint32_t m_reserved[2];         /**< 保留字段，当前必须全部为 0。 */
} XCanConfig;

/**
 * @brief CAN 帧。
 * @details m_length 表示实际数据字节数；接收帧的 m_dlc 保存原始 DLC，发送
 *          帧将 m_dlc 设置为 XCAN_DLC_AUTO 时由后端根据 m_length 计算。CAN FD
 *          的 9、10、11、12、13、14、15 DLC 分别对应 12、16、20、24、32、
 *          48、64 字节，后端可以按协议补齐数据。Remote 帧的 m_length 表示
 *          请求长度，m_data 不使用。Error 帧的 m_data 内容由后端定义。
 */
typedef struct XCanFrame {
    uint32_t m_id;                  /**< 11 位或 29 位 CAN 标识符。 */
    XCanIdFormat m_idFormat;        /**< 标识符格式。 */
    XCanFrameType m_type;           /**< 数据帧、远程帧或错误帧。 */
    XCanFrameFlags m_flags;         /**< XCanFrameFlag 按位组合。 */
    uint8_t m_length;               /**< 实际数据长度或 Remote 请求长度。 */
    uint8_t m_dlc;                  /**< 原始 DLC；发送时可使用 XCAN_DLC_AUTO。 */
    uint8_t m_reserved[2];          /**< 保留字段，当前必须全部为 0。 */
    uint64_t m_timestampUs;         /**< 接收时间戳，单位微秒；不可用时为无效值。 */
    uint8_t m_data[XCAN_FD_DATA_LENGTH]; /**< 数据区；未使用部分必须置 0。 */
} XCanFrame;

/**
 * @brief 接收过滤器的帧类型匹配方式。
 */
typedef enum XCanFilterFrameType {
    XCanFilterFrameType_All = 0, /**< 匹配所有允许的帧类型。 */
    XCanFilterFrameType_Data,     /**< 只匹配数据帧。 */
    XCanFilterFrameType_Remote,   /**< 只匹配远程帧。 */
    XCanFilterFrameType_Error     /**< 只匹配错误帧。 */
} XCanFilterFrameType;

/**
 * @brief CAN 过滤器标志。
 */
typedef enum XCanFilterFlag {
    XCanFilterFlag_None = 0,   /**< 无特殊行为。 */
    XCanFilterFlag_Invert = 1u << 0 /**< 对本过滤器匹配结果取反。 */
} XCanFilterFlag;
typedef uint32_t XCanFilterFlags;

/**
 * @brief CAN 接收过滤器。
 * @details 普通掩码匹配规则为：
 *          (frame.id & m_mask) == (m_id & m_mask)。m_mask 为 0 时匹配指定
 *          标识符格式下的全部帧。m_idFormat、m_frameType 和 m_flags 还会
 *          参与匹配。硬件不支持的复杂过滤器必须返回 Unsupported，不能
 *          静默扩大接收范围。
 */
typedef struct XCanFilter {
    uint32_t m_id;                 /**< 过滤器标识符。 */
    uint32_t m_mask;               /**< 标识符掩码，0 表示该格式全部标识符。 */
    XCanIdFormat m_idFormat;       /**< 标准或扩展标识符格式。 */
    XCanFilterFrameType m_frameType; /**< 数据、远程、错误或全部帧。 */
    XCanFilterFlags m_flags;       /**< XCanFilterFlag 按位组合。 */
} XCanFilter;

/**
 * @brief CAN 总线状态和错误计数快照。
 * @details 计数值是后端或硬件提供的快照，不保证在两次调用之间保持一致。
 *          不支持某个计数的后端应将对应值设置为 UINT32_MAX。
 */
typedef struct XCanStatus {
    XCanBusState m_state;           /**< 当前总线状态。 */
    uint32_t m_receiveErrorCount;   /**< 接收错误计数。 */
    uint32_t m_transmitErrorCount;  /**< 发送错误计数。 */
    uint32_t m_rxPending;           /**< 待处理接收帧数；未知时为 UINT32_MAX。 */
    uint32_t m_txPending;           /**< 待处理发送帧数；未知时为 UINT32_MAX。 */
    uint32_t m_nativeFlags;         /**< 后端原生状态位，公共层不解释。 */
} XCanStatus;

/**
 * @brief CAN 事件类型。
 */
typedef enum XCanEventType {
    XCanEventType_FrameReceived = 0, /**< 收到一帧。 */
    XCanEventType_FrameTransmitted,   /**< 一帧已经完成发送。 */
    XCanEventType_BusStateChanged,    /**< 总线状态发生变化。 */
    XCanEventType_Error               /**< 控制器或总线发生错误。 */
} XCanEventType;

/**
 * @brief CAN 事件快照。
 * @details 事件指针只在回调执行期间有效。FrameReceived 和 FrameTransmitted
 *          使用 m_frame；其他事件只保证 m_status、m_error 和 m_nativeError
 *          有效。未使用字段由后端置零。
 */
typedef struct XCanEvent {
    XCanEventType m_type;            /**< 事件类型。 */
    XCanCallbackContext m_context;   /**< 本次回调的执行上下文。 */
    XCanFrame m_frame;               /**< 收发事件对应的帧。 */
    XCanStatus m_status;             /**< 事件发生时的总线状态快照。 */
    XCanError m_error;               /**< 通用错误；无错误时为 None。 */
    int32_t m_nativeError;           /**< 平台原生错误码；无错误时为 0。 */
} XCanEvent;

/**
 * @brief CAN 事件回调函数类型。
 * @param can 产生事件的 CAN 句柄；借用，回调期间有效，不能释放。
 * @param event 事件快照；借用，仅在回调期间有效，不能异步保存。
 * @param userData 注册回调时传入的用户数据；后端只借用，不负责释放。
 * @return 无。中断上下文中不得调用可能阻塞的 API。
 */
typedef void (*XCanEventCallback)(XCan* can, const XCanEvent* event, void* userData);

/**
 * @brief 返回 CAN FD DLC 对应的实际数据长度。
 * @param dlc 0 到 15 的 CAN FD DLC。
 * @return 对应的实际数据长度；dlc 大于 15 时返回 0。
 */
static inline size_t XCan_lengthFromDlc(uint8_t dlc)
{
    static const uint8_t lengths[] = { 0u, 1u, 2u, 3u, 4u, 5u, 6u, 7u,
                                       8u, 12u, 16u, 20u, 24u, 32u, 48u, 64u };
    return dlc < sizeof(lengths) ? lengths[dlc] : 0u;
}

/**
 * @brief 将实际数据长度转换为可承载该长度的 CAN FD DLC。
 * @param length 实际数据长度，范围为 0 到 64。
 * @return 可承载该长度的 DLC；长度大于 64 时返回 XCAN_DLC_AUTO。
 * @note 9 到 11、13 到 15、17 到 19、21 到 23、25 到 31、33 到 47 和
 *       49 到 63 字节会向上取 CAN FD 允许的下一个数据长度。后端发送时应
 *       将补齐字节置为 0，或按后端约定处理。
 */
static inline uint8_t XCan_dlcFromLength(size_t length)
{
    static const uint8_t lengths[] = { 0u, 1u, 2u, 3u, 4u, 5u, 6u, 7u,
                                       8u, 12u, 16u, 20u, 24u, 32u, 48u, 64u };
    size_t i;
    if (length > XCAN_FD_DATA_LENGTH) return XCAN_DLC_AUTO;
    for (i = 0u; i < sizeof(lengths); ++i) {
        if (length <= lengths[i]) return (uint8_t)i;
    }
    return XCAN_DLC_AUTO;
}

/**
 * @brief CAN 默认控制器配置。
 * @details 默认使用逻辑通道 0、500 kbit/s Classical CAN、正常模式和后端
 *          默认队列。使用该宏初始化后，调用者仍应根据实际硬件修改通道。
 */
#define XCAN_CONFIG_INIT                                                    \
    {                                                                       \
        { 0u, 0u, NULL }, XCanFrameFormat_Classical, XCanMode_Normal,       \
        { 500000u, 0u, 0u, 0u, 0u, 0u },                                    \
        { 0u, 0u, 0u, 0u, 0u, 0u }, 0u, 0u, 0u, XCanConfigFlag_None,         \
        { 0u, 0u }                                                          \
    }

/**
 * @brief CAN 数据帧默认值。
 * @details 默认创建标准 Classical CAN 数据帧，标识符为 0，数据长度为 0，
 *          DLC 由后端自动计算，时间戳标记为不可用。
 */
#define XCAN_FRAME_INIT                                                      \
    {                                                                       \
        0u, XCanIdFormat_Standard, XCanFrameType_Data, XCanFrameFlag_None,  \
        0u, XCAN_DLC_AUTO, { 0u, 0u }, XCAN_TIMESTAMP_INVALID,               \
        { 0u }                                                               \
    }

/**
 * @brief CAN 掩码过滤器默认值。
 * @details 默认匹配指定标识符格式下的全部帧；调用者可以设置 m_id 和
 *          m_mask，将匹配范围收窄到目标标识符。
 */
#define XCAN_FILTER_INIT                                                     \
    { 0u, 0u, XCanIdFormat_Standard, XCanFilterFrameType_All,               \
      XCanFilterFlag_None }

/* =========================================================================
 * 生命周期和控制器状态
 * ========================================================================= */

/**
 * @brief 创建一个尚未打开 CAN 控制器资源的句柄。
 * @param config 初始配置；借用，函数会复制内容，不能为 NULL。
 * @return 新 CAN 句柄；失败返回 NULL，成功后必须使用 XCan_delete 释放。
 * @note create 只创建软件句柄，不保证已经占用控制器或配置硬件；必须显式
 *       调用 XCan_open 和 XCan_start。后端如果在 create 中准备资源，也必须
 *       保持 open/close/start/stop 的可观察语义一致。
 */
XCan* XCan_create(const XCanConfig* config);

/**
 * @brief 打开并配置 CAN 控制器资源。
 * @param can CAN 句柄；不能为 NULL，且不能已经打开。
 * @return 成功返回 true；设备不存在、权限不足、位时序非法或硬件失败返回 false。
 * @note 打开成功后控制器处于 Stopped 状态，调用者应再调用 XCan_start 开始
 *       参与总线通信。Linux/Windows 后端可能要求调用者预先配置系统或供应商
 *       驱动中的位速率，此时未支持运行时位时序配置会返回 Unsupported。
 */
bool XCan_open(XCan* can);

/**
 * @brief 关闭 CAN 控制器资源但保留软件句柄。
 * @param can CAN 句柄；可为 NULL。
 * @return 无。关闭前，后端必须停止控制器并完成或取消未完成收发。
 * @note 关闭后的句柄可以再次调用 XCan_open；关闭不会清除已配置的回调和
 *       软件过滤器，但后端不能在关闭后继续投递事件。
 */
void XCan_close(XCan* can);

/**
 * @brief 删除 CAN 句柄并释放后端资源。
 * @param can CAN 句柄；可为 NULL。
 * @return 无。函数会先关闭仍处于打开状态的控制器，再释放句柄。
 * @warning 删除后不得继续使用 can，也不得重复调用 XCan_delete。
 */
void XCan_delete(XCan* can);

/**
 * @brief 判断 CAN 控制器是否已经打开。
 * @param can CAN 句柄；NULL 返回 false。
 * @return 已成功占用控制器资源返回 true，否则返回 false。
 */
bool XCan_isOpen(const XCan* can);

/**
 * @brief 启动 CAN 控制器并进入总线通信状态。
 * @param can 已打开且尚未启动的 CAN 句柄；不能为 NULL。
 * @return 成功返回 true；未打开、已经启动或硬件启动失败返回 false。
 */
bool XCan_start(XCan* can);

/**
 * @brief 停止 CAN 控制器但保留打开的资源。
 * @param can CAN 句柄；可为 NULL。
 * @return 无。已停止的句柄重复调用安全；失败时错误可通过 XCan_lastError 获取。
 */
void XCan_stop(XCan* can);

/**
 * @brief 判断 CAN 控制器是否已经启动。
 * @param can CAN 句柄；NULL 返回 false。
 * @return 已启动并允许收发返回 true，否则返回 false。
 */
bool XCan_isStarted(const XCan* can);

/**
 * @brief 获取当前配置的副本。
 * @param can CAN 句柄；不能为 NULL。
 * @param config 输出配置空间；不能为 NULL。
 * @return 成功返回 true；参数非法返回 false，config 保持不变。
 */
bool XCan_getConfig(const XCan* can, XCanConfig* config);

/**
 * @brief 修改 CAN 配置并在控制器停止时应用。
 * @param can CAN 句柄；不能为 NULL。
 * @param config 新配置；借用，函数会复制内容，不能为 NULL。
 * @return 成功返回 true；控制器已启动、参数非法、不支持或硬件失败返回 false。
 * @note 失败时原配置和硬件状态应保持不变。后端无法回滚时必须返回
 *       XCanError_Hardware，并在平台实现文档中说明。
 */
bool XCan_configure(XCan* can, const XCanConfig* config);

/* =========================================================================
 * 收发帧
 * ========================================================================= */

/**
 * @brief 同步发送一帧 CAN 数据。
 * @param can 已打开并启动的 CAN 句柄；不能为 NULL。
 * @param frame 待读取并发送的帧；借用，不能为 NULL，调用期间保持有效。
 * @param timeoutMs 等待时间，0 表示非阻塞，正数表示最多等待指定毫秒，负数表示一直等待。
 * @return Success 表示帧已提交并完成后端定义的发送；Timeout 或 WouldBlock 表示
 *         未完成；Error 表示失败，详细原因见 XCan_lastError。
 * @note Classical CAN 的数据长度不能超过 8，CAN FD 的数据长度不能超过 64。
 *       ListenOnly 模式不能发送；发送缓冲区在函数返回后即可复用。
 */
XCanIoResult XCan_send(XCan* can, const XCanFrame* frame, int32_t timeoutMs);

/**
 * @brief 同步接收一帧 CAN 数据。
 * @param can 已打开并启动的 CAN 句柄；不能为 NULL。
 * @param frame 输出帧空间；不能为 NULL，失败时内容保持不变。
 * @param timeoutMs 等待时间，0 表示非阻塞，正数表示最多等待指定毫秒，负数表示一直等待。
 * @return Success 表示已写入一帧；Timeout 或 WouldBlock 表示没有可用帧；Error
 *         表示失败，详细原因见 XCan_lastError。
 * @note 使用事件回调时，后端仍可保留同步接收 API；是否由回调和 receive
 *       共享同一接收队列由平台实现定义，应用应选择一种主要消费方式。
 */
XCanIoResult XCan_receive(XCan* can, XCanFrame* frame, int32_t timeoutMs);

/**
 * @brief 清空待发送帧队列。
 * @param can 已打开的 CAN 句柄；不能为 NULL。
 * @return 成功返回 true；后端不支持队列操作或控制器无效返回 false。
 * @note 已经进入硬件发送流程的帧不一定能够撤销；后端应在文档中说明。
 */
bool XCan_clearTransmitQueue(XCan* can);

/**
 * @brief 清空待接收帧队列。
 * @param can 已打开的 CAN 句柄；不能为 NULL。
 * @return 成功返回 true；后端不支持队列操作或控制器无效返回 false。
 */
bool XCan_clearReceiveQueue(XCan* can);

/* =========================================================================
 * 接收过滤器
 * ========================================================================= */

/**
 * @brief 添加一个接收过滤器。
 * @param can 已打开的 CAN 句柄；不能为 NULL。
 * @param filter 过滤器配置；借用，不能为 NULL。
 * @param filterId 输出过滤器标识；成功时写入后端分配的 ID，可为 NULL。
 * @return 成功返回 true；过滤器不支持、资源不足或参数非法返回 false。
 * @note 过滤器配置会被复制。后端使用软件过滤时，仍必须保持与硬件过滤器
 *       相同的匹配语义。
 */
bool XCan_addFilter(XCan* can, const XCanFilter* filter, uint32_t* filterId);

/**
 * @brief 删除一个已添加的接收过滤器。
 * @param can 已打开的 CAN 句柄；不能为 NULL。
 * @param filterId XCan_addFilter 返回的过滤器标识。
 * @return 成功返回 true；标识无效或后端不支持动态删除返回 false。
 */
bool XCan_removeFilter(XCan* can, uint32_t filterId);

/**
 * @brief 删除当前控制器的全部接收过滤器。
 * @param can 已打开的 CAN 句柄；可为 NULL。
 * @return 无。失败时错误可通过 XCan_lastError 获取。
 */
void XCan_clearFilters(XCan* can);

/* =========================================================================
 * 事件和总线状态
 * ========================================================================= */

/**
 * @brief 注册或清除 CAN 事件回调。
 * @param can CAN 句柄；不能为 NULL。
 * @param callback 回调函数；可为 NULL，NULL 表示清除回调。
 * @param userData 用户数据；后端只借用，可为 NULL。
 * @return 成功返回 true；后端不支持回调返回 false，并设置 Unsupported。
 * @note 清除回调后，后端必须保证不会再向该回调地址投递新事件；正在执行的
 *       回调由具体后端的同步规则决定。回调中不要释放 can 或修改过滤器。
 */
bool XCan_setEventCallback(XCan* can, XCanEventCallback callback, void* userData);

/**
 * @brief 在调用线程中处理待处理的 CAN 事件和回调。
 * @param can 已打开的 CAN 句柄；不能为 NULL。
 * @param timeoutMs 等待时间，0 表示非阻塞，正数表示最多等待指定毫秒，负数表示一直等待。
 * @return Event 表示至少分发一个事件；Timeout 表示没有事件；Error 表示失败。
 * @note 只有报告 XCanFeature_ProcessEvents 的后端才保证支持。STM32 ISR、
 *       ESP32 驱动任务或 Windows 后台线程直接回调时，不要求调用本函数。
 */
XCanProcessResult XCan_processEvents(XCan* can, int32_t timeoutMs);

/**
 * @brief 获取 CAN 总线状态和错误计数快照。
 * @param can 已打开的 CAN 句柄；不能为 NULL。
 * @param status 输出状态空间；不能为 NULL。
 * @return 成功返回 true；后端不支持状态查询或参数非法返回 false，status 不变。
 */
bool XCan_getStatus(const XCan* can, XCanStatus* status);

/**
 * @brief 请求控制器从 Bus Off 状态恢复。
 * @param can 已打开的 CAN 句柄；不能为 NULL。
 * @return 成功提交恢复请求返回 true；不支持、控制器未打开或恢复失败返回 false。
 * @note 对支持硬件自动恢复的后端，该函数可以立即返回 true；调用者应通过
 *       XCan_getStatus 确认最终已经回到 ErrorActive 或 ErrorWarning。
 */
bool XCan_recoverBus(XCan* can);

/* =========================================================================
 * 能力、原生句柄和错误
 * ========================================================================= */

/**
 * @brief 获取 CAN 后端能力位。
 * @param can CAN 句柄；NULL 返回 XCanFeature_None。
 * @return 当前控制器和后端实际支持的能力组合。
 */
XCanFeatures XCan_features(const XCan* can);

/**
 * @brief 判断 CAN 后端是否支持指定能力。
 * @param can CAN 句柄；NULL 返回 false。
 * @param feature 要检查的单个能力标志。
 * @return 支持该能力返回 true；feature 为 0、句柄无效或不支持返回 false。
 */
bool XCan_hasFeature(const XCan* can, XCanFeature feature);

/**
 * @brief 获取 CAN 后端原生句柄。
 * @param can CAN 句柄；NULL 返回 XCAN_INVALID_NATIVE_HANDLE。
 * @return 后端借用的原生句柄；调用者不能关闭、释放或修改该值。
 */
XCanNativeHandle XCan_handle(const XCan* can);

/**
 * @brief 获取 CAN 最近一次通用错误。
 * @param can CAN 句柄；NULL 返回 XCanError_InvalidArgument。
 * @return 最近一次错误；成功操作后后端应清除为 XCanError_None。
 */
XCanError XCan_lastError(const XCan* can);

/**
 * @brief 获取 CAN 最近一次平台原生错误码。
 * @param can CAN 句柄；NULL 返回 0。
 * @return 平台原生错误码；无原生错误或句柄无效时返回 0。
 */
int32_t XCan_nativeError(const XCan* can);

/**
 * @brief 清除 CAN 错误状态。
 * @param can CAN 句柄；可为 NULL。
 * @return 无。清除后 XCan_lastError 返回 XCanError_None。
 */
void XCan_clearError(XCan* can);

/**
 * @brief 获取通用 CAN 错误的 UTF-8 描述。
 * @param error 通用错误值。
 * @return 库或后端静态持有的 UTF-8 字符串；调用者不能释放，未知值返回
 *         "Unknown"。
 */
const char* XCan_errorString(XCanError error);

#ifdef __cplusplus
}
#endif

#endif /* XCAN_H */
