/**
 * @file       xindustry_buttonstation_test.c
 * @brief      Industry 工业设备层按钮组（操作柱）测试（通道组喂点，无硬件依赖）。
 * @details    覆盖（设计对齐 xindustry_valve_test / xindustry_sensor_test 的
 *             收编形态）：
 *               - 按需绑定：只绑用到的按钮/开关，未绑元素不参与、查询恒静止；
 *               - 点动边沿：pressed/released 只在真实变化时发，**挂接播种不发**；
 *               - 急停安全侧：缺省常闭（断开=拍下）、极性可配、已绑定但无数据
 *                 按已拍下；
 *               - 转换开关：单触点两位置、双触点 Manual/Auto/矛盾/双静默；
 *               - 指示灯：手动点灯、同值去重、跟随模式自动镜像、未绑灯返回 false；
 *               - 装配校验：跨组拒绝、点未登记/类型不符拒绝、重复 detach；
 *               - 解绑与 deinit：信号隔离、自动解组；
 *               - 异步链路：灯写落定失败发 lampWriteFailed 且允许同值重写。
 *             测试点用纯数据 XIoPoint（DI/DO），数据经 XIoChannelGroup_feedRole
 *             驱动（组信号按 role 分发），写经进程内假链路（XTestLink）受理。
 *             运行依赖事件循环（信号派发），进程退出码 0=全绿。
 */
#include "XChannel/XIoChannelGroup.h"
#include "XChannel/XIoLink.h"
#include "XButtonStation.h"
#include "XObject.h"
#include "XCoreApplication.h"
#include "XThread.h"
#include "XDateTime.h"
#include "XMemory.h"
#include "XPrintf.h"
#include <string.h>
#include <stdint.h>

#if XINDUSTRY_ON && XBUTTON_STATION_ON

/* ==================== 测试辅助 ==================== */

static int g_pass = 0;
static int g_fail = 0;

#define CHECK(desc, cond) \
    do { \
        if (cond) { g_pass++; XPrintf("[PASS] %s\n", desc); } \
        else      { g_fail++; XPrintf("[FAIL] %s\n", desc); } \
    } while (0)

/* ---- 信号记录（connect_2：第 1 参即 sender） ---- */
static int g_inputElement = -1;      /**< 最近 inputChanged 元素。 */
static bool g_inputActive = false;   /**< 最近 inputChanged 动作态。 */
static int g_inputCount = 0;         /**< inputChanged 计数。 */
static int g_pressElement = -1;      /**< 最近 pressed 元素。 */
static int g_pressCount = 0;         /**< pressed 计数。 */
static int g_releaseElement = -1;    /**< 最近 released 元素。 */
static int g_releaseCount = 0;       /**< released 计数。 */
static bool g_estopEngaged = false;  /**< 最近 eStopChanged 状态。 */
static int g_estopCount = 0;         /**< eStopChanged 计数。 */
static int g_mode = -1;              /**< 最近 modeChanged 位置。 */
static int g_modeCount = 0;          /**< modeChanged 计数。 */
static int g_lampElement = -1;       /**< 最近 lampChanged 元素。 */
static bool g_lampOn = false;        /**< 最近 lampChanged 灯态。 */
static int g_lampCount = 0;          /**< lampChanged 计数。 */
static int g_lampFailElement = -1;   /**< 最近 lampWriteFailed 元素。 */
static int g_lampFailCount = 0;      /**< lampWriteFailed 计数。 */
static int g_errorSeen = 0;          /**< errorOccurred 计数。 */
static int g_lastError = 0;          /**< 最近错误码。 */

static void resetFlags(void)
{
    g_inputElement = -1;
    g_inputActive = false;
    g_inputCount = 0;
    g_pressElement = -1;
    g_pressCount = 0;
    g_releaseElement = -1;
    g_releaseCount = 0;
    g_estopEngaged = false;
    g_estopCount = 0;
    g_mode = -1;
    g_modeCount = 0;
    g_lampElement = -1;
    g_lampOn = false;
    g_lampCount = 0;
    g_lampFailElement = -1;
    g_lampFailCount = 0;
    g_errorSeen = 0;
    g_lastError = 0;
}

static void onInput2(XObject* sender, XVarList* args)
{
    (void)sender;
    if (!args) return;
    XVarList_start(args);
    g_inputElement = XVarList_arg(args, int);
    g_inputActive = XVarList_arg(args, bool);
    ++g_inputCount;
}

static void onPressed2(XObject* sender, XVarList* args)
{
    (void)sender;
    if (!args) return;
    XVarList_start(args);
    g_pressElement = XVarList_arg(args, int);
    ++g_pressCount;
}

static void onReleased2(XObject* sender, XVarList* args)
{
    (void)sender;
    if (!args) return;
    XVarList_start(args);
    g_releaseElement = XVarList_arg(args, int);
    ++g_releaseCount;
}

static void onEStop2(XObject* sender, XVarList* args)
{
    (void)sender;
    if (!args) return;
    XVarList_start(args);
    g_estopEngaged = XVarList_arg(args, bool);
    ++g_estopCount;
}

static void onMode2(XObject* sender, XVarList* args)
{
    (void)sender;
    if (!args) return;
    XVarList_start(args);
    g_mode = XVarList_arg(args, int);
    ++g_modeCount;
}

static void onLamp2(XObject* sender, XVarList* args)
{
    (void)sender;
    if (!args) return;
    XVarList_start(args);
    g_lampElement = XVarList_arg(args, int);
    g_lampOn = XVarList_arg(args, bool);
    ++g_lampCount;
}

