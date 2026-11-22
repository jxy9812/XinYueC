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
# [四档角色——追加不重排] 0=匿名（只读）/ 1=普通用户（只读+下载）/
# 2=管理员（全权）/ 3=操作者（只读+下载+设备控制）。
# Operator 刻意取追加值 3 而不是插到 1/2 之间：账户库已落盘上线，
# 把 Admin 从 2 改成 3 会让存量 root（role=2）静默降级。故**禁止用数值
# 大小比较角色**，一律走 permissions 模块的权限点判定。
ROLE_ANONYMOUS = 0
ROLE_USER = 1
ROLE_ADMIN = 2
ROLE_OPERATOR = 3

# ---- 权限位（与 C 侧 XAccessPerm 逐值一致） --------------------------------
PERM_VIEW = 1 << 0             # 查看遥测/快照/历史
PERM_DEVICE_CONTROL = 1 << 1   # 设备控制（操作者+）
PERM_MODE_SWITCH = 1 << 2      # 手/自动切换（管理员）
PERM_AUTO_CONTROL = 1 << 3     # 自动运行启停（管理员）
PERM_ACCOUNT_ADMIN = 1 << 4    # 账户/服务端配置（管理员）
PERM_DOWNLOAD = 1 << 5         # 数据下载/导出（普通用户+；追加位）
PERM_ALL = 0x3F

# ---- 登录结果码（AuthResult 载荷首字节）---------------------------------
#
# 与 C 侧 `Src/XCore/XProtocol/XLink/XLinkProto.h` 的 `XLinkAuthStatus`
# 逐值一致。**语义分永久/瞬时两类**（2026-11-09 根修）：
#   · 永久：BAD_USER（用户不存在）/ BAD_PASSWORD / LOCKED / NO_CREDENTIAL
#           —— 客户端停自动重连，留在登录窗。
#   · 瞬时：REJECTED —— **登录策略**拒绝（同账并发「该账户已在线」/
#           会话数已达上限）。服务端（本文件所在实现 + C 侧
#           XLinkSessionAuthLookup_Rejected）必须用它，**不得**伪造成
#           BAD_USER/BAD_PASSWORD：客户端据此退避重试，旧会话一被回收即
#           自动恢复。若伪装成凭据错误，一次链路抖动会让已登录界面永久
#           全 `--`（实锅）。
#           TIMEOUT / PROTOCOL 同属瞬时（链路噪声）。
# 客户端镜像：`client_py/hsfsrv_client/proto.py` 同值同注释。
AUTHSTATUS_OK = 0
AUTHSTATUS_BAD_USER = 1
AUTHSTATUS_BAD_PASSWORD = 2
AUTHSTATUS_LOCKED = 3
AUTHSTATUS_REJECTED = 4
AUTHSTATUS_NO_CREDENTIAL = 5
AUTHSTATUS_TIMEOUT = 6
AUTHSTATUS_PROTOCOL = 7

#: 瞬时拒绝（REJECTED）后的自动重连上限（次数）。镜像 C 侧
#: `XLINK_config.h` 的 XLINK_AUTH_REJECT_RETRY_LIMIT（客户端消费；
#: 服务端仅登记以保持常量表同步）。
XLINK_AUTH_REJECT_RETRY_LIMIT = 30

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
#: [⚠ 口径订正 2026-11-09] 阀告警**实际并不走本主题**：与 C 侧
#: HsfServerApp.c hsfAppPushValveAlarms 同口径，告警随 0x00010007
#: （TOPIC_VALVE_STATUS）**合帧推送**——载荷带 "alarms" 数组 + "ac" 条数
#: 键（见 app._publish_valve_alarms），客户端按"载荷里有 alarms 就走告警
#: 分支"处理。本主题 0x00010008 为**遗留未用**：编号保留不回收（防止将来
#: 新主题复用此号造成新旧客户端错配），勿在其上启用新语义。
#:
#: [历史备注] 当初单独开主题的理由（周期推送与事件混推会刷屏/漏订阅）
#: 依然成立，只是落地方案改为"同主题内事件帧与状态帧按键空间区分"
#: （状态用 v{i}_* 前缀，事件用 alarms 数组），效果等价。
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
#: op 操作字：**选择自动模式**（只记粘性 + 持久化，不启动不动设备）——
#: 现场启动按钮用「上次选中的」（粘性）；客户端主界面的模式下拉在
#: activated 时发本 op 同步选择，使服务端重启/首启后的粘性与界面一致
#: （2026-11-13 板端实锅：物理启动键被拒「未选择自动模式」即粘性为空）。
AUTO_OP_SELECT = 5

#: 自动运行告警推送主题（载荷 = 单条告警 BSON {kind,atMs,text}；事件驱动）。
#:
#: [与 C 侧 XhsfLinkProto.h TOPIC_AUTO_ALARM 同号] 远程管理闭环：
#: 订阅本主题监视自动运行异常 + AUTO_CONTROL 远程启停/急停/复位。
#: 告警类型值与 C 侧 XAutoControlAlarmKind 逐值一致（AUTO_ALARM_*）。
TOPIC_AUTO_ALARM = 0x0001000B

#: 相机画面推送主题（2026-11-19 新增；与 C 侧 HSF_TOPIC_CAMERA_FRAME 同号）。
#:
#: [载荷是 RAW 二进制，不是 BSON] 两端 bson.py 只实现 0x01/0x02/0x03/0x04/
#: 0x08/0x0A/0x10/0x12，**binary(0x05) 遇未知类型直接 raise** ⇒ 画面字节
#: 只能走协议层的 DATATYPE_RAW。
#: [为什么必须分片] XLink 单帧载荷上限 16KB（XLINK_MAX_PAYLOAD）⇒ 一张
#: 704x704 的 JPEG（实测 41KB）塞不进；超限会让客户端 decode_header 返回
#: None ⇒ **整会话断链**（不是丢一帧）。⇒ 服务端切片推，客户端组帧。
TOPIC_CAMERA_FRAME = 0x0001000C

# ---- 相机画面分片格式（三端逐值一致，勿单端改）----
#:
#: 帧负载 = topicId(4B，XLink 既有规则) + 片头(16B) + 片数据。
#: 片头（**小端**，16 字节）：
#:   u16 magic   = 0x4341 ('CA')  防错帧
#:   u32 frameId 帧序号（同一帧所有片相同）
#:   u16 seq     片序号（从 0 开始）
#:   u16 total   本帧总片数
#:   u16 width   图像宽
#:   u16 height  图像高
#:   u16 flags   bit0=首片, bit1=末片
#:
#: [组帧规则，三端一致] ① seq==0 时丢弃同 frameId 的旧缓冲（覆盖式）；
#: ② 收齐 total 片且带末片标记 ⇒ 校验 JPEG 头尾(FFD8…FFD9)⇒ 交给显示层；
#: ③ **丢片即丢整帧**（下一帧 seq==0 到来时未收齐的旧缓冲直接丢，不等超时）；
#: ④ 服务端只发最新帧的分片，慢客户端自然丢帧、不堆积；
#: ⑤ **没人订阅相机主题时一帧都不推**（用户 2026-11-19 口径：只在客户端
#:    打开相机界面要看画面的时候才推送）⇒ 采集照常，推送按订阅门控。
CAM_FRAME_MAGIC = 0x4341
CAM_FRAME_HDR_LEN = 16
#: 单片数据上限：XLink 16KB 减去 topicId(4) 与片头(16)，留余量。
CAM_CHUNK_MAX = 12288
#: 分片片头**位标志**。
CAM_FRAME_FLAG_FIRST = 0x0001
CAM_FRAME_FLAG_LAST = 0x0002

#: 自动运行告警类型：动作下发失败。
AUTO_ALARM_ACTION_FAILED = 1
#: 自动运行告警类型：依赖设备不可用。
AUTO_ALARM_DEVICE_UNAVAILABLE = 2
#: 自动运行告警类型：逻辑自定义。
AUTO_ALARM_CUSTOM = 3
#: 自动运行告警类型：急停触发。
AUTO_ALARM_ESTOP = 4
#: 自动运行告警类型：模式序列阀动作超时（到位等不到）。2026-11-11 新增，
#: [与 client_py proto、C 侧 XhsfLinkProto.h HSF_AUTO_ALARM_VALVE_TIMEOUT
#: 已逐值同步——两端客户端的报警页落表消费它]。
AUTO_ALARM_VALVE_TIMEOUT = 5
#: 自动运行告警类型：**流量计报警**（上限/下限/空管/系统；含"进水泵开着
#: 仍报空管"的保护动作）。2026-11-15 新增，
#: [与 C 侧 XhsfLinkProto.h HSF_AUTO_ALARM_FLOWMETER 逐值同步]。
#:
#: [为什么要单开一类] 流量计的报警位此前只作为遥测里的一个位图
#: （TOPIC_FLOWMETER_STATUS 的 alarms 键）下发，界面在水质页显示文字，
#: **不进报警页、也不进 alarms 表** ⇒ 现场问"什么时候报的空管"查无实据。
#: 现在按**边沿**（报警位出现/消失）推一条事件，落进客户端报警表 + alarms 表。
AUTO_ALARM_FLOWMETER = 6

#: 自动运行告警类型：**设备故障**（变频器故障等设备级故障）。
#:
#: [2026-11-15 用户定案] 凡是**进了报警历史表**的故障级告警，都要：
#:   ① **锁存**（故障消失也不自动解锁，必须人工复位）；
#:   ② **自动停止设备**；
#:   ③ **必须手动复位**才允许再次启动设备（手动模式的远程控制同样被拒）。
#: 变频器故障此前只出现在设备健康里（首页设备表/报警页的健康行），
#: **不进报警历史表、也不锁存、不停机** —— 设备带着故障继续跑。
AUTO_ALARM_DEVICE_FAULT = 7
#: 自动运行告警类型：**压差过高**（运行保护的压差过大动作；2026-11-15）。
#: 与 client_py proto / C 侧 XhsfLinkProto.h HSF_AUTO_ALARM_PRESS_HIGH
#: 逐值同步。
AUTO_ALARM_PRESS_HIGH = 8

#: 告警帧里的**设备类别**键（int；proto.KIND_*）。
#:
#: [为什么加这个键] 告警帧原本只有 {kind,atMs,text} ⇒ 客户端落表时设备
#: 类别只能写死"自动运行" ⇒ 流量计报警那一行的"类型"列会显示"自动运行"，
#: 与现场实际设备不符。带上设备类别后，客户端按自己的 KIND_TEXT 表翻译，
#: 流量计告警显示成"流量计"。
#:
#: [缺键兼容] 缺该键 = 老服务端 ⇒ 客户端按"自动运行"处理（不崩、不改
#: 老行为）。键名与 C 侧 XhsfLinkProto.h HSF_KEY_AUTOALARM_DEV 同字面。
KEY_AUTOALARM_DEV = "dev"

# ---- 自动模式（产水/反洗/顺洗 + 预留「自动」组合；2026-11-11 新增）----
#
#: [两端同步点] 与 client_py/hsfsrv_client/proto.py 的同名常量逐值同步
#: （那边先落地，界面先行）；C 侧 XhsfLinkProto.h 待 C 端实现模式序列时
#: 一并补。config "auto" 节的存储与运行期消费在服务端（本文件常量 +
#: config.py AutoModeConfig + autocontrol.py 模式状态机）。
#: [C 端扩展键 2026-11-13] C 服务端的 auto 节另收发 "buttons" 子节
#: （按键触点类型 normallyClosed + 去抖 debounceMs，对齐 Python 的
#: buttons.items.normally_closed / button_debounce_ms 配置链）——Python
#: 服务端不经 0x0117 收发它（走自己的 cfg 文件）；GET 应答若带 buttons
#: 子节，Python 客户端按「应答缺键不误改」纪律忽略即可。
CFG_SECTION_AUTO = "auto"

#: 自动模式 id（协议稳定 id, 界面显示名）。顺序＝客户端下拉顺序。
#:
#: [这是「能运行 + 能选 + 能配」的集合] 首页「模式」下拉、服务端启动闸
#: （mode_config_error / op=5 选模式）、远端设置的配置块都按它。
#: **组合循环「自动（流量循环）」的子模式另见 AUTO_CYCLE_MODES**（三个，
#: 不含放水）——2026-11-18 用户订正：「放水模式不进自动模式」指的是**不进
#: 流量循环**，它本身是可以选来启动的普通自动模式。
AUTO_MODES = (
    ("produce", "产水模式"),
    ("backwash", "反洗模式"),
    ("rinse", "顺洗模式"),
    ("drain", "放水模式"),
)

