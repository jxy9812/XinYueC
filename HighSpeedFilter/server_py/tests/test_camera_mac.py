# -*- coding: utf-8 -*-
"""相机按 MAC 连接 + 设备健康 + 可配断线重连（2026-11-20 用户口径回归锁）。

[锁什么]
  A. MAC 归一/解析（ARP 表命中 / 网段探测补表 / 未解析到）
  B. 协议面 fail-closed（连接方式/MAC/重连间隔的非法值明确拒）
  C. 相机进设备健康（在线/离线条目 + MAC 地址列）
  D. 断线重连语义（关=停止重试置错误态；开=按间隔继续）
"""
import os
import sys

for _stream in (sys.stdout, sys.stderr):
    try:
        _stream.reconfigure(encoding="utf-8", errors="replace")
    except (AttributeError, ValueError, OSError):
        pass
import tempfile
import shutil
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))

from hsfsrv import proto                                     # noqa: E402
from hsfsrv import camera_mac                                # noqa: E402
from hsfsrv.camera import CameraCollector                    # noqa: E402
from fake_devices import build_fake_app                      # noqa: E402
from hsfsrv.config import Config                             # noqa: E402

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


def section(title):
    print("\n=== %s ===" % title)


def _rig(basedir):
    cfg = Config()
    cfg.resolve_paths(basedir)
    cfg.config_path = os.path.join(basedir, "cfg", "x.json")
    cfg.users_path = os.path.join(basedir, "cfg", "u.json")
    os.makedirs(os.path.join(basedir, "cfg"), exist_ok=True)
    cfg.port = 0
    cfg.enable_serial = False
    business = build_fake_app(cfg, log=lambda m: None)
    business.store.insert_alarm = lambda *a, **k: True
    return business, cfg


def test_a_normalize_and_resolve():
    section("A MAC 归一与解析")
    # A1 归一：冒号/连字符/裸 hex → 小写冒号
    for raw, want in (("AA:BB:CC:DD:EE:FF", "aa:bb:cc:dd:ee:ff"),
                      ("AA-BB-CC-DD-EE-FF", "aa:bb:cc:dd:ee:ff"),
                      # [点分宽容] Cisco 风格 aabb.ccdd.eeff 也认（适配更多
                      # 传感器/交换机抄下来的写法）。
                      ("aabb.ccdd.eeff", "aa:bb:cc:dd:ee:ff"),
                      ("aabbccddeeff", "aa:bb:cc:dd:ee:ff"),
                      ("AA:BB:CC:DD:EE", None),          # 5 段不够
                      ("zz:bb:cc:dd:ee:ff", None),       # 非 hex
                      ("", None)):
        got = camera_mac.normalize_mac(raw)
        check("A1 归一 %r → %r" % (raw, want), got == want,
              "got=%r" % (got,))

    # A2 表命中（大小写/连字符与 ARP 表写法互认）
    state = {"table": {"aa:bb:cc:dd:ee:ff": "192.168.1.64"}}
    ip = camera_mac.resolve_mac_ip(
        "AA-BB-CC-DD-EE-FF", sweep=False, arp_fn=lambda: dict(state["table"]))
    check("A2 表命中（ARP 写法 AA-BB-…）", ip == "192.168.1.64", "ip=%r" % ip)

    # A3 未命中 + 不扫 ⇒ None
    state["table"] = {}
    ip = camera_mac.resolve_mac_ip("aa:bb:cc:dd:ee:ff", sweep=False,
                                   arp_fn=lambda: dict(state["table"]))
    check("A3 表未命中且不扫 ⇒ None", ip is None, "ip=%r" % ip)

    # A4 未命中 + 扫 ⇒ 探测后重查（种子/探测次数都收到）
    sweeps = []

    def _fake_sweep(seed):
        sweeps.append(seed)
        state["table"]["aa:bb:cc:dd:ee:ff"] = "192.168.1.77"
        return 1

    # [多网段 2026-11-20] 现在会按「seed 优先 + 本机各网段」逐个扫，
    # 且解析结果要过可达性验证 ⇒ 注入 probe（假 IP 真连不上）。
    ip = camera_mac.resolve_mac_ip(
        "aa:bb:cc:dd:ee:ff", seed_ip="192.168.1.10", sweep=True,
        arp_fn=lambda: dict(state["table"]), sweep_fn=_fake_sweep,
        ipv4s_fn=lambda: ["192.168.1.74"], probe_fn=lambda _ip: True)
    check("A4 探测后命中：ip=192.168.1.77", ip == "192.168.1.77",
          "ip=%r sweeps=%r" % (ip, sweeps))
    check("A4b 第一个扫描种子 = 上次/配置给的网段 IP（其余本机网段跟进）",
          sweeps and sweeps[0] == "192.168.1.10", "sweeps=%r" % (sweeps,))

    # ---- A4c 相机换到**另一个本机网段**（换网线实锅）：seed 那个网段
    #      扫空，但本机另一个网段能扫到 ⇒ 必须命中（不能只看配置 IP 网段）。
    state["table"] = {}
    sweeps.clear()

    def _fake_sweep2(seed):
        sweeps.append(seed)
        if seed.startswith("10.168.1."):
            state["table"]["aa:bb:cc:dd:ee:ff"] = "10.168.1.101"
        return 1

    ip = camera_mac.resolve_mac_ip(
        "aa:bb:cc:dd:ee:ff", seed_ip="192.168.1.181", sweep=True,
        arp_fn=lambda: dict(state["table"]), sweep_fn=_fake_sweep2,
        ipv4s_fn=lambda: ["192.168.1.181", "10.168.1.100"],
        probe_fn=lambda _ip: True)
    check("A4c 相机换网段（seed 扫空，本机另一网段命中）⇒ 解析成功",
          ip == "10.168.1.101" and "10.168.1.100" in sweeps,
          "ip=%r sweeps=%r" % (ip, sweeps))

    # A5 MAC 没填 ⇒ None（不扫不打网）
    ip = camera_mac.resolve_mac_ip("", sweep=True, arp_fn=lambda: {})
    check("A5 空 MAC ⇒ None（不打网）", ip is None, "ip=%r" % ip)


