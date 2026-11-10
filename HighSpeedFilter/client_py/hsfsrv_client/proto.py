# -*- coding: utf-8 -*-
"""高速过滤器服务端 —— 线协议常量（与 C 侧头文件逐字对齐）。

本文件是「线格式唯一权威」的 Python 镜像，**不写业务逻辑**，改动必须与
下列 C 头文件同步（两侧任一侧漂移都会让客户端静默丢弃整帧）：

    Src/XCore/XProtocol/XLink/XLink_config.h     版本/帧头/心跳/认证/上限
    Src/XCore/XProtocol/XLink/XLinkProto.h       消息类/会话子码/能力位/角色
    HighSpeedFilter/common/XhsfLinkProto.h       命令号/主题号/键名

约定：
  * XLink 线上**一律小端**（帧头与载荷内的整型字段都是）。
  * Modbus 线上**一律大端**（MBAP 与 PDU 的数据区），CRC16 低字节在前。
  * 字符串一律 UTF-8，**不含**结尾 NUL（长度由字段给出）。
"""

from . import ports

# ============================================================================
#                          XLink 协议基座
# ============================================================================

XLINK_VERSION = 0x0200          # 主版本 0x02 → 帧头 16 字节（主版本不一致即断链）
XLINK_HEADER_SIZE = 16
XLINK_MAX_PAYLOAD = 16 * 1024
XLINK_TOPIC_USER_BASE = 0x00010000

#: 心跳：对端静默超过 XLINK_HEARTBEAT_INTERVAL_MS 即主动发一帧；
#: 自身静默超过 XLINK_HEARTBEAT_TIMEOUT_MS 判链路断开。
XLINK_HEARTBEAT_INTERVAL_MS = 1000
XLINK_HEARTBEAT_TIMEOUT_MS = 6000
XLINK_HEARTBEAT_TICK_MS = 250

XLINK_HANDSHAKE_TIMEOUT_MS = 5000
XLINK_AUTH_TIMEOUT_MS = 5000
XLINK_AUTH_FAILURE_LIMIT = 3

#: 认证：盐/摘要/用户名字节数、口令派生迭代次数。
XLINK_AUTH_SALT_BYTES = 16
XLINK_AUTH_NONCE_BYTES = 32
XLINK_AUTH_HASH_ITERATIONS = 10000
XLINK_AUTH_NAME_BYTES = 32
XLINK_AUTH_TEXT_BYTES = 96

# ---- 消息类 ----------------------------------------------------------------
MSGCLASS_SESSION = 0
MSGCLASS_REQUEST = 1
MSGCLASS_RESPONSE = 2
MSGCLASS_PUBLISH = 3
MSGCLASS_SUBSCRIBE = 4
MSGCLASS_UNSUBSCRIBE = 5
MSGCLASS_FILE = 6

VALID_MSGCLASS = (0, 1, 2, 3, 4, 5, 6)

# ---- 载荷数据类型 ----------------------------------------------------------
DATATYPE_RAW = 0
DATATYPE_JSON = 1
DATATYPE_BSON = 2
VALID_DATATYPE = (0, 1, 2)

# ---- 会话子码（msgClass=Session 时的 msgCode） -----------------------------
SESSION_HELLO = 1
SESSION_HELLO_ACK = 2
SESSION_HEARTBEAT = 3
SESSION_GOODBYE = 4
SESSION_AUTH_REQUEST = 5
SESSION_AUTH_CHALLENGE = 6
SESSION_AUTH_RESPONSE = 7
SESSION_AUTH_RESULT = 8

# ---- Goodbye 原因（载荷首字节） --------------------------------------------
GOODBYE_NORMAL = 0
GOODBYE_PROTOCOL = 1
GOODBYE_AUTH_FAILED = 2
GOODBYE_AUTH_REQUIRED = 3
GOODBYE_TIMEOUT = 4
GOODBYE_SERVER_SHUTDOWN = 5

# ---- 能力位（Hello/HelloAck 载荷 u16） ------------------------------------
CAP_NONE = 0x0000
CAP_ZLIB = 0x0001
CAP_TLS = 0x0002
CAP_SNAPSHOT = 0x0004
CAP_REQUEST_TIMEOUT = 0x0008
CAP_AUTH = 0x0010
CAP_FILE_TRANSFER = 0x0020

# ---- 角色与认证结果 --------------------------------------------------------
# [四档角色——追加不重排] 0=匿名 / 1=普通用户 / 2=管理员 / 3=操作者。
# Operator 取追加值 3（不插入 1/2 之间，存量账户库不受影响）⇒ 角色值不单调，
# 禁止用数值大小比较角色；判定一律走 permissions.can_role / bridge.can。
ROLE_ANONYMOUS = 0
ROLE_USER = 1
ROLE_ADMIN = 2
ROLE_OPERATOR = 3

