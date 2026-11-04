
/**
 * @file       XTempHumTransmitterCodec.h
 * @brief      温湿度变送器状态编解码公开 API：阀位快照 ↔ JSON/BSON。
 * @details    对齐 Qt 无直接对应类（服务网络传输的序列化胶水层），与
 *             XSolenoidValveCodec（电磁阀侧）同一套约定：
 *               - 键列表裁剪：调用方给出关心的 XTempHumTransmitterField
 *                 数组，输出只含这些键；
 *               - JSON 与 BSON 键名一致（同一张映射表），格式可互转；
 *               - **无控制面**：变送器是只读设备（无 open/close 类动作），
 *                 故不提供 applyControl——这是与电磁阀 codec 的唯一差异，
 *                 也是设备语义的自然结果。
 *             快照解码产出 XTempHumTransmitterSnapshot 值结构（零分配），
 *             供远端镜像/记录回放使用。
 */
#ifndef XTEMPHUMTRANSMITTERCODEC_H
#define XTEMPHUMTRANSMITTERCODEC_H

#ifdef __cplusplus
extern "C" {
#endif

#include "CXinYueConfig.h"
#include "XIndustry_config.h"

#if XINDUSTRY_ON && XTEMP_HUM_ON

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include "XByteArray.h"
#include "XTempHumTransmitter.h"

/**
 * @brief 温湿度快照字段键（键列表元素；数值稳定，线上键名由实现映射表决定）。
 */
typedef enum XTempHumTransmitterField {
    XTempHumTransmitterField_None = 0,      /**< 占位（禁用）。 */
    XTempHumTransmitterField_TempValue = 1 << 0,   /**< 温度工程值（"temp"；int32 定点）。 */
    XTempHumTransmitterField_HumValue = 1 << 1,    /**< 湿度工程值（"hum"；int32 定点）。 */
    XTempHumTransmitterField_TempValid = 1 << 2,   /**< 温度质量（"tempValid"；bool）。 */
    XTempHumTransmitterField_HumValid = 1 << 3,    /**< 湿度质量（"humValid"；bool）。 */
    XTempHumTransmitterField_StaleMs = 1 << 4      /**< 陈旧阈值（"staleMs"；int32 毫秒）。 */
} XTempHumTransmitterField;

/**
 * @brief 温湿度快照（值语义，零分配；解码产物）。
 * @details 字段有效性由解码方按需判断（未提供的键保持 0/false）。
 */
typedef struct XTempHumTransmitterSnapshot {
    int32_t m_tempEng;              /**< 温度工程值（定点；无效时为 0）。 */
    int32_t m_humEng;               /**< 湿度工程值（定点；无效时为 0）。 */
    bool m_tempValid;               /**< 温度质量（已收拍且未陈旧）。 */
    bool m_humValid;                /**< 湿度质量。 */
    uint32_t m_staleMs;             /**< 陈旧阈值（毫秒；0=不判陈旧）。 */
} XTempHumTransmitterSnapshot;

/* ==================== 状态编码（变送器 → 载荷） ==================== */

/**
 * @brief 温湿度快照 → BSON 载荷（按键列表裁剪）。
 * @param transmitter 源变送器（借用；NULL 视为全零快照）。
 * @param keys 键列表（借用；NULL 时输出空文档）。
 * @param count 键数量；负数按 0 处理。
 * @return 新建 XByteArray（BSON 文档字节）；分配失败返回 NULL。
 *         @warning 返回值归调用方所有，用 XClassDelete 释放。
 */
XByteArray* XTempHumTransmitterCodec_stateToBson(const XTempHumTransmitter* transmitter,
                                                 const XTempHumTransmitterField* keys,
                                                 int count);

/**
 * @brief 温湿度快照 → JSON 载荷（紧凑 UTF-8；键名与 BSON 一致）。
 * @param transmitter 源变送器（借用；可为 NULL）。
 * @param keys 键列表（借用；可为 NULL）。
 * @param count 键数量。
 * @return 新建 XByteArray；分配失败返回 NULL。
 *         @warning 返回值归调用方所有，用 XClassDelete 释放。
 */
XByteArray* XTempHumTransmitterCodec_stateToJson(const XTempHumTransmitter* transmitter,
                                                 const XTempHumTransmitterField* keys,
                                                 int count);

/**
 * @brief 全部快照键的现成列表（temp/hum/tempValid/humValid/staleMs）。
 * @param outCount 输出键数量（可为 NULL——此时返回 NULL）。
 * @return 静态常量数组（借用，禁释放）。
 */
const XTempHumTransmitterField* XTempHumTransmitterCodec_allKeys(int* outCount);

/* ==================== 状态解码（载荷 → 快照） ==================== */

/**
 * @brief BSON 载荷 → 温湿度快照（就地填充）。
 * @param out 输出快照（调用方提供存储；先整体清零再填充）。
 * @param data BSON 文档字节（借用；调用期间有效）。
 * @param size 字节数。
 * @return 解析成功返回 true；data 为 NULL、size 为 0 或非 BSON 文档返回
 *         false（out 已清零）。
 */
bool XTempHumTransmitterCodec_stateFromBson(XTempHumTransmitterSnapshot* out,
                                            const uint8_t* data, size_t size);

/**
 * @brief JSON 载荷 → 温湿度快照（就地填充）。
 * @param out 输出快照。
 * @param data UTF-8 JSON 文本字节（借用）。
 * @param size 字节数。
 * @return 解析成功返回 true；非法 JSON / 顶层非对象返回 false。
 */
bool XTempHumTransmitterCodec_stateFromJson(XTempHumTransmitterSnapshot* out,
                                            const uint8_t* data, size_t size);

#endif /* XINDUSTRY_ON && XTEMP_HUM_ON */

#ifdef __cplusplus
}
#endif
#endif /* XTEMPHUMTRANSMITTERCODEC_H */
