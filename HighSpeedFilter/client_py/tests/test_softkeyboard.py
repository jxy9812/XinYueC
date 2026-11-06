# -*- coding: utf-8 -*-
"""软键盘（拼音 / 字母 / 数字三类）+ 拼音引擎实际测试。

    python tests/test_softkeyboard.py

测什么（用户要求：触摸屏无键盘部署，拼音/数字/字母三类键盘都要能用，
同一套焦点驱动、由调用方告知要哪种键盘）：
  A. 环境探测开关（HSF_TOUCH 强制开/关）
  B. 键盘类型声明：显式 API / inputMethodHints / 缺省，及优先级
  C. 拼音引擎（数据与 C 端同源）：切分、跨音节组串、候选两段式、提交四路
  D. 焦点驱动：输入框聚焦 → 弹；换框 → 重绑并换键盘类型；点别处 → 收
  E. 真实按键点击：拼音选词 / 英文字母 / 数字框写入 / 退格 / 翻页
  F. 排除标记（TimePickerEdit 不弹）
"""

import os
import sys

os.environ["QT_QPA_PLATFORM"] = "offscreen"
os.environ["HSF_TOUCH"] = "1"                 # 强制开（桌面机探测为关）
sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))

from PyQt5.QtCore import QEvent, QPoint, Qt                    # noqa: E402
from PyQt5.QtGui import QMouseEvent                            # noqa: E402
from PyQt5.QtWidgets import (QApplication, QLineEdit, QPushButton,  # noqa: E402
                             QSpinBox, QWidget)

PASS = 0
FAIL = 0


def check(name, ok, detail=""):
    global PASS, FAIL
    if ok:
        PASS += 1
        print("  [PASS] %s %s" % (name, detail))
    else:
        FAIL += 1
        print("  [FAIL] %s %s" % (name, detail))


# ============================================================================
#                          A. 自动探测
# ============================================================================

def part_a_probe():
    print("== A. 自动探测 ==")
    from hsfsrv_client import softkeyboard as sk
    check("A1 HSF_TOUCH=1 → 强制开", sk.touch_default_enabled() is True)
    saved = os.environ.get("HSF_TOUCH")
    os.environ["HSF_TOUCH"] = "0"
    try:
        check("A2 HSF_TOUCH=0 → 强制关", sk.touch_default_enabled() is False)
    finally:
        os.environ["HSF_TOUCH"] = saved


# ============================================================================
#                 B. 键盘类型声明（调用方告知要哪种键盘）
# ============================================================================

def part_b_kinds():
    print("== B. 键盘类型声明 ==")
    from hsfsrv_client import softkeyboard as sk

    plain = QLineEdit()
    check("B1 普通输入框缺省 = 拼音键盘",
          sk.keyboard_kind_of(plain) == (sk.KIND_PINYIN, None))

    latin = QLineEdit()
    latin.setInputMethodHints(Qt.ImhLatinOnly)
    check("B2 ImhLatinOnly → 字母键盘",
          sk.keyboard_kind_of(latin) == (sk.KIND_ALPHA, None))

    pwd = QLineEdit()
    pwd.setEchoMode(QLineEdit.Password)
    check("B3 密码框（自带 HiddenText）→ 字母键盘",
          sk.keyboard_kind_of(pwd) == (sk.KIND_ALPHA, None))

    spin = QSpinBox()
    spin.setRange(0, 2000)
    check("B4 QSpinBox（Qt 自带 DigitsOnly）→ 纯数字盘",
          sk.keyboard_kind_of(spin) == (sk.KIND_NUMERIC, "digits"))

    nums = QLineEdit()
    nums.setInputMethodHints(Qt.ImhPreferNumbers)
    check("B5 ImhPreferNumbers → 数字键盘（带符号）",
          sk.keyboard_kind_of(nums) == (sk.KIND_NUMERIC, "number"))

    dial = QLineEdit()
    dial.setInputMethodHints(Qt.ImhDialableCharactersOnly)
    check("B6 ImhDialableCharactersOnly → 拨号盘",
          sk.keyboard_kind_of(dial) == (sk.KIND_NUMERIC, "dialpad"))

    forced = QLineEdit()
    sk.set_keyboard_kind(forced, sk.KIND_NUMERIC)
    check("B7 显式声明 numeric（覆盖缺省拼音）",
          sk.keyboard_kind_of(forced) == (sk.KIND_NUMERIC, "number"))

    forced2 = QLineEdit()
    forced2.setInputMethodHints(Qt.ImhDigitsOnly)
    sk.set_keyboard_kind(forced2, sk.KIND_ALPHA)
    check("B8 显式声明优先于 hints",
          sk.keyboard_kind_of(forced2) == (sk.KIND_ALPHA, None))

    none_w = QLineEdit()
    sk.set_keyboard_kind(none_w, sk.KIND_NONE)
    check("B9 显式声明 none → 不要键盘",
          sk.keyboard_kind_of(none_w) == (sk.KIND_NONE, None))

    bad = QLineEdit()
    try:
        sk.set_keyboard_kind(bad, "klingon")
        check("B10 未知类型被拒", False)
    except ValueError:
        check("B10 未知类型被拒", True)


