/**
 * @file       XPinyinEngine.c
 * @brief      全键拼音 IME 组串状态机实现 + 拼音布局载体。
 * @details     与公开头 XPinyinEngine.h 的公共 API 一一对应；实现要点：
 *             - 状态机为纯逻辑（无绘制、无编辑框依赖、无动态内存）：
 *               组串不变式 INV2（V2）——m_buffer 恒为『完整音节序列前
 *               缀 + 至多一个未完音节前缀』，由 feedLetter 的接受/截断
 *               /清空三路径维持（接受判据=切分 DP：∃k∈[0,L]，前 k 字
 *               母可达恰切分且余部有音节前缀；k=0 项即 V1 原判据 INV1，
 *               接受集只增不减）；候选查询恒可安全调表；
 *             - 候选两段式零拷贝装配（不做跨表频度归一）：路径枚举
 *               （最长音节优先 DFS，上限 XKEYBOARD_IME_PATH_MAX=8 路）
 *               → 各路词组 XPinyinPhrase_find（词组段，受
 *               XKEYBOARD_IME_PHRASE_ON 门控；缺资产/关开关恒 miss）
 *               → 首路各音节 XPinyinTable_find（单字段，静态表，
 *               1989 条/408 音节，受 XKEYBOARD_IME_RANK_LIMIT 裁剪口
 *               径约束，相邻相同区间合并）→ 装配 m_regions 借用区间
 *               描述符（预算 XKEYBOARD_IME_REGION_MAX=8+15 由构造不
 *               可溢，代码设到达即停防御闸）。词组在前单字垫后，各段
 *               组内频序；
 *             - 布局载体：44 键 5 行拼音 QWERTY 静态借用表
 *               （s_imeMap/s_imeCtrl，全表无 POPOVER 位），经
 *               XPinyinEngine_map/XPinyinEngine_ctrlMap 取用，供
 *               XVirtualKeyboard_setImeEnabled 装入 User1 槽位（setMap 借用
 *               生命周期契约由静态存储满足）。
 *             - 九键（T9）数字通道：feedT9Digit 以数字组口径扩组串
 *               （2=abc..9=wxyz），接受判据 INV-T9 镜像 INV2（∃k：前
 *               k 位数字恰切为『逐位字母都落在对应数字组内』的音节序
 *               列且余部存在以其数字模式开头的音节；音节枚举经
 *               XPinyinTable_digitsSyllables 字典序窗口扫描——数字组
 *               首尾界串定窗、窗内逐位校验，即逐字母增量剪枝）；「分
 *               词」键 feedDigitSeparator 固化当前边界（首路读法音节
 *               序列入 m_t9FixedIds，其后数字另起新音节）；候选装配与
 *               字母通道同汇点 imeAssembleCandidates（词组+首路单字，
 *               与全键同源）；T9 态 composingText 返回数字串（键盘候
 *               选带直显），退格删末位数字、提交链/选候选/翻页全部
 *               复用。字母/数字双通道互斥：字母键压过数字通道切回全
 *               键组串，数字键在字母组串中冻结。
 * @note       本文件无库外依赖：字符串长度/比较用 XStringUtils 的
 *             XStrlen/XStrcmp（不引入 string.h 白名单外接口）；词组
 *             头 XPinyinPhrase.h 是库内头，仅按
 *             XKEYBOARD_IME_PHRASE_ON 引用。
 * @author     XinYueC 团队
 ******************************************************************************/
#include "XPinyinEngine.h"

#if XKEYBOARD_IME_ON

#include "XVirtualKeyboard.h"
#include "XStringUtils.h"
#if XKEYBOARD_IME_PHRASE_ON
#include "XPinyinPhrase.h"
#endif

/* ==================== 内部常量 ==================== */

/** @brief 页容量上限（候选带单页最多 9 个 chip，数字键 1..9 直选）。 */
#define IME_PAGE_SIZE_MAX 9
/** @brief 最长音节字母数（zhuang/chuang/shuang 6 字母；切分段长上限）。 */
#define IME_SYLLABLE_LEN_MAX 6
/** @brief 音节临时缓冲（6 字母 + NUL）。 */
#define IME_SYL_BUF (IME_SYLLABLE_LEN_MAX + 1)
/** @brief 控制键标志组合（NO_REPEAT|CLICK_TRIG|CHECKED；与键盘内置表
 *         XKB_FLAGS 同口径，本文件自持常量避免跨 TU 依赖私有宏）。 */
#define IME_CTRL_FLAGS ((int)XKEYBOARD_CTRL_BUTTON_FLAGS)

/* ==================== 拼音布局载体（44 键 5 行） ==================== */

/* 44 键 5 行全键拼音布局（<=XKEYBOARD_MAX_BUTTONS=64、行数<=上限）：
 * 行1：1..0（数字直选候选之外的普通数字键，可重复）；
 * 行2：q w e r t y u i o p；
 * 行3：a s d f g h j k l + 退格(宽2，裸控制字可重复，仿数字表退格口
 *      径——长按逐 tick 删组串字母，删空后透传编辑框退格)；
 * 行4：中/EN(控制键宽2) + z x c v b n m + 关闭(控制键宽1)；
 * 行5：收起(2) <- (2) 空格(6，可重复) -> (2) 换行(控制键宽2)。
 * 全表无 POPOVER 位（键盘顶行气泡带不预留，行高让给候选带）、无
 * abc/ABC/1#（英文大写/符号走既有内置布局或物理键盘，V1 不做中文标
 * 点映射）；行内单位总数依次 10/10/11/10/14。 */
