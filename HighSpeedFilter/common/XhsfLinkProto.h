
/**
 * @file       XhsfLinkProto.h
 * @brief      高速过滤器 XLink 承载协议约定（服务端/客户端共用，无代码）。
 * @details    载荷统一走 XLinkDataType_Bson（XBsonDocument 文档字节），
 *             业务数据由 Src/Industry 的 XVfdCodec / XSolenoidValveCodec
 *             与具体结构互转（键名/键列表口径见各 Codec 头文件）。
 *             登录鉴权直接使用 XLink 内建挑战应答（XLinkServer_setAuthEnabled
 *             + XLinkClient_setCredential），本头只定义命令号/主题号/角色
 *             策略。常量两端共用，改这里即两端同步。
 *
 *             [消息面]
 *               Request  HSF_CMD_VFD_CONTROL   变频器控制（XVfdControlRequest
 *                                              BSON + side 字段；Response = 应答包络）
 *               Request  HSF_CMD_VFD_SNAPSHOT  请求一拍变频器遥测（载荷 {"side":int}；
 *                                              Response = XVfdStatus BSON）
 *               Request  HSF_CMD_AI_SNAPSHOT   请求一拍模拟量遥测（空载荷；
 *                                              Response = AI 快照 BSON）
 *               Publish  HSF_TOPIC_DEVICE_HEALTH 设备运行状态与错误（**事件驱动**：
 *                                              仅状态变化推送 + 客户端首订阅即补发）
 *               Request  HSF_CMD_DEVICE_HEALTH 拉取当前健康快照（断线重连对账）
 *               Request  HSF_CMD_RUN_MODE      查询/设置运行模式（{"mode":int}；
 *                                              无 mode=查询；设置需 root）
 *               Request  HSF_CMD_TEMPHUM_SNAPSHOT 请求一拍温湿度（载荷 {"side":int}；
 *                                              Response = 温湿度快照 BSON）
 *               Request  HSF_CMD_FLOWMETER_SNAPSHOT 请求一拍电磁流量计
 *                                              （载荷 {"side":int}；Response =
 *                                              流量计快照 BSON）
 *               Publish  HSF_TOPIC_VFD_STATUS  变频器遥测周期推送（XVfdStatus
 *                                              BSON + side 字段，载荷首 4 字节
 *                                              为 topicId —— XLink Publish 线格式）
 *               Publish  HSF_TOPIC_AI_STATUS   模拟量遥测周期推送（AI 快照 BSON）
 *               Publish  HSF_TOPIC_TEMPHUM_STATUS 温湿度遥测周期推送（温湿度快照
 *                                              BSON + side 字段）
 *               Publish  HSF_TOPIC_FLOWMETER_STATUS 电磁流量计遥测周期推送
 *                                              （流量计快照 BSON + side 字段）
 *
 *             [两侧寻址] 现场两路独立 485 各管一侧，侧号 HsfSide（0=A，1=B）
 *             随载荷下发；缺省（无 side 键）按 A 侧处理，兼容单侧老客户端。
 *
 *             [应答包络] Response 载荷统一 {"ok":bool, "err":utf8}；
 *             err 仅在 ok=false 时有意义（失败原因，UTF-8 中文）。
 */
#ifndef XHSFLINKPROTO_H
#define XHSFLINKPROTO_H

#include <stdint.h>

/* ==================== 连接 ==================== */

/** @brief 服务端默认监听端口（XLink TCP）。 */
#define HSF_XLINK_DEFAULT_PORT      6100u

/* ==================== 命令号（msgClass=Request/Response 的 msgCode） ==================== */

/** @brief 变频器控制命令（Request 载荷=XVfdControlRequest BSON；需管理角色）。 */
#define HSF_CMD_VFD_CONTROL         0x0101u

/** @brief 变频器遥测快照请求（Request 载荷={"side":int}；Response=XVfdStatus BSON）。 */
#define HSF_CMD_VFD_SNAPSHOT        0x0102u

/** @brief 模拟量遥测快照请求（Request 空载荷；Response=AI 快照 BSON）。 */
#define HSF_CMD_AI_SNAPSHOT         0x0103u

/** @brief 温湿度遥测快照请求（Request 载荷={"side":int}；Response=温湿度快照 BSON）。 */
#define HSF_CMD_TEMPHUM_SNAPSHOT    0x0104u

/**
 * @brief 运行模式：查询 / 设置（载荷 {"mode":int}；无 mode 键=查询）。
 * @details 设置需 root（HSF_ROLE_CONTROL）。应答：{"ok":bool,"mode":int,"auto":bool}。
 *          自动模式下 HSF_CMD_VFD_CONTROL 一律被拒（err="自动运行中：远程控制被禁用"）。
 */
