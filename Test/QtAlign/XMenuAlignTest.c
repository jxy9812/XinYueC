/**
 * @file       XMenuAlignTest.c
 * @brief      XMenu × Qt 6.8 QMenu 对齐契约测试。
 * @details    对齐依据：/opt/Qt/6.8.3/gcc_64/include/QtWidgets/qmenu.h
 *             与本机 Qt 6.8.3 源码
 *             /home/xinyue/Qt-src/6.8.3/Src/qtbase/src/widgets/widgets/qmenu.cpp：
 *             - 动作容器：addAction / addMenu / addSeparator / insert 族
 *               （insertAction/insertMenu/insertSeparator）/ removeAction /
 *               clear / actions（qmenu.cpp:1868-1990、2216-2225）；
 *             - isEmpty 判据是「没有可见且非分隔条的动作」
 *               （qmenu.cpp:2199-2211），不是"动作表为空"；
 *             - actionGeometry/actionAt 的条目矩形式排布
 *               （qmenu.cpp:…updateActionRects）；
 *             - clear 只销毁菜单拥有的动作，外部注入动作仅摘除
 *               （qmenu.cpp:2216-2225 的 parent()==this 判定）；
 *             - addMenu(QMenu*) 复用子菜单自己的 menuAction（标题联动）；
 *             - separatorsCollapsible 缺省 true 且布局真正折叠（开头/
 *               相邻/尾部分隔条不占行，actionGeometry 返回空矩形）；
 *             - popup(const QPoint&, QAction*) 的 at 是定位锚（不改变
 *               activeAction，只把该条目对齐到给定点）。
 *             本机 Qt 6.8 探针（QT_QPA_PLATFORM=offscreen）实测：
 *             - 默认 title 为空、isEmpty()==true；
 *             - 两个动作的菜单 sizeHint 高 = 2×条目高（探针 120x50、条目高
 *               25）；actionGeometry(第二个动作).y == 条目高；
 *             - addSection 返回 isSeparator()==true 且 isEnabled()==true 的
 *               动作（带文本的分隔条）。
 * @author     XinYueC 团队
 */
#include "xq_align.h"

#include "XGuiConfig.h"

#if XGUI_ON && XWIDGET_ON && XMENU_ON && XACTION_ON

#include "XMenu.h"
#include "XAction.h"
#include "XWidget.h"
#include "XObject.h"
#include "XString.h"
#include "XGeometry.h"
#include "XClass.h"

/** @brief 菜单动作文本（借用）。
 *  @note  XString_toUtf8 返回内码缓冲的 UTF-8 视图，仅在本表达式内有效。
 */
static const char* xmenu_text(const XAction* action)
{
    const XString* text = XAction_text_const(action);
    return XString_toUtf8((XString*)text);
}

/** @brief 动作表元素个数（借用）。 */
static int64_t xmenu_count(const XMenu* menu)
{
    const XVector* list = XMenu_actions(menu);
    return list ? (int64_t)XVector_size_base((const XContainer*)list) : 0;
}

/* ==================== ① 构造默认值 + 动作容器 + isEmpty ==================== */

