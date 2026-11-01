/**
 * @file       XGpio.h
 * @brief      GPIO 平台抽象接口。
 * @details    本接口没有 Qt 对齐对象。本文件定义 STM32、ESP32 和 Linux
 *             GPIO 后端共同遵守的纯函数式接口。公共层只使用逻辑控制器编号、
 *             逻辑引脚编号、
 *             GPIO 配置和不透明句柄，不包含 GPIO_TypeDef、ESP-IDF 或
 *             Linux GPIO 字符设备等平台头文件。
 *
 *             后端应在 Drive/STM32、Drive/ESP32、Drive/Posix 等目录中实现
 *             同名函数。应用层只依赖本文件即可完成 GPIO 的打开、配置、
 *             读写、翻转和中断处理。GPIO 句柄是硬件资源句柄，不支持拷贝、
 *             移动或跨线程并发操作，除非具体后端另有明确说明。
 */
#ifndef XGPIO_H
#define XGPIO_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief GPIO 不透明句柄。
 * @details 句柄的具体结构由平台后端拥有和定义，调用者不能访问其成员，
 *          也不能使用 memcpy、直接赋值或自行释放句柄。
 */
typedef struct XGpio XGpio;

/**
 * @brief GPIO 的物理电平。
 */
typedef enum XGpioLevel {
    XGpioLevel_Low = 0,  /**< 低电平，逻辑值为 0。 */
    XGpioLevel_High = 1  /**< 高电平，逻辑值为 1。 */
} XGpioLevel;

/**
 * @brief GPIO 输入输出方向。
 */
typedef enum XGpioDirection {
    XGpioDirection_Input = 0,  /**< 输入模式，后端不得主动驱动引脚。 */
    XGpioDirection_Output = 1  /**< 输出模式，后端允许驱动引脚电平。 */
} XGpioDirection;

/**
 * @brief GPIO 内部上下拉配置。
 * @details Linux 等平台可能不具备运行时配置内部上下拉的能力；后端不支持
 *          请求的模式时，必须返回 XGpioError_Unsupported，不得静默改成另一种
 *          上下拉模式。
 */
typedef enum XGpioPull {
    XGpioPull_None = 0,  /**< 不启用内部上拉或下拉。 */
    XGpioPull_Up,        /**< 启用内部上拉。 */
    XGpioPull_Down       /**< 启用内部下拉。 */
} XGpioPull;

/**
 * @brief GPIO 输出驱动类型。
 */
typedef enum XGpioOutputType {
    XGpioOutputType_PushPull = 0, /**< 推挽输出，高低电平都由 GPIO 驱动。 */
    XGpioOutputType_OpenDrain     /**< 开漏输出，高电平依赖外部或内部上拉。 */
} XGpioOutputType;

/**
 * @brief GPIO 打开或重新配置时使用的初始输出电平。
 */
typedef enum XGpioInitialLevel {
    XGpioInitialLevel_Keep = 0, /**< 保持后端当前电平，不主动修改输出值。 */
    XGpioInitialLevel_Low,      /**< 配置完成后输出低电平。 */
    XGpioInitialLevel_High       /**< 配置完成后输出高电平。 */
} XGpioInitialLevel;

/**
 * @brief GPIO 的有效电平定义。
 * @details 该值只影响 writeActive/readActive 的逻辑转换，不改变
 *          write/read 对物理电平的语义。
 */
typedef enum XGpioActiveLevel {
    XGpioActiveLevel_Low = 0,  /**< 低电平表示有效，常用于低有效设备。 */
    XGpioActiveLevel_High      /**< 高电平表示有效，常用于高有效设备。 */
} XGpioActiveLevel;

/**
 * @brief GPIO 中断触发边沿。
 */
typedef enum XGpioInterruptEdge {
    XGpioInterruptEdge_Disabled = 0, /**< 不产生 GPIO 边沿事件。 */
    XGpioInterruptEdge_Rising,       /**< 低电平变为高电平时触发。 */
    XGpioInterruptEdge_Falling,      /**< 高电平变为低电平时触发。 */
    XGpioInterruptEdge_Both          /**< 上升沿和下降沿都触发。 */
} XGpioInterruptEdge;