def test_b_config_validation():
    section("B 协议面 fail-closed")
    basedir = tempfile.mkdtemp(prefix="hsf_cammac_")
    try:
        business, cfg = _rig(basedir)

        # B1 连接方式：合法值收下；野值拒
        v = business._cfg_camera_value(proto.CFGK_CAMERA_RESOLVE_MODE, "mac")
        check("B1 resolveMode=mac 收下", v == "mac", "v=%r" % (v,))
        for bad in ("ethernet", "", None, 1):
            try:
                business._cfg_camera_value(
                    proto.CFGK_CAMERA_RESOLVE_MODE, bad)
                ok = False
            except ValueError:
                ok = True
            check("B1b resolveMode 野值 %r 拒" % (bad,), ok)

        # B2 MAC：三种写法归一；非法拒
        v = business._cfg_camera_value(proto.CFGK_CAMERA_MAC,
                                       "AA-BB-CC-DD-EE-FF")
        check("B2 MAC 连字符写法 → 小写冒号",
              v == "aa:bb:cc:dd:ee:ff", "v=%r" % (v,))
        for bad in ("nope", "AA:BB:CC:DD:EE", 12345):
            try:
                business._cfg_camera_value(proto.CFGK_CAMERA_MAC, bad)
                ok = False
            except ValueError:
                ok = True
            check("B2b MAC 非法 %r 拒" % (bad,), ok)

        # B3 重连开关/间隔：bool 与范围
        v = business._cfg_camera_value(
            proto.CFGK_CAMERA_RECONNECT_ENABLED, False)
        check("B3 reconnectEnabled=False 收下", v is False, "v=%r" % (v,))
        v = business._cfg_camera_value(
            proto.CFGK_CAMERA_RECONNECT_INTERVAL_S, 30)
        check("B3b interval=30 收下", v == 30, "v=%r" % (v,))
        for bad in ("x", 0, 9999):
            try:
                business._cfg_camera_value(
                    proto.CFGK_CAMERA_RECONNECT_INTERVAL_S, bad)
                ok = False
            except ValueError:
                ok = True
            check("B3c interval 野值 %r 拒" % (bad,), ok)

        # B4 GET 文档键齐（含四新键 + 缺省）
        cfg.camera_resolve_mode = "mac"
        cfg.camera_mac = "aa:bb:cc:dd:ee:ff"
        cfg.camera_reconnect_enabled = False
        cfg.camera_reconnect_interval_s = 12
        doc = business._camera_settings()
        check("B4 _camera_settings 四新键齐（mac/12s/关）",
              doc.get(proto.CFGK_CAMERA_RESOLVE_MODE) == "mac"
              and doc.get(proto.CFGK_CAMERA_MAC) == "aa:bb:cc:dd:ee:ff"
              and doc.get(proto.CFGK_CAMERA_RECONNECT_ENABLED) is False
              and doc.get(proto.CFGK_CAMERA_RECONNECT_INTERVAL_S) == 12,
              "%r" % ({k: doc.get(k) for k in
                       ("resolveMode", "mac", "reconnectEnabled",
                        "reconnectIntervalS")},))
    finally:
        shutil.rmtree(basedir, ignore_errors=True)