XQ_TEST(XMenu, defaultsAndActionContainer)
{
    XMenu* menu = XMenu_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, NULL, "文件");
    XAction* a1;
    XAction* a2;
    XAction* sep;

    XQ_REQUIRE(menu != NULL);

    /* 构造默认值（qmenu.cpp 构造 + 探针）：标题来自 title 构造，
       动作表空、isEmpty 真、分隔条合并默认 true（QMenu 缺省）。 */
    XQ_EQ_STR(XString_toUtf8((XString*)XMenu_title_const(menu)), "文件");
    XQ_EQ_I64(xmenu_count(menu), 0);
    XQ_CHECK(XMenu_isEmpty(menu));
    XQ_CHECK(XMenu_separatorsCollapsible(menu));
    XQ_CHECK(!XMenu_toolTipsVisible(menu));
    XQ_CHECK(!XMenu_tearOffEnabled(menu));
    XQ_CHECK(XMenu_activeAction(menu) == NULL);
    XQ_CHECK(XMenu_defaultAction(menu) == NULL);

    /* addAction_2：追加到末尾并返回归菜单所有的动作。 */
    a1 = XMenu_addAction_2(menu, "打开");
    XQ_REQUIRE(a1 != NULL);
    a2 = XMenu_addAction_2(menu, "另存为");
    XQ_REQUIRE(a2 != NULL);
    XQ_EQ_I64(xmenu_count(menu), 2);
    XQ_EQ_STR(xmenu_text(a1), "打开");
    XQ_CHECK(!XMenu_isEmpty(menu)); /* 有可见非分隔条动作 */

    /* 探针：isEmpty 只看「可见非分隔条」动作——只剩分隔条时为 true，
       动作全部隐藏时也为 true。 */
    XMenu_clear(menu);
    sep = XMenu_addSeparator(menu);
    XQ_REQUIRE(sep != NULL);
    XQ_CHECK(XAction_isSeparator(sep));
    XQ_EQ_I64(xmenu_count(menu), 1);
    XQ_CHECK(XMenu_isEmpty(menu)); /* 只有分隔条 → 空 */

    a1 = XMenu_addAction_2(menu, "打开");
    XQ_REQUIRE(a1 != NULL);
    XQ_CHECK(!XMenu_isEmpty(menu));
    XAction_setVisible(a1, false);
    XQ_CHECK(XMenu_isEmpty(menu)); /* 唯一动作被隐藏 → 空（Qt 同） */
    XAction_setVisible(a1, true);
    XQ_CHECK(!XMenu_isEmpty(menu));

    /* 动作归属：菜单自建动作随 clear 释放；外部注入动作只摘除。
       这里用 XMenu_actions 观察摘除结果，动作对象由本用例释放。 */
    {
        XAction* ext = XAction_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, NULL,
                                        "外部");
        XQ_REQUIRE(ext != NULL);
        XMenu_addActionAction(menu, ext);
        XQ_EQ_I64(xmenu_count(menu), 3);
        XMenu_removeAction(menu, ext);
        XQ_EQ_I64(xmenu_count(menu), 2);
        XClassDelete((XClass*)ext);
    }

    /* addSection：对标 QMenu::addSection（qmenu.cpp:1936-1942）——
     * 带文本的**分隔条**且保持 enabled（本机 Qt 6.8 探针：
     * isSeparator=1/enabled=1/visible=1），gaps 192。 */
    {
        XAction* section = XMenu_addSection_2(menu, "分组");
        XQ_REQUIRE(section != NULL);
        XQ_CHECK(XAction_isSeparator(section));
        XQ_CHECK(XAction_isEnabled(section));   /* 不是「禁用普通动作」 */
        XQ_EQ_STR(xmenu_text(section), "分组");
        /* 节标题仍算分隔条 → 不改变 isEmpty 的「无可见非分隔条动作」判据。 */
        XQ_CHECK(XMenu_isEmpty(menu) == false); /* 还有 a1/a2 */
        XMenu_removeAction(menu, section);
        XClassDelete((XClass*)section);
        /* insertSection 同口径。 */
        section = XMenu_insertSection_2(menu, NULL, "插入节");
        XQ_REQUIRE(section != NULL);
        XQ_CHECK(XAction_isSeparator(section));
        XQ_CHECK(XAction_isEnabled(section));
        XMenu_removeAction(menu, section);
        XClassDelete((XClass*)section);
    }

    /* clear 清空动作表（菜单拥有的动作随之释放）。 */
    XMenu_clear(menu);
    XQ_EQ_I64(xmenu_count(menu), 0);
    XQ_CHECK(XMenu_isEmpty(menu));

    /* NULL 安全（头文件口径）。 */
    XQ_CHECK(XMenu_isEmpty(NULL));
    XQ_CHECK(XMenu_actions(NULL) == NULL);
    XQ_CHECK(XMenu_addAction_2(NULL, "x") == NULL);

    XClassDelete((XClass*)menu);
}

/* ==================== ② 子菜单、几何与激活动作 ==================== */

