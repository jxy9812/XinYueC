# -*- coding: utf-8 -*-
"""变频器参数读写（0x011B）的**帧层 + socket 级**回归（2026-11-18 补洞）。

[为什么补这两层] 服务端回归 27/27 段全绿，但 0x011B 仍有两处覆盖缺口：
    A. **帧层**：`tests/fake_devices.FakeRtuMaster` **永远立刻应答** ⇒
       `hsfsrv/devices.py` 里两条**真机约束**在测试里完全跑不到：
         ① 0x3000 / 0x8000 的**数量必须 = 1**（连读 ≥2 直接回 M512 错误帧
            err=0x05）——设备层要是连读，真机直接拒，根本取不到值；
         ② **写后 0.3~0.5s 不应答窗口**——写完立刻读会超时 ⇒ 回读要
            ≤3 次、间隔 0.2s。
       真实设备层代码（read_params_snapshot / write_param）是跑的，缺的
       正是**帧层与时间窗**。⇒ 本文件造一台**可控的内存 M512 从站**
       （把上面两条怪癖建模进去），用**真 RtuMaster**（真 CRC、真帧解析、
       真超时、真重试）驱动它。
    B. **socket 级**：`tests/test_vfd_params.py` 直接调
       `business.handle_request` + 假 session ⇒ BSON 编解码/处理器/权限闸/
       RunLocked 闸/失败包络都是真的，但**没走 XLink 帧收发与 requestId
       归属**。⇒ 本文件补真 socket + 真 XLink 帧。

[测什么]
    A 帧层：连读 ≥2 被真机拒（且设备层自己只连读 1）/ 写后不应答窗口的
      重试与间隔 / 写后回读不一致不谎报成功 / FC06 的 M512 错误帧不许当
      写成功（地址 0x8001 的正常回显形状撞车仍要放行）。
    B socket 级：读 / 写（管理员）/ 权限闸 / RunLocked 闸（**不发总线写**）
      / 失败包络 / requestId 归属（两条并发请求各自拿到自己的应答）。

[怎么跑]
    cd HighSpeedFilter/server_py && python tests/test_vfd_params_frame.py

[端口纪律] socket 段用 `cfg.port = 0`（系统随机分配空闲端口）⇒ 不碰
6100/6200 上别人在跑的 sim。
"""

import asyncio
import os
import shutil
import struct
import sys
import tempfile
import threading
import time

# [输出编码 2026-11-14] 中文 Windows 默认代码页 cp936 ⇒ stdout 被重定向
# 到文件时用 GBK ⇒ 打印「⇒」(U+21D2，**GBK 编不了**) 直接
# UnicodeEncodeError ⇒ **整套测试在打印断言时被打断**（实测退出码仍是 0，
# 看着像"跑完了"）。强制 UTF-8 + errors="replace"（写不出去也不崩）。
for _stream in (sys.stdout, sys.stderr):
    try:
        _stream.reconfigure(encoding="utf-8", errors="replace")
    except (AttributeError, ValueError, OSError):
        pass

HERE = os.path.dirname(os.path.abspath(__file__))
SRV = os.path.dirname(HERE)            # server_py/
sys.path.insert(0, HERE)
sys.path.insert(0, SRV)

from hsfsrv import auth as auth_mod          # noqa: E402
from hsfsrv import bson                      # noqa: E402
from hsfsrv import config as cfgmod          # noqa: E402
from hsfsrv import devices, modbus, permissions, proto, virtualdev, xlink  # noqa: E402,E501
from fake_devices import build_fake_app      # noqa: E402
from test_vfd_ops import Client              # noqa: E402

PASS = 0
FAIL = 0
FAILED = []

#: P1 组地址（与 tests/test_vfd_params.py 同口径）。
ADDR_P100 = 0xF100
ADDR_P101 = 0xF101


def check(name, ok, detail=""):
    """记一条断言结果（口径与其它 tests 一致：PASS/FAIL 计数 + 明细）。"""
    global PASS, FAIL
    if ok:
        PASS += 1
        print("  [PASS] %s %s" % (name, detail))
    else:
        FAIL += 1
        FAILED.append(name)
        print("  [FAIL] %s %s" % (name, detail))


# ============================================================================
#              A 帧层：可控的内存 M512 从站 + 真 RtuMaster
# ============================================================================

