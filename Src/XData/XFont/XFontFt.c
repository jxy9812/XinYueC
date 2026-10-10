/******************************************************************************
 * @file       XFontFt.c
 * @brief      XFont FreeType 文件后端实现（阶段二 ft2 provider）。
 * @author     XinYueC 团队
 * @note       阶段二设计 out/xfont_ft_phase2/design.md：FT_Library 单例与
 *             失败锁存（§2.2，所有权在此）、候选路径迭代（§3.1，独立实现）、
 *             4 字节魔数首读（§3.1 有界首读）、家族槽（§2.2；遗留-1 P0
 *             重构为 family→face+当前字号，见家族槽节注释）、解析失败
 *             家族锁存（§3.1）、FT_Outline_Decompose→XFontOutlineSink
 *             与 26.6→设计单位度量换算（§2.3/§1.2 闭合公式）、env 首读。
 ******************************************************************************/
#include "XFontFt.h"
#include "XFont.h"
#include "XFontFace_Protected.h"
#include "XFile.h"
#include "XCoreApplication.h"
#include "XSystem.h"
#include "XMemory.h"
#include "XPrintf.h"
#include <string.h>
#include <stdlib.h>
#include <math.h>

#if XFONT_FT_ON
#include FT_FREETYPE_H
#include FT_IMAGE_H
#include FT_OUTLINE_H
#include FT_TRUETYPE_TABLES_H /* TT_OS2 / ft_sfnt_os2（行度量覆盖档）。 */
#include FT_MODULE_H /* FT_New_Library/FT_Add_Default_Modules/
                        FT_Set_Default_Properties（自定义 FT_Memory 装配
                        三件套；此前隐式声明=64 位返回值截断隐患）。 */
#endif /* XFONT_FT_ON */

#if XFONT_FT_ON

/* ========== FT_Library 进程单例（§2.2，所有权收敛在此） ==========
   阶段一 painter 内的局部单例（XPainter.c:12020-12027）由 P2.5 上移改引
   XFontFt_library()。懒初始化一次（初始化要装配模块注册表，逐字形做是
   数量级浪费）；初始化失败置永久锁存、永不重试。XGui 文本绘制为单 UI
   线程模型，与既有静态字形/路径缓存同口径，不加锁。
   红线：全工程不得 FT_Done_FreeType——共享化后家族槽 FT_Face 挂在同一
   库上，Done 库即全部悬空（UAF）；painter 朝向自检失败只允许置 painter
   局部锁存回退旧扫描线（P2.5 改写 XPainter.c:12231-12237 的 Done 分支）。 */
static FT_Library g_ftLibrary = NULL;
static bool g_ftLibraryReady = false;
static bool g_ftLibraryFailed = false;

/* FreeType 内存管理接 XMemory（2026-10-10）：ftsystem.c 默认分配器直连
 * 系统 malloc/free，字体内存（face/字形数据/autofit）游离在项目内存统计
 * 之外。自定义 FT_Memory 把 FT 全部堆分配转发 XMemory——设备 OOM 排查
 * 时字体分配可被 XMemory 追踪，与工程「分配必须过 XMemory」纪律一致。
 * FT_MemoryRec 生命周期=进程期（库单例同寿命）。
 *
 * 长期/临时双路（2026-10-11 裁定）：FT_Memory 的 alloc 入口看不见分配
 * 寿命，长期/临时的分拣由驱动侧模式位完成——
 *   长期（库装配/face+流+表区/字号尺度对象/光栅器对象）→ XMalloc_System
 *   ：进程期常驻块不占池档；
 *   临时（FT_Load_Glyph 内的字形 scratch：glyph loader 扩容/TT 解释器
 *   暂存/cf2 hint 映射等，load 内即生即灭）→ XMalloc_Hybrid：≤256B 落
 *   MultiPool 固定档免碎片，更大回落系统堆。
 * 模式位只包住 slotLoadGlyph 里的 FT_Load_Glyph 本身（decompose 与
 * painter FT_Outline_Render 回长期态，raster 对象随库常驻走系统堆），
 * 其余全部站点处于缺省长期态。free 一律 XFree_Hybrid 按指针所属自动
 * 识别（池块/系统块均正确），realloc 新块跟随当前模式、旧块按指针
 * 释放——FT 在临时态对 face 建立期初缓冲（glyph loader）扩容亦不出错。
 * 已知例外：TrueType hinting 的 bytecode 上下文由 FT 在首次 Load_Glyph
 * 内惰性装配但随 face 常驻，落在临时态——缺省 Noto CJK 为 CFF 不触此
 * 路径，TTF 的常驻占用有界（家族槽封顶）。单 UI 线程模型（文件头），
 * 模式位无需原子。 */
static long g_ftAllocLive;
static long g_ftAllocPeak;
static long g_ftAllocCount;
static long g_ftAllocBytes;
static long g_ftFreeCount;
/* 长期/临时分流计数（OOM 取证口径）：临时态再分记池内小块（≤256B 落
   MultiPool 固定档）与池外大块（>256B 回落系统堆），验证模式位真实生效。 */
static long g_ftAllocTempCount;      /* 临时态分配次数合计 */
static long g_ftAllocTempBytes;      /* 临时态分配字节合计 */
static long g_ftAllocTempSmallCount; /* 临时态池内小块（≤阈值，进固定档池） */
static long g_ftAllocTempLargeBytes; /* 临时态池外大块字节（>阈值，系统堆） */
static bool g_ftAllocTemp; /* false=长期态 XMalloc_System；true=临时态 XMalloc_Hybrid */

/* 临时态分配（2026-10-11 阈值 512 配套）：池耗尽=硬失败（XMultiPool
 * malloc 返回 NULL 且无兜底，落到 FT 即字形丢弃），故 Hybrid 拒绝时
 * 回落系统堆；free 仍走 XFree_Hybrid 按指针所属识别，两种来源均正确
 * 释放。长期态直接系统堆，不经此路径。 */
static void* xfontft_tempAlloc(size_t size)
{
    void* p = XMalloc_Hybrid(size);
    return p ? p : XMalloc_System(size);
}

/* 512B 档定夺测量（2026-10-11，脚手架：阈值定夺后可整体移除）：
 * 临时块请求尺寸 2^n 分箱直方图（含 realloc 新块——扩容走 realloc 回调，
 * 此前 alloc 计数看不到）+ 各箱并发活集峰值。活集=直映射表 ptr→size，
 * free/realloc 旧块出表减账；表满丢记只使活账偏高（保守）。设备侧
 * 结论换算：嵌入式全局池档块数 16/8/4/2/1（桌面 256×3/128/64），是
 * 阈值能否上抬的硬约束——池耗尽返回 NULL→字形丢弃，无系统兜底。 */
#define FT_HIST_BINS 8 /* 64/128/256/512/1K/4K/16K/16K+ */
static long g_ftHistCount[FT_HIST_BINS];
static long g_ftHistBytes[FT_HIST_BINS];
static long g_ftHistReallocs;
static long g_ftBandLive[FT_HIST_BINS];
static long g_ftBandPeak[FT_HIST_BINS];
#define FT_LIVE_SLOTS 4096
#define FT_LIVE_MASK (FT_LIVE_SLOTS - 1)
static struct { const void* ptr; long size; } g_ftLiveTab[FT_LIVE_SLOTS];

static int xfontft_histBin(long size)
{
    if (size <= 64) return 0;
    if (size <= 128) return 1;
    if (size <= 256) return 2;
    if (size <= 512) return 3;
    if (size <= 1024) return 4;
    if (size <= 4096) return 5;
    if (size <= 16384) return 6;
    return 7;
}

static void xfontft_histNote(long size)
{
    int b = xfontft_histBin(size);
    ++g_ftHistCount[b];
    g_ftHistBytes[b] += size;
}

static void xfontft_livePut(const void* p, long size)
{
    size_t i = ((size_t)p * 2654435761u) >> 18 & FT_LIVE_MASK;
    while (g_ftLiveTab[i].ptr)
        i = (i + 1) & FT_LIVE_MASK;
    g_ftLiveTab[i].ptr = p;
    g_ftLiveTab[i].size = size;
}

