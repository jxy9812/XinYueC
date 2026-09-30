/**
 * @file       XPinyinPhrase.c
 * @brief      全键拼音 IME 外挂词组库实现（双格式加载 + 二分查询）。
 * @details     与公开头 XPinyinPhrase.h 的公共 API 一一对应；实现
 *             要点：
 *             - 加载链：imePhraseReadFile 整文件读入 blob 后按文件头
 *               魔数自动识别格式——"XIPB" 走 V3 二进制快路径
 *               imePhraseLoadBinary（头/版本/音节表条数与指纹/CRC/体积
 *               恒等式/逐条 walk 复检，全部通过才落位常驻态，任一步
 *               失败一条诊断+整体拒绝落负缓存，绝不降级读同名 txt）；
 *               其他内容走 V2 文本路径：剥 UTF-8 BOM → 原地切行（不用
 *               任何 readLine 重载——XIODevice_readLine_2/_3 实为
 *               read_1 全量读的损坏规避 + CRLF 污染规避双因）→ 逐行六
 *               条校验（口径见头文件 @details）→ 词组字段 '\t' 原地改
 *               写 '\0' 使条目 m_utf8 恒为 NUL 结尾 → (音节 id 序列,
 *               组内频序) 稳定插入排序（实测资产已序 0 违例故 O(n)，
 *               容忍手改乱序文件）；
 *             - 查询链（XPinyinTable_find 同型）：id 序列下界二
 *               分得组首 → 顺序扫组尾 → XKEYBOARD_IME_PHRASE_LIMIT 组
 *               内 top-N 裁剪 → 零拷贝区间；
 *             - 路径解析三段（XFont 外挂资产先例）：默认宏/setPath 覆
 *               盖 → open 失败且 ../ 起头剥前缀重试（仓库 bin/ 直跑口
 *               径，CTest WORKING_DIRECTORY=bin 对齐）→ 仍失败且相对
 *               路径时以 XCoreApplication_applicationDirPath 拼 exe 目
 *               录兜底；
 *             - 生命周期：进程期常驻（loaded 标志含负结果粘滞，绝不逐
 *               键重探——候选刷新每击键执行）；unload/reload 为手动口，
 *               调用前置=无任何借用区间（头文件 @warning 契约）。
 * @note       本文件是 XPinyinEngine 家族唯一含文件 IO 的 TU（受
 *             XKEYBOARD_IME_PHRASE_ON=XKEYBOARD_IME_ON&&XFILE_ON 门控，
 *             XFILE_ON=0 时 #error 兜底）；XPinyinTable/XVirtualKeyboard
 *             Ime 的无库外依赖注记不受影响。
 * @author     XinYueC 团队
 ******************************************************************************/
#include "XPinyinPhrase.h"

#if XKEYBOARD_IME_PHRASE_ON

#include "XFileSystem_config.h" /* XFILE_ON 定义源（词组硬依赖文件系统）。 */

#if !XFILE_ON
#error "XKEYBOARD_IME_PHRASE_ON=1 requires XFILE_ON=1 (expression macro should have forced 0)"
#endif

#include <stdint.h>
#include <stdbool.h>
#include <string.h> /* 仅白名单裸内存原语 memcpy（词串区恰量拷贝）。 */
#include "XFile.h"
#include "XByteArray.h"
#include "XString.h"
#include "XCoreApplication.h"
#include "XMemory.h"
#include "XStringUtils.h"
#include "XPrintf.h"
#include "XCrc.h" /* CRC-32/ISO-HDLC（XImageCodecPng.c 同款先例）。 */
#include "XPinyinTable.h"

/* ==================== 内部常量 ==================== */

/** @brief 词组键音节数下限（词组>=2 字；1 音节行拒收口径②）。 */
#define IME_PHRASE_SYLLABLE_MIN 2
/** @brief 词组键音节数上限（条目 m_syllable[4] 容量）。 */
#define IME_PHRASE_SYLLABLE_MAX 4
/** @brief 单个音节最大字母数（zhuang/chuang/shuang 6 字母 + NUL）。 */
#define IME_PHRASE_SYL_BUF 7
/** @brief 条目数组初始容量（首括后按 2 倍增长）。 */
#define IME_PHRASE_ENTRIES_INITIAL 1024

/* ==================== 内部状态（进程期常驻缓存） ==================== */

/** @brief setPath 覆盖路径（空串=未覆盖，用 XKEYBOARD_IME_PHRASE_PATH）。 */
static char g_phrasePath[XKEYBOARD_IME_PHRASE_PATH_MAX];
/** @brief 词库文本 blob（条目 m_utf8 全部指入；unload 释放）。 */
static char* g_phraseBlob = NULL;
/** @brief 词库条目堆数组（排序后；unload 释放）。 */
static XPinyinPhraseEntry* g_phraseEntries = NULL;
/** @brief 词条总数。 */
static int32_t g_phraseCount = 0;
/** @brief 已加载标志（含负结果粘滞：失败亦置 true 不重探）。 */
static bool g_phraseLoaded = false;
/** @brief 加载成功且有词条。 */
static bool g_phraseReady = false;

/* ==================== 内部函数：字节串工具 ==================== */

/**
 * @brief      内部条目音节 id 序列比较（字典序；白名单外不引 string.h）。
 * @param      a 比较条目借用指针；不为 NULL。
 * @param      b 比较条目借用指针；不为 NULL。
 * @return     a<b 返回负数；相等（含同序列同频序）返回 0；a>b 返回正数。
 */