/**
 * @brief GPIO 中断回调的执行上下文。
 * @details STM32 裸机后端可能在 ISR 中回调，ESP32 后端可能在 GPIO 驱动
 *          任务中回调，Linux 后端可能在 XGpio_processEvents() 的调用线程
 *          中回调。调用者必须根据该值和具体后端约束决定是否允许阻塞。
 */
typedef enum XGpioCallbackContext {
    XGpioCallbackContext_Unknown = 0, /**< 后端无法说明回调执行上下文。 */
    XGpioCallbackContext_Interrupt,   /**< 回调运行在中断上下文，禁止阻塞。 */
    XGpioCallbackContext_Task         /**< 回调运行在普通任务或线程上下文。 */
} XGpioCallbackContext;

/**
 * @brief GPIO 后端返回的通用错误。
 * @details 后端可以同时保存原生错误码；调用者通过 XGpio_nativeError 获取
 *          原生值。通用错误用于跨 STM32、ESP32 和 Linux 的统一判断。
 */
typedef enum XGpioError {
    XGpioError_None = 0,             /**< 没有错误。 */
    XGpioError_InvalidArgument,      /**< 参数为 NULL、越界或组合无效。 */
    XGpioError_NotOpen,              /**< 句柄尚未打开硬件资源。 */
    XGpioError_AlreadyOpen,          /**< 句柄已经打开。 */
    XGpioError_NotConfigured,        /**< 当前句柄尚未完成有效 GPIO 配置。 */
    XGpioError_Unsupported,          /**< 当前后端或硬件不支持该能力。 */
    XGpioError_Busy,                 /**< 引脚已被其他对象或系统占用。 */
    XGpioError_PermissionDenied,     /**< 当前进程或任务没有操作权限。 */
    XGpioError_Timeout,              /**< 等待硬件或中断事件超时。 */
    XGpioError_Interrupted,          /**< 等待操作被系统或后端中断。 */
    XGpioError_Hardware,             /**< GPIO 控制器返回硬件错误。 */
    XGpioError_Closed,               /**< 硬件资源已经关闭。 */
    XGpioError_Unknown               /**< 未分类错误。 */
} XGpioError;

/**
 * @brief GPIO 后端能力位标志。
 * @details 各标志可以按位组合。后端必须准确报告能力，不能因为接口存在
 *          就把未实现的能力报告为支持。
 */
typedef enum XGpioFeature {
    XGpioFeature_None = 0,                    /**< 不具备额外能力。 */
    XGpioFeature_Input = 1u << 0,             /**< 支持输入配置。 */
    XGpioFeature_Output = 1u << 1,            /**< 支持输出配置。 */
    XGpioFeature_PullUp = 1u << 2,            /**< 支持内部上拉。 */
    XGpioFeature_PullDown = 1u << 3,          /**< 支持内部下拉。 */
    XGpioFeature_OpenDrain = 1u << 4,         /**< 支持开漏输出。 */
    XGpioFeature_Readback = 1u << 5,          /**< 支持读取当前物理电平。 */
    XGpioFeature_Toggle = 1u << 6,            /**< 支持硬件或后端实现的翻转操作。 */
    XGpioFeature_InterruptRising = 1u << 7,   /**< 支持上升沿中断。 */
    XGpioFeature_InterruptFalling = 1u << 8,  /**< 支持下降沿中断。 */
    XGpioFeature_InterruptBoth = 1u << 9,    /**< 支持双边沿中断。 */
    XGpioFeature_Debounce = 1u << 10,         /**< 支持硬件或后端去抖。 */
    XGpioFeature_ActiveLevel = 1u << 11,     /**< 支持有效电平逻辑转换。 */
    XGpioFeature_ProcessEvents = 1u << 12    /**< 支持通过 processEvents 分发事件。 */
} XGpioFeature;

/** @brief XGpioFeature 按位组合后的 GPIO 后端能力集合。 */
typedef uint32_t XGpioFeatures;

