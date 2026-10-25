/* [两孩子删除句柄身份压测 2026-10-25]
 * 高精度定时器组压力测试：N 个定时器挂随机且互不相同的到期时间（高频
 * 命中 TwoChild_erase 两孩子删除），随机顺序按句柄 remove 一部分，其余
 * 等待触发；最后全量句柄回收。
 *
 * 断言：
 *   1. 每个未移除定时器的回调恰好触发一次，且 userData 身份正确——
 *      旧实现的 TwoChild_erase 交换节点内联数据，句柄身份漂移后此断言
 *      失败（回调收到别的定时器的槽位）；
 *   2. 被移除定时器的回调不触发；
 *   3. 全量 remove 后 count 归零，无崩溃（旧实现对同一节点会走两次
 *      free，安卓 Scudo "invalid chunk state" 实证）。
 * 返回失败断言数（0=通过）。
 */
#include "XHrTimerGroup.h"
#include "XCoreApplication.h"
#include "XPrintf.h"
#include "XThread.h"
#include <string.h>

typedef struct XHrStressSlot
{
    int magic;          /**< 身份魔数（槽位地址派生，唯一）。 */
    int fire_count;     /**< 实际触发次数。 */
    int expect_remove;  /**< true=注册后即计划移除（不应触发）。 */
    int removed_done;   /**< true=已按句柄移除（防二次 remove）。 */
} XHrStressSlot;

static void xhr_stress_cb(void* userData, XTimerData* data)
{
    XHrStressSlot* slot;
    (void)data;
    if (!userData) return;
    slot = (XHrStressSlot*)userData;
    /* 身份校验说明：数据交换型缺陷无法在回调内自检（userData 随数据
     * 一起被换走，槽位本身合法）——真正的断言在收尾的 fire_count 比对：
     * 句柄身份漂移会让"被移除槽位"顶替"保留槽位"触发，fire_count 与
     * expect 错位即暴露。 */
    ++slot->fire_count;
}

static unsigned xhr_stress_rand(unsigned* seed)
{
    *seed = *seed * 1103515245u + 12345u;
    return (*seed >> 16u) & 0x7fffu;
}

