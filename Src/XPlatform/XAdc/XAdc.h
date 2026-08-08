/**
 * @file       XAdc.h
 * @brief      ADC 平台抽象接口。
 * @details    本接口没有 Qt 对齐对象。本文件只描述逻辑 ADC 控制器和通道，
 *             不包含 Linux、Windows、STM32 HAL、ESP-IDF 或其他平台头文件。
 *             平台后端必须在 Drive
 *             目录实现同名函数；上层只能通过本接口创建、打开、配置和读取
 *             ADC，不能直接访问原生寄存器或句柄。
 *
 *             XAdc 是独占硬件资源的不透明句柄，不支持复制、移动或跨线程
 *             并发使用，除非平台后端另有明确说明。XAdc_create 返回的新对象
 *             必须通过 XAdc_delete 释放。
 */
#ifndef XADC_H
#define XADC_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief ADC 不透明句柄。
 * @details 具体结构和硬件资源由平台后端管理；调用者不能访问其成员、复制
 *          句柄内存或直接释放句柄。
 */
typedef struct XAdc XAdc;

/**
 * @brief ADC 逻辑通道标识。
 * @details m_controller 和 m_channel 的映射由板级配置或平台后端定义。
 *          m_name 只在创建或配置调用期间借用，后端如需长期使用必须复制。
 */
typedef struct XAdcChannel {
    uint32_t m_controller; /**< ADC 控制器逻辑编号；有效范围由后端定义。 */
    uint32_t m_channel;    /**< 控制器内模拟通道逻辑编号；有效范围由后端定义。 */
    const char* m_name;    /**< 可选 UTF-8 平台名称；可为 NULL，调用期间借用。 */
} XAdcChannel;

/**
 * @brief ADC 配置值。
 * @details XAdc_create 和 XAdc_configure 会复制整个结构，不保存调用者对
 *          XAdcConfig 的指针；其中 m_channel.m_name 的长期保存规则由
 *          XAdcChannel 说明。
 */
typedef struct XAdcConfig {
    XAdcChannel m_channel;      /**< 逻辑 ADC 控制器和通道标识。 */
    uint8_t m_resolutionBits;   /**< 转换分辨率，单位 bit；0 表示后端默认值，最大 32。 */
    uint32_t m_referenceMv;     /**< 参考电压，单位 mV；0 表示使用后端默认值。 */
    uint32_t m_sampleTimeUs;    /**< 单次采样时间，单位 us；0 表示使用后端默认值。 */
    uint16_t m_oversample;      /**< 过采样次数；0 或 1 表示禁用，大于 1 表示采样次数。 */
    uint32_t m_flags;           /**< 保留配置标志；当前必须为 0，调用者不得修改其含义。 */
} XAdcConfig;

/**
 * @brief ADC 配置的安全默认值。
 * @details 默认选择逻辑控制器 0、通道 0，其余采样参数均由后端选择；调用者
 *          应根据实际硬件修改通道和所需参数。
 */
#define XADC_CONFIG_INIT \
    { { 0u, 0u, NULL }, 0u, 0u, 0u, 0u, 0u }

/**
 * @brief ADC 后端能力位。
 * @details 各枚举值可以按位组合；后端只能报告当前控制器实际支持的能力。
 */
typedef enum XAdcFeature {
    XAdcFeature_None = 0,                   /**< 不具备可报告的 ADC 能力。 */
    XAdcFeature_RawRead = 1u << 0,       /**< 支持读取原始转换值。 */
    XAdcFeature_MillivoltRead = 1u << 1, /**< 支持换算并读取毫伏值。 */
    XAdcFeature_Resolution = 1u << 2,    /**< 支持运行时配置分辨率。 */
    XAdcFeature_Reference = 1u << 3,     /**< 支持运行时配置参考电压。 */
    XAdcFeature_SampleTime = 1u << 4,    /**< 支持运行时配置采样时间。 */
    XAdcFeature_Oversample = 1u << 5     /**< 支持运行时配置过采样。 */
} XAdcFeature;

/** @brief XAdcFeature 按位组合后的 ADC 能力集合。 */
typedef uint32_t XAdcFeatures;

/**
 * @brief ADC 通用错误。
 * @details 平台后端可另外保存原生错误码，并通过 XAdc_nativeError 返回。
 */
typedef enum XAdcError {
    XAdcError_None = 0,         /**< 没有错误。 */
    XAdcError_InvalidArgument,  /**< 参数为 NULL、越界或配置组合无效。 */
    XAdcError_NotOpen,          /**< 句柄尚未打开 ADC 硬件资源。 */
    XAdcError_AlreadyOpen,      /**< 句柄已经打开，不能重复打开。 */
    XAdcError_Unsupported,      /**< 当前后端或硬件不支持请求的能力。 */
    XAdcError_Busy,             /**< ADC 控制器、通道或转换单元正在使用。 */
    XAdcError_Timeout,          /**< 在指定时间内没有完成转换。 */
    XAdcError_PermissionDenied, /**< 当前进程或任务没有访问 ADC 的权限。 */
    XAdcError_Hardware,         /**< ADC 控制器或模拟前端报告硬件错误。 */
    XAdcError_Closed,           /**< 硬件资源已关闭或设备已经断开。 */
    XAdcError_Unknown           /**< 未分类错误。 */
} XAdcError;

/**
 * @brief 创建一个尚未打开硬件资源的 ADC 句柄。
 * @param config 初始配置；调用期间借用，函数复制其内容，不能为 NULL。
 * @return 成功返回新 ADC 句柄，参数无效或内存分配失败返回 NULL；成功返回的
 *         对象必须使用 XAdc_delete 释放。
 */
