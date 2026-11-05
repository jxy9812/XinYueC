/**
 * @file       HsfServerIoPump.h
 * @brief      服务端进水泵组：数字量模块上的一路 DO 输出（只有输出，无反馈）。
 * @details    与 Python 侧 `hsfsrv/pumps.py` 的 `PumpBank` 同口径：
 *               · 泵**没有行程反馈**——现场只控制接触器，写了就是命令；
 *               · 状态 = 命令状态（最后一次写的值），经 DO 回显校准；
 *               · **不判到位、不设超时定时器、无告警**——没有反馈就没有
 *                 可比对的真相源，硬造"超时未到位"等于撒谎（要真正监视泵，
 *                 得现场加运行反馈点，那时再扩展本类）；
 *               · 点位全部来自配置（哪个 DO），未配置的泵显示"未配置"
 *                 且控制被拒；
 *               · 控制幂等：已是该状态不重复写（避免无谓的继电器动作
 *                 与总线占用，与阀 ignored 同一口径）。
 *
 *             [外部改动接管] DO 回显是**唯一**能发现"这个口被别人改过"的
 *             手段（数字量页、现场 HMI、自动逻辑都能绕过本类）。poll 发现
 *             回显 ≠ knownDo 时**以实际输出为准**并留痕——界面显示一个与
 *             实际相反的状态是最危险的。
 *
 *             线程模型：单线程事件循环；全部 API 须在 XCoreApplication
 *             事件循环内调用。
 */
#ifndef HSFSERVERIOPUMP_H
#define HSFSERVERIOPUMP_H

