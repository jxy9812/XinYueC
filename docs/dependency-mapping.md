# 依赖映射清单：C 标准库/平台功能 → 库内自研 API

> 生成环境：仓库 `/home/xinyue/Code/XinYueC`，分支 `codex/xdevice-file-platform`，HEAD `e728ca64`。
> 本清单是后续所有"标准库替换路"的**唯一依据**。所有签名均摘自本仓库头文件原文（附 `文件:行号`）。
>
> **替换铁律**（沿用任务口径）：
> 1. 只做"库内已有同功能 API"的替换；语义不完全等价或有行为风险的一律不动、记入第十章 deferred。
> 2. 禁碰 `Drive/`（平台适配层调用 OS 是其职责）。
> 3. 禁碰主树 `bin/`（总验收由主线统一重建）。
> 4. 改完对所改每个 TU 按 `build/compile_commands.json` 口径 `-fsyntax-only` 须 rc=0。

---

## 一、文件 I/O：stdio.h / dirent.h / stat.h → XFile 家族

模块总览（开关均默认开启，见 `Src/XCode/XFile/XFileSystem_config.h:25-58`）：
`XFile`（文件对象）、`XFileDevice`（文件设备基类）、`XFileInfo`（文件属性）、`XDir`（目录）、`XSaveFile`（原子保存）、`XStorageInfo`（存储容量）。
读写定位/读写统一入口在 `Src/XIO/XIODevice/XIODevice.h`（XFile 的基类链：XObject → XClass → XIODevice → XFileDevice → XFile）。

