#!/bin/sh
# ============================================================================
#  HsfServerPy 板端部署脚本（RK3568 / Debian 11 / Python 3.9）
#
#  用法（在开发机上执行）：
#      sh deploy.sh                     部署到 192.168.0.10
#      sh deploy.sh --no-serial         部署并写成「不碰串口」形态
#      sh deploy.sh --host 192.168.0.10
#
#  [为什么串口默认关] 板上 demo 常以 Modbus 从站模式（slave485.mode）独占
#  ttyS3，Python 服务端再开同一串口会互踢。需要采变频器+温湿度时：
#      ssh root@<ip> 'systemctl stop run_test.service'
#      ssh root@<ip> 'sed -i "s/serial_enabled.:\s*false/serial_enabled: true/" \
#          /root/Desktop/XinYueC/hsf_server_py/cfg/hsf_server_py.json'
#  然后重启 hsfserverpy 服务。
# ============================================================================
set -e

HOST="192.168.0.10"
REMOTE_DIR="/root/Desktop/XinYueC/hsf_server_py"
PORT=""
SERIAL="false"

while [ $# -gt 0 ]; do
    case "$1" in
        --host) HOST="$2"; shift 2 ;;
        --dir)  REMOTE_DIR="$2"; shift 2 ;;
        --port) PORT="$2"; shift 2 ;;
        --serial) SERIAL="true"; shift ;;
        --no-serial) SERIAL="false"; shift ;;
        *) echo "未知参数: $1"; exit 1 ;;
    esac
done

SRC_DIR=$(cd "$(dirname "$0")" && pwd)

# 端口从 ports.py 取（单一事实源）；失败则回退，不让脚本因读不到而中断。
if [ -z "$PORT" ]; then
    # [解释器选择] 板上只有 python3；Git Bash 里 python3 可能是
# Windows Store 占位程序（退出码 49、无输出），故优先 python3、回退 python。
_PORT_PY=""
for _cand in python3 python; do
    if "$_cand" -c "import sys; sys.exit(0 if sys.version_info >= (3, 7) else 1)"         2>/dev/null; then _PORT_PY="$_cand"; break; fi
