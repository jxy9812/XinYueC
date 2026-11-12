/**
 * @file       XButtonStation.c
 * @brief      按钮组（操作柱）设备抽象实现。
 * @details    与 XSolenoidValve.c / XTempHumTransmitter.c / XFlowMeter.c 同族：
 *             组信号槽经 connect_1 订阅（receiver=设备，槽内按 role 过滤本设备
 *             关注点）、解绑与 deinit 共用内部路径。无定时器——按钮组是被动
 *             设备（只对触点变化与外部点灯请求作出反应），不持有任何周期/超时
 *             资源，故无「触发≠注销」一类泄漏面。
 *
 *             数据流（读侧）：组 inputChanged(role, raw) → 按 role 定位元素
 *             （一次线性扫 6 元素，见下）→ 极性解释 → 状态发布。
 *
 *             [为什么这里允许 6 次线性扫]
 *             其它设备用「点 role 反查元素」的循环，是因为它们的 role 由装配
 *             层任意指定（流量计 22 个 role、变频器 68 个），无法反算下标。
 *             本类只有 6 个元素，且**同一元素的触点与指示灯共用下标**——
 *             仍按同一写法扫 6 项，编译期常量上界 + 命中即断，代价与查表
 *             同量级，不值得引入第二张 role→元素表（那张表还要在 attach/
 *             detach 两处维护一致性，是更贵的错）。
 *
 *             [灯态去重与失败沿]
 *             同值重发会让总线每拍都多一帧，故灯态按「最近一次受理值」去重；
 *             但异步链路落定失败时该灯必须能被同值重写（否则一次总线抖动
 *             就把这盏灯永久锁在错误状态），故失败沿清掉去重位。
 */
#include "XButtonStation.h"

#if XINDUSTRY_ON && XBUTTON_STATION_ON

#include <string.h>
#include "XMemory.h"

/* ==================== 内部辅助 ==================== */

/** @brief 点动按钮元素上界（Start..Reset；其后的元素都是转换开关位置）。 */
#define XBUTTONSTATION_PUSHBUTTON_MIN ((int)XButtonStationElement_Start)
#define XBUTTONSTATION_PUSHBUTTON_MAX ((int)XButtonStationElement_Reset)

/** @brief 元素号合法化。 */
static bool buttonElementOk(int element)
{
    return element >= 0 && element < (int)XButtonStationElement_Count;
}

/**
 * @brief 是否发 pressed/released 边沿的点动按钮。
 * @note 急停不发边沿：它走 eStopChanged 状态信号（漏掉一个边沿也不会让
 *       安全信息丢失，且安全侧缺省「已拍下」只能经状态信号表达）。
 */
static bool buttonIsPushButton(int element)
{
    return element >= XBUTTONSTATION_PUSHBUTTON_MIN &&
           element <= XBUTTONSTATION_PUSHBUTTON_MAX;
}

/** @brief 是否转换开关位置。 */
static bool buttonIsModeSelector(int element)
{
    return element == (int)XButtonStationElement_ModeManual ||
           element == (int)XButtonStationElement_ModeAuto;
}

/** @brief 订阅槽前向声明：解绑/deinit 的断开坐标需要引用。 */
static void VXButtonStation_onInputChanged(XObject* receiver, XVarList* args);
static void VXButtonStation_onWriteDone(XObject* receiver, XVarList* args);

/**
 * @brief 电气触点原始状态 → 元素动作态（按极性解释）。
 * @param self 按钮组（提供极性）。
 * @param element 元素号。
 * @param closed 触点是否闭合（电气侧）。
 * @return 动作态。
 */
static bool buttonInterpret(const XButtonStation* self, int element, bool closed)
{
    return self->m_activeHigh[element] ? closed : !closed;
}

/* ==================== 信号发布（去重 + 播种区分） ==================== */

