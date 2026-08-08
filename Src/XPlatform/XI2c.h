/**
 * @file       XI2c.h
 * @brief      I2C 平台无关事务接口。
 * @details    本接口没有 Qt 对齐对象。本文件只定义逻辑控制器、从设备地址、
 *             配置、事务和错误契约，不包含 Linux ioctl、Windows 驱动、
 *             STM32 HAL 或 RTOS 头文件。
 *             具体平台后端应在 Drive 目录实现同名函数；不支持的能力必须
 *             返回 XI2cError_Unsupported，不能静默改变调用者的事务。
 *             XI2c 是独占总线目标的不透明句柄，不支持复制、移动或跨线程
 *             并发使用，除非平台后端另有明确说明。
 */
#ifndef XI2C_H
#define XI2C_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief I2C 不透明句柄。
 * @details 具体结构和控制器资源由平台后端管理；调用者不能访问其成员、复制
 *          句柄内存或直接释放句柄。
 */
typedef struct XI2c XI2c;

/** @brief I2C 地址格式；各枚举值互斥，不能按位组合。 */
typedef enum XI2cAddressMode {
    XI2cAddressMode_SevenBit = 0, /**< 标准 7 位从设备地址。 */
    XI2cAddressMode_TenBit         /**< 扩展 10 位从设备地址。 */
} XI2cAddressMode;

/** @brief I2C 逻辑控制器和目标设备地址。 */
typedef struct XI2cTarget {
    uint32_t m_controller; /**< 逻辑 I2C 控制器编号；有效范围由后端定义。 */
    uint16_t m_address;    /**< 不含读写位的目标地址；7 位范围 0..0x7f，10 位范围 0..0x3ff。 */
    XI2cAddressMode m_addressMode; /**< 7 位或 10 位地址格式。 */
} XI2cTarget;

/** @brief I2C 配置；后端必须复制配置内容，不保存调用者指针。 */
typedef struct XI2cConfig {
    XI2cTarget m_target;   /**< 控制器和从设备地址。 */
    uint32_t m_frequencyHz; /**< SCL 频率，单位 Hz；0 表示使用后端默认值。 */
    uint32_t m_flags;      /**< 保留配置标志；当前必须为 0。 */
} XI2cConfig;

/**
 * @brief I2C 配置的安全默认值。
 * @details 默认选择逻辑控制器 0、7 位地址 0 和 100 kHz；调用者必须根据
 *          实际目标设备修改地址，并确认该地址不是平台保留地址。
 */
#define XI2C_CONFIG_INIT \
    { { 0u, 0u, XI2cAddressMode_SevenBit }, 100000u, 0u }

/**
 * @brief I2C 后端能力位。
 * @details 各枚举值可以按位组合；后端只能报告当前控制器实际支持的能力。
 */
typedef enum XI2cFeature {
    XI2cFeature_None = 0,                 /**< 不具备可报告的 I2C 能力。 */
    XI2cFeature_Read = 1u << 0,       /**< 支持读事务。 */
    XI2cFeature_Write = 1u << 1,      /**< 支持写事务。 */
    XI2cFeature_WriteRead = 1u << 2,  /**< 支持无 STOP 的写后读事务。 */
    XI2cFeature_TenBitAddress = 1u << 3, /**< 支持 10 位地址。 */
    XI2cFeature_Frequency = 1u << 4,  /**< 支持运行时配置频率。 */
    XI2cFeature_ProcessEvents = 1u << 5 /**< 支持事件轮询（保留）。 */
} XI2cFeature;

/** @brief XI2cFeature 按位组合后的 I2C 能力集合。 */
typedef uint32_t XI2cFeatures;

/**
 * @brief I2C 通用错误。
 * @details 平台后端可另外保存原生错误码，并通过 XI2c_nativeError 返回。
 */