# ---- 权限位（与 C 侧 XAccessPerm / 服务端 proto.PERM_* 逐值一致） ----------
PERM_VIEW = 1 << 0
PERM_DEVICE_CONTROL = 1 << 1
PERM_MODE_SWITCH = 1 << 2
PERM_AUTO_CONTROL = 1 << 3
PERM_ACCOUNT_ADMIN = 1 << 4
PERM_DOWNLOAD = 1 << 5
PERM_ALL = 0x3F

# ---- 登录结果码（AuthResult 载荷首字节）---------------------------------
#
# 与 C 侧 `Src/XCore/XProtocol/XLink/XLinkProto.h` 的 `XLinkAuthStatus`
# 逐值一致；**语义分「永久」与「瞬时」两类**（2026-11-09 根修，见
# `hsfsrv_client/xlink.py` 的 `_on_auth_result`）：
#   · 永久（客户端停自动重连，登录窗留在原地）：
#       BAD_USER / BAD_PASSWORD / LOCKED / NO_CREDENTIAL —— 重试同一凭据
#       永远不会成功，且会把服务端账户失败计数推向锁定。
#   · 瞬时（客户端退避重试）：
#       REJECTED —— 服务端**策略**拒绝（同账并发「该账户已在线」/会话数
#                   已达上限）。凭据可能是对的，旧会话被回收后重试即通过。
#       TIMEOUT / PROTOCOL —— 链路噪声/半开连接。
# 服务端镜像：`server_py/hsfsrv/proto.py` 同值；C 服务端策略闸返回
# `XLinkSessionAuthLookup_Rejected`，框架直接下发 REJECTED（不再伪造成
# BAD_PASSWORD）。
AUTHSTATUS_OK = 0
AUTHSTATUS_BAD_USER = 1
AUTHSTATUS_BAD_PASSWORD = 2
AUTHSTATUS_LOCKED = 3
AUTHSTATUS_REJECTED = 4
AUTHSTATUS_NO_CREDENTIAL = 5
AUTHSTATUS_TIMEOUT = 6
AUTHSTATUS_PROTOCOL = 7

#: 瞬时拒绝（REJECTED）后的自动重连上限（次数）。镜像 C 侧
#: `XLINK_config.h` 的 XLINK_AUTH_REJECT_RETRY_LIMIT；达上限才终局，
#: 避免「账户已在线」这类瞬时状态让客户端永久没数据，也避免无限重试。
XLINK_AUTH_REJECT_RETRY_LIMIT = 30

AUTH_METHOD_ITERATED_HMAC = 1
AUTH_PROOF_DOMAIN = 0x01

# ============================================================================
#                        HSF 业务命令号 / 主题号
# ============================================================================

#: **协议规格**里的默认端口（与 C 版 XhsfLinkProto.h 一致），从 ports 取。
#:
#: 注意：这是「C 版规范值」，**不是** Python 版服务端实际监听的端口。
#: 客户端/服务端的端口见 `ports.PY_SERVER_PORT`（缺省 6200，刻意与 C 版错开，
#: 好让两套服务端同机并存、互不抢占）。
HSF_XLINK_DEFAULT_PORT = ports.C_SERVER_PORT

CMD_VFD_CONTROL = 0x0101
CMD_VFD_SNAPSHOT = 0x0102
CMD_AI_SNAPSHOT = 0x0103
CMD_TEMPHUM_SNAPSHOT = 0x0104
CMD_RUN_MODE = 0x0105
CMD_FLOWMETER_SNAPSHOT = 0x0106
#: 浊度仪快照（Response = 两台 BSON）。
CMD_TURBIDITY_SNAPSHOT = 0x0112
#: 浊度仪遥测推送主题。
TOPIC_TURBIDITY_STATUS = 0x00010009
#: 进水泵快照（Response = 泵状态 BSON）。
CMD_PUMP_SNAPSHOT = 0x0113
#: 进水泵开关（Request 载荷 = {"index":int, "on":bool}）。
CMD_PUMP_CONTROL = 0x0114

#: 自动运行控制（载荷 {"on":bool}；需 root + 自动模式）。
#:
#: [与 CMD_RUN_MODE 分开] 切模式只是**授权**自动可以跑；这条才是
#: 真正按下启动/停止。合成一个动作会让"只想切个模式"变成"设备立刻
#: 开始自动运行"——现场认为太危险。
CMD_AUTO_CONTROL = 0x0115

# ---- 远程配置（采集项/采集频率/数据落盘；与 server_py proto.py 同号同键）----

