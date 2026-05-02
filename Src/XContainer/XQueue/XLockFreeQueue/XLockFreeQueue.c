#include "XLockFreeQueue.h"
#if XCircularQueue_ON
#include"XAlgorithm.h"
#include<string.h>
#include<stdlib.h>
// 计算能容纳 [0, max_value] 所需的最少位数
static inline size_t calculate_bits_needed_for_max(uintptr_t max_value) {
    if (max_value == 0) return 1;
    size_t bits = 0;
    while (max_value > 0) {
        bits++;
        max_value >>= 1;
    }
    return bits;
}

// 打包索引和版本号
static inline size_t  pack_index_version(size_t index, size_t version, size_t index_bits, uintptr_t version_mask) {
    size_t index_part = index;
    size_t version_part = (version & version_mask) << index_bits;
    return index_part | version_part;
}

// 从打包值中解包出索引
static inline size_t unpack_index(size_t packed, uintptr_t index_mask) {
    return packed & index_mask;
}

// 从打包值中解包出版本号
static inline size_t unpack_version(size_t packed, size_t index_bits, uintptr_t version_mask) {
    return (packed >> index_bits) & version_mask;
}

static bool VXLockFreeQueue_isEmpty(const XLockFreeQueue* this_queue);
static bool VXLockFreeQueue_isFull(const XLockFreeQueue* this_queue);
static void VXLockFreeQueue_clear(XLockFreeQueue* this_queue);//清空
static size_t VXLockFreeQueue_size(const XLockFreeQueue* this_queue);
//插入到队列的队尾
static bool VXLockFreeQueue_push(XLockFreeQueue* this_queue, void* pvValue, XCDataCreatMethod dataCreatMethod);
//出队
static void VXLockFreeQueue_pop(XLockFreeQueue* this_queue);
// 返回队头元素
static void* VXLockFreeQueue_top(XLockFreeQueue* this_queue);
static bool VXLockFreeQueue_receive(XLockFreeQueue* this_queue, void* pvBuffer);

static void VXClass_copy(XLockFreeQueue* object, const XLockFreeQueue* src);
static void VXClass_move(XLockFreeQueue* object, XLockFreeQueue* src);
static void VXClass_deinit(XLockFreeQueue* this_queue);

XVtable* XLockFreeQueue_class_init()
{
    XVTABLE_CREAT_DEFAULT
        //虚函数表初始化
#if VTABLE_ISSTACK
        XVTABLE_STACK_INIT_DEFAULT(XLOCKFREEQUEUE_VTABLE_SIZE)
#else
        XVTABLE_HEAP_INIT_DEFAULT
#endif
        //继承类
        XVTABLE_INHERIT_DEFAULT(XContainer_class_init());
    void* table[] = { VXLockFreeQueue_push,VXLockFreeQueue_pop,VXLockFreeQueue_top,VXLockFreeQueue_receive,VXLockFreeQueue_isFull };
    //追加虚函数
    XVTABLE_ADD_FUNC_LIST_DEFAULT(table);
    //重载
    //XVTABLE_OVERLOAD_DEFAULT(EXContainer_IsEmpty, VXLockFreeQueue_isEmpty);
    XVTABLE_OVERLOAD_DEFAULT(EXContainer_Clear, VXLockFreeQueue_clear);
    XVTABLE_OVERLOAD_DEFAULT(EXContainer_Size, VXLockFreeQueue_size);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Copy, VXClass_copy);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Move, VXClass_move);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Deinit, VXClass_deinit)
#if SHOWCONTAINERSIZE
    printf("XLockFreeQueue size:%d\n", XVtable_size(XVTABLE_DEFAULT));
