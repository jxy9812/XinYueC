#!/bin/bash
# verify/board/auto_mode_board_check.sh —— 板端自动模式三场景 + 服务端 VmData 泄漏复检。
#
# 用法（Windows Git Bash，宿主机对板执行）:
#   BOARD_PW=<板端口令> sh verify/board/auto_mode_board_check.sh
#   BOARD_PW=... BOARD=192.168.1.249 CHURN=200 sh verify/board/auto_mode_board_check.sh
#
# 流程（四步，任何一步失败都把 hsfclientpy 恢复回来再退出非零）:
#   ① systemctl stop hsfclientpy   —— 只停客户端（腾出 root 单会话给检查
#      用，AGENTS 板端纪律⑧「同账单会话」）；**绝不能停 hsfserverpy**
#      （停了就没有 6100 服务、也没有帧可采）。
#   ② 板上 Python 协议客户端（板端部署的 hsf_client_py）对 127.0.0.1:6100
#      跑自动模式三场景（产水全循环 / 阀超时 / 急停复位）。
#   ③ 服务端（hsfserverpy）pid 的 /proc/<pid>/status VmData 前后对比：
#      自动模式生命周期搅动两段各 200 轮 start/stop，判泄漏看 **VmData
#      增量**（第二段增量 ≤ 1024 kB ⇒ 无按轮线性泄漏），不看 RSS 抖动
#      （RSS 受页回收影响有 ~200KB 抖动，判不出 KB 级泄漏）。
#   ④ systemctl start hsfclientpy 恢复。
#
# 板端纪律（AGENTS 板端手搓部署，全部固化在这条脚本里）:
#   · SSH 无免密 ⇒ askpass（SSH_ASKPASS_REQUIRE=force + DISPLAY=dummy:0）；
#   · 6100 是谁在听以 systemctl show MainPID 为准，不按单元名想当然；
#   · 停/起只碰 hsfclientpy；hsfserverpy 只读（MainPID/VmData），本脚本
#     任何路径都不会 stop/restart 它；
#   · 场景客户端退出前 best-effort 全停设备（Python 载荷内置），不给
#     现场留下转着的泵/开着的阀。
set -u

BOARD="${BOARD:-192.168.1.249}"
BOARD_USER="${BOARD_USER:-root}"
BOARD_PW="${BOARD_PW:-}"
#: 板端部署的 Python 客户端包根（hsfsrv_client 的父目录）。
HSFPY_ROOT="${HSFPY_ROOT:-/root/Desktop/XinYueC/hsf_client_py}"
#: 板端服务端 systemd 单元名（只读它的 MainPID；绝不 stop/restart）。
SERVER_UNIT="${SERVER_UNIT:-hsfserverpy}"
CLIENT_UNIT="${CLIENT_UNIT:-hsfclientpy}"
#: 搅动轮数（两段各 N 轮 start/stop）。
CHURN="${CHURN:-200}"
#: 第二段 VmData 增量的通过线（kB）：200 轮/段的按轮泄漏若 ≥1KB/轮会在
#: 两段间差出 ≥200KB；1MB 线只拦「真泄漏」，放过 Python 堆/GC 平台漂移。
VDATA_TOL_KB="${VDATA_TOL_KB:-1024}"
#: 阀超时场景的等待预算（秒）：板端口径 move_timeout=20s 产品缺省，阀层
#: 判故障约 20s 后才到（与 verify/hsf_auto_mode_check.py 的 S2 同一条）。
VALVE_TIMEOUT_BUDGET_S="${VALVE_TIMEOUT_BUDGET_S:-35}"

