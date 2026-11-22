# -*- coding: utf-8 -*-
"""相机 ES 解码线程（H.264/H.265 NAL → RGB 帧；桌面客户端）。

[架构位置] 2026-11-19 画面直传后，服务端**不解码**：XLink 推来的分片在
bridge 里组回 **NAL**（Annex-B），本模块把它解成 RGB 像素交给 UI。旧
JPEG 路（服务端编好 JPEG）不经过这里（`mode="jpeg"` 时 model 里直接是
JPEG 字节，QImage.fromData 即可）。

[为什么是独立线程] 解码 704²@20fps 软解约占一核 15~30%（x86 更低）——
绝不能占 UI 线程。bridge 的网络线程只做 `feed(nal)`（入队，非阻塞），
解码在本线程跑，解出的帧经回调交 bridge emit 到 UI。

[硬解（GPU/VPU）口径 —— 用户要求"桌面端可以用GPU硬解"＋远端设置可选]
PyAV(FFmpeg) 的硬解后端是**逐家 GPU 的专用解码器**：
    Intel 核显/Arc  → h264_qsv / hevc_qsv
    NVIDIA          → h264_cuvid / hevc_cuvid
    AMD             → h264_amf / hevc_amf
本机（Intel UHD 770）实测 h264_qsv 出帧且 `to_ndarray` 可取像素（hw 帧
自动落到可读格式）；没有对应 GPU 的机器 create 成功但 open/首帧失败
（本机 cuvid=PermissionError、amf=UnknownError，2026-11-19 实测）。
⇒ `hwDecode="auto"`：按 qsv → cuvid → amf 试，**真解出首帧才算数**，
   否则回落软解 h264/hevc；`"on"` 强制硬解（不可用则在状态里报原因）；
   `"off"` 直接软解。
[704² 软解根本不贵] 本机实测 0.47ms/帧（x86）；硬解的真实价值在以后
  切主码流（2528² H.265）时才体现——接口现在就留好。

[参数集/IDR 自举] 解码器只在见到参数集（SPS/PPS/VPS）后的首个 IDR 起
送数据（`_need_key`）——中途加入不喂，等下一 GOP。硬解试挂/模式切换
重建解码器后同样等下一 GOP（一次性 ≤1 个 GOP 的黑屏，换确定性）。
"""

import os
import queue
import sys
import threading

try:
    import av
    import av.error as _av_error
    #: [PyAV 19] 异常基类是 `av.error.FFmpegError`（旧别名 av.AVError 已删）
    _FFError = _av_error.FFmpegError
    _EAGAIN = getattr(_av_error, "EAGAIN", None)
    _HAS_AV = True
except ImportError:                                   # pragma: no cover
    _HAS_AV = False

#: NAL 判定（与服务端 camera.py 同规则；客户端独立包自备一份）。
def _nal_type(nal, h265):
    body = nal
    while len(body) > 1 and body[0] == 0:
        body = body[1:]
    if len(body) < 2:
        return -1
    if h265:
        return (body[1] >> 1) & 0x3F
    return body[1] & 0x1F


def _is_param_set(t, h265):
    return (t in (7, 8)) if not h265 else (t in (32, 33, 34))


def _is_vcl(t, h265):
    """VCL（图像数据）：H.264 =1..5；H.265 =0..31。"""
    return (1 <= t <= 5) if not h265 else (0 <= t <= 31)


def _is_idr(t, h265):
    return (t == 5) if not h265 else (16 <= t <= 21)


#: auto 模式的硬解候选次序（前缀按 codec 拼：h264_qsv / hevc_qsv …）。
_HW_CANDIDATES = ("qsv", "cuvid", "amf")


#: 解码链调试开关（HSF_CAM_DEBUG=1 打印每个 AU 的长度/头字节与解码异常
#: 包详情；缺省关。诊断「InvalidData 循环」这类解码器侧问题时用）。
_CAM_DEBUG = os.environ.get("HSF_CAM_DEBUG") == "1"