int XHrTimerStressTest(void)
{
    enum { N = 64, ROUNDS = 3 };
    XHrStressSlot slots[N];
    XHandle handles[N];
    int round;
    int failures = 0;
    unsigned seed = 20261025u;

    for (round = 0; round < ROUNDS; ++round)
    {
        XHrTimerGroup* group = XHrTimerGroup_create(1000000ULL);
        int i;
        int removed = 0;
        if (!group) { ++failures; break; }
        for (i = 0; i < N; ++i)
        {
            XTimerData data;
            memset(&data, 0, sizeof(data));
            slots[i].magic = (int)(size_t)&slots[i] ^ (int)(i * 2654435761u);
            slots[i].fire_count = 0;
            slots[i].expect_remove = 0;
            slots[i].removed_done = 0;
            data.m_timeout = 5 + (size_t)xhr_stress_rand(&seed) % 200; /* 毫秒 */
            data.m_interval = 0;
            data.m_isSingleShot = true;
            data.m_timerCallback = xhr_stress_cb;
            data.m_userData = &slots[i];
            handles[i] = XHrTimerGroup_addTimerMs_base(group, data);
            if (!handles[i]) { XPrintf("XHR-FAIL: add %d\n", i); ++failures; }
        }
        /* 随机移除一半（乱序句柄，制造两孩子删除 + 结构后继回写）。 */
        for (i = 0; i < N; ++i)
        {
            if (xhr_stress_rand(&seed) % 2 == 0)
            {
                slots[i].expect_remove = 1;
                slots[i].removed_done = 1;
                XHrTimerGroup_removeTimer_base(group, handles[i]);
                ++removed;
            }
        }
        /* 等待全部到期后 tick 收口（分几轮，模拟真实节奏）。 */
        for (i = 0; i < 8; ++i)
        {
            XThread_msleep(80);
            XHrTimerGroup_tick_base(group);
        }
        /* 断言：未移除的恰好触发一次；被移除的不触发。 */
        for (i = 0; i < N; ++i)
        {
            int expect = slots[i].expect_remove ? 0 : 1;
            if (slots[i].fire_count != expect)
            {
                XPrintf("XHR-FAIL: slot %d fire=%d expect=%d\n",
                        i, slots[i].fire_count, expect);
                ++failures;
            }
        }
        /* 全量句柄回收：仅对未移除过的句柄 remove（已移除句柄的节点
         * 已释放，二次 remove 属调用方误用，真实调度器有 XFd 表守卫）。 */
        for (i = 0; i < N; ++i)
        {
            if (!slots[i].removed_done)
                XHrTimerGroup_removeTimer_base(group, handles[i]);
        }
        if (XHrTimerGroup_count(group) != 0)
        {
            XPrintf("XHR-FAIL: count=%d after full reclaim\n",
                    (int)XHrTimerGroup_count(group));
            ++failures;
        }
        XClassDelete(group);
        XPrintf("XHR round %d done (removed=%d failures=%d)\n",
                round, removed, failures);
    }

    /* [周期重插回归 2026-10-25] 同刻到期 → 全脱离（树清空）→ 逐颗重插。
     * 修复前：首颗重插走 insertAlign 空树早退分支，节点 RChild 里残留的
     * 过期链表"下一颗"指针原样入树；第二颗重插顺着残留指针游走即自成环
     * ——insertAdjust SpinLL 永旋（安卓真机下拉框卡死实证）。
     * 断言：同刻到期同刻重挂，各表触发次数必须一致且非零；中途 count
     * 恒等于表数；全量回收后归零。 */
    {
        enum { P = 12, PROUNDS = 6 };
        XHrStressSlot pslots[P];
        XHandle phandles[P];
        XHrTimerGroup* group = XHrTimerGroup_create(1000000ULL);
        int i, r;
        if (!group) { ++failures; }
        else
        {
            for (i = 0; i < P; ++i)
            {
                XTimerData data;
                memset(&data, 0, sizeof(data));
                pslots[i].magic = 0;
                pslots[i].fire_count = 0;
                pslots[i].expect_remove = 0;
                pslots[i].removed_done = 0;
                data.m_timeout = 30;   /* 毫秒；全部同刻 → 同 tick 全脱离 */
                data.m_interval = 30;  /* 周期：tick 阶段三重插回树 */
                data.m_isSingleShot = false;
                data.m_timerCallback = xhr_stress_cb;
                data.m_userData = &pslots[i];
                phandles[i] = XHrTimerGroup_addTimerMs_base(group, data);
                if (!phandles[i]) { XPrintf("XHR-FAIL: padd %d\n", i); ++failures; }
            }
            for (r = 0; r < PROUNDS; ++r)
            {
                XThread_msleep(40);
                XHrTimerGroup_tick_base(group);
                if (XHrTimerGroup_count(group) != P)
                {
                    XPrintf("XHR-FAIL: periodic count=%d expect=%d (round %d)\n",
                            (int)XHrTimerGroup_count(group), P, r);
                    ++failures;
                    break;
                }
            }
            /* 同刻同刻重挂 → 各表触发次数必须一致；且至少触发过 2 轮。 */
            for (i = 1; i < P; ++i)
            {
                if (pslots[i].fire_count != pslots[0].fire_count ||
                    pslots[0].fire_count < 2)
                {
                    XPrintf("XHR-FAIL: periodic fire[%d]=%d vs [0]=%d\n",
                            i, pslots[i].fire_count, pslots[0].fire_count);
                    ++failures;
                }
            }
            /* 全量回收（12 颗在树内周期节点，含两孩子删除）。 */
            for (i = 0; i < P; ++i)
                XHrTimerGroup_removeTimer_base(group, phandles[i]);
            if (XHrTimerGroup_count(group) != 0)
            {
                XPrintf("XHR-FAIL: periodic count=%d after reclaim\n",
                        (int)XHrTimerGroup_count(group));
                ++failures;
            }
            XClassDelete(group);
            XPrintf("XHR periodic done (fire=%d failures=%d)\n",
                    pslots[0].fire_count, failures);
        }
    }

    XPrintf("XHrTimerStressTest %s\n", failures == 0 ? "PASS" : "FAIL");
    return failures;
}
