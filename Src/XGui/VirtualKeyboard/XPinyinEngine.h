/**
 * @file       XPinyinEngine.h
 * @brief      全键拼音 IME 组串状态机公开 API（XVirtualKeyboard 拼音输入扩
 *             展的纯逻辑模块）。
 * @details    为 XVirtualKeyboard 拼音输入提供无绘制、无编辑框依赖的组串状
 *             态机（XGui 扩展，无 Qt/LVGL 对齐对象；实现只依赖
 *             XinYueC 抽象层，禁止调用 Win32、POSIX、Qt 或其他平台
 *             API）：
 *             - 状态：EN（m_chinese=false，feed 一律 Ignored 交还键
 *               盘原语义）/ ZH_IDLE（中文态且组串空）/ ZH_COMPOSING
 *               （组串非空）。核心不变式 INV2（V2 起）：组串 m_buffer
 *               恒为『完整音节序列前缀 + 至多一个未完音节前缀』（即可
 *               切分为音节序列的前缀，V1 的『单一音节前缀』判据是其
 *               k=0 特例，接受集只增不减）——跨音节组串（nihao/
 *               zhongguo）由此可组，候选查询恒可安全调表；
 *             - 字母进组串：切分可达（∃k：前 k 字母恰切为音节序列且
 *               余部有音节以其开头）则接受；非法时截断至最长合法后缀
 *               （对逐后缀跑同一判据，最长合法后缀优先；不做自动提交
 *               ——自动提交等于替用户猜字，错字静默）；无合法后缀清
 *               空。ü 按表口径以 v 键位输入（lv/nv/lue/nue 即合法音
 *               节），v 打头被吞属设计行为；
 *             - 候选：两段式映射（不做跨表频度归一）——①词组段：组
 *               串切分路径逐路经 XPinyinPhrase_find 取零拷贝借
 *               用区间（进程期常驻词库，缺资产/关开关恒 miss）；②单
 *               字段：仅首路（主读法）各音节经 XPinyinTable_find
 *               顺序拼接（静态只读表，相邻相同区间合并）。词组在前单
 *               字垫后，各段组内频序。m_regions 为借用区间描述符数组
 *               （容量=8 词组路+15 首路音节，构造不可溢），候选总数
 *               =区间条数和；分页容量钳位 [1,9]；
 *             - 提交四路：点选候选/数字 1..9（当前页第 n 个）/空格
 *               （全局首候选；无候选组串提交原字母串）/回车（原字母
 *               串）。提交串副本存 m_commit（16 字节，15 字节内 UTF-8
 *               完整），下次 feed/reset 前有效。
 * @note       模块总开关 XKEYBOARD_IME_ON 定义于 XGuiConfig.h；=0 时
 *             裁剪全部公共 API。词组库开关 XKEYBOARD_IME_PHRASE_ON
 *             （=XKEYBOARD_IME_ON&&XFILE_ON）=0 时词组段编译裁剪，但
 *             INV2 切分与单字区间装配不受门控——组串行为是 V1 接受
 *             集的严格超集（非逐位 V1：nihao 组串 "nihao" 而非
 *             "hao"），候选为纯单字。本模块只依赖 XGuiConfig.h 与
 *             XPinyinTable.h（词组头仅实现文件按开关引用，无包
 *             含环）；组串显示统一收敛在键盘候选带，编辑框在提交前零
 *             触碰（无行内 preedit）。
 * @author     XinYueC 团队
 ******************************************************************************/
#ifndef XPINYINENGINE_H
#define XPINYINENGINE_H
#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>
#include "XGuiConfig.h"
#include "XPinyinTable.h"

#if XKEYBOARD_IME_ON

/* ==================== 常量 ==================== */

/**
 * @brief      组串容量（字母数上限；最长拼音串 14 字母
 *             chuanghongdeng，词库实测；15=uint16 可达位图上限，本实
 *             现可达集由布尔数组承载，容量对齐该口径）。
 * @details    维持 V1 的 6 则 zhongguo(8)/shijian(7) 等多数双字词打不
 *             进去；<=6 字母输入逐位保持 V1 行为。
 */
#ifndef XKEYBOARD_IME_BUFFER_CAP
#define XKEYBOARD_IME_BUFFER_CAP 15
#endif

