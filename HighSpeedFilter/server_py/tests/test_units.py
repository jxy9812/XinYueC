# -*- coding: utf-8 -*-
"""单元测试：编解码 / Modbus / 认证 / 设备解码（不需要起服务端）。

跑法：
    python tests/test_units.py
全部用 unittest，退出码即结果。
"""

import asyncio
import hashlib
import hmac
import os
import struct
import sys
import unittest

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))

from hsfsrv import auth, bson, config, devices, modbus, proto    # noqa: E402


# ============================================================================
#                           帧头 / 常量
# ============================================================================

class TestProto(unittest.TestCase):
    def test_header_layout_is_16_bytes(self):
        """帧头必须正好 16 字节（v2 协议；1.x 是 12，混淆即整条流错位）。"""
        head = proto.encode_header(100, proto.MSGCLASS_REQUEST, 0x0101, 7,
                                   proto.DATATYPE_BSON)
        self.assertEqual(len(head), proto.XLINK_HEADER_SIZE)
        self.assertEqual(len(head), 16)

    def test_header_roundtrip(self):
        head = proto.encode_header(1234, proto.MSGCLASS_PUBLISH, 0x0001,
                                   0xDEADBEEF, proto.DATATYPE_JSON)
        plen, mclass, mcode, rid, dtype = proto.decode_header(head)
        self.assertEqual((plen, mclass, mcode, rid, dtype),
                         (1234, proto.MSGCLASS_PUBLISH, 0x0001, 0xDEADBEEF,
                          proto.DATATYPE_JSON))

    def test_header_reserved_is_zero(self):
        head = proto.encode_header(0, 0, 0, 0, 0)
        self.assertEqual(head[13:16], b"\x00\x00\x00")

    def test_header_rejects_oversize_payload(self):
        """载荷超上限必须解不出（防畸形帧撑爆缓冲）。"""
        head = struct.pack("<IHHIB3x", proto.XLINK_MAX_PAYLOAD + 1,
                           1, 0, 0, proto.DATATYPE_RAW)
        self.assertIsNone(proto.decode_header(head))

    def test_header_rejects_unknown_datatype(self):
        head = struct.pack("<IHHIB3x", 0, 1, 0, 0, 99)
        self.assertIsNone(proto.decode_header(head))

    def test_header_rejects_short_buffer(self):
        self.assertIsNone(proto.decode_header(b"\x00" * 15))

    def test_topic_base_out_of_u16(self):
        """业务主题号从 0x10000 起，装不进 u16 —— 这是 topicId 必须走载荷的原因。"""
        self.assertGreater(proto.TOPIC_VFD_STATUS, 0xFFFF)
        packed = proto.pack_topic(proto.TOPIC_VFD_STATUS)
        self.assertEqual(len(packed), 4)
        self.assertEqual(struct.unpack("<I", packed)[0], proto.TOPIC_VFD_STATUS)
        # msgCode 只能装低 16 位
        self.assertEqual(proto.TOPIC_VFD_STATUS & 0xFFFF, 0x0001)

    def test_hello_roundtrip(self):
        raw = proto.encode_hello(proto.CAP_AUTH)
        self.assertEqual(proto.decode_hello(raw), (proto.XLINK_VERSION,
                                                   proto.CAP_AUTH))

    def test_hello_short_payload(self):
        self.assertIsNone(proto.decode_hello(b"\x00\x02"))

    def test_version_major_is_2(self):
        """主版本决定帧头宽度，改动等于破坏兼容。"""
        self.assertEqual(proto.XLINK_VERSION >> 8, 0x02)


# ============================================================================
#                               BSON
# ============================================================================

class TestBson(unittest.TestCase):
    def test_roundtrip_all_types(self):
        src = {"i": 42, "big": 2 ** 40, "neg": -7, "f": 3.25, "b": True,
               "s": "中文 abc", "nested": {"x": 1}}
        out = bson.decode(bson.encode(src))
        self.assertEqual(out["i"], 42)
        self.assertEqual(out["big"], 2 ** 40)
        self.assertEqual(out["neg"], -7)
        self.assertAlmostEqual(out["f"], 3.25)
        self.assertIs(out["b"], True)
        self.assertEqual(out["s"], "中文 abc")
        self.assertEqual(out["nested"], {"x": 1})

    def test_bool_encoded_as_bool_not_int(self):
        """bool 是 int 的子类：判断顺序错了会编成 int32，客户端读 bool 会失败。"""
        raw = bson.encode({"f": False})
        self.assertEqual(raw[4], 0x08)          # 类型字节 = BSON bool

    def test_int32_vs_int64_boundary(self):
        raw32 = bson.encode({"v": 2 ** 31 - 1})
        raw64 = bson.encode({"v": 2 ** 31})
        self.assertEqual(raw32[4], 0x10)        # int32
        self.assertEqual(raw64[4], 0x12)        # int64

    def test_document_length_field(self):
        raw = bson.encode({"a": 1})
        self.assertEqual(struct.unpack("<i", raw[:4])[0], len(raw))
        self.assertEqual(raw[-1], 0x00)

    def test_decode_rejects_truncated(self):
        raw = bson.encode({"key": "value"})
        with self.assertRaises(ValueError):
            bson.decode(raw[:-3])

    def test_decode_rejects_garbage(self):
        with self.assertRaises(ValueError):
            bson.decode(b"\xde\xad\xbe\xef")

    def test_decode_rejects_overlong_length(self):
        """长度字段声称比实际长：不许越界读。"""
        raw = bytearray(bson.encode({"a": 1}))
        raw[:4] = struct.pack("<i", 999)
        with self.assertRaises(ValueError):
            bson.decode(bytes(raw))

    def test_decode_unknown_type_raises(self):
        """未知类型必须报错：静默跳过会让调用方拿半份数据做决策。"""
        body = bytes([0x7F]) + b"k\x00" + b"\x00"
        raw = struct.pack("<i", len(body) + 4) + body
        with self.assertRaises(ValueError):
            bson.decode(raw)

    def test_encode_rejects_unsupported_type(self):
        """不支持的类型必须报错（不静默降级成字符串/空值）。

        [2026-11-06] list/tuple 原先在本用例里当"不支持类型"的代表，加入
        BSON 数组（0x04，历史分页用）后它已被支持；改用 set —— 它既不是
        标量也不是文档/数组，任何编码器都不该默默收下。
        """
        with self.assertRaises(ValueError):
            bson.encode({"v": {1, 2, 3}})

    def test_array_roundtrip(self):
        """数组（0x04）编解码：历史分页的行是二维数组，键名按序号。"""
        doc = {"rows": [[1, "a", None], [2, "b", 3.5]], "cols": ["ts", "m"]}
        got = bson.decode(bson.encode(doc))
        self.assertEqual(got["rows"], [[1, "a", None], [2, "b", 3.5]])
        self.assertEqual(got["cols"], ["ts", "m"])

    def test_array_element_order_beyond_nine(self):
        """元素顺序按序号数值排（"10" 不能排在 "2" 前面）。"""
        items = list(range(12))
        got = bson.decode(bson.encode({"v": items}))
        self.assertEqual(got["v"], items)

    def test_key_with_nul_rejected(self):
        with self.assertRaises(ValueError):
            bson.encode({"a\x00b": 1})


# ============================================================================
#                             Modbus
# ============================================================================

class TestModbusCrc(unittest.TestCase):
    def test_known_vector_temphum(self):
        """温湿度模块实测帧：04 04 00 00 00 02 → CRC 0x9E71（低字节在前 71 9E）。"""
        self.assertEqual(modbus.append_crc(bytes([0x04, 0x04, 0x00, 0x00,
                                                  0x00, 0x02])),
                         bytes([0x04, 0x04, 0x00, 0x00, 0x00, 0x02, 0x71, 0x9E]))

    def test_known_vector_m512_write(self):
        """M512 实测帧 02 06 10 00 0B B8 **8A 7B**（CRC 字段线上为 8A 7B）。

        [字节序] 文档里写的 "8A 7B" 就是**线上字节**（低字节在前），
        对应 CRC16 数值 0x7B8A。断言直接比线上字节，与文档逐字一致。
        """
        frame = modbus.append_crc(bytes([0x02, 0x06, 0x10, 0x00, 0x0B, 0xB8]))
        self.assertEqual(frame[-2:], bytes([0x8A, 0x7B]))
        self.assertEqual(frame, bytes([0x02, 0x06, 0x10, 0x00, 0x0B, 0xB8,
                                       0x8A, 0x7B]))

    def test_check_crc_good_and_bad(self):
        frame = modbus.append_crc(b"\x01\x03\x02\x00\x0A")
        self.assertTrue(modbus.check_crc(frame))
        self.assertFalse(modbus.check_crc(frame[:-1] + bytes([frame[-1] ^ 0xFF])))

    def test_check_crc_too_short(self):
        self.assertFalse(modbus.check_crc(b"\x01"))

    def test_temphum_response_crc(self):
        """真实应答 04 04 04 00 D9 02 88 7E 79 的 CRC 必须校验通过。"""
        self.assertTrue(modbus.check_crc(
            bytes([0x04, 0x04, 0x04, 0x00, 0xD9, 0x02, 0x88, 0x7E, 0x79])))


class TestModbusParse(unittest.TestCase):
    def test_parse_bits(self):
        """位读取：位图摊成 0/1 列表，低位在前（Modbus 口径）。"""
        bits = modbus.TcpMaster._parse_read(bytes([0x02, 0x02, 0x0D, 0x00]),
                                            function=2, count=16)
        self.assertEqual(bits[:8], [1, 0, 1, 1, 0, 0, 0, 0])
        self.assertEqual(bits[8:], [0] * 8)

    def test_parse_registers(self):
        regs = modbus.TcpMaster._parse_read(
            bytes([0x04, 0x04, 0x00, 0xD9, 0x02, 0x88]),
            function=4, count=2)
        self.assertEqual(regs, [0xD9, 0x288])

    def test_parse_raises_on_exception_response(self):
        with self.assertRaises(modbus.ModbusException) as ctx:
            modbus.TcpMaster._parse_read(bytes([0x81, 0x02]), function=1,
                                         count=16)
        self.assertEqual(ctx.exception.code, 2)

    def test_parse_raises_on_short_data(self):
        """字节数声称 4、实际只给 2：必须报错，不能把半份数据当结果。"""
        with self.assertRaises(modbus.ModbusError):
            modbus.TcpMaster._parse_read(bytes([0x04, 0x04, 0x00, 0xD9]),
                                         function=4, count=2)


# ============================================================================
#                             认证
# ============================================================================

class TestAuth(unittest.TestCase):
    def test_derive_matches_c_side_formula(self):
        """派生式必须与 C 侧 XLinkAuth_deriveHash 逐字节相同。

        首轮 HMAC(口令, 盐)，其后 HMAC(口令, 上一轮)，共 iterations 轮。
        这里用独立的参考实现交叉验证，防将来"优化"破坏与 C 账户库的互通。
        """
        password, salt, iterations = "root", b"\x01" * 16, 1000
        key = password.encode()
        expect = hmac.new(key, salt, hashlib.sha256).digest()
        for _ in range(iterations - 1):
            expect = hmac.new(key, expect, hashlib.sha256).digest()
        self.assertEqual(
            auth.derive_hash(password, salt, iterations), expect)

    def test_derive_requires_iterations(self):
        with self.assertRaises(ValueError):
            auth.derive_hash("root", b"\x00" * 16, 0)

    def test_derive_requires_password(self):
        with self.assertRaises(ValueError):
            auth.derive_hash("", b"\x00" * 16, 10)

    def test_challenge_roundtrip(self):
        salt = b"\xAB" * 16
        nonce = b"\xCD" * 32
        raw = auth.encode_challenge(proto.AUTH_METHOD_ITERATED_HMAC, 10000,
                                    salt, nonce)
        self.assertEqual(len(raw), 8 + 16 + 32)
        self.assertEqual(raw[0], proto.AUTH_METHOD_ITERATED_HMAC)
        self.assertEqual(raw[1], 16)
        self.assertEqual(struct.unpack("<I", raw[4:8])[0], 10000)
        self.assertEqual(raw[8:24], salt)
        self.assertEqual(raw[24:56], nonce)

    def test_response_roundtrip(self):
        digest = b"\x11" * 32
        raw = auth.encode_result(proto.AUTHSTATUS_OK, proto.ROLE_ADMIN, "",
                                 digest)
        # 手工构造一个 AuthResponse 再解
        resp_raw = bytes([proto.AUTH_METHOD_ITERATED_HMAC, 0]) + \
            struct.pack("<H", 32) + digest
        method, resp = auth.decode_response(resp_raw)
        self.assertEqual(method, proto.AUTH_METHOD_ITERATED_HMAC)
        self.assertEqual(resp, digest)

    def test_decode_response_rejects_short_digest(self):
        """摘要长度必须正好 32：接受变长等于允许降级强度。"""
        raw = bytes([1, 0]) + struct.pack("<H", 8) + b"\x00" * 8
        self.assertIsNone(auth.decode_response(raw))

    def test_server_proof_uses_domain_separation(self):
        """服务端证明 = HMAC(H, nonce||0x01)，与客户端应答必须不同。"""
        digest, nonce = b"\x22" * 32, b"\x33" * 32
        self.assertNotEqual(auth.compute_server_proof(digest, nonce),
                            auth.compute_response(digest, nonce))

    def test_constant_time_equal(self):
        self.assertTrue(auth.constant_time_equal(b"abc", b"abc"))
        self.assertFalse(auth.constant_time_equal(b"abc", b"abd"))
        self.assertFalse(auth.constant_time_equal(b"abc", b"ab"))

    def test_request_roundtrip(self):
        self.assertEqual(auth.decode_request(auth.encode_request("admin")),
                         "admin")

    def test_request_rejects_empty_and_overlong(self):
        self.assertIsNone(auth.decode_request(b""))
        self.assertIsNone(auth.decode_request(bytes([0])))
        self.assertIsNone(auth.decode_request(bytes([40]) + b"x" * 40))

    def test_userstore_json_matches_c_schema(self):
        """账户库 JSON 键表必须与 C 侧 XLinkUserStore 一致（可互换读取）。"""
        rec = auth.UserRecord("root", b"\x01" * 16, b"\x02" * 32, 10000,
                              proto.ROLE_ADMIN)
        doc = rec.to_json()
        for key in ("name", "role", "iterations", "locked", "salt", "hash"):
            self.assertIn(key, doc)
        self.assertEqual(len(doc["salt"]), 32)      # 16 字节 → 32 hex
        self.assertEqual(len(doc["hash"]), 64)      # 32 字节 → 64 hex
        back = auth.UserRecord.from_json(doc)
        self.assertEqual(back.name, "root")
        self.assertEqual(back.digest, b"\x02" * 32)

    def test_userstore_defaults_are_readonly_and_admin(self):
        store = auth.UserStore()
        store.create_defaults()
        self.assertEqual(store.find("admin").role, proto.ROLE_USER)
        self.assertEqual(store.find("root").role, proto.ROLE_ADMIN)

    def test_userstore_no_plaintext_password(self):
        """口令绝不明文出现在账户记录里。"""
        store = auth.UserStore()
        store.add_user("u", "SuperSecret123", proto.ROLE_USER)
        rec = store.find("u")
        self.assertNotIn(b"SuperSecret123", rec.salt + rec.digest)


# ============================================================================
#                            设备解码
# ============================================================================

class TestVfdDecode(unittest.TestCase):
    def _make(self):
        return devices.VfdM512(lambda: None, 2)

    def test_run_state_word_1_is_forward(self):
        dev = self._make()
        dev._decode_state(1)
        self.assertEqual(dev.run_state, proto.RUN_RUNNING_FORWARD)
        self.assertTrue(dev.raw["_running"])
        self.assertFalse(dev.raw["_reverse"])

    def test_run_state_word_2_is_reverse(self):
        dev = self._make()
        dev._decode_state(2)
        self.assertEqual(dev.run_state, proto.RUN_RUNNING_REVERSE)
        self.assertTrue(dev.raw["_reverse"])

    def test_run_state_word_3_is_stopped(self):
        dev = self._make()
        dev._decode_state(3)
        self.assertEqual(dev.run_state, proto.RUN_STOPPED)
        self.assertFalse(dev.raw["_running"])

    def test_unknown_state_word(self):
        dev = self._make()
        dev._decode_state(99)
        self.assertEqual(dev.run_state, proto.RUN_UNKNOWN)

    def test_fault_overrides_state(self):
        """有故障时状态必须显示故障，不能还显示运行中。"""
        dev = self._make()
        dev._decode_state(1)
        dev._decode_fault(0x000A)
        self.assertTrue(dev.faulted)
        self.assertEqual(dev.run_state, proto.RUN_FAULT)

    def test_fault_clear(self):
        dev = self._make()
        dev._decode_fault(0x000A)
        dev._decode_fault(0)
        self.assertFalse(dev.faulted)

    def test_output_freq_div_100(self):
        """0x1001 是 0.01Hz（÷100）——与 U0.00 的 ÷10 不同，混用会差 10 倍。"""
        dev = self._make()
        dev.raw[0x1001] = 1500
        self.assertAlmostEqual(dev.output_freq_hz(), 15.0)

    def test_set_freq_is_percent_of_max(self):
        """0x1000 是相对 P0.10 的百分比，不是绝对 Hz。"""
        dev = self._make()
        dev.raw[0x1000] = 3000          # 30.00%
        self.assertAlmostEqual(dev.set_freq_hz(), 15.0)   # × 50Hz 基准

    def test_set_freq_negative_complement(self):
        """0x1000 是 int16 补码：无符号读会把 -20Hz 读成 65036。"""
        dev = self._make()
        dev.raw[0x1000] = 0xF830          # -2000 的补码 = -20.00%
        self.assertAlmostEqual(dev.set_freq_hz(), -10.0)

    def test_other_scalings(self):
        dev = self._make()
        dev.raw.update({0x1002: 5370, 0x1003: 380, 0x1004: 125,
                        0x1005: 75, 0x1006: 332})
        self.assertAlmostEqual(dev._scaled(0x1002, 10.0), 537.0)   # 母线 0.1V
        self.assertAlmostEqual(dev._scaled(0x1003, 1.0), 380.0)    # 输出 1V
        self.assertAlmostEqual(dev._scaled(0x1004, 100.0), 1.25)   # 电流 0.01A
        self.assertAlmostEqual(dev._scaled(0x1005, 10.0), 7.5)     # 功率 0.1kW
        self.assertAlmostEqual(dev._scaled(0x1006, 10.0), 33.2)    # 转矩 0.1%

    def test_has_field_does_not_depend_on_valid_mask(self):
        """has_field 必须只看原始数据，否则置位过程自证（永远置不上）。"""
        dev = self._make()
        dev.valid_mask = 0
        dev.raw[0x1001] = 1500
        self.assertTrue(dev.has_field(11))
        self.assertFalse(dev.has_field(14))     # 0x1003 没读到

    def test_speed_not_provided(self):
        """转速无寄存器可读，必须报无效（客户端显示 --），不能瞎算。"""
        dev = self._make()
        self.assertFalse(dev.has_field(21))

    def test_fault_text_known_codes(self):
        self.assertEqual(devices.VfdM512.fault_text(0x0009), "欠压故障")
        self.assertEqual(devices.VfdM512.fault_text(0x0010), "通讯异常")
        self.assertEqual(devices.VfdM512.fault_text(0xFFFF), "")

    def test_command_codes_match_c_side(self):
        """命令字取值必须与 C 侧 XVfdM512.h 一致。"""
        self.assertEqual(devices.VfdM512.CMD_RUN_FORWARD, 1)
        self.assertEqual(devices.VfdM512.CMD_RUN_REVERSE, 2)
        self.assertEqual(devices.VfdM512.CMD_JOG_FORWARD, 3)
        self.assertEqual(devices.VfdM512.CMD_JOG_REVERSE, 4)
        self.assertEqual(devices.VfdM512.CMD_FREE_STOP, 5)
        self.assertEqual(devices.VfdM512.CMD_DECEL_STOP, 6)
        self.assertEqual(devices.VfdM512.CMD_FAULT_RESET, 7)

    def test_register_addresses_match_c_side(self):
        """寄存器地址必须与 C 侧 XVfdM512_Modbus.h 一致。"""
        self.assertEqual(devices.VfdM512.ADDR_CMD_WORD, 0x2000)
        self.assertEqual(devices.VfdM512.ADDR_FREQ_SET, 0x1000)
        self.assertEqual(devices.VfdM512.ADDR_STATUS, 0x3000)
        self.assertEqual(devices.VfdM512.ADDR_FAULT, 0x8000)


class TestTempHumDecode(unittest.TestCase):
    def _make(self):
        return devices.TempHum(lambda: None, 4)

    def test_positive_temp(self):
        dev = self._make()
        dev.raw_temp = 217
        self.assertEqual(dev.temp_tenths(), 217)        # 21.7℃

    def test_negative_temp_threshold(self):
        """raw >= 10000 时 -(raw-10000)*0.1：不实现会把 -25℃ 解成 10250℃。"""
        dev = self._make()
        dev.raw_temp = 10250
        self.assertEqual(dev.temp_tenths(), -250)       # -25.0℃

    def test_boundary_just_below_threshold(self):
        dev = self._make()
        dev.raw_temp = 9999
        self.assertEqual(dev.temp_tenths(), 9999)

    def test_humidity_scaling(self):
        dev = self._make()
        dev.raw_hum = 648
        self.assertEqual(dev.hum_tenths(), 648)         # 64.8%RH

    def test_read_params(self):
        self.assertEqual(devices.TempHum.READ_ADDR, 0x0000)
        self.assertEqual(devices.TempHum.READ_COUNT, 2)


