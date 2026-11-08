
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

/**
 * @brief 数字量模块（16DI/16DO）遥测快照请求（载荷={"side":int}；可空）。
 * @details Response = 数字量快照 BSON，键表见 HSF_KEY_DI_* / HSF_KEY_DO_*。
 *          Python 侧同号（hsfsrv/proto.py CMD_DIGITAL_SNAPSHOT）。
 */
#define HSF_CMD_DIGITAL_SNAPSHOT    0x0108u

/**
 * @brief 数字量输出控制（载荷 {"index":int,"on":bool}；需管理角色）。
 * @details Response = 应答包络；数字量页 DO 点动即走本命令。
 *          自动模式下与变频器控制同款被拒。
 */
#define HSF_CMD_DIGITAL_CONTROL     0x0109u

/**
 * @brief 电磁阀状态快照请求（空载荷；Response=6 路阀快照 BSON）。
 * @details 键表见 HSF_KEY_VALVE_*；与周期推送同构，供入页/重连对账。
 */
#define HSF_CMD_VALVE_SNAPSHOT      0x0110u

/**
 * @brief 电磁阀控制（载荷 {"index":int,"on":bool}；需管理角色）。
 * @details on=true 开阀、false 关阀。Response 携带最新那一路的快照字段
 *          （v<i>_*），客户端据此即时刷新卡片而不等下一拍推送。
 */
#define HSF_CMD_VALVE_CONTROL       0x0111u

/**
 * @brief 浊度仪遥测快照请求（载荷空；Response = 两台浊度合并 BSON）。
 * @details 与 Python 侧 proto.CMD_TURBIDITY_SNAPSHOT 同号。键表见 HSF_KEY_TURB_*。
 *          只读设备——浊度仪无控制面（标定/清洗都在仪表面板上做）。
 */
#define HSF_CMD_TURBIDITY_SNAPSHOT  0x0112u

/**
 * @brief 进水泵快照请求（载荷空；Response = 泵状态 BSON）。
 * @details 与 Python 侧 proto.CMD_PUMP_SNAPSHOT 同号。键表见 HSF_KEY_PUMP_*。
 *          泵只有输出没有反馈，快照表达的是**命令状态**（见头文件泵键说明）。
 */
#define HSF_CMD_PUMP_SNAPSHOT       0x0113u

/**
 * @brief 进水泵开关（载荷 {"index":int,"on":bool}；需 root + 手动模式）。
 * @details 与 Python 侧 proto.CMD_PUMP_CONTROL 同号。Response = 应答包络 +
 *          泵快照键（与推送同一套键，客户端复用一套解析）。
 */
#define HSF_CMD_PUMP_CONTROL        0x0114u

/**
 * @brief 自动运行启停（载荷 {"op":int}；需 root；启动还须已处自动模式）。
 * @details 与 Python 侧 proto.CMD_AUTO_CONTROL 同号。
 *          [op 操作字（HSF_AUTO_OP_*；缺省/兼容口径见下）]
 *            op=1 启动    —— 授权态（自动模式）才放行；
 *            op=2 停止    —— 永远放行（停机类动作不被任何闸门挡）；
 *            op=3 急停    —— 永远放行，锁存：复位前拒绝一切启动；
 *            op=4 复位    —— 解除急停锁存（回到停止态，**不是**重新启动）。
 *          [兼容] 老载荷 {"on":bool} 仍然受理（true≈op1 / false≈op2）——
 *          已部署客户端不必升级；op 键存在时优先生效。
 *          [与 HSF_CMD_RUN_MODE 的分工——⚠ 是两件事]
 *            RUN_MODE(mode=1) → **授权**自动（远程控制被禁），控制器仍停；
 *            AUTO_CONTROL(op=1) → 人真正按下"开始自动运行"。
 *          把"切模式"和"启动运行"合成一个动作是危险的：操作员只想切模式，
 *          设备就立刻被自动逻辑驱动（现场反馈：默认应该是停止状态）。
 *          [急停的边界] 服务端急停只停**自动控制器**（不再发起新动作）；
 *          设备级急停（切断动力）是硬件链职责，软件不冒充。
 *          Response = {ok, mode, auto, autoRunning}（控制器真实状态回显）。
 */
#define HSF_CMD_AUTO_CONTROL        0x0115u