static void onLampFail2(XObject* sender, XVarList* args)
{
    (void)sender;
    if (!args) return;
    XVarList_start(args);
    g_lampFailElement = XVarList_arg(args, int);
    ++g_lampFailCount;
}

static void onError2(XObject* sender, XVarList* args)
{
    (void)sender;
    if (!args) return;
    XVarList_start(args);
    g_lastError = XVarList_arg(args, int);
    ++g_errorSeen;
}

/** @brief 事件泵小睡（信号派发驱动，同其它 Industry 测试拍法）。 */
static void spinMs(int64_t ms)
{
    int64_t t0 = XDateTime_currentMSecsSinceEpoch();
    while (XDateTime_currentMSecsSinceEpoch() - t0 < ms) {
        XCoreApplication_processEvents(0);
        XThread_msleep(2);
    }
}

/* ==================== 测试用假链路（XIoLink 子类） ==================== */

XCLASS_DEFINE_BEGING(XBtnTestLink)
XCLASS_DEFINE_ENUM(XBtnTestLink, BindRole) = XCLASS_VTABLE_GET_SIZE(XIoLink),
XCLASS_DEFINE_ENUM(XBtnTestLink, ReadRole),
XCLASS_DEFINE_ENUM(XBtnTestLink, WriteRole),
XCLASS_DEFINE_ENUM(XBtnTestLink, IsAsync),
XCLASS_DEFINE_END(XBtnTestLink)

/**
 * @brief 假链路：记录写值/写次数，可注入同步拒绝与「受理后回传失败」。
 * @details 本类只需要写侧——按钮组的读侧走通道组缓存（feedRole 灌入），
 *          与真实链路的数据到达路径一致。
 */
typedef struct XBtnTestLink {
    XIoLink m_base;      /**< 第一个成员，基类继承。 */
    int32_t m_lastRaw;   /**< 最近一次成功写入的原生值。 */
    int m_writeCount;    /**< 成功受理的写次数。 */
    bool m_failSync;     /**< 置位时下一次写同步拒绝（返回 false）。 */
    bool m_failAsync;    /**< 置位时下一次写受理后在调用栈内回传 writeDone(false)。 */
} XBtnTestLink;

static bool VXBtnTestLink_bindRole(XBtnTestLink* self, uint16_t role, uint32_t addr,
                                   uint32_t aux)
{
    (void)addr;
    (void)aux;
    return self && role < 64;
}

static bool VXBtnTestLink_readRole(XBtnTestLink* self, uint16_t role, int32_t* outRaw)
{
    (void)self;
    (void)role;
    if (outRaw) *outRaw = 0;
    return false;   /* 读走组缓存，本假链路不提供读 */
}

static bool VXBtnTestLink_writeRole(XBtnTestLink* self, uint16_t role, int32_t raw)
{
    XIoChannelGroup* group;
    if (!self) return false;
    if (self->m_failSync) {
        self->m_failSync = false;
        return false;                       /* 同步拒绝：不发完成信号 */
    }
    group = (XIoChannelGroup*)XIoLink_group(&self->m_base);
    if (self->m_failAsync) {
        self->m_failAsync = false;
        /* 失败回传发生在写调用栈内（同步链路的失败确认形态） */
        if (group) XIoChannelGroup_completeWrite(group, role, false);
        return true;
    }
    self->m_lastRaw = raw;
    ++self->m_writeCount;
    if (group) XIoChannelGroup_completeWrite(group, role, true);
    return true;
}

static bool VXBtnTestLink_isAsync(const XBtnTestLink* self)
{
    (void)self;
    return false;   /* 同步链路：写当场落定 */
}

XVtable* XBtnTestLink_class_init(void)
{
    XVTABLE_INIT_DEFAULT(XBtnTestLink)
    XVTABLE_INHERIT_XCLASS(XIoLink);
    XVTABLE_OVERLOAD_DEFAULT(EXIoLink_BindRole, VXBtnTestLink_bindRole);
    XVTABLE_OVERLOAD_DEFAULT(EXIoLink_ReadRole, VXBtnTestLink_readRole);
    XVTABLE_OVERLOAD_DEFAULT(EXIoLink_WriteRole, VXBtnTestLink_writeRole);
    XVTABLE_OVERLOAD_DEFAULT(EXIoLink_IsAsync, VXBtnTestLink_isAsync);
    XCLASS_SHOW_SIZE_DEFAULT(XBtnTestLink);
    return XVTABLE_DEFAULT;
}

static void btnTestLinkInit(XBtnTestLink* obj)
{
    if (!obj) return;
    XIoLink_init(&obj->m_base);
    XClassGetVtable(obj) = XBtnTestLink_class_init();
    memset(&obj->m_lastRaw, 0, sizeof(XBtnTestLink) - sizeof(XIoLink));
}

/* ==================== 夹具 ==================== */

/** @brief 面板夹具：组 + 假链路 + 六触点 + 六指示灯（role 20..31）。 */
typedef struct ButtonFixture {
    XIoChannelGroup* m_group;                          /**< 通道组（堆）。 */
    XBtnTestLink* m_link;                              /**< 假链路（堆）。 */
    XIoPoint m_input[XButtonStationElement_Count];     /**< 触点（栈；role=20+元素）。 */
    XIoPoint m_lamp[XButtonStationElement_Count];      /**< 指示灯（栈；role=30+元素）。 */
    XButtonStation* m_dev;                             /**< 被测按钮组。 */
} ButtonFixture;

