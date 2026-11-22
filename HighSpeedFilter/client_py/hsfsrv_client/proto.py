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
#: [2026-11-11 两端同口径] C 服务端（HsfServerIo.c applyCollectConfig）已
#: **运行期消费**这 8 个开关（485 三台停用=从站最小发送间隔顶天文数字、
#: 模拟量/数字量停用=设备轮询间隔 0，与 Python 服务端 start_polling 的
#: enable_* 同语义，改后下一拍生效）；缺键回落装配事实（装了就开）。
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
# collect 节键：压差计/温度传感器的通道号与量程上下限（2026-11-20 远端
# 可配；二改：量程=下限~上限，4mA→下限、20mA→上限，「温度满量程」废弃——
# 用户口径「只要填上限和下限即可，范围自己算」，压差计同样可配）。通道号
# 0..模块路数-1。与服务端 hsfsrv/proto.py 逐值同步。
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
#: 量程上下限界（与服务端一致；越界 SET 服务端明确拒；下限<上限另做交叉校验）。
CFGK_ANALOG_PRESS_LOW_MPA_MIN = -10.0
CFGK_ANALOG_PRESS_LOW_MPA_MAX = 100.0
CFGK_ANALOG_PRESS_HIGH_MPA_MIN = 0.001
CFGK_ANALOG_PRESS_HIGH_MPA_MAX = 100.0
CFGK_ANALOG_TEMP_LOW_C_MIN = -100.0
CFGK_ANALOG_TEMP_LOW_C_MAX = 100.0
CFGK_ANALOG_TEMP_HIGH_C_MIN = -50.0
CFGK_ANALOG_TEMP_HIGH_C_MAX = 500.0
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

#: storage 节键：**历史入库的触发方式**（2026-11-12 用户定案「在远端增加
#: 设置历史输入入库，触发方式是持续采集还是自动运行采集」）。
#:
#: [两种触发方式]
#:   · always（缺省）：持续采集入库（与自动模式无关，保持既有行为）；
#:   · auto：仅自动运行时入库（停机/手动期间不记历史）。
#: 只影响**落盘**；遥测采集与界面推送照旧。
CFGK_STORAGE_HISTORY_TRIGGER = "historyTrigger"
HISTORY_TRIGGER_ALWAYS = "always"
HISTORY_TRIGGER_AUTO = "auto"
#: 下拉项（键, 文案）——与服务端 proto.HISTORY_TRIGGERS 同序。
HISTORY_TRIGGERS = (
    (HISTORY_TRIGGER_ALWAYS, "持续采集入库（与自动模式无关）"),
    (HISTORY_TRIGGER_AUTO, "仅自动运行时入库（停机期间不记）"),
)
HISTORY_TRIGGER_KEYS = tuple(k for k, _t in HISTORY_TRIGGERS)

# ---- 运行保护（protect 节；2026-11-15 用户定案）----
#
#: [这一节是什么] 设备侧的**保护联锁**：进水泵开着、流量计却一直报"空管"
#: ⇒ 说明水路没通（或流量计坏了）⇒ 到时限就按配置动作（停设备 / 只报警）。
#: [两端同步点] server_py/hsfsrv/proto.py 与 C 侧 XhsfLinkProto.h
#: HSF_CFG_SECTION_PROTECT / HSF_CFGK_PROTECT_* 逐字同值。
CFG_SECTION_PROTECT = "protect"
#: protect 节键：进水泵空管超时（int 毫秒；**0 = 关闭该保护**）。
CFGK_PROTECT_INLET_NOFLOW_TIMEOUT_MS = "inletNoFlowTimeoutMs"
#: protect 节键：超时后是否**停止设备**（bool；缺省 true = 停）。
CFGK_PROTECT_INLET_NOFLOW_STOP = "inletNoFlowStop"
#: [缺省 0 = 关闭该保护] 现场流量计的空管位可能长期挂着（实测板端 3.6 m³/h
#: 时也带空管）⇒ 缺省启用会"每次开泵一分钟就停线" ⇒ 由现场设时限再打开。
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

#: protect 节键：**电机超温保护**是否启用（bool；缺省 false = 不启用）。
#:
#: [判据] 温度传感器（模拟量，通道远端可配 tempChannel）换算出的 ℃
#: **连续**超过 `motorOverTempLimitC` 达 2 秒 ⇒ 触发（电机过热继续跑会
#: 烧毁 ⇒ 报警锁存 + 按配置停设备）。缺省不启用：阈值与现场电机强相关，
#: 写死必误动作 ⇒ 现场自己定。与服务端 proto 逐值同步（2026-11-20）。
CFGK_PROTECT_MOTOR_OVER_TEMP_ENABLED = "motorOverTempEnabled"
#: protect 节键：**电机超温阈值**（double；℃）。
CFGK_PROTECT_MOTOR_OVER_TEMP_LIMIT_C = "motorOverTempLimitC"
#: protect 节键：超温触发后是否**停止设备**（bool；缺省 true = 停）。
CFGK_PROTECT_MOTOR_OVER_TEMP_STOP = "motorOverTempStop"
#: 缺省值（75 ℃；缺省**不启用**；范围 0~200 ℃）。
PROTECT_MOTOR_OVER_TEMP_LIMIT_C_DEF = 75.0
PROTECT_MOTOR_OVER_TEMP_LIMIT_C_MIN = 0.0
PROTECT_MOTOR_OVER_TEMP_LIMIT_C_MAX = 200.0
PROTECT_MOTOR_OVER_TEMP_ENABLED_DEF = False
PROTECT_MOTOR_OVER_TEMP_STOP_DEF = True
#: 超温触发前的**持续时长**（毫秒）：短时尖峰（传感器抖动）不动作。
PROTECT_MOTOR_OVER_TEMP_DEBOUNCE_MS = 2000

# ---- 相机（网络相机取流；2026-11-19 新增）----
#
#: [这一节是什么] 服务端用 RTSP 拉一路**网络相机**的画面，抽帧成 JPEG 后
#: **分片**推给客户端（TOPIC_CAMERA_FRAME）。客户端**一律不直连相机**：
#: 外网访问场景里浏览器/客户端在公网、相机在内网，只有服务端两边都够得到
#: （用户 2026-11-19 口径）。
#:
#: [两端同步点] 与服务端 hsfsrv/proto.py 及 C 侧 XhsfLinkProto.h 的
#: HSF_CFG_SECTION_CAMERA / HSF_CFGK_CAMERA_* 逐字同值。
CFG_SECTION_CAMERA = "camera"