#ifdef __cplusplus
extern "C" {
#endif

#include "CXinYueConfig.h"
#include <stdbool.h>
#include <stdint.h>

#include "XObject.h"
#include "XhsfLinkProto.h"   /* HSF_PUMP_COUNT。 */

/* ==================== 虚函数表定义（继承 XObject，不新增虚槽） ==================== */
XCLASS_DEFINE_BEGING(HsfServerIoPump)
XCLASS_DEFINE_EXTEND_END(HsfServerIoPump, XObject)

/**
 * @brief 单台进水泵运行态。
 * @details 成员禁止直接访问，一律经 API 操作。
 */
typedef struct HsfServerIoPumpItem {
    char m_name[48];        /**< 显示名（UTF-8）。 */
    int  m_do;              /**< 输出 DO 通道号；-1=未配置。 */
    bool m_on;              /**< 命令状态（true=运行/接触器吸合）。**无反馈信号**。 */
    bool m_knownDo;         /**< 我们认为的 DO 实际位置（外部改动检测基准）。 */
    bool m_knownDoSeeded;   /**< 是否已用真实 DO 回显校准过（首拍校准标志）。 */
    bool m_valid;           /**< 数据有效（回显读到且新鲜）。 */
    bool m_online;          /**< 回显源模块本拍在线且数据新鲜。 */
    char m_text[96];        /**< 状态说明（"未配置点位"/"模块离线"等，可空）。 */
} HsfServerIoPumpItem;

/**
 * @brief 进水泵组运行态（本模块拥有其内部资源）。
 * @details 成员禁止直接访问，一律经 API 操作。
 */
typedef struct HsfServerIoPump {
    XObject m_class;                     /**< 第一成员：基类 XObject。 */
    HsfServerIoPumpItem m_items[HSF_PUMP_COUNT]; /**< 各泵运行态。 */
    int  m_count;                        /**< 实际台数（0..HSF_PUMP_COUNT）。 */
    bool m_enabled;                      /**< 是否启用。 */
    bool m_ready;                        /**< 至少一台配好点位。 */
} HsfServerIoPump;

/* ==================== 类初始化 / 构造与析构 ==================== */

/**
 * @brief 初始化 HsfServerIoPump 类的虚函数表（单例）。
 * @return 类虚拟函数表指针；不可释放。
 */
XVtable* HsfServerIoPump_class_init(void);

/**
 * @brief 栈上初始化进水泵组（空配置，未启用）。
 * @param st 目标对象；不能为 NULL，可为未初始化内存（内部先清零）。
 */
void HsfServerIoPump_init(HsfServerIoPump* st);

/**
 * @brief 单台点位装配入参（与配置层解耦：配置结构体由调用方转换后传入）。
 */
typedef struct HsfServerIoPumpCfg {
    const char* m_name;   /**< 显示名（UTF-8；借用，内部深拷贝；NULL=缺省名）。 */
    int m_do;             /**< 输出 DO 通道号；-1=未配置。 */
} HsfServerIoPumpCfg;

/**
 * @brief 按配置装配进水泵组（不触碰硬件；DO 翻译由调用方负责）。
 * @param st 目标泵组。
 * @param items 点位配置数组（借用，本函数深拷贝名字）。
 * @param count 台数（0..HSF_PUMP_COUNT）。
 * @param enabled 是否启用。
 * @return 装配成功（含"全部未配置"这种合法情形）返回 true。
 */
bool HsfServerIoPump_configure(HsfServerIoPump* st,
                               const HsfServerIoPumpCfg* items, int count,
                               bool enabled);

/**
 * @brief 清空（幂等）。
 */
void HsfServerIoPump_clear(HsfServerIoPump* st);

/* ==================== 动作 ==================== */

/**
 * @brief 下发开关命令（校验 + 意图记账；真正出帧由调用方经数字量站完成）。
 * @param st 目标泵组。
 * @param index 泵号（0..count-1）。
 * @param on true=启动（接触器吸合）、false=停止。
 * @param outErr 失败原因输出（UTF-8，容量 cap；可为 NULL）。
 * @param cap 失败原因缓冲容量。
 * @return 允许下发返回 true；未配置/越界/未启用/模块离线返回 false。
 * @note  [幂等] 已是该状态（valid 且 on==目标）返回 true 且**不置待写标记**
 *        ——调用方据此跳过 DO 写（避免无谓的继电器动作与总线占用）。
 *        本层是纯业务层不查时钟，写后状态当场落定（泵没有"在途"过程）。
 */
bool HsfServerIoPump_setOutput(HsfServerIoPump* st, int index, bool on,
                               char* outErr, int cap);

/* ==================== 反馈更新（DO 回显校准 / 外部改动接管） ==================== */

/**
 * @brief 用一帧 DO 回显刷新全部泵的命令状态与可用性。
 * @param st 目标泵组。
 * @param doMask DO 位图（bit n = 通道 n）。
 * @param doValid DO 数据是否有效（false ⇒ 全部 valid=false）。
 * @param doFresh DO 回显是否新鲜（链路在线且未过期）。
 * @details [首拍校准] 模块还没采过一帧时回显不可信：只标 valid，不校准
 *          也不判改动（与 Python known_do_seeded 同口径）。
 *          [外部改动] 校准后发现回显 ≠ knownDo ⇒ 以回显为准覆盖 on 并留
 *          文本（调用方打日志——本层不查时钟不打印，保持纯业务层）。
 */
void HsfServerIoPump_poll(HsfServerIoPump* st, uint32_t doMask, bool doValid,
                          bool doFresh);

/* ==================== 查询 ==================== */

/** @brief 台数。 */
int HsfServerIoPump_count(const HsfServerIoPump* st);

/** @brief 至少一台配好点位。 */
bool HsfServerIoPump_ready(const HsfServerIoPump* st);

/**
 * @brief 取某一台只读条目。
 * @return 借用指针；越界返回 NULL。
 */
const HsfServerIoPumpItem* HsfServerIoPump_item(const HsfServerIoPump* st,
                                                int index);

#ifdef __cplusplus
}
#endif
#endif /* HSFSERVERIOPUMP_H */