static void buttonFixtureInit(ButtonFixture* fx)
{
    int e;
    memset(fx, 0, sizeof(*fx));
    fx->m_group = XIoChannelGroup_create();
    fx->m_link = (XBtnTestLink*)XMemory_malloc(sizeof(XBtnTestLink),
                                               XCLASS_DEFAULT_MEMORY_TYPE);
    btnTestLinkInit(fx->m_link);
    Set_Class_IsHeap(fx->m_link, true);
    for (e = 0; e < (int)XButtonStationElement_Count; ++e) {
        XIoPoint_init(&fx->m_input[e]);
        XIoPoint_setRole(&fx->m_input[e], (uint16_t)(20 + e));
        XIoPoint_setType(&fx->m_input[e], XIoPoint_DigitalInput);
        XIoChannelGroup_addPoint(fx->m_group, &fx->m_input[e], false);

        XIoPoint_init(&fx->m_lamp[e]);
        XIoPoint_setRole(&fx->m_lamp[e], (uint16_t)(30 + e));
        XIoPoint_setType(&fx->m_lamp[e], XIoPoint_DigitalOutput);
        XIoChannelGroup_addPoint(fx->m_group, &fx->m_lamp[e], false);
    }
    XIoChannelGroup_setLink(fx->m_group, (XIoLink*)fx->m_link);
    XIoLink_start_base((XIoLink*)fx->m_link);

    fx->m_dev = XButtonStation_create();
    /* 信号观测（connect_2：第 1 参即 sender）。 */
    XObject_connect_2((XObject*)fx->m_dev,
                      XSignal(XButtonStation_inputChanged_signal), onInput2);
    XObject_connect_2((XObject*)fx->m_dev,
                      XSignal(XButtonStation_pressed_signal), onPressed2);
    XObject_connect_2((XObject*)fx->m_dev,
                      XSignal(XButtonStation_released_signal), onReleased2);
    XObject_connect_2((XObject*)fx->m_dev,
                      XSignal(XButtonStation_eStopChanged_signal), onEStop2);
    XObject_connect_2((XObject*)fx->m_dev,
                      XSignal(XButtonStation_modeChanged_signal), onMode2);
    XObject_connect_2((XObject*)fx->m_dev,
                      XSignal(XButtonStation_lampChanged_signal), onLamp2);
    XObject_connect_2((XObject*)fx->m_dev,
                      XSignal(XButtonStation_lampWriteFailed_signal), onLampFail2);
    XObject_connect_2((XObject*)fx->m_dev,
                      XSignal(XButtonStation_errorOccurred_signal), onError2);
}

static void buttonFixtureDeinit(ButtonFixture* fx)
{
    int e;
    if (fx->m_dev) XClassDelete((XClass*)fx->m_dev);
    if (fx->m_link) XClassDelete((XClass*)fx->m_link);
    if (fx->m_group) XClassDelete((XClass*)fx->m_group);
    for (e = 0; e < (int)XButtonStationElement_Count; ++e) {
        XClassDeinit((XClass*)&fx->m_input[e]);
        XClassDeinit((XClass*)&fx->m_lamp[e]);
    }
}

/** @brief 触点喂值（组按 role 分发 → 按钮组按极性解释）。 */
static void feedContact(ButtonFixture* fx, int element, bool closed)
{
    XIoChannelGroup_feedRole(fx->m_group, (uint16_t)(20 + element),
                             closed ? 1 : 0);
}

/** @brief 绑一个元素的触点（role=20+元素）。 */
static bool attachInput(ButtonFixture* fx, int element)
{
    return XButtonStation_attachInput(fx->m_dev, fx->m_group, element,
                                      &fx->m_input[element]);
}

/** @brief 绑一个元素的指示灯（role=30+元素）。 */
static bool attachLamp(ButtonFixture* fx, int element)
{
    return XButtonStation_attachLamp(fx->m_dev, fx->m_group, element,
                                     &fx->m_lamp[element]);
}

/* ==================== 用例：按需绑定与点动边沿 ==================== */

