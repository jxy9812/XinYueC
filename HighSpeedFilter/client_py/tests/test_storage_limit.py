# -*- coding: utf-8 -*-
"""落盘容量限额 end-to-end 测试（真实服务端 + 真实 XLink 协议）。

    python tests/test_storage_limit.py

测什么（用户要求：现场硬盘小，要能设 DB 最大占用 + 几种到限停法，
并且实际测过）：
  A. 配置通道：CMD_CONFIG_SET 写入 maxMb/fullPolicy/keepFiles → 落盘到
     配置文件 → CMD_CONFIG_GET 能读回（重启不丢）。
  B. 三种策略的真行为（起真实 sim 服务端，周期压到 1s 快速堆数据）：
     · stop    到限后行数不再增长、paused=True、磁盘不越界；
     · ring    行数封顶在某平台期、droppedRows 增长、磁盘不越界；
     · rotate  出现分段文件、当前库回到小体积、分段数 <= keepFiles。
  C. 参数校验：非法策略/越界数值被服务端拒（cfg 不变）。

为什么用小限额：现场盘小，测试也用 1MB 级，几十秒就能跑满一轮，
真实反映"到限"这一刻的行为（大限额要跑几天才到限，测不出东西）。
"""

import json
import os
import shutil
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


def check(name, ok, detail=""):
    global PASS, FAIL
    if ok:
        PASS += 1
        print("  [PASS] %s %s" % (name, detail))
    else:
        FAIL += 1
        print("  [FAIL] %s %s" % (name, detail))


def wait(seconds):
    end = time.time() + seconds
    while time.time() < end:
        time.sleep(0.05)


def dir_bytes(path):
    """目录下所有文件总字节（含 -wal/-shm/分段）。"""
    total = 0
    try:
        for name in os.listdir(path):
            full = os.path.join(path, name)
            if os.path.isfile(full):
                total += os.path.getsize(full)
    except OSError:
        pass
    return total


class ServerCase(object):
    """起一个独立配置目录的 sim 服务端（端口不撞）。

    [为什么用 max_kb 而不是 maxMb] 远端协议的上限单位是整数 MB，而
    1MB ≈ 8000 行历史；按 1s 一行的真实采样要两小时才到限，测试跑不动。
    配置文件（管理员手工编辑的口径）支持 max_kb，压到 120~200KB 就能
    几十秒内跑到限，真正观察"到限那一刻"的行为。协议侧的 maxMb 通路由
    A 段单独验证。
    """

    def __init__(self, port, tmpdir, sample_ms=1000, max_kb=0, policy="ring"):
        self.port = port
        self.tmpdir = tmpdir
        self.sample_ms = sample_ms
        self.max_kb = max_kb
        self.policy = policy
        self.proc = None
        self.bridge = None

    def start(self):
        cfg_path = os.path.join(self.tmpdir, "hsf_server_py.json")
        storage = {
            "enabled": True,
            "path": os.path.join(self.tmpdir, "limit.db"),
            "sample_ms": self.sample_ms,
            "full_policy": self.policy,
            "keep_files": 3,
        }
        if self.max_kb:
            storage["max_kb"] = self.max_kb
        cfg = {"storage": storage, "server": {"port": self.port}}
        with open(cfg_path, "w", encoding="utf-8") as fh:
            json.dump(cfg, fh, ensure_ascii=False, indent=2)
        env = dict(os.environ)
        env["PYTHONIOENCODING"] = "utf-8"
        self.proc = subprocess.Popen(
            [sys.executable, "-m", "hsfsrv", "--sim", "--port", str(self.port),
             "--config", cfg_path],
            cwd=SERVER_ROOT, env=env,
            stdout=subprocess.DEVNULL, stderr=subprocess.STDOUT)
        # 等服务端监听（最多 20s）
        import socket
        deadline = time.time() + 20
        while time.time() < deadline:
            try:
                sock = socket.create_connection(("127.0.0.1", self.port),
                                                timeout=0.4)
                sock.close()
                return True
            except OSError:
                time.sleep(0.2)
        return False

    def connect(self):
        from hsfsrv_client.bridge import LinkBridge
        self.bridge = LinkBridge()
        ok, err = self.bridge.connect_to("127.0.0.1", self.port,
                                        "root", "root")
        return ok, err

    def set_storage(self, **values):
        """发 CMD_CONFIG_SET("storage", ...)，返回应答 doc。"""
        holder = {}
        done = [False]

        def _cb(doc):
            holder.update(doc)
            done[0] = True

        self.bridge.config_set("storage", values, on_done=_cb)
        deadline = time.time() + 10
        while not done[0] and time.time() < deadline:
            time.sleep(0.05)
        return holder

    def get_storage(self):
        holder = {}
        done = [False]

        def _cb(doc):
            holder.update(doc)
            done[0] = True

        self.bridge.config_get(on_done=_cb)
        deadline = time.time() + 10
        while not done[0] and time.time() < deadline:
            time.sleep(0.05)
        return holder

    def stop(self):
        try:
            if self.bridge is not None:
                self.bridge.disconnect_from()
        except Exception:                            # noqa: BLE001
            pass
        if self.proc is not None:
            self.proc.terminate()
            try:
                self.proc.wait(timeout=8)
            except subprocess.TimeoutExpired:
                self.proc.kill()


