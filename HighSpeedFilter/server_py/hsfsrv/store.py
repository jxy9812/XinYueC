# -*- coding: utf-8 -*-
"""SQLite 落盘层：历史数据 / 运行日志 / 告警事件 三张表 + 容量限额。

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

[容量限额（现场硬盘小，必须能封顶）] 上限 `max_bytes` 由远端设置页给
（0 = 不限）。写满时按 `full_policy` 三选一：

    stop    到限停写 —— 已有数据原样保留（只读），不再入库。
            适合「宁可丢新数据也不能丢历史」的取证场景。
    ring    循环覆盖 —— 删最旧的整段数据（三表按同一时间线裁剪），
            腾出的页用 incremental_vacuum 真正还给系统。
            适合长期无人值守的常规运行（缺省）。
    rotate  分段滚动 —— 写满就把当前库改名成带时间戳的分段文件、
            另起新库，只保留最近 `keep_files` 个分段。
            适合「要能按时间段整体归档/拷贝」的场景。

    [rotate 的磁盘口径] 单段上限 = max_bytes / keep_files，所以总分段
    占用仍封顶在 max_bytes —— 与另两种策略的「最大占用」是同一个数，
    不会因为多保留几段就悄悄翻几倍。

[体积怎么算] 物理占用 = 主库 + `-wal` + `-shm`（WAL 写多了能涨到主库
量级，只算主库文件会低估）。逻辑占用 =（page_count-freelist_count）×
page_size —— 循环删除后页进了空闲表、物理文件暂时不缩，用逻辑口径
判断「内容是否已经低到水位以下」才准。

[失败口径] 打开/写入失败**不致命**：服务照跑（采集与推送不停），
只是落盘停了。磁盘满/路径不可写这类现场问题不该拖垮监控本身。
"""

import os
import sqlite3
import time

#: 缺省库文件名（落在服务端程序目录）。
DEFAULT_DB_NAME = "hsf_data.db"

#: 满额策略取值（与 proto/界面下拉一一对应）。
POLICY_STOP = "stop"        #: 到限停写（数据只读保留）
POLICY_RING = "ring"        #: 循环覆盖（删最旧）
POLICY_ROTATE = "rotate"    #: 分段滚动（写满换文件）
POLICIES = (POLICY_STOP, POLICY_RING, POLICY_ROTATE)
DEFAULT_POLICY = POLICY_RING