static void xfontft_liveAdd(const void* p, long size)
{
    xfontft_livePut(p, size);
    ++g_ftBandLive[xfontft_histBin(size)];
    if (g_ftBandLive[xfontft_histBin(size)] >
        g_ftBandPeak[xfontft_histBin(size)])
        g_ftBandPeak[xfontft_histBin(size)] =
            g_ftBandLive[xfontft_histBin(size)];
}

static void xfontft_liveDel(const void* p)
{
    size_t i = ((size_t)p * 2654435761u) >> 18 & FT_LIVE_MASK;
    while (g_ftLiveTab[i].ptr)
    {
        if (g_ftLiveTab[i].ptr == p)
        {
            long size = g_ftLiveTab[i].size;
            size_t j;
            g_ftLiveTab[i].ptr = NULL;
            --g_ftBandLive[xfontft_histBin(size)];
            /* 线性探测删除：回填后续连续簇保持链完整 */
            j = (i + 1) & FT_LIVE_MASK;
            while (g_ftLiveTab[j].ptr)
            {
                const void* q = g_ftLiveTab[j].ptr;
                long qs = g_ftLiveTab[j].size;
                g_ftLiveTab[j].ptr = NULL;
                xfontft_livePut(q, qs);
                j = (j + 1) & FT_LIVE_MASK;
            }
            return;
        }
        i = (i + 1) & FT_LIVE_MASK;
    }
}

static void* xfontft_ftAlloc(FT_Memory memory, long size)
{
    (void)memory;
    void* p = g_ftAllocTemp ? xfontft_tempAlloc((size_t)size)
                            : XMalloc_System((size_t)size);
    if (p)
    {
        g_ftAllocLive += size;
        g_ftAllocBytes += size;
        if (g_ftAllocLive > g_ftAllocPeak) g_ftAllocPeak = g_ftAllocLive;
        ++g_ftAllocCount;
        if (g_ftAllocTemp)
        {
            ++g_ftAllocTempCount;
            g_ftAllocTempBytes += size;
            xfontft_histNote(size);
            xfontft_liveAdd(p, size);
            if (size <= XMEMORY_HYBRID_THRESHOLD)
                ++g_ftAllocTempSmallCount; /* 真落 MultiPool 固定档的部分 */
            else
                g_ftAllocTempLargeBytes += size; /* 回落系统堆的部分 */
        }
    }
    return p;
}

static void xfontft_ftFree(FT_Memory memory, void* block)
{
    (void)memory;
    ++g_ftFreeCount;
    if (!block) return;
    if (g_ftAllocTemp) xfontft_liveDel(block);
    XFree_Hybrid(block); /* 按指针所属槽分发：池块/系统块均正确释放 */
}

static void* xfontft_ftRealloc(FT_Memory memory, long cur_size,
                               long new_size, void* block)
{
    (void)memory;
    /* 扩容走 realloc 回调，alloc 计数看不到——直方图与活集在此补记。 */
    ++g_ftHistReallocs;
    if (new_size <= 0) { if (g_ftAllocTemp) xfontft_liveDel(block); XFree_Hybrid(block); return NULL; }
    {
        void* newBlock = g_ftAllocTemp
            ? xfontft_tempAlloc((size_t)new_size)
            : XMalloc_System((size_t)new_size);
        if (!newBlock) return NULL;
        if (block)
        {
            long copy = (cur_size < new_size) ? cur_size : new_size;
            memcpy(newBlock, block, (size_t)copy);
            if (g_ftAllocTemp) xfontft_liveDel(block);
            XFree_Hybrid(block);
        }
        if (g_ftAllocTemp)
        {
            xfontft_histNote(new_size);
            xfontft_liveAdd(newBlock, new_size);
            ++g_ftAllocTempCount;
            g_ftAllocTempBytes += new_size;
        }
        return newBlock;
    }
}

static struct FT_MemoryRec_ g_ftMemoryRec = {
    NULL,               /* user（FreeType 契约：内存对象用户数据） */
    xfontft_ftAlloc,
    xfontft_ftFree,
    xfontft_ftRealloc,
};

FT_Library XFontFt_library(void)
{
    static int useXMemory = -1;
    if (g_ftLibraryReady)
        return g_ftLibrary;
    if (g_ftLibraryFailed)
        return NULL;
    /* bisect 开关：XFONT_FT_XMEMORY=0 走 ftsystem 默认分配器（malloc），
       缺省 1 走 XMemory 桥（2026-10-10 接入，10-11 起长期/临时双路）。 */
    if (useXMemory < 0)
    {
        const char* v = XSystem_environment("XFONT_FT_XMEMORY");
        useXMemory = (v && v[0] == '0' && v[1] == '\0') ? 0 : 1;
    }
    if (!useXMemory)
    {
        if (FT_Init_FreeType(&g_ftLibrary) != 0)
        {
            g_ftLibrary = NULL;
            g_ftLibraryFailed = true;
            return NULL;
        }
        g_ftLibraryReady = true;
        return g_ftLibrary;
    }
    /* FT_New_Library + FT_Add_Default Modules：与 FT_Init_FreeType 等
       价，但允许注入自定义 FT_Memory（FT_Init_FreeType 固定用
       ftsystem.c 的默认分配器）。缺省模块集=truetype/cff/sfnt/psaux/
       psnames/pshinter/autofit/smooth/gzip 等（CMake 构建清单内的全
       部已编模块）。 */
    if (FT_New_Library(&g_ftMemoryRec, &g_ftLibrary) != 0)
    {
        /* 失败锁存：库指针归零，此后恒 NULL，永不重试（§2.2）。 */
        g_ftLibrary = NULL;
        g_ftLibraryFailed = true;
        return NULL;
    }
    FT_Add_Default_Modules(g_ftLibrary);
    FT_Set_Default_Properties(g_ftLibrary); /* 与 FT_Init_FreeType 同口径：
                                                读 FREETYPE_PROPERTIES env
                                                （no-stem-darkening 等）。 */
    g_ftLibraryReady = true;
    return g_ftLibrary;
}

/* 临时态进入/退出（2026-10-11 长期/临时双路）：只包 slotLoadGlyph 里的
   FT_Load_Glyph 站点——load 内的 glyph loader 扩容、TT 解释器暂存、
   cf2 hint 映射等字形 scratch 即生即灭，落混合池免碎片；decompose/
   painter 光栅化回长期态。XFontFt_library() 等长期站点在临时态内被
   调用也不受影响（fillFamily 先于 load 完成，时序上不发生；即便发生，
   FT_New_Library 分支缺省长期态照常装配）。进入/退出用显式函数对，
   不用语句表达式宏（MSVC 无此扩展）。 */
static bool XFontFt_tempScopeEnter(void)
{
    bool prev = g_ftAllocTemp;
    g_ftAllocTemp = true;
    return prev;
}

static void XFontFt_tempScopeExit(bool prev)
{
    g_ftAllocTemp = prev;
}

void XFontFt_memStat(long* count, long* bytes, long* peak)
{
    if (count) *count = g_ftAllocCount;
    if (bytes) *bytes = g_ftAllocBytes;
    if (peak) *peak = g_ftAllocPeak;
}

void XFontFt_memStatSplit(long* tempCount, long* tempBytes,
                          long* tempSmallCount, long* tempLargeBytes)
{
    if (tempCount) *tempCount = g_ftAllocTempCount;
    if (tempBytes) *tempBytes = g_ftAllocTempBytes;
    if (tempSmallCount) *tempSmallCount = g_ftAllocTempSmallCount;
    if (tempLargeBytes) *tempLargeBytes = g_ftAllocTempLargeBytes;
}

void XFontFt_histReport(void (*emit)(const char* line))
{
    static const char* bandName[FT_HIST_BINS] = {
        "<=64", "65-128", "129-256", "257-512",
        "513-1K", "1K-4K", "4K-16K", ">16K"
    };
    char line[128];
    int b;
    if (!emit)
        return;
    for (b = 0; b < FT_HIST_BINS; ++b)
    {
        if (!g_ftHistCount[b] && !g_ftBandPeak[b])
            continue;
        XSnprintf(line, sizeof(line),
                  "FT-hist %s: count=%ld bytes=%ld livePeak=%ld",
                  bandName[b], g_ftHistCount[b], g_ftHistBytes[b],
                  g_ftBandPeak[b]);
        emit(line);
    }
    XSnprintf(line, sizeof(line), "FT-hist reallocs=%ld", g_ftHistReallocs);
    emit(line);
}

