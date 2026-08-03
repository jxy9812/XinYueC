#include "XAbstractNativeEventFilter.h"
#include "XMemory.h"
#include <string.h>

// =============== 虚函数默认实现 =================

/**
 * @brief 纯虚函数 nativeEventFilter 的默认实现（不应被直接调用）
 */
static bool VXAbstractNativeEventFilter_nativeEventFilter(
    XAbstractNativeEventFilter* self,
    const XByteArray* eventType,
    void* message,
    int64_t* result)
{
    (void)self;
    (void)eventType;
    (void)message;
    (void)result;
    // 纯虚函数，子类必须重写
    return false;
}

// =============== 虚函数表初始化 =================

XVtable* XAbstractNativeEventFilter_class_init(void)
{
    XVTABLE_CREAT_DEFAULT

#if VTABLE_ISSTACK
        XVTABLE_STACK_INIT_DEFAULT(XAbstractNativeEventFilter)
#else
        XVTABLE_HEAP_INIT_DEFAULT
#endif
        //继承类
        XVTABLE_INHERIT_XCLASS(XClass);
        // 无基类继承（XClass 是最底层）
        void* table[] = {
            (void*)VXAbstractNativeEventFilter_nativeEventFilter
    };
    XVTABLE_ADD_FUNC_LIST_DEFAULT(table);

#if SHOWCONTAINERSIZE
    printf("XAbstractNativeEventFilter size:%d\n", XVtable_size(XVTABLE_DEFAULT));
#endif

    return XVTABLE_DEFAULT;
}

// =============== 构造函数 =======================

void XAbstractNativeEventFilter_init(XAbstractNativeEventFilter* self)
{
    if (ISNULL(self, "")) return;

    // 初始化基类 XClass
    XClass_init((XClass*)self);
    XClassGetVtable(self) = XAbstractNativeEventFilter_class_init();

    // 注意：XAbstractNativeEventFilter 没有额外成员，无需 memset
    // 若未来有成员，可在此初始化
}

// =============== 虚函数多态入口 =================

bool XAbstractNativeEventFilter_nativeEventFilter_base(
    XAbstractNativeEventFilter* self,
    const XByteArray* eventType,
    void* message,
    int64_t* result)
{
    if (ISNULL(self, "") || ISNULL(XClassGetVtable(self), "")) {
        return false;
    }
    return XClassGetVirtualFunc(
        self,
        EXAbstractNativeEventFilter_NativeEventFilter,
        bool(*)(XAbstractNativeEventFilter*, const XByteArray*, void*, int64_t*)
    )(self, eventType, message, result);
}