#: **组合循环「自动（流量循环）」参与循环的子模式集合**（**缺省有序列表**：
#: 产水→反洗→顺洗）。
#:
#: [2026-11-18 从"口径声明"升级为"缺省列表"] 原先这里只是一份**口径声明**
#: （没人读它，循环链是硬编码的 _CYCLE_PRODUCE/_CYCLE_BACKWASH/_CYCLE_RINSE
#: 三常量）⇒ 现场想改参与循环的模式/顺序就得改代码。现在**参与循环的模式与
#: 顺序都可配**（auto.cycle 子节的 cycleModes），本常量降级为**缺省值**：
#:   · 缺省＝产水→反洗→顺洗（与旧硬编码行为一致 ⇒ 老配置零改动）；
#:   · 放水**可以**被配置进循环（旧的「循环里没有放水」只是缺省，不是禁令）。
#:
#: [有序] 第一个元素＝**主运行阶段**（按压差/定时兜底切换），其余元素按
#: 各自时长到期切换；最后一个的下一个＝第一个（闭环）。
AUTO_CYCLE_MODES = (
    ("produce", "产水模式"),
    ("backwash", "反洗模式"),
    ("rinse", "顺洗模式"),
)
#: 组合模式「自动」：已实现（2026-11-12 用户定案）——按流量循环调用上面
#: 三个模式（产水→（压差或定时兜底）→反洗→顺洗→产水，直到人按停止；
#: 停止/急停从记忆阶段继续，复位从头）。启动按循环配置校验（未启用/
#: 子模式缺设备明确拒并回原因，不静默、不假跑）。
#:
#: [2026-11-18 配置化] 循环的**参与模式与顺序改由配置决定**（见
#: CFG_AUTO_CYCLE_MODES / AUTO_CYCLE_MODES_DEF）——本行的三个模式只是
#: **缺省**参与列表，不是硬编码的链。
AUTO_MODE_AUTO = "auto"
AUTO_MODE_IDS = tuple(m for m, _t in AUTO_MODES) + (AUTO_MODE_AUTO,)

#: **参与循环的模式池**（＝ AUTO_MODES 的 id 集合：产水/反洗/顺洗/放水）。
#:
#: [为什么不是三模式] 2026-11-18 起「参与循环的模式」可配置 ⇒ 池子必须
#: 与**可配置模式集合**同源（AUTO_MODES），否则放水永远选不进来。
AUTO_CYCLE_MODE_POOL = tuple(m for m, _t in AUTO_MODES)

#: **可配置**的自动模式集合（远端设置「自动模式配置」卡的块）。
#:
#: [2026-11-18 订正] 与 AUTO_MODES **同源**（四个：产水/反洗/顺洗/放水）——
#: 用户先要求「放水模式不进自动模式」，现场看板上拉没有它 ⇒ 订正为：
#: 放水是**可配置、可选、可启动**的普通自动模式。
#: [2026-11-18 再订正] 「放水不进组合循环」也**不再是禁令**：循环的参与
#: 模式改为**可配置**（AUTO_CYCLE_MODE_POOL 里都能选），缺省列表里没有它
#: 只是缺省值（AUTO_CYCLE_MODES_DEF）。配置块与可运行集合同源 ⇒ 不必再拆
#: 两个表。
#:
#: [缺省口径] 放水模式的缺省**拷贝三模式**的缺省（不勾设备、搅拌电机
#: 1440rpm 正转、停止关阀复位=True、急停保持现状=False、两个换相
#: 策略=先停再启）——服务端 config._default_auto_mode() 一份代码生成，
#: 不另写第二份。
AUTO_CONFIG_MODES = AUTO_MODES
AUTO_CONFIG_MODE_IDS = tuple(m for m, _t in AUTO_CONFIG_MODES)

#: 模式内可勾选启动的设备（协议稳定 id, 界面显示名）。
#: valve1..6 = 6 台电动球阀（DO=i）；stirPump = 变频器搅拌泵；
#: inletPump = 进水泵（DO6）。
AUTO_DEVICES = (
    ("valve1", "电动球阀1"),
    ("valve2", "电动球阀2"),
    ("valve3", "电动球阀3"),
    ("valve4", "电动球阀4"),
    ("valve5", "电动球阀5"),
    ("valve6", "电动球阀6"),
    ("stirPump", "搅拌泵"),
    ("inletPump", "进水泵"),
)
AUTO_DEVICE_IDS = tuple(d for d, _t in AUTO_DEVICES)

#: 搅拌电机转速范围/缺省（rpm；铭牌额定点 50Hz≈1440rpm）。
AUTO_STIR_RPM_MIN = 0
AUTO_STIR_RPM_MAX = 1440
AUTO_STIR_RPM_DEF = 1440

#: 每模式键：**停止自动运行**时是否把电动球阀关回原位（复位）。
#:
#: [为什么可配 2026-11-12 用户定案] 停止有两种合理诉求：
#:   · 关阀复位（True）——设备回到安全位，适合"这一轮做完了、下一步换模式"；
#:   · 保持现状（False）——阀停在当前位（例如刚开着的产水阀留着等下一步
#:     继续用），避免每次停止都让阀白走一趟 10s 行程。
#: 缺省 True = 保持既有行为（停止即全停含关阀）。
AUTO_MODE_RESET_VALVES_ON_STOP = "resetValvesOnStop"
#: 每模式键：**急停**时是否把电动球阀关回原位（复位）。
#:
#: [缺省 False —— 与用户口径一致] 「按下急停后要停止所有设备（电动球阀
#: 可以不动）」：急停是**人身安全**事件，该停的是泵与变频器（切断动力）；
#: 阀可能正停在中间位，此时再叠一条"关"命令等于叠加一个未知动作，且
#: 阀卡在中间位本身要人工处理，急停时不必再动它。需要"急停也关阀"的
#: 现场可在本项勾选。
AUTO_MODE_RESET_VALVES_ON_ESTOP = "resetValvesOnEstop"
#: 两个开关的缺省（True=关阀复位 / False=保持现状）。
AUTO_MODE_RESET_VALVES_ON_STOP_DEF = True
AUTO_MODE_RESET_VALVES_ON_ESTOP_DEF = False

#: 每模式键：**搅拌泵**在换相（切到本模式）期间的处置策略。
#: 每模式键：**进水泵**在换相期间的处置策略。
#:
#: [为什么可配 2026-11-14 用户定案] 阀门切换要时间（真机行程 ~9s），
#: 这期间泵怎么办有三种合理诉求：
#:   · stopRestart —— **保持之前的逻辑**：换相先停泵，阀到位后（泵相）
#:     再按新模式配置启动（既有行为，缺省）；
#:   · carry      —— **保持上一个模式的状态**：换相途中该泵**不动**
#:     （运行中就继续运行、停了就继续停 ⇒ 物理状态＝上一模式的状态）；
#:   · direct     —— **直接切到当前模式的状态**：换相**立即**按新模式
#:     的目标启停该泵（与阀门切换并行；新模式要它开就现在开、不要就
#:     现在停）。
#: 策略只管**换相窗口内**；换相完成（进入泵相/运行相）后一律按当前模式
#: 的配置收敛。
#: [策略取自哪个模式] 取**即将进入的那个模式**（目标模式）的配置——
#: 「进入本模式时，泵怎么处理」；"上一个模式的状态"指的是物理现状
#: （上一个模式跑出来的状态），不是再读一次上一个模式的配置。
AUTO_MODE_STIR_SWITCH = "stirSwitchPolicy"
AUTO_MODE_INLET_SWITCH = "inletSwitchPolicy"

#: 换相策略取值（协议稳定 id，界面显示名）。顺序＝下拉顺序。
AUTO_SWITCH_POLICIES = (
    # [文案从简 2026-11-14] 原来「先停再启（保持既有逻辑）」这类长文案把
    # 下拉撑得极宽 ⇒ 两个下拉一行放不下（900px 窗下就溢出卡片）⇒ 改成
    # 短文案，完整含义放 tooltip（两端一致）。
    ("stopRestart", "先停再启"),
    ("carry", "保持上一模式"),
    ("direct", "直接切到当前模式"),
)
AUTO_SWITCH_POLICY_IDS = tuple(p for p, _t in AUTO_SWITCH_POLICIES)
#: 缺省策略＝保持既有逻辑（先停再启）。
AUTO_SWITCH_POLICY_DEF = "stopRestart"
#: 三个策略的单值别名（代码里按语义引用，别写字面量）。
AUTO_SWITCH_POLICY_STOP_RESTART = "stopRestart"
AUTO_SWITCH_POLICY_CARRY = "carry"
AUTO_SWITCH_POLICY_DIRECT = "direct"
#: 策略的显示名（服务端日志/应答用）。
AUTO_SWITCH_POLICY_TEXT = dict(AUTO_SWITCH_POLICIES)

#: 运行状态键（CMD_RUN_MODE 查询/0x0115 应答 extra；客户端主界面模式卡消费）。
#: 当前（选中/正在运行的）自动模式 id（空串=未选）。
KEY_AUTO_MODE = "autoMode"
#: 控制器相位：idle / paused（停止＝暂停）/ valves（阀动作中）/ pumps
#: （启动泵）/ running / estop。
KEY_AUTO_PHASE = "autoPhase"
#: **暂停态标志**（bool；2026-11-19 用户口径「停止＝暂停」）。
#:
#: True = 停止之后**记住了运行态**，下次按启动是**续跑**（同一模式、同一
#: 阶段、阶段计时接着走），而不是从头开始；只有按过**复位**（屏幕上的或
#: 实体的）才清零。False = 没有可续跑的暂停态（没跑过 / 已复位 / 停了又
#: 启动了别的模式）。
#:
#: [与 autoPhase 的分工] autoPhase="paused" 已能表达"暂停中"；本键是
#: **显式布尔**，客户端不必去认相位字符串（老客户端不认 "paused" 也不
#: 会崩，只是显示不出"暂停中"）。
KEY_AUTO_PAUSED = "autoPaused"
#: **暂停时记住的模式**（str；组合循环时＝子阶段；单模式时＝模式本身；
#: 空串 = 没有暂停态）。
KEY_AUTO_PAUSE_MODE = "autoPauseMode"
#: 报警锁存（bool）：模式序列报警（阀超时等）后必须复位才能再启动。
KEY_AUTO_FAULT = "autoFault"
#: **累计自动运行时长**（毫秒；2026-11-12 用户定案：主界面显示
#: 「自动运行 天 时 分 秒」）。只在自动运行（running）期间累加，停止后
#: 冻结并保留，**多次运行是累计**。服务端计时（唯一事实源）——客户端
#: 断线/重启会归零，不能在客户端计。
KEY_AUTO_RUN_MS = "autoRunMs"

#: **当前子阶段（模式）已运行时长**（毫秒；2026-11-12 用户定案：主界面
#: 自动模式卡显示「剩余时间」表盘）。组合模式（auto）下以**本阶段**
#: 计时起点算（阀相→泵相→运行相都在内），换相即归零重计；-1 = 不在
#: 自动运行/没有阶段（不可知）。
#:
#: [为什么服务端算] 「本阶段还剩多久」取决于服务端的循环配置快照
#: （反洗/顺洗分钟数、产水定时兜底），客户端拿的配置可能已过期；
#: 而且换相时刻只有服务端知道。客户端只做显示，不做倒计时推算。
KEY_AUTO_STAGE_ELAPSED_MS = "autoStageElapsedMs"
#: **当前子阶段的总时长**（毫秒；0 = 本阶段没有定时，不可知）。
#: 反洗/顺洗 = 配置的分钟数；产水 = **仅定时兜底开启时**才有（产水默认
#: 按压差切换，剩余时间本来就不可知 —— 不编造倒计时）。
#: 客户端按 elapsed/total 画表盘；total=0 时只显示「已运行」。
KEY_AUTO_STAGE_TOTAL_MS = "autoStageTotalMs"