/**
 * @brief 历史分页查询（载荷 {offset:int, limit:int, beforeTs?:int,
 *                            loTs?:int, hiTs?:int}）。
 * @details 与 Python 侧 proto.CMD_HISTORY_PAGE 同号。**只读数据，不需要
 *          root**（看历史不是控制面动作）。
 *          [窗口锚点 beforeTs] 客户端首屏把自己的"现在"发上来，翻页时
 *          原样回传 ⇒ 整段分页钉在同一批数据上。否则库在持续入库
 *          （5s 一行），翻到第 2 页时新行已顶进来，offset 整体偏移 ⇒
 *          相邻两页出现重复行、旧行被跳过。
 *          [时间单位] 全部是 **epoch 毫秒**（墙钟；不是 monotonic）。
 *          Response = {ok, total, offset, limit, cols, rows, dbRows,
 *          latestTs, earliestTs, windowStartTs, windowEndTs}：
 *          cols 是列名数组，rows 是**原始值**数组（每行一个数组，NULL 列
 *          发 BSON null），显示格式化在客户端做（与服务端口径一致）。
 */
#define HSF_CMD_HISTORY_PAGE        0x0118u

/**
 * @brief 远端配置读取（载荷空；**任何角色都可读**，只读回显不涉密）。
 * @details 与 Python 侧 proto.CMD_CONFIG_GET 同号。应答：
 *          {ok, collect:{…}, storage:{…}, storageDbPath, storageStats}
 *          · collect —— 采集节（各设备开关 + 轮询周期）；
 *          · storage —— 落盘节（开关/路径/采样周期/上限/策略/保留份数）；
 *          · storageDbPath —— 实际库文件路径；
 *          · storageStats —— 容量实况（usedBytes/contentBytes/maxMb/rows…）。
 *          键名与 Python proto.CFGK_* 完全一致 ⇒ 两端客户端可互换。
 */
#define HSF_CMD_CONFIG_GET          0x0116u

/**
 * @brief 远端配置写入（载荷 {section:"collect"|"storage", …键}；**需 root**）。
 * @details 与 Python 侧 proto.CMD_CONFIG_SET 同号。[为什么 root 但不看自动
 *          模式闸] 改配置不动任何输出点，不属"控制"；自动运行中允许调采集
 *          频率/开关（监视不受模式限制）。
 *          [C 端口径差异] 采集节参数在 C 端是**编译期常量**（写在
 *          HsfServerConfig.h 的宏里，与现场接线同性质），故写入落 INI、
 *          **重启服务端后生效**；落盘节是运行期参数，立即生效。
 */
#define HSF_CMD_CONFIG_SET          0x0117u

/* ---- 远端配置键（与 Python 侧 proto.CFGK_* 同名） ---- */

/* 采集节：设备开关。 */
#define HSF_CFGK_VFD_ENABLED        "vfdEnabled"
#define HSF_CFGK_TEMPHUM_ENABLED    "temphumEnabled"
#define HSF_CFGK_FLOWMETER_ENABLED  "flowmeterEnabled"
#define HSF_CFGK_TURBIDITY_ENABLED  "turbidityEnabled"
#define HSF_CFGK_ANALOG_ENABLED     "analogEnabled"
#define HSF_CFGK_DIGITAL_ENABLED    "digitalEnabled"

/* 采集节：轮询周期（毫秒）。 */
#define HSF_CFGK_VFD_POLL_MS        "vfdPollMs"
#define HSF_CFGK_TEMPHUM_POLL_MS    "temphumPollMs"
#define HSF_CFGK_TURBIDITY_POLL_MS  "turbidityPollMs"
#define HSF_CFGK_ANALOG_POLL_MS     "analogPollMs"
#define HSF_CFGK_DIGITAL_POLL_MS    "digitalPollMs"

/* 落盘节。 */
#define HSF_CFGK_STORAGE_ENABLED    "enabled"
#define HSF_CFGK_STORAGE_PATH       "path"
#define HSF_CFGK_STORAGE_SAMPLE_MS  "sampleMs"
#define HSF_CFGK_STORAGE_MAX_MB     "maxMb"
#define HSF_CFGK_STORAGE_FULL_POLICY "fullPolicy"
#define HSF_CFGK_STORAGE_KEEP_FILES "keepFiles"

/* 登录/账户策略节（2026-11-08 新增；与 Python 侧 CFGK_LOGIN_* 同值同名）。
 * 语义见服务端 XLoginPolicy：
 *   - loginAllowAnonymous：未宣告认证的对端是否放行为匿名会话（缺省关）；
 *   - loginAllowConcurrent：同一账户是否允许同时在线多个会话（缺省关，
 *     即 root 单会话；压测/多端围观场景可开）；
 *   - loginMaxSessions：会话总数上限（0=不限；缺省 8）。
 * 写入立即对**新登录**生效并落盘 hsf_login_policy.json，存量会话不受影响。 */