| 标准库函数 | 库内 API（签名） | 头文件:行 |
|---|---|---|
| `fopen` | `XFile* XFile_create_2(const XString* name);` + `bool XFile_open_2(XFile* file, XIODeviceBaseMode mode, XFilePermissions permissions);`（模式枚举 `XIODevice_ReadOnly/WriteOnly/ReadWrite/Append/Truncate/Text/Unbuffered`，`XIODevice.h:35-49`） | `Src/XCode/XFile/XFile/XFile.h:73,164` |
| `fclose` | `void XIODevice_close_base(XIODevice* self);`（`_base` 即本库公共虚派发入口，库内惯用调用形式见 `Src/XGui/Graphics/XImageWriter.c:135`） | `Src/XIO/XIODevice/XIODevice.h:363` |
| `fread` | `int64_t XIODevice_read_1(XIODevice* self, char* data, int64_t maxlen);`；另有 `read_2/_3`、`XIODevice_readAll_1/2/3`、`XIODevice_readLine_1/2/3` | `Src/XIO/XIODevice/XIODevice.h:193-230,210-212,221-230` |
| `fwrite` | `int64_t XIODevice_write_1(XIODevice* self, const char* data, int64_t len);`（`write_2` 收 `const XByteArray*`，`write_3` 收 C 字符串） | `Src/XIO/XIODevice/XIODevice.h:267-283` |
| `fseek` | `bool XIODevice_seek_base(XIODevice* self, int64_t pos);`（重置到头：`XIODevice_reset_base`，`:406`） | `Src/XIO/XIODevice/XIODevice.h:392` |
| `ftell` | `int64_t XIODevice_pos_base(const XIODevice* self);` | `Src/XIO/XIODevice/XIODevice.h:377` |
| `fseek(SEEK_END)+ftell` 求大小 | `int64_t XIODevice_size_base(const XIODevice* self);` | `Src/XIO/XIODevice/XIODevice.h:384` |
| `fflush` | `bool XIODevice_flush(XIODevice* self);`（XFile 上宏映射为 `XFile_flush`） | `Src/XIO/XIODevice/XIODevice.h:290`；`Src/XCode/XFile/XFile/XFile.h:133` |
| `remove` | `bool XFile_remove_static(const XString* fileName);`（成员版 `XFile_remove`） | `Src/XCode/XFile/XFile/XFile.h:211,204` |
| `rename` | `bool XFile_rename_static(const XString* oldName, const XString* newName);`（成员版 `XFile_rename`；目录改名用 `XDir_rename`） | `Src/XCode/XFile/XFile/XFile.h:231,226`；`Src/XCode/XFile/XDir/XDir.h:381` |
| `access`/存在检查 | `bool XFile_exists_static(const XString* fileName);`；`bool XFileInfo_exists_static(const XString* path);` | `Src/XCode/XFile/XFile/XFile.h:193`；`Src/XCode/XFile/XFileInfo/XFileInfo.h:188` |
| `stat` 全量属性 | `void XFileInfo_stat(XFileInfo* info);` + `XFileStat` 结构（`size/permissions/ownerId/isFile/isDir/isSymLink/isHidden…` 位域，`XFileInfo.h:84-113`）；便捷判定：`XFileInfo_isFile/isDir/isSymLink/isReadable/isWritable/isExecutable/isHidden` | `Src/XCode/XFile/XFileInfo/XFileInfo.h:190-219` |
| `stat::st_size` | `int64_t XFileInfo_size(const XFileInfo* info);` | `Src/XCode/XFile/XFileInfo/XFileInfo.h:225` |
| `stat::st_mode` 权限位 | `XFilePermissions XFileInfo_permissions(const XFileInfo* info);`、`bool XFileInfo_permission(const XFileInfo*, XFilePermissions);`、静态版 `XFile_permissions_static` / `XFile_setPermissions_static` | `Src/XCode/XFile/XFileInfo/XFileInfo.h:218-219`；`Src/XCode/XFile/XFile/XFile.h:327,335` |
| `stat::st_mtime/st_atime/st_ctime`（ Birth/Access/Modify ） | `XDateTime XFileInfo_lastModified/lastRead/birthTime/metadataChangeTime(const XFileInfo*);`、`XDateTime XFileInfo_fileTime(const XFileInfo*, XFileTime);`；写侧 `XDateTime XFileDevice_fileTime(const XFileDevice*, XFileTime);` + `bool XFileDevice_setFileTime(XFileDevice*, const XDateTime*, XFileTime);` | `Src/XCode/XFile/XFileInfo/XFileInfo.h:226-230`；`Src/XCode/XFile/XFileDevice/XFileDevice.h:241,251` |
| `truncate` | `bool XFile_resize_static(const XString* fileName, int64_t sz);`（虚入口 `XFileDevice_resize_base`） | `Src/XCode/XFile/XFile/XFile.h:320`；`Src/XCode/XFile/XFileDevice/XFileDevice.h:179` |
| `opendir/readdir/closedir` 目录枚举 | `XStringList* XDir_entryList_1(const XDir* dir, XDirFilters filters, XDirSortFlags sort);`、`XStringList* XDir_entryList_2(const XDir*, const XStringList* nameFilters, XDirFilters, XDirSortFlags);`、带属性版 `XFileInfoList* XDir_entryInfoList_1/2(...)`（`XFileInfoList` 即 `XVector`，`XFileInfo.h:114`）；`size_t XDir_count(const XDir*);`、`XString* XDir_at(const XDir*, size_t pos);` | `Src/XCode/XFile/XDir/XDir.h:283-314,266,274` |
| `mkdir` / `mkdir -p` | `bool XDir_mkdir(XDir* dir, const XString* dirName);` / `bool XDir_mkpath(XDir* dir, const XString* dirPath);` | `Src/XCode/XFile/XDir/XDir.h:333,341` |
| `rmdir` / 递归删 | `bool XDir_rmdir(XDir* dir, const XString* dirName);` / `bool XDir_rmpath(...)` / `bool XDir_removeRecursively(XDir* dir);` | `Src/XCode/XFile/XDir/XDir.h:349,357,364` |
| `getcwd` | `XString* XDir_currentPath(void);` | `Src/XCode/XFile/XDir/XDir.h:577` |
| `chdir` | `bool XDir_setCurrent(const XString* path);` | `Src/XCode/XFile/XDir/XDir.h:584` |
| `symlink` / `readlink` | `bool XFile_link_static(const XString* fileName, const XString* linkName);` / `XString* XFile_symLinkTarget_static(const XString*);`、`XString* XFileInfo_readSymLink(const XFileInfo*);` | `Src/XCode/XFile/XFile/XFile.h:271,308`；`Src/XCode/XFile/XFileInfo/XFileInfo.h:246` |
| `realpath` | `XString* XFileInfo_canonicalFilePath(const XFileInfo*);` / `XString* XDir_canonicalPath(const XDir*);` | `Src/XCode/XFile/XFileInfo/XFileInfo.h:173`；`Src/XCode/XFile/XDir/XDir.h:205` |
| `basename/dirname` | `XString* XFileInfo_fileName/baseName/suffix/path/absolutePath(const XFileInfo*);`、`XString* XDir_dirName(const XDir*);` | `Src/XCode/XFile/XFileInfo/XFileInfo.h:174-181`；`Src/XCode/XFile/XDir/XDir.h:212` |
| `cp`（复制文件） | `bool XFile_copy_static(const XString* fileName, const XString* newName);` | `Src/XCode/XFile/XFile/XFile.h:251` |
| `mmap/munmap` | `void* XFileDevice_map(XFileDevice*, int64_t offset, int64_t size, XFileDeviceMemoryMapFlags);` / `bool XFileDevice_unmap(XFileDevice*, void* address);` | `Src/XCode/XFile/XFileDevice/XFileDevice.h:284,292` |
| `tmpfile`+`rename` 原子写（配置/状态保存惯用法） | `XSaveFile`：`XSaveFile* XSaveFile_create_2(const XString* name);` + `bool XSaveFile_commit(XSaveFile* file);`（同目录临时文件+原子提交，`directWriteFallback` 兜底） | `Src/XCode/XFile/XSaveFile/XSaveFile.h:85,170,197` |