XQ_TEST(XMenu, submenuGeometryAndActiveAction)
{
    XMenu* root = XMenu_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, NULL, "根");
    XMenu* sub;
    XAction* a1;
    XAction* a2;
    XAction* subAction;
    XRect geo1;
    XRect geo2;
    XPoint inside;
    XPoint outside;

    XQ_REQUIRE(root != NULL);
    XWidget_resize((XWidget*)root, 160, 120);

    a1 = XMenu_addAction_2(root, "一");
    a2 = XMenu_addAction_2(root, "二");
    XQ_REQUIRE(a1 != NULL && a2 != NULL);

    /* 几何：条目按索引竖向等距排布；actionAt 命中/未命中。 */
    geo1 = XMenu_actionGeometry(root, a1);
    geo2 = XMenu_actionGeometry(root, a2);
    XQ_CHECK(geo1.height > 0);
    XQ_EQ_I64(geo2.y - geo1.y, geo1.height);
    XPoint_init(&inside, geo2.x + 1, geo2.y + 1);
    XQ_CHECK(XMenu_actionAt(root, &inside) == a2);
    /* 所有条目之下（越界）→ 未命中。 */
    XPoint_init(&outside, geo2.x + 1, geo2.y + geo2.height + 100);
    XQ_CHECK(XMenu_actionAt(root, &outside) == NULL);
    /* 非成员动作 → 空矩形（Qt 同：actionGeometry 返回空 QRect）。 */
    {
        XAction* stranger = XAction_create_ex(XCLASS_DEFAULT_MEMORY_TYPE,
                                             NULL, "X");
        XQ_REQUIRE(stranger != NULL);
        XQ_CHECK(XMenu_actionGeometry(root, stranger).width == 0);
        XClassDelete((XClass*)stranger);
    }

    /* 子菜单：addMenu_2 建立父子关系并返回子菜单；代表动作可用
       menuInAction 反查（qmenu.cpp:1868-1873 的返回语义）。 */
    sub = XMenu_addMenu_2(root, "更多");
    XQ_REQUIRE(sub != NULL);
    XQ_CHECK(XObject_parent((XObject*)sub) == (XObject*)root);
    XQ_EQ_I64(xmenu_count(root), 3);
    {
        const XVector* list = XMenu_actions(root);
        /* 元素类型 XAction*：XVector_at_base 返回**元素槽地址**，必须按
         * XAction* 解引用（XVector_At_Base 宏封装）。此前把槽地址直接
         * 当动作指针，subAction 恒为野指针。 */
        subAction = XVector_At_Base(list, 2, XAction*);
    }
    XQ_CHECK_PTR(subAction);
    XQ_CHECK(XMenu_menuInAction(subAction) == sub);
    XQ_CHECK(XAction_menu(subAction) == sub);
    /* 对标 QMenu::addMenu(QMenu*)（qmenu.cpp:1866-1873）：加入的是子菜单
     * **自己的** menuAction（不是另建代表动作），gaps 194。 */
    XQ_CHECK(subAction == XMenu_menuAction(sub));
    /* 标题联动：子菜单 setTitle 写 menuAction 文本，父菜单条目随动。 */
    XMenu_setTitle_2(sub, "更多...");
    XQ_EQ_STR(xmenu_text(subAction), "更多...");
    /* addMenu 幂等：重复加入不新增动作。 */
    XQ_CHECK(XMenu_addMenu(root, sub) == subAction);
    XQ_EQ_I64(xmenu_count(root), 3);
    /* addMenu(NULL) 的容忍扩展：创建分隔占位（Qt 会解引用 NULL）。 */
    {
        XAction* placeholder = XMenu_addMenu(root, NULL);
        XQ_CHECK_PTR(placeholder);
        XQ_CHECK(XAction_isSeparator(placeholder));
        XClassDelete((XClass*)sub); /* 先摘掉子菜单，避免父析构再删一次 */
    }

    /* 激活/默认动作往返（qmenu.cpp:2055-2068、2171-2187）。 */
    XMenu_setActiveAction(root, a2);
    XQ_CHECK(XMenu_activeAction(root) == a2);
    XMenu_setActiveAction(root, NULL);
    XQ_CHECK(XMenu_activeAction(root) == NULL);
    XMenu_setDefaultAction(root, a1);
    XQ_CHECK(XMenu_defaultAction(root) == a1);

    /* menuAction：懒创建且缓存（QAction 归属菜单自身）。 */
    {
        XAction* own1 = XMenu_menuAction(root);
        XAction* own2 = XMenu_menuAction(root);
        XQ_CHECK_PTR(own1);
        XQ_CHECK(own1 == own2);
        XQ_EQ_STR(xmenu_text(own1), "根");
        /* setTitle 同步代表动作文本（qmenu.cpp:2230+ setTitle → menuAction）。 */
        XMenu_setTitle_2(root, "根2");
        XQ_EQ_STR(xmenu_text(XMenu_menuAction(root)), "根2");
        XQ_EQ_STR(XString_toUtf8((XString*)XMenu_title_const(root)), "根2");
    }

    XClassDelete((XClass*)root);
}

/* ============ ③ separatorsCollapsible 折叠 + popup(pos, at) 定位锚 ============ */

