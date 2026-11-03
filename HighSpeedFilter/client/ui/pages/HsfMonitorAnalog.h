/**
 * @file       HsfMonitorAnalog.h
 * @brief      模拟量监视页：数值卡网格（原始值 + 转换值 + 单位 + 通道号）
 *             + 统一操作闸 + 全量 API。
 * @details    页面为「看板」：模拟量点位的值/名称/单位/量程等由业务层经
 *             本头公开 API 写入（内存模型随点数惰性重建）。每张卡自绘：
 *             通道号+名称（第一行）、原始值（第二行，整型口径）、转换值
 *             + 单位（第三行，工程量 double，按小数位格式化；量程越限
 *             显示报警红）。看板按列自适应排布（列数随视口宽实时反解，
 *             或 setColumns 强制），复用主窗统一滚动壳。
 *             操作闸与「自动运行锁界面」语义同数字量页：API 可操作 +
 *             闸放行时点击卡片经 HsfMonitorAnalog_setWriteHandler 回调
 *             业务层（UI 不落业务语义）。
 *             兼容自测钩子：XHSF_ANALOG_AUTOTEST / XHSF_ANALOG_AUTO
 *             （环境变量触发，走公开 API 验证回显与点击链路）。
 */
#ifndef HSFMONITORANALOG_H
#define HSFMONITORANALOG_H