#endif // SHOWCONTAINERSIZE
    return XVTABLE_DEFAULT;
}
XLockFreeQueue* XLockFreeQueue_create(size_t typeSize, size_t count)
{
    if (ISNULL(typeSize, "") || ISNULL(count, ""))
        return NULL;
    XLockFreeQueue* this_queue = XMemory_malloc(sizeof(XLockFreeQueue));
    if (!this_queue) return NULL;
    XLockFreeQueue_init(this_queue, typeSize, count);
    Set_Class_MemoryFree(this_queue, XFree);
    return this_queue;
}
void XLockFreeQueue_init(XLockFreeQueue* this_queue, size_t typeSize, size_t count)
{
    if (ISNULL(this_queue, "") || ISNULL(typeSize, "") || ISNULL(count, ""))
        return NULL;
    size_t desired_capacity = count + 1; // 我们需要能容纳 count 个元素，所以缓冲区大小至少为 count+1
    size_t actual_buffer_size = 1;
    while (actual_buffer_size < desired_capacity) {
        actual_buffer_size <<= 1; // 左移一位，乘以2
    }
    // 现在 actual_buffer_size 是 >= (count+1) 的最小2的幂
    XVector_init(this_queue, typeSize);
    XVector_resize_base(this_queue, actual_buffer_size);
    this_queue->m_index_bits = 0;
    size_t temp = actual_buffer_size;
    while (temp > 1) {
        this_queue->m_index_bits++;
        temp >>= 1;
    }
    this_queue->m_index_mask = actual_buffer_size - 1; // 例如，size=8, mask=7 (0b111)
    // --- 关键修改: 初始化打包的 head 和 tail ---
    size_t queue_size = actual_buffer_size; // 实际缓冲区大小
    //this_queue->m_index_bits = calculate_bits_needed_for_max(queue_size - 1);
    //this_queue->m_index_mask = ((size_t)1 << this_queue->m_index_bits) - 1;
    this_queue->m_version_mask = ((size_t)1 << (sizeof(size_t) * 8 - this_queue->m_index_bits)) - 1;

    // 安全检查：确保有足够的版本号位
    if ((sizeof(size_t) * 8 - this_queue->m_index_bits) < 16) {
        // 处理错误，例如设置一个无效状态或断言
        // 这里简单地将掩码设为0，后续操作会失败
        this_queue->m_version_mask = 0;
    }

    size_t initial_packed = pack_index_version(0, 0, this_queue->m_index_bits, this_queue->m_version_mask);
    XAtomic_init(this_queue->m_head, initial_packed);
    XAtomic_init(this_queue->m_tail, initial_packed);
    XContainerSize(this_queue)=0;
    XClassGetVtable(this_queue) = XLockFreeQueue_class_init();
    // --- 调试: 验证 XVector 是否按预期工作 ---
    //printf("Requested count: %zu\n", count);
    //printf("Actual buffer size (slots): %zu\n", actual_buffer_size);
    //printf("XContainerCapacity reports: %zu\n", XContainerCapacity(this_queue)); // 应该等于 actual_buffer_size
    //printf("XContainerSize reports: %zu\n", XContainerSize(this_queue)); // 初始化后应该为 0
    //printf("Data ptr: %p\n", XContainerDataPtr(this_queue));
}
bool VXLockFreeQueue_isEmpty(const XLockFreeQueue* this_queue)
{
    if (this_queue == NULL)
        return true;

    // 使用 Acquire 读取 tail 来建立同步
    size_t tail_packed = XAtomic_load_size_t(&(this_queue->m_tail), XAtomic_MemoryOrder_Acquire);
    size_t tail_index = unpack_index(tail_packed, this_queue->m_index_mask);

    size_t head_packed = XAtomic_load_size_t(&(this_queue->m_head), XAtomic_MemoryOrder_Relaxed);
    size_t head_index = unpack_index(head_packed, this_queue->m_index_mask);

    return (head_index == tail_index);
}

bool VXLockFreeQueue_isFull(const XLockFreeQueue* this_queue)
{
    if (this_queue == NULL)
        return false;
    return XLockFreeQueue_size_base(this_queue) + 1 == XContainerCapacity(this_queue);
    //size_t head = unpack_index(XAtomic_load_size_t(&(this_queue->m_head), XAtomic_MemoryOrder_Relaxed), this_queue->m_index_mask);
    //size_t tail = unpack_index(XAtomic_load_size_t(&(this_queue->m_tail), XAtomic_MemoryOrder_Relaxed), this_queue->m_index_mask);
    //// --- 修正: 统一使用 & 掩码 ---
    //return (((tail + 1) & this_queue->m_index_mask) == head);
}

void VXLockFreeQueue_clear(XLockFreeQueue* this_queue)
{
    if (this_queue == NULL)
        return;

    while (!VXLockFreeQueue_isEmpty(this_queue))
    {
        VXLockFreeQueue_receive(this_queue, NULL);
    }
}