class FakeM512Serial(object):
    """内存串口：一台 M512 变频器（含真机的几条怪癖）。

    [建模的真机行为]
      ① **0x3000 / 0x8000 数量必须 = 1** —— 连读 ≥2 回 M512 错误帧
         （[站号][fc][80][01][00][code][CRC2]，code=0x05）；
      ② **写后不应答窗口** —— 写完的随后 N 次读请求不应答（真机 0.3~0.5s，
         从站要重建通讯任务）；
      ③ **异常应答是 8 字节且功能码不置 0x80**（标准判定认不出来）；
      ④ 正常 FC06 应答 = 请求**原样回显**（8 字节）。

    [为什么自己造而不复用 FakeRtuMaster] FakeRtuMaster 永远立刻应答 ⇒
    上面 ①② 两条在测试里根本触发不到；本类把它们做成**可注入的行为**
    （窗口长度 / 拒写地址 / 错误帧地址），真 RtuMaster 照常解析。
    """

    #: M512 错误帧里的异常码。
    ERR_ILLEGAL_FUNCTION = 0x01
    ERR_ILLEGAL_DATA = 0x02
    ERR_DATA_ERR = 0x03
    ERR_CONFIRM = 0x05

    def __init__(self, unit=2, regs=None):
        self.unit = unit
        self.regs = dict(regs or {})
        self.tx = bytearray()
        self.is_open = True
        self.requests = []            # [(fc, addr, count), ...] 全部请求
        self.writes = []              # [(addr, value), ...] 真正落了的写
        self.silent = False           # 全静默（任何请求都不回）
        self.no_answer_window = 0     # 写成功后，后续多少次读不应答
        self.no_answer_reads = 0      # 还剩几次读不应答
        self.deny_write = set()       # 这些地址：ACK 但**不落**（拒写）
        self.err_write = set()        # 这些地址：写回 M512 错误帧
        self.err_read = set()         # 这些地址：读回 M512 错误帧

    # ---- pyserial 子集（真 RtuMaster 调得到的那几个）----
    def reset_input_buffer(self):
        self.tx.clear()

    def flush(self):
        pass

    def close(self):
        self.is_open = False

    def read(self, n):
        if not self.tx:
            return b""
        take = min(n, len(self.tx))
        out = bytes(self.tx[:take])
        del self.tx[:take]
        return out

    # ---- 事务 ----
    def write(self, data):
        body = bytes(data[:-2])              # 去 CRC
        unit, fc = body[0], body[1]
        addr = (body[2] << 8) | body[3]
        arg = (body[4] << 8) | body[5]       # FC03=数量 / FC06=值
        self.requests.append((fc, addr, arg))
        if fc == 3:
            self._handle_read(unit, fc, addr, arg)
        elif fc == 6:
            self._handle_write(unit, fc, addr, arg)
        else:
            self._send_err(unit, fc, self.ERR_ILLEGAL_FUNCTION)

    def _handle_read(self, unit, fc, addr, count):
        if self.silent:
            return
        if self.no_answer_reads > 0:
            # [真机约束②] 写后不应答窗口内：从站不应答（只能靠超时判失败）
            self.no_answer_reads -= 1
            return
        if addr in self.err_read:
            self._send_err(unit, fc, self.ERR_ILLEGAL_DATA)
            return
        # [真机约束①] 0x3000 / 0x8000 数量必须 = 1（连读 ≥2 直接错误帧）
        if addr in (0x3000, 0x8000) and count != 1:
            self._send_err(unit, fc, self.ERR_CONFIRM)
            return
        vals = []
        for i in range(count):
            a = addr + i
            if a not in self.regs:
                self._send_err(unit, fc, self.ERR_ILLEGAL_DATA)
                return
            vals.append(int(self.regs[a]) & 0xFFFF)
        payload = b"".join(struct.pack(">H", v) for v in vals)
        self.tx += modbus.append_crc(
            bytes([unit, fc, len(payload)]) + payload)

    def _handle_write(self, unit, fc, addr, value):
        if self.silent:
            return
        if addr in self.err_write:
            self._send_err(unit, fc, self.ERR_DATA_ERR)
            return
        if addr not in self.deny_write:
            self.regs[addr] = value & 0xFFFF
            self.writes.append((addr, value & 0xFFFF))
            # [真机约束②] 写后进入不应答窗口（从站重建通讯任务）
            self.no_answer_reads = self.no_answer_window
        # 正常应答 = 请求原样回显（8 字节）
        self.tx += modbus.append_crc(bytes([unit, fc,
                                             (addr >> 8) & 0xFF, addr & 0xFF,
                                             (value >> 8) & 0xFF,
                                             value & 0xFF]))

    def _send_err(self, unit, fc, code):
        """M512 错误帧：[站号][fc][80][01][00][code][CRC2]（8 字节）。"""
        self.tx += modbus.append_crc(
            bytes([unit, fc, 0x80, 0x01, 0x00, code]))


