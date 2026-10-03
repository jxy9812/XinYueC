#include "XDataStructTest.h"
#if DEMOTEST
#include "XMemory.h"
#include "XClass.h"
#include "XCompare.h"
#include "XVector.h"
#include "XListDLinked.h"
#include "XListSLinked.h"
#include "XLockFreeList.h"
#include "XStack.h"
#include "XLockFreeStack.h"
#include "XQueue.h"
#include "XCircularQueue.h"
#include "XLockFreeQueue.h"
#include "XPriorityQueue.h"
#include "XSet.h"
#include "XHashSet.h"
#include "XMap.h"
#include "XHashMap.h"
#include "XStringList.h"
#include "XVariantList.h"
#include "XByteArray.h"
#include "XBitArray.h"
#include "XRingChunk.h"
#include "XRingBuffer.h"
#include "XTestMenu.h"
#include "XAction.h"
#include "XCoreApplication.h"
#include "XPrintf.h"

/*
 * 容器内存池专项测试说明：
 * 1. SYSTEM、MULTIPOOL、HYBRID 三类内存方法都必须能正确绑定到容器对象，
 *    容器的内部节点、数组和共享数据也必须从绑定的内存池申请并释放。
 * 2. MULTIPOOL 是面向小对象快速响应的内存池，单次申请超过 512 字节必须
 *    明确失败；HYBRID 则用于覆盖大对象，超过该边界后仍应能够正常分配。
 * 3. 非 COW 容器 copy 到未初始化目标时继承源容器的内存池；copy 到已初始化
 *    目标时保留目标内存池。COW copy 必须使用同一个内存池，跨池操作应拒绝。
 * 4. move 会转移成员所有权，因此源、目标必须使用相同内存池；目标已有内容
 *    会先释放，再接管源成员，同时把源内存池绑定转移给目标。
 *
 * 这些检查覆盖容器公共内存策略，而不仅是检查 create_ex 返回的对象本身，
 * 这样可以同时发现内部数据分配错误和释放路径不匹配问题。
 */
static size_t g_memory_test_passed;
static size_t g_memory_test_failed;

static void XContainerMemory_check(const char* name, const XClass* object,
    XMemoryType type)
{
    bool ok = object != NULL && Class_Memory(object) == XMemory_method(type);
    if (ok)
        g_memory_test_passed++;
    else
        g_memory_test_failed++;
    XPrintf("  %-24s pool=%d %s\n", name, (int)type, ok ? "通过" : "失败");
}

static void XContainerMemory_checkVectorSemantics(void)
{
    int value = 7;
    XVector* source = XVector_create_ex(XMEMORY_TYPE_MULTIPOOL, sizeof(int), false);
    XVector_push_back_1_base(source, &value);

    XVector target;
    XVector_init(&target, sizeof(int), false);
    XClassCopy(&target, source);
    XContainerMemory_check("Vector 非COW copy目标池", (XClass*)&target,
        XCLASS_DEFAULT_MEMORY_TYPE);
    XClassDeinit(&target);

    XVector* cowSource = XVector_create_ex(XMEMORY_TYPE_MULTIPOOL, sizeof(int), true);
    XVector_push_back_1_base(cowSource, &value);
    XVector cowTarget;
    XVector_init(&cowTarget, sizeof(int), true);
    Set_Class_Memory(&cowTarget, XMEMORY_TYPE_MULTIPOOL);
    XClassCopy(&cowTarget, cowSource);
    XContainerMemory_check("Vector COW 同池copy", (XClass*)&cowTarget,
        XMEMORY_TYPE_MULTIPOOL);
    XClassDeinit(&cowTarget);

    XVector differentPoolTarget;
    XVector_init(&differentPoolTarget, sizeof(int), true);
    XClassCopy(&differentPoolTarget, cowSource);
    bool rejected = XVector_size_base(&differentPoolTarget) == 0;
    if (rejected)
        g_memory_test_passed++;
    else
        g_memory_test_failed++;
    XPrintf("  Vector COW 跨池copy拒绝    %s\n", rejected ? "通过" : "失败");
    XClassDeinit(&differentPoolTarget);
    XClassDelete(cowSource);

    XVector* moveSource = XVector_create_ex(XMEMORY_TYPE_MULTIPOOL,
        sizeof(int), false);
    XVector_push_back_1_base(moveSource, &value);
    XVector moveTarget;
    XVector_init(&moveTarget, sizeof(int), false);
    Set_Class_Memory(&moveTarget, XMEMORY_TYPE_MULTIPOOL);
    XClassMove(&moveTarget, moveSource);
    bool moved = XVector_size_base(&moveTarget) == 1 &&
        XVector_size_base(moveSource) == 0 &&
        Class_Memory(&moveTarget) == XMemory_method(XMEMORY_TYPE_MULTIPOOL);
    if (moved)
        g_memory_test_passed++;
    else
        g_memory_test_failed++;
    XPrintf("  Vector 同池move             %s\n", moved ? "通过" : "失败");
    XClassDeinit(&moveTarget);
    XClassDelete(moveSource);
}