/**
 * @brief GPIO 事件处理结果。
 */
typedef enum XGpioProcessResult {
    XGpioProcessResult_Error = -1,  /**< 处理失败，详细原因见 XGpio_lastError。 */
    XGpioProcessResult_Timeout = 0, /**< 在指定时间内没有收到 GPIO 事件。 */
    XGpioProcessResult_Event = 1    /**< 至少分发了一个 GPIO 事件。 */
} XGpioProcessResult;

/**
 * @brief GPIO 逻辑引脚标识。
 * @details controller 和 line 都是逻辑编号，由具体后端映射到 GPIOA、
 *          ESP GPIO_NUM_x、Linux gpiochip 和 line offset 等原生对象。
 *          逻辑编号的含义必须由后端文档或板级配置固定，不能在公共层
 *          假定不同平台的编号相同。
 */
typedef struct XGpioPin {
    uint32_t m_controller; /**< GPIO 控制器或 GPIO bank 的逻辑编号。 */
    uint32_t m_line;       /**< 控制器内 GPIO line 的逻辑编号。 */
} XGpioPin;

/**
 * @brief GPIO 配置值。
 * @details create 和 configure 会复制整个配置，不会保存调用者对配置对象
 *          的借用指针。initialLevel 只在输出方向有意义；输入方向下必须为
 *          XGpioInitialLevel_Keep，否则后端应返回 XGpioError_InvalidArgument。
 */
typedef struct XGpioConfig {
    XGpioPin m_pin;                    /**< 逻辑控制器和逻辑引脚编号。 */
    XGpioDirection m_direction;        /**< 输入或输出方向。 */
    XGpioPull m_pull;                  /**< 内部上拉或下拉配置。 */
    XGpioOutputType m_outputType;      /**< 输出模式；输入模式下必须为 PushPull。 */
    XGpioInitialLevel m_initialLevel;  /**< 打开或配置时使用的初始输出电平。 */
    XGpioActiveLevel m_activeLevel;    /**< writeActive/readActive 使用的有效电平。 */
    XGpioInterruptEdge m_interruptEdge; /**< 打开后的默认中断边沿。 */
    uint32_t m_debounceUs;             /**< 去抖时间，单位微秒；0 表示禁用。 */
    uint32_t m_flags;                  /**< 保留的配置标志，当前必须为 0。 */
} XGpioConfig;

/**
 * @brief GPIO 配置的安全默认值。
 * @details 默认配置为逻辑控制器 0 的 line 0、输入、无上下拉、推挽模式、
 *          保持当前输出、高有效、关闭中断且不启用去抖。使用该宏初始化后，
 *          调用者仍必须根据实际硬件修改 m_pin 和其他配置字段。
 */
#define XGPIO_CONFIG_INIT                                                   \
    {                                                                       \
        { 0u, 0u }, XGpioDirection_Input, XGpioPull_None,                  \
        XGpioOutputType_PushPull, XGpioInitialLevel_Keep,                  \
        XGpioActiveLevel_High, XGpioInterruptEdge_Disabled, 0u, 0u         \
    }

/**
 * @brief GPIO 中断事件。
 * @details event 只在回调执行期间有效，回调返回后不能继续保存或修改该指针。
 */
typedef struct XGpioInterruptEvent {
    XGpioInterruptEdge m_edge;       /**< 触发本次回调的边沿。 */
    XGpioLevel m_level;              /**< 回调时读取到的物理电平。 */
    XGpioCallbackContext m_context;  /**< 当前回调执行上下文。 */
} XGpioInterruptEvent;

/**
 * @brief GPIO 中断回调函数类型。
 * @param gpio 产生事件的 GPIO 句柄；借用，回调期间有效，不能释放。
 * @param event 当前事件；借用，仅在回调期间有效，不能异步保存。
 * @param userData 注册回调时传入的用户数据；由调用者管理，后端不释放。
 * @return 无。回调不得修改 GPIO 配置；ISR 上下文中不得调用可能阻塞的 API。
 */
typedef void (*XGpioInterruptCallback)(XGpio* gpio,
                                       const XGpioInterruptEvent* event,
                                       void* userData);

