/**
 * @file       XClassMetaAlignTest.c
 * @brief      类元信息与类型查询 × Qt 6.8 对齐契约测试。
 * @details    Qt 的类身份来自 moc 生成的 `QMetaObject`（`className()` /
 *             `superClass()` 链），`QObject::inherits`、`qobject_cast<T>`、
 *             `findChild<T>` 都沿这条链比对。X 没有 moc，**类身份的事实源
 *             是虚函数表**：
 *               - 类名   = `XVTABLE_INIT_DEFAULT(Type)` 写入的 `m_name`
 *                          → `XObject_className`（对标 metaObject()->className()）；
 *               - 父类链 = `XVTABLE_INHERIT_XCLASS(父类)` 经
 *                          `XVtable_append_vtable` 登记的 `m_parent`
 *                          → `XVtable_parent`（对标 QMetaObject::superClass()）；
 *               - 类型判定 = 沿父链比对**类表指针**
 *                          → `XObject_isA_class`（对标 `T::staticMetaObject.cast` /
 *                          `qobject_cast<T>`，含派生类）；
 *               - 类名判定 = 沿父链比对类名字符串
 *                          → `XObject_inherits`（对标 QObject::inherits），
 *                          `XObject_isA` 是它的同义别名。
 *
 *             对齐依据（本机 Qt 6.8.0）：
 *             - `/usr/include/x86_64-linux-gnu/qt6/QtCore/qobjectdefs.h:236-239`
 *               `QMetaObject::className()/superClass()/inherits(const QMetaObject*)`；
 *             - `qobjectdefs.h:676-677` `superClass() { return d.superdata; }`；
 *             - `qobject.h:348` `QObject::inherits(const char*)` =
 *               `qt_metacast(classname) != nullptr`；
 *             - `qobject.h:154-199` `findChild<T>/findChildren<T>` 用
 *               `T::staticMetaObject.cast` 过滤（含派生类）。
 *
 *             **Qt 探针实测结论**（offscreen，随断言注明）：
 *             - `QPushButton` 对象 `inherits("QWidget")/("QObject")/("QPushButton")`
 *               均为 true，`QWidget` 对象 `inherits("QPushButton")` 为 false；
 *               类名大小写敏感（`inherits("qwidget")==false`）；
 *             - `findChild<QWidget*>` 能命中 QPushButton（cast 语义，非精确相等）；
 *             - 名字 + 类型是 **AND**：`findChild<QPushButton*>("wid")`（wid 是
 *               QWidget）返回 nullptr；
 *             - 单个 `findChild` 是**浅层优先**（先直接子对象，再递归），
 *               `FindDirectChildrenOnly` 找不到孙子；
 *             - `findChildren` 是**深度优先前序**：根下 c1、c2，c1 下挂 gc 时
 *               顺序为 `c1, gc, c2`。
 *
 * @note       C99；对象全部释放（父析构带走子对象），由运行器泄漏门复核。
 * @author     XinYueC 团队
 */
#include "xq_align.h"

#include "XObject.h"
#include "XVtable.h"
#include "XString.h"
#include "XVector.h"
#include "XContainer.h"
#include "XMemory.h"
#include "XGuiConfig.h"
#include "XRegularExpression.h"

#if XGUI_ON && XWIDGET_ON
#include "XWidget.h"
#include "XFrame.h"
#include "XLabel.h"
#include "XPushButton.h"
#endif

/* 图表轴：验证"按父类槽位容量建表"的类也登记了类名（见各轴 class_init）。 */
#if XGUI_ON && XCHARTS_ON
#include "XAbstractAxis.h"
#include "XValueAxis.h"
#include "XCategoryAxis.h"
#include "XValueCategoryAxis.h"
#include "XColorAxis.h"
#include "XDateTimeAxis.h"
#include "XLogValueAxis.h"
#endif

/* ==================== 测试用派生类（三级链） ====================
 * 与真实类一样：XCLASS_DEFINE_BEGING/END 只定义槽位枚举（枚举体里无法
 * 登记类名——见 XVtable.h 的宏注释），类名/父链由 class_init 的
 * XVTABLE_INIT_DEFAULT + XVTABLE_INHERIT_XCLASS 自动登记。 */

XCLASS_DEFINE_BEGING(XQMProbe)
XCLASS_DEFINE_EXTEND_END(XQMProbe, XObject)

/** @brief 直接继承 XObject 的探针类。 */
typedef struct XQMProbe
{
    XObject m_base;   /**< 基类；必须是第一个成员（C 的多态布局）。 */
    int m_tag;        /**< 任意载荷，证明类表换成派生类后对象仍可用。 */
} XQMProbe;

/** @brief 二级派生：XQMChild → XQMProbe → XObject（验证多级父链）。 */
XCLASS_DEFINE_BEGING(XQMChild)
XCLASS_DEFINE_EXTEND_END(XQMChild, XQMProbe)

typedef struct XQMChild
{
    XQMProbe m_base;  /**< 基类；必须是第一个成员。 */
} XQMChild;