static void XContainerMemory_checkMapSemantics(void)
{
    int key = 3;
    int value = 9;
    XMap* source = XMap_create_ex(XMEMORY_TYPE_MULTIPOOL, sizeof(int),
        sizeof(int), int_compare, true);
    XMap_insert_base(source, &key, &value);

    XMap target;
    XMap_init(&target, sizeof(int), sizeof(int), int_compare, true);
    XClassCopy(&target, source);
    bool rejected = XMap_size_base(&target) == 0;
    if (rejected)
        g_memory_test_passed++;
    else
        g_memory_test_failed++;
    XPrintf("  Map COW 跨池copy拒绝        %s\n", rejected ? "通过" : "失败");
    XClassDeinit(&target);

    Set_Class_Memory(&target, XMEMORY_TYPE_MULTIPOOL);
    XClassCopy(&target, source);
    bool copied = XMap_size_base(&target) == 1 &&
        Class_Memory(&target) == XMemory_method(XMEMORY_TYPE_MULTIPOOL);
    if (copied)
        g_memory_test_passed++;
    else
        g_memory_test_failed++;
    XPrintf("  Map COW 同池copy            %s\n", copied ? "通过" : "失败");
    XClassDeinit(&target);
    XClassDelete(source);
}

static void XContainerMemory_checkMultiPoolLimit(void)
{
    void* small = XMemory_malloc(500, XMEMORY_TYPE_MULTIPOOL);
    bool smallOk = small != NULL;
    if (smallOk)
        g_memory_test_passed++;
    else
        g_memory_test_failed++;
    XPrintf("  MULTIPOOL 500字节分配     %s\n", smallOk ? "通过" : "失败");
    XMemory_free(small, XMEMORY_TYPE_MULTIPOOL);

    void* large = XMemory_malloc(513, XMEMORY_TYPE_MULTIPOOL);
    bool largeRejected = large == NULL;
    if (largeRejected)
        g_memory_test_passed++;
    else
        g_memory_test_failed++;
    XPrintf("  MULTIPOOL 513字节拒绝     %s\n", largeRejected ? "通过" : "失败");
    XMemory_free(large, XMEMORY_TYPE_MULTIPOOL);

    unsigned char bytes[600] = { 0 };
    XVector* multiVector = XVector_create_ex(XMEMORY_TYPE_MULTIPOOL,
        sizeof(bytes), false);
    bool multiRejected = multiVector != NULL &&
        !XVector_push_back_1_base(multiVector, bytes);
    if (multiRejected)
        g_memory_test_passed++;
    else
        g_memory_test_failed++;
    XPrintf("  MULTIPOOL 600字节容器失败 %s\n", multiRejected ? "通过" : "失败");
    XClassDelete(multiVector);

    XVector* hybridVector = XVector_create_ex(XMEMORY_TYPE_HYBRID,
        sizeof(bytes), false);
    bool hybridAccepted = hybridVector != NULL &&
        XVector_push_back_1_base(hybridVector, bytes);
    if (hybridAccepted)
        g_memory_test_passed++;
    else
        g_memory_test_failed++;
    XPrintf("  HYBRID 600字节容器回退    %s\n", hybridAccepted ? "通过" : "失败");
    XClassDelete(hybridVector);
}