static void test_partialBindingAndEdges(void)
{
    ButtonFixture fx;

    resetFlags();
    buttonFixtureInit(&fx);
    /* 只绑启动/停止两个按钮：其余元素不参与。 */
    CHECK("绑启动触点", attachInput(&fx, XButtonStationElement_Start));
    CHECK("绑停止触点", attachInput(&fx, XButtonStationElement_Stop));
    CHECK("未绑元素查询恒静止",
          !XButtonStation_isActive(fx.m_dev, XButtonStationElement_Reset) &&
          !XButtonStation_isActive(fx.m_dev, XButtonStationElement_EmergencyStop));
    CHECK("未绑急停不算拍下", !XButtonStation_isEStopEngaged(fx.m_dev));
    CHECK("播种不产生边沿", g_pressCount == 0 && g_releaseCount == 0);
    /* 状态信号（inputChanged）播种发初值：两个已绑按钮各发一次 false。 */
    CHECK("播种发状态初值（false）", g_inputCount == 2 && !g_inputActive);

    /* 按下启动 → pressed + inputChanged(true)。 */
    feedContact(&fx, XButtonStationElement_Start, true);
    spinMs(5);
    CHECK("启动按下发 pressed", g_pressCount == 1 &&
          g_pressElement == XButtonStationElement_Start);
    CHECK("启动按下发 inputChanged", g_inputCount >= 1 && g_inputActive &&
          g_inputElement == XButtonStationElement_Start);
    CHECK("启动按下查询为真", XButtonStation_isActive(fx.m_dev,
                                                      XButtonStationElement_Start));

    /* 同值重喂不重复发。 */
    {
        int pc = g_pressCount;
        int ic = g_inputCount;
        feedContact(&fx, XButtonStationElement_Start, true);
        spinMs(5);
        CHECK("同值重喂不重发", g_pressCount == pc && g_inputCount == ic);
    }
    /* 松开 → released。 */
    feedContact(&fx, XButtonStationElement_Start, false);
    spinMs(5);
    CHECK("启动松开发 released", g_releaseCount == 1 &&
          g_releaseElement == XButtonStationElement_Start);
    CHECK("启动松开查询为假", !XButtonStation_isActive(fx.m_dev,
                                                       XButtonStationElement_Start));

    /* 停止按钮独立工作。 */
    feedContact(&fx, XButtonStationElement_Stop, true);
    spinMs(5);
    CHECK("停止按下独立计数", g_pressCount == 2 &&
          g_pressElement == XButtonStationElement_Stop);
    feedContact(&fx, XButtonStationElement_Stop, false);
    spinMs(5);
    CHECK("停止松开", g_releaseCount == 2);

    /* 未绑元素的触点被喂值也不影响（点虽在组内但未绑到按钮组）。 */
    feedContact(&fx, XButtonStationElement_Reset, true);
    spinMs(5);
    CHECK("未绑元素喂值不参与", g_pressCount == 2 &&
          !XButtonStation_isActive(fx.m_dev, XButtonStationElement_Reset));

    buttonFixtureDeinit(&fx);
}

/* ==================== 用例：急停安全侧 ==================== */

static void test_estopSafety(void)
{
    ButtonFixture fx;

    resetFlags();
    buttonFixtureInit(&fx);
    /* 急停缺省常闭：绑定时无数据 → 安全侧按已拍下（播种即发布）。 */
    CHECK("绑急停触点", attachInput(&fx, XButtonStationElement_EmergencyStop));
    CHECK("无数据按已拍下", XButtonStation_isEStopEngaged(fx.m_dev));
    CHECK("安全侧缺省已发布 eStopChanged", g_estopCount == 1 && g_estopEngaged);

    /* 常闭触点闭合 = 正常（未拍下）→ 状态翻转。 */
    feedContact(&fx, XButtonStationElement_EmergencyStop, true);
    spinMs(5);
    CHECK("常闭闭合=未拍下", !XButtonStation_isEStopEngaged(fx.m_dev));
    CHECK("复位沿已发布", g_estopCount == 2 && !g_estopEngaged);

    /* 常闭断开 = 拍下。 */
    feedContact(&fx, XButtonStationElement_EmergencyStop, false);
    spinMs(5);
    CHECK("常闭断开=已拍下", XButtonStation_isEStopEngaged(fx.m_dev));
    CHECK("拍下沿已发布", g_estopCount == 3 && g_estopEngaged);
    CHECK("急停不发 pressed 边沿", g_pressCount == 0);

    /* 极性改常开：闭合即拍下。 */
    CHECK("极性改常开", XButtonStation_setContactActiveHigh(
                            fx.m_dev, XButtonStationElement_EmergencyStop, true));
    CHECK("常开下查询极性", XButtonStation_contactActiveHigh(
                                fx.m_dev, XButtonStationElement_EmergencyStop));
    /* 当前触点仍断开 → 常开解释为未拍下。 */
    CHECK("极性切换立即生效", !XButtonStation_isEStopEngaged(fx.m_dev));
    feedContact(&fx, XButtonStationElement_EmergencyStop, true);
    spinMs(5);
    CHECK("常开闭合=已拍下", XButtonStation_isEStopEngaged(fx.m_dev));

    /* 未绑急停的面板不误报。 */
    {
        XButtonStation* bare = XButtonStation_create();
        CHECK("未绑急停不误报", !XButtonStation_isEStopEngaged(bare));
        XClassDelete((XClass*)bare);
    }
    buttonFixtureDeinit(&fx);
}

/* ==================== 用例：转换开关 ==================== */

