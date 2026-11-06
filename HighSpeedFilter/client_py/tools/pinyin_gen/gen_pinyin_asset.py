# -*- coding: utf-8 -*-
"""把 C 框架的拼音数据编译成客户端运行资产（pinyin_zh.bin）。

    python tools/pinyin_gen/gen_pinyin_asset.py
    python tools/pinyin_gen/gen_pinyin_asset.py --verify

[为什么要有这一步] 客户端 Python 端不做「猜拼音」，候选必须与 C 端
（Src/XGui/VirtualKeyboard/XPinyinTable.c 单字表 + Library/VirtualKeyboard
/phrases_zh.txt 词组库）逐字一致 —— 两边同一套词、同一套频序，联调时
不会出现「C 端板子和 Python 界面候选不一样」。所以资产直接从 C 源解析，
不手工另抄一份（抄一份必然漂移）。

[为什么不在运行时读 C 源/txt] 板端部署包只带 hsfsrv_client 目录，不带
整个 C 仓；txt 每行正则解析也比读二进制慢一个量级。生成器把两大表压成
一个自描述小二进制，随包发布。

文件格式（小端，无 BOM）：
    0   magic[4]      b'XPYB'
    4   version  u16   =1
    6   sylCount u16   音节表条数
    8   singleCount u32 单字条目数
    12  phraseCount u32 词组条目数
    16  保留 u32 =0
    20  音节区：sylCount × (u8 len + ascii 字节)        —— 字典序
    之后 单字区：singleCount × (u16 sid + u8 rank + u8 len + utf8 字节)
        词组区：phraseCount × (u8 nSyl + u16×nSyl + u8 rank + u8 len + utf8)

字段全部 u8/u16 定长，rank 组内从 1 递增（与 C 的 m_rank 同口径），
词串区不带 NUL（len 前缀），load 侧零歧义。
"""

import argparse
import os
import re
import struct
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
CLIENT_ROOT = os.path.dirname(os.path.dirname(HERE))
REPO_ROOT = os.path.dirname(os.path.dirname(CLIENT_ROOT))

DEFAULT_SRC = os.path.join(REPO_ROOT, "Src", "XGui", "VirtualKeyboard",
                           "XPinyinTable.c")
DEFAULT_PHRASES = os.path.join(REPO_ROOT, "Library", "VirtualKeyboard",
                               "phrases_zh.txt")
DEFAULT_OUT = os.path.join(CLIENT_ROOT, "hsfsrv_client", "data",
                           "pinyin_zh.bin")

MAGIC = b"XPYB"
VERSION = 1
HEADER = struct.Struct("<4sHHIII")     # 20 字节（含 4 字节保留）


# ============================================================================
#                        C 源解析（音节表 + 单字表）
# ============================================================================

def parse_syllables(text):
    """k_imeSyllables 数组 → 音节串列表（保序 = 音节 id）。"""
    body = text[text.index("k_imeSyllables"):]
    body = body[:body.index("};")]
    return re.findall(r'"([a-z]+)"', body)


def parse_single_entries(text):
    """k_imeEntries 数组 → [(sid, 汉字, rank)]。

    汉字在 C 源里写成 "\\xE5\\x95\\x8A" 这种十六进制转义（与源码文件编码
    解耦）。这里先按 C 语义还原字节，再按 UTF-8 解码 —— 顺序反了会得到
    乱码（本项目实测踩过）。
    """
    body = text[text.index("static const XPinyinTableEntry k_imeEntries"):]
    body = body[:body.index("\n};")]
    out = []
    for m in re.finditer(r"\{(\d+),\s*((?:\"[^\"]*\"\s*)+),\s*(\d+)\}", body):
        sid = int(m.group(1))
        joined = "".join(re.findall(r'"([^"]*)"', m.group(2)))
        raw = joined.encode("latin-1").decode("unicode_escape").encode("latin-1")
        out.append((sid, raw.decode("utf-8"), int(m.group(3))))
    return out


def parse_phrase_txt(path):
    """词组库 txt → [((sid...), 词, rank)]（音节串经 table 反查成 id）。"""
    rows = []
    with open(path, encoding="utf-8-sig") as fh:
        for lineno, line in enumerate(fh, 1):
            line = line.rstrip("\n").rstrip("\r")
            if not line.strip() or line.lstrip().startswith("#"):
                continue
            fields = line.split("\t")
            if len(fields) != 3:
                raise ValueError("%s:%d 字段数 %d != 3（%r）"
                                 % (path, lineno, len(fields), line))
            syllables = fields[0].split()
            word = fields[1]
            rank = int(fields[2])
            if not 2 <= len(syllables) <= 4:
                raise ValueError("%s:%d 音节数 %d 超界（2..4）"
                                 % (path, lineno, len(syllables)))
            if len(word.encode("utf-8")) > 15:
                raise ValueError("%s:%d 词超 15 字节（%r）"
                                 % (path, lineno, word))
            rows.append((tuple(syllables), word, rank))
    return rows


# ============================================================================
#                                打包
# ============================================================================