/** @brief 组串切分路径枚举上限（最长音节优先 DFS 前 8 路；第 9 路后
 *         歧义读法弃用属文档化防御截断）。 */
#ifndef XKEYBOARD_IME_PATH_MAX
#define XKEYBOARD_IME_PATH_MAX 8
#endif

/**
 * @brief      候选借用区间描述符数组容量（8 词组路 + 15 首路音节）。
 * @details    词组区域数 <= 路径枚举上限 8（每路 find 至多 1 区间）；
 *             单字区域数 = 首路音节数（相邻合并后 <= 该值）<= 组串容
 *             量 15——由构造不可溢。代码仍设到达即停止追加的防御闸
 *             （永不应触发；触发即丢弃尾部区域，绝不越界写）。
 */
#ifndef XKEYBOARD_IME_REGION_MAX
#define XKEYBOARD_IME_REGION_MAX (XKEYBOARD_IME_PATH_MAX + XKEYBOARD_IME_BUFFER_CAP)
#endif

/* ==================== 枚举 ==================== */

/**
 * @brief      单次按键喂入结果（键盘侧据此决定拦截或透传原语义）。
 */
typedef enum XPinyinEngineFeed
{
    XPinyinEngineFeed_Ignored = 0,  /**< 未消费：键盘走原语义（编辑框
                                        直写/退格/换行/空格/数字）。 */
    XPinyinEngineFeed_Consumed,     /**< 已消费无上屏（进组串/吞掉）。 */
    XPinyinEngineFeed_Committed     /**< 已消费且产出上屏串（经
                                        XPinyinEngine_commitString 取用）。 */
} XPinyinEngineFeed;

/* ==================== 类型定义 ==================== */

/**
 * @brief      候选借用区间描述符（零拷贝两段式映射的最小单元）。
 * @details    m_begin 按 m_isPhrase 解释为 XPinyinPhraseEntry*
 *             （词组库堆区间，进程期常驻）或 XPinyinTableEntry*（单字
 *             表静态区间）——两类条目独立类型，候选文本统一经状态机
 *             访问器取出，调用方不直接触碰本描述符。
 */
typedef struct XPinyinEngineRegion
{
    const void* m_begin;   /**< 区间首条目借用指针（按 m_isPhrase 转
                                型解释；不得释放或修改）。 */
    uint16_t m_count;      /**< 区间条目数（>=1）。 */
    uint8_t m_isPhrase;    /**< 1=词组区间（XPinyinPhraseEntry）；
                                0=单字区间（XPinyinTableEntry）。 */
} XPinyinEngineRegion;

/**
 * @brief      拼音 IME 组串状态机（纯数据，无资源，可整体赋值拷贝）。
 * @details    调用者不得手工修改字段；一律走公开 API。m_regions 为借
 *             用区间描述符（零拷贝）：单字表区间静态进程期有效；词组
 *             区间指入词库进程期常驻堆——任何路径（resetComposition/
 *             setChinese/imeCommit/closePopup）都不得释放词库内存，
 *             词库卸载（XPinyinPhrase_unload/reload）前必须先清
 *             全部活动状态机的借用区间（resetComposition）。
 */
typedef struct XPinyinEngine
{
    char m_buffer[XKEYBOARD_IME_BUFFER_CAP + 1]; /**< 组串缓冲（最长
                                        15 字母 + NUL；恒为『音节序
                                        列前缀 + 至多一个未完音节前
                                        缀』，INV2）。 */
    char m_commit[XKEYBOARD_IME_BUFFER_CAP + 1]; /**< 最近一次
                                        Committed 上屏串副本（最长 15
                                        字节 + NUL，2/3 字词组与 15 字
                                        母原串全完整；下次 feed/reset
                                        前有效）。 */
    bool m_chinese;                /**< 中文态（false=EN 直写）。 */
    XPinyinEngineRegion m_regions[XKEYBOARD_IME_REGION_MAX]; /**< 候选
                                        借用区间描述符数组（词组路 +
                                        首路单字节；本模块外零引用）。 */
    uint16_t m_regionCount;        /**< 生效区间数（<=
                                        XKEYBOARD_IME_REGION_MAX）。 */
    uint16_t m_candidateCount;     /**< 候选总数（全部区间条数和；全
                                        量 0 基下标）。 */
    int32_t m_page;                /**< 当前页（0 基；钳位 [0,pageCount)）。 */
    int32_t m_pageSize;            /**< 页容量（钳位 [1,9]；默认 9）。 */
} XPinyinEngine;

