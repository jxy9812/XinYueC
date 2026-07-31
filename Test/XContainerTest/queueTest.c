#include"XDataStructTest.h"
#if DEMOTEST
#include"XQueue.h"
#include"XPriorityQueue.h"
#include"XSort.h"
#include"XVector.h"
#include"XMenu.h"
#include"XAction.h"
#include"XCoreApplication.h"
#include"XPrintf.h"
#include<time.h>
#include<stdlib.h>

/* ============================================================
 *  XQueue（单向链表实现的普通队列）测试
 *  说明：Qt 里 QQueue<T> : QList<T>，接口只有 enqueue/dequeue/head/swap。
 *        本文件用新增的 Qt 别名宏（*_enqueue_base / *_dequeue_base /
 *        *_head_base / *_count_base / *_length_base / *_empty_base）
 *        覆盖 QQueue 的 4 个主 API，同时保留 push/pop/top/receive 老名字。
 * ============================================================ */

#if XQueue_ON
/* -------- 1. 基础 FIFO / Qt 别名 -------- */
static void XQueueBasicTest(void)
{
    XPrintf("===== XQueue 基础 FIFO / Qt 别名 =====\n");
    XQueue* q = XQueue_Create(int);

    /* enqueue 5 项（Qt: QQueue::enqueue） */
    for (int i = 0; i < 5; i++) {
        XQueue_Enqueue_Base(q, int, i * 10);
    }
    XPrintf("count=%zu length=%zu empty=%d head=%d (期望:5/5/0/0)\n",
        XQueue_count_base(q), XQueue_length_base(q),
        (int)XQueue_empty_base(q), XQueue_Head_Base(q, int));

    /* dequeue（Qt: QQueue::dequeue，出+返） */
    int v = -1;
    while (!XQueue_empty_base(q)) {
        XQueue_dequeue_base(q, &v);
        XPrintf("dequeue=%d ", v);
    }
    XPrintf("\n剩余 empty=%d count=%zu (期望:1/0)\n",
        (int)XQueue_empty_base(q), XQueue_count_base(q));
    XQueue_delete_base(q);
    XCoreApplication_quit();
}

/* -------- 2. 移动语义 push_move / enqueue_move -------- */
static void XQueueMoveTest(void)
{
    XPrintf("===== XQueue 移动语义入队 =====\n");
    XQueue* q = XQueue_Create(int);
    for (int i = 0; i < 4; i++) {
        int tmp = i + 100;
        XQueue_enqueue_move_base(q, &tmp);
    }
    XPrintf("移动入队后 count=%zu head=%d\n",
        XQueue_count_base(q), XQueue_Head_Base(q, int));
    while (!XQueue_empty_base(q)) {
        XPrintf("%d ", XQueue_Head_Base(q, int));
        XQueue_dequeue_void_base(q);
    }
    XPrintf("\n");
    XQueue_delete_base(q);
    XCoreApplication_quit();
}

/* -------- 3. 大量数据 / 稳定性 -------- */
static void XQueueBulkTest(void)
{
    XPrintf("===== XQueue 大量数据(10000) =====\n");
    XQueue* q = XQueue_Create(int);
    for (int i = 0; i < 10000; i++) {
        XQueue_Enqueue_Base(q, int, i);
    }
    XPrintf("入队完成 length=%zu (期望:10000)\n", XQueue_length_base(q));

    int mismatch = 0;
    for (int i = 0; i < 10000; i++) {
        int v = -1;
        XQueue_dequeue_base(q, &v);
        if (v != i) mismatch++;
    }
    XPrintf("FIFO 顺序 mismatch=%d empty=%d (期望:0/1)\n",
        mismatch, (int)XQueue_empty_base(q));
    XQueue_delete_base(q);
    XCoreApplication_quit();
}
#endif /* XQueue_ON */

/* ============================================================
 *  XPriorityQueue（堆）测试
 * ============================================================ */
#if XPriorityQueue_ON
static void XPQInsertHelper(void* value, void* args)
{
    XPriorityQueue_push_base(args, value);
    XPrintf("入队:%d 堆顶:%d\n",
        *(int*)value, *(int*)XPriorityQueue_top_base(args));
}

static void XPriorityQueueBasicTest(void)
{
    XPrintf("===== XPriorityQueue 基础(大顶堆) =====\n");
    XPriorityQueue* q = XPriorityQueue_create(sizeof(int), int_compare, XSORT_DESC);
    XContainerSetCompare(q, int_compare);
    XVector* v = XVector_Create(int);
    for (size_t i = 0; i < 10; i++)
        XVector_push_back_1_base(v, &i);
    XDerangement(XVector_data(v),
        XVector_size_base(v), sizeof(int));

    XPrintf("入队顺序:\n");
    XVector_iterator_for_each(v, XPQInsertHelper, q);

    int rm = 3;
    XPriorityQueue_remove(q, &rm, 1);

    XPrintf("出队(应从大到小，缺 3):");
    while (!XPriorityQueue_empty_base(q)) {
        int* val = XPriorityQueue_head_base(q);
        XPrintf("%d ", *val);
        XPriorityQueue_dequeue_void_base(q);
    }
    XPrintf("\n");
    XPriorityQueue_delete_base(q);
    XVector_delete_base(v);
    XCoreApplication_quit();
}

