/**
 * @file       xchannel_test.c
 * @brief      XChannel 通道层专项单元测试（不依赖传输层，进程内自跑）。
 * @details    [2026-11-02 XChannel 重写配套] 本文件覆盖重写后的新接口，
 *             与三条 XVfd 闭环互补：闭环验的是「设备语义正确」，本文件验的是
 *             「通道层机制正确」——点容器、链路虚槽、地址映射边界、装配失败路径。
 *
 *             覆盖分组：
 *               ① XIoPoint 纯数据容器：role/type/tag/符号/生命周期
 *                  （2026-11-03 拆分后含 XIoAnalogPoint 标度容器）
 *               ② XIoAnalogPoint_toRaw 符号语义：有符号补码、无符号钳位（含负数根修回归）
 *               ③ XIoPoint_toEngineering 基类入口分发：数字恒等、模拟标度、除零容错；
 *                  ③b 拆分守门：尺寸收益、isa 判定、登记闸口拒绝、多态删除
 *               ④ XIoLink 虚槽缺省实现：未实现返回 false、isAsync 保守取真
 *               ⑤ 假链路虚槽分派：同步完成 / 异步完成 / 拒绝写入
 *               ⑥ XIoChannelGroup：addPoint 位冲突、role 越界、contains 的 O(1) 语义、
 *                  takePoint/removeRole 所有权、recordCount 计数器
 *               ⑦ XModbusLink 地址映射：首地址/末地址/越界/空洞/跨区
 *               ⑧ XModbusLink 订阅：Start 幂等、Stop 断开、订阅数恒为 2
 *               ⑨ XChannelAssembly：装配成功/role 冲突/未挂链路/单点装配
 *
 *             不依赖 socket 与 pty：所有链路行为由进程内假链路模拟，
 *             故本机环回丢包环境下仍可判定。
 *
 *             退出码 = 失败用例数。
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "XCoreApplication.h"
#include "XEventLoop.h"
#include "XDateTime.h"
#include "XChannel/XIoPoint.h"
#include "XChannel/XIoAnalogPoint.h"
#include "XChannel/XIoLink.h"
#include "XChannel/XIoChannelGroup.h"
#include "XChannel/XChannelAssembly.h"

#if XINDUSTRY_ON && XINDUSTRY_CHANNEL_ON

#include "XChannel/XModbusLink.h"

/* ==================== 断言宏（与其他测试同款口径） ==================== */

static int g_pass = 0;
static int g_fail = 0;

/**
 * @brief 记录一条断言。
 * @param cond 条件（true=通过）。
 * @param desc 用例描述（静态字符串）。
 * @return 无。
 */
static void check(bool cond, const char* desc)
{
    if (cond) {
        ++g_pass;
        printf("  [OK] %s\n", desc);
    } else {
        ++g_fail;
        fprintf(stderr, "  [FAIL] %s\n", desc);
    }
}

/* ==================== 假链路（XIoLink 子类） ==================== */

XCLASS_DEFINE_BEGING(XTestChanLink)
XCLASS_DEFINE_ENUM(XTestChanLink, BindRole) = XCLASS_VTABLE_GET_SIZE(XIoLink),
XCLASS_DEFINE_ENUM(XTestChanLink, ReadRole),
XCLASS_DEFINE_ENUM(XTestChanLink, WriteRole),
XCLASS_DEFINE_ENUM(XTestChanLink, IsAsync),
XCLASS_DEFINE_ENUM(XTestChanLink, Start),
XCLASS_DEFINE_ENUM(XTestChanLink, Stop),
XCLASS_DEFINE_END(XTestChanLink)

/** @brief 假链路：记录绑定与写入，可注入拒绝/异步/同步三种行为。 */
typedef struct XTestChanLink {
    XIoLink m_base;            /**< 基类（第一成员）。 */
    bool m_async;              /**< true=声明异步（写受理后不立即落定）。 */
    bool m_rejectWrite;        /**< true=下一次写返回 false（同步拒绝）。 */
    bool m_ackNow;             /**< true=写受理后立即 completeWrite(true)。 */
    int m_bindCount;           /**< BindRole 成功次数。 */
    uint16_t m_lastRole;       /**< 最近一次操作的 role。 */
    int32_t m_lastRaw;         /**< 最近一次写入的原生值。 */
    int32_t m_values[8];       /**< role → 读回值（0..7）。 */
    bool m_hasValue[8];        /**< 该 role 是否有值。 */
    int m_startCount;          /**< Start 调用次数（验幂等）。 */
    int m_stopCount;           /**< Stop 调用次数。 */
} XTestChanLink;

