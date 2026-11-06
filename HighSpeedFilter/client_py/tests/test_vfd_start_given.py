# -*- coding: utf-8 -*-
"""启动/点动携带给定的端到端测试（真服务端 + 真 XLink）。

    python tests/test_vfd_start_given.py

用户反馈：「点启动的时候也要下发频率，用下面设置的转速或频率来启动」
            「按住点动的频率也是」

根因（实测）：服务端禁止「停机时下发给定」，所以客户端原来那套
「先单独发一帧给定、再发启动」会被拒——启动按变频器里残留的旧给定跑
（界面显示 3Hz，实际 25Hz）。修法是把给定绑在启动/点动命令上，
服务端先写给定再发命令字。

本测试验证：
  A. 频率模式：设 3Hz → 启动 → 实际输出≈3Hz（不是残留的旧给定）
  B. 转速模式：设任意 rpm → 启动 → 实际输出频率 = rpm 换算值
  C. 换一个给定值再启动（验证"每次启动都按当前设定"）
  D. 点动也带给定：按住点动 → 实际输出≈当前设定，而不是旧给定
  E. 停机时单独下发给定仍被拒（原有安全语义不许回归）
"""

import os
import subprocess
import sys
import tempfile
import time

HERE = os.path.dirname(os.path.abspath(__file__))
CLIENT_ROOT = os.path.dirname(HERE)
SERVER_ROOT = os.path.join(os.path.dirname(CLIENT_ROOT), "server_py")
sys.path.insert(0, CLIENT_ROOT)

PASS = 0
FAIL = 0
PORT = 9155


def check(name, ok, detail=""):
    global PASS, FAIL
    if ok:
        PASS += 1
        print("  [PASS] %s %s" % (name, detail))
    else:
        FAIL += 1
        print("  [FAIL] %s %s" % (name, detail))


def wait(sec):
    end = time.time() + sec
    while time.time() < end:
        time.sleep(0.05)


class Server(object):
    def __init__(self):
        self.tmp = tempfile.mkdtemp(prefix="hsf_startgiven_")
        self.proc = None
        self.bridge = None

    def start(self):
        cfg_path = os.path.join(self.tmp, "c.json")
        import json
        with open(cfg_path, "w", encoding="utf-8") as fh:
            json.dump({"server": {"port": PORT},
                       "storage": {"enabled": False}}, fh)
        env = dict(os.environ)
        env["PYTHONIOENCODING"] = "utf-8"
        self.proc = subprocess.Popen(
            [sys.executable, "-m", "hsfsrv", "--sim", "--port", str(PORT),
             "--config", cfg_path],
            cwd=SERVER_ROOT, env=env,
            stdout=subprocess.DEVNULL, stderr=subprocess.STDOUT)
        import socket
        for _ in range(120):
            try:
                socket.create_connection(("127.0.0.1", PORT),
                                        timeout=0.3).close()
                return True
            except OSError:
                time.sleep(0.2)
        return False

    def connect(self):
        from hsfsrv_client.bridge import LinkBridge
        self.bridge = LinkBridge()
        ok, err = self.bridge.connect_to("127.0.0.1", PORT, "root", "root")
        return ok, err

    def op(self, **doc):
        """发一条变频器控制并等应答，返回应答 doc。"""
        holder, done = {}, [False]

        def _done(code, dtype, data):
            from hsfsrv_client import bson
            try:
                holder.update(bson.decode(data) if data else {})
            except ValueError:
                pass
            done[0] = True

        def _err(msg):
            holder["err"] = msg
            done[0] = True

        self.bridge.send_vfd_control(doc, on_done=_done, on_error=_err)
        end = time.time() + 8
        while not done[0] and time.time() < end:
            time.sleep(0.05)
        return holder

    def snapshot(self):
        """拉一次变频器快照（返回 doc）。"""
        from hsfsrv_client import proto, bson
        holder, done = {}, [False]

        def _done(code, dtype, data):
            try:
                holder.update(bson.decode(data) if data else {})
            except ValueError:
                pass
            done[0] = True

        self.bridge.request(proto.CMD_VFD_SNAPSHOT, None, on_done=_done)
        end = time.time() + 8
        while not done[0] and time.time() < end:
            time.sleep(0.05)
        return holder

    def output_hz(self, settle=3.0):
        """等运行稳定后读输出频率。

        [字段名] 遥测键见 server_py/proto.py 的 VFD_TELEMETRY_FIELDS：
        `outputFreqHz`（输出频率）/ `setFreqHz`（设定频率）。读不到返回
        None，让断言明确报"读不到"而不是伪装成 0。
        """
        wait(settle)
        doc = self.snapshot()
        value = doc.get("outputFreqHz")
        if value is None:
            return None
        try:
            return float(value)
        except (TypeError, ValueError):
            return None

    def stop(self):
        try:
            if self.bridge is not None:
                from hsfsrv_client import proto
                self.bridge.send_vfd_control({"op": proto.OP_STOP})
                wait(0.5)
                self.bridge.disconnect_from()
        except Exception:                            # noqa: BLE001
            pass
        if self.proc is not None:
            self.proc.terminate()
            try:
                self.proc.wait(timeout=8)
            except subprocess.TimeoutExpired:
                self.proc.kill()