/**
 * @brief 平台原生 GPIO 句柄类型。
 * @details Linux 后端通常可返回文件描述符，STM32 后端可返回板级映射后的
 *          原生地址，ESP32 后端可返回驱动对象的整数化句柄。该值始终是借用
 *          值，调用者不能关闭、释放或修改它。
 */
typedef intptr_t XGpioNativeHandle;

/**
 * @brief 无效的平台原生 GPIO 句柄。
 */
#define XGPIO_INVALID_NATIVE_HANDLE ((XGpioNativeHandle)-1)

/* =========================================================================
 * 生命周期和资源管理
 * ========================================================================= */

/**
 * @brief 创建一个尚未打开硬件资源的 GPIO 句柄。
 * @param config 初始配置；借用，函数会复制内容，不能为 NULL。
 * @return 新 GPIO 句柄；失败返回 NULL。成功后必须使用 XGpio_delete 释放。
 * @note create 只创建软件句柄，不保证已经占用硬件引脚；必须显式调用
 *       XGpio_open 才能访问 GPIO。后端也可以在 create 中完成资源准备，
 *       但必须保持 open/close 的可观察语义一致。
 */
XGpio* XGpio_create(const XGpioConfig* config);

/**
 * @brief 打开并配置 GPIO 硬件资源。
 * @param gpio GPIO 句柄；不能为 NULL，且不能已经打开。
 * @return 成功返回 true；参数非法、引脚占用、配置不支持或硬件失败返回 false。
 * @note 失败时句柄保持关闭状态；调用者可通过 XGpio_lastError 和
 *       XGpio_nativeError 获取错误信息。
 */
bool XGpio_open(XGpio* gpio);

/**
 * @brief 关闭 GPIO 硬件资源但保留软件句柄。
 * @param gpio GPIO 句柄；可为 NULL。
 * @return 无。已关闭的句柄重复调用安全；关闭失败时错误可通过
 *          XGpio_lastError 获取。
 * @note 关闭后仍可重新调用 XGpio_open；回调不会在关闭后继续收到新的事件。
 */
void XGpio_close(XGpio* gpio);

/**
 * @brief 删除 GPIO 句柄并释放后端资源。
 * @param gpio GPIO 句柄；可为 NULL。
 * @return 无。函数会先关闭仍处于打开状态的 GPIO，再释放句柄。
 * @warning 删除后不得继续使用 gpio，也不得重复调用 XGpio_delete。
 */
void XGpio_delete(XGpio* gpio);

/**
 * @brief 判断 GPIO 是否已经打开。
 * @param gpio GPIO 句柄；NULL 返回 false。
 * @return 已成功占用并打开硬件资源返回 true，否则返回 false。
 */
bool XGpio_isOpen(const XGpio* gpio);

/* =========================================================================
 * 配置和基本读写
 * ========================================================================= */

/**
 * @brief 获取当前配置的副本。
 * @param gpio GPIO 句柄；不能为 NULL。
 * @param config 调用者提供的输出配置空间；不能为 NULL。
 * @return 成功返回 true；句柄或输出参数非法返回 false，config 保持不变。
 */
bool XGpio_getConfig(const XGpio* gpio, XGpioConfig* config);

/**
 * @brief 修改 GPIO 配置并在已打开时立即应用。
 * @param gpio GPIO 句柄；不能为 NULL。
 * @param config 新配置；借用，函数会复制内容，不能为 NULL。
 * @return 成功返回 true；不支持、参数非法或硬件应用失败返回 false。
 * @note 失败时原配置和硬件状态必须保持不变；后端无法做到原子回滚时，
 *       必须在实现文档中说明并返回 XGpioError_Hardware。
 */
bool XGpio_configure(XGpio* gpio, const XGpioConfig* config);

/**
 * @brief 设置 GPIO 输入输出方向。
 * @param gpio GPIO 句柄；不能为 NULL，且必须已打开。
 * @param direction 新方向。
 * @return 成功返回 true；句柄未打开、方向不支持或硬件失败返回 false。
 */