static bool tcl_bindRole(XTestChanLink* self, uint16_t role, uint32_t addr, uint32_t aux)
{
    (void)addr;
    (void)aux;
    if (!self || role >= 8) return false;
    ++self->m_bindCount;
    return true;
}

static bool tcl_readRole(XTestChanLink* self, uint16_t role, int32_t* outRaw)
{
    if (!self || !outRaw || role >= 8 || !self->m_hasValue[role]) return false;
    *outRaw = self->m_values[role];
    return true;
}

static bool tcl_writeRole(XTestChanLink* self, uint16_t role, int32_t raw)
{
    XIoChannelGroup* group;
    if (!self) return false;
    if (self->m_rejectWrite) {
        self->m_rejectWrite = false;
        return false;                       /* 同步拒绝：不发落定信号。 */
    }
    self->m_lastRole = role;
    self->m_lastRaw = raw;
    if (self->m_async) return true;         /* 异步：受理即返回，落定另行驱动。 */
    group = (XIoChannelGroup*)XIoLink_group(&self->m_base);
    if (self->m_ackNow && group) {
        XIoChannelGroup_completeWrite(group, role, true);
    }
    return true;
}

static bool tcl_isAsync(const XTestChanLink* self)
{
    return self ? self->m_async : true;
}

static bool tcl_start(XTestChanLink* self)
{
    if (!self) return false;
    ++self->m_startCount;
    return true;
}

static void tcl_stop(XTestChanLink* self)
{
    if (!self) return;
    ++self->m_stopCount;
}

XVtable* XTestChanLink_class_init(void)
{
    XVTABLE_INIT_DEFAULT(XTestChanLink)
    XVTABLE_INHERIT_XCLASS(XIoLink);
    XVTABLE_OVERLOAD_DEFAULT(EXIoLink_BindRole, tcl_bindRole);
    XVTABLE_OVERLOAD_DEFAULT(EXIoLink_ReadRole, tcl_readRole);
    XVTABLE_OVERLOAD_DEFAULT(EXIoLink_WriteRole, tcl_writeRole);
    XVTABLE_OVERLOAD_DEFAULT(EXIoLink_IsAsync, tcl_isAsync);
    XVTABLE_OVERLOAD_DEFAULT(EXIoLink_Start, tcl_start);
    XVTABLE_OVERLOAD_DEFAULT(EXIoLink_Stop, tcl_stop);
    XCLASS_SHOW_SIZE_DEFAULT(XTestChanLink);
    return XVTABLE_DEFAULT;
}

static void testChanLinkInit(XTestChanLink* link, bool asyncMode)
{
    if (!link) return;
    XIoLink_init(&link->m_base);
    XClassGetVtable(link) = XTestChanLink_class_init();
    memset(&link->m_async, 0, sizeof(XTestChanLink) - sizeof(XIoLink));
    link->m_async = asyncMode;
}

/* ==================== ① XIoPoint 纯数据容器 ==================== */

static void test_point_container(void)
{
    XIoPoint pt;
    int32_t v = -1;

    XIoPoint_init(&pt);
    check(XIoPoint_role(&pt) == 0, "点初始 role = 0");
    check(XIoPoint_type(&pt) == XIoPoint_DigitalInput, "点初始类型 = DI");
    check(XIoPoint_tag(&pt) == NULL, "点初始无 tag");
    check(!XIoPoint_isValid(&pt), "点初始无效");
    check(!XIoPoint_value(&pt, &v), "无值时不返回值");

    check(XIoPoint_setRole(&pt, 42) && XIoPoint_role(&pt) == 42, "设置 role = 42");
    check(!XIoPoint_setRole(&pt, 9999), "role 越界被拒");
    check(XIoPoint_role(&pt) == 42, "越界失败后 role 不变");

    XIoPoint_setType(&pt, XIoPoint_AnalogOutput);
    check(XIoPoint_type(&pt) == XIoPoint_AnalogOutput, "设置类型 = AO");

    XIoPoint_setTag(&pt, "TAG1");
    check(strcmp(XIoPoint_tag(&pt), "TAG1") == 0, "设置 tag");

    XIoPoint_setSigned(&pt, true);
    check(XIoPoint_isSigned(&pt), "设置有符号");
    XIoPoint_setSigned(&pt, false);
    check(!XIoPoint_isSigned(&pt), "取消有符号");

    XIoPoint_setUseHolding(&pt, true);
    check(XIoPoint_useHolding(&pt), "设置 HR 区选");

    XIoPoint_setAutoDelete(&pt, true);
    check(XIoPoint_autoDelete(&pt), "设置自动释放");

    XClassDeinit((XClass*)&pt);
}

/* ==================== ①b XIoAnalogPoint 标度容器 ==================== */

