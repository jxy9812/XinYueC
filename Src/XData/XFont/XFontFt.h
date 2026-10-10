/******************************************************************************
 * @file       XFontFt.h
 * @brief      XFont FreeType 文件后端（阶段二 ft2 provider）。
 * @author     XinYueC 团队
 * @note       阶段二设计 out/xfont_ft_phase2/design.md §1.2/§2.2/§3：
 *             FT_Library 进程单例（所有权收敛在本模块）、外挂
 *             .ttf/.otf/.ttc 候选解析、按家族一槽的静态 face、
 *             family→face+当前字号模型（遗留-1 P0 重构：字号切换
 *             FT_Set_Char_Size，face 进程期不重建）、像素→设计单位
 *             闭合回路。P2.3 的 XFontOutlineFace m_ft 分支与 P2.4 的
 *             XFont_face 接线是本模块的既定消费点。
 ******************************************************************************/
#ifndef XFONTFT_H
#define XFONTFT_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#include "XFontFace.h"
#include "XFontOutlineFace.h"

#if XFONT_FT_ON
#include <ft2build.h>
#include FT_FREETYPE_H

/**
 * @brief FT_Library 进程单例访问器（懒初始化 + 失败永久锁存）。
 * @details 初始化失败置模块级锁存、此后恒返回 NULL，永不重试；
 *          单 UI 线程模型不加锁（同 XPainter.c 论证）。
 * @warning 红线（§2.2）：任何调用方不得 FT_Done_FreeType——painter
 *          光栅化与家族槽 FT_Face 共享同一库实例，Done 库即令槽内
 *          全部 face 悬空（UAF）。全工程零调用点为门禁。
 * @return 库句柄；初始化失败后恒为 NULL。
 */
FT_Library XFontFt_library(void);

/** @brief FreeType 内存桥统计（自库初始化起累计）：count=分配次数、
 *         bytes=累计请求字节、peak=live 峰值近似（分配侧累计）。 */
void XFontFt_memStat(long* count, long* bytes, long* peak);
/** @brief 长期/临时双路分流统计（2026-10-11）：tempCount/tempBytes=
 *         临时态（FT_Load_Glyph 字形 scratch）分配次数/字节合计；
 *         tempSmallCount=其中 ≤256B 真落 MultiPool 固定档的小块次数；
 *         tempLargeBytes=>256B 回落系统堆的大块字节。 */
void XFontFt_memStatSplit(long* tempCount, long* tempBytes,
                          long* tempSmallCount, long* tempLargeBytes);
/** @brief 临时块尺寸直方图报告（512B 档定夺测量脚手架）：逐箱
 *         count/bytes/livePeak，另报 realloc 次数（扩容补记）。 */
void XFontFt_histReport(void (*emit)(const char* line));
long XFontFt_memFreeCount(void);
#endif /* XFONT_FT_ON */

/* [已移除 2026-10-07] XFontFt_providerActive：env XFONT_PROVIDER 运行期
 * 选边（ft2/xfo1 双档）随 XFO1 实现删除——FT 为唯一轮廓字实现，
 * XFont_face 无条件先探 FT 文件，失败落位图回退。 */

/**
 * @brief 家族槽/字号切换计数器（遗留-1 P0 取证口径，单 UI 线程无锁）。
 * @details m_faceCreates = 全后端 FT_New_Memory_Face 成功次数，重构后
 *          恒等于家族槽数（face 常驻，字号切换零重建）；m_charSizeSets
 *          与 m_charSizeSkips = 字号切换两去路（26.6 直设值变/不变）；
 *          m_glyphLoads = FT_Load_Glyph 成功次数；m_glyphNoHintLoads =
 *          其中走小字号 NO_HINTING 分档的次数（遗留-2：px < 
 *          XFONT_FT_HINT_MIN_PX 的加载子集，m_glyphLoads ≥ 本值）。
 *          证据工具（巡检/stress harness）据此断言「换入换出已消除」
 *          与「分档只落 load_flags」。
 */