long XFontFt_memFreeCount(void)
{
    return g_ftFreeCount;
}


#if XFONT_FT_FACE_ON

/* ========== 运行期开关（[已移除 2026-10-07]） ==========
 * 原 XFontFt_providerActive 的 env XFONT_PROVIDER 首读分档随 XFO1 实现
 * 删除：FT 是唯一轮廓字实现，XFont_face 无条件先探 FT 文件、失败落
 * 位图回退（XFontFace.c），不再有第二档可选。 */

/** @brief XFONT_FT_AUTOHINT 首读：缺省开（阶段二题设），精确 "0" 关闭
 *         切回 CFF/TT 原生引擎 bisect（§2.3）。 */
static bool XFontFt_autohintDefault(void)
{
    /* 缺省关（2026-10-10「确」口底横蒸发根修）：autohint 引擎在
       16px CJK 上把 1px 横带两缘塌成零高折返段（供给层零面积、
       even-odd 下口不闭口；GRIDFIT=0 原生 hinting 对照完整）。
       全局关的代价=E 类灰带 2792→3449（+657，发灰非缺笔），由
       γ 提档（14→18）补偿浓度。XFONT_FT_AUTOHINT=1 可回退。 */
    static int cached = -1;
    if (cached < 0)
    {
        const char* v = XSystem_environment("XFONT_FT_AUTOHINT");
        cached = (v && v[0] == '1' && v[1] == '\0') ? 1 : 0;
    }
    return cached != 0;
}

/** @brief XFONT_FT_FACE_INDEX 首读：.ttc 子字体序号，缺省 0；负值钳 0。 */
static long XFontFt_faceIndex(void)
{
    static long cached = -1;
    if (cached < 0)
    {
        const char* v = XSystem_environment("XFONT_FT_FACE_INDEX");
        long value = 0;
        if (v && v[0])
            value = atol(v);
        cached = value > 0 ? value : 0;
    }
    return cached;
}

/* ========== 小字号 hint 分档（遗留-2 修复，2026-10-07） ==========
   取证：out/xfont_ft_p2_ab/report.md §3.3/§4/§6 遗留-2——LIGHT autohint
   在 8~12px 极小 ppem 对 CJK 少笔画/细笔画做 stem 压缩与对齐，细笔画整
   根对没或整字压灰：山 U+5C71@8px 三竖笔全失、二 U+4E8C@10px 双横全灰
   （A 类"整字发灰"，当时对旧 xfo1 档取证为净新增项——xfo1 档已移除；
   10x 图证 png/ab_gate_A_*_10x.png）；px12 门检 E-literal 全灰横带
   2558→4428
   （+73%）与"发灰发虚"观感同向；ink 分水岭在 12→16px（≤12px 全面偏浅
   −3～−13/255，≥16px 反转偏浓）。
   修复（report §6-2 与 §5 一致指向"按字号分档"）：px < 阈值改
   FT_LOAD_NO_HINTING——纯反走样保笔画完整，接受轻微发虚；px ≥ 阈值
   维持 FT_LOAD_TARGET_LIGHT（+autohint）——16px 密笔画字 hinting 的
   真实收益段（疆 ink +22.5、灰带 −44.7%，report §4-3）不受扰动。
   阈值缺省 13（px ≤12 无 hint）的实测依据：A 类丢笔字全部落在
   8/10px；12px 是 E-literal 恶化峰值档（+37.8% bad）；16px 是 hinting
   收益档——三证据夹逼，无 13~15px 单档数据时取保守边界（只把已知
   受害档切出，收益档 16px 原样保留）。门检只存在 8/10/12/16 档位，
   13~15px 无单档实测，属阈值缺省的已知边界而非实测结论。 */
#ifndef XFONT_FT_HINT_MIN_PX
#define XFONT_FT_HINT_MIN_PX 13
#endif

/** @brief XFONT_FT_HINT_MIN_PX 首读：px < 本值走 FT_LOAD_NO_HINTING
 *         分档（slotLoadGlyph 只落 load_flags 位，不触 face/字号状态
 *         ——家族槽模型下 hint 档切换零重建，与 XFONT_FT_AUTOHINT 同
 *         口径）。env "0" 关分档（全字号恢复 LIGHT，修复前行为的
 *         A/B bisect 开关）；负值/非法回退缺省宏。 */
uint32_t XFontFt_hintMinPx(void)
{
    static long cached = -1;
    if (cached < 0)
    {
        const char* v = XSystem_environment("XFONT_FT_HINT_MIN_PX");
        long value = -1;
        if (v && v[0])
        {
            char* end = NULL;
            value = strtol(v, &end, 10);
            if (end == v || *end != '\0' || value < 0 || value > 1024)
                value = -1; /* 非法：回退缺省宏（不落 0，防误关）。 */
        }
        cached = value >= 0 ? value : (long)XFONT_FT_HINT_MIN_PX;
    }
    return (uint32_t)cached;
}

/* ========== 候选路径迭代（§3.1，独立实现） ==========
   （[已移除] 原注"形态对照 XFont.c XFontOutlinePathIter"——XFO1 外挂
   迭代器已随自研轮廓字实现删除，本迭代器成为唯一候选枚举实现。）
   直接路径/带后缀输入（含分隔符、盘符或 .ttf/.otf/.ttc 后缀）：
     0/1/2 = 原样（无后缀时补 .ttf/.otf/.ttc）
     3/4/5 = exe 目录前缀 + 同上（相对路径时）
   普通家族名：
     0/1/2 = dir/<family>.{ttf,otf,ttc}
     3/4/5 = exe 目录 + dir + family.{ttf,otf,ttc}（dir 相对时）
   ——与 §3.2 成本口径一致：3 后缀 × 原样/exe 前缀两档 = 每家族探测
   6 次 open + 每候选一次 4 字节定长首读，无整读落负路径。 */
#define XFONT_FT_SUFFIX_COUNT 3
static const char* const g_ftSuffixes[XFONT_FT_SUFFIX_COUNT] =
{
    ".ttf", ".otf", ".ttc"
};

typedef struct XFontFtPathIter
{
    int m_index;
    bool m_direct;      /**< 含分隔符/盘符或已带字体后缀（两者同义处理）。 */
    bool m_suffix;      /**< 直接路径已带 .ttf/.otf/.ttc 后缀（不再补探）。 */
    bool m_dirRelative;
    const char* m_family;
    const char* m_exeDir;
} XFontFtPathIter;

/** @brief 4 字节后缀比较（.ttf/.otf/.ttc 等长后缀，大小写敏感）。 */
static bool XFontFt_suffixEqual(const char* tail, const char* suffix)
{
    size_t i;
    for (i = 0; i < 3u; ++i)
        if (tail[i] != suffix[i])
            return false;
    return true;
}

/** @brief 直接路径是否已带 .ttf/.otf/.ttc 后缀（带则候选不再补探）。 */
static bool XFontFt_hasFontSuffix(const char* name)
{
    size_t length;
    int i;
    if (!name)
        return false;
    length = strlen(name);
    if (length < 4u)
        return false;
    for (i = 0; i < XFONT_FT_SUFFIX_COUNT; ++i)
        if (XFontFt_suffixEqual(name + length - 4u, g_ftSuffixes[i]))
            return true;
    return false;
}

/** @brief 输入是否为直接路径：含分隔符/盘符，或已带 ttf/otf/ttc 后缀。 */
static bool XFontFt_isDirectPath(const char* name)
{
    const char* p;
    size_t length;
    if (!name || !name[0])
        return false;
    for (p = name; *p; ++p)
        if (*p == '/' || *p == '\\' || *p == ':')
            return true;
    length = strlen(name);
    return length >= 4u && name[length - 4u] == '.';
}

static bool XFontFt_isAbsPath(const char* path)
{
    if (!path || !path[0])
        return false;
    if (path[0] == '/' || path[0] == '\\')
        return true;
    return ((path[0] >= 'A' && path[0] <= 'Z') ||
            (path[0] >= 'a' && path[0] <= 'z')) && path[1] == ':';
}

static bool XFontFt_strCopy(char* buf, size_t cap, const char* s)
{
    size_t length = strlen(s);
    if (length + 1u > cap)
        return false;
    memcpy(buf, s, length + 1u);
    return true;
}