static const char* const s_imeMap[] = {
    "1", "2", "3", "4", "5", "6", "7", "8", "9", "0", "\n",
    "q", "w", "e", "r", "t", "y", "u", "i", "o", "p", "\n",
    "a", "s", "d", "f", "g", "h", "j", "k", "l",
    XKEYBOARD_LBL_BACKSPACE, "\n",
    XKEYBOARD_LBL_IME, "z", "x", "c", "v", "b", "n", "m",
    XKEYBOARD_LBL_CLOSE, "\n",
    XKEYBOARD_LBL_DISMISS, XKEYBOARD_LBL_LEFT, " ", XKEYBOARD_LBL_RIGHT,
    XKEYBOARD_LBL_NEWLINE,
    NULL
};
static const XKeyboardButtonCtrl s_imeCtrl[] = {
    /* 行1：数字 1..0（裸宽度 1，可重复）。 */
    (XKeyboardButtonCtrl)1, (XKeyboardButtonCtrl)1, (XKeyboardButtonCtrl)1,
    (XKeyboardButtonCtrl)1, (XKeyboardButtonCtrl)1, (XKeyboardButtonCtrl)1,
    (XKeyboardButtonCtrl)1, (XKeyboardButtonCtrl)1, (XKeyboardButtonCtrl)1,
    (XKeyboardButtonCtrl)1,
    /* 行2：q w e r t y u i o p。 */
    (XKeyboardButtonCtrl)1, (XKeyboardButtonCtrl)1, (XKeyboardButtonCtrl)1,
    (XKeyboardButtonCtrl)1, (XKeyboardButtonCtrl)1, (XKeyboardButtonCtrl)1,
    (XKeyboardButtonCtrl)1, (XKeyboardButtonCtrl)1, (XKeyboardButtonCtrl)1,
    (XKeyboardButtonCtrl)1,
    /* 行3：a s d f g h j k l（裸 1）+ 退格（宽 2 裸控制字，可重复）。 */
    (XKeyboardButtonCtrl)1, (XKeyboardButtonCtrl)1, (XKeyboardButtonCtrl)1,
    (XKeyboardButtonCtrl)1, (XKeyboardButtonCtrl)1, (XKeyboardButtonCtrl)1,
    (XKeyboardButtonCtrl)1, (XKeyboardButtonCtrl)1, (XKeyboardButtonCtrl)1,
    (XKeyboardButtonCtrl)2,
    /* 行4：中/EN（控制键宽 2）+ z x c v b n m（裸 1）+ 关闭（控制键宽 1）。 */
    (XKeyboardButtonCtrl)(IME_CTRL_FLAGS | 2),
    (XKeyboardButtonCtrl)1, (XKeyboardButtonCtrl)1, (XKeyboardButtonCtrl)1,
    (XKeyboardButtonCtrl)1, (XKeyboardButtonCtrl)1, (XKeyboardButtonCtrl)1,
    (XKeyboardButtonCtrl)1,
    (XKeyboardButtonCtrl)(IME_CTRL_FLAGS | 1),
    /* 行5：收起 + <- + 空格（宽 6 可重复）+ -> + 换行。 */
    (XKeyboardButtonCtrl)(IME_CTRL_FLAGS | 2),
    (XKeyboardButtonCtrl)((int)XKEYBOARD_CTRL_CHECKED | 2),
    (XKeyboardButtonCtrl)6,
    (XKeyboardButtonCtrl)((int)XKEYBOARD_CTRL_CHECKED | 2),
    (XKeyboardButtonCtrl)(IME_CTRL_FLAGS | 2)
};

/* ==================== 内部函数：INV2 切分判定 ==================== */

/**
 * @brief      判断缓冲内一截字母段是否恰为完整合法音节。
 * @param      s 段首借用指针（不要求 NUL 结尾于段长处）；不为 NULL。
 * @param      len 段长；1..IME_SYLLABLE_LEN_MAX。
 * @return     是合法音节返回 true；否则 false。
 */
static bool imeIsSyllableSpan(const char* s, int len)
{
    char tmp[IME_SYL_BUF];
    int i;
    for (i = 0; i < len; ++i)
        tmp[i] = s[i];
    tmp[len] = '\0';
    return XPinyinTable_isLegalSyllable(tmp);
}

/**
 * @brief      INV2 接受判据（组串不变式唯一合法门）。
 * @details    ∃k∈[0,L]：reachable[k] ∧ hasSyllablePrefix(s[k..])。
 *             reachable[k]=s[0..k) 可恰好切成合法音节序列（reachable
 *             [0]=true；reachable[k]=∃j∈[max(0,k-6),k)：reachable[j]
 *             ∧ 段 s[j..k) 是合法音节，段长上限由最长音节天然约束）。
 *             k=0 项=V1 原判据（INV1），接受集只增不减。空串按 hasSyll
 *             ablePrefix("") 恒真处理（组串起点合法）。
 * @param      s 候选串借用指针（NUL 结尾）；不为 NULL。
 * @return     可接受返回 true；否则 false。
 */
static bool imeInv2Acceptable(const char* s)
{
    bool reachable[XKEYBOARD_IME_BUFFER_CAP + 1];
    int len = 0;
    int k;
    int j;
    while (s[len] != '\0')
        ++len;
    if (len > XKEYBOARD_IME_BUFFER_CAP)
        return false;
    reachable[0] = true;
    for (k = 1; k <= len; ++k)
        reachable[k] = false;
    for (k = 1; k <= len; ++k)
    {
        int low = k - IME_SYLLABLE_LEN_MAX;
        if (low < 0)
            low = 0;
        for (j = low; j < k; ++j)
        {
            if (reachable[j] && imeIsSyllableSpan(s + j, k - j))
            {
                reachable[k] = true;
                break;
            }
        }
    }
    for (k = 0; k <= len; ++k)
    {
        if (reachable[k] && XPinyinTable_hasSyllablePrefix(s + k))
            return true;
    }
    return false;
}