/** @brief 灯态下发（内部；不发布信号，失败照常返回 false）。 */
static bool buttonWriteLamp(XButtonStation* self, int element, bool on)
{
    if (!self->m_group || !self->m_lampPt[element]) return false;
    return XIoChannelGroup_writeDigitalOutput(self->m_group,
                                              XIoPoint_role(self->m_lampPt[element]),
                                              on);
}

/**
 * @brief 灯态受理：更新缓存、去重位、发射 lampChanged。
 * @return 无。
 */
static void buttonPublishLamp(XButtonStation* self, int element, bool on)
{
    self->m_lampOn[element] = on;
    self->m_lampPublished[element] = true;
    XButtonStation_lampChanged_signal(self, element, on);
}

/**
 * @brief 点/灭灯（内部统一入口，手动与跟随共用）。
 * @param self 按钮组。
 * @param element 元素号。
 * @param on 目标灯态。
 * @param force 忽略去重位（跟随模式重算时用，保证与实际一致）。
 * @return 受理（或已处于该状态）返回 true。
 * @note 写拒绝 → errorOccurred；异步落定失败由 writeDone 槽后续发布。
 *       **同步链路的失败也可能经 writeDone(false) 在写调用栈内回传**
 *       （假从站/进程内链路就是这样），此时 write 本身可能仍返回 true。
 *       为免「受理信号」与「失败信号」次序颠倒（先 lampChanged 再
 *       lampWriteFailed 会让上层看到一次假亮），写栈内收到的失败由
 *       m_lampFailed 记下，写返回后抑制 lampChanged，只留失败信号。
 */
static bool buttonApplyLamp(XButtonStation* self, int element, bool on, bool force)
{
    bool accepted;

    if (!buttonElementOk(element)) return false;
    if (!self->m_group || !self->m_lampPt[element]) {
        XButtonStation_errorOccurred_signal(self, XButtonStationError_NotAttached);
        return false;
    }
    if (!force && self->m_lampPublished[element] && self->m_lampOn[element] == on)
        return true;                                  /* 同值：不重发，免总线抖动。 */

    self->m_lampWriting[element] = true;
    self->m_lampFailed[element] = false;
    accepted = buttonWriteLamp(self, element, on);
    self->m_lampWriting[element] = false;

    if (self->m_lampFailed[element]) {
        /* 写栈内已收到失败回传：失败沿已发，不再发受理（见函数头）。 */
        self->m_lampFailed[element] = false;
        self->m_lampPublished[element] = false;
        return false;
    }
    if (!accepted) {
        /* 同步拒绝：不写缓存、不去重（下次仍可重试同一目标值）。 */
        self->m_lampPublished[element] = false;
        XButtonStation_errorOccurred_signal(self, XButtonStationError_LampWriteRejected);
        return false;
    }
    buttonPublishLamp(self, element, on);
    return true;
}

/** @brief 跟随模式下的灯态重算（未跟随/未绑灯时为空操作）。 */
static void buttonSyncFollowLamp(XButtonStation* self, int element)
{
    if (!buttonElementOk(element)) return;
    if (!self->m_lampFollow[element] || !self->m_lampPt[element]) return;
    buttonApplyLamp(self, element, self->m_active[element], false);
}

/**
 * @brief 转换开关位置推导（单/双触点通用，见头文件）。
 * @param self 按钮组。
 * @return 推导结果。
 */
