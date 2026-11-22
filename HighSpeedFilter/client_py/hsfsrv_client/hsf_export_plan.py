# -*- coding: utf-8 -*-
"""数据三页「导出筛选全量」的**纯逻辑**助手：批次计划 + 逐批取数状态机。

[为什么单独成文件]
    表格是**分页**的：界面只显示当前页那几十行；而导出必须是**筛选命中的
    全部行**（用户口径 2026-11-11：「导出的数据要按筛选结果，不是当前表格
    显示的数据」）。直接导当前页 ⇒ 导出的行数比用户以为的少一个数量级，
    而且**看不出少了**（文件里就是"表格里的那些行"）。

    批次归属是契约级逻辑：批行数一旦与**页大小档位**（10/20/50）或**图表
    批行数**（50）撞键，在途应答会被认错来源。照 C 端 HsfExportPlan 的
    既定范式把它抽成不依赖 GUI 的纯函数 + 可测的状态机，回归锁逐条钉住。

[口径与 C 端一致（client/ui/pages/HsfExportPlan.h 同款）]
    · 批行数 40：与页大小 10/20/50、图表批 50 **两两不相交** ⇒ 取数期间
      用户翻页/改筛选时迟到的旧应答不会被当成导出批；
    · 逐批**连续**取（步长=批行数）⇒ 跨页不重不漏；导出不抽样；
    · 整轮钉**同一个 beforeTs 锚点**（每批各取"当下"会让窗口上界逐批
      增长 ⇒ 顺序取必重复/漏行）；
    · 结束判据 = 短批（got<limit）/空批/写满服务端报的总行数；
    · 超上限**显式截断**并在状态行注明（截断是唯一的省略方式，不许静默
      丢行）。

[不碰控件] 这里只做整数算术、请求编排与文件落盘，不碰任何 GUI
⇒ 可在无 GUI 的测试里直接调用（落盘那部分见文末 CsvStreamWriter /
write_xlsx）。
"""

#: 导出批行数（行）。必须与页大小档位（10/20/50）和图表批行数（50）
#: 两两不相交——见模块头注。
EXPORT_CHUNK = 40

#: 行缓存上限（行）。对齐 C 端 HSF_EXPORT_PLAN_MAX_ROWS：历史页 16 列
#: × 约 48B ≈ 0.8KB/行，12000 行 ≈ 9.5MB 峰值。超出**显式截断**。
EXPORT_MAX_ROWS = 12000


def export_finish_text(count, total, truncated=False, write_error="",
                       fallback=0, path="", extra_note=""):
    """导出收尾文案（**纯函数**：三页共用，测试直接驱动）。

    [为什么抽成函数 2026-11-17] 收尾判定原来散在每页 `_finished` 闭包里，
    三件事都容易写错且**测试够不着**（闭包不可直接调）：
      ① 命中 0 行不能说"已导出 0 条 → 某路径"（文件根本没生成）；
      ② XLSX 收尾失败（磁盘满/权限）必须**失败优先**——`_export_close_xlsx`
         先写了失败状态行，紧接着 `_finished` 无条件覆盖成"成功"，用户看到
         成功、文件却打不开；
      ③ 实际行数 < 服务端 total 说明导出途中库被裁剪/滚动（ring/rotate
         与导出无快照隔离），少的是**真少了**，要说明而不是装作导全了。

    :param count: 实际导出/取到的行数。
    :param total: 服务端回报的命中总数（0/未知时不做少行判断）。
    :param truncated: 是否触到 max_rows 上限被截断。
    :param write_error: 写出器收尾错误文本（空 = 无错）。
    :param fallback: 按原始值导出的行数（数值转换失败兜底）。
    :param path: 产物路径（成功文案里带）。
    :param extra_note: 调用方追加的说明（如报警页的内存上限提示）。
    :returns: 状态行文本。
    """
    if write_error:
        return "导出失败：%s" % write_error
    if int(count or 0) <= 0:
        return "筛选命中 0 行，未生成文件"
    note = ""
    if truncated:
        note += "，超出上限已截断"
    if fallback:
        note += "，%d 行按原始值导出" % fallback
    missing = int(total or 0) - int(count or 0)
    if missing > 0:
        note += "，%d 行因导出期间库被裁剪/滚动未取到" % missing
    note += extra_note or ""
    # [total 未知（-1/0）时不编造"命中 N 条" 2026-11-17] 导出模式下服务端
    # 刻意不数总数（那是每批一次全表扫，66 分钟的绝大部分）⇒ 只说已导出
    # 多少条，不说命中多少（把 -1 或 0 当命中数是撒谎）。
    if int(total or 0) <= 0:
        return "已导出 %d 条%s → %s" % (count, note, path)
    return "已导出 %d 条（命中 %d 条%s）→ %s" % (
        count, total, note, path)