# ============================================================================
#                      C. 拼音引擎（与 C 端同源数据）
# ============================================================================

def part_c_engine():
    print("== C. 拼音引擎 ==")
    from hsfsrv_client import pinyin as py

    lex = py.lexicon()
    check("C0 词库就绪（412 音节）", lex.ready and len(lex.syllables) == 412,
          "err=%s" % lex.error)
    check("C0a 词库资产路径存在", os.path.isfile(lex.path), lex.path)

    eng = py.PinyinEngine(lex)

    # ---- 单音节 ----
    for ch in "shi":
        eng.feed_letter(ch)
    cands = eng.all_candidates()
    check("C1 'shi' 组串成立", eng.composing_text() == "shi")
    check("C2 'shi' 首候选=是", cands and cands[0] == "是", str(cands[:3]))
    check("C3 'shi' 多页（>9 候选）", len(cands) > 9 and eng.page_count() > 1,
          "共 %d 条 / %d 页" % (len(cands), eng.page_count()))

    # ---- 翻页 + 数字选词 ----
    first_page = eng.candidates()
    eng.page_next()
    second_page = eng.candidates()
    check("C4 翻页后候选不同", first_page[0] != second_page[0],
          "%s → %s" % (first_page[:2], second_page[:2]))
    eng.page_prev()
    want = eng.candidates()[2]
    feed = eng.feed_digit(3)
    check("C5 数字 3 选当前页第 3 个",
          feed == py.FEED_COMMITTED and eng.commit_string() == want,
          "commit=%r want=%r" % (eng.commit_string(), want))

    # ---- 跨音节组串 ----
    eng.reset()
    for ch in "nihao":
        eng.feed_letter(ch)
    check("C6 跨音节 'nihao' 首候选=你好",
          eng.all_candidates()[:1] == ["你好"], str(eng.all_candidates()[:4]))

    eng.reset()
    for ch in "zhongguo":
        eng.feed_letter(ch)
    check("C7 'zhongguo' 首候选=中国",
          eng.all_candidates()[:1] == ["中国"], str(eng.all_candidates()[:4]))

    # ---- 切分歧义：xian 既可 xi+an 也可 xian ----
    eng.reset()
    for ch in "xian":
        eng.feed_letter(ch)
    cands = eng.all_candidates()
    check("C8 'xian' 词组段在单字段之前（西安 先 …）",
          cands[:2] == ["西安", "先"], str(cands[:4]))

    # ---- 非法输入：截断到最长合法后缀，不自动提交 ----
    eng.reset()
    eng.feed_letter("z")
    feed = eng.feed_letter("q")             # q 不能接在 z 后面
    check("C9 非法字母被吞，组串回到合法后缀",
          feed == py.FEED_CONSUMED and eng.composing_text() == "q",
          "composing=%r" % eng.composing_text())

    # ---- 退格 ----
    eng.reset()
    for ch in "nih":
        eng.feed_letter(ch)
    eng.feed_backspace()
    check("C10 退格删一个字母", eng.composing_text() == "ni")
    eng.feed_backspace()
    eng.feed_backspace()
    check("C11 组串删空后不再消费（透传编辑框退格）",
          eng.feed_backspace() == py.FEED_IGNORED)

    # ---- 提交通道 ----
    eng.reset()
    for ch in "nihao":
        eng.feed_letter(ch)
    eng.feed_commit_first()
    check("C12 空格提交首候选", eng.commit_string() == "你好")

    eng.reset()
    for ch in "zzz":                        # 逐键截断后组串只剩合法后缀
        eng.feed_letter(ch)
    raw = eng.composing_text()
    eng.feed_commit_raw()
    check("C13 回车提交原串（无候选不猜字）", eng.commit_string() == raw,
          "组串=%r 提交=%r" % (raw, eng.commit_string()))

    eng.reset()
    for ch in "shi":
        eng.feed_letter(ch)
    eng.feed_commit_raw()
    check("C14 有候选时回车仍提交原字母串",
          eng.commit_string() == "shi", repr(eng.commit_string()))

    # ---- 中英切换 ----
    eng.reset()
    eng.set_chinese(False)
    check("C15 英文态字母不消费（直写编辑框）",
          eng.feed_letter("n") == py.FEED_IGNORED)
    eng.set_chinese(True)
    check("C16 切回中文态可组串",
          eng.feed_letter("n") == py.FEED_CONSUMED
          and eng.composing_text() == "n")

    # ---- 组串容量 ----
    eng.reset()
    long_ok = all(eng.feed_letter("a") == py.FEED_CONSUMED for _ in range(20))
    check("C17 组串不超过容量 15", long_ok and len(eng.composing_text()) <= 15,
          "len=%d" % len(eng.composing_text()))

    # ---- 与 C 端资产同源校验（频序连续 / 无表外 id 由生成器保证）----
    syl_ids = set(lex.singles)
    check("C18 单字表覆盖主要音节", len(syl_ids) > 380,
          "%d/412 音节有候选" % len(syl_ids))
    check("C19 词组库非空（>3000 条）",
          sum(len(v) for v in lex.phrases.values()) > 3000,
          "%d 条 / %d 组" % (sum(len(v) for v in lex.phrases.values()),
                            len(lex.phrases)))


