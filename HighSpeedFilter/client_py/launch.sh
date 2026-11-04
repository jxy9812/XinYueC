#!/bin/sh
# 客户端启动脚本（板端）：所有参数原样透传给 python 模块。
#
# [为什么单独一个脚本] systemd 与手工启动都要设置同一组环境变量
# （DISPLAY / Qt 平台插件 / 关 GL），集中在这里避免两处漂移。
cd "$(dirname "$0")" || exit 1
export DISPLAY=${DISPLAY:-:0}
export QT_QPA_PLATFORM=xcb
export QT_OPENGL=software
export QT_LOGGING_RULES="qt.qpa.*=false"
exec /usr/bin/python3 -m hsfsrv_client "$@"
