#!/usr/bin/env python3
"""Compile the pinyin phrase text library (txt) into the XIPB binary format.

The runtime loader (Src/XGui/VirtualKeyboard/XPinyinPhrase.c) recognizes the
"XIPB" magic and takes an O(1)-per-entry fast path: no line parsing, no
sorting, just header checks + CRC + a linear walk.  This tool guarantees at
compile time everything the loader re-checks at load time (six rules, strict
ordering, group rank continuity), so bad rows can never be silently dropped.

Usage:
    python Tools/VirtualKeyboard/ime_phrases_compile.py <in.txt> <out.bin>
        [--syllable-src Src/XGui/VirtualKeyboard/XPinyinTable.c]
        [--lenient] [--verify]

The syllable whitelist is extracted from k_imeSyllables{} in
XPinyinTable.c (never a hardcoded copy) so the compiler and the runtime
table can never drift apart.  Line rules (mirroring the loader's text path;
numbers match the ruleN labels raised by validate_row):
  rule1  syllables legal: each in the whitelist, each <= 6 letters
  rule2  syllable count 2..4 (reported as "rule1/2" together with rule1)
  rule3  word field 1..15 bytes, no byte < 0x20
  rule4  rank decimal 1..255
  rule5  exactly 3 TAB-separated fields
  rule6  word field well-formed UTF-8
Blank lines and '#' comment lines are skipped silently (not counted as bad,
same accounting as the loader).  In strict mode (default) any bad row fails
the compile with all offending line numbers listed -- the shipped asset must
be clean so no word can be silently lost.  --lenient skips bad rows with a
count and a summary (mirrors the loader) and exists only for compiling the
bad-row test fixture.

Output layout (little endian, no BOM):
  magic[4]='XIPB' | version u16=1 | syllableCount u16 | entryCount u32
  | wordsLen u32 | syllableFingerprint u32 | crc32 u32 | entries*16B | words
Entry (16B): syllable id[0..3] u16*4 (count<4 padded 0) | count u8 | rank u8
  | reserved u8*2 (zero) | wordOffset u32.
CRC-32/ISO-HDLC (zlib.crc32) covers entries+words (not the header).  The
syllable fingerprint is FNV-1a 64-bit over all syllable ASCII strings in id
order joined by a single 0x00 (no trailing NUL), low 32 bits.

--verify re-reads the produced file and re-walks it exactly like the loader
(all six rules as bin equivalents) plus the size identity and checksums;
EXIT=0 on success.  See Tools/VirtualKeyboard/test_ime_phrases_compile.py for the
regenerate-and-byte-compare regression.
"""

import argparse
import re
import struct
import sys
import zlib
from pathlib import Path

MAGIC = b"XIPB"
VERSION = 1
HEADER_SIZE = 24
ENTRY_SIZE = 16
MAX_SYLLABLES = 4
MIN_SYLLABLES = 2
MAX_SYL_LETTERS = 6
MAX_WORD_BYTES = 15
MAX_RANK = 255

HEAD_FMT = "<4sHHIIII"  # magic, version, syllableCount, entryCount,
                        # wordsLen, fingerprint, crc32 -- exactly 24 bytes
ENTRY_FMT = "<4HBB2xI"  # id0..id3, count, rank, reserved, wordOffset -- 16B
assert struct.calcsize(HEAD_FMT) == HEADER_SIZE
assert struct.calcsize(ENTRY_FMT) == ENTRY_SIZE

FNV_OFFSET = 0xCBF29CE484222325
FNV_PRIME = 0x100000001B3


class BadRow(Exception):
    """A row rejected by one of the six line rules (rule id attached)."""


def fnv1a64_low32(data, h=FNV_OFFSET):
    """FNV-1a 64-bit over bytes, folded to the low 32 bits."""
    for ch in data:
        h ^= ch
        h = (h * FNV_PRIME) & 0xFFFFFFFFFFFFFFFF
    return h & 0xFFFFFFFF


def syllable_fingerprint(syllables):
    """FNV-1a64 low 32 bits over syllables in id order, 0x00-joined."""
    h = FNV_OFFSET
    for i, syl in enumerate(syllables):
        if i:
            h = fnv1a64_low32(b"\x00", h)
        h = fnv1a64_low32(syl.encode("ascii"), h)
    return h