/* ==================== 公开函数 ==================== */

/**
 * @brief      初始化状态机（中文态 true、组串/候选/页清零、页容量 9）。
 * @param      self 状态机指针；NULL 无操作。
 * @return     无。
 */
void XPinyinEngine_init(XPinyinEngine* self);

/**
 * @brief      清组串/候选/页（保留中文态与页容量）。
 * @details    组串生命周期挂点：中/EN 切换、键盘换绑目标/收层/隐藏/
 *             切换模式、IME 启停时由键盘侧调用。同时释放全部词组借用
 *             区间——是 XPinyinPhrase_unload/reload 的调用前置。
 * @param      self 状态机指针；NULL 无操作。
 * @return     无。
 */
void XPinyinEngine_resetComposition(XPinyinEngine* self);

/**
 * @brief      查询中文态。
 * @param      self 状态机借用指针；NULL 返回 false。
 * @return     中文态返回 true；EN 态或 self 为 NULL 返回 false。
 */
bool XPinyinEngine_isChinese(const XPinyinEngine* self);

/**
 * @brief      设置中文态（内部清组串/候选/页）。
 * @param      self 状态机指针；NULL 无操作。
 * @param      chinese true=中文态（拼音组串），false=EN 直写。
 * @return     无。
 */
void XPinyinEngine_setChinese(XPinyinEngine* self, bool chinese);

/**
 * @brief      查询是否组串中（中文态且组串非空）。
 * @param      self 状态机借用指针；NULL 返回 false。
 * @return     组串非空返回 true；IDLE 或 self 为 NULL 返回 false。
 */
bool XPinyinEngine_isComposing(const XPinyinEngine* self);

/**
 * @brief      查询组串文本（ASCII 小写字母串）。
 * @param      self 状态机借用指针。
 * @return     组串借用指针（内部缓冲，下次 feed/reset 前有效；不得释
 *             放或修改）；非组串或 self 为 NULL 返回空串。
 */
const char* XPinyinEngine_composingText(const XPinyinEngine* self);

/**
 * @brief      设置页容量（钳位 [1,9] 并重钳当前页）。
 * @details    页容量由键盘布局按候选带可用宽度与候选实宽换算注入（变
 *             宽 chip 后每次组串变化同步重算）；控件未布局（带宽 0）
 *             时调用方按钳位兜底注入。
 * @param      self 状态机指针；NULL 无操作。
 * @param      size 目标页容量；<1 按 1、>9 按 9 处理。
 * @return     无。
 */
void XPinyinEngine_setPageSize(XPinyinEngine* self, int32_t size);

/**
 * @brief      查询页容量。
 * @param      self 状态机借用指针；NULL 返回默认值 9。
 * @return     页容量（[1,9]）。
 */
int32_t XPinyinEngine_pageSize(const XPinyinEngine* self);

/**
 * @brief      喂入一个字母键（'a'..'z' 进组串）。
 * @details    INV2 判据：∃k∈[0,L]——前 k 字母可恰切为合法音节序列
 *             （reachable，段长<=6 由最长音节天然约束）且余部存在以其
 *             开头的音节（k=0 项即 V1 原判据，接受集只增不减）。非法
 *             时对逐后缀跑同一判据截断至最长合法后缀（例 "zhu"+o 保
 *             留可达后缀）；无合法后缀清空。截断/清空路径均返回
 *             Consumed（非法字母静默吞掉，绝不写编辑框）；组串已满
 *             15 字母时吞掉。
 * @param      self 状态机指针；NULL 返回 Ignored。
 * @param      letter 目标字母；仅接受 'a'..'z'（其他返回 Ignored）。
 * @return     Consumed（字母已消费进组串/吞掉）；EN 态或非法输入返回
 *             Ignored（键盘走原语义）。
 */
XPinyinEngineFeed XPinyinEngine_feedLetter(XPinyinEngine* self, char letter);

/**
 * @brief      喂入退格（删组串末字母）。
 * @param      self 状态机指针；NULL 返回 Ignored。
 * @return     Consumed（删了组串字母，键盘拦截不透传）；空组串/EN 态
 *             返回 Ignored（键盘透传编辑框退格）。
 */