typedef struct XFontFtStats
{
    uint64_t m_familyFills;   /**< 家族槽装载次数（=blob 整读次数）。 */
    uint64_t m_faceCreates;   /**< FT_New_Memory_Face 成功次数（=家族数）。 */
    uint64_t m_charSizeSets;  /**< FT_Set_Char_Size 成功次数（字号变更）。 */
    uint64_t m_charSizeSkips; /**< 字号未变、零 FT 调用直返次数。 */
    uint64_t m_glyphLoads;    /**< FT_Load_Glyph 成功次数。 */
    uint64_t m_glyphNoHintLoads; /**< 其中小字号 NO_HINTING 分档次数。 */
} XFontFtStats;

/**
 * @brief 读取上述计数器快照（模块未编入 face 部分时全零）。
 */
void XFontFt_stats(XFontFtStats* out);

/**
 * @brief FT 文件后端全局度量（Info 虚函数供值，免字号，hhea 口径 §2.3）。
 * @param font 字体属性对象；只消费家族名。
 * @param info 输出 unitsPerEm/ascent/descent/lineGap（设计单位）。
 * @return 家族解析成功返回 true；缺文件/魔数不符/解析失败（锁存）/
 *         家族槽满/init 失败返回 false——调用方按 §3.2 落回退链。
 */
bool XFontFt_fileInfo(const XFont* font, XFontOutlineInfo* info);

/**
 * @brief FT 文件后端按旧槽加载字形（无字号载体，语义 = scale 1.0，§2.1）。
 * @details P2.3 的 VXFontOutlineFace_loadGlyph m_ft 分支消费。
 */
bool XFontFt_fileLoadGlyph(const XFont* font, uint32_t codepoint,
                           XFontOutlineGlyphMetrics* metrics,
                           const XFontOutlineSink* sink);

/**
 * @brief FT 文件后端按 float scale（=target_px/unitsPerEm）加载字形。
 * @details §1.2 闭合回路的 FT 侧入口：px26_6 = round(upem*scale*64)
 *          （double 域）直设 FT_Set_Char_Size，hinted 26.6 像素轮廓除以
 *          同一字号换算回设计单位供货；painter 端乘同一 scale 逐位复原
 *          hinted 像素几何。P2.3 的新虚槽 LoadOutlineGlyphScaled 消费；
 *          scale <= 0 或非有限视为非法返回 false。
 */
bool XFontFt_fileLoadGlyphScaled(const XFont* font, float scale,
                                 uint32_t codepoint,
                                 XFontOutlineGlyphMetrics* metrics,
                                 const XFontOutlineSink* sink);

/**
 * @brief 返回 family 对应家族槽的静态 face（§2.1/§2.2）。
 * @details 家族槽只增不复用，face 指针进程期永稳（painter 各缓存的
 *          家族身份锚）。仅返回已解析槽位，不做解析——调用方须先经
 *          XFontFt_fileInfo 成功（P2.4 接线次序），未解析返回 NULL。
 */
const XFontFace* XFontFt_fileFace(const char* family);

/**
 * @brief 小字号 hint 分档阈值（XFONT_FT_HINT_MIN_PX，env 可覆写）。
 * @details XPainter SW-AA 网格对齐（sink X 吸附 + 塌缩竖笔修复遍）
 *          与 FT 加载档共用同一字号边界：< 阈值走免吸附/免 hint 的
 *          纯反走样保笔路径。2026-10-08 真屏 12px「即」右卩蒸发根修
 *          接线：sub-1px 竖笔两缘在 X 吸附下撞同一半整列，修复遍
 *          判据未覆盖该形态，13px 门槛下 SW-AA 不再吸附（亚像素竖笔
 *          交 ftgrays AA + γ 提亮呈现）。
 */
uint32_t XFontFt_hintMinPx(void);

#ifdef __cplusplus
}
#endif

#endif /* XFONTFT_H */
