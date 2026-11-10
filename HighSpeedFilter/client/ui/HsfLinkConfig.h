/**
 * @file       HsfLinkConfig.h
 * @brief      远端配置（0x0116 读 / 0x0117 写）的客户端数据面。
 * @details    与 Python 客户端同接口同键名（proto.CFGK_*）：服务端把
 *             采集节（各设备开关 + 轮询周期）与落盘节（开关/路径/采样
 *             周期/上限/到限策略/保留份数）连同容量实况一起下发，客户端
 *             只负责显示与提交修改。
 */
#ifndef HSF_LINK_CONFIG_H
#define HSF_LINK_CONFIG_H

#ifdef __cplusplus
extern "C" {
#endif

#include "CXinYueConfig.h"
#include <stdbool.h>
#include <stdint.h>

/** @brief 采集节（各设备开关 + 轮询周期）。 */
typedef struct HsfConfigCollect {
    bool m_vfdEnabled;
    bool m_temphumEnabled;
    bool m_flowmeterEnabled;
    bool m_turbidityEnabled;
    /** [逐台开关 2026-11-10] 两台浊度仪是两台独立设备（站1/站3），各设各的；
     *  m_turbidityEnabled 只作**共同缺省**（旧服务端/旧配置无逐台键时用它）。 */
    bool m_turbidity1Enabled;
    bool m_turbidity2Enabled;
    bool m_analogEnabled;
    bool m_digitalEnabled;
    int  m_vfdPollMs;
    int  m_temphumPollMs;
    int  m_turbidityPollMs;   /**< 两台浊度的缺省（兼容键）。 */
    int  m_turbidity1PollMs;  /**< 浊度仪1（站1）周期。 */
    int  m_turbidity2PollMs;  /**< 浊度仪2（站3）周期。 */
    int  m_flowmeterPollMs;   /**< 流量计（站5）周期。 */
    int  m_analogPollMs;
    int  m_digitalPollMs;
} HsfConfigCollect;

/** @brief 落盘节。 */
typedef struct HsfConfigStorage {
    bool m_enabled;
    char m_path[256];      /**< 配置值（空=程序目录）。 */
    int  m_sampleMs;       /**< 采样周期（毫秒）。 */
    int  m_maxMb;          /**< 上限（MB；0=不限）。 */
    char m_fullPolicy[32]; /**< 到限策略。 */
    int  m_keepFiles;      /**< 保留份数（rotate 时有效）。 */
} HsfConfigStorage;

/** @brief 容量实况。 */
typedef struct HsfConfigStats {
    int64_t m_usedBytes;
    int64_t m_contentBytes;
    double  m_usedMb;        /**< 已用（MB；服务端直给，缺省由字节换算）。 */
    int  m_maxMb;
    int  m_rows;
    int  m_segments;
    int  m_droppedRows;
    bool m_paused;           /**< 已停写（到限策略=stop 且已满）。 */
    char m_policy[32];       /**< 生效策略（ring/stop/rotate；与 Python 同词）。 */
    char m_pauseReason[128]; /**< 停写原因（paused=true 时显示）。 */
} HsfConfigStats;

/** @brief 登录/账户策略节（2026-11-08；写入需管理员 AccountAdmin）。 */
typedef struct HsfConfigLogin {
    bool m_allowAnonymous;   /**< 允许匿名（只读）会话接入。 */
    bool m_allowConcurrent;  /**< 允许同一账户并发在线（关=root 单会话）。 */
    int  m_maxSessions;      /**< 会话总数上限（0=不限）。 */
} HsfConfigLogin;

/** @brief 服务端配置快照（0x0116 应答的解析产物）。 */
typedef struct HsfConfig {
    bool m_valid;
    HsfConfigCollect m_collect;
    HsfConfigStorage m_storage;
    HsfConfigStats m_stats;
    HsfConfigLogin m_login; /**< 登录策略节。 */
    char m_dbPath[256];    /**< 实际库文件路径。 */
    /** @brief 应答里**确实带了**落盘节（storage）。
     *  @details [为什么要有这个标志 2026-11-10] 没有这一节时 m_storage 的
     *           字段全是结构体零值（m_enabled=false）——若界面把它当成
     *           「服务端说落盘关闭」并写回去，就会**静默关掉落盘** ⇒ 历史
     *           表格从此空（现场"表格数据咋没了"）。缺省 ≠ 已关闭：
     *           没收到就不写这一节（见 HsfSettingsRemote.c 的应用路径）。
     */
    bool m_storagePresent;
} HsfConfig;

/**
 * @brief 解析 0x0116 应答（BSON）到配置快照。
 * @param data BSON 字节；len 长度。
 * @param out  输出（调用方提供；内部先清零）。
 * @return 解析到有效应答返回 true。
 */
bool HsfConfig_parse(const uint8_t* data, uint32_t len, HsfConfig* out);

#ifdef __cplusplus
}
#endif

#endif /* HSF_LINK_CONFIG_H */
