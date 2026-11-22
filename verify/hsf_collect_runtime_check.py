# -*- coding: utf-8 -*-
"""E2E 复检：远端设置「采集配置」的采集时间/开关**运行期立即生效**（C 服务端）。

跑法（无人值守，退出码 0=全绿 / 1=有失败）：
    py -3 verify\\hsf_collect_runtime_check.py
    （本机 py -3=3.12.10 带 PyQt5；D:\\anaconda3 不存在）

流程：
  1. 备份 bin/cfg/hsf_server.ini（finally 还原，现场数据不丢）；
  2. 起 bin\\HsfServer.exe --sim <port>（stdout 必须落文件——进管道会死锁）；
  3. Python 客户端桥（hsfsrv_client.bridge.LinkBridge）root/root 登录；
  4. config_set("collect", {7 个周期键}) ⇒ 断言：
     · 应答 ok=true 且文案含「运行期立即生效」（文案装在 err 键）；
     · config_get 回读逐键相等（回显走 HsfServerIo_collectConfig=生效值）；
     · 服务端日志出现「采集节拍已应用」+「陈旧阈值已随周期联动」；
  5. 5 个逐设备开关（vfd/temphum/flowmeter/analog/digital）逐个写 False：
     · 回读**同一键**变 False（CONFIG_GET 回显不再串键：早先 temphumEnabled
       回显读的是 vfdEnabled 的值）；
     · 浊度逐台开关同款；
  6. 不写开关时 --sim 下浊度两台缺省应为 true（缺省回落装配事实——路 B
     由模拟模式强制启；Windows 树 HSF_BUS_B_ENABLED=0，回落宏会把演示/
     回归里的浊度"缺省停采"）；
  7. 收尾：杀进程（terminate→kill）→ 还原 INI → 打印日志尾部。

C 侧整链（INI → 生效值 → Hub 从站约束/设备节拍）由回归锁 §N7 钉死：
    bin\\XinYueC_Static.exe --test hsf-regression
"""
import os
import shutil
import subprocess
import sys
import tempfile
import time

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(REPO, "HighSpeedFilter", "client_py"))

PORT = int(os.environ.get("HSF_E2E_PORT", "6173"))
SERVER = os.path.join(REPO, "bin", "HsfServer.exe")
BIN_DIR = os.path.join(REPO, "bin")
INI = os.path.join(BIN_DIR, "cfg", "hsf_server.ini")
INI_BAK = INI + ".collect_e2e.bak"
# 服务端 stdout 落文件（进管道会死锁）；日志放系统临时目录，不污染仓库。
LOG = os.path.join(tempfile.gettempdir(), "hsf_collect_e2e_server.log")

PERIOD_KEYS = {
    "vfdPollMs": 700,
    "temphumPollMs": 1500,
    "turbidity1PollMs": 1300,
    "turbidity2PollMs": 1700,
    "flowmeterPollMs": 1900,
    "analogPollMs": 450,
    "digitalPollMs": 550,
}
ENABLE_KEYS = [
    "vfdEnabled", "temphumEnabled", "flowmeterEnabled",
    "analogEnabled", "digitalEnabled",
    "turbidity1Enabled", "turbidity2Enabled",
]

RC = 0


def fail(msg):
    global RC
    RC = 1
    print("FAIL: " + msg, flush=True)


def ok(msg):
    print("ok: " + msg, flush=True)


def wait_port(port, timeout=25.0):
    import socket
    t0 = time.time()
    while time.time() - t0 < timeout:
        s = socket.socket()
        s.settimeout(0.5)
        try:
            s.connect(("127.0.0.1", port))
            return True
        except OSError:
            time.sleep(0.2)
        finally:
            try:
                s.close()
            except OSError:
                pass
    return False


def dump_log_tail():
    try:
        with open(LOG, encoding="utf-8", errors="replace") as f:
            return f.read()
    except OSError:
        return ""


