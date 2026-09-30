#include "XKeyboardTest.h"
#include "XGuiConfig.h" /* 门控宏定义源：必须在 :3 的 #if 之前（原缺陷——
                            宏未定义时 #if 恒 0，真实测试体整段被编译
                            出，套件经静默 stub 恒绿，断言全部失效）。 */

#if XWIDGET_ON && XKEYBOARD_ON

#include "XVirtualKeyboard.h"
#include "XWidget_Protected.h"
#include "XObject.h"
#include "XEvent.h"
#include "XVarList.h"
#include "XMemory.h"
#include <stdio.h>
#include <string.h>
#if XKEYBOARD_IME_ON
#include "XPinyinEngine.h"
#include "XPinyinTable.h"
#endif /* XKEYBOARD_IME_ON */
#if XKEYBOARD_IME_PHRASE_ON
#include "XPinyinPhrase.h"
#endif /* XKEYBOARD_IME_PHRASE_ON */
#if XLINEEDIT_ON
#include "XLineEdit.h"
#endif /* XLINEEDIT_ON */
#if XVIRTUALKEYBOARD_ON
/* 虚拟键盘框架（Src/XGui/VirtualKeyboard，设计 apiMapping#1-5/7）：
 * 门控未定义时（框架落地前）整段编译出，既有用例保持原样全绿；框架
 * 落地（XGuiConfig.h 注册 XVIRTUALKEYBOARD_ON）后本段激活收口新语义。 */
#include "XVirtualKeyboardInputContext.h"
#include "XVirtualKeyboardInputEngine.h"
#include "XVirtualKeyboardSelectionListModel.h"
#include "XVirtualKeyboardSettings.h"
#include "XGuiApplication.h"
#include "XWindowSystemInterface.h" /* ⑦.1b 按下位置驱动：顶层桥注入鼠标 PRESS。 */
#include "XStyleHints.h"
#include "XString.h"
#include "XVariant.h"
#include "XDateTime.h" /* XDateTime_currentMSecsSinceEpoch（长按重复节拍计时）。 */
#endif /* XVIRTUALKEYBOARD_ON */

/* ==================== 断言与事件工具 ==================== */

static int xkb_failures = 0;

/* 信号计数槽（ready/cancel）。 */
static int xkb_readyCount = 0;
static int xkb_cancelCount = 0;
static int xkb_activatedCount = 0;
#if XLINEEDIT_ON
static int xkb_returnPressedCount = 0;
#endif

static void xkb_readySlot(XObject* receiver, XVarList* args)
{
    (void)receiver;
    (void)args;
    ++xkb_readyCount;
}
static void xkb_cancelSlot(XObject* receiver, XVarList* args)
{
    (void)receiver;
    (void)args;
    ++xkb_cancelCount;
}
static void xkb_activatedSlot(XObject* receiver, XVarList* args)
{
    (void)receiver;
    (void)args;
    ++xkb_activatedCount;
}
#if XLINEEDIT_ON
static void xkb_returnPressedSlot(XObject* receiver, XVarList* args)
{
    (void)receiver;
    (void)args;
    ++xkb_returnPressedCount;
}
#endif

static void xkb_expect(bool cond, const char* what)
{
    if (!cond) {
        fprintf(stderr, "[XKB-FAIL] %s\n", what ? what : "");
        ++xkb_failures;
    }
}

/** @brief 按标签找按钮索引（未找到返回 -1）。 */
static int xkb_findButton(XVirtualKeyboard* kb, const char* label)
{
    uint32_t i;
    for (i = 0; i < XVirtualKeyboard_buttonCount(kb); ++i) {
        const char* t = XVirtualKeyboard_buttonText(kb, i);
        if (t && strcmp(t, label) == 0) return (int)i;
    }
    return -1;
}

#if XKEYBOARD_IME_ON
/** @brief 向状态机逐字母喂串（⑥ 段组串序列工具）。 */
static void xkb_imeFeedStr(XPinyinEngine* ime, const char* letters)
{
    int i;
    for (i = 0; letters[i] != '\0'; ++i)
        XPinyinEngine_feedLetter(ime, letters[i]);
}
#endif /* XKEYBOARD_IME_ON */

/** @brief 组串缓冲读取垫片（既有用例随迁：断言语义不变、路径更换）。
 * @details 面板 m_ime 成员已随框架化删除（apiMapping#8，XVirtualKeyboard.h
 *          现状）：组串状态镜像=context.preeditText()——拼音插件每
 *          次组串变化经 setPreeditText 同步（Qt pinyin 经
 *          inputContext->setPreeditText 写串同型，apiMapping#13）。
 *          框架未开（XVIRTUALKEYBOARD_ON=0）时面板 IME API 整体裁剪
 *          （XVirtualKeyboard.h 现状同门控），④/⑤ 键盘集成段一并编译出，
 *          本垫片无消费者。preeditText 返回新建 XString*（真头文件
 *          契约）：取 UTF-8 拷入测试内静态缓冲（组串容量 15 字节，
 *          32 足够）后立即释放。 */
#if XKEYBOARD_IME_ON && XVIRTUALKEYBOARD_ON
static const char* xkb_imeBuffer(const XVirtualKeyboard* kb)
{
    static char s_buf[32];
    XString* s = XVirtualKeyboardInputContext_preeditText(
        XVirtualKeyboardInputContext_instance());
    const char* u = s ? XString_toUtf8(s) : NULL;
    (void)kb;
    if (u) {
        size_t i = 0;
        for (; i + 1 < sizeof(s_buf) && u[i] != '\0'; ++i)
            s_buf[i] = u[i];
        s_buf[i] = '\0';
    } else {
        s_buf[0] = '\0';
    }
    if (s) XString_delete_base((XClass*)s);
    return s_buf;
}
#endif /* XKEYBOARD_IME_ON && XVIRTUALKEYBOARD_ON */

/** @brief 向键盘直发合成鼠标按压（可选补释放；XObject_event_base 经
 *         vtable 虚槽分派，与真实输入同路径且不涉过滤器）。 */
static void xkb_clickAt(XVirtualKeyboard* kb, int idx, bool releaseToo)
{
    XMouseEvent me;
    XPoint pos;
    if (idx < 0 || (uint32_t)idx >= XVirtualKeyboard_buttonCount(kb)) return;
    pos.x = kb->m_keyRects[idx].x + kb->m_keyRects[idx].width / 2;
    pos.y = kb->m_keyRects[idx].y + kb->m_keyRects[idx].height / 2;
    XMouseEvent_init(&me, XEVENT_TYPE_MOUSE_BUTTON_PRESS,
                     XMouseButton_LeftButton, 0, pos);
    XObject_event_base((XObject*)kb, (XEvent*)&me);
    if (releaseToo) {
        XMouseEvent_init(&me, XEVENT_TYPE_MOUSE_BUTTON_RELEASE,
                         XMouseButton_LeftButton, 0, pos);
        XObject_event_base((XObject*)kb, (XEvent*)&me);
    }
}

/** @brief 自定义布局表（setMap 借用语义验证用；须存活到下次 setMap）。 */
static const char* const s_xkbCustomMap[] = { "x", "\n", "y", NULL };
static const XKeyboardButtonCtrl s_xkbCustomCtrl[] = { 1, 2 };
/** @brief "" 终止习惯表（兼容 LVGL 习惯）。 */
static const char* const s_xkbEmptyStopMap[] = { "a", "", "b", NULL };
/** @brief 超上限表（65 按钮 → 整体拒绝）。 */
static const char* const s_xkbOverflowMap[] = {
    "0", "1", "2", "3", "4", "5", "6", "7", "8", "9",
    "0", "1", "2", "3", "4", "5", "6", "7", "8", "9",
    "0", "1", "2", "3", "4", "5", "6", "7", "8", "9",
    "0", "1", "2", "3", "4", "5", "6", "7", "8", "9",
    "0", "1", "2", "3", "4", "5", "6", "7", "8", "9",
    "0", "1", "2", "3", "4", "5", "6", "7", "8", "9",
    "0", "1", "2", "3", "4", "Z", NULL
};

/* ==================== ⑥e 词库二进制格式 V3 辅助（PHRASE_ON 门控内）
   ==================== */

/** @brief 音节三元组串（空格分隔）解析为 id 序列（抽查表用）。
 * @param triple 「s0 s1 s2」形式借用串；须来自 ⑥e 抽查表。
 * @param outIds 调用方提供存储空间；成功时接收 3 个音节 id。
 * @return 全部音节合法返回 true；否则 false（抽查表与音节表脱节时
 *         由调用方断言规模锁定兜住，不静默跳过查询）。 */
static bool imeParseSyllableTriple(const char* triple, uint16_t* outIds)
{
    char buf[24];
    int n = 0;
    int i = 0;
    while (triple[n] != '\0')
    {
        if (n >= (int)sizeof(buf) - 1)
            return false;
        buf[n] = triple[n];
        ++n;
    }
    buf[n] = '\0';
    /* 依空格切 3 段。 */
    {
        int p = 0; /* 游标跨段持续推进（不得在段循环内重置）。 */
        for (i = 0; i < 3; ++i)
        {
            char token[8];
            int t = 0;
            while (p < n && buf[p] == ' ')
                ++p;
            while (p < n && buf[p] != ' ')
            {
                if (t >= (int)sizeof(token) - 1)
                    return false;
                token[t++] = buf[p++];
            }
            token[t] = '\0';
            if (t == 0 || !XPinyinTable_syllableIdOf(token, &outIds[i]))
                return false;
            while (p < n && buf[p] == ' ')
                ++p;
            if (i < 2 && p >= n)
                return false; /* 段数不足。 */
        }
    }
    return true;
}

/** @brief 黄金摘要扫描（txt/bin 双侧共用）：412×412 全双音节键 +
 *         全部真实三音节组抽样，find 结果滚入 64 位 FNV-1a 摘要。
 * @param outHash 调用方提供存储空间；成功时接收 32 位摘要（64 位折
 *        叠低/高异或）。
 * @param outCount 调用方提供存储空间；接收总命中条目数（含三音节抽
 *        样）。
 * @param triples 三音节组抽查表借用指针（file scope 常量）。
 * @param tripleCount 抽查表条数。
 * @param niId/haoId/shiId/xiId/anId 预解析音节 id（合法性由断言链保
 *        证，此处仅传参）。
 * @return 无。要求调用前已 setPath+reload 完成一侧资产装载。 */
static void imeGoldenScan(uint32_t* outHash, uint32_t* outCount,
                          const char* const* triples, int tripleCount,
                          uint16_t niId, uint16_t haoId, uint16_t shiId,
                          uint16_t xiId, uint16_t anId)
{
    uint64_t hash = 0xCBF29CE484222325ull;
    const uint64_t prime = 0x100000001B3ull;
    uint16_t sylN = XPinyinTable_syllableCount();
    uint16_t a;
    uint16_t b;
    uint32_t i;
    (void)niId; (void)haoId; (void)shiId; (void)xiId; (void)anId;
    *outCount = 0;
    for (a = 0; a < sylN; ++a)
    {
        for (b = 0; b < sylN; ++b)
        {
            uint16_t key[2];
            const XPinyinPhraseEntry* pe = NULL;
            uint16_t pc = 0;
            uint32_t q;
            key[0] = a;
            key[1] = b;
            if (!XPinyinPhrase_find(key, 2, &pe, &pc))
                continue;
            hash ^= (uint64_t)pc;
            hash *= prime;
            for (q = 0; q < pc; ++q)
            {
                const char* w = pe[q].m_utf8;
                while (*w)
                {
                    hash ^= (uint64_t)(unsigned char)*w;
                    hash *= prime;
                    ++w;
                }
                hash ^= 0u; /* 词 NUL 分隔。 */
                hash *= prime;
                hash ^= (uint64_t)pe[q].m_rank;
                hash *= prime;
                hash ^= (uint64_t)pe[q].m_syllableCount;
                hash *= prime;
            }
            *outCount += (uint32_t)pc;
        }
    }
    for (i = 0; i < (uint32_t)tripleCount; ++i)
    {
        uint16_t ids[3];
        uint16_t key[3];
        const XPinyinPhraseEntry* pe = NULL;
        uint16_t pc = 0;
        uint32_t q;
        if (!imeParseSyllableTriple(triples[i], ids))
            continue;
        key[0] = ids[0];
        key[1] = ids[1];
        key[2] = ids[2];
        if (!XPinyinPhrase_find(key, 3, &pe, &pc))
            continue;
        hash ^= (uint64_t)pc;
        hash *= prime;
        for (q = 0; q < pc; ++q)
        {
            const char* w = pe[q].m_utf8;
            while (*w)
            {
                hash ^= (uint64_t)(unsigned char)*w;
                hash *= prime;
                ++w;
            }
            hash ^= 0u;
            hash *= prime;
            hash ^= (uint64_t)pe[q].m_rank;
            hash *= prime;
            hash ^= (uint64_t)pe[q].m_syllableCount;
            hash *= prime;
        }
        *outCount += (uint32_t)pc;
    }
    *outHash = (uint32_t)(hash ^ (hash >> 32));
}

/** @brief 抽查组显式逐条比对（ni hao/shi shi/xi an）：对当前已装载资
 *         产查询并把结果摘要与静态锁存比对——首查（txt 侧）定锁存，
 *         二查（bin 侧）断言逐位一致；两查之间调用方须先
 *         resetComposition+setPath+reload 换资产。
 * @param niId/haoId/shiId/xiId/anId 预解析音节 id。
 * @return 无。 */
static void imeGoldenSpotKeys(uint16_t niId, uint16_t haoId, uint16_t shiId,
                              uint16_t xiId, uint16_t anId)
{
    static uint32_t s_latched = 0;
    static bool s_latchedValid = false;
    static const uint16_t* s_keySpec[3];
    uint16_t k0[2];
    uint16_t k1[2];
    uint16_t k2[2];
    uint64_t h = 0xCBF29CE484222325ull;
    const uint64_t prime = 0x100000001B3ull;
    uint32_t spot;
    int ki;
    k0[0] = niId;  k0[1] = haoId;
    k1[0] = shiId; k1[1] = shiId;
    k2[0] = xiId;  k2[1] = anId;
    {
        uint16_t* ks[3] = { k0, k1, k2 };
        for (ki = 0; ki < 3; ++ki)
        {
            const XPinyinPhraseEntry* pe = NULL;
            uint16_t pc = 0;
            uint32_t q;
            xkb_expect(XPinyinPhrase_find(ks[ki], 2, &pe, &pc) &&
                           pc >= 1 && pe[0].m_rank == 1,
                       "⑥e 抽查组命中且首条 rank=1（ni hao/shi shi/xi an）");
            h ^= (uint64_t)pc;
            h *= prime;
            for (q = 0; q < pc; ++q)
            {
                const char* w = pe[q].m_utf8;
                while (*w)
                {
                    h ^= (uint64_t)(unsigned char)*w;
                    h *= prime;
                    ++w;
                }
                h ^= 0u;
                h *= prime;
                h ^= (uint64_t)pe[q].m_rank;
                h *= prime;
                h ^= (uint64_t)pe[q].m_syllableCount;
                h *= prime;
            }
        }
    }
    spot = (uint32_t)(h ^ (h >> 32));
    if (!s_latchedValid)
    {
        s_latched = spot; /* 首查（txt）定锁存。 */
        s_latchedValid = true;
    }
    else
    {
        xkb_expect(spot == s_latched,
                   "⑥e 抽查组 bin 与 txt 逐位一致（显式比对）");
    }
}

#if XVIRTUALKEYBOARD_ON
/* 长按重复节拍宏（XVirtualKeyboardInputEngine.h #ifndef 默认 600/50；
 * 此处兜底定义仅为编译期锚常量可求值——头文件已定义时以头文件为准）。
 * 默认值锚在 ⑦.4 运行期断言。 */
#ifndef XVIRTUALKEYBOARD_REPEAT_FIRST_MS
#define XVIRTUALKEYBOARD_REPEAT_FIRST_MS 600
#endif
#ifndef XVIRTUALKEYBOARD_REPEAT_MS
#define XVIRTUALKEYBOARD_REPEAT_MS 50
#endif

/** @brief 直发一次守护轮询 tick：合成 XTIMER 类型事件按 m_guardTimer
 *         id 命中键盘 timerEvent 分派（与真实定时器事件同 vtable 路
 *         径，不依赖 200ms 周期与事件调度器；守护已停时幂等无操作）。 */
