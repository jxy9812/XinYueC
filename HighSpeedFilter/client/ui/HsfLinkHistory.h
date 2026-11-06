/**
 * @file       HsfLinkHistory.h
 * @brief      历史分页（0x0118）应答解析：服务端原始值 → 15 个显示列。
 * @details    服务端只发**原始值**（库里存的是 numeric + 模式文本 + 单位
 *             文本），显示口径集中在客户端（对齐 Python 客户端
 *             _server_row_texts）：
 *               时间 / 模式 / 搅拌状态 / 输出频率 / 输出电流 / 搅拌转速 /
 *               浊度1 / 浊度2 / 瞬时流量 / 正累计 / 压差 / 电箱温度 /
 *               电箱湿度 / DI(32位) / DO(32位)
 *             缺数据的列显示 "--"（与 Python 口径一致）。
 */
#ifndef HSF_LINK_HISTORY_H
#define HSF_LINK_HISTORY_H

#ifdef __cplusplus
extern "C" {
#endif

#include "CXinYueConfig.h"
#include <stdbool.h>
#include <stdint.h>

/** @brief 浊度越限阈值（NTU；与水质监测页同口径，也是异常行判据）。 */
#define HSF_HISTORY_TURB_WARN_NTU   20.0

/**
 * @brief 历史分页的一行（15 个显示格；已格式化）。
 */
typedef struct HsfHistoryRow {
    char m_cells[15][40]; /**< 15 个显示格（UTF-8；"--"=无数据）。 */
    bool m_abnormal;       /**< 异常行（故障/浊度越限/压差越限）。 */
} HsfHistoryRow;

/**
 * @brief 一页历史（解析产物）。
 */
typedef struct HsfHistoryPage {
    bool m_valid;          /**< 已解析过应答。 */
    int  m_total;          /**< 本窗口内总行数（页码总数按它算）。 */
    int  m_offset;         /**< 本页起始偏移。 */
    int  m_limit;           /**< 本页行数上限。 */
    int  m_count;           /**< 本页实际行数。 */
    int64_t m_latestTs;     /**< 最新有效墙钟毫秒。 */
    int64_t m_earliestTs;   /**< 最早有效墙钟毫秒。 */
    HsfHistoryRow m_rows[64]; /**< 本页行（协议上限 50 行 + 余量）。 */
} HsfHistoryPage;

/**
 * @brief 解析 0x0118 应答（BSON）到结果结构。
 * @param data BSON 字节；len 长度。
 * @param out  结果输出（调用方提供；内部会先清零）。
 * @return 解析到有效应答返回 true。
 */
bool HsfHistoryPage_parse(const uint8_t* data, uint32_t len,
                          HsfHistoryPage* out);

#ifdef __cplusplus
}
#endif

#endif /* HSF_LINK_HISTORY_H */
