# -*- coding: utf-8 -*-
"""持久化与记录：告警边沿 / 运行日志。

[为什么没有历史自采]（2026-11-13 起，HistoryStore 已删）
历史真源是**服务端 SQLite**（19 列 HISTORY_COLUMNS，分页命令 0x0118）；
web 自采会与服务端口径漂移（列集/单位/格式各有一套，且服务端可能按
`historyTrigger=auto` 只在自动运行时入库——自采的"全量"是假的）。
历史/日志页全部走 `/api/history/page`、`/api/log/page` 转发（api.py），
导出走 `/api/history/export`、`/api/log/export`（export.py 按筛选全量批取）。

[两个子系统的职责]
    AlarmLog      健康与阀/自动告警的**边沿**记录（进入异常记一条、恢复再记
                  一条），不是每拍都记 —— 否则一晚上能写出几十万行重复记录
    RunLog        网页服务器自身日志（连接/命令/错误），环形缓冲 + 落盘

[落盘的崩溃安全性]
  全部用「追加写 + 行缓冲」，不做「读全量→重写」。追加写在进程被杀时
  最多丢最后一行的半截；重写模式会丢掉整个文件。
"""

import csv
import io
import os
import threading
import time
from collections import deque

from . import model as model_mod
from . import proto

#: 报警页「类型」列用的**设备型别**字面（与参考端同一套 8 类，
#: client model.KIND_TEXT —— 单一事实源，不另写字面量）。
#: 自动运行告警的类型是「自动运行」（参考 pages_other.KIND_TEXT_AUTO）。
KIND_VALVE_TEXT = model_mod.KIND_TEXT[6]     # 电动球阀组
KIND_AUTO_TEXT = "自动运行"


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

    def _append(self, kind, source, name, level, text, at=None,
                device_kind=""):
        # [时刻必须是墙钟] 上游告警帧的 atMs 有两端口径：C 端发墙钟毫秒，
        # Python 端发 **monotonic 毫秒**（webui 2026-11-13 实测：阀超时告警
        # 的 at ≈ 开机以来毫秒 ⇒ 报警页时间列显示 1970-01-08）。与
        # export.VALID_TS_MIN 同值（2020-01-01）做下界：不像墙钟的一律
        # 退回「收到时刻」——参考客户端报警页就是按本机收到时刻记账。
        now = wall_ms() if at is None else int(at)
        if now < 1577836800000:
            now = wall_ms()
        row = {
            "ts": now,
            "time": stamp_text(now),
            "kind": kind,        # health / valve / auto
            "source": source,    # 设备名 / 阀名
            "name": name,        # 状态列文案（健康档：正常/告警/数据过期/离线/故障）
            # 设备型别（8 类字面，对齐 client model.KIND_TEXT）——报警页
            # 「类型」列按它显示与筛选（参考 pages_other.py:4391 KIND_ITEMS：
            # 全部类型 + 8 类设备型别 + 自动运行，共 10 档）。
            "deviceKind": device_kind,
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
        cols = ("ts", "time", "kind", "source", "name", "deviceKind",
                "level", "text")
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
            # [异常阈值与参考端同档] health 有 5 档（正常/告警/过期/离线/
            # 故障），参考客户端按 `dev.health >= 2` 记一条（pages_other.py:
            # 4511）⇒ 告警/数据过期/离线/故障都进报警表。web 此前只对
            # 3(离线)/4(故障) 记 ⇒ 同一现场两页报警条数对不上。
            # [一个设备只记一段] 边沿键是 (来源, 设备名)（不含健康档），
            # ⇒ 「过期 → 离线」是同一段异常，**不会**记成两行。
            abnormal = dev.health >= proto.HEALTH_STALE
            key = ("health", dev.name)
            seen.add(key)
            if abnormal and key not in self._active:
                self._active[key] = wall_ms()
                added.append(self._append(
                    "health", dev.name, dev.health_text(),
                    "fault" if dev.health == proto.HEALTH_FAULT else "warn",
                    dev.text or dev.health_text(),
                    device_kind=dev.kind_text()))
            elif not abnormal and key in self._active:
                del self._active[key]
                added.append(self._append(
                    "health", dev.name, "恢复正常", "clear",
                    "设备已恢复到%s" % dev.health_text(),
                    device_kind=dev.kind_text()))
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
        # 阀告警的「类型」列用设备型别「电动球阀组」（参考 model.KIND_TEXT
        # 第 6 类），状态列放告警类型（动作超时/到位信号冲突/已恢复）。
        return self._append("valve", name, label, level, text,
                            at=alarm.get("at"), device_kind=KIND_VALVE_TEXT)

    # ---- 自动运行告警（TOPIC_AUTO_ALARM）----
    def feed_auto_alarm(self, label, text, at=0):
        """把服务端推来的一条自动运行告警存档（阀超时/空管等）。

        [为什么存档] 自动告警是**事件**（不重复），AlarmLog 的边沿口径
        天然匹配：来一条存一条，AlarmPage 的类型下拉里就能按 kind 过滤。
        """
        return self._append("auto", text or label, label, "warn",
                            text or label, at=at or None,
                            device_kind=KIND_AUTO_TEXT)

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