static void test_modeSelector(void)
{
    ButtonFixture fx;

    resetFlags();
    buttonFixtureInit(&fx);
    /* 单触点（只绑手动位）：动作=Manual，静止=Auto。 */
    CHECK("绑手动位触点", attachInput(&fx, XButtonStationElement_ModeManual));
    CHECK("无数据位置不明", XButtonStation_mode(fx.m_dev) ==
                            XButtonStationMode_Unknown);
    feedContact(&fx, XButtonStationElement_ModeManual, true);
    spinMs(5);
    CHECK("手动位动作=Manual", XButtonStation_mode(fx.m_dev) ==
                               XButtonStationMode_Manual);
    CHECK("modeChanged 已发", g_modeCount >= 1 && g_mode == XButtonStationMode_Manual);
    feedContact(&fx, XButtonStationElement_ModeManual, false);
    spinMs(5);
    CHECK("手动位静止=Auto", XButtonStation_mode(fx.m_dev) ==
                             XButtonStationMode_Auto);
    CHECK("切自动已发布", g_mode == XButtonStationMode_Auto);
    CHECK("转换开关不发 pressed", g_pressCount == 0);

    /* 补绑自动位触点：双触点口径。 */
    CHECK("绑自动位触点", attachInput(&fx, XButtonStationElement_ModeAuto));
    feedContact(&fx, XButtonStationElement_ModeAuto, false);
    feedContact(&fx, XButtonStationElement_ModeManual, false);
    spinMs(5);
    CHECK("双静默位置不明", XButtonStation_mode(fx.m_dev) ==
                            XButtonStationMode_Unknown);
    feedContact(&fx, XButtonStationElement_ModeAuto, true);
    spinMs(5);
    CHECK("自动位动作=Auto", XButtonStation_mode(fx.m_dev) ==
                             XButtonStationMode_Auto);

    /* 双触点同时动作态 = 矛盾。 */
    {
        int ec = g_errorSeen;
        feedContact(&fx, XButtonStationElement_ModeManual, true);
        spinMs(5);
        CHECK("双触点矛盾落 Unknown", XButtonStation_mode(fx.m_dev) ==
                                      XButtonStationMode_Unknown);
        CHECK("矛盾上报错误", g_errorSeen > ec &&
              g_lastError == XButtonStationError_ModeContradiction);
        /* 矛盾持续不重复报错（沿触发）。 */
        {
            int ec2 = g_errorSeen;
            feedContact(&fx, XButtonStationElement_ModeAuto, false);
            spinMs(5);
            CHECK("矛盾解除不误报", g_errorSeen == ec2);
        }
    }
    /* 上电即停在自动位：首拍读到的就是「手动位触点断开」，动作态 false
     * 与初值相同——但位置必须能从 Unknown 推到 Auto（可用性变化不改值）。 */
    {
        XButtonStation* dev2 = XButtonStation_create();
        int mc;
        resetFlags();
        CHECK("场景2：绑手动位触点", XButtonStation_attachInput(
                                         dev2, fx.m_group,
                                         XButtonStationElement_ModeManual,
                                         &fx.m_input[XButtonStationElement_ModeManual]));
        mc = g_modeCount;
        feedContact(&fx, XButtonStationElement_ModeManual, false);
        spinMs(5);
        CHECK("首拍断开数据也推定 Auto", XButtonStation_mode(dev2) ==
                                        XButtonStationMode_Auto);
        CHECK("首拍推定位发 modeChanged", g_modeCount == mc + 1);
        XClassDelete((XClass*)dev2);
    }

    buttonFixtureDeinit(&fx);
}

/* ==================== 用例：指示灯 ==================== */

static void test_lamps(void)
{
    ButtonFixture fx;
    bool on = false;

    resetFlags();
    buttonFixtureInit(&fx);
    /* 未绑灯：点灯返回 false，不算错误。 */
    CHECK("未绑灯点灯返回 false", !XButtonStation_setLamp(fx.m_dev,
                                                          XButtonStationElement_Start,
                                                          true));
    CHECK("未绑灯不误报错误", g_errorSeen == 0);
    CHECK("未绑灯查询返回 false", !XButtonStation_lampState(
                                      fx.m_dev, XButtonStationElement_Start, &on));

    /* 手动点灯。 */
    CHECK("绑启动灯", attachLamp(&fx, XButtonStationElement_Start));
    CHECK("点灯受理", XButtonStation_setLamp(fx.m_dev,
                                             XButtonStationElement_Start, true));
    CHECK("灯已写链路", fx.m_link->m_lastRaw == 1 && fx.m_link->m_writeCount == 1);
    CHECK("lampChanged 已发", g_lampCount == 1 && g_lampOn &&
          g_lampElement == XButtonStationElement_Start);
    CHECK("灯态查询为亮", XButtonStation_lampState(fx.m_dev,
                                                   XButtonStationElement_Start, &on) && on);
    /* 同值去重：不再写链路。 */
    CHECK("同值点灯仍受理", XButtonStation_setLamp(fx.m_dev,
                                                   XButtonStationElement_Start, true));
    CHECK("同值不重写链路", fx.m_link->m_writeCount == 1 && g_lampCount == 1);
    /* 灭灯。 */
    CHECK("灭灯受理", XButtonStation_setLamp(fx.m_dev,
                                             XButtonStationElement_Start, false));
    CHECK("灭灯已写链路", fx.m_link->m_lastRaw == 0 && fx.m_link->m_writeCount == 2);

    /* 跟随模式：按钮按下即亮，松开发灭。 */
    CHECK("绑停止灯", attachLamp(&fx, XButtonStationElement_Stop));
    CHECK("打开跟随", XButtonStation_setLampFollow(fx.m_dev,
                                                   XButtonStationElement_Stop, true));
    CHECK("跟随查询", XButtonStation_lampFollow(fx.m_dev,
                                                XButtonStationElement_Stop));
    CHECK("打开跟随即刻驱动一次（静止=灭）", fx.m_link->m_lastRaw == 0 &&
          XButtonStation_lampState(fx.m_dev, XButtonStationElement_Stop, &on) && !on);
    attachInput(&fx, XButtonStationElement_Stop);
    feedContact(&fx, XButtonStationElement_Stop, true);
    spinMs(5);
    CHECK("跟随按下即亮", XButtonStation_lampState(fx.m_dev,
                                                   XButtonStationElement_Stop, &on) && on);
    CHECK("跟随点亮已写链路", fx.m_link->m_lastRaw == 1);
    feedContact(&fx, XButtonStationElement_Stop, false);
    spinMs(5);
    CHECK("跟随松开即灭", XButtonStation_lampState(fx.m_dev,
                                                   XButtonStationElement_Stop, &on) && !on);

    /* 同步写拒绝：返回 false + 错误上报，灯态不变。 */
    {
        int ec = g_errorSeen;
        fx.m_link->m_failSync = true;
        CHECK("同步拒绝返回 false", !XButtonStation_setLamp(
                                        fx.m_dev, XButtonStationElement_Start, true));
        CHECK("同步拒绝上报错误", g_errorSeen > ec &&
              g_lastError == XButtonStationError_LampWriteRejected);
        CHECK("同步拒绝灯态未变", XButtonStation_lampState(
                                      fx.m_dev, XButtonStationElement_Start, &on) && !on);
    }
    /* 写栈内失败回传：不发 lampChanged（只发 lampWriteFailed），且允许同值重写。 */
    {
        int lc = g_lampCount;
        fx.m_link->m_failAsync = true;
        CHECK("失败回传不算受理", !XButtonStation_setLamp(
                                      fx.m_dev, XButtonStationElement_Start, true));
        CHECK("失败回传发 lampWriteFailed", g_lampFailCount == 1 &&
              g_lampFailElement == XButtonStationElement_Start);
        CHECK("失败回传不发 lampChanged", g_lampCount == lc);
        CHECK("失败后可同值重写", XButtonStation_setLamp(
                                      fx.m_dev, XButtonStationElement_Start, true) &&
              g_lampCount == lc + 1);
    }
    buttonFixtureDeinit(&fx);
}