XQ_TEST(XMenu, separatorsCollapsibleAndPopupAnchor)
{
    XMenu* menu;
    XAction* lead;
    XAction* a;
    XAction* s1;
    XAction* s2;
    XAction* b;
    XAction* trail;
    XRect base;
    XRect ra;
    XRect rb;
    XSize hint;
    XPoint p;
    int ah;

    menu = XMenu_create();
    XQ_REQUIRE(menu != NULL);
    XWidget_resize((XWidget*)menu, 180, 200);

    /* 本机 Qt 6.8 探针（QT_QPA_PLATFORM=offscreen）布局：
     * 开头分隔条 0x0；a 占第 0 行；相邻两个分隔条只保留第一个（第二个
     * 0x0）；b 紧随；尾部分隔条 0x0（qmenu.cpp:392-403 + 487-502）。 */
    lead = XMenu_addSeparator(menu);
    a = XMenu_addAction_2(menu, "A");
    s1 = XMenu_addSeparator(menu);
    s2 = XMenu_addSeparator(menu);
    b = XMenu_addAction_2(menu, "B");
    trail = XMenu_addSeparator(menu);
    XQ_REQUIRE(lead && a && s1 && s2 && b && trail);

    XQ_CHECK(XMenu_separatorsCollapsible(menu)); /* 缺省 true */
    base = XWidget_rect((XWidget*)menu);
    ah = XMenu_actionGeometry(menu, a).height;
    XQ_CHECK(ah > 0);
    XQ_EQ_I64(XMenu_actionGeometry(menu, lead).height, 0);
    XQ_EQ_I64(XMenu_actionGeometry(menu, s2).height, 0);
    XQ_EQ_I64(XMenu_actionGeometry(menu, trail).height, 0);
    ra = XMenu_actionGeometry(menu, a);
    rb = XMenu_actionGeometry(menu, b);
    XQ_EQ_I64(ra.y, base.y);
    XQ_EQ_I64(XMenu_actionGeometry(menu, s1).y, ra.y + ah);
    XQ_EQ_I64(rb.y, ra.y + 2 * ah); /* 折叠后 b 只隔「a+第一个分隔条」两行 */
    hint = XMenu_sizeHint(menu);
    XQ_EQ_I64(hint.height, 3 * ah); /* 3 个可见行 */

    /* 命中按视觉行反查：b 的行返回 b，被折叠的 s2 没有独立命中行。 */
    {
        XPoint hit;
        XPoint_init(&hit, rb.x + 2, rb.y + ah / 2);
        XQ_CHECK(XMenu_actionAt(menu, &hit) == b);
    }

    /* 关闭折叠：所有分隔条各占一行（本机 Qt 6.8 探针 nocol 全为 2px 行，
     * 本库统一条目高模型）。 */
    XMenu_setSeparatorsCollapsible(menu, false);
    XQ_CHECK(!XMenu_separatorsCollapsible(menu));
    XQ_CHECK(XMenu_actionGeometry(menu, lead).height > 0);
    XQ_CHECK(XMenu_actionGeometry(menu, s1).height > 0);
    XQ_CHECK(XMenu_actionGeometry(menu, s2).height > 0);
    XQ_CHECK(XMenu_actionGeometry(menu, trail).height > 0);
    XQ_EQ_I64(XMenu_actionGeometry(menu, b).y - XMenu_actionGeometry(menu, a).y,
              3 * ah); /* a 行 1 → b 行 4 */
    XQ_EQ_I64(XMenu_sizeHint(menu).height, 6 * ah);

    /* popup(pos, at)：at 是**定位锚**——菜单上移 at 之前各布局行高度，
     * 使该条目落在 pos（qmenu.cpp:2426-2450；本机 Qt 6.8 探针：
     * popup((100,100), second) 后菜单 y=75、第二行 y=25）。 */
    XPoint_init(&p, 120, 300);
    XMenu_popup_2(menu, &p, b);
    /* XWidget_rect 是控件局部矩形（恒 0,0,w,h，对标 QWidget::rect）；
     * 屏幕锚点要看父/全局坐标系里的 geometry（对标 QWidget::geometry）。 */
    XQ_EQ_I64(XWidget_geometry((XWidget*)menu).y, 300 - 4 * ah);
    /* popup 不改高亮（Qt 同：activeAction 仍为 null）。 */
    XQ_CHECK(XMenu_activeAction(menu) == NULL);
    XWidget_hide((XWidget*)menu);

    /* at 为 NULL → 原位弹出；不在菜单内的 at 同（不偏移）。 */
    XMenu_popup_2(menu, &p, NULL);
    XQ_EQ_I64(XWidget_geometry((XWidget*)menu).y, 300);
    XWidget_hide((XWidget*)menu);

    XClassDelete((XClass*)menu);
}

#endif /* XGUI_ON && XWIDGET_ON && XMENU_ON && XACTION_ON */
