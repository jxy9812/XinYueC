/**
 * @file       XToolBoxIconAlignTest.c
 * @brief      XToolBox 页头图标（§B7 / P0-80）行为级回归锁。
 * @details    对齐依据（Qt 6.8.3 源码 qtoolbox.cpp）：
 *             - 条目图标存进页头按钮：`insertItem` 的 `c.setIcon(icon)`
 *               （`:330`）→ `Page::setIcon` = `button->setIcon(is)`
 *               （`:64`）；`setItemIcon` 走同一路径（`:599-605`）；
 *             - 页头绘制**图标与文本一起画**：`QToolBoxButton::initStyleOption`
 *               填 `option->icon = icon()`（`:179`），`paintEvent` 调
 *               `drawControl(CE_ToolBoxTab, ...)`（`:202-209`）；style 侧
 *               `CE_ToolBoxTabLabel`（qcommonstyle.cpp:1952-1997）取
 *               `PM_SmallIconSize` 画图标、文本矩形从图标右缘起，无图标
 *               （`pm.isNull()`）时文本矩形直接取内容区 ⇒ **图标改变版式**；
 *             - 页头是**整条按钮**：点击任何像素（含图标列）都命中该页
 *               ——`_q_buttonClicked`（`:359-372`）由按钮 clicked 反查页，
 *               命中区不因图标收窄；
 *             - 禁用页头不响应：`setItemEnabled` 的
 *               `c->button->setEnabled(enabled)`（`:551`），按钮禁用后
 *               clicked 不发射；程序化 `setCurrentIndex` 不受此限
 *               （`:387-403` 只拦无效索引与同页）。
 *
 *             本仓实现对应点（Src/XGui/Widget/XToolBox.c；行号以符号名为
 *             准、随改动漂移，括号内为本文落地时的当前工作树位置）：图标
 *             路径存于 `XToolBoxItem.icon`（`:48`），绘制在
 *             `VX_toolBox_paintEvent`（`:457` 起；样式路径 `:540-585`、
 *             无样式回退 `:585-625`），解码走 `xtb2_drawItemIcon`
 *             （`:311-340`：`XIcon_init_file_2` → `XIcon_pixmap` →
 *             `XPixmap_toImage` → `XPainter_drawImage`；取图模式随条目启用位
 *             取 Normal/Disabled），16px 图标画在页头左起 x=6、文本从
 *             `6+16+4=26` 起（`XTOOLBOX_ICON_PX`，`:35`）；文本色由
 *             `xtb2_headerTextColor`（`:367-374`）按选中/启用取；命中映射
 *             `xtb2_headerIndexAt`（`:381-408`）只按 y 划分页头行 ⇒ 整行
 *             （含图标列）都可点。
 *
 * @note       两条版式口径分别锁：① 图标**真实像素**出现在图标位（用
 *             XPM 造真图标并断言可解码，避免「解码失败 ⇒ 画不出 ⇒ 假绿」）；
 *             ② 清除图标后页头带必须还原（闭环，防残影）。
 * @note       页头几何（`xtb2_headerIndexAt`）：激活页**之上**的页头每
 *             22px 一个；激活页占其页头之下的整块页高 `pageH`，**其后**
 *             的页头从 `22+pageH` 起。故本文件所有点击用例都把当前页
 *             置于**末页**——此时全部非末页页头都落在 `[0, 22*n)` 的
 *             确定区间，点击坐标不依赖 `pageH`（页高由滚动区按视口
 *             回填，值随布局而变，写死会漂）。
 * @note       [顶层窗口装饰遮蔽 2026-11-12，同 XToolBoxAlignTest:390-398]
 *             顶层窗口在 CSD 自绘标题栏下会被 28px 装饰条盖住页头条
 *             （y 0..21）⇒ 抓图/绘制用例把工具箱挂在**宿主控件**之下
 *             （`XWindowDecoration_activeFor=0`，装饰不进图），并
 *             `XWidget_show(host)`——隐藏控件不进入 paintTree
 *             （XWidget.c:12987 的 `!w->m_visible` 跳过），不 show 会抓到
 *             两张空白图、差异恒 0。
 * @author     XinYueC 团队
 */
#include "xq_align.h"

#include "XGuiConfig.h"

