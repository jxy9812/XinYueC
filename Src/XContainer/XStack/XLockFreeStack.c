#include "XLockFreeStack.h"
#include "XAlgorithm.h"
#include <string.h>
#include <stdlib.h>

static bool VXLockFreeStack_isEmpty(const XLockFreeStack* this_stack);
static bool VXLockFreeStack_isFull(const XLockFreeStack* this_stack);
static void VXLockFreeStack_clear(XLockFreeStack* this_stack);
static size_t VXLockFreeStack_size(const XLockFreeStack* this_stack);

// 压栈操作
static bool VXLockFreeStack_push(XLockFreeStack* this_stack, void* pvValue, XCDataCreatMethod dataCreatMethod);
// 弹栈操作
static void VXLockFreeStack_pop(XLockFreeStack* this_stack);
// 返回栈顶元素
static void* VXLockFreeStack_top(XLockFreeStack* this_stack);
// 接收并弹栈操作
static bool VXLockFreeStack_receive(XLockFreeStack* this_stack, void* pvBuffer);

static void VXClass_copy(XLockFreeStack* object, const XLockFreeStack* src);
static void VXClass_move(XLockFreeStack* object, XLockFreeStack* src);
static void VXClass_deinit(XLockFreeStack* this_stack);

XVtable* XLockFreeStack_class_init()
{
    XVTABLE_CREAT_DEFAULT
        // 虚函数表初始化
#if VTABLE_ISSTACK
        XVTABLE_STACK_INIT_DEFAULT_SIZE(XLOCKFREESTACK_VTABLE_SIZE)
#else
        XVTABLE_HEAP_INIT_DEFAULT
#endif
        // 继承类
        XVTABLE_INHERIT_XCLASS(XContainer);

    void* table[] = {
        VXLockFreeStack_push,
        VXLockFreeStack_pop,
        VXLockFreeStack_top,
        VXLockFreeStack_receive,
        VXLockFreeStack_isFull
    };

    // 追加虚函数
    XVTABLE_ADD_FUNC_LIST_DEFAULT(table);

    // 重载
    XVTABLE_OVERLOAD_DEFAULT(EXContainer_IsEmpty, VXLockFreeStack_isEmpty);
    XVTABLE_OVERLOAD_DEFAULT(EXContainer_Clear, VXLockFreeStack_clear);
    XVTABLE_OVERLOAD_DEFAULT(EXContainer_Size, VXLockFreeStack_size);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Copy, VXClass_copy);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Move, VXClass_move);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Deinit, VXClass_deinit);

#if SHOWCONTAINERSIZE
    printf("XLockFreeStack size:%d\n", XVtable_size(XVTABLE_DEFAULT));
#endif // SHOWCONTAINERSIZE

    return XVTABLE_DEFAULT;
}

XLockFreeStack* XLockFreeStack_create(size_t typeSize, size_t capacity)
{
    if (ISNULL(typeSize, "") || ISNULL(capacity, ""))
        return NULL;

    XLockFreeStack* this_stack = XMalloc_System(sizeof(XLockFreeStack));
    if (!this_stack) return NULL;

    XLockFreeStack_init(this_stack, typeSize, capacity);
    Set_Class_MemoryFree(this_stack, XFree_System);
    return this_stack;
}

void XLockFreeStack_init(XLockFreeStack* this_stack, size_t typeSize, size_t capacity)
{
    if (ISNULL(this_stack, "") || ISNULL(typeSize, "") || ISNULL(capacity, ""))
        return;

    // 初始化底层向量
    XVector_init(this_stack, typeSize,false);
    XVector_resize_base(this_stack, capacity);

    // 计算索引位数和掩码
    /* index_bits 必须能表示 top_index 的最大值 = capacity（非 capacity-1）,
     * 否则当 top_index==capacity 时打包被截断为 0，绕过 isFull 检查。 */
    this_stack->m_index_bits = XAtomic_index_bits(capacity);
    this_stack->m_index_mask = XAtomic_index_mask(this_stack->m_index_bits);
    this_stack->m_version_mask = XAtomic_version_mask(this_stack->m_index_bits);

    // 安全检查：确保有足够的版本号位
    if (XAtomic_version_bits(this_stack->m_index_bits) < 16) {
        this_stack->m_version_mask = 0;
    }

    // 初始化栈顶为0（空栈），版本号为0
    size_t initial_packed = XAtomic_pack_index_version(0, 0, this_stack->m_index_bits, this_stack->m_version_mask);
    XAtomic_init(this_stack->m_top, initial_packed);

    XContainerSize(this_stack) = 0;
    XClassGetVtable(this_stack) = XLockFreeStack_class_init();
}