static void test_analog_point_scale(void)
{
    XIoAnalogPoint pt;

    /* 标度四件套（已从 XIoPoint 拆入子类）：负死区按 0 处理。 */
    XIoAnalogPoint_init(&pt);
    XIoPoint_setType(&pt.m_base, XIoPoint_AnalogInput);   /* 分发入口按类型路由。 */
    check(XIoPoint_type(&pt.m_base) == XIoPoint_AnalogInput, "设置类型 = AI");
    check(XIoAnalogPoint_isInstance(&pt.m_base), "init 后即本类实例");
    XIoAnalogPoint_setScale(&pt, 2, 0, 100, -5);
    check(XIoPoint_toEngineering(&pt.m_base, 50) == 200,
          "除率为 0 按 1：50*2/1+100 = 200（经基类入口分发）");

    XClassDeinit((XClass*)&pt);
}

/* ==================== ② XIoAnalogPoint_toRaw 符号语义（负数根修回归） ==================== */

static void test_point_toRaw_signed(void)
{
    XIoAnalogPoint pt;

    /* 有符号点：负值必须是 16 位补码，不得被钳成 0。 */
    XIoAnalogPoint_init(&pt);
    XIoPoint_setSigned(&pt.m_base, true);
    XIoAnalogPoint_setScale(&pt, 1, 1, 0, 0);
    check(XIoAnalogPoint_toRaw(&pt, -2000) == (uint16_t)0xF830,
          "有符号点：-2000 → 0xF830（16 位补码）");
    check(XIoAnalogPoint_toRaw(&pt, -1) == (uint16_t)0xFFFF,
          "有符号点：-1 → 0xFFFF");
    check(XIoAnalogPoint_toRaw(&pt, 2000) == (uint16_t)2000,
          "有符号点：正值原样");
    check(XIoAnalogPoint_toRaw(&pt, 0) == 0, "有符号点：0 → 0");

    /* 无符号点：负值钳 0，超限钳 65535。 */
    XIoPoint_setSigned(&pt.m_base, false);
    check(XIoAnalogPoint_toRaw(&pt, -2000) == 0, "无符号点：负值钳 0");
    check(XIoAnalogPoint_toRaw(&pt, 70000) == 65535, "无符号点：超限钳 65535");
    check(XIoAnalogPoint_toRaw(&pt, 30000) == 30000, "无符号点：范围内原样");

    XClassDeinit((XClass*)&pt);
}

static void test_point_toRaw_scale(void)
{
    XIoAnalogPoint pt;

    /* 工程值 → 原生值：raw = (v - offset) * den / num。
     * 注意两个方向的除号相反：
     *   工程值 = raw * num / den（见 XIoAnalogPoint_toEngineering）
     *   raw     = 工程值 * den / num（本函数）
     * 故 num=1/den=10 表示「工程值放大 10 倍才是原生值」。 */
    XIoAnalogPoint_init(&pt);
    XIoAnalogPoint_setScale(&pt, 100, 1, 0, 0);     /* 工程值 = raw*100/1：raw = v/100 */
    check(XIoAnalogPoint_toRaw(&pt, 6900) == 69, "标度 num=100：6900 → 69");

    XIoAnalogPoint_setScale(&pt, 100, 1, 50, 0);    /* 带偏移：raw = (v-50)/100 */
    check(XIoAnalogPoint_toRaw(&pt, 6450) == 64, "带偏移：(6450-50)/100 = 64");

    /* 除零容错。 */
    XIoAnalogPoint_setScale(&pt, 0, 0, 0, 0);
    check(XIoAnalogPoint_toRaw(&pt, 42) == 42, "num/den 为 0 按 1：42 → 42");

    XClassDeinit((XClass*)&pt);
}

/* ==================== ③ XIoPoint_toEngineering（基类入口分发） ==================== */

static void test_point_toEngineering(void)
{
    XIoAnalogPoint pt;

    XIoAnalogPoint_init(&pt);
    XIoPoint_setType(&pt.m_base, XIoPoint_AnalogInput);   /* 分发入口按类型路由。 */
    XIoAnalogPoint_setScale(&pt, 1, 100, 0, 0);
    check(XIoPoint_toEngineering(&pt.m_base, 2500) == 25, "2500/100 = 25");

    XIoAnalogPoint_setScale(&pt, 1, 10, 0, 0);
    check(XIoPoint_toEngineering(&pt.m_base, 69) == 6, "69/10 整数截断 = 6");

    XIoAnalogPoint_setScale(&pt, 1, 1, 100, 0);
    check(XIoPoint_toEngineering(&pt.m_base, 5) == 105, "偏移：5+100 = 105");

    /* 数字量：0/1 直通（无标度，恒等返回）。 */
    {
        XIoPoint d;
        XIoPoint_init(&d);
        check(XIoPoint_toEngineering(&d, 1) == 1, "数字点恒等：1 → 1");
        check(XIoPoint_toEngineering(&d, 0) == 0, "数字点恒等：0 → 0");
        XClassDeinit((XClass*)&d);
    }

    XClassDeinit((XClass*)&pt);
}

