/**
 * @file       XPinyinTable.h
 * @brief      全键拼音 IME 单字候选表公开 API（音节→汉字→频序等级）。
 * @details    为 XVirtualKeyboard 全键拼音输入提供静态只读候选数据（XGui 扩展，
 *             无 Qt 对齐对象；实现只依赖 XinYueC 抽象层，禁止调用 Win32、
 *             POSIX、Qt 或其他平台 API）：
 *             - 音节口径：合法普通话拼音音节（无调合并，412 个，
 *               内嵌于 XPinyinTable.c 的 k_imeSyllables，按字典序存
 *               放）；ü 按 v 键位写作 lv/nv/lue/nue，j/q/x 后的 ü 按正词
 *               法写作 ju/qu/xu 系；
 *             - 条目口径：常用汉字 2017 条（去重 1989
 *               字，处于任务建议的 1200~2000 区间），每条 = 音节 id +
 *               汉字 UTF-8 + 组内频序等级；先按音节 id 升序、同音节内
 *               按频序等级升序存放，供二分与顺序候选输出；多音字按读
 *               音分列多条；收录深度按组大小分层截断（~40 字大组取
 *               top21、25~39 字组取 top9、20~24 字组取 top5、其余组
 *               top4~3），截断口径即本表“常用”边界；
 *             - 查询：XPinyinTable_find 输入小写音节串（'a'..'z'），
 *               返回该音节的条目区间（零拷贝借用），并按
 *               XKEYBOARD_IME_RANK_LIMIT 宏自动做频序 top-N 裁剪；
 *             - 数据为模型知识整理（非权威语料统计）：候选顺序反映通
 *               用常识常用度，未做语料级频度校准，个别罕读音节可能无收
 *               录字（find 返回 false 属正常）。
 * @note       本表静态只读，无动态内存、无库外依赖（libc 白名单外接口
 *             一律未用，音节串比较由内部实现）。裁剪口子：将
 *             XKEYBOARD_IME_RANK_LIMIT 设为 8 即只取每音节 top-8 候选。
 * @note       模块总开关 XKEYBOARD_IME_ON 定义于 XGuiConfig.h（与拼音
 *             IME 同门控；XKEYBOARD_ON=0 时经依赖级联连带为 0）。
 * @author     XinYueC 团队
 ******************************************************************************/
#ifndef XPINYINTABLE_H
#define XPINYINTABLE_H
#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>
#include "XGuiConfig.h"

#if XKEYBOARD_IME_ON

/* ==================== 常量 ==================== */

/**
 * @brief      频序裁剪宏口子（按频序取每音节 top-N）。
 * @details    XPinyinTable_find 只返回组内 m_rank <= 该值的候选；
 *             0 = 不裁剪（默认全量）。裁剪发生在返回区间上，数据本体不
 *             变；嵌入式可在编译期 -DXKEYBOARD_IME_RANK_LIMIT=8 一类方
 *             式整体收紧候选深度。
 */
#ifndef XKEYBOARD_IME_RANK_LIMIT
#define XKEYBOARD_IME_RANK_LIMIT 0
#endif

/* ==================== 类型定义 ==================== */

/**
 * @brief      IME 单字候选条目。
 * @details    条目按 m_syllable 升序、同音节内按 m_rank 升序存放在
 *             XPinyinTable.c 的静态只读数组中，调用者只读不得改。
 */
typedef struct XPinyinTableEntry
{
    uint16_t m_syllable;   /**< 音节 id（合法音节表下标，按字典序；由
                                XPinyinTable_syllableAt 反查音节串）。 */
    char m_utf8[4];        /**< 汉字 UTF-8 编码（常用字 3 字节 + NUL，
                                可直接当 C 字符串绘制；静态只读）。 */
    uint8_t m_rank;        /**< 组内频序等级（1=该音节最常用候选，组内
                                严格 1..n 递增；受
                                XKEYBOARD_IME_RANK_LIMIT 裁剪）。 */
} XPinyinTableEntry;

/* ==================== 公开函数 ==================== */

/**
 * @brief      查询合法音节总数。
 * @return     内嵌音节表条数（无调口径）。
 */
uint16_t XPinyinTable_syllableCount(void);

/**
 * @brief      查询候选条目总数。
 * @return     条目表条数（多音字按读音计多条；裁剪宏不影响本值）。
 */
uint16_t XPinyinTable_entryCount(void);

/**
 * @brief      查询生效的频序裁剪上限。
 * @details    返回 XKEYBOARD_IME_RANK_LIMIT 的生效值；0 表示未裁剪。
 * @return     裁剪上限（每音节保留的最多候选数）；0 = 不裁剪。
 */
uint16_t XPinyinTable_rankLimit(void);

/**
 * @brief      按下标查询音节串。
 * @param      syllableId 音节下标；取值 [0, XPinyinTable_syllableCount())。
 * @return     音节串借用指针（小写字母、NUL 结尾、静态存储，不得释放或
 *             修改）；越界返回 NULL。
 */
const char* XPinyinTable_syllableAt(uint16_t syllableId);

/**
 * @brief      判断音节串是否为合法普通话拼音音节（无调口径）。
 * @details    输入按 UTF-8 解码，仅接受 'a'..'z' 的小写音节串（如 "lv"、
 *             "shi"），大小写敏感，不接受空串或含非字母字符的串。
 * @param      syllable 音节串借用指针；NULL 返回 false。
 * @return     合法返回 true；非法、NULL 或查无此音节返回 false。
 */
bool XPinyinTable_isLegalSyllable(const char* syllable);

