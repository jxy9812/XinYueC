/**
 * @file XFileSystem_config.h
 * @brief XFile模块文件系统后端配置文件
 *
 * 通过此配置文件可以选择文件系统的底层实现：
 *   1. 平台API模式 - 使用操作系统原生API（Windows/Posix）
 *   2. FatFS模式 - 使用FatFS文件系统库（嵌入式设备）
 *
 * 优先级（从高到低）：
 *   XFILE_USE_PLATFORM_API > XFILE_USE_FATFS
 */

#ifndef XFILESYSTEM_CONFIG_H
#define XFILESYSTEM_CONFIG_H

#ifdef __cplusplus
extern "C" {
#endif

/* ========================================================================== */
/*                        模式选择                                            */
/* ========================================================================== */

/**
 * @brief 文件系统后端模式选择
 *
 * 两种模式只能启用一种，优先级：PLATFORM_API > FATFS
 *
 * XFILE_USE_PLATFORM_API - 平台API模式
 *                          优点：完整支持所有30个API，性能最优
 *                          缺点：依赖操作系统
 *                          适用：Windows/Linux/macOS桌面应用
 *                          API覆盖率：100%（31/31）
 *
 * XFILE_USE_FATFS         - FatFS模式
 *                          优点：跨平台，无操作系统依赖，适合嵌入式
 *                          缺点：部分API不支持（特殊路径、符号链接等）
 *                          适用：STM32等嵌入式设备
 *                          API覆盖率：75%（必需API 15/20）
 */

/* 取消注释以启用对应模式 */

/* 未通过编译选项显式选择时，桌面平台默认使用平台API。嵌入式工程可通过
 * -DXFILE_USE_FATFS 选择 FatFS，不能在这里无条件定义平台后端。 */
#if !defined(XFILE_USE_PLATFORM_API) && !defined(XFILE_USE_FATFS)
#define XFILE_USE_PLATFORM_API     /* 平台API模式 */
#endif

/* ========================================================================== */
/*                        FatFS模式配置                                        */
/* ========================================================================== */

#if defined(XFILE_USE_FATFS)

/**
 * @brief FatFS卷数量配置
 *
 * 定义支持的逻辑驱动器数量（对应FatFS的FF_VOLUMES）
 */
#ifndef XFILE_FATFS_VOLUMES
#define XFILE_FATFS_VOLUMES       4
#endif

/**
 * @brief FatFS字符串卷标支持
 *
 * 启用后可使用字符串代替数字作为驱动器标识
 * 例如："sd:/file.txt", "C:/file.txt"
 * 
 * 对应FatFS配置：FF_STR_VOLUME_ID = 1
 * 
 * 优点：
 * - 兼容Windows风格盘符（C:, D:等）
 * - 更直观的设备标识
 */
#ifndef XFILE_FATFS_STR_VOLUME_ID
#define XFILE_FATFS_STR_VOLUME_ID    1
#endif

/**
 * @brief 卷标字符串定义
 *
 * 格式：驱动器0名称, 驱动器1名称, ...
 * 数量需与 XFILE_FATFS_VOLUMES 一致
 * 
 * Windows兼容配置：    "C", "D", "E", "F"
 * 嵌入式配置：         "sd", "usb", "flash", "nand"
 */
#ifndef XFILE_FATFS_VOLUME_STRS
#define XFILE_FATFS_VOLUME_STRS     "C", "D", "E", "F"
#endif

/**
 * @brief 默认驱动器卷标
 *
 * 用于未指定驱动器时的默认路径前缀
 */
#ifndef XFILE_FATFS_DEFAULT_DRIVE
#define XFILE_FATFS_DEFAULT_DRIVE   "C"
#endif

/**
 * @brief FatFS长文件名支持
 *
 * XFILE_FATFS_LFN_NONE    - 不支持长文件名
 * XFILE_FATFS_LFN_STATIC  - 静态缓冲区（线程不安全）
 * XFILE_FATFS_LFN_DYNAMIC - 动态分配（需要ff_memalloc/ff_memfree）
 */
#ifndef XFILE_FATFS_LFN
#define XFILE_FATFS_LFN           3  /* 默认使用堆动态分配 */
#endif

/**
 * @brief FatFS路径前缀
 *
 * 定义FatFS路径前缀，用于适配层路径转换
 * 使用字符串卷标时格式为："卷标:"
 * 
 * 示例：
 * - Windows兼容：    "C:"
 * - 嵌入式风格：     "sd:"
 */
#ifndef XFILE_FATFS_PATH_PREFIX
#define XFILE_FATFS_PATH_PREFIX   "C:"
#endif

/**
 * @brief FatFS临时目录路径
 *
 * 由于FatFS无系统临时目录概念，需预定义路径
 */
#ifndef XFILE_FATFS_TEMP_PATH
#define XFILE_FATFS_TEMP_PATH     "C:/tmp"
#endif

/**
 * @brief FatFS根目录路径
 *
 * 定义根目录返回值
 */
#ifndef XFILE_FATFS_ROOT_PATH
#define XFILE_FATFS_ROOT_PATH     "C:/"
#endif

/**
 * @brief FatFS主目录路径
 *
 * 定义主目录返回值（嵌入式场景通常无用户主目录概念）
 */
#ifndef XFILE_FATFS_HOME_PATH
#define XFILE_FATFS_HOME_PATH     "C:/home"
#endif

/**
 * @brief FatFS最大文件描述符数量
 *
 * 用于适配层文件句柄映射表大小
 */
#ifndef XFILE_FATFS_MAX_FILES
#define XFILE_FATFS_MAX_FILES     8
#endif

/**
 * @brief FatFS最大目录迭代器数量
 */
#ifndef XFILE_FATFS_MAX_DIRS
#define XFILE_FATFS_MAX_DIRS      4
#endif

/**
 * @brief FatFS字符编码配置
 *
 * 统一使用UTF-8编码，确保跨平台兼容性
 * FatFS配置：FF_USE_LFN=1, FF_LFN_UNICODE=2 (UTF-8)
 */
#define XFILE_FATFS_USE_UTF8      1  /* 启用UTF-8编码 */

/**
 * @brief FatFS代码页配置
 *
 * 用于短文件名（SFN）编码，长文件名使用UTF-8
 * 常用值：936(GBK), 437(US), 850(Latin1), 932(Japanese)
 */
#ifndef XFILE_FATFS_CODE_PAGE
#define XFILE_FATFS_CODE_PAGE     437  /* US ASCII，配合UTF-8使用 */
#endif

/**
 * @brief FatFS是否启用exFAT支持
 */
#ifndef XFILE_FATFS_USE_EXFAT
#define XFILE_FATFS_USE_EXFAT     0
#endif

/**
 * @brief FatFS是否只读模式
 *
 * 启用后禁止写入操作，可减少代码体积
 */
#ifndef XFILE_FATFS_READONLY
#define XFILE_FATFS_READONLY      0
#endif

/**
 * @brief FatFS是否启用格式化功能
 */
#ifndef XFILE_FATFS_USE_MKFS
#define XFILE_FATFS_USE_MKFS      1
#endif

/**
 * @brief FatFS 磁盘I/O模式
 *
 * 0 = 文件镜像模式（Windows调试用）
 * 1 = 物理磁盘模式
 */
#ifndef XFILE_FATFS_DISKIO_MODE
#define XFILE_FATFS_DISKIO_MODE       0
#endif

/**
 * @brief 文件镜像模式下的驱动器配置
 */
#if XFILE_FATFS_DISKIO_MODE == 0

/* 镜像文件路径前缀 */
#ifndef XFILE_FATFS_DISK_IMAGE_PATH
#define XFILE_FATFS_DISK_IMAGE_PATH   "fatfs_disk"
#endif

/* 文件镜像模式下可用驱动器数量 */
#ifndef XFILE_FATFS_FILEMODE_DRIVE_COUNT
#define XFILE_FATFS_FILEMODE_DRIVE_COUNT  2
#endif

/* 文件镜像模式磁盘镜像大小（字节） */
#ifndef XFILE_FATFS_DISK_IMAGE_SIZE
#define XFILE_FATFS_DISK_IMAGE_SIZE       (64LL * 1024 * 1024)  /* 32 MB */
#endif

/* 自动格式化时使用的文件系统类型 */
#ifndef XFILE_FATFS_DEFAULT_FORMAT_TYPE
#define XFILE_FATFS_DEFAULT_FORMAT_TYPE   FM_ANY   /* FM_ANY/FM_FAT32/FM_EXFAT/FM_FAT */
#endif

/* 文件镜像模式下驱动器前缀列表 */
#ifndef XFILE_FATFS_FILEMODE_DRIVE_STRS
#define XFILE_FATFS_FILEMODE_DRIVE_STRS   "C:", "D:"
#endif

/* 文件镜像模式下主目录 */
#ifndef XFILE_FATFS_HOME_PATH
#define XFILE_FATFS_HOME_PATH     "C:/home"
#endif

/* 文件镜像模式下根目录 */
#ifndef XFILE_FATFS_ROOT_PATH
#define XFILE_FATFS_ROOT_PATH     "C:/root"
#endif

/* 文件镜像模式下临时目录 */
#ifndef XFILE_FATFS_TEMP_PATH
#define XFILE_FATFS_TEMP_PATH     "C:/tmp"
#endif

#endif /* XFILE_FATFS_DISKIO_MODE == 0 */

#endif /* XFILE_USE_FATFS */

/* ========================================================================== */
/*                        平台API模式配置                                      */
/* ========================================================================== */

#if defined(XFILE_USE_PLATFORM_API)

/**
 * @brief 平台API模式配置
 *
 * Windows: 使用 Win32 API (CreateFile, ReadFile, WriteFile等)
 * Linux:   使用 POSIX API (open, read, write, opendir等)
 * macOS:   使用 POSIX API
 */

/**
 * @brief 是否启用符号链接支持
 *
 * Windows: 需要管理员权限或开发者模式
 * Linux/macOS: 默认支持
 */
#ifndef XFILE_PLATFORM_SUPPORT_SYMLINK
#define XFILE_PLATFORM_SUPPORT_SYMLINK  1
#endif

/**
 * @brief 是否启用内存映射支持
 *
 * Windows: 使用 CreateFileMapping/MapViewOfFile
 * Linux:   使用 mmap/munmap
 */
#ifndef XFILE_PLATFORM_SUPPORT_MMAP
#define XFILE_PLATFORM_SUPPORT_MMAP     1
#endif

/**
 * @brief 是否启用权限操作支持
 *
 * Windows: 使用 ACL 或 SetFileAttributes（有限支持）
 * Linux:   使用 chmod（完整支持）
 */
#ifndef XFILE_PLATFORM_SUPPORT_PERMISSIONS
#define XFILE_PLATFORM_SUPPORT_PERMISSIONS  1
#endif

/**
 * @brief 默认文件缓冲区大小
 */
#ifndef XFILE_PLATFORM_BUFFER_SIZE
#define XFILE_PLATFORM_BUFFER_SIZE      4096
#endif

#endif /* XFILE_USE_PLATFORM_API */

/* ========================================================================== */
/*                        自动模式检测                                         */
/* ========================================================================== */

/**
 * @brief 自动选择默认模式
 *
 * 如果用户未指定任何模式，根据平台自动选择：
 *   - Windows/Linux/macOS -> XFILE_USE_PLATFORM_API
 *   - 其他平台            -> XFILE_USE_FATFS
 */
#if !defined(XFILE_USE_PLATFORM_API) && !defined(XFILE_USE_FATFS)

#if defined(_WIN32) || defined(__linux__) || defined(__APPLE__)
#define XFILE_USE_PLATFORM_API
#else
#define XFILE_USE_FATFS
#endif

#endif

/* ========================================================================== */
/*                        模式验证                                             */
/* ========================================================================== */

/* 检查是否同时启用了多个模式 */
#if defined(XFILE_USE_PLATFORM_API) && defined(XFILE_USE_FATFS)
#error "XFileSystem: 不能同时启用多个文件系统后端，请只选择一种模式"
#endif

/* 检查是否未启用任何模式 */
#if !defined(XFILE_USE_PLATFORM_API) && !defined(XFILE_USE_FATFS)
#error "XFileSystem: 必须启用至少一个文件系统后端模式"
#endif

/* ========================================================================== */
/*                        API可用性宏定义                                       */
/* ========================================================================== */

/**
 * @brief API功能可用性宏
 *
 * 用于编译时判断特定API是否可用，便于条件编译
 */

/* 核心文件操作 - 两种模式均支持 */
#define XFILE_API_OPEN              1
#define XFILE_API_CLOSE             1
#define XFILE_API_POS               1
#define XFILE_API_SEEK              1
#define XFILE_API_READ              1
#define XFILE_API_WRITE             1
#define XFILE_API_FLUSH             1
#define XFILE_API_RESIZE            1  /* FatFS: 通过truncate+write实现 */

/* 文件属性操作 - 两种模式均支持 */
#define XFILE_API_STAT              1
#define XFILE_API_FSTAT             1

/* 文件系统操作 - 两种模式均支持 */
/* XFILE_API_EXISTS: merged into XFILE_API_STAT (inline wrapper) */
#define XFILE_API_REMOVE            1
#define XFILE_API_RENAME            1
#define XFILE_API_COPY              1

/* 目录操作 - 两种模式均支持 */
#define XFILE_API_MKDIR             1
#define XFILE_API_RMDIR             1
#define XFILE_API_OPENDIR           1
#define XFILE_API_READDIR           1
#define XFILE_API_CLOSEDIR          1

/* 路径操作 - FatFS部分支持 */
#define XFILE_API_RESOLVE_PATH      1

/* 特殊路径 - 仅平台API模式完全支持 */
#if defined(XFILE_USE_PLATFORM_API)
#define XFILE_API_SPECIAL_PATH     1
#define XFILE_API_SET_CURRENT_PATH 1
/* merged into XFILE_API_SPECIAL_PATH */
/* merged into XFILE_API_SPECIAL_PATH */
/* merged into XFILE_API_SPECIAL_PATH */
#elif defined(XFILE_USE_FATFS)
#define XFILE_API_SPECIAL_PATH     1  /* FatFS: f_getcwd/f_chdir */
#define XFILE_API_SET_CURRENT_PATH 1  /* FatFS: f_chdir/f_chdrive */
/* merged into XFILE_API_SPECIAL_PATH */
/* merged into XFILE_API_SPECIAL_PATH */
/* merged into XFILE_API_SPECIAL_PATH */
#endif

/* 符号链接操作 - 仅平台API模式支持 */
#if defined(XFILE_USE_PLATFORM_API) && XFILE_PLATFORM_SUPPORT_SYMLINK
#define XFILE_API_LINK              1
#define XFILE_API_READ_LINK         1
#else
#define XFILE_API_LINK              0
#define XFILE_API_READ_LINK         0
#endif

/* 权限操作 - 仅平台API模式完整支持 */
#if defined(XFILE_USE_PLATFORM_API) && XFILE_PLATFORM_SUPPORT_PERMISSIONS
#define XFILE_API_SET_PERMISSIONS   1
#else
#define XFILE_API_SET_PERMISSIONS   0
#endif

/* 内存映射 - 仅平台API模式支持 */
#if defined(XFILE_USE_PLATFORM_API) && XFILE_PLATFORM_SUPPORT_MMAP
#define XFILE_API_MAP               1
#define XFILE_API_UNMAP             1
#else
#define XFILE_API_MAP               0
#define XFILE_API_UNMAP             0
#endif

/* 递归删除目录 - 可在应用层实现 */
/* XFILE_API_RMDIR_RECURSIVE: merged into XFILE_API_RMDIR (recursive parameter) */

/* 文件时间修改 - FatFS有限支持（f_utime） */
#if defined(XFILE_USE_PLATFORM_API)
#define XFILE_API_SET_FILE_TIME     1  /* fd-based, path version via open→set→close */
#elif defined(XFILE_USE_FATFS)
#define XFILE_API_SET_FILE_TIME     1  /* fd-based, path version via open→set→close */  /* FatFS: f_utime有限支持 */
#endif

/* 驱动器列表 - 仅平台API模式完整支持 */
#if defined(XFILE_USE_PLATFORM_API)
#define XFILE_API_DRIVES            1
#else
#define XFILE_API_DRIVES            0
#endif

/* 存储设备信息 - 为 XStorageInfo 提供 */
#define XFILE_API_BYTES_TOTAL       1
#define XFILE_API_BYTES_FREE        1
#define XFILE_API_BYTES_AVAILABLE   1
#define XFILE_API_BLOCK_SIZE        1
#define XFILE_API_DEVICE            1
#define XFILE_API_FILE_SYSTEM_TYPE  1
#define XFILE_API_VOLUME_NAME       1
#define XFILE_API_SUBVOLUME         1  /* 仅 Linux 支持 Btrfs */
#define XFILE_API_IS_READY          1
#define XFILE_API_IS_READ_ONLY      1
#define XFILE_API_GET_STORAGE_INFO  1

/* ========================================================================== */
/*                        兼容性宏定义                                         */
/* ========================================================================== */

/**
 * @brief 废弃API编译时警告
 *
 * 当调用不支持的API时，通过编译警告提示用户
 */
#if defined(XFILE_USE_FATFS)

/* 对于FatFS不支持的API，可以定义替代行为或返回失败的实现 */

#ifndef XFILE_UNSUPPORTED_API_HANDLER
#define XFILE_UNSUPPORTED_API_HANDLER(api_name) \
    do { \
        /* 可在此添加日志记录或断言 */ \
        (void)0; \
    } while(0)
#endif

#endif

#ifdef __cplusplus
}
#endif

#endif /* XFILESYSTEM_CONFIG_H */