/* ==================== 内部函数：切分路径枚举 ==================== */

/**
 * @brief      切分路径集合（最长音节优先 DFS 的产出；首路=主读法）。
 * @details    存完整音节序列（深度<=组串容量 15）：词组查询键口径
 *             2..4 音节，超长序列由 XPinyinPhrase_find 的长度守
 *             卫恒 miss；单字装配使用首路完整序列（首路音节数<=15）。
 */
typedef struct XkbImePathSet
{
    uint16_t m_ids[XKEYBOARD_IME_PATH_MAX]
                  [XKEYBOARD_IME_BUFFER_CAP]; /**< 每路的音节 id 序列。 */
    uint8_t m_len[XKEYBOARD_IME_PATH_MAX];    /**< 每路音节数（完整）。 */
    int m_count;                              /**< 已收集路数（<=上限）。 */
} XkbImePathSet;

/**
 * @brief      路径枚举 DFS（最长音节优先；到组串尾记录一路）。
 * @param      set 路径集合指针；不为 NULL。
 * @param      buf 组串借用指针；不为 NULL。
 * @param      len 组串长度。
 * @param      pos 当前位置。
 * @param      acc 当前路径 id 累积缓冲（调用方栈上提供）。
 * @param      depth 当前路径深度。
 * @return     无（路数达上限即停止收集）。
 */
static void imeEnumPathsDfs(XkbImePathSet* set, const char* buf, int len,
                            int pos, uint16_t* acc, int depth)
{
    char tmp[IME_SYL_BUF];
    int maxLen;
    int l;
    if (set->m_count >= XKEYBOARD_IME_PATH_MAX)
        return;
    if (pos == len)
    {
        /* 记录完整一路（深度<=组串容量；词组查询键超 4 音节由
           XPinyinPhrase_find 长度守卫恒 miss，不影响装配）。 */
        int i;
        for (i = 0; i < depth; ++i)
            set->m_ids[set->m_count][i] = acc[i];
        set->m_len[set->m_count] = (uint8_t)depth;
        ++set->m_count;
        return;
    }
    maxLen = len - pos;
    if (maxLen > IME_SYLLABLE_LEN_MAX)
        maxLen = IME_SYLLABLE_LEN_MAX;
    for (l = maxLen; l >= 1; --l)
    {
        uint16_t id = 0;
        int i;
        for (i = 0; i < l; ++i)
            tmp[i] = buf[pos + i];
        tmp[l] = '\0';
        if (XPinyinTable_syllableIdOf(tmp, &id))
        {
            acc[depth] = id;
            imeEnumPathsDfs(set, buf, len, pos + l, acc, depth + 1);
            if (set->m_count >= XKEYBOARD_IME_PATH_MAX)
                return;
        }
    }
}

/* ==================== 内部函数：候选装配 ==================== */

#if XKEYBOARD_IME_PHRASE_ON

/**
 * @brief      词组借用串是否已出现在已装配词组区间中（UTF-8 字节去重）。
 * @param      self 状态机指针；不为 NULL。
 * @param      regionCount 已装配区间数。
 * @param      utf8 待查词组串借用指针；不为 NULL。
 * @return     已出现返回 true；否则 false。
 */
static bool imePhraseUtf8Seen(const XPinyinEngine* self, int regionCount,
                              const char* utf8)
{
    int r;
    for (r = 0; r < regionCount; ++r)
    {
        const XPinyinPhraseEntry* e;
        int i;
        if (self->m_regions[r].m_isPhrase == 0)
            continue;
        e = (const XPinyinPhraseEntry*)self->m_regions[r].m_begin;
        for (i = 0; i < (int)self->m_regions[r].m_count; ++i)
        {
            if (XStrcmp(e[i].m_utf8, utf8) == 0)
                return true;
        }
    }
    return false;
}

/**
 * @brief      追加一个词组借用区间（按路序并集、UTF-8 字节去重、防御
 *             闸）。
 * @details    领先重复前移区间首、区间内首见重复截断区间尾（零拷贝区
 *             间不可拆分，实测资产无跨键同词，去重属防御路径）；预算
 *             到达 XKEYBOARD_IME_REGION_MAX 即停（构造不可达）。
 * @param      self 状态机指针；不为 NULL。
 * @param      begin 词组区间首借用指针；不为 NULL。
 * @param      count 区间条数；>=1。
 * @param      regionCount 区间数累积（输入输出）。
 * @param      total 候选总数累积（输入输出）。
 * @return     无。
 */
static void imeAppendPhraseRegion(XPinyinEngine* self,
                                  const XPinyinPhraseEntry* begin,
                                  uint16_t count, int* regionCount,
                                  int32_t* total)
{
    int32_t lo = 0;
    int32_t hi;
    uint16_t n;
    while ((lo < (int32_t)count) &&
           imePhraseUtf8Seen(self, *regionCount, begin[lo].m_utf8))
        ++lo;
    if (lo >= (int32_t)count)
        return;
    hi = lo + 1;
    while ((hi < (int32_t)count) &&
           !imePhraseUtf8Seen(self, *regionCount, begin[hi].m_utf8))
        ++hi;
    n = (uint16_t)(hi - lo);
    if (*regionCount >= XKEYBOARD_IME_REGION_MAX)
        return; /* 防御闸：丢尾不越界写（构造不可达）。 */
    self->m_regions[*regionCount].m_begin = begin + lo;
    self->m_regions[*regionCount].m_count = n;
    self->m_regions[*regionCount].m_isPhrase = 1;
    ++(*regionCount);
    *total += n;
}

#endif /* XKEYBOARD_IME_PHRASE_ON */

