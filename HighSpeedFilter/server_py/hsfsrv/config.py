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


class SerialConfig(object):
    """一路 485 总线（本版只有一路：变频器 + 温湿度共用）。"""

    __slots__ = ("port", "baudrate", "timeout_ms", "retries",
                 "vfd_poll_ms", "temphum_poll_ms", "temphum_push_ms")

    def __init__(self, **kw):
        self.port = kw.get("port", default_serial_port())
        self.baudrate = int(kw.get("baudrate", 115200))
        self.timeout_ms = int(kw.get("timeout_ms", 400))
        self.retries = int(kw.get("retries", 1))
        #: 变频器是快设备，每拍都给机会；温湿度物理更新 30Hz，500ms 绰绰有余。
        self.vfd_poll_ms = int(kw.get("vfd_poll_ms", 100))
        self.temphum_poll_ms = int(kw.get("temphum_poll_ms", 500))
        self.temphum_push_ms = int(kw.get("temphum_push_ms", 500))


class AnalogConfig(object):
    """模拟量输入模块（Modbus TCP）。"""

    __slots__ = ("host", "port", "unit_id", "channels", "poll_ms")

    def __init__(self, **kw):
        self.host = kw.get("host", "192.168.0.11")
        self.port = int(kw.get("port", 8234))
        self.unit_id = int(kw.get("unit_id", 1))
        self.channels = int(kw.get("channels", 8))
        self.poll_ms = int(kw.get("poll_ms", 200))


class DigitalConfig(object):
    """一台 16DI/16DO 数字量模块（Modbus TCP）。"""

    __slots__ = ("name", "host", "port", "unit_id", "timeout_s")

    def __init__(self, name, **kw):
        self.name = name
        self.host = kw.get("host", "192.168.0.12")
        self.port = int(kw.get("port", 502))
        self.unit_id = int(kw.get("unit_id", 1))
        #: 该模块应答实测 0.4~0.8s（比其他设备慢一个数量级），超时给足。
        self.timeout_s = float(kw.get("timeout_s", 2.0))


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
        self.vfd_unit_id = 2
        self.temphum_unit_id = 4
        self.vfd_stale_ms = 5000
        self.temphum_stale_ms = 5000
        self.digital_stale_ms = 5000
        self.digital_poll_ms = 200
        self.serial = SerialConfig()
        self.analog = AnalogConfig()
        self.digital_modules = [
            DigitalConfig("数字量1", host="192.168.0.12"),
            DigitalConfig("数字量2", host="192.168.0.13"),
        ]
        # ---- 电磁阀（6 路，每路 1 DO + 2 DI 反馈）----
        # 点位**先留空**：现场还没定用哪个口。留空时该路显示"未配置"，
        # 控制被拒，界面能先跑起来；接线定下来后只改 items 即可。
        self.enable_valves = True
        self.valve_poll_ms = 200
        #: 从下发命令到"必须到位"的时限；超时判故障（阀卡住/线圈断）。
        self.valve_move_timeout_ms = 8000
        #: 每项 {name, module, do, openDi, closeDi}；module=None 表示未配置。
        self.valve_items = [
            {"name": "电磁阀%d" % (i + 1), "module": None,
             "do": None, "openDi": None, "closeDi": None}
            for i in range(6)
        ]
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
        if "vfd_enabled" in serial:
            self.enable_vfd = bool(serial.get("vfd_enabled"))
        if "temphum_enabled" in serial:
            self.enable_temphum = bool(serial.get("temphum_enabled"))

        analog = doc.get("analog") or {}
        if analog:
            self.analog = AnalogConfig(
                host=analog.get("host", self.analog.host),
                port=_int(analog.get("port"), self.analog.port),
                unit_id=_int(analog.get("unit_id"), self.analog.unit_id),
                channels=_int(analog.get("channels"), self.analog.channels),
                poll_ms=_int(analog.get("poll_ms"), self.analog.poll_ms),
            )
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
                    host=item.get("host", "192.168.0.12"),
                    port=_int(item.get("port"), 502),
                    unit_id=_int(item.get("unit_id"), 1),
                    timeout_s=_float(item.get("timeout_s"), 2.0)))
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

        stale = doc.get("stale") or {}
        self.vfd_stale_ms = _int(stale.get("vfd_ms"), self.vfd_stale_ms)
        self.temphum_stale_ms = _int(stale.get("temphum_ms"),
                                     self.temphum_stale_ms)
        self.digital_stale_ms = _int(stale.get("digital_ms"),
                                     self.digital_stale_ms)
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
                "unit_ids": {
                    "vfd": self.vfd_unit_id,
                    "temphum": self.temphum_unit_id,
                },
            },
            "analog": {
                "enabled": self.enable_analog,
                "host": self.analog.host,
                "port": self.analog.port,
                "unit_id": self.analog.unit_id,
                "channels": self.analog.channels,
                "poll_ms": self.analog.poll_ms,
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
