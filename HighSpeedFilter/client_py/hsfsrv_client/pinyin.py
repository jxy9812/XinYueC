# -*- coding: utf-8 -*-
"""全键拼音输入法引擎（1:1 对齐 C 框架 XPinyinEngine 语义）。

[C 端对应] Src/XGui/VirtualKeyboard/XPinyinEngine.c —— 组串状态机与候选
装配规则逐条移植，候选**逐字一致**（同一份数据资产、同一套切分判据）。
差别只在实现形态：C 是零拷贝借用区间，Python 是列表切片；语义等价。

[为什么不自己写一套拼音] 客户端界面与 C 端板子必须候选一致，否则联调
时「同一个词两边候选不同」会被当成 bug 排查。数据由
tools/pinyin_gen/gen_pinyin_asset.py 从 C 源（XPinyinTable.c 单字表 +
Library/VirtualKeyboard/phrases_zh.txt 词组库）编译，本模块只做查询。

核心语义（与 C 逐条对应，改这里必须同步改 C，反之亦然）：

1. **INV2 组串不变式**：组串恒为「完整音节序列前缀 + 至多一个未完音节
   前缀」。判据 = ∃k∈[0,L]：s[0..k) 可恰好切成合法音节序列，且 s[k..)
   是某个合法音节的前缀。k=0 项退化为「整串是某音节前缀」。
   因此 `nihao`、`zhongguo` 这类跨音节组串可组。

2. **非法输入截断**：新字母使组串非法时，对逐后缀跑同一判据，截断到
   **最长合法后缀**（不做自动提交——自动提交等于替用户猜字，错字静默）。

3. **候选两段式**：切分路径枚举（最长音节优先 DFS，上限 8 路）→
   ①词组段：每一路查词组库，按路序并集、UTF-8 去重；
   ②单字段：**仅首路**（主读法）各音节查单字表，顺序拼接，相邻相同
   区间合并。词组在前、单字垫后，各段组内频序。

4. **提交四路**：点选候选 / 数字 1..9（当前页第 n 个）/ 空格（全局首
   候选；无候选时提交原字母串）/ 回车（原字母串）。
"""

import bisect
import os
import struct

#: 组串容量（字母数上限，同 C 的 XKEYBOARD_IME_BUFFER_CAP）。
BUFFER_CAP = 15
#: 切分路径枚举上限（同 C 的 XKEYBOARD_IME_PATH_MAX）。
PATH_MAX = 8
#: 单页候选上限（九宫格数字键 1..9 直选）。
PAGE_MAX = 9
#: 最长音节字母数（zhuang/chuang/shuang）。
SYLLABLE_LEN_MAX = 6

#: feed* 返回值（同 C 的 XPinyinEngineFeed）。
FEED_IGNORED = 0        #: 未消费：调用方走原语义
FEED_CONSUMED = 1       #: 已消费，无上屏
FEED_COMMITTED = 2      #: 已消费且产出上屏串

#: 默认资产路径（包内 data/，随 deploy.sh 一起上传）。
DEFAULT_ASSET = os.path.join(os.path.dirname(os.path.abspath(__file__)),
                             "data", "pinyin_zh.bin")

MAGIC = b"XPYB"
VERSION = 1
_HEADER = struct.Struct("<4sHHIII")