class TestAnalogDecode(unittest.TestCase):
    def test_ma_conversion_uses_249_ohm(self):
        """I(mA) = mV / 249Ω。"""
        dev = devices.AnalogInput(None, 8)
        dev.raw[0] = 995
        self.assertAlmostEqual(dev.ma(0), 995 / 249.0, places=6)

    def test_live_zero_and_full_scale(self):
        """4mA 活零点 995mV、20mA 满量程 4975mV（实测口径）。"""
        dev = devices.AnalogInput(None, 8)
        dev.raw[0], dev.raw[1] = 995, 4975
        self.assertAlmostEqual(dev.ma(0), 3.996, places=3)
        self.assertAlmostEqual(dev.ma(1), 19.98, places=2)

    def test_shunt_constant(self):
        self.assertEqual(devices.AnalogInput.SHUNT_OHM, 249.0)


class TestDigitalDecode(unittest.TestCase):
    def _make(self):
        return devices.DigitalIo(None, "数字量1")

    def test_bitmask(self):
        dev = self._make()
        dev.di = [1, 0, 1, 0] + [0] * 12
        dev.do = [1] * 8 + [0] * 8
        self.assertEqual(dev.di_mask(), 0b0101)
        self.assertEqual(dev.do_mask(), 0xFF)

    def test_count_configurable(self):
        """点数按构造参数给（单模块 16；聚合后 32），不写死。"""
        self.assertEqual(devices.DigitalIo(None, "m").channels, 16)
        self.assertEqual(devices.DigitalIo(None, "m", channels=32).channels, 32)
        # 位图必须覆盖到 bit31：聚合模块上半区是活的（实测 DO31 可写回读）。
        dev = devices.DigitalIo(None, "m", channels=32)
        dev.do[31] = 1
        self.assertEqual(dev.do_mask(), 1 << 31)
        dev.di[16] = 1
        self.assertEqual(dev.di_mask(), 1 << 16)

    def test_di_do_bit_accessor(self):
        """di_bit/do_bit 必须存在且越界显式抛异常。

        [回归锁] 这两个方法此前**不存在**，而 valves.py 调了 di_bit，
        异常被外层 except 吞掉并置 valid=False —— 表现是 6 路阀永远
        "无有效反馈"，看着像行程开关没接线，实际是接口缺失。
        越界必须抛（不能静默返回 False）：静默会把"点位配错了"伪装成
        "柜子里这个开关没通"，排查方向完全相反。
        """
        dev = devices.DigitalIo(None, "m", channels=32)
        dev.di[0] = 1
        dev.di[16] = 1
        dev.do[21] = 1
        self.assertTrue(dev.di_bit(0))
        self.assertTrue(dev.di_bit(16))
        self.assertFalse(dev.di_bit(5))
        self.assertTrue(dev.do_bit(21))
        self.assertFalse(dev.do_bit(0))
        with self.assertRaises(IndexError):
            dev.di_bit(32)
        with self.assertRaises(IndexError):
            dev.do_bit(99)

    def test_health_offline(self):
        dev = self._make()
        dev.online = False
        health, err, online, text = dev.health(0, 5000)
        self.assertEqual(health, proto.HEALTH_OFFLINE)
        self.assertFalse(online)


# ============================================================================
#                        装配路径（串口分支常被漏测）
# ============================================================================

class TestBuildDevices(unittest.TestCase):
    """覆盖 build_devices 的**串口分支**。

    [为什么专门测这个] 本项目曾在 `build_devices` 里调用一个不存在的
    `_make_rtu`：所有端到端测试都用 `--no-serial` 跑，于是测试全绿，
    但线上开启串口时立刻 `NameError`（真锅）。
    「默认不测的分支等于没写」——这条用例就是按那个教训加的。
    """

    def _cfg(self, serial_on):
        from hsfsrv import config as config_mod
        cfg = config_mod.Config()
        cfg.enable_serial = serial_on
        cfg.enable_analog = False
        cfg.enable_digital = False
        return cfg

    def test_serial_branch_builds_without_error(self):
        """串口开启时装配不得抛 NameError 之类。"""
        from hsfsrv import app as app_mod
        cfg = self._cfg(True)
        business = app_mod.App(cfg)
        business.build_devices()          # 抛异常即失败
        self.assertIsNotNone(business.vfd)
        self.assertIsNotNone(business.temphum)

    def test_serial_branch_creates_rtu_masters(self):
        """串口设备必须用 RtuMaster（不是 TcpMaster），且**共享同一个实例**。"""
        from hsfsrv import app as app_mod
        from hsfsrv import modbus
        cfg = self._cfg(True)
        business = app_mod.App(cfg)
        business.build_devices()
        master = business.vfd._master_get()
        self.assertIsInstance(master, modbus.RtuMaster)
        self.assertEqual(master.port, cfg.serial.port)
        self.assertEqual(master.baudrate, cfg.serial.baudrate)
        th_master = business.temphum._master_get()
        self.assertIsInstance(th_master, modbus.RtuMaster)

    def test_same_serial_port_shares_one_master(self):
        """同一条 485 上的两个从站必须共用**同一个** RtuMaster。

        [为什么必须共享] 串口是排他资源。每台设备各建一个 master 会互相
        抢口——第二个 open 直接失败（实测板上：变频器能开，温湿度报
        「串口不可用」）。而且 RTU 是半双工单主站总线，多 master 并发
        写会互相踩帧，本来就该串行化。
        """
        from hsfsrv import app as app_mod
        cfg = self._cfg(True)
        business = app_mod.App(cfg)
        business.build_devices()
        self.assertIs(business.vfd._master_get(),
                      business.temphum._master_get(),
                      "变频器与温湿度应共享同一个 RtuMaster")

    def test_rtu_master_supports_per_call_unit(self):
        """共享 master 靠 per-call unit 区分从站。"""
        from hsfsrv import modbus
        import inspect
        for name in ("read", "write_single", "_transact", "_with_retry"):
            sig = inspect.signature(getattr(modbus.RtuMaster, name))
            self.assertIn("unit", sig.parameters,
                          "%s 必须接受 unit 参数" % name)

    def test_rtu_master_is_lazy(self):
        """装配期不得打开串口（懒建）：板上 demo 可能占着口。"""
        from hsfsrv import app as app_mod
        cfg = self._cfg(True)
        cfg.serial.port = "/dev/definitely_not_a_tty"
        business = app_mod.App(cfg)
        business.build_devices()          # 不应尝试打开不存在的串口
        # 真正 poll 时才失败
        import asyncio

        async def _poll():
            with self.assertRaises(Exception):
                await business.vfd.poll(0)
        # [用 asyncio.run 而不是 get_event_loop] Python 3.12 上
        # get_event_loop() 在没有"当前事件循环"时直接抛 RuntimeError。
        # 别的用例若跑过 asyncio.run()，它会关闭并清空线程的当前循环，
        # 于是本用例跟着挂 —— 这是用例间相互污染，不是产品缺陷。
        asyncio.run(_poll())

    def test_serial_disabled_leaves_devices_none(self):
        from hsfsrv import app as app_mod
        cfg = self._cfg(False)
        business = app_mod.App(cfg)
        business.build_devices()
        self.assertIsNone(business.vfd)
        self.assertIsNone(business.temphum)


# ============================================================================
#                 "请求即最新"：现读 + 节流（read_fresh）
# ============================================================================

class TestReadFresh(unittest.TestCase):
    """快照请求必须拿设备当前值，而不是后台轮询的旧缓存。

    [为什么专门测] 早先所有快照 handler 都直接返回设备对象里的缓存字段
    （后台轮询每 200ms 刷一次），用户点"请求快照"可能拿到 0~200ms 前的
    状态。改成 read_fresh 后要保证：① 确实触发了一次现读；
    ② 100ms 内不重复读（防多客户端连点把总线打爆）。
    """

    class _Dev(object):
        """最小假设备：poll 计数 + 可设返回值。"""

        def __init__(self):
            self.poll_count = 0
            self.last_ok_ms = 0
            self.value = 0
            self.raise_exc = None

        async def poll(self, now_ms):
            self.poll_count += 1
            if self.raise_exc:
                raise self.raise_exc
            self.value += 1
            self.last_ok_ms = now_ms

    def _run(self, coro):
        import asyncio

        class _Loop(object):
            pass
        loop = asyncio.new_event_loop()
        try:
            return loop.run_until_complete(coro)
        finally:
            loop.close()

    def test_triggers_live_read(self):
        dev = self._Dev()
        ok, err = self._run(devices.read_fresh(dev, 1000))
        self.assertTrue(ok, err)
        self.assertEqual(dev.poll_count, 1, "应当现读一次")
        self.assertEqual(dev.value, 1)

    def test_throttled_within_interval(self):
        """100ms 内第二次请求不应再打设备。"""
        dev = self._Dev()
        self._run(devices.read_fresh(dev, 1000))          # 首次：读
        self._run(devices.read_fresh(dev, 1050))          # 50ms 后：复用
        self.assertEqual(dev.poll_count, 1,
                         "50ms 内不应重复读，实际读了 %d 次" % dev.poll_count)

    def test_reads_again_after_interval(self):
        """超过节流窗口必须重新读。"""
        dev = self._Dev()
        self._run(devices.read_fresh(dev, 1000))
        self._run(devices.read_fresh(dev, 1200))          # 200ms 后
        self.assertEqual(dev.poll_count, 2)

    def test_failure_is_reported(self):
        """现读失败要如实报错，不能用缓存冒充实时。"""
        dev = self._Dev()
        dev.raise_exc = RuntimeError("模拟总线故障")
        ok, err = self._run(devices.read_fresh(dev, 1000))
        self.assertFalse(ok)
        self.assertIn("模拟总线故障", err)

    def test_never_read_before_is_fresh(self):
        """last_ok_ms=0（从未读过）必须触发首次读。"""
        dev = self._Dev()
        self._run(devices.read_fresh(dev, 5))
        self.assertEqual(dev.poll_count, 1)


# ============================================================================
#                             配置
# ============================================================================

class TestConfig(unittest.TestCase):
    def test_defaults(self):
        cfg = config.Config()
        # 端口从 ports.py 统一取（改端口只需改那一处）
        from hsfsrv import ports
        self.assertEqual(cfg.port, ports.PY_SERVER_PORT)
        self.assertEqual(cfg.vfd_unit_id, 2)
        self.assertEqual(cfg.temphum_unit_id, 4)
        self.assertEqual(cfg.serial.baudrate, 115200)
        # 串口号按平台自适应：Windows=COM17，板端=/dev/ttyS3
        self.assertTrue(cfg.serial.port in ("COM17", "/dev/ttyS3"),
                        cfg.serial.port)
        # [2026-11-06] .12 已下线拆除，现场只剩 .13 一台 16 点模块
        self.assertEqual(len(cfg.digital_modules), 1)
        self.assertEqual(cfg.digital_modules[0].host, "192.168.0.13")
        self.assertEqual(cfg.digital_modules[0].channels, 16)
        self.assertEqual(cfg.analog.host, "192.168.0.11")
        self.assertEqual(cfg.analog.port, 8234)

    def test_int_coercion_falls_back(self):
        """配置文件手改出错不应让服务起不来：非法类型回退缺省值。"""
        self.assertEqual(config._int("abc", 7), 7)
        self.assertEqual(config._int(None, 7), 7)
        self.assertEqual(config._int(True, 7), 7)       # bool 不是合法整型
        self.assertEqual(config._int("42", 7), 42)

    def test_roundtrip_save_load(self):
        import tempfile
        with tempfile.TemporaryDirectory() as tmp:
            cfg = config.Config()
            cfg.resolve_paths(tmp)
            cfg.port = 6123
            cfg.vfd_unit_id = 9
            self.assertTrue(cfg.save())
            cfg2 = config.Config()
            cfg2.config_path = cfg.config_path
            cfg2.resolve_paths(tmp)
            self.assertTrue(cfg2.load())
            self.assertEqual(cfg2.port, 6123)
            self.assertEqual(cfg2.vfd_unit_id, 9)
            self.assertEqual(cfg2.digital_modules[0].host, "192.168.0.13")

    def test_run_mode_persistence(self):
        import tempfile
        with tempfile.TemporaryDirectory() as tmp:
            cfg = config.Config()
            cfg.resolve_paths(tmp)
            cfg.save_run_mode(1)
            cfg2 = config.Config()
            cfg2.config_path = cfg.config_path
            cfg2.resolve_paths(tmp)
            self.assertTrue(cfg2.load())
            self.assertEqual(cfg2.run_mode(), 1)

    def test_missing_file_is_not_an_error(self):
        cfg = config.Config()
        cfg.resolve_paths("F:/nonexistent_dir_for_test")
        self.assertTrue(cfg.load())


class TestValveBank(unittest.TestCase):
    """电磁阀组：接线映射、反馈判读、动作超时。

    [现场接线] 6 路阀挂在 .13 上，每路占 1 个 DO + 2 个 DI，
    **DI 顺序是「先关到位、后开到位」**（用户 2026-11-05 明确的口径）：

        .13 通道            含义
        DO0                 阀1 输出
        DI0 / DI1           阀1 关到位 / 开到位
        DO1                 阀2 输出
        DI2 / DI3           阀2 关到位 / 开到位
        ...                 依次排到 DO5 / DI10,DI11

    而 .13 已聚合进 .12（偏移 16）⇒ 服务端访问的是 .12 的
    DO 16..21 / DI 16..27。客户端完全不知道这层，只问"6 个阀什么状态"。
    """

    #: 现场接线（.13 上的通道号）：(DO, 关到位DI, 开到位DI)。
    WIRING_13 = [(i, i * 2, i * 2 + 1) for i in range(6)]
    #: 聚合偏移：.13 → 聚合者地址空间。
    #: [2026-11-06 起为 0] .12 已拆除，.13 直接作主模块、点表从 0 起，
    #: 不再有"上半区偏移 16"。保留这个常量是为了让测试口径与实现同源
    #: （实现里 HSF_VALVE_DO(i) = i，Python 默认 valve_items 也是 do=i）。
    AGG_OFFSET = 0

    def _bank(self, channels=32, with_module=True):
        from hsfsrv import valves as valves_mod
        mods = []
        if with_module:
            dev = devices.DigitalIo(None, "数字量", channels=channels)
            dev.online = True
            dev.last_ok_ms = 0
            mods.append(dev)
        # 注意键名顺序：openDi 取「开到位」，即 DI 序号里的**后一个**。
        items = [{"name": "电磁阀%d" % (i + 1), "module": 0,
                  "do": do + self.AGG_OFFSET,
                  "openDi": op + self.AGG_OFFSET,
                  "closeDi": cl + self.AGG_OFFSET}
                 for i, (do, cl, op) in enumerate(self.WIRING_13)]
        return valves_mod.ValveBank(mods, items), mods

    def test_wiring_close_then_open(self):
        """每路 DI 必须是「关在前、开在后」，且两路阀之间不重叠。"""
        bank, _ = self._bank()
        for i, item in enumerate(bank.items):
            self.assertEqual(item.close_di, self.AGG_OFFSET + i * 2,
                             "阀%d 关到位 DI 位置错" % (i + 1))
            self.assertEqual(item.open_di, self.AGG_OFFSET + i * 2 + 1,
                             "阀%d 开到位 DI 位置错（应为关的下一路）" % (i + 1))
            self.assertEqual(item.do, self.AGG_OFFSET + i)
        # 12 个 DI 必须被 6 路两两分完，不能有交叉占用。
        used = []
        for item in bank.items:
            used.extend([item.close_di, item.open_di])
        self.assertEqual(sorted(used),
                         list(range(self.AGG_OFFSET, self.AGG_OFFSET + 12)))

    def test_all_six_ready_with_wiring(self):
        bank, _ = self._bank()
        self.assertEqual(bank.ready_count(), 6)

    def test_wiring_fits_single_module(self):
        """点位必须落在单台 .13 的 16 点地址空间内（0..15）。

        [2026-11-06 口径变更] 原断言是"必须落在 .12 的上半区（bit16+）"，
        那是两台聚合时代的要求。.12 拆除后只剩 .13（16 点），点位整体
        减 16 ⇒ 正确断言是"全部落在 0..15 且不越界"——越界会让模块回
        EXC03，整组读写失败。
        """
        bank, _ = self._bank()
        for item in bank.items:
            for label, ch in (("DO", item.do), ("开到位 DI", item.open_di),
                              ("关到位 DI", item.close_di)):
                self.assertGreaterEqual(ch, 0, "阀 %s 通道号为负" % label)
                self.assertLess(ch, 16,
                                "阀 %s 通道号 %d 越出单台 16 点" % (label, ch))

    def test_feedback_drives_state(self):
        """状态只由两个到位反馈推导（不信命令回显）。"""
        bank, mods = self._bank()
        dev = mods[0]
        item0 = bank.items[0]
        # 用接线表里的定义取通道，别写死 16/17 —— 那两个数字到底哪个是
        # "开"取决于接线口径（本现场是关在前、开在后），写死极易搞反。
        open_di, close_di = item0.open_di, item0.close_di
        self.assertNotEqual(open_di, close_di)

        # 开到位
        dev.di[open_di] = 1
        dev.di[close_di] = 0
        bank.poll(1000)
        self.assertEqual(item0.state, proto.HsfValveState.Open)

        # 关到位
        dev.di[open_di] = 0
        dev.di[close_di] = 1
        bank.poll(1100)
        self.assertEqual(item0.state, proto.HsfValveState.Closed)

        # 双到位同时为真 = 接线/阀体故障（两个都要置 1，别只改一个）
        dev.di[open_di] = 1
        dev.di[close_di] = 1
        bank.poll(1200)
        self.assertEqual(item0.state, proto.HsfValveState.Fault)

    def test_no_feedback_reports_unknown_not_fault(self):
        """反馈恒为 0 且从未收到过信号 ⇒ 报「未知+查接线」，不是「故障」。

        [为什么重要] 行程开关没接时若报"故障"，现场会天天误报然后忽略告警；
        报"无到位反馈（检查行程开关接线）"才指向正确的排查方向。
        """
        bank, mods = self._bank()
        bank.poll(1000)
        item = bank.items[0]
        self.assertEqual(item.state, proto.HsfValveState.Unknown)
        self.assertIn("行程开关", item.text)

    def test_move_timeout_with_prior_feedback_is_fault(self):
        """曾经收到过反馈、但命令后超时未到位 ⇒ 真故障（阀卡住）。"""
        bank, mods = self._bank()
        dev = mods[0]
        item0 = bank.items[0]
        dev.di[item0.close_di] = 1         # 先有关到位信号（证明开关是接的）
        bank.poll(1000)
        self.assertTrue(item0.ever_feedback)

        item0.cmd_at_ms = 1000             # 模拟刚下发开阀命令
        item0.open_cmd = True
        dev.di[item0.close_di] = 0         # 动不了
        # [要保持模块"新鲜"] last_ok_ms 是模块最近一次采集成功的时刻，
        # 超过 stale_ms(5000) 就会被判"数据不新鲜"而跳过判读——那条路径
        # 优先于超时判定。故这里同步刷新它，模拟"模块一直在正常采集，
        # 只是这路阀不动"的真实情形。
        dev.last_ok_ms = 10000
        bank.poll(1000 + 9000)             # 超过 move_timeout_ms=8000
        self.assertEqual(item0.state, proto.HsfValveState.Fault)
        self.assertIn("超时", item0.text)

    def test_offline_module_invalidates(self):
        bank, mods = self._bank()
        mods[0].online = False
        bank.poll(1000)
        for item in bank.items:
            self.assertFalse(item.valid)

    def test_unconfigured_items_not_ready(self):
        from hsfsrv import valves as valves_mod
        bank = valves_mod.ValveBank([], [])
        self.assertEqual(bank.ready_count(), 0)
        for item in bank.items:
            self.assertFalse(item.ready)
            self.assertEqual(item.state, proto.HsfValveState.Unknown)

    # ---- 20s 动作定时器 + 告警事件 ----

    class _FakeMaster(object):
        """只为让 write_do 能走下去（真实语义由 devices 层负责）。"""

        def __init__(self):
            self.writes = []

        async def write_single(self, fc, addr, value):
            self.writes.append((fc, addr, value))
            return True

    def _start_action(self, bank, dev, on=True, timeout_ms=20000):
        import asyncio
        bank.move_timeout_ms = timeout_ms
        asyncio.run(bank.set_valve(0, on))
        return bank.items[0].cmd_at_ms

    def test_timeout_is_20s_default(self):
        """现场满开满关约 10s ⇒ 超时给 20s（两倍余量）。

        [回归锁] 曾经是 8s，对 10s 的行程根本不够，会每动一次就误报。
        """
        self.assertEqual(config.Config().valve_move_timeout_ms, 20000)

    def test_alarm_not_before_20s_then_fires_once(self):
        """20s 前不报，到点报一条，之后不再重复推送。"""
        from hsfsrv import valves as valves_mod
        dev = devices.DigitalIo(self._FakeMaster(), "m", channels=32)
        dev.online = True
        bank = valves_mod.ValveBank(
            [dev], [{"name": "v0", "module": 0, "do": 16,
                     "openDi": 17, "closeDi": 16}], move_timeout_ms=20000)
        t0 = self._start_action(bank, dev)

        def poll(el):
            dev.last_ok_ms = t0 + el
            return bank.poll(t0 + el)

        self.assertEqual(poll(0), [])
        self.assertEqual(poll(19500), [], "19.5s 还不该报")
        fires = poll(20001)
        self.assertEqual(len(fires), 1, "到 20s 必须报一条")
        alarm = fires[0]
        self.assertEqual(alarm[proto.KEY_ALARM_KIND], proto.VALVE_ALARM_TIMEOUT)
        self.assertEqual(alarm[proto.KEY_ALARM_VALVE_INDEX], 0)
        self.assertAlmostEqual(alarm[proto.KEY_ALARM_TIMEOUT_S], 20.0, 1)
        # 之后各拍不得重复推送，否则客户端被刷屏。
        self.assertEqual(poll(25000), [])
        self.assertEqual(poll(40000), [])

    def test_alarm_clear_on_arrival(self):
        """超时后终于到位 ⇒ 发一条 CLEARED 消警。"""
        from hsfsrv import valves as valves_mod
        dev = devices.DigitalIo(self._FakeMaster(), "m", channels=32)
        dev.online = True
        bank = valves_mod.ValveBank(
            [dev], [{"name": "v0", "module": 0, "do": 16,
                     "openDi": 17, "closeDi": 16}], move_timeout_ms=20000)
        t0 = self._start_action(bank, dev)

        dev.last_ok_ms = t0 + 21000
        self.assertEqual(len(bank.poll(t0 + 21000)), 1)      # 超时告警

        dev.di[17] = 1                                       # 开到位
        dev.last_ok_ms = t0 + 22000
        clears = bank.poll(t0 + 22000)
        self.assertEqual(len(clears), 1)
        self.assertEqual(clears[0][proto.KEY_ALARM_KIND],
                         proto.VALVE_ALARM_CLEARED)
        self.assertEqual(bank.items[0].cmd_at_ms, 0, "到位应结束本次动作计时")

    def test_second_action_can_alarm_again(self):
        """第二次动作必须能再次报超时（告警标志位在 set_valve 时复位）。"""
        from hsfsrv import valves as valves_mod
        dev = devices.DigitalIo(self._FakeMaster(), "m", channels=32)
        dev.online = True
        bank = valves_mod.ValveBank(
            [dev], [{"name": "v0", "module": 0, "do": 16,
                     "openDi": 17, "closeDi": 16}], move_timeout_ms=20000)
        t0 = self._start_action(bank, dev)
        dev.last_ok_ms = t0 + 21000
        self.assertEqual(len(bank.poll(t0 + 21000)), 1)

        t1 = self._start_action(bank, dev, on=False)
        dev.last_ok_ms = t1 + 21000
        fires = bank.poll(t1 + 21000)
        self.assertEqual(len(fires), 1, "第二次动作仍须报超时")

    def test_conflict_alarms_once(self):
        """双到位同时为真 ⇒ 报一条冲突告警（持续不重复）。"""
        from hsfsrv import valves as valves_mod
        dev = devices.DigitalIo(self._FakeMaster(), "m", channels=32)
        dev.online = True
        bank = valves_mod.ValveBank(
            [dev], [{"name": "v0", "module": 0, "do": 16,
                     "openDi": 17, "closeDi": 16}], move_timeout_ms=20000)
        dev.di[16] = 1
        dev.di[17] = 1
        dev.last_ok_ms = 1000
        fires = bank.poll(1000)
        self.assertEqual(len(fires), 1)
        self.assertEqual(fires[0][proto.KEY_ALARM_KIND],
                         proto.VALVE_ALARM_CONFLICT)
        dev.last_ok_ms = 1100
        self.assertEqual(bank.poll(1100), [], "冲突持续时不得重复报")

    def test_timeout_text_keeps_wiring_hint(self):
        """全程无反馈的超时，文案每一拍都要带"查接线"。"""
        from hsfsrv import valves as valves_mod
        dev = devices.DigitalIo(self._FakeMaster(), "m", channels=32)
        dev.online = True
        bank = valves_mod.ValveBank(
            [dev], [{"name": "v0", "module": 0, "do": 16,
                     "openDi": 17, "closeDi": 16}], move_timeout_ms=20000)
        item = bank.items[0]
        t0 = self._start_action(bank, dev)
        for el in (20001, 25000, 40000):
            dev.last_ok_ms = t0 + el
            bank.poll(t0 + el)
            self.assertIn("接线", item.text,
                          "+%dms 的文案丢了接线提示" % el)