done
PORT=""
if [ -n "$_PORT_PY" ]; then
    PORT=$(cd "$SRC_DIR" && "$_PORT_PY" -c "
from hsfsrv import ports
print(ports.PY_SERVER_PORT)
" 2>/dev/null)
fi
[ -n "$PORT" ] || PORT=6200
fi
echo "==> 端口: $PORT（来自 hsfsrv/ports.py）"

echo "==> 目标: root@$HOST:$REMOTE_DIR"
echo "==> 源码: $SRC_DIR"

# ---- 1. 打包（排除本机运行产物与测试临时目录）----
TARBALL=$(mktemp -t hsfpy.XXXXXX.tar.gz)
trap 'rm -f "$TARBALL"' EXIT
tar czf "$TARBALL" -C "$SRC_DIR" \
    --exclude='__pycache__' \
    --exclude='*.pyc' \
    --exclude='cfg' \
    --exclude='tests/_run' \
    hsfsrv tests README.md 2>/dev/null || \
tar czf "$TARBALL" -C "$SRC_DIR" \
    --exclude='__pycache__' --exclude='*.pyc' --exclude='cfg' \
    --exclude='tests/_run' hsfsrv tests
echo "==> 包大小: $(wc -c < "$TARBALL") 字节"

# ---- 2. 传上去并解包 ----
ssh -o StrictHostKeyChecking=no "root@$HOST" "mkdir -p '$REMOTE_DIR'"
scp -o StrictHostKeyChecking=no -q "$TARBALL" "root@$HOST:$REMOTE_DIR/src.tar.gz"
ssh -o StrictHostKeyChecking=no "root@$HOST" \
    "cd '$REMOTE_DIR' && tar xzf src.tar.gz && rm -f src.tar.gz"

# ---- 3. 依赖检查（pyserial 可选：只跑 TCP 设备时不需要）----
ssh -o StrictHostKeyChecking=no "root@$HOST" \
    "python3 -c 'import serial; print(\"  pyserial\", serial.VERSION)' 2>/dev/null || \
     echo '  [提示] 未装 pyserial（只跑 TCP 设备不需要）。要采串口请执行：apt-get install -y python3-serial'"

# ---- 4. 首次运行生成配置（不改已有的）----
# [为什么用参数传值] 早先把 shell 变量直接插进内联 Python 源码里，
# $SERIAL 展开成 shell 的 false → Python 侧 NameError。改成 sys.argv 传参。
ssh -o StrictHostKeyChecking=no "root@$HOST" "cd '$REMOTE_DIR' && \
    if [ ! -f cfg/hsf_server_py.json ]; then \
        python3 - '${PORT:-6200}' '$SERIAL' <<'PYEOF'
import json, os, sys
port = int(sys.argv[1])
# 接受 shell 风格的 true/false 与 Python 风格 True/False
serial_on = sys.argv[2].strip().lower() in ('true', '1', 'yes')
os.makedirs('cfg', exist_ok=True)
cfg = {
  'server': {'port': port, 'serial_enabled': serial_on},
  'serial': {'port': '/dev/ttyS3', 'baudrate': 115200, 'timeout_ms': 400,
             'retries': 1, 'vfd_poll_ms': 100, 'temphum_poll_ms': 500,
             'temphum_push_ms': 500, 'vfd_enabled': True,
             'temphum_enabled': True,
             'unit_ids': {'vfd': 2, 'temphum': 4}},
  'analog': {'enabled': True, 'host': '192.168.0.11', 'port': 8234,
             'unit_id': 1, 'channels': 8, 'poll_ms': 200},
  'digital': {'enabled': True, 'poll_ms': 200, 'stale_ms': 5000,
              'modules': [
                {'name': '数字量1', 'host': '192.168.0.12', 'port': 502,
                 'unit_id': 1, 'timeout_s': 2.0},
                {'name': '数字量2', 'host': '192.168.0.13', 'port': 502,
                 'unit_id': 1, 'timeout_s': 2.0}]},
  'stale': {'vfd_ms': 5000, 'temphum_ms': 5000, 'digital_ms': 5000},
  'runtime': {'run_mode': 0}}
json.dump(cfg, open('cfg/hsf_server_py.json','w'), ensure_ascii=False, indent=2)
print('  已生成 cfg/hsf_server_py.json（串口 %s）'
      % ('启用' if serial_on else '关闭'))
PYEOF
    else echo '  沿用已有 cfg/hsf_server_py.json'; fi"

# ---- 5. systemd 服务 ----
ssh -o StrictHostKeyChecking=no "root@$HOST" "cat > /etc/systemd/system/hsfserverpy.service <<SVCEOF
[Unit]
Description=HSF Server (Python, XLink over TCP)
After=network-online.target
Wants=network-online.target

[Service]
Type=simple
WorkingDirectory=$REMOTE_DIR
ExecStart=/usr/bin/python3 -m hsfsrv
Restart=on-failure
RestartSec=3
# 日志走 journal：journald 已封顶（SystemMaxUse=50M），不会吃满内存
StandardOutput=journal
StandardError=journal
SyslogIdentifier=hsfserverpy

[Install]
WantedBy=multi-user.target
SVCEOF
systemctl daemon-reload && systemctl enable hsfserverpy.service >/dev/null 2>&1"

# ---- 6. 自检 + 起服务 ----
# 自检用临时端口：正式服务可能已在 6200 上跑着（重部署场景），用默认端口
# 会以 "address already in use" 污染自检输出。
echo "==> 设备自检"
ssh -o StrictHostKeyChecking=no "root@$HOST" \
    "cd '$REMOTE_DIR' && timeout 45 python3 -m hsfsrv --diag --port 61999 2>&1 | grep -E '自检|OK|失败' || true"

echo "==> 启动服务"
ssh -o StrictHostKeyChecking=no "root@$HOST" \
    "systemctl restart hsfserverpy.service && sleep 3 && \
     systemctl is-active hsfserverpy.service && \
     ss -ltn 2>/dev/null | grep -E ':${PORT:-6200}' || true"

echo ""
echo "部署完成。常用命令："
echo "  ssh root@$HOST 'systemctl status hsfserverpy'"
echo "  ssh root@$HOST 'journalctl -u hsfserverpy -f'"
echo "  ssh root@$HOST 'systemctl stop hsfserverpy'"