bool VXLockFreeStack_isEmpty(const XLockFreeStack* this_stack)
{
    if (this_stack == NULL)
        return true;

    size_t top_packed = XAtomic_load_size_t(&(this_stack->m_top), XAtomic_MemoryOrder_Relaxed);
    size_t top_index = XAtomic_unpack_index(top_packed, this_stack->m_index_mask);
    return (top_index == 0);
}

bool VXLockFreeStack_isFull(const XLockFreeStack* this_stack)
{
    if (this_stack == NULL)
        return false;

    size_t top_packed = XAtomic_load_size_t(&(this_stack->m_top), XAtomic_MemoryOrder_Relaxed);
    size_t top_index = XAtomic_unpack_index(top_packed, this_stack->m_index_mask);
    return (top_index >= XContainerCapacity(this_stack));
}

void VXLockFreeStack_clear(XLockFreeStack* this_stack)
{
    if (this_stack == NULL)
        return;

    // 原子地重置栈顶到0
    size_t initial_packed = XAtomic_pack_index_version(0, 0, this_stack->m_index_bits, this_stack->m_version_mask);
    XAtomic_store_size_t(&(this_stack->m_top), initial_packed, XAtomic_MemoryOrder_Release);
    XAtomic_store_size_t(&XContainerSize(this_stack), 0, XAtomic_MemoryOrder_Release);
}

size_t VXLockFreeStack_size(const XLockFreeStack* this_stack)
{
    if (this_stack == NULL)
        return 0;

    size_t top_packed = XAtomic_load_size_t(&(this_stack->m_top), XAtomic_MemoryOrder_Relaxed);
    size_t top_index = XAtomic_unpack_index(top_packed, this_stack->m_index_mask);
    return top_index;
}

bool VXLockFreeStack_push(XLockFreeStack* this_stack, void* pvValue, XCDataCreatMethod dataCreatMethod)
{
    if (!this_stack || !pvValue) return false;

    size_t old_top_packed = XAtomic_load_size_t(&(this_stack->m_top), XAtomic_MemoryOrder_Relaxed);
    size_t new_top_packed;
    size_t old_top_index, new_top_index;

    // 循环尝试直到成功或栈满
    while (1) {
        // 1. 读取当前栈顶
        old_top_index = XAtomic_unpack_index(old_top_packed, this_stack->m_index_mask);

        // 2. 检查栈是否已满
        if (old_top_index >= XContainerCapacity(this_stack))
            return false; // 栈已满

        new_top_index = old_top_index + 1;

        // 3. 打包新栈顶 (版本号+1)
        size_t old_version = XAtomic_unpack_version(old_top_packed, this_stack->m_index_bits, this_stack->m_version_mask);
        new_top_packed = XAtomic_pack_index_version(new_top_index, old_version + 1, this_stack->m_index_bits, this_stack->m_version_mask);

        // 4. 使用CAS操作尝试更新栈顶
        if (XAtomic_compare_exchange_strong_size_t(
            &(this_stack->m_top), &old_top_packed, new_top_packed,
            XAtomic_MemoryOrder_Release, XAtomic_MemoryOrder_Relaxed)) {
            break; // 成功获得写入权限
        }

        // 否则，表示其他线程已更新m_top，重试
    }

    // 安全写入数据到栈顶位置（注意：栈顶索引是从1开始的，所以实际位置是old_top_index-1？）
    // 修正：对于栈来说，索引0是第一个元素，所以栈顶索引就是下一个可用位置
    // 当前栈顶索引是old_top_index，所以新元素应该放在old_top_index位置
    char* data_ptr = (char*)XContainerDataPtr(this_stack);
    size_t type_size = XContainerTypeSize(this_stack);
    void* write_slot = data_ptr + (old_top_index * type_size);

    if (dataCreatMethod) {
        memset(write_slot, 0, type_size);
        dataCreatMethod(write_slot, pvValue);
    }
    else {
        memcpy(write_slot, pvValue, type_size);
    }

    XAtomic_fetch_add_size_t(&XContainerSize(this_stack), 1, XAtomic_MemoryOrder_Relaxed);
    return true;
}

