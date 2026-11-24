"""Read relay (coil) and input states from a Modbus-RTU relay board.

Read-only: only FC01 (read coils) and FC02 (read discrete inputs) are sent.
No writes, no address/baud changes.
"""
import sys
import time
import serial

PORT = "COM17"
BAUD = 9600
SLAVE = 0x0A  # station 10
QTY = 8       # read a full byte so we can see the real channel layout


def crc16(data):
    crc = 0xFFFF
    for b in data:
        crc ^= b
        for _ in range(8):
            if crc & 1:
                crc = (crc >> 1) ^ 0xA001
            else:
                crc >>= 1
    return crc & 0xFFFF


def build(slave, fc, qty, start=0):
    body = [slave, fc, (start >> 8) & 0xFF, start & 0xFF, 0x00, qty]
    crc = crc16(body)
    return bytes(body + [crc & 0xFF, crc >> 8])


def find_frame(buf):
    """Return the first CRC-valid Modbus frame found in buf."""
    for i in range(len(buf) - 4):
        fc = buf[i + 1]
        if fc not in (0x01, 0x02):
            continue
        if i + 3 >= len(buf):
            continue
        count = buf[i + 2]
        total = 5 + count  # addr, fc, count, data[count], crc(2)
        if i + total > len(buf):
            continue
        f = buf[i:i + total]
        if crc16(f[:-2]) == (f[-2] | (f[-1] << 8)):
            return f
    return None


def exchange(ser, req, retries=3, quiet=0.25):
    """Read a burst of bytes after the request, then pick a valid frame."""
    for _ in range(retries):
        ser.reset_input_buffer()
        ser.reset_output_buffer()
        ser.write(req)
        time.sleep(0.08)  # let the board start answering
        buf = bytearray()
        deadline = time.time() + 0.4
        while time.time() < deadline:
            n = ser.in_waiting
            if n:
                buf.extend(ser.read(n))
                deadline = time.time() + quiet  # reset silence timer
            else:
                time.sleep(0.01)
        if buf:
            f = find_frame(buf)
            if f:
                return bytes(f), bytes(buf)
            return b"", bytes(buf)
    return b"", b""


def parse_bits(resp, qty):
    count = resp[2]
    data = resp[3:3 + count]
    return [(data[b // 8] >> (b % 8)) & 1 for b in range(qty)]


def probe(parity):
    print("\n===== parity=%s =====" % parity)
    with serial.Serial(port=PORT, baudrate=BAUD, bytesize=8, parity=parity,
                       stopbits=1, timeout=0.1) as ser:
        time.sleep(0.2)
        results = {}
        for name, fc in [("继电器输出 (coils, FC01)", 0x01),
                         ("输入口 (inputs, FC02)", 0x02)]:
            req = build(SLAVE, fc, QTY)
            frame, raw = exchange(ser, req)
            print("%-26s TX %s" % (name, req.hex(' ').upper()))
            print("    RAW %s" % (raw.hex(' ').upper() or "(空)"))
            if frame:
                print("    FRAME %s  CRC=OK" % frame.hex(' ').upper())
                bits = parse_bits(frame, QTY)
                results[fc] = bits
                print("    bit0..7 : %s" % " ".join(str(b) for b in bits))
                print("    通道1=%s 通道2=%s" %
                      ("ON" if bits[0] else "OFF", "ON" if bits[1] else "OFF"))
            else:
                print("    无有效应答")
        return results


if __name__ == "__main__":
    print("端口 %s @ %d 8?1, 站号 %d (0x%02X), 读取 %d 路" %
          (PORT, BAUD, SLAVE, SLAVE, QTY))
    res = None
    for par in ("N", "E"):
        try:
            r = probe(par)
            if r:
                res = r
                break
        except serial.SerialException as e:
            print("串口打开失败 (%s): %s" % (par, e))
    print("\n结论:", "读取成功" if res else "未读到数据（检查站号/波特率/接线/奇偶校验）")