#ifdef __cplusplus
extern "C" {
#endif

#include "CXinYueConfig.h"
#include "XWidget.h"
#include "HsfPages.h"

#if XWIDGET_ON && XLABEL_ON

/**
 * @brief 单点模拟量类型（输入/输出，决定 UI 形态与可否界面操作）。
 * @details 输入（AI）只读监视：卡上标「AI」，点击不产生写操作；
 *          输出（AO）可操作：卡上标「AO」，统一闸放行时点击经写回调
 *          出 UI（业务层收到后自行决定动作——弹设定值输入/步进调节等，
 *          确认后经 setValue/setScaled 回显）。新建点缺省 AI。
 */
typedef enum HsfAnalogKind
{
    HSF_ANALOG_INPUT  = 0, /**< 输入（AI）：只读监视。 */
    HSF_ANALOG_OUTPUT = 1  /**< 输出（AO）：可操作（仍受统一闸约束）。 */
} HsfAnalogKind;

/**
 * @brief      AO 点写请求回调（用户点击 AO 卡且统一闸放行时触发；
 *             业务层在此对接设定值下发/弹输入等，UI 不预设语义）。
 * @param userData setWriteHandler 透传的用户数据。
 * @param index  点位下标（0 起始，恒为 [0,count) 内合法值）。
 * @note  UI 侧不自动改值：业务层确认后应显式回写
 *        HsfMonitorAnalog_setValue/setScaled（真实通道是异步的）。
 */
typedef void (*HsfAnalogWriteFn)(void* userData, int index);

/**
 * @brief      AI/AO 卡片点击回调（统一闸放行时触发；含 AO 写请求与
 *             AI 点查看动作——业务层按点的类型自行分流）。
 * @param userData setWriteHandler 透传的用户数据。
 * @param index  点位下标（0 起始，恒为 [0,count) 内合法值）。
 */
typedef void (*HsfAnalogTapFn)(void* userData, int index);

/**
 * @brief      创建模拟量叶子页（堆分配，挂 parent，随主窗级联释放）。
 * @param parent 父控件（堆栈宿主）。
 * @param navFn 导航回调（builders 契约保留；本页返回语义由导航树承担）。
 * @param userData 透传回调的用户数据。
 * @return 页根控件借用指针；失败 NULL。
 */
XWidget* HsfMonitorAnalog_create(XWidget* parent, HsfMenuNavFn navFn,
                                 void* userData);

/**
 * @brief      形态自适应（HsfUi adapt 表按页调用；横竖屏分档）。
 * @details    竖屏：列最小宽收窄/卡片触控加高/边距收窄；横屏恢复。
 *             幂等、廉价，可重复调用。
 * @param page 页根控件（HsfMonitorAnalog_create 返回值）。
 * @param compact true=紧凑/竖屏形态（false=宽屏）。
 */
void HsfMonitorAnalog_adapt(XWidget* page, bool compact);

/* ---- 数据供给 API（业务层唯一入口；调用前页可未创建，安全空操作） ---- */

/**
 * @brief      设置模拟量点数（读写）。
 * @details    模型惰性重建：扩点新增 0 值缺省卡、缩点截尾；已建页立即
 *             重排重绘。
 * @param count 新点数；<=0 视为 0（清空看板）。
 */
void HsfMonitorAnalog_setCount(int count);

/** @brief 取当前模拟量点数（未建页返回 0）。 */
int HsfMonitorAnalog_count(void);

/**
 * @brief      一步写双值（常用口：业务层一次拿到原始+转换时用）。
 * @param index 点位下标；越界忽略。
 * @param raw   原始值（整型口径：ADC 码值/寄存器原始字）。
 * @param scaled 转换值（工程量 double：按变比/标定换算后）。
 */
void HsfMonitorAnalog_setValue(int index, int raw, double scaled);

/** @brief 写原始值（只动原始值，转换值不变）。越界忽略。 */
void HsfMonitorAnalog_setRaw(int index, int raw);

/** @brief 读原始值；越界返回 0。 */
int HsfMonitorAnalog_raw(int index);

/** @brief 写转换值（只动转换值，原始值不变）。越界忽略。 */
void HsfMonitorAnalog_setScaled(int index, double scaled);

/** @brief 读转换值；越界返回 0.0。 */
double HsfMonitorAnalog_scaled(int index);

/**
 * @brief      写单点名称（UTF-8；内部深拷贝）。越界忽略。
 * @param index 点位下标。
 * @param name  名称文本；NULL 置空（显示退化为「点 N」缺省名）。
 */
void HsfMonitorAnalog_setName(int index, const char* name);

/**
 * @brief      读单点名称到调用方缓冲。
 * @param index 点位下标；越界输出缺省名「点 N」。
 * @param buf   输出缓冲（UTF-8，恒 NUL 收尾）；NULL 忽略。
 * @param size  缓冲字节数；<=0 忽略。
 */
void HsfMonitorAnalog_name(int index, char* buf, int size);

/**
 * @brief      写单点单位（UTF-8；内部深拷贝；如 "℃"/"kPa"/"L/min"）。
 * @param index 点位下标；越界忽略。
 * @param unit  单位文本；NULL/空 串清除（转换值后不显示单位）。
 */
void HsfMonitorAnalog_setUnit(int index, const char* unit);

/**
 * @brief      读单点单位到调用方缓冲。
 * @param index 点位下标；未设置输出空串。
 * @param buf   输出缓冲（UTF-8，恒 NUL 收尾）；NULL 忽略。
 * @param size  缓冲字节数；<=0 忽略。
 */
void HsfMonitorAnalog_unit(int index, char* buf, int size);

/**
 * @brief      写单点通道号（显示在名称前，如「CH3 泵前压力」）。
 * @param index 点位下标；越界忽略。
 * @param channel 通道号；<0 清除（不显示通道前缀）。
 */
void HsfMonitorAnalog_setChannel(int index, int channel);

/** @brief 读单点通道号；越界或未设置返回 -1。 */
int HsfMonitorAnalog_channel(int index);

/**
 * @brief      写单点转换值小数位（显示格式化；0=整数，2=两位小数）。
 * @details    原始值恒整数显示；本位只作用于转换值。>6 按 6 钳口。
 * @param index 点位下标；越界忽略。
 * @param decimals 小数位（0..6）；负数按 0。
 */
void HsfMonitorAnalog_setDecimals(int index, int decimals);

/** @brief 读单点小数位；越界返回 2（缺省两位）。 */
int HsfMonitorAnalog_decimals(int index);

/**
 * @brief      写单点量程（转换值口径；越限时转换值显示报警红）。
 * @details    min>=max 视为不设限（清除报警判定）。
 * @param index 点位下标；越界忽略。
 * @param min   量程下限（工程量）。
 * @param max   量程上限（工程量）。
 */
void HsfMonitorAnalog_setRange(int index, double min, double max);

/**
 * @brief      读单点量程。
 * @param index 点位下标。
 * @param minOut 量程下限出参；可为 NULL。
 * @param maxOut 量程上限出参；可为 NULL。
 * @return true=已设量程（会做越限判定）；false=未设（越界/未设置）。
 */
bool HsfMonitorAnalog_range(int index, double* minOut, double* maxOut);

/**
 * @brief      写单点类型（读写 API；AI=只读 / AO=可操作）。
 * @details    新建点缺省 AI（只读监视）；卡上按类型标 AI/AO 角标。
 * @param index 点位下标；越界忽略。
 * @param kind  目标类型。
 */
void HsfMonitorAnalog_setKind(int index, HsfAnalogKind kind);

/** @brief 读单点类型；越界返回 HSF_ANALOG_INPUT。 */
HsfAnalogKind HsfMonitorAnalog_kind(int index);

/**
 * @brief      强制列数（布局 API）。
 * @details    0=自动（按视口宽与列最小宽反解，缺省）；>0=固定列数
 *             （1..8 钳口）；竖屏分档仍按 adapt 收窄列最小宽。
 * @param columns 列数；<0 按 0（自动）。
 */
void HsfMonitorAnalog_setColumns(int columns);

/** @brief 读强制列数（0=自动）。 */
int HsfMonitorAnalog_columns(void);

/**
 * @brief      统一操作闸（读写 API）：是否允许界面点击卡片出动作。
 * @details    false（缺省）时点击卡片仅在状态栏提示「操作已禁止」；
 *             true 时点击经写回调出 UI。只影响操作放行，不影响回显。
 * @param enabled true=允许界面操作；false=只读监视。
 */
void HsfMonitorAnalog_setOperationEnabled(bool enabled);

/** @brief 查询统一操作闸当前状态。 */
bool HsfMonitorAnalog_operationEnabled(void);

/**
 * @brief      界面交互模式（读写 API；对标「自动运行锁界面」场景）。
 * @details    interactive=false（自动运行中）：勾选框整体隐藏、操作闸
 *             强制只读（API setOperationEnabled 也被锁），看板仅可
 *             查看，点击卡片在状态栏提示「自动运行中: 界面只读」；
 *             interactive=true（缺省）：恢复显示勾选框，闸值回到勾选框
 *             此前状态。仅影响操作放行与闸 UI 显隐，不影响状态回显。
 * @param interactive true=允许界面交互（手动模式）；false=自动运行只读。
 */
void HsfMonitorAnalog_setInteractiveMode(bool interactive);

/** @brief 查询界面交互模式（true=可交互；false=自动运行只读）。 */
bool HsfMonitorAnalog_interactiveMode(void);

/**
 * @brief      注册卡片交互出口（UI → 业务层唯一通道；多次注册以最后
 *             为准）。点击任意卡片时触发：AI 点=查看请求，AO 点=写请求
 *             （业务层按 HsfMonitorAnalog_kind 分流；统一闸放行才有）。
 * @param handler 点击回调；NULL=注销（此时即使闸放行也不出 UI）。
 * @param userData 透传回调的用户数据。
 */
void HsfMonitorAnalog_setTapHandler(HsfAnalogTapFn handler, void* userData);

/**
 * @brief      状态栏反馈挂接（由 HsfUi.c 在建页后调用；业务勿用）。
 * @param setStatus HsfPages_setStatus 同款回调；可 NULL（断开反馈）。
 * @param user     归属主窗指针。
 */
void HsfMonitorAnalog_attachStatus(void (*setStatus)(void*, const char*),
                                   void* user);

#if XGUI_ON
/**
 * @brief      兼容自测钩子（截图/CI 用；业务勿用）。
 * @details    环境变量 XHSF_ANALOG_AUTOTEST=1 时执行：走公开 API 灌
 *             12 点演示数据（名称/通道号/单位/量程越限样例），操作闸
 *             置开，返回写入点数；XHSF_ANALOG_AUTO=1 额外进自动模式
 *             （验证锁界面 UI）。未触发返回 -1。仅改 UI 模型。
 * @param page 页根控件（HsfMonitorAnalog_create 返回值；仅校验非 NULL）。
 * @return     执行时灌入的点数（>=0）；未触发 -1。
 */
int HsfMonitorAnalog_autotest(XWidget* page);
#endif /* XGUI_ON */

#endif /* XWIDGET_ON && XLABEL_ON */

#ifdef __cplusplus
}
#endif

#endif /* HSFMONITORANALOG_H */
