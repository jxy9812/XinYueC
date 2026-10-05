/**
 * @file       XPinyinT9Test.c
 * @brief      九键（T9）消歧拼音输入回归测试实现（纯模块级，无控件依
 *             赖；XGuiDemo 统一测试入口）。
 * @details    覆盖三层：① XPinyinTable 九键数字组查询原语
 *             （digitsSyllables 枚举/计数、hasSyllableDigitsPrefix 窗
 *             口外延不漏、全表 412 音节回环）；② XPinyinEngine 数字通
 *             道状态机——INV-T9 接受/拒绝（"57" 无可达读法）、组串上
 *             限 15、EN 态/字母通道互斥冻结、数字直显
 *             （composingText=digitComposition）、分词键固化与幂等、
 *             退格删位与固化边界收缩；③ 候选/提交复用既有机制——词
 *             组+单字混排与全键同源（西安/爸爸/白菜按词库就绪双分支
 *             断言）、feedCommitFirst/Raw、feedDigit(int) 选候选、分
 *             页与 resetComposition。全部期望值由内嵌表静态推导（探针
 *             脚本核对口径：音节切分路径=最长音节优先 DFS、同长按音
 *             节 id 序、词组段按路序并集+首路单字垫后）。
 * @author     XinYueC 团队
 ******************************************************************************/
#include "XPinyinT9Test.h"
#include "XGuiConfig.h" /* 门控宏定义源：必须在 :2 的 #if 之前（XKeyboard
                            Test 同款纪律——宏未定义时 #if 恒 0，套件
                            经静默 stub 恒绿，断言全部失效）。 */

#if XKEYBOARD_IME_ON

#include "XPinyinEngine.h"
#include "XPinyinTable.h"
#if XKEYBOARD_IME_PHRASE_ON
#include "XPinyinPhrase.h"
#endif
#include <stdio.h>
#include <string.h>

/* ==================== 断言与工具 ==================== */

static int xkb_failures = 0;

static void xkb_expect(bool cond, const char* what)
{
    if (!cond) {
        fprintf(stderr, "[XKB-FAIL] %s\n", what ? what : "");
        ++xkb_failures;
    }
}

/** @brief 字母 → 九键数字组（2=abc 3=def 4=ghi 5=jkl 6=mno 7=pqrs
 *         8=tuv 9=wxyz；组外原样返回便于回环断言）。 */
static char xkb_t9DigitOf(char c)
{
    if (c >= 'a' && c <= 'c') return '2';
    if (c >= 'd' && c <= 'f') return '3';
    if (c >= 'g' && c <= 'i') return '4';
    if (c >= 'j' && c <= 'l') return '5';
    if (c >= 'm' && c <= 'o') return '6';
    if (c >= 'p' && c <= 's') return '7';
    if (c >= 't' && c <= 'v') return '8';
    if (c >= 'w' && c <= 'z') return '9';
    return c;
}

/** @brief 向九键数字通道逐位喂串（任一位被拒即整体失败，供“全接受”
 *         语义断言）。 */
static bool xkb_t9FeedStr(XPinyinEngine* ime, const char* digits)
{
    int i;
    for (i = 0; digits[i] != '\0'; ++i)
        if (!XPinyinEngine_feedT9Digit(ime, digits[i])) return false;
    return true;
}

/** @brief 候选文本取用垫片（越界 NULL 归一为空串，strcmp 直比）。 */
static const char* xkb_cand(const XPinyinEngine* ime, int32_t idx)
{
    const char* t = XPinyinEngine_candidateAt(ime, idx);
    return t ? t : "";
}

/** @brief 汉字 UTF-8 转义锚（与 XPinyinTable.c 的 \xNN 书写同口径）：
 *         西=E8A5BF 安=E58989 先=E58588 弦=E5BCA6 下=E4B88B 爸=E788B8
 *         把=E68A8A 白=E799BD 菜=E88F9C 及=E58F8A 你=E4BDA0。 */
