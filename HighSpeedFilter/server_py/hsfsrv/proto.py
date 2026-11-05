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
ROLE_ANONYMOUS = 0
ROLE_USER = 1
ROLE_ADMIN = 2

AUTHSTATUS_OK = 0
AUTHSTATUS_BAD_USER = 1
AUTHSTATUS_BAD_PASSWORD = 2
AUTHSTATUS_LOCKED = 3
AUTHSTATUS_REJECTED = 4
AUTHSTATUS_NO_CREDENTIAL = 5
AUTHSTATUS_TIMEOUT = 6
AUTHSTATUS_PROTOCOL = 7

AUTH_METHOD_ITERATED_HMAC = 1
AUTH_PROOF_DOMAIN = 0x01

# ============================================================================
#                        HSF 业务命令号 / 主题号
# ============================================================================

#: **协议规格**里的默认端口（与 C 版 XhsfLinkProto.h 一致），从 ports 取。
#:
#: 注意：这是「C 版规范值」，**不是** Python 版服务端实际监听的端口。
#: 本服务的端口见 `ports.PY_SERVER_PORT`（缺省 6200，刻意与 C 版错开，
#: 好让两套服务端同机并存、互不抢占）。
HSF_XLINK_DEFAULT_PORT = ports.C_SERVER_PORT

CMD_VFD_CONTROL = 0x0101
CMD_VFD_SNAPSHOT = 0x0102
CMD_AI_SNAPSHOT = 0x0103
CMD_TEMPHUM_SNAPSHOT = 0x0104
CMD_RUN_MODE = 0x0105
CMD_FLOWMETER_SNAPSHOT = 0x0106
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
#: [为什么与 KEY_AUTO 并存] 两者是不同的问题：
#:   · `auto`        —— 现在是不是自动**模式**（人选的运行方式）
#:   · `autoRunning` —— 自动**控制器**有没有真的在跑（服务端实际状态）
#:
#: 正常实现下两者同步（进自动就启动控制器），但一旦出现"模式显示自动、
#: 控制器却没启动"（装配时序错、控制器崩过、将来做暂停/单步），只报 `auto`
#: 的界面会显示"自动在跑"而实际什么都不做 —— 这是最难查的一类假正常。
#: 分开报，界面才能把"模式=自动但控制器已停"如实呈现出来。
KEY_AUTO_RUNNING = "autoRunning"
KEY_DEV = "dev"
KEY_NAME = "name"
KEY_DI = "di"
KEY_DO = "do"
KEY_COUNT = "count"
#: 全量帧标志：True=全量（带通道名等"一次下发"字段，客户端据此重建点表并
#: 收名字）；False=周期增量帧（只带状态/数值）。
#: [2026-11-05 补齐] C 端服务端一直有这个键（HsfServerApp.c 的
#: hsfAppEncodeXxx(bool full)），Python 端与模拟器都没发 —— 跨端不一致：
#: 客户端只在 full=True 时解析名字，于是"点有了但全都没名字"。
KEY_FULL = "full"
KEY_VALID = "valid"
KEY_STALE_MS = "staleMs"
KEY_MASK = "mask"
KEY_VALUES = "values"
KEY_ON = "on"

# ---- 数字量通道名（服务端是名字的唯一事实源）----
#
# [为什么名字必须由服务端下发] 哪些口被电磁阀占用、每个口是"开到位"还是
# "关到位"，全是**现场接线**，客户端不该知道。名字在服务端由
# valve_bank 的点位映射**自动推导**（见 app.encode_digital），接线一改
# 客户端零改动就跟着变。
#
# 键名形如 "din16" / "don16"（通道号接在后面）：
#: DI 通道名键格式（%d = 通道号）。
KEY_DI_NAME_FMT = "din%d"
#: DO 通道名键格式（%d = 通道号）。
KEY_DO_NAME_FMT = "don%d"

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
TOPIC_VALVE_STATUS = 0x00010007

#: 阀动作超时告警主题（载荷 = 单条告警 BSON）。
#:
#: [为什么单独一个主题而不是塞进阀状态] 阀状态主题是**周期推送**（200ms
#: 一拍），超时告警是**事件**（发生才推一次）。混在一起有两个问题：
#:   ① 客户端每 200ms 收一帧重复告警，无法区分"新告警"与"仍在超时"；
#:   ② 只看数字量页/主界面的客户端没订阅阀主题，就收不到这条告警。
#: 故告警走独立主题，且**全推给所有已连接会话**（不要求订阅）——见
#: app._broadcast_valve_alarm。
TOPIC_VALVE_ALARM = 0x00010008


# ============================================================================
#                  浊度仪与进水泵（监视页"水质"区）
# ============================================================================

