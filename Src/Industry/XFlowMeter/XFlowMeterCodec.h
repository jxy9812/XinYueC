
/**
 * @file       XFlowMeterCodec.h
 * @brief      电磁流量计状态编解码公开 API：流量计快照 ↔ JSON/BSON。
 * @details    对齐 Qt 无直接对应类（服务网络传输的序列化胶水层），与
 *             XDiffPressureGaugeCodec / XTempHumTransmitterCodec 同一套约定：
 *               - 键列表裁剪：调用方给出关心的 XFlowMeterField 数组，
 *                 输出只含这些键；
 *               - JSON 与 BSON 键名一致（同一张映射表），格式可互转；
 *               - **无控制面**：流量计是只读设备（无写参数/清零类动作），
 *                 故不提供 applyControl——设备语义的自然结果。
 *             快照解码产出 XFlowMeterSnapshot 值结构（零分配），供远端
 *             镜像/记录回放使用。
 */
#ifndef XFLOWMETERCODEC_H
#define XFLOWMETERCODEC_H

#ifdef __cplusplus
extern "C" {
#endif

#include "CXinYueConfig.h"
#include "XIndustry_config.h"

#if XINDUSTRY_ON && XFLOW_METER_ON

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include "XByteArray.h"
#include "XFlowMeter.h"

/**
 * @brief 流量计快照字段键（键列表元素；数值稳定，线上键名由实现映射表决定）。
 */
typedef enum XFlowMeterField {
    XFlowMeterField_None = 0,               /**< 占位（禁用）。 */
    XFlowMeterField_InstantFlow = 1 << 0,   /**< 瞬时流量（"flow"；double）。 */
    XFlowMeterField_InstantSpeed = 1 << 1,  /**< 瞬时流速（"speed"；double）。 */
    XFlowMeterField_FlowPercent = 1 << 2,   /**< 流量百分比（"percent"；double）。 */
    XFlowMeterField_Conductivity = 1 << 3,  /**< 流体电导比（"conductivity"；double）。 */
    XFlowMeterField_ForwardTotal = 1 << 4,  /**< 正向累积量（"forwardTotal"；double）。 */
    XFlowMeterField_ReverseTotal = 1 << 5,  /**< 反向累积量（"reverseTotal"；double）。 */
    XFlowMeterField_FlowUnit = 1 << 6,      /**< 瞬时流量单位（"flowUnit"；int32 代码）。 */
    XFlowMeterField_TotalUnit = 1 << 7,     /**< 累积总量单位（"totalUnit"；int32 代码）。 */
    XFlowMeterField_Alarms = 1 << 8,        /**< 报警位（"alarms"；int32 位掩码）。 */
    XFlowMeterField_Valid = 1 << 9,         /**< 质量（"valid"；bool）。 */
    XFlowMeterField_StaleMs = 1 << 10       /**< 陈旧阈值（"staleMs"；int32 毫秒）。 */
} XFlowMeterField;

/**
 * @brief 流量计快照（值语义，零分配；解码产物）。
 * @details 字段有效性由解码方按需判断（未提供的键保持 0/false）。
 */
typedef struct XFlowMeterSnapshot {
    double m_instantFlow;           /**< 瞬时流量（无效时为 0）。 */
    double m_instantSpeed;          /**< 瞬时流速。 */
    double m_flowPercent;           /**< 流量百分比。 */
    double m_conductivity;          /**< 流体电导比。 */
    double m_forwardTotal;          /**< 正向累积量。 */
    double m_reverseTotal;          /**< 反向累积量。 */
    uint16_t m_flowUnitCode;        /**< 瞬时流量单位代码（原样，未翻译）。 */
    uint16_t m_totalUnitCode;       /**< 累积总量单位代码。 */
    uint8_t m_alarmBits;            /**< XFlowMeterAlarm 位掩码。 */
    bool m_valid;                   /**< 质量（已收拍且未陈旧）。 */
    uint32_t m_staleMs;             /**< 陈旧阈值（毫秒；0=不判陈旧）。 */
} XFlowMeterSnapshot;

/* ==================== 状态编码（流量计 → 载荷） ==================== */

/**
 * @brief 流量计快照 → BSON 载荷（按键列表裁剪）。
 * @param meter 源流量计（借用；NULL 视为全零快照）。
 * @param keys 键列表（借用；NULL 时输出空文档）。
 * @param count 键数量；负数按 0 处理。
 * @return 新建 XByteArray（BSON 文档字节）；分配失败返回 NULL。
 *         @warning 返回值归调用方所有，用 XClassDelete 释放。
 */
XByteArray* XFlowMeterCodec_stateToBson(const XFlowMeter* meter,
                                        const XFlowMeterField* keys, int count);

/**
 * @brief 流量计快照 → JSON 载荷（紧凑 UTF-8；键名与 BSON 一致）。
 * @param meter 源流量计（借用；可为 NULL）。
 * @param keys 键列表（借用；可为 NULL）。
 * @param count 键数量。
 * @return 新建 XByteArray；分配失败返回 NULL。
 *         @warning 返回值归调用方所有，用 XClassDelete 释放。
 */
XByteArray* XFlowMeterCodec_stateToJson(const XFlowMeter* meter,
                                        const XFlowMeterField* keys, int count);

/**
 * @brief 全部快照键的现成列表。
 * @param outCount 输出键数量（可为 NULL——此时返回 NULL）。
 * @return 静态常量数组（借用，禁释放）。
 */
const XFlowMeterField* XFlowMeterCodec_allKeys(int* outCount);

/* ==================== 状态解码（载荷 → 快照） ==================== */

/**
 * @brief BSON 载荷 → 流量计快照（就地填充）。
 * @param out 输出快照（调用方提供存储；先整体清零再填充）。
 * @param data BSON 文档字节（借用；调用期间有效）。
 * @param size 字节数。
 * @return 解析成功返回 true；data 为 NULL、size 为 0 或非 BSON 文档返回
 *         false（out 已清零）。
 */
bool XFlowMeterCodec_stateFromBson(XFlowMeterSnapshot* out,
                                   const uint8_t* data, size_t size);

/**
 * @brief JSON 载荷 → 流量计快照（就地填充）。
 * @param out 输出快照。
 * @param data UTF-8 JSON 文本字节（借用）。
 * @param size 字节数。
 * @return 解析成功返回 true；非法 JSON / 顶层非对象返回 false。
 */
bool XFlowMeterCodec_stateFromJson(XFlowMeterSnapshot* out,
                                   const uint8_t* data, size_t size);

#endif /* XINDUSTRY_ON && XFLOW_METER_ON */

#ifdef __cplusplus
}
#endif
#endif /* XFLOWMETERCODEC_H */
