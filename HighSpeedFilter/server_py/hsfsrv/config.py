# -*- coding: utf-8 -*-
"""部署配置：设备清单 / 串口参数 / 推送节拍 / 运行期持久化。

[与 C 侧的分工] C 版把「串口号/站号/地址」写成 HsfServerConfig.h 的**编译期
宏**（现场改头文件再编）；Python 版改用**运行期配置文件 + 命令行覆盖**——
现场没有编译环境，改 JSON 重启即可，这也正是本次移植的主要动机之一。

[配置优先级] 命令行 > 配置文件 > 内置缺省。缺键一律用缺省值补齐（老配置
文件加新设备不需要重写整份配置）。
"""

import json
import os

from . import ports
from .ports import default_serial_port


# ============================================================================
#                              缺省值
# ============================================================================

#: 配置文件相对程序根目录的位置（对齐 C 侧 cfg/ 口径）。
DEFAULT_CONFIG_REL = "cfg/hsf_server_py.json"
#: 账户库与配置文件同目录。
DEFAULT_USERS_REL = "cfg/hsf_users.json"
#: 缺省监听端口 —— **从 ports.py 统一取**，不要在这里另写数字。
#:
#: 改端口只需改 `hsfsrv/ports.py` 里的 PY_SERVER_PORT（那个文件有详细说明）。
DEFAULT_PORT = ports.PY_SERVER_PORT


def _next_serial_port(port):
    """从路 A 的串口名推路 B 的缺省口（COM17 → COM18；非 COM 形式原样返回 +2）。

    [为什么只是缺省] 现场两个 USB-485 转换器插在不同 USB 口，Windows 分配
    的 COM 号不一定相邻。这个推导只保证「默认不撞车」，真实口名以现场
    设备管理器为准，由配置 `serial_b.port` 覆盖。
    """
    import re
    m = re.match(r"^(.*?)(\d+)$", str(port))
    if not m:
        return "%s_2" % port
    return "%s%d" % (m.group(1), int(m.group(2)) + 1)


class SerialConfig(object):
    """一路 485 总线（串口号 + 波特率 + 链路参数）。

    [现场是两条独立 485，不是一条] 用户明确：
      · 路 A：变频器 + 温湿度，115200
      · 路 B：流量计 + 浊度仪 ×2，9600
    两条线物理独立、速率不同，不能共用一个 master（共用会让 9600 的设备
    被按 115200 访问，全部不通）。故配置里有两个实例：`serial`（路 A）与
    `serial_b`（路 B）。`make_rtu` 按 (port, baudrate) 缓存，天然分成两个
    master；健康表的地址文案也按设备所属路取不同的口和速率。

    [向后兼容] 老配置文件只有 `serial` 一节，没有 `serial_b`；缺省把路 B
    设成「另一个串口 + 9600」。若现场确实两条线接在同一个口上（不可能，
    速率不同），可由配置显式改。
    """

    __slots__ = ("port", "baudrate", "timeout_ms", "retries",
                 "vfd_poll_ms", "temphum_poll_ms", "temphum_push_ms",
                 "vfd_rated_rpm", "vfd_rated_hz", "vfd_max_hz")

    def __init__(self, **kw):
        self.port = kw.get("port", default_serial_port())
        self.baudrate = int(kw.get("baudrate", 115200))
        self.timeout_ms = int(kw.get("timeout_ms", 400))
        self.retries = int(kw.get("retries", 1))
        #: 变频器是快设备，每拍都给机会；温湿度物理更新 30Hz，500ms 绰绰有余。
        self.vfd_poll_ms = int(kw.get("vfd_poll_ms", 100))
        self.temphum_poll_ms = int(kw.get("temphum_poll_ms", 500))
        self.temphum_push_ms = int(kw.get("temphum_push_ms", 500))
        # ---- 变频器额定点（铭牌参数，用于 Hz ↔ rpm 换算）----
        #: 额定转速（rpm）。现场实测 50Hz 约 1440rpm（4 极异步电机带滑差）。
        #: [为什么用额定点而不是极对数公式] 极对数算出的是同步转速
        #: （4 极 = 1500rpm），与实测差 4%；用铭牌额定点更贴近实际。
        self.vfd_rated_rpm = float(kw.get("vfd_rated_rpm", 1440.0))
        #: 额定频率（Hz），与额定转速同一点。
        self.vfd_rated_hz = float(kw.get("vfd_rated_hz", 50.0))
        #: 最大频率（Hz）：决定频率给定上限（0x1000 的 ±100% 对应它）。
        self.vfd_max_hz = float(kw.get("vfd_max_hz", 50.0))


