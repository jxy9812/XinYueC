#if defined(__linux__) || defined(__APPLE__) || defined(__BSD__)
#include "diskio.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <linux/fs.h>  // 仅Linux需要，用于BLKSSZGET等宏
#include <errno.h>

/* 配置选项 */
#define USE_PHYSICAL_DISK 0  // 0:使用镜像文件 1:使用物理磁盘（如/dev/sdX）
#define SECTOR_SIZE       512 // 扇区大小（字节），物理磁盘会自动检测
#define MAX_DEVICES       FF_VOLUMES   // 支持的最大设备数量

/* 设备路径（根据模式选择） */
#if USE_PHYSICAL_DISK
// 物理磁盘路径（需根据实际系统修改，如/dev/sdb、/dev/mmcblk0）
static const char* dev_paths[MAX_DEVICES] = {
    "/dev/sdb",    // 设备0
    "/dev/sdc",    // 设备1
    "/dev/mmcblk0" // 设备2
};
#else
// 镜像文件路径（当前目录下）
static const char* dev_paths[MAX_DEVICES] = {
    "flash.img",   // 设备0
    "sdcard.img",  // 设备1
    "usb.img"      // 设备2
};
#endif

/* 设备句柄（文件描述符） */
static int dev_fds[MAX_DEVICES] = { -1, -1, -1 };

/*-----------------------------------------------------------------------*/
/* 初始化磁盘设备                                                        */
/*-----------------------------------------------------------------------*/
DSTATUS disk_initialize(BYTE pdrv) {
    if (pdrv >= MAX_DEVICES) return STA_NOINIT;

    int fd;
    struct stat st;
    const char* path = dev_paths[pdrv];

#if USE_PHYSICAL_DISK
    // 打开物理磁盘（需要读写权限，可能需要root）
    fd = open(path, O_RDWR);
#else
    // 打开镜像文件，不存在则创建并初始化大小（16MB）
    fd = open(path, O_RDWR);
    if (fd < 0) {
        // 镜像文件不存在，创建新文件
        fd = open(path, O_RDWR | O_CREAT | O_EXCL, 0644);
        if (fd < 0) {
            fprintf(stderr, "创建镜像文件失败: %s, 错误: %s\n", path, strerror(errno));
            return STA_NOINIT;
        }

        // 初始化镜像文件大小为16MB（32768个扇区）
        off_t size = (off_t)32768 * SECTOR_SIZE;
        if (ftruncate(fd, size) < 0) {
            fprintf(stderr, "设置镜像文件大小失败: %s\n", strerror(errno));
            close(fd);
            return STA_NOINIT;
        }
    }
#endif

    // 验证设备是否可访问
    if (fstat(fd, &st) < 0) {
        fprintf(stderr, "获取设备信息失败: %s\n", strerror(errno));
        close(fd);
        return STA_NOINIT;
    }

#if USE_PHYSICAL_DISK
    // 物理磁盘：检测实际扇区大小（覆盖默认SECTOR_SIZE）
    int actual_sector_size;
    if (ioctl(fd, BLKSSZGET, &actual_sector_size) == 0) {
        // 此处可根据需要调整全局SECTOR_SIZE，或在disk_ioctl中返回实际值
        fprintf(stdout, "物理磁盘扇区大小: %d 字节\n", actual_sector_size);
    }
#endif

    dev_fds[pdrv] = fd;
    return 0; // 初始化成功
}

/*-----------------------------------------------------------------------*/
/* 获取磁盘状态                                                          */
/*-----------------------------------------------------------------------*/
DSTATUS disk_status(BYTE pdrv) {
    if (pdrv >= MAX_DEVICES) return STA_NOINIT;
    return (dev_fds[pdrv] >= 0) ? 0 : STA_NOINIT;
}