**替换要点**：① 入参是 `XString*` 而非 `char*`（`XString_create_utf8` 构造，见第五章）；② 打开模式是 `XIODeviceBaseMode` 位标志，非 `"r"/"w"` 字符串，逐位映射：`"r"→ReadOnly`、`"w"→WriteOnly|Truncate|Create`、`"a"→WriteOnly|Append|Create`；③ `remove` 对**目录**无效（C 标准 `remove` 可删空目录），目录须走 `XDir_rmdir`，替换时按对象类型分流（见 deferred-5）。

---

## 二、时间：time.h → XDateTime / XDate / XTime

| 标准库函数 | 库内 API（签名） | 头文件:行 |
|---|---|---|
| `time(NULL)`（秒级墙钟） | `int64_t XDateTime_currentSecsSinceEpoch(void);`、`int64_t XDateTime_currentMSecsSinceEpoch(void);`、`int64_t XDateTime_currentNSecsSinceEpoch(void);` | `Src/XData/XDateTime/XDateTime.h:52-63` |
| `localtime`（当前本地日期时间） | `XDateTime XDateTime_currentDateTime(void);`；分量版 `XDate XDate_currentDate(void);`、`XTime XTime_currentTime(void);` | `Src/XData/XDateTime/XDateTime.h:41`；`Src/XData/XDateTime/XDate.h:39`；`Src/XData/XDateTime/XTime.h:40` |
| `gmtime` | `XDateTime XDateTime_currentDateTimeUtc(void);` | `Src/XData/XDateTime/XDateTime.h:46` |
| `struct tm` 年月日时分秒分量 | `XDate`：`int XDate_year/month/day(const XDate*);`（`XDate.h:60,67,74`）；`XTime`：`int XTime_hour/minute/second/msec(const XTime*);`（`XTime.h:61-82`） | `Src/XData/XDateTime/XDate.h`、`XTime.h` |
| `strftime`（格式化） | `XString* XDateTime_toString_format(const XDateTime*, const char* format);`（Qt 风格格式串 `"yyyy-MM-dd HH:mm:ss"`）、`XString* XDateTime_toString_iso(const XDateTime*);`、`XString* XDate_toString_format(const XDate*, const char*);`、`XString* XTime_toString_format(const XTime*, const char*);` | `Src/XData/XDateTime/XDateTime.h:183,190`；`XDate.h:151`；`XTime.h:133` |
| `strptime`（解析） | `XDateTime XDateTime_fromString_format(const char* str, const char* format);`、`XDateTime XDateTime_fromString_iso(const char* str);` | `Src/XData/XDateTime/XDateTime.h:198,205` |
| `mktime`/`timegm`（epoch 互转） | `int64_t XDateTime_toSecsSinceEpoch(const XDateTime*);` / `bool XDateTime_setSecsSinceEpoch(XDateTime*, int64_t secs);`（毫秒版 `toMSecsSinceEpoch`/`setMSecsSinceEpoch`，`:97,111`） | `Src/XData/XDateTime/XDateTime.h:103,119` |
| `difftime(t1,t0)` | `int64_t XDateTime_secsTo(const XDateTime* from, const XDateTime* to);`、`int64_t XDateTime_msecsTo(const XDateTime*, const XDateTime*);`、`int64_t XDateTime_daysTo(const XDateTime*, const XDateTime*);` | `Src/XData/XDateTime/XDateTime.h:213-229` |