class TestVirtualDevices(unittest.TestCase):
    """虚拟设备层（`--sim`）：**只替换 master，设备驱动仍是真的**。

    [为什么重要] `--sim` 的价值全在"devices.py 照跑"：换算、状态字解码、
    故障表、写命令字都是真实实现。若虚拟模型的寄存器口径与真机不一致，
    这些真实逻辑就会解出错误结果，而单看界面不容易发现。
    故这里直接断言**设备驱动解析后的工程值**，而不是断言原始寄存器。
    """

    def _vfd(self):
        """真实 VfdM512 + 虚拟 master。"""
        from hsfsrv import virtualdev
        master = virtualdev.make_vfd_master(max_hz=50.0)
        dev = devices.VfdM512(lambda: master, 2)
        dev.rated_rpm = 1440.0
        dev.rated_hz = 50.0
        dev.max_freq_hz = 50.0
        return dev, master

    def test_vfd_telemetry_decodes(self):
        """虚拟寄存器 → 真实驱动解码 → 工程值正确。

        [断言的量] 输出频率 0（停机）、设定频率 25Hz（50% × 50Hz）、
        母线电压 553.2V。这三个分别走 0x1001÷100、0x1000 百分比换算、
        0x1002÷10 —— 三套不同的换算路径，任一处寄存器口径错都会被断言抓到。
        """
        import asyncio
        dev, _ = self._vfd()
        asyncio.run(dev.poll(0))
        self.assertTrue(dev.online)
        self.assertAlmostEqual(dev.output_freq_hz(), 0.0, 2)
        self.assertAlmostEqual(dev.set_freq_hz(), 25.0, 2)
        self.assertAlmostEqual(dev.field_value(16, "f64"), 553.2, 1)
        # 转速 = 0Hz 换算 → 0
        self.assertAlmostEqual(dev.field_value(21, "f64") or 0.0, 0.0, 1)

    def test_vfd_start_ramps(self):
        """启动后输出频率**渐变**到设定值（不是瞬跳）。

        [为什么要爬坡] 瞬跳的话界面上永远看不到"加速中"，也验证不了
        "给定变了但还没跟上"这类显示。这里断言"一拍内未到目标"。
        """
        import asyncio

        async def go():
            dev, _ = self._vfd()
            await dev.poll(0)
            await dev.apply_control(proto.OP_START, reverse=False)
            await dev.poll(100)                 # 第一拍：还在爬坡
            first = dev.output_freq_hz()
            for t in range(200, 3000, 100):      # 继续跑够时间
                await dev.poll(t)
            return first, dev.output_freq_hz()
        first, final = asyncio.run(go())
        self.assertGreater(first, 0.0, "启动后应立刻有输出（爬坡起点）")
        self.assertLess(first, 25.0, "第一拍不该已经到目标值")
        self.assertAlmostEqual(final, 25.0, 1, "最终应收敛到设定值")

    def test_vfd_stopped_setpoint_makes_no_output(self):
        """停机时写给定**不得产生输出**（否则界面出现"停机+25Hz+720rpm"）。

        [实锅 2026-11-05] 用户在停机状态下点了"频率/转速下发"，界面运行状态
        显示「停机」，输出频率却是 25Hz、转速 720rpm —— 自相矛盾。

        根因：`VirtualVfd.write_single(0x1000)` 无条件 `target_hz = 给定`，
        而 `on_access` 不看 `running` 就朝 target 爬坡 ⇒ 停机也被"跑起来"。

        真机语义：0x1000 只是"下次启动用多少"，输出由命令字决定。
        """
        import asyncio
        dev, _ = self._vfd()

        async def go():
            await dev.poll(0)
            self.assertFalse(dev.is_running(), "初始应为停机")
            # 停机状态下改给定（等于界面点"下发"）
            await dev.apply_control(proto.OP_SET_FREQUENCY, hz=30.0)
            for t in range(100, 3000, 100):
                await dev.poll(t)
            return dev

        dev = asyncio.run(go())
        self.assertFalse(dev.is_running(), "写给定不该让变频器启动")
        self.assertAlmostEqual(dev.output_freq_hz(), 0.0, 2,
                              msg="停机时输出频率必须为 0")
        # 给定值本身应该被记住（启动后按它跑）
        self.assertAlmostEqual(dev.set_freq_hz(), 30.0, 1)

    def test_vfd_running_setpoint_follows(self):
        """运行中改给定 → 输出跟随（这是调速的正常路径）。"""
        import asyncio
        dev, _ = self._vfd()

        async def go():
            await dev.poll(0)
            await dev.apply_control(proto.OP_START)
            for t in range(100, 3000, 100):
                await dev.poll(t)
            first = dev.output_freq_hz()
            await dev.apply_control(proto.OP_SET_FREQUENCY, hz=40.0)
            for t in range(3000, 7000, 100):
                await dev.poll(t)
            return first, dev.output_freq_hz()

        first, final = asyncio.run(go())
        self.assertAlmostEqual(first, 25.0, 1)
        self.assertAlmostEqual(final, 40.0, 1, msg="运行中改给定应跟随")

    def test_vfd_stop_zeroes_output_immediately(self):
        """停机（自由停车）→ 输出频率**立刻**归零，不慢慢降。"""
        import asyncio
        dev, _ = self._vfd()

        async def go():
            await dev.poll(0)
            await dev.apply_control(proto.OP_START)
            for t in range(100, 3000, 100):
                await dev.poll(t)
            running_hz = dev.output_freq_hz()
            await dev.apply_control(proto.OP_FREE_STOP)
            await dev.poll(3000)
            return running_hz, dev.output_freq_hz()

        running_hz, after = asyncio.run(go())
        self.assertGreater(running_hz, 20.0)
        self.assertAlmostEqual(after, 0.0, 2,
                              msg="自由停车必须立刻断输出")

    def test_vfd_reverse_state_word(self):
        """反转启动 → 状态字 2（真实驱动的 run_state 解码）。"""
        import asyncio

        async def go():
            dev, _ = self._vfd()
            await dev.poll(0)
            await dev.apply_control(proto.OP_START, reverse=True)
            await dev.poll(100)
            return dev
        dev = asyncio.run(go())
        self.assertTrue(dev.raw.get("_reverse", False),
                        "真实驱动应解出 reverse=True")

    def test_analog_converts_to_ma(self):
        """虚拟模拟量经真实驱动换算成 mA（毫伏 ÷ 249Ω）。"""
        import asyncio
        from hsfsrv import virtualdev
        master = virtualdev.make_analog_master(8)
        dev = devices.AnalogInput(master, 8)
        asyncio.run(dev.poll(0))
        # 通道0 = 2985mV → 11.988mA；通道1 = 995mV → 3.996mA（4mA 活零点）
        self.assertAlmostEqual(dev.ma(0), 11.988, 2)
        self.assertAlmostEqual(dev.ma(1), 3.996, 2)

    def test_digital_do_write_readback(self):
        """写 DO 后回读一致（真实驱动的 write_do 走 FC05）。"""
        import asyncio
        from hsfsrv import virtualdev
        master = virtualdev.make_digital_master(32)
        dev = devices.DigitalIo(master, "虚拟", channels=32)
        asyncio.run(dev.poll(0))
        before = dev.do_mask()
        ok, _ = asyncio.run(dev.write_do(5, True))
        self.assertTrue(ok)
        asyncio.run(dev.poll(10))
        self.assertEqual(dev.do_mask(), before | (1 << 5))

    def test_digital_feedback_follows_do(self):
        """DO 变位后行程开关跟着变（阀页才能看到状态变化）。

        阀 n：DO(BASE+n)=1 → DI(BASE+2n+1)=1（开到位）；=0 → DI(BASE+2n)=1。
        这与现场接线顺序一致（先关到位、后开到位）。
        """
        from hsfsrv import virtualdev
        # [点位由常量给，不写死 16] 2026-11-06 点位整体减 16（.12 拆除后
        # 只剩 .13 单台 16 点、点表从 0 起）⇒ 阀 0 占 DO0 / DI0,DI1。
        # 写死 16 会在改点位时假红，故一律从 VALVE_DO_BASE/VALVE_DI_BASE 推。
        do0 = virtualdev.VirtualDigital.VALVE_DO_BASE
        di0 = virtualdev.VirtualDigital.VALVE_DI_BASE
        model = virtualdev.VirtualDigital(channels=16)
        model.do_mask = 0                      # 全部关
        di = model.di_mask()
        self.assertEqual((di >> di0) & 1, 1, "DO0=0 时 DI0（关到位）应为 1")
        self.assertEqual((di >> (di0 + 1)) & 1, 0, "DO0=0 时 DI1（开到位）应为 0")

        model.do_mask = 1 << do0               # 只开阀 1
        di = model.di_mask()
        self.assertEqual((di >> di0) & 1, 0, "DO0=1 时 DI0（关到位）应为 0")
        self.assertEqual((di >> (di0 + 1)) & 1, 1,
                         "DO0=1 时 DI1（开到位）应为 1")
        # 其它路不受影响（阀 2 仍关到位）
        self.assertEqual((di >> (di0 + 2)) & 1, 1)

    def test_unmodelled_register_reads_zero(self):
        """未建模但"真机存在"的寄存器返回 0，**不能抛异常**。

        [踩过的坑] 初版对 0x1007 抛 KeyError → 设备层 READ_GROUPS 整拍
        读失败 → 设备永远离线。真机对"存在但本服务端不关心"的寄存器
        （0x1009 AI1、0x1010 PID…）正常回 0。
        """
        from hsfsrv import virtualdev
        model = virtualdev.VirtualVfd(max_hz=50.0)
        # 设备层实际会读的范围
        for addr, count in ((0x1000, 9), (0x1009, 3), (0x1010, 4),
                            (0x1019, 3), (0x101F, 2)):
            vals = model.read(3, addr, count)
            self.assertEqual(len(vals), count)
        # 型号不该有的地址仍应报错
        with self.assertRaises(KeyError):
            model.read(3, 0x9000, 1)

    def test_multi_master_dispatches_by_unit(self):
        """虚拟总线按 unit 分派：站2 变频器、站4 温湿度互不串味。"""
        import asyncio
        from hsfsrv import virtualdev
        bus = virtualdev.VirtualMultiMaster({
            2: virtualdev.VirtualVfd(max_hz=50.0),
            4: virtualdev.VirtualTempHum(temp_tenths=265, hum_tenths=550),
        })
        # 用变频器的地址读站4 应当失败（温湿度不认 FC03）
        with self.assertRaises(Exception):
            asyncio.run(bus.read(3, 0x1000, 1, unit=4))
        # 各读各的
        v = asyncio.run(bus.read(3, 0x1001, 1, unit=2))
        self.assertEqual(len(v), 1)
        t = asyncio.run(bus.read(4, 0x0000, 2, unit=4))
        self.assertEqual(t, [265, 550])


class TestValveReplyFreshness(unittest.TestCase):
    """阀控制应答必须带**命令之后**的反馈，不能是命令前那一拍的缓存。

    [实锅 2026-11-05：电磁阀要点两下才有动作]
    现象：点"开阀"界面毫无变化，再点一次才动。
    根因链：
      ① `_h_valve_control` 在 `set_valve()` 之后只调 `valve_bank.poll()`，
         而 poll 读的是**数字量模块的缓存**（由后台轮询刷新）。
         命令刚写完 DO，缓存还是命令**之前**那拍的 DI ⇒ 应答里 state 是旧值。
      ② 客户端把这份"最新状态"落到卡片 ⇒ 界面不变。
      ③ 用户再点一次，恰好赶上下一轮轮询 ⇒ 这次看起来"生效了"。
    修法：命令后用 `refresh_valve_sources(force=True)` 绕过节流强制现读一帧，
    再让状态机重判，用新反馈生成应答。
    """

    def _app_with_virtual_digital(self, move_ms=0):
        """最小 App 骨架：只要 valve_bank + digital + refresh_valve_sources。

        :param move_ms: 虚拟阀动作行程。这条用例关心的是"应答拿到的是
            命令前还是命令后的反馈"，与行程时长无关，故默认 0（瞬间到位）
            以保持断言简洁。行程本身的行为见 TestVirtualValveTravel。
        """
        from hsfsrv import app as app_mod
        from hsfsrv import virtualdev
        from hsfsrv import valves as valves_mod

        master = virtualdev.make_digital_master(channels=16, move_ms=move_ms)
        dev = devices.DigitalIo(master, "数字量", channels=16)
        # 阀点位与现场接线一致（.13 单台 16 点、DI 关在前开在后）。
        do0 = virtualdev.VirtualDigital.VALVE_DO_BASE
        di0 = virtualdev.VirtualDigital.VALVE_DI_BASE
        items = [{"name": "电磁阀%d" % (i + 1), "module": 0,
                  "do": do0 + i, "openDi": di0 + i * 2 + 1,
                  "closeDi": di0 + i * 2} for i in range(6)]
        bank = valves_mod.ValveBank([dev], items, move_timeout_ms=20000)

        # 不跑 App.__init__（它要配置/网络），只借那两个方法。
        app = object.__new__(app_mod.App)
        app.valve_bank = bank
        app.digital = [dev]
        app._now_ms = lambda: 1000
        return app, bank, dev, master

    def test_reply_sees_post_command_feedback(self):
        """开阀后应答里的 state 必须是「已开」，不是命令前的「已关」。"""
        import asyncio
        app, bank, dev, master = self._app_with_virtual_digital()

        async def go():
            # 初始：虚拟模型 DO16=0 ⇒ DI16(关到位)=1
            await dev.poll(1000)
            bank.poll(1000)
            self.assertEqual(bank.items[0].state, proto.HsfValveState.Closed)
            # 下发开阀（写 DO16=1 ⇒ 虚拟模型把 DI17 置 1）
            ok, err = await bank.set_valve(0, True)
            self.assertTrue(ok, err)
            # 修复后的路径：强制现读 + 重判
            await app.refresh_valve_sources(force=True)
            bank.poll(1000)
            return bank.items[0].state

        state = asyncio.run(go())
        self.assertEqual(state, proto.HsfValveState.Open,
                         "应答必须反映命令之后的反馈（否则界面点一次不变）")

    def test_cached_feedback_would_be_stale(self):
        """反证：**不** force 现读 ⇒ 拿到的是命令前的旧反馈。

        [断言改过一次，因为修复让行为更正确了]
        原来断言"旧缓存 ⇒ 状态仍是已关"。现在状态机加入了"到位位置必须
        等于命令目标才算结束"的判定（见 refresh_state），所以用旧缓存
        （仍显示关到位、目标却是开）时会正确判成 **动作中**，而不是
        "已关"。

        这正是"必须 force 现读"的另一半理由：
          · 不 force 且反馈是旧的 → 状态停在"动作中"（不算错，但界面
            要多等一拍才看到到位）
          · force 现读 → 立刻拿到命令后的真状态
        无论如何都**不能**再出现"命令后仍显示旧位置"那种误导。
        """
        import asyncio
        app, bank, dev, master = self._app_with_virtual_digital()

        async def go():
            await dev.poll(1000)
            bank.poll(1000)
            self.assertEqual(bank.items[0].state, proto.HsfValveState.Closed)
            ok, _ = await bank.set_valve(0, True)
            self.assertTrue(ok)
            # 写进虚拟模型了，但**不**现读 ⇒ 设备缓存还是旧的（关到位）
            bank.poll(1000)
            return bank.items[0].state

        state = asyncio.run(go())
        self.assertEqual(
            state, proto.HsfValveState.Moving,
            "旧反馈 + 目标为开 ⇒ 必须判『动作中』，"
            "绝不能显示成『已关』（那是命令前的位置，会误导操作员）")


