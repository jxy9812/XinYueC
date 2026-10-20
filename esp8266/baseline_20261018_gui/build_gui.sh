#!/bin/bash
# ESP8266 XGui + SSD1306 128x64 冒烟固件：编译 + 链接 + elf2bin。
# 底座=build/arduino_nos 启动铁律（cre.S 建 cache/清 .bss、真实堆 pvPort*、
# ets_post --defsym 钉 ROM、__wrap_spi_flash_*）；源集=esp8266_console
# filelist（裁 heap_4）+ XGui/XFont/Unsupported-Graphics 增量 + 本地三件。
# 产物：gui.elf / gui.map / fw_gui.bin（eboot@0x0 + app@0x10000）。
# 用法：bash build_gui.sh   （在 build/arduino_gui/ 下执行）
set -e
cd "$(dirname "$0")"

ARD=C:/esp/arduino8266
SDK=$ARD/tools/sdk
TC=C:/esp/toolchain-xtensa/bin
mkdir -p _obj

# ---- 1. 合成 filelist：console 210 文件（裁 heap_4）+ GUI 增量 + 本地三件 ----
{ grep -v "heap_4" ../esp8266_console/filelist.txt; cat filelist_gui_extra.txt; \
  echo __GUIMAIN__; echo __STUBS__; echo __SSD1306__; echo __WIFISTUBS__; } > _obj/filelist_final.txt
echo "== filelist: $(wc -l < _obj/filelist_final.txt) files =="

# ---- 2. 并行编译 ----
xargs -P 6 -I{} bash recompile_gui_one.sh {} < _obj/filelist_final.txt

# ---- 3. 对象清单严格按 filelist 生成（不 ls 目录，防历史残留混入） ----
: > _obj/all_objs.txt
while IFS= read -r rel; do
  [ -z "$rel" ] && continue
  case "$rel" in
    __GUIMAIN__)   echo "_obj/gui_main.o" >> _obj/all_objs.txt ;;
    __STUBS__)     echo "_obj/gui_stubs.o" >> _obj/all_objs.txt ;;
    __SSD1306__)   echo "_obj/XDeviceDisplay_ssd1306_i2c.o" >> _obj/all_objs.txt ;;
    __WIFISTUBS__) echo "_obj/wifi_stubs.o" >> _obj/all_objs.txt ;;
    *) echo "_obj/$(echo "$rel" | tr '/' '_').o" >> _obj/all_objs.txt ;;
  esac
done < _obj/filelist_final.txt
echo "== objs: $(wc -l < _obj/all_objs.txt) =="

# ---- 3.5 cre.S（cache 建立例程，抄 arduino_nos 铁律） + cont.S（Arduino
#          同款上下文切换，GUI 流程跑在 4KB 专用 cont 栈上避开 NONOS
#          boot 栈过浅的踩穿） ----
$TC/xtensa-lx106-elf-gcc -c -mlongcalls cre.S -o cre.o
$TC/xtensa-lx106-elf-gcc -c -Os -I C:/esp/arduino8266/cores/esp8266 \
  C:/esp/arduino8266/cores/esp8266/cont.S -o _obj/cont.o
echo "_obj/cont.o" >> _obj/all_objs.txt

# ---- 4. 链接：裸机直入 + Cache_Read_Enable（补全 irom 映射）+ 精简库组 ----
# 结论链：(a) call_user_start 全 SDK 启动：boot 栈剩 ~2KB + multipool
#      碎片 OOM（n=515/960）；(b) 裸机直入无 Cache_Read_Enable：深调用
#      跨 irom 页取指异常；(c) 定版 = 裸机直入 + app_entry 内
#      Cache_Read_Enable(0,0,1) 补全 irom 页映射 + boot 栈 ~8KB + 堆
#      ~25KB（无 SDK/WiFi 静态）。WiFi 库组不链，100 桩由 wifi_stubs.c
#      提供（仅满足 libmain 被引用成员的链接）。
$TC/xtensa-lx106-elf-gcc -g -Os -nostdlib -Wl,--no-check-sections -u app_entry \
 -Wl,-static -Wl,-wrap,spi_flash_read -Wl,-wrap,spi_flash_write \
 -Wl,-wrap,spi_flash_erase_sector \
 -Wl,--defsym=ets_post=0x40000e24 -Wl,--defsym=ets_intr_lock=0x40000f74 \
 -Wl,--defsym=ets_intr_unlock=0x40000f80 \
 -L$SDK/lib -L$SDK/lib/NONOSDK305 \
 -Tlocal.eagle.flash.ld -Wl,--gc-sections -Wl,-Map=gui.map -o gui.elf \
 @_obj/all_objs.txt cre.o \
 -Wl,--start-group -lhal -lmain -lm -lc -lgcc -Wl,--end-group

echo "== link OK =="
$TC/xtensa-lx106-elf-size.exe gui.elf
$TC/xtensa-lx106-elf-nm.exe -u gui.elf > _obj/undefined_all.txt || true
echo "undefined symbols: $(wc -l < _obj/undefined_all.txt)"

# ---- 5. eboot 合并镜像（烧录 @0x0，应用 @0x10000） ----
python $ARD/tools/elf2bin.py --eboot $ARD/bootloaders/eboot/eboot.elf \
  --app gui.elf --flash_mode dout --flash_freq 40 --flash_size 4M \
  --path $TC --out fw_gui.bin
ls -l fw_gui.bin
