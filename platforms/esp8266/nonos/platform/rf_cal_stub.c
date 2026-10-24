/* rf_cal_stub.c — NONOS3 要求应用提供 RF 校准扇区号。
 * 迁移来的 SDK libmain.a 是官方原版（无 Arduino 补丁的默认实现），链接期
 * 缺 user_rf_cal_sector_set。4MB flash = 1024 扇区，取 1024-5 = 1019，
 * 与 app.c 分区表 SYSTEM_PARTITION_PHY_DATA @ 0x3FB000 同一扇区。 */
unsigned user_rf_cal_sector_set(void)
{
    return 1019u;
}
