# -*- coding: utf-8 -*-
"""相机（camera 节 + RTSP 采集 + 分片推送）回归锁（2026-11-19）。

[测什么]
    A. **配置面**：camera 节的键/缺省/非法值 fail-closed/越界/落盘往返/
       口令不回显明文/空口令=不改。
    B. **分片协议**：片头 16B 小端七字段、单帧载荷不超 XLink 上限
       （XLINK_MAX_PAYLOAD 16KB ⇒ 片长 ≤ 12288 + 16 片头）、首片/末片标记、
       分片按序拼回与原始字节**逐字节相同**。
    C. **订阅门控**（用户 2026-11-19 口径「只在客户端打开相机页面时才推送」）：
       无订阅者 ⇒ 一帧都不推；有订阅者 ⇒ 推；同一帧不重复推；
       没人订阅时不更新"已推序号"（等有人看了要立刻补最新帧）。
    D. **采集器纯函数**：JPEG 宽高解析（SOF0/SOF2）、JPEG 流切帧
       （SOI/EOI）、质量/帧率参数进 gst 命令行（双后端）。
    E. **三端组帧器**（client_py / webui_py）——直接 import 两端的
       CameraAssembler，对同一串分片喂进去，断言输出一致（**三端同款
       规则：覆盖式丢帧 / 丢片丢整帧 / JPEG 头尾校验**）。

[怎么跑]
    python3 tests/test_camera.py            # 独立跑
    pytest tests/test_camera.py -q          # pytest 收集

[为什么不起真相机] 相机是真外部设备（现场那台 TP-LINK 之外，CI 机器上没有）
⇒ 本文件全部用**合成 JPEG + 假采集器/假服务端**驱动协议面与判定逻辑；
真机链路由 `docs/HSF相机RTSP接入_设计方案_20261119.md` 里记录的板端实测覆盖。
"""

import asyncio
import os
import struct
import sys
import tempfile

# [输出编码 2026-11-14] 中文 Windows 默认代码页 cp936 ⇒ stdout 被重定向
# 到文件时用 GBK ⇒ 打印「⇒」(U+21D2，GBK 编不了) 直接 UnicodeEncodeError
# ⇒ 整套测试在打印断言时被打断（退出码仍是 0）。强制 UTF-8。
for _stream in (sys.stdout, sys.stderr):
    try:
        _stream.reconfigure(encoding="utf-8", errors="replace")
    except (AttributeError, ValueError, OSError):
        pass

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, HERE)
sys.path.insert(0, ROOT)

from hsfsrv import camera as camera_mod       # noqa: E402
from hsfsrv import config as cfgmod           # noqa: E402
from hsfsrv import proto                      # noqa: E402
from fake_devices import build_fake_app       # noqa: E402


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


#: XLink 单帧载荷上限（与服务端 xlink.py 一致；分片总长必须严格小于它）。
XLINK_MAX_PAYLOAD = 16 * 1024


def _fake_jpeg(width=704, height=704, payload=60 * 1024):
    """造一张「形状合法」的 JPEG（SOI + SOF0 宽高 + 填充 + EOI）。

    [为什么造而不读真图] 这里测的是**分片/组帧/宽高解析**，不是编码质量 ⇒
    只要能过 `jpeg_size` 与头尾校验（FFD8…FFD9）即可，尺寸可控才测得到
    边界（1 片 / 恰好整片 / 跨片）。
    """
    body = bytearray(b"\xff\xd8")                      # SOI
    # APP0（最小合法段）
    app0 = b"\xff\xe0" + struct.pack(">H", 16) + b"JFIF\x00\x01\x01\x00" \
        + struct.pack(">HH", 1, 1) + b"\x00\x00"
    body += app0
    # SOF0：长度 17、精度 8、高、宽、3 分量
    sof = (b"\xff\xc0" + struct.pack(">H", 17) + b"\x08"
           + struct.pack(">HH", height, width) + b"\x03"
           + b"\x01\x22\x00" + b"\x02\x11\x01" + b"\x03\x11\x01")
    body += sof
    # 填充压缩数据到目标体积（避开 FFD9 字节对）
    pad = bytearray()
    while len(body) + len(pad) + 4 < max(payload, len(body) + 8):
        pad += b"\x5a"
    body += pad
    body += b"\xff\xd9"                                # EOI
    return bytes(body)


class _FakeServer(object):
    """只记发布帧的假服务端（相机推送走 publish）。"""

    def __init__(self, subscribers=1):
        self.frames = []
        self.subscribers = subscribers

    def topic_subscriber_count(self, topic_id):
        if topic_id == proto.TOPIC_CAMERA_FRAME:
            return self.subscribers
        return 0

    async def publish(self, topic, payload, data_type=None):
        self.frames.append((topic, payload, data_type))

    def camera_chunks(self):
        return [p for (t, p, _dt) in self.frames
                if t == proto.TOPIC_CAMERA_FRAME]