#: 读取服务端配置快照（无需载荷）。应答 = {"ok","collect","storage",
#: "storageDbPath"}。先 GET 回显当前值，改完 SET 提交。
CMD_CONFIG_GET = 0x0116
#: 设置服务端配置（**需 root**）。载荷 {"section":"collect"/"storage",
#: …配置键…}；服务端在线生效并立即落盘（cfg.save）。
CMD_CONFIG_SET = 0x0117
#: collect 节键：各设备采集开关（bool）。
CFGK_VFD_ENABLED = "vfdEnabled"
CFGK_TEMPHUM_ENABLED = "temphumEnabled"
CFGK_FLOWMETER_ENABLED = "flowmeterEnabled"
CFGK_TURBIDITY_ENABLED = "turbidityEnabled"
#: [逐台开关 2026-11-10] 两台浊度仪是两台独立设备，开关各自可设；与
#: server_py proto.py 同名常量、C 侧 HSF_CFGK_TURBIDITY1_ENABLED/_TURBIDITY2_
#: 逐值同步。老的 turbidityEnabled 保留为两台共同缺省（兼容旧服务端）。
CFGK_TURBIDITY1_ENABLED = "turbidity1Enabled"
CFGK_TURBIDITY2_ENABLED = "turbidity2Enabled"
CFGK_ANALOG_ENABLED = "analogEnabled"
CFGK_DIGITAL_ENABLED = "digitalEnabled"
#: collect 节键：轮询周期（int 毫秒；服务端夹到 50~60000）。
CFGK_VFD_POLL_MS = "vfdPollMs"
CFGK_TEMPHUM_POLL_MS = "temphumPollMs"
CFGK_TURBIDITY_POLL_MS = "turbidityPollMs"
#: [逐台周期 2026-11-10] 两台浊度仪与流量计各按自己的周期采集（共用 485
#: 但节拍互不影响）。与 server_py proto.py 的同名常量、C 侧
#: XhsfLinkProto.h 的 HSF_CFGK_TURBIDITY1_POLL_MS/_TURBIDITY2_/_FLOWMETER_
#: 逐值同步；老的 turbidityPollMs 仍作两台浊度的兼容缺省。
CFGK_TURBIDITY1_POLL_MS = "turbidity1PollMs"
CFGK_TURBIDITY2_POLL_MS = "turbidity2PollMs"
CFGK_FLOWMETER_POLL_MS = "flowmeterPollMs"
CFGK_ANALOG_POLL_MS = "analogPollMs"
CFGK_DIGITAL_POLL_MS = "digitalPollMs"
#: storage 节键：是否落盘 / 库文件路径（空串=服务端程序目录）/ 采样周期。
CFGK_STORAGE_ENABLED = "enabled"
CFGK_STORAGE_PATH = "path"
CFGK_STORAGE_SAMPLE_MS = "sampleMs"
#: 容量限额（现场硬盘小，必须能封顶）。与 server_py proto.py 同名同值。
CFGK_STORAGE_MAX_MB = "maxMb"
CFGK_STORAGE_FULL_POLICY = "fullPolicy"
CFGK_STORAGE_KEEP_FILES = "keepFiles"
#: 满额策略下拉项（键, 文案）——与服务端 proto.STORAGE_POLICIES 同序。
STORAGE_POLICIES = (
    ("ring", "循环覆盖（删最旧，长期无人值守）"),
    ("stop", "到限停写（保留已有数据，只读）"),
    ("rotate", "分段滚动（写满换文件，按段归档）"),
)
STORAGE_POLICY_KEYS = tuple(k for k, _t in STORAGE_POLICIES)
STORAGE_MAX_MB_MIN = 0
STORAGE_MAX_MB_MAX = 1024 * 1024
#: 满额策略 → 一句话说明（界面在选中项下方显示，避免下拉文字太长）。
STORAGE_POLICY_HINTS = {
    "ring": "库满时删最旧的整段数据（三表同一时间线），磁盘占用封顶在上限。"
            "适合长期无人值守。",
    "stop": "库满后停止入库，已有数据原样保留只读。适合宁可丢新数据也"
            "不能丢历史取证的场合。",
    "rotate": "库满后改名归档成带时间戳的分段文件并另起新库，只保留最近"
              "N 段。单段上限 = 总上限 / 段数。",
}
#: 轮询周期允许范围（与 server_py proto.py 同值：服务端越界自动夹取）。
CFG_POLL_MS_MIN = 50
CFG_POLL_MS_MAX = 60000