#: **最近一条报警**（2026-11-19 用户定案「主页加一张最近报警卡」）。
#:
#: 值 = {"kind":<来源类别 str>, "dev":<设备类别 int|缺>,
#:        "text":<告警原文 str>, "atMs":<epoch 毫秒 int>}
#: **无报警时为 null**（不是空文档 —— 空文档与"没这个键"分不开）。
#:
#: [为什么放在状态快照里] 报警历史本来走 0x011A 分页命令（客户端报警页
#: 用），但主页要的是**一行**摘要 ⇒ 每次状态轮询多带一份最近行即可，
#: 不必让主页再发一条分页命令（1s 轮询已经在了）。
#:
#: [为什么服务端取] 报警是**服务端记**的（alarms 表，单一事实源；
#: 客户端与网页端都取同一份）⇒ "最近一条"只能服务端取 —— 客户端本地
#: 没有副本（2026-11-17 起本地不留副本）。
#:
#: 三端键名逐字一致：client_py/hsfsrv_client/proto.py 的
#: KEY_LAST_ALARM、webui_py/hsfweb/proto.py 的 KEY_LAST_ALARM。
KEY_LAST_ALARM = "lastAlarm"
#: lastAlarm 对象内的键（与服务端 alarms 表的列同源）。
LAST_ALARM_KEY_KIND = "kind"      #: 来源类别（health/auto/valve/device/…）
LAST_ALARM_KEY_DEV = "dev"        #: 设备类别（proto.KIND_*；缺 = 无）
LAST_ALARM_KEY_TEXT = "text"      #: 告警原文
LAST_ALARM_KEY_AT_MS = "atMs"     #: 事件时刻（epoch 毫秒，墙钟）

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
#: [2026-11-11 两端同口径] C 端（HsfServerIo.c applyCollectConfig）也已
#: **运行期消费**这 8 个开关：485 三台停用=从站最小发送间隔顶天文数字、
#: 模拟量/数字量停用=设备轮询间隔 0 ——与本端 start_polling 的 enable_*
#: 同语义（远程改后下一拍生效）；缺键回落装配事实（该路/该站装了就开）。
CFGK_VFD_ENABLED = "vfdEnabled"
CFGK_TEMPHUM_ENABLED = "temphumEnabled"
CFGK_FLOWMETER_ENABLED = "flowmeterEnabled"
CFGK_TURBIDITY_ENABLED = "turbidityEnabled"
#: [2026-11-10 逐台开关] 两台浊度仪是两台独立设备（站1/站3），开关各自可设；
#: 与 C 端 XhsfLinkProto.h 的 HSF_CFGK_TURBIDITY1_ENABLED / _TURBIDITY2_ 同值。
#: 老的 turbidityEnabled 保留为两台共同缺省（缺键时回落它）。
CFGK_TURBIDITY1_ENABLED = "turbidity1Enabled"
CFGK_TURBIDITY2_ENABLED = "turbidity2Enabled"
CFGK_ANALOG_ENABLED = "analogEnabled"
CFGK_DIGITAL_ENABLED = "digitalEnabled"
#: collect 节键：轮询周期（int 毫秒；服务端夹到 50~60000）。
CFGK_VFD_POLL_MS = "vfdPollMs"
CFGK_TEMPHUM_POLL_MS = "temphumPollMs"
CFGK_TURBIDITY_POLL_MS = "turbidityPollMs"
#: [2026-11-10 逐台] 两台浊度仪 + 流量计各自可设采集周期；与 C 端
#: XhsfLinkProto.h 的 HSF_CFGK_TURBIDITY1_POLL_MS / _TURBIDITY2_ / _FLOWMETER_
#: 逐值同步。老的 turbidityPollMs 仍作两台共同缺省（向后兼容）。
CFGK_TURBIDITY1_POLL_MS = "turbidity1PollMs"
CFGK_TURBIDITY2_POLL_MS = "turbidity2PollMs"
CFGK_FLOWMETER_POLL_MS = "flowmeterPollMs"
CFGK_ANALOG_POLL_MS = "analogPollMs"
CFGK_DIGITAL_POLL_MS = "digitalPollMs"
# collect 节键：压差计/温度传感器的通道号与量程上下限（2026-11-20 远端
# 可配；二改：量程=下限~上限，4mA→下限、20mA→上限，「温度满量程」废弃——
# 用户口径「只要填上限和下限即可，范围自己算」，压差计同样可配）。通道号
# 0..模块路数-1。两端逐值同步。
#: 模拟量模块路数缺省（通道下拉条目数；真值以 GET 的 analogChannels 为准）。
ANALOG_CHANNELS_DEF = 8
CFGK_ANALOG_PRESS_CHANNEL = "pressChannel"
#: 压差计量程下限/上限（MPa；4mA→下限、20mA→上限；缺省 0~1）。
CFGK_ANALOG_PRESS_LOW_MPA = "pressLowMpa"
CFGK_ANALOG_PRESS_HIGH_MPA = "pressHighMpa"
#: 「4mA 以下有效」（0~4mA 段按线性延续显示，可适配更多传感器）：
#: 压差计缺省**开**（4mA 以下是真负压，既有特殊处理）；温度缺省**关**
#: （低于量程按欠量程钳到下限，保护不误动作）。
CFGK_ANALOG_PRESS_BELOW4_VALID = "pressBelow4Valid"
CFGK_ANALOG_TEMP_BELOW4_VALID = "tempBelow4Valid"
CFGK_ANALOG_TEMP_CHANNEL = "tempChannel"
#: 温度传感器量程下限/上限（℃；4mA→下限、20mA→上限；缺省 0~100）。
CFGK_ANALOG_TEMP_LOW_C = "tempLowC"
CFGK_ANALOG_TEMP_HIGH_C = "tempHighC"
#: 量程上下限界（越界 SET 明确报错，不静默夹；下限 < 上限另做交叉校验）。
CFGK_ANALOG_PRESS_LOW_MPA_MIN = -10.0
CFGK_ANALOG_PRESS_LOW_MPA_MAX = 100.0
CFGK_ANALOG_PRESS_HIGH_MPA_MIN = 0.001
CFGK_ANALOG_PRESS_HIGH_MPA_MAX = 100.0
CFGK_ANALOG_TEMP_LOW_C_MIN = -100.0
CFGK_ANALOG_TEMP_LOW_C_MAX = 100.0
CFGK_ANALOG_TEMP_HIGH_C_MIN = -50.0
CFGK_ANALOG_TEMP_HIGH_C_MAX = 500.0
#: storage 节键：是否落盘（bool）/ 库文件路径（空串=程序目录）/ 采样周期。
CFGK_STORAGE_ENABLED = "enabled"
CFGK_STORAGE_PATH = "path"
CFGK_STORAGE_SAMPLE_MS = "sampleMs"
#: 容量限额（现场硬盘小，必须能封顶）。
#:   maxMb      容量上限（int MB；0 = 不限）
#:   fullPolicy 满额策略："stop"=到限停写 / "ring"=循环覆盖 / "rotate"=分段滚动
#:   keepFiles  rotate 模式保留的分段数（含当前库，1..99）
CFGK_STORAGE_MAX_MB = "maxMb"
CFGK_STORAGE_FULL_POLICY = "fullPolicy"
CFGK_STORAGE_KEEP_FILES = "keepFiles"
#: 满额策略取值与界面文本（与客户端 proto 同表；界面按此顺序出下拉）。
STORAGE_POLICIES = (
    ("ring", "循环覆盖（删最旧，长期无人值守）"),
    ("stop", "到限停写（保留已有数据，只读）"),
    ("rotate", "分段滚动（写满换文件，按段归档）"),
)
STORAGE_POLICY_KEYS = tuple(k for k, _t in STORAGE_POLICIES)

#: storage 节键：**历史入库的触发方式**（2026-11-12 用户定案「在远端增加
#: 设置历史输入入库，触发方式是持续采集还是自动运行采集」）。
#:
#: [两种触发方式]
#:   · always（缺省）：**持续采集** —— 只要落盘开着，按入库周期一直记，
#:     与自动模式开没开无关（保持既有行为）。
#:   · auto：**仅自动运行时采集** —— 只有自动控制器真的在跑（running）时
#:     才入库；手动/停机期间不入库（历史里只有"设备在自动生产时"的时段）。
#:     注意：这只影响**落盘**，遥测采集与界面推送照旧（界面数据不缺）。
CFGK_STORAGE_HISTORY_TRIGGER = "historyTrigger"
HISTORY_TRIGGER_ALWAYS = "always"
HISTORY_TRIGGER_AUTO = "auto"
#: 下拉项（键, 文案）——界面与服务端同序。
HISTORY_TRIGGERS = (
    (HISTORY_TRIGGER_ALWAYS, "持续采集入库（与自动模式无关）"),
    (HISTORY_TRIGGER_AUTO, "仅自动运行时入库（停机期间不记）"),
)
HISTORY_TRIGGER_KEYS = tuple(k for k, _t in HISTORY_TRIGGERS)

# ---- 运行保护（protect 节；2026-11-15 用户定案）----
#
#: [这一节是什么] 设备侧的**保护联锁**：进水泵开着、流量计却一直报"空管"
#: ⇒ 说明水路没通（或流量计坏了）⇒ 到时限就按配置动作（停设备 / 只报警）。
#:
#: [为什么单开一节] collect 节是"采哪些、多快采"，storage 是"落盘"，
#: auto 是"工艺序列"——保护联锁不属于任何一类 ⇒ 单开 protect 节。
#: [两端同步点] 与 C 侧 XhsfLinkProto.h HSF_CFG_SECTION_PROTECT /
#: HSF_CFGK_PROTECT_* 逐字同值。
CFG_SECTION_PROTECT = "protect"

#: protect 节键：**进水泵空管超时**（int 毫秒）。
#:
#: [判据] 进水泵开着（DO6=1）且流量计仍报空管，**连续**这么久 ⇒ 保护动作。
#:   · **0 = 关闭该保护**（不判超时，什么都不做；**缺省也是 0**）；
#:   · 设了值才启用：现场流量计的空管位可能长期挂着（实测板端 3.6 m³/h
#:     时也带空管）⇒ 缺省启用会"每次开泵一分钟就停线"。
#:
#: [读不到 ≠ 空管] 流量计离线/数据不新鲜时**不判定**（不累计、不动作）
#: —— 拿"没数据"当"空管"会把整条线停掉。
CFGK_PROTECT_INLET_NOFLOW_TIMEOUT_MS = "inletNoFlowTimeoutMs"
#: protect 节键：**超时后是否停止设备**（bool）。
#:
#: [为什么还要一个开关] 现场流量计的"空管"位可能长期挂着（实测板端读数
#: 3.6 m³/h 时也带空管报警）⇒ 一律停机会把线停死。给了开关：只报警
#: （先观察）还是直接停（真断水/仪表坏时保护设备），由现场自己选。
CFGK_PROTECT_INLET_NOFLOW_STOP = "inletNoFlowStop"

#: 缺省值（缺键 = 用缺省；0 仍是合法值 ⇒ 判"关闭保护"时用 `is None` 区分）。
#:
#: [缺省 = 0（**关闭该保护**）] 现场实测：板端流量计在**有流量**（3.6 m³/h）
#: 时也一直挂着"空管"报警位（见 AGENTS「板端 ttyS4 实机读数 …/空管」）⇒
#: 若缺省就启用、时限又短，现场每次开泵一分钟就会把线停掉 ⇒ 缺省
#: **不启用**，由现场自己设好时限再打开（这是"保护"，不是"默认动作"）。
PROTECT_INLET_NOFLOW_TIMEOUT_MS_DEF = 0
PROTECT_INLET_NOFLOW_TIMEOUT_MS_MIN = 0
PROTECT_INLET_NOFLOW_TIMEOUT_MS_MAX = 3600000
PROTECT_INLET_NOFLOW_STOP_DEF = True