#: camera 节键：**总开关**（bool；缺省 false = 不采集）。
CFGK_CAMERA_ENABLED = "enabled"
#: camera 节键：相机 **IP**（str）。
CFGK_CAMERA_IP = "ip"
#: camera 节键：**RTSP 端口**（int；缺省 554）。
CFGK_CAMERA_PORT = "port"
#: camera 节键：**账号**（str）。
CFGK_CAMERA_USERNAME = "username"
#: camera 节键：**口令**（str）。
#: [安全] 服务端 GET 应答**不回显明文**（只回 hasPassword 布尔）；
#: 客户端留空提交 = 不修改既有口令。
CFGK_CAMERA_PASSWORD = "password"
#: camera 节键：**码流档**（"main" 主码流 / "minor" 子码流）。
CFGK_CAMERA_STREAM = "stream"
#: camera 节键：**采集帧率**（int）。
CFGK_CAMERA_FPS = "fps"
#: camera 节键：**输出帧宽**（int；等高缩放）。
CFGK_CAMERA_WIDTH = "width"
#: camera 节键：**JPEG 质量**（int，1~100，**越大越清晰**、文件越大）。
#: [口径 = GStreamer jpegenc 的 quality]（**与 ffmpeg 的 -q:v 方向相反**：
#: ffmpeg 是 1~31 越小越好 ⇒ 两套语义混用会得到"调大反而更糊"）。
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

#: 码流档（协议稳定 id；取值集合之外的**明确拒**，fail-closed）。
CAMERA_STREAM_MAIN = "main"
CAMERA_STREAM_MINOR = "minor"
CAMERA_STREAM_CHOICES = (CAMERA_STREAM_MAIN, CAMERA_STREAM_MINOR)

#: [帧率档位＝跟随相机自身档位] 实测本相机（TP-LINK IPC）能力表
#: frame_rates = [65537, 65546, 65551, 65556] 即 65536+fps ⇒ **1/10/15/20**
#: 四档，**相机没有 25/30fps** ⇒ 界面下拉只列这四档（也可在更高的流上抽帧）。
CAMERA_FPS_CHOICES = (1, 10, 15, 20)
CAMERA_FPS_MIN = 1
CAMERA_FPS_MAX = 30
CAMERA_WIDTH_MIN = 128
CAMERA_WIDTH_MAX = 4096
CAMERA_QUALITY_MIN = 1
CAMERA_QUALITY_MAX = 100

#: [2026-11-19 架构转向：画面直传] camera 节键：**传输模式**
#: "stream"（缺省，服务端不解码直接转发 NAL，客户端解）/ "jpeg"（旧路兜底）。
CFGK_CAMERA_TRANSPORT = "transport"
CAMERA_TRANSPORT_STREAM = "stream"
CAMERA_TRANSPORT_JPEG = "jpeg"
CAMERA_TRANSPORT_CHOICES = (CAMERA_TRANSPORT_STREAM, CAMERA_TRANSPORT_JPEG)
CAMERA_TRANSPORT_DEF = CAMERA_TRANSPORT_STREAM

#: camera 节键：**期望的编码**（h264 缺省 / h265）。实际编码随服务端状态回传
#: （actualCodec），转发不关心编码，客户端要能解。
CFGK_CAMERA_CODEC = "codec"
CAMERA_CODEC_H264 = "h264"
CAMERA_CODEC_H265 = "h265"
CAMERA_CODEC_CHOICES = (CAMERA_CODEC_H264, CAMERA_CODEC_H265)
CAMERA_CODEC_DEF = CAMERA_CODEC_H264

#: camera 节键：**客户端解码方式**（远端设置相机卡可编辑）："auto" 缺省
#: （按机器 GPU 试 qsv/cuvid/amf，不行回落软解）/ "on" 强制硬解 /
#: "off" 强制软解。
CFGK_CAMERA_HW_DECODE = "hwDecode"
CAMERA_HW_AUTO = "auto"
CAMERA_HW_ON = "on"
CAMERA_HW_OFF = "off"
CAMERA_HW_CHOICES = (CAMERA_HW_AUTO, CAMERA_HW_ON, CAMERA_HW_OFF)
CAMERA_HW_DEF = CAMERA_HW_AUTO
# ---- 连接方式 / MAC / 断线重连（2026-11-20 用户口径；与服务端逐值同步）----
#: 连接方式：ip=按 IP 直连（缺省）；mac=动态 IP 相机按 MAC 解析
#: （服务端连接前把 MAC 换成当前 IP：先查本机 ARP，查不到对网段做
#: TCP 探测把 ARP 表喂起来再查）。
CFGK_CAMERA_RESOLVE_MODE = "resolveMode"
CAMERA_RESOLVE_IP = "ip"
CAMERA_RESOLVE_MAC = "mac"
CAMERA_RESOLVE_CHOICES = (CAMERA_RESOLVE_IP, CAMERA_RESOLVE_MAC)
CAMERA_RESOLVE_DEF = CAMERA_RESOLVE_IP
#: 相机 MAC（存盘归一小写冒号）。
CFGK_CAMERA_MAC = "mac"


def normalize_mac(text):
    """任意常见 MAC 写法 → 小写冒号；不合法返回 None（与服务端同规则）。

    冒号/连字符/裸 hex/点分都认；用于输入框提交前归一与回显清洗。
    """
    import re as _re
    t = str(text or "").strip().lower().replace("：", ":")
    if not t:
        return None
    hexes = _re.findall(r"[0-9a-f]{1,2}", t.replace(":", "").replace(".", ""))
    if len(hexes) != 6 or sum(len(h) for h in hexes) != 12:
        return None
    return ":".join(hexes)
#: 断线重连：开关 + 重连间隔（秒；关=停在错误上等人工重新应用）。
CFGK_CAMERA_RECONNECT_ENABLED = "reconnectEnabled"
CFGK_CAMERA_RECONNECT_INTERVAL_S = "reconnectIntervalS"
CAMERA_RECONNECT_ENABLED_DEF = True
CAMERA_RECONNECT_INTERVAL_S_DEF = 5
CAMERA_RECONNECT_INTERVAL_S_MIN = 1
CAMERA_RECONNECT_INTERVAL_S_MAX = 3600

#: 分片 flags 扩展位（与 C 头 / webui proto 逐值同步）：
#: bit2 = 载荷是编码流 NAL（Annex-B 含起始码；frameId=NAL 序号，宽高=0）；
#: bit3 = 编码流是 H.265（bit2=0 时恒 0 = JPEG）。
CAM_FRAME_FLAG_ES = 0x0004
CAM_FRAME_FLAG_H265 = 0x0008