static XButtonStationMode buttonDeriveMode(const XButtonStation* self)
{
    const int manual = (int)XButtonStationElement_ModeManual;
    const int aut = (int)XButtonStationElement_ModeAuto;
    bool hasManual = (self->m_inputPt[manual] != NULL);
    bool hasAuto = (self->m_inputPt[aut] != NULL);

    if (!hasManual && !hasAuto) return XButtonStationMode_Unknown;
    /* 已绑定但无数据：不推断（不知道开关停在哪，也不知道线是否断着）。 */
    if ((hasManual && !self->m_hasData[manual]) ||
        (hasAuto && !self->m_hasData[aut]))
        return XButtonStationMode_Unknown;
    if (hasManual && hasAuto) {
        if (self->m_active[manual] && self->m_active[aut])
            return XButtonStationMode_Unknown;   /* 矛盾：由发布侧报错。 */
        if (self->m_active[manual]) return XButtonStationMode_Manual;
        if (self->m_active[aut]) return XButtonStationMode_Auto;
        return XButtonStationMode_Unknown;       /* 双静默：位置不明（不断言）。 */
    }
    if (hasManual)
        return self->m_active[manual] ? XButtonStationMode_Manual
                                      : XButtonStationMode_Auto;
    return self->m_active[aut] ? XButtonStationMode_Auto
                               : XButtonStationMode_Manual;
}

/**
 * @brief 模式发布（变化/首次发布才发信号；双触点矛盾沿发一次错误）。
 */
static void buttonPublishMode(XButtonStation* self)
{
    XButtonStationMode now = buttonDeriveMode(self);
    const int manual = (int)XButtonStationElement_ModeManual;
    const int aut = (int)XButtonStationElement_ModeAuto;
    bool contradiction = self->m_inputPt[manual] && self->m_inputPt[aut] &&
                         self->m_hasData[manual] && self->m_hasData[aut] &&
                         self->m_active[manual] && self->m_active[aut];

    if (contradiction != self->m_modeContradiction) {
        self->m_modeContradiction = contradiction;
        if (contradiction)
            XButtonStation_errorOccurred_signal(self,
                                               XButtonStationError_ModeContradiction);
    }
    if (self->m_modePublished && self->m_mode == now) return;
    self->m_mode = now;
    self->m_modePublished = true;
    XButtonStation_modeChanged_signal(self, (int)now);
}

/**
 * @brief 元素动作态发布：状态信号 + 点动边沿信号。
 * @param self 按钮组。
 * @param element 元素号。
 * @param active 新动作态。
 * @return 无。
 * @note 边沿只在**该元素已经发布过状态**之后才发：首次发布是挂接播种，
 *       上电就已合上的触点不算一次按键；而换点/解绑后的变化是真实变化
 *       （旧点确实按下过），必须发边沿，否则上层会一直以为按钮按着。
 */
static void buttonPublishActive(XButtonStation* self, int element, bool active)
{
    bool old = self->m_active[element];
    bool first = !self->m_inputPublished[element];

    self->m_active[element] = active;
    /* 播种发初值；此后仅变化才发（值未变且已发布过则静默）。 */
    if (!first && old == active) {
        /* 值未变：不发状态/边沿信号，但转换开关仍须重算位置——
         * 「首次读到数据」这种可用性变化不改值（上电就停在自动位、首拍读到
         * 的就是断开），却决定位置能否推断。见下方 mode 分支。 */
        buttonSyncFollowLamp(self, element);
        if (buttonIsModeSelector(element))
            buttonPublishMode(self);
        return;
    }
    self->m_inputPublished[element] = true;
    XButtonStation_inputChanged_signal(self, element, active);

    if (buttonIsPushButton(element) && !first) {
        if (active)
            XButtonStation_pressed_signal(self, element);
        else
            XButtonStation_released_signal(self, element);
    }
    if (element == (int)XButtonStationElement_EmergencyStop)
        XButtonStation_eStopChanged_signal(self, active);

    buttonSyncFollowLamp(self, element);
    if (buttonIsModeSelector(element))
        buttonPublishMode(self);
}

/**
 * @brief 重读一个元素触点并按绑定情况发布。
 * @param self 按钮组。
 * @param element 元素号。
 * @return 读到了有效数据返回 true。
 * @note 未绑定/无数据时的安全侧缺省：
 *       - 急停 → 动作态置 true（无法证明安全即按已拍下）；
 *       - 其余 → 静止（false）。
 *       急停缺省同样照常发布（状态信号），使上层上电即知安全侧状态。
 *       是否抑制 pressed/released 由「该元素是否已发布过状态」决定
 *       （见 buttonPublishActive），本函数无需区分播种与在线更新。
 */