XAdc* XAdc_create(const XAdcConfig* config);

/**
 * @brief 打开并按当前配置占用 ADC 硬件资源。
 * @param adc ADC 句柄；不能为 NULL，且不能已经打开。
 * @return 成功返回 true；句柄无效、资源忙、能力不支持或硬件失败返回 false。
 * @note 失败时句柄保持关闭状态，详细原因通过 XAdc_lastError 查询。
 */
bool XAdc_open(XAdc* adc);

/**
 * @brief 关闭 ADC 硬件资源但保留软件句柄。
 * @param adc ADC 句柄；可为 NULL。
 * @return 无。NULL 或重复关闭不会执行硬件操作；关闭后可以再次调用 XAdc_open。
 */
void XAdc_close(XAdc* adc);

/**
 * @brief 删除 ADC 句柄并释放后端资源。
 * @param adc 由 XAdc_create 返回的句柄；可为 NULL。
 * @return 无。函数会关闭仍处于打开状态的硬件资源。
 * @warning 删除后不得继续使用 adc，也不得重复释放同一非 NULL 句柄。
 */
void XAdc_delete(XAdc* adc);

/**
 * @brief 判断 ADC 是否已经打开。
 * @param adc ADC 句柄；可为 NULL。
 * @return 已成功占用硬件资源返回 true；句柄为 NULL 或尚未打开返回 false。
 */
bool XAdc_isOpen(const XAdc* adc);

/**
 * @brief 获取 ADC 当前配置的副本。
 * @param adc ADC 句柄；不能为 NULL，函数只读借用该对象。
 * @param config 调用者提供的输出空间；不能为 NULL，成功时写入配置副本。
 * @return 成功返回 true；任一参数无效返回 false，失败时 config 保持不变。
 */
bool XAdc_getConfig(const XAdc* adc, XAdcConfig* config);

/**
 * @brief 修改 ADC 配置，并在已打开时由后端应用到硬件。
 * @param adc ADC 句柄；不能为 NULL。
 * @param config 新配置；调用期间借用，函数成功时复制其内容，不能为 NULL。
 * @return 成功返回 true；配置无效、动态配置不受支持或硬件应用失败返回 false。
 * @note 失败时后端应保持原配置和硬件状态不变；不能回滚时应报告
 *       XAdcError_Hardware。
 */
bool XAdc_configure(XAdc* adc, const XAdcConfig* config);

/**
 * @brief 读取一次原始 ADC 转换值。
 * @param adc 已打开的 ADC 句柄；不能为 NULL。
 * @param value 调用者提供的输出变量；不能为 NULL，成功时写入无符号原始码值。
 * @param timeoutMs 等待时间，单位 ms；0 表示非阻塞，正数表示最长等待时间，
 *                  负数表示按后端约定持续等待。
 * @return 完成一次转换并写入 value 返回 true；参数无效、未打开、超时或硬件
 *         失败返回 false，失败时 value 保持不变。
 */
bool XAdc_readRaw(XAdc* adc, uint32_t* value, int32_t timeoutMs);

/**
 * @brief 读取一次换算后的电压值。
 * @param adc 已打开的 ADC 句柄；不能为 NULL。
 * @param value 调用者提供的输出变量；不能为 NULL，成功时写入电压，单位 mV。
 * @param timeoutMs 等待时间，单位 ms；语义与 XAdc_readRaw 相同。
 * @return 完成采样和换算并写入 value 返回 true；不支持毫伏换算、未打开、
 *         超时或硬件失败返回 false，失败时 value 保持不变。
 */
bool XAdc_readMillivolts(XAdc* adc, uint32_t* value, int32_t timeoutMs);

/**
 * @brief 获取 ADC 后端能力位。
 * @param adc ADC 句柄；可为 NULL，函数只读借用该对象。
 * @return 当前控制器和后端实际支持的 XAdcFeature 组合；adc 为 NULL 时返回
 *         XAdcFeature_None。
 */
XAdcFeatures XAdc_features(const XAdc* adc);

/**
 * @brief 判断 ADC 后端是否支持指定能力。
 * @param adc ADC 句柄；可为 NULL，函数只读借用该对象。
 * @param feature 要检查的单个 XAdcFeature 能力位。
 * @return 后端支持该能力返回 true；句柄无效、feature 为 None 或不支持时返回 false。
 */
bool XAdc_hasFeature(const XAdc* adc, XAdcFeature feature);

/**
 * @brief 获取 ADC 最近一次通用错误。
 * @param adc ADC 句柄；可为 NULL，函数只读借用该对象。
 * @return 最近一次 XAdcError；adc 为 NULL 时返回 XAdcError_InvalidArgument。
 */
XAdcError XAdc_lastError(const XAdc* adc);

/**
 * @brief 获取 ADC 最近一次平台原生错误码。
 * @param adc ADC 句柄；可为 NULL，函数只读借用该对象。
 * @return 平台原生错误码；无原生错误或 adc 为 NULL 时返回 0。
 */
int32_t XAdc_nativeError(const XAdc* adc);

/**
 * @brief 将 ADC 通用错误转换成稳定的 ASCII 描述。
 * @param error 要转换的 XAdcError 枚举值。
 * @return 后端静态持有的零结尾 ASCII 字符串；调用者不能修改或释放，未知值
 *         返回 "unknown"。
 */
const char* XAdc_errorString(XAdcError error);

#ifdef __cplusplus
}
#endif

#endif /* XADC_H */
