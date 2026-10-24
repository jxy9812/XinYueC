/* partition_stub.c — 新机迁移的 NONOS3V0 库未导出 Arduino 侧
 * system_partition_table_regist；取证固件已经固定使用 4MB 非 FOTA 布局，
 * 分区表内容仍由 app.c 保留，这里只满足启动链的注册调用并返回成功。
 * 旧机原版库若恢复，删除此桩即可回到 SDK 实现。 */
#include <stdbool.h>
#include <stddef.h>

typedef unsigned int uint32;
typedef unsigned char uint8;

typedef enum {
    SYSTEM_PARTITION_INVALID = 0,
    SYSTEM_PARTITION_BOOTLOADER,
    SYSTEM_PARTITION_OTA_1,
    SYSTEM_PARTITION_OTA_2,
    SYSTEM_PARTITION_RF_CAL,
    SYSTEM_PARTITION_PHY_DATA,
    SYSTEM_PARTITION_SYSTEM_PARAMETER
} partition_type_t;

typedef struct {
    partition_type_t type;
    uint32 addr;
    uint32 size;
} partition_item_t;

bool system_partition_table_regist(const partition_item_t* table,
                                   uint8 item_count,
                                   uint8 flash_size_map)
{
    (void)table;
    (void)item_count;
    (void)flash_size_map;
    return true;
}