#if XGUI_ON && XWIDGET_ON && XFRAME_ON && XTOOLBOX_ON

#include "XToolBox.h"
#include "XWidget.h"
#include "XObject.h"
#include "XObject_Protected.h" /* XObject_event_base（事件注入路径）。 */
#include "XEvent.h"            /* XMouseEvent / 键码。 */
#include "XString.h"           /* 路径 UTF-8 承载。 */
#include "XClass.h"
#include "XImage.h"            /* 造图标 + XWidget_grab 像素比对。 */
#include "XIcon.h"             /* XIcon_init_file_2 / XIcon_isNull。 */
#include "XCoreApplication.h"  /* 延迟删除冲刷（泄漏门）。 */
#if XPIXMAPCACHE_ON
#include "XPixmapCache.h"      /* 收掉图标缩放缓存（见 xtb2_clearIconCaches）。 */
#endif
#include <stdio.h>             /* remove()：清测试自建的 /tmp 图标。 */

/* 与 XToolBox.c 同源的页头常量（改一侧必须同步另一侧）。 */
#define XTB2_ICON_PX 16   /**< XToolBox.c:35 XTOOLBOX_ICON_PX。 */
#define XTB2_ICON_X 6     /**< 图标左缘（XToolBox.c:565/593 的 x=6）。 */
#define XTB2_HEADER_H 22  /**< XToolBox.c:33 XTOOLBOX_HEADER_H。 */

/** @brief 造一个 8×8 纯色 XPM 图标（纯文本编解码、无 zlib 依赖）。 */
static void xtb2_makeIcon(const char* path, uint32_t color)
{
    XImage image;

    XImage_init_ex(&image, 8, 8, XImageFormat_ARGB32);
    XImage_fill(&image, color);
    (void)XImage_save_2(&image, path, "xpm", 100);
    XClassDeinit((XClass*)&image);
}

/**
 * @brief 预热「图标解码 + 缩放」一拍（**测量桩，不是被测行为**）。
 * @details [已根修 2026-11-16，key=iconcache] 旧口径曾以
 *          `XPixmap_cacheKey(&entry->m_pixmap)`（全局单调实例序列号）作
 *          缓存键，本控件每帧新建 XIcon 解码 ⇒ 每帧插新条目、无平台期。
 *          现 XIcon.c 改用「文件名 hash + 源像素内容指纹」派生稳定键
 *          （XPixmap_contentFingerprint），同资源跨实例必命中，见
 *          Test/QtAlign/XIconCacheAlignTest.c 的验证。
 *          本函数保留的唯一作用仍是把「图标解码 + 缩放」的一次性懒初始化
 *          （字体/编解码/缓存表结构）提前做掉，使首遍读数更稳定；实测删掉
 *          本调用后泄漏判读与保留时相同。用例收尾的 xtb2_clearIconCaches()
 *          仍必要：修复后每个资源仍插入 1 条缓存条目，clear 是收自己的账。
 * @param path 图标路径（须可解码，否则不会走缓存插入）。
 */
static void xtb2_warmIconCache(const char* path)
{
    int i;

    for (i = 0; i < 4; ++i) {
        XIcon icon;
        XPixmap pixmap;

        XMemset(&pixmap, 0, sizeof(pixmap)); /* 裸栈清零：防 init 的
            vtable 探测把前序栈残留误判为已初始化（同 XIcon.c:1060 注）。 */
        XIcon_init_file_2(&icon, path);
        if (XIcon_isNull(&icon)) {
            XClassDeinit((XClass*)&icon);
            continue;
        }
        XPixmap_init(&pixmap);
        XIcon_pixmap(&icon, XTB2_ICON_PX, XTB2_ICON_PX, XIconMode_Normal,
                     XIconState_Off, &pixmap);
        XClassDeinit((XClass*)&pixmap);
        XClassDeinit((XClass*)&icon);
    }
}