# ============================================================================
#                           D/E. 面板交互
# ============================================================================

class Harness(object):
    """把软键盘装到一个宿主窗口上，提供 focus/press 辅助。"""

    def __init__(self, app):
        from hsfsrv_client import softkeyboard as sk
        self.sk = sk
        self.app = app
        self.host = QWidget()
        self.host.resize(1280, 860)
        self.edit = QLineEdit(self.host)
        self.edit.setGeometry(20, 20, 420, 36)
        self.spin = QSpinBox(self.host)
        self.spin.setRange(0, 2000)
        self.spin.setSuffix(" ms")
        self.spin.setValue(200)
        self.spin.setGeometry(20, 70, 220, 36)
        self.latin = QLineEdit(self.host)
        self.latin.setInputMethodHints(Qt.ImhLatinOnly)
        self.latin.setGeometry(20, 130, 300, 36)
        self.plain_area = QWidget(self.host)
        self.plain_area.setGeometry(20, 200, 300, 120)
        self.host.show()
        app.setActiveWindow(self.host)
        app.processEvents()
        self.kb = sk.SoftKeyboard()
        self.kb.attach(app, self.host)
        app.processEvents()
        app.processEvents()

    def focus(self, widget):
        # 真实触摸点击会把窗口激活、焦点落到被点的控件；离屏下要显式
        # 激活宿主窗，否则窗口级焦点一直是 None，FocusIn 不来。
        widget.setFocus(Qt.OtherFocusReason)
        widget.window().activateWindow()
        self.app.processEvents()
        self.app.processEvents()

    def keys(self):
        """键区里所有可点按键。

        [遍历口径] 键区结构是「行外框 QWidget → 行布局 → 键」：行外框是
        **控件**（为了钉住行高，见 softkeyboard._render_page 的说明），
        其内部才是布局。故遍历要同时处理布局项和控件项 —— 只递归 layout
        会漏掉全部按键（实测：返回空列表，连拼音都没法点）。
        """
        out = []

        def walk(layout):
            for i in range(layout.count()):
                item = layout.itemAt(i)
                sub = item.layout()
                if sub is not None:
                    walk(sub)
                    continue
                w = item.widget()
                if w is None:
                    continue
                if isinstance(w, QPushButton):
                    out.append(w)
                    continue
                # 行外框控件：钻进去看它的布局
                if w.layout() is not None:
                    walk(w.layout())

        walk(self.kb._rows_box)
        return out

    def key_labels(self):
        return [w.text() for w in self.keys()]

    def press(self, label):
        """点一枚「软件按键」（等价于手指戳）。返回是否找到。"""
        for w in self.keys():
            if w.text() == label:
                w.click()
                self.app.processEvents()
                return True
        return False

    def press_all(self, text):
        missing = [ch for ch in text if not self.press(ch)]
        return not missing

    def band_labels(self):
        """候选带按钮文本（含中/EN 与翻页标记）——测试/诊断用。"""
        out = []
        for i in range(self.kb._band_box.count()):
            w = self.kb._band_box.itemAt(i).widget()
            if isinstance(w, QPushButton):
                out.append(w.text())
        return out

    def band_candidates(self):
        """候选带上的候选词（排除中/EN 模式 chip 与翻页箭头）。"""
        out = []
        for i in range(self.kb._band_box.count()):
            w = self.kb._band_box.itemAt(i).widget()
            if isinstance(w, QPushButton) and \
                    w.objectName() != "hsf_kb_mode_chip" and \
                    w.text() not in ("‹", "›"):
                out.append(w.text())
        return out

    def click_band(self, label):
        """点候选带上的控件（模式 chip 优先按 objectName 精确定位）。"""
        for i in range(self.kb._band_box.count()):
            w = self.kb._band_box.itemAt(i).widget()
            if isinstance(w, QPushButton) and w.text() == label:
                if label in ("中", "EN") and \
                        w.objectName() != "hsf_kb_mode_chip":
                    continue                # 候选词恰好是「中」时别点错
                w.click()
                self.app.processEvents()
                return True
        return False

    def click_candidate(self, word):
        """点候选带上文本为 word 的候选 chip。"""
        for i in range(self.kb._band_box.count()):
            w = self.kb._band_box.itemAt(i).widget()
            if isinstance(w, QPushButton) and w.text() == word and \
                    w.objectName() != "hsf_kb_mode_chip":
                w.click()
                self.app.processEvents()
                return True
        return False