class _FakeCollector(object):
    """假采集器：只提供 latest_frame()（推送路径唯一依赖）。"""

    def __init__(self, frame):
        self.frame = frame
        self.started = False
        self.stopped = False
        # [transport=jpeg] 本装具模拟的是 JPEG 路的采集器（ES 直传有
        # 自己的专用断言段，见 test_camera_es）。
        self.settings = {"transport": proto.CAMERA_TRANSPORT_JPEG}

    def latest_frame(self):
        return self.frame

    def start(self):
        self.started = True

    def stop(self):
        self.stopped = True


def _rig(basedir):
    """假设备业务层（相机节不依赖任何 IO ⇒ 复用现有假设备装配）。"""
    cfg = cfgmod.Config()
    cfg.resolve_paths(basedir)
    cfg.config_path = os.path.join(basedir, "cfg", "x.json")
    cfg.users_path = os.path.join(basedir, "cfg", "hsf_users.json")
    os.makedirs(os.path.join(basedir, "cfg"), exist_ok=True)
    cfg.port = 0
    cfg.enable_serial = False
    business = build_fake_app(cfg, log=lambda m: None)
    return business, cfg


# ============================================================================
#                            A 配置面
# ============================================================================

def test_camera_config():
    """A1~A10：缺省 / 逐键写 / 非法键 / 非法值 / 越界 / 口令脱敏 / 落盘。"""
    section("A 相机配置面（camera 节）")
    basedir = tempfile.mkdtemp(prefix="hsf_camera_cfg_")
    try:
        business, cfg = _rig(basedir)
        doc = business._config_camera_doc()

        # A1 缺省：不采集 + 端口 554 + 子码流 + 704px。
        check("A1 缺省：未启用 + 端口554 + 子码流 + 704px + fps2",
              doc.get(proto.CFGK_CAMERA_ENABLED) is False
              and doc.get(proto.CFGK_CAMERA_PORT) == 554
              and doc.get(proto.CFGK_CAMERA_STREAM) == "minor"
              and doc.get(proto.CFGK_CAMERA_WIDTH) == 704
              and doc.get(proto.CFGK_CAMERA_FPS) == 2,
              "%r" % (doc,))

        # A2 逐键写生效。
        business._cfg_set_camera_key(proto.CFGK_CAMERA_ENABLED, True)
        business._cfg_set_camera_key(proto.CFGK_CAMERA_IP, "192.168.1.181")
        business._cfg_set_camera_key(proto.CFGK_CAMERA_PORT, 8554)
        business._cfg_set_camera_key(proto.CFGK_CAMERA_USERNAME, "admin")
        business._cfg_set_camera_key(proto.CFGK_CAMERA_PASSWORD, "mk123456")
        business._cfg_set_camera_key(proto.CFGK_CAMERA_STREAM, "main")
        business._cfg_set_camera_key(proto.CFGK_CAMERA_FPS, 10)
        business._cfg_set_camera_key(proto.CFGK_CAMERA_WIDTH, 1280)
        business._cfg_set_camera_key(proto.CFGK_CAMERA_QUALITY, 90)
        doc = business._config_camera_doc()
        check("A2 逐键写生效（enabled/ip/port/user/stream/fps/width/quality）",
              doc.get(proto.CFGK_CAMERA_ENABLED) is True
              and doc.get(proto.CFGK_CAMERA_IP) == "192.168.1.181"
              and doc.get(proto.CFGK_CAMERA_PORT) == 8554
              and doc.get(proto.CFGK_CAMERA_STREAM) == "main"
              and doc.get(proto.CFGK_CAMERA_FPS) == 10
              and doc.get(proto.CFGK_CAMERA_WIDTH) == 1280
              and doc.get(proto.CFGK_CAMERA_QUALITY) == 90,
              "%r" % (doc,))

        # A3 GET 应答**不回显明文口令**，只给 hasPassword。
        check("A3 口令脱敏：GET 不含明文、hasPassword=true",
              doc.get(proto.CFGK_CAMERA_PASSWORD) == ""
              and doc.get("hasPassword") is True,
              "password=%r hasPassword=%r"
              % (doc.get(proto.CFGK_CAMERA_PASSWORD),
                 doc.get("hasPassword")))

        # A4 空口令 = 不改（GET 不回明文 ⇒ 客户端回填空是常态；把空当真值
        # 写下去会**一存就把口令抹了**）。
        business._cfg_set_camera_key(proto.CFGK_CAMERA_PASSWORD, "")
        check("A4 空口令提交 = 不改（口令仍在）",
              cfg.camera_password == "mk123456",
              "password=%r" % (cfg.camera_password,))

        # A5 未知键 fail-closed。
        try:
            business._cfg_set_camera_key("nope", 1)
            check("A5 未知键被拒", False, "未抛异常")
        except ValueError:
            check("A5 未知键被拒", True)

        # A6 码流档非法值 fail-closed（拼错的档位不能静默回落缺省）。
        try:
            business._cfg_set_camera_key(proto.CFGK_CAMERA_STREAM, "major")
            check("A6 未知码流档被拒", False, "未抛异常")
        except ValueError:
            check("A6 未知码流档被拒", True)

        # A7 IP 形状非法被拒（空串合法 = 未配）。
        try:
            business._cfg_set_camera_key(proto.CFGK_CAMERA_IP, "not a host!")
            check("A7 非法 IP 被拒", False, "未抛异常")
        except ValueError:
            check("A7 非法 IP 被拒", True)
        business._cfg_set_camera_key(proto.CFGK_CAMERA_IP, "cam.local")
        check("A7b 主机名合法（cam.local）", cfg.camera_ip == "cam.local")

        # A8 端口越界被拒（0 / 65536）。
        bad = []
        for bad_port in (0, 65536, -1):
            try:
                business._cfg_set_camera_key(proto.CFGK_CAMERA_PORT, bad_port)
                bad.append(bad_port)
            except ValueError:
                pass
        check("A8 端口越界（0/65536/-1）全被拒", not bad, "漏放=%r" % (bad,))

        # A9 帧率/帧宽/质量越界被拒。
        bad = []
        for key, val in ((proto.CFGK_CAMERA_FPS, 0),
                         (proto.CFGK_CAMERA_FPS, 31),
                         (proto.CFGK_CAMERA_WIDTH, 127),
                         (proto.CFGK_CAMERA_WIDTH, 4097),
                         (proto.CFGK_CAMERA_QUALITY, 0),
                         (proto.CFGK_CAMERA_QUALITY, 101)):
            try:
                business._cfg_set_camera_key(key, val)
                bad.append((key, val))
            except ValueError:
                pass
        check("A9 帧率/帧宽/质量越界全被拒", not bad, "漏放=%r" % (bad,))

        # A10 落盘往返（camera 节落 cfg 文件，重新 load 后还在）。
        business._cfg_set_camera_key(proto.CFGK_CAMERA_FPS, 15)
        cfg.save()
        cfg2 = cfgmod.Config()
        cfg2.resolve_paths(basedir)
        cfg2.config_path = cfg.config_path
        cfg2.users_path = cfg.users_path
        cfg2.load()
        check("A10 落盘往返（fps 15 / ip / 口令都在）",
              cfg2.camera_fps == 15 and cfg2.camera_ip == "cam.local"
              and cfg2.camera_password == "mk123456",
              "fps=%r ip=%r pass=%r"
              % (cfg2.camera_fps, cfg2.camera_ip, cfg2.camera_password))
    finally:
        import shutil
        shutil.rmtree(basedir, ignore_errors=True)