/** @brief 只声明、从不实例化的类：用于"未知类型过滤"返回 NULL。 */
XCLASS_DEFINE_BEGING(XQMUnused)
XCLASS_DEFINE_EXTEND_END(XQMUnused, XObject)

typedef struct XQMUnused
{
    XObject m_base;
} XQMUnused;

static XVtable* XQMProbe_class_init(void)
{
    XVTABLE_INIT_DEFAULT(XQMProbe)
    XVTABLE_INHERIT_XCLASS(XObject);
    return XVTABLE_DEFAULT;
}

static XVtable* XQMChild_class_init(void)
{
    XVTABLE_INIT_DEFAULT(XQMChild)
    XVTABLE_INHERIT_XCLASS(XQMProbe);
    return XVTABLE_DEFAULT;
}

static XVtable* XQMUnused_class_init(void)
{
    XVTABLE_INIT_DEFAULT(XQMUnused)
    XVTABLE_INHERIT_XCLASS(XObject);
    return XVTABLE_DEFAULT;
}

/** @brief 按 XObject 的对象创建套路构造探针（初始化 + 换类表 + 堆位）。 */
static XQMProbe* XQMProbe_create(void)
{
    XQMProbe* p = (XQMProbe*)XMemory_malloc(sizeof(XQMProbe),
                                            XCLASS_DEFAULT_MEMORY_TYPE);
    if (!p) return NULL;
    XObject_init(&p->m_base);
    XClassGetVtable(p) = XQMProbe_class_init();
    Set_Class_Memory(p, XCLASS_DEFAULT_MEMORY_TYPE);
    Set_Class_IsHeap(p, true);
    return p;
}

static XQMChild* XQMChild_create(void)
{
    XQMChild* c = (XQMChild*)XMemory_malloc(sizeof(XQMChild),
                                            XCLASS_DEFAULT_MEMORY_TYPE);
    if (!c) return NULL;
    XObject_init(&c->m_base.m_base);
    XClassGetVtable(c) = XQMChild_class_init();
    Set_Class_Memory(c, XCLASS_DEFAULT_MEMORY_TYPE);
    Set_Class_IsHeap(c, true);
    return c;
}

/* ==================== 类名 ==================== */

/** @brief className：类表登记的名字、派生类覆盖、基类指针取到最派生类名。 */
XQ_TEST(XClassMeta, className)
{
    XObject* plain = XObject_create();
    XQMProbe* probe = XQMProbe_create();
    XQMChild* child = XQMChild_create();

    XQ_REQUIRE(plain != NULL && probe != NULL && child != NULL);

    /* Qt：QObject 的 className() = "QObject"，派生类 = 最派生类名
     * （moc 为每个类生成独立 staticMetaObject）。 */
    XQ_EQ_STR(XObject_className(plain), "XObject");
    XQ_EQ_STR(XObject_className((XObject*)probe), "XQMProbe");
    XQ_EQ_STR(XObject_className((XObject*)child), "XQMChild");

    /* **基类指针判定**：C 里派生对象常以 XObject* 流转（Qt 里是 QObject*），
     * 类身份由对象自己的虚表决定，不因静态类型而变——
     * 等价于 Qt 的 `qobject_cast<QObject*>(child)->metaObject()->className()`
     * 仍返回派生类名。 */
    {
        XObject* asBase = (XObject*)child;
        XQ_EQ_STR(XObject_className(asBase), "XQMChild");
        XQ_CHECK(XObject_inherits(asBase, "XQMChild"));
        XQ_CHECK(XObject_inherits(asBase, "XQMProbe"));
        XQ_CHECK(XObject_inherits(asBase, "XObject"));
        XQ_CHECK(XObject_isA_class(asBase, XQMChild_class_init()));
        XQ_CHECK(XObject_isA_class(asBase, XQMProbe_class_init()));
    }

    /* 类名是**借用**的进程生命周期字符串（类表 m_name 指向字面量）：
     * 两次调用返回同一指针，调用方不得释放。 */
    XQ_CHECK(XObject_className((XObject*)child) ==
             XObject_className((XObject*)child));

    /* NULL 入参：Qt 无对应观测（metaObject() 不接空对象），X 侧按文档化
     * 兜底返回 "XObject"，不崩溃。 */
    XQ_EQ_STR(XObject_className(NULL), "XObject");

    XClassDelete((XClass*)plain);
    XClassDelete((XClass*)probe);
    XClassDelete((XClass*)child);
}

/* ==================== 继承判定（按类名） ==================== */

