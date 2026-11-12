/**
 * @file       XIconThemeAlignTest.c
 * @brief      XIcon 主题引擎（XIconThemeEngine/XIconThemeInternal）与
 *             Qt 6.8 QIconLoader 行为级契约测试（运行期自造主题资产）。
 * @details    Test/ 全仓此前零 fromTheme 用例（2026-11-14 审计：grep 无命中），
 *             本文件补上主题引擎行为级覆盖。全部主题资产在**运行期**于
 *             XDir_tempPath() 下的**固定名**目录程序化构造（程序写文件、
 *             用后递归删除，零二进制入库、零环境依赖；手法源自
 *             Test/XDataTest/XFontFtTest.c:94 XFONTFT_TTF_CAP）：
 *               <root>/testtheme/index.theme    Directories=16x16/apps,32x32/apps
 *               <root>/testtheme/16x16/apps/a.png   16×16 红（0xFFD23F3F）
 *               <root>/testtheme/32x32/apps/a.png   32×32 蓝（0xFF3F6FD2）
 *               <root>/testtheme/32x32/apps/c.png   32×32 蓝
 *               <root>/secondtheme/index.theme  Inherits=testtheme + 48x48/apps
 *               <root>/secondtheme/48x48/apps/a.png 48×48 绿（0xFF3FD25A）
 *               <root>/hicolor/16x16/apps/a.png     16×16 黄（0xFFD2C23F）
 *             （hicolor 故意**不带 index.theme**——同时锁定引擎「无
 *             index.theme 时按内嵌 XDG 尺寸/上下文目录名匹配」的旧式路径，
 *             XIconThemeInternal.c:5-7 与 theme_size_dirs/theme_context_dirs
 *             两张内嵌表。）
 *             PNG 由框架自带编解码器生成（XImage_fill + XImage_save_2，
 *             XIMAGECODEC_PNG_ON 兜底默认开）——同一先例见
 *             XComboBoxAlignTest 的 itemIconSizeAndDecoration 探针。
 *
 *             【资产路径必须是固定名（每次运行复用同一路径），不能用
 *             时间戳唯一目录】解码图像走 XImageCache（path+format 键控、
 *             32 条目/1MB 预算 LRU，XImageCache.h:50-61；XImage_load_2 查/
 *             插缓存 XImage.c:4889-4894/:4952-4954；主题引擎经
 *             XPixmap_load_2→XImage_load_2 间接使用，XPixmap.c:873-879）。
 *             每个新路径首次解码向缓存插入一条 ~5.7KB 条目（32×32 ARGB
 *             深拷贝+簿记，有界、非所有权泄漏）；运行器零容忍泄漏门按
 *             「复核遍仍增长」判 [LEAK]——时间戳唯一目录每遍都新建路径
 *             必然每遍 +N×5.7KB 判死（2026-11-14 实测），同路径重建键相同
 *             命中即零增长。故与 XFontFtTest 的「唯一文件名」先例分道：
 *             该先例的 TTF 后端无按路径驻留缓存，而 PNG 解码有；用例改为
 *             固定名+开头防御性清树+内容确定性重建（跨运行键不变）。
 *
 *             【两处引擎修复依赖（随本用例以 sharedEdits 提交，修复未
 *             落地前对应断言会失败）】
 *               - P2 theme_collectSizes 旧式（无 index.theme）分支
 *                 （XIconThemeInternal.c:1957-1958）只按尺寸层拼探测路径、
 *                 context 循环变量从未入路径 ⇒ hicolor 这类无索引主题在
 *                 availableSizes 恒贡献 0 条，与位图路径 theme_tryTheme
 *                 （:488-502）的「尺寸×上下文」双层口径不一致——修复为
 *                 拼全两层，本文件用例③的 3 条断言据此成立。
 *               - P3 theme_searchThemeExists 的 parsed 分支
 *                 （:1727-1747）登记条目时按 target（themeHasIcon 硬编码
 *                 48，:2543/:2570）做 theme_directoryMatches 精确门 ⇒
 *                 只有 16/32 档的主题整链报「无条目」，同函数 legacy 分支
 *                 （:1748-1781）却无尺寸门（无索引主题反而能报 true）。
 *                 Qt 6.8 原文：hasThemeIcon = fromTheme(name).name()==name
 *                 （qicon.cpp:1431-1435），findIconHelper 对全部声明目录
 *                 只按文件存在收集条目、不设尺寸门（qiconloader.cpp
 *                 :490-533，尺寸选择在 entryForSize :849-879 查询期做）
 *                 ⇒ 修复为存在性查询去尺寸门。用例④的 c 断言据此成立。
 *
 *             覆盖的 Qt 6.8 原文口径（qtbase v6.8.3
 *             gui/image/qiconloader.cpp，本机 /home/xinyue/Qt-src/6.8.3）：
 *               - directoryMatchesSize（:794-815）/ directorySizeDistance
 *                 （:821-846）：Fixed 目录距离 = |dir.size*scale - 请求|
 *                 （X 侧 theme_dirDistance，XIconThemeInternal.c:965-1006）；
 *               - entryForSize（:849-879）：先精确后最小距离选档
 *                 （X 侧 theme_tryParsedTheme 第一/第二轮，:1374-1467）；
 *               - QIconLoaderEngine::actualSize（:882-905）：非缩放条目
 *                 返回 min(dir.size*scale, min(请求宽,高))——**永不大于请求**
 *                 （X 侧 VXIconThemeEngine_actualSize 的 declaredSize 收口，
 *                 XIconThemeEngine.c:216-313）；
 *               - PixmapEntry::pixmap（:908-935）adjustSize 只缩不放：源图
 *                 小于请求保持原尺寸（X 侧 theme_scaledToSizeRect，
 *                 XIconThemeInternal.c:1829-1856）；
 *               - Inherits 父主题链 + hicolor 永远殿后
 *                 （theme_parentsFor，XIconThemeInternal.c:1507-1534）；
 *                 availableSizes 只在当前主题无登记时才跟进 Inherits
 *                 （theme_collectSizes 注记，:1970-1972）；跨父主题条目
 *                 不去重（Qt entries 逐目录追加，:490-533 同构）；
 *               - fallbackThemeName 参与查找链（resolve 链第二级，
 *                 XIconThemeInternal.c:2275-2281）。
 *
 *             用例分组（每例独立建树/清树/还原全局态，运行器泄漏门零容忍）：
 *               ① fromThemeHitsRuntimeTheme      —— setThemeSearchPaths +
 *                  setThemeName 后 fromTheme 命中自造主题（含 XString 重载、
 *                  hasThemeIcon、name 回读、真实 PNG 解码、未命中负例）；
 *               ② actualSizeSelectsNearestFixedTier —— actualSize 随请求档
 *                  与目录距离选档（20→16 档 / 26→32 档，双断言：声明尺寸
 *                  + 解码内容色）；
 *               ③ inheritsFallsBackToParentTheme —— 自有条目遮蔽继承档
 *                  （availableSizes 只报 48），删本地 a.png 后回落父主题
 *                  （内容色 绿→红 可观察）；
 *               ④ missingThemeFallsToHicolorAndFallbackTheme —— 主题整体
 *                  缺失时 hicolor 终回退（内容色黄）；fallbackThemeName
 *                  参与 c 图标回退（hasThemeIcon 翻转，依赖 P3 修复）。
 *
 * @note       C99；对象全部释放，满足运行器零泄漏门；进程级 XPixmapCache
 *             在用例收尾清空（XComboBoxAlignTest 同款注记：主题缩放缓存
 *             是 Qt 同款纯性能缓存语义，非所有权泄漏，但键不含主题名，
 *             跨用例同名图标会串色，收尾必须清）。
 * @author     XinYueC 团队
 */