# ============================================================================
#                        B 分片协议
# ============================================================================

def test_camera_fragments():
    """B1~B6：片头形状 / 片长上限 / 首末标记 / 拼回逐字节一致 / 单帧边界。"""
    section("B 分片协议（16B 片头 + 12KB 片长）")
    basedir = tempfile.mkdtemp(prefix="hsf_camera_frag_")
    try:
        business, _cfg = _rig(basedir)

        frame = camera_mod.CameraFrame(_fake_jpeg(), 704, 704, 1, 7)
        chunks = business._camera_fragments(frame)

        # B1 分片数 = ceil(len / CAM_CHUNK_MAX)。
        want = (len(frame.data) + proto.CAM_CHUNK_MAX - 1) // proto.CAM_CHUNK_MAX
        check("B1 分片数 = ceil(len/12288)", len(chunks) == want,
              "chunks=%d want=%d len=%d" % (len(chunks), want, len(frame.data)))

        # B2 片头 16 字节、七字段、小端（解出来与源帧对得上）。
        ok = True
        detail = ""
        for i, ch in enumerate(chunks):
            if len(ch) < proto.CAM_FRAME_HDR_LEN:
                ok, detail = False, "第 %d 片短于片头" % i
                break
            (magic, fid, seq, total, w, h,
             flags) = struct.unpack("<HIHHHHH",
                                    ch[:proto.CAM_FRAME_HDR_LEN])
            if (magic != proto.CAM_FRAME_MAGIC or fid != 7 or seq != i
                    or total != len(chunks) or w != 704 or h != 704):
                ok, detail = False, ("第 %d 片头异常 magic=%#x fid=%d seq=%d "
                                    "total=%d %dx%d"
                                    % (i, magic, fid, seq, total, w, h))
                break
        check("B2 片头 16B 小端七字段逐片正确", ok, detail)

        # B3 首片带 FIRST、末片带 LAST，中间片不带。
        def _flags(ch):
            return struct.unpack("<HIHHHHH", ch[:16])[6]
        check("B3 首片 FIRST / 末片 LAST / 中间片零标记",
              (_flags(chunks[0]) & proto.CAM_FRAME_FLAG_FIRST)
              and (_flags(chunks[-1]) & proto.CAM_FRAME_FLAG_LAST)
              and all(_flags(c) == 0 for c in chunks[1:-1]),
              "flags=%r" % ([_flags(c) for c in chunks],))

        # B4 单片总长严格小于 XLink 上限（超限 ⇒ 客户端 decode_header 返回
        # None ⇒ **整会话断链**，不是丢一帧）。
        longest = max(len(c) for c in chunks)
        check("B4 单片总长 < XLINK_MAX_PAYLOAD(16KB)",
              longest < XLINK_MAX_PAYLOAD,
              "max=%d（片头16+片数据%d）" % (longest, longest - 16))

        # B5 拼回与原始字节逐字节一致。
        joined = b"".join(c[proto.CAM_FRAME_HDR_LEN:] for c in chunks)
        check("B5 分片拼回 = 原始 JPEG（逐字节）",
              joined == frame.data, "len %d vs %d" % (len(joined),
                                                      len(frame.data)))

        # B6 边界：正好一片 / 一片多一字节。
        exact = camera_mod.CameraFrame(
            b"\xff\xd8" + b"\x00" * (proto.CAM_CHUNK_MAX - 4) + b"\xff\xd9",
            64, 64, 2, 8)
        one = business._camera_fragments(exact)
        over = camera_mod.CameraFrame(exact.data + b"\x00", 64, 64, 3, 9)
        two = business._camera_fragments(over)
        check("B6 边界：整 12288B=1 片、多 1 字节=2 片",
              len(one) == 1 and len(two) == 2,
              "one=%d two=%d" % (len(one), len(two)))

        # B7 空帧 ⇒ 无分片（不给客户端发空片头）。
        check("B7 空帧不产生分片",
              business._camera_fragments(
                  camera_mod.CameraFrame(b"", 0, 0, 0, 10)) == [])
    finally:
        import shutil
        shutil.rmtree(basedir, ignore_errors=True)