/** @brief inherits：含自身、沿父链、大小写敏感、未知类名/空入参。 */
XQ_TEST(XClassMeta, inherits)
{
    XObject* plain = XObject_create();
    XQMProbe* probe = XQMProbe_create();
    XQMChild* child = XQMChild_create();

    XQ_REQUIRE(plain != NULL && probe != NULL && child != NULL);

    /* 自身 + 各级父类（Qt 实测：QPushButton 对 QWidget/QObject 均为 true） */
    XQ_CHECK(XObject_inherits((XObject*)child, "XQMChild"));
    XQ_CHECK(XObject_inherits((XObject*)child, "XQMProbe"));
    XQ_CHECK(XObject_inherits((XObject*)child, "XObject"));
    XQ_CHECK(XObject_inherits((XObject*)child, "XClass")); /* 父链根 */
    XQ_CHECK(XObject_inherits((XObject*)probe, "XQMProbe"));
    XQ_CHECK(XObject_inherits((XObject*)probe, "XObject"));
    XQ_CHECK(XObject_inherits(plain, "XObject"));

    /* **反向不成立**：父类对象不是子类（Qt 实测 QWidget 对 QPushButton false） */
    XQ_CHECK(!XObject_inherits((XObject*)probe, "XQMChild"));
    XQ_CHECK(!XObject_inherits(plain, "XQMProbe"));
    XQ_CHECK(!XObject_inherits(plain, "XQMChild"));

    /* **未知类名**：不在父链上的名字一律 false */
    XQ_CHECK(!XObject_inherits((XObject*)child, "XQMUnused"));
    XQ_CHECK(!XObject_inherits((XObject*)child, "XQMNoSuchClass"));
    XQ_CHECK(!XObject_inherits((XObject*)child, "QObject")); /* 别的框架的类名 */

    /* 大小写敏感（Qt 实测 inherits("qwidget") == false） */
    XQ_CHECK(!XObject_inherits(plain, "xobject"));
    XQ_CHECK(!XObject_inherits(plain, "XOBJECT"));

    /* 空/无效入参：不崩溃、返回 false（Qt 实测 inherits("")==false、
     * inherits(nullptr)==false） */
    XQ_CHECK(!XObject_inherits(NULL, "XObject"));
    XQ_CHECK(!XObject_inherits(plain, NULL));
    XQ_CHECK(!XObject_inherits(plain, ""));

    /* `XObject_isA` 是继承判断的**别名**（与 inherits 逐条同结果） */
    XQ_CHECK(XObject_isA((XObject*)child, "XQMChild"));
    XQ_CHECK(XObject_isA((XObject*)child, "XQMProbe"));
    XQ_CHECK(XObject_isA((XObject*)child, "XObject"));
    XQ_CHECK(!XObject_isA((XObject*)probe, "XQMChild"));
    XQ_CHECK(!XObject_isA(plain, "XQMNoSuchClass"));
    XQ_CHECK(!XObject_isA(plain, "xobject"));
    XQ_CHECK(!XObject_isA(NULL, "XObject"));
    XQ_CHECK(!XObject_isA(plain, NULL));
    XQ_CHECK(!XObject_isA(plain, ""));

    XClassDelete((XClass*)plain);
    XClassDelete((XClass*)probe);
    XClassDelete((XClass*)child);
}

/* ==================== 类型判定（按类表指针） ==================== */

/** @brief isA：qobject_cast 语义（含派生类、父类指针可判、向下转型失败）。 */
XQ_TEST(XClassMeta, isACast)
{
    XObject* plain = XObject_create();
    XQMProbe* probe = XQMProbe_create();
    XQMChild* child = XQMChild_create();

    XQ_REQUIRE(plain != NULL && probe != NULL && child != NULL);

    /* 自身类表 */
    XQ_CHECK(XObject_isA_class((XObject*)child, XQMChild_class_init()));
    XQ_CHECK(XObject_isA_class((XObject*)probe, XQMProbe_class_init()));
    /* 父类表（qobject_cast<XQMProbe*> 对 XQMChild 必须成功） */
    XQ_CHECK(XObject_isA_class((XObject*)child, XQMProbe_class_init()));
    XQ_CHECK(XObject_isA_class((XObject*)child, XObject_class_init()));
    XQ_CHECK(XObject_isA_class((XObject*)child, XClass_class_init()));
    XQ_CHECK(XObject_isA_class((XObject*)probe, XObject_class_init()));
    XQ_CHECK(XObject_isA_class(plain, XObject_class_init()));

    /* **向下转型必须失败**（Qt 实测 qobject_cast<QPushButton*>(QWidget*) == null） */
    XQ_CHECK(!XObject_isA_class(plain, XQMProbe_class_init()));
    XQ_CHECK(!XObject_isA_class(plain, XQMChild_class_init()));
    XQ_CHECK(!XObject_isA_class((XObject*)probe, XQMChild_class_init()));
    /* 未知/无关类型：没有任何对象使用该表 */
    XQ_CHECK(!XObject_isA_class(plain, XQMUnused_class_init()));
    XQ_CHECK(!XObject_isA_class((XObject*)child, XQMUnused_class_init()));

    /* 空入参 */
    XQ_CHECK(!XObject_isA_class(NULL, XObject_class_init()));
    XQ_CHECK(!XObject_isA_class(plain, NULL));

    /* 类表是每类一份单例（对象里的 m_vtable 就是 class_init 返回的指针）——
     * qobject_cast / findChild<T> 的指针比对依赖这一点。 */
    XQ_CHECK(XQMProbe_class_init() == XQMProbe_class_init());
    XQ_CHECK(XQMChild_class_init() == XQMChild_class_init());
    XQ_CHECK(XClassGetVtable((XObject*)probe) == XQMProbe_class_init());
    XQ_CHECK(XClassGetVtable((XObject*)child) == XQMChild_class_init());

    XClassDelete((XClass*)plain);
    XClassDelete((XClass*)probe);
    XClassDelete((XClass*)child);
}

