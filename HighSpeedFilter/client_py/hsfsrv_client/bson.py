# -*- coding: utf-8 -*-
"""BSON 编解码（标准 BSON 文档，只实现本协议用得到的类型）。

XLink 的 XLinkDataType_Bson 通道承载的是**标准 BSON 文档**（XBsonDocument
的 toBson 输出），不是 JSON 文本：首 4 字节小端即文档总长，末字节必为 0x00。

为什么要自己写而不用第三方库：
  * 目标板 Python 3.9 + 无 pip 环境，除 pyserial 外不保证有任何第三方包；
  * 协议只用到 5 种类型，标准库 struct 足够，零依赖 = 零部署风险。

字段顺序 = 写入顺序，与 C 侧 XBsonDocument 的插入顺序一致（BSON 规范不要求
有序，但保持同序便于逐字节比对与抓包排查）。

支持的类型（其余一律抛 ValueError，**不静默降级**）：
    0x01 double  0x02 string  0x03 document  0x04 array
    0x08 bool    0x0A null    0x10 int32     0x12 int64

[数组 0x04] 线格式与嵌套文档**完全相同**，只是键名按 BSON 规范换成
"0","1","2"…（元素序号字符串）。历史分页一次返回多行记录，用数组
比"r0/r1/r2…"这种手拼键名干净得多；C 侧 XBson 早已支持 0x04
（XBSON_TYPE_ARRAY），Python 侧此前没用到所以没实现。
"""

import struct

# BSON 类型字节
T_DOUBLE = 0x01
T_STRING = 0x02
T_DOCUMENT = 0x03
T_ARRAY = 0x04
T_BOOL = 0x08
T_NULL = 0x0A
T_INT32 = 0x10
T_INT64 = 0x12


def encode(doc):
    """dict → BSON 文档字节。

    Python 侧类型映射（与 C 侧插入函数一一对应）：
        bool        → 0x08（**必须先判 bool**，Python 里 bool 是 int 的子类）
        float       → 0x01 double
        int         → 0x10 / 0x12（按 int32 值域自动选）
        str         → 0x02 UTF-8
        dict        → 0x03 嵌套文档
        list/tuple  → 0x04 数组（元素键名 "0","1",…）
        None        → 0x0A null（历史行的"该列没数据"，与 0 是两件事）
    """
    body = bytearray()
    for key, val in doc.items():
        # bool 必须先判：isinstance(True, int) 为 True，否则会被编成 int32。
        if val is None:
            body += bytes([T_NULL]) + _cstr(key)
        elif isinstance(val, bool):
            body += bytes([T_BOOL]) + _cstr(key) + (b"\x01" if val else b"\x00")
        elif isinstance(val, float):
            body += bytes([T_DOUBLE]) + _cstr(key) + struct.pack("<d", val)
        elif isinstance(val, int):
            if -2 ** 31 <= val < 2 ** 31:
                body += bytes([T_INT32]) + _cstr(key) + struct.pack("<i", val)
            else:
                body += bytes([T_INT64]) + _cstr(key) + struct.pack("<q", val)
        elif isinstance(val, str):
            raw = val.encode("utf-8") + b"\x00"
            body += bytes([T_STRING]) + _cstr(key) + struct.pack("<i", len(raw)) + raw
        elif isinstance(val, dict):
            sub = encode(val)
            body += bytes([T_DOCUMENT]) + _cstr(key) + sub
        elif isinstance(val, (list, tuple)):
            sub = _encode_array(val)
            body += bytes([T_ARRAY]) + _cstr(key) + sub
        else:
            raise ValueError("BSON 不支持的 Python 类型: %r (%r)" % (type(val), val))
    body += b"\x00"
    return struct.pack("<i", len(body) + 4) + bytes(body)


def _encode_array(items):
    """数组体：键名 "0"…"n-1" 的文档（BSON 规范口径，与嵌套文档同线格式）。"""
    return encode(dict((str(i), item) for i, item in enumerate(items)))


def _cstr(key):
    """键名 → UTF-8 + NUL 终止（BSON 的 e_name 口径）。"""
    raw = key.encode("utf-8")
    if b"\x00" in raw:
        raise ValueError("BSON 键名不得含 NUL: %r" % (key,))
    return raw + b"\x00"