#include "xq_align.h"

#include "XGuiConfig.h"

/* 图像编解码裁剪（XIMAGECODEC_ON/XIMAGECODEC_PNG_ON=0）时无法程序化生成
 * PNG 主题资产：整段不编译，与 XFileAlignTest 的模块裁剪口径一致
 * （不伪造空断言）。XGUI_ON=0 的构建由 Test/CMakeLists.txt 整目录剔除。 */
#if XGUI_ON && XIMAGECODEC_ON && XIMAGECODEC_PNG_ON

#include "XIcon.h"
#include "XImage.h"
#include "XPixmap.h"
#include "XPixmapCache.h"   /* 主题缩放缓存收尾清理（防泄漏门误报/串色） */
#include "XString.h"
#include "XStringList.h"
#include "XContainer.h"     /* XVector_size_base / XVector_at_base */
#include "XGeometry.h"      /* XSize */
#include "XDir.h"
#include "XFile.h"
#include "XIODevice.h"
#include "XMemory.h"

#include <string.h>

/* ==================== 资产颜色（0xAARRGGBB） ==================== */
#define XQ_IC_COLOR_TEST_A16  0xFFD23F3Fu  /* testtheme 16 档 a：红 */
#define XQ_IC_COLOR_TEST_A32  0xFF3F6FD2u  /* testtheme 32 档 a：蓝 */
#define XQ_IC_COLOR_TEST_C32  0xFF3F6FD2u  /* testtheme 32 档 c：蓝 */
#define XQ_IC_COLOR_SECOND_A  0xFF3FD25Au  /* secondtheme 48 档 a：绿 */
#define XQ_IC_COLOR_HICOLOR_A 0xFFD2C23Fu  /* hicolor 16 档 a：黄 */

/* index.theme 内容。字段与 XIconThemeInternal.c:764-943 的解析口径逐项
 * 对齐：[Icon Theme] 节只消费 Directories/Inherits（:838-843）；目录节名
 * 必须与 Directories 逐字相等（themeContext_findDir 用 XStrcmp，
 * :731-744）；Size<=0 的目录按 Invalid 剔除（:746-762）；Type/Context
 * 键名与值大小写不敏感（theme_keyEquals）。 */