/* ==================== 父类链（superClass 等价物） ==================== */

/** @brief XVtable_parent：直接父类、根为 NULL、借用不转移所有权。 */
XQ_TEST(XClassMeta, superClassChain)
{
    XObject* plain = XObject_create();
    XQ_REQUIRE(plain != NULL);

    /* 多级链：XQMChild → XQMProbe → XObject → XClass → NULL */
    XQ_CHECK(XVtable_parent(XQMChild_class_init()) == XQMProbe_class_init());
    XQ_CHECK(XVtable_parent(XQMProbe_class_init()) == XObject_class_init());
    XQ_CHECK(XVtable_parent(XObject_class_init()) == XClass_class_init());
    XQ_CHECK(XVtable_parent(XClass_class_init()) == NULL);
    XQ_CHECK(XVtable_parent(NULL) == NULL);

    /* 父表是**借用**：返回的指针必须就是父类 class_init 的单例，
     * 不得是副本（否则 inherits 链与槽位布局会分叉）。
     * plain 是 XObject 实例 ⇒ 它的类表的直接父类就是 XClass 的类表。 */
    XQ_CHECK(XVtable_parent((XVtable*)XClassGetVtable(plain)) ==
             XClass_class_init());

    /* 真实 GUI 类同一条链：XWidget 直接继承 XObject */
#if XGUI_ON && XWIDGET_ON
    XQ_CHECK(XVtable_parent(XWidget_class_init()) == XObject_class_init());
    XQ_CHECK(XVtable_parent(XLabel_class_init()) != NULL);
    XQ_CHECK(XVtable_parent(XLabel_class_init()) == XFrame_class_init());
#endif

    XClassDelete((XClass*)plain);
}

/* ==================== 按类型查找 ==================== */