# ============================================================================
#                      C 订阅门控（不看不推）
# ============================================================================

def test_camera_gating():
    """C1~C5：无订阅不推 / 有订阅推 / 同帧不重推 / 无人看时不消费帧号。"""
    section("C 订阅门控（只在有人打开相机页时推）")
    basedir = tempfile.mkdtemp(prefix="hsf_camera_gate_")
    try:
        business, _cfg = _rig(basedir)
        frame = camera_mod.CameraFrame(_fake_jpeg(), 704, 704, 1000, 42)
        business.camera = _FakeCollector(frame)
        server = _FakeServer(subscribers=0)
        business.server = server

        loop = asyncio.new_event_loop()
        try:
            # C1 无订阅者 ⇒ 一帧都不推（连切片都不做）。
            loop.run_until_complete(business._publish_camera_frame())
            check("C1 无订阅者 ⇒ 不发任何片",
                  server.camera_chunks() == [],
                  "chunks=%d" % len(server.camera_chunks()))

            # C2 有订阅者 ⇒ 推（分片数 = ceil(len/12288)）。
            server.subscribers = 1
            loop.run_until_complete(business._publish_camera_frame())
            want = (len(frame.data) + proto.CAM_CHUNK_MAX - 1) \
                // proto.CAM_CHUNK_MAX
            check("C2 有订阅者 ⇒ 推满一帧（%d 片）" % want,
                  len(server.camera_chunks()) == want,
                  "chunks=%d" % len(server.camera_chunks()))

            # C3 同一帧不重复推（每拍 tick 都调 ⇒ 不去重会 10fps 刷同一张）。
            before = len(server.camera_chunks())
            loop.run_until_complete(business._publish_camera_frame())
            check("C3 同一帧不重复推",
                  len(server.camera_chunks()) == before,
                  "before=%d after=%d" % (before,
                                          len(server.camera_chunks())))

            # C4 新帧号 ⇒ 再推一帧。
            business.camera = _FakeCollector(
                camera_mod.CameraFrame(frame.data, 704, 704, 2000, 43))
            loop.run_until_complete(business._publish_camera_frame())
            check("C4 新帧号 ⇒ 再推一帧",
                  len(server.camera_chunks()) == before * 2,
                  "chunks=%d" % len(server.camera_chunks()))

            # C5 没人看时不更新「已推序号」⇒ 有人一打开就**立刻**拿到最新帧
            # （不能因为"这帧推过"让新订阅者等下一帧）。
            business._camera_published_seq = -1
            server.subscribers = 0
            business.camera = _FakeCollector(
                camera_mod.CameraFrame(frame.data, 704, 704, 3000, 44))
            loop.run_until_complete(business._publish_camera_frame())
            seq_after_silent = business._camera_published_seq
            server.subscribers = 1
            loop.run_until_complete(business._publish_camera_frame())
            check("C5 无人看时不消费帧号（有人一看立刻补最新帧）",
                  seq_after_silent == -1
                  and len(server.camera_chunks()) > 0,
                  "seq_after_silent=%r chunks=%d"
                  % (seq_after_silent, len(server.camera_chunks())))

            # C6 采集器为空（未启用/未连上）⇒ 不崩不推。
            business.camera = None
            n = len(server.camera_chunks())
            loop.run_until_complete(business._publish_camera_frame())
            check("C6 无采集器 ⇒ 静默（不推不崩）",
                  len(server.camera_chunks()) == n)
        finally:
            loop.close()
    finally:
        import shutil
        shutil.rmtree(basedir, ignore_errors=True)


