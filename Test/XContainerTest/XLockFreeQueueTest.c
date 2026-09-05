#include"XDataStructTest.h"
#if DEMOTEST
#include<stdint.h>
#include<time.h>
#include<stdlib.h>
#include"XLockFreeQueue.h"
#include"XThread.h"
#include"XTestMenu.h"
#include"XAction.h"
#include"XCoreApplication.h"
#include"XPrintf.h"
#include"XEvent.h"

#if XLockFreeQueue_ON

/* =========================================================
 *  XLockFreeQueue（无锁环形队列）测试
 *  单线程覆盖 + 多线程压力（多生产/多消费）
 * ========================================================= */

/* -------- 1. 基础 FIFO / Qt 别名 -------- */
static void XLFQBasicTest(void)
{
    XPrintf("===== XLockFreeQueue 基础 FIFO / Qt 别名 =====\n");
    XLockFreeQueue* q = XLockFreeQueue_Create(int, 16);
    for (int i = 0; i < 8; i++) {
        int val = i * 10;
        while (!XLockFreeQueue_enqueue_base(q, &val)) { /* retry */ }
    }
    XPrintf("count=%zu length=%zu empty=%d isFull=%d\n",
        XLockFreeQueue_count_base(q), XLockFreeQueue_length_base(q),
        (int)XLockFreeQueue_empty_base(q),
        (int)XLockFreeQueue_isFull_base(q));

    int mismatch = 0;
    for (int i = 0; i < 8; i++) {
        int v = -1;
        while (!XLockFreeQueue_dequeue_base(q, &v)) { /* retry */ }
        if (v != i * 10) mismatch++;
    }
    XPrintf("FIFO 顺序 mismatch=%d empty=%d (期望:0/1)\n",
        mismatch, (int)XLockFreeQueue_empty_base(q));
    XLockFreeQueue_delete_base(q);
    //XCoreApplication_quit();
}

/* -------- 2. isFull(容量固定) -------- */
static void XLFQFullTest(void)
{
    XPrintf("===== XLockFreeQueue isFull 边界 =====\n");
    /* index_bits 要求 count 是 2 的幂，取 16 */
    XLockFreeQueue* q = XLockFreeQueue_Create(int, 16);
    int filled = 0;
    for (int i = 0; i < 32; i++) {
        int v = i;
        if (XLockFreeQueue_enqueue_base(q, &v)) filled++;
        else break;
    }
    XPrintf("实际填入=%d count=%zu isFull=%d\n",
        filled, XLockFreeQueue_count_base(q),
        (int)XLockFreeQueue_isFull_base(q));
    XLockFreeQueue_pop_base(q);
    XPrintf("pop后 count=%zu\n", XLockFreeQueue_count_base(q));
    XLockFreeQueue_clear_base(q);
    XPrintf("clear后 count=%zu empty=%d\n",
        XLockFreeQueue_count_base(q),
        (int)XLockFreeQueue_empty_base(q));
    XLockFreeQueue_delete_base(q);
    //XCoreApplication_quit();
}

/* -------- 3. 大量数据(单线程) -------- */
static void XLFQBulkTest(void)
{
    XPrintf("===== XLockFreeQueue 大量数据(单线程 100000 轮) =====\n");
    XLockFreeQueue* q = XLockFreeQueue_Create(int, 1024);
    size_t pushed = 0, popped = 0;
    for (int round = 0; round < 100000; round++) {
        int v = round;
        while (!XLockFreeQueue_enqueue_base(q, &v)) {
            int out = 0;
            if (XLockFreeQueue_dequeue_base(q, &out)) popped++;
        }
        pushed++;
    }
    int out = 0;
    while (XLockFreeQueue_dequeue_base(q, &out)) popped++;
    XPrintf("pushed=%zu popped=%zu 剩余count=%zu (期望:100000/100000/0)\n",
        pushed, popped, XLockFreeQueue_count_base(q));
    XLockFreeQueue_delete_base(q);
    //XCoreApplication_quit();
}

/* =========================================================
 *  多生产/多消费并发测试
 * ========================================================= */
#define XLFQ_CAPACITY    2048
#define XLFQ_PRODUCERS   4
#define XLFQ_CONSUMERS   8
#define XLFQ_PER_PROD    5000     /* 每个生产者产多少项 */

typedef struct XLFQCtx {
    XLockFreeQueue*  q;
    XAtomic_size_t*  produced;
    XAtomic_size_t*  consumed;
    XAtomic_size_t*  producers_done;
    XAtomic_size_t*  threads_finished;
    size_t           threads_total;
} XLFQCtx;

#if XTHREAD_ON
static void XLFQProducer(XThread* thread, XVarList* vl)
{
    XVarList_args_1(vl, XLFQCtx*, ctx);
    for (int i = 0; i < XLFQ_PER_PROD; i++) {
        int v = i;
        while (!XLockFreeQueue_enqueue_base(ctx->q, &v)) { /* retry */ }
        XAtomic_fetch_add_size_t(ctx->produced, 1, XAtomic_MemoryOrder_Relaxed);
    }
    size_t d = XAtomic_fetch_add_size_t(ctx->producers_done, 1,
        XAtomic_MemoryOrder_Relaxed) + 1;
    XPrintf("[P] tid=%p 完成 %d 项 (%zu/%d)\n",
        XThread_currentThreadId(), XLFQ_PER_PROD, d, XLFQ_PRODUCERS);
    size_t fin = XAtomic_fetch_add_size_t(ctx->threads_finished, 1,
        XAtomic_MemoryOrder_AcqRel) + 1;
    XThread_deleteLater(thread);
   /* if (fin == ctx->threads_total) XCoreApplication_quit();*/
}

