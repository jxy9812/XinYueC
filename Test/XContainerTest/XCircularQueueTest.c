#include"XDataStructTest.h"
#if DEMOTEST
#include"XCircularQueue.h"
#include"XMenu.h"
#include"XAction.h"
#include"XCoreApplication.h"
#include"XPrintf.h"

#if XCircularQueue_ON

/* -------- 1. 基础 FIFO / Qt 别名 -------- */
static void XCQBasicTest(void)
{
    XPrintf("===== XCircularQueue 基础 FIFO / Qt 别名 =====\n");
    XCircularQueue* q = XCircularQueue_Create(int, 8);
    XContainerSetCompare(q, int_compare);
    for (int i = 0; i < 5; i++) {
        XCircularQueue_Enqueue_Base(q, int, i + 1);
    }
    XPrintf("count=%zu length=%zu empty=%d isFull=%d head=%d (期望:5/5/0/0/1)\n",
        XCircularQueue_count_base(q), XCircularQueue_length_base(q),
        (int)XCircularQueue_empty_base(q),
        (int)XCircularQueue_isFull_base(q),
        XCircularQueue_Head_Base(q, int));

    int v = 0;
    while (!XCircularQueue_empty_base(q)) {
        XCircularQueue_dequeue_base(q, &v);
        XPrintf("dequeue=%d ", v);
    }
    XPrintf("\ncount=%zu (期望:0)\n", XCircularQueue_count_base(q));
    XCircularQueue_delete_base(q);
    XCoreApplication_quit();
}

/* -------- 2. 环绕(wraparound) -------- */
static void XCQWrapTest(void)
{
    XPrintf("===== XCircularQueue 环绕(head/tail 回卷) =====\n");
    XCircularQueue* q = XCircularQueue_Create(int, 4);
    XContainerSetCompare(q, int_compare);
    /* 关闭自动扩容，制造回卷 */
    XCircularQueue_setAutoExpansion(q, false);

    /* 填满 4 项 */
    for (int i = 0; i < 4; i++) XCircularQueue_Enqueue_Base(q, int, i);
    XPrintf("初始满 count=%zu isFull=%d\n",
        XCircularQueue_count_base(q), (int)XCircularQueue_isFull_base(q));

    /* 出 2 入 2，让 tail 回卷到 0 */
    for (int i = 0; i < 2; i++) XCircularQueue_dequeue_void_base(q);
    for (int i = 100; i < 102; i++) XCircularQueue_Enqueue_Base(q, int, i);
    XPrintf("回卷后 count=%zu head=%d isFull=%d (期望:4/2/1)\n",
        XCircularQueue_count_base(q),
        XCircularQueue_Head_Base(q, int),
        (int)XCircularQueue_isFull_base(q));

    XPrintf("出队顺序:");
    while (!XCircularQueue_empty_base(q)) {
        XPrintf("%d ", XCircularQueue_Head_Base(q, int));
        XCircularQueue_dequeue_void_base(q);
    }
    XPrintf("\n");
    XCircularQueue_delete_base(q);
    XCoreApplication_quit();
}

/* -------- 3. 自动扩容 -------- */
static void XCQAutoExpansionTest(void)
{
    XPrintf("===== XCircularQueue 自动扩容(1.5×) =====\n");
    XCircularQueue* q = XCircularQueue_Create(int, 4);
    XContainerSetCompare(q, int_compare);
    XCircularQueue_setAutoExpansion(q, true);
    for (int i = 0; i < 20; i++) XCircularQueue_Enqueue_Base(q, int, i);
    XPrintf("入队 20 项 count=%zu capacity=%zu\n",
        XCircularQueue_count_base(q),
        XCircularQueue_capacity_base(q));

    int mismatch = 0;
    for (int i = 0; i < 20; i++) {
        int v = -1;
        XCircularQueue_dequeue_base(q, &v);
        if (v != i) mismatch++;
    }
    XPrintf("FIFO 顺序 mismatch=%d empty=%d (期望:0/1)\n",
        mismatch, (int)XCircularQueue_empty_base(q));
    XCircularQueue_delete_base(q);
    XCoreApplication_quit();
}