/**
 * @brief 清掉图标绘制留下的全局像素图缓存条目。
 * @details [已根修 2026-11-16] 缩放缓存键已改为「文件名 hash + 内容指纹」
 *          （XIcon.c/XPixmap.c），同资源跨实例命中、不再每帧插新条目；但
 *          本用例触发的绘制仍会为每个资源插入 1 条缓存条目，收尾调框架
 *          公开 API `XPixmapCache_clear()` 把缓存还给基线仍是**收自己的账**
 *          （去掉它会 `[LEAK]`），不是掩盖本域泄漏——真泄漏（对象/图像
 *          未释放）不在这份全局缓存里，仍会被泄漏门抓住。
 *          `XPIXMAPCACHE_ON=0` 的裁剪构建没有该缓存，退化为空操作。
 */
static void xtb2_clearIconCaches(void)
{
#if XPIXMAPCACHE_ON
    XPixmapCache_clear();
#endif
}

/** @brief 图标文件能否被 XIcon 解码（决定图标像素断言是否可做）。 */
static bool xtb2_iconDecodable(const char* path)
{
    XIcon probe;
    bool ok;

    XMemset(&probe, 0, sizeof(probe)); /* 裸栈清零：同 XIcon.c:1060 注。 */
    XIcon_init_file_2(&probe, path);
    ok = !XIcon_isNull(&probe);
    XClassDeinit((XClass*)&probe);
    return ok;
}

/** @brief 向控件直发一次鼠标按下事件（坐标=控件本地坐标）。 */
static void xtb2_mousePress(XWidget* target, int x, int y)
{
    XMouseEvent me;
    XPoint pos;

    if (!target)
        return;
    XPoint_init(&pos, x, y);
    XMouseEvent_init(&me, XEVENT_TYPE_MOUSE_BUTTON_PRESS,
                     XMouseButton_LeftButton, XKeyboardModifier_NoModifier,
                     pos);
    XObject_event_base((XObject*)target, (XEvent*)&me);
}

/** @brief 两张等尺寸抓图在 [x0,x1)×[y0,y1) 区间的差异像素数。 */
static int xtb2_regionDiff(const XImage* a, const XImage* b, int x0, int y0,
                           int x1, int y1)
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
    if (x1 > w) x1 = w;
    if (y1 > h) y1 = h;
    for (y = y0; y < y1; ++y) {
        for (x = x0; x < x1; ++x) {
            if (XImage_pixel(a, x, y) != XImage_pixel(b, x, y)) ++diff;
        }
    }
    return diff;
}

/**
 * @brief 在差异图中求「图标像素」的实际左/右缘（精确锁 x=6 与图标宽）。
 * @details 逐列判断该列在页头行 [y0,y1) 内是否存在与基准图不同的像素；
 *          返回是否找到，并回填最左/最右差异列。
 *          用途：`regionDiff > 0` 只能证明「图标位里出现过差异」，无法
 *          区分图标画在 x=6 还是被挪到 x=14（两者都会让该区间非零）。
 *          本函数把断言收紧到**左缘等于绘制口径的 6**，从而真正锁住
 *          「图标行内左起、不压文字」（台账 B7 验收项 4 的 ±1px 对拍口径）。
 * @param with 带图标的抓图。
 * @param plain 无图标的基准抓图（同尺寸）。
 * @param x0/x1 搜索列区间（本地坐标）。
 * @param y0/y1 搜索行区间。
 * @param leftOut 命中时回填最左差异列。
 * @param rightOut 命中时回填最右差异列。
 * @return 找到差异列返回 true。
 */
static bool xtb2_diffColumnSpan(const XImage* with, const XImage* plain,
                                int x0, int x1, int y0, int y1,
                                int* leftOut, int* rightOut)
{
    int x;
    int y;
    int left = -1;
    int right = -1;

    if (!with || !plain || !leftOut || !rightOut) return false;
    if (XImage_width(with) != XImage_width(plain) ||
        XImage_height(with) != XImage_height(plain))
        return false;
    if (x1 > XImage_width(with)) x1 = XImage_width(with);
    if (y1 > XImage_height(with)) y1 = XImage_height(with);
    for (x = x0; x < x1; ++x) {
        bool colDiff = false;
        for (y = y0; y < y1; ++y) {
            if (XImage_pixel(with, x, y) != XImage_pixel(plain, x, y)) {
                colDiff = true;
                break;
            }
        }
        if (!colDiff) continue;
        if (left < 0) left = x;
        right = x;
    }
    if (left < 0) return false;
    *leftOut = left;
    *rightOut = right;
    return true;
}