/* buf = a/b（a 尾部已有分隔符则不重复）；截断返回 false。 */
static bool XFontFt_pathJoin(char* buf, size_t cap, const char* a,
                             const char* b)
{
    size_t la = strlen(a);
    size_t lb = strlen(b);
    bool sep = la > 0u && a[la - 1u] != '/' && a[la - 1u] != '\\';
    if (la + (sep ? 1u : 0u) + lb + 1u > cap)
        return false;
    memcpy(buf, a, la);
    if (sep)
        buf[la] = '/';
    memcpy(buf + la + (sep ? 1u : 0u), b, lb + 1u);
    return true;
}

static bool XFontFt_pathAppend(char* buf, size_t cap, const char* ext)
{
    size_t len = strlen(buf);
    size_t add = strlen(ext);
    if (len + add + 1u > cap)
        return false;
    memcpy(buf + len, ext, add + 1u);
    return true;
}

static void XFontFt_pathIterInit(XFontFtPathIter* it, const char* family,
                                 const char* exeDir)
{
    const char* dir = XFONT_EXTERNAL_FT_FONT_DIR;
    it->m_index = 0;
    it->m_family = family;
    it->m_exeDir = exeDir;
    it->m_direct = XFontFt_isDirectPath(family);
    it->m_suffix = it->m_direct && XFontFt_hasFontSuffix(family);
    it->m_dirRelative = dir && dir[0] && !XFontFt_isAbsPath(dir);
}

/* 候选 idx ∈ [0, 5]：低半段原样/dir 前缀，高半段 exe 前缀变体；
   不适用（截断/绝对路径不加前缀/dir 绝对）的候选返回 false 跳过。 */
static bool XFontFt_pathBuild(const XFontFtPathIter* it, int idx,
                              char* buf, size_t cap)
{
    const char* dir = XFONT_EXTERNAL_FT_FONT_DIR;
    const char* fam = it->m_family;
    int suffix = idx % XFONT_FT_SUFFIX_COUNT;
    bool exeVariant = idx >= XFONT_FT_SUFFIX_COUNT;
    /* 直接路径带字体后缀=只探自身；无后缀=补探 .ttf/.otf/.ttc（头注
       「无后缀时补后缀」的实现兑现——无后缀直连家族串即可命中同目录
       任一后缀字体（demo 扩展名无关接入即此））；普通家族名恒补后缀。 */
    const char* ext = (it->m_direct && it->m_suffix) ? "" : g_ftSuffixes[suffix];
    buf[0] = '\0';
    if (it->m_direct)
    {
        /* 直接路径：原样（无后缀补后缀）→ exe 目录前缀变体（相对路径时）。 */
        if (!exeVariant)
            return XFontFt_strCopy(buf, cap, fam) &&
                   XFontFt_pathAppend(buf, cap, ext);
        return it->m_exeDir && !XFontFt_isAbsPath(fam) &&
               XFontFt_pathJoin(buf, cap, it->m_exeDir, fam) &&
               XFontFt_pathAppend(buf, cap, ext);
    }
    /* 普通家族名：dir/family.{ttf,otf,ttc} → exe 前缀变体（dir 相对时）。 */
    if (!exeVariant)
        return dir && XFontFt_pathJoin(buf, cap, dir, fam) &&
               XFontFt_pathAppend(buf, cap, ext);
    {
        char rel[XFONT_EXTERNAL_FONT_PATH_MAX];
        return it->m_exeDir && it->m_dirRelative && dir &&
               XFontFt_pathJoin(rel, sizeof(rel), dir, fam) &&
               XFontFt_pathJoin(buf, cap, it->m_exeDir, rel) &&
               XFontFt_pathAppend(buf, cap, ext);
    }
}

static bool XFontFt_pathNext(XFontFtPathIter* it, char* buf, size_t cap)
{
    while (it->m_index < 2 * XFONT_FT_SUFFIX_COUNT)
    {
        int idx = it->m_index++;
        if (XFontFt_pathBuild(it, idx, buf, cap) && buf[0])
            return true;
    }
    return false;
}

/* ========== 4 字节魔数首读（§3.1 有界首读） ==========
   识别 ttf(00 01 00 00)/"true"/"OTTO"/"ttcf"；魔数不符按"非候选"处理
   （同缺文件，可重探）。禁止对候选直接整读——损坏/异构大文件（Noto
   .ttc 为 10-20MB 级）不得落在逐字形热路径上反复整读。 */

/** @brief 4 字节定长首读；../ 前缀失败回退与 XFont_readFileBytes 同款，
 *         保证探到的路径即整读路径。 */
static bool XFontFt_magicRead(const char* path, unsigned char magic[4])
{
    XString* name = NULL;
    XFile* file = NULL;
    int64_t got;
    if (!path || !path[0])
        return false;
    name = XString_create_utf8(path);
    file = name ? XFile_create() : NULL;
    if (!name || !file)
        goto failed;
    XFile_setFileName(file, name);
    if (!XFile_open_2(file, XIODevice_ReadOnly, 0))
    {
        if (path[0] == '.' && path[1] == '.' &&
            (path[2] == '/' || path[2] == '\\'))
        {
            const char* alternatePath = path + 3;
            XClassDelete((XClass*)file);
            XClassDelete((XClass*)name);
            file = NULL;
            name = XString_create_utf8(alternatePath);
            file = name ? XFile_create() : NULL;
            if (!name || !file)
                goto failed;
            XFile_setFileName(file, name);
            if (!XFile_open_2(file, XIODevice_ReadOnly, 0))
                goto failed;
        }
        else
            goto failed;
    }
    got = XIODevice_read_1((XIODevice*)file, (char*)magic, 4);
    XIODevice_close_base((XIODevice*)file);
    XClassDelete((XClass*)file);
    XClassDelete((XClass*)name);
    return got == 4;
failed:
    if (file) XClassDelete((XClass*)file);
    if (name) XClassDelete((XClass*)name);
    return false;
}

/** @brief sfnt 魔数白名单：ttf/"true"/OTTO/ttc。 */
static bool XFontFt_magicOk(const unsigned char m[4])
{
    if (m[0] == 0x00 && m[1] == 0x01 && m[2] == 0x00 && m[3] == 0x00)
        return true;                                    /* TrueType */
    if (memcmp(m, "true", 4) == 0)
        return true;                                    /* Apple TrueType */
    if (memcmp(m, "OTTO", 4) == 0)
        return true;                                    /* OpenType/CFF */
    if (memcmp(m, "ttcf", 4) == 0)
        return true;                                    /* TrueType 集合 */
    return false;
}

/* ========== 家族槽（§2.2：family → 常驻 FT_Face + 当前字号） ==========
   遗留-1（P0）重构（2026-10-07，替换原 "(px26_6,autohint) 4 路 LRU 各持
   FT_Face" 模型）：原模型多字号交错负载下每次加载必逐出+换入，换入即
   FT_New_Memory_Face 对整个 TTC/CFF 全量重析（19.5MB Noto CJK 实测
   裸解析 1.16ms/次；6 字号交错 5.64ms/字形 = 同负载按字号分块 0.18ms
   的 31 倍，out/xfont_ft_p0fix/run/stress_prefix.log）。现模型：
   同一家族进程期只 FT_New_Memory_Face 一次（度量读定与字形供货共用
   该 face，blob 同槽常驻），字号切换一律 FT_Set_Char_Size——驱动仅
   重算尺度/全局度量，不触碰表区，对大文件是廉价操作；autohint 与
   face 生命周期正交，只进 FT_Load_Glyph 的 load_flags 位（进程期
   常量，XFONT_FT_AUTOHINT 首读），切换不重建 face。
   家族槽仍只增不复用：XFontFace* 静态对象与 FT_Face 进程期永稳
   （painter 各缓存以 face 指针为家族身份锚，§2.2 原理由来）。 */

typedef struct XFontFtFamilySlot
{
    char m_family[XFONT_EXTERNAL_FONT_PATH_MAX]; /**< 家族名（=候选输入原样）。 */
    XByteArray* m_blob;     /**< 整文件字节（XFont_readFileBytes 读入，进程期持有）。 */
    XFontOutlineFace m_face; /**< 静态 XObject；m_class.m_family 指向槽内名。 */
    XFontOutlineInfo m_info; /**< 全局度量（hhea 口径，槽装载时一次读定）。 */
    FT_Face m_ftFace;       /**< 家族常驻 face（每家族进程期恰一次 FT_New_Memory_Face）。 */
    uint32_t m_px26_6;      /**< m_ftFace 当前字号（26.6 直设值）；0=尚未设定。 */
} XFontFtFamilySlot;

