/**
 * @file       XSpi.h
 * @brief      SPI 平台无关全双工事务接口。
 * @details    本接口没有 Qt 对齐对象。公共层只处理逻辑控制器、片选、时钟
 *             模式、位序和字节事务，不包含任何操作系统或芯片厂商头文件。
 *             后端必须准确报告能力，
 *             对不支持的配置返回 XSpiError_Unsupported。
 *             XSpi 是独占总线目标的不透明句柄，不支持复制、移动或跨线程
 *             并发使用，除非平台后端另有明确说明。
 */
#ifndef XSPI_H
#define XSPI_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief SPI 不透明句柄。
 * @details 具体结构和控制器资源由平台后端管理；调用者不能访问其成员、复制
 *          句柄内存或直接释放句柄。
 */
typedef struct XSpi XSpi;

/**
 * @brief SPI 控制器、片选和传输格式配置。
 * @details XSpi_create 和 XSpi_configure 会复制整个结构，不保存调用者对
 *          XSpiConfig 的指针。
 */
typedef struct XSpiConfig {
    uint32_t m_controller; /**< 逻辑 SPI 控制器编号；有效范围由后端定义。 */
    uint32_t m_chipSelect; /**< 逻辑片选编号；有效范围由后端定义。 */
    uint32_t m_frequencyHz; /**< SCK 频率，单位 Hz；0 表示后端默认值。 */
    uint8_t m_mode;        /**< SPI 模式；有效范围 0..3，分别对应 CPOL/CPHA 组合。 */
    uint8_t m_bitsPerWord; /**< 每个传输字的位数；当前契约允许 8 或 16。 */
    bool m_lsbFirst;       /**< true 表示最低位先发送。 */
    uint32_t m_flags;      /**< 保留配置标志；当前必须为 0。 */
} XSpiConfig;

/**
 * @brief SPI 配置的安全默认值。
 * @details 默认选择逻辑控制器 0、片选 0、1 MHz、模式 0、8 bit 和高位先发；
 *          调用者应根据实际目标器件修改控制器、片选和时序参数。
 */
#define XSPI_CONFIG_INIT \
    { 0u, 0u, 1000000u, 0u, 8u, false, 0u }

/**
 * @brief SPI 后端能力位。
 * @details 各枚举值可以按位组合；后端只能报告当前控制器实际支持的能力。
 */
typedef enum XSpiFeature {
    XSpiFeature_None = 0,                 /**< 不具备可报告的 SPI 能力。 */
    XSpiFeature_Transfer = 1u << 0, /**< 支持全双工交换。 */
    XSpiFeature_Frequency = 1u << 1, /**< 支持运行时配置频率。 */
    XSpiFeature_Mode = 1u << 2,     /**< 支持运行时配置模式。 */
    XSpiFeature_BitsPerWord = 1u << 3, /**< 支持配置每字位数。 */
    XSpiFeature_LsbFirst = 1u << 4  /**< 支持 LSB first。 */
} XSpiFeature;

/** @brief XSpiFeature 按位组合后的 SPI 能力集合。 */
typedef uint32_t XSpiFeatures;

/**
 * @brief SPI 通用错误。
 * @details 平台后端可另外保存原生错误码，并通过 XSpi_nativeError 返回。
 */
typedef enum XSpiError {
    XSpiError_None = 0,         /**< 没有错误。 */
    XSpiError_InvalidArgument,  /**< 参数为 NULL、越界或配置组合无效。 */
    XSpiError_NotOpen,          /**< 句柄尚未打开控制器和片选资源。 */
    XSpiError_AlreadyOpen,      /**< 句柄已经打开，不能重复打开。 */
    XSpiError_Unsupported,      /**< 当前后端或硬件不支持请求的能力。 */
    XSpiError_Busy,             /**< 控制器、片选或传输队列正在使用。 */
    XSpiError_Timeout,          /**< 在指定时间内没有完成完整事务。 */
    XSpiError_PermissionDenied, /**< 当前进程或任务没有访问控制器的权限。 */
    XSpiError_Interrupted,      /**< 等待中的事务被系统或后端中断。 */
    XSpiError_Hardware,         /**< SPI 控制器或物理线路报告硬件错误。 */
    XSpiError_Closed,           /**< 控制器资源已关闭或设备已经断开。 */
    XSpiError_Unknown           /**< 未分类错误。 */
} XSpiError;

/**
 * @brief 创建 SPI 软件句柄。
 * @param config 初始配置；调用期间借用，函数复制其内容，不能为 NULL。
 * @return 成功返回新 SPI 句柄，分配或参数校验失败返回 NULL；成功返回的对象
 *         必须使用 XSpi_delete 释放。
 */
XSpi* XSpi_create(const XSpiConfig* config);
/**
 * @brief 打开控制器和片选资源。
 * @param spi SPI 句柄；不能为 NULL，且不能重复打开。
 * @return 成功返回 true；后端不支持、资源忙或硬件失败返回 false。
 */