**重要结论（单调时长，2026-09-27 用户裁定后更新）**：`XDateTime_currentMSecsSinceEpoch` 底层已切换为**单调时钟**（posix=`clock_gettime(CLOCK_MONOTONIC)`，win32=`GetTickCount64()`），是全库统一"当前毫秒"计时源——`clock()`/`clock_gettime(CLOCK_MONOTONIC)` 的时长测量场景一律用它，Src 禁直呼时间源。注意其返回值不再是纪元毫秒：需要墙钟时刻的调用方用 `XDateTime_currentDateTime`/`currentSecsSinceEpoch`（现存量调用点已审计迁移，唯一真纪元消费方 XDateTimeEdit 默认值改走 currentDateTime）。`XTimer`（`Src/XTimer/XTimer.h`）是事件定时回调设施，非时长测量仪。

---

## 三、字符分类与转换：ctype.h → XChar

`XChar` 为 `typedef uint16_t XChar`（UTF-16 码元，`Src/XData/XChar/XChar.h:399`），对齐 Qt QChar，全表 Unicode 实现（ASCII 域与 ctype 语义等价，非 ASCII 域为超集）。对单字节 `char` 先 `XChar XChar_fromLatin1(char c);`（`XChar.h:430`）。各函数均有 UCS-4 静态版 `_2(uint32_t)` 后缀变体。

| ctype 函数 | 库内 API（签名） | 头文件:行 |
|---|---|---|
| `isdigit` | `bool XChar_isDigit(XChar ch);`（十进制数字 Nd；泛数字 `XChar_isNumber`，`:616`；数字值 `int XChar_digitValue(XChar);`，`:556`） | `Src/XData/XChar/XChar.h:630` |
| `isspace` | `bool XChar_isSpace(XChar ch);` | `Src/XData/XChar/XChar.h:595` |
| `isalpha` | `bool XChar_isLetter(XChar ch);` | `Src/XData/XChar/XChar.h:609` |
| `isalnum` | `bool XChar_isLetterOrNumber(XChar ch);` | `Src/XData/XChar/XChar.h:623` |
| `ispunct` | `bool XChar_isPunct(XChar ch);` | `Src/XData/XChar/XChar.h:602` |
| `isprint` | `bool XChar_isPrint(XChar ch);` | `Src/XData/XChar/XChar.h:588` |
| `iscntrl` | `bool XChar_isControl(XChar ch);` | `Src/XData/XChar/XChar.h:682` |
| `isupper` / `islower` | `bool XChar_isUpper(XChar ch);` / `bool XChar_isLower(XChar ch);` | `Src/XData/XChar/XChar.h:651,655` |
| `toupper` / `tolower` | `XChar XChar_toUpper(XChar ch);` / `XChar XChar_toLower(XChar ch);`（另有 `toCaseFolded`/`toTitleCase`，`:906,913`） | `Src/XData/XChar/XChar.h:892,899` |

**注意**：对 Latin-1 以外字符，`XChar_isSpace/isAlpha` 等按 Unicode 判定（如全角空格 U+3000 判真而 `isspace` 判假），替换纯 ASCII 处理代码时语义等价；若原代码刻意依赖"非 ASCII 非空白"行为则不动（记 deferred 原则）。

---

## 四、内存：stdlib.h malloc/free/calloc/realloc → XMemory / XClass

