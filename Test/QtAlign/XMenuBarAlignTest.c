/**
 * @file       XMenuBarAlignTest.c
 * @brief      XMenuBar × Qt 6.8 QMenuBar 对齐契约测试。
 * @details    对齐依据：/opt/Qt/6.8.3/gcc_64/include/QtWidgets/qmenubar.h
 *             与本机 Qt 6.8.3 源码
 *             /home/xinyue/Qt-src/6.8.3/Src/qtbase/src/widgets/widgets/qmenubar.cpp。
 *             关键行为用本机 Qt 6.8 探针（QT_QPA_PLATFORM=offscreen）实测：
 *             - 构造默认 defaultUp=false、isNativeMenuBar=false、动作表空；
 *             - addMenu(title) 后动作表 +1，动作文本 = 菜单标题；
 *             - addMenu(QMenu*) 返回该菜单的代表动作（qmenubar.cpp
 *               addMenu → addAction(menu->menuAction())）；
 *             - 对已在栏内的动作 insertAction 保持"先摘后插"顺序语义；
 *             - sizeHint 高 = minimumSizeHint 高（探针 23/23），
 *               heightForWidth 与宽度无关（探针恒 23）；
 *             - cornerWidget() 默认查 TopRightCorner，未设置返回 NULL。
 * @author     XinYueC 团队
 */
#include "xq_align.h"

#include "XGuiConfig.h"

#if XGUI_ON && XWIDGET_ON && XMENUBAR_ON && XMENU_ON && XACTION_ON

#include "XMenuBar.h"
#include "XMenu.h"
#include "XAction.h"
#include "XWidget.h"
#include "XObject.h"
#include "XString.h"
#include "XGeometry.h"
#include "XClass.h"

/** @brief 动作文本（借用视图）。 */
static const char* xmb_text(const XAction* action)
{
    return XString_toUtf8((const XString*)XAction_text_const(action));
}

/** @brief 按左上到右下的扫描顺序取栏内第一个命中的动作（用于在只有
 *         actionAt 的公开面上验证条目排列）。 */
static XAction* xmb_firstHit(XMenuBar* bar, int width, int height)
{
    XPoint point;
    int y;
    int x;
    for (y = 1; y < height; y += 3) {
        for (x = 1; x < width; x += 3) {
            XAction* hit;
            XPoint_init(&point, x, y);
            hit = XMenuBar_actionAt(bar, &point);
            if (hit) return hit;
        }
    }
    return NULL;
}

/* ==================== ① 默认值 + 菜单注册 + 几何 ==================== */