static void xkb_pumpGuardTick(XVirtualKeyboard* kb)
{
    XTimerEvent te;
    if (!kb || kb->m_guardTimer == XTIMER_INVALID_ID) return;
    memset(&te, 0, sizeof(te));
    XEvent_init(&te.m_base, XEVENT_TYPE_TIMER);
    te.timerId = kb->m_guardTimer;
    XObject_event_base((XObject*)kb, (XEvent*)&te);
}

/** @brief 顶层桥注入一次完整点击（PRESS+RELEASE 同点，经
 *         XWidget_dispatchPointerEvent 真实派发链）。必须成对：键盘面
 *         板 mousePress 即 grabMouse（XVirtualKeyboard.c VXKeyboard_
 *         mousePressEvent），只发 PRESS 不补 RELEASE 时抓取滞留，后续
 *         顶层点击全被 grab 分支重定向到面板——真指针恒有成对释放，
 *         测试同口径。 */
static void xkb_windowClick(XWidget* win, int x, int y)
{
    XPoint pos;
    XWindow* window;
    if (!win) return;
    window = XWidget_windowHandle(win);
    if (!window) return;
    pos.x = (short)x;
    pos.y = (short)y;
    XWindowSystemInterface_handleMouseEvent(
        window, XEVENT_TYPE_MOUSE_BUTTON_PRESS,
        XMouseButton_LeftButton, XMouseButton_LeftButton,
        (XKeyboardModifiers)0, pos);
    XWindowSystemInterface_handleMouseEvent(
        window, XEVENT_TYPE_MOUSE_BUTTON_RELEASE,
        XMouseButton_LeftButton, (XMouseButton)0,
        (XKeyboardModifiers)0, pos);
}

/** @brief 仅补发释放事件（长按保持后的收尾；坐标取键中心，与
 *         xkb_clickAt 同口径）。 */
static void xkb_clickAtReleaseOnly(XVirtualKeyboard* kb, int idx)
{
    XMouseEvent me;
    XPoint pos;
    if (idx < 0 || (uint32_t)idx >= XVirtualKeyboard_buttonCount(kb)) return;
    pos.x = kb->m_keyRects[idx].x + kb->m_keyRects[idx].width / 2;
    pos.y = kb->m_keyRects[idx].y + kb->m_keyRects[idx].height / 2;
    XMouseEvent_init(&me, XEVENT_TYPE_MOUSE_BUTTON_RELEASE,
                     XMouseButton_LeftButton, 0, pos);
    XObject_event_base((XObject*)kb, (XEvent*)&me);
}

/** @brief 忙等至距 startMs 已过 ms（engine 重复节拍真时钟注入；
 *         计时源=XDateTime_currentMSecsSinceEpoch，全库统一单调毫
 *         秒口径）。 */
static void xkb_waitMsSince(int64_t startMs, int64_t ms)
{
    while (XDateTime_currentMSecsSinceEpoch() - startMs < ms)
        ;
}

/** @brief preeditText 往返断言垫片：真 API 返回新建 XString*（调用方
 *         释放），取 UTF-8 与期望串比较后释放。 */
static int xkb_preeditEq(const XVirtualKeyboardInputContext* ctx,
                         const char* expect)
{
    XString* s = XVirtualKeyboardInputContext_preeditText(ctx);
    const char* u = s ? XString_toUtf8(s) : NULL;
    /* XString_toUtf8 对空串返回 NULL（XString 层约定）：期望为空串时
     * NULL 即语义等价（组串确已清空），不得误判为失败。 */
    int ok = (expect && expect[0] == '\0')
                 ? (u == NULL || u[0] == '\0')
                 : (u != NULL && strcmp(u, expect) == 0);
    if (s) XString_delete_base((XClass*)s);
    return ok;
}
#endif /* XVIRTUALKEYBOARD_ON */

