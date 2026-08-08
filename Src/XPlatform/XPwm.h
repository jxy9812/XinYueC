/**
 * @file       XPwm.h
 * @brief      PWM 平台抽象接口。
 * @details    本接口没有 Qt 对齐对象，是面向控制台和平台驱动的纯函数式
 *             契约，不依赖任何平台 API。仓库已有的 XPWMDeviceBase 继续保留
 *             作为 XIODevice/虚表兼容
 *             类；XPwm 用于按逻辑控制器和通道管理独立 PWM 资源，使嵌入式
 *             Shell 在 Linux、Windows 和 MCU 后端之间保持一致行为。
 *
 *             XPwm 是独占硬件资源的不透明句柄，不支持复制、移动或跨线程并发
 *             使用，除非平台后端另有说明。XPwm_create 返回的新对象必须通过
 *             XPwm_delete 释放。
 */
#ifndef XPWM_H
#define XPWM_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief PWM 不透明句柄。
 * @details 具体结构和硬件资源由平台后端管理；调用者不能访问其成员、复制
 *          句柄内存或直接释放句柄。
 */
typedef struct XPwm XPwm;

/**
 * @brief PWM 逻辑控制器和通道标识。
 * @details m_name 只在创建或配置调用期间借用，后端如需长期使用必须复制。
 */
typedef struct XPwmChannel {
    uint32_t m_controller; /**< PWM 控制器逻辑编号；有效范围由后端定义。 */
    uint32_t m_channel;    /**< 控制器内输出通道逻辑编号；有效范围由后端定义。 */
    const char* m_name;    /**< 可选 UTF-8 平台名称；可为 NULL，调用期间借用。 */
} XPwmChannel;

/** @brief PWM 有效脉冲极性；各枚举值互斥，不能按位组合。 */
typedef enum XPwmPolarity {
    XPwmPolarity_Normal = 0, /**< 占空比高电平有效。 */
    XPwmPolarity_Inverted    /**< 占空比低电平有效。 */
} XPwmPolarity;

/**
 * @brief PWM 配置值。
 * @details XPwm_create 和 XPwm_configure 会复制整个结构，不保存调用者对
 *          XPwmConfig 的指针；其中 m_channel.m_name 的长期保存规则由
 *          XPwmChannel 说明。
 */
typedef struct XPwmConfig {
    XPwmChannel m_channel;       /**< 逻辑 PWM 控制器和输出通道。 */
    uint32_t m_frequencyHz;      /**< 输出频率，单位 Hz；必须大于 0。 */
    uint16_t m_dutyPermille;     /**< 占空比万分比；范围 0..10000 表示 0%..100%。 */
    XPwmPolarity m_polarity;     /**< 有效脉冲极性。 */
    bool m_initialEnabled;       /**< true 表示打开成功后立即启动输出。 */
    uint32_t m_flags;            /**< 保留配置标志；当前必须为 0。 */
} XPwmConfig;

/**
 * @brief PWM 配置的安全默认值。
 * @details 默认选择逻辑控制器 0、通道 0、1 kHz、0% 占空比、正常极性且
 *          打开后不自动输出；调用者应根据实际硬件修改通道和频率。
 */
#define XPWM_CONFIG_INIT \
    { { 0u, 0u, NULL }, 1000u, 0u, XPwmPolarity_Normal, false, 0u }

/**
 * @brief PWM 后端能力位。
 * @details 各枚举值可以按位组合；后端只能报告当前通道实际支持的能力。
 */
typedef enum XPwmFeature {
    XPwmFeature_None = 0,                    /**< 不具备可报告的 PWM 能力。 */
    XPwmFeature_StartStop = 1u << 0,      /**< 支持启停输出。 */
    XPwmFeature_Frequency = 1u << 1,      /**< 支持运行时调频。 */
    XPwmFeature_Duty = 1u << 2,           /**< 支持运行时修改占空比。 */
    XPwmFeature_Polarity = 1u << 3        /**< 支持极性配置。 */
} XPwmFeature;

