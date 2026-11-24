# -*- coding: utf-8 -*-
"""高速过滤器服务端 —— 线协议常量（与 C 侧头文件逐字对齐）。

本文件是「线格式唯一权威」的 Python 镜像，**不写业务逻辑**，改动必须与
下列文件同步（任一侧漂移都会让对端静默丢弃整帧）：

    Src/XCore/XProtocol/XLink/XLink_config.h     版本/帧头/心跳/认证/上限
    Src/XCore/XProtocol/XLink/XLinkProto.h       消息类/会话子码/能力位/角色
    HighSpeedFilter/common/XhsfLinkProto.h       命令号/主题号/键名
    server_py/hsfsrv/proto.py                    服务端 Python 副本（同源）
    client_py/hsfsrv_client/proto.py             客户端 Python 副本（同源）

[webui 这份副本] 逐常量对齐 client_py/hsfsrv_client/proto.py（2026-11-13
全量重写；旧骨架缺权限位/自动控制/配置读写/浊度与泵命令/日志分页 0x0119）。
改任何值必须三处同步（server_py / client_py / 本文件）——铁律。

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
# 禁止用数值大小比较角色；判定一律走 permissions.can_role。
# 与 C 侧 XhsfLinkProto.h 的 HSF_ROLE_* 逐值一致（XhsfLinkProto.h:712-715）。
ROLE_ANONYMOUS = 0
ROLE_USER = 1
ROLE_ADMIN = 2
ROLE_OPERATOR = 3

# ---- 权限位（与 C 侧 XAccessPerm / 服务端 proto.PERM_* 逐值一致） ----------
# C 侧 HSF_PERM_*（XhsfLinkProto.h:721-727）。
PERM_VIEW = 1 << 0
PERM_DEVICE_CONTROL = 1 << 1
PERM_MODE_SWITCH = 1 << 2
PERM_AUTO_CONTROL = 1 << 3
PERM_ACCOUNT_ADMIN = 1 << 4
PERM_DOWNLOAD = 1 << 5
PERM_ALL = 0x3F

# ---- 登录结果码（AuthResult 载荷首字节；与 C 侧 XLinkAuthStatus 逐值一致） --
# 语义分「永久」与「瞬时」两类（对齐 client_py/xlink.py 的 _on_auth_result）：
#   · 永久（停止自动重连）：BAD_USER / BAD_PASSWORD / LOCKED / NO_CREDENTIAL
#     —— 重试同一凭据永远不会成功，且会把服务端账户失败计数推向锁定。
#   · 瞬时（退避重试）：REJECTED —— 服务端**策略**拒绝（同账并发
#     「该账户已在线」/会话数已达上限）。凭据可能是对的，旧会话被回收后
#     重试即通过。TIMEOUT / PROTOCOL —— 链路噪声/半开连接。
AUTHSTATUS_OK = 0
AUTHSTATUS_BAD_USER = 1
AUTHSTATUS_BAD_PASSWORD = 2
AUTHSTATUS_LOCKED = 3
AUTHSTATUS_REJECTED = 4
AUTHSTATUS_NO_CREDENTIAL = 5
AUTHSTATUS_TIMEOUT = 6
AUTHSTATUS_PROTOCOL = 7

#: 瞬时拒绝（REJECTED）后的自动重连上限（次数）。镜像 C 侧 XLINK_config.h
#: 的 XLINK_AUTH_REJECT_RETRY_LIMIT；达上限才终局。
XLINK_AUTH_REJECT_RETRY_LIMIT = 30

AUTH_METHOD_ITERATED_HMAC = 1
AUTH_PROOF_DOMAIN = 0x01

# ============================================================================
#                        HSF 业务命令号 / 主题号
# ============================================================================

#: **协议规格**里的默认端口（与 C 版 XhsfLinkProto.h 一致），从 ports 取。
#:
#: 注意：这是「C 版规范值」，**不是** Python 版服务端实际监听的端口。
#: Python 版服务端监听 `ports.PY_SERVER_PORT`（缺省 6200，刻意与 C 版错开）。
HSF_XLINK_DEFAULT_PORT = ports.C_SERVER_PORT

CMD_VFD_CONTROL = 0x0101
CMD_VFD_SNAPSHOT = 0x0102
CMD_AI_SNAPSHOT = 0x0103
CMD_TEMPHUM_SNAPSHOT = 0x0104
CMD_RUN_MODE = 0x0105
CMD_FLOWMETER_SNAPSHOT = 0x0106
#: 设备健康（Publish=事件驱动 + Request=断线重连对账拉全量）。
CMD_DEVICE_HEALTH = 0x0107
#: 数字量（16DI/16DO，聚合后 32 点）快照请求。载荷 {"dev":int}。
CMD_DIGITAL_SNAPSHOT = 0x0108
#: 数字量 DO 控制。载荷 {"dev":int,"do":int,"on":bool} 单点，
#: 或 {"dev":int,"mask":int,"values":int} 整组。
CMD_DIGITAL_CONTROL = 0x0109
#: 电动球阀快照请求（Response = 6 路状态 BSON）。载荷空。
CMD_VALVE_SNAPSHOT = 0x0110
#: 电动球阀手动开关（Request 载荷 = {"index":int, "on":bool}）。
CMD_VALVE_CONTROL = 0x0111
#: 浊度仪快照（Response = 两台 BSON）。
CMD_TURBIDITY_SNAPSHOT = 0x0112
#: 进水泵快照（Response = 泵状态 BSON）。
CMD_PUMP_SNAPSHOT = 0x0113
#: 进水泵开关（Request 载荷 = {"index":int, "on":bool}）。
CMD_PUMP_CONTROL = 0x0114

#: 自动运行控制。载荷 {"on":bool}（兼容口径）或 {"op":1..4}（四板斧）；
#: 还可捎带 KEY_AUTO_MODE 指定模式。需自动模式 + PERM_AUTO_CONTROL。
#:
#: [与 CMD_RUN_MODE 分开] 切模式只是**授权**自动可以跑；这条才是
#: 真正按下启动/停止/急停/复位。合成一个动作会让"只想切个模式"变成
#: "设备立刻开始自动运行"——现场认为太危险。
CMD_AUTO_CONTROL = 0x0115

#: 读取服务端配置快照（无需载荷）。应答 = {"ok","collect","storage","login",
#: "auto","storageDbPath","storageStats"}。先 GET 回显当前值，改完 SET 提交。
CMD_CONFIG_GET = 0x0116
#: 设置服务端配置（需管理员 PERM_ACCOUNT_ADMIN）。载荷
#: {"section":"collect"/"storage"/"login"/"auto", …配置键…}；
#: 服务端在线生效并立即落盘。**局部节回显**：应答只带回本节，客户端
#: 按「应答缺键不误改」纪律只刷新自己那一节。
CMD_CONFIG_SET = 0x0117

#: 分页读取服务端历史数据（只读；export=true 时服务端验 PERM_DOWNLOAD）。
#: 载荷 {"offset","limit","beforeTs"?,"loTs"?,"hiTs"?,"kw"?,"kwCols"?,"export"?}；
#: 应答 {"ok","total","rows","cols","dbRows","latestTs","earliestTs",
#: "windowStartTs","windowEndTs","offset","limit"}。
#: [为什么必须分页] 现场库能存十几万行，一次全拉会打爆服务端（SQL 全表
#: 读 + 百 KB 级 BSON）和板端内存；协议载荷上限本身也只有 16KB。
CMD_HISTORY_PAGE = 0x0118

#: 运行日志分页查询（**两端都已实现**，2026-11-09 起；与 C 侧
#: XhsfLinkProto.h 的 HSF_CMD_LOG_PAGE=0x0119 同号。server_py `_h_log_page`
#: 与 C 端 hsfAppHandleLogPage 应答键集逐键一致（2026-11-13 实测）——
#: 「Python 侧不实现该命令」的说法均已过时，勿再照抄）。
#: 载荷 {"limit","offset","beforeTs"?,"loTs"?,"hiTs"?,"category"?,"kw"}；
#: 应答 {"ok","rows":[{ts,category,text}…],"total","count","offset","limit"}。
#:
#: [与 0x0118 的两点不同] ① rows 是**对象数组**（{ts,category,text}），
#: 0x0118 是按 cols 排的值数组；② 本命令无 export 键形态——导出权限闸
#: 只在客户端（web 端按 PERM_DOWNLOAD 在网关层拦）。
CMD_LOG_PAGE = 0x0119

#: 报警历史分页查询（**新命令** 2026-11-17；与 server_py proto.py 的
#: CMD_ALARM_PAGE=0x011A 同号，键集逐键一致——见本文件下方
#: 「报警历史分页键」段）。报警页改「服务端单一数据源」后，网页端经
#: api.py `/api/alarms/page` 转发本命令取数（与桌面客户端 pages_other.
#: AlarmPage 同一份服务端 alarms 表）。
#:
#: [C 侧暂无此命令] XhsfLinkProto.h 目前只有 0x0118/0x0119 ⇒ 上游是
#: 尚未实现 0x011A 的 C 服务端时，本命令回**显式失败包络**（ok:false），
#: 报警页据此回落本地边沿存档（fail-open，见 page11_alarm.js 头注）。
#: 载荷 {"offset","limit","beforeTs"?,"loTs"?,"hiTs"?,"kw"?}；
#: 应答 {"ok","err"?,"total","count","offset","limit",
#: "rows":[{ts,kind,source,text,dev}]}（ts DESC 取页、页内正序，与
#: 0x0118/0x0119 同口径）。
CMD_ALARM_PAGE = 0x011A

#: 变频器参数读/写（**新命令** 2026-11-18；与 server_py hsfsrv/proto.py 的
#: CMD_VFD_PARAMS=0x011B 同号，键集逐键一致——见本文件下方
#: 「变频器参数键」段）。远端设置页「变频器参数」区块经 api.py
#: /api/vfd/params 转发本命令：GET 读真实值 / POST 写参数（管理员）。
#:
#: [C 侧暂无此命令] XhsfLinkProto.h 目前没有 0x011B ⇒ 上游是尚未实现的
#: C 服务端时，hub.request 拿到空应答/否定包络 ⇒ api 层 _with_ok 原样回
#: ok:false + err（**显式失败包络**，前端把原因显示在状态行、不假成功）。
#: 载荷 {"op":0|1, "values"?:{…}}；应答 {"ok","err"?,"params":[…],
#: "vfdFaulted","runState"}（键义见下方 VFD_PARAMS_KEY_*）。
CMD_VFD_PARAMS = 0x011B

#: 相机画面：客户端**按需请求一帧**（2026-11-19 新增；与服务端同号）。
CMD_CAMERA_FRAME = 0x011C

# ---- 推送主题（topicId = 0x00010000 + n；与 C 侧 HSF_TOPIC_* 同值）--------

TOPIC_VFD_STATUS = 0x00010001
TOPIC_AI_STATUS = 0x00010002
TOPIC_TEMPHUM_STATUS = 0x00010003
TOPIC_FLOWMETER_STATUS = 0x00010004
TOPIC_DEVICE_HEALTH = 0x00010005
#: 数字量遥测（逐模块一帧，载荷含 dev 区分 0/1）。
TOPIC_DIGITAL_STATUS = 0x00010006
#: 电动球阀遥测周期推送主题（载荷 = 6 路状态 BSON）。
#:
#: [同一主题也承载告警帧] 超时/互斥故障是**事件**，服务端在同一主题上另发
#: 一帧（键空间 "ac"/"a0_*"，无 count、无 v{i}_*）。订阅一次即可两种
#: 都收到；解析必须先认 KEY_ALARM_COUNT，否则会把告警帧当状态帧把点位清空。
TOPIC_VALVE_STATUS = 0x00010007
#: 浊度仪遥测推送主题（两台合帧；C 侧 HSF_TOPIC_TURBIDITY_STATUS 同号）。
TOPIC_TURBIDITY_STATUS = 0x00010009
#: 进水泵遥测推送主题。
TOPIC_PUMP_STATUS = 0x0001000A
#: 自动运行告警主题（载荷 {"kind","atMs","text"}；空管/阀超时等自动序列
#: 的异常事件）。
TOPIC_AUTO_ALARM = 0x0001000B

#: 相机画面推送主题（2026-11-19；与服务端/C 侧同号）。
#:
#: [为什么网页端也走这个主题] 用户口径（2026-11-19）：**网页端也要从服务端
#: 取数据，不能直连相机** —— 外网访问场景里浏览器在公网、相机在内网，只有
#: 服务端两边都够得到。⇒ webui（本机 HTTP 服务）从上游服务端收**分片**，
#: 组帧后以 `image/jpeg` 喂浏览器（`/api/camera/frame`）。
#: [载荷是 RAW 二进制] 画面字节走 DATATYPE_RAW（BSON 不支持 binary(0x05)）。
TOPIC_CAMERA_FRAME = 0x0001000C

# ---- 相机画面分片格式（与服务端/C 侧逐值一致）----
#: 片头（小端 16B）：magic(2) frameId(4) seq(2) total(2)
#:                   width(2) height(2) flags(2)
CAM_FRAME_MAGIC = 0x4341
CAM_FRAME_HDR_LEN = 16
#: 单片数据上限（XLink 16KB 减去 topicId 与片头）。
CAM_CHUNK_MAX = 12288
#: 分片片头位标志（与 C 头 / 两端 proto 逐值同步）。
CAM_FRAME_FLAG_FIRST = 0x0001
CAM_FRAME_FLAG_LAST = 0x0002
#: [2026-11-19 ES 直传] bit2 = 载荷是编码流 NAL（Annex-B，frameId=NAL 序号，
#: width/height=0）；bit3 = 编码流是 H.265（bit2=0 时恒 0 = JPEG）。
CAM_FRAME_FLAG_ES = 0x0004
CAM_FRAME_FLAG_H265 = 0x0008

# ---- 相机配置三键（2026-11-21 三端对齐补洞；与 client_py proto.py:397-419 逐值同步）----
#: 远端设置 camera 节的传输/编码/本端解码三键。网页端原先只消费服务端推流
#: （连接类常量不需要），但**配置卡**也要能切换这三项——故补齐常量，
#: 与桌面客户端 RemoteSettingsPage 的同名下拉逐值一致。
CFGK_CAMERA_TRANSPORT = "transport"
CAMERA_TRANSPORT_STREAM = "stream"
CAMERA_TRANSPORT_JPEG = "jpeg"
CAMERA_TRANSPORT_CHOICES = (CAMERA_TRANSPORT_STREAM, CAMERA_TRANSPORT_JPEG)
CAMERA_TRANSPORT_DEF = CAMERA_TRANSPORT_STREAM
CFGK_CAMERA_CODEC = "codec"
CAMERA_CODEC_H264 = "h264"
CAMERA_CODEC_H265 = "h265"
CAMERA_CODEC_CHOICES = (CAMERA_CODEC_H264, CAMERA_CODEC_H265)
CAMERA_CODEC_DEF = CAMERA_CODEC_H264
CFGK_CAMERA_HW_DECODE = "hwDecode"
CAMERA_HW_AUTO = "auto"
CAMERA_HW_ON = "on"
CAMERA_HW_OFF = "off"
CAMERA_HW_CHOICES = (CAMERA_HW_AUTO, CAMERA_HW_ON, CAMERA_HW_OFF)
CAMERA_HW_DEF = CAMERA_HW_AUTO

#: [遗留不推] 阀告警独立主题。C 头 XhsfLinkProto.h:1033-1050 明示
#: 「告警随 0x00010007 合帧（`ac` 键判型），新代码勿再使用」——常量保留
#: 只为文档对齐，**web 不订阅**。
TOPIC_VALVE_ALARM = 0x00010008

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
#: 真的在驱动设备"。两者正常同步，但界面**不能**从模式位推断运行位。
KEY_AUTO_RUNNING = "autoRunning"
KEY_DEV = "dev"
KEY_NAME = "name"
KEY_DI = "di"
KEY_DO = "do"
KEY_COUNT = "count"
#: 全量帧标志：True=全量（带通道名等"一次下发"字段，客户端据此重建点表并
#: 收名字）；False=周期增量帧（只带状态位图）。缺省按 True 处理（旧服务端
#: 不发该键但它每拍都带名字）。
KEY_FULL = "full"
KEY_VALID = "valid"
KEY_STALE_MS = "staleMs"
KEY_MASK = "mask"
KEY_VALUES = "values"
KEY_ON = "on"

#: 设备健康快照键（与 C 侧 HsfAppHealthEntry → BSON 的拼法一致；
#: 逐条目键 = "d{i}_name/kind/side/health/error/online/runState/text/addr"）。
KEY_DEV_COUNT = "devCount"
KEY_DEV_SEQ = "seq"
KEY_DEV_TS = "ts"

#: 数字量通道名键（全量帧专用）：与 C 侧 HSF_KEY_DIG_DI_NAME_FMT /
#: HSF_KEY_DIG_DO_NAME_FMT 逐字一致。服务端按阀门点位映射反推生成
#: （"电动球阀1 开到位"这种），客户端不推导。
KEY_DI_NAME_FMT = "din%d"
KEY_DO_NAME_FMT = "don%d"

#: 数字量通道数**上限**（客户端与服务端共用的容量口径）。
#: 现场 32 点（两台 MT3A-IO1632 聚合）；这是**数组容量**，不是实际点数：
#: 实际点数恒由服务端帧里的 count 键给出。
HSF_DIGITAL_CHANNELS = 32

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
#      的 AUTO_OP_* 逐值同步）----
AUTO_OP_START = 1     #: 启动自动运行（须已处自动模式 + 无锁存）
AUTO_OP_STOP = 2      #: 停止自动运行（永远放行）
AUTO_OP_ESTOP = 3     #: 急停（锁存；复位前拒绝启动）
AUTO_OP_RESET = 4     #: 复位（解除急停锁存 + 报警锁存；**不启动**）
#: 只选模式（记粘性 + 落盘，**不启动、不动设备**；下拉同步用）。
#:
#: [为什么要补这个操作字] 参考客户端改「模式」下拉即发 0x0115 op=5
#: （client_py/hsfsrv_client/bridge.py:560-579 send_auto_select）——现场
#: 物理启动按钮用「上次选中的」（服务端粘性），客户端不同步 ⇒ 服务端粘性
#: 为空/过期 ⇒ 物理启动键被拒「未选择自动模式」（2026-11-13 板端实锅）。
#: 网页端原先下拉纯本地（刷新即丢、服务端粘性与界面不同步）⇒ 与参考端
#: 对不齐，按同口径补齐。
#:
#: [两端支持度不同（已知差异）] **Python 端服务端支持 op=5**
#: （server_py/hsfsrv/app.py 的 AUTO_OP_SELECT 分支：记粘性 + persist），
#: **C 端服务端不支持**（common/XhsfLinkProto.h 只有 START/STOP/ESTOP/
#: RESET；server/HsfServerApp.c 的 default 分支回「非法 op 操作字」）⇒
#: 对 C sim 下拉会回「模式选择失败：非法 op 操作字」。参考客户端对 C sim
#: **同样**失败（同一操作字口径）⇒ 两端行为一致，不是网页端独有问题。
AUTO_OP_SELECT = 5

#: 变频器快照**额外**字段（仅 CMD_VFD_SNAPSHOT 应答带，周期推送不带）。
#: 供"转速调速"对齐输入量程与 Hz↔rpm 换算；客户端不写死额定点。
KEY_RATED_RPM = "ratedRpm"
KEY_RATED_HZ = "ratedHz"

#: 运行状态键（CMD_RUN_MODE 查询 / 0x0115 应答；与 server_py proto 逐值同步）。
#: 当前（选中/正在运行的）自动模式 id（空串=未选）。
KEY_AUTO_MODE = "autoMode"
#: 控制器相位：idle / valves（阀动作中）/ pumps（启动泵）/ running / estop。
KEY_AUTO_PHASE = "autoPhase"
#: 报警锁存（bool）：模式序列报警后必须复位才能再启动。
KEY_AUTO_FAULT = "autoFault"
#: **累计自动运行时长**（毫秒）：只在自动运行期间累加，停止后冻结保留、
#: 多次运行累计。**服务端计时**（唯一事实源）。
KEY_AUTO_RUN_MS = "autoRunMs"
#: **当前循环实例标题**（str；「产水模式2」= 同模式第 2 个实例；空串 =
#: 不在循环 / 单模式）。2026-11-27 用户「主界面自动模式卡片当使用自动循环
#: 模式时各模式提示带序号后缀」（单模式不带）。可选键：老服务端缺它 ⇒
#: 按 autoMode 显示，**不编造序号**。
#:
#: [为什么必须靠它判循环 2026-11-27 根修] 服务端运行组合循环时 `autoMode`
#: 报的是**子阶段**（produce/backwash/rinse，见 server_py app._auto_state_extra
#: 与 webui_check 的「组合模式运行中 autoMode == produce」断言），"auto"
#: 只在停止/复位后才回到 `autoMode` ⇒ 拿 `autoMode === "auto"` 判"循环
#: 正在跑"运行中**恒假**（网页端下拉停在「产水模式」的根因）。
#:
#: [两端同步点] server_py/hsfsrv/proto.py 与 client_py/hsfsrv_client/proto.py
#: 的 KEY_AUTO_STAGE_TITLE（逐字同值）。
KEY_AUTO_STAGE_TITLE = "autoStageTitle"
#: **组合循环进度（组内重复遍数）**（2026-11-27 用户「组合有多次循环要在
#: 主界面提示总共几次、当前第几次」）：当前阶段所属**最近一层组**
#: 的显示名 / 当前第几遍（1-based；0=不可用）/ 总遍数（0=不可用；
#: ==1 时客户端不显示）。不在组内 ⇒ ""/0/0。
#:
#: [服务端已实现，语义不可改] server_py/hsfsrv/app.py 的 `_auto_state_extra`
#: 从展开项 `_rep_group/_rep_cur/_rep_total` 取；三端键名逐字一致。
KEY_AUTO_REP_GROUP = "autoRepGroup"
KEY_AUTO_REP_CURRENT = "autoRepCurrent"
KEY_AUTO_REP_TOTAL = "autoRepTotal"
#: **组合循环进度（顶层轮次）**（2026-11-27 用户「远端设置中顶层的也支持
#: 下循环次数，支持一直循环，或指定循环次数后停止」）：当前第几轮
#: （1-based；0=不在循环）/ 配置总轮数（0=一直循环）。
#:
#: [可选键] 老服务端缺键 ⇒ 客户端**不显示**，不编造轮次。
KEY_AUTO_ROUND = "autoRound"
KEY_AUTO_ROUND_TOTAL = "autoRoundTotal"
#: **当前子阶段（模式）已运行时长**（毫秒；**-1 = 不在运行/没有阶段**）。
KEY_AUTO_STAGE_ELAPSED_MS = "autoStageElapsedMs"
#: **当前子阶段总时长**（毫秒；0 = 本阶段无定时，剩余不可知）。
KEY_AUTO_STAGE_TOTAL_MS = "autoStageTotalMs"
#: **最近一条报警**（2026-11-19 用户定案「主页加一张最近报警卡」）。
#:
#: 值 = {"kind":<来源类别 str>, "dev":<设备类别 int|缺>,
#:        "text":<告警原文 str>, "atMs":<epoch 毫秒 int>}
#: **无报警时为 null**（不是空文档 —— 空文档与"没这个键"分不开）。
#:
#: [老服务端兼容] 缺这个键 ⇒ model.last_alarm 保持 None ⇒ 界面显示
#: 「无报警」；**不报错、不崩**（与 autoRunMs 的缺键口径同款）。
#:
#: 三端键名逐字一致：server_py/hsfsrv/proto.py 的 KEY_LAST_ALARM、
#: client_py/hsfsrv_client/proto.py 的 KEY_LAST_ALARM。
KEY_LAST_ALARM = "lastAlarm"
#: lastAlarm 对象内的键（与服务端 alarms 表的列同源）。
LAST_ALARM_KEY_KIND = "kind"      #: 来源类别（health/auto/valve/device/…）
LAST_ALARM_KEY_DEV = "dev"        #: 设备类别（proto.KIND_*；缺 = 无）
LAST_ALARM_KEY_TEXT = "text"      #: 告警原文
LAST_ALARM_KEY_AT_MS = "atMs"     #: 事件时刻（epoch 毫秒，墙钟）
#: 自动运行告警类型：模式序列阀动作超时（与 server_py proto 同值）。
AUTO_ALARM_VALVE_TIMEOUT = 5
#: 自动运行告警类型：**流量计报警**（上限/下限/空管/系统 + 空管保护动作）。
#: 与 server_py proto / client_py proto / C 侧 XhsfLinkProto.h
#: HSF_AUTO_ALARM_FLOWMETER 逐值同步（2026-11-15）。
AUTO_ALARM_FLOWMETER = 6

#: 自动运行告警类型：**设备故障**（变频器故障等；2026-11-15）。
#: 与 server_py proto / client_py proto / C 侧 XhsfLinkProto.h
#: HSF_AUTO_ALARM_DEVICE_FAULT 逐值同步。
AUTO_ALARM_DEVICE_FAULT = 7
#: 自动运行告警类型：**压差过高**（运行保护的压差过大动作；2026-11-15）。
#: 与 server_py proto / client_py proto / C 侧 XhsfLinkProto.h
#: HSF_AUTO_ALARM_PRESS_HIGH 逐值同步。
AUTO_ALARM_PRESS_HIGH = 8
#: [告警 kind 号段 2026-11-25] 8 压差过高 / 9 电箱超温 / 10 电机超温 /
#: 11 压力1过高 / 12 压力2过高（与 server_py / client_py proto 逐值同步）。
AUTO_ALARM_BOX_OVER_TEMP = 9
AUTO_ALARM_MOTOR_OVER_TEMP = 10
AUTO_ALARM_PRESS1_HIGH = 11
AUTO_ALARM_PRESS2_HIGH = 12

#: 运行状态0x3000 原始字口径：1 正转 / 2 反转 / 3 停机——2026-11-18 订正。
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
#                        电动球阀（6 路，1 DO + 2 DI 反馈）
# ============================================================================

#: 电动球阀路数。
HSF_VALVE_COUNT = 6

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
#: 阀名（utf8，可空 → 客户端按"电动球阀 N"显示）。
KEY_VALVE_NAME = "name"
#: 是否已下发"开"指令（bool）。**这是命令意图，不是到位结果**。
KEY_VALVE_OPEN_CMD = "openCmd"
#: **控制命令**的目标态（bool；CMD_VALVE_CONTROL 载荷键）。
#:
#: [为什么与 KEY_VALVE_OPEN_CMD 是两个不同的键] 一个是**命令载荷**的输入键
#: （C 侧 HSF_KEY_VALVE_ON = "on"），一个是**状态快照**的输出键
#: （HSF_KEY_VALVE_OPEN_CMD = "openCmd"）。两者不可混用：服务端只读 "on"，
#: 发 "openCmd":true 会被当作"未给目标态"而取缺省 false ⇒ **每次开阀请求
#: 实际都在关阀**，且应答仍是 ok=True（2026-11-05 实测踩过）。
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
#: "本来就在那儿"。
KEY_VALVE_CMD_RESULT = "cmdResult"

#: 电动球阀状态机（**由两个到位反馈推导，不是命令回显**）。
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
#                        浊度仪 / 进水泵键（v{index}_ 前缀）
# ============================================================================

#: 浊度值 ×100（int；客户端 ÷100 显示两位小数）。
KEY_TURB_NTU = "ntu"
#: 原始寄存器值（int；排查真相用）。
KEY_TURB_RAW = "raw"
#: 该台是否在线（bool）。
KEY_TURB_ONLINE = "online"
#: 水温 ×100（int；**缺键 = 该机型无温度，不是 0℃**）。
KEY_TURB_TEMP = "temp"

# ---- 进水泵键 ----

#: 泵路号（int，0 起）。
KEY_PUMP_INDEX = "index"
#: 泵名（utf8）。
KEY_PUMP_NAME = "name"
#: 命令/回显状态（bool；**无反馈信号**，只反映输出）。
KEY_PUMP_ON = "on"
#: 该泵输出是否可控（bool）。
KEY_PUMP_READY = "ready"

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
#: 数字量模块；客户端按 kind 分组，未知值应降级显示。
KIND_DIGITAL_IO = 5
#: 电动球阀组（6 路；一个健康条目汇总整组）。
#: [命名口径 2026-11-10] 现场就是 6 台规格一样的电动球阀（不是"电磁阀"）。
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
#                     远程配置键（0x0116/0x0117；与 server_py proto 同名同值）
# ============================================================================

# ---- collect 节键：各设备采集开关（bool） ----
# [2026-11-11 两端同口径] C 服务端（HsfServerIo.c applyCollectConfig）已
# **运行期消费**这 8 个开关；缺键回落装配事实（装了就开）。
CFGK_VFD_ENABLED = "vfdEnabled"
CFGK_TEMPHUM_ENABLED = "temphumEnabled"
CFGK_FLOWMETER_ENABLED = "flowmeterEnabled"
CFGK_TURBIDITY_ENABLED = "turbidityEnabled"
#: [逐台开关 2026-11-10] 两台浊度仪是两台独立设备，开关各自可设。
#: 老的 turbidityEnabled 保留为两台共同缺省（兼容旧服务端）。
CFGK_TURBIDITY1_ENABLED = "turbidity1Enabled"
CFGK_TURBIDITY2_ENABLED = "turbidity2Enabled"
CFGK_ANALOG_ENABLED = "analogEnabled"
CFGK_DIGITAL_ENABLED = "digitalEnabled"
#: collect 节键：轮询周期（int 毫秒；服务端夹到 50~60000）。
CFGK_VFD_POLL_MS = "vfdPollMs"
CFGK_TEMPHUM_POLL_MS = "temphumPollMs"
CFGK_TURBIDITY_POLL_MS = "turbidityPollMs"
#: [逐台周期 2026-11-10] 两台浊度仪与流量计各按自己的周期采集。
CFGK_TURBIDITY1_POLL_MS = "turbidity1PollMs"
CFGK_TURBIDITY2_POLL_MS = "turbidity2PollMs"
CFGK_FLOWMETER_POLL_MS = "flowmeterPollMs"
#: collect 节键：滤速计算面积（m²；0=未设置）。滤速 = 瞬时流量 / 面积。
CFGK_FILTER_AREA_M2 = "filterAreaM2"
FILTER_AREA_M2_MIN = 0.0
FILTER_AREA_M2_MAX = 10000.0
FILTER_AREA_M2_DEF = 0.0
CFGK_ANALOG_POLL_MS = "analogPollMs"
CFGK_DIGITAL_POLL_MS = "digitalPollMs"
#: 轮询周期允许范围（服务端越界自动夹取）。
CFG_POLL_MS_MIN = 50
CFG_POLL_MS_MAX = 60000

# ---- storage 节键 ----
CFGK_STORAGE_ENABLED = "enabled"
CFGK_STORAGE_PATH = "path"
CFGK_STORAGE_SAMPLE_MS = "sampleMs"
#: 容量限额（现场硬盘小，必须能封顶）。**键不存在 = 用缺省；显式写 0 = 不限**
#: （服务端 StorageConfig 用 kw.get("max_mb", None) 区分，别写成 or）。
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

#: storage 节键：**历史入库的触发方式**（2026-11-12 用户定案）。
#:   · always（缺省）：持续采集入库（与自动模式无关）；
#:   · auto：仅自动运行时入库（停机/手动期间不记）。
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

#: 落盘**入库周期缺省**（毫秒；1 分钟）与**容量上限缺省**（MB；50）。
#: 2026-11-11 用户定案。**三处同源常量**：C 侧 HSF_STORAGE_SAMPLE_MS_DEF /
#: HSF_STORAGE_MAX_MB_DEF（XhsfLinkProto.h:328,337）、server_py proto 的
#: STORAGE_SAMPLE_MS_DEF / STORAGE_MAX_MB_DEF、本文件——改一处同步另两处。
STORAGE_SAMPLE_MS_DEF = 60000
STORAGE_MAX_MB_DEF = 50
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

# ---- login 节键（账户/登录策略；与 C 侧 HSF_CFGK_LOGIN_* 同名同值）----
#: 写入立即对**新登录**生效并落盘；存量会话不受影响。需 PERM_ACCOUNT_ADMIN。
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
#:
#: [两端同步点] server_py/hsfsrv 的 proto.py / config.py / app.py 已实现
#: auto 配置节与三模式序列；C 端已同步（XhsfLinkProto.h + HsfServerApp.c +
#: HsfServerAutoMode.c）。
#: [C 端扩展键 2026-11-13] C 服务端 auto 节另收发 "buttons" 子节（按键
#: 触点/去抖）；Python 服务端不经 0x0117 收发它，GET 应答带它时按
#: 「应答缺键不误改」纪律忽略即可。
CFG_SECTION_AUTO = "auto"

#: 自动模式：(协议稳定 id, 界面显示名)。顺序＝下拉顺序＝配置块顺序。
#:
#: [2026-11-18 订正] 「放水模式不进自动模式」指的是**不进组合循环**，它本身
#: 是普通的可配置/可选/可启动模式 ⇒ 首页「模式」下拉 = 这四个 + 组合
#: 「自动」；远端设置配置块 = 这四个。
AUTO_MODES = (
    ("produce", "产水模式"),
    ("backwash", "反洗模式"),
    ("rinse", "顺洗模式"),
    ("drain", "放水模式"),
    # [搅拌模式 2026-11-27 用户「默认配置下拉再增加一个搅拌」]
    # 只跑搅拌泵的独立模式（加药/清洗用）：设备由模式配置勾选
    # （通常只 stirPump），参数/触发与其它模式同款。加在**末尾**：
    # 既有顺序与下标语义不变。
    ("stir", "搅拌模式"),
)

#: **组合循环「自动（流量循环）」的**缺省**参与模式集合**（三个，不含放水）。
#: [与 server_py/hsfsrv/proto.py 逐值同步] 同名同值同序。
#: [2026-11-18 起它是**缺省值**不是禁令] 参与循环的模式与顺序改由配置决定
#: （auto 节的 cycle 子节）⇒ 可勾选的池子见 AUTO_CYCLE_MODE_POOL（四个，
#: 含放水）——放水**可以**被勾进循环（缺省列表里没有它只是缺省）。
AUTO_CYCLE_MODES = (
    ("produce", "产水模式"),
    ("backwash", "反洗模式"),
    ("rinse", "顺洗模式"),
)
#: **可勾选进循环的模式池**（＝AUTO_MODES 的 id 集合：四个，含放水）。
#: [与 server_py/client_py 的同名常量逐值同步]
AUTO_CYCLE_MODE_POOL = AUTO_MODES
#: 缺省参与循环的模式与顺序（产水→反洗→顺洗；与既有行为一致）。
AUTO_CYCLE_MODES_DEF = ("produce", "backwash", "rinse")
#: 组合模式「自动」：按流量循环调用三个模式（产水→反洗→顺洗循环 +
#: 阶段记忆）；启动按循环配置校验，未启用/子模式缺设备会明确拒并回原因。
AUTO_MODE_AUTO = "auto"
AUTO_MODE_IDS = tuple(m for m, _t in AUTO_MODES) + (AUTO_MODE_AUTO,)

#: **可配置**的自动模式集合（远端设置页 page14 的配置块）。
#: [2026-11-18 订正] 与 AUTO_MODES 同源（四块，含放水）——放水是普通的
#: 可配置/可启动模式；**也能被勾进组合循环**（缺省参与列表里没有它，见
#: AUTO_CYCLE_MODES；可勾选的池子见 AUTO_CYCLE_MODE_POOL）。
#: [与 server_py/hsfsrv/proto.py 逐值同步] 同名同值同序。
AUTO_CONFIG_MODES = AUTO_MODES
AUTO_CONFIG_MODE_IDS = tuple(m for m, _t in AUTO_CONFIG_MODES)

#: 每个模式可勾选启动的设备：(协议稳定 id, 界面显示名)。
#: [id 命名口径] valve1..6 对应现场 6 台电动球阀（DO=i）；stirPump＝
#: 变频器驱动的搅拌泵（搅拌电机）；inletPump＝进水泵（DO6）。
#: [改名 2026-11-23 用户口径] 电动球阀1~6 ⇒ 工艺位号名（id 不变）；
#: 与 server_py proto / 两端客户端逐字一致。
AUTO_DEVICES = (
    ("valve1", "产水阀(1)"),
    ("valve2", "反洗进水阀(2)"),
    ("valve3", "进水阀(3)"),
    ("valve4", "排空阀(4)"),
    ("valve5", "反洗出水阀(5)"),
    ("valve6", "顺洗出水阀(6)"),
    ("stirPump", "搅拌泵"),
    ("inletPump", "进水泵"),
)
AUTO_DEVICE_IDS = tuple(d for d, _t in AUTO_DEVICES)

#: 搅拌电机转速范围与缺省（rpm；铭牌额定点 50Hz≈1440rpm）。界面在框旁
#: 显示 rpm→Hz 折算提示。
AUTO_STIR_RPM_MIN = 0
AUTO_STIR_RPM_MAX = 1440
AUTO_STIR_RPM_DEF = 1440
#: auto 节顶层键：搅拌泵最高转速限制（rpm；100~1440，缺省 1440）。
AUTO_STIR_MAX_RPM = "stirMaxRpm"
AUTO_STIR_MAX_RPM_MIN = 100
AUTO_STIR_MAX_RPM_MAX = 1440
AUTO_STIR_MAX_RPM_DEF = 1440
#: 每模式键：搅拌变频器加速度（rpm/s；0=跟随变频器当前加速时间）。
#: 从 0 加到设定转速约 stirRpm / stirAccelRpmS 秒。
AUTO_MODE_STIR_ACCEL_RPM_S = "stirAccelRpmS"
AUTO_STIR_ACCEL_RPM_S_MIN = 0
AUTO_STIR_ACCEL_RPM_S_MAX = 1440
AUTO_STIR_ACCEL_RPM_S_DEF = 0
#: 折算基点（rpm/Hz 对）——界面提示用；与变频器页现场口径一致。
AUTO_STIR_RATED_RPM = 1440.0
AUTO_STIR_RATED_HZ = 50.0

#: 每模式键：**停止自动运行**时是否把电动球阀关回原位（复位）。
#: 缺省 True = 保持既有行为（停止即全停含关阀）。
AUTO_MODE_RESET_VALVES_ON_STOP = "resetValvesOnStop"
#: 每模式键：**急停**时是否把电动球阀关回原位（复位）。
#: 缺省 False —— 与用户口径一致：「按下急停后要停止所有设备(电动球阀可以
#: 不动)」；急停是人身安全事件，该停的是泵与变频器。
AUTO_MODE_RESET_VALVES_ON_ESTOP = "resetValvesOnEstop"
#: 两个开关的缺省（True=关阀复位 / False=保持现状）。
AUTO_MODE_RESET_VALVES_ON_STOP_DEF = True
AUTO_MODE_RESET_VALVES_ON_ESTOP_DEF = False

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
#: [2026-11-18 起为**旧键**] 新的「每阶段各自的切换条件」走
#: CFG_AUTO_CYCLE_STAGE_CFG；旧键被服务端**折进对应阶段**（只改值）⇒
#: 客户端**不再提交**它们（发一次就会悄悄覆盖别的端配好的该阶段条件）。
CFG_AUTO_CYCLE_BACKWASH_MINUTES = "backwashMinutes"
#: 顺洗运行时间（**分钟**）——到期切产水（同上：旧键）。
CFG_AUTO_CYCLE_RINSE_MINUTES = "rinseMinutes"

# ---- 自动循环新契约（2026-11-18 用户定案；三端同口径）----
#
#: [cycleModes] **字符串数组**，元素是模式 id；**数组顺序＝循环顺序**：
#: 第一个＝**主运行阶段**，最后一个的下一个是第一个（闭环）；长度 ≥ 2。
#: 不在数组里的模式＝不参与循环（仍留在可勾选的池子里）。
CFG_AUTO_CYCLE_MODES = "cycleModes"
#: [stageCfg] **对象** {模式 id: {…}}：**每个参与阶段各自**的切换条件。
#: 只含**参与**循环的模式（未参与的不提交）。
#:   · 压差触发：pressEnabled + pressThreshold + pressCompare
#:     （压差读不到 ⇒ 不切）；
#:   · 定时触发：timerEnabled + timerMinutes；
#:   · 两个触发**都启用 ⇒ 先到先切**；**都没开 ⇒ 服务端明确拒**
#:     （fail-closed：「阶段 X 未设置切换条件（压差/定时至少一个）」）。
CFG_AUTO_CYCLE_STAGE_CFG = "stageCfg"
#: stageCfg 里每个阶段的键（五个，**与服务端/桌面端逐字一致**）。
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

# ---- 自动循环实例化（2026-11-26 用户定案；三端同口径）----
#
#: [instances] **有序数组**，每个元素一个「模式实例」对象；顺序＝循环顺序，
#: 第一个＝起始实例，末位回绕首位。**同一模式可重复出现多次**（同 mode
#: 多实例靠下标区分，不再按 mode id）。旧键（cycleModes/stageCfg/六扁平键）
#: 降级为**派生兼容视图**：GET 会回（由实例算出，有损——同 mode 多实例只
#: 保留首个）、SET 在「未显式配置 instances」时走旧合成链、已显式时写穿
#: 到对应实例；客户端只发 {enabled, instances}。
CFG_AUTO_CYCLE_INSTANCES = "instances"
#: **顶层总循环次数**（2026-11-27 用户「自动循环模式，远端设置中顶层的也
#: 支持下循环次数，支持一直循环，或指定循环次数后停止」）。
#:
#: 0 = **一直循环**（缺省，保持既有行为）；N>=1 = 展开计划完整跑完 N 轮后
#: 自动停止（等价现场停止按钮）。配置文件落盘键 `top_repeat`（snake_case，
#: 与 cycle 其它键同风格）。SET 非整数 ⇒ fail-closed 拒（文案含 topRepeat），
#: 整数越界钳位到 0..999（与 minutes 同口径）。运行中改不立即生效（启动时
#: 快照，停止再启动才生效）。与 server_py/client_py proto 逐值同步。
CFG_AUTO_CYCLE_TOP_REPEAT = "topRepeat"
AUTO_CYCLE_TOP_REPEAT_MIN = 0
AUTO_CYCLE_TOP_REPEAT_MAX = 999
AUTO_CYCLE_TOP_REPEAT_DEF = 0
#: 实例对象的键（协议面 camelCase，**与 server_py/client_py proto 逐字一致**）。
CYCLE_INST_MODE = "mode"                          #: 模式 id（必填，池内）
#: **实例自定义名称**（2026-11-27）：空串 = 按默认规则「产水模式N」；非空 =
#: 界面/日志原样使用。限长 CYCLE_INST_TITLE_MAX（超长 SET 由服务端拒绝）。
CYCLE_INST_TITLE = "title"
CYCLE_INST_TITLE_MAX = 24
#: **嵌套组合实例**（2026-11-27 用户「自动循环支持嵌套，可以添加一个组合
#: 模式，组合模式中可以再添加多个模式并且可以设置循环几遍」）：
#:   · `mode` 取 CYCLE_INST_GROUP = "group" ⇒ 这一项是**组**（容器，不参与
#:     触发；展开 = children 顺序 × repeat 遍）；
#:   · `children` = 组内子实例数组（**可递归**：子项也可以是组合，嵌套层数
#:     上限 CYCLE_GROUP_DEPTH_MAX——顶层实例算第 1 层）；
#:   · `repeat` = 组内序列重复遍数（CYCLE_INST_REPEAT_MIN ~ MAX，缺省 1）；
#:   · 组是容器、不参与触发；展开 = children 顺序 × repeat 遍（递归展开）。
#: 与 server_py/client_py proto 逐值一致（camelCase 是线上键）。
CYCLE_INST_GROUP = "group"
#: 组合的**显示名/默认名**（不是可运行模式；与 client proto 的
#: AUTO_INST_GROUP_LABEL 同值）。添加时默认写这个名字，同层重名追加数字。
CYCLE_INST_GROUP_LABEL = "组合"
#: 嵌套层数上限（顶层实例算第 1 层；与 server CYCLE_GROUP_DEPTH_MAX 同值）。
#: 界面在达到上限时把「组合」选项置灰并提示（服务端同样 fail-closed）。
CYCLE_GROUP_DEPTH_MAX = 3
CYCLE_INST_CHILDREN = "children"
CYCLE_INST_REPEAT = "repeat"
CYCLE_INST_REPEAT_MIN = 1
CYCLE_INST_REPEAT_MAX = 99
CYCLE_INST_DEVICES = "devices"                    #: 启动设备 id 数组（缺省 []）
CYCLE_INST_PRESS_ENABLED = "pressEnabled"         #: 压差触发（bool, 缺省 false）
CYCLE_INST_PRESS_THRESHOLD = "pressThreshold"     #: 压差阈值（MPa, 缺省 0.5）
CYCLE_INST_PRESS_COMPARE = "pressCompare"         #: 压差方向（"gt"/"lt"）
CYCLE_INST_PRESS1_ENABLED = "press1Enabled"       #: 压力1触发（bool, 缺省 false）
CYCLE_INST_PRESS1_THRESHOLD = "press1Threshold"   #: 压力1阈值（MPa, 缺省 1.0）
CYCLE_INST_PRESS1_COMPARE = "press1Compare"       #: 压力1方向（"gt"/"lt"）
CYCLE_INST_PRESS2_ENABLED = "press2Enabled"       #: 压力2触发（bool, 缺省 false）
CYCLE_INST_PRESS2_THRESHOLD = "press2Threshold"   #: 压力2阈值（MPa, 缺省 1.0）
CYCLE_INST_PRESS2_COMPARE = "press2Compare"       #: 压力2方向（"gt"/"lt"）
#: [浊度触发 2026-11-27 用户「切换条件增加出水浊度」+「进水浊度也加
#: 进去」] 单位 NTU（float）；进水缺省 5.0 / 出水 1.0，都缺省关。
CYCLE_INST_TURB1_ENABLED = "turb1Enabled"         #: 进水浊度触发（bool）
CYCLE_INST_TURB1_THRESHOLD = "turb1Threshold"     #: 进水浊度阈值（NTU, 5.0）
CYCLE_INST_TURB1_COMPARE = "turb1Compare"         #: 进水浊度方向（"gt"/"lt"）
CYCLE_INST_TURB2_ENABLED = "turb2Enabled"         #: 出水浊度触发（bool）
CYCLE_INST_TURB2_THRESHOLD = "turb2Threshold"     #: 出水浊度阈值（NTU, 1.0）
CYCLE_INST_TURB2_COMPARE = "turb2Compare"         #: 出水浊度方向（"gt"/"lt"）
CYCLE_INST_TIMER_ENABLED = "timerEnabled"         #: 定时触发（bool, 缺省 false）
CYCLE_INST_TIMER_MINUTES = "timerMinutes"         #: 定时时长（int, 分钟, 缺省 10）
CYCLE_INST_STIR_RPM = "stirRpm"                   #: 搅拌转速（rpm, 缺省 1440）
CYCLE_INST_STIR_REVERSE = "stirReverse"           #: 方向（bool, 缺省 false）
CYCLE_INST_STIR_ACCEL = "stirAccelRpmS"           #: 加速度（rpm/s, 缺省 0=跟随）
CYCLE_INST_RST_STOP = "resetValvesOnStop"         #: 停止关阀复位（bool, 缺省 true）
CYCLE_INST_RST_ESTOP = "resetValvesOnEstop"       #: 急停关阀复位（bool, 缺省 false）
CYCLE_INST_STIR_SWITCH = "stirSwitchPolicy"      #: 搅拌泵换相策略（缺省 stopRestart）
CYCLE_INST_INLET_SWITCH = "inletSwitchPolicy"    #: 进水泵换相策略（缺省 stopRestart）
#: 实例键名清单（载荷/回显逐项处理用它；顺序＝协议清单顺序）。
#: [title 2026-11-27] 普通实例也带 title（空串＝按默认规则命名）；
#: children/repeat 是**组专属**结构键，不进本清单（组是容器，载荷只发
#: {mode,title,repeat,children} 四个键）。
AUTO_CYCLE_INSTANCE_KEYS = (CYCLE_INST_MODE, CYCLE_INST_TITLE,
                            CYCLE_INST_DEVICES,
                            CYCLE_INST_PRESS_ENABLED,
                            CYCLE_INST_PRESS_THRESHOLD,
                            CYCLE_INST_PRESS_COMPARE,
                            CYCLE_INST_PRESS1_ENABLED,
                            CYCLE_INST_PRESS1_THRESHOLD,
                            CYCLE_INST_PRESS1_COMPARE,
                            CYCLE_INST_PRESS2_ENABLED,
                            CYCLE_INST_PRESS2_THRESHOLD,
                            CYCLE_INST_PRESS2_COMPARE,
                            CYCLE_INST_TURB1_ENABLED,
                            CYCLE_INST_TURB1_THRESHOLD,
                            CYCLE_INST_TURB1_COMPARE,
                            CYCLE_INST_TURB2_ENABLED,
                            CYCLE_INST_TURB2_THRESHOLD,
                            CYCLE_INST_TURB2_COMPARE,
                            CYCLE_INST_TIMER_ENABLED,
                            CYCLE_INST_TIMER_MINUTES,
                            CYCLE_INST_STIR_RPM, CYCLE_INST_STIR_REVERSE,
                            CYCLE_INST_STIR_ACCEL, CYCLE_INST_RST_STOP,
                            CYCLE_INST_RST_ESTOP, CYCLE_INST_STIR_SWITCH,
                            CYCLE_INST_INLET_SWITCH)
#: 实例数上限（防手滑 + GET 快照体积；与服务端/桌面端同值）。
AUTO_CYCLE_INSTANCES_MAX = 32
#: 压力1/压力2 显示名常量（与 server_py/client_py proto 逐字同步；
#: 以后改名只动常量——界面标签/提示语都从它取）。
PRESS1_LABEL = "进水压力"
PRESS2_LABEL = "出水压力"
#: 进水/出水浊度显示名（历史表头/趋势序列；与两端 proto 逐字同步）。
TURB1_LABEL = "进水浊度"
TURB2_LABEL = "出水浊度"

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
#: 各分钟项允许范围（1~1440 分钟）。
AUTO_CYCLE_MINUTES_MIN = 1
AUTO_CYCLE_MINUTES_MAX = 1440
#: 压差阈值允许范围（MPa）。
AUTO_CYCLE_PRESS_THRESHOLD_MAX = 10.0
#: [各阶段缺省切换条件] 逐模式的缺省（与 server_py/client_py 同值）：
#: 产水（主运行阶段）＝**压差触发**（0.5MPa 大于才切）+ 定时关；
#: 反洗/顺洗/放水＝**定时触发**（10 分钟）+ 压差关。
AUTO_CYCLE_STAGE_CFG_DEFAULTS = {
    "produce": {
        CFG_AUTO_STAGE_PRESS_ENABLED: True,
        CFG_AUTO_STAGE_PRESS_THRESHOLD: AUTO_CYCLE_PRESS_THRESHOLD_DEF,
        CFG_AUTO_STAGE_PRESS_COMPARE: AUTO_PRESS_COMPARE_GT,
        CFG_AUTO_STAGE_TIMER_ENABLED: False,
        CFG_AUTO_STAGE_TIMER_MINUTES: 30,
    },
    "backwash": {
        CFG_AUTO_STAGE_PRESS_ENABLED: False,
        CFG_AUTO_STAGE_PRESS_THRESHOLD: AUTO_CYCLE_PRESS_THRESHOLD_DEF,
        CFG_AUTO_STAGE_PRESS_COMPARE: AUTO_PRESS_COMPARE_GT,
        CFG_AUTO_STAGE_TIMER_ENABLED: True,
        CFG_AUTO_STAGE_TIMER_MINUTES: AUTO_CYCLE_BACKWASH_MINUTES_DEF,
    },
    "rinse": {
        CFG_AUTO_STAGE_PRESS_ENABLED: False,
        CFG_AUTO_STAGE_PRESS_THRESHOLD: AUTO_CYCLE_PRESS_THRESHOLD_DEF,
        CFG_AUTO_STAGE_PRESS_COMPARE: AUTO_PRESS_COMPARE_GT,
        CFG_AUTO_STAGE_TIMER_ENABLED: True,
        CFG_AUTO_STAGE_TIMER_MINUTES: AUTO_CYCLE_RINSE_MINUTES_DEF,
    },
    "drain": {
        CFG_AUTO_STAGE_PRESS_ENABLED: False,
        CFG_AUTO_STAGE_PRESS_THRESHOLD: AUTO_CYCLE_PRESS_THRESHOLD_DEF,
        CFG_AUTO_STAGE_PRESS_COMPARE: AUTO_PRESS_COMPARE_GT,
        CFG_AUTO_STAGE_TIMER_ENABLED: True,
        CFG_AUTO_STAGE_TIMER_MINUTES: 10,
    },
}

# ============================================================================
#                     历史 / 日志分页键（0x0118 / 0x0119）
# ============================================================================

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
#: [motor_temp 同样在末尾 2026-11-23] 电机温度（模拟量温度传感器）。
#: [press1_mpa/press2_mpa 2026-11-23 继续追加在末尾] 线上列 20→22；
#: 显示列序另有一套（压力1/压力2 插在压差之后），见 export.HISTORY_HEADERS。
HISTORY_COLUMNS = (
    "ts", "mode", "vfd_state", "freq_hz", "current_a", "rpm",
    "turb1", "turb2", "flow", "flow_unit", "flow_total", "total_unit",
    "press_mpa", "press_note", "temp", "hum", "di", "do", "water_temp",
    "motor_temp", "press1_mpa", "press2_mpa")

# ---- 运行日志分页键（与 C 侧 XhsfLinkProto.h HSF_KEY_LOG_* 同字面）----
# C 头注释互相点名：字面值与 0x0118 的 HIST_KEY_* 一致的键，改任何一个
# 必须两处（本文件 + C 头）同步（铁律：同号同键）。
KEY_LOG_LIMIT = "limit"        # 与 HIST_KEY_LIMIT 同字面
KEY_LOG_OFFSET = "offset"      # 与 HIST_KEY_OFFSET 同字面
KEY_LOG_BEFORE_TS = "beforeTs" # 与 HIST_KEY_BEFORE_TS 同字面
KEY_LOG_LO_TS = "loTs"         # 与 HIST_KEY_LO_TS 同字面
KEY_LOG_HI_TS = "hiTs"         # 与 HIST_KEY_HI_TS 同字面
#: 类别过滤（utf8；conn=连接 / ctrl=控制 / alarm=告警 / auto=自动 /
#: auth=鉴权 / sys=系统（Python 服务端专有档，C 端不产生）；空/缺省 = 全部）。
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

#: 日志类别字面（与服务端入库口径一致：C 端 HsfServerStore insertLog；
#: Python 端另产 sys 档）。
LOG_CAT_CONN = "conn"
LOG_CAT_CTRL = "ctrl"
LOG_CAT_ALARM = "alarm"
LOG_CAT_AUTO = "auto"
LOG_CAT_AUTH = "auth"
LOG_CAT_SYS = "sys"

#: 日志页大小上限（服务端夹取；与 C 侧 HSF_LOG_PAGE_MAX 同值）。
LOG_PAGE_MAX = 50
#: 日志页大小缺省（与 C 侧 HSF_LOG_PAGE_DEFAULT 同值，= 历史页缺省）。
LOG_PAGE_DEFAULT = 20

# ============================================================================
#                     报警历史分页键（0x011A，2026-11-17）
# ============================================================================

# [命名避坑，与 client_py proto.py 同款] 本文件上方已有 KEY_ALARM_COUNT="ac"
# / KEY_ALARM_KIND="k" / KEY_ALARM_TEXT="t"（**阀告警帧**的键，阀状态帧
# 判型认它）⇒ 本组一律用 ALARM_KEY_* 前缀，**绝不用** KEY_ALARM_*：
# 同名会互相覆盖（Python 后定义者胜）⇒ 阀告警帧再也认不出来，阀页告警
# 静默失效。字面值与 server_py hsfsrv/proto.py 的 ALARM_KEY_* 逐字一致。
ALARM_KEY_OFFSET = "offset"       # 与 HIST_KEY_OFFSET 同字面
ALARM_KEY_LIMIT = "limit"         # 与 HIST_KEY_LIMIT 同字面
ALARM_KEY_BEFORE_TS = "beforeTs"  # 与 HIST_KEY_BEFORE_TS 同字面
ALARM_KEY_LO_TS = "loTs"          # 与 HIST_KEY_LO_TS 同字面
ALARM_KEY_HI_TS = "hiTs"          # 与 HIST_KEY_HI_TS 同字面
#: 关键字（下推服务端，在 source/text 上匹配；空/缺省 = 不限）。
ALARM_KEY_KW = "kw"               # 与 HIST_KEY_KW 同字面
#: 应答行数组（每元素一个对象 {ts,kind,source,text,dev}）。
ALARM_KEY_ROWS = "rows"           # 与 HIST_KEY_ROWS 同字面
#: 应答：本窗口命中总行数（分页总数按它算）。
ALARM_KEY_TOTAL = "total"         # 与 HIST_KEY_TOTAL 同字面
#: 应答：本页实际行数。
ALARM_KEY_COUNT = "count"         # 与 KEY_LOG_COUNT 同字面
# 行对象内键（与 server_py proto.py / client_py proto.py 同字面）。
ALARM_KEY_TS = "ts"               # 事件时刻（int64，epoch 毫秒，墙钟）
ALARM_KEY_KIND = "kind"           # 来源类别（health/auto/valve/flowmeter/device）
ALARM_KEY_SOURCE = "source"       # 设备名/来源对象
ALARM_KEY_TEXT = "text"           # 状态文案（"数据过期"/"已恢复正常"/告警原文…）
#: 设备类别（KIND_* 的 int；缺省/None = 无——消费方按 kind 兜底显示）。
ALARM_KEY_DEV = "dev"

#: 报警页大小上限（服务端夹取；与 server_py proto.py ALARM_PAGE_MAX 同值，
#: 由 16KB 载荷反推）。
ALARM_PAGE_MAX = 50
#: 报警页大小缺省（= 历史页/日志页缺省）。
ALARM_PAGE_DEFAULT = 20

# ============================================================================
#                     变频器参数键（0x011B，2026-11-18）
# ============================================================================

# [命名避坑，与 ALARM_KEY_* 同理由] 本文件上方 KEY_VALUES="values" 已被
# VFD 控制/遥测帧占用 ⇒ 本组一律用 VFD_PARAMS_KEY_* 前缀，**绝不用**
# KEY_VALUES/KEY_*（同名会互相覆盖，Python 后定义者胜）。字面值与
# server_py hsfsrv/proto.py 的 VFD_PARAMS_KEY_* 逐字一致。
#: op 取值：0=读参数（服务端现读驱动器回真实值）。
VFD_PARAM_OP_READ = 0
#: op 取值：1=写参数（RunLocked/无权限/越界由服务端终审拒绝）。
VFD_PARAM_OP_WRITE = 1

VFD_PARAMS_KEY_OP = "op"
#: 写载荷的参数值表：{寄存器地址 → 原始码}。**BSON 的 e_name 必须是字符
#: 串** ⇒ hub.vfd_params 出站前把 int 地址转 str（服务端按 int(addr) 收）；
#: HTTP/JSON 层（浏览器 → api.py）的键本来就是字符串。
VFD_PARAMS_KEY_VALUES = "values"
#: 应答：参数条目数组（每元素一个对象，键见 VFD_PARAM_ITEM_*）。
VFD_PARAMS_KEY_PARAMS = "params"
#: 应答：变频器是否处于故障（bool）。
VFD_PARAMS_KEY_VFD_FAULTED = "vfdFaulted"
#: 应答：变频器运行状态（XVfdRunState 枚举值，1=停机 2=就绪 3/9=运行 …；
#: 与 webui model.py 的 RUN_STATE_TEXT 同源。消费方缺键按未知显示）。
VFD_PARAMS_KEY_RUN_STATE = "runState"

# 应答 params[] 条目内键（与 server_py proto.py 逐字一致）。
VFD_PARAM_ITEM_CODE = "code"            #: 参数号（"P1.00"…）
VFD_PARAM_ITEM_LABEL = "label"          #: 中文名（"电机类型选择"…）
VFD_PARAM_ITEM_OPTIONS = "options"      #: 枚举项的可选值（[{"value":int,
                                        #:   "label":"普通异步电机"}…]）
VFD_PARAM_ITEM_VALUE_TEXT = "valueText"  #: 回读值对应的中文名（枚举型）
VFD_PARAM_ITEM_ADDR = "addr"            #: 寄存器地址（0xF100…，int）
VFD_PARAM_ITEM_RAW = "raw"              #: 原始码（int；raw = 工程值 × scale）
#: **服务端从驱动器回读的工程值**——页面只认它刷新显示，绝不显示提交值。
VFD_PARAM_ITEM_VALUE = "value"
VFD_PARAM_ITEM_UNIT = "unit"            #: 单位（""=无纲量）
VFD_PARAM_ITEM_WRITABLE = "writable"    #: 是否可写（bool）
#: 运行锁定（true=变频器运行中，此时写会被服务端拒绝）。
VFD_PARAM_ITEM_RUN_LOCKED = "runLocked"
#: 写应答可选键：该项是否已写入并回读确认（bool；缺键=老服务端，按整包
#: ok 判）。
VFD_PARAM_ITEM_APPLIED = "applied"

#: [RunLocked 拒写文案，三端同款] 服务端拒写时 err 的原文（C 端
#: HsfServerApp / py 端 h_vfd_params / web 端只**透传不拼装**——现场排查
#: 时三端看到同一句话）。此处登记仅为对齐契约，web 不引用它造句。
#:
#:   「变频器运行中禁止写参数（请先停止）」

# ----------------------------------------------------------------------------
# 参数集 P1.00~P1.05（0xF100~0xF105 电机铭牌组）——**与服务端逐值一致**
# ----------------------------------------------------------------------------
# 单一事实源 = server_py hsfsrv/proto.py 的 PARAM_DEFS（服务端按它驱动
# 0x011B 的读写）；本表是网页端的**同值镜像**：page14_remote.js 用它把
# 输入的工程值换算成原始码（raw = 工程值 × scale）并给行标签兜底。
# 改参数集必须两处同步（内容出处 = docs/M512_511_512_513寄存器映射表.md
# §9.5 的 0xF100 段）。
#
# scale = 10^小数位（寄存器原始码 = 工程值 × scale；如 P1.03 电流 2 位
# 小数 ⇒ 原始码 123 = 1.23 A）。P1.04 的小数位按手册预置 1，寄存器映射表
# §9.9 标「待实机标定」——标定后两端同步改。
#: [枚举型参数的可选项] raw → 中文名。**与服务端 / 桌面端同值**（server_py
#: hsfsrv/proto.py、client_py hsfsrv_client/proto.py 的 PARAM_OPTIONS），
#: 来源 = 说明书第 20 页「P1 组 电机参数」P1.00 电机类型选择。
#: **网页端优先用服务端应答里的 options**（本表只是老服务端兜底）。
PARAM_OPTIONS = {
    0xF100: ((0, "普通异步电机"), (1, "变频异步电机"),
             (2, "永磁同步电机")),
}

PARAM_DEFS = (
    {"code": "P1.00", "label": "电机类型选择", "addr": 0xF100,
     "unit": "", "scale": 1,
     "writable": True, "runLocked": True,
     "decimals": 0, "vmin": 0.0, "vmax": 2.0,
     # [枚举型] 有选项 ⇒ 该行渲染下拉框（用户直接选类型）。
     "options": PARAM_OPTIONS[0xF100]},
    {"code": "P1.01", "label": "电机额定功率", "addr": 0xF101,
     "unit": "kW", "scale": 10,
     "writable": True, "runLocked": True,
     "decimals": 1, "vmin": 1.0, "vmax": 1000.0},
    {"code": "P1.02", "label": "电机额定电压", "addr": 0xF102,
     "unit": "V", "scale": 1,
     "writable": True, "runLocked": True,
     "decimals": 0, "vmin": 1.0, "vmax": 2000.0},
    {"code": "P1.03", "label": "电机额定电流", "addr": 0xF103,
     "unit": "A", "scale": 100,
     "writable": True, "runLocked": True,
     "decimals": 2, "vmin": 0.01, "vmax": 655.35},
    # [小数位待实机标定] 手册 0xF104 小数位列写「1（待实机标定）」——
    # 先按 1 对齐服务端；实机标定改了必须两端同批改。
    {"code": "P1.04", "label": "电机额定频率", "addr": 0xF104,
     "unit": "Hz", "scale": 10,
     "writable": True, "runLocked": True,
     "decimals": 1, "vmin": 0.1, "vmax": 5000.0},
    {"code": "P1.05", "label": "电机额定转速", "addr": 0xF105,
     "unit": "rpm", "scale": 1,
     "writable": True, "runLocked": True,
     "decimals": 0, "vmin": 1.0, "vmax": 65535.0},
)

# ============================================================================
#                              缺省账户
# ============================================================================

#: 缺省三档建户（服务端空库自动建：Python `auth.create_defaults` /
#: C `XAccountManager_ensureDefaults`；两端名单逐字一致）。
DEFAULT_USER_NAME = "admin"
DEFAULT_USER_PASS = "admin"
DEFAULT_ROOT_NAME = "root"
DEFAULT_ROOT_PASS = "root"
DEFAULT_OPER_NAME = "oper"
DEFAULT_OPER_PASS = "oper"

#: [deprecated] 旧闸值；语义上=PERM_DEVICE_CONTROL。
#: 新代码请用 permissions.can_role（角色值不单调，禁写 role >= 比较）。
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
