/**
 * @file       XSignalHandleAlignTest.c
 * @brief      「连接句柄 + 信号阻塞守卫」与 Qt 6.8 对齐契约测试。
 * @details    补齐 gaps 台账第 6、16、19 条：
 *             - QMetaObject::Connection 的**引用计数句柄**语义
 *               （XConnection / XConnectionHandle：create / create2 /
 *               retain / release / disconnect / isValid）；
 *             - QSignalBlocker 的**栈式守卫**语义
 *               （XSignalBlocker：init / destroy，记住并恢复前态）；
 *             - connect 对无效信号 id 告警并返回无效连接（不静默建桶）。
 *
 *             Qt 6.8 依据（本机探针 /tmp/probe_conn.cpp、/tmp/probe_conn2.cpp，
 *             用 `g++ -std=c++17 -fPIC ... $(pkg-config --cflags --libs
 *             Qt6Core)` 编译后 `QT_QPA_PLATFORM=offscreen` 运行，实测输出）：
 *               ① 断开一个 Connection 后 `bool(conn)` 立即为 false
 *                  （`disconnect1=1 bool-after=0`）；
 *               ② 对已断开的 Connection 再 disconnect 返回 false
 *                  （`disconnect2=0`），**不崩溃**；
 *               ③ 拷贝一个已断开的 Connection 安全，`bool(copy)==0`；
 *               ④ 无效信号：`connect(sender, SIGNAL(noSuchSignal(int)), ...)`
 *                  打印 `QObject::connect: No such signal`，返回
 *                  `bool(conn)==0`，对其 disconnect 返回 false；
 *               ⑤ QSignalBlocker 嵌套：先 block(true) 再建 blocker，退出后
 *                  仍为阻塞（`blk-restore-blocked=1`）；未阻塞时建 blocker，
 *                  退出后恢复未阻塞（`blk2-restore-blocked=0`）；
 *                  `blockSignals()` 返回调用前状态（`blockSignals-false-prev=1`）；
 *               ⑥ 接收者/发送者析构后 `bool(conn)==0`、disconnect 返回 false
 *                  （`after-receiver-deleted bool=0` / `after-sender-deleted bool=0`）。
 *               ⑦ 本轮（2026-11-11）用 /tmp/probe_conn_x.cpp 复跑 ①~⑥，
 *                  输出与上面逐条一致（含 `bad-valid=0 disconnect-bad=0`、
 *                  `blocked-after-inner=1 blocked-after-outer=0`）。
 *
 *             X 侧口径：
 *               - 句柄就是 `XConnection*`（`XConnectionHandle` 是同一类型的
 *                 Qt 命名别名）；发送者连接表持 1 个引用，连接对象释放时从
 *                 **全局活连接表**注销，任何接受句柄的 API 先查表再解引用
 *                 ⇒ 重复断开/接收者析构后断开都安全；
 *               - 断开后仍要保存句柄必须 `XConnection_retain`，用完
 *                 `XConnection_release`；未 retain 就 release 会被拒绝
 *                 （那一份属于发送者表，释放会让仍挂在 connList 里的连接
 *                 对象被回收 ⇒ 下次发射 UAF）；
 *               - `XSignalBlocker_init/destroy` 与 `_ctor/_dtor` 是同一实现
 *                 （后者是 Qt 命名别名，保留给既有调用点）。
 *
 *             泄漏由运行器自动判定：本文件所有对象显式 XClassDelete，
 *             所有 retain 的句柄都配平 release。
 * @note       C99；断言失败不中断用例（XQ_CHECK），前置条件用 XQ_REQUIRE。
 * @author     XinYueC 团队
 */
#include "xq_align.h"

#include "XObject.h"
#include "XSignalSlot.h"
#include "XEvent.h"

/* ==================== 探针信号/槽 ==================== */

/** @brief 测试用信号（self==NULL 时返回自身地址作为信号标识）。 */
static void* xqHandleProbeSignal(XObject* self)
{
    XEmitSignal(self, xqHandleProbeSignal, NULL, NULL, NULL, XEVENT_PRIORITY_NORMAL);
}

