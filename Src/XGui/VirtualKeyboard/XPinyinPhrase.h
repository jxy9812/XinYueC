/**
 * @file       XPinyinPhrase.h
 * @brief      全键拼音 IME 外挂词组库公开 API（运行期文件资产的加载、
 *             生命周期与音节序列查询；XVirtualKeyboard 拼音输入 V2 扩展）。
 * @details    为 XPinyinEngine 组串状态机提供单字表（XPinyinTable，
 *             编译期静态）之外的第二候选来源：词组（2..4 个音节的汉字
 *             词）从外挂 UTF-8 文本文件加载为进程期常驻堆数据，查询接
 *             口与 XPinyinTable_find 同型（音节 id 序列精确匹配 →
 *             零拷贝借用区间）：
 *             - 资产口径（V3 双格式）：默认路径宏
 *               XKEYBOARD_IME_PHRASE_PATH（../Library/VirtualKeyboard/
 *               phrases_zh.bin），加载入口按文件头魔数自动识别二选一：
 *               △XIPB 二进制（V3 快路径，O(n) walk 零解码）：头 24 字
 *               节（magic "XIPB"、version u16=1、音节表条数 u16、条目
 *               数 u32、词串区字节数 u32、音节表指纹 u32、CRC-32/
 *               ISO-HDLC u32，全小端）+ 条目区（16 字节定长/条：音节
 *               id u16×4、有效数 u8、频序 u8、保留 u8×2、词偏移 u32）+
 *               词串区（UTF-8 原字节，词间恰一个 NUL）。由
 *               Tools/VirtualKeyboard/ime_phrases_compile.py 从 txt 编译产出，加载
 *               期顺序校验（版本/音节表条数与指纹/CRC/体积恒等式/逐
 *               条 walk 复检六规则）任一步失败即整体拒绝；
 *               △UTF-8 文本（V2 口径原样保留）：格式为「音节列 <TAB>
 *               词组 UTF-8 <TAB> 组内频序整数」，'#' 注释行/空行跳过，
 *               UTF-8 BOM 由加载器剥离，容忍行尾 CR。文本仍是唯一源
 *               格式与维护入口，改词先改 txt 再重编 bin；
 *               格式假定资产经编译器/手工维护，加载器只做结构防御
 *               （文本坏行逐行跳过计数、bin 校验失败整体拒绝）不认语
 *               义——拼音列是否构成通顺读音、词组是否常用均不校验，
 *               属于资产可信边界内的维护责任；
 *             - 加载校验（六条，坏行一律跳过+计数+收尾汇总诊断，不终
 *               止整文件）：①音节列逐个经
 *               XPinyinTable_syllableIdOf 换 id，未命中=坏行；
 *               ②音节数恰 2..4（1 音节行拒收，锁死单音节键永不侵入
 *               V1 单字候选）；③词组字段 1..15 字节（
 *               XKEYBOARD_IME_PHRASE_UTF8_MAX）、非空、无 <0x20 字节；
 *               ④频序整数 1..255；⑤恰 3 个 TAB 分隔字段；⑥词组字段
 *               必须为良构 UTF-8 序列（结构级校验：首字节 0xC2..0xF4
 *               定长 2/3/4，续字节 0x80..0xBF 齐备；截断在序列中间/
 *               孤立续字节/非法首字节=坏行）——防止『尾部字节均 ≥0x20
 *               通过 1..15 字节校验的半字符』经 XPainter_drawTextRect
 *               直绘出乱码（XVirtualKeyboard 候选 chip 同路径）；
 *             - 生命周期：懒加载+负结果粘滞（缺文件/解析全坏/OOM →
 *               load 返回 false 且不重探——候选刷新每击键执行，逐键
 *               文件探测不可接受，XFont 外挂字库负缓存先例）；进程期
 *               缓存不自动卸载，unload/reload 供测试与资产热替换；
 *             - 借用契约：find 返回的区间指针指入进程期常驻 blob，等
 *               效 XPinyinTable 静态表『进程期有效』语义；unload/
 *               reload 前必须无任何 XPinyinEngine 持有词组借用区间（先
 *               XPinyinEngine_resetComposition 清区间），错序是悬垂
 *               UB（设计不做运行时追踪）。
 *             - 信任边界（V3）：外部文件一律视为不可信输入，两条路径
 *               同等防御，均不假设生成期合法性——文本路径逐行六条校
 *               验（坏行跳过+计数）；bin 路径校验全部在偏移推进/解引
 *               用之前完成（magic/版本/音节表条数与指纹/CRC-32/体积
 *               恒等式 raw==24+count*16+wordsLen/逐条 walk 复检六规则
 *               的 bin 等价物），任一步失败整体拒绝并落负缓存，绝不
 *               携带越界指针、非良构 UTF-8 词或乱序索引进入常驻态。
 *               CRC 只证明『自上次编译以来未被改动』，不替代结构校验
 *               （合法 CRC 的外来 bin 仍需过全部 walk 复检）。
 * @note       模块开关 XKEYBOARD_IME_PHRASE_ON 定义于 XGuiConfig.h（表
 *             达式宏 XKEYBOARD_IME_ON && XFILE_ON；XFILE_ON=0 嵌入式无
 *             文件系统时整模块编译裁剪）。本头文件不暴露 XFile/
 *             XIODevice——文件读取属实现细节，调用方只见条目与查询。
 * @author     XinYueC 团队
 ******************************************************************************/