def _new_serial(unit=2):
    """一台"停机、无故障"的 M512：寄存器表 = 模拟器缺省参数 + 状态字。"""
    regs = dict(virtualdev.VirtualVfd.PARAM_DEFAULTS)
    regs[0x3000] = 3          # 运行状态字：3 = 停机
    regs[0x8000] = 0          # 故障码：0 = 无故障
    return FakeM512Serial(unit=unit, regs=regs)


def _make_device(serial, gap=None, timeout=0.05, retries=0, unit=2):
    """真 RtuMaster（挂内存串口）+ 真 VfdM512 设备层。

    :param gap: 回读重试间隔（秒）。None = 用产品缺省 0.2s；给小数是为了
        让"三次都不应答"那条用例别真等 0.4s（间隔**契约**另有断言锁）。
    :param timeout: 单次事务超时（秒）。产品缺省 0.4s（真机慢从站），
        这里取 0.05s —— 帧层要验的是**形状**（超时→重试），不是等待时长。
    :param retries: master 级重试次数。0 = 每次事务只发一次 ⇒ 设备层的
        回读重试次数可被精确观测（不受 master 重试干扰）。
    """
    master = modbus.RtuMaster("FAKE", 115200, unit, timeout, retries,
                              log=lambda s: None)
    master._serial = serial
    cls = devices.VfdM512
    if gap is not None:
        cls = type("_FastGapVfd", (devices.VfdM512,),
                   {"PARAM_WRITE_READBACK_GAP_S": gap})
    dev = cls(lambda: master, unit, log=lambda m: None)
    return dev, master


def _count_reads(serial, addr):
    """统计对某地址发过的 FC03 读请求数（回读次数的可观测点）。"""
    return len([1 for (fc, a, _c) in serial.requests
                if fc == 3 and a == addr])


class _SleepSpy(object):
    """记录 `devices` 模块里 asyncio.sleep 的调用（间隔契约可观测）。

    [为什么要它] 回读重试间隔（0.2s）是**契约**（真机写后 0.3~0.5s
    不应答窗口 ⇒ 回读必须跨过它）。若只靠"耗时"判断，分不清"等了间隔"
    与"恰好超时这么久"；若真等 0.3~0.5s 又拖慢整套回归。
    ⇒ 记录调用次数与参数（间隔值），时间窗本身由假从站的"吞应答"建模，
    **不真等**（真机那 0.3~0.5s 窗口是设备行为，测试里由假从站吞应答代替；
    0.2s 的重试间隔是真等的——它就是要验的契约本身）。

    用法：`with _SleepSpy() as spy: await dev.write_param(...)`
    """

    def __init__(self):
        self.sleeps = []
        self._saved = None

    def __enter__(self):
        self._saved = devices.asyncio
        spy = self

        class _Shim(object):
            def __init__(self, real):
                self._real = real

            def __getattr__(self, name):
                return getattr(self._real, name)

            async def sleep(self, delay, *a, **kw):
                spy.sleeps.append(delay)
                return await self._real.sleep(delay, *a, **kw)

        devices.asyncio = _Shim(self._saved)
        return self

    def __exit__(self, *exc):
        devices.asyncio = self._saved
        return False


async def _a1_read_count():
    """A1：读参数组时 0x3000 / 0x8000 的**连读数量必须 = 1**。"""
    print("=== A1 帧层：连读数量约束（0x3000 / 0x8000 数量必须=1）===")
    serial = _new_serial()
    dev, master = _make_device(serial)
    snap = await dev.read_params_snapshot()
    reads = {}
    for (fc, addr, cnt) in serial.requests:
        reads[(fc, addr)] = cnt
    check("A1a 快照读出 6 个参数寄存器（P1.00~P1.05）",
          len(snap.get("regs") or {}) == 6, "%r" % (snap.get("regs"),))
    check("A1b 运行状态字 0x3000 的读数量 == 1（真机硬约束）",
          reads.get((3, 0x3000)) == 1, "%r" % (reads,))
    check("A1c 故障码 0x8000 的读数量 == 1（真机硬约束）",
          reads.get((3, 0x8000)) == 1, "%r" % (reads,))
    check("A1d 参数组 0xF100 是一帧连读 6 个（手册允许）",
          reads.get((3, 0xF100)) == 6, "%r" % (reads,))
    check("A1e 快照值与寄存器一致（P1.02=380V / 停机 / 无故障）",
          (snap.get("regs") or {}).get(0xF102) == 380
          and snap.get("runState") == 3
          and snap.get("faulted") is False, "%r" % (snap,))