static int g_handleHits;        /**< 计数槽命中次数 */
static int g_handleDestroyed;   /**< destroyed 槽命中次数 */

/** @brief 计数槽。 */
static void xqHandleSlot_count(XObject* receiver, XVarList* args)
{
    (void)receiver;
    (void)args;
    ++g_handleHits;
}

/** @brief destroyed 槽（只计数）。 */
static void xqHandleSlot_destroyed(XObject* receiver, XVarList* args)
{
    (void)receiver;
    (void)args;
    ++g_handleDestroyed;
}

/** @brief 只拿发送者、不用 receiver 的槽（connect_2 / connect_context 用）。 */
static void xqHandleSlot_senderOnly(XObject* sender, XVarList* args)
{
    (void)sender;
    (void)args;
    ++g_handleHits;
}

/** @brief 重置本文件计数器。 */
static void xqHandleReset(void)
{
    g_handleHits = 0;
    g_handleDestroyed = 0;
}

/* ==================== 用例 ==================== */

/**
 * @brief 引用计数句柄：retain/release 配平、断开后 retain 安全、误 release 被拒。
 * @details Qt 对应：`QMetaObject::Connection` 的拷贝构造持有、析构释放；
 *          断开后 `bool(conn)==false`（探针①②③）。
 */
XQ_TEST(XSignalHandle, retainReleaseBalance)
{
    XObject* emitter = XObject_create();
    XObject* receiver = XObject_create();
    XConnectionHandle* h = NULL;

    xqHandleReset();
    XQ_REQUIRE(emitter != NULL && receiver != NULL);

    h = XObject_connect_1(emitter, XSignal(xqHandleProbeSignal), receiver,
                          xqHandleSlot_count, XConnectionType_Direct);
    XQ_CHECK_PTR(h);
    XQ_REQUIRE(h != NULL);
    XQ_CHECK(XConnection_isValid(h));           /* 连接中 ⇒ 有效句柄 */

    /* 句柄的空指针语义 */
    XQ_CHECK(!XConnection_isValid(NULL));
    XQ_CHECK(!XConnection_retain(NULL));
    XQ_CHECK(!XConnection_release(NULL));
    XQ_CHECK(!XConnection_disconnect(NULL));

    /* 两次"拷贝句柄" ⇒ 引用计数 1(发送者) + 2(调用方) */
    XQ_CHECK(XConnection_retain(h));
    XQ_CHECK(XConnection_retain(h));
    XQ_CHECK(XConnection_isValid(h));

    xqHandleProbeSignal(emitter);
    XQ_EQ_I64(g_handleHits, 1);

    /* 断开：只释放发送者表那一份，调用方的两份仍持有句柄对象 */
    XQ_CHECK(XConnection_disconnect(h));
    XQ_CHECK(!XConnection_isValid(h));          /* Qt 探针①：bool==false */

    /* 断开后 retain 安全（句柄对象还活着；Qt 拷一个已断开的 Connection
     * 同样安全，copy 的 bool 为 false） */
    XQ_CHECK(XConnection_retain(h));
    XQ_CHECK(!XConnection_isValid(h));
    XQ_CHECK(!XConnection_disconnect(h));       /* Qt 探针②：重复断开 false */

    xqHandleProbeSignal(emitter);
    XQ_EQ_I64(g_handleHits, 1);                 /* 断开后不再派发 */

    /* 引用账：1(发送者) + 2(初始 retain) = 3 → 断开释放发送者那份 = 2
     *         → 断开后 retain = 3 份调用方引用
     *         ⇒ 需要三次 release 才归零（前两次 false，最后一次 true）。 */
    XQ_CHECK(!XConnection_release(h));          /* 3 → 2 */
    XQ_CHECK(!XConnection_release(h));          /* 2 → 1 */
    XQ_CHECK(XConnection_release(h));           /* 1 → 0：句柄对象回收 */
    /* 已释放的旧句柄：所有入口只返回 false，不解引用 */
    XQ_CHECK(!XConnection_isValid(h));
    XQ_CHECK(!XConnection_retain(h));
    XQ_CHECK(!XConnection_release(h));
    XQ_CHECK(!XConnection_disconnect(h));
    XQ_CHECK(!XObject_disconnect_2(h));

    /* **未 retain 就 release**：那一份属于发送者连接表，必须拒绝——
     * 否则连接对象会在仍挂在 connList 里时被释放（下次发射 UAF）。 */
    h = XObject_connect_1(emitter, XSignal(xqHandleProbeSignal), receiver,
                          xqHandleSlot_count, XConnectionType_Direct);
    XQ_REQUIRE(h != NULL);
    XQ_CHECK(!XConnection_release(h));          /* 拒绝 */
    XQ_CHECK(XConnection_isValid(h));           /* 连接仍在 */
    xqHandleProbeSignal(emitter);
    XQ_EQ_I64(g_handleHits, 2);
    XQ_CHECK(XObject_disconnect_2(h));          /* 正常断开仍然可以 */
    XQ_CHECK(!XConnection_isValid(h));

    XClassDelete((XClass*)emitter);
    XClassDelete((XClass*)receiver);
}

