#!/bin/sh
# ============================================================================
#  HsfClientPy 板端部署脚本（RK3568 / Debian 11 / Python 3.9 / PyQt5）
#
#  用法：
#      sh deploy.sh                      部署到 192.168.0.10
#      sh deploy.sh --auto 192.168.0.10  部署并配成开机自动登录该服务端
#      sh deploy.sh --host 192.168.0.10 --dir /root/Desktop/XinYueC/hsf_client_py
#
#  [前置条件] 板上需要 python3-pyqt5（apt 装，不能 pip）：
#      apt-get install -y python3-pyqt5
#  若报 qtbase 包 404，是 security 源里该版本在镜像缺失，可临时注释掉
#  debian-security 源再装（本脚本会自动检测并提示）。
# ============================================================================
set -e

HOST="192.168.0.10"
REMOTE_DIR="/root/Desktop/XinYueC/hsf_client_py"
AUTO_HOST=""
PORT=""
FULLSCREEN="1"

while [ $# -gt 0 ]; do
    case "$1" in
        --host)       HOST="$2"; shift 2 ;;
        --dir)        REMOTE_DIR="$2"; shift 2 ;;
        --auto)       AUTO_HOST="$2"; shift 2 ;;
        --port)       PORT="$2"; shift 2 ;;
        --windowed)   FULLSCREEN="0"; shift ;;
        *) echo "未知参数: $1"; exit 1 ;;
    esac
done

SRC_DIR=$(cd "$(dirname "$0")" && pwd)

# 端口从 ports.py 取（单一事实源）；失败则回退。
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
from hsfsrv_client import ports
print(ports.PY_SERVER_PORT)
" 2>/dev/null)
fi
[ -n "$PORT" ] || PORT=6200
fi
echo "==> 端口: $PORT（来自 hsfsrv_client/ports.py）"

echo "==> 目标: root@$HOST:$REMOTE_DIR"
echo "==> 源码: $SRC_DIR"

# ---- 1. 打包 ----
TARBALL=$(mktemp -t hsfcli.XXXXXX.tar.gz)
trap 'rm -f "$TARBALL"' EXIT
tar czf "$TARBALL" -C "$SRC_DIR" \
    --exclude='__pycache__' --exclude='*.pyc' \
    hsfsrv_client tests docs README.md 2>/dev/null || \
tar czf "$TARBALL" -C "$SRC_DIR" \
    --exclude='__pycache__' --exclude='*.pyc' hsfsrv_client tests
echo "==> 包大小: $(wc -c < "$TARBALL") 字节"

# ---- 2. 传输 ----
ssh -o StrictHostKeyChecking=no "root@$HOST" "mkdir -p '$REMOTE_DIR'"
scp -o StrictHostKeyChecking=no -q "$TARBALL" "root@$HOST:$REMOTE_DIR/src.tar.gz"
ssh -o StrictHostKeyChecking=no "root@$HOST" \
    "cd '$REMOTE_DIR' && tar xzf src.tar.gz && rm -f src.tar.gz"

# ---- 3. 依赖检查 ----
echo "==> 依赖检查"
ssh -o StrictHostKeyChecking=no "root@$HOST" '
if python3 -c "import PyQt5" 2>/dev/null; then
    python3 -c "from PyQt5.QtCore import QT_VERSION_STR; print(\"  PyQt5 OK, Qt \" + QT_VERSION_STR)"
else
    echo "  [缺失] PyQt5，尝试安装…"
    apt-get install -y -q python3-pyqt5 2>&1 | tail -2 \
      || echo "  [失败] 请手工 apt-get install python3-pyqt5（security 源缺包时先注释它）"
fi
# 中文字体（缺了会显示方框）
python3 -c "
import subprocess
out = subprocess.run([\"fc-list\"], capture_output=True, text=True).stdout
if \"WenQuanYi\" in out or \"Noto Sans CJK\" in out or \"Droid\" in out:
    print(\"  中文字体 OK\")
else:
    print(\"  [提示] 未检测到中文字体，建议 apt-get install -y fonts-wqy-zenhei\")
"'

# ---- 4. 启动脚本 ----
# launch.sh 随包一起上传（不现场生成：heredoc/printf 都会被 shell 插值改写，
# 实测生成的脚本里参数变成 `if [ -n "" ]`，客户端收不到 --auto）。
ssh -o StrictHostKeyChecking=no "root@$HOST" "chmod +x '$REMOTE_DIR/launch.sh'"

# ---- 5. 可选：开机自启（覆盖 demo 的默认行为） ----
if [ -n "$AUTO_HOST" ]; then
    if [ "$FULLSCREEN" = "1" ]; then FS="--fullscreen"; else FS=""; fi
    ssh -o StrictHostKeyChecking=no "root@$HOST" "cat > /etc/systemd/system/hsfclientpy.service <<SVCEOF
[Unit]
Description=HSF Client (Python/PyQt5)
After=graphical.target network-online.target
Wants=network-online.target

[Service]
Type=simple
User=root
Environment=DISPLAY=:0
Environment=QT_QPA_PLATFORM=xcb
Environment=QT_OPENGL=software
WorkingDirectory=$REMOTE_DIR
ExecStart=/usr/bin/python3 -m hsfsrv_client --auto $AUTO_HOST --port $PORT $FS
Restart=on-failure
RestartSec=5
StandardOutput=journal
StandardError=journal
SyslogIdentifier=hsfclientpy

[Install]
WantedBy=graphical.target
SVCEOF
systemctl daemon-reload && systemctl enable hsfclientpy.service >/dev/null 2>&1"
    echo "==> 已配置开机自启（连 $AUTO_HOST:$PORT）"
fi

# ---- 6. 自检 ----
echo "==> 离屏自检（不抢前台窗口）"
ssh -o StrictHostKeyChecking=no "root@$HOST" \
    "cd '$REMOTE_DIR' && QT_QPA_PLATFORM=offscreen timeout 40 python3 -m hsfsrv_client \
     --offline --shot /tmp/hsf_client_selftest.png --size 1280x720 2>&1 | tail -3; \
     ls -l /tmp/hsf_client_selftest.png 2>/dev/null || echo '  截图未生成'"

echo ""
echo "部署完成。常用命令："
echo "  ssh root@$HOST 'cd $REMOTE_DIR && DISPLAY=:0 ./run_client.sh --auto 192.168.0.10'"
echo "  ssh root@$HOST 'systemctl status hsfclientpy'"
echo "  ssh root@$HOST 'journalctl -u hsfclientpy -f'"
echo "  ssh root@$HOST 'pkill -f hsfsrv_client'    # 关掉手动启动的客户端"