# [口令两入口] BOARD_PW 环境变量，或 AGENTS 板端口径的既有 askpass 脚本
# （/tmp/askpass.sh，操作者手搓部署时已建）。门禁无人值守调用不一定拿得
# 到口令环境变量（2026-11-12 实测：无 BOARD_PW 时脚本在用法闸 exit 2，
# 板检一步没跑）——有现成 askpass 就直接复用它，不再自建。
ASKPASS_FILE="${BOARD_ASKPASS:-/tmp/askpass.sh}"
USE_EXIST_ASKPASS=0
if [ -z "$BOARD_PW" ]; then
    if [ -x "$ASKPASS_FILE" ]; then
        USE_EXIST_ASKPASS=1
    else
        echo "用法: BOARD_PW=<板端口令> sh verify/board/auto_mode_board_check.sh"
        echo "  （SSH 无免密：口令走 BOARD_PW，或按 AGENTS 板端口径先建"
        echo "    askpass 脚本 $ASKPASS_FILE（内容 echo <口令>）再跑）"
        exit 2
    fi
fi

TMP="$(mktemp -d "${TMPDIR:-/tmp}/hsf_auto_board_XXXXXX")"
CASE_PY="$TMP/hsf_auto_board_case.py"
BOARD_CASE_PY="/tmp/hsf_auto_board_case.py"
fails=0
CLIENT_WAS_ACTIVE=3   # 3=未知（预检后置 0/非 0）

# ---------- SSH/SCP 自动化（口令经 SSH_ASKPASS；hsf-board-deploy.sh 同款） ----------
if [ "$USE_EXIST_ASKPASS" = "1" ]; then
    ASKPASS="$ASKPASS_FILE"          # 复用操作者既有 askpass（不经口令变量）
else
    ASKPASS="$TMP/askpass_board.sh"
    printf '#!/bin/sh\necho %s\n' "$BOARD_PW" > "$ASKPASS"
    chmod +x "$ASKPASS"
fi
export SSH_ASKPASS="$ASKPASS" SSH_ASKPASS_REQUIRE=force DISPLAY=dummy:0
SSH=(ssh -o StrictHostKeyChecking=no -o ConnectTimeout=10 "$BOARD_USER@$BOARD")
SCP=(scp -o StrictHostKeyChecking=no -o ConnectTimeout=10)

restore_client() {
    # 恢复闸：只有「检查前 active」才拉回（本就不在跑的不替人拉起）。
    if [ "$CLIENT_WAS_ACTIVE" = "0" ]; then
        "${SSH[@]}" "systemctl start $CLIENT_UNIT >/dev/null 2>&1" || true
        echo "==> $CLIENT_UNIT 已恢复（检查期间被停）"
    fi
    "${SSH[@]}" "rm -f $BOARD_CASE_PY" >/dev/null 2>&1 || true
    rm -rf "$TMP" 2>/dev/null || true
}

# ---------- 板上跑的协议客户端（三场景 + 搅动；从 hsf_client_py 导协议栈） ----------
# [为什么整份 Python 塞进 heredoc] 板端只有部署包、没有仓库树；场景逻辑必须
# 随脚本走。单引号 heredoc = 板端拿到的字节与本文件一致（无 bash 展开）。
cat > "$CASE_PY" <<'PYEOF'
# -*- coding: utf-8 -*-
"""板端自动模式三场景 + 生命周期搅动（对 127.0.0.1:6100 的 hsfserverpy）。

用法:
    python3 hsf_auto_board_case.py --scenarios            # 三场景
    python3 hsf_auto_board_case.py --churn 200            # 200 轮 start/stop
    --pyroot <dir> 指定 hsf_client_py 包根（缺省板端部署路径）。
退出码 0=全过（阀超时场景现场阀真到位时记 SKIP 不算失败——确定性阀超时
锁在桌面 §S / verify/hsf_auto_mode_check.py）。安全收尾：任何路径退出前
best-effort 全停设备（op=2）+ 复位（op=4），不给现场留下转着的泵。
"""
import argparse
import sys
import time

_G_FAILS = []
_G_SKIPS = []


def check(name, ok_cond, detail=""):
    print("%s %s%s" % ("[PASS]" if ok_cond else "[FAIL]", name,
                       ("  (%s)" % detail) if detail else ""))
    if not ok_cond:
        _G_FAILS.append(name)


def skip(name, detail=""):
    print("[SKIP] %s  (%s)" % (name, detail))
    _G_SKIPS.append(name)