#: 浊度仪台数（两台，同一 485 口不同站号）。
HSF_TURBIDITY_COUNT = 2
#: 浊度仪快照请求（Response = 两台浊度 BSON）。载荷空。
CMD_TURBIDITY_SNAPSHOT = 0x0112
#: 浊度仪遥测周期推送主题（载荷 = 两台浊度 BSON）。
TOPIC_TURBIDITY_STATUS = 0x00010009

#: 进水泵台数（当前 1 台；结构上留多台位置）。
HSF_PUMP_COUNT = 1

#: 进水泵快照请求（Response = 泵状态 BSON）。载荷空。
CMD_PUMP_SNAPSHOT = 0x0113
#: 进水泵开关（Request 载荷 = {"index":0, "on":bool}；需 root + 手动模式）。
CMD_PUMP_CONTROL = 0x0114
#: 进水泵遥测周期推送主题（载荷 = 泵状态 BSON）。
TOPIC_PUMP_STATUS = 0x0001000A

# ---- 自动运行控制（⚠ 与"切模式"是两件事）----

#: 自动运行控制（Request 载荷 = {"op":int}；需 root；启动还须自动模式）。
#:
#: [为什么单独一条命令] 把"切到自动模式"和"开始自动运行"合成一个动作是
#: **危险的**：操作员只想切模式，设备就立刻被自动逻辑驱动。现场反馈
#: "默认应该是停止状态，不然太危险"。故：
#:
#:     CMD_RUN_MODE(mode=1) → 授权自动（远程控制被禁），控制器**仍停**
#:     CMD_AUTO_CONTROL(op=1) → 人才真正按下"开始自动运行"
#:
#: 同理，服务端重启后即使配置里是自动模式，控制器也**一律不自动启动**
#: （断电/升级重启不该让设备自己动起来）。
#:
#: [op 操作字（2026-11-05 C 侧对齐新增，向后兼容）] {"op":int} 四板斧：
#: 启动/停止/急停/复位；老载荷 {"on":bool} 仍受理（true≈op1 / false≈op2），
#: op 键存在时优先生效。急停锁存：复位（op=4）前拒绝启动，复位只回停止态。
CMD_AUTO_CONTROL = 0x0115

#: op 操作字：启动自动运行（须已处自动模式）。
AUTO_OP_START = 1
#: op 操作字：停止自动运行（永远放行）。
AUTO_OP_STOP = 2
#: op 操作字：急停（锁存；复位前拒绝启动；永远放行）。
AUTO_OP_ESTOP = 3
#: op 操作字：复位（解除急停锁存，回到停止态；不是重新启动）。
AUTO_OP_RESET = 4

#: 自动运行告警推送主题（载荷 = 单条告警 BSON {kind,atMs,text}；事件驱动）。
#:
#: [与 C 侧 XhsfLinkProto.h TOPIC_AUTO_ALARM 同号] 远程管理闭环：
#: 订阅本主题监视自动运行异常 + AUTO_CONTROL 远程启停/急停/复位。
#: 告警类型值与 C 侧 XAutoControlAlarmKind 逐值一致（AUTO_ALARM_*）。
TOPIC_AUTO_ALARM = 0x0001000B

#: 自动运行告警类型：动作下发失败。
AUTO_ALARM_ACTION_FAILED = 1
#: 自动运行告警类型：依赖设备不可用。
AUTO_ALARM_DEVICE_UNAVAILABLE = 2
#: 自动运行告警类型：逻辑自定义。
AUTO_ALARM_CUSTOM = 3
#: 自动运行告警类型：急停触发。
AUTO_ALARM_ESTOP = 4

# ---- 远程配置（采集项/采集频率/数据落盘；2026-11-05 新增）----

#: 读取服务端配置快照（无需载荷）。
#:
#: 应答 = {"ok":true,
#:         "collect": {…采集项开关与轮询周期…},
#:         "storage": {…数据落盘…}}
#: 与 CMD_CONFIG_SET 成对：设置页先 GET 回显当前值，改完 SET 提交。
CMD_CONFIG_GET = 0x0116

#: 设置服务端配置（**需 root**）。
#:
#: 载荷 = {"section":"collect",   …采集配置键…} 或
#:        {"section":"storage",   …落盘配置键…}
#:
#: [为什么按 section 分] 采集与落盘是两拨键，一次只提一节：客户端两
#: 张设置卡各自提交，服务端校验/生效范围也按节走（collect ⇒ 重启采集
#: 任务；storage ⇒ 重开/关闭数据库）。
#:
#: [生效口径] **在线生效 + 立即落盘**（cfg.save）：
#:   · collect：轮询周期下一拍生效（循环每拍重读）；启停开关经
#:     「重启采集任务」生效——被关的设备不再轮询，健康条目转数据过期
#:     （如实反映"不采了"，不是装作在线）。
#:   · storage：enabled/path/sample_ms 变更后重开（或关闭）数据库；
#:     历史/日志/告警随之落到新位置，旧文件不搬动。
#: 仅 Py 版（C 版可后补同号命令，语义见注释）。
CMD_CONFIG_SET = 0x0117

