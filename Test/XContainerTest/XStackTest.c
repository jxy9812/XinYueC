#include"XDataStructTest.h"
#if DEMOTEST
#include<stdint.h>
#include<time.h>
#include<stdlib.h>
#include"XStack.h"
#include"XLockFreeStack.h"
#include"XThread.h"
#include"XMenu.h"
#include"XAction.h"
#include"XCoreApplication.h"
#include"XPrintf.h"
#include"XEvent.h"

/* ============================================================
 *  XStack / XLockFreeStack 测试
 *  说明：Qt 里 QStack<T> : QList<T>，公开 API 只有 push/pop/top/swap；
 *        其中 pop() 是"弹出并返回 T"。本项目 XStack_pop_base 为 void 出栈，
 *        "读+弹" 语义由 XStack_receive_base / XStack_pop_return_base 承担。
 *        通用 count/length/empty(来自 QList) 均通过别名对齐 size/isEmpty。
 * ============================================================ */

#if XStack_ON

/* -------- 1. XStack 基础 LIFO / Qt 别名 -------- */
static void XStackBasicTest(void)
{
    XPrintf("===== XStack 基础 LIFO / Qt 别名 =====\n");
    XStack* s = XStack_Create(int);
    int arr[] = { 10, 20, 30, 40, 50 };
    for (size_t i = 0; i < sizeof(arr)/sizeof(arr[0]); i++) {
        XStack_push_base(s, arr + i);
    }
    XPrintf("count=%zu length=%zu empty=%d top=%d (期望:5/5/0/50)\n",
        XStack_count_base(s), XStack_length_base(s),
        (int)XStack_empty_base(s), XStack_Top_Base(s, int));

    /* Qt pop() 语义：pop_return（读+弹） */
    XPrintf("pop_return 顺序(LIFO):");
    while (!XStack_empty_base(s)) {
        int v = -1;
        XStack_pop_return_base(s, &v);   /* Qt 别名 -> XStack_receive_base */
        XPrintf("%d ", v);
    }
    XPrintf("\n剩余 empty=%d count=%zu (期望:1/0)\n",
        (int)XStack_empty_base(s), XStack_count_base(s));
    XStack_delete_base(s);
    XCoreApplication_quit();
}

/* -------- 2. XStack 移动语义 push_move -------- */
static void XStackMoveTest(void)
{
    XPrintf("===== XStack 移动语义压栈 =====\n");
    XStack* s = XStack_Create(int);
    for (int i = 0; i < 4; i++) {
        int tmp = i + 100;
        XStack_push_move_base(s, &tmp);
    }
    XPrintf("移动压栈后 count=%zu top=%d (期望:4/103)\n",
        XStack_count_base(s), XStack_Top_Base(s, int));
    XPrintf("弹出顺序:");
    while (!XStack_empty_base(s)) {
        XPrintf("%d ", XStack_Top_Base(s, int));
        XStack_pop_base(s);
    }
    XPrintf("\n");
    XStack_delete_base(s);
    XCoreApplication_quit();
}

/* -------- 3. XStack 变长元素(字符串) -------- */
static void XStackStringTest(void)
{
    XPrintf("===== XStack 变长元素(字符串) =====\n");
    XStack* s = XStack_Create(char[64]);
    const char* names[] = { "琦神", "小白", "皮皮", "蛇蛇" };
    for (size_t i = 0; i < sizeof(names)/sizeof(names[0]); i++) {
        XStack_push_base(s, (void*)names[i]);
    }
    XPrintf("count=%zu length=%zu 出栈(LIFO):\n",
        XStack_count_base(s), XStack_length_base(s));
    while (!XStack_empty_base(s)) {
        XPrintf("%s\n", XStack_top_base(s));
        XStack_pop_base(s);
    }
    XStack_delete_base(s);
    XCoreApplication_quit();
}