#define XKB_UTF8_XI    "\xE8\xA5\xBF"
#define XKB_UTF8_AN    "\xE5\xAE\x89"
#define XKB_UTF8_XIAN  "\xE8\xA5\xBF\xE5\xAE\x89"
#define XKB_UTF8_XIAN1 "\xE5\x85\x88"
#define XKB_UTF8_XIAN2 "\xE5\xBC\xA6"
#define XKB_UTF8_XIA   "\xE4\xB8\x8B"
#define XKB_UTF8_BA    "\xE7\x88\xB8"
#define XKB_UTF8_BABA  "\xE7\x88\xB8\xE7\x88\xB8"
#define XKB_UTF8_BA3   "\xE6\x8A\x8A"
#define XKB_UTF8_BAI   "\xE7\x99\xBD"
#define XKB_UTF8_BAICAI "\xE7\x99\xBD\xE8\x8F\x9C"
#define XKB_UTF8_JI    "\xE5\x8F\x8A"
#define XKB_UTF8_NI    "\xE4\xBD\xA0"

bool XPinyinT9Test_runAll(void)
{
    int phraseReady = 0;

    xkb_failures = 0;

#if XKEYBOARD_IME_PHRASE_ON
    /* 词组库懒加载（幂等；负结果粘滞）：回归工作目录=bin/，资产按
     * ../Library 相对解析；缺资产象限走回退分支断言（词组段恒
     * miss、候选=纯单字），不造通过假象。 */
    XPinyinPhrase_load();
    phraseReady = XPinyinPhrase_isReady() ? 1 : 0;
#endif

    /* ================================================================
     * ① 表层 T9 原语（XPinyinTable 数字组查询）。
     * ================================================================ */
    {
        uint16_t hits[16];
        int idN;
        int k;

        xkb_expect(XPinyinTable_digitsSyllables("942", 3, hits, 16) == 2,
                   "digitsSyllables(942) 命中 2 音节");
        xkb_expect(strcmp(XPinyinTable_syllableAt(hits[0]), "xia") == 0 &&
                       strcmp(XPinyinTable_syllableAt(hits[1]), "zha") == 0,
                   "digitsSyllables(942) 按音节 id 序 xia,zha");
        xkb_expect(XPinyinTable_digitsSyllables("94", 2, hits, 16) == 3 &&
                       strcmp(XPinyinTable_syllableAt(hits[0]), "xi") == 0 &&
                       strcmp(XPinyinTable_syllableAt(hits[1]), "yi") == 0 &&
                       strcmp(XPinyinTable_syllableAt(hits[2]), "zi") == 0,
                   "digitsSyllables(94) 命中 xi,yi,zi");
        xkb_expect(XPinyinTable_digitsSyllables("9426", 4, hits, 16) == 4 &&
                       strcmp(XPinyinTable_syllableAt(hits[0]), "xian") == 0,
                   "digitsSyllables(9426) 命中 4 音节且首为 xian");
        xkb_expect(XPinyinTable_digitsSyllables("7264", 4, hits, 16) == 3 &&
                       strcmp(XPinyinTable_syllableAt(hits[0]), "pang") == 0 &&
                       strcmp(XPinyinTable_syllableAt(hits[1]), "rang") == 0 &&
                       strcmp(XPinyinTable_syllableAt(hits[2]), "sang") == 0,
                   "digitsSyllables(7264) 命中 pang,rang,sang");
        xkb_expect(XPinyinTable_digitsSyllables("9", 1, hits, 16) == 0,
                   "digitsSyllables(9) 零命中（无单字母音节落在 wxyz）");
        xkb_expect(XPinyinTable_digitsSyllables("57", 2, hits, 16) == 0,
                   "digitsSyllables(57) 零命中（jkl×pqrs 无音节）");
        xkb_expect(XPinyinTable_digitsSyllables(NULL, 3, hits, 16) == 0,
                   "digitsSyllables(NULL) 0");
        xkb_expect(XPinyinTable_digitsSyllables("942", 0, hits, 16) == 0 &&
                       XPinyinTable_digitsSyllables("942", 7, hits, 16) == 0 &&
                       XPinyinTable_digitsSyllables("942", -1, hits, 16) == 0,
                   "digitsSyllables 长度越界 0");
        xkb_expect(XPinyinTable_digitsSyllables("142", 3, hits, 16) == 0,
                   "digitsSyllables 含 '1' 数字 0（组口径 2..9）");
        xkb_expect(XPinyinTable_digitsSyllables("942", 3, NULL, 0) == 2,
                   "digitsSyllables 只计数（outIds=NULL）");
        memset(hits, 0xFF, sizeof(hits));
        xkb_expect(XPinyinTable_digitsSyllables("942", 3, hits, 1) == 2 &&
                       strcmp(XPinyinTable_syllableAt(hits[0]), "xia") == 0,
                   "digitsSyllables 容量截断：返回完整计数、写出按容量");

        xkb_expect(XPinyinTable_hasSyllableDigitsPrefix("9", 1),
                   "hasDigitsPrefix(9) 真（wan/xi/za 系均可达）");
        xkb_expect(XPinyinTable_hasSyllableDigitsPrefix("93", 2),
                   "hasDigitsPrefix(93) 真（zen=936 窗外延不漏）");
        xkb_expect(XPinyinTable_hasSyllableDigitsPrefix("94264", 5),
                   "hasDigitsPrefix(94264) 真（xiang=94264）");
        xkb_expect(!XPinyinTable_hasSyllableDigitsPrefix("57", 2),
                   "hasDigitsPrefix(57) 假");
        xkb_expect(!XPinyinTable_hasSyllableDigitsPrefix("2255", 4),
                   "hasDigitsPrefix(2255) 假");
        xkb_expect(XPinyinTable_hasSyllableDigitsPrefix("", 0),
                   "hasDigitsPrefix(空) 恒真（切分起点/恰完语义）");
        xkb_expect(!XPinyinTable_hasSyllableDigitsPrefix(NULL, 0),
                   "hasDigitsPrefix(NULL) 假");
        xkb_expect(!XPinyinTable_hasSyllableDigitsPrefix("921", 3),
                   "hasDigitsPrefix 含 '1' 假");

        /* 全表回环：每音节派生数字串，枚举命中集必含自身（412 全量）。 */
        idN = (int)XPinyinTable_syllableCount();
        xkb_expect(idN == 412, "音节表 412 条（回环前置）");
        for (k = 0; k < idN; ++k) {
            const char* syl = XPinyinTable_syllableAt((uint16_t)k);
            char dig[8];
            int n = 0;
            int got;
            int h;
            int seen = 0;
            if (!syl) {
                xkb_expect(false, "全表回环：syllableAt 越界");
                break;
            }
            while (syl[n] != '\0') {
                dig[n] = xkb_t9DigitOf(syl[n]);
                ++n;
            }
            dig[n] = '\0';
            got = XPinyinTable_digitsSyllables(dig, n, hits, 16);
            if (got < 1) {
                xkb_expect(false, "全表回环：数字模式至少命中自身");
                break;
            }
            for (h = 0; h < got && h < 16; ++h)
                if (hits[h] == (uint16_t)k) seen = 1;
            if (!seen) {
                xkb_expect(false, "全表回环：音节 id 在其模式命中集");
                break;
            }
        }
    }

    /* ================================================================
     * ② 基础组串与数字直显（feedT9Digit/digitComposition）。
     * ================================================================ */
    {
        XPinyinEngine ime;

        xkb_expect(!XPinyinEngine_feedT9Digit(NULL, '2'),
                   "feedT9Digit(NULL) false");
        xkb_expect(strcmp(XPinyinEngine_digitComposition(NULL), "") == 0,
                   "digitComposition(NULL) 空串");

        XPinyinEngine_init(&ime);
        xkb_expect(!XPinyinEngine_feedT9Digit(&ime, '0') &&
                       !XPinyinEngine_feedT9Digit(&ime, '1') &&
                       !XPinyinEngine_feedT9Digit(&ime, 'a'),
                   "非 '2'..'9' 拒绝（'0'/'1' 留给选候选链）");
        xkb_expect(strcmp(XPinyinEngine_digitComposition(&ime), "") == 0,
                   "拒绝不落串：digitComposition 空");
        xkb_expect(XPinyinEngine_feedT9Digit(&ime, '9'),
                   "喂 '9' 接受");
        xkb_expect(strcmp(XPinyinEngine_digitComposition(&ime), "9") == 0,
                   "digitComposition=9");
        xkb_expect(strcmp(XPinyinEngine_composingText(&ime), "9") == 0,
                   "T9 态 composingText=数字串（键盘直显口径）");
        xkb_expect(XPinyinEngine_isComposing(&ime) &&
                       XPinyinEngine_candidateCount(&ime) == 0,
                   "纯前缀态组串中且零候选（不猜测）");
        xkb_expect(XPinyinEngine_feedT9Digit(&ime, '4') &&
                   strcmp(XPinyinEngine_digitComposition(&ime), "94") == 0,
                   "喂 '4' 扩至 94");
        xkb_expect(XPinyinEngine_candidateCount(&ime) == 51 &&
                       strcmp(xkb_cand(&ime, 0), XKB_UTF8_XI) == 0,
                   "94 候选=多路并集（xi|yi|zi）51 条且首字西");
        xkb_expect(XPinyinEngine_feedT9Digit(&ime, '2') &&
                   strcmp(XPinyinEngine_digitComposition(&ime), "942") == 0,
                   "喂 '2' 扩至 942");
        xkb_expect(XPinyinEngine_candidateCount(&ime) == 67 &&
                       strcmp(xkb_cand(&ime, 0), XKB_UTF8_XIA) == 0,
                   "942 候选=多路并集 67 条且首字下");
        xkb_expect(XPinyinEngine_feedT9Digit(&ime, '2') &&
                   XPinyinEngine_candidateCount(&ime) == 160 &&
                   strcmp(xkb_cand(&ime, 0), XKB_UTF8_XIA) == 0,
                   "9422 多路并集 160 条且首字下");
        xkb_expect(strcmp(XPinyinEngine_composingText(&ime), "9422") == 0,
                   "组串中 composingText 恒数字串");
    }

    /* ================================================================
     * ③ INV-T9 拒绝/组串上限/EN 冻结。
     * ================================================================ */
    {
        XPinyinEngine ime;
        int i;

        XPinyinEngine_init(&ime);
        xkb_expect(XPinyinEngine_feedT9Digit(&ime, '5') &&
                       XPinyinEngine_candidateCount(&ime) == 0,
                   "喂 '5' 接受（纯前缀零候选）");
        xkb_expect(!XPinyinEngine_feedT9Digit(&ime, '7'),
                   "喂 '7' 拒绝（57 无可达读法）");
        xkb_expect(strcmp(XPinyinEngine_digitComposition(&ime), "5") == 0 &&
                       XPinyinEngine_candidateCount(&ime) == 0,
                   "拒绝不改态：组串仍 5、零候选");
        xkb_expect(XPinyinEngine_feedT9Digit(&ime, '4') &&
                       XPinyinEngine_candidateCount(&ime) == 42 &&
                       strcmp(xkb_cand(&ime, 0), XKB_UTF8_JI) == 0,
                   "54 可达：多路并集 42 条且首字及");

        XPinyinEngine_init(&ime);
        for (i = 0; i < 15; ++i)
            xkb_expect(XPinyinEngine_feedT9Digit(&ime, '2'),
                       "15 位内逐位接受（15×'2' 链）");
        xkb_expect(XPinyinEngine_candidateCount(&ime) == 81 &&
                       strcmp(xkb_cand(&ime, 0), XKB_UTF8_BA3) == 0,
                   "15×'2' 多路并集 81 条且首字把");
        xkb_expect(!XPinyinEngine_feedT9Digit(&ime, '2'),
                   "第 16 位拒绝（组串上限 15）");
        xkb_expect(strlen(XPinyinEngine_digitComposition(&ime)) == 15,
                   "超限不落串：数字组串仍 15 位");

        XPinyinEngine_setChinese(&ime, false);
        xkb_expect(strcmp(XPinyinEngine_digitComposition(&ime), "") == 0,
                   "EN 切换清数字组串（setChinese 复位链）");
        xkb_expect(!XPinyinEngine_feedT9Digit(&ime, '2'),
                   "EN 态冻结：feedT9Digit false");
        XPinyinEngine_setChinese(&ime, true);
        xkb_expect(XPinyinEngine_feedT9Digit(&ime, '2'),
                   "回中文态恢复接受");
    }

    /* ================================================================
     * ④ 分词键（feedDigitSeparator：固化边界/幂等/退格收缩）。
     * ================================================================ */
    {
        XPinyinEngine ime;

        XPinyinEngine_init(&ime);
        xkb_expect(!XPinyinEngine_feedDigitSeparator(&ime),
                   "空组串分词 false");

        XPinyinEngine_resetComposition(&ime);
        xkb_expect(XPinyinEngine_feedT9Digit(&ime, '9'),
                   "喂 '9' 接受");
        xkb_expect(!XPinyinEngine_feedDigitSeparator(&ime),
                   "纯前缀态（9 无恰切分）分词 false");
        xkb_expect(strcmp(XPinyinEngine_digitComposition(&ime), "9") == 0,
                   "分词失败不改态");

        /* 全串后分词：9426 按首路固化单音节 xian（对照 ⑦ 无分词 10 条
           词组命中——固化收敛后词组 miss、候选=纯首路单字）。 */
        XPinyinEngine_resetComposition(&ime);
        xkb_t9FeedStr(&ime, "9426");
        xkb_expect(XPinyinEngine_feedDigitSeparator(&ime),
                   "9426 分词 true（首路 xian 恰切分）");
        xkb_expect(XPinyinEngine_candidateCount(&ime) == 9 &&
                       strcmp(xkb_cand(&ime, 0), XKB_UTF8_XIAN1) == 0,
                   "固化 xian：候选收敛 9 条且首字先");

        /* 边中分词：94 固化 xi 后接 26 → xi|an 读法命中词组西安（与
           无分词首路 4 字母 xian 的候选集实质分叉）。 */
        XPinyinEngine_resetComposition(&ime);
        xkb_t9FeedStr(&ime, "94");
        xkb_expect(XPinyinEngine_feedDigitSeparator(&ime) &&
                       XPinyinEngine_candidateCount(&ime) == 21 &&
                       strcmp(xkb_cand(&ime, 0), XKB_UTF8_XI) == 0,
                   "94 分词固化 xi：候选仍 21 条首字西");
        xkb_expect(XPinyinEngine_feedT9Digit(&ime, '2') &&
                       XPinyinEngine_candidateCount(&ime) == 23 &&
                       strcmp(xkb_cand(&ime, 0), XKB_UTF8_XI) == 0,
                   "固化后 +2：首路 xi|a → 21+2=23 条");
        xkb_expect(XPinyinEngine_feedT9Digit(&ime, '6') &&
                       XPinyinEngine_candidateCount(&ime) ==
                           (phraseReady ? 109 : 108) &&
                       strcmp(xkb_cand(&ime, 0),
                              phraseReady ? XKB_UTF8_XIAN : XKB_UTF8_XI) == 0,
                   "94⎵26：xi|an 词组西安垫首（缺词库回落首字西）");
        xkb_expect(XPinyinEngine_feedDigitSeparator(&ime),
                   "再次分词 true（增量固化 xi|an）");
        /* 固化读法收敛（多路并集口径）：未固化部分按数字组歧义并集、
           固化部分按音节区间——收敛后 27/26 条。 */
        xkb_expect(XPinyinEngine_candidateCount(&ime) ==
                       (phraseReady ? 27 : 26),
                   "固化读法收敛：候选数不变");

        /* 固化边界的退格收缩：删位后被截断的固化音节整体退出。 */
        XPinyinEngine_feedBackspace(&ime);
        xkb_expect(strcmp(XPinyinEngine_digitComposition(&ime), "942") == 0 &&
                       XPinyinEngine_candidateCount(&ime) == 23 &&
                       strcmp(xkb_cand(&ime, 0), XKB_UTF8_XI) == 0,
                   "退格至 942：固化 [xi] 保留，xi|a 23 条");
        XPinyinEngine_feedBackspace(&ime);
        xkb_expect(strcmp(XPinyinEngine_digitComposition(&ime), "94") == 0 &&
                       XPinyinEngine_candidateCount(&ime) == 21 &&
                       strcmp(xkb_cand(&ime, 0), XKB_UTF8_XI) == 0,
                   "退格至 94：固化恰完，21 条");
        XPinyinEngine_feedBackspace(&ime);
        xkb_expect(strcmp(XPinyinEngine_digitComposition(&ime), "9") == 0 &&
                       XPinyinEngine_candidateCount(&ime) == 0,
                   "退格至 9：固化 [xi] 整体退出（覆盖 2>1），零候选");
        xkb_expect(XPinyinEngine_feedBackspace(&ime) ==
                       XPinyinEngineFeed_Consumed &&
                       strcmp(XPinyinEngine_digitComposition(&ime), "") == 0,
                   "退格删末位数字清空：Consumed");
        xkb_expect(XPinyinEngine_feedBackspace(&ime) ==
                       XPinyinEngineFeed_Ignored,
                   "双通道空组串退格 Ignored（透传编辑框）");
        xkb_expect(strcmp(XPinyinEngine_digitComposition(&ime), "") == 0,
                   "退格清空后 digitComposition 空串");

        /* 2222⎵6：固化 [ba,ba] 后缀 o → ba|ba|o 三音节 8 条。 */
        XPinyinEngine_resetComposition(&ime);
        xkb_t9FeedStr(&ime, "2222");
        xkb_expect(XPinyinEngine_candidateCount(&ime) ==
                       (phraseReady ? 53 : 52),
                   "2222 候选数（词组爸爸+多路并集单字）");
        xkb_expect(XPinyinEngine_feedDigitSeparator(&ime),
                   "2222 分词 true");
        xkb_expect(XPinyinEngine_feedDigitSeparator(&ime),
                   "边界已在串尾：分词幂等 true");
        xkb_expect(XPinyinEngine_candidateCount(&ime) ==
                       (phraseReady ? 6 : 5) &&
                       strcmp(xkb_cand(&ime, 0),
                              phraseReady ? XKB_UTF8_BABA
                                          : XKB_UTF8_BA3) == 0,
                   "固化 [ba,ba]：ba|ba 音节区间收敛 6/5 条（爸爸/把垫首）");
        xkb_expect(XPinyinEngine_feedT9Digit(&ime, '6') &&
                       XPinyinEngine_candidateCount(&ime) == 8 &&
                       strcmp(xkb_cand(&ime, 0), XKB_UTF8_BA3) == 0,
                   "固化后 +6：ba|ba|o 5+3=8 条且首字把");
    }

    /* ================================================================
     * ⑤ 提交链/选候选/分页复用（与全键同机制）。
     * ================================================================ */
    {
        XPinyinEngine ime;

        XPinyinEngine_init(&ime);
        xkb_t9FeedStr(&ime, "942");
        xkb_expect(XPinyinEngine_feedCommitFirst(&ime) ==
                           XPinyinEngineFeed_Committed &&
                       strcmp(XPinyinEngine_commitString(&ime),
                              XKB_UTF8_XIA) == 0,
                   "空格提交首候选：下");
        xkb_expect(!XPinyinEngine_isComposing(&ime) &&
                       strcmp(XPinyinEngine_digitComposition(&ime), "") == 0 &&
                       XPinyinEngine_candidateCount(&ime) == 0,
                   "提交后组串/候选/数字串全清");

        xkb_t9FeedStr(&ime, "9");
        xkb_expect(XPinyinEngine_feedCommitFirst(&ime) ==
                           XPinyinEngineFeed_Committed &&
                       strcmp(XPinyinEngine_commitString(&ime), "9") == 0,
                   "无候选空格：提交数字原串（确定性出口）");
        xkb_t9FeedStr(&ime, "94");
        xkb_expect(XPinyinEngine_feedCommitRaw(&ime) ==
                           XPinyinEngineFeed_Committed &&
                       strcmp(XPinyinEngine_commitString(&ime), "94") == 0,
                   "回车：提交数字原串");

        xkb_t9FeedStr(&ime, "2222");
        xkb_expect(XPinyinEngine_feedDigit(&ime, 1) ==
                           XPinyinEngineFeed_Committed &&
                       strcmp(XPinyinEngine_commitString(&ime),
                              phraseReady ? XKB_UTF8_BABA
                                          : XKB_UTF8_BA3) == 0,
                   "数字 1 选当前页第 1 候选（爸爸/把）");

        xkb_t9FeedStr(&ime, "9426");
        xkb_expect(XPinyinEngine_feedCandidate(&ime, 0) ==
                           XPinyinEngineFeed_Committed &&
                       strcmp(XPinyinEngine_commitString(&ime),
                              phraseReady ? XKB_UTF8_XIAN
                                          : XKB_UTF8_XIAN1) == 0,
                   "点选候选 0：西安（缺词库回落先）");
        xkb_t9FeedStr(&ime, "9426");
        xkb_expect(XPinyinEngine_feedCandidate(&ime, 999) ==
                       XPinyinEngineFeed_Ignored,
                   "点选越界 Ignored");
        xkb_expect(XPinyinEngine_feedDigit(&ime, 0) ==
                       XPinyinEngineFeed_Ignored &&
                   XPinyinEngine_feedDigit(&ime, 10) ==
                       XPinyinEngineFeed_Ignored,
                   "数字选候选 0/10 越界 Ignored");
        {
            int32_t total = XPinyinEngine_candidateCount(&ime);
            const char* second = xkb_cand(&ime, 1); /* 借用指针（静态表/
                                词库常驻堆），提交清组串后仍有效。 */
            xkb_expect(XPinyinEngine_feedDigit(&ime, 2) ==
                               XPinyinEngineFeed_Committed &&
                           strcmp(XPinyinEngine_commitString(&ime),
                                  second) == 0,
                       "数字 2 选第 2 候选（与 candidateAt(1) 一致）");
            xkb_expect(total == 97,
                       "9426 全量候选数（词组+多路并集单字混排）");
        }

        /* 分页：9426 共 97（词库就绪）条，页容量 5 → 20 页。 */
        if (phraseReady) {
            xkb_t9FeedStr(&ime, "9426");
            XPinyinEngine_setPageSize(&ime, 5);
            xkb_expect(XPinyinEngine_pageCount(&ime) == 20 &&
                           XPinyinEngine_pageIndex(&ime) == 0,
                       "页容量 5：9426 共 20 页从首页起");
            xkb_expect(XPinyinEngine_pageNext(&ime) &&
                           XPinyinEngine_pageIndex(&ime) == 1,
                       "翻页一次到第 2 页");
            xkb_expect(XPinyinEngine_feedDigit(&ime, 5) ==
                               XPinyinEngineFeed_Committed &&
                           strcmp(XPinyinEngine_commitString(&ime),
                                  XKB_UTF8_XIAN2) == 0,
                       "第 2 页数字 5：全量下标 9（第 10 候选=弦）");
            xkb_expect(XPinyinEngine_feedCandidate(&ime, -1) ==
                           XPinyinEngineFeed_Ignored,
                       "点选负下标 Ignored");
        }
    }

    /* ================================================================
     * ⑥ 双通道互斥（字母键压过数字通道 / 数字通道冻结字母组串）。
     * ================================================================ */
    {
        XPinyinEngine ime;

        XPinyinEngine_init(&ime);
        xkb_t9FeedStr(&ime, "94");
        xkb_expect(XPinyinEngine_feedLetter(&ime, 'n') ==
                           XPinyinEngineFeed_Consumed &&
                       strcmp(XPinyinEngine_digitComposition(&ime), "") == 0 &&
                       strcmp(XPinyinEngine_composingText(&ime), "n") == 0,
                   "字母键压过 T9 通道：数字串弃、字母组串 n");
        xkb_expect(XPinyinEngine_feedLetter(&ime, 'i') ==
                       XPinyinEngineFeed_Consumed,
                   "字母 i 续接组串 Consumed");
        xkb_expect(XPinyinEngine_candidateCount(&ime) == 5 &&
                       strcmp(xkb_cand(&ime, 0), XKB_UTF8_NI) == 0,
                   "字母通道 ni 候选 5 条首字你（全键原语义不变）");
        xkb_expect(!XPinyinEngine_feedT9Digit(&ime, '6'),
                   "字母组串活跃：T9 喂数字冻结 false");
        xkb_expect(strcmp(XPinyinEngine_composingText(&ime), "ni") == 0,
                   "冻结不改字母组串");
        XPinyinEngine_resetComposition(&ime);
        xkb_expect(strcmp(XPinyinEngine_composingText(&ime), "") == 0 &&
                       strcmp(XPinyinEngine_digitComposition(&ime), "") == 0 &&
                       !XPinyinEngine_isComposing(&ime),
                   "resetComposition 双通道统一复位");
    }

    /* ================================================================
     * ⑦ 词组混排同源锁（与全键同一装配汇点的资产锚）。
     * ================================================================ */
    {
        XPinyinEngine ime;

        XPinyinEngine_init(&ime);
        xkb_t9FeedStr(&ime, "224224");
        xkb_expect(XPinyinEngine_candidateCount(&ime) ==
                       (phraseReady ? 71 : 70),
                   "224224 候选数（白菜+多路并集 bai 组）");
        xkb_expect(strcmp(xkb_cand(&ime, 0),
                          phraseReady ? XKB_UTF8_BAICAI
                                      : XKB_UTF8_BAI) == 0,
                   "224224 首候选：白菜（缺词库回落白）");
        /* 次候选双分支（文件头双象限纪律）：词库在位=词组后单字垫白；
           缺词库象限首路 [bai,bai] 相邻同音节区间合并为 bai 组 4 条，
           候选 1 是 bai 组第 2 字（与单字表逐位对齐，非白）。 */
        {
            const XPinyinTableEntry* bb = NULL;
            uint16_t bn = 0;
            const char* cand1 = XKB_UTF8_BAI;
            XPinyinTable_find("bai", &bb, &bn);
            if (!phraseReady && bb != NULL && bn >= 2)
                cand1 = (const char*)bb[1].m_utf8;
            xkb_expect(strcmp(xkb_cand(&ime, 1), cand1) == 0,
                       "词组后单字垫：次候选白（缺词库=bai 组第 2 字）");
        }
    }

    {
        int failures = xkb_failures;
        xkb_failures = 0;
        if (failures == 0) {
            fprintf(stderr, "XPinyinT9 test: PASS\n");
            return true;
        }
        fprintf(stderr, "XPinyinT9 test: %d assertion(s) failed\n",
                failures);
        return false;
    }
}

#else /* !XKEYBOARD_IME_ON */

bool XPinyinT9Test_runAll(void)
{
    return true; /* IME 关：无断言可跑，零回归语义。 */
}

#endif /* XKEYBOARD_IME_ON */
