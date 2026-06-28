/*-----------------------------------------------------------------------*/
/* Low level disk I/O module for Windows (C)ChaN, 2019 + 适配修改         */
/*-----------------------------------------------------------------------*/
#ifdef _WIN32
#include "ff.h"			/* FatFs头文件 */
#include "diskio.h"		/* 磁盘操作函数声明 */
#define _NO_TCHAR_DEFINES  // 阻止windows.h定义TCHAR
#include <windows.h>	/* Windows API头文件 */
#include <stdio.h>		/* 标准输入输出 */
/* 磁盘模式切换宏 */
#define USE_PHYSICAL_DISK 1  // 0:模拟文件 1:物理磁盘
#define MAX_DEVICES			FF_VOLUMES
/* 定义物理驱动器编号对应的模拟文件 */
#define DEV_EX_FLASH	0	/* 模拟外部闪存 */
#define DEV_SD			1	/* 模拟SD卡 */
#define DEV_USB			2	/* 模拟U盘 */

/* 模拟磁盘的扇区大小 (固定为512字节，与大多数存储设备兼容) */
#define SECTOR_SIZE		512

/* 设备对应的模拟文件路径 */
static const char* dev_paths[MAX_DEVICES] = {
	"flash.img",	/* DEV_EX_FLASH 对应的镜像文件 */
	"sdcard.img",	/* DEV_SD 对应的镜像文件 */
	"usb.img"		/* DEV_USB 对应的镜像文件 */
};

/* 设备句柄数组 (保存打开的文件句柄) */
static HANDLE dev_handles[MAX_DEVICES] = { INVALID_HANDLE_VALUE, INVALID_HANDLE_VALUE, INVALID_HANDLE_VALUE };

/*-----------------------------------------------------------------------*/
/* 获取磁盘状态                                                          */
/*-----------------------------------------------------------------------*/
DSTATUS disk_status(BYTE pdrv)
{
	/* 检查设备编号是否有效 */
	if (pdrv >= MAX_DEVICES) return STA_NOINIT;

	/* 检查设备是否已打开 */
	if (dev_handles[pdrv] == INVALID_HANDLE_VALUE) {
		return STA_NOINIT;	/* 设备未初始化 */
	}

	return 0;	/* 设备正常 */
}

/*-----------------------------------------------------------------------*/
/* 初始化磁盘                                                            */
/*-----------------------------------------------------------------------*/
DSTATUS disk_initialize(BYTE pdrv)
{
	DWORD bytes_written;
	HANDLE hFile;
	char path[MAX_PATH];

	/* 检查设备编号是否有效 */
	if (pdrv >= MAX_DEVICES) return STA_NOINIT;
	if (dev_handles[pdrv] != INVALID_HANDLE_VALUE)return 0;
	/* 构造完整路径 (在当前目录下创建镜像文件) */
	GetCurrentDirectoryA(MAX_PATH, path);
	strcat(path, "\\");
	strcat(path,  dev_paths[pdrv]);

	/* 尝试打开现有文件 */
	hFile = CreateFileA(
		path,
		GENERIC_READ | GENERIC_WRITE,	/* 读写权限 */
		FILE_SHARE_READ | FILE_SHARE_WRITE,	/* 允许共享读写 */
		NULL,
		OPEN_EXISTING,	/* 先尝试打开现有文件 */
		FILE_ATTRIBUTE_NORMAL,
		NULL
	);

	/* 如果文件不存在，则创建新文件 (初始大小为16MB) */
	if (hFile == INVALID_HANDLE_VALUE) {
		hFile = CreateFileA(
			path,
			GENERIC_READ | GENERIC_WRITE,
			FILE_SHARE_READ | FILE_SHARE_WRITE,
			NULL,
			CREATE_NEW,	/* 创建新文件 */
			FILE_ATTRIBUTE_NORMAL,
			NULL
		);

		/* 创建成功后设置初始大小 (16MB = 32768个扇区) */
		if (hFile != INVALID_HANDLE_VALUE) {
			LARGE_INTEGER size;
			size.QuadPart = (LONGLONG)32768 * SECTOR_SIZE;
			SetFilePointerEx(hFile, size, NULL, FILE_BEGIN);
			SetEndOfFile(hFile);
		}
	}

	/* 保存文件句柄 */
	if (hFile != INVALID_HANDLE_VALUE) {
		dev_handles[pdrv] = hFile;
		return 0;	/* 初始化成功 */
	}

	return STA_NOINIT;	/* 初始化失败 */
}