/* ==================== 用例：装配校验与解绑 ==================== */

static void test_attachValidationAndDetach(void)
{
    ButtonFixture fx;
    XIoChannelGroup* other;

    resetFlags();
    buttonFixtureInit(&fx);
    /* 元素号越界 / 空点。 */
    CHECK("元素号越界拒绝", !XButtonStation_attachInput(fx.m_dev, fx.m_group,
                                                        XButtonStationElement_Count,
                                                        &fx.m_input[0]));
    CHECK("空点拒绝", !XButtonStation_attachInput(fx.m_dev, fx.m_group,
                                                  XButtonStationElement_Start, NULL));
    CHECK("空组拒绝", !XButtonStation_attachInput(fx.m_dev, NULL,
                                                  XButtonStationElement_Start,
                                                  &fx.m_input[0]));
    /* 类型不符：把指示灯点当触点绑。 */
    {
        int ec = g_errorSeen;
        CHECK("类型不符拒绝", !XButtonStation_attachInput(
                                  fx.m_dev, fx.m_group, XButtonStationElement_Start,
                                  &fx.m_lamp[XButtonStationElement_Start]));
        CHECK("类型不符上报 BadPoint", g_errorSeen > ec &&
              g_lastError == XButtonStationError_BadPoint);
    }
    /* 未登记于组的点。 */
    {
        XIoPoint stray;
        int ec = g_errorSeen;
        XIoPoint_init(&stray);
        XIoPoint_setRole(&stray, 60);
        XIoPoint_setType(&stray, XIoPoint_DigitalInput);
        CHECK("未登记点拒绝", !XButtonStation_attachInput(
                                  fx.m_dev, fx.m_group, XButtonStationElement_Start,
                                  &stray));
        CHECK("未登记点上报 BadPoint", g_errorSeen > ec &&
              g_lastError == XButtonStationError_BadPoint);
        XClassDeinit((XClass*)&stray);
    }
    /* 跨组混挂拒绝。 */
    other = XIoChannelGroup_create();
    {
        int ec = g_errorSeen;
        CHECK("绑第一个触点", attachInput(&fx, XButtonStationElement_Start));
        CHECK("跨组拒绝", !XButtonStation_attachLamp(
                              fx.m_dev, other, XButtonStationElement_Start,
                              &fx.m_lamp[XButtonStationElement_Start]));
        CHECK("跨组上报 DifferentGroup", g_errorSeen > ec &&
              g_lastError == XButtonStationError_DifferentGroup);
        CHECK("跨组后原组仍生效", XButtonStation_group(fx.m_dev) == fx.m_group);
    }
    XClassDelete(other);

    /* 解绑触点：动作态回落并发布 released。 */
    feedContact(&fx, XButtonStationElement_Start, true);
    spinMs(5);
    {
        int rc = g_releaseCount;
        CHECK("解绑触点", XButtonStation_detachInput(fx.m_dev,
                                                     XButtonStationElement_Start));
        spinMs(5);
        CHECK("解绑发 released", g_releaseCount == rc + 1);
        CHECK("解绑后查询恒静止", !XButtonStation_isActive(
                                      fx.m_dev, XButtonStationElement_Start));
        CHECK("重复解绑返回 false", !XButtonStation_detachInput(
                                        fx.m_dev, XButtonStationElement_Start));
    }
    /* 绑灯 → 解绑 → 自动解组（无任何绑定时组指针归零）。 */
    CHECK("绑停止灯", attachLamp(&fx, XButtonStationElement_Stop));
    CHECK("解绑灯", XButtonStation_detachLamp(fx.m_dev,
                                              XButtonStationElement_Stop));
    CHECK("全部解绑后自动解组", XButtonStation_group(fx.m_dev) == NULL);
    CHECK("解组后重复 detachAll 返回 false", !XButtonStation_detachAll(fx.m_dev));

    /* 解组后重新挂接可用（组指针已释放，不残留旧组约束）。 */
    CHECK("重新挂接", attachInput(&fx, XButtonStationElement_Reset));
    CHECK("重新挂接组归属正确", XButtonStation_group(fx.m_dev) == fx.m_group);
    CHECK("detachAll 解绑全部", XButtonStation_detachAll(fx.m_dev));
    CHECK("detachAll 后解组", XButtonStation_group(fx.m_dev) == NULL);

    /* NULL 查询安全。 */
    CHECK("NULL 查询安全", !XButtonStation_isActive(NULL, 0) &&
          !XButtonStation_isEStopEngaged(NULL) &&
          XButtonStation_mode(NULL) == XButtonStationMode_Unknown &&
          XButtonStation_group(NULL) == NULL);
    buttonFixtureDeinit(&fx);
}