#: protect 节键：**压差过大保护**是否启用（bool；缺省 false = 不启用）。
#:
#: [判据] 压差（模拟量通道 0 换算出的 MPa）**连续**超过 `pressHighLimitMpa`
#: 达 2 秒 ⇒ 触发（限流阀被堵/水路异常 ⇒ 泵在憋压，继续跑会伤设备）。
#: [为什么要有"启用"开关] 压差上限与现场工艺强相关（滤芯阻力、清洗周期
#: 都会改），写死一个值必然误动作 ⇒ 由现场自己定阈值并决定开不开。
CFGK_PROTECT_PRESS_HIGH_ENABLED = "pressHighEnabled"
#: protect 节键：**压差上限**（double；MPa）。压差 > 此值 ⇒ 判"过大"。
#: 缺省 0.8 MPa（实测正常运行压差约 0.5 MPa ⇒ 留一倍余量）。
CFGK_PROTECT_PRESS_HIGH_LIMIT_MPA = "pressHighLimitMpa"
#: protect 节键：压差过大触发后是否**停止设备**（bool；缺省 true = 停）。
#: 只报警 = 先观察趋势、不打断生产。
CFGK_PROTECT_PRESS_HIGH_STOP = "pressHighStop"
#: 缺省值（0.8 MPa = 正常运行 0.5 MPa 的约 1.6 倍余量；缺省**不启用**）。
PROTECT_PRESS_HIGH_LIMIT_MPA_DEF = 0.8
PROTECT_PRESS_HIGH_LIMIT_MPA_MIN = 0.0
PROTECT_PRESS_HIGH_LIMIT_MPA_MAX = 10.0
PROTECT_PRESS_HIGH_ENABLED_DEF = False
PROTECT_PRESS_HIGH_STOP_DEF = True
#: 压差过大触发前的**持续时长**（毫秒）：滤芯堵塞是分钟级过程，但水锤/
#: 启泵瞬间会有尖峰 ⇒ 要求连续超过该时长才动作，避免一次尖峰停线。
PROTECT_PRESS_HIGH_DEBOUNCE_MS = 2000

#: protect 节键：**电机超温保护**（2026-11-20 用户口径「大于多少度停止
#: 设备并报电机超温故障，可禁用」）。判据 = 模拟量温度传感器（CH1，
#: 通道号远端可配）工程量 **连续超过阈值**（去抖同压差保护 2s）⇒
#: 停止设备（同现场停止按钮）+ 报警（kind=电机超温，进 alarms 表 +
#: TOPIC_AUTO_ALARM）+ **报警锁存**（温度回落后不自动恢复，必须人工
#: 复位才能再启动）。**可禁用**：enabled=false 时完全不判定。
CFGK_PROTECT_MOTOR_OVER_TEMP_ENABLED = "motorOverTempEnabled"
#: protect 节键：**电机超温阈值**（double；℃）。连续超此值达去抖窗 ⇒ 动作。
#: 缺省 75℃（电机绝缘 B 级耐受 ~130℃，留一倍余量；现场可按电机铭牌调）。
CFGK_PROTECT_MOTOR_OVER_TEMP_LIMIT_C = "motorOverTempLimitC"
#: protect 节键：电机超温触发后是否**停止设备**（bool；缺省 true = 停）。
CFGK_PROTECT_MOTOR_OVER_TEMP_STOP = "motorOverTempStop"
#: 缺省值（75℃ 常用电机报警阈值；缺省**不启用**——与压差保护同口径，
#: 由现场自己定阈值并决定开不开）。
PROTECT_MOTOR_OVER_TEMP_LIMIT_C_DEF = 75.0
PROTECT_MOTOR_OVER_TEMP_LIMIT_C_MIN = 0.0
PROTECT_MOTOR_OVER_TEMP_LIMIT_C_MAX = 200.0
PROTECT_MOTOR_OVER_TEMP_ENABLED_DEF = False
PROTECT_MOTOR_OVER_TEMP_STOP_DEF = True
#: 电机超温去抖窗（毫秒）＝压差保护同款。
PROTECT_MOTOR_OVER_TEMP_DEBOUNCE_MS = 2000
#: 电机超温告警 kind（alarms 表 / TOPIC_AUTO_ALARM 的 kind 键）。
#: 与既有 6 类并列（1 泵空管 / 2 压差过大 / 3 阀超时 / 4 动作失败 /
#: 5 变频器故障 / 6 流量计告警）。
PROTECT_ALARM_KIND_MOTOR_OVER_TEMP = 8

# ---- 相机（网络相机取流；2026-11-19 新增）----
#
#: [这一节是什么] 采集一路**网络相机**（TP-LINK IPC 之类）的画面：服务端自己
#: 用 RTSP 拉流 → 板端硬解 → 抽帧成 JPEG → **分片**推给客户端
#: （TOPIC_CAMERA_FRAME）。客户端**一律不直连相机**——外网访问场景里浏览器
#: 在公网、相机在内网，只有服务端能同时够到两端。
#:
#: [为什么单开一节] collect/storage/auto/protect 都是「本机设备侧」的配置；
#: 相机是**另一类外部设备**（自带账号口令、自带码流档）⇒ 单开 camera 节。
#:
#: [两端同步点] 与 C 侧 XhsfLinkProto.h HSF_CFG_SECTION_CAMERA /
#: HSF_CFGK_CAMERA_* 逐字同值。
CFG_SECTION_CAMERA = "camera"

#: camera 节键：**总开关**（bool；缺省 false = 不采集）。
#: [为什么缺省关] 相机是**可选外部设备**，现场未必装 ⇒ 不开就不起采集进程。
CFGK_CAMERA_ENABLED = "enabled"
#: camera 节键：相机 **IP**（str）。
CFGK_CAMERA_IP = "ip"
#: camera 节键：**RTSP 端口**（int；缺省 554）。
CFGK_CAMERA_PORT = "port"
#: camera 节键：**账号**（str）。
CFGK_CAMERA_USERNAME = "username"
#: camera 节键：**口令**（str）。
#: [安全] 落盘与 GET 应答都要**脱敏**（不回显明文）；日志里只出现账号。
CFGK_CAMERA_PASSWORD = "password"
#: camera 节键：**码流档**（"main" 主码流 / "minor" 子码流）。
CFGK_CAMERA_STREAM = "stream"
#: camera 节键：**采集帧率**（int）。
CFGK_CAMERA_FPS = "fps"
#: camera 节键：**输出帧宽**（int；等高缩放）。
CFGK_CAMERA_WIDTH = "width"
#: camera 节键：**JPEG 质量**（int，1~100，**越大越清晰**、文件越大）。
#: [口径 = GStreamer jpegenc 的 quality]（与板端 mppjpegenc 的 q-factor 同向；
#: **与 ffmpeg 的 -q:v 方向相反** —— ffmpeg 是 1~31 越小越好 ⇒ 两套语义混用
#: 会得到"调大反而更糊"的怪现象）。
#: 实测（704² 源，板端）：quality=70→78KB、quality=90→138KB；
#: 同体积下**软编画质优于硬编**（mppjpegenc q95 只有 39KB 但画面块状明显）。
CFGK_CAMERA_QUALITY = "quality"
#: camera 节键：**握手/重连超时**（毫秒）。
CFGK_CAMERA_TIMEOUT_MS = "timeoutMs"

#: 缺省值（缺键 = 用缺省）。
CAMERA_ENABLED_DEF = False
CAMERA_IP_DEF = ""
CAMERA_PORT_DEF = 554
CAMERA_USERNAME_DEF = "admin"
CAMERA_PASSWORD_DEF = ""
CAMERA_STREAM_DEF = "minor"
CAMERA_FPS_DEF = 2
CAMERA_WIDTH_DEF = 704
CAMERA_QUALITY_DEF = 80
CAMERA_TIMEOUT_MS_DEF = 5000

#: 码流档（协议稳定 id）。
CAMERA_STREAM_MAIN = "main"
CAMERA_STREAM_MINOR = "minor"
#: [码流档取值集合] 非法值 fail-closed 拒绝（照 auto/protect 节的纪律）。
CAMERA_STREAM_CHOICES = (CAMERA_STREAM_MAIN, CAMERA_STREAM_MINOR)

#: [帧率档位＝跟随相机自身档位] 实测本相机（TP-LINK IPC）能力表的
#: frame_rates = [65537, 65546, 65551, 65556]，即 65536+fps ⇒ **1/10/15/20**
#: 四档，**相机没有 25/30fps** ⇒ 采集帧率只在这四档里选（也可低于相机帧率
#: 做抽帧，例如在 20fps 的流上取 2fps）。
CAMERA_FPS_CHOICES = (1, 10, 15, 20)
#: 采集帧率取值范围（1~30；超过相机档位时按相机实际帧率，不报错）。
CAMERA_FPS_MIN = 1
CAMERA_FPS_MAX = 30
#: 输出帧宽范围（px）。
CAMERA_WIDTH_MIN = 128
CAMERA_WIDTH_MAX = 4096
#: JPEG 质量范围（1~100，与 GStreamer jpegenc 的 quality 同口径）。
CAMERA_QUALITY_MIN = 1
CAMERA_QUALITY_MAX = 100

#: [2026-11-19 架构转向：画面直传] camera 节键：**传输模式**
#: "stream"（**缺省**，相机 H.264/H.265 子码流不解码直接转发，解码在客户端
#: ⇒ 服务端不再启 gst，CPU 从 ~90% 单核归零、帧率从 2fps 上到相机自身
#: 20fps）/ "jpeg"（旧路：服务端 gst 硬解→软编 JPEG，作**兜底**保留——
#: 老客户端/排查问题时可切回）。
CFGK_CAMERA_TRANSPORT = "transport"
CAMERA_TRANSPORT_STREAM = "stream"
CAMERA_TRANSPORT_JPEG = "jpeg"
CAMERA_TRANSPORT_CHOICES = (CAMERA_TRANSPORT_STREAM, CAMERA_TRANSPORT_JPEG)
CAMERA_TRANSPORT_DEF = CAMERA_TRANSPORT_STREAM

#: camera 节键：**期望的编码**（"h264" 缺省 / "h265"）。外网访问带宽紧时
#: 切 h265（子码流 512K → ~256K）。服务端从 SDP 探测**实际**编码并随
#: 状态回传（actualCodec）——配置与实际不一致时照转不误（转发本来就不
#: 关心编码），只是客户端要能解。
CFGK_CAMERA_CODEC = "codec"
CAMERA_CODEC_H264 = "h264"
CAMERA_CODEC_H265 = "h265"
CAMERA_CODEC_CHOICES = (CAMERA_CODEC_H264, CAMERA_CODEC_H265)
CAMERA_CODEC_DEF = CAMERA_CODEC_H264

#: camera 节键：**客户端解码方式**（远端设置相机卡可编辑；管理员一处配置
#: 全部客户端统一行为）："auto"（**缺省**，按机器 GPU 依次试 qsv/cuvid/amf，
#: 都不行回落软解）/ "on"（强制硬解，硬解不可用则画面异常并在状态行明示）/
#: "off"（强制软解）。
CFGK_CAMERA_HW_DECODE = "hwDecode"
CAMERA_HW_AUTO = "auto"
CAMERA_HW_ON = "on"
CAMERA_HW_OFF = "off"
CAMERA_HW_CHOICES = (CAMERA_HW_AUTO, CAMERA_HW_ON, CAMERA_HW_OFF)
CAMERA_HW_DEF = CAMERA_HW_AUTO
# ---- 连接方式 / MAC / 断线重连（2026-11-20 用户口径）----
#: 连接方式：按 IP 直连（缺省）/ 按 MAC（相机是 DHCP 动态 IP，IP 可能变——
#: 服务端每次连接前把 MAC 解析成当前 IP：先查本机 ARP 表，查不到就对
#: 候选网段做一次 TCP 探测把 ARP 表喂起来再查）。
CFGK_CAMERA_RESOLVE_MODE = "resolveMode"
CAMERA_RESOLVE_IP = "ip"
CAMERA_RESOLVE_MAC = "mac"
CAMERA_RESOLVE_CHOICES = (CAMERA_RESOLVE_IP, CAMERA_RESOLVE_MAC)
CAMERA_RESOLVE_DEF = CAMERA_RESOLVE_IP
#: 相机 MAC（AA:BB:CC:DD:EE:FF；大小写/连字符宽容，存盘归一成小写冒号）。
CFGK_CAMERA_MAC = "mac"
#: 断线重连：开关 + 重连间隔（秒）。关 = 连不上/掉线后**停在那里**报错
#: （人工再「应用」才重试）；开 = 每隔间隔秒重试一次（不再指数退避）。
CFGK_CAMERA_RECONNECT_ENABLED = "reconnectEnabled"
CFGK_CAMERA_RECONNECT_INTERVAL_S = "reconnectIntervalS"
CAMERA_RECONNECT_ENABLED_DEF = True
CAMERA_RECONNECT_INTERVAL_S_DEF = 5
CAMERA_RECONNECT_INTERVAL_S_MIN = 1
CAMERA_RECONNECT_INTERVAL_S_MAX = 3600