/**
 * @brief      候选借用区间装配公共汇点（字母/九键数字双通道共用）。
 * @details    装配规则：①词组段——路径逐路 find，按路序并集、UTF-8
 *             字节去重；②单字段——allPaths=false 仅首路（主读法）各
 *             音节 XPinyinTable_find 区间顺序拼接（字母通道口径，回归
 *             锁定），allPaths=true 遍历全部路径的音节区间按路序拼接
 *             （九键数字通道口径：数字组歧义 26=an/ao/bo/co 只取首路
 *             会把其余读法的候选整段丢弃——实测九键 26 仅 5 条、翻页
 *             键灰置无效）；两种口径均相邻相同区间合并（同音节相邻时
 *             find 返回的指针与长度相同则跳过，避免同字 chip 重复刷
 *             屏）；③词组在前单字垫后、各自组内频序；④候选总数=区
 *             间条数和（上界 8×9 + 15×21=387，uint16 充裕）。零路径
 *             （无可达切分，如字母 "z" 或九键 "9"）零候选——不猜测。
 *             路径枚举（字母通道 imeEnumPathsDfs / 数字通道
 *             imeT9EnumPathsDfs）由调用方先行完成。
 * @param      self 状态机指针；不为 NULL。
 * @param      paths 枚举好的切分路径集合借用指针；不为 NULL。
 * @param      allPaths 单字段是否遍历全部路径（false=仅首路，字母通
 *             道口径；true=多路并集，九键数字通道口径）。
 * @return     无。
 */
static void imeAssembleCandidates(XPinyinEngine* self,
                                  const XkbImePathSet* paths, bool allPaths)
{
    int regionCount = 0;
    int32_t total = 0;
    int i;
    int pathCount = allPaths ? paths->m_count : (paths->m_count > 0 ? 1 : 0);
    int p;
    self->m_regionCount = 0;
    self->m_candidateCount = 0;
#if XKEYBOARD_IME_PHRASE_ON
    if (XPinyinPhrase_isReady())
    {
        for (p = 0; p < paths->m_count; ++p)
        {
            const XPinyinPhraseEntry* begin = NULL;
            uint16_t n = 0;
            if (XPinyinPhrase_find(paths->m_ids[p], paths->m_len[p],
                                        &begin, &n))
                imeAppendPhraseRegion(self, begin, n, &regionCount, &total);
        }
    }
#endif
    /* 单字段：allPaths=false 仅首路（主读法，字母通道口径）；true 遍
     * 历全部路径按路序拼接（九键数字通道口径）。相邻相同区间合并。 */
    for (p = 0; p < pathCount; ++p)
    {
        const XPinyinTableEntry* prevBegin = NULL;
        uint16_t prevCount = 0;
        if (regionCount >= XKEYBOARD_IME_REGION_MAX)
            break; /* 防御闸：丢尾不越界写（构造不可达）。 */
        for (i = 0; i < paths->m_len[p]; ++i)
        {
            const char* syl =
                XPinyinTable_syllableAt(paths->m_ids[p][i]);
            const XPinyinTableEntry* begin = NULL;
            uint16_t n = 0;
            if (!syl || !XPinyinTable_find(syl, &begin, &n))
                continue;
            if (begin == prevBegin && n == prevCount)
                continue; /* 相邻相同单字区间合并。 */
            if (regionCount >= XKEYBOARD_IME_REGION_MAX)
                break; /* 防御闸：丢尾不越界写（构造不可达）。 */
            self->m_regions[regionCount].m_begin = begin;
            self->m_regions[regionCount].m_count = n;
            self->m_regions[regionCount].m_isPhrase = 0;
            ++regionCount;
            total += n;
            prevBegin = begin;
            prevCount = n;
        }
    }
    self->m_regionCount = (uint16_t)regionCount;
    self->m_candidateCount = (uint16_t)total;
    self->m_page = 0;
}

/**
 * @brief      按字母组串刷新候选借用区间并回页 0（字母通道组串变化汇
 *             点）。
 * @details    最长音节优先 DFS 枚举组串切分路径（上限
 *             XKEYBOARD_IME_PATH_MAX=8 路）后交公共装配汇点
 *             imeAssembleCandidates（词组+首路单字两段式规则见彼处）。
 * @param      self 状态机指针；不为 NULL。
 * @return     无。
 */
static void imeRefreshCandidates(XPinyinEngine* self)
{
    XkbImePathSet paths;
    uint16_t acc[XKEYBOARD_IME_BUFFER_CAP];
    int len = 0;
    while (self->m_buffer[len] != '\0')
        ++len;
    paths.m_count = 0;
    if (len > 0)
        imeEnumPathsDfs(&paths, self->m_buffer, len, 0, acc, 0);
    imeAssembleCandidates(self, &paths, false); /* 字母通道：首路口径。 */
}

/* ==================== 内部函数：九键（T9）数字通道 ==================== */

/** @brief 单模式音节枚举缓冲容量（数字组宽 3~4 的表内落点实测最大 6
 *         条/模式，取 2 的幂裕量；到达即截断写出，返回值仍为完整命中
 *         数）。 */
#define IME_T9_MATCH_CAP 16

/**
 * @brief      固化边界位置（固化音节序列覆盖的数字位数）。
 * @details    由固化音节 id 序列派生（每音节位数=其字母数），不单设
 *             冗余字段，杜绝双记数失同步；id 来自音节表反查恒合法，
 *             查无（防御）按 0 位跳过。
 * @param      self 状态机借用指针；不为 NULL。
 * @return     固化边界位置（[0, 数字组串长]）。
 */
static int imeT9FixedLen(const XPinyinEngine* self)
{
    int len = 0;
    int i;
    for (i = 0; i < (int)self->m_t9FixedCount; ++i)
    {
        const char* syl = XPinyinTable_syllableAt(self->m_t9FixedIds[i]);
        if (syl)
        {
            int n = 0;
            while (syl[n] != '\0')
                ++n;
            len += n;
        }
    }
    return len;
}

