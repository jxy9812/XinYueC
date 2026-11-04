
/**
 * @file       XDiffPressureGaugeCodec.h
 * @brief      压差计状态编解码公开 API：压差快照 ↔ JSON/BSON。
 * @details    对齐 Qt 无直接对应类（服务网络传输的序列化胶水层），与
 *             XSolenoidValveCodec / XTempHumTransmitterCodec 同一套约定：
 *               - 键列表裁剪：调用方给出关心的 XDiffPressureGaugeField
 *                 数组，输出只含这些键；
 *               - JSON 与 BSON 键名一致（同一张映射表），格式可互转；
 *               - **无控制面**：压差计是只读设备（无 open/close 类动作），
 *                 故不提供 applyControl——设备语义的自然结果。
 *             快照解码产出 XDiffPressureGaugeSnapshot 值结构（零分配），
 *             供远端镜像/记录回放使用。
 */
#ifndef XDIFFPRESSUREGAUGECODEC_H
#define XDIFFPRESSUREGAUGECODEC_H

#ifdef __cplusplus
extern "C" {
#endif

#include "CXinYueConfig.h"
#include "XIndustry_config.h"

#if XINDUSTRY_ON && XPRESS_DIFF_ON

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include "XByteArray.h"
#include "XDiffPressureGauge.h"

/**
 * @brief 压差快照字段键（键列表元素；数值稳定，线上键名由实现映射表决定）。
 */
typedef enum XDiffPressureGaugeField {
    XDiffPressureGaugeField_None = 0,       /**< 占位（禁用）。 */
    XDiffPressureGaugeField_Value = 1 << 0, /**< 压差工程值（"value"；int32 定点）。 */
    XDiffPressureGaugeField_Valid = 1 << 1, /**< 质量（"valid"；bool）。 */
    XDiffPressureGaugeField_StaleMs = 1 << 2 /**< 陈旧阈值（"staleMs"；int32 毫秒）。 */
} XDiffPressureGaugeField;

/**
 * @brief 压差快照（值语义，零分配；解码产物）。
 * @details 字段有效性由解码方按需判断（未提供的键保持 0/false）。
 */
typedef struct XDiffPressureGaugeSnapshot {
    int32_t m_valueEng;             /**< 压差工程值（定点；无效时为 0）。 */
    bool m_valid;                   /**< 质量（已收拍且未陈旧）。 */
    uint32_t m_staleMs;             /**< 陈旧阈值（毫秒；0=不判陈旧）。 */
} XDiffPressureGaugeSnapshot;

/* ==================== 状态编码（压差计 → 载荷） ==================== */

/**
 * @brief 压差快照 → BSON 载荷（按键列表裁剪）。
 * @param gauge 源压差计（借用；NULL 视为全零快照）。
 * @param keys 键列表（借用；NULL 时输出空文档）。
 * @param count 键数量；负数按 0 处理。
 * @return 新建 XByteArray（BSON 文档字节）；分配失败返回 NULL。
 *         @warning 返回值归调用方所有，用 XClassDelete 释放。
 */
XByteArray* XDiffPressureGaugeCodec_stateToBson(const XDiffPressureGauge* gauge,
                                                const XDiffPressureGaugeField* keys,
                                                int count);

/**
 * @brief 压差快照 → JSON 载荷（紧凑 UTF-8；键名与 BSON 一致）。
 * @param gauge 源压差计（借用；可为 NULL）。
 * @param keys 键列表（借用；可为 NULL）。
 * @param count 键数量。
 * @return 新建 XByteArray；分配失败返回 NULL。
 *         @warning 返回值归调用方所有，用 XClassDelete 释放。
 */
XByteArray* XDiffPressureGaugeCodec_stateToJson(const XDiffPressureGauge* gauge,
                                                const XDiffPressureGaugeField* keys,
                                                int count);

/**
 * @brief 全部快照键的现成列表（value/valid/staleMs）。
 * @param outCount 输出键数量（可为 NULL——此时返回 NULL）。
 * @return 静态常量数组（借用，禁释放）。
 */
const XDiffPressureGaugeField* XDiffPressureGaugeCodec_allKeys(int* outCount);

/* ==================== 状态解码（载荷 → 快照） ==================== */

/**
 * @brief BSON 载荷 → 压差快照（就地填充）。
 * @param out 输出快照（调用方提供存储；先整体清零再填充）。
 * @param data BSON 文档字节（借用；调用期间有效）。
 * @param size 字节数。
 * @return 解析成功返回 true；data 为 NULL、size 为 0 或非 BSON 文档返回
 *         false（out 已清零）。
 */
bool XDiffPressureGaugeCodec_stateFromBson(XDiffPressureGaugeSnapshot* out,
                                           const uint8_t* data, size_t size);

/**
 * @brief JSON 载荷 → 压差快照（就地填充）。
 * @param out 输出快照。
 * @param data UTF-8 JSON 文本字节（借用）。
 * @param size 字节数。
 * @return 解析成功返回 true；非法 JSON / 顶层非对象返回 false。
 */
bool XDiffPressureGaugeCodec_stateFromJson(XDiffPressureGaugeSnapshot* out,
                                           const uint8_t* data, size_t size);

#endif /* XINDUSTRY_ON && XPRESS_DIFF_ON */

#ifdef __cplusplus
}
#endif
#endif /* XDIFFPRESSUREGAUGECODEC_H */