typedef enum XI2cError {
    XI2cError_None = 0,          /**< 没有错误。 */
    XI2cError_InvalidArgument,   /**< 参数为 NULL、地址越界或配置组合无效。 */
    XI2cError_NotOpen,           /**< 句柄尚未打开控制器资源。 */
    XI2cError_AlreadyOpen,       /**< 句柄已经打开，不能重复打开。 */
    XI2cError_Unsupported,       /**< 当前后端或硬件不支持请求的能力。 */
    XI2cError_Busy,              /**< 控制器或总线正被其他事务占用。 */
    XI2cError_Timeout,           /**< 在指定时间内没有完成完整事务。 */
    XI2cError_Nack,              /**< 目标地址或数据阶段收到 NACK。 */
    XI2cError_ArbitrationLost,   /**< 多主机总线仲裁失败。 */
    XI2cError_Bus,               /**< 检测到非法总线状态或总线错误。 */
    XI2cError_PermissionDenied,  /**< 当前进程或任务没有访问控制器的权限。 */
    XI2cError_Interrupted,       /**< 等待中的事务被系统或后端中断。 */
    XI2cError_Hardware,          /**< I2C 控制器或物理线路报告硬件错误。 */
    XI2cError_Closed,            /**< 控制器资源已关闭或设备已经断开。 */
    XI2cError_Unknown            /**< 未分类错误。 */
} XI2cError;

/**
 * @brief 创建 I2C 软件句柄。
 * @param config 初始配置；调用期间借用，函数复制其内容，不能为 NULL。
 * @return 成功返回新 I2C 句柄，分配或参数校验失败返回 NULL；成功返回的对象
 *         必须使用 XI2c_delete 释放。
 */
XI2c* XI2c_create(const XI2cConfig* config);
/**
 * @brief 打开配置的 I2C 控制器和从设备。
 * @param bus I2C 句柄；不能为 NULL，且不能重复打开。
 * @return 成功返回 true；后端不支持、资源忙或硬件失败返回 false。
 */
bool XI2c_open(XI2c* bus);
/**
 * @brief 关闭控制器资源但保留软件句柄。
 * @param bus I2C 句柄；可为 NULL，重复关闭安全。
 * @return 无。关闭后可以再次调用 XI2c_open，NULL 不执行任何操作。
 */
void XI2c_close(XI2c* bus);
/**
 * @brief 删除句柄并释放后端资源。
 * @param bus 由 XI2c_create 返回的句柄；可为 NULL。
 * @return 无。函数会关闭仍处于打开状态的控制器资源。
 * @warning 删除后不得继续使用 bus，也不得重复释放同一非 NULL 句柄。
 */
void XI2c_delete(XI2c* bus);
/**
 * @brief 判断 I2C 句柄是否已打开。
 * @param bus I2C 句柄；可为 NULL，函数只读借用该对象。
 * @return 已成功占用控制器资源返回 true；bus 为 NULL 或尚未打开返回 false。
 */
bool XI2c_isOpen(const XI2c* bus);
/**
 * @brief 获取当前配置副本。
 * @param bus I2C 句柄；不能为 NULL，函数只读借用该对象。
 * @param config 调用者提供的输出对象；不能为 NULL，成功时写入配置副本。
 * @return 成功返回 true；参数非法返回 false，失败时 config 保持不变。
 */
bool XI2c_getConfig(const XI2c* bus, XI2cConfig* config);
/**
 * @brief 修改配置。
 * @param bus I2C 句柄；不能为 NULL。
 * @param config 新配置；调用期间借用，函数成功时复制其内容，不能为 NULL。
 * @return 成功返回 true；配置非法、不支持动态修改或硬件应用失败返回 false。
 * @note 失败时原配置和硬件状态应保持不变；不能回滚时应报告 XI2cError_Hardware。
 */
bool XI2c_configure(XI2c* bus, const XI2cConfig* config);
/**
 * @brief 执行写事务。
 * @param bus 已打开的 I2C 句柄；不能为 NULL。
 * @param data 待发送字节缓冲区；调用期间只读借用，不取得所有权；length 为
 *             0 时可为 NULL，否则不能为 NULL。
 * @param length 待发送字节数，单位 byte；允许为 0，最大值由后端决定。
 * @param timeoutMs 整个事务的超时时间，单位 ms；0 表示非阻塞，正数表示最长
 *                  等待时间，负数表示按后端约定持续等待。
 * @return 完整事务成功返回 true；参数无效、未打开、NACK、超时或硬件失败
 *         返回 false。函数返回后调用者可以立即复用或释放 data。
 */