# ============================================================================
#                   D 采集器纯函数（JPEG 解析 / 切帧 / 命令行）
# ============================================================================

def test_camera_pure_functions():
    """D1~D7：jpeg_size / split_jpeg_stream / _build_cmd（质量与后端）。"""
    section("D 采集器纯函数")
    img = _fake_jpeg(width=640, height=360, payload=1024)

    # D1 宽高解析（SOF0）。
    check("D1 jpeg_size 解析 SOF0 宽高 = 640×360",
          camera_mod.jpeg_size(img) == (640, 360),
          "%r" % (camera_mod.jpeg_size(img),))

    # D2 非 JPEG 数据 ⇒ (0,0) 而不是崩溃。
    check("D2 垃圾数据 ⇒ (0,0)",
          camera_mod.jpeg_size(b"\x00" * 64) == (0, 0))

    # D3 流切帧：两张首尾相接的 JPEG。
    stream = img + _fake_jpeg(width=320, height=320, payload=512)
    parts = camera_mod.split_jpeg_stream(stream)
    check("D3 连续 JPEG 流切成 2 张（SOI/EOI）",
          len(parts) == 2 and parts[0] == img
          and parts[1][:2] == b"\xff\xd8",
          "parts=%d" % len(parts))

    # D4 半截流（只有 SOI 没 EOI）⇒ 切不出（不吐残帧）。
    check("D4 半截流不吐残帧",
          camera_mod.split_jpeg_stream(img[:-2]) == [])

    # D5 命令行含质量与尺寸（**软件 jpegenc** 后端 = 缺省口径）。
    settings = {"enabled": True, "ip": "1.2.3.4", "port": 554,
                "username": "u", "password": "p", "stream": "minor",
                "fps": 10, "width": 704, "quality": 80, "timeoutMs": 5000}
    saved = os.environ.pop("HSF_CAMERA_BACKEND", None)
    try:
        os.environ["HSF_CAMERA_BACKEND"] = "gst"
        col = camera_mod.CameraCollector(settings, log=lambda m: None)
        cmd = " ".join(col._build_cmd())
        # [管线定稿 2026-11-20 板端实测] 走过两条弯路：
        # ① 旧序 `videoconvert ! videorate ! videoscale`：CSC 对解码器
        #    buffer 上的**相机全帧率**做 ⇒ 295% 单核、6 秒只出 4 帧。
        # ② 全硬件 `mppvideodec ! mppjpegenc`：CPU 6% 但**色度错位、画面
        #    偏紫、q-factor 失效**（画质不可用）。
        # 定稿 = **抽帧前置 + 软转 I420 + 软编**。
        check("D5 gst 后端：硬解 → 抽帧 → 软件 CSC → 软编 JPEG（10fps）",
              "mppvideodec" in cmd and "videorate" in cmd
              and "videoconvert" in cmd and "jpegenc" in cmd
              and "quality=80" in cmd and "framerate=10/1" in cmd,
              cmd[:220])
        # D5b [硬约束] 抽帧必须在 CSC **之前**（CSC 成本 ∝ 它处理过的
        # 帧率；顺序一反就从 47% 回到 295%）。
        check("D5b 抽帧在 CSC 之前（顺序错了 CPU 变 10 倍）",
              cmd.index("videorate") < cmd.index("videoconvert"),
              cmd[:220])
        # D5c [硬约束] **不用 mppjpegenc**：本板固件的 mppjpegenc 在 NV12
        # 输入下色度平面解释错误（偏紫/棕、q-factor 几乎无效），零拷贝/
        # DMABuf/强制输入格式/bt601 全部试尽无解。
        check("D5c 不用 mppjpegenc（硬件 JPEG 色度错位、画质不可用）",
              "mppjpegenc" not in cmd, cmd[:220])

        # D6 ffmpeg 兜底：-q:v 与 quality **方向相反**（1~31 越小越好）
        # ⇒ 按 31 - quality*30//100 折算（80 ⇒ 7），别把 80 直接塞进去。
        os.environ["HSF_CAMERA_BACKEND"] = "ffmpeg"
        col3 = camera_mod.CameraCollector(settings, log=lambda m: None)
        cmd3 = col3._build_cmd()
        cmd3s = " ".join(cmd3)
        try:
            qv = int(cmd3[cmd3.index("-q:v") + 1])
        except (ValueError, IndexError):
            qv = -1
        check("D6 ffmpeg 后端 -q:v 反向折算（quality 80 ⇒ 7）",
              "ffmpeg" in cmd3s and qv == 7, "q:v=%r" % (qv,))

        # D7 未设环境变量时按 PATH 自动挑（两种都不是 ⇒ 回落 ffmpeg 名字，
        # 由运行时失败重连兜住，不在 _build_cmd 抛异常）。
        os.environ.pop("HSF_CAMERA_BACKEND", None)
        col4 = camera_mod.CameraCollector(settings, log=lambda m: None)
        auto_cmd = col4._build_cmd()
        check("D7 无环境变量也能构造命令（自动挑后端，不抛异常）",
              isinstance(auto_cmd, list) and len(auto_cmd) > 4,
              " ".join(auto_cmd)[:100])
    finally:
        os.environ.pop("HSF_CAMERA_BACKEND", None)
        if saved is not None:
            os.environ["HSF_CAMERA_BACKEND"] = saved