/**
 * @brief      九键数字串是否可接受（INV-T9 判据，数字通道唯一合法门）。
 * @details    镜像字母通道 INV2：自固化边界 from 起 ∃k∈[from,L]——
 *             reachable[k]（前 k 位可恰切为『逐位字母落在对应数字组
 *             内』的完整音节序列；段长上限=最长音节 6）∧ 余部存在以
 *             其数字模式开头的音节（XPinyinTable_hasSyllableDigitsPrefix；
 *             k=L 项=整串恰完，空模式恒真）。段内音节存在性经
 *             XPinyinTable_digitsSyllables 计数查询（字典序窗口扫描，
 *             即逐字母增量剪枝的落点）。
 * @param      digits 数字串借用指针（NUL 结尾）；不为 NULL。
 * @param      len 数字串长度。
 * @param      from 固化边界起点（前缀已恰切分）。
 * @return     可接受返回 true；否则 false。
 */
static bool imeT9Acceptable(const char* digits, int len, int from)
{
    bool reachable[XKEYBOARD_IME_BUFFER_CAP + 1];
    int k;
    int j;
    if (len > XKEYBOARD_IME_BUFFER_CAP)
        return false;
    if (from < 0 || from > len)
        return false; /* 防御：固化边界越界（调用序保证不达）。 */
    for (k = 0; k <= len; ++k)
        reachable[k] = false;
    reachable[from] = true;
    for (k = from + 1; k <= len; ++k)
    {
        int low = k - IME_SYLLABLE_LEN_MAX;
        if (low < from)
            low = from;
        for (j = low; j < k; ++j)
        {
            if (reachable[j] &&
                XPinyinTable_digitsSyllables(digits + j, k - j, NULL, 0) > 0)
            {
                reachable[k] = true;
                break;
            }
        }
    }
    for (k = from; k <= len; ++k)
    {
        if (reachable[k] &&
            XPinyinTable_hasSyllableDigitsPrefix(digits + k, len - k))
            return true;
    }
    return false;
}

/**
 * @brief      T9 切分路径枚举 DFS（最长音节优先；同长按音节 id 序）。
 * @details    与字母通道 imeEnumPathsDfs 同构，差异仅在段内音节来源：
 *             数字模式（数字组界窗扫描）可命中多条音节，逐条递归。路
 *             数达上限即停止收集（文档化防御截断，与全键同口径）。
 * @param      set 路径集合指针；不为 NULL。
 * @param      digits 数字串借用指针；不为 NULL。
 * @param      len 数字串长度。
 * @param      pos 当前位置（固化边界起）。
 * @param      acc 当前路径 id 累积缓冲（调用方栈上提供；固化前缀已
 *             预填，深度=固化音节数起步）。
 * @param      depth 当前路径深度。
 * @return     无（路数达上限即停止收集）。
 */
static void imeT9EnumPathsDfs(XkbImePathSet* set, const char* digits, int len,
                              int pos, uint16_t* acc, int depth)
{
    int maxLen;
    int l;
    if (set->m_count >= XKEYBOARD_IME_PATH_MAX)
        return;
    if (pos == len)
    {
        /* 记录完整一路（深度<=组串容量；词组查询键超 4 音节由
           XPinyinPhrase_find 长度守卫恒 miss，不影响装配）。 */
        int i;
        for (i = 0; i < depth; ++i)
            set->m_ids[set->m_count][i] = acc[i];
        set->m_len[set->m_count] = (uint8_t)depth;
        ++set->m_count;
        return;
    }
    maxLen = len - pos;
    if (maxLen > IME_SYLLABLE_LEN_MAX)
        maxLen = IME_SYLLABLE_LEN_MAX;
    for (l = maxLen; l >= 1; --l)
    {
        uint16_t hits[IME_T9_MATCH_CAP];
        int n = XPinyinTable_digitsSyllables(digits + pos, l, hits,
                                             IME_T9_MATCH_CAP);
        int h;
        for (h = 0; h < n; ++h)
        {
            acc[depth] = hits[h];
            imeT9EnumPathsDfs(set, digits, len, pos + l, acc, depth + 1);
            if (set->m_count >= XKEYBOARD_IME_PATH_MAX)
                return;
        }
    }
}

/**
 * @brief      按数字组串刷新候选借用区间并回页 0（数字通道组串变化汇
 *             点）。
 * @details    固化前缀音节直入路径累积，自固化边界起枚举后缀切分路径
 *             （≤8 路）后交公共装配汇点 imeAssembleCandidates（词组+
 *             首路单字，与全键同源）。全串已固化（纯尾）时 pos==len
 *             即记完整一路，候选收敛于固化读法。
 * @param      self 状态机指针；不为 NULL。
 * @return     无。
 */
static void imeT9RefreshCandidates(XPinyinEngine* self)
{
    XkbImePathSet paths;
    uint16_t acc[XKEYBOARD_IME_BUFFER_CAP];
    int len = 0;
    int i;
    while (self->m_digits[len] != '\0')
        ++len;
    paths.m_count = 0;
    for (i = 0; i < (int)self->m_t9FixedCount; ++i)
        acc[i] = self->m_t9FixedIds[i];
    if (len > 0)
        imeT9EnumPathsDfs(&paths, self->m_digits, len, imeT9FixedLen(self),
                          acc, (int)self->m_t9FixedCount);
    imeAssembleCandidates(self, &paths, true); /* 数字通道：多路并集。 */
}

/**
 * @brief      清九键数字通道（数字组串+固化边界；不动字母通道）。
 * @param      self 状态机指针；不为 NULL。
 * @return     无。
 */
static void imeT9Clear(XPinyinEngine* self)
{
    self->m_digits[0] = '\0';
    self->m_t9FixedCount = 0;
}