size_t VXLockFreeQueue_size(const XLockFreeQueue* this_queue)
{
    if (this_queue == NULL)
        return 0;
    //return XAtomic_load_size_t(&XContainerSize(this_queue), XAtomic_MemoryOrder_Relaxed);

    size_t head1, tail1, head2, tail2;
    do {
        // 读取顺序很重要，先读 head 再读 tail
        head1 = XAtomic_load_size_t(&(this_queue->m_head), XAtomic_MemoryOrder_Relaxed);
        tail1 = XAtomic_load_size_t(&(this_queue->m_tail), XAtomic_MemoryOrder_Acquire); // Acquire for push's Release

        head2 = XAtomic_load_size_t(&(this_queue->m_head), XAtomic_MemoryOrder_Relaxed);
        tail2 = XAtomic_load_size_t(&(this_queue->m_tail), XAtomic_MemoryOrder_Relaxed);
    } while (head1 != head2 || tail1 != tail2);

    size_t head_index = unpack_index(head1, this_queue->m_index_mask);
    size_t tail_index = unpack_index(tail1, this_queue->m_index_mask);

    if (tail_index >= head_index) {
        return tail_index - head_index;
    }
    else {
        // 考虑到环形缓冲区回绕
        return this_queue->m_index_mask + 1 + tail_index - head_index;
    }

   return  XAtomic_load_size_t(&XContainerSize(this_queue), XAtomic_MemoryOrder_Relaxed);
}

bool VXLockFreeQueue_push(XLockFreeQueue* this_queue, void* pvValue, XCDataCreatMethod dataCreatMethod)
{
    if (!this_queue) return false;

    size_t old_tail_packed;
    size_t new_tail_packed;
    size_t old_tail_index, new_tail_index;

    // 循环尝试直到成功或队列满
    while (1) {
        // 1. 读取当前尾
        old_tail_packed = XAtomic_load_size_t(&(this_queue->m_tail), XAtomic_MemoryOrder_Relaxed);
        old_tail_index = unpack_index(old_tail_packed, this_queue->m_index_mask);
        size_t head_packed = XAtomic_load_size_t(&(this_queue->m_head), XAtomic_MemoryOrder_Relaxed);
        size_t head_index = unpack_index(head_packed, this_queue->m_index_mask);

        // 2. 检查队列是否已满
        new_tail_index = (old_tail_index + 1) & this_queue->m_index_mask;
        if (new_tail_index == head_index)
            return false; // 队列已满

        // 3. 打包新尾 (版本号+1)
        size_t old_version = unpack_version(old_tail_packed, this_queue->m_index_bits, this_queue->m_version_mask);
        new_tail_packed = pack_index_version(new_tail_index, old_version + 1, this_queue->m_index_bits, this_queue->m_version_mask);

        // 4. 使用CAS操作尝试更新队尾
        if (XAtomic_compare_exchange_strong_size_t(
            &(this_queue->m_tail), &old_tail_packed, new_tail_packed,
            XAtomic_MemoryOrder_Release, XAtomic_MemoryOrder_Relaxed)) {
            break; // 成功获得写入权限
        }
        
        // 否则，表示其他线程已更新m_tail，重试
    }

    // 安全写入数据
    char* data_ptr = (char*)XContainerDataPtr(this_queue);
    size_t type_size = XContainerTypeSize(this_queue);
    void* write_slot = data_ptr + (old_tail_index * type_size);

    if (dataCreatMethod) {
        memset(write_slot, 0, type_size);
        dataCreatMethod(write_slot, pvValue);
    }
    else {
        memcpy(write_slot, pvValue, type_size);
    }
    XAtomic_fetch_add_size_t(&XContainerSize(this_queue),1, XAtomic_MemoryOrder_Relaxed);
    return true;
}

void VXLockFreeQueue_pop(XLockFreeQueue* this_queue)
{
    if (VXLockFreeQueue_isEmpty(this_queue))
        return;

    /*void* temp = XMemory_malloc(XContainerTypeSize(this_queue));
    if (temp == NULL) return;*/

    VXLockFreeQueue_receive(this_queue, NULL);
    //XMemory_free(temp);
}