class EsDecoder(object):
    """NAL → RGB 解码线程。bridge 网络线程 feed()，本线程解。"""

    #: 送解码器的 NAL 队列上限（条）。20fps×GOP2s≈40 条，8 倍余量。
    QUEUE_MAX = 300
    #: 硬解探活：喂了这么多条 NAL 仍无帧 ⇒ 判不可用（回落/报错）。
    HW_PROBE_NALS = 120

    def __init__(self, on_frame, on_note=None):
        """
        :param on_frame: 回调 `fn(rgb_bytes, width, height)`（解码线程直调）。
        :param on_note: 回调 `fn(text)`——解码方式切换/失败原因（状态行用）。
        """
        self._on_frame = on_frame
        self._on_note = on_note or (lambda text: None)
        self._q = queue.Queue(maxsize=self.QUEUE_MAX)
        self._stop = threading.Event()
        self._lock = threading.Lock()
        self._h265 = False
        self._hw_requested = "auto"     # 外部配置（auto / on / off）
        self._hw_mode = "auto"          # 内部**有效**解码方式（回落只改它）
        self._hw_failed = set()         # 本流内已证不可用的硬解后端
        self._rebuild = False           # 置位 ⇒ 解码线程重建解码器
        self._need_key = True
        self._dec = None
        self._dec_name = ""
        self._hw_active = False         # 当前是不是硬解
        self._frames = 0
        self._thread = None
        if _HAS_AV:
            self._thread = threading.Thread(target=self._run,
                                            name="camera-es-decoder")
            self._thread.daemon = True
            self._thread.start()
        else:
            self._on_note("未安装 PyAV（pip install av），画面无法解码")

    # ---- 网络线程侧 ----------------------------------------------------

    def feed(self, nal):
        """一个 Annex-B NAL 到达（非阻塞；满队 ⇒ 丢队等下一 GOP）。"""
        if self._thread is None:
            return
        try:
            self._q.put_nowait(nal)
        except queue.Full:
            dropped = 0
            while True:
                try:
                    self._q.get_nowait()
                    dropped += 1
                except queue.Empty:
                    break
            self._need_key = True
            self._on_note("解码积压丢队 %d 条，等下一关键帧" % dropped)
            try:
                self._q.put_nowait(nal)
            except queue.Full:
                pass

    def configure(self, h265=None, hw_mode=None):
        """编码/解码方式变化时调用（变化才重建；幂等；None=不动该项）。

        [比较基准是 `_hw_requested`] 内部回落（硬解失败改用软解）只改
        `_hw_mode`（有效值），**不改** `_hw_requested` ⇒ bridge 每片重发
        同一份配置不会触发无谓重建（实锅：回落把有效值改成 off 后，外部
        的 auto 每片都算"变化" ⇒ 每片 rebuild ⇒ 解码器刚建就被拆，永远
        黑屏，2026-11-19 diag5/diag8 抓到）。
        """
        with self._lock:
            changed = ((h265 is not None and h265 != self._h265)
                       or (hw_mode is not None
                           and hw_mode != self._hw_requested))
            if hw_mode is not None:
                self._hw_requested = hw_mode
                self._hw_mode = hw_mode
                self._hw_failed = set()       # 显式改配置 ⇒ 重试各后端
            if h265 is not None:
                self._h265 = h265
        if changed:
            self._rebuild = True

    def close(self):
        self._stop.set()
        thread = self._thread
        if thread and thread.is_alive():
            thread.join(timeout=3.0)

    @property
    def hw_active(self):
        return self._hw_active

    @property
    def frames(self):
        return self._frames

    # ---- 解码线程 ------------------------------------------------------

    def _note(self, text):
        try:
            self._on_note(text)
        except Exception:                             # noqa: BLE001
            pass

    def _run(self):
        while not self._stop.is_set():
            if self._rebuild:
                self._teardown()
                self._rebuild = False
                self._need_key = True
            try:
                nal = self._q.get(timeout=0.5)
            except queue.Empty:
                continue
            self._decode_one(nal)

    def _teardown(self):
        if self._dec is not None:
            try:
                self._dec.close()
            except Exception:                         # noqa: BLE001
                pass
            self._dec = None
            self._dec_name = ""
        # [_au 一并清空 2026-11-19 审查实修] 重建发生在 AU 中间时（解码
        # 出错、硬解探活回落），残留的半包会拼进新解码器的第一个包 ⇒
        # 坏包。清空等下一 GOP 重新自举（need_key 已由调用方置位）。
        self._au = bytearray()
        self._au_has_vcl = False

    def _open_decoder(self, hw_name=None):
        """按名字开一个解码器；失败返回 None（软解名恒在，不会失败）。"""
        codec = ("hevc" if self._h265 else "h264") \
            + ("" if hw_name is None else "_" + hw_name)
        try:
            dec = av.CodecContext.create(codec, "r")
            dec.open()
            self._dec = dec
            self._dec_name = codec
            return dec
        except Exception as exc:                      # noqa: BLE001
            if hw_name is not None:
                self._note("硬解 %s 不可用（%s）" % (codec, exc))
            return None

    def _decode_one(self, nal):
        """一个 NAL 到达：**AU 聚合**后再喂解码器。

        [为什么聚合] 硬解后端（qsv 实测）逐 NAL 喂不出帧——解码器要的
        是**完整访问单元**（参数集+IDR/slice 一包）。软解虽然逐 NAL 也能
        出帧（ffmpeg 内部有解析缓存），但聚合对两者都无害且更贴近码流
        本义 ⇒ 统一走聚合：参数集/SEI 先攒着，VCL 到来拼成整包 flush。
        """
        h265 = self._h265
        t = _nal_type(nal, h265)
        self._au = getattr(self, "_au", bytearray())
        self._au_has_vcl = getattr(self, "_au_has_vcl", False)
        if _is_param_set(t, h265):
            # [PPS 不能清掉 SPS] SPS/PPS 是两条独立参数集 NAL：只有 _au 里
            # **已有 VCL**（上一 GOP 在进行）才重开；否则直接追加——否则
            # PPS 会把 SPS 清掉，发出去的 AU 缺 SPS ⇒ ffmpeg 一直报
            # InvalidData（2026-11-19 diag10 抓到，与服务端 GOP 缓冲同款）。
            if self._au_has_vcl or not self._au:
                self._au = bytearray()
                self._au_has_vcl = False
            self._need_key = False
            if self._dec is None:
                self._pick_decoder()
        elif self._need_key and _is_idr(t, h265):
            return                      # 中途接入：等参数集
        if self._need_key or self._dec is None:
            return
        self._au += nal
        if _is_vcl(t, h265) or _is_idr(t, h265):
            self._au_has_vcl = True
        else:
            # [参数集/SEI 只攒着] 单独 flush 会被解码器当残包拒
            # （实测 SPS-only 28B 包直接 InvalidData ⇒ 永远出不了首帧），
            # 等 VCL 到来拼成整 AU 一次发。
            return
        packet = bytes(self._au)
        self._au = bytearray()
        if _CAM_DEBUG:
            print("[cam-dec] AU len=%d head=%s tail=%s"
                  % (len(packet), packet[:8].hex(), packet[-4:].hex()),
                  file=sys.stderr, flush=True)
        # [硬解探活] create 成功 ≠ 真能解（没有对应 GPU 的机器 open 也
        # 可能过了，首包才炸）⇒ 喂了 HW_PROBE_NALS 个 AU 仍 0 帧 ⇒ 判不可用。
        probing = getattr(self, "_probe_left", -1) > 0
        try:
            for frame in self._dec.decode(av.Packet(packet)):
                rgb = frame.to_ndarray(format="rgb24")
                self._frames += 1
                self._probe_left = -1
                try:
                    self._on_frame(rgb.tobytes(), frame.width, frame.height)
                except Exception as exc:              # noqa: BLE001
                    self._note("帧回调异常：%s: %s"
                               % (type(exc).__name__, exc))
        except _FFError as exc:
            # 解码器状态坏（缺参考帧等）⇒ 重建等下一 GOP；硬解频繁失败
            # ⇒ 记坏后端 + 有效值降级软解（auto 语义；`_hw_requested`
            # 不动 ⇒ 外部重发配置不触发无谓重建）。
            if _CAM_DEBUG:
                print("[cam-dec] decode 异常 %s: %s（AU len=%d）"
                      % (type(exc).__name__, exc, len(packet)),
                      file=sys.stderr, flush=True)
            self._note("解码出错（%s: %s）⇒ 重建等下一关键帧"
                       % (type(exc).__name__, exc))
            self._teardown()
            self._need_key = True
            if self._hw_active and self._hw_requested == "auto":
                self._hw_mode = "off"
                self._hw_failed.update(_HW_CANDIDATES)
                self._note("硬解不稳 ⇒ 自动改用软解")
            return
        if probing:
            self._probe_left -= 1
            if self._probe_left <= 0 and self._frames == 0:
                self._note("硬解 %s 喂了 %d 条 NAL 仍无帧 ⇒ 回落软解"
                           % (self._dec_name, self.HW_PROBE_NALS))
                # 记坏后端（本流内不再反复试）+ 有效值降级软解
                self._hw_failed.add(self._dec_name.split("_", 1)[-1])
                self._teardown()
                self._need_key = True
                self._hw_mode = "off"
                self._hw_active = False

    def _pick_decoder(self):
        """按 hw_mode 选解码器。成功标准 = 真解出首帧（试挂在首 GOP）。

        [传后缀不是全名] `_open_decoder(hw_name)` 自己拼 `<h264|hevc>_<hw>`，
        这里只传 `"qsv"` 等后缀——之前把 `h264_qsv` 整个传进去又拼了一层
        ⇒ `h264_h264_qsv`（不存在的解码器）⇒ 全部硬解静默失败
        （2026-11-19 diag8 抓到）。
        """
        mode = self._hw_requested
        tried_hw = False
        if mode in ("auto", "on"):
            for suffix in _HW_CANDIDATES:
                if suffix in self._hw_failed:
                    continue          # 本流内已证不可用，不再反复试
                tried_hw = True
                dec = self._open_decoder(suffix)
                if dec is None:
                    self._hw_failed.add(suffix)
                    continue
                self._hw_active = True
                self._probe_left = self.HW_PROBE_NALS
                self._note("解码方式：%s（%s 硬解）"
                           % (dec.name, "核显/独显（%s）" % suffix))
                return
            if mode == "on":
                self._note("本机没有可用的 GPU 硬解后端"
                           "（qsv/cuvid/amf 都不可用）")
        # 软解（auto 回落 / off / on 失败后的兜底——on 失败也先给画面）
        dec = self._open_decoder(None)
        if dec is not None:
            self._hw_active = False
            if tried_hw and mode == "auto":
                self._note("解码方式：%s（软解；本机无可用 GPU 硬解）"
                           % dec.name)
            else:
                self._note("解码方式：%s（软解）" % dec.name)