/**
 * @brief      数字组串删尾至 newLen 位并收缩固化边界。
 * @details    固化音节按「数字覆盖不越过 newLen」整体保留：被截断的
 *             尾部固化音节整个退出固化序列（残位不再是完整读法）。
 *             INV-T9 对删尾保持（论证同字母通道退格前缀闭包：可达位
 *             置不受删尾影响、数字模式前缀性对缩短保持；固化音节退出
 *             后其自身即余下前缀的恰切分）。
 * @param      self 状态机指针；不为 NULL。
 * @param      newLen 删尾后位数（0..组串容量）。
 * @return     无。
 */
static void imeT9Truncate(XPinyinEngine* self, int newLen)
{
    self->m_digits[newLen] = '\0';
    while ((self->m_t9FixedCount > 0) && (imeT9FixedLen(self) > newLen))
        --self->m_t9FixedCount;
}

/**
 * @brief      候选文本统一访问器（区间顺序行走，两段式映射单点）。
 * @param      self 状态机指针；不为 NULL。
 * @param      index 全量 0 基下标；调用方保证 [0,candidateCount)。
 * @return     候选 UTF-8 串借用指针；越界（防御）返回 NULL。
 */
static const char* imeCandidateText(const XPinyinEngine* self, int32_t index)
{
    int r;
    for (r = 0; r < (int)self->m_regionCount; ++r)
    {
        int32_t n = (int32_t)self->m_regions[r].m_count;
        if (index < n)
        {
#if XKEYBOARD_IME_PHRASE_ON
            if (self->m_regions[r].m_isPhrase)
            {
                const XPinyinPhraseEntry* e =
                    (const XPinyinPhraseEntry*)
                        self->m_regions[r].m_begin;
                return e[index].m_utf8;
            }
#else
            /* Q3（PHRASE_OFF）：词组段编译裁剪，m_isPhrase 恒 0，
               词组分支不可达。 */
            if (self->m_regions[r].m_isPhrase)
                return NULL;
#endif
            {
                const XPinyinTableEntry* e =
                    (const XPinyinTableEntry*)self->m_regions[r].m_begin;
                return e[index].m_utf8;
            }
        }
        index -= n;
    }
    return NULL;
}

/**
 * @brief      提交串落缓冲并复位组串（公共提交尾）。
 * @details    拷贝上界 15 字节（m_commit 16 字节含 NUL）：2 字词组
 *             （7B）/3 字词组（10B）/15 字母原串（16B）全完整；超长串
 *             与 XKEYBOARD_IME_PHRASE_UTF8_MAX 加载校验闭环（解析期
 *             拒收，运行期不可能出现放不下的串）。提交尾清组串与全部
 *             借用区间（resetComposition）。
 * @param      self 状态机指针；不为 NULL。
 * @param      text 上屏串（组串或表内/词库候选借用指针）。
 * @return     恒返回 XPinyinEngineFeed_Committed。
 */
static XPinyinEngineFeed imeCommit(XPinyinEngine* self, const char* text)
{
    int32_t i = 0;

    while ((i < XKEYBOARD_IME_BUFFER_CAP) && (text[i] != '\0'))
    {
        self->m_commit[i] = text[i];
        ++i;
    }
    self->m_commit[i] = '\0';
    XPinyinEngine_resetComposition(self);
    return XPinyinEngineFeed_Committed;
}

/* ==================== 生命周期与查询 ==================== */

void XPinyinEngine_init(XPinyinEngine* self)
{
    if (!self) return;
    self->m_buffer[0] = '\0';
    self->m_commit[0] = '\0';
    imeT9Clear(self);
    self->m_chinese = true;
    self->m_regionCount = 0;
    self->m_candidateCount = 0;
    self->m_page = 0;
    self->m_pageSize = IME_PAGE_SIZE_MAX;
}

void XPinyinEngine_resetComposition(XPinyinEngine* self)
{
    if (!self) return;
    self->m_buffer[0] = '\0';
    imeT9Clear(self); /* 字母/数字双通道统一复位汇点。 */
    self->m_regionCount = 0;
    self->m_candidateCount = 0;
    self->m_page = 0;
}

bool XPinyinEngine_isChinese(const XPinyinEngine* self)
{
    return self ? self->m_chinese : false;
}

void XPinyinEngine_setChinese(XPinyinEngine* self, bool chinese)
{
    if (!self) return;
    self->m_chinese = chinese;
    XPinyinEngine_resetComposition(self);
}

bool XPinyinEngine_isComposing(const XPinyinEngine* self)
{
    return (self && (self->m_buffer[0] != '\0' ||
                     self->m_digits[0] != '\0'));
}

const char* XPinyinEngine_composingText(const XPinyinEngine* self)
{
    if (!self) return "";
    /* 九键数字通道活跃：组串显示口径=数字串（键盘候选带直显，
     * xvkpy_sync 组串镜像链路零改动接线）。 */
    if (self->m_digits[0] != '\0') return self->m_digits;
    if (self->m_buffer[0] == '\0') return "";
    return self->m_buffer;
}

void XPinyinEngine_setPageSize(XPinyinEngine* self, int32_t size)
{
    if (!self) return;
    if (size < 1) size = 1;
    if (size > IME_PAGE_SIZE_MAX) size = IME_PAGE_SIZE_MAX;
    self->m_pageSize = size;
    /* 重钳当前页（容量变小可能越出末页）。 */
    if (self->m_page >= XPinyinEngine_pageCount(self))
        self->m_page = XPinyinEngine_pageCount(self) - 1;
    if (self->m_page < 0) self->m_page = 0;
}

int32_t XPinyinEngine_pageSize(const XPinyinEngine* self)
{
    return self ? self->m_pageSize : IME_PAGE_SIZE_MAX;
}

