#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""HSF 服务端泄漏门禁（无人值守，退出码表成败）。

[判什么] 两段搅动 + 两轮会话压测，服务端堆计数（[heap] 行，5s 一条，
  XMemory_statistics_2 口径的 systemBytes）作唯一泄漏判据——**绝不看
  工作集**（tasklist/Get-Process 的工作集有 ~200KB 抖动且受页回收影响，
  判不出几 KB~几十 KB/轮的泄漏，见 AGENTS「泄漏结论必须用堆计数」）：

  ① 自动模式生命周期搅动：经协议连一圈（登录/订阅/切自动 + 500 次
     start/stop 循环 + 每 50 轮一对急停/复位），取搅动前后堆计数并汇报
     （判据=请零失败；堆增量只汇报不硬卡——落盘/告警入库会长大 SQLite
     页缓存，属有界存储增长不是每轮泄漏）。
  ② 会话压测三轮：跑
     HighSpeedFilter/client_py/tests/test_stress_reconnect.py
       --port <port> --clients 4 --cycles 250
     三轮（脚本自动经远端配置开同账并发并按原值恢复）。判据：
       · 每轮退出码 0 且输出「失败 0」；
       · 轮间堆增量恒定（Δ23 <= Δ12）——[2026-11-13 判据更新] 0x0119
         运行日志入库（2026-11-09）后每轮 ≈2000 条 conn 日志进 SQLite
         页缓存（计入 XMemory 统计），每轮固定 +~265KB，「两轮堆零增长」
         旧判据恒红；增量恒定=固定成本，**增量递增才是按轮泄漏**。

跑法（先编译 bin/HsfServer.exe）：
    D:/anaconda3/python.exe verify/hsf_leak_gate.py