def extract_syllables(source_path):
    """Extract k_imeSyllables{} (in id order) from XPinyinTable.c."""
    if not Path(source_path).is_file():
        raise SystemExit("error: syllable source not found: %s"
                         % source_path)
    text = Path(source_path).read_text(encoding="utf-8-sig")
    m = re.search(r"k_imeSyllables\[\]\[IME_SYLLABLE_BUF\]\s*=\s*\{(.*?)\};",
                  text, re.S)
    if not m:
        raise SystemExit("error: k_imeSyllables not found in %s" % source_path)
    syllables = re.findall(r'\{"([a-z]+)"\}', m.group(1))
    if not syllables:
        raise SystemExit("error: empty syllable table in %s" % source_path)
    if syllables != sorted(syllables):
        raise SystemExit("error: syllable table not sorted (id order broken)")
    if len(set(syllables)) != len(syllables):
        raise SystemExit("error: duplicate syllable in table")
    return syllables


def utf8_well_formed(raw):
    """Structural UTF-8 check mirroring imePhraseUtf8WellFormed()."""
    i = 0
    while i < len(raw):
        c = raw[i]
        if c < 0x80:
            i += 1
            continue
        if 0xC2 <= c <= 0xDF:
            n = 2
        elif 0xE0 <= c <= 0xEF:
            n = 3
        elif 0xF0 <= c <= 0xF4:
            n = 4
        else:
            return False  # continuation byte / overlong / 0xF5..
        if i + n > len(raw):
            return False  # truncated sequence
        for j in range(1, n):
            if raw[i + j] < 0x80 or raw[i + j] > 0xBF:
                return False
        i += n
    return True


def validate_row(line, syllable_ids):
    """Validate one data row, return (syllable tuple, word bytes, rank).

    ``line`` is the already-decoded line text (decode is row-level in
    parse_source).  Raises BadRow with the failing rule number (mirrors the
    loader's six rules; comment/blank lines never reach here).
    """
    fields = line.split("\t")
    if len(fields) != 3:
        raise BadRow("rule5: expected exactly 3 TAB fields, got %d"
                     % len(fields))
    syl_field, word_field, rank_field = fields
    syls = syl_field.split(" ")
    if not (MIN_SYLLABLES <= len(syls) <= MAX_SYLLABLES):
        raise BadRow("rule1/2: syllable count %d not in 2..4" % len(syls))
    for syl in syls:
        if not syl or len(syl) > MAX_SYL_LETTERS or syl not in syllable_ids:
            raise BadRow("rule1: illegal/overlong syllable %r" % syl)
    word = word_field.encode("utf-8")  # decoded strictly, cannot fail here
    if not (1 <= len(word) <= MAX_WORD_BYTES):
        raise BadRow("rule3: word byte length %d not in 1..15" % len(word))
    if any(b < 0x20 for b in word):
        raise BadRow("rule3: control byte (<0x20) in word")
    if not utf8_well_formed(word):
        raise BadRow("rule6: word not well-formed UTF-8")
    if not rank_field.isdigit() or not (1 <= int(rank_field) <= MAX_RANK):
        raise BadRow("rule4: rank %r not decimal 1..255" % rank_field)
    return tuple(syls), word, int(rank_field)


def parse_source(txt_path, syllable_ids, lenient):
    """Read the txt, validate rows, return (entries, bad_count).

    Entries are (syllable-id tuple, word bytes, rank).  Bad-row accounting
    mirrors the loader: blank lines and '#' comments are skipped silently
    and never counted.  Decoding is row-level (bytes split on LF, CR
    stripped, then strict UTF-8 per line) so a file containing a truncated
    sequence fails only that row -- the fixture depends on it (a whole-file
    decode would raise before any row is seen).
    """
    raw = Path(txt_path).read_bytes()
    if raw.startswith(b"\xef\xbb\xbf"):
        raw = raw[3:]  # BOM tolerated (loader strips it too)
    entries = []
    bad_rows = []  # (line number, rule text)
    for lineno, line_bytes in enumerate(raw.split(b"\n"), start=1):
        line_bytes = line_bytes.rstrip(b"\r")
        if not line_bytes or line_bytes.startswith(b"#"):
            continue  # silent skip, not counted (loader :555-556 semantics)
        try:
            line = line_bytes.decode("utf-8")
        except UnicodeDecodeError:
            bad_rows.append((lineno, "rule6: line is not valid UTF-8"))
            continue
        try:
            entries.append(validate_row(line, syllable_ids))
        except BadRow as exc:
            bad_rows.append((lineno, str(exc)))
    if bad_rows:
        if not lenient:
            detail = "\n".join("  line %d: %s" % (n, why)
                               for n, why in bad_rows)
            raise SystemExit("error: %d bad row(s) in %s (strict mode, "
                             "asset must be clean):\n%s"
                             % (len(bad_rows), txt_path, detail))
        print("[lenient] skipped %d bad row(s):" % len(bad_rows))
        for n, why in bad_rows:
            print("[lenient]   line %d: %s" % (n, why))
    return entries, len(bad_rows)