XPinyinEngineFeed XPinyinEngine_feedBackspace(XPinyinEngine* self);

/**
 * @brief      喂入空格（提交键：有候选提交全局首候选；无候选组串提
 *             交原字母串）。
 * @param      self 状态机指针；NULL 返回 Ignored。
 * @return     Committed（上屏串经 XPinyinEngine_commitString 取用）；
 *             IDLE/EN 态返回 Ignored（键盘放行为真空格）。
 */
XPinyinEngineFeed XPinyinEngine_feedCommitFirst(XPinyinEngine* self);

/**
 * @brief      喂入回车（组串中提交原字母串——未经选字的确定性出口）。
 * @param      self 状态机指针；NULL 返回 Ignored。
 * @return     Committed；IDLE/EN 态返回 Ignored（键盘放行原换行语义）。
 */
XPinyinEngineFeed XPinyinEngine_feedCommitRaw(XPinyinEngine* self);

/**
 * @brief      喂入数字选候选（digit 1..9 选当前页第 n 个）。
 * @param      self 状态机指针；NULL 返回 Ignored。
 * @param      digit 目标序号（1..9；对应全量下标 page*pageSize+d-1）。
 * @return     Committed；越界/无候选且组串中返回 Consumed（吞掉）；
 *             IDLE/EN 态返回 Ignored（放行为普通数字）。
 */
XPinyinEngineFeed XPinyinEngine_feedDigit(XPinyinEngine* self, int digit);

/**
 * @brief      点选候选（全量 0 基下标）。
 * @param      self 状态机指针；NULL 返回 Ignored。
 * @param      candidateIndex 全量候选下标。
 * @return     Committed；越界/EN 态返回 Ignored。
 */
XPinyinEngineFeed XPinyinEngine_feedCandidate(XPinyinEngine* self,
                                            int32_t candidateIndex);

/**
 * @brief      翻下一页（钳位 [0,pageCount)）。
 * @param      self 状态机指针；NULL 返回 false。
 * @return     实际移动返回 true；已到末页或无候选返回 false。
 */
bool XPinyinEngine_pageNext(XPinyinEngine* self);

/**
 * @brief      翻上一页（钳位 [0,pageCount)）。
 * @param      self 状态机指针；NULL 返回 false。
 * @return     实际移动返回 true；已在首页返回 false。
 */
bool XPinyinEngine_pagePrev(XPinyinEngine* self);

/**
 * @brief      查询候选总数（全量，非当前页）。
 * @param      self 状态机借用指针；NULL 返回 0。
 * @return     候选总数；无候选组串（无可达切分）返回 0。
 */
int32_t XPinyinEngine_candidateCount(const XPinyinEngine* self);

/**
 * @brief      按全量 0 基下标取候选文本。
 * @param      self 状态机借用指针。
 * @param      candidateIndex 全量候选下标。
 * @return     候选 UTF-8 串借用指针（单字表静态只读或词库进程期常驻
 *             堆，不得释放或修改；词库 unload/reload 前必须先
 *             resetComposition 清全部借用区间）；越界或 self 为 NULL
 *             返回 NULL。
 */
const char* XPinyinEngine_candidateAt(const XPinyinEngine* self,
                                     int32_t candidateIndex);

/**
 * @brief      查询当前页（0 基）。
 * @param      self 状态机借用指针；NULL 返回 0。
 * @return     当前页下标（[0,pageCount)）。
 */
int32_t XPinyinEngine_pageIndex(const XPinyinEngine* self);

/**
 * @brief      查询总页数。
 * @param      self 状态机借用指针；NULL 返回 0。
 * @return     总页数；无候选返回 0。
 */
int32_t XPinyinEngine_pageCount(const XPinyinEngine* self);

/**
 * @brief      查询最近一次 Committed 的上屏串。
 * @param      self 状态机借用指针。
 * @return     上屏串借用指针（内部缓冲，下次 feed/reset 前有效；不得
 *             释放或修改）；从未提交或 self 为 NULL 返回空串。
 */
const char* XPinyinEngine_commitString(const XPinyinEngine* self);

#endif /* XKEYBOARD_IME_ON */

#ifdef __cplusplus
}
#endif
#endif /* XPINYINENGINE_H */