# ============================================================================
#                  E 三端组帧器一致（client_py / webui_py）
# ============================================================================

def _load_submodule(pkg_parent, pkg_name, sub, alias_suffix=""):
    """按**包**导入某一端的某个子模块（三端各自一份，不许互相 import）。

    [为什么按包导入] 两端都是真包（proto.py 里 `from . import ports`）⇒
    按文件加载会 `ImportError: attempted relative import with no known
    parent package`。⇒ 把该端的**父目录**临时放进 sys.path，用唯一别名
    （pkg_name + "_x"）导入，避免两端的 `proto`/`ports` 互相覆盖；
    导入完把别名下的模块全清掉（下一端重新导入自己那份）。
    """
    import importlib.util

    alias = pkg_name + "_x" + alias_suffix
    parent = os.path.abspath(pkg_parent)
    if not os.path.isdir(os.path.join(parent, pkg_name)):
        return None
    inserted = parent not in sys.path
    if inserted:
        sys.path.insert(0, parent)
    try:
        spec = importlib.util.spec_from_file_location(
            alias, os.path.join(parent, pkg_name, "__init__.py"),
            submodule_search_locations=[os.path.join(parent, pkg_name)])
        if spec is None or spec.loader is None:
            return None
        pkg = importlib.util.module_from_spec(spec)
        sys.modules[alias] = pkg
        spec.loader.exec_module(pkg)
        sub_spec = importlib.util.spec_from_file_location(
            alias + "." + sub,
            os.path.join(parent, pkg_name, sub + ".py"))
        if sub_spec is None or sub_spec.loader is None:
            return None
        mod = importlib.util.module_from_spec(sub_spec)
        mod.__package__ = alias
        sys.modules[alias + "." + sub] = mod
        sub_spec.loader.exec_module(mod)
        return mod
    finally:
        if inserted and parent in sys.path:
            sys.path.remove(parent)
        for key in [k for k in list(sys.modules) if k == alias
                    or k.startswith(alias + ".")]:
            sys.modules.pop(key, None)


def _load_assembler(pkg_parent, pkg_name):
    """导入某一端的 camera_frames（内部依赖同包的 proto）。"""
    return _load_submodule(pkg_parent, pkg_name, "camera_frames")


