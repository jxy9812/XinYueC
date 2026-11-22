# -*- coding: utf-8 -*-
"""网页 16 页移动端截图（verify/webui/web_sweep_mobile.py，Edge headless）。

用法
====

    python verify/webui/web_sweep_mobile.py            # 默认 390x844 竖屏
    python verify/webui/web_sweep_mobile.py --size 360,780
    python verify/webui/web_sweep_mobile.py --size 1280,800 --out shots/web_desktop

职责与口径（与 web_sweep_edge.py 同款，只换视口）
==================================================

1. **自管前置**：C sim（6100）与 webui（8085）探活——在跑直接用，不在才
   后台拉起并等就绪。**绝不停 webui**（它就是被截图的对象）。
2. **登录拿 token**：POST /api/login（root/root）→ JSON 的 ``token`` 字段。
   重试 5 次（C 服务端回收旧会话的窗口期会瞬时拒绝）。
3. **逐页 spawn Edge**（每页一个独立进程，串行）：
       msedge --headless=new --disable-gpu --no-first-run --hide-scrollbars
              --user-data-dir=<临时目录> --screenshot=<out>
              --window-size=<W>,<H> --virtual-time-budget=8000
              "http://127.0.0.1:8085/?authtoken=<tok>#/page/N"
   **每页独立 --user-data-dir**（共用 profile 时第二发不落盘）。
4. **判据**：缺图、文件 <15KB、不同页逐字节相同（= authtoken 直登没完成
   拍到的登录遮罩）都算败。
5. **不动常驻服务**：跑完 C sim 与 webui 原样在跑。

输出：verify/webui/shots/web_m/pNN.png（390x844）。
"""

import hashlib
import json
import os
import shutil
import subprocess
import sys
import tempfile
import time
import urllib.request

HERE = os.path.dirname(os.path.abspath(__file__))
REPO_ROOT = os.path.dirname(os.path.dirname(HERE))          # F:/Code/CMake/XinYueC
BIN_DIR = os.path.join(REPO_ROOT, "bin")
WEBUI_DIR = os.path.join(REPO_ROOT, "HighSpeedFilter", "webui_py")

HOST, C_PORT, WEB_PORT = "127.0.0.1", 6100, 8085
#: 页数（2026-11-13 三页合并后 18 → 16）。
PAGE_COUNT = 16
#: 小于 15KB 的截图 = 白板/黑板（正常页面远大于此）。
BLANK_BYTES = 15 * 1024
EDGE_TIMEOUT_S = 30
#: 启动器退出后等分离浏览器落盘 PNG 的上限。
SHOT_WAIT_S = 45

_EDGE_CANDIDATES = [
    r"C:/Program Files (x86)/Microsoft/Edge/Application/msedge.exe",
    r"C:/Program Files/Microsoft/Edge/Application/msedge.exe",
]


def parse_args(argv):
    size = "390,844"
    out = "web_m"
    pages = None
    i = 0
    while i < len(argv):
        a = argv[i]
        if a == "--size" and i + 1 < len(argv):
            size = argv[i + 1]; i += 2
        elif a == "--out" and i + 1 < len(argv):
            out = argv[i + 1]; i += 2
        elif a == "--pages" and i + 1 < len(argv):
            pages = [int(x) for x in argv[i + 1].split(",") if x.strip() != ""]
            i += 2
        else:
            i += 1
    w, h = size.split(",")
    return int(w), int(h), out, pages


def find_edge():
    for path in _EDGE_CANDIDATES:
        if os.path.isfile(path):
            return path
    return None


def tcp_open(host, port, timeout=1.5):
    import socket
    try:
        sock = socket.create_connection((host, port), timeout=timeout)
        sock.close()
        return True
    except OSError:
        return False


def wait_tcp(host, port, timeout_s, what):
    deadline = time.time() + timeout_s
    while time.time() < deadline:
        if tcp_open(host, port):
            return True
        time.sleep(0.5)
    print("[web_m] 等待 %s (%s:%d) 超时" % (what, host, port), file=sys.stderr)
    return False


def ensure_c_sim():
    """C 模拟服务端探活：在跑直接用，不在才后台拉起（cwd 必须在 bin/）。"""
    if tcp_open(HOST, C_PORT):
        print("[web_m] C sim 已在跑（127.0.0.1:%d），直接复用" % C_PORT)
        return True
    print("[web_m] C sim 不在，后台拉起 bin/HsfServer.exe --sim …")
    exe = os.path.join(BIN_DIR, "HsfServer.exe")
    if not os.path.isfile(exe):
        print("[web_m] 找不到 %s" % exe, file=sys.stderr)
        return False
    with open(os.devnull, "wb") as devnull:
        subprocess.Popen([exe, "--sim"], cwd=BIN_DIR,
                         stdout=devnull, stderr=devnull)
    if not wait_tcp(HOST, C_PORT, 40, "C sim"):
        return False
    time.sleep(3)
    print("[web_m] C sim 已就绪")
    return True