int32_t XPinyinEngine_candidateCount(const XPinyinEngine* self)
{
    return self ? (int32_t)self->m_candidateCount : 0;
}

const char* XPinyinEngine_candidateAt(const XPinyinEngine* self,
                                     int32_t candidateIndex)
{
    if (!self || candidateIndex < 0 ||
        candidateIndex >= (int32_t)self->m_candidateCount)
        return NULL;
    return imeCandidateText(self, candidateIndex);
}

int32_t XPinyinEngine_pageIndex(const XPinyinEngine* self)
{
    return self ? self->m_page : 0;
}

int32_t XPinyinEngine_pageCount(const XPinyinEngine* self)
{
    int32_t count;
    int32_t size;
    if (!self) return 0;
    count = (int32_t)self->m_candidateCount;
    size = self->m_pageSize;
    if (size < 1) size = 1;
    return (count + size - 1) / size;
}

const char* XPinyinEngine_commitString(const XPinyinEngine* self)
{
    if (!self) return "";
    return self->m_commit;
}

/* ==================== 喂入（状态机迁移） ==================== */

XPinyinEngineFeed XPinyinEngine_feedLetter(XPinyinEngine* self, char letter)
{
    char candidate[XKEYBOARD_IME_BUFFER_CAP + 2];
    int32_t len;
    int32_t cut;

    if (!self || !self->m_chinese) return XPinyinEngineFeed_Ignored;
    if (letter < 'a' || letter > 'z') return XPinyinEngineFeed_Ignored;
    if (self->m_digits[0] != '\0')
        imeT9Clear(self); /* 字母键压过九键数字通道：切回全键组串
                             （双通道互斥；数字组串与固化边界一并弃）。 */
    len = (int32_t)XStrlen(self->m_buffer);
    if (len >= XKEYBOARD_IME_BUFFER_CAP) return XPinyinEngineFeed_Consumed;
    for (cut = 0; cut < len; ++cut)
        candidate[cut] = self->m_buffer[cut];
    candidate[len] = letter;
    candidate[len + 1] = '\0';
    if (imeInv2Acceptable(candidate))
    {
        /* INV2 可达：接受（不变式保持）；刷新候选（词组+首路单字）。 */
        int32_t i = 0;
        while (candidate[i] != '\0')
        {
            self->m_buffer[i] = candidate[i];
            ++i;
        }
        self->m_buffer[i] = '\0';
        imeRefreshCandidates(self);
        return XPinyinEngineFeed_Consumed;
    }
    /* 非法：对逐后缀跑同一 INV2 判据，截断至最长合法后缀（最长合法
       后缀优先语义与循环结构同 V1；不做自动提交——自动提交等于替用
       户猜字，错字静默）。 */
    for (cut = 1; candidate[cut] != '\0'; ++cut)
    {
        if (imeInv2Acceptable(candidate + cut))
        {
            int32_t i = 0;
            while (candidate[cut + i] != '\0')
            {
                self->m_buffer[i] = candidate[cut + i];
                ++i;
            }
            self->m_buffer[i] = '\0';
            imeRefreshCandidates(self);
            return XPinyinEngineFeed_Consumed;
        }
    }
    /* 无合法后缀：清空（静默吞掉，绝不写编辑框）。 */
    XPinyinEngine_resetComposition(self);
    return XPinyinEngineFeed_Consumed;
}

XPinyinEngineFeed XPinyinEngine_feedLetterRaw(XPinyinEngine* self,
                                              char letter)
{
    int32_t len;
    if (!self || !self->m_chinese) return XPinyinEngineFeed_Ignored;
    if (letter < 'a' || letter > 'z') return XPinyinEngineFeed_Ignored;
    if (self->m_digits[0] != '\0')
        imeT9Clear(self); /* 字母键压过九键数字通道（互斥同 feedLetter）。 */
    len = (int32_t)XStrlen(self->m_buffer);
    if (len >= XKEYBOARD_IME_BUFFER_CAP) return XPinyinEngineFeed_Consumed;
    /* 无条件入组串（不过 INV2 截断）：多击中间态（"ng"/"nh" 等非音节
       前缀）是必经路径而非错字；候选按可达读法刷新，无可达=0 条。 */
    self->m_buffer[len] = letter;
    self->m_buffer[len + 1] = '\0';
    imeRefreshCandidates(self);
    return XPinyinEngineFeed_Consumed;
}

XPinyinEngineFeed XPinyinEngine_feedBackspace(XPinyinEngine* self)
{
    int32_t len;
    if (!self || !self->m_chinese) return XPinyinEngineFeed_Ignored;
    /* 九键数字通道优先：删末位数字并收缩固化边界。 */
    len = (int32_t)XStrlen(self->m_digits);
    if (len > 0)
    {
        imeT9Truncate(self, (int)len - 1);
        imeT9RefreshCandidates(self);
        return XPinyinEngineFeed_Consumed;
    }
    len = (int32_t)XStrlen(self->m_buffer);
    if (len == 0) return XPinyinEngineFeed_Ignored; /* 透传编辑框退格。 */
    self->m_buffer[len - 1] = '\0';
    /* INV2 接受集前缀闭包（k 项判据删尾后仍达——可达位置不受删尾影
       响，音节前缀性对删尾保持）；刷新候选。 */
    imeRefreshCandidates(self);
    return XPinyinEngineFeed_Consumed;
}

XPinyinEngineFeed XPinyinEngine_feedCommitFirst(XPinyinEngine* self)
{
    const char* text;
    if (!self || !self->m_chinese) return XPinyinEngineFeed_Ignored;
    if (!XPinyinEngine_isComposing(self)) return XPinyinEngineFeed_Ignored;
    text = self->m_candidateCount > 0 ? imeCandidateText(self, 0) : NULL;
    if (text)
        return imeCommit(self, text);
    /* 无候选组串：空格与回车同义（提交原组串——字母/数字通道取活跃
     * 者；九键提交数字串本身，与全键提交原字母串同口径的确定性出
     * 口）。 */
    return imeCommit(self, self->m_digits[0] != '\0' ? self->m_digits
                                                     : self->m_buffer);
}