void VXLockFreeStack_pop(XLockFreeStack* this_stack)
{
    if (VXLockFreeStack_isEmpty(this_stack))
        return;

    VXLockFreeStack_receive(this_stack, NULL);
}

void* VXLockFreeStack_top(XLockFreeStack* this_stack)
{
    if (this_stack == NULL)
        return NULL;

    size_t top_packed = XAtomic_load_size_t(&(this_stack->m_top), XAtomic_MemoryOrder_Relaxed);
    size_t top_index = XAtomic_unpack_index(top_packed, this_stack->m_index_mask);

    if (top_index == 0) {
        return NULL; // 栈为空
    }

    // 返回栈顶元素地址（栈顶元素在top_index-1位置）
    return ((char*)XContainerDataPtr(this_stack)) + ((top_index - 1) * XContainerTypeSize(this_stack));
}

bool VXLockFreeStack_receive(XLockFreeStack* this_stack, void* pvBuffer)
{
    if (!this_stack) return false;

    size_t old_top_packed = XAtomic_load_size_t(&(this_stack->m_top), XAtomic_MemoryOrder_Relaxed);
    size_t new_top_packed;
    size_t old_top_index, new_top_index;

    while (1) {
        old_top_index = XAtomic_unpack_index(old_top_packed, this_stack->m_index_mask);

        if (old_top_index == 0)
            return false; // 栈为空

        new_top_index = old_top_index - 1;

        size_t old_version = XAtomic_unpack_version(old_top_packed, this_stack->m_index_bits, this_stack->m_version_mask);
        new_top_packed = XAtomic_pack_index_version(new_top_index, old_version + 1, this_stack->m_index_bits, this_stack->m_version_mask);

        // 安全读取数据（从栈顶位置读取）
        if (pvBuffer) {
            char* data_ptr = (char*)XContainerDataPtr(this_stack);
            size_t type_size = XContainerTypeSize(this_stack);
            void* read_slot = data_ptr + ((old_top_index - 1) * type_size);
            memcpy(pvBuffer, read_slot, type_size);
        }

        if (XAtomic_compare_exchange_strong_size_t(
            &(this_stack->m_top), &old_top_packed, new_top_packed,
            XAtomic_MemoryOrder_AcqRel, XAtomic_MemoryOrder_Relaxed)) {
            XAtomic_fetch_sub_size_t(&XContainerSize(this_stack), 1, XAtomic_MemoryOrder_Relaxed);
            break;
        }
    }

    return true;
}

void VXClass_copy(XLockFreeStack* object, const XLockFreeStack* src)
{
    if (!object || !src) return;
    if (XClassIsVtableNull(object))
        XLockFreeStack_init(object, XContainerTypeSize(src), XContainerCapacity(src));
    XVtableGetFunc(XVector_class_init(), EXClass_Copy, void(*)(XVector*, const XVector*))(object, src);
    object->m_top = src->m_top;
    object->m_index_bits = src->m_index_bits;
    object->m_index_mask = src->m_index_mask;
    object->m_version_mask = src->m_version_mask;
}

void VXClass_move(XLockFreeStack* object, XLockFreeStack* src)
{
    if (XClassIsVtableNull(object))
    {
        XLockFreeStack_init(object, XContainerTypeSize(src), XContainerCapacity(src));
    }
    else if (!XLockFreeStack_isEmpty_base(object))
    {
        XLockFreeStack_clear_base(object);
    }
    XSwap((XClass*)object + 1, (XClass*)src + 1, sizeof(XLockFreeStack) - sizeof(XClass));
}

void VXClass_deinit(XLockFreeStack* this_stack)
{
    XVtableGetFunc(XVector_class_init(), EXClass_Deinit, void(*)(XVector*))(this_stack);
    this_stack->m_top.value = 0;
    this_stack->m_index_bits = 0;
    this_stack->m_index_mask = 0;
    this_stack->m_version_mask = 0;
}