def main():
    if not os.path.exists(SERVER):
        fail("找不到服务端 %s" % SERVER)
        return 1

    had_ini = os.path.exists(INI)
    if had_ini:
        shutil.copyfile(INI, INI_BAK)

    env = dict(os.environ)
    env["QT_QPA_PLATFORM"] = "offscreen"
    logf = open(LOG, "w", encoding="utf-8", errors="replace")
    proc = subprocess.Popen(
        [SERVER, "--sim", str(PORT)],
        cwd=BIN_DIR,
        stdout=logf, stderr=subprocess.STDOUT, env=env)
    br = None
    app = None
    try:
        if not wait_port(PORT):
            fail("服务端端口 %d 未就绪" % PORT)
            print(dump_log_tail()[-3000:], flush=True)
            return 1
        ok("服务端已就绪 :%d" % PORT)

        from PyQt5.QtWidgets import QApplication
        app = QApplication(sys.argv)
        from hsfsrv_client.bridge import LinkBridge
        br = LinkBridge()
        ok_, err = br.connect_to("127.0.0.1", PORT, "root", "root")
        if not ok_:
            fail("登录失败: %s" % err)
            return 1
        ok("登录成功 root/root")

        # ---- 桥的同步请求/应答小助手 ----
        def call(fn, *a, **kw):
            holder = {}
            done = [False]

            def _cb(doc):
                holder.update(doc if isinstance(doc, dict) else {"doc": doc})
                done[0] = True

            def _err(e):
                holder["__err__"] = str(e)
                done[0] = True

            fn(*a, on_done=_cb, on_error=_err, **kw)
            t0 = time.time()
            while not done[0] and time.time() - t0 < 10:
                time.sleep(0.05)
            if not done[0]:
                fail("%s 超时无应答" % fn.__name__)
                holder.setdefault("__err__", "timeout")
            return holder

        def get_collect():
            doc = call(br.config_get)
            if "__err__" in doc:
                fail("config_get 错误: %s" % doc["__err__"])
            return doc.get("collect") or {}

        # ---- 基线：--sim 纯净 INI 下浊度两台缺省应为开（坑④） ----
        base = get_collect()
        print("基线 collect: %s" % base, flush=True)
        # 现场INI 若已显式写过该键，缺省口径无从区分（应答只给回落后的
        # 值）——只有 INI 里没有该键时才断言"缺省回落装配事实"。
        ini_text = ""
        if had_ini:
            try:
                with open(INI, encoding="utf-8", errors="replace") as f:
                    ini_text = f.read()
            except OSError:
                ini_text = ""
        for k in ("turbidity1Enabled", "turbidity2Enabled"):
            if k in ini_text:
                print("跳过 %s 缺省断言（INI 已显式配置该键）" % k, flush=True)
            elif bool(base.get(k)) is not True:
                fail("--sim 缺省 %s=%r，期望 True（缺省回落装配事实）"
                     % (k, base.get(k)))
            else:
                ok("--sim 缺省 %s=True（装配事实回落）" % k)

        # ---- ① 7 个采集周期：写入 → 应答 → 回读 ----
        holder = call(br.config_set, "collect", dict(PERIOD_KEYS))
        if "__err__" in holder:
            fail("config_set 错误: %s" % holder["__err__"])
            return 1
        if not holder.get("ok"):
            fail("config_set 应答 ok=false: %s" % holder)
            return 1
        ok("config_set 应答 ok=true")
        msg = holder.get("err") or holder.get("msg") or ""
        print("应答文案: %r" % msg, flush=True)
        if "运行期立即生效" not in msg:
            fail("应答文案不含「运行期立即生效」（实际=%r）" % msg)
        else:
            ok("应答文案含「运行期立即生效」")

        now = get_collect()
        for k, v in PERIOD_KEYS.items():
            if int(now.get(k, -1)) != int(v):
                fail("回读 %s = %r，期望 %r" % (k, now.get(k), v))
            else:
                ok("回读 %s = %d（与写入一致）" % (k, v))

        # ---- ② 5 个逐设备开关 + 浊度逐台：写 False → 回读同一键 ----
        for k in ENABLE_KEYS:
            holder = call(br.config_set, "collect", {k: False})
            if "__err__" in holder or not holder.get("ok"):
                fail("config_set %s=False 失败: %s" % (k, holder))
                continue
            back = get_collect()
            if bool(back.get(k)) is not False:
                fail("写 %s=False 后回读 %s=%r（回显串键或未生效）"
                     % (k, k, back.get(k)))
            else:
                ok("写 %s=False → 回读同键 False（不串键、运行期生效）" % k)
            # 写回 True，避免后续键的回读被上一键污染（浊度共同键语义）。
            call(br.config_set, "collect", {k: True})

        # 还原后逐键应回到 True
        now = get_collect()
        for k in ENABLE_KEYS:
            if bool(now.get(k)) is not True:
                fail("还原后 %s=%r，期望 True" % (k, now.get(k)))
            else:
                ok("还原 %s=True" % k)

        br.disconnect_from()
        app.quit()
    finally:
        try:
            proc.terminate()
            proc.wait(timeout=5)
        except Exception:
            try:
                proc.kill()
            except Exception:
                pass
        logf.close()
        # ---- 现场还原：INI 拷回（服务端退出后才不会被覆写） ----
        if had_ini and os.path.exists(INI_BAK):
            try:
                shutil.copyfile(INI_BAK, INI)
                os.remove(INI_BAK)
                ok("已还原 %s" % INI)
            except OSError as e:
                fail("还原 INI 失败: %s" % e)
        tail = dump_log_tail()
        print("---- 服务端日志关键行 ----", flush=True)
        for line in tail.splitlines():
            if ("采集节拍已应用" in line or "陈旧阈值已随周期联动" in line
                    or "远程配置" in line):
                print("  " + line, flush=True)
        if "采集节拍已应用" not in tail:
            fail("服务端日志缺「采集节拍已应用」行")
        if "陈旧阈值已随周期联动" not in tail:
            fail("服务端日志缺「陈旧阈值已随周期联动」行")

    print("=== 退出码 %d ===" % RC, flush=True)
    return RC


if __name__ == "__main__":
    sys.exit(main())
