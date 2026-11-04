# -*- coding: utf-8 -*-
"""高速过滤器服务端（Python 版）。

对应 C 侧 HighSpeedFilter/server/，线协议完全兼容（XLink v2 + HSF 命令面），
可被现有 C 客户端 HsfClient 直接连接。

模块划分：
    proto.py   线协议常量（帧头/消息类/命令号/主题号/键名）—— 线格式唯一权威
    bson.py    BSON 编解码（标准库实现，零第三方依赖）
    auth.py    迭代 HMAC-SHA256 挑战应答 + 账户库（JSON，与 C 侧可互通）
    xlink.py   XLink 服务端会话层（握手/认证/订阅/发布/心跳）
    modbus.py  Modbus RTU + TCP 主站（纯标准库，含各设备协议怪癖处理）
    devices.py 设备驱动（M512 变频器 / 温湿度 / 模拟量 / 16DI+16DO）
    app.py     业务层（命令路由/遥测推送/设备健康/运行模式/点动看门狗）
    config.py  部署配置（运行期 JSON，替代 C 侧编译期宏）
    __main__.py 入口（python3 -m hsfsrv）

运行：python3 -m hsfsrv --help
"""

__version__ = "1.0.0"