bool XSpi_open(XSpi* spi);
/**
 * @brief 关闭控制器和片选资源但保留软件句柄。
 * @param spi SPI 句柄；可为 NULL，重复关闭安全。
 * @return 无。关闭后可以再次调用 XSpi_open，NULL 不执行任何操作。
 */
void XSpi_close(XSpi* spi);

/**
 * @brief 删除 SPI 句柄并释放后端资源。
 * @param spi 由 XSpi_create 返回的句柄；可为 NULL。
 * @return 无。函数会关闭仍处于打开状态的控制器和片选资源。
 * @warning 删除后不得继续使用 spi，也不得重复释放同一非 NULL 句柄。
 */
void XSpi_delete(XSpi* spi);

/**
 * @brief 判断 SPI 句柄是否已打开。
 * @param spi SPI 句柄；可为 NULL，函数只读借用该对象。
 * @return 已成功占用控制器和片选资源返回 true；spi 为 NULL 或尚未打开返回 false。
 */
bool XSpi_isOpen(const XSpi* spi);
/**
 * @brief 获取当前配置副本。
 * @param spi SPI 句柄；不能为 NULL，函数只读借用该对象。
 * @param config 调用者提供的输出对象；不能为 NULL，成功时写入配置副本。
 * @return 成功返回 true；参数非法返回 false，失败时 config 保持不变。
 */
bool XSpi_getConfig(const XSpi* spi, XSpiConfig* config);
/**
 * @brief 修改配置。
 * @param spi SPI 句柄；不能为 NULL。
 * @param config 新配置；调用期间借用，函数成功时复制其内容，不能为 NULL。
 * @return 成功返回 true；配置非法、不支持动态修改或硬件应用失败返回 false。
 * @note 失败时原配置和硬件状态应保持不变；不能回滚时应报告 XSpiError_Hardware。
 */
bool XSpi_configure(XSpi* spi, const XSpiConfig* config);
/**
 * @brief 执行全双工 SPI 事务。
 * @param spi 已打开的 SPI 句柄；不能为 NULL。
 * @param tx 发送缓冲区；调用期间只读借用，不取得所有权；只接收时可为 NULL。
 * @param rx 调用者提供的接收缓冲区；函数不取得所有权；只发送时可为 NULL。
 * @param length 交换字节数，单位 byte；允许为 0，非零时 tx 和 rx 不能同时为
 *               NULL，最大值由后端决定。
 * @param timeoutMs 整个事务的超时时间，单位 ms；0 表示非阻塞，正数表示最长
 *                  等待时间，负数表示按后端约定持续等待。
 * @return 完整事务成功返回 true；参数无效、未打开、超时、不支持目标配置或
 *         硬件失败返回 false，失败时 rx 内容未定义。函数返回后调用者可以
 *         立即复用或释放 tx 和 rx。
 */
bool XSpi_transfer(XSpi* spi, const uint8_t* tx, uint8_t* rx,
                   size_t length, int32_t timeoutMs);
/**
 * @brief 获取 SPI 后端能力位。
 * @param spi SPI 句柄；可为 NULL，函数只读借用该对象。
 * @return 当前控制器和后端实际支持的 XSpiFeature 组合；spi 为 NULL 时返回
 *         XSpiFeature_None。
 */
XSpiFeatures XSpi_features(const XSpi* spi);
/**
 * @brief 判断后端是否支持指定能力。
 * @param spi SPI 句柄；可为 NULL，函数只读借用该对象。
 * @param feature 要检查的单个 XSpiFeature 能力位。
 * @return 后端支持该能力返回 true；句柄无效、feature 为 None 或不支持时返回 false。
 */
bool XSpi_hasFeature(const XSpi* spi, XSpiFeature feature);
/**
 * @brief 获取 SPI 最近一次通用错误。
 * @param spi SPI 句柄；可为 NULL，函数只读借用该对象。
 * @return 最近一次 XSpiError；spi 为 NULL 时返回 XSpiError_InvalidArgument。
 */
XSpiError XSpi_lastError(const XSpi* spi);

/**
 * @brief 获取 SPI 最近一次平台原生错误码。
 * @param spi SPI 句柄；可为 NULL，函数只读借用该对象。
 * @return 平台原生错误码；无原生错误或 spi 为 NULL 时返回 0。
 */
int32_t XSpi_nativeError(const XSpi* spi);

/**
 * @brief 清除 SPI 错误状态。
 * @param spi SPI 句柄；可为 NULL。
 * @return 无。非 NULL 句柄清除后 XSpi_lastError 返回 XSpiError_None。
 */
void XSpi_clearError(XSpi* spi);

/**
 * @brief 获取 SPI 通用错误的稳定 ASCII 描述。
 * @param error 要转换的 XSpiError 枚举值。
 * @return 后端静态持有的零结尾 ASCII 字符串；调用者不能修改或释放，未知值
 *         返回 "unknown"。
 */
const char* XSpi_errorString(XSpiError error);

#ifdef __cplusplus
}
#endif

#endif /* XSPI_H */
