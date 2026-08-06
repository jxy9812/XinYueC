/*---------------------------------------------------------------------------/
/  FatFs模块配置
/---------------------------------------------------------------------------*/

#define FFCONF_DEF	5380	/* 修订版本ID */

/* XinYueC 的 FatFS 配置覆盖。未启用 XFILE_USE_FATFS 时保持 FatFs 默认值，
 * 这样该第三方库仍可被独立编译。 */
#include "XFileSystem_config.h"

/*---------------------------------------------------------------------------/
/ 功能配置（针对XFile模块优化）
/---------------------------------------------------------------------------*/

#if defined(XFILE_USE_FATFS)
#define FF_FS_READONLY	XFILE_FATFS_READONLY
#else
#define FF_FS_READONLY	0
#endif
/* 此选项用于切换只读配置。(0: 读写模式 或 1: 只读模式)
/  只读配置会移除写入相关的API函数，如f_write()、f_sync()、
/  f_unlink()、f_mkdir()、f_chmod()、f_rename()、f_truncate()、f_getfree()
/  以及可选的写入函数。 */

#define FF_FS_MINIMIZE	0
/* 此选项定义最小化级别，用于移除一些基本的API函数。
/
/   0: 基本函数全部启用。
/   1: 移除f_stat()、f_getfree()、f_unlink()、f_mkdir()、f_truncate()和f_rename()函数。
/   2: 在级别1的基础上，再移除f_opendir()、f_readdir()和f_closedir()函数。
/   3: 在级别2的基础上，再移除f_lseek()函数。 */

#define FF_USE_FIND		0
/* 此选项用于切换过滤目录读取函数f_findfirst()和f_findnext()的启用状态。(0: 禁用, 1: 启用 2: 同时匹配altname[]启用)
/  已关闭：XFile使用opendir/readdir实现目录遍历 */

#if defined(XFILE_USE_FATFS)
#define FF_USE_MKFS		XFILE_FATFS_USE_MKFS
#else
#define FF_USE_MKFS		1
#endif
/* 此选项用于切换f_mkfs()函数的启用状态。(0: 禁用 或 1: 启用)

#define FF_USE_FASTSEEK	1
/* 此选项用于切换快速查找功能的启用状态。(0: 禁用 或 1: 启用) */

#define FF_USE_EXPAND	0
/* 此选项用于切换f_expand()函数的启用状态。(0: 禁用 或 1: 启用)
/  已关闭：可用f_truncate替代 */

#define FF_USE_CHMOD	1
/* 此选项用于切换属性控制API函数f_chmod()和f_utime()的启用状态。
/  (0: 禁用 或 1: 启用) 此外，要启用此选项，FF_FS_READONLY必须为0。 */

#define FF_USE_LABEL	0
/* 此选项用于切换卷标API函数f_getlabel()和f_setlabel()的启用状态。
/  (0: 禁用 或 1: 启用)
/  已关闭：XFile不需要卷标操作 */

#define FF_USE_FORWARD	0
/* 此选项用于切换f_forward()函数的启用状态。(0: 禁用 或 1: 启用)
/  已关闭：XFile不使用流式转发 */

#define FF_USE_STRFUNC	0
#define FF_PRINT_LLI	0
#define FF_PRINT_FLOAT	0
#define FF_STRF_ENCODE	0
/* 已关闭：XFile有自己的字符串处理，不需要Fatfs的printf支持 */
/* FF_USE_STRFUNC用于切换字符串API函数f_gets()、f_putc()、f_puts()和f_printf()的启用状态。
/
/   0: 禁用。FF_PRINT_LLI、FF_PRINT_FLOAT和FF_STRF_ENCODE无效。
/   1: 启用，不进行LF - CRLF转换。
/   2: 启用，进行LF - CRLF转换。
/
/  FF_PRINT_LLI = 1使f_printf()支持long long类型参数，FF_PRINT_FLOAT = 1/2使f_printf()支持浮点类型参数。
/  这些特性需要C99或更高版本的支持。
/  当FF_LFN_UNICODE >= 1且启用了LFN时，字符串API函数会对其中的字符编码进行转换。
/  FF_STRF_ENCODE选择通过这些函数读写文件时文件的字符编码假设。
/
/   0: 当前代码页的ANSI/OEM编码
/   1: UTF-16LE格式的Unicode编码
/   2: UTF-16BE格式的Unicode编码
/   3: UTF-8格式的Unicode编码
*/

/*---------------------------------------------------------------------------/
/ 区域设置和命名空间配置
/---------------------------------------------------------------------------*/

#if defined(XFILE_USE_FATFS)
#define FF_CODE_PAGE	XFILE_FATFS_CODE_PAGE
#else
#define FF_CODE_PAGE	936
#endif
/* 此选项指定目标系统上使用的OEM代码页。
/  错误的代码页设置可能会导致文件打开失败。
/
/   437 - 美国
/   720 - 阿拉伯语
/   737 - 希腊语
/   771 - KBL
/   775 - 波罗的海语
/   850 - 拉丁语1
/   852 - 拉丁语2
/   855 - 西里尔语
/   857 - 土耳其语
/   860 - 葡萄牙语
/   861 - 冰岛语
/   862 - 希伯来语
/   863 - 加拿大法语
/   864 - 阿拉伯语
/   865 - 北欧语
/   866 - 俄语
/   869 - 希腊语2
/   932 - 日语 (双字节字符集)
/   936 - 简体中文 (双字节字符集)
/   949 - 韩语 (双字节字符集)
/   950 - 繁体中文 (双字节字符集)
/     0 - 包含上述所有代码页，并通过f_setcp()进行配置
*/