def test_assemblers_agree():
    """E1~E6：两端组帧器对同一串分片给出同样结果（规则必须三端同款）。"""
    section("E 三端组帧器一致（client_py / webui_py）")
    HSF = os.path.dirname(ROOT)
    ends = [
        ("客户端", os.path.join(HSF, "client_py"), "hsfsrv_client"),
        ("网页端", os.path.join(HSF, "webui_py"), "hsfweb"),
    ]
    # 分片器用服务端那份（唯一事实源）。
    basedir = tempfile.mkdtemp(prefix="hsf_camera_asm_")
    try:
        business, _cfg = _rig(basedir)
        frame = camera_mod.CameraFrame(_fake_jpeg(), 704, 704, 1000, 1)
        chunks = business._camera_fragments(frame)

        loaded = []
        for name, pkg_parent, pkg_name in ends:
            mod = _load_assembler(pkg_parent, pkg_name)
            check("%s 组帧器可加载（%s）" % (name, pkg_name), mod is not None,
                  pkg_parent)
            if mod is not None:
                loaded.append((name, mod))

        for name, mod in loaded:
            asm = mod.CameraAssembler()
            # E1 完整一帧 ⇒ 拼回并与原图逐字节一致。
            out = None
            for ch in chunks:
                got = asm.feed(ch)
                if got is not None:
                    out = got
            check("%s：完整分片 ⇒ 组出整帧且逐字节一致" % name,
                  out == frame.data,
                  "len=%r vs %r" % (None if out is None else len(out),
                                    len(frame.data)))
            check("%s：frames=1 / dropped=0 / 宽高 704×704" % name,
                  asm.frames == 1 and asm.dropped == 0
                  and asm.width == 704 and asm.height == 704,
                  "frames=%d dropped=%d %dx%d"
                  % (asm.frames, asm.dropped, asm.width, asm.height))

            # E2 丢片（抽掉中间一片）⇒ 整帧作废（不吐半帧）。
            asm2 = mod.CameraAssembler()
            outs = []
            for i, ch in enumerate(chunks):
                if i == 1:
                    continue
                got = asm2.feed(ch)
                if got is not None:
                    outs.append(got)
            check("%s：丢片 ⇒ 整帧作废（dropped>0 且不出图）" % name,
                  not outs and asm2.dropped > 0,
                  "outs=%d dropped=%d" % (len(outs), asm2.dropped))

            # E3 覆盖式：第二帧的 seq==0 进来时，上一帧未收齐的缓冲作废。
            asm3 = mod.CameraAssembler()
            asm3.feed(chunks[0])                       # 只喂首片
            frame2 = camera_mod.CameraFrame(frame.data, 704, 704, 2000, 2)
            chunks2 = business._camera_fragments(frame2)
            out2 = None
            for ch in chunks2:
                got = asm3.feed(ch)
                if got is not None:
                    out2 = got
            check("%s：新帧覆盖未收齐的旧帧 ⇒ 新帧完整出图" % name,
                  out2 == frame.data and asm3.dropped >= 1,
                  "out=%r dropped=%d"
                  % (out2 is not None, asm3.dropped))

            # E4 垃圾/短包不崩。
            asm4 = mod.CameraAssembler()
            bad = (asm4.feed(b"") or asm4.feed(b"\x00" * 4)
                   or asm4.feed(b"\x00" * 64))
            check("%s：空/短/错 magic 包被忽略（不崩）" % name, bad is None)

            # E5 头尾不是 JPEG（缺片拼错）⇒ 作废。
            asm5 = mod.CameraAssembler()
            junk = camera_mod.CameraFrame(b"\x00" * 4096, 64, 64, 3, 3)
            out5 = None
            for ch in business._camera_fragments(junk):
                got = asm5.feed(ch)
                if got is not None:
                    out5 = got
            check("%s：非 JPEG 整帧被头尾校验拦下" % name,
                  out5 is None and asm5.dropped >= 1,
                  "out=%r dropped=%d"
                  % (out5 is not None, asm5.dropped))

        # E6 两端的常量与服务端逐值一致（协议单一事实源）。
        names = ("CAM_FRAME_MAGIC", "CAM_FRAME_HDR_LEN", "CAM_CHUNK_MAX",
                 "CAM_FRAME_FLAG_FIRST", "CAM_FRAME_FLAG_LAST",
                 "TOPIC_CAMERA_FRAME", "CMD_CAMERA_FRAME")
        diffs = []
        for name, pkg_parent, pkg_name in ends:
            # proto 也按包导入（它 `from . import ports`）⇒ 用与组帧器相同的
            # 别名机制取到该端自己的那一份。
            proto_path = os.path.join(pkg_parent, pkg_name, "proto.py")
            pmod = _load_submodule(pkg_parent, pkg_name, "proto")
            if pmod is None:
                diffs.append((name, "<proto 加载失败>", None, proto_path))
                continue
            for n in names:
                if getattr(pmod, n, None) != getattr(proto, n, None):
                    diffs.append((name, n, getattr(pmod, n, None),
                                  getattr(proto, n, None)))
        check("E6 两端分片常量与服务端逐值一致", not diffs, repr(diffs))
    finally:
        import shutil
        shutil.rmtree(basedir, ignore_errors=True)


# ============================================================================
#                    F ES 直传（2026-11-19 架构转向）
# ============================================================================

def _mk_nal(t):
    """造一条 H.264 NAL（Annex-B 4 字节起始码 + header 指定类型）。"""
    import struct as _s
    return (b"\x00\x00\x00\x01" + bytes([(0x60 | (t & 0x1F)) if t >= 6
                                         else (0x40 | t)]) + b"\xde\xad" * 3)


def _es_rig(basedir, transport="stream", codec="h264"):
    """ES 直传装具：App + 假 ES 采集器（合成 NAL 序列）。"""
    business, cfg = _rig(basedir)

    class _FakeEs(object):
        """只实现 `_publish_camera_es`/`publish_topic_now` 依赖的接口。"""

        def __init__(self):
            self.settings = {"transport": transport}
            self._gop = []
            self._pending = []
            self._codec = codec

        def push(self, nal):
            from hsfsrv import camera as cam
            t = cam._nal_type(nal, False)
            if cam._nal_is_param_set(t, False):
                has_vcl = any(cam._nal_is_vcl(cam._nal_type(n, False), False)
                              for n in self._gop)
                if has_vcl or not self._gop:
                    self._gop = []
                self._gop.append(nal)
            elif self._gop:
                self._gop.append(nal)
            self._pending.append(nal)

        def drain_nals(self):
            out, self._pending = self._pending, []
            return out

        def gop_nals(self):
            return list(self._gop)

        def detected_codec(self):
            return self._codec

        def latest_frame(self):
            return None

    biz = business
    biz.camera = _FakeEs()
    biz._camera_subs_last = 0
    biz._camera_published_seq = -1
    return biz


class _EsServer(_FakeServer):
    """计数发布帧的假服务端（复用 _FakeServer 的订阅门控）。"""

    pass