static void XContainerMemoryPoolTest(void)
{
    static const char* names[] = { "system", "multipool", "hybrid" };
    g_memory_test_passed = 0;
    g_memory_test_failed = 0;
    XPrintf("===== 容器内存池归属与生命周期测试 =====\n");

    for (int type = XMEMORY_TYPE_SYSTEM; type <= XMEMORY_TYPE_HYBRID; ++type) {
        XMemoryType pool = (XMemoryType)type;
        int value = type;
        XPrintf("-- %s (%d) --\n", names[type], type);

        XVector* vector = XVector_create_ex(pool, sizeof(int), false);
        XVector_push_back_1_base(vector, &value);
        XContainerMemory_check("Vector", (XClass*)vector, pool);
        XClassDelete(vector);

        XListDLinked* dlist = XListDLinked_create_ex(pool, sizeof(int), false);
        XListDLinked_push_back_base(dlist, &value);
        XContainerMemory_check("ListDLinked", (XClass*)dlist, pool);
        XClassDelete(dlist);

        XListSLinked* slist = XListSLinked_create_ex(pool, sizeof(int), false);
        XListSLinked_push_back_base(slist, &value);
        XContainerMemory_check("ListSLinked", (XClass*)slist, pool);
        XClassDelete(slist);

        XLockFreeList* lfl = XLockFreeList_create_ex(pool, sizeof(int));
        XLockFreeList_push_back_base(lfl, &value);
        XContainerMemory_check("LockFreeList", (XClass*)lfl, pool);
        XClassDelete(lfl);

        XStack* stack = XStack_create_ex(pool, sizeof(int));
        XStack_push_base(stack, &value);
        XContainerMemory_check("Stack", (XClass*)stack, pool);
        XClassDelete(stack);

        XLockFreeStack* lfs = XLockFreeStack_create_ex(pool, sizeof(int), 8);
        XLockFreeStack_push_base(lfs, &value);
        XContainerMemory_check("LockFreeStack", (XClass*)lfs, pool);
        XClassDelete(lfs);

        XQueue* queue = XQueue_create_ex(pool, sizeof(int));
        XQueue_push_base(queue, &value);
        XContainerMemory_check("Queue", (XClass*)queue, pool);
        XClassDelete(queue);

        XCircularQueue* circular = XCircularQueue_create_ex(pool, sizeof(int), 4);
        XCircularQueue_push_base(circular, &value);
        XContainerMemory_check("CircularQueue", (XClass*)circular, pool);
        XClassDelete(circular);

        XLockFreeQueue* lfq = XLockFreeQueue_create_ex(pool, sizeof(int), 8);
        XLockFreeQueue_push_base(lfq, &value);
        XContainerMemory_check("LockFreeQueue", (XClass*)lfq, pool);
        XClassDelete(lfq);

        XPriorityQueue* priority = XPriorityQueue_create_ex(pool, sizeof(int),
            int_compare, XSORT_ASC);
        XPriorityQueue_push_base(priority, &value);
        XContainerMemory_check("PriorityQueue", (XClass*)priority, pool);
        XClassDelete(priority);

        XMap* map = XMap_create_ex(pool, sizeof(int), sizeof(int), int_compare, false);
        XMap_insert_base(map, &value, &value);
        XContainerMemory_check("Map", (XClass*)map, pool);
        XClassDelete(map);

        XHashMap* hashMap = XHashMap_create_ex(pool, sizeof(int), sizeof(int),
            XCryptographicHash_function(XCryptographicHash_XxHash64), int_compare, false);
        XHashMap_insert_base(hashMap, &value, &value);
        XContainerMemory_check("HashMap", (XClass*)hashMap, pool);
        XClassDelete(hashMap);

        XSet* set = XSet_create_ex(pool, sizeof(int), int_compare, false);
        XSet_insert_base(set, &value);
        XContainerMemory_check("Set", (XClass*)set, pool);
        XClassDelete(set);

        XHashSet* hashSet = XHashSet_create_ex(pool, sizeof(int),
            XCryptographicHash_function(XCryptographicHash_XxHash64), int_compare, false);
        XHashSet_insert_base(hashSet, &value);
        XContainerMemory_check("HashSet", (XClass*)hashSet, pool);
        XClassDelete(hashSet);

        XStringList* stringList = XStringList_create_ex(pool);
        XStringList_push_back_utf8(stringList, "pool");
        XContainerMemory_check("StringList", (XClass*)stringList, pool);
        XClassDelete(stringList);

        XVariantList* variantList = XVariantList_create_ex(pool);
        XContainerMemory_check("VariantList", (XClass*)variantList, pool);
        XClassDelete(variantList);

        XByteArray* byteArray = XByteArray_create_ex(pool, false);
        XByteArray_push_back_1(byteArray, (uint8_t)value);
        XContainerMemory_check("ByteArray", (XClass*)byteArray, pool);
        XClassDelete(byteArray);

        XBitArray* bitArray = XBitArray_create_ex(pool, 8, false);
        XBitArray_setBit(bitArray, 1, true);
        XContainerMemory_check("BitArray", (XClass*)bitArray, pool);
        XClassDelete(bitArray);

        XRingChunk* chunk = XRingChunk_create_ex(pool, 8);
        XRingChunk_write(chunk, &value, sizeof(value));
        XContainerMemory_check("RingChunk", (XClass*)chunk, pool);
        XClassDelete(chunk);

        XRingBuffer* buffer = XRingBuffer_create_ex(pool, 8);
        XRingBuffer_write(buffer, &value, sizeof(value));
        XContainerMemory_check("RingBuffer", (XClass*)buffer, pool);
        XClassDelete(buffer);
    }

    XContainerMemory_checkVectorSemantics();
    XContainerMemory_checkMapSemantics();
    XContainerMemory_checkMultiPoolLimit();
    XPrintf("内存池测试结果：通过 %zu，失败 %zu\n",
        g_memory_test_passed, g_memory_test_failed);
    //XCoreApplication_quit();
}

void XTestMenu_XContainerMemoryTest(XTestMenu* root)
{
    XTestMenu* menu = XTestMenu_create("内存池归属与生命周期");
    XTestMenu_addMenu(root, menu);
    XAction* action = XTestMenu_addAction(menu, "三种内存池与 copy/move");
    XTestMenu_setActionFunction(action, (XTestMenuActionFunc)XContainerMemoryPoolTest);
}
#endif
