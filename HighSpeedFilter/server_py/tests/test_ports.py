# -*- coding: utf-8 -*-
"""端口配置一致性测试：锁住「单一事实源」。

    python tests/test_ports.py

[为什么需要它] 服务端与客户端是**两个独立可部署目录**，各自带一份
`ports.py`（不能互相 import）。两份一旦漂移，就会出现「服务端听 6200、
客户端连 6100」这类最难查的故障——两边代码都对，就是连不上。

本测试做四件事：
  1. 两份 ports.py 的**关键常量逐项相等**
  2. 服务端/客户端实际生效的默认端口**都等于 ports 里的值**
     （防止有人绕过 ports 又写死一个数字）
  3. 代码里**没有新的裸端口字面量**（扫源码，排除注释/docstring）
  4. 端口值本身合法（1024~65535 非特权端口）
"""

import io
import os
import re
import sys
import unittest

HERE = os.path.dirname(os.path.abspath(__file__))
SERVER_ROOT = os.path.dirname(HERE)                                  # server_py
CLIENT_ROOT = os.path.join(os.path.dirname(SERVER_ROOT), "client_py")

sys.path.insert(0, SERVER_ROOT)
sys.path.insert(0, CLIENT_ROOT)

from hsfsrv import ports as server_ports                             # noqa: E402

#: 客户端目录可能不存在（板端只部署服务端时）。此时跳过"两份一致"的检查，
#: 但仍校验服务端自身的端口口径 —— 不能因为缺一侧就整体跳过。
try:
    from hsfsrv_client import ports as client_ports
    HAVE_CLIENT = True
except ImportError:
    client_ports = None
    HAVE_CLIENT = False


#: 两份 ports.py 必须一致的常量。
PORT_CONSTANTS = (
    "PY_SERVER_PORT",
    "C_SERVER_PORT",
    "DEFAULT_BIND_HOST",
    "LOOPBACK_HOST",
    "SERIAL_PORT_WINDOWS",
    "SERIAL_PORT_POSIX",
)


class TestPortsMatch(unittest.TestCase):
    """1. 两份 ports.py 必须逐项相等（客户端未部署时跳过）。"""

    @unittest.skipUnless(HAVE_CLIENT, "客户端目录未部署")
    def test_constants_identical(self):
        for name in PORT_CONSTANTS:
            self.assertTrue(hasattr(server_ports, name),
                            "服务端 ports 缺 %s" % name)
            self.assertTrue(hasattr(client_ports, name),
                            "客户端 ports 缺 %s" % name)
            self.assertEqual(
                getattr(server_ports, name), getattr(client_ports, name),
                "%s 在两份 ports.py 里不一致：服务端=%r 客户端=%r"
                % (name, getattr(server_ports, name),
                   getattr(client_ports, name)))

    @unittest.skipUnless(HAVE_CLIENT, "客户端目录未部署")
    def test_files_have_same_content(self):
        """两份文件内容应完全相同（含注释），避免解释性文字漂移。"""
        with io.open(os.path.join(SERVER_ROOT, "hsfsrv", "ports.py"),
                     encoding="utf-8") as fh:
            server_text = fh.read()
        with io.open(os.path.join(CLIENT_ROOT, "hsfsrv_client", "ports.py"),
                     encoding="utf-8") as fh:
            client_text = fh.read()
        self.assertEqual(server_text, client_text,
                         "两份 ports.py 内容不同——改端口时请同步另一份")


