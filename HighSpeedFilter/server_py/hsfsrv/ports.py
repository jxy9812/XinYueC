# -*- coding: utf-8 -*-
"""全项目端口与网络参数的**唯一事实源**（single source of truth）。

[为什么单独一个文件]
端口号原先散落在四处（服务端 config、客户端 xlink、命令行帮助文本、
部署脚本与文档），改一次要动四五处，漏一处就出现"服务端听 A、客户端连 B"
这种最难查的故障（现象是连不上，但两边代码看起来都没错）。

现在：**只改这个文件**，两端与服务端/客户端都从这里取。

[两端各带一份副本]
`server_py/hsfsrv/ports.py` 与 `client_py/hsfsrv_client/ports.py` 内容
完全相同（两端是独立可部署的目录，不能互相 import）。改的时候两份一起改，
`tests/test_ports.py` 会校验两份一致——不一致直接测试失败，不会静静漂移。
"""

# ============================================================================
#                          XLink 监听/连接端口
# ============================================================================

#: **Python 版服务端监听端口**，客户端缺省也连它。
#:
#: [为什么是 6200] 刻意与 C 版（6100）错开，好让「C 版 + Python 版」两套
#: 服务端同机并存做对比调试。共用端口时后启动的那个直接起不来
#: （`address already in use`），或者客户端连到预料之外的那一方却毫无察觉
#: （两套协议完全兼容，数据却来自不同实现）。
PY_SERVER_PORT = 6200

#: **C 版服务端端口**（协议规格值，与 C 头文件 XhsfLinkProto.h 一致）。
#:
#: 仅供「明确要连 C 版」的场景使用（客户端 `--port 6100`）。
#: **不要**把它当成 Python 版服务端的默认端口——那是 PY_SERVER_PORT。
C_SERVER_PORT = 6100

#: 监听/连接地址（服务端绑 0.0.0.0 = 全部网卡）。
DEFAULT_BIND_HOST = "0.0.0.0"
#: 客户端连本机时用的回环地址。
LOOPBACK_HOST = "127.0.0.1"


# ============================================================================
#                          串口缺省值
# ============================================================================

#: 桌面端（Windows 开发机）缺省串口：CH343 USB-485 转换器。
SERIAL_PORT_WINDOWS = "COM17"
#: 板端（RK3568）缺省串口：板载 UART（就是接 485 的那一路）。
SERIAL_PORT_POSIX = "/dev/ttyS3"


def default_server_port():
    """Python 版服务端的监听端口。"""
    return PY_SERVER_PORT


def default_client_port():
    """客户端的缺省连接端口（连 Python 版服务端）。"""
    return PY_SERVER_PORT


def default_serial_port():
    """按平台给缺省串口号（桌面 COM17 / 板端 /dev/ttyS3）。"""
    import sys
    if sys.platform.startswith("win"):
        return SERIAL_PORT_WINDOWS
    return SERIAL_PORT_POSIX