/* ==================== ③b 数字/模拟拆分守门 ==================== */

static void test_point_split_gate(void)
{
    /* 尺寸收益：数字点（基类）必须严格小于模拟点（基类 + 标度四件套）。 */
    printf("[size] XIoPoint=%d XIoAnalogPoint=%d\n",
           (int)sizeof(XIoPoint), (int)sizeof(XIoAnalogPoint));
    check(sizeof(XIoPoint) < sizeof(XIoAnalogPoint),
          "数字点尺寸 < 模拟点尺寸（拆分生效）");

    /* isa 判定：基类点不是模拟量点。 */
    {
        XIoPoint d;
        XIoPoint_init(&d);
        check(!XIoAnalogPoint_isInstance(&d), "基类点 isInstance = false");
        XClassDeinit((XClass*)&d);
    }

    /* 登记闸口：AI/AO 类型必须是 XIoAnalogPoint 实例，裸基类点被拒。 */
    {
        XIoChannelGroup* grp = XIoChannelGroup_create();
        XIoPoint* badAi = XIoPoint_create_ex(XCLASS_DEFAULT_MEMORY_TYPE);
        XIoAnalogPoint* okAi = XIoAnalogPoint_create_ex(XCLASS_DEFAULT_MEMORY_TYPE);
        if (!grp || !badAi || !okAi) {
            if (grp) XClassDelete((XClass*)grp);
            if (badAi) XClassDelete((XClass*)badAi);
            if (okAi) XClassDelete((XClass*)okAi);
            return;
        }
        XIoPoint_setRole(badAi, 11);
        XIoPoint_setType(badAi, XIoPoint_AnalogInput);
        check(!XIoChannelGroup_addPoint(grp, badAi, true),
              "AI 类型裸基类点被登记闸口拒绝");
        XIoPoint_setRole(&okAi->m_base, 12);
        XIoPoint_setType(&okAi->m_base, XIoPoint_AnalogInput);
        check(XIoChannelGroup_addPoint(grp, &okAi->m_base, true),
              "AI 类型模拟点登记成功");
        XIoPoint_setRole(badAi, 0);
        XClassDelete((XClass*)badAi);
        XClassDelete((XClass*)grp);   /* autoDelete 连带释放 okAi。 */
    }

    /* 多态删除：基类指针 XClassDelete 走子类虚表，尺寸对齐无踩内存。 */
    {
        XIoAnalogPoint* apt = XIoAnalogPoint_create_ex(XCLASS_DEFAULT_MEMORY_TYPE);
        XIoPoint* base;
        if (!apt) return;
        base = &apt->m_base;
        XIoPoint_setType(base, XIoPoint_AnalogOutput);   /* 分发入口按类型路由。 */
        XIoAnalogPoint_setScale(apt, 3, 1, 7, 0);
        check(XIoPoint_toEngineering(base, 10) == 37, "删除前标度可用 10*3+7 = 37");
        XClassDelete(base);           /* 基类句柄删除子类实例。 */
        check(true, "基类句柄多态删除模拟点不崩溃");
    }
}

/* ==================== ④ XIoLink 缺省实现 ==================== */

static void test_link_defaults(void)
{
    XIoLink link;
    int32_t v = 0;

    XIoLink_init(&link);
    check(!XIoLink_bindRole_base(&link, 0, 0, 0), "XIoLink 缺省 BindRole 返回 false");
    check(!XIoLink_readRole_base(&link, 0, &v), "XIoLink 缺省 ReadRole 返回 false");
    check(!XIoLink_writeRole_base(&link, 0, 0), "XIoLink 缺省 WriteRole 返回 false");
    check(XIoLink_isAsync_base(&link), "XIoLink 缺省 isAsync = true（保守）");
    check(XIoLink_start_base(&link), "XIoLink 缺省 Start 成功");
    XIoLink_stop_base(&link);                /* 不应崩溃 */
    check(true, "XIoLink 缺省 Stop 不崩溃");

    XIoLink_setGroup(&link, (void*)0x1234);
    check(XIoLink_group(&link) == (void*)0x1234, "链路记录所属组");
    XIoLink_setGroup(&link, NULL);
    check(XIoLink_group(&link) == NULL, "链路解绑组");

    XClassDeinit((XClass*)&link);
}

/* ==================== ⑤ 假链路虚槽分派 ==================== */

