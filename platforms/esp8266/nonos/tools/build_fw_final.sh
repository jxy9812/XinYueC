#!/bin/bash
# ESP8266 NONOS3 合体固件：shell(XCoreApplication+XConsoleShell) + XGui(SSD1306
# oled3 验证�? I2C) + lwip + 【单线程事件�?�?，无 FreeRTOS】�??
#
# �?�?（去 V9 + 分配器改 XMemory）�?��?�改�?�?
#   1) 源集�? = console 210 + gui 增量 74 = 284，用合并档�??
#      xgui_shell_esp8266.h（XGui 档�?? + 打开 shell �?关）全量重编�?
#   2) 【删�? V9】仓�? FreeRTOS 内核（tasks/list/queue/heap_4）与�?�?录移植层
#      port.c / port_isr.c / portasm.S 全部不再编译、不再链接；链接脚本也不�?
#      注入�?�? L1/L2/KEX 向量槽（V9_VECTORS 默�?? 0），回到 SDK 原生向量�?
#      filelist �? Drive/FreeRTOS/** 全部剔除（Sync 适配层随平台判定失效）�??
#   3) 事件�?�?回到单线程：user_init 跑完首拍后�?�返回�?�，�? NONOS �? ets_run()
#      �? os_timer/WiFi 回调；每 5ms �?�? cont_run �? XCoreApplication_processEvents
#      （app.c）�?�任务栈�?4~8KB/�?）全部不�? DRAM�?
#   4) 分配器唯�?化：stubs.c 的全�? C 运�?�时入口（malloc/free/calloc/realloc �?
#      _r 家族、NONOS �? pvPort* 胖�?�名）转接到框架�?�? XMemory 的两块池
#      （�?�级固定�? �?256B / 全局 TLSF �?变池 >256B，arena �? .bss 静�?�缓冲）�?
#   5) 全局 -D__FreeRTOS__ 撤除（→ CXinYueConfig.h:24 判定 XPLATFORM_BAREMETAL=1）�??
#      注意必须�? recompile_final_one.sh 完全�?致，否则同一头文件在不同 TU �?
#      展开出不同结构体布局（ODR 事故）�??
#
# 铁律（未改）：cre.S �? cache、app_entry 手清 .bss�?--defsym �? ROM 三�?�号 +
# ets_memcpy�?--wrap spi_flash 三件、putc1 钩子、分区表、长�?�?喂狗�?
set -e
SELF_DIR="$(cd "$(dirname "$0")" && pwd)"    # .../platforms/esp8266/nonos/tools
cd "$SELF_DIR/.."                            # NONOS ģ���
# ���й����м����ͳһ�� build/������ʱ����ɾ���رࣩ
B=build
mkdir -p "$B"
# �ֿ����2026-10-24 ģ��Ǩ�� platforms/ �������ļ���ԭ��������
# �� esp_env.sh / recompile_final_one.sh / build.py ����ͬ������
ROOT="$(cd "$SELF_DIR/../../../.." && pwd)"     # �ֿ��
CFG="$PWD/cfg/xgui_shell_esp8266.h"
REPO=$ROOT/Library/lwip

# ---- 外部工具链路径（换电脑只�? tools/esp_env.sh 或�?�环境变量）----
# 提供 TC / ARD / SDK / CORE / SDKINC / NONOSINC / EXTRAINC / EB2BIN / EBOOT�?
# 缺路径会在这里直接报错并给出处理方式，不会跑到一半才失败�?
. "$SELF_DIR/esp_env.sh"
_esp_check_paths || exit 1

# ---- 0. 链接脚本生成（照底座�? ----
# V9 总闸：唯�?取�?�来源是裁剪档�?? xgui_shell_esp8266.h �?
# #define XESP8266_FREERTOS_V9_ON <0|1>（默�? 0）�?�脚�?读它决定�?
#   V9=0：不�?/不链 V9 内核 + XTENSA_LX106 移�?�层 + heap_4；不�?
#         xTaskStartScheduler（app.c �? #if 分支不出编译）；链接脚本�?
#         注入�?研向量槽；主�?�? = XCoreApplication 事件�?�?（单线程）�??
#   V9=1：编/�? Drive/FreeRTOS/** �? Drive/FreeRTOS/portable/GCC/XTENSA_
#         _LX106/{port.c,port_isr.c,portasm.S}，heap_4 重新接�?�堆，链�?
#         脚本注入 .MyV2/.MyK/.MyU 向量槽，app.c �? v9_app_task +
#         xTaskStartScheduler。⚠=1 分支�?�?�?在真机验证�??
V9=$(sed -n 's/^[[:space:]]*#define[[:space:]]*XESP8266_FREERTOS_V9_ON[[:space:]]*\([01]\).*/\1/p' "$CFG" | head -1)
[ -z "$V9" ] && V9=0
echo "== XESP8266_FREERTOS_V9_ON=$V9�?$([ "$V9" = 1 ] && echo 'V9 内核+移�?�层+heap_4 编入, 起调度器' || echo '单线程事件循�?, V9 不编�?')�?=="

