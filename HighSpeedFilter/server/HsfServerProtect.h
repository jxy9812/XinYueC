/**
 * @file       HsfServerProtect.h
 * @brief      服务端运行保护（protect 节）+ 流量计告警事件（2026-11-15）。
 * @details    Python 事实源：server_py/hsfsrv/app.py 的 `_protect_step` /
 *             `_flowmeter_alarm_step`（与本模块逐条同口径）。
 *
 *             [这一节是什么] 设备侧的**保护联锁**：进水泵开着、流量计却一直
 *             报"空管" ⇒ 说明水路没通（或流量计坏了）⇒ 到时限就按配置动作
 *             （停设备 / 只报警）。另含**流量计告警的边沿上报**（报警位
 *             出现/消失各一条事件 ⇒ 落客户端报警表 + alarms 表）。
 *
 *             [为什么独立成模块] 它既不是设备层（要跨设备比较），也不是
 *             app 的界面逻辑；独立出来才能被**回归锁直接驱动**（app 层的
 *             静态函数测试够不着）。
 *
 *             [与 App 的分工] 本模块只**算**事件（不碰网络、不碰数据库）：
 *             事件交回 App 去发布/入库（App 持有 XLinkServer 与 store）。
 */
#ifndef HSF_SERVER_PROTECT_H
#define HSF_SERVER_PROTECT_H

#include <stdbool.h>
#include <stdint.h>

/* [前向声明] 应用 protect 配置要用 BSON 文档（XBsonDocument 不透明）。 */
struct XBsonDocument;

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 一条保护/告警事件（由 App 发布到 TOPIC_AUTO_ALARM 并落 alarms 表）。
 */
typedef struct HsfServerProtectEvent {
    bool m_hasAlarm;   /**< 本拍有事件（false = 无事发生）。 */
    int  m_kind;       /**< 告警类型（HSF_AUTO_ALARM_*）。 */
    int  m_devKind;    /**< 设备类别（HsfDeviceKind_*；-1 = 不带该键）。 */
    char m_text[160];  /**< 告警文字（UTF-8，给操作员看）。 */
} HsfServerProtectEvent;

/**
 * @brief 压差过大保护：是否启用（缺省 false = 不启用）。
 * @details App 的配置 GET/SET 回显走这里（**单一事实源**：读的就是保护
 *          自己判定时读的那份配置）。
 */
bool HsfServerProtect_pressHighEnabled(void);

/**
 * @brief 压差上限（MPa；缺省 0.8 —— 正常运行约 0.5 MPa）。
 */
double HsfServerProtect_pressHighLimitMpa(void);

/**
 * @brief 压差过大触发后是否停止设备（缺省 true = 停）。
 */
bool HsfServerProtect_pressHighStop(void);

/**
 * @brief 压差取数回调（MPa）。
 * @details 与自动循环的压差取数**同一条换算链**（模拟量通道 0 + 工程量
 *          标定）——界面压差卡、历史 press_mpa、循环切相阈值、这里的保护
 *          阈值必须是同一个 MPa 口径，否则两处会漂移（Python 侧
 *          ctx.press_mpa() 同款设计）。
 * @param outMpa 输出（MPa）。
 * @return 读到有效值返回 true；通道离线/未标定返回 false（**不按缺数据
 *         误判**：拿"没数据"当"压差过大"会把正在生产的线停掉）。
 */
typedef bool (*HsfServerProtectPressFn)(double* outMpa);

/**
 * @brief 注入压差取数回调（装配期由 App 调一次；NULL = 无压差数据 ⇒
 *        压差保护不判定）。
 */
void HsfServerProtect_setPressureFn(HsfServerProtectPressFn fn);

/**
 * @brief 跑一拍保护判定与流量计告警边沿。
 * @param nowMs 当前时刻（单调毫秒）。
 * @param out   事件输出（可为 NULL = 只跑判定不要事件）。
 * @return 本拍是否产生事件（等价 out->m_hasAlarm）。
 * @details 判据（与 Python `_protect_step` 逐条同口径）：
 *          ① 进水泵在开（DO 回显）；② 流量计**在线且数据新鲜**并报**空管**；
 *          ③ ①∧② **连续**成立 ≥ `inletNoFlowTimeoutMs`（0 = 关闭保护）。
 *          [为什么算"连续成立"的时长] 泵开着很久、流量计此刻才闪一次空管
 *          位 ⇒ 按"泵开时长"判会立刻停线 ⇒ 必须**连续**空管满一个时限。
 *          [读不到 ≠ 空管] 流量计离线/陈旧 ⇒ 不判定（拿"没数据"当"空管"
 *          会把整条线停掉）。
 *          [动作] 到时限 ⇒ 报一条告警 + 按配置**停止设备**
 *          （`inletNoFlowStop`：true=停 / false=只报警）。
 */
bool HsfServerProtect_tick(uint64_t nowMs, HsfServerProtectEvent* out);

/**
 * @brief 运行保护配置读：进水泵空管超时（毫秒；0 = 关闭该保护）。
 * @details 读 INI 的 protect/inletNoFlowTimeoutMs（缺键=缺省=0=关闭；
 *          越界夹到合法区间）。App 的配置 GET/SET 回显也用它 ⇒ **单一
 *          事实源**（不在 app 侧再抄一份）。
 */
int HsfServerProtect_timeoutMs(void);

/**
 * @brief 运行保护动作是否**停止设备**（true=停；缺省 true）。
 * @details 读 INI 的 protect/inletNoFlowStop。
 */
bool HsfServerProtect_stopDevices(void);

/**
 * @brief 清空保护模块的内部状态（测试复位用：报警位基线与计时器）。
 */
void HsfServerProtect_reset(void);

/**
 * @brief 应用 protect 节的一整包配置（**fail-closed**，2026-11-15）。
 * @param doc 载荷文档（键 = 协议键名；"section" 键忽略）。
 * @param err 出错文本（可为 NULL；出错时写入）。
 * @param cap  err 缓冲区容量。
 * @return true = 全部合法且**已写入**；false = 有未知键/非法类型
 *         （**一个键都没写** ⇒ 不留半截配置）。
 * @details 纪律与 Python 侧 `_cfg_set_protect_key` + `_cfg_apply_batch`
 *          同口径：
 *          · **未知键明确报错**（原来静默忽略 ⇒ 客户端拼错键以为存了）；
 *          · **类型不符明确报错**（`XBsonValue_toBool(v, true)` 对字符串
 *            "false" 返回**缺省 true** ⇒ 本意"只报警不停机"变成"直接
 *            停机"）；
 *          · **整包校验通过才写**（不留半截配置）。
 *          独立成函数是为了让**回归锁能直接驱动**（App 层的命令处理函数
 *          是 static，测试够不着）。
 */
bool HsfServerProtect_applyConfigDoc(const struct XBsonDocument* doc,
                                     char* err, int cap);

#ifdef __cplusplus
}
#endif
#endif /* HSF_SERVER_PROTECT_H */
