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
    bool m_analogEnabled;
    bool m_digitalEnabled;
    int  m_vfdPollMs;
    int  m_temphumPollMs;
    int  m_turbidityPollMs;
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
    int  m_maxMb;
    int  m_rows;
    int  m_segments;
    int  m_droppedRows;
    bool m_paused;
} HsfConfigStats;

/** @brief 服务端配置快照（0x0116 应答的解析产物）。 */
typedef struct HsfConfig {
    bool m_valid;
    HsfConfigCollect m_collect;
    HsfConfigStorage m_storage;
    HsfConfigStats m_stats;
    char m_dbPath[256];    /**< 实际库文件路径。 */
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
