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
import threading
import time

#: 缺省库文件名（落在服务端程序目录）。
#: 「本拍放弃滚动」的内部信号（_rotate 用；区别于真失败——放弃不停写）。
class _RotateRetry(OSError):
    pass


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
#: [2026-11-21 调整] 单块从 1000 降到 250 行 ⇒ 同样的总量需要更多轮
#: ⇒ 上限从 64 提到 256（否则一轮只能裁 16000 行，ring 策略收敛太慢）。
RING_TRIM_MAX_ROUNDS = 256
#: **后台分块裁剪**的单块行数（2026-11-14 首版 1000；2026-11-21 下调）。
#: 写锁持有时长 ≈ 正比于块大小，**而且被 auto_vacuum=INCREMENTAL 的
#: 「搬页」成本放大**：删掉的页要搬到文件尾（实测删 1000 行最坏一次
#: **81.6ms** ⇒ 前台写者被挡满 ⇒ 事件循环冻 80ms ⇒ 自动控制应答被顶
#: 到 60ms 以上）。⇒ 块降到 **250 行**（实测写锁持有 ~10ms 级），
#: 配合 RING_TRIM_MAX_ROUNDS 调大 ⇒ 整轮裁剪的总量不变。
RING_TRIM_CHUNK_ROWS = 250
#: **WAL 归并的触发阈值**（字节，2026-11-14 首版；2026-11-21 下调）。
#: 检查点是一个事务 ⇒ 合并期间**持有写锁** ⇒ 前台写者要等 ⇒
#: **不能攒一大坨再合并**。合并时长 ∝ WAL 体积（实测 0.13MB→1.6ms、
#: 1MB→2.2ms、4MB→3.5ms、8MB→6.5ms；现场实测 23MB 的 WAL 一次合并要
#: **20~114ms** ⇒ 前台写者被挡那么久 ⇒ 自动控制应答被顶到 90ms 以上）。
#: ⇒ 阈值从 4MB 压到 **256KB**：WAL 平时就一小坨 ⇒ 每次合并只拷贝
#: 几百 KB（~1~3ms）⇒ 前台最多等几毫秒。
#: （维护线程的轮询间隔也一起收短，见 MAINT_POLL_S ⇒ 攒不出大 WAL。）
WAL_TRIGGER_BYTES = 256 * 1024
#: **后台维护线程的轮询间隔**（秒）：WAL 超阈值时用**密**档、平时用
#: **疏**档。WAL 在这段窗口里增长的上限 ≈ 写入速率 × 间隔；间隔太长就
#: 会攒出大 WAL（实测 0.1s 间隔下 4 万行灌库把 WAL 撑到 **23MB** ⇒
#: 随后的检查点一次要 20~114ms ⇒ 前台写者被挡 ⇒ 自动控制应答被顶到
#: 90ms 以上）。⇒ 密档 10ms（攒不出大 WAL ⇒ 检查点复制量小 ⇒ 写锁
#: 持有短），疏档 100ms（不白费 CPU）。单次轮询只是「取个文件大小 +
#: 看个时钟」，开销可忽略。
MAINT_POLL_S = 0.01
MAINT_POLL_IDLE_S = 0.1
#: **允许做 TRUNCATE 检查点的 WAL 上限**（字节，2026-11-15 审计补）。
#: TRUNCATE 是把 -wal 文件收回 0 字节的唯一手段（容量按**物理占用**判
#: ⇒ -wal 不缩就虚高 ⇒ 会误触发裁剪），但它要等**所有**写者完成 ⇒ 前台
#: 并发写时长时间持写锁（实测大 WAL 挡前台 116~554ms）。故只在 WAL 本身
#: 不大（≤4MB ⇒ 归并是毫秒级 ⇒ 挡前台 ~1ms）时才做 TRUNCATE；WAL 大时
#: 只做 PASSIVE（内容照样归并进主库，-wal 文件留给后台检查点线程收）。
WAL_TRUNCATE_SAFE_BYTES = 4 * 1024 * 1024
#: 单轮删除的最小行数（小库时按比例算出来的行数太少会删不动）。
RING_TRIM_MIN_ROWS = 200
#: **后台维护线程让路给前台写者的最长等待**（秒，2026-11-21 根修）。
#: 后台每做一块事务前先看有没有前台写者在写（前台写者 = 事件循环
#: 线程 ⇒ 它一卡就是整个事件循环卡住）⇒ 让它先写完。
#: 超时兜底：前台若持续在写（批量灌库的极端情形），后台不能无限等。
FRONT_WRITE_YIELD_MAX_S = 0.05
#: **后台维护连接用的短忙超时**（毫秒，2026-11-21 根修）。
#: SQLite 的 busy handler 退避是**指数级**（1,2,5,10,15,20,25,25,25,
#: 50,50,100ms 累计）⇒ 后台维护一旦输掉第一次写锁竞争，下一次重试要
#: 等 15~25ms ⇒ 前台持续写时后台**几乎永远抢不到**写锁 ⇒ WAL 攒到
#: 20MB ⇒ 随后的检查点一次要 20~114ms ⇒ 前台写者被挡 ⇒ 恶性循环。
#: ⇒ 后台用**短忙超时 + 自己的短间隔重试**（自己掌握节奏）⇒ 几毫秒
#: 内就能插进去。
MAINT_BUSY_TIMEOUT_MS = 5
#: 后台写事务的**重试上限**（次）与重试间隔（秒）。
MAINT_RETRY_TRIES = 200
MAINT_RETRY_SLEEP_S = 0.0005
#: **前台写语句用的短忙超时**（毫秒，2026-11-21 根修）。理由同
#: MAINT_BUSY_TIMEOUT_MS（方向相反：前台是那个被饿的）。
#: 只在**写这条语句**期间生效（读路径仍用默认的 5s，避免读被误判为
#: 「库忙」而静默返回空结果）。
WRITE_BUSY_TIMEOUT_MS = 5
#: 前台写语句的重试上限与重试间隔。
WRITE_RETRY_TRIES = 200
WRITE_RETRY_SLEEP_S = 0.002
#: 写连接/读连接的**默认忙超时**（毫秒）＝ sqlite3.connect 的 timeout
#: 缺省（5s）。写语句期间会临时调成 WRITE_BUSY_TIMEOUT_MS，写完恢复。
DEFAULT_BUSY_TIMEOUT_MS = 5000
#: 库小于该体积时直接 VACUUM 还盘（限额外的小库，毫秒级）；更大才走
#: incremental_vacuum（避免长事务卡事件循环）。
VACUUM_FULL_MAX_BYTES = 64 * 1024 * 1024
#: 大库增量回收的**单次调用时间预算**（秒，2026-11-21 根修②）。
#: [为什么不再是「固定轮数」] 每步只搬几页（写锁短），但**轮数必须够**
#: ——限额外的小库要搬几百页才能把文件缩回上限；固定 8 轮 × 4 页 = 32 页
#: ⇒ 空间回收慢 25 倍 ⇒ **容量上限封不住**（实测 test_store_limit
#: C2/C3/C5 三条红：内容 3443KB / 上限 120KB）。⇒ 改成「搬到搬完，或
#: 用完这个时间预算」：后台线程每轮都会再进来 ⇒ 最终一定回收完，而单次
#: 调用不会长时间持锁（预算封顶）。
VACUUM_TIME_BUDGET_S = 1.5
#: **单次 incremental_vacuum 搬几页**（2026-11-21 根修）。
#: 搬页要把空闲页挪到文件尾 ⇒ N 越大越慢、且**全程持写锁** ⇒ 前台
#: 写者（事件循环线程）被挡 ⇒ 自动控制应答被顶到 90ms 以上（实测一次
#: 搬 61 页 = 322ms；实测 8 页一次最坏 47ms）。⇒ 每次只搬 4 页
#: （~10ms 级），多搬几轮（总量不变）。
VACUUM_PAGES_PER_STEP = 4
#: WAL 自动检查点阈值（页）。SQLite 缺省 1000 页 ≈ 4MB，限额场景下
#: -wal 会比主库大一个量级（实测主库 172KB、-wal 4.1MB）；压到 128 页
#: （512KB）让物理占用跟内容同步。
#:
#: [2026-11-14 改口径] 这个阈值现在**不再**挂在写连接上（挂上去 = 检查点
#: **内联在写入的那次 execute 里** ⇒ 12MB 库实测单次 130~150ms ⇒ 事件
#: 循环被堵 ⇒ **自动控制应答被顶到 130ms 之后**：自动控制往返中位 0.9ms
#: 但 p95=134.6ms / max=147.7ms，120 次里 18 次超 10ms —— 典型的
#: 「大部分很快、偶尔被磁盘 IO 堵一下」）。改为：写连接
#: `wal_autocheckpoint=0`（内联写入恒 0.1ms 量级），检查点交给
#: **独立连接的后台线程**定期做 PASSIVE 检查（PASSIVE 不阻塞写者 ⇒
#: 写入与检查点互不影响）。见 Store._start_maintenance。
WAL_AUTOCHECKPOINT_PAGES = 128
#: [2026-11-22 订正] 上名是**历史遗留死常量**，全仓零引用（实际写连接与
#: 后台连接都是 `PRAGMA wal_autocheckpoint=0`，见 :474/:595）——不要被它
#: 误导成「自动检查点还挂在写连接上」。保留仅为史料，新代码勿用。
#: 后台检查点线程的周期（秒）。WAL 在这段窗口里增长的上限 ≈ 写入速率 ×
#: 周期；2s 足够把 -wal 压在几百 KB 量级（与 128 页口径同量级）。
CHECKPOINT_INTERVAL_S = 2.0

_SCHEMA = """
CREATE TABLE IF NOT EXISTS history (
    ts        INTEGER PRIMARY KEY,   -- epoch 毫秒（采样时刻）
    mode      TEXT,                  -- 手动 / 自动·运行中 / 自动·停止 / 未知
    vfd_state TEXT,                  -- 搅拌机状态（停机/正转运行/故障 …）
    freq_hz   REAL,                  -- 输出频率
    current_a REAL,                  -- 输出电流
    rpm       REAL,                  -- 搅拌转速（频率换算值）
    turb1     REAL,                  -- 进水浊度仪 NTU
    turb2     REAL,                  -- 出水浊度仪 NTU
    flow      REAL,                  -- 瞬时流量
    flow_unit TEXT,                  -- 瞬时流量单位（m³/h …，随流量计）
    flow_total REAL,                 -- 正向累积
    total_unit TEXT,                 -- 累积单位（m³ …）
    press_mpa REAL,                  -- 压差（工程量）
    press_note TEXT,                 -- 压差越限说明（空=正常）
    temp      REAL,                  -- 电箱温度 ℃
    hum       REAL,                  -- 电箱湿度 %RH
    di        INTEGER,               -- DI 位图（≤32 点，4 字节）
    do        INTEGER,               -- DO 位图
    water_temp REAL,                 -- 水温 ℃（出水浊度仪 随帧带出；无温度机型 NULL）
    motor_temp REAL,                 -- 电机温度 ℃（模拟量温度传感器；未配/无读数 NULL）
    press1_mpa REAL,                 -- 压力1 MPa（模拟量「压力1」通道；未配/无读数 NULL）
    press2_mpa REAL                  -- 压力2 MPa（模拟量「压力2」通道；未配/无读数 NULL）
);
CREATE INDEX IF NOT EXISTS idx_history_ts ON history(ts);

CREATE TABLE IF NOT EXISTS logs (
    ts       INTEGER,                -- epoch 毫秒
    category TEXT,                   -- 类别（conn/ctrl/alarm/auto/auth/sys；空=未分类）
    text     TEXT
);
CREATE INDEX IF NOT EXISTS idx_logs_ts ON logs(ts);

CREATE TABLE IF NOT EXISTS alarms (
    ts     INTEGER,                  -- epoch 毫秒
    kind   TEXT,                     -- valve / auto / …（来源分类）
    source TEXT,                     -- 具体对象（阀名 等）
    text   TEXT,                     -- 告警内容
    dev    INTEGER                   -- 设备类别（proto.KIND_*；空 = 无类别）
);
CREATE INDEX IF NOT EXISTS idx_alarms_ts ON alarms(ts);
"""