static XFontFtFamilySlot g_ftFamilies[XFONT_FT_MAX_FAMILIES];
static size_t g_ftFamilyCount;
static bool g_ftFamilyFullWarned;

/* ========== 计数器（遗留-1 取证口径：换入换出消除的可测量证据） ==========
   faceCreates=家族数（重构成立）；charSizeSets/charSizeSkips=字号切换
   两去路（设计§1.2 的 26.6 直设值不变时零开销）。单 UI 线程，无锁。 */
static XFontFtStats g_ftStats;

void XFontFt_stats(XFontFtStats* out)
{
    if (out)
        *out = g_ftStats;
}

/* 解析失败家族锁存（§3.1）：魔数通过但 FT_New_Memory_Face 失败 → 按家族
   名锁存，进程期不再触碰文件（损坏不会自愈）。文件不存在/魔数不符不
   锁存（时序自愈；缺文件不锁存的可重探哲学与原 XFO1 负缓存设计一致）。
   容量独立于家族槽
   （解析失败不占槽），溢出后新家族退化为可重探（有界首读兜底代价）。 */
#define XFONT_FT_FAILED_FAMILY_MAX (2 * XFONT_FT_MAX_FAMILIES)
static char g_ftFailedFamilies[XFONT_FT_FAILED_FAMILY_MAX]
                              [XFONT_EXTERNAL_FONT_PATH_MAX];

static bool XFontFt_familyLatched(const char* family)
{
    int i;
    for (i = 0; i < XFONT_FT_FAILED_FAMILY_MAX; ++i)
        if (g_ftFailedFamilies[i][0] &&
            strcmp(g_ftFailedFamilies[i], family) == 0)
            return true;
    return false;
}

static void XFontFt_latchFamily(const char* family)
{
    int i;
    for (i = 0; i < XFONT_FT_FAILED_FAMILY_MAX; ++i)
        if (!g_ftFailedFamilies[i][0])
        {
            XFontFt_strCopy(g_ftFailedFamilies[i],
                            sizeof(g_ftFailedFamilies[i]), family);
            XPrintf("XFontFt: 字体解析失败，家族 '%s' 已锁存回退（进程期）\n",
                    family);
            return;
        }
    /* 锁存表满：该家族保持可重探（每次探测多付一次有界首读+整读+FT
       拒载，量级可接受），不再追加警示。 */
}

static XFontFtFamilySlot* XFontFt_findFamily(const char* family)
{
    size_t i;
    for (i = 0; i < g_ftFamilyCount; ++i)
        if (strcmp(g_ftFamilies[i].m_family, family) == 0)
            return &g_ftFamilies[i];
    return NULL;
}

/* 前置声明：字重变体探测（下方）引用后文的槽填充与家族名取值。 */
static XFontFtFamilySlot* XFontFt_ensureFamily(const char* family);
static const char* XFontFt_familyOf(const XFont* font);

/* ========== 字重文件变体（2026-10-08 用户指令"补一下"） ==========
   Noto Sans CJK 的 Bold 与 Regular 是两个独立文件（非同 TTC 混装），
   仅按家族名探测永远拿不到真 Bold 字形。粗体字体（weight>=DemiBold）
   在原家族名探测成功后追加探测 Bold 变体名（<family>-Bold/_Bold/Bold
   后缀族），命中则独立开槽（槽键=变体名）；未命中回落 Regular 槽+
   既有合成粗体（γ3.2 曲线，兜底不变）。斜体不做文件变体：CJK 字体
   几乎无真斜体文件，painter 合成斜切（§8.0g11）在 FT 路径同样生效。 */
#define XFONT_FT_BOLD_SUFFIX_COUNT 3
static const char* const g_ftBoldSuffixes[XFONT_FT_BOLD_SUFFIX_COUNT] =
{
    "-Bold", "_Bold", "Bold"
};

/** @brief 家族名是否以 Bold 变体后缀收尾（字重变体槽判定）。 */
static bool XFontFt_hasBoldSuffix(const char* name)
{
    size_t length;
    int i;
    if (!name)
        return false;
    length = strlen(name);
    for (i = 0; i < XFONT_FT_BOLD_SUFFIX_COUNT; ++i)
    {
        const char* suffix = g_ftBoldSuffixes[i];
        size_t slen = strlen(suffix);
        if (length > slen &&
            strcmp(name + length - slen, suffix) == 0)
            return true;
    }
    return false;
}

/** @brief 粗体变体家族名：探测顺序=变体名优先、原家族名兜底。
 *  @param out 变体家族名缓冲（命中 Bold 文件时=带 -Bold 后缀的探测名，
 *             未命中=原家族名——调用方以此决定开哪个槽）。 */
static XFontFtFamilySlot* XFontFt_ensureFamilyWeighted(const XFont* font,
                                                       char* out,
                                                       size_t outCap)
{
    const char* family = XFontFt_familyOf(font);
    int weight = font ? XFont_weight(font) : 0;
    size_t n = strlen(family);
    int i;
    if (weight < XFont_Bold || n == 0 ||
        n + 5 >= XFONT_EXTERNAL_FONT_PATH_MAX)
    {
        if (out && outCap) XFontFt_strCopy(out, outCap, family);
        return XFontFt_ensureFamily(family);
    }
    for (i = 0; i < XFONT_FT_BOLD_SUFFIX_COUNT; ++i)
    {
        char variant[XFONT_EXTERNAL_FONT_PATH_MAX];
        XFontFtFamilySlot* slot;
        if (!XFontFt_strCopy(variant, sizeof(variant), family))
            break;
        if (!XFontFt_pathAppend(variant, sizeof(variant),
                                g_ftBoldSuffixes[i]))
            break;
        slot = XFontFt_ensureFamily(variant); /* 只探测不回落注册名。 */
        if (slot)
        {
            if (out && outCap) XFontFt_strCopy(out, outCap, variant);
            return slot; /* 真 Bold 文件命中：独立槽。 */
        }
    }
    if (out && outCap) XFontFt_strCopy(out, outCap, family);
    return XFontFt_ensureFamily(family); /* Bold 文件缺席：Regular+合成。 */
}

/** @brief provider 保底桩：家族槽 face 不注册进解析表，m_ft 分支
 *         （P2.3）已接管全部加载分发；此桩仅为防误分派兜底——若未来
 *         分发逻辑漏查 m_ft 落到 provider 分支，此处返回 false 而非
 *         空指针解引用。 */
static bool XFontFt_providerStub(uint32_t codepoint,
                                 XFontOutlineGlyphMetrics* metrics,
                                 const XFontOutlineSink* sink, void* userData)
{
    (void)codepoint; (void)metrics; (void)sink; (void)userData;
    return false;
}

/* 家族名 → 全局度量取自 face 全局字段（freetype.h:1170-1184：ascender/
   descender/height 均为 font units），换算式照 §2.3，校验照
   XFontOutlineFace_validProvider（XFontOutlineFace.c:16-27）口径。

   [行度量覆盖档 2026-10-07] hhea 之后按 OS/2 表覆盖（对标 Qt 6.8.3
   QFontEngine::initializeHeightMetrics 的 processHheaTable→processOS2Table
   次序）：fsSelection bit7（USE_TYPO_METRICS）或调用方字体策略含
   XFont_PreferTypoLineMetrics → 取 sTypo 三元组；否则取 usWinAscent/
   usWinDescent 且 leading=0（Qt win 档 lineGap 归零口径）。OS/2 缺失/
   非法回落 hhea。现役部署字体（XFontOutlineCommon.ttc）hhea 恰与 OS/2
   win 一致，本档对存量渲染零扰动；换 win/typo≠hhea 字体后与 Qt 系统性
   对齐。g_ftTypoPreferred 由家族槽首次填充的触发字体策略置位
   （XFontFt_fileInfo，单线程注册语义）。 */
static bool g_ftTypoPreferred = false;

