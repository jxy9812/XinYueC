/**
 * @file       HsfServerIoPump.c
 * @brief      服务端进水泵组实现（命令状态 + DO 回显校准 + 外部改动接管）。
 * @details    纯业务层：不持有任何传输对象，DO 回显由调用方（HsfServerIo）
 *             经数字量站喂进来。泵与阀的本质区别见头文件（无反馈 ⇒ 无状态
 *             机 ⇒ 无超时告警）。状态判读口径与 Python 侧 pumps.py 逐条对齐。
 */
#include "HsfServerIoPump.h"

#include "XPrintf.h"
#include <string.h>
#include <stdio.h>

/* ==================== 生命周期 ==================== */

static void VHsfServerIoPump_deinit(XObject* obj)
{
    HsfServerIoPump* st = (HsfServerIoPump*)obj;
    if (!st) return;
    HsfServerIoPump_clear(st);
    XClass_Deinit_Parent(XObject, obj);
}

XVtable* HsfServerIoPump_class_init(void)
{
    XVTABLE_INIT_DEFAULT(HsfServerIoPump)
    XVTABLE_INHERIT_XCLASS(XObject);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Deinit, VHsfServerIoPump_deinit);
    XCLASS_SHOW_SIZE_DEFAULT(HsfServerIoPump);
    return XVTABLE_DEFAULT;
}

void HsfServerIoPump_init(HsfServerIoPump* st)
{
    int i;
    if (!st) return;
    memset(st, 0, sizeof(*st));
    XObject_init((XObject*)st);
    XClassGetVtable(st) = HsfServerIoPump_class_init();
    for (i = 0; i < HSF_PUMP_COUNT; ++i)
        st->m_items[i].m_do = -1;
}

void HsfServerIoPump_clear(HsfServerIoPump* st)
{
    if (!st) return;
    HsfServerIoPump_init(st);
}

/* ==================== 装配 ==================== */

bool HsfServerIoPump_configure(HsfServerIoPump* st,
                               const HsfServerIoPumpCfg* items, int count,
                               bool enabled)
{
    int i;
    int readyCount = 0;

    if (!st) return false;
    HsfServerIoPump_init(st);

    st->m_enabled = enabled;
    if (count < 0) count = 0;
    if (count > HSF_PUMP_COUNT) count = HSF_PUMP_COUNT;
    st->m_count = count;

    for (i = 0; i < count; ++i) {
        HsfServerIoPumpItem* it = &st->m_items[i];
        if (items) {
            if (items[i].m_name && items[i].m_name[0])
                snprintf(it->m_name, sizeof(it->m_name), "%s", items[i].m_name);
            it->m_do = items[i].m_do;
        } else {
            it->m_do = -1;
        }
        if (!it->m_name[0])
            snprintf(it->m_name, sizeof(it->m_name),
                     "\xE8\xBF\x9B\xE6\xB0\xB4\xE6\xB3\xB5%d", i + 1); /* 进水泵N */
        it->m_on = false;
        it->m_knownDo = false;
        it->m_knownDoSeeded = false;
        it->m_valid = false;
        /* [缺省乐观] 首拍 poll 之前没有在线信息，不能凭空判离线——否则
         * 装配完成到第一次回显之间用户点启停会被莫名拒绝。待首拍覆写。 */
        it->m_online = true;
        if (it->m_do < 0)
            snprintf(it->m_text, sizeof(it->m_text),
                     "\xE7\x82\xB9\xE4\xBD\x8D\xE6\x9C\xAA\xE9\x85\x8D\xE7\xBD\xAE"); /* 点位未配置 */
        else
            ++readyCount;
    }
    st->m_ready = (readyCount > 0);
    XPrintf("HsfServerIoPump: 装配 %d 台，其中 %d 台点位齐全（启用=%s）\n",
            st->m_count, readyCount, st->m_enabled ? "是" : "否");
    return true;
}

/* ==================== 动作 ==================== */