def part_d_focus(app):
    print("== D. 焦点驱动 ==")
    h = Harness(app)

    # [装配即焦点] attach 时若已有输入框持焦，面板应直接出来。
    h.focus(h.edit)
    check("D1 输入框聚焦 → 拼音面板弹出",
          h.kb.isVisible() and h.kb._kind == h.sk.KIND_PINYIN,
          "kind=%s" % h.kb._kind)
    check("D2 目标 = 该输入框", h.kb._live_target() is h.edit)

    h.focus(h.spin)
    check("D3 换到数字框 → 面板重绑为纯数字盘",
          h.kb.isVisible() and h.kb._kind == h.sk.KIND_NUMERIC
          and h.kb._numeric_layout == "digits"
          and h.kb._live_target() is h.spin,
          "kind=%s/%s" % (h.kb._kind, h.kb._numeric_layout))

    h.focus(h.latin)
    check("D4 换到拉丁框 → 字母键盘（ABC/abc 布局）",
          h.kb.isVisible() and h.kb._kind == h.sk.KIND_ALPHA
          and h.kb._live_target() is h.latin, "kind=%s" % h.kb._kind)

    # 换框不得把上一框的组串带过来（会误上屏到新框）。
    h.focus(h.edit)
    h.press_all("nihao")
    check("D5 拼音框组串中", h.kb._engine.composing_text() == "nihao")
    h.focus(h.spin)
    check("D6 换框清组串", h.kb._engine.composing_text() == "")

    # 点面板外的普通区域 → 收起
    press_ev = QMouseEvent(QEvent.MouseButtonPress, QPoint(5, 5),
                           Qt.LeftButton, Qt.LeftButton, Qt.NoModifier)
    app.sendEvent(h.plain_area, press_ev)
    app.processEvents()
    check("D7 点面板外非输入区 → 收起", not h.kb.isVisible())

    # 点「收起」键
    h.focus(h.edit)
    h.press("收起")
    check("D8 收起键隐藏面板", not h.kb.isVisible())

    return h