/** @brief findChild/findChildren 按类表过滤：名字+类型组合、递归开关、未知类型。 */
XQ_TEST(XClassMeta, findChildByClass)
{
    /* 树：
     *   root
     *    ├─ probeA  (XQMProbe, "a")
     *    │    └─ childC (XQMChild, "c")
     *    └─ plainB  (XObject, "b")                                  */
    XObject* root = XObject_create();
    XQMProbe* probeA = XQMProbe_create();
    XObject* plainB = XObject_create();
    XQMChild* childC = XQMChild_create();
    XString* nameA = XString_create_utf8("a");
    XString* nameB = XString_create_utf8("b");
    XString* nameC = XString_create_utf8("c");
    XString* empty = XString_create_utf8("");
    XObjectList* list = NULL;

    XQ_REQUIRE(root && probeA && plainB && childC);
    XQ_REQUIRE(nameA && nameB && nameC && empty);

    XObject_setObjectName((XObject*)probeA, nameA);
    XObject_setObjectName(plainB, nameB);
    XObject_setObjectName((XObject*)childC, nameC);
    XObject_setParent((XObject*)probeA, root);
    XObject_setParent(plainB, root);
    XObject_setParent((XObject*)childC, (XObject*)probeA);

    /* 只按类型：直接子对象里第一个 XQMProbe 是 probeA（Qt 实测
     * findChild<QWidget*> 命中第一个可 cast 的子对象） */
    XQ_CHECK(XObject_findChild_class(root, NULL, XQMProbe_class_init(),
                                     XFindChildOption_DirectChildrenOnly)
             == (XObject*)probeA);
    /* 浅层优先：递归时仍先命中直接子对象 probeA，而不是先钻到 childC */
    XQ_CHECK(XObject_findChild_class(root, NULL, XQMProbe_class_init(),
                                     XFindChildOption_ChildrenRecursively)
             == (XObject*)probeA);
    /* 从 probeA 往下找 XQMChild */
    XQ_CHECK(XObject_findChild_class((XObject*)probeA, NULL,
                                     XQMChild_class_init(),
                                     XFindChildOption_DirectChildrenOnly)
             == (XObject*)childC);

    /* **名字 + 类型组合（AND）** */
    XQ_CHECK(XObject_findChild_class(root, nameC, XQMChild_class_init(),
                                     XFindChildOption_ChildrenRecursively)
             == (XObject*)childC);
    /* inherits 语义：用父类表也能命中子类实例 */
    XQ_CHECK(XObject_findChild_class(root, nameC, XQMProbe_class_init(),
                                     XFindChildOption_ChildrenRecursively)
             == (XObject*)childC);
    XQ_CHECK(XObject_findChild_class(root, nameC, XObject_class_init(),
                                     XFindChildOption_ChildrenRecursively)
             == (XObject*)childC);
    /* 名字对但类型不对 ⇒ NULL（Qt 实测 findChild<QPushButton*>("wid") == null） */
    XQ_CHECK(XObject_findChild_class(root, nameB, XQMProbe_class_init(),
                                     XFindChildOption_ChildrenRecursively)
             == NULL);
    /* 类型对但名字不对 ⇒ NULL */
    XQ_CHECK(XObject_findChild_class(root, nameA, XQMChild_class_init(),
                                     XFindChildOption_ChildrenRecursively)
             == NULL);
    /* 深层对象不能被 DirectChildrenOnly 找到（Qt 实测） */
    XQ_CHECK(XObject_findChild_class(root, nameC, XQMChild_class_init(),
                                     XFindChildOption_DirectChildrenOnly)
             == NULL);

    /* classVtable == NULL ⇒ 不按类型过滤（findChild<QObject*> 的等价物） */
    XQ_CHECK(XObject_findChild_class(root, NULL, NULL,
                                     XFindChildOption_DirectChildrenOnly)
             == (XObject*)probeA);
    XQ_CHECK(XObject_findChild_class(root, nameB, NULL,
                                     XFindChildOption_ChildrenRecursively)
             == plainB);
    /* 空名字/ NULL 名字同样不过滤（项目有意偏差，见 XObject.h） */
    XQ_CHECK(XObject_findChild_class(root, empty, XQMProbe_class_init(),
                                     XFindChildOption_DirectChildrenOnly)
             == (XObject*)probeA);

    /* 未知/无实例类型 ⇒ NULL */
    XQ_CHECK(XObject_findChild_class(root, NULL, XQMUnused_class_init(),
                                     XFindChildOption_ChildrenRecursively)
             == NULL);
    /* 空父对象 */
    XQ_CHECK(XObject_findChild_class(NULL, NULL, XQMProbe_class_init(),
                                     XFindChildOption_ChildrenRecursively)
             == NULL);

    /* findChildren：按类型全量 + 深度优先前序（Qt 实测顺序口径） */
    list = XObject_findChildren_class(root, NULL, XQMProbe_class_init(),
                                      XFindChildOption_ChildrenRecursively);
    XQ_CHECK_PTR(list);
    if (list) {
        XQ_EQ_SIZE(XContainer_size_base((const XContainer*)list), 2);
        XQ_CHECK(*(XObject**)XVector_at_base(list, 0) == (XObject*)probeA);
        XQ_CHECK(*(XObject**)XVector_at_base(list, 1) == (XObject*)childC);
        XClassDelete((XClass*)list);
    }
    list = XObject_findChildren_class(root, NULL, XQMChild_class_init(),
                                      XFindChildOption_ChildrenRecursively);
    XQ_CHECK_PTR(list);
    if (list) {
        XQ_EQ_SIZE(XContainer_size_base((const XContainer*)list), 1);
        XQ_CHECK(*(XObject**)XVector_at_base(list, 0) == (XObject*)childC);
        XClassDelete((XClass*)list);
    }
    /* 类型 + 名字组合的全量查找 */
    list = XObject_findChildren_class(root, nameA, XObject_class_init(),
                                      XFindChildOption_ChildrenRecursively);
    XQ_CHECK_PTR(list);
    if (list) {
        XQ_EQ_SIZE(XContainer_size_base((const XContainer*)list), 1);
        XQ_CHECK(*(XObject**)XVector_at_base(list, 0) == (XObject*)probeA);
        XClassDelete((XClass*)list);
    }

    XClassDelete((XClass*)root);   /* probeA/plainB/childC 随 root 释放 */
    XClassDelete((XClass*)nameA);
    XClassDelete((XClass*)nameB);
    XClassDelete((XClass*)nameC);
    XClassDelete((XClass*)empty);
}

#if XRegularExpression_ON
/** @brief 「类型 + 名字正则」的 AND 过滤（Qt 6.8 的
 *         `findChildren<T>(const QRegularExpression&, options)` 完整语义）。
 *
 *  Qt 侧模板实参 `T` 与正则名字条件是 AND（qobject.cpp:2152-2166：
 *  `if (mo.cast(obj)) { if (re.match(obj->objectName()).hasMatch()) ... }`）。
 *  本用例用 XQMProbe / XQMChild 两级链验证 `XObject_isA_class`（含派生类）
 *  与正则 objectName 的组合，并核对深度优先前序。 */