static const char* const xq_ic_testthemeIndex =
    "[Icon Theme]\n"
    "Name=Test Theme\n"
    "Directories=16x16/apps,32x32/apps\n"
    "\n"
    "[16x16/apps]\n"
    "Size=16\n"
    "Type=Fixed\n"
    "Context=Applications\n"
    "\n"
    "[32x32/apps]\n"
    "Size=32\n"
    "Type=Fixed\n"
    "Context=Applications\n";

static const char* const xq_ic_secondthemeIndex =
    "[Icon Theme]\n"
    "Name=Second Theme\n"
    "Inherits=testtheme\n"
    "Directories=48x48/apps\n"
    "\n"
    "[48x48/apps]\n"
    "Size=48\n"
    "Type=Fixed\n"
    "Context=Applications\n";

/* ==================== 资产构造辅助 ==================== */

/**
 * @brief 生成用例固定名主题根目录路径（新 XString，调用方释放）。
 * @note  **固定名**是泄漏门口径要求（见文件头注）：解码位图落
 *        XImageCache 的 path+format 键（XImageCache.h:50-61），唯一
 *        时间戳目录每遍新建键 ⇒ 每遍新增 ~5.7KB 缓存条目 ⇒ 零容忍门
 *        复核遍仍增长必判 [LEAK]；固定名+确定性内容重建 ⇒ 复核遍命中
 *        同键零增长。开头防御性清树 + 内容确定性重建保证跨运行语义
 *        不变（同路径同内容）。
 */
static XString* xq_ic_fixedRoot(const char* tag)
{
    XString* dir = XDir_tempPath();
    XString* path;
    if (!dir) return NULL;
    path = XString_create_copy(dir);
    XClassDelete((XClass*)dir);
    if (!path) return NULL;
    XString_append_utf8(path, "/xq_icontheme_");
    XString_append_utf8(path, tag);
    return path;
}

/** @brief 递归删除主题根目录（前置声明：resetRoot 在定义点之前使用）。 */
static bool xq_ic_removeTree(const XString* root);

/**
 * @brief 建树前置：清掉可能残留的旧树（首次运行/上一遍收尾已删时为
 *        无害空操作），保证 rebuild 在确定的空起点上进行。
 */
static void xq_ic_resetRoot(const XString* root)
{
    (void)xq_ic_removeTree(root);
}

/** @brief root + "/" + sub 相对段拼接（新 XString，调用方释放）。 */
static XString* xq_ic_subPath(const XString* root, const char* sub)
{
    XString* path = XString_create_copy(root);
    if (!path) return NULL;
    XString_append_utf8(path, "/");
    XString_append_utf8(path, sub);
    return path;
}

/** @brief 递归创建目录（绝对路径），失败返回 false。 */
static bool xq_ic_mkpath(const XString* path)
{
    XDir* dir = XDir_create_2(path);
    bool ok;
    if (!dir) return false;
    ok = XDir_mkpath(dir, path);
    XClassDelete((XClass*)dir);
    return ok;
}

/** @brief 写 UTF-8 文本文件（覆盖创建），失败返回 false。 */
static bool xq_ic_writeText(const XString* path, const char* text)
{
    XFile* file = XFile_create_2(path);
    size_t len = strlen(text);
    bool ok;
    if (!file) return false;
    ok = XFile_open_2(file,
                      XIODevice_WriteOnly | XIODevice_Truncate |
                      XIODevice_Create, 0);
    if (ok)
        ok = (len == 0) || (XIODevice_write_1((XIODevice*)file, text,
                                              (int64_t)len) == (int64_t)len);
    XIODevice_close_base((XIODevice*)file);
    XClassDelete((XClass*)file);
    return ok;
}

/** @brief 写一张纯色 PNG（框架 XImage 编码器，quality=100 无损），失败返回 false。 */
static bool xq_ic_writePng(const XString* path, int width, int height,
                           uint32_t color)
{
    XImage image;
    bool ok;
    XImage_init_ex(&image, width, height, XImageFormat_ARGB32);
    XImage_fill(&image, color);
    ok = XImage_save_2(&image, XString_toUtf8(path), "png", 100);
    XClassDeinit((XClass*)&image);
    return ok;
}

/**
 * @brief 一次建齐全部主题资产（testtheme + secondtheme + hicolor）。
 * @return 全部目录与文件创建成功返回 true；任一步失败返回 false。
 */