#: login 节键（账户/登录策略；与 C 侧 XhsfLinkProto.h 的 HSF_CFGK_LOGIN_*
#: 同名同值，2026-11-08 新增）。写入立即对**新登录**生效并落盘
#: hsf_login_policy.json；存量会话不受影响。需管理员（AccountAdmin）。
CFG_SECTION_LOGIN = "login"
CFGK_LOGIN_ALLOW_ANON = "loginAllowAnonymous"
CFGK_LOGIN_ALLOW_CONCUR = "loginAllowConcurrent"
CFGK_LOGIN_MAX_SESSIONS = "loginMaxSessions"
#: 会话上限允许范围（0=不限；服务端夹到 0~256）。
CFG_LOGIN_MAX_SESSIONS_MIN = 0
CFG_LOGIN_MAX_SESSIONS_MAX = 256
#: 登录策略一句话说明（界面在卡内显示）。
LOGIN_POLICY_HINTS = (
    "允许匿名：未宣告认证的对端直接以匿名（只读）会话接入。",
    "允许同账并发：同一账户可同时在多个客户端在线；关闭时 root 单会话。",
    "会话上限：全部在线会话总数封顶，0=不限。",
)

#: 分页读取服务端历史数据（**只读，无需 root**）。
#: 载荷 {"offset","limit","beforeTs"?,"loTs"?,"hiTs"?}；
#: 应答 {"ok","total","rows","cols","dbRows","latestTs",…}。
#: [为什么必须分页] 现场库能存上万行（5s 一行 ≈ 1.7 万行/天），一次全拉
#: 会打爆服务端（SQL 全表读 + 百 KB 级 BSON）和板端内存；协议载荷上限
#: 本身也只有 16KB。客户端定页大小、服务端只回一页 + 总数。
CMD_HISTORY_PAGE = 0x0118
HIST_KEY_OFFSET = "offset"
HIST_KEY_LIMIT = "limit"
HIST_KEY_BEFORE_TS = "beforeTs"
HIST_KEY_LO_TS = "loTs"
HIST_KEY_HI_TS = "hiTs"
#: 关键字（下推服务端，在显示文本上匹配；空串/缺省 = 不限）。
HIST_KEY_KW = "kw"
#: 关键字要比对的显示列下标数组（缺省/空 = 全部列）。
HIST_KEY_KW_COLS = "kwCols"
HIST_KEY_TOTAL = "total"
HIST_KEY_ROWS = "rows"
HIST_KEY_COLS = "cols"
HIST_KEY_DB_ROWS = "dbRows"
HIST_KEY_LATEST_TS = "latestTs"
HIST_KEY_EARLIEST_TS = "earliestTs"
HIST_KEY_WINDOW_START_TS = "windowStartTs"
HIST_KEY_WINDOW_END_TS = "windowEndTs"
#: 导出/下载意图标记（bool；缺省 false=浏览）。导出时置 true，服务端验
#: PERM_DOWNLOAD。
HIST_KEY_EXPORT = "export"
#: 单页行数上下限（服务端夹取；与 server_py proto.py 同值）。
#: 上限由 16KB 载荷反推（实测 50 行 ≈ 11.8KB）。
HISTORY_PAGE_MAX = 50
HISTORY_PAGE_MIN = 1
HISTORY_PAGE_DEFAULT = 20
#: 历史表列名（应答 rows 的列序；与服务端 store.HISTORY_COLUMNS 同序）。
#: [water_temp 在**末尾**] 2026-11-10 新增水温列；按位置追加是为了让 C
#: 客户端的下标映射（HsfLinkHistory.c 的 kSrc/kDst）既有条目全不变。
#: 显示列序另有一套（HistoryPage.COLUMNS，水温在第 8 位紧跟浊度2）。
HISTORY_COLUMNS = (
    "ts", "mode", "vfd_state", "freq_hz", "current_a", "rpm",
    "turb1", "turb2", "flow", "flow_unit", "flow_total", "total_unit",
    "press_mpa", "press_note", "temp", "hum", "di", "do", "water_temp")

#: 运行日志分页查询（**C 端扩展**，2026-11-09 起；与 C 侧
#: XhsfLinkProto.h 的 HSF_CMD_LOG_PAGE=0x0119 同号，头注释里"Python 侧
#: 暂无同号命令"的说法在本常量落库后即为历史——server_py 仍不实现该
#: 命令，本客户端只作消费方）。载荷 {"limit","offset","beforeTs"?,
#: "loTs"?,"hiTs"?,"category"?,"kw"}；应答 {"ok","rows":[{ts,category,
#: text}…],"total","count","offset","limit"}。
#:
#: [与 0x0118 的两点不同] ① rows 是**对象数组**（{ts,category,text}），
#: 0x0118 是按 cols 排的值数组；② 本命令无 export 键形态——导出走
#: "当前页重拉"（C 端 logExportStart 同口径），下载权限闸只在客户端。
#: [窗口锚点 beforeTs] 语义与 0x0118 完全一致：epoch 毫秒；上界 =
#: min(hiTs, beforeTs)，缺省/0 = 服务端取"现在"。库在持续入库，翻页
#: 必须沿用同一锚点，否则 offset 被新行顶偏 ⇒ 相邻页重复/漏行。
#: [category] 类别过滤（精确匹配；服务端入库口径 conn/ctrl/alarm/auto/
#: auth，见 C 端 HsfServerStore insertLog）；空/缺省 = 全部。
CMD_LOG_PAGE = 0x0119