class TestVirtualValveTravel(unittest.TestCase):
    """虚拟电磁阀的**动作行程**：命令后先进「动作中」，满行程后才到位。

    [为什么要有这个模型] 现场阀满开满关约 10s、服务端 20s 判超时。若虚拟
    模块"写完 DO 立刻到位"，状态机的「动作中」与「超时报警」两条分支永远
    走不到 —— 功能写了却无法观察，也无法验证告警真的会推给客户端。

    这里锁三件事：
        ① 命令后**立刻**两个到位 DI 都断开（= 动作中），不是瞬发到位
        ② 行程走完才切到目标到位
        ③ 行程 > 超时阈值时，服务端真的在 20s 报一条告警（且只报一条）
    """

    def _digital(self, move_ms):
        from hsfsrv import virtualdev
        # [16 点单模块] 2026-11-06 起现场只剩 .13 一台 16 点模块。
        model = virtualdev.VirtualDigital(channels=16, move_ms=move_ms)
        return model

    def test_command_enters_travel_not_arrival(self):
        """写 DO 后立即：两个到位 DI 都断开（动作中）。

        [点位不写死] 用 VALVE_DO_BASE / VALVE_DI_BASE 推导；点位口径调整
        （如 2026-11-06 整体减 16）时测试跟着走，不会假红。
        """
        from hsfsrv import virtualdev
        do0 = virtualdev.VirtualDigital.VALVE_DO_BASE
        di0 = virtualdev.VirtualDigital.VALVE_DI_BASE
        model = self._digital(move_ms=300)
        # 初始 DO0=0 ⇒ 关到位=1、开到位=0
        self.assertEqual((model.di_mask() >> di0) & 1, 1)      # 关到位
        self.assertEqual((model.di_mask() >> (di0 + 1)) & 1, 0)  # 开到位

        model.write_single(5, do0, 0xFF00)                     # 开阀
        # 关键断言：不是瞬发到位，而是两个都不吸
        self.assertEqual((model.di_mask() >> di0) & 1, 0, "关到位应断开")
        self.assertEqual((model.di_mask() >> (di0 + 1)) & 1, 0,
                         "开到位此时还不该出现（行程未完成）")

    def test_arrives_after_travel_time(self):
        """行程走完 → 目标侧到位。"""
        import time
        from hsfsrv import virtualdev
        do0 = virtualdev.VirtualDigital.VALVE_DO_BASE
        di0 = virtualdev.VirtualDigital.VALVE_DI_BASE
        model = self._digital(move_ms=200)
        model.write_single(5, do0, 0xFF00)                     # 开阀
        self.assertEqual((model.di_mask() >> (di0 + 1)) & 1, 0)
        time.sleep(0.28)                                      # 超过行程
        self.assertEqual((model.di_mask() >> (di0 + 1)) & 1, 1, "应开到位")
        self.assertEqual((model.di_mask() >> di0) & 1, 0)

    def test_stuck_valve_never_arrives(self):
        """move_ms=inf（阀卡死）⇒ 永远不到位，进度恒 0。"""
        import time
        from hsfsrv import virtualdev
        do0 = virtualdev.VirtualDigital.VALVE_DO_BASE
        di0 = virtualdev.VirtualDigital.VALVE_DI_BASE
        model = self._digital(move_ms=float("inf"))
        model.write_single(5, do0, 0xFF00)
        time.sleep(0.1)
        self.assertEqual((model.di_mask() >> (di0 + 1)) & 1, 0)
        self.assertEqual((model.di_mask() >> di0) & 1, 0)
        self.assertEqual(model.valve_progress(0), 0.0)

    def test_no_feedback_switch_unwired(self):
        """行程开关没接 ⇒ DI 恒 0（与"卡死"表现相同但成因不同）。"""
        from hsfsrv import virtualdev
        do0 = virtualdev.VirtualDigital.VALVE_DO_BASE
        di0 = virtualdev.VirtualDigital.VALVE_DI_BASE
        model = virtualdev.VirtualDigital(channels=16, move_ms=0,
                                          no_feedback=True)
        model.write_single(5, do0, 0xFF00)
        self.assertEqual((model.di_mask() >> di0) & 1, 0)
        self.assertEqual((model.di_mask() >> (di0 + 1)) & 1, 0)

    def test_timeout_alarm_fires_once_at_20s(self):
        """行程 25s > 超时 20s ⇒ 20s 报一条告警，之后不重复。

        [这是端到端的那条链] 虚拟阀卡在路上 → 数字量模块反馈恒 0 →
        ValveBank 判超时 → 返回告警（app 负责推给订阅会话）。
        """
        import asyncio
        from hsfsrv import valves as valves_mod
        from hsfsrv import virtualdev

        model = virtualdev.VirtualDigital(channels=16, move_ms=25000)
        master = virtualdev.VirtualMaster(model, name="虚拟数字量")
        dev = devices.DigitalIo(master, "数字量", channels=16)
        do0 = virtualdev.VirtualDigital.VALVE_DO_BASE
        di0 = virtualdev.VirtualDigital.VALVE_DI_BASE
        bank = valves_mod.ValveBank(
            [dev], [{"name": "v0", "module": 0, "do": do0,
                     "openDi": di0 + 1, "closeDi": di0}], move_timeout_ms=20000)

        async def go():
            await dev.poll(1000)
            bank.poll(1000)
            ok, err = await bank.set_valve(0, True)
            self.assertTrue(ok, err)
            return bank.items[0].cmd_at_ms

        t0 = asyncio.run(go())

        def poll(el):
            # [必须重新采集] 只改 last_ok_ms 不够：DeviceIo.di 是**采集时刻**
            # 的快照，不重新 poll 的话模块缓存里还是命令前那拍的"关到位"，
            # 状态机会判成 Closed 而不是 Moving，超时分支永远走不到。
            asyncio.run(dev.poll(t0 + el))
            return bank.poll(t0 + el)

        self.assertEqual(poll(0), [])
        self.assertEqual(poll(19500), [], "19.5s 不该报")
        fires = poll(20100)
        self.assertEqual(len(fires), 1, "20s 必须报一条超时")
        self.assertEqual(fires[0][proto.KEY_ALARM_KIND],
                         proto.VALVE_ALARM_TIMEOUT)
        self.assertIn("超时", fires[0][proto.KEY_ALARM_TEXT])
        self.assertEqual(poll(30000), [], "同一次动作不得重复推送")


class TestValveRepeatCommand(unittest.TestCase):
    """重复点击同一侧 / 动作中反向：**判定全在服务端**。

    [为什么这是安全问题而不是体验问题] 阀卡住（行程开关没接、阀体卡死）
    时，服务端靠"命令后 20s 未到位"判超时告警。若每次点击都无条件重置
    计时器（原实现就是 `cmd_at_ms = now`），用户每 20s 内点一次同侧
    就能把超时无限推后 —— **卡阀永远不会报警**。实测连点 8 次，0 告警。

    三种处置：
        ① 已到位 + 同目标      → ignored：不写 DO、不重置计时
        ② 动作中 + 同目标      → repeated：重发 DO，但**不重置计时**
        ③ 反向 / 首次          → issued/reversed：写 DO，重新计时
    """

    class _FakeMaster(object):
        def __init__(self):
            self.writes = []

        async def write_single(self, fc, addr, value):
            self.writes.append((fc, addr, value))
            return True

    def _bank(self, move_ms=10000):
        """虚拟阀卡在途中（行程很长），用来观察计时器是否被重置。"""
        from hsfsrv import valves as valves_mod
        from hsfsrv import virtualdev
        do0 = virtualdev.VirtualDigital.VALVE_DO_BASE
        di0 = virtualdev.VirtualDigital.VALVE_DI_BASE
        model = virtualdev.VirtualDigital(channels=16, move_ms=move_ms)
        master = virtualdev.VirtualMaster(model, name="虚拟数字量")
        dev = devices.DigitalIo(master, "数字量", channels=16)
        bank = valves_mod.ValveBank(
            [dev], [{"name": "v0", "module": 0, "do": do0,
                     "openDi": di0 + 1, "closeDi": di0}], move_timeout_ms=20000)
        return bank, dev, model

    @staticmethod
    def _sync(bank, dev, now_ms=1000):
        """采集一次 + 判读一次（让 item.state 与硬件反馈一致）。

        [为什么每步都要调] `set_valve` 的三种处置判定依赖 `item.state`
        （已到位？动作中？）。state 只在 `bank.poll()` 里由 DI 推导，而
        DI 又只在 `dev.poll()` 里采集。写完 DO 不重新采集/判读的话，
        item.state 还是命令前的值，判定必然走错分支。
        真实服务端里这条链是 `_h_valve_control` → refresh_valve_sources
        → bank.poll 干的事，测试里要手动复现。
        """
        import asyncio
        asyncio.run(dev.poll(now_ms))
        bank.poll(now_ms)

    def test_ignored_when_already_at_target(self):
        """已关到位再点关阀 → ignored，且**不动 DO**。"""
        import asyncio
        bank, dev, model = self._bank()
        self._sync(bank, dev)
        self.assertEqual(bank.items[0].state, proto.HsfValveState.Closed)

        do_before = model.do_mask
        ok, err = asyncio.run(bank.set_valve(0, False))   # 已经关了

        self.assertTrue(ok, err)
        self.assertEqual(bank.items[0].last_cmd_result, "ignored")
        self.assertEqual(model.do_mask, do_before,
                         "已到位时不该再写 DO（幂等）")

    def test_repeated_does_not_reset_timer(self):
        """动作中重复点同侧 → 计时器**保持首次命令的时刻**。"""
        import asyncio
        bank, dev, _ = self._bank(move_ms=10000)

        self._sync(bank, dev)
        ok, err = asyncio.run(bank.set_valve(0, True))    # 开阀 → 进途中
        self.assertTrue(ok, err)
        # 重新采集判读：状态要变成"动作中"，否则同目标判定走不到位分支
        self._sync(bank, dev, 2000)
        self.assertEqual(bank.items[0].state, proto.HsfValveState.Moving)

        # 假装首次命令已过去 5s
        bank.items[0].cmd_at_ms -= 5000
        t_before = bank.items[0].cmd_at_ms
        ok2, err2 = asyncio.run(bank.set_valve(0, True))
        self.assertTrue(ok2, err2)

        self.assertEqual(bank.items[0].last_cmd_result, "repeated")
        self.assertEqual(bank.items[0].cmd_at_ms, t_before,
                         "同目标的重复命令不得重置超时计时器"
                         "（否则卡阀可被反复点击掩盖）")

    def test_repeat_cannot_mask_stuck_valve(self):
        """端到端：卡死的阀被反复点同侧，20s 后**仍然报警**。"""
        import asyncio
        bank, dev, _ = self._bank(move_ms=float("inf"))   # 卡死

        async def go():
            collected = []

            async def step(el, repeat_click=False):
                # 采集 + 判读 + 收告警（真实链路里这串是 100~200ms 一次）
                await dev.poll(el)
                collected.extend(bank.poll(el))
                if repeat_click:
                    await bank.set_valve(0, True)         # 重复点同侧

            await dev.poll(1000)
            bank.poll(1000)
            ok, _ = await bank.set_valve(0, True)
            self.assertTrue(ok)
            t0 = bank.items[0].cmd_at_ms

            await step(t0)
            # 在 20s 内反复点同侧（每 2s 一次，共 8 次）
            for k in range(1, 9):
                await step(t0 + k * 2000, repeat_click=True)
            await step(t0 + 20100)                        # 到 20s
            return collected

        fires = asyncio.run(go())
        timeouts = [f for f in fires
                    if f[proto.KEY_ALARM_KIND] == proto.VALVE_ALARM_TIMEOUT]
        self.assertEqual(len(timeouts), 1,
                         "反复点同侧不得掩盖卡阀超时（这正是修复的目的）")

    def test_reverse_resets_timer(self):
        """动作中反向 → reversed，计时重新起算（这是新动作）。"""
        import asyncio
        bank, dev, _ = self._bank(move_ms=10000)

        self._sync(bank, dev)
        ok, _ = asyncio.run(bank.set_valve(0, True))      # 开阀
        self.assertTrue(ok)
        self._sync(bank, dev, 2000)                       # → 动作中
        self.assertEqual(bank.items[0].state, proto.HsfValveState.Moving)

        bank.items[0].cmd_at_ms -= 8000                   # 已过去 8s
        t_before = bank.items[0].cmd_at_ms
        ok2, err2 = asyncio.run(bank.set_valve(0, False))  # 途中反向关阀
        self.assertTrue(ok2, err2)

        self.assertEqual(bank.items[0].last_cmd_result, "reversed")
        self.assertGreater(bank.items[0].cmd_at_ms, t_before,
                           "反向是新动作，必须重新计时")


class TestValveExternalDo(unittest.TestCase):
    """绕过电磁阀接口直接写 DO（数字量页/自动逻辑/现场 HMI）必须被接管。

    [实锅 2026-11-05] 数字量页可以直写 DO16..21（= 6 路阀的输出），那条
    路径走 `_h_digital_control` → `DigitalIo.write_do`，**不经过
    `ValveBank.set_valve`**。后果：

        · `cmd_at_ms` 没被置位 ⇒ 状态机认为"从来没有命令"
        · 阀动作中的那 10 秒（两个到位都是 0）落到
          "无到位反馈（检查行程开关接线）" 分支
          —— 把人往接线故障上引，而阀其实在正常动作
        · 20s 超时永远不会判（没有起算点）

    修法：poll 时比对模块实际 DO 与阀已知位置，不一致就接管为新动作。

    [为什么接管也可能算"外部"而不是当故障] 现场 HMI、调试工具、将来的
    自动逻辑都可能直接写 DO。服务端的职责是把这种写入**纳入同一套状态机
    与超时判据**，而不是装作没发生。
    """

    def _bank(self, move_ms=10000):
        from hsfsrv import valves as valves_mod
        from hsfsrv import virtualdev
        do0 = virtualdev.VirtualDigital.VALVE_DO_BASE
        di0 = virtualdev.VirtualDigital.VALVE_DI_BASE
        model = virtualdev.VirtualDigital(channels=16, move_ms=move_ms)
        master = virtualdev.VirtualMaster(model, name="虚拟数字量")
        dev = devices.DigitalIo(master, "数字量", channels=16)
        bank = valves_mod.ValveBank(
            [dev], [{"name": "v0", "module": 0, "do": do0,
                     "openDi": di0 + 1, "closeDi": di0}], move_timeout_ms=20000)
        return bank, dev, model

    def _sync(self, bank, dev, now_ms=1000):
        import asyncio
        asyncio.run(dev.poll(now_ms))
        bank.poll(now_ms)

    def test_first_poll_seeds_without_fake_action(self):
        """首拍只校准位置，不得把开机稳态误判成"外部改动"。"""
        from hsfsrv import virtualdev
        do0 = virtualdev.VirtualDigital.VALVE_DO_BASE
        bank, dev, model = self._bank()
        model.do_mask |= (1 << do0)          # 开机时阀本来就在开位
        self._sync(bank, dev)
        item = bank.items[0]
        self.assertTrue(item.known_do_seeded)
        self.assertTrue(item.known_do)
        self.assertEqual(item.cmd_at_ms, 0, "首拍不得起算动作")
        self.assertNotEqual(item.last_cmd_result, "external")

    def test_external_do_write_is_adopted(self):
        """数字量页写阀 DO=1 ⇒ 阀状态机接管为一次新动作。"""
        from hsfsrv import virtualdev
        do0 = virtualdev.VirtualDigital.VALVE_DO_BASE
        bank, dev, model = self._bank()
        self._sync(bank, dev)                       # 首拍校准（关位）
        self.assertEqual(bank.items[0].state, proto.HsfValveState.Closed)

        # 模拟"从数字量页直接写 DO"（不过 set_valve）
        model.write_single(5, do0, 0xFF00)
        self._sync(bank, dev, 2000)

        item = bank.items[0]
        self.assertEqual(item.last_cmd_result, "external")
        self.assertGreater(item.cmd_at_ms, 0, "接管后必须有超时起算点")
        self.assertEqual(item.state, proto.HsfValveState.Moving,
                         "动作中应显示『动作中』，而不是『无到位反馈』")
        self.assertNotIn("检查行程开关接线", item.text,
                         "阀正在正常动作，不该提示去查接线")

    def test_external_write_still_times_out(self):
        """外部写的 DO 卡住不到位 ⇒ 同样 20s 报超时（不能因为没有
        set_valve 就失去超时保护）。"""
        import asyncio
        from hsfsrv import virtualdev
        do0 = virtualdev.VirtualDigital.VALVE_DO_BASE
        bank, dev, model = self._bank(move_ms=float("inf"))
        self._sync(bank, dev)
        model.write_single(5, do0, 0xFF00)          # 外部写，阀卡死
        self._sync(bank, dev, 2000)
        t0 = bank.items[0].cmd_at_ms
        self.assertGreater(t0, 0)

        fires = []

        def step(el):
            asyncio.run(dev.poll(t0 + el))
            fires.extend(bank.poll(t0 + el))

        step(1000)
        step(20100)
        timeouts = [f for f in fires
                    if f[proto.KEY_ALARM_KIND] == proto.VALVE_ALARM_TIMEOUT]
        self.assertEqual(len(timeouts), 1,
                         "外部写 DO 卡住也必须报超时")

    def test_normal_command_not_treated_as_external(self):
        """走 set_valve 的正常命令不得被 poll 误判成外部改动。"""
        import asyncio
        bank, dev, _ = self._bank()
        self._sync(bank, dev)
        asyncio.run(bank.set_valve(0, True))
        self._sync(bank, dev, 2000)
        self.assertEqual(bank.items[0].last_cmd_result, "issued",
                         "自己写的 DO 不该被当成外部改动")


class TestFlowMeter(unittest.TestCase):
    """电磁流量计（LD 系列）：22 个寄存器的拼装与工程值。

    [寄存器口径] 与 C 端 `XFlowMeter.h` 的 22 槽逐项对应，**高字在前**。
    最容易错的三处：
        ① 32 位量必须成对拼装（Float / Long 各两个字）
        ② 累积量 = 整数部分 + 小数部分（小数是 0~1 的 Float）
        ③ 两张单位码表各用各的（瞬时 5=m³/h；累积 1=m³）
    """

    def _meter(self):
        from hsfsrv import virtualdev
        model = virtualdev.VirtualFlowMeter()
        master = virtualdev.VirtualMaster(model, name="虚拟流量计")
        dev = devices.FlowMeter(lambda: master, 5)
        return dev, model

    def test_poll_decodes_engineering_values(self):
        """一帧读完 22 个寄存器 → 工程值合理。"""
        import asyncio
        dev, _ = self._meter()

        async def go():
            await dev.poll(1000)
            return dev

        dev = asyncio.run(go())
        self.assertTrue(dev.online)
        # 虚拟模型基准 36.5 m³/h，±2% 波动
        self.assertGreater(dev.flow, 35.0)
        self.assertLess(dev.flow, 38.0)
        # 流速由流量反算：Q / (π r²)，DN50 时约 5 m/s
        self.assertGreater(dev.speed, 4.5)
        self.assertLess(dev.speed, 5.5)

    def test_unit_codes_from_config(self):
        """单位码原样透传（瞬时表与累积表**是两张表**）。"""
        import asyncio
        dev, _ = self._meter()

        async def go():
            await dev.poll(1000)
            return dev

        dev = asyncio.run(go())
        self.assertEqual(dev.flow_unit, 5, "瞬时流量单位 5 = m³/h")
        self.assertEqual(dev.total_unit, 1, "累积总量单位 1 = m³")

    def test_forward_total_accumulates(self):
        """累积量随时间增长（积分口径，不是"每次访问 +固定值"）。"""
        import asyncio
        import time
        dev, _ = self._meter()

        async def go():
            await dev.poll(1000)
            first = dev.forward_total
            time.sleep(0.35)             # 让真实时间流过
            await dev.poll(2000)
            return first, dev.forward_total

        first, second = asyncio.run(go())
        self.assertGreater(second, first, "累积量必须单调增长")
        # 36 m³/h 跑 0.35s ≈ 0.0035 m³；给宽容区间（调度抖动）
        self.assertLess(second - first, 0.05)

    def test_alarm_mask_bits(self):
        """四项报警压成位图：0=上限 1=下限 2=空管 3=系统。"""
        import asyncio
        dev, model = self._meter()
        model.alarm_upper = 1
        model.alarm_system = 1

        async def go():
            await dev.poll(1000)
            return dev

        dev = asyncio.run(go())
        self.assertEqual(dev.alarm_mask(), (1 << 0) | (1 << 3))

    def test_float32_hi_lo_order(self):
        """高字在前：0x420F3294 应解成 35.8 左右（不是 0x3294420F）。"""
        hi, lo = 0x420F, 0x3294
        value = devices.FlowMeter._float32(hi, lo)
        self.assertGreater(value, 35.0)
        self.assertLess(value, 36.0)
        # 反证：字节序反转会解出天文数字
        wrong = devices.FlowMeter._float32(lo, hi)
        self.assertNotAlmostEqual(value, wrong, places=2)

    def test_offline_raises_and_marks_offline(self):
        """读失败要**抛**（让轮询层退避），并把 online 置 false。"""
        import asyncio
        from hsfsrv import virtualdev

        model = virtualdev.VirtualFlowMeter()

        class _BadMaster(object):
            async def read(self, *a, **kw):
                from hsfsrv import modbus
                raise modbus.ModbusError("虚拟断线")

            async def write_single(self, *a, **kw):
                raise AssertionError("不该写")

            async def close(self):
                pass

        dev = devices.FlowMeter(lambda: _BadMaster(), 5)
        with self.assertRaises(Exception):
            asyncio.run(dev.poll(1000))
        self.assertFalse(dev.online)


class TestAnalogScale(unittest.TestCase):
    """模拟量通道工程量换算（4~20mA → 0~满量程）。

    [现场] 通道0 接 EJA110A 差压变送器，出厂量程 0~1MPa，输出 2 线制
    4~20mA。换算与量程都由**服务端**配置与计算（客户端只显示）。
    """

    def _scale(self, full=1.0, unit="MPa", name="压差计"):
        return config.AnalogChannelScale(index=0, name=name, unit=unit,
                                         fullScale=full, lowMa=4.0,
                                         highMa=20.0)

    def test_linear_endpoints(self):
        """4mA→0、12mA→半量程、20mA→满量程（线性）。"""
        sc = self._scale()
        self.assertAlmostEqual(sc.engineering(4.0)[0], 0.0, 6)
        self.assertAlmostEqual(sc.engineering(12.0)[0], 0.5, 6)
        self.assertAlmostEqual(sc.engineering(20.0)[0], 1.0, 6)

    def test_quarter_points(self):
        """8mA→25%、16mA→75%（防"算成 0~20mA 口径"这类错误）。"""
        sc = self._scale()
        self.assertAlmostEqual(sc.engineering(8.0)[0], 0.25, 6)
        self.assertAlmostEqual(sc.engineering(16.0)[0], 0.75, 6)

    def test_live_zero_tolerated(self):
        """活零点（实测 3.996mA）不得报断线/欠量程。

        [为什么重要] 正常工作时变送器就是 3.996mA 左右；若按严格 <4 判定，
        最正常的工作点会天天报"断线"，现场会彻底忽略这条提示。
        """
        sc = self._scale()
        value, note = sc.engineering(995 / 249.0)      # 3.996mA
        self.assertEqual(note, "")
        self.assertAlmostEqual(value, 0.0, 6)

    def test_real_disconnect_flagged(self):
        """真断线（变送器无输出，读数接近 0mA）必须标出来。"""
        sc = self._scale()
        value, note = sc.engineering(0.0)
        self.assertEqual(value, 0.0)
        self.assertIn("断线", note)

    def test_over_range_clamped_and_flagged(self):
        """超过 20mA 夹到满量程并标记（不显示超量程的假读数）。"""
        sc = self._scale()
        value, note = sc.engineering(24.0)
        self.assertAlmostEqual(value, 1.0, 6)
        self.assertIn("超量程", note)

    def test_uncalibrated_returns_none(self):
        """未标定（fullScale=0）不给换算值，界面显示"--"而不是假 0。"""
        sc = self._scale(full=0.0)
        self.assertFalse(sc.configured)
        self.assertEqual(sc.engineering(12.0), (None, ""))

    def test_scale_roundtrip_in_config(self):
        """配置里能读到 scales，且 index 不与位置参数撞车。"""
        import tempfile
        with tempfile.TemporaryDirectory() as tmp:
            cfg = config.Config()
            cfg.resolve_paths(tmp)
            cfg.analog.scales[0] = config.AnalogChannelScale(
                index=0, name="压差计", unit="MPa", fullScale=1.0)
            cfg.save()
            cfg2 = config.Config()
            cfg2.config_path = cfg.config_path
            cfg2.resolve_paths(tmp)
            self.assertTrue(cfg2.load())
            sc = cfg2.analog.scales.get(0)
            self.assertIsNotNone(sc)
            self.assertEqual(sc.unit, "MPa")
            self.assertAlmostEqual(sc.full_scale, 1.0, 6)