XQ_TEST(XMenuBar, defaultsAndMenuRegistration)
{
    XMenuBar* bar = XMenuBar_create(NULL, 0);
    XMenu* menu;
    XAction* action;
    XSize hint;
    XRect geo;
    XPoint inside;
    XPoint outside;

    XQ_REQUIRE(bar != NULL);
    XWidget_resize((XWidget*)bar, 320, 24);

    /* 构造默认值（QMenuBar Q_PROPERTY 缺省）。 */
    XQ_CHECK(!XMenuBar_isDefaultUp(bar));
    XQ_CHECK(!XMenuBar_isNativeMenuBar(bar));
    XQ_EQ_I64(XMenuBar_actionCount(bar), 0);
    XQ_CHECK(XMenuBar_activeAction(bar) == NULL);
    XQ_CHECK(XMenuBar_cornerWidget(bar, XMenuBarCorner_TopRight) == NULL);
    XQ_CHECK(XMenuBar_platformMenuBar(bar) == NULL);

    /* addMenu(title)：`new QMenu(title, this)`——菜单以菜单栏为父（Qt
     * 持有，qmenubar.cpp:738-744）；加入的是 menu->menuAction()，
     * 动作文本 = 菜单标题（gaps 196/194）。 */
    menu = XMenuBar_addMenu_2(bar, "文件");
    XQ_REQUIRE(menu != NULL);
    XQ_CHECK(XObject_parent((XObject*)menu) == (XObject*)bar);
    XQ_EQ_I64(XMenuBar_actionCount(bar), 1);
    XQ_EQ_STR(xmb_text(xmb_firstHit(bar, 320, 24)), "文件");
    {
        XAction* menuAction = XMenu_menuAction(menu);
        XQ_REQUIRE(menuAction != NULL);
        XQ_CHECK(XMenuBar_actionGeometry(bar, menuAction).width > 0);
        /* 标题联动：菜单改标题 → 栏条目文本跟随（menuAction 文本同步）。 */
        XMenu_setTitle_2(menu, "文件2");
        XQ_EQ_STR(xmb_text(menuAction), "文件2");
        XQ_EQ_STR(xmb_text(xmb_firstHit(bar, 320, 24)), "文件2");
        XMenu_setTitle_2(menu, "文件");
    }

    /* addMenu(QMenu*)：外部菜单不转移所有权，返回其 menuAction。 */
    {
        XMenu* external = XMenu_create(); /* 无父：菜单栏不取得所有权 */
        XAction* ret;
        XQ_REQUIRE(external != NULL);
        XMenu_setTitle_2(external, "编辑");
        ret = XMenuBar_addMenu(bar, external);
        XQ_CHECK(ret == XMenu_menuAction(external));
        XQ_CHECK(XObject_parent((XObject*)external) == NULL);
        XQ_EQ_I64(XMenuBar_actionCount(bar), 2);
        /* 菜单先亡：menuAction 随之销毁，destroyed 槽自动从栏内摘除。 */
        XClassDelete((XClass*)external);
        XQ_EQ_I64(XMenuBar_actionCount(bar), 1);
    }

    /* addAction(text)：返回归菜单栏所有的动作，文本/几何/命中一致。 */
    action = XMenuBar_addAction_2(bar, "帮助");
    XQ_REQUIRE(action != NULL);
    XQ_EQ_I64(XMenuBar_actionCount(bar), 2);
    XQ_EQ_STR(xmb_text(action), "帮助");
    geo = XMenuBar_actionGeometry(bar, action);
    XQ_CHECK(geo.width > 0 && geo.height > 0);
    XPoint_init(&inside, geo.x + 1, geo.y + geo.height / 2);
    XQ_CHECK(XMenuBar_actionAt(bar, &inside) == action);
    XPoint_init(&outside, geo.x + geo.width + 500, geo.y);
    XQ_CHECK(XMenuBar_actionAt(bar, &outside) == NULL);

    /* 尺寸链条：sizeHint/最小尺寸正、heightForWidth 与宽度无关
       （Qt 探针：三种宽度都返回同一高度）。 */
    hint = XMenuBar_sizeHint(bar);
    XQ_CHECK(hint.width > 0 && hint.height > 0);
    XQ_EQ_I64(XMenuBar_heightForWidth(bar, 100),
              XMenuBar_heightForWidth(bar, 1000));
    XQ_CHECK(XMenuBar_minimumSizeHint(bar).height > 0);
    XQ_CHECK(XMenuBar_sizeHint(NULL).width == 0);
    XQ_EQ_I64(XMenuBar_actionCount(NULL), 0);
    XQ_CHECK(XMenuBar_actionAt(NULL, NULL) == NULL);
    XQ_CHECK(XMenuBar_actionGeometry(NULL, NULL).width == 0);

    /* clear：动作表清空、激活动作复位。 */
    XMenuBar_setActiveAction(bar, action);
    XQ_CHECK(XMenuBar_activeAction(bar) == action);
    XMenuBar_clear(bar);
    XQ_EQ_I64(XMenuBar_actionCount(bar), 0);
    XQ_CHECK(XMenuBar_activeAction(bar) == NULL);
    XQ_CHECK(xmb_firstHit(bar, 320, 24) == NULL);

    XClassDelete((XClass*)menu); /* addMenu(title) 的菜单归菜单栏，先删亦可 */
    XClassDelete((XClass*)bar);
}

/* ==================== ② 动作插入/摘除所有权 + 角落控件 ==================== */