/*-----------------------------------------------------------------------*/
/* 读取扇区                                                              */
/*-----------------------------------------------------------------------*/
DRESULT disk_read(
	BYTE pdrv,		/* 设备编号 */
	BYTE* buff,		/* 数据缓冲区 */
	LBA_t sector,	/* 起始扇区 */
	UINT count		/* 扇区数量 */
)
{
	DWORD bytes_read;
	LARGE_INTEGER offset;

	/* 参数检查 */
	if (pdrv >= MAX_DEVICES || !buff || count == 0) return RES_PARERR;
	if (dev_handles[pdrv] == INVALID_HANDLE_VALUE) return RES_NOTRDY;

	/* 计算扇区偏移量 (扇区号 * 扇区大小) */
	offset.QuadPart = (LONGLONG)sector * SECTOR_SIZE;
	if (!SetFilePointerEx(dev_handles[pdrv], offset, NULL, FILE_BEGIN)) {
		return RES_ERROR;	/* 定位失败 */
	}

	/* 读取数据 (count个扇区 = count * SECTOR_SIZE字节) */
	if (!ReadFile(dev_handles[pdrv], buff, count * SECTOR_SIZE, &bytes_read, NULL)) {
		return RES_ERROR;	/* 读取失败 */
	}

	if (bytes_read != count * SECTOR_SIZE) {
		return RES_ERROR;	/* 读取不完整 */
	}

	return RES_OK;	/* 读取成功 */
}

/*-----------------------------------------------------------------------*/
/* 写入扇区                                                              */
/*-----------------------------------------------------------------------*/
#if FF_FS_READONLY == 0
DRESULT disk_write(
	BYTE pdrv,			/* 设备编号 */
	const BYTE* buff,	/* 待写入数据 */
	LBA_t sector,		/* 起始扇区 */
	UINT count			/* 扇区数量 */
)
{
	DWORD bytes_written;
	LARGE_INTEGER offset;

	/* 参数检查 */
	if (pdrv >= MAX_DEVICES || !buff || count == 0) return RES_PARERR;
	if (dev_handles[pdrv] == INVALID_HANDLE_VALUE) return RES_NOTRDY;

	/* 计算扇区偏移量 */
	offset.QuadPart = (LONGLONG)sector * SECTOR_SIZE;
	if (!SetFilePointerEx(dev_handles[pdrv], offset, NULL, FILE_BEGIN)) {
		return RES_ERROR;	/* 定位失败 */
	}

	/* 写入数据 */
	if (!WriteFile(dev_handles[pdrv], buff, count * SECTOR_SIZE, &bytes_written, NULL)) {
		return RES_ERROR;	/* 写入失败 */
	}

	if (bytes_written != count * SECTOR_SIZE) {
		return RES_ERROR;	/* 写入不完整 */
	}

	return RES_OK;	/* 写入成功 */
}
#endif

/*-----------------------------------------------------------------------*/
/* 磁盘控制函数                                                          */
/*-----------------------------------------------------------------------*/
DRESULT disk_ioctl(
	BYTE pdrv,	/* 设备编号 */
	BYTE cmd,	/* 控制命令 */
	void* buff	/* 数据缓冲区 */
)
{
	LARGE_INTEGER file_size;
	DWORD* dptr = (DWORD*)buff;

	/* 参数检查 */
	if (pdrv >= MAX_DEVICES) return RES_PARERR;
	if (dev_handles[pdrv] == INVALID_HANDLE_VALUE) return RES_NOTRDY;

	switch (cmd) {
	case CTRL_SYNC:
		/* 同步缓存 (Windows会自动处理，这里只需返回成功) */
		return RES_OK;

	case GET_SECTOR_SIZE:
		/* 返回扇区大小 */
		*(WORD*)buff = SECTOR_SIZE;
		return RES_OK;

	case GET_BLOCK_SIZE:
		/* 返回擦除块大小 (模拟为8个扇区) */
		*(WORD*)buff = 8;
		return RES_OK;

	case GET_SECTOR_COUNT:
		/* 返回总扇区数 (文件大小 / 扇区大小) */
		if (GetFileSizeEx(dev_handles[pdrv], &file_size)) {
			*dptr = (DWORD)(file_size.QuadPart / SECTOR_SIZE);
			return RES_OK;
		}
		return RES_ERROR;

	default:
		return RES_PARERR;	/* 不支持的命令 */
	}
}


/*-----------------------------------------------------------------------*/
/* 获取当前时间 (用于文件时间戳)                                          */
/*-----------------------------------------------------------------------*/
DWORD get_fattime(void)
{
	SYSTEMTIME st;
	GetLocalTime(&st);	/* 获取本地时间 */

	/* 转换为FatFs要求的时间格式:
	 * bit31-25: 年 (0-127, 1980年为基准)
	 * bit24-21: 月 (1-12)
	 * bit20-16: 日 (1-31)
	 * bit15-11: 时 (0-23)
	 * bit10-5: 分 (0-59)
	 * bit4-0: 秒/2 (0-29)
	 */
	return ((st.wYear - 1980) << 25) |
		(st.wMonth << 21) |
		(st.wDay << 16) |
		(st.wHour << 11) |
		(st.wMinute << 5) |
		(st.wSecond / 2);
}
#endif