def part_e_typing(app, h):
    print("== E. 真实按键点击 ==")
    # ---- E1 拼音选词（首候选）----
    h.focus(h.edit)
    h.edit.clear()
    h.press_all("nihao")
    check("E1a 组串显示 nihao", h.kb._engine.composing_text() == "nihao")
    check("E1b 候选带出现候选词",
          "你好" in h.band_candidates(), str(h.band_candidates()[:4]))
    h.press(" ")
    check("E1c 空格上屏首候选", h.edit.text() == "你好", repr(h.edit.text()))

    # ---- E2 点候选 chip（单字「中」——同时验证不与模式 chip 混淆）----
    h.press_all("zhongguo")
    cands = h.band_candidates()
    check("E2a 候选带含「中国」", "中国" in cands, str(cands[:4]))
    if "中" in cands:
        h.click_candidate("中")
        check("E2b 点单字候选上屏", h.edit.text() == "你好中",
              repr(h.edit.text()))
    else:
        check("E2b 点单字候选上屏", False, "候选带无「中」：%r" % (cands,))
    h.edit.clear()

    # ---- E3 翻页 + 数字选词 ----
    h.press_all("shi")
    first = h.kb._engine.candidates()[0]
    h.click_band("›")
    check("E3a 翻页后候选变化",
          h.kb._engine.candidates()[0] != first,
          "%s → %s" % (first, h.kb._engine.candidates()[0]))
    page_cands = h.kb._engine.candidates()
    h.press("123")
    h.press("2")
    check("E3b 数字键选当前页第 2 个", h.edit.text() == page_cands[1],
          "实际=%r 期望=%r" % (h.edit.text(), page_cands[1]))
    h.press("拼音")
    check("E3c 拼音键回字母布局", h.kb._pinyin_page == "letters")

    # ---- E4 退格（组串优先）----
    h.edit.clear()
    h.press_all("nih")
    h.press("退格")
    check("E4a 退格删组串字母",
          h.kb._engine.composing_text() == "ni" and h.edit.text() == "",
          "组串=%r 编辑框=%r" % (h.kb._engine.composing_text(), h.edit.text()))
    h.press("退格")
    h.press("退格")
    h.press("退格")                       # 组串已空 → 删编辑框
    check("E4b 组串空后退格落到编辑框",
          h.kb._engine.composing_text() == "" and h.edit.text() == "")

    # ---- E5 中/EN 切换 ----
    h.edit.clear()
    check("E5a 中文态", h.kb._engine.is_chinese())
    h.press("中/EN")
    check("E5b 切英文态", not h.kb._engine.is_chinese())
    h.press_all("abc")
    check("E5c 英文态字母直写编辑框", h.edit.text() == "abc",
          repr(h.edit.text()))
    h.press("中/EN")
    check("E5d 切回中文态", h.kb._engine.is_chinese())

    # ---- E6 数字框：纯数字盘 + value 同步 ----
    h.focus(h.spin)
    h.spin.setValue(200)
    h.spin.lineEdit().selectAll()
    h.press_all("600")
    check("E6a 数字盘写入 600",
          h.spin.lineEdit().text().startswith("600"), h.spin.lineEdit().text())
    check("E6b spinbox value 同步为 600", h.spin.value() == 600,
          str(h.spin.value()))
    # 纯数字盘不该出现字母键
    labels = h.key_labels()
    check("E6c 纯数字盘无字母键",
          "q" not in labels and "1" in labels, str(labels))
    # [中间态不炸] 输入低于量程下限的中间态不该被立刻 clamp 掉，
    # 否则最终落值会变成上限（实锅：逐键 interpretText 把 600 变成 2000）。
    h.spin.setValue(200)
    h.spin.lineEdit().selectAll()
    h.press("6")
    check("E6d 中间态不预先 clamp", h.spin.lineEdit().text().startswith("6"),
          h.spin.lineEdit().text())
    h.press_all("00")
    check("E6e 续敲后落值正确（600 而非被 clamp 成 2000）",
          h.spin.value() == 600, str(h.spin.value()))
    # [换框提交] 没按确定就换到别的输入框，数值也该已落地。
    h.spin.setValue(200)
    h.spin.lineEdit().selectAll()
    h.press_all("950")
    h.focus(h.edit)
    check("E6f 换框时把未解析文本提交成 value", h.spin.value() == 950,
          str(h.spin.value()))

    # ---- E7 字母键盘：Shift 大小写 + 符号盘 ----
    h.focus(h.latin)
    h.latin.clear()
    h.press_all("root")
    check("E7a 字母键盘直接出英文", h.latin.text() == "root",
          repr(h.latin.text()))
    # 字母键盘行 3 左侧是 Shift（键帽随态显示 ABC/abc，与 C 的 shift 三态
    # 同义）：点一次进大写态，键帽换大写。
    h.press("ABC" if "ABC" in h.key_labels() else "abc")
    check("E7b Shift 切出大写键帽", "A" in h.key_labels(),
          str(h.key_labels()[:6]))
    h.press("A")
    check("E7c 大写字母上屏", h.latin.text() == "rootA", repr(h.latin.text()))
    h.press("符")
    check("E7d 「符」进符号盘", h.kb._mode == "Special", h.kb._mode)
    h.press("/")
    check("E7e 符号布局可插入", h.latin.text() == "rootA/",
          repr(h.latin.text()))
    h.press("abc")
    check("E7f 符号盘「abc」回小写布局",
          h.kb._mode == "TextLower", h.kb._mode)

    # ---- E8 候选带「中/EN」chip ----
    h.focus(h.edit)
    before = h.kb._engine.is_chinese()
    h.click_band("中" if before else "EN")
    check("E8 候选带模式 chip 也能切中英",
          h.kb._engine.is_chinese() != before)