# ---- 自动循环（组合模式「自动」）配置；2026-11-12 用户定案 ----
#
#: [循环顺序] 产水 →（压差达标 **或** 定时兜底）→ 反洗 →（定时）→ 顺洗
#: →（定时）→ 产水 ……三个子模式循环，直到人按停止。
#:
#: [键的位置] auto 配置节内的 "cycle" 子节（与 "modes" 平级）。
CFG_AUTO_CYCLE = "cycle"
CFG_AUTO_CYCLE_ENABLED = "enabled"
#: 产水切换到反洗的**压差阈值**（MPa）。
CFG_AUTO_CYCLE_PRESS_THRESHOLD = "pressThreshold"
#: 比较方向：压差大于 / 小于阈值才切换。
CFG_AUTO_CYCLE_PRESS_COMPARE = "pressCompare"
#: 是否启用**定时器兜底**（默认关）：产水到点强制切反洗（防压差计坏掉卡死）。
CFG_AUTO_CYCLE_TIMER_FALLBACK = "timerFallback"
#: 产水阶段的兜底定时（**分钟**）。
CFG_AUTO_CYCLE_TIMER_MINUTES = "timerMinutes"
#: 反洗运行时间（**分钟**）——到期切顺洗。
CFG_AUTO_CYCLE_BACKWASH_MINUTES = "backwashMinutes"
#: 顺洗运行时间（**分钟**）——到期切产水。
CFG_AUTO_CYCLE_RINSE_MINUTES = "rinseMinutes"
#
# ---- 参与循环的模式 + 顺序（2026-11-18 用户定案）----
#
#: [cycleModes] **字符串数组**，元素是模式 id（produce/backwash/rinse/drain），
#: **数组顺序 = 循环顺序**：第一个 = **主运行阶段**（按压差/兜底定时切换），
#: 其余按各自时长切换；最后一个的下一个是第一个（闭环）。长度 ≥ 2。
#: 不在数组里的模式 = 不参与循环（仍留在「可参与」池里）。
CFG_AUTO_CYCLE_MODES = "cycleModes"
#: [stageMinutes] **对象** {modeId: 分钟}：各阶段的运行时长。只含**参与**
#: 循环的模式（未参与的不提交）。首元素（主运行阶段）的时长由服务端口径
#: 处理，界面上作为普通阶段时长项提交。
CFG_AUTO_CYCLE_STAGE_MINUTES = "stageMinutes"
#
# ---- 每阶段各自的切换条件（2026-11-18 用户定案）----
#
#: [stageCfg] **对象** {modeId: {...}}：**每个参与阶段各自**的切换条件。
#: 只含**参与**循环的模式（未参与的不提交）。
#:
#: [为什么要它] 上一轮的 cycle 只把「压差/兜底定时」绑在**主运行阶段**
#: （第一个）身上，其余阶段只有时长 ⇒ 想让「反洗也看压差」「放水也定时」
#: 就得改代码。现在每个阶段各自配自己的触发条件：
#:   · 压差触发：启用 + 阈值（MPa）+ 比较方向（大于/小于）；
#:   · 定时触发：启用 + 分钟数；
#:   · **两者都启用 ⇒ 先到先切**（谁先满足谁切）。
#: [与旧键并存] 旧的 cycle 级键（pressThreshold/pressCompare/
#: timerFallback/timerMinutes）仍按**首元素**（主运行阶段）的值提交 ⇒
#: 老服务端照样能用（见 pages_other._auto_cycle_values）。
CFG_AUTO_CYCLE_STAGE_CFG = "stageCfg"
#: stageCfg 里每个阶段的键（五个，**与服务端逐字一致**）。
CFG_AUTO_STAGE_PRESS_ENABLED = "pressEnabled"    #: 压差触发是否启用（bool）
CFG_AUTO_STAGE_PRESS_THRESHOLD = "pressThreshold"  #: 压差阈值（double, MPa）
CFG_AUTO_STAGE_PRESS_COMPARE = "pressCompare"      #: 比较方向（"gt"/"lt"）
CFG_AUTO_STAGE_TIMER_ENABLED = "timerEnabled"      #: 定时触发是否启用（bool）
CFG_AUTO_STAGE_TIMER_MINUTES = "timerMinutes"      #: 定时时长（int, 分钟）
#: stageCfg 里每个阶段的键名清单（回显/提交时按它逐项处理）。
AUTO_STAGE_CFG_KEYS = (CFG_AUTO_STAGE_PRESS_ENABLED,
                        CFG_AUTO_STAGE_PRESS_THRESHOLD,
                        CFG_AUTO_STAGE_PRESS_COMPARE,
                        CFG_AUTO_STAGE_TIMER_ENABLED,
                        CFG_AUTO_STAGE_TIMER_MINUTES)

#: 压差比较方向取值。
AUTO_PRESS_COMPARE_GT = "gt"     #: 大于阈值才切换
AUTO_PRESS_COMPARE_LT = "lt"     #: 小于阈值才切换
AUTO_PRESS_COMPARE_KEYS = (AUTO_PRESS_COMPARE_GT, AUTO_PRESS_COMPARE_LT)
#: 压差比较方向的下拉文案（界面用）。
AUTO_PRESS_COMPARE_LABELS = (
    (AUTO_PRESS_COMPARE_GT, "大于阈值时切换"),
    (AUTO_PRESS_COMPARE_LT, "小于阈值时切换"),
)

