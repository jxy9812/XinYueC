# -*- coding: utf-8 -*-
"""持久化与记录：历史数据 / 告警边沿 / 运行日志。

[为什么网页版需要这一层，而 Qt 客户端不需要]
=============================================
Qt 客户端的历史页是「本进程内缓存最近 300 条，重启清空」（见
`pages_other.py: HistoryPage`）。那在单人单机桌面场景够用，但网页版是
**多人共用一台板子**：

  · 张三上午看到的曲线，李四下午要复盘 —— 进程重启就没了，做不到
  · 手机端页面一刷新（F5）就重新连一次 SSE，若历史在内存里且按连接
    存储，刷新即清空 —— 用户会以为"数据丢了"

所以网页版把历史落到 CSV（纯文本、可用 Excel 打开、不需要数据库），
并提供轻量查询接口。板端没有 pip 环境，**不能用 sqlite3 之外的任何库**；
这里连 sqlite 都不用，直接 CSV + 内存索引，零依赖且文件可人工查看。

[三个子系统的职责]
    HistoryStore  把变频器/温湿度/模拟量按固定节拍采样落盘，支持按时间范围查
    AlarmLog      健康与阀告警的**边沿**记录（进入异常记一条、恢复再记一条），
                  不是每拍都记 —— 否则一晚上能写出几十万行重复记录
    RunLog        网页服务器自身日志（连接/命令/错误），环形缓冲 + 落盘

[落盘的崩溃安全性]
  全部用「追加写 + 行缓冲」，不做「读全量→重写」。追加写在进程被杀时
  最多丢最后一行的半截；重写模式会丢掉整个文件。历史文件按时段切分
  （每小时一个），单文件不会无限长大。
"""

import csv
import io
import os
import threading
import time
from collections import deque

from . import proto


def wall_ms():
    """墙上时间毫秒（记录时间戳用，**不是**单调时钟）。"""
    return int(time.time() * 1000)


def stamp_text(ms=None):
    """毫秒 → "YYYY-MM-DD HH:MM:SS" 本地时间。"""
    if ms is None:
        ms = wall_ms()
    return time.strftime("%Y-%m-%d %H:%M:%S", time.localtime(ms / 1000.0))


def day_stamp(ms=None):
    if ms is None:
        ms = wall_ms()
    return time.strftime("%Y-%m-%d", time.localtime(ms / 1000.0))


def hour_stamp(ms=None):
    if ms is None:
        ms = wall_ms()
    return time.strftime("%Y%m%d-%H", time.localtime(ms / 1000.0))


# ============================================================================
#                              运行日志
# ============================================================================

class RunLog(object):
    """网页服务器运行日志：环形缓冲（给界面看）+ 追加落盘（给复盘看）。

    [为什么要环形缓冲] 日志页要能翻最近几百条；但进程跑一个月会积累
    几十万条，全留在内存里会吃掉板子上宝贵的内存（板子总共 1~2GB，
    Qt 界面 + 服务端已经占掉大头）。环形缓冲固定上限，老的自动丢。

    [为什么落盘与缓冲分开] 落盘是给"事后复盘"用的（可以 grep、可以留证），
    缓冲是给"现在界面显示"用的。两者生命周期不同：落盘按天切文件，
    缓冲只留最近 N 条。
    """

    #: 内存里保留的条数上限。
    MAX_LINES = 800
    #: 落盘文件按天切分。
    FILE_FMT = "webui-%s.log"

    def __init__(self, log_dir=None, echo=True):
        self.lines = deque(maxlen=self.MAX_LINES)
        self.log_dir = log_dir
        self.echo = echo
        self._lock = threading.Lock()
        self._day = None
        self._path = None
        if self.log_dir:
            try:
                os.makedirs(self.log_dir, exist_ok=True)
            except OSError as exc:
                # 日志目录建不出来不能阻止服务启动（只降级为"不落盘"）。
                self.log_dir = None
                self._emit_console("[webui] 日志目录不可用（%s），仅内存记录"
                                   % exc)

    def _emit_console(self, text):
        if self.echo:
            try:
                print(text, flush=True)
            except Exception:                               # noqa: BLE001
                pass

    def _ensure_file(self, now):
        """按天切文件（跨天时换新文件）。"""
        day = day_stamp(now)
        if day == self._day and self._path:
            return self._path
        self._day = day
        self._path = os.path.join(self.log_dir, self.FILE_FMT % day)
        return self._path

    def write(self, text, level="INFO"):
        """记一条日志。

        :param level: DEBUG / INFO / WARN / ERROR（界面按级别上色）
        """
        now = wall_ms()
        stamp = stamp_text(now)
        line = "%s [%s] %s" % (stamp, level, text)
        with self._lock:
            self.lines.append({"at": now, "stamp": stamp,
                               "level": level, "text": text})
            if self.log_dir:
                try:
                    path = self._ensure_file(now)
                    with io.open(path, "a", encoding="utf-8") as fh:
                        fh.write(line + "\n")
                except OSError:
                    # 落盘失败不该拖垮业务（磁盘满/权限问题都很常见）。
                    self.log_dir_failed = True
        self._emit_console(line)
        return line

    def debug(self, text):
        return self.write(text, "DEBUG")

    def info(self, text):
        return self.write(text, "INFO")

    def warn(self, text):
        return self.write(text, "WARN")

    def error(self, text):
        return self.write(text, "ERROR")

    def tail(self, limit=200, level=None, contains=None):
        """取最近 N 条（可按级别/关键字过滤）。"""
        with self._lock:
            items = list(self.lines)
        if level:
            items = [i for i in items if i["level"] == level]
        if contains:
            needle = contains.lower()
            items = [i for i in items if needle in i["text"].lower()]
        if limit and len(items) > limit:
            items = items[-limit:]
        return items

    def clear(self):
        with self._lock:
            self.lines.clear()


