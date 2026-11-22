# -*- coding: utf-8 -*-
"""高速过滤器网页服务器（webui_py/hsfweb）—— 纯标准库后端包。

一条上游 XLink 连接 + 多浏览器 SSE 扇出；历史/日志/配置全部转发上游
服务端（web 不自建历史库）。入口：``python -m hsfweb``（见 __main__.py）。
"""
