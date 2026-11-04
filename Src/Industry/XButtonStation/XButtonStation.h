/**
 * @file       XButtonStation.h
 * @brief      按钮组（操作柱/操作箱）设备抽象公开 API：
 *             急停 / 启动 / 停止 / 复位 四个按钮 + 手动/自动转换开关 +
 *             按钮指示灯。
 * @details    对齐 Qt 无直接对应类（语义对标工业现场操作柱：一组带灯按钮
 *             与一只双位置转换开关）。设计完全对齐 XSolenoidValve /
 *             XTempHumTransmitter / XFlowMeter（2026-11-01 通道组重构形态）：
 *             纯业务层设备，只依赖通道接口族（通道组登记的
 *             DigitalInput/DigitalOutput 点，XIoPoint* 直传，信号经
 *             XIoChannelGroup 分发按 role 过滤），**不关心触点接在何处**——
 *             GPIO 干接点、Modbus 离散输入、CAN 报文位域……都由 Industry/XChannel
 *             的适配器在装配期注入（attach，借用不拥有）。本类永不 include
 *             任何具体传输头文件，也不认识任何链路/协议类型。
 *
 *             [方向：读触点 → 发语义事件]
 *             按钮与转换开关是**输入**（DI 触点）：本类只读不写，把电气状态
 *             解释成业务语义后发信号（pressed / released / eStopChanged /
 *             modeChanged），**动作由上层业务决定**（启泵、切自动……）。
 *             指示灯是**输出**（DO）：本类唯一写通道，用于点灯。
 *             按钮组不持有任何链路知识，也不替上层做联锁。
 *
 *             [按需绑定]
 *             现场面板千差万别：有的只有急停+启动+停止，有的没有转换开关，
 *             有的按钮不带灯。因此每个元素的两侧（触点/指示灯）都**独立可选
 *             绑定**：未绑定的元素不参与（不读、不写、不发信号，查询返回静态
 *             缺省），使用者只绑自己真正用到的按钮或开关，不需要凑齐六件套。
 *
 *             [元素下标：触点与指示灯共用]
 *             同一元素（如「启动」）的触点与指示灯用**同一个枚举下标**寻址，
 *             故 attachInput/attachLamp 传同一个 XButtonStationElement 值即可，
 *             无需维护第二张映射表——全部访问都是数组下标，O(1)，无遍历。
 *
 *             [触点极性可配（安全侧口径）]
 *             电气侧「触点闭合」不等于业务侧「按钮按下」：常开按钮闭合=按下，
 *             常闭急停断开=拍下。故每个输入元素有一个极性位
 *             m_activeHigh（见 XButtonStation_setContactActiveHigh）：
 *               - true （缺省，常开触点）：触点闭合 = 元素动作态；
 *               - false（常闭触点）  ：触点断开 = 元素动作态。
 *             缺省值：急停为 false（常闭，失电即拍下），其余为 true。
 *
 *             [急停的安全侧缺省]
 *             **绑定但取不到数据**的急停按「已拍下」处理（engaged=true）——
 *             触点线断、从站失联、链路未启动都属于「无法证明安全」，
 *             工业安全侧一律按最坏情况处理。未绑定急停则视为该面板没有急停
 *             （engaged=false，不发任何信号），不会让每个部署都被误报。
 *
 *             [信号口径：状态信号 vs 边沿信号]
 *               - 状态信号（inputChanged / eStopChanged / modeChanged）：
 *                 表达「现在是什么状态」，挂接播种时发布初值（未知→初值算
 *                 一次变化），无数据时急停落 engaged=true 也会发布；
 *               - 边沿信号（pressed / released）：表达「发生了一次按键」，
 *                 **播种不发**——上电时已经合上的启动触点不算一次按键，
 *                 否则设备会在启动瞬间被触发一次。
 *             状态去重：值未变化静默（与通道组「变化才通知」契约一致）。
 *
 *             [转换开关]
 *             单触点或双触点都能用：
 *               - 只绑 ModeManual：触点在本位置=Manual，否则=Auto；
 *               - 只绑 ModeAuto  ：触点在本位置=Auto，否则=Manual；
 *               - 两个都绑      ：两者同时动作态=矛盾（发
 *                                 ModeContradiction，模式落 Unknown）；
 *                                 两者都静止=位置不明（Unknown，不报错）；
 *               - 已绑定但无数据：模式落 Unknown（不做任何推断）。
 *             触点极性同样按 setContactActiveHigh 解释，故常闭/常开接线
 *             不影响模式语义。
 *
 *             [指示灯]
 *             attachLamp 绑定 DO 点后两种驱动方式：
 *               - 手动：XButtonStation_setLamp 直接点/灭（上层按自己状态机点灯）；
 *               - 跟随：XButtonStation_setLampFollow 打开后自动镜像本元素状态
 *                 （按钮=按下态、转换开关=当前位置），用于「按下即亮」的
 *                 带灯按钮或位置指示灯。跟随打开时由本类接管该灯，上层
 *                 不应再手动点同一盏。
 *             写结果：受理即发 lampChanged；异步链路（Modbus 等）落定失败
 *             经组 writeDone 回传后发 lampWriteFailed，写失败**不静默**。
 *
 *             设备对象为句柄性质：禁止拷贝/移动语义；对象可能在自身信号
 *             调用栈上被删除时，调用方必须使用 XObject_deleteLater。
 */