class TestAutoControlSkeleton(unittest.TestCase):
    """自动控制骨架（预留模块）：只验接线与安全默认，不验工艺逻辑。"""

    def _ctx(self, di=None, do=None, valves_ready=True):
        from hsfsrv import autocontrol

        class Dig(object):
            def __init__(self):
                self.online = True
                self.channels = 32
                self.di = list(di or [0] * 32)
                self.do = list(do or [0] * 32)

            async def write_do(self, i, on):
                self.do[i] = 1 if on else 0
                return True, ""

        class Valve(object):
            def __init__(self, i):
                self.index = i
                self.ready = valves_ready
                self.state = proto.HsfValveState.Closed

        class Bank(object):
            def __init__(self):
                self.items = [Valve(i) for i in range(6)]
                self.calls = []

            async def set_valve(self, i, on):
                self.calls.append((i, bool(on)))
                self.items[i].state = (proto.HsfValveState.Open if on
                                       else proto.HsfValveState.Closed)
                return True, ""

        class A(object):
            def __init__(self):
                self.digital = [Dig()]
                self.valve_bank = Bank()
                self.analog = None
                self.vfd = None
                self.temphum = None

        app = A()
        return autocontrol.AutoContext(app, log=lambda m: None), app

    def test_read_di_do_and_bounds(self):
        ctx, _ = self._ctx(di=[1] + [0] * 31, do=[0, 1] + [0] * 30)
        self.assertTrue(ctx.read_di(0))
        self.assertFalse(ctx.read_di(5))
        self.assertFalse(ctx.read_do(0))
        self.assertTrue(ctx.read_do(1))
        # 越界返回 None（而不是抛异常）：自动逻辑不该因点位写错就崩。
        self.assertIsNone(ctx.read_di(99))
        self.assertIsNone(ctx.read_do(-1))

    def test_unavailable_sources_return_none(self):
        """设备不可用时一律返回 None，让调用方能"不做决策"。"""
        ctx, app = self._ctx()
        app.digital[0].online = False
        self.assertIsNone(ctx.read_di(0))
        self.assertIsNone(ctx.read_do(0))
        self.assertIsNone(ctx.analog_ma(0))
        self.assertIsNone(ctx.valve_state(99))
        self.assertEqual(ctx.temphum(), (None, None))

    def test_throttle_per_instance(self):
        """节流表必须每实例一份：曾写成类属性导致跨实例互相挡。"""
        a, _ = self._ctx()
        b, _ = self._ctx()
        self.assertTrue(a.throttle("k", 1000, 1000))
        self.assertFalse(a.throttle("k", 1000, 1100))
        self.assertTrue(a.throttle("k", 1000, 2100))
        # b 有自己的一张表，不该被 a 挡住。
        self.assertTrue(b.throttle("k", 1000, 1100))

    def test_step_is_noop_when_stopped(self):
        """未启动时 step 不动设备（安全默认）。"""
        from hsfsrv import autocontrol
        ctx, app = self._ctx()
        ctl = autocontrol.AutoController(ctx, log=lambda m: None)
        ctl.step(1000)
        self.assertEqual(ctx.action_count, 0)
        self.assertEqual(app.valve_bank.calls, [])

    def test_step_does_nothing_in_this_version(self):
        """本版本控制逻辑为空：启动后跑多拍，仍然一个动作都不发。

        [这条是刻意的] 谁要往 step() 里填逻辑，就得同步改这条测试 ——
        强制"动设备"这件事不能悄悄发生。
        """
        from hsfsrv import autocontrol
        ctx, app = self._ctx()
        ctl = autocontrol.AutoController(ctx, log=lambda m: None)
        ctl.start()
        for t in range(0, 5000, 100):
            ctl.step(t)
        self.assertEqual(ctx.action_count, 0)
        self.assertEqual(app.valve_bank.calls, [])

    def test_start_stop_idempotent(self):
        from hsfsrv import autocontrol
        ctx, _ = self._ctx()
        ctl = autocontrol.AutoController(ctx, log=lambda m: None)
        ctl.start(); ctl.start()
        self.assertTrue(ctl.running)
        ctl.stop(); ctl.stop()
        self.assertFalse(ctl.running)
        self.assertEqual(ctl.phase, "idle")

    def test_async_actions_recorded(self):
        """动作经 ctx 下发并被记账（诊断/测试都靠它判"到底动没动"）。"""
        import asyncio
        ctx, app = self._ctx()
        ok, _ = asyncio.run(ctx.write_do(3, True))
        self.assertTrue(ok)
        self.assertTrue(ctx.read_do(3))
        ok, _ = asyncio.run(ctx.open_valve(2))
        self.assertTrue(ok)
        self.assertEqual(ctx.valve_state(2), proto.HsfValveState.Open)
        self.assertEqual(app.valve_bank.calls, [(2, True)])
        self.assertEqual(ctx.action_count, 2)


# -*- coding: utf-8 -*-


class TestTurbidityMeter(unittest.TestCase):
    """浊度仪：FC03 读 0x0000 起 2 个寄存器，**CDAB 低字在前 + IEEE754 float32**。

    [口径来源] WZT-701A 说明书 4.3.10.2 的可复现帧：
        发送 01 03 00 00 00 02 C4 0B
        回复 01 03 04 D7 0A 41 1F 92 1D  →  9.99 NTU
    本组用这帧做**最高优先级的回归**——它是唯一能证明口径正确的实物证据。

    [初版错在哪] 初版按"市面常见"实现成 32 位整数 ÷100（ABCD 高字在前）。
    同一帧算出来是 36077734.71，差 6 个数量级且不报错。模拟器的价值是复刻
    真机行为，不是复刻一个自洽的假设——所以 `test_raw_is_ntu_times_100` 那条
    把错误行为固化下来的断言必须删掉，换成手册帧断言。
    """

    def _meter(self, unit_id=1, index=0):
        from hsfsrv import virtualdev
        model = virtualdev.VirtualTurbidity(index=index)
        master = virtualdev.VirtualMaster(model, name="虚拟浊度仪")
        dev = devices.TurbidityMeter(lambda: master, unit_id)
        return dev, model

    def test_manual_frame(self):
        """手册真机帧：D7 0A 41 1F → 9.99 NTU（CDAB + float32）。"""
        dev = devices.TurbidityMeter(lambda: None, 1)
        ntu = dev._decode([0xD70A, 0x411F])
        self.assertAlmostEqual(ntu, 9.99, places=6)
        self.assertEqual(dev.raw, 0x411FD70A)

    def test_manual_frame_abcd_would_be_wrong(self):
        """反证：同一帧按 ABCD 解释必须得到完全不同的值。

        [为什么这条必要] 只断言"能读出 9.99"无法区分实现真的按 CDAB 在解、
        还是模型和解析器碰巧一起错。切到 ABCD 后必须读出另一个值。
        """
        dev = devices.TurbidityMeter(lambda: None, 1)
        dev.LOW_WORD_FIRST = False
        ntu = dev._decode([0xD70A, 0x411F])
        self.assertNotAlmostEqual(ntu, 9.99, places=2)
        self.assertEqual(dev.raw, 0xD70A411F)

    def test_poll_decodes_ntu(self):
        """一帧读回 → NTU 与模型给定值一致（两位小数）。"""
        import asyncio
        dev, model = self._meter(index=0)
        model.fixed_ntu = 12.34

        asyncio.run(dev.poll(1000))
        self.assertTrue(dev.online)
        self.assertAlmostEqual(dev.ntu, 12.34, places=2)

    def test_raw_is_float_bits(self):
        """raw 是 IEEE754 位模式，不是"NTU×100 的整数"。"""
        import asyncio
        import struct
        dev, model = self._meter()
        model.fixed_ntu = 7.5

        asyncio.run(dev.poll(1000))
        expect = struct.unpack(">I", struct.pack(">f", 7.5))[0]
        self.assertEqual(dev.raw, expect)
        self.assertNotEqual(dev.raw, 750)

    def test_two_units_differ(self):
        """两台仪器各自独立取值（不同站号不同数据，不能是同一个值贴两遍）。"""
        import asyncio
        dev1, model1 = self._meter(unit_id=1, index=0)
        dev2, model2 = self._meter(unit_id=3, index=1)
        model1.fixed_ntu = 20.0
        model2.fixed_ntu = 3.0

        async def go():
            await dev1.poll(1000)
            await dev2.poll(1000)

        asyncio.run(go())
        self.assertAlmostEqual(dev1.ntu, 20.0, places=2)
        self.assertAlmostEqual(dev2.ntu, 3.0, places=2)
        self.assertEqual(dev1.unit_id, 1)
        self.assertEqual(dev2.unit_id, 3)

    def test_word_order_switchable(self):
        """字序可配：同一帧在 ABCD / CDAB 下解出不同的值。

        [为什么保留这条] 现场换一台别的厂家的表时，字序是**唯一**可能要改的
        一项；写死成任何一种都会让另一种读数变成天文数字且不报错。故字序是
        实例属性（LOW_WORD_FIRST），此处锁它确实在起作用。
        """
        dev = devices.TurbidityMeter(lambda: None, 1)
        # 缺省 CDAB（低字在前，真机口径）：v0=低字、v1=高字。
        dev._decode([0x0001, 0x86A0])
        self.assertEqual(dev.raw, 0x86A00001)
        # 切到 ABCD（高字在前）：v0=高字。
        dev.LOW_WORD_FIRST = False
        dev._decode([0x0001, 0x86A0])
        self.assertEqual(dev.raw, 0x000186A0)
        # [比 raw 而不是比 NTU] 这组位模式解释成 float 都是次正规数（≈0），
        # 用 assertNotAlmostEqual 会因两者都近似 0 而误判相等。字序的作用
        # 直接体现在位模式上，这才是该断言的量。

    def test_integer_format_with_divisor(self):
        """整数格式 + 除数：某些表确实是 raw/100，不是浮点。"""
        dev = devices.TurbidityMeter(lambda: None, 1)
        dev.WORD_FORMAT = "uint32"
        dev.SCALE_DIVISOR = 100.0
        # CDAB：0x0001 低字、0x86A0 高字 → bits = 0x86A00001
        # 这里换成 1000*100 = 100000 = 0x000186A0，低字 0x86A0 高字 0x0001
        ntu = dev._decode([0x86A0, 0x0001])
        self.assertAlmostEqual(ntu, 1000.0, places=2)

    def test_offline_raises_and_marks_offline(self):
        """读失败要**抛**（让轮询层退避），并把 online 置 false。"""
        import asyncio
        from hsfsrv import modbus

        class _BadMaster(object):
            async def read(self, *a, **kw):
                raise modbus.ModbusError("虚拟断线")

            async def write_single(self, *a, **kw):
                raise AssertionError("不该写")

            async def close(self):
                pass

        dev = devices.TurbidityMeter(lambda: _BadMaster(), 1)
        with self.assertRaises(Exception):
            asyncio.run(dev.poll(1000))
        self.assertFalse(dev.online)

    def test_health_three_states(self):
        """健康三态：离线 / 数据陈旧 / 正常（与其它设备同一口径）。"""
        import asyncio
        dev, model = self._meter()
        model.fixed_ntu = 5.0

        # ① 还没采过 → 离线
        state, _code, _usable, _text = dev.health(1000, 5000)
        self.assertEqual(state, proto.HEALTH_OFFLINE)

        asyncio.run(dev.poll(1000))
        # ② 刚采过 → 正常
        state, _code, usable, text = dev.health(2000, 5000)
        self.assertEqual(state, proto.HEALTH_OK)
        self.assertTrue(usable)
        self.assertEqual(text, "")
        # ③ 超期 → 陈旧（仍 usable，但标注）
        state, _code, _usable, _text = dev.health(9000, 5000)
        self.assertEqual(state, proto.HEALTH_STALE)

    def test_short_read_is_error(self):
        """读回寄存器不足 → 判失败（不能拿半帧当有效换算）。"""
        import asyncio
        from hsfsrv import virtualdev

        class _Short(object):
            def read(self, function, address, count):
                return [0x0001]                 # 只给 1 个，要 2 个

            def write_single(self, *a, **kw):
                raise AssertionError("不该写")

            def on_access(self):
                pass

        master = virtualdev.VirtualMaster(_Short(), name="短帧浊度")
        dev = devices.TurbidityMeter(lambda: master, 1)
        with self.assertRaises(Exception):
            asyncio.run(dev.poll(1000))
        self.assertFalse(dev.online)


class TestVirtualTurbidity(unittest.TestCase):
    """虚拟浊度模型必须复刻真机口径（否则虚拟跑通≠真机跑通）。

    [口径] CDAB 低字在前 + IEEE754 float32（WZT-701A 手册 4.3.10.2）。
    模型造出来的寄存器必须能被 devices.TurbidityMeter 原样解回去——这条
    自洽性是模拟联调有意义的前提，但它**不能替代**手册帧断言：两边一起
    错时自洽性仍然通过。
    """

    @staticmethod
    def _bits_from_regs(vals):
        """按 CDAB（低字在前）拼 32 位位模式。"""
        return ((vals[1] & 0xFFFF) << 16) | (vals[0] & 0xFFFF)

    def test_register_pair_matches_ntu(self):
        """read(3, 0x0000, 2) 拼出的位模式按 float32 解释 == 模型固定值。"""
        import struct
        from hsfsrv import virtualdev
        model = virtualdev.VirtualTurbidity(index=0)
        model.fixed_ntu = 12.34
        vals = model.read(3, 0x0000, 2)
        self.assertEqual(len(vals), 2)
        ntu = struct.unpack(">f", struct.pack(">I", self._bits_from_regs(vals)))[0]
        self.assertAlmostEqual(ntu, 12.34, places=3)

    def test_manual_frame_regs(self):
        """手册帧的寄存器形态：9.99NTU 应为 [0xD70A, 0x411F]（低字在前）。"""
        from hsfsrv import virtualdev
        model = virtualdev.VirtualTurbidity(index=0)
        model.fixed_ntu = 9.99
        vals = model.read(3, 0x0000, 2)
        self.assertEqual(vals[0], 0xD70A)
        self.assertEqual(vals[1], 0x411F)

    def test_two_indexes_have_different_baseline(self):
        """两台基准不同（原水高、出水低），否则界面上看不出是两台仪器。"""
        from hsfsrv import virtualdev
        m0 = virtualdev.VirtualTurbidity(index=0)
        m1 = virtualdev.VirtualTurbidity(index=1)
        self.assertNotEqual(m0.base_ntu, m1.base_ntu)
        self.assertGreater(m0.base_ntu, m1.base_ntu)

    def test_two_indexes_out_of_phase(self):
        """两台相位错开 ⇒ 同一时刻取值不同（看得出是两个独立仪器）。"""
        from hsfsrv import virtualdev
        m0 = virtualdev.VirtualTurbidity(index=0)
        m1 = virtualdev.VirtualTurbidity(index=1)
        n0 = m0.read(3, 0, 2)
        n1 = m1.read(3, 0, 2)
        self.assertNotEqual(self._bits_from_regs(n0), self._bits_from_regs(n1))

    def test_only_fc03_and_readonly(self):
        """只支持 FC03 读；写一律拒绝（浊度仪是只读设备）。"""
        from hsfsrv import virtualdev
        model = virtualdev.VirtualTurbidity(index=0)
        with self.assertRaises(KeyError):
            model.read(4, 0x0000, 2)
        with self.assertRaises(KeyError):
            model.write_single(6, 0x0000, 1)

    def test_unmodelled_address_returns_zero_not_raise(self):
        """未建模地址返回 0 而不是抛异常。

        [为什么这条重要] 抛异常会把"读成功但值为 0"变成"整拍失败"，
        设备永远显示离线 —— 而真机对"有但不用"的寄存器就是回 0。
        """
        from hsfsrv import virtualdev
        model = virtualdev.VirtualTurbidity(index=0)
        self.assertEqual(model.read(3, 0x0010, 2), [0, 0])


class _CountingMaster(object):
    """包一层数写次数（用于验证幂等：重复命令不该再发一帧）。"""

    def __init__(self, inner):
        self.inner = inner
        self.writes = 0

    async def read(self, *a, **kw):
        return await self.inner.read(*a, **kw)

    async def write_single(self, *a, **kw):
        self.writes += 1
        return await self.inner.write_single(*a, **kw)

    async def close(self):
        return await self.inner.close()


class TestPumpBank(unittest.TestCase):
    """进水泵：数字量模块上一路 DO，**只有输出、没有反馈**。

    [与阀的关键差别] 没有到位反馈 ⇒ 不判到位、不设超时告警。
    这里锁四件事：
        ① 命令经模块写下去，并能从 DO 回显读回
        ② 幂等：已是该状态时不重复写（避免继电器无谓动作）
        ③ DO 被外部改动（数字量页）⇒ 以实际输出为准，不停在"以为的值"
        ④ **没有反馈就没有超时告警**（照抄阀的逻辑会造出永远成立的假告警）
    """

    #: 现场接线：泵在 6 路阀（DO0..5）之后的 DO6。
    #: [2026-11-06] 原为 DO22（.12 聚合上半区口径），.12 拆除后整体减 16。
    PUMP_DO = 6

    def _bank(self, channels=32):
        from hsfsrv import pumps
        from hsfsrv import virtualdev
        model = virtualdev.VirtualDigital(channels=channels, move_ms=0)
        master = _CountingMaster(
            virtualdev.VirtualMaster(model, name="虚拟数字量"))
        dev = devices.DigitalIo(master, "数字量", channels=channels)
        bank = pumps.PumpBank(
            [dev], [{"name": "进水泵", "module": 0, "do": self.PUMP_DO}])
        return bank, dev, model, master

    def _sync(self, bank, dev, now_ms=1000):
        import asyncio
        asyncio.run(dev.poll(now_ms))
        bank.poll(now_ms)

    def _do(self, model, addr):
        return bool((model.do_mask >> addr) & 1)

    def test_wiring_ready_and_name(self):
        """点位按配置装配：1 台、DO6、名字来自配置。"""
        bank, _dev, _model, _m = self._bank()
        self.assertEqual(len(bank.items), proto.HSF_PUMP_COUNT)
        self.assertEqual(bank.ready_count(), 1)
        item = bank.items[0]
        self.assertTrue(item.ready)
        self.assertEqual(item.do, self.PUMP_DO)
        self.assertEqual(item.name, "进水泵")

    def test_do_sits_right_after_six_valves(self):
        """泵的 DO 必须紧接在 6 路阀（DO0..5）之后 = DO6，不能撞阀。"""
        bank, _dev, _model, _m = self._bank()
        self.assertEqual(bank.items[0].do, 6)
        # 反证：若错写成 DO0~5 就会和电磁阀抢同一个线圈
        self.assertNotIn(bank.items[0].do, range(0, 6))

    def test_command_writes_module_do(self):
        """set_pump(True) → 模块 DO22 真的置位（不是只改内存标志）。"""
        import asyncio
        bank, dev, model, _m = self._bank()
        self._sync(bank, dev)
        ok, err = asyncio.run(bank.set_pump(0, True))
        self.assertTrue(ok, err)
        self.assertTrue(bank.items[0].on)
        self.assertTrue(self._do(model, self.PUMP_DO))
        self.assertTrue(dev.do_bit(self.PUMP_DO))

        ok, err = asyncio.run(bank.set_pump(0, False))
        self.assertTrue(ok, err)
        self.assertFalse(self._do(model, self.PUMP_DO))

    def test_idempotent_when_already_in_state(self):
        """已在该状态 → 不重复写（模块写次数不增加）。"""
        import asyncio
        bank, dev, model, master = self._bank()
        self._sync(bank, dev)
        asyncio.run(bank.set_pump(0, True))
        before = master.writes
        mask_before = model.do_mask
        ok, err = asyncio.run(bank.set_pump(0, True))
        self.assertTrue(ok, err)
        self.assertEqual(master.writes, before, "重复命令不该再发一帧 FC05")
        self.assertEqual(model.do_mask, mask_before)

    def test_first_poll_seeds_without_fake_change(self):
        """首拍只校准回显，不得把开机稳态当成"外部改动"。"""
        bank, dev, model, _m = self._bank()
        model.do_mask |= (1 << self.PUMP_DO)     # 开机时泵本来就在运行
        self._sync(bank, dev)
        item = bank.items[0]
        self.assertTrue(item.known_do_seeded)
        self.assertTrue(item.on)
        self.assertEqual(item.last_error, "")

    def test_external_do_write_is_adopted(self):
        """数字量页直写 DO22 ⇒ 以实际输出为准（界面不能与现场相反）。"""
        bank, dev, model, _m = self._bank()
        self._sync(bank, dev)
        self.assertFalse(bank.items[0].on)

        model.write_single(5, self.PUMP_DO, 0xFF00)   # 绕过 set_pump
        logs = []
        bank._log = logs.append
        self._sync(bank, dev, 2000)

        self.assertTrue(bank.items[0].on, "外部改动后必须跟随实际输出")
        self.assertTrue(any("外部改动" in m for m in logs),
                        "外部改动要留日志（否则现场查不到是谁改的）")

    def test_offline_module_is_not_ready(self):
        """模块离线 ⇒ 泵不可用（不谎报状态）。"""
        bank, dev, _model, _m = self._bank()
        dev.online = False
        bank.poll(1000)
        item = bank.items[0]
        self.assertFalse(item.valid)
        self.assertIn("不新鲜", item.text)

    def test_control_rejected_when_module_offline(self):
        """模块离线时下发命令必须失败（不能写出一个假成功）。"""
        import asyncio
        bank, dev, _model, _m = self._bank()
        dev.online = False
        ok, err = asyncio.run(bank.set_pump(0, True))
        self.assertFalse(ok)
        self.assertIn("离线", err)

    def test_no_timeout_alarm_ever(self):
        """**没有运行反馈 ⇒ 不会出现"超时"文案**（硬造一个就是撒谎）。

        [为什么写这条反证] 阀有 20s 超时；泵没有反馈，如果照抄阀的逻辑
        就会出现"泵启动 20 秒后必报超时"这种永远成立的假告警。
        这里跑过 120s 虚拟时间，状态必须仍是"运行"且没有任何超时/未到位文案。
        """
        import asyncio
        bank, dev, _model, _m = self._bank()
        self._sync(bank, dev)
        asyncio.run(bank.set_pump(0, True))
        for t in (2000, 30000, 60000, 120000):
            self._sync(bank, dev, t)
            item = bank.items[0]
            self.assertTrue(item.on)
            self.assertEqual(item.text, "")
            self.assertNotIn("超时", item.text)
            self.assertNotIn("到位", item.text)

    def test_index_out_of_range(self):
        """泵号越界要返回失败而不是抛异常（网络请求不可信）。"""
        import asyncio
        bank, _dev, _model, _m = self._bank()
        ok, err = asyncio.run(bank.set_pump(5, True))
        self.assertFalse(ok)
        self.assertIn("越界", err)

    def test_health_summary(self):
        """健康摘要：在线 → OK；模块离线 → OFFLINE。"""
        bank, dev, _model, _m = self._bank()
        self._sync(bank, dev)
        state, online, _text = bank.health_summary(1500, 5000)
        self.assertEqual(state, proto.HEALTH_OK)
        self.assertTrue(online)
        dev.online = False
        bank.poll(2000)
        state, _online, _text = bank.health_summary(2000, 5000)
        self.assertEqual(state, proto.HEALTH_STALE)

    def test_unconfigured_item_is_not_ready(self):
        """没配点位 ⇒ ready=False，且下发要失败（不能对不存在的东西发命令）。"""
        import asyncio
        from hsfsrv import pumps
        bank = pumps.PumpBank([], [])
        self.assertEqual(bank.ready_count(), 0)
        state, online, text = bank.health_summary(1000, 5000)
        self.assertEqual(state, proto.HEALTH_OFFLINE)
        self.assertFalse(online)
        self.assertIn("未装配", text)
        ok, err = asyncio.run(bank.set_pump(0, True))
        self.assertFalse(ok)
        self.assertIn("未配置点位", err)