void* VXLockFreeQueue_top(XLockFreeQueue* this_queue)
{
    if (this_queue == NULL)
        return NULL;

    // --- 关键: 使用 Acquire 读取 tail，以同步 push 的 Release ---
    // 我们读取 tail 是为了检查队列是否为空，但更重要的是建立同步
    size_t tail_packed = XAtomic_load_size_t(&(this_queue->m_tail), XAtomic_MemoryOrder_Acquire);
    size_t tail_index = unpack_index(tail_packed, this_queue->m_index_mask);

    size_t head_packed = XAtomic_load_size_t(&(this_queue->m_head), XAtomic_MemoryOrder_Relaxed);
    size_t head_index = unpack_index(head_packed, this_queue->m_index_mask);

    if (head_index == tail_index) {
        return NULL; // 队列为空
    }

    // 此时，由于 Acquire-Release 同步，我们可以安全地读取 head_index 槽位的数据
    return ((char*)XContainerDataPtr(this_queue)) + (head_index * XContainerTypeSize(this_queue));
}

bool VXLockFreeQueue_receive(XLockFreeQueue* this_queue, void* pvBuffer)
{
    if (!this_queue || !pvBuffer ) return false;

    size_t old_head_packed;
    size_t new_head_packed;
    size_t old_head_index, new_head_index;

    while (1) {
        old_head_packed = XAtomic_load_size_t(&(this_queue->m_head), XAtomic_MemoryOrder_Relaxed);
        old_head_index = unpack_index(old_head_packed, this_queue->m_index_mask);
        size_t tail_packed = XAtomic_load_size_t(&(this_queue->m_tail), XAtomic_MemoryOrder_Relaxed);
        size_t tail_index = unpack_index(tail_packed, this_queue->m_index_mask);

        if (old_head_index == tail_index)
            return false;

        new_head_index = (old_head_index + 1) & this_queue->m_index_mask;
        size_t old_version = unpack_version(old_head_packed, this_queue->m_index_bits, this_queue->m_version_mask);
        new_head_packed = pack_index_version(new_head_index, old_version + 1, this_queue->m_index_bits, this_queue->m_version_mask);

        // 安全读取数据
        if(pvBuffer)
        {
            char* data_ptr = (char*)XContainerDataPtr(this_queue);
            size_t type_size = XContainerTypeSize(this_queue);
            void* read_slot = data_ptr + (old_head_index * type_size);
            memcpy(pvBuffer, read_slot, type_size);
        }

        if (XAtomic_compare_exchange_strong_size_t(
            &(this_queue->m_head), &old_head_packed, new_head_packed,
            XAtomic_MemoryOrder_AcqRel, XAtomic_MemoryOrder_Relaxed)) 
        {
            XAtomic_fetch_sub_size_t(&XContainerSize(this_queue), 1, XAtomic_MemoryOrder_Relaxed);
            break;
        }
    }

   

    // 如果有删除方法，则调用它
   /* if (XContainerDataDeinitMethod(this_queue) != NULL) {
        void* temp = XMemory_malloc(type_size);
        if (temp != NULL) {
            memcpy(temp, read_slot, type_size);
            XContainerDataDeinitMethod(this_queue)(temp);
            XMemory_free(temp);
        }
    }*/

    return true;
}
void VXClass_copy(XLockFreeQueue* object, const XLockFreeQueue* src)
{
    XVtableGetFunc(XVector_class_init(), EXClass_Copy, void(*)(XVector*, const XVector*))(object, src);
    object->m_head = src->m_head;
    object->m_tail = src->m_tail;
}
void VXClass_move(XLockFreeQueue* object, XLockFreeQueue* src)
{
    if (((XClass*)object)->m_vtable == NULL)
    {
        XLockFreeQueue_init(object, XContainerTypeSize(src), XContainerCapacity(src)-1);
    }
    else if (!XLockFreeQueue_isEmpty_base(object))
    {
        XLockFreeQueue_clear_base(object);
    }
    XSwap((XClass*)object + 1, (XClass*)src + 1, sizeof(XLockFreeQueue) - sizeof(XClass));
}
void VXClass_deinit(XLockFreeQueue* this_queue)
{
    XVtableGetFunc(XVector_class_init(), EXClass_Deinit, void(*)(XVector*))(this_queue);
    this_queue->m_head.value = 0;
    this_queue->m_tail.value = 0;
}
#endif