static bool xq_ic_buildThemeTree(const XString* root)
{
    XString* path;
    bool ok = true;

    /* ---- testtheme：16/32 两档 Fixed 的 a + 仅 32 档的 c ---- */
    path = xq_ic_subPath(root, "testtheme/16x16/apps");
    ok = ok && path && xq_ic_mkpath(path);
    if (path) XClassDelete((XClass*)path);
    path = xq_ic_subPath(root, "testtheme/32x32/apps");
    ok = ok && path && xq_ic_mkpath(path);
    if (path) XClassDelete((XClass*)path);
    path = xq_ic_subPath(root, "testtheme/index.theme");
    ok = ok && path && xq_ic_writeText(path, xq_ic_testthemeIndex);
    if (path) XClassDelete((XClass*)path);
    path = xq_ic_subPath(root, "testtheme/16x16/apps/a.png");
    ok = ok && path && xq_ic_writePng(path, 16, 16, XQ_IC_COLOR_TEST_A16);
    if (path) XClassDelete((XClass*)path);
    path = xq_ic_subPath(root, "testtheme/32x32/apps/a.png");
    ok = ok && path && xq_ic_writePng(path, 32, 32, XQ_IC_COLOR_TEST_A32);
    if (path) XClassDelete((XClass*)path);
    path = xq_ic_subPath(root, "testtheme/32x32/apps/c.png");
    ok = ok && path && xq_ic_writePng(path, 32, 32, XQ_IC_COLOR_TEST_C32);
    if (path) XClassDelete((XClass*)path);

    /* ---- secondtheme：自有 48 档 a + Inherits=testtheme ---- */
    path = xq_ic_subPath(root, "secondtheme/48x48/apps");
    ok = ok && path && xq_ic_mkpath(path);
    if (path) XClassDelete((XClass*)path);
    path = xq_ic_subPath(root, "secondtheme/index.theme");
    ok = ok && path && xq_ic_writeText(path, xq_ic_secondthemeIndex);
    if (path) XClassDelete((XClass*)path);
    path = xq_ic_subPath(root, "secondtheme/48x48/apps/a.png");
    ok = ok && path && xq_ic_writePng(path, 48, 48, XQ_IC_COLOR_SECOND_A);
    if (path) XClassDelete((XClass*)path);

    /* ---- hicolor：无 index.theme（旧式 XDG 目录约定），仅 16 档 a ---- */
    path = xq_ic_subPath(root, "hicolor/16x16/apps");
    ok = ok && path && xq_ic_mkpath(path);
    if (path) XClassDelete((XClass*)path);
    path = xq_ic_subPath(root, "hicolor/16x16/apps/a.png");
    ok = ok && path && xq_ic_writePng(path, 16, 16, XQ_IC_COLOR_HICOLOR_A);
    if (path) XClassDelete((XClass*)path);

    return ok;
}

/** @brief 递归删除主题根目录（泄漏门 + 零残留），返回是否删除成功。 */
static bool xq_ic_removeTree(const XString* root)
{
    XDir* dir = XDir_create_2(root);
    bool ok;
    if (!dir) return false;
    ok = XDir_removeRecursively(dir);
    XClassDelete((XClass*)dir);
    return ok;
}

/* ==================== 全局主题态辅助 ==================== */

/**
 * @brief 激活自造主题：搜索路径=唯一主题根 + 主题名。
 * @note 两个 setter 均会清 XIconScaledPixmapCache（XIcon.c:1729/:1787），
 *       跨用例同名图标不会从上一用例的缓存串色。
 */
static void xq_ic_activateTheme(const XString* root, const char* themeName)
{
    XStringList* paths = XStringList_create();
    if (paths) {
        XStringList_push_back_utf8(paths, XString_toUtf8(root));
        XIcon_setThemeSearchPaths(paths);
        XClassDelete((XClass*)paths);
    }
    XIcon_setThemeName_2(themeName);
}

/** @brief 还原全局主题态（清用户主题/后备主题/搜索路径 + 清缓存）。
 *  @note setThemeName_2(NULL) 触发平台搜索路径恢复（XIcon.c:1784-1786），
 *        随后 setThemeSearchPaths(NULL) 清空用户路径存储——与测试宿主
 *        启动态等价（后续访问方走惰性的平台路径 + :/icons 重建分支）。 */
static void xq_ic_restoreGlobals(void)
{
    XIcon_setThemeName_2(NULL);
    XIcon_setFallbackThemeName_2(NULL);
    XIcon_setThemeSearchPaths(NULL);
#if XPIXMAPCACHE_ON
    XPixmapCache_clear();
#endif
}

/**
 * @brief 建树失败的统一退出口：先清树再报失败（零残留纪律）。
 * @return 恒 false（供调用方直接 return）。
 */
static bool xq_ic_bailAfterBuildFailure(const XString* root, int line)
{
    xq_fail_at(__FILE__, line, "主题资产构建失败（临时目录或 PNG 编码器不可用）");
    if (!xq_ic_removeTree(root))
        xq_fail_at(__FILE__, line, "主题资产清理也失败（残留于临时目录）");
    return false;
}

/* ==================== 断言辅助 ==================== */

/**
 * @brief 取主题图标在某请求尺寸下实际解码位图的首像素（0xAARRGGBB）。
 * @return 位图为空返回 0。纯色源图经缩放（只缩不放，
 *         theme_scaledToSizeRect）后任意采样点颜色不变。
 */
