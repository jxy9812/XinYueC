/**
 * @file       HsfAlarmLogic.h
 * @brief      数据-报警页的**纯逻辑**助手：行可见判定 + 客户端内部分页。
 *
 * @details [为什么单独成文件]
 *           报警页 2026-11-11 起与 Python 客户端 AlarmPage 同口径升级：
 *           去「报警次数/分钟」折线图（用户定案：日志和报警都不要图表）、
 *           加关键字筛选（设备/类型/状态三列包含匹配）、加客户端内部分页
 *           （10/20/50，缺省 20）、加滚动锁定（锁定期新报警只入库不刷表）。
 *           「一行算不算命中」「全集能分几页」「第 x 页从全集第几行开始」
 *           是契约级判定：页面、导出、统计三处共用，算错一处就是「筛
 *           不出/翻页越界/统计与表格对不上」。照 HsfExportPlan 的既定
 *           范式抽成不依赖 GUI 的纯函数，回归锁
 *           （Test/XProtocolTest/hsf_regression_test.c 报警逻辑节）单 TU
 *           收录逐条钉住。口径来源 = Python 客户端
 *           pages_other.py 的 AlarmPage._row_visible / _page_count /
 *           _rebuild（含关键字匹配列、页号夹取与「至少 1 页」）。
 *
 * @note  本文件不碰控件与网络，只做文本比较与整数算术 ⇒ 可在无 GUI 的
 *        测试宿主里直接调用，且零堆分配（无泄漏面）。助手本体只随
 *        HsfClient 编译（不进任何库），测试单 TU 收录不会与库内符号撞键。
 */
#ifndef HSFALARMLOGIC_H
#define HSFALARMLOGIC_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/** @brief 报警页的分页档位（与历史/日志页同款 10/20/50）。 */
#define HSF_ALARM_PAGE_SIZES_COUNT 3

/**
 * @brief 一条报警记录的筛选条件（对齐 Python AlarmPage 的筛选状态）。
 * @details 文本字段都是**借用**（调用方保证调用期间有效；NULL/空串 =
 *          该条件不限）。
 */
typedef struct HsfAlarmFilter {
    const char* m_kindText; /**< 类型筛选文本（全等匹配；NULL/空=全部类型）。 */
    int64_t     m_loTs;     /**< 时间窗下界（epoch 毫秒；<=0 不限）。 */
    int64_t     m_hiTs;     /**< 时间窗上界（epoch 毫秒；<=0 不限）。 */
    const char* m_kw;       /**< 关键字（NULL/空=不筛；设备/类型/状态包含）。 */
} HsfAlarmFilter;

/**
 * @brief 判断一条报警记录是否落在当前筛选条件内。
 * @param filter   筛选条件（NULL = 全部条件不限 ⇒ 恒可见）。
 * @param ts       记录时刻（epoch 毫秒；<=0 视为无时刻 ⇒ 时间窗不淘汰它）。
 * @param kindText 记录的类型文本（类型列；NULL 按空串参与关键字匹配）。
 * @param name     记录的设备名（描述列；NULL 按空串参与关键字匹配）。
 * @param state    记录的状态文本（状态列；NULL 按空串参与关键字匹配）。
 * @return 命中返回 true。
 * @details [判定顺序与 Python _row_visible 逐条同构]
 *          1. 类型：filter->m_kindText 非空时与记录类型**全等**（strcmp；
 *             下拉的"全部类型"在页面层已折成 NULL/空串，不进本函数）；
 *          2. 时间窗：界 >0 才生效；记录 ts<=0（无时刻）不参与时间窗淘汰
 *             （与 Python 同口径：正数下界拦不住 0 时刻，上界同理）；
 *          3. 关键字：非空时在**设备名/类型/状态**三列文本里做"包含"
 *             匹配（strstr 子串），任一列包含即命中；**不含时间列**
 *             （Python 口径：匹配列是 1/2/3，不是 0）。
 */
bool HsfAlarmLogic_rowVisible(const HsfAlarmFilter* filter, int64_t ts,
                              const char* kindText, const char* name,
                              const char* state);

/**
 * @brief 筛选命中全集按当前页大小能分几页（至少 1 页）。
 * @param totalRows 筛选命中的全集行数（非当前页行数）。
 * @param pageSize  页大小（<=0 按 1 处理防除零 ⇒ 恒 1 页）。
 * @return 页数 = max(1, ceil(totalRows/pageSize))；0 行也是 1 页（空表
 *         仍要显示「第 1/1 页」），与 Python _page_count 同口径。
 */
int HsfAlarmLogic_pageCount(int totalRows, int pageSize);

/**
 * @brief 第 pageIndex 页在「全集倒序（最新在前）视图」里的起始行号。
 * @param totalRows 筛选命中的全集行数。
 * @param pageSize  页大小（<=0 按 1 处理）。
 * @param pageIndex 请求页号（0 基；越界自动夹取）。
 * @return 起始行号（0 基，向下取整到 pageSize 网格；空全集恒 0）。
 * @details [夹取口径对齐 Python _rebuild] 页号 >= 页数 ⇒ 用最后一页；
 *          页号 < 0 ⇒ 按第 0 页。调用方在筛选/解锁后行数变少时不用自己
 *          先夹页号——本函数返回的起始行号永远不会越过全集末尾。
 */
int HsfAlarmLogic_pageStart(int totalRows, int pageSize, int pageIndex);

#ifdef __cplusplus
}
#endif

#endif /* HSFALARMLOGIC_H */
