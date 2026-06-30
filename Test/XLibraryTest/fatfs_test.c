#include "ff.h"
#include "diskio.h"
#include <stdio.h>
#include <string.h>
#include "XPrintf.h"
// 定义测试使用的设备编号（对应diskio.c中的DEV_EX_FLASH=0）
#define TEST_DEV 0
#define SECTOR_SIZE		512

#define PATH          "2:"
// 测试UTF-8文件名（包含中文、英文、数字）
#define TEST_UTF8_FILE  PATH "测试文件_123.txt"
// 测试写入内容
#define TEST_CONTENT "Hello FatFs UTF-8! 测试内容：12345"

/**
 * @brief 打印FatFs错误信息
 * @param res 错误码（FRESULT）
 */
static void print_fatfs_error(FRESULT res) {
    switch (res) {
    case FR_OK:           XPrintf("操作成功\n"); break;
    case FR_DISK_ERR:     XPrintf("磁盘I/O错误\n"); break;
    case FR_INT_ERR:      XPrintf("内部错误\n"); break;
    case FR_NOT_READY:    XPrintf("设备未准备好\n"); break;
    case FR_NO_FILE:      XPrintf("文件不存在\n"); break;
    case FR_NO_PATH:      XPrintf("路径不存在\n"); break;
    case FR_INVALID_NAME: XPrintf("文件名无效（可能UTF-8不兼容）\n"); break;
    default:              XPrintf("错误码：%d\n", res);
    }
}
// 计算磁盘大小（转换为MB）
static float calc_mb(DWORD bytes) {
    return (float)bytes / (1024 * 1024);  // 1MB = 1024*1024字节
}
// 获取物理磁盘总大小（字节）
static uint64_t get_physical_disk_size(BYTE pdrv) {
    DSTATUS status;
    DRESULT res;
    DWORD sector_count;  // 总扇区数（32位，若磁盘超过2TB需用64位）
    WORD sector_size;    // 扇区大小（字节）

    // 1. 初始化磁盘
    status = disk_initialize(pdrv);
    if (status != 0) {
        XPrintf("磁盘初始化失败，状态: %d\n", status);
        return 0;
    }

    // 2. 检查磁盘状态
    status = disk_status(pdrv);
    if (status != 0) {
        XPrintf("磁盘状态错误: %d\n", status);
        return 0;
    }

    // 3. 获取扇区大小
    res = disk_ioctl(pdrv, GET_SECTOR_SIZE, &sector_size);
    if (res != RES_OK) {
        XPrintf("获取扇区大小失败: %d\n", res);
        return 0;
    }

    // 4. 获取总扇区数
    res = disk_ioctl(pdrv, GET_SECTOR_COUNT, &sector_count);
    if (res != RES_OK) {
        XPrintf("获取总扇区数失败: %d\n", res);
        return 0;
    }

    // 5. 计算总大小（扇区数 × 扇区大小）
    return (uint64_t)sector_count * sector_size;
}
/**
 * @brief 测试流程：初始化→挂载→创建文件→写入→读取→删除→卸载
 */