/* -------- 4. XStack 压力测试(单线程 10000) -------- */
static void XStackBulkTest(void)
{
    XPrintf("===== XStack 压力(单线程 10000) =====\n");
    XStack* s = XStack_Create(int);
    for (int i = 0; i < 10000; i++) {
        XStack_push_base(s, &i);
    }
    XPrintf("压栈完成 length=%zu (期望:10000)\n", XStack_length_base(s));

    int mismatch = 0;
    for (int i = 9999; i >= 0; i--) {
        int v = -1;
        XStack_pop_return_base(s, &v);
        if (v != i) mismatch++;
    }
    XPrintf("LIFO 顺序 mismatch=%d empty=%d (期望:0/1)\n",
        mismatch, (int)XStack_empty_base(s));
    XStack_delete_base(s);
    XCoreApplication_quit();
}

/* -------- 5. XStack clear -------- */
static void XStackClearTest(void)
{
    XPrintf("===== XStack clear =====\n");
    XStack* s = XStack_Create(int);
    for (int i = 0; i < 10; i++) XStack_push_base(s, &i);
    XPrintf("清空前 count=%zu\n", XStack_count_base(s));
    XStack_clear_base(s);
    XPrintf("清空后 count=%zu empty=%d (期望:0/1)\n",
        XStack_count_base(s), (int)XStack_empty_base(s));
    XStack_delete_base(s);
    XCoreApplication_quit();
}

/* ============================================================
 *  XLockFreeStack（真无锁栈，基于版本号+CAS）测试
 * ============================================================ */

/* -------- 6. XLockFreeStack 基础 LIFO / Qt 别名 -------- */
static void XLFSBasicTest(void)
{
    XPrintf("===== XLockFreeStack 基础 LIFO / Qt 别名 =====\n");
    XLockFreeStack* s = XLockFreeStack_Create(int, 16);
    for (int i = 0; i < 8; i++) {
        int v = i * 10;
        while (!XLockFreeStack_push_base(s, &v)) { /* retry */ }
    }
    XPrintf("count=%zu length=%zu empty=%d isFull=%d top=%d (期望:8/8/0/0/70)\n",
        XLockFreeStack_count_base(s), XLockFreeStack_length_base(s),
        (int)XLockFreeStack_empty_base(s),
        (int)XLockFreeStack_isFull_base(s),
        XLockFreeStack_Top_Base(s, int));

    int mismatch = 0;
    for (int i = 7; i >= 0; i--) {
        int v = -1;
        while (!XLockFreeStack_pop_return_base(s, &v)) { /* retry */ }
        if (v != i * 10) mismatch++;
    }
    XPrintf("LIFO 顺序 mismatch=%d empty=%d (期望:0/1)\n",
        mismatch, (int)XLockFreeStack_empty_base(s));
    XLockFreeStack_delete_base(s);
    XCoreApplication_quit();
}

/* -------- 7. XLockFreeStack isFull 边界 -------- */
static void XLFSFullTest(void)
{
    XPrintf("===== XLockFreeStack isFull 边界 =====\n");
    /* capacity 需为 2 的幂 */
    XLockFreeStack* s = XLockFreeStack_Create(int, 16);
    int filled = 0;
    for (int i = 0; i < 32; i++) {
        int v = i;
        if (XLockFreeStack_push_base(s, &v)) filled++;
        else break;
    }
    XPrintf("实际填入=%d count=%zu isFull=%d (期望:16/16/1)\n",
        filled, XLockFreeStack_count_base(s),
        (int)XLockFreeStack_isFull_base(s));
    XLockFreeStack_delete_base(s);
    XCoreApplication_quit();
}