#: 分片 flags 扩展位（与 C 头 HSF_CAM_FRAME_FLAG_* / 两端客户端 proto 逐值同步）：
#: bit2 = 载荷是**编码流 NAL**（Annex-B，含 00000001 起始码；frameId=NAL 序号，
#: width/height=0）；bit3 = 该编码流是 **H.265**（bit2=0 时恒 0 = JPEG）。
CAM_FRAME_FLAG_ES = 0x0004
CAM_FRAME_FLAG_H265 = 0x0008

# ---- 自动循环（组合模式「自动」的配置；2026-11-12 用户定案）----
#
#: [循环顺序 · 2026-11-18 起可配] 缺省＝产水 →（压差或定时兜底触发）→
#: 反洗 →（定时）→ 顺洗 →（定时）→ 产水 ……；但**参与循环的模式与顺序
#: 由配置决定**（见 CFG_AUTO_CYCLE_MODES）：列表第一个＝主运行阶段（按
#: 压差/定时兜底切换），其余按各自时长到期切换，最后一个的下一个＝第一个。
#:
#: [键的位置] auto 配置节内的 "cycle" 子节（与 "modes" 平级）。
CFG_AUTO_CYCLE = "cycle"
#: 循环总开关（关掉则组合模式「自动」启动被拒：循环配置校验口径）。
CFG_AUTO_CYCLE_ENABLED = "enabled"
#: **主运行阶段**（列表第一个）切换到下一阶段的**压差阈值**（MPa；现场
#: 压差计量程见模拟量配置）。
CFG_AUTO_CYCLE_PRESS_THRESHOLD = "pressThreshold"
#: 比较方向：压差 **大于** 阈值才切 / **小于** 阈值才切。
CFG_AUTO_CYCLE_PRESS_COMPARE = "pressCompare"
#: 是否启用**定时器兜底**（默认关）：启用后即使压差一直不满足，主运行阶段
#: 到阈值时间也强制切下一阶段（防止压差计坏掉导致流程卡死）。
CFG_AUTO_CYCLE_TIMER_FALLBACK = "timerFallback"
#: 主运行阶段的兜底定时（**分钟**）。
CFG_AUTO_CYCLE_TIMER_MINUTES = "timerMinutes"
#: 反洗运行时间（**分钟**）——**旧键**，保留是为了老读方不受影响；新的
#: 各阶段时长统一走 CFG_AUTO_CYCLE_STAGE_MINUTES（保存时两套都写）。
CFG_AUTO_CYCLE_BACKWASH_MINUTES = "backwashMinutes"
#: 顺洗运行时间（**分钟**）——同上（旧键）。
CFG_AUTO_CYCLE_RINSE_MINUTES = "rinseMinutes"
#: **参与循环的模式列表**（有序的字符串数组；元素＝模式 id）。
#:
#: [为什么必须保序] 列表的**顺序就是循环的顺序**（第一个＝主运行阶段）⇒
#: 必须用 list（用 set 会丢顺序 ⇒ 循环顺序随机）。
#: [SET 是 fail-closed 的] 类型错 / 未知模式 / 重复模式 / 空列表 ⇒ 明确
#: 报错，不静默接受（见 app._cfg_set_auto_cycle）。
CFG_AUTO_CYCLE_MODES = "cycleModes"
#: **各阶段时长**（对象：{模式 id: 分钟}）。
#:
#: [为什么新增这张表] 原来的 backwashMinutes/rinseMinutes 只管两个固定
#: 模式；参与列表可配之后，**每个**阶段都要能配自己的时长。
#: [向后兼容] 配置里**没有**本键时，由旧的 backwashMinutes/rinseMinutes
#: 合成（{backwash:…, rinse:…}）；表里没写的模式按缺省 10 分钟。
#:
#: [2026-11-18 起为**兼容视图**] 真正的"每阶段切换条件"改由 stageCfg
#: 承担（下面）——`stageMinutes` 只是 stageCfg 里 timerMinutes 的**派生
#: 视图**（服务端 GET 时由 stageCfg 算出、SET 时折进 stageCfg）⇒
#: **单一事实源**仍是 stageCfg，这个键只服务已经上线的旧客户端界面
#: （它只认"每阶段多少分钟"这一个概念）。
CFG_AUTO_CYCLE_STAGE_MINUTES = "stageMinutes"

#: **各阶段的切换条件**（对象：{模式 id: {…}}）。
#:
#: [为什么升级成"每阶段各自一份"] 2026-11-18 之前只有"产水按压差（+可选
#: 兜底定时）、反洗/顺洗按固定分钟数"这一套**写死**的口径 ⇒ 现场想
#: "反洗也按压差切"或"产水不按压差、只按分钟"都得改代码。现在**每个参与
#: 阶段**都能各自配两个触发条件（互不排斥、**先到先切**）：
#:   · 压差触发：pressEnabled + pressThreshold + pressCompare；
#:     **压差读不到 ⇒ 不切**（不按缺数据误判，沿用既有安全侧口径）；
#:   · 定时触发：timerEnabled + timerMinutes；本阶段已耗时 ≥ 分钟 ⇒ 切。
#: 两个触发**都没开** ⇒ 该阶段配置非法 ⇒ 启动闸明确拒绝（fail-closed）。
#:
#: [键名] 协议面 camelCase（pressEnabled/…）；配置文件面 snake_case
#: （press_enabled/…）——规整助手**两种拼写都认**（见
#: `normalize_cycle_stage_cfg`）。
CFG_AUTO_CYCLE_STAGE_CFG = "stageCfg"
#: 阶段是否启用**压差触发**。
CFG_AUTO_CYCLE_STAGE_PRESS_ENABLED = "pressEnabled"
#: 阶段的压差阈值（MPa；夹在 [0, AUTO_CYCLE_PRESS_THRESHOLD_MAX]）。
CFG_AUTO_CYCLE_STAGE_PRESS_THRESHOLD = "pressThreshold"
#: 阶段的压差比较方向（"gt" 大于 / "lt" 小于）。
CFG_AUTO_CYCLE_STAGE_PRESS_COMPARE = "pressCompare"
#: 阶段是否启用**定时触发**。
CFG_AUTO_CYCLE_STAGE_TIMER_ENABLED = "timerEnabled"
#: 阶段的定时时长（分钟；夹在 [AUTO_CYCLE_MINUTES_MIN, AUTO_CYCLE_MINUTES_MAX]）。
CFG_AUTO_CYCLE_STAGE_TIMER_MINUTES = "timerMinutes"

#: stageCfg 条目的字段：协议键(camelCase) / 配置键(snake_case) / 类型。
#:
#: [为什么列成一张表] 协议面（app.py 回显/收 SET）与配置面（config.py 落盘/
#: 读盘）字段名不同（camelCase vs snake_case）⇒ 两边各写一遍迟早对不上
#: ⇒ 列一张表，两边都照它换算。
AUTO_CYCLE_STAGE_FIELDS = (
    (CFG_AUTO_CYCLE_STAGE_PRESS_ENABLED, "press_enabled", "bool"),
    (CFG_AUTO_CYCLE_STAGE_PRESS_THRESHOLD, "press_threshold", "float"),
    (CFG_AUTO_CYCLE_STAGE_PRESS_COMPARE, "press_compare", "str"),
    (CFG_AUTO_CYCLE_STAGE_TIMER_ENABLED, "timer_enabled", "bool"),
    (CFG_AUTO_CYCLE_STAGE_TIMER_MINUTES, "timer_minutes", "int"),
)

#: 压差比较方向取值。
AUTO_PRESS_COMPARE_GT = "gt"     #: 大于阈值才切换
AUTO_PRESS_COMPARE_LT = "lt"     #: 小于阈值才切换
AUTO_PRESS_COMPARE_KEYS = (AUTO_PRESS_COMPARE_GT, AUTO_PRESS_COMPARE_LT)

#: 自动循环配置缺省（与客户端 proto 同值）。
AUTO_CYCLE_ENABLED_DEF = True
AUTO_CYCLE_PRESS_THRESHOLD_DEF = 0.5     # MPa
AUTO_CYCLE_PRESS_COMPARE_DEF = AUTO_PRESS_COMPARE_GT
AUTO_CYCLE_TIMER_FALLBACK_DEF = False    # 定时兜底**默认不开启**（用户定案）
AUTO_CYCLE_TIMER_MINUTES_DEF = 30
AUTO_CYCLE_BACKWASH_MINUTES_DEF = 10
AUTO_CYCLE_RINSE_MINUTES_DEF = 10
#: **参与循环的模式缺省有序列表**（产水→反洗→顺洗；与旧硬编码链一致 ⇒
#: 老配置零改动仍按原样跑）。
AUTO_CYCLE_MODES_DEF = ("produce", "backwash", "rinse")
#: **各阶段时长**的缺省映射（模式 id → 分钟）；由旧的
#: backwashMinutes/rinseMinutes 合成而来（向后兼容）。
AUTO_CYCLE_STAGE_MINUTES_DEF = {
    "backwash": AUTO_CYCLE_BACKWASH_MINUTES_DEF,
    "rinse": AUTO_CYCLE_RINSE_MINUTES_DEF,
}
#: 时长映射里**没写**的模式 ⇒ 缺省时长（分钟）。
AUTO_CYCLE_STAGE_MINUTES_FALLBACK = 10
#: 阶段缺省：**压差触发开**（主运行阶段；现场口径「压差到了就切」）。
AUTO_CYCLE_STAGE_PRESS_ENABLED_DEF = True
#: 阶段缺省：**定时触发关**（兜底定时默认不开启——用户 2026-11-12 定案，
#: 现场压差计正常，用不着兜底；缺省开会把"定时兜底"变成常开行为）。
AUTO_CYCLE_STAGE_TIMER_ENABLED_DEF = False
#: 阶段缺省：**定时分钟数**（非主运行阶段：反洗/顺洗/放水各跑 10 分钟）。
AUTO_CYCLE_STAGE_TIMER_MINUTES_DEF = 10
#: **主运行阶段**（参与列表的第一个）的缺省切换条件：
#: 压差触发**开**（阈值 0.5 MPa、大于才切），定时触发**关**（30 分钟的
#: 兜底定时默认不开——用户 2026-11-12 定案）。
AUTO_CYCLE_STAGE_MAIN_DEF = {
    "press_enabled": AUTO_CYCLE_STAGE_PRESS_ENABLED_DEF,
    "press_threshold": AUTO_CYCLE_PRESS_THRESHOLD_DEF,
    "press_compare": AUTO_CYCLE_PRESS_COMPARE_DEF,
    "timer_enabled": AUTO_CYCLE_STAGE_TIMER_ENABLED_DEF,
    "timer_minutes": AUTO_CYCLE_TIMER_MINUTES_DEF,
}
#: **其余阶段**的缺省切换条件：压差触发**关**，定时触发**开**（10 分钟）。
AUTO_CYCLE_STAGE_TIMED_DEF = {
    "press_enabled": False,
    "press_threshold": AUTO_CYCLE_PRESS_THRESHOLD_DEF,
    "press_compare": AUTO_CYCLE_PRESS_COMPARE_DEF,
    "timer_enabled": True,
    "timer_minutes": AUTO_CYCLE_STAGE_TIMER_MINUTES_DEF,
}
#: **各阶段切换条件的缺省**（{模式 id: {字段}}）——缺省参与列表
#: （产水→反洗→顺洗）下：产水＝主运行阶段（压差切），反洗/顺洗/放水＝定时
#: 阶段（各 10 分钟）。
#:
#: [与旧写死链逐字一致] 改动前的行为就是"产水按压差（+可选兜底定时）切、
#: 反洗/顺洗各 10 分钟切" ⇒ 这份缺省保证**老配置零改动仍按原样跑**。
AUTO_CYCLE_STAGE_CFG_DEF = {
    "produce": dict(AUTO_CYCLE_STAGE_MAIN_DEF),
    "backwash": dict(AUTO_CYCLE_STAGE_TIMED_DEF),
    "rinse": dict(AUTO_CYCLE_STAGE_TIMED_DEF),
    "drain": dict(AUTO_CYCLE_STAGE_TIMED_DEF),
}
#: 各分钟项允许范围（1~1440 分钟 = 1 分钟 ~ 1 天）。
AUTO_CYCLE_MINUTES_MIN = 1
AUTO_CYCLE_MINUTES_MAX = 1440
#: 压差阈值允许范围（MPa；防手滑填成 1000）。
AUTO_CYCLE_PRESS_THRESHOLD_MAX = 10.0
#: 容量上限允许范围（MB）：0 = 不限；上限防手滑填成 TB 级。
STORAGE_MAX_MB_MIN = 0
STORAGE_MAX_MB_MAX = 1024 * 1024

