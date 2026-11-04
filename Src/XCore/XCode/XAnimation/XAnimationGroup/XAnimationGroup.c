#include "XAnimationGroup.h"

#include "XAnimation_p.h"
#include "XMemory.h"

static void VXAnimationGroup_deinit(XAnimationGroup* group)
{
    if (!group)
        return;
    XAnimationGroup_clear(group);
    if (group->m_animations) {
        XClassDelete((XClass*)group->m_animations);
        group->m_animations = NULL;
    }
    XVtableGetFunc(XAbstractAnimation_class_init(), EXClass_Deinit,
                   void(*)(XAbstractAnimation*))((XAbstractAnimation*)group);
}

XVtable* XAnimationGroup_class_init(void)
{
    XVTABLE_INIT_DEFAULT(XAnimationGroup)
    XVTABLE_INHERIT_XCLASS(XAbstractAnimation);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Deinit, VXAnimationGroup_deinit);
    return XVTABLE_DEFAULT;
}

XAnimationGroup* XAnimationGroup_create_ex(XMemoryType memory)
{
    XAnimationGroup* group = XMemory_malloc(sizeof(XAnimationGroup), memory);
    if (!group)
        return NULL;
    XAnimationGroup_init(group);
    Set_Class_Memory(group, memory);
    Set_Class_IsHeap(group, true);
    return group;
}

void XAnimationGroup_init(XAnimationGroup* group)
{
    if (!group)
        return;
    XAbstractAnimation_init((XAbstractAnimation*)group, NULL);
    XClassSetVtable(group, XAnimationGroup);
    group->m_animations = XVector_Create(XAbstractAnimation*);
    XAssert(group->m_animations, "Memory allocation failed");
}

bool XAnimationGroup_addAnimation(XAnimationGroup* group, XAbstractAnimation* animation)
{
    if (!group || !animation || !group->m_animations)
        return false;
    if (animation->m_group == group)
        return true;
    /* 对标 Qt insertAnimation：已属于其他组的动画先从旧组移出再插入
     *（qanimationgroup.cpp 的 oldGroup->removeAnimation 分支）。 */
    if (animation->m_group)
        XAnimationGroup_removeAnimation(animation->m_group, animation);

    if (!XVector_push_back_1_base(group->m_animations, &animation))
        return false;
    XAbstractAnimation_setGroup_internal(animation, group);
    XObject_setParent((XObject*)animation, (XObject*)group);
    return true;
}

void XAnimationGroup_removeAnimation(XAnimationGroup* group, XAbstractAnimation* animation)
{
    if (!group || !animation || !group->m_animations || animation->m_group != group)
        return;
    const void* key = animation;
    int64_t index = XVector_indexOf(group->m_animations, &key, 0);
    if (index >= 0)
        XVector_removeAt_base(group->m_animations, index);
    XAbstractAnimation_setGroup_internal(animation, NULL);
    if (XObject_parent((XObject*)animation) == (XObject*)group)
        XObject_setParent((XObject*)animation, NULL);
    /* 对标 animationRemoved：组被取空时停组并把时间归零。 */
    if (XVector_isEmpty_base((const XContainer*)group->m_animations)
        && XAbstractAnimation_state((XAbstractAnimation*)group) != XAbstractAnimation_Stopped) {
        group->m_class.m_totalCurrentTime = 0;
        group->m_class.m_currentLoopTime = 0;
        XAbstractAnimation_stop((XAbstractAnimation*)group);
    }
}

int XAnimationGroup_animationCount(const XAnimationGroup* group)
{
    if (!group || !group->m_animations)
        return 0;
    return (int)XVector_size_base((const XContainer*)group->m_animations);
}

XAbstractAnimation* XAnimationGroup_animationAt(const XAnimationGroup* group, int index)
{
    if (!group || !group->m_animations || index < 0
        || index >= (int)XVector_size_base((const XContainer*)group->m_animations))
        return NULL;
    return XVector_At_Base(group->m_animations, index, XAbstractAnimation*);
}

int XAnimationGroup_indexOfAnimation(const XAnimationGroup* group,
                                     const XAbstractAnimation* animation)
{
    if (!group || !group->m_animations || !animation)
        return -1;
    const void* key = animation;
    return (int)XVector_indexOf(group->m_animations, &key, 0);
}

XAbstractAnimation* XAnimationGroup_takeAnimation(XAnimationGroup* group, int index)
{
    XAbstractAnimation* animation = XAnimationGroup_animationAt(group, index);
    if (!animation)
        return NULL;
    XVector_removeAt_base(group->m_animations, index);
    XAbstractAnimation_setGroup_internal(animation, NULL);
    if (XObject_parent((XObject*)animation) == (XObject*)group)
        XObject_setParent((XObject*)animation, NULL);
    /* 对标 animationRemoved：组被取空时停组并把时间归零。 */
    if (XVector_isEmpty_base((const XContainer*)group->m_animations)
        && XAbstractAnimation_state((XAbstractAnimation*)group) != XAbstractAnimation_Stopped) {
        group->m_class.m_totalCurrentTime = 0;
        group->m_class.m_currentLoopTime = 0;
        XAbstractAnimation_stop((XAbstractAnimation*)group);
    }
    return animation;
}

void XAnimationGroup_clear(XAnimationGroup* group)
{
    if (!group || !group->m_animations)
        return;
    while (!XVector_isEmpty_base((const XContainer*)group->m_animations)) {
        XAbstractAnimation* animation = XVector_At_Base(group->m_animations, 0, XAbstractAnimation*);
        XVector_removeAt_base(group->m_animations, 0);
        if (!animation)
            continue;
        XAbstractAnimation_setGroup_internal(animation, NULL);
        /* 与 Qt 一致：子动画的父对象是组，clear 随父所有权一并销毁。 */
        if (Class_IsHeap(animation))
            XClassDelete((XClass*)animation);
        else
            XClassDeinit((XClass*)animation);
    }
    /* 对标 Qt：clear 后组停并把当前时间归零。 */
    group->m_class.m_totalCurrentTime = 0;
    group->m_class.m_currentLoopTime = 0;
    group->m_class.m_currentLoop = 0;
    if (XAbstractAnimation_state((XAbstractAnimation*)group) != XAbstractAnimation_Stopped)
        XAbstractAnimation_stop((XAbstractAnimation*)group);
}