async def _a2_multi_read_rejected():
    """A2：连读 ≥2 ⇒ 真机回错误帧 ⇒ 帧层必须识别为异常（fail-closed）。"""
    print("=== A2 帧层：连读 ≥2 被真机拒（err=0x05）===")
    serial = _new_serial()
    dev, master = _make_device(serial)
    exc = None
    try:
        await master.read(3, 0x3000, 2, unit=2)
    except Exception as err:                        # noqa: BLE001
        exc = err
    check("A2a 连读 0x3000×2 ⇒ 识别为 Modbus 异常（code=5）",
          isinstance(exc, modbus.ModbusException) and exc.code == 5,
          "%r" % (exc,))
    exc2 = None
    try:
        await master.read(3, 0x8000, 2, unit=2)
    except Exception as err:                        # noqa: BLE001
        exc2 = err
    check("A2b 连读 0x8000×2 ⇒ 识别为 Modbus 异常（code=5）",
          isinstance(exc2, modbus.ModbusException) and exc2.code == 5,
          "%r" % (exc2,))
    # 设备层自己不许连读 ⇒ 快照读仍然成功（没踩这条约束）
    snap = await dev.read_params_snapshot()
    check("A2c 设备层读快照仍成功（它只连读 1 个 ⇒ 真机不拒）",
          snap.get("runState") == 3, "%r" % (snap,))


async def _a3_read_fail_closed():
    """A3：状态字读失败 ⇒ 整个快照失败（**不给半套假值**）。"""
    print("=== A3 帧层：读失败必须 fail-closed（不给半套假值）===")
    serial = _new_serial()
    serial.err_read.add(0x3000)         # 状态字回错误帧
    dev, master = _make_device(serial)
    snap = None
    err = None
    try:
        snap = await dev.read_params_snapshot()
    except Exception as exc:                        # noqa: BLE001
        err = exc
    check("A3a 状态字读失败 ⇒ 整个快照失败（没有半套假值）",
          snap is None and isinstance(err, modbus.ModbusError),
          "snap=%r err=%r" % (snap, err))
    check("A3b 失败原因可读（异常应答带 code，不是静默取错值）",
          isinstance(err, modbus.ModbusException)
          and "code=" in str(err), "%r" % (err,))


async def _a4_write_no_answer_window():
    """A4：写后不应答窗口 ⇒ 回读重试（≤3 次、间隔 0.2s）。"""
    print("=== A4 帧层：写后不应答窗口（重试 + 间隔 + 最终判定）===")
    # ---- A4a：第一次回读不应答，第二次给 ⇒ 重试后成功 ----
    serial = _new_serial()
    serial.no_answer_window = 1
    dev, master = _make_device(serial)
    with _SleepSpy() as spy:
        applied, raw_now, err = await dev.write_param(ADDR_P101, 220)
    reads = _count_reads(serial, ADDR_P101)
    check("A4a 写后不应答：重试后成功 applied=True（按回读值判定）",
          applied is True and raw_now == 220 and err == "",
          "%r" % ((applied, raw_now, err),))
    check("A4b 回读确实重试了（读 0xF101 两次）",
          reads == 2, "reads=%d" % reads)
    check("A4c 两次回读之间等了一个间隔（缺省 0.2s）",
          spy.sleeps == [devices.VfdM512.PARAM_WRITE_READBACK_GAP_S],
          "%r" % (spy.sleeps,))
    check("A4d 写帧只发了一次（不应答窗口只影响读）",
          len([1 for (fc, _a, _c) in serial.requests if fc == 6]) == 1,
          "%r" % (serial.requests,))

    # ---- A4b：三次回读都不应答 ⇒ applied=False（不谎报成功）----
    serial2 = _new_serial()
    serial2.no_answer_window = 99
    dev2, master2 = _make_device(serial2, gap=0.02)
    with _SleepSpy() as spy2:
        applied2, raw_now2, err2 = await dev2.write_param(ADDR_P101, 220)
    reads2 = _count_reads(serial2, ADDR_P101)
    check("A4e 三次回读都不应答 ⇒ applied=False（不谎报成功）",
          applied2 is False and raw_now2 is None,
          "%r" % ((applied2, raw_now2),))
    check("A4f 失败原因可读（回读失败 + 写了重试次数）",
          "回读失败" in (err2 or "") and "3" in (err2 or ""),
          "err=%r" % (err2,))
    check("A4g 回读重试 ≤3 次（读 0xF101 三次，不无限重试）",
          reads2 == 3, "reads=%d" % reads2)
    check("A4h 两次重试各隔一个间隔（共 2 次等待）",
          len(spy2.sleeps) == 2, "%r" % (spy2.sleeps,))