XQ_TEST(XClassMeta, findChildrenByClassAndRegex)
{
    /* 树：root ─┬─ probeA (XQMProbe, "a")
     *          │    └─ childC (XQMChild, "c")
     *          └─ plainB (XObject, "b")                        */
    XObject* root = XObject_create();
    XQMProbe* probeA = XQMProbe_create();
    XObject* plainB = XObject_create();
    XQMChild* childC = XQMChild_create();
    XString* nameA = XString_create_utf8("a");
    XString* nameB = XString_create_utf8("b");
    XString* nameC = XString_create_utf8("c");
    XRegularExpression* reAny = NULL;
    XRegularExpression* reOnlyB = NULL;
    XRegularExpression* reOnlyC = NULL;
    XObjectList* list = NULL;

    XQ_REQUIRE(root && probeA && plainB && childC);
    XQ_REQUIRE(nameA && nameB && nameC);

    XObject_setObjectName((XObject*)probeA, nameA);
    XObject_setObjectName(plainB, nameB);
    XObject_setObjectName((XObject*)childC, nameC);
    XObject_setParent((XObject*)probeA, root);
    XObject_setParent(plainB, root);
    XObject_setParent((XObject*)childC, (XObject*)probeA);

    reAny = XRegularExpression_create_utf8("", XRegularExpression_NoPatternOption);
    reOnlyB = XRegularExpression_create_utf8("^b$", XRegularExpression_NoPatternOption);
    reOnlyC = XRegularExpression_create_utf8("^c$", XRegularExpression_NoPatternOption);
    XQ_REQUIRE(reAny && reOnlyB && reOnlyC);

    /* 类型 XQMProbe（inherits 语义，含 XQMChild）+ 空正则（全命中）
     * ⇒ probeA、childC，深度优先前序 */
    list = XObject_findChildren_class_regex(root, XQMProbe_class_init(), reAny,
                                            XFindChildOption_ChildrenRecursively);
    XQ_CHECK_PTR(list);
    if (list) {
        XQ_EQ_SIZE(XContainer_size_base((const XContainer*)list), 2);
        if (XContainer_size_base((const XContainer*)list) == 2) {
            XQ_CHECK(*(XObject**)XVector_at_base(list, 0) == (XObject*)probeA);
            XQ_CHECK(*(XObject**)XVector_at_base(list, 1) == (XObject*)childC);
        }
        XClassDelete((XClass*)list);
    }

    /* 类型对、正则只命中 plainB（非探针）⇒ 空：AND 语义 */
    list = XObject_findChildren_class_regex(root, XQMProbe_class_init(), reOnlyB,
                                            XFindChildOption_ChildrenRecursively);
    XQ_CHECK_PTR(list);
    if (list) {
        XQ_EQ_SIZE(XContainer_size_base((const XContainer*)list), 0);
        XClassDelete((XClass*)list);
    }

    /* 正则对、类型用父类 XObject ⇒ 命中 plainB（类型不过滤） */
    list = XObject_findChildren_class_regex(root, XObject_class_init(), reOnlyB,
                                            XFindChildOption_ChildrenRecursively);
    XQ_CHECK_PTR(list);
    if (list) {
        XQ_EQ_SIZE(XContainer_size_base((const XContainer*)list), 1);
        if (XContainer_size_base((const XContainer*)list) == 1)
            XQ_CHECK(*(XObject**)XVector_at_base(list, 0) == plainB);
        XClassDelete((XClass*)list);
    }

    /* classVtable == NULL ⇒ 不按类型过滤（findChildren<QObject*>(re)） */
    list = XObject_findChildren_class_regex(root, NULL, reOnlyC,
                                            XFindChildOption_ChildrenRecursively);
    XQ_CHECK_PTR(list);
    if (list) {
        XQ_EQ_SIZE(XContainer_size_base((const XContainer*)list), 1);
        if (XContainer_size_base((const XContainer*)list) == 1)
            XQ_CHECK(*(XObject**)XVector_at_base(list, 0) == (XObject*)childC);
        XClassDelete((XClass*)list);
    }

    /* 单个对象形式（X 扩展）：类型 + 正则 AND；DirectChildrenOnly 找不到孙子 */
    XQ_CHECK(XObject_findChild_class_regex(root, XQMChild_class_init(), reAny,
                                           XFindChildOption_DirectChildrenOnly) == NULL);
    XQ_CHECK(XObject_findChild_class_regex(root, XQMChild_class_init(), reAny,
                                           XFindChildOption_ChildrenRecursively)
             == (XObject*)childC);
    XQ_CHECK(XObject_findChild_class_regex(root, XQMProbe_class_init(), reAny,
                                           XFindChildOption_ChildrenRecursively)
             == (XObject*)probeA);   /* 浅层优先 */

    XClassDelete((XClass*)root);   /* probeA/plainB/childC 随 root 释放 */
    XClassDelete((XClass*)nameA);
    XClassDelete((XClass*)nameB);
    XClassDelete((XClass*)nameC);
    XClassDelete((XClass*)reAny);
    XClassDelete((XClass*)reOnlyB);
    XClassDelete((XClass*)reOnlyC);
}
#endif /* XRegularExpression_ON */