bool HsfServerIoPump_setOutput(HsfServerIoPump* st, int index, bool on,
                               char* outErr, int cap)
{
    HsfServerIoPumpItem* it;

    if (outErr && cap > 0) outErr[0] = '\0';
    if (!st || !st->m_enabled) {
        if (outErr) snprintf(outErr, (size_t)cap, "%s",
                             "\xE8\xBF\x9B\xE6\xB0\xB4\xE6\xB3\xB5\xE7\xBB\x84"
                             "\xE6\x9C\xAA\xE5\x90\xAF\xE7\x94\xA8"); /* 进水泵组未启用 */
        return false;
    }
    if (index < 0 || index >= st->m_count) {
        if (outErr) snprintf(outErr, (size_t)cap,
                             "\xE6\xB3\xB5\xE5\x8F\xB7\xE8\xB6\x8A\xE7\x95\x8C"); /* 泵号越界 */
        return false;
    }
    it = &st->m_items[index];
    if (it->m_do < 0) {
        if (outErr) snprintf(outErr, (size_t)cap, "%s",
                             "\xE7\x82\xB9\xE4\xBD\x8D\xE6\x9C\xAA\xE9\x85\x8D\xE7\xBD\xAE"
                             "\xEF\xBC\x8C\xE6\x97\xA0\xE6\xB3\x95\xE6\x8E\xA7\xE5\x88\xB6"); /* 点位未配置，无法控制 */
        return false;
    }
    /* [模块离线 ⇒ 明确拒绝] 与阀同款：离线时放行会先显示"已启动"再被
     * 回显打回——界面撒谎比拒绝更危险。 */
    if (!it->m_online) {
        if (outErr) snprintf(outErr, (size_t)cap, "%s",
                             "\xE6\x95\xB0\xE5\xAD\x97\xE9\x87\x8F\xE6\xA8\xA1\xE5\x9D\x97"
                             "\xE7\xA6\xBB\xE7\xBA\xBF\xEF\xBC\x8C\xE6\x97\xA0\xE6\xB3\x95"
                             "\xE6\x8E\xA7\xE5\x88\xB6"); /* 数字量模块离线，无法控制 */
        return false;
    }
    /* [幂等] 已是该状态 → 不写（避免无谓的继电器动作与总线占用）。 */
    if (it->m_valid && it->m_on == on) {
        if (outErr) snprintf(outErr, (size_t)cap, "%s",
                             "\xE5\xB7\xB2\xE5\xA4\x84\xE4\xBA\x8E\xE8\xAF\xA5\xE7\x8A\xB6\xE6\x80\x81"); /* 已处于该状态 */
        return true;
    }
    /* 命令状态当场落定（泵没有"在途"过程）；knownDo 同步成目标值，
     * 否则下一拍 poll 会把自己的写当成"外部改动"。 */
    it->m_on = on;
    it->m_knownDo = on;
    it->m_knownDoSeeded = true;
    it->m_text[0] = '\0';
    return true;
}

/* ==================== 反馈更新 ==================== */

void HsfServerIoPump_poll(HsfServerIoPump* st, uint32_t doMask, bool doValid,
                          bool doFresh)
{
    int i;

    if (!st) return;
    for (i = 0; i < st->m_count; ++i) {
        HsfServerIoPumpItem* it = &st->m_items[i];

        if (it->m_do < 0) {
            it->m_valid = false;
            it->m_online = false;
            snprintf(it->m_text, sizeof(it->m_text),
                     "\xE7\x82\xB9\xE4\xBD\x8D\xE6\x9C\xAA\xE9\x85\x8D\xE7\xBD\xAE"); /* 点位未配置 */
            continue;
        }
        it->m_online = doFresh && doValid;
        if (!doFresh) {
            it->m_valid = false;
            snprintf(it->m_text, sizeof(it->m_text),
                     "\xE6\x95\xB0\xE5\xAD\x97\xE9\x87\x8F\xE6\xA8\xA1\xE5\x9D\x97"
                     "\xE6\x95\xB0\xE6\x8D\xAE\xE4\xB8\x8D\xE6\x96\xB0\xE9\xB2\x9C"); /* 数字量模块数据不新鲜 */
            continue;
        }
        it->m_valid = doValid;
        if (!doValid) {
            /* 从没读到过回显（模块在但本拍无有效值）：保留上一状态，
             * 只标 valid=false。绝不在这里改状态。 */
            continue;
        }
        {
            bool actual = (doMask & (1u << it->m_do)) != 0;
            if (!it->m_knownDoSeeded) {
                /* 首拍校准：回显第一次可信，直接播种（不判改动）。 */
                it->m_knownDoSeeded = true;
                it->m_knownDo = actual;
                it->m_on = actual;
                it->m_text[0] = '\0';
                continue;
            }
            if (actual != it->m_knownDo) {
                /* [外部改动接管] 以实际输出为准（本层不打印——调用方比对
                 * on 变化自行留痕；文本给操作员一个可读结论）。 */
                it->m_knownDo = actual;
                it->m_on = actual;
                snprintf(it->m_text, sizeof(it->m_text),
                         "\xE6\xA3\x80\xE6\xB5\x8B\xE5\x88\xB0\xE8\xBE\x93\xE5\x87\xBA"
                         "\xE8\xA2\xAB\xE5\xA4\x96\xE9\x83\xA8\xE6\x94\xB9\xE5\x8A\xA8"
                         "\xEF\xBC\x8C\xE5\xB7\xB2\xE4\xBB\xA5\xE5\xAE\x9E\xE9\x99\x85"
                         "\xE4\xB8\xBA\xE5\x87\x86"); /* 检测到输出被外部改动，已以实际为准 */
            } else if (it->m_on == actual && it->m_text[0] != '\0') {
                /* 状态一致后清掉一次性说明（避免告警文案常驻）。 */
                it->m_text[0] = '\0';
            }
        }
    }
}

/* ==================== 查询 ==================== */

int HsfServerIoPump_count(const HsfServerIoPump* st)
{
    return st ? st->m_count : 0;
}

bool HsfServerIoPump_ready(const HsfServerIoPump* st)
{
    return st && st->m_ready;
}

const HsfServerIoPumpItem* HsfServerIoPump_item(const HsfServerIoPump* st,
                                                int index)
{
    if (!st || index < 0 || index >= st->m_count) return NULL;
    return &st->m_items[index];
}
