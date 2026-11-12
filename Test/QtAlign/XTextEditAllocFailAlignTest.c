/**
 * @file       XTextEditAllocFailAlignTest.c
 * @brief      P0-E1 失败分支的确定性分配故障注入（非真实 OOM）。
 * @details    `xte_htmlToPlainAlloc`（XTextEdit.c:1624-1640）按 strlen(html)
 *             动态分配纯文本降级缓冲；分配失败时显式记日志并返回 NULL，
 *             `setHtml/append/insertHtml` 三个入口共用一个早退分支。此前
 *             只有成功路径的长输入不截断用例，失败分支仅经代码审读。
 *
 *             本文件用框架**公开**的分配器换装接口
 *             `XMemory_method(XMEMORY_TYPE_SYSTEM)` /
 *             `XMemory_setMallocMethod`（XMemory.h:93-121）临时装一个
 *             「按目标尺寸只失败一次」的选择性桩：命中
 *             `XMalloc_System(XStrlen(html)+1)` 时返回 NULL，其余申请一律
 *             透传给原方法（记账仍由原包装器维护，泄漏门读数不变）。
 *             配合 `XPrintf_outputPush/Pop` 捕获失败日志，断言三个入口在
 *             失败后均**不改动编辑缓冲**、且日志含输入字节数。
 *
 *             为什么不触发真实 OOM：真实 OOM 不可控、会污染同进程其它
 *             用例，且失败点随机；这里由桩返回值精确命中目标分支。
 *             为什么不加生产侧自测钩子：AGENTS.md「临时诊断探针必须零
 *             残留」——本方案生产代码零改动，无 env 门控残留。
 *
 * @note       全局槽是进程级状态：换装期间不使用任何 XQ_REQUIRE（会在
 *             还原前 return），所有退出路径都先无条件还原再返回；
 *             QtAlign 运行器单线程顺序执行，泄漏门复核重跑亦幂等。
 *             XPRINTF_OUTPUT_REDIRECT_ON=0 时整组用例不编译（不伪造断言）。
 * @author     XinYueC 团队
 */
#include "xq_align.h"

#include "XTextEdit.h"
#include "XPlainTextEdit.h"
#include "XString.h"
#include "XStringUtils.h"
#include "XMemory.h"
#include "XPrintf.h"

#include <string.h>

#if XWIDGET_ON && XABSTRACTSCROLLAREA_ON && XPLAINTEXTEDIT_ON && XTEXTEDIT_ON \
    && XPRINTF_OUTPUT_REDIRECT_ON

/* ==================== 选择性分配失败桩 ==================== */

static MallocMethod s_xteSavedMalloc; /**< 换装前的 SYSTEM 槽方法。 */
static size_t s_xteFailSize;          /**< 只让这一尺寸的申请失败一次。 */
static int s_xteFailHits;             /**< 实际命中次数（应为 1）。 */
static int s_xteArmed;                /**< 尚未命中时为真。 */

static void* xte_failOnceMalloc(size_t size)
{
    /* 目标尺寸取奇数且远离结构尺寸；前置富文本解析的缓冲按 XChar
     * 元素数申请（恒为 2 的倍数），不会误命中。命中一次即解除：后续
     * 同尺寸申请（含 XPrintf 日志缓冲）一律透传。 */
    if (s_xteArmed && size == s_xteFailSize) {
        ++s_xteFailHits;
        s_xteArmed = 0;
        return NULL;
    }
    /* 透传原方法 ⇒ 成功分配的堆记账仍由 xmemory_system_malloc 维护。 */
    return s_xteSavedMalloc ? s_xteSavedMalloc(size) : NULL;
}

/* ==================== 日志捕获（固定缓冲，回调内零分配） ==================== */

typedef struct XTEAllocFailCapture
{
    char buf[512];
    size_t len;
} XTEAllocFailCapture;

static int64_t xte_captureWrite(void* userData, const char* data, size_t size)
{
    XTEAllocFailCapture* cap = (XTEAllocFailCapture*)userData;
    size_t n;
    if (!cap || !data) return -1;
    n = size;
    if (cap->len + n >= sizeof(cap->buf)) n = sizeof(cap->buf) - 1 - cap->len;
    if (n > 0) {
        XMemcpy(cap->buf + cap->len, data, n);
        cap->len += n;
    }
    cap->buf[cap->len] = '\0';
    return (int64_t)size; /* 消费全部字节；截断只为守住固定缓冲。 */
}

/* ==================== 场景 ==================== */

typedef enum XTEAllocFailEntry
{
    XTEAllocFail_SetHtml = 0,
    XTEAllocFail_Append,
    XTEAllocFail_InsertHtml
} XTEAllocFailEntry;