class PinyinLexicon(object):
    """拼音数据资产（单字表 + 词组库 + 音节表）。

    载入一次进程内复用；载入失败不抛异常（`ready` 为 False，引擎退化为
    「无候选」——组串照常显示、回车提交原串，绝不因为缺资产把输入框搞
    成不能打字）。
    """

    def __init__(self, path=None):
        self.path = path or DEFAULT_ASSET
        self.syllables = []          # 按字典序，下标即音节 id
        self.singles = {}            # sid -> [(词, rank)]
        self.phrases = {}            # (sid,...) -> [(词, rank)]
        self.error = None
        try:
            self._load()
        except Exception as exc:                        # noqa: BLE001
            self.error = str(exc)
            self.syllables = []
            self.singles = {}
            self.phrases = {}
            return
        # 前缀查询用的哈希集合（音节全量 412 条，逐条登记开销极小）。
        self._syllable_set = set(self.syllables)

    # ------------------------------------------------------------------
    @property
    def ready(self):
        return bool(self.syllables)

    def _load(self):
        with open(self.path, "rb") as fh:
            blob = fh.read()
        if len(blob) < _HEADER.size:
            raise ValueError("资产过短（%d 字节）：%s" % (len(blob), self.path))
        magic, version, syl_n, single_n, phrase_n, _res = \
            _HEADER.unpack_from(blob, 0)
        if magic != MAGIC:
            raise ValueError("魔数不符 %r（期望 %r）" % (magic, MAGIC))
        if version != VERSION:
            raise ValueError("版本 %d 不受支持（期望 %d）" % (version, VERSION))
        pos = _HEADER.size
        syllables = []
        for _ in range(syl_n):
            ln = blob[pos]
            pos += 1
            syllables.append(blob[pos:pos + ln].decode("ascii"))
            pos += ln
        singles = {}
        for _ in range(single_n):
            sid, rank, ln = struct.unpack_from("<HBB", blob, pos)
            pos += 4
            word = blob[pos:pos + ln].decode("utf-8")
            pos += ln
            if sid >= len(syllables):
                raise ValueError("单字条目音节 id %d 越界" % sid)
            singles.setdefault(sid, []).append((word, rank))
        phrases = {}
        for _ in range(phrase_n):
            n = blob[pos]
            pos += 1
            ids = struct.unpack_from("<%dH" % n, blob, pos)
            pos += 2 * n
            rank, ln = blob[pos], blob[pos + 1]
            pos += 2
            word = blob[pos:pos + ln].decode("utf-8")
            pos += ln
            phrases.setdefault(ids, []).append((word, rank))
        if pos != len(blob):
            raise ValueError("尾部残留 %d 字节（体积恒等式不成立）"
                             % (len(blob) - pos))
        self.syllables = syllables
        self.singles = singles
        self.phrases = phrases

    # ------------------------------------------------------------------
    def id_of(self, syllable):
        """音节串 → id（二分；未收录返回 -1）。"""
        i = bisect.bisect_left(self.syllables, syllable)
        if i < len(self.syllables) and self.syllables[i] == syllable:
            return i
        return -1

    def is_legal(self, syllable):
        """是否合法音节（精确匹配）。"""
        return syllable in self._syllable_set

    def has_prefix(self, prefix):
        """是否存在以 prefix 开头的合法音节（C 的 hasSyllablePrefix）。"""
        if prefix == "":
            return True
        for ch in prefix:
            if ch < "a" or ch > "z":
                return False
        i = bisect.bisect_left(self.syllables, prefix)
        return (i < len(self.syllables)
                and self.syllables[i].startswith(prefix))

    def words_of_syllable(self, sid):
        """音节 id → 单字候选 [(词, rank)]。"""
        return self.singles.get(sid, ())

    def words_of_phrase(self, ids):
        """音节 id 序列 → 词组候选 [(词, rank)]。"""
        return self.phrases.get(tuple(ids), ())


