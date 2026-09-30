#!/usr/bin/env python3
"""Regression for the IME phrase compiler: regenerate and byte-compare.

Regenerates the shipped binary assets from their txt sources and compares
them byte-for-byte with the committed products.  EXIT=0 means the committed
.bin files are exactly what the current compiler + txt + syllable table
produce (same contract as Tools/codegen/test_gen_spv.py).

Usage:
    python Tools/VirtualKeyboard/test_ime_phrases_compile.py
"""

import struct
import subprocess
import sys
import tempfile
import zlib
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
COMPILER = ROOT / "Tools" / "VirtualKeyboard" / "ime_phrases_compile.py"

# (txt source, committed bin, compile args)
CASES = [
    ("Library/VirtualKeyboard/phrases_zh.txt",
     "Library/VirtualKeyboard/phrases_zh.bin",
     ["--verify"]),
    ("Library/VirtualKeyboard/test/phrases_test_fixture.txt",
     "Library/VirtualKeyboard/test/phrases_test_fixture.bin",
     ["--lenient", "--verify"]),
]


def run_compile(txt, out, extra_args):
    run_compile_to(txt, ROOT / out, extra_args)


def run_compile_to(txt, out_path, extra_args):
    cmd = [sys.executable, str(COMPILER), str(ROOT / txt),
           str(out_path), "--syllable-src",
           str(ROOT / "Src/XGui/VirtualKeyboard/XPinyinTable.c")] + extra_args
    result = subprocess.run(cmd, capture_output=True, text=True)
    if result.returncode != 0:
        print(result.stdout)
        print(result.stderr)
        raise SystemExit("error: compile failed for %s (exit %d)"
                         % (txt, result.returncode))
    return result.stdout


def check_bad_bins():
    """Sanity-check the deliberately broken fixtures still break."""
    committed_bin = (ROOT / "Library/VirtualKeyboard/"
                     "test/phrases_test_fixture.bin").read_bytes()
    committed = (ROOT / "Library/VirtualKeyboard/"
                 "test/phrases_test_fixture_badcrc.bin").read_bytes()
    # badcrc: flip one byte in the words region -> CRC must differ
    badcrc = bytearray(committed_bin)
    badcrc[-1] ^= 0xFF
    assert bytes(badcrc) == committed, "badcrc fixture drifted"
    assert (zlib.crc32(badcrc[24:]) & 0xFFFFFFFF) != \
        struct.unpack("<I", badcrc[20:24])[0], "badcrc CRC not broken"

    # badver: version field must be 2 (vs committed 1)
    badver = bytearray(committed_bin)
    struct.pack_into("<H", badver, 4, 2)
    committed = (ROOT / "Library/VirtualKeyboard/"
                 "test/phrases_test_fixture_badver.bin").read_bytes()
    assert bytes(badver) == committed, "badver fixture drifted"
    assert struct.unpack("<H", bytes(badver[4:6]))[0] != \
        struct.unpack("<H", committed_bin[4:6])[0]

    # trunc: header + 1 entry (40B) -> loader step 5 bound 56 > 40
    trunc = committed_bin[:40]
    committed = (ROOT / "Library/VirtualKeyboard/"
                 "test/phrases_test_fixture_trunc.bin").read_bytes()
    assert trunc == committed, "trunc fixture drifted"
    assert len(trunc) < 24 + 2 * 16

    # badmagic: magic 'XIPX' -> sniffed as text (0 valid lines)
    badmagic = bytearray(committed_bin)
    badmagic[0:4] = b"XIPX"
    committed = (ROOT / "Library/VirtualKeyboard/"
                 "test/phrases_test_fixture_badmagic.bin").read_bytes()
    assert bytes(badmagic) == committed, "badmagic fixture drifted"
    assert bytes(badmagic[:4]) != b"XIPB"


def main():
    for txt, out, extra in CASES:
        committed = (ROOT / out).read_bytes()
        # 先编译到临时路径，再与未触动的入库产物比对——绝不能把编译器
        # 输出直接落在入库路径上：那会先覆盖再读回，比对退化为"两次新
        # 鲜编译互比"，入库产物被静默篡改时漂移不可检测（评审实证）。
        with tempfile.TemporaryDirectory() as td:
            tmp = Path(td) / "regen.bin"
            run_compile_to(txt, tmp, extra)
            if tmp.read_bytes() != committed:
                raise SystemExit("error: %s does not match regeneration "
                                 "(byte-compare FAILED — committed asset "
                                 "drifted from source)" % out)
        print("[test_ime_phrases_compile] %s byte-compare OK (%d bytes)"
              % (out, len(committed)))
    check_bad_bins()
    print("[test_ime_phrases_compile] bad-bin fixtures OK")
    print("PASS")
    return 0


if __name__ == "__main__":
    sys.exit(main())
