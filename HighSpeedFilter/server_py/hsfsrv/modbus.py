# -*- coding: utf-8 -*-
"""Modbus 主站：RTU（串口）与 TCP（网口），纯标准库。

[C 侧对应] 本模块替代 Src/XCore/XProtocol/XModbus 的客户端+Hub 角色，但
刻意做得更小：Python 版只有 4 类从站、点位固定，不需要 Hub 的统一虚拟地址
空间与映射段管理——每个设备自己按自己的读组轮询即可。

[为什么不用 pymodbus]
    * 目标板 pip 不可用，只能 apt；Debian bullseye 的 python3-pymodbus 是
      2.1.0（catalog 极老，API 与 3.x 完全不同，同步模型也不合 asyncio）；
    * 现场三个设备的协议都有非标准怪癖（见下列注释），改库比写库更费。
    * 纯标准库 ⇒ 换板子/换系统零部署风险。

[各设备的协议怪癖 —— 每一条都是实测踩出来的，勿"优化"掉]
  1. M512 变频器错误应答是 8 字节且**功能码不置 0x80**（标准是 5 字节 +
     功能码|0x80）。通用库会把错误帧误判成正常读应答，必须按 8 字节识别。
  2. M512 单点地址（0x3000 / 0x8000）**数量必须 = 1**，≥2 直接回错误帧。
  3. 温湿度模块非法请求**静默丢弃**（永远等不到异常码）⇒ 只能靠超时判失败。
  4. 温湿度帧内字节间隔 >1.5ms 会整帧丢弃 ⇒ 必须一次 write 出完整 8 字节。
  5. 温湿度偶发「正常应答后多出 8 字节残尾」⇒ 发送前清接收缓冲 + CRC 双校验。
  6. 数字量模块（AMSAMOTION MT3A-IO1632）对 FC0F **会发两次相同应答**，
     且事务号恒 0 无法用 tid 区分 ⇒ 整组写用 FC05 逐点，不用 FC0F。
"""

import asyncio
import struct

#: 串口 485 收发方向切换的静默时间。RTU 规范要求 3.5 字符；
#: 9600bps 下 3.5 字符 ≈ 4ms，115200 下不足 0.5ms。统一取 2ms 保守值：
#: 反正是小帧低频（≥200ms 一拍），这点延迟不影响任何指标。
RTU_TURNAROUND_S = 0.002


# ============================================================================
#                              CRC16
# ============================================================================

def _build_crc_table():
    """预生成 CRC16/MODBUS（多项式 0xA001 反向）查表，避免逐位循环拖慢轮询。"""
    table = []
    for byte in range(256):
        crc = byte
        for _ in range(8):
            crc = (crc >> 1) ^ 0xA001 if crc & 1 else crc >> 1
        table.append(crc)
    return tuple(table)


_CRC_TABLE = _build_crc_table()


def crc16(data):
    """Modbus CRC16（低字节在前写入帧尾）。"""
    crc = 0xFFFF
    for byte in data:
        crc = (crc >> 8) ^ _CRC_TABLE[(crc ^ byte) & 0xFF]
    return crc


def append_crc(body):
    """在 PDU 尾部追加 CRC16（低字节在前）。"""
    return bytes(body) + struct.pack("<H", crc16(body))


def check_crc(frame):
    """校验整帧（含末尾 2 字节 CRC）。帧太短或不符返回 False。"""
    if len(frame) < 4:
        return False
    body, got = frame[:-2], struct.unpack("<H", frame[-2:])[0]
    return crc16(body) == got


# ============================================================================
#                          异常与结果
# ============================================================================

class ModbusError(Exception):
    """通讯失败基类。"""


class ModbusTimeout(ModbusError):
    """无应答（RTU 下绝大多数「非法请求」的表现就是静默超时）。"""


class ModbusException(ModbusError):
    """从站返回异常应答。

    :ivar code: 异常码（1=非法功能 2=非法地址 3=非法数据 4=从站故障 5=确认）
    """

    def __init__(self, code, function=0):
        self.code = code
        self.function = function
        super(ModbusException, self).__init__(
            "Modbus 异常应答 fc=0x%02X code=%d" % (function, code))