static int imePhraseKeyCompare(const XPinyinPhraseEntry* a,
                               const XPinyinPhraseEntry* b)
{
    int n = (a->m_syllableCount < b->m_syllableCount)
                ? a->m_syllableCount
                : b->m_syllableCount;
    int i;
    for (i = 0; i < n; ++i)
    {
        if (a->m_syllable[i] != b->m_syllable[i])
            return (int)a->m_syllable[i] - (int)b->m_syllable[i];
    }
    /* 公共前缀相等：短序列排前（字典序口径）。 */
    if (a->m_syllableCount != b->m_syllableCount)
        return (int)a->m_syllableCount - (int)b->m_syllableCount;
    return (int)a->m_rank - (int)b->m_rank;
}

/**
 * @brief      内部查询键与条目键比较（仅音节 id 序列，不含 rank）。
 * @param      ids 查询 id 序列借用指针；不为 NULL。
 * @param      count 序列长度；1..IME_PHRASE_SYLLABLE_MAX。
 * @param      e 条目借用指针；不为 NULL。
 * @return     键<条目 返回负数；序列相同返回 0；键>条目 返回正数。
 */
static int imePhraseKeyCompareIds(const uint16_t* ids, int32_t count,
                                  const XPinyinPhraseEntry* e)
{
    int32_t i;
    for (i = 0; i < count; ++i)
    {
        if (i >= (int32_t)e->m_syllableCount)
            return 1; /* 查询键比条目序列长：键更大。 */
        if (ids[i] != e->m_syllable[i])
            return (int)ids[i] - (int)e->m_syllable[i];
    }
    if (count < (int32_t)e->m_syllableCount)
        return -1; /* 查询键是条目序列前缀：键更小。 */
    return 0;
}

/**
 * @brief      内部 UTF-8 良构校验（加载校验口径⑥，结构级防御）。
 * @details    首字节 0xC2..0xF4 定长 2/3/4、续字节 0x80..0xBF 齐备；
 *             截断在序列中间/孤立续字节(0x80..0xBF)/非法首字节
 *             (0x80..0xC1、0xF5..0xFF)=坏行。ASCII(0x00..0x7F) 恒良构
 *             （口径③已另拒控制字节）。防『尾部字节均 >=0x20 通过长
 *             度校验的半字符』经 XPainter_drawTextRect 直绘出乱码。
 * @param      s 字节序列借用指针；不为 NULL。
 * @param      len 字节长度；>=0。
 * @return     良构返回 true；否则 false。
 */
static bool imePhraseUtf8WellFormed(const unsigned char* s, int32_t len)
{
    int32_t i = 0;
    while (i < len)
    {
        unsigned char c = s[i];
        int n;
        int j;
        if (c < 0x80)
        {
            ++i;
            continue;
        }
        if (c >= 0xC2 && c <= 0xDF)
            n = 2;
        else if (c >= 0xE0 && c <= 0xEF)
            n = 3;
        else if (c >= 0xF0 && c <= 0xF4)
            n = 4;
        else
            return false; /* 续字节/0xC0..0xC1 过长编码/0xF5.. 非法。 */
        if (i + n > len)
            return false; /* 序列中间截断。 */
        for (j = 1; j < n; ++j)
        {
            if (s[i + j] < 0x80 || s[i + j] > 0xBF)
                return false;
        }
        i += n;
    }
    return true;
}

/**
 * @brief      内部十进制解析（频序字段；仅接受 1..255 数字串，按行尾
 *             截断不读越界）。
 * @param      s 字段首借用指针；不为 NULL。
 * @param      end 字段结束位置（exclusive，行尾）；不为 NULL。
 * @param      outValue 调用方提供存储空间；成功时接收解析值。
 * @return     合法返回 true；空串/含非数字/越界 [1,255] 返回 false。
 */
static bool imePhraseParseRank(const char* s, const char* end, int* outValue)
{
    int value = 0;
    if (!s || !end || !outValue || s >= end)
        return false;
    while (s < end)
    {
        if (*s < '0' || *s > '9')
            return false;
        value = value * 10 + (*s - '0');
        if (value > 255)
            return false;
        ++s;
    }
    if (value < 1)
        return false;
    *outValue = value;
    return true;
}

/**
 * @brief      内部有界路径拷贝（不引 string.h 白名单外接口）。
 * @param      buf 目标缓冲借用指针；不为 NULL。
 * @param      cap 缓冲容量（含 NUL）。
 * @param      src 源串借用指针；不为 NULL。
 * @return     拷贝完整返回 true；超长截断返回 false（恒 NUL 结尾）。
 */
static bool imePhrasePathCopy(char* buf, int cap, const char* src)
{
    int i = 0;
    if (!buf || cap < 1 || !src)
        return false;
    while ((i < cap - 1) && (src[i] != '\0'))
    {
        buf[i] = src[i];
        ++i;
    }
    buf[i] = '\0';
    return src[i] == '\0';
}

/**
 * @brief      内部盘符/根路径判定（XFont_isAbsPath 先例同款；exe 目录
 *             兜底只对相对路径追加前缀）。
 * @param      path 路径借用指针；可为 NULL（NULL=相对）。
 * @return     绝对路径返回 true；否则 false。
 */
static bool imePhraseIsAbsPath(const char* path)
{
    if (!path || !path[0])
        return false;
    if (path[0] == '/' || path[0] == '\\')
        return true;
    return ((path[0] >= 'A' && path[0] <= 'Z') ||
            (path[0] >= 'a' && path[0] <= 'z')) && path[1] == ':';
}

/**
 * @brief      内部单次打开尝试（建路径串+建文件对象+只读打开）。
 * @param      path 文件路径借用指针；不为 NULL。
 * @param      outStr 调用方提供存储空间；成功时接收 XString 堆对象
 *             （所有权随成功转出，调用方负责 XClass_delete_base）。
 * @param      outFile 调用方提供存储空间；成功时接收已打开 XFile 堆
 *             对象（同上）。
 * @return     打开成功返回 true；任一步失败返回 false（内部清理）。
 */