#: 循环模式的裁剪水位（占上限的比例）：删到 70% 再放行，避免「删一行
#: 写一行」的抖动；每轮多腾 30% 让下一次触发隔得久一些。
RING_TRIM_TARGET = 0.70
#: 循环模式每轮最多裁剪轮数（防御：目标值算错也不会死循环）。
RING_TRIM_MAX_ROUNDS = 64
#: 单轮删除的最小行数（小库时按比例算出来的行数太少会删不动）。
RING_TRIM_MIN_ROWS = 200
#: 库小于该体积时直接 VACUUM 还盘（限额外的小库，毫秒级）；更大才走
#: incremental_vacuum（避免长事务卡事件循环）。
VACUUM_FULL_MAX_BYTES = 64 * 1024 * 1024
#: 大库增量回收的轮数上限（每轮一批，避免一次占满 CPU）。
VACUUM_ROUNDS = 8
#: WAL 自动检查点阈值（页）。SQLite 缺省 1000 页 ≈ 4MB，限额场景下
#: -wal 会比主库大一个量级（实测主库 172KB、-wal 4.1MB）；压到 128 页
#: （512KB）让物理占用跟内容同步。
WAL_AUTOCHECKPOINT_PAGES = 128

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
    di        INTEGER,               -- DI 位图（≤32 点，4 字节）
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
    """SQLite 落盘器：懒打开、路径在线变更、容量限额、失败不致命。"""

    def __init__(self, base_dir, log=None):
        self._base_dir = base_dir
        self._log = log or (lambda msg: None)
        self._conn = None
        #: 当前库文件路径（None = 尚未打开）。远端改路径后据此判断要重开。
        self._open_path = None
        self._open_cfg_path = None      # 打开时的目标路径（含缺省展开）

        # ---- 容量限额（远端设置页在线改，不需重开库）----
        self._max_bytes = 0             # 0 = 不限
        self._full_policy = DEFAULT_POLICY
        self._keep_files = 3            # rotate：保留的分段数（含当前）
        #: stop 策略下「已停写」的原因（同一次满额只报一次日志）。
        self._pause_reason = ""
        #: 循环模式累计丢弃的行数（界面显示「已循环掉多少」）。
        self._dropped_rows = 0
        #: 维护动作（裁剪/vacuum/分段）进行中：期间的日志不再入库，
        #: 否则「写库失败→打日志→又写库」会递归。
        self._in_maintenance = False
        #: 分段文件路径列表（rotate 模式统计/清理用，按时间升序）。
        self._segments = []

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

    # ---- 容量配置 ----
    def configure(self, max_mb=None, full_policy=None, keep_files=None,
                  max_kb=None):
        """设置容量上限与满额策略（在线生效，不需重开库）。

        :param max_mb:      上限（MB，远端协议口径）；0/None = 不限。
        :param max_kb:      上限（KB，配置文件/极小盘口径）；给了就优先于
                            max_mb（现场盘小到要按 KB 卡时才用）。
        :param full_policy: POLICY_* 之一；非法值回落缺省。
        :param keep_files:  rotate 模式保留的分段数（1..99）。
        :returns: 是否有变化。
        """
        changed = False
        if max_kb is not None:
            bytes_new = max(0, int(max_kb)) * 1024
        elif max_mb is not None:
            bytes_new = max(0, int(max_mb)) * 1024 * 1024
        else:
            bytes_new = self._max_bytes
        if bytes_new != self._max_bytes:
            self._max_bytes = bytes_new
            changed = True
        if full_policy is not None:
            pol = str(full_policy or "").strip().lower()
            if pol not in POLICIES:
                pol = DEFAULT_POLICY
            if pol != self._full_policy:
                self._full_policy = pol
                changed = True
        if keep_files is not None:
            keep = max(1, min(99, int(keep_files)))
            if keep != self._keep_files:
                self._keep_files = keep
                changed = True
        if changed:
            # 配置一变就解除「满额停写」，新上限可能更宽。
            self._pause_reason = ""
        return changed

    @property
    def max_bytes(self):
        return self._max_bytes

    @property
    def full_policy(self):
        return self._full_policy

    @property
    def keep_files(self):
        return self._keep_files

    @property
    def pause_reason(self):
        """非空 = 当前因满额停写（界面/日志用）。"""
        return self._pause_reason

    # ---- 体积测量 ----
    @staticmethod
    def _sidecar_bytes(path):
        """主库 + -wal + -shm 的物理占用（WAL 能涨到主库量级，必须算）。"""
        total = 0
        for suffix in ("", "-wal", "-shm"):
            try:
                total += os.path.getsize(path + suffix)
            except OSError:
                pass
        return total

    def used_bytes(self):
        """当前物理占用（字节）；未打开 = 0。"""
        if self._open_path is None:
            return 0
        return self._sidecar_bytes(self._open_path)

    def _wal_bytes(self):
        """WAL 文件体积（含未回主库的写事务）。"""
        if self._open_path is None:
            return 0
        try:
            return os.path.getsize(self._open_path + "-wal")
        except OSError:
            return 0

    def _logical_bytes(self):
        """逻辑占用 =（总页 - 空闲页）× 页大小（删行后能反映"内容变少了"）。"""
        if self._conn is None:
            return 0
        try:
            page = self._conn.execute("PRAGMA page_count").fetchone()[0]
            free = self._conn.execute("PRAGMA freelist_count").fetchone()[0]
            size = self._conn.execute("PRAGMA page_size").fetchone()[0]
        except sqlite3.Error:
            return 0
        return max(0, page - free) * size

    def row_count(self, table=None):
        """行数（table 给定时单表，否则三表合计）。"""
        if self._conn is None:
            return 0
        tables = (table,) if table else ("history", "logs", "alarms")
        total = 0
        for name in tables:
            try:
                total += self._conn.execute(
                    "SELECT COUNT(*) FROM %s" % name).fetchone()[0]
            except sqlite3.Error:
                continue
        return total

    def stats(self):
        """容量实况（GET 应答 / 界面进度条用）。

        [两个口径都报，但界面按 contentBytes 判进度] `usedBytes` 是磁盘
        真实占用（主库+WAL+shm，现场 `du` 看到的就是它）；`contentBytes`
        是**限额判定口径**（逻辑内容 + WAL）。二者在 WAL/shm 上会差几十
        KB，进度条若按 usedBytes 画，会出现「显示超限但系统没停写」的
        矛盾观感（实测踩到）。界面以 contentBytes 为准，usedBytes 供现场
        核对磁盘。
        """
        used = self.used_bytes()
        content = self._content_bytes()
        return {
            "usedBytes": used,
            "usedMb": round(used / (1024.0 * 1024.0), 3),
            "contentBytes": content,
            "contentMb": round(content / (1024.0 * 1024.0), 3),
            "maxMb": int(self._max_bytes // (1024 * 1024)),
            "maxKb": int(self._max_bytes // 1024),
            "policy": self._full_policy,
            "keepFiles": self._keep_files,
            "paused": bool(self._pause_reason),
            "pauseReason": self._pause_reason,
            "droppedRows": self._dropped_rows,
            "rows": self.row_count(),
            "segments": len(self._segments),
        }

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
            # [isolation_level=None 必须] Python sqlite3 缺省在执行 SELECT 后
            # 会隐式开一个读事务并一直挂着，导致 WAL 检查点永远被阻塞——
            # 实测 `wal_checkpoint` 返回 (0, N, N)：N 帧待写一个都写不进去，
            # -wal 一路涨到 527KB 而主库只有 8 页。设成 autocommit 后检查点
            # 返回 (0,0,0)，WAL 立即归零。这是「容量限额」能成立的前提。
            conn = sqlite3.connect(target, isolation_level=None)
            self._conn = conn
            self._open_path = target
            self._open_cfg_path = configured_path
            # [顺序强制] auto_vacuum 必须在切 WAL 之前设：实测先
            # journal_mode=WAL 再设 auto_vacuum 恒得 0（设了不生效），
            # 循环删除就永远缩不回去。
            self._prepare_autovacuum(target)
            conn.execute("PRAGMA journal_mode=WAL")
            conn.execute("PRAGMA synchronous=NORMAL")
            # [WAL 上限] 默认 1000 页才检查点，5s 一条的写入会让 -wal
            # 长到 4MB（主库才 172KB）——限额场景下等于白占四倍空间。
            # 单库超过 1/8 就检查点并把 WAL 截断回 0。
            conn.execute("PRAGMA wal_autocheckpoint=%d"
                         % WAL_AUTOCHECKPOINT_PAGES)
            conn.executescript(_SCHEMA)
            conn.commit()
            self._refresh_segments(target)
            self._log("数据落盘已启用：%s" % target)
            return True
        except (sqlite3.Error, OSError) as exc:
            self._conn = None
            self._open_path = None
            self._open_cfg_path = None
            self._log("数据落盘打开失败（%s）：不影响采集与推送" % exc)
            return False

    def _prepare_autovacuum(self, path):
        """把库切到 incremental auto_vacuum，让循环删除能真正还盘。

        [为什么必须] 普通 SQLite 删行只把页放进 freelist，文件不会缩小——
        「循环储存」看起来就是磁盘一直停在限额上。incremental_vacuum
        才能按需把空闲页还给系统。auto_vacuum 只在建库前或 VACUUM 后才
        生效，故老库就地转一次（库本来就是限额级的，临时空间可控）。
        """
        try:
            mode = self._conn.execute("PRAGMA auto_vacuum").fetchone()[0]
            if mode == 2:
                return
            self._conn.execute("PRAGMA auto_vacuum=INCREMENTAL")
            # 老库（auto_vacuum=NONE 且已有数据）要靠 VACUUM 才真正生效。
            if mode == 0 and self.row_count() > 0:
                self._conn.execute("VACUUM")
                self._log("数据落盘：库已转 incremental auto_vacuum"
                          "（循环储存可真正释放磁盘）")
        except sqlite3.Error as exc:
            # 转不了不影响写入，只是循环模式物理占用会停在限额上。
            self._log("数据落盘：auto_vacuum 设置失败（%s）" % exc)

    def close(self):
        if self._conn is not None:
            try:
                self._conn.close()
            except sqlite3.Error:
                pass
        self._conn = None
        self._open_path = None
        self._open_cfg_path = None
        self._pause_reason = ""
        self._segments = []

    @property
    def db_path(self):
        """当前库文件路径（未打开 = None；给界面/日志显示用）。"""
        return self._open_path

    # ---- 限额执行 ----
    def _limit_text(self):
        """上限的可读文本（小限额也显示得清：512KB 不写成 0 MB）。"""
        mb = self._max_bytes / (1024.0 * 1024.0)
        if mb >= 1:
            return "%g MB" % round(mb, 2)
        return "%d KB" % max(1, int(self._max_bytes // 1024))
    def _before_write(self):
        """写前闸：按策略腾空间。返回 False = 本次不写。

        [口径] 用**逻辑占用**（页数-空闲页）判定是否到限，而不是物理文件
        大小：WAL 模式下的写事务会先把数据追加到 `-wal` 再检查点回主库，
        按物理大小判定会在每次检查点前后剧烈抖动（实测主库 172KB 时
        -wal 已 4MB），限额就变成了「WAL 有多大算多大」。物理占用由
        `wal_autocheckpoint=128` + 裁剪后 checkpoint(TRUNCATE) 收敛到
        与内容同量级，`stats()` 里两个口径都报出来供现场核对。

        [为什么在写前而不是写后] 写后判定会先越界一批；在写前拦下，
        内容占用不会超过上限。
        """
        if self._max_bytes <= 0 or self._conn is None:
            return True
        if self._full_policy == POLICY_STOP:
            if self._content_bytes() >= self._max_bytes:
                reason = ("已达上限 %s，停止入库（策略：到限停写）"
                          % self._limit_text())
                if reason != self._pause_reason:
                    self._pause_reason = reason
                    self._log_raw("数据落盘：" + reason)
                return False
            return True
        if self._full_policy == POLICY_RING:
            if self._content_bytes() >= self._max_bytes:
                # [无条件裁到水位] 进到这里就说明闸门判定超限了；进去后
                # 再拿 `_content_bytes()<=target` 当循环条件会漏裁——WAL
                # 可能在两次测量之间被自动检查点清掉（实测：闸门看到 19
                # 次超限、每次都"进去即达标"、一行没删）。故只在**首轮
                # 必删**，之后才按水位收敛。
                self._ring_trim()
            return True
        # rotate
        if self._content_bytes() >= self._segment_limit():
            self._rotate()
        return True

    def _content_bytes(self):
        """限额判定口径：逻辑占用 + WAL 的有效部分。

        [为什么加 WAL] 只按逻辑页算，`-wal` 会在两次检查点之间独立膨胀
        （实测 543KB 对 172KB 主库），磁盘上看到的总量会明显超限。
        把 WAL 也算进来，到限时连 WAL 一起被"挤"下来（裁剪→vacuum→
        checkpoint 三步把它清零）。
        """
        return self._logical_bytes() + self._wal_bytes()

    def _used_or_logical(self):
        """取物理与逻辑占用的较大值（物理含 WAL 膨胀，逻辑反映真实内容）。"""
        return max(self.used_bytes(), self._logical_bytes())

    def _segment_limit(self):
        """rotate 单段上限 = 总上限 / 保留段数（总分段占用仍封顶总上限）。"""
        return max(64 * 1024, self._max_bytes // max(1, self._keep_files))

    def _log_raw(self, msg):
        """维护期日志：只写回调，不经 SQLite（防递归）。"""
        if self._in_maintenance:
            return
        self._log(msg)

    def _ring_trim(self):
        """循环覆盖：删最旧的行，直到占用降到水位以下，然后真正还盘。

        [判据与闸门必须同口径] 写前闸用的是 `_content_bytes()`（逻辑 +
        WAL）；如果这里改用纯逻辑占用判「够了就停」，WAL 占大头时会出现
        「闸门说超了→进来→立刻判定没超→什么都不删→下次还是超」的死循环
        （实测：逻辑恒 32768、WAL 涨到 90KB，一行都没删）。故这里用同一
        个 `_content_bytes()`，并在收尾 vacuum+checkpoint 把 WAL 清掉。
        """
        self._in_maintenance = True
        target = int(self._max_bytes * RING_TRIM_TARGET)
        dropped = 0
        try:
            # [先收 WAL] 超限常常不是内容多，而是 WAL 堆积（实测 40 行时
            # 主库 32KB、-wal 已 65KB）。先检查点截断就能解决——无需删
            # 任何一行数据，这在现场是「白赚」的：数据一行没丢但空间回来了。
            self._wal_checkpoint()
            for round_index in range(RING_TRIM_MAX_ROUNDS):
                # 首轮必删（闸门已判定超限，见 _before_write 的说明）；
                # 之后按水位收敛，避免删过头。
                if round_index > 0 and self._content_bytes() <= target:
                    break
                # 每轮开始时再收一次 WAL：上一轮删完/WAL 又涨了。
                if round_index > 0:
                    self._wal_checkpoint()
                    if self._content_bytes() <= target:
                        break
                total = self.row_count("history")
                if total <= 0:
                    # 历史空了还超限（日志/告警把库撑满）：按同口径删。
                    if not self._trim_table("logs", RING_TRIM_MIN_ROWS):
                        if not self._trim_table("alarms",
                                                RING_TRIM_MIN_ROWS):
                            break
                    continue
                # 每轮删「当前行数的 25%」，但不低于总行数本身（否则小库
                # 永远删不到：OFFSET 超过表长 → 拿不到分界 → break，实测
                # 20 行的库一行都删不掉、content 永远卡在初始 32KB）。
                n = max(1, int(total * 0.25))
                if n >= total:
                    # 删的比例已经覆盖整张表：直接按"保留最近 1/4"删。
                    n = max(1, total // 4)
                cut = self._cutoff_ts("history", n)
                if cut is None:
                    # 表里行数不足 n（极小库）：清空历史表（保留结构）。
                    cur = self._conn.execute("DELETE FROM history")
                    self._conn.commit()
                    removed = cur.rowcount or 0
                    dropped += removed
                    if removed <= 0:
                        break
                    self._vacuum_back()
                    self._wal_checkpoint()
                    continue
                removed = self._delete_older_than(cut)
                dropped += removed
                if removed <= 0:
                    break
                # [每轮清 WAL] WAL 会跟着删除事务一起涨；不清掉下一轮的
                # 判据仍按虚高的 WAL 算，会误判"还超着"。
                self._vacuum_back()
                self._wal_checkpoint()
            # 收尾：还页 + 截断 WAL（两步顺序不能反）。
            self._vacuum_back()
            self._wal_checkpoint()
        except sqlite3.Error as exc:
            self._log("数据落盘：循环裁剪失败（%s）" % exc)
        finally:
            self._in_maintenance = False
        self._dropped_rows += dropped
        if dropped:
            self._log("数据落盘：已达上限 %s，循环删除最旧 %d 行（累计 %d）"
                      % (self._limit_text(), dropped, self._dropped_rows))

    def _cutoff_ts(self, table, drop_rows):
        """取「删掉最旧 drop_rows 行」后的时间分界；空表返回 None。"""
        row = self._conn.execute(
            "SELECT ts FROM %s ORDER BY ts LIMIT 1 OFFSET ?" % table,
            (max(0, drop_rows - 1),)).fetchone()
        return row[0] if row else None

    def _delete_older_than(self, cut_ts):
        """三表按同一时间线裁剪（数据保持"最近一段时间"的完整性）。"""
        total = 0
        for table in ("history", "logs", "alarms"):
            cur = self._conn.execute(
                "DELETE FROM %s WHERE ts < ?" % table, (cut_ts,))
            total += cur.rowcount or 0
        self._conn.commit()
        return total

    def _trim_table(self, table, rows):
        """按行数删最旧（历史为空时的兜底路径）。返回删掉的行数。"""
        cut = self._cutoff_ts(table, rows)
        if cut is None:
            return 0
        cur = self._conn.execute(
            "DELETE FROM %s WHERE ts < ?" % table, (cut,))
        self._conn.commit()
        return cur.rowcount or 0

    def _vacuum_back(self):
        """把空闲页真正还给系统。

        [为什么优先 VACUUM 而不是只靠 incremental_vacuum] 实测
        incremental_vacuum 每轮只回收 1 页（freelist 104→103→102…），
        几十轮才缩一点；而 VACUUM 一次就把空闲页全还了（实测 1.7MB →
        0.58MB 用 6ms，限额外的小库代价可忽略）。所以：库小就直接
        VACUUM；库大（>64MB）才用增量口径，避免长事务卡住事件循环。
        """
        try:
            freelist = self._conn.execute(
                "PRAGMA freelist_count").fetchone()[0]
            if freelist <= 0:
                return
            page_size = self._conn.execute(
                "PRAGMA page_size").fetchone()[0]
            pages = self._conn.execute("PRAGMA page_count").fetchone()[0]
            if pages * page_size <= VACUUM_FULL_MAX_BYTES:
                self._conn.execute("VACUUM")
                self._conn.commit()
                return
            if self._conn.execute("PRAGMA auto_vacuum").fetchone()[0] != 2:
                return
            for _ in range(VACUUM_ROUNDS):
                before = self._conn.execute(
                    "PRAGMA freelist_count").fetchone()[0]
                if before <= 0:
                    break
                self._conn.execute("PRAGMA incremental_vacuum(%d)" % before)
                self._conn.commit()
                after = self._conn.execute(
                    "PRAGMA freelist_count").fetchone()[0]
                if after >= before:
                    break
        except sqlite3.Error:
            pass

    def _wal_checkpoint(self):
        """WAL 截断（否则 -wal 一直涨，物理占用虚高）。"""
        try:
            self._conn.execute("PRAGMA wal_checkpoint(TRUNCATE)")
        except sqlite3.Error:
            pass

    # ---- 分段滚动 ----
    @staticmethod
    def _segment_name(path, stamp=None):
        """分段文件名：`<root>.<yyyyMMdd_HHMMSS_微秒><ext>`。

        [必须带微秒] 只有秒级时间戳时，限额很小（现场实测 0.5MB）会在
        同一秒内连续滚动多次 → 重名 → os.rename 报 WinError 183，
        滚动失败退化成停写。加 6 位微秒后同一秒内也不撞。
        """
        if stamp is None:
            now = time.time()
            stamp = "%s_%06d" % (time.strftime("%Y%m%d_%H%M%S",
                                               time.localtime(now)),
                                 int((now % 1) * 1000000))
        root, ext = os.path.splitext(path)
        return "%s.%s%s" % (root, stamp, ext or ".db")

    def _refresh_segments(self, path):
        """列出与主库同名的分段文件（`<root>.<时间戳><ext>`）并清超龄段。

        [别把主库算进去] 主库名形如 `hsf_data.db`，分段是
        `hsf_data.20261106_101500.db`——拿 `hsf_data.` 做前缀会把主库
        文件自己也匹配上（它不以 `.db.` 开头但以 `hsf_data.` 开头吗？
        不会，但 `hsf_data.db` 的 basename 正是 `hsf_data.db`，前缀
        `hsf_data.` 恰好命中）。故显式排除主库文件本身。
        """
        root, ext = os.path.splitext(path)
        directory = os.path.dirname(path) or "."
        prefix = os.path.basename(root) + "."
        segments = []
        try:
            for name in os.listdir(directory):
                if not name.startswith(prefix):
                    continue
                if ext and not name.endswith(ext):
                    continue
                full = os.path.join(directory, name)
                if not os.path.isfile(full):
                    continue
                if os.path.abspath(full) == os.path.abspath(path):
                    continue        # 主库本体不算分段（见上方说明）
                segments.append(full)
        except OSError:
            return
        segments.sort()
        self._segments = segments
        # 只保留最近 keep_files-1 个分段（当前库另算一段）。
        allowed = max(0, self._keep_files - 1)
        while len(self._segments) > allowed:
            old = self._segments.pop(0)
            try:
                for suffix in ("", "-wal", "-shm"):
                    if os.path.exists(old + suffix):
                        os.remove(old + suffix)
            except OSError:
                continue

    def _rotate(self):
        """写满一个分段：改名归档、另起新库、清掉超龄分段。"""
        self._in_maintenance = True
        old_path = self._open_path
        cfg_path = self._open_cfg_path
        try:
            self._wal_checkpoint()
            self._conn.close()
            self._conn = None
            archived = self._segment_name(old_path)
            os.rename(old_path, archived)
            self._log("数据落盘：分段滚动 → %s" % os.path.basename(archived))
        except (OSError, sqlite3.Error) as exc:
            # 滚不动就退回「到限停写」口径：至少不把磁盘撑爆。
            self._pause_reason = "分段滚动失败（%s），已暂停入库" % exc
            self._log("数据落盘：" + self._pause_reason)
        finally:
            self._in_maintenance = False
        # 重开新库（失败由 _write 的空连接保护兜住）。
        self._conn = None
        self._open_path = None
        self._open_cfg_path = None
        self.open_for(cfg_path)
        if self._open_path:
            self._refresh_segments(self._open_path)

    # ---- 写入（全部静默失败：落盘停了不该拖垮监控）----
    def _write(self, sql, params):
        if self._conn is None:
            return False
        if not self._before_write():
            return False
        try:
            self._conn.execute(sql, params)
            self._conn.commit()
            return True
        except sqlite3.Error as exc:
            self._log_raw("数据落盘写入失败：%s" % exc)
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

    # ---- 查询（历史页/图表用；无连接返回空）----
    def query_history(self, limit=500):
        """取最近 limit 行历史（时间正序）。库未开 = 空列表。"""
        if self._conn is None:
            return []
        try:
            rows = self._conn.execute(
                "SELECT ts, mode, vfd_state, freq_hz, current_a, rpm,"
                " turb1, turb2, flow, flow_total, press_mpa, temp, hum,"
                " di, do FROM history ORDER BY ts DESC LIMIT ?",
                (int(limit),)).fetchall()
        except sqlite3.Error:
            return []
        return list(reversed(rows))

    # ---- 分页查询（历史页远端读取；历史数据可能上万行，必须分页）----

    #: 客户端「显示列」的顺序（与 client_py HistoryPage.COLUMNS 一一对应）。
    #: 关键字要在**显示文本**上匹配，服务端必须能按同一顺序拼出这些文本。
    #: 改这里必须同步客户端 COLUMNS 与 _server_row_texts（有测试锁两端一致）。
    DISPLAY_COLUMNS = (
        "时间", "模式", "搅拌状态", "输出频率(Hz)", "输出电流(A)",
        "搅拌转速(rpm)", "浊度1(NTU)", "浊度2(NTU)", "瞬时流量", "正累计",
        "压差(MPa)", "电箱温度(℃)", "电箱湿度(%RH)", "DI(32位)", "DO(32位)")

    #: 每列的 SQLite 文本表达式 —— 与客户端 `_server_row_texts` 的
    #: 格式化规则逐字对齐（数值列两位小数、转速零位、位图 0x%08X、
    #: 流量带单位、压差优先显示说明文字、NULL → "--"）。
    #:
    #: [为什么必须用 CASE ... IS NULL] SQLite 的 `printf('%.2f', NULL)`
    #: 返回字符串 '0.00' 而不是 NULL（实测），若不加保护，离线列的 "--"
    #: 会在搜索里变成 "0.00"，搜 "0.00" 会误命中一堆其实没数据的行。
    #:
    #: [为什么关键字要下推] 分页在服务端、筛选在客户端时，客户端只能在
    #: 拿到的那一页里搜 —— 命中行绝大多数永远翻不到，界面上就是"关键字
    #: 筛选无效"（用户实测反馈）。总数与页内容必须共用同一条 WHERE。
    DISPLAY_EXPR = (
        "strftime('%Y-%m-%d %H:%M:%S', ts / 1000.0, 'unixepoch', 'localtime')",
        "COALESCE(mode, '--')",
        "COALESCE(vfd_state, '--')",
        "CASE WHEN freq_hz IS NULL THEN '--' ELSE printf('%.2f', freq_hz) END",
        "CASE WHEN current_a IS NULL THEN '--' ELSE printf('%.2f', current_a) END",
        "CASE WHEN rpm IS NULL THEN '--' ELSE printf('%.0f', rpm) END",
        "CASE WHEN turb1 IS NULL THEN '--' ELSE printf('%.2f', turb1) END",
        "CASE WHEN turb2 IS NULL THEN '--' ELSE printf('%.2f', turb2) END",
        "CASE WHEN flow IS NULL THEN '--' ELSE printf('%.2f', flow) || ' ' || COALESCE(flow_unit, '') END",
        "CASE WHEN flow_total IS NULL THEN '--' ELSE printf('%.2f', flow_total) || ' ' || COALESCE(total_unit, '') END",
        "CASE WHEN press_note IS NOT NULL AND press_note != '' THEN press_note WHEN press_mpa IS NULL THEN '--' ELSE printf('%.3f', press_mpa) END",
        "CASE WHEN temp IS NULL THEN '--' ELSE printf('%.1f', temp) END",
        "CASE WHEN hum IS NULL THEN '--' ELSE printf('%.1f', hum) END",
        "CASE WHEN di IS NULL THEN '--' ELSE printf('0x%08X', di & 0xFFFFFFFF) END",
        "CASE WHEN do IS NULL THEN '--' ELSE printf('0x%08X', do & 0xFFFFFFFF) END",
    )

    # 时间列单独用 ts（本地时间格式化靠 strftime）；注意时区口径：
    # 客户端用 time.localtime，SQLite 用 'localtime' 修饰符，二者一致。
    @classmethod
    def _keyword_condition(cls, kw, col_indexes=None):
        """关键字条件 → (SQL 片段, 参数)。无条件返回 ("", [])。

        :param col_indexes: 客户端勾选的显示列下标（None/空 = 全部列）。
        LIKE 里 % 与 _ 要转义（用户输入当作字面量，不是通配符）。
        """
        if not kw:
            return "", []
        exprs = list(cls.DISPLAY_EXPR)
        if col_indexes:
            picked = [exprs[i] for i in col_indexes if 0 <= i < len(exprs)]
            exprs = picked or exprs          # 下标全越界时退回全列，勿变成空条件
        joined = " || ' ' || ".join("COALESCE(%s, '')" % e for e in exprs)
        return (" AND (%s) LIKE ? ESCAPE '\\'" % joined), [
            "%" + str(kw).replace("\\", "\\\\").replace("%", "\\%")
                .replace("_", "\\_") + "%"
        ]

    def _history_where(self, lo_ms=None, hi_ms=None, before_ms=None,
                       kw=None, kw_cols=None):
        """历史分页的 WHERE 子句 + 参数（时间 + 关键字 + 列）。

        [所有筛选条件都在库里筛] 分页的总数必须与页内容同一口径：客户端
        先取一页再丢掉不合条件的行，会出现「这一页全被滤空、页码却还
        算着」的怪状 —— 关键字在有分页时更是**完全失效**（命中行在别的
        页上，永远翻不到）。故时间/关键字/列三者都走同一条 WHERE。
        """
        conds, params = [], []
        if lo_ms is not None:
            conds.append("ts >= ?")
            params.append(int(lo_ms))
        if hi_ms is not None:
            conds.append("ts <= ?")
            params.append(int(hi_ms))
        if before_ms is not None:
            conds.append("ts <= ?")
            params.append(int(before_ms))
        kw_sql, kw_params = self._keyword_condition(kw, kw_cols)
        where = (" WHERE " + " AND ".join(conds)) if conds else ""
        if kw_sql:
            # 没有时间条件时 where 还是空串，得去掉 kw_sql 开头的 " AND "
            if where:
                where += kw_sql
            else:
                where = " WHERE " + kw_sql[len(" AND "):]
            params += kw_params
        return where, params

    #: 历史分页返回的列顺序（**客户端按此顺序或按 cols 名字取值**）。
    HISTORY_COLUMNS = (
        "ts", "mode", "vfd_state", "freq_hz", "current_a", "rpm",
        "turb1", "turb2", "flow", "flow_unit", "flow_total", "total_unit",
        "press_mpa", "press_note", "temp", "hum", "di", "do")

    #: 合法的墙钟毫秒下界（2020-01-01）。比它小的 ts 一律是坏数据。
    #:
    #: [为什么必须有这个] 2026-11-06 之前历史表的 ts 误用了 monotonic
    #: （开机计时，实测入库值 3236093 ≈ 1970-01-01），修掉写入口径后
    #: 库里仍留着那批旧行。直接 `MIN(ts)` 会拿到 1970 年：拿它当"最前
    #: 一条数据的时间"填到起始框里，会筛出全部行、跟没填一样（用户要的
    #: 是"最前一条真实数据"）。凡是"给用户看的时间"都要过这道闸。
    VALID_TS_MIN = 1577836800000

    def history_bounds(self):
        """(最早, 最新) 的**有效**墙钟时间戳（毫秒）；空库返回 (0, 0)。

        只统计 ts >= VALID_TS_MIN 的行；非法历史行不参与"最前/最后一条"
        的判定，但仍在表里（不擅自删用户数据）。
        """
        if self._conn is None:
            return 0, 0
        try:
            got = self._conn.execute(
                "SELECT MIN(ts), MAX(ts) FROM history WHERE ts >= ?",
                (self.VALID_TS_MIN,)).fetchone()
        except sqlite3.Error:
            return 0, 0
        if not got:
            return 0, 0
        return (int(got[0] or 0), int(got[1] or 0))

    def count_history(self, lo_ms=None, hi_ms=None, before_ms=None,
                      kw=None, kw_cols=None):
        """满足条件的总行数（分页总数）。库未开 = 0。"""
        if self._conn is None:
            return 0
        where, params = self._history_where(lo_ms, hi_ms, before_ms,
                                            kw, kw_cols)
        try:
            return int(self._conn.execute(
                "SELECT COUNT(*) FROM history" + where, params).fetchone()[0])
        except sqlite3.Error:
            return 0

    def query_history_page(self, offset=0, limit=50, lo_ms=None, hi_ms=None,
                           before_ms=None, kw=None, kw_cols=None):
        """取一页历史（**时间正序**返回，便于直接铺表）。

        :param offset: 跳过多少行（按 ts 倒序数，0 = 最新那行）
        :param limit:  本页最多多少行（调用方负责夹到协议允许的上限）
        :param before_ms: 时间上界（**分页窗口锚点**）。给定时整段分页
            固定在"截至这一刻"的数据上——新采样的行不会插进来把
            offset 顶偏，翻到第 3 页看到的还是同一批行。
        :param kw: 关键字（在显示文本上匹配，含在 WHERE 里）
        :param kw_cols: 关键字要比对的显示列下标（None = 全部）
        :returns: (rows, window_total)。rows 是 tuple 列表（列序见
            HISTORY_COLUMNS），window_total 是**本窗口内**的行数
            （页码总数按它算，不是全库行数）。
        """
        if self._conn is None:
            return [], 0
        where, params = self._history_where(lo_ms, hi_ms, before_ms,
                                            kw, kw_cols)
        try:
            rows = self._conn.execute(
                "SELECT " + ",".join(self.HISTORY_COLUMNS)
                + " FROM history" + where
                + " ORDER BY ts DESC LIMIT ? OFFSET ?",
                params + [int(limit), int(offset)]).fetchall()
            window = int(self._conn.execute(
                "SELECT COUNT(*) FROM history" + where,
                params).fetchone()[0])
        except sqlite3.Error:
            return [], 0
        return list(reversed(rows)), window