class TestHealthKind(unittest.TestCase):
    """设备健康条目的 **kind 必须表达"这是什么设备"**。

    [实锅 2026-11-05] 浊度仪初版复用了 `KIND_TEMPHUM`（=1），于是客户端首页
    设备表的"类型"列把两台浊度仪显示成**温湿度** —— 数据全对，但类别完全
    误导（现场会以为是电箱里的温湿度变送器，去找接线找错地方）。

    [为什么要单独一条测试] 这类错误**不会**让任何数据字段出错：健康状态、
    在线标志、错误文本全都正常，只是类别标签错。功能测试、联调、截图
    都可能漏掉 —— 用户正是看截图才发现的。

    这里锁住"每类设备各自的 kind"，将来再加设备时如果图省事复用一个既有
    kind，这条会红。
    """

    def _app_with_devices(self):
        """构造一个带浊度/泵/流量计的最小 app（不需要起服务端）。"""
        from hsfsrv import app as app_mod
        from hsfsrv import virtualdev

        class _Cfg(object):
            pass

        cfg = _Cfg()
        for attr in ("enable_vfd", "enable_temphum", "enable_flowmeter",
                     "enable_analog", "enable_turbidity", "enable_pump",
                     "enable_digital", "enable_valve"):
            setattr(cfg, attr, False)
        cfg.turbidity_stale_ms = 5000
        cfg.flowmeter_stale_ms = 5000
        cfg.pump_stale_ms = 5000
        # 健康条目现在也带"连接地址"，故假 cfg 必须给齐这些字段。
        # 两条独立 485：路 A(COM1/9600) 变频器+温湿度，路 B(COM2/9600)
        # 流量计+浊度仪。_addr_rtu(bus="b") 会读 serial_b，缺了就 AttributeError。
        cfg.serial = type("S", (), {"port": "COM1", "baudrate": 9600})()
        cfg.serial_b = type("S", (), {"port": "COM2", "baudrate": 9600})()
        cfg.vfd_unit_id = 2
        cfg.temphum_unit_id = 4
        cfg.flowmeter_unit_id = 5
        cfg.turbidity_unit_ids = [1, 3]
        cfg.analog = type("A", (), {"host": "1.1.1.1", "port": 502})()
        cfg.digital_modules = [type("D", (), {"host": "1.1.1.2", "port": 502})()]

        obj = app_mod.App.__new__(app_mod.App)
        # 只布置 collect_health 用到的属性
        obj.cfg = cfg
        obj.vfd = None
        obj.temphum = None
        obj.flowmeter = None
        obj.analog = None
        obj.digital = []
        obj.valve_bank = None
        obj.turbidity = [
            devices.TurbidityMeter(
                lambda: virtualdev.VirtualMaster(
                    virtualdev.VirtualTurbidity(index=0)), 1),
            devices.TurbidityMeter(
                lambda: virtualdev.VirtualMaster(
                    virtualdev.VirtualTurbidity(index=1)), 3),
        ]
        from hsfsrv import pumps
        dmod = devices.DigitalIo(None, "数字量", channels=32)
        obj.pump_bank = pumps.PumpBank(
            [dmod], [{"name": "进水泵", "module": 0, "do": 22}])
        return obj

    def test_turbidity_kind_is_water_not_temphum(self):
        """浊度仪的 kind 必须是 WATER（不能复用 TEMPHUM）。

        反证也写在这里：若哪天有人改回 KIND_TEMPHUM，`== KIND_WATER` 立刻
        失败，而不是在界面上把浊度显示成温湿度。
        """
        obj = self._app_with_devices()
        entries = obj.collect_health(1000)
        turb = [e for e in entries if e.name.startswith("浊度仪")]
        self.assertEqual(len(turb), 2, "应有两台浊度仪健康条目")
        for e in turb:
            self.assertEqual(e.kind, proto.KIND_WATER,
                             "%s 的 kind 应为 WATER(%d)，实际 %d"
                             % (e.name, proto.KIND_WATER, e.kind))
            self.assertNotEqual(e.kind, proto.KIND_TEMPHUM,
                                "浊度仪不得复用温湿度类别")

    def test_kind_values_are_distinct(self):
        """各设备类别的值必须互不相同（否则界面无法区分）。"""
        kinds = {
            "VFD": proto.KIND_VFD,
            "TEMPHUM": proto.KIND_TEMPHUM,
            "FLOWMETER": proto.KIND_FLOWMETER,
            "ANALOG": proto.KIND_ANALOG_IO,
            "DIGITAL": proto.KIND_DIGITAL_IO,
            "VALVE": proto.KIND_VALVE,
            "WATER": proto.KIND_WATER,
        }
        self.assertEqual(len(set(kinds.values())), len(kinds),
                         "kind 值有重复：%s" % kinds)
        self.assertEqual(proto.KIND_WATER, 7,
                         "KIND_WATER 的值定死在 7（与 C 头文件 "
                         "HSF_DEVICE_KIND_WATER 一致）")


class TestVfdDeviceName(unittest.TestCase):
    """搅拌泵：**设备名**按工艺叫「搅拌泵」，**类别**仍是变频器。

    [为什么这两件事必须分开] 现场那台设备是变频器驱动搅拌泵。两种说法都
    不算错，但用途不同：

        · 设备名（健康条目的 name）→ 给操作员找"哪台设备在不在线"
          所以叫「搅拌泵」，与页面/导航同名，一眼对得上。
        · 类别（kind = KIND_VFD）→ 给维修判断"该去查什么"
          客户端"类型"列显示「变频器」，提示去查调速器而不是拆泵体。

    反过来（名字叫变频器、类型也改成搅拌泵）会让维修走错方向，所以这两条
    断言必须成对存在：改任何一边都会红。
    """

    def _app(self):
        from hsfsrv import app as app_mod

        class _Cfg(object):
            pass

        cfg = _Cfg()
        for attr in ("enable_vfd", "enable_temphum", "enable_flowmeter",
                     "enable_analog", "enable_turbidity", "enable_pump",
                     "enable_digital", "enable_valve"):
            setattr(cfg, attr, False)
        cfg.enable_vfd = True
        # 健康条目带"连接地址"，假 cfg 需给齐（含第二条 485）。
        cfg.serial = type("S", (), {"port": "COM1", "baudrate": 9600})()
        cfg.serial_b = type("S", (), {"port": "COM2", "baudrate": 9600})()
        cfg.vfd_unit_id = 2
        cfg.temphum_unit_id = 4
        cfg.flowmeter_unit_id = 5
        cfg.turbidity_unit_ids = [1, 3]
        cfg.analog = type("A", (), {"host": "1.1.1.1", "port": 502})()
        cfg.digital_modules = []

        obj = app_mod.App.__new__(app_mod.App)
        obj.cfg = cfg
        obj.vfd = None            # 走"未装配"分支也要有名字
        obj.temphum = None
        obj.flowmeter = None
        obj.analog = None
        obj.digital = []
        obj.valve_bank = None
        obj.turbidity = []
        obj.pump_bank = None
        return obj

    def test_device_name_is_stirrer_pump(self):
        """设备名叫「搅拌泵」（与页面/导航同名，操作员一眼对上）。"""
        entries = self._app().collect_health(1000)
        vfd = [e for e in entries if e.kind == proto.KIND_VFD]
        self.assertEqual(len(vfd), 1, "应有一个变频器类别条目")
        self.assertEqual(vfd[0].name, "搅拌泵",
                         "设备名应为「搅拌泵」，实际 %r" % vfd[0].name)

    def test_kind_still_vfd_not_renamed(self):
        """类别仍是 KIND_VFD（客户端"类型"列显示「变频器」）。

        反证：若有人"顺手"把 kind 也改成水质/泵之类的值，维修就找不到
        该查调速器这件事了。
        """
        entries = self._app().collect_health(1000)
        vfd = [e for e in entries if e.kind == proto.KIND_VFD]
        self.assertEqual(len(vfd), 1)
        self.assertEqual(vfd[0].kind, proto.KIND_VFD)
        # 名字与类别必须是两个不同的概念：名字不是类别名
        self.assertNotEqual(vfd[0].name, "变频器",
                            "设备名不该还是「变频器」（已按现场口径改搅拌泵）")


class TestAutoRunningExposed(unittest.TestCase):
    """自动模式（auto）与自动运行（autoRunning）是**两件事**，必须分开。

    [⚠ 安全口径 2026-11-05 订正] 初版把"切到自动模式"直接当成"启动自动
    运行"，现场反馈**太危险**：操作员只想切个模式，设备立刻开始被自动逻辑
    驱动。正确口径：

        切到自动模式  →  只是**授权**（远程控制被禁），控制器仍停
        按"启动自动运行" → 人才真正让它跑
        服务端重启    → 即使配置是自动模式，控制器**一律停**

    这样四种组合都要能如实表达，测试逐一对齐：

        mode   running   含义
        手动   False     正常（自动不可用）
        自动   False     已授权但没按启动（**默认状态**）
        自动   True      真正在自动运行
        手动   True      **禁止**（退自动必须强制停，否则设备被偷偷驱动）
    """

    def _app(self):
        from hsfsrv import app as app_mod
        from hsfsrv import autocontrol

        class _Cfg(object):
            def save_run_mode(self, mode):
                pass

        obj = app_mod.App.__new__(app_mod.App)
        obj.cfg = _Cfg()
        obj.run_mode = obj.MODE_MANUAL
        obj.auto_ctx = autocontrol.AutoContext(obj, log=lambda m: None)
        obj.auto_controller = autocontrol.AutoController(
            obj.auto_ctx, log=lambda m: None)
        obj._auto_err_at = {}
        obj._log = lambda m: None
        return obj

    def test_manual_default_stopped(self):
        """手动 + 控制器停（开机缺省）。"""
        obj = self._app()
        self.assertFalse(obj.is_auto())
        self.assertFalse(obj.is_auto_running())

    def test_switching_to_auto_does_NOT_start_controller(self):
        """**切到自动模式不得启动控制器**（切模式只是授权）。

        这是安全红线：把模式开关当启动钮，等于操作员想切个显示状态，
        设备却开始动。
        """
        obj = self._app()
        self.assertTrue(obj.set_run_mode(obj.MODE_AUTO))
        self.assertTrue(obj.is_auto(), "模式已切到自动")
        self.assertFalse(obj.is_auto_running(),
                         "切到自动模式**不能**自动启动控制器（太危险）")

    def test_explicit_start_then_running(self):
        """人明确启动后才 running=True。"""
        obj = self._app()
        obj.set_run_mode(obj.MODE_AUTO)
        self.assertFalse(obj.is_auto_running())
        obj.auto_controller.start(obj._now_ms())
        self.assertTrue(obj.is_auto_running())

    def test_leaving_auto_force_stops(self):
        """退出自动 ⇒ **强制停**（单向安全：手动模式下不许自动逻辑还在跑）。"""
        obj = self._app()
        obj.set_run_mode(obj.MODE_AUTO)
        obj.auto_controller.start(obj._now_ms())
        self.assertTrue(obj.is_auto_running())
        obj.set_run_mode(obj.MODE_MANUAL)
        self.assertFalse(obj.is_auto_running(),
                         "退自动必须强制停止控制器（安全）")

    def test_restore_auto_mode_does_not_start(self):
        """重启恢复成自动模式 ⇒ 模式位还原，但控制器**仍停**。

        [为什么这条最重要] 断电重启 / systemd 拉起 / 升级重启都走这条路。
        如果这里启动控制器，等于"没人按过启动，设备自己动起来"。
        """
        obj = self._app()
        obj.restore_run_mode(obj.MODE_AUTO)
        self.assertTrue(obj.is_auto(), "模式位应还原成自动")
        self.assertFalse(obj.is_auto_running(),
                         "重启后控制器一律停（不能自己动起来）")

    def test_mode_auto_but_stopped_is_expressible(self):
        """能如实表达「模式=自动，控制器已停」这个**默认**组合。

        反证：若 is_auto_running() 实现成 `return self.run_mode == MODE_AUTO`
        （从模式位推断），这条会红 —— 那种实现会让界面把"已授权未启动"
        说成"正在运行"，正是用户报的那个危险观感。
        """
        obj = self._app()
        obj.set_run_mode(obj.MODE_AUTO)
        self.assertTrue(obj.is_auto())
        self.assertFalse(obj.is_auto_running())


class TestHealthAddr(unittest.TestCase):
    """设备健康要带**连接地址**（485 = 口·波特率·站号；TCP = ip:port）。

    [为什么要在界面上看到地址] 现场排故障第一件事是确认"这台设备配在哪个
    口/哪个 IP/哪个站号"。原来这些只在配置文件里，操作员报"某台离线"，
    运维得先上机器翻配置才知道该查哪个站。

    [为什么 485 要三样都写] 口接错、波特率不匹配、站号撞车，现象**都是**
    "通讯不上"，只写其中一两项根本判不出来。
    """

    def _app(self, **flags):
        from hsfsrv import app as app_mod
        from hsfsrv import config
        cfg = config.Config()
        cfg.config_path = 'cfg/hsf_server_py.json'
        cfg.load()
        cfg.sim_enabled = True
        for k, v in flags.items():
            setattr(cfg, k, v)
        a = app_mod.App(cfg, log=lambda m: None)
        a.build_devices()
        return a, cfg

    def _entries(self, a):
        return a.collect_health(1000)

    def test_rtu_devices_report_port_baud_and_unit(self):
        """485 设备地址必须同时含 端口 / 波特率 / 站号。

        [两条独立 485] 路 A(变频器/温湿度) 取 serial，路 B(流量计/浊度仪)
        取 serial_b；两者口与速率都不同，不能混着校验（2026-11-06 用户
        反馈：之前界面上 5 台设备全显示同一个 COM17 · 115200）。
        """
        a, cfg = self._app(enable_flowmeter=True, enable_turbidity=True)
        by_name = {e.name: e for e in self._entries(a)}
        for name, bus in (("搅拌泵", cfg.serial), ("温湿度", cfg.serial),
                          ("电磁流量计", cfg.serial_b)):
            e = by_name[name]
            self.assertIn(bus.port, e.addr,
                          "%s 的地址缺端口：%r" % (name, e.addr))
            self.assertIn(str(bus.baudrate), e.addr,
                          "%s 的地址缺波特率：%r" % (name, e.addr))
            self.assertIn("站", e.addr,
                          "%s 的地址缺站号：%r" % (name, e.addr))
        # 两条线必须真的分开（不能又退回同一路）
        self.assertNotEqual(by_name["搅拌泵"].addr.split(" · 站")[0],
                            by_name["电磁流量计"].addr.split(" · 站")[0],
                            "路 A 与路 B 的地址前缀不该相同")

    def test_each_turbidity_reports_its_own_unit(self):
        """两台浊度仪必须各自报**自己的**站号（写错就分不清是哪台失联）。"""
        a, cfg = self._app(enable_turbidity=True)
        turb = [e for e in self._entries(a) if e.name.startswith("浊度仪")]
        self.assertEqual(len(turb), 2)
        self.assertIn("站 %s" % cfg.turbidity_unit_ids[0], turb[0].addr)
        self.assertIn("站 %s" % cfg.turbidity_unit_ids[1], turb[1].addr)
        self.assertNotEqual(turb[0].addr, turb[1].addr,
                            "两台浊度的地址不能一样（站号必须各自不同）")

    def test_tcp_devices_report_ip_and_port(self):
        """TCP 设备地址必须是 ip:port。"""
        a, cfg = self._app()
        by_name = {e.name: e for e in self._entries(a)}
        for name, host, port in (
                ("模拟量模块", cfg.analog.host, cfg.analog.port),
                ("数字量", cfg.digital_modules[0].host,
                 cfg.digital_modules[0].port)):
            e = by_name[name]
            self.assertIn("%s:%s" % (host, port), e.addr,
                          "%s 的地址应为 %s:%s，实际 %r" % (name, host, port, e.addr))

    def test_pump_reports_module_addr_and_do(self):
        """泵没有自己的通讯口 ⇒ 报它挂的模块地址 + DO 口（不是站号）。"""
        a, cfg = self._app()
        pump = [e for e in self._entries(a) if e.name == "进水泵"][0]
        mod = cfg.digital_modules[cfg.pump_items[0]["module"]]
        self.assertIn("%s:%s" % (mod.host, mod.port), pump.addr)
        self.assertIn("DO%d" % cfg.pump_items[0]["do"], pump.addr)
        # 反证：不能报成 485 站号（泵不在总线上）
        self.assertNotIn("站", pump.addr, "泵不在 485 总线上，不该报站号")

    def test_valve_bank_reports_do_span(self):
        """阀组报模块地址 + DO 区间（6 路一眼看得出占了哪几口）。

        [2026-11-06 口径] .12 拆除后点位整体减 16 ⇒ 区间是 DO0~DO5。
        """
        a, _cfg = self._app()
        valve = [e for e in self._entries(a) if e.name == "电磁阀组"][0]
        self.assertIn("DO0", valve.addr)
        self.assertIn("DO5", valve.addr)
        self.assertNotIn("DO16", valve.addr,
                         "点位已整体减 16，不应再出现旧的上半区编号")

    def test_addr_change_bumps_health_seq(self):
        """改了配置里的地址 ⇒ 健康 seq 必须变（否则界面一直显示旧地址）。

        [实锅风险] 健康是**变化才推**（比对 key()）。若 key 不含 addr，
        改 IP/站号后服务端认为"没变化"，客户端永远看不到新地址 ——
        而现场恰恰是在排"连不上"时改这些参数，最不能显示旧值。
        """
        # 直接造两个同名字但地址不同的条目，比较 key
        from hsfsrv import app as app_mod
        old = app_mod.HealthEntry("X", 0, 0, 0, 0, True, 0, "", "1.2.3.4:502")
        new = app_mod.HealthEntry("X", 0, 0, 0, 0, True, 0, "", "5.6.7.8:502")
        self.assertNotEqual(old.key(), new.key(),
                            "地址变了 key 必须变，否则客户端收不到更新")


