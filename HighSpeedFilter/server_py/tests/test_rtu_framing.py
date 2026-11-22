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

# [输出编码 2026-11-14] 中文 Windows 默认代码页 cp936 ⇒ stdout 被重定向
# 到文件时用 GBK ⇒ 打印「⇒」(U+21D2，**GBK 编不了**) 直接
# UnicodeEncodeError ⇒ **整套测试在打印断言时被打断**（实测退出码仍是 0，
# 看着像"跑完了"）。强制 UTF-8 + errors="replace"（写不出去也不崩）。
for _stream in (sys.stdout, sys.stderr):
    try:
        _stream.reconfigure(encoding="utf-8", errors="replace")
    except (AttributeError, ValueError, OSError):
        pass

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

    # ---- 读路径：错误帧后面**多带一个尾字节** ⇒ 仍要认出异常码 ----
    # [2026-11-18 帧层深检] 判据原是「帧长 ≤8」⇒ 9 字节时掉进「读应答
    # 不完整：需要 133 实际 9」分支 ⇒ **异常码丢失**（仍 fail-closed，
    # 只是现场看不到 code=5 说不清原因）⇒ 改用「形状 + 前 8 字节 CRC」。
    print("\n=== M512 错误帧带尾字节（读路径）===")

    class ErrTailSerial(FakeSerial):
        def write(self, data):
            unit = data[0]
            fc = data[1]
            self.tx += append_crc(bytes([unit, fc, 0x80, 0x01, 0x00, 0x05]))
            self.tx += b"\x00"      # 同一次读里粘上一个尾字节

    mt = RtuMaster("FAKE", 115200, 2, 0.4, 0, log=lambda s: None)
    mt._serial = ErrTailSerial([])
    try:
        await mt.read(3, 0x3000, 2, unit=2)
        print("  [FAIL] 带尾字节的错误帧未被识别")
        results.append(False)
    except Exception as exc:
        good = "code=5" in str(exc) or "Modbus 异常" in str(exc)
        print("  [%s] 带尾字节仍认出异常码：%s"
              % ("PASS" if good else "FAIL", exc))
        results.append(good)

    # ---- 写路径（FC06）的 M512 错误帧：必须抛异常，不能当成功 ----
    # [2026-11-18 审计] 此前写路径无覆盖 ⇒ 错误帧（8 字节、CRC 合法）
    # 命中「正常回显」分支被当成写成功 ⇒ 驱动拒收停机/给定也报成功。
    print("\n=== M512 错误帧（写路径 FC06）===")
    class ErrWriteSerial(FakeSerial):
        def write(self, data):
            unit = data[0]
            fc = data[1]
            self.tx += append_crc(bytes([unit, fc, 0x80, 0x01, 0x00, 0x03]))

    mw = RtuMaster("FAKE", 115200, 2, 0.4, 0, log=lambda s: None)
    mw._serial = ErrWriteSerial([])
    try:
        await mw.write_single(6, 0x2000, 1, unit=2)
        print("  [FAIL] 写错误帧未被识别（被当成写成功）")
        results.append(False)
    except Exception as exc:
        good = ("异常" in str(exc)) or ("code=3" in str(exc))
        print("  [%s] 写路径错误帧识别为异常：%s"
              % ("PASS" if good else "FAIL", exc))
        results.append(good)

    # ---- 写路径的**正常回显**：必须被接受（2026-11-18 服务端回归补）----
    # [为什么补] 上一条只锁了「错误帧要报错」；**成功回显分支当时完全
    # 没测** ⇒ 那条新判定就插在没有护栏的地方（本轮就查出了 R1 潜伏
    # 缺陷：写地址 0x8001 时正常回显的形状与 M512 错误帧撞车 ⇒ 写成功
    # 被误报成失败）。⇒ 这三条把「成功路径」钉死。
    print("\n=== 写路径正常回显（FC06 / FC05）===")

    class EchoSerial(FakeSerial):
        """标准回显：[站号][fc][addrH][addrL][valH][valL][CRC2]。"""

        def write(self, data):
            body = bytes(data[:-2])          # 去 CRC
            self.tx += append_crc(body)

    async def _write_echo_ok(label, function, address, value):
        m = RtuMaster("FAKE", 115200, 2, 0.4, 0, log=lambda s: None)
        m._serial = EchoSerial([])
        try:
            await m.write_single(function, address, value, unit=2)
            print("  [PASS] %s" % label)
            results.append(True)
        except Exception as exc:                      # noqa: BLE001
            print("  [FAIL] %s：%s" % (label, exc))
            results.append(False)

    await _write_echo_ok("FC06 正常回显被接受（0x2000=1）", 6, 0x2000, 1)
    # [R1 的回归锁] 写地址 0x8001 ⇒ 回显前 4 字节 = 80 01（与 M512 错误帧
    # 形状撞车）⇒ 必须仍判为**写成功**，不能误报异常。
    await _write_echo_ok("FC06 正常回显被接受（**地址 0x8001**，形状撞车）",
                         6, 0x8001, 0x1234)
    await _write_echo_ok("FC05 正常回显被接受（线圈 0x0003=0xFF00）",
                         5, 0x0003, 0xFF00)

    # ---- 写路径 FC05 的 M512 错误帧：同样必须报错（上一条只锁了 FC06）----
    print("\n=== M512 错误帧（写路径 FC05）===")

    class ErrCoilSerial(FakeSerial):
        def write(self, data):
            unit = data[0]
            fc = data[1]
            self.tx += append_crc(bytes([unit, fc, 0x80, 0x01, 0x00, 0x03]))

    mc = RtuMaster("FAKE", 115200, 2, 0.4, 0, log=lambda s: None)
    mc._serial = ErrCoilSerial([])
    try:
        await mc.write_single(5, 0x0003, 0xFF00, unit=2)
        print("  [FAIL] FC05 错误帧未被识别（被当成写成功）")
        results.append(False)
    except Exception as exc:
        good = ("异常" in str(exc)) or ("code=3" in str(exc))
        print("  [%s] FC05 写路径错误帧识别为异常：%s"
              % ("PASS" if good else "FAIL", exc))
        results.append(good)

    print("\n%s  （%d/%d 通过）"
          % ("全部通过" if all(results) else "有失败",
             sum(results), len(results)))
    return 0 if all(results) else 1


sys.exit(asyncio.run(main()))