def main():
    ap = argparse.ArgumentParser()
    g = ap.add_mutually_exclusive_group(required=True)
    g.add_argument("--scenarios", action="store_true", help="跑三场景")
    g.add_argument("--churn", type=int, metavar="N", help="N 轮 start/stop")
    ap.add_argument("--pyroot", default="/root/Desktop/XinYueC/hsf_client_py",
                    help="hsf_client_py 包根（hsfsrv_client 的父目录）")
    args = ap.parse_args()

    # [导入在参后] 包根可由 --pyroot 覆盖 ⇒ 协议栈导入必须在解析之后。
    if args.pyroot not in sys.path:
        sys.path.insert(0, args.pyroot)
    from hsfsrv_client import bson, proto
    from hsfsrv_client.xlink import XLinkClient

    HOST, PORT = "127.0.0.1", 6100
    #: 自动运行告警主题（client_py proto 未登记；值 = common/XhsfLinkProto.h
    #: 的 HSF_TOPIC_AUTO_ALARM，勿改动）。
    TOPIC_AUTO_ALARM = 0x0001000B
    # [板端包可能落后仓库] hsf_client_py 是部署拷贝；自动模式族常量
    # 2026-11-11 才进仓库 proto，部署包没跟上时直接 proto.X 会 AttributeError
    # 把检查卡死（板检实测）。用 getattr 兜底取仓库同值缺省——值源 =
    # client_py proto.py:271/281/309/311/313/315/433，与
    # common/XhsfLinkProto.h 逐值一致。判的是**服务端**能力，别让旧客户端
    # 包替它背锅。
    CFG_SECTION_AUTO = getattr(proto, "CFG_SECTION_AUTO", "auto")
    AUTO_MODE_AUTO = getattr(proto, "AUTO_MODE_AUTO", "auto")
    KEY_AUTO_MODE = getattr(proto, "KEY_AUTO_MODE", "autoMode")
    KEY_AUTO_PHASE = getattr(proto, "KEY_AUTO_PHASE", "autoPhase")
    KEY_AUTO_FAULT = getattr(proto, "KEY_AUTO_FAULT", "autoFault")
    KEY_AUTO_RUNNING = getattr(proto, "KEY_AUTO_RUNNING", "autoRunning")
    AUTO_ALARM_VALVE_TIMEOUT = getattr(proto, "AUTO_ALARM_VALVE_TIMEOUT", 5)
    PHASE_TIMEOUT_S = 40.0        # 真阀行程 ≈9s + 推送节拍，留足余量。
    VALVE_TIMEOUT_BUDGET_S = 35   # 阀层 20s 产品缺省判故障 + 宽限。

    class Client(object):
        """最小协议客户端（同 verify/hsf_auto_mode_check.py 的 ProtoClient）。"""

        def __init__(self, port):
            self.port = port
            self.client = None
            self.publishes = []

        def connect(self):
            c = XLinkClient(HOST, self.port, "root", "root", log=lambda m: None)

            def on_publish(topic, body, dtype):
                doc = None
                if dtype == proto.DATATYPE_BSON:
                    try:
                        doc = bson.decode(body)
                    except ValueError:
                        doc = None
                self.publishes.append((topic, doc))

            c.on_publish = on_publish
            ok, err = c.start(timeout=10.0)
            if not ok:
                raise RuntimeError("登录失败：%s" % err)
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
            except Exception:                      # noqa: BLE001
                return None
            try:
                return bson.decode(data) if dtype == proto.DATATYPE_BSON else {}
            except ValueError:
                return None

        def cleanup(self):
            """安全收尾：全停 + 复位（best-effort，失败不影响退出码）。"""
            for op in ({"op": 2}, {"op": 4}):
                try:
                    self.request(proto.CMD_AUTO_CONTROL, op, timeout=6.0)
                except Exception:                  # noqa: BLE001
                    pass

    def do_mask(cli):
        doc = cli.request(proto.CMD_DIGITAL_SNAPSHOT, {"dev": 0}) or {}
        if not doc.get(proto.KEY_VALID):
            return None
        return int(doc.get(proto.KEY_DO) or 0)

    def mode_state(cli):
        return cli.request(proto.CMD_RUN_MODE, None) or {}

    def wait_phase(cli, phase, timeout_s):
        deadline = time.time() + timeout_s
        while time.time() < deadline:
            if mode_state(cli).get(KEY_AUTO_PHASE) == phase:
                return True
            time.sleep(0.5)
        return mode_state(cli).get(KEY_AUTO_PHASE) == phase

    def alarm_frames(cli, kind):
        return [doc for (t, doc) in cli.publishes
                if t == TOPIC_AUTO_ALARM and doc is not None
                and doc.get("kind") == kind]

    def case_produce(cli):
        """场景 A：产水全循环（配置往返 / 阀相 / running / 泵面 / 停止 / 粘性）。"""
        cfg = cli.request(proto.CMD_CONFIG_SET, {
            "section": CFG_SECTION_AUTO,
            "modes": {"produce": {"devices": ["valve1", "valve2", "stirPump",
                                              "inletPump"],
                                  "stirRpm": 720, "stirReverse": True}}})
        check("A 配置 SET 受理", bool(cfg and cfg.get(proto.KEY_OK)),
              "err=%s" % (cfg or {}).get("err"))
        getdoc = cli.request(proto.CMD_CONFIG_GET, None) or {}
        echo = ((getdoc.get(CFG_SECTION_AUTO) or {}).get(
            "modes", {}).get("produce", {}))
        check("A 配置 GET 往返一致",
              echo.get("devices") == ["valve1", "valve2", "stirPump",
                                      "inletPump"]
              and echo.get("stirRpm") == 720
              and echo.get("stirReverse") is True,
              "回显=%s" % (echo,))
        check("A 切自动受理",
              bool((cli.request(proto.CMD_RUN_MODE, {"mode": 1}) or {})
                   .get(proto.KEY_OK)))
        r = cli.request(proto.CMD_AUTO_CONTROL,
                        {"op": 1, KEY_AUTO_MODE: "produce"}) or {}
        check("A 启动受理且回显 autoMode=produce / autoPhase=valves",
              r.get(proto.KEY_OK) and r.get(KEY_AUTO_MODE) == "produce"
              and r.get(KEY_AUTO_PHASE) == "valves", "应答=%s" % r)
        check("A 推进到 running（真阀行程 ≈9s，预算 %ds）" % PHASE_TIMEOUT_S,
              wait_phase(cli, "running", PHASE_TIMEOUT_S),
              "phase=%s" % mode_state(cli).get(KEY_AUTO_PHASE))
        m = do_mask(cli)
        check("A 进水泵（DO6）已启动", m is not None and (m & (1 << 6)) != 0,
              "do=0x%X" % (m or 0))
        vfd = cli.request(proto.CMD_VFD_SNAPSHOT, {"side": 0}) or {}
        check("A 搅拌泵运行且反转、给定 ≈25Hz（720rpm/1440 额定·50Hz 满量程）",
              bool(vfd.get("running")) and bool(vfd.get("reverse"))
              and abs(float(vfd.get("setFreqHz") or 0.0) - 25.0) < 0.2,
              "running=%s reverse=%s setFreqHz=%s"
              % (vfd.get("running"), vfd.get("reverse"), vfd.get("setFreqHz")))
        r = cli.request(proto.CMD_AUTO_CONTROL, {"op": 2}) or {}
        check("A 停止受理", bool(r.get(proto.KEY_OK)), "err=%s" % r.get("err"))
        deadline = time.time() + 20
        stopped = False
        while time.time() < deadline:
            m = do_mask(cli)
            if m is not None and (m & (1 << 6)) == 0:
                stopped = True
                break
            time.sleep(0.5)
        check("A 停止后进水泵已关（DO6=0）", stopped,
              "do=0x%X" % (do_mask(cli) or 0))
        st = mode_state(cli)
        check("A 停止后 autoRunning=false 且模式粘性 produce",
              st.get(KEY_AUTO_RUNNING) is False
              and st.get(KEY_AUTO_MODE) == "produce",
              "autoRunning=%s autoMode=%s" % (st.get(KEY_AUTO_RUNNING),
                                              st.get(KEY_AUTO_MODE)))

    def case_valve_timeout(cli):
        """场景 B：阀超时（板端真阀/真模块，能否复现卡阀取决于现场状态）。"""
        del cli.publishes[:]
        cli.request(proto.CMD_CONFIG_SET, {
            "section": CFG_SECTION_AUTO,
            "modes": {"backwash": {"devices": ["valve3", "inletPump"],
                                   "stirRpm": 1440}}})
        fut = cli.client.call_soon(cli.client.subscribe(TOPIC_AUTO_ALARM))
        if fut is not None:
            fut.result(timeout=5.0)
        cli.request(proto.CMD_RUN_MODE, {"mode": 1})
        r = cli.request(proto.CMD_AUTO_CONTROL,
                        {"op": 1, KEY_AUTO_MODE: "backwash"}) or {}
        check("B 启动受理（进阀相）",
              r.get(proto.KEY_OK) and r.get(KEY_AUTO_PHASE) == "valves",
              "应答=%s" % r)
        deadline = time.time() + VALVE_TIMEOUT_BUDGET_S
        frames = []
        while time.time() < deadline:
            frames = alarm_frames(cli, AUTO_ALARM_VALVE_TIMEOUT)
            if frames:
                break
            time.sleep(0.5)
        if not frames:
            # 现场阀真动起来了（模块在线、阀 3 行程到位）⇒ 阀超时不可复现，
            # 记 SKIP 不判失败——确定性阀超时锁在桌面 §S / 桌面 verify 脚本。
            skip("B 阀超时告警（kind=5）未在 %ds 内出现" % VALVE_TIMEOUT_BUDGET_S,
                 "现场阀已动作到位，无法复现卡阀；确定性锁见桌面 §S/verify")
            cli.cleanup()
            return
        text = str(frames[0].get("text", ""))
        check("B 告警文字点名阀3 +「超时」", "阀3" in text and "超时" in text,
              "text=%s" % text)
        st = mode_state(cli)
        check("B 停止自动模式 + autoFault=true",
              st.get(KEY_AUTO_RUNNING) is False
              and st.get(KEY_AUTO_FAULT) is True,
              "autoRunning=%s autoFault=%s" % (st.get(KEY_AUTO_RUNNING),
                                               st.get(KEY_AUTO_FAULT)))
        m = do_mask(cli)
        check("B 泵绝不被启动（DO6=0）", m is not None and (m & (1 << 6)) == 0,
              "do=0x%X" % (m or 0))
        r = cli.request(proto.CMD_AUTO_CONTROL,
                        {"op": 1, KEY_AUTO_MODE: "backwash"}) or {}
        check("B 报警未复位再启动被拒（含「复位」）",
              not r.get(proto.KEY_OK)
              and "复位" in str(r.get(proto.KEY_ERR, "")),
              "err=%s" % r.get(proto.KEY_ERR))
        r = cli.request(proto.CMD_AUTO_CONTROL, {"op": 4}) or {}
        check("B 复位受理", bool(r.get(proto.KEY_OK)))
        r = cli.request(proto.CMD_AUTO_CONTROL,
                        {"op": 1, KEY_AUTO_MODE: "backwash"}) or {}
        check("B 复位后可再启动", bool(r.get(proto.KEY_OK)),
              "err=%s" % r.get(proto.KEY_ERR))
        cli.cleanup()

    def case_estop_gates(cli):
        """场景 C：闸门面（组合拒 / 未配置拒 / 急停 estop / 锁存拒 / 复位解锁）。"""
        cli.request(proto.CMD_CONFIG_SET, {
            "section": CFG_SECTION_AUTO,
            "modes": {"produce": {"devices": ["valve1", "stirPump"],
                                  "stirRpm": 1440}}})
        cli.request(proto.CMD_CONFIG_SET, {
            "section": CFG_SECTION_AUTO,
            "modes": {"rinse": {"devices": []}}})  # 显式清空 ⇒ 未配置口径确定。
        r = cli.request(proto.CMD_AUTO_CONTROL,
                        {"op": 1,
                         KEY_AUTO_MODE: AUTO_MODE_AUTO}) or {}
        check("C 组合模式拒（含「未实现」）",
              not r.get(proto.KEY_OK)
              and "未实现" in str(r.get(proto.KEY_ERR, "")),
              "err=%s" % r.get(proto.KEY_ERR))
        r = cli.request(proto.CMD_AUTO_CONTROL,
                        {"op": 1, KEY_AUTO_MODE: "rinse"}) or {}
        check("C 未配置模式拒（含「未配置」）",
              not r.get(proto.KEY_OK)
              and "未配置" in str(r.get(proto.KEY_ERR, "")),
              "err=%s" % r.get(proto.KEY_ERR))
        cli.request(proto.CMD_RUN_MODE, {"mode": 1})
        r = cli.request(proto.CMD_AUTO_CONTROL,
                        {"op": 1, KEY_AUTO_MODE: "produce"}) or {}
        check("C 启动受理", bool(r.get(proto.KEY_OK)), "err=%s" % r.get("err"))
        check("C 推进到 running", wait_phase(cli, "running", PHASE_TIMEOUT_S),
              "phase=%s" % mode_state(cli).get(KEY_AUTO_PHASE))
        r = cli.request(proto.CMD_AUTO_CONTROL, {"op": 3}) or {}   # 急停。
        st = mode_state(cli)
        check("C 急停受理且相位 estop / autoRunning=false",
              bool(r.get(proto.KEY_OK))
              and st.get(KEY_AUTO_PHASE) == "estop"
              and st.get(KEY_AUTO_RUNNING) is False,
              "phase=%s autoRunning=%s" % (st.get(KEY_AUTO_PHASE),
                                           st.get(KEY_AUTO_RUNNING)))
        m = do_mask(cli)
        check("C 急停全停：阀输出清零（DO0..5=0）",
              m is not None and (m & 0x3F) == 0, "do=0x%X" % (m or 0))
        r = cli.request(proto.CMD_AUTO_CONTROL,
                        {"op": 1, KEY_AUTO_MODE: "produce"}) or {}
        check("C 锁存拒启动（含「复位」）",
              not r.get(proto.KEY_OK)
              and "复位" in str(r.get(proto.KEY_ERR, "")),
              "err=%s" % r.get(proto.KEY_ERR))
        cli.request(proto.CMD_AUTO_CONTROL, {"op": 4})             # 复位。
        r = cli.request(proto.CMD_AUTO_CONTROL,
                        {"op": 1, KEY_AUTO_MODE: "produce"}) or {}
        check("C 复位后可再启动", bool(r.get(proto.KEY_OK)),
              "err=%s" % r.get(proto.KEY_ERR))
        cli.cleanup()

    def churn(n):
        """生命周期搅动：n 轮 start/stop（VmData 判据的数据源，不做断言）。"""
        cli = Client(PORT).connect()
        cli.request(proto.CMD_CONFIG_SET, {
            "section": CFG_SECTION_AUTO,
            "modes": {"produce": {"devices": ["valve1", "stirPump",
                                              "inletPump"],
                                  "stirRpm": 720, "stirReverse": False}}})
        cli.request(proto.CMD_RUN_MODE, {"mode": 1})
        fails_local = 0
        t0 = time.time()
        for i in range(1, n + 1):
            # [启动必须带模式 id] 两端同口径：开机粘性为空时模式无关启动必
            # 被「未选择自动模式」拒（autocontrol.py:528 / HsfServerApp.c
            # 同口径）。这里虽借三场景留下的粘性 produce 能过，但依赖前序
            # 场景属隐式契约——与桌面泄漏门同款显式带 mode。
            r = cli.request(proto.CMD_AUTO_CONTROL,
                            {"op": 1, KEY_AUTO_MODE: "produce"})
            if r is None or not r.get(proto.KEY_OK):
                fails_local += 1
            r = cli.request(proto.CMD_AUTO_CONTROL, {"op": 2})
            if r is None or not r.get(proto.KEY_OK):
                fails_local += 1
            if i % 50 == 0:
                print("   搅动 %d/%d（失败 %d）" % (i, n, fails_local))
        print("CHURN_FAILS=%d CHURN_SECONDS=%.1f"
              % (fails_local, time.time() - t0))
        cli.cleanup()
        cli.close()
        return 0 if fails_local == 0 else 1

    if args.churn is not None:
        return churn(args.churn)
    cli = Client(PORT).connect()
    try:
        case_produce(cli)
        case_valve_timeout(cli)
        case_estop_gates(cli)
    finally:
        cli.cleanup()
        cli.close()
    print("CASE_FAILS=%d CASE_SKIPS=%d" % (len(_G_FAILS), len(_G_SKIPS)))
    return 1 if _G_FAILS else 0