# ============================================================================
#                              历史数据
# ============================================================================

class HistoryStore(object):
    """按节拍采样落盘 + 内存缓存最近 N 条。

    [采样口径与 Qt 版对齐] Qt 客户端历史页 1 秒采一次（推送是 100ms 一拍，
    但历史表不需要每 100ms 一行）。网页版默认 1 秒，可用 `--history-ms` 调。

    [落盘格式] 每小时一个 CSV，表头固定。列：
        ts           毫秒时间戳（便于程序排序）
        time         可读时间
        各测点列       vfd_* / th_* / ai_<通道>_ma / ai_<通道>_eng

    [为什么不落 BSON/JSON] CSV 是唯一"运维能直接用 Excel 打开、能用
    awk/grep 处理、出问题不用装工具"的格式。板子上没有 pandas。
    """

    #: 内存里保留的最近条数（界面曲线用）。
    MAX_ROWS = 3600

    #: 固定列（不随通道数变）。
    BASE_COLUMNS = ("ts", "time",
                    "vfd_run_state", "vfd_output_hz", "vfd_set_hz",
                    "vfd_bus_v", "vfd_current_a", "vfd_rpm",
                    "th_temp_c", "th_hum_pct")

    def __init__(self, data_dir=None, sample_ms=1000, max_channels=8):
        self.rows = deque(maxlen=self.MAX_ROWS)
        self.data_dir = data_dir
        self.sample_ms = max(200, int(sample_ms))
        self.max_channels = max(0, int(max_channels))
        self._lock = threading.Lock()
        self._last_sample_ms = 0
        self._hour = None
        self._path = None
        self._wrote_header = False
        self.total_samples = 0
        self.last_error = ""
        if self.data_dir:
            try:
                os.makedirs(self.data_dir, exist_ok=True)
            except OSError as exc:
                self.data_dir = None
                self.last_error = "数据目录不可用：%s" % exc

    # ---- 列定义 ----
    def columns(self):
        cols = list(self.BASE_COLUMNS)
        for i in range(self.max_channels):
            cols.append("ai%d_ma" % i)
        for i in range(self.max_channels):
            cols.append("ai%d_eng" % i)
        return cols

    # ---- 采样 ----
    def maybe_sample(self, model, now_ms=None):
        """按节拍采样（不到时间直接返回 None，不做任何工作）。

        :returns: 写入的行（dict）或 None
        """
        now = wall_ms() if now_ms is None else int(now_ms)
        if self._last_sample_ms and now - self._last_sample_ms < self.sample_ms:
            return None
        # [只采"有数据"的拍] 全无新鲜数据时落一行全空的行没有意义，
        # 反而让曲线出现一段假的零值。这里要求至少一类数据新鲜。
        vfd_fresh = model.vfd.is_fresh()
        th_fresh = model.temphum.is_fresh()
        ai_fresh = model.analog.is_fresh()
        if not (vfd_fresh or th_fresh or ai_fresh):
            return None
        self._last_sample_ms = now

        vfd = model.vfd
        th = model.temphum
        ai = model.analog
        row = {
            "ts": now,
            "time": stamp_text(now),
            "vfd_run_state": vfd.run_state if vfd_fresh else "",
            "vfd_output_hz": (round(vfd.output_freq_hz, 2)
                              if vfd_fresh and vfd.has("outputFreqHz") else ""),
            "vfd_set_hz": (round(vfd.set_freq_hz, 2)
                           if vfd_fresh and vfd.has("setFreqHz") else ""),
            "vfd_bus_v": (round(vfd.bus_voltage_v, 1)
                          if vfd_fresh and vfd.has("busVoltageV") else ""),
            "vfd_current_a": (round(vfd.output_current_a, 2)
                              if vfd_fresh and vfd.has("outputCurrentA") else ""),
            "vfd_rpm": (round(vfd.rpm_estimate())
                        if vfd_fresh and vfd.rpm_estimate() is not None else ""),
            "th_temp_c": (round(th.temp_tenths / 10.0, 1)
                          if th_fresh and th.temp_valid
                          and th.temp_tenths is not None else ""),
            "th_hum_pct": (round(th.hum_tenths / 10.0, 1)
                           if th_fresh and th.hum_valid
                           and th.hum_tenths is not None else ""),
        }
        for i in range(self.max_channels):
            valid = ai_fresh and ai.channel_valid(i)
            row["ai%d_ma" % i] = round(ai.ma(i), 3) if valid else ""
            if valid and ai.has_engineering(i):
                row["ai%d_eng" % i] = round(ai.eng.get(i, 0) / 1000.0, 4)
            else:
                row["ai%d_eng" % i] = ""

        with self._lock:
            self.rows.append(row)
            self.total_samples += 1
            self._append_file(now, row)
        return row

    def _append_file(self, now, row):
        if not self.data_dir:
            return
        hour = hour_stamp(now)
        if hour != self._hour or self._path is None:
            self._hour = hour
            self._path = os.path.join(self.data_dir, "history-%s.csv" % hour)
            self._wrote_header = os.path.isfile(self._path) \
                and os.path.getsize(self._path) > 0
        cols = self.columns()
        try:
            with io.open(self._path, "a", encoding="utf-8",
                         newline="") as fh:
                writer = csv.DictWriter(fh, fieldnames=cols,
                                        extrasaction="ignore")
                if not self._wrote_header:
                    writer.writeheader()
                    self._wrote_header = True
                writer.writerow(row)
        except OSError as exc:
            self.last_error = "历史落盘失败：%s" % exc

    # ---- 查询 ----
    def query(self, limit=300, since_ms=None):
        """取最近 N 条（可选只取 since 之后的）。"""
        with self._lock:
            rows = list(self.rows)
        if since_ms is not None:
            rows = [r for r in rows if r["ts"] >= int(since_ms)]
        if limit and len(rows) > limit:
            rows = rows[-limit:]
        return rows

    def stats(self):
        with self._lock:
            rows = list(self.rows)
        return {
            "buffered": len(rows),
            "capacity": self.MAX_ROWS,
            "totalSamples": self.total_samples,
            "sampleMs": self.sample_ms,
            "dataDir": self.data_dir or "",
            "lastError": self.last_error,
            "firstMs": rows[0]["ts"] if rows else 0,
            "lastMs": rows[-1]["ts"] if rows else 0,
        }

    def files(self):
        """已落盘的历史文件（按名倒序 = 最新在前）。"""
        if not self.data_dir:
            return []
        try:
            names = [n for n in os.listdir(self.data_dir)
                     if n.startswith("history-") and n.endswith(".csv")]
        except OSError:
            return []
        names.sort(reverse=True)
        out = []
        for name in names[:48]:
            path = os.path.join(self.data_dir, name)
            try:
                size = os.path.getsize(path)
            except OSError:
                size = 0
            out.append({"name": name, "bytes": size})
        return out