$TC/xtensa-lx106-elf-gcc -CC -E -P -DVTABLES_IN_FLASH \
  -DMMU_IRAM_SIZE=0x8000 -DMMU_ICACHE_SIZE=0x8000 \
  $SDK/ld/eagle.app.v6.common.ld.h -o $B/local.eagle.app.v6.common.ld
sed -i 's|INCLUDE "../ld/eagle.rom.addr.v6.ld"|INCLUDE "'$SDK'/ld/eagle.rom.addr.v6.ld"|' $B/local.eagle.app.v6.common.ld
# V9 移�?�层�? .MyV2/.MyK/.MyU 向量槽：�? V9 总闸�?起开/�?
# （V9=0 时回�? SDK 原生向量�?=1 时才注入�?研槽）�??
if [ "$V9" = "1" ] || [ "${V9_VECTORS:-0}" = "1" ]; then
sed -i 's|    \*(.DebugExceptionVector.text)|    KEEP(*(.MyV2.text))\n    *(.DebugExceptionVector.text)|' $B/local.eagle.app.v6.common.ld
sed -i 's|    \*(.KernelExceptionVector.text)|    KEEP(*(.MyK.text))\n    *(.KernelExceptionVector.text)|' $B/local.eagle.app.v6.common.ld
sed -i 's|    \*(.UserExceptionVector.text)|    KEEP(*(.MyU.text))\n    *(.UserExceptionVector.text)|' $B/local.eagle.app.v6.common.ld
fi
cp $SDK/ld/eagle.flash.4m1m.ld $B/local.eagle.flash.ld
# ---- 0.6 .rodata �? DRAM �?�? irom0（flash�?----
# �?�? DRAM �?�? 96KB�?.data(1.7KB) + .rodata(26KB) + .bss(35KB) 全挤�?
# dram0_0_seg 里，留给 heap_4 的静态堆�?�? ~32KB，�?�实�? 24KB 以上的堆�?
# 让固件在 SDK 首�?? flash 读之前就挂�?�（ROM 0x400042B9 / cause 20
# InstFetchProhibited，�?? cap_t16v_on.log 的�?�照记录）�??
# NONOS �? irom0 区由 app_entry �? cre.S call_cache_enable_dout() 建立映射�?
# irom0.text �?来就�?�?读数�?（l32r 字面量就住在那里），因�?�把 .rodata
# �?并放�? irom0_0_seg �?同一条已验证的映射，不�?��?�引入取�?/取数风险�?
# RODATA_IN_FLASH=0 �?�?回原布局做�?�照�?
if [ "${RODATA_IN_FLASH:-1}" = "1" ]; then
  $PY tools/move_rodata_to_flash.py $B/local.eagle.app.v6.common.ld
  grep -n "_rodata_end" -A1 $B/local.eagle.app.v6.common.ld | head -4
fi
# DRAM 区扩展：SDK 默�?? dram0_0_seg �?�? 0x3FFFC000�?96KB DRAM 扣掉顶部
# 16KB SYS 栈）。本�?�? V9 后应用不再跑在任何任务栈上（app �? cont 4KB 栈，
# SYS 栈只�? user_init 期间�? SDK �?），顶部�?保留 1KB�?
# dram0_0_seg 扩到 0x17C00�?0x3FFE8000+0x17C00 = 0x3FFFFC00）�??
sed -i 's|len = 0x14000|len = 0x17C00|' $B/local.eagle.flash.ld
grep -n "dram0_0_seg" $B/local.eagle.flash.ld

mkdir -p "$B/_obj"
# $B/obj/ ��ű��� TU �� .o������������rm -rf obj��ʱ�����ؽ������� gcc ��
# "Fatal error: can't create $B/obj/xxx.o: No such file or directory"
mkdir -p "$B/obj"

