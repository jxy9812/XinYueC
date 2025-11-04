/*----------------------------------------------------------------------------/
/  FatFs - 通用 FAT 文件系统模块  R0.15a                              /
/-----------------------------------------------------------------------------/
/
/ 版权所有 (C) 2024，ChaN，保留所有权利。
/
/ FatFs 模块是一个开源软件。允许对 FatFs 的源代码和二进制形式进行重新分发和使用，
/ 无论是否进行修改，但需满足以下条件：

/ 1. 源代码的重新分发必须保留上述版权声明、本条件以及以下免责声明。
/
/ 本软件由版权持有者和贡献者按“原样”提供，与本软件相关的任何保证均被免除。
/ 版权所有者或贡献者不对因使用本软件而造成的任何损害承担责任。
/
/----------------------------------------------------------------------------*/

#ifndef FF_DEFINED
#define FF_DEFINED	5380	/* Revision ID */

#ifdef __cplusplus
extern "C" {
#endif

#if !defined(FFCONF_DEF)
#include "ffconf.h"		/* FatFs configuration options */
#endif
#if FF_DEFINED != FFCONF_DEF
#error Wrong configuration file (ffconf.h).
#endif


/* Integer types used for FatFs API */

#if defined(_WIN32)		/* Windows VC++ (for development only) */
#define FF_INTDEF 2
#define _NO_TCHAR_DEFINES  // 阻止 Windows 定义 TCHAR
#include <windows.h>
typedef unsigned __int64 QWORD;
#include <float.h>
#define isnan(v) _isnan(v)
#define isinf(v) (!_finite(v))

#elif (defined(__STDC_VERSION__) && __STDC_VERSION__ >= 199901L) || defined(__cplusplus)	/* C99 or later */
#define FF_INTDEF 2
#include <stdint.h>
// 定义 UINT 为无符号整数类型，int 必须是 16 位或 32 位
typedef unsigned int	UINT;	
// 定义 BYTE 为无符号字符类型，char 必须是 8 位
typedef unsigned char	BYTE;	
// 定义 WORD 为 16 位无符号整数类型
typedef uint16_t		WORD;	
// 定义 DWORD 为 32 位无符号整数类型
typedef uint32_t		DWORD;	
// 定义 QWORD 为 64 位无符号整数类型
typedef uint64_t		QWORD;	
// 定义 WCHAR 为 UTF - 16 编码单元
typedef WORD			WCHAR;	

#else  	// 当编译器不支持 C99 标准时的类型定义
#define FF_INTDEF 1
// 定义 UINT 为无符号整数类型，int 必须是 16 位或 32 位
typedef unsigned int	UINT;	
// 定义 BYTE 为无符号字符类型，char 必须是 8 位
typedef unsigned char	BYTE;	
// 定义 WORD 为无符号短整型，short 必须是 16 位
typedef unsigned short	WORD;	
// 定义 DWORD 为无符号长整型，long 必须是 32 位
typedef unsigned long	DWORD;	
// 定义 WCHAR 为 UTF - 16 编码单元
typedef WORD			WCHAR;	
#endif


/* 文件大小和逻辑块地址（LBA）变量的类型 */

#if FF_FS_EXFAT
#if FF_INTDEF != 2
#error exFAT feature wants C99 or later
#endif
typedef QWORD FSIZE_t;
#if FF_LBA64
typedef QWORD LBA_t;
#else
typedef DWORD LBA_t;
#endif
#else
#if FF_LBA64
#error exFAT needs to be enabled when enable 64-bit LBA
#endif
typedef DWORD FSIZE_t;
typedef DWORD LBA_t;
#endif



/* FatFs API 上路径名字符串的类型 (TCHAR) */
// 新增：仅在 FTCHAR 未被定义（包括 Windows 未定义）时才定义
#ifndef FTCHAR
// 如果启用长文件名功能（FF_USE_LFN 为真）且指定使用 UTF - 16 编码的 Unicode
#if FF_USE_LFN && FF_LFN_UNICODE == 1
    // 定义 FTCHAR 类型为 WCHAR（通常为 16 位无符号整数，用于存储 UTF - 16 编码单元）
    typedef WCHAR FTCHAR;
    // 定义 _T 宏，将字符串常量转换为宽字符字符串常量（在字符串前添加 L）
    #define _T(x) L ## x
    // 定义 _TEXT 宏，功能与 _T 宏相同
    #define _TEXT(x) L ## x
// 如果启用长文件名功能且指定使用 UTF - 8 编码的 Unicode
#elif FF_USE_LFN && FF_LFN_UNICODE == 2
    // 定义 FTCHAR 类型为 char，用于存储 UTF - 8 编码的字符
    typedef char FTCHAR;
    // 定义 _T 宏，将字符串常量转换为 UTF - 8 字符串常量（在字符串前添加 u8）
    #define _T(x) u8 ## x
    // 定义 _TEXT 宏，功能与 _T 宏相同
    #define _TEXT(x) u8 ## x
// 如果启用长文件名功能且指定使用 UTF - 32 编码的 Unicode
#elif FF_USE_LFN && FF_LFN_UNICODE == 3
    // 定义 FTCHAR 类型为 DWORD（通常为 32 位无符号整数），用于存储 UTF - 32 编码的字符
    typedef DWORD FTCHAR;
    // 定义 _T 宏，将字符串常量转换为 UTF - 32 字符串常量（在字符串前添加 U）
    #define _T(x) U ## x
    // 定义 _TEXT 宏，功能与 _T 宏相同
    #define _TEXT(x) U ## x
// 如果启用长文件名功能，但 FF_LFN_UNICODE 的设置值不在 0 到 3 范围内
#elif FF_USE_LFN && (FF_LFN_UNICODE < 0 || FF_LFN_UNICODE > 3)
    // 编译时输出错误信息，提示 FF_LFN_UNICODE 设置错误
    #error Wrong FF_LFN_UNICODE setting
// 如果未启用长文件名功能，使用 ANSI 或 OEM 编码的单字节字符集（SBCS）或双字节字符集（DBCS）
#else
    // 定义 FTCHAR 类型为 char，用于存储 ANSI 或 OEM 编码的字符
    typedef char FTCHAR;
    // 定义 _T 宏，直接返回原字符串常量
    #define _T(x) x
    // 定义 _TEXT 宏，功能与 _T 宏相同
    #define _TEXT(x) x
#endif

#define _TCHAR_DEFINED  // 标记 FTCHAR 已定义，避免 Windows 重复定义
#endif  // FTCHAR


/* 卷管理的定义 */

// 如果启用多分区配置（FF_MULTI_PARTITION 为真）
#if FF_MULTI_PARTITION
    // 定义 PARTITION 结构体，用于关联物理驱动器和分区
    typedef struct {
        BYTE pd;    /* 关联的物理驱动器编号 */
        BYTE pt;    /* 关联的分区编号（0: 自动检测，1 - 4: 强制指定分区） */
    } PARTITION;
    // 声明一个外部数组 VolToPart，用于卷到分区的映射表
    extern PARTITION VolToPart[];
#endif

// 如果启用字符串形式的卷 ID（FF_STR_VOLUME_ID 为真）
#if FF_STR_VOLUME_ID
    // 如果未定义 FF_VOLUME_STRS
    #ifndef FF_VOLUME_STRS
        // 声明一个外部常量字符指针数组 VolumeStr，用于用户自定义的卷 ID 表
        extern const char* VolumeStr[FF_VOLUMES];
    #endif
#endif


/* 文件系统对象结构体 (FATFS) */

typedef struct {
    // 文件系统类型（0 表示空白的文件系统对象）
    BYTE fs_type;        
    // 卷所在的物理驱动器编号
    BYTE pdrv;           
    // 逻辑驱动器编号（仅在 FF_FS_REENTRANT 启用时使用）
    BYTE ldrv;           
    // 文件分配表（FAT）的数量（1 或 2）
    BYTE n_fats;         
    // win[] 数组的状态（1 表示已修改，即“脏”状态）
    BYTE wflag;          
    // 分配信息控制标志（b7 位：禁用；b0 位：已修改，即“脏”状态）
    BYTE fsi_flag;       
    // 卷挂载 ID，用于标识挂载状态
    WORD id;             
    // 根目录项的数量（仅适用于 FAT12/16 文件系统）
    WORD n_rootdir;      
    // 簇的大小，以扇区为单位
    WORD csize;          

    // 如果最大扇区大小不等于最小扇区大小，则需要单独记录扇区大小
#if FF_MAX_SS != FF_MIN_SS
    // 扇区大小（可以是 512、1024、2048 或 4096 字节）
    WORD ssize;          
#endif

    // 如果启用长文件名支持
#if FF_USE_LFN
    // 长文件名工作缓冲区指针
    WCHAR* lfnbuf;       
#endif

    // 如果使用 exFAT 文件系统
#if FF_FS_EXFAT
    // exFAT 目录项块暂存缓冲区指针
    BYTE* dirbuf;        
#endif

    // 如果不是只读文件系统
#if !FF_FS_READONLY
    // 最后分配的簇号（如果大于等于 n_fatent 则表示未知）
    DWORD last_clst;     
    // 空闲簇的数量（如果大于等于 n_fatent - 2 则表示未知）
    DWORD free_clst;     
#endif

    // 如果启用相对路径支持
#if FF_FS_RPATH
    // 当前目录的起始簇号（0 表示根目录）
    DWORD cdir;          

    // 如果使用 exFAT 文件系统且启用相对路径支持
#if FF_FS_EXFAT
    // 包含当前目录的目录起始簇号（当 cdir 为 0 时无效）
    DWORD cdc_scl;       
    // 包含当前目录的目录大小（b31 - b8 位：大小；b7 - b0 位：链状态）
    DWORD cdc_size;      
    // 在包含当前目录的目录中的偏移量（当 cdir 为 0 时无效）
    DWORD cdc_ofs;       
#endif
#endif

    // 文件分配表项的数量（簇的数量 + 2）
    DWORD n_fatent;      
    // 每个文件分配表占用的扇区数量
    DWORD fsize;         
    // 卷的起始扇区逻辑块地址
    LBA_t volbase;       
    // 文件分配表的起始扇区逻辑块地址
    LBA_t fatbase;       
    // 根目录的起始扇区逻辑块地址（FAT12/16）或起始簇号（FAT32/exFAT）
    LBA_t dirbase;       
    // 数据区的起始扇区逻辑块地址
    LBA_t database;      

    // 如果使用 exFAT 文件系统
#if FF_FS_EXFAT
    // 分配位图的起始扇区逻辑块地址
    LBA_t bitbase;       
#endif

    // 当前出现在 win[] 数组中的扇区逻辑块地址
    LBA_t winsect;       
    // 用于目录、文件分配表（以及在小型配置下的文件数据）的磁盘访问窗口
    BYTE win[FF_MAX_SS]; 
} FATFS;


/* 对象 ID 与分配信息 (FFOBJID) */

typedef struct {
    // 指向该对象所在卷的文件系统对象的指针
    FATFS* fs;              
    // 所在卷的挂载 ID，用于标识挂载状态
    WORD id;                
    // 对象的属性，例如文件或目录的只读、隐藏等属性
    BYTE attr;              
    // 对象链的状态（b1 - b0 位：0 表示不连续；2 表示连续；3 表示在本次会话中碎片化；b2 位：子目录是否扩展）
    BYTE stat;              
    // 对象数据的起始簇号（0 表示无簇或根目录）
    DWORD sclust;           
    // 对象的大小（当 sclust 不为 0 时有效）
    FSIZE_t objsize;         

    // 如果使用 exFAT 文件系统
#if FF_FS_EXFAT
    // 第一个片段的大小减 1（当 stat 等于 3 时有效）
    DWORD n_cont;           
    // 最后一个片段的大小，需要写入文件分配表（非零值时有效）
    DWORD n_frag;           
    // 包含该对象的目录的起始簇号（当 sclust 不为 0 时有效）
    DWORD c_scl;            
    // 包含该对象的目录的大小（b31 - b8 位：目录大小；b7 - b0 位：链状态，当 c_scl 不为 0 时有效）
    DWORD c_size;           
    // 在包含该对象的目录中的偏移量（对于文件对象且 sclust 不为 0 时有效）
    DWORD c_ofs;            
#endif

    // 如果启用文件锁定功能
#if FF_FS_LOCK
    // 文件锁定 ID，从 1 开始（文件信号量表 Files[] 的索引）
    UINT lockid;            
#endif
} FFOBJID;



/* 文件对象结构体 (FIL) */

typedef struct {
    // 对象标识符（必须是结构体的第一个成员，用于检测无效的对象指针）
    FFOBJID obj;            
    // 文件状态标志，用于表示文件的各种状态，如是否打开、是否只读等
    BYTE flag;              
    // 中止标志（错误代码），当文件操作出现错误时，存储相应的错误码
    BYTE err;               
    // 文件读写指针，指示当前文件读写操作的位置，文件打开时初始化为 0
    FSIZE_t fptr;           
    // 当前文件指针所在的簇号，当文件指针为 0 时该值无效
    DWORD clust;            
    // 出现在 buf[] 中的扇区号，值为 0 表示无效
    LBA_t sect;             

    // 如果不是只读文件系统
#if !FF_FS_READONLY
    // 包含目录项的扇区号（在 exFAT 文件系统中不使用）
    LBA_t dir_sect;         
    // 指向 win[] 中目录项的指针（在 exFAT 文件系统中不使用）
    BYTE* dir_ptr;          
#endif

    // 如果启用快速查找功能
#if FF_USE_FASTSEEK
    // 指向簇链接映射表的指针，文件打开时为空，可由应用程序设置
    DWORD* cltbl;           
#endif

    // 如果不是使用微型配置的文件系统
#if !FF_FS_TINY
    // 文件私有数据读写窗口，用于存储从文件中读取或要写入文件的数据
    BYTE buf[FF_MAX_SS];    
#endif
} FIL;



/* 目录对象结构体 (DIR) */

typedef struct {
    // 对象标识符，用于标识该目录对象
    FFOBJID obj;            
    // 当前的读写偏移量，指示在目录中当前操作的位置
    DWORD dptr;             
    // 当前所在的簇号，用于定位目录数据在磁盘上的存储位置
    DWORD clust;            
    // 当前所在的扇区号，当值为 0 时表示读取操作已结束
    LBA_t sect;             
    // 指向 win[] 数组中当前目录项的指针，方便对目录项进行操作
    BYTE* dir;              
    // 短文件名（Short File Name，SFN），数组包含文件名主体（8 字节）、扩展名（3 字节）和状态（1 字节）
    BYTE fn[12];            

    // 如果启用长文件名支持
#if FF_USE_LFN
    // 当前正在处理的条目块的偏移量，值为 0xFFFFFFFF 表示无效
    DWORD blk_ofs;          
#endif

    // 如果启用文件查找功能
#if FF_USE_FIND
    // 指向名称匹配模式的指针，用于在目录中查找符合特定模式的文件或子目录
    const FTCHAR* pat;       
#endif
} DIR;



/* 文件信息结构体 (FILINFO) */

typedef struct {
    FSIZE_t fsize;  /* 文件大小 */
    WORD fdate;     /* 修改日期 */
    WORD ftime;     /* 修改时间 */
    BYTE fattrib;   /* 文件属性 */
#if FF_USE_LFN
    // 长文件名功能启用时，存储备用文件名
    FTCHAR altname[FF_SFN_BUF + 1]; 
    // 长文件名功能启用时，存储主文件名
    FTCHAR fname[FF_LFN_BUF + 1];    
#else
    // 长文件名功能未启用时，存储文件名
    FTCHAR fname[12 + 1];     
#endif
} FILINFO;



/* 格式化参数结构体 (MKFS_PARM) */

typedef struct {
    BYTE fmt;          /* 格式化选项（FM_FAT、FM_FAT32、FM_EXFAT 和 FM_SFD） */
    BYTE n_fat;        /* 文件分配表（FAT）的数量 */
    UINT align;        /* 数据区对齐方式（以扇区为单位） */
    UINT n_root;       /* 根目录项的数量 */
    DWORD au_size;     /* 簇大小（字节） */
} MKFS_PARM;



/* File function return code (FRESULT) */

// 定义一个枚举类型 FRESULT，用于表示文件系统操作的结果
typedef enum {
    FR_OK = 0,                /* (0) 函数执行成功 */
    FR_DISK_ERR,              /* (1) 底层磁盘 I/O 层发生硬件错误 */
    FR_INT_ERR,               /* (2) 断言失败 */
    FR_NOT_READY,             /* (3) 物理驱动器无法正常工作 */
    FR_NO_FILE,               /* (4) 未找到指定的文件 */
    FR_NO_PATH,               /* (5) 未找到指定的路径 */
    FR_INVALID_NAME,          /* (6) 路径名格式无效 */
    FR_DENIED,                /* (7) 由于访问受限或目录已满，访问被拒绝 */
    FR_EXIST,                 /* (8) 由于访问受限，访问被拒绝 */
    FR_INVALID_OBJECT,        /* (9) 文件/目录对象无效 */
    FR_WRITE_PROTECTED,       /* (10) 物理驱动器被写保护 */
    FR_INVALID_DRIVE,         /* (11) 逻辑驱动器号无效 */
    FR_NOT_ENABLED,           /* (12) 卷没有工作区 */
    FR_NO_FILESYSTEM,         /* (13) 未找到有效的 FAT 文件系统卷 */
    FR_MKFS_ABORTED,          /* (14) 由于某些问题，f_mkfs 函数中止执行 */
    FR_TIMEOUT,               /* (15) 在规定时间内无法控制卷 */
    FR_LOCKED,                /* (16) 根据文件共享策略，操作被拒绝 */
    FR_NOT_ENOUGH_CORE,       /* (17) 无法分配长文件名 (LFN) 工作缓冲区，或者给定的缓冲区大小不足 */
    FR_TOO_MANY_OPEN_FILES,   /* (18) 打开的文件数量超过了 FF_FS_LOCK 的限制 */
    FR_INVALID_PARAMETER      /* (19) 给定的参数无效 */
} FRESULT;




/*--------------------------------------------------------------*/
/* FatFs 模块应用程序接口                           */
/*--------------------------------------------------------------*/

/**
 * @brief 打开或创建一个文件
 * @param fp 指向文件对象的指针
 * @param path 要打开或创建的文件的路径
 * @param mode 打开模式
 * @return 操作结果，FRESULT类型
 */
FRESULT f_open (FIL* fp, const FTCHAR* path, BYTE mode);

/**
 * @brief 关闭一个已打开的文件对象
 * @param fp 指向要关闭的文件对象的指针
 * @return 操作结果，FRESULT类型
 */
FRESULT f_close (FIL* fp);

/**
 * @brief 从文件中读取数据
 * @param fp 指向要读取的文件对象的指针
 * @param buff 用于存储读取数据的缓冲区
 * @param btr 要读取的字节数
 * @param br 实际读取的字节数
 * @return 操作结果，FRESULT类型
 */
FRESULT f_read (FIL* fp, void* buff, UINT btr, UINT* br);

/**
 * @brief 向文件中写入数据
 * @param fp 指向要写入的文件对象的指针
 * @param buff 要写入的数据缓冲区
 * @param btw 要写入的字节数
 * @param bw 实际写入的字节数
 * @return 操作结果，FRESULT类型
 */
FRESULT f_write (FIL* fp, const void* buff, UINT btw, UINT* bw);

/**
 * @brief 移动文件对象的文件指针
 * @param fp 指向文件对象的指针
 * @param ofs 要移动到的文件偏移量
 * @return 操作结果，FRESULT类型
 */
FRESULT f_lseek (FIL* fp, FSIZE_t ofs);

/**
 * @brief 截断文件,将文件的大小调整为当前文件指针所在的位置
 * @param fp 指向要截断的文件对象的指针
 * @return 操作结果，FRESULT类型
 */
FRESULT f_truncate (FIL* fp);

/**
 * @brief 刷新正在写入的文件的缓存数据
 * @param fp 指向要刷新的文件对象的指针
 * @return 操作结果，FRESULT类型
 */
FRESULT f_sync (FIL* fp);

/**
 * @brief 打开一个目录
 * @param dp 指向目录对象的指针
 * @param path 要打开的目录的路径
 * @return 操作结果，FRESULT类型
 */
FRESULT f_opendir (DIR* dp, const FTCHAR* path);

/**
 * @brief 关闭一个已打开的目录
 * @param dp 指向要关闭的目录对象的指针
 * @return 操作结果，FRESULT类型
 */
FRESULT f_closedir (DIR* dp);

/**
 * @brief 读取一个目录项
 * @param dp 指向目录对象的指针
 * @param fno 用于存储目录项信息的结构体指针
 * @return 操作结果，FRESULT类型
 */
FRESULT f_readdir (DIR* dp, FILINFO* fno);

/**
 * @brief 查找第一个符合条件的文件
 * @param dp 指向目录对象的指针
 * @param fno 用于存储文件信息的结构体指针
 * @param path 查找的路径
 * @param pattern 查找的文件模式
 * @return 操作结果，FRESULT类型
 */
FRESULT f_findfirst (DIR* dp, FILINFO* fno, const FTCHAR* path, const FTCHAR* pattern);

/**
 * @brief 查找下一个符合条件的文件
 * @param dp 指向目录对象的指针
 * @param fno 用于存储文件信息的结构体指针
 * @return 操作结果，FRESULT类型
 */
FRESULT f_findnext (DIR* dp, FILINFO* fno);

/**
 * @brief 创建一个子目录
 * @param path 要创建的子目录的路径
 * @return 操作结果，FRESULT类型
 */
FRESULT f_mkdir (const FTCHAR* path);

/**
 * @brief 删除一个已存在的文件或目录
 * @param path 要删除的文件或目录的路径
 * @return 操作结果，FRESULT类型
 */
FRESULT f_unlink (const FTCHAR* path);

/**
 * @brief 重命名/移动一个文件或目录
 * @param path_old 原文件或目录的路径
 * @param path_new 新文件或目录的路径
 * @return 操作结果，FRESULT类型
 */
FRESULT f_rename (const FTCHAR* path_old, const FTCHAR* path_new);

/**
 * @brief 获取文件状态
 * @param path 要获取状态的文件的路径
 * @param fno 用于存储文件信息的结构体指针
 * @return 操作结果，FRESULT类型
 */
FRESULT f_stat (const FTCHAR* path, FILINFO* fno);

/**
 * @brief 修改文件/目录的属性
 * @param path 要修改属性的文件或目录的路径
 * @param attr 要设置的属性
 * @param mask 用于屏蔽的属性
 * @return 操作结果，FRESULT类型
 */
FRESULT f_chmod (const FTCHAR* path, BYTE attr, BYTE mask);

/**
 * @brief 修改文件/目录的时间戳
 * @param path 要修改时间戳的文件或目录的路径
 * @param fno 包含新时间戳信息的结构体指针
 * @return 操作结果，FRESULT类型
 */
FRESULT f_utime (const FTCHAR* path, const FILINFO* fno);

/**
 * @brief 更改当前目录
 * @param path 要更改到的目录的路径
 * @return 操作结果，FRESULT类型
 */
FRESULT f_chdir (const FTCHAR* path);

/**
 * @brief 更改当前驱动器
 * @param path 包含驱动器号的路径
 * @return 操作结果，FRESULT类型
 */
FRESULT f_chdrive (const FTCHAR* path);

/**
 * @brief 获取当前目录
 * @param buff 用于存储当前目录路径的缓冲区
 * @param len 缓冲区的长度
 * @return 操作结果，FRESULT类型
 */
FRESULT f_getcwd (FTCHAR* buff, UINT len);

/**
 * @brief 获取驱动器上的空闲簇数量
 * @param path 包含驱动器号的路径
 * @param nclst 用于存储空闲簇数量的变量指针
 * @param fatfs 用于存储文件系统对象指针的指针
 * @return 操作结果，FRESULT类型
 */
FRESULT f_getfree (const FTCHAR* path, DWORD* nclst, FATFS** fatfs);

/**
 * @brief 获取卷标
 * @param path 包含驱动器号的路径
 * @param label 用于存储卷标的缓冲区
 * @param vsn 用于存储卷序列号的变量指针
 * @return 操作结果，FRESULT类型
 */
FRESULT f_getlabel (const FTCHAR* path, FTCHAR* label, DWORD* vsn);

/**
 * @brief 设置卷标
 * @param label 要设置的卷标
 * @return 操作结果，FRESULT类型
 */
FRESULT f_setlabel (const FTCHAR* label);

/**
 * @brief 将数据转发到流中
 * @param fp 指向文件对象的指针
 * @param func 数据转发函数指针
 * @param btf 要转发的字节数
 * @param bf 实际转发的字节数
 * @return 操作结果，FRESULT类型
 */
FRESULT f_forward (FIL* fp, UINT(*func)(const BYTE*,UINT), UINT btf, UINT* bf);

/**
 * @brief 为文件分配一个连续的块
 * @param fp 指向文件对象的指针
 * @param fsz 要分配的块大小
 * @param opt 分配选项
 * @return 操作结果，FRESULT类型
 */
FRESULT f_expand (FIL* fp, FSIZE_t fsz, BYTE opt);

/**
 * @brief 挂载/卸载一个逻辑驱动器
 * @param fs 指向要注册的文件系统对象的指针（传入 NULL 表示执行卸载操作）
 * @param path 要挂载或卸载的逻辑驱动器号
 * @param opt 挂载选项：0 = 不立即挂载（延迟挂载），1 = 立即挂载
 * @return 操作结果，FRESULT类型
 */
FRESULT f_mount (FATFS* fs, const FTCHAR* path, BYTE opt);

/**
 * @brief 创建一个 FAT 文件系统卷
 * @param path 包含驱动器号的路径
 * @param opt 文件系统创建参数
 * @param work 工作缓冲区
 * @param len 工作缓冲区长度
 * @return 操作结果，FRESULT类型
 */
FRESULT f_mkfs (const FTCHAR* path, const MKFS_PARM* opt, void* work, UINT len);

/**
 * @brief 将一个物理驱动器划分为若干个分区
 * @param pdrv 物理驱动器号
 * @param ptbl 分区表
 * @param work 工作缓冲区
 * @return 操作结果，FRESULT类型
 */
FRESULT f_fdisk (BYTE pdrv, const LBA_t ptbl[], void* work);

/**
 * @brief 设置当前代码页
 * @param cp 要设置的代码页
 * @return 操作结果，FRESULT类型
 */
FRESULT f_setcp (WORD cp);

/**
 * @brief 向文件中写入一个字符
 * @param c 要写入的字符
 * @param fp 指向文件对象的指针
 * @return 操作结果，成功返回字符，失败返回EOF
 */
int f_putc (FTCHAR c, FIL* fp);

/**
 * @brief 向文件中写入一个字符串
 * @param str 要写入的字符串
 * @param cp 指向文件对象的指针
 * @return 操作结果，成功返回非负值，失败返回EOF
 */
int f_puts (const FTCHAR* str, FIL* cp);

/**
 * @brief 向文件中写入一个格式化字符串
 * @param fp 指向文件对象的指针
 * @param str 格式化字符串
 * @param ... 可变参数
 * @return 操作结果，成功返回写入的字符数，失败返回负值
 */
int f_printf (FIL* fp, const FTCHAR* str, ...);

/**
 * @brief 从文件中读取一个字符串
 * @param buff 用于存储读取字符串的缓冲区
 * @param len 缓冲区的长度
 * @param fp 指向文件对象的指针
 * @return 指向读取字符串的指针，失败返回NULL
 */
FTCHAR* f_gets (FTCHAR* buff, int len, FIL* fp);

/* 部分 API 函数以宏的形式实现 */

// 判断文件是否到达末尾
// 若文件指针位置等于文件大小，则表示文件已到末尾，返回 1；否则返回 0
#define f_eof(fp) ((int)((fp)->fptr == (fp)->obj.objsize))

// 获取文件操作的错误状态
// 返回文件对象的错误标志位，用于判断文件操作过程中是否出现错误
#define f_error(fp) ((fp)->err)

// 获取当前文件指针的位置
// 返回文件对象的文件指针值，即当前读写操作在文件中的位置
#define f_tell(fp) ((fp)->fptr)

// 获取文件的大小
// 返回文件对象的文件大小值，即文件所占用的字节数
#define f_size(fp) ((fp)->obj.objsize)

// 将文件指针重置到文件开头
// 调用 f_lseek 函数将文件指针移动到文件的起始位置（偏移量为 0）
#define f_rewind(fp) f_lseek((fp), 0)

// 重置目录读取指针
// 调用 f_readdir 函数，传入目录对象指针和 0，将目录读取指针重置到目录开头
#define f_rewinddir(dp) f_readdir((dp), 0)

// 删除目录
// 调用 f_unlink 函数来删除指定路径的目录，功能等同于删除文件的 f_unlink 操作
#define f_rmdir(path) f_unlink(path)

// 卸载文件系统卷
// 调用 f_mount 函数，传入空的文件系统对象指针、卷路径和卸载标志 0，实现卷的卸载操作
#define f_unmount(path) f_mount(0, path, 0)



/*--------------------------------------------------------------*/
/* 附加函数                                         */
/*--------------------------------------------------------------*/

/* RTC 函数（由用户提供） */
// 如果不是只读文件系统且需要 RTC 支持
#if !FF_FS_READONLY && !FF_FS_NORTC
    // 获取当前时间，返回一个表示 FAT 时间格式的 DWORD 值
    DWORD get_fattime (void);	
#endif

/* 长文件名（LFN）支持函数（在 ffunicode.c 中定义） */
// 如果启用了长文件名支持（FF_USE_LFN 大于等于 1）
#if FF_USE_LFN >= 1
    // 将 OEM 编码字符转换为 Unicode 字符，cp 为代码页
    WCHAR ff_oem2uni (WCHAR oem, WORD cp);	
    // 将 Unicode 字符转换为 OEM 编码字符，cp 为代码页
    WCHAR ff_uni2oem (DWORD uni, WORD cp);	
    // 将 Unicode 字符转换为大写形式
    DWORD ff_wtoupper (DWORD uni);			
#endif

/* 依赖操作系统的函数（示例在 ffsystem.c 中） */
// 如果使用动态内存分配方式支持长文件名（FF_USE_LFN 等于 3）
#if FF_USE_LFN == 3
    // 分配指定大小的内存块
    void* ff_memalloc (UINT msize);		
    // 释放之前分配的内存块
    void ff_memfree (void* mblock);		
#endif
// 如果文件系统支持可重入操作
#if FF_FS_REENTRANT
    // 为指定卷创建一个同步对象，返回操作结果
    int ff_mutex_create (int vol);		
    // 删除指定卷的同步对象
    void ff_mutex_delete (int vol);		
    // 锁定指定卷的同步对象，返回操作结果
    int ff_mutex_take (int vol);		
    // 解锁指定卷的同步对象
    void ff_mutex_give (int vol);		
#endif

/*--------------------------------------------------------------*/
/* 标志和偏移地址                                     */
/*--------------------------------------------------------------*/

/* 文件访问模式和打开方法标志（f_open 函数的第三个参数） */
// 以只读模式打开文件
#define FA_READ             0x01	
// 以写模式打开文件
#define FA_WRITE            0x02	
// 打开已存在的文件，若文件不存在则打开失败
#define FA_OPEN_EXISTING    0x00	
// 创建一个新文件，若文件已存在则打开失败
#define FA_CREATE_NEW       0x04	
// 总是创建一个新文件，若文件已存在则覆盖原文件
#define FA_CREATE_ALWAYS    0x08	
// 总是打开文件，若文件不存在则创建一个新文件
#define FA_OPEN_ALWAYS      0x10	
// 以追加模式打开文件，若文件不存在则创建一个新文件
#define FA_OPEN_APPEND      0x30	

/* 快速查找控制（f_lseek 函数的第二个参数） */
// 创建簇链接映射表
#define CREATE_LINKMAP ((FSIZE_t)0 - 1)	

/* 格式化选项（f_mkfs 函数的第二个参数） */
// 格式化为 FAT12/FAT16 文件系统
#define FM_FAT        0x01	
// 格式化为 FAT32 文件系统
#define FM_FAT32      0x02	
// 格式化为 exFAT 文件系统
#define FM_EXFAT      0x04	
// 自动选择合适的 FAT 文件系统进行格式化
#define FM_ANY        0x07	
// 仅格式化单个分区
#define FM_SFD        0x08	

/* 文件系统类型（FATFS.fs_type） */
// FAT12 文件系统
#define FS_FAT12      1	
// FAT16 文件系统
#define FS_FAT16      2	
// FAT32 文件系统
#define FS_FAT32      3	
// exFAT 文件系统
#define FS_EXFAT      4	

/* 目录项的文件属性位（FILINFO.fattrib） */
// 只读属性
#define AM_RDO 0x01	
// 隐藏属性
#define AM_HID 0x02	
// 系统属性
#define AM_SYS 0x04	
// 目录属性
#define AM_DIR 0x10	
// 存档属性
#define AM_ARC 0x20	


#ifdef __cplusplus
}
#endif

#endif /* FF_DEFINED */