#ifndef XPINYINPHRASE_H
#define XPINYINPHRASE_H
#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>
#include "XGuiConfig.h"

#if XKEYBOARD_IME_PHRASE_ON

/* ==================== 常量 ==================== */

/**
 * @brief      词组库外挂文件默认路径（相对仓库 bin/ 工作目录口径，与
 *             XFONT_EXTERNAL_FONT_DIR 同款；open 失败时加载器按
 *             XFont 先例做 ../ 剥前缀重试与 exe 目录兜底）。
 * @details    V3 起默认指向 XIPB 二进制资产（编译器
 *             Tools/VirtualKeyboard/ime_phrases_compile.py 产出）；同名 .txt 保留
 *             为唯一源格式与维护入口。加载器按魔数自动识别格式，本宏
 *             改指 .txt 亦合法。嵌入式工程可经编译选项覆盖为实际挂载
 *             目录，如 "-DXKEYBOARD_IME_PHRASE_PATH=\"0:/ime/phrases.bin\""。
 */
#ifndef XKEYBOARD_IME_PHRASE_PATH
#define XKEYBOARD_IME_PHRASE_PATH "../Library/VirtualKeyboard/phrases_zh.bin"
#endif

/** @brief 词组库完整路径缓冲上限（含结尾 NUL；仿
 *         XFONT_EXTERNAL_FONT_PATH_MAX）。 */
#ifndef XKEYBOARD_IME_PHRASE_PATH_MAX
#define XKEYBOARD_IME_PHRASE_PATH_MAX 512
#endif

/**
 * @brief      词组候选条数裁剪口子（每组按频序取 top-N）。
 * @details    XPinyinPhrase_find 只返回组内 m_rank <= 该值的候选，
 *             镜像 XKEYBOARD_IME_RANK_LIMIT 口径；0 = 不裁剪（默认全
 *             量）。裁剪发生在返回区间上，数据本体不变。
 */
#ifndef XKEYBOARD_IME_PHRASE_LIMIT
#define XKEYBOARD_IME_PHRASE_LIMIT 9
#endif

/** @brief 词组字段 UTF-8 最大字节数（超长行加载期拒收；与
 *         XPinyinEngine 提交缓冲 15 字母容量闭环）。 */
#ifndef XKEYBOARD_IME_PHRASE_UTF8_MAX
#define XKEYBOARD_IME_PHRASE_UTF8_MAX 15
#endif

/* ==================== 类型定义 ==================== */

/**
 * @brief      IME 词组候选条目。
 * @details    条目按音节 id 序列字典序、同组内按 m_rank 升序存放在加
 *             载器堆数组中；m_utf8 指入进程期常驻 blob（NUL 结尾，零
 *             拷贝借用），调用者只读不得改、不得释放。注意与单字条目
 *             XPinyinTableEntry（m_utf8[4] 定长值数组）是两个独立类型，
 *             不复用——词组最长 15 字节放不进 4 字节字段。
 */
typedef struct XPinyinPhraseEntry
{
    uint16_t m_syllable[4]; /**< 音节 id 序列（合法音节表下标，按字典
                                  序；m_syllableCount 个有效，其余为 0
                                  填充）。 */
    uint8_t m_syllableCount; /**< 有效音节数（2..4；加载校验口径②）。 */
    const char* m_utf8;      /**< 词组 UTF-8 串借用指针（指入进程期常
                                  驻 blob，NUL 结尾；不得释放或修改，
                                  unload 后失效）。 */
    uint8_t m_rank;          /**< 组内频序等级（1=该组最常用，组内严
                                  格 1..n 递增；受
                                  XKEYBOARD_IME_PHRASE_LIMIT 裁剪）。 */
} XPinyinPhraseEntry;