static void XLFQConsumer(XThread* thread, XVarList* vl)
{
    XVarList_args_1(vl, XLFQCtx*, ctx);
    int local = 0, idle = 0;
    size_t target = (size_t)XLFQ_PRODUCERS * (size_t)XLFQ_PER_PROD;
    for (;;) {
        int v = 0;
        if (XLockFreeQueue_dequeue_base(ctx->q, &v)) {
            local++; idle = 0;
            size_t total = XAtomic_fetch_add_size_t(ctx->consumed, 1,
                XAtomic_MemoryOrder_Relaxed) + 1;
            if (total >= target) break;
        } else {
            size_t pd = XAtomic_load_size_t(ctx->producers_done,
                XAtomic_MemoryOrder_Relaxed);
            if (pd >= XLFQ_PRODUCERS) {
                size_t prod = XAtomic_load_size_t(ctx->produced,
                    XAtomic_MemoryOrder_Relaxed);
                size_t cons = XAtomic_load_size_t(ctx->consumed,
                    XAtomic_MemoryOrder_Relaxed);
                if (cons >= prod) break;
                if (++idle > 200000) {
                    XPrintf("[C] tid=%p 空转 %d 次退出 (cons=%zu/prod=%zu)\n",
                        XThread_currentThreadId(), idle, cons, prod);
                    break;
                }
            }
        }
    }
    XPrintf("[C] tid=%p 消费=%d 累计=%zu 剩余count=%zu\n",
        XThread_currentThreadId(), local,
        XAtomic_load_size_t(ctx->consumed, XAtomic_MemoryOrder_Relaxed),
        XLockFreeQueue_count_base(ctx->q));
    size_t fin = XAtomic_fetch_add_size_t(ctx->threads_finished, 1,
        XAtomic_MemoryOrder_AcqRel) + 1;
    XThread_deleteLater(thread);
    /*if (fin == ctx->threads_total) XCoreApplication_quit();*/
}

static void XLFQConcurrentTest(void)
{
    XPrintf("===== XLockFreeQueue 并发 %dP/%dC (%d 项/生产者) =====\n",
        XLFQ_PRODUCERS, XLFQ_CONSUMERS, XLFQ_PER_PROD);

    /* 预热 XEvent 子类 vtable，避免多线程首次调用 deleteLater 触发 lazy-init 竞争 */
    (void)XEvent_class_init();
    (void)XEventFunc_class_init();
    XLockFreeQueue* q = XLockFreeQueue_Create(int, XLFQ_CAPACITY);
    XAtomic_size_t produced = { 0 };
    XAtomic_size_t consumed = { 0 };
    XAtomic_size_t producers_done = { 0 };
    XAtomic_size_t threads_finished = { 0 };
    XLFQCtx ctx = { q, &produced, &consumed, &producers_done,
        &threads_finished, (size_t)(XLFQ_PRODUCERS + XLFQ_CONSUMERS) };
    XLFQCtx* pctx = &ctx;

    for (int i = 0; i < XLFQ_PRODUCERS; i++) {
        XThread* t = XThread_create_func(XLFQProducer,
            XVarList_Create(XVar(XLFQCtx*, pctx)));
        XThread_start(t);
    }
    for (int i = 0; i < XLFQ_CONSUMERS; i++) {
        XThread* t = XThread_create_func(XLFQConsumer,
            XVarList_Create(XVar(XLFQCtx*, pctx)));
        XThread_start(t);
    }
    XCoreApplication_exec();

    size_t total = (size_t)XLFQ_PRODUCERS * (size_t)XLFQ_PER_PROD;
    XPrintf("并发结束: produced=%zu consumed=%zu remain=%zu (期望:%zu/%zu/0)\n",
        XAtomic_load_size_t(&produced, XAtomic_MemoryOrder_Relaxed),
        XAtomic_load_size_t(&consumed, XAtomic_MemoryOrder_Relaxed),
        XLockFreeQueue_count_base(q),
        total, total);
    XLockFreeQueue_delete_base(q);
    XPrintf("\n");
    //XCoreApplication_quit();
}
#endif // XTHREAD_ON

/* -------- 全部（不含并发） -------- */
static void XLFQAllTest(void)
{
    XPrintf("========== XLockFreeQueue 全部测试开始(不含并发) ==========\n");
    XLFQBasicTest();
    XLFQFullTest();
    XLFQBulkTest();
    XPrintf("========== XLockFreeQueue 全部测试结束(不含并发) ==========\n");
    //XCoreApplication_quit();
}
#endif

void XTestMenu_XLockFreeQueueTest(XTestMenu* root)
{
    XTestMenu* menu = XTestMenu_create("XLockFreeQueue(无锁环形队列)");
    XTestMenu_addMenu(root, menu);
#if XLockFreeQueue_ON
    { XAction* a = XTestMenu_addAction(menu, "【全部测试(不含并发)】"); XTestMenu_setActionFunction(a, (XTestMenuActionFunc)XLFQAllTest); }
    { XAction* a = XTestMenu_addAction(menu, "基础 FIFO / Qt 别名"); XTestMenu_setActionFunction(a, (XTestMenuActionFunc)XLFQBasicTest); }
    { XAction* a = XTestMenu_addAction(menu, "isFull 边界"); XTestMenu_setActionFunction(a, (XTestMenuActionFunc)XLFQFullTest); }
    { XAction* a = XTestMenu_addAction(menu, "大量数据(单线程 100000)"); XTestMenu_setActionFunction(a, (XTestMenuActionFunc)XLFQBulkTest); }
#if XTHREAD_ON
    { XAction* a = XTestMenu_addAction(menu, "并发多生产/多消费"); XTestMenu_setActionFunction(a, (XTestMenuActionFunc)XLFQConcurrentTest); }
#endif // XTHREAD_ON
#endif
}
#endif