static void test_link_dispatch(void)
{
    XTestChanLink link;
    int32_t v = 0;

    /* 同步链路 */
    testChanLinkInit(&link, false);
    check(!XIoLink_isAsync_base((XIoLink*)&link), "同步链路 isAsync = false");
    check(XIoLink_bindRole_base((XIoLink*)&link, 3, 100, 2), "BindRole 成功");
    check(link.m_bindCount == 1, "BindRole 计数 = 1");
    check(!XIoLink_bindRole_base((XIoLink*)&link, 9, 100, 2), "BindRole role 越界被拒");
    check(!XIoLink_readRole_base((XIoLink*)&link, 3, &v), "无值时 ReadRole 返回 false");
    link.m_values[3] = 777;
    link.m_hasValue[3] = true;
    check(XIoLink_readRole_base((XIoLink*)&link, 3, &v) && v == 777, "有值时 ReadRole 命中");

    check(XIoLink_writeRole_base((XIoLink*)&link, 3, 500), "WriteRole 受理");
    check(link.m_lastRaw == 500, "写入值被记录");
    check(XIoLink_start_base((XIoLink*)&link) && link.m_startCount == 1, "Start 计数 = 1");
    XIoLink_stop_base((XIoLink*)&link);
    check(link.m_stopCount == 1, "Stop 计数 = 1");

    /* 同步拒绝 */
    link.m_rejectWrite = true;
    check(!XIoLink_writeRole_base((XIoLink*)&link, 3, 1), "拒绝写入返回 false");

    /* 异步链路 */
    testChanLinkInit(&link, true);
    check(XIoLink_isAsync_base((XIoLink*)&link), "异步链路 isAsync = true");

    XClassDeinit((XClass*)&link);
}

/* ==================== ⑥ XIoChannelGroup ==================== */

static void test_group_basics(void)
{
    XIoChannelGroup* grp = XIoChannelGroup_create();
    XIoPoint* p1 = XIoPoint_create_ex(XCLASS_DEFAULT_MEMORY_TYPE);
    XIoAnalogPoint* p2 = XIoAnalogPoint_create_ex(XCLASS_DEFAULT_MEMORY_TYPE);
    bool b = false;
    int32_t v = 0;

    check(grp != NULL && p1 != NULL && p2 != NULL, "组与点创建");
    if (!grp || !p1 || !p2) return;

    XIoPoint_setRole(p1, 5);
    XIoPoint_setType(p1, XIoPoint_DigitalInput);
    XIoPoint_setRole(&p2->m_base, 7);
    XIoPoint_setType(&p2->m_base, XIoPoint_AnalogOutput);

    check(XIoChannelGroup_recordCount(grp) == 0, "空组记录数 = 0");
    check(XIoChannelGroup_addPoint(grp, p1, true), "登记点 role=5");
    check(XIoChannelGroup_recordCount(grp) == 1, "记录数 = 1");
    check(XIoChannelGroup_contains(grp, p1), "contains 命中");
    check(!XIoChannelGroup_contains(grp, p2), "未登记点 contains = false");
    check(XIoChannelGroup_pointAt(grp, 5) == p1, "按 role 取点（O(1)）");
    check(XIoChannelGroup_pointAt(grp, 6) == NULL, "空位取点返回 NULL");
    check(XIoChannelGroup_pointAt(grp, 9999) == NULL, "role 越界取点返回 NULL");
    /* 位冲突：同一 role 第二个点必须被拒。 */
    check(!XIoChannelGroup_addPoint(grp, p1, true), "同点重复登记被拒");
    {
        XIoPoint* dup = XIoPoint_create_ex(XCLASS_DEFAULT_MEMORY_TYPE);
        XIoPoint_setRole(dup, 5);
        check(!XIoChannelGroup_addPoint(grp, dup, true), "同 role 不同点被拒（位冲突）");
        XClassDelete((XClass*)dup);
    }

    /* role 越界。 */
    {
        XIoPoint* bad = XIoPoint_create_ex(XCLASS_DEFAULT_MEMORY_TYPE);
        bad->m_role = 9999;
        check(!XIoChannelGroup_addPoint(grp, bad, true), "role 越界的点被拒");
        XClassDelete((XClass*)bad);
    }

    check(XIoChannelGroup_addPoint(grp, p2, true), "登记点 role=7");
    check(XIoChannelGroup_recordCount(grp) == 2, "记录数 = 2");

    /* 读：无数据时拒绝。 */
    check(!XIoChannelGroup_readDigitalInput(grp, 5, &b), "无数据时读 DI 返回 false");
    check(!XIoChannelGroup_readAnalogInput(grp, 7, &v), "无数据时读 AI 返回 false");
    /* 类型不符。 */
    check(!XIoChannelGroup_readDigitalInput(grp, 7, &b), "AO 点按 DI 读被拒");
    /* 未挂链路时写拒绝。 */
    check(!XIoChannelGroup_writeDigitalOutput(grp, 5, true), "无链路时写被拒");
    check(!XIoChannelGroup_writeAnalogOutput(grp, 7, 100), "无链路时写 AO 被拒");

    /* 摘除：所有权移交调用方。 */
    check(XIoChannelGroup_takePoint(grp, p1) == p1, "takePoint 摘出并移交所有权");
    check(!XIoChannelGroup_contains(grp, p1), "摘出后 contains = false");
    check(XIoChannelGroup_recordCount(grp) == 1, "摘出后记录数 = 1");
    XClassDelete((XClass*)p1);

    /* removeRole：autoDelete 点当场销毁。 */
    check(XIoChannelGroup_removeRole(grp, 7), "removeRole 摘除 role=7");    check(XIoChannelGroup_recordCount(grp) == 0, "摘除后记录数 = 0");
    check(!XIoChannelGroup_removeRole(grp, 7), "重复摘除返回 false");
    check(!XIoChannelGroup_removeRole(grp, 9999), "越界 role 摘除返回 false");

    XClassDelete((XClass*)grp);
}