static uint32_t xq_ic_iconPixel(const XIcon* icon, int size)
{
    XPixmap pixmap;
    XImage image;
    uint32_t pixel = 0u;
    XMemset(&pixmap, 0, sizeof(pixmap));
    XIcon_pixmap(icon, size, size, XIconMode_Normal, XIconState_Off, &pixmap);
    if (!XPixmap_isNull(&pixmap)) {
        XImage_init(&image);
        XPixmap_toImage(&pixmap, &image);
        pixel = XImage_pixel(&image, 0, 0);
        XClassDeinit((XClass*)&image);
    }
    XClassDeinit((XClass*)&pixmap);
    return pixel;
}

/** @brief 统计主题图标 availableSizes 条目数（XVector of XSize）。 */
static int xq_ic_availableSizeCount(const XIcon* icon)
{
    XVector sizes;
    int count = 0;
    XVector_init(&sizes, sizeof(XSize), true);
    XIcon_availableSizes(icon, XIconMode_Normal, XIconState_Off, &sizes);
    count = (int)XVector_size_base((const XContainer*)&sizes);
    XClassDeinit((XClass*)&sizes);
    return count;
}

/** @brief availableSizes 中是否存在指定方形尺寸。 */
static bool xq_ic_availableSizeHas(const XIcon* icon, int size)
{
    XVector sizes;
    bool found = false;
    size_t i;
    XVector_init(&sizes, sizeof(XSize), true);
    XIcon_availableSizes(icon, XIconMode_Normal, XIconState_Off, &sizes);
    for (i = 0; i < XVector_size_base((const XContainer*)&sizes); ++i) {
        const XSize* item = (const XSize*)XVector_at_base(&sizes, (int64_t)i);
        if (item && item->width == size && item->height == size) {
            found = true;
            break;
        }
    }
    XClassDeinit((XClass*)&sizes);
    return found;
}

/** @brief actualSize 正方形便捷查询（Normal/Off）。 */
static XSize xq_ic_actualSize(const XIcon* icon, int size)
{
    XSize result;
    result.width = 0;
    result.height = 0;
    XIcon_actualSize(icon, size, size, XIconMode_Normal, XIconState_Off,
                     &result);
    return result;
}

/* ==================== ① fromTheme 命中自造主题 ==================== */

/**
 * Qt 6.8 口径：QIcon::fromTheme 经 QIconLoader 按主题搜索路径解析
 * （qicon.cpp:1038-1052 一带）；hasThemeIcon 比较 fromTheme().name() 与
 * 请求原文。X 侧消费面：XIcon_setThemeSearchPaths（XIcon.c:1723）、
 * XIcon_setThemeName_2（:1776）、XIcon_fromTheme/_2（:1578/:1573）、
 * XIcon_hasThemeIcon/_2（:1591/:1583）。
 */
XQ_TEST(XIconTheme, fromThemeHitsRuntimeTheme)
{
    XString* root = xq_ic_fixedRoot("hit");
    XString* nameA;
    XIcon icon;
    XIcon iconViaString;
    XIcon miss;
    XString* iconName;
    XPixmap pm16;
    XPixmap pm32;

    XQ_REQUIRE(root != NULL);
    xq_ic_resetRoot(root);
    if (!xq_ic_buildThemeTree(root)) {
        (void)xq_ic_bailAfterBuildFailure(root, __LINE__);
        XClassDelete((XClass*)root);
        return;
    }
    xq_ic_activateTheme(root, "testtheme");

    /* ---- 主题命中：hasThemeIcon 与 fromTheme 两族入口 ---- */
    XQ_CHECK(XIcon_hasThemeIcon_2("a"));
    nameA = XString_create_utf8("a");
    XQ_CHECK(nameA != NULL);
    XQ_CHECK(XIcon_hasThemeIcon(nameA));

    XMemset(&icon, 0, sizeof(icon));
    XIcon_fromTheme_2("a", NULL, &icon);
    XQ_CHECK(!XIcon_isNull(&icon));
    iconName = XIcon_isNull(&icon) ? NULL : XIcon_name(&icon);
    XQ_EQ_STR(iconName ? XString_toUtf8(iconName) : NULL, "a");
    if (iconName) XClassDelete((XClass*)iconName);

    /* XString 重载（XIcon.h:560）同口径命中。 */
    XMemset(&iconViaString, 0, sizeof(iconViaString));
    XIcon_fromTheme(nameA, NULL, &iconViaString);
    XQ_CHECK(!XIcon_isNull(&iconViaString));
    XClassDeinit((XClass*)&iconViaString);
    XClassDelete((XClass*)nameA);

    /* ---- 真实 PNG 解码证明：两档各自取到对应源图 ---- */
    /* 损坏 PNG 也会被登记（存在即停，XIconThemeInternal.c:1038-1046），
     * 只有真解码成功位图才非空——位图尺寸即源档位。 */
    XMemset(&pm16, 0, sizeof(pm16));
    XIcon_pixmap(&icon, 16, 16, XIconMode_Normal, XIconState_Off, &pm16);
    XQ_CHECK(!XPixmap_isNull(&pm16));
    XQ_EQ_I64(XPixmap_width(&pm16), 16);
    XQ_EQ_I64(XPixmap_height(&pm16), 16);
    XQ_EQ_I64((long long)XQ_IC_COLOR_TEST_A16,
              (long long)xq_ic_iconPixel(&icon, 16));
    XClassDeinit((XClass*)&pm16);

    XMemset(&pm32, 0, sizeof(pm32));
    XIcon_pixmap(&icon, 32, 32, XIconMode_Normal, XIconState_Off, &pm32);
    XQ_CHECK(!XPixmap_isNull(&pm32));
    XQ_EQ_I64(XPixmap_width(&pm32), 32);
    XQ_EQ_I64(XPixmap_height(&pm32), 32);
    XQ_EQ_I64((long long)XQ_IC_COLOR_TEST_A32,
              (long long)xq_ic_iconPixel(&icon, 32));
    XClassDeinit((XClass*)&pm32);

    /* ---- 未命中负例：主题内不存在的名 → null 且 hasThemeIcon false ---- */
    XMemset(&miss, 0, sizeof(miss));
    XIcon_fromTheme_2("xq_no_such_icon", NULL, &miss);
    XQ_CHECK(XIcon_isNull(&miss));
    XQ_CHECK(!XIcon_hasThemeIcon_2("xq_no_such_icon"));
    XClassDeinit((XClass*)&miss);

    XClassDeinit((XClass*)&icon);
    xq_ic_restoreGlobals();
    XQ_CHECK(xq_ic_removeTree(root));
    XClassDelete((XClass*)root);
}