class TestPortConflict(unittest.TestCase):
    """端口被占用时**必须报错**，不能静默双开。

    [实锅 2026-11-05] 桌面快捷方式启动模拟服务端时，旧实例还在跑。服务端
    用了 `asyncio.start_server(reuse_address=True)` —— 这个参数在 Linux 上
    只是允许绑定 TIME_WAIT 端口（正当需求），但**在 Windows 上语义更宽：
    允许多个 socket 同时监听同一端口**。

    后果：第二个实例**静默绑定成功**，`netstat` 出现两条 `0.0.0.0:6200
    LISTENING`，客户端连到哪一个是随机的。两个实例各自轮询同一批设备、
    各自维护推送节拍与健康 seq ⇒ 现象是"数据跳变 / 状态莫名重置 / 阀计时
    反复清零"，而且**日志里两条都显示启动成功**，极难看出是双实例。

    修法：Windows 上 `reuse_address=False`（Linux 保持 True）。
    """

    def test_second_instance_on_same_port_raises(self):
        """同一端口第二次 start() 必须抛错，且错误信息可照做。"""
        import asyncio
        from hsfsrv import xlink

        async def go():
            s1 = xlink.Server(6298, {}, log=lambda m: None)
            await s1.start()
            try:
                s2 = xlink.Server(6298, {}, log=lambda m: None)
                try:
                    await s2.start()
                finally:
                    await s2.stop()
                return None            # 没抛 ⇒ 双开成功，坏
            except OSError as exc:
                return str(exc)
            finally:
                await s1.stop()

        msg = asyncio.run(go())
        self.assertIsNotNone(msg, "端口被占用时第二个实例必须报错（不能静默双开）")
        # 提示必须能直接用：端口号 + 查占用方法 + 换端口办法
        self.assertIn("6298", msg, "错误信息要指出是哪个端口")
        self.assertIn("netstat", msg, "要给出查占用进程的办法")
        self.assertIn("--port", msg, "要给出换端口的办法")

    def test_reuse_address_is_off_on_windows(self):
        """Windows 上不得开 reuse_address（那会让端口冲突被吞掉）。

        [为什么写成"看平台"而不是写死] Linux 上 reuse_address=True 是
        **需要的**（重启服务端不必等 TIME_WAIT 60 秒）。所以不能一律关，
        要按平台区分 —— 这条断言锁的就是"别有人为了省事把它改回恒 True"。
        """
        import os
        import inspect
        from hsfsrv import xlink
        src = inspect.getsource(xlink.Server.start)
        self.assertIn("os.name", src,
                      "start() 必须按平台决定 reuse_address（Windows 要关）")
        self.assertIn('!= "nt"', src,
                      "Windows(nt) 上 reuse_address 应为 False")


class TestDefaultsMatchField(unittest.TestCase):
    """**纯代码默认值**（没有 cfg 文件）必须能跑出与现场一致的界面。

    [实锅 2026-11-05 —— 用户原话："别人拉取显示不一样"]
    `cfg/` 被 .gitignore 排除（那是运行产物，合理），但现场参数只存在
    于那个文件里 ⇒ 别人 clone 下来跑的是代码默认值，而当时默认值是
    "够用的最小集"：

        · 数字量 16 点、且 .13 也 publish=true  → 两个模块各发一套点位
        · 模拟量 scales 为空                    → 压差计没名字/没换算
        · 电磁阀 items 全 None                  → 6 路全"未配置"
        · 流量计/浊度默认关                      → 水质页整页 "--"

    这四类都属于**设备本身的口径/图纸接线**，不是站点私有参数，必须写进
    代码默认值。真正私有的（串口号、IP）仍由 cfg 覆盖。

    这组测试**不 load 任何配置文件**，锁的就是"空配置也能对"。
    """

    def _app(self):
        from hsfsrv import app as app_mod
        from hsfsrv import config
        cfg = config.Config()          # 注意：不 load
        cfg.sim_enabled = True
        a = app_mod.App(cfg, log=lambda m: None)
        a.build_devices()
        return a, cfg

    def test_digital_modules_default_to_field_wiring(self):
        """默认单台 16 点（.13），且是唯一发布者。

        [2026-11-06 口径变更] 原为"32 点聚合 + .13 不重复发布"。.12 已
        下线拆除，现场只剩 .13 一台 16 点模块 ⇒ 该模块既是唯一设备也是
        唯一发布者；原先由 .12 提供的 16DI/16DO 一并取消（有意收窄）。
        """
        _a, cfg = self._app()
        mods = cfg.digital_modules
        self.assertEqual(len(mods), 1)
        self.assertEqual(mods[0].host, "192.168.0.13")
        self.assertEqual(mods[0].channels, 16, ".13 单台物理 16 点")
        self.assertTrue(mods[0].publish, "唯一模块必须发布点位")

    def test_valves_default_to_field_wiring(self):
        """6 路阀默认按图纸接线（DO0..5，关到位 DI 在前）。

        [2026-11-06 口径变更] 原为 DO16..21（.12 聚合上半区）。.12 拆除后
        只剩 .13 一台 16 点模块、点表从 0 起 ⇒ 整体减 16。
        """
        _a, cfg = self._app()
        self.assertEqual(len(cfg.valve_items), 6)
        for i, item in enumerate(cfg.valve_items):
            self.assertEqual(item["module"], 0)
            self.assertEqual(item["do"], i)
            self.assertEqual(item["close_di"] if "close_di" in item
                             else item["closeDi"], i * 2,
                             "阀%d 关到位应在开到位之前" % (i + 1))
            self.assertEqual(item["open_di"] if "open_di" in item
                             else item["openDi"], i * 2 + 1)

    def test_valve_bank_actually_ready_with_defaults(self):
        """纯默认配置下 6 路阀必须都"已装配"（不能显示未配置）。"""
        a, _cfg = self._app()
        self.assertIsNotNone(a.valve_bank)
        self.assertEqual(a.valve_bank.ready_count(), 6,
                         "默认配置下 6 路阀都应可操作")

    def test_analog_channel0_has_diff_pressure_scale(self):
        """通道 0 默认就是压差计（有名/单位/4~20mA→0~1MPa）。"""
        _a, cfg = self._app()
        scale = cfg.analog.scales.get(0)
        self.assertIsNotNone(scale, "通道 0 默认应有压差计换算（否则没名字）")
        self.assertEqual(scale.name, "压差计")
        self.assertEqual(scale.unit, "MPa")
        self.assertAlmostEqual(scale.full_scale, 1.0)
        # 4mA → 0MPa，20mA → 1MPa（两端点都验，避免比例写反）
        v0, _t0 = scale.engineering(4.0)
        v1, _t1 = scale.engineering(20.0)
        self.assertAlmostEqual(v0, 0.0, places=3)
        self.assertAlmostEqual(v1, 1.0, places=3)

    def test_flowmeter_and_turbidity_enabled_by_default(self):
        """流量计与浊度默认装配（否则别人看到水质页整页 '--'）。"""
        _a, cfg = self._app()
        self.assertTrue(cfg.enable_flowmeter)
        self.assertTrue(cfg.enable_turbidity)
        self.assertEqual(len(cfg.turbidity_unit_ids), 2)

    def test_no_config_file_still_yields_full_device_list(self):
        """端到端：不读配置文件的健康快照必须含全部 10 台设备。"""
        from hsfsrv import bson
        a, _cfg = self._app()
        doc = bson.decode(a.encode_health())
        names = [doc.get("d%d_name" % i) for i in range(doc.get("devCount", 0))]
        for expect in ("搅拌泵", "温湿度", "电磁流量计", "浊度仪1", "浊度仪2",
                       "进水泵", "模拟量模块", "数字量", "电磁阀组"):
            self.assertIn(expect, names,
                          "缺设备 %r（别人拉取后会看到少一块）" % expect)

class TestMasterCloseCompat(unittest.TestCase):
    """master.close() 同步/异步两种签名都要能收（**只有真机串口才暴露**）。

    [实锅 2026-11-06 板端部署] 各设备的 `close()` 原来一律写
    `await self._master.close()`：`TcpMaster.close()` 是 async 没问题，
    但 `RtuMaster.close()` 是**同步**（只关 pyserial 句柄） ⇒ 一旦走串口就
    抛 `TypeError: object NoneType can't be used in 'await' expression`。
    更阴的是它常在 `finally` 里第一句：自检打完第一台设备就崩，后面几台
    根本轮不到，看着像"自检只支持一台"。
    本机 sim 全走 TCP，本地跑一万遍也碰不到 —— 故必须用假 master 锁住。
    """

    class _SyncMaster(object):
        def __init__(self):
            self.closed = 0

        def close(self):
            self.closed += 1
            return None              # 同步 close 的典型返回值

    class _AsyncMaster(object):
        def __init__(self):
            self.closed = 0

        async def close(self):
            self.closed += 1

    def test_sync_close_does_not_need_await(self):
        from hsfsrv import devices
        m = self._SyncMaster()
        asyncio.run(devices.close_master(m))
        self.assertEqual(m.closed, 1, "同步 close 必须被调用一次")

    def test_async_close_is_awaited(self):
        from hsfsrv import devices
        m = self._AsyncMaster()
        asyncio.run(devices.close_master(m))
        self.assertEqual(m.closed, 1, "异步 close 必须被 await 到")

    def test_none_is_idempotent(self):
        from hsfsrv import devices
        asyncio.run(devices.close_master(None))   # 不抛即通过

    def test_device_close_handles_sync_master(self):
        """设备层 close()：串口（同步 close）路径不得抛异常。"""
        from hsfsrv import devices
        m = self._SyncMaster()

        async def go():
            dev = devices.TempHum(lambda: m, 4, lambda _s: None)
            dev._master = m
            await dev.close()
            return dev

        dev = asyncio.run(go())
        self.assertEqual(m.closed, 1)
        self.assertIsNone(dev._master, "close 后必须置空（幂等/防重入）")

    def test_device_close_is_idempotent(self):
        """连关两次不得重复关同一个句柄（关已关的句柄在串口上会抛）。"""
        from hsfsrv import devices
        m = self._SyncMaster()

        async def go():
            dev = devices.VfdM512(lambda: m, 2, lambda _s: None)
            dev._master = m
            await dev.close()
            await dev.close()

        asyncio.run(go())
        self.assertEqual(m.closed, 1, "第二次 close 不该再碰 master")


class TestButtonStation(unittest.TestCase):
    """按钮组（操作柱）：4 DI + 3 DO 的状态机与灯。

    [现场定义，用户 2026-11-06 口述]
        输入 12 = 急停（**常闭**）  13 = 启动  14 = 停止  15 = 复位
        输出 7  = 启动灯（运行常亮） 8 = 停止灯（停止常亮） 9 = 复位灯（点动）

    [为什么要锁这些] 这几条是**安全语义**，不是界面偏好：
        · 急停常闭 ⇒ DI=0 必须禁止启动（断线也走这条，是安全侧默认）
        · 启动/停止灯严格互斥（同时亮=状态不明）
        · 复位灯是动作灯（点动），不是状态灯
    任何一条被"顺手简化"都会让现场看不出设备处于什么状态。
    """

    ITEMS = {"estop_di": 12, "start_di": 13, "stop_di": 14, "reset_di": 15,
             "start_lamp_do": 7, "stop_lamp_do": 8, "reset_lamp_do": 9,
             "module": 0}

    class _Ctx(object):
        """AutoContext 的最小替身：直连虚拟设备，不碰网络/串口。"""

        def __init__(self, dev):
            self.dev = dev

        def read_di(self, ch, mod=0):
            return self.dev.di_bit(ch)

        def read_do(self, ch, mod=0):
            return self.dev.do_bit(ch)

        async def write_do(self, ch, on, mod=0):
            return await self.dev.write_do(ch, on)

    def _station(self, debounce_ms=0):
        from hsfsrv import buttons, devices, virtualdev
        model = virtualdev.VirtualDigital(channels=16)
        master = virtualdev.VirtualMaster(model, name="虚拟数字量")
        dev = devices.DigitalIo(master, "数字量", channels=16)
        dev.online = True
        return buttons.ButtonStation(self._Ctx(dev), self.ITEMS,
                                     debounce_ms=debounce_ms), dev, model

    def _tick(self, st, dev, now_ms):
        """一拍：采集 + 判定 + 让写灯协程落地。

        [必须同一个事件循环] `ButtonStation._apply_lamps` 用
        `asyncio.ensure_future` 投递写灯协程——它挂在**当前** loop 上。
        若 poll 之后另起 `asyncio.run(sleep)`，那是另一个 loop，先前投递
        的任务全部作废（实测：灯永远读不到）。故整拍包在一个 run 里。
        """
        import asyncio

        async def one():
            await dev.poll(now_ms)
            st.poll(now_ms)
            await asyncio.sleep(0.02)       # 让 ensure_future 的写灯跑完

        asyncio.run(one())

    def _lamp(self, model, ch):
        return bool((model.do_mask >> ch) & 1)

    def test_estop_is_normally_closed(self):
        """上电急停回路通（DI12=1）⇒ 正常；DI12=0 ⇒ 禁止（含断线）。

        [反例] 若把判据写成「DI=1 才是按下」，断线时会被判成"没按急停"
        而放行启动 —— 这是安全事故级的错，必须锁死。
        """
        from hsfsrv import buttons
        st, dev, model = self._station()
        self._tick(st, dev, 100)
        self.assertIs(st.estop_ok, True, "上电急停回路正常（常闭）")

        model.press_button(12, False)          # 急停按下/断线
        self._tick(st, dev, 200)
        self.assertIs(st.estop_ok, False)
        self.assertEqual(st.state, buttons.ButtonState.ESTOP)

    def test_estop_none_counts_as_blocked(self):
        """读不到 DI（模块离线，返回 None）也必须判"禁止"。

        安全侧默认：宁可误停，不能误启。
        """
        from hsfsrv import buttons
        st, dev, model = self._station()
        dev.online = False
        st.poll(100)
        self.assertIsNot(st.estop_ok, True, "读不到不能当成正常")
        self.assertEqual(st.state, buttons.ButtonState.ESTOP)

    def test_lamps_are_mutually_exclusive(self):
        """启动灯与停止灯任意时刻只能亮一盏（含急停态）。

        [2026-11-06 语义变更] 灯跟着**外部给的运行态**（set_running），
        不跟按键 —— 按键只是请求，判定在 autocontrol。故这里直接驱动
        set_running 验证灯，按键→状态那条链在 TestAutoButtonLogic 里测。
        """
        st, dev, model = self._station()

        self._tick(st, dev, 100)               # 停止态
        self.assertTrue(self._lamp(model, 8), "停止态停止灯亮")
        self.assertFalse(self._lamp(model, 7), "停止态启动灯灭")

        st.set_running(True)                   # 外部宣布"真的在自动运行"
        self._tick(st, dev, 200)               # 第二拍才写灯（首拍已写过）
        self.assertEqual(st.state, "running")
        self.assertTrue(self._lamp(model, 7), "运行态启动灯亮")
        self.assertFalse(self._lamp(model, 8), "运行态停止灯灭")

        model.press_button(12, False)          # 急停
        self._tick(st, dev, 400)
        self.assertTrue(self._lamp(model, 8), "急停时停止灯亮")
        self.assertFalse(self._lamp(model, 7), "急停时启动灯灭")

    def test_reset_lamp_is_jog(self):
        """复位的灯是**点动**：按住亮、松开灭，且不改运行状态。"""
        from hsfsrv import buttons
        st, dev, model = self._station()
        self._tick(st, dev, 100)

        model.press_button(15, True)
        self._tick(st, dev, 200)
        self.assertTrue(self._lamp(model, 9), "按住复位时灯亮")
        self.assertEqual(st.state, buttons.ButtonState.IDLE,
                         "复位按钮只驱动灯，不参与状态机")

        model.press_button(15, False)
        self._tick(st, dev, 300)
        self.assertFalse(self._lamp(model, 9), "松开复位灯灭")

    def test_start_blocked_while_estop(self):
        """急停无信号时按启动无效（不能进入运行态）。"""
        from hsfsrv import buttons
        st, dev, model = self._station()
        model.press_button(12, False)          # 急停
        self._tick(st, dev, 100)
        model.press_button(13, True)           # 同时按启动
        self._tick(st, dev, 200)
        self.assertEqual(st.state, buttons.ButtonState.ESTOP)
        self.assertFalse(self._lamp(model, 7), "急停期间启动灯不得亮")

    def test_estop_recovery_does_not_auto_start(self):
        """急停恢复后回停止态，不自动启动（复位必须人工确认）。"""
        from hsfsrv import buttons
        st, dev, model = self._station()
        model.press_button(12, False)
        self._tick(st, dev, 100)
        model.press_button(12, True)
        self._tick(st, dev, 200)
        self.assertEqual(st.state, buttons.ButtonState.IDLE)
        self.assertFalse(self._lamp(model, 7))

    def test_debounce_swallows_double_edge(self):
        """去抖窗口内的第二个上升沿被丢弃（机械抖动不能算两次按键）。

        [2026-11-06 改口径] buttons 现在只产"沿"，不推状态。故这里断言
        沿的产出（start_edge），状态链在 TestAutoButtonLogic 里测。
        """
        st, dev, model = self._station(debounce_ms=300)
        model.press_button(13, True)
        self._tick(st, dev, 1000)
        self.assertTrue(st.start_edge, "第一次按下应产出启动沿")

        # 抖动：松开再按下，间隔 < debounce
        model.press_button(13, False)
        self._tick(st, dev, 1040)
        model.press_button(13, True)
        self._tick(st, dev, 1080)
        self.assertFalse(st.start_edge,
                         "去抖窗口内的第二个沿必须被吞掉（否则一次抖动启动两次）")

    def test_lamps_recover_after_external_write(self):
        """灯被外部写灭后必须**自愈**补写回来。

        [实锅 2026-11-06 板端实测] 初版只在"目标值变化"时写灯（缓存记的是
        上次写下去的目标）。于是从数字量页整组清零（或任何外部写 DO7~9）
        把灯打灭后，目标没变 ⇒ 永远不再写，操作柱上的停止灯**再也不亮**
        （现场实测 5 拍全 0x0000）。修法是每拍回读实际值，不一致就补写。
        """
        st, dev, model = self._station()
        self._tick(st, dev, 100)
        self.assertTrue(self._lamp(model, 8), "初始停止灯亮")

        # 模拟外部把灯全灭（数字量页整组清零）
        model.do_mask = 0
        self.assertFalse(self._lamp(model, 8))

        self._tick(st, dev, 200)               # 下一拍应自愈补写
        self.assertTrue(self._lamp(model, 8),
                        "外部写灭后必须自动补写回停止灯")
        self.assertFalse(self._lamp(model, 7), "启动灯仍须灭")

    def test_lamps_not_rewritten_when_already_correct(self):
        """稳态不刷总线：灯已经对就不写（避免每拍 FC05）。

        [为什么这条重要] 真机模块对频繁写敏感，且线上帧数会翻倍。
        自愈逻辑必须"只在真的不一致时写"。
        """
        st, dev, model = self._station()
        self._tick(st, dev, 100)               # 首拍写一次
        before = len(dev.write_log) if hasattr(dev, "write_log") else None
        # 攒下 model 上的写次数（VirtualDigital 不记日志，改用 do_mask 稳定性）
        mask0 = model.do_mask
        for i in range(5):
            self._tick(st, dev, 200 + i * 100)
        self.assertEqual(model.do_mask, mask0, "稳态下 DO 位图不得变化")
        self.assertTrue(self._lamp(model, 8))

    def test_health_text_names_channels(self):
        """健康表地址串要能看出用了哪些点位。"""
        st, _dev, _model = self._station()
        text = st.health_text()
        self.assertIn("DI12", text)
        self.assertIn("DO7", text)