static bool XFontFt_readGlobalMetrics(FT_Face face, XFontOutlineInfo* info)
{
    int upem = (int)face->units_per_EM;
    int ascent = (int)face->ascender;
    int descent = -(int)face->descender; /* FT descender 为负，接口要求 >=0。 */
    int lineGap = (int)face->height - (int)face->ascender + (int)face->descender;
    if (!(face->face_flags & FT_FACE_FLAG_SCALABLE) ||
        upem < 1 || upem > 65535 ||
        ascent < 0 || descent < 0 ||
        ascent > 32767 || descent > 32767)
        return false;
    if (lineGap < 0)
        lineGap = 0;
    if (lineGap > 32767)
        lineGap = 32767;
    {
        TT_OS2* os2 = (TT_OS2*)FT_Get_Sfnt_Table(face, ft_sfnt_os2);
        if (os2 && os2->version != 0xFFFF)
        {
            bool typo = (os2->fsSelection & 0x80) != 0 || g_ftTypoPreferred;
            int a = typo ? (int)os2->sTypoAscender : (int)os2->usWinAscent;
            int d = typo ? -(int)os2->sTypoDescender : (int)os2->usWinDescent;
            int g = typo ? (int)os2->sTypoLineGap : 0;
            /* typo 档占位数据（v0 表全零）与越界值一律弃用，回落 hhea。 */
            if (a > 0 && d >= 0 && a <= 32767 && d <= 32767)
            {
                ascent = a;
                descent = d;
                lineGap = g < 0 ? 0 : (g > 32767 ? 32767 : g);
            }
        }
    }
    info->unitsPerEm = upem;
    info->ascent = ascent;
    info->descent = descent;
    info->lineGap = lineGap;
    return true;
}

/* 候选迭代 + 魔数首读 + 整读 + FT 解析；成功填槽返回，失败按 §3.1 语义
   处理（缺文件/魔数不符不锁存；FT 拒载锁存家族）。遗留-1 重构后此处
   是全后端唯一的 FT_New_Memory_Face 站点：成功路径 face 常驻入槽（度量
   读定与后续字形供货共用），失败路径保持「先 FT_Done_Face 后弃 blob」
   红线次序。 */
static XFontFtFamilySlot* XFontFt_fillFamily(const char* family)
{
    XFontFtPathIter it;
    char exeDir[XFONT_EXTERNAL_FONT_PATH_MAX];
    char path[XFONT_EXTERNAL_FONT_PATH_MAX];
    bool hasExeDir;
    FT_Library library = XFontFt_library();
    if (!library)
        return NULL;
    if (strlen(family) >= XFONT_EXTERNAL_FONT_PATH_MAX)
        return NULL; /* 名字放不进槽，按非候选处理。 */
    /* [死码接线] 解析失败锁存命中：进程期不再触碰文件（§3.1 时序自愈
     * 语义；此前负缓存只写不读，XFontFt_familyLatched 成孤立函数）。 */
    if (XFontFt_familyLatched(family))
        return NULL;
    /* exe 目录：applicationDirPath 每次返回调用方拥有的新 XString
     * （XCoreApplication.c:275/292，XFont_exeDir 同款用后即删），取值后
     * 必须释放——双调用会泄漏且悬空引用。 */
    {
        const XString* dir = XCoreApplication_applicationDirPath();
        const char* utf8 = dir ? XString_toUtf8(dir) : NULL;
        hasExeDir = utf8 && utf8[0] &&
                    XFontFt_strCopy(exeDir, sizeof(exeDir), utf8);
        if (dir)
            XClassDelete((XClass*)dir);
    }
    XFontFt_pathIterInit(&it, family, hasExeDir ? exeDir : NULL);
    while (XFontFt_pathNext(&it, path, sizeof(path)))
    {
        unsigned char magic[4];
        XByteArray* bytes = NULL;
        FT_Face face = NULL;
        XFontFtFamilySlot* slot;
        XFontOutlineInfo info;
        if (!XFontFt_magicRead(path, magic) || !XFontFt_magicOk(magic))
            continue;                       /* 非候选：同缺文件，可重探。 */
        if (!XFont_readFileBytes(path, &bytes) || !bytes)
            continue;                       /* 首读/整读间隙文件消失：重探。 */
        /* blob 生命周期与 face 绑定在槽内（§2.2），进程期同存同亡；
           红线：FT_Done_Face 只允许出现在槽建立前的失败路径，且必须
           先于弃 blob（下方两处失败分支即此序）。 */
        if (FT_New_Memory_Face(library, XByteArray_data(bytes),
                               (FT_Long)XByteArray_size_base(
                                   (XContainer*)bytes),
                               (FT_Long)XFontFt_faceIndex(), &face) != 0 ||
            !face)
        {
            XClassDelete((XClass*)bytes);
            XFontFt_latchFamily(family);    /* 解析失败：进程期锁存。 */
            return NULL;
        }
        ++g_ftStats.m_faceCreates;
        if (!XFontFt_readGlobalMetrics(face, &info))
        {
            /* 非可缩放/度量越界 = 家族解析失败，同 FT 拒载锁存。 */
            FT_Done_Face(face);
            XClassDelete((XClass*)bytes);
            XFontFt_latchFamily(family);
            return NULL;
        }
        if (g_ftFamilyCount >= XFONT_FT_MAX_FAMILIES)
        {
            /* 家族槽满：FT 判失败，调用方（XFont_face）落位图回退，
               一次性警示。 */
            if (!g_ftFamilyFullWarned)
            {
                XPrintf("XFontFt: 家族槽满（%d），新家族回退位图字库\n",
                        (int)XFONT_FT_MAX_FAMILIES);
                g_ftFamilyFullWarned = true;
            }
            FT_Done_Face(face);
            XClassDelete((XClass*)bytes);
            return NULL;
        }
        slot = &g_ftFamilies[g_ftFamilyCount];
        memset(slot, 0, sizeof(*slot));
        XFontFt_strCopy(slot->m_family, sizeof(slot->m_family), family);
        slot->m_blob = bytes;
        slot->m_info = info;
        /* 字重变体槽（2026-10-08）：Bold 后缀名命中的文件=真 Bold 字体，
           painter 据此跳过合成粗体加浓（防真 Bold+γ3.2 双重加粗）。 */
        slot->m_info.m_ftRealBold = XFontFt_hasBoldSuffix(family);
        slot->m_ftFace = face; /* 家族常驻 face（不再 Done，blob 随槽进程期持有）。 */
        slot->m_px26_6 = 0u;   /* 字号未设定：首次字形加载时 FT_Set_Char_Size。 */
        XFontOutlineFace_init(&slot->m_face, NULL);
        slot->m_face.m_ft = true; /* P2.3：三分支分发的 FT 文件后端位（§2.1）。 */
        slot->m_face.m_provider.m_family = slot->m_family;
        slot->m_face.m_provider.m_info = slot->m_info;
        slot->m_face.m_provider.m_loadGlyph = XFontFt_providerStub;
        slot->m_face.m_class.m_family = slot->m_family;
        ++g_ftFamilyCount;
        ++g_ftStats.m_familyFills;
        return slot;
    }
    return NULL; /* 全候选落空 = 缺文件：不锁存（时序自愈）。 */
}

/* find-or-fill：命中即返（纯 strcmp 扫描，零 open/零读——家族解析
   成功路径进程期只 open/整读一次，即 fillFamily 入槽那一次，遗留-1
   方向 3 的成功路径锁存语义由家族槽本身兑现）；未命中且槽有余则
   装载（失败按 §3.1：缺文件/魔数不符不锁存可重探，FT 拒载锁存）。 */
static XFontFtFamilySlot* XFontFt_ensureFamily(const char* family)
{
    XFontFtFamilySlot* slot = XFontFt_findFamily(family);
    if (slot)
        return slot;
    if (g_ftFamilyCount >= XFONT_FT_MAX_FAMILIES)
    {
        if (!g_ftFamilyFullWarned)
        {
            XPrintf("XFontFt: 家族槽满（%d），新家族回退位图字库\n",
                    (int)XFONT_FT_MAX_FAMILIES);
            g_ftFamilyFullWarned = true;
        }
        return NULL;
    }
    return XFontFt_fillFamily(family);
}

/* ========== 字号切换（遗留-1 P0 重构：family→face + 当前字号） ==========
   家族 face 常驻（fillFamily 唯一创建站点），字号未变原样返回（零
   开销热路径）；变更走 FT_Set_Char_Size——驱动仅重算尺度/全局度量，
   不重读表区，19.5MB CFF TTC 上实测字形供给 0.18ms（stress 工具
   phaseC），与文件大小无关。原 "(px26_6,autohint) LRU 逐出重建" 路径
   整体删除：逐出 FT_Done_Face / 换入 FT_New_Memory_Face 全消失。 */