def decode(data):
    """BSON 文档字节 → dict。

    解析**不信任任何长度字段**：每一字段都先核对剩余空间再推进，越界即抛
    ValueError（调用方按「载荷解析失败」回否定应答，不让畸形包影响后续帧）。
    未知类型同样抛错——静默跳过会让调用方拿着半份数据做决策。
    嵌套文档递归解为 dict。
    """
    if not isinstance(data, (bytes, bytearray)) or len(data) < 5:
        raise ValueError("BSON 长度不足")
    total = struct.unpack("<i", data[:4])[0]
    if total < 5 or total > len(data):
        raise ValueError("BSON 文档长度非法: %d (实际 %d)" % (total, len(data)))
    out = {}
    i = 4
    end = total - 1          # 末字节是文档终止符 0x00
    while i < end:
        ftype = data[i]
        i += 1
        nul = data.find(b"\x00", i, end)
        if nul < 0:
            raise ValueError("BSON 键名未终止（偏移 %d）" % i)
        key = data[i:nul].decode("utf-8", "replace")
        i = nul + 1
        if ftype == T_DOUBLE:
            _need(data, i, 8, end)
            out[key] = struct.unpack("<d", data[i:i + 8])[0]
            i += 8
        elif ftype == T_NULL:
            out[key] = None
        elif ftype == T_BOOL:
            _need(data, i, 1, end)
            out[key] = data[i] != 0
            i += 1
        elif ftype == T_INT32:
            _need(data, i, 4, end)
            out[key] = struct.unpack("<i", data[i:i + 4])[0]
            i += 4
        elif ftype == T_INT64:
            _need(data, i, 8, end)
            out[key] = struct.unpack("<q", data[i:i + 8])[0]
            i += 8
        elif ftype == T_STRING:
            _need(data, i, 4, end)
            n = struct.unpack("<i", data[i:i + 4])[0]
            if n < 1:
                raise ValueError("BSON 字符串长度非法: %d" % n)
            _need(data, i + 4, n, end)
            out[key] = data[i + 4:i + 4 + n - 1].decode("utf-8", "replace")
            i += 4 + n
        elif ftype == T_DOCUMENT:
            _need(data, i, 4, end)
            sub_len = struct.unpack("<i", data[i:i + 4])[0]
            if sub_len < 5 or i + sub_len > end + 1:
                raise ValueError("BSON 嵌套文档长度非法: %d" % sub_len)
            out[key] = decode(data[i:i + sub_len])
            i += sub_len
        elif ftype == T_ARRAY:
            # 数组与嵌套文档同线格式，区别只在键名是序号串：解成 list。
            _need(data, i, 4, end)
            sub_len = struct.unpack("<i", data[i:i + 4])[0]
            if sub_len < 5 or i + sub_len > end + 1:
                raise ValueError("BSON 数组长度非法: %d" % sub_len)
            sub = decode(data[i:i + sub_len])
            out[key] = _array_items(sub)
            i += sub_len
        else:
            raise ValueError("BSON 不支持的类型 0x%02X（键 %r）" % (ftype, key))
    return out


def _array_items(sub):
    """嵌套文档 dict → list（按键名序号的数值顺序）。

    键名不是纯序号时（非法/异构的数组编码）**退回按线上顺序**取值，
    不按字典序 —— "10" 排在 "2" 前面会让行序错乱，这比类型宽松更危险。
    """
    try:
        return [sub[k] for k in sorted(sub, key=lambda s: int(s))]
    except (TypeError, ValueError):
        return list(sub.values())


def _need(data, offset, size, end):
    """核对 [offset, offset+size) 是否落在 (…, end) 内。"""
    if offset + size > end:
        raise ValueError("BSON 字段越界（偏移 %d 需要 %d 字节，剩余 %d）"
                         % (offset, size, end - offset))


def peek_int32(data, key, default=0):
    """宽松取值：解析失败或缺键时回 default（用于「应答包络」这类非关键路径）。"""
    try:
        val = decode(data).get(key, default)
        return val if isinstance(val, int) and not isinstance(val, bool) else default
    except ValueError:
        return default