static bool imePhraseTryOpen(const char* path, XString** outStr,
                             XFile** outFile)
{
    XString* str;
    XFile* file;
    if (!path || !path[0] || !outStr || !outFile)
        return false;
    str = XString_create_utf8(path);
    file = str ? XFile_create() : NULL;
    if (!str || !file)
    {
        if (file) XClass_delete_base((XClass*)file);
        if (str) XClass_delete_base((XClass*)str);
        return false;
    }
    XFile_setFileName(file, str);
    if (!XFile_open_2(file, XIODevice_ReadOnly, 0))
    {
        XClass_delete_base((XClass*)file);
        XClass_delete_base((XClass*)str);
        return false;
    }
    *outStr = str;
    *outFile = file;
    return true;
}

/**
 * @brief      整文件读入堆缓冲（XFont_readFileBytes 先例同款三段路径）。
 * @details    候选路径依次：①原路径；②open 失败且 ../ 起头剥前缀重试
 *             （仓库 bin/ 直跑口径）；③仍失败且相对路径时以
 *             XCoreApplication_applicationDirPath 拼 exe 目录兜底（安
 *             装后部署布局）。成功时 *outBlob=XMalloc_System 堆缓冲
 *             （恰好 raw 字节）、*outLen=字节数，释放责任归调用方
 *             （XFree_System）。
 * @param      path 文件路径借用指针；不为 NULL。
 * @param      outBlob 调用方提供存储空间；成功时接收堆缓冲借用指针。
 * @param      outLen 调用方提供存储空间；成功时接收文件字节数。
 * @return     成功返回 true；缺文件/打开失败/读失败/OOM 返回 false
 *             （*outBlob 置 NULL、*outLen 置 0）。
 */
static bool imePhraseReadFile(const char* path, char** outBlob, int32_t* outLen)
{
    XString* str = NULL;
    XFile* file = NULL;
    XByteArray* bytes = NULL;
    char* blob = NULL;
    int32_t raw = 0;
    if (!path || !path[0] || !outBlob || !outLen)
        return false;
    *outBlob = NULL;
    *outLen = 0;
    /* ①原路径。 */
    if (!imePhraseTryOpen(path, &str, &file))
    {
        /* ②../ 剥前缀重试（XFont.c:343-357 同款）。 */
        if (path[0] == '.' && path[1] == '.' &&
            (path[2] == '/' || path[2] == '\\'))
        {
            imePhraseTryOpen(path + 3, &str, &file);
        }
        /* ③exe 目录兜底（XFont.c:620-634 同款；仅相对路径）。 */
        if (!file && !imePhraseIsAbsPath(path))
        {
            const XString* dir = XCoreApplication_applicationDirPath();
            if (dir)
            {
                const char* utf8 = XString_toUtf8(dir);
                if (utf8 && utf8[0])
                {
                    char joined[XKEYBOARD_IME_PHRASE_PATH_MAX];
                    int n = 0;
                    int i = 0;
                    bool sep;
                    while (utf8[n] != '\0')
                        ++n;
                    sep = n > 0 && utf8[n - 1] != '/' && utf8[n - 1] != '\\';
                    if (n + (sep ? 1 : 0) < XKEYBOARD_IME_PHRASE_PATH_MAX)
                    {
                        for (i = 0; i < n; ++i)
                            joined[i] = utf8[i];
                        if (sep)
                            joined[i++] = '/';
                        imePhrasePathCopy(joined + i,
                                          XKEYBOARD_IME_PHRASE_PATH_MAX - i,
                                          path);
                        imePhraseTryOpen(joined, &str, &file);
                    }
                }
                XString_delete_base((XClass*)dir);
            }
        }
        if (!file)
            return false;
    }
    bytes = XIODevice_readAll_3((XIODevice*)file);
    XIODevice_close_base((XIODevice*)file);
    if (!bytes)
        goto failed;
    raw = (int32_t)XByteArray_size_base((XContainer*)bytes);
    /* blob 拷贝：条目 m_utf8 指入进程期常驻堆，XByteArray 随读随释。 */
    blob = raw > 0 ? (char*)XMalloc_System((size_t)raw) : NULL;
    if (!blob)
        goto failed;
    {
        int32_t i;
        const uint8_t* src = XByteArray_data(bytes);
        for (i = 0; i < raw; ++i)
            blob[i] = (char)src[i];
    }
    *outBlob = blob;
    *outLen = raw;
    XClass_delete_base((XClass*)bytes);
    XClass_delete_base((XClass*)file);
    XClass_delete_base((XClass*)str);
    return true;
failed:
    if (blob) XFree_System(blob);
    if (bytes) XClass_delete_base((XClass*)bytes);
    if (file) XClass_delete_base((XClass*)file);
    if (str) XClass_delete_base((XClass*)str);
    return false;
}

/* ==================== 内部函数：行解析 ==================== */

/**
 * @brief      解析单行的音节列为 id 序列（加载校验口径①②）。
 * @param      line 音节列字段首借用指针；不为 NULL。
 * @param      end 字段结束位置（exclusive，第一个 TAB 处）。
 * @param      outIds 调用方提供存储空间；成功时接收 id 序列。
 * @param      outCount 调用方提供存储空间；成功时接收音节数。
 * @return     合法（2..4 个合法音节、单空格分隔、无空音节）返回 true；
 *             否则 false。
 */