/* ==================== 用例：换点 ==================== */

/**
 * @brief 换点语义：旧点按下态由新点播种按**终值**发布。
 * @details 旧 true → 新 false 恰好就是那条 released（不多不少），不得先补发
 *          一次静止再播种——那会凭空多出一条 released→pressed，上层会当成
 *          用户连按两次。
 */
static void test_rebindPoint(void)
{
    ButtonFixture fx;
    XIoPoint spare;
    int rc;
    int pc;

    resetFlags();
    buttonFixtureInit(&fx);
    XIoPoint_init(&spare);
    XIoPoint_setRole(&spare, 50);
    XIoPoint_setType(&spare, XIoPoint_DigitalInput);
    XIoChannelGroup_addPoint(fx.m_group, &spare, false);

    attachInput(&fx, XButtonStationElement_Start);
    feedContact(&fx, XButtonStationElement_Start, true);
    spinMs(5);
    CHECK("换点前已按下", XButtonStation_isActive(fx.m_dev,
                                                  XButtonStationElement_Start));
    rc = g_releaseCount;
    pc = g_pressCount;
    CHECK("换点成功", XButtonStation_attachInput(
                          fx.m_dev, fx.m_group, XButtonStationElement_Start,
                          &spare));
    spinMs(5);
    CHECK("换点后按新点落静止", !XButtonStation_isActive(
                                    fx.m_dev, XButtonStationElement_Start));
    CHECK("换点发一条 released", g_releaseCount == rc + 1);
    CHECK("换点不凭空多出 pressed", g_pressCount == pc);

    XClassDeinit((XClass*)&spare);
    buttonFixtureDeinit(&fx);
}

/* ==================== 用例：解绑后信号隔离 ==================== */

static void test_signalIsolation(void)
{
    ButtonFixture fx;

    resetFlags();
    buttonFixtureInit(&fx);
    attachInput(&fx, XButtonStationElement_Start);
    attachLamp(&fx, XButtonStationElement_Start);
    CHECK("解绑全部", XButtonStation_detachAll(fx.m_dev));
    {
        int ic = g_inputCount;
        int pc = g_pressCount;
        feedContact(&fx, XButtonStationElement_Start, true);
        spinMs(10);
        CHECK("解绑后触点信号隔离", g_inputCount == ic && g_pressCount == pc);
        CHECK("解绑后点灯返回 false", !XButtonStation_setLamp(
                                          fx.m_dev, XButtonStationElement_Start, true));
    }
    buttonFixtureDeinit(&fx);
}

/**
 * @brief 配置入口边界与取值器：越界元素号、NULL 对象、group() 借用语义。
 * @details 元素号是**外部可传**的下标（页/装配层按枚举遍历），越界必须
 *          逐个入口拒绝——漏一处就是数组越界写。group() 是借用指针，
 *          未挂接时必须回到 NULL（调用方据此判"是否已装配"）。
 */