#define HSF_CFGK_LOGIN_ALLOW_ANON   "loginAllowAnonymous"
#define HSF_CFGK_LOGIN_ALLOW_CONCUR "loginAllowConcurrent"
#define HSF_CFGK_LOGIN_MAX_SESSIONS "loginMaxSessions"

/* 应答段/顶层键。 */
#define HSF_CFG_SECTION_KEY         "section"
#define HSF_CFG_SECTION_COLLECT     "collect"
#define HSF_CFG_SECTION_STORAGE     "storage"
#define HSF_CFG_SECTION_LOGIN       "login"
#define HSF_CFG_STORAGE_DB_PATH     "storageDbPath"
#define HSF_CFG_STORAGE_STATS       "storageStats"

/* ---- 历史分页键（与 Python 侧 proto.HIST_KEY_* 同名） ---- */

#define HSF_KEY_HIST_OFFSET         "offset"
#define HSF_KEY_HIST_LIMIT          "limit"
#define HSF_KEY_HIST_BEFORE_TS      "beforeTs"
#define HSF_KEY_HIST_LO_TS          "loTs"
#define HSF_KEY_HIST_HI_TS          "hiTs"
#define HSF_KEY_HIST_TOTAL          "total"
#define HSF_KEY_HIST_ROWS           "rows"
#define HSF_KEY_HIST_COLS           "cols"
#define HSF_KEY_HIST_DB_ROWS        "dbRows"
#define HSF_KEY_HIST_LATEST_TS      "latestTs"
#define HSF_KEY_HIST_EARLIEST_TS    "earliestTs"
#define HSF_KEY_HIST_WINDOW_START   "windowStartTs"
#define HSF_KEY_HIST_WINDOW_END     "windowEndTs"
/**
 * @brief 关键字筛选（utf8；下推服务端，与 Python store._keyword_condition 同口径）。
 * @details [为什么下推] 分页在服务端、筛选在客户端时，只能在当前页里搜；
 *          命中行绝大多数在别的页上，界面表现就是"筛选无效"。总数与页
 *          内容必须共用同一条 WHERE。
 */
#define HSF_KEY_HIST_KW             "kw"
/** @brief 关键字比对列下标数组（int32 数组；0..14 = 显示列；缺省=全部列）。 */
#define HSF_KEY_HIST_KW_COLS        "kwCols"
/** @brief 显示列数（与服务端 15 个显示表达式/客户端表格列一一对应）。 */
#define HSF_HISTORY_DISPLAY_COLS    15
/**
 * @brief 导出/下载意图标记（bool；缺省 false=普通分页浏览）。
 * @details [为什么要上服务端] "下载数据"是权限点（XAccessPerm_Download），
 *          不能只靠客户端把按钮灰掉——绕过界面直接发帧的人必须被服务端
 *          拒绝。客户端在**导出**时把本键置 true；服务端据此验权限，
 *          普通浏览（false/缺省）按只读面放行。
 */
#define HSF_KEY_HIST_EXPORT         "export"

/** @brief 历史分页：页大小上限（与 Python HISTORY_PAGE_MAX 同值）。 */
#define HSF_HISTORY_PAGE_MAX        50
/** @brief 历史分页：页大小下限。 */
#define HSF_HISTORY_PAGE_MIN        1
/** @brief 历史分页：页大小缺省（与 Python HISTORY_PAGE_DEFAULT 同值）。 */
#define HSF_HISTORY_PAGE_DEFAULT    20

/* ---- 自动运行 op 操作字（HSF_CMD_AUTO_CONTROL 载荷 "op" 键取值） ---- */

/** @brief 启动自动运行（须已处自动模式）。 */
#define HSF_AUTO_OP_START           1
/** @brief 停止自动运行（永远放行）。 */
#define HSF_AUTO_OP_STOP            2
/** @brief 急停（锁存；复位前拒绝启动；永远放行）。 */
#define HSF_AUTO_OP_ESTOP           3
/** @brief 复位（解除急停锁存，回到停止态；不是重新启动）。 */
#define HSF_AUTO_OP_RESET           4

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

/**
 * @brief 数字量模块遥测推送主题（载荷=数字量快照 BSON）。
 * @details 周期推送本侧（A 侧=.12 主模块）的 16DI/16DO 位图。
 */
#define HSF_TOPIC_DIGITAL_STATUS    0x00010006u

/** @brief 电磁阀状态推送主题（载荷=6 路阀快照 BSON；周期推送）。 */
#define HSF_TOPIC_VALVE_STATUS      0x00010007u