#if defined(XFILE_USE_FATFS)
#define FF_USE_LFN		XFILE_FATFS_LFN
#else
#define FF_USE_LFN		3
#endif
#define FF_MAX_LFN		255
/* FF_USE_LFN用于切换对长文件名(LFN)的支持。
/
/   0: 禁用LFN。FF_MAX_LFN无效。
/   1: 使用BSS段上的静态工作缓冲区启用LFN。始终不是线程安全的。
/   2: 使用栈上的动态工作缓冲区启用LFN。
/   3: 使用堆上的动态工作缓冲区启用LFN。
/
/  要启用LFN，需要将ffunicode.c添加到项目中。LFN功能需要一个占用(FF_MAX_LFN + 1) * 2字节的内部工作缓冲区，
/  当启用exFAT时，还需要额外的(FF_MAX_LFN + 44) / 15 * 32字节。
/  FF_MAX_LFN定义了以UTF-16代码单元为单位的工作缓冲区大小，其范围可以在12到255之间。
/  建议将其设置为255以完全支持LFN规范。
/  当使用栈作为工作缓冲区时，要注意栈溢出问题。当使用堆内存作为工作缓冲区时，
/  需要将ffsystem.c中示例的内存管理函数ff_memalloc()和ff_memfree()添加到项目中。 */

#define FF_LFN_UNICODE	2
/* 此选项用于在启用LFN时切换API上的字符编码。
/
/   0: 当前代码页的ANSI/OEM编码 (TCHAR = char)
/   1: UTF-16格式的Unicode编码 (TCHAR = WCHAR)
/   2: UTF-8格式的Unicode编码 (TCHAR = char)
/   3: UTF-32格式的Unicode编码 (TCHAR = DWORD)
/
/  此选项还会影响字符串I/O函数的行为。
/  当LFN未启用时，此选项无效。 */

#define FF_LFN_BUF		255
#define FF_SFN_BUF		12
/* 这组选项定义了FILINFO结构中文件名成员的大小，该结构用于读取目录项。
/  这些值应该足够大，以容纳要读取的文件名。
/  读取的文件名的最大可能长度取决于字符编码。当LFN未启用时，这些选项无效。 */

#define FF_FS_RPATH		2
/* 此选项用于配置对相对路径的支持。
/
/   0: 禁用相对路径并移除相关的API函数。
/   1: 启用相对路径。f_chdir()和f_chdrive()可用。
/   2: 在级别1的基础上，f_getcwd()可用。
*/

/*---------------------------------------------------------------------------/
/ 驱动器/卷配置
/---------------------------------------------------------------------------*/

#if defined(XFILE_USE_FATFS)
#define FF_VOLUMES		XFILE_FATFS_VOLUMES
#else
#define FF_VOLUMES		10
#endif
/* 要使用的卷(逻辑驱动器)数量。(1 - 10) */

#if defined(XFILE_USE_FATFS)
#define FF_STR_VOLUME_ID	XFILE_FATFS_STR_VOLUME_ID
#else
#define FF_STR_VOLUME_ID	0
#endif
#if defined(XFILE_USE_FATFS)
#define FF_VOLUME_STRS		XFILE_FATFS_VOLUME_STRS
#else
#define FF_VOLUME_STRS		"RAM","NAND","CF","SD","SD2","USB","USB2","USB3"
#endif
/* FF_STR_VOLUME_ID用于切换对任意字符串作为卷ID的支持。
/  当FF_STR_VOLUME_ID设置为1或2时，任意字符串可以在路径名中用作驱动器号。
/  FF_VOLUME_STRS定义了每个逻辑驱动器的卷ID字符串。
/  项的数量不得少于FF_VOLUMES。卷ID字符串的有效字符为A - Z、a - z和0 - 9，
/  不过比较时不区分大小写。如果FF_STR_VOLUME_ID >= 1且未定义FF_VOLUME_STRS，
/  则需要一个用户定义的卷字符串表，如下所示：
/
/  const char* VolumeStr[FF_VOLUMES] = {"ram","flash","sd","usb",...
*/

#define FF_MULTI_PARTITION	0
/* 此选项用于切换对物理驱动器上多个卷的支持。
/  默认情况下(0)，每个逻辑驱动器号绑定到相同的物理驱动器号，
/  并且只会挂载物理驱动器上找到的一个FAT卷。
/  当此功能启用(1)时，每个逻辑驱动器号可以绑定到VolToPart[]中列出的任意物理驱动器和分区。
/  此外，f_fdisk()函数也将可用。 */