class TestEffectiveDefaults(unittest.TestCase):
    """2. 两端实际生效的默认值必须来自 ports。"""

    def test_server_default_port_comes_from_ports(self):
        from hsfsrv import config
        self.assertEqual(config.DEFAULT_PORT, server_ports.PY_SERVER_PORT)

    @unittest.skipUnless(HAVE_CLIENT, "客户端目录未部署")
    def test_client_default_port_comes_from_ports(self):
        from hsfsrv_client import xlink
        self.assertEqual(xlink.DEFAULT_PORT, client_ports.PY_SERVER_PORT)

    @unittest.skipUnless(HAVE_CLIENT, "客户端目录未部署")
    def test_server_and_client_agree(self):
        """服务端监听端口 == 客户端缺省连接端口（否则开箱即连不上）。"""
        from hsfsrv import config
        from hsfsrv_client import xlink
        self.assertEqual(config.DEFAULT_PORT, xlink.DEFAULT_PORT)

    def test_spec_constant_is_c_port(self):
        """协议规格常量应等于 C 版端口，而非 Python 版端口。"""
        from hsfsrv import proto as server_proto
        self.assertEqual(server_proto.HSF_XLINK_DEFAULT_PORT,
                         server_ports.C_SERVER_PORT)
        if HAVE_CLIENT:
            from hsfsrv_client import proto as client_proto
            self.assertEqual(client_proto.HSF_XLINK_DEFAULT_PORT,
                             client_ports.C_SERVER_PORT)

    def test_python_port_differs_from_c_port(self):
        """Python 版端口必须与 C 版不同（这是当初改端口的全部理由）。"""
        self.assertNotEqual(server_ports.PY_SERVER_PORT,
                            server_ports.C_SERVER_PORT)


class TestPortValues(unittest.TestCase):
    """4. 端口值合法。"""

    def test_range(self):
        for name in ("PY_SERVER_PORT", "C_SERVER_PORT"):
            value = getattr(server_ports, name)
            self.assertIsInstance(value, int, name)
            # 避开特权端口（<1024）与操作系统临时端口段（>=49152）
            self.assertGreaterEqual(value, 1024, name)
            self.assertLessEqual(value, 49151, name)


class TestNoHardcodedPorts(unittest.TestCase):
    """3. 源码里不得再出现裸端口字面量（只有 ports.py 例外）。

    扫描时不看注释与 docstring —— 文档里写"缺省 6200"是合理的，
    代码里写死 6200 才是问题。
    """

    #: 允许出现端口字面量的文件（ports.py 自己）。
    ALLOWED = {"ports.py"}

    def _iter_py_files(self):
        for root_dir, pkg in ((SERVER_ROOT, "hsfsrv"),
                              (CLIENT_ROOT, "hsfsrv_client")):
            base = os.path.join(root_dir, pkg)
            if not os.path.isdir(base):
                continue
            for name in sorted(os.listdir(base)):
                if name.endswith(".py"):
                    yield os.path.join(base, name), name

    @staticmethod
    def _strip_comments_and_docstrings(text):
        """粗暴但够用：去掉 # 注释行与三引号块，留下"真代码"。"""
        text = re.sub(r'"""[\s\S]*?"""', "", text)
        text = re.sub(r"'''[\s\S]*?'''", "", text)
        out = []
        for line in text.split("\n"):
            hash_at = line.find("#")
            out.append(line if hash_at < 0 else line[:hash_at])
        return "\n".join(out)

    def test_no_literal_python_or_c_port_in_code(self):
        ports_to_check = (server_ports.PY_SERVER_PORT,
                          server_ports.C_SERVER_PORT)
        offenders = []
        for path, name in self._iter_py_files():
            if name in self.ALLOWED:
                continue
            with io.open(path, encoding="utf-8") as fh:
                code = self._strip_comments_and_docstrings(fh.read())
            for port in ports_to_check:
                # 只匹配独立的数字字面量（排除 16200 这种更长数字的一部分）
                if re.search(r"(?<!\d)%d(?!\d)" % port, code):
                    offenders.append("%s 含端口字面量 %d" % (name, port))
        self.assertEqual(offenders, [],
                         "以下文件绕过 ports.py 写死了端口：\n  "
                         + "\n  ".join(offenders))


if __name__ == "__main__":
    unittest.main(verbosity=2)
