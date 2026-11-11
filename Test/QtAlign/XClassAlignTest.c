/**
 * @file       XClassAlignTest.c
 * @brief      XClass 与 Qt 6.8 对象模型对齐契约测试。
 * @details    XClass 是 X 侧"虚函数表 + 生命周期"的最小核心，对应 Qt 的
 *             QObjectData/QObjectPrivate 基座 + `delete` 语义。本文件锁定：
 *             - 初始化默认值（虚函数表、内存方法、堆所有权位）；
 *             - `XClassDelete` 的栈/堆双语义（对标 Qt：栈对象不能 `delete`，
 *               X 侧退化为只反初始化）；
 *             - 类名登记（对标 metaObject()->className()）；
 *             - **不可拷贝**：对标 Qt `Q_DISABLE_COPY(QObject)`，XClassCopy /
 *               XClassMove 对 XObject 必须是"目标对象保持原状"的空操作。
 *
 *             对齐依据：/usr/include/x86_64-linux-gnu/qt6/QtCore/qobject.h
 *             （QObject 的 `Q_DISABLE_COPY(QObject)`；本机 Qt 6.8.0）。
 * @note       C99；用例内的对象全部释放由运行器的泄漏门复核。
 * @author     XinYueC 团队
 */
#include "xq_align.h"

#include "XClass.h"
#include "XObject.h"
#include "XString.h"

/* 一个最小派生类：只用于验证「类表登记的父链」（对标 QMetaObject::superClass）。
 * 类名由 XVTABLE_INIT_DEFAULT 的 #Type 写入，父链由 XVTABLE_INHERIT_XCLASS
 * 在 XVtable_append_vtable 内登记。 */
XCLASS_DEFINE_BEGING(XQClassDerived)
XCLASS_DEFINE_EXTEND_END(XQClassDerived, XObject)

typedef struct XQClassDerived
{
    XObject m_base;
} XQClassDerived;

static XVtable* XQClassDerived_class_init(void)
{
    XVTABLE_INIT_DEFAULT(XQClassDerived)
    XVTABLE_INHERIT_XCLASS(XObject);
    return XVTABLE_DEFAULT;
}

static XQClassDerived* XQClassDerived_create(void)
{
    XQClassDerived* d = (XQClassDerived*)XMemory_malloc(sizeof(XQClassDerived),
                                                         XCLASS_DEFAULT_MEMORY_TYPE);
    if (!d) return NULL;
    XObject_init(&d->m_base);
    XClassGetVtable(d) = XQClassDerived_class_init();
    Set_Class_Memory(d, XCLASS_DEFAULT_MEMORY_TYPE);
    Set_Class_IsHeap(d, true);
    return d;
}

/** @brief 虚函数表父链 + 类名链（对标 QMetaObject::superClass / inherits）。 */
XQ_TEST(XClass, vtableParentChain)
{
    XQClassDerived* derived = XQClassDerived_create();
    XObject* plain = XObject_create();
    XQ_REQUIRE(derived != NULL && plain != NULL);

#if XCLASS_VTABLE_NAME_ON
    /* 派生类表登记的直接父类 == 父类的共享表（class_init 单例） */
    XQ_CHECK(XVtable_parent(XQClassDerived_class_init()) == XObject_class_init());
    XQ_CHECK(XVtable_parent(XObject_class_init()) == XClass_class_init());
    XQ_CHECK(XVtable_parent(XClass_class_init()) == NULL);
    /* 类名 */
    XQ_EQ_STR(XVTABLE_GET_NAME(XClassGetVtable(derived)), "XQClassDerived");
    XQ_EQ_STR(XObject_className((XObject*)derived), "XQClassDerived");
    /* inherits 沿父链比对（含自身/父/祖父） */
    XQ_CHECK(XObject_inherits((XObject*)derived, "XQClassDerived"));
    XQ_CHECK(XObject_inherits((XObject*)derived, "XObject"));
    XQ_CHECK(XObject_inherits((XObject*)derived, "XClass"));
    XQ_CHECK(!XObject_inherits((XObject*)derived, "QXClassDerived"));
    XQ_CHECK(!XObject_inherits(plain, "XQClassDerived"));
#endif
    /* isA 按类表指针（qobject_cast 等价） */
    XQ_CHECK(XObject_isA_class((XObject*)derived, XQClassDerived_class_init()));
    XQ_CHECK(XObject_isA_class((XObject*)derived, XObject_class_init()));
    XQ_CHECK(!XObject_isA_class(plain, XQClassDerived_class_init()));

    XClassDelete((XClass*)plain);
    XClassDelete((XClass*)derived);
}