/**
 * @brief 重复断开：第二次起返回 false 且不崩溃（Qt 探针②）。
 * @details 覆盖两种情况：未 retain（断开即回收，旧指针仍在活表外）与
 *          retain（连接对象留存但已失效）。
 */
XQ_TEST(XSignalHandle, repeatDisconnectIsSafe)
{
    XObject* emitter = XObject_create();
    XObject* receiver = XObject_create();
    XObject* stranger = XObject_create();
    XConnection* conn = NULL;

    xqHandleReset();
    XQ_REQUIRE(emitter != NULL && receiver != NULL && stranger != NULL);

    /* —— 未 retain：断开即回收 —— */
    conn = XObject_connect_1(emitter, XSignal(xqHandleProbeSignal), receiver,
                             xqHandleSlot_count, XConnectionType_Direct);
    XQ_REQUIRE(conn != NULL);
    XQ_CHECK(XObject_disconnect_2(conn));       /* 第一次：真的断开 */
    XQ_CHECK(!XConnection_isValid(conn));
    /* 重复断开：活连接表已注销该地址 ⇒ 只返回 false，不解引用 */
    XQ_CHECK(!XObject_disconnect_2(conn));
    XQ_CHECK(!XObject_disconnect_2(conn));
    XQ_CHECK(!XConnection_disconnect(conn));
    xqHandleProbeSignal(emitter);
    XQ_EQ_I64(g_handleHits, 0);

    /* —— retain：句柄对象留存，重复断开同样 false —— */
    conn = XObject_connect_1(emitter, XSignal(xqHandleProbeSignal), receiver,
                             xqHandleSlot_count, XConnectionType_Direct);
    XQ_REQUIRE(conn != NULL);
    XQ_CHECK(XConnection_retain(conn));
    XQ_CHECK(XObject_disconnect_2(conn));
    XQ_CHECK(!XObject_disconnect_2(conn));
    XQ_CHECK(!XConnection_disconnect(conn));
    XQ_CHECK(XConnection_release(conn));        /* 回收调用方那份 */

    /* —— 断开的对象/句柄组合 —— */
    XQ_CHECK(!XObject_disconnect_2(NULL));
    XQ_CHECK(!XConnection_disconnect(NULL));
    XQ_CHECK(!XObject_disconnect_1(emitter, XSignal(xqHandleProbeSignal),
                                   stranger, xqHandleSlot_count));  /* 无匹配 */
    XQ_CHECK(!XObject_disconnect_1(NULL, 0, NULL, NULL));

    XClassDelete((XClass*)emitter);
    XClassDelete((XClass*)receiver);
    XClassDelete((XClass*)stranger);
}

/**
 * @brief 接收者析构后断开句柄：安全 + 返回 false（Qt 探针⑥）。
 * @details 接收者析构时其 XSignalSlot 把连接从发送者表反注册——
 *          retain 过的句柄仍存活但 `isValid()==false`；未 retain 的句柄
 *          随连接一起回收，旧指针各入口同样只返回 false。
 */