static bool buttonRefreshElement(XButtonStation* self, int element)
{
    bool closed = false;
    bool ok = false;
    bool active;

    if (self->m_group && self->m_inputPt[element]) {
        ok = XIoChannelGroup_readDigitalInput(self->m_group,
                                             XIoPoint_role(self->m_inputPt[element]),
                                             &closed);
        if (ok) self->m_hasData[element] = true;
    }
    if (ok) {
        active = buttonInterpret(self, element, closed);
    } else if (!self->m_inputPt[element]) {
        /* 该元素根本没有触点：不参与（动作态恒静止，不发信号）。 */
        self->m_hasData[element] = false;
        if (self->m_inputPublished[element])
            buttonPublishActive(self, element, false);
        return false;
    } else {
        /* 已绑定但取不到数据：安全侧缺省（急停按已拍下）。 */
        active = (element == (int)XButtonStationElement_EmergencyStop);
    }
    buttonPublishActive(self, element, active);
    return ok;
}

/** @brief 按当前绑定重算转换开关位置（解绑/极性变更后收敛用）。 */
static void buttonRefreshMode(XButtonStation* self)
{
    buttonPublishMode(self);
}

/* ==================== 组信号槽（经 connect_1 连接，receiver=按钮组） ==================== */

/**
 * @brief 组输入变化槽：按触点 role 过滤本按钮组，更新对应元素。
 * @param receiver 连接的接收者（即本按钮组）；**不是**组，禁止直接强转。
 * @param args 信号参数（uint16_t role, int32_t value）。
 * @return 无。
 */
static void VXButtonStation_onInputChanged(XObject* receiver, XVarList* args)
{
    XButtonStation* self;
    int e;

    if (!receiver || !args) return;
    self = (XButtonStation*)receiver;
    XVarList_args_2(args, uint16_t, role, int32_t, value);
    (void)value;   /* 只信「该 role 变了」，值一律经组重读（口径统一）。 */
    for (e = 0; e < (int)XButtonStationElement_Count; ++e) {
        if (!self->m_inputPt[e] || role != XIoPoint_role(self->m_inputPt[e]))
            continue;
        buttonRefreshElement(self, e);
        return;    /* 一个 role 只可能对应一个元素。 */
    }
}

/**
 * @brief 组写落定槽：按指示灯 role 过滤本按钮组，发布异步失败沿。
 * @param receiver 连接的接收者（即本按钮组）；**不是**组，禁止直接强转。
 * @param args 信号参数（uint16_t role, bool ok）。
 * @return 无。
 * @note 同步链路在本槽到达前就已返回 true，本槽只在异步链路（Modbus 应答）
 *       上有实际作用；成功落定不重发 lampChanged（受理时已发）。
 */
static void VXButtonStation_onWriteDone(XObject* receiver, XVarList* args)
{
    XButtonStation* self;
    int e;

    if (!receiver || !args) return;
    self = (XButtonStation*)receiver;
    XVarList_args_2(args, uint16_t, role, bool, ok);
    if (ok) return;    /* 成功：受理时已发布，无需重复。 */
    for (e = 0; e < (int)XButtonStationElement_Count; ++e) {
        if (!self->m_lampPt[e] || role != XIoPoint_role(self->m_lampPt[e]))
            continue;
        /* 失败沿：清去重位，允许同值重写（否则这盏灯被永久锁在错误状态）。 */
        self->m_lampPublished[e] = false;
        if (self->m_lampWriting[e]) {
            /* 写调用栈内回传（同步链路的失败确认）：受理信号尚未发出，
             * 先记下由 buttonApplyLamp 抑制，避免「先亮后失败」的假象。 */
            self->m_lampFailed[e] = true;
        }
        XButtonStation_lampWriteFailed_signal(self, e);
        return;
    }
}