/** @brief 造一只挂在宿主下、含 count 个空文本页的工具箱。 */
static XToolBox* xtb2_makeBox(XWidget* host, int count)
{
    XToolBox* box;
    int i;

    if (!host) return NULL;
    box = XToolBox_create(host, 0);
    if (!box) return NULL;
    XWidget_setGeometry((XWidget*)box, 0, 0, 160, 220);
    for (i = 0; i < count; ++i) {
        XWidget* page = XWidget_create(NULL, 0);
        if (!page) break;
        XWidget_resize(page, 160, 50);
        if (XToolBox_addItem(box, page, "") < 0) break;
    }
    return box;
}

/* ==================== ① itemIcon 存取往返（公共 API 契约） ============ */

/** @brief setItemIcon/itemIcon 的往返、覆盖、清除、越界与条目隔离。
 * @details 对标 `QToolBox::setItemIcon` / `itemIcon`
 *          （qtoolbox.cpp:599-605 / :649-654）：越界返回空图标（X 侧
 *          `itemIcon` 返回 NULL 借用指针），设置/清除只作用于目标条目。 */
XQ_TEST(XToolBoxIcon, setGetRoundTripAndClear)
{
    XToolBox* box = XToolBox_create(NULL, 0);
    XWidget* p0;
    XWidget* p1;
    const XString* got;
    const char* pathA = "/tmp/xtb2_icon_a.xpm";
    const char* pathB = "/tmp/xtb2_icon_b.xpm";

    XQ_REQUIRE(box != NULL);
    p0 = XWidget_create(NULL, 0);
    p1 = XWidget_create(NULL, 0);
    XQ_REQUIRE(p0 != NULL && p1 != NULL);
    XWidget_resize(p0, 160, 50);
    XWidget_resize(p1, 160, 50);
    XWidget_setGeometry((XWidget*)box, 0, 0, 160, 220);
    XQ_EQ_I64(XToolBox_addItem(box, p0, "页一"), 0);
    XQ_EQ_I64(XToolBox_addItem(box, p1, "页二"), 1);
    XQ_EQ_I64(XToolBox_count(box), 2);

    /* 初始无图标：借用返回 NULL（不得释放）。 */
    XQ_CHECK(XToolBox_itemIcon(box, 0) == NULL);
    XQ_CHECK(XToolBox_itemIcon(box, 1) == NULL);

    /* 设置图标路径（UTF-8 重载）⇒ 精确回读同一路径。 */
    XToolBox_setItemIcon_2(box, 0, pathA);
    got = XToolBox_itemIcon(box, 0);
    XQ_CHECK_PTR((void*)got);
    if (got) XQ_EQ_STR(XString_toUtf8(got), pathA);
    /* 只作用于目标条目：第 2 条仍无图标。 */
    XQ_CHECK(XToolBox_itemIcon(box, 1) == NULL);

    /* 覆盖设置：新路径替换旧值，不留旧串。 */
    XToolBox_setItemIcon_2(box, 0, pathB);
    got = XToolBox_itemIcon(box, 0);
    XQ_CHECK_PTR((void*)got);
    if (got) XQ_EQ_STR(XString_toUtf8(got), pathB);

    /* XString 主版本入口与 UTF-8 重载同源（同一槽位、同一借用返回值）。 */
    {
        XString* s = XString_create_utf8(pathA);
        XQ_REQUIRE(s != NULL);
        XToolBox_setItemIcon(box, 0, s);
        XClassDelete((XClass*)s); /* 主版本按拷贝语义存值，源可释放。 */
        got = XToolBox_itemIcon(box, 0);
        XQ_CHECK_PTR((void*)got);
        if (got) XQ_EQ_STR(XString_toUtf8(got), pathA);
    }

    /* NULL 清除（等价 Qt `setItemIcon(index, QIcon())`）；再清幂等不崩。 */
    XToolBox_setItemIcon_2(box, 0, NULL);
    XQ_CHECK(XToolBox_itemIcon(box, 0) == NULL);
    XToolBox_setItemIcon_2(box, 0, NULL);
    XQ_CHECK(XToolBox_itemIcon(box, 0) == NULL);

    /* 越界索引安全返回 NULL（qtoolbox.cpp:651-653：越界返回空图标）；
     * 越界设置是无操作。 */
    XQ_CHECK(XToolBox_itemIcon(box, 2) == NULL);
    XQ_CHECK(XToolBox_itemIcon(box, -1) == NULL);
    XToolBox_setItemIcon_2(box, 99, pathA);
    XQ_CHECK(XToolBox_itemIcon(box, 99) == NULL);

    XClassDelete((XClass*)box); /* 页随容器级联释放。 */
    /* 清掉本用例在 /tmp 造的图标（同 XComboBoxAlignTest.c:814 先例）。 */
    remove(pathA);
    remove(pathB);
}

