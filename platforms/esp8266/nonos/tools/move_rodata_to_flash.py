"""把生成的 eagle.app.v6.common.ld 改两处：

1) .rodata 输出段的目的 region：dram0_0_seg -> irom0_0_seg。
   ESP8266 的 irom0 区已由 cre.S 的 call_cache_enable_dout() 建立映射，
   irom0.text 本就是可读数据（l32r 字面量就住在那里），所以把 26KB 的常量
   搬到 flash 不引入额外的取数风险，却能把 DRAM 腾给 heap_4。

2) 把 .bss 块整体前移到 .data 块之后。
   输出段没有显式地址时 ld 沿用「当前 dot」，.rodata 挪进 irom0 之后，
   紧随其后的 .bss 会被一起拖到 0x4027xxxx，报
   "section .bss is not within region dram0_0_seg"。把 .bss 提前到 .data
   之后（此时 dot 还在 dram0_0_seg），就与 .rodata 的先后无关地落回 DRAM。
   试过但无效的两条方案（留档）：(a) 给 .rodata 加 irom0_0_phdr 而不动 .bss；
   (b) `.bss (dram0_0_seg + SIZEOF(.data)) (NOLOAD)` —— ld 报
   "non constant or forward reference address expression for section .bss"。
"""
import sys

path = sys.argv[1]
s = open(path, encoding='utf-8').read()

# ---- 1. .rodata 换 region ----
marker = '\n    _rodata_end = ABSOLUTE(.);'
i = s.index(marker) + 1
j = s.index('>', i)
k = s.index('\n', j)
s = s[:j] + '>irom0_0_seg :irom0_0_phdr' + s[k:]

# ---- 2. .bss 块前移到 .data 块之后 ----
bss_start = s.index('  .bss ALIGN(8) (NOLOAD) : ALIGN(4)')
bss_endmark = '  } >dram0_0_seg :dram0_0_bss_phdr\n'
bss_end = s.index(bss_endmark, bss_start) + len(bss_endmark)
bss_block = s[bss_start:bss_end]
s = s[:bss_start] + s[bss_end:]

data_endmark = '  } >dram0_0_seg :dram0_0_phdr\n'
data_end = s.index(data_endmark) + len(data_endmark)
# .dram.rodata：.rodata 整段搬去 flash 之后，仍必须留在 DRAM 的少数几张表。
# 目前只有 app.c 的 partition_item_t at_partition_table —— SDK 的
# system_partition_table_regist 在 user_pre_init 里按指针直接读它，走 flash
# 窗口会拿到代码字节（真机实测 "please check partition type 62080785"）。
# 输出段必须排在 .bss 之前：ld 对 NOLOAD 段之后的下一个段仍用上一个段的
# region dot，排在 .bss 之后会被丢进 irom（实测 .dram.rodata 落到 0x4026xxxx）。
dram_block = ('  .dram.rodata : ALIGN(4)\n'
              '  {\n'
              '    _dram_rodata_start = ABSOLUTE(.);\n'
              '    *(.dram.rodata)\n'
              '    *(.dram.rodata.*)\n'
              '    _dram_rodata_end = ABSOLUTE(.);\n'
              '  } >dram0_0_seg :dram0_0_phdr\n')
s = s[:data_end] + dram_block + bss_block + s[data_end:]

open(path, 'w', encoding='utf-8').write(s)
print('rodata -> irom0_0_seg:irom0_0_phdr ; .dram.rodata + .bss moved right after .data')