static void test_configBoundsAndGetters(void)
{
    ButtonFixture fx;

    resetFlags();
    buttonFixtureInit(&fx);

    /* ---- group()：未挂接为 NULL，挂接后非 NULL ---- */
    CHECK("未挂接 group 为 NULL", XButtonStation_group(fx.m_dev) == NULL);
    CHECK("挂接触点", XButtonStation_attachInput(
                          fx.m_dev, fx.m_group, XButtonStationElement_Start,
                          &fx.m_input[XButtonStationElement_Start]));
    CHECK("挂接后 group 非 NULL", XButtonStation_group(fx.m_dev) == fx.m_group);

    /* ---- 越界元素号：全部配置/查询入口都要拒绝 ---- */
    CHECK("越界 setContactActiveHigh 拒绝",
          !XButtonStation_setContactActiveHigh(fx.m_dev,
                                               XButtonStationElement_Count, true));
    CHECK("负元素号 setContactActiveHigh 拒绝",
          !XButtonStation_setContactActiveHigh(fx.m_dev, -1, true));
    CHECK("越界 contactActiveHigh 返回 false",
          !XButtonStation_contactActiveHigh(fx.m_dev,
                                            XButtonStationElement_Count));
    CHECK("越界 setLampFollow 拒绝",
          !XButtonStation_setLampFollow(fx.m_dev,
                                        XButtonStationElement_Count, true));
    CHECK("负元素号 setLampFollow 拒绝",
          !XButtonStation_setLampFollow(fx.m_dev, -1, true));
    CHECK("越界 lampFollow 返回 false",
          !XButtonStation_lampFollow(fx.m_dev, XButtonStationElement_Count));

    /* ---- 合法范围内读写一致 ---- */
    CHECK("设常开触点", XButtonStation_setContactActiveHigh(
                            fx.m_dev, XButtonStationElement_Start, true));
    CHECK("常开触点回读 true",
          XButtonStation_contactActiveHigh(fx.m_dev, XButtonStationElement_Start));
    CHECK("设常闭触点", XButtonStation_setContactActiveHigh(
                            fx.m_dev, XButtonStationElement_Start, false));
    CHECK("常闭触点回读 false",
          !XButtonStation_contactActiveHigh(fx.m_dev, XButtonStationElement_Start));
    CHECK("设灯跟随", XButtonStation_setLampFollow(
                          fx.m_dev, XButtonStationElement_Start, true));
    CHECK("灯跟随回读 true",
          XButtonStation_lampFollow(fx.m_dev, XButtonStationElement_Start));
    CHECK("关灯跟随", XButtonStation_setLampFollow(
                          fx.m_dev, XButtonStationElement_Start, false));
    CHECK("灯跟随回读 false",
          !XButtonStation_lampFollow(fx.m_dev, XButtonStationElement_Start));

    /* ---- NULL 对象：全部安全且不回真 ---- */
    CHECK("NULL group 为 NULL", XButtonStation_group(NULL) == NULL);
    CHECK("NULL isActive 安全", !XButtonStation_isActive(
                                    NULL, XButtonStationElement_Start));
    CHECK("NULL isEStopEngaged 安全", !XButtonStation_isEStopEngaged(NULL));
    CHECK("NULL lampState 安全", (XButtonStation_lampState(
                                      NULL, XButtonStationElement_Start, NULL),
                                  true));
    CHECK("NULL mode 安全", XButtonStation_mode(NULL) == XButtonStationMode_Unknown);
    CHECK("NULL setContactActiveHigh 安全",
          !XButtonStation_setContactActiveHigh(NULL,
                                               XButtonStationElement_Start, true));
    CHECK("NULL contactActiveHigh 安全",
          !XButtonStation_contactActiveHigh(NULL, XButtonStationElement_Start));
    CHECK("NULL setLampFollow 安全",
          !XButtonStation_setLampFollow(NULL, XButtonStationElement_Start, true));
    CHECK("NULL lampFollow 安全",
          !XButtonStation_lampFollow(NULL, XButtonStationElement_Start));
    CHECK("NULL setLamp 安全", !XButtonStation_setLamp(
                                   NULL, XButtonStationElement_Start, true));
    CHECK("NULL detachAll 安全", (XButtonStation_detachAll(NULL), true));
    CHECK("NULL detachInput 安全", !XButtonStation_detachInput(
                                       NULL, XButtonStationElement_Start));
    CHECK("NULL detachLamp 安全", !XButtonStation_detachLamp(
                                      NULL, XButtonStationElement_Start));

    buttonFixtureDeinit(&fx);
}

/** @brief 跑一轮全部用例（泄漏压测循环体）。 */
static void buttonRunAll(void)
{
    test_partialBindingAndEdges();
    test_estopSafety();
    test_modeSelector();
    test_lamps();
    test_attachValidationAndDetach();
    test_rebindPoint();
    test_signalIsolation();
    test_configBoundsAndGetters();
}

/**
 * @brief 泄漏压测：全部用例循环 iterations 轮，堆在用字节数必须回基线。
 */
static void buttonLeakLoop(int iterations)
{
    XMemoryStatistics base;
    XMemoryStatistics now;
    int i;
    if (!XMemory_statisticsEnabled()) {
        ++g_pass;
        XPrintf("[SKIP] 内存统计被构建裁剪（XMEMORY_STATISTICS_ON=0）\n");
        return;
    }
    base = XMemory_statistics(XMEMORY_TYPE_SYSTEM);
    for (i = 0; i < iterations; ++i)
        buttonRunAll();
    now = XMemory_statistics(XMEMORY_TYPE_SYSTEM);
    CHECK("泄漏压测：堆在用字节数回基线", now.systemBytes == base.systemBytes);
    if (now.systemBytes != base.systemBytes)
        XPrintf("       基线=%llu 现在=%llu（差 %lld）\n",
                (unsigned long long)base.systemBytes,
                (unsigned long long)now.systemBytes,
                (long long)now.systemBytes - (long long)base.systemBytes);
}

/* ==================== 收编入口（_run 供 --test/Test 命令分派） ==================== */

int XIndustryButtonStationTest_run(void)
{
    XCoreApplication* app;
    bool owned;

    /* [单例所有权] 同 XIndustryValveTest_run：只删除自己创建的实例。 */
    owned = (XCoreApplication_instance() == NULL);
    app = XCoreApplication_create(0, NULL);
    if (!app) {
        XPrintf("[FAIL] 事件循环创建失败\n");
        return 1;
    }

    buttonRunAll();
    XPrintf("\n===== XIndustryButtonStationTest: %d passed, %d failed =====\n",
            g_pass, g_fail);

    /* 泄漏压测：全部用例 200 轮，统计须回零。 */
    XPrintf("===== 泄漏压测 200 轮… =====\n");
    buttonLeakLoop(200);
    XPrintf("===== XIndustryButtonStationTest(含压测): %d passed, %d failed =====\n",
            g_pass, g_fail);

    if (owned) XClassDelete(app);
    return g_fail == 0 ? 0 : 1;
}

void XIndustryButtonStationTest(XVariant* data)
{
    const int fails = XIndustryButtonStationTest_run();
    (void)data;
    XPrintf("=== XIndustryButtonStationTest: %s（失败 %d 项）===\n",
            fails == 0 ? "PASS" : "FAIL", fails);
    fflush(stdout);
}

#else /* !XINDUSTRY_ON || !XBUTTON_STATION_ON */

#include "XPrintf.h"
int XIndustryButtonStationTest_run(void)
{
    XPrintf("XIndustryButtonStationTest: XINDUSTRY/XBUTTON_STATION 未启用，跳过\n");
    return 0;
}

#endif /* XINDUSTRY_ON && XBUTTON_STATION_ON */