/**
 * @brief 浊度仪遥测推送主题（载荷=两台浊度合并 BSON；周期推送）。
 * @details 与 Python 侧 proto.TOPIC_TURBIDITY_STATUS 同号。
 *          [为什么两台合成一帧] 它们总是成对显示、成对刷新，分成两个主题
 *          只会让客户端写两遍同样的订阅逻辑；台数由帧里的 count 决定。
 */
#define HSF_TOPIC_TURBIDITY_STATUS  0x00010009u

/**
 * @brief 进水泵状态推送主题（载荷=泵快照 BSON；周期推送）。
 * @details 与 Python 侧 proto.TOPIC_PUMP_STATUS 同号。
 */
#define HSF_TOPIC_PUMP_STATUS       0x0001000Au

/**
 * @brief 自动运行告警推送主题（载荷=单条告警 BSON；**事件驱动**）。
 * @details [为什么单独一个主题] 自动运行的报警（动作失败/设备不可用/
 *          逻辑自定义）与遥测不同：遥测是周期帧，报警是**事件**——发生
 *          才推一次。远端（客户端/运维终端）订阅本主题即可远程监视自动
 *          运行的异常，配合 HSF_CMD_AUTO_CONTROL 完成远程管理闭环。
 *          帧键表复用阀告警键（ac/vi/k/t/at/to）还是专用键？——本主题用
 *          XAutoControlCodec 的告警帧（kind/atMs/text），与阀告警解耦：
 *          两类告警的消费者、展示页、处置动作都不同，共用键表只会让
 *          客户端多一层"先猜类型"的分支。
 */
#define HSF_TOPIC_AUTO_ALARM        0x0001000Bu

/**
 * @brief 自动运行告警帧键（XAutoControlCodec 口径，一条告警一帧）。
 * @details 与阀告警帧（ac/vi/k/t/at/to）**刻意不同键**：两类告警的消费者、
 *          展示页、处置动作都不同，共用键表只会让客户端多一层"先猜类型"
 *          的分支。类型值与 XAutoControlAlarmKind 逐值一致（宏见下）。
 */
#define HSF_KEY_AUTOALARM_KIND      "kind"  /**< 告警类型（int；HSF_AUTO_ALARM_*）。 */
#define HSF_KEY_AUTOALARM_AT        "atMs"  /**< 发生时刻（int64 单调毫秒）。 */
#define HSF_KEY_AUTOALARM_TEXT      "text"  /**< 告警文字（utf8，给操作员看）。 */

/** @brief 自动运行告警类型：动作下发失败（与 XAutoControlAlarm_ActionFailed 同值）。 */
#define HSF_AUTO_ALARM_ACTION_FAILED     1
/** @brief 自动运行告警类型：依赖设备不可用（与 XAutoControlAlarm_DeviceUnavailable 同值）。 */
#define HSF_AUTO_ALARM_DEVICE_UNAVAIL    2
/** @brief 自动运行告警类型：逻辑自定义（与 XAutoControlAlarm_Custom 同值）。 */
#define HSF_AUTO_ALARM_CUSTOM            3
/** @brief 自动运行告警类型：急停触发（与 XAutoControlAlarm_EmergencyStop 同值）。 */
#define HSF_AUTO_ALARM_ESTOP             4

/* ==================== 侧号（两路 485 各管一侧） ==================== */

/** @brief A 侧（路 A 总线）。 */
#define HSF_SIDE_A                  0
/** @brief B 侧（路 B 总线，预留）。 */
#define HSF_SIDE_B                  1

/** @brief 侧号载荷键名（int；缺省按 A 侧）。 */
#define HSF_KEY_SIDE                "side"

/* ==================== 角色策略（XLinkAuthRole） ==================== */

/**
 * @brief 四档角色的权限语义（与 Src/Industry/XAccessControl 的 XPermPolicy 同源）。
 * @details 匿名=只能查看；普通用户=查看+下载；操作者=+设备控制；管理员=全权。
 *          **角色值不单调**（Operator=3 是追加值，Admin=2），故下面**不提供
 *          "最低角色"宏**——服务端一律走 XPermPolicy 权限点判定，禁止
 *          `role >= HSF_ROLE_CONTROL` 这类数值比较。
 *          HSF_ROLE_CONTROL 仅为兼容旧代码保留，新代码请用权限点。
 * @note  控件客户端若需自定义闸，用 HsfLinkClient_can(HSF_PERM_*)。
 */
#define HSF_ROLE_ANONYMOUS          0u   /* XLinkAuthRole_Anonymous */
#define HSF_ROLE_USER               1u   /* XLinkAuthRole_User      */
#define HSF_ROLE_ADMIN              2u   /* XLinkAuthRole_Admin     */
#define HSF_ROLE_OPERATOR           3u   /* XLinkAuthRole_Operator（追加值） */