#ifndef XBUTTONSTATION_H
#define XBUTTONSTATION_H

#ifdef __cplusplus
extern "C" {
#endif

#include "CXinYueConfig.h"
#include "XIndustry_config.h"
#include "XObject.h"
#include "XChannel/XIoChannelGroup.h"

#if XINDUSTRY_ON && XBUTTON_STATION_ON

/**
 * @brief 按钮组元素号（触点与指示灯共用同一套下标）。
 * @details 0..3 是点动按钮（按下发 pressed，松开发 released）；4..5 是
 *          转换开关的两个位置（保持型，只在 inputChanged/modeChanged 上体现）。
 *          同一元素号的触点与指示灯一一对应，如
 *          `XButtonStationElement_Start` 既是启动按钮触点，也是启动按钮指示灯。
 * @note 枚举值即组内数组下标，新增元素须追加在 Count 之前（不得插队，
 *       否则既有装配的语义编号整体平移）。
 */
typedef enum XButtonStationElement {
    XButtonStationElement_EmergencyStop = 0, /**< 急停（缺省常闭触点；带灯）。 */
    XButtonStationElement_Start,             /**< 启动（点动；带灯）。 */
    XButtonStationElement_Stop,              /**< 停止（点动；带灯）。 */
    XButtonStationElement_Reset,             /**< 复位（点动；带灯）。 */
    XButtonStationElement_ModeManual,        /**< 手动/自动转换开关·手动位。 */
    XButtonStationElement_ModeAuto,          /**< 手动/自动转换开关·自动位。 */
    XButtonStationElement_Count              /**< 元素个数（数组容量；非元素）。 */
} XButtonStationElement;

/**
 * @brief 转换开关当前位置（由已绑定的触点推导）。
 */
typedef enum XButtonStationMode {
    XButtonStationMode_Unknown = 0, /**< 位置不明：未绑定、无数据、双触点矛盾或双静默。 */
    XButtonStationMode_Manual,      /**< 手动位。 */
    XButtonStationMode_Auto         /**< 自动位。 */
} XButtonStationMode;

/**
 * @brief 按钮组错误码。
 */
typedef enum XButtonStationError {
    XButtonStationError_None = 0,             /**< 无错误。 */
    XButtonStationError_NotAttached,          /**< 元素未绑定通道组/点，操作被拒绝。 */
    XButtonStationError_BadPoint,             /**< 点未登记于该组或类型与元素不符（装配错误）。 */
    XButtonStationError_DifferentGroup,       /**< 本组已挂接另一通道组（跨组混挂拒绝）。 */
    XButtonStationError_LampWriteRejected,    /**< 指示灯写被同步拒绝（未挂链路/忙/类型不符）。 */
    XButtonStationError_LampWriteFailed,      /**< 指示灯写异步落定失败（writeDone(false)）。 */
    XButtonStationError_ModeContradiction     /**< 手动/自动两触点同时为动作态，位置矛盾。 */
} XButtonStationError;

/* ==================== 虚函数表定义（继承 XObject，不新增虚槽） ==================== */
XCLASS_DEFINE_BEGING(XButtonStation)
XCLASS_DEFINE_EXTEND_END(XButtonStation, XObject)

/**
 * @brief XButtonStation 按钮组。
 * @details 成员禁止直接访问，一律经 API 操作。通道组与两侧点均为借用，
 *          不拥有：调用方保证其生命周期覆盖按钮组，并在其销毁前 detach
 *          或先删按钮组。
 */
typedef struct XButtonStation {
    XObject m_class;                          /**< 第一个成员，基类 XObject，由 XClass 管理，禁止手工修改。 */
    XIoChannelGroup* m_group;                 /**< 绑定的通道组（借用，可为 NULL=未挂接）；读触点与点灯经组分发。 */
    XIoPoint* m_inputPt[XButtonStationElement_Count]; /**< 触点[元素]（组内 DigitalInput 点）；NULL=该元素无触点。 */
    XIoPoint* m_lampPt[XButtonStationElement_Count];  /**< 指示灯[元素]（组内 DigitalOutput 点）；NULL=该元素无灯。 */
    bool m_activeHigh[XButtonStationElement_Count];   /**< 触点极性[元素]：true=闭合即动作态（常开）；false=断开即动作态（常闭）。 */
    bool m_active[XButtonStationElement_Count];       /**< 已解释的动作态[元素]（含无数据时的安全侧缺省）。 */
    bool m_hasData[XButtonStationElement_Count];      /**< 该触点是否读到过有效数据（决定模式可否推断）。 */
    bool m_inputPublished[XButtonStationElement_Count]; /**< 动作态是否已发布过（区分播种与边沿）。 */
    bool m_lampOn[XButtonStationElement_Count];       /**< 最近一次受理的灯态[元素]。 */
    bool m_lampPublished[XButtonStationElement_Count];/**< 灯态是否已写过（写去重）。 */
    bool m_lampFollow[XButtonStationElement_Count];   /**< 灯是否跟随本元素状态自动驱动。 */
    bool m_lampWriting[XButtonStationElement_Count];  /**< 该灯正处于 write 调用栈内（区分同步回传与异步落定）。 */
    bool m_lampFailed[XButtonStationElement_Count];   /**< write 调用栈内已收到失败回传（写返回后据此抑制 lampChanged）。 */
    int m_inputCount;                         /**< 已绑触点个数（O(1) 判空，免遍历）。 */
    int m_lampCount;                          /**< 已绑指示灯个数（O(1) 判空，免遍历）。 */
    bool m_inputSignalAttached;               /**< 组 inputChanged 是否已订阅（全组一次）。 */
    bool m_lampSignalAttached;                /**< 组 writeDone 是否已订阅（全组一次）。 */
    XButtonStationMode m_mode;                /**< 推导出的转换开关位置，默认 Unknown。 */
    bool m_modePublished;                     /**< 模式是否已发布过（区分播种与变化）。 */
    bool m_modeContradiction;                 /**< 上次推导是否处于矛盾态（矛盾沿才发错误）。 */
} XButtonStation;

/* ==================== 虚函数表初始化 / 构造与析构 ==================== */

/**
 * @brief 初始化 XButtonStation 类的虚函数表（单例）。
 * @return 类虚函数表指针；不可释放。
 */
XVtable* XButtonStation_class_init(void);

/**
 * @brief 栈上初始化按钮组。
 * @param obj 目标对象；不能为 NULL，可为未初始化内存（内部先清零）。
 * @return 无。参数为 NULL 时不执行任何操作。
 * @note 与 XClassDeinit 成对出现；初始未挂接、无任何元素绑定、
 *       急停触点极性为常闭（false）、其余为常开（true）、模式 Unknown。
 */
void XButtonStation_init(XButtonStation* obj);

/**
 * @brief 堆上创建按钮组。
 * @return 新对象；内存分配失败返回 NULL。不再使用时必须 XClassDelete；
 *         对象可能仍被信号链引用时改用 XObject_deleteLater。
 */
XButtonStation* XButtonStation_create(void);

/* ==================== 装配（借用注入，解耦点） ==================== */

/**
 * @brief 绑定一个元素的触点（DI）。
 * @param self 目标按钮组；不能为 NULL。
 * @param group 通道组；借用，不能为 NULL。本组只接受**同一个**通道组，
 *              已挂接别组时拒绝（DifferentGroup）——面板接线天然同组，
 *              跨组混挂是装配错误，暴露而不是悄悄并存。
 * @param element 元素号（0..Count-1）。
 * @param pt 触点（组内已登记的 DigitalInput 点）；借用，不能为 NULL。
 * @return 绑定成功 true；参数非法、点未登记于该组、类型不符、跨组返回 false
 *         （后三者已发 errorOccurred）。
 * @note 重复绑同一元素换点：先按新点重新播种。绑定后立即同步读一次触点
 *       发布状态快照（不发 pressed/released 边沿，见文件头信号口径）。
 */
bool XButtonStation_attachInput(XButtonStation* self, XIoChannelGroup* group,
                                int element, XIoPoint* pt);

/**
 * @brief 绑定一个元素的指示灯（DO）。
 * @param self 目标按钮组；不能为 NULL。
 * @param group 通道组；借用，不能为 NULL（同 attachInput 的同组约束）。
 * @param element 元素号（0..Count-1）。
 * @param pt 指示灯（组内已登记的 DigitalOutput 点）；借用，不能为 NULL。
 * @return 绑定成功 true；校验失败返回 false（同 attachInput）。
 * @note 已打开跟随（setLampFollow）的元素，绑定后立即按当前状态点一次灯。
 */
bool XButtonStation_attachLamp(XButtonStation* self, XIoChannelGroup* group,
                               int element, XIoPoint* pt);

/**
 * @brief 解绑一个元素的触点。
 * @param self 目标按钮组；不能为 NULL。
 * @param element 元素号。
 * @return 解绑了已绑定的触点返回 true；原本未绑定返回 false。
 * @note 解绑把该元素动作态回落为静止（发状态变化；点动元素发 released），
 *       模式随之重算。本组最后一个点解绑后自动断开组信号订阅并解组。
 */
bool XButtonStation_detachInput(XButtonStation* self, int element);

/**
 * @brief 解绑一个元素的指示灯。
 * @param self 目标按钮组；不能为 NULL。
 * @param element 元素号。
 * @return 解绑了已绑定的指示灯返回 true；原本未绑定返回 false。
 * @note 解绑不写灯、不改变灯态缓存；本组最后一个点解绑后自动解组。
 */
bool XButtonStation_detachLamp(XButtonStation* self, int element);

/**
 * @brief 解绑全部触点与指示灯并断开组信号订阅。
 * @param self 目标按钮组；不能为 NULL。
 * @return 解绑了任一点返回 true；原本未挂接返回 false。
 * @note 各元素动作态回落为静止并按绑定情况发布变化；指示灯不被驱动。
 */
bool XButtonStation_detachAll(XButtonStation* self);

/**
 * @brief 读绑定的通道组（借用）。
 * @param self 目标按钮组；传入 NULL 返回 NULL。
 * @return 通道组指针；未绑定返回 NULL。
 */
XIoChannelGroup* XButtonStation_group(const XButtonStation* self);

/* ==================== 配置 ==================== */

/**
 * @brief 设置触点极性（电气侧 → 业务侧的解释）。
 * @param self 目标按钮组；不能为 NULL。
 * @param element 元素号。
 * @param activeHigh true=触点闭合即动作态（常开触点）；
 *                   false=触点断开即动作态（常闭触点，急停缺省）。
 * @return 设置成功 true；元素号越界返回 false。
 * @note 立即生效：已绑定元素用新极性重读重算，动作态/模式变化照常发布。
 */
bool XButtonStation_setContactActiveHigh(XButtonStation* self, int element,
                                         bool activeHigh);

/**
 * @brief 查询触点极性。
 * @param self 目标按钮组；传入 NULL 返回 false。
 * @param element 元素号。
 * @return true=常开；false=常闭或元素号越界。
 */
bool XButtonStation_contactActiveHigh(const XButtonStation* self, int element);

/**
 * @brief 设置指示灯跟随（自动镜像本元素状态）。
 * @param self 目标按钮组；不能为 NULL。
 * @param element 元素号。
 * @param follow true=由本类驱动该灯；false=交回上层手动点灯。
 * @return 设置成功 true；元素号越界返回 false。
 * @note 打开时立即按当前状态驱动一次（已绑灯才写）。跟随打开期间
 *       上层不应再对同一盏调用 setLamp（以最后一次调用为准，不做仲裁）。
 */
bool XButtonStation_setLampFollow(XButtonStation* self, int element, bool follow);

/**
 * @brief 查询指示灯是否跟随。
 * @param self 目标按钮组；传入 NULL 返回 false。
 * @param element 元素号。
 * @return true=跟随。
 */
bool XButtonStation_lampFollow(const XButtonStation* self, int element);

/* ==================== 动作（仅指示灯） ==================== */

/**
 * @brief 点/灭一个指示灯（手动驱动）。
 * @param self 目标按钮组；不能为 NULL。
 * @param element 元素号。
 * @param on true=亮，false=灭。
 * @return 写意图已受理（或已是该状态无需重写）返回 true；元素未绑灯、
 *         未挂组、同步被拒返回 false（后两者已发 errorOccurred）。
 * @note 与上次受理值相同则不下发（免总线抖动）；异步链路的落定失败经
 *       lampWriteFailed 上报。
 */
bool XButtonStation_setLamp(XButtonStation* self, int element, bool on);

/* ==================== 查询 ==================== */

/**
 * @brief 查元素动作态（已按极性解释）。
 * @param self 目标按钮组；可为 NULL。
 * @param element 元素号。
 * @return 动作态：按钮=按下、急停=已拍下、转换开关=处于该位置；
 *         未绑定返回 false（不参与）。
 * @note 急停绑定但无数据时返回 true（安全侧，见文件头）。
 */
bool XButtonStation_isActive(const XButtonStation* self, int element);

/**
 * @brief 查急停是否已拍下。
 * @param self 目标按钮组；可为 NULL。
 * @return 已拍下（含「已绑定但无数据」的安全侧缺省）返回 true；
 *         未绑定或未拍下返回 false。
 */
bool XButtonStation_isEStopEngaged(const XButtonStation* self);

/**
 * @brief 读转换开关当前位置。
 * @param self 目标按钮组；传入 NULL 返回 Unknown。
 * @return 推导出的位置；无法推断时为 Unknown（见文件头）。
 */
XButtonStationMode XButtonStation_mode(const XButtonStation* self);

/**
 * @brief 读最近一次受理的灯态。
 * @param self 目标按钮组；可为 NULL。
 * @param element 元素号。
 * @param outOn 输出灯态（调用方提供；可为 NULL 忽略）。
 * @return 该元素已绑定指示灯返回 true；未绑定返回 false（outOn 写 false）。
 */
bool XButtonStation_lampState(const XButtonStation* self, int element, bool* outOn);

/* ==================== 信号 ==================== */

/**
 * @brief inputChanged 信号：元素动作态变化（状态信号，播种发布初值）。
 * @param self 发射信号的按钮组；经 XSignal() 宏取地址连接，禁止直调。
 * @param element 变化的元素号（XButtonStationElement）。
 * @param active 变化后的动作态（已按触点极性解释）。
 * @return 信号函数地址；仅供 XSignal() 宏取地址使用。
 */
void* XButtonStation_inputChanged_signal(XButtonStation* self, int element,
                                         bool active);

/**
 * @brief pressed 信号：点动按钮进入按下态（边沿信号，播种不发）。
 * @param self 发射信号的按钮组；经 XSignal() 宏取地址连接，禁止直调。
 * @param element 按钮元素号（Start/Stop/Reset；急停走 eStopChanged，
 *                转换开关不会发本信号）。
 * @return 信号函数地址；仅供 XSignal() 宏取地址使用。
 */
void* XButtonStation_pressed_signal(XButtonStation* self, int element);

/**
 * @brief released 信号：点动按钮回到松开态（边沿信号）。
 * @param self 发射信号的按钮组；经 XSignal() 宏取地址连接，禁止直调。
 * @param element 按钮元素号（Start/Stop/Reset）。
 * @return 信号函数地址；仅供 XSignal() 宏取地址使用。
 */
void* XButtonStation_released_signal(XButtonStation* self, int element);

/**
 * @brief eStopChanged 信号：急停拍下/复位（状态信号，播种发布初值）。
 * @param self 发射信号的按钮组；经 XSignal() 宏取地址连接，禁止直调。
 * @param engaged true=已拍下（含无数据的安全侧缺省），false=正常。
 * @return 信号函数地址；仅供 XSignal() 宏取地址使用。
 */
void* XButtonStation_eStopChanged_signal(XButtonStation* self, bool engaged);

/**
 * @brief modeChanged 信号：转换开关位置变化（状态信号，播种发布初值）。
 * @param self 发射信号的按钮组；经 XSignal() 宏取地址连接，禁止直调。
 * @param mode 变化后的位置（XButtonStationMode）。
 * @return 信号函数地址；仅供 XSignal() 宏取地址使用。
 */
void* XButtonStation_modeChanged_signal(XButtonStation* self, int mode);

/**
 * @brief lampChanged 信号：指示灯写意图已受理（灯态缓存已更新）。
 * @param self 发射信号的按钮组；经 XSignal() 宏取地址连接，禁止直调。
 * @param element 元素号。
 * @param on 受理的灯态。
 * @return 信号函数地址；仅供 XSignal() 宏取地址使用。
 */
void* XButtonStation_lampChanged_signal(XButtonStation* self, int element, bool on);

/**
 * @brief lampWriteFailed 信号：指示灯写异步落定失败。
 * @param self 发射信号的按钮组；经 XSignal() 宏取地址连接，禁止直调。
 * @param element 元素号。
 * @return 信号函数地址；仅供 XSignal() 宏取地址使用。
 * @note 仅异步链路上会出现（同步链路当场给出受理结果）；失败后该灯
 *       允许被同值重写（去重位复位）。
 */
void* XButtonStation_lampWriteFailed_signal(XButtonStation* self, int element);

/**
 * @brief errorOccurred 信号：按钮组发生错误。
 * @param self 发射信号的按钮组；经 XSignal() 宏取地址连接，禁止直调。
 * @param error 错误码（XButtonStationError 枚举值）。
 * @return 信号函数地址；仅供 XSignal() 宏取地址使用。
 */
void* XButtonStation_errorOccurred_signal(XButtonStation* self, int error);

#endif /* XINDUSTRY_ON && XBUTTON_STATION_ON */

#ifdef __cplusplus
}
#endif
#endif /* XBUTTONSTATION_H */