def test_camera_es_passthrough():
    """F1~F8：直传发布 / GOP 自举 / 订阅门控 / 分片带 ES·H265 位。"""
    section("F ES 直传（transport=stream）")
    basedir = tempfile.mkdtemp(prefix="hsf_camera_es_")
    try:
        business = _es_rig(basedir)

        # 喂一个完整 GOP：SPS PPS IDR slice
        sps, pps = _mk_nal(7), _mk_nal(8)
        idr, sl = _mk_nal(5), _mk_nal(1)
        business.camera.push(sps)
        business.camera.push(pps)
        business.camera.push(idr)
        business.camera.push(sl)
        check("F1 GOP 缓冲 = 参数集+IDR+slice（4 条）",
              len(business.camera.gop_nals()) == 4,
              "%d" % len(business.camera.gop_nals()))

        server = _FakeServer(subscribers=0)
        business.server = server
        loop = asyncio.new_event_loop()
        try:
            # F2 无人订阅 ⇒ 不发布且 **pending 丢弃**（2026-11-19 审查
            # 修订：攒着会在订阅沿把过期 NAL 一股脑发给新订阅者——白耗
            # 0.5MB 带宽；自举靠 GOP 缓冲就够了）。
            loop.run_until_complete(business._publish_camera_frame())
            _pending = len(business.camera.drain_nals())
            check("F2 无订阅 ⇒ 不发且 pending 丢弃（不留过期积压）",
                  server.camera_chunks() == [] and _pending == 0,
                  "chunks=%d pending=%d"
                  % (len(server.camera_chunks()), _pending))

            # F3 订阅沿（0→正）⇒ 先 GOP 后增量，分片带 ES 位
            server.subscribers = 1
            loop.run_until_complete(business._publish_camera_frame())
            chunks = server.camera_chunks()
            check("F3 订阅沿 ⇒ 发布 GOP+增量（非零）",
                  len(chunks) > 0,
                  "chunks=%d" % len(chunks))
            flag_bits = {struct.unpack("<HIHHHHH", c[:16])[6]
                         for c in chunks}
            check("F4 分片 flags 全带 ES 位",
                  all(f & proto.CAM_FRAME_FLAG_ES for f in flag_bits),
                  "%r" % ([hex(f) for f in flag_bits],))
            fid_set = {struct.unpack("<HIHHHHH", c[:16])[1]
                       for c in chunks}
            check("F5 每条 NAL 独立 frameId（4 NAL ⇒ ≥4 fid）",
                  len(fid_set) >= 4, "%d" % len(fid_set))

            # F6 持续订阅 ⇒ 只发增量（无新 NAL ⇒ 不发）
            before = len(server.camera_chunks())
            loop.run_until_complete(business._publish_camera_frame())
            check("F6 无新 NAL ⇒ 不重复发", len(server.camera_chunks()) == before)

            # F7 新 NAL ⇒ 只发增量（不含 GOP）
            business.camera.push(_mk_nal(1))
            loop.run_until_complete(business._publish_camera_frame())
            new_fids = {struct.unpack("<HIHHHHH", c[:16])[1]
                        for c in server.camera_chunks()[before:]}
            check("F7 增量拍只发新 NAL（1 条 ⇒ 1 个 fid）",
                  len(new_fids) == 1, "%r" % (new_fids,))

            # F8 H.265：分片带 H265 位
            business.camera._codec = "h265"
            business.camera.push(_mk_nal(1))
            server.frames.clear()   # 清内部记录（camera_chunks 返回的是副本）
            loop.run_until_complete(business._publish_camera_frame())
            flag_bits = {struct.unpack("<HIHHHHH", c[:16])[6]
                         for c in server.camera_chunks()}
            check("F8 h265 ⇒ 分片带 ES|H265 位",
                  flag_bits and all(f & proto.CAM_FRAME_FLAG_ES
                                    and f & proto.CAM_FRAME_FLAG_H265
                                    for f in flag_bits),
                  "%r" % ([hex(f) for f in flag_bits],))
        finally:
            loop.close()

        # F9 订阅补发（publish_topic_now 的 ES 分支）返回 GOP 列表（协程）
        biz2 = _es_rig(basedir)
        biz2.camera.push(sps)
        biz2.camera.push(pps)
        biz2.camera.push(idr)
        loop2 = asyncio.new_event_loop()
        try:
            result = loop2.run_until_complete(
                business.publish_topic_now(None, proto.TOPIC_CAMERA_FRAME))
            check("F9 订阅补发返回 GOP 分片列表（含 ES 位）",
                  isinstance(result, list) and len(result) >= 3
                  and all(dt == proto.DATATYPE_RAW for _p, dt in result),
                  "%d 条" % (len(result) if isinstance(result, list) else 0))
        finally:
            loop2.close()
    finally:
        import shutil
        shutil.rmtree(basedir, ignore_errors=True)


# ============================================================================

def main():
    test_camera_config()
    test_camera_fragments()
    test_camera_gating()
    test_camera_pure_functions()
    test_assemblers_agree()
    test_camera_es_passthrough()
    print("\n" + "=" * 62)
    print("  汇总：PASS %d / FAIL %d" % (PASS, FAIL))
    print("=" * 62)
    return 0 if FAIL == 0 else 1


if __name__ == "__main__":
    sys.exit(main())