# ============================================================================
#                           Modbus TCP 主站
# ============================================================================

class TcpMaster(object):
    """Modbus TCP 主站（单连接长连，串行化请求）。

    [为什么串行化] Modbus TCP 允许流水线（靠事务号配对），但现场的
    AMSAMOTION 模块事务号恒 0、FC0F 还会重发应答 ⇒ 并发请求必然配错对。
    故统一「一问一答、加锁串行」，用可靠性换吞吐——本场景每设备 ≤10Hz，
    余量充足。

    [为什么复用一条连接] 模块对连接 churn 敏感：频繁连断会把它打进数分钟
    不应答状态（实测，见 docs 与项目记忆）。故长连 + 断线指数退避重连。
    """

    def __init__(self, host, port=502, unit=1, timeout=2.0, log=None):
        self.host = host
        self.port = port
        self.unit = unit
        self.timeout = timeout
        self._reader = None
        self._writer = None
        self._lock = asyncio.Lock()
        self._tid = 0
        self._log = log or (lambda msg: None)
        self._reconnect_backoff = 0.5

    # ---- 连接管理 ----
    async def _ensure_connected(self):
        if self._writer is not None and not self._writer.is_closing():
            return
        self._reader, self._writer = await asyncio.wait_for(
            asyncio.open_connection(self.host, self.port), timeout=self.timeout)
        # 小帧交互必须关 Nagle，否则每帧可能被攒 40ms 级。
        try:
            sock = self._writer.get_extra_info("socket")
            if sock is not None:
                import socket as _socket
                sock.setsockopt(_socket.IPPROTO_TCP, _socket.TCP_NODELAY, 1)
        except OSError:
            pass

    async def _drop(self):
        if self._writer is not None:
            try:
                self._writer.close()
            except (ConnectionError, OSError, RuntimeError):
                pass
        self._reader = None
        self._writer = None

    # ---- 事务 ----
    async def _read_upto(self, want):
        """读满 want 字节；不足则继续等到超时。

        [为什么不能用 readexactly 一次了事] 实测 MT3A 数字量模块会把一帧
        应答拆成多个 TCP 段发（qty=16 时先回 0202 再补 00；qty=32 时 4 字节
        数据更容易被切开）。而 `readexactly` 在**流未断**的情况下会一直等，
        但一旦对端分段后短暂静默，`asyncio.wait_for` 的外层超时就会把它
        取消 —— 取消会把**已缓冲的半帧丢掉**，整条流从此刻起永久错位。
        正确做法：只按"还差多少字节"反复读，EOF 才认失败。
        """
        buf = b""
        while len(buf) < want:
            chunk = await self._reader.read(want - len(buf))
            if not chunk:
                raise asyncio.IncompleteReadError(buf, want)
            buf += chunk
        return buf

    async def _transact(self, pdu):
        """发一条 PDU，返回应答 PDU（不含 MBAP）；异常映射为 ModbusError。

        [迟到帧防御] 上一拍读超时后，从站的应答可能迟到并留在流里。若不管它，
        下一拍会把迟到帧当成对本次请求的应答（**张冠李戴**：写 FC05 的点位
        却收到上一条 FC01 的数据）。故这里逐帧校验：
          * MBAP 长度字段合理（1..260，含 unit 字节）；
          * 若有事务号则与本次一致（该模块 tid 恒 0，此时退化为长度校验）；
        不符的帧**跳过**继续等，直到超时。
        """
        await self._ensure_connected()
        self._tid = (self._tid + 1) & 0xFFFF
        tid = self._tid
        # MBAP: tid(2) pid(2) len(2) unit(1)；len = unit + PDU
        frame = struct.pack(">HHHB", tid, 0, len(pdu) + 1, self.unit) + pdu
        self._writer.write(frame)
        await self._writer.drain()

        loop = asyncio.get_event_loop()
        deadline = loop.time() + self.timeout
        while True:
            remaining = deadline - loop.time()
            if remaining <= 0:
                raise asyncio.TimeoutError()
            # [不许在帧中间被取消] 帧头与帧体各自带超时，但**读的粒度**是
            # "还差多少字节"：分段到达时继续读，不会因为一次 read() 返回
            # 半截就丢数据。
            head = await asyncio.wait_for(self._read_upto(7),
                                          timeout=remaining)
            r_tid, _pid, length, _unit = struct.unpack(">HHHB", head)
            if not 1 <= length <= 260:
                # 帧头已不可信：流错位，无法再对齐 → 交由上层断连重建。
                raise ConnectionError(
                    "MBAP 长度非法（%d），字节流已错位" % length)
            body = await asyncio.wait_for(
                self._read_upto(length - 1),
                timeout=max(0.001, deadline - loop.time()))
            # tid 匹配：该模块恒发 0，故把它当"未提供事务号"放行；
            # 一旦对端给了非 0 tid，就必须与本次相同。
            if r_tid not in (0, tid):
                continue          # 迟到帧：丢弃后继续等本次应答
            return body

    async def read(self, function, address, count):
        """读从站（FC01/02/03/04）。

        :returns: 原始字节值列表（每个寄存器 2 字节，位读回为位图寄存器列表）
        """
        async with self._lock:
            try:
                pdu = struct.pack(">BHH", function, address, count)
                resp = await self._transact(pdu)
            except (asyncio.TimeoutError, asyncio.IncompleteReadError,
                    ConnectionError, OSError) as exc:
                await self._on_failure(exc)
                raise ModbusTimeout("读 %s fc=%d addr=0x%04X: %s"
                                    % (self.host, function, address,
                                       type(exc).__name__))
        return self._parse_read(resp, function, count)

    async def write_single(self, function, address, value):
        """写单个寄存器(FC06) / 单线圈(FC05)。"""
        async with self._lock:
            try:
                pdu = struct.pack(">BHH", function, address, value & 0xFFFF)
                resp = await self._transact(pdu)
            except (asyncio.TimeoutError, asyncio.IncompleteReadError,
                    ConnectionError, OSError) as exc:
                await self._on_failure(exc)
                raise ModbusTimeout("写 %s fc=%d addr=0x%04X: %s"
                                    % (self.host, function, address,
                                       type(exc).__name__))
        self._raise_if_exception(resp, function)
        if len(resp) < 5:
            raise ModbusError("写应答过短：%s" % resp.hex())
        # 正常应答是请求的原样回显（fc+addr+value）。
        return True

    async def _on_failure(self, exc):
        """事务失败后的连接处置：**区分「流脏了」与「设备偶尔不回」**。

        [为什么不能一律断连] AMSAMOTION 模块对连接 churn 极敏感：频繁连断会
        把它打进数分钟不应答状态（实测，见 docs 与项目记忆）。而该模块应答
        本来就慢（0.4~0.8s），偶发一次读超时属正常抖动。

        故只在**字节流无法再对齐**时才断连：
          * IncompleteReadError / 连接被对端关闭 / OSError —— 流已不可用；
          * 读超时但连接仍在 —— **保留连接**，下一拍重试即可。
        超时后残留的迟到字节由 _transact 开头的 MBAP 长度自校验兜住
        （长度/事务号不符即视为残帧丢弃）。
        """
        fatal = isinstance(exc, (asyncio.IncompleteReadError,
                                 ConnectionError, OSError))
        if fatal:
            await self._drop()

    @staticmethod
    def _parse_read(resp, function, count):
        if not resp:
            raise ModbusError("空应答")
        TcpMaster._raise_if_exception(resp, function)
        if len(resp) < 2:
            raise ModbusError("读应答过短：%s" % resp.hex())
        byte_count = resp[1]
        data = resp[2:2 + byte_count]
        if len(data) < byte_count:
            raise ModbusError("读应答数据不足：需要 %d 实际 %d"
                              % (byte_count, len(data)))
        if function in (1, 2):
            # 位读取：把位图摊成 0/1 列表，长度 = count。
            bits = []
            for i in range(count):
                byte_i, bit_i = divmod(i, 8)
                if byte_i < len(data):
                    bits.append((data[byte_i] >> bit_i) & 1)
                else:
                    bits.append(0)
            return bits
        return list(struct.unpack(">%dH" % (byte_count // 2), data))

    @staticmethod
    def _raise_if_exception(resp, function):
        """识别异常应答。

        标准 TCP：function 字节置了 0x80，下一字节是异常码。
        """
        if resp and (resp[0] & 0x80):
            code = resp[1] if len(resp) > 1 else 0
            raise ModbusException(code, resp[0] & 0x7F)

    async def close(self):
        await self._drop()


# ============================================================================
#                           Modbus RTU 主站
# ============================================================================

class RtuMaster(object):
    """Modbus RTU 主站（串口，经 pyserial 的 fd 走 asyncio 非阻塞包装）。

    [为什么用 fd 包装而不是 pyserial 的阻塞读] 阻塞 read 会卡死整个事件循环
    （单线程 asyncio）；而 pyserial 的 timeout 语义在「读到部分字节」时会
    按时长返回，容易把半帧当整帧。故取 fd 自己管读，超时由 asyncio 掌握。

    [M512 的 8 字节错误帧] 本设备的异常应答是
        [站号][原功能码][0x80][0x01][0x00][错误码][CRC16]   共 8 字节
    功能码**不置 0x80**，标准判定逻辑（resp[0] & 0x80）认不出来，会把它当成
    正常读应答（byte_count=0x80 → 数据不足）。故必须显式按此形态识别。
    """

    def __init__(self, port, baudrate, unit, timeout=0.4, retries=1, log=None,
                 min_tx_gap_ms=0):
        self.port = port
        self.baudrate = baudrate
        self.unit = unit
        self.timeout = timeout
        self.retries = max(0, retries)
        #: [站间最小间隙 2026-11-09] 同一 485 上相邻两次事务的**最小间隔**。
        #: ttyS4 三台混跑实测（docs/HSF板端ttyS4三设备取数实测总表 §9）：
        #: 站5 的 49 字节长帧读完后立刻问站1 → 几乎零应答；间隔 ≥100~125ms
        #: 才恢复，200ms 稳定 10/10。9600 路 B 传 200；115200 路 A 传 0
        #: （变频器 100ms 轮询不能被拖慢）。
        self.min_tx_gap_ms = max(0, int(min_tx_gap_ms))
        #: 上一次事务结束的时刻（loop.time()），配合 min_tx_gap_ms 用。
        self._last_tx_end = 0.0
        self._serial = None
        self._lock = asyncio.Lock()
        self._log = log or (lambda msg: None)
        #: 打开失败过的时刻（0=没试过）。用于退避，避免每拍都重试打开。
        self._open_failed_ms = 0
        #: 打开失败的最短重试间隔（毫秒）。串口被占时别每秒戳十次。
        self.OPEN_RETRY_MS = 5000

    # ---- 打开 / 关闭 ----
    def open(self):
        """打开串口。失败抛 ModbusError（调用方跳过该路，不阻断其它设备）。

        [参数] 8N1 + 无流控，与现场三个从站的实测配置一致；
        timeout=0 表示非阻塞读，靠 asyncio 控超时。
        """
        try:
            import serial
        except ImportError:
            raise ModbusError("未安装 pyserial（apt-get install python3-serial）")
        try:
            self._serial = serial.Serial(
                port=self.port,
                baudrate=self.baudrate,
                bytesize=serial.EIGHTBITS,
                parity=serial.PARITY_NONE,
                stopbits=serial.STOPBITS_ONE,
                timeout=0,              # 非阻塞：由 asyncio 控超时
                write_timeout=1.0,
                exclusive=True,         # Linux 下独占，防别的进程抢口
            )
        except Exception as exc:                        # noqa: BLE001
            raise ModbusError("串口 %s 打开失败：%s" % (self.port, exc))
        self._open_failed_ms = 0
        return True

    def ensure_open(self):
        """确保串口已打开（首次使用时懒打开 + 失败退避）。

        [为什么懒开 + 退避] 串口可能被板上 demo 占着（slave485.mode）。
        装配期就开会让服务端启动直接失败；每拍都重试打开又会刷爆日志
        并反复惊扰占口者。故：首次用到时开，失败后 5 秒内不再重试。
        """
        if self.is_open():
            return True
        now = int(asyncio.get_event_loop().time() * 1000)
        if (self._open_failed_ms
                and now - self._open_failed_ms < self.OPEN_RETRY_MS):
            raise ModbusError(
                "串口 %s 不可用（%.0fs 后重试）：上次打开失败"
                % (self.port,
                   (self.OPEN_RETRY_MS - (now - self._open_failed_ms)) / 1000.0))
        try:
            self.open()
        except ModbusError:
            self._open_failed_ms = now
            raise
        return True

    def close(self):
        if self._serial is not None:
            try:
                self._serial.close()
            except Exception:                           # noqa: BLE001
                pass
            self._serial = None

    def is_open(self):
        return self._serial is not None and self._serial.is_open

    def _flush_input(self):
        """清接收缓冲。

        [为什么每次发前都清] 温湿度模块实测有 0.55% 概率在正常应答后多吐
        8 字节残尾；不清的话这些字节会串到下一条事务的前面，导致 CRC 失败。
        """
        try:
            self._serial.reset_input_buffer()
        except Exception:                               # noqa: BLE001
            pass

    # ---- 底层收发 ----
    #: 帧间静默判定（毫秒）：收到字节后若连续这么久无新字节，认为一帧结束。
    #: RTU 规范是 3.5 字符；115200 下约 0.3ms，取 3ms 留足余量（半双工
    #: 收发切换 + USB 转串口批量上报都有抖动）。它只影响"何时停止收"，
    #: 不影响正确性——多收的字节由 CRC/长度校验自然丢弃。
    RTU_IDLE_MS = 3

    async def _read_until_idle(self, deadline):
        """读到总线静默（或总超时）为止，返回收到的全部字节。

        [为什么不是"按字段精确读 n 字节"] 分次读会在中间空档把帧拆断，
        实测造成半帧错位（0x1000 解成 4619 而非 3000）。RTU 帧短且以
        静默分隔，一次性收完再解析最稳。
        """
        buf = bytearray()
        loop = asyncio.get_event_loop()
        last_byte_at = 0.0
        while True:
            now = loop.time()
            if now >= deadline:
                break
            # 已收到字节且静默够久 => 一帧结束
            if buf and (now - last_byte_at) * 1000.0 >= self.RTU_IDLE_MS:
                break
            try:
                chunk = self._serial.read(256)
            except Exception as exc:                    # noqa: BLE001
                raise ModbusError("串口读失败：%s" % exc)
            if chunk:
                buf += chunk
                last_byte_at = loop.time()
                continue
            # 还没收到任何字节：等头一个字节；已收到：等静默判定。
            await asyncio.sleep(0.001)
        return bytes(buf)

    #: 补齐读的上限（毫秒）：内核第二批交付的批间隔实测 ~20.6ms，取 3 倍余量。
    #: 只在「长度不足」时才等，正常短帧零开销。
    RTU_TOPUP_MS = 60

    async def _top_up_short_read(self, raw, deadline):
        """[按长度收满] 对「标准读应答但长度不足」的帧补一次收齐。

        只认读功能码（1/2/3/4）的 `[站号][fc][byteCount][data][CRC2]` 形态：
        异常应答（fc|0x80，固定 5 字节）与写应答（固定 8 字节回显）长度本就
        确定，不需要补。M512 的 byteCount=0x80 错误帧形态特殊，也不补。
        """
        if len(raw) < 3:
            return raw
        fc = raw[1]
        if fc & 0x80:
            return raw                      # 异常应答固定 5 字节
        if fc not in (1, 2, 3, 4):
            return raw                      # 写应答等固定长度，按原口径
        byte_count = raw[2]
        if byte_count == 0x80:
            return raw                      # M512 错误帧（长度另行判定）
        need = byte_count + 5
        if len(raw) >= need:
            return raw                      # 已收满（正常路径，零开销）
        loop = asyncio.get_event_loop()
        top_deadline = loop.time() + self.RTU_TOPUP_MS / 1000.0
        buf = bytearray(raw)
        while loop.time() < top_deadline and len(buf) < need:
            try:
                chunk = self._serial.read(256)
            except Exception as exc:        # noqa: BLE001
                raise ModbusError("串口读失败：%s" % exc)
            if chunk:
                buf += chunk
                continue
            await asyncio.sleep(0.001)
        return bytes(buf)

    async def _transact(self, pdu, unit=None):
        """发一条 RTU PDU 并收应答 PDU（不含 CRC）。

        :param unit: 目标从站号；None 用实例缺省（self.unit）。
                     同一 485 上挂多个从站时由调用方逐次指定。
        """
        if unit is None:
            unit = self.unit
        self.ensure_open()
        # [站间最小间隙] 上一帧收完后若还没到 min_tx_gap_ms，先等到期再发。
        # 站5 长帧后立刻问站1 会得到零应答（接收器恢复期），200ms 稳定。
        if self.min_tx_gap_ms > 0 and self._last_tx_end > 0:
            loop = asyncio.get_event_loop()
            earliest = self._last_tx_end + self.min_tx_gap_ms / 1000.0
            delay = earliest - loop.time()
            if delay > 0:
                await asyncio.sleep(delay)
        request = append_crc(bytes([unit]) + pdu)
        self._flush_input()
        try:
            # 一次 write 出完整帧：温湿度模块按 1.5ms 字符间隔判帧尾，
            # 逐字节发会整帧被丢弃（实测 3ms 间隔 → 0/15 应答）。
            self._serial.write(request)
            self._serial.flush()
        except Exception as exc:                        # noqa: BLE001
            raise ModbusError("串口写失败：%s" % exc)

        loop = asyncio.get_event_loop()
        deadline = loop.time() + self.timeout

        # [为什么要"尽量一次收完再解析" —— 实锅]
        # 早先按「读2字节→判长度→再读N字节」分三段读，在 Windows 上实测
        # 出现半帧错位：0x1000 应为 3000(0x0BB8)，实际解出 4619(0x120B)
        # —— 0x0B 之后把下一寄存器的 0x12 当成了自己的低字节。
        # 根因是分次 read 时中间出现可达 1ms 的空档（非阻塞 read 返回空后
        # sleep），把一帧拆断。RTU 帧本身很短（≤256 字节），直接**读到
        # 静默或超时为止**再整体解析，比"按字段精确读"稳得多。
        raw = await self._read_until_idle(deadline)
        # [按长度收满 2026-11-09] 静默判帧尾在「内核分批交付」的长应答上恒定
        # 截断（ttyS4 9600 实测：49 字节应答被拆成 32+17 两批，批间隔
        # ~20.6ms，3ms 静默口径下 20/20 全断在 32 字节——证据与复现见
        # docs/HSF板端ttyS4三设备取数实测总表_20261109.md §3.2）。这里对
        # 「读应答但长度不足」的帧做一次补齐读（最多 RTU_TOPUP_MS），把
        # 第二批收进来；多收的字节由下面的 CRC/长度校验自然丢弃。
        raw = await self._top_up_short_read(raw, deadline)
        # [站间最小间隙] 收发完成时刻（后续只是解析，不再上总线）。
        self._last_tx_end = asyncio.get_event_loop().time()
        if not raw:
            raise ModbusTimeout("无应答（0 字节）")
        if len(raw) < 2:
            raise ModbusTimeout("应答不完整（%d 字节）" % len(raw))
        if raw[0] != unit:
            raise ModbusError("站号不符：期望 %d 实际 %d" % (unit, raw[0]))
        function = raw[1]

        # ---- 标准异常应答：[站号][fc|0x80][code][CRC2] ----
        if function & 0x80:
            if len(raw) < 5 or not check_crc(raw[:5]):
                raise ModbusError("异常帧 CRC 错：%s" % raw.hex())
            raise ModbusException(raw[2], function & 0x7F)

        if function in (1, 2, 3, 4):
            # 标准读应答：[站号][fc][byteCount][data...][CRC2]
            if len(raw) < 3:
                raise ModbusTimeout("读应答缺 byteCount：%s" % raw.hex())
            byte_count = raw[2]
            # ---- M512 怪癖：错误帧的"byteCount"位是 0x80 ----
            # 形态：[站号][fc][80][01][00][code][CRC2] 共 8 字节，
            # 功能码**不置 0x80**，标准判定认不出来。
            if byte_count == 0x80:
                if len(raw) < 8:
                    raise ModbusTimeout("M512 错误帧不完整：%s" % raw.hex())
                raise ModbusException(raw[5], function)
            # 帧总长 = 站号(1) + 功能码(1) + 字节数(1) + 数据(byte_count) + CRC(2)
            # [实锅] 曾写成 byte_count+4，少算 1 字节 -> 截掉末字节、CRC 必错
            need = byte_count + 5
            if len(raw) < need:
                raise ModbusTimeout("读应答不完整：需要 %d 实际 %d（%s）"
                                    % (need, len(raw), raw.hex()))
            frame = raw[:need]
            if not check_crc(frame):
                raise ModbusError("CRC 错：%s" % frame.hex())
            # 数据区起点是 3（站号+功能码+字节数 各 1 字节）。
            # [实锅] 曾写成 frame[2:2+byte_count]，把 byte_count 自身也算进
            # 数据里 -> 整体错位一个字节（真机现象：0x1000 解成 4619 而非 3000，
            # 所有工程值全错）。
            return frame[3:3 + byte_count]

        if function in (5, 6):
            # 正常应答是请求的原样回显，共 8 字节
            if len(raw) >= 8:
                frame = raw[:8]
                if check_crc(frame):
                    return frame[2:6]
                # M512 错误帧：[站号][fc][80][01][00][code][CRC2]
                if frame[2] == 0x80 and frame[3] == 0x01:
                    raise ModbusException(frame[5], function)
            raise ModbusError("写应答畸形：%s" % raw.hex())

        raise ModbusError("不支持的功能码应答 0x%02X（%s）"
                          % (function, raw.hex()))

    async def _with_retry(self, pdu, unit=None):
        """按配置重试（重试能把滞留的错误帧冲掉——M512 实测必要）。"""
        last = None
        for attempt in range(self.retries + 1):
            try:
                return await self._transact(pdu, unit)
            except ModbusException:
                raise                    # 从站明确拒绝：重试无意义
            except ModbusError as exc:
                last = exc
                if attempt < self.retries:
                    await asyncio.sleep(RTU_TURNAROUND_S * 4)
        raise last

    async def read(self, function, address, count, unit=None):
        """读从站（FC01/02/03/04），返回寄存器值列表 / 位列表。

        :param unit: 从站号（None=实例缺省）。同总线多从站共享本 master 时
                     由设备层逐次传入。
        """
        async with self._lock:
            pdu = struct.pack(">BHH", function, address, count)
            data = await self._with_retry(pdu, unit)
        if function in (1, 2):
            bits = []
            for i in range(count):
                byte_i, bit_i = divmod(i, 8)
                bits.append((data[byte_i] >> bit_i) & 1 if byte_i < len(data) else 0)
            return bits
        if len(data) < count * 2:
            raise ModbusError("读回寄存器不足：需要 %d 实际 %d"
                              % (count, len(data) // 2))
        return list(struct.unpack(">%dH" % count, data[:count * 2]))

    async def write_single(self, function, address, value, unit=None):
        """写单个寄存器(FC06) / 单线圈(FC05)。"""
        async with self._lock:
            await self._with_retry(struct.pack(">BHH", function, address,
                                               value & 0xFFFF), unit)
        return True