def plan_batches(total, chunk=EXPORT_CHUNK, max_rows=EXPORT_MAX_ROWS):
    """规划一轮导出取数的批数与步进（**纯函数**，同 C 端口径）。

    :param total:  筛选命中的总行数（服务端答应的 total；<=0 ⇒ 0 批）。
    :param chunk:  每批行数（本轮实际下发的 limit；<=0 按 1 处理防除零）。
    :param max_rows: 行缓存上限（<=0 = 不设上限）。
    :returns: dict —— ``batches`` 批数 / ``step`` 偏移步进（= 批行数，
              逐批连续取）/ ``fetch_rows`` 实际取数行数 / ``truncated``
              是否因超上限被截断。

    [与图表计划的分界] 图表按目标点数**跳批抽样**铺满全区间；导出不
    抽样——必须逐批连续取回**每一行**，所以批数 = ceil(fetch_rows/chunk)。

    [截断口径] total 超上限时把取数行数钉在「max_rows 向下取整到整批」
    ⇒ 每批都取满 chunk 行、不产生只装一半的残批；上限比一批还小时至少
    取一批（否则一行都取不到，"截断"会退化成"没导出"）。
    """
    total = int(total or 0)
    chunk = int(chunk or 0) or 1
    if total <= 0:
        return {"batches": 0, "step": chunk, "fetch_rows": 0,
                "truncated": False}
    fetch_rows = total
    if max_rows > 0 and total > max_rows:
        fetch_rows = (max_rows // chunk) * chunk
        if fetch_rows <= 0:
            fetch_rows = chunk
    batches = (fetch_rows + chunk - 1) // chunk
    if batches < 1:
        batches = 1
    return {"batches": batches, "step": chunk, "fetch_rows": fetch_rows,
            "truncated": fetch_rows < total}


class BatchExport(object):
    """一轮「导出筛选全量」的逐批取数状态机（只做计数与编排，不碰控件）。

    [怎么用]
        runner = BatchExport(keep_rows=False, max_rows=0)   # CSV：边取边写
        runner.start(fetch=..., on_rows=..., on_finished=...)

        · ``fetch(offset, limit, on_done, on_error)``（页面提供）：发一批
          请求，应答到货调 ``on_done(doc)``（服务端解出来的应答字典）。
        · ``on_rows(batch_rows)``：**每批**到货时回调 —— CSV 走这里边取边
          写（不限行数）；XLSX 也可在这里累加（工作簿在收尾前都在内存，
          故有行数上限）。
        · ``on_finished(total, truncated, count)``：整轮结束（count = 实际
          交付行数；truncated = 因超上限被截断）。

    [为什么 CSV 不设上限] 导出的是"筛选命中的全部行"，而库里可能十几万
    行：全攒在内存里再写，既容易撑内存又会把 CSV 也截掉。边取边写就没有
    这个上限（C 端同款：CSV 不设限，只有 XLSX 有内存闸）。

    [为什么不在页面里各写一遍] 历史页/日志页的取数协议不同，但"逐批连续
    取、锚点钉住、短批收尾、超限截断"这套口径完全相同 ⇒ 一份实现、两处
    复用，改一处不会漏另一处。
    """

    def __init__(self, chunk=EXPORT_CHUNK, max_rows=EXPORT_MAX_ROWS,
                 keep_rows=True):
        self._chunk = max(1, int(chunk))
        self._max_rows = max(0, int(max_rows))
        self._keep_rows = bool(keep_rows)
        self._rows = []
        self._count = 0
        self._offset = 0
        self._total = 0
        self._truncated = False
        self._running = False
        self._fetch = None
        self._on_rows = None
        self._on_finished = None
        self._on_error = None
        self._on_progress = None

    # ---- 对外状态 ----

    @property
    def running(self):
        """本轮是否还在取数。"""
        return self._running

    @property
    def written(self):
        """已交付的行数（累加模式下也是内存里的行数）。"""
        return self._count

    @property
    def rows(self):
        """累加的行（keep_rows=False 时为空列表）。"""
        return self._rows

    @property
    def total(self):
        """服务端报的总行数（0 = 还不知道）。"""
        return self._total

    # ---- 生命周期 ----

    def start(self, fetch, on_finished, on_error, on_rows=None,
              on_progress=None):
        """发起整轮取数（从 offset=0 开始）。

        :param fetch: ``fetch(offset, limit, on_done, on_error)``。
        :param on_finished: ``on_finished(total, truncated, count)``。
        :param on_error: ``on_error(msg)``。
        :param on_rows: ``on_rows(batch_rows)``（可选，每批回调；CSV
                        边取边写用它）。
        :param on_progress: ``on_progress(written, total)``（可选，进度行）。
        """
        self._fetch = fetch
        self._on_finished = on_finished
        self._on_error = on_error
        self._on_rows = on_rows
        self._on_progress = on_progress
        self._rows = []
        self._count = 0
        self._offset = 0
        self._total = 0
        self._truncated = False
        self._running = True
        self._request(0)

    def abort(self):
        """放弃本轮（页面销毁/再次发起时用；不再回调）。"""
        self._running = False

    # ---- 内部 ----

    def _request(self, offset):
        if not self._running or self._fetch is None:
            return
        try:
            self._fetch(offset, self._chunk, self._on_batch, self._fail)
        except Exception as exc:                        # noqa: BLE001
            self._fail("%s: %s" % (type(exc).__name__, exc))

    def _fail(self, msg):
        self._running = False
        if self._on_error:
            self._on_error(str(msg))

    def _on_batch(self, doc):
        """一批应答到货：交付这一批 → 判界 → 取下一批 / 收尾。"""
        if not self._running:
            return
        if not isinstance(doc, dict):
            self._fail("导出失败：应答格式不对")
            return
        if not doc.get("ok"):
            self._fail("导出失败：%s" % (doc.get("err") or "服务端拒绝"))
            return
        rows = doc.get("rows") or []
        try:
            total = int(doc.get("total") or 0)
        except (TypeError, ValueError):
            total = 0
        if self._total <= 0 and total > 0:
            self._total = total
        # [上限闸] 超上限就截断（XLSX 的内存闸；CSV 不设上限 ⇒ max_rows=0）。
        take = rows
        if self._max_rows > 0:
            room = self._max_rows - self._count
            if room <= 0:
                self._truncated = True
                take = []
            elif len(rows) > room:
                take = rows[:room]
                self._truncated = True
        if take:
            self._count += len(take)
            if self._keep_rows:
                self._rows.extend(take)
            if self._on_rows:
                try:
                    self._on_rows(take)
                except Exception as exc:                # noqa: BLE001
                    self._fail("导出失败：写文件失败（%s）"
                               % (type(exc).__name__,))
                    return
        got = len(rows)
        # [结束判据] 短批 = 服务端在这个 offset 已到数据尾；写满总数 = 取完。
        if (self._truncated or got <= 0 or got < self._chunk
                or (self._total > 0 and self._count >= self._total)):
            self._finish()
            return
        if self._on_progress:
            self._on_progress(self._count, self._total)
        # [连续取] 步长 = 本批实到行数（不是 chunk）⇒ 服务端少给几行时
        # 也不会跳行/重复。
        self._offset += got
        self._request(self._offset)

    def _finish(self):
        self._running = False
        if self._on_finished:
            self._on_finished(self._total, self._truncated, self._count)


# ==========================================================================
#                          落盘助手（CSV 流式 / XLSX 一次性）
# ==========================================================================


class CsvStreamWriter(object):
    """CSV 流式写出器（utf-8-sig，Excel 双击直开）。

    [为什么要流式] 导出的是"筛选命中的全部行"，库里可能十几万行：全攒在
    内存里再写，既容易撑内存，也会给 CSV 也套上一个行数上限 ⇒ 导出"看着
    完整、其实少了一截"。流式写就没有这个问题。

    [用法] 写文件 → write_rows(一批) → … → close()。
    """

    def __init__(self, path, headers):
        import csv as _csv
        self._csv = _csv
        self.path = path
        self._fh = open(path, "w", encoding="utf-8-sig", newline="")
        self._writer = _csv.writer(self._fh)
        self._writer.writerow(list(headers))
        self.rows = 0

    def write_rows(self, rows):
        """写一批行（每行是显示文本列表；缺列补空串）。"""
        for texts in rows:
            self._writer.writerow(
                [("" if texts is None else
                  ("" if c >= len(texts) or texts[c] is None else texts[c]))
                 for c in range(len(texts))])
            self.rows += 1

    def close(self):
        """关文件（重复调用安全）。"""
        if self._fh is not None:
            try:
                self._fh.close()
            finally:
                self._fh = None


def write_xlsx(path, sheet, headers, rows, col_widths=None):
    """把行集写成 XLSX（走 xlsx_bridge 动态库）。

    :returns: (ok: bool, err: str) —— 库缺失/写入失败都**显式报错**，不
              静默降级成 CSV（"以为是 Excel 其实是 CSV"比直接报错更误事）。
    """
    try:
        from . import xlsx_writer
    except ImportError:
        return False, "Excel 导出需要 xlsx_bridge 动态库（tools/xlsx_bridge/build.bat），CSV 导出不受影响"
    if xlsx_writer is None or not xlsx_writer.available():
        return False, "Excel 导出需要 xlsx_bridge 动态库（tools/xlsx_bridge/build.bat），CSV 导出不受影响"
    data = [list(r) for r in rows]
    try:
        xlsx_writer.write_rows(path, sheet, list(headers), data,
                               col_widths or {0: 10})
    except Exception as exc:                         # noqa: BLE001
        return False, "Excel 导出失败：%s" % exc
    return True, ""