def main():
    from hsfsrv_client import proto
    srv = Server()
    if not srv.start():
        print("[FAIL] 服务端启动失败")
        return 1
    try:
        ok, err = srv.connect()
        if not ok:
            print("[FAIL] 登录失败：%s" % err)
            return 1
        print("已连接 sim 服务端 :%d" % PORT)

        # 额定点（客户端按这个换算转速）
        snap = srv.snapshot()
        rated_rpm = float(snap.get("ratedRpm") or 1440.0)
        rated_hz = float(snap.get("ratedHz") or 50.0)
        print("额定点：%.0f rpm @ %.0f Hz" % (rated_rpm, rated_hz))

        # ---- A. 频率模式：3Hz 启动 ----
        print("== A. 频率模式：3Hz 启动 ==")
        resp = srv.op(op=proto.OP_START, rev=False, hz=3.0)
        check("A1 启动命令被受理", resp.get("ok") is True,
              str(resp.get("err", ""))[:50])
        out = srv.output_hz()
        check("A2 实际输出 = 3Hz（不是残留旧给定）",
              out is not None and abs(out - 3.0) < 0.6,
              "out=%.2fHz 期望 3.0Hz" % (out if out is not None else -1))
        srv.op(op=proto.OP_STOP)
        wait(1.5)

        # ---- B. 换给定值再启动：8Hz ----
        print("== B. 换给定再启动：8Hz ==")
        resp = srv.op(op=proto.OP_START, rev=False, hz=8.0)
        check("B1 启动受理", resp.get("ok") is True, str(resp.get("err", ""))[:50])
        out = srv.output_hz()
        check("B2 实际输出 = 8Hz（按当前设定，不是上一次的 3Hz）",
              out is not None and abs(out - 8.0) < 0.6,
              "out=%.2fHz 期望 8.0Hz" % (out if out is not None else -1))
        srv.op(op=proto.OP_STOP)
        wait(1.5)

        # ---- C. 转速模式：给 rpm，服务端换算成频率 ----
        print("== C. 转速模式：给 rpm ==")
        want_rpm = rated_rpm / 2.0                       # 半速
        want_hz = want_rpm / rated_rpm * rated_hz        # = rated_hz/2
        resp = srv.op(op=proto.OP_START, rev=False, rpm=want_rpm,
                      hz=want_hz)
        check("C1 启动受理（转速口径）", resp.get("ok") is True,
              str(resp.get("err", ""))[:50])
        out = srv.output_hz()
        check("C2 实际输出 = 转速换算值 %.1fHz" % want_hz,
              out is not None and abs(out - want_hz) < 0.6,
              "out=%.2fHz 期望 %.1fHz" % (out if out is not None else -1,
                                          want_hz))
        srv.op(op=proto.OP_STOP)
        wait(1.5)

        # ---- D. 点动也带给定 ----
        print("== D. 点动带给定 ==")
        resp = srv.op(op=proto.OP_JOG, rev=False, hz=5.0)
        check("D1 点动受理", resp.get("ok") is True, str(resp.get("err", ""))[:50])
        # [必须持续发心跳] 服务端点动看门狗 600ms 收不到帧就自动停机
        # （安全兜底）。只发一帧就等 2.5s 的话设备早停了——那测的是
        # 看门狗而不是给定值。这里按客户端真实行为每 200ms 重发。
        out = None
        end = time.time() + 3.0
        while time.time() < end:
            srv.bridge.send_vfd_control(
                {"op": proto.OP_JOG, "rev": False, "hz": 5.0})
            time.sleep(0.2)
        doc = srv.snapshot()
        out = doc.get("outputFreqHz")
        out = float(out) if out is not None else None
        check("D2 点动输出 = 5Hz（按当前设定）",
              out is not None and abs(out - 5.0) < 0.8,
              "out=%.2fHz 期望 5.0Hz" % (out if out is not None else -1))
        srv.op(op=proto.OP_JOG_RELEASE)
        wait(1.0)

        # ---- E. 停机时单独下发给定仍被拒（安全语义不许回归）----
        print("== E. 停机单独下发给定仍被拒 ==")
        resp = srv.op(op=proto.OP_SET_FREQUENCY, hz=10.0)
        check("E1 停机单独给定被拒", resp.get("ok") is not True,
              str(resp.get("err", ""))[:60])

    finally:
        srv.stop()

    print("==== %d passed, %d failed ====" % (PASS, FAIL))
    return 1 if FAIL else 0


if __name__ == "__main__":
    sys.exit(main())