bool XGpio_setDirection(XGpio* gpio, XGpioDirection direction);

/**
 * @brief 获取 GPIO 当前配置方向。
 * @param gpio GPIO 句柄；NULL 返回 XGpioDirection_Input。
 * @return 当前方向；句柄无效时返回输入方向作为安全默认值。
 */
XGpioDirection XGpio_direction(const XGpio* gpio);

/**
 * @brief 写入 GPIO 物理电平。
 * @param gpio GPIO 句柄；不能为 NULL，且必须已打开为输出。
 * @param level 要写入的物理电平，不受 activeLevel 影响。
 * @return 成功返回 true；当前方向不是输出、句柄未打开或硬件失败返回 false。
 */
bool XGpio_write(XGpio* gpio, XGpioLevel level);

/**
 * @brief 读取 GPIO 物理电平。
 * @param gpio GPIO 句柄；不能为 NULL，且必须已打开。
 * @param level 调用者提供的输出变量；成功时写入物理电平，不能为 NULL。
 * @return 成功返回 true；后端不支持读回或硬件读取失败返回 false，level 不变。
 */
bool XGpio_read(const XGpio* gpio, XGpioLevel* level);

/**
 * @brief 翻转 GPIO 当前物理电平。
 * @param gpio GPIO 句柄；不能为 NULL，且必须已打开为输出。
 * @return 成功返回 true；不支持翻转、句柄未打开或硬件失败返回 false。
 * @note 除非后端明确提供原子翻转能力，否则读后写不是原子操作，不能用于
 *       需要严格并发保护的场景。
 */
bool XGpio_toggle(XGpio* gpio);

/**
 * @brief 按有效电平语义写入 GPIO。
 * @param gpio GPIO 句柄；不能为 NULL，且必须已打开为输出。
 * @param active true 表示写入有效状态，false 表示写入无效状态。
 * @return 成功返回 true；参数非法、句柄未打开或硬件失败返回 false。
 * @note active=true 在 activeLevel 为 High 时写入高电平，在 activeLevel 为
 *       Low 时写入低电平；该函数不是对 XGpio_write 的别名。
 */
bool XGpio_writeActive(XGpio* gpio, bool active);

/**
 * @brief 按有效电平语义读取 GPIO。
 * @param gpio GPIO 句柄；不能为 NULL，且必须已打开。
 * @param active 调用者提供的输出变量；成功时写入有效状态，不能为 NULL。
 * @return 成功返回 true；读取失败时返回 false，active 保持不变。
 */
bool XGpio_readActive(const XGpio* gpio, bool* active);

/* =========================================================================
 * 中断和事件
 * ========================================================================= */

/**
 * @brief 设置 GPIO 中断触发边沿。
 * @param gpio GPIO 句柄；不能为 NULL，且必须已打开。
 * @param edge 中断边沿；Disabled 表示关闭边沿触发配置。
 * @return 成功返回 true；边沿不支持、句柄未打开或硬件失败返回 false。
 */
bool XGpio_setInterruptEdge(XGpio* gpio, XGpioInterruptEdge edge);

/**
 * @brief 获取当前 GPIO 中断边沿配置。
 * @param gpio GPIO 句柄；NULL 返回 XGpioInterruptEdge_Disabled。
 * @return 当前边沿配置；句柄无效时返回 Disabled。
 */
XGpioInterruptEdge XGpio_interruptEdge(const XGpio* gpio);

/**
 * @brief 注册或清除 GPIO 中断回调。
 * @param gpio GPIO 句柄；不能为 NULL。
 * @param callback 回调函数；可为 NULL，NULL 表示清除回调。
 * @param userData 回调用户数据；由调用者管理，后端只借用，不负责释放。
 * @return 成功返回 true；句柄非法或后端不支持回调返回 false。
 * @note 清除回调后，后端必须保证不会再向该回调地址投递新事件；正在执行
 *       的回调由具体后端的同步规则决定，调用者应先禁用中断再清除回调。
 */
bool XGpio_setInterruptCallback(XGpio* gpio,
                                XGpioInterruptCallback callback,
                                void* userData);

