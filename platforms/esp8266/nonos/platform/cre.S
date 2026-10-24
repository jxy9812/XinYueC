    .section .text
    .literal_position
    .global call_cache_enable_dout
    .type call_cache_enable_dout, @function
call_cache_enable_dout:
    /* 直写 SPI0(cache) 寄存器建立 SIO Fast Read 映射：
     * SPI_CTRL(0x60000208) = FASTRD_MODE(bit13)
     * SPI_CLOCK(0x60000218) = 0x00003043 (ROM 实测可用值)
     * SPI_USER1(0x60000220) 低字节 dummy=8 */
    movi a2, 0x60000208
    movi a3, 0x2000
    s32i a3, a2, 0
    movi a2, 0x60000218
    movi a3, 0x3043
    s32i a3, a2, 0
    movi a2, 0x60000220
    l32i a3, a2, 0
    movi a4, 0xffffff00
    and a3, a3, a4
    movi a4, 8
    or a3, a3, a4
    s32i a3, a2, 0
    ret