/** @brief 初始化默认值：虚函数表就绪、非堆所有权、内存方法有效。 */
XQ_TEST(XClass, initDefaults)
{
    XClass cls;
    XClass_init(&cls);

    XQ_CHECK_PTR(XClassGetVtable(&cls));
    XQ_CHECK_PTR(Class_Memory(&cls));
    XQ_CHECK(!Class_IsHeap(&cls));     /* *_init 初始化的对象默认不归堆管 */
    /* 栈对象的 XClassDelete 只反初始化、不释放结构体：调用后对象仍可读。 */
    XClassDelete(&cls);
    XQ_CHECK(!Class_IsHeap(&cls));
}

/** @brief 堆对象：*_create 打上堆所有权位，XClassDelete 负责释放。 */
XQ_TEST(XClass, heapOwnership)
{
    XObject* obj = XObject_create();

    XQ_REQUIRE(obj != NULL);
    XQ_CHECK(Class_IsHeap(obj));
    XQ_CHECK_PTR(Class_Memory(obj));
    XQ_CHECK_PTR(XClassGetVtable(obj));
    /* 释放后由泄漏门复核；此处不再触碰 obj。 */
    XClassDelete((XClass*)obj);
}

/** @brief 栈上对象反初始化即可，不崩溃且被标记为已删除（对标析构守卫）。 */
XQ_TEST(XClass, deinitStackObject)
{
    XObject* obj = (XObject*)XMemory_malloc(sizeof(XObject), XCLASS_DEFAULT_MEMORY_TYPE);

    XQ_REQUIRE(obj != NULL);
    XObject_init(obj);
    XQ_CHECK(!obj->m_was_deleted);
    /* 未打堆位：XClassDelete 走"只反初始化"分支 */
    XClassDelete((XClass*)obj);
    XQ_CHECK(obj->m_was_deleted);
    XQ_CHECK(XObject_parent(obj) == NULL);
    XFree_System(obj);   /* 结构体由调用方按自己的分配方式回收 */
}

/** @brief 类名登记：对标 Qt 的 metaObject()->className()。 */
XQ_TEST(XClass, className)
{
    XObject* obj = XObject_create();
    XQ_REQUIRE(obj != NULL);
#if XCLASS_VTABLE_NAME_ON
    {
        const char* name = XVTABLE_GET_NAME(XClassGetVtable(obj));
        XQ_CHECK_PTR(name);
        if (name) XQ_EQ_STR(name, "XObject");
    }
#endif
    XClassDelete((XClass*)obj);
}

/** @brief 不可拷贝：XClassCopy/XClassMove 对 XObject 是空操作（Q_DISABLE_COPY）。 */
XQ_TEST(XClass, nonCopyable)
{
    XObject* src = XObject_create();
    XObject* dst = XObject_create();
    XObject* child = NULL;
    XString* dstName = XString_create_utf8("dst");

    XQ_REQUIRE(src != NULL && dst != NULL && dstName != NULL);
    XObject_setObjectName(dst, dstName);
    child = XObject_createWithParent(dst);
    XQ_REQUIRE(child != NULL);

    /* Qt：QObject 的拷贝构造/赋值被删除；X 侧以"空操作 + 报错"表达。
     * 关键断言：目标对象的名称、对象树一律保持原状（不得被浅拷贝污染）。 */
    XClassCopy(dst, src);
    XClassMove(dst, src);

    {
        const XString* got = XObject_objectName(dst);
        XQ_CHECK_PTR(got);
        if (got) XQ_EQ_STR(XString_toUtf8(got), "dst");
    }
    XQ_CHECK(XObject_parent(child) == dst);
    XQ_CHECK(XObject_children(dst) != NULL);
    if (XObject_children(dst))
        XQ_EQ_SIZE(XContainer_size_base((const XContainer*)XObject_children(dst)), 1);
    /* 源对象没有子对象：拷贝没有把源的对象树搬过来 */
    XQ_CHECK(XObject_children(src) == NULL);

    XClassDelete((XClass*)src);
    XClassDelete((XClass*)dst);   /* child 随 dst 释放 */
    XClassDelete((XClass*)dstName);
}