XQ_TEST(XSignalHandle, receiverDestroyedThenDisconnect)
{
    XObject* emitter = XObject_create();
    XObject* receiver = XObject_create();
    XObject* receiver2 = XObject_create();
    XConnection* keep = NULL;
    XConnection* plain = NULL;

    xqHandleReset();
    XQ_REQUIRE(emitter != NULL && receiver != NULL && receiver2 != NULL);

    /* —— retain 过的句柄 —— */
    keep = XObject_connect_1(emitter, XSignal(xqHandleProbeSignal), receiver,
                             xqHandleSlot_count, XConnectionType_Direct);
    XQ_REQUIRE(keep != NULL);
    XQ_CHECK(XConnection_retain(keep));
    xqHandleProbeSignal(emitter);
    XQ_EQ_I64(g_handleHits, 1);

    XClassDelete((XClass*)receiver);            /* 接收者亡 ⇒ 连接自动断开 */
    XQ_CHECK(!XConnection_isValid(keep));       /* Qt 探针⑥：bool==false */
    XQ_CHECK(!XObject_disconnect_2(keep));      /* 断开失效句柄：false，不崩 */
    XQ_CHECK(!XConnection_disconnect(keep));
    xqHandleProbeSignal(emitter);
    XQ_EQ_I64(g_handleHits, 1);                 /* 槽不再被调用 */
    XQ_CHECK(XConnection_release(keep));        /* 释放调用方那份 ⇒ 回收 */

    /* —— 未 retain 的句柄：随连接一起回收 —— */
    plain = XObject_connect_1(emitter, XSignal(xqHandleProbeSignal), receiver2,
                              xqHandleSlot_count, XConnectionType_Direct);
    XQ_REQUIRE(plain != NULL);
    XQ_CHECK(XConnection_isValid(plain));
    XClassDelete((XClass*)receiver2);
    /* 此后 plain 已释放：只做"不安全指针"的查表判定，不做任何解引用 */
    XQ_CHECK(!XConnection_isValid(plain));
    XQ_CHECK(!XObject_disconnect_2(plain));
    XQ_CHECK(!XConnection_retain(plain));
    XQ_CHECK(!XConnection_release(plain));
    xqHandleProbeSignal(emitter);
    XQ_EQ_I64(g_handleHits, 1);

    /* 发送者仍可继续正常建新连接（旧连接已从表中彻底移除） */
    {
        XObject* late = XObject_create();
        XConnection* fresh = NULL;
        XQ_REQUIRE(late != NULL);
        fresh = XObject_connect_1(emitter, XSignal(xqHandleProbeSignal), late,
                                  xqHandleSlot_count, XConnectionType_Direct);
        XQ_CHECK_PTR(fresh);
        xqHandleProbeSignal(emitter);
        XQ_EQ_I64(g_handleHits, 2);
        XQ_CHECK(XObject_disconnect_2(fresh));
        XClassDelete((XClass*)late);
    }

    XClassDelete((XClass*)emitter);
}

/**
 * @brief 发送者析构后断开句柄：安全 + 返回 false（Qt 探针⑥）。
 * @details 发送者析构时清空自己的连接表，retain 过的句柄留存但失效；
 *          未 retain 的句柄随连接回收。接收者随后析构也不得二次释放连接。
 */