/** @brief [deprecated] 旧闸值；语义上=权限点 DeviceControl（操作者+）。 */
#define HSF_ROLE_CONTROL            2u

/* ---- 权限位（与 XAccessPerm 同值；客户端/服务端同源口径） ---- */
#define HSF_PERM_VIEW               (1u << 0)  /**< 查看遥测/快照/历史。 */
#define HSF_PERM_DEVICE_CONTROL     (1u << 1)  /**< 设备控制（操作者+）。 */
#define HSF_PERM_MODE_SWITCH        (1u << 2)  /**< 手/自动切换（管理员）。 */
#define HSF_PERM_AUTO_CONTROL       (1u << 3)  /**< 自动运行启停（管理员）。 */
#define HSF_PERM_ACCOUNT_ADMIN      (1u << 4)  /**< 账户/服务端配置（管理员）。 */
#define HSF_PERM_DOWNLOAD           (1u << 5)  /**< 数据下载/导出（普通用户+）。 */
#define HSF_PERM_ALL                (0x3Fu)

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

/* ---- 设备类别补充（与 Python 侧 proto.KIND_* 逐值一致） ---- */

#ifndef HSF_DEVICE_KIND_DIGITAL
#define HSF_DEVICE_KIND_DIGITAL     5   /**< 数字量模块（16DI/16DO）。 */
#endif
#ifndef HSF_DEVICE_KIND_VALVE
#define HSF_DEVICE_KIND_VALVE       6   /**< 电磁阀组。 */
#endif
#ifndef HSF_DEVICE_KIND_WATER
/**
 * @brief 水质仪表（浊度仪等）。
 * @note  [实锅 2026-11-05] 浊度仪初版复用了 HsfDeviceKind_TempHum（1），
 *        首页"类型"列把两台浊度仪显示成"温湿度" —— 数据正确但类别完全
 *        误导（现场会以为那是电箱里的温湿度变送器）。类别必须表达"这是
 *        什么设备"，复用一个不相关的值等于在界面上撒谎。
 */
#define HSF_DEVICE_KIND_WATER       7
#endif

/* ==================== 数字量模块（32DI/32DO） ==================== */

/**
 * @brief 数字量通道数**上限**（客户端与服务端共用的编译期数组容量）。
 * @note  [2026-11-05 现场 32 点] 原值 16。现场两台 MT3A-IO1632 经 .12 聚合
 *        对外表现为 32DI/32DO（.13 的 16 点映射到 .12 的上半区 bit16..31），
 *        故容量放到 32。
 *
 *        这是**数组容量**，不是实际点数：实际点数恒由服务端帧里的 count 键
 *        给出，客户端按 count 显示（可以少于 32）。把它当成"实际点数"写死
 *        会在少点位时截断——Python 模拟器刻意把合成信号放在 bit16..31，
 *        就是为了让这类截断立刻暴露（实测：模拟器发 32 点、客户端只画 16）。
 */
#define HSF_DIGITAL_CHANNELS        32

/** @brief 模块下标（多模块时区分；当前 A 侧单模块恒 0）。 */
#define HSF_KEY_DIG_DEV             "dev"
/** @brief DI 位图（int；bit n = 通道 n，1=通）。 */
#define HSF_KEY_DIG_DI              "di"
/** @brief DO 位图（int；bit n = 通道 n，1=通）。 */
#define HSF_KEY_DIG_DO              "do"
/** @brief 本模块点数（int；DI/DO 同名路数）。 */
#define HSF_KEY_DIG_COUNT           "count"
/** @brief 通道输出控制载荷：通道下标（int）。 */
#define HSF_KEY_DIG_INDEX           "index"
/** @brief 通道输出控制载荷：目标值（bool，true=通电）。 */
#define HSF_KEY_DIG_ON              "on"

/**
 * @brief 通道名键（**全量快照专用**；格式 "din<通道>"）。
 * @details 服务端是通道名的唯一事实源（名字写在 HsfServerConfig.h 的宏里）。
 *          客户端**首次连接拉一次全量**拿到它，之后只收状态/数值增量；
 *          名字变化时服务端**主动下发**一个只含名字键的快照（见
 *          HSF_KEY_NAME_FULL 语义），客户端按"有名字键就更新名字"处理。
 */
#define HSF_KEY_DIG_DI_NAME_FMT     "din%d"   /**< DI 通道 n 的名字（utf8）。 */
#define HSF_KEY_DIG_DO_NAME_FMT     "don%d"   /**< DO 通道 n 的名字（utf8）。 */

