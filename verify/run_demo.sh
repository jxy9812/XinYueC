#!/bin/bash
cd "$(dirname "$0")/bin"
export DISPLAY=:0
HERE="$(dirname "$0")"
# 485 联调模式开关（存在对应标志文件即生效）：
#   slave485.mode  → Modbus RTU 从站（ttyS3, 从站页 18, 自动监听）
#   master485.mode → Modbus RTU 主站（ttyS3, 主站页 17, 自动连接）
#
# 渲染后端开关（2026-11-01 新增）：
#   gpu.mode       → GPU 直通呈现（XGUI_RENDER_BACKEND=gpu）
#                    框架默认口径已改为「软件光栅，GPU 按需启用」
#                    （RK3568 实测：GPU 直通进程 CPU 是软件光栅的
#                     2~4 倍，无零拷贝呈现链时默认 GPU 是净亏损）。
#   gpuswap.mode   → GPU 直通 + swap 呈现（叠加于 gpu.mode）：
#                    XGPU_PRESENT=swap，GPU 直接 swapBuffers 上屏，
#                    框架不再重复 XPutImage（立项书任务 C）。
# 渲染后端默认**软件光栅**（2026-11-01 定案：板端用软件渲染）——
# 显式钉死，不依赖框架默认口径，避免残留 env 或旗标文件误开 GPU。
export XGUI_RENDER_BACKEND=software
if [ -f "$HERE/gpu.mode" ]; then
    export XGUI_RENDER_BACKEND=gpu
fi
if [ -f "$HERE/gpuswap.mode" ]; then
    export XGUI_RENDER_BACKEND=gpu
    export XGPU_PRESENT=swap
fi
if [ -f "$HERE/slave485.mode" ]; then
    export XGUI_DEMO_MB_SLAVE_SERIAL=/dev/ttyS3
    export XGUI_DEMO_MB_SLAVE_BAUD=115200
    export XGUI_DEMO_MB_AUTOSTART=slave
    exec stdbuf -o0 -e0 ./XGuiWindowDemo_Test --fullscreen --page 18 "$@"
fi
if [ -f "$HERE/master485.mode" ]; then
    export XGUI_DEMO_MB_SERIAL=1
    export XGUI_DEMO_MB_SERIAL_PORT=/dev/ttyS3
    export XGUI_DEMO_MB_BAUD=9600
    export XGUI_DEMO_MB_AUTOSTART=master
    exec ./XGuiWindowDemo_Test --fullscreen --page 17 "$@"
fi
exec stdbuf -o0 -e0 ./XGuiWindowDemo_Test --fullscreen "$@"