/* ==================== ② 图标进入页头绘制（§B7 核心缺口） ============ */

/** @brief icon 必须参与页头**绘制**，而不只是存取。
 * @details 变量隔离法：两只同尺寸、同内容（空标题）的工具箱，唯一差别是
 *          其中一只首条带图标 ⇒ 抓图差异只能来自图标像素。空标题让
 *          「无图标」那只的页头只剩样式底，图标位 x∈[6,22) 是纯背景，
 *          差异即图标本身。
 *          再清图标 ⇒ 页头带必须与「自始无图标」逐像素一致（闭环）。 */
XQ_TEST(XToolBoxIcon, iconDrawnInHeader)
{
    XWidget* hostA = XWidget_create(NULL, 0);
    XWidget* hostB = XWidget_create(NULL, 0);
    XToolBox* withIcon;
    XToolBox* withoutIcon;
    XImage* gWith;
    XImage* gWithout;
    int diffIconCol;
    int diffBand;
    const char* iconPath = "/tmp/xtb2_icon_paint.xpm";

    XQ_REQUIRE(hostA != NULL && hostB != NULL);
    xtb2_makeIcon(iconPath, 0xFF3366CCu); /* 蓝底，与页头样式底高对比。 */
    /* 前置：XPM 走纯文本编解码（XIMAGECODEC_XPM_ON 缺省 1，无 zlib 依赖）。
     * 必须可解码——绘制路径在 XIcon_isNull 时提前返回，不可解码会得到
     * 恒 0 差异的假绿。 */
    XQ_CHECK_MSG(xtb2_iconDecodable(iconPath),
                 "XPM 测试图标必须可解码（%s）", iconPath);
    /* 先把框架的图标缩放缓存喂到平台期，泄漏门才只反映本用例的真实释放
     * （见 xtb2_warmIconCache 的说明）。 */
    xtb2_warmIconCache(iconPath);

    XWidget_resize(hostA, 160, 220);
    XWidget_resize(hostB, 160, 220);
    withIcon = xtb2_makeBox(hostA, 1);
    withoutIcon = xtb2_makeBox(hostB, 1);
    XQ_REQUIRE(withIcon != NULL && withoutIcon != NULL);
    XQ_EQ_I64(XToolBox_count(withIcon), 1);
    XQ_EQ_I64(XToolBox_count(withoutIcon), 1);
    XToolBox_setItemIcon_2(withIcon, 0, iconPath);
    XQ_CHECK_PTR((void*)XToolBox_itemIcon(withIcon, 0));

    /* 必须 show：隐藏控件不进入 paintTree（XWidget.c:12987）。 */
    XWidget_show(hostA);
    XWidget_show(hostB);

    gWithout = XWidget_grab((XWidget*)withoutIcon);
    XQ_REQUIRE(gWithout != NULL);
    gWith = XWidget_grab((XWidget*)withIcon);
    XQ_REQUIRE(gWith != NULL);

    /* ① 图标位（x∈[6,22)、首条页头行 y∈[0,22)）必须有图标像素。 */
    diffIconCol = xtb2_regionDiff(gWith, gWithout, XTB2_ICON_X, 0,
                                  XTB2_ICON_X + XTB2_ICON_PX,
                                  XTB2_HEADER_H);
    XQ_CHECK_MSG(diffIconCol > 0,
                 "页头图标位必须有图标像素（差异 %d）", diffIconCol);

    /* ② 整条页头行也必须有差异（图标参与页头绘制）。 */
    diffBand = xtb2_regionDiff(gWith, gWithout, 0, 0,
                               XImage_width(gWith), XTB2_HEADER_H);
    XQ_CHECK_MSG(diffBand > 0,
                 "图标必须进入页头绘制（整行差异 %d）", diffBand);

    /* ③ 图标**左缘必须恰为 6**（绘制口径），且右缘不越过
     *    「6 + 图标宽 - 1」——只断言「图标位有差异」无法区分图标被挪到
     *    x=14（压到文本位）的情形，这里把位置收紧到实参。 */
    {
        int left = -1;
        int right = -1;
        bool found = xtb2_diffColumnSpan(gWith, gWithout, 0,
                                         XImage_width(gWith), 0,
                                         XTB2_HEADER_H, &left, &right);
        XQ_CHECK_MSG(found, "页头行内必须能找到图标像素列");
        if (found) {
            XQ_CHECK_MSG(left == XTB2_ICON_X,
                         "图标左缘必须为 %d（实际 %d）", XTB2_ICON_X, left);
            XQ_CHECK_MSG(right <= XTB2_ICON_X + XTB2_ICON_PX - 1,
                         "图标右缘必须不越过 %d（实际 %d）",
                         XTB2_ICON_X + XTB2_ICON_PX - 1, right);
        }
    }

    /* ④ 清除图标 ⇒ 页头带与「自始无图标」逐像素一致（不得留残影）。
     *    只比页头带（y 0..21）：下图区两态本就应相同，整图比较会把与
     *    图标无关的差异（滚动区脏块等）算进来。 */
    XToolBox_setItemIcon(withIcon, 0, NULL);
    XQ_CHECK(XToolBox_itemIcon(withIcon, 0) == NULL);
    {
        XImage* gCleared = XWidget_grab((XWidget*)withIcon);
        int diffCleared;
        XQ_REQUIRE(gCleared != NULL);
        diffCleared = xtb2_regionDiff(gCleared, gWithout, 0, 0,
                                      XImage_width(gCleared), XTB2_HEADER_H);
        XQ_CHECK_MSG(diffCleared == 0,
                     "清除图标后页头必须还原（残留差异 %d）", diffCleared);
        XClassDelete((XClass*)gCleared);
    }

    XClassDelete((XClass*)gWith);
    XClassDelete((XClass*)gWithout);
    XClassDelete((XClass*)withIcon);
    XClassDelete((XClass*)withoutIcon);
    XClassDelete((XClass*)hostA);
    XClassDelete((XClass*)hostB);
    XCoreApplication_sendPostedEvents(NULL, XEVENT_TYPE_DEFERRED_DELETE);
    /* 收掉图标绘制写入全局像素图缓存的条目：缓存键源自单调递增的 pixmap
     * 序列号 ⇒ 每次重绘都是新键、永不命中（框架侧既有缺陷，详见
     * xtb2_clearIconCaches 的说明）。本用例必须把这份「框架缓存」还给
     * 基线，泄漏门才只衡量本域代码的释放；用框架公开 API 而非自造机件。 */
    xtb2_clearIconCaches();
    remove(iconPath); /* 清 /tmp 自建图标（同 XComboBoxAlignTest.c:814）。 */
}