/**
 * @brief "本帧是否全量快照"标志（bool）。
 * @details true ⇒ 载荷含全部通道名 + 全部状态（客户端据此重建点表）；
 *          false/缺省 ⇒ 只有状态，客户端沿用已缓存的名字。
 *          [为什么需要这个键] 增量帧不带名字，客户端若靠"有名字键"猜测
 *          会漏判；显式标志让两条路径完全可区分。
 */
#define HSF_KEY_FULL                "full"

/* ==================== 模拟量输入 ==================== */

/** @brief 模拟量通道数上限（中盛 8 路 4-20mA 模块）。 */
#define HSF_ANALOG_CHANNELS         8

/** @brief 通道数（int）。 */
#define HSF_KEY_AI_COUNT            "n"
/** @brief 有效位掩码（int；bit i=通道 i 有效）。 */
#define HSF_KEY_AI_VALIDMASK        "v"
/** @brief 通道 i 原始寄存器值（int）。 */
#define HSF_KEY_AI_RAW_FMT          "r%d"
/** @brief 通道 i 工程值（微安定点，int）。 */
#define HSF_KEY_AI_UA_FMT           "u%d"
/** @brief 通道 i 的名字（utf8；**全量快照专用**）。 */
#define HSF_KEY_AI_NAME_FMT         "an%d"
/**
 * @brief 通道 i 的**工程量**（定点整数：值 ×1000；如 0.499MPa ⇒ 499）。
 * @details [与 Python 侧同键同义] Python server_py 的 analog 帧用 e{i}/
 *          eu{i}/en{i}/ew{i} 四键表达"标定过的通道"：换算值、单位、通道名、
 *          越限说明。跨端对齐的关键是**键字面一致**，故本端同用。
 *          换算在服务端做（量程是现场标定参数，与接线同性质）。
 */
#define HSF_KEY_AI_ENG_FMT          "e%d"
/** @brief 通道 i 的工程量单位（utf8；如 "MPa"）。 */
#define HSF_KEY_AI_ENG_UNIT_FMT     "eu%d"
/** @brief 通道 i 的标定名称（utf8；如 "压差计"；仅有标定的通道才有）。 */
#define HSF_KEY_AI_ENG_NAME_FMT     "en%d"
/** @brief 通道 i 的越限说明（utf8；空=正常，如"低于 4.0mA（断线/欠量程）"）。 */
#define HSF_KEY_AI_ENG_NOTE_FMT     "ew%d"

/* ==================== 电磁阀（6 路，每路 1 DO + 2 DI 反馈） ==================== */

/** @brief 电磁阀路数上限（与本侧阀组表容量一致）。 */
#define HSF_VALVE_COUNT             6

/**
 * @brief 阀位状态（跨端统一口径；与 Python 侧 HsfValveState 逐值一致）。
 * @details 状态**只由两个到位反馈推导**，不看命令回显——写了 1 只代表命令
 *          发出去了，阀有没有真动要靠行程开关说话（工业现场最常见的故障
 *          就是"命令发了、阀卡住了"）。
 */
typedef enum HsfValveState {
    HsfValveState_Unknown = 0,  /**< 状态未知（含无反馈/未配置）。 */
    HsfValveState_Closed  = 1,  /**< 已关（关到位）。 */
    HsfValveState_Open    = 2,  /**< 已开（开到位）。 */
    HsfValveState_Moving  = 3,  /**< 动作中（两个反馈都没到位）。 */
    HsfValveState_Fault   = 4   /**< 故障（双限位矛盾/动作超时）。 */
} HsfValveState;

/* ==================== 浊度仪（两台合并一帧；v{index}_ 前缀） ==================== */

/* [为什么两台合成一帧] 它们总是成对显示、成对刷新，分开两个主题只会让客户端
 * 写两遍同样的订阅逻辑；台数由帧里的 count 决定，将来加第三台也只是 count 变大。 */

/** @brief 浊度仪台数上限（结构上留多台位置；当前 2 台）。
 *  @details 与 Python 侧 proto.HSF_TURBIDITY_COUNT、服务端 HSFServerConfig
 *           的 HSF_TURBIDITY_COUNT 同值；放在公共协议头是**为了让客户端也能
 *           定长开数组**（客户端不引服务端配置头）。
 */
#ifndef HSF_TURBIDITY_COUNT
#define HSF_TURBIDITY_COUNT       2
#endif