void  fatfs_test() 
{
    FRESULT res;
    FATFS fs;         // 文件系统对象
    FIL file;         // 文件对象
    BYTE read_buff[256] = { 0 };  // 读取缓冲区
    UINT bw, br;      // 写入/读取字节数
    BYTE work_buf[3 * 512];  // 格式化用工作缓冲区
    // 1. 初始化磁盘
    //XPrintf("1. 初始化磁盘...\n");
    //DSTATUS disk_stat = disk_initialize(5);
    //if (disk_stat != 0) {
    //    XPrintf("磁盘初始化失败！状态：%d\n", disk_stat);
    //    return -1;
    //}
    //XPrintf("磁盘初始化成功\n");
    // 2. 检查是否已有文件系统（尝试挂载）
    XPrintf("\n2. 检查文件系统...\n");//
    res = f_mount(&fs, PATH, 1);  // 尝试挂载，1=立即挂载
    if (res == FR_OK) {
        XPrintf("检测到已有文件系统，无需格式化\n");
    }
    else if (res == FR_NO_FILESYSTEM) 
    {
       
        // 无有效文件系统，执行格式化
        XPrintf("未检测到文件系统，开始格式化...\n");
        //res = f_mkfs(PATH, FM_FAT32, work_buf, sizeof(work_buf));
        if (res != FR_OK) {
            XPrintf("格式化失败：");
            print_fatfs_error(res);
            return -1;
        }
        XPrintf("格式化成功，重新挂载...\n");
        // 格式化后重新挂载
        res = f_mount(&fs, PATH, 1);
        if (res != FR_OK) {
            XPrintf("格式化后挂载失败：");
            print_fatfs_error(res);
            return -1;
        }
    }
    else {
        // 其他挂载错误（如设备未准备好、权限问题等）
        XPrintf("挂载检查失败：");
        print_fatfs_error(res);
        return -1;
    }
    // 3. 挂载成功后，获取并显示磁盘大小信息（修正版）
    XPrintf("\n3. 磁盘信息：\n");
    // 3.1 从底层磁盘IO获取总扇区数（可靠来源）
    DWORD total_sectors;
    res = disk_ioctl(2, GET_SECTOR_COUNT, &total_sectors);
    if (res != RES_OK) {
        XPrintf("获取总扇区数失败！\n");
        f_mount(NULL, PATH, 1);
        return -1;
    }
    WORD bytes_per_sec = SECTOR_SIZE;  // 固定512字节
    DWORD total_bytes = total_sectors * bytes_per_sec;  // 总大小（字节）

    // 3.2 获取空闲簇数和文件系统信息
    DWORD free_clusters;
    FATFS* fs_ptr;
    res = f_getfree(PATH, &free_clusters, &fs_ptr);
    if (res != FR_OK) {
        XPrintf("获取空闲簇数失败：");
        print_fatfs_error(res);
        f_mount(NULL, PATH, 1);
        return -1;
    }
    WORD sec_per_cluster = fs_ptr->csize;  // 每簇扇区数（通用字段）

    // 3.3 计算空闲大小（空闲簇数 × 每簇扇区数 × 扇区大小）
    DWORD free_sectors = free_clusters * sec_per_cluster;
    DWORD free_bytes = free_sectors * bytes_per_sec;

    // 3.4 已使用大小 = 总大小 - 空闲大小（确保非负）
    DWORD used_bytes = (total_bytes >= free_bytes) ? (total_bytes - free_bytes) : 0;

    // 转换为MB并打印（保留2位小数）
    XPrintf("  总大小：%.2f MB\n", calc_mb(total_bytes));
    XPrintf("  已使用：%.2f MB\n", calc_mb(used_bytes));
    XPrintf("  空闲大小：%.2f MB\n", calc_mb(free_bytes));

    // 3. 创建并写入UTF-8文件名的文件
    XPrintf("\n3. 创建文件：%s...\n", TEST_UTF8_FILE);
    res = f_open(&file, TEST_UTF8_FILE, FA_WRITE | FA_CREATE_ALWAYS);
    if (res != FR_OK) {
        XPrintf("创建文件失败：");
        print_fatfs_error(res);
        f_mount(NULL, PATH, 1);  // 卸载
        return -1;
    }

    // 写入测试内容
    res = f_write(&file, TEST_CONTENT, strlen(TEST_CONTENT), &bw);
    if (res != FR_OK || bw != strlen(TEST_CONTENT)) {
        XPrintf("写入失败：");
        print_fatfs_error(res);
        f_close(&file);
        f_mount(NULL, PATH, 1);
        return -1;
    }
    XPrintf("写入成功，写入字节数：%d\n", bw);
    f_close(&file);

    // 4. 读取文件内容验证
    XPrintf("\n4. 读取文件内容...\n");
    res = f_open(&file, TEST_UTF8_FILE, FA_READ);
    if (res != FR_OK) {
        XPrintf("打开文件失败：");
        print_fatfs_error(res);
        f_mount(NULL, PATH, 1);
        return -1;
    }

    res = f_read(&file, read_buff, sizeof(read_buff) - 1, &br);
    if (res != FR_OK) {
        XPrintf("读取失败：");
        print_fatfs_error(res);
        f_close(&file);
        f_mount(NULL, PATH, 1);
        return -1;
    }
    read_buff[br] = '\0';  // 确保字符串结束
    XPrintf("读取成功，内容：%s\n", read_buff);
    f_close(&file);

    // 5. 验证内容是否一致
    if (strcmp(read_buff, TEST_CONTENT) != 0) {
        XPrintf("内容不一致！测试失败\n");
        f_mount(NULL, PATH, 1);
        return -1;
    }

    // 6. 删除测试文件
    XPrintf("\n5. 删除测试文件...\n");
    res = f_unlink(TEST_UTF8_FILE);
    if (res != FR_OK) {
        XPrintf("删除失败：");
        print_fatfs_error(res);
        f_mount(NULL, PATH, 1);
        return -1;
    }
    XPrintf("文件删除成功\n");

    //// 7. 卸载文件系统前先同步缓存
    //XPrintf("\n6. 同步文件系统缓存...\n");
    //res = f_sync(&file);  
    //if (res != FR_OK) {
    //    XPrintf("缓存同步失败：");
    //    print_fatfs_error(res);
    //    return -1;
    //}

    // 7. 卸载文件系统
    XPrintf("\n6. 卸载文件系统...\n");
    res = f_mount(&fs, PATH, 0);
    if (res != FR_OK) {
        XPrintf("卸载失败：");
        print_fatfs_error(res);
        return -1;
    }
    XPrintf("所有测试通过！\n");

}