XQ_TEST(XSignalHandle, senderDestroyedThenDisconnect)
{
    XObject* emitter = XObject_create();
    XObject* receiver = XObject_create();
    XObject* emitter2 = XObject_create();
    XObject* receiver2 = XObject_create();
    XConnection* keep = NULL;
    XConnection* plain = NULL;

    xqHandleReset();
    XQ_REQUIRE(emitter != NULL && receiver != NULL && emitter2 != NULL
               && receiver2 != NULL);

    /* —— retain 过的句柄 —— */
    keep = XObject_connect_1(emitter, XSignal(xqHandleProbeSignal), receiver,
                             xqHandleSlot_count, XConnectionType_Direct);
    XQ_REQUIRE(keep != NULL);
    XQ_CHECK(XConnection_retain(keep));
    XClassDelete((XClass*)emitter);             /* 发送者亡 ⇒ 连接表清空 */
    XQ_CHECK(!XConnection_isValid(keep));
    XQ_CHECK(!XObject_disconnect_2(keep));
    XQ_CHECK(!XConnection_disconnect(keep));
    XQ_CHECK(XConnection_release(keep));        /* 回收 */

    /* 接收者仍活：其簿记表不得残留已释放的连接（残留会在析构时二次释放） */
    XClassDelete((XClass*)receiver);

    /* —— 未 retain 的句柄 —— */
    plain = XObject_connect_1(emitter2, XSignal(xqHandleProbeSignal), receiver2,
                              xqHandleSlot_count, XConnectionType_Direct);
    XQ_REQUIRE(plain != NULL);
    XClassDelete((XClass*)emitter2);
    XQ_CHECK(!XConnection_isValid(plain));
    XQ_CHECK(!XObject_disconnect_2(plain));
    XQ_CHECK(!XConnection_retain(plain));
    XQ_CHECK(!XConnection_release(plain));
    XClassDelete((XClass*)receiver2);
}

/**
 * @brief 无效信号 id：返回无效连接 + 不建桶（Qt 探针④）。
 * @details X 的信号 id 是信号函数在定义模块内的地址，**唯一可判定的非法值
 *          是 0**：`XSignalSlot_connect1` 打告警并返回 NULL，不创建空桶。
 *          非 0 地址的合法性校验需要每个信号登记签名表（新子系统，deferred）。
 */
XQ_TEST(XSignalHandle, invalidSignalIdRejected)
{
    XObject* emitter = XObject_create();
    XObject* receiver = XObject_create();
    XConnection* good = NULL;

    xqHandleReset();
    XQ_REQUIRE(emitter != NULL && receiver != NULL);

    /* 三种 connect 入口对 id==0 都返回无效连接 */
    XQ_CHECK(XObject_connect_1(emitter, 0, receiver, xqHandleSlot_count,
                               XConnectionType_Direct) == NULL);
    XQ_CHECK(XObject_connect_2(emitter, 0, xqHandleSlot_senderOnly) == NULL);
    XQ_CHECK(XObject_connect_context(emitter, 0, receiver,
                                     xqHandleSlot_senderOnly,
                                     XConnectionType_Direct) == NULL);

    /* 没有静默建桶：0 号信号上 0 个接收者（若旧实现建了空桶，这里会是 0，
     * 所以再补一条"发射 0 号信号也不触达任何槽"） */
    XQ_CHECK(emitter->m_signalSlot == NULL
             || XSignalSlot_receivers(emitter->m_signalSlot, 0) == 0);
    XObject_emitSignal(emitter, 0, NULL, NULL, NULL, XEVENT_PRIORITY_NORMAL);
    XQ_EQ_I64(g_handleHits, 0);

    /* 无效请求之后，合法信号照常连接/派发（守卫不污染连接表） */
    good = XObject_connect_1(emitter, XSignal(xqHandleProbeSignal), receiver,
                             xqHandleSlot_count, XConnectionType_Direct);
    XQ_CHECK_PTR(good);
    XQ_REQUIRE(good != NULL);
    xqHandleProbeSignal(emitter);
    XQ_EQ_I64(g_handleHits, 1);
    XQ_CHECK(XObject_disconnect_2(good));

    XClassDelete((XClass*)emitter);
    XClassDelete((XClass*)receiver);
}

/**
 * @brief 句柄工厂 `XConnectionHandle_create/create2`：与裸指针 connect 同源同语义。
 * @details 对标 Qt `QMetaObject::Connection` 由 `connect(...)` 返回这一条：
 *          X 侧不复制连接逻辑，工厂逐字转调 `XObject_connect_1/2`（单一事实源），
 *          故无效信号 id、NULL 参数等行为与裸指针入口完全一致。
 *          本用例验证「工厂返回的句柄可直接参与 retain/disconnect/release 账」。
 */