/* ==================== ② actualSize 随请求档 + 目录距离选档 =========== */

/**
 * Qt 6.8 口径（qiconloader.cpp）：entryForSize（:849-879）先精确匹配
 * 再最小距离（Fixed 距离=|dir.size-请求|，:821-827）；actualSize
 * （:882-905）非缩放条目返回 min(dir.size, min(请求宽,高))，永不大于
 * 请求。X 侧：theme_dirDistance（XIconThemeInternal.c:965-1006）、
 * VXIconThemeEngine_actualSize（XIconThemeEngine.c:216-313）。
 *
 * 选档几何（Fixed 16 与 Fixed 32 两档）：
 *   请求 20 → |16-20|=4 < |32-20|=12 → 16 档 → min(16,20)=16×16；
 *   请求 26 → |16-26|=10 > |32-26|=6 → 32 档 → min(32,26)=26×26。
 * 若引擎选错档：请求 20 误取 32 档会收口成 20×20、请求 26 误取 16 档
 * 会报 16×16——断言均可区分。位图内容色（红/蓝）为源文件路由的
 * 第二重证据。
 */
XQ_TEST(XIconTheme, actualSizeSelectsNearestFixedTier)
{
    XString* root = xq_ic_fixedRoot("tier");
    XIcon icon;
    XSize actual;
    XPixmap pm;

    XQ_REQUIRE(root != NULL);
    xq_ic_resetRoot(root);
    if (!xq_ic_buildThemeTree(root)) {
        (void)xq_ic_bailAfterBuildFailure(root, __LINE__);
        XClassDelete((XClass*)root);
        return;
    }
    xq_ic_activateTheme(root, "testtheme");

    XMemset(&icon, 0, sizeof(icon));
    XIcon_fromTheme_2("a", NULL, &icon);
    XQ_CHECK(!XIcon_isNull(&icon));

    /* ---- 精确档：请求=目录 Size ---- */
    actual = xq_ic_actualSize(&icon, 16);
    XQ_EQ_I64(actual.width, 16);
    XQ_EQ_I64(actual.height, 16);
    actual = xq_ic_actualSize(&icon, 32);
    XQ_EQ_I64(actual.width, 32);
    XQ_EQ_I64(actual.height, 32);

    /* ---- 距离选档：请求 20 落 16 档（内容=红） ---- */
    actual = xq_ic_actualSize(&icon, 20);
    XQ_EQ_I64(actual.width, 16);
    XQ_EQ_I64(actual.height, 16);
    XMemset(&pm, 0, sizeof(pm));
    XIcon_pixmap(&icon, 20, 20, XIconMode_Normal, XIconState_Off, &pm);
    XQ_CHECK(!XPixmap_isNull(&pm));
    XQ_EQ_I64(XPixmap_width(&pm), 16);   /* 源 16×16 小于请求：保持原尺寸 */
    XQ_EQ_I64((long long)XQ_IC_COLOR_TEST_A16,
              (long long)xq_ic_iconPixel(&icon, 20));
    XClassDeinit((XClass*)&pm);

    /* ---- 距离选档：请求 26 落 32 档（内容=蓝） ---- */
    actual = xq_ic_actualSize(&icon, 26);
    XQ_EQ_I64(actual.width, 26);         /* 声明 32 收口到请求 26 */
    XQ_EQ_I64(actual.height, 26);
    XMemset(&pm, 0, sizeof(pm));
    XIcon_pixmap(&icon, 26, 26, XIconMode_Normal, XIconState_Off, &pm);
    XQ_CHECK(!XPixmap_isNull(&pm));
    XQ_EQ_I64(XPixmap_width(&pm), 26);   /* 源 32×32 超请求：缩到 26×26 */
    XQ_EQ_I64((long long)XQ_IC_COLOR_TEST_A32,
              (long long)xq_ic_iconPixel(&icon, 26));
    XClassDeinit((XClass*)&pm);

    /* ---- DPR 重载（XIcon.h:452）dpr=1 与普通路径同口径 ---- */
    XMemset(&actual, 0, sizeof(actual));
    XIcon_actualSizeRatio(&icon, 16, 16, 1.0f, XIconMode_Normal,
                          XIconState_Off, &actual);
    XQ_EQ_I64(actual.width, 16);
    XQ_EQ_I64(actual.height, 16);

    XClassDeinit((XClass*)&icon);
    xq_ic_restoreGlobals();
    XQ_CHECK(xq_ic_removeTree(root));
    XClassDelete((XClass*)root);
}