static bool imePhraseParseSyllables(const char* line, const char* end,
                                    uint16_t* outIds, uint8_t* outCount)
{
    const char* p = line;
    uint8_t count = 0;
    char token[IME_PHRASE_SYL_BUF];
    while (p < end)
    {
        int t = 0;
        uint16_t id = 0;
        while (p < end && *p != ' ')
        {
            if (t >= IME_PHRASE_SYL_BUF - 1)
                return false; /* 音节超长（>6 字母）。 */
            token[t++] = *p;
            ++p;
        }
        token[t] = '\0';
        if (t == 0)
            return false; /* 空音节（连续空格/首尾空格/空字段）。 */
        if (!XPinyinTable_syllableIdOf(token, &id))
            return false; /* 口径①：非法音节。 */
        if (count >= IME_PHRASE_SYLLABLE_MAX)
            return false; /* 口径②：超过 4 音节。 */
        outIds[count++] = id;
        if (p >= end)
            break;
        ++p; /* 跳过分隔空格（后续空音节自然拒收）。 */
    }
    if (count < IME_PHRASE_SYLLABLE_MIN)
        return false; /* 口径②：1 音节行拒收（锁死单音节键不侵入 V1）。 */
    *outCount = count;
    return true;
}

/**
 * @brief      解析一行并产出条目（坏行返回 false 由调用方计数）。
 * @param      line 行首借用指针；不为 NULL。
 * @param      end 行尾位置（exclusive，已剥 CR）；不为 NULL。
 * @param      blob blob 首地址（字段原地 NUL 化与指针换算用）；不为
 *             NULL。
 * @param      outEntry 调用方提供存储空间；成功时接收新条目（m_utf8
 *             指入 blob）。
 * @return     合法行返回 true；坏行返回 false。
 */
static bool imePhraseParseLine(const char* line, const char* end,
                               char* blob,
                               XPinyinPhraseEntry* outEntry)
{
    const char* tab1 = line;
    const char* tab2;
    const char* word;
    const char* rankField;
    uint16_t ids[IME_PHRASE_SYLLABLE_MAX];
    uint8_t sylCount = 0;
    int32_t wordLen;
    int rank = 0;
    int32_t i;
    /* 字段 1：音节列（第一个 TAB 止）。 */
    while (tab1 < end && *tab1 != '\t')
        ++tab1;
    if (tab1 >= end)
        return false; /* 恰 1 个字段：口径⑤字段数错误。 */
    if (!imePhraseParseSyllables(line, tab1, ids, &sylCount))
        return false;
    /* 字段 2：词组 UTF-8（第二个 TAB 止）。 */
    word = tab1 + 1;
    tab2 = word;
    while (tab2 < end && *tab2 != '\t')
        ++tab2;
    if (tab2 >= end)
        return false; /* 只有 2 个字段：口径⑤。 */
    wordLen = (int32_t)(tab2 - word);
    /* 口径③：1..15 字节、非空、无 <0x20 字节（TAB 是分隔符不可能出
       现在 字段内；其余控制字节拒收）。 */
    if (wordLen < 1 || wordLen > XKEYBOARD_IME_PHRASE_UTF8_MAX)
        return false;
    for (i = 0; i < wordLen; ++i)
    {
        if ((unsigned char)word[i] < 0x20)
            return false;
    }
    /* 口径⑥：良构 UTF-8 结构校验（防半字符直绘乱码）。 */
    if (!imePhraseUtf8WellFormed((const unsigned char*)word, wordLen))
        return false;
    /* 字段 3：组内频序（行尾止）。 */
    rankField = tab2 + 1;
    if (!imePhraseParseRank(rankField, end, &rank))
        return false; /* 口径④：1..255。 */
    /* 词组字段原地 NUL 结尾（第二个 TAB 改写；blob 可变，此后条目
       m_utf8 恒为 C 串，供 XStrcmp/直绘）。 */
    blob[tab2 - blob] = '\0';
    outEntry->m_syllable[0] = ids[0];
    outEntry->m_syllable[1] = ids[1];
    outEntry->m_syllable[2] = sylCount > 2 ? ids[2] : 0;
    outEntry->m_syllable[3] = sylCount > 3 ? ids[3] : 0;
    outEntry->m_syllableCount = sylCount;
    outEntry->m_utf8 = blob + (word - blob);
    outEntry->m_rank = (uint8_t)rank;
    return true;
}

/**
 * @brief      稳定插入排序（(id 序列字典序, 组内频序) 键）。
 * @details    实测资产已序 0 违例故 O(n)；容忍手改乱序文件（稳定排序
 *             保同键内频序相对次序）。
 * @param      entries 条目数组借用指针；不为 NULL。
 * @param      count 条目数；>=0。
 * @return     无。
 */
static void imePhraseSort(XPinyinPhraseEntry* entries, int32_t count)
{
    int32_t i;
    for (i = 1; i < count; ++i)
    {
        XPinyinPhraseEntry key = entries[i];
        int32_t j = i - 1;
        while (j >= 0 && imePhraseKeyCompare(&entries[j], &key) > 0)
        {
            entries[j + 1] = entries[j];
            --j;
        }
        entries[j + 1] = key;
    }
}

/* ==================== 内部函数：XIPB 二进制快路径 ==================== */

/* XIPB 布局常量（全小端；编译器 Tools/VirtualKeyboard/ime_phrases_compile.py 同构
 * 产出；头 24B + 条目 16B 定长，全文件经逐字节访问器拼装、不假设宿主
 * 端序——XFontOutline_Xfo.c 的 xfo16/xfo32 先例，win32 与大端 MCU 双端
 * 成立）。 */

/** @brief 文件头字节数（magic4+u16+u16+u32*4）。 */
#define XIPB_HEADER_SIZE 24
/** @brief 条目定长字节数（u16*4+u8+u8+保留 u8*2+u32）。 */
#define XIPB_ENTRY_SIZE 16
/** @brief 文件头内字段偏移。 */
#define XIPB_OFF_VERSION 4    /* u16 格式版本（当前 1）。 */
#define XIPB_OFF_SYL_COUNT 6  /* u16 音节表条数（编译期白名单）。 */
#define XIPB_OFF_ENTRY_COUNT 8  /* u32 条目数。 */
#define XIPB_OFF_WORDS_LEN 12   /* u32 词串区字节数。 */
#define XIPB_OFF_SYL_FP 16      /* u32 音节表指纹（FNV-1a64 低 32 位）。 */
#define XIPB_OFF_CRC 20         /* u32 CRC-32/ISO-HDLC（条目区+词串区）。 */
/** @brief 条目内字段偏移。 */
#define XIPB_EOFF_COUNT 8   /* u8 有效音节数（2..4）。 */
#define XIPB_EOFF_RANK 9    /* u8 组内频序（1..255）。 */
#define XIPB_EOFF_RESERVED 10 /* u8*2 保留（写 0、载入校验=0）。 */
#define XIPB_EOFF_WORD_OFFSET 12 /* u32 词首字节在词串区内偏移。 */