static void test_group_feed(void)
{
    XIoChannelGroup* grp = XIoChannelGroup_create();
    XIoPoint* pt = XIoPoint_create_ex(XCLASS_DEFAULT_MEMORY_TYPE);
    bool b = false;

    if (!grp || !pt) return;
    XIoPoint_setRole(pt, 2);
    XIoPoint_setType(pt, XIoPoint_DigitalInput);
    XIoChannelGroup_addPoint(grp, pt, true);

    check(XIoChannelGroup_feedRole(grp, 2, 1), "首次喂值成功（应有变化沿）");
    check(XIoChannelGroup_readDigitalInput(grp, 2, &b) && b == true, "读回 true");
    check(XIoChannelGroup_isValid(grp, 2), "喂值后数据有效");
    check(!XIoChannelGroup_feedRole(grp, 2, 1), "同值再喂返回 false（无变化）");
    check(XIoChannelGroup_feedRole(grp, 2, 0), "值变化再喂返回 true");
    check(XIoChannelGroup_readDigitalInput(grp, 2, &b) && b == false, "读回 false");
    check(!XIoChannelGroup_feedRole(grp, 9, 1), "未登记 role 喂值返回 false");

    /* 标度换算：点标度 den=10，喂 690 应存 69。 */
    {
        XIoAnalogPoint* a = XIoAnalogPoint_create_ex(XCLASS_DEFAULT_MEMORY_TYPE);
        int32_t v = 0;
        XIoPoint_setRole(&a->m_base, 3);
        XIoPoint_setType(&a->m_base, XIoPoint_AnalogInput);
        XIoAnalogPoint_setScale(a, 1, 10, 0, 0);
        XIoChannelGroup_addPoint(grp, &a->m_base, true);
        XIoChannelGroup_feedRole(grp, 3, 690);
        check(XIoChannelGroup_readAnalogInput(grp, 3, &v) && v == 69, "喂值经标度换算 690→69");
    }

    /* completeWrite 对未登记 role 返回 false。 */
    check(!XIoChannelGroup_completeWrite(grp, 9, true), "未登记 role 写落定返回 false");
    check(XIoChannelGroup_completeWrite(grp, 2, true), "已登记 role 写落定成功");
    check(XIoChannelGroup_isValid(grp, 2), "写落定后数据有效");

    XClassDelete((XClass*)grp);
}

/* ==================== ⑦ ⑧ XModbusLink 地址映射与订阅 ==================== */

#if XINDUSTRY_MODBUS_ON && XMODBUS_HUB_ON