#define HSF_CMD_RUN_MODE            0x0105u

/**
 * @brief 设备健康：查询当前全量快照（载荷空；应答=健康快照 BSON）。
 * @details 与订阅推送同构，供客户端按需拉取（断线重连后对账用）。
 */
#define HSF_CMD_DEVICE_HEALTH       0x0107u

/** @brief 电磁流量计遥测快照请求（载荷={"side":int}；Response=流量计快照 BSON）。 */
#define HSF_CMD_FLOWMETER_SNAPSHOT  0x0106u

/* ==================== 推送主题（topicId = XLINK_TOPIC_USER_BASE + n） ==================== */

/** @brief 变频器遥测推送主题（载荷=XVfdStatus BSON + side，周期见服务端配置）。 */
#define HSF_TOPIC_VFD_STATUS        0x00010001u

/** @brief 模拟量遥测推送主题（载荷=AI 快照 BSON）。 */
#define HSF_TOPIC_AI_STATUS         0x00010002u

/** @brief 温湿度遥测推送主题（载荷=温湿度快照 BSON + side）。 */
#define HSF_TOPIC_TEMPHUM_STATUS    0x00010003u

/** @brief 电磁流量计遥测推送主题（载荷=流量计快照 BSON + side）。 */
#define HSF_TOPIC_FLOWMETER_STATUS  0x00010004u

/**
 * @brief 设备运行状态与错误主题（**事件驱动**：仅状态变化时推送）。
 * @details 与其它主题不同：本主题不是周期推送，而是
 *            - 任一设备健康状态**发生变化**时推送；
 *            - 客户端**首次订阅**时立即补发一份当前全量快照（免等待）。
 *          载荷见 HsfDeviceHealthBundle 的键表（devCount/各设备条目）。
 */
#define HSF_TOPIC_DEVICE_HEALTH     0x00010005u

/* ==================== 侧号（两路 485 各管一侧） ==================== */

/** @brief A 侧（路 A 总线）。 */
#define HSF_SIDE_A                  0
/** @brief B 侧（路 B 总线，预留）。 */
#define HSF_SIDE_B                  1

/** @brief 侧号载荷键名（int；缺省按 A 侧）。 */
#define HSF_KEY_SIDE                "side"

/* ==================== 角色策略（XLinkAuthRole） ==================== */

/**
 * @brief 控制类命令所需最低角色：管理员（root）。
 * @details admin（XLinkAuthRole_User）只读：可订阅遥测、可请求快照；
 *          root（XLinkAuthRole_Admin）可下发控制。服务端在分发回调里
 *          以 XLinkServer_sessionRole 强制执行。
 */
#define HSF_ROLE_CONTROL            2u   /* XLinkAuthRole_Admin */

/* ==================== 缺省账户（首次启动自动创建） ==================== */

/** @brief 缺省只读账户（查看权限）。 */
#define HSF_DEFAULT_USER_NAME       "admin"
#define HSF_DEFAULT_USER_PASS       "admin"

/** @brief 缺省管理账户（查看+修改权限）。 */
#define HSF_DEFAULT_ROOT_NAME       "root"
#define HSF_DEFAULT_ROOT_PASS       "root"

/* ==================== 点动手持心跳 ==================== */

/**
 * @brief 点动心跳时序（服务端看门狗 + 客户端重发）。
 * @details 点动是**手持式**操作：客户端按住按钮期间周期性重发点动帧，
 *          服务端在帧间隔内保持运行，超时立即停机——链路断开/客户端崩溃
 *          时设备不会失控转动。
 *
 *          [取值依据]
 *            客户端重发 HSF_JOG_REPEAT_MS = 200ms
 *              人感知阈值 ~200ms，按住即转、松手即停的观感要求重发足够密。
 *            服务端超时 HSF_JOG_TIMEOUT_MS = 600ms
 *              = 3 × 重发间隔：容忍连续丢 2 帧仍不断机（现场 485 偶发丢帧），
 *              又不至于人松手后还转很久。
 *          [为什么不能更短] 超时 < 2×重发间隔时，单帧丢失就会误停机，
 *              表现为"按住时一顿一顿"。
 *          [为什么不能更长] 超时 > 1s 时链路断了设备还在转，不安全。
 */
#define HSF_JOG_REPEAT_MS           200u   /**< 客户端点动重发间隔（毫秒）。 */
#define HSF_JOG_TIMEOUT_MS          600u   /**< 服务端点动看门狗超时（毫秒）。 */

