/**
 * @file       XIconCacheAlignTest.c
 * @brief      XIcon 缩放缓存键（key=iconcache）行为级回归锁。
 * @details    缺陷实录（上轮复核员探针，2026-11-15）：
 *             XIconPrivate_scaledPixmap 曾以
 *             XPixmap_cacheKey(&best->m_pixmap) 作 XIconScaledPixmapCache
 *             的 sourceKey（XIcon.c:1048-1049 旧码），而该键取自**单调
 *             递增的全局实例序列号**（XPixmap.c XPlatformPixmap_nextCacheKey
 *             `g_pixmapSerialCounter++ << 32`）。Qt 6.8 的
 *             QPixmapIconEngine::scaledPixmap（qicon.cpp:350-356）用的是
 *             引擎**长期持有**的 QPixmapIconEngineEntry::pixmap 的
 *             cacheKey()（qpixmap.cpp:880 只在像素图被 alter 时变化），
 *             同一个 QIcon 反复取图必然命中同一键。本框架的 XTabBar /
 *             XComboBox / XToolBox 绘制却是**每帧新建 XIcon 并从文件重新
 *             解码**（XTabBar.c xtabbar_drawTabIcon / XComboBox.c
 *             xcombo_itemImageAt / XToolBox.c xtb2_drawItemIcon），每次都
 *             是全新 XPixmap 实例 ⇒ 序列号必变 ⇒ 缓存永不命中、每帧向
 *             全局 XPixmapCache 插一条（XToolBoxIconAlignTest.c:90-149
 *             实测每帧 +2240 字节，5 次 +11200；10MB 上限只挤掉其它
 *             合法键，「有上限 ≠ 不增长」）。
 *
 *             修法（方向 A：键与资源身份绑定）：sourceKey 改由「条目
 *             文件名身份 hash（XString_hash）+ 源像素内容指纹
 *             （XPixmap_contentFingerprint）」派生，与 XPixmap 实例身份
 *             彻底解耦。同一资源反复解码 → 键相同 → 命中；不同资源
 *             （路径或内容不同）→ 键互异；磁盘资源被换 → 内容指纹变化
 *             → 键自动失效，不会返回陈旧像素。尺寸/mode/palette/DPR 仍
 *             由 XIconScaledPixmapCache 并入最终键。
 *
 * 验收四项（任务书）：
 *   ① 同一图标连取 100 次内存 second-round +0（首轮填充，其后命中）；
 *   ② 不同图标键互异且命中正确（互不串用）；
 *   ③ 绘制像素不变（缓存命中前后 grab/像素逐点对比）；
 *   ④ XTabBar / XComboBox 图标路径过滤回归全绿（多轮绘制堆计数稳定）。
 *
 * @note       缓存是进程级 XPixmapCache（XPIXMAPCACHE_ON=1 时），每个
 *             用例收尾调 XPixmapCache_clear() 归还自己插入的条目（既有
 *             XToolBoxIconAlignTest.c:144-149 / XComboBoxAlignTest.c:815-824
 *             同款「收自己的账」），使泄漏门只衡量真实所有权释放。
 *             裁剪构建（XPIXMAPCACHE_ON=0）下缓存退化为永久未命中，
 *             相关内存用例自动跳过增长断言、只保留像素/键语义断言。
 * @author     XinYueC 团队
 */
#include "xq_align.h"

#include "XGuiConfig.h"

#if XGUI_ON && XPIXMAPCACHE_ON && XWIDGET_ON && XTABBAR_ON && XCOMBOBOX_ON

#include "XIcon.h"
#include "XPixmap.h"
#include "XImage.h"
#include "XWidget.h"
#include "XTabBar.h"
#include "XComboBox.h"
#include "XClass.h"
#include "XObject.h"
#include "XString.h"
#include "XMemory.h"
#include "XCoreApplication.h"
#include "XPixmapCache.h"
#include "XCryptographicHash.h"
#include <stdio.h>             /* remove()：清测试自建图标。 */