async def _a5_readback_mismatch():
    """A5：写后回读值 ≠ 写入值 ⇒ applied=False 且不谎报成功。"""
    print("=== A5 帧层：写后回读不一致（不谎报成功）===")
    serial = _new_serial()
    serial.deny_write.add(ADDR_P101)     # 真机拒写但 ACK（回显照回、不落）
    dev, master = _make_device(serial)
    applied, raw_now, err = await dev.write_param(ADDR_P101, 220)
    check("A5a 回读不一致 ⇒ applied=False",
          applied is False, "applied=%r" % (applied,))
    check("A5b 回读值 = 寄存器实际值（150，不是提交的 220）",
          raw_now == 150, "raw_now=%r" % (raw_now,))
    check("A5c 失败原因可读（写入未生效 + 两个值）",
          ("未生效" in (err or "")) and ("150" in (err or ""))
          and ("220" in (err or "")), "err=%r" % (err,))
    check("A5d 寄存器实际没变（拒写未落）",
          serial.regs.get(ADDR_P101) == 150,
          "raw=%r" % (serial.regs.get(ADDR_P101),))


async def _a6_write_error_frame():
    """A6：写路径收到 M512 错误帧 ⇒ 必须当异常（不许当写成功）。"""
    print("=== A6 帧层：FC06 的 M512 错误帧不许当写成功 ===")
    serial = _new_serial()
    serial.err_write.add(ADDR_P101)     # 写回 [站号][fc][80][01][00][03]
    dev, master = _make_device(serial)
    applied, raw_now, err = await dev.write_param(ADDR_P101, 220)
    check("A6a 写路径错误帧 ⇒ applied=False（不许当写成功）",
          applied is False and raw_now is None,
          "%r" % ((applied, raw_now),))
    check("A6b 失败原因含异常应答（驱动器拒收）",
          ("异常" in (err or "")) or ("code=" in (err or "")), "err=%r" % (err,))
    check("A6c 寄存器没被改（错误帧不是写成功）",
          serial.regs.get(ADDR_P101) == 150
          and not [1 for (a, _v) in serial.writes if a == ADDR_P101],
          "raw=%r writes=%r" % (serial.regs.get(ADDR_P101), serial.writes))


async def _a7_write_echo_ok():
    """A7：正常 FC06 回显必须被接受（含**地址 0x8001 形状撞车**）。"""
    print("=== A7 帧层：正常 FC06 回显（含 0x8001 撞车）===")
    serial = _new_serial()
    dev, master = _make_device(serial)
    applied, raw_now, err = await dev.write_param(ADDR_P101, 220)
    check("A7a 正常 FC06 回显 ⇒ applied=True",
          applied is True and raw_now == 220 and err == "",
          "%r" % ((applied, raw_now, err),))
    # [形状撞车] 写地址 0x8001 ⇒ 回显前 4 字节 = 80 01（与 M512 错误帧
    # 一模一样）⇒ 判定必须**与请求对账**，不能只看形状，否则写成功被
    # 误报成失败。
    applied2, raw_now2, err2 = await dev.write_param(0x8001, 0x1234)
    check("A7b 写地址 0x8001（回显形状与错误帧撞车）仍判成功",
          applied2 is True and raw_now2 == 0x1234 and err2 == "",
          "%r" % ((applied2, raw_now2, err2),))


def _a8_constants():
    """A8：写后回读的**契约常量**（重试次数 / 间隔 / 组长度）。"""
    print("=== A8 帧层：写后回读契约常量 ===")
    check("A8a 回读重试次数缺省 = 3",
          devices.VfdM512.PARAM_WRITE_READBACK_TRIES == 3,
          "%r" % (devices.VfdM512.PARAM_WRITE_READBACK_TRIES,))
    check("A8b 回读间隔缺省 = 0.2s（跨真机 0.3~0.5s 不应答窗口）",
          abs(devices.VfdM512.PARAM_WRITE_READBACK_GAP_S - 0.2) < 1e-9,
          "%r" % (devices.VfdM512.PARAM_WRITE_READBACK_GAP_S,))
    check("A8c 参数组一帧连读长度 = 6（0xF100~0xF105）",
          devices.VfdM512.PARAM_P1_COUNT == 6,
          "%r" % (devices.VfdM512.PARAM_P1_COUNT,))


# ============================================================================
#              B socket 级：真 XLink 帧（真 socket + 真 requestId 归属）
# ============================================================================