def test_c_health_entry():
    section("C 相机进设备健康")
    basedir = tempfile.mkdtemp(prefix="hsf_cammac_h_")
    try:
        business, cfg = _rig(basedir)
        # 相机启用 + 在线状态注入（collect_health 只读缓存不发 IO）
        cfg.camera_enabled = True
        cfg.camera_resolve_mode = "mac"
        cfg.camera_mac = "aa:bb:cc:dd:ee:ff"
        business._camera_status = {"online": True, "frames": 1234,
                                   "error": "", "transport": "stream",
                                   "resolvedIp": "192.168.1.77"}
        entries = business.collect_health(1000)
        cam = [e for e in entries if e.name == "网络相机"]
        check("C1 在线：健康清单含「网络相机」（KIND_CAMERA=8）",
              len(cam) == 1 and cam[0].kind == proto.KIND_CAMERA,
              "entries=%r" % ([(e.name, e.kind) for e in entries],))
        if cam:
            e = cam[0]
            # [2026-11-25 板端日志实锅] 健康 text 里不能放"已收 N 帧"这种
            # **单调计数器**：5fps 的相机会让健康快照每拍都判成"变化" ⇒
            # 每 500ms 推一帧 + 落一条运行日志（板端 3 小时 3000+ 条）。
            # 帧数在「远端设置 → 相机」卡与相机页都能看。
            check("C2 在线条目：健康=正常 + 健康文案不含单调帧数",
                  e.online is True and e.health == proto.HEALTH_OK
                  and e.text == "",
                  "health=%r text=%r" % (e.health, e.text))
            check("C3 MAC 模式地址列 = MAC → 解析 IP",
                  "MAC aa:bb:cc:dd:ee:ff" in e.addr
                  and "→" in e.addr,
                  "addr=%r" % (e.addr,))

        # 离线：错误文案进条目
        business._camera_status = {"online": False, "frames": 0,
                                   "error": "连接超时", "transport": "stream"}
        entries = business.collect_health(2000)
        cam = [e for e in entries if e.name == "网络相机"]
        check("C4 离线：健康=离线 + 错误文案",
              cam and cam[0].health == proto.HEALTH_OFFLINE
              and "连接超时" in cam[0].text,
              "health=%r text=%r"
              % (cam[0].health if cam else None,
                 cam[0].text if cam else None))

        # 相机没启用 ⇒ 条目仍在，标「已停用」（2026-11-20 用户口径
        # 「相机的连接情况加进入也没做」——不启用也出条目，与浊度仪
        # 逐台停用同口径，现场才分得清"没这功能"还是"没配"）。
        cfg.camera_enabled = False
        business._camera_status = {"online": False, "frames": 0,
                                   "error": "", "transport": "stream"}
        entries = business.collect_health(3000)
        cam = [e for e in entries if e.name == "网络相机"]
        check("C5 未启用相机：条目仍在（健康=离线 + 文案「已停用」）",
              cam and cam[0].health == proto.HEALTH_OFFLINE
              and cam[0].text == "已停用" and cam[0].online is False,
              "health=%r text=%r"
              % (cam[0].health if cam else None,
                 cam[0].text if cam else None))
    finally:
        shutil.rmtree(basedir, ignore_errors=True)