/* ==================== ③ Inherits 父主题回退 ========================= */

/**
 * Qt 6.8 口径：findIconHelper 沿 Inherits 链逐主题收集条目；hicolor 由
 * QIconLoader 恒定殿后（theme_parentsFor，XIconThemeInternal.c:1507-1534
 * ——Inherits 里的 hicolor 被滤重、fallbackThemeName 次序插入、hicolor
 * 末尾追加）。availableSizes 只在当前主题无登记时才跟进 Inherits
 * （theme_collectSizes 注记 :1970-1972，对齐 Qt 条目语义）。
 *
 * 场景：secondtheme 自有 48 档 a（绿）+ Inherits=testtheme（16 红/32 蓝）。
 * 删本地 a.png 前后，同名请求 16 的解码内容从绿变红——缓存键不含主题名
 * （XIconScaledPixmapCache_find 只带图标名/调色板/模式/尺寸/DPR），同名
 * 同尺寸跨主题会串缓存，故删除后按 setThemeName_2 同名重设触发
 * XIconScaledPixmapCache_clear（XIcon.c:1787）再断言。
 */
XQ_TEST(XIconTheme, inheritsFallsBackToParentTheme)
{
    XString* root = xq_ic_fixedRoot("inherits");
    XString* ownA;
    XIcon icon;
    XPixmap pm;

    XQ_REQUIRE(root != NULL);
    xq_ic_resetRoot(root);
    if (!xq_ic_buildThemeTree(root)) {
        (void)xq_ic_bailAfterBuildFailure(root, __LINE__);
        XClassDelete((XClass*)root);
        return;
    }
    xq_ic_activateTheme(root, "secondtheme");

    XMemset(&icon, 0, sizeof(icon));
    XIcon_fromTheme_2("a", NULL, &icon);
    XQ_CHECK(!XIcon_isNull(&icon));

    /* ---- 自有条目遮蔽继承：availableSizes 只报本地 48 档 ---- */
    XQ_EQ_I64(xq_ic_availableSizeCount(&icon), 1);
    XQ_CHECK(xq_ic_availableSizeHas(&icon, 48));
    XQ_CHECK(!xq_ic_availableSizeHas(&icon, 16));
    XQ_CHECK(!xq_ic_availableSizeHas(&icon, 32));

    /* 请求 16：唯一候选是本地 48 档（距离 32）→ 48 源缩到 16，内容绿。 */
    XMemset(&pm, 0, sizeof(pm));
    XIcon_pixmap(&icon, 16, 16, XIconMode_Normal, XIconState_Off, &pm);
    XQ_CHECK(!XPixmap_isNull(&pm));
    XQ_EQ_I64(XPixmap_width(&pm), 16);
    XQ_EQ_I64((long long)XQ_IC_COLOR_SECOND_A,
              (long long)xq_ic_iconPixel(&icon, 16));
    XClassDeinit((XClass*)&pm);

    /* ---- 删本地 a.png → 回落父主题 testtheme ---- */
    ownA = xq_ic_subPath(root, "secondtheme/48x48/apps/a.png");
    XQ_CHECK(ownA != NULL);
    if (ownA) {
        XQ_CHECK(XFile_remove_static(ownA));
        XClassDelete((XClass*)ownA);
    }
    /* 同名重设主题：清空进程级像素缓存，避免上一断言的绿色 16×16
     * 缓存条目串入（键不含主题名，见用例头注）。 */
    XIcon_setThemeName_2("secondtheme");

    XIcon_fromTheme_2("a", NULL, &icon);
    XQ_CHECK(!XIcon_isNull(&icon));

    /* availableSizes 跟进继承链：testtheme 16/32（parsed 分支按目录名
     * 全路径探测）+ hicolor 16（legacy 分支按「尺寸/上下文」双层探测，
     * 依赖 P2 引擎修复：修复前 context 层未拼入路径、殿后主题贡献 0 条、
     * 实测只有 2 条）→ 3 条、不去重（Qt entries 逐目录追加同构），
     * 48 消失。 */
    XQ_EQ_I64(xq_ic_availableSizeCount(&icon), 3);
    XQ_CHECK(xq_ic_availableSizeHas(&icon, 16));
    XQ_CHECK(xq_ic_availableSizeHas(&icon, 32));
    XQ_CHECK(!xq_ic_availableSizeHas(&icon, 48));

    /* 请求 16 内容变红：经 Inherits 命中 testtheme 的 16 档。 */
    XQ_EQ_I64((long long)XQ_IC_COLOR_TEST_A16,
              (long long)xq_ic_iconPixel(&icon, 16));

    /* hasThemeIcon 保持 true：resolveThemeIconName 的链上命中。
     * 依赖 P3 引擎修复——存在性查询去尺寸门后，本主题（48 档缺失）
     * 才能在 Inherits 命中的 testtheme 16/32 档登记条目；修复前该查询
     * 按 target=48 精确门逐层落空，实测是经平台 fallback 主题名
     * （hicolor）的 legacy 无门分支碰巧命中，并非 Inherits 语义。 */
    XQ_CHECK(XIcon_hasThemeIcon_2("a"));

    XClassDeinit((XClass*)&icon);
    xq_ic_restoreGlobals();
    XQ_CHECK(xq_ic_removeTree(root));
    XClassDelete((XClass*)root);
}