# ---- 运行日志分页键（与 C 侧 XhsfLinkProto.h HSF_KEY_LOG_* 同字面）----
# C 头注释互相点名：字面值与 0x0118 的 HIST_KEY_* 一致的键，改任何一个
# 必须两处（本文件 + C 头）同步（铁律：同号同键）。
KEY_LOG_LIMIT = "limit"        # 与 HIST_KEY_LIMIT 同字面
KEY_LOG_OFFSET = "offset"      # 与 HIST_KEY_OFFSET 同字面
KEY_LOG_BEFORE_TS = "beforeTs" # 与 HIST_KEY_BEFORE_TS 同字面
KEY_LOG_LO_TS = "loTs"         # 与 HIST_KEY_LO_TS 同字面
KEY_LOG_HI_TS = "hiTs"         # 与 HIST_KEY_HI_TS 同字面
#: 类别过滤（utf8；conn=连接 / ctrl=控制 / alarm=告警 / auto=自动 /
#: auth=鉴权；空/缺省 = 全部）。
KEY_LOG_CATEGORY = "category"
KEY_LOG_KW = "kw"              # 与 HIST_KEY_KW 同字面
#: 应答行数组（每元素一个对象 {ts,category,text}；区别于 0x0118 值数组）。
KEY_LOG_ROWS = "rows"          # 与 HIST_KEY_ROWS 同字面
#: 应答：本窗口命中总行数（分页总数按它算）。
KEY_LOG_TOTAL = "total"        # 与 HIST_KEY_TOTAL 同字面
#: 应答：本页实际行数。
KEY_LOG_COUNT = "count"
#: 行对象内键（category 与请求过滤键同字面）。
KEY_LOG_TS = "ts"              # 事件时刻（int64，epoch 毫秒）
KEY_LOG_TEXT = "text"          # 事件文本（utf8）

#: 日志页大小上限（服务端夹取；与 C 侧 HSF_LOG_PAGE_MAX 同值）。
LOG_PAGE_MAX = 50
#: 日志页大小缺省（与 C 侧 HSF_LOG_PAGE_DEFAULT 同值，= 历史页缺省）。
LOG_PAGE_DEFAULT = 20

#: 进水泵遥测推送主题。
TOPIC_PUMP_STATUS = 0x0001000A
CMD_DEVICE_HEALTH = 0x0107
#: 数字量（16DI/16DO）快照请求。载荷 {"dev":int}；应答见 app.handle_digital。
CMD_DIGITAL_SNAPSHOT = 0x0108
#: 数字量 DO 控制。载荷 {"dev":int,"do":int,"on":bool} 单点，
#: 或 {"dev":int,"mask":int,"values":int} 整组；需 root + 手动模式。
CMD_DIGITAL_CONTROL = 0x0109

TOPIC_VFD_STATUS = 0x00010001
TOPIC_AI_STATUS = 0x00010002
TOPIC_TEMPHUM_STATUS = 0x00010003
TOPIC_FLOWMETER_STATUS = 0x00010004
TOPIC_DEVICE_HEALTH = 0x00010005
#: 数字量遥测（逐模块一帧，载荷含 dev 区分 0/1）。
TOPIC_DIGITAL_STATUS = 0x00010006

# ============================================================================
#                              载荷键名
# ============================================================================

KEY_SIDE = "side"
KEY_OK = "ok"
KEY_ERR = "err"
KEY_MODE = "mode"
KEY_AUTO = "auto"
#: 自动控制器的**运行状态**（bool）：自动逻辑此刻是否真的在跑。
#:
#: [与 KEY_AUTO 的区别] `auto` 是"人把模式拨到自动了"；这个才是"自动逻辑
#: 真的在驱动设备"。两者正常同步，但界面**不能**从模式位推断运行位 ——
#: 一旦控制器没启动，界面会显示"自动在跑"而实际什么都不做（最难的假正常）。
KEY_AUTO_RUNNING = "autoRunning"
KEY_DEV = "dev"
KEY_NAME = "name"
KEY_DI = "di"
KEY_DO = "do"
KEY_COUNT = "count"
KEY_VALID = "valid"
KEY_STALE_MS = "staleMs"
KEY_MASK = "mask"
KEY_VALUES = "values"
KEY_ON = "on"

#: 设备健康快照键（与 C 侧 HsfAppHealthEntry → BSON 的拼法一致）。
KEY_DEV_COUNT = "devCount"
KEY_DEV_SEQ = "seq"
KEY_DEV_TS = "ts"