XQ_TEST(XSignalHandle, handleCreateWrappers)
{
    XObject* emitter = XObject_create();
    XObject* receiver = XObject_create();
    XConnectionHandle* h1 = NULL;
    XConnectionHandle* h2 = NULL;

    xqHandleReset();
    XQ_REQUIRE(emitter != NULL && receiver != NULL);

    /* create == connect_1：receiver 版本 */
    h1 = XConnectionHandle_create(emitter, XSignal(xqHandleProbeSignal), receiver,
                                  xqHandleSlot_count, XConnectionType_Direct);
    XQ_CHECK_PTR(h1);
    XQ_REQUIRE(h1 != NULL);
    XQ_CHECK(XConnection_isValid(h1));
    xqHandleProbeSignal(emitter);
    XQ_EQ_I64(g_handleHits, 1);

    /* create2 == connect_2：无 receiver，槽首参是发送者 */
    h2 = XConnectionHandle_create2(emitter, XSignal(xqHandleProbeSignal),
                                   xqHandleSlot_senderOnly);
    XQ_CHECK_PTR(h2);
    XQ_REQUIRE(h2 != NULL);
    XQ_CHECK(XConnection_isValid(h2));
    xqHandleProbeSignal(emitter);
    XQ_EQ_I64(g_handleHits, 3);                 /* 两条连接各命中一次 */

    /* 无效信号 id：与 connect 同口径（告警 + NULL，不建桶） */
    XQ_CHECK(XConnectionHandle_create(emitter, 0, receiver, xqHandleSlot_count,
                                      XConnectionType_Direct) == NULL);
    XQ_CHECK(XConnectionHandle_create2(emitter, 0, xqHandleSlot_senderOnly) == NULL);

    /* 工厂句柄可参与完整引用账：retain → disconnect → 失效但仍可检查 →
     * 重复断开安全 → release 回收调用方那份。 */
    XQ_CHECK(XConnection_retain(h1));
    XQ_CHECK(XConnection_disconnect(h1));
    XQ_CHECK(!XConnection_isValid(h1));
    XQ_CHECK(!XConnection_disconnect(h1));      /* 重复断开：false，不崩 */
    xqHandleProbeSignal(emitter);
    XQ_EQ_I64(g_handleHits, 4);                 /* 只剩 create2 那条 */
    XQ_CHECK(XConnection_release(h1));          /* 回收调用方那份 */

    /* create2 的句柄未 retain：断开即回收，旧指针各入口只返回 false */
    XQ_CHECK(XConnection_disconnect(h2));
    XQ_CHECK(!XConnection_disconnect(h2));
    XQ_CHECK(!XConnection_isValid(h2));
    XQ_CHECK(!XConnection_retain(h2));
    XQ_CHECK(!XConnection_release(h2));
    xqHandleProbeSignal(emitter);
    XQ_EQ_I64(g_handleHits, 4);                 /* 全部断开，槽不再触发 */

    /* NULL 参数：与裸指针入口同口径返回 NULL */
    XQ_CHECK(XConnectionHandle_create(NULL, 1, receiver, xqHandleSlot_count,
                                      XConnectionType_Direct) == NULL);
    XQ_CHECK(XConnectionHandle_create(emitter, 1, receiver, NULL,
                                      XConnectionType_Direct) == NULL);
    XQ_CHECK(XConnectionHandle_create2(NULL, 1, xqHandleSlot_senderOnly) == NULL);
    XQ_CHECK(XConnectionHandle_create2(emitter, 1, NULL) == NULL);

    XClassDelete((XClass*)emitter);
    XClassDelete((XClass*)receiver);
}

/**
 * @brief XSignalBlocker：进入阻塞、退出恢复**前态**、嵌套、空对象、destroyed 例外。
 * @details Qt 6.8 探针⑤逐档实测（/tmp/probe_conn.cpp）：
 *          - 未阻塞时建 blocker ⇒ 退出后 `signalsBlocked()==false`；
 *          - 先 block(true) 再建 blocker ⇒ 退出后仍 `true`（恢复前态，
 *            不是无条件解开）；
 *          - 嵌套 blocker 内层退出仍阻塞，最外层退出才解开；
 *          - `blockSignals(false)` 返回调用前状态（prev==true）。
 *          `destroyed()` 不受阻塞影响（Qt 文档 + 探针）。
 */
