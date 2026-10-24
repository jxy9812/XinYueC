#!/bin/bash
# 合体固件单文件编译配方（ESP8266 NONOS3, call0 ABI, -mlongcalls 必需）。
# $1 = 相对仓库根的源文件路径。档案 = xgui_shell_esp8266.h（XGui + shell + XMemory 两池）。
# 本轮去掉 -D__FreeRTOS__：不再链接仓库 FreeRTOS V9，改走 XPLATFORM_BAREMETAL
# （XMemory 的 SYSTEM 槽因此落在 .bss 静态 arena 的全局可变池上）。
# 增量：对象比源新则跳过（档案/参数变更须 rm _obj/*.o 全量重建）。
set -u
SELF_DIR="$(cd "$(dirname "$0")" && pwd)"
# 仓库根：模板在 <repo>/platforms/esp8266/nonos/tools/，上溯四级（原三级）
ROOT="$(cd "$SELF_DIR/../../../.." && pwd)"
BUILD="$(cd "$SELF_DIR/.." && pwd)"
# 外部工具链路径与 build_fw_final.sh 共用同一份配置（换电脑只改 esp_env.sh）
. "$SELF_DIR/esp_env.sh"
TC="$ESP_TOOLCHAIN/bin/xtensa-lx106-elf-gcc.exe"
CFG="$BUILD/cfg/xgui_shell_esp8266.h"
INCDIRS="-I$BUILD -I$BUILD/cfg/shim -I$ROOT/Drive/FreeRTOS/include -I$ROOT/Drive/FreeRTOS/portable/GCC/XTENSA_LX106 -I$ROOT/Library/xxHash -I$ROOT/Library/zlib -I$EXTRAINC -I$CORE -I$SDKINC -I$SDKINC/esp8266 -I$SDK/lwip2/include"
while IFS= read -r d; do INCDIRS="$INCDIRS -I$ROOT/$d"; done < "$BUILD/cfg/inc_dirs.txt"
src="$ROOT/$1"; obj="$BUILD/build/_obj/$(echo "$1" | tr '/' '_').o"
# 2026-10-22：档案（CFG，-include 强制包含）也是每个 TU 的输入。它一变，
# 所有 TU 的展开都会变（如 XTHREAD_ON 0/1），只比较源文件 mtime 会留下
# 陈旧 .o —— 链接期才爆 undefined reference / ODR 错位，且症状远离根因。
# 与 build_fw_final.sh 的 build_tu() 同一口径：.o 比源和档案都新才跳过。
if [ -f "$obj" ] && [ "$obj" -nt "$src" ] && [ "$obj" -nt "$CFG" ]; then exit 0; fi
"$TC" -std=gnu99 -Os -c -mlongcalls -mtext-section-literals -falign-functions=4 \
 -ffunction-sections -fdata-sections -include "$CFG" \
 -D__ESP8266__ -D__ets__ -DICACHE_FLASH -DXCHAR_NO_SYSTEM_GBK=1 \
 -DF_CPU=80000000L -DNONOSDK305=1 -DLWIP_OPEN_SRC \
 -DTCP_MSS=536 -DLWIP_FEATURES=1 -DLWIP_IPV6=0 -DARDUINO=10819 \
 -Wno-implicit-function-declaration -Wno-builtin-declaration-mismatch \
 $INCDIRS "$src" -o "$obj" 2>"$BUILD/build/_obj/cc_err_$$.txt" || { echo "ERROR $1"; grep -i "error" "$BUILD/build/_obj/cc_err_$$.txt" | head -5; }
rm -f "$BUILD/build/_obj/cc_err_$$.txt"