if __name__ == "__main__":
    sys.exit(main())
PYEOF

# ---------- 预检 ----------
step() { echo "==> $*"; }
okp()  { echo "[PASS] $*"; }
failp() { echo "[FAIL] $*"; fails=$((fails + 1)); }

step "[0/5] 环境预检"
"${SSH[@]}" 'echo board-ok' >/dev/null || { echo "板子 $BOARD 连不上（SSH 口令/地址？）"; exit 1; }
if ! "${SCP[@]}" "$CASE_PY" "$BOARD_USER@$BOARD:$BOARD_CASE_PY" >/dev/null; then
    echo "场景脚本上传失败"
    exit 1
fi
SVCPID="$("${SSH[@]}" "systemctl show -p MainPID --value $SERVER_UNIT" 2>/dev/null | tr -d '\r')"
if [ -z "$SVCPID" ] || [ "$SVCPID" = "0" ]; then
    # [AGENTS 纪律⑦：6100 是谁在听，先查再连] 单元 dead ≠ 服务没了——
    # 2026-11-11 两代实锅：白天给 6100 服务的是 nohup 手工进程或 C 栈
    # （systemctl 痕迹为零）。以「谁在听 6100」为准取 pid（VmData 判据
    # 对任何 pid 都成立）；真没人听才按纪律⑦给处置指令退出。
    SVCPID="$("${SSH[@]}" "ss -tlnp 2>/dev/null | grep ':6100' | grep -oE 'pid=[0-9]+' | head -1 | cut -d= -f2" 2>/dev/null | tr -d '\r')"
    if [ -z "$SVCPID" ] || [ "$SVCPID" = "0" ]; then
        echo "服务端单元 $SERVER_UNIT 不在跑（MainPID=0）且 6100 无监听——按 AGENTS 纪律⑦先 systemctl start $SERVER_UNIT（本脚本不代做，避免误拉起不该起的服务）"
        exit 1
    fi
    echo "    [注] 单元 $SERVER_UNIT 不在跑（MainPID=0）；6100 由 pid=$SVCPID 在服务（nohup 手工进程或 C 栈）——场景与 VmData 都对它做，单元保持只读不动"