# ---- 1. 源集合：console 210 + gui 增量 74 = 284，全量重编（档�?�变了）�?
#         Drive/FreeRTOS/** 的去留�?�完全由 V9 总闸决定】：
#           V9=0：整�?录剔除（内核 tasks/list/queue + heap_4 + Sync 适配�?
#                 + 移�?�层 port.c/port_isr.c/portasm.S 都不参与编译）�?�文�?
#                 �?�?没删，全部在仓库 Drive/FreeRTOS/portable/GCC/XTENSA_
#                 LX106/ �? Source/、portable/MemMang/ 里，进版�?控制�?
#           V9=1：保留内�? + Sync 适配层（�? -D__FreeRTOS__），移�?�层在下�?
#                 �? 3.5 步单�?编�?? ----
if [ "$V9" = "1" ]; then
  V9DEFS="-D__FreeRTOS__"
  cat $ROOT/build/esp8266_console/filelist.txt $ROOT/build/arduino_gui/filelist_gui_extra.txt \
    | sed '/^$/d' \
    | grep -v "Drive/FreeRTOS/portable/GCC/XTENSA_LX106/port_isr.c" \
    | sort -u > $B/_obj/filelist_final.txt
else
  V9DEFS=""
  # 2026-10-21 新机适配：esp8266_console（filelist 来源）尚�?随旧机拷齐时�?
  # 直接复用上�?�构建留下的合并清单（V9=0 版本，与�?�? V9 �?致）�?
  if [ -f "$PWD/_obj/filelist_final.txt" ]; then
    echo "== filelist: reuse existing $B/_obj/filelist_final.txt =="
  else
    cat "$PWD/cfg/filelist_final.txt" > $B/_obj/filelist_final.txt  # conf 清单�? sort -u 去重；�?��?�不得再�? sort（Windows sort.exe 不�?? -u�?
  fi
fi
echo "== filelist: $(wc -l < $B/_obj/filelist_final.txt) files (V9=$V9) =="
# ��β�Լ죺bash �� `read -r` �ᱣ�� CRLF �� \r����������ƴ����ÿ�� -I ��ָ��
# �����ڵ�Ŀ¼ �� ���� "ͷ�ļ�ȱʧ / Invalid argument"����������޹�����
# �б��ļ���cfg/*.txt������� LF���� esp8266/docs/PITFALLS.md A9��
for _lf in "$PWD/cfg/inc_dirs.txt" "$PWD/cfg/filelist_final.txt"; do
  if [ -f "$_lf" ] && grep -q $'\r' "$_lf"; then
    echo "!! ��β����: $_lf �� CRLF��bash read ����� \\r������ -I ·��ʧЧ��"
    echo "   �޷�: python -c \"import sys;p=sys.argv[1];d=open(p,'rb').read();open(p,'wb').write(d.replace(b'\\r\\n',b'\\n'))\" \"$_lf\""
    exit 1
  fi