def strict_checks(entries):
    """Generation-time guarantees beyond the per-row rules.

    - group ranks are exactly 1..k consecutive (loader assumes group-internal
      rank ascending for the top-N cut; the text asset documents 1..k);
    - (syllable sequence, word) pairs are unique.
    """
    groups = {}
    seen = set()
    dups = 0
    for syls, word, _rank in entries:
        groups.setdefault(syls, []).append(_rank)
        key = (syls, word)
        if key in seen:
            dups += 1
        seen.add(key)
    broken = []
    for syls, ranks in groups.items():
        if sorted(ranks) != list(range(1, len(ranks) + 1)):
            broken.append(syls)
    if broken:
        raise SystemExit("error: %d group(s) with rank not 1..k consecutive "
                         "(first: %s)" % (len(broken),
                                          " ".join(" ".join(g)
                                                   for g in broken[:3])))
    if dups:
        raise SystemExit("error: %d duplicate (syllables, word) pair(s)"
                         % dups)


def build_blob(entries, syllables, fingerprint):
    """Serialize entries into the XIPB blob (header CRC field zeroed first)."""
    entries.sort(key=lambda e: (e[0], e[2]))  # (id tuple, rank) total order
    syllable_id = {syl: i for i, syl in enumerate(syllables)}
    words = bytearray()
    body = bytearray()
    for syls, word, rank in entries:
        ids = [syllable_id[s] for s in syls]
        ids += [0] * (MAX_SYLLABLES - len(ids))
        body += struct.pack(ENTRY_FMT, *ids, len(syls), rank, len(words))
        words += word + b"\x00"
    crc = zlib.crc32(bytes(body) + bytes(words)) & 0xFFFFFFFF
    header = struct.pack(HEAD_FMT, MAGIC, VERSION, len(syllables),
                         len(entries), len(words), fingerprint, crc)
    return header + bytes(body) + bytes(words)