bool XKeyboardTest_runAll(void)
{
    XVirtualKeyboard* kb = XVirtualKeyboard_create(NULL, 0);

    xkb_readyCount = 0;
    xkb_cancelCount = 0;
    xkb_activatedCount = 0;

    /* 1. 默认状态（mode=TextLower / popovers=false / autoPopup=true /
     *    USER_1..4 槽位回落小写表 / 布局 40 键）。 */
    xkb_expect(XVirtualKeyboard_mode(kb) == XKeyboardMode_TextLower, "默认小写模式");
    xkb_expect(!XVirtualKeyboard_popovers(kb), "默认关气泡");
    xkb_expect(XVirtualKeyboard_autoPopup(kb), "默认开自动弹出");
    xkb_expect(!XVirtualKeyboard_popupVisible(kb), "默认弹层不可见");
    xkb_expect(XVirtualKeyboard_textArea(kb) == NULL, "默认未绑定目标");
    xkb_expect(XVirtualKeyboard_buttonCount(kb) == 40, "小写布局 40 键");
    xkb_expect(XVirtualKeyboard_selectedButton(kb) == XKEYBOARD_BUTTON_NONE,
               "默认无选中按钮");
    xkb_expect(XVirtualKeyboard_buttonText(kb, 0) != NULL &&
               strcmp(XVirtualKeyboard_buttonText(kb, 0), "1#") == 0,
               "按钮 0 为 1# 切换键");
    XVirtualKeyboard_setMode(kb, XKeyboardMode_User1);
    xkb_expect(XVirtualKeyboard_buttonCount(kb) == 40 &&
               strcmp(XVirtualKeyboard_buttonText(kb, 0), "1#") == 0,
               "USER_1 槽位回落小写映射");
    XVirtualKeyboard_setMode(kb, XKeyboardMode_TextLower);

    /* 1.2 多行布局不变式（rowStart 差一回归锁）：经公开几何断言——
     * 40 键分 4 行、各行键数 12/11/12/5、各行首键标签依次为
     * 1#/ABC/_/收起（rowStart[r] 为第 r 行首键索引的几何投影；原缺陷
     * 下中间行起点漏登、末行起点为栈垃圾，行分组与键数必然错乱）。 */
    {
        uint32_t i;
        int rows = 0;
        int rowBegin[8] = {0};
        int rowKeyCount[8] = {0};
        const char* rowLabel;
        /* 行数记录封顶 8 行（回归 produced 病态行分组时断言失败即可，
           记录本身不越界）。 */
        for (i = 0; i < XVirtualKeyboard_buttonCount(kb) && rows < 8; ++i) {
            if (i == 0 || kb->m_keyRects[i].y != kb->m_keyRects[i - 1].y) {
                rowBegin[rows] = (int)i;
                rowKeyCount[rows] = 1;
                ++rows;
            } else {
                ++rowKeyCount[rows - 1];
            }
        }
        xkb_expect(rows == 4, "小写布局 4 行");
        xkb_expect(rows == 4 && rowKeyCount[0] == 12 &&
                       rowKeyCount[1] == 11 && rowKeyCount[2] == 12 &&
                       rowKeyCount[3] == 5,
                   "各行键数 12/11/12/5（rowStart 不变式）");
        xkb_expect(rowBegin[0] == 0, "行 0 从按钮 0 起");
        rowLabel = XVirtualKeyboard_buttonText(kb, (uint32_t)rowBegin[1]);
        xkb_expect(rows == 4 && rowLabel &&
                       strcmp(rowLabel, XKEYBOARD_LBL_UPPER) == 0,
                   "行 1 首键为 ABC（rowStart[1] 落在换行符之后）");
        rowLabel = XVirtualKeyboard_buttonText(kb, (uint32_t)rowBegin[2]);
        xkb_expect(rows == 4 && rowLabel && strcmp(rowLabel, "_") == 0,
                   "行 2 首键为 _");
        rowLabel = XVirtualKeyboard_buttonText(kb, (uint32_t)rowBegin[3]);
        xkb_expect(rows == 4 && rowLabel &&
                       strcmp(rowLabel, XKEYBOARD_LBL_DISMISS) == 0,
                   "行 3 首键为收起键（rowStart[3] 显式登记非栈垃圾）");
    }

    /* 1.5 信号计数连接（经纯 ID getter；后续按键断言依赖计数，须在
     * 触发断言之前接好）。 */
    XObject_connect_1((XObject*)kb, (size_t)XVirtualKeyboard_ready_signal(NULL),
                      (XObject*)kb, xkb_readySlot, XConnectionType_Direct);
    XObject_connect_1((XObject*)kb, (size_t)XVirtualKeyboard_cancel_signal(NULL),
                      (XObject*)kb, xkb_cancelSlot, XConnectionType_Direct);
    XObject_connect_1((XObject*)kb,
                      (size_t)XVirtualKeyboard_buttonActivated_signal(NULL, 0),
                      (XObject*)kb, xkb_activatedSlot,
                      XConnectionType_Direct);

    /* 2. setMode 切换后布局变化（Number 布局 4 行 17 键）。 */
    XVirtualKeyboard_setMode(kb, XKeyboardMode_Number);
    xkb_expect(XVirtualKeyboard_mode(kb) == XKeyboardMode_Number, "setMode 生效");
    xkb_expect(XVirtualKeyboard_buttonCount(kb) == 17, "数字布局 17 键");
    xkb_expect(XVirtualKeyboard_buttonText(kb, 3) != NULL &&
               strcmp(XVirtualKeyboard_buttonText(kb, 3),
                      XKEYBOARD_LBL_DISMISS) == 0,
               "数字布局键 3 为收起键");
    xkb_expect(XVirtualKeyboard_buttonText(kb, XKEYBOARD_MAX_BUTTONS) == NULL,
               "越界 buttonText 返回 NULL");
    /* 收起键语义：数字模式收起键发 CANCEL 非切回文本。 */
    xkb_expect(XVirtualKeyboard_handleButton(kb, 3), "handleButton 识别收起键");
    xkb_expect(xkb_cancelCount == 1, "收起键发 cancel 信号");
    xkb_expect(XVirtualKeyboard_mode(kb) == XKeyboardMode_Number,
               "收起键不切回文本模式");
    XVirtualKeyboard_setMode(kb, XKeyboardMode_TextLower);

    /* 3. setMap 槽位替换与借用语义（指针等同）+ "" 终止 + 超限拒绝。 */
    XVirtualKeyboard_setMap(kb, XKeyboardMode_User2, s_xkbCustomMap,
                     s_xkbCustomCtrl);
    XVirtualKeyboard_setMode(kb, XKeyboardMode_User2);
    xkb_expect(XVirtualKeyboard_buttonCount(kb) == 2, "自定义布局 2 键");
    xkb_expect(XVirtualKeyboard_buttonText(kb, 0) == s_xkbCustomMap[0],
               "buttonText 返回借用指针（setMap 借用语义）");
    xkb_expect(XVirtualKeyboard_mapArray(kb) == s_xkbCustomMap,
               "mapArray 返回 setMap 装入的借用表");
    xkb_expect((int)XVirtualKeyboard_buttonCtrl(kb, 1) == 2, "自定义控制字往返");
    XVirtualKeyboard_setMap(kb, XKeyboardMode_User3, s_xkbEmptyStopMap, NULL);
    XVirtualKeyboard_setMode(kb, XKeyboardMode_User3);
    xkb_expect(XVirtualKeyboard_buttonCount(kb) == 1, "\x22\x22 按钮终止解析");
    /* 超限表装 User4 划痕槽位（勿装 Number——setMap 先写槽位后重建，
       拒绝只保当前布局不回滚槽位，槽位被超限表占住会让后续 Number
       布局断言（+/- 段）拿到 65 键解析失败的陈旧布局）。 */
    XVirtualKeyboard_setMap(kb, XKeyboardMode_User4, s_xkbOverflowMap, NULL);
    xkb_expect(XVirtualKeyboard_mode(kb) == XKeyboardMode_User3 &&
               XVirtualKeyboard_buttonCount(kb) == 1,
               "超上限 setMap 整体拒绝保持原布局");
    XVirtualKeyboard_setMode(kb, XKeyboardMode_TextLower);

    /* 4. setPopovers 剥离/恢复生效控制字。 */
    {
        int qIdx = xkb_findButton(kb, "q");
        xkb_expect(qIdx >= 0, "小写布局可定位 q 键");
        if (qIdx >= 0) {
            xkb_expect(((int)XVirtualKeyboard_buttonCtrl(kb, (uint32_t)qIdx) &
                        (int)XKEYBOARD_CTRL_POPOVER) == 0,
                       "popovers=0 生效表剥 POPOVER 位");
            XVirtualKeyboard_setPopovers(kb, true);
            xkb_expect(XVirtualKeyboard_popovers(kb), "setPopovers(true) 生效");
            xkb_expect(((int)XVirtualKeyboard_buttonCtrl(kb, (uint32_t)qIdx) &
                        (int)XKEYBOARD_CTRL_POPOVER) != 0,
                       "popovers=1 保留 POPOVER 位");
            XVirtualKeyboard_setPopovers(kb, false);
        }
    }

    /* 5. 触发时序（合成鼠标事件直发键盘自身）：默认按下触发。 */
    XVirtualKeyboard_setGeometry(kb, 0, 0, 400, 160);

    /* 4.5 键位几何回归锁（demo 真机误触连锁的根因）：行内矩形无缝衔
     * 接、首键宽=单位占比（LOWER 行 0 单位总数 52，1# 占 5/52）——
     * 宽度公式若把像素偏移当单位累加会逐键乘 contentW/total 爆炸，
     * 此处以公开矩形锁死。 */
    {
        uint32_t i;
        int seam = 0;
        for (i = 1; i < XVirtualKeyboard_buttonCount(kb); ++i) {
            if (kb->m_keyRects[i].y == kb->m_keyRects[i - 1].y &&
                kb->m_keyRects[i].x !=
                    kb->m_keyRects[i - 1].x + kb->m_keyRects[i - 1].width)
                ++seam;
        }
        xkb_expect(seam == 0, "行内键位矩形无缝衔接（宽度累加不变式）");
        xkb_expect(kb->m_keyRects[0].width ==
                       5 * (XVirtualKeyboard_width(kb) - 4) / 52,
                   "首键宽=单位占比（1# 占行宽 5/52）");
    }
#if XLINEEDIT_ON
    {
        /* ---- 绑定 XLineEdit 后的写入链断言族 ---- */
        XLineEdit* edit = XLineEdit_create(NULL, 0);
        XObject_connect_1((XObject*)edit,
                          (size_t)XLineEdit_returnPressed_signal(NULL),
                          (XObject*)edit, xkb_returnPressedSlot,
                          XConnectionType_Direct);
        XVirtualKeyboard_setTextArea(kb, (XWidget*)edit);
        xkb_expect(XVirtualKeyboard_textArea(kb) == (XWidget*)edit, "绑定生效");

        /* 大写布局键入 'Q'：Shift 修饰位契约。 */
        XVirtualKeyboard_setMode(kb, XKeyboardMode_TextUpper);
        {
            int qIdx = xkb_findButton(kb, "Q");
            xkb_expect(qIdx >= 0, "大写布局可定位 Q 键");
            xkb_clickAt(kb, qIdx, false); /* popovers=0：按下即触发 */
        }
        xkb_expect(strcmp(XLineEdit_text(edit), "Q") == 0,
                   "大写布局键入 'Q' 输出大写（Shift 修饰位契约）");
        XVirtualKeyboard_setMode(kb, XKeyboardMode_TextLower);

        /* 小写 q w e 逐键（按下触发，无需释放）。 */
        {
            int qIdx = xkb_findButton(kb, "q");
            int wIdx = xkb_findButton(kb, "w");
            int eIdx = xkb_findButton(kb, "e");
            xkb_clickAt(kb, qIdx, false);
            xkb_clickAt(kb, wIdx, false);
            xkb_clickAt(kb, eIdx, false);
        }
        xkb_expect(strcmp(XLineEdit_text(edit), "Qqwe") == 0,
                   "小写 qwe 依次写入");
        xkb_expect(xkb_activatedCount >= 4, "buttonActivated 随键计数");

        /* 退格/光标键（合成 XKeyEvent 到目标编辑框）。 */
        {
            int bsIdx = xkb_findButton(kb, XKEYBOARD_LBL_BACKSPACE);
            int leftIdx = xkb_findButton(kb, XKEYBOARD_LBL_LEFT);
            int rightIdx = xkb_findButton(kb, XKEYBOARD_LBL_RIGHT);
            xkb_clickAt(kb, bsIdx, false);
            xkb_expect(strcmp(XLineEdit_text(edit), "Qqw") == 0,
                       "退格删除末字符");
            xkb_clickAt(kb, leftIdx, false);
            xkb_expect(XLineEdit_cursorPosition(edit) == 2,
                       "左移键光标到 2");
            xkb_clickAt(kb, rightIdx, false);
            xkb_expect(XLineEdit_cursorPosition(edit) == 3,
                       "右移键光标回末尾");
        }

        /* 换行键 → returnPressed 信号计数。 */
        {
            int nlIdx = xkb_findButton(kb, XKEYBOARD_LBL_NEWLINE);
            xkb_clickAt(kb, nlIdx, false);
            xkb_expect(xkb_returnPressedCount == 1,
                       "换行键触发 returnPressed 信号");
        }

        /* 确认键 → ready 信号；确认不自动收层（确认键为 FLAGS 组合含
         * CLICK_TRIG：释放触发，合成事件须按压+释放成对）。
         * 弹层状态断言用 m_popped 弹出态标志（popup 置位、仅收层路径
         * 复位）：无头回归环境编辑框为未 show 顶层，有效可见性
         * （popupVisible=XWidget_isVisible）恒假（探针实测 popped=1/
         * visible=0），GUI 环境（demo 页）两者同真——m_popped 两环境
         * 语义一致。 */
        {
            int okIdx = xkb_findButton(kb, XKEYBOARD_LBL_OK);
            XWidget_setFocus((XWidget*)edit);
            XVirtualKeyboard_popup(kb, (XWidget*)edit);
            xkb_expect(kb->m_popped, "popup 后弹层为弹出态（m_popped）");
            xkb_clickAt(kb, okIdx, true);
            xkb_expect(xkb_readyCount == 1, "确认键发 ready 信号");
            xkb_expect(kb->m_popped, "确认键不自动收层（弹出态保持）");
        }

        /* 弹层几何（host=顶层编辑框自身 200x30 → 上界钳位生效）。 */
        xkb_expect(XVirtualKeyboard_width(kb) == XWidget_width((XWidget*)edit),
                   "弹层宽==宿主宽");
        xkb_expect(XVirtualKeyboard_height(kb) > 0, "弹层高经钳位为正");
        xkb_expect(XWidget_hasFocus((XWidget*)edit),
                   "弹层期间焦点保持在编辑框（键盘 NoFocus 不抢焦点）");
        XVirtualKeyboard_closePopup(kb);
        xkb_expect(!XVirtualKeyboard_popupVisible(kb), "closePopup 后弹层不可见");

        /* 收起键 → cancel 信号 + 弹层可见即自动收层（收起键同为 FLAGS
         * 组合：释放触发，合成事件按压+释放成对）。 */
        {
            int disIdx = xkb_findButton(kb, XKEYBOARD_LBL_DISMISS);
            XVirtualKeyboard_popup(kb, (XWidget*)edit);
            xkb_clickAt(kb, disIdx, true);
            xkb_expect(xkb_cancelCount == 2, "收起键再发 cancel 信号");
            xkb_expect(!XVirtualKeyboard_popupVisible(kb),
                       "cancel 后弹层可见即自动收层");
        }

        /* CLICK_TRIG 释放触发：1# 键（FLAGS 含 CLICK_TRIG）按下不切、
         * 释放才切 Special；滑动出键取消（按住移出后释放不触发）。 */
        {
            int spIdx = xkb_findButton(kb, XKEYBOARD_LBL_SPECIAL);
            XMouseEvent me;
            XPoint pos;
            XPoint outPos;
            xkb_expect(XVirtualKeyboard_mode(kb) == XKeyboardMode_TextLower,
                       "前置：文本小写模式");
            pos.x = kb->m_keyRects[spIdx].x +
                    kb->m_keyRects[spIdx].width / 2;
            pos.y = kb->m_keyRects[spIdx].y +
                    kb->m_keyRects[spIdx].height / 2;
            XMouseEvent_init(&me, XEVENT_TYPE_MOUSE_BUTTON_PRESS,
                             XMouseButton_LeftButton, 0, pos);
            XObject_event_base((XObject*)kb, (XEvent*)&me);
            xkb_expect(XVirtualKeyboard_mode(kb) == XKeyboardMode_TextLower,
                       "CLICK_TRIG 键按下不触发");
            XMouseEvent_init(&me, XEVENT_TYPE_MOUSE_BUTTON_RELEASE,
                             XMouseButton_LeftButton, 0, pos);
            XObject_event_base((XObject*)kb, (XEvent*)&me);
            xkb_expect(XVirtualKeyboard_mode(kb) == XKeyboardMode_Special,
                       "CLICK_TRIG 键释放触发");
            /* 滑动出键取消：按住 abc 键 → 移出 → 释放，模式保持。 */
            XVirtualKeyboard_setMode(kb, XKeyboardMode_Special);
            {
                int abcIdx = xkb_findButton(kb, XKEYBOARD_LBL_LOWER);
                pos.x = kb->m_keyRects[abcIdx].x +
                        kb->m_keyRects[abcIdx].width / 2;
                pos.y = kb->m_keyRects[abcIdx].y +
                        kb->m_keyRects[abcIdx].height / 2;
                XMouseEvent_init(&me, XEVENT_TYPE_MOUSE_BUTTON_PRESS,
                                 XMouseButton_LeftButton, 0, pos);
                XObject_event_base((XObject*)kb, (XEvent*)&me);
                outPos.x = kb->m_keyRects[abcIdx].x - 200;
                outPos.y = kb->m_keyRects[abcIdx].y - 200;
                XMouseEvent_init(&me, XEVENT_TYPE_MOUSE_MOVE,
                                 XMouseButton_LeftButton, 0, outPos);
                XObject_event_base((XObject*)kb, (XEvent*)&me);
                XMouseEvent_init(&me, XEVENT_TYPE_MOUSE_BUTTON_RELEASE,
                                 XMouseButton_LeftButton, 0, outPos);
                XObject_event_base((XObject*)kb, (XEvent*)&me);
                xkb_expect(XVirtualKeyboard_mode(kb) == XKeyboardMode_Special,
                           "滑动出键取消触发");
            }
        }

        /* +/- 符号翻转（数字模式，XLineEdit 主路径：home/选删/行首插）。 */
        {
            int signIdx = -1;
            XVirtualKeyboard_setMode(kb, XKeyboardMode_Number);
            signIdx = xkb_findButton(kb, XKEYBOARD_LBL_SIGN);
            XLineEdit_clear(edit);
            XLineEdit_insert(edit, "12");
            xkb_clickAt(kb, signIdx, false);
            xkb_expect(strcmp(XLineEdit_text(edit), "-12") == 0,
                       "+/- 无符号时行首插 -");
            xkb_clickAt(kb, signIdx, false);
            xkb_expect(strcmp(XLineEdit_text(edit), "+12") == 0,
                       "+/- 翻转 - 为 +");
            XVirtualKeyboard_setMode(kb, XKeyboardMode_TextLower);
        }

        /* handleButton 可复用入口：未消费按钮回落字符语义。 */
        {
            int qIdx = xkb_findButton(kb, "q");
            XLineEdit_clear(edit);
            xkb_expect(XVirtualKeyboard_handleButton(kb, (uint32_t)qIdx),
                       "handleButton 字符键返回 true");
            xkb_expect(strcmp(XLineEdit_text(edit), "q") == 0,
                       "handleButton 复用按键写入语义");
        }

        /* destroyed 防悬垂：目标销毁即自动解绑（先摘离键盘防父子级联
         * 连带销毁——popup 曾把键盘挂为编辑框子控件）。先断开
         * returnPressed 计数连接再销毁（自连接随对象销毁自然消亡，
         * delete 之后再 disconnect 即解引用已释放对象）。 */
        XVirtualKeyboard_closePopup(kb);
        XVirtualKeyboard_setParent(kb, NULL, 0);
        XObject_disconnect_1((XObject*)edit,
                             (size_t)XLineEdit_returnPressed_signal(NULL),
                             (XObject*)edit, xkb_returnPressedSlot);
        XLineEdit_delete_base(edit);
        xkb_expect(XVirtualKeyboard_textArea(kb) == NULL,
                   "目标销毁后 m_target 自动解绑");
    }
#else
    /* 无编辑控件适配时仅验证键盘核心（布局/切换/控制字）。 */
    {
        int spIdx = xkb_findButton(kb, XKEYBOARD_LBL_SPECIAL);
        xkb_expect(XVirtualKeyboard_handleButton(kb, (uint32_t)spIdx),
                   "handleButton 识别 1# 切换键");
        xkb_expect(XVirtualKeyboard_mode(kb) == XKeyboardMode_Special,
                   "1# 切到特殊符号模式");
        XVirtualKeyboard_setMode(kb, XKeyboardMode_TextLower);
    }
    /* 裁剪分支未用垫片（防 -Wunused 告警）。 */
    (void)xkb_clickAt;
    (void)xkb_returnPressedSlot;
#endif /* XLINEEDIT_ON */

    /* 信号计数断开（删除前收尾）。 */
    XObject_disconnect_1((XObject*)kb, (size_t)XVirtualKeyboard_ready_signal(NULL),
                         (XObject*)kb, xkb_readySlot);
    XObject_disconnect_1((XObject*)kb, (size_t)XVirtualKeyboard_cancel_signal(NULL),
                         (XObject*)kb, xkb_cancelSlot);
    XObject_disconnect_1((XObject*)kb,
                         (size_t)XVirtualKeyboard_buttonActivated_signal(NULL, 0),
                         (XObject*)kb, xkb_activatedSlot);

    XVirtualKeyboard_delete_base(kb);

#if XKEYBOARD_IME_ON
    /* ================================================================
     * IME 用例段（XKEYBOARD_IME_ON 门控）：① 表机械回归；② 前缀判
     * 定；③ 状态机序列（纯模块级，无控件）；④ 键盘集成（XVirtualKeyboard+
     * XLineEdit 目标）；⑤ 候选带几何。
     * ================================================================ */
    /* ---- ① 表机械回归（表头 @details 口径实测值）。 ---- */
    xkb_expect(XPinyinTable_syllableCount() == 412, "音节表 412 条");
    xkb_expect(XPinyinTable_entryCount() == 2017, "条目表 2017 条");
    xkb_expect(XPinyinTable_rankLimit() ==
                   (XKEYBOARD_IME_RANK_LIMIT > 0
                        ? (uint16_t)XKEYBOARD_IME_RANK_LIMIT
                        : 0),
               "rankLimit 回环（默认 0=全量）");
    xkb_expect(XPinyinTable_isLegalSyllable("zhuang"),
               "zhuang 合法音节");
    xkb_expect(!XPinyinTable_isLegalSyllable("zz"), "zz 非法音节");
    {
        const XPinyinTableEntry* b = NULL;
        uint16_t n = 0;
        xkb_expect(!XPinyinTable_find("", &b, &n), "find(\"\") false");
        xkb_expect(!XPinyinTable_find(NULL, &b, &n), "find(NULL) false");
        xkb_expect(XPinyinTable_find("fu", &b, &n) && n == 21 &&
                       b != NULL &&
                       strcmp((const char*)b[0].m_utf8, "\xE5\xA4\xAB") == 0,
                   "find(fu) 21 条且首字夫");
    }

    /* ---- ② 前缀判定（下界二分语义）。 ---- */
    xkb_expect(XPinyinTable_hasSyllablePrefix(""), "空串恒真");
    xkb_expect(XPinyinTable_hasSyllablePrefix("z"), "z 前缀存在");
    xkb_expect(!XPinyinTable_hasSyllablePrefix("zz"), "zz 前缀不存在");
    xkb_expect(!XPinyinTable_hasSyllablePrefix("v"), "v 打头无音节");
    xkb_expect(XPinyinTable_hasSyllablePrefix("ao"), "ao 前缀存在");
    xkb_expect(!XPinyinTable_hasSyllablePrefix(NULL), "NULL false");

    /* ---- ③ 状态机序列（纯模块级，无控件）。 ---- */
    {
        XPinyinEngine ime;
        const XPinyinTableEntry* b = NULL;
        uint16_t n = 0;

        XPinyinEngine_init(&ime);
        xkb_expect(XPinyinEngine_isChinese(&ime) &&
                       !XPinyinEngine_isComposing(&ime),
                   "init 默认中文态且非组串");
        xkb_expect(XPinyinEngine_pageSize(&ime) == 9, "init 页容量 9");
        xkb_expect(XPinyinEngine_composingText(&ime)[0] == '\0',
                   "非组串 composingText 空串");

        xkb_expect(XPinyinEngine_feedLetter(&ime, 'n') ==
                       XPinyinEngineFeed_Consumed,
                   "纯前缀 n 消费无候选");
        xkb_expect(XPinyinEngine_candidateCount(&ime) == 0,
                   "纯前缀组串无候选（不猜测）");
        xkb_expect(XPinyinEngine_feedLetter(&ime, 'i') ==
                       XPinyinEngineFeed_Consumed,
                   "ni 进组串");
        xkb_expect(XPinyinEngine_candidateCount(&ime) == 5 &&
                       strcmp(XPinyinEngine_candidateAt(&ime, 0),
                              "\xE4\xBD\xA0") == 0,
                   "ni 候选 5 条且首字你");
        XPinyinTable_find("ni", &b, &n);
        xkb_expect(XPinyinEngine_feedCommitFirst(&ime) ==
                       XPinyinEngineFeed_Committed &&
                       strcmp(XPinyinEngine_commitString(&ime),
                              (const char*)b[0].m_utf8) == 0 &&
                       !XPinyinEngine_isComposing(&ime),
                   "空格提交全局首候选且组串清空");

        xkb_expect(XPinyinEngine_feedLetter(&ime, 'z') ==
                       XPinyinEngineFeed_Consumed &&
                       XPinyinEngine_feedLetter(&ime, 'h') ==
                           XPinyinEngineFeed_Consumed &&
                       XPinyinEngine_feedLetter(&ime, 'u') ==
                           XPinyinEngineFeed_Consumed,
                   "zhu 进组串");
        xkb_expect(XPinyinEngine_feedDigit(&ime, 1) ==
                       XPinyinEngineFeed_Committed &&
                       strcmp(XPinyinEngine_commitString(&ime),
                              "\xE4\xBD\x8F") == 0,
                   "数字 1 选当前页第 1 候选（住）");

        xkb_expect(XPinyinEngine_feedLetter(&ime, 'z') ==
                       XPinyinEngineFeed_Consumed &&
                       XPinyinEngine_feedLetter(&ime, 'h') ==
                           XPinyinEngineFeed_Consumed &&
                       XPinyinEngine_feedLetter(&ime, 'u') ==
                           XPinyinEngineFeed_Consumed &&
                       XPinyinEngine_feedLetter(&ime, 'a') ==
                           XPinyinEngineFeed_Consumed &&
                       XPinyinEngine_feedLetter(&ime, 'n') ==
                           XPinyinEngineFeed_Consumed &&
                       XPinyinEngine_feedLetter(&ime, 'g') ==
                           XPinyinEngineFeed_Consumed &&
                       strcmp(XPinyinEngine_composingText(&ime),
                              "zhuang") == 0,
                   "zhuang 6 字母最长音节进组串");
        xkb_expect(XPinyinEngine_feedCommitRaw(&ime) ==
                       XPinyinEngineFeed_Committed &&
                       strcmp(XPinyinEngine_commitString(&ime), "zhuang") == 0,
                   "回车提交原字母串");

        xkb_expect(XPinyinEngine_feedLetter(&ime, 'n') ==
                       XPinyinEngineFeed_Consumed &&
                       XPinyinEngine_feedLetter(&ime, 'j') ==
                           XPinyinEngineFeed_Consumed &&
                       strcmp(XPinyinEngine_composingText(&ime), "j") == 0,
                   "nj 非法截断至最长合法后缀 j");
        xkb_expect(XPinyinEngine_feedLetter(&ime, 'v') ==
                       XPinyinEngineFeed_Consumed &&
                       XPinyinEngine_composingText(&ime)[0] == '\0',
                   "jv 无合法后缀清空");
        xkb_expect(XPinyinEngine_feedBackspace(&ime) ==
                       XPinyinEngineFeed_Ignored,
                   "空组串退格 Ignored（透传编辑框）");

        xkb_expect(XPinyinEngine_feedLetter(&ime, 'f') ==
                       XPinyinEngineFeed_Consumed &&
                       XPinyinEngine_feedLetter(&ime, 'u') ==
                           XPinyinEngineFeed_Consumed &&
                       XPinyinEngine_pageCount(&ime) == 3,
                   "fu 21 条按页容量 9 分 3 页");
        xkb_expect(XPinyinEngine_pageIndex(&ime) == 0 &&
                       XPinyinEngine_pageNext(&ime) &&
                       XPinyinEngine_pageIndex(&ime) == 1 &&
                       XPinyinEngine_pageNext(&ime) &&
                       XPinyinEngine_pageIndex(&ime) == 2 &&
                       !XPinyinEngine_pageNext(&ime) &&
                       XPinyinEngine_pagePrev(&ime),
                   "翻页钳位 [0,pageCount)");
        XPinyinEngine_resetComposition(&ime);

        xkb_expect(XPinyinEngine_feedLetter(&ime, 'n') ==
                       XPinyinEngineFeed_Consumed &&
                       XPinyinEngine_feedDigit(&ime, 7) ==
                           XPinyinEngineFeed_Consumed,
                   "数字越界且组串中吞掉（ni 仅 5 候选）");
        XPinyinEngine_resetComposition(&ime);
        xkb_expect(XPinyinEngine_feedDigit(&ime, 5) ==
                       XPinyinEngineFeed_Ignored,
                   "IDLE 数字 Ignored（放行普通数字）");
        xkb_expect(XPinyinEngine_feedLetter(&ime, 'z') ==
                       XPinyinEngineFeed_Consumed &&
                       XPinyinEngine_feedCommitFirst(&ime) ==
                           XPinyinEngineFeed_Committed &&
                       strcmp(XPinyinEngine_commitString(&ime), "z") == 0,
                   "纯前缀组串空格提交原字母串");
        XPinyinEngine_resetComposition(&ime);

        XPinyinEngine_setPageSize(&ime, 0);
        xkb_expect(XPinyinEngine_pageSize(&ime) == 1, "页容量下限钳位 1");
        XPinyinEngine_setPageSize(&ime, 100);
        xkb_expect(XPinyinEngine_pageSize(&ime) == 9, "页容量上限钳位 9");

        XPinyinEngine_setChinese(&ime, false);
        xkb_expect(!XPinyinEngine_isChinese(&ime) &&
                       XPinyinEngine_feedLetter(&ime, 'a') ==
                           XPinyinEngineFeed_Ignored &&
                       XPinyinEngine_feedDigit(&ime, 1) ==
                           XPinyinEngineFeed_Ignored &&
                       XPinyinEngine_feedCommitFirst(&ime) ==
                           XPinyinEngineFeed_Ignored &&
                       XPinyinEngine_feedCommitRaw(&ime) ==
                           XPinyinEngineFeed_Ignored &&
                       XPinyinEngine_feedBackspace(&ime) ==
                           XPinyinEngineFeed_Ignored &&
                       XPinyinEngine_feedCandidate(&ime, 0) ==
                           XPinyinEngineFeed_Ignored,
                   "EN 态全部 feed Ignored");
        xkb_expect(strcmp(XPinyinEngine_commitString(&ime), "") != 0,
                   "EN 态 feed 不动最近提交串");
    }

    /* ---- ④/⑤ 键盘集成与候选带几何（XVirtualKeyboard + XLineEdit 目标；
     *     面板 IME API 已随框架化挂 XVIRTUALKEYBOARD_ON 门控——XKey
     *     board.h 现状，框架未开时本段整体编译出）。 ---- */
#if XLINEEDIT_ON && XVIRTUALKEYBOARD_ON
    {
        XVirtualKeyboard* kb2 = XVirtualKeyboard_create(NULL, 0);
        XLineEdit* edit2 = XLineEdit_create(NULL, 0);
        int imeIdx;
        int bsIdx;
        int nlIdx;
        int spIdx;
        int nIdx;
        XVirtualKeyboard_setGeometry(kb2, 0, 0, 400, 200);
        xkb_expect(!XVirtualKeyboard_imeEnabled(kb2), "默认 IME 关闭");
        xkb_expect(XVirtualKeyboard_setImeEnabled(kb2, true), "setImeEnabled(true)");
        xkb_expect(XVirtualKeyboard_imeEnabled(kb2), "IME 开关往返");
        xkb_expect(XVirtualKeyboard_mode(kb2) == XKeyboardMode_User1,
                   "启用后切 User1 槽位");
        xkb_expect(XVirtualKeyboard_buttonCount(kb2) == 44, "拼音布局 44 键");
        imeIdx = xkb_findButton(kb2, XKEYBOARD_LBL_IME);
        bsIdx = xkb_findButton(kb2, XKEYBOARD_LBL_BACKSPACE);
        nlIdx = xkb_findButton(kb2, XKEYBOARD_LBL_NEWLINE);
        spIdx = xkb_findButton(kb2, " ");
        xkb_expect(imeIdx >= 0 && bsIdx >= 0 && nlIdx >= 0 && spIdx >= 0,
                   "拼音布局可定位 中/EN 退格 换行 空格");
        xkb_expect(xkb_findButton(kb2, XKEYBOARD_LBL_LOWER) < 0 &&
                       xkb_findButton(kb2, XKEYBOARD_LBL_SPECIAL) < 0,
                   "拼音布局不含 abc/1# 切换键");
        xkb_expect(XVirtualKeyboard_imeChinese(kb2), "启用后默认中文态");

        XVirtualKeyboard_setTextArea(kb2, (XWidget*)edit2);
        xkb_expect(XVirtualKeyboard_textArea(kb2) == (XWidget*)edit2, "IME 键盘绑定目标");

        /* 合成点击 n i：字母不直写，进组串（覆盖按下触发路径）。 */
        nIdx = xkb_findButton(kb2, "n");
        xkb_clickAt(kb2, nIdx, false);
        xkb_clickAt(kb2, xkb_findButton(kb2, "i"), false);
        xkb_expect(XLineEdit_text(edit2)[0] == '\0',
                   "中文态字母不直写编辑框");
        xkb_expect(strcmp(xkb_imeBuffer(kb2), "ni") == 0, "组串为 ni");
#if XVIRTUALKEYBOARD_ON
        /* Qt 形态消费面（apiMapping#13）：候选=engine.wordCandidateListModel()
         * （dataAt 取 XVariant*），组串镜像=context.preeditText()。ni 单
         * 音节 5 候选与单字表逐位一致（⑥b 同口径）。 */
        {
            XVirtualKeyboardSelectionListModel* model =
                XVirtualKeyboardInputEngine_wordCandidateListModel(
                    XVirtualKeyboardInputContext_inputEngine(
                        XVirtualKeyboardInputContext_instance()));
            xkb_expect(model != NULL &&
                           XVirtualKeyboardSelectionListModel_count(model) ==
                               5,
                       "候选模型 ni 5 条（SelectionListModel 插件喂养）");
        }
#endif /* XVIRTUALKEYBOARD_ON */
        xkb_clickAt(kb2, spIdx, false);
        xkb_expect(strcmp(XLineEdit_text(edit2), "\xE4\xBD\xA0") == 0,
                   "空格上屏全局首候选（你）");

        /* 中/EN 键（CLICK_TRIG 释放触发）切英文 → q 直写。 */
        xkb_clickAt(kb2, imeIdx, true);
        xkb_expect(!XVirtualKeyboard_imeChinese(kb2), "中/EN 键切英文态");
        xkb_clickAt(kb2, xkb_findButton(kb2, "q"), false);
        xkb_expect(strcmp(XLineEdit_text(edit2), "\xE4\xBD\xA0q") == 0,
                   "英文态字母直写编辑框");
        xkb_expect(kb2->m_imeBandRect.height == 0, "英文态候选带归零");

        /* 复用入口 XVirtualKeyboard_handleButton 分发字母 id 同样进组串。 */
        xkb_expect(XVirtualKeyboard_setImeChinese(kb2, true), "程序化切回中文");
        xkb_expect(XVirtualKeyboard_handleButton(
                       kb2, (uint32_t)xkb_findButton(kb2, "h")),
                   "handleButton 字母键返回 true");
        xkb_expect(XVirtualKeyboard_handleButton(
                       kb2, (uint32_t)xkb_findButton(kb2, "a")),
                   "handleButton 组串 a");
        xkb_expect(XVirtualKeyboard_handleButton(
                       kb2, (uint32_t)xkb_findButton(kb2, "o")),
                   "handleButton 组串 o");
        xkb_expect(strcmp(xkb_imeBuffer(kb2), "hao") == 0,
                   "handleButton 组串 hao");

        /* 组串中退格：逐字母删，删空后透传编辑框退格。 */
        xkb_clickAt(kb2, bsIdx, false);
        xkb_expect(strcmp(xkb_imeBuffer(kb2), "ha") == 0, "退格删组串字母");
        xkb_clickAt(kb2, bsIdx, false);
        xkb_clickAt(kb2, bsIdx, false);
        xkb_expect(xkb_imeBuffer(kb2)[0] == '\0', "组串删空");
        xkb_clickAt(kb2, bsIdx, false);
        xkb_expect(strcmp(XLineEdit_text(edit2), "\xE4\xBD\xA0") == 0,
                   "空组串退格透传删编辑框末字符（q）");

        /* 组串中换行键（CLICK_TRIG 释放触发）：原字母串上屏且
           returnPressed 不计数。 */
        {
            int rp0 = xkb_returnPressedCount;
            xkb_clickAt(kb2, nIdx, false);
            xkb_clickAt(kb2, xkb_findButton(kb2, "i"), false);
            xkb_clickAt(kb2, nlIdx, true);
            xkb_expect(strcmp(XLineEdit_text(edit2), "\xE4\xBD\xA0ni") == 0,
                       "组串中换行提交原字母串 ni");
            xkb_expect(xkb_returnPressedCount == rp0,
                       "IME 原串提交不发 returnPressed");
        }

        /* 收层复位：closePopup 弃组串草稿。 */
        xkb_clickAt(kb2, xkb_findButton(kb2, "z"), false);
        xkb_expect(xkb_imeBuffer(kb2)[0] == 'z', "前置：组串 z");
        XVirtualKeyboard_closePopup(kb2);
        xkb_expect(xkb_imeBuffer(kb2)[0] == '\0', "closePopup 组串复位");
#if XVIRTUALKEYBOARD_ON
        /* 改断言口径（评审#9）：closePopup=面板调 engine reset（插件
         * 复位）→ preeditText 空且候选模型清空（band 无候选）。 */
        {
            XVirtualKeyboardSelectionListModel* model =
                XVirtualKeyboardInputEngine_wordCandidateListModel(
                    XVirtualKeyboardInputContext_inputEngine(
                        XVirtualKeyboardInputContext_instance()));
            xkb_expect(!model ||
                           XVirtualKeyboardSelectionListModel_count(model) ==
                               0,
                       "closePopup：engine reset 后候选模型清空（band 无候选）");
        }
#endif /* XVIRTUALKEYBOARD_ON */

        /* ---- ⑤ 候选带几何。 ---- */
        /* :694 之后历经 handleButton 组串/退格/换行原串提交/closePopup
           ——中文态是键盘偏好，这些路径均不翻转（closePopup 仅清组串
           保留中文态），且均不触发重建——须先程序化切回英文态（触发
           setImeChinese(false) 的重建路径把带矩形归零），断言才成立。 */
        xkb_expect(XVirtualKeyboard_setImeChinese(kb2, false), "程序化切英文态");
        xkb_expect(!XVirtualKeyboard_imeChinese(kb2) &&
                       kb2->m_imeBandRect.height == 0,
                   "英文态无候选带");
        xkb_expect(XVirtualKeyboard_setImeChinese(kb2, true), "切回中文态");
        xkb_expect(kb2->m_imeBandRect.height > 0, "中文态候选带预留");
        xkb_expect(kb2->m_imeBandRect.y + kb2->m_imeBandRect.height <=
                       kb2->m_keyRects[0].y,
                   "候选带在键区上方（无交叠）");
        xkb_expect(XVirtualKeyboard_setImeEnabled(kb2, false) &&
                       !XVirtualKeyboard_imeEnabled(kb2),
                   "setImeEnabled(false) 生效");
        xkb_expect(kb2->m_imeBandRect.height == 0, "关闭后候选带归零");
        xkb_expect(XVirtualKeyboard_buttonCount(kb2) == 44,
                   "User1 槽位仍为拼音表（槽位占用约定，不恢复原表）");

        XVirtualKeyboard_setTextArea(kb2, NULL);
        XLineEdit_delete_base(edit2);
        XVirtualKeyboard_delete_base(kb2);
    }
#else
    /* 无编辑控件适配：仅验证 IME 开关与布局装载。 */
    {
        XVirtualKeyboard* kb2 = XVirtualKeyboard_create(NULL, 0);
        XVirtualKeyboard_setGeometry(kb2, 0, 0, 400, 200);
        xkb_expect(XVirtualKeyboard_setImeEnabled(kb2, true) &&
                       XVirtualKeyboard_imeEnabled(kb2) &&
                       XVirtualKeyboard_mode(kb2) == XKeyboardMode_User1 &&
                       XVirtualKeyboard_buttonCount(kb2) == 44,
                   "IME 启用装拼音布局");
        xkb_expect(XVirtualKeyboard_setImeEnabled(kb2, false) &&
                       !XVirtualKeyboard_imeEnabled(kb2),
                   "IME 关闭");
        XVirtualKeyboard_delete_base(kb2);
    }
#endif /* XLINEEDIT_ON && XVIRTUALKEYBOARD_ON */

    /* ================================================================
     * ⑥ 切分 DP + 词组文件 + 混排（V2）：INV2 跨音节组串在 PHRASE 三
     *    象限（ON∧ready / ON∧缺资产 / OFF）全部相同，只套 XKEYBOARD_
     *    IME_ON 门控；词组断言再套 XKEYBOARD_IME_PHRASE_ON，资产相关
     *    断言全部 isReady() 守卫双分支（缺资产象限由红改绿，断言回
     *    退行为）。黄金值=本轮实跑仿真（对真实 408 音节表与词库资产）。
     * ================================================================ */
    /* ---- ⑥a INV2 组串黄金（PHRASE 无关）。 ---- */
    {
        XPinyinEngine ime;
        XPinyinEngine_init(&ime);
        /* nihao：INV2 升级点——V1 同点组 "hao"（'h' 处 "nih" 无音节
           前缀被截断），V2 组出跨音节串（V1 接受集严格超集）。 */
        xkb_imeFeedStr(&ime, "nihao");
        xkb_expect(strcmp(XPinyinEngine_composingText(&ime), "nihao") == 0,
                   "INV2: nihao 组串（V1 同点 hao，接受集严格超集）");
        XPinyinEngine_resetComposition(&ime);
        xkb_imeFeedStr(&ime, "fangan");
        xkb_expect(strcmp(XPinyinEngine_composingText(&ime), "fangan") == 0,
                   "INV2: fangan 组串（切分 fang+an / fan+gan）");
        XPinyinEngine_resetComposition(&ime);
        xkb_imeFeedStr(&ime, "zhuangtai");
        xkb_expect(strcmp(XPinyinEngine_composingText(&ime),
                         "zhuangtai") == 0,
                   "INV2: zhuangtai 组串（V1 同点 zhuang 6 字母上限截断）");
        XPinyinEngine_resetComposition(&ime);
        /* 组串容量 15：喂 16 字母吞第 16 个，长度恒 15。 */
        xkb_imeFeedStr(&ime, "aaaaaaaaaaaaaaaa");
        xkb_expect((int)strlen(XPinyinEngine_composingText(&ime)) == 15,
                   "组串容量 15（16 字母吞尾，词库最长拼音 14 字母口径）");
        XPinyinEngine_resetComposition(&ime);
        /* V1 平价黄金（16 组既有序列本轮实测 buf 全等，抽 3 组锁）。 */
        xkb_imeFeedStr(&ime, "nj");
        xkb_expect(strcmp(XPinyinEngine_composingText(&ime), "j") == 0,
                   "nj 非法截断至最长合法后缀 j（V1 平价）");
        xkb_imeFeedStr(&ime, "v");
        xkb_expect(XPinyinEngine_composingText(&ime)[0] == '\0',
                   "jv 无合法后缀清空（V1 平价）");
        xkb_imeFeedStr(&ime, "zz");
        xkb_expect(strcmp(XPinyinEngine_composingText(&ime), "z") == 0,
                   "zz 截断至 z（z 无可达切分零候选，V1 平价）");
        XPinyinEngine_resetComposition(&ime);
        /* "a"x15：单字母音节链可达（408 表含 a/e/o），候选首=啊（区
           域预算/相邻合并锁，PHRASE 无关——词组键>=2 音节恒 miss）。 */
        xkb_imeFeedStr(&ime, "aaaaaaaaaaaaaaaa");
        xkb_expect(XPinyinEngine_candidateCount(&ime) == 2 &&
                       strcmp(XPinyinEngine_candidateAt(&ime, 0),
                              "\xE5\x95\x8A") == 0,
                   "a x15 组串可达且候选首=啊（首路 15 段相邻合并）");
        XPinyinEngine_resetComposition(&ime);
    }

    /* ---- ⑥b 单音节平价：候选集与 XPinyinTable_find 逐位一致
     *      （单音节键永不侵入词组——加载器拒收 1 音节键；选取的音节
     *      均无交替切分词组键，与全部既有测试组串零交集；PHRASE 无
     *      关三象限恒真）。 ---- */
    {
        static const char* const syls[] = { "ni",  "fu", "zhu",
                                            "hao", "a",  "an",
                                            "nv" };
        int si;
        for (si = 0; si < (int)(sizeof(syls) / sizeof(syls[0])); ++si) {
            XPinyinEngine ime;
            const XPinyinTableEntry* b = NULL;
            uint16_t n = 0;
            XPinyinEngine_init(&ime);
            xkb_imeFeedStr(&ime, syls[si]);
            XPinyinTable_find(syls[si], &b, &n);
            xkb_expect(XPinyinEngine_candidateCount(&ime) == (int32_t)n &&
                           (n == 0
                                ? XPinyinEngine_candidateAt(&ime, 0) == NULL
                                : (b != NULL &&
                                   XPinyinEngine_candidateAt(&ime, 0) !=
                                       NULL &&
                                   strcmp(XPinyinEngine_candidateAt(&ime, 0),
                                          (const char*)b[0].m_utf8) == 0)),
                       "单音节候选与单字表 find 逐位一致（V1 平价）");
        }
    }

#if XKEYBOARD_IME_PHRASE_ON
    /* ---- ⑥c 词组混排（ready 守卫双分支；黄金值=实跑仿真）。 ---- */
    {
        XPinyinEngine ime;
        XPinyinEngine_init(&ime);
        if (XPinyinPhrase_isReady()) {
            /* Q1：nihao → [你好]+find(ni)5+find(hao)5=11 条 2 页。 */
            xkb_imeFeedStr(&ime, "nihao");
            xkb_expect(XPinyinEngine_candidateCount(&ime) == 11 &&
                           XPinyinEngine_pageCount(&ime) == 2 &&
                           strcmp(XPinyinEngine_candidateAt(&ime, 0),
                                  "\xE4\xBD\xA0\xE5\xA5\xBD") == 0,
                       "nihao 混排 11 条 2 页且首候选=你好（词组前单字垫后）");
            xkb_expect(XPinyinEngine_feedCommitFirst(&ime) ==
                           XPinyinEngineFeed_Committed &&
                           strcmp(XPinyinEngine_commitString(&ime),
                                  "\xE4\xBD\xA0\xE5\xA5\xBD") == 0 &&
                           strlen(XPinyinEngine_commitString(&ime)) == 6,
                       "空格提交词组首候选你好（m_commit 扩容 6 字节锁）");
            XPinyinEngine_resetComposition(&ime);
            xkb_imeFeedStr(&ime, "xian");
            xkb_expect(strcmp(XPinyinEngine_candidateAt(&ime, 0),
                              "\xE8\xA5\xBF\xE5\xAE\x89") == 0,
                       "xian 首候选=西安（xi+an 交替切分词组）");
            XPinyinEngine_resetComposition(&ime);
            xkb_imeFeedStr(&ime, "qie");
            xkb_expect(strcmp(XPinyinEngine_candidateAt(&ime, 0),
                              "\xE4\xBC\x81\xE9\xB9\x85") == 0,
                       "qie 首候选=企鹅（qi+e）");
            XPinyinEngine_resetComposition(&ime);
            xkb_imeFeedStr(&ime, "yue");
            xkb_expect(strcmp(XPinyinEngine_candidateAt(&ime, 0),
                              "\xE4\xBD\x99\xE9\xA2\x9D") == 0,
                       "yue 首候选=余额（yu+e）");
            XPinyinEngine_resetComposition(&ime);
            xkb_imeFeedStr(&ime, "women");
            xkb_expect(strcmp(XPinyinEngine_candidateAt(&ime, 0),
                              "\xE6\x88\x91\xE4\xBB\xAC") == 0,
                       "women 首候选=我们");
            XPinyinEngine_resetComposition(&ime);
            xkb_imeFeedStr(&ime, "shijian");
            xkb_expect(XPinyinEngine_candidateCount(&ime) >= 2 &&
                           strcmp(XPinyinEngine_candidateAt(&ime, 0),
                                  "\xE6\x97\xB6\xE9\x97\xB4") == 0 &&
                           strcmp(XPinyinEngine_candidateAt(&ime, 1),
                                  "\xE5\xAE\x9E\xE8\xB7\xB5") == 0,
                       "shijian 词组组=时间,实践（组内频序）");
            XPinyinEngine_resetComposition(&ime);
            xkb_imeFeedStr(&ime, "zhongguo");
            xkb_expect(strcmp(XPinyinEngine_candidateAt(&ime, 0),
                              "\xE4\xB8\xAD\xE5\x9B\xBD") == 0,
                       "zhongguo 首候选=中国");
            XPinyinEngine_resetComposition(&ime);
            xkb_imeFeedStr(&ime, "wanan");
            xkb_expect(strcmp(XPinyinEngine_candidateAt(&ime, 0),
                              "\xE6\x99\x9A\xE5\xAE\x89") == 0,
                       "wanan 首候选=晚安（wan+an 歧义切分保留）");
            XPinyinEngine_resetComposition(&ime);
            /* fangan 词组 miss（库无 fang an / fan gan 键）→ 纯单字。 */
            xkb_imeFeedStr(&ime, "fangan");
            xkb_expect(XPinyinEngine_candidateCount(&ime) == 10 &&
                           strcmp(XPinyinEngine_candidateAt(&ime, 0),
                                  (const char*)"\xE6\x96\xB9") == 0,
                       "fangan 词组 miss 回退纯单字 10 条首字方");
            XPinyinEngine_resetComposition(&ime); /* 清借用区间（⑥d reload 前置）。 */
        } else {
            /* Q2 回退锁：词组 find 恒 miss → 首路纯单字（首字你）。 */
            const XPinyinTableEntry* bn = NULL;
            const XPinyinTableEntry* bh = NULL;
            uint16_t cn = 0;
            uint16_t ch = 0;
            XPinyinTable_find("ni", &bn, &cn);
            XPinyinTable_find("hao", &bh, &ch);
            xkb_imeFeedStr(&ime, "nihao");
            xkb_expect(XPinyinEngine_candidateCount(&ime) ==
                           (int32_t)(cn + ch) &&
                           bn != NULL &&
                           strcmp(XPinyinEngine_candidateAt(&ime, 0),
                                  (const char*)bn[0].m_utf8) == 0,
                       "缺资产回退：nihao 纯单字=首路各音节区间和");
            XPinyinEngine_resetComposition(&ime);
            xkb_imeFeedStr(&ime, "xian");
            {
                const XPinyinTableEntry* bx = NULL;
                uint16_t cx = 0;
                XPinyinTable_find("xian", &bx, &cx);
                xkb_expect(XPinyinEngine_candidateCount(&ime) == (int32_t)cx,
                           "缺资产回退：xian 纯单字=find(xian)");
            }
        }
    }

    /* ---- ⑥d 词组文件解析（夹具经 setPath+reload）。调用契约（问
     *      题 5 采纳）：reload/unload 前必须无活动借用区间——先
     *      XPinyinEngine_resetComposition，错序悬垂 UB。 ---- */
    {
        XPinyinEngine ime;
        const XPinyinPhraseEntry* pb = NULL;
        uint16_t pn = 0;
        uint16_t niId = 0;
        uint16_t haoId = 0;
        XPinyinEngine_init(&ime);
        XPinyinEngine_resetComposition(&ime); /* 前置：清借用区间。 */
        XPinyinPhrase_setPath(
            "../Library/VirtualKeyboard/test/phrases_test_fixture.txt");
        XPinyinPhrase_reload();
        xkb_expect(XPinyinPhrase_count() == 2,
                   "夹具恰装 2 条（坏行逐行跳过：非法音节/单音节/rank=0"
                   "/超15字节/字段数错误/空行/UTF-8 截断）");
        xkb_expect(XPinyinTable_syllableIdOf("ni", &niId) &&
                       XPinyinTable_syllableIdOf("hao", &haoId) &&
                       niId != haoId,
                   "syllableIdOf 导出契约（音节串→id）");
        {
            uint16_t key[2];
            key[0] = niId;
            key[1] = haoId;
            xkb_expect(XPinyinPhrase_find(key, 2, &pb, &pn) &&
                           pn == 1 &&
                           strcmp(pb[0].m_utf8,
                                  "\xE4\xBD\xA0\xE5\xA5\xBD") == 0 &&
                           pb[0].m_rank == 1 &&
                           pb[0].m_syllableCount == 2,
                       "夹具 find(ni hao) 命中你好 rank=1（零拷贝区间）");
        }
        xkb_expect(!XPinyinPhrase_find(&niId, 1, &pb, &pn),
                   "单音节键查询恒 miss（词组键 2..4 音节）");
        /* 坏路径 → 负缓存粘滞（不重探）。 */
        XPinyinPhrase_setPath(
            "../Library/VirtualKeyboard/phrases_no_such_file.txt");
        xkb_expect(!XPinyinPhrase_reload() &&
                       !XPinyinPhrase_isReady() &&
                       XPinyinPhrase_count() == 0,
                   "坏路径 reload 负缓存粘滞（count==0）");
        /* 恢复默认资产（缺资产环境跳过规模锁不红，记 SKIP）。 */
        XPinyinPhrase_setPath(NULL);
        XPinyinPhrase_reload();
        if (XPinyinPhrase_isReady())
            xkb_expect(XPinyinPhrase_count() == 3143,
                       "恢复默认资产 3143 条（口径=运行期文件资产，区别"
                       "于编译期 412/2017 静态表）");
        else
            fprintf(stderr,
                    "XVirtualKeyboard test: SKIP 词组默认资产恢复断言（资产缺席"
                    "环境，ready 守卫双分支）\n");
    }

    /* ---- ⑥e 词库二进制格式 V3（XIPB 快路径 + 坏件拒绝 + txt/bin 黄
     *      金一致）。调用契约同 ⑥d：每步 reload 前先
     *      XPinyinEngine_resetComposition 清借用区间。 ---- */
    {
        XPinyinEngine ime;
        const XPinyinPhraseEntry* pb = NULL;
        uint16_t pn = 0;
        uint16_t niId = 0;
        uint16_t haoId = 0;
        uint16_t shiId = 0;
        uint16_t xiId = 0;
        uint16_t anId = 0;
        uint32_t hashA = 0;
        uint32_t countA = 0;
        uint32_t hashB = 0;
        uint32_t countB = 0;

        XPinyinEngine_init(&ime);
        XPinyinEngine_resetComposition(&ime); /* 前置：清借用区间。 */

        /* a) 好件：XIPB 夹具加载与查询（txt 段 ⑥d 同断言镜像）。 */
        XPinyinPhrase_setPath(
            "../Library/VirtualKeyboard/test/phrases_test_fixture.bin");
        xkb_expect(XPinyinPhrase_reload() &&
                       XPinyinPhrase_isReady() &&
                       XPinyinPhrase_count() == 2,
                   "XIPB 夹具 reload 恰装 2 条（bin 快路径）");
        xkb_expect(XPinyinTable_syllableIdOf("ni", &niId) &&
                       XPinyinTable_syllableIdOf("hao", &haoId) &&
                       niId != haoId,
                   "XIPB 夹具 syllableIdOf 契约（ni/hao）");
        {
            uint16_t key[2];
            key[0] = niId;
            key[1] = haoId;
            xkb_expect(XPinyinPhrase_find(key, 2, &pb, &pn) &&
                           pn == 1 &&
                           strcmp(pb[0].m_utf8,
                                  "\xE4\xBD\xA0\xE5\xA5\xBD") == 0 &&
                           pb[0].m_rank == 1 &&
                           pb[0].m_syllableCount == 2,
                       "XIPB 夹具 find(ni hao) 命中你好 rank=1（与 txt "
                       "段同断言镜像）");
        }
        xkb_expect(!XPinyinPhrase_find(&niId, 1, &pb, &pn),
                   "XIPB 夹具单音节键查询恒 miss（词组键 2..4 音节）");

        /* b) 坏 bin 断言：逐一 setPath+reload，整体拒绝不降级（负缓存
         *    粘滞）。_badcrc 走 bin 步⑥（CRC 错）、_badver 走步③（版
         *    本不符）、_trunc 走步⑤（体积上界 24+2*16=56>40）。 */
        XPinyinPhrase_setPath(
            "../Library/VirtualKeyboard/test/phrases_test_fixture_badcrc.bin");
        xkb_expect(!XPinyinPhrase_reload() &&
                       !XPinyinPhrase_isReady() &&
                       XPinyinPhrase_count() == 0,
                   "坏 bin badcrc 整体拒绝（bin 步⑥ CRC 错，负缓存粘滞"
                   "不降级）");
        XPinyinPhrase_setPath(
            "../Library/VirtualKeyboard/test/phrases_test_fixture_badver.bin");
        xkb_expect(!XPinyinPhrase_reload() &&
                       !XPinyinPhrase_isReady() &&
                       XPinyinPhrase_count() == 0,
                   "坏 bin badver 整体拒绝（bin 步③ 版本不符）");
        XPinyinPhrase_setPath(
            "../Library/VirtualKeyboard/test/phrases_test_fixture_trunc.bin");
        xkb_expect(!XPinyinPhrase_reload() &&
                       !XPinyinPhrase_isReady() &&
                       XPinyinPhrase_count() == 0,
                   "坏 bin trunc 整体拒绝（bin 步⑤ 上界 56>40）");
        /* _badmagic 单列：magic 'XIPX' 不符被嗅探当文本 → 逐行解析（
         * 已仿真该文件按文本恰 0 合法行）→ 解析全坏负缓存。断言形状
         * 与 bin 拒绝相同但覆盖文本路径，覆盖面不同。 */
        XPinyinPhrase_setPath(
            "../Library/VirtualKeyboard/test/phrases_test_fixture_badmagic.bin");
        xkb_expect(!XPinyinPhrase_reload() &&
                       !XPinyinPhrase_isReady() &&
                       XPinyinPhrase_count() == 0,
                   "坏 bin badmagic 覆盖文本路径（magic 不符→文本解析全"
                   "坏→负缓存）");

        /* c) 黄金一致断言：txt 与 bin 加载后 find 结果逐位一致——
         *    412×412 全双音节键扫描 + 资产内全部 328 个真实三音节组抽
         *    样（显式表，与 phrases_zh.txt 内容锁定），各自喂入 64 位
         *    FNV-1a 滚动摘要（测试内 8 行实现，零依赖），断言 A==B 且
         *    两次 count 相等；另对 ni hao / shi shi / xi an 抽查组显式
         *    逐条比对（不依赖摘要碰撞自由度）。 */
        xkb_expect(XPinyinTable_syllableIdOf("shi", &shiId) &&
                       XPinyinTable_syllableIdOf("xi", &xiId) &&
                       XPinyinTable_syllableIdOf("an", &anId),
                   "XIPB 黄金扫描 syllableIdOf（shi/xi/an）");
        {
            /* 资产内全部真实三音节组（由 phrases_zh.txt 机械提取；
             * 改词库须重新提取同步，kTripleCount==328 锁规模）。 */
            static const char* const kTripleSyllables[] = {
            "ai qing pian", "an quan dai", "bai fen bi", "ban gong lou", "ban gong shi",
            "ban jue sai", "ban ma xian", "ban zhu ren", "bao ling qiu",
            "bei bing yang", "bei ji xiong", "bi ji ben", "bi ye sheng", "bi ye zheng",
            "bian li dian", "bing qi lin", "bo wu guan", "bu ji ge", "bu ke qi",
            "bu xi ban", "bu xing jie", "cai pan yuan", "cai shi chang", "can ji ren",
            "can jin zhi", "chang fang xing", "chang jing lu", "chao jin lu",
            "chao sheng bo", "cheng ji dan", "cheng wu yuan", "chong dian qi",
            "chong dian xian", "chu zu che", "chuan gan qi", "chuang hong deng",
            "chuang tou gui", "da jie ju", "da qi ceng", "da shu ju", "da ti qin",
            "da tou zhen", "da xi yang", "da xiong mao", "da xue sheng", "da yin ji",
            "da zi ran", "dan ding he", "dan xing dao", "deng ji pai",
            "dian bing xiang", "dian dong che", "dian dong ji", "dian fan guo",
            "dian feng shan", "dian hua ka", "dian shi ji", "dian ying jie",
            "dian ying piao", "dian ying yuan", "dian zi qin", "dong bei hu",
            "dong hua pian", "dong wu yuan", "dong zuo pian", "dou ban jiang",
            "dui bu qi", "fa dian ji", "fa dong ji", "fang bian mian", "fang da jing",
            "fang xiang pan", "fei ji chang", "fei xing yuan", "fen zi qian",
            "fu dao ban", "fu dao yuan", "fu jia shi", "fu wu qi", "fu wu yuan",
            "fu yin ji", "fu zuo yong", "gan lan qiu", "gong cheng shi", "gong ji jin",
            "gong jiao che", "gong ju xiang", "gong zuo fu", "gong zuo ri",
            "gong zuo zheng", "gu shi pian", "guan cha li", "ha mi gua", "han xiu cao",
            "hang tian yuan", "he chang tuan", "he dian zhan", "he wu qi",
            "hong lv deng", "hong wai xian", "hou bei xiang", "hou che shi",
            "hou ji shi", "hu kou ben", "hu lian wang", "hu luo bo", "hua zhuang pin",
            "huan niao bu", "huang jin zhou", "hui yi shi", "hui yuan ka",
            "huo che zhan", "huo long guo", "ji lu pian", "ji nian pin", "ji suan ji",
            "ji suan qi", "ji yi li", "jia shi sai", "jia shi yuan", "jia shi zheng",
            "jia you zhan", "jia zhang hui", "jia zi gu", "jian hu ren", "jian zhu shi",
            "jian zi sheng", "jiang xue jin", "jiao wu chu", "jie li sai",
            "jin biao sai", "jin si hou", "jin zhen gu", "jing shi pai", "jiu hu che",
            "ju qing pian", "kai chang bai", "ke cheng biao", "ke dai biao",
            "ke huan pian", "ke jian cao", "ke xue jia", "kong bu pian",
            "kong shou dao", "kong xin cai", "kuai di yuan", "kuang quan shui",
            "la ba zhou", "la du zi", "la ji tong", "li fa shi", "liao tian shi",
            "lie che yuan", "ling jiang tai", "liu lan qi", "liu shui xian",
            "liu xue sheng", "lu xiang ji", "lu yin ji", "lv xing she", "lv you tuan",
            "ma la song", "ma xi tuan", "mai ke feng", "man hua jia", "mao mao chong",
            "mao tou ying", "mei guan xi", "mi hou tao", "mian bao che",
            "mian shui dian", "mo tuo che", "nian zhong jiang", "ning meng shui",
            "pai chu suo", "peng you quan", "ping pang qiu", "pu gong ying",
            "qi che zhan", "qi ye jia", "qiao ke li", "qing jie gong", "re shui qi",
            "ren min bi", "ren xing dao", "ren yi qiu", "sai long zhou", "san fen qiu",
            "san jiao xing", "san lun che", "sao miao yi", "sha chen bao",
            "shai tai yang", "shan di che", "shao nian gong", "she ji shi",
            "she xiang ji", "she xiang tou", "shen fen zheng", "sheng chan xian",
            "shi fa zhan", "shi gong zhong", "shi yan shi", "shou cang jia",
            "shou dian tong", "shou feng qin", "shou huo yuan", "shou men yuan",
            "shou piao yuan", "shou yin ji", "shu ju ku", "shu ju xian",
            "shu zhuang tai", "shui long tou", "shun feng che", "si he yuan",
            "song hua dan", "tai ji quan", "tai ping yang", "tai quan dao",
            "tai yang jing", "tai yang neng", "tan ce qi", "tao tai sai", "ti yu guan",
            "tian hua ban", "ting che chang", "tong zhi shu", "tu shu guan",
            "tui xiu jin", "tuo yuan xing", "wai mai yuan", "wan zi xi",
            "wang yuan jing", "wei bo lu", "wei sheng su", "wei sheng zhi",
            "wen hua gong", "wen jian jia", "wen ju he", "wen xue jia", "wu xia pian",
            "wu xian wang", "xi chen qi", "xi fa shui", "xi hong shi", "xi ju pian",
            "xi lan hua", "xi wan ji", "xi yi ji", "xia shui dao", "xian ren zhang",
            "xian shi ping", "xian shi qi", "xian wei jing", "xian ya dan",
            "xiang ri kui", "xiang xiang li", "xiao fang che", "xiao fang dui",
            "xiao fang yuan", "xiao fei zhe", "xiao huo zi", "xiao jiao che",
            "xiao mai bu", "xiao ti qin", "xiao xue sheng", "xin dian tu",
            "xin neng yuan", "xing li xiang", "xing qi ri", "xing qi tian",
            "xing qu ban", "xue sheng zheng", "ya sui qian", "yan chang hui",
            "yan jiu sheng", "yan zheng ma", "yang lao jin", "yang sheng qi",
            "yao ji shi", "yao kong qi", "yao qing sai", "yi bei zi", "yi shu jia",
            "yin du yang", "yin hang ka", "yin shui ji", "yin yue hui", "yin yue jia",
            "ying huo chong", "ying ye yuan", "you di yuan", "you le chang",
            "you le yuan", "you yong chi", "yu gao pian", "yu hang yuan", "yu mao qiu",
            "yuan xiao jie", "yuan zhu bi", "yuan zi dan", "yun dong hui",
            "yun dong yuan", "yun ji suan", "zhan lan guan", "zhan lan hui",
            "zhan zheng pian", "zhao xiang ji", "zheng fang xing", "zhi pian ren",
            "zhi shi pai", "zhi wu yuan", "zhi yuan zhe", "zhong dian zhan",
            "zhong xue sheng", "zhu chi ren", "zhu xue jin", "zhu yi li",
            "zhuo mu niao", "zi jia you", "zi lai shui", "zi wai xian", "zi xing che",
            "zi you xing", "zong jing li", "zuo qu jia", "zuo ye ben",
            };
            const int kTripleCount =
                (int)(sizeof(kTripleSyllables) / sizeof(kTripleSyllables[0]));
            xkb_expect(kTripleCount == 328,
                       "三音节组抽查表规模 328（与 phrases_zh.txt 锁定）");

            /* txt 黄金摘要 A。 */
            XPinyinEngine_resetComposition(&ime);
            XPinyinPhrase_setPath(
                "../Library/VirtualKeyboard/phrases_zh.txt");
            XPinyinPhrase_reload();
            if (XPinyinPhrase_isReady())
                imeGoldenScan(&hashA, &countA, kTripleSyllables,
                              kTripleCount, niId, haoId, shiId, xiId, anId);
            else
                fprintf(stderr,
                        "XVirtualKeyboard test: SKIP ⑥e 黄金一致断言（txt 资产"
                        "缺席环境）\n");

            /* bin 摘要 B（V3 默认资产口径）。 */
            XPinyinEngine_resetComposition(&ime);
            XPinyinPhrase_setPath(NULL);
            XPinyinPhrase_reload();
            if (XPinyinPhrase_isReady())
            {
                imeGoldenScan(&hashB, &countB, kTripleSyllables,
                              kTripleCount, niId, haoId, shiId, xiId, anId);
                if (hashA != 0)
                    xkb_expect(hashA == hashB && countA == countB &&
                                   countB == 3143u,
                               "黄金一致：txt 与 bin 全双音节键扫描摘要相"
                               "等且 count 同为 3143（V3 默认资产口径）");
            }
            else
                fprintf(stderr,
                        "XVirtualKeyboard test: SKIP ⑥e 黄金一致断言（bin 默认"
                        "资产缺席环境）\n");

            /* 抽查组显式逐条比对（ni hao / shi shi / xi an，txt 与 bin
             * 逐位一致）。 */
            XPinyinEngine_resetComposition(&ime);
            XPinyinPhrase_setPath(
                "../Library/VirtualKeyboard/phrases_zh.txt");
            XPinyinPhrase_reload();
            if (XPinyinPhrase_isReady())
            {
                imeGoldenSpotKeys(niId, haoId, shiId, xiId, anId);
                XPinyinEngine_resetComposition(&ime);
                XPinyinPhrase_setPath(NULL);
                XPinyinPhrase_reload();
                if (XPinyinPhrase_isReady())
                    imeGoldenSpotKeys(niId, haoId, shiId, xiId, anId);
                else
                    fprintf(stderr,
                            "XVirtualKeyboard test: SKIP ⑥e 抽查 bin 侧（默认资"
                            "产缺席环境）\n");
            }
            else
                fprintf(stderr,
                        "XVirtualKeyboard test: SKIP ⑥e 抽查 txt 侧（txt 资产缺"
                        "席环境）\n");
        }
        XPinyinEngine_resetComposition(&ime);
    }

#endif /* XKEYBOARD_IME_PHRASE_ON */
#endif /* XKEYBOARD_IME_ON */

    /* ================================================================
     * ⑦ 虚拟键盘框架化回归锁（XVIRTUALKEYBOARD_ON 门控；设计 testPlan
     *    回归锁·C：①守护边沿矩阵/②hints→布局映射/③shift·caps/④长按
     *    重复/⑤altKeys/⑥closeOnReturn + 新类契约与单例同指针锚）。
     *    框架未落地（XGuiConfig.h 未注册宏）时整段编译出——既有段落
     *    保持原样全绿；落地后本段激活收口新语义。
     *
     *    【门禁对齐点】本段引用的 Src 侧新 API 按设计签名 + 库命名惯
     *    例（函数=X类型名_功能名、self 首参；单例=X*_instance()）推
     *    导。落地命名如有出入，以 Src/XGui/VirtualKeyboard 实际
     *    头文件为准调整引用（断言语义不变）。推导假设已在设计评审
     *    notes 声明的：preeditText 返回 const char* 借用（对齐
     *    XLineEdit_text 惯例）；SelectionListModel.dataAt 返回新建
     *    XVariant*（XVariantType_String 承载，设计 apiMapping#3/4）。
     * ================================================================ */
#if XVIRTUALKEYBOARD_ON
    /* 长按重复节拍断言需要事件调度器泵定时器：幂等补建应用单例
     * （XGuiApplication_create_ex 对既有实例返回同指针）。 */
    {
        XGuiApplication* app =
            XGuiApplication_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, 0, NULL);
        xkb_expect(app != NULL, "⑦ 前置：应用单例可建（重复节拍泵）");
    }
    {
        XVirtualKeyboardInputContext* ctx =
            XVirtualKeyboardInputContext_instance();
        XVirtualKeyboardSettings* settings =
            XVirtualKeyboardSettings_instance();

        /* ---- ⑦.0 新类契约与单例同指针。 ---- */
        xkb_expect(ctx != NULL, "InputContext 进程单例可取");
        xkb_expect(XVirtualKeyboardInputContext_instance() == ctx,
                   "InputContext 单例同指针（两次 instance 恒等）");
        {
            XVirtualKeyboardInputEngine* eng1 =
                XVirtualKeyboardInputContext_inputEngine(ctx);
            XVirtualKeyboardInputEngine* eng2 =
                XVirtualKeyboardInputContext_inputEngine(ctx);
            xkb_expect(eng1 != NULL && eng1 == eng2,
                       "inputEngine 同指针（上下文唯一引擎）");
        }
        {
            XVirtualKeyboard* panel1 = XGuiApplication_virtualKeyboard();
            XVirtualKeyboard* panel2 = XGuiApplication_virtualKeyboard();
            xkb_expect(panel1 != NULL && panel1 == panel2,
                       "virtualKeyboard 默认面板单例同指针");
        }
        xkb_expect(settings != NULL &&
                       XVirtualKeyboardSettings_instance() == settings,
                   "Settings 进程单例同指针");
        xkb_expect(XVirtualKeyboardSettings_keyboardEnabled(settings),
                   "总开关默认开（keyboardEnabled 默认 true）");

        /* ---- ⑦.1 守护边沿矩阵（dismissFix + hide 方向）：guardTick
         *     三段统一边沿，合成 XTimerEvent 按 m_guardTimer id 直派
         *     （与真实 timerEvent 同 vtable 路径，不依赖 200ms 周期）。 */
        {
            XVirtualKeyboard* kg = XVirtualKeyboard_create(NULL, 0);
            XLineEdit* ea = XLineEdit_create(NULL, 0);
            XLineEdit* eb = XLineEdit_create(NULL, 0);
            XWidget* plain = XWidget_create(NULL, 0);
            XVirtualKeyboard_setGeometry(kg, 0, 0, 400, 160);

            /* 边沿 NULL→A：焦点落编辑框 1 tick 弹出（show 方向）。 */
            XWidget_setFocus((XWidget*)ea);
            xkb_pumpGuardTick(kg);
            xkb_expect(kg->m_popped &&
                           XVirtualKeyboard_textArea(kg) == (XWidget*)ea,
                       "守护：焦点落编辑框 1 tick 弹出（边沿 NULL→A）");

            /* 守护兜底不重弹（按下位置驱动定版后本段降级为兜底语义）：
             * closePopup 同步 m_prevFocus=当前焦点采样，此后仅泵守护
             * tick（无 PRESS、无焦点边沿）恒收——重弹主判据已移交按下
             * 路径（⑦.1b 同框 retap 重弹锁），守护无边沿不得自行翻出。 */
            XVirtualKeyboard_closePopup(kg);
            {
                int tick;
                for (tick = 0; tick < 3; ++tick) {
                    xkb_pumpGuardTick(kg);
                    xkb_expect(!kg->m_popped,
                               "closePopup 后仅泵守护 tick 不重弹（无边沿"
                               "兜底不翻出；同框 retap 重弹归按下路径"
                               "⑦.1b）");
                }
            }

            /* 换框 B：边沿 A→B 重绑重弹且 m_target==B（多框跟随）。 */
            XWidget_setFocus((XWidget*)eb);
            xkb_pumpGuardTick(kg);
            xkb_expect(kg->m_popped &&
                           XVirtualKeyboard_textArea(kg) == (XWidget*)eb,
                       "守护：焦点切 B 重弹且 m_target==B");

            /* hide 方向（评审#4）：焦点边沿迁入取焦点的非编辑控件 →
             * 收层（对齐 Qt evaluateInputPanelVisible=m_visible&&
             * (focusObject&&inputMethodAccepted()) 的 hide 臂）。 */
            XWidget_setFocus(plain);
            xkb_pumpGuardTick(kg);
            xkb_expect(!kg->m_popped, "焦点迁入非编辑控件收层（hide 方向）");

            /* 焦点 NULL（点空白）→ 收层；回 A → 重弹（往返路径）。 */
            XWidget_setFocus((XWidget*)ea);
            xkb_pumpGuardTick(kg);
            xkb_expect(kg->m_popped, "守护：回 A 重弹（往返路径）");
            XWidget_clearFocus((XWidget*)ea);
            xkb_pumpGuardTick(kg);
            xkb_expect(!kg->m_popped, "焦点 NULL（点空白）收层");

            /* popup 直呼不受抑制（保底契约）。 */
            XVirtualKeyboard_popup(kg, (XWidget*)ea);
            xkb_expect(kg->m_popped, "popup 直呼不受抑制态拦截");
            XVirtualKeyboard_closePopup(kg);

            /* WA14 opt-out：编辑控件 init 置位（E 契约）；置掉后守护
             * 边沿不弹（接受判据=supportedTarget&&testAttribute(14)，
             * Qt qwidget.cpp:9057-9065 兜底口径；opt-out 契约在按下位
             * 置驱动定版后仍双向成立——守护边沿不弹（本锁）+ 按下路径
             * 不弹/已弹收层（⑦.1b），两路 accept 判据全等）。 */
            xkb_expect(XWidget_testAttribute(
                           (XWidget*)ea,
                           XWidgetAttribute_InputMethodEnabled),
                       "编辑控件 init 置位 WA_InputMethodEnabled");
            XWidget_setAttribute((XWidget*)ea,
                                 XWidgetAttribute_InputMethodEnabled, false);
            XWidget_setFocus((XWidget*)ea);
            xkb_pumpGuardTick(kg);
            xkb_expect(!kg->m_popped,
                       "WA_InputMethodEnabled=false 守护不弹（opt-out）");
            XWidget_setAttribute((XWidget*)ea,
                                 XWidgetAttribute_InputMethodEnabled, true);

            /* 已弹则收层（守护②′稳态复核）：WA14 复位后点空白→回框
             * 1 tick 弹出；再原地置掉 WA14（焦点不变、无边沿）→ 守护
             * tick 幂等收层——accept 可在焦点不变时翻转（Qt
             * evaluateInputPanelVisible hide 臂的轮询化）。 */
            XWidget_clearFocus((XWidget*)ea);
            xkb_pumpGuardTick(kg);
            XWidget_setFocus((XWidget*)ea);
            xkb_pumpGuardTick(kg);
            xkb_expect(kg->m_popped, "前置：WA14 复位回框 1 tick 弹出");
            XWidget_setAttribute((XWidget*)ea,
                                 XWidgetAttribute_InputMethodEnabled, false);
            xkb_pumpGuardTick(kg);
            xkb_expect(!kg->m_popped,
                       "原地 opt-out 已弹则收层（守护稳态复核）");
            XWidget_setAttribute((XWidget*)ea,
                                 XWidgetAttribute_InputMethodEnabled, true);

            /* autoPopup=false 行为不变（既有口径）：收层随层停守护，
             * 焦点变化不再弹；重开守护自愈常驻（demo 页 -1 段口径）。 */
            XVirtualKeyboard_setAutoPopup(kg, false);
            XWidget_setFocus((XWidget*)eb);
            xkb_pumpGuardTick(kg); /* 守护已停：合成 tick 无 id 可命中（幂等） */
            xkb_expect(!kg->m_popped, "autoPopup=false 焦点变化不弹");
            XVirtualKeyboard_setAutoPopup(kg, true);
            xkb_expect(kg->m_guardTimer != XTIMER_INVALID_ID,
                       "autoPopup 重开守护自愈常驻");

            /* 总开关（XGui 扩展，Qt 无直接等价物）：关 → 边沿分支走
             * closePopup 路径（已弹层收起）+ 重开失效；重开恢复。 */
            XVirtualKeyboard_popup(kg, (XWidget*)ea);
            xkb_expect(kg->m_popped, "前置：总开关切换前弹层可见");
            XVirtualKeyboardSettings_setKeyboardEnabled(settings, false);
            XWidget_setFocus((XWidget*)eb);
            xkb_pumpGuardTick(kg);
            xkb_expect(!kg->m_popped,
                       "总开关关：弹层收起（边沿分支走 closePopup 路径）");
            XWidget_setFocus((XWidget*)ea);
            xkb_pumpGuardTick(kg);
            xkb_expect(!kg->m_popped, "总开关关：守护重开失效");
            XVirtualKeyboardSettings_setKeyboardEnabled(settings, true);
            XWidget_setFocus((XWidget*)eb);
            xkb_pumpGuardTick(kg);
            xkb_expect(kg->m_popped &&
                           XVirtualKeyboard_textArea(kg) == (XWidget*)eb,
                       "总开关重开恢复守护弹出");

            XVirtualKeyboard_closePopup(kg);
            XVirtualKeyboard_setParent(kg, NULL, 0);
            XLineEdit_delete_base(ea);
            XLineEdit_delete_base(eb);
            XWidget_delete_base(plain);
            XVirtualKeyboard_delete_base(kg);
        }

        /* ---- ⑦.1b 按下位置驱动（自动弹收主判据，标准触摸 UX；
         *     dismissFix「同框 retap 不重弹」契约作废定版）：经
         *     XWindowSystemInterface_handleMouseEvent 走真实顶层桥→
         *     XWidget_dispatchPointerEvent PRESS 汇聚点→
         *     XGuiApplication_virtualKeyboardNotifyPress 转发→默认面板
         *     单例。双向序列锁：点编辑框（含收起后同框 retap，无需焦点
         *     往返）→弹；点空白/非编辑控件→收；键盘面板自身按键→不受
         *     影响。 ---- */
        {
            XVirtualKeyboard* kp = XGuiApplication_virtualKeyboard();
            XWidget* win = XWidget_create(NULL, 0);
            XLineEdit* ed = XLineEdit_create(win, 0);
            XWidget* blank = XWidget_create(win, 0);
            XWidget_setGeometry(win, 0, 0, 400, 300);
            XWidget_setGeometry((XWidget*)ed, 20, 20, 200, 28);
            XWidget_setGeometry(blank, 20, 80, 200, 40);
            XWidget_show(win);
            XWidget_show((XWidget*)ed);
            XWidget_show(blank);
            xkb_expect(kp != NULL, "⑦.1b 前置：默认面板单例可取");
            xkb_expect(XWidget_windowHandle(win) != NULL,
                       "⑦.1b 前置：测试顶层桥接窗口可建");

            /* 点编辑框→弹出：PRESS 命中即弹（即时，不等 200ms 守护
             * tick；此后焦点经控件点击聚焦落 ed，与弹出时序无关）。 */
            xkb_windowClick(win, 100, 34);
            xkb_expect(kp->m_popped &&
                           XVirtualKeyboard_textArea(kp) == (XWidget*)ed,
                       "按下驱动：PRESS 编辑框弹出（即时，不等守护）");

            /* 收起后同框 retap→重弹（旧 dismissFix 锁作废改写）：焦点
             * 未走（点编辑框不迁移焦点），直接再按同一编辑框，无需焦点
             * 往返即唤回。 */
            XVirtualKeyboard_closePopup(kp);
            xkb_expect(!kp->m_popped, "⑦.1b 前置：closePopup 收层");
            xkb_windowClick(win, 100, 34);
            xkb_expect(kp->m_popped &&
                           XVirtualKeyboard_textArea(kp) == (XWidget*)ed,
                       "按下驱动：收起后同框 retap 重弹（无需焦点往返）");

            /* 键盘面板自身 PRESS→不受影响（子树豁免，弹出态按键照常
             * 输入；不收层、不重绑）。点位取 "a" 键中心（win 局部=面板
             * 原点+键矩形），避免误落关闭/收起控制键。 */
            {
                int aIdx = xkb_findButton(kp, "a");
                xkb_expect(aIdx >= 0, "⑦.1b 前置：弹出布局可定位 a 键");
                if (aIdx >= 0)
                    xkb_windowClick(win,
                                    XWidget_x((XWidget*)kp) +
                                        kp->m_keyRects[aIdx].x +
                                        kp->m_keyRects[aIdx].width / 2,
                                    XWidget_y((XWidget*)kp) +
                                        kp->m_keyRects[aIdx].y +
                                        kp->m_keyRects[aIdx].height / 2);
            }
            xkb_expect(kp->m_popped &&
                           XVirtualKeyboard_textArea(kp) == (XWidget*)ed,
                       "按下驱动：键盘面板自身 PRESS 不收不重绑");

            /* 点非编辑控件→收起（dismiss 语义）。 */
            xkb_windowClick(win, 100, 100);
            xkb_expect(!kp->m_popped,
                       "按下驱动：PRESS 非编辑控件收层（dismiss）");

            /* 点页面空白（childAt 无命中→顶层回退靶）→收起：点空白不
             * 迁移焦点也能收（用户实测缺陷根修点）。 */
            xkb_windowClick(win, 100, 34); /* 先回框重弹，供空白点击收层。 */
            xkb_windowClick(win, 360, 60);
            xkb_expect(!kp->m_popped,
                       "按下驱动：PRESS 页面空白收层（点空白不迁移焦点"
                       "也能收）");

            /* 守护兜底并存：收层后仅泵守护 tick（无 PRESS、无边沿）不
             * 重弹（按下即时 + 守护 200ms 兜底无冲突）。 */
            {
                int tick;
                for (tick = 0; tick < 3; ++tick) {
                    xkb_pumpGuardTick(kp);
                    xkb_expect(!kp->m_popped,
                               "按下驱动收层后守护兜底不重弹（无边沿）");
                }
            }

            /* WA14 opt-out 编辑框按下不弹（press 路径 accept 判据与守
             * 护全等；opt-out 契约双向成立）。 */
            XWidget_setAttribute((XWidget*)ed,
                                 XWidgetAttribute_InputMethodEnabled,
                                 false);
            xkb_windowClick(win, 100, 34);
            xkb_expect(!kp->m_popped,
                       "按下驱动：WA14 opt-out 编辑框 PRESS 不弹");
            XWidget_setAttribute((XWidget*)ed,
                                 XWidgetAttribute_InputMethodEnabled, true);

            /* 清理：收层+解绑后把面板归还顶层（面板挂测试宿主随宿主
             * 销毁会连带删单例，应用析构二次删除）；并停守护统一收口
             * ——autoPopup=true 时守护常驻，后续 ⑦.4/⑦.5 真时钟
             * processEvents 会泵守护 tick，焦点边沿把单例重挂到彼时
             * 聚焦编辑框（顶层）名下，该编辑框销毁即连带删单例
             * （app->m_virtualKeyboard 悬垂：应用析构二次删除、后续
             * press 触发 notifyPress 即 UAF 读）。setAutoPopup(false)
             * 同时令 notifyPress 早退（!autoPopup 直接返回），单例在
             * 本测试进程内完全惰性化；⑦.3/⑦.6/⑦.7 无 processEvents
             * 无同类暴露，⑦.4/⑦.5 的暴露由本行一并闭合。 */
            XVirtualKeyboard_closePopup(kp);
            XVirtualKeyboard_setAutoPopup(kp, false);
            XWidget_setParent((XWidget*)kp, NULL, 0);
            XWidget_delete_base(win);
        }

        /* ---- ⑦.2 hints→布局映射表驱动（Keyboard.qml:42-49 优先级；
         *     键盘消费照 Qt VK 子集：布局 5 位 + LatinOnly 组锁 main）。
         *     numbers=既有 17 键数字布局（Number 模式，:415-430 锁扩
         *     展）；digits=新 12 键；dialpad=新 3×4。digits/dialpad 的
         *     新布局槽位枚举命名未在设计冻结——仅锁 buttonCount+键帽
         *     特征（TODO 门禁对齐：Src 若以新 XKeyboardMode 槽位承载，
         *     可追加 mode 精确断言）。popup 消费的是焦点控件 hints
         *     （经 XInputMethod_defaultQueryHandler 查询桥），故先
         *     setFocus 再 popup。 ---- */
        {
            static const struct
            {
                XInputMethodHints hints;
                uint32_t buttonCount;
                int mode; /* -1=新布局槽位未冻结，仅锁键数。 */
                const char* what;
                const char* modeWhat;
            } kHintsTable[] = {
                { (XInputMethodHints)0, 40, (int)XKeyboardMode_TextLower,
                  "无 hints→main 40 键（缺省回落 main）",
                  "无 hints→TextLower 模式" },
                { XInputMethodHint_LatinOnly, 40, (int)XKeyboardMode_TextLower,
                  "LatinOnly→main 40 键（Latin 锁定）",
                  "LatinOnly→Latin 锁定（TextLower）" },
                { XInputMethodHint_EmailCharactersOnly, 40,
                  (int)XKeyboardMode_TextLower,
                  "EmailCharactersOnly→main（LatinOnly 组同锁）",
                  "EmailCharactersOnly→Latin 锁定" },
                { XInputMethodHint_UrlCharactersOnly, 40,
                  (int)XKeyboardMode_TextLower,
                  "UrlCharactersOnly→main（LatinOnly 组同锁）",
                  "UrlCharactersOnly→Latin 锁定" },
                { XInputMethodHint_PreferNumbers, 17, (int)XKeyboardMode_Number,
                  "PreferNumbers→numbers 17 键（Numeric+符号态）",
                  "PreferNumbers→Number 模式" },
                { XInputMethodHint_FormattedNumbersOnly, 17,
                  (int)XKeyboardMode_Number,
                  "FormattedNumbersOnly→numbers 17 键",
                  "FormattedNumbersOnly→Number 模式" },
                { XInputMethodHint_DialableCharactersOnly, 12, -1,
                  "DialableCharactersOnly→dialpad 12 键（新 3×4）", NULL },
                { XInputMethodHint_DigitsOnly, 12, -1,
                  "DigitsOnly→digits 12 键（新 12 键）", NULL },
            };
            XVirtualKeyboard* kh = XVirtualKeyboard_create(NULL, 0);
            XLineEdit* eh = XLineEdit_create(NULL, 0);
            size_t ti;
            XVirtualKeyboard_setGeometry(kh, 0, 0, 400, 160);
            for (ti = 0; ti < sizeof(kHintsTable) / sizeof(kHintsTable[0]);
                 ++ti) {
                XWidget_setInputMethodHints((XWidget*)eh,
                                            kHintsTable[ti].hints);
                XWidget_setFocus((XWidget*)eh);
                XVirtualKeyboard_popup(kh, (XWidget*)eh);
                xkb_expect(XVirtualKeyboard_buttonCount(kh) ==
                               kHintsTable[ti].buttonCount,
                           kHintsTable[ti].what);
                if (kHintsTable[ti].modeWhat)
                    xkb_expect((int)XVirtualKeyboard_mode(kh) == kHintsTable[ti].mode,
                               kHintsTable[ti].modeWhat);
                XVirtualKeyboard_closePopup(kh);
            }
            /* dialpad/digits 同为 12 键：键帽特征区分——3×4 拨号盘含
             * "*"/"#"，digits 盘含 0..9。负向断言（digits 不含 #/*）
             * 暂不加：XKeyboardLayouts 五型静态表键帽细节为 Src 侧自
             * 由度（TODO 门禁对齐）。 */
            XWidget_setInputMethodHints(
                (XWidget*)eh, XInputMethodHint_DialableCharactersOnly);
            XWidget_setFocus((XWidget*)eh);
            XVirtualKeyboard_popup(kh, (XWidget*)eh);
            xkb_expect(xkb_findButton(kh, "*") >= 0 &&
                           xkb_findButton(kh, "#") >= 0,
                       "dialpad 键帽含 * 与 #（3×4 拨号盘特征）");
            XVirtualKeyboard_closePopup(kh);
            XWidget_setInputMethodHints((XWidget*)eh,
                                        XInputMethodHint_DigitsOnly);
            XVirtualKeyboard_popup(kh, (XWidget*)eh);
            xkb_expect(xkb_findButton(kh, "0") >= 0 &&
                           xkb_findButton(kh, "9") >= 0,
                       "digits 键帽含 0..9（12 键数字盘特征）");
            XVirtualKeyboard_closePopup(kh);
            /* 优先级（Keyboard.qml:42-49 顺序）：Dialable 先于 Digits
             * （多限制位 OR=并集语义照 Qt，ask risk#3；排他类布局位取
             * 高优先）。 */
            XWidget_setInputMethodHints(
                (XWidget*)eh,
                (XInputMethodHints)(XInputMethodHint_DialableCharactersOnly |
                                    XInputMethodHint_DigitsOnly));
            XWidget_setFocus((XWidget*)eh);
            XVirtualKeyboard_popup(kh, (XWidget*)eh);
            xkb_expect(xkb_findButton(kh, "*") >= 0,
                       "排他位并集：Dialable 优先于 Digits（Qt 优先级）");
            XVirtualKeyboard_closePopup(kh);
            /* hints OR 叠加（评审#8）：控件 hints=DigitsOnly + Settings
             * hints=DialableCharactersOnly → 生效布局=dialpad（context
             * 输入提示叠加 OR，qvirtualkeyboardinputcontext_p.cpp:409
             * 口径）。 */
            XWidget_setInputMethodHints((XWidget*)eh,
                                        XInputMethodHint_DigitsOnly);
            XVirtualKeyboardSettings_setInputMethodHints(
                settings, XInputMethodHint_DialableCharactersOnly);
            XWidget_setFocus((XWidget*)eh);
            XVirtualKeyboard_popup(kh, (XWidget*)eh);
            xkb_expect(XVirtualKeyboard_buttonCount(kh) == 12 &&
                           xkb_findButton(kh, "*") >= 0,
                       "hints OR 叠加：控件 DigitsOnly+Settings Dialable→"
                       "dialpad（并集语义）");
            XVirtualKeyboardSettings_setInputMethodHints(
                settings, (XInputMethodHints)0);
            XVirtualKeyboard_closePopup(kh);
            XWidget_setInputMethodHints((XWidget*)eh, (XInputMethodHints)0);
            XVirtualKeyboard_setParent(kh, NULL, 0);
            XLineEdit_delete_base(eh);
            XVirtualKeyboard_delete_base(kh);
        }

        /* ---- ⑦.3 shift/自动大写/大小写锁（shifthandler.cpp:197-306
         *     口径照抄）：单击=临时大写（sticky，Qt 口径不随字符键自
         *     动回落）、间隔内双击=capsLock 钉住、再击=解除；UI 映射
         *     视觉不变：shiftActive→临时切既有 TextUpper 布局、caps
         *     钉住。双击间隔数据源=XStyleHints（评审#11①），注入宽
         *     松值使合成双击（间隔≈0）恒判中。 ---- */
        {
            XVirtualKeyboard* ks = XVirtualKeyboard_create(NULL, 0);
            XLineEdit* es = XLineEdit_create(NULL, 0);
            XStyleHints* styleHints = XGuiApplication_styleHints();
            int savedDbl = styleHints
                               ? XStyleHints_mouseDoubleClickInterval(
                                     styleHints)
                               : 400;
            int abcIdx;
            XVirtualKeyboard_setGeometry(ks, 0, 0, 400, 160);
            if (styleHints)
                XStyleHints_setMouseDoubleClickInterval(styleHints, 4000);
            xkb_expect(!XVirtualKeyboardInputContext_isShiftActive(ctx) &&
                           !XVirtualKeyboardInputContext_isCapsLockActive(
                               ctx) &&
                           !XVirtualKeyboardInputContext_isUppercase(ctx),
                       "shift 三读数默认全 false");
            XWidget_setFocus((XWidget*)es);
            XVirtualKeyboard_popup(ks, (XWidget*)es);
            abcIdx = xkb_findButton(ks, XKEYBOARD_LBL_UPPER);
            xkb_expect(abcIdx >= 0, "主布局可定位 ABC（shift）键");
            /* 前置清态：popup 可能触发 autoCapitalize（空串首位置自动
             * 大写，shifthandler autoCapitalize 口径）——先单击回小写
             * 再进入确定性序列。 */
            if (XVirtualKeyboardInputContext_isShiftActive(ctx))
                xkb_clickAt(ks, abcIdx, true);
            xkb_expect(!XVirtualKeyboardInputContext_isShiftActive(ctx),
                       "前置：shift 清态");
            /* 单击 shift：shiftActive + 临时切 TextUpper 布局。 */
            xkb_clickAt(ks, abcIdx, true);
            xkb_expect(XVirtualKeyboardInputContext_isShiftActive(ctx),
                       "单击 shift：shiftActive");
            xkb_expect(XVirtualKeyboardInputContext_isUppercase(ctx) &&
                           XVirtualKeyboard_mode(ks) == XKeyboardMode_TextUpper,
                       "shiftActive→isUppercase 且临时切 TextUpper 布局"
                       "（视觉映射不变）");
            /* :507-510 链保持：大写态键入 q 输出 'Q'；Qt 口径 shift
             * sticky——字符键入后 shiftActive 保持（不自动回落）。 */
            {
                int qIdx = xkb_findButton(ks, "q");
                xkb_clickAt(ks, qIdx, false);
            }
            xkb_expect(strcmp(XLineEdit_text(es), "Q") == 0,
                       "shift 态键入 q 输出大写 Q（:507-510 链保持）");
            xkb_expect(XVirtualKeyboardInputContext_isShiftActive(ctx),
                       "shift sticky：字符键入后保持（Qt 口径）");
            /* 间隔内再击：capsLock 钉住（双击判定）。 */
            xkb_clickAt(ks, abcIdx, true);
            xkb_expect(XVirtualKeyboardInputContext_isCapsLockActive(ctx) &&
                           XVirtualKeyboardInputContext_isUppercase(ctx) &&
                           XVirtualKeyboard_mode(ks) == XKeyboardMode_TextUpper,
                       "间隔内双击 shift：capsLock 钉住大写");
            {
                int wIdx = xkb_findButton(ks, "w");
                xkb_clickAt(ks, wIdx, false);
            }
            xkb_expect(strcmp(XLineEdit_text(es), "QW") == 0,
                       "capsLock 钉住下键入 w 输出 'W'");
            /* 第三击：capsLock 解除回落小写。 */
            xkb_clickAt(ks, abcIdx, true);
            xkb_expect(!XVirtualKeyboardInputContext_isCapsLockActive(ctx) &&
                           !XVirtualKeyboardInputContext_isShiftActive(ctx) &&
                           XVirtualKeyboard_mode(ks) == XKeyboardMode_TextLower,
                       "再击 shift：capsLock 解除回落 TextLower");
            if (styleHints)
                XStyleHints_setMouseDoubleClickInterval(styleHints, savedDbl);
            XVirtualKeyboard_closePopup(ks);
            XVirtualKeyboard_setParent(ks, NULL, 0);
            XLineEdit_delete_base(es);
            XVirtualKeyboard_delete_base(ks);
        }

        /* ---- ⑦.4 长按重复（重复从面板 400/100ms 移入 engine
         *     600/50ms，宏 XVIRTUALKEYBOARD_REPEAT_FIRST_MS/REPEAT_MS
         *     可覆写供测试注入；与长按弹层 500ms 两套定时器分开）。
         *     退格键为无 altKeys 重复键（重复键不配 altKeys）：真时
         *     钟 + processEvents 泵 engine 定时器断言节拍与 release
         *     杀表；宏默认值经编译期锚（源码顶部 #if 段）。 ---- */
        {
            XVirtualKeyboard* kr = XVirtualKeyboard_create(NULL, 0);
            XLineEdit* er = XLineEdit_create(NULL, 0);
            int bsIdx;
            size_t len0;
            int64_t t0;
            XVirtualKeyboard_setGeometry(kr, 0, 0, 400, 160);
            bsIdx = xkb_findButton(kr, XKEYBOARD_LBL_BACKSPACE);
            xkb_expect(bsIdx >= 0, "主布局可定位退格键");
            /* 宏默认节拍锚（600/50；测试注入覆写时本锚按覆写值失陪）。 */
            xkb_expect(XVIRTUALKEYBOARD_REPEAT_FIRST_MS == 600 &&
                           XVIRTUALKEYBOARD_REPEAT_MS == 50,
                       "重复节拍宏默认 600/50（engine 承载，替代面板 "
                       "400/100）");
            XLineEdit_insert(er, "0123456789");
            XWidget_setFocus((XWidget*)er);
            XVirtualKeyboard_popup(kr, (XWidget*)er);
            /* 按下不释放：立即删 1 字（既有按下触发链），engine 起振
             * 定时器（首重复 600ms 后）。 */
            xkb_clickAt(kr, bsIdx, false);
            len0 = strlen(XLineEdit_text(er));
            xkb_expect(len0 == 9, "退格按下即删 1 字（按下触发链保持）");
            t0 = XDateTime_currentMSecsSinceEpoch();
            xkb_waitMsSince(t0, 700); /* > 首振 600ms。 */
            XGuiApplication_processEvents(XEventLoop_AllEvents);
            xkb_expect(strlen(XLineEdit_text(er)) < len0,
                       "长按 600ms 起振：首重复删除（engine 节拍）");
            t0 = XDateTime_currentMSecsSinceEpoch();
            xkb_waitMsSince(t0, 80); /* > 重复间隔 50ms。 */
            XGuiApplication_processEvents(XEventLoop_AllEvents);
            xkb_expect(strlen(XLineEdit_text(er)) < len0 - 1,
                       "50ms 间隔重复：第二次删除");
            xkb_clickAtReleaseOnly(kr, bsIdx); /* 释放：杀重复表。 */
            {
                size_t lenHold = strlen(XLineEdit_text(er));
                t0 = XDateTime_currentMSecsSinceEpoch();
                xkb_waitMsSince(t0, 200);
                XGuiApplication_processEvents(XEventLoop_AllEvents);
                xkb_expect(strlen(XLineEdit_text(er)) == lenHold,
                           "release 杀表：释放后不再重复");
            }
            XVirtualKeyboard_closePopup(kr);
            XVirtualKeyboard_setParent(kr, NULL, 0);
            XLineEdit_delete_base(er);
            XVirtualKeyboard_delete_base(kr);
        }

        /* ---- ⑦.5 altKeys 长按弹层（alternativeKeys 数据 + 长按
         *     500ms 顶部气泡条；repeat 键不配 altKeys）：550ms ∈
         *     [500,600) 安全窗——弹层已起而重复未振，两种键帽配置
         *     （有/无变体表）下释放都恰落 1 个基础字符。变体选择
         *     上屏断言 TODO 门禁对齐：需气泡条几何/选择公开面落地
         *     后补（ Src 侧未冻结该内部几何）。 ---- */
        {
            XVirtualKeyboard* ka = XVirtualKeyboard_create(NULL, 0);
            XLineEdit* ea2 = XLineEdit_create(NULL, 0);
            int aIdx;
            int64_t t0;
            XVirtualKeyboard_setGeometry(ka, 0, 0, 400, 160);
            aIdx = xkb_findButton(ka, "a");
            xkb_expect(aIdx >= 0, "主布局可定位 a 键");
            XWidget_setFocus((XWidget*)ea2);
            XVirtualKeyboard_popup(ka, (XWidget*)ea2);
            xkb_clickAt(ka, aIdx, false); /* 按住不放。 */
            t0 = XDateTime_currentMSecsSinceEpoch();
            xkb_waitMsSince(t0, 550); /* ∈ [500,600)：弹层起、重复未振。 */
            XGuiApplication_processEvents(XEventLoop_AllEvents);
            xkb_clickAtReleaseOnly(ka, aIdx);
            XGuiApplication_processEvents(XEventLoop_AllEvents);
            xkb_expect(strcmp(XLineEdit_text(ea2), "a") == 0,
                       "长按 550ms 释放恰落 1 个基础字符（弹层/重复两套"
                       "定时器分离）");
            XVirtualKeyboard_closePopup(ka);
            XVirtualKeyboard_setParent(ka, NULL, 0);
            XLineEdit_delete_base(ea2);
            XVirtualKeyboard_delete_base(ka);
        }

        /* ---- ⑦.6 closeOnReturn（Settings 生效子集）：非 MultiLine
         *     回车收面板；MultiLine 不收；默认 false 保持既有「回车
         *     不收层」口径（换行键链 :543-549 保持）。 ---- */
        {
            XVirtualKeyboard* kc = XVirtualKeyboard_create(NULL, 0);
            XLineEdit* ec = XLineEdit_create(NULL, 0);
            int nlIdx;
            XVirtualKeyboard_setGeometry(kc, 0, 0, 400, 160);
            nlIdx = xkb_findButton(kc, XKEYBOARD_LBL_NEWLINE);
            xkb_expect(nlIdx >= 0, "主布局可定位换行键");
            XVirtualKeyboard_popup(kc, (XWidget*)ec);
            xkb_clickAt(kc, nlIdx, true);
            xkb_expect(kc->m_popped,
                       "默认 closeOnReturn=false：回车不收层（既有口径）");
            XVirtualKeyboard_closePopup(kc);
            XVirtualKeyboardSettings_setCloseOnReturn(settings, true);
            XVirtualKeyboard_popup(kc, (XWidget*)ec);
            /* 无头环境合成点击命中依赖宿主几何（0 布局宿主链下键盘有
             * 效可见性/键矩形不可靠）：回车键激活改走 handleButton 单
             * 点（与 closeOnReturn 语义断言无关几何）。 */
            XVirtualKeyboard_handleButton(kc, (uint32_t)nlIdx);
            xkb_expect(!kc->m_popped,
                       "closeOnReturn=true：非 MultiLine 回车收面板");
            XWidget_setInputMethodHints((XWidget*)ec,
                                        XInputMethodHint_MultiLine);
            XVirtualKeyboard_popup(kc, (XWidget*)ec);
            XVirtualKeyboard_handleButton(kc, (uint32_t)nlIdx);
            xkb_expect(kc->m_popped,
                       "closeOnReturn=true：MultiLine 回车不收");
            XVirtualKeyboardSettings_setCloseOnReturn(settings, false);
            XWidget_setInputMethodHints((XWidget*)ec, (XInputMethodHints)0);
            XVirtualKeyboard_closePopup(kc);
            XVirtualKeyboard_setParent(kc, NULL, 0);
            XLineEdit_delete_base(ec);
            XVirtualKeyboard_delete_base(kc);
        }

        /* ---- ⑦.7 commit/keyEvent 落地契约（评审#5 定型）：公共信
         *     号 commitRequested/keyEventRequested → 默认面板在
         *     setTextArea/popup 时连接、closePopup 断开；无面板连接
         *     即丢弃（上下文零绑定文档化契约）。 ---- */
        {
            XVirtualKeyboard* ku = XVirtualKeyboard_create(NULL, 0);
            XLineEdit* eu = XLineEdit_create(NULL, 0);
            XVirtualKeyboard_setGeometry(ku, 0, 0, 400, 160);
            /* 无面板连接：提交/虚键落空（丢弃）。 */
            XVirtualKeyboardInputContext_commit_2(ctx, "X");
            XVirtualKeyboardInputContext_sendKeyClick(ctx, 'k', "k", 0);
            xkb_expect(XLineEdit_text(eu)[0] == '\0',
                       "无面板连接：commit/keyEvent 落空（零绑定契约）");
            /* popup 后面板连接二信号：commit 经写入链落编辑框。 */
            XVirtualKeyboard_popup(ku, (XWidget*)eu);
            XVirtualKeyboardInputContext_commit_2(ctx, "OK");
            xkb_expect(strcmp(XLineEdit_text(eu), "OK") == 0,
                       "commit 往返：commitRequested→面板写入链→编辑框");
            XVirtualKeyboardInputContext_sendKeyClick(ctx, 'k', "k", 0);
            xkb_expect(strcmp(XLineEdit_text(eu), "OKk") == 0,
                       "sendKeyClick：keyEventRequested→面板写入链");
            /* preedit 镜像：setPreeditText 同步组串显示、不直写编辑框
             * （状态镜像契约，apiMapping#13）。 */
            XVirtualKeyboardInputContext_setPreeditText_2(ctx, "ni");
            xkb_expect(xkb_preeditEq(ctx, "ni"),
                       "setPreeditText 往返（组串镜像）");
            xkb_expect(strcmp(XLineEdit_text(eu), "OKk") == 0,
                       "preedit 不直写编辑框（镜像非提交）");
            XVirtualKeyboardInputContext_clear(ctx);
            xkb_expect(xkb_preeditEq(ctx, ""),
                       "clear 清组串镜像");
            /* closePopup 断开二信号：提交恢复丢弃。 */
            XVirtualKeyboard_closePopup(ku);
            XVirtualKeyboardInputContext_commit_2(ctx, "Z");
            xkb_expect(strcmp(XLineEdit_text(eu), "OKk") == 0,
                       "closePopup 断开：commitRequested 无人消费丢弃");
            XVirtualKeyboard_setParent(ku, NULL, 0);
            XLineEdit_delete_base(eu);
            XVirtualKeyboard_delete_base(ku);
        }
    }
#endif /* XVIRTUALKEYBOARD_ON */

    {
        int failures = xkb_failures;
        xkb_failures = 0;
        if (failures == 0) {
            fprintf(stderr, "XVirtualKeyboard test: PASS\n");
            return true;
        }
        fprintf(stderr, "XVirtualKeyboard test: %d assertion(s) failed\n",
                failures);
        return false;
    }
}

#else /* !(XWIDGET_ON && XKEYBOARD_ON) */

bool XKeyboardTest_runAll(void)
{
    return true; /* 开关关闭：无断言可跑，零回归语义。 */
}

#endif /* XWIDGET_ON && XKEYBOARD_ON */