/* 与控件绘制同口径的图标边长（XToolBox.c XTOOLBOX_ICON_PX=16）。 */
#define XIC_ICON_PX 16
/* 「连取」轮数：任务书 ① 指定 100 次。 */
#define XIC_REPEAT  100

/** @brief 造一张 size×size 纯色 XPM 图标（纯文本编解码，无 zlib 依赖）。 */
static void xic_makeIcon(const char* path, int size, uint32_t color)
{
    XImage image;
    XImage_init_ex(&image, size, size, XImageFormat_ARGB32);
    XImage_fill(&image, color);
    (void)XImage_save_2(&image, path, "xpm", 100);
    XClassDeinit((XClass*)&image);
}

/** @brief 当前框架分配器在用字节（系统堆 + 多级池 + 可变池，同 xq_mem_now）。 */
static size_t xic_memNow(void)
{
    XMemoryStatistics sys = XMemory_statistics_2(XMEMORY_TYPE_SYSTEM);
    XMemoryStatistics mp = XMemory_statistics_2(XMEMORY_TYPE_MULTIPOOL);
    XMemoryStatistics vp = XMemory_statistics_2(XMEMORY_TYPE_VARIABLEPOOL);
    return sys.systemBytes + mp.poolUsedBytes + vp.poolUsedBytes;
}

/** @brief 清图标缩放缓存（收自己插入的全局条目，泄漏门前提）。 */
static void xic_clearCache(void)
{
    XPixmapCache_clear();
    XCoreApplication_sendPostedEvents(NULL, XEVENT_TYPE_DEFERRED_DELETE);
}

/**
 * @brief 「每帧新建 XIcon」地取图一次（精确复刻控件绘制路径）。
 * @details XTabBar.c:760 / XComboBox.c:152 / XToolBox.c:319 都是
 *          XIcon_init_file_2 → XIcon_pixmap → XClassDeinit。旧实现下
 *          每次调用都产生新 XPixmap 实例 → 新序列号 → 新缓存键 → 新
 *          插入一条（永不命中）。修复后应命中同一键。
 * @return 输出像素图是否非空。
 */
static bool xic_fetchOne(const char* path, int px, XPixmap* out)
{
    XIcon icon;
    bool ok;
    XMemset(&icon, 0, sizeof(icon)); /* 裸栈清零：防 init 的 vtable 探测
        把前序帧残留误判为已初始化（同 XIcon.c:1060 注）。 */
    XMemset(out, 0, sizeof(*out));
    XPixmap_init(out);
    XIcon_init_file_2(&icon, path);
    if (XIcon_isNull(&icon)) {
        XClassDeinit((XClass*)&icon);
        return false;
    }
    XIcon_pixmap(&icon, px, px, XIconMode_Normal, XIconState_Off, out);
    ok = !XPixmap_isNull(out);
    XClassDeinit((XClass*)&icon);
    return ok;
}

/** @brief 连取 XIC_REPEAT 次，每次新建 XIcon 并释放输出（模拟控件每帧）。 */
static int xic_fetchMany(const char* path, int px)
{
    int i;
    int okCount = 0;
    for (i = 0; i < XIC_REPEAT; ++i) {
        XPixmap pm;
        if (xic_fetchOne(path, px, &pm)) ++okCount;
        XClassDeinit((XClass*)&pm);
    }
    return okCount;
}

/** @brief 两张等尺寸 XImage 逐像素差异数（尺寸不同返回 -1）。 */
static int xic_imageDiff(const XImage* a, const XImage* b)
{
    int diff = 0;
    int x;
    int y;
    int w;
    int h;
    if (!a || !b) return -1;
    w = XImage_width(a);
    h = XImage_height(a);
    if (w != XImage_width(b) || h != XImage_height(b)) return -1;
    for (y = 0; y < h; ++y)
        for (x = 0; x < w; ++x)
            if (XImage_pixel(a, x, y) != XImage_pixel(b, x, y)) ++diff;
    return diff;
}