def part_f_exclude(app):
    print("== F. 排除标记 ==")
    from hsfsrv_client.pages_other import TimePickerEdit
    from hsfsrv_client import softkeyboard as sk
    picker = TimePickerEdit("日期")
    check("F1 TimePickerEdit 自动带排除标记",
          picker.property(sk.NO_SOFT_KBD) is True)
    check("F2 排除标记不参与键盘类型（不弹）",
          bool(picker.property(sk.NO_SOFT_KBD)))
    none_w = QLineEdit()
    sk.set_keyboard_kind(none_w, sk.KIND_NONE)
    check("F3 声明 none 的控件同样不弹",
          sk.keyboard_kind_of(none_w)[0] == sk.KIND_NONE)


def part_g_geometry(app):
    """G. 面板几何：底部无死白 + 切换键盘时尺寸恒定。

    [用户 2026-11-06 报的两个现象]
        "屏幕键盘调用后底部很大一块空的" —— 面板比内容高，多出来的高度
        被尾部 stretch 全沉到底部（实测拼音盘 16px + 底边距 6px = 22px）。
        "切换不同的键盘的时候边框应该没有自己收缩" —— 各键盘按自己的行数
        算高（拼音 带+4 行 = 340、数字 5 行 = 355），换类型就跳。

    修法是「槽位口径」：所有键盘共用 SLOTS 个槽（候选带也算一槽），键高
    由槽数反算 ⇒ 面板尺寸恒定、内容铺满。这里锁这两条。
    """
    from hsfsrv_client import softkeyboard as sk
    from PyQt5.QtWidgets import QLineEdit, QSpinBox, QWidget, QVBoxLayout

    host = QWidget()
    host.resize(1280, 800)
    lay = QVBoxLayout(host)
    edit = QLineEdit(host)
    lay.addWidget(edit)
    spin = QSpinBox(host)
    spin.setRange(0, 1000)
    lay.addWidget(spin)
    host.show()
    app.processEvents()

    kb = sk.SoftKeyboard()
    kb._main = host

    def settle():
        for _ in range(6):
            app.processEvents()

    def kb_buttons():
        """递归收集键区所有按键（行外框是 QWidget，要钻进去）。"""
        out = []

        def walk(layout):
            for i in range(layout.count()):
                it = layout.itemAt(i)
                sub = it.layout()
                if sub is not None:
                    walk(sub)
                    continue
                w = it.widget()
                if w is None:
                    continue
                if isinstance(w, QPushButton):
                    out.append(w)
                    continue
                if w.layout() is not None:
                    walk(w.layout())

        walk(kb._rows_box)
        return out

    heights = {}
    for tag, widget in (("pinyin", edit), ("numeric", spin)):
        kb.bind(widget)
        settle()
        heights[tag] = kb.height()
        # 内容底 = 键区容器底（容器内边距之外就是死白）
        host_bottom = kb._keys_host.y() + kb._keys_host.height()
        gap = kb.height() - host_bottom
        # 只允许 root 的底边距（6px），多出来就是死白
        margins = kb.layout().contentsMargins()
        check("G-%s 底部无死白" % tag,
              gap <= margins.top() + margins.bottom(),
              "面板 %d, 内容底 %d, 差 %d" % (kb.height(), host_bottom, gap))
        # 末行键必须贴到键区容器底部（不留空带）
        # [口径] 行外框现在是 QWidget（行高靠它钉），故按"取行控件 y+h"
        # 算末行底，而不是钻进旧的两层布局。
        last_bottom = 0
        for i in range(kb._rows_box.count()):
            it = kb._rows_box.itemAt(i)
            host_w = it.widget()
            if host_w is None:
                continue
            last_bottom = max(last_bottom, host_w.y() + host_w.height())
        dead = kb._keys_host.height() - (last_bottom
                                         + kb._rows_box.contentsMargins().bottom())
        check("G-%s 末行铺满键区" % tag, dead <= 2,
              "末行底 %d, 容器高 %d, 死白 %d"
              % (last_bottom, kb._keys_host.height(), dead))

    check("G3 两套键盘高度一致（切换时不收缩）",
          heights["pinyin"] == heights["numeric"],
          "pinyin=%d numeric=%d" % (heights["pinyin"], heights["numeric"]))

    # 键高必须都等于反算值（不许某行被压 1~2px）
    kb.bind(edit)
    settle()
    expect = kb._key_h_for_slots()
    bad = []
    # [口径] 行外框是 QWidget，键在它的布局里
    for w in kb_buttons():
        if w.height() > 3 and w.height() != expect:
            bad.append(w.height())
    check("G4 所有键高等于槽位反算值", not bad,
          "期望 %d，异常 %s" % (expect, sorted(set(bad))))

    # ---- G5：行外框必须是**控件**且固定高度 ----
    # [这一条锁的是真机缺陷 2026-11-06] 行外框原来是裸 QHBoxLayout：
    # QLayout 钉不住高度（没有 setFixedHeight），它的 sizeHint 高度按子项
    # 最小要求算 ⇒ 板端实测行高被压成 32/35px（期望 58），底部空出 77px
    # —— 用户看到的"按键下面一大块空白"红框。包一层 setFixedHeight 的
    # QWidget 后板端四行都是 58、末行死白 0。
    rows_ok = True
    detail = []
    for i in range(kb._rows_box.count()):
        it = kb._rows_box.itemAt(i)
        host_w = it.widget()
        if host_w is None:
            continue                      # 尾部 stretch
        if host_w.height() != expect or host_w.maximumHeight() != expect:
            rows_ok = False
            detail.append("row%d h=%d max=%d" % (i, host_w.height(),
                                                 host_w.maximumHeight()))
    check("G5 行外框是固定高度的 QWidget", rows_ok,
          "; ".join(detail) if detail else "期望 %d" % expect)

    # ---- G6：按键高度扣掉了 QSS 边框 ----
    # [踩过] QSS 的 min/max-height 是内容区尺寸，`border: 1px` 会再加 2px：
    # 写 58 量出来 60，面板总高与 reflow() 的算术就不一致了。故样式里写
    # h-2、控件上仍钉 h。
    border_bad = []
    for w in kb_buttons():
        if w.height() != expect:
            border_bad.append("%s h=%d" % (w.text()[:4], w.height()))
    check("G6 按键实测高度 == 槽位值（边框已补偿）", not border_bad,
          "; ".join(border_bad[:3]) if border_bad else "全部 %d" % expect)

    # ---- G7/G8：弹出即全选（用户 2026-11-06 要求）----
    # 「变频器界面输入框弹出键盘后默认选中全部，这样可以***直接重新完整
    # 输入一个值***」。全选后第一个字符就顶掉原值（QLineEdit.insert 有
    # 选区时是替换），是最少动作的输入方式。
    spin.setValue(720)
    kb.hide()
    app.processEvents()
    kb.bind(spin)                       # 弹出（面板由隐藏→显示）
    settle()
    sel = kb._adapter.editor.selectedText()
    check("G7 弹出时默认全选", sel == kb._adapter.editor.text() and sel != "",
          "选中 %r 全文 %r" % (sel, kb._adapter.editor.text()))

    # 连续按键不得再次全选，否则每敲一个字符就把已输入的覆盖掉
    kb._adapter.insert("9")             # 模拟首次按键（替换选区）
    app.processEvents()
    before = kb._adapter.editor.text()
    kb.bind(spin)                        # 同一目标再走一次 bind（翻页等路径）
    settle()
    after = kb._adapter.editor.text()
    check("G8 同一目标重复 bind 不重复全选", after == before,
          "输入 %r → 再 bind 后 %r" % (before, after))

    # 换目标才再全选（先填内容，否则 '' == '' 没有区分力）
    edit.setText("abcdef")
    kb.hide(); app.processEvents()
    kb.bind(edit)
    settle()
    sel2 = kb._adapter.editor.selectedText()
    check("G9 换目标后重新全选", sel2 == "abcdef",
          "选中 %r（期望 abcdef）" % (sel2,))

    host.close()