| stdlib 函数 | 库内 API（签名） | 头文件:行 |
|---|---|---|
| `malloc` | `void* XMalloc_System(size_t size);`（= 系统分配器；可配置版 `void* XMemory_malloc(size_t size, XMemoryType type);`，type 取 `XMEMORY_TYPE_SYSTEM/MULTIPOOL/HYBRID`，`XMemory.h:67-71`） | `Src/XMemory/XMemory.h:158-159,118` |
| `free` | `void XFree_System(void* ptr);`（可配置版 `void XMemory_free(void* ptr, XMemoryType type);`） | `Src/XMemory/XMemory.h:183-184,123` |
| `calloc` | `void* XCalloc_System(size_t count, size_t size);`（可配置版 `XMemory_calloc`） | `Src/XMemory/XMemory.h:203-204,137` |
| `realloc` | `void* XRealloc_System(void* ptr, size_t size);`（可配置版 `XMemory_realloc`） | `Src/XMemory/XMemory.h:193-194,130` |
| `strdup` | `char* XMemory_strdup(const char* text);`（**必须**用 `XFree_System` 释放） | `Src/XMemory/XMemory.h:169` |
| `aligned_alloc`/`posix_memalign` | `void* XAlignedMalloc_System(size_t size, size_t alignment);` / `void XAlignedFree_System(void* ptr);`（alignment 须为 2 的幂） | `Src/XMemory/XMemory.h:178-179` |
| 对象分配惯用法 | `#define XClass_Malloc(Type) ((Type*)XMemory_malloc(sizeof(Type), XCLASS_DEFAULT_MEMORY_TYPE))`；`#define XNew(type) (type*)XMalloc_System(sizeof(type))` / `#define XDelete(ptr) XFree_System(ptr);` | `Src/XClass/XClass.h:36-37`；`Src/XMemory/XMemory.h:149,154` |

**替换要点**：① 分配/释放必须**成对**替换（`XMalloc_System`↔`XFree_System`；`XClass_Malloc`/池分配的内存不可用系统 `free`），半个替换即引入跨分配器 UB；② 用了 `XMemory_setMallocMethod` 等全局注入的进程，统一走 `XMemory_*` 才能被统计/替换覆盖——这正是替换的价值；③ 裸内存原语见第九章白名单。

---

## 五、字符串：string.h + 数值转换 → XString / XChar

`XString` 内部为 UTF-16，容量自管理（手写 `char` 缓冲拼接的根治替代）。

| 标准/惯用函数 | 库内 API（签名） | 头文件:行 |
|---|---|---|
| `strlen` | `#define XString_size XString_length_base`（`XString_length_base` 即 `XContainer_size_base`）；UTF-8 字节长 `size_t XString_toUtf8_length(const XString*);` | `Src/XContainer/XString/XString.h:1298,1306,197,700` |
| `strcmp` | `int32_t XString_compare(const XString* str1, const XString* str2);`；带大小写开关 `bool XString_equals(const XString*, const XString*, XChar_CaseSensitivity cs);` | `Src/XContainer/XString/XString.h:546,608` |
| `strncmp`/`strncasecmp` | `XString_compare`/`XString_equals(cs)` + `XChar_CaseSensitive/Insensitive`（Qt 语义）；子串比较可取 `XString* XString_left/right/mid(const XString*, size_t…)` 后比较 | `XString.h:546,608,927,935,944` |
| `strcpy`/`strncpy` | `bool XString_assign_utf8(XString* str, const char* utf8_str);` / `bool XString_assign_with_length_utf8(XString*, const char*, size_t len);` | `Src/XContainer/XString/XString.h:276,285` |
| `strcat`/`strncat` | `bool XString_append_utf8(XString* str, const char* utf8_str);` / `bool XString_append_with_length_utf8(XString*, const char*, size_t);` | `Src/XContainer/XString/XString.h:259,260` |
| `strstr` | `int64_t XString_indexOf_utf8(const XString* str, const char* substr, size_t from, XChar_CaseSensitivity cs);`（找不到返回 -1，非 NULL）；布尔版 `bool XString_contains_utf8(...)`；逆向 `XString_lastIndexOf_utf8` | `Src/XContainer/XString/XString.h:410,445,419` |
| `strchr` | `int64_t XString_indexOf_char(const XString* str, XChar ch, size_t from, XChar_CaseSensitivity cs);` | `Src/XContainer/XString/XString.h:1350` |
| `sprintf`/`snprintf` | `XString* XString_create_fmt_utf8(const char* utf8_format, ...);`（新建）/ `bool XString_assign_fmt_utf8(XString* str, const char* utf8_format, ...);`（写入已有对象，等价 snprintf 的"无缓冲溢出"安全性由 XString 自管理保证） | `Src/XContainer/XString/XString.h:92,294` |
| `atoi`/`strtol` | `int XString_toInt(const XString* str, bool* ok, int base);`、`long long XString_toLongLong(const XString*, bool* ok, int base);`（全家族 `toShort/toUShort/toUInt/toULong/toULongLong`，`:771-834`；`ok` 出参替代 `errno`/`endptr` 判错） | `Src/XContainer/XString/XString.h:780,798` |
| `atof`/`strtod` | `double XString_toDouble(const XString* str, bool* ok);` / `float XString_toFloat(const XString*, bool* ok);` | `Src/XContainer/XString/XString.h:850,842` |
| `itoa`（非标准） | `XString* XString_number_llong(long long n, int base);`（宏 `XString_number_int/uint/long/ulong`，`:1205-1211`）；就地版 `bool XString_setNum_int(XString*, int n, int base);` 等全家族（`:860-918`）；浮点 `XString* XString_number_double(double n, char format, int precision);` | `Src/XContainer/XString/XString.h:1228,1205,860,1242` |
| `strtok` | **无有状态等价**；一次性拆分 `XStringList* XString_split_utf8(const XString*, const char* delimiter, XChar_CaseSensitivity cs);` 语义不同 → 见 deferred-2 | `Src/XContainer/XString/XString.h:978` |
| 裸 `char*` 数值转换（不建 XString 直接转） | 无直接等价：`XChar_toInt(const XChar* xchars, size_t input_count, int base, bool* success);` 吃 UTF-16 数组；`char*` 须先经 `XString_create_utf8` 或 `XChar_fromUtf8Stream` 转换 → 见 deferred-3 | `Src/XData/XChar/XChar.h:1179,1022` |