static FT_Face XFontFt_slotFaceForSize(XFontFtFamilySlot* slot,
                                       uint32_t px26_6)
{
    if (!slot || !slot->m_blob || !slot->m_ftFace)
        return NULL;
    if (slot->m_px26_6 == px26_6)
    {
        ++g_ftStats.m_charSizeSkips;
        return slot->m_ftFace;
    }
    /* 26.6 直设（§1.2），不用 FT_Set_Pixel_Sizes 的取整。 */
    if (FT_Set_Char_Size(slot->m_ftFace, 0, (FT_F26Dot6)px26_6, 72, 72) != 0)
    {
        /* 设定失败：字号状态置回未设定，下次加载重设（face 本体仍可用，
           绝不因字号失败 Done——blob/face 进程期同存同亡）。 */
        slot->m_px26_6 = 0u;
        return NULL;
    }
    slot->m_px26_6 = px26_6;
    ++g_ftStats.m_charSizeSets;
    return slot->m_ftFace;
}

/* ========== 26.6 像素 → 设计单位换算（§2.3 度量 / §1.2 闭合公式） ==========
   design = (v26_6/64)px * upem / (px26_6_set/64)px = v26_6 * upem / px26_6_set
   （double 域）。恒等式只要求 painter_scale ≡ px/upem；provider 设定字号
   与 painter 目标一致时设备几何 = hinted_px * target/px 逐位复原。 */

static double XFontFt_upemOverPx26_6(int upem, uint32_t px26_6)
{
    return (double)upem / (double)px26_6;
}

static int XFontFt_26_6ToDesignInt(double v26_6, double upemOverPx)
{
    double d = v26_6 * upemOverPx;
    return (int)floor(d + 0.5);
}

static float XFontFt_26_6ToDesignFloat(double v26_6, double upemOverPx)
{
    return (float)(v26_6 * upemOverPx);
}

/* ========== FT_Outline_Decompose → XFontOutlineSink（§2.3） ==========
   FT 与接口同为 y-up，无翻转；move/line/conic(→quadTo)/cubic 直映五回调，
   每条轮廓结束补 close（FT 回调无显式轮廓结束通知，由状态机补发）。 */

typedef struct XFontFtDecomposeState
{
    const XFontOutlineSink* m_sink;
    double m_upemOverPx; /**< upem / px26_6_set（预先约简的换算系数）。 */
    bool m_open;         /**< 当前轮廓未闭合。 */
    bool m_aborted;      /**< 回调拒绝（如建路径上限），整次加载判失败。 */
} XFontFtDecomposeState;

static int XFontFt_ftMoveTo(const FT_Vector* to, void* user)
{
    XFontFtDecomposeState* st = (XFontFtDecomposeState*)user;
    if (st->m_aborted)
        return 1;
    if (st->m_open && st->m_sink->close &&
        !st->m_sink->close(st->m_sink->userData))
    {
        st->m_aborted = true;
        return 1;
    }
    st->m_open = true;
    if (st->m_sink->moveTo &&
        !st->m_sink->moveTo(st->m_sink->userData,
                            XFontFt_26_6ToDesignFloat(to->x, st->m_upemOverPx),
                            XFontFt_26_6ToDesignFloat(to->y, st->m_upemOverPx)))
    {
        st->m_aborted = true;
        return 1;
    }
    return 0;
}

static int XFontFt_ftLineTo(const FT_Vector* to, void* user)
{
    XFontFtDecomposeState* st = (XFontFtDecomposeState*)user;
    if (st->m_aborted)
        return 1;
    if (st->m_sink->lineTo &&
        !st->m_sink->lineTo(st->m_sink->userData,
                            XFontFt_26_6ToDesignFloat(to->x, st->m_upemOverPx),
                            XFontFt_26_6ToDesignFloat(to->y, st->m_upemOverPx)))
    {
        st->m_aborted = true;
        return 1;
    }
    return 0;
}

static int XFontFt_ftConicTo(const FT_Vector* control, const FT_Vector* to,
                             void* user)
{
    XFontFtDecomposeState* st = (XFontFtDecomposeState*)user;
    if (st->m_aborted)
        return 1;
    if (st->m_sink->quadTo &&
        !st->m_sink->quadTo(st->m_sink->userData,
                            XFontFt_26_6ToDesignFloat(control->x,
                                                      st->m_upemOverPx),
                            XFontFt_26_6ToDesignFloat(control->y,
                                                      st->m_upemOverPx),
                            XFontFt_26_6ToDesignFloat(to->x, st->m_upemOverPx),
                            XFontFt_26_6ToDesignFloat(to->y, st->m_upemOverPx)))
    {
        st->m_aborted = true;
        return 1;
    }
    return 0;
}

static int XFontFt_ftCubicTo(const FT_Vector* c1, const FT_Vector* c2,
                             const FT_Vector* to, void* user)
{
    XFontFtDecomposeState* st = (XFontFtDecomposeState*)user;
    if (st->m_aborted)
        return 1;
    if (st->m_sink->cubicTo &&
        !st->m_sink->cubicTo(st->m_sink->userData,
                             XFontFt_26_6ToDesignFloat(c1->x, st->m_upemOverPx),
                             XFontFt_26_6ToDesignFloat(c1->y, st->m_upemOverPx),
                             XFontFt_26_6ToDesignFloat(c2->x, st->m_upemOverPx),
                             XFontFt_26_6ToDesignFloat(c2->y, st->m_upemOverPx),
                             XFontFt_26_6ToDesignFloat(to->x, st->m_upemOverPx),
                             XFontFt_26_6ToDesignFloat(to->y, st->m_upemOverPx)))
    {
        st->m_aborted = true;
        return 1;
    }
    return 0;
}

static bool XFontFt_decompose(const FT_Outline* outline,
                              const XFontOutlineSink* sink,
                              double upemOverPx)
{
    /* shift=0/delta=0：26.6 坐标直通回调（ftimage.h:669-679 字段序）。 */
    static const FT_Outline_Funcs funcs =
    {
        XFontFt_ftMoveTo, XFontFt_ftLineTo,
        XFontFt_ftConicTo, XFontFt_ftCubicTo,
        0, 0
    };
    XFontFtDecomposeState st;
    st.m_sink = sink;
    st.m_upemOverPx = upemOverPx;
    st.m_open = false;
    st.m_aborted = false;
    if (FT_Outline_Decompose((FT_Outline*)outline, &funcs, &st) != 0)
        return false;
    if (st.m_aborted)
        return false;
    if (st.m_open && sink->close && !sink->close(sink->userData))
        return false;
    return true;
}

/* ========== 字形加载（§2.3 档位 + §1.2 字号复原） ========== */