/** @brief 帧内台数（顶层，"count"；int）。 */
#define HSF_KEY_TURB_COUNT          "count"
/** @brief 单台字段前缀格式：v<i>_ntu 等（i=台序，从 0 起）。 */
#define HSF_KEY_TURB_PREFIX_FMT     "v%d_%s"
/** @brief 仪器名（utf8；服务端给，客户端不猜"是原水还是出水"）。 */
#define HSF_KEY_TURB_NAME           "name"
/**
 * @brief 浊度值 NTU × 100 的**整数**（int）。
 * @details [为什么发定点整数而不是浮点] 与模拟量工程值同一口径，避免浮点在
 *          BSON 上的精度争议；客户端 ÷100 显示两位小数。
 *          **注意方向**：这里是 NTU×100，而 XTurbidityMeterCodec 的 "ntu" 是
 *          NTU 原值（double）——两者同名但口径不同，是刻意的：
 *            · 设备编解码层（XTurbidityMeterCodec）服务于**进程内/记录回放**，
 *              给的是无损原值；
 *            · 线上协议层（本文件）服务于**跨进程展示**，按 Python 侧既定口径
 *              发定点整数（proto.KEY_TURB_NTU 也是 ×100）。
 *          混用会差 100 倍且不报错，故两处注释互相点名。
 */
#define HSF_KEY_TURB_NTU            "ntu"
/** @brief 32 位原始寄存器位模式（int；排查真相用，不受换算影响）。 */
#define HSF_KEY_TURB_RAW            "raw"
/** @brief 该台是否在线（bool；已读到且未陈旧）。 */
#define HSF_KEY_TURB_ONLINE         "online"

/* ==================== 进水泵（1 台；只有输出、无反馈） ==================== */

/**
 * @brief 进水泵台数上限（结构上留多台位置；当前 1 台）。
 * @details 与 Python 侧 proto.HSF_PUMP_COUNT 逐值一致。
 */
#ifndef HSF_PUMP_COUNT
#define HSF_PUMP_COUNT              1
#endif

/** @brief 泵台数（快照顶层键；与阀/浊度同键名 "count"）。 */
#define HSF_KEY_PUMP_COUNT          "count"

/**
 * @brief [泵与阀的本质区别] 泵没有行程反馈——现场只控制接触器，写了就是
 *        命令。故泵**不判到位、不设超时定时器、无告警**：状态是**命令
 *        状态**（我们最后一次写的值，经 DO 回显校准）。硬造"超时未到位"
 *        等于撒谎——没有反馈就没有可比对的真相源。要真正监视泵，得现场
 *        加运行反馈点（接触器辅助触点/流量开关），那时再扩展键表。
 */

/** @brief 泵路号（快照前缀键；int，0 起）。 */
#define HSF_KEY_PUMP_INDEX          "index"
/** @brief 泵名（utf8）。 */
#define HSF_KEY_PUMP_NAME           "name"
/** @brief 命令状态（bool；true=运行/接触器吸合）。**只反映输出，无反馈信号**。 */
#define HSF_KEY_PUMP_ON             "on"
/** @brief 该泵输出是否可控（点位已配置且模块在线）（bool）。 */
#define HSF_KEY_PUMP_READY          "ready"
/** @brief 数据有效性：与阀同键名 "valid"（HSF_KEY_VALVE_VALID；前缀后同名）。 */
/** @brief 说明（utf8，可空；"未配置点位"/"模块离线"等）。 */
#define HSF_KEY_PUMP_TEXT           "text"

/** @brief 阀路数（快照顶层）。 */
#define HSF_KEY_VALVE_COUNT         "count"
/** @brief 单路字段前缀格式：v<i>_name 等（i=路序，从 0 起）。 */
#define HSF_KEY_VALVE_PREFIX_FMT    "v%d_%s"
/** @brief 阀名（utf8）。 */
#define HSF_KEY_VALVE_NAME          "name"
/** @brief 最近一次命令意图（bool；true=开）。 */
#define HSF_KEY_VALVE_OPEN_CMD      "openCmd"
/** @brief 开到位反馈（bool）。 */
#define HSF_KEY_VALVE_OPEN_FB       "openFb"
/** @brief 关到位反馈（bool）。 */
#define HSF_KEY_VALVE_CLOSE_FB      "closeFb"
/** @brief 阀位状态（int，HsfValveState）。 */
#define HSF_KEY_VALVE_STATE         "state"
/** @brief 点位是否配置齐全（bool；false=未配置，控制被拒）。 */
#define HSF_KEY_VALVE_READY         "ready"
/** @brief 数据有效性（bool；服务端质量口径）。 */
#define HSF_KEY_VALVE_VALID         "valid"
/** @brief 状态说明（utf8；故障原因/排查提示，可空）。 */
#define HSF_KEY_VALVE_TEXT          "text"
/** @brief 阀控制载荷：路序（int）。 */
#define HSF_KEY_VALVE_INDEX         "index"
/** @brief 阀控制载荷：目标动作（bool；true=开阀）。 */
#define HSF_KEY_VALVE_ON            "on"
/**
 * @brief 阀控制处置结果（utf8；随快照/控制应答以 v{i}_ 前缀下发）。
 * @details 与 Python 侧 KEY_VALVE_CMD_RESULT 同名同值域：issued（新动作）/
 *          repeated（同目标在途重发，不重置计时）/ reversed（在途反向）/
 *          ignored（已到位同目标，幂等忽略）/ external（DO 被外部改动，
 *          接管为新动作）。客户端据此给不同状态栏文案。
 */