static void test_modbuslink_mapping(void)
{
    XModbusHub* hub = XModbusHub_create();
    XModbusLink* link = XModbusLink_create(hub);
    int area = -1;
    uint16_t addr = 0;

    check(hub != NULL && link != NULL, "Hub 与链路创建");
    if (!hub || !link) return;

    check(XModbusLink_boundCount(link) == 0, "初始绑定数 = 0");
    check(!XModbusLink_queryBinding(link, 0, &area, &addr), "未绑定 role 查询返回 false");

    /* 首地址 / 末地址 / 中间地址。 */
    check(XIoLink_bindRole_base((XIoLink*)link, 0, 100, 2), "绑定 role0 → addr100");
    check(XIoLink_bindRole_base((XIoLink*)link, 1, 200, 2), "绑定 role1 → addr200");
    check(XIoLink_bindRole_base((XIoLink*)link, 2, 150, 2), "绑定 role2 → addr150（中间）");
    check(XModbusLink_boundCount(link) == 3, "绑定数 = 3");

    check(XModbusLink_queryBinding(link, 0, &area, &addr) && addr == 100 && area == 2,
          "查 role0 → (area2, 100)");
    check(XModbusLink_queryBinding(link, 1, &area, &addr) && addr == 200,
          "查 role1（末地址）→ 200");
    check(XModbusLink_queryBinding(link, 2, &area, &addr) && addr == 150,
          "查 role2（中间）→ 150");
    check(!XModbusLink_queryBinding(link, 5, &area, &addr), "未绑定 role5 查询 false");

    /* 越界参数。 */
    check(!XIoLink_bindRole_base((XIoLink*)link, 0, 70000, 2), "地址超 65535 被拒");
    check(!XIoLink_bindRole_base((XIoLink*)link, 0, 100, 9), "区下标越界被拒");
    check(!XIoLink_bindRole_base((XIoLink*)link, 9999, 100, 2), "role 越界被拒");

    /* 跨区：同一 role 不同区互不干扰。 */
    check(XIoLink_bindRole_base((XIoLink*)link, 3, 300, 0), "绑定 role3 到 DI 区");
    check(XModbusLink_queryBinding(link, 3, &area, &addr) && area == 0,
          "role3 落在 DI 区（area0）");

    check(XIoLink_isAsync_base((XIoLink*)link), "Modbus 链路 isAsync = true（要等应答）");
    check(XModbusLink_subscriptionCount(link) == 0, "未 Start 时订阅数 = 0");

    XClassDelete((XClass*)link);
    XModbusHub_deleteLater(hub);
}

static void test_modbuslink_subscription(void)
{
    XModbusHub* hub = XModbusHub_create();
    XModbusLink* link = XModbusLink_create(hub);

    check(hub != NULL && link != NULL, "Hub 与链路创建（订阅测试）");
    if (!hub || !link) return;

    check(XIoLink_start_base((XIoLink*)link), "Start 成功");
    check(XModbusLink_subscriptionCount(link) == 2,
          "Start 后订阅数 = 2（valueChanged + writeCompleted）");
    check(XIoLink_start_base((XIoLink*)link), "重复 Start 幂等成功");
    check(XModbusLink_subscriptionCount(link) == 2, "重复 Start 后订阅数仍 = 2（不重复订阅）");

    XIoLink_stop_base((XIoLink*)link);
    check(XModbusLink_subscriptionCount(link) == 0, "Stop 后订阅数 = 0");

    XClassDelete((XClass*)link);
    XModbusHub_deleteLater(hub);
}

#endif /* XINDUSTRY_MODBUS_ON && XMODBUS_HUB_ON */

/* ==================== ⑨ XChannelAssembly ==================== */