static bool XFontFt_slotLoadGlyph(XFontFtFamilySlot* slot, float scale,
                                  uint32_t codepoint,
                                  XFontOutlineGlyphMetrics* metrics,
                                  const XFontOutlineSink* sink)
{
    FT_Face face;
    FT_GlyphSlot glyph;
    double upemOverPx;
    double px266;
    uint32_t px26_6;
    uint32_t hintMinPx;
    bool autohint;
    bool noHint;
    FT_UInt gindex;
    FT_Int32 loadFlags;
    FT_Error ftError;
    if (!slot || !(scale > 0.0f) || !isfinite((double)scale))
        return false;
    /* 字号复原（§1.2）：px26_6 = round(upem*scale*64)，double 域。
       全 upem 合法域内 float scale 的 ~1e-7 相对误差不改变 round 结果
       （target<=2^16 时恒等于 target<<6），无分档无 gate。 */
    px266 = floor((double)slot->m_info.unitsPerEm * (double)scale * 64.0 + 0.5);
    if (!(px266 >= 1.0) || px266 > 134217728.0 /* 26.6 安全上界 2^27 */)
        return false;
    px26_6 = (uint32_t)px266;
    /* autohint 与小字号分档都不参与 face/字号状态：只落在 load_flags
       位（下方），XFONT_FT_AUTOHINT/XFONT_FT_HINT_MIN_PX 切换与字号
       阈值跨越均不重建 face、不重设字号（遗留-1 重构方向 2；本文件
       「小字号 hint 分档」节）。 */
    autohint = XFontFt_autohintDefault();
    hintMinPx = XFontFt_hintMinPx();
    /* 26.6 整数比较等价于 px 比较（px26_6/64 < hintMinPx ⇔
       px26_6 < hintMinPx<<6，无浮点无取整歧义）。 */
    noHint = hintMinPx != 0u && px26_6 < (hintMinPx << 6);
    face = XFontFt_slotFaceForSize(slot, px26_6);
    if (!face)
        return false;
    /* 缺字形（cmap 无映射）：provider 返回 false，交既有 ASCII 代理表与
       空白占位口径；不做跨 face 逐字回退（§2.3/§4）。 */
    gindex = FT_Get_Char_Index(face, codepoint);
    if (gindex == 0)
        return false;
    if (noHint)
        /* 小字号档（遗留-2）：无 hint 纯反走样——stem 压缩/对齐整段
           关闭，保细笔画完整；advance/度量回到原轮廓，交上游网格对齐
           与 γ 加浓（XPainter gridfit/混合端）管浓淡与落格。 */
        loadFlags = FT_LOAD_DEFAULT | FT_LOAD_NO_BITMAP | FT_LOAD_NO_HINTING;
    else
        /* LIGHT 档：仅竖向网格适配；FT_RENDER 无需——只在 outline 域
           消费，覆盖图由阶段一 ftgrays 管线产出（§2.3）。 */
        loadFlags = FT_LOAD_DEFAULT | FT_LOAD_NO_BITMAP |
                    FT_LOAD_TARGET_LIGHT |
                    (autohint ? FT_LOAD_FORCE_AUTOHINT : 0);
    /* 临时态包住 load 本体：字形 scratch（glyph loader 扩容/TT 暂存/
       cf2 映射）即生即灭落混合池。已知例外：TTF bytecode 上下文在首次
       load 内惰性装配却随 face 常驻（缺省 Noto CJK 为 CFF 不触此路，
       见文件头注）；autohint 缺省关，autofit scratch 不发生。 */
    {
        bool tempPrev = XFontFt_tempScopeEnter();
        ftError = FT_Load_Glyph(face, gindex, loadFlags);
        XFontFt_tempScopeExit(tempPrev);
    }
    if (ftError != 0)
        return false;
    ++g_ftStats.m_glyphLoads;
    if (noHint)
        ++g_ftStats.m_glyphNoHintLoads;
    glyph = face->glyph;
    if (!glyph || glyph->format != FT_GLYPH_FORMAT_OUTLINE)
        return false;
    upemOverPx = XFontFt_upemOverPx26_6(slot->m_info.unitsPerEm, px26_6);
    if (metrics)
    {
        /* 度量换算（§2.3）：advance=hinted 26.6；盒 = bearing/width 组合。 */
        metrics->advance = XFontFt_26_6ToDesignInt(glyph->advance.x, upemOverPx);
        metrics->xMin = XFontFt_26_6ToDesignInt(glyph->metrics.horiBearingX,
                                                upemOverPx);
        metrics->xMax = XFontFt_26_6ToDesignInt(
            glyph->metrics.horiBearingX + glyph->metrics.width, upemOverPx);
        metrics->yMin = XFontFt_26_6ToDesignInt(
            glyph->metrics.horiBearingY - glyph->metrics.height, upemOverPx);
        metrics->yMax = XFontFt_26_6ToDesignInt(glyph->metrics.horiBearingY,
                                                upemOverPx);
    }
    if (sink)
        return XFontFt_decompose(&glyph->outline, sink, upemOverPx);
    return true;
}

/* 家族名取值：font 的家族串，空/NULL 落默认家族（原 XFO1 文件后端
   同口径；该后端已移除）。 */
static const char* XFontFt_familyOf(const XFont* font)
{
    const char* family = font ? XFont_family(font) : XFONT_DEFAULT_FAMILY;
    if (!family || !family[0])
        family = XFONT_DEFAULT_FAMILY;
    return family;
}

bool XFontFt_fileInfo(const XFont* font, XFontOutlineInfo* info)
{
    XFontFtFamilySlot* slot;
    if (!info)
        return false;
    /* [行度量接线] 家族槽首次填充的触发字体捐献 PreferTypoLineMetrics
     * 策略位（XFont.h:65 此前为死枚举）：readGlobalMetrics 的 OS/2 typo
     * 档据此取三元组。单线程注册语义（XFont_face 主线程解析），标志
     * 只影响首填时刻，槽固化后不再随字体变化——家族槽模型不动。 */
    g_ftTypoPreferred =
        (XFont_styleStrategy(font) & XFont_PreferTypoLineMetrics) != 0;
    slot = XFontFt_ensureFamilyWeighted(font, NULL, 0);
    if (!slot)
        return false;
    *info = slot->m_info;
    return true;
}

bool XFontFt_fileLoadGlyph(const XFont* font, uint32_t codepoint,
                           XFontOutlineGlyphMetrics* metrics,
                           const XFontOutlineSink* sink)
{
    XFontFtFamilySlot* slot = XFontFt_ensureFamilyWeighted(font, NULL, 0);
    /* 旧槽无字号载体，语义 = scale 1.0（px26_6 = upem<<6，§2.1）。 */
    return slot && XFontFt_slotLoadGlyph(slot, 1.0f, codepoint, metrics,
                                         sink);
}

bool XFontFt_fileLoadGlyphScaled(const XFont* font, float scale,
                                 uint32_t codepoint,
                                 XFontOutlineGlyphMetrics* metrics,
                                 const XFontOutlineSink* sink)
{
    XFontFtFamilySlot* slot = XFontFt_ensureFamilyWeighted(font, NULL, 0);
    return slot && XFontFt_slotLoadGlyph(slot, scale, codepoint, metrics,
                                         sink);
}

const XFontFace* XFontFt_fileFace(const char* family)
{
    XFontFtFamilySlot* slot;
    if (!family || !family[0])
        family = XFONT_DEFAULT_FAMILY;
    slot = XFontFt_findFamily(family); /* 只查不装载：fileInfo 先行（P2.4）。 */
    if (!slot)
        return NULL;
    return &slot->m_face.m_class;
}

#else /* !XFONT_FT_FACE_ON：face 面桩（接线代码任意档可编译） */

void XFontFt_stats(XFontFtStats* out)
{
    if (out)
        memset(out, 0, sizeof(*out));
}

bool XFontFt_fileInfo(const XFont* font, XFontOutlineInfo* info)
{
    (void)font; (void)info;
    return false;
}

bool XFontFt_fileLoadGlyph(const XFont* font, uint32_t codepoint,
                           XFontOutlineGlyphMetrics* metrics,
                           const XFontOutlineSink* sink)
{
    (void)font; (void)codepoint; (void)metrics; (void)sink;
    return false;
}

bool XFontFt_fileLoadGlyphScaled(const XFont* font, float scale,
                                 uint32_t codepoint,
                                 XFontOutlineGlyphMetrics* metrics,
                                 const XFontOutlineSink* sink)
{
    (void)font; (void)scale; (void)codepoint; (void)metrics; (void)sink;
    return false;
}

const XFontFace* XFontFt_fileFace(const char* family)
{
    (void)family;
    return NULL;
}

#endif /* XFONT_FT_FACE_ON */

#else /* !XFONT_FT_ON：face 全关档，全 API 桩形供给 */

bool XFontFt_fileInfo(const XFont* font, XFontOutlineInfo* info)
{
    (void)font; (void)info;
    return false;
}

bool XFontFt_fileLoadGlyph(const XFont* font, uint32_t codepoint,
                           XFontOutlineGlyphMetrics* metrics,
                           const XFontOutlineSink* sink)
{
    (void)font; (void)codepoint; (void)metrics; (void)sink;
    return false;
}

bool XFontFt_fileLoadGlyphScaled(const XFont* font, float scale,
                                 uint32_t codepoint,
                                 XFontOutlineGlyphMetrics* metrics,
                                 const XFontOutlineSink* sink)
{
    (void)font; (void)scale; (void)codepoint; (void)metrics; (void)sink;
    return false;
}

const XFontFace* XFontFt_fileFace(const char* family)
{
    (void)family;
    return NULL;
}

#endif /* XFONT_FT_ON */