class Store(object):
    """SQLite 落盘器：懒打开、路径在线变更、容量限额、失败不致命。"""

    def __init__(self, base_dir, log=None):
        self._base_dir = base_dir
        self._log = log or (lambda msg: None)
        self._conn = None
        #: **读连接**（查询线程用；与写连接互不阻塞 ⇒ 慢查询不堵事件循环）。
        self._reader = None
        self._reader_lock = threading.RLock()
        #: 读连接后台关闭的完成事件（rotate 改名前要等它；见 _close_reader）。
        self._reader_close_evt = None
        #: 后台容量维护线程（**兼做 WAL 归并/截断** + 裁剪/还盘；独立连接
        #: + 分块小事务 ⇒ 写路径最多等 ~1ms，见 _request_maintenance 的说明）。
        self._maint_thread = None
        self._maint_stop = None
        self._maint_wake = threading.Event()
        self._maint_rotate = False
        #: [2026-11-22 存活看门狗] 后台线程是 daemon + 宽 catch，一旦静默
        #: 死亡，容量裁剪/WAL 归并永久停止、磁盘无界增长但上层毫无察觉。
        #: 这里记累计被吞掉的异常数 + 最后一次原因，测试与运维可判活。
        self._maint_errors = 0
        self._maint_last_error = ""
        #: 分段滚动两拍（2026-11-14）：前台标记该滚 → 后台把 WAL 归并完
        #: （_wal_drained=True）→ 前台下一次写入才真正改名归档。
        self._rotate_pending = False
        self._wal_drained = False
        #: 当前库文件路径（None = 尚未打开）。远端改路径后据此判断要重开。
        self._open_path = None
        self._open_cfg_path = None      # 打开时的目标路径（含缺省展开）
        #: **最近一条报警**（内存缓存；主页「最近报警」卡的数据源）。
        #: None = 还没发生过报警。格式 {"kind","source","text","dev","ts"}。
        #: [为什么缓存而不是每次查库] 客户端 1s 轮询一次状态快照 ⇒ 每次
        #: 都查库会给事件循环加一根柴（历史查询已经够慢了）；缓存由
        #: `insert_alarm` 单点维护 + 开库时从表里读一次 ⇒ O(1)。
        #: [为什么落盘没开也记] 报警**事件**确实发生了 ⇒ 说"无报警"是
        #: 撒谎（AGENTS「应答里没有的字段不要当成已关闭」）。落盘只是
        #: 不留历史，不影响"刚刚发生过什么"。
        self._last_alarm = None

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
        #: **前台写者「被写锁挡住」的计数**（后台维护线程据此让路，见
        #: `_wait_front_writer`）。前台写者 = 事件循环线程上的落盘写入
        #: （自动控制的运行日志、历史行…）⇒ 它一卡就是**整个事件循环
        #: 卡住** ⇒ 后台维护必须让路。
        #: [只数「真被挡住」的] 正常写（0.1ms 级）不让路 ⇒ 否则后台裁剪
        #: 会被拖到前台写完为止（实测拖到「一条都没裁」）。
        #: [用锁保护] `+=` 不是原子操作（多线程写会漂）⇒ 漂了会让后台
        #: 一直等（裁剪变慢）或一直不等（退回老行为）⇒ 加锁。
        self._front_write_blocked = 0
        self._front_write_lock = threading.Lock()

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

    def _logical_bytes(self, conn=None):
        """逻辑占用 =（总页 - 空闲页）× 页大小（删行后能反映"内容变少了"）。

        [为什么用**写连接**而不是读连接 2026-11-14] 本方法在**写路径**上
        被调用（_before_write → _content_bytes）⇒ 跑在事件循环线程上。
        若它去抢读连接那把锁，而读连接此刻正被工作线程的**长查询**（关键字
        全表扫描 277ms）持着 ⇒ 事件循环就被堵 277ms ⇒ 自动控制应答又被
        顶掉（实测：查询进行中的第一个自动控制往返 233ms）。故写路径一律
        用写连接（写连接只有写入与这些轻量 PRAGMA 读，不会被查询占住）。

        [conn 参数 2026-11-14] 后台维护线程必须传**自己的**连接：sqlite3
        的连接对象**不能跨线程使用**（拿前台连接去用会抛 ProgrammingError
        ⇒ 后台线程直接死掉，裁剪永远不发生——这个坑踩过）。
        """
        if conn is None:
            conn = self._conn
        if conn is None:
            return 0
        try:
            page = conn.execute("PRAGMA page_count").fetchone()[0]
            free = conn.execute("PRAGMA freelist_count").fetchone()[0]
            size = conn.execute("PRAGMA page_size").fetchone()[0]
        except sqlite3.Error:
            return 0
        return max(0, page - free) * size

    def row_count(self, table=None, conn=None):
        """行数（table 给定时单表，否则三表合计）。

        [写连接 2026-11-14] 同 _logical_bytes：本方法在写路径/统计路径上，
        跑在事件循环线程 ⇒ 用写连接，不与长查询抢读连接。

        [conn 参数 2026-11-14] 后台维护线程传自己的连接（sqlite3 连接不能
        跨线程使用）。
        """
        if conn is None:
            conn = self._conn
        if conn is None:
            return 0
        tables = (table,) if table else ("history", "logs", "alarms")
        total = 0
        for name in tables:
            try:
                total += conn.execute(
                    "SELECT COUNT(*) FROM %s" % name).fetchone()[0]
            except sqlite3.Error:
                continue
        return total

    def maintenance_alive(self):
        """后台维护线程在跑（正常 close 后为 False）。"""
        t = self._maint_thread
        return bool(t is not None and t.is_alive())

    def maintenance_errors(self):
        """后台维护累计被吞掉的异常数（存活但反复失败也会涨）。"""
        return self._maint_errors

    def maintenance_last_error(self):
        """最近一次后台维护异常的原因文本（无异常为空串）。"""
        return self._maint_last_error

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
            # [check_same_thread=False 2026-11-20] 相机采集线程的失败日志
            # 走 insert_log ⇒ _write 用的是主线程建的写连接 ⇒ SQLite 线程
            # 亲和报错（"数据落盘写入失败：SQLite objects created in a
            # thread..."，实锅）。写连接与读连接（下方已有该参数）一致：
            # 跨线程使用 + 依赖 busy_timeout 串行化。
            conn = sqlite3.connect(target, isolation_level=None,
                                   check_same_thread=False)
            self._conn = conn
            self._open_path = target
            self._open_cfg_path = configured_path
            # [顺序强制] auto_vacuum 必须在切 WAL 之前设：实测先
            # journal_mode=WAL 再设 auto_vacuum 恒得 0（设了不生效），
            # 循环删除就永远缩不回去。
            self._prepare_autovacuum(target)
            conn.execute("PRAGMA journal_mode=WAL")
            conn.execute("PRAGMA synchronous=NORMAL")
            # [WAL 检查点挪到后台 2026-11-14] 写连接**关掉**自动检查点：
            # 内联检查点 = 检查点的磁盘成本落在「写这条日志」的那一次 execute
            # 里（12MB 库实测单次 130~150ms）⇒ 事件循环被堵 ⇒ 自动控制
            # 应答被顶到 130ms 之后（实测 p95=134.6ms / max=147.7ms）。
            # 改成 0（永不内联）+ 后台维护线程定期归并/截断（见
            # _start_maintenance）：内联写入恒 0.1ms 量级。
            conn.execute("PRAGMA wal_autocheckpoint=0")
            conn.executescript(_SCHEMA)
            conn.commit()
            self._migrate_logs_category(conn)
            self._migrate_history_water_temp(conn)
            self._migrate_history_motor_temp(conn)
            self._migrate_history_pressure_cols(conn)
            self._migrate_alarms_dev(conn)
            # [最近报警 2026-11-19] 开库后读一次最近一行 ⇒ 服务端重启后
            # 主页的「最近报警」卡显示的是**重启前**那条，不是"重启后还没
            # 发生过"。
            self._load_last_alarm()
            self._refresh_segments(target)
            self._open_reader(target)
            self._start_maintenance(target)
            self._log("数据落盘已启用：%s" % target)
            return True
        except (sqlite3.Error, OSError) as exc:
            self._conn = None
            self._open_path = None
            self._open_cfg_path = None
            self._log("数据落盘打开失败（%s）：不影响采集与推送" % exc)
            return False

    # ---- 读连接（查询线程用） ----

    def _open_reader(self, target):
        """开读连接（WAL 模式下读者不阻塞写者）。"""
        self._close_reader()
        try:
            conn = sqlite3.connect(target, isolation_level=None,
                                   check_same_thread=False)
            conn.execute("PRAGMA busy_timeout=5000")
            self._reader = conn
        except (sqlite3.Error, OSError):
            self._reader = None

    def _close_reader(self):
        """关读连接：**先摘引用（快、持锁），真正 close 交后台线程**。

        [2026-11-16 审计] 原来在事件循环线程里直接 `conn.close()`，且不持
        `_reader_lock`：① 与工作线程在途的长查询（关键字全表扫 273~397ms）
        竞态 ⇒ 查询中途连接被关，异常被吞成"共 0 条"（假空页）；
        ② Windows 上 SQLite 延迟关闭会让文件句柄滞留，与 rotate 的改名
        撞 WinError 32。⇒ 摘引用在锁内（读侧 `_reader_conn` 拿到的立刻是
        None，不会再用旧连接）；真正 close 在锁外**后台线程**做——它要
        等在途查询用完（拿同一把锁），事件循环完全不等。
        完成后置 `self._reader_close_evt`（rotate 要等它：句柄没落地
        改名必撞 WinError 32）。
        """
        conn, self._reader = self._reader, None
        if conn is None:
            self._reader_close_evt = None
            return
        evt = threading.Event()
        self._reader_close_evt = evt

        def _shutdown():
            with self._reader_lock:
                try:
                    conn.close()
                except sqlite3.Error:
                    pass
            evt.set()

        try:
            threading.Thread(target=_shutdown, name="hsf-reader-close",
                             daemon=True).start()
        except (RuntimeError, OSError):
            # 起不了线程（解释器收尾等）：就地关——比泄漏句柄强。
            try:
                conn.close()
            except sqlite3.Error:
                pass
            evt.set()

    def _reader_conn(self):
        """取读连接（没有就现开一条；失败返回 None ⇒ 调用方回空结果）。"""
        if self._reader is None and self._open_path:
            self._open_reader(self._open_path)
        return self._reader

    # ---- 后台容量维护（2026-11-14）----
    #
    # [为什么要有这一层] 容量维护（循环裁剪 / 还盘 / 分段滚动）是一次性
    # 重活：实测整轮裁剪 108~271ms、WAL 截断 187ms、VACUUM 23ms。它原先
    # 在**写路径上同步做** ⇒ 事件循环被堵 ⇒ 自动控制命令的应答被顶到几百
    # 毫秒之后（用户口径「自动控制不能因为任何情况出现延迟、停滞」）。
    # 现在：**判定留在写路径（亚毫秒），动作交给后台线程**。
    #
    # [后台线程的三条纪律]
    #   ① **自己的连接**：同一条 sqlite3 连接不能被两个线程同时用 ⇒ 后台
    #      线程自己开一条（写连接留给前台）。
    #   ② **分块小事务**：一次删 1 万行 = 7.4ms 写锁持有（前台写者要等）；
    #      一次删 1000 行 = 0.9ms ⇒ 后台按块删，前台最多等 ~1ms。
    #   ③ **不与前台争长事务**：后台只做短事务（增量回收），全库 VACUUM
    #      这类长事务留给前台之外的路径。

    def _request_maintenance(self, rotate=False):
        """请求后台做容量维护（**非阻塞**：写路径只置标志 + 叫醒线程）。"""
        self._maint_rotate = bool(rotate)
        self._maint_wake.set()

    def _start_maintenance(self, target):
        """起后台维护线程（裁剪 / 还盘；独立连接 + 分块小事务）。"""
        self._stop_maintenance()
        stop_evt = threading.Event()
        self._maint_stop = stop_evt
        # [残留唤醒 2026-11-19 实锅·数据丢失级] `_stop_maintenance` 会
        # `wake.set()`（去阻塞在等的老线程），而这个 Event 是**跨代共用**
        # 的：close() → open_for() 重建（服务端每次启动、远端改库路径、
        # 停用再启用落盘都会走）时，新线程第一拍 `wait(0.1)` 直接被上一代
        # 留下的 wake 放行 ⇒ `woke=True` 且 `_maint_rotate=False` ⇒ 走
        # `_ring_trim` 的「首轮必删」⇒ **库里有 ≥4000 行历史时每次启动都
        # 静默删掉最旧的 ~1000 行**，日志还谎称「已达上限」（根本没到）。
        # ⇒ 起线程前把 wake 清干净，让新线程只响应真正的维护请求。
        self._maint_wake.clear()

        def _loop():
            conn = None
            last_truncate = 0.0
            try:
                conn = sqlite3.connect(target, isolation_level=None)
                conn.execute("PRAGMA wal_autocheckpoint=0")
                # [短忙超时 2026-11-21 根修] 后台必须能**插队**拿到写锁：
                # SQLite 的 busy handler 退避是指数级 ⇒ 前台持续写时后台
                # 几乎永远抢不到 ⇒ WAL 攒到 20MB ⇒ 检查点一次 20~114ms
                # ⇒ 前台写者被挡。短忙超时 + 自己的短间隔重试（见
                # `_maint_exec`）⇒ 几毫秒内插进去。
                conn.execute("PRAGMA busy_timeout=%d" % MAINT_BUSY_TIMEOUT_MS)
                while not stop_evt.is_set():
                    # [自适应轮询 2026-11-21] WAL 超阈值时**密**地查
                    # （攒不出大 WAL ⇒ 检查点的复制量小 ⇒ 写锁持有短），
                    # 平时**疏**地查（不白费 CPU）。
                    try:
                        _wal_now = self._wal_bytes()
                    except OSError:
                        _wal_now = 0
                    woke = self._maint_wake.wait(
                        MAINT_POLL_S if _wal_now > WAL_TRIGGER_BYTES
                        else MAINT_POLL_IDLE_S)
                    if stop_evt.is_set():
                        break
                    if not woke:
                        # [自适应归并 2026-11-14] 检查点是**一个事务** ⇒
                        # 合并期间持有写锁 ⇒ 前台写者要等它。合并时长 ∝ WAL
                        # 大小 ⇒ **不能攒一大坨再合并**（实测 351MB 的 WAL
                        # 合并一次挡前台 116ms）。改口径：
                        #   · WAL 超过阈值（**WAL_TRIGGER_BYTES = 256KB**）
                        #     ⇒ **立刻**归并（此时它只有几百 KB ⇒ 合并
                        #     ~1ms ⇒ 前台最多等 ~1ms）；
                        #   · 否则按**自适应**间隔兜底归并（密档
                        #     MAINT_POLL_S=10ms / 疏档 MAINT_POLL_IDLE_S=
                        #     100ms；把 -wal 截断到 0 ⇒ 磁盘占用口径准确、
                        #     也不会「文件虚大 ⇒ 反复误触发裁剪」）。
                        # [数字订正 2026-11-21] 这段注释原先写「4MB / 每 2
                        # 秒兜底」——那是 2026-11-14 的旧口径，实际常量
                        # 早已改成 256KB + 自适应 10ms/100ms（见文件头
                        # WAL_TRIGGER_BYTES / MAINT_POLL_*），**注释漂移会
                        # 把人带回旧认知**，已订正。
                        try:
                            big = self._wal_bytes() > WAL_TRIGGER_BYTES
                            now = time.monotonic()
                            if big or (now - last_truncate) >= CHECKPOINT_INTERVAL_S:
                                # [2026-11-21 根修] 走**安全 TRUNCATE**
                                # （先等读连接空闲，不持写锁地等）⇒ 不再
                                # 因为「有长查询在跑」而把前台写者挡满整
                                # 个查询时长（实测 191.7ms ⇒ 现在 ~3ms）。
                                # [让路] 检查点也是写事务 ⇒ 先让前台写者
                                # 写完，避免被它挤掉。
                                self._wait_front_writer()
                                self._wal_checkpoint(conn)
                                last_truncate = now
                        except sqlite3.Error as exc:
                            self._maint_errors += 1
                            self._maint_last_error = "WAL检查点: %s" % exc
                        continue
                    self._maint_wake.clear()
                    try:
                        if self._maint_rotate:
                            # [分段滚动第一拍 2026-11-14] 把 WAL 归并回
                            # 主库并**截断**（TRUNCATE，在后台做 ⇒ 不顶
                            # 事件循环）⇒ 之后前台关连接时 SQLite 已无页可
                            # 归并 ⇒ 关连接从 148ms 降到 **1.3ms**（实测），
                            # 前台只剩「关连接 → 改名 → 重开」= 毫秒级。
                            # [2026-11-21 根修] 走**安全 TRUNCATE**（先等
                            # 读连接空闲，不持写锁地等）⇒ 长查询在跑时不会
                            # 把前台写者挡满整个查询时长。
                            self._wal_checkpoint(conn)
                            self._wal_drained = True

                        else:
                            self._ring_trim(conn, chunked=True)
                    except sqlite3.Error as exc:
                        self._maint_errors += 1
                        self._maint_last_error = "维护动作: %s" % exc
                        self._log("数据落盘：后台维护失败（%s）" % exc)
            except (sqlite3.Error, OSError) as exc:
                # [2026-11-22] 这处曾是最危险的静默死亡点：一句 pass 让
                # 裁剪/归并永久停摆而无人知晓 ⇒ 计数 + 留痕。
                self._maint_errors += 1
                self._maint_last_error = "线程退出: %s" % exc
                self._log("数据落盘：后台维护线程异常退出（%s）" % exc)
            finally:
                if conn is not None:
                    try:
                        conn.close()
                    except sqlite3.Error:
                        pass

        thread = threading.Thread(target=_loop, name="hsf-store-maintenance",
                                  daemon=True)
        self._maint_thread = thread
        thread.start()

    def _stop_maintenance(self):
        """停后台维护线程（幂等）。

        [2026-11-15 审计] 原来的 `thread.join(2.0)` **会阻塞事件循环最多
        2 秒**：`close()` 是从配置 SET 的处理函数里调的（事件循环线程）⇒
        运维在远端设置点一次「应用」⇒ 整个服务端（遥测推送 + 自动控制
        命令）静默 2 秒。⇒ 改成**短等 + 放手**：维护线程是 daemon，且
        **用的是自己那条连接**（不与前台连接共用）⇒ 它自己会退出，晚一点
        退出没有任何副作用。
        """
        thread, self._maint_thread = self._maint_thread, None
        stop_evt, self._maint_stop = self._maint_stop, None
        if stop_evt is not None:
            stop_evt.set()
        # [只在确有老线程在等时才 set] 2026-11-19 起 `_start_maintenance`
        # 会 clear 这个共用 Event；这里若无条件 set，紧随其后的
        # `_start_maintenance` 的 clear 之前有一小窗，老线程也可能不在等
        # ⇒ set 白留残留 ⇒ 又回到「每次启动假维护、删最旧 1000 行」。
        # 故只在有活线程需要叫醒时才 set（没有线程在等就不欠任何唤醒）。
        if thread is not None and thread.is_alive():
            self._maint_wake.set()
            # [短等 2026-11-15] 0.2s 足够它从 wait 里醒来并退出；超时也
            # 不强等（daemon 线程 + 独立连接 ⇒ 放手安全）。
            thread.join(0.2)

    def _migrate_history_water_temp(self, conn):
        """老库补 history.water_temp 列（2026-11-10，水温列落地）。

        [为什么要迁移] `CREATE TABLE IF NOT EXISTS` 对**已存在**的表不动
        ⇒ 现场已有 hsf_data.db 永远补不上 water_temp，而分页查询的 SELECT
        点名了它 ⇒ **整页查询失败、历史页一行都取不到**（不是"少一列"，
        是"整页空"）。

        [为什么先查再改] ADD COLUMN 不幂等（第二次报 duplicate column
        name）⇒ 先 PRAGMA table_info 看有没有。与 C 端
        `storeEnsureHistoryWaterTemp`（HsfServerStore.c）同口径。
        """
        try:
            cols = [row[1] for row in
                    conn.execute("PRAGMA table_info(history)").fetchall()]
        except sqlite3.Error as exc:
            self._log("数据落盘：history 表结构探测失败（%s）：水温列不可用"
                      % exc)
            return
        if "water_temp" in cols:
            return
        try:
            conn.execute("ALTER TABLE history ADD COLUMN water_temp REAL")
            conn.commit()
            self._log("数据落盘：history 表已补 water_temp 列（水温）")
        except sqlite3.Error as exc:
            self._log("数据落盘：history 表补 water_temp 列失败（%s）："
                      "历史页可能取不到数据" % exc)

    def _migrate_history_motor_temp(self, conn):
        """老库补 history.motor_temp 列（2026-11-23，电机温度列落地）。

        [为什么要迁移] 与 water_temp 完全同因：`CREATE TABLE IF NOT EXISTS`
        对**已存在**的表不动 ⇒ 现场已有 hsf_data.db 永远补不上 motor_temp，
        而分页查询的 SELECT 点名了它 ⇒ **整页查询失败、历史页一行都取不到**。

        [为什么先查再改] ADD COLUMN 不幂等（第二次报 duplicate column name）
        ⇒ 先 PRAGMA table_info 看有没有。
        """
        try:
            cols = [row[1] for row in
                    conn.execute("PRAGMA table_info(history)").fetchall()]
        except sqlite3.Error as exc:
            self._log("数据落盘：history 表结构探测失败（%s）：电机温度列不可用"
                      % exc)
            return
        if "motor_temp" in cols:
            return
        try:
            conn.execute("ALTER TABLE history ADD COLUMN motor_temp REAL")
            conn.commit()
            self._log("数据落盘：history 表已补 motor_temp 列（电机温度）")
        except sqlite3.Error as exc:
            self._log("数据落盘：history 表补 motor_temp 列失败（%s）："
                      "历史页可能取不到数据" % exc)

    def _migrate_history_pressure_cols(self, conn):
        """老库补 history.press1_mpa / press2_mpa 列（2026-11-23，压力1/2）。

        [为什么要迁移] 与 water_temp/motor_temp 完全同因：`CREATE TABLE IF
        NOT EXISTS` 对**已存在**的表不动 ⇒ 现场已有 hsf_data.db 永远补不上
        press1_mpa/press2_mpa，而分页查询的 SELECT 点名了它们 ⇒ **整页
        查询失败、历史页一行都取不到**（不是"少两列"，是"整页空"）。

        [先 PRAGMA 探测再 ALTER] ADD COLUMN 不幂等（第二次报 duplicate
        column name）⇒ 逐列探测、缺哪列补哪列；两列都已在的新库直接返回。
        """
        try:
            cols = [row[1] for row in
                    conn.execute("PRAGMA table_info(history)").fetchall()]
        except sqlite3.Error as exc:
            self._log("数据落盘：history 表结构探测失败（%s）："
                      "压力列不可用" % exc)
            return
        for col, label in (("press1_mpa", "进水压力"),
                           ("press2_mpa", "出水压力")):
            if col in cols:
                continue
            try:
                conn.execute("ALTER TABLE history ADD COLUMN %s REAL" % col)
                conn.commit()
                self._log("数据落盘：history 表已补 %s 列（%s）"
                          % (col, label))
            except sqlite3.Error as exc:
                self._log("数据落盘：history 表补 %s 列失败（%s）："
                          "历史页可能取不到数据" % (col, exc))

    def _migrate_alarms_dev(self, conn):
        """老库补 alarms.dev 列（2026-11-17，报警历史取数）。

        [为什么要迁移] 同 logs.category：CREATE TABLE IF NOT EXISTS 对**已存在**
        的表不做任何改动 ⇒ 现场已有库永远补不上 dev 列 ⇒ 客户端报警页的
        「类型」列显示不出设备类别（温湿度/流量计…）。
        [为什么先查再改] ADD COLUMN 不幂等（第二次报 duplicate column）。
        """
        try:
            cols = [row[1] for row in
                    conn.execute("PRAGMA table_info(alarms)").fetchall()]
        except sqlite3.Error as exc:
            self._log("数据落盘：alarms 表结构探测失败（%s）：设备类别不可用"
                      % exc)
            return
        if "dev" in cols:
            return
        try:
            conn.execute("ALTER TABLE alarms ADD COLUMN dev INTEGER")
            conn.commit()
            self._log("数据落盘：alarms 表已补 dev 列（报警设备类别）")
        except sqlite3.Error as exc:
            self._log("数据落盘：alarms 表补 dev 列失败（%s）：设备类别不可用"
                      % exc)

    def _migrate_logs_category(self, conn):
        """老库补 logs.category 列（2026-11-09，类别列落地）。

        [为什么要迁移] CREATE TABLE IF NOT EXISTS 对**已存在**的表不做任何
        改动 ⇒ 现场已有 hsf_data.db（只有 ts/text 两列）永远补不上类别列，
        0x0119 的类别筛选就一直是空的。

        [为什么先查再改] SQLite 的 ADD COLUMN 不是幂等的（第二次会报
        duplicate column name）⇒ 先 PRAGMA table_info 看有没有。

        [失败不致命] 迁移失败只是类别筛选不可用，落盘主体照常工作。
        """
        try:
            cols = [row[1] for row in
                    conn.execute("PRAGMA table_info(logs)").fetchall()]
        except sqlite3.Error as exc:
            self._log("数据落盘：logs 表结构探测失败（%s）：类别筛选不可用" % exc)
            return
        if "category" in cols:
            return
        try:
            conn.execute("ALTER TABLE logs ADD COLUMN category TEXT")
            conn.commit()
            self._log("数据落盘：logs 表已补 category 列（日志类别）")
        except sqlite3.Error as exc:
            self._log("数据落盘：logs 表补 category 列失败（%s）：类别筛选不可用"
                      % exc)

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
        self._stop_maintenance()
        self._close_reader()
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

        [2026-11-14 关键改动：**重活不在这条路径上**] 裁剪/还盘/分段滚动
        是一次性重活（实测：整轮 ring 裁剪 108~271ms、WAL 截断 187ms、
        VACUUM 23ms）。它原先在这里**同步**做 ⇒ 事件循环被堵 ⇒
        **自动控制命令的应答被顶到几百毫秒之后**（用户口径「自动控制不能
        因为任何情况出现延迟、停滞」）。改法：
          · 判定（`_content_bytes()`，纯 PRAGMA+stat，亚毫秒）留在写路径；
          · **动作**（裁剪/还盘/分段）交给**后台维护线程**（独立连接 + 分块
            小事务 ⇒ 写锁持有时间 ~1ms/块 ⇒ 写路径最多等 ~1ms）；
          · 「到限停写」（POLICY_STOP）仍在写路径判定并拒写（那是不写，不是
            重活，语义要求立即生效）；
          · 环形覆盖（POLICY_RING）改为「**请求后台裁剪**」后**本次写入照
            常放行**（数据会短暂超限一点，后台随即裁回）——宁可短暂超限，
            也不能让控制命令卡住。
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
                # [2026-11-14] 裁剪动作改由后台维护线程做（写路径不阻塞）。
                self._request_maintenance()
            return True
        # rotate
        if self._content_bytes() >= self._segment_limit():
            # [2026-11-14 分段滚动改成「两拍」] 分段滚动要改库文件名 ⇒
            # 必须在前台做（前台持有写连接，Windows 下别的连接开着就改名
            # 失败）。但**贵的不是改名**（实测 1.8ms），而是「关连接时
            # SQLite 把 WAL 归并回主库」（实测 148ms）⇒ 拆成两拍：
            #   ① 前台：标记「该滚了」+ 请后台把 WAL 先归并（后台做 ⇒ 不顶
            #      事件循环），**本次写入照常放行**（段限额短暂超一点可
            #      接受，限额是软闸）；
            #   ② 后台归并完（_wal_drained=True）后，前台下一次写入时
            #      才真正滚：关连接（此时 WAL 已空 ⇒ 毫秒级）→ 改名归档
            #      → 重开新库。
            if not self._rotate_pending:
                self._rotate_pending = True
                self._wal_drained = False
                self._request_maintenance(rotate=True)
            elif self._wal_drained:
                self._rotate_pending = False
                self._wal_drained = False
                self._rotate()
        return True

    def _content_bytes(self, conn=None):
        """限额判定口径：逻辑占用 + **WAL 文件占用**（物理口径）。

        [为什么加 WAL] 只按逻辑页算，`-wal` 会在两次检查点之间独立膨胀
        （实测 543KB 对 172KB 主库），磁盘上看到的总量会明显超限。
        把 WAL 也算进来，到限时连 WAL 一起被"挤"下来（裁剪→vacuum→
        checkpoint 三步把它清零）。

        [口径必须稳定 2026-11-14] 曾一度改成「WAL 里**还没归并**的量」
        （后台算好缓存给前台读）⇒ 语义变了：**磁盘上限**变成了「内容上限」，
        连带客户端回归的「ring 策略真的丢行」用例失效（上限 120KB、
        只长 40 行 ⇒ 内容远不到上限 ⇒ 不丢行）。**限额是给现场守磁盘的**
        ⇒ 必须按物理占用判。后台维护线程按 **WAL 超过 256KB 就归并**
        （否则每 10/100ms 自适应兜底一次，见 `_start_maintenance`）⇒
        -wal 平时就是 0 字节 ⇒ 物理口径既准确又不会虚高（不会误触发裁剪）。

        [conn 参数 2026-11-14] 后台维护线程必须传**自己的**连接：sqlite3
        的连接对象**不能跨线程使用**（拿前台连接去用会抛 ProgrammingError
        ⇒ 后台线程直接死掉，裁剪永远不发生——这个坑踩过）。
        """
        return self._logical_bytes(conn) + self._wal_bytes()

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

    def _ring_trim(self, conn=None, chunked=False):
        """循环覆盖：删最旧的行，直到占用降到水位以下，然后真正还盘。

        [判据与闸门必须同口径] 写前闸用的是 `_content_bytes()`（逻辑 +
        WAL）；如果这里改用纯逻辑占用判「够了就停」，WAL 占大头时会出现
        「闸门说超了→进来→立刻判定没超→什么都不删→下次还是超」的死循环
        （实测：逻辑恒 32768、WAL 涨到 90KB，一行都没删）。故这里用同一
        个 `_content_bytes()`，并在收尾 vacuum+checkpoint 把 WAL 清掉。

        [2026-11-14 参数化] `conn` = 本次使用的连接（后台维护线程用**自己
        的**连接；同一条 sqlite3 连接不能被两个线程同时用）。
        `chunked=True` ⇒ **分块小事务**：每块自己一个事务（写锁持有 ~1ms）
        ⇒ 前台写者最多等 ~1ms，而不是被一次大 DELETE/TRUNCATE 堵 100ms+
        （实测：整轮裁剪 108~271ms、WAL 截断 187ms、VACUUM 23ms）。
        """
        if conn is None:
            conn = self._conn
        # [守卫 2026-11-14] 上限可能已被放开（运行期改配置）或已放大 ⇒
        # 这次后台裁剪**不再必要**：直接返回。
        # [为什么必须查] 裁剪请求是「异步到达」的：发出请求时超限，之后
        # 管理员把上限调大/取消了，唤醒的那次裁剪仍会按旧意图执行——而
        # 「首轮必删」逻辑会一路删到水位（甚至删光）⇒ 白删数据（实测：
        # 上限放开后仍被裁掉 3600+ 行）。
        if self._max_bytes <= 0:
            return
        # [2026-11-19] 「残留/过时的维护请求」在**唤醒层**根治（见
        # `_start_maintenance` 的 clear 与 `_stop_maintenance` 的按需 set）：
        # 新线程起手 wake 已清干净 ⇒ 只有真正由写前闸发出的请求才会唤醒
        # 裁剪 ⇒ 不需要在这里再复核占用（复核会扰动裁剪节拍：突发写入的
        # 空窗里 content 短暂回落就整轮跳过，实测把 C1「发生了丢弃」打成
        # dropped=0；而旧注记的「进去即达标死循环」靠循环内每轮复核收敛，
        # 语义不变）。
        self._in_maintenance = True
        target = int(self._max_bytes * RING_TRIM_TARGET)
        dropped = 0
        # [口径 2026-11-14] 循环内的占用用**本连接**算（前台连接是另一条
        # ⇒ 两条连接看到的 WAL 状态可能不同）。
        try:
            # [先收 WAL] 超限常常不是内容多，而是 WAL 堆积（实测 40 行时
            # 主库 32KB、-wal 已 65KB）。先检查点就能解决——无需删
            # 任何一行数据，这在现场是「白赚」的：数据一行没丢但空间回来了。
            # [用 PASSIVE 而不是 TRUNCATE 2026-11-14] TRUNCATE 要等所有
            # 写者完成 ⇒ 前台并发写时它会长时间持写锁（实测挡住前台写
            # 554ms！）；PASSIVE 不阻塞写者 ⇒ 后台裁剪全程只挡前台 ~1ms/块。
            self._wal_checkpoint(conn, mode="PASSIVE")
            for round_index in range(RING_TRIM_MAX_ROUNDS):
                # [让路 2026-11-21 根修] 每块事务之前先让前台写者
                # （= 事件循环线程）写完 ⇒ 它不会被后台的连续分块事务
                # **饿满整轮**（实测 46~62ms）。此时**不持**写锁 ⇒ 不会
                # 死锁（前台写者正在 busy handler 里等的就是这把锁）。
                self._wait_front_writer()
                # 首轮必删（闸门已判定超限，见 _before_write 的说明）；
                # 之后按水位收敛，避免删过头。
                if round_index > 0 and self._content_bytes(conn) <= target:
                    break
                # 每轮开始时再收一次 WAL：上一轮删完/WAL 又涨了。
                if round_index > 0:
                    self._wal_checkpoint(conn, mode="PASSIVE")
                    if self._content_bytes(conn) <= target:
                        break
                total = self.row_count("history", conn)
                if total <= 0:
                    # 历史空了还超限（日志/告警把库撑满）：按同口径删。
                    if not self._trim_table("logs", RING_TRIM_MIN_ROWS, conn):
                        if not self._trim_table("alarms",
                                                RING_TRIM_MIN_ROWS, conn):
                            break
                    continue
                # 每轮删「当前行数的 25%」，但不低于总行数本身（否则小库
                # 永远删不到：OFFSET 超过表长 → 拿不到分界 → break，实测
                # 20 行的库一行都删不掉、content 永远卡在初始 32KB）。
                n = max(1, int(total * 0.25))
                if n >= total:
                    # 删的比例已经覆盖整张表：直接按"保留最近 1/4"删。
                    n = max(1, total // 4)
                if chunked:
                    # [分块] 一轮再切若干小块，每块一个事务 ⇒ 写锁持有
                    # ~1ms/块（实测删 1000 行 0.9ms、删 10000 行 7.4ms）。
                    n = min(n, RING_TRIM_CHUNK_ROWS)
                cut = self._cutoff_ts("history", n, conn)
                if cut is None:
                    # 表里行数不足 n（极小库）：清空历史表（保留结构）。
                    cur = self._maint_exec(conn, "DELETE FROM history")
                    if cur is None:
                        break
                    removed = cur.rowcount or 0
                    dropped += removed
                    if removed <= 0:
                        break
                    self._vacuum_back(conn)
                    self._wal_checkpoint(conn, mode="PASSIVE")
                    continue
                removed = self._delete_older_than(cut, conn)
                dropped += removed
                if removed <= 0:
                    break
                # [每轮清 WAL] WAL 会跟着删除事务一起涨；不清掉下一轮的
                # 判据仍按虚高的 WAL 算，会误判"还超着"。
                # [2026-11-15 审计] 这里**必须用 PASSIVE**：TRUNCATE 会等
                # 所有写者完成 ⇒ 前台并发写时长时间持写锁（实测大 WAL 时
                # 挡前台写 116~554ms）⇒ 自动控制应答被顶掉。PASSIVE 不挡
                # 写者（内容照样归并进主库），只是 -wal 文件不缩。
                self._vacuum_back(conn)
                self._wal_checkpoint(conn, mode="PASSIVE")
            # 收尾：还页 + 收 WAL（两步顺序不能反）。
            # [TRUNCATE 只在 WAL 不大时做] TRUNCATE 才是把 -wal 文件收回
            # 0 字节的唯一手段（容量按**物理占用**判 ⇒ -wal 不缩就虚高），
            # 但它会挡前台写 ⇒ 只在 WAL 小（< 4MB，此时归并是毫秒级）时
            # 才做；WAL 大时只 PASSIVE，归并由后台检查点线程兜底。
            self._vacuum_back(conn)
            if self._wal_bytes() <= WAL_TRUNCATE_SAFE_BYTES:
                self._wal_checkpoint(conn)          # TRUNCATE：把 -wal 收小
            else:
                self._wal_checkpoint(conn, mode="PASSIVE")
        except sqlite3.Error as exc:
            # [2026-11-22 看门狗] 这是最关键的裁剪路径：它自己吞异常，
            # 不会冒泡到 _loop 的「维护动作」计数点 ⇒ 不在这里记，裁剪
            # 静默失败时 maintenance_errors() 仍是 0，C7 假绿。
            self._maint_errors += 1
            self._maint_last_error = "循环裁剪: %s" % exc
            self._log("数据落盘：循环裁剪失败（%s）" % exc)
        finally:
            self._in_maintenance = False
        self._dropped_rows += dropped
        if dropped:
            self._log("数据落盘：已达上限 %s，循环删除最旧 %d 行（累计 %d）"
                      % (self._limit_text(), dropped, self._dropped_rows))

    def _cutoff_ts(self, table, drop_rows, conn=None):
        """取「删掉最旧 drop_rows 行」后的时间分界；空表返回 None。"""
        if conn is None:
            conn = self._conn
        row = conn.execute(
            "SELECT ts FROM %s ORDER BY ts LIMIT 1 OFFSET ?" % table,
            (max(0, drop_rows - 1),)).fetchone()
        return row[0] if row else None

    def _delete_older_than(self, cut_ts, conn=None):
        """三表按同一时间线裁剪（数据保持"最近一段时间"的完整性）。

        [2026-11-21 根修] 走 `_maint_exec`：被前台写者占着写锁时
        **快速重试**（SQLite 自己的 busy handler 退避是指数级 ⇒ 后台
        几乎抢不到锁 ⇒ WAL 攒大 ⇒ 检查点变长 ⇒ 前台被挡 ⇒ 恶性循环）。

        [2026-11-21 根修②：**每张表一个事务**（不要攒着一起提交）]
            原来用 `commit=False` 把三张表的 DELETE 留在**同一个未提交
            事务**里 ⇒ 后台线程**一直持着写锁** ⇒ 前台写者（事件循环
            线程）拿不到锁 ⇒ 而后台的 `_wait_front_writer` 又在等前台
            写完 ⇒ **互等** ⇒ 裁剪循环第二轮起就拿不到锁（实测
            test_store_limit C5「丢弃计数不涨」600→600、C2/C3「内容封顶」
            红：3443KB / 上限 120KB）⇒ 容量上限根本封不住。
            ⇒ 改成**每张表删完立刻提交**：写锁持有时间回到毫秒级，
            前台随时能插进来，后台下一轮也能重新抢到锁。
        """
        if conn is None:
            conn = self._conn
        total = 0
        for table in ("history", "logs", "alarms"):
            cur = self._maint_exec(
                conn, "DELETE FROM %s WHERE ts < ?" % table, (cut_ts,),
                commit=True)
            if cur is None:
                break
            total += cur.rowcount or 0
        return total

    def _trim_table(self, table, rows, conn=None):
        """按行数删最旧（历史为空时的兜底路径）。返回删掉的行数。"""
        if conn is None:
            conn = self._conn
        cut = self._cutoff_ts(table, rows, conn)
        if cut is None:
            return 0
        cur = self._maint_exec(
            conn, "DELETE FROM %s WHERE ts < ?" % table, (cut,))
        return (cur.rowcount or 0) if cur is not None else 0

    def _vacuum_back(self, conn=None):
        """把空闲页真正还给系统。

        [为什么优先 VACUUM 而不是只靠 incremental_vacuum] 实测
        incremental_vacuum 每轮只回收 1 页（freelist 104→103→102…），
        几十轮才缩一点；而 VACUUM 一次就把空闲页全还了（实测 1.7MB →
        0.58MB 用 6ms，限额外的小库代价可忽略）。所以：库小就直接
        VACUUM；库大（>64MB）才用增量口径，避免长事务卡住事件循环。

        [2026-11-14 conn 参数] 后台维护线程用**自己的**连接（同一条
        sqlite3 连接不能被两个线程同时用）；VACUUM 是全库独占事务 ⇒
        后台跑它时前台写者仍会被挡（实测大库 VACUUM 23ms）⇒ 故后台
        分块模式下**只走增量口径**（逐页小事务，写锁持有 ~ms 级）。
        """
        if conn is None:
            conn = self._conn
        chunked = conn is not self._conn
        try:
            freelist = conn.execute(
                "PRAGMA freelist_count").fetchone()[0]
            if freelist <= 0:
                return
            page_size = conn.execute(
                "PRAGMA page_size").fetchone()[0]
            pages = conn.execute("PRAGMA page_count").fetchone()[0]
            if pages * page_size <= VACUUM_FULL_MAX_BYTES and not chunked:
                conn.execute("VACUUM")
                conn.commit()
                return
            if conn.execute("PRAGMA auto_vacuum").fetchone()[0] != 2:
                return
            # [2026-11-21 根修②] **按时间预算持续搬**（不是固定 8 轮）：
            #   每步仍只搬 VACUUM_PAGES_PER_STEP 页（写锁持有 ~10ms 级，
            #   这是治「自动控制被顶 90ms+」的关键，**不能**为了搬得快
            #   回到「一次搬全部页」的写法）；
            #   但**轮数必须够**——原来固定 8 轮 × 4 页 = 一次最多搬 32 页，
            #   而限额外的小库要搬几百页才能把文件缩回上限 ⇒ 空间回收慢了
            #   25 倍 ⇒ **容量上限封不住**（实测 test_store_limit C2/C3/C5
            #   三条红：内容 3443KB / 上限 120KB）。
            #   ⇒ 改成「一直搬到搬完，或用完时间预算（VACUUM_TIME_BUDGET_S）
            #   为止」：后台线程每轮都会再进来 ⇒ 空间最终一定回收完，
            #   而单次调用不会长时间持锁（预算上限）。
            _deadline = time.monotonic() + VACUUM_TIME_BUDGET_S
            while True:
                before = conn.execute(
                    "PRAGMA freelist_count").fetchone()[0]
                if before <= 0:
                    break
                # [2026-11-21 根修] **每次只搬几页**：`incremental_vacuum`
                # 在 auto_vacuum=INCREMENTAL 下要把空闲页**搬到文件尾**
                # ⇒ N 越大越慢、且**全程持写锁**（实测一次性搬 61 页要
                # 322ms ⇒ 前台写者被挡满 ⇒ 自动控制应答被顶到 90ms）。
                # ⇒ 每次只搬 VACUUM_PAGES_PER_STEP 页（几毫秒），
                # 多搬几轮 —— 总量不变，写锁持有时长被压到毫秒级。
                cur = self._maint_exec(
                    conn, "PRAGMA incremental_vacuum(%d)"
                    % min(before, VACUUM_PAGES_PER_STEP))
                if cur is None:
                    break
                after = conn.execute(
                    "PRAGMA freelist_count").fetchone()[0]
                if after >= before:
                    break
                if time.monotonic() >= _deadline:
                    break
        except sqlite3.Error:
            pass

    def _wal_checkpoint(self, conn=None, mode="TRUNCATE"):
        """WAL 检查点。

        [mode 2026-11-14] PASSIVE = 不阻塞写者（后台裁剪用这个）；
        TRUNCATE = 把 -wal 物理截断（会等所有写者 ⇒ 只在后台专用场合用）。

        [conn 参数 2026-11-14] 后台维护线程用自己的连接。

        [2026-11-21 根修：TRUNCATE 必须在 `_reader_lock` 里做]
            **病根**：SQLite 的 TRUNCATE/RESTART 检查点要**等所有读者
            结束**才能重置 WAL，而这段等待期间**写锁是被持有的** ⇒
            只要有一个长查询在跑（历史分页的关键字全表扫，实测
            155~400ms），**前台写者（= 事件循环线程）就被挡满整个
            查询时长** ⇒ 事件循环整个冻住 ⇒ 自动控制（0x0115）的
            应答被顶到几百毫秒之后。
            实测（2026-11-21 插桩）：慢查询 155.6ms 期间后台做
            `PRAGMA wal_checkpoint(TRUNCATE)`（耗时 172.4ms）⇒ 前台
            `INSERT INTO logs`（自动控制命令的落盘）被挡 **191.2ms**
            ⇒ 控制往返 **191.7ms**；微基准复现：1 秒长读者 ⇒ TRUNCATE
            耗时 990ms、前台写被挡 998ms。
            **修法**：先拿 `_reader_lock`（读连接上的长查询全都在它
            里面）——拿不到就在**不持写锁**的状态下等；拿到后说明
            当前没有读者 ⇒ TRUNCATE 立刻完成（实测 WAL ≤4MB 时
            1.4~3.5ms）⇒ **写锁只被短暂持有**。
            PASSIVE 不等读者、不重置 WAL ⇒ 不需要拿锁（它只复制到
            最老读者的标记为止 ⇒ 实测 1~3ms 且不挡写者）。
        """
        if conn is None:
            conn = self._conn
        if mode == "TRUNCATE":
            # 安全 TRUNCATE：先等读连接空闲（在途长查询都在 _reader_lock
            # 里）⇒ 等不到时是「不持写锁地等」，不会挡住前台写者。
            with self._reader_lock:
                # [重试 2026-11-21 根修] 前台持续写时后台抢不到写锁 ⇒
                # 短忙超时 + 自己的短间隔重试（见 `_maint_exec` 的说明）。
                self._maint_exec(conn, "PRAGMA wal_checkpoint(TRUNCATE)",
                                 commit=False)
            return
        # PASSIVE：不等读者、不重置 WAL ⇒ 只复制到最老读者的标记为止。
        self._maint_exec(conn, "PRAGMA wal_checkpoint(%s)" % mode,
                         commit=False)

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
        """写满一个分段：改名归档、另起新库、清掉超龄分段。

        [2026-11-14 三处修正]
          ① **改名前必须关掉所有别的连接**：Windows 上文件被打开时改名报
             WinError 32（"另一个程序正在使用此文件"）⇒ 后台检查点线程、
             后台维护线程、读连接各自都持有一条连接 ⇒ 全部关掉才能改名
             （实测：开着必失败，关掉后改名只要 1.8ms）。
          ② **WAL 截断挪到后台**：TRUNCATE 检查点是整条路径里最贵的一步
             （实测 131~187ms）⇒ 先让后台把 WAL 归并回主库（PASSIVE），
             前台只做「关连接 → 改名 → 重开」（毫秒级）。
          ③ 归档文件连 **-wal/-shm** 一起改名：只改主库的话，残留的
             -wal/-shm 会跟新库文件同名 ⇒ SQLite 可能把旧 WAL 当新库的
             日志读进去（数据错乱）。
          ④ [2026-11-16 审计] 后台维护线程没退出就**本拍放弃滚动**（连接
             一个都不动，写前闸下拍再试）——原先硬着头皮改名，与还活着
             的旧线程撞 WinError 32 ⇒ 滚动失败退"停写"；且失败后
             `_pause_reason` 永不清 ⇒ 界面恒显"已暂停入库"而数据照写
             （口径相反）。现在：成功滚动即清 `_pause_reason`。
        """
        self._in_maintenance = True
        old_path = self._open_path
        cfg_path = self._open_cfg_path
        try:
            # ① 停后台线程（它持有一条连接）+ 关读连接 ⇒ 改名不被占用挡。
            #
            # [等旧线程真退出 2026-11-16 审计] `_stop_maintenance` 的
            # join(0.2) 超时即放手（为不堵事件循环）——但旧线程可能还在
            # 跑一轮裁剪（实测 108~271ms）且**持着旧库连接** ⇒ 紧接着的
            # os.rename 必撞 WinError 32。⇒ 给一次**有上限的补等**（~1.5s，
            # 分 50ms 小睡）：rotate 本来就是低频事件（写满一个分段才
            # 一次），多等一拍可接受，换"滚动必成"。
            # [注意先取引用再停] `_stop_maintenance` 会把 `_maint_thread`
            # 置 None（join 超时即放手）⇒ 循环里再读它恒 None ⇒ 等了个空。
            thread = self._maint_thread
            self._stop_maintenance()
            for _ in range(30):
                if thread is None or not thread.is_alive():
                    break
                time.sleep(0.05)
            else:
                raise _RotateRetry(
                    "后台维护线程未退出，本拍放弃滚动（下拍再试）")
            self._close_reader()
            # [等读连接句柄落地 2026-11-16] 读连接的 close 在后台线程（要
            # 等在途长查询）⇒ 这里等它完成再改名，否则 Windows 上句柄还
            # 开着、改名必撞 WinError 32。同样有上限（~1.5s），超时本拍
            # 放弃（下拍再试）。
            evt = self._reader_close_evt
            if evt is not None:
                for _ in range(30):
                    if evt.is_set():
                        break
                    time.sleep(0.05)
                else:
                    raise _RotateRetry(
                        "读连接关闭未完成（在途长查询），本拍放弃滚动"
                        "（下拍再试）")
            # ② **不显式 TRUNCATE**：关掉最后一条连接时 SQLite 自己会把
            #    WAL 归并回主库并删掉 -wal/-shm（实测这一步与显式
            #    TRUNCATE 同量级，但 TRUNCATE 是**前台**做的 ⇒ 直接把
            #    131ms 顶在事件循环上）。改成：先让后台维护线程把 WAL
            #    PASSIVE 归并（后台做，不顶循环），前台只关连接+改名。
            self._conn.close()
            self._conn = None
            archived = self._segment_name(old_path)
            os.rename(old_path, archived)
            # ③ WAL/SHM 一起归档（否则残留文件与新库同名 ⇒ 数据错乱）。
            for suffix in ("-wal", "-shm"):
                src = old_path + suffix
                if os.path.exists(src):
                    try:
                        os.rename(src, archived + suffix)
                    except OSError:
                        try:
                            os.remove(src)
                        except OSError:
                            pass
            self._log("数据落盘：分段滚动 → %s" % os.path.basename(archived))
            # ④ 成功滚动 ⇒ 历史的"暂停入库"说明已过时（新库从零开始）。
            self._pause_reason = ""
        except _RotateRetry as exc:
            # [本拍放弃] 连接一个都没动（旧的还在收尾）⇒ 不停写、不改名，
            # `_rotate_pending` 保持 ⇒ 下一次写前闸再走一遍 `_rotate()`。
            # `_stop_maintenance` 已置停位 ⇒ 旧线程最迟一个等待周期自己退。
            self._log("数据落盘：%s" % exc)
            self._in_maintenance = False
            return
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
        # [2026-11-15 审计] `_before_write()` 可能触发 rotate：rotate 失败
        # 时它把 `self._conn` 置 None（"停写"）⇒ 这里再取一次，否则
        # `self._conn.execute` 抛 **AttributeError**（不在 except
        # sqlite3.Error 内）⇒ 从日志调用点冒泡进业务代码 ⇒ 打断正在处理
        # 异常的代码（严重时会连 tick 一起带死）。
        conn = self._conn
        if conn is None:
            return False
        # [让路标记 2026-11-21 根修] 前台写者（**事件循环线程**）真被
        # 写锁挡住时，后台维护线程必须让路（见 `_wait_front_writer`）。
        # [为什么] SQLite 的写锁**不是公平锁**：同一个连接释放后可以
        # 立刻再拿到 ⇒ 后台连续做分块事务时，前台写者会被**饿满整轮**
        # （实测 46~62ms）⇒ 事件循环整个冻住 ⇒ 自动控制应答被顶到
        # 60ms 以上（实测「容量维护进行中」max=139.9ms）。
        return self._exec_write(conn, sql, params)

    def _exec_write(self, conn, sql, params):
        """执行一条写语句；写锁被后台维护占着时**自己快速重试**。

        [为什么自己重试 2026-11-21 根修] SQLite 的 busy handler 退避是
        **指数级**（1,2,5,10,15,20,25,25,25,50,50,100ms 累计）⇒ 前台
        写者（= **事件循环线程**）一旦输掉第一次写锁竞争，下一次重试
        要等 15~25ms ⇒ 实测被后台的分块事务/检查点**饿满整轮**
        （16~62ms）⇒ 事件循环整个冻住 ⇒ 自动控制（0x0115）的应答被
        顶到 60ms 以上。
        ⇒ 写这条语句期间把**忙超时调短**（WRITE_BUSY_TIMEOUT_MS）+ 用
        **自己的短间隔重试**掌握节奏 ⇒ 几毫秒内插进去。
        ⇒ 撞到 BUSY 时置「**前台被挡**」标志 ⇒ 后台维护线程据此让路
        （`_wait_front_writer`）⇒ 双向都不被饿。
        ⇒ 写完把忙超时**恢复**成缺省：读路径（row_count / history_bounds
        / _logical_bytes，都走这条写连接）仍要 5s 的耐心 —— 读在 WAL
        模式下不与写者抢锁，但万一碰上也不该被误判成「库忙」而静默
        返回空结果（那会让历史页显示 1970 年）。
        ⇒ 重试到头仍失败 ⇒ 如实记日志返回 False（落盘失败不致命，
        但**不假装成功**）。
        """
        try:
            conn.execute("PRAGMA busy_timeout=%d" % WRITE_BUSY_TIMEOUT_MS)
        except sqlite3.Error:
            pass
        blocked_marked = False
        try:
            for _ in range(WRITE_RETRY_TRIES):
                try:
                    conn.execute(sql, params)
                    conn.commit()
                    return True
                except sqlite3.OperationalError as exc:
                    msg = str(exc).lower()
                    if "locked" not in msg and "busy" not in msg:
                        self._log_raw("数据落盘写入失败：%s" % exc)
                        return False
                    if not blocked_marked:
                        blocked_marked = True
                        with self._front_write_lock:
                            self._front_write_blocked += 1
                    time.sleep(WRITE_RETRY_SLEEP_S)
                except sqlite3.Error as exc:
                    self._log_raw("数据落盘写入失败：%s" % exc)
                    return False
            self._log_raw("数据落盘写入失败：写库被占用，重试到头")
            return False
        finally:
            if blocked_marked:
                with self._front_write_lock:
                    self._front_write_blocked -= 1
            try:
                conn.execute("PRAGMA busy_timeout=%d"
                             % DEFAULT_BUSY_TIMEOUT_MS)
            except sqlite3.Error:
                pass

    def _wait_front_writer(self, timeout_s=FRONT_WRITE_YIELD_MAX_S):
        """后台维护：**让路**给前台写者（调用时**不持**写锁 ⇒ 不会死锁）。

        [为什么 2026-11-21 根修] SQLite 的写锁**不是公平锁**：后台维护
        线程连续做分块事务（DELETE → incremental_vacuum → 检查点，一个
        接一个）时，前台写者（= 事件循环线程）即使很快就能拿到锁也会被
        挤掉 ⇒ 被**饿满整轮**（实测 46~62ms）⇒ 事件循环冻住 ⇒ 自动
        控制（0x0115）的应答被顶到 60ms 以上。
        ⇒ 每块事务之前先看**有没有前台写者真的被挡住了**（`_exec_write`
        撞到 BUSY 时会置 `_front_write_blocked`）⇒ 有就等它写完——
        前台写者此刻在重试循环里等锁，我们一让出来它就能拿到。
        ⇒ **只看「真被挡住」不看「正在写」**：前台正常写（0.1ms 级）
        时也让路 ⇒ 后台裁剪会被拖到前台写完为止（实测把 ring 裁剪拖到
        「一条都没裁」⇒ test_store_limit 的 C1 红）。
        ⇒ 超时兜底：万一前台写者持续被挡（批量灌库的极端情形），后台
        不能无限等 ⇒ 到点就继续做（后台让路是「锦上添花」，不能反过来
        把容量维护拖死）。
        """
        waited = 0.0
        while self._front_write_blocked and waited < timeout_s:
            time.sleep(0.0005)
            waited += 0.0005


    @staticmethod
    def _maint_exec(conn, sql, params=(), commit=True):
        """后台维护用的写语句执行：**被前台占着写锁时快速重试**。

        [为什么 2026-11-21 根修] SQLite 的 busy handler 退避是**指数
        级**（1,2,5,10,15,20,25,25,25,50,50,100ms 累计）⇒ 后台维护一旦
        输掉第一次写锁竞争，下一次重试要等 15~25ms ⇒ 前台持续写时
        后台**几乎永远抢不到**写锁 ⇒ WAL 攒到 20MB（实测）⇒ 随后的
        检查点一次要 20~114ms ⇒ 前台写者被挡 ⇒ 恶性循环。
        ⇒ 用**短忙超时**（MAINT_BUSY_TIMEOUT_MS）+ **短间隔重试**
        （自己掌握节奏）⇒ 几毫秒内插进去。
        ⇒ 返回 None = 重试到头仍拿不到锁（调用方按「这轮跳过」处理，
        下一轮再来；**不丢数据**：语句要么整条成功要么整条没做）。
        """
        for _ in range(MAINT_RETRY_TRIES):
            try:
                cur = conn.execute(sql, params)
                if commit:
                    conn.commit()
                return cur
            except sqlite3.OperationalError as exc:
                msg = str(exc).lower()
                if "locked" not in msg and "busy" not in msg:
                    raise
                time.sleep(MAINT_RETRY_SLEEP_S)
            except sqlite3.Error:
                raise
        return None

    def insert_log(self, text, now_ms=None, category=""):
        """运行日志入库（app._log 的旁路；入此处不再回调 _log，防递归）。

        :param category: 运行日志类别（conn/ctrl/alarm/auto/auth/sys；
            空串 = 未分类）。老库（无 category 列）的行按未分类处理。
        """
        ts = int(now_ms if now_ms is not None else time.time() * 1000)
        return self._write("INSERT INTO logs(ts, category, text) VALUES(?, ?, ?)",
                           (ts, str(category or ""), str(text)))

    def insert_alarm(self, kind, source, text, now_ms=None, dev=None):
        """告警事件入库（阀超时/自动运行告警/健康边沿…）。

        :param dev: 设备类别（proto.KIND_*；None = 无类别）。
        """
        ts = int(now_ms if now_ms is not None else time.time() * 1000)
        # [最近报警缓存 2026-11-19] 主页「最近报警」卡要一行摘要 ⇒ 每次
        # 入库顺手更新内存缓存（O(1)，不查库）。缓存**先于**落盘结果更新：
        # 落盘失败（库没开/磁盘满）不代表事件没发生 ⇒ 说"无报警"是撒谎。
        self._last_alarm = {"kind": str(kind or ""),
                            "source": str(source or ""),
                            "text": str(text or ""),
                            "dev": None if dev is None else int(dev),
                            "ts": ts}
        return self._write(
            "INSERT INTO alarms(ts, kind, source, text, dev)"
            " VALUES(?, ?, ?, ?, ?)",
            (ts, str(kind or ""), str(source or ""), str(text or ""),
             None if dev is None else int(dev)))

    def last_alarm(self):
        """**最近一条报警**（dict；None = 从未发生过报警）。

        :returns: {"kind","source","text","dev","ts"}（ts = epoch 毫秒）。

        [为什么带 source] 主页卡显示「时间 + 类型 + 文本」，类型用 dev
        映射（proto.KIND_*）；source 是设备名/来源对象 —— 阀告警的 source
        是阀名（如「进水阀(3)」），自动告警是「自动运行」⇒ 主页摘要能说清
        "哪台/哪类设备出的事"，不必让用户跳到报警页才看明白。

        [为什么开库时读一次表] 服务端重启后，报警**历史**仍在表里 ⇒
        "最近一条"应该是重启前那条，而不是"重启后还没发生过"。
        """
        if self._last_alarm is None and self._conn is not None:
            self._load_last_alarm()
        return dict(self._last_alarm) if self._last_alarm else None

    def _load_last_alarm(self):
        """开库后从 alarms 表读一次最近一行（补重启前的"最近一条"）。

        [只在内存缓存还是 None 时才读] 内存里已有 ⇒ 那是**刚发生的**事
        实（比库里任何一行都新）⇒ 不能被库里的旧行盖掉。换了库路径也
        一样：事件发生过就是发生过，不因为换了落盘位置就消失。
        """
        if self._last_alarm is not None:
            return
        conn = self._conn
        if conn is None:
            return
        try:
            row = conn.execute(
                "SELECT ts, COALESCE(kind, ''), COALESCE(source, ''),"
                " COALESCE(text, ''), dev FROM alarms"
                " ORDER BY ts DESC, rowid DESC LIMIT 1").fetchone()
        except sqlite3.Error:
            return
        if row:
            self._last_alarm = {"ts": int(row[0]), "kind": str(row[1]),
                                "source": str(row[2]), "text": str(row[3]),
                                "dev": None if row[4] is None else int(row[4])}

    def query_alarms_page(self, offset=0, limit=50, lo_ms=None, hi_ms=None,
                          before_ms=None, kw=None, with_count=True):
        """取一页报警历史（**ts 降序**，与历史/日志分页同口径）。

        :returns: (rows, window_total)。rows 是 [(ts, kind, source, text,
            dev)]，**页内按 ts 正序**（第 1 页 = 最新的一页，页内由旧到新）。
        """
        if self._conn is None:
            return [], 0
        where = []
        params = []
        if before_ms is not None:
            where.append("ts <= ?")
            params.append(int(before_ms))
        if lo_ms is not None:
            where.append("ts >= ?")
            params.append(int(lo_ms))
        if hi_ms is not None:
            where.append("ts <= ?")
            params.append(int(hi_ms))
        if kw:
            where.append("(COALESCE(text, '') || ' ' || COALESCE(source, '')"
                         " || ' ' || COALESCE(kind, '')) LIKE ? ESCAPE '\\'")
            params.append("%" + str(kw).replace("\\", "\\\\")
                          .replace("%", "\\%").replace("_", "\\_") + "%")
        clause = (" WHERE " + " AND ".join(where)) if where else ""
        conn = self._reader_conn()
        if conn is None:
            return [], 0
        try:
            with self._reader_lock:
                rows = conn.execute(
                    "SELECT ts, COALESCE(kind, ''), COALESCE(source, ''),"
                    " COALESCE(text, ''), dev FROM alarms" + clause
                    + " ORDER BY ts DESC, rowid DESC LIMIT ? OFFSET ?",
                    params + [int(limit), int(offset)]).fetchall()
                window = -1
                if with_count:
                    window = int(conn.execute(
                        "SELECT COUNT(*) FROM alarms" + clause,
                        params).fetchone()[0])
        except sqlite3.Error:
            return [], 0
        return list(reversed(rows)), window

    def insert_history(self, row):
        """一行历史快照入库。row 为 dict，缺的列记 NULL。

        键与 _SCHEMA 的列名一致：mode/vfd_state/freq_hz/current_a/rpm/
        turb1/turb2/flow/flow_unit/flow_total/total_unit/press_mpa/
        press_note/temp/hum/di/do/water_temp/motor_temp/press1_mpa/
        press2_mpa
        """
        cols = ("mode", "vfd_state", "freq_hz", "current_a", "rpm",
                "turb1", "turb2", "flow", "flow_unit", "flow_total",
                "total_unit", "press_mpa", "press_note", "temp", "hum",
                "di", "do", "water_temp", "motor_temp",
                # [压力1/压力2 2026-11-23 追加在末尾] 与 _SCHEMA / 
                # HISTORY_COLUMNS 同序：尾部增量列不移动任何既有下标。
                "press1_mpa", "press2_mpa")
        ts = int(row.get("ts") or time.time() * 1000)
        values = [ts] + [row.get(c) for c in cols]
        marks = ",".join("?" * (len(cols) + 1))
        return self._write(
            "INSERT OR REPLACE INTO history(ts,%s) VALUES(%s)"
            % (",".join(cols), marks), values)

    # ---- 查询（历史页/图表用；无连接返回空）----
    def query_history(self, limit=500):
        """取最近 limit 行历史（时间正序）。库未开 = 空列表。"""
        conn = self._reader_conn()
        if conn is None:
            return []
        try:
            with self._reader_lock:
                rows = conn.execute(
                    "SELECT ts, mode, vfd_state, freq_hz, current_a, rpm,"
                    " turb1, turb2, flow, flow_total, press_mpa, temp, hum,"
                    " di, do, water_temp, motor_temp, press1_mpa, press2_mpa"
                    " FROM history"
                    " ORDER BY ts DESC LIMIT ?",
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
        "搅拌转速(rpm)", "电机温度(℃)", "进水浊度(NTU)", "出水浊度(NTU)",
        "水温(℃)",
        "瞬时流量", "正累计",
        # [压力1/压力2 2026-11-23] 显示列插在「压差(MPa)」之后（用户口径
        # 「压力1和压力2也要进历史数据被采集」）：压差卡的三栏同序，看
        # 表格时三路压力相邻；其后 电箱温度/湿度/DI/DO 整体右移 2 位。
        # 线上列序另在 HISTORY_COLUMNS 末尾追加（C 端旧下标不受影响）。
        "压差(MPa)", "进水压力(MPa)", "出水压力(MPa)",
        "电箱温度(℃)", "电箱湿度(%RH)", "DI(32位)", "DO(32位)")

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
        # [电机温度 2026-11-23] 模拟量温度传感器（与 VFD 卡 motorTempC 同源：
        # cfg.analog.temp_c）；未配置/读不到入库 NULL ⇒ 显示 "--"。
        "CASE WHEN motor_temp IS NULL THEN '--' "
        "ELSE printf('%.1f', motor_temp) END",
        "CASE WHEN turb1 IS NULL THEN '--' ELSE printf('%.2f', turb1) END",
        "CASE WHEN turb2 IS NULL THEN '--' ELSE printf('%.2f', turb2) END",
        # [水温：显示列第 8 位，紧跟浊度2] 线上列序里它在末尾（HISTORY_COLUMNS），
        # 显示列序由 DISPLAY_COLUMNS 决定，两套顺序互不影响。
        "CASE WHEN water_temp IS NULL THEN '--' "
        "ELSE printf('%.1f', water_temp) END",
        "CASE WHEN flow IS NULL THEN '--' ELSE printf('%.2f', flow) || ' ' || COALESCE(flow_unit, '') END",
        "CASE WHEN flow_total IS NULL THEN '--' ELSE printf('%.2f', flow_total) || ' ' || COALESCE(total_unit, '') END",
        # [压差列显示**数值**（2026-11-19 用户口径「历史数据里应该记录
        #  具体的负压值用负数，而不是说是个负压」）] 原来 note 优先 ⇒ 负压
        #  行显示「低于 4.0mA（负压）」而看不到 -0.008 ⇒ 现在**数值优先**
        #  （负压就是负数）；「这行当时越限」的信息仍在 press_note 列里
        #  （异常行标红照旧按它判）⇒ 不丢信号。
        "CASE WHEN press_mpa IS NULL THEN '--' ELSE printf('%.3f', press_mpa) END",
        # [压力1/压力2 2026-11-23] 紧跟压差；三位小数与压差同口径。
        # 未配传感器/通道无读数 ⇒ NULL ⇒ "--"（不是 0）。
        "CASE WHEN press1_mpa IS NULL THEN '--' "
        "ELSE printf('%.3f', press1_mpa) END",
        "CASE WHEN press2_mpa IS NULL THEN '--' "
        "ELSE printf('%.3f', press2_mpa) END",
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

        [2026-11-20 性能剖析：试过「逐列 OR」写法，**实测是负优化，已回退**]
            试想：把 16 个显示表达式**各自** LIKE 再 OR 起来（不去串接那条
            16 段的长文本），应当比"串接成一条再 LIKE"省掉每行的字符串
            拼接。**但实测只在"关键字命中绝大多数行"时才快**（那时 OR
            能在第一列就短路），一旦命中率低就**更慢**（每行要跑 16 次
            LIKE，而串接版只跑 1 次）。

            端到端实测（2 万行库，各列取值有多种，模拟真实数据）：

                关键字            命中率   串接版      逐列 OR 版
                （无关键字）        —      3.29 ms      2.42 ms
                '自动·停止'        25%    84.82 ms     89.28 ms   （慢 5%）
                '正转运行'          8%    88.55 ms     90.42 ms   （慢 2%）
                '42.00'            0%   165.72 ms    208.64 ms   （慢 26%）
                'zzz-没有这个'      0%   166.73 ms    204.28 ms   （慢 23%）

            ⇒ 真实数据上 OR 版**没有一项更快** ⇒ 保持原串接版不动。
            （早期用"每行取值都一样"的夹具量到 4.2 倍加速，是夹具失真：
            那种数据下关键字命中每一行，OR 永远在第一列短路。）

        [关键字分页是 O(全表) 的事实]
            显示文本是在**库里逐行现算**的（16 个表达式/行），LIKE '%kw%'
            又用不上索引 ⇒ 命中与否都要扫全表 ⇒ 2 万行库单次 80~200 ms。
            要再快只能**缩小参与匹配的列集**或**建索引**，两者都会改口径，
            本轮不做（见交付报告）。
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
        "press_mpa", "press_note", "temp", "hum", "di", "do",
        # [water_temp 追加在**末尾**] 行值按**位置**发给 C 客户端
        # （kSrc/kDst 是下标映射表），插在中间会让后续下标整体平移；
        # 追加在末尾则既有下标全不变。显示列序另由 DISPLAY_COLUMNS 决定。
        # [motor_temp 2026-11-23 同样追加在末尾] 原因同上：C 端（未部署
        # 的旧实现）按位置读前 19 列，尾部多一列它自然忽略。
        # [press1_mpa/press2_mpa 2026-11-23 继续追加在末尾] 线上列 20→22；
        # 显示列序插在压差之后由 DISPLAY_COLUMNS/DISPLAY_EXPR 决定，与此处
        # 无关。客户端历史表按列名取值（_server_row_texts 的 dict(zip)），
        # 故显示列的插入不动这里的下标契约。
        "water_temp", "motor_temp", "press1_mpa", "press2_mpa")

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

        [2026-11-20 性能优化①：改用索引定位（0.78 ms → 0.008 ms）]
            原实现 `SELECT MIN(ts), MAX(ts) FROM history WHERE ts >= ?`
            —— SQLite 对**带 WHERE 的 MIN/MAX 聚合**不做索引定位，
            会**扫一遍索引**（2 万行实测 **0.78 ms**；这个方法在历史页
            每次翻页都要调，是服务端侧仅次于关键字 COUNT 的开销）。
            改成两条**带 ORDER BY ts LIMIT 1** 的点查（走 idx_history_ts
            的索引定位，各 0.004 ms）⇒ **快约 100 倍**，结果逐字相同。
            [口径说明 2026-11-22] 上面是**2 万行**口径；文档 §4.2.1 记的
            「4 万行 2.134ms → 0.014ms（157×）」是**另一轮 4 万行**实测，
            两套数字数据集不同、都成立，别当成互相矛盾。

        [2026-11-20 性能优化②：**改走写连接**，不再抢读连接那把锁]
            根因（本轮 5 连跑实测：慢查询进行中的自动控制被顶 **233~267ms**）：
            本方法原先用**读连接** + `_reader_lock` ⇒ 与在途的**关键字全表
            扫**（4 万行实测 273~397ms）**互等** ⇒ 它自己被堵到查询结束 ⇒
            它是在 `await asyncio.to_thread(...)` 里等的 ⇒ **事件循环照样
            被卡**（app.py 注释里已自曝过同样现象：「若挪到工作线程，它会
            去抢读连接那把锁 ⇒ 与长查询互等 ⇒ 又把事件循环堵住」）。
            ⇒ 边界查询只是「取 ts 的最小/最大」，**不需要读连接**：改用
            **写连接**（与读连接独立、互不排队；写连接上的点查是微秒级
            索引定位），与 `row_count` 的既有口径一致（「跑在事件循环线程
            ⇒ 用写连接，不与长查询抢读连接」）。
        """
        if self._conn is None:
            return 0, 0
        conn = self._conn
        # [BUSY 重试 2026-11-21] 写语句期间写连接被临时设成**短忙超时**
        # （见 `_exec_write`）⇒ 同一瞬间本方法若撞上 BUSY 就会静默回
        # (0,0)（客户端会把「最前一条数据」显示成 1970 年）。⇒ 重试。
        # （WAL 模式下读不与写者抢写锁 ⇒ 实际上极难撞上；重试只是兜底。）
        for _ in range(3):
            try:
                first = conn.execute(
                    "SELECT ts FROM history WHERE ts >= ? "
                    "ORDER BY ts ASC LIMIT 1",
                    (self.VALID_TS_MIN,)).fetchone()
                last = conn.execute(
                    "SELECT ts FROM history WHERE ts >= ? "
                    "ORDER BY ts DESC LIMIT 1",
                    (self.VALID_TS_MIN,)).fetchone()
                break
            except sqlite3.OperationalError as exc:
                msg = str(exc).lower()
                if "locked" not in msg and "busy" not in msg:
                    return 0, 0
                time.sleep(0.002)
            except sqlite3.Error:
                return 0, 0
        else:
            return 0, 0
        if not first or not last:
            return 0, 0
        return (int(first[0] or 0), int(last[0] or 0))

    def count_history(self, lo_ms=None, hi_ms=None, before_ms=None,
                      kw=None, kw_cols=None):
        """满足条件的总行数（分页总数）。库未开 = 0。"""
        where, params = self._history_where(lo_ms, hi_ms, before_ms,
                                            kw, kw_cols)
        conn = self._reader_conn()
        if conn is None:
            return 0
        try:
            with self._reader_lock:
                return int(conn.execute(
                    "SELECT COUNT(*) FROM history" + where,
                    params).fetchone()[0])
        except sqlite3.Error:
            return 0

    def query_history_page(self, offset=0, limit=50, lo_ms=None, hi_ms=None,
                           before_ms=None, kw=None, kw_cols=None,
                           with_count=True):
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
        conn = self._reader_conn()
        if conn is None:
            return [], 0
        try:
            # [读连接 2026-11-14] 查询走**独立读连接** ⇒ ① 不与写连接
            # 互斥（写不会被慢查询卡住）；② 可以在**工作线程**里跑（见
            # app 的 asyncio.to_thread）⇒ 关键字全表扫描（12MB 库实测
            # 397ms）不再阻塞事件循环 ⇒ 自动控制应答不被顶掉。
            with self._reader_lock:
                rows = conn.execute(
                    "SELECT " + ",".join(self.HISTORY_COLUMNS)
                    + " FROM history" + where
                    + " ORDER BY ts DESC LIMIT ? OFFSET ?",
                    params + [int(limit), int(offset)]).fetchall()
                # [导出模式不数总数 2026-11-17] `COUNT(*)` 对**关键字**
                # 查询是一次完整全表扫（20 万行 / 16 个显示表达式实测
                # **747ms**）；导出每批都跑一次 ⇒ 单批 790ms 里 747ms 花在
                # "数总数"，66 分钟的绝大部分是这个。导出**不需要** total
                # （终止靠短批；客户端只用它写"命中 N 条"）⇒ 回 -1 表示
                # 未知。界面翻页（未带 export）照旧数。
                window = -1
                if with_count:
                    window = int(conn.execute(
                        "SELECT COUNT(*) FROM history" + where,
                        params).fetchone()[0])
        except sqlite3.Error:
            return [], 0
        return list(reversed(rows)), window

    def query_logs_page(self, offset=0, limit=50, lo_ms=None, hi_ms=None,
                        before_ms=None, kw=None, category=None,
                        with_count=True):
        """取一页运行日志（**ts 降序**，与历史分页同口径：offset=0 是最新）。

        [为什么需要] C 客户端的「日志」页走 0x0119（CMD_LOG_PAGE）；本服务端
        原先**没有实现该命令** ⇒ C 客户端日志页永远停在"读取中"、一条都
        看不到（跨端缺陷，2026-11-09 发现）。

        :param offset: 跳过多少行（按 ts 倒序数，0 = 最新那行）。
        :param limit:  本页最多多少行（调用方夹到协议上限）。
        :param before_ms: 分页窗口锚点（翻页钉在同一批数据上）。
        :param kw: 关键字（在事件文本**与类别**上匹配，与 C 端口径一致：
            C 端 storeBuildLogKwCondition 拼的是 text||' '||category）。
        :param category: 类别精确过滤（conn/ctrl/alarm/auto/auth；空/None
            = 不过滤，即"全部类别"）。老库未分类的行（category 为空串）
            不匹配任何具体类别，与 C 端 COALESCE 后等值比较的口径一致。
        :returns: (rows, window_total)。rows 是 [(ts, category, text)]，
            **页内按 ts 正序**（与 0x0118 历史分页、与 C 端
            `HsfServerStore_queryLogs` 同口径：SQL 侧 ORDER BY ts DESC 取页，
            再在内存里翻转 ⇒ 第 1 页 = 最新的一页，页内由旧到新）。
            调用方（两个客户端）把它当"一行一行铺表"用，不依赖倒序。
        """
        if self._conn is None:
            return [], 0
        where = []
        params = []
        if before_ms is not None:
            where.append("ts <= ?")
            params.append(int(before_ms))
        if lo_ms is not None:
            where.append("ts >= ?")
            params.append(int(lo_ms))
        if hi_ms is not None:
            where.append("ts <= ?")
            params.append(int(hi_ms))
        if kw:
            # 关键字同时匹配类别列（C 端口径：text||' '||category），
            # 否则"输入 conn 找不到连接类事件"——类别是用户看得见的一列，
            # 搜不到它等于筛选器少了一半能力。
            where.append("(COALESCE(text, '') || ' ' || COALESCE(category, ''))"
                         " LIKE ? ESCAPE '\\'")
            params.append("%" + str(kw).replace("\\", "\\\\")
                          .replace("%", "\\%").replace("_", "\\_") + "%")
        if category:
            where.append("COALESCE(category, '') = ?")
            params.append(str(category))
        clause = (" WHERE " + " AND ".join(where)) if where else ""
        conn = self._reader_conn()
        if conn is None:
            return [], 0
        try:
            # [读连接 2026-11-14] 同历史页：查询走独立读连接 + 可在工作
            # 线程里跑 ⇒ 慢查询不阻塞事件循环（自动控制应答不被顶掉）。
            with self._reader_lock:
                rows = conn.execute(
                    "SELECT ts, COALESCE(category, ''), text FROM logs" + clause
                    + " ORDER BY ts DESC, rowid DESC LIMIT ? OFFSET ?",
                    params + [int(limit), int(offset)]).fetchall()
                # [导出模式不数总数 2026-11-17] 同历史页：导出每批一次
                # COUNT 纯属浪费（日志表无关键字时很便宜，有关键字同样是
                # 全表扫）。-1 = 未知。
                window = -1
                if with_count:
                    window = int(conn.execute(
                        "SELECT COUNT(*) FROM logs" + clause,
                        params).fetchone()[0])
        except sqlite3.Error:
            return [], 0
        # [页内翻转成时间正序 2026-11-09 两端口径对齐] 与 C 端
        # `HsfServerStore_queryLogs` 尾部的「倒序取的 ⇒ 反转让输出时间正序」
        # 逐字同构，也与本文件 query_history_page 的 list(reversed(rows)) 一致。
        # 此前这里直接回 SQL 的降序 ⇒ 同一个客户端连 C 服务端与连本服务端
        # 看到的日志表是**反的**（同一个页面、同一批数据，顺序随服务端变）。
        return list(reversed(rows)), window