/** @brief XPwmFeature 按位组合后的 PWM 能力集合。 */
typedef uint32_t XPwmFeatures;

/**
 * @brief PWM 通用错误。
 * @details 平台后端可另外保存原生错误码，并通过 XPwm_nativeError 返回。
 */
typedef enum XPwmError {
    XPwmError_None = 0,         /**< 没有错误。 */
    XPwmError_InvalidArgument,  /**< 参数为 NULL、越界或配置组合无效。 */
    XPwmError_NotOpen,          /**< 句柄尚未打开 PWM 硬件资源。 */
    XPwmError_AlreadyOpen,      /**< 句柄已经打开，不能重复打开。 */
    XPwmError_NotRunning,       /**< PWM 当前没有运行，不能执行目标操作。 */
    XPwmError_AlreadyRunning,   /**< PWM 已经运行，不能重复启动。 */
    XPwmError_Unsupported,      /**< 当前后端或硬件不支持请求的能力。 */
    XPwmError_Busy,             /**< PWM 控制器、通道或定时器正在使用。 */
    XPwmError_PermissionDenied, /**< 当前进程或任务没有访问 PWM 的权限。 */
    XPwmError_Hardware,         /**< PWM 控制器、定时器或输出级报告硬件错误。 */
    XPwmError_Closed,           /**< 硬件资源已关闭或设备已经断开。 */
    XPwmError_Unknown           /**< 未分类错误。 */
} XPwmError;

/**
 * @brief 创建一个尚未打开硬件资源的 PWM 句柄。
 * @param config 初始配置；调用期间借用，函数复制其内容，不能为 NULL。
 * @return 成功返回新 PWM 句柄，参数无效或内存分配失败返回 NULL；成功返回的
 *         对象必须使用 XPwm_delete 释放。
 */
XPwm* XPwm_create(const XPwmConfig* config);

/**
 * @brief 打开并按当前配置占用 PWM 硬件资源。
 * @param pwm PWM 句柄；不能为 NULL，且不能已经打开。
 * @return 成功返回 true；句柄无效、资源忙、能力不支持或硬件失败返回 false。
 * @note m_initialEnabled 为 true 时，打开成功后输出处于运行状态；失败时句柄
 *       保持关闭状态。
 */
bool XPwm_open(XPwm* pwm);

/**
 * @brief 停止 PWM 输出并释放硬件占用，但保留软件句柄。
 * @param pwm PWM 句柄；可为 NULL。
 * @return 无。NULL 或重复关闭不会执行硬件操作；关闭后可以再次调用 XPwm_open。
 */
void XPwm_close(XPwm* pwm);

/**
 * @brief 删除 PWM 句柄并释放后端资源。
 * @param pwm 由 XPwm_create 返回的句柄；可为 NULL。
 * @return 无。函数会停止并关闭仍在使用的 PWM 硬件资源。
 * @warning 删除后不得继续使用 pwm，也不得重复释放同一非 NULL 句柄。
 */
void XPwm_delete(XPwm* pwm);

/**
 * @brief 判断 PWM 是否已打开。
 * @param pwm PWM 句柄；可为 NULL。
 * @return 已成功占用硬件资源返回 true；句柄为 NULL 或尚未打开返回 false。
 */
bool XPwm_isOpen(const XPwm* pwm);

/**
 * @brief 获取 PWM 当前配置的副本。
 * @param pwm PWM 句柄；不能为 NULL，函数只读借用该对象。
 * @param config 调用者提供的输出空间；不能为 NULL，成功时写入配置副本。
 * @return 成功返回 true；任一参数无效返回 false，失败时 config 保持不变。
 */
bool XPwm_getConfig(const XPwm* pwm, XPwmConfig* config);

/**
 * @brief 修改 PWM 配置，并在已打开时由后端应用到硬件。
 * @param pwm PWM 句柄；不能为 NULL。
 * @param config 新配置；调用期间借用，函数成功时复制其内容，不能为 NULL。
 * @return 成功返回 true；配置无效、动态配置不受支持或硬件应用失败返回 false。
 * @note 运行中修改时后端必须保证输出更新的安全性；失败时应保持原配置和输出
 *       状态不变，不能回滚时应报告 XPwmError_Hardware。
 */