def part_h_x11_flag():
    """H. X11 上必须绕过窗口管理器。

    [板端实测 2026-11-06] 普通 Qt.Tool 窗口被 xfwm4 补 15px 系统标题栏
    （Qt 几何 340、X 上 355），并且一 map 就把 xfce4 面板抬到全屏窗之上，
    **系统顶栏盖住软件自己的标题栏**。加 X11BypassWindowManagerHint 后
    两个现象同时消失（三态对照图 pb_none/normal/bypass）。
    """
    from hsfsrv_client import softkeyboard as sk
    from PyQt5.QtCore import Qt

    # 非 X11 平台不加该标志（Windows 上是空操作，显式判平台让意图清楚）
    kb = sk.SoftKeyboard()
    if sk._is_x11():
        check("H1 X11 上带绕过 WM 标志",
              bool(kb.windowFlags() & Qt.X11BypassWindowManagerHint),
              hex(int(kb.windowFlags())))
    else:
        check("H1 非 X11 不带绕过 WM 标志（空操作但意图明确）",
              not bool(kb.windowFlags() & Qt.X11BypassWindowManagerHint))
    kb.close()


def main():
    app = QApplication.instance() or QApplication(sys.argv[:1])

    part_a_probe()
    part_b_kinds()
    part_c_engine()
    h = part_d_focus(app)
    part_e_typing(app, h)
    part_f_exclude(app)
    part_g_geometry(app)
    part_h_x11_flag()

    print("==== %d passed, %d failed ====" % (PASS, FAIL))
    return 1 if FAIL else 0


if __name__ == "__main__":
    sys.exit(main())
