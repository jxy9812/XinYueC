# -*- coding: utf-8 -*-
"""一键跑全部测试（单元 + 端到端）。

    python tests/run_all.py            # 全跑
    python tests/run_all.py --units    # 只跑单元测试

端到端部分会自动在后台起假设备服务端、跑完再关掉，不需要手工准备环境。
"""

import argparse
import os
import subprocess
import sys
import tempfile
import time

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
PORT = 6199


def run(cmd, label, timeout=300):
    print("\n" + "=" * 70)
    print("  " + label)
    print("=" * 70)
    t0 = time.time()
    try:
        proc = subprocess.run(cmd, cwd=ROOT, timeout=timeout)
        ok = proc.returncode == 0
    except subprocess.TimeoutExpired:
        print("[TIMEOUT] %s" % label)
        ok = False
    print("  -> %s  (%.1fs)" % ("PASS" if ok else "FAIL", time.time() - t0))
    return ok


def port_free(port):
    """端口是否可绑（Windows/Linux 通用，纯 socket）。"""
    import socket
    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as sock:
        try:
            sock.bind(("0.0.0.0", port))
            return True
        except OSError:
            return False


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--units", action="store_true", help="只跑单元测试")
    ap.add_argument("--port", type=int, default=PORT)
    args = ap.parse_args()

    py = sys.executable
    results = []

    results.append(("端口一致性", run([py, "tests/test_ports.py"],
                                       "端口配置（单一事实源）")))
    results.append(("单元测试", run([py, "tests/test_units.py"], "单元测试")))
    results.append(("模式规则", run([py, "tests/test_mode_lamp_rules.py"],
                                     "模式规则（手动不写灯 + 切模式全停）")))
    results.append(("浊度逐台", run([py, "tests/test_turbidity_per_meter.py"],
                                     "两台浊度独立开关（逐台门控/兼容键）")))
    results.append(("RTU 帧解析", run([py, "tests/test_rtu_framing.py"],
                                       "RTU 帧解析（模拟从站）")))

    if args.units:
        return _summary(results)

    if not port_free(args.port):
        print("[ERROR] 端口 %d 已被占用——先停掉残留的服务端" % args.port)
        print("        Windows: netstat -ano | findstr :%d  → taskkill /F /PID <pid>"
              % args.port)
        return 2

    # [stdout 必须落文件，不能用没人读的管道 —— 2026-11-09 根修]
    # 原实现 `stdout=subprocess.PIPE` 且全流程**从不读**它：harness 打满
    # 64KB（Windows 匿名管道缓冲）后 write() 阻塞 ⇒ 服务端协程被卡住 ⇒
    # 客户端请求超时 ⇒ 端到端套件出现"偶发 no response / reverse=None"
    # 的假红（单独手起 harness 全绿、经 run_all 跑就红几项，实测 74/5 vs
    # 79/0）。日志量越大越容易触发（本次运行日志类别改动又增加了输出）。
    # 落文件即可：既不会阻塞，又保留现场（跑挂时可直接 tail）。
    # [落临时目录，不落仓库] 之前落在 tests/harness.out ⇒ 每次跑都多一个
    # 未跟踪文件污染工作区。改到临时目录（跑完由临时目录自己回收）。
    harness_log_path = os.path.join(tempfile.gettempdir(),
                                    "hsf_harness_%d.out" % os.getpid())
    harness_log = open(harness_log_path, "wb")
    harness = subprocess.Popen(
        [py, "tests/harness.py", "--port", str(args.port), "--seconds", "300"],
        cwd=ROOT, stdout=harness_log, stderr=subprocess.STDOUT)
    try:
        # 等服务端就绪（读 harness 打印的 PORT 行）
        ready = False
        deadline = time.time() + 15
        while time.time() < deadline:
            if harness.poll() is not None:
                break
            if not port_free(args.port):
                ready = True
                break
            time.sleep(0.2)
        if not ready:
            print("[ERROR] 假设备服务端未能就绪")
            return 2
        time.sleep(0.5)

        results.append(("变频器全操作", run(
            [py, "tests/test_vfd_ops.py", "--port", str(args.port)],
            "变频器全操作（66 项）")))
        results.append(("IO 模块", run(
            [py, "tests/test_io_modules.py", "--port", str(args.port)],
            "数字量 / 模拟量 / 健康（68 项）")))
    finally:
        harness.terminate()
        try:
            harness.wait(timeout=5)
        except subprocess.TimeoutExpired:
            harness.kill()
        try:
            harness_log.close()
        except OSError:
            pass

    return _summary(results)


def _summary(results):
    print("\n" + "=" * 70)
    print("  汇总")
    print("=" * 70)
    for label, ok in results:
        print("  [%s] %s" % ("PASS" if ok else "FAIL", label))
    failed = [label for label, ok in results if not ok]
    print("\n  %d/%d 组通过" % (len(results) - len(failed), len(results)))
    return 0 if not failed else 1


if __name__ == "__main__":
    sys.exit(main())