static void VXButtonStation_deinit(XObject* obj)
{
    XButtonStation* self = (XButtonStation*)obj;

    if (!self) return;
    if (self->m_group) {
        if (self->m_inputSignalAttached) {
            XObject_disconnect((XObject*)self->m_group,
                                 XSignal(XIoChannelGroup_inputChanged_signal),
                                 (XObject*)self,
                                 VXButtonStation_onInputChanged);
            self->m_inputSignalAttached = false;
        }
        if (self->m_lampSignalAttached) {
            XObject_disconnect((XObject*)self->m_group,
                                 XSignal(XIoChannelGroup_writeDone_signal),
                                 (XObject*)self,
                                 VXButtonStation_onWriteDone);
            self->m_lampSignalAttached = false;
        }
    }
    XClass_Deinit_Parent(XObject, obj);
}

/* ==================== 虚函数表初始化（单例） ==================== */

XVtable* XButtonStation_class_init(void)
{
    XVTABLE_INIT_DEFAULT(XButtonStation)
    XVTABLE_INHERIT_XCLASS(XObject);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Deinit, VXButtonStation_deinit);
    return XVTABLE_DEFAULT;
}

/* ==================== 构造函数 ==================== */

void XButtonStation_init(XButtonStation* obj)
{
    int e;
    if (!obj) return;
    memset(obj, 0, sizeof(XButtonStation));
    XObject_init((XObject*)obj);
    XClassSetVtable(obj, XButtonStation);
    obj->m_group = NULL;
    for (e = 0; e < (int)XButtonStationElement_Count; ++e) {
        obj->m_inputPt[e] = NULL;
        obj->m_lampPt[e] = NULL;
        /* 缺省极性：急停常闭（断开=拍下），其余常开（闭合=动作）。 */
        obj->m_activeHigh[e] =
            (e != (int)XButtonStationElement_EmergencyStop);
        obj->m_active[e] = false;
        obj->m_hasData[e] = false;
        obj->m_inputPublished[e] = false;
        obj->m_lampOn[e] = false;
        obj->m_lampPublished[e] = false;
        obj->m_lampFollow[e] = false;
    }
    obj->m_inputCount = 0;
    obj->m_lampCount = 0;
    obj->m_inputSignalAttached = false;
    obj->m_lampSignalAttached = false;
    obj->m_mode = XButtonStationMode_Unknown;
    obj->m_modePublished = false;
    obj->m_modeContradiction = false;
}

XButtonStation* XButtonStation_create(void)
{
    XButtonStation* obj = (XButtonStation*)XMemory_calloc(
        1, sizeof(XButtonStation), XCLASS_DEFAULT_MEMORY_TYPE);
    if (!obj) return NULL;
    XButtonStation_init(obj);
    Set_Class_IsHeap(obj, true);
    return obj;
}

/* ==================== 装配内部路径 ==================== */

/**
 * @brief 组归属校验：本组只接受同一个通道组。
 * @return 可接受返回 true；跨组已发错误返回 false。
 */
static bool buttonAcceptGroup(XButtonStation* self, XIoChannelGroup* group)
{
    if (!group) return false;
    if (self->m_group && self->m_group != group) {
        XButtonStation_errorOccurred_signal(self,
                                           XButtonStationError_DifferentGroup);
        return false;
    }
    self->m_group = group;
    return true;
}

/**
 * @brief 点归属与类型校验。
 * @param self 按钮组（组已确认）。
 * @param pt 待校验点。
 * @param type 要求类型。
 * @return 通过返回 true；未登记/类型不符已发错误返回 false。
 */
static bool buttonAcceptPoint(XButtonStation* self, XIoPoint* pt, XIoPointType type)
{
    if (!self->m_group || !pt) return false;
    if (!XIoChannelGroup_contains(self->m_group, pt) || XIoPoint_type(pt) != type) {
        XButtonStation_errorOccurred_signal(self, XButtonStationError_BadPoint);
        return false;
    }
    return true;
}