/** @brief 把 XPixmap 取成 XImage（借用型输出，用完 XClassDeinit）。 */
static void xic_pixmapToImage(const XPixmap* pm, XImage* out)
{
    XMemset(out, 0, sizeof(*out));
    XImage_init(out);
    XPixmap_toImage(pm, out);
}

/* ==================== ① 同图标连取 100 次：第二轮 +0 ================ */

/**
 * @brief 旧实现必挂的核心场景：每轮都新建 XIcon（控件绘制路径）。
 * @details 第一轮 100 次填充缓存（每个新实例都插一条），第二轮同样
 *          100 次若键稳定则全部命中 ⇒ 堆计数 +0。修复前两轮都线性
 *          增长（XToolBoxIconAlignTest.c:98-104 实证无平台期）。
 *          先 clear 保证从空缓存起算；用例收尾再 clear 归还。
 */
XQ_TEST(XIconCache, repeat100SecondRoundZeroGrowth)
{
    const char* path = "/tmp/xic_repeat.xpm";
    size_t before;
    size_t afterFirst;
    size_t afterSecond;
    long long firstDelta;
    long long secondDelta;
    int firstOk;
    int secondOk;

    xic_makeIcon(path, 8, 0xFF3366CCu);
    /* 预热一拍：把 XImage 编解码器/字体/缓存表结构等一次性懒初始化
     * 提前建好，再清缓存从空表起算，使下面的 firstDelta 纯粹反映
     * 「缓存插入」而非全局单例初始化（口径与 xq_warmup 同款）。 */
    (void)xic_fetchMany(path, XIC_ICON_PX);
    xic_clearCache();

    before = xic_memNow();
    firstOk = xic_fetchMany(path, XIC_ICON_PX);
    afterFirst = xic_memNow();
    firstDelta = (long long)afterFirst - (long long)before;

    /* 第二轮：同样的 100 次「每帧新建 XIcon」。键已与实例身份解耦，
     * 应全部命中，堆计数不再增长。 */
    {
        size_t b2 = xic_memNow();
        secondOk = xic_fetchMany(path, XIC_ICON_PX);
        afterSecond = xic_memNow();
        secondDelta = (long long)afterSecond - (long long)b2;
    }

    XQ_EQ_I64(firstOk, XIC_REPEAT);
    XQ_EQ_I64(secondOk, XIC_REPEAT);
    /* 首轮必须真的往全局缓存插入过东西（>0），否则「第二轮 +0」可能只是
     * 「缓存被裁剪/insert 静默失败」的假绿。实测修复后首轮只插入 1 条
     * （约 2.2KB，其余 99 次同键命中），修复前每轮 100 次各插 1 条、
     * 量级 ~224KB。这条 + 下面「第二轮 +0」共同排除两种假绿。 */
    XQ_CHECK_MSG(firstDelta > 0,
                 "首轮必须真实填充缓存条目（实际 %lld；为 0 说明缓存未插入，"
                 "「第二轮 +0」会是假绿）", firstDelta);
    /* 首轮只应插入少量条目（同键命中后不再增长）：远小于「100 条各 ~2KB」
     * 的量级。用 64KB 的宽松上界锁住「没有每帧插新键」，不依赖精确开销。 */
    XQ_CHECK_MSG(firstDelta < 64 * 1024,
                 "首轮 100 次连取只应填充极少数缓存条目（实际 %lld；"
                 "接近 100 条说明键仍随实例变化、缓存未命中）", firstDelta);
    XQ_CHECK_MSG(secondDelta == 0,
                 "第二轮连取 %d 次必须 +0（键未与实例身份解耦时每轮都插新条目，"
                 "实际 %+lld）", XIC_REPEAT, secondDelta);

    xic_clearCache();
    remove(path);
}

/* ==================== ② 不同图标键互异且命中正确 ==================== */