---

## 六、容器：手写数组/静态表 → 库内容器

| 手写惯用法 | 库内设施（关键签名） | 头文件 |
|---|---|---|
| `T arr[N]` + `int n` 动态数组 | `XVector`：`#define XVector_create(typeSize) XVector_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, typeSize, true)`、`#define XVector_Create(Type) XVector_create(sizeof(Type))`、`bool XVector_push_back_1_base(XVector*, void* pvValue);` | `Src/XContainer/XVector/XVector.h:59,83,154` |
| `char* list[]` 字符串数组 | `XStringList`（`XString_split_utf8`、`XDir_entryList_1` 的返回类型） | `Src/XContainer/XStringList/XStringList.h` |
| 键值映射表/手写哈希 | `XMap` / `XHashMap` / `XMapBase` | `Src/XContainer/XMap/XMap/XMap.h` 等 |
| 手写链表 | `XListDLinked` / `XListSLinked` / `XListBase` / `XLockFreeList` | `Src/XContainer/XList/` |
| 环形缓冲/队列/栈 | `XRingBuffer` / `XRingChunk` / `XQueue` / `XStack` | `Src/XContainer/` |
| 位图/字节数组 | `XBitArray` / `XByteArray` | `Src/XContainer/XBitArray/`、`XByteArray/` |
| 二元组 struct | `XPair`：`XPair* XPair_create(const size_t firstTypeSize, const size_t secondTypeSize);`、`void XPair_insert(XPair*, void* firstData, void* secondData);`、`void* XPair_first/second(XPair*);` | `Src/XData/XPair/XPair.h:37,79,117,130` |
| `void*`+类型约定的万能值 | `XVariant`：`XVariant* XVariant_create_int(int val);`（另有 int8…int64/uint/bool/float/double/ptr/size_t 全家族，`XVariant.h:85-181`）、`int64_t XVariant_toInt64(const XVariant*);`、`double XVariant_toDouble(const XVariant*);` | `Src/XData/XVariant/XVariant.h:157,279,375` |
| `void* args[]` 变参实参包 | `XVarList`：`XVarList* XVarList_create(uint8_t count, ...);` | `Src/XData/XVarList/XVarList.h:121` |
| 手写 JSON 拼接/解析 | `XJsonDocument`（`XJsonDocument_fromString(const XString*)`、`XJsonDocument_fromVariant`、`XJsonDocument_root`）、`XJsonValue`/`XJsonObject`/`XJsonArray` | `Src/XData/XJson/XJsonDocument/XJsonDocument.h:235,67,144` |
| 手写引用计数块 | `XSharedData`：`XSharedData* XSharedData_create(void* dataPtr, size_t dataSize);`、`void XSharedData_addRef(XSharedData*);`、`bool XSharedData_release(XSharedData*, XMemory*);`、`int32_t XSharedData_refCount(const XSharedData*);` | `Src/XData/XSharedData/XSharedData.h:32,40,46,65` |