/* 小顶堆（升序优先） */
static void XPriorityQueueAscTest(void)
{
    XPrintf("===== XPriorityQueue 小顶堆(升序优先) =====\n");
    XPriorityQueue* q = XPriorityQueue_create(sizeof(int), int_compare, XSORT_ASC);
    XContainerSetCompare(q, int_compare);
    int arr[] = { 7, 2, 9, 4, 1, 8, 3, 6, 5, 0 };
    for (size_t i = 0; i < sizeof(arr)/sizeof(arr[0]); i++) {
        XPriorityQueue_enqueue_base(q, arr + i);
    }
    XPrintf("count=%zu 出队(应升序):", XPriorityQueue_count_base(q));
    while (!XPriorityQueue_empty_base(q)) {
        XPrintf("%d ", XPriorityQueue_Head_Base(q, int));
        XPriorityQueue_dequeue_void_base(q);
    }
    XPrintf("\n");
    XPriorityQueue_delete_base(q);
    XCoreApplication_quit();
}

/* remove(value,n) 语义 */
static void XPriorityQueueRemoveTest(void)
{
    XPrintf("===== XPriorityQueue remove(v,n) =====\n");
    XPriorityQueue* q = XPriorityQueue_create(sizeof(int), int_compare, XSORT_ASC);
    XContainerSetCompare(q, int_compare);
    int arr[] = { 5, 3, 5, 1, 5, 2, 5 };
    for (size_t i = 0; i < sizeof(arr)/sizeof(arr[0]); i++)
        XPriorityQueue_enqueue_base(q, arr + i);
    int v = 5;
    size_t n = XPriorityQueue_remove(q, &v, 2); /* 只删 2 个 5 */
    XPrintf("删除 5×2 实际=%zu 剩余count=%zu\n", n, XPriorityQueue_count_base(q));
    XPrintf("剩余(升序):");
    while (!XPriorityQueue_empty_base(q)) {
        XPrintf("%d ", XPriorityQueue_Head_Base(q, int));
        XPriorityQueue_dequeue_void_base(q);
    }
    XPrintf("\n");
    XPriorityQueue_delete_base(q);
    XCoreApplication_quit();
}
#endif /* XPriorityQueue_ON */

/* ============================================================ */
static void XQueueAllTest(void)
{
    XPrintf("========== XQueue 全部测试开始 ==========\n");
#if XQueue_ON
    XQueueBasicTest();
    XQueueMoveTest();
    XQueueBulkTest();
#endif
    XPrintf("========== XQueue 全部测试结束 ==========\n");
    XCoreApplication_quit();
}

static void XPriorityQueueAllTest(void)
{
    XPrintf("========== XPriorityQueue 全部测试开始 ==========\n");
#if XPriorityQueue_ON
    XPriorityQueueBasicTest();
    XPriorityQueueAscTest();
    XPriorityQueueRemoveTest();
#endif
    XPrintf("========== XPriorityQueue 全部测试结束 ==========\n");
    XCoreApplication_quit();
}

void XMenu_XQueueTest(XMenu* root)
{
    XMenu* menu = XMenu_create("XQueue(普通队列)");
    XMenu_addMenu(root, menu);
#if XQueue_ON
    { XAction* a = XMenu_addAction(menu, "【全部测试】"); XAction_setAction(a, (Action)XQueueAllTest); }
    { XAction* a = XMenu_addAction(menu, "基础 FIFO / Qt 别名"); XAction_setAction(a, (Action)XQueueBasicTest); }
    { XAction* a = XMenu_addAction(menu, "移动语义入队"); XAction_setAction(a, (Action)XQueueMoveTest); }
    { XAction* a = XMenu_addAction(menu, "大量数据(10000)"); XAction_setAction(a, (Action)XQueueBulkTest); }
#endif
}

void XMenu_XPriorityQueueTest(XMenu* root)
{
    XMenu* menu = XMenu_create("XPriorityQueue(优先队列)");
    XMenu_addMenu(root, menu);
#if XPriorityQueue_ON
    { XAction* a = XMenu_addAction(menu, "【全部测试】"); XAction_setAction(a, (Action)XPriorityQueueAllTest); }
    { XAction* a = XMenu_addAction(menu, "大顶堆(演示)"); XAction_setAction(a, (Action)XPriorityQueueBasicTest); }
    { XAction* a = XMenu_addAction(menu, "小顶堆(升序优先)"); XAction_setAction(a, (Action)XPriorityQueueAscTest); }
    { XAction* a = XMenu_addAction(menu, "remove(v,n)"); XAction_setAction(a, (Action)XPriorityQueueRemoveTest); }
#endif
}
#endif