/** @brief 一次「装桩 → 调入口 → 还原 → 断言」的完整故障注入。 */
static void xte_runAllocFailure(XTEAllocFailEntry entry, const char* html,
                                int expectedLen)
{
    XTextEdit* te;
    XPrintfOutputScope scope;
    XTEAllocFailCapture cap;
    char* plain;
    bool pushed;
    size_t len;

    te = XTextEdit_create(NULL, 0);
    XQ_REQUIRE(te != NULL);
    XTextEdit_setPlainText(te, "SENTINEL");
    len = XStrlen(html);

    XMemset(&scope, 0, sizeof(scope));
    XMemset(&cap, 0, sizeof(cap));
    /* push 必须在换装之前：首次建立 stdout relay 会走系统槽分配。 */
    pushed = XPrintf_outputPush(&scope, xte_captureWrite, &cap);
    XQ_REQUIRE(pushed);

    s_xteSavedMalloc = XMemory_method(XMEMORY_TYPE_SYSTEM)->malloc;
    s_xteFailSize = len + 1; /* xte_htmlToPlainAlloc 的精确申请尺寸。 */
    s_xteFailHits = 0;
    s_xteArmed = 1;
    XMemory_setMallocMethod(xte_failOnceMalloc, XMEMORY_TYPE_SYSTEM);

    switch (entry) {
    case XTEAllocFail_SetHtml:
        XTextEdit_setHtml(te, html);
        break;
    case XTEAllocFail_Append:
        XTextEdit_append(te, html);
        break;
    case XTEAllocFail_InsertHtml:
        XTextEdit_insertHtml(te, html);
        break;
    default:
        break;
    }

    /* 单一出口：无条件先还原全局槽，再收重定向；此后才允许断言/取数
     * （断言失败时 xq_check_impl 可能走 XPrintf，不能让桩继续生效）。 */
    XMemory_setMallocMethod(s_xteSavedMalloc, XMEMORY_TYPE_SYSTEM);
    s_xteArmed = 0;
    XPrintf_outputPop(&scope);

    XQ_EQ_I64(s_xteFailHits, 1); /* 目标分配确实被注入失败。 */
    XQ_CHECK(cap.len > 0);
    XQ_CHECK(strstr(cap.buf, "[XTextEdit] HTML 降级纯文本缓冲分配失败")
             != NULL);
    {
        char expect[32];
        XSnprintf(expect, sizeof(expect), "%d", expectedLen);
        XQ_CHECK(strstr(cap.buf, expect) != NULL);
    }

    /* 失败不得退化成「内容变短」：编辑缓冲保留哨兵，格式态不翻转。
     * 此处全局槽/重定向均已还原，取堆副本再释放不影响泄漏门。 */
    plain = XPlainTextEdit_toPlainText(te->m_editor);
    XQ_CHECK_PTR(plain);
    if (plain) {
        XQ_EQ_STR(plain, "SENTINEL");
        XFree_System(plain);
    }
    XQ_CHECK(!XTextEdit_isBold(te));

    XClassDelete((XClass*)te);
}

/** @brief 三个入口共用同一失败分支：显式日志 + 不动编辑缓冲。
 *
 *  HTML 长度取 5008（`<b>` + 5001 个 `a` + `</b>`）⇒ 目标申请 5009
 *  字节（奇数且远离 XTDBlock/XString 等结构尺寸），避免前置富文本解析
 *  的分配误命中同一尺寸；桩只失败一次也保证日志缓冲不被吞掉。 */
XQ_TEST(XTextEdit, htmlAllocFailureKeepsBuffer)
{
    const int kBodyLen = 5001;
    char* html;
    int i;
    size_t off;

    html = (char*)XMalloc_System((size_t)kBodyLen + 16);
    XQ_REQUIRE(html != NULL);
    off = 0;
    html[off++] = '<'; html[off++] = 'b'; html[off++] = '>';
    for (i = 0; i < kBodyLen; ++i) html[off++] = 'a';
    html[off++] = '<'; html[off++] = '/'; html[off++] = 'b'; html[off++] = '>';
    html[off] = '\0';
    XQ_EQ_SIZE(XStrlen(html), (size_t)(kBodyLen + 7));

    xte_runAllocFailure(XTEAllocFail_SetHtml, html, kBodyLen + 7);
    xte_runAllocFailure(XTEAllocFail_Append, html, kBodyLen + 7);
    xte_runAllocFailure(XTEAllocFail_InsertHtml, html, kBodyLen + 7);

    XFree_System(html);
}

#endif /* 模块裁剪守卫 */
