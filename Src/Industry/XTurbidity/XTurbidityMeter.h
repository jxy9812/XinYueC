/**
 * @file       XTurbidityMeter.h
 * @brief      浊度仪设备抽象公开 API（通用，不绑定传输）。
 * @details    对齐 Qt 无直接对应类（语义对标工业现场在线浊度监测仪，如上仪
 *             WZT-701A / 梅特勒 InPro 系列）。设计完全对齐 XFlowMeter /
 *             XTempHumTransmitter（2026-11-01 通道组重构形态）：纯业务层设备，
 *             只依赖通道接口族（通道组登记的点，XIoPoint* 直传，信号经
 *             XIoChannelGroup 分发按 role 过滤），**不关心数据来自何处**——
 *             Modbus RTU / Modbus TCP / 4-20mA 模拟量 / CAN / 任何未来传输都
 *             经 Industry/XChannel 的适配器在装配期注入（attach，借用不拥有）。
 *             本类永不 include 任何具体传输头文件，也不假设站号/波特率/端口。
 *
 *             [两种数据源形态 —— 这是"通用"的全部内容]
 *             浊度仪在市场上只有两类电气接口，本类各给一条路，装配期选一次：
 *
 *               ① Words（数字量，RS-485/Modbus 一类）
 *                  浊度占据**连续两个 16 位寄存器**，拼成一个 32 位量再解释。
 *                  口径旋钮必须可配，因为它们**各厂家不同**：
 *                    · 字序（word order）：CDAB（低字在前）/ ABCD（高字在前）/
 *                      DCBA（整字节反序=小端，2026-11-09 ZHEYI-5100ZDY 实测新增）
 *                    · 格式（format）：IEEE754 单精度 / 无符号整数 / 有符号整数
 *                    · 除数（divisor）：整数格式下的 raw / divisor = NTU
 *                  缺省值取**实测真机**口径（WZT-701A：CDAB + Float32），
 *                  见下面 [真机实测帧]。带温度的机型（ZHEYI-5100ZDY 按
 *                  0x0000×4 一帧读浊度+温度）由装配层多登记两个点（role
 *                  2/3），本类自动同刻解出温度（XTurbidityMeter_temperature）。
 *
 *               ② Analog（模拟量，4-20mA 一类）
 *                  浊度只占**一路**模拟量输入点，点内已有通道层标度
 *                  （原始码 → 通道工程值）。本类再做第二级标度
 *                  （通道工程值 → 0.01NTU 定点），缺省 1/1/0 直通。
 *
 *             [真机实测帧 —— 缺省口径的来源]
 *             WZT-701A 说明书 4.3.10.2「测试命令」给出的可复现帧：
 *                 发送：01 03 00 00 00 02 C4 0B
 *                 回复：01 03 04 D7 0A 41 1F 92 1D
 *                 原文：「按照低字在前高字在后传输(CDAB)，浊度为 2 字
 *                       16 进制数 D7 0A 41 1F 对应的浮点型数据为 9.99NTU」
 *             即 raw32 = (D70A) | (411F << 16) = 0x411FD70A，
 *             memcpy 成 float 恰为 9.99f（本类测试用该帧逐字节回归）。
 *
 *             [与电磁流量计的口径不同，不要照抄]
 *             XFlowMeter 的 32 位量是**高字在前（ABCD）**；本类缺省是**低字在前
 *             （CDAB）**。这不是笔误：两者来自不同厂家的协议表。任何"统一成
 *             一种字序"的改动都会让其中一台读数变成天文数字——故字序是运行期
 *             可配项，而不是写死的常量。
 *
 *             [为什么用点缓存/链路原始值拼装，而不是读点工程值相加]
 *             32 位量的两个字分属两次寄存器的值，必须在**同一帧**内取值。
 *             本类经 XIoLink_readRole_base 读原始字（读的是 Hub 缓存快照），
 *             一帧整块回填 ⇒ 两次读同源。链路不可用时（纯 mock 场景）回落
 *             点缓存，此时要求点标度恒等（1/1/0）——这是装配约定，测试与
 *             装配层共同保证（与 XFlowMeter 同款）。
 *
 *             [值类型] 浊度用 double（NTU）：量程 0.00~1000.0，分辨率 0.01，
 *             用 int32 定点要么装不下量程要么丢小数。与 XFlowMeter 的
 *             瞬时流量同为 double 口径。
 *
 *             [质量口径] 与 XFlowMeter / XTempHumTransmitter 完全一致：已收到
 *             至少一拍 && 未陈旧（staleMs，0=不判陈旧），巡检定时器翻转沿发
 *             qualityChanged。
 *
 *             [只读设备] 本类无控制面：不提供标定 / 偏移量 / 清洗触发 / 报警
 *             限值写入。手册确认这些都在仪表面板上做（4.3.3 标定设置、
 *             4.3.8 清洗控制），且对外通讯只暴露测量值（4.3.10：仅 FC03
 *             读 0x0000×2）。若将来需要写参数，按 XVfdDevice 的参数读写形态
 *             另开一组 API，不在本类里混进链路知识。
 *
 *             设备对象为句柄性质：禁止拷贝/移动语义；对象可能在自身信号
 *             调用栈上被删除时，调用方必须使用 XObject_deleteLater。
 */