/**
 * @brief 点动释放命令（停止点动）。
 * @details 客户端松手时发一次，服务端立刻停（不等看门狗）。
 *          载荷 {"op":JogRelease,"side":int}；与 VFD_CONTROL 同通道。
 * @note 复用 HSF_CMD_VFD_CONTROL，不新增命令号——语义由 op 区分。
 */
#define HSF_OP_JOG_RELEASE          101    /**< 点动释放（自定义 op，避开 XVfdOperation 值域）。 */

/* ==================== 设备运行状态与错误 ==================== */

/**
 * @brief 设备健康状态（跨设备统一口径）。
 * @details 只表达"能不能正常运行"，不表达业务值：
 *            Ok      —— 在线、有数据、无错误
 *            Stale   —— 通讯在但数据过期（超过陈旧阈值）
 *            Offline —— 从站失联 / 未装配 / 无首拍
 *            Fault   —— 设备报告故障（变频器故障码非 0 等）
 *            Warning —— 有告警但可运行（预留）
 */
typedef enum HsfDeviceHealth {
    HsfDeviceHealth_Ok      = 0,
    HsfDeviceHealth_Warning = 1,
    HsfDeviceHealth_Stale   = 2,
    HsfDeviceHealth_Offline = 3,
    HsfDeviceHealth_Fault   = 4
} HsfDeviceHealth;

/**
 * @brief 设备类别（决定客户端展示分组）。
 */
typedef enum HsfDeviceKind {
    HsfDeviceKind_Vfd       = 0,   /**< 变频器。 */
    HsfDeviceKind_TempHum   = 1,   /**< 温湿度变送器。 */
    HsfDeviceKind_FlowMeter = 2,   /**< 电磁流量计。 */
    HsfDeviceKind_AnalogIo  = 3,   /**< 模拟量模块。 */
    HsfDeviceKind_Bus       = 4    /**< 485 总线本身（物理链路）。 */
} HsfDeviceKind;

/* 健康快照键（Publish/Response 载荷） */
#define HSF_KEY_DEV_COUNT       "devCount"    /**< 设备条目数（int）。 */
#define HSF_KEY_DEV_NAME        "name"        /**< 设备显示名（utf8）。 */
#define HSF_KEY_DEV_KIND        "kind"        /**< 设备类别（int，HsfDeviceKind）。 */
#define HSF_KEY_DEV_SIDE        "side"        /**< 所在侧（int，HsfSide）。 */
#define HSF_KEY_DEV_HEALTH      "health"      /**< 健康状态（int，HsfDeviceHealth）。 */
#define HSF_KEY_DEV_ERROR       "error"       /**< 错误码/故障码（int；0=无）。 */
#define HSF_KEY_DEV_ERROR_TEXT  "errorText"   /**< 错误说明（utf8，可空）。 */
#define HSF_KEY_DEV_ONLINE      "online"      /**< 通讯在线（bool）。 */
#define HSF_KEY_DEV_SEQ         "seq"         /**< 变化序号（int；单调递增，客户端据此判新旧）。 */
#define HSF_KEY_DEV_TS          "ts"          /**< 生成时刻（int64 单调毫秒）。 */

/* ==================== 运行模式 ==================== */

/**
 * @brief 服务端运行模式。
 * @details [手动] 接受 XLink 控制命令（人工远程调试）。
 *          [自动] **拒绝全部 XLink 控制命令**（设备由现场/本地逻辑驱动，
 *          远程只能看不能动）。遥测与快照不受影响，始终可读。
 *          模式本身可由 root 经 XLink 切换（否则远程无法恢复手动），
 *          切换受 HSF_ROLE_CONTROL 角色门禁约束。
 */
typedef enum HsfRunMode {
    HsfRunMode_Manual = 0,   /**< 手动：接受 XLink 控制。 */
    HsfRunMode_Auto   = 1    /**< 自动：拒绝 XLink 控制（仅遥测可读）。 */
} HsfRunMode;

/** @brief 运行模式键（Request/Response 载荷，int）。 */
#define HSF_KEY_MODE                "mode"
/** @brief 自动模式布尔键（Response 冗余字段，客户端可直读）。 */
#define HSF_KEY_AUTO                "auto"

/* ==================== 应答包络键（Response 载荷） ==================== */

/** @brief 应答是否成功（bool）。 */
#define HSF_KEY_OK                  "ok"
/** @brief 失败原因（utf8；仅 ok=false 时有意义）。 */
#define HSF_KEY_ERR                 "err"

#endif /* XHSFLINKPROTO_H */