/* ==================== 生命周期 ==================== */

/**
 * @brief      懒加载词组库（幂等；负结果粘滞）。
 * @details    首次调用执行：路径解析（setPath 覆盖或默认宏）→ 整文件
 *             读入 → 逐行解析校验 → 稳定排序建表。之后调用直接返回缓
 *             存结果。缺文件/解析全坏/OOM 时缓存负结果（不重探）。
 *             挂点约定：XVirtualKeyboard_setImeEnabled(true) 内触发，纯 UI 巡
 *             检路径（守护定时器等）禁止调用（内含文件 IO）。
 * @return     加载后 XPinyinPhrase_isReady() 的值；已加载时直接
 *             返回缓存状态，不重读文件。
 */
bool XPinyinPhrase_load(void);

/**
 * @brief      查询词组库是否可用（已加载且有词条）。
 * @return     已成功加载且词条数 >0 返回 true；未加载、加载失败或词库
 *             为空返回 false（查询接口全部安全回落 miss）。
 */
bool XPinyinPhrase_isReady(void);

/**
 * @brief      查询词组条目总数。
 * @return     词条总数；未加载或加载失败返回 0。
 */
int32_t XPinyinPhrase_count(void);

/**
 * @brief      卸载词组库（释放 blob 与条目数组，进程期缓存的手动卸载
 *             口，仅测试/资产热替换/收尾使用）。
 * @param      无。
 * @return     无。
 * @warning    调用前置=无任何 XPinyinEngine 持有词组借用区间（先
 *             XPinyinEngine_resetComposition 清 m_regions），错序是悬
 *             针 UB（设计不做运行时追踪）。卸载后 next load 重新读盘。
 */
void XPinyinPhrase_unload(void);

/**
 * @brief      重载词组库（unload+load，配合 setPath 换资产生效）。
 * @param      无。
 * @return     重载后 XPinyinPhrase_isReady() 的值。
 * @warning    调用前置与 unload 相同：先 XPinyinEngine_resetComposition
 *             清全部借用区间，错序是悬垂 UB。
 */
bool XPinyinPhrase_reload(void);

/**
 * @brief      覆盖词组库路径（测试/定制；配合 reload 生效）。
 * @param      path 新路径借用指针（UTF-8；超 XKEYBOARD_IME_PHRASE_PATH
 *             _PATH_MAX 截断；NULL=清除覆盖，回默认宏路径）。仅存拷贝，
 *             不取得所有权，返回后可立即释放。
 * @return     无。
 */
void XPinyinPhrase_setPath(const char* path);

/* ==================== 查询 ==================== */

/**
 * @brief      音节 id 序列精确查询（二分定位，零拷贝区间）。
 * @details    查到词组键后返回其在条目表中的连续区间 [outBegin,
 *             outBegin+outCount)，区间内按 m_rank 升序（1=最常用）；
 *             区间已按 XKEYBOARD_IME_PHRASE_LIMIT 做组内 top-N 裁剪。
 *             词组键为 2..4 个音节 id（加载器拒收其它长度，1 音节序
 *             列查询恒 miss——单音节候选永远只来自单字表，V1 平价）。
 * @param      syllableIds 音节 id 序列借用指针（只读；元素为
 *             XPinyinTable_syllableIdOf 产出的合法音节下标）。
 * @param      count 序列长度（1..4；越界返回 false）。
 * @param      outBegin 调用方提供存储空间；成功时接收区间首条目借用指
 *             针（指入进程期常驻堆，不得释放或修改），失败时置 NULL。
 * @param      outCount 调用方提供存储空间；成功时接收条目数，失败时
 *             置 0。
 * @return     查到且有候选返回 true；未 ready、查无此键或参数非法返回
 *             false（此时 *outBegin==NULL 且 *outCount==0）。
 */
bool XPinyinPhrase_find(const uint16_t* syllableIds, int32_t count,
                             const XPinyinPhraseEntry** outBegin,
                             uint16_t* outCount);

#endif /* XKEYBOARD_IME_PHRASE_ON */

#ifdef __cplusplus
}
#endif
#endif /* XPINYINPHRASE_H */
