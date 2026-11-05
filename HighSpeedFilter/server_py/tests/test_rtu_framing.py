"""RTU 帧解析测试（模拟从站，不依赖真机/串口）。

[为什么必须有它] 真机上 0x1000 的 9 个寄存器被解成
[4619, 47104, 21, 10496, ...]（正确应为 [3000, 0, 5417, ...]）。
根因是数据区切片写成 `frame[2:2+byte_count]`——**把 byte_count 自身
也当成了数据**，整体错位一字节。这个 bug 只有在"能真正读到一帧完整
应答"时才暴露，而之前所有测试都没碰串口，所以一路全绿到了真机。

本测试用内存假串口喂**手工构造的合法 RTU 帧**，并且用 chunk=1..N
覆盖"逐字节到达"的极端分片，专门锁死这类偏移/错位缺陷。

做法：起一个 asyncio 任务扮演从站，通过一对**内存管道**与 RtuMaster 交互？
不行——RtuMaster 直接用 pyserial。

故改用 monkeypatch：把 RtuMaster._serial 换成一个内存串口对象，
它按 Modbus RTU 语义应答。这样能精确复现「分次读导致半帧错位」的场景。
"""
import asyncio
import os
import struct
import sys

# [跨平台路径] 早先这里硬编码了 Windows 的 F:\... 绝对路径，板端（Linux）
# 一跑就 ModuleNotFoundError。改成按本文件位置反推包根。
_HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.dirname(_HERE))

from hsfsrv.modbus import RtuMaster, crc16, append_crc


class FakeSerial(object):
    """内存串口：收到请求就生成应答；可配置"每次最多吐几字节"模拟半帧。"""

    def __init__(self, responses, chunk_size=1):
        self.responses = list(responses)   # [(unit, fc, addr, cnt, payload), ...]
        self.chunk_size = chunk_size       # 每次 read 最多返回几个字节
        self.tx = bytearray()              # 待读字节
        self.is_open = True
        self.requests = []

    def reset_input_buffer(self):
        self.tx.clear()

    def write(self, data):
        self.requests.append(bytes(data))
        body = bytes(data[:-2])
        unit, fc = body[0], body[1]
        addr = (body[2] << 8) | body[3]
        cnt = (body[4] << 8) | body[5]
        for (r_unit, r_fc, r_addr, r_cnt, payload) in self.responses:
            if (r_unit, r_fc, r_addr, r_cnt) == (unit, fc, addr, cnt):
                frame = bytes([unit, fc, len(payload)]) + bytes(payload)
                self.tx += append_crc(frame)
                return
        self.tx += append_crc(bytes([unit, fc | 0x80, 0x02]))

    def flush(self):
        pass

    def read(self, n):
        if not self.tx:
            return b""
        take = min(n, self.chunk_size, len(self.tx))
        out = bytes(self.tx[:take])
        del self.tx[:take]
        return out

    def close(self):
        self.is_open = False


async def run_case(label, chunk_size):
    """0x1000 x9 应解出 [3000, 0, 5417, 0, 0, 0, 0, 0, 0]。"""
    expect = [3000, 0, 5417, 0, 0, 0, 0, 0, 0]
    payload = b"".join(struct.pack(">H", v) for v in expect)
    fake = FakeSerial([(2, 3, 0x1000, 9, payload)], chunk_size=chunk_size)
    m = RtuMaster("FAKE", 115200, 2, 0.4, 0, log=lambda s: None)
    m._serial = fake                     # 跳过真串口
    got = await m.read(3, 0x1000, 9, unit=2)
    ok = got == expect
    print("  [%s] %-28s chunk=%-2d -> %s"
          % ("PASS" if ok else "FAIL", label, chunk_size,
             got if ok else "%s (期望 %s)" % (got, expect)))
    return ok


async def main():
    print("=== RtuMaster 帧解析（模拟从站）===")
    results = []
    # chunk_size 越小越容易复现"分次读"问题：1 字节 = 每个字段都可能被拆开
    for cs in (1, 2, 3, 5, 21):
        results.append(await run_case("读 0x1000x9", cs))

    # M512 的 8 字节错误帧（功能码不置 0x80）
    print("\n=== M512 非标准错误帧识别 ===")
    class ErrSerial(FakeSerial):
        def write(self, data):
            unit = data[0]
            fc = data[1]
            # [站号][fc][80][01][00][code][CRC2]
            self.tx += append_crc(bytes([unit, fc, 0x80, 0x01, 0x00, 0x05]))
    m = RtuMaster("FAKE", 115200, 2, 0.4, 0, log=lambda s: None)
    m._serial = ErrSerial([])
    try:
        await m.read(3, 0x3000, 2, unit=2)   # 数量 2 会触发本机错误帧
        print("  [FAIL] 未识别出错误帧")
        results.append(False)
    except Exception as exc:
        good = "Modbus 异常" in str(exc) or "code=5" in str(exc)
        print("  [%s] 识别为异常应答：%s" % ("PASS" if good else "FAIL", exc))
        results.append(good)

    print("\n%s  （%d/%d 通过）"
          % ("全部通过" if all(results) else "有失败",
             sum(results), len(results)))
    return 0 if all(results) else 1


sys.exit(asyncio.run(main()))