/**
 * @brief      音节串 → 音节 id 查询（二分，包内部 imeFindSyllableId）。
 * @details    供词组库（XPinyinPhrase）加载期把文本音节列换算为
 *             id 序列使用；音节 id 是合法音节表下标（按字典序，可与
 *             XPinyinTable_syllableAt 反查）。
 * @param      syllable 音节串借用指针；仅接受小写字母串，NULL 返回 false。
 * @param      outId 调用方提供存储空间；命中时接收音节 id，未命中不写。
 * @return     命中返回 true；非法串、NULL 或查无此音节返回 false。
 */
bool XPinyinTable_syllableIdOf(const char* syllable, uint16_t* outId);

/**
 * @brief      音节串 → 候选条目区间查询（二分定位，零拷贝）。
 * @details    查到音节后返回其在静态条目表中的连续区间 [outBegin,
 *             outBegin+outCount)，区间内按 m_rank 升序（1=最常用）；
 *             区间已按 XKEYBOARD_IME_RANK_LIMIT 做频序 top-N 裁剪。
 * @param      syllable 音节串借用指针；仅接受小写字母串，NULL 返回 false。
 * @param      outBegin 调用方提供存储空间；成功时接收区间首条目借用指
 *             针（静态只读，指向 k_imeEntries 内部，不得释放或修改），
 *             失败时置 NULL。
 * @param      outCount 调用方提供存储空间；成功时接收条目数（可为 0 的
 *             音节返回 false），失败时置 0。
 * @return     查到且有候选返回 true；音节非法、无收录字或参数为 NULL
 *             返回 false（此时 *outBegin==NULL 且 *outCount==0）。
 */
bool XPinyinTable_find(const char* syllable,
                            const XPinyinTableEntry** outBegin,
                            uint16_t* outCount);

/**
 * @brief      判断是否存在以 prefix 开头的合法音节（下界二分）。
 * @details    组串状态机 INV2 切分 DP 的段间前缀原语（V2 判据为『组串
 *             可切分为音节序列前缀』，本函数判定其中『某段存在以其开
 *             头的音节』；V1 时代曾是整个组串合法性判据 INV1，语义不
 *             变，用法升级）。实现为音节表下界二分（首个 >= prefix 的
 *             音节）后做前缀匹配，仅接受 'a'..'z' 的小写字母串。
 * @param      prefix 前缀串借用指针；只读。
 * @return     存在以 prefix 开头的音节返回 true；NULL 返回 false；空
 *             串返回 true（任何非空音节表下恒真，对应组串起点合法）；
 *             含非 'a'..'z' 字符返回 false。
 */
bool XPinyinTable_hasSyllablePrefix(const char* prefix);

/* ==================== 九键（T9）数字组查询 ==================== */

/* 九键数字组口径：2=abc 3=def 4=ghi 5=jkl 6=mno 7=pqrs 8=tuv 9=wxyz
 * （各数字对应字母为 ASCII 连续区间，供窗口扫描直接比界）。供
 * XPinyinEngine 九键消歧状态机（feedT9Digit 通道）做逐字母增量剪枝：
 * 数字模式串的每一段（最长 6 字母=最长音节 zhuang/chuang/shuang）经
 * 下方查询枚举「长度恰等且逐位字母落在对应数字组内」的音节 id 列表
 * ——实现为音节表字典序窗口扫描（下界二分定窗首，窗内逐条截断比较+
 * 逐位组界校验），不做全表线性扫。数字组为通用电话键盘口径，与
 * XPinyinEngine.h 的 T9 语义共用。 */

/**
 * @brief      九键数字模式音节枚举（长度恰等 + 逐位数字组匹配）。
 * @details    枚举「字母数恰为 len 且第 i 个字母落在 digits[i] 数字组
 *             内」的全部音节，按音节 id 升序（字典序）写入 outIds；
 *             实现为下界二分定窗 + 窗内截断字典序推进，单次开销与窗
 *             宽（数字组宽度 3~4 的笛卡尔积在表内的实际落点数，实测
 *             全表单模式最大 6 条）同阶，远小于全表扫描。
 * @param      digits 数字模式串借用指针（'2'..'9'；NULL 返回 0）。
 * @param      len 模式长度（1..6=最长音节；越界返回 0）。
 * @param      outIds 调用方提供存储空间；可为 NULL（此时只计数不写
 *             出，供存在性判定做零拷贝计数）。
 * @param      maxIds outIds 容量（outIds 为 NULL 时忽略）；命中数超
 *             容量时只截断写出、返回值仍为完整命中数。
 * @return     命中音节数（>=0）；参数非法返回 0。
 */
int XPinyinTable_digitsSyllables(const char* digits, int len,
                                 uint16_t* outIds, int maxIds);

/**
 * @brief      判断是否存在数字串以其数字模式开头的音节（九键前缀原语）。
 * @details    九键状态机切分 DP 的段间前缀原语（与全键
 *             XPinyinTable_hasSyllablePrefix 同位）：存在音节其数字串
 *             以 digits[0..len) 开头即真（音节可比模式长，如模式 "93"
 *             命中 zen 的数字串 936）。实现为同一窗口扫描的前缀形态
 *             （不要求长度恰等）。
 * @param      digits 数字模式串借用指针（'2'..'9'；NULL 返回 false）。
 * @param      len 模式长度；0 返回 true（任何非空音节表下恒真，对应
 *             切分起点/整串恰完语义）；含 '2'..'9' 外字符返回 false。
 * @return     存在返回 true；否则 false。
 */
bool XPinyinTable_hasSyllableDigitsPrefix(const char* digits, int len);

#endif /* XKEYBOARD_IME_ON */

#ifdef __cplusplus
}
#endif
#endif /* XPINYINTABLE_H */