# ============================================================================
#                              告警记录
# ============================================================================

class AlarmLog(object):
    """告警记录：**边沿**记录，不重复刷同一条。

    [为什么是边沿而不是电平] 健康巡检每秒一拍，设备离线 8 小时就是
    28800 条一模一样的记录。现场要看的是"什么时候坏的、什么时候好的"，
    不是"每一秒都还坏着"。故按 (设备, 异常类型) 记边沿：
      进入异常 → 记一条 active
      恢复     → 记一条 clear
      持续异常 → 不再记（但可以在快照里看到它仍处于 active）

    [与阀告警的关系] 阀告警由服务端算好推来（超时/冲突/恢复），
    这里只负责**存档与展示**，不做二次判断 —— 两边各算一套必然对不上。
    """

    #: 内存保留条数。
    MAX_ROWS = 500

    def __init__(self, data_dir=None):
        self.rows = deque(maxlen=self.MAX_ROWS)
        #: 当前处于活动状态的键 → 首次进入时间
        self._active = {}
        self._lock = threading.Lock()
        self.total = 0
        self.data_dir = data_dir
        self._path = None
        if self.data_dir:
            try:
                os.makedirs(self.data_dir, exist_ok=True)
            except OSError:
                self.data_dir = None

    def _append(self, kind, source, name, level, text, at=None):
        now = wall_ms() if at is None else int(at)
        row = {
            "ts": now,
            "time": stamp_text(now),
            "kind": kind,        # health / valve
            "source": source,    # 设备名 / 阀名
            "name": name,        # 异常类型（故障/离线/动作超时…）
            "level": level,      # warn / fault / clear / info
            "text": text,
        }
        with self._lock:
            self.rows.append(row)
            self.total += 1
            self._append_file(row)
        return row

    def _append_file(self, row):
        if not self.data_dir:
            return
        path = os.path.join(self.data_dir, "alarms-%s.csv" % day_stamp(row["ts"]))
        cols = ("ts", "time", "kind", "source", "name", "level", "text")
        try:
            exists = os.path.isfile(path) and os.path.getsize(path) > 0
            with io.open(path, "a", encoding="utf-8", newline="") as fh:
                writer = csv.DictWriter(fh, fieldnames=cols,
                                        extrasaction="ignore")
                if not exists:
                    writer.writeheader()
                writer.writerow(row)
        except OSError:
            pass

    # ---- 健康边沿 ----
    def feed_health(self, snapshot):
        """按健康快照做边沿检测，返回本次新增的记录列表。"""
        added = []
        seen = set()
        for dev in snapshot.devices:
            # [只看"离线/故障"两档] health 有 5 档（正常/告警/过期/离线/故障），
            # 其中 1(告警)/2(过期) 会频繁抖动（一次读超时就跳一下），
            # 全记会把日志刷满。这里只对 3(离线)/4(故障) 做边沿。
            abnormal = dev.health in (proto.HEALTH_OFFLINE,
                                      proto.HEALTH_FAULT)
            key = ("health", dev.name)
            seen.add(key)
            if abnormal and key not in self._active:
                self._active[key] = wall_ms()
                added.append(self._append(
                    "health", dev.name, dev.health_text(),
                    "fault" if dev.health == proto.HEALTH_FAULT else "warn",
                    dev.text or dev.health_text()))
            elif not abnormal and key in self._active:
                del self._active[key]
                added.append(self._append(
                    "health", dev.name, "恢复正常", "clear",
                    "设备已恢复到%s" % dev.health_text()))
        # 消失的设备（服务端不再上报）：视为恢复，清掉活动标记。
        for key in list(self._active.keys()):
            if key[0] == "health" and key not in seen:
                del self._active[key]
        return added

    # ---- 阀告警 ----
    def feed_valve_alarm(self, alarm):
        """把服务端推来的一条阀告警存档。"""
        kind = alarm.get("k", 0)
        name = alarm.get("vn") or ("阀%d" % (int(alarm.get("vi", 0)) + 1))
        text = alarm.get("t", "")
        level = {3: "clear", 2: "fault"}.get(int(kind), "warn")
        label = {1: "动作超时", 2: "到位信号冲突", 3: "已恢复"}.get(int(kind), "告警")
        return self._append("valve", name, label, level, text,
                            at=alarm.get("at"))

    # ---- 查询 ----
    def query(self, limit=200, level=None, kind=None):
        with self._lock:
            rows = list(self.rows)
        if level:
            rows = [r for r in rows if r["level"] == level]
        if kind:
            rows = [r for r in rows if r["kind"] == kind]
        if limit and len(rows) > limit:
            rows = rows[-limit:]
        return list(reversed(rows))          # 最新在前（界面口径）

    def active(self):
        """当前仍处于异常中的条目。"""
        with self._lock:
            items = list(self._active.items())
        return [{"kind": k[0], "source": k[1], "sinceMs": ms}
                for k, ms in items]

    def clear(self):
        with self._lock:
            self.rows.clear()
            self._active.clear()