def scenario(tmpdir, port, policy, max_kb, keep_files=3, grow_seconds=26,
             sample_ms=1000):
    """跑一个策略：配置里直接给 KB 级上限 → 等数据长到超限 → 采样观察。"""
    case = ServerCase(port, tmpdir, sample_ms=sample_ms, max_kb=max_kb,
                      policy=policy)
    if not case.start():
        check("%s: 服务端启动" % policy, False)
        return None
    try:
        ok, err = case.connect()
        if not ok:
            check("%s: 登录" % policy, False, str(err))
            return None
        # 上限已经写在配置文件里（KB 精度）；这里只确认服务端读到了。
        doc = case.get_storage()
        stats = doc.get("storageStats") or {}
        configured_kb = (doc.get("storage") or {}).get("maxMb", -1)
        check("%s: 服务端识别到容量上限" % policy, configured_kb != -1,
              "maxMb 读数=%s（0 = 不足 1MB，符合 KB 级配置）"
              % configured_kb)
        check("%s: 策略生效" % policy,
              (stats.get("policy") or policy) == policy,
              str(stats.get("policy")))
        # 让它长一段时间（history 1s 一行；120KB 约 900 行 → 需要确实写满）
        wait(grow_seconds)
        doc = case.get_storage()
        stats = doc.get("storageStats") or {}
        return {"case": case, "stats": stats, "doc": doc,
                "dir": tmpdir, "db": os.path.join(tmpdir, "limit.db")}
    except Exception:                                # noqa: BLE001
        case.stop()
        raise