---

## 七、日志/调试设施（已存在，登记备替换 printf 调试输出）

库内**有**日志设施，三件套：

1. **XPrintf**（`Src/XCode/XPrintf/XPrintf.h`）——stdout 打印：`int XPrintf(const char* format, ...);`（:67）、`int XPrintf_line(const char* prefix, const char* format, ...);`（:69）、`int XPrintf_2(const XString*); XPrintf_3(const char*); XPrintf_4(const XByteArray*); XPrintf_5(XChar*);`（:75-87）。支持输出重定向栈 `XPrintf_outputPush/XPrintf_outputPop`（:49,57）。
2. **XDebug**（`Src/XCode/XDebug/XDebug.h`）——带位置/类型的调试流：`XDebug* XDebug_create_with_location_(const char* file, const char* function, int line);`（:39）、`XDebug* XDebug_printf_(XDebug*, const char* format, ...);`（:60）、类型化输出 `XDebug_int64_/double_/ptr_/hex64_` 等（:66-81）；流式宏 `XDebug_start_stream … XDebug_end_stream`（:102-140，仅 `DEBUG_ON || _DEBUG` 生效，:99）。
3. **XInfo**（`Src/XCode/XDebug/XInfo.h`）——自动携带 `__FILE__/__FUNCTION__/__LINE__` 的信息日志流宏 `XInfo_start_stream … XInfo_end_stream`。

**结论**：printf/puts 调试输出可替换为 `XPrintf`；`fprintf(stderr,…)` 风格诊断可替换为 XDebug/XInfo 流。重定向行为差异见 deferred-4。

---

## 八、其他平台功能（顺带登记）

| 标准函数 | 库内 API | 头文件:行 |
|---|---|---|
| `getenv` | `const char* XSystem_environment(const char* name);`（只读查询）；`bool XSystem_hasEnvironment(const char* name);` | `Src/XPlatform/XSystem.h:161,169` |
| `getpid` | `int64_t XSystem_pid(void);` | `Src/XPlatform/XSystem.h:178` |
| `rand`/`srand` | `XRandomGenerator`：`XRandomGenerator* XRandomGenerator_create(void);`、`void XRandomGenerator_seed(XRandomGenerator*, uint32_t);`、`uint32_t XRandomGenerator_generate(XRandomGenerator*);`、`double XRandomGenerator_generateDouble(...)`、`uint32_t XRandomGenerator_boundedU32(XRandomGenerator*, uint32_t highest);` | `Src/XCode/XRandom/XRandomGenerator.h:43,93,111,127,171` |
| `setitimer`/定时回调 | `XTimer`：`XTimer* XTimer_create_ex(XMemoryType);`、`void XTimer_setTimeout(XTimer*, size_t);`、`void XTimer_callOnTimeout1/2(...)`、`void XTimer_singleShot1/2(...)` | `Src/XTimer/XTimer.h:42,71,176,192` |
| `abort`/`exit` | **无库内等价**。`XSystem_shutdown/reset/reboot` 是整机平台语义（`XSystem.h:129-150`），非进程退出，禁替换 | — |

---

## 九、白名单类目（沿用任务口径，保留不动）

- 头文件类：`stdbool.h`、`stdint.h`、`stddef.h`、`limits.h`、`float.h`、`inttypes.h`、`stdarg.h`、`assert.h`（语言基础设施，无库内等价）。
- `string.h` 的 `memcpy`/`memset`/`memmove`/`memcmp`（裸内存原语）。备注：库内 `Src/XMemory/XMemory.h:252-277` 另有 `void* XMemcpy(void*, const void*, size_t);`、`XMemset`、`XMemmove`、`int XMemcmp(...)`，本轮按白名单**不**替换。
- `math.h`（库内无数学模块）。
- 本轮无日志设施则 printf 白名单——已失效：第七章证实有设施，printf 调试输出**不在**白名单内，可按上述映射替换。

---

## 十、Deferred（语义不完全等价/有行为风险，本轮一律不动）