def test_d_reconnect_semantics():
    section("D 断线重连语义（采集器）")
    # D1 重连关：_wait_reconnect 返回 False + 状态置「已断线（自动重连…）」
    col = CameraCollector({"enabled": True, "ip": "192.168.1.64",
                           "reconnectEnabled": False,
                           "reconnectIntervalS": 5})
    got = col._wait_reconnect({"enabled": True, "ip": "192.168.1.64",
                               "reconnectEnabled": False,
                               "reconnectIntervalS": 5})
    st = col.status()
    check("D1 重连关 ⇒ 停止重试（False）+ 状态可读",
          got is False and "已断线" in st.get("error", "")
          and "重连" in st.get("error", ""),
          "got=%r err=%r" % (got, st.get("error")))

    # D2 重连开：按间隔等待后返回 True（间隔 1s，实测 ~1s）
    col2 = CameraCollector({"enabled": True, "ip": "192.168.1.64",
                            "reconnectEnabled": True,
                            "reconnectIntervalS": 1})
    t0 = time.time()
    got2 = col2._wait_reconnect({"enabled": True, "ip": "192.168.1.64",
                                 "reconnectEnabled": True,
                                 "reconnectIntervalS": 1})
    dt = time.time() - t0
    check("D2 重连开 ⇒ 等一个间隔后继续（interval 0 夹 1s）",
          got2 is True and 0.8 <= dt < 3.0,
          "got=%r dt=%.2fs" % (got2, dt))

    # D3 status() 带 resolvedIp 键（MAC 模式回显通道存在）
    col3 = CameraCollector({"enabled": False, "resolveMode": "mac",
                            "mac": "aa:bb:cc:dd:ee:ff"})
    st3 = col3.status()
    check("D3 status 带 resolvedIp（缺省空串）",
          "resolvedIp" in st3 and st3.get("resolvedIp") == "",
          "resolvedIp=%r" % (st3.get("resolvedIp"),))

    # D4 解析注入：模拟「解析到 IP 后用它连」——直接打 collector 内部状态
    col3._resolved_ip = "192.168.1.77"
    check("D4 解析结果进状态（resolvedIp=192.168.1.77）",
          col3.status().get("resolvedIp") == "192.168.1.77",
          "resolvedIp=%r" % (col3.status().get("resolvedIp"),))