#: 自动循环配置缺省（与服务端同值）。
AUTO_CYCLE_ENABLED_DEF = True
AUTO_CYCLE_PRESS_THRESHOLD_DEF = 0.5     # MPa
AUTO_CYCLE_PRESS_COMPARE_DEF = AUTO_PRESS_COMPARE_GT
AUTO_CYCLE_TIMER_FALLBACK_DEF = False    # 定时器兜底**默认不开启**
AUTO_CYCLE_TIMER_MINUTES_DEF = 30
AUTO_CYCLE_BACKWASH_MINUTES_DEF = 10
AUTO_CYCLE_RINSE_MINUTES_DEF = 10
#: [缺省参与循环的模式与顺序 2026-11-18] 产水→反洗→顺洗（与既有行为一致）。
#: 「放水模式」默认**不参与**（用户可勾选加入）。
AUTO_CYCLE_MODES_DEF = ("produce", "backwash", "rinse")
#: [各阶段缺省时长（分钟）] 逐模式的缺省 stageMinutes。
AUTO_CYCLE_STAGE_MINUTES_DEF = 30
AUTO_CYCLE_STAGE_MINUTES_DEFAULTS = {
    "produce": 30,
    "backwash": AUTO_CYCLE_BACKWASH_MINUTES_DEF,
    "rinse": AUTO_CYCLE_RINSE_MINUTES_DEF,
    "drain": 10,
}
#: [各阶段缺省切换条件 2026-11-18] 逐模式的 stageCfg 缺省。
#:   · 产水（主运行阶段）：**压差触发**（0.5 MPa 大于）+ 定时兜底**关**
#:     （30 分钟的兜底值留着，用户勾上即用）；
#:   · 反洗 / 顺洗 / 放水：**定时触发**（10 分钟）+ 压差**关**。
#: [为什么产水的压差缺省开、反洗的压差缺省关] 产水是主运行阶段，靠滤芯
#: 阻力（压差）判断该洗了；反洗/顺洗是**按时间**走的工艺段。
AUTO_CYCLE_STAGE_CFG_DEF = {
    CFG_AUTO_STAGE_PRESS_ENABLED: False,
    CFG_AUTO_STAGE_PRESS_THRESHOLD: AUTO_CYCLE_PRESS_THRESHOLD_DEF,
    CFG_AUTO_STAGE_PRESS_COMPARE: AUTO_CYCLE_PRESS_COMPARE_DEF,
    CFG_AUTO_STAGE_TIMER_ENABLED: True,
    CFG_AUTO_STAGE_TIMER_MINUTES: AUTO_CYCLE_STAGE_MINUTES_DEF,
}
AUTO_CYCLE_STAGE_CFG_DEFAULTS = {
    "produce": {
        CFG_AUTO_STAGE_PRESS_ENABLED: True,
        CFG_AUTO_STAGE_PRESS_THRESHOLD: AUTO_CYCLE_PRESS_THRESHOLD_DEF,
        CFG_AUTO_STAGE_PRESS_COMPARE: AUTO_CYCLE_PRESS_COMPARE_DEF,
        CFG_AUTO_STAGE_TIMER_ENABLED: False,
        CFG_AUTO_STAGE_TIMER_MINUTES: AUTO_CYCLE_STAGE_MINUTES_DEF,
    },
    "backwash": {
        CFG_AUTO_STAGE_PRESS_ENABLED: False,
        CFG_AUTO_STAGE_PRESS_THRESHOLD: AUTO_CYCLE_PRESS_THRESHOLD_DEF,
        CFG_AUTO_STAGE_PRESS_COMPARE: AUTO_CYCLE_PRESS_COMPARE_DEF,
        CFG_AUTO_STAGE_TIMER_ENABLED: True,
        CFG_AUTO_STAGE_TIMER_MINUTES: AUTO_CYCLE_BACKWASH_MINUTES_DEF,
    },
    "rinse": {
        CFG_AUTO_STAGE_PRESS_ENABLED: False,
        CFG_AUTO_STAGE_PRESS_THRESHOLD: AUTO_CYCLE_PRESS_THRESHOLD_DEF,
        CFG_AUTO_STAGE_PRESS_COMPARE: AUTO_CYCLE_PRESS_COMPARE_DEF,
        CFG_AUTO_STAGE_TIMER_ENABLED: True,
        CFG_AUTO_STAGE_TIMER_MINUTES: AUTO_CYCLE_RINSE_MINUTES_DEF,
    },
    "drain": {
        CFG_AUTO_STAGE_PRESS_ENABLED: False,
        CFG_AUTO_STAGE_PRESS_THRESHOLD: AUTO_CYCLE_PRESS_THRESHOLD_DEF,
        CFG_AUTO_STAGE_PRESS_COMPARE: AUTO_CYCLE_PRESS_COMPARE_DEF,
        CFG_AUTO_STAGE_TIMER_ENABLED: True,
        CFG_AUTO_STAGE_TIMER_MINUTES: AUTO_CYCLE_STAGE_MINUTES_DEFAULTS["drain"],
    },
}
#: 各分钟项允许范围（1~1440 分钟）。
AUTO_CYCLE_MINUTES_MIN = 1
AUTO_CYCLE_MINUTES_MAX = 1440
#: 压差阈值允许范围（MPa）。
AUTO_CYCLE_PRESS_THRESHOLD_MAX = 10.0
STORAGE_MAX_MB_MIN = 0
STORAGE_MAX_MB_MAX = 1024 * 1024

#: 落盘**入库周期缺省**（毫秒；1 分钟）与**容量上限缺省**（MB；50）。
#: 2026-11-11 用户定案：入库周期默认 1 分钟、容量默认封顶 50MB。
#: 界面在**收到服务端应答之前**先用这两个值起控件（应答到达后以服务端
#: 为准）—— 缺省与服务端一致，才不会出现"界面写着 5s、库里却是 1 分钟"。
#: [两端同口径] 服务端 proto.STORAGE_SAMPLE_MS_DEF / STORAGE_MAX_MB_DEF
#: （server_py/hsfsrv/proto.py）、C 侧 HSF_STORAGE_SAMPLE_MS_DEF /
#: HSF_STORAGE_MAX_MB_DEF（common/XhsfLinkProto.h），三处同值。
STORAGE_SAMPLE_MS_DEF = 60000
STORAGE_MAX_MB_DEF = 50
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

# ---- 自动模式配置（auto 配置节；2026-11-11 用户定案）----
#
#: [两端同步点] server_py/hsfsrv 的 proto.py / config.py / app.py 已于
#: 同日实现 auto 配置节与三模式序列（服务端消费 + 启停）；C 端已同步
#: （XhsfLinkProto.h + HsfServerApp.c + HsfServerAutoMode.c）。
#: [C 端扩展键 2026-11-13] C 服务端 auto 节另收发 "buttons" 子节（按键
#: 触点/去抖）；Python 服务端不经 0x0117 收发它，客户端 GET 应答带它时
#: 按「应答缺键不误改」纪律忽略即可。
CFG_SECTION_AUTO = "auto"

#: 自动模式：(协议稳定 id, 界面显示名)。顺序＝下拉顺序＝配置块顺序。
#:
#: [2026-11-18 订正] 用户先说「放水模式不进自动模式」，现场看板上拉里没有
#: 它 ⇒ 订正为：**放水模式是普通的可配置、可选、可启动的自动模式**，只是
#: **不进组合循环「自动（流量循环）」**（见 AUTO_CYCLE_MODES）。
#: ⇒ 首页「模式」下拉 = 这四个 + 组合「自动」；远端设置配置块 = 这四个。
AUTO_MODES = (
    ("produce", "产水模式"),
    ("backwash", "反洗模式"),
    ("rinse", "顺洗模式"),
    ("drain", "放水模式"),
)

#: **所有服务端都认识**的模式子集（通用子集，三个）。
#:
#: [为什么单独有这张表 2026-11-18] 下发给服务端时，若**还不知道**服务端
#: 认识哪些模式（还没收到 auto 回显），只能保守发这三个 ⇒ C 服务端只
#: 实现三个模式、且对**未知模式键整包拒** ⇒ 多发一个「放水」就会让
#: 整包失败（连产水/反洗/顺洗都存不进去）。⇒ 这里的「通用子集」**不能**
#: 用 AUTO_MODES（它含放水＝四个），必须是这张三模式的表。
AUTO_MODES_COMMON = (
    ("produce", "产水模式"),
    ("backwash", "反洗模式"),
    ("rinse", "顺洗模式"),
)