/** @brief 词组字段 UTF-8 上限的 bin 侧常量（与公开宏同值，条目 walk
 *         用；脱离 XKEYBOARD_IME_PHRASE_UTF8_MAX 可覆盖点独立取值时以
 *         公开宏为准，此处仅为可读性别名）。 */
#define XIPB_WORD_MAX XKEYBOARD_IME_PHRASE_UTF8_MAX

/**
 * @brief      逐字节读取小端 u16（不假设宿主端序）。
 * @param      p 字节序列借用指针；p[0..1] 可解引用。
 * @return     小端拼装出的 16 位无符号值。
 */
static uint16_t imePhraseBin16(const unsigned char* p)
{
    return (uint16_t)((uint16_t)p[0] | ((uint16_t)p[1] << 8));
}

/**
 * @brief      逐字节读取小端 u32（不假设宿主端序）。
 * @param      p 字节序列借用指针；p[0..3] 可解引用。
 * @return     小端拼装出的 32 位无符号值。
 */
static uint32_t imePhraseBin32(const unsigned char* p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

/**
 * @brief      现算音节表指纹（FNV-1a 64 位取低 32 位）。
 * @details    输入=按 id 序（XPinyinTable_syllableAt(0..n-1)）的全
 *             部音节 ASCII 串，音节间以单个 0x00 连接、无尾 NUL；与编
 *             译器 Tools/VirtualKeyboard/ime_phrases_compile.py 的
 *             syllable_fingerprint 同式。防『同条数重排』（条数校验的
 *             盲区），与 syllableCount 相等校验互为双保险。
 * @param      无。
 * @return     32 位指纹值。
 */
static uint32_t imePhraseSyllableFingerprint(void)
{
    uint64_t h = 0xCBF29CE484222325ull; /* FNV-1a 64 位 offset 基数。 */
    const uint64_t fnvPrime = 0x100000001B3ull; /* FNV-1a 64 位素数。 */
    uint16_t n = XPinyinTable_syllableCount();
    uint16_t i;
    for (i = 0; i < n; ++i)
    {
        const char* syl = XPinyinTable_syllableAt(i);
        if (!syl)
            return 0; /* 表内空项：指纹失真，load 侧比对必失败。 */
        if (i > 0)
        {
            h ^= 0u; /* 音节间 0x00 分隔（无尾 NUL）。 */
            h *= fnvPrime;
        }
        while (*syl)
        {
            h ^= (uint64_t)(unsigned char)*syl;
            h *= fnvPrime;
            ++syl;
        }
    }
    return (uint32_t)h; /* 取低 32 位。 */
}

/**
 * @brief      XIPB 二进制快路径加载（magic 匹配后由加载入口调用）。
 * @details    顺序校验，任一步失败→一条诊断+释放 blob+返回 false（由
 *             load 落负缓存粘滞）：①raw>=24；②magic=="XIPB"；③
 *             version==1；④头音节条数==XPinyinTable_syllableCount()
 *             且音节指纹==现算值（防音节表增删与同条数重排）；⑤体积上
 *             界 24+count*16<=raw（size_t 中间量防溢出）；⑤′体积恒等
 *             式 raw==24+count*16+wordsLen（wordsLen 完全交叉验证，防
 *             词串区越读）；⑥CRC-32/ISO-HDLC（条目区+词串区一次算）；
 *             ⑦零解析建索引：条目数组+瘦身词串 blob 两次分配、文件镜
 *             像按 wordsLen 恰量 memcpy 后即释放；⑧O(n) 逐条 walk 复
 *             检六规则的 bin 等价物（count 2..4、rank>=1、保留位=0、
 *             id 越界、wordOffset 严格递增且 <wordsLen、词长 1..15、
 *             词字节 >=0x20、词 UTF-8 良构、(id 序列,rank) 键非降），
 *             通过才做指针翻译 m_utf8=slim+off、m_syllable[i]=id[i]；
 *             ⑨落位 g_phraseBlob/g_phraseEntries/g_phraseCount。成功后
 *             g_phraseBlob 指向瘦身词串区（unload 兼容释放）。
 * @param      blob 整文件堆缓冲借用指针（本函数取得所有权：成功时释放
 *             文件镜像、保留 slim 词串区；失败时全部释放）。
 * @param      raw 文件字节数；>=24。
 * @return     加载并落位成功返回 true；任一校验失败返回 false。
 */
static bool imePhraseLoadBinary(char* blob, int32_t raw)
{
    const unsigned char* u = (const unsigned char*)blob;
    uint16_t version;
    uint16_t headerSylCount;
    uint32_t entryCount;
    uint32_t wordsLen;
    uint32_t headerFp;
    uint32_t headerCrc;
    uint32_t sylCount;
    uint32_t crcExpect;
    const unsigned char* entriesRegion;
    char* slim = NULL;
    XPinyinPhraseEntry* entries = NULL;
    uint32_t i;
    int32_t count;
    uint16_t prevIds[IME_PHRASE_SYLLABLE_MAX];
    uint8_t prevCount = 0;
    uint8_t prevRank = 0;
    bool firstEntry = true;
    if (!blob || raw < XIPB_HEADER_SIZE)
    {
        XFree_System(blob);
        return false;
    }
    /* ②magic（①′raw>=24 已由入口嗅探保证，这里保持自包含复核）。 */
    if (blob[0] != 'X' || blob[1] != 'I' || blob[2] != 'P' || blob[3] != 'B')
    {
        XFree_System(blob);
        return false;
    }
    /* ③版本（未来高版本一律拒绝）。 */
    version = imePhraseBin16(u + XIPB_OFF_VERSION);
    if (version != 1u)
    {
        XFree_System(blob);
        return false;
    }
    /* ④音节表条数+指纹双保险（防增删与同条数重排）。 */
    headerSylCount = imePhraseBin16(u + XIPB_OFF_SYL_COUNT);
    sylCount = XPinyinTable_syllableCount();
    if ((uint32_t)headerSylCount != sylCount)
    {
        XFree_System(blob);
        return false;
    }
    headerFp = imePhraseBin32(u + XIPB_OFF_SYL_FP);
    if (headerFp != imePhraseSyllableFingerprint())
    {
        XFree_System(blob);
        return false;
    }
    entryCount = imePhraseBin32(u + XIPB_OFF_ENTRY_COUNT);
    wordsLen = imePhraseBin32(u + XIPB_OFF_WORDS_LEN);
    /* ⑤体积上界（除法式先验：entryCount 是文件内 u32，32 位 size_t
       下 (size_t)entryCount*16 会回绕——评审发现的越界读路径，改用
       每条目 16B 的除法上界在乘法发生前钳位，双端无溢出）。 */
    if ((size_t)raw < XIPB_HEADER_SIZE ||
        entryCount > (uint32_t)(((size_t)raw - XIPB_HEADER_SIZE) /
                                XIPB_ENTRY_SIZE))
    {
        XFree_System(blob);
        return false;
    }
    /* ⑤′体积恒等式（wordsLen 完全交叉验证，步⑦恰量 memcpy 不越尾）。 */
    if ((size_t)XIPB_HEADER_SIZE + (size_t)entryCount * XIPB_ENTRY_SIZE +
            (size_t)wordsLen != (size_t)raw)
    {
        XFree_System(blob);
        return false;
    }
    /* ⑥CRC-32/ISO-HDLC（条目区+词串区相邻一次算）。 */
    headerCrc = imePhraseBin32(u + XIPB_OFF_CRC);
    crcExpect = XCrc32_calculate(
        XCrc32_Algorithm_IsoHdlc,
        u + XIPB_HEADER_SIZE,
        (size_t)raw - XIPB_HEADER_SIZE);
    if (headerCrc != crcExpect)
    {
        XFree_System(blob);
        return false;
    }
    if (entryCount < 1u)
    {
        XFree_System(blob);
        return false; /* 空库与文本路径 count<1 同判负缓存。 */
    }
    /* ⑦零解析建索引：条目数组+瘦身词串 blob。文件镜像 blob 在 walk
       期间必须保持存活（条目区复检直接读 blob；此前把释放放在 walk
       之前是 use-after-free——大块释放后页回收即 AV，回归实证）。 */
    entries = (XPinyinPhraseEntry*)XMalloc_System(
        (size_t)entryCount * sizeof(XPinyinPhraseEntry));
    if (!entries)
    {
        XFree_System(blob);
        return false;
    }
    slim = (char*)XMalloc_System((size_t)wordsLen);
    if (!slim)
    {
        XFree_System(entries);
        XFree_System(blob);
        return false;
    }
    entriesRegion = u + XIPB_HEADER_SIZE;
    memcpy(slim, blob + XIPB_HEADER_SIZE + (size_t)entryCount * XIPB_ENTRY_SIZE,
           (size_t)wordsLen);
    /* ⑧O(n) 逐条 walk：六规则的 bin 等价物，无生成期信任假设。 */
    for (i = 0; i < entryCount; ++i)
    {
        const unsigned char* e = entriesRegion + (size_t)i * XIPB_ENTRY_SIZE;
        uint16_t ids[IME_PHRASE_SYLLABLE_MAX];
        uint32_t off;
        uint32_t wordLen;
        uint32_t j;
        XPinyinPhraseEntry* out = &entries[i];
        ids[0] = imePhraseBin16(e);
        ids[1] = imePhraseBin16(e + 2);
        ids[2] = imePhraseBin16(e + 4);
        ids[3] = imePhraseBin16(e + 6);
        count = (int32_t)e[XIPB_EOFF_COUNT];
        if (count < IME_PHRASE_SYLLABLE_MIN ||
            count > IME_PHRASE_SYLLABLE_MAX)
            goto corrupt;
        if (e[XIPB_EOFF_RANK] < 1u)
            goto corrupt; /* 口径④ bin 等价物。 */
        if (e[XIPB_EOFF_RESERVED] != 0u || e[XIPB_EOFF_RESERVED + 1] != 0u)
            goto corrupt; /* 保留位非 0：非本格式产物。 */
        for (j = 0; j < (uint32_t)count; ++j)
        {
            if (ids[j] >= sylCount)
                goto corrupt; /* 口径① bin 等价物：id 越界。 */
        }
        for (j = (uint32_t)count; j < IME_PHRASE_SYLLABLE_MAX; ++j)
        {
            if (ids[j] != 0u)
                goto corrupt; /* 补零位非 0：非本格式产物。 */
        }
        off = imePhraseBin32(e + XIPB_EOFF_WORD_OFFSET);
        if (off >= wordsLen)
            goto corrupt; /* 词首必须落在词串区内。 */
        /* 词长=下一偏移-本偏移-1（末条=区尾-1）；词 NUL 由下一偏移-1
           或区尾-1 承担，词间恰一个 0x00。 */
        if (i + 1u < entryCount)
        {
            const unsigned char* en =
                entriesRegion + (size_t)(i + 1u) * XIPB_ENTRY_SIZE;
            uint32_t next = imePhraseBin32(en + XIPB_EOFF_WORD_OFFSET);
            if (next <= off + 1u || next >= wordsLen)
                goto corrupt; /* 下一词偏移必须严格落区内（next-off-1
                                 才是合法词长）。 */
            wordLen = next - off - 1u;
        }
        else
        {
            if (off + 1u > wordsLen - 1u)
                goto corrupt; /* 末条：区尾 NUL 至少占 1 字节。 */
            wordLen = wordsLen - off - 1u;
        }
        if (wordLen < 1u || wordLen > XIPB_WORD_MAX)
            goto corrupt; /* 口径③a bin 等价物。 */
        /* 词间恰一个 NUL ⇒ 首词偏移恒 0、wordOffset 严格递增（next>
           off+1 已隐含）。 */
        if (i == 0u && off != 0u)
            goto corrupt;
        if (!firstEntry)
        {
            /* (id 序列,rank) 键非降（imePhraseKeyCompare 生成序不变量
               防御）。 */
            int cmp = 0;
            uint16_t n = (prevCount < (uint8_t)count) ? prevCount
                                                      : (uint8_t)count;
            uint16_t k;
            for (k = 0; k < n && cmp == 0; ++k)
            {
                if (prevIds[k] != ids[k])
                    cmp = (int)prevIds[k] - (int)ids[k];
            }
            if (cmp == 0 && prevCount != (uint8_t)count)
                cmp = (int)prevCount - (int)count;
            if (cmp == 0)
            {
                if ((int)prevRank > (int)e[XIPB_EOFF_RANK])
                    goto corrupt;
            }
            else if (cmp > 0)
            {
                goto corrupt;
            }
        }
        for (j = 0; j < wordLen; ++j)
        {
            if ((unsigned char)slim[off + j] < 0x20u)
                goto corrupt; /* 口径③b：控制字节。 */
        }
        if (!imePhraseUtf8WellFormed((const unsigned char*)slim + off,
                                     (int32_t)wordLen))
            goto corrupt; /* 口径⑥：防 CRC 合法的外来 bin 携带非良构词
                             直绘乱码。 */
        /* 词 NUL 终止校验：分隔字节（off+wordLen 处，即下一偏移-1 或
           区尾-1）必须为 0x00——缺此校验时 CRC 合法的手工 bin 可携带
           非 NUL 结尾的 m_utf8，strlen 式消费方（XStrcmp/textWidth）
           将越过 wordsLen 越界读（评审发现的契约违反路径）。 */
        if ((unsigned char)slim[off + wordLen] != 0x00u)
            goto corrupt;
        /* 全部校验通过才做指针翻译。 */
        out->m_utf8 = slim + off;
        out->m_syllable[0] = ids[0];
        out->m_syllable[1] = ids[1];
        out->m_syllable[2] = ids[2];
        out->m_syllable[3] = ids[3];
        out->m_syllableCount = (uint8_t)count;
        out->m_rank = e[XIPB_EOFF_RANK];
        prevIds[0] = ids[0];
        prevIds[1] = ids[1];
        prevIds[2] = ids[2];
        prevIds[3] = ids[3];
        prevCount = (uint8_t)count;
        prevRank = e[XIPB_EOFF_RANK];
        firstEntry = false;
        continue;
    corrupt:
        XPrintf("[XPinyinPhrase] XIPB 条目 %u 校验失败，整体拒绝"
                "（负结果粘滞）\n", (unsigned)i);
        XFree_System(entries);
        XFree_System(slim);
        XFree_System(blob); /* walk 全程 blob 存活；失败在此统一释放。 */
        return false;
    }
    XFree_System(blob); /* walk 全部通过：文件镜像退役，常驻态只留 slim。 */
    /* ⑨落位常驻态。 */
    g_phraseBlob = slim;
    g_phraseEntries = entries;
    g_phraseCount = (int32_t)entryCount;
    return true;
}

/**
 * @brief      实际加载（load 首次进入；负结果粘滞由调用方落标志）。
 * @param      无。
 * @return     无。
 */
static void imePhraseLoadInternal(void)
{
    char* blob = NULL;
    int32_t raw = 0;
    XPinyinPhraseEntry* entries = NULL;
    int32_t capacity = 0;
    int32_t count = 0;
    int32_t bad = 0;
    int32_t pos = 0;
    const char* path;
    path = g_phrasePath[0] != '\0' ? g_phrasePath : XKEYBOARD_IME_PHRASE_PATH;
    if (!imePhraseReadFile(path, &blob, &raw))
    {
        /* 负缓存：缺文件与解析全坏同型，绝不逐键重探。 */
        XPrintf("[XPinyinPhrase] 词库文件不可读（%s），词组候选回退"
                "单字（负结果粘滞，不再重试）\n",
                path);
        return;
    }
    /* 格式嗅探：XIPB 魔数走二进制快路径（校验失败整体拒绝+负缓存，
       绝不降级读同名 txt）；其余内容走原文本路径（BOM 剥离起，逐行
       不变）。快路径在成功与失败两分支都接管 blob 所有权。 */
    if (raw >= XIPB_HEADER_SIZE && blob[0] == 'X' && blob[1] == 'I' &&
        blob[2] == 'P' && blob[3] == 'B')
    {
        (void)imePhraseLoadBinary(blob, raw);
        return;
    }
    entries = (XPinyinPhraseEntry*)XMalloc_System(
        (size_t)IME_PHRASE_ENTRIES_INITIAL * sizeof(XPinyinPhraseEntry));
    if (!entries)
    {
        XFree_System(blob);
        XPrintf("[XPinyinPhrase] 条目数组分配失败，词组候选回退单字\n");
        return;
    }
    capacity = IME_PHRASE_ENTRIES_INITIAL;
    /* 剥 UTF-8 BOM（实测资产存在）。 */
    if (raw >= 3 && (unsigned char)blob[0] == 0xEF &&
        (unsigned char)blob[1] == 0xBB && (unsigned char)blob[2] == 0xBF)
        pos = 3;
    /* 原地切行：LF 分行，容忍行尾 CR 自剥（不用任何 readLine 重载）。 */
    while (pos < raw)
    {
        const char* line;
        const char* end;
        XPinyinPhraseEntry entry;
        line = blob + pos;
        while (pos < raw && blob[pos] != '\n')
            ++pos;
        end = blob + pos;
        if (end > line && *(end - 1) == '\r')
            --end; /* 行尾 CR 自剥。 */
        ++pos; /* 越过 LF（最后一行无 LF 时到 raw 同样安全）。 */
        if (end > line && line[0] != '#')
        {
            if (imePhraseParseLine(line, end, blob, &entry))
            {
                if (count >= capacity)
                {
                    XPinyinPhraseEntry* grown;
                    int32_t next = capacity * 2;
                    grown = (XPinyinPhraseEntry*)XRealloc_System(
                        entries,
                        (size_t)next * sizeof(XPinyinPhraseEntry));
                    if (!grown)
                    {
                        ++bad; /* OOM 行按坏行计（收尾汇总口径）。 */
                        continue;
                    }
                    entries = grown;
                    capacity = next;
                }
                entries[count++] = entry;
            }
            else
            {
                ++bad; /* 坏行：跳过+计数，不终止整文件。 */
            }
        }
    }
    if (count < 1)
    {
        /* 解析全坏：负缓存（ready=false 粘滞）。 */
        XPrintf("[XPinyinPhrase] 词库无可装词条（%s，坏行 %d），词组"
                "候选回退单字\n",
                path, (int)bad);
        XFree_System(entries);
        XFree_System(blob);
        return;
    }
    imePhraseSort(entries, count);
    if (bad > 0)
    {
        /* 收尾一条汇总诊断（逐行不刷屏）。 */
        XPrintf("[XPinyinPhrase] %s 装载 %d 条，跳过坏行 %d 条\n", path,
                (int)count, (int)bad);
    }
    g_phraseBlob = blob;
    g_phraseEntries = entries;
    g_phraseCount = count;
}

/* ==================== 生命周期 ==================== */

bool XPinyinPhrase_load(void)
{
    if (g_phraseLoaded)
        return g_phraseReady;
    imePhraseLoadInternal();
    g_phraseLoaded = true;
    g_phraseReady = (g_phraseEntries != NULL && g_phraseCount > 0);
    return g_phraseReady;
}

bool XPinyinPhrase_isReady(void)
{
    return g_phraseLoaded && g_phraseReady;
}

int32_t XPinyinPhrase_count(void)
{
    return g_phraseReady ? g_phraseCount : 0;
}

void XPinyinPhrase_unload(void)
{
    /* 契约：调用前置=无任何 XPinyinEngine 持有词组借用区间（先
       XPinyinEngine_resetComposition），错序是悬垂 UB（设计不做运行时
       追踪，见头文件 @warning）。 */
    if (g_phraseBlob)
    {
        XFree_System(g_phraseBlob);
        g_phraseBlob = NULL;
    }
    if (g_phraseEntries)
    {
        XFree_System(g_phraseEntries);
        g_phraseEntries = NULL;
    }
    g_phraseCount = 0;
    g_phraseLoaded = false;
    g_phraseReady = false;
}

bool XPinyinPhrase_reload(void)
{
    XPinyinPhrase_unload();
    return XPinyinPhrase_load();
}

void XPinyinPhrase_setPath(const char* path)
{
    if (!path)
    {
        g_phrasePath[0] = '\0'; /* NULL=回默认宏路径。 */
        return;
    }
    imePhrasePathCopy(g_phrasePath, XKEYBOARD_IME_PHRASE_PATH_MAX, path);
}

/* ==================== 查询 ==================== */

bool XPinyinPhrase_find(const uint16_t* syllableIds, int32_t count,
                             const XPinyinPhraseEntry** outBegin,
                             uint16_t* outCount)
{
    int32_t low;
    int32_t high;
    int32_t begin;
    int32_t end;
    if (outBegin)
        *outBegin = NULL;
    if (outCount)
        *outCount = 0;
    if (!syllableIds || !outBegin || !outCount)
        return false;
    if (count < 1 || count > IME_PHRASE_SYLLABLE_MAX)
        return false;
    if (!XPinyinPhrase_isReady())
        return false;
    /* 下界二分（首个键 >= 查询键的条目）。 */
    low = 0;
    high = g_phraseCount;
    while (low < high)
    {
        int32_t mid = (low + high) / 2;
        if (imePhraseKeyCompareIds(syllableIds, count,
                                   &g_phraseEntries[mid]) > 0)
            low = mid + 1;
        else
            high = mid;
    }
    /* 扫组尾（同键连续存放）。 */
    begin = low;
    end = begin;
    while (end < g_phraseCount &&
           imePhraseKeyCompareIds(syllableIds, count,
                                  &g_phraseEntries[end]) == 0)
        ++end;
#if (XKEYBOARD_IME_PHRASE_LIMIT > 0)
    {
        int32_t trimmed = 0;
        int32_t i;
        for (i = begin; i < end; ++i)
        {
            if (g_phraseEntries[i].m_rank >
                (uint8_t)XKEYBOARD_IME_PHRASE_LIMIT)
                break;
            ++trimmed;
        }
        end = begin + trimmed; /* 组内 m_rank 升序，截前 LIMIT 条。 */
    }
#endif
    if (end <= begin)
        return false;
    *outBegin = &g_phraseEntries[begin];
    *outCount = (uint16_t)(end - begin);
    return true;
}

#endif /* XKEYBOARD_IME_PHRASE_ON */
