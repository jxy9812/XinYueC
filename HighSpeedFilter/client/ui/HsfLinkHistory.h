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
#include "XhsfLinkProto.h"
#include <stdbool.h>
#include <stdint.h>

/** @brief 浊度越限阈值（NTU；与水质监测页同口径，也是异常行判据）。 */
#define HSF_HISTORY_TURB_WARN_NTU   20.0

/** @brief 数值列区间（3 输出频率 … 13 电箱湿度；图表逐列取数用）。
 *  @note  2026-11-10 插入「水温(℃)」显示列（第 8 位，紧跟浊度2）⇒
 *         右端从 12 移到 13（水温也是数值列，画进折线）。 */
#define HSF_HISTORY_NUM_FIRST   3
#define HSF_HISTORY_NUM_LAST    13
/** @brief 数值列个数（= 折线序列数）。 */
#define HSF_HISTORY_NUM_COUNT   (HSF_HISTORY_NUM_LAST - HSF_HISTORY_NUM_FIRST + 1)

/**
 * @brief 历史应答一行的**协议列数**（解析时按它把 BSON 数组摊成 cell[]）。
 * @note  [2026-11-10] 加「水温」列后协议列从 18 变 19：解析端写死 18 会
 *        读不到最后那一格。**已改为引用公共头**（XhsfLinkProto.h 的
 *        HSF_HISTORY_WIRE_COLS）：此处再写一个 19 就是两处数字、必漂移
 *        （同一根因：HsfDataHistory.c 的 cols[15] 栈越界）。
 */
#define HSF_HISTORY_COLUMNS     HSF_HISTORY_WIRE_COLS

/**
 * @brief 历史分页的一行（16 个显示格；已格式化）。
 * @note  [原始值旁路] 显示格是给人看的（含单位/"--"/越限说明），图表要
 *        的是**未格式化原值 + 真实时间戳**。只从显示格反解析会把带单位的
 *        「瞬时流量」「正累计」整列丢掉（strtod 停在单位前的空格），也会
 *        让 X 轴退化成页内下标（缩放到真实时间域就错位）。故解析时同时
 *        留存 m_ts（epoch 毫秒）与 m_num/m_hasNum（第 3..13 列原值）。
 */
typedef struct HsfHistoryRow {
    /* [显示格数 == 显示列数] 绑定公共宏（不再写死 16）：显示列数一变，
     * 这里的缓冲若漏改 ⇒ 解析时按新列数写进旧缓冲 ⇒ 越界踩坏结构体
     * （与 HsfDataHistory.c 的 cols[15] 栈越界同源）。 */
    char m_cells[HSF_HISTORY_DISPLAY_COLS][40]; /**< 显示格（UTF-8；"--"=无数据）。 */
    bool m_abnormal;       /**< 异常行（故障/浊度越限/压差越限）。 */
    int64_t m_ts;          /**< 该行真实时间戳（epoch 毫秒；0=无）。 */
    double m_num[HSF_HISTORY_NUM_COUNT]; /**< 数值列原值（列 3..12）。 */
    bool m_hasNum[HSF_HISTORY_NUM_COUNT];/**< 对应原值是否存在。 */
} HsfHistoryRow;

/**
 * @brief 一页历史（解析产物）。
 * @note  [应答身份] 0x0118 应答的帧头 m_requestId（XLinkProto.h:253）才是
 *        应答的唯一身份；HsfLinkClient 目前只解析载荷、没把 reqId 带进本
 *        结构，页面只能退而按服务端回显的 offset/limit 认领（故页面侧用
 *        HIST_CHART_CHUNK 让各消费者 limit 两两不相交，见 HsfDataHistory.c）。
 *        将来补 reqId 链路时：hsfLcHistoryQuery 已拿到 sendRequest_ex 的
 *        reqId（现在丢弃），hsfLcDispatch 有 frame->m_requestId，加一个
 *        `uint32_t m_reqId` 字段填入即可，页面改成按它比对。
 */
typedef struct HsfHistoryPage {
    bool m_valid;          /**< 已解析过应答。 */
    /** @brief 服务端拒绝文案（空 = 无错误）。
     *  @details [为什么要留痕 2026-11-10] 落盘关闭时服务端回的是**失败包络**
     *           （{"ok":false,"err":"落盘未启用：服务端没有历史数据可查"}），
     *           不是分页文档。此前客户端把它当分页文档解析 ⇒ total=0、
     *           rows 空 ⇒ 界面只显示「共 0 条」并停在「读取中」，现场看着
     *           像「表格数据没了」，看不到真正原因。现在把 err 原文带上来，
     *           页面直接显示这句话。
     */
    char m_error[192];
    uint32_t m_seq;        /**< 应答序号（每次成功解析 +1；导出等"等同一次应答"的场合按它判新）。 */
    int  m_total;          /**< 本窗口内总行数（页码总数按它算）。 */
    int64_t m_dbRows;      /**< 全库行数（"库内共 N 条"；与 Python dbRows 同口径）。 */
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