XPinyinEngineFeed XPinyinEngine_feedCommitRaw(XPinyinEngine* self)
{
    if (!self || !self->m_chinese) return XPinyinEngineFeed_Ignored;
    if (!XPinyinEngine_isComposing(self)) return XPinyinEngineFeed_Ignored;
    return imeCommit(self, self->m_digits[0] != '\0' ? self->m_digits
                                                     : self->m_buffer);
}

XPinyinEngineFeed XPinyinEngine_feedDigit(XPinyinEngine* self, int digit)
{
    int32_t index;
    const char* text;
    if (!self || !self->m_chinese) return XPinyinEngineFeed_Ignored;
    if (digit < 1 || digit > IME_PAGE_SIZE_MAX)
        return XPinyinEngineFeed_Ignored;
    /* 字母/数字任一通道组串中即可选候选（九键态经同一分页/区间链）。 */
    if (!XPinyinEngine_isComposing(self)) return XPinyinEngineFeed_Ignored;
    index = self->m_page * self->m_pageSize + (digit - 1);
    if (index >= (int32_t)self->m_candidateCount)
        return XPinyinEngineFeed_Consumed; /* 越界/无候选吞掉。 */
    text = imeCandidateText(self, index);
    if (!text) return XPinyinEngineFeed_Consumed;
    return imeCommit(self, text);
}

bool XPinyinEngine_feedT9Digit(XPinyinEngine* self, char digit)
{
    char candidate[XKEYBOARD_IME_BUFFER_CAP + 2];
    int32_t len;
    int32_t i;

    if (!self || !self->m_chinese) return false; /* EN 态冻结（直写）。 */
    if (digit < '2' || digit > '9') return false; /* 非法数字（'0'/'1'
                                                     由键盘侧走选候选链）。 */
    if (self->m_buffer[0] != '\0') return false; /* 字母通道持有组串：
                                                    互斥冻结。 */
    len = (int32_t)XStrlen(self->m_digits);
    if (len >= XKEYBOARD_IME_BUFFER_CAP) return false; /* 组串上限 15。 */
    for (i = 0; i < len; ++i)
        candidate[i] = self->m_digits[i];
    candidate[len] = digit;
    candidate[len + 1] = '\0';
    if (!imeT9Acceptable(candidate, (int)len + 1, imeT9FixedLen(self)))
        return false; /* 无可达读法：拒绝不吞不改态（INV-T9 门）。 */
    self->m_digits[len] = digit;
    self->m_digits[len + 1] = '\0';
    imeT9RefreshCandidates(self);
    return true;
}

const char* XPinyinEngine_digitComposition(const XPinyinEngine* self)
{
    if (!self || self->m_digits[0] == '\0') return "";
    return self->m_digits;
}

bool XPinyinEngine_feedDigitSeparator(XPinyinEngine* self)
{
    XkbImePathSet paths;
    uint16_t acc[XKEYBOARD_IME_BUFFER_CAP];
    int len = 0;
    int from;
    int i;

    if (!self || !self->m_chinese) return false;
    if (self->m_digits[0] == '\0') return false; /* 无组串无可固化。 */
    while (self->m_digits[len] != '\0')
        ++len;
    from = imeT9FixedLen(self);
    if (from >= len)
        return from == len; /* 边界已在串尾：幂等成功（>len 防御态按
                               失败，调用序保证不达）。 */
    paths.m_count = 0;
    for (i = 0; i < (int)self->m_t9FixedCount; ++i)
        acc[i] = self->m_t9FixedIds[i];
    imeT9EnumPathsDfs(&paths, self->m_digits, len, from, acc,
                      (int)self->m_t9FixedCount);
    if (paths.m_count == 0)
        return false; /* 纯前缀态：全串无恰切分，无边界可固化。 */
    /* 增量固化：已固化前缀保持（重复分词不重排既有读法），尾段取首
       路（主读法）新音节。 */
    for (i = (int)self->m_t9FixedCount; i < (int)paths.m_len[0]; ++i)
        self->m_t9FixedIds[i] = paths.m_ids[0][i];
    self->m_t9FixedCount = paths.m_len[0];
    imeT9RefreshCandidates(self);
    return true;
}

XPinyinEngineFeed XPinyinEngine_feedCandidate(XPinyinEngine* self,
                                            int32_t candidateIndex)
{
    const char* text;
    if (!self || !self->m_chinese) return XPinyinEngineFeed_Ignored;
    if (candidateIndex < 0 ||
        candidateIndex >= (int32_t)self->m_candidateCount)
        return XPinyinEngineFeed_Ignored;
    text = imeCandidateText(self, candidateIndex);
    if (!text) return XPinyinEngineFeed_Ignored;
    return imeCommit(self, text);
}

bool XPinyinEngine_pageNext(XPinyinEngine* self)
{
    if (!self) return false;
    if (self->m_page + 1 >= XPinyinEngine_pageCount(self)) return false;
    ++self->m_page;
    return true;
}

bool XPinyinEngine_pagePrev(XPinyinEngine* self)
{
    if (!self || self->m_page <= 0) return false;
    --self->m_page;
    return true;
}

/* ==================== 布局载体取用 ==================== */

const char* const* XPinyinEngine_map(void)
{
    return s_imeMap;
}

const XKeyboardButtonCtrl* XPinyinEngine_ctrlMap(void)
{
    return s_imeCtrl;
}

#endif /* XKEYBOARD_IME_ON */
