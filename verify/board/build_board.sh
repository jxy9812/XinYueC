#!/bin/sh
# verify/board/build_board.sh —— 在 WSL Ubuntu 里把 HsfServer / HsfClient
# 交叉编译成 aarch64（目标板 hd-rk3568，Debian 11，glibc 2.31）。
#
# 前置（WSL 宿主，root）：
#   - /usr/bin/aarch64-linux-gnu-gcc            Ubuntu 交叉工具链
#   - /root/board-sysroot                       板端 sysroot（lib/ usr/）
#   - /root/isoc23_shim.o                       __isoc23_* 兜底垫片（工具链引用）
#   - 源码树里要有 CMake_Config.cmake（gitignore 的本机文件；仓库只有
#     CMake_Config_template.cmake 模板，内容与 Windows 侧本机副本一致，
#     直接 cp 模板为 CMake_Config.cmake 即可，缺它 configure 报
#     Unknown CMake command "FIND_INCLUDE_DIR"）
#   源码必须在 WSL ext4（~/ 下），不要在 /mnt/* 下构建（drvfs 慢一个量级）。
#
# 用法：
#   sh verify/board/build_board.sh [构建目录]     # 缺省 ~/xboard
#
# 产物：$BUILD/bin/HsfServer、$BUILD/bin/HsfClient（ELF aarch64）
# 部署：scp 到板 /root/Desktop/XinYueC/bin/（板端已有 11-02 的
#       XGuiWindowDemo_Test 同款目录）。
#
# 说明：
#   - 输出目录显式钉在 $BUILD/bin —— 根 CMakeLists 对交叉构建有 configure
#     期硬门（CMAKE_RUNTIME_OUTPUT_DIRECTORY 指向仓库 bin/ 即 FATAL_ERROR，
#     防 ARM 产物覆盖桌面 x86 门禁二进制）。
#   - 不开 XPLATFORM_FBDEV_ON/DRM_ON/FBINPUT（2026-11-10 实测）：根
#     CMakeLists 对「交叉 + FBDEV」组合有意排除 dbus/fontconfig 的
#     pkg-config 探测（防误抓宿主 x86 库，见根 CMakeLists.txt 437-462
#     注释），而 sysroot 有 X11 ⇒ XINYUE_C_HAS_X11=1 ⇒ X11 窗口段
#     （XPlatformNativeWindow_posix.c fcitx5 前端）无条件
#     #include <dbus/dbus.h> ⇒ fatal error: dbus/dbus.h: No such file。
#     本构建对齐 11-02 板端先例 XGuiWindowDemo_Test 的纯 X11 交叉口径：
#     dbus/fontconfig 经 sysroot pkgconfig（工具链文件设 PKG_CONFIG_LIBDIR）
#     正常检出。DRM/fbdev 直写变体须另配「无 X11 探测的纯嵌入式」口径，
#     本次不做。
#   - 只编 HsfServer/HsfClient 两个目标（依赖链自动带上框架静态库与三方），
#     不编 Test/ 的测试宿主，省一半时间。
set -eu

SRC="$(cd "$(dirname "$0")/../.." && pwd)"
BUILD="${1:-$HOME/xboard}"
TC="$SRC/verify/board/toolchain-aarch64.cmake"

for p in /usr/bin/aarch64-linux-gnu-gcc /root/board-sysroot /root/isoc23_shim.o; do
    if [ ! -e "$p" ]; then
        echo "[board] 缺前置：$p" >&2
        exit 1
    fi
done

# 本机 CMake_Config.cmake（gitignore）缺失时从入库模板生成（幂等）。
if [ ! -f "$SRC/CMake_Config.cmake" ]; then
    echo "[board] 生成 CMake_Config.cmake（自模板）"
    cp "$SRC/CMake_Config_template.cmake" "$SRC/CMake_Config.cmake"
fi

echo "[board] 源码  : $SRC"
echo "[board] 构建  : $BUILD"
echo "[board] 工具链: $TC"

# 生成器：有 ninja 用 ninja，否则回退 Unix Makefiles。
if command -v ninja >/dev/null 2>&1; then
    GEN="Ninja"
else
    GEN="Unix Makefiles"
fi

# [板端串口端口可覆盖 2026-11-10] 真机 485 联调时用环境变量给板端串口：
#   BOARD_BUS_A_PORT=/dev/ttyS3   路 A（变频器站2 + 温湿度站4，115200）
#   BOARD_BUS_B_PORT=/dev/ttyS4   路 B（浊度站1/2 + 流量计站5，9600）
# （HsfServerConfig.h 的两个端口宏都有 #ifndef 守卫，-D 可覆盖；缺省仍是
#   Windows 口径 COM17/COM18。）宏值必须是 C 字符串字面量，故引号三层。
BUS_A_PORT="${BOARD_BUS_A_PORT:-COM17}"
BUS_B_PORT="${BOARD_BUS_B_PORT:-COM18}"
# 路 B（浊度×2 + 流量计，9600）板端默认启用；要关传 BOARD_BUS_B_ENABLED=0。
BUS_B_ENABLED="${BOARD_BUS_B_ENABLED:-1}"
PORT_FLAGS="-DHSF_BUS_A_PORT=\"\\\"$BUS_A_PORT\\\"\" -DHSF_BUS_B_PORT=\"\\\"$BUS_B_PORT\\\"\" -DHSF_BUS_B_ENABLED=$BUS_B_ENABLED"
echo "[board] 路 A 口: $BUS_A_PORT   路 B 口: $BUS_B_PORT   路 B 使能: $BUS_B_ENABLED"

# [端口宏变了必须重配+全量重编] 头文件宏经 -D 注入，ninja 的依赖图看不到
# 命令行变化；改过端口时清掉旧缓存强制重建（增量产物会残留旧宏）。
if [ "${BOARD_FORCE_RECONFIG:-1}" != "0" ] && [ -f "$BUILD/CMakeCache.txt" ]; then
    echo "[board] 清理旧构建缓存（端口宏走命令行，缓存依赖图不可见）"
    rm -rf "$BUILD"
fi

cmake -S "$SRC" -B "$BUILD" -G "$GEN" \
    -DCMAKE_TOOLCHAIN_FILE="$TC" \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_RUNTIME_OUTPUT_DIRECTORY="$BUILD/bin" \
    -DCMAKE_C_FLAGS="$PORT_FLAGS" \
    -DXGUI_ON=ON

# 只编两个 HSF 目标；依赖图自动带出框架静态库 + 三方（mbedtls/lwip/zlib/
# fatfs/pcre2/sqlite/freetype/xxHash）。
cmake --build "$BUILD" --target HsfServer HsfClient -j "$(nproc)"

echo
echo "[board] 产物："
file "$BUILD/bin/HsfServer" "$BUILD/bin/HsfClient"
echo "[board] 部署提示：scp '$BUILD/bin/HsfServer' '$BUILD/bin/HsfClient' \
root@192.168.1.249:/root/Desktop/XinYueC/bin/ && ssh root@192.168.1.249 \
'chmod +x /root/Desktop/XinYueC/bin/HsfServer /root/Desktop/XinYueC/bin/HsfClient'"
