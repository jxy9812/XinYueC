# -*- coding: utf-8 -*-
"""SQLite 落盘层：历史数据 / 运行日志 / 告警事件 三张表。

[为什么是 sqlite3 标准库] 零依赖（板上部署不添包）；数据量级
（历史 5s 一行 ≈ 1.7 万行/天，日志/告警事件级）远够用。真到了要
长期趋势分析的那天，文件本身就是标准 SQLite，任何工具都能读。

[写入模型] 全部**同步写**（asyncio 线程里直接执行）。原因：
  · 单条写入是毫秒级小事务，5s 一条历史的量级阻塞可忽略；
  · 引入写线程/队列会让「路径在线变更」「关库」多出竞态面，
    对这个量级不值得。
WAL 模式让「写入不锁读」——将来加查询命令时写读互不拖累。

[三张表共用一个文件] 历史是周期数据、日志是文本流、告警是事件，
分表不混行；但同文件才能被「数据存哪了」一句话说清（现场运维口径）。

[失败口径] 打开/写入失败**不致命**：服务照跑（采集与推送不停），
只是落盘停了。磁盘满/路径不可写这类现场问题不该拖垮监控本身。
"""

import os
import sqlite3
import time

#: 缺省库文件名（落在服务端程序目录）。
DEFAULT_DB_NAME = "hsf_data.db"

_SCHEMA = """
CREATE TABLE IF NOT EXISTS history (
    ts        INTEGER PRIMARY KEY,   -- epoch 毫秒（采样时刻）
    mode      TEXT,                  -- 手动 / 自动·运行中 / 自动·停止 / 未知
    vfd_state TEXT,                  -- 搅拌机状态（停机/正转运行/故障 …）
    freq_hz   REAL,                  -- 输出频率
    current_a REAL,                  -- 输出电流
    rpm       REAL,                  -- 搅拌转速（频率换算值）
    turb1     REAL,                  -- 浊度仪1 NTU
    turb2     REAL,                  -- 浊度仪2 NTU
    flow      REAL,                  -- 瞬时流量
    flow_unit TEXT,                  -- 瞬时流量单位（m³/h …，随流量计）
    flow_total REAL,                 -- 正向累积
    total_unit TEXT,                 -- 累积单位（m³ …）
    press_mpa REAL,                  -- 压差（工程量）
    press_note TEXT,                 -- 压差越限说明（空=正常）
    temp      REAL,                  -- 电箱温度 ℃
    hum       REAL,                  -- 电箱湿度 %RH
    di        INTEGER,               -- DI 位图（32 点 ≤ 4 字节）
    do        INTEGER                -- DO 位图
);
CREATE INDEX IF NOT EXISTS idx_history_ts ON history(ts);

CREATE TABLE IF NOT EXISTS logs (
    ts   INTEGER,                     -- epoch 毫秒
    text TEXT
);
CREATE INDEX IF NOT EXISTS idx_logs_ts ON logs(ts);

CREATE TABLE IF NOT EXISTS alarms (
    ts     INTEGER,                  -- epoch 毫秒
    kind   TEXT,                     -- valve / auto / …（来源分类）
    source TEXT,                     -- 具体对象（阀名 等）
    text   TEXT                      -- 告警内容
);
CREATE INDEX IF NOT EXISTS idx_alarms_ts ON alarms(ts);
"""


class Store(object):
    """SQLite 落盘器：懒打开、路径在线变更、失败不致命。"""

    def __init__(self, base_dir, log=None):
        self._base_dir = base_dir
        self._log = log or (lambda msg: None)
        self._conn = None
        #: 当前库文件路径（None = 尚未打开）。远端改路径后据此判断要重开。
        self._open_path = None
        self._open_cfg_path = None      # 打开时的目标路径（含缺省展开）

    # ---- 路径 ----
    def resolve_path(self, configured):
        """配置路径 → 实际文件路径（空 = 服务端程序目录）。"""
        configured = (configured or "").strip()
        if not configured:
            return os.path.join(self._base_dir, DEFAULT_DB_NAME)
        # 相对路径按服务端程序目录展开（现场只写个文件名即可）。
        if not os.path.isabs(configured):
            return os.path.join(self._base_dir, configured)
        return configured

    # ---- 开/关 ----
    def open_for(self, configured_path):
        """按需打开（含路径变更后的重开）。失败返回 False，不抛。"""
        target = self.resolve_path(configured_path)
        if self._conn is not None and self._open_cfg_path == configured_path:
            return True
        self.close()
        try:
            directory = os.path.dirname(target)
            if directory and not os.path.isdir(directory):
                os.makedirs(directory)
            conn = sqlite3.connect(target)
            conn.execute("PRAGMA journal_mode=WAL")
            conn.execute("PRAGMA synchronous=NORMAL")
            conn.executescript(_SCHEMA)
            conn.commit()
            self._conn = conn
            self._open_path = target
            self._open_cfg_path = configured_path
            self._log("数据落盘已启用：%s" % target)
            return True
        except (sqlite3.Error, OSError) as exc:
            self._conn = None
            self._open_path = None
            self._open_cfg_path = None
            self._log("数据落盘打开失败（%s）：不影响采集与推送" % exc)
            return False

    def close(self):
        if self._conn is not None:
            try:
                self._conn.close()
            except sqlite3.Error:
                pass
            self._conn = None
            self._open_path = None
            self._open_cfg_path = None

    @property
    def db_path(self):
        """当前库文件路径（未打开 = None；给界面/日志显示用）。"""
        return self._open_path

    # ---- 写入（全部静默失败：落盘停了不该拖垮监控）----
    def _write(self, sql, params):
        if self._conn is None:
            return False
        try:
            self._conn.execute(sql, params)
            self._conn.commit()
            return True
        except sqlite3.Error as exc:
            self._log("数据落盘写入失败：%s" % exc)
            return False

    def insert_log(self, text, now_ms=None):
        """运行日志入库（app._log 的旁路；入此处不再回调 _log，防递归）。"""
        ts = int(now_ms if now_ms is not None else time.time() * 1000)
        return self._write("INSERT INTO logs(ts, text) VALUES(?, ?)",
                           (ts, str(text)))

    def insert_alarm(self, kind, source, text, now_ms=None):
        """告警事件入库（阀超时/自动运行告警…）。"""
        ts = int(now_ms if now_ms is not None else time.time() * 1000)
        return self._write(
            "INSERT INTO alarms(ts, kind, source, text) VALUES(?, ?, ?, ?)",
            (ts, str(kind or ""), str(source or ""), str(text or "")))

    def insert_history(self, row):
        """一行历史快照入库。row 为 dict，缺的列记 NULL。

        键与 _SCHEMA 的列名一致：mode/vfd_state/freq_hz/current_a/rpm/
        turb1/turb2/flow/flow_unit/flow_total/total_unit/press_mpa/
        press_note/temp/hum/di/do
        """
        cols = ("mode", "vfd_state", "freq_hz", "current_a", "rpm",
                "turb1", "turb2", "flow", "flow_unit", "flow_total",
                "total_unit", "press_mpa", "press_note", "temp", "hum",
                "di", "do")
        ts = int(row.get("ts") or time.time() * 1000)
        values = [ts] + [row.get(c) for c in cols]
        marks = ",".join("?" * (len(cols) + 1))
        return self._write(
            "INSERT OR REPLACE INTO history(ts,%s) VALUES(%s)"
            % (",".join(cols), marks), values)
