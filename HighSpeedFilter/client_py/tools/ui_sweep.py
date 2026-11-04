# -*- coding: utf-8 -*-
"""板端 UI 巡检：逐页启动客户端、抓屏、存图。

    python tools/ui_sweep.py --host 192.168.0.10            # 全部 17 页
    python tools/ui_sweep.py --host 192.168.0.10 --pages 0,3,5
    python tools/ui_sweep.py --offline                      # 离线（不连服务端）

在**板子上**跑（需要 DISPLAY=:0 与 PyQt5）。输出到 /tmp/ui_sweep/。
抓图用 Qt grabWindow（不用 xwd —— 见 tools/shot.py 的说明）。
"""

import argparse
import os
import subprocess
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)

PAGE_NAMES = ["主界面", "主菜单", "监视", "数字量", "模拟量", "变频器",
              "电磁流量计", "数据", "历史数据", "日志", "报警", "设置",
              "本地设置", "远端设置", "网络设置", "悬浮窗设置", "窗口设置"]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--host", default=None, help="服务端地址（不给则离线）")
    ap.add_argument("--port", type=int, default=6200)
    ap.add_argument("--pages", default=None, help="页索引列表，如 0,3,5")
    ap.add_argument("--out", default="/tmp/ui_sweep")
    ap.add_argument("--size", default="1280x720")
    ap.add_argument("--settle", type=float, default=6.0,
                    help="每页等待秒数（等数据到达）")
    args = ap.parse_args()

    pages = ([int(x) for x in args.pages.split(",")] if args.pages
             else list(range(len(PAGE_NAMES))))
    os.makedirs(args.out, exist_ok=True)

    env = dict(os.environ)
    env.setdefault("DISPLAY", ":0")
    env["QT_QPA_PLATFORM"] = "xcb"
    env["QT_OPENGL"] = "software"

    for page in pages:
        name = PAGE_NAMES[page] if page < len(PAGE_NAMES) else "page%d" % page
        print("== 页 %2d %s ==" % (page, name), flush=True)
        cmd = [sys.executable, "-m", "hsfsrv_client",
               "--page", str(page), "--size", args.size,
               "--seconds", str(int(args.settle + 8))]
        if args.host:
            cmd += ["--auto", args.host, "--port", str(args.port)]
        else:
            cmd.append("--offline")
        proc = subprocess.Popen(cmd, cwd=ROOT, env=env,
                                stdout=subprocess.DEVNULL,
                                stderr=subprocess.DEVNULL)
        time.sleep(args.settle)
        shot = os.path.join(args.out, "p%02d.png" % page)
        rc = subprocess.call([sys.executable, os.path.join(HERE, "shot.py"),
                              shot], cwd=ROOT, env=env)
        proc.terminate()
        try:
            proc.wait(timeout=4)
        except subprocess.TimeoutExpired:
            proc.kill()
        if rc != 0:
            print("  截图失败", flush=True)
        time.sleep(1.0)

    print("\n全部完成，输出目录：%s" % args.out)
    return 0


if __name__ == "__main__":
    sys.exit(main())