static void test_assembly(void)
{
    XIoChannelGroup* grp = XIoChannelGroup_create();
    XTestChanLink link;
    XChannelPointBinding binds[3];
    XChannelAssemblySpec spec;
    XChannelAssemblyResult res;

    testChanLinkInit(&link, false);
    memset(binds, 0, sizeof(binds));
    binds[0].role = 0; binds[0].type = (uint16_t)XIoPoint_DigitalInput;
    binds[0].nativeAddr = 10; binds[0].tag = "A";
    binds[1].role = 1; binds[1].type = (uint16_t)XIoPoint_AnalogInput;
    binds[1].nativeAddr = 20; binds[1].scaleNum = 1; binds[1].scaleDen = 10;
    binds[2].role = 2; binds[2].type = (uint16_t)XIoPoint_AnalogOutput;
    binds[2].nativeAddr = 30; binds[2].signedValue = 1;

    /* 未挂链路：必须失败而不是静默成功。 */
    spec.m_bindings = binds;
    spec.m_bindingCount = 3;
    spec.m_identity = false;
    check(!XChannelAssembly_mount(grp, (XIoLink*)&link, &spec, &res),
          "未 setLink 时装配失败");

    /* 挂链路后装配成功。 */
    XIoChannelGroup_setLink(grp, (XIoLink*)&link);
    check(XChannelAssembly_mount(grp, (XIoLink*)&link, &spec, &res), "装配 3 个点成功");
    check(res.m_boundCount == 3 && res.m_failedRole == -1, "装配结果：3 点全成功");
    check(XIoChannelGroup_recordCount(grp) == 3, "组内点表 = 3");
    check(link.m_bindCount == 3, "链路 BindRole 调用 3 次");

    /* 点属性被正确设置。 */
    {
        XIoPoint* p = XIoChannelGroup_pointAt(grp, 0);
        check(p != NULL && XIoPoint_type(p) == XIoPoint_DigitalInput, "role0 类型 = DI");
        check(p && strcmp(XIoPoint_tag(p), "A") == 0, "role0 tag = A");
        p = XIoChannelGroup_pointAt(grp, 1);
        check(p != NULL && XIoPoint_toEngineering(p, 690) == 69, "role1 标度 den=10 生效");
        p = XIoChannelGroup_pointAt(grp, 2);
        check(p && XIoPoint_isSigned(p), "role2 有符号标记生效");
    }

    /* role 冲突：重复装配应失败并报出冲突的 role。 */
    check(!XChannelAssembly_mount(grp, (XIoLink*)&link, &spec, &res), "重复装配失败");
    check(res.m_failedRole == 0, "装配结果报出首个冲突 role = 0");
    check(XIoChannelGroup_recordCount(grp) == 3, "失败后组内点表不变（仍 3）");

    /* 单点装配。 */
    {
        XChannelPointBinding one;
        memset(&one, 0, sizeof(one));
        one.role = 5;
        one.type = (uint16_t)XIoPoint_DigitalOutput;
        one.nativeAddr = 50;
        check(XChannelAssembly_mountOne(grp, (XIoLink*)&link, &one), "单点装配 role5 成功");
        check(XIoChannelGroup_recordCount(grp) == 4, "单点装配后组内 = 4");
        check(!XChannelAssembly_mountOne(grp, (XIoLink*)&link, &one), "重复单点装配失败");
    }

    /* role 越界的绑定被拒。 */
    {
        XChannelPointBinding bad;
        memset(&bad, 0, sizeof(bad));
        bad.role = 9999;
        bad.type = (uint16_t)XIoPoint_DigitalInput;
        check(!XChannelAssembly_mountOne(grp, (XIoLink*)&link, &bad), "role 越界装配失败");
    }

    /* 参数校验。 */
    check(!XChannelAssembly_mount(NULL, (XIoLink*)&link, &spec, &res), "group=NULL 装配失败");
    check(!XChannelAssembly_mount(grp, NULL, &spec, &res), "link=NULL 装配失败");
    check(!XChannelAssembly_mount(grp, (XIoLink*)&link, NULL, &res), "spec=NULL 装配失败");

    XIoChannelGroup_setLink(grp, NULL);
    XClassDelete((XClass*)grp);
    XClassDeinit((XClass*)&link);
}

/* ==================== 主入口 ==================== */

int XChannelTest_run(void)
{
    XCoreApplication* app;
    bool owned = false;

    g_pass = 0;
    g_fail = 0;

    app = XCoreApplication_instance();
    if (!app) {
        app = XCoreApplication_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, 0, NULL);
        owned = true;
    }

    printf("=== XChannel 通道层专项单元测试 ===\n");

    printf("-- ① XIoPoint 纯数据容器 --\n");
    test_point_container();
    test_analog_point_scale();

    printf("-- ② XIoAnalogPoint_toRaw 符号语义（负数根修回归）--\n");
    test_point_toRaw_signed();
    test_point_toRaw_scale();

    printf("-- ③ XIoPoint_toEngineering（基类入口分发）--\n");
    test_point_toEngineering();

    printf("-- ③b 数字/模拟拆分守门 --\n");
    test_point_split_gate();

    printf("-- ④ XIoLink 缺省实现 --\n");
    test_link_defaults();

    printf("-- ⑤ 假链路虚槽分派 --\n");
    test_link_dispatch();

    printf("-- ⑥ XIoChannelGroup --\n");
    test_group_basics();
    test_group_feed();

#if XINDUSTRY_MODBUS_ON && XMODBUS_HUB_ON
    printf("-- ⑦ XModbusLink 地址映射（含边界）--\n");
    test_modbuslink_mapping();

    printf("-- ⑧ XModbusLink 订阅（性能断言）--\n");
    test_modbuslink_subscription();
#endif

    printf("-- ⑨ XChannelAssembly 装配路径 --\n");
    test_assembly();

    printf("\n===== XChannelTest: %d passed, %d failed =====\n", g_pass, g_fail);
    fflush(stdout);
    if (owned) XClassDelete((XClass*)app);
    return g_fail;
}

void XChannelTest(XVariant* data)
{
    const int fails = XChannelTest_run();
    (void)data;
    printf("=== XChannelTest: %s（失败 %d 项）===\n", fails == 0 ? "PASS" : "FAIL", fails);
    fflush(stdout);
}

#else /* !XINDUSTRY_ON || !XINDUSTRY_CHANNEL_ON */

int XChannelTest_run(void)
{
    printf("XChannelTest: XINDUSTRY/XCHANNEL 未启用，跳过\n");
    return 0;
}

#endif /* XINDUSTRY_ON && XINDUSTRY_CHANNEL_ON */