#: 落盘**入库周期缺省**（毫秒；1 分钟）。
#: 2026-11-11 用户定案：历史/报警入库默认 1 分钟一档（原先 5s）。历史是
#: 慢变量（趋势/事后排查用），1 分钟足够看趋势，库体积压到 1/12 —— 现场
#: 机器磁盘小，不封顶会一路写到把盘撑满。
#: [两端同口径] C 侧同名常量 HSF_STORAGE_SAMPLE_MS_DEF（common/XhsfLinkProto.h），
#: 值必须一致（注释互相点名）。
STORAGE_SAMPLE_MS_DEF = 60000

#: 落盘**容量上限缺省**（MB；50）。0 仍表示"不限"，但缺省不再是 0。
#: [两端同口径] C 侧同名常量 HSF_STORAGE_MAX_MB_DEF（common/XhsfLinkProto.h）。
STORAGE_MAX_MB_DEF = 50


# ---------------------------------------------------------------------------
# 自动循环：配置解析的**宽松取**助手（2026-11-18）
# ---------------------------------------------------------------------------
#
#: [为什么放在 proto] 三处都要用同一份口径：配置文件解析（config.py）、
#: 控制器运行期快照（autocontrol.py）、协议 SET 的回显（app.py）⇒
#: 放一处，别各写一遍（各写一遍就会出现"配置里能写、跑起来又不认"的
#: 隐性 bug 面）。

def normalize_cycle_modes(raw, default=None):
    """把「参与循环的模式列表」规整成**有序**的合法模式 id 列表。

    :param raw: 原始值（配置文件里可能是任何类型）。
    :param default: raw **不是列表**时回退的缺省（None ⇒ AUTO_CYCLE_MODES_DEF）。

    [宽松取 · 手改配置不能把服务搞挂] 非列表 ⇒ 回缺省；非法元素丢弃；
    重复元素**去重但保序**（顺序就是循环顺序，不能用 set）。

    [空列表**保留为空**] 输入是"一个列表"但规整后一个合法模式都不剩
    ⇒ 返回空列表（= **未配置参与循环的模式**），**不**回缺省 ——
    启动闸要能明确报「未配置参与循环的模式」；若在这里悄悄回缺省，配置
    写坏了用户会以为配好了，实际跑的是另一套工艺。
    """
    if not isinstance(raw, (list, tuple)):
        return list(AUTO_CYCLE_MODES_DEF if default is None else default)
    out = []
    for item in raw:
        mid = str(item or "").strip()
        if mid in AUTO_CYCLE_MODE_POOL and mid not in out:
            out.append(mid)
    return out


def normalize_cycle_stage_minutes(raw, default=None):
    """把「各阶段时长」规整成 {模式 id: 分钟}。

    :param raw: 原始值（应是一个字典）。
    :param default: 规整后为空时回退的缺省（None ⇒ 不回退＝空字典）。

    [宽松取] 非法模式 id 丢弃；非法分钟数丢弃；分钟数夹在
    [AUTO_CYCLE_MINUTES_MIN, AUTO_CYCLE_MINUTES_MAX]。
    """
    out = {}
    if isinstance(raw, dict):
        for key, val in raw.items():
            mid = str(key or "").strip()
            if mid not in AUTO_CYCLE_MODE_POOL:
                continue
            if isinstance(val, bool):
                continue
            try:
                num = int(val)
            except (TypeError, ValueError):
                continue
            out[mid] = max(AUTO_CYCLE_MINUTES_MIN,
                           min(AUTO_CYCLE_MINUTES_MAX, num))
    if not out:
        return dict(AUTO_CYCLE_STAGE_MINUTES_DEF if default is None
                    else default)
    return out


# ---------------------------------------------------------------------------
# 自动循环：各阶段**切换条件**（stageCfg）的规整与合成
# ---------------------------------------------------------------------------
#
#: [为什么"每阶段各自一份"] 旧的写死口径是"产水按压差（+可选兜底定时）、
#: 反洗/顺洗按固定分钟数" ⇒ 现场想调"反洗也按压差切"必须改代码。现在
#: **每个参与阶段**各自配：压差触发（阈值 + 方向）与定时触发（分钟），
#: 两者**先到先切**。

def _cycle_stage_bool(val, default):
    """宽松取布尔：bool / "true"/"1"/1 ⇒ True；其余回缺省。"""
    if isinstance(val, bool):
        return val
    if isinstance(val, (int, float)) and not isinstance(val, bool):
        return bool(val)
    if isinstance(val, str):
        low = val.strip().lower()
        if low in ("true", "1", "yes", "on"):
            return True
        if low in ("false", "0", "no", "off"):
            return False
    return default


def normalize_cycle_stage_cfg(raw, default=None):
    """把「各阶段切换条件」规整成 {模式 id: {字段: 值}}。

    :param raw: 原始值（配置文件里可能是任何类型；协议面 camelCase、
        配置面 snake_case **两种拼写都认**）。
    :param default: raw 不是字典时回退的缺省（None ⇒ 空字典＝不覆盖）。

    [宽松取 · 手改配置不能把服务搞挂] 非字典 ⇒ 回缺省；未知模式 id 丢弃；
    非法字段值丢弃（该项回缺省）；分钟数夹在允许范围、阈值夹在允许范围。

    [只返回"显式给了"的字段] 一个条目里没写的字段**不出现在结果里**
    ⇒ 由调用方（合成函数）与缺省合并（"缺阶段配置 ⇒ 用合成缺省"）。
    """
    out = {}
    if not isinstance(raw, dict):
        return dict(default or {})
    for key, val in raw.items():
        mid = str(key or "").strip()
        if mid not in AUTO_CYCLE_MODE_POOL:
            continue
        if not isinstance(val, dict):
            continue
        entry = {}
        for proto_key, file_key, kind in AUTO_CYCLE_STAGE_FIELDS:
            if proto_key in val:
                src = val.get(proto_key)
            elif file_key in val:
                src = val.get(file_key)
            else:
                continue
            if kind == "bool":
                if isinstance(src, bool):
                    entry[file_key] = src
                elif isinstance(src, (int, float)):
                    entry[file_key] = bool(src)
                elif isinstance(src, str) and src.strip().lower() in (
                        "true", "false", "1", "0", "yes", "no", "on", "off"):
                    # [宽松取] 只认得懂的布尔字面量（其余丢弃 ⇒ 该项回缺省）
                    entry[file_key] = _cycle_stage_bool(src, False)
            elif kind == "float":
                if isinstance(src, bool):
                    continue
                try:
                    num = float(src)
                except (TypeError, ValueError):
                    continue
                entry[file_key] = max(0.0, min(AUTO_CYCLE_PRESS_THRESHOLD_MAX,
                                               num))
            elif kind == "int":
                if isinstance(src, bool):
                    continue
                try:
                    num = int(src)
                except (TypeError, ValueError):
                    continue
                entry[file_key] = max(AUTO_CYCLE_MINUTES_MIN,
                                      min(AUTO_CYCLE_MINUTES_MAX, num))
            elif kind == "str":
                text = str(src or "").strip().lower()
                if text in AUTO_PRESS_COMPARE_KEYS:
                    entry[file_key] = text
        if entry:
            out[mid] = entry
    return out


def default_cycle_stage_cfg(modes=None):
    """各阶段切换条件的**缺省**（按参与列表的角色合成）。

    [角色口径 · 与旧写死链逐字一致]
      · **主运行阶段**（列表第一个）：压差触发**开**（阈值/方向取旧键缺省），
        定时触发**关**（兜底默认不开）；
      · **其余阶段**（反洗/顺洗/放水…）：压差触发**关**，定时触发**开**
        （缺省 10 分钟）。
    """
    modes = list(modes or AUTO_CYCLE_MODES_DEF)
    first = modes[0] if modes else AUTO_CYCLE_MODES_DEF[0]
    out = {}
    for mid in AUTO_CYCLE_MODE_POOL:
        out[mid] = dict(AUTO_CYCLE_STAGE_MAIN_DEF if mid == first
                         else AUTO_CYCLE_STAGE_TIMED_DEF)
    return out


def build_cycle_stage_cfg(modes=None, legacy=None, explicit=None):
    """算各阶段的**最终切换条件**（旧键合成 + 显式 stageCfg 覆盖）。

    :param modes: 参与循环的模式列表（有序；第一个＝主运行阶段）。
    :param legacy: 旧键值（press_threshold / press_compare / timer_fallback
        / timer_minutes / backwash_minutes / rinse_minutes）。
    :param explicit: 显式 stageCfg（{模式 id: {字段: 值}}；camelCase 与
        snake_case 都认）。

    [合成规则 · 向后兼容]
      · 旧 `pressThreshold/pressCompare/timerFallback/timerMinutes`
        ⇒ 应用到**列表首元素**（＝旧口径的"产水阶段"）；
      · 旧 `backwashMinutes/rinseMinutes` ⇒ 应用到对应模式的 timerMinutes；
      · 显式 `stageCfg` ⇒ **以它为准**（逐字段覆盖；表里没写的阶段用上面
        合成出来的缺省）。

    [返回] {模式 id: {press_enabled, press_threshold, press_compare,
    timer_enabled, timer_minutes}}；**覆盖模式池里的每个模式**（不只参与
    的）⇒ 某个模式退出循环后再加回来，它的设置不丢。
    """
    legacy = dict(legacy or {})
    modes = list(modes or AUTO_CYCLE_MODES_DEF)
    first = modes[0] if modes else AUTO_CYCLE_MODES_DEF[0]
    out = default_cycle_stage_cfg(modes)
    # ---- 旧键 ⇒ 对应模式的定时分钟数 ----
    # [先做这一步] 下面"首元素规则"要覆盖它（首元素规则优先 ⇒ 首元素就是
    # 反洗时，它的定时分钟数取 timer_minutes 而不是 backwashMinutes）。
    for mid, legacy_key, def_min in (
            ("backwash", "backwash_minutes", AUTO_CYCLE_BACKWASH_MINUTES_DEF),
            ("rinse", "rinse_minutes", AUTO_CYCLE_RINSE_MINUTES_DEF)):
        out[mid]["timer_minutes"] = max(
            AUTO_CYCLE_MINUTES_MIN, min(AUTO_CYCLE_MINUTES_MAX,
                                         int(legacy.get(legacy_key,
                                                        def_min))))
    # ---- 旧键 ⇒ 列表首元素（压差 + 兜底定时）----
    out[first]["press_enabled"] = True
    out[first]["press_threshold"] = max(
        0.0, min(AUTO_CYCLE_PRESS_THRESHOLD_MAX,
                 float(legacy.get("press_threshold",
                                  AUTO_CYCLE_PRESS_THRESHOLD_DEF))))
    out[first]["press_compare"] = str(
        legacy.get("press_compare", AUTO_CYCLE_PRESS_COMPARE_DEF))
    out[first]["timer_enabled"] = bool(
        legacy.get("timer_fallback", AUTO_CYCLE_STAGE_TIMER_ENABLED_DEF))
    out[first]["timer_minutes"] = max(
        AUTO_CYCLE_MINUTES_MIN, min(AUTO_CYCLE_MINUTES_MAX,
                                     int(legacy.get(
                                         "timer_minutes",
                                         AUTO_CYCLE_TIMER_MINUTES_DEF))))
    # ---- 显式 stageCfg 覆盖（逐字段）----
    for mid, entry in (explicit or {}).items():
        if mid not in out or not isinstance(entry, dict):
            continue
        for _pk, file_key, _kind in AUTO_CYCLE_STAGE_FIELDS:
            if file_key in entry:
                out[mid][file_key] = entry[file_key]
    return out


def cycle_stage_cfg_to_protocol(stage_cfg):
    """内部（snake_case）stageCfg → 协议面（camelCase）stageCfg。"""
    out = {}
    for mid, entry in (stage_cfg or {}).items():
        if not isinstance(entry, dict):
            continue
        item = {}
        for proto_key, file_key, _kind in AUTO_CYCLE_STAGE_FIELDS:
            if file_key in entry:
                item[proto_key] = entry[file_key]
        if item:
            out[str(mid)] = item
    return out


def cycle_stage_cfg_to_file(stage_cfg):
    """协议面（camelCase）stageCfg → 内部（snake_case）stageCfg。

    [为什么要单独一个函数] 协议 SET 收到的条目是 camelCase，落盘要
    snake_case ⇒ 换算只在这里做一次（别在两处各写一遍）。
    """
    return normalize_cycle_stage_cfg(stage_cfg, default={})