#: **组合循环「自动（流量循环）」的子模式集合**（三个，不含放水）。
#: [与 server_py/hsfsrv/proto.py 逐值同步] 同名同值同序。
AUTO_CYCLE_MODES = (
    ("produce", "产水模式"),
    ("backwash", "反洗模式"),
    ("rinse", "顺洗模式"),
)
#: **可参与组合循环的模式池**（2026-11-18 用户定案：循环模式可勾选 + 可排序）。
#: 四个（含「放水模式」）——远端设置页「参与循环的模式与顺序」列表按它渲染
#: 全部候选行；勾上才参与（写进 auto.cycle.cycleModes）。
#:
#: [与 AUTO_CYCLE_MODES 的区别] AUTO_CYCLE_MODES 是**既有服务端**实现的
#: 循环子集（三个）；本表是**界面候选池**（四个）——服务端 2026-11-18 起
#: 支持 cycleModes 后，放水模式也能进循环了。提交时仍按服务端回显的模式
#: 集合过滤（见 pages_other 的 _cycle_allowed_modes）。
AUTO_CYCLE_MODE_POOL = AUTO_MODES
#: 组合模式「自动」：已实现（2026-11-12）——按流量循环调用三个模式
#: （产水→反洗→顺洗循环 + 阶段记忆）；启动按循环配置校验，未启用/
#: 子模式缺设备会明确拒并回原因。
AUTO_MODE_AUTO = "auto"
AUTO_MODE_IDS = tuple(m for m, _t in AUTO_MODES) + (AUTO_MODE_AUTO,)

#: **可配置**的自动模式集合（2026-11-18 用户定案「增加一个放水模式」）。
#: 远端设置页按它渲染配置块（三模式 + 放水 = 四块）；**不放水**进首页
#: 下拉、也不进组合循环的子模式集合。
#: [与 server_py/hsfsrv/proto.py 逐值同步] 同名同值同序。
#: **可配置**的自动模式集合（远端设置页的配置块）。
#: [2026-11-18 订正] 与 AUTO_MODES **同源**（四个，含放水）——放水是普通
#: 的可配置/可启动模式，只是**不进组合循环**（AUTO_CYCLE_MODES）。
#: [与 server_py/hsfsrv/proto.py 逐值同步] 同名同值同序。
AUTO_CONFIG_MODES = AUTO_MODES
AUTO_CONFIG_MODE_IDS = tuple(m for m, _t in AUTO_CONFIG_MODES)

#: 每个模式可勾选启动的设备：(协议稳定 id, 界面显示名)。
#: [id 命名口径] valve1..6 对应现场 6 台电动球阀（DO=i）；stirPump＝
#: 变频器驱动的搅拌泵（搅拌电机）；inletPump＝进水泵（DO6）。
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

#: 搅拌电机转速范围与缺省（rpm；铭牌额定点 50Hz≈1440rpm，与变频器页
#: DEFAULT_SPEED_MODE=转速 同口径）。界面在框旁显示 rpm→Hz 折算提示。
AUTO_STIR_RPM_MIN = 0
AUTO_STIR_RPM_MAX = 1440
AUTO_STIR_RPM_DEF = 1440
#: 折算基点（rpm/Hz 对）——界面提示用；与变频器页现场口径一致。
AUTO_STIR_RATED_RPM = 1440.0
AUTO_STIR_RATED_HZ = 50.0

#: 每模式键：**停止自动运行**时是否把电动球阀关回原位（复位）。
#: 缺省 True = 保持既有行为（停止即全停含关阀）。
AUTO_MODE_RESET_VALVES_ON_STOP = "resetValvesOnStop"
#: 每模式键：**急停**时是否把电动球阀关回原位（复位）。
#: 缺省 False —— 与用户口径一致：「按下急停后要停止所有设备(电动球阀可以
#: 不动)」；急停是人身安全事件，该停的是泵与变频器，阀可能正停在中间位。
AUTO_MODE_RESET_VALVES_ON_ESTOP = "resetValvesOnEstop"
#: 两个开关的缺省（True=关阀复位 / False=保持现状）。
AUTO_MODE_RESET_VALVES_ON_STOP_DEF = True
AUTO_MODE_RESET_VALVES_ON_ESTOP_DEF = False

#: 每模式键：**搅拌泵**在换相（切到本模式）期间的处置策略。
#: 每模式键：**进水泵**在换相期间的处置策略。
#: [与 server_py/hsfsrv/proto.py 逐值同步]
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
#: 三个策略的单值别名。
AUTO_SWITCH_POLICY_STOP_RESTART = "stopRestart"
AUTO_SWITCH_POLICY_CARRY = "carry"
AUTO_SWITCH_POLICY_DIRECT = "direct"

