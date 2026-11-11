#include "XPlatformFontDatabase.h"
#if XPLATFORMINTEGRATION_ON && defined(__linux__) && defined(XINYUE_C_HAS_FONTCONFIG)
#include "XString.h"
#include <fontconfig/fontconfig.h>
#include <stdlib.h> /* atexit：退出钩子（见下方 xpfont_fcInitEnsure）。 */
#include <string.h>

/* fontconfig 的全局库（FcInit 建立的全局配置/字体缓存骨架）没有「创建即得
 * 句柄」的形态，只能用 FcFini 释放；不释放则是进程退出时的 LeakSanitizer
 * 报告。2026-11-11 ASan 分片实测（/tmp/xq_asan_shard0..3.log 四份**同一条**
 * 报告）：`Direct leak of 512 byte(s) in 2 object(s)` +
 * `Indirect leak of 96 byte(s) in 3 object(s)` +
 * `strdup ... FcValueSave`，合计 630 字节 6 次分配，报告栈里**只有
 * libfontconfig**——因为分配发生在 FcInit/FcFontList 内部，而本文件是仓库
 * 唯一的 FcInit 调用点（grep FcInit 仅此一处）。本机探针复现（gcc
 * -fsanitize=address）：FcInit + FcFontList + Destroy 不调 FcFini ⇒ 正是那
 * 630 字节 6 次分配；末尾补 FcFini() ⇒ 干净退出（exit=0）。
 *
 * 为什么放 atexit 而不是每次 collect 后立即 FcFini：FcInit 要解析 /etc/fonts
 * 全套配置（本机一次十余毫秒），而 collect 每次建平台字体库都会跑（XFontComboBox
 * 每次按 fontFilters 重建族列表就建一个库，见 XFontComboBox.c 的 xfcb_populate），
 * 每次释放再重建等于把配置解析开销摊到每次列表刷新。保持「进程期初始化一次、
 * 退出统一释放」——族列表内容不变（同一份 fontconfig 配置），差别只在不再
 * 每次 collect 重解析一遍配置。仓库同款
 * 惯例：XSaveFile.c / XUpgradeStoreAb.c / XPlatformNativeWindow_posix.c 的
 * atexit 兜底。atexit 钩子先于 LeakSanitizer 的退出检查执行（探针实测：钩子
 * 打印后 exit=0、无泄漏报告），所以这是真释放，不是屏蔽报错。
 *
 * atexit 登记失败（几乎不可能）时，collect 收尾当场 FcFini——维持「要么已
 * 登记退出释放、要么当场释放」，不让全局状态留到进程退出。
 *
 * **FcInit 不是幂等调用：每一次 FcInit 都要有且仅有一次 FcFini 配对。**
 * 本机 fontconfig 2.17.1 探针（gcc -fsanitize=address，独立小用例只调
 * FcInit/FcFini）实测：
 *   FcInit×1 + FcFini×0 ⇒ 630 字节 / 6 次分配（= 分片报告那一笔）；
 *   FcInit×1 + FcFini×1 ⇒ 干净退出（exit=0，无报告）；
 *   FcInit×2 + FcFini×1 ⇒ **仍报 630 字节 / 6 次分配**（多出的那次 FcInit
 *                        没有 FcFini 收口）；
 *   FcInit×2 + FcFini×2、×3+×3、×5+×5 ⇒ 全部干净。
 * 由此得出两条约束：① 退出时那次 FcFini 只够抵一次 FcInit，所以 FcInit 必须
 * **只做一次**（`xpfont_fcInitEnsure` 的 s_fcInitialized 守卫就是干这个的）；
 * ② collect 是热路径（XFontComboBox 每次按 fontFilters 重建族列表都建一个
 * 平台字体库 ⇒ 都会进来），逐个 FcInit/FcFini 配对虽也干净，但每次都要重新
 * 解析 /etc/fonts 全套配置（本机首次 FcInit ≈ 21ms，配置已开时一次枚举
 * ≈ 1.2ms），等于把 18 倍开销摊到每次列表刷新，故取「首次 FcInit + 退出
 * FcFini 各一次」。
 * 复现命令（探针留在 /tmp/fcprobe，非仓库文件）：
 *   gcc -std=c99 -fsanitize=address -g p10.c \
 *       $(pkg-config --cflags --libs fontconfig) -o p10 && ./p10 2,1
 *   ⇒ SUMMARY: AddressSanitizer: 630 byte(s) leaked in 6 allocation(s) */