/** @brief findChildren 顺序：Qt 实测深度优先前序（c1, gc, c2）。 */
XQ_TEST(XClassMeta, findChildrenQtOrder)
{
    XObject* root = XObject_create();
    XObject* c1 = XObject_create();
    XObject* c2 = XObject_create();
    XObject* gc = XObject_create();
    XString* n1 = XString_create_utf8("c1");
    XString* n2 = XString_create_utf8("c2");
    XString* ng = XString_create_utf8("gc");
    XObjectList* list = NULL;

    XQ_REQUIRE(root && c1 && c2 && gc && n1 && n2 && ng);
    XObject_setObjectName(c1, n1);
    XObject_setObjectName(c2, n2);
    XObject_setObjectName(gc, ng);
    XObject_setParent(c1, root);
    XObject_setParent(c2, root);
    XObject_setParent(gc, c1);

    /* Qt 6.8.0 offscreen 实测：findChildren<QObject*>(QString()) 返回
     * [c1, gc, c2]——父对象先于其子树、同层按下标顺序（深度优先前序）。 */
    list = XObject_findChildren(root, NULL, XFindChildOption_ChildrenRecursively);
    XQ_CHECK_PTR(list);
    if (list) {
        XQ_EQ_SIZE(XContainer_size_base((const XContainer*)list), 3);
        XQ_CHECK(*(XObject**)XVector_at_base(list, 0) == c1);
        XQ_CHECK(*(XObject**)XVector_at_base(list, 1) == gc);
        XQ_CHECK(*(XObject**)XVector_at_base(list, 2) == c2);
        XClassDelete((XClass*)list);
    }
    /* 带类型过滤也必须是同一顺序 */
    list = XObject_findChildren_class(root, NULL, XObject_class_init(),
                                      XFindChildOption_ChildrenRecursively);
    XQ_CHECK_PTR(list);
    if (list) {
        XQ_EQ_SIZE(XContainer_size_base((const XContainer*)list), 3);
        XQ_CHECK(*(XObject**)XVector_at_base(list, 0) == c1);
        XQ_CHECK(*(XObject**)XVector_at_base(list, 1) == gc);
        XQ_CHECK(*(XObject**)XVector_at_base(list, 2) == c2);
        XClassDelete((XClass*)list);
    }
    /* DirectChildrenOnly 只给直接子对象（c1, c2），且顺序保持 */
    list = XObject_findChildren(root, NULL, XFindChildOption_DirectChildrenOnly);
    XQ_CHECK_PTR(list);
    if (list) {
        XQ_EQ_SIZE(XContainer_size_base((const XContainer*)list), 2);
        XQ_CHECK(*(XObject**)XVector_at_base(list, 0) == c1);
        XQ_CHECK(*(XObject**)XVector_at_base(list, 1) == c2);
        XClassDelete((XClass*)list);
    }

    XClassDelete((XClass*)root);
    XClassDelete((XClass*)n1);
    XClassDelete((XClass*)n2);
    XClassDelete((XClass*)ng);
}

/** @brief 未知类名 / 无实例类表：所有查询都返回"不匹配"，不崩溃。 */
XQ_TEST(XClassMeta, unknownClass)
{
    XObject* obj = XObject_create();
    XQMProbe* probe = XQMProbe_create();
    XQ_REQUIRE(obj != NULL && probe != NULL);

    /* 未知类名（任何父链上都没有） */
    XQ_CHECK(!XObject_inherits(obj, "XQMNoSuchClass"));
    XQ_CHECK(!XObject_inherits((XObject*)probe, "XQMNoSuchClass"));
    XQ_CHECK(!XObject_inherits(obj, "QWidget")); /* Qt 的类名不在这条链上 */

    /* 合法但不匹配的类表（XQMUnused 从未实例化，但类表本身有效） */
    XQ_CHECK(XVtable_parent(XQMUnused_class_init()) == XObject_class_init());
    XQ_CHECK(!XObject_isA_class(obj, XQMUnused_class_init()));
    XQ_CHECK(XObject_findChild_class(obj, NULL, XQMUnused_class_init(),
                                     XFindChildOption_ChildrenRecursively)
             == NULL);

    /* 完全未知的类名字符串给 findChild 的类型过滤没有意义（C 侧类型参数
     * 是类表指针，不是字符串）——这里锁定：按类名的"存在性判定"走
     * inherits，按类型的查找走类表。 */
    XQ_CHECK(!XObject_inherits(obj, ""));

    XClassDelete((XClass*)obj);
    XClassDelete((XClass*)probe);
}

/* ==================== 真实 GUI 类链（XWidget 系） ==================== */