class AnalogChannelScale(object):
    """单路模拟量的**工程量换算**（4~20mA → 物理量）。

    [口径] 4~20mA 线性映射到 0~满量程：

        p = (I - 4.0) / 16.0 × fullScale

    低于 4mA（断线/欠量程）与高于 20mA（过载/超量程）都**夹到端点**并在
    说明里标出来——不夹会算出负压差或超量程数，看着像真实读数。

    [为什么满量程必须由配置给] 同一个变送器型号有 L/M/H/V 多种膜盒，
    量程差三个数量级（10kPa ~ 14MPa）。写死在代码里等于替现场猜，
    猜错时读数会出现"永远 0.00"或"一上电就满量程"这种极难排查的现象。
    """

    __slots__ = ("index", "name", "unit", "full_scale", "low_ma", "high_ma")

    def __init__(self, index=0, **kw):
        self.index = int(index)
        self.name = kw.get("name", "")
        self.unit = kw.get("unit", "")
        #: 满量程（单位见 unit）；0/None = 未标定（不显示换算值）。
        self.full_scale = float(kw.get("fullScale", 0) or 0)
        self.low_ma = float(kw.get("lowMa", 4.0))
        self.high_ma = float(kw.get("highMa", 20.0))

    @property
    def configured(self):
        return self.full_scale > 0 and self.high_ma > self.low_ma

    def engineering(self, ma):
        """电流(mA) → 工程量；未标定返回 None。

        :returns: (value, text)；text 为空表示正常，非空是越限说明。

        [零点容差] 4~20mA 变送器的活零点实测会略低于 4.000mA
        （本现场 995mV / 249Ω = 3.996mA），那是正常的零点抖动，不是断线。
        若严格按 `< low_ma` 判定，最正常的工作点反而天天报"断线"。
        故留 0.05mA（≈0.3%）容差；低于 low_ma-0.05 才算真的欠量程/断线
        （断线时变送器输出通常 ≤3.6mA，离这个门限很远）。
        """
        if not self.configured or ma is None:
            return None, ""
        span = self.high_ma - self.low_ma
        value = (float(ma) - self.low_ma) / span * self.full_scale
        note = ""
        if ma < self.low_ma - 0.05:
            value = 0.0
            note = "低于 %.1fmA（断线/欠量程）" % self.low_ma
        elif ma > self.high_ma + 0.05:
            value = self.full_scale
            note = "高于 %.1fmA（超量程）" % self.high_ma
        else:
            # 容差带内：夹到 [0, full_scale]，不显示越限提示。
            value = min(max(value, 0.0), self.full_scale)
        return value, note

    def to_doc(self):
        return {"index": self.index, "name": self.name, "unit": self.unit,
                "fullScale": self.full_scale, "lowMa": self.low_ma,
                "highMa": self.high_ma}


class AnalogConfig(object):
    """模拟量输入模块（Modbus TCP）。"""

    __slots__ = ("host", "port", "unit_id", "channels", "poll_ms", "scales")

    def __init__(self, **kw):
        self.host = kw.get("host", "192.168.0.11")
        self.port = int(kw.get("port", 8234))
        self.unit_id = int(kw.get("unit_id", 1))
        self.channels = int(kw.get("channels", 8))
        self.poll_ms = int(kw.get("poll_ms", 200))
        #: 通道号 -> AnalogChannelScale（只放需要换算的通道）。
        #:
        #: [通道 0 默认就是压差计] 现场通道 0 接 EJA110A 差压变送器，
        #: 出厂量程 0~1MPa、2 线制 4~20mA。这属于**设备本身的口径**，
        #: 不是站点私有参数 ⇒ 写进默认值。原先留空，别人拉取后模拟量页
        #: 只显示电流值、没有"压差计 / MPa"名字与换算 —— 与实际不一致。
        self.scales = {
            0: AnalogChannelScale(index=0, name="压差计", unit="MPa",
                                  fullScale=1.0, lowMa=4.0, highMa=20.0),
        }


class DigitalConfig(object):
    """一台数字量模块（Modbus TCP）。

    [点数口径] 单模块物理 16 点；该模块支持**主从聚合**（把另一台 16 点
    模块映射进自己的地址空间，对外 32 点），故点数是**每模块可变**的，
    不能写死 16。但本现场 2026-11-06 起只保留 .13 一台（16 点）——
    .12（原聚合者，对外 32 点）已下线拆除，它的 16DI/16DO 不再出现在
    点表里。默认值按"单台 16 点"给出。
    """

    __slots__ = ("name", "host", "port", "unit_id", "timeout_s", "channels",
                 "publish")

    def __init__(self, name, **kw):
        self.name = name
        self.host = kw.get("host", "192.168.0.13")
        self.port = int(kw.get("port", 502))
        self.unit_id = int(kw.get("unit_id", 1))
        #: 该模块应答实测 0.4~0.8s（比其他设备慢一个数量级），超时给足。
        self.timeout_s = float(kw.get("timeout_s", 2.0))
        #: DI/DO 路数（单模块 16；聚合后 32）。
        #: 起始地址恒 0，且起始+点数不得跨出模块自己的地址空间。
        self.channels = int(kw.get("channels", 16))
        #: 是否发布点位。
        #: [为什么需要] 聚合架构下，被聚合者的点已经出现在聚合者的地址
        #: 空间里。若两台都发布，客户端会看到两套重复点位。但被聚合者
        #: **仍要保留**：它自己是否在线是独立故障信息（模块断电/网线松/
        #: 被聚合链路断，只有它自己的应答能反映）——故照常轮询并出健康
        #: 条目，只是不发点位帧。
        #: [当前现场已无聚合] 只剩 .13 一台 ⇒ 恒为 True（默认值）。
        self.publish = bool(kw.get("publish", True))


