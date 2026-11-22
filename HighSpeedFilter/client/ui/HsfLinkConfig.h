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

/* 落盘缺省值走公共协议头（服务端/客户端/Python 三处同值；口径见
 * HSF_STORAGE_SAMPLE_MS_DEF / HSF_STORAGE_MAX_MB_DEF 的注释）。 */
#include "XhsfLinkProto.h"

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
    int  m_turbidity1PollMs;  /**< 进水浊度仪（站1）周期。 */
    int  m_turbidity2PollMs;  /**< 出水浊度仪（站3）周期。 */
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
    /** [入库触发 2026-11-12] always=持续采集入库（缺省）；auto=仅自动运行时入库。 */
    char m_historyTrigger[16];
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

/**
 * @brief 自动模式配置——单模式的设备清单 + 搅拌电机参数（2026-11-11）。
 * @details devices 下标 = HSF_AUTO_DEVICE_COUNT 的稳定位序：0..5=电动球阀
 *          1..6（设备 id "valve1".."valve6"）、6=搅拌泵（"stirPump"）、
 *          7=进水泵（"inletPump"）。与 Python 服务端 auto 配置节
 *          {"devices":[…],"stirRpm":int,"stirReverse":bool} 同键同义。
 */
typedef struct HsfConfigAutoMode {
    bool m_devices[HSF_AUTO_DEVICE_COUNT]; /**< 该模式要启动的设备勾选位。 */
    int  m_stirRpm;        /**< 搅拌电机转速（rpm；0..HSF_AUTO_STIR_RPM_MAX）。 */
    bool m_stirReverse;    /**< 搅拌电机方向（false=正转）。 */
    /** [逐模式阀复位 2026-11-12] 停止时关阀复位（缺省 true）。 */
    bool m_resetValvesOnStop;
    /** [逐模式阀复位 2026-11-12] 急停时关阀复位（缺省 false）。 */
    bool m_resetValvesOnEstop;
} HsfConfigAutoMode;

/**
 * @brief 自动循环（组合模式「自动」）配置（auto 节的 cycle 子节）。
 * @details 与 Python cfg.auto_cycle 同键同义；缺键按协议缺省（见
 *          HSF_AUTO_CYCLE_*_DEF）。
 */
typedef struct HsfConfigAutoCycle {
    bool   m_enabled;
    double m_pressThreshold;  /**< MPa。 */
    char   m_pressCompare[8]; /**< "gt"/"lt"。 */
    bool   m_timerFallback;
    int    m_timerMinutes;
    int    m_backwashMinutes;
    int    m_rinseMinutes;
} HsfConfigAutoCycle;

/**
 * @brief 自动模式配置节（CONFIG GET/SET 的 auto 节；2026-11-11 三模式）。
 * @details m_modes 下标 = 协议稳定顺序：0=produce（产水）1=backwash（反洗）
 *          2=rinse（顺洗）；组合模式 "auto" 的循环参数在应答的 cycle 子节
 *          （2026-11-12 组合循环落地后服务端按循环配置校验并回 cycle 键，
 *          本结构暂不展开它——客户端卡片从应答原样回显）。
 *          [局部应答纪律] m_autoPresent 只在应答**确实带了** auto 节时置位；
 *          没带时 m_modes 是零值（全不勾/0rpm），界面若把它当成「服务端
 *          配置」铺回控件，会把用户没动过的模式块冲回出厂缺省——照
 *          m_storagePresent 的同一口径：没收到就不铺这一节。
 */
typedef struct HsfConfigAuto {
    bool m_autoPresent;    /**< 应答里确实带了 auto 节。 */
    HsfConfigAutoMode m_modes[HSF_AUTO_MODE_COUNT]; /**< 产水/反洗/顺洗。 */
    /** [自动循环 2026-11-12] 循环配置；m_cyclePresent=false 时保持缺省
     *  （老服务端不带 cycle 子节 ⇒ 不得把界面冲成缺省参数）。 */
    bool m_cyclePresent;
    HsfConfigAutoCycle m_cycle;
} HsfConfigAuto;

/**
 * @brief 运行保护配置节（protect；2026-11-15）。
 * @details 进水泵开着、流量计却一直报"空管" ⇒ 到时限就按配置动作
 *          （停设备 / 只报警）。缺键=服务端缺省（60s + 停设备）。
 */