XQ_TEST(XMenuBar, insertRemoveOwnershipAndCornerWidget)
{
    XMenuBar* bar = XMenuBar_create(NULL, 0);
    XAction* created;
    XAction* external;
    XWidget* corner;

    XQ_REQUIRE(bar != NULL);
    XWidget_resize((XWidget*)bar, 300, 24);

    /* insertAction 注入的是借用动作：removeAction 后对象仍存活
       （对标 QWidget::removeAction 不释放）。 */
    created = XMenuBar_addAction_2(bar, "内置");
    external = XAction_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, NULL, "外部");
    XQ_REQUIRE(created != NULL && external != NULL);
    XQ_EQ_I64(XMenuBar_actionCount(bar), 1);
    XQ_CHECK(XMenuBar_insertAction(bar, created, external) == external);
    XQ_EQ_I64(XMenuBar_actionCount(bar), 2);
    /* 插入在 before 之前：外部动作的条目几何应在内置动作左侧。 */
    {
        XRect geoIn = XMenuBar_actionGeometry(bar, created);
        XRect geoEx = XMenuBar_actionGeometry(bar, external);
        XQ_CHECK(geoEx.x < geoIn.x);
    }

    XMenuBar_removeAction(bar, external);
    XQ_EQ_I64(XMenuBar_actionCount(bar), 1);
    /* 外部动作仍可用（未被菜单栏销毁）。 */
    XAction_setText_2(external, "外部2");
    XQ_EQ_STR(xmb_text(external), "外部2");

    /* 已在栏内的动作再 insert：先摘后插的移动语义（Qt 同）。 */
    XQ_CHECK(XMenuBar_insertAction(bar, NULL, external) == external);
    XQ_EQ_I64(XMenuBar_actionCount(bar), 2);
    /* 摘除不存在的动作是无操作。 */
    XMenuBar_removeAction(bar, NULL);
    XMenuBar_removeAction(bar, external);
    XQ_EQ_I64(XMenuBar_actionCount(bar), 1);

    /* 角落控件：按 Qt::Corner 两角独立存放，清除传 NULL
       （qmenubar.h `setCornerWidget(QWidget*, Qt::Corner = TopRightCorner)`）。 */
    corner = XWidget_create(NULL, 0);
    XQ_REQUIRE(corner != NULL);
    XMenuBar_setCornerWidget(bar, corner, XMenuBarCorner_TopRight);
    XQ_CHECK(XMenuBar_cornerWidget(bar, XMenuBarCorner_TopRight) == corner);
    XQ_CHECK(XMenuBar_cornerWidget(bar, XMenuBarCorner_TopLeft) == NULL);
    XMenuBar_setCornerWidget(bar, corner, XMenuBarCorner_TopLeft);
    XQ_CHECK(XMenuBar_cornerWidget(bar, XMenuBarCorner_TopLeft) == corner);
    /* Qt 6.8 的 setCornerWidget 只写目标角槽（qmenubar.cpp:1716-1741），
     * 不把控件从另一个角槽摘除：本机通配探针实测把同一控件依次设到
     * TopRight、TopLeft 后，cornerWidget(TopRight) 仍返回该控件（两槽
     * 同时持有）。旧断言按「搬移」写，与 Qt 不符。 */
    XQ_CHECK(XMenuBar_cornerWidget(bar, XMenuBarCorner_TopRight) == corner);
    XMenuBar_setCornerWidget(bar, NULL, XMenuBarCorner_TopLeft);
    XQ_CHECK(XMenuBar_cornerWidget(bar, XMenuBarCorner_TopLeft) == NULL);
    /* 清左槽不影响右槽（Qt 同：两角各自独立）。 */
    XQ_CHECK(XMenuBar_cornerWidget(bar, XMenuBarCorner_TopRight) == corner);
    XMenuBar_setCornerWidget(bar, NULL, XMenuBarCorner_TopRight);
    XQ_CHECK(XMenuBar_cornerWidget(bar, XMenuBarCorner_TopRight) == NULL);

    /* defaultUp / nativeMenuBar 往返。 */
    XMenuBar_setDefaultUp(bar, true);
    XQ_CHECK(XMenuBar_isDefaultUp(bar));
    XMenuBar_setDefaultUp(bar, false);
    XQ_CHECK(!XMenuBar_isDefaultUp(bar));
    XMenuBar_setNativeMenuBar(bar, true);
    XQ_CHECK(XMenuBar_isNativeMenuBar(bar));
    XMenuBar_setNativeMenuBar(bar, false);
    XQ_CHECK(!XMenuBar_isNativeMenuBar(bar));

    XClassDelete((XClass*)corner);
    XClassDelete((XClass*)external);
    XClassDelete((XClass*)bar);
}

#endif /* XGUI_ON && XWIDGET_ON && XMENUBAR_ON && XMENU_ON && XACTION_ON */