bool XI2c_write(XI2c* bus, const uint8_t* data, size_t length,
                int32_t timeoutMs);
/**
 * @brief 执行读事务。
 * @param bus 已打开的 I2C 句柄；不能为 NULL。
 * @param data 调用者提供的接收缓冲区；length 为 0 时可为 NULL，否则不能为
 *             NULL；函数不取得缓冲区所有权。
 * @param length 要读取的字节数，单位 byte；允许为 0，最大值由后端决定。
 * @param timeoutMs 整个事务的超时时间，单位 ms；语义与 XI2c_write 相同。
 * @return 完整事务成功返回 true，失败时 data 内容保持未定义。
 */
bool XI2c_read(XI2c* bus, uint8_t* data, size_t length,
               int32_t timeoutMs);
/**
 * @brief 执行写后重复起始再读事务。
 * @param bus 已打开的 I2C 句柄；不能为 NULL。
 * @param writeData 写入缓冲区；调用期间只读借用；writeLength 为 0 时可为
 *                  NULL，否则不能为 NULL。
 * @param writeLength 重复起始前写入的字节数，单位 byte；允许为 0。
 * @param readData 调用者提供的读取缓冲区；readLength 为 0 时可为 NULL，否则
 *                 不能为 NULL；函数不取得所有权。
 * @param readLength 重复起始后读取的字节数，单位 byte；允许为 0。
 * @param timeoutMs 整个组合事务的超时时间，单位 ms；语义与 XI2c_write 相同。
 * @return 无 STOP 的写后读组合事务完整成功返回 true；参数无效、未打开、
 *         不支持重复起始、NACK、超时或硬件失败返回 false，失败时 readData
 *         内容未定义。
 */
bool XI2c_writeRead(XI2c* bus, const uint8_t* writeData, size_t writeLength,
                    uint8_t* readData, size_t readLength, int32_t timeoutMs);
/**
 * @brief 获取 I2C 后端能力位。
 * @param bus I2C 句柄；可为 NULL，函数只读借用该对象。
 * @return 当前控制器和后端实际支持的 XI2cFeature 组合；bus 为 NULL 时返回
 *         XI2cFeature_None。
 */
XI2cFeatures XI2c_features(const XI2c* bus);
/**
 * @brief 判断后端是否支持指定能力。
 * @param bus I2C 句柄；可为 NULL，函数只读借用该对象。
 * @param feature 要检查的单个 XI2cFeature 能力位。
 * @return 后端支持该能力返回 true；句柄无效、feature 为 None 或不支持时返回 false。
 */
bool XI2c_hasFeature(const XI2c* bus, XI2cFeature feature);
/**
 * @brief 获取 I2C 最近一次通用错误。
 * @param bus I2C 句柄；可为 NULL，函数只读借用该对象。
 * @return 最近一次 XI2cError；bus 为 NULL 时返回 XI2cError_InvalidArgument。
 */
XI2cError XI2c_lastError(const XI2c* bus);

/**
 * @brief 获取 I2C 最近一次平台原生错误码。
 * @param bus I2C 句柄；可为 NULL，函数只读借用该对象。
 * @return 平台原生错误码；无原生错误或 bus 为 NULL 时返回 0。
 */
int32_t XI2c_nativeError(const XI2c* bus);

/**
 * @brief 清除 I2C 错误状态。
 * @param bus I2C 句柄；可为 NULL。
 * @return 无。非 NULL 句柄清除后 XI2c_lastError 返回 XI2cError_None。
 */
void XI2c_clearError(XI2c* bus);

/**
 * @brief 获取 I2C 通用错误的稳定 ASCII 描述。
 * @param error 要转换的 XI2cError 枚举值。
 * @return 后端静态持有的零结尾 ASCII 字符串；调用者不能修改或释放，未知值
 *         返回 "unknown"。
 */
const char* XI2c_errorString(XI2cError error);

#ifdef __cplusplus
}
#endif

#endif /* XI2C_H */