/** @brief 触点订阅（幂等；首个触点绑定时建立一次连接）。 */
static bool buttonEnsureInputSignal(XButtonStation* self)
{
    if (self->m_inputSignalAttached) return true;
    if (!self->m_group) return false;
    if (!XObject_connect((XObject*)self->m_group,
                           XSignal(XIoChannelGroup_inputChanged_signal),
                           (XObject*)self, VXButtonStation_onInputChanged,
                           XConnectionType_Auto))
        return false;
    self->m_inputSignalAttached = true;
    return true;
}

/** @brief 灯写落定订阅（幂等；首个灯绑定时建立一次连接）。 */
static bool buttonEnsureLampSignal(XButtonStation* self)
{
    if (self->m_lampSignalAttached) return true;
    if (!self->m_group) return false;
    if (!XObject_connect((XObject*)self->m_group,
                           XSignal(XIoChannelGroup_writeDone_signal),
                           (XObject*)self, VXButtonStation_onWriteDone,
                           XConnectionType_Auto))
        return false;
    self->m_lampSignalAttached = true;
    return true;
}

/** @brief 两个计数都归零时彻底解组（断开订阅并清组指针）。 */
static void buttonReleaseGroupIfIdle(XButtonStation* self)
{
    if (self->m_inputCount > 0 || self->m_lampCount > 0) return;
    if (!self->m_group) return;
    if (self->m_inputSignalAttached) {
        XObject_disconnect((XObject*)self->m_group,
                             XSignal(XIoChannelGroup_inputChanged_signal),
                             (XObject*)self, VXButtonStation_onInputChanged);
        self->m_inputSignalAttached = false;
    }
    if (self->m_lampSignalAttached) {
        XObject_disconnect((XObject*)self->m_group,
                             XSignal(XIoChannelGroup_writeDone_signal),
                             (XObject*)self, VXButtonStation_onWriteDone);
        self->m_lampSignalAttached = false;
    }
    self->m_group = NULL;
}

/* ==================== 装配 ==================== */

bool XButtonStation_attachInput(XButtonStation* self, XIoChannelGroup* group,
                                int element, XIoPoint* pt)
{
    if (ISNULL(self, "XButtonStation")) return false;
    if (!buttonElementOk(element) || !pt) return false;
    if (!buttonAcceptGroup(self, group)) return false;
    if (!buttonAcceptPoint(self, pt, XIoPoint_DigitalInput)) {
        buttonReleaseGroupIfIdle(self);
        return false;
    }
    if (!buttonEnsureInputSignal(self)) {
        buttonReleaseGroupIfIdle(self);
        return false;
    }
    if (!self->m_inputPt[element]) ++self->m_inputCount;
    self->m_inputPt[element] = pt;
    /* 换点时数据可信度归零：新点可能尚无数据（安全侧缺省随即生效）。 */
    self->m_hasData[element] = false;
    /* 播种：同步读新触点并按终值发布——旧点曾按下、新点未按下时，这一步
     * 就是那条 released（旧 true → 新 false 是一条真变化沿）。此处**不能**
     * 先补发一次「静止」，否则同一按键会凭空多出一条 released→pressed。 */
    buttonRefreshElement(self, element);
    return true;
}

bool XButtonStation_attachLamp(XButtonStation* self, XIoChannelGroup* group,
                               int element, XIoPoint* pt)
{
    if (ISNULL(self, "XButtonStation")) return false;
    if (!buttonElementOk(element) || !pt) return false;
    if (!buttonAcceptGroup(self, group)) return false;
    if (!buttonAcceptPoint(self, pt, XIoPoint_DigitalOutput)) {
        buttonReleaseGroupIfIdle(self);
        return false;
    }
    if (!buttonEnsureLampSignal(self)) {
        buttonReleaseGroupIfIdle(self);
        return false;
    }
    if (!self->m_lampPt[element]) ++self->m_lampCount;
    self->m_lampPt[element] = pt;
    /* 换灯：去重位归零（新灯实际状态未知，首次写必须落到线上）。 */
    self->m_lampPublished[element] = false;
    if (self->m_lampFollow[element])
        buttonApplyLamp(self, element, self->m_active[element], true);
    return true;
}