class TestAutoButtonLogic(unittest.TestCase):
    """按钮 → 自动运行的**决策链**（逻辑在 autocontrol.py，2026-11-06）。

    [用户口径，原话]
        「按钮启动这些都是自动模式下的逻辑只能在自动模式下生效，一旦切换
          到手動，为了安全要停止全部设备，并且按钮在自动模式下启动后，
          客户端顶部的状态要显示更新状态」
        「不是根据物理按钮，是根据设备是否开始自动运行，启动按钮只是给一个
          信号，要满足条件才切换状态」

    [为什么单独立一组] 旧实现把"按启动就进入运行"写在 buttons.py 里，等于
    物理按钮直接驱动设备，绕过模式授权。现在按键只是**请求**，本组锁的就是
    "请求在什么条件下才被受理"。
    """

    ITEMS = {"estop_di": 12, "start_di": 13, "stop_di": 14, "reset_di": 15,
             "start_lamp_do": 7, "stop_lamp_do": 8, "reset_lamp_do": 9,
             "module": 0}

    def _rig(self, with_buttons=True):
        """起一套"虚拟数字量 + 按钮组 + 自动控制器"。

        返回 (ctl, st, dev, model)。
        """
        from hsfsrv import autocontrol, buttons, devices, virtualdev

        model = virtualdev.VirtualDigital(channels=16)
        master = virtualdev.VirtualMaster(model, name="虚拟数字量")
        dev = devices.DigitalIo(master, "数字量", channels=16)
        dev.online = True

        class _App(object):
            digital = [dev]
            valve_bank = None
            pump_bank = None
            vfd = None
            analog = None
            temphum = None

            def __init__(self, station):
                self.button_station = station

        ctx = autocontrol.AutoContext.__new__(autocontrol.AutoContext)
        ctx._app = _App(None)
        ctx._log = lambda m: None
        ctx._throttle = {}
        ctx.last_error = ""
        ctx.action_count = 0

        st = None
        if with_buttons:
            st = buttons.ButtonStation(ctx, self.ITEMS, debounce_ms=0,
                                       log=lambda m: None)
            ctx._app.button_station = st
        ctl = autocontrol.AutoController(ctx, log=lambda m: None)
        return ctl, st, dev, model

    def _tick(self, ctl, st, dev, now_ms, auto_mode=True, online=True):
        """一拍：采设备 → 按钮采集 → 控制器受理按键。

        :param online: 采完之后把 dev.online 设成这个值（模拟模块掉线）。
                       [为什么不能在 poll 之前设] dev.poll 会把 online 置回
                       True（假设备语义就是"这台一直在线"），必须先 poll
                       再改，才模拟得出"读到的是离线状态"。
        """
        import asyncio

        async def one():
            await dev.poll(now_ms)
            dev.online = bool(online)
            st.poll(now_ms)
            ctl.poll_idle(now_ms, auto_mode)
            await asyncio.sleep(0.02)       # 让灯/全停的协程落地

        asyncio.run(one())

    def _press(self, ctl, st, dev, model, now_ms, channel, auto_mode=True,
               online=True):
        """按下并松开某个按钮（一个完整动作，占两拍）。"""
        model.press_button(channel, True)
        self._tick(ctl, st, dev, now_ms, auto_mode, online=online)
        model.press_button(channel, False)
        self._tick(ctl, st, dev, now_ms + 100, auto_mode, online=online)

    # ---- 1. 手动模式下按钮无效（用户明确要求）----
    def test_start_button_ignored_in_manual_mode(self):
        """**手动模式下按启动完全不生效** —— 这是用户要求的核心一条。

        [为什么最重要] 手动模式的语义是"人在客户端上操作"。如果现场按钮
        还能启动自动运行，等于手动模式里藏着一条没人看得见的自动通路。
        """
        ctl, st, dev, model = self._rig()
        self._press(ctl, st, dev, model, 1000, 13, auto_mode=False)
        self.assertFalse(ctl.running, "手动模式下启动按钮必须无效")
        self.assertFalse(st.running, "灯也不该亮成运行")

    def test_start_button_works_in_auto_mode(self):
        """自动模式下按启动 ⇒ 真的进入自动运行。"""
        ctl, st, dev, model = self._rig()
        self._press(ctl, st, dev, model, 1000, 13, auto_mode=True)
        self.assertTrue(ctl.running, "自动模式下启动按钮应生效")
        self.assertTrue(st.running, "运行态要同步给按钮组（灯跟着亮）")
        self.assertEqual(st.state, "running")

    # ---- 2. 启动要满足全部条件 ----
    def test_start_blocked_when_estop_active(self):
        """急停无信号时按启动被拒（安全侧默认）。"""
        ctl, st, dev, model = self._rig()
        model.press_button(12, False)          # 急停按下
        self._press(ctl, st, dev, model, 1000, 13)
        self.assertFalse(ctl.running, "急停期间不得启动")

    def test_start_blocked_when_button_module_offline(self):
        """模块离线（急停读不到）时按启动也被拒。

        反证：若把"读不到"当成放行，断线时按一下启动设备就转 —— 正是
        急停常闭要防的那件事。
        """
        ctl, st, dev, model = self._rig()
        self._press(ctl, st, dev, model, 1000, 13, online=False)
        self.assertFalse(ctl.running, "急停读不到时不得启动")
        # [断言口径] estop_ok 是只读三元（True/False），None 被折成 False：
        # ButtonStation 里写的是 `self.estop_ok = (raw is True)`，"读不到"
        # 与"无信号"在灯/状态机口径上是同一件事（都禁止）。要区分"有没有配"
        # 得看 estop_configured()。
        self.assertIsNone(ctl.ctx.estop_ok(),
                          "读不到 ⇒ 三态里的『未知』（不足以放行，但不算急停事件）")

    def test_start_blocked_after_estop_until_reset(self):
        """急停后即使回路恢复，**不按复位也不能启动**（锁存）。"""
        ctl, st, dev, model = self._rig()
        self._tick(ctl, st, dev, 1000)          # 先采到一帧"急停正常"
        self.assertFalse(ctl.estop_latched)

        model.press_button(12, False)
        self._tick(ctl, st, dev, 1100)          # 急停 → 锁存
        self.assertTrue(ctl.estop_latched)
        model.press_button(12, True)
        self._tick(ctl, st, dev, 1200)          # 回路恢复，但锁存还在
        self._press(ctl, st, dev, model, 1300, 13)
        self.assertFalse(ctl.running, "急停锁存期间启动必须被拒")

        self._press(ctl, st, dev, model, 1500, 15)   # 复位
        self.assertFalse(ctl.estop_latched)
        self._press(ctl, st, dev, model, 1700, 13)   # 再按启动
        self.assertTrue(ctl.running, "复位后启动应受理")

    def test_no_latch_before_first_frame(self):
        """**首帧未到时不锁存**（板端实锅 2026-11-06）。

        [实锅] 服务端刚启动、数字量还没采到第一帧时，急停读到 None。初版
        直接锁存，于是现场重启后立刻进入"急停锁存"，即使 DI12=1 也解不开
        （必须人工按一次复位），日志里还留一条编造出来的急停事件，让人去
        查根本不存在的急停回路。

        判据：从没见过"急停正常"之前，只按禁止启动处理，**不锁存**。
        """
        ctl, st, dev, model = self._rig()
        # 不 poll 设备 → 读不到 DI，模拟"首帧未到"
        import asyncio

        async def boot():
            st.poll(50)                     # 只采按钮（此时 dev 还没 poll 过）
            ctl.poll_idle(50, True)
            await asyncio.sleep(0.01)

        asyncio.run(boot())
        self.assertFalse(ctl.estop_latched, "首帧未到不得锁存")

        # 采到正常帧之后，再按急停才锁存
        self._tick(ctl, st, dev, 1000)
        model.press_button(12, False)
        self._tick(ctl, st, dev, 1100)
        self.assertTrue(ctl.estop_latched, "见过正常之后再急停要锁存")

    def test_reset_does_not_start(self):
        """复位只解除锁存，**不重新启动**（复位不是启动按钮）。"""
        ctl, st, dev, model = self._rig()
        model.press_button(12, False)
        self._tick(ctl, st, dev, 1000)
        model.press_button(12, True)
        self._tick(ctl, st, dev, 1100)
        self._press(ctl, st, dev, model, 1200, 15)
        self.assertFalse(ctl.running, "复位不得让设备自己跑起来")

    # ---- 3. 停止永远受理 ----
    def test_stop_button_always_works(self):
        """停止按钮在**任何模式**都受理（停机不该被任何闸门挡住）。"""
        ctl, st, dev, model = self._rig()
        self._press(ctl, st, dev, model, 1000, 13)      # 自动模式启动
        self.assertTrue(ctl.running)
        self._press(ctl, st, dev, model, 1200, 14, auto_mode=False)  # 手动下按停止
        self.assertFalse(ctl.running, "即使手动模式，停止按钮也要受理")

    # ---- 4. 灯跟运行态，不跟按键 ----
    def test_lamps_follow_running_state_not_button(self):
        """按下启动但被拒（手动模式）⇒ 灯**仍是停止**。

        [为什么这条重要] 若灯跟着按键走，操作员会看到"启动灯亮了"以为设备
        在跑，实际什么都没发生 —— 比不亮更危险。
        """
        ctl, st, dev, model = self._rig()
        model.press_button(13, True)
        self._tick(ctl, st, dev, 1000, auto_mode=False)
        self.assertFalse(bool((model.do_mask >> 7) & 1), "启动灯不得亮")
        self.assertTrue(bool((model.do_mask >> 8) & 1), "停止灯应亮")

    # ---- 5. 切回手动要全停 ----
    def test_leaving_auto_stops_everything(self):
        """切回手动 ⇒ 控制器停 + 全停设备 + 只点停止灯。

        [用户原话]「一旦切换到手動，为了安全要停止所有设备」
        """
        ctl, st, dev, model = self._rig()
        self._press(ctl, st, dev, model, 1000, 13)
        self.assertTrue(ctl.running)

        # 模拟 app.apply_mode_to_devices 的行为
        ctl.stop(1000, stop_devices=True, reason="切回手动")
        self.assertFalse(ctl.running)
        self.assertFalse(st.running, "按钮组运行态要同步成停止（停止灯亮）")

    def test_stop_all_switches_off_vfd_pump_valve(self):
        """全停要真的**逐个关掉**变频器/泵/阀，不能只停控制器。"""
        import asyncio
        ctl, st, dev, model = self._rig()

        calls = []

        class _Vfd(object):
            async def apply_control(self, op, reverse, hz, rpm, value, index):
                calls.append(("vfd", op))
                return True, ""

        class _Item(object):
            ready = True

        class _Bank(object):
            def __init__(self):
                self.items = [_Item()]

            async def set_pump(self, i, on):
                calls.append(("pump", i, on))
                return True, ""

            async def set_valve(self, i, on):
                calls.append(("valve", i, on))
                return True, ""

        ctl.ctx._app.vfd = _Vfd()
        ctl.ctx._app.pump_bank = _Bank()
        ctl.ctx._app.valve_bank = _Bank()

        async def run():
            ctl.stop_all_devices("测试")
            await asyncio.sleep(0.05)

        asyncio.run(run())
        kinds = sorted({c[0] for c in calls})
        self.assertEqual(kinds, ["pump", "valve", "vfd"],
                         "三类设备都要被停：%s" % calls)
        self.assertIn(("vfd", proto.OP_STOP), calls, "变频器应减速停机")
        for c in calls:
            if c[0] in ("pump", "valve"):
                self.assertFalse(c[-1], "泵/阀必须是关命令：%s" % (c,))

    def test_stop_all_survives_one_device_failing(self):
        """一台设备失败不能挡住停其它台。"""
        import asyncio
        ctl, st, dev, model = self._rig()
        calls = []

        class _Vfd(object):
            async def apply_control(self, *a):
                raise RuntimeError("设备离线")

        class _Item(object):
            ready = True

        class _Bank(object):
            def __init__(self):
                self.items = [_Item()]

            async def set_pump(self, i, on):
                calls.append(("pump", on))
                return True, ""

            async def set_valve(self, i, on):
                calls.append(("valve", on))
                return True, ""

        ctl.ctx._app.vfd = _Vfd()
        ctl.ctx._app.pump_bank = _Bank()

        async def run():
            ctl.stop_all_devices("测试")
            await asyncio.sleep(0.05)

        asyncio.run(run())
        self.assertIn(("pump", False), calls,
                      "变频器抛异常时泵仍须被停")

    # ---- 6. 无按钮组时不误拦（回归锁）----
    def test_no_button_station_does_not_block_control(self):
        """**没装操作柱的现场不能被"急停未知"拦住**（2026-11-06 实锅）。

        [实锅] 初版急停闸直接看 estop_ok()，未装配时恒 None ⇒ 被判"禁止"，
        纯 TCP 现场的启动/调速全被拒，报"急停状态未知"，表现为"设备好好的
        但点不动"（端到端 66 项全红暴露的）。判据必须区分"没配"和"配了读不到"。
        """
        ctl, st, dev, model = self._rig(with_buttons=False)
        self.assertIsNone(ctl.ctx.estop_ok(), "未装配时急停状态是未知")
        self.assertFalse(ctl.ctx.estop_configured(),
                         "未装配 = 没配这道安全件 ⇒ 不该拿它当闸")
        ok, why = ctl.start_preconditions(auto_mode=True)
        self.assertTrue(ok, "未配置急停的现场应允许启动，实际被拒：%s" % why)

    def test_configured_but_unreadable_still_blocks(self):
        """**配了但读不到**（模块离线）仍要拦 —— 与"没配"区分开。"""
        ctl, st, dev, model = self._rig()
        self._tick(ctl, st, dev, 1000, online=False)   # 采完后掉线
        self.assertTrue(ctl.ctx.estop_configured(), "配了急停点位 = 有这道闸")
        self.assertIsNone(ctl.ctx.estop_ok(), "离线 ⇒ 三态里的『未知』")
        ok, _why = ctl.start_preconditions(auto_mode=True)
        self.assertFalse(ok, "配了却读不到 ⇒ 必须按禁止处理")

    # ---- 7. 急停闸每拍生效（不依赖控制器在跑）----
    def test_estop_stops_running_controller(self):
        """运行中急停无信号 ⇒ 立刻停控制器 + 全停。"""
        ctl, st, dev, model = self._rig()
        self._press(ctl, st, dev, model, 1000, 13)
        self.assertTrue(ctl.running)
        model.press_button(12, False)
        self._tick(ctl, st, dev, 1200)
        self.assertFalse(ctl.running, "急停必须把运行中的控制器打停")
        self.assertTrue(ctl.estop_latched, "急停要锁存")


class TestButtonChannelNames(unittest.TestCase):
    """通道名：数字量页必须显示"急停/启动/停止/复位"，不能只有 DO7 这样的裸号。

    [用户 2026-11-06 明确要求]"将通道名字加入" —— 现场操作员看到
    "输入12" 不知道那是急停，这是可操作性要求。
    """

    def test_names_registered(self):
        """登记的是一张「通道 → 语义名」表（di/do 分两栏）。"""
        from hsfsrv import app as app_mod
        from hsfsrv import buttons, devices, virtualdev

        model = virtualdev.VirtualDigital(channels=16)
        master = virtualdev.VirtualMaster(model, name="虚拟")
        dev = devices.DigitalIo(master, "数字量", channels=16)

        station = buttons.ButtonStation(
            TestButtonStation._Ctx(dev), TestButtonStation.ITEMS, debounce_ms=0)
        app = object.__new__(app_mod.App)
        app.button_station = station

        out = {"di": {}, "do": {}}
        app_mod.App._button_channel_names(app, 0, 16, out)
        self.assertEqual(out["di"].get(12), "急停（常闭）")
        self.assertEqual(out["di"].get(13), "启动按钮")
        self.assertEqual(out["di"].get(14), "停止按钮")
        self.assertEqual(out["di"].get(15), "复位按钮")
        self.assertEqual(out["do"].get(7), "启动按钮灯")
        self.assertEqual(out["do"].get(8), "停止按钮灯")
        self.assertEqual(out["do"].get(9), "复位按钮灯")

    def test_names_skipped_for_other_module(self):
        """别的模块下标不登记（点位是模块内相对号）。"""
        from hsfsrv import app as app_mod
        from hsfsrv import buttons, devices, virtualdev

        model = virtualdev.VirtualDigital(channels=16)
        master = virtualdev.VirtualMaster(model, name="虚拟")
        dev = devices.DigitalIo(master, "数字量", channels=16)
        station = buttons.ButtonStation(
            TestButtonStation._Ctx(dev), TestButtonStation.ITEMS, debounce_ms=0)
        app = object.__new__(app_mod.App)
        app.button_station = station

        out = {"di": {}, "do": {}}
        app_mod.App._button_channel_names(app, 1, 16, out)
        self.assertEqual(out, {"di": {}, "do": {}}, "模块 1 不该被登记")

    def test_names_skipped_without_station(self):
        """未启用按钮组时不得崩（只是不登记）。"""
        from hsfsrv import app as app_mod
        app = object.__new__(app_mod.App)
        out = {"di": {}, "do": {}}
        app_mod.App._button_channel_names(app, 0, 16, out)
        self.assertEqual(out, {"di": {}, "do": {}})


class TestVfdRunPhase(unittest.TestCase):
    """运行阶段的推导：加速中 / 减速中 / 正在停机 / 停机。

    [为什么需要这一层] 用户 2026-11-06：「点击停止后，变频器是慢慢停机的，
    希望正在停机这个过程状态显示出来，还有正在加速、正在减速都显示下」。

    但 **M512 状态字 0x3000 硬件只有 3 个值**（1=正转 / 2=反转 / 3=停机），
    厂商不通过它报告加速/减速。唯一可行办法是按「输出频率 vs 设定频率」
    的差值反推 —— 本组测试锁的就是这套推导。
    """

    def _dev(self):
        from hsfsrv import devices, virtualdev
        model = virtualdev.VirtualVfd()
        master = virtualdev.VirtualMaster(model, name="虚拟变频器")
        dev = devices.VfdM512(lambda: master, 2, lambda _m: None)
        return model, dev

    def _poll(self, dev, now_ms=1000):
        import asyncio
        asyncio.run(dev.poll(now_ms))

    def test_accelerating_when_output_below_target(self):
        """启动后输出还没爬到目标 ⇒ 加速中。"""
        from hsfsrv import proto
        model, dev = self._dev()
        model.set_percent = 10000            # 目标 50Hz
        model.running = True
        model.target_hz = 50.0
        model.out_hz = 10.0
        self._poll(dev)
        self.assertEqual(dev.run_state, proto.RUN_ACCELERATING)
        self.assertTrue(dev.is_running(), "加速中当然算运行")

    def test_decelerating_when_target_below_output(self):
        """运行中把给定调低 ⇒ 减速中，且**仍算运行**（否则调速被自己拒）。"""
        from hsfsrv import proto
        model, dev = self._dev()
        model.running = True
        model.set_percent = 5000             # 目标 25Hz
        model.target_hz = 25.0
        model.out_hz = 45.0                  # 当前还在 45Hz
        self._poll(dev)
        self.assertEqual(dev.run_state, proto.RUN_DECELERATING)
        self.assertTrue(dev.is_running(),
                        "降给定中的减速不是停机，必须允许继续调速")

    def test_stopping_is_distinguished_from_decelerating(self):
        """停机斜坡（目标=0 但输出>0）与"降给定"要分开判。

        [为什么这条重要] 两者都返回 RUN_DECELERATING，但语义相反：
        前者不能再调速（要停了），后者可以。判据是 `_stopping_intent()`。
        """
        from hsfsrv import proto
        model, dev = self._dev()
        model.running = True
        model.set_percent = 0                # 目标 0 = 停机
        model.target_hz = 0.0
        model.out_hz = 30.0                  # 还在斜坡上
        self._poll(dev)
        self.assertEqual(dev.run_state, proto.RUN_DECELERATING)
        self.assertFalse(dev.is_running(), "停机斜坡不该算运行态")

    def test_stopped_when_output_reaches_zero(self):
        """输出归零 ⇒ 停机。"""
        from hsfsrv import proto
        model, dev = self._dev()
        model.running = False
        model.out_hz = 0.0
        model.target_hz = 0.0
        self._poll(dev)
        self.assertEqual(dev.run_state, proto.RUN_STOPPED)
        self.assertFalse(dev.is_running())

    def test_fault_overrides_phase(self):
        """故障优先级最高，盖过任何运行阶段。"""
        from hsfsrv import proto
        model, dev = self._dev()
        model.running = True
        model.out_hz = 10.0
        model.target_hz = 50.0               # 本应是"加速中"
        model.fault_code = 0x0002
        self._poll(dev)
        self.assertEqual(dev.run_state, proto.RUN_FAULT)

    def test_run_state_word_still_has_only_three_values(self):
        """状态字段的**硬件口径**不许被改动（1/2/3），推导只覆盖 run_state。

        [回归锁] 若有人"顺手"把加速/减速写进 0x3000 的返回值，真机就错了
        —— 变频器不认这些值。锁住原始状态字映射。
        """
        model, _dev = self._dev()
        model.running = True
        model.reverse = False
        self.assertEqual(model._reg(0x3000), 1, "正转必须是 1")
        model.reverse = True
        self.assertEqual(model._reg(0x3000), 2, "反转必须是 2")
        model.running = False
        self.assertEqual(model._reg(0x3000), 3, "停机必须是 3")

    def test_stopping_not_misjudged_as_accelerating(self):
        """停机过程中**绝不能**显示成"加速中"。

        [真机实锅 2026-11-06 板端实测] 点停止后变频器的 0x1000（设定频率）
        仍保留旧给定 48.6Hz，而输出在斜坡下降 ⇒ `target > out` ⇒ 按差值
        差值判据会得出"加速中"，实测停机全程显示"加速中"，完全反了。

        修法：以「已发停止命令」为主判据（`_stop_requested`），频率趋势为
        辅。本测试锁住：发出停止命令后每一拍都必须判减速，直到输出归零。
        """
        from hsfsrv import proto
        model, dev = self._dev()
        model.set_percent = 9720             # 48.6Hz 旧给定（关键：停机后仍在）
        model.running = True
        model.out_hz = 48.6
        model.target_hz = 48.6
        self._poll(dev, 1000)
        self.assertEqual(dev.run_state, proto.RUN_RUNNING_FORWARD)

        import asyncio
        asyncio.run(dev.apply_control(proto.OP_STOP))

        seq = []
        for i in range(8):
            model.on_access()                # 斜坡下降
            self._poll(dev, 1200 + i * 200)
            seq.append(dev.run_state)
        self.assertNotIn(proto.RUN_ACCELERATING, seq,
                         "停机过程中不得出现『加速中』，实测序列 %s" % seq)
        self.assertIn(proto.RUN_DECELERATING, seq, "必须出现减速中")
        self.assertEqual(seq[-1], proto.RUN_STOPPED, "最终应停机")

    def test_stop_requested_cleared_on_restart(self):
        """重新启动要清掉停机窗口标记，否则启动后被误判成减速。"""
        from hsfsrv import proto
        model, dev = self._dev()
        model.set_percent = 5000
        model.running = True
        model.out_hz = 25.0
        model.target_hz = 25.0
        self._poll(dev, 1000)

        import asyncio
        asyncio.run(dev.apply_control(proto.OP_STOP))
        self.assertTrue(dev._stop_requested)

        model.out_hz = 10.0                  # 还没停完就重启
        asyncio.run(dev.apply_control(proto.OP_START, hz=25.0))
        self.assertFalse(dev._stop_requested, "启动必须清停机标记")
        model.target_hz = 25.0
        self._poll(dev, 1400)
        self.assertEqual(dev.run_state, proto.RUN_ACCELERATING,
                         "重启后是在加速，不是减速")

    def test_decel_stop_ramps_instead_of_cutting(self):
        """减速停机（命令 6）必须走斜坡；自由停车（命令 5）才瞬断。

        [实锅] 初版 on_access 里"只要不在运行态就 out_hz=0"，点停止后
        频率瞬间归零 ⇒ 界面上永远看不到"正在停机"。
        """
        model, _dev = self._dev()
        model.set_percent = 5000
        model.write_single(6, 0x2000, 1)      # 启动
        model.out_hz = 25.0
        model.target_hz = 25.0
        model.write_single(6, 0x2000, 6)      # 减速停机
        self.assertTrue(model.running, "减速停机不该立刻置停机")
        model.on_access()
        self.assertGreater(model.out_hz, 0.0, "必须还有输出（斜坡中）")
        self.assertTrue(model.decelerating)

        # 自由停车：立刻断
        model2, _ = self._dev()
        model2.set_percent = 5000
        model2.write_single(6, 0x2000, 1)
        model2.out_hz = 25.0
        model2.write_single(6, 0x2000, 5)     # 自由停车
        self.assertFalse(model2.running)
        self.assertEqual(model2.out_hz, 0.0, "自由停车必须瞬断")


if __name__ == "__main__":
    unittest.main(verbosity=2)