async def _spin_server(basedir, log):
    """起一台假设备服务端（端口 0 = 随机空闲端口，不碰 6100/6200）。"""
    cfg = cfgmod.Config()
    cfg.resolve_paths(basedir)
    cfg.config_path = os.path.join(basedir, "cfg", "x.json")
    cfg.users_path = os.path.join(basedir, "cfg", "hsf_users.json")
    os.makedirs(os.path.join(basedir, "cfg"), exist_ok=True)
    cfg.port = 0                      # 随机空闲端口
    cfg.enable_serial = False
    users = auth_mod.UserStore(cfg.users_path)
    users.create_defaults()           # admin/oper/root 三档
    business = build_fake_app(cfg, log=log)

    # [总线计数] "RunLocked/权限闸不发总线写"要**可观测** ⇒ 给假 master
    # 套一层计数（不改产品代码）。
    counters = {"read": 0, "write": 0}
    master = business.vfd._master_get()
    _orig_read = master.read
    _orig_write = master.write_single

    async def _counted_read(*a, **kw):
        counters["read"] += 1
        return await _orig_read(*a, **kw)

    async def _counted_write(*a, **kw):
        counters["write"] += 1
        return await _orig_write(*a, **kw)

    master.read = _counted_read
    master.write_single = _counted_write
    business._bus_counters = counters

    # 先喂一拍：假变频器的状态字 0x3000 由 poll 写入；不喂的话参数读
    # 会撞「未建模地址」⇒ 读面必然假红。
    await business.vfd.poll(int(time.monotonic() * 1000))

    server = xlink.Server(cfg.port, users, log=log, require_auth=True)
    business.attach(server)
    await server.start()
    return business, server


async def _feed_while(business, thread):
    """客户端线程跑期间替假设备喂数据（状态字 0x3000 要新鲜）。"""
    while thread.is_alive():
        try:
            await business.vfd.poll(int(time.monotonic() * 1000))
        except Exception:                              # noqa: BLE001
            pass
        await asyncio.sleep(0.02)


def _wait_run_state(business, want, timeout=3.0):
    """等假变频器的状态字变成期望值（0x3000：1运行/2反转/3停机）。"""
    deadline = time.time() + timeout
    while time.time() < deadline:
        if business.vfd.raw.get(devices.VfdM512.ADDR_STATUS) == want:
            return True
        time.sleep(0.02)
    return False