#: collect 节键：各设备采集开关（bool）。
CFGK_VFD_ENABLED = "vfdEnabled"
CFGK_TEMPHUM_ENABLED = "temphumEnabled"
CFGK_FLOWMETER_ENABLED = "flowmeterEnabled"
CFGK_TURBIDITY_ENABLED = "turbidityEnabled"
CFGK_ANALOG_ENABLED = "analogEnabled"
CFGK_DIGITAL_ENABLED = "digitalEnabled"
#: collect 节键：轮询周期（int 毫秒；服务端夹到 50~60000）。
CFGK_VFD_POLL_MS = "vfdPollMs"
CFGK_TEMPHUM_POLL_MS = "temphumPollMs"
CFGK_TURBIDITY_POLL_MS = "turbidityPollMs"
CFGK_ANALOG_POLL_MS = "analogPollMs"
CFGK_DIGITAL_POLL_MS = "digitalPollMs"
#: storage 节键：是否落盘（bool）/ 库文件路径（空串=程序目录）/ 采样周期。
CFGK_STORAGE_ENABLED = "enabled"
CFGK_STORAGE_PATH = "path"
CFGK_STORAGE_SAMPLE_MS = "sampleMs"

#: 轮询周期允许范围（毫秒）。下限 50 是总线的现实（该模块应答 0.4~0.8s，
#: 再快只是空转+退避刷日志）；上限防手滑填成小时级。
CFG_POLL_MS_MIN = 50
CFG_POLL_MS_MAX = 60000

# ---- 浊度仪键（v{index}_ 前缀，与阀/数字量同一风格）----

#: 浊度值 ×100（int；NTU × 100，避免浮点在 BSON 上的精度争议）。
KEY_TURB_NTU = "ntu"
#: 原始寄存器值（int；排查真相用，不受换算影响）。
KEY_TURB_RAW = "raw"
#: 该台是否在线（bool）。
KEY_TURB_ONLINE = "online"

# ---- 进水泵键 ----

#: 泵路号（int，0 起）。
KEY_PUMP_INDEX = "index"
#: 泵名（utf8）。
KEY_PUMP_NAME = "name"
#: 命令状态（bool；True=运行/开）。**只反映输出，无反馈信号**。
KEY_PUMP_ON = "on"
#: 该泵输出是否可控（点位已配置且模块在线）。
KEY_PUMP_READY = "ready"
#: 说明（utf8，可空）。
KEY_PUMP_TEXT = "text"

# ---- 阀告警键 ----

#: 本帧告警条数（int）。**有该键就说明这是告警帧**（不是周期状态帧）。
KEY_ALARM_COUNT = "ac"
#: 告警路号（int，0 起）。
KEY_ALARM_VALVE_INDEX = "vi"
#: 告警阀名（utf8）。
KEY_ALARM_VALVE_NAME = "vn"
#: 告警类型（int；见 VALVE_ALARM_*）。
KEY_ALARM_KIND = "k"
#: 告警文字（utf8，给操作员看）。
KEY_ALARM_TEXT = "t"
#: 发生时刻（int64 单调毫秒）。
KEY_ALARM_AT = "at"
#: 超时秒数（float；仅超时类有意义）。
KEY_ALARM_TIMEOUT_S = "to"

#: 告警类型：动作超时未到位。
VALVE_ALARM_TIMEOUT = 1
#: 告警类型：开/关到位信号同时有效（接线或阀体故障）。
VALVE_ALARM_CONFLICT = 2
#: 告警类型：阀恢复（超时后终于到位）。客户端据此消警。
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
#: [为什么需要] 服务端对重复点击的处理有三种不同结果：真的动了、原样重发
#: 但不重置计时、已到位什么都不做。若应答只回 ok=True，客户端无法区分
#: "动了"和"本来就在那儿"，用户会以为按钮坏了。见 valves.VALVE_CMD_*。
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
KIND_ANALOG_IO = 3
KIND_BUS = 4
#: 数字量模块（本 Python 版新增；客户端按 kind 分组，未知值应降级显示）。
KIND_DIGITAL_IO = 5
#: 电磁阀组（6 路；一个健康条目汇总整组）。
KIND_VALVE = 6
#: 水质仪表（浊度仪等；与"温湿度"分开）。
#:
#: [实锅 2026-11-05] 浊度仪初版复用了 KIND_TEMPHUM（=1），于是首页设备表的
#: "类型"列把两台浊度仪显示成**温湿度** —— 数据是对的，但类别完全误导
#: （现场会以为那是电箱里的温湿度变送器）。类别必须能表达"这是什么设备"，
#: 复用一个不相关的值等于在界面上撒谎。
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

#: 控制类命令所需最低角色（对齐 XLinkAuthRole_Admin）。
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