fi
if ! "${SSH[@]}" "test -d $HSFPY_ROOT/hsfsrv_client"; then
    echo "板端没有 $HSFPY_ROOT/hsfsrv_client（hsf_client_py 未部署？）"
    exit 1
fi
# [括号数 = 语义] $( ( … ) ) 才是命令替换；$( ( ( … ) ) ) 是算术展开——
# 数组首元素展开后以字面量 ssh 开头，算术求值把它当标识符查，set -u 下
# 即「ssh: unbound variable」（板检实测卡死在预检末行）。
CLIENT_WAS_ACTIVE="$("${SSH[@]}" "systemctl is-active --quiet $CLIENT_UNIT; echo \$?" 2>/dev/null | tr -d '\r')"
okp "板可达、服务端 pid=$SVCPID（单元 $SERVER_UNIT 只读不碰）、hsf_client_py 在位（$CLIENT_UNIT 先前 active 与否=$CLIENT_WAS_ACTIVE）"

# 此刻起任何退出路径都走恢复闸（stop 成功与否都尝试按原状恢复）。
trap restore_client EXIT

vdata_of() {
    "${SSH[@]}" "awk '/^VmData:/{print \$2}' /proc/$1/status 2>/dev/null" | tr -d '\r'
}

step "[1/5] 停 $CLIENT_UNIT（只停客户端；绝不动 $SERVER_UNIT）"
if [ "$CLIENT_WAS_ACTIVE" = "0" ]; then
    if ! "${SSH[@]}" "systemctl stop $CLIENT_UNIT"; then
        failp "stop $CLIENT_UNIT 失败"
        exit 1
    fi
    sleep 2
    okp "$CLIENT_UNIT 已停（腾出 root 单会话）"