/*-----------------------------------------------------------------------*/
/* 读取扇区（多个连续扇区）                                              */
/*-----------------------------------------------------------------------*/
DRESULT disk_read(
    BYTE pdrv,      // 设备编号
    BYTE* buff,     // 数据缓冲区
    LBA_t sector,   // 起始扇区号（LBA）
    UINT count      // 扇区数量
) {
    if (pdrv >= MAX_DEVICES || count == 0 || buff == NULL)
        return RES_PARERR;
    if (dev_fds[pdrv] < 0)
        return RES_NOTRDY;

    int fd = dev_fds[pdrv];
    off_t offset = (off_t)sector * SECTOR_SIZE;

    // 定位到目标扇区
    if (lseek(fd, offset, SEEK_SET) != offset) {
        fprintf(stderr, "扇区定位失败: %s\n", strerror(errno));
        return RES_ERROR;
    }

    // 读取数据（按扇区大小计算总字节数）
    ssize_t total_read = 0;
    size_t bytes_needed = count * SECTOR_SIZE;
    while (total_read < bytes_needed) {
        ssize_t n = read(fd, buff + total_read, bytes_needed - total_read);
        if (n < 0) {
            fprintf(stderr, "扇区读取失败: %s\n", strerror(errno));
            return RES_ERROR;
        }
        else if (n == 0) {
            // 到达文件末尾（镜像文件可能提前结束）
            fprintf(stderr, "读取到文件末尾，数据不完整\n");
            return RES_ERROR;
        }
        total_read += n;
    }

    return RES_OK;
}

/*-----------------------------------------------------------------------*/
/* 写入扇区（多个连续扇区）                                              */
/*-----------------------------------------------------------------------*/
DRESULT disk_write(
    BYTE pdrv,      // 设备编号
    const BYTE* buff,// 数据缓冲区
    LBA_t sector,   // 起始扇区号（LBA）
    UINT count      // 扇区数量
) {
    if (pdrv >= MAX_DEVICES || count == 0 || buff == NULL)
        return RES_PARERR;
    if (dev_fds[pdrv] < 0)
        return RES_NOTRDY;

    int fd = dev_fds[pdrv];
    off_t offset = (off_t)sector * SECTOR_SIZE;

    // 定位到目标扇区
    if (lseek(fd, offset, SEEK_SET) != offset) {
        fprintf(stderr, "扇区定位失败: %s\n", strerror(errno));
        return RES_ERROR;
    }

    // 写入数据（按扇区大小计算总字节数）
    ssize_t total_written = 0;
    size_t bytes_needed = count * SECTOR_SIZE;
    while (total_written < bytes_needed) {
        ssize_t n = write(fd, buff + total_written, bytes_needed - total_written);
        if (n < 0) {
            fprintf(stderr, "扇区写入失败: %s\n", strerror(errno));
            return RES_ERROR;
        }
        total_written += n;
    }

    // 同步缓存到磁盘（确保数据写入物理介质）
    fsync(fd);

    return RES_OK;
}

/*-----------------------------------------------------------------------*/
/* 磁盘控制命令                                                          */
/*-----------------------------------------------------------------------*/
DRESULT disk_ioctl(
    BYTE pdrv,      // 设备编号
    BYTE cmd,       // 控制命令
    void* buff      // 数据缓冲区
) {
    if (pdrv >= MAX_DEVICES)
        return RES_PARERR;
    if (dev_fds[pdrv] < 0)
        return RES_NOTRDY;

    int fd = dev_fds[pdrv];
    switch (cmd) {
    case CTRL_SYNC:
        // 同步缓存（确保数据写入磁盘）
        fsync(fd);
        return RES_OK;

    case GET_SECTOR_SIZE:
        // 返回扇区大小（字节）
#if USE_PHYSICAL_DISK
            // 物理磁盘：获取实际扇区大小
        int sector_size;
        if (ioctl(fd, BLKSSZGET, &sector_size) == 0) {
            *(WORD*)buff = sector_size;
        }
        else {
            *(WORD*)buff = SECTOR_SIZE; // 失败时使用默认值
        }
#else
            // 镜像文件：使用预设扇区大小
        * (WORD*)buff = SECTOR_SIZE;
#endif
        return RES_OK;

    case GET_SECTOR_COUNT:
        // 返回总扇区数
        struct stat st;
        if (fstat(fd, &st) < 0)
            return RES_ERROR;
        // 总扇区数 = 设备总大小 / 扇区大小
        *(DWORD*)buff = (DWORD)(st.st_size / *(WORD*)buff); // 依赖GET_SECTOR_SIZE的结果
        return RES_OK;

    case GET_BLOCK_SIZE:
        // 返回擦除块大小（扇区数，这里简单返回8）
        *(DWORD*)buff = 8;
        return RES_OK;

    default:
        return RES_PARERR;
    }
}

/*-----------------------------------------------------------------------*/
/* 关闭磁盘设备（释放资源）                                              */
/*-----------------------------------------------------------------------*/
void disk_deinitialize(BYTE pdrv) {
    if (pdrv < MAX_DEVICES && dev_fds[pdrv] >= 0) {
        close(dev_fds[pdrv]);
        dev_fds[pdrv] = -1;
    }
}
#endif