def _run_socket_checks(business, server):
    """socket 级：真 XLink 帧打 0x011B（读/写/权限/RunLocked/归属）。"""
    counters = business._bus_counters
    c = Client("127.0.0.1", server.port)
    try:
        c.hello()
        ok, info = c.login("root", "root")
        if not ok:
            raise RuntimeError("root 登录失败：%r" % (info,))

        # ---- B1 读（登录即可）----
        business.vfd._running = False
        _wait_run_state(business, 3)
        r = c.request(proto.CMD_VFD_PARAMS,
                      bson.encode({"op": proto.VFD_PARAMS_OP_READ}))
        check("B1 读应答 ok=true",
              r is not None and r[1].get("ok") is True, "%r" % (r,))
        check("B1b 应答回带同一 msgCode（0x011B）",
              r is not None and r[0] == proto.CMD_VFD_PARAMS,
              "%r" % (r[0] if r else None,))
        params = (r[1].get(proto.KEY_VFD_PARAMS) or []) if r else []
        check("B1c 参数组 6 条（P1.00~P1.05）", len(params) == 6,
              "n=%d" % len(params))
        by_addr = dict((p["addr"], p) for p in params if isinstance(p, dict))
        p101 = by_addr.get(ADDR_P101, {})
        check("B1d 数值型参数键面与契约一致（读不带 applied）",
              set(p101.keys())
              == {"code", "label", "addr", "raw", "value", "unit",
                  "writable", "runLocked"},
              "%r" % (sorted(p101.keys()),))
        p100 = by_addr.get(ADDR_P100, {})
        check("B1d2 枚举型参数带 options/valueText（且读仍不带 applied）",
              isinstance(p100.get("options"), list)
              and p100.get("valueText") == "普通异步电机"
              and proto.KEY_PARAM_APPLIED not in p100,
              "%r" % (sorted(p100.keys()),))
        check("B1e 运行状态字回显（停机=3）+ vfdFaulted=False",
              (r[1].get(proto.KEY_VFD_RUN_WORD) == 3)
              and (r[1].get(proto.KEY_VFD_FAULTED) is False),
              "runState=%r faulted=%r" % (r[1].get(proto.KEY_VFD_RUN_WORD),
                                          r[1].get(proto.KEY_VFD_FAULTED)))

        # ---- B2 写（管理员）----
        writes_before = counters["write"]
        r = c.request(proto.CMD_VFD_PARAMS, bson.encode({
            "op": proto.VFD_PARAMS_OP_WRITE,
            "values": {str(ADDR_P100): 1}}))
        check("B2 管理员写 ok=true",
              r is not None and r[1].get("ok") is True, "%r" % (r,))
        by_addr = dict((p["addr"], p)
                       for p in (r[1].get(proto.KEY_VFD_PARAMS) or [])
                       if isinstance(p, dict))
        check("B2b 写后回读 params 用**回读值**（raw=1）且 applied=True",
              by_addr.get(ADDR_P100, {}).get("raw") == 1
              and by_addr.get(ADDR_P100, {}).get(proto.KEY_PARAM_APPLIED)
              is True, "%r" % (by_addr.get(ADDR_P100),))
        check("B2c 命令真的落到了寄存器（不是只回了个成功）",
              business.vfd.raw.get(ADDR_P100) == 1,
              "raw=%r" % (business.vfd.raw.get(ADDR_P100),))
        check("B2d 确实发了总线写", counters["write"] == writes_before + 1,
              "writes=%d" % counters["write"])

        # ---- B3 普通用户写 ⇒ 权限闸（与 0x0117 同款）----
        c2 = Client("127.0.0.1", server.port)
        try:
            c2.hello()
            ok2, info2 = c2.login("admin", "admin")     # 普通用户档
            if not ok2:
                raise RuntimeError("admin 登录失败：%r" % (info2,))
            writes_before = counters["write"]
            reads_before = counters["read"]
            r2 = c2.request(proto.CMD_VFD_PARAMS, bson.encode({
                "op": proto.VFD_PARAMS_OP_WRITE,
                "values": {str(ADDR_P101): 500}}))
            check("B3 普通用户写被拒 ok=false",
                  r2 is not None and r2[1].get("ok") is False, "%r" % (r2,))
            check("B3b 拒绝文案与 0x0117 同款（管理员权限）",
                  r2 is not None
                  and r2[1].get("err")
                  == permissions.denied_text(proto.PERM_ACCOUNT_ADMIN),
                  "err=%r" % (r2[1].get("err") if r2 else None,))
            check("B3c 被拒时不发总线写", counters["write"] == writes_before,
                  "writes=%d" % counters["write"])
            check("B3d 被拒时连总线读都没有（权限闸在碰总线之前）",
                  counters["read"] == reads_before,
                  "reads=%d" % counters["read"])
            check("B3e 寄存器没被改",
                  business.vfd.raw.get(ADDR_P101)
                  == virtualdev.VirtualVfd.PARAM_DEFAULTS[ADDR_P101],
                  "raw=%r" % (business.vfd.raw.get(ADDR_P101),))
        finally:
            c2.close()

        # ---- B4 requestId 归属：两条并发请求各自拿到自己的应答 ----
        business.vfd._running = False
        _wait_run_state(business, 3)
        rid_w = c.req_id + 1
        rid_r = c.req_id + 2
        c.req_id = rid_r
        c._send(proto.MSGCLASS_REQUEST, proto.CMD_VFD_PARAMS,
                proto.DATATYPE_BSON,
                bson.encode({"op": proto.VFD_PARAMS_OP_WRITE,
                             "values": {str(ADDR_P100): 2}}), rid_w)
        c._send(proto.MSGCLASS_REQUEST, proto.CMD_VFD_PARAMS,
                proto.DATATYPE_BSON,
                bson.encode({"op": proto.VFD_PARAMS_OP_READ}), rid_r)
        c.pump(3.0)
        rw = c.pending.pop(rid_w, None)
        rr = c.pending.pop(rid_r, None)
        check("B4a 两条并发请求都拿到应答（requestId 归属没串）",
              rw is not None and rr is not None,
              "rw=%r rr=%r" % (rw is not None, rr is not None))
        if rw is not None and rr is not None:
            pw = dict((p["addr"], p)
                      for p in (rw[1].get(proto.KEY_VFD_PARAMS) or [])
                      if isinstance(p, dict))
            pr = dict((p["addr"], p)
                      for p in (rr[1].get(proto.KEY_VFD_PARAMS) or [])
                      if isinstance(p, dict))
            check("B4b 写应答带 applied、读应答不带（两条没互相串台）",
                  pw.get(ADDR_P100, {}).get(proto.KEY_PARAM_APPLIED) is True
                  and proto.KEY_PARAM_APPLIED not in pr.get(ADDR_P100, {}),
                  "write=%r read=%r" % (pw.get(ADDR_P100),
                                        pr.get(ADDR_P100)))

        # ---- B5 RunLocked（运行中）⇒ 拒写且**不发总线写**----
        business.vfd._running = True
        got = _wait_run_state(business, 1)
        check("B5a 前置：状态字已是运行中（0x3000=1）", got,
              "status=%r" % (business.vfd.raw.get(0x3000),))
        writes_before = counters["write"]
        raw_before = business.vfd.raw.get(ADDR_P101)
        r = c.request(proto.CMD_VFD_PARAMS, bson.encode({
            "op": proto.VFD_PARAMS_OP_WRITE,
            "values": {str(ADDR_P101): 600}}))
        check("B5 运行中写被拒 ok=false",
              r is not None and r[1].get("ok") is False, "%r" % (r,))
        check("B5b err 文案 = 三端同款「变频器运行中禁止写参数（请先停止）」",
              r is not None
              and r[1].get("err") == proto.VFD_PARAMS_ERR_RUN_LOCKED,
              "err=%r" % (r[1].get("err") if r else None,))
        check("B5c 拒写时**不发总线写**（闸在写之前）",
              counters["write"] == writes_before,
              "writes=%d" % counters["write"])
        check("B5d 拒写时寄存器没变",
              business.vfd.raw.get(ADDR_P101) == raw_before,
              "raw=%r" % (business.vfd.raw.get(ADDR_P101),))
        check("B5e 拒绝包络不带 params",
              r is not None and proto.KEY_VFD_PARAMS not in r[1],
              "%r" % (sorted(r[1].keys()) if r else None,))

        # ---- B6 总线坏了 ⇒ 失败包络（不编假值）----
        master = business.vfd._master_get()

        async def _boom(*a, **kw):
            raise modbus.ModbusError("总线炸了（测试注入）")

        master.read = _boom
        r = c.request(proto.CMD_VFD_PARAMS,
                      bson.encode({"op": proto.VFD_PARAMS_OP_READ}))
        check("B6 总线故障时读 ⇒ ok=false + 「变频器未就绪」",
              r is not None and r[1].get("ok") is False
              and r[1].get("err") == proto.VFD_PARAMS_ERR_NOT_READY,
              "%r" % (r,))
        check("B6b 失败包络不带 params（没有可信回读就不发数据）",
              r is not None and proto.KEY_VFD_PARAMS not in r[1],
              "%r" % (sorted(r[1].keys()) if r else None,))
        r = c.request(proto.CMD_VFD_PARAMS, bson.encode({
            "op": proto.VFD_PARAMS_OP_WRITE,
            "values": {str(ADDR_P101): 700}}))
        check("B6c 总线故障时写 ⇒ 同样「变频器未就绪」（不谎报成功）",
              r is not None and r[1].get("ok") is False
              and r[1].get("err") == proto.VFD_PARAMS_ERR_NOT_READY,
              "%r" % (r,))
    finally:
        c.close()