# ============================================================================
#                        VFD 口径（XVfdDevice / XVfdCodec）
# ============================================================================

#: 控制动作（XVfdOperation 数值）。载荷键 op 取值必须在此集合内，
#: 与 C 侧 XVfdCodec 的 xvfdCodecFillControl 白名单一致。
OP_NONE = 0
OP_READ_STATUS = 1
OP_SET_FREQUENCY = 2
OP_SET_SPEED = 3
OP_START = 4
OP_STOP = 5
OP_FREE_STOP = 6
OP_QUICK_STOP = 7
OP_RESET_FAULT = 8
OP_JOG = 9
OP_SELECT_PRESET_SPEED = 10
OP_SET_PRESET_SPEED_HZ = 11
OP_SET_ACCEL_DECEL = 12
OP_SET_PID_TARGET = 13
#: 点动释放（自定义 op，避开 XVfdOperation 值域；见 XhsfLinkProto.h）。
OP_JOG_RELEASE = 101

VALID_CONTROL_OPS = (
    OP_START, OP_STOP, OP_FREE_STOP, OP_QUICK_STOP, OP_JOG, OP_RESET_FAULT,
    OP_SET_FREQUENCY, OP_SET_SPEED, OP_SET_PID_TARGET, OP_SELECT_PRESET_SPEED,
    OP_JOG_RELEASE,
)

#: 控制载荷键名（XVfdCodec 的 XVFD_CODEC_KEY_*）。
KEY_OP = "op"
KEY_REV = "rev"
KEY_HZ = "hz"
KEY_RPM = "rpm"
KEY_VAL = "val"
KEY_IDX = "idx"

#: 运行状态（XVfdRunState）。
RUN_UNKNOWN = 0
RUN_STOPPED = 1
RUN_READY = 2
RUN_RUNNING_FORWARD = 3
RUN_RUNNING_REVERSE = 4
RUN_JOG_FORWARD = 5
RUN_JOG_REVERSE = 6
RUN_ACCELERATING = 7
RUN_DECELERATING = 8
RUN_CONSTANT_SPEED = 9
RUN_DC_BRAKING = 10
RUN_FREE_STOPPING = 11
RUN_QUICK_STOPPING = 12
RUN_FAULT = 13

#: 遥测键列表（XVfdCodec 的 s_telemetryKeys，位号 → 线上键名）。
#: 顺序即 BSON 字段写入顺序，与 C 侧逐字节一致。
VFD_TELEMETRY_FIELDS = (
    (0,  "runState",          "i32"),
    (2,  "running",           "bool"),
    (3,  "faulted",           "bool"),
    (5,  "reverse",           "bool"),
    (8,  "jogActive",         "bool"),
    (7,  "remote",            "bool"),
    (10, "setFreqHz",         "f64"),
    (11, "outputFreqHz",      "f64"),
    (14, "outputVoltageV",    "f64"),
    (15, "outputCurrentA",    "f64"),
    (16, "busVoltageV",       "f64"),
    (18, "outputPowerKw",     "f64"),
    (20, "torquePercent",     "f64"),
    (21, "speedRpm",          "f64"),
    (39, "faultCode",         "u32"),
)

#: 有效掩码键（恒出现，即使无任何字段有效）。int64。
KEY_VALID_MASK = "vm"

# ============================================================================
#                        电磁阀（6 路，1 DO + 2 DI 反馈）
# ============================================================================

#: 电磁阀路数。
HSF_VALVE_COUNT = 6

#: 电磁阀快照请求（Response = 6 路状态 BSON）。载荷空。
CMD_VALVE_SNAPSHOT = 0x0110
#: 电磁阀手动开关（Request 载荷 = {"index":int, "on":bool}；需 root + 手动模式）。
CMD_VALVE_CONTROL = 0x0111
#: 电磁阀遥测周期推送主题（载荷 = 6 路状态 BSON）。
#:
#: [同一主题也承载告警帧] 超时/互斥故障是**事件**，服务端在同一主题上另发
#: 一帧（键空间 "ac"/"a0_*"，无 count、无 v{i}_*）。客户端订阅一次即可两种
#: 都收到；解析必须先认 KEY_ALARM_COUNT，否则会把告警帧当状态帧把点位清空。
TOPIC_VALVE_STATUS = 0x00010007

# ---- 阀告警键 ----

#: 本帧告警条数（int）。**有该键就说明这是告警帧**（不是周期状态帧）。
KEY_ALARM_COUNT = "ac"
#: 告警路号（int，0 起）。
KEY_ALARM_VALVE_INDEX = "vi"
#: 告警阀名（utf8）。
KEY_ALARM_VALVE_NAME = "vn"
#: 告警类型（int；见 VALVE_ALARM_*）。
KEY_ALARM_KIND = "k"
#: 告警文字（utf8，给操作员看；服务端已写好，客户端直接显示）。
KEY_ALARM_TEXT = "t"
#: 发生时刻（int64 单调毫秒）。
KEY_ALARM_AT = "at"
#: 超时秒数（float；仅超时类有意义）。
KEY_ALARM_TIMEOUT_S = "to"

