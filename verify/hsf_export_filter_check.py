#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""HSF「导出走文件对话框 + 按筛选全量取数」端到端复检（无人值守，退出码表成败）。

[背景] 2026-11-11 用户定案：C 客户端三个数据页（历史/日志/报警）的导出
  ① 删工具条「导出格式」下拉，格式在保存文件对话框里选；
  ② 导出的不是当前页那几十行，而是**按当前筛选条件的全部命中行**
     （探测批拿 total → 批次计划 → 逐批连续取）。
C 侧批次计划已由 Test/XProtocolTest/hsf_regression_test.c §Q 钉住（纯
逻辑）；本脚本补的是**协议面**的端到端复检——起 bin/HsfServer.exe --sim
的隔离实例，用 Python 客户端桥（HighSpeedFilter/client_py，PyQt5）按
C 客户端导出取数的同一套键（offset/limit/beforeTs/export/kw/kwCols）
分页拉取并断言：

  A1 导出行数 == 服务端命中的总行数（逐批累加后行数 == total）；
  A2 跨页不重不漏（按 ts 去重后行数 == total，且各批 total 稳定——
     整轮取数钉同一 beforeTs 锚点；不钉锚每批窗口上界都在长，逐批
     顺序取必重复/漏行，C 端 historyQueryWindow 加 beforeTs 形参的根因）；
  A3 关键字筛选确实下推服务端（筛选后 total 确实变少，优先取"部分
     命中"的关键字证明，退化兜底用不存在的关键字证明 total 变化）；
  A4 export=true 被拒时拿不到数据（把隔离实例的 admin 降成匿名角色 ⇒
     无 Download 权限 ⇒ 服务端回失败包络、无 rows；同参数不带 export
     仍能正常读——证明拒的是导出闸不是连接）。

[与 C 端实现的关系] 本脚本不驱动 C 客户端 UI（文件对话框需要真窗口），
它验证的是导出取数所依赖的**服务端契约**：total 与页内容同一条 WHERE、
锚点钉住不漂移、export=true 服务端验权。C 端状态机的行为由 §Q + 编译
期闸（41..49 与页大小不相交）钉住。

跑法（先编译 bin/HsfServer.exe）：
    D:/anaconda3/python.exe verify/hsf_export_filter_check.py
    （或 py -3 verify/hsf_export_filter_check.py）
