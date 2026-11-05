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
    results.append(("RTU 帧解析", run([py, "tests/test_rtu_framing.py"],
                                       "RTU 帧解析（模拟从站）")))

    if args.units:
        return _summary(results)

    if not port_free(args.port):
        print("[ERROR] 端口 %d 已被占用——先停掉残留的服务端" % args.port)
        print("        Windows: netstat -ano | findstr :%d  → taskkill /F /PID <pid>"
              % args.port)
        return 2

    harness = subprocess.Popen(
        [py, "tests/harness.py", "--port", str(args.port), "--seconds", "300"],
        cwd=ROOT, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
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