class PinyinEngine(object):
    """拼音组串状态机（对应 C 的 XPinyinEngine）。

    只用公开方法操作；字段名与 C 结构体同序，便于双向对照阅读。
    """

    def __init__(self, lexicon=None, chinese=True):
        self.lexicon = lexicon if lexicon is not None else _default_lexicon()
        self.chinese = bool(chinese)
        self.reset()

    # ------------------------------------------------------------------
    #                            查询
    # ------------------------------------------------------------------
    @property
    def composing(self):
        return bool(self._buffer)

    def composing_text(self):
        return self._buffer

    def is_chinese(self):
        return self.chinese

    def set_chinese(self, chinese):
        self.chinese = bool(chinese)
        self.reset()

    def commit_string(self):
        return self._commit

    def candidate_count(self):
        return self._candidate_total

    def candidates(self):
        """当前页候选文本列表（最多 PAGE_MAX 条）。"""
        out = []
        start = self._page * PAGE_MAX
        for i in range(start, min(start + PAGE_MAX, len(self._candidates))):
            out.append(self._candidates[i])
        return out

    def all_candidates(self):
        """全部候选（跨页，供测试与调试断言）。"""
        return list(self._candidates)

    def page_index(self):
        return self._page

    def page_count(self):
        count = len(self._candidates)
        return (count + PAGE_MAX - 1) // PAGE_MAX

    # ------------------------------------------------------------------
    #                            状态迁移
    # ------------------------------------------------------------------
    def reset(self):
        """清组串与候选（提交后/切中英/取消时调用）。"""
        self._buffer = ""
        self._candidates = []
        self._candidate_total = 0
        self._page = 0
        self._commit = ""

    def feed_letter(self, letter):
        """字母键（中文态）。返回 FEED_IGNORED 表示调用方应直写编辑框。"""
        if not self.chinese:
            return FEED_IGNORED
        if not ("a" <= letter <= "z"):
            return FEED_IGNORED
        if len(self._buffer) >= BUFFER_CAP:
            return FEED_CONSUMED
        candidate = self._buffer + letter
        if self._acceptable(candidate):
            self._buffer = candidate
            self._refresh()
            return FEED_CONSUMED
        # 非法：截断到最长合法后缀（不做自动提交，见模块头注 §2）。
        for cut in range(1, len(candidate)):
            if self._acceptable(candidate[cut:]):
                self._buffer = candidate[cut:]
                self._refresh()
                return FEED_CONSUMED
        self.reset()
        return FEED_CONSUMED

    def feed_backspace(self):
        """退格。组串为空返回 FEED_IGNORED（调用方删编辑框字符）。"""
        if not self.chinese:
            return FEED_IGNORED
        if not self._buffer:
            return FEED_IGNORED
        self._buffer = self._buffer[:-1]
        self._refresh()
        return FEED_CONSUMED

    def feed_commit_first(self):
        """空格：有候选提交首候选，无候选提交原字母串。"""
        if not self.chinese or not self.composing:
            return FEED_IGNORED
        text = self._candidates[0] if self._candidates else self._buffer
        return self._commit_text(text)

    def feed_commit_raw(self):
        """回车：提交原字母串（不猜字）。"""
        if not self.chinese or not self.composing:
            return FEED_IGNORED
        return self._commit_text(self._buffer)

    def feed_digit(self, digit):
        """数字 1..9：选当前页第 n 个候选。"""
        if not self.chinese or not self.composing:
            return FEED_IGNORED
        if not 1 <= digit <= PAGE_MAX:
            return FEED_IGNORED
        index = self._page * PAGE_MAX + (digit - 1)
        if index >= len(self._candidates):
            return FEED_CONSUMED          # 越界/无候选：吞掉不写编辑框
        return self._commit_text(self._candidates[index])

    def feed_candidate(self, index):
        """点选候选（全量 0 基下标）。"""
        if not self.chinese:
            return FEED_IGNORED
        if not 0 <= index < len(self._candidates):
            return FEED_IGNORED
        return self._commit_text(self._candidates[index])

    def page_next(self):
        if self._page + 1 >= self.page_count():
            return False
        self._page += 1
        return True

    def page_prev(self):
        if self._page <= 0:
            return False
        self._page -= 1
        return True

    # ------------------------------------------------------------------
    #                         内部：切分与装配
    # ------------------------------------------------------------------
    def _legal_span(self, span):
        return (1 <= len(span) <= SYLLABLE_LEN_MAX
                and self.lexicon.is_legal(span))

    def _acceptable(self, text):
        """INV2 接受判据（见模块头注 §1）。"""
        n = len(text)
        if n > BUFFER_CAP:
            return False
        reachable = [False] * (n + 1)
        reachable[0] = True
        for k in range(1, n + 1):
            for j in range(max(0, k - SYLLABLE_LEN_MAX), k):
                if reachable[j] and self._legal_span(text[j:k]):
                    reachable[k] = True
                    break
        for k in range(0, n + 1):
            if reachable[k] and self.lexicon.has_prefix(text[k:]):
                return True
        return False

    def _enum_paths(self, text):
        """切分路径枚举：最长音节优先 DFS，上限 PATH_MAX 路（首路=主读法）。"""
        paths = []
        n = len(text)

        def dfs(pos, acc):
            if len(paths) >= PATH_MAX:
                return
            if pos == n:
                paths.append(tuple(acc))
                return
            for ln in range(min(SYLLABLE_LEN_MAX, n - pos), 0, -1):
                span = text[pos:pos + ln]
                if self.lexicon.is_legal(span):
                    acc.append(self.lexicon.id_of(span))
                    dfs(pos + ln, acc)
                    acc.pop()
                    if len(paths) >= PATH_MAX:
                        return

        dfs(0, [])
        return paths

    def _refresh(self):
        """按当前组串重算候选并回第 0 页。"""
        self._candidates = []
        self._candidate_total = 0
        self._page = 0
        if not self._buffer:
            return
        paths = self._enum_paths(self._buffer)
        if not paths:
            return
        regions = []                       # 每段 = [(词, rank)]

        # ① 词组段：逐路查询、按路序并集、UTF-8 去重。
        seen = set()
        for path in paths:
            hits = self.lexicon.words_of_phrase(path)
            if not hits:
                continue
            fresh = []
            for word, rank in hits:
                if word in seen:
                    continue
                fresh.append((word, rank))
            if fresh:
                seen.update(w for w, _r in fresh)
                regions.append(fresh)

        # ② 单字段：仅首路（主读法），各音节顺序拼接，相邻相同区间合并。
        prev_ids = None
        for sid in paths[0]:
            hits = self.lexicon.words_of_syllable(sid)
            if not hits:
                continue
            if sid == prev_ids:
                continue
            # 词组段里已有的单字不再重复（C 端单字段不做跨段去重——词组
            # 是整词、单字是单字，同字共存是预期行为，故此处不加过滤）。
            regions.append(list(hits))
            prev_ids = sid

        self._candidates = [word for region in regions for word, _rank in region]
        self._candidate_total = len(self._candidates)

    def _commit_text(self, text):
        # reset 会清 _commit，故先复位再落上屏串。
        self.reset()
        self._commit = text[:BUFFER_CAP]
        return FEED_COMMITTED


# ============================================================================
#                        进程内共享词库（懒加载）
# ============================================================================

_LEXICON = None


def _default_lexicon():
    global _LEXICON
    if _LEXICON is None:
        path = os.environ.get("HSF_PINYIN_ASSET") or DEFAULT_ASSET
        _LEXICON = PinyinLexicon(path)
    return _LEXICON


def lexicon():
    """取进程内共享词库（测试可经 HSF_PINYIN_ASSET 换资产）。"""
    return _default_lexicon()