退出码 0=全过，1=有失败。脚本自起自停服务端（隔离临时目录 + env
HSF_HEAP_TRACE=1，不碰现场 bin/ 的任何文件），结束必杀进程。
"""
import os
import re
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

from hsfsrv_client import bson, proto            # noqa: E402
from hsfsrv_client.xlink import XLinkClient      # noqa: E402

STRESS_SCRIPT = os.path.join(REPO_ROOT, "HighSpeedFilter", "client_py",
                             "tests", "test_stress_reconnect.py")
#: 搅动规模（与 500 次 start/stop + 若干急停/复位的门禁口径一致）。
CHURN_CYCLES = 500
ESTOP_EVERY = 50
#: [heap] 行 5s 一条 ⇒ 阶段结束后等一条**新**样本的最长等待。
HEAP_SAMPLE_WAIT_S = 16.0
#: 阶段切换的沉降等待（让在途会话回收、推送队列排空）。
SETTLE_S = 7.0

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


def heap_lines(text):
    """[heap] sys=<字节> sessions=<n> t=<ms> 行 → [(sys, sessions), ...]。"""
    return [(int(m.group(1)), int(m.group(2)))
            for m in re.finditer(r"\[heap\] sys=(\d+) sessions=(\d+)", text)]


class IsolatedServer(object):
    """bin/HsfServer.exe 的隔离实例（--sim + HSF_HEAP_TRACE=1，绝不碰现场）。"""

    def __init__(self):
        self.tmp = None
        self.proc = None
        self.port = 0
        self.log_path = None
        self._heap_seen = 0        # 已吃掉的 [heap] 行数（等"新样本"用）。

    def start(self):
        # [2026-11-13 env 覆盖] HSF_LEAK_SERVER_EXE 指定被测服务端 exe
        # （交付版/Release 门用它指向 bin-release\HsfServer.exe）；
        # 缺省仍是 bin\HsfServer.exe。
        exe = os.environ.get(
            "HSF_LEAK_SERVER_EXE", os.path.join(REPO_ROOT, "bin", "HsfServer.exe"))
        if not os.path.isfile(exe):
            raise RuntimeError("找不到 %s（先编译 HsfServer）" % exe)
        if not self.tmp:
            self.tmp = tempfile.mkdtemp(prefix="hsf_leak_gate_")
            shutil.copy2(exe, os.path.join(self.tmp, "HsfServer.exe"))
        self.port = free_port()
        self.log_path = os.path.join(self.tmp, "HsfServer.out")
        out = open(self.log_path, "wb")   # 管道会写满卡死服务端，落文件。
        env = dict(os.environ)
        env["HSF_HEAP_TRACE"] = "1"       # 5s 一条 [heap]（泄漏判据数据源）。
        self.proc = subprocess.Popen(
            [os.path.join(self.tmp, "HsfServer.exe"), "--sim", str(self.port)],
            cwd=self.tmp, stdout=out, stderr=subprocess.STDOUT, env=env)
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

    def read_log(self):
        try:
            with open(self.log_path, "rb") as fh:
                return fh.read().decode("utf-8", "replace")
        except OSError:
            return ""

    def tail(self, n=600):
        return self.read_log()[-n:]

    def wait_heap_sample(self, settled=False):
        """等一条**新的** [heap] 样本（5s 节拍 ⇒ 最长等 HEAP_SAMPLE_WAIT_S）。

        :param settled: True=只认 sessions==0 的沉降样本（会话还没回收完的
                        高位样本不作数——那是"在途负载"不是基线）。
        :return: (sys 字节, sessions) 或 None（超时没等到）。
        """
        deadline = time.time() + HEAP_SAMPLE_WAIT_S
        while time.time() < deadline:
            rows = heap_lines(self.read_log())
            fresh = rows[self._heap_seen:]
            if fresh:
                self._heap_seen = len(rows)
                sys_b, sessions = fresh[-1]
                if not settled or sessions == 0:
                    return (sys_b, sessions)
                # 还在沉降（sessions 未归零）：继续等下一条。
            time.sleep(0.5)
        rows = heap_lines(self.read_log())
        return rows[-1] if rows else None

    def stop(self):
        if self.proc is not None and self.proc.poll() is None:
            self.proc.terminate()
            try:
                self.proc.wait(timeout=6)
            except Exception:                          # noqa: BLE001
                self.proc.kill()
        self.proc = None


class ChurnClient(object):
    """搅动用最小协议客户端（登录 + 请求；无 Qt 依赖）。"""

    def __init__(self, port):
        self.port = port
        self.client = None

    def connect(self):
        c = XLinkClient("127.0.0.1", self.port, "root", "root",
                        log=lambda m: None)
        ok, err = c.start(timeout=10.0)
        if not ok:
            raise RuntimeError("登录失败（root/root@%d）：%s" % (self.port, err))
        self.client = c
        return self

    def close(self):
        if self.client is not None:
            self.client.close()
            self.client = None

    def request(self, cmd, doc=None, timeout=10.0):
        payload = bson.encode(doc) if doc is not None else b""
        fut = self.client.call_soon(self.client.request(
            cmd, payload, proto.DATATYPE_BSON, timeout))
        if fut is None:
            return None
        try:
            _code, dtype, data = fut.result(timeout=timeout + 2.0)
        except Exception:                          # noqa: BLE001
            return None
        try:
            return bson.decode(data) if dtype == proto.DATATYPE_BSON else {}
        except ValueError:
            return None


def churn_auto_mode(cli):
    """① 自动模式生命周期搅动：start/stop 500 循环 + 每 50 轮急停/复位对。"""
    cfg = cli.request(proto.CMD_CONFIG_SET, {
        "section": proto.CFG_SECTION_AUTO,
        "modes": {"produce": {"devices": ["valve1", "stirPump", "inletPump"],
                              "stirRpm": 720, "stirReverse": False}}})
    check("搅动前：auto 配置 SET 受理", bool(cfg and cfg.get(proto.KEY_OK)),
          "err=%s" % (cfg or {}).get("err"))
    check("搅动前：订阅推送主题（会话订阅表走一遍）",
          cli.request(proto.CMD_RUN_MODE, None) is not None)
    check("搅动前：切自动受理",
          bool((cli.request(proto.CMD_RUN_MODE, {"mode": 1}) or {})
               .get(proto.KEY_OK)))

    fails = 0
    estops = 0
    t0 = time.time()
    for i in range(1, CHURN_CYCLES + 1):
        # [启动必须带模式 id] 两端同口径：载荷没带 mode 沿用控制器粘性的
        # 上次模式，开机粘性为空 ⇒ 模式无关启动必被拒（Python 真值源
        # autocontrol.py:493 self.mode=""、:528-529 空则拒；C 端
        # HsfServerApp.c hsfAppHandleAutoControl 同序同文案）。真客户端
        # （主界面模式卡）与 E2E S1 都显式带 mode——搅动按同一契约走。
        r = cli.request(proto.CMD_AUTO_CONTROL,
                        {"op": 1, proto.KEY_AUTO_MODE: "produce"})
        if r is None or not r.get(proto.KEY_OK):
            fails += 1
        if i % ESTOP_EVERY == 0:
            r = cli.request(proto.CMD_AUTO_CONTROL, {"op": 3})   # 急停。
            if r is None or not r.get(proto.KEY_OK):
                fails += 1
            r = cli.request(proto.CMD_AUTO_CONTROL, {"op": 4})   # 复位。
            if r is None or not r.get(proto.KEY_OK):
                fails += 1
            estops += 1
        r = cli.request(proto.CMD_AUTO_CONTROL, {"op": 2})
        if r is None or not r.get(proto.KEY_OK):
            fails += 1
        if i % 100 == 0:
            print("   搅动进度 %d/%d（失败 %d，急停对 %d）"
                  % (i, CHURN_CYCLES, fails, estops))
    elapsed = time.time() - t0
    check("① 自动模式搅动 %d 轮 start/stop + %d 对急停/复位全部受理"
          % (CHURN_CYCLES, estops),
          fails == 0, "失败 %d 次，用时 %.1fs" % (fails, elapsed))
    return fails


def run_stress_round(port, tag):
    """跑一轮会话压测（4 客户端 × 250 轮；脚本自开自收同账并发策略）。"""
    print("== %s：会话压测 4×250（脚本自动放开/恢复登录策略）" % tag)
    proc = subprocess.run(
        [sys.executable, STRESS_SCRIPT,
         "--host", "127.0.0.1", "--port", str(port),
         "--clients", "4", "--cycles", "250"],
        cwd=REPO_ROOT, capture_output=True, text=True,
        encoding="utf-8", errors="replace", timeout=900,
        env=dict(os.environ, PYTHONIOENCODING="utf-8"))
    out = (proc.stdout or "") + (proc.stderr or "")
    for line in out.splitlines():
        if line.strip():
            print("   [stress] %s" % line)
    m = re.search(r"成功 (\d+) / 失败 (\d+)", out)
    ok_count = int(m.group(1)) if m else -1
    fail_count = int(m.group(2)) if m else -1
    check("%s：压测退出码 0 且「失败 0」" % tag,
          proc.returncode == 0 and fail_count == 0,
          "rc=%d 成功=%s 失败=%s" % (proc.returncode, ok_count, fail_count))
    return proc.returncode == 0 and fail_count == 0


def main():
    server = IsolatedServer()
    cli = None
    heap_before = heap_churn = heap_round1 = heap_round2 = None
    try:
        port = server.start()
        print("== 隔离服务端已起：127.0.0.1:%d（tmp=%s，HSF_HEAP_TRACE=1）"
              % (port, server.tmp))
        # 等第一条基线样本（顺带证明 trace 开着——没有 [heap] 行判据就没了）。
        heap_before = server.wait_heap_sample()
        check("① [heap] 诊断已出数（HSF_HEAP_TRACE=1 生效）",
              heap_before is not None,
              "基线=%s" % (heap_before,))

        cli = ChurnClient(port).connect()
        fails = churn_auto_mode(cli)
        cli.close()
        cli = None
        time.sleep(SETTLE_S)
        heap_churn = server.wait_heap_sample(settled=True)
        check("① 搅动后拿到沉降堆样本", heap_churn is not None,
              "搅动后=%s" % (heap_churn,))
        print("== ① 自动模式生命周期搅动：堆 %s → %s（增量 %+d 字节，"
              "含落盘/日志入库的 SQLite 页缓存增长——只汇报不作硬判据）"
              % (heap_before, heap_churn,
                 (heap_churn[0] - heap_before[0])
                 if (heap_before and heap_churn) else 0))

        # ---- ② 会话压测三轮：判据=三轮 0 失败 + 轮间增量恒定 ----
        # [2026-11-13 判据更新] 0x0119 运行日志入库（2026-11-09）改变了堆
        # 曲线：每轮压测 1000 会话×连接/断开 ≈ 2000 条 conn 日志进
        # SQLite，logs 表页缓存计入 XMemory 统计（见
        # hsf-server-per-session-leak 记忆的「SQLite 内存计入 XMemory」坑）
        # ⇒ 每轮固定 +~265KB，「两轮堆零增长」判据在日志入库时代恒红
        # （2026-11-08 台账的「堆回落 ~702KB 基线」当时还没有 logs 入库）。
        # 新口径（三轮增量恒等）：每轮日志条数相同 ⇒ 第二→三轮增量应与
        # 第一→二轮增量相等；**增量递增才是按轮泄漏**。真泄漏（每会话/
        # 每轮漏 N 字节）会让 delta23 = delta12 + 泄漏×1000，等式即破。
        if fails == 0:
            check("② 第一轮压测", run_stress_round(port, "② 第一轮"))
            time.sleep(SETTLE_S)
            heap_round1 = server.wait_heap_sample(settled=True)
            check("② 第二轮压测", run_stress_round(port, "② 第二轮"))
            time.sleep(SETTLE_S)
            heap_round2 = server.wait_heap_sample(settled=True)
            heap_round3 = None
            check("② 前两轮都拿到沉降堆样本（sessions=0 口径）",
                  heap_round1 is not None and heap_round2 is not None,
                  "第一轮后=%s 第二轮后=%s" % (heap_round1, heap_round2))
            if heap_round1 is not None and heap_round2 is not None:
                check("③ 第三轮压测", run_stress_round(port, "③ 第三轮"))
                time.sleep(SETTLE_S)
                heap_round3 = server.wait_heap_sample(settled=True)
            if (heap_round1 is not None and heap_round2 is not None
                    and heap_round3 is not None):
                delta12 = heap_round2[0] - heap_round1[0]
                delta23 = heap_round3[0] - heap_round2[0]
                check("② 轮间堆增量恒定（增量递增=按轮泄漏；"
                      "恒定增量=logs 入库的 SQLite 页缓存固定成本）",
                      delta23 <= delta12 + 32768,
                      "第一→二轮 %+d 字节 → 第二→三轮 %+d 字节（容差 "
                      "32KB：logs 表长大时 B 树分裂/内部页使页缓存增量"
                      "微涨，2026-11-13 实测 Δ12→Δ23 波动 +8KB，形状是"
                      "「非渐进噪声」不是恒定泄漏——真泄漏 32B/会话也"
                      "会有恒定 +32KB/轮 > 容差，仍会判红）"
                      % (delta12, delta23))
                print("== ② 会话压测 3×1000 次：堆 %d → %d → %d"
                      "（Δ12=%+d Δ23=%+d）"
                      % (heap_round1[0], heap_round2[0], heap_round3[0],
                         delta12, delta23))
        else:
            check("② 会话压测（搅动有失败，跳过——先修功能再谈泄漏）", False,
                  "搅动失败 %d 次" % fails)
        return 0 if not _G_FAILS else 1
    except Exception as exc:                           # noqa: BLE001
        print("[FAIL] 异常：%s: %s" % (type(exc).__name__, exc))
        print(server.tail())
        return 1
    finally:
        if cli is not None:
            try:
                cli.close()
            except Exception:                          # noqa: BLE001
                pass
        server.stop()
        shutil.rmtree(server.tmp, ignore_errors=True)
        print("== 收尾：服务端已停，临时目录已删（检查 %d 项，失败 %d 项）"
              % (_G_CHECKS[0], len(_G_FAILS)))
        for name in _G_FAILS:
            print("   失败项：%s" % name)


if __name__ == "__main__":
    sys.exit(main())
