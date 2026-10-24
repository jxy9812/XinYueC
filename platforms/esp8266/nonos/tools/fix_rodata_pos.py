#!/usr/bin/env python3
"""RODATA_IN_FLASH=1 专用镜像后处理：把 flash 常量段并回线性映射位置。

背景（R1c/R1d 真机定位）：build_fw_final.sh 用 move_rodata_to_flash.py 把
.rodata 挪进 irom0_0_seg（VMA 0x4026xxxx）后，Arduino 的 elf2bin.py 仍按
段列表顺序 [.irom0.text, .text, .text1, .data, .rodata] 顺序写镜像 ——
.rodata 的字节落在镜像尾部（~0x685B7），而 ESP8266 的 flash cache 是纯
线性映射：读 VMA x 就是读 flash 偏移 x-0x40200000。于是 CPU 在 .rodata
VMA 处读到的是 31KB 之前的 .irom0.text 代码字节。启动早期走 spi_flash_read
直读的打印（boot not set 等）正常，net80211/phy 走 cache 读常量即拿到垃圾
→ SDK 永久停摆（cap_R1c/cap_R1d：Mo 之后无任何输出、无 KEX、无复位）。

Arduino 原生流程 .rodata 永远在 DRAM（由 eboot SPIRead 搬运），所以 elf2bin
这一错位从未暴露。eboot 的 load_app_from_flash_raw 对 0x40200000+ 段是跳过
语义（eboot.c: `if (!load) { pos += size; continue; }`），因此本脚本把
.rodata 并进 .irom0.text 段头（间隙补零）：
    raw(.rodata) = 0x1010 + size(irom0.text) + gap = VMA(.rodata) - 0x40200000
精确成立，eboot 跳过整段，cache 线性映射直接服务。

幂等：找不到「非首块的 0x40200000+ 段」就原样退出（DRAM 布局或已修过）。
末尾按 stock elf2bin 规则重算镜像 checksum 与整体 CRC。
"""
import struct
import sys

APP_OFFSET = 0x1000
CSZ_OFF = APP_OFFSET + 16
CVAL_OFF = APP_OFFSET + 16 + 4


def crc8266(data):
    crc = 0xffffffff
    for byte in data:
        crc ^= byte
        for _ in range(8):
            bit = crc & 0x80000000
            crc = (crc << 1) & 0xffffffff
            if bit:
                crc ^= 0x04c11db7
    return crc


def main(path):
    data = bytearray(open(path, 'rb').read())
    pos = APP_OFFSET
    magic, nseg = data[pos], data[pos + 1]
    assert magic == 0xE9, 'bad app magic at 0x1000: %02x' % magic
    pos += 8

    segs = []   # (addr, raw)
    for _ in range(nseg):
        addr, size = struct.unpack_from('<II', data, pos)
        pos += 8
        segs.append((addr, bytes(data[pos:pos + size])))
        pos += size
    assert pos == len(data) - 1 or pos <= len(data), 'image parse overrun'

    flash_segs = [a for a, _ in segs if 0x40200000 <= a < 0x60000000]
    if len(flash_segs) < 2:
        print('no separate flash-resident .rodata segment; nothing to do')
        return 0
    irom0_addr = flash_segs[0]
    rod_addr = [a for a in flash_segs if a != irom0_addr][0]

    out_segs = []
    for addr, raw in segs:
        if addr == rod_addr:
            continue
        if addr == irom0_addr:
            gap = rod_addr - (addr + len(raw))
            assert gap >= 0, 'negative gap %d' % gap
            out_segs.append((addr, raw + b'\x00' * gap +
                             next(r for a, r in segs if a == rod_addr)))
        else:
            out_segs.append((addr, raw))

    img = bytearray()
    img.append(magic)
    img.append(len(out_segs))
    img += data[APP_OFFSET + 2:APP_OFFSET + 8]          # mode/freq/size/entry 原样
    checksum = 0xEF
    for addr, raw in out_segs:
        img += struct.pack('<II', addr, len(raw))
        img += raw
        for b in raw:
            checksum ^= b
    while len(img) & 15:
        img.append(0)
    img.append(checksum)

    # ⚠⚠ 2026-10-22 崩溃根修：此前这里把「镜像长度 + CRC32」写到
    # CSZ_OFF=0x1010 / CVAL_OFF=0x1014——ESP8266 app image 在 byte 16 处
    # **没有** size/CRC 字段（那是 ESP32 的 extended header 约定；
    # eboot 只认镜像尾部的单字节 checksum），byte 16 起就是第一段的
    # 数据。.rodata 并入 irom0 后第一段 = .irom0.text，byte 16 恰好是
    # XAtomic_exchange_bool 的入口指令——前 8 字节被长度+CRC 覆盖，
    # CPU 从 0x40201010 取指拿到垃圾（第 4 字节 0x00 = 非法指令），
    # 稳定复现 Fatal exception 0 @ epc1=0x40201013。该 bug 自本脚本
    # 诞生起就存在，但彼时没有任何执行流走到该函数（时基 -1 → 时间轮
    # 全哑），一直假阴性。修复 = 不写这两个字段。
    out = bytearray(data[:APP_OFFSET]) + img
    open(path, 'wb').write(out)
    print('merged .rodata (VMA 0x%08x) into irom0 segment (VMA 0x%08x, gap %d); '
          'image %d -> %d bytes' % (rod_addr, irom0_addr, gap, len(data), len(out)))
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv[1] if len(sys.argv) > 1 else 'fw_final.bin'))
