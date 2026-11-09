#ifndef HSFSERVERSTORE_H
#define HSFSERVERSTORE_H

#ifdef __cplusplus
extern "C" {
#endif

#include "CXinYueConfig.h"
#include <stdbool.h>
#include <stdint.h>

#include "XObject.h"

#if XSQL_ON

/**
 * @file       HsfServerStore.h
 * @brief      服务端 SQLite 落盘层：历史采样 / 告警事件 / 运行日志 三张表。
 * @details    对齐 Python 侧 `hsfsrv/store.py`（三表合一文件、失败不致命、
 *             WAL 写读不互锁）。本层只负责「把一行行数据写进库」；**采样
 *             内容的拼装在 HsfServerApp**（与推送同源——同一批设备对象、
 *             同一个 tick，保证「库里一行 = 界面一拍」可互相印证）。
 *
 *             [SQL 拼装口径] 本层数值列一律 NULL/定点数、字符串列全部来自
 *             服务端自身（设备名/模式文案/告警文字），无任何外部输入，
 *             故用字面量拼装 + 单引号加倍转义，不走过绑定期——少一层
 *             占位符风格（? 与 :name）的驱动差异面。
 *
 *             [logs 表与 Python 差异] Python store.py 的 logs 表是
 *             (ts, text) 两列；C 端为日志页的类别筛选加了 category 列
 *             （(ts, category, text) 三列，类别入库口径见 insertLog）。
 *             库文件两端各自持有（不互开对方的库），列差异互不影响。
 *
 *             线程模型：单线程事件循环；全部 API 须在 XCoreApplication
 *             事件循环内调用。
 */

XCLASS_DEFINE_BEGING(HsfServerStore)
XCLASS_DEFINE_EXTEND_END(HsfServerStore, XObject)

/**
 * @brief 落盘层运行态。
 * @details 成员禁止直接访问，一律经 API 操作。库句柄经 XSqlDatabase 注册表
 *          管理（连接名 hsf_store），close 只关不反注册（重复 open 复用）。
 */
typedef struct HsfServerStore {
    XObject m_class;        /**< 第一成员：基类 XObject。 */
    bool m_enabled;         /**< 是否启用（configure 结果）。 */
    int m_sampleMs;         /**< 历史采样周期（毫秒，≥1000）。 */
    struct XSqlDatabase* m_db;  /**< 库连接（借用；NULL=未打开）。 */
    struct XString* m_path;     /**< 库文件路径（拥有）。 */
    uint64_t m_lastSampleMs;    /**< 上次采样时刻（单调毫秒；0=未采）。 */
    bool m_openLogged;          /**< 打开日志只打一次。 */
} HsfServerStore;

/** @brief 库文件缺省名（落 cfg/ 数据目录，与 Python DEFAULT_DB_NAME 同值）。 */
#define HSF_STORE_DB_NAME       "hsf_data.db"
/** @brief 历史采样周期缺省（毫秒；与 Python StorageConfig.sample_ms 同值）。 */
#define HSF_STORE_SAMPLE_MS     5000

/**
 * @brief 一行历史快照（可空列 = 不可用，入库记 NULL）。
 * @details 可空口径与界面 "--" 一致：设备离线/数据无效的列写 NULL
 *          （NULL 是"没数据"，不是 0）。
 */
typedef struct HsfHistoryRow {
    int64_t tsMs;            /**< 采样时刻（**墙钟** epoch 毫秒）。 */
    const char* mode;        /**< 模式文案（UTF-8）：手动/自动·运行中/自动·停止。 */
    const char* vfdState;    /**< 变频器状态文案（UTF-8）；NULL=离线。 */
    double freqHz;           /**< 输出频率（Hz）。 */
    bool freqHzValid;
    double currentA;         /**< 输出电流（A）。 */
    bool currentAValid;
    double rpm;              /**< 搅拌转速（换算值，额定 50Hz↔1440rpm）。 */
    bool rpmValid;
    double turb1;            /**< 浊度仪1（NTU）。 */
    bool turb1Valid;
    double turb2;            /**< 浊度仪2（NTU）。 */
    bool turb2Valid;
    double flow;             /**< 瞬时流量（随流量计单位）。 */
    bool flowValid;
    const char* flowUnit;    /**< 瞬时流量单位文案；NULL=无效。 */
    double flowTotal;        /**< 正向累积量。 */
    bool flowTotalValid;
    const char* totalUnit;   /**< 累积单位文案；NULL=无效。 */
    double pressMpa;         /**< 压差（MPa，通道 0 换算）。 */
    bool pressValid;
    const char* pressNote;   /**< 压差越限说明；NULL/空=正常。 */
    double temp;             /**< 电箱温度（℃）。 */
    bool tempValid;
    double hum;              /**< 电箱湿度（%RH）。 */
    bool humValid;
    uint32_t di;             /**< DI 位图（位宽随实际点数；聚合时最多 32 点）。 */
    bool diValid;
    uint32_t doMask;         /**< DO 位图。 */
    bool doValid;
} HsfHistoryRow;

/**
 * @brief 历史表的列数（与 Python store.HISTORY_COLUMNS 同序同数）。
 * @details 列序：ts, mode, vfd_state, freq_hz, current_a, rpm, turb1,
 *          turb2, flow, flow_unit, flow_total, total_unit, press_mpa,
 *          press_note, temp, hum, di, do。
 */
#define HSF_HISTORY_COLUMNS     18

/**
 * @brief 历史页的一格（原始值；NULL 与"空串"是两件事）。
 * @details [为什么单独带 isNull] 空串是"有这个值、值就是空"，NULL 是
 *          "这一列没数据"——界面把后者显示成 "--"。协议里也必须是 BSON
 *          null（Python 侧按 None 判 "--"），发空串会让两端显示不一致。
 */
typedef struct HsfHistoryCell {
    char m_text[48];        /**< 值的文本（UTF-8；数值按原样格式化）。 */
    bool m_isNull;          /**< true=该列为 NULL（没数据）。 */
} HsfHistoryCell;

/** @brief 历史页的一行（HSF_HISTORY_COLUMNS 格）。 */
typedef struct HsfHistoryQueryRow {
    HsfHistoryCell m_cells[HSF_HISTORY_COLUMNS];
} HsfHistoryQueryRow;

/**
 * @brief 取一页历史（**时间正序**，与 Python store.query_history_page 同口径）。
 * @param st 落盘层。
 * @param offset 跳过多少行（按 ts 倒序数；0=最新那行）。
 * @param limit 本页最多多少行。
 * @param loMs 时间下界（epoch 毫秒；<=0=不限）。
 * @param hiMs 时间上界（epoch 毫秒；<=0=不限）。
 * @param kw 关键字（utf8；NULL/空=不筛）。**下推服务端**，与 Python
 *           store._keyword_condition 同口径（按显示列文本 LIKE 匹配）。
 * @param kwCols 要比对的显示列下标数组（0..14）；NULL/0 项=全部列。
 * @param kwColCount 下标数组长度。
 * @param outRows 行输出缓冲（调用方提供；容量 outMaxRows）。
 * @param outMaxRows 行缓冲容量（行数）。
 * @param outWindowTotal 本窗口内总行数（页码总数按它算，不是全库行数）。
 * @param outDbRows 全库行数（可 NULL）。
 * @param outLatestTs 最新有效墙钟毫秒（可 NULL）。
 * @param outEarliestTs 最早有效墙钟毫秒（可 NULL）。
 * @return 实际写入的行数（<=0=无数据/未开库）。
 * @details [窗口锚点] 分页窗口由调用方给的 hiMs 固定 ⇒ 库在持续入库
 *          （5s 一行）时，翻到第 2 页也不会被新行顶偏（offset 整体偏移
 *          ⇒ 相邻两页出现重复行）。
 *          [有效时间闸] 只统计 ts >= 1577836800000（2020-01-01）的行：
 *          旧版本误把 monotonic 当墙钟写进来（实测入库 1970 年），
 *          直接 MIN(ts) 会把"最前一条"报成 1970-01-01。
 */
int HsfServerStore_queryHistory(HsfServerStore* st, int offset, int limit,
                                int64_t loMs, int64_t hiMs,
                                const char* kw,
                                const uint32_t* kwCols, int kwColCount,
                                HsfHistoryQueryRow* outRows, int outMaxRows,
                                int* outWindowTotal, int64_t* outDbRows,
                                int64_t* outLatestTs, int64_t* outEarliestTs);

/**
 * @brief 初始化落盘层（栈上构造；未配置、未打开）。
 */
void HsfServerStore_init(HsfServerStore* st);

/**
 * @brief 清空（幂等；先关库）。
 */
void HsfServerStore_clear(HsfServerStore* st);

/**
 * @brief 从配置装配（INI 键 storage/enabled、storage/sample_ms、storage/path）。
 * @param st 目标对象。
 * @param baseDir 数据目录（UTF-8；path 为相对路径时以它为基准，缺省库文件
 *                名落这里）。
 * @return 配置后是否启用（enabled=false 时 open/tick/insert 均为空操作）。
 */
bool HsfServerStore_configure(HsfServerStore* st, const char* baseDir);

/**
 * @brief 打开库并建表（幂等；失败不致命——服务照跑，只是不落盘）。
 * @details 打开即执行 PRAGMA journal_mode=WAL + 三张 CREATE TABLE IF NOT
 *          EXISTS（与 Python _SCHEMA 逐列一致）。
 */
bool HsfServerStore_open(HsfServerStore* st);

/**
 * @brief 关库（幂等）。
 */
void HsfServerStore_close(HsfServerStore* st);

/**
 * @brief 库是否已打开（可写入）。
 */
bool HsfServerStore_isOpen(const HsfServerStore* st);

/**
 * @brief 是否启用（configure 结果）。
 */
bool HsfServerStore_enabled(const HsfServerStore* st);

/**
 * @brief 采样周期（毫秒；配置值，<1000 夹到 1000）。
 */
int HsfServerStore_sampleMs(const HsfServerStore* st);

/**
 * @brief 实际库文件路径（借用指针；未开库返回空串）。
 * @details 远端配置的 storageDbPath 用（客户端显示"数据落在哪"）。
 */
const char* HsfServerStore_dbPath(const HsfServerStore* st);

/**
 * @brief 库文件占用字节数（含 WAL；未开库返回 0）。
 * @details 远端配置的 storageStats 用（容量进度条/到限提示）。
 */
int64_t HsfServerStore_usedBytes(const HsfServerStore* st);

/**
 * @brief history 表的行数（未开库返回 0）。
 * @details 远端配置的 storageStats 用（"库里有多少条"）。
 */
int64_t HsfServerStore_rowCount(const HsfServerStore* st);

/**
 * @brief 写一行历史（ts 为主键，同毫秒重写覆盖）。
 * @return 写入成功返回 true；未启用/未打开/SQL 失败返回 false（不抛错）。
 */
bool HsfServerStore_insertHistory(HsfServerStore* st, const HsfHistoryRow* row);

/**
 * @brief 写一条告警事件（kind: "valve"/"auto"/…；与 Python insert_alarm 同表）。
 */
bool HsfServerStore_insertAlarm(HsfServerStore* st, const char* kind,
                                const char* source, const char* text);

/* ==================== 运行日志（logs 表；HSF_CMD_LOG_PAGE 数据源） ==================== */

/**
 * @brief 运行日志一行（查询输出；text 截断到缓冲容量，不入库截断在调用方）。
 * @details category 入库口径（HsfServerApp 关键事件定点入库）：
 *            conn  —— 会话接入/断开；
 *            ctrl  —— 控制命令受理/拒绝（数字量写、自动模式拒控制）；
 *            alarm —— 阀告警等设备级异常事件；
 *            auto  —— 自动运行启停/急停/复位、运行模式切换；
 *            auth  —— 登录成功/被拒。
 *          日常周期性日志（遥测、推送、采样）**不**入库——logs 表只收
 *          运维要翻的事件，防止 5s 一行把分页冲成流水账。
 */
typedef struct HsfLogRow {
    int64_t m_ts;            /**< 事件时刻（**墙钟** epoch 毫秒）。 */
    char m_category[16];     /**< 类别（utf8；上表口径，空=旧无类别行）。 */
    char m_text[256];        /**< 事件文本（utf8）。 */
} HsfLogRow;

/**
 * @brief 写一条运行日志（logs 表；未开库=空操作返回 false，不抛错）。
 * @param st 落盘层。
 * @param tsMs 事件时刻（墙钟 epoch 毫秒；<=0 时取当前墙钟）。
 * @param category 类别（utf8；可 NULL=空串）。
 * @param text 事件文本（utf8；可 NULL=空串；超长按缓冲容量截断）。
 * @return 写入成功返回 true。
 */
bool HsfServerStore_insertLog(HsfServerStore* st, int64_t tsMs,
                              const char* category, const char* text);

/**
 * @brief 取一页运行日志（**时间正序**输出，与 queryHistory 同口径）。
 * @param st 落盘层。
 * @param offset 跳过多少行（按 ts 倒序数；0=最新那条）。
 * @param limit 本页最多多少行（<=0 夹到 1）。
 * @param loMs 时间下界（epoch 毫秒；<=0=不限）。
 * @param hiMs 时间上界（epoch 毫秒；<=0=不限；同 queryHistory 只认
 *             >= HSF_STORE_VALID_TS_MIN 的有效墙钟）。
 * @param category 类别过滤（utf8 精确匹配；NULL/空=全部）。
 * @param kw 关键字（utf8；NULL/空=不筛）。**下推服务端**：text 与
 *           category 列拼接后 LIKE 匹配、ESCAPE 转义（口径照本文件
 *           storeBuildKwCondition——总数与页内容共用同一条 WHERE）。
 * @param outRows 行输出缓冲（调用方提供；容量 outMaxRows）。
 * @param outMaxRows 行缓冲容量（行数）。
 * @param outTotal 本窗口（时间+类别+关键字过滤后）命中总行数（可 NULL）。
 * @return 实际写入的行数（<=0=无数据/未开库）。
 * @details [窗口锚点] 与 queryHistory 同理：分页窗口由调用方给的 hiMs
 *          固定，翻页不被新入库行顶偏（logs 表同样在持续入库）。
 */
int HsfServerStore_queryLogs(HsfServerStore* st, int offset, int limit,
                             int64_t loMs, int64_t hiMs,
                             const char* category, const char* kw,
                             HsfLogRow* outRows, int outMaxRows,
                             int* outTotal);

#endif /* XSQL_ON */

#ifdef __cplusplus
}
#endif
#endif /* HSFSERVERSTORE_H */