typedef struct HsfConfigProtect {
    int  m_inletNoFlowTimeoutMs; /**< 空管判定时长（毫秒；0=关闭该保护）。 */
    bool m_inletNoFlowStop;      /**< 超时后是否停止设备（true=停）。 */
    /* [压差过大保护 2026-11-15] */
    bool m_pressHighEnabled;     /**< 压差过大保护是否启用（缺省 false）。 */
    double m_pressHighLimitMpa;  /**< 压差上限（MPa；缺省 0.8）。 */
    bool m_pressHighStop;        /**< 触发后是否停止设备（缺省 true）。 */
} HsfConfigProtect;

/* [编译期断言] 本文件数组长度与协议常量绑死；MSVC C 模式不认
 * _Static_assert ⇒ 负数组长度 typedef（不匹配报 C2118/C2148）。 */
typedef char HsfCfgAutoDevCountAssert[(HSF_AUTO_DEVICE_COUNT == 8) ? 1 : -1];
typedef char HsfCfgAutoModeCountAssert[(HSF_AUTO_MODE_COUNT == 3) ? 1 : -1];

/** @brief 服务端配置快照（0x0116 应答的解析产物）。 */
typedef struct HsfConfig {
    bool m_valid;
    HsfConfigCollect m_collect;
    HsfConfigStorage m_storage;
    HsfConfigStats m_stats;
    HsfConfigLogin m_login; /**< 登录策略节。 */
    HsfConfigAuto m_auto;  /**< 自动模式配置节（2026-11-11；见 m_autoPresent 注记）。 */
    HsfConfigProtect m_protect; /**< 运行保护节（2026-11-15）。 */
    /** @brief 应答里**确实带了**运行保护节（protect）。
     *  @details 与 m_loginPresent 同一纪律：局部应答不带 protect ⇒ 界面
     *           不得把保护卡冲回缺省（否则点一次别的「应用」就把保护
     *           悄悄改了）。
     */
    bool m_protectPresent;
    char m_dbPath[256];    /**< 实际库文件路径。 */
    /** @brief 应答里**确实带了**落盘节（storage）。
     *  @details [为什么要有这个标志 2026-11-10] 没有这一节时 m_storage 的
     *           字段全是结构体零值（m_enabled=false）——若界面把它当成
     *           「服务端说落盘关闭」并写回去，就会**静默关掉落盘** ⇒ 历史
     *           表格从此空（现场"表格数据咋没了"）。缺省 ≠ 已关闭：
     *           没收到就不写这一节（见 HsfSettingsRemote.c 的应用路径）。
     */
    bool m_storagePresent;
    /**
     * @brief 应答里**确实带了**采集节（collect）。
     *  @details [为什么要有这个标志 2026-11-12] 与 m_storagePresent 同一
     *           纪律：局部应答（应用存储/登录策略）只带自己那一节，解析
     *           层整结构清零 ⇒ 没带 collect 时 m_collect 全是零值。界面若
     *           把它当「服务端说采集全关/周期 0」铺回控件，用户点一次
     *           「应用登录策略」就会把采集配置卡冲成缺省（且周期 0 可能
     *           被下一次应用写回服务端）。没收到就不铺这一节。
     */
    bool m_collectPresent;
    /**
     * @brief 应答里**确实带了**登录策略节（login）。
     *  @details 同上：应用采集/落盘配置的局部应答不带 login ⇒ 不得把
     *           登录策略卡的勾选与会话上限冲回缺省。
     */
    bool m_loginPresent;
    /**
     * @brief 应答里**确实带了**存储侧统计信息（storageStats 与
     *        storageDbPath 之一即可）。
     *  @details [为什么与 m_storagePresent 分开] storage 节与统计节是
     *           两回事：服务端回「落盘配置」不带统计（用量/库路径），
     *           回「存储统计」时才带 dbPath+storageStats。Python 端
     *           同口径（`if "storageDbPath" in doc or "storageStats" in doc`）
     *           ——分开的守卫才能做到「点一次登录策略，用量行不被清空」。
     */
    bool m_statsPresent;
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