#define FF_MIN_SS		512
#define FF_MAX_SS		512
/* 这组选项配置了支持的扇区大小范围。(512、1024、2048或4096)
/  对于大多数系统、通用存储卡和硬盘，通常将两者都设置为512，
/  但对于板载闪存和某些类型的光学媒体，可能需要更大的值。
/  当FF_MAX_SS大于FF_MIN_SS时，FatFs配置为可变扇区大小模式，
/  并且disk_ioctl()需要实现GET_SECTOR_SIZE命令。 */

#define FF_LBA64		0
/* 此选项用于切换对64位LBA的支持。(0: 禁用 或 1: 启用)
/  要启用64位LBA，还需要启用exFAT。(FF_FS_EXFAT == 1) */

#define FF_MIN_GPT		0x10000000
/* 在f_mkfs()和f_fdisk()中作为分区格式切换到GPT的最小扇区数。最大为2^32个扇区。
/  当FF_LBA64 == 0时，此选项无效。 */

#define FF_USE_TRIM		0
/* 此选项用于切换对ATA-TRIM的支持。(0: 禁用 或 1: 启用)
/  要启用此功能，还需要在disk_ioctl()中实现CTRL_TRIM命令。 */

/*---------------------------------------------------------------------------/
/ 系统配置
/---------------------------------------------------------------------------*/

#define FF_FS_TINY		0
/* 此选项用于切换小缓冲区配置。(0: 正常 或 1: 小缓冲区)
/  在小缓冲区配置下，文件对象(FIL)的大小会缩小FF_MAX_SS字节。
/  代替从文件对象中移除的私有扇区缓冲区，文件系统对象(FATFS)中的公共扇区缓冲区用于文件数据传输。 */

#if defined(XFILE_USE_FATFS)
#define FF_FS_EXFAT		XFILE_FATFS_USE_EXFAT
#else
#define FF_FS_EXFAT		1
#endif
/* 此选项用于切换对exFAT文件系统的支持。(0: 禁用 或 1: 启用)
/  要启用exFAT，还需要启用LFN。(FF_USE_LFN >= 1)
/  注意，启用exFAT会丢弃ANSI C (C89)兼容性。 */

#define FF_FS_NORTC		0
#define FF_NORTC_MON	11
#define FF_NORTC_MDAY	1
#define FF_NORTC_YEAR	2024
/* 选项FF_FS_NORTC用于切换时间戳功能。如果系统没有实时时钟(RTC)
/  或者不需要有效的时间戳，将FF_FS_NORTC设置为1以禁用时间戳功能。
/  由FatFs修改的每个对象将具有由FF_NORTC_MON、FF_NORTC_MDAY和FF_NORTC_YEAR
/  在本地时间定义的固定时间戳。
/  要启用时间戳功能(FF_FS_NORTC = 0)，需要将get_fattime()添加到项目中，
/  以从实时时钟读取当前时间。FF_NORTC_MON、FF_NORTC_MDAY和FF_NORTC_YEAR无效。
/  这些选项在只读配置(FF_FS_READONLY = 1)中无效。 */

#define FF_FS_NOFSINFO	0
/* 如果需要知道FAT32卷上的正确可用空间，请设置此选项的第0位，
/  并且在挂载卷后第一次调用f_getfree()将强制进行完整的FAT扫描。
/  第1位控制对最后分配的簇号的使用。
/
/  位0 = 0: 如果可用，使用FSINFO中的可用簇计数。
/  位0 = 1: 不相信FSINFO中的可用簇计数。
/  位1 = 0: 如果可用，使用FSINFO中的最后分配的簇号。
/  位1 = 1: 不相信FSINFO中的最后分配的簇号。
*/

#define FF_FS_LOCK		0
/* 选项FF_FS_LOCK用于切换文件锁定功能，以控制重复打开文件和对打开对象的非法操作。
/  当FF_FS_READONLY为1时，此选项必须为0。
/
/  0: 禁用文件锁定功能。为避免卷损坏，应用程序应避免对打开的对象进行非法打开、删除和重命名操作。
/  >0: 启用文件锁定功能。该值定义了在文件锁定控制下可以同时打开的文件/子目录数量。
/  请注意，文件锁定控制与可重入性无关。 */

#define FF_FS_REENTRANT	1
#define FF_FS_TIMEOUT	1000
/* 选项 FF_FS_REENTRANT 用于切换 FatFs 模块本身的可重入性（线程安全）。
/  请注意，无论此选项如何设置，对不同卷的文件访问始终是可重入的，而卷控制函数（如 f_mount()、f_mkfs()
/  和 f_fdisk()）始终是不可重入的。只有对同一卷的文件/目录访问才受此功能的控制。
/
/   0: 禁用可重入性。此时 FF_FS_TIMEOUT 选项无效。
/   1: 启用可重入性。此外，用户必须提供同步处理函数，即 ff_mutex_create()、ff_mutex_delete()、
/      ff_mutex_take() 和 ff_mutex_give()，并将它们添加到项目中。示例代码可在 ffsystem.c 中找到。
/
/  FF_FS_TIMEOUT 以操作系统的时钟节拍为单位定义了超时时间。
*/

/*--- 配置选项结束 ---*/