#: 运行状态键（CMD_RUN_MODE 查询 / 0x0115 应答；与 server_py proto 逐值同步）。
#: 当前（选中/正在运行的）自动模式 id（空串=未选）。
KEY_AUTO_MODE = "autoMode"
#: 控制器相位：idle / valves（阀动作中）/ pumps（启动泵）/ running / estop。
KEY_AUTO_PHASE = "autoPhase"
#: 报警锁存（bool）：模式序列报警后必须复位才能再启动。
KEY_AUTO_FAULT = "autoFault"
#: **累计自动运行时长**（毫秒；2026-11-12 用户定案：主界面显示
#: 「自动运行 天 时 分 秒」）。只在自动运行期间累加，停止后冻结保留、
#: 多次运行累计。**服务端计时**（唯一事实源）——客户端断线/重启会归零。
KEY_AUTO_RUN_MS = "autoRunMs"
#: **当前子阶段（模式）已运行时长**（毫秒；2026-11-12 用户定案：主界面
#: 自动模式卡显示「剩余时间」表盘）。组合模式（auto）下按本阶段计时
#: （阀相→泵相→运行相），换相归零；**-1 = 不在运行/没有阶段**。
#: 与 server_py/hsfsrv/proto.py 的 KEY_AUTO_STAGE_ELAPSED_MS 逐值同步。
KEY_AUTO_STAGE_ELAPSED_MS = "autoStageElapsedMs"
#: **当前子阶段总时长**（毫秒；0 = 本阶段无定时，剩余不可知）。反洗/
#: 顺洗=配置分钟数；产水=仅定时兜底开启时才有（默认按压差切换）。
KEY_AUTO_STAGE_TOTAL_MS = "autoStageTotalMs"
#: **最近一条报警**（2026-11-19 用户定案「主页加一张最近报警卡」）。
#:
#: 值 = {"kind":<来源类别 str>, "dev":<设备类别 int|缺>,
#:        "text":<告警原文 str>, "atMs":<epoch 毫秒 int>}
#: **无报警时为 null**（不是空文档 —— 空文档与"没这个键"分不开）。
#:
#: [为什么放在状态快照里] 报警历史本来走 0x011A 分页命令（报警页用），
#: 但主页要的是**一行**摘要 ⇒ 状态快照里多带一份最近行即可（客户端本来
#: 就 1s 轮询一次状态），不必让主页再发一条分页命令。
#:
#: [老服务端兼容] 缺这个键 ⇒ model.last_alarm 保持 None ⇒ 界面显示
#: 「无报警」；**不报错、不崩**（与 autoRunMs 的缺键口径同款）。
#:
#: 三端键名逐字一致：server_py/hsfsrv/proto.py 的 KEY_LAST_ALARM、
#: webui_py/hsfweb/proto.py 的 KEY_LAST_ALARM。
KEY_LAST_ALARM = "lastAlarm"
#: lastAlarm 对象内的键（与服务端 alarms 表的列同源）。
LAST_ALARM_KEY_KIND = "kind"      #: 来源类别（health/auto/valve/device/…）
LAST_ALARM_KEY_DEV = "dev"        #: 设备类别（proto.KIND_*；缺 = 无）
LAST_ALARM_KEY_TEXT = "text"      #: 告警原文
LAST_ALARM_KEY_AT_MS = "atMs"     #: 事件时刻（epoch 毫秒，墙钟）
#: **自动运行告警推送主题**（载荷 = 单条告警 BSON {kind,atMs,text}；
#: 事件驱动）。与 server_py proto / C 侧 XhsfLinkProto.h 的
#: HSF_TOPIC_AUTO_ALARM 同号。客户端**常驻订阅**（bridge 连接期订阅、
#: 重连补订，不进页面订阅表——事件发生在哪个页面不可预知，漏采不可补，
#: C 端 HsfLinkClient hsfLcSubscribeTopics 同款），报警页落表。
TOPIC_AUTO_ALARM = 0x0001000B
#: 告警帧键（与 server_py app._drain_auto_alarms / C 侧
#: HSF_KEY_AUTOALARM_* 逐字一致）。
AUTOALARM_KEY_KIND = "kind"
#: 发生时刻（毫秒；**两端口径不同**：C 墙钟 / Python 单调钟）——展示一律
#: 用客户端接收时刻，不要拿它当跨端时间轴。
AUTOALARM_KEY_AT_MS = "atMs"
AUTOALARM_KEY_TEXT = "text"
#: 告警帧的**设备类别**键（int；model.KIND_TEXT 下标；2026-11-15）。
#: 缺键 = 老服务端 ⇒ 报警页按"自动运行"落表（不崩、不改老行为）。
AUTOALARM_KEY_DEV = "dev"
#: 自动运行告警类型：动作下发失败。
AUTO_ALARM_ACTION_FAILED = 1
#: 自动运行告警类型：依赖设备不可用。
AUTO_ALARM_DEVICE_UNAVAILABLE = 2
#: 自动运行告警类型：逻辑自定义。
AUTO_ALARM_CUSTOM = 3
#: 自动运行告警类型：急停触发。
AUTO_ALARM_ESTOP = 4
#: 自动运行告警类型：模式序列阀动作超时（到位等不到）。
#: 与 server_py proto / C 侧 XhsfLinkProto.h HSF_AUTO_ALARM_VALVE_TIMEOUT
#: 逐值同步（三方已一致）。
AUTO_ALARM_VALVE_TIMEOUT = 5
#: 自动运行告警类型：**流量计报警**（上限/下限/空管/系统；含"进水泵开着
#: 仍报空管"的保护动作）。
#: 与 server_py proto / C 侧 XhsfLinkProto.h HSF_AUTO_ALARM_FLOWMETER
#: 逐值同步（三方已一致）。
AUTO_ALARM_FLOWMETER = 6

#: 自动运行告警类型：**设备故障**（变频器故障等设备级故障；2026-11-15）。
#: 与 server_py proto / C 侧 XhsfLinkProto.h HSF_AUTO_ALARM_DEVICE_FAULT
#: 逐值同步。
AUTO_ALARM_DEVICE_FAULT = 7
#: 自动运行告警类型：**压差过高**（运行保护的压差过大动作；2026-11-15）。
#: 与 server_py proto / C 侧 XhsfLinkProto.h HSF_AUTO_ALARM_PRESS_HIGH
#: 逐值同步。
AUTO_ALARM_PRESS_HIGH = 8

#: 告警帧里的**设备类别**键（int；model.KIND_TEXT 的下标）。
#: 缺键 = 老服务端 ⇒ 按"自动运行"处理（不崩、不改老行为）。
KEY_AUTOALARM_DEV = "dev"

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

#: 运行日志分页查询（**两端都已实现**，2026-11-09 起；与 C 侧
#: XhsfLinkProto.h 的 HSF_CMD_LOG_PAGE=0x0119 同号。2026-11-13 实测核对口
#: 径：server_py/hsfsrv/app.py `_h_log_page` 与 C 端 HsfServerApp.c
#: hsfAppHandleLogPage 应答键集逐键一致——本端与 C 头注释里「Python 侧
#: 暂无同号命令/不实现」的说法均已过时，勿再照抄）。载荷 {"limit",
#: "offset","beforeTs"?,"loTs"?,"hiTs"?,"category"?,"kw"}；应答 {"ok",
#: "rows":[{ts,category,text}…],"total","count","offset","limit"}。
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

# ==================== 报警历史分页（0x011A，2026-11-17 新增） ====================
#: 报警历史分页查询（**与 server_py/hsfsrv/proto.py 的 CMD_ALARM_PAGE 同号**）。
#:
#: [为什么新增] 报警页此前是数据三页里**唯一不向服务端取数**的一页：客户端与
#: 网页端各自在**本地进程里**算健康边沿 ⇒ 记录只在该进程活着期间存在
#: （网页端是常驻服务、客户端一重启历史全丢）⇒ 用户实报「web 端报警有数据，
#: 客户端报警视图没数据」。⇒ 边沿改由**服务端**记进 alarms 表，两端都按
#: 本命令取**同一份** ⇒ 单一数据源、客户端重启不丢、两端一致。
#:
#: 载荷 {"offset","limit","beforeTs"?,"loTs"?,"hiTs"?,"kw"?}；
#: 应答 {"ok","err"?,"total","count","offset","limit",
#: "rows":[{ts,kind,source,text,dev}]}（ts DESC 取页、页内正序）。
CMD_ALARM_PAGE = 0x011A