/* -------- 4. isFull(容量固定) + push 满时行为 -------- */
static void XCQFullTest(void)
{
    XPrintf("===== XCircularQueue isFull / 满时 push 行为 =====\n");
    XCircularQueue* q = XCircularQueue_Create(int, 3);
    XContainerSetCompare(q, int_compare);
    XCircularQueue_setAutoExpansion(q, false);

    for (int i = 0; i < 3; i++) XCircularQueue_Enqueue_Base(q, int, i);
    XPrintf("填满: isFull=%d count=%zu\n",
        (int)XCircularQueue_isFull_base(q),
        XCircularQueue_count_base(q));

    int over = 999;
    bool ok = XCircularQueue_enqueue_base(q, &over);
    XPrintf("满时再入队 返回=%d (期望:0) 计数=%zu\n",
        (int)ok, XCircularQueue_count_base(q));
    XCircularQueue_delete_base(q);
    XCoreApplication_quit();
}

/* -------- 5. remove(value, n) -------- */
static void XCQRemoveTest(void)
{
    XPrintf("===== XCircularQueue remove(v,n) =====\n");
    XCircularQueue* q = XCircularQueue_Create(int, 16);
    XContainerSetCompare(q, int_compare);
    XCircularQueue_setAutoExpansion(q, true);
    int arr[] = { 5, 3, 5, 1, 5, 2, 5, 4 };
    for (size_t i = 0; i < sizeof(arr)/sizeof(arr[0]); i++)
        XCircularQueue_enqueue_base(q, arr + i);
    int v = 5;
    size_t n = XCircularQueue_remove(q, &v, 2);
    XPrintf("删 5×2 实际=%zu 剩余count=%zu\n",
        n, XCircularQueue_count_base(q));
    XPrintf("剩余(FIFO):");
    while (!XCircularQueue_empty_base(q)) {
        XPrintf("%d ", XCircularQueue_Head_Base(q, int));
        XCircularQueue_dequeue_void_base(q);
    }
    XPrintf("\n");
    XCircularQueue_delete_base(q);
    XCoreApplication_quit();
}

/* -------- 6. 大量数据（无扩容 + 满时丢弃） -------- */
static void XCQBulkTest(void)
{
    XPrintf("===== XCircularQueue 压力(1000 次入/出，capacity=64) =====\n");
    XCircularQueue* q = XCircularQueue_Create(int, 64);
    XContainerSetCompare(q, int_compare);
    XCircularQueue_setAutoExpansion(q, false);

    size_t pushed = 0, popped = 0, rejected = 0;
    for (int round = 0; round < 1000; round++) {
        for (int i = 0; i < 10; i++) {
            int val = round * 10 + i;
            if (XCircularQueue_enqueue_base(q, &val)) pushed++;
            else rejected++;
        }
        for (int i = 0; i < 8; i++) {
            int v = 0;
            if (XCircularQueue_dequeue_base(q, &v)) popped++;
            else break;
        }
    }
    XPrintf("pushed=%zu popped=%zu rejected=%zu 剩余count=%zu\n",
        pushed, popped, rejected, XCircularQueue_count_base(q));
    XCircularQueue_delete_base(q);
    XCoreApplication_quit();
}

static void XCQAllTest(void)
{
    XPrintf("========== XCircularQueue 全部测试开始 ==========\n");
    XCQBasicTest();
    XCQWrapTest();
    XCQAutoExpansionTest();
    XCQFullTest();
    XCQRemoveTest();
    XCQBulkTest();
    XPrintf("========== XCircularQueue 全部测试结束 ==========\n");
    XCoreApplication_quit();
}
#endif

void XMenu_XCircularQueueTest(XMenu* root)
{
    XMenu* menu = XMenu_create("XCircularQueue(环形队列)");
    XMenu_addMenu(root, menu);
#if XCircularQueue_ON
    { XAction* a = XMenu_addAction(menu, "【全部测试】"); XAction_setAction(a, (Action)XCQAllTest); }
    { XAction* a = XMenu_addAction(menu, "基础 FIFO / Qt 别名"); XAction_setAction(a, (Action)XCQBasicTest); }
    { XAction* a = XMenu_addAction(menu, "环绕 (wraparound)"); XAction_setAction(a, (Action)XCQWrapTest); }
    { XAction* a = XMenu_addAction(menu, "自动扩容 1.5×"); XAction_setAction(a, (Action)XCQAutoExpansionTest); }
    { XAction* a = XMenu_addAction(menu, "isFull / 满时行为"); XAction_setAction(a, (Action)XCQFullTest); }
    { XAction* a = XMenu_addAction(menu, "remove(v,n)"); XAction_setAction(a, (Action)XCQRemoveTest); }
    { XAction* a = XMenu_addAction(menu, "压力 1000 轮"); XAction_setAction(a, (Action)XCQBulkTest); }
#endif
}
#endif