/* -------- 8. XLockFreeStack 大量数据(单线程) -------- */
static void XLFSBulkTest(void)
{
    XPrintf("===== XLockFreeStack 大量数据(单线程 100000 轮) =====\n");
    XLockFreeStack* s = XLockFreeStack_Create(int, 1024);
    size_t pushed = 0, popped = 0;
    for (int round = 0; round < 100000; round++) {
        int v = round;
        while (!XLockFreeStack_push_base(s, &v)) {
            int out = 0;
            if (XLockFreeStack_pop_return_base(s, &out)) popped++;
        }
        pushed++;
    }
    int out = 0;
    while (XLockFreeStack_pop_return_base(s, &out)) popped++;
    XPrintf("pushed=%zu popped=%zu 剩余count=%zu (期望:100000/100000/0)\n",
        pushed, popped, XLockFreeStack_count_base(s));
    XLockFreeStack_delete_base(s);
    XCoreApplication_quit();
}

/* =========================================================
 *  XLockFreeStack 多生产/多消费并发测试
 * ========================================================= */
#define XLFS_CAPACITY    2048
#define XLFS_PRODUCERS   4
#define XLFS_CONSUMERS   8
#define XLFS_PER_PROD    5000

typedef struct XLFSCtx {
    XLockFreeStack*  s;
    XAtomic_size_t*  produced;
    XAtomic_size_t*  consumed;
    XAtomic_size_t*  producers_done;
    XAtomic_size_t*  threads_finished;
    size_t           threads_total;
} XLFSCtx;

static void XLFSProducer(XThread* thread, XVarList* vl)
{
    XVarList_args_1(vl, XLFSCtx*, ctx);
    for (int i = 0; i < XLFS_PER_PROD; i++) {
        int v = i;
        while (!XLockFreeStack_push_base(ctx->s, &v)) { /* retry */ }
        XAtomic_fetch_add_size_t(ctx->produced, 1, XAtomic_MemoryOrder_Relaxed);
    }
    size_t d = XAtomic_fetch_add_size_t(ctx->producers_done, 1,
        XAtomic_MemoryOrder_Relaxed) + 1;
    XPrintf("[P] tid=%p 完成 %d 项 (%zu/%d)\n",
        XThread_currentThreadId(), XLFS_PER_PROD, d, XLFS_PRODUCERS);
    size_t fin = XAtomic_fetch_add_size_t(ctx->threads_finished, 1,
        XAtomic_MemoryOrder_AcqRel) + 1;
    XThread_deleteLater(thread);
    if (fin == ctx->threads_total) XCoreApplication_quit();
}