# ---- 报警历史分页键（与服务端 proto.py 同字面）----
# [命名避坑 2026-11-17] **不能**用 KEY_ALARM_* 前缀：本文件下方已有
# KEY_ALARM_COUNT="ac" / KEY_ALARM_KIND="k" / KEY_ALARM_TEXT="t"（**阀告警帧**
# 的键，model.take_alarms 认它）⇒ 同名会**互相覆盖**（Python 后定义者胜）
# ⇒ 一旦覆盖，阀告警帧就再也认不出来（阀页的告警静默失效）。
# ⇒ 本组一律用 ALARM_KEY_* 前缀（与服务端 server_py/hsfsrv/proto.py 同名，
# 两端协议常量铁律）。
ALARM_KEY_OFFSET = "offset"      # 与 HIST_KEY_OFFSET 同字面
ALARM_KEY_LIMIT = "limit"        # 与 HIST_KEY_LIMIT 同字面
ALARM_KEY_BEFORE_TS = "beforeTs"  # 与 HIST_KEY_BEFORE_TS 同字面
ALARM_KEY_LO_TS = "loTs"         # 与 HIST_KEY_LO_TS 同字面
ALARM_KEY_HI_TS = "hiTs"         # 与 HIST_KEY_HI_TS 同字面
ALARM_KEY_KW = "kw"              # 与 HIST_KEY_KW 同字面
ALARM_KEY_ROWS = "rows"          # 与 HIST_KEY_ROWS 同字面
ALARM_KEY_TOTAL = "total"        # 与 HIST_KEY_TOTAL 同字面
ALARM_KEY_COUNT = "count"
#: 行对象内键。
ALARM_KEY_TS = "ts"              # 事件时刻（int64，epoch 毫秒）
ALARM_KEY_KIND = "kind"          # 来源类别（health/auto/valve/…）
ALARM_KEY_SOURCE = "source"      # 设备名/来源对象
ALARM_KEY_TEXT = "text"          # 状态文案（"数据过期"/"已恢复正常"…）
ALARM_KEY_DEV = "dev"            # 设备类别（KIND_*；缺省 = 无）

#: 报警页大小上限（与服务端夹取一致）。
ALARM_PAGE_MAX = 50
#: 报警页大小缺省（= 历史页缺省）。
ALARM_PAGE_DEFAULT = 20
#: 「恢复正常」类行的文案后缀（服务端 health 边沿写入）。
ALARM_TEXT_RECOVERED = "已恢复正常"

# ==================== 变频器参数读/写（0x011B，2026-11-18 新增） ====================
#: 变频器参数读/写（**与服务端 server_py/hsfsrv/proto.py 的 CMD_VFD_PARAMS
#: 同号**；C 侧 XhsfLinkProto.h 的 HSF_CMD_VFD_PARAMS 同号）。
#:
#: [这一条做什么] 远程读/改变频器（搅拌泵驱动器）的**功能码参数**
#: （P1.00~P1.05 电机铭牌组起步）。此前只有变频器页的运行控制（启停/给频/
#: 给转），铭牌类参数（额定功率/电流/频率/转速…）现场只能拿面板按。
#:
#: [请求] {"op": 0|1, "values"?: {<addr 十进制字符串>: <raw int>}}
#:   · op=0 读整组（不带 values）——应答 params 含**全部** PARAM_DEFS；
#:   · op=1 写若干项（values = 本次要写的 addr→raw，**只发被改项**）；
#:   · [BSON 键只能是字符串] values 的键是**寄存器地址的十进制字符串**
#:     （str(0xF100)="61696"。注意 0xF100=61696，别与 0xF000=61440 混淆），值是**原始寄存器值**（int，非工程值）——
#:     工程值↔raw 的标度换算由 PARAM_DEFS 的 scale 定义，两端各自换算，
#:     线上只走 raw。
#: [应答] {"ok","err"?,"params":[{code,label,addr,raw,value,unit,writable,
#:   runLocked,applied?}…],"vfdFaulted","runState"}
#:   · **params[].value 是服务端从驱动器回读的工程值**（写后服务端重新
#:     读驱动器再返回）⇒ 界面只认应答里的 value，**绝不显示本地提交值**；
#:   · applied（可选，bool）= 该项本次写入是否被驱动器确认（op=1 才有意义）；
#:   · vfdFaulted / runState = 变频器当前故障位与运行态（**0x3000 原始字**：
#:     1 正转 / 2 反转 / 3 停机——不是 XVfdRunState 枚举，2026-11-18 订正）。
#: [写闸] runLocked=true 或变频器运行中 ⇒ 服务端拒写，err=
#:   「变频器运行中禁止写参数（请先停止）」——客户端把这句**原样**显示
#:   到状态行（服务端文案是唯一事实源，客户端不改写）。
#: [权限] 读=登录即可；写=管理员（PERM_ACCOUNT_ADMIN，与本页其它卡一致）。
CMD_VFD_PARAMS = 0x011B

#: 相机画面：客户端**按需请求一帧**（2026-11-19 新增；与服务端同号）。
#: 应答走 BSON（帧宽/高/字节数/时间戳）；画面本体走分片主题（同一格式）。
CMD_CAMERA_FRAME = 0x011C

# ---- 请求 op 取值（与 server_py proto.VFD_PARAMS_OP_* 同名同值）----
VFD_PARAMS_OP_READ = 0       #: 读整组（values 不带/忽略）
VFD_PARAMS_OP_WRITE = 1      #: 写若干项（values = {addr: raw}）

# ---- 应答/载荷键（与 server_py proto / C 侧 HSF_KEY_VFDP_* 同字面）----
#: 请求键：要写的 {addr 十进制字符串: raw}（复用既有 "values" 键字面）。
KEY_VFDP_VALUES = "values"
#: 应答键：参数条目数组（每元素一个对象，键见 PARAM_DEFS）。
KEY_VFDP_PARAMS = "params"
#: 应答键：变频器当前是否故障（bool）。
KEY_VFDP_FAULTED = "vfdFaulted"
#: 应答键：变频器运行态（int；**0x3000 原始字** 1 正转 / 2 反转 / 3 停机，
#: 不是 XVfdRunState 枚举——2026-11-18 订正）。
KEY_VFDP_RUN_STATE = "runState"
#: 0x3000 原始字 → 中文（2026-11-18 订正：原来是 XVfdRunState 枚举表，
#: 运行中显示"停机"/停机显示"正转运行"，方向反了）。
VFD_RUN_WORD_TEXT = {1: "正转运行", 2: "反转运行", 3: "停机"}
#: 参数条目内键（服务端拼、客户端按名取）。
VFDP_KEY_CODE = "code"            #: 参数号（"P1.00"）
VFDP_KEY_LABEL = "label"          #: 中文名（"电机额定功率"）
VFDP_KEY_ADDR = "addr"            #: 寄存器地址（int，0xF100 段）
VFDP_KEY_RAW = "raw"              #: 原始寄存器值（int）
VFDP_KEY_VALUE = "value"          #: **回读工程值**（float；界面唯一显示源）
VFDP_KEY_UNIT = "unit"            #: 单位（""/"kW"/"V"/"A"/"Hz"/"rpm"）
VFDP_KEY_WRITABLE = "writable"    #: 可写（bool；false=只读展示）
VFDP_KEY_RUN_LOCKED = "runLocked" #: 运行中禁止写（bool）
VFDP_KEY_APPLIED = "applied"      #: 本次写入是否被驱动器确认（可选，bool）
VFDP_KEY_OPTIONS = "options"      #: 枚举项的可选值（可选；[{"value":int,
                                   #:   "label":"普通异步电机"}…]）