#define HSF_KEY_VALVE_CMD_RESULT    "cmdResult"

/* ---------------- 阀告警（事件帧，随阀状态主题下发） ---------------- */

/**
 * @brief 阀告警主题（**遗留定义，2026-11-06 起服务端不再使用**）。
 * @details [为什么改回阀状态主题] Python 生态（server_py/client_py/webui_py）
 *          把告警帧推在 `TOPIC_VALVE_STATUS`(0x00010007) 上——载荷带 "ac"
 *          键即告警帧，键空间 `a{i}_*` 与周期状态的 `v{i}_*` 不相交，客户端
 *          按键判型。而 Python 客户端只订阅 0x00010007 ⇒ C 服务端推 0x00010008
 *          的告警在 Python 界面上被**静默丢弃**（TOPIC_KIND 查不到即 return）。
 *          独立主题"全推不要求订阅"的初衷由 publish_ex 的订阅语义兜不住
 *          （无人订阅即 0 投递），故对齐 Python 布局：随 0x00010007 推
 *          {ac:N, a0_vi/vn/k/t/at/to...}，取走即消费保证一条只推一次。
 *          本宏保留仅供对照，新代码勿再使用。
 */
#define HSF_TOPIC_VALVE_ALARM       0x00010008u

/** @brief 本帧告警条数（int）。**有该键就说明这是告警帧**（不是周期状态帧）。 */
#define HSF_KEY_ALARM_COUNT         "ac"
/** @brief 告警路号（int，0 起）。 */
#define HSF_KEY_ALARM_VALVE_INDEX   "vi"
/** @brief 告警阀名（utf8）。 */
#define HSF_KEY_ALARM_VALVE_NAME    "vn"
/** @brief 告警类型（int；见 HsfValveAlarmKind）。 */
#define HSF_KEY_ALARM_KIND          "k"
/** @brief 告警文字（utf8，给操作员看）。 */
#define HSF_KEY_ALARM_TEXT          "t"
/** @brief 发生时刻（int64 单调毫秒）。 */
#define HSF_KEY_ALARM_AT            "at"
/** @brief 超时秒数（float；仅超时类有意义）。 */
#define HSF_KEY_ALARM_TIMEOUT_S     "to"

/**
 * @brief 阀告警类型（与 Python 侧 VALVE_ALARM_* 逐值一致）。
 */
typedef enum HsfValveAlarmKind {
    HsfValveAlarmKind_Timeout  = 1, /**< 动作超时未到位。 */
    HsfValveAlarmKind_Conflict = 2, /**< 开/关到位信号同时有效（接线/阀体故障）。 */
    HsfValveAlarmKind_Cleared  = 3  /**< 已到位（消警）。 */
} HsfValveAlarmKind;

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
/**
 * @brief 自动运行**控制器**启停状态（Response 冗余字段，bool）。
 * @details [与 HSF_KEY_AUTO 的区别——两个"自动"不是一回事]
 *            auto       = 模式闸：远程控制是否被禁（授权态）；
 *            autoRunning = 控制器：自动逻辑是否正在跑（动作态）。
 *          CMD_RUN_MODE(mode=1) 后 auto=true 而 autoRunning=false——授权了
 *          但没人按下"开始"；CMD_AUTO_CONTROL(on=1) 后两者才同时为 true。
 *          RUN_MODE 查询/设置应答与 AUTO_CONTROL 应答都带本键：客户端拿到
 *          的就是**切换之后**的控制器真实状态，不必等下一帧推送。
 */
#define HSF_KEY_AUTO_RUNNING        "autoRunning"

/* ==================== 应答包络键（Response 载荷） ==================== */

/** @brief 应答是否成功（bool）。 */
#define HSF_KEY_OK                  "ok"
/** @brief 失败原因（utf8；仅 ok=false 时有意义）。 */
#define HSF_KEY_ERR                 "err"

#endif /* XHSFLINKPROTO_H */