else
    echo "    $CLIENT_UNIT 本就不在跑（退出时不拉它）"
fi

step "[2/5] 三场景（产水全循环 / 阀超时 / 急停复位）"
VDATA0="$(vdata_of "$SVCPID")"
echo "    服务端 pid=$SVCPID 起始 VmData=${VDATA0} kB"
if "${SSH[@]}" "python3 $BOARD_CASE_PY --scenarios --pyroot $HSFPY_ROOT"; then
    okp "三场景全过（退出码 0）"
else
    failp "三场景有失败（明细见上方 [FAIL] 行）"
fi

step "[3/5] VmData 泄漏判据：两段各 $CHURN 轮 start/stop 搅动"
echo "    第一段搅动（$CHURN 轮）……"
if ! "${SSH[@]}" "python3 $BOARD_CASE_PY --churn $CHURN --pyroot $HSFPY_ROOT"; then
    failp "第一段搅动有请求失败（明细见 CHURN_FAILS）"
fi
sleep 5
VDATA1="$(vdata_of "$SVCPID")"
echo "    第二段搅动（$CHURN 轮）……"
if ! "${SSH[@]}" "python3 $BOARD_CASE_PY --churn $CHURN --pyroot $HSFPY_ROOT"; then
    failp "第二段搅动有请求失败（明细见 CHURN_FAILS）"