#if XGUI_ON && XWIDGET_ON
/** @brief XWidget/XLabel/XPushButton 的类名、继承与按类型查找。 */
XQ_TEST(XClassMeta, widgetClassChain)
{
    XWidget* host = XWidget_create(NULL, 0);
    XLabel* label = NULL;
    XPushButton* button = NULL;
    XString* labelName = XString_create_utf8("lbl");
    XString* buttonName = XString_create_utf8("btn");
    XObjectList* list = NULL;

    XQ_REQUIRE(host != NULL && labelName != NULL && buttonName != NULL);
    label = XLabel_create(host, 0);
    button = XPushButton_create(host, 0);
    XQ_REQUIRE(label != NULL && button != NULL);
    XObject_setObjectName((XObject*)label, labelName);
    XObject_setObjectName((XObject*)button, buttonName);

    /* 类名（Qt 实测：QWidget/QPushButton 的 className 就是类名本身） */
    XQ_EQ_STR(XObject_className((XObject*)host), "XWidget");
    XQ_EQ_STR(XObject_className((XObject*)label), "XLabel");
    XQ_EQ_STR(XObject_className((XObject*)button), "XPushButton");

    /* 继承：Qt 实测 QPushButton 对 QWidget/QObject 均 true */
    XQ_CHECK(XObject_inherits((XObject*)button, "XPushButton"));
    XQ_CHECK(XObject_inherits((XObject*)button, "XAbstractButton"));
    XQ_CHECK(XObject_inherits((XObject*)button, "XWidget"));
    XQ_CHECK(XObject_inherits((XObject*)button, "XObject"));
    XQ_CHECK(!XObject_inherits((XObject*)button, "XLabel"));

    /* 基类指针判定：控件以 XObject 指针或 XWidget 指针流转时，类身份不变 */
    XQ_CHECK(XObject_isA_class((XObject*)label, XWidget_class_init()));
    XQ_CHECK(XObject_isA_class((XObject*)label, XFrame_class_init()));
    XQ_CHECK(XObject_isA((XObject*)label, "XLabel"));
    XQ_CHECK(XObject_isA((XObject*)label, "XFrame"));
    XQ_CHECK(!XObject_isA((XObject*)label, "XPushButton"));

    /* 按类型查找：XWidget 过滤能命中所有控件（含派生类） */
    XQ_CHECK(XObject_findChild_class((XObject*)host, NULL,
                                     XWidget_class_init(),
                                     XFindChildOption_DirectChildrenOnly)
             == (XObject*)label);
    /* 名字 + 类型：label 名字配 XPushButton 类型 ⇒ NULL */
    XQ_CHECK(XObject_findChild_class((XObject*)host, labelName,
                                     XPushButton_class_init(),
                                     XFindChildOption_DirectChildrenOnly)
             == NULL);
    XQ_CHECK(XObject_findChild_class((XObject*)host, buttonName,
                                     XPushButton_class_init(),
                                     XFindChildOption_DirectChildrenOnly)
             == (XObject*)button);
    list = XObject_findChildren_class((XObject*)host, NULL,
                                      XWidget_class_init(),
                                      XFindChildOption_ChildrenRecursively);
    XQ_CHECK_PTR(list);
    if (list) {
        XQ_EQ_SIZE(XContainer_size_base((const XContainer*)list), 2);
        XClassDelete((XClass*)list);
    }

    XClassDelete((XClass*)host);   /* label/button 随父释放 */
    XClassDelete((XClass*)labelName);
    XClassDelete((XClass*)buttonName);
}
#endif /* XGUI_ON && XWIDGET_ON */

/* ==================== 图表轴（按父类容量建表的类） ==================== */

#if XGUI_ON && XCHARTS_ON
/**
 * @brief 图表轴的类名/父链：这 6 个类用 `XVTABLE_INIT_DEFAULT_SIZE(父类槽数)`
 *        建表（不新增虚槽），类型宏拿不到 #Type，类名必须显式登记——
 *        本轮补齐，这里逐类锁定（否则 className 回落 "XObject"，
 *        `inherits("XValueAxis")` 永远 false）。
 */
XQ_TEST(XClassMeta, chartAxisClassName)
{
    XAbstractAxis* axes[6];
    static const char* const names[6] = {
        "XValueAxis", "XCategoryAxis", "XValueCategoryAxis",
        "XColorAxis", "XDateTimeAxis", "XLogValueAxis"
    };
    int i = 0;

    axes[0] = (XAbstractAxis*)XValueAxis_create();
    axes[1] = (XAbstractAxis*)XCategoryAxis_create();
    axes[2] = (XAbstractAxis*)XValueCategoryAxis_create();
    axes[3] = (XAbstractAxis*)XColorAxis_create();
    axes[4] = (XAbstractAxis*)XDateTimeAxis_create();
    axes[5] = (XAbstractAxis*)XLogValueAxis_create();
    XQ_REQUIRE(axes[0] && axes[1] && axes[2] && axes[3] && axes[4] && axes[5]);

    for (i = 0; i < 6; ++i) {
        XQ_CHECK_MSG(XObject_className((XObject*)axes[i]) != NULL &&
                     strcmp(XObject_className((XObject*)axes[i]), names[i]) == 0,
                     "axis %d className=%s expect=%s", i,
                     XObject_className((XObject*)axes[i]), names[i]);
        XQ_CHECK(XObject_inherits((XObject*)axes[i], names[i]));
        /* 全部沿 XAbstractAxis → XObject 链 */
        XQ_CHECK(XObject_inherits((XObject*)axes[i], "XAbstractAxis"));
        XQ_CHECK(XObject_inherits((XObject*)axes[i], "XObject"));
        XQ_CHECK(XObject_isA_class((XObject*)axes[i], XAbstractAxis_class_init()));
        XQ_CHECK(XObject_isA_class((XObject*)axes[i], XObject_class_init()));
        /* 父类表登记：直接父类必须是 XAbstractAxis（XValueCategoryAxis 例外，
         * 它继承 XValueAxis）。 */
        XQ_CHECK(XVtable_parent(XClassGetVtable((XObject*)axes[i])) != NULL);
    }
    /* XValueCategoryAxis 的真实直接父类是 XValueAxis（两级） */
    XQ_CHECK(XVtable_parent(XValueCategoryAxis_class_init()) ==
             XValueAxis_class_init());
    XQ_CHECK(XVtable_parent(XValueAxis_class_init()) ==
             XAbstractAxis_class_init());
    XQ_CHECK(XVtable_parent(XAbstractAxis_class_init()) == XObject_class_init());

    for (i = 0; i < 6; ++i)
        XClassDelete((XClass*)axes[i]);
}
#endif /* XGUI_ON && XCHARTS_ON */