/* ==================== ③ 图标列命中：整条页头可点 ==================== */

/** @brief 页头命中是**整行**，图标列不是死区；内容区仍不是页头。
 * @details Qt 侧页头是整只 QToolBoxButton，点击任意像素（含图标）经
 *          `_q_buttonClicked`（qtoolbox.cpp:359-372）切页。X 侧
 *          `xtb2_headerIndexAt` 只按 y 划分 ⇒ 图标列 x 必须与别处等价。
 *
 *          几何设计：3 页、当前页置**末页**（索引 2）——此时三个页头都
 *          落在 `[0,22) / [22,44) / [44,66)`，点击坐标与 `pageH` 无关
 *          （`pageH` 由滚动区按视口回填，写死会漂）。 */
XQ_TEST(XToolBoxIcon, iconColumnHitsHeader)
{
    XWidget* host = XWidget_create(NULL, 0);
    XToolBox* box;
    const char* iconPath = "/tmp/xtb2_icon_hit.xpm";

    XQ_REQUIRE(host != NULL);
    xtb2_makeIcon(iconPath, 0xFFCC6633u);
    XWidget_resize(host, 160, 220);
    box = xtb2_makeBox(host, 3);
    XQ_REQUIRE(box != NULL);
    XQ_EQ_I64(XToolBox_count(box), 3);
    /* 中间页（索引 1）带图标：待验证的「图标列点击」就落在它头上。 */
    XToolBox_setItemIcon_2(box, 1, iconPath);
    XQ_CHECK_PTR((void*)XToolBox_itemIcon(box, 1));

    /* 当前页置末页 ⇒ 三个页头都在内容区之上。 */
    XToolBox_setCurrentIndex(box, 2);
    XQ_EQ_I64(XToolBox_currentIndex(box), 2);

    /* ① 点第 1 页页头（带图标）的**图标列**中心 (10, 33) ⇒ 切到第 1 页。
     *    y=33 ∈ [22,44) = 第 1 页页头行；x=10 ∈ [6,22) = 图标位。 */
    xtb2_mousePress((XWidget*)box, XTB2_ICON_X + 4, XTB2_HEADER_H + 11);
    XQ_EQ_I64(XToolBox_currentIndex(box), 1);

    /* ② 点第 0 页页头的图标列 (10, 11) ⇒ 切到第 0 页（图标列在任意页头
     *    行上都等价命中，与是否真的带图标无关）。 */
    xtb2_mousePress((XWidget*)box, XTB2_ICON_X + 4, 11);
    XQ_EQ_I64(XToolBox_currentIndex(box), 0);

    /* ③ 内容区不是页头：当前页 0 的内容区从 y=22 起、占整块 pageH
     *    （pageH ≥ 视口高下限 30）⇒ y=30 落在内容区，点击不切页。
     *    这条锁住「图标列不会把内容区误判成页头」。 */
    XQ_CHECK_MSG(XWidget_height((XWidget*)box) == 220, "几何前置");
    xtb2_mousePress((XWidget*)box, XTB2_ICON_X + 4, 30);
    XQ_EQ_I64(XToolBox_currentIndex(box), 0);

    XClassDelete((XClass*)box);
    XClassDelete((XClass*)host);
    XCoreApplication_sendPostedEvents(NULL, XEVENT_TYPE_DEFERRED_DELETE);
    remove(iconPath); /* 清 /tmp 自建图标（同 XComboBoxAlignTest.c:814）。 */
}