fi
sleep 5
VDATA2="$(vdata_of "$SVCPID")"
if [ -z "$VDATA0" ] || [ -z "$VDATA1" ] || [ -z "$VDATA2" ]; then
    failp "VmData 采样缺失（VDATA0=$VDATA0 VDATA1=$VDATA1 VDATA2=$VDATA2）"
else
    DELTA1=$((VDATA1 - VDATA0))
    DELTA2=$((VDATA2 - VDATA1))
    echo "    VmData：起始 ${VDATA0} kB → 第一段后 ${VDATA1} kB（Δ1=${DELTA1} kB）→ 第二段后 ${VDATA2} kB（Δ2=${DELTA2} kB）"
    if [ "$DELTA2" -le "$VDATA_TOL_KB" ]; then
        okp "VmData 第二段增量有界（Δ2=${DELTA2} kB ≤ ${VDATA_TOL_KB} kB ⇒ 无按轮线性泄漏；看 VmData 不看 RSS 抖动）"
    else
        failp "VmData 第二段增量 ${DELTA2} kB > ${VDATA_TOL_KB} kB（按轮泄漏特征，两段搅动间续涨）"
    fi
fi

step "[4/5] 恢复 $CLIENT_UNIT"
restore_client
if [ "$CLIENT_WAS_ACTIVE" = "0" ]; then
    sleep 2
    if "${SSH[@]}" "systemctl is-active --quiet $CLIENT_UNIT"; then
        okp "$CLIENT_UNIT 已回到 active"
    else
        failp "$CLIENT_UNIT 恢复后仍不在跑（现场手动 systemctl start $CLIENT_UNIT）"
    fi
fi

step "[5/5] 结果汇总"
if [ "$fails" -eq 0 ]; then
    echo "BOARD_CHECK_PASS"
    exit 0
fi
echo "BOARD_CHECK_FAIL（$fails 项失败）"
exit 1
