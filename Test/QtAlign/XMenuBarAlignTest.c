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
 *             2026-11-11 台账整改同步（P0-37/P0-81/P3-23~28）：
 *             - clear 只摘表**不 delete**（Qt QWidget::clear 口径）：
 *               addAction 返回的动作在 clear 后仍存活、归调用方；
 *             - 增删动作联动几何失效（updateGeometry）⇒ sizeHint 随增删变化；
 *             - 无平台菜单栏后端 ⇒ setNativeMenuBar 无效果、读恒 false
 *               （qmenubar.cpp:1779-1783），旧「存储位往返」断言作废；
 *             - 角件接管：setCornerWidget 换父 + sizeHint/条目起点让位；
 *             - defaultUp 弹出落位本体需窗口平台（无头不弹窗），锁状态位。
 * @author     XinYueC 团队
 */
#include "xq_align.h"

#include "XGuiConfig.h"

#if XGUI_ON && XWIDGET_ON && XMENUBAR_ON && XMENU_ON && XACTION_ON

#include "XMenuBar.h"
#include "XMenu.h"
#include "XAction.h"
#include "XWidget.h"
#include "XWidget_Protected.h" /* XWidget_setSizeHint（P3-25 让位断言用；
                                  同目录 XGroupBox/Splitter 等用例同款）。 */
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
    hint = XWidget_sizeHint_base((const XWidget*)bar);
    XQ_CHECK(hint.width > 0 && hint.height > 0);
    XQ_EQ_I64(XMenuBar_heightForWidth(bar, 100),
              XMenuBar_heightForWidth(bar, 1000));
    XQ_CHECK(XWidget_minimumSizeHint_base((const XWidget*)bar).height > 0);
    /* NULL 入口按基类 `*_base` 契约返回无效尺寸 (-1,-1)（原无后缀公开
     * 函数返回 (0,0)，收敛后统一走 XWidget_sizeHint_base 分派）。 */
    XQ_CHECK(XWidget_sizeHint_base(NULL).width == -1);
    XQ_EQ_I64(XMenuBar_actionCount(NULL), 0);
    XQ_CHECK(XMenuBar_actionAt(NULL, NULL) == NULL);
    XQ_CHECK(XMenuBar_actionGeometry(NULL, NULL).width == 0);

    /* P3-23/P3-24：defaultUp 方向状态往返。弹出落位本体（defaultUp 时
       菜单翻到动作矩形上方，qmenubar.cpp:289/:308/:330-332）经
       xmb_bridgeTriggeredSlot 生效——真弹窗需窗口平台，控制台宿主无头
       不弹窗（同 demo 口径），此处锁状态位与 getter。 */
    XMenuBar_setDefaultUp(bar, true);
    XQ_CHECK(XMenuBar_isDefaultUp(bar));
    XMenuBar_setDefaultUp(bar, false);
    XQ_CHECK(!XMenuBar_isDefaultUp(bar));

    /* P0-81：增删动作必须联动几何（sizeHint 随动作增删变化）——此前
       addAction 的 XWidget_updateGeometry 写在 return 之后（不可达），
       其余增删路径则完全没有。 */
    {
        XSize before = XWidget_sizeHint_base((const XWidget*)bar);
        XAction* extra;
        extra = XMenuBar_addAction_2(bar, "扩展");
        XQ_REQUIRE(extra != NULL);
        XQ_CHECK(XWidget_sizeHint_base((const XWidget*)bar).width > before.width);
        XMenuBar_removeAction(bar, extra);
        XQ_EQ_I64(XWidget_sizeHint_base((const XWidget*)bar).width, before.width);
        XClassDelete((XClass*)extra); /* removeAction 后归调用方 */
    }

    /* clear：动作表清空、激活动作复位。[P0-37 对标 QWidget::clear] 只
       摘表、**不 delete**：addAction 返回的动作在 clear 后仍存活、归
       调用方（此前实现直接 XClassDelete 自建动作 ⇒ 调用方指针悬垂）。 */
    XMenuBar_setActiveAction(bar, action);
    XQ_CHECK(XMenuBar_activeAction(bar) == action);
    XMenuBar_clear(bar);
    XQ_EQ_I64(XMenuBar_actionCount(bar), 0);
    XQ_CHECK(XMenuBar_activeAction(bar) == NULL);
    XQ_CHECK(xmb_firstHit(bar, 320, 24) == NULL);
    XAction_setText_2(action, "帮助2"); /* clear 未删除：对象仍可用 */
    XQ_EQ_STR(xmb_text(action), "帮助2");

    XClassDelete((XClass*)action); /* clear 后归调用方，先于菜单栏释放 */
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
    /* [P3-25 接管] 对标 qmenubar.cpp:1728-1730 `w->setParent(this)`：
       角件换父到菜单栏并 show（此前「借用仅存储」的分歧按台账作废）。 */
    XQ_CHECK(XObject_parent((XObject*)corner) == (XObject*)bar);
    XQ_CHECK(!XWidget_isHidden(corner));
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

    /* [P3-25 让位] 角件宽度计入 sizeHint、条目区起点左移同宽
       （对标 QMenuBarPrivate::calculateRects 的角件占位）。 */
    {
        XSize hintBefore = XWidget_sizeHint_base((const XWidget*)bar);
        XRect geoBefore = XMenuBar_actionGeometry(bar, created);
        XSize cornerHint;
        XRect geoAfter;
        XSize_init(&cornerHint, 50, 20);
        XWidget_setSizeHint(corner, &cornerHint);
        XMenuBar_setCornerWidget(bar, corner, XMenuBarCorner_TopLeft);
        XQ_EQ_I64(XWidget_sizeHint_base((const XWidget*)bar).width, hintBefore.width + 50);
        geoAfter = XMenuBar_actionGeometry(bar, created);
        XQ_EQ_I64(geoAfter.x, geoBefore.x + 50);
        /* 右角件同样计入提示宽度（两槽同时持有同一控件时叠加）。 */
        XMenuBar_setCornerWidget(bar, corner, XMenuBarCorner_TopRight);
        XQ_EQ_I64(XWidget_sizeHint_base((const XWidget*)bar).width, hintBefore.width + 100);
        XMenuBar_setCornerWidget(bar, NULL, XMenuBarCorner_TopLeft);
        XMenuBar_setCornerWidget(bar, NULL, XMenuBarCorner_TopRight);
        XQ_EQ_I64(XWidget_sizeHint_base((const XWidget*)bar).width, hintBefore.width);
    }

    /* defaultUp 往返（P3-23/24：方向经弹出桥真实生效，落位本体需窗口
       平台，控制台宿主无头不弹窗，状态位与 getter 在此锁定）。 */
    XMenuBar_setDefaultUp(bar, true);
    XQ_CHECK(XMenuBar_isDefaultUp(bar));
    XMenuBar_setDefaultUp(bar, false);
    XQ_CHECK(!XMenuBar_isDefaultUp(bar));
    /* [P3-27/P3-28 对标 Qt 无平台后端口径] qmenubar.cpp:1779-1783：
       「On other platforms, setting this attribute has no effect, and
       reading this attribute will always return false」——本库无平台菜单
       栏后端（platformMenuBar 恒 NULL），set 无效果、读恒 false；旧断言
       锁「存储位往返」锁的是反 Qt 行为，按台账同步作废。 */
    XMenuBar_setNativeMenuBar(bar, true);
    XQ_CHECK(!XMenuBar_isNativeMenuBar(bar));
    XQ_CHECK(XMenuBar_platformMenuBar(bar) == NULL);
    XMenuBar_setNativeMenuBar(bar, false);
    XQ_CHECK(!XMenuBar_isNativeMenuBar(bar));

    XClassDelete((XClass*)corner);
    XClassDelete((XClass*)external);
    XClassDelete((XClass*)bar);
}

#endif /* XGUI_ON && XWIDGET_ON && XMENUBAR_ON && XMENU_ON && XACTION_ON */