done
# FORCE_ALL=1 时全量重编（档�?�头改动后必须用，否则�?�量脚本�?比�?? .c �? mtime�?
[ "${FORCE_ALL:-0}" = "1" ] && rm -f $B/_obj/*.o
V9DEFS="$V9DEFS" xargs -P 6 -I{} bash "$SELF_DIR/recompile_final_one.sh" {} < $B/_obj/filelist_final.txt

# ---- 2. lwip 组成（照 arduino_lwip/final：esp 侧胶水抽�? + 仓库 core 重命名） ----
rm -rf "$B/_esp" && mkdir -p "$B/_esp"
(cd "$B/_esp" && $TC/xtensa-lx106-elf-ar x $SDK/lib/liblwip2-536.a lwip-esp.o)
cp -f $B/_esp/lwip-esp.o $B/lwip_esp_side.o

INCDIRS="-I. -I$PWD/cfg/shim \
 -I$ROOT/Library/xxHash -I$ROOT/Library/zlib -I$NONOSINC \
 -I$CORE -I$EXTRAINC \
 -I$SDKINC -I$SDKINC/esp8266 -I$SDK/lwip2/include -I$REPO/include"
while IFS= read -r d; do INCDIRS="$INCDIRS -I$ROOT/$d"; done < "$PWD/cfg/inc_dirs.txt"

# lwip TU 头路径：repo lwip + 模板�?�? + Src/Drive 全部子目�?
# （cc.h→XPrintf.h→CXinYueConfig 链需要；不含 SDK/NONOS 头，避免
#   nonos_v305 ip_addr �? lwip2 ip_addr 的重定义冲突�?
LWINC="-I. -I$PWD/platform -I$REPO/include -I$ROOT/Library/xxHash -I$ROOT/Library/zlib"
while IFS= read -r d; do LWINC="$LWINC -I$ROOT/$d"; done < "$PWD/cfg/inc_dirs.txt"

REN="-Dlwip_init=lwip_init_r221 -Dnetif_add=netif_add_r221 -Dnetif_remove=netif_remove_r221 \
 -Dnetif_set_addr=netif_set_addr_r221 -Dnetif_set_default=netif_set_default_r221 \
 -Dnetif_set_up=netif_set_up_r221 -Dnetif_set_down=netif_set_down_r221 \
 -Dpbuf_alloc=pbuf_alloc_r221 -Dpbuf_free=pbuf_free_r221 -Dpbuf_ref=pbuf_ref_r221 \
 -Dethernet_input=ethernet_input_r221 -Detharp_output=etharp_output_r221 \
 -Dethbroadcast=ethbroadcast_r221 -Dnetif_default=netif_default_r221 \
 -Dsys_timeout=sys_timeout_r221 -Dsys_untimeout=sys_untimeout_r221"

CFLAGS_LW="-c -Os -g -free -fipa-pta -Werror=return-type -nostdlib -mlongcalls \
 -mtext-section-literals -falign-functions=4 -ffunction-sections -fdata-sections \
 -fcommon \
 -DNONOSDK305=1 -DF_CPU=80000000L -DVTABLES_IN_FLASH -DARDUINO=10819"
SDKLWINC="-I$SDKINC -I$SDK/lwip2/include -I$CORE -I$PWD/platform -DLWIP_OPEN_SRC=1"

rm -rf "$B/_lwip" && mkdir -p "$B/_lwip"
# ==== ������բ��2026-10-22����XESP8266_NET_ON=0 ʱ���� lwip ������ ====
# Ŀ�ģ��ѡ����ԭ���¼�ѭ��������ֲ������ջ�����ܵĶ�ʱ����ϵ
# ��ʱ���� / CoarseTimer���� lwip �޹أ��Ȱ��������ժ�ɾ��������ж�
# ���ԭ����̬��������Ƿ���á���������ġ����ض����
# cfg/xgui_shell_esp8266.h��Ĭ�� 0 = �أ���
NET_ON=0
if [ "${XESP8266_NET_ON:-0}" = "1" ]; then NET_ON=1; fi
echo "== NET=$NET_ON (lwip always built: SDK libnet80211/libwpa need it) =="
# ==== 2026-10-20 ����ջ�ع�����ص�������֤��ʵ�� ====
# 教�??（务必�?�完再改）：
#   lwip2 �? glue 层（lwip-esp.o）是 WiFi 驱动与应用协�?栈之间�?�唯�?】�?�路�?
#     esp2glue_alloc_for_recv() 向应用�?�缓冲，返回 NULL 则�?�包直接丢弃�?
#   "BYPASS / �? SDK �?占数�?�?"�?错�??设想—�?�SDK 没有�?�?立工作的平�?�协�?栈，
#   那样做等于丢掉每�?�?包（表现�? ping 100% 超时、且因为没流量所以也不崩）�??
#   �?用的方�?�只有：应用提供缓冲、把包喂给仓�? lwip（底座固件的做法�?0% 丢包）�??
#   驱动文件�? build/arduino_lwip 逐字节一致（md5 c20f42be...），勿再�? TX �?义�??
LW_SRCS="core/def.c core/inet_chksum.c core/init.c core/ip.c core/mem.c core/memp.c \
 core/netif.c core/pbuf.c core/raw.c core/stats.c core/sys.c core/tcp.c core/tcp_in.c \
 core/tcp_out.c core/timeouts.c core/udp.c \
 core/ipv4/etharp.c core/ipv4/icmp.c core/ipv4/ip4.c core/ipv4/ip4_addr.c core/ipv4/ip4_frag.c \
 netif/ethernet.c"
for f in $LW_SRCS; do
  o=$B/_lwip/$(echo "$f" | tr / _).c.o
  $TC/xtensa-lx106-elf-gcc $CFLAGS_LW $LWINC $REN "$REPO/$f" -o "$o"
done
$TC/xtensa-lx106-elf-gcc $CFLAGS_LW $LWINC $SDKLWINC $REN \
  platform/XDeviceNetwork_lwip_esp8266.c -o $B/_lwip/XDeviceNetwork_lwip_esp8266.c.o

# ---- 3. �?地三�? + OLED 驱动（oled3 验证版，时序�?改） ----
# ⚠️ �?地这几个 TU 的宏集必须与 recompile_final_one.sh 逐字�?致（�?�?�?大的
# 隐患就是 -D__FreeRTOS__ �?在批量配方里�?撤掉、本地这几个 TU 仍带�?�?
# 会�?�同�?头文件在两类 TU 里展�?出不同布�?）�?�故此�?�同样不定义 __FreeRTOS__
# 与任�? X9_* 旋钮�?

CFLAGS="-std=gnu99 -Os -c -DXCHAR_NO_SYSTEM_GBK=1 \
 -mlongcalls -mtext-section-literals -falign-functions=4 \
  -ffunction-sections -fdata-sections -include $CFG \
  -D__ESP8266__ -D__ets__ -DICACHE_FLASH \
  -DCONT_STACKSIZE=6144 \
  -DF_CPU=80000000L -DNONOSDK305=1 -DLWIP_OPEN_SRC \
  -DTCP_MSS=536 -DLWIP_FEATURES=1 -DLWIP_IPV6=0 -DARDUINO=10819 \
  -Wno-implicit-function-declaration -Wno-builtin-declaration-mismatch \
  $V9DEFS $INCDIRS"

echo "== CFG: V9=$V9, $([ "$V9" = 1 ] && echo 'FreeRTOS(xTaskStartScheduler)' || echo 'baremetal(single-thread)'), XMemory pools =="
# user_interface.h �? NONOS v305 版在�?定义 LWIP_OPEN_SRC 时包�? legacy
# ip_addr.h（其�?定义 struct ip_info）；�?机迁移的 Arduino lwip 头会�?
# LWIP_OPEN_SRC 下遮蔽它，�?�致 app.c 的分�?/SoftAP 入口无法重编�?
CFLAGS_APP="$CFLAGS -ULWIP_OPEN_SRC"

# ---------------------------------------------------------------------------
# �?�? TU 增量编译�?2026-10-22 重写�?
#
# �? 旧写法是 `[ -f $B/obj/xxx.o ] || gcc ...` —�?? �?�? .o 存在就�?�永不重编�?��??
#   后果（真实踩过）：①源文件改了不生效；②编译失败留下的旧 .o / �?的机�?
#   拷来�? .o �?当成有效产物；③`XSerialPortESP8266.c` 因头文件缺失编不过，
#   就一直靠旧机 .o 顶着，一旦�?? .o 丢失就彻底构建不了，且脚�?【不报错】�??
#
# 现在的�?�则（标�? mtime 增量）：
#   .o 不存�?              �? �?
#   .o 比源文件�?          �? �?
#   .o 比全�?档�?? CFG �?   �? 编（CFG �? -include 强制包含，改它等于改�?�? TU�?
#   否则                   �? 跳过
# 任何�?次编译失败都【立即�??出并打印错�??】，不再静默留旧产物�?
#
# 用法：build_tu <输出.o> <源文�?> [额�?�编译参�?...]
# ---------------------------------------------------------------------------
build_tu() {
  _out="$1"; _src="$2"; shift 2
  if [ ! -f "$_src" ]; then
    echo "!! 源文件不存在: $_src"; exit 1
  fi
  if [ ! -f "$_out" ] || [ "$_src" -nt "$_out" ] || [ "$CFG" -nt "$_out" ]; then
    echo "  CC  $_src"
    if ! $TC/xtensa-lx106-elf-gcc $CFLAGS "$@" "$_src" -o "$_out"; then
      echo "!! 编译失败: $_src"
      echo "   提示：若报头文件/类型缺失，先�?�? cfg/shim �? cfg/inc_dirs.txt 的路径；"
      echo "        不�?�用�? .o 顶替—�?�那会�?�问题延后到换机时才爆�??"
      exit 1
    fi
  fi
}

# ---- 入口与驱动层 ----
# main.c �?�? -ULWIP_OPEN_SRC（�?? CFLAGS_APP 上方说明），作为额�?�参数传入；
# 注意 build_tu �? $CFLAGS 在参数之前展�?�?-U 在其后生效，顺序正确�?
build_tu $B/obj/main.c.o main.c -ULWIP_OPEN_SRC
build_tu $B/obj/XDeviceDisplay_ssd1306_i2c.c.o app/XDeviceDisplay_ssd1306_i2c.c
build_tu $B/obj/drivers_xdevice_uart.c.o drivers/xdevice_uart.c
build_tu $B/obj/drivers_xdevice_oled.c.o drivers/xdevice_oled.c
build_tu $B/obj/drivers_xdevice_init.c.o drivers/xdevice_init.c
# 用户业务：存在才编（覆盖 main.c 的弱符号）；不存在则�? main.c 空弱实现�?
[ -f app/user_business.c ] && build_tu $B/obj/user_business.c.o app/user_business.c

# ---- 平台�? ----
# 单线程同步原�?后�??：取�? Drive/FreeRTOS/Sync/*FreeRTOS.c（那四个文件整体�?
# #ifdef __FreeRTOS__ 包着，V9=0 时不编译，链接期就缺 XMutex_platform_* /
# XSemaphore_* / XWaitCondition_* / XThread_* 四族符号—�?��?�轮链接实证）�??
# V9=1 时改由仓�? Sync 适配层提供（�? filelist），�?文件必须让位，否则重复定义�??
[ "$V9" = "1" ] || build_tu $B/obj/sync_baremetal.c.o platform/sync_baremetal.c
# stubs.c 刻意不带 -include CFG 的完�? CFLAGS（保持与原配方一致：-fno-builtin
# + �?注入必�?�的 SDK 宏），故单独写一条�?�则�?
if [ ! -f $B/obj/stubs.c.o ] || [ platform/stubs.c -nt $B/obj/stubs.c.o ] || [ "$CFG" -nt $B/obj/stubs.c.o ]; then
  echo "  CC  platform/stubs.c"
  $TC/xtensa-lx106-elf-gcc -std=gnu99 -c -Os -fno-builtin -mlongcalls -mtext-section-literals \
    -ffunction-sections -fdata-sections -include $CFG -D__ESP8266__ -D__ets__ -DICACHE_FLASH \
    -DCONT_STACKSIZE=6144 \
    -DNONOSDK305=1 -DARDUINO=10819 $INCDIRS platform/stubs.c -o $B/obj/stubs.c.o || {
      echo "!! 编译失败: platform/stubs.c"; exit 1; }
fi
# cre.S：cache 建立汇编（只依赖�?�?，不依赖 CFG�?
if [ ! -f $B/obj/cre.o ] || [ platform/cre.S -nt $B/obj/cre.o ]; then
  echo "  AS  platform/cre.S"
  $TC/xtensa-lx106-elf-gcc -c -mlongcalls platform/cre.S -o $B/obj/cre.o || {
    echo "!! 汇编失败: platform/cre.S"; exit 1; }
fi
# cont.S：Arduino core 同�?�上下文切换（main.c �? 4KB cont 栈泵要用）�??
# V9 移�?�层�? port.c 里自带一份，�? V9 后它随之消失 �? �? core 取回原版�?
if [ ! -f $B/obj/cont.o ] || [ "$CORE/cont.S" -nt $B/obj/cont.o ]; then
  echo "  AS  $CORE/cont.S"
  $TC/xtensa-lx106-elf-gcc -c -mlongcalls "$CORE/cont.S" -o $B/obj/cont.o || {
    echo "!! 汇编失败: cont.S"; exit 1; }
fi
# NONOS3 必需�? RF 校准扇区回调（官方原�? libmain.a 无默认实现）
if [ ! -f $B/obj/rf_cal_stub.o ] || [ platform/rf_cal_stub.c -nt $B/obj/rf_cal_stub.o ]; then
  echo "  CC  platform/rf_cal_stub.c"
  $TC/xtensa-lx106-elf-gcc -std=gnu99 -c -Os -mlongcalls -fno-builtin \
    platform/rf_cal_stub.c -o $B/obj/rf_cal_stub.o || { echo "!! 失败: rf_cal_stub"; exit 1; }
fi
if [ ! -f $B/obj/partition_stub.o ] || [ platform/partition_stub.c -nt $B/obj/partition_stub.o ]; then
  echo "  CC  platform/partition_stub.c"
  $TC/xtensa-lx106-elf-gcc -std=gnu99 -c -Os -mlongcalls -fno-builtin \
    platform/partition_stub.c -o $B/obj/partition_stub.o || { echo "!! 失败: partition_stub"; exit 1; }
fi
# 平台安全随机数填充：XRandomGenerator.c 以�?�部符号要求（仓库缺
# XRandomGenerator_platform.c），ESP8266 侧用�?�? RNG 实现�?
if [ ! -f $B/obj/xdevice_random.o ] || [ platform/xdevice_random.c -nt $B/obj/xdevice_random.o ]; then
  echo "  CC  platform/xdevice_random.c"
  $TC/xtensa-lx106-elf-gcc -std=gnu99 -c -Os -mlongcalls -fno-builtin \
    platform/xdevice_random.c -o $B/obj/xdevice_random.o || { echo "!! 失败: xdevice_random"; exit 1; }
fi

# ---- 3.5 V9 移�?�层（仓�? Drive/FreeRTOS/portable/GCC/XTENSA_LX106/�?
#          �? V9=1 编译；V9=0 时这三个文件在仓库里但不进构�? �? �?�? elf/map
#          里查不到 pxPortInitialiseStack 等任务�?�号（nm 验证见脚�?�?尾）�? ----
V9PORT_OBJS=""
if [ "$V9" = "1" ]; then
  PORTDIR=$ROOT/Drive/FreeRTOS/portable/GCC/XTENSA_LX106
  $TC/xtensa-lx106-elf-gcc $CFLAGS $PORTDIR/port.c     -o $B/obj/port.c.o
  $TC/xtensa-lx106-elf-gcc $CFLAGS $PORTDIR/port_isr.c -o $B/obj/port_isr.c.o
  $TC/xtensa-lx106-elf-gcc -c -mlongcalls -mtext-section-literals \
    $PORTDIR/portasm.S -o $B/obj/portasm.o
  V9PORT_OBJS="$B/obj/port.c.o $B/obj/port_isr.c.o $B/obj/portasm.o"
  echo "== V9 port layer compiled: $V9PORT_OBJS =="
fi

# ---- 4. V9 内核与自研移植层（port.c / port_isr.c / portasm.S）本�?全部
#         删除：不编译、不链接、文件保留在仓库做历史�?�照。任务栈随之
#         �? DRAM 里整体消失�?? ----

# ---- 5. 对象清单（严格按 filelist 生成，防历史残留混入�? ----
: > $B/obj/all_objs.txt
while IFS= read -r rel; do
  [ -z "$rel" ] && continue
  echo "$B/_obj/$(echo "$rel" | tr '/' '_').o" >> $B/obj/all_objs.txt
done < $B/_obj/filelist_final.txt
for o in $B/obj/main.c.o $B/obj/XDeviceDisplay_ssd1306_i2c.c.o \
         $B/obj/drivers_xdevice_uart.c.o $B/obj/drivers_xdevice_oled.c.o $B/obj/drivers_xdevice_init.c.o \
         $B/obj/stubs.c.o $B/obj/cre.o \
         $B/obj/cont.o $B/obj/rf_cal_stub.o $B/obj/partition_stub.o $B/obj/xdevice_random.o; do
  echo "$o" >> $B/obj/all_objs.txt
done
# lwip objects stay in the image even with NET=0: the SDK's libnet80211 /
# libwpa reference pbuf_alloc / dhcp_start / netif_set_addr unconditionally,
# so the linker needs them. NET=0 only means "do not initialise the network
# and do not pump the lwip timers" (see main.c) �� the framework event loop
# then runs with the network stack idle, which is what isolates the
# framework's own behaviour.
echo "$B/lwip_esp_side.o" >> $B/obj/all_objs.txt
for o in $B/_lwip/*.o; do echo "$o" >> $B/obj/all_objs.txt; done
# 用户业务（可选）：存在才链接
[ -f $B/obj/user_business.c.o ] && echo "$B/obj/user_business.c.o" >> $B/obj/all_objs.txt
# V9=0 才带 sync_baremetal（V9=1 时由仓库 Sync 适配层顶上）；V9=1 才带移�?�层
[ "$V9" = "1" ] || echo "$B/obj/sync_baremetal.c.o" >> $B/obj/all_objs.txt
for o in $V9PORT_OBJS; do [ -n "$o" ] && echo "$o" >> $B/obj/all_objs.txt; done
missing=0
while IFS= read -r o; do
  [ -f "$o" ] || { echo "MISSING OBJ: $o"; missing=1; }
done < $B/obj/all_objs.txt
[ "$missing" = "0" ] || exit 1
echo "== objs: $(wc -l < $B/obj/all_objs.txt) =="

# ---- 6. 链接（底座配方：--defsym �? ROM + --wrap spi_flash 三件 + 完整库组�? ----
$TC/xtensa-lx106-elf-gcc -g -Os -nostdlib -Wl,--no-check-sections -u app_entry \
 -Wl,-static -Wl,-wrap,spi_flash_read -Wl,-wrap,spi_flash_write \
 -Wl,-wrap,spi_flash_erase_sector \
 -Wl,--defsym=ets_post=0x40000e24 -Wl,--defsym=ets_intr_lock=0x40000f74 \
 -Wl,--defsym=ets_intr_unlock=0x40000f80 -Wl,--defsym=ets_memcpy=0x400018b4 \
 -L$SDK/lib -L$SDK/lib/NONOSDK305 -L"$B" \
 -T$B/local.eagle.flash.ld -Wl,--gc-sections -Wl,-Map=$B/fw_final.map -o $B/fw_final.elf \
 @$B/obj/all_objs.txt \
 -Wl,--start-group -lhal -lphy -lpp -lnet80211 -lwpa -lcrypto -lmain -lwps \
 -lespnow -lsmartconfig -lairkiss -lwpa2 -lm -lc -lgcc -Wl,--end-group

echo "== link OK =="
$TC/xtensa-lx106-elf-size.exe $B/fw_final.elf
$TC/xtensa-lx106-elf-nm.exe -u $B/fw_final.elf > $B/undefined_final.txt || true
echo "undefined symbols: $(wc -l < $B/undefined_final.txt)"
# DRAM 占用核�?�：�? V9 任务栈后，静态余量必须落�? dram0_0_seg
# �?0x3FFE8000..0x3FFFFC00）内；越界会�? SDK 首�?? flash 读前挂�?��??
$TC/xtensa-lx106-elf-nm.exe -S --size-sort $B/fw_final.elf | tail -8

# ---- V9 符号核验：V9=0 时最�? elf 里必须�?�一�?都查不到】任�?/调度�?/移�?�层
#      符号。这证明 V9 �?�?没�??编进去（源码在仓库里，�??
#      Drive/FreeRTOS/portable/GCC/XTENSA_LX106/），而不�?�?删掉�? ----
V9SYMS="pxPortInitialiseStack prvPortInitialiseStack xPortStartScheduler \
xTaskStartScheduler vTaskStartScheduler xTaskCreate uxTaskGetTickCount \
vTaskDelay xQueueCreate xQueueGenericCreate prvTaskCheckFreeStackSpace \
uxTaskGetNumberOfTasks vTaskSwitchContext xTaskIncrementTick"
# �?：pvPortMalloc/xPortGetFreeHeapSize �? NONOS libmain 要求的转发入口，
#     stubs.c 有意保留（转发到 XMemory �?变池），不算「任务�?�号」，故不在表内�??
FWDSTUBS="pvPortMalloc vPortFree xPortGetFreeHeapSize pvPortZalloc"
$TC/xtensa-lx106-elf-nm.exe $B/fw_final.elf > $B/nm_final.txt
V9HITS=$(grep -cwE "$(echo $V9SYMS | tr -s ' ' '|')" $B/nm_final.txt || true)
echo "== V9 task-symbol hits in $B/fw_final.elf: $V9HITS (V9=$V9) =="
grep -wE "$(echo $V9SYMS | tr -s ' ' '|')" $B/nm_final.txt | head -20 || true
echo "== allocator forwarding stubs present (expected, stubs.c -> XMemory): $(grep -cwE "$(echo $FWDSTUBS | tr -s ' ' '|')" $B/nm_final.txt || true) =="
grep -wE "$(echo $FWDSTUBS | tr -s ' ' '|')" $B/nm_final.txt | head || true
if [ "$V9" = "0" ] && [ "$V9HITS" != "0" ]; then
  echo "!! V9=0 �? elf 里仍�? V9 任务符号，构建门控失�?"; exit 1
fi

$PY $EB2BIN --eboot $EBOOT \
  --app $B/fw_final.elf --flash_mode dout --flash_freq 40 --flash_size 4M \
  --path $TC --out fw_final.bin
# RODATA_IN_FLASH=1：elf2bin 按�?�列表把 .rodata 排到镜像尾部，�?? XIP �?线�??
# 映射（VMA-0x40200000=raw），错位 ~31KB �? SDK 早期 cache 读常量全垃圾 �?
# 永久停摆（cap_R1c/R1d 真机定位）�?��?��?�把 .rodata 并回 .irom0.text 段头�?
# �? raw == VMA-0x40200000 精确成立（eboot �? 0x40200000+ 段本就跳过）�?
if [ "${RODATA_IN_FLASH:-1}" = "1" ]; then
  $PY tools/fix_rodata_pos.py fw_final.bin
fi
ls -l fw_final.bin