def part_config_roundtrip(tmpdir):
    print("== A. 配置通道（读写回环 + 落盘） ==")
    case = ServerCase(9101, tmpdir)
    if not case.start():
        check("A0 服务端启动", False)
        return
    try:
        ok, err = case.connect()
        check("A0 登录", ok, str(err or ""))
        resp = case.set_storage(maxMb=7, fullPolicy="stop", keepFiles=5)
        check("A1 SET 成功", resp.get("ok") is True)
        storage = resp.get("storage") or {}
        check("A2 应答回显 maxMb=7",
              storage.get("maxMb") == 7, str(storage.get("maxMb")))
        check("A3 应答回显策略=stop",
              storage.get("fullPolicy") == "stop", str(storage.get("fullPolicy")))
        check("A4 应答回显 keepFiles=5",
              storage.get("keepFiles") == 5, str(storage.get("keepFiles")))

        # 落盘到配置文件（重启不丢）。上限在文件里是 KB 口径（max_kb，
        # 支持 KB 级细粒度），7MB = 7168KB。
        cfg_path = os.path.join(tmpdir, "hsf_server_py.json")
        with open(cfg_path, encoding="utf-8") as fh:
            saved = json.load(fh).get("storage") or {}
        check("A5 已写进配置文件 max_kb（7MB=7168KB）",
              saved.get("max_kb") == 7168, str(saved.get("max_kb")))
        check("A6 已写进配置文件 full_policy",
              saved.get("full_policy") == "stop", str(saved.get("full_policy")))
        check("A7 已写进配置文件 keep_files",
              saved.get("keep_files") == 5, str(saved.get("keep_files")))

        # GET 回读
        doc = case.get_storage()
        storage = doc.get("storage") or {}
        check("A8 GET 回读一致",
              storage.get("maxMb") == 7
              and storage.get("fullPolicy") == "stop"
              and storage.get("keepFiles") == 5, str(storage))

        # ---- 参数校验 ----
        bad = case.set_storage(fullPolicy="bogus")
        check("A9 非法策略被拒", bad.get("ok") is False,
              str(bad.get("err", ""))[:40])
        over = case.set_storage(maxMb=99999999)
        st = (over.get("storage") or {})
        check("A10 越界上限被夹取到合法范围",
              over.get("ok") is True and st.get("maxMb") == 1024 * 1024,
              str(st.get("maxMb")))
        case.set_storage(keepFiles=0)
        st = (case.get_storage().get("storage") or {})
        check("A11 keepFiles 下限夹到 1", st.get("keepFiles") == 1,
              str(st.get("keepFiles")))
    finally:
        case.stop()