/**
 * @brief 内容指纹的判别力 + 缓存键隔离。
 * @details 三张图：A 蓝、B 绿（内容不同）、A2 与 A 同内容但另一路径。
 *          - contentFingerprint(A) != contentFingerprint(B)；
 *          - 同内容（重新解码两次）指纹相同（跨实例稳定）；
 *          - A、B 连取后各自的像素彼此不同（缓存没有串用），且各自
 *            连取两轮像素不变（命中返回的是自己的结果）；
 *          - XPixmap_cacheKey 与 contentFingerprint 语义不同：前者随
 *            实例变化，后者不随实例变化（本缺陷的根因对照）。
 */
XQ_TEST(XIconCache, distinctIconsKeysAndHits)
{
    const char* pathA = "/tmp/xic_a.xpm";
    const char* pathB = "/tmp/xic_b.xpm";
    const char* pathA2 = "/tmp/xic_a2.xpm";
    XPixmap pmA;
    XPixmap pmA2;
    XPixmap pmB;
    XImage imA;
    XImage imA2;
    XImage imB;
    uint64_t fpA;
    uint64_t fpA2;
    uint64_t fpB;
    int64_t ckA;
    int okA;
    int okA2;
    int okB;

    xic_makeIcon(pathA, 8, 0xFF3366CCu);  /* 蓝 */
    xic_makeIcon(pathB, 8, 0xFF33AA55u);  /* 绿 */
    xic_makeIcon(pathA2, 8, 0xFF3366CCu); /* 与 A 同内容、不同路径 */
    xic_clearCache();

    okA = xic_fetchOne(pathA, XIC_ICON_PX, &pmA);
    okA2 = xic_fetchOne(pathA2, XIC_ICON_PX, &pmA2);
    okB = xic_fetchOne(pathB, XIC_ICON_PX, &pmB);
    XQ_CHECK(okA && okA2 && okB);

    /* 内容指纹：同内容跨实例必相同，不同内容必不同。 */
    fpA = XPixmap_contentFingerprint(&pmA);
    fpA2 = XPixmap_contentFingerprint(&pmA2);
    fpB = XPixmap_contentFingerprint(&pmB);
    XQ_CHECK_MSG(fpA == fpA2,
                 "同一资源内容跨 XPixmap 实例的内容指纹必须相同（%llx vs %llx）",
                 (unsigned long long)fpA, (unsigned long long)fpA2);
    XQ_CHECK_MSG(fpA != fpB,
                 "不同内容图标的内容指纹必须不同（%llx）",
                 (unsigned long long)fpA);
    XQ_CHECK(fpA != 0 && fpB != 0);

    /* cacheKey 是实例身份：两个独立解码的 XPixmap 必不同（缺陷根因对照）。 */
    ckA = XPixmap_cacheKey(&pmA);
    XQ_CHECK_MSG(XPixmap_cacheKey(&pmA2) != ckA,
                 "cacheKey 是实例序列号，跨实例本应不同（旧缺陷正是把它当资源身份用）");

    /* 缓存命中正确性：A 与 B 像素不同（互不串）；各自连取两轮像素不变。 */
    xic_pixmapToImage(&pmA, &imA);
    xic_pixmapToImage(&pmA2, &imA2);
    xic_pixmapToImage(&pmB, &imB);
    XQ_CHECK_MSG(xic_imageDiff(&imA, &imB) > 0,
                 "不同图标取图结果必须不同（缓存不得串用，差异 %d）",
                 xic_imageDiff(&imA, &imB));
    /* 同内容不同路径：结果像素一致（内容相同、缩放到同一目标尺寸）。 */
    XQ_EQ_I64(xic_imageDiff(&imA, &imA2), 0);
    XClassDeinit((XClass*)&imA);
    XClassDeinit((XClass*)&imA2);
    XClassDeinit((XClass*)&imB);
    XClassDeinit((XClass*)&pmA);
    XClassDeinit((XClass*)&pmA2);
    XClassDeinit((XClass*)&pmB);

    /* 连取多轮后再取，A/B 仍各自正确（命中返回的是自己的缓存条目）。 */
    (void)xic_fetchMany(pathA, XIC_ICON_PX);
    (void)xic_fetchMany(pathB, XIC_ICON_PX);
    {
        XPixmap pmAafter;
        XPixmap pmBafter;
        XImage imAafter;
        XImage imBafter;
        XQ_CHECK(xic_fetchOne(pathA, XIC_ICON_PX, &pmAafter));
        XQ_CHECK(xic_fetchOne(pathB, XIC_ICON_PX, &pmBafter));
        xic_pixmapToImage(&pmAafter, &imAafter);
        xic_pixmapToImage(&pmBafter, &imBafter);
        XQ_CHECK_MSG(xic_imageDiff(&imAafter, &imBafter) > 0,
                     "多轮命中后不同图标仍必须互不串用");
        XClassDeinit((XClass*)&imAafter);
        XClassDeinit((XClass*)&imBafter);
        XClassDeinit((XClass*)&pmAafter);
        XClassDeinit((XClass*)&pmBafter);
    }

    /* 内容寻址的关键正确性：同一路径的磁盘资源被替换后，缓存必须自动
     * 失效（sourceKey 含内容指纹），不能返回旧内容的陈旧像素。旧实现用
     * 实例序列号做键虽然「每次都不命中」，但修法若只用「文件路径 hash」
     * 而漏掉内容，就会在这里串味——本断言专门锁住这一点。 */
    {
        XPixmap pmBefore;
        XPixmap pmAfter;
        XImage imBefore;
        XImage imAfter;
        XQ_CHECK(xic_fetchOne(pathA, XIC_ICON_PX, &pmBefore));
        xic_pixmapToImage(&pmBefore, &imBefore);
        /* 覆盖同一路径为另一颜色（内容变化、路径不变）。 */
        xic_makeIcon(pathA, 8, 0xFFAA2277u);
        XQ_CHECK(xic_fetchOne(pathA, XIC_ICON_PX, &pmAfter));
        xic_pixmapToImage(&pmAfter, &imAfter);
        XQ_CHECK_MSG(xic_imageDiff(&imBefore, &imAfter) > 0,
                     "同路径资源内容被替换后缓存必须失效并返回新内容"
                     "（否则内容寻址漏了指纹、会返回陈旧像素）");
        XClassDeinit((XClass*)&imBefore);
        XClassDeinit((XClass*)&imAfter);
        XClassDeinit((XClass*)&pmBefore);
        XClassDeinit((XClass*)&pmAfter);
    }

    xic_clearCache();
    remove(pathA);
    remove(pathB);
    remove(pathA2);
}