XQ_TEST(XSignalHandle, signalBlockerNestedRestore)
{
    XObject* emitter = XObject_create();
    XObject* receiver = XObject_create();
    XSignalBlocker outer;
    XSignalBlocker inner;
    XSignalBlocker empty;

    xqHandleReset();
    XQ_REQUIRE(emitter != NULL && receiver != NULL);
    XQ_CHECK(XObject_connect_1(emitter, XSignal(xqHandleProbeSignal), receiver,
                               xqHandleSlot_count, XConnectionType_Direct) != NULL);

    /* 起点：未阻塞 */
    XQ_CHECK(!XObject_signalsBlocked(emitter));
    xqHandleProbeSignal(emitter);
    XQ_EQ_I64(g_handleHits, 1);

    /* 进入阻塞并记住旧值 false */
    XSignalBlocker_init(&outer, emitter);
    XQ_CHECK(outer.m_object == emitter);
    XQ_CHECK(!outer.m_wasBlocked);              /* 旧值 == false */
    XQ_CHECK(XObject_signalsBlocked(emitter));
    xqHandleProbeSignal(emitter);
    XQ_EQ_I64(g_handleHits, 1);                 /* 阻塞期间槽不触发 */

    /* 嵌套：内层记住"已被阻塞"，退出内层仍保持阻塞 */
    XSignalBlocker_init(&inner, emitter);
    XQ_CHECK(inner.m_object == emitter);
    XQ_CHECK(inner.m_wasBlocked);               /* 旧值 == true */
    XSignalBlocker_destroy(&inner);
    XQ_CHECK(XObject_signalsBlocked(emitter));  /* 内层退出不解开 */
    xqHandleProbeSignal(emitter);
    XQ_EQ_I64(g_handleHits, 1);

    /* 最外层退出：恢复到进入前的 false */
    XSignalBlocker_destroy(&outer);
    XQ_CHECK(!XObject_signalsBlocked(emitter));
    xqHandleProbeSignal(emitter);
    XQ_EQ_I64(g_handleHits, 2);

    /* 重复 destroy 是 no-op（m_object 已清空） */
    XSignalBlocker_destroy(&outer);

    /* 构造前已阻塞 ⇒ 退出后仍阻塞（Qt 探针⑤ blk-restore-blocked=1） */
    XQ_CHECK(!XObject_blockSignals(emitter, true));   /* prev=false */
    XSignalBlocker_init(&outer, emitter);
    XQ_CHECK(outer.m_wasBlocked);
    XSignalBlocker_destroy(&outer);
    XQ_CHECK(XObject_signalsBlocked(emitter));
    XQ_CHECK(XObject_blockSignals(emitter, false));   /* prev=true（Qt 同口径） */
    XQ_CHECK(!XObject_signalsBlocked(emitter));

    /* NULL 对象：守卫为空操作，destroy 可重复 */
    XSignalBlocker_init(&empty, NULL);
    XQ_CHECK(empty.m_object == NULL);
    XSignalBlocker_destroy(&empty);
    XSignalBlocker_destroy(&empty);
    /* NULL 守卫存储本身也不得崩溃 */
    XSignalBlocker_init(NULL, emitter);
    XSignalBlocker_destroy(NULL);
    XQ_CHECK(!XObject_signalsBlocked(emitter));
    /* NULL 对象上的裸 API */
    XQ_CHECK(!XObject_signalsBlocked(NULL));
    XQ_CHECK(!XObject_blockSignals(NULL, true));

    /* destroyed() 是唯一不受阻塞影响的信号（Qt 文档；X 在发射路径按标识放行） */
    XQ_CHECK(XObject_connect_1(emitter, XSignal(XObject_destroyed_signal), receiver,
                               xqHandleSlot_destroyed, XConnectionType_Direct) != NULL);
    XQ_CHECK(!XObject_blockSignals(emitter, true));
    XClassDelete((XClass*)emitter);
    XQ_EQ_I64(g_handleDestroyed, 1);
    XQ_EQ_I64(g_handleHits, 2);                 /* 阻塞期间 destroyed 之下的普通槽不触发 */

    XClassDelete((XClass*)receiver);
}