bool XButtonStation_detachInput(XButtonStation* self, int element)
{
    if (ISNULL(self, "XButtonStation")) return false;
    if (!buttonElementOk(element) || !self->m_inputPt[element]) return false;
    self->m_inputPt[element] = NULL;
    if (self->m_inputCount > 0) --self->m_inputCount;
    self->m_hasData[element] = false;
    /* 触点摘除：动作态回落静止（正被按下的点动按钮会发 released）。
     * 注意**不能先置 m_active 再发布**——发布函数靠「旧值 vs 新值」判变化，
     * 先改就永远看不到这条变化沿。 */
    if (self->m_inputPublished[element])
        buttonPublishActive(self, element, false);
    else if (buttonIsModeSelector(element))
        buttonRefreshMode(self);
    buttonReleaseGroupIfIdle(self);
    return true;
}

bool XButtonStation_detachLamp(XButtonStation* self, int element)
{
    if (ISNULL(self, "XButtonStation")) return false;
    if (!buttonElementOk(element) || !self->m_lampPt[element]) return false;
    self->m_lampPt[element] = NULL;
    if (self->m_lampCount > 0) --self->m_lampCount;
    self->m_lampPublished[element] = false;
    buttonReleaseGroupIfIdle(self);
    return true;
}

bool XButtonStation_detachAll(XButtonStation* self)
{
    int e;
    bool had = false;
    if (ISNULL(self, "XButtonStation")) return false;
    for (e = 0; e < (int)XButtonStationElement_Count; ++e) {
        if (self->m_inputPt[e]) {
            self->m_inputPt[e] = NULL;
            self->m_hasData[e] = false;
            if (self->m_inputPublished[e])
                buttonPublishActive(self, e, false);
            had = true;
        }
        if (self->m_lampPt[e]) {
            self->m_lampPt[e] = NULL;
            self->m_lampPublished[e] = false;
            had = true;
        }
    }
    self->m_inputCount = 0;
    self->m_lampCount = 0;
    buttonReleaseGroupIfIdle(self);
    return had;
}

XIoChannelGroup* XButtonStation_group(const XButtonStation* self)
{
    if (!self) return NULL;
    return self->m_group;
}

/* ==================== 配置 ==================== */

bool XButtonStation_setContactActiveHigh(XButtonStation* self, int element,
                                         bool activeHigh)
{
    if (ISNULL(self, "XButtonStation")) return false;
    if (!buttonElementOk(element)) return false;
    self->m_activeHigh[element] = activeHigh;
    /* 立即生效：已绑定元素用新极性重读重算（不播种口径——极性变更是显式
     * 配置动作，其导致的动作态翻转是真状态变化，应发边沿）。 */
    if (self->m_inputPt[element])
        buttonRefreshElement(self, element);
    else if (buttonIsModeSelector(element))
        buttonPublishMode(self);
    return true;
}

bool XButtonStation_contactActiveHigh(const XButtonStation* self, int element)
{
    if (!self || !buttonElementOk(element)) return false;
    return self->m_activeHigh[element];
}

bool XButtonStation_setLampFollow(XButtonStation* self, int element, bool follow)
{
    if (ISNULL(self, "XButtonStation")) return false;
    if (!buttonElementOk(element)) return false;
    self->m_lampFollow[element] = follow;
    if (follow && self->m_lampPt[element])
        buttonApplyLamp(self, element, self->m_active[element], true);
    return true;
}

bool XButtonStation_lampFollow(const XButtonStation* self, int element)
{
    if (!self || !buttonElementOk(element)) return false;
    return self->m_lampFollow[element];
}

/* ==================== 动作（仅指示灯） ==================== */