#: 动作超时未到位（服务端 20s 定时器到点）。
VALVE_ALARM_TIMEOUT = 1
#: 开/关到位信号同时有效（接线或阀体故障）。
VALVE_ALARM_CONFLICT = 2
#: 阀恢复（超时后终于到位）。客户端据此消警。
VALVE_ALARM_CLEARED = 3

# ---- 快照键 ----

#: 路数（int）。每路字段以 "v{index}_" 前缀，下方键名接在该前缀后。
KEY_VALVE_COUNT = "count"

# ---- 单路状态键（键名接在 "v{index}_" 前缀之后，与设备健康 dN_* 同一风格） ----

#: 路号（0..5，int）。
KEY_VALVE_INDEX = "index"
#: 阀名（utf8，可空 → 客户端按"电磁阀 N"显示）。
KEY_VALVE_NAME = "name"
#: 是否已下发"开"指令（bool）。**这是命令意图，不是到位结果**。
KEY_VALVE_OPEN_CMD = "openCmd"
#: **控制命令**的目标态（bool；CMD_VALVE_CONTROL 载荷键）。
#:
#: [为什么与 KEY_VALVE_OPEN_CMD 是两个不同的键] 一个是**命令载荷**的输入键
#: （C 侧 HSF_KEY_VALVE_ON = "on"），一个是**状态快照**的输出键
#: （HSF_KEY_VALVE_OPEN_CMD = "openCmd"）。两者不可混用：服务端只读 "on"，
#: 发 "openCmd":true 会被当作"未给目标态"而取缺省 false ⇒ **每次开阀请求
#: 实际都在关阀**，且应答仍是 ok=True（命令确实被受理了）。现场表现为
#: "点了开阀，界面闪一下又变回已关"，从日志几乎看不出来（2026-11-05 实测踩过）。
KEY_VALVE_ON = "on"
#: 开到位反馈（bool）。
KEY_VALVE_OPEN_FB = "openFb"
#: 关到位反馈（bool）。
KEY_VALVE_CLOSE_FB = "closeFb"
#: 阀的实际状态（int，HsfValveState）。
KEY_VALVE_STATE = "state"
#: 该路是否已装配/可用（bool；未配点位时为 false）。
KEY_VALVE_READY = "ready"
#: 数据是否新鲜（bool）。
KEY_VALVE_VALID = "valid"
#: 故障说明（utf8，可空）。
KEY_VALVE_TEXT = "text"
#: 控制命令的**处置结果**（utf8：issued/repeated/reversed/ignored）。
#:
#: [为什么需要] 服务端对重复点击有三种不同处置：真的动了、原样重发但不
#: 重置计时、已到位什么都不做。只回 ok=True 的话界面区分不了"动了"和
#: "本来就在那儿"，用户会以为按钮坏了。
KEY_VALVE_CMD_RESULT = "cmdResult"

#: 电磁阀状态机（**由两个到位反馈推导，不是命令回显**）。
#:
#: [为什么要状态机] 现场只有"开到位/关到位"两个干接点，没有中间位置传感器。
#: 两个都没有 = 阀在途中（动作需要几秒）；两个都有 = 接线/阀体故障
#: （互斥反馈同时为真，必须报警而不是"当成开着"）。
class HsfValveState(object):
    Unknown = 0      #: 未知（未装配/无反馈）
    Closed = 1       #: 关到位（closeFb=1, openFb=0）
    Open = 2         #: 开到位（openFb=1, closeFb=0）
    Moving = 3       #: 动作中（两个反馈都为 0）
    Fault = 4        #: 故障（两个反馈同时为 1，或超时未到位）


def valve_state_from_feedback(open_fb, close_fb):
    """由两个到位反馈推导阀状态。

    :param open_fb: 开到位是否闭合。
    :param close_fb: 关到位是否闭合。
    :return: HsfValveState 之一。
    """
    if open_fb and close_fb:
        return HsfValveState.Fault      # 互斥反馈同时为真 = 接线/阀体故障
    if open_fb:
        return HsfValveState.Open
    if close_fb:
        return HsfValveState.Closed
    return HsfValveState.Moving


# ============================================================================
#                        设备健康（HsfDeviceHealth / Kind）
# ============================================================================

HEALTH_OK = 0
HEALTH_WARNING = 1
HEALTH_STALE = 2
HEALTH_OFFLINE = 3
HEALTH_FAULT = 4