def _client_scenario(business, server, errors):
    try:
        _run_socket_checks(business, server)
    except Exception as exc:                            # noqa: BLE001
        import traceback
        errors.append("%s\n%s" % (exc, traceback.format_exc()))


async def _case_socket():
    print("=== B socket 级：真 XLink 帧打 0x011B ===")
    basedir = tempfile.mkdtemp(prefix="hsf_vfdp_sock_")
    errors = []
    try:
        business, server = await _spin_server(basedir, lambda m: None)
        thread = threading.Thread(target=_client_scenario,
                                  args=(business, server, errors),
                                  name="vfdparams-client")
        thread.start()
        try:
            await _feed_while(business, thread)
        finally:
            thread.join(timeout=60)
            await server.stop()
        for err in errors:
            print("  [ERROR] %s" % err)
            globals()["FAIL"] += 1
            FAILED.append("socket 级用例异常")
    finally:
        shutil.rmtree(basedir, ignore_errors=True)


# ============================================================================
#                                  入口
# ============================================================================

async def _run_case(name, fn):
    """跑一个用例：**异常也算红**（不许让异常打断整套回归）。

    [为什么] 真机约束一旦被回归破掉（设备层改成连读 ≥2 ⇒ 帧层直接抛
    ModbusException），异常会一路冒到 asyncio.run ⇒ 整套测试**崩**而不是
    **红** ⇒ 后面的用例一条都跑不到、退出码也只反映崩溃。⇒ 异常就地
    记成一条 FAIL，其余用例照跑。
    """
    try:
        await fn()
    except Exception as exc:                        # noqa: BLE001
        import traceback
        check("%s 用例异常（回归？）" % name, False,
              "%s: %s\n%s" % (type(exc).__name__, exc,
                              traceback.format_exc()))


async def _run():
    await _run_case("A1", _a1_read_count)
    await _run_case("A2", _a2_multi_read_rejected)
    await _run_case("A3", _a3_read_fail_closed)
    await _run_case("A4", _a4_write_no_answer_window)
    await _run_case("A5", _a5_readback_mismatch)
    await _run_case("A6", _a6_write_error_frame)
    await _run_case("A7", _a7_write_echo_ok)
    _a8_constants()
    await _case_socket()
    # [让出事件循环] 收尾再转一圈，保证没有悬空任务告警。
    await asyncio.sleep(0)


def main():
    asyncio.run(_run())
    print("=" * 62)
    print("  汇总：PASS %d / FAIL %d" % (PASS, FAIL))
    if FAILED:
        print("  失败项：%s" % ("、".join(FAILED),))
    return 0 if FAIL == 0 else 1


if __name__ == "__main__":
    sys.exit(main())