bool XButtonStation_setLamp(XButtonStation* self, int element, bool on)
{
    if (ISNULL(self, "XButtonStation")) return false;
    if (!buttonElementOk(element)) return false;
    if (!self->m_lampPt[element]) {
        /* 未绑灯：静默返回 false（无灯面板是常态，不算错误）。 */
        return false;
    }
    return buttonApplyLamp(self, element, on, false);
}

/* ==================== 查询 ==================== */

bool XButtonStation_isActive(const XButtonStation* self, int element)
{
    if (!self || !buttonElementOk(element)) return false;
    if (!self->m_inputPt[element]) return false;   /* 未绑定：不参与。 */
    return self->m_active[element];
}

bool XButtonStation_isEStopEngaged(const XButtonStation* self)
{
    const int estop = (int)XButtonStationElement_EmergencyStop;
    if (!self) return false;
    if (!self->m_inputPt[estop]) return false;     /* 未绑定：该面板无急停。 */
    return self->m_active[estop];
}

XButtonStationMode XButtonStation_mode(const XButtonStation* self)
{
    if (!self) return XButtonStationMode_Unknown;
    return self->m_mode;
}

bool XButtonStation_lampState(const XButtonStation* self, int element, bool* outOn)
{
    if (outOn) *outOn = false;
    if (!self || !buttonElementOk(element) || !self->m_lampPt[element]) return false;
    if (outOn) *outOn = self->m_lampOn[element];
    return true;
}

/* ==================== 信号 ==================== */

void* XButtonStation_inputChanged_signal(XButtonStation* self, int element,
                                         bool active)
{
    XEmitSignal(self, XButtonStation_inputChanged_signal,
                XVarList_Create(XVar(int, element), XVar(bool, active)),
                NULL, NULL, XEVENT_PRIORITY_NORMAL);
}

void* XButtonStation_pressed_signal(XButtonStation* self, int element)
{
    XEmitSignal(self, XButtonStation_pressed_signal,
                XVarList_Create(XVar(int, element)),
                NULL, NULL, XEVENT_PRIORITY_NORMAL);
}

void* XButtonStation_released_signal(XButtonStation* self, int element)
{
    XEmitSignal(self, XButtonStation_released_signal,
                XVarList_Create(XVar(int, element)),
                NULL, NULL, XEVENT_PRIORITY_NORMAL);
}

void* XButtonStation_eStopChanged_signal(XButtonStation* self, bool engaged)
{
    XEmitSignal(self, XButtonStation_eStopChanged_signal,
                XVarList_Create(XVar(bool, engaged)),
                NULL, NULL, XEVENT_PRIORITY_NORMAL);
}

void* XButtonStation_modeChanged_signal(XButtonStation* self, int mode)
{
    XEmitSignal(self, XButtonStation_modeChanged_signal,
                XVarList_Create(XVar(int, mode)),
                NULL, NULL, XEVENT_PRIORITY_NORMAL);
}

void* XButtonStation_lampChanged_signal(XButtonStation* self, int element, bool on)
{
    XEmitSignal(self, XButtonStation_lampChanged_signal,
                XVarList_Create(XVar(int, element), XVar(bool, on)),
                NULL, NULL, XEVENT_PRIORITY_NORMAL);
}

void* XButtonStation_lampWriteFailed_signal(XButtonStation* self, int element)
{
    XEmitSignal(self, XButtonStation_lampWriteFailed_signal,
                XVarList_Create(XVar(int, element)),
                NULL, NULL, XEVENT_PRIORITY_NORMAL);
}

void* XButtonStation_errorOccurred_signal(XButtonStation* self, int error)
{
    XEmitSignal(self, XButtonStation_errorOccurred_signal,
                XVarList_Create(XVar(int, error)),
                NULL, NULL, XEVENT_PRIORITY_NORMAL);
}

#endif /* XINDUSTRY_ON && XBUTTON_STATION_ON */