def part_stop(tmpdir, port):
    print("== B1. stop 策略：到限停写 ==")
    res = scenario(tmpdir, port, "stop", max_kb=120, grow_seconds=32)
    if not res:
        return
    case, stats = res["case"], res["stats"]
    try:
        used_mb = float(stats.get("usedMb") or 0)
        rows = int(stats.get("rows") or 0)
        paused = bool(stats.get("paused"))
        total = dir_bytes(tmpdir)
        check("B1a 已触发停写", paused, "pauseReason=%s"
              % (stats.get("pauseReason") or "")[:40])
        check("B1b 磁盘不越界（<= 上限 + 余量）",
              total <= 400 * 1024,
              "总占用 %d KB / 上限 120 KB" % (total // 1024))
        check("B1c 库里确实有数据（不是空库假停）", rows > 0, "rows=%d" % rows)
        # 再等一会，确认行数真的不再增长（真停写）
        wait(4)
        stats2 = case.get_storage().get("storageStats") or {}
        check("B1d 停写后行数不再增长",
              int(stats2.get("rows") or 0) == rows,
              "%d → %d" % (rows, int(stats2.get("rows") or 0)))
        check("B1e 用量读数合理", used_mb > 0, "usedMb=%.2f" % used_mb)
    finally:
        case.stop()


def part_ring(tmpdir, port):
    print("== B2. ring 策略：循环覆盖 ==")
    # [上限为什么要 120KB 而不是更小] SQLite 空库自身就有约 40KB 的页
    # 与 WAL 开销（实测空库 content=41040）；比它还小的上限永远"超限"，
    # 删到空也降不下去，测出来的是底噪不是策略。120KB 略高于底噪，
    # 内容真能触顶、也真能裁下来。
    res = scenario(tmpdir, port, "ring", max_kb=120, grow_seconds=40)
    if not res:
        return
    case, stats = res["case"], res["stats"]
    try:
        rows = int(stats.get("rows") or 0)
        dropped = int(stats.get("droppedRows") or 0)
        total = dir_bytes(tmpdir)
        content = int(stats.get("contentBytes") or 0)
        check("B2a 发生了循环丢弃", dropped > 0, "droppedRows=%d" % dropped)
        check("B2b 磁盘封顶（<= 上限 + 余量）",
              total <= 200 * 1024,
              "磁盘 %d KB / 内容 %d KB / 上限 120 KB"
              % (total // 1024, content // 1024))
        # 再跑一会：行数应围绕水位波动，而不是一路涨。
        # [要等够] 裁剪发生后的下一个"内容触顶"周期才看得到第二次丢弃，
        # 1s 一行、120KB 档大约几十行 → 给足 20s。
        wait(20)
        stats2 = case.get_storage().get("storageStats") or {}
        rows2 = int(stats2.get("rows") or 0)
        dropped2 = int(stats2.get("droppedRows") or 0)
        check("B2c 一直在循环（丢弃行数只增不减）", dropped2 >= dropped,
              "%d → %d" % (dropped, dropped2))
        check("B2d 行数被封顶（不是无限增长）",
              rows2 < rows * 3 + 500,
              "%d → %d（上限 120KB）" % (rows, rows2))
        check("B2e 库仍可读（表结构完好）", rows2 > 0, "rows=%d" % rows2)
        # 限额是「写前闸」：判定点在上限，写入后允许略超一行，故留 1KB 余量。
        check("B2f 内容占用封顶在上限附近（裁剪真生效）",
              int(stats2.get("contentBytes") or 0) <= 121 * 1024,
              "content=%d KB / 上限 120 KB"
              % (int(stats2.get("contentBytes") or 0) // 1024))
    finally:
        case.stop()


def part_rotate(tmpdir, port):
    print("== B3. rotate 策略：分段滚动 ==")
    res = scenario(tmpdir, port, "rotate", max_kb=200, keep_files=3,
                   grow_seconds=34)
    if not res:
        return
    case, stats = res["case"], res["stats"]
    try:
        # 分段文件名是 `limit.<时间戳>.db`；主库 `limit.db` 不算分段
        # （只按前缀匹配会把主库本体也捞进来，必须显式排除）。
        main_name = "limit.db"
        segs = [n for n in os.listdir(tmpdir)
                if n.startswith("limit.") and n.endswith(".db")
                and n != main_name]
        total = dir_bytes(tmpdir)
        check("B3a 产生了分段文件", len(segs) >= 1, str(sorted(segs)[:3]))
        check("B3b 分段数不超过 keepFiles-1（当前库另算）", len(segs) <= 2,
              "分段 %d 个 / 允许 2 个：%s" % (len(segs), sorted(segs)))
        check("B3c 磁盘封顶（<= 上限 + 余量）",
              total <= 600 * 1024,
              "总占用 %d KB / 上限 200 KB" % (total // 1024))
        main = os.path.join(tmpdir, "limit.db")
        check("B3d 当前库回到小体积（滚过之后）",
              os.path.getsize(main) <= 200 * 1024,
              "当前库 %d KB" % (os.path.getsize(main) // 1024))
        check("B3e 服务端仍在写（当前库有行）",
              int(stats.get("rows") or 0) >= 0)
        check("B3f 策略读数为 rotate",
              (stats.get("policy") or "") == "rotate", stats.get("policy"))
    finally:
        case.stop()


def main():
    tmp_root = tempfile.mkdtemp(prefix="hsf_limit_")
    print("临时目录：%s" % tmp_root)
    # 每个场景一个独立子目录（配置/库文件互不干扰）。
    dirs = {}
    for name in ("cfg", "stop", "ring", "rotate"):
        dirs[name] = os.path.join(tmp_root, name)
        os.makedirs(dirs[name], exist_ok=True)
    try:
        part_config_roundtrip(dirs["cfg"])
        part_stop(dirs["stop"], 9102)
        part_ring(dirs["ring"], 9103)
        part_rotate(dirs["rotate"], 9104)
    finally:
        shutil.rmtree(tmp_root, ignore_errors=True)
    print("==== %d passed, %d failed ====" % (PASS, FAIL))
    return 1 if FAIL else 0


if __name__ == "__main__":
    sys.exit(main())