/**
 * @brief 使能 GPIO 中断。
 * @param gpio GPIO 句柄；不能为 NULL，且必须已打开并注册有效边沿。
 * @return 成功返回 true；未配置边沿、未注册回调或硬件失败返回 false。
 */
bool XGpio_enableInterrupt(XGpio* gpio);

/**
 * @brief 禁用 GPIO 中断。
 * @param gpio GPIO 句柄；可为 NULL。
 * @return 无。重复禁用安全；后端关闭硬件中断失败时记录错误。
 */
void XGpio_disableInterrupt(XGpio* gpio);

/**
 * @brief 判断 GPIO 中断当前是否已使能。
 * @param gpio GPIO 句柄；NULL 返回 false。
 * @return 中断已使能返回 true，否则返回 false。
 */
bool XGpio_isInterruptEnabled(const XGpio* gpio);

/**
 * @brief 在调用线程中处理待处理的 GPIO 事件。
 * @param gpio GPIO 句柄；不能为 NULL，且必须已打开并使能事件。
 * @param timeoutMs 等待时间，0 表示非阻塞，正数表示最多等待指定毫秒，
 *                  负数表示一直等待直到收到事件或发生错误。
 * @return Event 表示至少分发一个事件；Timeout 表示没有事件；Error 表示失败。
 * @note STM32 ISR 回调和 ESP32 驱动任务回调可能不需要调用该函数；Linux
 *       字符设备后端通常通过该函数等待并分发 line event。后端不支持轮询时
 *       返回 Error，并设置 XGpioError_Unsupported。
 */
XGpioProcessResult XGpio_processEvents(XGpio* gpio, int32_t timeoutMs);

/* =========================================================================
 * 后端能力、句柄和错误
 * ========================================================================= */

/**
 * @brief 获取 GPIO 后端能力位。
 * @param gpio GPIO 句柄；NULL 返回 XGpioFeature_None。
 * @return 当前硬件和后端实际支持的能力组合。
 */
XGpioFeatures XGpio_features(const XGpio* gpio);

/**
 * @brief 判断 GPIO 后端是否支持指定能力。
 * @param gpio GPIO 句柄；NULL 返回 false。
 * @param feature 要检查的单个能力标志。
 * @return 支持该能力返回 true；feature 为 0、句柄无效或不支持返回 false。
 */
bool XGpio_hasFeature(const XGpio* gpio, XGpioFeature feature);

/**
 * @brief 获取平台原生 GPIO 句柄。
 * @param gpio GPIO 句柄；NULL 返回 XGPIO_INVALID_NATIVE_HANDLE。
 * @return 后端借用的原生句柄；调用者不能关闭、释放或修改该值。
 */
XGpioNativeHandle XGpio_handle(const XGpio* gpio);

/**
 * @brief 获取 GPIO 最近一次通用错误。
 * @param gpio GPIO 句柄；NULL 返回 XGpioError_InvalidArgument。
 * @return 最近一次错误；成功操作后后端应清除为 XGpioError_None。
 */
XGpioError XGpio_lastError(const XGpio* gpio);

/**
 * @brief 获取 GPIO 最近一次平台原生错误码。
 * @param gpio GPIO 句柄；NULL 返回 0。
 * @return 平台原生错误码；无原生错误或句柄无效时返回 0，不得释放返回值。
 */
int32_t XGpio_nativeError(const XGpio* gpio);

/**
 * @brief 清除 GPIO 错误状态。
 * @param gpio GPIO 句柄；可为 NULL。
 * @return 无。清除后 XGpio_lastError 返回 XGpioError_None。
 */
void XGpio_clearError(XGpio* gpio);

/**
 * @brief 获取通用错误的 UTF-8 描述。
 * @param error 通用错误值。
 * @return 库或后端静态持有的 UTF-8 字符串；调用者不能释放，未知值返回
 *         "Unknown"。
 */
const char* XGpio_errorString(XGpioError error);

#ifdef __cplusplus
}
#endif

#endif /* XGPIO_H */