def verify_blob(blob, syllables, fingerprint):
    """Re-walk the produced blob exactly like the loader (steps 1'-8)."""
    if len(blob) < HEADER_SIZE:
        raise SystemExit("verify fail: shorter than header")
    magic, version, syl_cnt, entry_cnt, words_len, fp, crc = \
        struct.unpack(HEAD_FMT, blob[:HEADER_SIZE])
    if magic != MAGIC:
        raise SystemExit("verify fail: magic")
    if version != VERSION:
        raise SystemExit("verify fail: version")
    if syl_cnt != len(syllables):
        raise SystemExit("verify fail: syllable count %d != %d"
                         % (syl_cnt, len(syllables)))
    if fp != fingerprint:
        raise SystemExit("verify fail: syllable fingerprint")
    if len(blob) != HEADER_SIZE + entry_cnt * ENTRY_SIZE + words_len:
        raise SystemExit("verify fail: size identity")
    if zlib.crc32(blob[HEADER_SIZE:]) & 0xFFFFFFFF != crc:
        raise SystemExit("verify fail: crc32")
    prev_key = None
    prev_off = -1
    words_base = HEADER_SIZE + entry_cnt * ENTRY_SIZE
    for i in range(entry_cnt):
        id0, id1, id2, id3, cnt, rank, off = \
            struct.unpack(ENTRY_FMT,
                          blob[HEADER_SIZE + i * ENTRY_SIZE:
                               HEADER_SIZE + (i + 1) * ENTRY_SIZE])
        ids = [id0, id1, id2, id3]
        if not (MIN_SYLLABLES <= cnt <= MAX_SYLLABLES):
            raise SystemExit("verify fail: entry %d count %d" % (i, cnt))
        if not (1 <= rank <= MAX_RANK):
            raise SystemExit("verify fail: entry %d rank %d" % (i, rank))
        if any(ids[j] != 0 for j in range(cnt, MAX_SYLLABLES)):
            raise SystemExit("verify fail: entry %d padding not zero"
                             % (i,))
        if any(ids[j] >= syl_cnt for j in range(cnt)):
            raise SystemExit("verify fail: entry %d id out of range" % (i,))
        if off <= prev_off or off >= words_len:
            raise SystemExit("verify fail: entry %d offset %d" % (i, off))
        if i + 1 == entry_cnt:
            end = words_len - 1  # last word: NUL carried by region tail
        else:
            nxt = struct.unpack(
                ENTRY_FMT,
                blob[HEADER_SIZE + (i + 1) * ENTRY_SIZE:
                     HEADER_SIZE + (i + 2) * ENTRY_SIZE])
            end = nxt[6] - 1  # word length = next offset - this - 1
        wlen = end - off
        if not (1 <= wlen <= MAX_WORD_BYTES):
            raise SystemExit("verify fail: entry %d word length %d"
                             % (i, wlen))
        word = blob[words_base + off: words_base + off + wlen]
        if any(b < 0x20 for b in word):
            raise SystemExit("verify fail: entry %d control byte" % (i,))
        try:
            word.decode("utf-8")
        except UnicodeDecodeError:
            raise SystemExit("verify fail: entry %d not UTF-8" % (i,))
        key = (tuple(ids[:cnt]), rank)
        if prev_key is not None and key < prev_key:
            raise SystemExit("verify fail: entry %d order" % (i,))
        prev_key = key
        prev_off = off
    return entry_cnt, words_len


def main(argv=None):
    # Repo root (Tools/VirtualKeyboard/<this file> -> parents[2]); the --syllable-src
    # default is resolved against it so the tool works from any CWD.
    default_syllable_src = (Path(__file__).resolve().parents[2]
                            / "Src/XGui/VirtualKeyboard/XPinyinTable.c")
    parser = argparse.ArgumentParser(
        description="Compile the IME phrase txt library to XIPB binary.")
    parser.add_argument("input", help="source txt (UTF-8, TAB separated)")
    parser.add_argument("output", help="output .bin path")
    parser.add_argument(
        "--syllable-src",
        default=str(default_syllable_src),
        help="path to XPinyinTable.c (syllable whitelist source; default: "
             "Src/XGui/VirtualKeyboard/XPinyinTable.c)")
    parser.add_argument("--lenient", action="store_true",
                        help="skip bad rows with a summary instead of "
                             "failing (test fixtures only)")
    parser.add_argument("--verify", action="store_true",
                        help="re-read the output and re-walk it like the "
                             "loader before exiting")
    args = parser.parse_args(argv)

    syllables = extract_syllables(args.syllable_src)
    syllable_ids = set(syllables)
    entries, _bad = parse_source(args.input, syllable_ids, args.lenient)
    if not entries and not args.lenient:
        raise SystemExit("error: no valid rows in %s" % args.input)
    strict_checks(entries)
    fingerprint = syllable_fingerprint(syllables)
    blob = build_blob(entries, syllables, fingerprint)
    Path(args.output).write_bytes(blob)
    print("[ime_phrases_compile] %s: %d entries, %d syllables, %d bytes, "
          "fingerprint=0x%08X, crc32=0x%08X"
          % (args.output, len(entries), len(syllables), len(blob),
             fingerprint,
             struct.unpack("<I", blob[20:24])[0]))
    if args.verify:
        reread = Path(args.output).read_bytes()
        if reread != blob:
            raise SystemExit("verify fail: re-read differs from written")
        entry_cnt, words_len = verify_blob(reread, syllables, fingerprint)
        print("[ime_phrases_compile] verify ok: walked %d entries, "
              "words region %d bytes" % (entry_cnt, words_len))
    return 0


if __name__ == "__main__":
    sys.exit(main())
