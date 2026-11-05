/**
 * @file       XTurbidityMeterCodec.h
 * @brief      浊度仪状态编解码公开 API：浊度快照 ↔ JSON/BSON。
 * @details    对齐 Qt 无直接对应类（服务网络传输的序列化胶水层），与
 *             XFlowMeterCodec / XDiffPressureGaugeCodec 同一套约定：
 *               - 键列表裁剪：调用方给出关心的 XTurbidityMeterField 数组，
 *                 输出只含这些键；
 *               - JSON 与 BSON 键名一致（同一张映射表），格式可互转；
 *               - **无控制面**：浊度仪是只读设备（标定/偏移/清洗都在仪表面板
 *                 上做，对外通讯只暴露测量值），故不提供 applyControl——
 *                 设备语义的自然结果。
 *             快照解码产出 XTurbidityMeterSnapshot 值结构（零分配），供远端
 *             镜像/记录回放使用。
 */
#ifndef XTURBIDITYMETERCODEC_H
#define XTURBIDITYMETERCODEC_H

#ifdef __cplusplus
extern "C" {
#endif

#include "CXinYueConfig.h"
#include "XIndustry_config.h"

#if XINDUSTRY_ON && XTURBIDITY_ON

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include "XByteArray.h"
#include "XTurbidityMeter.h"

/**
 * @brief 浊度仪快照字段键（键列表元素；数值稳定，线上键名由实现映射表决定）。
 */
typedef enum XTurbidityMeterField {
    XTurbidityMeterField_None = 0,           /**< 占位（禁用）。 */
    XTurbidityMeterField_Ntu = 1 << 0,       /**< 浊度（"ntu"；double，NTU）。 */
    XTurbidityMeterField_RawBits = 1 << 1,   /**< 32 位原始位模式（"rawBits"；uint32，诊断用）。 */
    XTurbidityMeterField_Source = 1 << 2,    /**< 数据源形态（"source"；int32 枚举）。 */
    XTurbidityMeterField_Valid = 1 << 3,     /**< 质量（"valid"；bool）。 */
    XTurbidityMeterField_StaleMs = 1 << 4    /**< 陈旧阈值（"staleMs"；int32 毫秒）。 */
} XTurbidityMeterField;

/**
 * @brief 浊度仪快照（值语义，零分配；解码产物）。
 * @details 字段有效性由解码方按需判断（未提供的键保持 0/false）。
 * @note  **ntu 是 double（NTU 原值），不是定点整数**。与流量计的 double 口径
 *        一致；与"模拟量工程值 ×100 定点"的差异是因为浊度量程跨 0.00~1000.0，
 *        分辨率 0.01，用 0.01NTU 定点（int32，上限 2147 万 NTU）其实也装得下，
 *        但上位机要显示就得再除一次——多一次换算就多一处可能对不上的口径。
 *        传输层若要求整数（如 BSON 上的定点约定），请在该层做，不在本层。
 */
typedef struct XTurbidityMeterSnapshot {
    double m_ntu;                   /**< 浊度（NTU；无效时为 0）。 */
    uint32_t m_rawBits;             /**< Words 模式 32 位原始位模式（诊断）。 */
    int32_t m_source;               /**< XTurbidityMeterSourceMode 枚举值。 */
    bool m_valid;                   /**< 质量（已收拍且未陈旧）。 */
    uint32_t m_staleMs;             /**< 陈旧阈值（毫秒；0=不判陈旧）。 */
} XTurbidityMeterSnapshot;

/* ==================== 状态编码（浊度仪 → 载荷） ==================== */

/**
 * @brief 浊度仪快照 → BSON 载荷（按键列表裁剪）。
 * @param meter 源浊度仪（借用；NULL 视为全零快照）。
 * @param keys 键列表（借用；NULL 时输出空文档）。
 * @param count 键数量；负数按 0 处理。
 * @return 新建 XByteArray（BSON 文档字节）；分配失败返回 NULL。
 *         @warning 返回值归调用方所有，用 XClassDelete 释放。
 */
XByteArray* XTurbidityMeterCodec_stateToBson(const XTurbidityMeter* meter,
                                             const XTurbidityMeterField* keys,
                                             int count);

/**
 * @brief 浊度仪快照 → JSON 载荷（紧凑 UTF-8；键名与 BSON 一致）。
 * @param meter 源浊度仪（借用；可为 NULL）。
 * @param keys 键列表（借用；可为 NULL）。
 * @param count 键数量。
 * @return 新建 XByteArray；分配失败返回 NULL。
 *         @warning 返回值归调用方所有，用 XClassDelete 释放。
 */
XByteArray* XTurbidityMeterCodec_stateToJson(const XTurbidityMeter* meter,
                                             const XTurbidityMeterField* keys,
                                             int count);

/**
 * @brief 全部快照键的现成列表。
 * @param outCount 输出键数量（可为 NULL——此时返回 NULL）。
 * @return 静态常量数组（借用，禁释放）。
 */
const XTurbidityMeterField* XTurbidityMeterCodec_allKeys(int* outCount);

/* ==================== 状态解码（载荷 → 快照） ==================== */

/**
 * @brief BSON 载荷 → 浊度仪快照（就地填充）。
 * @param out 输出快照（调用方提供存储；先整体清零再填充）。
 * @param data BSON 文档字节（借用；调用期间有效）。
 * @param size 字节数。
 * @return 解析成功返回 true；data 为 NULL、size 为 0 或非 BSON 文档返回
 *         false（out 已清零）。
 */
bool XTurbidityMeterCodec_stateFromBson(XTurbidityMeterSnapshot* out,
                                        const uint8_t* data, size_t size);

/**
 * @brief JSON 载荷 → 浊度仪快照（就地填充）。
 * @param out 输出快照。
 * @param data UTF-8 JSON 文本字节（借用）。
 * @param size 字节数。
 * @return 解析成功返回 true；非法 JSON / 顶层非对象返回 false。
 */
bool XTurbidityMeterCodec_stateFromJson(XTurbidityMeterSnapshot* out,
                                        const uint8_t* data, size_t size);

#endif /* XINDUSTRY_ON && XTURBIDITY_ON */

#ifdef __cplusplus
}
#endif
#endif /* XTURBIDITYMETERCODEC_H */