def cycle_stage_minutes_view(stage_cfg, modes=None):
    """stageCfg ⇒ **兼容视图** {模式 id: 分钟}（旧客户端界面的"每阶段分钟"）。

    [为什么留这个视图] 已经上线的客户端界面只认"每阶段多少分钟"这一个
    概念（stageMinutes）⇒ 服务端 GET 继续给它这个派生视图（由 stageCfg
    的 timerMinutes 算出）⇒ 客户端零改动也能显示对的时长。
    真值只有 stageCfg 一份（这个函数**不产生**新的状态）。
    """
    out = {}
    for mid in (modes if modes is not None else AUTO_CYCLE_MODE_POOL):
        entry = (stage_cfg or {}).get(mid)
        if isinstance(entry, dict) and "timer_minutes" in entry:
            try:
                out[str(mid)] = int(entry["timer_minutes"])
            except (TypeError, ValueError):
                continue
    return out


#: 轮询周期允许范围（毫秒）。下限 50 是总线的现实（该模块应答 0.4~0.8s，
#: 再快只是空转+退避刷日志）；上限防手滑填成小时级。
CFG_POLL_MS_MIN = 50
CFG_POLL_MS_MAX = 60000

#: login 节键（账户/登录策略；与 C 侧 XhsfLinkProto.h 的 HSF_CFGK_LOGIN_*
#: 同名同值，2026-11-08 新增）。写入立即对下一次登录生效并落盘配置文件；
#: 已在线会话不受影响（关闭同账并发不会踢人）。需管理员（AccountAdmin）。
CFG_SECTION_LOGIN = "login"
CFGK_LOGIN_ALLOW_ANON = "loginAllowAnonymous"
CFGK_LOGIN_ALLOW_CONCUR = "loginAllowConcurrent"
CFGK_LOGIN_MAX_SESSIONS = "loginMaxSessions"
#: 会话上限允许范围（0=不限）。
CFG_LOGIN_MAX_SESSIONS_MIN = 0
CFG_LOGIN_MAX_SESSIONS_MAX = 256

# ---- 历史数据分页读取（2026-11-06 新增）----

#: 分页读取历史数据（**只读，无角色要求**：历史数据不是控制面）。
#:
#: 载荷 = {"offset":int, "limit":int, "pageSizeMs":int?,  "beforeTs":int?,
#:         "loTs":int?, "hiTs":int?}
#:   offset      跳过多少行（0 = 最新），int
#:   limit       本页最多多少行，由客户端决定（服务端夹到 1..HISTORY_PAGE_MAX）
#:   beforeTs    分页窗口锚点（epoch ms）：**首屏取数时客户端用自己的
#:               "现在"发上来，之后翻页原样回传**，这样窗口固定，新入库的
#:               行不会把 offset 顶偏（否则翻第 2 页会重复/漏行）。
#:               缺省 = 服务端当前时间。
#:   loTs/hiTs   可选时间范围过滤（服务端在 SQL 里筛，保证总数与页内容同口径）
#:   kw          关键字（在**显示文本**上包含匹配，与界面显示同一套格式化
#:               规则；与 loTs/hiTs 一起进同一条 WHERE）
#:   kwCols      关键字要比对的显示列下标数组；缺省/空 = 全部列
#:
#: [为什么关键字必须在服务端筛] 分页在服务端、筛选在客户端时，客户端只能
#: 在拿到的那一页里搜，命中行绝大多数在别的页上 → 界面上"关键字筛选无效"
#: （用户实测反馈）。总数与页内容必须共用同一条 WHERE。
#:
#: 应答 = {"ok":true, "total":int, "offset":int, "limit":int,
#:         "windowStartTs":int, "windowEndTs":int,
#:         "cols":[列名…], "rows":[[值…], …],
#:         "dbRows":int, "latestTs":int}
#:   total   本窗口（≤ beforeTs，且落在 lo/hi 内）的行数 —— 客户端据此算
#:           总页数、显示"共 N 条"
#:   rows    时间**正序**排列的二维数组（每行按 cols 顺序；离线列是 null）
#:   dbRows  全库行数（不受窗口限制）；latestTs 全库最新一行时间戳
#:
#: [为什么用「二维数组 + cols」而不是每行一个子文档] 单行子文档带 18 个
#: 键名 ≈ 306B，其中键名占一半；同样的数据用「列名只发一次 + 每行一个
#: 值数组」压到约 230B/行。16KB 载荷上限下，单页上限因此从 ~48 行提到
#: ~65 行（实测 50 行 = 11.8KB，留足余量）。
CMD_HISTORY_PAGE = 0x0118

#: 运行日志分页（**C 端扩展命令**，2026-11-09；本端已实现，见 app._h_log_page）。
#:
#: C 侧 HighSpeedFilter/common/XhsfLinkProto.h 的 HSF_CMD_LOG_PAGE=0x0119u
#: 与此处常量逐值同步（两端协议常量铁律）；本端 logs 表列
#: (ts, category, text) —— category 是与 C 端 hsfAppEventLog 同口径的类别
#: 字面（conn/ctrl/alarm/auto/auth/sys，见 LOG_CAT_*）。Python 客户端
#: （client_py）消费该命令。
#: 载荷 {"limit":1..50,"offset":int,"beforeTs"?:epochMs,"loTs"?:,"hiTs"?:,
#: "category"?:str,"kw":?}；应答 {"ok","err"?,"rows":[{ts,category,text}],
#: "total","count"?,"offset","limit"}（时间 DESC 取页、页内正序）。
CMD_LOG_PAGE = 0x0119

#: 分页载荷键。
HIST_KEY_OFFSET = "offset"
HIST_KEY_LIMIT = "limit"
HIST_KEY_BEFORE_TS = "beforeTs"
HIST_KEY_LO_TS = "loTs"
HIST_KEY_HI_TS = "hiTs"
#: 关键字（在**显示文本**上做包含匹配；空串/缺省 = 不限）。
HIST_KEY_KW = "kw"
#: 关键字要比对的显示列下标数组（缺省/空 = 全部列）。
HIST_KEY_KW_COLS = "kwCols"
#: 分页应答键。
#: 历史分页：导出/下载意图标记（bool；缺省 false=普通浏览）。
#: 客户端导出时置 true，服务端验 PERM_DOWNLOAD（界面灰按钮拦不住直接发帧）。
HIST_KEY_EXPORT = "export"

HIST_KEY_TOTAL = "total"
HIST_KEY_ROWS = "rows"
HIST_KEY_COLS = "cols"
HIST_KEY_DB_ROWS = "dbRows"
HIST_KEY_LATEST_TS = "latestTs"
HIST_KEY_EARLIEST_TS = "earliestTs"
HIST_KEY_WINDOW_START_TS = "windowStartTs"
HIST_KEY_WINDOW_END_TS = "windowEndTs"

# ==================== 运行日志分页（0x0119，C 侧扩展命令） ====================
#: [为什么单独一套键] 与历史分页**同字面**的键（offset/limit/beforeTs/loTs/
#: hiTs/kw/total/rows）直接复用 HIST_KEY_*；只有日志特有的三个另起：
#: 类别（category）、事件时刻（ts）、事件文本（text）。
#: [与 C 侧逐字一致] C 侧 HighSpeedFilter/common/XhsfLinkProto.h 的
#: HSF_KEY_LOG_* —— 字面不同则 C 客户端日志页读不到任何行。
LOG_KEY_CATEGORY = "category"
LOG_KEY_TS = "ts"
LOG_KEY_TEXT = "text"
#: 本页实际行数（C 侧同名字段；C 端商店铺按行数自行计数也兼容）。
LOG_KEY_COUNT = "count"

# ==================== 报警历史分页（0x011A） ====================
#: 报警历史分页（**新命令**，2026-11-17）。
#:
#: [为什么新增] 报警页是数据三页里**唯一不向服务端取数**的一页：客户端与
#: 网页端各自在**本地进程里**算健康边沿（网页端 hub.feed_health / 客户端
#: AppModel.track_health_alarms）⇒ 记录只在**该进程活着的时候**存在：
#:   · 网页端是常驻服务（板端开机起）⇒ 看得到开机以来的全部边沿；
#:   · 客户端是 UI（重启/升级/崩溃都会重来）⇒ **一重启历史全丢**
#:     （用户实报「web 端报警有数据，客户端报警视图没数据」）。
#: ⇒ 边沿改由**服务端**记进 alarms 表（健康异常/恢复 + 自动告警 + 阀告警
#: + 流量计/保护告警），客户端与网页端都按本命令**取同一份数据** ⇒
#: 单一数据源、客户端重启不丢、两端内容一致。
#:
#: 载荷 {"offset":int,"limit":int,"beforeTs"?:epochMs,"loTs"?:,"hiTs"?:,
#: "kw"?:str,"export"?:bool}；应答 {"ok","err"?,"total","count","offset",
#: "limit","rows":[{ts,kind,source,text,dev}]}（ts DESC 取页、页内正序，
#: 与 0x0118/0x0119 同口径）。
CMD_ALARM_PAGE = 0x011A

#: 报警行的「来源类别」字面（alarms 表 kind 列）。
ALARM_KIND_HEALTH = "health"     #: 设备健康边沿（数据过期/离线/故障/恢复）
ALARM_KIND_AUTO = "auto"         #: 自动运行告警
ALARM_KIND_VALVE = "valve"       #: 电动球阀告警（超时/冲突/恢复）
ALARM_KIND_FLOWMETER = "flowmeter"
ALARM_KIND_DEVICE = "device"     #: 设备故障（变频器等）
#: 报警行的键（与日志那套同形，键名不同以免混用）。
ALARM_KEY_KIND = "kind"
ALARM_KEY_SOURCE = "source"      #: 设备名/来源对象
ALARM_KEY_TEXT = "text"          #: 状态文案（"数据过期" / "已恢复正常"…）
ALARM_KEY_DEV = "dev"            #: 设备类别（proto.KIND_*；缺省 = 无）
#: 「恢复正常」类行的状态文案后缀（客户端据此显示状态列）。
ALARM_TEXT_RECOVERED = "已恢复正常"

# ==================== 变频器参数读写（0x011B，2026-11-18 新增） ====================
#:
#: [这一条命令是什么] 远程读写变频器（M512/M513）的**参数**（电机铭牌组
#: P1.00~P1.05 起步），区别于 0x0101 的"运行控制"（启停/调速）：参数是
#: 停机维护动作，有独立的权限与运行锁定闸。
#:
#: [三端同步点] C 侧 `HighSpeedFilter/common/XhsfLinkProto.h` 已加
#: HSF_CMD_VFD_PARAMS=0x011Bu（**C 端服务端/客户端暂缓实现**，先把协议号
#: 占住防他号复用）；本节常量与键名是客户端（client_py）/网页端
#: （webui_py）实现时**照抄的单一事实源**。
#:
#: 请求载荷（BSON）：
#:     {"op": 0=读整组 / 1=写若干项,
#:      "values": {"<addr 十进制>": rawValue(int)}}  ← 写才带
#:   [为什么键是十进制**字符串**] BSON 文档键只能是 cstring（两端
#:   bson.py/_cstr 与 C 侧 XBsonDocument 同口径），int 键上不了线——
#:   地址以十进制串作键（"61697" = 0xF101；0xF100=61696，0xF000 才是 61440），值 = raw int32；
#:   "addr 是 10 进制 int" 指地址按十进制表达（不是 hex 串、不是参数号）。
#:   服务端 int(key) 归一后校验，解析不了即报错（fail-closed）。
#: 应答载荷：
#:     {"ok": bool, "err"?: utf8,
#:      "params": [ {code, label, addr, raw, value, unit,
#:                   writable, runLocked, applied?} … ],
#:      "vfdFaulted": bool, "runState": int}
#:   · params[].value = **从驱动器回读的工程值**（raw ÷ scale，按
#:     param_value_of 取整）——写操作后服务端**重新读驱动器**再用回读值
#:     填表，绝不把提交值当成功值返回；
#:   · params[].applied 仅写操作的项携带：True=回读值与写入值一致；
#:   · runState = 0x3000 原始字（1 正转 / 2 反转 / 3 停机）；
#:   · 失败包络（未装配/离线/权限/运行锁定）不带 params，只带 ok/err。
#: 权限：读=登录即可（只读面，PERM_VIEW）；写=管理员（PERM_ACCOUNT_ADMIN，
#: 照 0x0117 config_set 同款闸与拒绝文案）。
#: RunLocked：0x3000 为 1/2（运行中）⇒ 拒写（**不发总线写**），err=
#: VFD_PARAMS_ERR_RUN_LOCKED（三端同款文案）。
CMD_VFD_PARAMS = 0x011B