static bool s_fcInitialized = false; /**< FcInit 已成功且尚未 FcFini。 */
static bool s_fcFiniAtExit = false;  /**< 退出钩子已登记（成功即无需手工释放）。 */

/** @brief 退出钩子：释放 fontconfig 全局库（幂等，重复调用无副作用）。 */
static void xpfont_fcFiniAtExit(void)
{
    if (!s_fcInitialized) return;
    s_fcInitialized = false;
    s_fcFiniAtExit = false;
    FcFini();
}

/**
 * @brief 确保 fontconfig 全局库已初始化，首次成功后登记一次退出释放。
 * @return FcInit 成功返回 true；失败返回 false。
 * @warning **FcInit 必须与 FcFini 一一配对，且每次 FcInit 都要配一次
 *          FcFini（本机 fontconfig 2.17.1 探针实测，见文件头注释）。**
 *          所以这里只在首次调用 FcInit，后续 collect 直接复用同一份全局
 *          配置——重复调 FcInit 而只 FcFini 一次，进程退出时必报 630 字节
 *          泄漏，正是本函数此前漏掉的那一笔。
 */
static bool xpfont_fcInitEnsure(void)
{
    if (s_fcInitialized) return true; /* 已初始化：**绝不再调 FcInit**。 */
    if (!FcInit()) return false;
    s_fcInitialized = true;
    /* 只登记一次：钩子里已 FcFini，后续钩子全是空转（也避免多登记后
     * 重复 FcFini 把别人/下一轮的 FcInit 配对打乱）。 */
    s_fcFiniAtExit = (atexit(xpfont_fcFiniAtExit) == 0);
    return true;
}

/** @brief 收尾：退出钩子没登记上就当场释放全局库（登记失败兜底）。 */
static void xpfont_fcReleaseIfUnregistered(void)
{
    if (s_fcInitialized && !s_fcFiniAtExit) xpfont_fcFiniAtExit();
}

static bool xpfont_hasFamily(const XVector* families, const char* family)
{
    size_t i;
    size_t n = XVector_size_base((const XContainer*)families);
    for (i = 0; i < n; ++i) {
        XString* const* item = (XString* const*)XVector_at_base(
            families, (int64_t)i);
        if (item && *item && strcmp(XString_toUtf8(*item), family) == 0)
            return true;
    }
    return false;
}
bool XPlatformFontDatabaseDriver_collect(XVector* families)
{
    FcPattern* pattern;
    FcObjectSet* objects;
    FcFontSet* set;
    bool ok = false;
    int i;
    if (!families || !xpfont_fcInitEnsure()) return false;
    pattern = FcPatternBuild(NULL, FC_SCALABLE, FcTypeBool, FcTrue, NULL);
    objects = FcObjectSetBuild(FC_FAMILY, NULL);
    set = pattern && objects ? FcFontList(NULL, pattern, objects) : NULL;
    if (pattern) FcPatternDestroy(pattern);
    if (objects) FcObjectSetDestroy(objects);
    if (set) {
        for (i = 0; i < set->nfont; ++i) {
            FcChar8* family = NULL;
            if (FcPatternGetString(set->fonts[i], FC_FAMILY, 0, &family) == FcResultMatch && family) {
                if (!xpfont_hasFamily(families, (const char*)family)) {
                    XString* value = XString_create_utf8((const char*)family);
                    if (value) XVector_Push_Back_Base(families, XString*, value);
                }
            }
        }
        FcFontSetDestroy(set);
        ok = true;
    }
    /* 族名已深拷贝进 families（XString_create_utf8），fontconfig 的全局库
     * 在本函数之后不再被需要；仅在 atexit 登记失败时才在这里当场释放。 */
    xpfont_fcReleaseIfUnregistered();
    return ok;
}
#endif