1. **`clock()` / 单调时长测量——已解决（2026-09-27）**：`XDateTime_currentMSecsSinceEpoch` 已切换单调实现（posix=`CLOCK_MONOTONIC`，win32=`GetTickCount64()`），时长/超时/限流场景统一用它；`Src/XGui/Widget/XFileDialog.c` 的 `xff_nowMs` 与 `Src/XGui/Widget/XWidget.c` present 限频的 `clock()` 直呼已迁移。**Src 全域禁直呼 clock/clock_gettime/GetTickCount 等时间源。**
2. **`strtok`——已解决（2026-09-27 新增 `XStrtokReentrant`）**：`split_utf8` 分隔符是字面序列非任意字符集，确实不适用；改为把 XCanDbcFileParser 已验证的文件级 `xStrtok`（可重入 savePtr 版）提升为公共 API `XStringUtils_strtokReentrant`（`Src/XCode/XAlgorithm/XStringUtils.h`，无静态状态线程安全，语义与 strtok 对齐：跳前导分隔符/不产空 token/写 '\0'）。已迁移三处：`XFileDialog.c` 过滤串（" ;,"）、`XHostAddress.c` host:port（":"）、`XNetworkProxyHandshake.c` bypass 列表（",;"）。回归契约：`test_xstrtok_reentrant_contract`。
3. **裸 `char*` 数值转换**：`atoi/strtol/strtod` 直接作用于 `char*`；库内 `XString_toInt`/`XChar_toInt` 需先构造 `XString`/`XChar` 数组（涉及 UTF-8 解码与对象生命周期），逐点评估后才可替换。
4. **`printf` 直出 → `XPrintf`——已解决（2026-09-27 用户裁定，全量替换）**：`Src/` 内 26 处 bare `printf`（19 个 TU：容器族/XData/IO 设备/XDebug 等）已全部改为 `XPrintf`，接受输出重定向栈语义（`XPrintf_outputPush`，`XPrintf.h:49`）。`fprintf(stderr,…)` 3 处与 `snprintf` 缓冲惯用法不在本条范围（见各自条目）。例外：`XPrintf.c` 内部底层写出保持 `fwrite(stdout)`（递归防护，白名单）。
5. **`remove` 删目录——已解决（2026-09-27 新增 `XDir_removePath_static`）**：`Src/XCode/XFile/XDir/XDir.h` 提供按路径类型分流删除（文件→`XDeviceFile_removePermanent` 永久删除；目录→`XDeviceFile_rmdir(path,false)` 仅空目录，非空目录失败，与 C `remove` 语义对齐；不存在→false）。嵌入式适配：Src 禁用 C 标准库 `remove`，删除路径统一走本 API；递归删目录仍用 `XDir_removeRecursively`。回归契约：`xgui_regression_test.c` `test_xdir_remove_path_contract`（文件/空目录/非空目录/不存在/NULL 五分支）。
6. **`tmpfile`/`tmpnam`——已解决（2026-09-27 新增匿名临时文件族）**：`Src/XCode/XFile/XSaveFile/XSaveFile.h` 新增——`XSaveFile_uniqueTempPath_static(prefix)`（tmpnam 等价：临时目录+前缀+唯一后缀，探测不存在）、`XSaveFile_openUniqueTemp(file, prefix)`（tmpfile 等价：独占创建打开，deinit 自动删除=用后即焚）、`XSaveFile_setTempDir_static/tempDir_static`（模块临时目录，未设置回退 TMPDIR 再退当前目录；嵌入式显式指向 scratch 挂载点）。注意与 `XSaveFile` 既有"目标同目录+commit 原子改名"语义并存不混用。回归契约：`xgui_regression_test.c` `test_xsavefile_temp_contract`。
7. **`fwrite`/`fread` 到 `FILE*` 的场景——已解决（2026-09-27 整文件粒度切换）**：`Src/XProtocol/XCan/XCanDbcFileParser.c` `parseFileInternal` 的 fopen/fseek/ftell/fread/fclose 五件套已整体切换为 `XFile`（`XFile_init_2/open_2/size_base/read_1/close_base/deinit`，XIODevice 缓冲与事务语义），Src 此后无 `FILE*` 流。例外白名单：`XPrintf.c`/`XAbstractEventDispatcher.c` 的 `fwrite(stdout)` 属控制台输出原语本体（递归防护注释在案），保留。
