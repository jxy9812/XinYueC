# -*- coding: utf-8 -*-
"""相机画面分片组帧（桌面客户端）。

与服务端/网页端**同格式**（proto.CAM_FRAME_*）：
    片头（小端 16B）：magic(2) frameId(4) seq(2) total(2)
                      width(2) height(2) flags(2)

组帧规则（三端一致，**别单端改**）：
    ① seq==0 ⇒ 丢弃同 frameId 的旧缓冲（**覆盖式**：慢客户端只保最新帧）；
    ② 收齐 total 片且带末片标记 ⇒ 校验 JPEG 头尾(FFD8…FFD9) ⇒ 交给显示层；
    ③ **丢片即丢整帧**（下一帧 seq==0 到来时未收齐的旧缓冲直接丢，不等超时）
       —— 服务端只发最新帧的分片，慢客户端丢片是常态，不是异常。

[为什么客户端也要组帧] 画面**必须经服务端**：相机在内网、客户端可能在公网，
且服务端是唯一同时够得到相机与客户端的地方（用户 2026-11-19 口径）。
服务端把一帧 JPEG 切成 <=12KB 的片（XLink 单帧上限 16KB）⇒ 客户端组帧。

[为什么是本包一份而不是共用网页端那份] 三端是**独立可分发**的包
（网页端/桌面端/C 端互不依赖），组帧器也就各带一份（同一份协议、同一条
规则，改一处要同步另一处）。
"""

import struct

from . import proto

_HDR_LEN = proto.CAM_FRAME_HDR_LEN
#: "<HIHHHHH" = magic(2) frameId(4) seq(2) total(2) width(2) height(2) flags(2)
_HDR_FMT = "<HIHHHHH"


class CameraAssembler(object):
    """把相机分片组回整帧。"""

    def __init__(self):
        self._buf = None          # bytearray：当前帧已收到的片
        self._frame_id = None
        self._total = 0
        self._next_seq = 0
        self.width = 0
        self.height = 0
        self.frames = 0           # 组好的帧数
        self.dropped = 0          # 丢掉的帧数（缺片/校验不过）
        #: 最近一次组出帧的片头 flags（ES/H265 位在这里——调用方据此判断
        #: 组出的是 JPEG 整帧还是编码流 NAL；2026-11-19 直传）。
        self.last_flags = 0

    def feed(self, body):
        """喂一片（**不含** topicId 前缀 —— bridge 已剥）。

        :return: 组好的整帧 bytes；没组好返回 None。
        """
        if body is None or len(body) < _HDR_LEN:
            return None
        try:
            (magic, frame_id, seq, total, width, height,
             flags) = struct.unpack(_HDR_FMT, body[:_HDR_LEN])
        except struct.error:
            return None
        if magic != proto.CAM_FRAME_MAGIC:
            return None
        if total <= 0 or seq >= total:
            return None
        self.last_flags = flags
        chunk = body[_HDR_LEN:]
        if seq == 0:
            # [覆盖式] 新的一帧开始 ⇒ 旧的未收齐缓冲直接作废
            if self._buf is not None and self._frame_id != frame_id:
                self.dropped += 1
            self._buf = bytearray()
            self._frame_id = frame_id
            self._total = total
            self._next_seq = 0
            self.width = width
            self.height = height
        elif frame_id != self._frame_id:
            # 别的帧的续片（前面丢了）⇒ 整帧作废，等下一帧的 seq==0
            self._buf = None
            self.dropped += 1
            return None
        if seq != self._next_seq:
            # 缺片 ⇒ 整帧作废（不等超时）
            self._buf = None
            self.dropped += 1
            return None
        self._buf += chunk
        self._next_seq += 1
        if not (flags & proto.CAM_FRAME_FLAG_LAST):
            return None
        if self._next_seq < self._total:
            # 末片标记与 total 不符 ⇒ 作废
            self._buf = None
            self.dropped += 1
            return None
        data = bytes(self._buf)
        self._buf = None
        # [JPEG 头尾校验] 缺片/错位时字节流不是合法 JPEG ⇒ 丢弃。
        # [ES 直传 2026-11-19] 载荷是编码流 NAL（Annex-B，00000001 起），
        # **不是 JPEG** ⇒ 按 ES 位跳过 JPEG 头尾校验。
        if flags & proto.CAM_FRAME_FLAG_ES:
            self.frames += 1
            return data
        if len(data) < 4 or data[:2] != b"\xff\xd8" or data[-2:] != b"\xff\xd9":
            self.dropped += 1
            return None
        self.frames += 1
        return data

    def stats(self):
        return {"frames": self.frames, "dropped": self.dropped,
                "width": self.width, "height": self.height}