class StorageConfig(object):
    """历史/日志/报警的 SQLite 落盘配置（含容量限额）。

    [为什么默认开] 历史数据的价值全在「事后」，进程一重启内存缓存就没了；
    落盘默认开、路径默认落在**服务端程序目录**（不依赖任何安装选择），
    现场不需要做任何配置就能拿到完整数据。要挪到别处（如挂载的大容量
    分区）改 `path` 即可，远端设置页可在线改。

    [容量限额为什么必须给] 现场那台机器硬盘很小（用户明确提出的约束），
    不封顶就会一路写到把盘撑满，最后连系统日志都写不进去。`max_mb=0`
    表示不限（缺省），现场按可用空间填一个值即可；写满后的行为由
    `full_policy` 三选一（见 store.py 模块头注）。
    """

    __slots__ = ("enabled", "path", "sample_ms", "max_kb", "full_policy",
                 "keep_files")

    def __init__(self, **kw):
        self.enabled = bool(kw.get("enabled", True))
        #: 数据库文件路径；**空串 = 服务端程序目录下的 hsf_data.db**。
        self.path = str(kw.get("path", "") or "")
        #: 历史采样周期（毫秒）。历史是慢变量（趋势排查用），5s 一行
        #: 一天约 1.7 万行，SQLite 毫无压力；周期推送（100ms）没必要入库。
        self.sample_ms = int(kw.get("sample_ms", 5000))
        #: 库文件容量上限（**KB**，内部统一口径）；0 = 不限。
        #: 远端协议用 MB（max_mb 属性换算），配置文件也接受 max_kb 直给。
        self.max_kb = int(kw.get("max_kb", 0) or 0)
        #: 满额策略：stop / ring / rotate（见 store.POLICIES）。
        self.full_policy = str(kw.get("full_policy", "") or "") or "ring"
        #: rotate 模式保留的分段数（含当前库）。
        self.keep_files = int(kw.get("keep_files", 3) or 3)

    @property
    def max_mb(self):
        """容量上限（MB，远端协议口径；非整数 MB 的上限向下取整）。"""
        return self.max_kb // 1024

    @max_mb.setter
    def max_mb(self, value):
        self.max_kb = max(0, int(value)) * 1024

    def set_max_kb(self, kb):
        self.max_kb = max(0, int(kb))