def ensure_webui():
    """网页服务器探活：在跑直接用，不在才后台拉起 python -m hsfweb。"""
    if tcp_open(HOST, WEB_PORT):
        print("[web_m] webui 已在跑（127.0.0.1:%d），直接复用" % WEB_PORT)
        return True
    python = os.environ.get("PYTHON", "") or "python"
    print("[web_m] webui 不在，后台拉起 %s -m hsfweb（cwd=%s）…" % (python, WEBUI_DIR))
    with open(os.devnull, "wb") as devnull:
        subprocess.Popen([python, "-m", "hsfweb"], cwd=WEBUI_DIR,
                         stdout=devnull, stderr=devnull)
    if not wait_tcp(HOST, WEB_PORT, 25, "webui"):
        return False
    time.sleep(2)
    print("[web_m] webui 已就绪（http://127.0.0.1:%d）" % WEB_PORT)
    return True


def web_login():
    """POST /api/login（root/root）→ token。重试扛会话回收窗口期。"""
    url = "http://%s:%d/api/login" % (HOST, WEB_PORT)
    payload = json.dumps({"user": "root", "password": "root"}).encode("utf-8")
    last_err = ""
    for attempt in range(5):
        try:
            req = urllib.request.Request(
                url, data=payload, method="POST",
                headers={"Content-Type": "application/json"})
            with urllib.request.urlopen(req, timeout=15) as resp:
                doc = json.loads(resp.read().decode("utf-8"))
            token = doc.get("token", "")
            if token:
                return token
            last_err = "应答无 token：%r" % (doc.get("error"),)
        except Exception as exc:                         # noqa: BLE001
            last_err = str(exc)
        print("[web_m] 登录失败（第 %d 次）：%s" % (attempt + 1, last_err),
              file=sys.stderr)
        time.sleep(2)
    print("[web_m] 登录最终失败：%s" % last_err, file=sys.stderr)
    return ""


def web_logout(token):
    try:
        req = urllib.request.Request(
            "http://%s:%d/api/logout" % (HOST, WEB_PORT), data=b"",
            method="POST", headers={"Authorization": "Bearer " + token})
        urllib.request.urlopen(req, timeout=5).read()
    except Exception:                                    # noqa: BLE001
        pass


def upstream_ready(token, timeout_s=30.0):
    """上游就绪门（照搬 web_sweep_edge.py 同款：ready/known + 数据面活值 + config）。"""
    url = "http://%s:%d/api/state" % (HOST, WEB_PORT)
    cfg_url = "http://%s:%d/api/config" % (HOST, WEB_PORT)
    fm_url = "http://%s:%d/api/flowmeter/snapshot" % (HOST, WEB_PORT)
    tb_url = "http://%s:%d/api/turbidity/snapshot" % (HOST, WEB_PORT)

    def _authed(u, method="GET"):
        return urllib.request.Request(
            u, headers={"Authorization": "Bearer " + token},
            data=b"" if method == "POST" else None, method=method)

    def _get_json(req):
        with urllib.request.urlopen(req, timeout=8) as resp:
            return json.loads(resp.read().decode("utf-8"))

    def _num(v):
        return isinstance(v, (int, float)) and not isinstance(v, bool)

    deadline = time.time() + timeout_s
    last = ""
    while time.time() < deadline:
        try:
            doc = _get_json(_authed(url))
            link = doc.get("link") or {}
            mode = doc.get("mode") or {}
            th = doc.get("temphum") or {}
            basic_ok = (link.get("ready") is True and bool(mode.get("known"))
                        and _num(th.get("tempC")))
            values_ok = False
            cfg_ok = False
            if basic_ok:
                _get_json(_authed(fm_url, "POST"))
                _get_json(_authed(tb_url, "POST"))
                doc = _get_json(_authed(url))
                fm = doc.get("flowmeter") or {}
                tb = doc.get("turbidity") or {}
                items = tb.get("items") or []
                turb1 = (items[0] or {}).get("ntu") if items else None
                values_ok = (fm.get("fresh") is True and _num(fm.get("flow"))
                             and fm.get("flow") > 0
                             and tb.get("fresh") is True
                             and _num(turb1) and turb1 > 0)
            if values_ok:
                cfg_ok = _get_json(_authed(cfg_url)).get("ok") is True
            if basic_ok and values_ok and cfg_ok:
                print("[web_m] 上游已就绪")
                return True
            last = ("link=%r mode=%r tempC=%r flow=%r turb1=%r cfg=%r"
                    % (link.get("ready"), mode.get("known"), th.get("tempC"),
                       (doc.get("flowmeter") or {}).get("flow"),
                       (((doc.get("turbidity") or {}).get("items") or [{}])[0]
                        .get("ntu")), cfg_ok))
        except Exception as exc:                         # noqa: BLE001
            last = str(exc)
        time.sleep(0.5)
    print("[web_m] 上游就绪门超时(%ds)：%s" % (int(timeout_s), last), file=sys.stderr)
    return False