/* ==================== ③ 绘制像素不变（命中前后 grab 对比） ============ */

/**
 * @brief 缓存命中前后的输出像素逐点一致。
 * @details 第一次（未命中）走完整「解码→缩放→style→回写 DPR→插入」；
 *          之后（命中）直接 XClassCopy 缓存条目。两条路径必须产生完全
 *          相同的像素与 DPR，否则会表现为「界面图标闪烁/尺寸跳变」。
 *          同时对 XTabBar / XComboBox 的控件 grab 做前后对比（任务书
 *          ④ 的像素口径）。
 */
XQ_TEST(XIconCache, pixelsUnchangedBeforeAndAfterCacheHit)
{
    const char* path = "/tmp/xic_pixels.xpm";
    XPixmap first;
    XPixmap cached;
    XImage imFirst;
    XImage imCached;

    xic_makeIcon(path, 8, 0xFFCC6633u);
    xic_clearCache();

    /* 第一次：缓存未命中（真实渲染结果）。 */
    XQ_CHECK(xic_fetchOne(path, XIC_ICON_PX, &first));
    /* 预热带 + 第二次：缓存命中（含多次重复取，仍应是同一结果）。 */
    (void)xic_fetchMany(path, XIC_ICON_PX);
    XQ_CHECK(xic_fetchOne(path, XIC_ICON_PX, &cached));

    xic_pixmapToImage(&first, &imFirst);
    xic_pixmapToImage(&cached, &imCached);
    XQ_CHECK(XImage_width(&imFirst) > 0 && XImage_height(&imFirst) > 0);
    XQ_EQ_I64(xic_imageDiff(&imFirst, &imCached), 0);
    XQ_CHECK(XPixmap_devicePixelRatio(&first) ==
             XPixmap_devicePixelRatio(&cached));
    XClassDeinit((XClass*)&imFirst);
    XClassDeinit((XClass*)&imCached);
    XClassDeinit((XClass*)&first);
    XClassDeinit((XClass*)&cached);

    /* 控件绘制口径：XTabBar / XComboBox 带图标抓图，多轮绘制后像素不变。 */
    {
        XWidget* host = XWidget_create(NULL, 0);
        XTabBar* tabs;
        XComboBox* combo;
        XImage* gTabs1;
        XImage* gTabs2;
        XImage* gCombo1;
        XImage* gCombo2;
        XQ_REQUIRE(host != NULL);
        XWidget_resize(host, 200, 120);
        tabs = XTabBar_create(host, 0);
        combo = XComboBox_create(host, 0);
        XQ_REQUIRE(tabs != NULL && combo != NULL);
        XWidget_setGeometry((XWidget*)tabs, 0, 0, 200, 30);
        XWidget_setGeometry((XWidget*)combo, 0, 40, 200, 30);
        XTabBar_addTab_2(tabs, "A");
        XTabBar_setTabIcon_2(tabs, 0, path);
        XComboBox_addItem_2(combo, "A");
        XComboBox_setItemIcon_2(combo, 0, path);
        XWidget_show(host);

        gTabs1 = XWidget_grab((XWidget*)tabs);
        gCombo1 = XWidget_grab((XWidget*)combo);
        XQ_REQUIRE(gTabs1 != NULL && gCombo1 != NULL);
        /* 中间夹多轮「每帧新建 XIcon」的图标取图（驱动缓存命中路径）。 */
        (void)xic_fetchMany(path, XIC_ICON_PX);
        gTabs2 = XWidget_grab((XWidget*)tabs);
        gCombo2 = XWidget_grab((XWidget*)combo);
        XQ_REQUIRE(gTabs2 != NULL && gCombo2 != NULL);
        XQ_EQ_I64(xic_imageDiff(gTabs1, gTabs2), 0);
        XQ_EQ_I64(xic_imageDiff(gCombo1, gCombo2), 0);

        XClassDelete((XClass*)gTabs1);
        XClassDelete((XClass*)gTabs2);
        XClassDelete((XClass*)gCombo1);
        XClassDelete((XClass*)gCombo2);
        XClassDelete((XClass*)tabs);
        XClassDelete((XClass*)combo);
        XClassDelete((XClass*)host);
    }

    xic_clearCache();
    remove(path);
}