def test_e_silent_timeout_and_gbk_arp():
    """E1~E3：RTSP 静默判死（板端实锅）+ Windows arp 的 GBK 解码（实锅）。

    [E 段锁的是什么] 2026-11-20 现场「重启相机后画面就没了、断线重连没用」：
    ① `read_es` 的 `except socket.timeout: continue` 是**永久空转**（TCP
       半死时收不到 FIN/RST，recv 一直超时但永远不判死）⇒ 采集线程挂死、
       重连循环轮不到；
    ② Windows `arp -a` 输出 GBK，`text=True` 按 UTF-8 解码 ⇒ 解析恒空
       （MAC 连接在 Windows 上根本工作不了）。
    """
    import socket as _socket
    import subprocess as _sp
    from hsfsrv import camera as cam_mod

    section("E 静默判死 + GBK 解码")

    # ---- E1 连续静默超阈值 ⇒ 抛 CameraError（不再是死循环）----
    # [read_es 在 RtspClient 上] 不 connect、直接塞假 sock（只测读循环）。
    col = cam_mod.RtspClient("1.2.3.4", 554, "", "", "stream2")

    class _DeadSock(object):
        """模拟 TCP 半死：recv 永远超时（收不到数据也不 EOF）。"""
        def recv(self, _n):
            raise _socket.timeout("timed out")

    col.sock = _DeadSock()
    saved = cam_mod.RTSP_SILENT_TIMEOUT_S
    cam_mod.RTSP_SILENT_TIMEOUT_S = 0.2      # 缩短阈值加速用例
    try:
        err = ""
        try:
            col.read_es(lambda _b: None, lambda: False)
        except cam_mod.CameraError as exc:
            err = str(exc)
        check("E1 连续静默超阈值 ⇒ 抛 CameraError（不再永久空转）",
              "静默超时" in err, "err=%r" % (err,))
    finally:
        cam_mod.RTSP_SILENT_TIMEOUT_S = saved

    # ---- E2 阈值内收到字节 ⇒ silent_since 刷新（判死靠"连续"无数据）----
    col2 = cam_mod.RtspClient("1.2.3.4", 554, "", "", "stream2")
    frames = {"n": 0}

    class _FreshSock(object):
        """先给一帧 RTP 再静默：第一轮不算静默（silent_since 被刷新）。"""
        def __init__(self):
            self.first = True

        def recv(self, _n):
            if self.first:
                self.first = False
                # 12B RTP 头 + 1B NAL（type 1 = non-IDR slice）
                return b"$" + bytes([0, 0, 0, 13]) + bytes(
                    [0x80, 0x60, 0, 1, 0, 0, 0, 0, 0, 0, 0, 1]) + bytes([0x41])
            raise _socket.timeout("timed out")

    col2.sock = _FreshSock()
    cam_mod.RTSP_SILENT_TIMEOUT_S = 0.2
    try:
        try:
            col2.read_es(lambda _b: None, lambda: False)
            err2 = ""
        except cam_mod.CameraError as exc:
            err2 = str(exc)
        check("E2 阈值内收到过数据 ⇒ 依然判死（静默是「连续」无数据）",
              "静默超时" in err2, "err=%r" % (err2,))
    finally:
        cam_mod.RTSP_SILENT_TIMEOUT_S = saved

    # ---- E3 stop_flag=True ⇒ 立刻返回，不抛错（正常停止路径）----
    col3 = cam_mod.RtspClient("1.2.3.4", 554, "", "", "stream2")
    col3.sock = _DeadSock()
    raised = False
    try:
        col3.read_es(lambda _b: None, lambda: True)
    except cam_mod.CameraError:
        raised = True
    check("E3 stop_flag 为真 ⇒ 立即返回（正常停止不报错）",
          raised is False, "raised=%r" % (raised,))

    # ---- E4 Windows arp GBK 解码：GBK 字节也能解析出 MAC→IP ----
    _nl = chr(13) + chr(10)
    gbk_out = ("接口: 192.168.1.74 --- 0x5" + _nl
               + "  Internet 地址         物理地址              类型" + _nl
               + "  192.168.1.181         f8-c9-03-5e-1c-5a     动态"
               + _nl).encode("gbk")

    class _Out(object):
        stdout = gbk_out

    _real_run = _sp.run
    _sp.run = lambda *a, **k: _Out()
    try:
        table = camera_mac._arp_table_windows()
    finally:
        _sp.run = _real_run
    check("E4 Windows arp 输出为 GBK ⇒ 仍能解析（f8:c9:→192.168.1.181）",
          table.get("f8:c9:03:5e:1c:5a") == "192.168.1.181",
          "table=%r" % (table,))


def main():
    test_a_normalize_and_resolve()
    test_b_config_validation()
    test_c_health_entry()
    test_d_reconnect_semantics()
    test_e_silent_timeout_and_gbk_arp()
    print("=" * 62)
    print("  汇总：PASS %d / FAIL %d" % (PASS, FAIL))
    return 0 if FAIL == 0 else 1


if __name__ == "__main__":
    sys.exit(main())