bool XPwm_configure(XPwm* pwm, const XPwmConfig* config);

/**
 * @brief 启动 PWM 输出。
 * @param pwm 已打开且尚未运行的 PWM 句柄；不能为 NULL。
 * @return 成功进入运行状态返回 true；未打开、已经运行、不支持启停或硬件失败
 *         返回 false。
 */
bool XPwm_start(XPwm* pwm);

/**
 * @brief 停止 PWM 输出但保留已打开的硬件资源。
 * @param pwm 已打开的 PWM 句柄；不能为 NULL。
 * @return 成功进入停止状态返回 true；未打开、尚未运行、不支持启停或硬件失败
 *         返回 false。
 */
bool XPwm_stop(XPwm* pwm);

/**
 * @brief 判断 PWM 是否正在输出。
 * @param pwm PWM 句柄；可为 NULL，函数只读借用该对象。
 * @return PWM 已打开且正在运行返回 true；句柄为 NULL、未打开或已停止返回 false。
 */
bool XPwm_isRunning(const XPwm* pwm);

/**
 * @brief 设置 PWM 输出频率。
 * @param pwm PWM 句柄；不能为 NULL，已打开时由后端立即应用。
 * @param frequencyHz 目标输出频率，单位 Hz；必须大于 0，最大值由后端决定。
 * @return 成功更新配置和硬件返回 true；参数越界、不支持动态调频或硬件失败
 *         返回 false，失败时原频率保持不变。
 */
bool XPwm_setFrequency(XPwm* pwm, uint32_t frequencyHz);

/**
 * @brief 设置 PWM 输出占空比。
 * @param pwm PWM 句柄；不能为 NULL，已打开时由后端立即应用。
 * @param dutyPermille 占空比万分比；范围 0..10000 表示 0%..100%。
 * @return 成功更新配置和硬件返回 true；参数越界、不支持动态调节或硬件失败
 *         返回 false，失败时原占空比保持不变。
 */
bool XPwm_setDuty(XPwm* pwm, uint16_t dutyPermille);

/**
 * @brief 获取 PWM 后端能力位。
 * @param pwm PWM 句柄；可为 NULL，函数只读借用该对象。
 * @return 当前通道和后端实际支持的 XPwmFeature 组合；pwm 为 NULL 时返回
 *         XPwmFeature_None。
 */
XPwmFeatures XPwm_features(const XPwm* pwm);

/**
 * @brief 判断 PWM 后端是否支持指定能力。
 * @param pwm PWM 句柄；可为 NULL，函数只读借用该对象。
 * @param feature 要检查的单个 XPwmFeature 能力位。
 * @return 后端支持该能力返回 true；句柄无效、feature 为 None 或不支持时返回 false。
 */
bool XPwm_hasFeature(const XPwm* pwm, XPwmFeature feature);

/**
 * @brief 获取 PWM 最近一次通用错误。
 * @param pwm PWM 句柄；可为 NULL，函数只读借用该对象。
 * @return 最近一次 XPwmError；pwm 为 NULL 时返回 XPwmError_InvalidArgument。
 */
XPwmError XPwm_lastError(const XPwm* pwm);

/**
 * @brief 获取 PWM 最近一次平台原生错误码。
 * @param pwm PWM 句柄；可为 NULL，函数只读借用该对象。
 * @return 平台原生错误码；无原生错误或 pwm 为 NULL 时返回 0。
 */
int32_t XPwm_nativeError(const XPwm* pwm);

/**
 * @brief 将 PWM 通用错误转换成稳定的 ASCII 描述。
 * @param error 要转换的 XPwmError 枚举值。
 * @return 后端静态持有的零结尾 ASCII 字符串；调用者不能修改或释放，未知值
 *         返回 "unknown"。
 */
const char* XPwm_errorString(XPwmError error);

#ifdef __cplusplus
}
#endif

#endif /* XPWM_H */