def build_binary(syllables, singles, phrases):
    for sid, _w, _r in singles:
        if sid >= len(syllables):
            raise ValueError("单字条目音节 id %d 越界（音节表 %d 条）"
                             % (sid, len(syllables)))

    out = bytearray()
    out += HEADER.pack(MAGIC, VERSION, len(syllables),
                       len(singles), len(phrases), 0)
    for syl in syllables:
        raw = syl.encode("ascii")
        out += bytes([len(raw)]) + raw
    for sid, word, rank in singles:
        raw = word.encode("utf-8")
        out += struct.pack("<HBB", sid, rank, len(raw)) + raw
    for sylls, word, rank in phrases:
        # phrases 入参已是由音节串反查好的 id 元组（main 侧转换一次），
        # 这里只做越界复检——再查一次表会把 id 当音节串比对而误报。
        key = tuple(sylls)
        if any(int(k) < 0 or int(k) >= len(syllables) for k in key):
            raise ValueError("词组 %r 的音节 id 越界：%r" % (word, key))
        raw = word.encode("utf-8")
        out += bytes([len(key)])
        out += struct.pack("<%dH" % len(key), *key)
        out += bytes([rank, len(raw)]) + raw
    return bytes(out)


def load_binary(blob):
    """按生成格式读回（--verify 与客户端 pinyin.py 共用同一口径）。"""
    if len(blob) < HEADER.size:
        raise ValueError("文件过短，非 XPYB")
    magic, version, syl_n, single_n, phrase_n, _res = HEADER.unpack_from(blob, 0)
    if magic != MAGIC:
        raise ValueError("魔数不符：%r" % (magic,))
    if version != VERSION:
        raise ValueError("版本不符：%d（本读取器只认 %d）" % (version, VERSION))
    pos = HEADER.size
    syllables = []
    for _ in range(syl_n):
        ln = blob[pos]
        pos += 1
        syllables.append(blob[pos:pos + ln].decode("ascii"))
        pos += ln
    singles = []
    for _ in range(single_n):
        sid, rank, ln = struct.unpack_from("<HBB", blob, pos)
        pos += 4
        singles.append((sid, blob[pos:pos + ln].decode("utf-8"), rank))
        pos += ln
    phrases = []
    for _ in range(phrase_n):
        n = blob[pos]
        pos += 1
        ids = struct.unpack_from("<%dH" % n, blob, pos)
        pos += 2 * n
        rank, ln = blob[pos], blob[pos + 1]
        pos += 2
        phrases.append((ids, blob[pos:pos + ln].decode("utf-8"), rank))
        pos += ln
    if pos != len(blob):
        raise ValueError("尾部有 %d 字节未消费（体积恒等式不成立）"
                         % (len(blob) - pos))
    return syllables, singles, phrases


def verify(syllables, singles, phrases, blob):
    """回读 + 结构校验：条数、rank 连续性、音节合法、解码无损。"""
    r_syl, r_single, r_phrase = load_binary(blob)
    assert r_syl == syllables, "音节表回读不一致"
    assert r_single == singles, "单字表回读不一致"
    assert r_phrase == phrases, "词组库回读不一致"

    def group_ok(rows, key_of):
        groups = {}
        for row in rows:
            groups.setdefault(key_of(row), []).append(row[-1])
        bad = [k for k, ranks in groups.items()
               if ranks != list(range(1, len(ranks) + 1))]
        return bad

    bad = group_ok(singles, lambda r: r[0])
    if bad:
        raise ValueError("单字表频序不连续：%d 组（首个 %r）" % (len(bad), bad[0]))
    bad = group_ok(phrases, lambda r: r[0])
    if bad:
        raise ValueError("词组库频序不连续：%d 组（首个 %r）" % (len(bad), bad[0]))
    return True


def main(argv=None):
    ap = argparse.ArgumentParser(description="生成客户端拼音资产 XPYB")
    ap.add_argument("--src", default=DEFAULT_SRC, help="XPinyinTable.c")
    ap.add_argument("--phrases", default=DEFAULT_PHRASES,
                    help="phrases_zh.txt 词组源")
    ap.add_argument("--out", default=DEFAULT_OUT, help="输出 .bin")
    ap.add_argument("--verify", action="store_true",
                    help="生成后回读校验（结构/频序/无损）")
    args = ap.parse_args(argv)

    sys.stdout.reconfigure(encoding="utf-8", errors="replace")
    text = open(args.src, encoding="utf-8-sig").read()
    syllables = parse_syllables(text)
    singles = parse_single_entries(text)
    raw_phrases = parse_phrase_txt(args.phrases)

    phrase_rows = []
    for sylls, word, rank in raw_phrases:
        ids = tuple(syllables.index(s) if s in syllables else -1
                    for s in sylls)
        if any(i < 0 for i in ids):
            missing = [s for s, i in zip(sylls, ids) if i < 0]
            raise ValueError("词组 %r 含表外音节 %r" % (word, missing))
        phrase_rows.append((ids, word, rank))

    blob = build_binary(syllables, singles, phrase_rows)
    os.makedirs(os.path.dirname(args.out), exist_ok=True)
    with open(args.out, "wb") as fh:
        fh.write(blob)

    print("音节 %d / 单字 %d / 词组 %d → %s（%d 字节）"
          % (len(syllables), len(singles), len(phrase_rows), args.out,
             len(blob)))
    if args.verify:
        try:
            verify(syllables, singles, phrase_rows, blob)
        except AssertionError as exc:
            print("[FAIL] %s" % exc)
            return 1
        print("[OK] 回读一致：条数/频序连续性/无尾部残留")
    return 0


if __name__ == "__main__":
    sys.exit(main())
