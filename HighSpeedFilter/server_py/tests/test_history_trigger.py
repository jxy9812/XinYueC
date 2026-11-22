# -*- coding: utf-8 -*-
"""历史入库触发方式回归（2026-11-12 用户定案）。

[测什么] 远端设置「数据存储 → 入库触发」的两种口径：

    · always（缺省）：**持续采集入库** —— 与自动模式开没开无关（既有行为）。
    · auto：**仅自动运行时入库** —— 控制器在跑才记；停机/手动期间不记。

    另外锁配置三态：缺省=always、落盘往返一致、非法值明确报错（fail-closed：
    拼错的键不能把"停机期不入库"悄悄变成"一直记"）。

[怎么跑]
    python -I tests/test_history_trigger.py          # 独立跑
    pytest tests/test_history_trigger.py -q          # pytest 收集
"""

import os
import sys

# [输出编码 2026-11-14] 中文 Windows 默认代码页 cp936 ⇒ stdout 被重定向
# 到文件时用 GBK ⇒ 打印「⇒」(U+21D2，**GBK 编不了**) 直接
# UnicodeEncodeError ⇒ **整套测试在打印断言时被打断**（实测退出码仍是 0，
# 看着像"跑完了"）。强制 UTF-8 + errors="replace"（写不出去也不崩）。
for _stream in (sys.stdout, sys.stderr):
    try:
        _stream.reconfigure(encoding="utf-8", errors="replace")
    except (AttributeError, ValueError, OSError):
        pass
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
sys.path.insert(0, os.path.dirname(HERE))

from hsfsrv import app as app_mod      # noqa: E402
from hsfsrv import config as cfgmod    # noqa: E402
from hsfsrv import proto               # noqa: E402

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


class _FakeCtl(object):
    def __init__(self, running):
        self.running = running


def _make_app(trigger, running):
    """白盒造一个 App（只塞判定要用的三样：cfg / auto_controller）。"""
    obj = app_mod.App.__new__(app_mod.App)
    cfg = cfgmod.Config()
    cfg.storage.history_trigger = trigger
    obj.cfg = cfg
    obj.auto_controller = _FakeCtl(running)
    return obj


def test_trigger_gate():
    """触发判定的四种组合。"""
    # ① always + 不在运行 ⇒ 仍入库（持续采集，既有行为）
    app = _make_app(proto.HISTORY_TRIGGER_ALWAYS, False)
    check("always + 未运行 ⇒ 仍入库（持续采集）",
          app._history_should_store() is True)
    # ② always + 在运行 ⇒ 入库
    app = _make_app(proto.HISTORY_TRIGGER_ALWAYS, True)
    check("always + 运行中 ⇒ 入库",
          app._history_should_store() is True)
    # ③ auto + 不在运行 ⇒ 不入库
    app = _make_app(proto.HISTORY_TRIGGER_AUTO, False)
    check("auto + 未运行 ⇒ 不入库",
          app._history_should_store() is False)
    # ④ auto + 在运行 ⇒ 入库
    app = _make_app(proto.HISTORY_TRIGGER_AUTO, True)
    check("auto + 运行中 ⇒ 入库",
          app._history_should_store() is True)


def test_config_roundtrip():
    """配置缺省 / 落盘往返 / 非法值。"""
    cfg = cfgmod.Config()
    check("缺省 = 持续采集（always）",
          cfg.storage.history_trigger == proto.HISTORY_TRIGGER_ALWAYS,
          cfg.storage.history_trigger)
    tmp = tempfile.mkdtemp(prefix="hsf_trig_")
    try:
        cfg.config_path = os.path.join(tmp, "x.json")
        cfg.storage.history_trigger = proto.HISTORY_TRIGGER_AUTO
        cfg.save()
        cfg2 = cfgmod.Config()
        cfg2.config_path = cfg.config_path
        cfg2.load()
        check("落盘往返一致（auto）",
              cfg2.storage.history_trigger == proto.HISTORY_TRIGGER_AUTO,
              cfg2.storage.history_trigger)
        # 非法值：load 时回缺省（不静默接受）
        import json
        doc = json.load(open(cfg.config_path, encoding="utf-8"))
        doc["storage"]["history_trigger"] = "bogus"
        json.dump(doc, open(cfg.config_path, "w", encoding="utf-8"))
        cfg3 = cfgmod.Config()
        cfg3.config_path = cfg.config_path
        cfg3.load()
        check("配置文件里的非法值 ⇒ 回缺省（always）",
              cfg3.storage.history_trigger == proto.HISTORY_TRIGGER_ALWAYS,
              cfg3.storage.history_trigger)
    finally:
        import shutil
        shutil.rmtree(tmp, ignore_errors=True)


def test_set_key_validation():
    """服务端 SET：合法值接受、非法值明确报错（fail-closed）。"""
    app = _make_app(proto.HISTORY_TRIGGER_ALWAYS, False)
    ok = app._cfg_set_storage_key(proto.CFGK_STORAGE_HISTORY_TRIGGER,
                                  proto.HISTORY_TRIGGER_AUTO)
    check("SET 合法值 ⇒ 生效并回报变化",
          ok is True
          and app.cfg.storage.history_trigger == proto.HISTORY_TRIGGER_AUTO,
          app.cfg.storage.history_trigger)
    try:
        app._cfg_set_storage_key(proto.CFGK_STORAGE_HISTORY_TRIGGER, "bogus")
        check("SET 非法值 ⇒ 报错", False, "未抛异常")
    except ValueError as exc:
        check("SET 非法值 ⇒ 报错（fail-closed）", True, str(exc))
    # 快照回显带这个键
    doc = app._config_storage_doc()
    check("GET 快照带 historyTrigger 键",
          doc.get(proto.CFGK_STORAGE_HISTORY_TRIGGER)
          == proto.HISTORY_TRIGGER_AUTO,
          "%s" % doc.get(proto.CFGK_STORAGE_HISTORY_TRIGGER))


def main():
    failed = []
    for name, fn in sorted(globals().items()):
        if name.startswith("test_") and callable(fn):
            try:
                fn()
                print("  [PASS] %s" % name)
            except AssertionError as exc:
                failed.append(name)
                print("  [FAIL] %s：%s" % (name, exc))
            except Exception as exc:                # noqa: BLE001
                failed.append(name)
                print("  [ERROR] %s：%s: %s" % (name, type(exc).__name__, exc))
    print("\n===== 历史入库触发方式: %d failed（PASS %d / FAIL %d）====="
          % (len(failed), PASS, FAIL))
    return 1 if failed or FAIL else 0


if __name__ == "__main__":
    sys.exit(main())