class Config(object):
    """全局配置。"""

    def __init__(self):
        self.base_dir = "."
        self.config_path = ""
        self.users_path = ""
        self.port = DEFAULT_PORT
        #: 串口总开关。板上 demo 以 Modbus 从站模式独占 ttyS3 时必须关掉，
        #: 否则两边抢同一个串口（打开失败或互相踢）。
        self.enable_serial = True
        self.enable_vfd = True
        self.enable_temphum = True
        self.enable_analog = True
        self.enable_digital = True
        #: 电磁流量计（LD 系列，Modbus-RTU）。
        #:
        #: [为什么默认关] 现场设备表里还没接流量计（C 端有 XFlowMeter 类
        #: 与真机联调记录，但本 Python 服务端的现场配置里没有这一路）。
        #: 默认关掉的另一个原因是它和变频器/温湿度抢同一条 485：多一个从站
        #: 就多一份轮询时间，没接的从站会一直超时拖慢整条总线。
        #: 接了设备就把 `serial.flowmeter_enabled` 置 true（或用 `--sim`
        #: 起来看虚拟数据）。
        # [默认开 2026-11-05] 现场这三样都在同一路 485 上，代码默认却关着
        # ⇒ 别人拉取后水质页整页 "--"。默认值与现场一致，站点确实没接的
        # 再用 cfg 关掉（多挂一个未接从站会拖慢同一条总线的轮询）。
        self.enable_flowmeter = True
        self.flowmeter_unit_id = 5
        #: 浊度仪 ×2（同一 485，不同站号）。**站号现场不确定**，走配置。
        #:
        #: [为什么默认关] 与流量计同理：现场还没接。且站号 1 可能与总线上
        #: 其它设备冲突，接上后要先确认再开。
        self.enable_turbidity = True
        #: 两台浊度仪的站号（现场手工设定，C 端预留写的是 1 和 2，
        #: 但现场实际可能是 1 和 3 —— 以实测为准，故走配置）。
        self.turbidity_unit_ids = [1, 3]
        self.turbidity_stale_ms = 5000
        #: 浊度仪轮询/推送节拍：浊度是慢变量，500ms 足够。
        self.turbidity_poll_ms = 500
        #: 进水泵（数字量模块上的一路 DO，只有输出、无反馈）。
        self.enable_pump = True
        #: 泵点位：DO6 = 第 6 路电磁阀（DO5）的下一个口。
        #: [2026-11-06] 随阀门点位整体减 16（原 DO22 → DO6）。
        self.pump_items = [{"name": "进水泵", "module": 0, "do": 6}]
        self.pump_stale_ms = 5000
        #: 按钮组（现场操作柱）：4 个 DI 输入 + 3 个 DO 指示灯。
        #:
        #: [2026-11-06 现场定义，用户口径]
        #:   输入 12 = 急停（**常闭**：无信号 = 按下/断开 = 禁止启动）
        #:   输入 13 = 启动按钮
        #:   输入 14 = 停止按钮
        #:   输入 15 = 复位按钮
        #:   输出 7  = 启动按钮灯（运行时常亮）
        #:   输出 8  = 停止按钮灯（停止时常亮，与启动灯互斥）
        #:   输出 9  = 复位按钮灯（**点动**：只在按下复位时亮）
        #:
        #: [为什么点位写进默认值] 与阀门/泵同性质——是**图纸确定的接线**，
        #: 不是站点私有参数。别人 clone 下来跑就该看到同一套点位。
        self.enable_buttons = True
        self.button_items = {
            # 输入（DI 通道号）
            "estop_di": 12,
            "start_di": 13,
            "stop_di": 14,
            "reset_di": 15,
            # 输出（DO 通道号）
            "start_lamp_do": 7,
            "stop_lamp_do": 8,
            "reset_lamp_do": 9,
            # 模块下标（cfg.digital_modules 顺序）
            "module": 0,
        }
        #: 按键去抖/沿判定窗口（毫秒）。DI 扫描 200ms 一拍，给够两拍余量。
        self.button_debounce_ms = 300
        #: 虚拟设备模式：用内存寄存器模型代替真实设备（见 virtualdev.py）。
        #: [语义] 只换**设备物理层**，devices.py 与 app.py 全部用真实实现 ——
        #: 换算、状态字解码、控制闸门、点动看门狗、阀定时器都照跑。
        #: 真实设备完全不碰（不开串口、不建 TCP），设备拆了也能跑。
        #: 打开方式：`python -m hsfsrv --sim` 或配置里 "sim": {"enabled": true}。
        self.sim_enabled = False
        #: 虚拟数字量模块的"抖动"概率（0=不抖）。真机实测无间隔连发会间歇
        #: 空应答；默认关掉，免得日常调试被随机失败干扰。
        self.sim_flaky_rate = 0.0
        #: 虚拟电磁阀**动作行程时间**（毫秒）。
        #:
        #: [为什么必须有这个参数] 现场阀满开满关约 10s、服务端 20s 判超时。
        #: 若虚拟模块"写完 DO 立刻到位"，状态机的"动作中"与"超时报警"两条
        #: 分支永远走不到 —— 功能写了却没法验证。
        #:   10000（缺省）  现场真实节奏：10s 到位，界面能看到"动作中"
        #:   25000          行程过长：20s 时报警（用来验证告警链路）
        #:   -1             阀卡死：永远不到位（验证"无到位反馈"文案）
        self.sim_valve_move_ms = 10000
        #: 虚拟行程开关"没接"：DI 恒 0。
        self.sim_valve_no_feedback = False
        self.vfd_unit_id = 2
        self.temphum_unit_id = 4
        self.vfd_stale_ms = 5000
        self.temphum_stale_ms = 5000
        self.flowmeter_stale_ms = 5000
        self.digital_stale_ms = 5000
        self.digital_poll_ms = 200
        self.serial = SerialConfig()
        #: 第二条独立 485：流量计 + 浊度仪 ×2（9600）。
        #: [串口号怎么定] 现场两条线各占一个 USB-485 转换器，Windows 下
        #: 通常是相邻的两个 COM 口。缺省从路 A 的口名推一个"下一个口"
        #: （COM17 → COM18），也可由配置 `serial_b.port` 显式指定。
        #: 关键是**速率必须不同**：路 A 115200、路 B 9600。
        self.serial_b = SerialConfig(port=_next_serial_port(self.serial.port),
                                     baudrate=9600)
        self.analog = AnalogConfig()
        # [默认值＝**现场口径**，不是"够用的最小集"]
        #
        # 实锅 2026-11-05：`cfg/` 被 .gitignore 排除（运行产物，合理），
        # 但现场参数只存在于那个文件里 ⇒ 别人 clone 下来跑的是"代码默认值"，
        # 而默认值当时是"16 点 + .13 也发布"，于是现象是：
        #     · 数字量页只有 16 点（.13 的 16 点重复出现两套）
        #     · 模拟量压差计没名字（默认 scales 为空）
        #     · 电磁阀 0 路、流量计/浊度不装配
        # 用户原话："别人拉取显示不一样"。
        #
        # 故把现场确定的接线上限写进默认值：没有配置文件也能跑出与现场
        # 一致的界面。真正的**站点私有**差异（串口号、IP）仍由 cfg 覆盖。
        #
        # 2026-11-06 变更：.12（原聚合者，对外 32 点）已下线拆除，现场只
        # 保留 .13 一台 16 点模块（ms=1 主站模式，实测 FC01/FC02 读 16 点、
        # FC05 写单点均可用）。原先由 .12 提供的 16DI/16DO 一并取消 ——
        # 对外点表从 32 点降到 16 点，这是**有意收窄**，不是漏配。
        self.digital_modules = [
            DigitalConfig("数字量", host="192.168.0.13", channels=16),
        ]
        # ---- 电磁阀（6 路，每路 1 DO + 2 DI 反馈）----
        # 点位**先留空**：现场还没定用哪个口。留空时该路显示"未配置"，
        # 控制被拒，界面能先跑起来；接线定下来后只改 items 即可。
        self.enable_valves = True
        self.valve_poll_ms = 200
        #: 从下发命令到"必须到位"的时限；超时判故障并向订阅者推送告警。
        #: [20s 的依据] 现场实测一个阀满开→满关约 10s；20s 是两倍余量，
        #: 既能容忍阀体老化/水压偏低导致的偏慢，又不至于让人等太久
        #: 才看到告警。（原来的 8000ms 对 10s 行程根本不够，会误报。）
        self.valve_move_timeout_ms = 20000
        #: 每项 {name, module, do, openDi, closeDi}；module=None 表示未配置。
        #:
        #: [默认填现场接线 2026-11-05，2026-11-06 点位整体减 16]
        #: 原先六路在「.12 聚合上半区」DO16..21；.12 拆除后只剩 .13 一台
        #: 16 点模块、点表从 0 起，故整体减 16 → DO0..5。物理端子没动，
        #: 变的只是寻址口径（上半区第 n 点本来就是 .13 自己的第 n 点）。
        #:   阀 n：DO n；**关到位 DI 在前、开到位 DI 在后**
        self.valve_items = [
            {"name": "电磁阀%d" % (i + 1), "module": 0,
             "do": i, "openDi": i * 2 + 1, "closeDi": i * 2}
            for i in range(6)
        ]
        # ---- 数据落盘（历史/日志/报警 → SQLite）----
        self.storage = StorageConfig()
        self._runtime = {}          # 运行期持久化（run_mode 等）

    # ---- 派生（只读转发，避免两处各存一份导致改一个忘一个）----
    @property
    def analog_poll_ms(self):
        return self.analog.poll_ms

    @property
    def temphum_poll_ms(self):
        return self.serial.temphum_poll_ms

    @property
    def vfd_poll_ms(self):
        return self.serial.vfd_poll_ms

    # ---- 路径解析 ----
    def resolve_paths(self, base_dir):
        self.base_dir = os.path.abspath(base_dir)
        if not self.config_path:
            self.config_path = os.path.join(self.base_dir, DEFAULT_CONFIG_REL)
        if not self.users_path:
            self.users_path = os.path.join(self.base_dir, DEFAULT_USERS_REL)

    def ensure_cfg_dir(self):
        """确保 cfg/ 目录存在（不存在则建，权限失败由调用方决定是否致命）。"""
        directory = os.path.dirname(self.config_path)
        if directory and not os.path.isdir(directory):
            try:
                os.makedirs(directory)
            except OSError:
                pass

    # ==================================================================
    #                            读写配置
    # ==================================================================
    def load(self):
        """读 JSON 配置；文件不存在视为全缺省（返回 True，不报错）。

        未知键**忽略**（前向兼容：新版本写的配置给旧版本跑不应崩）；
        类型不符的键回退缺省值并记录警告（不静默采信错误类型）。
        """
        if not self.config_path or not os.path.isfile(self.config_path):
            return True
        try:
            with open(self.config_path, "r", encoding="utf-8") as fh:
                doc = json.load(fh)
        except (OSError, ValueError):
            return False
        if not isinstance(doc, dict):
            return False

        server = doc.get("server") or {}
        self.port = _int(server.get("port"), self.port)
        if "serial_enabled" in server:
            self.enable_serial = bool(server.get("serial_enabled"))
        self._runtime = doc.get("runtime") or {}

        serial = doc.get("serial") or {}
        data_ids = serial.get("unit_ids") or {}
        self.vfd_unit_id = _int(data_ids.get("vfd"), self.vfd_unit_id)
        self.temphum_unit_id = _int(data_ids.get("temphum"), self.temphum_unit_id)
        self.flowmeter_unit_id = _int(data_ids.get("flowmeter"),
                                      self.flowmeter_unit_id)
        # 浊度仪站号是**两个**，配置里给数组；缺省沿用旧的占位值。
        turb_ids = data_ids.get("turbidity")
        if isinstance(turb_ids, list) and len(turb_ids) >= 2:
            self.turbidity_unit_ids = [_int(turb_ids[0],
                                            self.turbidity_unit_ids[0]),
                                       _int(turb_ids[1],
                                            self.turbidity_unit_ids[1])]
        self.serial = SerialConfig(
            port=serial.get("port", self.serial.port),
            baudrate=_int(serial.get("baudrate"), self.serial.baudrate),
            timeout_ms=_int(serial.get("timeout_ms"), self.serial.timeout_ms),
            retries=_int(serial.get("retries"), self.serial.retries),
            vfd_poll_ms=_int(serial.get("vfd_poll_ms"), self.serial.vfd_poll_ms),
            temphum_poll_ms=_int(serial.get("temphum_poll_ms"),
                                 self.serial.temphum_poll_ms),
            temphum_push_ms=_int(serial.get("temphum_push_ms"),
                                 self.serial.temphum_push_ms),
        )
        # ---- 第二条 485（流量计 + 浊度仪 ×2，9600）----
        # 老配置文件没有 serial_b 节：沿用 __init__ 推出来的缺省口与 9600。
        # 若路 A 的口名后来改了、而 serial_b 没配，就重新推一次，避免
        # 两条线悄悄落成同一个口（那样 9600 设备会被按 115200 访问）。
        sb = doc.get("serial_b") or {}
        default_b_port = _next_serial_port(self.serial.port)
        b_port = sb.get("port") or default_b_port
        if b_port == self.serial.port:
            b_port = default_b_port          # 撞车兜底
        self.serial_b = SerialConfig(
            port=b_port,
            baudrate=_int(sb.get("baudrate"), 9600),
            timeout_ms=_int(sb.get("timeout_ms"),
                            self.serial.timeout_ms),
            retries=_int(sb.get("retries"), self.serial.retries),
            vfd_poll_ms=self.serial.vfd_poll_ms,
            temphum_poll_ms=self.serial.temphum_poll_ms,
            temphum_push_ms=self.serial.temphum_push_ms,
        )
        if "vfd_enabled" in serial:
            self.enable_vfd = bool(serial.get("vfd_enabled"))
        if "temphum_enabled" in serial:
            self.enable_temphum = bool(serial.get("temphum_enabled"))
        if "flowmeter_enabled" in serial:
            self.enable_flowmeter = bool(serial.get("flowmeter_enabled"))
        if "turbidity_enabled" in serial:
            self.enable_turbidity = bool(serial.get("turbidity_enabled"))
        #: 浊度轮询周期（远程采集配置可改；原先只在 __init__ 里赋值、无
        #: load/save 通路 —— 远程改完重启就丢，配置文件里根本不存在）。
        self.turbidity_poll_ms = _int(serial.get("turbidity_poll_ms"),
                                      self.turbidity_poll_ms)

        analog = doc.get("analog") or {}
        if analog:
            self.analog = AnalogConfig(
                host=analog.get("host", self.analog.host),
                port=_int(analog.get("port"), self.analog.port),
                unit_id=_int(analog.get("unit_id"), self.analog.unit_id),
                channels=_int(analog.get("channels"), self.analog.channels),
                poll_ms=_int(analog.get("poll_ms"), self.analog.poll_ms),
            )
            # 通道工程量换算表（4~20mA → 物理量）。
            # 配置形如 {"index":0,"name":"压差计","unit":"MPa",
            #         "fullScale":1.0,"lowMa":4,"highMa":20}
            for item in (analog.get("scales") or []):
                if not isinstance(item, dict):
                    continue
                try:
                    idx = int(item.get("index", 0))
                except (TypeError, ValueError):
                    continue
                if 0 <= idx < self.analog.channels:
                    # [不能用 AnalogChannelScale(idx, **item)] item 里本身
                    # 就带 "index" 键，位置参数会与关键字参数撞车
                    # （TypeError: got multiple values for argument）。
                    scale = AnalogChannelScale(**item)
                    scale.index = idx          # 以解析出的有效下标为准
                    self.analog.scales[idx] = scale
        if "enabled" in analog:
            self.enable_analog = bool(analog.get("enabled"))

        digital = doc.get("digital") or {}
        if "enabled" in digital:
            self.enable_digital = bool(digital.get("enabled"))
        self.digital_poll_ms = _int(digital.get("poll_ms"), self.digital_poll_ms)
        self.digital_stale_ms = _int(digital.get("stale_ms"),
                                     self.digital_stale_ms)
        modules = digital.get("modules")
        if isinstance(modules, list) and modules:
            built = []
            for i, item in enumerate(modules):
                if not isinstance(item, dict):
                    continue
                name = item.get("name") or ("数字量%d" % (i + 1))
                built.append(DigitalConfig(
                    name,
                    host=item.get("host", "192.168.0.13"),
                    port=_int(item.get("port"), 502),
                    unit_id=_int(item.get("unit_id"), 1),
                    timeout_s=_float(item.get("timeout_s"), 2.0),
                    channels=_int(item.get("channels"), 16),
                    publish=item.get("publish", True)))
            if built:
                self.digital_modules = built

        valves = doc.get("valves") or {}
        if "enabled" in valves:
            self.enable_valves = bool(valves.get("enabled"))
        self.valve_poll_ms = _int(valves.get("poll_ms"), self.valve_poll_ms)
        self.valve_move_timeout_ms = _int(valves.get("move_timeout_ms"),
                                          self.valve_move_timeout_ms)
        items = valves.get("items")
        if isinstance(items, list) and items:
            built = []
            for i, item in enumerate(items[:6]):
                if not isinstance(item, dict):
                    item = {}
                built.append({
                    "name": item.get("name") or ("电磁阀%d" % (i + 1)),
                    "module": _opt_int(item.get("module")),
                    "do": _opt_int(item.get("do")),
                    "openDi": _opt_int(item.get("openDi")),
                    "closeDi": _opt_int(item.get("closeDi")),
                })
            while len(built) < 6:
                built.append({"name": "电磁阀%d" % (len(built) + 1),
                              "module": None, "do": None,
                              "openDi": None, "closeDi": None})
            self.valve_items = built

        # ---- 进水泵（数字量模块上的一路 DO）----
        pumps = doc.get("pumps") or {}
        if pumps:
            if "enabled" in pumps:
                self.enable_pump = bool(pumps.get("enabled"))
            pitems = pumps.get("items")
            if isinstance(pitems, list) and pitems:
                pbuilt = []
                for i, item in enumerate(pitems[:4]):
                    if not isinstance(item, dict):
                        item = {}
                    pbuilt.append({
                        "name": item.get("name") or ("进水泵%d" % (i + 1)),
                        "module": _opt_int(item.get("module")),
                        "do": _opt_int(item.get("do")),
                    })
                self.pump_items = pbuilt

        # ---- 按钮组（现场操作柱：4 DI 输入 + 3 DO 灯）----
        buttons = doc.get("buttons") or {}
        if buttons:
            if "enabled" in buttons:
                self.enable_buttons = bool(buttons.get("enabled"))
            self.button_debounce_ms = _int(buttons.get("debounce_ms"),
                                           self.button_debounce_ms)
            bitems = buttons.get("items")
            if isinstance(bitems, dict):
                merged = dict(self.button_items)
                for key in ("estop_di", "start_di", "stop_di", "reset_di",
                            "start_lamp_do", "stop_lamp_do", "reset_lamp_do",
                            "module"):
                    if key in bitems:
                        merged[key] = _opt_int(bitems.get(key))
                self.button_items = merged

        stale = doc.get("stale") or {}
        self.vfd_stale_ms = _int(stale.get("vfd_ms"), self.vfd_stale_ms)
        self.temphum_stale_ms = _int(stale.get("temphum_ms"),
                                     self.temphum_stale_ms)
        self.flowmeter_stale_ms = _int(stale.get("flowmeter_ms"),
                                       self.flowmeter_stale_ms)
        self.turbidity_stale_ms = _int(stale.get("turbidity_ms"),
                                        self.turbidity_stale_ms)
        self.digital_stale_ms = _int(stale.get("digital_ms"),
                                     self.digital_stale_ms)
        storage = doc.get("storage") or {}
        if "enabled" in storage:
            self.storage.enabled = bool(storage.get("enabled"))
        # 空串合法（= 服务端程序目录），故只在键存在时覆盖。
        if "path" in storage:
            self.storage.path = str(storage.get("path") or "")
        self.storage.sample_ms = _int(storage.get("sample_ms"),
                                      self.storage.sample_ms)
        if "max_mb" in storage:
            self.storage.max_mb = _int(storage.get("max_mb"),
                                       self.storage.max_mb)
        # [max_kb 优先] 配置文件是管理员手工编辑的，故接受 KB 级细粒度
        # （极小盘/回归测试要把上限压到几百 KB 才能几十秒内跑到限）。
        # 远端协议的 maxMb 仍是整数 MB（界面不让人填出 KB 级的怪值）。
        if "max_kb" in storage:
            kb = _int(storage.get("max_kb"), -1)
            if kb >= 0:
                self.storage.set_max_kb(kb)
        policy = str(storage.get("full_policy") or "").strip().lower()
        if policy in ("stop", "ring", "rotate"):
            self.storage.full_policy = policy
        self.storage.keep_files = max(
            1, min(99, _int(storage.get("keep_files"),
                            self.storage.keep_files)))
        return True

    def save(self):
        """落盘（原子写：先临时文件再 rename）。失败不致命（只记日志）。"""
        doc = {
            "server": {
                "port": self.port,
                "serial_enabled": self.enable_serial,
            },
            "serial": {
                "port": self.serial.port,
                "baudrate": self.serial.baudrate,
                "timeout_ms": self.serial.timeout_ms,
                "retries": self.serial.retries,
                "vfd_poll_ms": self.serial.vfd_poll_ms,
                "temphum_poll_ms": self.serial.temphum_poll_ms,
                "temphum_push_ms": self.serial.temphum_push_ms,
                "vfd_enabled": self.enable_vfd,
                "temphum_enabled": self.enable_temphum,
                # 浊度仪/流量计开关与浊度周期：load 侧有通路、save 侧
                # 曾漏写 —— 远程改完重启丢配置（远程采集配置 e2e 实测抓到）。
                "flowmeter_enabled": self.enable_flowmeter,
                "turbidity_enabled": self.enable_turbidity,
                "turbidity_poll_ms": self.turbidity_poll_ms,
                "unit_ids": {
                    "vfd": self.vfd_unit_id,
                    "temphum": self.temphum_unit_id,
                },
            },
            # 第二条 485：与 serial 平级，避免把 9600 的口/速率混进路 A。
            "serial_b": {
                "port": self.serial_b.port,
                "baudrate": self.serial_b.baudrate,
                "timeout_ms": self.serial_b.timeout_ms,
                "retries": self.serial_b.retries,
            },
            "analog": {
                "enabled": self.enable_analog,
                "host": self.analog.host,
                "port": self.analog.port,
                "unit_id": self.analog.unit_id,
                "channels": self.analog.channels,
                "poll_ms": self.analog.poll_ms,
                "scales": [self.analog.scales[i].to_doc()
                           for i in sorted(self.analog.scales)],
            },
            "digital": {
                "enabled": self.enable_digital,
                "poll_ms": self.digital_poll_ms,
                "stale_ms": self.digital_stale_ms,
                "modules": [
                    {
                        "name": m.name,
                        "host": m.host,
                        "port": m.port,
                        "unit_id": m.unit_id,
                        "timeout_s": m.timeout_s,
                        "channels": m.channels,
                        "publish": m.publish,
                    }
                    for m in self.digital_modules
                ],
            },
            "valves": {
                "enabled": self.enable_valves,
                "poll_ms": self.valve_poll_ms,
                "move_timeout_ms": self.valve_move_timeout_ms,
                "items": self.valve_items,
            },
            "buttons": {
                "enabled": self.enable_buttons,
                "debounce_ms": self.button_debounce_ms,
                "items": self.button_items,
            },
            "storage": {
                "enabled": self.storage.enabled,
                "path": self.storage.path,
                "sample_ms": self.storage.sample_ms,
                "max_kb": self.storage.max_kb,
                "full_policy": self.storage.full_policy,
                "keep_files": self.storage.keep_files,
            },
            "stale": {
                "vfd_ms": self.vfd_stale_ms,
                "temphum_ms": self.temphum_stale_ms,
                "digital_ms": self.digital_stale_ms,
            },
            "runtime": self._runtime,
        }
        self.ensure_cfg_dir()
        tmp = self.config_path + ".tmp"
        try:
            with open(tmp, "w", encoding="utf-8") as fh:
                json.dump(doc, fh, ensure_ascii=False, indent=2)
                fh.flush()
                os.fsync(fh.fileno())
            os.replace(tmp, self.config_path)
        except OSError:
            try:
                os.unlink(tmp)
            except OSError:
                pass
            return False
        return True

    # ---- 运行期持久化（运行模式） ----
    def run_mode(self, default=0):
        return _int(self._runtime.get("run_mode"), default)

    def save_run_mode(self, mode):
        self._runtime["run_mode"] = int(mode)
        self.save()


def _int(value, default):
    """宽松取整：非法值回退 default（配置文件手改出错不应让服务起不来）。"""
    if isinstance(value, bool):
        return default
    if isinstance(value, int):
        return value
    try:
        return int(value)
    except (TypeError, ValueError):
        return default


def _opt_int(value):
    """可选整数：None/空/非法 → None（表示"未配置"，与 0 区分）。"""
    if value is None or value == "":
        return None
    if isinstance(value, bool):
        return None
    try:
        return int(value)
    except (TypeError, ValueError):
        return None


def _float(value, default):
    if isinstance(value, bool):
        return default
    if isinstance(value, (int, float)):
        return float(value)
    try:
        return float(value)
    except (TypeError, ValueError):
        return default