/* ==================== ④ hicolor 终回退 + fallbackThemeName ========== */

/**
 * Qt 6.8 口径：主题整体缺失时查找链仍以 hicolor 收尾
 * （theme_parentsFor 恒追加，XIconThemeInternal.c:1532；hicolor 无
 * index.theme 时走内嵌 XDG 目录名旧式匹配，theme_size_dirs ×
 * theme_context_dirs，theme_tryTheme :488-502）；fallbackThemeName 是
 * 继承链之后、独立回退文件之前的第二级主题查找（resolve 链
 * XIconThemeInternal.c:2275-2281）。消费面：XIcon_setFallbackThemeName_2
 * （XIcon.c:1816）/ XIcon_fallbackThemeName_2（:1809）。
 */
XQ_TEST(XIconTheme, missingThemeFallsToHicolorAndFallbackTheme)
{
    XString* root = xq_ic_fixedRoot("hicolor");
    XIcon iconA;
    XIcon iconC;
    XSize actual;

    XQ_REQUIRE(root != NULL);
    xq_ic_resetRoot(root);
    if (!xq_ic_buildThemeTree(root)) {
        (void)xq_ic_bailAfterBuildFailure(root, __LINE__);
        XClassDelete((XClass*)root);
        return;
    }
    /* 清用户后备主题名：保证 hicolor 断言不被后备主题抢先
     * （getter 未设置时回落平台后备名，但平台名在唯一搜索根下无主题）。 */
    XIcon_setFallbackThemeName_2(NULL);
    xq_ic_activateTheme(root, "xq_no_such_theme");

    /* ---- 负例：c 只存在于 testtheme，链上无后备主题名时不可达 ---- */
    XQ_CHECK(!XIcon_hasThemeIcon_2("c"));

    /* ---- hicolor 终回退：主题缺失仍命中 hicolor 的 a（黄） ---- */
    XMemset(&iconA, 0, sizeof(iconA));
    XIcon_fromTheme_2("a", NULL, &iconA);
    XQ_CHECK(!XIcon_isNull(&iconA));
    actual = xq_ic_actualSize(&iconA, 16);
    XQ_EQ_I64(actual.width, 16);
    XQ_EQ_I64(actual.height, 16);
    XQ_EQ_I64((long long)XQ_IC_COLOR_HICOLOR_A,
              (long long)xq_ic_iconPixel(&iconA, 16));
    XClassDeinit((XClass*)&iconA);

    /* ---- fallbackThemeName 参与回退链：c 翻转为可用 ----
     * 依赖 P3 引擎修复：存在性查询（themeHasIcon :2543/:2570 硬编码
     * target=48）在 parsed 分支按尺寸精确门登记条目，testtheme 只有
     * 16/32 档 ⇒ 修复前整链报无条目（实测 false），Qt 6.8 口径
     * （findIconHelper 只按文件存在收集、尺寸在 entryForSize 查询期选）
     * 下 32 档 c 应命中。 */
    XIcon_setFallbackThemeName_2("testtheme");
    XQ_EQ_STR(XIcon_fallbackThemeName_2(), "testtheme");
    XQ_CHECK(XIcon_hasThemeIcon_2("c"));
    XMemset(&iconC, 0, sizeof(iconC));
    XIcon_fromTheme_2("c", NULL, &iconC);
    XQ_CHECK(!XIcon_isNull(&iconC));
    XClassDeinit((XClass*)&iconC);

    xq_ic_restoreGlobals();
    XQ_CHECK(xq_ic_removeTree(root));
    XClassDelete((XClass*)root);
}

#endif /* XGUI_ON && XIMAGECODEC_ON && XIMAGECODEC_PNG_ON */