static void XLFSConsumer(XThread* thread, XVarList* vl)
{
    XVarList_args_1(vl, XLFSCtx*, ctx);
    int local = 0, idle = 0;
    size_t target = (size_t)XLFS_PRODUCERS * (size_t)XLFS_PER_PROD;
    for (;;) {
        int v = 0;
        if (XLockFreeStack_pop_return_base(ctx->s, &v)) {
            local++; idle = 0;
            size_t total = XAtomic_fetch_add_size_t(ctx->consumed, 1,
                XAtomic_MemoryOrder_Relaxed) + 1;
            if (total >= target) break;
        } else {
            size_t pd = XAtomic_load_size_t(ctx->producers_done,
                XAtomic_MemoryOrder_Relaxed);
            if (pd >= XLFS_PRODUCERS) {
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
        XLockFreeStack_count_base(ctx->s));
    size_t fin = XAtomic_fetch_add_size_t(ctx->threads_finished, 1,
        XAtomic_MemoryOrder_AcqRel) + 1;
    XThread_deleteLater(thread);
    if (fin == ctx->threads_total) XCoreApplication_quit();
}

static void XLFSConcurrentTest(void)
{
    XPrintf("===== XLockFreeStack 并发 %dP/%dC (%d 项/生产者) =====\n",
        XLFS_PRODUCERS, XLFS_CONSUMERS, XLFS_PER_PROD);

    /* 预热 XEvent 子类 vtable，避免多线程首次调用 deleteLater 触发 lazy-init 竞争 */
    (void)XEvent_class_init();
    (void)XEventFunc_class_init();
    XLockFreeStack* s = XLockFreeStack_Create(int, XLFS_CAPACITY);
    XAtomic_size_t produced = { 0 };
    XAtomic_size_t consumed = { 0 };
    XAtomic_size_t producers_done = { 0 };
    XAtomic_size_t threads_finished = { 0 };
    XLFSCtx ctx = { s, &produced, &consumed, &producers_done,
        &threads_finished, (size_t)(XLFS_PRODUCERS + XLFS_CONSUMERS) };
    XLFSCtx* pctx = &ctx;

    for (int i = 0; i < XLFS_PRODUCERS; i++) {
        XThread* t = XThread_create_func(XLFSProducer,
            XVarList_Create(XVar(XLFSCtx*, pctx)));
        XThread_start(t);
    }
    for (int i = 0; i < XLFS_CONSUMERS; i++) {
        XThread* t = XThread_create_func(XLFSConsumer,
            XVarList_Create(XVar(XLFSCtx*, pctx)));
        XThread_start(t);
    }
    XCoreApplication_exec();

    size_t total = (size_t)XLFS_PRODUCERS * (size_t)XLFS_PER_PROD;
    XPrintf("并发结束: produced=%zu consumed=%zu remain=%zu (期望:%zu/%zu/0)\n",
        XAtomic_load_size_t(&produced, XAtomic_MemoryOrder_Relaxed),
        XAtomic_load_size_t(&consumed, XAtomic_MemoryOrder_Relaxed),
        XLockFreeStack_count_base(s),
        total, total);
    XLockFreeStack_delete_base(s);
    XPrintf("\n");
    XCoreApplication_quit();
}

/* -------- 全部（不含并发） -------- */
static void XStackAllTest(void)
{
    XPrintf("========== XStack/XLockFreeStack 全部测试开始(不含并发) ==========\n");
    XStackBasicTest();
    XStackMoveTest();
    XStackStringTest();
    XStackBulkTest();
    XStackClearTest();
    XLFSBasicTest();
    XLFSFullTest();
    XLFSBulkTest();
    XPrintf("========== XStack/XLockFreeStack 全部测试结束(不含并发) ==========\n");
    XCoreApplication_quit();
}
#endif /* XStack_ON */

void XMenu_XStackTest(XMenu* root)
{
    XMenu* menu = XMenu_create("XStack(栈)");
    XMenu_addMenu(root, menu);
#if XStack_ON
    { XAction* a = XMenu_addAction(menu, "【全部测试(不含并发)】"); XAction_setAction(a, (Action)XStackAllTest); }
    { XAction* a = XMenu_addAction(menu, "XStack 基础 LIFO / Qt 别名"); XAction_setAction(a, (Action)XStackBasicTest); }
    { XAction* a = XMenu_addAction(menu, "XStack 移动语义压栈"); XAction_setAction(a, (Action)XStackMoveTest); }
    { XAction* a = XMenu_addAction(menu, "XStack 变长元素(字符串)"); XAction_setAction(a, (Action)XStackStringTest); }
    { XAction* a = XMenu_addAction(menu, "XStack 压力(10000)"); XAction_setAction(a, (Action)XStackBulkTest); }
    { XAction* a = XMenu_addAction(menu, "XStack clear"); XAction_setAction(a, (Action)XStackClearTest); }
    { XAction* a = XMenu_addAction(menu, "XLockFreeStack 基础 LIFO / Qt 别名"); XAction_setAction(a, (Action)XLFSBasicTest); }
    { XAction* a = XMenu_addAction(menu, "XLockFreeStack isFull 边界"); XAction_setAction(a, (Action)XLFSFullTest); }
    { XAction* a = XMenu_addAction(menu, "XLockFreeStack 大量数据(单线程 100000)"); XAction_setAction(a, (Action)XLFSBulkTest); }
    { XAction* a = XMenu_addAction(menu, "XLockFreeStack 并发多生产/多消费"); XAction_setAction(a, (Action)XLFSConcurrentTest); }
#endif
}
#endif