#ifndef XTURBIDITYMETER_H
#define XTURBIDITYMETER_H

#ifdef __cplusplus
extern "C" {
#endif

#include "CXinYueConfig.h"
#include "XIndustry_config.h"
#include "XObject.h"
#include "XChannel/XIoChannelGroup.h"

#if XINDUSTRY_ON && XTURBIDITY_ON

/**
 * @brief 语义槽号（role）。
 * @details 组内 role 即下标，故必须 0 基连续。
 *
 *          | role | Words 模式（数字量）        | Analog 模式（模拟量）  |
 *          |------|-----------------------------|------------------------|
 *          | 0    | 首字（配合 m_wordOrder 解释）| 唯一一路模拟量输入     |
 *          | 1    | 次字（浊度 32 位量）        | 不使用                 |
 *          | 2    | 温度低字（可选，见下）      | 不使用                 |
 *          | 3    | 温度高字（可选，见下）      | 不使用                 |
 *
 *          [为什么 Words 模式的前两个 role 固定相邻] 装配层按「从站起始地址 +
 *          偏移」登记点，相邻两寄存器天然相邻；把 role 做成可配项只会让
 *          装配代码多一处可以写错的地方，且没有任何现场需要非相邻。字序
 *          的差异由 XTurbidityMeterWordOrder 表达，与 role 无关。
 *
 *          [role 2/3 为什么是"可选"] 2026-11-09 板端实测（ttyS4 总线）：
 *          ZHEYI-5100ZDY（2026-11-10 起面板站号 3）按 `0x0000×4` 一帧同刻读出浊度 + 温度
 *          （前两字浊度、后两字温度，字序与浊度一致）；WZT-701A（站 1）
 *          只有 2 个寄存器、无温度。装配层给带温度的机型登记 4 个点、
 *          不带的登记 2 个——role 2/3 **同时**存在才解温度，缺一个按
 *          "无温度"处理（不报错、不解半截）。温度只进设备层缓存与日志，
 *          不进遥测推送帧（0x00010009 形状不变）。
 */
typedef enum XTurbidityMeterRole {
    XTurbidityMeterRole_Word0 = 0,  /**< 首字（浊度 32 位量）/ 模拟量通道。 */
    XTurbidityMeterRole_Word1 = 1,  /**< 次字（浊度 32 位量；Analog 模式不用）。 */
    XTurbidityMeterRole_Word2 = 2,  /**< 温度低字（可选；两字齐备才解温度）。 */
    XTurbidityMeterRole_Word3 = 3,  /**< 温度高字（可选）。 */
    XTurbidityMeterRole_Count = 4   /**< 槽位总数（装配用上界，非真实槽）。 */
} XTurbidityMeterRole;

/**
 * @brief 数据源形态（装配期选一次）。
 */
typedef enum XTurbidityMeterSourceMode {
    XTurbidityMeterSource_Words = 0, /**< 两个 16 位字拼 32 位量（数字接口）。 */
    XTurbidityMeterSource_Analog = 1 /**< 一路模拟量（4-20mA 等模拟接口）。 */
} XTurbidityMeterSourceMode;

/**
 * @brief 32 位量的字序（**各厂家不同，必须可配**）。
 * @details 线上 4 字节 b0..b3（Modbus 寄存器载荷：w0=b0b1、w1=b2b3，寄存器
 *          内高字节在前）拼成 float32 的四种常见口径里，本类支持三种：
 *            LowHigh（CDAB）：bits = ((uint32_t)w1 << 16) | w0
 *            HighLow（ABCD）：bits = ((uint32_t)w0 << 16) | w1
 *            Dcba（整字节反序 = 小端）：bits 反推为
 *              ((w1&0xFF)<<24) | ((w1&0xFF00)<<8) | ((w0&0xFF)<<8) | (w0>>8)
 *              等价于「先按 ABCD 拼好再整 32 位字节反序」。
 *          其中 w0/w1 是 role 0/1 的原始 16 位字。
 *
 *          [Dcba 的来历] 2026-11-09 板端实测：ZHEYI-5100ZDY（时为站 2，
          2026-11-10 起面板改 3）的
 *          应答帧 `4F DD 37 3D` 按 DCBA（=struct.unpack("<f")）解出
 *          0.0449 NTU，与面板显示一致；按旧 CDAB 口径解出 1.1e-5（小 4
 *          个数量级的假数且不报错）。四口径对照见实测文档 §5.3/§7。
 */
typedef enum XTurbidityMeterWordOrder {
    XTurbidityMeterWordOrder_LowHigh = 0, /**< 低字在前（CDAB；WZT-701A 实测口径）。 */
    XTurbidityMeterWordOrder_HighLow = 1, /**< 高字在前（ABCD；流量计等同款口径）。 */
    XTurbidityMeterWordOrder_Dcba = 2     /**< 整字节反序（DCBA=小端 float32；ZHEYI-5100ZDY 实测口径）。 */
} XTurbidityMeterWordOrder;

/**
 * @brief 32 位量的数值格式。
 */
typedef enum XTurbidityMeterWordFormat {
    XTurbidityMeterWordFormat_Float32 = 0,   /**< IEEE754 单精度（WZT-701A 实测口径）。 */
    XTurbidityMeterWordFormat_UInt32Scaled = 1, /**< 无符号整数：NTU = bits / divisor。 */
    XTurbidityMeterWordFormat_Int32Scaled = 2   /**< 有符号整数：NTU = (int32)bits / divisor。 */
} XTurbidityMeterWordFormat;

/**
 * @brief 浊度仪错误码。
 */
typedef enum XTurbidityMeterError {
    XTurbidityMeterError_None = 0,      /**< 无错误。 */
    XTurbidityMeterError_NotAttached,   /**< 输入未挂接，操作被拒绝。 */
    XTurbidityMeterError_BadConfig,     /**< 配置非法（如 divisor=0、den=0、role 缺失）。 */
    XTurbidityMeterError_Internal       /**< 内部资源失败（如质量巡检定时器启动失败）。 */
} XTurbidityMeterError;

/* ==================== 虚函数表定义（继承 XObject，不新增虚槽） ==================== */
XCLASS_DEFINE_BEGING(XTurbidityMeter)
XCLASS_DEFINE_EXTEND_END(XTurbidityMeter, XObject)

/**
 * @brief XTurbidityMeter 在线浊度仪。
 * @details 成员禁止直接访问，一律经 API 操作。通道组与输入点均为借用，
 *          不拥有：调用方保证其生命周期覆盖浊度仪，并在其销毁前 detach
 *          或先删浊度仪。
 */
typedef struct XTurbidityMeter {
    XObject m_class;                    /**< 第一个成员，基类 XObject，由 XClass 管理，禁止手工修改。 */
    XIoChannelGroup* m_group;           /**< 通道组（借用，可为 NULL=未挂接）；数据与信号经组分发。 */
    XIoPoint* m_points[XTurbidityMeterRole_Count]; /**< 点表（role 作下标；NULL=该 role 未登记）。 */
    XTurbidityMeterSourceMode m_source; /**< 数据源形态（Words / Analog）。 */
    XTurbidityMeterWordOrder m_wordOrder;   /**< 32 位量字序（Words 模式）。 */
    XTurbidityMeterWordFormat m_wordFormat; /**< 32 位量格式（Words 模式）。 */
    double m_wordDivisor;               /**< 整数格式的除数（Words 模式）；缺省 100.0，0 非法。 */
    int32_t m_scaleNum;                 /**< 二级标度分子（Analog 模式）：fixed01 = chEng*num/den + offset。 */
    int32_t m_scaleDen;                 /**< 二级标度分母；0 非法（set 侧拒绝）。 */
    int32_t m_scaleOffset;              /**< 二级标度偏移（0.01NTU 定点，与 fixed01 同单位）。 */
    double m_ntu;                       /**< 最近浊度（NTU；未收到数据为 0）。 */
    uint32_t m_rawBits;                 /**< Words 模式：最近拼装出的 32 位原始位模式（诊断用）。 */
    int32_t m_analogEng;                /**< Analog 模式：最近一次通道工程值（诊断用）。 */
    double m_tempC;                     /**< 最近水温（℃；机型不带温度恒 0）。 */
    uint32_t m_tempRawBits;             /**< Words 模式：温度 32 位原始位模式（诊断用）。 */
    bool m_tempReceived;                /**< 是否收到过至少一拍温度（role 2/3 齐备才有）。 */
    bool m_received;                    /**< 是否收到过至少一拍。 */
    uint64_t m_lastChangeMs;            /**< 最近一拍时刻（单调毫秒；XDateTime 口径）。 */
    uint32_t m_staleMs;                 /**< 陈旧判定阈值（毫秒）；0=不判陈旧。默认 5000。 */
    bool m_signalAttached;              /**< 组 inputChanged 是否已订阅。 */
    XTimerId m_qualityTimer;            /**< 质量巡检定时器 id；无效时为 XTIMER_INVALID_ID。 */
    bool m_quality;                     /**< 上次发布的质量（翻转沿才发信号）。 */
} XTurbidityMeter;

/* ==================== 虚函数表初始化 / 构造与析构 ==================== */

/**
 * @brief 初始化 XTurbidityMeter 类的虚函数表（单例）。
 * @return 类虚函数表指针；不可释放。
 */
XVtable* XTurbidityMeter_class_init(void);

/**
 * @brief 栈上初始化浊度仪。
 * @param obj 目标对象；不能为 NULL，可为未初始化内存（内部先清零）。
 * @return 无。参数为 NULL 时不执行任何操作。
 * @note 与 XClassDeinit 成对出现；初始未挂接、Words 模式、CDAB + Float32、
 *       除数 100.0、二级标度 1/1/0、陈旧阈值 5000ms（即真机缺省口径）。
 */
void XTurbidityMeter_init(XTurbidityMeter* obj);

/**
 * @brief 堆上创建浊度仪。
 * @return 新对象；内存分配失败返回 NULL。不再使用时必须 XClassDelete；
 *         对象可能仍被信号链引用时改用 XObject_deleteLater。
 */
XTurbidityMeter* XTurbidityMeter_create(void);

/* ==================== 装配（借用注入，解耦点） ==================== */

/**
 * @brief 挂接通道组（按当前数据源形态取点）。
 * @param self 目标浊度仪；不能为 NULL。
 * @param group 通道组（已登记本设备需要的 role）；借用，不能为 NULL。
 * @return 所需 role 齐全且订阅成功返回 true；否则 false（已自动清理）。
 * @note 重复挂接先自动解挂旧组。挂接成功后立即同步读一次播种快照
 *       （有数据即发 valueChanged/qualityChanged）。
 * @note [Words 模式] role 0 **和** role 1 都必须存在——缺一个就无法解出 32 位量，
 *       装配错误必须尽早暴露，不能静默退化成"读 0"。
 * @note [Analog 模式] 只需要 role 0；role 1 有也不读。
 * @note 挂接前先调用 XTurbidityMeter_setSourceMode 选定形态（缺省 Words）。
 */
bool XTurbidityMeter_attach(XTurbidityMeter* self, XIoChannelGroup* group);

/**
 * @brief 解挂全部输入并断开组信号订阅。
 * @param self 目标浊度仪；不能为 NULL。
 * @return 原本已挂接返回 true；未挂接返回 false。
 * @note 解挂后质量转无效（发 qualityChanged(false)），缓存浊度值保留。
 */
bool XTurbidityMeter_detach(XTurbidityMeter* self);

/* ==================== 配置（装配期） ==================== */

/**
 * @brief 设置数据源形态。
 * @param self 目标浊度仪；不能为 NULL。
 * @param mode Words / Analog。
 * @return 设置成功返回 true；模式非法或已挂接（需先 detach）返回 false。
 * @note [为什么已挂接时拒绝] 形态决定"要哪些 role"。挂接期间改形态会留下
 *       一组与新模式不匹配的点表，查询结果将无声地变成 0——宁可让调用方
 *       显式 detach 再改，也不要留一个"看着成功、实际读错"的配置口。
 */
bool XTurbidityMeter_setSourceMode(XTurbidityMeter* self, XTurbidityMeterSourceMode mode);

/**
 * @brief 读数据源形态。
 * @param self 目标浊度仪；可为 NULL（返回 Words）。
 */
XTurbidityMeterSourceMode XTurbidityMeter_sourceMode(const XTurbidityMeter* self);

/**
 * @brief 设置 32 位量的字序（Words 模式）。
 * @param self 目标浊度仪；不能为 NULL。
 * @param order 字序。
 * @return 设置成功返回 true；参数非法返回 false。
 * @note 立即生效：用新字序重算已缓存量，值变化则重发 valueChanged。
 *       现场换一台不同厂家的表时只改这一项，不必改代码或重建对象。
 */
bool XTurbidityMeter_setWordOrder(XTurbidityMeter* self, XTurbidityMeterWordOrder order);

/**
 * @brief 读 32 位量字序。
 */
XTurbidityMeterWordOrder XTurbidityMeter_wordOrder(const XTurbidityMeter* self);

/**
 * @brief 设置 32 位量格式（Words 模式）。
 * @param self 目标浊度仪；不能为 NULL。
 * @param format Float32 / UInt32Scaled / Int32Scaled。
 * @return 设置成功返回 true；参数非法返回 false。
 * @note 立即生效（同 setWordOrder）。
 */
bool XTurbidityMeter_setWordFormat(XTurbidityMeter* self, XTurbidityMeterWordFormat format);

/**
 * @brief 读 32 位量格式。
 */
XTurbidityMeterWordFormat XTurbidityMeter_wordFormat(const XTurbidityMeter* self);

/**
 * @brief 设置整数格式的除数（Words 模式）。
 * @param self 目标浊度仪；不能为 NULL。
 * @param divisor raw / divisor = NTU；> 0（0 与负数拒绝）。
 * @return 设置成功返回 true。
 * @note 立即生效。Float32 格式下本值不参与计算（保留以便切换格式）。
 */
bool XTurbidityMeter_setWordDivisor(XTurbidityMeter* self, double divisor);

/**
 * @brief 读整数格式的除数。
 */
double XTurbidityMeter_wordDivisor(const XTurbidityMeter* self);

/**
 * @brief 设置模拟量的第二级标度（Analog 模式）。
 * @param self 目标浊度仪；不能为 NULL。
 * @param num 分子；den 分母（0 拒绝）；offset 偏移（0.01NTU 定点）。
 * @return 设置成功返回 true；参数非法返回 false，标度保持不变。
 * @details 换算：fixed01 = chEng * num / den + offset，NTU = fixed01 / 100.0。
 *          即本级的**输出单位是 0.01NTU**（与仪表分辨率一致），这样 4-20mA
 *          这类"整数 ADC 码 → 小数浊度"的换算是整数运算，不引入浮点累积误差。
 * @note 立即生效：用新标度重算已缓存值，值变化则重发 valueChanged。
 */
bool XTurbidityMeter_setScale(XTurbidityMeter* self, int32_t num, int32_t den,
                              int32_t offset);

/**
 * @brief 读模拟量第二级标度。
 * @param self 目标浊度仪；可为 NULL（全 0 出参）。
 * @param outNum/outDen/outOffset 输出（调用方提供；可为 NULL 忽略）。
 */
void XTurbidityMeter_scale(const XTurbidityMeter* self, int32_t* outNum,
                           int32_t* outDen, int32_t* outOffset);

/**
 * @brief 设置陈旧判定阈值。
 * @param self 目标浊度仪；不能为 NULL。
 * @param staleMs 阈值毫秒；0=不判陈旧（数据收到过即恒有效）。
 * @return 设置成功返回 true。
 * @note 立即生效：下次质量评估按新阈值（质量翻转经 qualityChanged 发布）。
 */
bool XTurbidityMeter_setStaleMs(XTurbidityMeter* self, uint32_t staleMs);

/**
 * @brief 读取陈旧判定阈值。
 * @param self 目标浊度仪；传入 NULL 返回 0。
 * @return 当前阈值毫秒；0=不判陈旧。
 */
uint32_t XTurbidityMeter_staleMs(const XTurbidityMeter* self);

/* ==================== 查询 ==================== */

/**
 * @brief 读浊度（NTU）。
 * @param self 目标浊度仪；可为 NULL。
 * @param outNtu 输出浊度（调用方提供；可为 NULL——此时仅作有效判定）。
 * @return 数据有效（已收到且未陈旧）返回 true 并写 outNtu；否则返回 false
 *         （outNtu 被写 0）。
 * @note 只读查询，无副作用、不发信号。
 */
bool XTurbidityMeter_value(const XTurbidityMeter* self, double* outNtu);

/**
 * @brief 读水温（℃）。
 * @param self 目标浊度仪；可为 NULL。
 * @param outTempC 输出温度（调用方提供；可为 NULL——此时仅作"有没有"判定）。
 * @return 机型带温度（role 2/3 齐备）且收到过至少一拍返回 true 并写
 *         outTempC；否则 false（outTempC 写 0）。
 * @note  [温度与浊度同刻] 温度字与浊度字在同一帧应答里（×4 按 0x0000×4
 *        一帧读），整帧回填后整拍重算，两者的"最近一拍"天然同源。
 *        [只进设备层/日志] 温度不参与遥测推送帧（0x00010009 形状铁律），
 *        调用方拿它做日志/诊断，不要往推送载荷里加键。
 */
bool XTurbidityMeter_temperature(const XTurbidityMeter* self, double* outTempC);

/**
 * @brief 读 Words 模式的温度 32 位原始位模式（诊断/口径反推用）。
 * @param self 目标浊度仪；可为 NULL。
 * @param outBits 输出位模式（调用方提供；可为 NULL）。
 * @return 当前是 Words 模式且收到过温度返回 true；否则 false（outBits 写 0）。
 * @note 拼装字序与浊度同一配置（m_wordOrder）；raw 与浊度的 rawWords 同理，
 *       是换算口径出错时唯一能反推真相的量。
 */
bool XTurbidityMeter_rawTemperature(const XTurbidityMeter* self, uint32_t* outBits);

/**
 * @brief 查数据质量（等价 value 的返回值）。
 * @param self 目标浊度仪；可为 NULL。
 * @return 有效返回 true。
 */
bool XTurbidityMeter_isValid(const XTurbidityMeter* self);

/**
 * @brief 查最近一拍时刻。
 * @param self 目标浊度仪；可为 NULL。
 * @param outMs 输出单调毫秒（调用方提供；可为 NULL）。
 * @return 收到过数据返回 true；从未收到返回 false（outMs 写 0）。
 */
bool XTurbidityMeter_lastUpdateMs(const XTurbidityMeter* self, uint64_t* outMs);

/**
 * @brief 读 Words 模式的 32 位原始位模式（诊断/口径反推用）。
 * @param self 目标浊度仪；可为 NULL。
 * @param outBits 输出拼装结果（调用方提供；可为 NULL）。
 * @return 当前是 Words 模式且收到过数据返回 true；否则 false（outBits 写 0）。
 * @details [为什么要暴露 raw] 换算口径一旦不对，raw 是唯一能反推真相的量：
 *          先看 raw 是否在合理范围，再判字序/格式/除数哪一项错了。
 *          （同 XFlowMeter 保留 raw 的理由。）
 */
bool XTurbidityMeter_rawWords(const XTurbidityMeter* self, uint32_t* outBits);

/**
 * @brief 读 Analog 模式的通道工程值（诊断用）。
 * @param self 目标浊度仪；可为 NULL。
 * @param outEng 输出通道工程值（调用方提供；可为 NULL）。
 * @return 当前是 Analog 模式且收到过数据返回 true；否则 false（outEng 写 0）。
 */
bool XTurbidityMeter_rawAnalog(const XTurbidityMeter* self, int32_t* outEng);

/* ==================== 信号 ==================== */

/**
 * @brief valueChanged 信号：浊度值更新（含挂接播种）。
 * @param self 发射信号的浊度仪；经 XSignal() 宏取地址连接，禁止直调。
 * @param ntu 变化后的浊度（NTU）。
 * @return 信号函数地址；仅供 XSignal() 宏取地址使用。
 * @note 只在值**真变**时发射；恒定浊度的心跳由采样拍维护质量与时刻，
 *       不制造无意义的信号（与 XFlowMeter 整帧通知的反差见下）。
 * @note [为什么不像 XFlowMeter 那样发 quantity 参数] 本设备只有一个物理量，
 *       没有"整帧多量"的扇出问题，故信号带值最省事、接收方也不必回查。
 */
void* XTurbidityMeter_valueChanged_signal(XTurbidityMeter* self, double ntu);

/**
 * @brief qualityChanged 信号：质量翻转（有效 ⇄ 无效/陈旧）。
 * @param self 发射信号的浊度仪；经 XSignal() 宏取地址连接，禁止直调。
 * @param valid 翻转后的质量。
 * @return 信号函数地址；仅供 XSignal() 宏取地址使用。
 * @note 质量评估在数据拍与巡检定时器两处触发，仅翻转沿发射。
 */
void* XTurbidityMeter_qualityChanged_signal(XTurbidityMeter* self, bool valid);

/**
 * @brief errorOccurred 信号：浊度仪发生错误。
 * @param self 发射信号的浊度仪；经 XSignal() 宏取地址连接，禁止直调。
 * @param error 错误码（XTurbidityMeterError 枚举值）。
 * @return 信号函数地址；仅供 XSignal() 宏取地址使用。
 */
void* XTurbidityMeter_errorOccurred_signal(XTurbidityMeter* self, int error);

#endif /* XINDUSTRY_ON && XTURBIDITY_ON */

#ifdef __cplusplus
}
#endif
#endif /* XTURBIDITYMETER_H */