/* ==================== ④ 禁用页头（带图标）不响应 ==================== */

/** @brief 禁用条目的页头（含图标列）点击必须被忽略；**且外观可区分**。
 * @details Qt 侧 `setItemEnabled` 下发 `button->setEnabled(enabled)`
 *          （qtoolbox.cpp:551），按钮禁用后 clicked 不发射、且绘制态变灰
 *          ——`CE_ToolBoxTabLabel` 取
 *          `enabled ? QIcon::Normal : QIcon::Disabled`
 *          （qcommonstyle.cpp:1958-1960）画图标，并以
 *          `drawItemText(..., enabled, txt, QPalette::ButtonText)`
 *          （qcommonstyle.cpp:1990）画文本，禁用控件的 QPalette 自动切到
 *          Disabled 色组（qwidget.cpp:4542-4550）⇒ 文字灰化。
 *          X 侧 `VX_toolBox_mousePressEvent`（XToolBox.c:630 入口，
 *          :652-655 忽略）拦交互；绘制侧由 `xtb2_drawItemIcon` 的模式参数
 *          与 `xtb2_headerTextColor` 承接灰化。
 *          禁用只拦**交互**：程序化 `setCurrentIndex` 仍可落在该页
 *          （Qt `:387-403` 同口径；XToolBoxAlignTest:184-187 已锁）。
 *
 *          几何同用例 ③：当前页置末页 ⇒ 页头坐标确定。禁用**非当前页**
 *          不触发自动迁移（`:552` 只在禁用当前页时迁移）。 */