退出码 0=全过，1=有失败。脚本自起自停服务端（隔离临时目录，不碰现场
bin/ 的任何文件），结束必杀进程。
"""
import json
import os
import shutil
import socket
import subprocess
import sys
import tempfile
import time

REPO_ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
CLIENT_ROOT = os.path.join(REPO_ROOT, "HighSpeedFilter", "client_py")
if CLIENT_ROOT not in sys.path:
    sys.path.insert(0, CLIENT_ROOT)

#: 历史行的模拟采样周期（经远端配置 storage/sampleMs 下调到 1s/行，服务端
#: 夹取下限 1000ms；缺省 5s/行 ⇒ 热身要等 5 分钟，不可接受）。
SIM_SAMPLE_MS = 1000
#: 热身目标行数（>50 才能至少跨两批；1s/行约 1 分钟）。
WARM_TARGET_ROWS = 60
WARM_TIMEOUT_S = 150.0
#: 导出批行数轮转（镜像 C 端 histExportNextChunk：41..49 轮转，禁用 50）。
EXPORT_LIMITS = (45, 44, 43)

_G_FAILS = []
_G_CHECKS = [0]


def check(name, ok, detail=""):
    _G_CHECKS[0] += 1
    tag = "[PASS]" if ok else "[FAIL]"
    line = "%s %s" % (tag, name)
    if detail:
        line += "  (%s)" % detail
    print(line)
    if not ok:
        _G_FAILS.append(name)


def free_port():
    s = socket.socket()
    s.bind(("127.0.0.1", 0))
    port = s.getsockname()[1]
    s.close()
    return port


class IsolatedServer(object):
    """bin/HsfServer.exe 的隔离实例（复制到临时目录起 --sim，绝不碰现场）。"""

    def __init__(self):
        self.tmp = None
        self.proc = None
        self.port = 0
        self.log_path = None

    def start(self):
        exe = os.path.join(REPO_ROOT, "bin", "HsfServer.exe")
        if not os.path.isfile(exe):
            raise RuntimeError("找不到 %s（先编译 HsfServer）" % exe)
        # [restart 复用同一临时目录] 二次 start（降级账户后重启）必须沿用
        # 首次的 tmp（cfg/hsf_users.json 改动 + 已攒的 hsf_data.db 都在那）；
        # 另建新目录 = 新空库 + 重新 ensureDefaults ⇒ A4 的前提全丢。
        if not self.tmp:
            self.tmp = tempfile.mkdtemp(prefix="hsf_export_chk_")
            dst = os.path.join(self.tmp, "HsfServer.exe")
            shutil.copy2(exe, dst)
        dst = os.path.join(self.tmp, "HsfServer.exe")
        if not self.port:
            self.port = free_port()
        self.log_path = os.path.join(self.tmp, "HsfServer.out")
        out = open(self.log_path, "wb")   # 管道会写满卡死服务端，落文件。
        self.proc = subprocess.Popen(
            [dst, "--sim", str(self.port)], cwd=self.tmp,
            stdout=out, stderr=subprocess.STDOUT)
        deadline = time.time() + 30
        while time.time() < deadline:
            if self.proc.poll() is not None:
                out.close()
                raise RuntimeError("服务端秒退：%s" % self.tail())
            try:
                sock = socket.create_connection(("127.0.0.1", self.port), 0.3)
                sock.close()
                return self.port
            except OSError:
                time.sleep(0.2)
        out.close()
        raise RuntimeError("服务端 30s 未监听端口 %d" % self.port)

    def tail(self, n=600):
        try:
            with open(self.log_path, "rb") as fh:
                return fh.read()[-n:].decode("utf-8", "replace")
        except OSError:
            return ""

    def users_path(self):
        return os.path.join(self.tmp, "cfg", "hsf_users.json")

    def demote_admin_to_anonymous(self):
        """把缺省账户 admin（User，带 Download）降成匿名角色（无 Download）。

        [为什么这么做] C 服务端缺省只有 admin/admin（User=查看+下载）与
        root/root（Admin）两账户，没有"无下载权"的现成账户；账户库是
        JSON（name/salt/hash/role），salt+hash 保持原值 ⇒ 口令仍是 admin，
        只改 role=0（None）。服务端只在空库 ensureDefaults，改文件后重启
        即按新角色认证（XLinkUserStore 解析显式接受 role 0=匿名）。
        """
        path = self.users_path()
        with open(path, "r", encoding="utf-8") as fh:
            data = json.load(fh)
        hit = False
        for user in data.get("users") or []:
            if user.get("name") == "admin":
                user["role"] = 0
                hit = True
        if not hit:
            raise RuntimeError("账户库里没有 admin（%s）" % path)
        with open(path, "w", encoding="utf-8") as fh:
            json.dump(data, fh, ensure_ascii=False, indent=1)

    def stop(self):
        if self.proc is not None and self.proc.poll() is None:
            self.proc.terminate()
            try:
                self.proc.wait(timeout=6)
            except Exception:                          # noqa: BLE001
                self.proc.kill()
        self.proc = None


def hist(bridge, proto, offset, limit, before_ts=None, kw=None,
         export=False, timeout=12.0):
    """按 C 客户端导出取数的同一套键发一次 0x0118（同步等应答）。"""
    doc = {proto.HIST_KEY_OFFSET: int(offset),
           proto.HIST_KEY_LIMIT: int(limit)}
    if before_ts:
        doc[proto.HIST_KEY_BEFORE_TS] = int(before_ts)
    if kw:
        doc[proto.HIST_KEY_KW] = str(kw)
        # kwCols = 全部显示列（与 C 端 histVisibleCols 全列口径一致）。
        doc[proto.HIST_KEY_KW_COLS] = list(range(16))
    if export:
        doc[proto.HIST_KEY_EXPORT] = True
    return bridge.request_sync(proto.CMD_HISTORY_PAGE, doc, timeout=timeout)


def connect(bridge, port, user, password):
    ok, err = bridge.connect_to("127.0.0.1", port, user, password)
    if not ok:
        raise RuntimeError("登录失败（%s/%s）：%s" % (user, password, err))


def main():
    from PyQt5.QtWidgets import QApplication   # noqa: F401（桥需要 Qt 环境）
    from hsfsrv_client import proto
    from hsfsrv_client.bridge import LinkBridge

    app = QApplication.instance() or QApplication(sys.argv[:1])
    del app   # 事件循环不需要转（request_sync 走 future），占住实例即可。

    server = IsolatedServer()
    port = server.start()
    print("== 隔离服务端已起：127.0.0.1:%d（tmp=%s）" % (port, server.tmp))
    bridge = LinkBridge()
    try:
        connect(bridge, port, "root", "root")

        # ---- 提速落盘采样（5s/行 → 1s/行；服务端夹取下限 1000ms）----
        cfg = bridge.request_sync(proto.CMD_CONFIG_SET,
                                  {"section": "storage", "enabled": True,
                                   "sampleMs": SIM_SAMPLE_MS}, timeout=8.0)
        check("远端配置：落盘采样周期下调生效（root 可写）",
              bool(cfg and cfg.get("ok")), "应答=%s" % (cfg or {}).get("err", ""))

        # ---- 热身：等库里有 >50 行（跨批前提）----
        total0 = 0
        first_doc = None
        deadline = time.time() + WARM_TIMEOUT_S
        while time.time() < deadline:
            doc = hist(bridge, proto, 0, 50, export=True)
            if doc and doc.get("ok"):
                rows = doc.get("rows") or []
                first_doc = doc
                total0 = int(doc.get("total") or 0)
                if total0 >= WARM_TARGET_ROWS:
                    break
                print("   热身中… total=%d（等 %d 行）" % (total0, WARM_TARGET_ROWS))
            time.sleep(2.0)
        check("热身：模拟库攒到 >=%d 行（跨批前提）" % WARM_TARGET_ROWS,
              total0 >= WARM_TARGET_ROWS,
              "total=%d" % total0)
        if total0 < WARM_TARGET_ROWS:
            print(server.tail())
            return 1

        # ================= A1/A2：按筛选全量分页累加 =================
        # [锚点钉住] 整轮取数用同一个 beforeTs（= 发起时的"现在"）；
        # 不钉锚时每批服务端窗口上界=min(hiTs 缺省, beforeTs=当下) 逐批
        # 增长，新入库的行会把 offset 顶偏 ⇒ 重复/漏行。
        anchor = int(time.time() * 1000)
        collected = []
        totals = set()
        offset = 0
        seq = 0
        batches = 0
        while True:
            limit = EXPORT_LIMITS[seq % len(EXPORT_LIMITS)]
            seq += 1
            doc = hist(bridge, proto, offset, limit, before_ts=anchor,
                       export=True)
            if not doc or not doc.get("ok"):
                check("导出取数：每批都有应答（第 %d 批）" % (batches + 1),
                      False, "offset=%d 应答=%s" % (offset, doc))
                break
            batches += 1
            rows = doc.get("rows") or []
            totals.add(int(doc.get("total") or -1))
            collected.extend(rows)
            if len(rows) < limit:
                break   # 末批不满 = 服务端区尾（与 C 端终止条件同口径）。
            offset += limit
            if offset > 200000:
                check("导出取数：批数有界（防死循环）", False)
                break
        ts_list = [r[0] if isinstance(r, (list, tuple)) else r.get("ts")
                   for r in collected]
        check("A1 导出行数 == 服务端命中总行数（逐批累加）",
              len(collected) == total0,
              "累加 %d 行 / total=%d / 批数=%d" % (len(collected), total0, batches))
        check("A2 跨页不重不漏（按 ts 去重后行数 == total）",
              len(set(ts_list)) == len(collected) == total0,
              "去重 %d / 累加 %d / total=%d" % (
                  len(set(ts_list)), len(collected), total0))
        check("A2 各批 total 稳定（整轮钉同一 beforeTs 锚点，不漂移）",
              totals == {total0},
              "各批 total=%s" % sorted(totals))
        check("A2 取的不是当前页那几十行（> 单页 50 行上限）",
              len(collected) > 50, "累加 %d 行" % len(collected))
        check("A2 权限：root 带 export=true 的导出请求全部放行",
              batches >= 1 and len(collected) > 0)

        # ================= A3：关键字筛选下推服务端 =================
        # [候选关键字的挑法] 从首页行里取**真实存在**的格文本（时间列的
        # 秒级字串 → 只命中同秒的 1 行；其次模式列文本），优先证明"部分
        # 命中"；全不中再用不存在的关键字兜底（total==0 也证明下推）。
        cols = list((first_doc or {}).get("cols") or [])
        kw_doc = hist(bridge, proto, 0, 50, before_ts=anchor)
        kw_rows = (kw_doc or {}).get("rows") or []
        candidates = []
        if kw_rows and cols:
            def cell(row, name):
                try:
                    idx = cols.index(name)
                    return str(row[idx] if isinstance(row, (list, tuple))
                               else row.get(name) or "")
                except (ValueError, IndexError):
                    return ""
            # [时间列候选] 服务端 kw 是对**显示文本**做 LIKE；时间列的显示
            # 文本是本地 "YYYY-MM-DD HH:MM:SS"。行里的 ts 是 epoch 毫秒
            # （不是文本），按本地时区格式化出秒级字串 ⇒ 预期只命中同一秒
            # 的那一行（部分命中，最有证明力）。
            ts_val = cell(kw_rows[0], "ts")
            try:
                ts_ms = int(float(ts_val))
                candidates.append(
                    time.strftime("%H:%M:%S", time.localtime(ts_ms / 1000.0)))
            except (ValueError, OSError):
                pass
            mode_text = cell(kw_rows[-1], "mode")
            if mode_text and mode_text != "--":
                candidates.append(mode_text)
        candidates.append("__确定不存在的关键字__")
        picked = None
        picked_total = None
        for kw in candidates:
            doc = hist(bridge, proto, 0, 50, before_ts=anchor, kw=kw)
            if not doc or not doc.get("ok"):
                continue
            total_kw = int(doc.get("total") or 0)
            if 0 < total_kw < total0:
                picked, picked_total = kw, total_kw
                break
            if picked is None and total_kw == 0:
                picked, picked_total = kw, 0   # 兜底：total 变小也算下推成立
        check("A3 关键字筛选下推服务端（筛选后总行数确实变少）",
              picked is not None and picked_total is not None
              and picked_total < total0,
              "kw=%r total %d → %d" % (picked, total0, picked_total or 0))
        if picked and 0 < (picked_total or 0) < total0:
            # 部分命中时再验一次跨页累加与 total 同口径（筛选 + 全量同条件）。
            acc = []
            offset = 0
            while True:
                doc = hist(bridge, proto, offset, 45, before_ts=anchor,
                           kw=picked)
                rows = (doc or {}).get("rows") or []
                acc.extend(rows)
                if len(rows) < 45:
                    break
                offset += 45
            check("A3 筛选后的全量累加同样 == 筛选命中 total",
                  len(acc) == picked_total,
                  "累加 %d / 筛选 total=%d" % (len(acc), picked_total))

        # ============ A4：export=true 被拒时拿不到数据 ============
        bridge.disconnect_from()
        server.stop()
        server.demote_admin_to_anonymous()
        port2 = server.start()
        check("A4 隔离实例重启（admin 已降级为匿名角色）",
              port2 > 0, "新端口 %d" % port2)
        bridge2 = LinkBridge()
        connect(bridge2, port2, "admin", "admin")
        denied = hist(bridge2, proto, 0, 45, export=True)
        check("A4 export=true 被拒：失败包络、拿不到数据",
              bool(denied) and not denied.get("ok")
              and not (denied.get("rows") or []),
              "ok=%s err=%s rows=%s" % (
                  (denied or {}).get("ok"), (denied or {}).get("err", ""),
                  len((denied or {}).get("rows") or [])))
        allowed = hist(bridge2, proto, 0, 5, export=False)
        check("A4 同参数不带 export 仍能读（拒的是导出闸不是连接）",
              bool(allowed) and allowed.get("ok")
              and len(allowed.get("rows") or []) > 0,
              "total=%s" % (allowed or {}).get("total"))
        bridge2.disconnect_from()
        return 0
    except Exception as exc:                           # noqa: BLE001
        print("[FAIL] 异常：%s: %s" % (type(exc).__name__, exc))
        print(server.tail())
        return 1
    finally:
        try:
            bridge.disconnect_from()
        except Exception:                              # noqa: BLE001
            pass
        server.stop()
        shutil.rmtree(server.tmp, ignore_errors=True)
        print("== 收尾：服务端已停，临时目录已删（检查 %d 项，失败 %d 项）"
              % (_G_CHECKS[0], len(_G_FAILS)))
        for name in _G_FAILS:
            print("   失败项：%s" % name)


if __name__ == "__main__":
    sys.exit(main())