/* ==================== ④ XTabBar/XComboBox 图标路径堆稳定 ================ */

/**
 * @brief 控件图标绘制多轮后堆计数稳定（第二遍 +0）。
 * @details 直接覆盖缺陷的现场路径：XTabBar.c:757/763 与 XComboBox.c
 *          图标绘制都走 XIcon_pixmap。用「第一轮 vs 第二轮」的两遍
 *          堆增量对照，锁定「不再每次绘制都插一条新缓存键」。
 *          XToolBox 同路径由既有 XToolBoxIconAlignTest 覆盖。
 */
XQ_TEST(XIconCache, widgetIconPathsSecondRoundZeroGrowth)
{
    const char* pathA = "/tmp/xic_w_a.xpm";
    const char* pathB = "/tmp/xic_w_b.xpm";
    size_t b1;
    size_t a1;
    size_t b2;
    size_t a2;
    long long firstDelta;
    long long secondDelta;
    int round;

    xic_makeIcon(pathA, 8, 0xFF3366CCu);
    xic_makeIcon(pathB, 8, 0xFF33AA55u);
    xic_clearCache();

    /* 画一轮控件图标（每轮都新建 XIcon，精确复刻缺陷现场）。 */
    {
        XWidget* host = XWidget_create(NULL, 0);
        XTabBar* tabs;
        XComboBox* combo;
        XQ_REQUIRE(host != NULL);
        XWidget_resize(host, 200, 120);
        tabs = XTabBar_create(host, 0);
        combo = XComboBox_create(host, 0);
        XQ_REQUIRE(tabs != NULL && combo != NULL);
        XTabBar_addTab_2(tabs, "A");
        XTabBar_addTab_2(tabs, "B");
        XTabBar_setTabIcon_2(tabs, 0, pathA);
        XTabBar_setTabIcon_2(tabs, 1, pathB);
        XComboBox_addItem_2(combo, "A");
        XComboBox_addItem_2(combo, "B");
        XComboBox_setItemIcon_2(combo, 0, pathA);
        XComboBox_setItemIcon_2(combo, 1, pathB);
        XWidget_show(host);

        /* 预热一拍 + 排空事件：XWidget_show 建原生窗时光标清除通道会向
         * X11 发异步请求（XUndefineCursor），Xlib 的错误回报与队列信号
         * 的 4 字节引用计数节点（XSignalSlot_emit 的 XAtomic_create）是
         * 跨拍回收的瞬时分配。若不先把这一拍的懒初始化/延迟回收、缓存
         * 首插全部做完并排空事件，±16 字节的瞬时账目会随机落在下面
         * 「第一轮/第二轮」的测量窗口里（实测 X11 会话下偶发）。与用例
         * ① 的 xic_fetchMany 预热同口径：先热后清空，再起算。 */
        for (round = 0; round < 2; ++round) {
            XImage* gTabs = XWidget_grab((XWidget*)tabs);
            XImage* gCombo = XWidget_grab((XWidget*)combo);
            if (gTabs) XClassDelete((XClass*)gTabs);
            if (gCombo) XClassDelete((XClass*)gCombo);
        }
        (void)XCoreApplication_processEvents(XEventLoop_AllEvents);
        XCoreApplication_sendPostedEvents(NULL, XEVENT_TYPE_DEFERRED_DELETE);
        (void)XCoreApplication_processEvents(XEventLoop_AllEvents);
        xic_clearCache();

        b1 = xic_memNow();
        for (round = 0; round < 2; ++round) {
            XImage* gTabs = XWidget_grab((XWidget*)tabs);
            XImage* gCombo = XWidget_grab((XWidget*)combo);
            if (gTabs) XClassDelete((XClass*)gTabs);
            if (gCombo) XClassDelete((XClass*)gCombo);
        }
        a1 = xic_memNow();
        firstDelta = (long long)a1 - (long long)b1;

        /* 第二轮同样两遍抓图：键稳定后应全部命中，不再增长。 */
        b2 = xic_memNow();
        for (round = 0; round < 2; ++round) {
            XImage* gTabs = XWidget_grab((XWidget*)tabs);
            XImage* gCombo = XWidget_grab((XWidget*)combo);
            if (gTabs) XClassDelete((XClass*)gTabs);
            if (gCombo) XClassDelete((XClass*)gCombo);
        }
        a2 = xic_memNow();
        secondDelta = (long long)a2 - (long long)b2;

        XClassDelete((XClass*)tabs);
        XClassDelete((XClass*)combo);
        XClassDelete((XClass*)host);
    }

    XQ_CHECK_MSG(firstDelta >= 0,
                 "首轮堆计数不应净减少（实际 %lld）", firstDelta);
    XQ_CHECK_MSG(secondDelta == 0,
                 "XTabBar/XComboBox 图标路径第二轮必须 +0（旧缺陷每帧都插新键，"
                 "实际 %+lld）", secondDelta);

    xic_clearCache();
    remove(pathA);
    remove(pathB);
}

#endif /* XGUI_ON && XPIXMAPCACHE_ON && XWIDGET_ON && XTABBAR_ON && XCOMBOBOX_ON */