#: 相机画面：客户端**按需请求一帧**（2026-11-19 新增；C 侧同号）。
#:
#: [与主题的分工] TOPIC_CAMERA_FRAME 是服务端**主动推**的周期帧；
#: 本命令是客户端**按需拉**的单帧（例如「拍张高清的」）⇒ 应答走 BSON
#: （帧宽/高/字节数/时间戳），画面本体仍走分片主题（同一分片格式）。
CMD_CAMERA_FRAME = 0x011C

#: op：读整组参数（PARAM_DEFS 全部条目）。
VFD_PARAMS_OP_READ = 0
#: op：写若干项（载荷必带 values；addr → raw，十进制 int）。
VFD_PARAMS_OP_WRITE = 1

#: [请求键复用] 请求载荷的 "op" 与 "values" 直接复用既有 KEY_OP / KEY_VALUES
#: （同字面，不另立常量）；应答/参数项键用本节新常量。
#
#: 应答键：参数项数组（每项键面见上方注释；写操作项多一个 applied）。
KEY_VFD_PARAMS = "params"
#: 应答键：变频器是否故障（0x8000 ≠ 0）。
KEY_VFD_FAULTED = "vfdFaulted"
#: 应答键：运行状态字（0x3000 原始值：1 正转 / 2 反转 / 3 停机）。
KEY_VFD_RUN_WORD = "runState"
#: 参数项键：写操作该项是否已确认生效（**回读值 == 写入值**才算）。
#: 仅写操作的项携带；读操作不带。
KEY_PARAM_APPLIED = "applied"

#: 失败文案：变频器未装配/离线/总线失败（不给假值，一律包络）。
#: 与 _h_vfd_control/_h_vfd_snapshot 的既有文案同字面。
VFD_PARAMS_ERR_NOT_READY = "变频器未就绪"
#: 失败文案：运行中禁止写参数（**三端同款**——C 端/web 端实现时照抄，
#: 不得改写：客户端按文案匹配展示，漂移会让现场看到两套说法）。
VFD_PARAMS_ERR_RUN_LOCKED = "变频器运行中禁止写参数（请先停止）"

#: 变频器参数表（第一期：P1 电机铭牌组 0xF100~0xF105）。
#:
#: [数据来源——不是编的]
#:   · 地址/单位/小数位/量程/访问约束：
#:     docs/M512_511_512_513寄存器映射表.md §9.5 参数描述表（0xF100 段，
#:     与 C 侧 `Src/Industry/XVfd/XVfdM512.c` 的 s_m512Params[] 144~149 行
#:     逐项一致）；0xF100 段组表允许 **6 连读**（单帧 ≤12 上限内，见该文档
#:     §9.2 远端地址区间表）。
#:   · scale = 10^小数位（C 侧 XVfdParamDesc.m_decimals：raw ÷ 10^decimals
#:     为工程值）。P1.04 的小数位手册标「1（待实机标定）」——先按 1 落，
#:     实机标定后三端同步改。
#:
#: [键面即应答项键面] 每项的 code/label/addr/unit/writable/runLocked 与
#: 应答 params[] 逐字同名 ⇒ 客户端/网页端照抄本表渲染；scale/min/max 是
#: 换算与校验用的服务端字段（不应答，但客户端做量程显示/输入框限位时
#: 同样照抄）。
#: [枚举型参数的可选项] raw → 中文名。**只有枚举型参数才有**这个键；
#: 数值型参数没有 ⇒ 客户端/网页端按「有没有 options」决定渲染下拉框还是
#: 数值框。取值来源是**手册**不是编的：说明书第 20 页「P1 组 电机参数」
#: P1.00 电机类型选择 = 0 普通异步电机 / 1 变频异步电机 / 2 永磁同步电机
#: （出厂值 0，可改）。**新增枚举参数时照此格式加，并注明手册页码。**
PARAM_OPTIONS = {
    0xF100: ((0, "普通异步电机"), (1, "变频异步电机"),
             (2, "永磁同步电机")),
}

PARAM_DEFS = (
    {
        "code": "P1.00", "label": "电机类型选择", "addr": 0xF100,
        "unit": "", "scale": 1, "min": 0, "max": 2,
        "writable": True, "runLocked": True,
        # [枚举型] 有选项 ⇒ 界面渲染下拉框（用户直接选类型，不看裸数字）。
        "options": PARAM_OPTIONS[0xF100],
    },
    {
        "code": "P1.01", "label": "电机额定功率", "addr": 0xF101,
        "unit": "kW", "scale": 10, "min": 10, "max": 10000,
        "writable": True, "runLocked": True,
    },
    {
        "code": "P1.02", "label": "电机额定电压", "addr": 0xF102,
        "unit": "V", "scale": 1, "min": 1, "max": 2000,
        "writable": True, "runLocked": True,
    },
    {
        "code": "P1.03", "label": "电机额定电流", "addr": 0xF103,
        "unit": "A", "scale": 100, "min": 1, "max": 65535,
        "writable": True, "runLocked": True,
    },
    {
        "code": "P1.04", "label": "电机额定频率", "addr": 0xF104,
        "unit": "Hz", "scale": 10, "min": 1, "max": 50000,
        "writable": True, "runLocked": True,
    },
    {
        "code": "P1.05", "label": "电机额定转速", "addr": 0xF105,
        "unit": "rpm", "scale": 1, "min": 1, "max": 65535,
        "writable": True, "runLocked": True,
    },
)

#: addr → 参数定义（服务端校验/换算用；客户端同样可建这张索引）。
PARAM_BY_ADDR = {d["addr"]: d for d in PARAM_DEFS}


def param_option_label(d, raw):
    """枚举型参数：raw → 中文名；**不在选项表里返回 None**（不编）。

    [回读不撒谎] 驱动器里存了表外的值（别的机型/别的固件）时，宁可**不给**
    名字（界面回落到原始码），也不能把 5 说成「永磁同步电机」。
    """
    for value, label in d.get("options") or ():
        if value == raw:
            return label
    return None


def param_value_of(raw, scale):
    """raw → 工程值（raw ÷ scale，按 scale 的十进位取整到该小数位）。

    [为什么不直接除] raw=6553、scale=100 的浮点除会给 65.529999…，
    界面上忽长忽短；按 scale 的位数 round 一次（1→整数、10→1 位、
    100→2 位），三端同口径（客户端/网页渲染照抄本函数的口径）。
    """
    raw = int(raw)
    if scale is None or scale <= 1:
        return raw
    decimals = len(str(int(scale))) - 1
    return round(raw / float(scale), decimals)


def vfd_param_run_locked(run_word):
    """0x3000 运行状态字 → 是否禁止写参数（1 正转 / 2 反转 = 运行中）。

    [只认 1/2] 手册口径 3=停机；其余值不视为运行中（放行后真写不进去时
    驱动器自己会拒——把"未知状态"当"运行中"会让停机态也改不了参数）。
    """
    return run_word in (1, 2)


# ==================== 运行日志类别（logs.category 列的字面） ====================
#: [口径与 C 端一致] C 侧 server/HsfServerApp.c 的 hsfAppEventLog() 只用这
#: 五个字面；C 客户端「类别」下拉（client/ui/pages/HsfDataLog.c 的
#: kLogCategoryKeys）也正是这五个 ⇒ 字面必须逐字相同，否则筛选落空。
LOG_CAT_CONN = "conn"      #: 会话接入/断开
LOG_CAT_CTRL = "ctrl"      #: 控制受理/拒绝（启停/设频/写 DO/点动…）
LOG_CAT_ALARM = "alarm"    #: 告警（阀超时、自动运行告警…）
LOG_CAT_AUTO = "auto"      #: 自动运行/模式切换/全停
LOG_CAT_AUTH = "auth"      #: 登录成功/被拒
#: [为什么多一个 sys] C 端的 logs 表**只**收上面五类定点事件；本端 logs 表
#: 还收了大量非事件类业务日志（采集失败、配置变更、健康变化…），统一归 sys
#: ⇒ C 客户端「类别」列不留空白，而下拉里的五类筛选仍精确命中。
#: **两端客户端的下拉都已带「系统」档**（2026-11-10：不带的话日志页「类别」
#: 列对这类行显示原始字面 "sys"，用户截图反馈「中文」）；字面必须逐字
#: 一致：本文件 ↔ XhsfLinkProto.h 的 HSF_KEY_LOG_CATEGORY 注释 ↔
#: client_py `LogPage.CATEGORY_ITEMS` ↔ C `HsfDataLog.c` 的 kLogCategoryKeys。
LOG_CAT_SYS = "sys"
#: C 侧客户端「类别」下拉可筛的五个字面（"全部类别"=空串，不在此列）。
LOG_CATEGORIES = (LOG_CAT_CONN, LOG_CAT_CTRL, LOG_CAT_ALARM, LOG_CAT_AUTO,
                   LOG_CAT_AUTH)

#: 单页行数上限：由 XLINK_MAX_PAYLOAD(16KB) 反推。
#: 实测一行（18 列，含中文模式/状态与单位串）≈230B，50 行 = 11.8KB，
#: 留 4KB 给包络与异常大的字符串。客户端可以要得更少（板端小屏一次
#: 20 行够用），但不能更多——超了会被服务端夹回而不是发不出去。
HISTORY_PAGE_MAX = 50
#: 单页行数下限（0/负数按 1 处理：要了就该给一行，不能静默变成空页）。
HISTORY_PAGE_MIN = 1
#: 客户端缺省页大小（不传 limit 时）。
HISTORY_PAGE_DEFAULT = 20

# ---- 浊度仪键（v{index}_ 前缀，与阀/数字量同一风格）----

#: 浊度值 ×100（int；NTU × 100，避免浮点在 BSON 上的精度争议）。
KEY_TURB_NTU = "ntu"
#: 原始寄存器值（int；排查真相用，不受换算影响）。
KEY_TURB_RAW = "raw"
#: 该台是否在线（bool）。
KEY_TURB_ONLINE = "online"
#: 该台水温 ×100（int；℃ × 100，与 NTU 同口径）。
#:
#: [2026-11-10 新增] 只有**带温度的机型**才有（WZT-701A 站1 无温度；
#: ZHEYI-5100ZDY 站3 一帧 0x0000×4 同刻带出浊度+温度）。
#:
#: [缺键 = 没有温度，不是 0℃] 机型不带温度时服务端压根不给这个键，
#: 客户端显示 "--"。若给 0℃ 会被当成真实读数（现场 0℃ 与"没这功能"
#: 差着一条温度报警的量级）。
#:
#: 与 C 侧 XhsfLinkProto.h 的 HSF_KEY_TURB_TEMP 同名同义。
KEY_TURB_TEMP = "temp"

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

#: 健康档 → 中文（**与 client_py/hsfsrv_client/model.py 的 HEALTH_TEXT
#: 逐字一致**；两端协议/文案铁律）。服务端写报警历史时用它填状态列，
#: 客户端按同一张表渲染 ⇒ 两端口径一致。
HEALTH_TEXT = {0: "正常", 1: "告警", 2: "数据过期", 3: "离线", 4: "故障"}

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
#: 网络相机（RTSP 取流；2026-11-20 进设备健康列表——动态 IP/断线也是
#: 运维要盯的事，跟 485 设备一个待遇）。
KIND_CAMERA = 8

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
# [操作者=三档缺省的第三档] 2026-11-11 用户定案「缺省建户也改成三档」：
# 权限三档（普通/操作者/管理）缺省各给一个账户，新装环境开箱即用全梯度。
# 名字取 oper（用户口径：operator 太长，与 admin/root 不搭配）；与 C 端
# XAccountManager_ensureDefaults 的第三户逐字一致，改一侧必须同步另一侧。
DEFAULT_OPER_NAME = "oper"
DEFAULT_OPER_PASS = "oper"

#: [deprecated] 旧闸值；语义上=权限点 PERM_DEVICE_CONTROL。
#: 新代码请用 permissions.require(session, PROTO_PERM...)——角色值不单调，
#: `role < ROLE_CONTROL` 这类比较已不可用。
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