def clear_old_shots(shot_dir):
    if not os.path.isdir(shot_dir):
        os.makedirs(shot_dir, exist_ok=True)
        return
    for name in os.listdir(shot_dir):
        if name.startswith("p") and name.endswith(".png"):
            try:
                os.remove(os.path.join(shot_dir, name))
            except OSError:
                pass


def shoot_page(edge, token, page_index, shot_dir, width, height):
    """spawn 一发 Edge headless 截第 page_index 页；返回 (ok, bytes, note)。"""
    tag = "p%02d" % page_index
    out_png = os.path.join(shot_dir, tag + ".png")
    profile = tempfile.mkdtemp(prefix="edge_m_%s_" % tag)
    url = ("http://%s:%d/?authtoken=%s#/page/%d"
           % (HOST, WEB_PORT, token, page_index))
    cmd = [
        edge,
        "--headless=new",
        "--disable-gpu",
        "--no-first-run",
        "--hide-scrollbars",
        "--user-data-dir=" + profile,
        "--screenshot=" + out_png,
        "--window-size=%d,%d" % (width, height),
        "--virtual-time-budget=8000",
        url,
    ]
    try:
        proc = subprocess.Popen(cmd, stdout=subprocess.DEVNULL,
                                stderr=subprocess.DEVNULL)
        try:
            proc.wait(timeout=EDGE_TIMEOUT_S)
        except subprocess.TimeoutExpired:
            subprocess.run(["taskkill", "/F", "/T", "/PID", str(proc.pid)],
                           capture_output=True)
            proc.wait()
            return False, 0, "超时(%ds)" % EDGE_TIMEOUT_S
        deadline = time.time() + SHOT_WAIT_S
        while time.time() < deadline and not os.path.isfile(out_png):
            time.sleep(0.3)
        if not os.path.isfile(out_png):
            return False, 0, "未落盘(启动器已退，等 %ds 无文件)" % SHOT_WAIT_S
        time.sleep(1.0)
        size = -1
        for _ in range(3):
            try:
                size = os.path.getsize(out_png)
                break
            except OSError:
                time.sleep(0.5)
        if size < 0:
            return False, 0, "落盘后消失(并发清扫竞态)"
        if size < BLANK_BYTES:
            return False, size, "空白(%d bytes)" % size
        return True, size, ""
    finally:
        shutil.rmtree(profile, ignore_errors=True)


def main():
    width, height, out_name, pages = parse_args(sys.argv[1:])
    shot_dir = os.path.join(HERE, "shots", out_name)
    edge = find_edge()
    if edge is None:
        print("[web_m] 找不到 msedge.exe（%s）" % _EDGE_CANDIDATES, file=sys.stderr)
        return 4
    if not ensure_c_sim():
        return 2
    if not ensure_webui():
        return 2

    token = web_login()
    if not token:
        return 3
    if not upstream_ready(token):
        web_logout(token)
        token = web_login()
        if not token or not upstream_ready(token):
            return 5

    clear_old_shots(shot_dir)
    todo = pages if pages else list(range(PAGE_COUNT))
    print("[web_m] 已登录（token 前 8 位 %s…），扫 %d 页 %dx%d → %s"
          % (token[:8], len(todo), width, height, shot_dir))

    failures = []
    seen_md5 = {}
    for i in todo:
        ok, size, note = shoot_page(edge, token, i, shot_dir, width, height)
        tag = "p%02d" % i
        if ok:
            md5 = hashlib.md5(
                open(os.path.join(shot_dir, tag + ".png"), "rb").read()).hexdigest()
            dup = seen_md5.get(md5)
            seen_md5[md5] = tag
            if dup is not None:
                ok = False
                note = "与 %s 逐字节相同——登录遮罩指纹" % dup
        if ok:
            print("[web_m] %s ok %d bytes" % (tag, size))
        else:
            failures.append("%s(%s)" % (tag, note))
            print("[web_m] %s 失败：%s" % (tag, note), file=sys.stderr)

    web_logout(token)
    if failures:
        print("[web_m] 缺图/空白：%s" % ", ".join(failures), file=sys.stderr)
        return 1
    print("[web_m] 完成：%d 张齐全 → %s" % (len(todo), shot_dir))
    return 0


if __name__ == "__main__":
    sys.exit(main())