VFDP_KEY_VALUE_TEXT = "valueText"  #: 回读值对应的中文名（可选；枚举型）

#: 变频器参数表的**客户端镜像**。
#:
#: [单一事实源在服务端] server_py/hsfsrv/proto.py 的 PARAM_DEFS 是唯一
#: 事实源（服务端按它拼应答/做校验）；本表**与服务端 PARAM_DEFS 保持
#: 逐值一致**（内容按 docs/M512_511_512_513寄存器映射表.md §9.5 的
#: 0xF100 段：有小数位/范围列）⇒ 改任何一个值必须两端同步，否则界面的
#: 量程/小数位与服务端校验口径分叉（用户会看到"界面让填、服务端拒"）。
#:
#: [条目键] code=参数号 / label=中文名 / addr=寄存器地址 / unit=单位 /
#: scale=工程值×scale=raw（即 raw÷scale=工程值，与服务端同式）/
#: writable=可写 / runLocked=变频器运行中禁止写 /
#: decimals=界面小数位（=log10(scale)，显式写出便于核对）/
#: vmin/vmax=工程值量程（=raw 范围÷scale，界面 spinbox 夹取）。
#:
#: [与服务端表的**形状差异**（2026-11-18 核对）] 服务端那张表用 **raw 区间**
#: `min`/`max`（如 P1.01 = 10..10000），本表用**工程量程** `vmin`/`vmax`
#: （1.0..1000.0）——两者**同一含义**（`min ÷ scale == vmin`，
#: `max ÷ scale == vmax`，6 条逐条核对相等）⇒ **不是分歧**，别去"统一"
#: 其中一边。改服务端 min/max 时，必须把本表的 vmin/vmax 同步换算（÷scale）。
#:
#: [RunLocked] 全组都是"运行中锁写"（RunLocked）：变频器**停止**时才能写
#: ——这是驱动器手册口径（电机铭牌参数运行中改 = 危险），不是客户端省事。
#: [枚举型参数的可选项] raw → 中文名。**与服务端同值**（server_py
#: hsfsrv/proto.py 的 PARAM_OPTIONS），来源 = 说明书第 20 页「P1 组 电机
#: 参数」P1.00 电机类型选择。**服务端应答里带 options 时以服务端为准**
#: （本表只是老服务端兜底）——所以两边不一致也不会显示错。
PARAM_OPTIONS = {
    0xF100: ((0, "普通异步电机"), (1, "变频异步电机"),
             (2, "永磁同步电机")),
}

PARAM_DEFS = (
    {"code": "P1.00", "label": "电机类型选择", "addr": 0xF100, "unit": "",
     "scale": 1, "writable": True, "runLocked": True,
     "decimals": 0, "vmin": 0.0, "vmax": 2.0,
     # [枚举型] 有选项 ⇒ 该行渲染下拉框（用户直接选类型）。
     "options": PARAM_OPTIONS[0xF100]},
    {"code": "P1.01", "label": "电机额定功率", "addr": 0xF101, "unit": "kW",
     "scale": 10, "writable": True, "runLocked": True,
     "decimals": 1, "vmin": 1.0, "vmax": 1000.0},
    {"code": "P1.02", "label": "电机额定电压", "addr": 0xF102, "unit": "V",
     "scale": 1, "writable": True, "runLocked": True,
     "decimals": 0, "vmin": 1.0, "vmax": 2000.0},
    {"code": "P1.03", "label": "电机额定电流", "addr": 0xF103, "unit": "A",
     "scale": 100, "writable": True, "runLocked": True,
     "decimals": 2, "vmin": 0.01, "vmax": 655.35},
    # [小数位待实机标定] 手册 0xF104 小数位列写「1（待实机标定）」——
    # 先按 1 对齐服务端；实机标定改了必须两端同批改。
    {"code": "P1.04", "label": "电机额定频率", "addr": 0xF104, "unit": "Hz",
     "scale": 10, "writable": True, "runLocked": True,
     "decimals": 1, "vmin": 0.1, "vmax": 5000.0},
    {"code": "P1.05", "label": "电机额定转速", "addr": 0xF105, "unit": "rpm",
     "scale": 1, "writable": True, "runLocked": True,
     "decimals": 0, "vmin": 1.0, "vmax": 65535.0},
)

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

#: 相机画面推送主题（2026-11-19；与服务端/C 侧同号）。
#: [载荷是 RAW 二进制] 画面字节走 DATATYPE_RAW（BSON 不支持 binary(0x05)），
#: 按下面 CAM_FRAME_* 的片头组帧 ⇒ 一张 704x704 JPEG（41KB）超 XLink 16KB
#: 单帧上限，必须分片（超限会让会话直接断链）。
TOPIC_CAMERA_FRAME = 0x0001000C

# ---- 相机画面分片格式（与服务端/C 侧逐值一致）----
#: 片头（小端 16B）：magic(2) frameId(4) seq(2) total(2)
#:                   width(2) height(2) flags(2)
CAM_FRAME_MAGIC = 0x4341
CAM_FRAME_HDR_LEN = 16
#: 单片数据上限（XLink 16KB 减去 topicId 与片头）。
CAM_CHUNK_MAX = 12288
#: 分片片头位标志。
CAM_FRAME_FLAG_FIRST = 0x0001
CAM_FRAME_FLAG_LAST = 0x0002

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

# ---- 自动控制操作字（CMD_AUTO_CONTROL 载荷的 "op"；与 server_py proto
#      的 AUTO_OP_* 逐值同步，2026-11-12 补）----
#:
#: [为什么客户端要发 op] 旧口径载荷只有 {"on":bool}（true≈启动/false≈停止），
#: 表达不了"急停"与"复位"。服务端一直支持四板斧（op=1/2/3/4），客户端
#: 只能发 on ⇒ 现场复位只能靠按钮组的物理复位按钮。主界面要加「复位」
#: 按钮（清报警锁存/急停锁存），客户端就得能发 op。
AUTO_OP_START = 1     #: 启动自动运行（须已处自动模式 + 无锁存）
AUTO_OP_STOP = 2      #: 停止自动运行（永远放行）
AUTO_OP_ESTOP = 3     #: 急停（锁存；复位前拒绝启动）
AUTO_OP_RESET = 4     #: 复位（解除急停锁存 + 报警锁存；**不启动**）
AUTO_OP_SELECT = 5    #: 只选模式（记粘性+落盘，不启动不动设备；下拉同步用）

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
#: 网络相机（2026-11-20 进设备健康列表；健康表"类型"列按 KIND_TEXT 显示）。
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