XQ_TEST(XToolBoxIcon, disabledHeaderWithIconIgnored)
{
    XWidget* host = XWidget_create(NULL, 0);
    XToolBox* box;
    const char* iconPath = "/tmp/xtb2_icon_disabled.xpm";
    XImage* gEnabled;
    XImage* gDisabled;
    int diffAll;
    int diffIconCol;

    XQ_REQUIRE(host != NULL);
    xtb2_makeIcon(iconPath, 0xFF33AA55u);
    XWidget_resize(host, 160, 220);
    box = xtb2_makeBox(host, 3);
    XQ_REQUIRE(box != NULL);
    XToolBox_setItemIcon_2(box, 1, iconPath);
    XToolBox_setCurrentIndex(box, 2);
    XQ_EQ_I64(XToolBox_currentIndex(box), 2);
    XQ_CHECK(XToolBox_isItemEnabled(box, 1));
    XWidget_show(host);

    /* ---- 视觉：禁用前后第 1 页页头（y∈[22,44)）必须可区分 ---- */
    {
        int wasDecodable = xtb2_iconDecodable(iconPath);
        XQ_CHECK_MSG(wasDecodable, "禁用视觉断言需可解码图标（%s）", iconPath);
    }
    gEnabled = XWidget_grab((XWidget*)box);
    XQ_REQUIRE(gEnabled != NULL);
    XToolBox_setItemEnabled(box, 1, false);
    gDisabled = XWidget_grab((XWidget*)box);
    XQ_REQUIRE(gDisabled != NULL);
    /* 整条第 1 页页头带必须变化（图标 + 文本灰化）。 */
    diffAll = xtb2_regionDiff(gEnabled, gDisabled, 0, XTB2_HEADER_H,
                              XImage_width(gEnabled),
                              XTB2_HEADER_H * 2);
    XQ_CHECK_MSG(diffAll > 0,
                 "禁用页头外观必须与启用态可区分（差异 %d）", diffAll);
    /* 图标列单独也必须变化（图标取 Disabled 模式，qcommonstyle.cpp:1958）。 */
    diffIconCol = xtb2_regionDiff(gEnabled, gDisabled, XTB2_ICON_X,
                                  XTB2_HEADER_H,
                                  XTB2_ICON_X + XTB2_ICON_PX,
                                  XTB2_HEADER_H * 2);
    XQ_CHECK_MSG(diffIconCol > 0,
                 "禁用页头图标必须灰化（图标列差异 %d）", diffIconCol);
    XClassDelete((XClass*)gEnabled);
    XClassDelete((XClass*)gDisabled);

    /* ---- 交互：禁用第 1 页（非当前页 ⇒ 不触发自动迁移）⇒ 其页头
     * （图标列与普通列）点击被忽略、当前页不动。 ---- */
    XQ_CHECK(!XToolBox_isItemEnabled(box, 1));
    XQ_EQ_I64(XToolBox_currentIndex(box), 2);
    xtb2_mousePress((XWidget*)box, XTB2_ICON_X + 4, XTB2_HEADER_H + 11);
    XQ_EQ_I64(XToolBox_currentIndex(box), 2);

    /* 同一条页头的非图标列（x=100）也被忽略 ⇒ 不是「图标列特例」。 */
    xtb2_mousePress((XWidget*)box, 100, XTB2_HEADER_H + 11);
    XQ_EQ_I64(XToolBox_currentIndex(box), 2);

    /* 程序化切换不受 enabled 限制（Qt 同口径）。 */
    XToolBox_setCurrentIndex(box, 1);
    XQ_EQ_I64(XToolBox_currentIndex(box), 1);
    XToolBox_setCurrentIndex(box, 2);

    /* 重新启用 ⇒ 图标列点击恢复切页。 */
    XToolBox_setItemEnabled(box, 1, true);
    XQ_CHECK(XToolBox_isItemEnabled(box, 1));
    xtb2_mousePress((XWidget*)box, XTB2_ICON_X + 4, XTB2_HEADER_H + 11);
    XQ_EQ_I64(XToolBox_currentIndex(box), 1);

    XClassDelete((XClass*)box);
    XClassDelete((XClass*)host);
    XCoreApplication_sendPostedEvents(NULL, XEVENT_TYPE_DEFERRED_DELETE);
    xtb2_clearIconCaches(); /* 本用例也走过绘制（grab 两态）。 */
    remove(iconPath);       /* 清 /tmp 自建图标（同 XComboBoxAlignTest.c:814）。 */
}

#endif /* XGUI_ON && XWIDGET_ON && XFRAME_ON && XTOOLBOX_ON */
