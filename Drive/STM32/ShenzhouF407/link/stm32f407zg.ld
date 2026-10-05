/* ==================== 神舟号 STM32F407ZGT6 链接脚本 ====================
 * Flash 1MB @0x08000000；主 SRAM 128KB（112KB SRAM1 + 16KB SRAM2 连续）
 * @0x20000000；CCM 64KB @0x10000000（仅内核 CPU 可访问，本脚本默认不用）。
 * 中断向量表置于 Flash 起始；MSP 中断栈 2KB（FreeRTOS 任务栈走 heap_4）。
 * _Min_Heap_Size=0：libc 堆不用（XMemory 全量回落 pvPortMalloc），
 * 仅保留 _sbrk 兜底拒绝区防静默溢出。
 */
ENTRY(Reset_Handler)

_Min_Heap_Size  = 0x000;
_Min_Stack_Size = 0x1000;

MEMORY
{
    FLASH  (rx)  : ORIGIN = 0x08000000, LENGTH = 1024K
    RAM    (rwx) : ORIGIN = 0x20000000, LENGTH = 128K
    CCMRAM (rwx) : ORIGIN = 0x10000000, LENGTH = 64K
    EXTSRAM (rwx) : ORIGIN = 0x68000000, LENGTH = 1024K
}

_estack = ORIGIN(RAM) + LENGTH(RAM);

SECTIONS
{
    .isr_vector :
    {
        . = ALIGN(4);
        KEEP(*(.isr_vector))
        . = ALIGN(4);
    } > FLASH

    .text :
    {
        . = ALIGN(4);
        *(.text)
        *(.text*)
        *(.rodata)
        *(.rodata*)
        KEEP(*(.init))
        KEEP(*(.fini))
        . = ALIGN(4);
        _etext = .;
    } > FLASH

    .ARM.extab :
    {
        *(.ARM.extab* .gnu.linkonce.armextab.*)
    } > FLASH

    .ARM :
    {
        __exidx_start = .;
        *(.ARM.exidx*)
        __exidx_end = .;
    } > FLASH

    .preinit_array :
    {
        PROVIDE_HIDDEN(__preinit_array_start = .);
        KEEP(*(.preinit_array*))
        PROVIDE_HIDDEN(__preinit_array_end = .);
    } > FLASH

    .init_array :
    {
        PROVIDE_HIDDEN(__init_array_start = .);
        KEEP(*(SORT(.init_array.*)))
        KEEP(*(.init_array*))
        PROVIDE_HIDDEN(__init_array_end = .);
    } > FLASH

    .fini_array :
    {
        PROVIDE_HIDDEN(__fini_array_start = .);
        KEEP(*(SORT(.fini_array.*)))
        KEEP(*(.fini_array*))
        PROVIDE_HIDDEN(__fini_array_end = .);
    } > FLASH

    _sidata = LOADADDR(.data);

    .data :
    {
        . = ALIGN(4);
        _sdata = .;
        *(.data)
        *(.data*)
        . = ALIGN(4);
        _edata = .;
    } > RAM AT > FLASH

    /* 外扩 SRAM 大块保留区：SYSTEM 包装的大分配（≥8KB，如后备存储）
     * 顺序 bump 分配，永不释放（演示期常驻对象）。 */
    .extbuf (NOLOAD) :
    {
        . = ALIGN(8);
        __extbuf_start = .;
        . = . + 16K;
        __extbuf_end = .;
    } > EXTSRAM

    .ccmram (NOLOAD) :
    {
        . = ALIGN(4);
        *(.ccmram)
        *(.ccmram*)
        . = ALIGN(4);
    } > CCMRAM

    .bss (NOLOAD) :
    {
        . = ALIGN(4);
        _sbss = .;
        *(.bss)
        *(.bss*)
        *(COMMON)
        . = ALIGN(4);
        _ebss = .;
    } > RAM

    /* libc 辅助堆（newlib stdio 缓冲等零星 malloc；放 CCM 不占主 RAM）。
     * 主应用内存走 FreeRTOS heap_4（XMemory 回落 pvPortMalloc）。 */
    .sysheap (NOLOAD) :
    {
        . = ALIGN(8);
        __heap_start = .;
        . = . + 16K;
        __heap_limit = .;
    } > CCMRAM

    /* FreeRTOS 堆（heap_4 ucHeap 经 XINYUE_HEAP_SECTION=".extheap" 归入）：
     * 外扩 1MB SRAM，XMemory SYSTEM API（pvPortMalloc）的落点。 */
    .extheap (NOLOAD) :
    {
        . = ALIGN(8);
        *(.extheap)
        . = ALIGN(8);
    } > EXTSRAM

    /* 片内 RAM 保留区：XMultiPool 分级子池（board_mem 接管）。 */
    .intpool (NOLOAD) :
    {
        . = ALIGN(8);
        __intpool_start = .;
        . = . + 4K;
        __intpool_end = .;
    } > RAM

    ._user_heap_stack (NOLOAD) :
    {
        . = ALIGN(8);
        PROVIDE(end = .);
        PROVIDE(_end = .);
        . = . + _Min_Heap_Size;
        . = . + _Min_Stack_Size;
        . = ALIGN(8);
    } > RAM

    /DISCARD/ :
    {
        libc.a ( * )
        libm.a ( * )
        libgcc.a ( * )
    }

    .ARM.attributes 0 : { *(.ARM.attributes) }
}