KIND_VFD = 0
KIND_TEMPHUM = 1
KIND_FLOWMETER = 2

# ---- 浊度仪键（v{index}_ 前缀）----

#: 浊度值 ×100（int；客户端 ÷100 显示两位小数）。
KEY_TURB_NTU = "ntu"
#: 原始寄存器值（int；排查真相用）。
KEY_TURB_RAW = "raw"
#: 该台是否在线（bool）。
KEY_TURB_ONLINE = "online"

# ---- 进水泵键 ----

#: 泵路号（int，0 起）。
KEY_PUMP_INDEX = "index"
#: 泵名（utf8）。
KEY_PUMP_NAME = "name"
#: 命令/回显状态（bool；**无反馈信号**，只反映输出）。
KEY_PUMP_ON = "on"
#: 该泵输出是否可控（bool）。
KEY_PUMP_READY = "ready"
KIND_ANALOG_IO = 3
KIND_BUS = 4
#: 数字量模块（本 Python 版新增；客户端按 kind 分组，未知值应降级显示）。
KIND_DIGITAL_IO = 5
#: 电磁阀组（6 路；一个健康条目汇总整组）。
KIND_VALVE = 6
#: 水质仪表（浊度仪等）。
#:
#: [实锅 2026-11-05] 浊度仪初版在服务端复用了 KIND_TEMPHUM，首页
#: "类型"列把两台浊度仪显示成温湿度 —— 数据对、类别完全误导。
KIND_WATER = 7

# ============================================================================
#                        点动手持时序（安全红线，勿随手改）
# ============================================================================

#: 客户端按住期间的重发间隔。
JOG_REPEAT_MS = 200
#: 服务端看门狗：超过该时长未收到点动帧即自动停机。
#: = 3 × 重发间隔 ⇒ 容忍连丢 2 帧；<2× 会「按住一顿一顿」，>1s 则链路断了还在转。
JOG_TIMEOUT_MS = 600

# ============================================================================
#                              缺省账户
# ============================================================================

DEFAULT_USER_NAME = "admin"
DEFAULT_USER_PASS = "admin"
DEFAULT_ROOT_NAME = "root"
DEFAULT_ROOT_PASS = "root"

#: [deprecated] 旧闸值；语义上=PERM_DEVICE_CONTROL。
#: 新代码请用 permissions.can_role / bridge.can（角色值不单调）。
ROLE_CONTROL = ROLE_ADMIN


# ============================================================================
#                          帧头编解码
# ============================================================================

def encode_header(payload_len, msg_class, msg_code, request_id, data_type):
    """打包 16 字节小端帧头。

    线序：[payloadLen u32][msgClass u16][msgCode u16][requestId u32]
          [dataType u8][reserved 3B]
    reserved 恒写 0（接收侧不校验，留作将来扩展锚点）。
    """
    import struct
    return struct.pack("<IHHIB3x", payload_len, msg_class, msg_code,
                       request_id, data_type)


def decode_header(buf):
    """解 16 字节帧头，返回 (payloadLen, msgClass, msgCode, requestId, dataType)。

    做与 C 侧 XLinkCodec_decodeHeader 同口径的校验：载荷上限与 dataType 取值。
    校验失败返回 None —— 调用方应判协议违例并断链（绝不继续按该帧头读载荷，
    否则整条字节流从此错位，表现为「连着但业务全乱」）。
    """
    import struct
    if len(buf) < XLINK_HEADER_SIZE:
        return None
    plen, mclass, mcode, rid, dtype = struct.unpack("<IHHIB3x", buf[:XLINK_HEADER_SIZE])
    if plen > XLINK_MAX_PAYLOAD:
        return None
    if dtype not in VALID_DATATYPE:
        return None
    return plen, mclass, mcode, rid, dtype


def encode_hello(capability):
    """Hello/HelloAck 载荷：version u16 + capability u16（小端）。"""
    import struct
    return struct.pack("<HH", XLINK_VERSION, capability)


def decode_hello(payload):
    """解 Hello 载荷，返回 (version, capability)；长度不足返回 None。"""
    import struct
    if len(payload) < 4:
        return None
    return struct.unpack("<HH", payload[:4])


def pack_topic(topic_id):
    """Publish/Subscribe/Unsubscribe 载荷前缀：完整 u32 topicId（小端）。

    msgCode 线上只有 u16 装不下 0x0001NNNN，故完整主题号一律走载荷首 4 字节。
    **解析侧必须以这 4 字节为准**——用 msgCode 补发会把主题号截成 0x00NN，
    客户端按完整号匹配 → 整帧丢弃（C 侧踩过：首帧补发「发了但收不到」）。
    """
    import struct
    return struct.pack("<I", topic_id & 0xFFFFFFFF)
